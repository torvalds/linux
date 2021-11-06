/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 2000, 2009
 *    Author(s): Hartmut Penner <hp@de.ibm.com>,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Christian Ehrhardt <ehrhardt@de.ibm.com>,
 */

#ifndef _ASM_S390_CPU_H
#define _ASM_S390_CPU_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/jump_label.h>

struct cpuid
{
	unsigned int version :	8;
	unsigned int ident   : 24;
	unsigned int machine : 16;
	unsigned int unused  : 16;
} __attribute__ ((packed, aligned(8)));

DECLARE_STATIC_KEY_FALSE(cpu_has_bear);

#endif /* __ASSEMBLY__ */
#endif /* _ASM_S390_CPU_H */
