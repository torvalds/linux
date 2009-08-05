/*
 *	Bus error event handling code for systems equipped with ECC
 *	handling logic, i.e. DECstation/DECsystem 5000/200 (KN02),
 *	5000/240 (KN03), 5000/260 (KN05) and DECsystem 5900 (KN03),
 *	5900/260 (KN05) systems.
 *
 *	Copyright (c) 2003, 2005  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/irq_regs.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/traps.h>

#include <asm/dec/ecc.h>
#include <asm/dec/kn02.h>
#include <asm/dec/kn03.h>
#include <asm/dec/kn05.h>

static volatile u32 *kn0x_erraddr;
static volatile u32 *kn0x_chksyn;

static inline void dec_ecc_be_ack(void)
{
	*kn0x_erraddr = 0;			/* any write clears the IRQ */
	iob();
}

static int dec_ecc_be_backend(struct pt_regs *regs, int is_fixup, int invoker)
{
	static const char excstr[] = "exception";
	static const char intstr[] = "interrupt";
	static const char cpustr[] = "CPU";
	static const char dmastr[] = "DMA";
	static const char readstr[] = "read";
	static const char mreadstr[] = "memory read";
	static const char writestr[] = "write";
	static const char mwritstr[] = "partial memory write";
	static const char timestr[] = "timeout";
	static const char overstr[] = "overrun";
	static const char eccstr[] = "ECC error";

	const char *kind, *agent, *cycle, *event;
	const char *status = "", *xbit = "", *fmt = "";
	unsigned long address;
	u16 syn = 0, sngl;

	int i = 0;

	u32 erraddr = *kn0x_erraddr;
	u32 chksyn = *kn0x_chksyn;
	int action = MIPS_BE_FATAL;

	/* For non-ECC ack ASAP, so that any subsequent errors get caught. */
	if ((erraddr & (KN0X_EAR_VALID | KN0X_EAR_ECCERR)) == KN0X_EAR_VALID)
		dec_ecc_be_ack();

	kind = invoker ? intstr : excstr;

	if (!(erraddr & KN0X_EAR_VALID)) {
		/* No idea what happened. */
		printk(KERN_ALERT "Unidentified bus error %s\n", kind);
		return action;
	}

	agent = (erraddr & KN0X_EAR_CPU) ? cpustr : dmastr;

	if (erraddr & KN0X_EAR_ECCERR) {
		/* An ECC error on a CPU or DMA transaction. */
		cycle = (erraddr & KN0X_EAR_WRITE) ? mwritstr : mreadstr;
		event = eccstr;
	} else {
		/* A CPU timeout or a DMA overrun. */
		cycle = (erraddr & KN0X_EAR_WRITE) ? writestr : readstr;
		event = (erraddr & KN0X_EAR_CPU) ? timestr : overstr;
	}

	address = erraddr & KN0X_EAR_ADDRESS;
	/* For ECC errors on reads adjust for MT pipelining. */
	if ((erraddr & (KN0X_EAR_WRITE | KN0X_EAR_ECCERR)) == KN0X_EAR_ECCERR)
		address = (address & ~0xfffLL) | ((address - 5) & 0xfffLL);
	address <<= 2;

	/* Only CPU errors are fixable. */
	if (erraddr & KN0X_EAR_CPU && is_fixup)
		action = MIPS_BE_FIXUP;

	if (erraddr & KN0X_EAR_ECCERR) {
		static const u8 data_sbit[32] = {
			0x4f, 0x4a, 0x52, 0x54, 0x57, 0x58, 0x5b, 0x5d,
			0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x31, 0x34,
			0x0e, 0x0b, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c,
			0x62, 0x64, 0x67, 0x68, 0x6b, 0x6d, 0x70, 0x75,
		};
		static const u8 data_mbit[25] = {
			0x07, 0x0d, 0x1f,
			0x2f, 0x32, 0x37, 0x38, 0x3b, 0x3d, 0x3e,
			0x43, 0x45, 0x46, 0x49, 0x4c, 0x51, 0x5e,
			0x61, 0x6e, 0x73, 0x76, 0x79, 0x7a, 0x7c, 0x7f,
		};
		static const char sbestr[] = "corrected single";
		static const char dbestr[] = "uncorrectable double";
		static const char mbestr[] = "uncorrectable multiple";

		if (!(address & 0x4))
			syn = chksyn;			/* Low bank. */
		else
			syn = chksyn >> 16;		/* High bank. */

		if (!(syn & KN0X_ESR_VLDLO)) {
			/* Ack now, no rewrite will happen. */
			dec_ecc_be_ack();

			fmt = KERN_ALERT "%s" "invalid\n";
		} else {
			sngl = syn & KN0X_ESR_SNGLO;
			syn &= KN0X_ESR_SYNLO;

			/*
			 * Multibit errors may be tagged incorrectly;
			 * check the syndrome explicitly.
			 */
			for (i = 0; i < 25; i++)
				if (syn == data_mbit[i])
					break;

			if (i < 25) {
				status = mbestr;
			} else if (!sngl) {
				status = dbestr;
			} else {
				volatile u32 *ptr =
					(void *)CKSEG1ADDR(address);

				*ptr = *ptr;		/* Rewrite. */
				iob();

				status = sbestr;
				action = MIPS_BE_DISCARD;
			}

			/* Ack now, now we've rewritten (or not). */
			dec_ecc_be_ack();

			if (syn && syn == (syn & -syn)) {
				if (syn == 0x01) {
					fmt = KERN_ALERT "%s"
					      "%#04x -- %s bit error "
					      "at check bit C%s\n";
					xbit = "X";
				} else {
					fmt = KERN_ALERT "%s"
					      "%#04x -- %s bit error "
					      "at check bit C%s%u\n";
				}
				i = syn >> 2;
			} else {
				for (i = 0; i < 32; i++)
					if (syn == data_sbit[i])
						break;
				if (i < 32)
					fmt = KERN_ALERT "%s"
					      "%#04x -- %s bit error "
					      "at data bit D%s%u\n";
				else
					fmt = KERN_ALERT "%s"
					      "%#04x -- %s bit error\n";
			}
		}
	}

	if (action != MIPS_BE_FIXUP)
		printk(KERN_ALERT "Bus error %s: %s %s %s at %#010lx\n",
			kind, agent, cycle, event, address);

	if (action != MIPS_BE_FIXUP && erraddr & KN0X_EAR_ECCERR)
		printk(fmt, "  ECC syndrome ", syn, status, xbit, i);

	return action;
}

int dec_ecc_be_handler(struct pt_regs *regs, int is_fixup)
{
	return dec_ecc_be_backend(regs, is_fixup, 0);
}

irqreturn_t dec_ecc_be_interrupt(int irq, void *dev_id)
{
	struct pt_regs *regs = get_irq_regs();

	int action = dec_ecc_be_backend(regs, 0, 1);

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


/*
 * Initialization differs a bit between KN02 and KN03/KN05, so we
 * need two variants.  Once set up, all systems can be handled the
 * same way.
 */
static inline void dec_kn02_be_init(void)
{
	volatile u32 *csr = (void *)CKSEG1ADDR(KN02_SLOT_BASE + KN02_CSR);

	kn0x_erraddr = (void *)CKSEG1ADDR(KN02_SLOT_BASE + KN02_ERRADDR);
	kn0x_chksyn = (void *)CKSEG1ADDR(KN02_SLOT_BASE + KN02_CHKSYN);

	/* Preset write-only bits of the Control Register cache. */
	cached_kn02_csr = *csr | KN02_CSR_LEDS;

	/* Set normal ECC detection and generation. */
	cached_kn02_csr &= ~(KN02_CSR_DIAGCHK | KN02_CSR_DIAGGEN);
	/* Enable ECC correction. */
	cached_kn02_csr |= KN02_CSR_CORRECT;
	*csr = cached_kn02_csr;
	iob();
}

static inline void dec_kn03_be_init(void)
{
	volatile u32 *mcr = (void *)CKSEG1ADDR(KN03_SLOT_BASE + IOASIC_MCR);
	volatile u32 *mbcs = (void *)CKSEG1ADDR(KN4K_SLOT_BASE + KN4K_MB_CSR);

	kn0x_erraddr = (void *)CKSEG1ADDR(KN03_SLOT_BASE + IOASIC_ERRADDR);
	kn0x_chksyn = (void *)CKSEG1ADDR(KN03_SLOT_BASE + IOASIC_CHKSYN);

	/*
	 * Set normal ECC detection and generation, enable ECC correction.
	 * For KN05 we also need to make sure EE (?) is enabled in the MB.
	 * Otherwise DBE/IBE exceptions would be masked but bus error
	 * interrupts would still arrive, resulting in an inevitable crash
	 * if get_dbe() triggers one.
	 */
	*mcr = (*mcr & ~(KN03_MCR_DIAGCHK | KN03_MCR_DIAGGEN)) |
	       KN03_MCR_CORRECT;
	if (current_cpu_type() == CPU_R4400SC)
		*mbcs |= KN4K_MB_CSR_EE;
	fast_iob();
}

void __init dec_ecc_be_init(void)
{
	if (mips_machtype == MACH_DS5000_200)
		dec_kn02_be_init();
	else
		dec_kn03_be_init();

	/* Clear any leftover errors from the firmware. */
	dec_ecc_be_ack();
}
