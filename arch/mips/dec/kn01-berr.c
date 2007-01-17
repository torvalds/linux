/*
 *	linux/arch/mips/dec/kn01-berr.c
 *
 *	Bus error event handling code for DECstation/DECsystem 3100
 *	and 2100 (KN01) systems equipped with parity error detection
 *	logic.
 *
 *	Copyright (c) 2005  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/inst.h>
#include <asm/irq_regs.h>
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/uaccess.h>

#include <asm/dec/kn01.h>


/* CP0 hazard avoidance. */
#define BARRIER				\
	__asm__ __volatile__(		\
		".set	push\n\t"	\
		".set	noreorder\n\t"	\
		"nop\n\t"		\
		".set	pop\n\t")

/*
 * Bits 7:0 of the Control Register are write-only -- the
 * corresponding bits of the Status Register have a different
 * meaning.  Hence we use a cache.  It speeds up things a bit
 * as well.
 *
 * There is no default value -- it has to be initialized.
 */
u16 cached_kn01_csr;
DEFINE_SPINLOCK(kn01_lock);


static inline void dec_kn01_be_ack(void)
{
	volatile u16 *csr = (void *)CKSEG1ADDR(KN01_SLOT_BASE + KN01_CSR);
	unsigned long flags;

	spin_lock_irqsave(&kn01_lock, flags);

	*csr = cached_kn01_csr | KN01_CSR_MEMERR;	/* Clear bus IRQ. */
	iob();

	spin_unlock_irqrestore(&kn01_lock, flags);
}

static int dec_kn01_be_backend(struct pt_regs *regs, int is_fixup, int invoker)
{
	volatile u32 *kn01_erraddr = (void *)CKSEG1ADDR(KN01_SLOT_BASE +
							KN01_ERRADDR);

	static const char excstr[] = "exception";
	static const char intstr[] = "interrupt";
	static const char cpustr[] = "CPU";
	static const char mreadstr[] = "memory read";
	static const char readstr[] = "read";
	static const char writestr[] = "write";
	static const char timestr[] = "timeout";
	static const char paritystr[] = "parity error";

	int data = regs->cp0_cause & 4;
	unsigned int __user *pc = (unsigned int __user *)regs->cp0_epc +
				  ((regs->cp0_cause & CAUSEF_BD) != 0);
	union mips_instruction insn;
	unsigned long entrylo, offset;
	long asid, entryhi, vaddr;

	const char *kind, *agent, *cycle, *event;
	unsigned long address;

	u32 erraddr = *kn01_erraddr;
	int action = MIPS_BE_FATAL;

	/* Ack ASAP, so that any subsequent errors get caught. */
	dec_kn01_be_ack();

	kind = invoker ? intstr : excstr;

	agent = cpustr;

	if (invoker)
		address = erraddr;
	else {
		/* Bloody hardware doesn't record the address for reads... */
		if (data) {
			/* This never faults. */
			__get_user(insn.word, pc);
			vaddr = regs->regs[insn.i_format.rs] +
				insn.i_format.simmediate;
		} else
			vaddr = (long)pc;
		if (KSEGX(vaddr) == CKSEG0 || KSEGX(vaddr) == CKSEG1)
			address = CPHYSADDR(vaddr);
		else {
			/* Peek at what physical address the CPU used. */
			asid = read_c0_entryhi();
			entryhi = asid & (PAGE_SIZE - 1);
			entryhi |= vaddr & ~(PAGE_SIZE - 1);
			write_c0_entryhi(entryhi);
			BARRIER;
			tlb_probe();
			/* No need to check for presence. */
			tlb_read();
			entrylo = read_c0_entrylo0();
			write_c0_entryhi(asid);
			offset = vaddr & (PAGE_SIZE - 1);
			address = (entrylo & ~(PAGE_SIZE - 1)) | offset;
		}
	}

	/* Treat low 256MB as memory, high -- as I/O. */
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

	return action;
}

int dec_kn01_be_handler(struct pt_regs *regs, int is_fixup)
{
	return dec_kn01_be_backend(regs, is_fixup, 0);
}

irqreturn_t dec_kn01_be_interrupt(int irq, void *dev_id)
{
	volatile u16 *csr = (void *)CKSEG1ADDR(KN01_SLOT_BASE + KN01_CSR);
	struct pt_regs *regs = get_irq_regs();
	int action;

	if (!(*csr & KN01_CSR_MEMERR))
		return IRQ_NONE;		/* Must have been video. */

	action = dec_kn01_be_backend(regs, 0, 1);

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


void __init dec_kn01_be_init(void)
{
	volatile u16 *csr = (void *)CKSEG1ADDR(KN01_SLOT_BASE + KN01_CSR);
	unsigned long flags;

	spin_lock_irqsave(&kn01_lock, flags);

	/* Preset write-only bits of the Control Register cache. */
	cached_kn01_csr = *csr;
	cached_kn01_csr &= KN01_CSR_STATUS | KN01_CSR_PARDIS | KN01_CSR_TXDIS;
	cached_kn01_csr |= KN01_CSR_LEDS;

	/* Enable parity error detection. */
	cached_kn01_csr &= ~KN01_CSR_PARDIS;
	*csr = cached_kn01_csr;
	iob();

	spin_unlock_irqrestore(&kn01_lock, flags);

	/* Clear any leftover errors from the firmware. */
	dec_kn01_be_ack();
}
