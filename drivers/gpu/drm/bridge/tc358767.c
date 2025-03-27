// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TC358767/TC358867/TC9595 DSI/DPI-to-DPI/(e)DP bridge driver
 *
 * The TC358767/TC358867/TC9595 can operate in multiple modes.
 * All modes are supported -- DPI->(e)DP / DSI->DPI / DSI->(e)DP .
 *
 * Copyright (C) 2016 CogentEmbedded Inc
 * Author: Andrey Gusakov <andrey.gusakov@cogentembedded.com>
 *
 * Copyright (C) 2016 Pengutronix, Philipp Zabel <p.zabel@pengutronix.de>
 *
 * Copyright (C) 2016 Zodiac Inflight Innovations
 *
 * Initially based on: drivers/gpu/drm/i2c/tda998x_drv.c
 *
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

/* Registers */

/* DSI D-PHY Layer registers */
#define D0W_DPHYCONTTX		0x0004
#define CLW_DPHYCONTTX		0x0020
#define D0W_DPHYCONTRX		0x0024
#define D1W_DPHYCONTRX		0x0028
#define D2W_DPHYCONTRX		0x002c
#define D3W_DPHYCONTRX		0x0030
#define COM_DPHYCONTRX		0x0038
#define CLW_CNTRL		0x0040
#define D0W_CNTRL		0x0044
#define D1W_CNTRL		0x0048
#define D2W_CNTRL		0x004c
#define D3W_CNTRL		0x0050
#define TESTMODE_CNTRL		0x0054

/* PPI layer registers */
#define PPI_STARTPPI		0x0104 /* START control bit */
#define PPI_BUSYPPI		0x0108 /* PPI busy status */
#define PPI_LPTXTIMECNT		0x0114 /* LPTX timing signal */
#define LPX_PERIOD			3
#define PPI_LANEENABLE		0x0134
#define PPI_TX_RX_TA		0x013c
#define TTA_GET				0x40000
#define TTA_SURE			6
#define PPI_D0S_ATMR		0x0144
#define PPI_D1S_ATMR		0x0148
#define PPI_D0S_CLRSIPOCOUNT	0x0164 /* Assertion timer for Lane 0 */
#define PPI_D1S_CLRSIPOCOUNT	0x0168 /* Assertion timer for Lane 1 */
#define PPI_D2S_CLRSIPOCOUNT	0x016c /* Assertion timer for Lane 2 */
#define PPI_D3S_CLRSIPOCOUNT	0x0170 /* Assertion timer for Lane 3 */
#define PPI_START_FUNCTION		BIT(0)

/* DSI layer registers */
#define DSI_STARTDSI		0x0204 /* START control bit of DSI-TX */
#define DSI_BUSYDSI		0x0208 /* DSI busy status */
#define DSI_LANEENABLE		0x0210 /* Enables each lane */
#define DSI_RX_START			BIT(0)

/* Lane enable PPI and DSI register bits */
#define LANEENABLE_CLEN		BIT(0)
#define LANEENABLE_L0EN		BIT(1)
#define LANEENABLE_L1EN		BIT(2)
#define LANEENABLE_L2EN		BIT(1)
#define LANEENABLE_L3EN		BIT(2)

#define DSI_LANESTATUS0		0x0214	/* DSI lane status 0 */
#define DSI_LANESTATUS1		0x0218	/* DSI lane status 1 */
#define DSI_INTSTATUS		0x0220	/* Interrupt Status */
#define DSI_INTMASK		0x0224	/* Interrupt Mask */
#define DSI_INTCLR		0x0228	/* Interrupt Clear */
#define DSI_LPTXTO		0x0230	/* LPTX Time Out Counter */

/* DSI General Registers */
#define DSIERRCNT		0x0300	/* DSI Error Count Register */

/* DSI Application Layer Registers */
#define APLCTRL			0x0400	/* Application layer Control Register */
#define RDPKTLN			0x0404	/* DSI Read packet Length Register */

/* Display Parallel Input Interface */
#define DPIPXLFMT		0x0440
#define VS_POL_ACTIVE_LOW		(1 << 10)
#define HS_POL_ACTIVE_LOW		(1 << 9)
#define DE_POL_ACTIVE_HIGH		(0 << 8)
#define SUB_CFG_TYPE_CONFIG1		(0 << 2) /* LSB aligned */
#define SUB_CFG_TYPE_CONFIG2		(1 << 2) /* Loosely Packed */
#define SUB_CFG_TYPE_CONFIG3		(2 << 2) /* LSB aligned 8-bit */
#define DPI_BPP_RGB888			(0 << 0)
#define DPI_BPP_RGB666			(1 << 0)
#define DPI_BPP_RGB565			(2 << 0)

/* Display Parallel Output Interface */
#define POCTRL			0x0448
#define POCTRL_S2P			BIT(7)
#define POCTRL_PCLK_POL			BIT(3)
#define POCTRL_VS_POL			BIT(2)
#define POCTRL_HS_POL			BIT(1)
#define POCTRL_DE_POL			BIT(0)

/* Video Path */
#define VPCTRL0			0x0450
#define VSDELAY			GENMASK(31, 20)
#define OPXLFMT_RGB666			(0 << 8)
#define OPXLFMT_RGB888			(1 << 8)
#define FRMSYNC_DISABLED		(0 << 4) /* Video Timing Gen Disabled */
#define FRMSYNC_ENABLED			(1 << 4) /* Video Timing Gen Enabled */
#define MSF_DISABLED			(0 << 0) /* Magic Square FRC disabled */
#define MSF_ENABLED			(1 << 0) /* Magic Square FRC enabled */
#define HTIM01			0x0454
#define HPW			GENMASK(8, 0)
#define HBPR			GENMASK(24, 16)
#define HTIM02			0x0458
#define HDISPR			GENMASK(10, 0)
#define HFPR			GENMASK(24, 16)
#define VTIM01			0x045c
#define VSPR			GENMASK(7, 0)
#define VBPR			GENMASK(23, 16)
#define VTIM02			0x0460
#define VFPR			GENMASK(23, 16)
#define VDISPR			GENMASK(10, 0)
#define VFUEN0			0x0464
#define VFUEN				BIT(0)   /* Video Frame Timing Upload */

/* System */
#define TC_IDREG		0x0500	/* Chip ID and Revision ID */
#define SYSBOOT			0x0504	/* System BootStrap Status Register */
#define SYSSTAT			0x0508	/* System Status Register */
#define SYSRSTENB		0x050c /* System Reset/Enable Register */
#define ENBI2C				(1 << 0)
#define ENBLCD0				(1 << 2)
#define ENBBM				(1 << 3)
#define ENBDSIRX			(1 << 4)
#define ENBREG				(1 << 5)
#define ENBHDCP				(1 << 8)
#define SYSCTRL			0x0510	/* System Control Register */
#define DP0_AUDSRC_NO_INPUT		(0 << 3)
#define DP0_AUDSRC_I2S_RX		(1 << 3)
#define DP0_VIDSRC_NO_INPUT		(0 << 0)
#define DP0_VIDSRC_DSI_RX		(1 << 0)
#define DP0_VIDSRC_DPI_RX		(2 << 0)
#define DP0_VIDSRC_COLOR_BAR		(3 << 0)
#define GPIOM			0x0540	/* GPIO Mode Control Register */
#define GPIOC			0x0544	/* GPIO Direction Control Register */
#define GPIOO			0x0548	/* GPIO Output Register */
#define GPIOI			0x054c	/* GPIO Input Register */
#define INTCTL_G		0x0560	/* General Interrupts Control Register */
#define INTSTS_G		0x0564	/* General Interrupts Status Register */

#define INT_SYSERR		BIT(16)
#define INT_GPIO_H(x)		(1 << (x == 0 ? 2 : 10))
#define INT_GPIO_LC(x)		(1 << (x == 0 ? 3 : 11))

#define TEST_INT_C		0x0570	/* Test Interrupts Control Register */
#define TEST_INT_S		0x0574	/* Test Interrupts Status Register */

#define INT_GP0_LCNT		0x0584	/* Interrupt GPIO0 Low Count Value Register */
#define INT_GP1_LCNT		0x0588	/* Interrupt GPIO1 Low Count Value Register */

/* Control */
#define DP0CTL			0x0600
#define VID_MN_GEN			BIT(6)   /* Auto-generate M/N values */
#define EF_EN				BIT(5)   /* Enable Enhanced Framing */
#define VID_EN				BIT(1)   /* Video transmission enable */
#define DP_EN				BIT(0)   /* Enable DPTX function */

/* Clocks */
#define DP0_VIDMNGEN0		0x0610	/* DP0 Video Force M Value Register */
#define DP0_VIDMNGEN1		0x0614	/* DP0 Video Force N Value Register */
#define DP0_VMNGENSTATUS	0x0618	/* DP0 Video Current M Value Register */
#define DP0_AUDMNGEN0		0x0628	/* DP0 Audio Force M Value Register */
#define DP0_AUDMNGEN1		0x062c	/* DP0 Audio Force N Value Register */
#define DP0_AMNGENSTATUS	0x0630	/* DP0 Audio Current M Value Register */

/* Main Channel */
#define DP0_SECSAMPLE		0x0640
#define DP0_VIDSYNCDELAY	0x0644
#define VID_SYNC_DLY		GENMASK(15, 0)
#define THRESH_DLY		GENMASK(31, 16)

#define DP0_TOTALVAL		0x0648
#define H_TOTAL			GENMASK(15, 0)
#define V_TOTAL			GENMASK(31, 16)
#define DP0_STARTVAL		0x064c
#define H_START			GENMASK(15, 0)
#define V_START			GENMASK(31, 16)
#define DP0_ACTIVEVAL		0x0650
#define H_ACT			GENMASK(15, 0)
#define V_ACT			GENMASK(31, 16)

#define DP0_SYNCVAL		0x0654
#define VS_WIDTH		GENMASK(30, 16)
#define HS_WIDTH		GENMASK(14, 0)
#define SYNCVAL_HS_POL_ACTIVE_LOW	(1 << 15)
#define SYNCVAL_VS_POL_ACTIVE_LOW	(1 << 31)
#define DP0_MISC		0x0658
#define TU_SIZE_RECOMMENDED		(63) /* LSCLK cycles per TU */
#define MAX_TU_SYMBOL		GENMASK(28, 23)
#define TU_SIZE			GENMASK(21, 16)
#define BPC_6				(0 << 5)
#define BPC_8				(1 << 5)

/* AUX channel */
#define DP0_AUXCFG0		0x0660
#define DP0_AUXCFG0_BSIZE	GENMASK(11, 8)
#define DP0_AUXCFG0_ADDR_ONLY	BIT(4)
#define DP0_AUXCFG1		0x0664
#define AUX_RX_FILTER_EN		BIT(16)

#define DP0_AUXADDR		0x0668
#define DP0_AUXWDATA(i)		(0x066c + (i) * 4)
#define DP0_AUXRDATA(i)		(0x067c + (i) * 4)
#define DP0_AUXSTATUS		0x068c
#define AUX_BYTES		GENMASK(15, 8)
#define AUX_STATUS		GENMASK(7, 4)
#define AUX_TIMEOUT		BIT(1)
#define AUX_BUSY		BIT(0)
#define DP0_AUXI2CADR		0x0698

/* Link Training */
#define DP0_SRCCTRL		0x06a0
#define DP0_SRCCTRL_PRE1		GENMASK(29, 28)
#define DP0_SRCCTRL_SWG1		GENMASK(25, 24)
#define DP0_SRCCTRL_PRE0		GENMASK(21, 20)
#define DP0_SRCCTRL_SWG0		GENMASK(17, 16)
#define DP0_SRCCTRL_SCRMBLDIS		BIT(13)
#define DP0_SRCCTRL_EN810B		BIT(12)
#define DP0_SRCCTRL_NOTP		(0 << 8)
#define DP0_SRCCTRL_TP1			(1 << 8)
#define DP0_SRCCTRL_TP2			(2 << 8)
#define DP0_SRCCTRL_LANESKEW		BIT(7)
#define DP0_SRCCTRL_SSCG		BIT(3)
#define DP0_SRCCTRL_LANES_1		(0 << 2)
#define DP0_SRCCTRL_LANES_2		(1 << 2)
#define DP0_SRCCTRL_BW27		(1 << 1)
#define DP0_SRCCTRL_BW162		(0 << 1)
#define DP0_SRCCTRL_AUTOCORRECT		BIT(0)
#define DP0_LTSTAT		0x06d0
#define LT_LOOPDONE			BIT(13)
#define LT_STATUS_MASK			(0x1f << 8)
#define LT_CHANNEL1_EQ_BITS		(DP_CHANNEL_EQ_BITS << 4)
#define LT_INTERLANE_ALIGN_DONE		BIT(3)
#define LT_CHANNEL0_EQ_BITS		(DP_CHANNEL_EQ_BITS)
#define DP0_SNKLTCHGREQ		0x06d4
#define DP0_LTLOOPCTRL		0x06d8
#define DP0_SNKLTCTRL		0x06e4
#define DP0_TPATDAT0		0x06e8	/* DP0 Test Pattern bits 29 to 0 */
#define DP0_TPATDAT1		0x06ec	/* DP0 Test Pattern bits 59 to 30 */
#define DP0_TPATDAT2		0x06f0	/* DP0 Test Pattern bits 89 to 60 */
#define DP0_TPATDAT3		0x06f4	/* DP0 Test Pattern bits 119 to 90 */

#define AUDCFG0			0x0700	/* DP0 Audio Config0 Register */
#define AUDCFG1			0x0704	/* DP0 Audio Config1 Register */
#define AUDIFDATA0		0x0708	/* DP0 Audio Info Frame Bytes 3 to 0 */
#define AUDIFDATA1		0x070c	/* DP0 Audio Info Frame Bytes 7 to 4 */
#define AUDIFDATA2		0x0710	/* DP0 Audio Info Frame Bytes 11 to 8 */
#define AUDIFDATA3		0x0714	/* DP0 Audio Info Frame Bytes 15 to 12 */
#define AUDIFDATA4		0x0718	/* DP0 Audio Info Frame Bytes 19 to 16 */
#define AUDIFDATA5		0x071c	/* DP0 Audio Info Frame Bytes 23 to 20 */
#define AUDIFDATA6		0x0720	/* DP0 Audio Info Frame Bytes 27 to 24 */

#define DP1_SRCCTRL		0x07a0	/* DP1 Control Register */
#define DP1_SRCCTRL_PRE			GENMASK(21, 20)
#define DP1_SRCCTRL_SWG			GENMASK(17, 16)

/* PHY */
#define DP_PHY_CTRL		0x0800
#define DP_PHY_RST			BIT(28)  /* DP PHY Global Soft Reset */
#define BGREN				BIT(25)  /* AUX PHY BGR Enable */
#define PWR_SW_EN			BIT(24)  /* PHY Power Switch Enable */
#define PHY_M1_RST			BIT(12)  /* Reset PHY1 Main Channel */
#define PHY_RDY				BIT(16)  /* PHY Main Channels Ready */
#define PHY_M0_RST			BIT(8)   /* Reset PHY0 Main Channel */
#define PHY_2LANE			BIT(2)   /* PHY Enable 2 lanes */
#define PHY_A0_EN			BIT(1)   /* PHY Aux Channel0 Enable */
#define PHY_M0_EN			BIT(0)   /* PHY Main Channel0 Enable */
#define DP_PHY_CFG_WR		0x0810	/* DP PHY Configuration Test Write Register */
#define DP_PHY_CFG_RD		0x0814	/* DP PHY Configuration Test Read Register */
#define DP0_AUX_PHY_CTRL	0x0820	/* DP0 AUX PHY Control Register */
#define DP0_MAIN_PHY_DBG	0x0840	/* DP0 Main PHY Test Debug Register */

/* I2S */
#define I2SCFG			0x0880	/* I2S Audio Config 0 Register */
#define I2SCH0STAT0		0x0888	/* I2S Audio Channel 0 Status Bytes 3 to 0 */
#define I2SCH0STAT1		0x088c	/* I2S Audio Channel 0 Status Bytes 7 to 4 */
#define I2SCH0STAT2		0x0890	/* I2S Audio Channel 0 Status Bytes 11 to 8 */
#define I2SCH0STAT3		0x0894	/* I2S Audio Channel 0 Status Bytes 15 to 12 */
#define I2SCH0STAT4		0x0898	/* I2S Audio Channel 0 Status Bytes 19 to 16 */
#define I2SCH0STAT5		0x089c	/* I2S Audio Channel 0 Status Bytes 23 to 20 */
#define I2SCH1STAT0		0x08a0	/* I2S Audio Channel 1 Status Bytes 3 to 0 */
#define I2SCH1STAT1		0x08a4	/* I2S Audio Channel 1 Status Bytes 7 to 4 */
#define I2SCH1STAT2		0x08a8	/* I2S Audio Channel 1 Status Bytes 11 to 8 */
#define I2SCH1STAT3		0x08ac	/* I2S Audio Channel 1 Status Bytes 15 to 12 */
#define I2SCH1STAT4		0x08b0	/* I2S Audio Channel 1 Status Bytes 19 to 16 */
#define I2SCH1STAT5		0x08b4	/* I2S Audio Channel 1 Status Bytes 23 to 20 */

/* PLL */
#define DP0_PLLCTRL		0x0900
#define DP1_PLLCTRL		0x0904	/* not defined in DS */
#define PXL_PLLCTRL		0x0908
#define PLLUPDATE			BIT(2)
#define PLLBYP				BIT(1)
#define PLLEN				BIT(0)
#define PXL_PLLPARAM		0x0914
#define IN_SEL_REFCLK			(0 << 14)
#define SYS_PLLPARAM		0x0918
#define REF_FREQ_38M4			(0 << 8) /* 38.4 MHz */
#define REF_FREQ_19M2			(1 << 8) /* 19.2 MHz */
#define REF_FREQ_26M			(2 << 8) /* 26 MHz */
#define REF_FREQ_13M			(3 << 8) /* 13 MHz */
#define SYSCLK_SEL_LSCLK		(0 << 4)
#define LSCLK_DIV_1			(0 << 0)
#define LSCLK_DIV_2			(1 << 0)

/* Test & Debug */
#define TSTCTL			0x0a00
#define COLOR_R			GENMASK(31, 24)
#define COLOR_G			GENMASK(23, 16)
#define COLOR_B			GENMASK(15, 8)
#define ENI2CFILTER		BIT(4)
#define COLOR_BAR_MODE		GENMASK(1, 0)
#define COLOR_BAR_MODE_BARS	2
#define PLL_DBG			0x0a04

static bool tc_test_pattern;
module_param_named(test, tc_test_pattern, bool, 0644);

struct tc_edp_link {
	u8			dpcd[DP_RECEIVER_CAP_SIZE];
	unsigned int		rate;
	u8			num_lanes;
	u8			assr;
	bool			scrambler_dis;
	bool			spread;
};

struct tc_data {
	struct device		*dev;
	struct regmap		*regmap;
	struct drm_dp_aux	aux;

	struct drm_bridge	bridge;
	struct drm_bridge	*panel_bridge;
	struct drm_connector	connector;

	struct mipi_dsi_device	*dsi;

	/* link settings */
	struct tc_edp_link	link;

	/* current mode */
	struct drm_display_mode	mode;

	u32			rev;
	u8			assr;
	u8			pre_emphasis[2];

	struct gpio_desc	*sd_gpio;
	struct gpio_desc	*reset_gpio;
	struct clk		*refclk;

	/* do we have IRQ */
	bool			have_irq;

	/* Input connector type, DSI and not DPI. */
	bool			input_connector_dsi;

	/* HPD pin number (0 or 1) or -ENODEV */
	int			hpd_pin;
};

static inline struct tc_data *aux_to_tc(struct drm_dp_aux *a)
{
	return container_of(a, struct tc_data, aux);
}

static inline struct tc_data *bridge_to_tc(struct drm_bridge *b)
{
	return container_of(b, struct tc_data, bridge);
}

static inline struct tc_data *connector_to_tc(struct drm_connector *c)
{
	return container_of(c, struct tc_data, connector);
}

static inline int tc_poll_timeout(struct tc_data *tc, unsigned int addr,
				  unsigned int cond_mask,
				  unsigned int cond_value,
				  unsigned long sleep_us, u64 timeout_us)
{
	unsigned int val;

	return regmap_read_poll_timeout(tc->regmap, addr, val,
					(val & cond_mask) == cond_value,
					sleep_us, timeout_us);
}

static int tc_aux_wait_busy(struct tc_data *tc)
{
	return tc_poll_timeout(tc, DP0_AUXSTATUS, AUX_BUSY, 0, 100, 100000);
}

static int tc_aux_write_data(struct tc_data *tc, const void *data,
			     size_t size)
{
	u32 auxwdata[DP_AUX_MAX_PAYLOAD_BYTES / sizeof(u32)] = { 0 };
	int ret, count = ALIGN(size, sizeof(u32));

	memcpy(auxwdata, data, size);

	ret = regmap_raw_write(tc->regmap, DP0_AUXWDATA(0), auxwdata, count);
	if (ret)
		return ret;

	return size;
}

static int tc_aux_read_data(struct tc_data *tc, void *data, size_t size)
{
	u32 auxrdata[DP_AUX_MAX_PAYLOAD_BYTES / sizeof(u32)];
	int ret, count = ALIGN(size, sizeof(u32));

	ret = regmap_raw_read(tc->regmap, DP0_AUXRDATA(0), auxrdata, count);
	if (ret)
		return ret;

	memcpy(data, auxrdata, size);

	return size;
}

static u32 tc_auxcfg0(struct drm_dp_aux_msg *msg, size_t size)
{
	u32 auxcfg0 = msg->request;

	if (size)
		auxcfg0 |= FIELD_PREP(DP0_AUXCFG0_BSIZE, size - 1);
	else
		auxcfg0 |= DP0_AUXCFG0_ADDR_ONLY;

	return auxcfg0;
}

static ssize_t tc_aux_transfer(struct drm_dp_aux *aux,
			       struct drm_dp_aux_msg *msg)
{
	struct tc_data *tc = aux_to_tc(aux);
	size_t size = min_t(size_t, DP_AUX_MAX_PAYLOAD_BYTES - 1, msg->size);
	u8 request = msg->request & ~DP_AUX_I2C_MOT;
	u32 auxstatus;
	int ret;

	ret = tc_aux_wait_busy(tc);
	if (ret)
		return ret;

	switch (request) {
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		break;
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
		if (size) {
			ret = tc_aux_write_data(tc, msg->buffer, size);
			if (ret < 0)
				return ret;
		}
		break;
	default:
		return -EINVAL;
	}

	/* Store address */
	ret = regmap_write(tc->regmap, DP0_AUXADDR, msg->address);
	if (ret)
		return ret;
	/* Start transfer */
	ret = regmap_write(tc->regmap, DP0_AUXCFG0, tc_auxcfg0(msg, size));
	if (ret)
		return ret;

	ret = tc_aux_wait_busy(tc);
	if (ret)
		return ret;

	ret = regmap_read(tc->regmap, DP0_AUXSTATUS, &auxstatus);
	if (ret)
		return ret;

	if (auxstatus & AUX_TIMEOUT)
		return -ETIMEDOUT;
	/*
	 * For some reason address-only DP_AUX_I2C_WRITE (MOT), still
	 * reports 1 byte transferred in its status. To deal we that
	 * we ignore aux_bytes field if we know that this was an
	 * address-only transfer
	 */
	if (size)
		size = FIELD_GET(AUX_BYTES, auxstatus);
	msg->reply = FIELD_GET(AUX_STATUS, auxstatus);

	switch (request) {
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		if (size)
			return tc_aux_read_data(tc, msg->buffer, size);
		break;
	}

	return size;
}

static const char * const training_pattern1_errors[] = {
	"No errors",
	"Aux write error",
	"Aux read error",
	"Max voltage reached error",
	"Loop counter expired error",
	"res", "res", "res"
};

static const char * const training_pattern2_errors[] = {
	"No errors",
	"Aux write error",
	"Aux read error",
	"Clock recovery failed error",
	"Loop counter expired error",
	"res", "res", "res"
};

static u32 tc_srcctrl(struct tc_data *tc)
{
	/*
	 * No training pattern, skew lane 1 data by two LSCLK cycles with
	 * respect to lane 0 data, AutoCorrect Mode = 0
	 */
	u32 reg = DP0_SRCCTRL_NOTP | DP0_SRCCTRL_LANESKEW | DP0_SRCCTRL_EN810B;

	if (tc->link.scrambler_dis)
		reg |= DP0_SRCCTRL_SCRMBLDIS;	/* Scrambler Disabled */
	if (tc->link.spread)
		reg |= DP0_SRCCTRL_SSCG;	/* Spread Spectrum Enable */
	if (tc->link.num_lanes == 2)
		reg |= DP0_SRCCTRL_LANES_2;	/* Two Main Channel Lanes */
	if (tc->link.rate != 162000)
		reg |= DP0_SRCCTRL_BW27;	/* 2.7 Gbps link */
	return reg;
}

static int tc_pllupdate(struct tc_data *tc, unsigned int pllctrl)
{
	int ret;

	ret = regmap_write(tc->regmap, pllctrl, PLLUPDATE | PLLEN);
	if (ret)
		return ret;

	/* Wait for PLL to lock: up to 7.5 ms, depending on refclk */
	usleep_range(15000, 20000);

	return 0;
}

static int tc_pxl_pll_calc(struct tc_data *tc, u32 refclk, u32 pixelclock,
			   int *out_best_pixelclock, u32 *out_pxl_pllparam)
{
	int i_pre, best_pre = 1;
	int i_post, best_post = 1;
	int div, best_div = 1;
	int mul, best_mul = 1;
	int delta, best_delta;
	int ext_div[] = {1, 2, 3, 5, 7};
	int clk_min, clk_max;
	int best_pixelclock = 0;
	int vco_hi = 0;
	u32 pxl_pllparam;

	/*
	 * refclk * mul / (ext_pre_div * pre_div) should be in range:
	 * - DPI ..... 0 to 100 MHz
	 * - (e)DP ... 150 to 650 MHz
	 */
	if (tc->bridge.type == DRM_MODE_CONNECTOR_DPI) {
		clk_min = 0;
		clk_max = 100000000;
	} else {
		clk_min = 150000000;
		clk_max = 650000000;
	}

	dev_dbg(tc->dev, "PLL: requested %d pixelclock, ref %d\n", pixelclock,
		refclk);
	best_delta = pixelclock;
	/* Loop over all possible ext_divs, skipping invalid configurations */
	for (i_pre = 0; i_pre < ARRAY_SIZE(ext_div); i_pre++) {
		/*
		 * refclk / ext_pre_div should be in the 1 to 200 MHz range.
		 * We don't allow any refclk > 200 MHz, only check lower bounds.
		 */
		if (refclk / ext_div[i_pre] < 1000000)
			continue;
		for (i_post = 0; i_post < ARRAY_SIZE(ext_div); i_post++) {
			for (div = 1; div <= 16; div++) {
				u32 clk, iclk;
				u64 tmp;

				/* PCLK PLL input unit clock ... 6..40 MHz */
				iclk = refclk / (div * ext_div[i_pre]);
				if (iclk < 6000000 || iclk > 40000000)
					continue;

				tmp = pixelclock * ext_div[i_pre] *
				      ext_div[i_post] * div;
				do_div(tmp, refclk);
				mul = tmp;

				/* Check limits */
				if ((mul < 1) || (mul > 128))
					continue;

				clk = (refclk / ext_div[i_pre] / div) * mul;
				if ((clk > clk_max) || (clk < clk_min))
					continue;

				clk = clk / ext_div[i_post];
				delta = clk - pixelclock;

				if (abs(delta) < abs(best_delta)) {
					best_pre = i_pre;
					best_post = i_post;
					best_div = div;
					best_mul = mul;
					best_delta = delta;
					best_pixelclock = clk;
				}
			}
		}
	}
	if (best_pixelclock == 0) {
		dev_err(tc->dev, "Failed to calc clock for %d pixelclock\n",
			pixelclock);
		return -EINVAL;
	}

	dev_dbg(tc->dev, "PLL: got %d, delta %d\n", best_pixelclock, best_delta);
	dev_dbg(tc->dev, "PLL: %d / %d / %d * %d / %d\n", refclk,
		ext_div[best_pre], best_div, best_mul, ext_div[best_post]);

	/* if VCO >= 300 MHz */
	if (refclk / ext_div[best_pre] / best_div * best_mul >= 300000000)
		vco_hi = 1;
	/* see DS */
	if (best_div == 16)
		best_div = 0;
	if (best_mul == 128)
		best_mul = 0;

	pxl_pllparam  = vco_hi << 24; /* For PLL VCO >= 300 MHz = 1 */
	pxl_pllparam |= ext_div[best_pre] << 20; /* External Pre-divider */
	pxl_pllparam |= ext_div[best_post] << 16; /* External Post-divider */
	pxl_pllparam |= IN_SEL_REFCLK; /* Use RefClk as PLL input */
	pxl_pllparam |= best_div << 8; /* Divider for PLL RefClk */
	pxl_pllparam |= best_mul; /* Multiplier for PLL */

	if (out_best_pixelclock)
		*out_best_pixelclock = best_pixelclock;

	if (out_pxl_pllparam)
		*out_pxl_pllparam = pxl_pllparam;

	return 0;
}

static int tc_pxl_pll_en(struct tc_data *tc, u32 refclk, u32 pixelclock)
{
	u32 pxl_pllparam = 0;
	int ret;

	ret = tc_pxl_pll_calc(tc, refclk, pixelclock, NULL, &pxl_pllparam);
	if (ret)
		return ret;

	/* Power up PLL and switch to bypass */
	ret = regmap_write(tc->regmap, PXL_PLLCTRL, PLLBYP | PLLEN);
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, PXL_PLLPARAM, pxl_pllparam);
	if (ret)
		return ret;

	/* Force PLL parameter update and disable bypass */
	return tc_pllupdate(tc, PXL_PLLCTRL);
}

static int tc_pxl_pll_dis(struct tc_data *tc)
{
	/* Enable PLL bypass, power down PLL */
	return regmap_write(tc->regmap, PXL_PLLCTRL, PLLBYP);
}

static int tc_stream_clock_calc(struct tc_data *tc)
{
	/*
	 * If the Stream clock and Link Symbol clock are
	 * asynchronous with each other, the value of M changes over
	 * time. This way of generating link clock and stream
	 * clock is called Asynchronous Clock mode. The value M
	 * must change while the value N stays constant. The
	 * value of N in this Asynchronous Clock mode must be set
	 * to 2^15 or 32,768.
	 *
	 * LSCLK = 1/10 of high speed link clock
	 *
	 * f_STRMCLK = M/N * f_LSCLK
	 * M/N = f_STRMCLK / f_LSCLK
	 *
	 */
	return regmap_write(tc->regmap, DP0_VIDMNGEN1, 32768);
}

static int tc_set_syspllparam(struct tc_data *tc)
{
	unsigned long rate;
	u32 pllparam = SYSCLK_SEL_LSCLK | LSCLK_DIV_1;

	rate = clk_get_rate(tc->refclk);
	switch (rate) {
	case 38400000:
		pllparam |= REF_FREQ_38M4;
		break;
	case 26000000:
		pllparam |= REF_FREQ_26M;
		break;
	case 19200000:
		pllparam |= REF_FREQ_19M2;
		break;
	case 13000000:
		pllparam |= REF_FREQ_13M;
		break;
	default:
		dev_err(tc->dev, "Invalid refclk rate: %lu Hz\n", rate);
		return -EINVAL;
	}

	return regmap_write(tc->regmap, SYS_PLLPARAM, pllparam);
}

static int tc_aux_link_setup(struct tc_data *tc)
{
	int ret;
	u32 dp0_auxcfg1;

	/* Setup DP-PHY / PLL */
	ret = tc_set_syspllparam(tc);
	if (ret)
		goto err;

	ret = regmap_write(tc->regmap, DP_PHY_CTRL,
			   BGREN | PWR_SW_EN | PHY_A0_EN);
	if (ret)
		goto err;
	/*
	 * Initially PLLs are in bypass. Force PLL parameter update,
	 * disable PLL bypass, enable PLL
	 */
	ret = tc_pllupdate(tc, DP0_PLLCTRL);
	if (ret)
		goto err;

	ret = tc_pllupdate(tc, DP1_PLLCTRL);
	if (ret)
		goto err;

	ret = tc_poll_timeout(tc, DP_PHY_CTRL, PHY_RDY, PHY_RDY, 100, 100000);
	if (ret == -ETIMEDOUT) {
		dev_err(tc->dev, "Timeout waiting for PHY to become ready");
		return ret;
	} else if (ret) {
		goto err;
	}

	/* Setup AUX link */
	dp0_auxcfg1  = AUX_RX_FILTER_EN;
	dp0_auxcfg1 |= 0x06 << 8; /* Aux Bit Period Calculator Threshold */
	dp0_auxcfg1 |= 0x3f << 0; /* Aux Response Timeout Timer */

	ret = regmap_write(tc->regmap, DP0_AUXCFG1, dp0_auxcfg1);
	if (ret)
		goto err;

	/* Register DP AUX channel */
	tc->aux.name = "TC358767 AUX i2c adapter";
	tc->aux.dev = tc->dev;
	tc->aux.transfer = tc_aux_transfer;
	drm_dp_aux_init(&tc->aux);

	return 0;
err:
	dev_err(tc->dev, "tc_aux_link_setup failed: %d\n", ret);
	return ret;
}

static int tc_get_display_props(struct tc_data *tc)
{
	u8 revision, num_lanes;
	unsigned int rate;
	int ret;
	u8 reg;

	/* Read DP Rx Link Capability */
	ret = drm_dp_dpcd_read(&tc->aux, DP_DPCD_REV, tc->link.dpcd,
			       DP_RECEIVER_CAP_SIZE);
	if (ret < 0)
		goto err_dpcd_read;

	revision = tc->link.dpcd[DP_DPCD_REV];
	rate = drm_dp_max_link_rate(tc->link.dpcd);
	num_lanes = drm_dp_max_lane_count(tc->link.dpcd);

	if (rate != 162000 && rate != 270000) {
		dev_dbg(tc->dev, "Falling to 2.7 Gbps rate\n");
		rate = 270000;
	}

	tc->link.rate = rate;

	if (num_lanes > 2) {
		dev_dbg(tc->dev, "Falling to 2 lanes\n");
		num_lanes = 2;
	}

	tc->link.num_lanes = num_lanes;

	ret = drm_dp_dpcd_readb(&tc->aux, DP_MAX_DOWNSPREAD, &reg);
	if (ret < 0)
		goto err_dpcd_read;
	tc->link.spread = reg & DP_MAX_DOWNSPREAD_0_5;

	ret = drm_dp_dpcd_readb(&tc->aux, DP_MAIN_LINK_CHANNEL_CODING, &reg);
	if (ret < 0)
		goto err_dpcd_read;

	tc->link.scrambler_dis = false;
	/* read assr */
	ret = drm_dp_dpcd_readb(&tc->aux, DP_EDP_CONFIGURATION_SET, &reg);
	if (ret < 0)
		goto err_dpcd_read;
	tc->link.assr = reg & DP_ALTERNATE_SCRAMBLER_RESET_ENABLE;

	dev_dbg(tc->dev, "DPCD rev: %d.%d, rate: %s, lanes: %d, framing: %s\n",
		revision >> 4, revision & 0x0f,
		(tc->link.rate == 162000) ? "1.62Gbps" : "2.7Gbps",
		tc->link.num_lanes,
		drm_dp_enhanced_frame_cap(tc->link.dpcd) ?
		"enhanced" : "default");
	dev_dbg(tc->dev, "Downspread: %s, scrambler: %s\n",
		tc->link.spread ? "0.5%" : "0.0%",
		tc->link.scrambler_dis ? "disabled" : "enabled");
	dev_dbg(tc->dev, "Display ASSR: %d, TC358767 ASSR: %d\n",
		tc->link.assr, tc->assr);

	return 0;

err_dpcd_read:
	dev_err(tc->dev, "failed to read DPCD: %d\n", ret);
	return ret;
}

static int tc_set_common_video_mode(struct tc_data *tc,
				    const struct drm_display_mode *mode)
{
	int left_margin = mode->htotal - mode->hsync_end;
	int right_margin = mode->hsync_start - mode->hdisplay;
	int hsync_len = mode->hsync_end - mode->hsync_start;
	int upper_margin = mode->vtotal - mode->vsync_end;
	int lower_margin = mode->vsync_start - mode->vdisplay;
	int vsync_len = mode->vsync_end - mode->vsync_start;
	int ret;

	dev_dbg(tc->dev, "set mode %dx%d\n",
		mode->hdisplay, mode->vdisplay);
	dev_dbg(tc->dev, "H margin %d,%d sync %d\n",
		left_margin, right_margin, hsync_len);
	dev_dbg(tc->dev, "V margin %d,%d sync %d\n",
		upper_margin, lower_margin, vsync_len);
	dev_dbg(tc->dev, "total: %dx%d\n", mode->htotal, mode->vtotal);

	/*
	 * LCD Ctl Frame Size
	 * datasheet is not clear of vsdelay in case of DPI
	 * assume we do not need any delay when DPI is a source of
	 * sync signals
	 */
	ret = regmap_write(tc->regmap, VPCTRL0,
			   FIELD_PREP(VSDELAY, right_margin + 10) |
			   OPXLFMT_RGB888 | FRMSYNC_ENABLED | MSF_DISABLED);
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, HTIM01,
			   FIELD_PREP(HBPR, ALIGN(left_margin, 2)) |
			   FIELD_PREP(HPW, ALIGN(hsync_len, 2)));
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, HTIM02,
			   FIELD_PREP(HDISPR, ALIGN(mode->hdisplay, 2)) |
			   FIELD_PREP(HFPR, ALIGN(right_margin, 2)));
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, VTIM01,
			   FIELD_PREP(VBPR, upper_margin) |
			   FIELD_PREP(VSPR, vsync_len));
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, VTIM02,
			   FIELD_PREP(VFPR, lower_margin) |
			   FIELD_PREP(VDISPR, mode->vdisplay));
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, VFUEN0, VFUEN); /* update settings */
	if (ret)
		return ret;

	/* Test pattern settings */
	ret = regmap_write(tc->regmap, TSTCTL,
			   FIELD_PREP(COLOR_R, 120) |
			   FIELD_PREP(COLOR_G, 20) |
			   FIELD_PREP(COLOR_B, 99) |
			   ENI2CFILTER |
			   FIELD_PREP(COLOR_BAR_MODE, COLOR_BAR_MODE_BARS));

	return ret;
}

static int tc_set_dpi_video_mode(struct tc_data *tc,
				 const struct drm_display_mode *mode)
{
	u32 value = POCTRL_S2P;

	if (tc->mode.flags & DRM_MODE_FLAG_NHSYNC)
		value |= POCTRL_HS_POL;

	if (tc->mode.flags & DRM_MODE_FLAG_NVSYNC)
		value |= POCTRL_VS_POL;

	return regmap_write(tc->regmap, POCTRL, value);
}

static int tc_set_edp_video_mode(struct tc_data *tc,
				 const struct drm_display_mode *mode)
{
	int ret;
	int vid_sync_dly;
	int max_tu_symbol;

	int left_margin = mode->htotal - mode->hsync_end;
	int hsync_len = mode->hsync_end - mode->hsync_start;
	int upper_margin = mode->vtotal - mode->vsync_end;
	int vsync_len = mode->vsync_end - mode->vsync_start;
	u32 dp0_syncval;
	u32 bits_per_pixel = 24;
	u32 in_bw, out_bw;
	u32 dpipxlfmt;

	/*
	 * Recommended maximum number of symbols transferred in a transfer unit:
	 * DIV_ROUND_UP((input active video bandwidth in bytes) * tu_size,
	 *              (output active video bandwidth in bytes))
	 * Must be less than tu_size.
	 */

	in_bw = mode->clock * bits_per_pixel / 8;
	out_bw = tc->link.num_lanes * tc->link.rate;
	max_tu_symbol = DIV_ROUND_UP(in_bw * TU_SIZE_RECOMMENDED, out_bw);

	/* DP Main Stream Attributes */
	vid_sync_dly = hsync_len + left_margin + mode->hdisplay;
	ret = regmap_write(tc->regmap, DP0_VIDSYNCDELAY,
		 FIELD_PREP(THRESH_DLY, max_tu_symbol) |
		 FIELD_PREP(VID_SYNC_DLY, vid_sync_dly));

	ret = regmap_write(tc->regmap, DP0_TOTALVAL,
			   FIELD_PREP(H_TOTAL, mode->htotal) |
			   FIELD_PREP(V_TOTAL, mode->vtotal));
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, DP0_STARTVAL,
			   FIELD_PREP(H_START, left_margin + hsync_len) |
			   FIELD_PREP(V_START, upper_margin + vsync_len));
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, DP0_ACTIVEVAL,
			   FIELD_PREP(V_ACT, mode->vdisplay) |
			   FIELD_PREP(H_ACT, mode->hdisplay));
	if (ret)
		return ret;

	dp0_syncval = FIELD_PREP(VS_WIDTH, vsync_len) |
		      FIELD_PREP(HS_WIDTH, hsync_len);

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		dp0_syncval |= SYNCVAL_VS_POL_ACTIVE_LOW;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		dp0_syncval |= SYNCVAL_HS_POL_ACTIVE_LOW;

	ret = regmap_write(tc->regmap, DP0_SYNCVAL, dp0_syncval);
	if (ret)
		return ret;

	dpipxlfmt = DE_POL_ACTIVE_HIGH | SUB_CFG_TYPE_CONFIG1 | DPI_BPP_RGB888;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		dpipxlfmt |= VS_POL_ACTIVE_LOW;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		dpipxlfmt |= HS_POL_ACTIVE_LOW;

	ret = regmap_write(tc->regmap, DPIPXLFMT, dpipxlfmt);
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, DP0_MISC,
			   FIELD_PREP(MAX_TU_SYMBOL, max_tu_symbol) |
			   FIELD_PREP(TU_SIZE, TU_SIZE_RECOMMENDED) |
			   BPC_8);
	return ret;
}

static int tc_wait_link_training(struct tc_data *tc)
{
	u32 value;
	int ret;

	ret = tc_poll_timeout(tc, DP0_LTSTAT, LT_LOOPDONE,
			      LT_LOOPDONE, 500, 100000);
	if (ret) {
		dev_err(tc->dev, "Link training timeout waiting for LT_LOOPDONE!\n");
		return ret;
	}

	ret = regmap_read(tc->regmap, DP0_LTSTAT, &value);
	if (ret)
		return ret;

	return (value >> 8) & 0x7;
}

static int tc_main_link_enable(struct tc_data *tc)
{
	struct drm_dp_aux *aux = &tc->aux;
	struct device *dev = tc->dev;
	u32 dp_phy_ctrl;
	u32 value;
	int ret;
	u8 tmp[DP_LINK_STATUS_SIZE];

	dev_dbg(tc->dev, "link enable\n");

	ret = regmap_read(tc->regmap, DP0CTL, &value);
	if (ret)
		return ret;

	if (WARN_ON(value & DP_EN)) {
		ret = regmap_write(tc->regmap, DP0CTL, 0);
		if (ret)
			return ret;
	}

	ret = regmap_write(tc->regmap, DP0_SRCCTRL,
			   tc_srcctrl(tc) |
			   FIELD_PREP(DP0_SRCCTRL_PRE0, tc->pre_emphasis[0]) |
			   FIELD_PREP(DP0_SRCCTRL_PRE1, tc->pre_emphasis[1]));
	if (ret)
		return ret;
	/* SSCG and BW27 on DP1 must be set to the same as on DP0 */
	ret = regmap_write(tc->regmap, DP1_SRCCTRL,
		 (tc->link.spread ? DP0_SRCCTRL_SSCG : 0) |
		 ((tc->link.rate != 162000) ? DP0_SRCCTRL_BW27 : 0) |
		 FIELD_PREP(DP1_SRCCTRL_PRE, tc->pre_emphasis[1]));
	if (ret)
		return ret;

	ret = tc_set_syspllparam(tc);
	if (ret)
		return ret;

	/* Setup Main Link */
	dp_phy_ctrl = BGREN | PWR_SW_EN | PHY_A0_EN | PHY_M0_EN;
	if (tc->link.num_lanes == 2)
		dp_phy_ctrl |= PHY_2LANE;

	ret = regmap_write(tc->regmap, DP_PHY_CTRL, dp_phy_ctrl);
	if (ret)
		return ret;

	/* PLL setup */
	ret = tc_pllupdate(tc, DP0_PLLCTRL);
	if (ret)
		return ret;

	ret = tc_pllupdate(tc, DP1_PLLCTRL);
	if (ret)
		return ret;

	/* Reset/Enable Main Links */
	dp_phy_ctrl |= DP_PHY_RST | PHY_M1_RST | PHY_M0_RST;
	ret = regmap_write(tc->regmap, DP_PHY_CTRL, dp_phy_ctrl);
	usleep_range(100, 200);
	dp_phy_ctrl &= ~(DP_PHY_RST | PHY_M1_RST | PHY_M0_RST);
	ret = regmap_write(tc->regmap, DP_PHY_CTRL, dp_phy_ctrl);

	ret = tc_poll_timeout(tc, DP_PHY_CTRL, PHY_RDY, PHY_RDY, 500, 100000);
	if (ret) {
		dev_err(dev, "timeout waiting for phy become ready");
		return ret;
	}

	/* Set misc: 8 bits per color */
	ret = regmap_update_bits(tc->regmap, DP0_MISC, BPC_8, BPC_8);
	if (ret)
		return ret;

	/*
	 * ASSR mode
	 * on TC358767 side ASSR configured through strap pin
	 * seems there is no way to change this setting from SW
	 *
	 * check is tc configured for same mode
	 */
	if (tc->assr != tc->link.assr) {
		dev_dbg(dev, "Trying to set display to ASSR: %d\n",
			tc->assr);
		/* try to set ASSR on display side */
		tmp[0] = tc->assr;
		ret = drm_dp_dpcd_writeb(aux, DP_EDP_CONFIGURATION_SET, tmp[0]);
		if (ret < 0)
			goto err_dpcd_read;
		/* read back */
		ret = drm_dp_dpcd_readb(aux, DP_EDP_CONFIGURATION_SET, tmp);
		if (ret < 0)
			goto err_dpcd_read;

		if (tmp[0] != tc->assr) {
			dev_dbg(dev, "Failed to switch display ASSR to %d, falling back to unscrambled mode\n",
				tc->assr);
			/* trying with disabled scrambler */
			tc->link.scrambler_dis = true;
		}
	}

	/* Setup Link & DPRx Config for Training */
	tmp[0] = drm_dp_link_rate_to_bw_code(tc->link.rate);
	tmp[1] = tc->link.num_lanes;

	if (drm_dp_enhanced_frame_cap(tc->link.dpcd))
		tmp[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	ret = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, tmp, 2);
	if (ret < 0)
		goto err_dpcd_write;

	/* DOWNSPREAD_CTRL */
	tmp[0] = tc->link.spread ? DP_SPREAD_AMP_0_5 : 0x00;
	/* MAIN_LINK_CHANNEL_CODING_SET */
	tmp[1] =  DP_SET_ANSI_8B10B;
	ret = drm_dp_dpcd_write(aux, DP_DOWNSPREAD_CTRL, tmp, 2);
	if (ret < 0)
		goto err_dpcd_write;

	/* Reset voltage-swing & pre-emphasis */
	tmp[0] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 |
		 FIELD_PREP(DP_TRAIN_PRE_EMPHASIS_MASK, tc->pre_emphasis[0]);
	tmp[1] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 |
		 FIELD_PREP(DP_TRAIN_PRE_EMPHASIS_MASK, tc->pre_emphasis[1]);
	ret = drm_dp_dpcd_write(aux, DP_TRAINING_LANE0_SET, tmp, 2);
	if (ret < 0)
		goto err_dpcd_write;

	/* Clock-Recovery */

	/* Set DPCD 0x102 for Training Pattern 1 */
	ret = regmap_write(tc->regmap, DP0_SNKLTCTRL,
			   DP_LINK_SCRAMBLING_DISABLE |
			   DP_TRAINING_PATTERN_1);
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, DP0_LTLOOPCTRL,
			   (15 << 28) |	/* Defer Iteration Count */
			   (15 << 24) |	/* Loop Iteration Count */
			   (0xd << 0));	/* Loop Timer Delay */
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, DP0_SRCCTRL,
			   tc_srcctrl(tc) | DP0_SRCCTRL_SCRMBLDIS |
			   DP0_SRCCTRL_AUTOCORRECT |
			   DP0_SRCCTRL_TP1 |
			   FIELD_PREP(DP0_SRCCTRL_PRE0, tc->pre_emphasis[0]) |
			   FIELD_PREP(DP0_SRCCTRL_PRE1, tc->pre_emphasis[1]));
	if (ret)
		return ret;

	/* Enable DP0 to start Link Training */
	ret = regmap_write(tc->regmap, DP0CTL,
			   (drm_dp_enhanced_frame_cap(tc->link.dpcd) ?
				EF_EN : 0) | DP_EN);
	if (ret)
		return ret;

	/* wait */

	ret = tc_wait_link_training(tc);
	if (ret < 0)
		return ret;

	if (ret) {
		dev_err(tc->dev, "Link training phase 1 failed: %s\n",
			training_pattern1_errors[ret]);
		return -ENODEV;
	}

	/* Channel Equalization */

	/* Set DPCD 0x102 for Training Pattern 2 */
	ret = regmap_write(tc->regmap, DP0_SNKLTCTRL,
			   DP_LINK_SCRAMBLING_DISABLE |
			   DP_TRAINING_PATTERN_2);
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, DP0_SRCCTRL,
			   tc_srcctrl(tc) | DP0_SRCCTRL_SCRMBLDIS |
			   DP0_SRCCTRL_AUTOCORRECT |
			   DP0_SRCCTRL_TP2 |
			   FIELD_PREP(DP0_SRCCTRL_PRE0, tc->pre_emphasis[0]) |
			   FIELD_PREP(DP0_SRCCTRL_PRE1, tc->pre_emphasis[1]));
	if (ret)
		return ret;

	/* wait */
	ret = tc_wait_link_training(tc);
	if (ret < 0)
		return ret;

	if (ret) {
		dev_err(tc->dev, "Link training phase 2 failed: %s\n",
			training_pattern2_errors[ret]);
		return -ENODEV;
	}

	/*
	 * Toshiba's documentation suggests to first clear DPCD 0x102, then
	 * clear the training pattern bit in DP0_SRCCTRL. Testing shows
	 * that the link sometimes drops if those steps are done in that order,
	 * but if the steps are done in reverse order, the link stays up.
	 *
	 * So we do the steps differently than documented here.
	 */

	/* Clear Training Pattern, set AutoCorrect Mode = 1 */
	ret = regmap_write(tc->regmap, DP0_SRCCTRL, tc_srcctrl(tc) |
			   DP0_SRCCTRL_AUTOCORRECT |
			   FIELD_PREP(DP0_SRCCTRL_PRE0, tc->pre_emphasis[0]) |
			   FIELD_PREP(DP0_SRCCTRL_PRE1, tc->pre_emphasis[1]));
	if (ret)
		return ret;

	/* Clear DPCD 0x102 */
	/* Note: Can Not use DP0_SNKLTCTRL (0x06E4) short cut */
	tmp[0] = tc->link.scrambler_dis ? DP_LINK_SCRAMBLING_DISABLE : 0x00;
	ret = drm_dp_dpcd_writeb(aux, DP_TRAINING_PATTERN_SET, tmp[0]);
	if (ret < 0)
		goto err_dpcd_write;

	/* Check link status */
	ret = drm_dp_dpcd_read_link_status(aux, tmp);
	if (ret < 0)
		goto err_dpcd_read;

	ret = 0;

	value = tmp[0] & DP_CHANNEL_EQ_BITS;

	if (value != DP_CHANNEL_EQ_BITS) {
		dev_err(tc->dev, "Lane 0 failed: %x\n", value);
		ret = -ENODEV;
	}

	if (tc->link.num_lanes == 2) {
		value = (tmp[0] >> 4) & DP_CHANNEL_EQ_BITS;

		if (value != DP_CHANNEL_EQ_BITS) {
			dev_err(tc->dev, "Lane 1 failed: %x\n", value);
			ret = -ENODEV;
		}

		if (!(tmp[2] & DP_INTERLANE_ALIGN_DONE)) {
			dev_err(tc->dev, "Interlane align failed\n");
			ret = -ENODEV;
		}
	}

	if (ret) {
		dev_err(dev, "0x0202 LANE0_1_STATUS:            0x%02x\n", tmp[0]);
		dev_err(dev, "0x0203 LANE2_3_STATUS             0x%02x\n", tmp[1]);
		dev_err(dev, "0x0204 LANE_ALIGN_STATUS_UPDATED: 0x%02x\n", tmp[2]);
		dev_err(dev, "0x0205 SINK_STATUS:               0x%02x\n", tmp[3]);
		dev_err(dev, "0x0206 ADJUST_REQUEST_LANE0_1:    0x%02x\n", tmp[4]);
		dev_err(dev, "0x0207 ADJUST_REQUEST_LANE2_3:    0x%02x\n", tmp[5]);
		return ret;
	}

	return 0;
err_dpcd_read:
	dev_err(tc->dev, "Failed to read DPCD: %d\n", ret);
	return ret;
err_dpcd_write:
	dev_err(tc->dev, "Failed to write DPCD: %d\n", ret);
	return ret;
}

static int tc_main_link_disable(struct tc_data *tc)
{
	int ret;

	dev_dbg(tc->dev, "link disable\n");

	ret = regmap_write(tc->regmap, DP0_SRCCTRL, 0);
	if (ret)
		return ret;

	ret = regmap_write(tc->regmap, DP0CTL, 0);
	if (ret)
		return ret;

	return regmap_update_bits(tc->regmap, DP_PHY_CTRL,
				  PHY_M0_RST | PHY_M1_RST | PHY_M0_EN,
				  PHY_M0_RST | PHY_M1_RST);
}

static int tc_dsi_rx_enable(struct tc_data *tc)
{
	u32 value;
	int ret;

	regmap_write(tc->regmap, PPI_D0S_CLRSIPOCOUNT, 5);
	regmap_write(tc->regmap, PPI_D1S_CLRSIPOCOUNT, 5);
	regmap_write(tc->regmap, PPI_D2S_CLRSIPOCOUNT, 5);
	regmap_write(tc->regmap, PPI_D3S_CLRSIPOCOUNT, 5);
	regmap_write(tc->regmap, PPI_D0S_ATMR, 0);
	regmap_write(tc->regmap, PPI_D1S_ATMR, 0);
	regmap_write(tc->regmap, PPI_TX_RX_TA, TTA_GET | TTA_SURE);
	regmap_write(tc->regmap, PPI_LPTXTIMECNT, LPX_PERIOD);

	value = ((LANEENABLE_L0EN << tc->dsi->lanes) - LANEENABLE_L0EN) |
		LANEENABLE_CLEN;
	regmap_write(tc->regmap, PPI_LANEENABLE, value);
	regmap_write(tc->regmap, DSI_LANEENABLE, value);

	/* Set input interface */
	value = DP0_AUDSRC_NO_INPUT;
	if (tc_test_pattern)
		value |= DP0_VIDSRC_COLOR_BAR;
	else
		value |= DP0_VIDSRC_DSI_RX;
	ret = regmap_write(tc->regmap, SYSCTRL, value);
	if (ret)
		return ret;

	usleep_range(120, 150);

	regmap_write(tc->regmap, PPI_STARTPPI, PPI_START_FUNCTION);
	regmap_write(tc->regmap, DSI_STARTDSI, DSI_RX_START);

	return 0;
}

static int tc_dpi_rx_enable(struct tc_data *tc)
{
	u32 value;

	/* Set input interface */
	value = DP0_AUDSRC_NO_INPUT;
	if (tc_test_pattern)
		value |= DP0_VIDSRC_COLOR_BAR;
	else
		value |= DP0_VIDSRC_DPI_RX;
	return regmap_write(tc->regmap, SYSCTRL, value);
}

static int tc_dpi_stream_enable(struct tc_data *tc)
{
	int ret;

	dev_dbg(tc->dev, "enable video stream\n");

	/* Setup PLL */
	ret = tc_set_syspllparam(tc);
	if (ret)
		return ret;

	/*
	 * Initially PLLs are in bypass. Force PLL parameter update,
	 * disable PLL bypass, enable PLL
	 */
	ret = tc_pllupdate(tc, DP0_PLLCTRL);
	if (ret)
		return ret;

	ret = tc_pllupdate(tc, DP1_PLLCTRL);
	if (ret)
		return ret;

	/* Pixel PLL must always be enabled for DPI mode */
	ret = tc_pxl_pll_en(tc, clk_get_rate(tc->refclk),
			    1000 * tc->mode.clock);
	if (ret)
		return ret;

	ret = tc_set_common_video_mode(tc, &tc->mode);
	if (ret)
		return ret;

	ret = tc_set_dpi_video_mode(tc, &tc->mode);
	if (ret)
		return ret;

	return tc_dsi_rx_enable(tc);
}

static int tc_dpi_stream_disable(struct tc_data *tc)
{
	dev_dbg(tc->dev, "disable video stream\n");

	tc_pxl_pll_dis(tc);

	return 0;
}

static int tc_edp_stream_enable(struct tc_data *tc)
{
	int ret;
	u32 value;

	dev_dbg(tc->dev, "enable video stream\n");

	/*
	 * Pixel PLL must be enabled for DSI input mode and test pattern.
	 *
	 * Per TC9595XBG datasheet Revision 0.1 2018-12-27 Figure 4.18
	 * "Clock Mode Selection and Clock Sources", either Pixel PLL
	 * or DPI_PCLK supplies StrmClk. DPI_PCLK is only available in
	 * case valid Pixel Clock are supplied to the chip DPI input.
	 * In case built-in test pattern is desired OR DSI input mode
	 * is used, DPI_PCLK is not available and thus Pixel PLL must
	 * be used instead.
	 */
	if (tc->input_connector_dsi || tc_test_pattern) {
		ret = tc_pxl_pll_en(tc, clk_get_rate(tc->refclk),
				    1000 * tc->mode.clock);
		if (ret)
			return ret;
	}

	ret = tc_set_common_video_mode(tc, &tc->mode);
	if (ret)
		return ret;

	ret = tc_set_edp_video_mode(tc, &tc->mode);
	if (ret)
		return ret;

	/* Set M/N */
	ret = tc_stream_clock_calc(tc);
	if (ret)
		return ret;

	value = VID_MN_GEN | DP_EN;
	if (drm_dp_enhanced_frame_cap(tc->link.dpcd))
		value |= EF_EN;
	ret = regmap_write(tc->regmap, DP0CTL, value);
	if (ret)
		return ret;
	/*
	 * VID_EN assertion should be delayed by at least N * LSCLK
	 * cycles from the time VID_MN_GEN is enabled in order to
	 * generate stable values for VID_M. LSCLK is 270 MHz or
	 * 162 MHz, VID_N is set to 32768 in  tc_stream_clock_calc(),
	 * so a delay of at least 203 us should suffice.
	 */
	usleep_range(500, 1000);
	value |= VID_EN;
	ret = regmap_write(tc->regmap, DP0CTL, value);
	if (ret)
		return ret;

	/* Set input interface */
	if (tc->input_connector_dsi)
		return tc_dsi_rx_enable(tc);
	else
		return tc_dpi_rx_enable(tc);
}

static int tc_edp_stream_disable(struct tc_data *tc)
{
	int ret;

	dev_dbg(tc->dev, "disable video stream\n");

	ret = regmap_update_bits(tc->regmap, DP0CTL, VID_EN, 0);
	if (ret)
		return ret;

	tc_pxl_pll_dis(tc);

	return 0;
}

static void
tc_dpi_bridge_atomic_enable(struct drm_bridge *bridge,
			    struct drm_bridge_state *old_bridge_state)

{
	struct tc_data *tc = bridge_to_tc(bridge);
	int ret;

	ret = tc_dpi_stream_enable(tc);
	if (ret < 0) {
		dev_err(tc->dev, "main link stream start error: %d\n", ret);
		tc_main_link_disable(tc);
		return;
	}
}

static void
tc_dpi_bridge_atomic_disable(struct drm_bridge *bridge,
			     struct drm_bridge_state *old_bridge_state)
{
	struct tc_data *tc = bridge_to_tc(bridge);
	int ret;

	ret = tc_dpi_stream_disable(tc);
	if (ret < 0)
		dev_err(tc->dev, "main link stream stop error: %d\n", ret);
}

static void
tc_edp_bridge_atomic_enable(struct drm_bridge *bridge,
			    struct drm_bridge_state *old_bridge_state)
{
	struct tc_data *tc = bridge_to_tc(bridge);
	int ret;

	ret = tc_get_display_props(tc);
	if (ret < 0) {
		dev_err(tc->dev, "failed to read display props: %d\n", ret);
		return;
	}

	ret = tc_main_link_enable(tc);
	if (ret < 0) {
		dev_err(tc->dev, "main link enable error: %d\n", ret);
		return;
	}

	ret = tc_edp_stream_enable(tc);
	if (ret < 0) {
		dev_err(tc->dev, "main link stream start error: %d\n", ret);
		tc_main_link_disable(tc);
		return;
	}
}

static void
tc_edp_bridge_atomic_disable(struct drm_bridge *bridge,
			     struct drm_bridge_state *old_bridge_state)
{
	struct tc_data *tc = bridge_to_tc(bridge);
	int ret;

	ret = tc_edp_stream_disable(tc);
	if (ret < 0)
		dev_err(tc->dev, "main link stream stop error: %d\n", ret);

	ret = tc_main_link_disable(tc);
	if (ret < 0)
		dev_err(tc->dev, "main link disable error: %d\n", ret);
}

static int tc_dpi_atomic_check(struct drm_bridge *bridge,
			       struct drm_bridge_state *bridge_state,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)
{
	struct tc_data *tc = bridge_to_tc(bridge);
	int adjusted_clock = 0;
	int ret;

	ret = tc_pxl_pll_calc(tc, clk_get_rate(tc->refclk),
			      crtc_state->mode.clock * 1000,
			      &adjusted_clock, NULL);
	if (ret)
		return ret;

	crtc_state->adjusted_mode.clock = adjusted_clock / 1000;

	/* DSI->DPI interface clock limitation: upto 100 MHz */
	if (crtc_state->adjusted_mode.clock > 100000)
		return -EINVAL;

	return 0;
}

static int tc_edp_atomic_check(struct drm_bridge *bridge,
			       struct drm_bridge_state *bridge_state,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)
{
	struct tc_data *tc = bridge_to_tc(bridge);
	int adjusted_clock = 0;
	int ret;

	ret = tc_pxl_pll_calc(tc, clk_get_rate(tc->refclk),
			      crtc_state->mode.clock * 1000,
			      &adjusted_clock, NULL);
	if (ret)
		return ret;

	crtc_state->adjusted_mode.clock = adjusted_clock / 1000;

	/* DPI->(e)DP interface clock limitation: upto 154 MHz */
	if (crtc_state->adjusted_mode.clock > 154000)
		return -EINVAL;

	return 0;
}

static enum drm_mode_status
tc_dpi_mode_valid(struct drm_bridge *bridge,
		  const struct drm_display_info *info,
		  const struct drm_display_mode *mode)
{
	/* DPI interface clock limitation: upto 100 MHz */
	if (mode->clock > 100000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static enum drm_mode_status
tc_edp_mode_valid(struct drm_bridge *bridge,
		  const struct drm_display_info *info,
		  const struct drm_display_mode *mode)
{
	struct tc_data *tc = bridge_to_tc(bridge);
	u32 req, avail;
	u32 bits_per_pixel = 24;

	/* DPI->(e)DP interface clock limitation: up to 154 MHz */
	if (mode->clock > 154000)
		return MODE_CLOCK_HIGH;

	req = mode->clock * bits_per_pixel / 8;
	avail = tc->link.num_lanes * tc->link.rate;

	if (req > avail)
		return MODE_BAD;

	return MODE_OK;
}

static void tc_bridge_mode_set(struct drm_bridge *bridge,
			       const struct drm_display_mode *mode,
			       const struct drm_display_mode *adj)
{
	struct tc_data *tc = bridge_to_tc(bridge);

	drm_mode_copy(&tc->mode, adj);
}

static const struct drm_edid *tc_edid_read(struct drm_bridge *bridge,
					   struct drm_connector *connector)
{
	struct tc_data *tc = bridge_to_tc(bridge);
	int ret;

	ret = tc_get_display_props(tc);
	if (ret < 0) {
		dev_err(tc->dev, "failed to read display props: %d\n", ret);
		return 0;
	}

	return drm_edid_read_ddc(connector, &tc->aux.ddc);
}

static int tc_connector_get_modes(struct drm_connector *connector)
{
	struct tc_data *tc = connector_to_tc(connector);
	int num_modes;
	const struct drm_edid *drm_edid;
	int ret;

	ret = tc_get_display_props(tc);
	if (ret < 0) {
		dev_err(tc->dev, "failed to read display props: %d\n", ret);
		return 0;
	}

	if (tc->panel_bridge) {
		num_modes = drm_bridge_get_modes(tc->panel_bridge, connector);
		if (num_modes > 0)
			return num_modes;
	}

	drm_edid = tc_edid_read(&tc->bridge, connector);
	drm_edid_connector_update(connector, drm_edid);
	num_modes = drm_edid_connector_add_modes(connector);
	drm_edid_free(drm_edid);

	return num_modes;
}

static const struct drm_connector_helper_funcs tc_connector_helper_funcs = {
	.get_modes = tc_connector_get_modes,
};

static enum drm_connector_status tc_bridge_detect(struct drm_bridge *bridge)
{
	struct tc_data *tc = bridge_to_tc(bridge);
	bool conn;
	u32 val;
	int ret;

	ret = regmap_read(tc->regmap, GPIOI, &val);
	if (ret)
		return connector_status_unknown;

	conn = val & BIT(tc->hpd_pin);

	if (conn)
		return connector_status_connected;
	else
		return connector_status_disconnected;
}

static enum drm_connector_status
tc_connector_detect(struct drm_connector *connector, bool force)
{
	struct tc_data *tc = connector_to_tc(connector);

	if (tc->hpd_pin >= 0)
		return tc_bridge_detect(&tc->bridge);

	if (tc->panel_bridge)
		return connector_status_connected;
	else
		return connector_status_unknown;
}

static const struct drm_connector_funcs tc_connector_funcs = {
	.detect = tc_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int tc_dpi_bridge_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct tc_data *tc = bridge_to_tc(bridge);

	if (!tc->panel_bridge)
		return 0;

	return drm_bridge_attach(tc->bridge.encoder, tc->panel_bridge,
				 &tc->bridge, flags);
}

static int tc_edp_bridge_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	struct tc_data *tc = bridge_to_tc(bridge);
	struct drm_device *drm = bridge->dev;
	int ret;

	if (tc->panel_bridge) {
		/* If a connector is required then this driver shall create it */
		ret = drm_bridge_attach(tc->bridge.encoder, tc->panel_bridge,
					&tc->bridge, flags | DRM_BRIDGE_ATTACH_NO_CONNECTOR);
		if (ret)
			return ret;
	}

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	tc->aux.drm_dev = drm;
	ret = drm_dp_aux_register(&tc->aux);
	if (ret < 0)
		return ret;

	/* Create DP/eDP connector */
	drm_connector_helper_add(&tc->connector, &tc_connector_helper_funcs);
	ret = drm_connector_init(drm, &tc->connector, &tc_connector_funcs, tc->bridge.type);
	if (ret)
		goto aux_unregister;

	/* Don't poll if don't have HPD connected */
	if (tc->hpd_pin >= 0) {
		if (tc->have_irq)
			tc->connector.polled = DRM_CONNECTOR_POLL_HPD;
		else
			tc->connector.polled = DRM_CONNECTOR_POLL_CONNECT |
					       DRM_CONNECTOR_POLL_DISCONNECT;
	}

	drm_display_info_set_bus_formats(&tc->connector.display_info,
					 &bus_format, 1);
	tc->connector.display_info.bus_flags =
		DRM_BUS_FLAG_DE_HIGH |
		DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE |
		DRM_BUS_FLAG_SYNC_DRIVE_NEGEDGE;
	drm_connector_attach_encoder(&tc->connector, tc->bridge.encoder);

	return 0;
aux_unregister:
	drm_dp_aux_unregister(&tc->aux);
	return ret;
}

static void tc_edp_bridge_detach(struct drm_bridge *bridge)
{
	drm_dp_aux_unregister(&bridge_to_tc(bridge)->aux);
}

#define MAX_INPUT_SEL_FORMATS	1
#define MAX_OUTPUT_SEL_FORMATS	1

static u32 *
tc_dpi_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
				 struct drm_bridge_state *bridge_state,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state,
				 u32 output_fmt,
				 unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kcalloc(MAX_INPUT_SEL_FORMATS, sizeof(*input_fmts),
			     GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	/* This is the DSI-end bus format */
	input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;
	*num_input_fmts = 1;

	return input_fmts;
}

static u32 *
tc_edp_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
				  struct drm_bridge_state *bridge_state,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state,
				  unsigned int *num_output_fmts)
{
	u32 *output_fmts;

	*num_output_fmts = 0;

	output_fmts = kcalloc(MAX_OUTPUT_SEL_FORMATS, sizeof(*output_fmts),
			      GFP_KERNEL);
	if (!output_fmts)
		return NULL;

	output_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;
	*num_output_fmts = 1;

	return output_fmts;
}

static const struct drm_bridge_funcs tc_dpi_bridge_funcs = {
	.attach = tc_dpi_bridge_attach,
	.mode_valid = tc_dpi_mode_valid,
	.mode_set = tc_bridge_mode_set,
	.atomic_check = tc_dpi_atomic_check,
	.atomic_enable = tc_dpi_bridge_atomic_enable,
	.atomic_disable = tc_dpi_bridge_atomic_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_get_input_bus_fmts = tc_dpi_atomic_get_input_bus_fmts,
};

static const struct drm_bridge_funcs tc_edp_bridge_funcs = {
	.attach = tc_edp_bridge_attach,
	.detach = tc_edp_bridge_detach,
	.mode_valid = tc_edp_mode_valid,
	.mode_set = tc_bridge_mode_set,
	.atomic_check = tc_edp_atomic_check,
	.atomic_enable = tc_edp_bridge_atomic_enable,
	.atomic_disable = tc_edp_bridge_atomic_disable,
	.detect = tc_bridge_detect,
	.edid_read = tc_edid_read,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_get_input_bus_fmts = drm_atomic_helper_bridge_propagate_bus_fmt,
	.atomic_get_output_bus_fmts = tc_edp_atomic_get_output_bus_fmts,
};

static bool tc_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	/* DSI D-PHY Layer */
	case 0x004:
	case 0x020:
	case 0x024:
	case 0x028:
	case 0x02c:
	case 0x030:
	case 0x038:
	case 0x040:
	case 0x044:
	case 0x048:
	case 0x04c:
	case 0x050:
	case 0x054:
	/* DSI PPI Layer */
	case PPI_STARTPPI:
	case 0x108:
	case 0x110:
	case PPI_LPTXTIMECNT:
	case PPI_LANEENABLE:
	case PPI_TX_RX_TA:
	case 0x140:
	case PPI_D0S_ATMR:
	case PPI_D1S_ATMR:
	case 0x14c:
	case 0x150:
	case PPI_D0S_CLRSIPOCOUNT:
	case PPI_D1S_CLRSIPOCOUNT:
	case PPI_D2S_CLRSIPOCOUNT:
	case PPI_D3S_CLRSIPOCOUNT:
	case 0x180:
	case 0x184:
	case 0x188:
	case 0x18c:
	case 0x190:
	case 0x1a0:
	case 0x1a4:
	case 0x1a8:
	case 0x1ac:
	case 0x1b0:
	case 0x1c0:
	case 0x1c4:
	case 0x1c8:
	case 0x1cc:
	case 0x1d0:
	case 0x1e0:
	case 0x1e4:
	case 0x1f0:
	case 0x1f4:
	/* DSI Protocol Layer */
	case DSI_STARTDSI:
	case DSI_BUSYDSI:
	case DSI_LANEENABLE:
	case DSI_LANESTATUS0:
	case DSI_LANESTATUS1:
	case DSI_INTSTATUS:
	case 0x224:
	case 0x228:
	case 0x230:
	/* DSI General */
	case DSIERRCNT:
	/* DSI Application Layer */
	case 0x400:
	case 0x404:
	/* DPI */
	case DPIPXLFMT:
	/* Parallel Output */
	case POCTRL:
	/* Video Path0 Configuration */
	case VPCTRL0:
	case HTIM01:
	case HTIM02:
	case VTIM01:
	case VTIM02:
	case VFUEN0:
	/* System */
	case TC_IDREG:
	case 0x504:
	case SYSSTAT:
	case SYSRSTENB:
	case SYSCTRL:
	/* I2C */
	case 0x520:
	/* GPIO */
	case GPIOM:
	case GPIOC:
	case GPIOO:
	case GPIOI:
	/* Interrupt */
	case INTCTL_G:
	case INTSTS_G:
	case 0x570:
	case 0x574:
	case INT_GP0_LCNT:
	case INT_GP1_LCNT:
	/* DisplayPort Control */
	case DP0CTL:
	/* DisplayPort Clock */
	case DP0_VIDMNGEN0:
	case DP0_VIDMNGEN1:
	case DP0_VMNGENSTATUS:
	case 0x628:
	case 0x62c:
	case 0x630:
	/* DisplayPort Main Channel */
	case DP0_SECSAMPLE:
	case DP0_VIDSYNCDELAY:
	case DP0_TOTALVAL:
	case DP0_STARTVAL:
	case DP0_ACTIVEVAL:
	case DP0_SYNCVAL:
	case DP0_MISC:
	/* DisplayPort Aux Channel */
	case DP0_AUXCFG0:
	case DP0_AUXCFG1:
	case DP0_AUXADDR:
	case 0x66c:
	case 0x670:
	case 0x674:
	case 0x678:
	case 0x67c:
	case 0x680:
	case 0x684:
	case 0x688:
	case DP0_AUXSTATUS:
	case DP0_AUXI2CADR:
	/* DisplayPort Link Training */
	case DP0_SRCCTRL:
	case DP0_LTSTAT:
	case DP0_SNKLTCHGREQ:
	case DP0_LTLOOPCTRL:
	case DP0_SNKLTCTRL:
	case 0x6e8:
	case 0x6ec:
	case 0x6f0:
	case 0x6f4:
	/* DisplayPort Audio */
	case 0x700:
	case 0x704:
	case 0x708:
	case 0x70c:
	case 0x710:
	case 0x714:
	case 0x718:
	case 0x71c:
	case 0x720:
	/* DisplayPort Source Control */
	case DP1_SRCCTRL:
	/* DisplayPort PHY */
	case DP_PHY_CTRL:
	case 0x810:
	case 0x814:
	case 0x820:
	case 0x840:
	/* I2S */
	case 0x880:
	case 0x888:
	case 0x88c:
	case 0x890:
	case 0x894:
	case 0x898:
	case 0x89c:
	case 0x8a0:
	case 0x8a4:
	case 0x8a8:
	case 0x8ac:
	case 0x8b0:
	case 0x8b4:
	/* PLL */
	case DP0_PLLCTRL:
	case DP1_PLLCTRL:
	case PXL_PLLCTRL:
	case PXL_PLLPARAM:
	case SYS_PLLPARAM:
	/* HDCP */
	case 0x980:
	case 0x984:
	case 0x988:
	case 0x98c:
	case 0x990:
	case 0x994:
	case 0x998:
	case 0x99c:
	case 0x9a0:
	case 0x9a4:
	case 0x9a8:
	case 0x9ac:
	/* Debug */
	case TSTCTL:
	case PLL_DBG:
		return true;
	}
	return false;
}

static const struct regmap_range tc_volatile_ranges[] = {
	regmap_reg_range(PPI_BUSYPPI, PPI_BUSYPPI),
	regmap_reg_range(DSI_BUSYDSI, DSI_BUSYDSI),
	regmap_reg_range(DSI_LANESTATUS0, DSI_INTSTATUS),
	regmap_reg_range(DSIERRCNT, DSIERRCNT),
	regmap_reg_range(VFUEN0, VFUEN0),
	regmap_reg_range(SYSSTAT, SYSSTAT),
	regmap_reg_range(GPIOI, GPIOI),
	regmap_reg_range(INTSTS_G, INTSTS_G),
	regmap_reg_range(DP0_VMNGENSTATUS, DP0_VMNGENSTATUS),
	regmap_reg_range(DP0_AMNGENSTATUS, DP0_AMNGENSTATUS),
	regmap_reg_range(DP0_AUXWDATA(0), DP0_AUXSTATUS),
	regmap_reg_range(DP0_LTSTAT, DP0_SNKLTCHGREQ),
	regmap_reg_range(DP_PHY_CTRL, DP_PHY_CTRL),
	regmap_reg_range(DP0_PLLCTRL, PXL_PLLCTRL),
};

static const struct regmap_access_table tc_volatile_table = {
	.yes_ranges = tc_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(tc_volatile_ranges),
};

static const struct regmap_range tc_precious_ranges[] = {
	regmap_reg_range(SYSSTAT, SYSSTAT),
};

static const struct regmap_access_table tc_precious_table = {
	.yes_ranges = tc_precious_ranges,
	.n_yes_ranges = ARRAY_SIZE(tc_precious_ranges),
};

static bool tc_writeable_reg(struct device *dev, unsigned int reg)
{
	/* RO reg */
	switch (reg) {
	case PPI_BUSYPPI:
	case DSI_BUSYDSI:
	case DSI_LANESTATUS0:
	case DSI_LANESTATUS1:
	case DSI_INTSTATUS:
	case TC_IDREG:
	case SYSBOOT:
	case SYSSTAT:
	case GPIOI:
	case DP0_LTSTAT:
	case DP0_SNKLTCHGREQ:
		return false;
	}
	/* WO reg */
	switch (reg) {
	case DSI_STARTDSI:
	case DSI_INTCLR:
		return true;
	}
	return tc_readable_reg(dev, reg);
}

static const struct regmap_config tc_regmap_config = {
	.name = "tc358767",
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = PLL_DBG,
	.cache_type = REGCACHE_MAPLE,
	.readable_reg = tc_readable_reg,
	.writeable_reg = tc_writeable_reg,
	.volatile_table = &tc_volatile_table,
	.precious_table = &tc_precious_table,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static irqreturn_t tc_irq_handler(int irq, void *arg)
{
	struct tc_data *tc = arg;
	u32 val;
	int r;

	r = regmap_read(tc->regmap, INTSTS_G, &val);
	if (r)
		return IRQ_NONE;

	if (!val)
		return IRQ_NONE;

	if (val & INT_SYSERR) {
		u32 stat = 0;

		regmap_read(tc->regmap, SYSSTAT, &stat);

		dev_err(tc->dev, "syserr %x\n", stat);
	}

	if (tc->hpd_pin >= 0 && tc->bridge.dev && tc->aux.drm_dev) {
		/*
		 * H is triggered when the GPIO goes high.
		 *
		 * LC is triggered when the GPIO goes low and stays low for
		 * the duration of LCNT
		 */
		bool h = val & INT_GPIO_H(tc->hpd_pin);
		bool lc = val & INT_GPIO_LC(tc->hpd_pin);

		if (h || lc) {
			dev_dbg(tc->dev, "GPIO%d: %s %s\n", tc->hpd_pin,
				h ? "H" : "", lc ? "LC" : "");
			drm_kms_helper_hotplug_event(tc->bridge.dev);
		}
	}

	regmap_write(tc->regmap, INTSTS_G, val);

	return IRQ_HANDLED;
}

static int tc_mipi_dsi_host_attach(struct tc_data *tc)
{
	struct device *dev = tc->dev;
	struct device_node *host_node;
	struct device_node *endpoint;
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	const struct mipi_dsi_device_info info = {
		.type = "tc358767",
		.channel = 0,
		.node = NULL,
	};
	int dsi_lanes, ret;

	endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 0, -1);
	dsi_lanes = drm_of_get_data_lanes_count(endpoint, 1, 4);
	host_node = of_graph_get_remote_port_parent(endpoint);
	host = of_find_mipi_dsi_host_by_node(host_node);
	of_node_put(host_node);
	of_node_put(endpoint);

	if (!host)
		return -EPROBE_DEFER;

	if (dsi_lanes < 0)
		return dsi_lanes;

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi))
		return dev_err_probe(dev, PTR_ERR(dsi),
				     "failed to create dsi device\n");

	tc->dsi = dsi;
	dsi->lanes = dsi_lanes;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tc_probe_dpi_bridge_endpoint(struct tc_data *tc)
{
	struct device *dev = tc->dev;
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	int ret;

	/* port@1 is the DPI input/output port */
	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, 0, &panel, &bridge);
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret,
				     "Could not find DPI panel or bridge\n");

	if (panel) {
		bridge = devm_drm_panel_bridge_add(dev, panel);
		if (IS_ERR(bridge))
			return PTR_ERR(bridge);
	}

	if (bridge) {
		tc->panel_bridge = bridge;
		tc->bridge.type = DRM_MODE_CONNECTOR_DPI;
		tc->bridge.funcs = &tc_dpi_bridge_funcs;

		return 0;
	}

	return ret;
}

static int tc_probe_edp_bridge_endpoint(struct tc_data *tc)
{
	struct device *dev = tc->dev;
	struct drm_panel *panel;
	int ret;

	/* port@2 is the output port */
	ret = drm_of_find_panel_or_bridge(dev->of_node, 2, 0, &panel, NULL);
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret,
				     "Could not find DSI panel or bridge\n");

	if (panel) {
		struct drm_bridge *panel_bridge;

		panel_bridge = devm_drm_panel_bridge_add(dev, panel);
		if (IS_ERR(panel_bridge))
			return PTR_ERR(panel_bridge);

		tc->panel_bridge = panel_bridge;
		tc->bridge.type = DRM_MODE_CONNECTOR_eDP;
	} else {
		tc->bridge.type = DRM_MODE_CONNECTOR_DisplayPort;
	}

	tc->bridge.funcs = &tc_edp_bridge_funcs;
	if (tc->hpd_pin >= 0)
		tc->bridge.ops |= DRM_BRIDGE_OP_DETECT;
	tc->bridge.ops |= DRM_BRIDGE_OP_EDID;

	return 0;
}

static int tc_probe_bridge_endpoint(struct tc_data *tc)
{
	struct device *dev = tc->dev;
	struct of_endpoint endpoint;
	struct device_node *node = NULL;
	const u8 mode_dpi_to_edp = BIT(1) | BIT(2);
	const u8 mode_dpi_to_dp = BIT(1);
	const u8 mode_dsi_to_edp = BIT(0) | BIT(2);
	const u8 mode_dsi_to_dp = BIT(0);
	const u8 mode_dsi_to_dpi = BIT(0) | BIT(1);
	u8 mode = 0;

	/*
	 * Determine bridge configuration.
	 *
	 * Port allocation:
	 * port@0 - DSI input
	 * port@1 - DPI input/output
	 * port@2 - eDP output
	 *
	 * Possible connections:
	 * DPI -> port@1 -> port@2 -> eDP :: [port@0 is not connected]
	 * DSI -> port@0 -> port@2 -> eDP :: [port@1 is not connected]
	 * DSI -> port@0 -> port@1 -> DPI :: [port@2 is not connected]
	 */

	for_each_endpoint_of_node(dev->of_node, node) {
		of_graph_parse_endpoint(node, &endpoint);
		if (endpoint.port > 2) {
			of_node_put(node);
			return -EINVAL;
		}
		mode |= BIT(endpoint.port);

		if (endpoint.port == 2) {
			of_property_read_u8_array(node, "toshiba,pre-emphasis",
						  tc->pre_emphasis,
						  ARRAY_SIZE(tc->pre_emphasis));

			if (tc->pre_emphasis[0] < 0 || tc->pre_emphasis[0] > 2 ||
			    tc->pre_emphasis[1] < 0 || tc->pre_emphasis[1] > 2) {
				dev_err(dev, "Incorrect Pre-Emphasis setting, use either 0=0dB 1=3.5dB 2=6dB\n");
				of_node_put(node);
				return -EINVAL;
			}
		}
	}

	if (mode == mode_dpi_to_edp || mode == mode_dpi_to_dp) {
		tc->input_connector_dsi = false;
		return tc_probe_edp_bridge_endpoint(tc);
	} else if (mode == mode_dsi_to_dpi) {
		tc->input_connector_dsi = true;
		return tc_probe_dpi_bridge_endpoint(tc);
	} else if (mode == mode_dsi_to_edp || mode == mode_dsi_to_dp) {
		tc->input_connector_dsi = true;
		return tc_probe_edp_bridge_endpoint(tc);
	}

	dev_warn(dev, "Invalid mode (0x%x) is not supported!\n", mode);

	return -EINVAL;
}

static int tc_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tc_data *tc;
	int ret;

	tc = devm_kzalloc(dev, sizeof(*tc), GFP_KERNEL);
	if (!tc)
		return -ENOMEM;

	tc->dev = dev;

	ret = tc_probe_bridge_endpoint(tc);
	if (ret)
		return ret;

	tc->refclk = devm_clk_get_enabled(dev, "ref");
	if (IS_ERR(tc->refclk))
		return dev_err_probe(dev, PTR_ERR(tc->refclk),
				     "Failed to get and enable the ref clk\n");

	/* tRSTW = 100 cycles , at 13 MHz that is ~7.69 us */
	usleep_range(10, 15);

	/* Shut down GPIO is optional */
	tc->sd_gpio = devm_gpiod_get_optional(dev, "shutdown", GPIOD_OUT_HIGH);
	if (IS_ERR(tc->sd_gpio))
		return PTR_ERR(tc->sd_gpio);

	if (tc->sd_gpio) {
		gpiod_set_value_cansleep(tc->sd_gpio, 0);
		usleep_range(5000, 10000);
	}

	/* Reset GPIO is optional */
	tc->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(tc->reset_gpio))
		return PTR_ERR(tc->reset_gpio);

	if (tc->reset_gpio) {
		gpiod_set_value_cansleep(tc->reset_gpio, 1);
		usleep_range(5000, 10000);
	}

	tc->regmap = devm_regmap_init_i2c(client, &tc_regmap_config);
	if (IS_ERR(tc->regmap)) {
		ret = PTR_ERR(tc->regmap);
		dev_err(dev, "Failed to initialize regmap: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(dev->of_node, "toshiba,hpd-pin",
				   &tc->hpd_pin);
	if (ret) {
		tc->hpd_pin = -ENODEV;
	} else {
		if (tc->hpd_pin < 0 || tc->hpd_pin > 1) {
			dev_err(dev, "failed to parse HPD number\n");
			return -EINVAL;
		}
	}

	if (client->irq > 0) {
		/* enable SysErr */
		regmap_write(tc->regmap, INTCTL_G, INT_SYSERR);

		ret = devm_request_threaded_irq(dev, client->irq,
						NULL, tc_irq_handler,
						IRQF_ONESHOT,
						"tc358767-irq", tc);
		if (ret) {
			dev_err(dev, "failed to register dp interrupt\n");
			return ret;
		}

		tc->have_irq = true;
	}

	ret = regmap_read(tc->regmap, TC_IDREG, &tc->rev);
	if (ret) {
		dev_err(tc->dev, "can not read device ID: %d\n", ret);
		return ret;
	}

	if ((tc->rev != 0x6601) && (tc->rev != 0x6603)) {
		dev_err(tc->dev, "invalid device ID: 0x%08x\n", tc->rev);
		return -EINVAL;
	}

	tc->assr = (tc->rev == 0x6601); /* Enable ASSR for eDP panels */

	if (!tc->reset_gpio) {
		/*
		 * If the reset pin isn't present, do a software reset. It isn't
		 * as thorough as the hardware reset, as we can't reset the I2C
		 * communication block for obvious reasons, but it's getting the
		 * chip into a defined state.
		 */
		regmap_update_bits(tc->regmap, SYSRSTENB,
				ENBLCD0 | ENBBM | ENBDSIRX | ENBREG | ENBHDCP,
				0);
		regmap_update_bits(tc->regmap, SYSRSTENB,
				ENBLCD0 | ENBBM | ENBDSIRX | ENBREG | ENBHDCP,
				ENBLCD0 | ENBBM | ENBDSIRX | ENBREG | ENBHDCP);
		usleep_range(5000, 10000);
	}

	if (tc->hpd_pin >= 0) {
		u32 lcnt_reg = tc->hpd_pin == 0 ? INT_GP0_LCNT : INT_GP1_LCNT;
		u32 h_lc = INT_GPIO_H(tc->hpd_pin) | INT_GPIO_LC(tc->hpd_pin);

		/* Set LCNT to 2ms */
		regmap_write(tc->regmap, lcnt_reg,
			     clk_get_rate(tc->refclk) * 2 / 1000);
		/* We need the "alternate" mode for HPD */
		regmap_write(tc->regmap, GPIOM, BIT(tc->hpd_pin));

		if (tc->have_irq) {
			/* enable H & LC */
			regmap_update_bits(tc->regmap, INTCTL_G, h_lc, h_lc);
		}
	}

	if (tc->bridge.type != DRM_MODE_CONNECTOR_DPI) { /* (e)DP output */
		ret = tc_aux_link_setup(tc);
		if (ret)
			return ret;
	}

	tc->bridge.of_node = dev->of_node;
	drm_bridge_add(&tc->bridge);

	i2c_set_clientdata(client, tc);

	if (tc->input_connector_dsi) {			/* DSI input */
		ret = tc_mipi_dsi_host_attach(tc);
		if (ret) {
			drm_bridge_remove(&tc->bridge);
			return dev_err_probe(dev, ret, "Failed to attach DSI host\n");
		}
	}

	return 0;
}

static void tc_remove(struct i2c_client *client)
{
	struct tc_data *tc = i2c_get_clientdata(client);

	drm_bridge_remove(&tc->bridge);
}

static const struct i2c_device_id tc358767_i2c_ids[] = {
	{ "tc358767" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tc358767_i2c_ids);

static const struct of_device_id tc358767_of_ids[] = {
	{ .compatible = "toshiba,tc358767", },
	{ }
};
MODULE_DEVICE_TABLE(of, tc358767_of_ids);

static struct i2c_driver tc358767_driver = {
	.driver = {
		.name = "tc358767",
		.of_match_table = tc358767_of_ids,
	},
	.id_table = tc358767_i2c_ids,
	.probe = tc_probe,
	.remove	= tc_remove,
};
module_i2c_driver(tc358767_driver);

MODULE_AUTHOR("Andrey Gusakov <andrey.gusakov@cogentembedded.com>");
MODULE_DESCRIPTION("tc358767 eDP encoder driver");
MODULE_LICENSE("GPL");
