/*
 * pv88090-regulator.h - Regulator definitions for PV88090
 * Copyright (C) 2015 Powerventure Semiconductor Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PV88090_REGISTERS_H__
#define __PV88090_REGISTERS_H__

/* System Control and Event Registers */
#define	PV88090_REG_EVENT_A			0x03
#define	PV88090_REG_MASK_A			0x06
#define	PV88090_REG_MASK_B			0x07

/* Regulator Registers */
#define	PV88090_REG_BUCK1_CONF0			0x18
#define	PV88090_REG_BUCK1_CONF1			0x19
#define	PV88090_REG_BUCK1_CONF2			0x1a
#define	PV88090_REG_BUCK2_CONF0			0x1b
#define	PV88090_REG_BUCK2_CONF1			0x1c
#define	PV88090_REG_BUCK2_CONF2			0x58
#define	PV88090_REG_BUCK3_CONF0			0x1d
#define	PV88090_REG_BUCK3_CONF1			0x1e
#define	PV88090_REG_BUCK3_CONF2			0x5c

#define	PV88090_REG_LDO1_CONT			0x1f
#define	PV88090_REG_LDO2_CONT			0x20
#define	PV88090_REG_LDO3_CONT			0x21
#define	PV88090_REG_BUCK_FOLD_RANGE			0x61

/* PV88090_REG_EVENT_A (addr=0x03) */
#define	PV88090_E_VDD_FLT				0x01
#define	PV88090_E_OVER_TEMP			0x02

/* PV88090_REG_MASK_A (addr=0x06) */
#define	PV88090_M_VDD_FLT				0x01
#define	PV88090_M_OVER_TEMP			0x02

/* PV88090_REG_BUCK1_CONF0 (addr=0x18) */
#define	PV88090_BUCK1_EN				0x80
#define PV88090_VBUCK1_MASK			0x7F
/* PV88090_REG_BUCK2_CONF0 (addr=0x1b) */
#define	PV88090_BUCK2_EN				0x80
#define PV88090_VBUCK2_MASK			0x7F
/* PV88090_REG_BUCK3_CONF0 (addr=0x1d) */
#define	PV88090_BUCK3_EN				0x80
#define PV88090_VBUCK3_MASK			0x7F
/* PV88090_REG_LDO1_CONT (addr=0x1f) */
#define	PV88090_LDO1_EN				0x40
#define PV88090_VLDO1_MASK			0x3F
/* PV88090_REG_LDO2_CONT (addr=0x20) */
#define	PV88090_LDO2_EN				0x40
#define PV88090_VLDO2_MASK			0x3F

/* PV88090_REG_BUCK1_CONF1 (addr=0x19) */
#define PV88090_BUCK1_ILIM_SHIFT			2
#define PV88090_BUCK1_ILIM_MASK			0x7C
#define PV88090_BUCK1_MODE_MASK			0x03

/* PV88090_REG_BUCK2_CONF1 (addr=0x1c) */
#define PV88090_BUCK2_ILIM_SHIFT			2
#define PV88090_BUCK2_ILIM_MASK			0x0C
#define PV88090_BUCK2_MODE_MASK			0x03

/* PV88090_REG_BUCK3_CONF1 (addr=0x1e) */
#define PV88090_BUCK3_ILIM_SHIFT			2
#define PV88090_BUCK3_ILIM_MASK			0x0C
#define PV88090_BUCK3_MODE_MASK			0x03

#define	PV88090_BUCK_MODE_SLEEP			0x00
#define	PV88090_BUCK_MODE_AUTO			0x01
#define	PV88090_BUCK_MODE_SYNC			0x02

/* PV88090_REG_BUCK2_CONF2 (addr=0x58) */
/* PV88090_REG_BUCK3_CONF2 (addr=0x5c) */
#define PV88090_BUCK_VDAC_RANGE_SHIFT			7
#define PV88090_BUCK_VDAC_RANGE_MASK			0x01

#define PV88090_BUCK_VDAC_RANGE_1			0x00
#define PV88090_BUCK_VDAC_RANGE_2			0x01

/* PV88090_REG_BUCK_FOLD_RANGE (addr=0x61) */
#define PV88090_BUCK_VRANGE_GAIN_SHIFT			3
#define PV88090_BUCK_VRANGE_GAIN_MASK			0x01

#define PV88090_BUCK_VRANGE_GAIN_1			0x00
#define PV88090_BUCK_VRANGE_GAIN_2			0x01

#endif	/* __PV88090_REGISTERS_H__ */
