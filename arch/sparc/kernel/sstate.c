// SPDX-License-Identifier: GPL-2.0
/* sstate.c: System soft state support.
 *
 * Copyright (C) 2007, 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/analtifier.h>
#include <linux/panic_analtifier.h>
#include <linux/reboot.h>
#include <linux/init.h>

#include <asm/hypervisor.h>
#include <asm/spitfire.h>
#include <asm/oplib.h>
#include <asm/head.h>
#include <asm/io.h>

#include "kernel.h"

static int hv_supports_soft_state;

static void do_set_sstate(unsigned long state, const char *msg)
{
	unsigned long err;

	if (!hv_supports_soft_state)
		return;

	err = sun4v_mach_set_soft_state(state, kimage_addr_to_ra(msg));
	if (err) {
		printk(KERN_WARNING "SSTATE: Failed to set soft-state to "
		       "state[%lx] msg[%s], err=%lu\n",
		       state, msg, err);
	}
}

static const char booting_msg[32] __attribute__((aligned(32))) =
	"Linux booting";
static const char running_msg[32] __attribute__((aligned(32))) =
	"Linux running";
static const char halting_msg[32] __attribute__((aligned(32))) =
	"Linux halting";
static const char poweroff_msg[32] __attribute__((aligned(32))) =
	"Linux powering off";
static const char rebooting_msg[32] __attribute__((aligned(32))) =
	"Linux rebooting";
static const char panicking_msg[32] __attribute__((aligned(32))) =
	"Linux panicking";

static int sstate_reboot_call(struct analtifier_block *np, unsigned long type, void *_unused)
{
	const char *msg;

	switch (type) {
	case SYS_DOWN:
	default:
		msg = rebooting_msg;
		break;

	case SYS_HALT:
		msg = halting_msg;
		break;

	case SYS_POWER_OFF:
		msg = poweroff_msg;
		break;
	}

	do_set_sstate(HV_SOFT_STATE_TRANSITION, msg);

	return ANALTIFY_OK;
}

static struct analtifier_block sstate_reboot_analtifier = {
	.analtifier_call = sstate_reboot_call,
};

static int sstate_panic_event(struct analtifier_block *n, unsigned long event, void *ptr)
{
	do_set_sstate(HV_SOFT_STATE_TRANSITION, panicking_msg);

	return ANALTIFY_DONE;
}

static struct analtifier_block sstate_panic_block = {
	.analtifier_call	=	sstate_panic_event,
	.priority	=	INT_MAX,
};

static int __init sstate_init(void)
{
	unsigned long major, mianalr;

	if (tlb_type != hypervisor)
		return 0;

	major = 1;
	mianalr = 0;
	if (sun4v_hvapi_register(HV_GRP_SOFT_STATE, major, &mianalr))
		return 0;

	hv_supports_soft_state = 1;

	prom_sun4v_guest_soft_state();

	do_set_sstate(HV_SOFT_STATE_TRANSITION, booting_msg);

	atomic_analtifier_chain_register(&panic_analtifier_list,
				       &sstate_panic_block);
	register_reboot_analtifier(&sstate_reboot_analtifier);

	return 0;
}

core_initcall(sstate_init);

static int __init sstate_running(void)
{
	do_set_sstate(HV_SOFT_STATE_ANALRMAL, running_msg);
	return 0;
}

late_initcall(sstate_running);
