#ifndef	_ASM_X86_INTEL_PCONFIG_H
#define	_ASM_X86_INTEL_PCONFIG_H

#include <asm/asm.h>
#include <asm/processor.h>

enum pconfig_target {
	INVALID_TARGET	= 0,
	MKTME_TARGET	= 1,
	PCONFIG_TARGET_NR
};

int pconfig_target_supported(enum pconfig_target target);

#endif	/* _ASM_X86_INTEL_PCONFIG_H */
