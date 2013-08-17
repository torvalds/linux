/*
 * Samsung Exynos4 SoC series FIMC-IS slave interface driver
 *
 * Register definitions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Younghwan Joo <yhwan.joo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_REG_H_
#define FIMC_IS_REG_H_

#include <mach/map.h>

/* WDT_ISP register */
#define WDT_ISP				0x00170000

/* MCUCTL register */
#define MCUCTL				0x00180000

/* MCU Controller Register */
#define MCUCTLR				(MCUCTL + 0x00)
#define MCUCTLR_AXI_ISPX_AWCACHE(x)	((x) << 16)
#define MCUCTLR_AXI_ISPX_ARCACHE(x)	((x) << 12)

#define MCUCTLR_MSWRST			(1 << 0)

/* Boot Base Offset Address Register */
#define BBOAR				(MCUCTL + 0x04)
#define BBOAR_BBOA(x)			((x) << 0)

/* Interrupt Generation Register 0 from Host CPU to VIC */
#define INTGR0				(MCUCTL + 0x08)
/* __n = 0...9 */
#define INTGR0_INTGC(__n)		(1 << ((__n) + 16))
/* __n = 0...5 */
#define INTGR0_INTGD(__n)		(1 << (__n))

/* Interrupt Clear Register 0 from Host CPU to VIC */
#define INTCR0				(MCUCTL + 0x0c)
/* __n = 0...9 */
#define INTCR0_INTGC(__n)		(1 << ((__n) + 16))
/* __n = 0...5 */
#define INTCR0_INTCD(__n)		(1 << ((__n) + 16))

/* Interrupt Mask Register 0 from Host CPU to VIC */
#define INTMR0				(MCUCTL + 0x10)
/* __n = 0...9 */
#define INTMR0_INTMC(__n)		(1 << ((__n) + 16))
/* __n = 0...5 */
#define INTMR0_INTMD(__n)		(1 << (__n))

/* Interrupt Status Register 0 from Host CPU to VIC */
#define INTSR0				(MCUCTL + 0x14)
/* __n (bit number) = 0...4 */
#define INTSR0_GET_INTSD(x, __n)	(((x) >> (__n)) & 0x1)
/* __n (bit number) = 0...9 */
#define INTSR0_GET_INTSC(x, __n)	(((x) >> ((__n) + 16)) & 0x1)

/* Interrupt Mask Status Register 0 from Host CPU to VIC */
#define INTMSR0				(MCUCTL + 0x18)
/* __n (bit number) = 0...4 */
#define INTMSR0_GET_INTMSD(x, __n)	(((x) >> (__n)) & 0x1)
/* __n (bit number) = 0...9 */
#define INTMSR0_GET_INTMSC(x, __n)	(((x) >> ((__n) + 16)) & 0x1)

/* Interrupt Generation Register 1 from ISP CPU to Host IC */
#define INTGR1				(MCUCTL + 0x1c)
/* __n = 0...9 */
#define INTGR1_INTGC(__n)		(1 << (__n))

/* Interrupt Clear Register 1 from ISP CPU to Host IC */
#define INTCR1				(MCUCTL + 0x20)
/* __n = 0...9 */
#define INTCR1_INTCC(__n)		(1 << (__n))

/* Interrupt Mask Register 1 from ISP CPU to Host IC */
#define INTMR1				(MCUCTL + 0x24)
/* __n = 0...9 */
#define INTMR1_INTMC(__n)		(1 << (__n))

/* Interrupt Status Register 1 from ISP CPU to Host IC */
#define INTSR1				(MCUCTL + 0x28)
/* Interrupt Mask Status Register 1 from ISP CPU to Host IC */
#define INTMSR1				(MCUCTL + 0x2c)

/* Interrupt Clear Register 2 from ISP BLK's interrupts to Host IC */
#define INTCR2				(MCUCTL + 0x30)
/* __n = 0...5 */
#define INTCR2_INTCC(__n)		(1 << ((__n) + 16))

/* Interrupt Mask Register 2 from ISP BLK's interrupts to Host IC */
#define INTMR2				(MCUCTL + 0x34)
/* __n = 0...25 */
#define INTMR2_INTMCIS(__n)		(1 << (__n))

/* Interrupt Status Register 2 from ISP BLK's interrupts to Host IC */
#define INTSR2				(MCUCTL + 0x38)
/* Interrupt Mask Status Register 2 from ISP BLK's interrupts to Host IC */
#define INTMSR2				(MCUCTL + 0x3c)

/* General Purpose Output Control Register (0~17) */
#define GPOCTLR				(MCUCTL + 0x40)
/* __n = 0...17 */
#define GPOCTLR_GPOG(__n)		(1 << (__n))

/* General Purpose Pad Output Enable Register (0~17) */
#define GPOENCTLR			(MCUCTL + 0x44)
/* __n = 0...17 */
#define GPOENCTLR_GPOEN(__n)		(1 << (__n))

/* General Purpose Input Control Register (0~17) */
#define GPICTLR				(MCUCTL + 0x48)

/*
 * Shared registers between ISP CPU and the host CPU - ISSRxx
 *
 * ISSR(1): Command Host -> IS
 * ISSR(1): Sensor ID for Command, ISSR2...5 = Parameter 1...4
 *
 * ISSR(10): Reply IS -> Host
 * ISSR(11): Sensor ID for Reply, ISSR12...15 = Parameter 1...4
 *
 * ISSR(20): ISP_FRAME_DONE : SENSOR ID
 * ISSR(21): ISP_FRAME_DONE : PARAMETER 1
 *
 * ISSR(24): SCALERC_FRAME_DONE : SENSOR ID
 * ISSR(25): SCALERC_FRAME_DONE : PARAMETER 1
 *
 * ISSR(28): 3DNR_FRAME_DONE : SENSOR ID
 * ISSR(29): 3DNR_FRAME_DONE : PARAMETER 1
 *
 * ISSR(32): SCALERP_FRAME_DONE : SENSOR ID
 * ISSR(33): SCALERP_FRAME_DONE : PARAMETER 1
 */

/* __n = 0...63 */
#define ISSR(__n)			(MCUCTL + 0x80 + ((__n) * 4))

/* FIXME: Add proper API for ISP at the PMU driver */
#define S5P_LPI_MASK0				(S5P_VA_PMU + 0x0004)
#define PMUREG_CMU_RESET_ISP_SYS_PWR_REG	(S5P_VA_PMU + 0x1174)
#define PMUREG_ISP_ARM_CONFIGURATION		(S5P_VA_PMU + 0x2280)
#define PMUREG_ISP_ARM_STATUS			(S5P_VA_PMU + 0x2284)
#define PMUREG_ISP_ARM_OPTION			(S5P_VA_PMU + 0x2288)
#define PMUREG_ISP_ARM_SYS			(S5P_VA_PMU + 0x1050)
#define PMUREG_CMU_SYSCLK_ISP_SYS_PWR_REG	(S5P_VA_PMU + 0x13B8)

void fimc_is_fw_clear_irq1(struct fimc_is *is, unsigned int intr_pos);
void fimc_is_fw_clear_irq2(struct fimc_is *is);
int fimc_is_hw_get_params(struct fimc_is *is, unsigned int num_args);

void fimc_is_hw_set_intgr0_gd0(struct fimc_is *is);
int fimc_is_hw_wait_intsr0_intsd0(struct fimc_is *is);
int fimc_is_hw_wait_intmsr0_intmsd0(struct fimc_is *is);
void fimc_is_hw_set_sensor_num(struct fimc_is *is);
void fimc_is_hw_set_stream(struct fimc_is *is, int on);
int fimc_is_hw_set_param(struct fimc_is *is);
void fimc_is_hw_change_mode(struct fimc_is *is);

void fimc_is_hw_close_sensor(struct fimc_is *is, u32 id);
void fimc_is_hw_get_setfile_addr(struct fimc_is *is);
void fimc_is_hw_load_setfile(struct fimc_is *is);
void fimc_is_hw_subip_power_off(struct fimc_is *is);

int fimc_is_itf_s_param(struct fimc_is *is, bool update_flg);
int fimc_is_itf_mode_change(struct fimc_is *is);
#endif /* FIMC_IS_REG_H_ */
