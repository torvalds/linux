/*
 * OMAP2/3 powerdomain control
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Copyright (C) 2007-2009 Nokia Corporation
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

#include <plat/cpu.h>


/* Powerdomain basic power states */
#define PWRDM_POWER_OFF		0x0
#define PWRDM_POWER_RET		0x1
#define PWRDM_POWER_INACTIVE	0x2
#define PWRDM_POWER_ON		0x3

#define PWRDM_MAX_PWRSTS	4

/* Powerdomain allowable state bitfields */
#define PWRSTS_ON		(1 << PWRDM_POWER_ON)
#define PWRSTS_OFF_ON		((1 << PWRDM_POWER_OFF) | \
				 (1 << PWRDM_POWER_ON))

#define PWRSTS_OFF_RET		((1 << PWRDM_POWER_OFF) | \
				 (1 << PWRDM_POWER_RET))

#define PWRSTS_RET_ON		((1 << PWRDM_POWER_RET) | \
				 (1 << PWRDM_POWER_ON))

#define PWRSTS_OFF_RET_ON	(PWRSTS_OFF_RET | (1 << PWRDM_POWER_ON))


/* Powerdomain flags */
#define PWRDM_HAS_HDWR_SAR	(1 << 0) /* hardware save-and-restore support */
#define PWRDM_HAS_MPU_QUIRK	(1 << 1) /* MPU pwr domain has MEM bank 0 bits
					  * in MEM bank 1 position. This is
					  * true for OMAP3430
					  */
#define PWRDM_HAS_LOWPOWERSTATECHANGE	(1 << 2) /*
						  * support to transition from a
						  * sleep state to a lower sleep
						  * state without waking up the
						  * powerdomain
						  */

/*
 * Number of memory banks that are power-controllable.	On OMAP4430, the
 * maximum is 5.
 */
#define PWRDM_MAX_MEM_BANKS	5

/*
 * Maximum number of clockdomains that can be associated with a powerdomain.
 * CORE powerdomain on OMAP4 is the worst case
 */
#define PWRDM_MAX_CLKDMS	9

/* XXX A completely arbitrary number. What is reasonable here? */
#define PWRDM_TRANSITION_BAILOUT 100000

struct clockdomain;
struct powerdomain;

/**
 * struct powerdomain - OMAP powerdomain
 * @name: Powerdomain name
 * @omap_chip: represents the OMAP chip types containing this pwrdm
 * @prcm_offs: the address offset from CM_BASE/PRM_BASE
 * @pwrsts: Possible powerdomain power states
 * @pwrsts_logic_ret: Possible logic power states when pwrdm in RETENTION
 * @flags: Powerdomain flags
 * @banks: Number of software-controllable memory banks in this powerdomain
 * @pwrsts_mem_ret: Possible memory bank pwrstates when pwrdm in RETENTION
 * @pwrsts_mem_on: Possible memory bank pwrstates when pwrdm in ON
 * @pwrdm_clkdms: Clockdomains in this powerdomain
 * @node: list_head linking all powerdomains
 * @state:
 * @state_counter:
 * @timer:
 * @state_timer:
 */
struct powerdomain {
	const char *name;
	const struct omap_chip_id omap_chip;
	const s16 prcm_offs;
	const u8 pwrsts;
	const u8 pwrsts_logic_ret;
	const u8 flags;
	const u8 banks;
	const u8 pwrsts_mem_ret[PWRDM_MAX_MEM_BANKS];
	const u8 pwrsts_mem_on[PWRDM_MAX_MEM_BANKS];
	struct clockdomain *pwrdm_clkdms[PWRDM_MAX_CLKDMS];
	struct list_head node;
	int state;
	unsigned state_counter[PWRDM_MAX_PWRSTS];
	unsigned ret_logic_off_counter;
	unsigned ret_mem_off_counter[PWRDM_MAX_MEM_BANKS];

#ifdef CONFIG_PM_DEBUG
	s64 timer;
	s64 state_timer[PWRDM_MAX_PWRSTS];
#endif
};


void pwrdm_init(struct powerdomain **pwrdm_list);

struct powerdomain *pwrdm_lookup(const char *name);

int pwrdm_for_each(int (*fn)(struct powerdomain *pwrdm, void *user),
			void *user);
int pwrdm_for_each_nolock(int (*fn)(struct powerdomain *pwrdm, void *user),
			void *user);

int pwrdm_add_clkdm(struct powerdomain *pwrdm, struct clockdomain *clkdm);
int pwrdm_del_clkdm(struct powerdomain *pwrdm, struct clockdomain *clkdm);
int pwrdm_for_each_clkdm(struct powerdomain *pwrdm,
			 int (*fn)(struct powerdomain *pwrdm,
				   struct clockdomain *clkdm));

int pwrdm_get_mem_bank_count(struct powerdomain *pwrdm);

int pwrdm_set_next_pwrst(struct powerdomain *pwrdm, u8 pwrst);
int pwrdm_read_next_pwrst(struct powerdomain *pwrdm);
int pwrdm_read_pwrst(struct powerdomain *pwrdm);
int pwrdm_read_prev_pwrst(struct powerdomain *pwrdm);
int pwrdm_clear_all_prev_pwrst(struct powerdomain *pwrdm);

int pwrdm_set_logic_retst(struct powerdomain *pwrdm, u8 pwrst);
int pwrdm_set_mem_onst(struct powerdomain *pwrdm, u8 bank, u8 pwrst);
int pwrdm_set_mem_retst(struct powerdomain *pwrdm, u8 bank, u8 pwrst);

int pwrdm_read_logic_pwrst(struct powerdomain *pwrdm);
int pwrdm_read_prev_logic_pwrst(struct powerdomain *pwrdm);
int pwrdm_read_logic_retst(struct powerdomain *pwrdm);
int pwrdm_read_mem_pwrst(struct powerdomain *pwrdm, u8 bank);
int pwrdm_read_prev_mem_pwrst(struct powerdomain *pwrdm, u8 bank);
int pwrdm_read_mem_retst(struct powerdomain *pwrdm, u8 bank);

int pwrdm_enable_hdwr_sar(struct powerdomain *pwrdm);
int pwrdm_disable_hdwr_sar(struct powerdomain *pwrdm);
bool pwrdm_has_hdwr_sar(struct powerdomain *pwrdm);

int pwrdm_wait_transition(struct powerdomain *pwrdm);

int pwrdm_state_switch(struct powerdomain *pwrdm);
int pwrdm_clkdm_state_switch(struct clockdomain *clkdm);
int pwrdm_pre_transition(void);
int pwrdm_post_transition(void);

#endif
