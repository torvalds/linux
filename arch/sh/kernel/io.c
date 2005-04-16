/*
 * linux/arch/sh/kernel/io.c
 *
 * Copyright (C) 2000  Stuart Menefy
 *
 * Provide real functions which expand to whatever the header file defined.
 * Also definitions of machine independent IO functions.
 */

#include <asm/io.h>
#include <linux/module.h>

/*
 * Copy data from IO memory space to "real" memory space.
 * This needs to be optimized.
 */
void  memcpy_fromio(void * to, unsigned long from, unsigned long count)
{
	char *p = to;
        while (count) {
                count--;
                *p = readb(from);
                p++;
                from++;
        }
}
 
/*
 * Copy data from "real" memory space to IO memory space.
 * This needs to be optimized.
 */
void  memcpy_toio(unsigned long to, const void * from, unsigned long count)
{
	const char *p = from;
        while (count) {
                count--;
                writeb(*p, to);
                p++;
                to++;
        }
}
 
/*
 * "memset" on IO memory space.
 * This needs to be optimized.
 */
void  memset_io(unsigned long dst, int c, unsigned long count)
{
        while (count) {
                count--;
                writeb(c, dst);
                dst++;
        }
}

EXPORT_SYMBOL(memcpy_fromio);
EXPORT_SYMBOL(memcpy_toio);
EXPORT_SYMBOL(memset_io);

