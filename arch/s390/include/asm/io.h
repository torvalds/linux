/*
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/io.h"
 */

#ifndef _S390_IO_H
#define _S390_IO_H

#include <asm/page.h>

#define IO_SPACE_LIMIT 0xffffffff

/*
 * Change virtual addresses to physical addresses and vv.
 * These are pretty trivial
 */
static inline unsigned long virt_to_phys(volatile void * address)
{
	unsigned long real_address;
	asm volatile(
		 "	lra	%0,0(%1)\n"
		 "	jz	0f\n"
		 "	la	%0,0\n"
                 "0:"
		 : "=a" (real_address) : "a" (address) : "cc");
        return real_address;
}

static inline void * phys_to_virt(unsigned long address)
{
	return (void *) address;
}

void *xlate_dev_mem_ptr(unsigned long phys);
void unxlate_dev_mem_ptr(unsigned long phys, void *addr);

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif
