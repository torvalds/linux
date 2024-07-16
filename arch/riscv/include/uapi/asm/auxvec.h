/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2015 Regents of the University of California
 */

#ifndef _UAPI_ASM_RISCV_AUXVEC_H
#define _UAPI_ASM_RISCV_AUXVEC_H

/* vDSO location */
#define AT_SYSINFO_EHDR 33

/*
 * The set of entries below represent more extensive information
 * about the caches, in the form of two entry per cache type,
 * one entry containing the cache size in bytes, and the other
 * containing the cache line size in bytes in the bottom 16 bits
 * and the cache associativity in the next 16 bits.
 *
 * The associativity is such that if N is the 16-bit value, the
 * cache is N way set associative. A value if 0xffff means fully
 * associative, a value of 1 means directly mapped.
 *
 * For all these fields, a value of 0 means that the information
 * is not known.
 */
#define AT_L1I_CACHESIZE	40
#define AT_L1I_CACHEGEOMETRY	41
#define AT_L1D_CACHESIZE	42
#define AT_L1D_CACHEGEOMETRY	43
#define AT_L2_CACHESIZE		44
#define AT_L2_CACHEGEOMETRY	45
#define AT_L3_CACHESIZE		46
#define AT_L3_CACHEGEOMETRY	47

/* entries in ARCH_DLINFO */
#define AT_VECTOR_SIZE_ARCH	9

#endif /* _UAPI_ASM_RISCV_AUXVEC_H */
