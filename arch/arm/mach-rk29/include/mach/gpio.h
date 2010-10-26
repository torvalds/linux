/* arch/arm/mach-rk29/include/mach/gpio.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __ARCH_ARM_MACH_RK29_GPIO_H
#define __ARCH_ARM_MACH_RK29_GPIO_H

typedef enum eGPIOPinLevel
{
	GPIO_LOW=0,
	GPIO_HIGH
}eGPIOPinLevel_t;

typedef enum eGPIOPinDirection
{
	GPIO_IN=0,
	GPIO_OUT
}eGPIOPinDirection_t;

typedef enum GPIOIntType {
	GPIOLevelLow=0,
	GPIOLevelHigh,	 
	GPIOEdgelFalling,
	GPIOEdgelRising
}eGPIOIntType_t;

//定义GPIO相关寄存器偏移地址
#define 	GPIO_SWPORT_DR		0x00
#define 	GPIO_SWPORT_DDR		0x04
#define 	GPIO_INTEN 			0x30
#define 	GPIO_INTMASK 		0x34
#define 	GPIO_INTTYPE_LEVEL 	0x38
#define 	GPIO_INT_POLARITY 	0x3c
#define 	GPIO_INT_STATUS 	0x40
#define 	GPIO_INT_RAWSTATUS  0x44
#define 	GPIO_DEBOUNCE 		0x48
#define 	GPIO_PORTS_EOI 		0x4c
#define 	GPIO_EXT_PORT		0x50
#define 	GPIO_LS_SYNC 		0x60

#define RK29_ID_GPIO0			0
#define RK29_ID_GPIO1			1
#define RK29_ID_GPIO2			2
#define RK29_ID_GPIO3			3
#define RK29_ID_GPIO4			4
#define RK29_ID_GPIO5			5
#define RK29_ID_GPIO6			6

#define NUM_GROUP				8
#define PIN_BASE				0
#define MAX_BANK				7

#define	RK29_PIN0_PA0		(0*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN0_PA1		(0*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN0_PA2		(0*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN0_PA3		(0*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN0_PA4		(0*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN0_PA5		(0*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN0_PA6		(0*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN0_PA7		(0*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN0_PB0		(1*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN0_PB1		(1*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN0_PB2		(1*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN0_PB3		(1*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN0_PB4		(1*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN0_PB5		(1*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN0_PB6		(1*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN0_PB7		(1*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN0_PC0		(2*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN0_PC1		(2*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN0_PC2		(2*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN0_PC3		(2*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN0_PC4		(2*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN0_PC5		(2*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN0_PC6		(2*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN0_PC7		(2*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN0_PD0		(3*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN0_PD1		(3*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN0_PD2		(3*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN0_PD3		(3*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN0_PD4		(3*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN0_PD5		(3*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN0_PD6		(3*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN0_PD7		(3*NUM_GROUP + PIN_BASE + 7);

#define	RK29_PIN1_PA0		(4*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN1_PA1		(4*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN1_PA2		(4*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN1_PA3		(4*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN1_PA4		(4*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN1_PA5		(4*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN1_PA6		(4*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN1_PA7		(4*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN1_PB0		(5*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN1_PB1		(5*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN1_PB2		(5*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN1_PB3		(5*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN1_PB4		(5*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN1_PB5		(5*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN1_PB6		(5*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN1_PB7		(5*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN1_PC0		(6*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN1_PC1		(6*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN1_PC2		(6*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN1_PC3		(6*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN1_PC4		(6*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN1_PC5		(6*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN1_PC6		(6*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN1_PC7		(6*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN1_PD0		(7*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN1_PD1		(7*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN1_PD2		(7*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN1_PD3		(7*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN1_PD4		(7*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN1_PD5		(7*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN1_PD6		(7*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN1_PD7		(7*NUM_GROUP + PIN_BASE + 7);

#define	RK29_PIN2_PA0		(8*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN2_PA1		(8*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN2_PA2		(8*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN2_PA3		(8*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN2_PA4		(8*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN2_PA5		(8*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN2_PA6		(8*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN2_PA7		(8*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN2_PB0		(9*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN2_PB1		(9*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN2_PB2		(9*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN2_PB3		(9*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN2_PB4		(9*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN2_PB5		(9*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN2_PB6		(9*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN2_PB7		(9*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN2_PC0		(10*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN2_PC1		(10*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN2_PC2		(10*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN2_PC3		(10*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN2_PC4		(10*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN2_PC5		(10*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN2_PC6		(10*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN2_PC7		(10*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN2_PD0		(11*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN2_PD1		(11*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN2_PD2		(11*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN2_PD3		(11*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN2_PD4		(11*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN2_PD5		(11*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN2_PD6		(11*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN2_PD7		(11*NUM_GROUP + PIN_BASE + 7);

#define	RK29_PIN3_PA0		(12*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN3_PA1		(12*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN3_PA2		(12*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN3_PA3		(12*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN3_PA4		(12*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN3_PA5		(12*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN3_PA6		(12*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN3_PA7		(12*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN3_PB0		(13*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN3_PB1		(13*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN3_PB2		(13*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN3_PB3		(13*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN3_PB4		(13*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN3_PB5		(13*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN3_PB6		(13*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN3_PB7		(13*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN3_PC0		(14*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN3_PC1		(14*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN3_PC2		(14*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN3_PC3		(14*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN3_PC4		(14*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN3_PC5		(14*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN3_PC6		(14*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN3_PC7		(14*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN3_PD0		(15*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN3_PD1		(15*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN3_PD2		(15*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN3_PD3		(15*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN3_PD4		(15*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN3_PD5		(15*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN3_PD6		(15*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN3_PD7		(15*NUM_GROUP + PIN_BASE + 7);

#define	RK29_PIN4_PA0		(16*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN4_PA1		(16*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN4_PA2		(16*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN4_PA3		(16*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN4_PA4		(16*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN4_PA5		(16*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN4_PA6		(16*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN4_PA7		(16*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN4_PB0		(17*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN4_PB1		(17*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN4_PB2		(17*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN4_PB3		(17*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN4_PB4		(17*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN4_PB5		(17*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN4_PB6		(17*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN4_PB7		(17*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN4_PC0		(18*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN4_PC1		(18*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN4_PC2		(18*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN4_PC3		(18*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN4_PC4		(18*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN4_PC5		(18*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN4_PC6		(18*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN4_PC7		(18*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN4_PD0		(19*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN4_PD1		(19*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN4_PD2		(19*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN4_PD3		(19*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN4_PD4		(19*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN4_PD5		(19*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN4_PD6		(19*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN4_PD7		(19*NUM_GROUP + PIN_BASE + 7);

#define	RK29_PIN5_PA0		(20*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN5_PA1		(20*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN5_PA2		(20*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN5_PA3		(20*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN5_PA4		(20*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN5_PA5		(20*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN5_PA6		(20*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN5_PA7		(20*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN5_PB0		(21*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN5_PB1		(21*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN5_PB2		(21*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN5_PB3		(21*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN5_PB4		(21*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN5_PB5		(21*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN5_PB6		(21*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN5_PB7		(21*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN5_PC0		(22*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN5_PC1		(22*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN5_PC2		(22*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN5_PC3		(22*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN5_PC4		(22*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN5_PC5		(22*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN5_PC6		(22*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN5_PC7		(22*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN5_PD0		(23*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN5_PD1		(23*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN5_PD2		(23*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN5_PD3		(23*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN5_PD4		(23*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN5_PD5		(23*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN5_PD6		(23*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN5_PD7		(23*NUM_GROUP + PIN_BASE + 7);

#define	RK29_PIN6_PA0		(24*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN6_PA1		(24*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN6_PA2		(24*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN6_PA3		(24*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN6_PA4		(24*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN6_PA5		(24*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN6_PA6		(24*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN6_PA7		(24*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN6_PB0		(25*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN6_PB1		(25*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN6_PB2		(25*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN6_PB3		(25*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN6_PB4		(25*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN6_PB5		(25*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN6_PB6		(25*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN6_PB7		(25*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN6_PC0		(26*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN6_PC1		(26*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN6_PC2		(26*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN6_PC3		(26*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN6_PC4		(26*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN6_PC5		(26*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN6_PC6		(26*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN6_PC7		(26*NUM_GROUP + PIN_BASE + 7);
#define	RK29_PIN6_PD0		(27*NUM_GROUP + PIN_BASE + 0);
#define	RK29_PIN6_PD1		(27*NUM_GROUP + PIN_BASE + 1);
#define	RK29_PIN6_PD2		(27*NUM_GROUP + PIN_BASE + 2);
#define	RK29_PIN6_PD3		(27*NUM_GROUP + PIN_BASE + 3);
#define	RK29_PIN6_PD4		(27*NUM_GROUP + PIN_BASE + 4);
#define	RK29_PIN6_PD5		(27*NUM_GROUP + PIN_BASE + 5);
#define	RK29_PIN6_PD6		(27*NUM_GROUP + PIN_BASE + 6);
#define	RK29_PIN6_PD7		(27*NUM_GROUP + PIN_BASE + 7);
                                           
#define ARCH_NR_GPIOS 		(NUM_GROUP*MAX_BANK*4)
                                           
struct rk29_gpio_bank {                                                          
	unsigned short id;			                          
	unsigned long offset;		                                     
};                                                                               
     
#ifndef __ASSEMBLY__
extern void __init rk29_gpio_init(struct rk29_gpio_bank *data, int nr_banks);  
extern void __init rk29_gpio_irq_setup(void); 
/*-------------------------------------------------------------------------*/

/* wrappers for "new style" GPIO calls. the old RK2818-specfic ones should
 * eventually be removed (along with this errno.h inclusion), and the
 * gpio request/free calls should probably be implemented.
 */

#include <asm/errno.h>
#include <asm-generic/gpio.h>		/* cansleep wrappers */

#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep

static inline int gpio_to_irq(unsigned gpio)
{
	return gpio;
}

static inline int irq_to_gpio(unsigned irq)
{
	return irq;       
}

#endif	/* __ASSEMBLY__ */     
                                                                                                                                                             
#endif                                                                                              
                                                                                 
