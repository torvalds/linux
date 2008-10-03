/*
 * OMAP2/3 powerdomain control
 *
 * Copyright (C) 2007-8 Texas Instruments, Inc.
 * Copyright (C) 2007-8 Nokia Corporation
 *
 * Written by Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ASM_ARM_ARCH_OMAP_POWERDOMAIN
#define ASM_ARM_ARCH_OMAP_POWERDOMAIN

#include <linux/types.h>
#include <linux/list.h>

#include <asm/atomic.h>

#include <mach/cpu.h>


/* Powerdomain basic power states */
#define PWRDM_POWER_OFF		0x0
#define PWRDM_POWER_RET		0x1
#define PWRDM_POWER_INACTIVE	0x2
#define PWRDM_POWER_ON		0x3

/* Powerdomain allowable state bitfields */
#define PWRSTS_OFF_ON		((1 << PWRDM_POWER_OFF) | \
				 (1 << PWRDM_POWER_ON))

#define PWRSTS_OFF_RET		((1 << PWRDM_POWER_OFF) | \
				 (1 << PWRDM_POWER_RET))

#define PWRSTS_OFF_RET_ON	(PWRSTS_OFF_RET | (1 << PWRDM_POWER_ON))


/* Powerdomain flags */
#define PWRDM_HAS_HDWR_SAR	(1 << 0) /* hardware save-and-restore support */


/*
 * Number of memory banks that are power-controllable.	On OMAP3430, the
 * maximum is 4.
 */
#define PWRDM_MAX_MEM_BANKS	4

/*
 * Maximum number of clockdomains that can be associated with a powerdomain.
 * CORE powerdomain is probably the worst case.
 */
#define PWRDM_MAX_CLKDMS	3

/* XXX A completely arbitrary number. What is reasonable here? */
#define PWRDM_TRANSITION_BAILOUT 100000

struct clockdomain;
struct powerdomain;

/* Encodes dependencies between powerdomains - statically defined */
struct pwrdm_dep {

	/* Powerdomain name */
	const char *pwrdm_name;

	/* Powerdomain pointer - resolved by the powerdomain code */
	struct powerdomain *pwrdm;

	/* Flags to mark OMAP chip restrictions, etc. */
	const struct omap_chip_id omap_chip;

};

struct powerdomain {

	/* Powerdomain name */
	const char *name;

	/* the address offset from CM_BASE/PRM_BASE */
	const s16 prcm_offs;

	/* Used to represent the OMAP chip types containing this pwrdm */
	const struct omap_chip_id omap_chip;

	/* Bit shift of this powerdomain's PM_WKDEP/CM_SLEEPDEP bit */
	const u8 dep_bit;

	/* Powerdomains that can be told to wake this powerdomain up */
	struct pwrdm_dep *wkdep_srcs;

	/* Powerdomains that can be told to keep this pwrdm from inactivity */
	struct pwrdm_dep *sleepdep_srcs;

	/* Possible powerdomain power states */
	const u8 pwrsts;

	/* Possible logic power states when pwrdm in RETENTION */
	const u8 pwrsts_logic_ret;

	/* Powerdomain flags */
	const u8 flags;

	/* Number of software-controllable memory banks in this powerdomain */
	const u8 banks;

	/* Possible memory bank pwrstates when pwrdm in RETENTION */
	const u8 pwrsts_mem_ret[PWRDM_MAX_MEM_BANKS];

	/* Possible memory bank pwrstates when pwrdm is ON */
	const u8 pwrsts_mem_on[PWRDM_MAX_MEM_BANKS];

	/* Clockdomains in this powerdomain */
	struct clockdomain *pwrdm_clkdms[PWRDM_MAX_CLKDMS];

	struct list_head node;

};


void pwrdm_init(struct powerdomain **pwrdm_list);

int pwrdm_register(struct powerdomain *pwrdm);
int pwrdm_unregister(struct powerdomain *pwrdm);
struct powerdomain *pwrdm_lookup(const char *name);

int pwrdm_for_each(int (*fn)(struct powerdomain *pwrdm));

int pwrdm_add_clkdm(struct powerdomain *pwrdm, struct clockdomain *clkdm);
int pwrdm_del_clkdm(struct powerdomain *pwrdm, struct clockdomain *clkdm);
int pwrdm_for_each_clkdm(struct powerdomain *pwrdm,
			 int (*fn)(struct powerdomain *pwrdm,
				   struct clockdomain *clkdm));

int pwrdm_add_wkdep(struct powerdomain *pwrdm1, struct powerdomain *pwrdm2);
int pwrdm_del_wkdep(struct powerdomain *pwrdm1, struct powerdomain *pwrdm2);
int pwrdm_read_wkdep(struct powerdomain *pwrdm1, struct powerdomain *pwrdm2);
int pwrdm_add_sleepdep(struct powerdomain *pwrdm1, struct powerdomain *pwrdm2);
int pwrdm_del_sleepdep(struct powerdomain *pwrdm1, struct powerdomain *pwrdm2);
int pwrdm_read_sleepdep(struct powerdomain *pwrdm1, struct powerdomain *pwrdm2);

int pwrdm_get_mem_bank_count(struct powerdomain *pwrdm);

int pwrdm_set_next_pwrst(struct powerdomain *pwrdm, u8 pwrst);
int pwrdm_read_next_pwrst(struct powerdomain *pwrdm);
int pwrdm_read_prev_pwrst(struct powerdomain *pwrdm);
int pwrdm_clear_all_prev_pwrst(struct powerdomain *pwrdm);

int pwrdm_set_logic_retst(struct powerdomain *pwrdm, u8 pwrst);
int pwrdm_set_mem_onst(struct powerdomain *pwrdm, u8 bank, u8 pwrst);
int pwrdm_set_mem_retst(struct powerdomain *pwrdm, u8 bank, u8 pwrst);

int pwrdm_read_logic_pwrst(struct powerdomain *pwrdm);
int pwrdm_read_prev_logic_pwrst(struct powerdomain *pwrdm);
int pwrdm_read_mem_pwrst(struct powerdomain *pwrdm, u8 bank);
int pwrdm_read_prev_mem_pwrst(struct powerdomain *pwrdm, u8 bank);

int pwrdm_enable_hdwr_sar(struct powerdomain *pwrdm);
int pwrdm_disable_hdwr_sar(struct powerdomain *pwrdm);
bool pwrdm_has_hdwr_sar(struct powerdomain *pwrdm);

int pwrdm_wait_transition(struct powerdomain *pwrdm);

#endif
