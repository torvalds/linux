/*
 * omap_hwmod_2430_data.c - hardware modules present on the OMAP2430 chips
 *
 * Copyright (C) 2009-2011 Nokia Corporation
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX handle crossbar/shared link difference for L3?
 * XXX these should be marked initdata for multi-OMAP kernels
 */

#include <linux/i2c-omap.h>
#include <linux/platform_data/asoc-ti-mcbsp.h>
#include <linux/platform_data/spi-omap2-mcspi.h>
#include <linux/omap-dma.h>
#include <plat/dmtimer.h>

#include "omap_hwmod.h"
#include "mmc.h"
#include "l3_2xxx.h"

#include "soc.h"
#include "omap_hwmod_common_data.h"
#include "prm-regbits-24xx.h"
#include "cm-regbits-24xx.h"
#include "i2c.h"
#include "wd_timer.h"

/*
 * OMAP2430 hardware module integration data
 *
 * All of the data in this section should be autogeneratable from the
 * TI hardware database or other technical documentation.  Data that
 * is driver-specific or driver-kernel integration-specific belongs
 * elsewhere.
 */

/*
 * IP blocks
 */

/* IVA2 (IVA2) */
static struct omap_hwmod_rst_info omap2430_iva_resets[] = {
	{ .name = "logic", .rst_shift = 0 },
	{ .name = "mmu", .rst_shift = 1 },
};

static struct omap_hwmod omap2430_iva_hwmod = {
	.name		= "iva",
	.class		= &iva_hwmod_class,
	.clkdm_name	= "dsp_clkdm",
	.rst_lines	= omap2430_iva_resets,
	.rst_lines_cnt	= ARRAY_SIZE(omap2430_iva_resets),
	.main_clk	= "dsp_fck",
};

/* I2C common */
static struct omap_hwmod_class_sysconfig i2c_sysc = {
	.rev_offs	= 0x00,
	.sysc_offs	= 0x20,
	.syss_offs	= 0x10,
	.sysc_flags	= (SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class i2c_class = {
	.name		= "i2c",
	.sysc		= &i2c_sysc,
	.rev		= OMAP_I2C_IP_VERSION_1,
	.reset		= &omap_i2c_reset,
};

static struct omap_i2c_dev_attr i2c_dev_attr = {
	.fifo_depth	= 8, /* bytes */
	.flags		= OMAP_I2C_FLAG_BUS_SHIFT_2 |
			  OMAP_I2C_FLAG_FORCE_19200_INT_CLK,
};

/* I2C1 */
static struct omap_hwmod omap2430_i2c1_hwmod = {
	.name		= "i2c1",
	.flags		= HWMOD_16BIT_REG,
	.mpu_irqs	= omap2_i2c1_mpu_irqs,
	.sdma_reqs	= omap2_i2c1_sdma_reqs,
	.main_clk	= "i2chs1_fck",
	.prcm		= {
		.omap2 = {
			/*
			 * NOTE: The CM_FCLKEN* and CM_ICLKEN* for
			 * I2CHS IP's do not follow the usual pattern.
			 * prcm_reg_id alone cannot be used to program
			 * the iclk and fclk. Needs to be handled using
			 * additional flags when clk handling is moved
			 * to hwmod framework.
			 */
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_I2CHS1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP2430_ST_I2CHS1_SHIFT,
		},
	},
	.class		= &i2c_class,
	.dev_attr	= &i2c_dev_attr,
};

/* I2C2 */
static struct omap_hwmod omap2430_i2c2_hwmod = {
	.name		= "i2c2",
	.flags		= HWMOD_16BIT_REG,
	.mpu_irqs	= omap2_i2c2_mpu_irqs,
	.sdma_reqs	= omap2_i2c2_sdma_reqs,
	.main_clk	= "i2chs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_I2CHS2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP2430_ST_I2CHS2_SHIFT,
		},
	},
	.class		= &i2c_class,
	.dev_attr	= &i2c_dev_attr,
};

/* gpio5 */
static struct omap_hwmod_irq_info omap243x_gpio5_irqs[] = {
	{ .irq = 33 + OMAP_INTC_START, }, /* INT_24XX_GPIO_BANK5 */
	{ .irq = -1 },
};

static struct omap_hwmod omap2430_gpio5_hwmod = {
	.name		= "gpio5",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap243x_gpio5_irqs,
	.main_clk	= "gpio5_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 2,
			.module_bit = OMAP2430_EN_GPIO5_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_GPIO5_SHIFT,
		},
	},
	.class		= &omap2xxx_gpio_hwmod_class,
	.dev_attr	= &omap2xxx_gpio_dev_attr,
};

/* dma attributes */
static struct omap_dma_dev_attr dma_dev_attr = {
	.dev_caps  = RESERVE_CHANNEL | DMA_LINKED_LCH | GLOBAL_PRIORITY |
				IS_CSSA_32 | IS_CDSA_32 | IS_RW_PRIORITY,
	.lch_count = 32,
};

static struct omap_hwmod omap2430_dma_system_hwmod = {
	.name		= "dma",
	.class		= &omap2xxx_dma_hwmod_class,
	.mpu_irqs	= omap2_dma_system_irqs,
	.main_clk	= "core_l3_ck",
	.dev_attr	= &dma_dev_attr,
	.flags		= HWMOD_NO_IDLEST,
};

/* mailbox */
static struct omap_hwmod_irq_info omap2430_mailbox_irqs[] = {
	{ .irq = 26 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap2430_mailbox_hwmod = {
	.name		= "mailbox",
	.class		= &omap2xxx_mailbox_hwmod_class,
	.mpu_irqs	= omap2430_mailbox_irqs,
	.main_clk	= "mailboxes_ick",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MAILBOXES_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MAILBOXES_SHIFT,
		},
	},
};

/* mcspi3 */
static struct omap_hwmod_irq_info omap2430_mcspi3_mpu_irqs[] = {
	{ .irq = 91 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info omap2430_mcspi3_sdma_reqs[] = {
	{ .name = "tx0", .dma_req = 15 }, /* DMA_SPI3_TX0 */
	{ .name = "rx0", .dma_req = 16 }, /* DMA_SPI3_RX0 */
	{ .name = "tx1", .dma_req = 23 }, /* DMA_SPI3_TX1 */
	{ .name = "rx1", .dma_req = 24 }, /* DMA_SPI3_RX1 */
	{ .dma_req = -1 }
};

static struct omap2_mcspi_dev_attr omap_mcspi3_dev_attr = {
	.num_chipselect = 2,
};

static struct omap_hwmod omap2430_mcspi3_hwmod = {
	.name		= "mcspi3",
	.mpu_irqs	= omap2430_mcspi3_mpu_irqs,
	.sdma_reqs	= omap2430_mcspi3_sdma_reqs,
	.main_clk	= "mcspi3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 2,
			.module_bit = OMAP2430_EN_MCSPI3_SHIFT,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCSPI3_SHIFT,
		},
	},
	.class		= &omap2xxx_mcspi_class,
	.dev_attr	= &omap_mcspi3_dev_attr,
};

/* usbhsotg */
static struct omap_hwmod_class_sysconfig omap2430_usbhsotg_sysc = {
	.rev_offs	= 0x0400,
	.sysc_offs	= 0x0404,
	.syss_offs	= 0x0408,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE|
			  SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			  SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			  MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class usbotg_class = {
	.name = "usbotg",
	.sysc = &omap2430_usbhsotg_sysc,
};

/* usb_otg_hs */
static struct omap_hwmod_irq_info omap2430_usbhsotg_mpu_irqs[] = {

	{ .name = "mc", .irq = 92 + OMAP_INTC_START, },
	{ .name = "dma", .irq = 93 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap2430_usbhsotg_hwmod = {
	.name		= "usb_otg_hs",
	.mpu_irqs	= omap2430_usbhsotg_mpu_irqs,
	.main_clk	= "usbhs_ick",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_USBHS_MASK,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP2430_ST_USBHS_SHIFT,
		},
	},
	.class		= &usbotg_class,
	/*
	 * Erratum ID: i479  idle_req / idle_ack mechanism potentially
	 * broken when autoidle is enabled
	 * workaround is to disable the autoidle bit at module level.
	 */
	.flags		= HWMOD_NO_OCP_AUTOIDLE | HWMOD_SWSUP_SIDLE
				| HWMOD_SWSUP_MSTANDBY,
};

/*
 * 'mcbsp' class
 * multi channel buffered serial port controller
 */

static struct omap_hwmod_class_sysconfig omap2430_mcbsp_sysc = {
	.rev_offs	= 0x007C,
	.sysc_offs	= 0x008C,
	.sysc_flags	= (SYSC_HAS_SOFTRESET),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_mcbsp_hwmod_class = {
	.name = "mcbsp",
	.sysc = &omap2430_mcbsp_sysc,
	.rev  = MCBSP_CONFIG_TYPE2,
};

static struct omap_hwmod_opt_clk mcbsp_opt_clks[] = {
	{ .role = "pad_fck", .clk = "mcbsp_clks" },
	{ .role = "prcm_fck", .clk = "func_96m_ck" },
};

/* mcbsp1 */
static struct omap_hwmod_irq_info omap2430_mcbsp1_irqs[] = {
	{ .name = "tx",		.irq = 59 + OMAP_INTC_START, },
	{ .name = "rx",		.irq = 60 + OMAP_INTC_START, },
	{ .name = "ovr",	.irq = 61 + OMAP_INTC_START, },
	{ .name = "common",	.irq = 64 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap2430_mcbsp1_hwmod = {
	.name		= "mcbsp1",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp1_irqs,
	.sdma_reqs	= omap2_mcbsp1_sdma_reqs,
	.main_clk	= "mcbsp1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MCBSP1_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCBSP1_SHIFT,
		},
	},
	.opt_clks	= mcbsp_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp_opt_clks),
};

/* mcbsp2 */
static struct omap_hwmod_irq_info omap2430_mcbsp2_irqs[] = {
	{ .name = "tx",		.irq = 62 + OMAP_INTC_START, },
	{ .name = "rx",		.irq = 63 + OMAP_INTC_START, },
	{ .name = "common",	.irq = 16 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap2430_mcbsp2_hwmod = {
	.name		= "mcbsp2",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp2_irqs,
	.sdma_reqs	= omap2_mcbsp2_sdma_reqs,
	.main_clk	= "mcbsp2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MCBSP2_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCBSP2_SHIFT,
		},
	},
	.opt_clks	= mcbsp_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp_opt_clks),
};

/* mcbsp3 */
static struct omap_hwmod_irq_info omap2430_mcbsp3_irqs[] = {
	{ .name = "tx",		.irq = 89 + OMAP_INTC_START, },
	{ .name = "rx",		.irq = 90 + OMAP_INTC_START, },
	{ .name = "common",	.irq = 17 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap2430_mcbsp3_hwmod = {
	.name		= "mcbsp3",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp3_irqs,
	.sdma_reqs	= omap2_mcbsp3_sdma_reqs,
	.main_clk	= "mcbsp3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_MCBSP3_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCBSP3_SHIFT,
		},
	},
	.opt_clks	= mcbsp_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp_opt_clks),
};

/* mcbsp4 */
static struct omap_hwmod_irq_info omap2430_mcbsp4_irqs[] = {
	{ .name = "tx",		.irq = 54 + OMAP_INTC_START, },
	{ .name = "rx",		.irq = 55 + OMAP_INTC_START, },
	{ .name = "common",	.irq = 18 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info omap2430_mcbsp4_sdma_chs[] = {
	{ .name = "rx", .dma_req = 20 },
	{ .name = "tx", .dma_req = 19 },
	{ .dma_req = -1 }
};

static struct omap_hwmod omap2430_mcbsp4_hwmod = {
	.name		= "mcbsp4",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp4_irqs,
	.sdma_reqs	= omap2430_mcbsp4_sdma_chs,
	.main_clk	= "mcbsp4_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_MCBSP4_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCBSP4_SHIFT,
		},
	},
	.opt_clks	= mcbsp_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp_opt_clks),
};

/* mcbsp5 */
static struct omap_hwmod_irq_info omap2430_mcbsp5_irqs[] = {
	{ .name = "tx",		.irq = 81 + OMAP_INTC_START, },
	{ .name = "rx",		.irq = 82 + OMAP_INTC_START, },
	{ .name = "common",	.irq = 19 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info omap2430_mcbsp5_sdma_chs[] = {
	{ .name = "rx", .dma_req = 22 },
	{ .name = "tx", .dma_req = 21 },
	{ .dma_req = -1 }
};

static struct omap_hwmod omap2430_mcbsp5_hwmod = {
	.name		= "mcbsp5",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp5_irqs,
	.sdma_reqs	= omap2430_mcbsp5_sdma_chs,
	.main_clk	= "mcbsp5_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_MCBSP5_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCBSP5_SHIFT,
		},
	},
	.opt_clks	= mcbsp_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp_opt_clks),
};

/* MMC/SD/SDIO common */
static struct omap_hwmod_class_sysconfig omap2430_mmc_sysc = {
	.rev_offs	= 0x1fc,
	.sysc_offs	= 0x10,
	.syss_offs	= 0x14,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_mmc_class = {
	.name = "mmc",
	.sysc = &omap2430_mmc_sysc,
};

/* MMC/SD/SDIO1 */
static struct omap_hwmod_irq_info omap2430_mmc1_mpu_irqs[] = {
	{ .irq = 83 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info omap2430_mmc1_sdma_reqs[] = {
	{ .name = "tx",	.dma_req = 61 }, /* DMA_MMC1_TX */
	{ .name = "rx",	.dma_req = 62 }, /* DMA_MMC1_RX */
	{ .dma_req = -1 }
};

static struct omap_hwmod_opt_clk omap2430_mmc1_opt_clks[] = {
	{ .role = "dbck", .clk = "mmchsdb1_fck" },
};

static struct omap_mmc_dev_attr mmc1_dev_attr = {
	.flags = OMAP_HSMMC_SUPPORTS_DUAL_VOLT,
};

static struct omap_hwmod omap2430_mmc1_hwmod = {
	.name		= "mmc1",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2430_mmc1_mpu_irqs,
	.sdma_reqs	= omap2430_mmc1_sdma_reqs,
	.opt_clks	= omap2430_mmc1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap2430_mmc1_opt_clks),
	.main_clk	= "mmchs1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 2,
			.module_bit  = OMAP2430_EN_MMCHS1_SHIFT,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MMCHS1_SHIFT,
		},
	},
	.dev_attr	= &mmc1_dev_attr,
	.class		= &omap2430_mmc_class,
};

/* MMC/SD/SDIO2 */
static struct omap_hwmod_irq_info omap2430_mmc2_mpu_irqs[] = {
	{ .irq = 86 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info omap2430_mmc2_sdma_reqs[] = {
	{ .name = "tx",	.dma_req = 47 }, /* DMA_MMC2_TX */
	{ .name = "rx",	.dma_req = 48 }, /* DMA_MMC2_RX */
	{ .dma_req = -1 }
};

static struct omap_hwmod_opt_clk omap2430_mmc2_opt_clks[] = {
	{ .role = "dbck", .clk = "mmchsdb2_fck" },
};

static struct omap_hwmod omap2430_mmc2_hwmod = {
	.name		= "mmc2",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2430_mmc2_mpu_irqs,
	.sdma_reqs	= omap2430_mmc2_sdma_reqs,
	.opt_clks	= omap2430_mmc2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap2430_mmc2_opt_clks),
	.main_clk	= "mmchs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 2,
			.module_bit  = OMAP2430_EN_MMCHS2_SHIFT,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MMCHS2_SHIFT,
		},
	},
	.class		= &omap2430_mmc_class,
};

/* HDQ1W/1-wire */
static struct omap_hwmod omap2430_hdq1w_hwmod = {
	.name		= "hdq1w",
	.mpu_irqs	= omap2_hdq1w_mpu_irqs,
	.main_clk	= "hdq_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_HDQ_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_HDQ_SHIFT,
		},
	},
	.class		= &omap2_hdq1w_class,
};

/*
 * interfaces
 */

/* L3 -> L4_CORE interface */
/* l3_core -> usbhsotg  interface */
static struct omap_hwmod_ocp_if omap2430_usbhsotg__l3 = {
	.master		= &omap2430_usbhsotg_hwmod,
	.slave		= &omap2xxx_l3_main_hwmod,
	.clk		= "core_l3_ck",
	.user		= OCP_USER_MPU,
};

/* L4 CORE -> I2C1 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__i2c1 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_i2c1_hwmod,
	.clk		= "i2c1_ick",
	.addr		= omap2_i2c1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C2 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__i2c2 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_i2c2_hwmod,
	.clk		= "i2c2_ick",
	.addr		= omap2_i2c2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap2430_usbhsotg_addrs[] = {
	{
		.pa_start	= OMAP243X_HS_BASE,
		.pa_end		= OMAP243X_HS_BASE + SZ_4K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/*  l4_core ->usbhsotg  interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__usbhsotg = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_usbhsotg_hwmod,
	.clk		= "usb_l4_ick",
	.addr		= omap2430_usbhsotg_addrs,
	.user		= OCP_USER_MPU,
};

/* L4 CORE -> MMC1 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__mmc1 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mmc1_hwmod,
	.clk		= "mmchs1_ick",
	.addr		= omap2430_mmc1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> MMC2 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__mmc2 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mmc2_hwmod,
	.clk		= "mmchs2_ick",
	.addr		= omap2430_mmc2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi3 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcspi3 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mcspi3_hwmod,
	.clk		= "mcspi3_ick",
	.addr		= omap2430_mcspi3_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* IVA2 <- L3 interface */
static struct omap_hwmod_ocp_if omap2430_l3__iva = {
	.master		= &omap2xxx_l3_main_hwmod,
	.slave		= &omap2430_iva_hwmod,
	.clk		= "core_l3_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap2430_timer1_addrs[] = {
	{
		.pa_start	= 0x49018000,
		.pa_end		= 0x49018000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_wkup -> timer1 */
static struct omap_hwmod_ocp_if omap2430_l4_wkup__timer1 = {
	.master		= &omap2xxx_l4_wkup_hwmod,
	.slave		= &omap2xxx_timer1_hwmod,
	.clk		= "gpt1_ick",
	.addr		= omap2430_timer1_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> wd_timer2 */
static struct omap_hwmod_addr_space omap2430_wd_timer2_addrs[] = {
	{
		.pa_start	= 0x49016000,
		.pa_end		= 0x4901607f,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__wd_timer2 = {
	.master		= &omap2xxx_l4_wkup_hwmod,
	.slave		= &omap2xxx_wd_timer2_hwmod,
	.clk		= "mpu_wdt_ick",
	.addr		= omap2430_wd_timer2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio1 */
static struct omap_hwmod_addr_space omap2430_gpio1_addr_space[] = {
	{
		.pa_start	= 0x4900C000,
		.pa_end		= 0x4900C1ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio1 = {
	.master		= &omap2xxx_l4_wkup_hwmod,
	.slave		= &omap2xxx_gpio1_hwmod,
	.clk		= "gpios_ick",
	.addr		= omap2430_gpio1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio2 */
static struct omap_hwmod_addr_space omap2430_gpio2_addr_space[] = {
	{
		.pa_start	= 0x4900E000,
		.pa_end		= 0x4900E1ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio2 = {
	.master		= &omap2xxx_l4_wkup_hwmod,
	.slave		= &omap2xxx_gpio2_hwmod,
	.clk		= "gpios_ick",
	.addr		= omap2430_gpio2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio3 */
static struct omap_hwmod_addr_space omap2430_gpio3_addr_space[] = {
	{
		.pa_start	= 0x49010000,
		.pa_end		= 0x490101ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio3 = {
	.master		= &omap2xxx_l4_wkup_hwmod,
	.slave		= &omap2xxx_gpio3_hwmod,
	.clk		= "gpios_ick",
	.addr		= omap2430_gpio3_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio4 */
static struct omap_hwmod_addr_space omap2430_gpio4_addr_space[] = {
	{
		.pa_start	= 0x49012000,
		.pa_end		= 0x490121ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio4 = {
	.master		= &omap2xxx_l4_wkup_hwmod,
	.slave		= &omap2xxx_gpio4_hwmod,
	.clk		= "gpios_ick",
	.addr		= omap2430_gpio4_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> gpio5 */
static struct omap_hwmod_addr_space omap2430_gpio5_addr_space[] = {
	{
		.pa_start	= 0x480B6000,
		.pa_end		= 0x480B61ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_core__gpio5 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_gpio5_hwmod,
	.clk		= "gpio5_ick",
	.addr		= omap2430_gpio5_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dma_system -> L3 */
static struct omap_hwmod_ocp_if omap2430_dma_system__l3 = {
	.master		= &omap2430_dma_system_hwmod,
	.slave		= &omap2xxx_l3_main_hwmod,
	.clk		= "core_l3_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dma_system */
static struct omap_hwmod_ocp_if omap2430_l4_core__dma_system = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_dma_system_hwmod,
	.clk		= "sdma_ick",
	.addr		= omap2_dma_system_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> mailbox */
static struct omap_hwmod_ocp_if omap2430_l4_core__mailbox = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mailbox_hwmod,
	.addr		= omap2_mailbox_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> mcbsp1 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp1 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mcbsp1_hwmod,
	.clk		= "mcbsp1_ick",
	.addr		= omap2_mcbsp1_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> mcbsp2 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp2 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mcbsp2_hwmod,
	.clk		= "mcbsp2_ick",
	.addr		= omap2xxx_mcbsp2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap2430_mcbsp3_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x4808C000,
		.pa_end		= 0x4808C0ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> mcbsp3 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp3 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mcbsp3_hwmod,
	.clk		= "mcbsp3_ick",
	.addr		= omap2430_mcbsp3_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap2430_mcbsp4_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x4808E000,
		.pa_end		= 0x4808E0ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> mcbsp4 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp4 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mcbsp4_hwmod,
	.clk		= "mcbsp4_ick",
	.addr		= omap2430_mcbsp4_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap2430_mcbsp5_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x48096000,
		.pa_end		= 0x480960ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> mcbsp5 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp5 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mcbsp5_hwmod,
	.clk		= "mcbsp5_ick",
	.addr		= omap2430_mcbsp5_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> hdq1w */
static struct omap_hwmod_ocp_if omap2430_l4_core__hdq1w = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_hdq1w_hwmod,
	.clk		= "hdq_ick",
	.addr		= omap2_hdq1w_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4 | OCPIF_SWSUP_IDLE,
};

/* l4_wkup -> 32ksync_counter */
static struct omap_hwmod_addr_space omap2430_counter_32k_addrs[] = {
	{
		.pa_start	= 0x49020000,
		.pa_end		= 0x4902001f,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_addr_space omap2430_gpmc_addrs[] = {
	{
		.pa_start	= 0x6e000000,
		.pa_end		= 0x6e000fff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__counter_32k = {
	.master		= &omap2xxx_l4_wkup_hwmod,
	.slave		= &omap2xxx_counter_32k_hwmod,
	.clk		= "sync_32k_ick",
	.addr		= omap2430_counter_32k_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if omap2430_l3__gpmc = {
	.master		= &omap2xxx_l3_main_hwmod,
	.slave		= &omap2xxx_gpmc_hwmod,
	.clk		= "core_l3_ck",
	.addr		= omap2430_gpmc_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if *omap2430_hwmod_ocp_ifs[] __initdata = {
	&omap2xxx_l3_main__l4_core,
	&omap2xxx_mpu__l3_main,
	&omap2xxx_dss__l3,
	&omap2430_usbhsotg__l3,
	&omap2430_l4_core__i2c1,
	&omap2430_l4_core__i2c2,
	&omap2xxx_l4_core__l4_wkup,
	&omap2_l4_core__uart1,
	&omap2_l4_core__uart2,
	&omap2_l4_core__uart3,
	&omap2430_l4_core__usbhsotg,
	&omap2430_l4_core__mmc1,
	&omap2430_l4_core__mmc2,
	&omap2xxx_l4_core__mcspi1,
	&omap2xxx_l4_core__mcspi2,
	&omap2430_l4_core__mcspi3,
	&omap2430_l3__iva,
	&omap2430_l4_wkup__timer1,
	&omap2xxx_l4_core__timer2,
	&omap2xxx_l4_core__timer3,
	&omap2xxx_l4_core__timer4,
	&omap2xxx_l4_core__timer5,
	&omap2xxx_l4_core__timer6,
	&omap2xxx_l4_core__timer7,
	&omap2xxx_l4_core__timer8,
	&omap2xxx_l4_core__timer9,
	&omap2xxx_l4_core__timer10,
	&omap2xxx_l4_core__timer11,
	&omap2xxx_l4_core__timer12,
	&omap2430_l4_wkup__wd_timer2,
	&omap2xxx_l4_core__dss,
	&omap2xxx_l4_core__dss_dispc,
	&omap2xxx_l4_core__dss_rfbi,
	&omap2xxx_l4_core__dss_venc,
	&omap2430_l4_wkup__gpio1,
	&omap2430_l4_wkup__gpio2,
	&omap2430_l4_wkup__gpio3,
	&omap2430_l4_wkup__gpio4,
	&omap2430_l4_core__gpio5,
	&omap2430_dma_system__l3,
	&omap2430_l4_core__dma_system,
	&omap2430_l4_core__mailbox,
	&omap2430_l4_core__mcbsp1,
	&omap2430_l4_core__mcbsp2,
	&omap2430_l4_core__mcbsp3,
	&omap2430_l4_core__mcbsp4,
	&omap2430_l4_core__mcbsp5,
	&omap2430_l4_core__hdq1w,
	&omap2xxx_l4_core__rng,
	&omap2430_l4_wkup__counter_32k,
	&omap2430_l3__gpmc,
	NULL,
};

int __init omap2430_hwmod_init(void)
{
	omap_hwmod_init();
	return omap_hwmod_register_links(omap2430_hwmod_ocp_ifs);
}
