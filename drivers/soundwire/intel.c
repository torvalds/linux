// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

/*
 * Soundwire Intel Master Driver
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include "cadence_master.h"
#include "intel.h"

/* Intel SHIM Registers Definition */
#define SDW_SHIM_LCAP			0x0
#define SDW_SHIM_LCTL			0x4
#define SDW_SHIM_IPPTR			0x8
#define SDW_SHIM_SYNC			0xC

#define SDW_SHIM_CTLSCAP(x)		(0x010 + 0x60 * x)
#define SDW_SHIM_CTLS0CM(x)		(0x012 + 0x60 * x)
#define SDW_SHIM_CTLS1CM(x)		(0x014 + 0x60 * x)
#define SDW_SHIM_CTLS2CM(x)		(0x016 + 0x60 * x)
#define SDW_SHIM_CTLS3CM(x)		(0x018 + 0x60 * x)
#define SDW_SHIM_PCMSCAP(x)		(0x020 + 0x60 * x)

#define SDW_SHIM_PCMSYCHM(x, y)		(0x022 + (0x60 * x) + (0x2 * y))
#define SDW_SHIM_PCMSYCHC(x, y)		(0x042 + (0x60 * x) + (0x2 * y))
#define SDW_SHIM_PDMSCAP(x)		(0x062 + 0x60 * x)
#define SDW_SHIM_IOCTL(x)		(0x06C + 0x60 * x)
#define SDW_SHIM_CTMCTL(x)		(0x06E + 0x60 * x)

#define SDW_SHIM_WAKEEN			0x190
#define SDW_SHIM_WAKESTS		0x192

#define SDW_SHIM_LCTL_SPA		BIT(0)
#define SDW_SHIM_LCTL_CPA		BIT(8)

#define SDW_SHIM_SYNC_SYNCPRD_VAL	0x176F
#define SDW_SHIM_SYNC_SYNCPRD		GENMASK(14, 0)
#define SDW_SHIM_SYNC_SYNCCPU		BIT(15)
#define SDW_SHIM_SYNC_CMDSYNC_MASK	GENMASK(19, 16)
#define SDW_SHIM_SYNC_CMDSYNC		BIT(16)
#define SDW_SHIM_SYNC_SYNCGO		BIT(24)

#define SDW_SHIM_PCMSCAP_ISS		GENMASK(3, 0)
#define SDW_SHIM_PCMSCAP_OSS		GENMASK(7, 4)
#define SDW_SHIM_PCMSCAP_BSS		GENMASK(12, 8)

#define SDW_SHIM_PCMSYCM_LCHN		GENMASK(3, 0)
#define SDW_SHIM_PCMSYCM_HCHN		GENMASK(7, 4)
#define SDW_SHIM_PCMSYCM_STREAM		GENMASK(13, 8)
#define SDW_SHIM_PCMSYCM_DIR		BIT(15)

#define SDW_SHIM_PDMSCAP_ISS		GENMASK(3, 0)
#define SDW_SHIM_PDMSCAP_OSS		GENMASK(7, 4)
#define SDW_SHIM_PDMSCAP_BSS		GENMASK(12, 8)
#define SDW_SHIM_PDMSCAP_CPSS		GENMASK(15, 13)

#define SDW_SHIM_IOCTL_MIF		BIT(0)
#define SDW_SHIM_IOCTL_CO		BIT(1)
#define SDW_SHIM_IOCTL_COE		BIT(2)
#define SDW_SHIM_IOCTL_DO		BIT(3)
#define SDW_SHIM_IOCTL_DOE		BIT(4)
#define SDW_SHIM_IOCTL_BKE		BIT(5)
#define SDW_SHIM_IOCTL_WPDD		BIT(6)
#define SDW_SHIM_IOCTL_CIBD		BIT(8)
#define SDW_SHIM_IOCTL_DIBD		BIT(9)

#define SDW_SHIM_CTMCTL_DACTQE		BIT(0)
#define SDW_SHIM_CTMCTL_DODS		BIT(1)
#define SDW_SHIM_CTMCTL_DOAIS		GENMASK(4, 3)

#define SDW_SHIM_WAKEEN_ENABLE		BIT(0)
#define SDW_SHIM_WAKESTS_STATUS		BIT(0)

/* Intel ALH Register definitions */
#define SDW_ALH_STRMZCFG(x)		(0x000 + (0x4 * x))

#define SDW_ALH_STRMZCFG_DMAT_VAL	0x3
#define SDW_ALH_STRMZCFG_DMAT		GENMASK(7, 0)
#define SDW_ALH_STRMZCFG_CHN		GENMASK(19, 16)

struct sdw_intel {
	struct sdw_cdns cdns;
	int instance;
	struct sdw_intel_link_res *res;
};

#define cdns_to_intel(_cdns) container_of(_cdns, struct sdw_intel, cdns)

/*
 * Read, write helpers for HW registers
 */
static inline int intel_readl(void __iomem *base, int offset)
{
	return readl(base + offset);
}

static inline void intel_writel(void __iomem *base, int offset, int value)
{
	writel(value, base + offset);
}

static inline u16 intel_readw(void __iomem *base, int offset)
{
	return readw(base + offset);
}

static inline void intel_writew(void __iomem *base, int offset, u16 value)
{
	writew(value, base + offset);
}

static int intel_clear_bit(void __iomem *base, int offset, u32 value, u32 mask)
{
	int timeout = 10;
	u32 reg_read;

	writel(value, base + offset);
	do {
		reg_read = readl(base + offset);
		if (!(reg_read & mask))
			return 0;

		timeout--;
		udelay(50);
	} while (timeout != 0);

	return -EAGAIN;
}

static int intel_set_bit(void __iomem *base, int offset, u32 value, u32 mask)
{
	int timeout = 10;
	u32 reg_read;

	writel(value, base + offset);
	do {
		reg_read = readl(base + offset);
		if (reg_read & mask)
			return 0;

		timeout--;
		udelay(50);
	} while (timeout != 0);

	return -EAGAIN;
}

/*
 * shim ops
 */

static int intel_link_power_up(struct sdw_intel *sdw)
{
	unsigned int link_id = sdw->instance;
	void __iomem *shim = sdw->res->shim;
	int spa_mask, cpa_mask;
	int link_control, ret;

	/* Link power up sequence */
	link_control = intel_readl(shim, SDW_SHIM_LCTL);
	spa_mask = (SDW_SHIM_LCTL_SPA << link_id);
	cpa_mask = (SDW_SHIM_LCTL_CPA << link_id);
	link_control |=  spa_mask;

	ret = intel_set_bit(shim, SDW_SHIM_LCTL, link_control, cpa_mask);
	if (ret < 0)
		return ret;

	sdw->cdns.link_up = true;
	return 0;
}

static int intel_shim_init(struct sdw_intel *sdw)
{
	void __iomem *shim = sdw->res->shim;
	unsigned int link_id = sdw->instance;
	int sync_reg, ret;
	u16 ioctl = 0, act = 0;

	/* Initialize Shim */
	ioctl |= SDW_SHIM_IOCTL_BKE;
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);

	ioctl |= SDW_SHIM_IOCTL_WPDD;
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);

	ioctl |= SDW_SHIM_IOCTL_DO;
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);

	ioctl |= SDW_SHIM_IOCTL_DOE;
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);

	/* Switch to MIP from Glue logic */
	ioctl = intel_readw(shim,  SDW_SHIM_IOCTL(link_id));

	ioctl &= ~(SDW_SHIM_IOCTL_DOE);
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);

	ioctl &= ~(SDW_SHIM_IOCTL_DO);
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);

	ioctl |= (SDW_SHIM_IOCTL_MIF);
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);

	ioctl &= ~(SDW_SHIM_IOCTL_BKE);
	ioctl &= ~(SDW_SHIM_IOCTL_COE);

	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);

	act |= 0x1 << SDW_REG_SHIFT(SDW_SHIM_CTMCTL_DOAIS);
	act |= SDW_SHIM_CTMCTL_DACTQE;
	act |= SDW_SHIM_CTMCTL_DODS;
	intel_writew(shim, SDW_SHIM_CTMCTL(link_id), act);

	/* Now set SyncPRD period */
	sync_reg = intel_readl(shim, SDW_SHIM_SYNC);
	sync_reg |= (SDW_SHIM_SYNC_SYNCPRD_VAL <<
			SDW_REG_SHIFT(SDW_SHIM_SYNC_SYNCPRD));

	/* Set SyncCPU bit */
	sync_reg |= SDW_SHIM_SYNC_SYNCCPU;
	ret = intel_clear_bit(shim, SDW_SHIM_SYNC, sync_reg,
				SDW_SHIM_SYNC_SYNCCPU);
	if (ret < 0)
		dev_err(sdw->cdns.dev, "Failed to set sync period: %d", ret);

	return ret;
}

/*
 * PDI routines
 */
static void intel_pdi_init(struct sdw_intel *sdw,
			struct sdw_cdns_stream_config *config)
{
	void __iomem *shim = sdw->res->shim;
	unsigned int link_id = sdw->instance;
	int pcm_cap, pdm_cap;

	/* PCM Stream Capability */
	pcm_cap = intel_readw(shim, SDW_SHIM_PCMSCAP(link_id));

	config->pcm_bd = (pcm_cap & SDW_SHIM_PCMSCAP_BSS) >>
					SDW_REG_SHIFT(SDW_SHIM_PCMSCAP_BSS);
	config->pcm_in = (pcm_cap & SDW_SHIM_PCMSCAP_ISS) >>
					SDW_REG_SHIFT(SDW_SHIM_PCMSCAP_ISS);
	config->pcm_out = (pcm_cap & SDW_SHIM_PCMSCAP_OSS) >>
					SDW_REG_SHIFT(SDW_SHIM_PCMSCAP_OSS);

	/* PDM Stream Capability */
	pdm_cap = intel_readw(shim, SDW_SHIM_PDMSCAP(link_id));

	config->pdm_bd = (pdm_cap & SDW_SHIM_PDMSCAP_BSS) >>
					SDW_REG_SHIFT(SDW_SHIM_PDMSCAP_BSS);
	config->pdm_in = (pdm_cap & SDW_SHIM_PDMSCAP_ISS) >>
					SDW_REG_SHIFT(SDW_SHIM_PDMSCAP_ISS);
	config->pdm_out = (pdm_cap & SDW_SHIM_PDMSCAP_OSS) >>
					SDW_REG_SHIFT(SDW_SHIM_PDMSCAP_OSS);
}

static int
intel_pdi_get_ch_cap(struct sdw_intel *sdw, unsigned int pdi_num, bool pcm)
{
	void __iomem *shim = sdw->res->shim;
	unsigned int link_id = sdw->instance;
	int count;

	if (pcm) {
		count = intel_readw(shim, SDW_SHIM_PCMSYCHC(link_id, pdi_num));
	} else {
		count = intel_readw(shim, SDW_SHIM_PDMSCAP(link_id));
		count = ((count & SDW_SHIM_PDMSCAP_CPSS) >>
					SDW_REG_SHIFT(SDW_SHIM_PDMSCAP_CPSS));
	}

	/* zero based values for channel count in register */
	count++;

	return count;
}

static int intel_pdi_get_ch_update(struct sdw_intel *sdw,
				struct sdw_cdns_pdi *pdi,
				unsigned int num_pdi,
				unsigned int *num_ch, bool pcm)
{
	int i, ch_count = 0;

	for (i = 0; i < num_pdi; i++) {
		pdi->ch_count = intel_pdi_get_ch_cap(sdw, pdi->num, pcm);
		ch_count += pdi->ch_count;
		pdi++;
	}

	*num_ch = ch_count;
	return 0;
}

static int intel_pdi_stream_ch_update(struct sdw_intel *sdw,
				struct sdw_cdns_streams *stream, bool pcm)
{
	intel_pdi_get_ch_update(sdw, stream->bd, stream->num_bd,
			&stream->num_ch_bd, pcm);

	intel_pdi_get_ch_update(sdw, stream->in, stream->num_in,
			&stream->num_ch_in, pcm);

	intel_pdi_get_ch_update(sdw, stream->out, stream->num_out,
			&stream->num_ch_out, pcm);

	return 0;
}

static int intel_pdi_ch_update(struct sdw_intel *sdw)
{
	/* First update PCM streams followed by PDM streams */
	intel_pdi_stream_ch_update(sdw, &sdw->cdns.pcm, true);
	intel_pdi_stream_ch_update(sdw, &sdw->cdns.pdm, false);

	return 0;
}

static void
intel_pdi_shim_configure(struct sdw_intel *sdw, struct sdw_cdns_pdi *pdi)
{
	void __iomem *shim = sdw->res->shim;
	unsigned int link_id = sdw->instance;
	int pdi_conf = 0;

	pdi->intel_alh_id = (link_id * 16) + pdi->num + 5;

	/*
	 * Program stream parameters to stream SHIM register
	 * This is applicable for PCM stream only.
	 */
	if (pdi->type != SDW_STREAM_PCM)
		return;

	if (pdi->dir == SDW_DATA_DIR_RX)
		pdi_conf |= SDW_SHIM_PCMSYCM_DIR;
	else
		pdi_conf &= ~(SDW_SHIM_PCMSYCM_DIR);

	pdi_conf |= (pdi->intel_alh_id <<
			SDW_REG_SHIFT(SDW_SHIM_PCMSYCM_STREAM));
	pdi_conf |= (pdi->l_ch_num << SDW_REG_SHIFT(SDW_SHIM_PCMSYCM_LCHN));
	pdi_conf |= (pdi->h_ch_num << SDW_REG_SHIFT(SDW_SHIM_PCMSYCM_HCHN));

	intel_writew(shim, SDW_SHIM_PCMSYCHM(link_id, pdi->num), pdi_conf);
}

static void
intel_pdi_alh_configure(struct sdw_intel *sdw, struct sdw_cdns_pdi *pdi)
{
	void __iomem *alh = sdw->res->alh;
	unsigned int link_id = sdw->instance;
	unsigned int conf;

	pdi->intel_alh_id = (link_id * 16) + pdi->num + 5;

	/* Program Stream config ALH register */
	conf = intel_readl(alh, SDW_ALH_STRMZCFG(pdi->intel_alh_id));

	conf |= (SDW_ALH_STRMZCFG_DMAT_VAL <<
			SDW_REG_SHIFT(SDW_ALH_STRMZCFG_DMAT));

	conf |= ((pdi->ch_count - 1) <<
			SDW_REG_SHIFT(SDW_ALH_STRMZCFG_CHN));

	intel_writel(alh, SDW_ALH_STRMZCFG(pdi->intel_alh_id), conf);
}

static int intel_prop_read(struct sdw_bus *bus)
{
	/* Initialize with default handler to read all DisCo properties */
	sdw_master_read_prop(bus);

	/* BIOS is not giving some values correctly. So, lets override them */
	bus->prop.num_freq = 1;
	bus->prop.freq = devm_kcalloc(bus->dev, sizeof(*bus->prop.freq),
					bus->prop.num_freq, GFP_KERNEL);
	if (!bus->prop.freq)
		return -ENOMEM;

	bus->prop.freq[0] = bus->prop.max_freq;
	bus->prop.err_threshold = 5;

	return 0;
}

static struct sdw_master_ops sdw_intel_ops = {
	.read_prop = sdw_master_read_prop,
	.xfer_msg = cdns_xfer_msg,
	.xfer_msg_defer = cdns_xfer_msg_defer,
	.reset_page_addr = cdns_reset_page_addr,
	.set_bus_conf = cdns_bus_conf,
};

/*
 * probe and init
 */
static int intel_probe(struct platform_device *pdev)
{
	struct sdw_cdns_stream_config config;
	struct sdw_intel *sdw;
	int ret;

	sdw = devm_kzalloc(&pdev->dev, sizeof(*sdw), GFP_KERNEL);
	if (!sdw)
		return -ENOMEM;

	sdw->instance = pdev->id;
	sdw->res = dev_get_platdata(&pdev->dev);
	sdw->cdns.dev = &pdev->dev;
	sdw->cdns.registers = sdw->res->registers;
	sdw->cdns.instance = sdw->instance;
	sdw->cdns.msg_count = 0;
	sdw->cdns.bus.dev = &pdev->dev;
	sdw->cdns.bus.link_id = pdev->id;

	sdw_cdns_probe(&sdw->cdns);

	/* Set property read ops */
	sdw_intel_ops.read_prop = intel_prop_read;
	sdw->cdns.bus.ops = &sdw_intel_ops;

	sdw_intel_ops.read_prop = intel_prop_read;
	sdw->cdns.bus.ops = &sdw_intel_ops;

	platform_set_drvdata(pdev, sdw);

	ret = sdw_add_bus_master(&sdw->cdns.bus);
	if (ret) {
		dev_err(&pdev->dev, "sdw_add_bus_master fail: %d\n", ret);
		goto err_master_reg;
	}

	/* Initialize shim and controller */
	intel_link_power_up(sdw);
	intel_shim_init(sdw);

	ret = sdw_cdns_init(&sdw->cdns);
	if (ret)
		goto err_init;

	ret = sdw_cdns_enable_interrupt(&sdw->cdns);

	/* Read the PDI config and initialize cadence PDI */
	intel_pdi_init(sdw, &config);
	ret = sdw_cdns_pdi_init(&sdw->cdns, config);
	if (ret)
		goto err_init;

	intel_pdi_ch_update(sdw);

	/* Acquire IRQ */
	ret = request_threaded_irq(sdw->res->irq, sdw_cdns_irq,
			sdw_cdns_thread, IRQF_SHARED, KBUILD_MODNAME,
			&sdw->cdns);
	if (ret < 0) {
		dev_err(sdw->cdns.dev, "unable to grab IRQ %d, disabling device\n",
				sdw->res->irq);
		goto err_init;
	}

	return 0;

err_init:
	sdw_delete_bus_master(&sdw->cdns.bus);
err_master_reg:
	return ret;
}

static int intel_remove(struct platform_device *pdev)
{
	struct sdw_intel *sdw;

	sdw = platform_get_drvdata(pdev);

	free_irq(sdw->res->irq, sdw);
	sdw_delete_bus_master(&sdw->cdns.bus);

	return 0;
}

static struct platform_driver sdw_intel_drv = {
	.probe = intel_probe,
	.remove = intel_remove,
	.driver = {
		.name = "int-sdw",

	},
};

module_platform_driver(sdw_intel_drv);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:int-sdw");
MODULE_DESCRIPTION("Intel Soundwire Master Driver");
