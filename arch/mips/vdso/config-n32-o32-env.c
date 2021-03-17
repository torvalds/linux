// SPDX-License-Identifier: GPL-2.0
/*
 * Configuration file for O32 and N32 binaries.
 * Note: To be included before lib/vdso/gettimeofday.c
 */
#if defined(CONFIG_MIPS32_O32) || defined(CONFIG_MIPS32_N32)
/*
 * In case of a 32 bit VDSO for a 64 bit kernel fake a 32 bit kernel
 * configuration.
 */
#undef CONFIG_64BIT

#define BUILD_VDSO32
#define CONFIG_32BIT 1
#define CONFIG_GENERIC_ATOMIC64 1
#define BUILD_VDSO32_64

#endif

