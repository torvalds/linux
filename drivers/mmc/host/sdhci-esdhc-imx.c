// SPDX-License-Identifier: GPL-2.0
/*
 * Freescale eSDHC i.MX controller driver for the platform bus.
 *
 * derived from the OF-version.
 *
 * Copyright (c) 2010 Pengutronix e.K.
 *   Author: Wolfram Sang <kernel@pengutronix.de>
 */

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pm_qos.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_data/mmc-esdhc-imx.h>
#include <linux/pm_runtime.h>
#include "sdhci-pltfm.h"
#include "sdhci-esdhc.h"
#include "cqhci.h"

#define ESDHC_SYS_CTRL_DTOCV_MASK	0x0f
#define	ESDHC_CTRL_D3CD			0x08
#define ESDHC_BURST_LEN_EN_INCR		(1 << 27)
/* VENDOR SPEC register */
#define ESDHC_VENDOR_SPEC		0xc0
#define  ESDHC_VENDOR_SPEC_SDIO_QUIRK	(1 << 1)
#define  ESDHC_VENDOR_SPEC_VSELECT	(1 << 1)
#define  ESDHC_VENDOR_SPEC_FRC_SDCLK_ON	(1 << 8)
#define ESDHC_DEBUG_SEL_AND_STATUS_REG		0xc2
#define ESDHC_DEBUG_SEL_REG			0xc3
#define ESDHC_DEBUG_SEL_MASK			0xf
#define ESDHC_DEBUG_SEL_CMD_STATE		1
#define ESDHC_DEBUG_SEL_DATA_STATE		2
#define ESDHC_DEBUG_SEL_TRANS_STATE		3
#define ESDHC_DEBUG_SEL_DMA_STATE		4
#define ESDHC_DEBUG_SEL_ADMA_STATE		5
#define ESDHC_DEBUG_SEL_FIFO_STATE		6
#define ESDHC_DEBUG_SEL_ASYNC_FIFO_STATE	7
#define ESDHC_WTMK_LVL			0x44
#define  ESDHC_WTMK_DEFAULT_VAL		0x10401040
#define  ESDHC_WTMK_LVL_RD_WML_MASK	0x000000FF
#define  ESDHC_WTMK_LVL_RD_WML_SHIFT	0
#define  ESDHC_WTMK_LVL_WR_WML_MASK	0x00FF0000
#define  ESDHC_WTMK_LVL_WR_WML_SHIFT	16
#define  ESDHC_WTMK_LVL_WML_VAL_DEF	64
#define  ESDHC_WTMK_LVL_WML_VAL_MAX	128
#define ESDHC_MIX_CTRL			0x48
#define  ESDHC_MIX_CTRL_DDREN		(1 << 3)
#define  ESDHC_MIX_CTRL_AC23EN		(1 << 7)
#define  ESDHC_MIX_CTRL_EXE_TUNE	(1 << 22)
#define  ESDHC_MIX_CTRL_SMPCLK_SEL	(1 << 23)
#define  ESDHC_MIX_CTRL_AUTO_TUNE_EN	(1 << 24)
#define  ESDHC_MIX_CTRL_FBCLK_SEL	(1 << 25)
#define  ESDHC_MIX_CTRL_HS400_EN	(1 << 26)
#define  ESDHC_MIX_CTRL_HS400_ES_EN	(1 << 27)
/* Bits 3 and 6 are not SDHCI standard definitions */
#define  ESDHC_MIX_CTRL_SDHCI_MASK	0xb7
/* Tuning bits */
#define  ESDHC_MIX_CTRL_TUNING_MASK	0x03c00000

/* dll control register */
#define ESDHC_DLL_CTRL			0x60
#define ESDHC_DLL_OVERRIDE_VAL_SHIFT	9
#define ESDHC_DLL_OVERRIDE_EN_SHIFT	8

/* tune control register */
#define ESDHC_TUNE_CTRL_STATUS		0x68
#define  ESDHC_TUNE_CTRL_STEP		1
#define  ESDHC_TUNE_CTRL_MIN		0
#define  ESDHC_TUNE_CTRL_MAX		((1 << 7) - 1)

/* strobe dll register */
#define ESDHC_STROBE_DLL_CTRL		0x70
#define ESDHC_STROBE_DLL_CTRL_ENABLE	(1 << 0)
#define ESDHC_STROBE_DLL_CTRL_RESET	(1 << 1)
#define ESDHC_STROBE_DLL_CTRL_SLV_DLY_TARGET_DEFAULT	0x7
#define ESDHC_STROBE_DLL_CTRL_SLV_DLY_TARGET_SHIFT	3
#define ESDHC_STROBE_DLL_CTRL_SLV_UPDATE_INT_DEFAULT	(4 << 20)

#define ESDHC_STROBE_DLL_STATUS		0x74
#define ESDHC_STROBE_DLL_STS_REF_LOCK	(1 << 1)
#define ESDHC_STROBE_DLL_STS_SLV_LOCK	0x1

#define ESDHC_VEND_SPEC2		0xc8
#define ESDHC_VEND_SPEC2_EN_BUSY_IRQ	(1 << 8)

#define ESDHC_TUNING_CTRL		0xcc
#define ESDHC_STD_TUNING_EN		(1 << 24)
/* NOTE: the minimum valid tuning start tap for mx6sl is 1 */
#define ESDHC_TUNING_START_TAP_DEFAULT	0x1
#define ESDHC_TUNING_START_TAP_MASK	0x7f
#define ESDHC_TUNING_CMD_CRC_CHECK_DISABLE	(1 << 7)
#define ESDHC_TUNING_STEP_MASK		0x00070000
#define ESDHC_TUNING_STEP_SHIFT		16

/* pinctrl state */
#define ESDHC_PINCTRL_STATE_100MHZ	"state_100mhz"
#define ESDHC_PINCTRL_STATE_200MHZ	"state_200mhz"

/*
 * Our interpretation of the SDHCI_HOST_CONTROL register
 */
#define ESDHC_CTRL_4BITBUS		(0x1 << 1)
#define ESDHC_CTRL_8BITBUS		(0x2 << 1)
#define ESDHC_CTRL_BUSWIDTH_MASK	(0x3 << 1)

/*
 * There is an INT DMA ERR mismatch between eSDHC and STD SDHC SPEC:
 * Bit25 is used in STD SPEC, and is reserved in fsl eSDHC design,
 * but bit28 is used as the INT DMA ERR in fsl eSDHC design.
 * Define this macro DMA error INT for fsl eSDHC
 */
#define ESDHC_INT_VENDOR_SPEC_DMA_ERR	(1 << 28)

/* the address offset of CQHCI */
#define ESDHC_CQHCI_ADDR_OFFSET		0x100

/*
 * The CMDTYPE of the CMD register (offset 0xE) should be set to
 * "11" when the STOP CMD12 is issued on imx53 to abort one
 * open ended multi-blk IO. Otherwise the TC INT wouldn't
 * be generated.
 * In exact block transfer, the controller doesn't complete the
 * operations automatically as required at the end of the
 * transfer and remains on hold if the abort command is not sent.
 * As a result, the TC flag is not asserted and SW received timeout
 * exception. Bit1 of Vendor Spec register is used to fix it.
 */
#define ESDHC_FLAG_MULTIBLK_NO_INT	BIT(1)
/*
 * The flag tells that the ESDHC controller is an USDHC block that is
 * integrated on the i.MX6 series.
 */
#define ESDHC_FLAG_USDHC		BIT(3)
/* The IP supports manual tuning process */
#define ESDHC_FLAG_MAN_TUNING		BIT(4)
/* The IP supports standard tuning process */
#define ESDHC_FLAG_STD_TUNING		BIT(5)
/* The IP has SDHCI_CAPABILITIES_1 register */
#define ESDHC_FLAG_HAVE_CAP1		BIT(6)
/*
 * The IP has erratum ERR004536
 * uSDHC: ADMA Length Mismatch Error occurs if the AHB read access is slow,
 * when reading data from the card
 * This flag is also set for i.MX25 and i.MX35 in order to get
 * SDHCI_QUIRK_BROKEN_ADMA, but for different reasons (ADMA capability bits).
 */
#define ESDHC_FLAG_ERR004536		BIT(7)
/* The IP supports HS200 mode */
#define ESDHC_FLAG_HS200		BIT(8)
/* The IP supports HS400 mode */
#define ESDHC_FLAG_HS400		BIT(9)
/*
 * The IP has errata ERR010450
 * uSDHC: Due to the I/O timing limit, for SDR mode, SD card clock can't
 * exceed 150MHz, for DDR mode, SD card clock can't exceed 45MHz.
 */
#define ESDHC_FLAG_ERR010450		BIT(10)
/* The IP supports HS400ES mode */
#define ESDHC_FLAG_HS400_ES		BIT(11)
/* The IP has Host Controller Interface for Command Queuing */
#define ESDHC_FLAG_CQHCI		BIT(12)
/* need request pmqos during low power */
#define ESDHC_FLAG_PMQOS		BIT(13)
/* The IP state got lost in low power mode */
#define ESDHC_FLAG_STATE_LOST_IN_LPMODE		BIT(14)
/* The IP lost clock rate in PM_RUNTIME */
#define ESDHC_FLAG_CLK_RATE_LOST_IN_PM_RUNTIME	BIT(15)
/*
 * The IP do not support the ACMD23 feature completely when use ADMA mode.
 * In ADMA mode, it only use the 16 bit block count of the register 0x4
 * (BLOCK_ATT) as the CMD23's argument for ACMD23 mode, which means it will
 * ignore the upper 16 bit of the CMD23's argument. This will block the reliable
 * write operation in RPMB, because RPMB reliable write need to set the bit31
 * of the CMD23's argument.
 * imx6qpdl/imx6sx/imx6sl/imx7d has this limitation only for ADMA mode, SDMA
 * do not has this limitation. so when these SoC use ADMA mode, it need to
 * disable the ACMD23 feature.
 */
#define ESDHC_FLAG_BROKEN_AUTO_CMD23	BIT(16)

struct esdhc_soc_data {
	u32 flags;
};

static const struct esdhc_soc_data esdhc_imx25_data = {
	.flags = ESDHC_FLAG_ERR004536,
};

static const struct esdhc_soc_data esdhc_imx35_data = {
	.flags = ESDHC_FLAG_ERR004536,
};

static const struct esdhc_soc_data esdhc_imx51_data = {
	.flags = 0,
};

static const struct esdhc_soc_data esdhc_imx53_data = {
	.flags = ESDHC_FLAG_MULTIBLK_NO_INT,
};

static const struct esdhc_soc_data usdhc_imx6q_data = {
	.flags = ESDHC_FLAG_USDHC | ESDHC_FLAG_MAN_TUNING
			| ESDHC_FLAG_BROKEN_AUTO_CMD23,
};

static const struct esdhc_soc_data usdhc_imx6sl_data = {
	.flags = ESDHC_FLAG_USDHC | ESDHC_FLAG_STD_TUNING
			| ESDHC_FLAG_HAVE_CAP1 | ESDHC_FLAG_ERR004536
			| ESDHC_FLAG_HS200
			| ESDHC_FLAG_BROKEN_AUTO_CMD23,
};

static const struct esdhc_soc_data usdhc_imx6sll_data = {
	.flags = ESDHC_FLAG_USDHC | ESDHC_FLAG_STD_TUNING
			| ESDHC_FLAG_HAVE_CAP1 | ESDHC_FLAG_HS200
			| ESDHC_FLAG_HS400
			| ESDHC_FLAG_STATE_LOST_IN_LPMODE,
};

static const struct esdhc_soc_data usdhc_imx6sx_data = {
	.flags = ESDHC_FLAG_USDHC | ESDHC_FLAG_STD_TUNING
			| ESDHC_FLAG_HAVE_CAP1 | ESDHC_FLAG_HS200
			| ESDHC_FLAG_STATE_LOST_IN_LPMODE
			| ESDHC_FLAG_BROKEN_AUTO_CMD23,
};

static const struct esdhc_soc_data usdhc_imx6ull_data = {
	.flags = ESDHC_FLAG_USDHC | ESDHC_FLAG_STD_TUNING
			| ESDHC_FLAG_HAVE_CAP1 | ESDHC_FLAG_HS200
			| ESDHC_FLAG_ERR010450
			| ESDHC_FLAG_STATE_LOST_IN_LPMODE,
};

static const struct esdhc_soc_data usdhc_imx7d_data = {
	.flags = ESDHC_FLAG_USDHC | ESDHC_FLAG_STD_TUNING
			| ESDHC_FLAG_HAVE_CAP1 | ESDHC_FLAG_HS200
			| ESDHC_FLAG_HS400
			| ESDHC_FLAG_STATE_LOST_IN_LPMODE
			| ESDHC_FLAG_BROKEN_AUTO_CMD23,
};

static struct esdhc_soc_data usdhc_imx7ulp_data = {
	.flags = ESDHC_FLAG_USDHC | ESDHC_FLAG_STD_TUNING
			| ESDHC_FLAG_HAVE_CAP1 | ESDHC_FLAG_HS200
			| ESDHC_FLAG_PMQOS | ESDHC_FLAG_HS400
			| ESDHC_FLAG_STATE_LOST_IN_LPMODE,
};

static struct esdhc_soc_data usdhc_imx8qxp_data = {
	.flags = ESDHC_FLAG_USDHC | ESDHC_FLAG_STD_TUNING
			| ESDHC_FLAG_HAVE_CAP1 | ESDHC_FLAG_HS200
			| ESDHC_FLAG_HS400 | ESDHC_FLAG_HS400_ES
			| ESDHC_FLAG_CQHCI
			| ESDHC_FLAG_STATE_LOST_IN_LPMODE
			| ESDHC_FLAG_CLK_RATE_LOST_IN_PM_RUNTIME,
};

static struct esdhc_soc_data usdhc_imx8mm_data = {
	.flags = ESDHC_FLAG_USDHC | ESDHC_FLAG_STD_TUNING
			| ESDHC_FLAG_HAVE_CAP1 | ESDHC_FLAG_HS200
			| ESDHC_FLAG_HS400 | ESDHC_FLAG_HS400_ES
			| ESDHC_FLAG_CQHCI
			| ESDHC_FLAG_STATE_LOST_IN_LPMODE,
};

struct pltfm_imx_data {
	u32 scratchpad;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_100mhz;
	struct pinctrl_state *pins_200mhz;
	const struct esdhc_soc_data *socdata;
	struct esdhc_platform_data boarddata;
	struct clk *clk_ipg;
	struct clk *clk_ahb;
	struct clk *clk_per;
	unsigned int actual_clock;
	enum {
		NO_CMD_PENDING,      /* no multiblock command pending */
		MULTIBLK_IN_PROCESS, /* exact multiblock cmd in process */
		WAIT_FOR_INT,        /* sent CMD12, waiting for response INT */
	} multiblock_status;
	u32 is_ddr;
	struct pm_qos_request pm_qos_req;
};

static const struct of_device_id imx_esdhc_dt_ids[] = {
	{ .compatible = "fsl,imx25-esdhc", .data = &esdhc_imx25_data, },
	{ .compatible = "fsl,imx35-esdhc", .data = &esdhc_imx35_data, },
	{ .compatible = "fsl,imx51-esdhc", .data = &esdhc_imx51_data, },
	{ .compatible = "fsl,imx53-esdhc", .data = &esdhc_imx53_data, },
	{ .compatible = "fsl,imx6sx-usdhc", .data = &usdhc_imx6sx_data, },
	{ .compatible = "fsl,imx6sl-usdhc", .data = &usdhc_imx6sl_data, },
	{ .compatible = "fsl,imx6sll-usdhc", .data = &usdhc_imx6sll_data, },
	{ .compatible = "fsl,imx6q-usdhc", .data = &usdhc_imx6q_data, },
	{ .compatible = "fsl,imx6ull-usdhc", .data = &usdhc_imx6ull_data, },
	{ .compatible = "fsl,imx7d-usdhc", .data = &usdhc_imx7d_data, },
	{ .compatible = "fsl,imx7ulp-usdhc", .data = &usdhc_imx7ulp_data, },
	{ .compatible = "fsl,imx8qxp-usdhc", .data = &usdhc_imx8qxp_data, },
	{ .compatible = "fsl,imx8mm-usdhc", .data = &usdhc_imx8mm_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_esdhc_dt_ids);

static inline int is_imx25_esdhc(struct pltfm_imx_data *data)
{
	return data->socdata == &esdhc_imx25_data;
}

static inline int is_imx53_esdhc(struct pltfm_imx_data *data)
{
	return data->socdata == &esdhc_imx53_data;
}

static inline int is_imx6q_usdhc(struct pltfm_imx_data *data)
{
	return data->socdata == &usdhc_imx6q_data;
}

static inline int esdhc_is_usdhc(struct pltfm_imx_data *data)
{
	return !!(data->socdata->flags & ESDHC_FLAG_USDHC);
}

static inline void esdhc_clrset_le(struct sdhci_host *host, u32 mask, u32 val, int reg)
{
	void __iomem *base = host->ioaddr + (reg & ~0x3);
	u32 shift = (reg & 0x3) * 8;

	writel(((readl(base) & ~(mask << shift)) | (val << shift)), base);
}

#define DRIVER_NAME "sdhci-esdhc-imx"
#define ESDHC_IMX_DUMP(f, x...) \
	pr_err("%s: " DRIVER_NAME ": " f, mmc_hostname(host->mmc), ## x)
static void esdhc_dump_debug_regs(struct sdhci_host *host)
{
	int i;
	char *debug_status[7] = {
				 "cmd debug status",
				 "data debug status",
				 "trans debug status",
				 "dma debug status",
				 "adma debug status",
				 "fifo debug status",
				 "async fifo debug status"
	};

	ESDHC_IMX_DUMP("========= ESDHC IMX DEBUG STATUS DUMP =========\n");
	for (i = 0; i < 7; i++) {
		esdhc_clrset_le(host, ESDHC_DEBUG_SEL_MASK,
			ESDHC_DEBUG_SEL_CMD_STATE + i, ESDHC_DEBUG_SEL_REG);
		ESDHC_IMX_DUMP("%s:  0x%04x\n", debug_status[i],
			readw(host->ioaddr + ESDHC_DEBUG_SEL_AND_STATUS_REG));
	}

	esdhc_clrset_le(host, ESDHC_DEBUG_SEL_MASK, 0, ESDHC_DEBUG_SEL_REG);

}

static inline void esdhc_wait_for_card_clock_gate_off(struct sdhci_host *host)
{
	u32 present_state;
	int ret;

	ret = readl_poll_timeout(host->ioaddr + ESDHC_PRSSTAT, present_state,
				(present_state & ESDHC_CLOCK_GATE_OFF), 2, 100);
	if (ret == -ETIMEDOUT)
		dev_warn(mmc_dev(host->mmc), "%s: card clock still not gate off in 100us!.\n", __func__);
}

static u32 esdhc_readl_le(struct sdhci_host *host, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	u32 val = readl(host->ioaddr + reg);

	if (unlikely(reg == SDHCI_PRESENT_STATE)) {
		u32 fsl_prss = val;
		/* save the least 20 bits */
		val = fsl_prss & 0x000FFFFF;
		/* move dat[0-3] bits */
		val |= (fsl_prss & 0x0F000000) >> 4;
		/* move cmd line bit */
		val |= (fsl_prss & 0x00800000) << 1;
	}

	if (unlikely(reg == SDHCI_CAPABILITIES)) {
		/* ignore bit[0-15] as it stores cap_1 register val for mx6sl */
		if (imx_data->socdata->flags & ESDHC_FLAG_HAVE_CAP1)
			val &= 0xffff0000;

		/* In FSL esdhc IC module, only bit20 is used to indicate the
		 * ADMA2 capability of esdhc, but this bit is messed up on
		 * some SOCs (e.g. on MX25, MX35 this bit is set, but they
		 * don't actually support ADMA2). So set the BROKEN_ADMA
		 * quirk on MX25/35 platforms.
		 */

		if (val & SDHCI_CAN_DO_ADMA1) {
			val &= ~SDHCI_CAN_DO_ADMA1;
			val |= SDHCI_CAN_DO_ADMA2;
		}
	}

	if (unlikely(reg == SDHCI_CAPABILITIES_1)) {
		if (esdhc_is_usdhc(imx_data)) {
			if (imx_data->socdata->flags & ESDHC_FLAG_HAVE_CAP1)
				val = readl(host->ioaddr + SDHCI_CAPABILITIES) & 0xFFFF;
			else
				/* imx6q/dl does not have cap_1 register, fake one */
				val = SDHCI_SUPPORT_DDR50 | SDHCI_SUPPORT_SDR104
					| SDHCI_SUPPORT_SDR50
					| SDHCI_USE_SDR50_TUNING
					| FIELD_PREP(SDHCI_RETUNING_MODE_MASK,
						     SDHCI_TUNING_MODE_3);

			if (imx_data->socdata->flags & ESDHC_FLAG_HS400)
				val |= SDHCI_SUPPORT_HS400;

			/*
			 * Do not advertise faster UHS modes if there are no
			 * pinctrl states for 100MHz/200MHz.
			 */
			if (IS_ERR_OR_NULL(imx_data->pins_100mhz) ||
			    IS_ERR_OR_NULL(imx_data->pins_200mhz))
				val &= ~(SDHCI_SUPPORT_SDR50 | SDHCI_SUPPORT_DDR50
					 | SDHCI_SUPPORT_SDR104 | SDHCI_SUPPORT_HS400);
		}
	}

	if (unlikely(reg == SDHCI_MAX_CURRENT) && esdhc_is_usdhc(imx_data)) {
		val = 0;
		val |= FIELD_PREP(SDHCI_MAX_CURRENT_330_MASK, 0xFF);
		val |= FIELD_PREP(SDHCI_MAX_CURRENT_300_MASK, 0xFF);
		val |= FIELD_PREP(SDHCI_MAX_CURRENT_180_MASK, 0xFF);
	}

	if (unlikely(reg == SDHCI_INT_STATUS)) {
		if (val & ESDHC_INT_VENDOR_SPEC_DMA_ERR) {
			val &= ~ESDHC_INT_VENDOR_SPEC_DMA_ERR;
			val |= SDHCI_INT_ADMA_ERROR;
		}

		/*
		 * mask off the interrupt we get in response to the manually
		 * sent CMD12
		 */
		if ((imx_data->multiblock_status == WAIT_FOR_INT) &&
		    ((val & SDHCI_INT_RESPONSE) == SDHCI_INT_RESPONSE)) {
			val &= ~SDHCI_INT_RESPONSE;
			writel(SDHCI_INT_RESPONSE, host->ioaddr +
						   SDHCI_INT_STATUS);
			imx_data->multiblock_status = NO_CMD_PENDING;
		}
	}

	return val;
}

static void esdhc_writel_le(struct sdhci_host *host, u32 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	u32 data;

	if (unlikely(reg == SDHCI_INT_ENABLE || reg == SDHCI_SIGNAL_ENABLE ||
			reg == SDHCI_INT_STATUS)) {
		if ((val & SDHCI_INT_CARD_INT) && !esdhc_is_usdhc(imx_data)) {
			/*
			 * Clear and then set D3CD bit to avoid missing the
			 * card interrupt. This is an eSDHC controller problem
			 * so we need to apply the following workaround: clear
			 * and set D3CD bit will make eSDHC re-sample the card
			 * interrupt. In case a card interrupt was lost,
			 * re-sample it by the following steps.
			 */
			data = readl(host->ioaddr + SDHCI_HOST_CONTROL);
			data &= ~ESDHC_CTRL_D3CD;
			writel(data, host->ioaddr + SDHCI_HOST_CONTROL);
			data |= ESDHC_CTRL_D3CD;
			writel(data, host->ioaddr + SDHCI_HOST_CONTROL);
		}

		if (val & SDHCI_INT_ADMA_ERROR) {
			val &= ~SDHCI_INT_ADMA_ERROR;
			val |= ESDHC_INT_VENDOR_SPEC_DMA_ERR;
		}
	}

	if (unlikely((imx_data->socdata->flags & ESDHC_FLAG_MULTIBLK_NO_INT)
				&& (reg == SDHCI_INT_STATUS)
				&& (val & SDHCI_INT_DATA_END))) {
			u32 v;
			v = readl(host->ioaddr + ESDHC_VENDOR_SPEC);
			v &= ~ESDHC_VENDOR_SPEC_SDIO_QUIRK;
			writel(v, host->ioaddr + ESDHC_VENDOR_SPEC);

			if (imx_data->multiblock_status == MULTIBLK_IN_PROCESS)
			{
				/* send a manual CMD12 with RESPTYP=none */
				data = MMC_STOP_TRANSMISSION << 24 |
				       SDHCI_CMD_ABORTCMD << 16;
				writel(data, host->ioaddr + SDHCI_TRANSFER_MODE);
				imx_data->multiblock_status = WAIT_FOR_INT;
			}
	}

	writel(val, host->ioaddr + reg);
}

static u16 esdhc_readw_le(struct sdhci_host *host, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	u16 ret = 0;
	u32 val;

	if (unlikely(reg == SDHCI_HOST_VERSION)) {
		reg ^= 2;
		if (esdhc_is_usdhc(imx_data)) {
			/*
			 * The usdhc register returns a wrong host version.
			 * Correct it here.
			 */
			return SDHCI_SPEC_300;
		}
	}

	if (unlikely(reg == SDHCI_HOST_CONTROL2)) {
		val = readl(host->ioaddr + ESDHC_VENDOR_SPEC);
		if (val & ESDHC_VENDOR_SPEC_VSELECT)
			ret |= SDHCI_CTRL_VDD_180;

		if (esdhc_is_usdhc(imx_data)) {
			if (imx_data->socdata->flags & ESDHC_FLAG_MAN_TUNING)
				val = readl(host->ioaddr + ESDHC_MIX_CTRL);
			else if (imx_data->socdata->flags & ESDHC_FLAG_STD_TUNING)
				/* the std tuning bits is in ACMD12_ERR for imx6sl */
				val = readl(host->ioaddr + SDHCI_AUTO_CMD_STATUS);
		}

		if (val & ESDHC_MIX_CTRL_EXE_TUNE)
			ret |= SDHCI_CTRL_EXEC_TUNING;
		if (val & ESDHC_MIX_CTRL_SMPCLK_SEL)
			ret |= SDHCI_CTRL_TUNED_CLK;

		ret &= ~SDHCI_CTRL_PRESET_VAL_ENABLE;

		return ret;
	}

	if (unlikely(reg == SDHCI_TRANSFER_MODE)) {
		if (esdhc_is_usdhc(imx_data)) {
			u32 m = readl(host->ioaddr + ESDHC_MIX_CTRL);
			ret = m & ESDHC_MIX_CTRL_SDHCI_MASK;
			/* Swap AC23 bit */
			if (m & ESDHC_MIX_CTRL_AC23EN) {
				ret &= ~ESDHC_MIX_CTRL_AC23EN;
				ret |= SDHCI_TRNS_AUTO_CMD23;
			}
		} else {
			ret = readw(host->ioaddr + SDHCI_TRANSFER_MODE);
		}

		return ret;
	}

	return readw(host->ioaddr + reg);
}

static void esdhc_writew_le(struct sdhci_host *host, u16 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	u32 new_val = 0;

	switch (reg) {
	case SDHCI_CLOCK_CONTROL:
		new_val = readl(host->ioaddr + ESDHC_VENDOR_SPEC);
		if (val & SDHCI_CLOCK_CARD_EN)
			new_val |= ESDHC_VENDOR_SPEC_FRC_SDCLK_ON;
		else
			new_val &= ~ESDHC_VENDOR_SPEC_FRC_SDCLK_ON;
		writel(new_val, host->ioaddr + ESDHC_VENDOR_SPEC);
		if (!(new_val & ESDHC_VENDOR_SPEC_FRC_SDCLK_ON))
			esdhc_wait_for_card_clock_gate_off(host);
		return;
	case SDHCI_HOST_CONTROL2:
		new_val = readl(host->ioaddr + ESDHC_VENDOR_SPEC);
		if (val & SDHCI_CTRL_VDD_180)
			new_val |= ESDHC_VENDOR_SPEC_VSELECT;
		else
			new_val &= ~ESDHC_VENDOR_SPEC_VSELECT;
		writel(new_val, host->ioaddr + ESDHC_VENDOR_SPEC);
		if (imx_data->socdata->flags & ESDHC_FLAG_MAN_TUNING) {
			new_val = readl(host->ioaddr + ESDHC_MIX_CTRL);
			if (val & SDHCI_CTRL_TUNED_CLK) {
				new_val |= ESDHC_MIX_CTRL_SMPCLK_SEL;
				new_val |= ESDHC_MIX_CTRL_AUTO_TUNE_EN;
			} else {
				new_val &= ~ESDHC_MIX_CTRL_SMPCLK_SEL;
				new_val &= ~ESDHC_MIX_CTRL_AUTO_TUNE_EN;
			}
			writel(new_val , host->ioaddr + ESDHC_MIX_CTRL);
		} else if (imx_data->socdata->flags & ESDHC_FLAG_STD_TUNING) {
			u32 v = readl(host->ioaddr + SDHCI_AUTO_CMD_STATUS);
			u32 m = readl(host->ioaddr + ESDHC_MIX_CTRL);
			if (val & SDHCI_CTRL_TUNED_CLK) {
				v |= ESDHC_MIX_CTRL_SMPCLK_SEL;
			} else {
				v &= ~ESDHC_MIX_CTRL_SMPCLK_SEL;
				m &= ~ESDHC_MIX_CTRL_FBCLK_SEL;
				m &= ~ESDHC_MIX_CTRL_AUTO_TUNE_EN;
			}

			if (val & SDHCI_CTRL_EXEC_TUNING) {
				v |= ESDHC_MIX_CTRL_EXE_TUNE;
				m |= ESDHC_MIX_CTRL_FBCLK_SEL;
				m |= ESDHC_MIX_CTRL_AUTO_TUNE_EN;
			} else {
				v &= ~ESDHC_MIX_CTRL_EXE_TUNE;
			}

			writel(v, host->ioaddr + SDHCI_AUTO_CMD_STATUS);
			writel(m, host->ioaddr + ESDHC_MIX_CTRL);
		}
		return;
	case SDHCI_TRANSFER_MODE:
		if ((imx_data->socdata->flags & ESDHC_FLAG_MULTIBLK_NO_INT)
				&& (host->cmd->opcode == SD_IO_RW_EXTENDED)
				&& (host->cmd->data->blocks > 1)
				&& (host->cmd->data->flags & MMC_DATA_READ)) {
			u32 v;
			v = readl(host->ioaddr + ESDHC_VENDOR_SPEC);
			v |= ESDHC_VENDOR_SPEC_SDIO_QUIRK;
			writel(v, host->ioaddr + ESDHC_VENDOR_SPEC);
		}

		if (esdhc_is_usdhc(imx_data)) {
			u32 wml;
			u32 m = readl(host->ioaddr + ESDHC_MIX_CTRL);
			/* Swap AC23 bit */
			if (val & SDHCI_TRNS_AUTO_CMD23) {
				val &= ~SDHCI_TRNS_AUTO_CMD23;
				val |= ESDHC_MIX_CTRL_AC23EN;
			}
			m = val | (m & ~ESDHC_MIX_CTRL_SDHCI_MASK);
			writel(m, host->ioaddr + ESDHC_MIX_CTRL);

			/* Set watermark levels for PIO access to maximum value
			 * (128 words) to accommodate full 512 bytes buffer.
			 * For DMA access restore the levels to default value.
			 */
			m = readl(host->ioaddr + ESDHC_WTMK_LVL);
			if (val & SDHCI_TRNS_DMA) {
				wml = ESDHC_WTMK_LVL_WML_VAL_DEF;
			} else {
				u8 ctrl;
				wml = ESDHC_WTMK_LVL_WML_VAL_MAX;

				/*
				 * Since already disable DMA mode, so also need
				 * to clear the DMASEL. Otherwise, for standard
				 * tuning, when send tuning command, usdhc will
				 * still prefetch the ADMA script from wrong
				 * DMA address, then we will see IOMMU report
				 * some error which show lack of TLB mapping.
				 */
				ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
				ctrl &= ~SDHCI_CTRL_DMA_MASK;
				sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
			}
			m &= ~(ESDHC_WTMK_LVL_RD_WML_MASK |
			       ESDHC_WTMK_LVL_WR_WML_MASK);
			m |= (wml << ESDHC_WTMK_LVL_RD_WML_SHIFT) |
			     (wml << ESDHC_WTMK_LVL_WR_WML_SHIFT);
			writel(m, host->ioaddr + ESDHC_WTMK_LVL);
		} else {
			/*
			 * Postpone this write, we must do it together with a
			 * command write that is down below.
			 */
			imx_data->scratchpad = val;
		}
		return;
	case SDHCI_COMMAND:
		if (host->cmd->opcode == MMC_STOP_TRANSMISSION)
			val |= SDHCI_CMD_ABORTCMD;

		if ((host->cmd->opcode == MMC_SET_BLOCK_COUNT) &&
		    (imx_data->socdata->flags & ESDHC_FLAG_MULTIBLK_NO_INT))
			imx_data->multiblock_status = MULTIBLK_IN_PROCESS;

		if (esdhc_is_usdhc(imx_data))
			writel(val << 16,
			       host->ioaddr + SDHCI_TRANSFER_MODE);
		else
			writel(val << 16 | imx_data->scratchpad,
			       host->ioaddr + SDHCI_TRANSFER_MODE);
		return;
	case SDHCI_BLOCK_SIZE:
		val &= ~SDHCI_MAKE_BLKSZ(0x7, 0);
		break;
	}
	esdhc_clrset_le(host, 0xffff, val, reg);
}

static u8 esdhc_readb_le(struct sdhci_host *host, int reg)
{
	u8 ret;
	u32 val;

	switch (reg) {
	case SDHCI_HOST_CONTROL:
		val = readl(host->ioaddr + reg);

		ret = val & SDHCI_CTRL_LED;
		ret |= (val >> 5) & SDHCI_CTRL_DMA_MASK;
		ret |= (val & ESDHC_CTRL_4BITBUS);
		ret |= (val & ESDHC_CTRL_8BITBUS) << 3;
		return ret;
	}

	return readb(host->ioaddr + reg);
}

static void esdhc_writeb_le(struct sdhci_host *host, u8 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	u32 new_val = 0;
	u32 mask;

	switch (reg) {
	case SDHCI_POWER_CONTROL:
		/*
		 * FSL put some DMA bits here
		 * If your board has a regulator, code should be here
		 */
		return;
	case SDHCI_HOST_CONTROL:
		/* FSL messed up here, so we need to manually compose it. */
		new_val = val & SDHCI_CTRL_LED;
		/* ensure the endianness */
		new_val |= ESDHC_HOST_CONTROL_LE;
		/* bits 8&9 are reserved on mx25 */
		if (!is_imx25_esdhc(imx_data)) {
			/* DMA mode bits are shifted */
			new_val |= (val & SDHCI_CTRL_DMA_MASK) << 5;
		}

		/*
		 * Do not touch buswidth bits here. This is done in
		 * esdhc_pltfm_bus_width.
		 * Do not touch the D3CD bit either which is used for the
		 * SDIO interrupt erratum workaround.
		 */
		mask = 0xffff & ~(ESDHC_CTRL_BUSWIDTH_MASK | ESDHC_CTRL_D3CD);

		esdhc_clrset_le(host, mask, new_val, reg);
		return;
	case SDHCI_SOFTWARE_RESET:
		if (val & SDHCI_RESET_DATA)
			new_val = readl(host->ioaddr + SDHCI_HOST_CONTROL);
		break;
	}
	esdhc_clrset_le(host, 0xff, val, reg);

	if (reg == SDHCI_SOFTWARE_RESET) {
		if (val & SDHCI_RESET_ALL) {
			/*
			 * The esdhc has a design violation to SDHC spec which
			 * tells that software reset should not affect card
			 * detection circuit. But esdhc clears its SYSCTL
			 * register bits [0..2] during the software reset. This
			 * will stop those clocks that card detection circuit
			 * relies on. To work around it, we turn the clocks on
			 * back to keep card detection circuit functional.
			 */
			esdhc_clrset_le(host, 0x7, 0x7, ESDHC_SYSTEM_CONTROL);
			/*
			 * The reset on usdhc fails to clear MIX_CTRL register.
			 * Do it manually here.
			 */
			if (esdhc_is_usdhc(imx_data)) {
				/*
				 * the tuning bits should be kept during reset
				 */
				new_val = readl(host->ioaddr + ESDHC_MIX_CTRL);
				writel(new_val & ESDHC_MIX_CTRL_TUNING_MASK,
						host->ioaddr + ESDHC_MIX_CTRL);
				imx_data->is_ddr = 0;
			}
		} else if (val & SDHCI_RESET_DATA) {
			/*
			 * The eSDHC DAT line software reset clears at least the
			 * data transfer width on i.MX25, so make sure that the
			 * Host Control register is unaffected.
			 */
			esdhc_clrset_le(host, 0xff, new_val,
					SDHCI_HOST_CONTROL);
		}
	}
}

static unsigned int esdhc_pltfm_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return pltfm_host->clock;
}

static unsigned int esdhc_pltfm_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return pltfm_host->clock / 256 / 16;
}

static inline void esdhc_pltfm_set_clock(struct sdhci_host *host,
					 unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	unsigned int host_clock = pltfm_host->clock;
	int ddr_pre_div = imx_data->is_ddr ? 2 : 1;
	int pre_div = 1;
	int div = 1;
	int ret;
	u32 temp, val;

	if (esdhc_is_usdhc(imx_data)) {
		val = readl(host->ioaddr + ESDHC_VENDOR_SPEC);
		writel(val & ~ESDHC_VENDOR_SPEC_FRC_SDCLK_ON,
			host->ioaddr + ESDHC_VENDOR_SPEC);
		esdhc_wait_for_card_clock_gate_off(host);
	}

	if (clock == 0) {
		host->mmc->actual_clock = 0;
		return;
	}

	/* For i.MX53 eSDHCv3, SYSCTL.SDCLKFS may not be set to 0. */
	if (is_imx53_esdhc(imx_data)) {
		/*
		 * According to the i.MX53 reference manual, if DLLCTRL[10] can
		 * be set, then the controller is eSDHCv3, else it is eSDHCv2.
		 */
		val = readl(host->ioaddr + ESDHC_DLL_CTRL);
		writel(val | BIT(10), host->ioaddr + ESDHC_DLL_CTRL);
		temp = readl(host->ioaddr + ESDHC_DLL_CTRL);
		writel(val, host->ioaddr + ESDHC_DLL_CTRL);
		if (temp & BIT(10))
			pre_div = 2;
	}

	temp = sdhci_readl(host, ESDHC_SYSTEM_CONTROL);
	temp &= ~(ESDHC_CLOCK_IPGEN | ESDHC_CLOCK_HCKEN | ESDHC_CLOCK_PEREN
		| ESDHC_CLOCK_MASK);
	sdhci_writel(host, temp, ESDHC_SYSTEM_CONTROL);

	if (imx_data->socdata->flags & ESDHC_FLAG_ERR010450) {
		unsigned int max_clock;

		max_clock = imx_data->is_ddr ? 45000000 : 150000000;

		clock = min(clock, max_clock);
	}

	while (host_clock / (16 * pre_div * ddr_pre_div) > clock &&
			pre_div < 256)
		pre_div *= 2;

	while (host_clock / (div * pre_div * ddr_pre_div) > clock && div < 16)
		div++;

	host->mmc->actual_clock = host_clock / (div * pre_div * ddr_pre_div);
	dev_dbg(mmc_dev(host->mmc), "desired SD clock: %d, actual: %d\n",
		clock, host->mmc->actual_clock);

	pre_div >>= 1;
	div--;

	temp = sdhci_readl(host, ESDHC_SYSTEM_CONTROL);
	temp |= (ESDHC_CLOCK_IPGEN | ESDHC_CLOCK_HCKEN | ESDHC_CLOCK_PEREN
		| (div << ESDHC_DIVIDER_SHIFT)
		| (pre_div << ESDHC_PREDIV_SHIFT));
	sdhci_writel(host, temp, ESDHC_SYSTEM_CONTROL);

	/* need to wait the bit 3 of the PRSSTAT to be set, make sure card clock is stable */
	ret = readl_poll_timeout(host->ioaddr + ESDHC_PRSSTAT, temp,
				(temp & ESDHC_CLOCK_STABLE), 2, 100);
	if (ret == -ETIMEDOUT)
		dev_warn(mmc_dev(host->mmc), "card clock still not stable in 100us!.\n");

	if (esdhc_is_usdhc(imx_data)) {
		val = readl(host->ioaddr + ESDHC_VENDOR_SPEC);
		writel(val | ESDHC_VENDOR_SPEC_FRC_SDCLK_ON,
			host->ioaddr + ESDHC_VENDOR_SPEC);
	}

}

static unsigned int esdhc_pltfm_get_ro(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	struct esdhc_platform_data *boarddata = &imx_data->boarddata;

	switch (boarddata->wp_type) {
	case ESDHC_WP_GPIO:
		return mmc_gpio_get_ro(host->mmc);
	case ESDHC_WP_CONTROLLER:
		return !(readl(host->ioaddr + SDHCI_PRESENT_STATE) &
			       SDHCI_WRITE_PROTECT);
	case ESDHC_WP_NONE:
		break;
	}

	return -ENOSYS;
}

static void esdhc_pltfm_set_bus_width(struct sdhci_host *host, int width)
{
	u32 ctrl;

	switch (width) {
	case MMC_BUS_WIDTH_8:
		ctrl = ESDHC_CTRL_8BITBUS;
		break;
	case MMC_BUS_WIDTH_4:
		ctrl = ESDHC_CTRL_4BITBUS;
		break;
	default:
		ctrl = 0;
		break;
	}

	esdhc_clrset_le(host, ESDHC_CTRL_BUSWIDTH_MASK, ctrl,
			SDHCI_HOST_CONTROL);
}

static int usdhc_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);

	/*
	 * i.MX uSDHC internally already uses a fixed optimized timing for
	 * DDR50, normally does not require tuning for DDR50 mode.
	 */
	if (host->timing == MMC_TIMING_UHS_DDR50)
		return 0;

	return sdhci_execute_tuning(mmc, opcode);
}

static void esdhc_prepare_tuning(struct sdhci_host *host, u32 val)
{
	u32 reg;
	u8 sw_rst;
	int ret;

	/* FIXME: delay a bit for card to be ready for next tuning due to errors */
	mdelay(1);

	/* IC suggest to reset USDHC before every tuning command */
	esdhc_clrset_le(host, 0xff, SDHCI_RESET_ALL, SDHCI_SOFTWARE_RESET);
	ret = readb_poll_timeout(host->ioaddr + SDHCI_SOFTWARE_RESET, sw_rst,
				!(sw_rst & SDHCI_RESET_ALL), 10, 100);
	if (ret == -ETIMEDOUT)
		dev_warn(mmc_dev(host->mmc),
		"warning! RESET_ALL never complete before sending tuning command\n");

	reg = readl(host->ioaddr + ESDHC_MIX_CTRL);
	reg |= ESDHC_MIX_CTRL_EXE_TUNE | ESDHC_MIX_CTRL_SMPCLK_SEL |
			ESDHC_MIX_CTRL_FBCLK_SEL;
	writel(reg, host->ioaddr + ESDHC_MIX_CTRL);
	writel(val << 8, host->ioaddr + ESDHC_TUNE_CTRL_STATUS);
	dev_dbg(mmc_dev(host->mmc),
		"tuning with delay 0x%x ESDHC_TUNE_CTRL_STATUS 0x%x\n",
			val, readl(host->ioaddr + ESDHC_TUNE_CTRL_STATUS));
}

static void esdhc_post_tuning(struct sdhci_host *host)
{
	u32 reg;

	reg = readl(host->ioaddr + ESDHC_MIX_CTRL);
	reg &= ~ESDHC_MIX_CTRL_EXE_TUNE;
	reg |= ESDHC_MIX_CTRL_AUTO_TUNE_EN;
	writel(reg, host->ioaddr + ESDHC_MIX_CTRL);
}

static int esdhc_executing_tuning(struct sdhci_host *host, u32 opcode)
{
	int min, max, avg, ret;

	/* find the mininum delay first which can pass tuning */
	min = ESDHC_TUNE_CTRL_MIN;
	while (min < ESDHC_TUNE_CTRL_MAX) {
		esdhc_prepare_tuning(host, min);
		if (!mmc_send_tuning(host->mmc, opcode, NULL))
			break;
		min += ESDHC_TUNE_CTRL_STEP;
	}

	/* find the maxinum delay which can not pass tuning */
	max = min + ESDHC_TUNE_CTRL_STEP;
	while (max < ESDHC_TUNE_CTRL_MAX) {
		esdhc_prepare_tuning(host, max);
		if (mmc_send_tuning(host->mmc, opcode, NULL)) {
			max -= ESDHC_TUNE_CTRL_STEP;
			break;
		}
		max += ESDHC_TUNE_CTRL_STEP;
	}

	/* use average delay to get the best timing */
	avg = (min + max) / 2;
	esdhc_prepare_tuning(host, avg);
	ret = mmc_send_tuning(host->mmc, opcode, NULL);
	esdhc_post_tuning(host);

	dev_dbg(mmc_dev(host->mmc), "tuning %s at 0x%x ret %d\n",
		ret ? "failed" : "passed", avg, ret);

	return ret;
}

static void esdhc_hs400_enhanced_strobe(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u32 m;

	m = readl(host->ioaddr + ESDHC_MIX_CTRL);
	if (ios->enhanced_strobe)
		m |= ESDHC_MIX_CTRL_HS400_ES_EN;
	else
		m &= ~ESDHC_MIX_CTRL_HS400_ES_EN;
	writel(m, host->ioaddr + ESDHC_MIX_CTRL);
}

static int esdhc_change_pinstate(struct sdhci_host *host,
						unsigned int uhs)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	struct pinctrl_state *pinctrl;

	dev_dbg(mmc_dev(host->mmc), "change pinctrl state for uhs %d\n", uhs);

	if (IS_ERR(imx_data->pinctrl) ||
		IS_ERR(imx_data->pins_100mhz) ||
		IS_ERR(imx_data->pins_200mhz))
		return -EINVAL;

	switch (uhs) {
	case MMC_TIMING_UHS_SDR50:
	case MMC_TIMING_UHS_DDR50:
		pinctrl = imx_data->pins_100mhz;
		break;
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS200:
	case MMC_TIMING_MMC_HS400:
		pinctrl = imx_data->pins_200mhz;
		break;
	default:
		/* back to default state for other legacy timing */
		return pinctrl_select_default_state(mmc_dev(host->mmc));
	}

	return pinctrl_select_state(imx_data->pinctrl, pinctrl);
}

/*
 * For HS400 eMMC, there is a data_strobe line. This signal is generated
 * by the device and used for data output and CRC status response output
 * in HS400 mode. The frequency of this signal follows the frequency of
 * CLK generated by host. The host receives the data which is aligned to the
 * edge of data_strobe line. Due to the time delay between CLK line and
 * data_strobe line, if the delay time is larger than one clock cycle,
 * then CLK and data_strobe line will be misaligned, read error shows up.
 */
static void esdhc_set_strobe_dll(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	u32 strobe_delay;
	u32 v;
	int ret;

	/* disable clock before enabling strobe dll */
	writel(readl(host->ioaddr + ESDHC_VENDOR_SPEC) &
		~ESDHC_VENDOR_SPEC_FRC_SDCLK_ON,
		host->ioaddr + ESDHC_VENDOR_SPEC);
	esdhc_wait_for_card_clock_gate_off(host);

	/* force a reset on strobe dll */
	writel(ESDHC_STROBE_DLL_CTRL_RESET,
		host->ioaddr + ESDHC_STROBE_DLL_CTRL);
	/* clear the reset bit on strobe dll before any setting */
	writel(0, host->ioaddr + ESDHC_STROBE_DLL_CTRL);

	/*
	 * enable strobe dll ctrl and adjust the delay target
	 * for the uSDHC loopback read clock
	 */
	if (imx_data->boarddata.strobe_dll_delay_target)
		strobe_delay = imx_data->boarddata.strobe_dll_delay_target;
	else
		strobe_delay = ESDHC_STROBE_DLL_CTRL_SLV_DLY_TARGET_DEFAULT;
	v = ESDHC_STROBE_DLL_CTRL_ENABLE |
		ESDHC_STROBE_DLL_CTRL_SLV_UPDATE_INT_DEFAULT |
		(strobe_delay << ESDHC_STROBE_DLL_CTRL_SLV_DLY_TARGET_SHIFT);
	writel(v, host->ioaddr + ESDHC_STROBE_DLL_CTRL);

	/* wait max 50us to get the REF/SLV lock */
	ret = readl_poll_timeout(host->ioaddr + ESDHC_STROBE_DLL_STATUS, v,
		((v & ESDHC_STROBE_DLL_STS_REF_LOCK) && (v & ESDHC_STROBE_DLL_STS_SLV_LOCK)), 1, 50);
	if (ret == -ETIMEDOUT)
		dev_warn(mmc_dev(host->mmc),
		"warning! HS400 strobe DLL status REF/SLV not lock in 50us, STROBE DLL status is %x!\n", v);
}

static void esdhc_reset_tuning(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	u32 ctrl;

	/* Reset the tuning circuit */
	if (esdhc_is_usdhc(imx_data)) {
		if (imx_data->socdata->flags & ESDHC_FLAG_MAN_TUNING) {
			ctrl = readl(host->ioaddr + ESDHC_MIX_CTRL);
			ctrl &= ~ESDHC_MIX_CTRL_SMPCLK_SEL;
			ctrl &= ~ESDHC_MIX_CTRL_FBCLK_SEL;
			writel(ctrl, host->ioaddr + ESDHC_MIX_CTRL);
			writel(0, host->ioaddr + ESDHC_TUNE_CTRL_STATUS);
		} else if (imx_data->socdata->flags & ESDHC_FLAG_STD_TUNING) {
			ctrl = readl(host->ioaddr + SDHCI_AUTO_CMD_STATUS);
			ctrl &= ~ESDHC_MIX_CTRL_SMPCLK_SEL;
			writel(ctrl, host->ioaddr + SDHCI_AUTO_CMD_STATUS);
		}
	}
}

static void esdhc_set_uhs_signaling(struct sdhci_host *host, unsigned timing)
{
	u32 m;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	struct esdhc_platform_data *boarddata = &imx_data->boarddata;

	/* disable ddr mode and disable HS400 mode */
	m = readl(host->ioaddr + ESDHC_MIX_CTRL);
	m &= ~(ESDHC_MIX_CTRL_DDREN | ESDHC_MIX_CTRL_HS400_EN);
	imx_data->is_ddr = 0;

	switch (timing) {
	case MMC_TIMING_UHS_SDR12:
	case MMC_TIMING_UHS_SDR25:
	case MMC_TIMING_UHS_SDR50:
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_MMC_HS200:
		writel(m, host->ioaddr + ESDHC_MIX_CTRL);
		break;
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		m |= ESDHC_MIX_CTRL_DDREN;
		writel(m, host->ioaddr + ESDHC_MIX_CTRL);
		imx_data->is_ddr = 1;
		if (boarddata->delay_line) {
			u32 v;
			v = boarddata->delay_line <<
				ESDHC_DLL_OVERRIDE_VAL_SHIFT |
				(1 << ESDHC_DLL_OVERRIDE_EN_SHIFT);
			if (is_imx53_esdhc(imx_data))
				v <<= 1;
			writel(v, host->ioaddr + ESDHC_DLL_CTRL);
		}
		break;
	case MMC_TIMING_MMC_HS400:
		m |= ESDHC_MIX_CTRL_DDREN | ESDHC_MIX_CTRL_HS400_EN;
		writel(m, host->ioaddr + ESDHC_MIX_CTRL);
		imx_data->is_ddr = 1;
		/* update clock after enable DDR for strobe DLL lock */
		host->ops->set_clock(host, host->clock);
		esdhc_set_strobe_dll(host);
		break;
	case MMC_TIMING_LEGACY:
	default:
		esdhc_reset_tuning(host);
		break;
	}

	esdhc_change_pinstate(host, timing);
}

static void esdhc_reset(struct sdhci_host *host, u8 mask)
{
	sdhci_reset(host, mask);

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
}

static unsigned int esdhc_get_max_timeout_count(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);

	/* Doc Erratum: the uSDHC actual maximum timeout count is 1 << 29 */
	return esdhc_is_usdhc(imx_data) ? 1 << 29 : 1 << 27;
}

static void esdhc_set_timeout(struct sdhci_host *host, struct mmc_command *cmd)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);

	/* use maximum timeout counter */
	esdhc_clrset_le(host, ESDHC_SYS_CTRL_DTOCV_MASK,
			esdhc_is_usdhc(imx_data) ? 0xF : 0xE,
			SDHCI_TIMEOUT_CONTROL);
}

static u32 esdhc_cqhci_irq(struct sdhci_host *host, u32 intmask)
{
	int cmd_error = 0;
	int data_error = 0;

	if (!sdhci_cqe_irq(host, intmask, &cmd_error, &data_error))
		return intmask;

	cqhci_irq(host->mmc, intmask, cmd_error, data_error);

	return 0;
}

static struct sdhci_ops sdhci_esdhc_ops = {
	.read_l = esdhc_readl_le,
	.read_w = esdhc_readw_le,
	.read_b = esdhc_readb_le,
	.write_l = esdhc_writel_le,
	.write_w = esdhc_writew_le,
	.write_b = esdhc_writeb_le,
	.set_clock = esdhc_pltfm_set_clock,
	.get_max_clock = esdhc_pltfm_get_max_clock,
	.get_min_clock = esdhc_pltfm_get_min_clock,
	.get_max_timeout_count = esdhc_get_max_timeout_count,
	.get_ro = esdhc_pltfm_get_ro,
	.set_timeout = esdhc_set_timeout,
	.set_bus_width = esdhc_pltfm_set_bus_width,
	.set_uhs_signaling = esdhc_set_uhs_signaling,
	.reset = esdhc_reset,
	.irq = esdhc_cqhci_irq,
	.dump_vendor_regs = esdhc_dump_debug_regs,
};

static const struct sdhci_pltfm_data sdhci_esdhc_imx_pdata = {
	.quirks = ESDHC_DEFAULT_QUIRKS | SDHCI_QUIRK_NO_HISPD_BIT
			| SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC
			| SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC
			| SDHCI_QUIRK_BROKEN_CARD_DETECTION,
	.ops = &sdhci_esdhc_ops,
};

static void sdhci_esdhc_imx_hwinit(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	struct cqhci_host *cq_host = host->mmc->cqe_private;
	int tmp;

	if (esdhc_is_usdhc(imx_data)) {
		/*
		 * The imx6q ROM code will change the default watermark
		 * level setting to something insane.  Change it back here.
		 */
		writel(ESDHC_WTMK_DEFAULT_VAL, host->ioaddr + ESDHC_WTMK_LVL);

		/*
		 * ROM code will change the bit burst_length_enable setting
		 * to zero if this usdhc is chosen to boot system. Change
		 * it back here, otherwise it will impact the performance a
		 * lot. This bit is used to enable/disable the burst length
		 * for the external AHB2AXI bridge. It's useful especially
		 * for INCR transfer because without burst length indicator,
		 * the AHB2AXI bridge does not know the burst length in
		 * advance. And without burst length indicator, AHB INCR
		 * transfer can only be converted to singles on the AXI side.
		 */
		writel(readl(host->ioaddr + SDHCI_HOST_CONTROL)
			| ESDHC_BURST_LEN_EN_INCR,
			host->ioaddr + SDHCI_HOST_CONTROL);

		/*
		 * erratum ESDHC_FLAG_ERR004536 fix for MX6Q TO1.2 and MX6DL
		 * TO1.1, it's harmless for MX6SL
		 */
		writel(readl(host->ioaddr + 0x6c) & ~BIT(7),
			host->ioaddr + 0x6c);

		/* disable DLL_CTRL delay line settings */
		writel(0x0, host->ioaddr + ESDHC_DLL_CTRL);

		/*
		 * For the case of command with busy, if set the bit
		 * ESDHC_VEND_SPEC2_EN_BUSY_IRQ, USDHC will generate a
		 * transfer complete interrupt when busy is deasserted.
		 * When CQHCI use DCMD to send a CMD need R1b respons,
		 * CQHCI require to set ESDHC_VEND_SPEC2_EN_BUSY_IRQ,
		 * otherwise DCMD will always meet timeout waiting for
		 * hardware interrupt issue.
		 */
		if (imx_data->socdata->flags & ESDHC_FLAG_CQHCI) {
			tmp = readl(host->ioaddr + ESDHC_VEND_SPEC2);
			tmp |= ESDHC_VEND_SPEC2_EN_BUSY_IRQ;
			writel(tmp, host->ioaddr + ESDHC_VEND_SPEC2);

			host->quirks &= ~SDHCI_QUIRK_NO_BUSY_IRQ;
		}

		if (imx_data->socdata->flags & ESDHC_FLAG_STD_TUNING) {
			tmp = readl(host->ioaddr + ESDHC_TUNING_CTRL);
			tmp |= ESDHC_STD_TUNING_EN |
				ESDHC_TUNING_START_TAP_DEFAULT;
			if (imx_data->boarddata.tuning_start_tap) {
				tmp &= ~ESDHC_TUNING_START_TAP_MASK;
				tmp |= imx_data->boarddata.tuning_start_tap;
			}

			if (imx_data->boarddata.tuning_step) {
				tmp &= ~ESDHC_TUNING_STEP_MASK;
				tmp |= imx_data->boarddata.tuning_step
					<< ESDHC_TUNING_STEP_SHIFT;
			}

			/* Disable the CMD CRC check for tuning, if not, need to
			 * add some delay after every tuning command, because
			 * hardware standard tuning logic will directly go to next
			 * step once it detect the CMD CRC error, will not wait for
			 * the card side to finally send out the tuning data, trigger
			 * the buffer read ready interrupt immediately. If usdhc send
			 * the next tuning command some eMMC card will stuck, can't
			 * response, block the tuning procedure or the first command
			 * after the whole tuning procedure always can't get any response.
			 */
			tmp |= ESDHC_TUNING_CMD_CRC_CHECK_DISABLE;
			writel(tmp, host->ioaddr + ESDHC_TUNING_CTRL);
		} else if (imx_data->socdata->flags & ESDHC_FLAG_MAN_TUNING) {
			/*
			 * ESDHC_STD_TUNING_EN may be configed in bootloader
			 * or ROM code, so clear this bit here to make sure
			 * the manual tuning can work.
			 */
			tmp = readl(host->ioaddr + ESDHC_TUNING_CTRL);
			tmp &= ~ESDHC_STD_TUNING_EN;
			writel(tmp, host->ioaddr + ESDHC_TUNING_CTRL);
		}

		/*
		 * On i.MX8MM, we are running Dual Linux OS, with 1st Linux using SD Card
		 * as rootfs storage, 2nd Linux using eMMC as rootfs storage. We let the
		 * the 1st linux configure power/clock for the 2nd Linux.
		 *
		 * When the 2nd Linux is booting into rootfs stage, we let the 1st Linux
		 * to destroy the 2nd linux, then restart the 2nd linux, we met SDHCI dump.
		 * After we clear the pending interrupt and halt CQCTL, issue gone.
		 */
		if (cq_host) {
			tmp = cqhci_readl(cq_host, CQHCI_IS);
			cqhci_writel(cq_host, tmp, CQHCI_IS);
			cqhci_writel(cq_host, CQHCI_HALT, CQHCI_CTL);
		}
	}
}

static void esdhc_cqe_enable(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct cqhci_host *cq_host = mmc->cqe_private;
	u32 reg;
	u16 mode;
	int count = 10;

	/*
	 * CQE gets stuck if it sees Buffer Read Enable bit set, which can be
	 * the case after tuning, so ensure the buffer is drained.
	 */
	reg = sdhci_readl(host, SDHCI_PRESENT_STATE);
	while (reg & SDHCI_DATA_AVAILABLE) {
		sdhci_readl(host, SDHCI_BUFFER);
		reg = sdhci_readl(host, SDHCI_PRESENT_STATE);
		if (count-- == 0) {
			dev_warn(mmc_dev(host->mmc),
				"CQE may get stuck because the Buffer Read Enable bit is set\n");
			break;
		}
		mdelay(1);
	}

	/*
	 * Runtime resume will reset the entire host controller, which
	 * will also clear the DMAEN/BCEN of register ESDHC_MIX_CTRL.
	 * Here set DMAEN and BCEN when enable CMDQ.
	 */
	mode = sdhci_readw(host, SDHCI_TRANSFER_MODE);
	if (host->flags & SDHCI_REQ_USE_DMA)
		mode |= SDHCI_TRNS_DMA;
	if (!(host->quirks2 & SDHCI_QUIRK2_SUPPORT_SINGLE))
		mode |= SDHCI_TRNS_BLK_CNT_EN;
	sdhci_writew(host, mode, SDHCI_TRANSFER_MODE);

	/*
	 * Though Runtime resume reset the entire host controller,
	 * but do not impact the CQHCI side, need to clear the
	 * HALT bit, avoid CQHCI stuck in the first request when
	 * system resume back.
	 */
	cqhci_writel(cq_host, 0, CQHCI_CTL);
	if (cqhci_readl(cq_host, CQHCI_CTL) && CQHCI_HALT)
		dev_err(mmc_dev(host->mmc),
			"failed to exit halt state when enable CQE\n");


	sdhci_cqe_enable(mmc);
}

static void esdhc_sdhci_dumpregs(struct mmc_host *mmc)
{
	sdhci_dumpregs(mmc_priv(mmc));
}

static const struct cqhci_host_ops esdhc_cqhci_ops = {
	.enable		= esdhc_cqe_enable,
	.disable	= sdhci_cqe_disable,
	.dumpregs	= esdhc_sdhci_dumpregs,
};

#ifdef CONFIG_OF
static int
sdhci_esdhc_imx_probe_dt(struct platform_device *pdev,
			 struct sdhci_host *host,
			 struct pltfm_imx_data *imx_data)
{
	struct device_node *np = pdev->dev.of_node;
	struct esdhc_platform_data *boarddata = &imx_data->boarddata;
	int ret;

	if (of_get_property(np, "fsl,wp-controller", NULL))
		boarddata->wp_type = ESDHC_WP_CONTROLLER;

	/*
	 * If we have this property, then activate WP check.
	 * Retrieveing and requesting the actual WP GPIO will happen
	 * in the call to mmc_of_parse().
	 */
	if (of_property_read_bool(np, "wp-gpios"))
		boarddata->wp_type = ESDHC_WP_GPIO;

	of_property_read_u32(np, "fsl,tuning-step", &boarddata->tuning_step);
	of_property_read_u32(np, "fsl,tuning-start-tap",
			     &boarddata->tuning_start_tap);

	of_property_read_u32(np, "fsl,strobe-dll-delay-target",
				&boarddata->strobe_dll_delay_target);
	if (of_find_property(np, "no-1-8-v", NULL))
		host->quirks2 |= SDHCI_QUIRK2_NO_1_8_V;

	if (of_property_read_u32(np, "fsl,delay-line", &boarddata->delay_line))
		boarddata->delay_line = 0;

	mmc_of_parse_voltage(np, &host->ocr_mask);

	if (esdhc_is_usdhc(imx_data)) {
		imx_data->pins_100mhz = pinctrl_lookup_state(imx_data->pinctrl,
						ESDHC_PINCTRL_STATE_100MHZ);
		imx_data->pins_200mhz = pinctrl_lookup_state(imx_data->pinctrl,
						ESDHC_PINCTRL_STATE_200MHZ);
	}

	/* call to generic mmc_of_parse to support additional capabilities */
	ret = mmc_of_parse(host->mmc);
	if (ret)
		return ret;

	if (mmc_gpio_get_cd(host->mmc) >= 0)
		host->quirks &= ~SDHCI_QUIRK_BROKEN_CARD_DETECTION;

	return 0;
}
#else
static inline int
sdhci_esdhc_imx_probe_dt(struct platform_device *pdev,
			 struct sdhci_host *host,
			 struct pltfm_imx_data *imx_data)
{
	return -ENODEV;
}
#endif

static int sdhci_esdhc_imx_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(imx_esdhc_dt_ids, &pdev->dev);
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_host *host;
	struct cqhci_host *cq_host;
	int err;
	struct pltfm_imx_data *imx_data;

	host = sdhci_pltfm_init(pdev, &sdhci_esdhc_imx_pdata,
				sizeof(*imx_data));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);

	imx_data = sdhci_pltfm_priv(pltfm_host);

	imx_data->socdata = of_id->data;

	if (imx_data->socdata->flags & ESDHC_FLAG_PMQOS)
		cpu_latency_qos_add_request(&imx_data->pm_qos_req, 0);

	imx_data->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(imx_data->clk_ipg)) {
		err = PTR_ERR(imx_data->clk_ipg);
		goto free_sdhci;
	}

	imx_data->clk_ahb = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(imx_data->clk_ahb)) {
		err = PTR_ERR(imx_data->clk_ahb);
		goto free_sdhci;
	}

	imx_data->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(imx_data->clk_per)) {
		err = PTR_ERR(imx_data->clk_per);
		goto free_sdhci;
	}

	pltfm_host->clk = imx_data->clk_per;
	pltfm_host->clock = clk_get_rate(pltfm_host->clk);
	err = clk_prepare_enable(imx_data->clk_per);
	if (err)
		goto free_sdhci;
	err = clk_prepare_enable(imx_data->clk_ipg);
	if (err)
		goto disable_per_clk;
	err = clk_prepare_enable(imx_data->clk_ahb);
	if (err)
		goto disable_ipg_clk;

	imx_data->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(imx_data->pinctrl))
		dev_warn(mmc_dev(host->mmc), "could not get pinctrl\n");

	if (esdhc_is_usdhc(imx_data)) {
		host->quirks2 |= SDHCI_QUIRK2_PRESET_VALUE_BROKEN;
		host->mmc->caps |= MMC_CAP_1_8V_DDR | MMC_CAP_3_3V_DDR;

		/* GPIO CD can be set as a wakeup source */
		host->mmc->caps |= MMC_CAP_CD_WAKE;

		if (!(imx_data->socdata->flags & ESDHC_FLAG_HS200))
			host->quirks2 |= SDHCI_QUIRK2_BROKEN_HS200;

		/* clear tuning bits in case ROM has set it already */
		writel(0x0, host->ioaddr + ESDHC_MIX_CTRL);
		writel(0x0, host->ioaddr + SDHCI_AUTO_CMD_STATUS);
		writel(0x0, host->ioaddr + ESDHC_TUNE_CTRL_STATUS);

		/*
		 * Link usdhc specific mmc_host_ops execute_tuning function,
		 * to replace the standard one in sdhci_ops.
		 */
		host->mmc_host_ops.execute_tuning = usdhc_execute_tuning;
	}

	if (imx_data->socdata->flags & ESDHC_FLAG_MAN_TUNING)
		sdhci_esdhc_ops.platform_execute_tuning =
					esdhc_executing_tuning;

	if (imx_data->socdata->flags & ESDHC_FLAG_ERR004536)
		host->quirks |= SDHCI_QUIRK_BROKEN_ADMA;

	if (imx_data->socdata->flags & ESDHC_FLAG_HS400)
		host->quirks2 |= SDHCI_QUIRK2_CAPS_BIT63_FOR_HS400;

	if (imx_data->socdata->flags & ESDHC_FLAG_BROKEN_AUTO_CMD23)
		host->quirks2 |= SDHCI_QUIRK2_ACMD23_BROKEN;

	if (imx_data->socdata->flags & ESDHC_FLAG_HS400_ES) {
		host->mmc->caps2 |= MMC_CAP2_HS400_ES;
		host->mmc_host_ops.hs400_enhanced_strobe =
					esdhc_hs400_enhanced_strobe;
	}

	if (imx_data->socdata->flags & ESDHC_FLAG_CQHCI) {
		host->mmc->caps2 |= MMC_CAP2_CQE | MMC_CAP2_CQE_DCMD;
		cq_host = devm_kzalloc(&pdev->dev, sizeof(*cq_host), GFP_KERNEL);
		if (!cq_host) {
			err = -ENOMEM;
			goto disable_ahb_clk;
		}

		cq_host->mmio = host->ioaddr + ESDHC_CQHCI_ADDR_OFFSET;
		cq_host->ops = &esdhc_cqhci_ops;

		err = cqhci_init(cq_host, host->mmc, false);
		if (err)
			goto disable_ahb_clk;
	}

	err = sdhci_esdhc_imx_probe_dt(pdev, host, imx_data);
	if (err)
		goto disable_ahb_clk;

	sdhci_esdhc_imx_hwinit(host);

	err = sdhci_add_host(host);
	if (err)
		goto disable_ahb_clk;

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 50);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_suspend_ignore_children(&pdev->dev, 1);
	pm_runtime_enable(&pdev->dev);

	return 0;

disable_ahb_clk:
	clk_disable_unprepare(imx_data->clk_ahb);
disable_ipg_clk:
	clk_disable_unprepare(imx_data->clk_ipg);
disable_per_clk:
	clk_disable_unprepare(imx_data->clk_per);
free_sdhci:
	if (imx_data->socdata->flags & ESDHC_FLAG_PMQOS)
		cpu_latency_qos_remove_request(&imx_data->pm_qos_req);
	sdhci_pltfm_free(pdev);
	return err;
}

static int sdhci_esdhc_imx_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	int dead;

	pm_runtime_get_sync(&pdev->dev);
	dead = (readl(host->ioaddr + SDHCI_INT_STATUS) == 0xffffffff);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	sdhci_remove_host(host, dead);

	clk_disable_unprepare(imx_data->clk_per);
	clk_disable_unprepare(imx_data->clk_ipg);
	clk_disable_unprepare(imx_data->clk_ahb);

	if (imx_data->socdata->flags & ESDHC_FLAG_PMQOS)
		cpu_latency_qos_remove_request(&imx_data->pm_qos_req);

	sdhci_pltfm_free(pdev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sdhci_esdhc_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	int ret;

	if (host->mmc->caps2 & MMC_CAP2_CQE) {
		ret = cqhci_suspend(host->mmc);
		if (ret)
			return ret;
	}

	if ((imx_data->socdata->flags & ESDHC_FLAG_STATE_LOST_IN_LPMODE) &&
		(host->tuning_mode != SDHCI_TUNING_MODE_1)) {
		mmc_retune_timer_stop(host->mmc);
		mmc_retune_needed(host->mmc);
	}

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	ret = sdhci_suspend_host(host);
	if (ret)
		return ret;

	ret = pinctrl_pm_select_sleep_state(dev);
	if (ret)
		return ret;

	ret = mmc_gpio_set_cd_wake(host->mmc, true);

	return ret;
}

static int sdhci_esdhc_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret)
		return ret;

	/* re-initialize hw state in case it's lost in low power mode */
	sdhci_esdhc_imx_hwinit(host);

	ret = sdhci_resume_host(host);
	if (ret)
		return ret;

	if (host->mmc->caps2 & MMC_CAP2_CQE)
		ret = cqhci_resume(host->mmc);

	if (!ret)
		ret = mmc_gpio_set_cd_wake(host->mmc, false);

	return ret;
}
#endif

#ifdef CONFIG_PM
static int sdhci_esdhc_runtime_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	int ret;

	if (host->mmc->caps2 & MMC_CAP2_CQE) {
		ret = cqhci_suspend(host->mmc);
		if (ret)
			return ret;
	}

	ret = sdhci_runtime_suspend_host(host);
	if (ret)
		return ret;

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	imx_data->actual_clock = host->mmc->actual_clock;
	esdhc_pltfm_set_clock(host, 0);
	clk_disable_unprepare(imx_data->clk_per);
	clk_disable_unprepare(imx_data->clk_ipg);
	clk_disable_unprepare(imx_data->clk_ahb);

	if (imx_data->socdata->flags & ESDHC_FLAG_PMQOS)
		cpu_latency_qos_remove_request(&imx_data->pm_qos_req);

	return ret;
}

static int sdhci_esdhc_runtime_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = sdhci_pltfm_priv(pltfm_host);
	int err;

	if (imx_data->socdata->flags & ESDHC_FLAG_PMQOS)
		cpu_latency_qos_add_request(&imx_data->pm_qos_req, 0);

	if (imx_data->socdata->flags & ESDHC_FLAG_CLK_RATE_LOST_IN_PM_RUNTIME)
		clk_set_rate(imx_data->clk_per, pltfm_host->clock);

	err = clk_prepare_enable(imx_data->clk_ahb);
	if (err)
		goto remove_pm_qos_request;

	err = clk_prepare_enable(imx_data->clk_per);
	if (err)
		goto disable_ahb_clk;

	err = clk_prepare_enable(imx_data->clk_ipg);
	if (err)
		goto disable_per_clk;

	esdhc_pltfm_set_clock(host, imx_data->actual_clock);

	err = sdhci_runtime_resume_host(host, 0);
	if (err)
		goto disable_ipg_clk;

	if (host->mmc->caps2 & MMC_CAP2_CQE)
		err = cqhci_resume(host->mmc);

	return err;

disable_ipg_clk:
	clk_disable_unprepare(imx_data->clk_ipg);
disable_per_clk:
	clk_disable_unprepare(imx_data->clk_per);
disable_ahb_clk:
	clk_disable_unprepare(imx_data->clk_ahb);
remove_pm_qos_request:
	if (imx_data->socdata->flags & ESDHC_FLAG_PMQOS)
		cpu_latency_qos_remove_request(&imx_data->pm_qos_req);
	return err;
}
#endif

static const struct dev_pm_ops sdhci_esdhc_pmops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdhci_esdhc_suspend, sdhci_esdhc_resume)
	SET_RUNTIME_PM_OPS(sdhci_esdhc_runtime_suspend,
				sdhci_esdhc_runtime_resume, NULL)
};

static struct platform_driver sdhci_esdhc_imx_driver = {
	.driver		= {
		.name	= "sdhci-esdhc-imx",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = imx_esdhc_dt_ids,
		.pm	= &sdhci_esdhc_pmops,
	},
	.probe		= sdhci_esdhc_imx_probe,
	.remove		= sdhci_esdhc_imx_remove,
};

module_platform_driver(sdhci_esdhc_imx_driver);

MODULE_DESCRIPTION("SDHCI driver for Freescale i.MX eSDHC");
MODULE_AUTHOR("Wolfram Sang <kernel@pengutronix.de>");
MODULE_LICENSE("GPL v2");
