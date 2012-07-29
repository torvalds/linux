/*
 * OMAP2/3/4 powerdomain control
 *
 * Copyright (C) 2007-2008, 2010 Texas Instruments, Inc.
 * Copyright (C) 2007-2011 Nokia Corporation
 *
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX This should be moved to the mach-omap2/ directory at the earliest
 * opportunity.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_POWERDOMAIN_H
#define __ARCH_ARM_MACH_OMAP2_POWERDOMAIN_H

#include <linux/types.h>
#include <linux/list.h>

#include <linux/atomic.h>

#include <plat/cpu.h>

#include "voltage.h"

/* Powerdomain basic power states */
#define PWRDM_POWER_OFF		0x0
#define PWRDM_POWER_RET		0x1
#define PWRDM_POWER_INACTIVE	0x2
#define PWRDM_POWER_ON		0x3

#define PWRDM_MAX_PWRSTS	4

/* Powerdomain allowable state bitfields */
#define PWRSTS_ON		(1 << PWRDM_POWER_ON)
#define PWRSTS_INACTIVE		(1 << PWRDM_POWER_INACTIVE)
#define PWRSTS_RET		(1 << PWRDM_POWER_RET)
#define PWRSTS_OFF		(1 << PWRDM_POWER_OFF)

#define PWRSTS_OFF_ON		(PWRSTS_OFF | PWRSTS_ON)
#define PWRSTS_OFF_RET		(PWRSTS_OFF | PWRSTS_RET)
#define PWRSTS_RET_ON		(PWRSTS_RET | PWRSTS_ON)
#define PWRSTS_OFF_RET_ON	(PWRSTS_OFF_RET | PWRSTS_ON)


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
 * PER powerdomain on AM33XX is the worst case
 */
#define PWRDM_MAX_CLKDMS	11

/* XXX A completely arbitrary number. What is reasonable here? */
#define PWRDM_TRANSITION_BAILOUT 100000

struct clockdomain;
struct powerdomain;

/**
 * struct powerdomain - OMAP powerdomain
 * @name: Powerdomain name
 * @voltdm: voltagedomain containing this powerdomain
 * @prcm_offs: the address offset from CM_BASE/PRM_BASE
 * @prcm_partition: (OMAP4 only) the PRCM partition ID containing @prcm_offs
 * @pwrsts: Possible powerdomain power states
 * @pwrsts_logic_ret: Possible logic power states when pwrdm in RETENTION
 * @flags: Powerdomain flags
 * @banks: Number of software-controllable memory banks in this powerdomain
 * @pwrsts_mem_ret: Possible memory bank pwrstates when pwrdm in RETENTION
 * @pwrsts_mem_on: Possible memory bank pwrstates when pwrdm in ON
 * @pwrdm_clkdms: Clockdomains in this powerdomain
 * @node: list_head linking all powerdomains
 * @voltdm_node: list_head linking all powerdomains in a voltagedomain
 * @pwrstctrl_offs: (AM33XX only) XXX_PWRSTCTRL reg offset from prcm_offs
 * @pwrstst_offs: (AM33XX only) XXX_PWRSTST reg offset from prcm_offs
 * @logicretstate_mask: (AM33XX only) mask for logic retention bitfield
 *	in @pwrstctrl_offs
 * @mem_on_mask: (AM33XX only) mask for mem on bitfield in @pwrstctrl_offs
 * @mem_ret_mask: (AM33XX only) mask for mem ret bitfield in @pwrstctrl_offs
 * @mem_pwrst_mask: (AM33XX only) mask for mem state bitfield in @pwrstst_offs
 * @mem_retst_mask: (AM33XX only) mask for mem retention state bitfield
 *	in @pwrstctrl_offs
 * @state:
 * @state_counter:
 * @timer:
 * @state_timer:
 *
 * @prcm_partition possible values are defined in mach-omap2/prcm44xx.h.
 */
struct powerdomain {
	const char *name;
	union {
		const char *name;
		struct voltagedomain *ptr;
	} voltdm;
	const s16 prcm_offs;
	const u8 pwrsts;
	const u8 pwrsts_logic_ret;
	const u8 flags;
	const u8 banks;
	const u8 pwrsts_mem_ret[PWRDM_MAX_MEM_BANKS];
	const u8 pwrsts_mem_on[PWRDM_MAX_MEM_BANKS];
	const u8 prcm_partition;
	struct clockdomain *pwrdm_clkdms[PWRDM_MAX_CLKDMS];
	struct list_head node;
	struct list_head voltdm_node;
	int state;
	unsigned state_counter[PWRDM_MAX_PWRSTS];
	unsigned ret_logic_off_counter;
	unsigned ret_mem_off_counter[PWRDM_MAX_MEM_BANKS];

	const u8 pwrstctrl_offs;
	const u8 pwrstst_offs;
	const u32 logicretstate_mask;
	const u32 mem_on_mask[PWRDM_MAX_MEM_BANKS];
	const u32 mem_ret_mask[PWRDM_MAX_MEM_BANKS];
	const u32 mem_pwrst_mask[PWRDM_MAX_MEM_BANKS];
	const u32 mem_retst_mask[PWRDM_MAX_MEM_BANKS];

#ifdef CONFIG_PM_DEBUG
	s64 timer;
	s64 state_timer[PWRDM_MAX_PWRSTS];
#endif
};

/**
 * struct pwrdm_ops - Arch specific function implementations
 * @pwrdm_set_next_pwrst: Set the target power state for a pd
 * @pwrdm_read_next_pwrst: Read the target power state set for a pd
 * @pwrdm_read_pwrst: Read the current power state of a pd
 * @pwrdm_read_prev_pwrst: Read the prev power state entered by the pd
 * @pwrdm_set_logic_retst: Set the logic state in RET for a pd
 * @pwrdm_set_mem_onst: Set the Memory state in ON for a pd
 * @pwrdm_set_mem_retst: Set the Memory state in RET for a pd
 * @pwrdm_read_logic_pwrst: Read the current logic state of a pd
 * @pwrdm_read_prev_logic_pwrst: Read the previous logic state entered by a pd
 * @pwrdm_read_logic_retst: Read the logic state in RET for a pd
 * @pwrdm_read_mem_pwrst: Read the current memory state of a pd
 * @pwrdm_read_prev_mem_pwrst: Read the previous memory state entered by a pd
 * @pwrdm_read_mem_retst: Read the memory state in RET for a pd
 * @pwrdm_clear_all_prev_pwrst: Clear all previous power states logged for a pd
 * @pwrdm_enable_hdwr_sar: Enable Hardware Save-Restore feature for the pd
 * @pwrdm_disable_hdwr_sar: Disable Hardware Save-Restore feature for a pd
 * @pwrdm_set_lowpwrstchange: Enable pd transitions from a shallow to deep sleep
 * @pwrdm_wait_transition: Wait for a pd state transition to complete
 */
struct pwrdm_ops {
	int	(*pwrdm_set_next_pwrst)(struct powerdomain *pwrdm, u8 pwrst);
	int	(*pwrdm_read_next_pwrst)(struct powerdomain *pwrdm);
	int	(*pwrdm_read_pwrst)(struct powerdomain *pwrdm);
	int	(*pwrdm_read_prev_pwrst)(struct powerdomain *pwrdm);
	int	(*pwrdm_set_logic_retst)(struct powerdomain *pwrdm, u8 pwrst);
	int	(*pwrdm_set_mem_onst)(struct powerdomain *pwrdm, u8 bank, u8 pwrst);
	int	(*pwrdm_set_mem_retst)(struct powerdomain *pwrdm, u8 bank, u8 pwrst);
	int	(*pwrdm_read_logic_pwrst)(struct powerdomain *pwrdm);
	int	(*pwrdm_read_prev_logic_pwrst)(struct powerdomain *pwrdm);
	int	(*pwrdm_read_logic_retst)(struct powerdomain *pwrdm);
	int	(*pwrdm_read_mem_pwrst)(struct powerdomain *pwrdm, u8 bank);
	int	(*pwrdm_read_prev_mem_pwrst)(struct powerdomain *pwrdm, u8 bank);
	int	(*pwrdm_read_mem_retst)(struct powerdomain *pwrdm, u8 bank);
	int	(*pwrdm_clear_all_prev_pwrst)(struct powerdomain *pwrdm);
	int	(*pwrdm_enable_hdwr_sar)(struct powerdomain *pwrdm);
	int	(*pwrdm_disable_hdwr_sar)(struct powerdomain *pwrdm);
	int	(*pwrdm_set_lowpwrstchange)(struct powerdomain *pwrdm);
	int	(*pwrdm_wait_transition)(struct powerdomain *pwrdm);
};

int pwrdm_register_platform_funcs(struct pwrdm_ops *custom_funcs);
int pwrdm_register_pwrdms(struct powerdomain **pwrdm_list);
int pwrdm_complete_init(void);

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
struct voltagedomain *pwrdm_get_voltdm(struct powerdomain *pwrdm);

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
int pwrdm_pre_transition(struct powerdomain *pwrdm);
int pwrdm_post_transition(struct powerdomain *pwrdm);
int pwrdm_set_lowpwrstchange(struct powerdomain *pwrdm);
int pwrdm_get_context_loss_count(struct powerdomain *pwrdm);
bool pwrdm_can_ever_lose_context(struct powerdomain *pwrdm);

extern void omap242x_powerdomains_init(void);
extern void omap243x_powerdomains_init(void);
extern void omap3xxx_powerdomains_init(void);
extern void am33xx_powerdomains_init(void);
extern void omap44xx_powerdomains_init(void);

extern struct pwrdm_ops omap2_pwrdm_operations;
extern struct pwrdm_ops omap3_pwrdm_operations;
extern struct pwrdm_ops am33xx_pwrdm_operations;
extern struct pwrdm_ops omap4_pwrdm_operations;

/* Common Internal functions used across OMAP rev's */
extern u32 omap2_pwrdm_get_mem_bank_onstate_mask(u8 bank);
extern u32 omap2_pwrdm_get_mem_bank_retst_mask(u8 bank);
extern u32 omap2_pwrdm_get_mem_bank_stst_mask(u8 bank);

extern struct powerdomain wkup_omap2_pwrdm;
extern struct powerdomain gfx_omap2_pwrdm;


#endif
