/*
 * Copyright (C) 2013 Texas Instruments Incorporated
 *
 * Hwmod present only in AM43x and those that differ other than register
 * offsets as compared to AM335x.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "omap_hwmod.h"
#include "omap_hwmod_33xx_43xx_common_data.h"
#include "prcm43xx.h"
#include "omap_hwmod_common_data.h"
#include "hdq1w.h"


/* IP blocks */
static struct omap_hwmod am43xx_emif_hwmod = {
	.name		= "emif",
	.class		= &am33xx_emif_hwmod_class,
	.clkdm_name	= "emif_clkdm",
	.flags		= HWMOD_INIT_NO_IDLE,
	.main_clk	= "dpll_ddr_m2_ck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= AM43XX_CM_PER_EMIF_CLKCTRL_OFFSET,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_l4_hs_hwmod = {
	.name		= "l4_hs",
	.class		= &am33xx_l4_hwmod_class,
	.clkdm_name	= "l3_clkdm",
	.flags		= HWMOD_INIT_NO_IDLE,
	.main_clk	= "l4hs_gclk",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= AM43XX_CM_PER_L4HS_CLKCTRL_OFFSET,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_rst_info am33xx_wkup_m3_resets[] = {
	{ .name = "wkup_m3", .rst_shift = 3, .st_shift = 5 },
};

static struct omap_hwmod am43xx_wkup_m3_hwmod = {
	.name		= "wkup_m3",
	.class		= &am33xx_wkup_m3_hwmod_class,
	.clkdm_name	= "l4_wkup_aon_clkdm",
	/* Keep hardreset asserted */
	.flags		= HWMOD_INIT_NO_RESET | HWMOD_NO_IDLEST,
	.main_clk	= "sys_clkin_ck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= AM43XX_CM_WKUP_WKUP_M3_CLKCTRL_OFFSET,
			.rstctrl_offs	= AM43XX_RM_WKUP_RSTCTRL_OFFSET,
			.rstst_offs	= AM43XX_RM_WKUP_RSTST_OFFSET,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.rst_lines	= am33xx_wkup_m3_resets,
	.rst_lines_cnt	= ARRAY_SIZE(am33xx_wkup_m3_resets),
};

static struct omap_hwmod am43xx_control_hwmod = {
	.name		= "control",
	.class		= &am33xx_control_hwmod_class,
	.clkdm_name	= "l4_wkup_clkdm",
	.flags		= HWMOD_INIT_NO_IDLE,
	.main_clk	= "sys_clkin_ck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= AM43XX_CM_WKUP_CONTROL_CLKCTRL_OFFSET,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_class_sysconfig am43xx_synctimer_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x4,
	.sysc_flags	= SYSC_HAS_SIDLEMODE,
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class am43xx_synctimer_hwmod_class = {
	.name	= "synctimer",
	.sysc	= &am43xx_synctimer_sysc,
};

static struct omap_hwmod am43xx_synctimer_hwmod = {
	.name		= "counter_32k",
	.class		= &am43xx_synctimer_hwmod_class,
	.clkdm_name	= "l4_wkup_aon_clkdm",
	.flags		= HWMOD_SWSUP_SIDLE,
	.main_clk	= "synctimer_32kclk",
	.prcm = {
		.omap4 = {
			.clkctrl_offs = AM43XX_CM_WKUP_SYNCTIMER_CLKCTRL_OFFSET,
			.modulemode   = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_timer8_hwmod = {
	.name		= "timer8",
	.class		= &am33xx_timer_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "timer8_fck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= AM43XX_CM_PER_TIMER8_CLKCTRL_OFFSET,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_timer9_hwmod = {
	.name		= "timer9",
	.class		= &am33xx_timer_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "timer9_fck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= AM43XX_CM_PER_TIMER9_CLKCTRL_OFFSET,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_timer10_hwmod = {
	.name		= "timer10",
	.class		= &am33xx_timer_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "timer10_fck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= AM43XX_CM_PER_TIMER10_CLKCTRL_OFFSET,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_timer11_hwmod = {
	.name		= "timer11",
	.class		= &am33xx_timer_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "timer11_fck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= AM43XX_CM_PER_TIMER11_CLKCTRL_OFFSET,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_epwmss3_hwmod = {
	.name		= "epwmss3",
	.class		= &am33xx_epwmss_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs = AM43XX_CM_PER_EPWMSS3_CLKCTRL_OFFSET,
			.modulemode   = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_epwmss4_hwmod = {
	.name		= "epwmss4",
	.class		= &am33xx_epwmss_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs = AM43XX_CM_PER_EPWMSS4_CLKCTRL_OFFSET,
			.modulemode   = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_epwmss5_hwmod = {
	.name		= "epwmss5",
	.class		= &am33xx_epwmss_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs = AM43XX_CM_PER_EPWMSS5_CLKCTRL_OFFSET,
			.modulemode   = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_spi2_hwmod = {
	.name		= "spi2",
	.class		= &am33xx_spi_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "dpll_per_m2_div4_ck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs = AM43XX_CM_PER_SPI2_CLKCTRL_OFFSET,
			.modulemode   = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_spi3_hwmod = {
	.name		= "spi3",
	.class		= &am33xx_spi_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "dpll_per_m2_div4_ck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs = AM43XX_CM_PER_SPI3_CLKCTRL_OFFSET,
			.modulemode   = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_spi4_hwmod = {
	.name		= "spi4",
	.class		= &am33xx_spi_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "dpll_per_m2_div4_ck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs = AM43XX_CM_PER_SPI4_CLKCTRL_OFFSET,
			.modulemode   = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_class am43xx_ocp2scp_hwmod_class = {
	.name	= "ocp2scp",
};

static struct omap_hwmod am43xx_ocp2scp0_hwmod = {
	.name		= "ocp2scp0",
	.class		= &am43xx_ocp2scp_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
	.prcm = {
		.omap4 = {
			.clkctrl_offs = AM43XX_CM_PER_USBPHYOCP2SCP0_CLKCTRL_OFFSET,
			.modulemode   = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_ocp2scp1_hwmod = {
	.name		= "ocp2scp1",
	.class		= &am43xx_ocp2scp_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
	.prcm = {
		.omap4 = {
			.clkctrl_offs	= AM43XX_CM_PER_USBPHYOCP2SCP1_CLKCTRL_OFFSET,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_class_sysconfig am43xx_usb_otg_ss_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.sysc_flags	= (SYSC_HAS_DMADISABLE | SYSC_HAS_MIDLEMODE |
				SYSC_HAS_SIDLEMODE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
				SIDLE_SMART_WKUP | MSTANDBY_FORCE |
				MSTANDBY_NO | MSTANDBY_SMART |
				MSTANDBY_SMART_WKUP),
	.sysc_fields	= &omap_hwmod_sysc_type2,
};

static struct omap_hwmod_class am43xx_usb_otg_ss_hwmod_class = {
	.name	= "usb_otg_ss",
	.sysc	= &am43xx_usb_otg_ss_sysc,
};

static struct omap_hwmod am43xx_usb_otg_ss0_hwmod = {
	.name		= "usb_otg_ss0",
	.class		= &am43xx_usb_otg_ss_hwmod_class,
	.clkdm_name	= "l3s_clkdm",
	.main_clk	= "l3s_gclk",
	.prcm = {
		.omap4 = {
			.clkctrl_offs	= AM43XX_CM_PER_USB_OTG_SS0_CLKCTRL_OFFSET,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod am43xx_usb_otg_ss1_hwmod = {
	.name		= "usb_otg_ss1",
	.class		= &am43xx_usb_otg_ss_hwmod_class,
	.clkdm_name	= "l3s_clkdm",
	.main_clk	= "l3s_gclk",
	.prcm = {
		.omap4 = {
			.clkctrl_offs	= AM43XX_CM_PER_USB_OTG_SS1_CLKCTRL_OFFSET,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_class_sysconfig am43xx_qspi_sysc = {
	.rev_offs	= 0,
	.sysc_offs      = 0x0010,
	.sysc_flags     = SYSC_HAS_SIDLEMODE,
	.idlemodes      = (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
				SIDLE_SMART_WKUP),
	.sysc_fields    = &omap_hwmod_sysc_type2,
};

static struct omap_hwmod_class am43xx_qspi_hwmod_class = {
	.name   = "qspi",
	.sysc   = &am43xx_qspi_sysc,
};

static struct omap_hwmod am43xx_qspi_hwmod = {
	.name           = "qspi",
	.class          = &am43xx_qspi_hwmod_class,
	.clkdm_name     = "l3s_clkdm",
	.main_clk       = "l3s_gclk",
	.prcm = {
		.omap4 = {
			.clkctrl_offs = AM43XX_CM_PER_QSPI_CLKCTRL_OFFSET,
			.modulemode   = MODULEMODE_SWCTRL,
		},
	},
};

/*
 * 'adc/tsc' class
 * TouchScreen Controller (Analog-To-Digital Converter)
 */
static struct omap_hwmod_class_sysconfig am43xx_adc_tsc_sysc = {
	.rev_offs	= 0x00,
	.sysc_offs	= 0x10,
	.sysc_flags	= SYSC_HAS_SIDLEMODE,
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			  SIDLE_SMART_WKUP),
	.sysc_fields	= &omap_hwmod_sysc_type2,
};

static struct omap_hwmod_class am43xx_adc_tsc_hwmod_class = {
	.name		= "adc_tsc",
	.sysc		= &am43xx_adc_tsc_sysc,
};

static struct omap_hwmod am43xx_adc_tsc_hwmod = {
	.name		= "adc_tsc",
	.class		= &am43xx_adc_tsc_hwmod_class,
	.clkdm_name	= "l3s_tsc_clkdm",
	.main_clk	= "adc_tsc_fck",
	.prcm		= {
		.omap4  = {
			.clkctrl_offs   = AM43XX_CM_WKUP_ADC_TSC_CLKCTRL_OFFSET,
			.modulemode     = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_class_sysconfig am43xx_des_sysc = {
	.rev_offs	= 0x30,
	.sysc_offs	= 0x34,
	.syss_offs	= 0x38,
	.sysc_flags	= SYSS_HAS_RESET_STATUS,
};

static struct omap_hwmod_class am43xx_des_hwmod_class = {
	.name		= "des",
	.sysc		= &am43xx_des_sysc,
};

static struct omap_hwmod am43xx_des_hwmod = {
	.name		= "des",
	.class		= &am43xx_des_hwmod_class,
	.clkdm_name	= "l3_clkdm",
	.main_clk	= "l3_gclk",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= AM43XX_CM_PER_DES_CLKCTRL_OFFSET,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* dss */

static struct omap_hwmod am43xx_dss_core_hwmod = {
	.name		= "dss_core",
	.class		= &omap2_dss_hwmod_class,
	.clkdm_name	= "dss_clkdm",
	.main_clk	= "disp_clk",
	.prcm = {
		.omap4 = {
			.clkctrl_offs = AM43XX_CM_PER_DSS_CLKCTRL_OFFSET,
			.modulemode   = MODULEMODE_SWCTRL,
		},
	},
};

/* dispc */

static struct omap_dss_dispc_dev_attr am43xx_dss_dispc_dev_attr = {
	.manager_count		= 1,
	.has_framedonetv_irq	= 0
};

static struct omap_hwmod_class_sysconfig am43xx_dispc_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_AUTOIDLE | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_MIDLEMODE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			   MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class am43xx_dispc_hwmod_class = {
	.name	= "dispc",
	.sysc	= &am43xx_dispc_sysc,
};

static struct omap_hwmod am43xx_dss_dispc_hwmod = {
	.name		= "dss_dispc",
	.class		= &am43xx_dispc_hwmod_class,
	.clkdm_name	= "dss_clkdm",
	.main_clk	= "disp_clk",
	.prcm = {
		.omap4 = {
			.clkctrl_offs = AM43XX_CM_PER_DSS_CLKCTRL_OFFSET,
		},
	},
	.dev_attr	= &am43xx_dss_dispc_dev_attr,
	.parent_hwmod	= &am43xx_dss_core_hwmod,
};

/* rfbi */

static struct omap_hwmod am43xx_dss_rfbi_hwmod = {
	.name		= "dss_rfbi",
	.class		= &omap2_rfbi_hwmod_class,
	.clkdm_name	= "dss_clkdm",
	.main_clk	= "disp_clk",
	.prcm = {
		.omap4 = {
			.clkctrl_offs = AM43XX_CM_PER_DSS_CLKCTRL_OFFSET,
		},
	},
	.parent_hwmod	= &am43xx_dss_core_hwmod,
};

/* HDQ1W */
static struct omap_hwmod_class_sysconfig am43xx_hdq1w_sysc = {
	.rev_offs       = 0x0000,
	.sysc_offs      = 0x0014,
	.syss_offs      = 0x0018,
	.sysc_flags     = (SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class am43xx_hdq1w_hwmod_class = {
	.name   = "hdq1w",
	.sysc   = &am43xx_hdq1w_sysc,
	.reset	= &omap_hdq1w_reset,
};

static struct omap_hwmod am43xx_hdq1w_hwmod = {
	.name           = "hdq1w",
	.class          = &am43xx_hdq1w_hwmod_class,
	.clkdm_name     = "l4ls_clkdm",
	.prcm = {
		.omap4 = {
			.clkctrl_offs = AM43XX_CM_PER_HDQ1W_CLKCTRL_OFFSET,
			.modulemode   = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_class_sysconfig am43xx_vpfe_sysc = {
	.rev_offs       = 0x0,
	.sysc_offs      = 0x104,
	.sysc_flags     = SYSC_HAS_MIDLEMODE | SYSC_HAS_SIDLEMODE,
	.idlemodes      = (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
				MSTANDBY_FORCE | MSTANDBY_SMART | MSTANDBY_NO),
	.sysc_fields    = &omap_hwmod_sysc_type2,
};

static struct omap_hwmod_class am43xx_vpfe_hwmod_class = {
	.name           = "vpfe",
	.sysc           = &am43xx_vpfe_sysc,
};

static struct omap_hwmod am43xx_vpfe0_hwmod = {
	.name           = "vpfe0",
	.class          = &am43xx_vpfe_hwmod_class,
	.clkdm_name     = "l3s_clkdm",
	.prcm           = {
		.omap4  = {
			.modulemode     = MODULEMODE_SWCTRL,
			.clkctrl_offs   = AM43XX_CM_PER_VPFE0_CLKCTRL_OFFSET,
		},
	},
};

static struct omap_hwmod am43xx_vpfe1_hwmod = {
	.name           = "vpfe1",
	.class          = &am43xx_vpfe_hwmod_class,
	.clkdm_name     = "l3s_clkdm",
	.prcm           = {
		.omap4  = {
			.modulemode     = MODULEMODE_SWCTRL,
			.clkctrl_offs   = AM43XX_CM_PER_VPFE1_CLKCTRL_OFFSET,
		},
	},
};

/* Interfaces */
static struct omap_hwmod_ocp_if am43xx_l3_main__emif = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am43xx_emif_hwmod,
	.clk		= "dpll_core_m4_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l3_main__l4_hs = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am43xx_l4_hs_hwmod,
	.clk		= "l3s_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_wkup_m3__l4_wkup = {
	.master		= &am43xx_wkup_m3_hwmod,
	.slave		= &am33xx_l4_wkup_hwmod,
	.clk		= "sys_clkin_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l4_wkup__wkup_m3 = {
	.master		= &am33xx_l4_wkup_hwmod,
	.slave		= &am43xx_wkup_m3_hwmod,
	.clk		= "sys_clkin_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l3_main__pruss = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_pruss_hwmod,
	.clk		= "dpll_core_m4_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_wkup__smartreflex0 = {
	.master		= &am33xx_l4_wkup_hwmod,
	.slave		= &am33xx_smartreflex0_hwmod,
	.clk		= "sys_clkin_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_wkup__smartreflex1 = {
	.master		= &am33xx_l4_wkup_hwmod,
	.slave		= &am33xx_smartreflex1_hwmod,
	.clk		= "sys_clkin_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_wkup__control = {
	.master		= &am33xx_l4_wkup_hwmod,
	.slave		= &am43xx_control_hwmod,
	.clk		= "sys_clkin_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_wkup__adc_tsc = {
	.master         = &am33xx_l4_wkup_hwmod,
	.slave          = &am43xx_adc_tsc_hwmod,
	.clk            = "dpll_core_m4_div2_ck",
	.user           = OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_hs__cpgmac0 = {
	.master		= &am43xx_l4_hs_hwmod,
	.slave		= &am33xx_cpgmac0_hwmod,
	.clk		= "cpsw_125mhz_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_wkup__timer1 = {
	.master		= &am33xx_l4_wkup_hwmod,
	.slave		= &am33xx_timer1_hwmod,
	.clk		= "sys_clkin_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_wkup__wd_timer1 = {
	.master		= &am33xx_l4_wkup_hwmod,
	.slave		= &am33xx_wd_timer1_hwmod,
	.clk		= "sys_clkin_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am33xx_l4_wkup__synctimer = {
	.master		= &am33xx_l4_wkup_hwmod,
	.slave		= &am43xx_synctimer_hwmod,
	.clk		= "sys_clkin_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__timer8 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_timer8_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__timer9 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_timer9_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__timer10 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_timer10_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__timer11 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_timer11_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__epwmss3 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_epwmss3_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__epwmss4 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_epwmss4_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__epwmss5 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_epwmss5_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__mcspi2 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_spi2_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__mcspi3 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_spi3_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__mcspi4 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_spi4_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__ocp2scp0 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_ocp2scp0_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__ocp2scp1 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_ocp2scp1_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if am43xx_l3_s__usbotgss0 = {
	.master         = &am33xx_l3_s_hwmod,
	.slave          = &am43xx_usb_otg_ss0_hwmod,
	.clk            = "l3s_gclk",
	.user           = OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l3_s__usbotgss1 = {
	.master         = &am33xx_l3_s_hwmod,
	.slave          = &am43xx_usb_otg_ss1_hwmod,
	.clk            = "l3s_gclk",
	.user           = OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l3_s__qspi = {
	.master         = &am33xx_l3_s_hwmod,
	.slave          = &am43xx_qspi_hwmod,
	.clk            = "l3s_gclk",
	.user           = OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_dss__l3_main = {
	.master		= &am43xx_dss_core_hwmod,
	.slave		= &am33xx_l3_main_hwmod,
	.clk		= "l3_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__dss = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_dss_core_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__dss_dispc = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_dss_dispc_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__dss_rfbi = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am43xx_dss_rfbi_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__hdq1w = {
	.master         = &am33xx_l4_ls_hwmod,
	.slave          = &am43xx_hdq1w_hwmod,
	.clk            = "l4ls_gclk",
	.user           = OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l3__vpfe0 = {
	.master         = &am43xx_vpfe0_hwmod,
	.slave          = &am33xx_l3_main_hwmod,
	.clk            = "l3_gclk",
	.user           = OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l3__vpfe1 = {
	.master         = &am43xx_vpfe1_hwmod,
	.slave          = &am33xx_l3_main_hwmod,
	.clk            = "l3_gclk",
	.user           = OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__vpfe0 = {
	.master         = &am33xx_l4_ls_hwmod,
	.slave          = &am43xx_vpfe0_hwmod,
	.clk            = "l4ls_gclk",
	.user           = OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l4_ls__vpfe1 = {
	.master         = &am33xx_l4_ls_hwmod,
	.slave          = &am43xx_vpfe1_hwmod,
	.clk            = "l4ls_gclk",
	.user           = OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if am43xx_l3_main__des = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am43xx_des_hwmod,
	.clk		= "l3_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if *am43xx_hwmod_ocp_ifs[] __initdata = {
	&am33xx_l4_wkup__synctimer,
	&am43xx_l4_ls__timer8,
	&am43xx_l4_ls__timer9,
	&am43xx_l4_ls__timer10,
	&am43xx_l4_ls__timer11,
	&am43xx_l4_ls__epwmss3,
	&am43xx_l4_ls__epwmss4,
	&am43xx_l4_ls__epwmss5,
	&am43xx_l4_ls__mcspi2,
	&am43xx_l4_ls__mcspi3,
	&am43xx_l4_ls__mcspi4,
	&am43xx_l3_main__pruss,
	&am33xx_mpu__l3_main,
	&am33xx_mpu__prcm,
	&am33xx_l3_s__l4_ls,
	&am33xx_l3_s__l4_wkup,
	&am43xx_l3_main__l4_hs,
	&am33xx_l3_main__l3_s,
	&am33xx_l3_main__l3_instr,
	&am33xx_l3_main__gfx,
	&am33xx_l3_s__l3_main,
	&am43xx_l3_main__emif,
	&am33xx_pruss__l3_main,
	&am43xx_wkup_m3__l4_wkup,
	&am33xx_gfx__l3_main,
	&am43xx_l4_wkup__wkup_m3,
	&am43xx_l4_wkup__control,
	&am43xx_l4_wkup__smartreflex0,
	&am43xx_l4_wkup__smartreflex1,
	&am43xx_l4_wkup__timer1,
	&am43xx_l4_wkup__wd_timer1,
	&am43xx_l4_wkup__adc_tsc,
	&am43xx_l3_s__qspi,
	&am33xx_l4_per__dcan0,
	&am33xx_l4_per__dcan1,
	&am33xx_l4_per__mailbox,
	&am33xx_l4_per__rng,
	&am33xx_l4_ls__mcasp0,
	&am33xx_l4_ls__mcasp1,
	&am33xx_l4_ls__timer2,
	&am33xx_l4_ls__timer3,
	&am33xx_l4_ls__timer4,
	&am33xx_l4_ls__timer5,
	&am33xx_l4_ls__timer6,
	&am33xx_l4_ls__timer7,
	&am33xx_l3_main__tpcc,
	&am33xx_l4_ls__spinlock,
	&am33xx_l4_ls__elm,
	&am33xx_l4_ls__epwmss0,
	&am33xx_l4_ls__epwmss1,
	&am33xx_l4_ls__epwmss2,
	&am33xx_l3_s__gpmc,
	&am33xx_l4_ls__mcspi0,
	&am33xx_l4_ls__mcspi1,
	&am33xx_l3_main__tptc0,
	&am33xx_l3_main__tptc1,
	&am33xx_l3_main__tptc2,
	&am33xx_l3_main__ocmc,
	&am43xx_l4_hs__cpgmac0,
	&am33xx_cpgmac0__mdio,
	&am33xx_l3_main__sha0,
	&am33xx_l3_main__aes0,
	&am43xx_l3_main__des,
	&am43xx_l4_ls__ocp2scp0,
	&am43xx_l4_ls__ocp2scp1,
	&am43xx_l3_s__usbotgss0,
	&am43xx_l3_s__usbotgss1,
	&am43xx_dss__l3_main,
	&am43xx_l4_ls__dss,
	&am43xx_l4_ls__dss_dispc,
	&am43xx_l4_ls__dss_rfbi,
	&am43xx_l4_ls__hdq1w,
	&am43xx_l3__vpfe0,
	&am43xx_l3__vpfe1,
	&am43xx_l4_ls__vpfe0,
	&am43xx_l4_ls__vpfe1,
	NULL,
};

static struct omap_hwmod_ocp_if *am43xx_rtc_hwmod_ocp_ifs[] __initdata = {
	&am33xx_l4_wkup__rtc,
	NULL,
};

int __init am43xx_hwmod_init(void)
{
	int ret;

	omap_hwmod_am43xx_reg();
	omap_hwmod_init();
	ret = omap_hwmod_register_links(am43xx_hwmod_ocp_ifs);

	if (!ret && of_machine_is_compatible("ti,am4372"))
		ret = omap_hwmod_register_links(am43xx_rtc_hwmod_ocp_ifs);

	return ret;
}
