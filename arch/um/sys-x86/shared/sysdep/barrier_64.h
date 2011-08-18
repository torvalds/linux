#ifndef __SYSDEP_X86_64_BARRIER_H
#define __SYSDEP_X86_64_BARRIER_H

/* Copied from include/asm-x86_64 for use by userspace. */
#define mb() 	asm volatile("mfence":::"memory")

#endif
