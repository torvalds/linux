/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Renesas R-Car System Controller
 *
 * Copyright (C) 2016 Glider bvba
 */
#ifndef __SOC_RENESAS_RCAR_SYSC_H__
#define __SOC_RENESAS_RCAR_SYSC_H__

#include <linux/types.h>


/*
 * Power Domain flags
 */
#define PD_CPU		BIT(0)	/* Area contains main CPU core */
#define PD_SCU		BIT(1)	/* Area contains SCU and L2 cache */
#define PD_NO_CR	BIT(2)	/* Area lacks PWR{ON,OFF}CR registers */

#define PD_CPU_CR	PD_CPU		  /* CPU area has CR (R-Car H1) */
#define PD_CPU_NOCR	PD_CPU | PD_NO_CR /* CPU area lacks CR (R-Car Gen2/3) */
#define PD_ALWAYS_ON	PD_NO_CR	  /* Always-on area */


/*
 * Description of a Power Area
 */

struct rcar_sysc_area {
	const char *name;
	u16 chan_offs;		/* Offset of PWRSR register for this area */
	u8 chan_bit;		/* Bit in PWR* (except for PWRUP in PWRSR) */
	u8 isr_bit;		/* Bit in SYSCI*R */
	int parent;		/* -1 if none */
	unsigned int flags;	/* See PD_* */
};


/*
 * SoC-specific Power Area Description
 */

struct rcar_sysc_info {
	int (*init)(void);	/* Optional */
	const struct rcar_sysc_area *areas;
	unsigned int num_areas;
	/* Optional External Request Mask Register */
	u32 extmask_offs;	/* SYSCEXTMASK register offset */
	u32 extmask_val;	/* SYSCEXTMASK register mask value */
};

extern const struct rcar_sysc_info r8a7743_sysc_info;
extern const struct rcar_sysc_info r8a7745_sysc_info;
extern const struct rcar_sysc_info r8a77470_sysc_info;
extern const struct rcar_sysc_info r8a774a1_sysc_info;
extern const struct rcar_sysc_info r8a774b1_sysc_info;
extern const struct rcar_sysc_info r8a774c0_sysc_info;
extern const struct rcar_sysc_info r8a7779_sysc_info;
extern const struct rcar_sysc_info r8a7790_sysc_info;
extern const struct rcar_sysc_info r8a7791_sysc_info;
extern const struct rcar_sysc_info r8a7792_sysc_info;
extern const struct rcar_sysc_info r8a7794_sysc_info;
extern struct rcar_sysc_info r8a7795_sysc_info;
extern const struct rcar_sysc_info r8a77960_sysc_info;
extern const struct rcar_sysc_info r8a77961_sysc_info;
extern const struct rcar_sysc_info r8a77965_sysc_info;
extern const struct rcar_sysc_info r8a77970_sysc_info;
extern const struct rcar_sysc_info r8a77980_sysc_info;
extern const struct rcar_sysc_info r8a77990_sysc_info;
extern const struct rcar_sysc_info r8a77995_sysc_info;


    /*
     * Helpers for fixing up power area tables depending on SoC revision
     */

extern void rcar_sysc_nullify(struct rcar_sysc_area *areas,
			      unsigned int num_areas, u8 id);

#endif /* __SOC_RENESAS_RCAR_SYSC_H__ */
