/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *
 * Authors: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *          Younghwan Joo <yhwan.joo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef FIMC_IS_REG_H_
#define FIMC_IS_REG_H_

/* WDT_ISP register */
#define REG_WDT_ISP			0x00170000

/* MCUCTL registers base offset */
#define MCUCTL_BASE			0x00180000

/* MCU Controller Register */
#define MCUCTL_REG_MCUCTRL		(MCUCTL_BASE + 0x00)
#define MCUCTRL_MSWRST			(1 << 0)

/* Boot Base Offset Address Register */
#define MCUCTL_REG_BBOAR		(MCUCTL_BASE + 0x04)

/* Interrupt Generation Register 0 from Host CPU to VIC */
#define MCUCTL_REG_INTGR0		(MCUCTL_BASE + 0x08)
/* __n = 0...9 */
#define INTGR0_INTGC(__n)		(1 << ((__n) + 16))
/* __n = 0...5 */
#define INTGR0_INTGD(__n)		(1 << (__n))

/* Interrupt Clear Register 0 from Host CPU to VIC */
#define MCUCTL_REG_INTCR0		(MCUCTL_BASE + 0x0c)
/* __n = 0...9 */
#define INTCR0_INTGC(__n)		(1 << ((__n) + 16))
/* __n = 0...5 */
#define INTCR0_INTCD(__n)		(1 << ((__n) + 16))

/* Interrupt Mask Register 0 from Host CPU to VIC */
#define MCUCTL_REG_INTMR0		(MCUCTL_BASE + 0x10)
/* __n = 0...9 */
#define INTMR0_INTMC(__n)		(1 << ((__n) + 16))
/* __n = 0...5 */
#define INTMR0_INTMD(__n)		(1 << (__n))

/* Interrupt Status Register 0 from Host CPU to VIC */
#define MCUCTL_REG_INTSR0		(MCUCTL_BASE + 0x14)
/* __n (bit number) = 0...4 */
#define INTSR0_GET_INTSD(x, __n)	(((x) >> (__n)) & 0x1)
/* __n (bit number) = 0...9 */
#define INTSR0_GET_INTSC(x, __n)	(((x) >> ((__n) + 16)) & 0x1)

/* Interrupt Mask Status Register 0 from Host CPU to VIC */
#define MCUCTL_REG_INTMSR0		(MCUCTL_BASE + 0x18)
/* __n (bit number) = 0...4 */
#define INTMSR0_GET_INTMSD(x, __n)	(((x) >> (__n)) & 0x1)
/* __n (bit number) = 0...9 */
#define INTMSR0_GET_INTMSC(x, __n)	(((x) >> ((__n) + 16)) & 0x1)

/* Interrupt Generation Register 1 from ISP CPU to Host IC */
#define MCUCTL_REG_INTGR1		(MCUCTL_BASE + 0x1c)
/* __n = 0...9 */
#define INTGR1_INTGC(__n)		(1 << (__n))

/* Interrupt Clear Register 1 from ISP CPU to Host IC */
#define MCUCTL_REG_INTCR1		(MCUCTL_BASE + 0x20)
/* __n = 0...9 */
#define INTCR1_INTCC(__n)		(1 << (__n))

/* Interrupt Mask Register 1 from ISP CPU to Host IC */
#define MCUCTL_REG_INTMR1		(MCUCTL_BASE + 0x24)
/* __n = 0...9 */
#define INTMR1_INTMC(__n)		(1 << (__n))

/* Interrupt Status Register 1 from ISP CPU to Host IC */
#define MCUCTL_REG_INTSR1		(MCUCTL_BASE + 0x28)
/* Interrupt Mask Status Register 1 from ISP CPU to Host IC */
#define MCUCTL_REG_INTMSR1		(MCUCTL_BASE + 0x2c)

/* Interrupt Clear Register 2 from ISP BLK's interrupts to Host IC */
#define MCUCTL_REG_INTCR2		(MCUCTL_BASE + 0x30)
/* __n = 0...5 */
#define INTCR2_INTCC(__n)		(1 << ((__n) + 16))

/* Interrupt Mask Register 2 from ISP BLK's interrupts to Host IC */
#define MCUCTL_REG_INTMR2		(MCUCTL_BASE + 0x34)
/* __n = 0...25 */
#define INTMR2_INTMCIS(__n)		(1 << (__n))

/* Interrupt Status Register 2 from ISP BLK's interrupts to Host IC */
#define MCUCTL_REG_INTSR2		(MCUCTL_BASE + 0x38)
/* Interrupt Mask Status Register 2 from ISP BLK's interrupts to Host IC */
#define MCUCTL_REG_INTMSR2		(MCUCTL_BASE + 0x3c)

/* General Purpose Output Control Register (0~17) */
#define MCUCTL_REG_GPOCTLR		(MCUCTL_BASE + 0x40)
/* __n = 0...17 */
#define GPOCTLR_GPOG(__n)		(1 << (__n))

/* General Purpose Pad Output Enable Register (0~17) */
#define MCUCTL_REG_GPOENCTLR		(MCUCTL_BASE + 0x44)
/* __n = 0...17 */
#define GPOENCTLR_GPOEN(__n)		(1 << (__n))

/* General Purpose Input Control Register (0~17) */
#define MCUCTL_REG_GPICTLR		(MCUCTL_BASE + 0x48)

/* Shared registers between ISP CPU and the host CPU - ISSRxx */

/* ISSR(1): Command Host -> IS */
/* ISSR(1): Sensor ID for Command, ISSR2...5 = Parameter 1...4 */

/* ISSR(10): Reply IS -> Host */
/* ISSR(11): Sensor ID for Reply, ISSR12...15 = Parameter 1...4 */

/* ISSR(20): ISP_FRAME_DONE : SENSOR ID */
/* ISSR(21): ISP_FRAME_DONE : PARAMETER 1 */

/* ISSR(24): SCALERC_FRAME_DONE : SENSOR ID */
/* ISSR(25): SCALERC_FRAME_DONE : PARAMETER 1 */

/* ISSR(28): 3DNR_FRAME_DONE : SENSOR ID */
/* ISSR(29): 3DNR_FRAME_DONE : PARAMETER 1 */

/* ISSR(32): SCALERP_FRAME_DONE : SENSOR ID */
/* ISSR(33): SCALERP_FRAME_DONE : PARAMETER 1 */

/* __n = 0...63 */
#define MCUCTL_REG_ISSR(__n)		(MCUCTL_BASE + 0x80 + ((__n) * 4))

/* PMU ISP register offsets */
#define REG_CMU_RESET_ISP_SYS_PWR_REG	0x1174
#define REG_CMU_SYSCLK_ISP_SYS_PWR_REG	0x13b8
#define REG_PMU_ISP_ARM_SYS		0x1050
#define REG_PMU_ISP_ARM_CONFIGURATION	0x2280
#define REG_PMU_ISP_ARM_STATUS		0x2284
#define REG_PMU_ISP_ARM_OPTION		0x2288

void fimc_is_fw_clear_irq1(struct fimc_is *is, unsigned int bit);
void fimc_is_fw_clear_irq2(struct fimc_is *is);
int fimc_is_hw_get_params(struct fimc_is *is, unsigned int num);

void fimc_is_hw_set_intgr0_gd0(struct fimc_is *is);
int fimc_is_hw_wait_intmsr0_intmsd0(struct fimc_is *is);
void fimc_is_hw_set_sensor_num(struct fimc_is *is);
void fimc_is_hw_set_isp_buf_mask(struct fimc_is *is, unsigned int mask);
void fimc_is_hw_stream_on(struct fimc_is *is);
void fimc_is_hw_stream_off(struct fimc_is *is);
int fimc_is_hw_set_param(struct fimc_is *is);
int fimc_is_hw_change_mode(struct fimc_is *is);

void fimc_is_hw_close_sensor(struct fimc_is *is, unsigned int index);
void fimc_is_hw_get_setfile_addr(struct fimc_is *is);
void fimc_is_hw_load_setfile(struct fimc_is *is);
void fimc_is_hw_subip_power_off(struct fimc_is *is);

int fimc_is_itf_s_param(struct fimc_is *is, bool update);
int fimc_is_itf_mode_change(struct fimc_is *is);

#endif /* FIMC_IS_REG_H_ */
