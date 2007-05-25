/* sstate.c: System soft state support.
 *
 * Copyright (C) 2007 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/init.h>

#include <asm/hypervisor.h>
#include <asm/sstate.h>
#include <asm/oplib.h>
#include <asm/head.h>
#include <asm/io.h>

static int hv_supports_soft_state;

static unsigned long kimage_addr_to_ra(const char *p)
{
	unsigned long val = (unsigned long) p;

	return kern_base + (val - KERNBASE);
}

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
static const char panicing_msg[32] __attribute__((aligned(32))) =
	"Linux panicing";

void sstate_booting(void)
{
	do_set_sstate(HV_SOFT_STATE_TRANSITION, booting_msg);
}

void sstate_running(void)
{
	do_set_sstate(HV_SOFT_STATE_NORMAL, running_msg);
}

void sstate_halt(void)
{
	do_set_sstate(HV_SOFT_STATE_TRANSITION, halting_msg);
}

void sstate_poweroff(void)
{
	do_set_sstate(HV_SOFT_STATE_TRANSITION, poweroff_msg);
}

void sstate_reboot(void)
{
	do_set_sstate(HV_SOFT_STATE_TRANSITION, rebooting_msg);
}

static int sstate_panic_event(struct notifier_block *n, unsigned long event, void *ptr)
{
	do_set_sstate(HV_SOFT_STATE_TRANSITION, panicing_msg);

	return NOTIFY_DONE;
}

static struct notifier_block sstate_panic_block = {
	.notifier_call	=	sstate_panic_event,
	.priority	=	INT_MAX,
};

void __init sun4v_sstate_init(void)
{
	unsigned long major, minor;

	major = 1;
	minor = 0;
	if (sun4v_hvapi_register(HV_GRP_SOFT_STATE, major, &minor))
		return;

	hv_supports_soft_state = 1;

	prom_sun4v_guest_soft_state();
	atomic_notifier_chain_register(&panic_notifier_list,
				       &sstate_panic_block);
}
