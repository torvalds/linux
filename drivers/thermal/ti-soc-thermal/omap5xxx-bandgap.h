/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OMAP5xxx bandgap registers, bitfields and temperature definitions
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Contact:
 *   Eduardo Valentin <eduardo.valentin@ti.com>
 */
#ifndef __OMAP5XXX_BANDGAP_H
#define __OMAP5XXX_BANDGAP_H

/**
 * *** OMAP5430 ***
 *
 * Below, in sequence, are the Register definitions,
 * the bitfields and the temperature definitions for OMAP5430.
 */

/**
 * OMAP5430 register definitions
 *
 * Registers are defined as offsets. The offsets are
 * relative to FUSE_OPP_BGAP_GPU on 5430.
 *
 * Register below are grouped by domain (not necessarily in offset order)
 */

/* OMAP5430.GPU register offsets */
#define OMAP5430_FUSE_OPP_BGAP_GPU			0x0
#define OMAP5430_TEMP_SENSOR_GPU_OFFSET			0x150
#define OMAP5430_BGAP_THRESHOLD_GPU_OFFSET		0x1A8
#define OMAP5430_BGAP_TSHUT_GPU_OFFSET			0x1B4
#define OMAP5430_BGAP_DTEMP_GPU_1_OFFSET		0x1F8
#define OMAP5430_BGAP_DTEMP_GPU_2_OFFSET		0x1FC

/* OMAP5430.MPU register offsets */
#define OMAP5430_FUSE_OPP_BGAP_MPU			0x4
#define OMAP5430_TEMP_SENSOR_MPU_OFFSET			0x14C
#define OMAP5430_BGAP_THRESHOLD_MPU_OFFSET		0x1A4
#define OMAP5430_BGAP_TSHUT_MPU_OFFSET			0x1B0
#define OMAP5430_BGAP_DTEMP_MPU_1_OFFSET		0x1E4
#define OMAP5430_BGAP_DTEMP_MPU_2_OFFSET		0x1E8

/* OMAP5430.MPU register offsets */
#define OMAP5430_FUSE_OPP_BGAP_CORE			0x8
#define OMAP5430_TEMP_SENSOR_CORE_OFFSET		0x154
#define OMAP5430_BGAP_THRESHOLD_CORE_OFFSET		0x1AC
#define OMAP5430_BGAP_TSHUT_CORE_OFFSET			0x1B8
#define OMAP5430_BGAP_DTEMP_CORE_1_OFFSET		0x20C
#define OMAP5430_BGAP_DTEMP_CORE_2_OFFSET		0x210

/* OMAP5430.common register offsets */
#define OMAP5430_BGAP_CTRL_OFFSET			0x1A0
#define OMAP5430_BGAP_STATUS_OFFSET			0x1C8

/**
 * Register bitfields for OMAP5430
 *
 * All the macros below define the required bits for
 * controlling temperature on OMAP5430. Bit defines are
 * grouped by register.
 */

/* OMAP5430.TEMP_SENSOR */
#define OMAP5430_BGAP_TEMP_SENSOR_SOC_MASK		BIT(12)
#define OMAP5430_BGAP_TEMPSOFF_MASK			BIT(11)
#define OMAP5430_BGAP_TEMP_SENSOR_EOCZ_MASK		BIT(10)
#define OMAP5430_BGAP_TEMP_SENSOR_DTEMP_MASK		(0x3ff << 0)

/* OMAP5430.BANDGAP_CTRL */
#define OMAP5430_MASK_COUNTER_DELAY_MASK		(0x7 << 27)
#define OMAP5430_MASK_FREEZE_CORE_MASK			BIT(23)
#define OMAP5430_MASK_FREEZE_GPU_MASK			BIT(22)
#define OMAP5430_MASK_FREEZE_MPU_MASK			BIT(21)
#define OMAP5430_MASK_HOT_CORE_MASK			BIT(5)
#define OMAP5430_MASK_COLD_CORE_MASK			BIT(4)
#define OMAP5430_MASK_HOT_GPU_MASK			BIT(3)
#define OMAP5430_MASK_COLD_GPU_MASK			BIT(2)
#define OMAP5430_MASK_HOT_MPU_MASK			BIT(1)
#define OMAP5430_MASK_COLD_MPU_MASK			BIT(0)

/* OMAP5430.BANDGAP_COUNTER */
#define OMAP5430_COUNTER_MASK				(0xffffff << 0)

/* OMAP5430.BANDGAP_THRESHOLD */
#define OMAP5430_T_HOT_MASK				(0x3ff << 16)
#define OMAP5430_T_COLD_MASK				(0x3ff << 0)

/* OMAP5430.TSHUT_THRESHOLD */
#define OMAP5430_TSHUT_HOT_MASK				(0x3ff << 16)
#define OMAP5430_TSHUT_COLD_MASK			(0x3ff << 0)

/* OMAP5430.BANDGAP_STATUS */
#define OMAP5430_HOT_CORE_FLAG_MASK			BIT(5)
#define OMAP5430_COLD_CORE_FLAG_MASK			BIT(4)
#define OMAP5430_HOT_GPU_FLAG_MASK			BIT(3)
#define OMAP5430_COLD_GPU_FLAG_MASK			BIT(2)
#define OMAP5430_HOT_MPU_FLAG_MASK			BIT(1)
#define OMAP5430_COLD_MPU_FLAG_MASK			BIT(0)

/**
 * Temperature limits and thresholds for OMAP5430
 *
 * All the macros below are definitions for handling the
 * ADC conversions and representation of temperature limits
 * and thresholds for OMAP5430. Definitions are grouped
 * by temperature domain.
 */

/* OMAP5430.common temperature definitions */
/* ADC conversion table limits */
#define OMAP5430_ADC_START_VALUE			540
#define OMAP5430_ADC_END_VALUE				945

/* OMAP5430.GPU temperature definitions */
/* bandgap clock limits */
#define OMAP5430_GPU_MAX_FREQ				1500000
#define OMAP5430_GPU_MIN_FREQ				1000000
/* interrupts thresholds */
#define OMAP5430_GPU_TSHUT_HOT				915
#define OMAP5430_GPU_TSHUT_COLD				900
#define OMAP5430_GPU_T_HOT				800
#define OMAP5430_GPU_T_COLD				795

/* OMAP5430.MPU temperature definitions */
/* bandgap clock limits */
#define OMAP5430_MPU_MAX_FREQ				1500000
#define OMAP5430_MPU_MIN_FREQ				1000000
/* interrupts thresholds */
#define OMAP5430_MPU_TSHUT_HOT				915
#define OMAP5430_MPU_TSHUT_COLD				900
#define OMAP5430_MPU_T_HOT				800
#define OMAP5430_MPU_T_COLD				795

/* OMAP5430.CORE temperature definitions */
/* bandgap clock limits */
#define OMAP5430_CORE_MAX_FREQ				1500000
#define OMAP5430_CORE_MIN_FREQ				1000000
/* interrupts thresholds */
#define OMAP5430_CORE_TSHUT_HOT				915
#define OMAP5430_CORE_TSHUT_COLD			900
#define OMAP5430_CORE_T_HOT				800
#define OMAP5430_CORE_T_COLD				795

#endif /* __OMAP5XXX_BANDGAP_H */
