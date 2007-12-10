#ifndef _ARCH_POWERPC_BOOT_LIBFDT_ENV_H
#define _ARCH_POWERPC_BOOT_LIBFDT_ENV_H

#include <types.h>
#include <string.h>

typedef u32 uint32_t;
typedef u64 uint64_t;

#define fdt16_to_cpu(x)		(x)
#define cpu_to_fdt16(x)		(x)
#define fdt32_to_cpu(x)		(x)
#define cpu_to_fdt32(x)		(x)
#define fdt64_to_cpu(x)		(x)
#define cpu_to_fdt64(x)		(x)

#endif /* _ARCH_POWERPC_BOOT_LIBFDT_ENV_H */
