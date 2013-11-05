#ifndef __MACH_PMU_H
#define __MACH_PMU_H

#include <linux/io.h>

#define PMU_WAKEUP_CFG0         0x0000
#define PMU_WAKEUP_CFG1         0x0004
#define PMU_WAKEUP_CFG2         0x0008
#define PMU_PWRDN_CON           0x000C
#define PMU_PWRDN_ST            0x0010
#define PMU_PWRMODE_CON         0x0014
#define PMU_SFT_CON             0x0018
#define PMU_INT_CON             0x001C
#define PMU_INT_ST              0x0020
#define PMU_GPIO_INT_ST         0x0024
#define PMU_GPIO_2EDGE_INT_ST   0x0028
#define PMU_NOC_REQ             0x002C
#define PMU_NOC_ST              0x0030
#define PMU_POWER_ST            0x0034
#define PMU_OSC_CNT             0x0040
#define PMU_PLLLOCK_CNT         0x0044
#define PMU_PLLRST_CNT          0x0048
#define PMU_STABLE_CNT          0x004C
#define PMU_DDRIO_PWRON_CNT     0x0050
#define PMU_WAKEUP_RST_CLR_CNT  0x005C
#define PMU_DDR_SREF_ST         0x0064
#define PMU_SYS_REG0            0x0070
#define PMU_SYS_REG1            0x0074
#define PMU_SYS_REG2            0x0078
#define PMU_SYS_REG3            0x007C

#define PMU_WAKEUP_CFG_BP       0x0120
#define PMU_PWRDN_CON_BP        0x0124
#define PMU_PWRMODE_CON_BP      0x0128
#define PMU_SFT_CON_BP          0x012C
#define PMU_INT_CON_BP          0x0130
#define PMU_INT_ST_BP           0x0134
#define PMU_PWRDN_ST_BP         0x0138
#define PMU_NOC_REQ_BP          0x0140
#define PMU_NOC_ST_BP           0x0144
#define PMU_POWER_ST_BP         0x0148
#define PMU_OSC_CNT_BP          0x014C
#define PMU_PLLLOCK_CNT_BP      0x0150
#define PMU_PLLRST_CNT_BP       0x0154
#define PMU_SYS_REG0_BP         0x0170
#define PMU_SYS_REG1_BP         0x0174
#define PMU_SYS_REG2_BP         0x0178
#define PMU_SYS_REG3_BP         0x017C

#endif
