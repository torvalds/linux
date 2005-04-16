#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.text
	.set    macro
	.set    noat
	.align	5

NESTED(it8172_IRQ, PT_SIZE, sp)
	SAVE_ALL
	CLI				# Important: mark KERNEL mode !

        /* We're working with 'reorder' set at this point. */
	/*
	 * Get pending interrupts
	 */

	mfc0	t0,CP0_CAUSE		# get pending interrupts
	mfc0	t1,CP0_STATUS		# get enabled interrupts
	and	t0,t1			# isolate allowed ones

	andi	t0,0xff00		# isolate pending bits
        beqz    t0, 3f                  # spurious interrupt

        andi    a0, t0, CAUSEF_IP7
        beq     a0, zero, 1f

        li	a0, 127			# MIPS_CPU_TIMER_IRQ = (NR_IRQS-1)
        move    a1, sp
        jal     ll_timer_interrupt
	j	ret_from_irq
        nop

1:
        andi    a0, t0, CAUSEF_IP2      # the only int we expect at this time
        beq     a0, zero, 3f
	move	a0,sp
	jal	it8172_hw0_irqdispatch

	mfc0	t0,CP0_STATUS		# disable interrupts
	ori	t0,1
	xori	t0,1
	mtc0	t0,CP0_STATUS
        nop
        nop
        nop

	la      a1, ret_from_irq
	jr	a1
        nop

3:
	move a0, sp
	jal	mips_spurious_interrupt
        nop
	la      a1, ret_from_irq
	jr	a1
        nop

END(it8172_IRQ)

