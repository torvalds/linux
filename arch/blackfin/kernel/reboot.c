/*
 * arch/blackfin/kernel/reboot.c - handle shutdown/reboot
 *
 * Copyright 2004-2007 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <asm/bfin-global.h>
#include <asm/reboot.h>
#include <asm/system.h>

/* A system soft reset makes external memory unusable so force
 * this function into L1.  We use the compiler ssync here rather
 * than SSYNC() because it's safe (no interrupts and such) and
 * we save some L1.  We do not need to force sanity in the SYSCR
 * register as the BMODE selection bit is cleared by the soft
 * reset while the Core B bit (on dual core parts) is cleared by
 * the core reset.
 */
__attribute__((l1_text))
void bfin_reset(void)
{
	/* Wait for completion of "system" events such as cache line
	 * line fills so that we avoid infinite stalls later on as
	 * much as possible.  This code is in L1, so it won't trigger
	 * any such event after this point in time.
	 */
	__builtin_bfin_ssync();

	while (1) {
		/* Initiate System software reset. */
		bfin_write_SWRST(0x7);

		/* Due to the way reset is handled in the hardware, we need
		 * to delay for 7 SCLKS.  The only reliable way to do this is
		 * to calculate the CCLK/SCLK ratio and multiply 7.  For now,
		 * we'll assume worse case which is a 1:15 ratio.
		 */
		asm(
			"LSETUP (1f, 1f) LC0 = %0\n"
			"1: nop;"
			:
			: "a" (15 * 7)
			: "LC0", "LB0", "LT0"
		);

		/* Clear System software reset */
		bfin_write_SWRST(0);

		/* Wait for the SWRST write to complete.  Cannot rely on SSYNC
		 * though as the System state is all reset now.
		 */
		asm(
			"LSETUP (1f, 1f) LC1 = %0\n"
			"1: nop;"
			:
			: "a" (15 * 1)
			: "LC1", "LB1", "LT1"
		);

		/* Issue core reset */
		asm("raise 1");
	}
}

__attribute__((weak))
void native_machine_restart(char *cmd)
{
}

void machine_restart(char *cmd)
{
	native_machine_restart(cmd);
	local_irq_disable();
	bfin_reset();
}

__attribute__((weak))
void native_machine_halt(void)
{
	idle_with_irq_disabled();
}

void machine_halt(void)
{
	native_machine_halt();
}

__attribute__((weak))
void native_machine_power_off(void)
{
	idle_with_irq_disabled();
}

void machine_power_off(void)
{
	native_machine_power_off();
}
