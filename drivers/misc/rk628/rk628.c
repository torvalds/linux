// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/backlight.h>
#include <linux/pm_runtime.h>
#include <video/videomode.h>
#include <linux/debugfs.h>

#include "rk628.h"
#include "rk628_cru.h"
#include "rk628_combrxphy.h"
#include "rk628_post_process.h"
#include "rk628_hdmirx.h"
#include "rk628_combtxphy.h"
#include "rk628_dsi.h"
#include "rk628_rgb.h"
#include "rk628_lvds.h"
#include "rk628_gvi.h"
#include "rk628_csi.h"
#include "rk628_hdmitx.h"

static const struct regmap_range rk628_cru_readable_ranges[] = {
	regmap_reg_range(CRU_CPLL_CON0, CRU_CPLL_CON4),
	regmap_reg_range(CRU_GPLL_CON0, CRU_GPLL_CON4),
	regmap_reg_range(CRU_MODE_CON00, CRU_MODE_CON00),
	regmap_reg_range(CRU_CLKSEL_CON00, CRU_CLKSEL_CON21),
	regmap_reg_range(CRU_GATE_CON00, CRU_GATE_CON05),
	regmap_reg_range(CRU_SOFTRST_CON00, CRU_SOFTRST_CON04),
};

static const struct regmap_access_table rk628_cru_readable_table = {
	.yes_ranges     = rk628_cru_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_cru_readable_ranges),
};

static const struct regmap_range rk628_combrxphy_readable_ranges[] = {
	regmap_reg_range(COMBRX_REG(0x6600), COMBRX_REG(0x665b)),
	regmap_reg_range(COMBRX_REG(0x66a0), COMBRX_REG(0x66db)),
	regmap_reg_range(COMBRX_REG(0x66f0), COMBRX_REG(0x66ff)),
	regmap_reg_range(COMBRX_REG(0x6700), COMBRX_REG(0x6790)),
};

static const struct regmap_access_table rk628_combrxphy_readable_table = {
	.yes_ranges     = rk628_combrxphy_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_combrxphy_readable_ranges),
};

static const struct regmap_range rk628_hdmirx_readable_ranges[] = {
	regmap_reg_range(HDMI_RX_HDMI_SETUP_CTRL, HDMI_RX_HDMI_SETUP_CTRL),
	regmap_reg_range(HDMI_RX_HDMI_PCB_CTRL, HDMI_RX_HDMI_PCB_CTRL),
	regmap_reg_range(HDMI_RX_HDMI_MODE_RECOVER, HDMI_RX_HDMI_ERROR_PROTECT),
	regmap_reg_range(HDMI_RX_HDMI_SYNC_CTRL, HDMI_RX_HDMI_CKM_RESULT),
	regmap_reg_range(HDMI_RX_HDMI_RESMPL_CTRL, HDMI_RX_HDMI_RESMPL_CTRL),
	regmap_reg_range(HDMI_RX_HDMI_VM_CFG_CH2, HDMI_RX_HDMI_STS),
	regmap_reg_range(HDMI_RX_HDCP_CTRL, HDMI_RX_HDCP_SETTINGS),
	regmap_reg_range(HDMI_RX_HDCP_KIDX, HDMI_RX_HDCP_KIDX),
	regmap_reg_range(HDMI_RX_HDCP_DBG, HDMI_RX_HDCP_AN0),
	regmap_reg_range(HDMI_RX_HDCP_STS, HDMI_RX_HDCP_STS),
	regmap_reg_range(HDMI_RX_MD_HCTRL1, HDMI_RX_MD_HACT_PX),
	regmap_reg_range(HDMI_RX_MD_VCTRL, HDMI_RX_MD_VSC),
	regmap_reg_range(HDMI_RX_MD_VOL, HDMI_RX_MD_VTL),
	regmap_reg_range(HDMI_RX_MD_IL_POL, HDMI_RX_MD_STS),
	regmap_reg_range(HDMI_RX_AUD_CTRL, HDMI_RX_AUD_CTRL),
	regmap_reg_range(HDMI_RX_AUD_PLL_CTRL, HDMI_RX_AUD_PLL_CTRL),
	regmap_reg_range(HDMI_RX_AUD_CLK_CTRL, HDMI_RX_AUD_CLK_CTRL),
	regmap_reg_range(HDMI_RX_AUD_FIFO_CTRL, HDMI_RX_AUD_FIFO_TH),
	regmap_reg_range(HDMI_RX_AUD_CHEXTR_CTRL, HDMI_RX_AUD_PAO_CTRL),
	regmap_reg_range(HDMI_RX_AUD_FIFO_STS, HDMI_RX_AUD_FIFO_STS),
	regmap_reg_range(HDMI_RX_AUDPLL_GEN_CTS, HDMI_RX_AUDPLL_GEN_N),
	regmap_reg_range(HDMI_RX_PDEC_CTRL, HDMI_RX_PDEC_CTRL),
	regmap_reg_range(HDMI_RX_PDEC_AUDIODET_CTRL, HDMI_RX_PDEC_AUDIODET_CTRL),
	regmap_reg_range(HDMI_RX_PDEC_ERR_FILTER, HDMI_RX_PDEC_ASP_CTRL),
	regmap_reg_range(HDMI_RX_PDEC_STS, HDMI_RX_PDEC_STS),
	regmap_reg_range(HDMI_RX_PDEC_GCP_AVMUTE, HDMI_RX_PDEC_GCP_AVMUTE),
	regmap_reg_range(HDMI_RX_PDEC_ACR_CTS, HDMI_RX_PDEC_ACR_N),
	regmap_reg_range(HDMI_RX_PDEC_AIF_CTRL, HDMI_RX_PDEC_AIF_PB0),
	regmap_reg_range(HDMI_RX_PDEC_AVI_PB, HDMI_RX_PDEC_AVI_PB),
	regmap_reg_range(HDMI_RX_HDMI20_CONTROL, HDMI_RX_CHLOCK_CONFIG),
	regmap_reg_range(HDMI_RX_SCDC_REGS1, HDMI_RX_SCDC_REGS2),
	regmap_reg_range(HDMI_RX_SCDC_WRDATA0, HDMI_RX_SCDC_WRDATA0),
	regmap_reg_range(HDMI_RX_PDEC_ISTS, HDMI_RX_PDEC_IEN),
	regmap_reg_range(HDMI_RX_AUD_FIFO_ISTS, HDMI_RX_AUD_FIFO_IEN),
	regmap_reg_range(HDMI_RX_MD_ISTS, HDMI_RX_MD_IEN),
	regmap_reg_range(HDMI_RX_HDMI_ISTS, HDMI_RX_HDMI_IEN),
	regmap_reg_range(HDMI_RX_DMI_DISABLE_IF, HDMI_RX_DMI_DISABLE_IF),
};

static const struct regmap_access_table rk628_hdmirx_readable_table = {
	.yes_ranges     = rk628_hdmirx_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_hdmirx_readable_ranges),
};

static const struct regmap_range rk628_key_readable_ranges[] = {
	regmap_reg_range(EDID_BASE, EDID_BASE + 0x400),
};

static const struct regmap_access_table rk628_key_readable_table = {
	.yes_ranges     = rk628_key_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_key_readable_ranges),
};

static const struct regmap_range rk628_combtxphy_readable_ranges[] = {
	regmap_reg_range(COMBTXPHY_BASE, COMBTXPHY_CON10),
};

static const struct regmap_access_table rk628_combtxphy_readable_table = {
	.yes_ranges     = rk628_combtxphy_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_combtxphy_readable_ranges),
};

static const struct regmap_range rk628_dsi0_readable_ranges[] = {
	regmap_reg_range(DSI0_BASE, DSI0_BASE + DSI_MAX_REGISTER),
};

static const struct regmap_access_table rk628_dsi0_readable_table = {
	.yes_ranges     = rk628_dsi0_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_dsi0_readable_ranges),
};

static const struct regmap_range rk628_dsi1_readable_ranges[] = {
	regmap_reg_range(DSI1_BASE, DSI1_BASE + DSI_MAX_REGISTER),
};

static const struct regmap_access_table rk628_dsi1_readable_table = {
	.yes_ranges     = rk628_dsi1_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_dsi1_readable_ranges),
};

static const struct regmap_range rk628_gvi_readable_ranges[] = {
	regmap_reg_range(GVI_BASE, GVI_BASE + GVI_COLOR_BAR_VTIMING1),
};

static const struct regmap_access_table rk628_gvi_readable_table = {
	.yes_ranges     = rk628_gvi_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_gvi_readable_ranges),
};

static const struct regmap_range rk628_csi_readable_ranges[] = {
	regmap_reg_range(CSITX_CONFIG_DONE, CSITX_CSITX_VERSION),
	regmap_reg_range(CSITX_SYS_CTRL0_IMD, CSITX_TIMING_HPW_PADDING_NUM),
	regmap_reg_range(CSITX_VOP_PATH_CTRL, CSITX_VOP_PATH_CTRL),
	regmap_reg_range(CSITX_VOP_PATH_PKT_CTRL, CSITX_VOP_PATH_PKT_CTRL),
	regmap_reg_range(CSITX_CSITX_STATUS0, CSITX_LPDT_DATA_IMD),
	regmap_reg_range(CSITX_DPHY_CTRL, CSITX_DPHY_CTRL),
};

static const struct regmap_access_table rk628_csi_readable_table = {
	.yes_ranges     = rk628_csi_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_csi_readable_ranges),
};

static const struct regmap_range rk628_hdmi_volatile_reg_ranges[] = {
	regmap_reg_range(HDMI_SYS_CTRL, HDMI_MAX_REG),
};

static const struct regmap_access_table rk628_hdmi_volatile_regs = {
	.yes_ranges = rk628_hdmi_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(rk628_hdmi_volatile_reg_ranges),
};

static const struct regmap_range rk628_gpio0_readable_ranges[] = {
	regmap_reg_range(RK628_GPIO0_BASE, RK628_GPIO0_BASE + GPIO_VER_ID),
};

static const struct regmap_access_table rk628_gpio0_readable_table = {
	.yes_ranges     = rk628_gpio0_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_gpio0_readable_ranges),
};

static const struct regmap_range rk628_gpio1_readable_ranges[] = {
	regmap_reg_range(RK628_GPIO1_BASE, RK628_GPIO1_BASE + GPIO_VER_ID),
};

static const struct regmap_access_table rk628_gpio1_readable_table = {
	.yes_ranges     = rk628_gpio1_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_gpio1_readable_ranges),
};

static const struct regmap_range rk628_gpio2_readable_ranges[] = {
	regmap_reg_range(RK628_GPIO2_BASE, RK628_GPIO2_BASE + GPIO_VER_ID),
};

static const struct regmap_access_table rk628_gpio2_readable_table = {
	.yes_ranges     = rk628_gpio2_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_gpio2_readable_ranges),
};

static const struct regmap_range rk628_gpio3_readable_ranges[] = {
	regmap_reg_range(RK628_GPIO3_BASE, RK628_GPIO3_BASE + GPIO_VER_ID),
};

static const struct regmap_access_table rk628_gpio3_readable_table = {
	.yes_ranges     = rk628_gpio3_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_gpio3_readable_ranges),
};

static const struct regmap_config rk628_regmap_config[RK628_DEV_MAX] = {
	[RK628_DEV_GRF] = {
		.name = "grf",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = GRF_MAX_REGISTER,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
	},
	[RK628_DEV_CRU] = {
		.name = "cru",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = CRU_MAX_REGISTER,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_cru_readable_table,
	},
	[RK628_DEV_COMBRXPHY] = {
		.name = "combrxphy",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = COMBRX_REG(0x6790),
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_combrxphy_readable_table,
	},
	[RK628_DEV_HDMIRX] = {
		.name = "hdmirx",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = HDMI_RX_MAX_REGISTER,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_hdmirx_readable_table,
	},
	[RK628_DEV_ADAPTER] = {
		.name = "adapter",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = KEY_MAX_REGISTER,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_key_readable_table,
	},
	[RK628_DEV_COMBTXPHY] = {
		.name = "combtxphy",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = COMBTXPHY_CON10,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_combtxphy_readable_table,
	},
	[RK628_DEV_DSI0] = {
		.name = "dsi0",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = DSI0_BASE + DSI_MAX_REGISTER,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_dsi0_readable_table,
	},
	[RK628_DEV_DSI1] = {
		.name = "dsi1",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = DSI1_BASE + DSI_MAX_REGISTER,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_dsi1_readable_table,
	},
	[RK628_DEV_GVI] = {
		.name = "gvi",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = GVI_COLOR_BAR_VTIMING1,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_gvi_readable_table,
	},
	[RK628_DEV_CSI] = {
		.name = "csi",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = CSI_MAX_REGISTER,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_csi_readable_table,
	},
	[RK628_DEV_HDMITX] = {
		.name = "hdmi",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = HDMI_MAX_REG,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_hdmi_volatile_regs,
	},
	[RK628_DEV_GPIO0] = {
		.name = "gpio0",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = RK628_GPIO0_BASE + GPIO_VER_ID,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_gpio0_readable_table,
	},
	[RK628_DEV_GPIO1] = {
		.name = "gpio1",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = RK628_GPIO1_BASE + GPIO_VER_ID,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_gpio1_readable_table,
	},
	[RK628_DEV_GPIO2] = {
		.name = "gpio2",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = RK628_GPIO2_BASE + GPIO_VER_ID,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_gpio2_readable_table,
	},
	[RK628_DEV_GPIO3] = {
		.name = "gpio3",
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = RK628_GPIO3_BASE + GPIO_VER_ID,
		.reg_format_endian = REGMAP_ENDIAN_NATIVE,
		.val_format_endian = REGMAP_ENDIAN_NATIVE,
		.rd_table = &rk628_gpio3_readable_table,
	},
};

static void rk628_display_disable(struct rk628 *rk628)
{
	if (!rk628->display_enabled)
		return;

	if (rk628->output_mode == OUTPUT_MODE_CSI)
		rk628_csi_disable(rk628);

	if (rk628->output_mode == OUTPUT_MODE_GVI)
		rk628_gvi_disable(rk628);

	if (rk628->output_mode == OUTPUT_MODE_LVDS)
		rk628_lvds_disable(rk628);

	if (rk628->output_mode == OUTPUT_MODE_DSI)
		rk628_dsi_disable(rk628);

	rk628_post_process_disable(rk628);

	if (rk628->input_mode == INPUT_MODE_HDMI)
		rk628_hdmirx_disable(rk628);

	rk628->display_enabled = false;
}

static void rk628_display_resume(struct rk628 *rk628)
{
	u8 ret = 0;

	if (rk628->display_enabled)
		return;

	if (rk628->input_mode == INPUT_MODE_HDMI) {
		ret = rk628_hdmirx_enable(rk628);
		if ((ret == HDMIRX_PLUGOUT) || (ret & HDMIRX_NOSIGNAL)) {
			rk628_display_disable(rk628);
			return;
		}
	}

	if (rk628->input_mode == INPUT_MODE_RGB)
		rk628_rgb_rx_enable(rk628);

	if (rk628->input_mode == INPUT_MODE_BT1120)
		rk628_bt1120_rx_enable(rk628);

	rk628_post_process_init(rk628);
	rk628_post_process_enable(rk628);

	if (rk628->output_mode == OUTPUT_MODE_DSI) {
		rk628_mipi_dsi_pre_enable(rk628);
		rk628_mipi_dsi_enable(rk628);
	}

	if (rk628->output_mode == OUTPUT_MODE_LVDS)
		rk628_lvds_enable(rk628);

	if (rk628->output_mode == OUTPUT_MODE_GVI)
		rk628_gvi_enable(rk628);

	if (rk628->output_mode == OUTPUT_MODE_CSI)
		rk628_csi_enable(rk628);

#ifdef CONFIG_RK628_MISC_HDMITX
	if (rk628->output_mode == OUTPUT_MODE_HDMI)
		rk628_hdmitx_enable(rk628);
#endif

	rk628->display_enabled = true;
}

static void rk628_display_enable(struct rk628 *rk628)
{
	u8 ret = 0;

	if (rk628->display_enabled)
		return;

	if (rk628->input_mode == INPUT_MODE_RGB)
		rk628_rgb_rx_enable(rk628);

	if (rk628->input_mode == INPUT_MODE_BT1120)
		rk628_bt1120_rx_enable(rk628);

	if (rk628->output_mode == OUTPUT_MODE_BT1120)
		rk628_bt1120_tx_enable(rk628);

	if (rk628->output_mode == OUTPUT_MODE_DSI)
		queue_delayed_work(rk628->dsi_wq, &rk628->dsi_delay_work, msecs_to_jiffies(10));

	if (rk628->input_mode == INPUT_MODE_HDMI) {
		ret = rk628_hdmirx_enable(rk628);
		if ((ret == HDMIRX_PLUGOUT) || (ret & HDMIRX_NOSIGNAL)) {
			rk628_display_disable(rk628);
			return;
		}
	}

	if (rk628->output_mode != OUTPUT_MODE_HDMI) {
		rk628_post_process_init(rk628);
		rk628_post_process_enable(rk628);
	}

	if (rk628->output_mode == OUTPUT_MODE_LVDS)
		rk628_lvds_enable(rk628);

	if (rk628->output_mode == OUTPUT_MODE_GVI)
		rk628_gvi_enable(rk628);

	if (rk628->output_mode == OUTPUT_MODE_CSI)
		rk628_csi_enable(rk628);

#ifdef CONFIG_RK628_MISC_HDMITX
	if (rk628->output_mode == OUTPUT_MODE_HDMI)
		rk628_hdmitx_enable(rk628);
#endif

	rk628->display_enabled = true;
}

static void rk628_display_work(struct work_struct *work)
{
	u8 ret = 0;
	struct rk628 *rk628 =
		container_of(work, struct rk628, delay_work.work);
	int delay = msecs_to_jiffies(2000);

	if (rk628->input_mode == INPUT_MODE_HDMI) {
		ret = rk628_hdmirx_detect(rk628);
		if (!(ret & (HDMIRX_CHANGED | HDMIRX_NOLOCK))) {
			if (!rk628->plugin_det_gpio)
				queue_delayed_work(rk628->monitor_wq,
						   &rk628->delay_work, delay);
			else
				rk628_hdmirx_enable_interrupts(rk628, true);
			return;
		}
	}

	if (ret & HDMIRX_PLUGIN) {
		/* if resolution or input format change, disable first */
		rk628_display_disable(rk628);
		rk628_display_enable(rk628);
	} else if (ret & HDMIRX_PLUGOUT) {
		rk628_display_disable(rk628);
	}

	if (rk628->input_mode == INPUT_MODE_HDMI) {
		if (!rk628->plugin_det_gpio) {
			if (ret & HDMIRX_NOLOCK)
				delay = msecs_to_jiffies(200);
			queue_delayed_work(rk628->monitor_wq, &rk628->delay_work,
					   delay);
		} else {
			rk628_hdmirx_enable_interrupts(rk628, true);
		}
	}
}

static void rk628_dsi_work(struct work_struct *work)
{
	struct rk628 *rk628 = container_of(work, struct rk628, dsi_delay_work.work);

	rk628_mipi_dsi_pre_enable(rk628);
	rk628_mipi_dsi_enable(rk628);
}

static irqreturn_t rk628_hdmirx_plugin_irq(int irq, void *dev_id)
{
	struct rk628 *rk628 = dev_id;

	rk628_hdmirx_enable_interrupts(rk628, false);
	/* clear interrupts */
	rk628_i2c_write(rk628, HDMI_RX_MD_ICLR, 0xffffffff);
	rk628_i2c_write(rk628, HDMI_RX_PDEC_ICLR, 0xffffffff);
	rk628_i2c_write(rk628, GRF_INTR0_CLR_EN, 0x01000100);

	/* control hpd after 50ms */
	schedule_delayed_work(&rk628->delay_work, HZ / 20);

	return IRQ_HANDLED;
}

static bool rk628_input_is_rgb(struct rk628 *rk628)
{
	if (rk628->input_mode == INPUT_MODE_RGB || rk628->input_mode == INPUT_MODE_BT1120)
		return true;

	return false;
}

static int rk628_display_route_info_parse(struct rk628 *rk628)
{
	struct device_node *np;
	int ret = 0;
	u32 val;

	if (of_property_read_bool(rk628->dev->of_node, "rk628,hdmi-in"))
		rk628->input_mode = INPUT_MODE_HDMI;
	else if (of_property_read_bool(rk628->dev->of_node, "rk628,rgb-in"))
		rk628->input_mode = INPUT_MODE_RGB;
	else if (of_property_read_bool(rk628->dev->of_node, "rk628,bt1120-in"))
		rk628->input_mode = INPUT_MODE_BT1120;
	else
		rk628->input_mode = INPUT_MODE_RGB;

	if (of_find_node_by_name(rk628->dev->of_node, "rk628-dsi")) {
		np = of_find_node_by_name(rk628->dev->of_node, "rk628-dsi");
		ret = rk628_dsi_parse(rk628, np);
	} else if (of_find_node_by_name(rk628->dev->of_node, "rk628-lvds")) {
		np = of_find_node_by_name(rk628->dev->of_node, "rk628-lvds");
		ret = rk628_lvds_parse(rk628, np);
	} else if (of_find_node_by_name(rk628->dev->of_node, "rk628-gvi")) {
		np = of_find_node_by_name(rk628->dev->of_node, "rk628-gvi");
		ret = rk628_gvi_parse(rk628, np);
	} else if (of_find_node_by_name(rk628->dev->of_node, "rk628-bt1120")) {
		rk628->output_mode = OUTPUT_MODE_BT1120;
	} else {
		if (of_property_read_bool(rk628->dev->of_node, "rk628,hdmi-out"))
			rk628->output_mode = OUTPUT_MODE_HDMI;
		else if (of_property_read_bool(rk628->dev->of_node, "rk628,csi-out"))
			rk628->output_mode = OUTPUT_MODE_CSI;
	}

	if (of_property_read_u32(rk628->dev->of_node, "mode-sync-pol", &val) < 0)
		rk628->sync_pol = MODE_FLAG_PSYNC;
	else
		rk628->sync_pol = (!val ? MODE_FLAG_NSYNC : MODE_FLAG_PSYNC);

	if (rk628_input_is_rgb(rk628) && rk628->output_mode == OUTPUT_MODE_RGB)
		return -EINVAL;

	return ret;
}

static void
rk628_display_mode_from_videomode(const struct rk628_videomode *vm,
				  struct rk628_display_mode *dmode)
{
	dmode->hdisplay = vm->hactive;
	dmode->hsync_start = dmode->hdisplay + vm->hfront_porch;
	dmode->hsync_end = dmode->hsync_start + vm->hsync_len;
	dmode->htotal = dmode->hsync_end + vm->hback_porch;

	dmode->vdisplay = vm->vactive;
	dmode->vsync_start = dmode->vdisplay + vm->vfront_porch;
	dmode->vsync_end = dmode->vsync_start + vm->vsync_len;
	dmode->vtotal = dmode->vsync_end + vm->vback_porch;

	dmode->clock = vm->pixelclock / 1000;
	dmode->flags = vm->flags;
}

static void
of_parse_rk628_display_timing(struct device_node *np, struct rk628_videomode *vm)
{
	u8 val;

	of_property_read_u32(np, "clock-frequency", &vm->pixelclock);
	of_property_read_u32(np, "hactive", &vm->hactive);
	of_property_read_u32(np, "hfront-porch", &vm->hfront_porch);
	of_property_read_u32(np, "hback-porch", &vm->hback_porch);
	of_property_read_u32(np, "hsync-len", &vm->hsync_len);

	of_property_read_u32(np, "vactive", &vm->vactive);
	of_property_read_u32(np, "vfront-porch", &vm->vfront_porch);
	of_property_read_u32(np, "vback-porch", &vm->vback_porch);
	of_property_read_u32(np, "vsync-len", &vm->vsync_len);

	vm->flags = 0;
	of_property_read_u8(np, "hsync-active", &val);
	vm->flags |= val ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;

	of_property_read_u8(np, "vsync-active", &val);
	vm->flags |= val ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;
}

static int rk628_get_video_mode(struct rk628 *rk628)
{

	struct device_node *timings_np, *src_np, *dst_np;
	struct rk628_videomode vm;

	timings_np = of_get_child_by_name(rk628->dev->of_node, "display-timings");
	if (!timings_np) {
		dev_info(rk628->dev, "failed to found display timings\n");
		return -EINVAL;
	}

	src_np = of_get_child_by_name(timings_np, "src-timing");
	if (!src_np) {
		dev_info(rk628->dev, "failed to found src timing\n");
		of_node_put(timings_np);
		return -EINVAL;
	}

	of_parse_rk628_display_timing(src_np, &vm);
	rk628_display_mode_from_videomode(&vm, &rk628->src_mode);
	dev_info(rk628->dev, "src mode: %d %d %d %d %d %d %d %d %d 0x%x\n",
		 rk628->src_mode.clock, rk628->src_mode.hdisplay, rk628->src_mode.hsync_start,
		 rk628->src_mode.hsync_end, rk628->src_mode.htotal, rk628->src_mode.vdisplay,
		 rk628->src_mode.vsync_start, rk628->src_mode.vsync_end, rk628->src_mode.vtotal,
		 rk628->src_mode.flags);

	dst_np = of_get_child_by_name(timings_np, "dst-timing");
	if (!dst_np) {
		dev_info(rk628->dev, "failed to found dst timing\n");
		of_node_put(timings_np);
		of_node_put(src_np);
		return -EINVAL;
	}

	of_parse_rk628_display_timing(dst_np, &vm);
	rk628_display_mode_from_videomode(&vm, &rk628->dst_mode);
	dev_info(rk628->dev, "dst mode: %d %d %d %d %d %d %d %d %d 0x%x\n",
		 rk628->dst_mode.clock, rk628->dst_mode.hdisplay, rk628->dst_mode.hsync_start,
		 rk628->dst_mode.hsync_end, rk628->dst_mode.htotal, rk628->dst_mode.vdisplay,
		 rk628->dst_mode.vsync_start, rk628->dst_mode.vsync_end, rk628->dst_mode.vtotal,
		 rk628->dst_mode.flags);

	of_node_put(timings_np);
	of_node_put(src_np);
	of_node_put(dst_np);

	return 0;
}

static int rk628_display_timings_get(struct rk628 *rk628)
{
	int ret;

	ret = rk628_get_video_mode(rk628);

	return ret;

}

#define DEBUG_PRINT(args...) \
		do { \
			if (s) \
				seq_printf(s, args); \
			else \
				pr_info(args); \
		} while (0)

static int rk628_debugfs_dump(struct seq_file *s, void *data)
{
	struct rk628 *rk628 = s->private;

	u32 val;
	u32 dsp_htotal, dsp_hs_end, dsp_hact_st, dsp_hact_end;
	u32 dsp_vtotal, dsp_vs_end, dsp_vact_st, dsp_vact_end;
	u32 src_hactive, src_hoffset, src_htotal, src_hs_end;
	u32 src_vactive, src_voffset, src_vtotal, src_vs_end;

	u32 input_mode, output_mode;
	char input_s[10];
	char output_s[13];

	bool r2y, y2r;
	char csc_mode_r2y_s[10];
	char csc_mode_y2r_s[10];
	u32 csc;
	enum csc_mode {
		BT601_L,
		BT709_L,
		BT601_F,
		BT2020
	};

	int sw_hsync_pol, sw_vsync_pol;
	u32 dsp_frame_v_start, dsp_frame_h_start;

	int sclk_vop_sel = 0;
	u32 sclk_vop_div;
	u64 sclk_vop;
	u32 reg_v;
	u32 fps;

	u32 imodet_clk;
	u32 imodet_clk_sel;
	u32 imodet_clk_div;

	int clk_rx_read_sel = 0;
	u32 clk_rx_read_div;
	u64 clk_rx_read;

	u32 tdms_clk_div;
	u32 tdms_clk;
	u32 common_tdms_clk[19] = {
		25170, 27000, 33750, 40000, 59400,
		65000, 68250, 74250, 83500, 85500,
		88750, 92812, 101000, 108000, 119000,
		135000, 148500, 162000, 297000,
	};

	//get sclk vop
	rk628_i2c_read(rk628, 0xc0088, &reg_v);
	sclk_vop_sel = (reg_v & 0x20) ? 1 : 0;
	rk628_i2c_read(rk628, 0xc00b4, &reg_v);
	if (reg_v)
		sclk_vop_div = reg_v;
	else
		sclk_vop_div = 0x10002;
	/* gpll 983.04MHz */
	/* cpll 1188MHz */
	if (sclk_vop_sel)
		sclk_vop = (u64)983040 * ((sclk_vop_div & 0xffff0000) >> 16);
	else
		sclk_vop = (u64)1188000 * ((sclk_vop_div & 0xffff0000) >> 16);
	do_div(sclk_vop, sclk_vop_div & 0xffff);

	//get rx read clk
	rk628_i2c_read(rk628, 0xc0088, &reg_v);
	clk_rx_read_sel = (reg_v & 0x10) ? 1 : 0;
	rk628_i2c_read(rk628, 0xc00b8, &reg_v);
	if (reg_v)
		clk_rx_read_div = reg_v;
	else
		clk_rx_read_div = 0x10002;
	/* gpll 983.04MHz */
	/* cpll 1188MHz */
	if (clk_rx_read_sel)
		clk_rx_read = (u64)983040 * ((clk_rx_read_div & 0xffff0000) >> 16);
	else
		clk_rx_read = (u64)1188000 * ((clk_rx_read_div & 0xffff0000) >> 16);
	do_div(clk_rx_read, clk_rx_read_div & 0xffff);

	//get imodet clk
	rk628_i2c_read(rk628, 0xc0094, &reg_v);
	imodet_clk_sel = (reg_v & 0x20) ? 1 : 0;

	if (reg_v)
		imodet_clk_div = (reg_v & 0x1f) + 1;
	else
		imodet_clk_div = 0x18;
	/* gpll 983.04MHz */
	/* cpll 1188MHz */
	if (imodet_clk_sel)
		imodet_clk = 983040 / imodet_clk_div;
	else
		imodet_clk = 1188000 / imodet_clk_div;

	//get input interface type
	rk628_i2c_read(rk628, GRF_SYSTEM_CON0, &val);
	input_mode = val & 0x7;
	output_mode = (val & 0xf8) >> 3;
	sw_hsync_pol = (val & 0x4000000) ? 1 : 0;
	sw_vsync_pol = (val & 0x2000000) ? 1 : 0;
	switch (input_mode) {
	case 0:
		strcpy(input_s, "HDMI");
		break;
	case 1:
		strcpy(input_s, "reserved");
		break;
	case 2:
		strcpy(input_s, "BT1120");
		break;
	case 3:
		strcpy(input_s, "RGB");
		break;
	case 4:
		strcpy(input_s, "YUV");
		break;
	default:
		strcpy(input_s, "unknown");
	}
	DEBUG_PRINT("input:%s\n", input_s);
	if (input_mode == 0) {
		//get tdms clk
		rk628_i2c_read(rk628, 0x16654, &reg_v);
		reg_v = (reg_v & 0x3f0000) >> 16;
		if (reg_v >= 0 && reg_v <= 19)
			tdms_clk = common_tdms_clk[reg_v];
		else
			tdms_clk = 148500;

		rk628_i2c_read(rk628, 0x166a8, &reg_v);
		reg_v = (reg_v & 0xf00) >> 8;
		if (reg_v == 0x6)
			tdms_clk_div = 1;
		else if (reg_v == 0x0)
			tdms_clk_div = 2;
		else
			tdms_clk_div = 1;

		//get input hdmi timing
		//get horizon timing
		rk628_i2c_read(rk628, 0x30150, &reg_v);
		src_hactive = reg_v & 0xffff;

		rk628_i2c_read(rk628, 0x3014c, &reg_v);
		src_hoffset = (reg_v & 0xffff);

		src_hactive *= tdms_clk_div;
		src_hoffset *=  tdms_clk_div;

		src_htotal = (reg_v & 0xffff0000)>>16;
		src_htotal *= tdms_clk_div;

		rk628_i2c_read(rk628, 0x30148, &reg_v);
		reg_v = reg_v & 0xffff;
		src_hs_end = reg_v * tdms_clk * tdms_clk_div / imodet_clk;

		//get vertical timing
		rk628_i2c_read(rk628, 0x30168, &reg_v);
		src_vactive = reg_v & 0xffff;
		rk628_i2c_read(rk628, 0x30170, &reg_v);
		src_vtotal = reg_v & 0xffff;
		rk628_i2c_read(rk628, 0x30164, &reg_v);
		src_voffset = (reg_v & 0xffff);

		rk628_i2c_read(rk628, 0x3015c, &reg_v);
		reg_v = reg_v & 0xffff;
		src_vs_end = reg_v * clk_rx_read;
		do_div(src_vs_end, imodet_clk * src_htotal);

		//get fps and print
		fps = clk_rx_read * 1000;
		do_div(fps, src_htotal * src_vtotal);
		DEBUG_PRINT("    Display mode: %dx%dp%d,dclk[%llu],tdms_clk[%d]\n",
			    src_hactive, src_vactive, fps, clk_rx_read, tdms_clk);

		DEBUG_PRINT("\tH: %d %d %d %d\n", src_hactive, src_htotal - src_hoffset,
			    src_htotal - src_hoffset + src_hs_end, src_htotal);

		DEBUG_PRINT("\tV: %d %d %d %d\n", src_vactive,
			    src_vtotal - src_voffset - src_vs_end,
			    src_vtotal - src_voffset, src_vtotal);
	} else if (input_mode == 2 || input_mode == 3 || input_mode == 4) {
		//get timing
		rk628_i2c_read(rk628, 0x130, &reg_v);
		src_hactive = reg_v & 0xffff;

		rk628_i2c_read(rk628, 0x12c, &reg_v);
		src_vactive = (reg_v & 0xffff);

		rk628_i2c_read(rk628, 0x134, &reg_v);
		src_htotal = (reg_v & 0xffff0000) >> 16;
		src_vtotal = reg_v & 0xffff;

		//get fps and print
		fps = clk_rx_read * 1000;
		do_div(fps, src_htotal * src_vtotal);
		DEBUG_PRINT("    Display mode: %dx%dp%d,dclk[%llu]\n",
			    src_hactive, src_vactive, fps, clk_rx_read);

		DEBUG_PRINT("\tH-total: %d\n", src_htotal);

		DEBUG_PRINT("\tV-total: %d\n", src_vtotal);
	}
	//get output interface type
	switch (output_mode & 0x7) {
	case 1:
		strcpy(output_s, "GVI");
		break;
	case 2:
		strcpy(output_s, "LVDS");
		break;
	case 3:
		strcpy(output_s, "HDMI");
		break;
	case 4:
		strcpy(output_s, "CSI");
		break;
	case 5:
		strcpy(output_s, "DSI");
		break;
	default:
		strcpy(output_s, "");
	}
	strcpy(output_s + 4, " ");
	switch (output_mode >> 2) {
	case 0:
		strcpy(output_s + 5, "");
		break;
	case 1:
		strcpy(output_s + 5, "BT1120");
		break;
	case 2:
		strcpy(output_s + 5, "RGB");
		break;
	case 3:
		strcpy(output_s + 5, "YUV");
		break;
	default:
		strcpy(output_s + 5, "unknown");
	}
	DEBUG_PRINT("output:%s\n", output_s);

	//get output timing
	rk628_i2c_read(rk628, GRF_SCALER_CON3, &val);
	dsp_htotal = val & 0xffff;
	dsp_hs_end = (val & 0xff0000) >> 16;

	rk628_i2c_read(rk628, GRF_SCALER_CON4, &val);
	dsp_hact_end = val & 0xffff;
	dsp_hact_st = (val & 0xfff0000) >> 16;

	rk628_i2c_read(rk628, GRF_SCALER_CON5, &val);
	dsp_vtotal = val & 0xfff;
	dsp_vs_end = (val & 0xff0000) >> 16;

	rk628_i2c_read(rk628, GRF_SCALER_CON6, &val);
	dsp_vact_st = (val & 0xfff0000) >> 16;
	dsp_vact_end = val & 0xfff;

	fps = sclk_vop * 1000;
	do_div(fps, dsp_vtotal * dsp_htotal);

	DEBUG_PRINT("    Display mode: %dx%dp%d,dclk[%llu]\n",
		    dsp_hact_end - dsp_hact_st, dsp_vact_end - dsp_vact_st, fps, sclk_vop);
	DEBUG_PRINT("\tH: %d %d %d %d\n", dsp_hact_end - dsp_hact_st,
		    dsp_htotal - dsp_hact_st, dsp_htotal - dsp_hact_st + dsp_hs_end, dsp_htotal);
	DEBUG_PRINT("\tV: %d %d %d %d\n", dsp_vact_end - dsp_vact_st,
		    dsp_vtotal - dsp_vact_st, dsp_vtotal - dsp_vact_st + dsp_vs_end, dsp_vtotal);

	//get csc and system information
	rk628_i2c_read(rk628, GRF_CSC_CTRL_CON, &val);
	r2y = ((val & 0x10) == 0x10);
	y2r = ((val & 0x1) == 0x1);
	csc = (val & 0xc0) >> 6;
	switch (csc) {
	case BT601_L:
		strcpy(csc_mode_r2y_s, "BT601_L");
		break;
	case BT601_F:
		strcpy(csc_mode_r2y_s, "BT601_F");
		break;
	case BT709_L:
		strcpy(csc_mode_r2y_s, "BT709_L");
		break;
	case BT2020:
		strcpy(csc_mode_r2y_s, "BT2020");
		break;
	}

	csc = (val & 0xc) >> 2;
	switch (csc) {
	case BT601_L:
		strcpy(csc_mode_y2r_s, "BT601_L");
		break;
	case BT601_F:
		strcpy(csc_mode_y2r_s, "BT601_F");
		break;
	case BT709_L:
		strcpy(csc_mode_y2r_s, "BT709_L");
		break;
	case BT2020:
		strcpy(csc_mode_y2r_s, "BT2020");
		break;

	}
	DEBUG_PRINT("csc:\n");

	if (r2y)
		DEBUG_PRINT("\tr2y[1],csc mode:%s\n", csc_mode_r2y_s);
	else if (y2r)
		DEBUG_PRINT("\ty2r[1],csc mode:%s\n", csc_mode_y2r_s);
	else
		DEBUG_PRINT("\tnot open\n");

	rk628_i2c_read(rk628, GRF_SCALER_CON2, &val);
	dsp_frame_h_start = val & 0xffff;
	dsp_frame_v_start = (val & 0xffff0000) >> 16;

	DEBUG_PRINT("system:\n");
	DEBUG_PRINT("\tsw_hsync_pol:%d, sw_vsync_pol:%d\n", sw_hsync_pol, sw_vsync_pol);
	DEBUG_PRINT("\tdsp_frame_h_start:%d, dsp_frame_v_start:%d\n",
		    dsp_frame_h_start, dsp_frame_v_start);

	return 0;
}

static int rk628_debugfs_open(struct inode *inode, struct file *file)
{
	struct rk628 *rk628 = inode->i_private;

	return single_open(file, rk628_debugfs_dump, rk628);
}


static const struct file_operations rk628_debugfs_summary_fops = {
	.owner = THIS_MODULE,
	.open = rk628_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,

};

static int
rk628_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct rk628 *rk628;
	int i, ret;
	int err;
	unsigned long irq_flags;
	struct dentry *debug_dir;

	dev_info(dev, "RK628 misc driver version: %s\n", DRIVER_VERSION);

	rk628 = devm_kzalloc(dev, sizeof(*rk628), GFP_KERNEL);
	if (!rk628)
		return -ENOMEM;

	rk628->dev = dev;
	rk628->client = client;
	i2c_set_clientdata(client, rk628);
	rk628->hdmirx_irq = client->irq;

	ret = rk628_display_route_info_parse(rk628);
	if (ret) {
		dev_err(dev, "display route err\n");
		return ret;
	}

	if (rk628->output_mode != OUTPUT_MODE_HDMI &&
	    rk628->output_mode != OUTPUT_MODE_CSI) {
		ret = rk628_display_timings_get(rk628);
		if (ret) {
			dev_info(dev, "display timings err\n");
			return ret;
		}
	}

	rk628->soc_24M = devm_clk_get(dev, "soc_24M");
	if (rk628->soc_24M == ERR_PTR(-ENOENT))
		rk628->soc_24M = NULL;

	if (IS_ERR(rk628->soc_24M)) {
		ret = PTR_ERR(rk628->soc_24M);
		dev_err(dev, "Unable to get soc_24M: %d\n", ret);
		return ret;
	}

	clk_prepare_enable(rk628->soc_24M);

	rk628->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						     GPIOD_OUT_LOW);
	if (IS_ERR(rk628->enable_gpio)) {
		ret = PTR_ERR(rk628->enable_gpio);
		dev_err(dev, "failed to request enable GPIO: %d\n", ret);
		return ret;
	}

	rk628->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(rk628->reset_gpio)) {
		ret = PTR_ERR(rk628->reset_gpio);
		dev_err(dev, "failed to request reset GPIO: %d\n", ret);
		return ret;
	}

	rk628->plugin_det_gpio = devm_gpiod_get_optional(dev, "plugin-det",
						    GPIOD_IN);
	if (IS_ERR(rk628->plugin_det_gpio)) {
		dev_err(rk628->dev, "failed to get hdmirx det gpio\n");
		ret = PTR_ERR(rk628->plugin_det_gpio);
		return ret;
	}

	gpiod_set_value(rk628->enable_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value(rk628->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value(rk628->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value(rk628->reset_gpio, 0);
	usleep_range(10000, 11000);

	for (i = 0; i < RK628_DEV_MAX; i++) {
		const struct regmap_config *config = &rk628_regmap_config[i];

		if (!config->name)
			continue;

		rk628->regmap[i] = devm_regmap_init_i2c(client, config);
		if (IS_ERR(rk628->regmap[i])) {
			ret = PTR_ERR(rk628->regmap[i]);
			dev_err(dev, "failed to allocate register map %d: %d\n",
				i, ret);
			return ret;
		}
	}

	/* selete int io function */
	ret = rk628_i2c_write(rk628, GRF_GPIO3AB_SEL_CON, 0x30002000);
	if (ret) {
		dev_err(dev, "failed to access register: %d\n", ret);
		return ret;
	}

	rk628->monitor_wq = alloc_ordered_workqueue("%s",
		WQ_MEM_RECLAIM | WQ_FREEZABLE, "rk628-monitor-wq");
	INIT_DELAYED_WORK(&rk628->delay_work, rk628_display_work);

	if (rk628->output_mode == OUTPUT_MODE_DSI) {
		rk628->dsi_wq = alloc_ordered_workqueue("%s",
			WQ_MEM_RECLAIM | WQ_FREEZABLE, "rk628-dsi-wq");
		INIT_DELAYED_WORK(&rk628->dsi_delay_work, rk628_dsi_work);
	}

	rk628_cru_init(rk628);

	if (rk628->output_mode == OUTPUT_MODE_CSI)
		rk628_csi_init(rk628);

	if (rk628->input_mode == INPUT_MODE_HDMI) {
		if (rk628->plugin_det_gpio) {
			rk628->plugin_irq = gpiod_to_irq(rk628->plugin_det_gpio);
			if (rk628->plugin_irq < 0) {
				dev_err(rk628->dev, "failed to get plugin det irq\n");
				err = rk628->plugin_irq;
				return err;
			}

			err = devm_request_threaded_irq(dev, rk628->plugin_irq, NULL,
					rk628_hdmirx_plugin_irq, IRQF_TRIGGER_FALLING |
					IRQF_TRIGGER_RISING | IRQF_ONESHOT, "rk628_hdmirx", rk628);
			if (err) {
				dev_err(rk628->dev, "failed to register plugin det irq (%d)\n",
					err);
				return err;
			}

			if (rk628->hdmirx_irq) {
				irq_flags =
					irqd_get_trigger_type(irq_get_irq_data(rk628->hdmirx_irq));
				dev_dbg(rk628->dev, "cfg hdmirx irq, flags: %lu!\n", irq_flags);
				err = devm_request_threaded_irq(dev, rk628->hdmirx_irq, NULL,
						rk628_hdmirx_plugin_irq, irq_flags |
						IRQF_ONESHOT, "rk628", rk628);
				if (err) {
					dev_err(rk628->dev, "request rk628 irq failed! err:%d\n",
							err);
					return err;
				}
				/* hdmirx int en */
				rk628_i2c_write(rk628, GRF_INTR0_EN, 0x01000100);
				rk628_display_enable(rk628);
				queue_delayed_work(rk628->monitor_wq, &rk628->delay_work,
						   msecs_to_jiffies(20));
			}
		} else {
			rk628_display_enable(rk628);
			queue_delayed_work(rk628->monitor_wq, &rk628->delay_work,
					    msecs_to_jiffies(50));
		}
	} else {
		rk628_display_enable(rk628);
	}

	pm_runtime_enable(dev);
	debug_dir = debugfs_create_dir(rk628->dev->driver->name, NULL);
	if (!debug_dir)
		return 0;

	debugfs_create_file("summary", 0400, debug_dir, rk628, &rk628_debugfs_summary_fops);

	return 0;
}

static int rk628_i2c_remove(struct i2c_client *client)
{
	struct rk628 *rk628 = i2c_get_clientdata(client);
	struct device *dev = &client->dev;

	if (rk628->output_mode == OUTPUT_MODE_DSI) {
		cancel_delayed_work_sync(&rk628->dsi_delay_work);
		destroy_workqueue(rk628->dsi_wq);
	}

	cancel_delayed_work_sync(&rk628->delay_work);
	destroy_workqueue(rk628->monitor_wq);
	pm_runtime_disable(dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rk628_suspend(struct device *dev)
{
	struct rk628 *rk628 = dev_get_drvdata(dev);

	rk628_display_disable(rk628);

	return 0;
}

static int rk628_resume(struct device *dev)
{
	struct rk628 *rk628 = dev_get_drvdata(dev);

	rk628_display_resume(rk628);

	return 0;
}
#endif

static const struct dev_pm_ops rk628_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend = rk628_suspend,
	.resume = rk628_resume,
#endif
};
static const struct of_device_id rk628_of_match[] = {
	{ .compatible = "rockchip,rk628", },
	{}
};
MODULE_DEVICE_TABLE(of, rk628_of_match);

static const struct i2c_device_id rk628_i2c_id[] = {
	{ "rk628", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, rk628_i2c_id);

static struct i2c_driver rk628_i2c_driver = {
	.driver = {
		.name = "rk628",
		.of_match_table = of_match_ptr(rk628_of_match),
		.pm = &rk628_pm_ops,
	},
	.probe = rk628_i2c_probe,
	.remove = rk628_i2c_remove,
	.id_table = rk628_i2c_id,
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT_RK628
static int __init rk628_i2c_driver_init(void)
{
	i2c_add_driver(&rk628_i2c_driver);

	return 0;
}
subsys_initcall_sync(rk628_i2c_driver_init);

static void __exit rk628_i2c_driver_exit(void)
{
	i2c_del_driver(&rk628_i2c_driver);
}
module_exit(rk628_i2c_driver_exit);
#else
module_i2c_driver(rk628_i2c_driver);
#endif

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 MFD driver");
MODULE_LICENSE("GPL");
