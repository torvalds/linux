#ifndef _ASM_X86_INTEL_ARCH_PERFMON_H
#define _ASM_X86_INTEL_ARCH_PERFMON_H

#define MSR_ARCH_PERFMON_PERFCTR0		0xc1
#define MSR_ARCH_PERFMON_PERFCTR1		0xc2

#define MSR_ARCH_PERFMON_EVENTSEL0		0x186
#define MSR_ARCH_PERFMON_EVENTSEL1		0x187

#define ARCH_PERFMON_EVENTSEL0_ENABLE	(1 << 22)
#define ARCH_PERFMON_EVENTSEL_INT	(1 << 20)
#define ARCH_PERFMON_EVENTSEL_OS	(1 << 17)
#define ARCH_PERFMON_EVENTSEL_USR	(1 << 16)

#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_SEL	(0x3c)
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_UMASK	(0x00 << 8)
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_INDEX (0)
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_PRESENT \
	(1 << (ARCH_PERFMON_UNHALTED_CORE_CYCLES_INDEX))

union cpuid10_eax {
	struct {
		unsigned int version_id:8;
		unsigned int num_counters:8;
		unsigned int bit_width:8;
		unsigned int mask_length:8;
	} split;
	unsigned int full;
};

#endif /* _ASM_X86_INTEL_ARCH_PERFMON_H */
