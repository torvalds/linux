/*
 * arch/arm/mach-sun3i/clock/csp_ccmu/ccmu/my_bits_ops.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef MY_BITS_OPS
#define MY_BITS_OPS

#define SET_BIT(regVal, bitPos)    ( (regVal) |= 1U<<(bitPos) )

#define CLEAR_BIT(regVal, bitPos)  ( (regVal) &= ~(1U<<(bitPos)) )

#define TEST_BIT(regVal, bitPos)    ( (regVal) & ( 1U<<(bitPos) ) )

#define BITS_MOD(Len)                 ( ( 1U<<(Len) ) - 1 )

#define BITS_MASK(Len, _pos)          ( ~ ( BITS_MOD(Len)<<(_pos) ) )

#define CLEAR_BITS(regVal, _pos, Len)   (regVal) &= BITS_MASK(Len, _pos)

#define SET_BITS(regVal, _pos, Len, _val)   (regVal) = ( (regVal) & BITS_MASK(Len, _pos) ) | ( ( (_val) & BITS_MOD(Len) )<<(_pos) )

#define TEST_BITS(regVal, _pos, Len, _val)       ( (regVal) & (~BITS_MASK(Len, _pos) ) ) == ( (_val)<<(_pos) )

#define GET_BITS_VAL(regVal, _pos, Len)          ( ( (regVal)>>(_pos) ) & BITS_MOD(Len) )

#define _CONDITION_BIT_SET(isTrue, reg, bitPos) ( (isTrue) ? SET_BIT(reg, bitPos) : CLEAR_BIT(reg, bitPos) )

#endif //#ifndef MY_BITS_OPS



