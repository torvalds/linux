#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.set	noat
	.align	5

NESTED(ev96100IRQ, PT_SIZE, sp)
	SAVE_ALL
	CLI				# Important: mark KERNEL mode !

	mfc0	t0, CP0_CAUSE		# get pending interrupts
	mfc0	t1, CP0_STATUS		# get enabled interrupts
	and	t0, t1			# isolate allowed ones

	# FIX ME add R7000 extensions
	andi	t0,0xff00		# isolate pending bits
	andi	a0, t0, CAUSEF_IP7
	beq	a0, zero, 1f
	move	a0, sp
	jal	mips_timer_interrupt
	j	ret_from_irq

1:	beqz	t0, 3f			# spurious interrupt

	move	a0, t0
	move	a1, sp
	jal	ev96100_cpu_irq
	j	ret_from_irq

3:	j	spurious_interrupt
	END(ev96100IRQ)
