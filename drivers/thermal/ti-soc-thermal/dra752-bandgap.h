/*
 * DRA752 bandgap registers, bitfields and temperature definitions
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Contact:
 *   Eduardo Valentin <eduardo.valentin@ti.com>
 *   Tero Kristo <t-kristo@ti.com>
 *
 * This is an auto generated file.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
#ifndef __DRA752_BANDGAP_H
#define __DRA752_BANDGAP_H

/**
 * *** DRA752 ***
 *
 * Below, in sequence, are the Register definitions,
 * the bitfields and the temperature definitions for DRA752.
 */

/**
 * DRA752 register definitions
 *
 * Registers are defined as offsets. The offsets are
 * relative to FUSE_OPP_BGAP_GPU on DRA752.
 * DRA752_BANDGAP_BASE		0x4a0021e0
 *
 * Register below are grouped by domain (not necessarily in offset order)
 */


/* DRA752.common register offsets */
#define DRA752_BANDGAP_CTRL_1_OFFSET		0x1a0
#define DRA752_BANDGAP_STATUS_1_OFFSET		0x1c8
#define DRA752_BANDGAP_CTRL_2_OFFSET		0x39c
#define DRA752_BANDGAP_STATUS_2_OFFSET		0x3b8

/* DRA752.core register offsets */
#define DRA752_STD_FUSE_OPP_BGAP_CORE_OFFSET		0x8
#define DRA752_TEMP_SENSOR_CORE_OFFSET			0x154
#define DRA752_BANDGAP_THRESHOLD_CORE_OFFSET		0x1ac
#define DRA752_BANDGAP_CUMUL_DTEMP_CORE_OFFSET		0x1c4
#define DRA752_DTEMP_CORE_0_OFFSET			0x208
#define DRA752_DTEMP_CORE_1_OFFSET			0x20c
#define DRA752_DTEMP_CORE_2_OFFSET			0x210
#define DRA752_DTEMP_CORE_3_OFFSET			0x214
#define DRA752_DTEMP_CORE_4_OFFSET			0x218

/* DRA752.iva register offsets */
#define DRA752_STD_FUSE_OPP_BGAP_IVA_OFFSET		0x388
#define DRA752_TEMP_SENSOR_IVA_OFFSET			0x398
#define DRA752_BANDGAP_THRESHOLD_IVA_OFFSET		0x3a4
#define DRA752_BANDGAP_CUMUL_DTEMP_IVA_OFFSET		0x3b4
#define DRA752_DTEMP_IVA_0_OFFSET			0x3d0
#define DRA752_DTEMP_IVA_1_OFFSET			0x3d4
#define DRA752_DTEMP_IVA_2_OFFSET			0x3d8
#define DRA752_DTEMP_IVA_3_OFFSET			0x3dc
#define DRA752_DTEMP_IVA_4_OFFSET			0x3e0

/* DRA752.mpu register offsets */
#define DRA752_STD_FUSE_OPP_BGAP_MPU_OFFSET		0x4
#define DRA752_TEMP_SENSOR_MPU_OFFSET			0x14c
#define DRA752_BANDGAP_THRESHOLD_MPU_OFFSET		0x1a4
#define DRA752_BANDGAP_CUMUL_DTEMP_MPU_OFFSET		0x1bc
#define DRA752_DTEMP_MPU_0_OFFSET			0x1e0
#define DRA752_DTEMP_MPU_1_OFFSET			0x1e4
#define DRA752_DTEMP_MPU_2_OFFSET			0x1e8
#define DRA752_DTEMP_MPU_3_OFFSET			0x1ec
#define DRA752_DTEMP_MPU_4_OFFSET			0x1f0

/* DRA752.dspeve register offsets */
#define DRA752_STD_FUSE_OPP_BGAP_DSPEVE_OFFSET			0x384
#define DRA752_TEMP_SENSOR_DSPEVE_OFFSET			0x394
#define DRA752_BANDGAP_THRESHOLD_DSPEVE_OFFSET			0x3a0
#define DRA752_BANDGAP_CUMUL_DTEMP_DSPEVE_OFFSET		0x3b0
#define DRA752_DTEMP_DSPEVE_0_OFFSET				0x3bc
#define DRA752_DTEMP_DSPEVE_1_OFFSET				0x3c0
#define DRA752_DTEMP_DSPEVE_2_OFFSET				0x3c4
#define DRA752_DTEMP_DSPEVE_3_OFFSET				0x3c8
#define DRA752_DTEMP_DSPEVE_4_OFFSET				0x3cc

/* DRA752.gpu register offsets */
#define DRA752_STD_FUSE_OPP_BGAP_GPU_OFFSET		0x0
#define DRA752_TEMP_SENSOR_GPU_OFFSET			0x150
#define DRA752_BANDGAP_THRESHOLD_GPU_OFFSET		0x1a8
#define DRA752_BANDGAP_CUMUL_DTEMP_GPU_OFFSET		0x1c0
#define DRA752_DTEMP_GPU_0_OFFSET			0x1f4
#define DRA752_DTEMP_GPU_1_OFFSET			0x1f8
#define DRA752_DTEMP_GPU_2_OFFSET			0x1fc
#define DRA752_DTEMP_GPU_3_OFFSET			0x200
#define DRA752_DTEMP_GPU_4_OFFSET			0x204

/**
 * Register bitfields for DRA752
 *
 * All the macros bellow define the required bits for
 * controlling temperature on DRA752. Bit defines are
 * grouped by register.
 */

/* DRA752.BANDGAP_STATUS_1 */
#define DRA752_BANDGAP_STATUS_1_ALERT_MASK		BIT(31)
#define DRA752_BANDGAP_STATUS_1_HOT_CORE_MASK		BIT(5)
#define DRA752_BANDGAP_STATUS_1_COLD_CORE_MASK		BIT(4)
#define DRA752_BANDGAP_STATUS_1_HOT_GPU_MASK		BIT(3)
#define DRA752_BANDGAP_STATUS_1_COLD_GPU_MASK		BIT(2)
#define DRA752_BANDGAP_STATUS_1_HOT_MPU_MASK		BIT(1)
#define DRA752_BANDGAP_STATUS_1_COLD_MPU_MASK		BIT(0)

/* DRA752.BANDGAP_CTRL_2 */
#define DRA752_BANDGAP_CTRL_2_FREEZE_IVA_MASK			BIT(22)
#define DRA752_BANDGAP_CTRL_2_FREEZE_DSPEVE_MASK		BIT(21)
#define DRA752_BANDGAP_CTRL_2_CLEAR_IVA_MASK			BIT(19)
#define DRA752_BANDGAP_CTRL_2_CLEAR_DSPEVE_MASK			BIT(18)
#define DRA752_BANDGAP_CTRL_2_CLEAR_ACCUM_IVA_MASK		BIT(16)
#define DRA752_BANDGAP_CTRL_2_CLEAR_ACCUM_DSPEVE_MASK		BIT(15)
#define DRA752_BANDGAP_CTRL_2_MASK_HOT_IVA_MASK			BIT(3)
#define DRA752_BANDGAP_CTRL_2_MASK_COLD_IVA_MASK		BIT(2)
#define DRA752_BANDGAP_CTRL_2_MASK_HOT_DSPEVE_MASK		BIT(1)
#define DRA752_BANDGAP_CTRL_2_MASK_COLD_DSPEVE_MASK		BIT(0)

/* DRA752.BANDGAP_STATUS_2 */
#define DRA752_BANDGAP_STATUS_2_HOT_IVA_MASK			BIT(3)
#define DRA752_BANDGAP_STATUS_2_COLD_IVA_MASK			BIT(2)
#define DRA752_BANDGAP_STATUS_2_HOT_DSPEVE_MASK			BIT(1)
#define DRA752_BANDGAP_STATUS_2_COLD_DSPEVE_MASK		BIT(0)

/* DRA752.BANDGAP_CTRL_1 */
#define DRA752_BANDGAP_CTRL_1_SIDLEMODE_MASK			(0x3 << 30)
#define DRA752_BANDGAP_CTRL_1_COUNTER_DELAY_MASK		(0x7 << 27)
#define DRA752_BANDGAP_CTRL_1_FREEZE_CORE_MASK			BIT(23)
#define DRA752_BANDGAP_CTRL_1_FREEZE_GPU_MASK			BIT(22)
#define DRA752_BANDGAP_CTRL_1_FREEZE_MPU_MASK			BIT(21)
#define DRA752_BANDGAP_CTRL_1_CLEAR_CORE_MASK			BIT(20)
#define DRA752_BANDGAP_CTRL_1_CLEAR_GPU_MASK			BIT(19)
#define DRA752_BANDGAP_CTRL_1_CLEAR_MPU_MASK			BIT(18)
#define DRA752_BANDGAP_CTRL_1_CLEAR_ACCUM_CORE_MASK		BIT(17)
#define DRA752_BANDGAP_CTRL_1_CLEAR_ACCUM_GPU_MASK		BIT(16)
#define DRA752_BANDGAP_CTRL_1_CLEAR_ACCUM_MPU_MASK		BIT(15)
#define DRA752_BANDGAP_CTRL_1_MASK_HOT_CORE_MASK		BIT(5)
#define DRA752_BANDGAP_CTRL_1_MASK_COLD_CORE_MASK		BIT(4)
#define DRA752_BANDGAP_CTRL_1_MASK_HOT_GPU_MASK			BIT(3)
#define DRA752_BANDGAP_CTRL_1_MASK_COLD_GPU_MASK		BIT(2)
#define DRA752_BANDGAP_CTRL_1_MASK_HOT_MPU_MASK			BIT(1)
#define DRA752_BANDGAP_CTRL_1_MASK_COLD_MPU_MASK		BIT(0)

/* DRA752.TEMP_SENSOR */
#define DRA752_TEMP_SENSOR_TMPSOFF_MASK		BIT(11)
#define DRA752_TEMP_SENSOR_EOCZ_MASK		BIT(10)
#define DRA752_TEMP_SENSOR_DTEMP_MASK		(0x3ff << 0)

/* DRA752.BANDGAP_THRESHOLD */
#define DRA752_BANDGAP_THRESHOLD_HOT_MASK		(0x3ff << 16)
#define DRA752_BANDGAP_THRESHOLD_COLD_MASK		(0x3ff << 0)


/* DRA752.BANDGAP_CUMUL_DTEMP_CORE */
#define DRA752_BANDGAP_CUMUL_DTEMP_CORE_MASK		(0xffffffff << 0)

/* DRA752.BANDGAP_CUMUL_DTEMP_IVA */
#define DRA752_BANDGAP_CUMUL_DTEMP_IVA_MASK		(0xffffffff << 0)

/* DRA752.BANDGAP_CUMUL_DTEMP_MPU */
#define DRA752_BANDGAP_CUMUL_DTEMP_MPU_MASK		(0xffffffff << 0)

/* DRA752.BANDGAP_CUMUL_DTEMP_DSPEVE */
#define DRA752_BANDGAP_CUMUL_DTEMP_DSPEVE_MASK		(0xffffffff << 0)

/* DRA752.BANDGAP_CUMUL_DTEMP_GPU */
#define DRA752_BANDGAP_CUMUL_DTEMP_GPU_MASK		(0xffffffff << 0)

/**
 * Temperature limits and thresholds for DRA752
 *
 * All the macros bellow are definitions for handling the
 * ADC conversions and representation of temperature limits
 * and thresholds for DRA752. Definitions are grouped
 * by temperature domain.
 */

/* DRA752.common temperature definitions */
/* ADC conversion table limits */
#define DRA752_ADC_START_VALUE		540
#define DRA752_ADC_END_VALUE		945

/* DRA752.GPU temperature definitions */
/* bandgap clock limits */
#define DRA752_GPU_MAX_FREQ				1500000
#define DRA752_GPU_MIN_FREQ				1000000
/* sensor limits */
#define DRA752_GPU_MIN_TEMP				-40000
#define DRA752_GPU_MAX_TEMP				125000
#define DRA752_GPU_HYST_VAL				5000
/* interrupts thresholds */
#define DRA752_GPU_T_HOT				800
#define DRA752_GPU_T_COLD				795

/* DRA752.MPU temperature definitions */
/* bandgap clock limits */
#define DRA752_MPU_MAX_FREQ				1500000
#define DRA752_MPU_MIN_FREQ				1000000
/* sensor limits */
#define DRA752_MPU_MIN_TEMP				-40000
#define DRA752_MPU_MAX_TEMP				125000
#define DRA752_MPU_HYST_VAL				5000
/* interrupts thresholds */
#define DRA752_MPU_T_HOT				800
#define DRA752_MPU_T_COLD				795

/* DRA752.CORE temperature definitions */
/* bandgap clock limits */
#define DRA752_CORE_MAX_FREQ				1500000
#define DRA752_CORE_MIN_FREQ				1000000
/* sensor limits */
#define DRA752_CORE_MIN_TEMP				-40000
#define DRA752_CORE_MAX_TEMP				125000
#define DRA752_CORE_HYST_VAL				5000
/* interrupts thresholds */
#define DRA752_CORE_T_HOT				800
#define DRA752_CORE_T_COLD				795

/* DRA752.DSPEVE temperature definitions */
/* bandgap clock limits */
#define DRA752_DSPEVE_MAX_FREQ				1500000
#define DRA752_DSPEVE_MIN_FREQ				1000000
/* sensor limits */
#define DRA752_DSPEVE_MIN_TEMP				-40000
#define DRA752_DSPEVE_MAX_TEMP				125000
#define DRA752_DSPEVE_HYST_VAL				5000
/* interrupts thresholds */
#define DRA752_DSPEVE_T_HOT				800
#define DRA752_DSPEVE_T_COLD				795

/* DRA752.IVA temperature definitions */
/* bandgap clock limits */
#define DRA752_IVA_MAX_FREQ				1500000
#define DRA752_IVA_MIN_FREQ				1000000
/* sensor limits */
#define DRA752_IVA_MIN_TEMP				-40000
#define DRA752_IVA_MAX_TEMP				125000
#define DRA752_IVA_HYST_VAL				5000
/* interrupts thresholds */
#define DRA752_IVA_T_HOT				800
#define DRA752_IVA_T_COLD				795

#endif /* __DRA752_BANDGAP_H */
