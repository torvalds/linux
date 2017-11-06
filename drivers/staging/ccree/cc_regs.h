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

#include <linux/bitfield.h>

#define AXIM_MON_COMP_VALUE GENMASK(DX_AXIM_MON_COMP_VALUE_BIT_SIZE + \
				    DX_AXIM_MON_COMP_VALUE_BIT_SHIFT, \
				    DX_AXIM_MON_COMP_VALUE_BIT_SHIFT)

/* Register name mangling macro */
#define CC_REG(reg_name) DX_ ## reg_name ## _REG_OFFSET

#endif /*_CC_REGS_H_*/
