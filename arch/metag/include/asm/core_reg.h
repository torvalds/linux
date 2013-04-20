#ifndef __ASM_METAG_CORE_REG_H_
#define __ASM_METAG_CORE_REG_H_

#include <asm/metag_regs.h>

extern void core_reg_write(int unit, int reg, int thread, unsigned int val);
extern unsigned int core_reg_read(int unit, int reg, int thread);

/*
 * These macros allow direct access from C to any register known to the
 * assembler. Example candidates are TXTACTCYC, TXIDLECYC, and TXPRIVEXT.
 */

#define __core_reg_get(reg) ({						\
	unsigned int __grvalue;						\
	asm volatile("MOV	%0," #reg				\
		     : "=r" (__grvalue));				\
	__grvalue;							\
})

#define __core_reg_set(reg, value) do {					\
	unsigned int __srvalue = (value);				\
	asm volatile("MOV	" #reg ",%0"				\
		     :							\
		     : "r" (__srvalue));				\
} while (0)

#define __core_reg_swap(reg, value) do {				\
	unsigned int __srvalue = (value);				\
	asm volatile("SWAP	" #reg ",%0"				\
		     : "+r" (__srvalue));				\
	(value) = __srvalue;						\
} while (0)

#endif
