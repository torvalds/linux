// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2021 NXP */

#include <dt-bindings/firmware/imx/rsrc.h>
#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/firmware/imx/sci.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/remoteproc.h>
#include <linux/slab.h>

#include "imx_rproc.h"
#include "remoteproc_elf_helpers.h"
#include "remoteproc_internal.h"

#define DSP_RPROC_CLK_MAX			5

#define REMOTE_IS_READY				BIT(0)
#define REMOTE_READY_WAIT_MAX_RETRIES		500

/* att flags */
/* DSP own area */
#define ATT_OWN					BIT(31)
/* DSP instruction area */
#define ATT_IRAM				BIT(30)

/* Definitions for i.MX8MP */
/* DAP registers */
#define IMX8M_DAP_DEBUG				0x28800000
#define IMX8M_DAP_DEBUG_SIZE			(64 * 1024)
#define IMX8M_DAP_PWRCTL			(0x4000 + 0x3020)
#define IMX8M_PWRCTL_CORERESET			BIT(16)

/* DSP audio mix registers */
#define IMX8M_AudioDSP_REG0			0x100
#define IMX8M_AudioDSP_REG1			0x104
#define IMX8M_AudioDSP_REG2			0x108
#define IMX8M_AudioDSP_REG3			0x10c

#define IMX8M_AudioDSP_REG2_RUNSTALL		BIT(5)
#define IMX8M_AudioDSP_REG2_PWAITMODE		BIT(1)

/* Definitions for i.MX8ULP */
#define IMX8ULP_SIM_LPAV_REG_SYSCTRL0		0x8
#define IMX8ULP_SYSCTRL0_DSP_DBG_RST		BIT(25)
#define IMX8ULP_SYSCTRL0_DSP_PLAT_CLK_EN	BIT(19)
#define IMX8ULP_SYSCTRL0_DSP_PBCLK_EN		BIT(18)
#define IMX8ULP_SYSCTRL0_DSP_CLK_EN		BIT(17)
#define IMX8ULP_SYSCTRL0_DSP_RST		BIT(16)
#define IMX8ULP_SYSCTRL0_DSP_OCD_HALT		BIT(14)
#define IMX8ULP_SYSCTRL0_DSP_STALL		BIT(13)

#define IMX8ULP_SIP_HIFI_XRDC			0xc200000e

/*
 * enum - Predefined Mailbox Messages
 *
 * @RP_MBOX_SUSPEND_SYSTEM: system suspend request for the remote processor
 *
 * @RP_MBOX_SUSPEND_ACK: successful response from remote processor for a
 * suspend request
 *
 * @RP_MBOX_RESUME_SYSTEM: system resume request for the remote processor
 *
 * @RP_MBOX_RESUME_ACK: successful response from remote processor for a
 * resume request
 */
enum imx_dsp_rp_mbox_messages {
	RP_MBOX_SUSPEND_SYSTEM			= 0xFF11,
	RP_MBOX_SUSPEND_ACK			= 0xFF12,
	RP_MBOX_RESUME_SYSTEM			= 0xFF13,
	RP_MBOX_RESUME_ACK			= 0xFF14,
};

/**
 * struct imx_dsp_rproc - DSP remote processor state
 * @regmap: regmap handler
 * @rproc: rproc handler
 * @dsp_dcfg: device configuration pointer
 * @clks: clocks needed by this device
 * @cl: mailbox client to request the mailbox channel
 * @cl_rxdb: mailbox client to request the mailbox channel for doorbell
 * @tx_ch: mailbox tx channel handle
 * @rx_ch: mailbox rx channel handle
 * @rxdb_ch: mailbox rx doorbell channel handle
 * @pd_dev: power domain device
 * @pd_dev_link: power domain device link
 * @ipc_handle: System Control Unit ipc handle
 * @rproc_work: work for processing virtio interrupts
 * @pm_comp: completion primitive to sync for suspend response
 * @num_domains: power domain number
 * @flags: control flags
 */
struct imx_dsp_rproc {
	struct regmap				*regmap;
	struct rproc				*rproc;
	const struct imx_dsp_rproc_dcfg		*dsp_dcfg;
	struct clk_bulk_data			clks[DSP_RPROC_CLK_MAX];
	struct mbox_client			cl;
	struct mbox_client			cl_rxdb;
	struct mbox_chan			*tx_ch;
	struct mbox_chan			*rx_ch;
	struct mbox_chan			*rxdb_ch;
	struct device				**pd_dev;
	struct device_link			**pd_dev_link;
	struct imx_sc_ipc			*ipc_handle;
	struct work_struct			rproc_work;
	struct completion			pm_comp;
	int					num_domains;
	u32					flags;
};

/**
 * struct imx_dsp_rproc_dcfg - DSP remote processor configuration
 * @dcfg: imx_rproc_dcfg handler
 * @reset: reset callback function
 */
struct imx_dsp_rproc_dcfg {
	const struct imx_rproc_dcfg		*dcfg;
	int (*reset)(struct imx_dsp_rproc *priv);
};

static const struct imx_rproc_att imx_dsp_rproc_att_imx8qm[] = {
	/* dev addr , sys addr  , size	    , flags */
	{ 0x596e8000, 0x556e8000, 0x00008000, ATT_OWN },
	{ 0x596f0000, 0x556f0000, 0x00008000, ATT_OWN },
	{ 0x596f8000, 0x556f8000, 0x00000800, ATT_OWN | ATT_IRAM},
	{ 0x55700000, 0x55700000, 0x00070000, ATT_OWN },
	/* DDR (Data) */
	{ 0x80000000, 0x80000000, 0x60000000, 0},
};

static const struct imx_rproc_att imx_dsp_rproc_att_imx8qxp[] = {
	/* dev addr , sys addr  , size	    , flags */
	{ 0x596e8000, 0x596e8000, 0x00008000, ATT_OWN },
	{ 0x596f0000, 0x596f0000, 0x00008000, ATT_OWN },
	{ 0x596f8000, 0x596f8000, 0x00000800, ATT_OWN | ATT_IRAM},
	{ 0x59700000, 0x59700000, 0x00070000, ATT_OWN },
	/* DDR (Data) */
	{ 0x80000000, 0x80000000, 0x60000000, 0},
};

static const struct imx_rproc_att imx_dsp_rproc_att_imx8mp[] = {
	/* dev addr , sys addr  , size	    , flags */
	{ 0x3b6e8000, 0x3b6e8000, 0x00008000, ATT_OWN },
	{ 0x3b6f0000, 0x3b6f0000, 0x00008000, ATT_OWN },
	{ 0x3b6f8000, 0x3b6f8000, 0x00000800, ATT_OWN | ATT_IRAM},
	{ 0x3b700000, 0x3b700000, 0x00040000, ATT_OWN },
	/* DDR (Data) */
	{ 0x40000000, 0x40000000, 0x80000000, 0},
};

static const struct imx_rproc_att imx_dsp_rproc_att_imx8ulp[] = {
	/* dev addr , sys addr  , size	    , flags */
	{ 0x21170000, 0x21170000, 0x00010000, ATT_OWN | ATT_IRAM},
	{ 0x21180000, 0x21180000, 0x00010000, ATT_OWN },
	/* DDR (Data) */
	{ 0x0c000000, 0x80000000, 0x10000000, 0},
	{ 0x30000000, 0x90000000, 0x10000000, 0},
};

/* Reset function for DSP on i.MX8MP */
static int imx8mp_dsp_reset(struct imx_dsp_rproc *priv)
{
	void __iomem *dap = ioremap_wc(IMX8M_DAP_DEBUG, IMX8M_DAP_DEBUG_SIZE);
	int pwrctl;

	/* Put DSP into reset and stall */
	pwrctl = readl(dap + IMX8M_DAP_PWRCTL);
	pwrctl |= IMX8M_PWRCTL_CORERESET;
	writel(pwrctl, dap + IMX8M_DAP_PWRCTL);

	/* Keep reset asserted for 10 cycles */
	usleep_range(1, 2);

	regmap_update_bits(priv->regmap, IMX8M_AudioDSP_REG2,
			   IMX8M_AudioDSP_REG2_RUNSTALL,
			   IMX8M_AudioDSP_REG2_RUNSTALL);

	/* Take the DSP out of reset and keep stalled for FW loading */
	pwrctl = readl(dap + IMX8M_DAP_PWRCTL);
	pwrctl &= ~IMX8M_PWRCTL_CORERESET;
	writel(pwrctl, dap + IMX8M_DAP_PWRCTL);

	iounmap(dap);
	return 0;
}

/* Reset function for DSP on i.MX8ULP */
static int imx8ulp_dsp_reset(struct imx_dsp_rproc *priv)
{
	struct arm_smccc_res res;

	/* Put DSP into reset and stall */
	regmap_update_bits(priv->regmap, IMX8ULP_SIM_LPAV_REG_SYSCTRL0,
			   IMX8ULP_SYSCTRL0_DSP_RST, IMX8ULP_SYSCTRL0_DSP_RST);
	regmap_update_bits(priv->regmap, IMX8ULP_SIM_LPAV_REG_SYSCTRL0,
			   IMX8ULP_SYSCTRL0_DSP_STALL,
			   IMX8ULP_SYSCTRL0_DSP_STALL);

	/* Configure resources of DSP through TFA */
	arm_smccc_smc(IMX8ULP_SIP_HIFI_XRDC, 0, 0, 0, 0, 0, 0, 0, &res);

	/* Take the DSP out of reset and keep stalled for FW loading */
	regmap_update_bits(priv->regmap, IMX8ULP_SIM_LPAV_REG_SYSCTRL0,
			   IMX8ULP_SYSCTRL0_DSP_RST, 0);
	regmap_update_bits(priv->regmap, IMX8ULP_SIM_LPAV_REG_SYSCTRL0,
			   IMX8ULP_SYSCTRL0_DSP_DBG_RST, 0);

	return 0;
}

/* Specific configuration for i.MX8MP */
static const struct imx_rproc_dcfg dsp_rproc_cfg_imx8mp = {
	.src_reg	= IMX8M_AudioDSP_REG2,
	.src_mask	= IMX8M_AudioDSP_REG2_RUNSTALL,
	.src_start	= 0,
	.src_stop	= IMX8M_AudioDSP_REG2_RUNSTALL,
	.att		= imx_dsp_rproc_att_imx8mp,
	.att_size	= ARRAY_SIZE(imx_dsp_rproc_att_imx8mp),
	.method		= IMX_RPROC_MMIO,
};

static const struct imx_dsp_rproc_dcfg imx_dsp_rproc_cfg_imx8mp = {
	.dcfg		= &dsp_rproc_cfg_imx8mp,
	.reset          = imx8mp_dsp_reset,
};

/* Specific configuration for i.MX8ULP */
static const struct imx_rproc_dcfg dsp_rproc_cfg_imx8ulp = {
	.src_reg	= IMX8ULP_SIM_LPAV_REG_SYSCTRL0,
	.src_mask	= IMX8ULP_SYSCTRL0_DSP_STALL,
	.src_start	= 0,
	.src_stop	= IMX8ULP_SYSCTRL0_DSP_STALL,
	.att		= imx_dsp_rproc_att_imx8ulp,
	.att_size	= ARRAY_SIZE(imx_dsp_rproc_att_imx8ulp),
	.method		= IMX_RPROC_MMIO,
};

static const struct imx_dsp_rproc_dcfg imx_dsp_rproc_cfg_imx8ulp = {
	.dcfg		= &dsp_rproc_cfg_imx8ulp,
	.reset          = imx8ulp_dsp_reset,
};

/* Specific configuration for i.MX8QXP */
static const struct imx_rproc_dcfg dsp_rproc_cfg_imx8qxp = {
	.att		= imx_dsp_rproc_att_imx8qxp,
	.att_size	= ARRAY_SIZE(imx_dsp_rproc_att_imx8qxp),
	.method		= IMX_RPROC_SCU_API,
};

static const struct imx_dsp_rproc_dcfg imx_dsp_rproc_cfg_imx8qxp = {
	.dcfg		= &dsp_rproc_cfg_imx8qxp,
};

/* Specific configuration for i.MX8QM */
static const struct imx_rproc_dcfg dsp_rproc_cfg_imx8qm = {
	.att		= imx_dsp_rproc_att_imx8qm,
	.att_size	= ARRAY_SIZE(imx_dsp_rproc_att_imx8qm),
	.method		= IMX_RPROC_SCU_API,
};

static const struct imx_dsp_rproc_dcfg imx_dsp_rproc_cfg_imx8qm = {
	.dcfg		= &dsp_rproc_cfg_imx8qm,
};

static int imx_dsp_rproc_ready(struct rproc *rproc)
{
	struct imx_dsp_rproc *priv = rproc->priv;
	int i;

	if (!priv->rxdb_ch)
		return 0;

	for (i = 0; i < REMOTE_READY_WAIT_MAX_RETRIES; i++) {
		if (priv->flags & REMOTE_IS_READY)
			return 0;
		usleep_range(100, 200);
	}

	return -ETIMEDOUT;
}

/*
 * Start function for rproc_ops
 *
 * There is a handshake for start procedure: when DSP starts, it
 * will send a doorbell message to this driver, then the
 * REMOTE_IS_READY flags is set, then driver will kick
 * a message to DSP.
 */
static int imx_dsp_rproc_start(struct rproc *rproc)
{
	struct imx_dsp_rproc *priv = rproc->priv;
	const struct imx_dsp_rproc_dcfg *dsp_dcfg = priv->dsp_dcfg;
	const struct imx_rproc_dcfg *dcfg = dsp_dcfg->dcfg;
	struct device *dev = rproc->dev.parent;
	int ret;

	switch (dcfg->method) {
	case IMX_RPROC_MMIO:
		ret = regmap_update_bits(priv->regmap,
					 dcfg->src_reg,
					 dcfg->src_mask,
					 dcfg->src_start);
		break;
	case IMX_RPROC_SCU_API:
		ret = imx_sc_pm_cpu_start(priv->ipc_handle,
					  IMX_SC_R_DSP,
					  true,
					  rproc->bootaddr);
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (ret)
		dev_err(dev, "Failed to enable remote core!\n");
	else
		ret = imx_dsp_rproc_ready(rproc);

	return ret;
}

/*
 * Stop function for rproc_ops
 * It clears the REMOTE_IS_READY flags
 */
static int imx_dsp_rproc_stop(struct rproc *rproc)
{
	struct imx_dsp_rproc *priv = rproc->priv;
	const struct imx_dsp_rproc_dcfg *dsp_dcfg = priv->dsp_dcfg;
	const struct imx_rproc_dcfg *dcfg = dsp_dcfg->dcfg;
	struct device *dev = rproc->dev.parent;
	int ret = 0;

	/* Make sure work is finished */
	flush_work(&priv->rproc_work);

	if (rproc->state == RPROC_CRASHED) {
		priv->flags &= ~REMOTE_IS_READY;
		return 0;
	}

	switch (dcfg->method) {
	case IMX_RPROC_MMIO:
		ret = regmap_update_bits(priv->regmap, dcfg->src_reg, dcfg->src_mask,
					 dcfg->src_stop);
		break;
	case IMX_RPROC_SCU_API:
		ret = imx_sc_pm_cpu_start(priv->ipc_handle,
					  IMX_SC_R_DSP,
					  false,
					  rproc->bootaddr);
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (ret)
		dev_err(dev, "Failed to stop remote core\n");
	else
		priv->flags &= ~REMOTE_IS_READY;

	return ret;
}

/**
 * imx_dsp_rproc_sys_to_da() - internal memory translation helper
 * @priv: private data pointer
 * @sys: system address (DDR address)
 * @len: length of the memory buffer
 * @da: device address to translate
 *
 * Convert system address (DDR address) to device address (DSP)
 * for there may be memory remap for device.
 */
static int imx_dsp_rproc_sys_to_da(struct imx_dsp_rproc *priv, u64 sys,
				   size_t len, u64 *da)
{
	const struct imx_dsp_rproc_dcfg *dsp_dcfg = priv->dsp_dcfg;
	const struct imx_rproc_dcfg *dcfg = dsp_dcfg->dcfg;
	int i;

	/* Parse address translation table */
	for (i = 0; i < dcfg->att_size; i++) {
		const struct imx_rproc_att *att = &dcfg->att[i];

		if (sys >= att->sa && sys + len <= att->sa + att->size) {
			unsigned int offset = sys - att->sa;

			*da = att->da + offset;
			return 0;
		}
	}

	return -ENOENT;
}

/* Main virtqueue message work function
 *
 * This function is executed upon scheduling of the i.MX DSP remoteproc
 * driver's workqueue. The workqueue is scheduled by the mailbox rx
 * handler.
 *
 * This work function processes both the Tx and Rx virtqueue indices on
 * every invocation. The rproc_vq_interrupt function can detect if there
 * are new unprocessed messages or not (returns IRQ_NONE vs IRQ_HANDLED),
 * but there is no need to check for these return values. The index 0
 * triggering will process all pending Rx buffers, and the index 1 triggering
 * will process all newly available Tx buffers and will wakeup any potentially
 * blocked senders.
 *
 * NOTE:
 *    The current logic is based on an inherent design assumption of supporting
 *    only 2 vrings, but this can be changed if needed.
 */
static void imx_dsp_rproc_vq_work(struct work_struct *work)
{
	struct imx_dsp_rproc *priv = container_of(work, struct imx_dsp_rproc,
						  rproc_work);

	rproc_vq_interrupt(priv->rproc, 0);
	rproc_vq_interrupt(priv->rproc, 1);
}

/**
 * imx_dsp_rproc_rx_tx_callback() - inbound mailbox message handler
 * @cl: mailbox client pointer used for requesting the mailbox channel
 * @data: mailbox payload
 *
 * This handler is invoked by mailbox driver whenever a mailbox
 * message is received. Usually, the SUSPEND and RESUME related messages
 * are handled in this function, other messages are handled by remoteproc core
 */
static void imx_dsp_rproc_rx_tx_callback(struct mbox_client *cl, void *data)
{
	struct rproc *rproc = dev_get_drvdata(cl->dev);
	struct imx_dsp_rproc *priv = rproc->priv;
	struct device *dev = rproc->dev.parent;
	u32 message = (u32)(*(u32 *)data);

	dev_dbg(dev, "mbox msg: 0x%x\n", message);

	switch (message) {
	case RP_MBOX_SUSPEND_ACK:
		complete(&priv->pm_comp);
		break;
	case RP_MBOX_RESUME_ACK:
		complete(&priv->pm_comp);
		break;
	default:
		schedule_work(&priv->rproc_work);
		break;
	}
}

/**
 * imx_dsp_rproc_rxdb_callback() - inbound mailbox message handler
 * @cl: mailbox client pointer used for requesting the mailbox channel
 * @data: mailbox payload
 *
 * For doorbell, there is no message specified, just set REMOTE_IS_READY
 * flag.
 */
static void imx_dsp_rproc_rxdb_callback(struct mbox_client *cl, void *data)
{
	struct rproc *rproc = dev_get_drvdata(cl->dev);
	struct imx_dsp_rproc *priv = rproc->priv;

	/* Remote is ready after firmware is loaded and running */
	priv->flags |= REMOTE_IS_READY;
}

/**
 * imx_dsp_rproc_mbox_init() - request mailbox channels
 * @priv: private data pointer
 *
 * Request three mailbox channels (tx, rx, rxdb).
 */
static int imx_dsp_rproc_mbox_init(struct imx_dsp_rproc *priv)
{
	struct device *dev = priv->rproc->dev.parent;
	struct mbox_client *cl;
	int ret;

	if (!of_get_property(dev->of_node, "mbox-names", NULL))
		return 0;

	cl = &priv->cl;
	cl->dev = dev;
	cl->tx_block = true;
	cl->tx_tout = 100;
	cl->knows_txdone = false;
	cl->rx_callback = imx_dsp_rproc_rx_tx_callback;

	/* Channel for sending message */
	priv->tx_ch = mbox_request_channel_byname(cl, "tx");
	if (IS_ERR(priv->tx_ch)) {
		ret = PTR_ERR(priv->tx_ch);
		dev_dbg(cl->dev, "failed to request tx mailbox channel: %d\n",
			ret);
		goto err_out;
	}

	/* Channel for receiving message */
	priv->rx_ch = mbox_request_channel_byname(cl, "rx");
	if (IS_ERR(priv->rx_ch)) {
		ret = PTR_ERR(priv->rx_ch);
		dev_dbg(cl->dev, "failed to request rx mailbox channel: %d\n",
			ret);
		goto err_out;
	}

	cl = &priv->cl_rxdb;
	cl->dev = dev;
	cl->rx_callback = imx_dsp_rproc_rxdb_callback;

	/*
	 * RX door bell is used to receive the ready signal from remote
	 * after firmware loaded.
	 */
	priv->rxdb_ch = mbox_request_channel_byname(cl, "rxdb");
	if (IS_ERR(priv->rxdb_ch)) {
		ret = PTR_ERR(priv->rxdb_ch);
		dev_dbg(cl->dev, "failed to request mbox chan rxdb, ret %d\n",
			ret);
		goto err_out;
	}

	return 0;

err_out:
	if (!IS_ERR(priv->tx_ch))
		mbox_free_channel(priv->tx_ch);
	if (!IS_ERR(priv->rx_ch))
		mbox_free_channel(priv->rx_ch);
	if (!IS_ERR(priv->rxdb_ch))
		mbox_free_channel(priv->rxdb_ch);

	return ret;
}

static void imx_dsp_rproc_free_mbox(struct imx_dsp_rproc *priv)
{
	mbox_free_channel(priv->tx_ch);
	mbox_free_channel(priv->rx_ch);
	mbox_free_channel(priv->rxdb_ch);
}

/**
 * imx_dsp_rproc_add_carveout() - request mailbox channels
 * @priv: private data pointer
 *
 * This function registers specified memory entry in @rproc carveouts list
 * The carveouts can help to mapping the memory address for DSP.
 */
static int imx_dsp_rproc_add_carveout(struct imx_dsp_rproc *priv)
{
	const struct imx_dsp_rproc_dcfg *dsp_dcfg = priv->dsp_dcfg;
	const struct imx_rproc_dcfg *dcfg = dsp_dcfg->dcfg;
	struct rproc *rproc = priv->rproc;
	struct device *dev = rproc->dev.parent;
	struct device_node *np = dev->of_node;
	struct of_phandle_iterator it;
	struct rproc_mem_entry *mem;
	struct reserved_mem *rmem;
	void __iomem *cpu_addr;
	int a;
	u64 da;

	/* Remap required addresses */
	for (a = 0; a < dcfg->att_size; a++) {
		const struct imx_rproc_att *att = &dcfg->att[a];

		if (!(att->flags & ATT_OWN))
			continue;

		if (imx_dsp_rproc_sys_to_da(priv, att->sa, att->size, &da))
			return -EINVAL;

		cpu_addr = devm_ioremap_wc(dev, att->sa, att->size);
		if (!cpu_addr) {
			dev_err(dev, "failed to map memory %p\n", &att->sa);
			return -ENOMEM;
		}

		/* Register memory region */
		mem = rproc_mem_entry_init(dev, cpu_addr, (dma_addr_t)att->sa,
					   att->size, da, NULL, NULL, "dsp_mem");

		if (mem)
			rproc_coredump_add_segment(rproc, da, att->size);
		else
			return -ENOMEM;

		rproc_add_carveout(rproc, mem);
	}

	of_phandle_iterator_init(&it, np, "memory-region", NULL, 0);
	while (of_phandle_iterator_next(&it) == 0) {
		/*
		 * Ignore the first memory region which will be used vdev buffer.
		 * No need to do extra handlings, rproc_add_virtio_dev will handle it.
		 */
		if (!strcmp(it.node->name, "vdev0buffer"))
			continue;

		rmem = of_reserved_mem_lookup(it.node);
		if (!rmem) {
			dev_err(dev, "unable to acquire memory-region\n");
			return -EINVAL;
		}

		if (imx_dsp_rproc_sys_to_da(priv, rmem->base, rmem->size, &da))
			return -EINVAL;

		cpu_addr = devm_ioremap_wc(dev, rmem->base, rmem->size);
		if (!cpu_addr) {
			dev_err(dev, "failed to map memory %p\n", &rmem->base);
			return -ENOMEM;
		}

		/* Register memory region */
		mem = rproc_mem_entry_init(dev, cpu_addr, (dma_addr_t)rmem->base,
					   rmem->size, da, NULL, NULL, it.node->name);

		if (mem)
			rproc_coredump_add_segment(rproc, da, rmem->size);
		else
			return -ENOMEM;

		rproc_add_carveout(rproc, mem);
	}

	return 0;
}

/* Prepare function for rproc_ops */
static int imx_dsp_rproc_prepare(struct rproc *rproc)
{
	struct imx_dsp_rproc *priv = rproc->priv;
	struct device *dev = rproc->dev.parent;
	struct rproc_mem_entry *carveout;
	int ret;

	ret = imx_dsp_rproc_add_carveout(priv);
	if (ret) {
		dev_err(dev, "failed on imx_dsp_rproc_add_carveout\n");
		return ret;
	}

	pm_runtime_get_sync(dev);

	/*
	 * Clear buffers after pm rumtime for internal ocram is not
	 * accessible if power and clock are not enabled.
	 */
	list_for_each_entry(carveout, &rproc->carveouts, node) {
		if (carveout->va)
			memset(carveout->va, 0, carveout->len);
	}

	return  0;
}

/* Unprepare function for rproc_ops */
static int imx_dsp_rproc_unprepare(struct rproc *rproc)
{
	pm_runtime_put_sync(rproc->dev.parent);

	return  0;
}

/* Kick function for rproc_ops */
static void imx_dsp_rproc_kick(struct rproc *rproc, int vqid)
{
	struct imx_dsp_rproc *priv = rproc->priv;
	struct device *dev = rproc->dev.parent;
	int err;
	__u32 mmsg;

	if (!priv->tx_ch) {
		dev_err(dev, "No initialized mbox tx channel\n");
		return;
	}

	/*
	 * Send the index of the triggered virtqueue as the mu payload.
	 * Let remote processor know which virtqueue is used.
	 */
	mmsg = vqid;

	err = mbox_send_message(priv->tx_ch, (void *)&mmsg);
	if (err < 0)
		dev_err(dev, "%s: failed (%d, err:%d)\n", __func__, vqid, err);
}

static int imx_dsp_rproc_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	if (rproc_elf_load_rsc_table(rproc, fw))
		dev_warn(&rproc->dev, "no resource table found for this firmware\n");

	return 0;
}

static const struct rproc_ops imx_dsp_rproc_ops = {
	.prepare	= imx_dsp_rproc_prepare,
	.unprepare	= imx_dsp_rproc_unprepare,
	.start		= imx_dsp_rproc_start,
	.stop		= imx_dsp_rproc_stop,
	.kick		= imx_dsp_rproc_kick,
	.load		= rproc_elf_load_segments,
	.parse_fw	= imx_dsp_rproc_parse_fw,
	.sanity_check	= rproc_elf_sanity_check,
	.get_boot_addr	= rproc_elf_get_boot_addr,
};

/**
 * imx_dsp_attach_pm_domains() - attach the power domains
 * @priv: private data pointer
 *
 * On i.MX8QM and i.MX8QXP there is multiple power domains
 * required, so need to link them.
 */
static int imx_dsp_attach_pm_domains(struct imx_dsp_rproc *priv)
{
	struct device *dev = priv->rproc->dev.parent;
	int ret, i;

	priv->num_domains = of_count_phandle_with_args(dev->of_node,
						       "power-domains",
						       "#power-domain-cells");

	/* If only one domain, then no need to link the device */
	if (priv->num_domains <= 1)
		return 0;

	priv->pd_dev = devm_kmalloc_array(dev, priv->num_domains,
					  sizeof(*priv->pd_dev),
					  GFP_KERNEL);
	if (!priv->pd_dev)
		return -ENOMEM;

	priv->pd_dev_link = devm_kmalloc_array(dev, priv->num_domains,
					       sizeof(*priv->pd_dev_link),
					       GFP_KERNEL);
	if (!priv->pd_dev_link)
		return -ENOMEM;

	for (i = 0; i < priv->num_domains; i++) {
		priv->pd_dev[i] = dev_pm_domain_attach_by_id(dev, i);
		if (IS_ERR(priv->pd_dev[i])) {
			ret = PTR_ERR(priv->pd_dev[i]);
			goto detach_pm;
		}

		/*
		 * device_link_add will check priv->pd_dev[i], if it is
		 * NULL, then will break.
		 */
		priv->pd_dev_link[i] = device_link_add(dev,
						       priv->pd_dev[i],
						       DL_FLAG_STATELESS |
						       DL_FLAG_PM_RUNTIME);
		if (!priv->pd_dev_link[i]) {
			dev_pm_domain_detach(priv->pd_dev[i], false);
			ret = -EINVAL;
			goto detach_pm;
		}
	}

	return 0;

detach_pm:
	while (--i >= 0) {
		device_link_del(priv->pd_dev_link[i]);
		dev_pm_domain_detach(priv->pd_dev[i], false);
	}

	return ret;
}

static int imx_dsp_detach_pm_domains(struct imx_dsp_rproc *priv)
{
	int i;

	if (priv->num_domains <= 1)
		return 0;

	for (i = 0; i < priv->num_domains; i++) {
		device_link_del(priv->pd_dev_link[i]);
		dev_pm_domain_detach(priv->pd_dev[i], false);
	}

	return 0;
}

/**
 * imx_dsp_rproc_detect_mode() - detect DSP control mode
 * @priv: private data pointer
 *
 * Different platform has different control method for DSP, which depends
 * on how the DSP is integrated in platform.
 *
 * For i.MX8QXP and i.MX8QM, DSP should be started and stopped by System
 * Control Unit.
 * For i.MX8MP and i.MX8ULP, DSP should be started and stopped by system
 * integration module.
 */
static int imx_dsp_rproc_detect_mode(struct imx_dsp_rproc *priv)
{
	const struct imx_dsp_rproc_dcfg *dsp_dcfg = priv->dsp_dcfg;
	struct device *dev = priv->rproc->dev.parent;
	struct regmap *regmap;
	int ret = 0;

	switch (dsp_dcfg->dcfg->method) {
	case IMX_RPROC_SCU_API:
		ret = imx_scu_get_handle(&priv->ipc_handle);
		if (ret)
			return ret;
		break;
	case IMX_RPROC_MMIO:
		regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "fsl,dsp-ctrl");
		if (IS_ERR(regmap)) {
			dev_err(dev, "failed to find syscon\n");
			return PTR_ERR(regmap);
		}

		priv->regmap = regmap;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static const char *imx_dsp_clks_names[DSP_RPROC_CLK_MAX] = {
	/* DSP clocks */
	"core", "ocram", "debug", "ipg", "mu",
};

static int imx_dsp_rproc_clk_get(struct imx_dsp_rproc *priv)
{
	struct device *dev = priv->rproc->dev.parent;
	struct clk_bulk_data *clks = priv->clks;
	int i;

	for (i = 0; i < DSP_RPROC_CLK_MAX; i++)
		clks[i].id = imx_dsp_clks_names[i];

	return devm_clk_bulk_get_optional(dev, DSP_RPROC_CLK_MAX, clks);
}

static int imx_dsp_rproc_probe(struct platform_device *pdev)
{
	const struct imx_dsp_rproc_dcfg *dsp_dcfg;
	struct device *dev = &pdev->dev;
	struct imx_dsp_rproc *priv;
	struct rproc *rproc;
	const char *fw_name;
	int ret;

	dsp_dcfg = of_device_get_match_data(dev);
	if (!dsp_dcfg)
		return -ENODEV;

	ret = rproc_of_parse_firmware(dev, 0, &fw_name);
	if (ret) {
		dev_err(dev, "failed to parse firmware-name property, ret = %d\n",
			ret);
		return ret;
	}

	rproc = rproc_alloc(dev, "imx-dsp-rproc", &imx_dsp_rproc_ops, fw_name,
			    sizeof(*priv));
	if (!rproc)
		return -ENOMEM;

	priv = rproc->priv;
	priv->rproc = rproc;
	priv->dsp_dcfg = dsp_dcfg;

	dev_set_drvdata(dev, rproc);

	INIT_WORK(&priv->rproc_work, imx_dsp_rproc_vq_work);

	ret = imx_dsp_rproc_detect_mode(priv);
	if (ret) {
		dev_err(dev, "failed on imx_dsp_rproc_detect_mode\n");
		goto err_put_rproc;
	}

	/* There are multiple power domains required by DSP on some platform */
	ret = imx_dsp_attach_pm_domains(priv);
	if (ret) {
		dev_err(dev, "failed on imx_dsp_attach_pm_domains\n");
		goto err_put_rproc;
	}
	/* Get clocks */
	ret = imx_dsp_rproc_clk_get(priv);
	if (ret) {
		dev_err(dev, "failed on imx_dsp_rproc_clk_get\n");
		goto err_detach_domains;
	}

	init_completion(&priv->pm_comp);
	rproc->auto_boot = false;
	ret = rproc_add(rproc);
	if (ret) {
		dev_err(dev, "rproc_add failed\n");
		goto err_detach_domains;
	}

	pm_runtime_enable(dev);

	return 0;

err_detach_domains:
	imx_dsp_detach_pm_domains(priv);
err_put_rproc:
	rproc_free(rproc);

	return ret;
}

static int imx_dsp_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct imx_dsp_rproc *priv = rproc->priv;

	pm_runtime_disable(&pdev->dev);
	rproc_del(rproc);
	imx_dsp_detach_pm_domains(priv);
	rproc_free(rproc);

	return 0;
}

/* pm runtime functions */
static int imx_dsp_runtime_resume(struct device *dev)
{
	struct rproc *rproc = dev_get_drvdata(dev);
	struct imx_dsp_rproc *priv = rproc->priv;
	const struct imx_dsp_rproc_dcfg *dsp_dcfg = priv->dsp_dcfg;
	int ret;

	/*
	 * There is power domain attached with mailbox, if setup mailbox
	 * in probe(), then the power of mailbox is always enabled,
	 * the power can't be saved.
	 * So move setup of mailbox to runtime resume.
	 */
	ret = imx_dsp_rproc_mbox_init(priv);
	if (ret) {
		dev_err(dev, "failed on imx_dsp_rproc_mbox_init\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(DSP_RPROC_CLK_MAX, priv->clks);
	if (ret) {
		dev_err(dev, "failed on clk_bulk_prepare_enable\n");
		return ret;
	}

	/* Reset DSP if needed */
	if (dsp_dcfg->reset)
		dsp_dcfg->reset(priv);

	return 0;
}

static int imx_dsp_runtime_suspend(struct device *dev)
{
	struct rproc *rproc = dev_get_drvdata(dev);
	struct imx_dsp_rproc *priv = rproc->priv;

	clk_bulk_disable_unprepare(DSP_RPROC_CLK_MAX, priv->clks);

	imx_dsp_rproc_free_mbox(priv);

	return 0;
}

static void imx_dsp_load_firmware(const struct firmware *fw, void *context)
{
	struct rproc *rproc = context;
	int ret;

	/*
	 * Same flow as start procedure.
	 * Load the ELF segments to memory firstly.
	 */
	ret = rproc_load_segments(rproc, fw);
	if (ret)
		goto out;

	/* Start the remote processor */
	ret = rproc->ops->start(rproc);
	if (ret)
		goto out;

	rproc->ops->kick(rproc, 0);

out:
	release_firmware(fw);
}

static __maybe_unused int imx_dsp_suspend(struct device *dev)
{
	struct rproc *rproc = dev_get_drvdata(dev);
	struct imx_dsp_rproc *priv = rproc->priv;
	__u32 mmsg = RP_MBOX_SUSPEND_SYSTEM;
	int ret;

	if (rproc->state != RPROC_RUNNING)
		goto out;

	reinit_completion(&priv->pm_comp);

	/* Tell DSP that suspend is happening */
	ret = mbox_send_message(priv->tx_ch, (void *)&mmsg);
	if (ret < 0) {
		dev_err(dev, "PM mbox_send_message failed: %d\n", ret);
		return ret;
	}

	/*
	 * DSP need to save the context at suspend.
	 * Here waiting the response for DSP, then power can be disabled.
	 */
	if (!wait_for_completion_timeout(&priv->pm_comp, msecs_to_jiffies(100)))
		return -EBUSY;

out:
	/*
	 * The power of DSP is disabled in suspend, so force pm runtime
	 * to be suspend, then we can reenable the power and clocks at
	 * resume stage.
	 */
	return pm_runtime_force_suspend(dev);
}

static __maybe_unused int imx_dsp_resume(struct device *dev)
{
	struct rproc *rproc = dev_get_drvdata(dev);
	int ret = 0;

	ret = pm_runtime_force_resume(dev);
	if (ret)
		return ret;

	if (rproc->state != RPROC_RUNNING)
		return 0;

	/*
	 * The power of DSP is disabled at suspend, the memory of dsp
	 * is reset, the image segments are lost. So need to reload
	 * firmware and restart the DSP if it is in running state.
	 */
	ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT,
				      rproc->firmware, dev, GFP_KERNEL,
				      rproc, imx_dsp_load_firmware);
	if (ret < 0) {
		dev_err(dev, "load firmware failed: %d\n", ret);
		goto err;
	}

	return 0;

err:
	pm_runtime_force_suspend(dev);

	return ret;
}

static const struct dev_pm_ops imx_dsp_rproc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(imx_dsp_suspend, imx_dsp_resume)
	SET_RUNTIME_PM_OPS(imx_dsp_runtime_suspend,
			   imx_dsp_runtime_resume, NULL)
};

static const struct of_device_id imx_dsp_rproc_of_match[] = {
	{ .compatible = "fsl,imx8qxp-hifi4", .data = &imx_dsp_rproc_cfg_imx8qxp },
	{ .compatible = "fsl,imx8qm-hifi4",  .data = &imx_dsp_rproc_cfg_imx8qm },
	{ .compatible = "fsl,imx8mp-hifi4",  .data = &imx_dsp_rproc_cfg_imx8mp },
	{ .compatible = "fsl,imx8ulp-hifi4", .data = &imx_dsp_rproc_cfg_imx8ulp },
	{},
};
MODULE_DEVICE_TABLE(of, imx_dsp_rproc_of_match);

static struct platform_driver imx_dsp_rproc_driver = {
	.probe = imx_dsp_rproc_probe,
	.remove = imx_dsp_rproc_remove,
	.driver = {
		.name = "imx-dsp-rproc",
		.of_match_table = imx_dsp_rproc_of_match,
		.pm = &imx_dsp_rproc_pm_ops,
	},
};
module_platform_driver(imx_dsp_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("i.MX HiFi Core Remote Processor Control Driver");
MODULE_AUTHOR("Shengjiu Wang <shengjiu.wang@nxp.com>");
