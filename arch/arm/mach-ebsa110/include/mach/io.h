/*
 *  arch/arm/mach-ebsa110/include/mach/io.h
 *
 *  Copyright (C) 1997,1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *  06-Dec-1997	RMK	Created.
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

u8 __inb8(unsigned int port);
void __outb8(u8  val, unsigned int port);

u8 __inb16(unsigned int port);
void __outb16(u8  val, unsigned int port);

u16 __inw(unsigned int port);
void __outw(u16 val, unsigned int port);

u32 __inl(unsigned int port);
void __outl(u32 val, unsigned int port);

u8  __readb(const volatile void __iomem *addr);
u16 __readw(const volatile void __iomem *addr);
u32 __readl(const volatile void __iomem *addr);

void __writeb(u8  val, void __iomem *addr);
void __writew(u16 val, void __iomem *addr);
void __writel(u32 val, void __iomem *addr);

/*
 * Argh, someone forgot the IOCS16 line.  We therefore have to handle
 * the byte stearing by selecting the correct byte IO functions here.
 */
#ifdef ISA_SIXTEEN_BIT_PERIPHERAL
#define inb(p) 			__inb16(p)
#define outb(v,p)		__outb16(v,p)
#else
#define inb(p)			__inb8(p)
#define outb(v,p)		__outb8(v,p)
#endif

#define inw(p)			__inw(p)
#define outw(v,p)		__outw(v,p)

#define inl(p)			__inl(p)
#define outl(v,p)		__outl(v,p)

#define readb(b)		__readb(b)
#define readw(b)		__readw(b)
#define readl(b)		__readl(b)
#define readb_relaxed(addr)	readb(addr)
#define readw_relaxed(addr)	readw(addr)
#define readl_relaxed(addr)	readl(addr)

#define writeb(v,b)		__writeb(v,b)
#define writew(v,b)		__writew(v,b)
#define writel(v,b)		__writel(v,b)

extern void insb(unsigned int port, void *buf, int sz);
extern void insw(unsigned int port, void *buf, int sz);
extern void insl(unsigned int port, void *buf, int sz);

extern void outsb(unsigned int port, const void *buf, int sz);
extern void outsw(unsigned int port, const void *buf, int sz);
extern void outsl(unsigned int port, const void *buf, int sz);

/* can't support writesb atm */
extern void writesw(void __iomem *addr, const void *data, int wordlen);
extern void writesl(void __iomem *addr, const void *data, int longlen);

/* can't support readsb atm */
extern void readsw(const void __iomem *addr, void *data, int wordlen);
extern void readsl(const void __iomem *addr, void *data, int longlen);

#endif
