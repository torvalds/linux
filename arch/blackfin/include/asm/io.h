/*
 * Copyright 2004-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BFIN_IO_H
#define _BFIN_IO_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/def_LPBlackfin.h>

#define __raw_readb bfin_read8
#define __raw_readw bfin_read16
#define __raw_readl bfin_read32
#define __raw_writeb(val, addr) bfin_write8(addr, val)
#define __raw_writew(val, addr) bfin_write16(addr, val)
#define __raw_writel(val, addr) bfin_write32(addr, val)

extern void outsb(unsigned long port, const void *addr, unsigned long count);
extern void outsw(unsigned long port, const void *addr, unsigned long count);
extern void outsw_8(unsigned long port, const void *addr, unsigned long count);
extern void outsl(unsigned long port, const void *addr, unsigned long count);
#define outsb outsb
#define outsw outsw
#define outsl outsl

extern void insb(unsigned long port, void *addr, unsigned long count);
extern void insw(unsigned long port, void *addr, unsigned long count);
extern void insw_8(unsigned long port, void *addr, unsigned long count);
extern void insl(unsigned long port, void *addr, unsigned long count);
extern void insl_16(unsigned long port, void *addr, unsigned long count);
#define insb insb
#define insw insw
#define insl insl

/**
 * I/O write barrier
 *
 * Ensure ordering of I/O space writes. This will make sure that writes
 * following the barrier will arrive after all previous writes.
 */
#define mmiowb() do { SSYNC(); wmb(); } while (0)

#include <asm-generic/io.h>

#endif
