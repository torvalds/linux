/*
 * IRQ flags handling
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#ifndef __ASSEMBLY__
/*
 * Get definitions for arch_local_save_flags(x), etc.
 */
#include <asm/hw_irq.h>

#else
#ifdef CONFIG_TRACE_IRQFLAGS
#ifdef CONFIG_IRQSOFF_TRACER
/*
 * Since the ftrace irqsoff latency trace checks CALLER_ADDR1,
 * which is the stack frame here, we need to force a stack frame
 * in case we came from user space.
 */
#define TRACE_WITH_FRAME_BUFFER(func)		\
	mflr	r0;				\
	stdu	r1, -32(r1);			\
	std	r0, 16(r1);			\
	stdu	r1, -32(r1);			\
	bl func;				\
	ld	r1, 0(r1);			\
	ld	r1, 0(r1);
#else
#define TRACE_WITH_FRAME_BUFFER(func)		\
	bl func;
#endif

/*
 * Most of the CPU's IRQ-state tracing is done from assembly code; we
 * have to call a C function so call a wrapper that saves all the
 * C-clobbered registers.
 */
#define TRACE_ENABLE_INTS	TRACE_WITH_FRAME_BUFFER(.trace_hardirqs_on)
#define TRACE_DISABLE_INTS	TRACE_WITH_FRAME_BUFFER(.trace_hardirqs_off)

#define TRACE_AND_RESTORE_IRQ_PARTIAL(en,skip)		\
	cmpdi	en,0;					\
	bne	95f;					\
	stb	en,PACASOFTIRQEN(r13);			\
	TRACE_WITH_FRAME_BUFFER(.trace_hardirqs_off)	\
	b	skip;					\
95:	TRACE_WITH_FRAME_BUFFER(.trace_hardirqs_on)	\
	li	en,1;
#define TRACE_AND_RESTORE_IRQ(en)		\
	TRACE_AND_RESTORE_IRQ_PARTIAL(en,96f);	\
	stb	en,PACASOFTIRQEN(r13);		\
96:
#else
#define TRACE_ENABLE_INTS
#define TRACE_DISABLE_INTS
#define TRACE_AND_RESTORE_IRQ_PARTIAL(en,skip)
#define TRACE_AND_RESTORE_IRQ(en)		\
	stb	en,PACASOFTIRQEN(r13)
#endif
#endif

#endif
