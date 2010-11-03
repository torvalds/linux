#ifndef ASMARM_SMP_MIDR_H
#define ASMARM_SMP_MIDR_H

#define hard_smp_processor_id()						\
	({								\
		unsigned int cpunum;					\
		__asm__("\n"						\
			"1:	mrc p15, 0, %0, c0, c0, 5\n"		\
			"	.pushsection \".alt.smp.init\", \"a\"\n"\
			"	.long	1b\n"				\
			"	mov	%0, #0\n"			\
			"	.popsection"				\
			: "=r" (cpunum));				\
		cpunum &= 0x0F;						\
	})

#endif
