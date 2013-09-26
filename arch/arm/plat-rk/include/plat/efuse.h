#ifndef __PLAT_EFUSE_H
#define __PLAT_EFUSE_H

#include <asm/types.h>

/* eFuse controller register */
#if defined(CONFIG_ARCH_RK3026)
#define EFUSE_A_SHIFT		(8)
#else
#define EFUSE_A_SHIFT		(6)
#endif
#define EFUSE_A_MASK		(0xFF)
//#define EFUSE_PD		(1 << 5)
//#define EFUSE_PS		(1 << 4)
#define EFUSE_PGENB		(1 << 3) //active low
#define EFUSE_LOAD		(1 << 2)
#define EFUSE_STROBE		(1 << 1)
#define EFUSE_CSB		(1 << 0) //active low

#define REG_EFUSE_CTRL		(0x0000)
#define REG_EFUSE_DOUT		(0x0004)

/* Interfaces to get efuse informations */
void rk_efuse_init(void);
int rk_pll_flag(void);
int rk_tflag(void);
int rk_leakage_val(void);
int rk3028_version_val(void);
int rk3026_version_val(void);

#endif
