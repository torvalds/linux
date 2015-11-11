#ifndef _ASM_ARM_MODULE_H
#define _ASM_ARM_MODULE_H

#include <asm-generic/module.h>

struct unwind_table;

#ifdef CONFIG_ARM_UNWIND
enum {
	ARM_SEC_INIT,
	ARM_SEC_DEVINIT,
	ARM_SEC_CORE,
	ARM_SEC_EXIT,
	ARM_SEC_DEVEXIT,
	ARM_SEC_MAX,
};

struct mod_arch_specific {
	struct unwind_table *unwind[ARM_SEC_MAX];
};
#endif

/*
 * Add the ARM architecture version to the version magic string
 */
#define MODULE_ARCH_VERMAGIC_ARMVSN "ARMv" __stringify(__LINUX_ARM_ARCH__) " "

/* Add __virt_to_phys patching state as well */
#ifdef CONFIG_ARM_PATCH_PHYS_VIRT
#define MODULE_ARCH_VERMAGIC_P2V "p2v8 "
#else
#define MODULE_ARCH_VERMAGIC_P2V ""
#endif

/* Add instruction set architecture tag to distinguish ARM/Thumb kernels */
#ifdef CONFIG_THUMB2_KERNEL
#define MODULE_ARCH_VERMAGIC_ARMTHUMB "thumb2 "
#else
#define MODULE_ARCH_VERMAGIC_ARMTHUMB ""
#endif

#define MODULE_ARCH_VERMAGIC \
	MODULE_ARCH_VERMAGIC_ARMVSN \
	MODULE_ARCH_VERMAGIC_ARMTHUMB \
	MODULE_ARCH_VERMAGIC_P2V

#endif /* _ASM_ARM_MODULE_H */
