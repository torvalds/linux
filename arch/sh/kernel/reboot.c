// SPDX-License-Identifier: GPL-2.0
#include <linux/pm.h>
#include <linux/kexec.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/module.h>
#include <asm/watchdog.h>
#include <asm/addrspace.h>
#include <asm/reboot.h>
#include <asm/tlbflush.h>
#include <asm/traps.h>

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

static void watchdog_trigger_immediate(void)
{
	sh_wdt_write_cnt(0xFF);
	sh_wdt_write_csr(0xC2);
}

static void native_machine_restart(char * __unused)
{
	local_irq_disable();

	/* Destroy all of the TLBs in preparation for reset by MMU */
	__flush_tlb_global();

	/* Address error with SR.BL=1 first. */
	trigger_address_error();

	/* If that fails or is unsupported, go for the watchdog next. */
	watchdog_trigger_immediate();

	/*
	 * Give up and sleep.
	 */
	while (1)
		cpu_sleep();
}

static void native_machine_shutdown(void)
{
	smp_send_stop();
}

static void native_machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
}

static void native_machine_halt(void)
{
	/* stop other cpus */
	machine_shutdown();

	/* stop this cpu */
	stop_this_cpu(NULL);
}

struct machine_ops machine_ops = {
	.power_off	= native_machine_power_off,
	.shutdown	= native_machine_shutdown,
	.restart	= native_machine_restart,
	.halt		= native_machine_halt,
#ifdef CONFIG_KEXEC
	.crash_shutdown = native_machine_crash_shutdown,
#endif
};

void machine_power_off(void)
{
	machine_ops.power_off();
}

void machine_shutdown(void)
{
	machine_ops.shutdown();
}

void machine_restart(char *cmd)
{
	machine_ops.restart(cmd);
}

void machine_halt(void)
{
	machine_ops.halt();
}

#ifdef CONFIG_KEXEC
void machine_crash_shutdown(struct pt_regs *regs)
{
	machine_ops.crash_shutdown(regs);
}
#endif
