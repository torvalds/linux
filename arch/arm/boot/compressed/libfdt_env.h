/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARM_LIBFDT_ENV_H
#define _ARM_LIBFDT_ENV_H

#include <linux/limits.h>
#include <linux/types.h>
#include <linux/string.h>
#include <asm/byteorder.h>

#define INT32_MAX	S32_MAX
#define UINT32_MAX	U32_MAX

typedef __be16 fdt16_t;
typedef __be32 fdt32_t;
typedef __be64 fdt64_t;

#define fdt16_to_cpu(x)		be16_to_cpu(x)
#define cpu_to_fdt16(x)		cpu_to_be16(x)
#define fdt32_to_cpu(x)		be32_to_cpu(x)
#define cpu_to_fdt32(x)		cpu_to_be32(x)
#define fdt64_to_cpu(x)		be64_to_cpu(x)
#define cpu_to_fdt64(x)		cpu_to_be64(x)

#endif
