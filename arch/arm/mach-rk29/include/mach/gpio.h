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
#include <asm/irq.h>
 
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

typedef enum GPIOPullType {
	PullDisable = 0,
	PullEnable,
	GPIONormal,  //PullEnable, please do not use it
	GPIOPullUp,	//PullEnable, please do not use it
	GPIOPullDown,//PullEnable, please do not use it
	GPIONOInit,//PullEnable, please do not use it
}eGPIOPullType_t;

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

#define NUM_GROUP				32
#define PIN_BASE				0
#define MAX_BANK				7

#define	RK29_PIN0_PA0		(0*NUM_GROUP + PIN_BASE + 0)
#define	RK29_PIN0_PA1		(0*NUM_GROUP + PIN_BASE + 1)
#define	RK29_PIN0_PA2		(0*NUM_GROUP + PIN_BASE + 2)
#define	RK29_PIN0_PA3		(0*NUM_GROUP + PIN_BASE + 3)
#define	RK29_PIN0_PA4		(0*NUM_GROUP + PIN_BASE + 4)
#define	RK29_PIN0_PA5		(0*NUM_GROUP + PIN_BASE + 5)
#define	RK29_PIN0_PA6		(0*NUM_GROUP + PIN_BASE + 6)
#define	RK29_PIN0_PA7		(0*NUM_GROUP + PIN_BASE + 7)
#define	RK29_PIN0_PB0		(0*NUM_GROUP + PIN_BASE + 8)
#define	RK29_PIN0_PB1		(0*NUM_GROUP + PIN_BASE + 9)
#define	RK29_PIN0_PB2		(0*NUM_GROUP + PIN_BASE + 10)
#define	RK29_PIN0_PB3		(0*NUM_GROUP + PIN_BASE + 11)
#define	RK29_PIN0_PB4		(0*NUM_GROUP + PIN_BASE + 12)
#define	RK29_PIN0_PB5		(0*NUM_GROUP + PIN_BASE + 13)
#define	RK29_PIN0_PB6		(0*NUM_GROUP + PIN_BASE + 14)
#define	RK29_PIN0_PB7		(0*NUM_GROUP + PIN_BASE + 15)
#define	RK29_PIN0_PC0		(0*NUM_GROUP + PIN_BASE + 16)
#define	RK29_PIN0_PC1		(0*NUM_GROUP + PIN_BASE + 17)
#define	RK29_PIN0_PC2		(0*NUM_GROUP + PIN_BASE + 18)
#define	RK29_PIN0_PC3		(0*NUM_GROUP + PIN_BASE + 19)
#define	RK29_PIN0_PC4		(0*NUM_GROUP + PIN_BASE + 20)
#define	RK29_PIN0_PC5		(0*NUM_GROUP + PIN_BASE + 21)
#define	RK29_PIN0_PC6		(0*NUM_GROUP + PIN_BASE + 22)
#define	RK29_PIN0_PC7		(0*NUM_GROUP + PIN_BASE + 23)
#define	RK29_PIN0_PD0		(0*NUM_GROUP + PIN_BASE + 24)
#define	RK29_PIN0_PD1		(0*NUM_GROUP + PIN_BASE + 25)
#define	RK29_PIN0_PD2		(0*NUM_GROUP + PIN_BASE + 26)
#define	RK29_PIN0_PD3		(0*NUM_GROUP + PIN_BASE + 27)
#define	RK29_PIN0_PD4		(0*NUM_GROUP + PIN_BASE + 28)
#define	RK29_PIN0_PD5		(0*NUM_GROUP + PIN_BASE + 29)
#define	RK29_PIN0_PD6		(0*NUM_GROUP + PIN_BASE + 30)
#define	RK29_PIN0_PD7		(0*NUM_GROUP + PIN_BASE + 31)

#define	RK29_PIN1_PA0		(1*NUM_GROUP + PIN_BASE + 0) 
#define	RK29_PIN1_PA1		(1*NUM_GROUP + PIN_BASE + 1) 
#define	RK29_PIN1_PA2		(1*NUM_GROUP + PIN_BASE + 2) 
#define	RK29_PIN1_PA3		(1*NUM_GROUP + PIN_BASE + 3) 
#define	RK29_PIN1_PA4		(1*NUM_GROUP + PIN_BASE + 4) 
#define	RK29_PIN1_PA5		(1*NUM_GROUP + PIN_BASE + 5) 
#define	RK29_PIN1_PA6		(1*NUM_GROUP + PIN_BASE + 6) 
#define	RK29_PIN1_PA7		(1*NUM_GROUP + PIN_BASE + 7) 
#define	RK29_PIN1_PB0		(1*NUM_GROUP + PIN_BASE + 8) 
#define	RK29_PIN1_PB1		(1*NUM_GROUP + PIN_BASE + 9) 
#define	RK29_PIN1_PB2		(1*NUM_GROUP + PIN_BASE + 10)
#define	RK29_PIN1_PB3		(1*NUM_GROUP + PIN_BASE + 11)
#define	RK29_PIN1_PB4		(1*NUM_GROUP + PIN_BASE + 12)
#define	RK29_PIN1_PB5		(1*NUM_GROUP + PIN_BASE + 13)
#define	RK29_PIN1_PB6		(1*NUM_GROUP + PIN_BASE + 14)
#define	RK29_PIN1_PB7		(1*NUM_GROUP + PIN_BASE + 15)
#define	RK29_PIN1_PC0		(1*NUM_GROUP + PIN_BASE + 16)
#define	RK29_PIN1_PC1		(1*NUM_GROUP + PIN_BASE + 17)
#define	RK29_PIN1_PC2		(1*NUM_GROUP + PIN_BASE + 18)
#define	RK29_PIN1_PC3		(1*NUM_GROUP + PIN_BASE + 19)
#define	RK29_PIN1_PC4		(1*NUM_GROUP + PIN_BASE + 20)
#define	RK29_PIN1_PC5		(1*NUM_GROUP + PIN_BASE + 21)
#define	RK29_PIN1_PC6		(1*NUM_GROUP + PIN_BASE + 22)
#define	RK29_PIN1_PC7		(1*NUM_GROUP + PIN_BASE + 23)
#define	RK29_PIN1_PD0		(1*NUM_GROUP + PIN_BASE + 24)
#define	RK29_PIN1_PD1		(1*NUM_GROUP + PIN_BASE + 25)
#define	RK29_PIN1_PD2		(1*NUM_GROUP + PIN_BASE + 26)
#define	RK29_PIN1_PD3		(1*NUM_GROUP + PIN_BASE + 27)
#define	RK29_PIN1_PD4		(1*NUM_GROUP + PIN_BASE + 28)
#define	RK29_PIN1_PD5		(1*NUM_GROUP + PIN_BASE + 29)
#define	RK29_PIN1_PD6		(1*NUM_GROUP + PIN_BASE + 30)
#define	RK29_PIN1_PD7		(1*NUM_GROUP + PIN_BASE + 31)

#define	RK29_PIN2_PA0		(2*NUM_GROUP + PIN_BASE + 0)
#define	RK29_PIN2_PA1		(2*NUM_GROUP + PIN_BASE + 1)
#define	RK29_PIN2_PA2		(2*NUM_GROUP + PIN_BASE + 2)
#define	RK29_PIN2_PA3		(2*NUM_GROUP + PIN_BASE + 3)
#define	RK29_PIN2_PA4		(2*NUM_GROUP + PIN_BASE + 4)
#define	RK29_PIN2_PA5		(2*NUM_GROUP + PIN_BASE + 5)
#define	RK29_PIN2_PA6		(2*NUM_GROUP + PIN_BASE + 6)
#define	RK29_PIN2_PA7		(2*NUM_GROUP + PIN_BASE + 7)
#define	RK29_PIN2_PB0		(2*NUM_GROUP + PIN_BASE + 8)
#define	RK29_PIN2_PB1		(2*NUM_GROUP + PIN_BASE + 9)
#define	RK29_PIN2_PB2		(2*NUM_GROUP + PIN_BASE + 10)
#define	RK29_PIN2_PB3		(2*NUM_GROUP + PIN_BASE + 11)
#define	RK29_PIN2_PB4		(2*NUM_GROUP + PIN_BASE + 12)
#define	RK29_PIN2_PB5		(2*NUM_GROUP + PIN_BASE + 13)
#define	RK29_PIN2_PB6		(2*NUM_GROUP + PIN_BASE + 14)
#define	RK29_PIN2_PB7		(2*NUM_GROUP + PIN_BASE + 15)
#define	RK29_PIN2_PC0		(2*NUM_GROUP + PIN_BASE + 16)
#define	RK29_PIN2_PC1		(2*NUM_GROUP + PIN_BASE + 17)
#define	RK29_PIN2_PC2		(2*NUM_GROUP + PIN_BASE + 18)
#define	RK29_PIN2_PC3		(2*NUM_GROUP + PIN_BASE + 19)
#define	RK29_PIN2_PC4		(2*NUM_GROUP + PIN_BASE + 20)
#define	RK29_PIN2_PC5		(2*NUM_GROUP + PIN_BASE + 21)
#define	RK29_PIN2_PC6		(2*NUM_GROUP + PIN_BASE + 22)
#define	RK29_PIN2_PC7		(2*NUM_GROUP + PIN_BASE + 23)
#define	RK29_PIN2_PD0		(2*NUM_GROUP + PIN_BASE + 24)
#define	RK29_PIN2_PD1		(2*NUM_GROUP + PIN_BASE + 25)
#define	RK29_PIN2_PD2		(2*NUM_GROUP + PIN_BASE + 26)
#define	RK29_PIN2_PD3		(2*NUM_GROUP + PIN_BASE + 27)
#define	RK29_PIN2_PD4		(2*NUM_GROUP + PIN_BASE + 28)
#define	RK29_PIN2_PD5		(2*NUM_GROUP + PIN_BASE + 29)
#define	RK29_PIN2_PD6		(2*NUM_GROUP + PIN_BASE + 30)
#define	RK29_PIN2_PD7		(2*NUM_GROUP + PIN_BASE + 31)

#define	RK29_PIN3_PA0		(3*NUM_GROUP + PIN_BASE + 0) 
#define	RK29_PIN3_PA1		(3*NUM_GROUP + PIN_BASE + 1) 
#define	RK29_PIN3_PA2		(3*NUM_GROUP + PIN_BASE + 2) 
#define	RK29_PIN3_PA3		(3*NUM_GROUP + PIN_BASE + 3) 
#define	RK29_PIN3_PA4		(3*NUM_GROUP + PIN_BASE + 4) 
#define	RK29_PIN3_PA5		(3*NUM_GROUP + PIN_BASE + 5) 
#define	RK29_PIN3_PA6		(3*NUM_GROUP + PIN_BASE + 6) 
#define	RK29_PIN3_PA7		(3*NUM_GROUP + PIN_BASE + 7) 
#define	RK29_PIN3_PB0		(3*NUM_GROUP + PIN_BASE + 8) 
#define	RK29_PIN3_PB1		(3*NUM_GROUP + PIN_BASE + 9) 
#define	RK29_PIN3_PB2		(3*NUM_GROUP + PIN_BASE + 10)
#define	RK29_PIN3_PB3		(3*NUM_GROUP + PIN_BASE + 11)
#define	RK29_PIN3_PB4		(3*NUM_GROUP + PIN_BASE + 12)
#define	RK29_PIN3_PB5		(3*NUM_GROUP + PIN_BASE + 13)
#define	RK29_PIN3_PB6		(3*NUM_GROUP + PIN_BASE + 14)
#define	RK29_PIN3_PB7		(3*NUM_GROUP + PIN_BASE + 15)
#define	RK29_PIN3_PC0		(3*NUM_GROUP + PIN_BASE + 16)
#define	RK29_PIN3_PC1		(3*NUM_GROUP + PIN_BASE + 17)
#define	RK29_PIN3_PC2		(3*NUM_GROUP + PIN_BASE + 18)
#define	RK29_PIN3_PC3		(3*NUM_GROUP + PIN_BASE + 19)
#define	RK29_PIN3_PC4		(3*NUM_GROUP + PIN_BASE + 20)
#define	RK29_PIN3_PC5		(3*NUM_GROUP + PIN_BASE + 21)
#define	RK29_PIN3_PC6		(3*NUM_GROUP + PIN_BASE + 22)
#define	RK29_PIN3_PC7		(3*NUM_GROUP + PIN_BASE + 23)
#define	RK29_PIN3_PD0		(3*NUM_GROUP + PIN_BASE + 24)
#define	RK29_PIN3_PD1		(3*NUM_GROUP + PIN_BASE + 25)
#define	RK29_PIN3_PD2		(3*NUM_GROUP + PIN_BASE + 26)
#define	RK29_PIN3_PD3		(3*NUM_GROUP + PIN_BASE + 27)
#define	RK29_PIN3_PD4		(3*NUM_GROUP + PIN_BASE + 28)
#define	RK29_PIN3_PD5		(3*NUM_GROUP + PIN_BASE + 29)
#define	RK29_PIN3_PD6		(3*NUM_GROUP + PIN_BASE + 30)
#define	RK29_PIN3_PD7		(3*NUM_GROUP + PIN_BASE + 31)

#define	RK29_PIN4_PA0		(4*NUM_GROUP + PIN_BASE + 0) 
#define	RK29_PIN4_PA1		(4*NUM_GROUP + PIN_BASE + 1) 
#define	RK29_PIN4_PA2		(4*NUM_GROUP + PIN_BASE + 2) 
#define	RK29_PIN4_PA3		(4*NUM_GROUP + PIN_BASE + 3) 
#define	RK29_PIN4_PA4		(4*NUM_GROUP + PIN_BASE + 4) 
#define	RK29_PIN4_PA5		(4*NUM_GROUP + PIN_BASE + 5) 
#define	RK29_PIN4_PA6		(4*NUM_GROUP + PIN_BASE + 6) 
#define	RK29_PIN4_PA7		(4*NUM_GROUP + PIN_BASE + 7) 
#define	RK29_PIN4_PB0		(4*NUM_GROUP + PIN_BASE + 8) 
#define	RK29_PIN4_PB1		(4*NUM_GROUP + PIN_BASE + 9) 
#define	RK29_PIN4_PB2		(4*NUM_GROUP + PIN_BASE + 10)
#define	RK29_PIN4_PB3		(4*NUM_GROUP + PIN_BASE + 11)
#define	RK29_PIN4_PB4		(4*NUM_GROUP + PIN_BASE + 12)
#define	RK29_PIN4_PB5		(4*NUM_GROUP + PIN_BASE + 13)
#define	RK29_PIN4_PB6		(4*NUM_GROUP + PIN_BASE + 14)
#define	RK29_PIN4_PB7		(4*NUM_GROUP + PIN_BASE + 15)
#define	RK29_PIN4_PC0		(4*NUM_GROUP + PIN_BASE + 16)
#define	RK29_PIN4_PC1		(4*NUM_GROUP + PIN_BASE + 17)
#define	RK29_PIN4_PC2		(4*NUM_GROUP + PIN_BASE + 18)
#define	RK29_PIN4_PC3		(4*NUM_GROUP + PIN_BASE + 19)
#define	RK29_PIN4_PC4		(4*NUM_GROUP + PIN_BASE + 20)
#define	RK29_PIN4_PC5		(4*NUM_GROUP + PIN_BASE + 21)
#define	RK29_PIN4_PC6		(4*NUM_GROUP + PIN_BASE + 22)
#define	RK29_PIN4_PC7		(4*NUM_GROUP + PIN_BASE + 23)
#define	RK29_PIN4_PD0		(4*NUM_GROUP + PIN_BASE + 24)
#define	RK29_PIN4_PD1		(4*NUM_GROUP + PIN_BASE + 25)
#define	RK29_PIN4_PD2		(4*NUM_GROUP + PIN_BASE + 26)
#define	RK29_PIN4_PD3		(4*NUM_GROUP + PIN_BASE + 27)
#define	RK29_PIN4_PD4		(4*NUM_GROUP + PIN_BASE + 28)
#define	RK29_PIN4_PD5		(4*NUM_GROUP + PIN_BASE + 29)
#define	RK29_PIN4_PD6		(4*NUM_GROUP + PIN_BASE + 30)
#define	RK29_PIN4_PD7		(4*NUM_GROUP + PIN_BASE + 31)

#define	RK29_PIN5_PA0		(5*NUM_GROUP + PIN_BASE + 0)
#define	RK29_PIN5_PA1		(5*NUM_GROUP + PIN_BASE + 1)
#define	RK29_PIN5_PA2		(5*NUM_GROUP + PIN_BASE + 2)
#define	RK29_PIN5_PA3		(5*NUM_GROUP + PIN_BASE + 3)
#define	RK29_PIN5_PA4		(5*NUM_GROUP + PIN_BASE + 4)
#define	RK29_PIN5_PA5		(5*NUM_GROUP + PIN_BASE + 5)
#define	RK29_PIN5_PA6		(5*NUM_GROUP + PIN_BASE + 6)
#define	RK29_PIN5_PA7		(5*NUM_GROUP + PIN_BASE + 7)
#define	RK29_PIN5_PB0		(5*NUM_GROUP + PIN_BASE + 8)
#define	RK29_PIN5_PB1		(5*NUM_GROUP + PIN_BASE + 9)
#define	RK29_PIN5_PB2		(5*NUM_GROUP + PIN_BASE + 10)
#define	RK29_PIN5_PB3		(5*NUM_GROUP + PIN_BASE + 11)
#define	RK29_PIN5_PB4		(5*NUM_GROUP + PIN_BASE + 12)
#define	RK29_PIN5_PB5		(5*NUM_GROUP + PIN_BASE + 13)
#define	RK29_PIN5_PB6		(5*NUM_GROUP + PIN_BASE + 14)
#define	RK29_PIN5_PB7		(5*NUM_GROUP + PIN_BASE + 15)
#define	RK29_PIN5_PC0		(5*NUM_GROUP + PIN_BASE + 16)
#define	RK29_PIN5_PC1		(5*NUM_GROUP + PIN_BASE + 17)
#define	RK29_PIN5_PC2		(5*NUM_GROUP + PIN_BASE + 18)
#define	RK29_PIN5_PC3		(5*NUM_GROUP + PIN_BASE + 19)
#define	RK29_PIN5_PC4		(5*NUM_GROUP + PIN_BASE + 20)
#define	RK29_PIN5_PC5		(5*NUM_GROUP + PIN_BASE + 21)
#define	RK29_PIN5_PC6		(5*NUM_GROUP + PIN_BASE + 22)
#define	RK29_PIN5_PC7		(5*NUM_GROUP + PIN_BASE + 23)
#define	RK29_PIN5_PD0		(5*NUM_GROUP + PIN_BASE + 24)
#define	RK29_PIN5_PD1		(5*NUM_GROUP + PIN_BASE + 25)
#define	RK29_PIN5_PD2		(5*NUM_GROUP + PIN_BASE + 26)
#define	RK29_PIN5_PD3		(5*NUM_GROUP + PIN_BASE + 27)
#define	RK29_PIN5_PD4		(5*NUM_GROUP + PIN_BASE + 28)
#define	RK29_PIN5_PD5		(5*NUM_GROUP + PIN_BASE + 29)
#define	RK29_PIN5_PD6		(5*NUM_GROUP + PIN_BASE + 30)
#define	RK29_PIN5_PD7		(5*NUM_GROUP + PIN_BASE + 31)

#define	RK29_PIN6_PA0		(6*NUM_GROUP + PIN_BASE + 0) 
#define	RK29_PIN6_PA1		(6*NUM_GROUP + PIN_BASE + 1) 
#define	RK29_PIN6_PA2		(6*NUM_GROUP + PIN_BASE + 2) 
#define	RK29_PIN6_PA3		(6*NUM_GROUP + PIN_BASE + 3) 
#define	RK29_PIN6_PA4		(6*NUM_GROUP + PIN_BASE + 4) 
#define	RK29_PIN6_PA5		(6*NUM_GROUP + PIN_BASE + 5) 
#define	RK29_PIN6_PA6		(6*NUM_GROUP + PIN_BASE + 6) 
#define	RK29_PIN6_PA7		(6*NUM_GROUP + PIN_BASE + 7) 
#define	RK29_PIN6_PB0		(6*NUM_GROUP + PIN_BASE + 8) 
#define	RK29_PIN6_PB1		(6*NUM_GROUP + PIN_BASE + 9) 
#define	RK29_PIN6_PB2		(6*NUM_GROUP + PIN_BASE + 10)
#define	RK29_PIN6_PB3		(6*NUM_GROUP + PIN_BASE + 11)
#define	RK29_PIN6_PB4		(6*NUM_GROUP + PIN_BASE + 12)
#define	RK29_PIN6_PB5		(6*NUM_GROUP + PIN_BASE + 13)
#define	RK29_PIN6_PB6		(6*NUM_GROUP + PIN_BASE + 14)
#define	RK29_PIN6_PB7		(6*NUM_GROUP + PIN_BASE + 15)
#define	RK29_PIN6_PC0		(6*NUM_GROUP + PIN_BASE + 16)
#define	RK29_PIN6_PC1		(6*NUM_GROUP + PIN_BASE + 17)
#define	RK29_PIN6_PC2		(6*NUM_GROUP + PIN_BASE + 18)
#define	RK29_PIN6_PC3		(6*NUM_GROUP + PIN_BASE + 19)
#define	RK29_PIN6_PC4		(6*NUM_GROUP + PIN_BASE + 20)
#define	RK29_PIN6_PC5		(6*NUM_GROUP + PIN_BASE + 21)
#define	RK29_PIN6_PC6		(6*NUM_GROUP + PIN_BASE + 22)
#define	RK29_PIN6_PC7		(6*NUM_GROUP + PIN_BASE + 23)
#define	RK29_PIN6_PD0		(6*NUM_GROUP + PIN_BASE + 24)
#define	RK29_PIN6_PD1		(6*NUM_GROUP + PIN_BASE + 25)
#define	RK29_PIN6_PD2		(6*NUM_GROUP + PIN_BASE + 26)
#define	RK29_PIN6_PD3		(6*NUM_GROUP + PIN_BASE + 27)
#define	RK29_PIN6_PD4		(6*NUM_GROUP + PIN_BASE + 28)
#define	RK29_PIN6_PD5		(6*NUM_GROUP + PIN_BASE + 29)
#define	RK29_PIN6_PD6		(6*NUM_GROUP + PIN_BASE + 30)
#define	RK29_PIN6_PD7		(6*NUM_GROUP + PIN_BASE + 31)
                                           
#define ARCH_NR_GPIOS 		(NUM_GROUP*MAX_BANK)
                                           
#ifndef __ASSEMBLY__
extern void __init rk29_gpio_init(void);
/*-------------------------------------------------------------------------*/

/* wrappers for "new style" GPIO calls. the old RK2818-specfic ones should
 * eventually be removed (along with this errno.h inclusion), and the
 * gpio request/free calls should probably be implemented.
 */

#include <asm/errno.h>
#include <asm-generic/gpio.h>		/* cansleep wrappers */
#include <mach/irqs.h>

#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep

static inline int gpio_to_irq(unsigned gpio)
{
	return (gpio + NR_AIC_IRQS);
}

static inline int irq_to_gpio(unsigned irq)
{
	return (irq - NR_AIC_IRQS);       
}

#endif	/* __ASSEMBLY__ */     
                                                                                                                                                             
#endif                                                                                              
                                                                                 
