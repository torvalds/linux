/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_HWCAP_H
#define _UAPI_ASM_HWCAP_H

/* HWCAP flags */
#define HWCAP_LOONGARCH_CPUCFG		(1 << 0)
#define HWCAP_LOONGARCH_LAM		(1 << 1)
#define HWCAP_LOONGARCH_UAL		(1 << 2)
#define HWCAP_LOONGARCH_FPU		(1 << 3)
#define HWCAP_LOONGARCH_LSX		(1 << 4)
#define HWCAP_LOONGARCH_LASX		(1 << 5)
#define HWCAP_LOONGARCH_CRC32		(1 << 6)
#define HWCAP_LOONGARCH_COMPLEX		(1 << 7)
#define HWCAP_LOONGARCH_CRYPTO		(1 << 8)
#define HWCAP_LOONGARCH_LVZ		(1 << 9)
#define HWCAP_LOONGARCH_LBT_X86		(1 << 10)
#define HWCAP_LOONGARCH_LBT_ARM		(1 << 11)
#define HWCAP_LOONGARCH_LBT_MIPS	(1 << 12)

#endif /* _UAPI_ASM_HWCAP_H */
