// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Bus error event handling code for 5000-series systems equipped
 *	with parity error detection logic, i.e. DECstation/DECsystem
 *	5000/120, /125, /133 (KN02-BA), 5000/150 (KN04-BA) and Personal
 *	DECstation/DECsystem 5000/20, /25, /33 (KN02-CA), 5000/50
 *	(KN04-CA) systems.
 *
 *	Copyright (c) 2005  Maciej W. Rozycki
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/cpu-type.h>
#include <asm/irq_regs.h>
#include <asm/ptrace.h>
#include <asm/traps.h>

#include <asm/dec/kn02ca.h>
#include <asm/dec/kn02xa.h>
#include <asm/dec/kn05.h>

static inline void dec_kn02xa_be_ack(void)
{
	volatile u32 *mer = (void *)CKSEG1ADDR(KN02XA_MER);
	volatile u32 *mem_intr = (void *)CKSEG1ADDR(KN02XA_MEM_INTR);

	*mer = KN02CA_MER_INTR;		/* Clear errors; keep the ARC IRQ. */
	*mem_intr = 0;			/* Any write clears the bus IRQ. */
	iob();
}

static int dec_kn02xa_be_backend(struct pt_regs *regs, int is_fixup,
				 int invoker)
{
	volatile u32 *kn02xa_mer = (void *)CKSEG1ADDR(KN02XA_MER);
	volatile u32 *kn02xa_ear = (void *)CKSEG1ADDR(KN02XA_EAR);

	static const char excstr[] = "exception";
	static const char intstr[] = "interrupt";
	static const char cpustr[] = "CPU";
	static const char mreadstr[] = "memory read";
	static const char readstr[] = "read";
	static const char writestr[] = "write";
	static const char timestr[] = "timeout";
	static const char paritystr[] = "parity error";
	static const char lanestat[][4] = { " OK", "BAD" };

	const char *kind, *agent, *cycle, *event;
	unsigned long address;

	u32 mer = *kn02xa_mer;
	u32 ear = *kn02xa_ear;
	int action = MIPS_BE_FATAL;

	/* Ack ASAP, so that any subsequent errors get caught. */
	dec_kn02xa_be_ack();

	kind = invoker ? intstr : excstr;

	/* No DMA errors? */
	agent = cpustr;

	address = ear & KN02XA_EAR_ADDRESS;

	/* Low 256MB is decoded as memory, high -- as TC. */
	if (address < 0x10000000) {
		cycle = mreadstr;
		event = paritystr;
	} else {
		cycle = invoker ? writestr : readstr;
		event = timestr;
	}

	if (is_fixup)
		action = MIPS_BE_FIXUP;

	if (action != MIPS_BE_FIXUP)
		printk(KERN_ALERT "Bus error %s: %s %s %s at %#010lx\n",
			kind, agent, cycle, event, address);

	if (action != MIPS_BE_FIXUP && address < 0x10000000)
		printk(KERN_ALERT "  Byte lane status %#3x -- "
		       "#3: %s, #2: %s, #1: %s, #0: %s\n",
		       (mer & KN02XA_MER_BYTERR) >> 8,
		       lanestat[(mer & KN02XA_MER_BYTERR_3) != 0],
		       lanestat[(mer & KN02XA_MER_BYTERR_2) != 0],
		       lanestat[(mer & KN02XA_MER_BYTERR_1) != 0],
		       lanestat[(mer & KN02XA_MER_BYTERR_0) != 0]);

	return action;
}

int dec_kn02xa_be_handler(struct pt_regs *regs, int is_fixup)
{
	return dec_kn02xa_be_backend(regs, is_fixup, 0);
}

irqreturn_t dec_kn02xa_be_interrupt(int irq, void *dev_id)
{
	struct pt_regs *regs = get_irq_regs();
	int action = dec_kn02xa_be_backend(regs, 0, 1);

	if (action == MIPS_BE_DISCARD)
		return IRQ_HANDLED;

	/*
	 * FIXME: Find the affected processes and kill them, otherwise
	 * we must die.
	 *
	 * The interrupt is asynchronously delivered thus EPC and RA
	 * may be irrelevant, but are printed for a reference.
	 */
	printk(KERN_ALERT "Fatal bus interrupt, epc == %08lx, ra == %08lx\n",
	       regs->cp0_epc, regs->regs[31]);
	die("Unrecoverable bus error", regs);
}


void __init dec_kn02xa_be_init(void)
{
	volatile u32 *mbcs = (void *)CKSEG1ADDR(KN4K_SLOT_BASE + KN4K_MB_CSR);

	/* For KN04 we need to make sure EE (?) is enabled in the MB.  */
	if (current_cpu_type() == CPU_R4000SC)
		*mbcs |= KN4K_MB_CSR_EE;
	fast_iob();

	/* Clear any leftover errors from the firmware. */
	dec_kn02xa_be_ack();
}
