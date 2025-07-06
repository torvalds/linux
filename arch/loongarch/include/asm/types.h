/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_TYPES_H
#define _ASM_TYPES_H

#include <asm-generic/int-ll64.h>
#include <uapi/asm/types.h>

#ifdef __ASSEMBLER__
#define _ULCAST_
#define _U64CAST_
#else
#define _ULCAST_ (unsigned long)
#define _U64CAST_ (u64)
#endif

#endif /* _ASM_TYPES_H */
