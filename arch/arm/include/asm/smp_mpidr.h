#ifndef ASMARM_SMP_MIDR_H
#define ASMARM_SMP_MIDR_H

#define hard_smp_processor_id()						\
	({								\
		unsigned int cpunum;					\
		__asm__("mrc p15, 0, %0, c0, c0, 5\n"			\
			: "=r" (cpunum));				\
		cpunum &= 0x0F;						\
	})

#endif
