/*
 * Copyright 2005-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#ifndef _MACH_FIO_FLAG_H
#define _MACH_FIO_FLAG_H

#include <asm/blackfin.h>
#include <asm/irqflags.h>

#if ANOMALY_05000311
#define BFIN_WRITE_FIO_FLAG(name) \
static inline void bfin_write_FIO_FLAG_##name(unsigned short val) \
{ \
	unsigned long flags; \
	flags = hard_local_irq_save(); \
	bfin_write16(FIO_FLAG_##name, val); \
	bfin_read_CHIPID(); \
	hard_local_irq_restore(flags); \
}
BFIN_WRITE_FIO_FLAG(D)
BFIN_WRITE_FIO_FLAG(C)
BFIN_WRITE_FIO_FLAG(S)
BFIN_WRITE_FIO_FLAG(T)

#define BFIN_READ_FIO_FLAG(name) \
static inline u16 bfin_read_FIO_FLAG_##name(void) \
{ \
	unsigned long flags; \
	u16 ret; \
	flags = hard_local_irq_save(); \
	ret = bfin_read16(FIO_FLAG_##name); \
	bfin_read_CHIPID(); \
	hard_local_irq_restore(flags); \
	return ret; \
}
BFIN_READ_FIO_FLAG(D)
BFIN_READ_FIO_FLAG(C)
BFIN_READ_FIO_FLAG(S)
BFIN_READ_FIO_FLAG(T)

#else
#define bfin_write_FIO_FLAG_D(val)           bfin_write16(FIO_FLAG_D, val)
#define bfin_write_FIO_FLAG_C(val)           bfin_write16(FIO_FLAG_C, val)
#define bfin_write_FIO_FLAG_S(val)           bfin_write16(FIO_FLAG_S, val)
#define bfin_write_FIO_FLAG_T(val)           bfin_write16(FIO_FLAG_T, val)
#define bfin_read_FIO_FLAG_T()               bfin_read16(FIO_FLAG_T)
#define bfin_read_FIO_FLAG_C()               bfin_read16(FIO_FLAG_C)
#define bfin_read_FIO_FLAG_S()               bfin_read16(FIO_FLAG_S)
#define bfin_read_FIO_FLAG_D()               bfin_read16(FIO_FLAG_D)
#endif

#endif /* _MACH_FIO_FLAG_H */
