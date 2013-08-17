/*
 * arch/xtensa/io.c
 *
 * IO primitives
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Copied from sparc.
 *
 * Chris Zankel <chris@zankel.net>
 *
 */

#include <asm/io.h>
#include <asm/byteorder.h>

void outsb(unsigned long addr, const void *src, unsigned long count) {
        while (count) {
                count -= 1;
                writeb(*(const char *)src, addr);
                src += 1;
                addr += 1;
        }
}

void outsw(unsigned long addr, const void *src, unsigned long count) {
        while (count) {
                count -= 2;
                writew(*(const short *)src, addr);
                src += 2;
                addr += 2;
        }
}

void outsl(unsigned long addr, const void *src, unsigned long count) {
        while (count) {
                count -= 4;
                writel(*(const long *)src, addr);
                src += 4;
                addr += 4;
        }
}

void insb(unsigned long addr, void *dst, unsigned long count) {
        while (count) {
                count -= 1;
                *(unsigned char *)dst = readb(addr);
                dst += 1;
                addr += 1;
        }
}

void insw(unsigned long addr, void *dst, unsigned long count) {
        while (count) {
                count -= 2;
                *(unsigned short *)dst = readw(addr);
                dst += 2;
                addr += 2;
        }
}

void insl(unsigned long addr, void *dst, unsigned long count) {
        while (count) {
                count -= 4;
                /*
                 * XXX I am sure we are in for an unaligned trap here.
                 */
                *(unsigned long *)dst = readl(addr);
                dst += 4;
                addr += 4;
        }
}
