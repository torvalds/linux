/*
*********************************************************************************************************
*											        eBase
*						                the Abstract of Hardware
*
*
*						        (c) Copyright 2006-2010, holigun China
*											All	Rights Reserved
*
* File    	: 	ebase_base_ops.h
* Date		:	2010-09-25
* By      	: 	holigun
* Version 	: 	V1.00
*********************************************************************************************************
*/
#ifndef	__EBASE_BASE_OPS_H__
#define	__EBASE_BASE_OPS_H__


/*
#define readb(reg)						(*(volatile unsigned char *)(reg))
#define readw(reg)						(*(volatile unsigned short *)(reg))
#define readl(reg)						(*(volatile unsigned long *)(reg))

#define writeb(value,reg)				(*(volatile unsigned char *)(reg) = (value))
#define writew(value,reg)				(*(volatile unsigned short *)(reg) = (value))
#define writel(value,reg)				(*(volatile unsigned long *)(reg) = (value))
*/

#define __REG(x)    (*(volatile unsigned int   *)(x))
#define __REGw(x)   (*(volatile unsigned int   *)(x))
#define __REGhw(x)  (*(volatile unsigned short *)(x))


//===

#define set_bit_b( mask, reg) 			(writeb((readb(reg) | (mask)) , (reg)))
#define set_bit_w( mask, reg) 	 		(writew((readw(reg) | (mask)) , (reg)))
#define clear_bit_b( mask, reg) 	 	(writeb((readb(reg) & (~ (mask))) , (reg)))
#define clear_bit_w( mask, reg) 	 	(writew((readw(reg) & (~ (mask))) , (reg)))

#undef  set_bit
#define set_bit( value, bit )      		( (value) |=  ( 1U << (bit) ) )

#undef  clear_bit
#define clear_bit( value, bit )    		( (value) &= ~( 1U << (bit) ) )

#undef  reverse_bit
#define reverse_bit( value, bit )  		( (value) ^=  ( 1U << (bit) ) )

#undef  test_bit
#define test_bit( value, bit )     		( (value)  &  ( 1U << (bit) ) )



#define _bits_mod(Len)                 ( ( 1U<<(Len) ) - 1 )

#define _bits_mask(Len, _pos)          ( ~ ( _bits_mod(Len)<<(_pos) ) )

#define clear_bits(regVal, _pos, Len)   (regVal) &= _bits_mask(Len, _pos)

#define set_bits(regVal, _pos, Len, _val)   (regVal) = ( (regVal) & _bits_mask(Len, _pos) ) | ( ( (_val) & _bits_mod(Len) )<<(_pos) )

#define test_bits(regVal, _pos, Len, _val)       ( (regVal) & (~ _bits_mask(Len, _pos) ) ) == ( (_val)<<(_pos) )




#undef  min
#define min( x, y )          			( (x) < (y) ? (x) : (y) )

#undef  max
#define max( x, y )          			( (x) > (y) ? (x) : (y) )


#endif	//__EBASE_BASE_OPS_H__

