/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_HWCAP_H
#define _UAPI_ASM_HWCAP_H

/* HWCAP flags */
#define HWCAP_MIPS_R6		(1 << 0)
#define HWCAP_MIPS_MSA		(1 << 1)
#define HWCAP_MIPS_CRC32	(1 << 2)
#define HWCAP_MIPS_MIPS16	(1 << 3)
#define HWCAP_MIPS_MDMX     (1 << 4)
#define HWCAP_MIPS_MIPS3D   (1 << 5)
#define HWCAP_MIPS_SMARTMIPS (1 << 6)
#define HWCAP_MIPS_DSP      (1 << 7)
#define HWCAP_MIPS_DSP2     (1 << 8)
#define HWCAP_MIPS_DSP3     (1 << 9)
#define HWCAP_MIPS_MIPS16E2 (1 << 10)
#define HWCAP_LOONGSON_MMI  (1 << 11)
#define HWCAP_LOONGSON_EXT  (1 << 12)
#define HWCAP_LOONGSON_EXT2 (1 << 13)
#define HWCAP_LOONGSON_CPUCFG (1 << 14)

#endif /* _UAPI_ASM_HWCAP_H */
