#ifndef _ASM_X86_IRQ_VECTORS_H
#define _ASM_X86_IRQ_VECTORS_H

/*
 * Linux IRQ vector layout.
 *
 * There are 256 IDT entries (per CPU - each entry is 8 bytes) which can
 * be defined by Linux. They are used as a jump table by the CPU when a
 * given vector is triggered - by a CPU-external, CPU-internal or
 * software-triggered event.
 *
 * Linux sets the kernel code address each entry jumps to early during
 * bootup, and never changes them. This is the general layout of the
 * IDT entries:
 *
 *  Vectors   0 ...  31 : system traps and exceptions - hardcoded events
 *  Vectors  32 ... 127 : device interrupts
 *  Vector  128         : legacy int80 syscall interface
 *  Vectors 129 ... 237 : device interrupts
 *  Vectors 238 ... 255 : special interrupts
 *
 * 64-bit x86 has per CPU IDT tables, 32-bit has one shared IDT table.
 *
 * This file enumerates the exact layout of them:
 */

#define NMI_VECTOR			0x02
#define MCE_VECTOR			0x12

/*
 * IDT vectors usable for external interrupt sources start
 * at 0x20:
 */
#define FIRST_EXTERNAL_VECTOR		0x20

#ifdef CONFIG_X86_32
# define SYSCALL_VECTOR			0x80
# define IA32_SYSCALL_VECTOR		0x80
#else
# define IA32_SYSCALL_VECTOR		0x80
#endif

/*
 * Reserve the lowest usable priority level 0x20 - 0x2f for triggering
 * cleanup after irq migration.
 */
#define IRQ_MOVE_CLEANUP_VECTOR		FIRST_EXTERNAL_VECTOR

/*
 * Vectors 0x30-0x3f are used for ISA interrupts.
 */
#define IRQ0_VECTOR			(FIRST_EXTERNAL_VECTOR + 0x10)

#define IRQ1_VECTOR			(IRQ0_VECTOR +  1)
#define IRQ2_VECTOR			(IRQ0_VECTOR +  2)
#define IRQ3_VECTOR			(IRQ0_VECTOR +  3)
#define IRQ4_VECTOR			(IRQ0_VECTOR +  4)
#define IRQ5_VECTOR			(IRQ0_VECTOR +  5)
#define IRQ6_VECTOR			(IRQ0_VECTOR +  6)
#define IRQ7_VECTOR			(IRQ0_VECTOR +  7)
#define IRQ8_VECTOR			(IRQ0_VECTOR +  8)
#define IRQ9_VECTOR			(IRQ0_VECTOR +  9)
#define IRQ10_VECTOR			(IRQ0_VECTOR + 10)
#define IRQ11_VECTOR			(IRQ0_VECTOR + 11)
#define IRQ12_VECTOR			(IRQ0_VECTOR + 12)
#define IRQ13_VECTOR			(IRQ0_VECTOR + 13)
#define IRQ14_VECTOR			(IRQ0_VECTOR + 14)
#define IRQ15_VECTOR			(IRQ0_VECTOR + 15)

/*
 * Special IRQ vectors used by the SMP architecture, 0xf0-0xff
 *
 *  some of the following vectors are 'rare', they are merged
 *  into a single vector (CALL_FUNCTION_VECTOR) to save vector space.
 *  TLB, reschedule and local APIC vectors are performance-critical.
 */

#define SPURIOUS_APIC_VECTOR		0xff
/*
 * Sanity check
 */
#if ((SPURIOUS_APIC_VECTOR & 0x0F) != 0x0F)
# error SPURIOUS_APIC_VECTOR definition error
#endif

#define ERROR_APIC_VECTOR		0xfe
#define RESCHEDULE_VECTOR		0xfd
#define CALL_FUNCTION_VECTOR		0xfc
#define CALL_FUNCTION_SINGLE_VECTOR	0xfb
#define THERMAL_APIC_VECTOR		0xfa
#define THRESHOLD_APIC_VECTOR		0xf9
#define REBOOT_VECTOR			0xf8

/* f0-f7 used for spreading out TLB flushes: */
#define INVALIDATE_TLB_VECTOR_END	0xf7
#define INVALIDATE_TLB_VECTOR_START	0xf0
#define NUM_INVALIDATE_TLB_VECTORS	   8

/*
 * Local APIC timer IRQ vector is on a different priority level,
 * to work around the 'lost local interrupt if more than 2 IRQ
 * sources per level' errata.
 */
#define LOCAL_TIMER_VECTOR		0xef

/*
 * Generic system vector for platform specific use
 */
#define GENERIC_INTERRUPT_VECTOR	0xed

/*
 * Performance monitoring pending work vector:
 */
#define LOCAL_PENDING_VECTOR		0xec

#define UV_BAU_MESSAGE			0xec

/*
 * Self IPI vector for machine checks
 */
#define MCE_SELF_VECTOR			0xeb

/*
 * First APIC vector available to drivers: (vectors 0x30-0xee) we
 * start at 0x31(0x41) to spread out vectors evenly between priority
 * levels. (0x80 is the syscall vector)
 */
#define FIRST_DEVICE_VECTOR		(IRQ15_VECTOR + 2)

#define NR_VECTORS			 256

#define FPU_IRQ				  13

#define	FIRST_VM86_IRQ			   3
#define LAST_VM86_IRQ			  15

#ifndef __ASSEMBLY__
static inline int invalid_vm86_irq(int irq)
{
	return irq < FIRST_VM86_IRQ || irq > LAST_VM86_IRQ;
}
#endif

/*
 * Size the maximum number of interrupts.
 *
 * If the irq_desc[] array has a sparse layout, we can size things
 * generously - it scales up linearly with the maximum number of CPUs,
 * and the maximum number of IO-APICs, whichever is higher.
 *
 * In other cases we size more conservatively, to not create too large
 * static arrays.
 */

#define NR_IRQS_LEGACY			  16

#define CPU_VECTOR_LIMIT		(  8 * NR_CPUS      )
#define IO_APIC_VECTOR_LIMIT		( 32 * MAX_IO_APICS )

#ifdef CONFIG_X86_IO_APIC
# ifdef CONFIG_SPARSE_IRQ
#  define NR_IRQS					\
	(CPU_VECTOR_LIMIT > IO_APIC_VECTOR_LIMIT ?	\
		(NR_VECTORS + CPU_VECTOR_LIMIT)  :	\
		(NR_VECTORS + IO_APIC_VECTOR_LIMIT))
# else
#  if NR_CPUS < MAX_IO_APICS
#   define NR_IRQS 			(NR_VECTORS + 4*CPU_VECTOR_LIMIT)
#  else
#   define NR_IRQS			(NR_VECTORS + IO_APIC_VECTOR_LIMIT)
#  endif
# endif
#else /* !CONFIG_X86_IO_APIC: */
# define NR_IRQS			NR_IRQS_LEGACY
#endif

#endif /* _ASM_X86_IRQ_VECTORS_H */
