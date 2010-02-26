/*
 * header file for hardware trace functions
 *
 * Copyright 2007-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BLACKFIN_TRACE_
#define _BLACKFIN_TRACE_

/* Normally, we use ON, but you can't turn on software expansion until
 * interrupts subsystem is ready
 */

#define BFIN_TRACE_INIT ((CONFIG_DEBUG_BFIN_HWTRACE_COMPRESSION << 4) | 0x03)
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
#define BFIN_TRACE_ON   (BFIN_TRACE_INIT | (CONFIG_DEBUG_BFIN_HWTRACE_EXPAND << 2))
#else
#define BFIN_TRACE_ON   (BFIN_TRACE_INIT)
#endif

#ifndef __ASSEMBLY__
extern unsigned long trace_buff_offset;
extern unsigned long software_trace_buff[];

/* Trace Macros for C files */

#ifdef CONFIG_DEBUG_BFIN_HWTRACE_ON

#define trace_buffer_init() bfin_write_TBUFCTL(BFIN_TRACE_INIT)

#define trace_buffer_save(x) \
	do { \
		(x) = bfin_read_TBUFCTL(); \
		bfin_write_TBUFCTL((x) & ~TBUFEN); \
	} while (0)

#define trace_buffer_restore(x) \
	do { \
		bfin_write_TBUFCTL((x));        \
	} while (0)
#else /* DEBUG_BFIN_HWTRACE_ON */

#define trace_buffer_save(x)
#define trace_buffer_restore(x)
#endif /* CONFIG_DEBUG_BFIN_HWTRACE_ON */

#else
/* Trace Macros for Assembly files */

#ifdef CONFIG_DEBUG_BFIN_HWTRACE_ON

#define trace_buffer_stop(preg, dreg)	\
	preg.L = LO(TBUFCTL);		\
	preg.H = HI(TBUFCTL);		\
	dreg = 0x1;			\
	[preg] = dreg;

#define trace_buffer_init(preg, dreg) \
	preg.L = LO(TBUFCTL);         \
	preg.H = HI(TBUFCTL);         \
	dreg = BFIN_TRACE_INIT;       \
	[preg] = dreg;

#define trace_buffer_save(preg, dreg) \
	preg.L = LO(TBUFCTL); \
	preg.H = HI(TBUFCTL); \
	dreg = [preg]; \
	[--sp] = dreg; \
	dreg = 0x1; \
	[preg] = dreg;

#define trace_buffer_restore(preg, dreg) \
	preg.L = LO(TBUFCTL); \
	preg.H = HI(TBUFCTL); \
	dreg = [sp++]; \
	[preg] = dreg;

#else /* CONFIG_DEBUG_BFIN_HWTRACE_ON */

#define trace_buffer_stop(preg, dreg)
#define trace_buffer_init(preg, dreg)
#define trace_buffer_save(preg, dreg)
#define trace_buffer_restore(preg, dreg)

#endif /* CONFIG_DEBUG_BFIN_HWTRACE_ON */

#ifdef CONFIG_DEBUG_BFIN_NO_KERN_HWTRACE
# define DEBUG_HWTRACE_SAVE(preg, dreg)    trace_buffer_save(preg, dreg)
# define DEBUG_HWTRACE_RESTORE(preg, dreg) trace_buffer_restore(preg, dreg)
#else
# define DEBUG_HWTRACE_SAVE(preg, dreg)
# define DEBUG_HWTRACE_RESTORE(preg, dreg)
#endif

#endif /* __ASSEMBLY__ */

#endif				/* _BLACKFIN_TRACE_ */
