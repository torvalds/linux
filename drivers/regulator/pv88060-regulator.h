/*
 * pv88060-regulator.h - Regulator definitions for PV88060
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

#ifndef __PV88060_REGISTERS_H__
#define __PV88060_REGISTERS_H__

/* System Control and Event Registers */
#define	PV88060_REG_EVENT_A			0x04
#define	PV88060_REG_MASK_A			0x08
#define	PV88060_REG_MASK_B			0x09
#define	PV88060_REG_MASK_C			0x0A

/* Regulator Registers */
#define	PV88060_REG_BUCK1_CONF0			0x1B
#define	PV88060_REG_BUCK1_CONF1			0x1C
#define	PV88060_REG_LDO1_CONF			0x1D
#define	PV88060_REG_LDO2_CONF			0x1E
#define	PV88060_REG_LDO3_CONF			0x1F
#define	PV88060_REG_LDO4_CONF			0x20
#define	PV88060_REG_LDO5_CONF			0x21
#define	PV88060_REG_LDO6_CONF			0x22
#define	PV88060_REG_LDO7_CONF			0x23

#define	PV88060_REG_SW1_CONF			0x3B
#define	PV88060_REG_SW2_CONF			0x3C
#define	PV88060_REG_SW3_CONF			0x3D
#define	PV88060_REG_SW4_CONF			0x3E
#define	PV88060_REG_SW5_CONF			0x3F
#define	PV88060_REG_SW6_CONF			0x40

/* PV88060_REG_EVENT_A (addr=0x04) */
#define	PV88060_E_VDD_FLT			0x01
#define	PV88060_E_OVER_TEMP			0x02

/* PV88060_REG_MASK_A (addr=0x08) */
#define	PV88060_M_VDD_FLT			0x01
#define	PV88060_M_OVER_TEMP			0x02

/* PV88060_REG_BUCK1_CONF0 (addr=0x1B) */
#define	PV88060_BUCK_EN			0x80
#define PV88060_VBUCK_MASK			0x7F
/* PV88060_REG_LDO1/2/3/4/5/6/7_CONT */
#define	PV88060_LDO_EN			0x40
#define PV88060_VLDO_MASK			0x3F
/* PV88060_REG_SW1/2/3/4/5_CONF */
#define	PV88060_SW_EN			0x80

/* PV88060_REG_BUCK1_CONF1 (addr=0x1C) */
#define	PV88060_BUCK_ILIM_SHIFT			2
#define	PV88060_BUCK_ILIM_MASK			0x0C
#define	PV88060_BUCK_MODE_SHIFT			0
#define	PV88060_BUCK_MODE_MASK			0x03
#define	PV88060_BUCK_MODE_SLEEP			0x00
#define	PV88060_BUCK_MODE_AUTO			0x01
#define	PV88060_BUCK_MODE_SYNC			0x02

#endif	/* __PV88060_REGISTERS_H__ */
