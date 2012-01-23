/*
*******************************************************************************
*           				eBase
*                 the Abstract of Hardware
*
*
*              (c) Copyright 2006-2010, ALL WINNER TECH.
*           								All Rights Reserved
*
* File     :  my_bits_ops.h
* Date     :  2010/11/12 16:45
* By       :  Sam.Wu
* Version  :  V1.00
* Description :
* Update   :  date      author      version     notes
*******************************************************************************
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



