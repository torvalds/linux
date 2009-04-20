/*
 *    Copyright IBM Corp. 2000,2009
 *    Author(s): Hartmut Penner <hp@de.ibm.com>,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Christian Ehrhardt <ehrhardt@de.ibm.com>
 */

#ifndef _ASM_S390_CPUID_H_
#define _ASM_S390_CPUID_H_

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 *  Members of this structure are referenced in head.S, so think twice
 *  before touching them. [mj]
 */

typedef struct
{
	unsigned int version :	8;
	unsigned int ident   : 24;
	unsigned int machine : 16;
	unsigned int unused  : 16;
} __attribute__ ((packed)) cpuid_t;

#endif /* _ASM_S390_CPUID_H_ */
