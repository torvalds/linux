#ifndef _EFUSE_H_
#define _EFUSE_H_

#include <asm/types.h>

/* eFuse controller register */
#define EFUSE_A_SHIFT		(6)
#define EFUSE_A_MASK		(0x3FF)
//#define EFUSE_PD		(1 << 5)
//#define EFUSE_PS		(1 << 4)
#define EFUSE_PGENB		(1 << 3) //active low
#define EFUSE_LOAD		(1 << 2)
#define EFUSE_STROBE		(1 << 1)
#define EFUSE_CSB		(1 << 0) //active low

#define REG_EFUSE_CTRL		(0x0000)
#define REG_EFUSE_DOUT		(0x0004)

#define ARM_LEAKAGE_CH	0
#define GPU_LEAKAGE_CH	1
#define LOG_LEAKAGE_CH	2

#define RK3288_PROCESS_V0	0
#define RK3288_PROCESS_V1	1

int rockchip_efuse_version(void);
int rockchip_process_version(void);
int rockchip_get_leakage(int ch);
#endif
