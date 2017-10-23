#ifndef _ASM_X86_MODULE_H
#define _ASM_X86_MODULE_H

#include <asm-generic/module.h>
#include <asm/orc_types.h>

struct mod_arch_specific {
#ifdef CONFIG_ORC_UNWINDER
	unsigned int num_orcs;
	int *orc_unwind_ip;
	struct orc_entry *orc_unwind;
#endif
};

#ifdef CONFIG_X86_64
/* X86_64 does not define MODULE_PROC_FAMILY */
#elif defined CONFIG_M486
#define MODULE_PROC_FAMILY "486 "
#elif defined CONFIG_M586
#define MODULE_PROC_FAMILY "586 "
#elif defined CONFIG_M586TSC
#define MODULE_PROC_FAMILY "586TSC "
#elif defined CONFIG_M586MMX
#define MODULE_PROC_FAMILY "586MMX "
#elif defined CONFIG_MCORE2
#define MODULE_PROC_FAMILY "CORE2 "
#elif defined CONFIG_MATOM
#define MODULE_PROC_FAMILY "ATOM "
#elif defined CONFIG_M686
#define MODULE_PROC_FAMILY "686 "
#elif defined CONFIG_MPENTIUMII
#define MODULE_PROC_FAMILY "PENTIUMII "
#elif defined CONFIG_MPENTIUMIII
#define MODULE_PROC_FAMILY "PENTIUMIII "
#elif defined CONFIG_MPENTIUMM
#define MODULE_PROC_FAMILY "PENTIUMM "
#elif defined CONFIG_MPENTIUM4
#define MODULE_PROC_FAMILY "PENTIUM4 "
#elif defined CONFIG_MK6
#define MODULE_PROC_FAMILY "K6 "
#elif defined CONFIG_MK7
#define MODULE_PROC_FAMILY "K7 "
#elif defined CONFIG_MK8
#define MODULE_PROC_FAMILY "K8 "
#elif defined CONFIG_MELAN
#define MODULE_PROC_FAMILY "ELAN "
#elif defined CONFIG_MCRUSOE
#define MODULE_PROC_FAMILY "CRUSOE "
#elif defined CONFIG_MEFFICEON
#define MODULE_PROC_FAMILY "EFFICEON "
#elif defined CONFIG_MWINCHIPC6
#define MODULE_PROC_FAMILY "WINCHIPC6 "
#elif defined CONFIG_MWINCHIP3D
#define MODULE_PROC_FAMILY "WINCHIP3D "
#elif defined CONFIG_MCYRIXIII
#define MODULE_PROC_FAMILY "CYRIXIII "
#elif defined CONFIG_MVIAC3_2
#define MODULE_PROC_FAMILY "VIAC3-2 "
#elif defined CONFIG_MVIAC7
#define MODULE_PROC_FAMILY "VIAC7 "
#elif defined CONFIG_MGEODEGX1
#define MODULE_PROC_FAMILY "GEODEGX1 "
#elif defined CONFIG_MGEODE_LX
#define MODULE_PROC_FAMILY "GEODE "
#else
#error unknown processor family
#endif

#ifdef CONFIG_X86_32
# define MODULE_ARCH_VERMAGIC MODULE_PROC_FAMILY
#endif

#endif /* _ASM_X86_MODULE_H */
