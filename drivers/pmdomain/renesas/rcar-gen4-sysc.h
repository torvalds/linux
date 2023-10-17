/* SPDX-License-Identifier: GPL-2.0 */
/*
 * R-Car Gen4 System Controller
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 */
#ifndef __SOC_RENESAS_RCAR_GEN4_SYSC_H__
#define __SOC_RENESAS_RCAR_GEN4_SYSC_H__

#include <linux/types.h>

/*
 * Power Domain flags
 */
#define PD_CPU		BIT(0)	/* Area contains main CPU core */
#define PD_SCU		BIT(1)	/* Area contains SCU and L2 cache */
#define PD_NO_CR	BIT(2)	/* Area lacks PWR{ON,OFF}CR registers */

#define PD_CPU_NOCR	(PD_CPU | PD_NO_CR) /* CPU area lacks CR */
#define PD_ALWAYS_ON	PD_NO_CR	  /* Always-on area */

/*
 * Description of a Power Area
 */
struct rcar_gen4_sysc_area {
	const char *name;
	u8 pdr;			/* PDRn */
	s8 parent;		/* -1 if none */
	u8 flags;		/* See PD_* */
};

/*
 * SoC-specific Power Area Description
 */
struct rcar_gen4_sysc_info {
	const struct rcar_gen4_sysc_area *areas;
	unsigned int num_areas;
};

extern const struct rcar_gen4_sysc_info r8a779a0_sysc_info;
extern const struct rcar_gen4_sysc_info r8a779f0_sysc_info;
extern const struct rcar_gen4_sysc_info r8a779g0_sysc_info;

#endif /* __SOC_RENESAS_RCAR_GEN4_SYSC_H__ */
