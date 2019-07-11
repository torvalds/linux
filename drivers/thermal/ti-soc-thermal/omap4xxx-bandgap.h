/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OMAP4xxx bandgap registers, bitfields and temperature definitions
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Contact:
 *   Eduardo Valentin <eduardo.valentin@ti.com>
 */
#ifndef __OMAP4XXX_BANDGAP_H
#define __OMAP4XXX_BANDGAP_H

/**
 * *** OMAP4430 ***
 *
 * Below, in sequence, are the Register definitions,
 * the bitfields and the temperature definitions for OMAP4430.
 */

/**
 * OMAP4430 register definitions
 *
 * Registers are defined as offsets. The offsets are
 * relative to FUSE_OPP_BGAP on 4430.
 */

/* OMAP4430.FUSE_OPP_BGAP */
#define OMAP4430_FUSE_OPP_BGAP				0x0

/* OMAP4430.TEMP_SENSOR  */
#define OMAP4430_TEMP_SENSOR_CTRL_OFFSET		0xCC

/**
 * Register and bit definitions for OMAP4430
 *
 * All the macros bellow define the required bits for
 * controlling temperature on OMAP4430. Bit defines are
 * grouped by register.
 */

/* OMAP4430.TEMP_SENSOR bits */
#define OMAP4430_BGAP_TEMPSOFF_MASK			BIT(12)
#define OMAP4430_BGAP_TSHUT_MASK			BIT(11)
#define OMAP4430_SINGLE_MODE_MASK			BIT(10)
#define OMAP4430_BGAP_TEMP_SENSOR_SOC_MASK		BIT(9)
#define OMAP4430_BGAP_TEMP_SENSOR_EOCZ_MASK		BIT(8)
#define OMAP4430_BGAP_TEMP_SENSOR_DTEMP_MASK		(0xff << 0)

/**
 * Temperature limits and thresholds for OMAP4430
 *
 * All the macros bellow are definitions for handling the
 * ADC conversions and representation of temperature limits
 * and thresholds for OMAP4430.
 */

/* ADC conversion table limits */
#define OMAP4430_ADC_START_VALUE			0
#define OMAP4430_ADC_END_VALUE				127
/* bandgap clock limits (no control on 4430) */
#define OMAP4430_MAX_FREQ				32768
#define OMAP4430_MIN_FREQ				32768

/**
 * *** OMAP4460 *** Applicable for OMAP4470
 *
 * Below, in sequence, are the Register definitions,
 * the bitfields and the temperature definitions for OMAP4460.
 */

/**
 * OMAP4460 register definitions
 *
 * Registers are defined as offsets. The offsets are
 * relative to FUSE_OPP_BGAP on 4460.
 */

/* OMAP4460.FUSE_OPP_BGAP */
#define OMAP4460_FUSE_OPP_BGAP				0x0

/* OMAP4460.TEMP_SENSOR */
#define OMAP4460_TEMP_SENSOR_CTRL_OFFSET		0xCC

/* OMAP4460.BANDGAP_CTRL */
#define OMAP4460_BGAP_CTRL_OFFSET			0x118

/* OMAP4460.BANDGAP_COUNTER */
#define OMAP4460_BGAP_COUNTER_OFFSET			0x11C

/* OMAP4460.BANDGAP_THRESHOLD */
#define OMAP4460_BGAP_THRESHOLD_OFFSET			0x120

/* OMAP4460.TSHUT_THRESHOLD */
#define OMAP4460_BGAP_TSHUT_OFFSET			0x124

/* OMAP4460.BANDGAP_STATUS */
#define OMAP4460_BGAP_STATUS_OFFSET			0x128

/**
 * Register bitfields for OMAP4460
 *
 * All the macros bellow define the required bits for
 * controlling temperature on OMAP4460. Bit defines are
 * grouped by register.
 */
/* OMAP4460.TEMP_SENSOR bits */
#define OMAP4460_BGAP_TEMPSOFF_MASK			BIT(13)
#define OMAP4460_BGAP_TEMP_SENSOR_SOC_MASK		BIT(11)
#define OMAP4460_BGAP_TEMP_SENSOR_EOCZ_MASK		BIT(10)
#define OMAP4460_BGAP_TEMP_SENSOR_DTEMP_MASK		(0x3ff << 0)

/* OMAP4460.BANDGAP_CTRL bits */
#define OMAP4460_SINGLE_MODE_MASK			BIT(31)
#define OMAP4460_MASK_HOT_MASK				BIT(1)
#define OMAP4460_MASK_COLD_MASK				BIT(0)

/* OMAP4460.BANDGAP_COUNTER bits */
#define OMAP4460_COUNTER_MASK				(0xffffff << 0)

/* OMAP4460.BANDGAP_THRESHOLD bits */
#define OMAP4460_T_HOT_MASK				(0x3ff << 16)
#define OMAP4460_T_COLD_MASK				(0x3ff << 0)

/* OMAP4460.TSHUT_THRESHOLD bits */
#define OMAP4460_TSHUT_HOT_MASK				(0x3ff << 16)
#define OMAP4460_TSHUT_COLD_MASK			(0x3ff << 0)

/* OMAP4460.BANDGAP_STATUS bits */
#define OMAP4460_HOT_FLAG_MASK				BIT(1)
#define OMAP4460_COLD_FLAG_MASK				BIT(0)

/**
 * Temperature limits and thresholds for OMAP4460
 *
 * All the macros bellow are definitions for handling the
 * ADC conversions and representation of temperature limits
 * and thresholds for OMAP4460.
 */

/* ADC conversion table limits */
#define OMAP4460_ADC_START_VALUE			530
#define OMAP4460_ADC_END_VALUE				932
/* bandgap clock limits */
#define OMAP4460_MAX_FREQ				1500000
#define OMAP4460_MIN_FREQ				1000000
/* interrupts thresholds */
#define OMAP4460_TSHUT_HOT				900	/* 122 deg C */
#define OMAP4460_TSHUT_COLD				895	/* 100 deg C */
#define OMAP4460_T_HOT					800	/* 73 deg C */
#define OMAP4460_T_COLD					795	/* 71 deg C */

#endif /* __OMAP4XXX_BANDGAP_H */
