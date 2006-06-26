/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 2003 by Ralf Baechle
 * Copyright (C) Wind River System Inc. Rongkai.Zhan <rongkai.zhan@windriver.com>
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/mach-wrppmc/mach-gt64120.h>

	.align	5
	.set	noat
NESTED(handle_IRQ, PT_SIZE, sp)
	SAVE_ALL
	CLI				# Important: mark KERNEL mode !
	.set	at

	mfc0	t0, CP0_CAUSE		# get pending interrupts
	mfc0	t1, CP0_STATUS		# get enabled interrupts
	and	t0, t0, t1		# get allowed interrupts
	andi	t0, t0, 0xFF00
	beqz	t0, 1f
	move	a1, sp			# Prepare 'struct pt_regs *regs' pointer

	andi	t1, t0, CAUSEF_IP7	# CPU Compare/Count internal timer
	bnez	t1, handle_cputimer_irq
	andi	t1, t0, CAUSEF_IP6	# UART 16550 port
	bnez	t1, handle_uart_irq
	andi	t1, t0, CAUSEF_IP3	# PCI INT_A
	bnez	t1, handle_pci_intA_irq

	/* wrong alarm or masked ... */
1:	j	spurious_interrupt
	nop
END(handle_IRQ)

	.align	5
handle_cputimer_irq:
	li	a0, WRPPMC_MIPS_TIMER_IRQ
	jal	do_IRQ
	j	ret_from_irq

	.align	5
handle_uart_irq:
	li	a0, WRPPMC_UART16550_IRQ
	jal	do_IRQ
	j	ret_from_irq

	.align	5
handle_pci_intA_irq:
	li	a0, WRPPMC_PCI_INTA_IRQ
	jal	do_IRQ
	j	ret_from_irq

