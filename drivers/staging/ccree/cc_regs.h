/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


/*!
 * @file
 * @brief This file contains macro definitions for accessing ARM TrustZone
 *        CryptoCell register space.
 */

#ifndef _CC_REGS_H_
#define _CC_REGS_H_

#include "cc_bitops.h"

#define AXIM_MON_BASE_OFFSET CC_REG_OFFSET(CRY_KERNEL, AXIM_MON_COMP)
#define AXIM_MON_COMP_VALUE GENMASK(DX_AXIM_MON_COMP_VALUE_BIT_SIZE + \
		DX_AXIM_MON_COMP_VALUE_BIT_SHIFT, \
		DX_AXIM_MON_COMP_VALUE_BIT_SHIFT)

/* Register Offset macro */
#define CC_REG_OFFSET(unit_name, reg_name)               \
	(DX_BASE_ ## unit_name + DX_ ## reg_name ## _REG_OFFSET)

#define CC_REG_BIT_SHIFT(reg_name, field_name)               \
	(DX_ ## reg_name ## _ ## field_name ## _BIT_SHIFT)

/* Read-Modify-Write a field of a register */
#define MODIFY_REGISTER_FLD(unitName, regName, fldName, fldVal)         \
do {								            \
	u32 regVal;						    \
	regVal = READ_REGISTER(CC_REG_ADDR(unitName, regName));       \
	CC_REG_FLD_SET(unitName, regName, fldName, regVal, fldVal); \
	WRITE_REGISTER(CC_REG_ADDR(unitName, regName), regVal);       \
} while (0)

/*! Bit fields get */
#define CC_REG_FLD_GET(unit_name, reg_name, fld_name, reg_val)	      \
	(DX_ ## reg_name ## _ ## fld_name ## _BIT_SIZE == 0x20 ?	      \
	reg_val /*!< \internal Optimization for 32b fields */ :			      \
	BITFIELD_GET(reg_val, DX_ ## reg_name ## _ ## fld_name ## _BIT_SHIFT, \
		     DX_ ## reg_name ## _ ## fld_name ## _BIT_SIZE))

/*! Bit fields access */
#define CC_REG_FLD_GET2(unit_name, reg_name, fld_name, reg_val)	      \
	(CC_ ## reg_name ## _ ## fld_name ## _BIT_SIZE == 0x20 ?	      \
	reg_val /*!< \internal Optimization for 32b fields */ :			      \
	BITFIELD_GET(reg_val, CC_ ## reg_name ## _ ## fld_name ## _BIT_SHIFT, \
		     CC_ ## reg_name ## _ ## fld_name ## _BIT_SIZE))

/* yael TBD !!!
 * all HW includes should start with CC_ and not DX_ !!
 */


/*! Bit fields set */
#define CC_REG_FLD_SET(                                               \
	unit_name, reg_name, fld_name, reg_shadow_var, new_fld_val)      \
do {                                                                     \
	if (DX_ ## reg_name ## _ ## fld_name ## _BIT_SIZE == 0x20)       \
		reg_shadow_var = new_fld_val; /*!< \internal Optimization for 32b fields */\
	else                                                             \
		BITFIELD_SET(reg_shadow_var,                             \
			DX_ ## reg_name ## _ ## fld_name ## _BIT_SHIFT,  \
			DX_ ## reg_name ## _ ## fld_name ## _BIT_SIZE,   \
			new_fld_val);                                    \
} while (0)

/*! Bit fields set */
#define CC_REG_FLD_SET2(                                               \
	unit_name, reg_name, fld_name, reg_shadow_var, new_fld_val)      \
do {                                                                     \
	if (CC_ ## reg_name ## _ ## fld_name ## _BIT_SIZE == 0x20)       \
		reg_shadow_var = new_fld_val; /*!< \internal Optimization for 32b fields */\
	else                                                             \
		BITFIELD_SET(reg_shadow_var,                             \
			CC_ ## reg_name ## _ ## fld_name ## _BIT_SHIFT,  \
			CC_ ## reg_name ## _ ## fld_name ## _BIT_SIZE,   \
			new_fld_val);                                    \
} while (0)

/* Usage example:
 * u32 reg_shadow = READ_REGISTER(CC_REG_ADDR(CRY_KERNEL,AES_CONTROL));
 * CC_REG_FLD_SET(CRY_KERNEL,AES_CONTROL,NK_KEY0,reg_shadow, 3);
 * CC_REG_FLD_SET(CRY_KERNEL,AES_CONTROL,NK_KEY1,reg_shadow, 1);
 * WRITE_REGISTER(CC_REG_ADDR(CRY_KERNEL,AES_CONTROL), reg_shadow);
 */

#endif /*_CC_REGS_H_*/
