/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Table of the Omap register configurations for the FUNC_MUX and
 * PULL_DWN combinations.
 *
 * Copyright (C) 2004 - 2008 Texas Instruments Inc.
 * Copyright (C) 2003 - 2008 Nokia Corporation
 *
 * Written by Tony Lindgren
 *
 * NOTE: Please use the following naming style for new pin entries.
 *	 For example, W8_1610_MMC2_DAT0, where:
 *	 - W8	     = ball
 *	 - 1610	     = 1510 or 1610, none if common for both 1510 and 1610
 *	 - MMC2_DAT0 = function
 */

#ifndef __ASM_ARCH_MUX_H
#define __ASM_ARCH_MUX_H

#include <linux/soc/ti/omap1-mux.h>

#define PU_PD_SEL_NA		0	/* No pu_pd reg available */
#define PULL_DWN_CTRL_NA	0	/* No pull-down control needed */

#ifdef	CONFIG_OMAP_MUX_DEBUG
#define MUX_REG(reg, mode_offset, mode) .mux_reg_name = "FUNC_MUX_CTRL_"#reg, \
					.mux_reg = FUNC_MUX_CTRL_##reg, \
					.mask_offset = mode_offset, \
					.mask = mode,

#define PULL_REG(reg, bit, status)	.pull_name = "PULL_DWN_CTRL_"#reg, \
					.pull_reg = PULL_DWN_CTRL_##reg, \
					.pull_bit = bit, \
					.pull_val = status,

#define PU_PD_REG(reg, status)		.pu_pd_name = "PU_PD_SEL_"#reg, \
					.pu_pd_reg = PU_PD_SEL_##reg, \
					.pu_pd_val = status,

#define MUX_REG_7XX(reg, mode_offset, mode) .mux_reg_name = "OMAP7XX_IO_CONF_"#reg, \
					.mux_reg = OMAP7XX_IO_CONF_##reg, \
					.mask_offset = mode_offset, \
					.mask = mode,

#define PULL_REG_7XX(reg, bit, status)	.pull_name = "OMAP7XX_IO_CONF_"#reg, \
					.pull_reg = OMAP7XX_IO_CONF_##reg, \
					.pull_bit = bit, \
					.pull_val = status,

#else

#define MUX_REG(reg, mode_offset, mode) .mux_reg = FUNC_MUX_CTRL_##reg, \
					.mask_offset = mode_offset, \
					.mask = mode,

#define PULL_REG(reg, bit, status)	.pull_reg = PULL_DWN_CTRL_##reg, \
					.pull_bit = bit, \
					.pull_val = status,

#define PU_PD_REG(reg, status)		.pu_pd_reg = PU_PD_SEL_##reg, \
					.pu_pd_val = status,

#define MUX_REG_7XX(reg, mode_offset, mode) \
					.mux_reg = OMAP7XX_IO_CONF_##reg, \
					.mask_offset = mode_offset, \
					.mask = mode,

#define PULL_REG_7XX(reg, bit, status)	.pull_reg = OMAP7XX_IO_CONF_##reg, \
					.pull_bit = bit, \
					.pull_val = status,

#endif /* CONFIG_OMAP_MUX_DEBUG */

#define MUX_CFG(desc, mux_reg, mode_offset, mode,	\
		pull_reg, pull_bit, pull_status,	\
		pu_pd_reg, pu_pd_status, debug_status)	\
{							\
	.name =	 desc,					\
	.debug = debug_status,				\
	MUX_REG(mux_reg, mode_offset, mode)		\
	PULL_REG(pull_reg, pull_bit, pull_status)	\
	PU_PD_REG(pu_pd_reg, pu_pd_status)		\
},


/*
 * OMAP730/850 has a slightly different config for the pin mux.
 * - config regs are the OMAP7XX_IO_CONF_x regs (see omap7xx.h) regs and
 *   not the FUNC_MUX_CTRL_x regs from hardware.h
 * - for pull-up/down, only has one enable bit which is in the same register
 *   as mux config
 */
#define MUX_CFG_7XX(desc, mux_reg, mode_offset, mode,	\
		   pull_bit, pull_status, debug_status)\
{							\
	.name =	 desc,					\
	.debug = debug_status,				\
	MUX_REG_7XX(mux_reg, mode_offset, mode)		\
	PULL_REG_7XX(mux_reg, pull_bit, pull_status)	\
	PU_PD_REG(NA, 0)		\
},

struct pin_config {
	char 			*name;
	const unsigned int 	mux_reg;
	unsigned char		debug;

	const unsigned char mask_offset;
	const unsigned char mask;

	const char *pull_name;
	const unsigned int pull_reg;
	const unsigned char pull_val;
	const unsigned char pull_bit;

	const char *pu_pd_name;
	const unsigned int pu_pd_reg;
	const unsigned char pu_pd_val;

#if	defined(CONFIG_OMAP_MUX_DEBUG) || defined(CONFIG_OMAP_MUX_WARNINGS)
	const char *mux_reg_name;
#endif

};

struct omap_mux_cfg {
	struct pin_config	*pins;
	unsigned long		size;
	int			(*cfg_reg)(const struct pin_config *cfg);
};

#ifdef	CONFIG_OMAP_MUX
/* setup pin muxing in Linux */
extern int omap1_mux_init(void);
extern int omap_mux_register(struct omap_mux_cfg *);
#else
/* boot loader does it all (no warnings from CONFIG_OMAP_MUX_WARNINGS) */
static inline int omap1_mux_init(void) { return 0; }
#endif

extern int omap2_mux_init(void);

#endif
