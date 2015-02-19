#ifndef ___ASM_SPARC_CPUDATA_H
#define ___ASM_SPARC_CPUDATA_H

#ifndef __ASSEMBLY__

#include <linux/threads.h>
#include <linux/percpu.h>

extern const struct seq_operations cpuinfo_op;

#endif /* !(__ASSEMBLY__) */

#if defined(__sparc__) && defined(__arch64__)
#include <asm/cpudata_64.h>
#else
#include <asm/cpudata_32.h>
#endif
#endif
