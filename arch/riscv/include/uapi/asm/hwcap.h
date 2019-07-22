/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copied from arch/arm64/include/asm/hwcap.h
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */
#ifndef __UAPI_ASM_HWCAP_H
#define __UAPI_ASM_HWCAP_H

/*
 * Linux saves the floating-point registers according to the ISA Linux is
 * executing on, as opposed to the ISA the user program is compiled for.  This
 * is necessary for a handful of esoteric use cases: for example, userpsace
 * threading libraries must be able to examine the actual machine state in
 * order to fully reconstruct the state of a thread.
 */
#define COMPAT_HWCAP_ISA_I	(1 << ('I' - 'A'))
#define COMPAT_HWCAP_ISA_M	(1 << ('M' - 'A'))
#define COMPAT_HWCAP_ISA_A	(1 << ('A' - 'A'))
#define COMPAT_HWCAP_ISA_F	(1 << ('F' - 'A'))
#define COMPAT_HWCAP_ISA_D	(1 << ('D' - 'A'))
#define COMPAT_HWCAP_ISA_C	(1 << ('C' - 'A'))

#endif
