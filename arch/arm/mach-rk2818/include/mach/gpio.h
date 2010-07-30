/* arch/arm/mach-rk2818/GPIO.h
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
#ifndef __ARCH_ARM_MACH_RK2818_GPIO_H
#define __ARCH_ARM_MACH_RK2818_GPIO_H


#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/irq.h>

#define PIN_BASE        		0//定义RK2818内部GPIO的第一个PIN口(即GPIO0_A0)在gpio_desc数组的地址
#define NUM_GROUP			8// 定义RK2818内部GPIO每一组最大的PIN数目，现在定为8个，即GPIOX_Y0~ GPIOX_Y7(其中X=0/1;Y=A/B/C/D)
#define MAX_GPIO_BANKS		8//定义RK2818内部GPIO总共有几组，现在定为8组，即GPIO0_A~ GPIO0_D，GPIO1_A~ GPIO1_D。
#define GPIOS_EXPANDER_BASE	(PIN_BASE+NUM_GROUP*MAX_GPIO_BANKS)
//定义GPIO的PIN口最大数目。(NUM_GROUP*MAX_GPIO_BANKS)表示RK2818的内部GPIO的PIN口最大数目；CONFIG_ARCH_EXTEND_GPIOS表示扩展IO的最大数目。
#define ARCH_NR_GPIOS  (NUM_GROUP*MAX_GPIO_BANKS) + CONFIG_EXPANDED_GPIO_NUM
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
	GPIONormal,
	GPIOPullUp,
	GPIOPullDown,
	GPIONOInit
}eGPIOPullType_t;

typedef enum GPIOIntType {
	GPIOLevelLow=0,
	GPIOLevelHigh,	 
	GPIOEdgelFalling,
	GPIOEdgelRising
}eGPIOIntType_t;
//GPIO Registers

struct rk2818_gpio_bank {
	unsigned short id;			//GPIO寄存器组的ID识别号
	unsigned long offset;		//GPIO0或GPIO1的基地址
	struct clk *clock;		/* associated clock */
};
//定义GPIO相关寄存器偏移地址
#define 	GPIO_SWPORTA_DR		0x00
#define 	GPIO_SWPORTA_DDR	0x04

#define 	GPIO_SWPORTB_DR		0x0c
#define 	GPIO_SWPORTB_DDR	0x10

#define 	GPIO_SWPORTC_DR		0x18
#define 	GPIO_SWPORTC_DDR	0x1c

#define 	GPIO_SWPORTD_DR		0x24
#define 	GPIO_SWPORTD_DDR  	0x28

#define 	GPIO_INTEN 			0x30
#define 	GPIO_INTMASK 			0x34
#define 	GPIO_INTTYPE_LEVEL 	0x38
#define 	GPIO_INT_POLARITY 	0x3c
#define 	GPIO_INT_STATUS 		0x40
#define 	GPIO_INT_RAWSTATUS  	0x44
#define 	GPIO_DEBOUNCE 		0x48
#define 	GPIO_PORTS_EOI 		0x4c
#define 	GPIO_EXT_PORTA  		0x50
#define 	GPIO_EXT_PORTB 		0x54
#define 	GPIO_EXT_PORTC  		0x58
#define 	GPIO_EXT_PORTD 		0x5c
#define 	GPIO_LS_SYNC 			0x60

#define RK2818_ID_PIOA	0
#define RK2818_ID_PIOB	1
#define RK2818_ID_PIOC	2
#define RK2818_ID_PIOD	3
#define RK2818_ID_PIOE	4
#define RK2818_ID_PIOF	5
#define RK2818_ID_PIOG	6
#define RK2818_ID_PIOH	7
/* these pin numbers double as IRQ numbers, like RK2818xxx_ID_* values */

#define	RK2818_PIN_PA0		(PIN_BASE + 0*NUM_GROUP + 0)
#define	RK2818_PIN_PA1		(PIN_BASE + 0*NUM_GROUP + 1)
#define	RK2818_PIN_PA2		(PIN_BASE + 0*NUM_GROUP + 2)
#define	RK2818_PIN_PA3		(PIN_BASE + 0*NUM_GROUP + 3)
#define	RK2818_PIN_PA4		(PIN_BASE + 0*NUM_GROUP + 4)
#define	RK2818_PIN_PA5		(PIN_BASE + 0*NUM_GROUP + 5)
#define	RK2818_PIN_PA6		(PIN_BASE + 0*NUM_GROUP + 6)
#define	RK2818_PIN_PA7		(PIN_BASE + 0*NUM_GROUP + 7)


#define	RK2818_PIN_PB0		(PIN_BASE + 1*NUM_GROUP + 0)
#define	RK2818_PIN_PB1		(PIN_BASE + 1*NUM_GROUP + 1)
#define	RK2818_PIN_PB2		(PIN_BASE + 1*NUM_GROUP + 2)
#define	RK2818_PIN_PB3		(PIN_BASE + 1*NUM_GROUP + 3)
#define	RK2818_PIN_PB4		(PIN_BASE + 1*NUM_GROUP + 4)
#define	RK2818_PIN_PB5		(PIN_BASE + 1*NUM_GROUP + 5)
#define	RK2818_PIN_PB6		(PIN_BASE + 1*NUM_GROUP + 6)
#define	RK2818_PIN_PB7		(PIN_BASE + 1*NUM_GROUP + 7)

#define	RK2818_PIN_PC0		(PIN_BASE + 2*NUM_GROUP + 0)
#define	RK2818_PIN_PC1		(PIN_BASE + 2*NUM_GROUP + 1)
#define	RK2818_PIN_PC2		(PIN_BASE + 2*NUM_GROUP + 2)
#define	RK2818_PIN_PC3		(PIN_BASE + 2*NUM_GROUP + 3)
#define	RK2818_PIN_PC4		(PIN_BASE + 2*NUM_GROUP + 4)
#define	RK2818_PIN_PC5		(PIN_BASE + 2*NUM_GROUP + 5)
#define	RK2818_PIN_PC6		(PIN_BASE + 2*NUM_GROUP + 6)
#define	RK2818_PIN_PC7		(PIN_BASE + 2*NUM_GROUP + 7)

#define	RK2818_PIN_PD0	(PIN_BASE + 3*NUM_GROUP + 0)
#define	RK2818_PIN_PD1	(PIN_BASE + 3*NUM_GROUP + 1)
#define	RK2818_PIN_PD2	(PIN_BASE + 3*NUM_GROUP + 2)
#define	RK2818_PIN_PD3	(PIN_BASE + 3*NUM_GROUP + 3)
#define	RK2818_PIN_PD4	(PIN_BASE + 3*NUM_GROUP + 4)
#define	RK2818_PIN_PD5	(PIN_BASE + 3*NUM_GROUP + 5)
#define	RK2818_PIN_PD6	(PIN_BASE + 3*NUM_GROUP + 6)
#define	RK2818_PIN_PD7	(PIN_BASE + 3*NUM_GROUP + 7)

#define	RK2818_PIN_PE0		(PIN_BASE + 4*NUM_GROUP + 0)
#define	RK2818_PIN_PE1		(PIN_BASE + 4*NUM_GROUP + 1)
#define	RK2818_PIN_PE2		(PIN_BASE + 4*NUM_GROUP + 2)
#define	RK2818_PIN_PE3		(PIN_BASE + 4*NUM_GROUP + 3)
#define	RK2818_PIN_PE4		(PIN_BASE + 4*NUM_GROUP + 4)
#define	RK2818_PIN_PE5		(PIN_BASE + 4*NUM_GROUP + 5)
#define	RK2818_PIN_PE6		(PIN_BASE + 4*NUM_GROUP + 6)
#define	RK2818_PIN_PE7		(PIN_BASE + 4*NUM_GROUP + 7)

#define	RK2818_PIN_PF0		(PIN_BASE + 5*NUM_GROUP + 0)
#define	RK2818_PIN_PF1		(PIN_BASE + 5*NUM_GROUP + 1)
#define	RK2818_PIN_PF2		(PIN_BASE + 5*NUM_GROUP + 2)
#define	RK2818_PIN_PF3		(PIN_BASE + 5*NUM_GROUP + 3)
#define	RK2818_PIN_PF4		(PIN_BASE + 5*NUM_GROUP + 4)
#define	RK2818_PIN_PF5		(PIN_BASE + 5*NUM_GROUP + 5)
#define	RK2818_PIN_PF6		(PIN_BASE + 5*NUM_GROUP + 6)
#define	RK2818_PIN_PF7		(PIN_BASE + 5*NUM_GROUP + 7)


#define	RK2818_PIN_PG0	(PIN_BASE + 6*NUM_GROUP + 0)
#define	RK2818_PIN_PG1	(PIN_BASE + 6*NUM_GROUP + 1)
#define	RK2818_PIN_PG2	(PIN_BASE + 6*NUM_GROUP + 2)
#define	RK2818_PIN_PG3	(PIN_BASE + 6*NUM_GROUP + 3)
#define	RK2818_PIN_PG4	(PIN_BASE + 6*NUM_GROUP + 4)
#define	RK2818_PIN_PG5	(PIN_BASE + 6*NUM_GROUP + 5)
#define	RK2818_PIN_PG6	(PIN_BASE + 6*NUM_GROUP + 6)
#define	RK2818_PIN_PG7	(PIN_BASE + 6*NUM_GROUP + 7)

#define	RK2818_PIN_PH0	(PIN_BASE + 7*NUM_GROUP + 0)
#define	RK2818_PIN_PH1	(PIN_BASE + 7*NUM_GROUP + 1)
#define	RK2818_PIN_PH2	(PIN_BASE + 7*NUM_GROUP + 2)
#define	RK2818_PIN_PH3	(PIN_BASE + 7*NUM_GROUP + 3)
#define	RK2818_PIN_PH4	(PIN_BASE + 7*NUM_GROUP + 4)
#define	RK2818_PIN_PH5	(PIN_BASE + 7*NUM_GROUP + 5)
#define	RK2818_PIN_PH6	(PIN_BASE + 7*NUM_GROUP + 6)
#define	RK2818_PIN_PH7	(PIN_BASE + 7*NUM_GROUP + 7)
/***********************define extern gpio pin num******************************/
#if defined(CONFIG_SPI_GPIO)
#define	FPGA_PIN_PA0 (GPIOS_EXPANDER_BASE + 0*NUM_GROUP + 0)
#define	FPGA_PIN_PA1 (GPIOS_EXPANDER_BASE + 0*NUM_GROUP + 1)
#define	FPGA_PIN_PA2 (GPIOS_EXPANDER_BASE + 0*NUM_GROUP + 2)
#define	FPGA_PIN_PA3 (GPIOS_EXPANDER_BASE + 0*NUM_GROUP + 3)
#define	FPGA_PIN_PA4 (GPIOS_EXPANDER_BASE + 0*NUM_GROUP + 4)
#define	FPGA_PIN_PA5 (GPIOS_EXPANDER_BASE + 0*NUM_GROUP + 5)
#define	FPGA_PIN_PA6 (GPIOS_EXPANDER_BASE + 0*NUM_GROUP + 6)
#define	FPGA_PIN_PA7 (GPIOS_EXPANDER_BASE + 0*NUM_GROUP + 7)

#define	FPGA_PIN_PB0 (GPIOS_EXPANDER_BASE + 1*NUM_GROUP + 0)
#define	FPGA_PIN_PB1 (GPIOS_EXPANDER_BASE + 1*NUM_GROUP + 1)
#define	FPGA_PIN_PB2 (GPIOS_EXPANDER_BASE + 1*NUM_GROUP + 2)
#define	FPGA_PIN_PB3 (GPIOS_EXPANDER_BASE + 1*NUM_GROUP + 3)
#define	FPGA_PIN_PB4 (GPIOS_EXPANDER_BASE + 1*NUM_GROUP + 4)
#define	FPGA_PIN_PB5 (GPIOS_EXPANDER_BASE + 1*NUM_GROUP + 5)
#define	FPGA_PIN_PB6 (GPIOS_EXPANDER_BASE + 1*NUM_GROUP + 6)
#define	FPGA_PIN_PB7 (GPIOS_EXPANDER_BASE + 1*NUM_GROUP + 7)

#define	FPGA_PIN_PC0 (GPIOS_EXPANDER_BASE + 2*NUM_GROUP + 0)
#define	FPGA_PIN_PC1 (GPIOS_EXPANDER_BASE + 2*NUM_GROUP + 1)
#define	FPGA_PIN_PC2 (GPIOS_EXPANDER_BASE + 2*NUM_GROUP + 2)
#define	FPGA_PIN_PC3 (GPIOS_EXPANDER_BASE + 2*NUM_GROUP + 3)
#define	FPGA_PIN_PC4 (GPIOS_EXPANDER_BASE + 2*NUM_GROUP + 4)
#define	FPGA_PIN_PC5 (GPIOS_EXPANDER_BASE + 2*NUM_GROUP + 5)
#define	FPGA_PIN_PC6 (GPIOS_EXPANDER_BASE + 2*NUM_GROUP + 6)
#define	FPGA_PIN_PC7 (GPIOS_EXPANDER_BASE + 2*NUM_GROUP + 7)

#define	FPGA_PIN_PD0 (GPIOS_EXPANDER_BASE + 3*NUM_GROUP + 0)
#define	FPGA_PIN_PD1 (GPIOS_EXPANDER_BASE + 3*NUM_GROUP + 1)
#define	FPGA_PIN_PD2 (GPIOS_EXPANDER_BASE + 3*NUM_GROUP + 2)
#define	FPGA_PIN_PD3 (GPIOS_EXPANDER_BASE + 3*NUM_GROUP + 3)
#define	FPGA_PIN_PD4 (GPIOS_EXPANDER_BASE + 3*NUM_GROUP + 4)
#define	FPGA_PIN_PD5 (GPIOS_EXPANDER_BASE + 3*NUM_GROUP + 5)
#define	FPGA_PIN_PD6 (GPIOS_EXPANDER_BASE + 3*NUM_GROUP + 6)
#define	FPGA_PIN_PD7 (GPIOS_EXPANDER_BASE + 3*NUM_GROUP + 7)

#define	FPGA_PIN_PE0 (GPIOS_EXPANDER_BASE + 4*NUM_GROUP + 0)
#define	FPGA_PIN_PE1 (GPIOS_EXPANDER_BASE + 4*NUM_GROUP + 1)
#define	FPGA_PIN_PE2 (GPIOS_EXPANDER_BASE + 4*NUM_GROUP + 2)
#define	FPGA_PIN_PE3 (GPIOS_EXPANDER_BASE + 4*NUM_GROUP + 3)
#define	FPGA_PIN_PE4 (GPIOS_EXPANDER_BASE + 4*NUM_GROUP + 4)
#define	FPGA_PIN_PE5 (GPIOS_EXPANDER_BASE + 4*NUM_GROUP + 5)
#define	FPGA_PIN_PE6 (GPIOS_EXPANDER_BASE + 4*NUM_GROUP + 6)
#define	FPGA_PIN_PE7 (GPIOS_EXPANDER_BASE + 4*NUM_GROUP + 7)

#define	FPGA_PIN_PF0 (GPIOS_EXPANDER_BASE + 5*NUM_GROUP + 0)
#define	FPGA_PIN_PF1 (GPIOS_EXPANDER_BASE + 5*NUM_GROUP + 1)
#define	FPGA_PIN_PF2 (GPIOS_EXPANDER_BASE + 5*NUM_GROUP + 2)
#define	FPGA_PIN_PF3 (GPIOS_EXPANDER_BASE + 5*NUM_GROUP + 3)
#define	FPGA_PIN_PF4 (GPIOS_EXPANDER_BASE + 5*NUM_GROUP + 4)
#define	FPGA_PIN_PF5 (GPIOS_EXPANDER_BASE + 5*NUM_GROUP + 5)
#define	FPGA_PIN_PF6 (GPIOS_EXPANDER_BASE + 5*NUM_GROUP + 6)
#define	FPGA_PIN_PF7 (GPIOS_EXPANDER_BASE + 5*NUM_GROUP + 7)

#define	FPGA_PIN_PG0 (GPIOS_EXPANDER_BASE + 6*NUM_GROUP + 0)
#define	FPGA_PIN_PG1 (GPIOS_EXPANDER_BASE + 6*NUM_GROUP + 1)
#define	FPGA_PIN_PG2 (GPIOS_EXPANDER_BASE + 6*NUM_GROUP + 2)
#define	FPGA_PIN_PG3 (GPIOS_EXPANDER_BASE + 6*NUM_GROUP + 3)
#define	FPGA_PIN_PG4 (GPIOS_EXPANDER_BASE + 6*NUM_GROUP + 4)
#define	FPGA_PIN_PG5 (GPIOS_EXPANDER_BASE + 6*NUM_GROUP + 5)
#define	FPGA_PIN_PG6 (GPIOS_EXPANDER_BASE + 6*NUM_GROUP + 6)
#define	FPGA_PIN_PG7 (GPIOS_EXPANDER_BASE + 6*NUM_GROUP + 7)

#define	FPGA_PIN_PH0 (GPIOS_EXPANDER_BASE + 7*NUM_GROUP + 0)
#define	FPGA_PIN_PH1 (GPIOS_EXPANDER_BASE + 7*NUM_GROUP + 1)
#define	FPGA_PIN_PH2 (GPIOS_EXPANDER_BASE + 7*NUM_GROUP + 2)
#define	FPGA_PIN_PH3 (GPIOS_EXPANDER_BASE + 7*NUM_GROUP + 3)
#define	FPGA_PIN_PH4 (GPIOS_EXPANDER_BASE + 7*NUM_GROUP + 4)
#define	FPGA_PIN_PH5 (GPIOS_EXPANDER_BASE + 7*NUM_GROUP + 5)
#define	FPGA_PIN_PH6 (GPIOS_EXPANDER_BASE + 7*NUM_GROUP + 6)
#define	FPGA_PIN_PH7 (GPIOS_EXPANDER_BASE + 7*NUM_GROUP + 7)

#define	FPGA_PIN_PI0 (GPIOS_EXPANDER_BASE + 8*NUM_GROUP + 0)
#define	FPGA_PIN_PI1 (GPIOS_EXPANDER_BASE + 8*NUM_GROUP + 1)
#define	FPGA_PIN_PI2 (GPIOS_EXPANDER_BASE + 8*NUM_GROUP + 2)
#define	FPGA_PIN_PI3 (GPIOS_EXPANDER_BASE + 8*NUM_GROUP + 3)
#define	FPGA_PIN_PI4 (GPIOS_EXPANDER_BASE + 8*NUM_GROUP + 4)
#define	FPGA_PIN_PI5 (GPIOS_EXPANDER_BASE + 8*NUM_GROUP + 5)
#define	FPGA_PIN_PI6 (GPIOS_EXPANDER_BASE + 8*NUM_GROUP + 6)
#define	FPGA_PIN_PI7 (GPIOS_EXPANDER_BASE + 8*NUM_GROUP + 7)

#define	FPGA_PIN_PJ0 (GPIOS_EXPANDER_BASE + 9*NUM_GROUP + 0)
#define	FPGA_PIN_PJ1 (GPIOS_EXPANDER_BASE + 9*NUM_GROUP + 1)
#define	FPGA_PIN_PJ2 (GPIOS_EXPANDER_BASE + 9*NUM_GROUP + 2)
#define	FPGA_PIN_PJ3 (GPIOS_EXPANDER_BASE + 9*NUM_GROUP + 3)
#define	FPGA_PIN_PJ4 (GPIOS_EXPANDER_BASE + 9*NUM_GROUP + 4)
#define	FPGA_PIN_PJ5 (GPIOS_EXPANDER_BASE + 9*NUM_GROUP + 5)
#define	FPGA_PIN_PJ6 (GPIOS_EXPANDER_BASE + 9*NUM_GROUP + 6)
#define	FPGA_PIN_PJ7 (GPIOS_EXPANDER_BASE + 9*NUM_GROUP + 7)

#define	FPGA_PIN_PK0 (GPIOS_EXPANDER_BASE + 10*NUM_GROUP + 0)
#define	FPGA_PIN_PK1 (GPIOS_EXPANDER_BASE + 10*NUM_GROUP + 1)
#define	FPGA_PIN_PK2 (GPIOS_EXPANDER_BASE + 10*NUM_GROUP + 2)
#define	FPGA_PIN_PK3 (GPIOS_EXPANDER_BASE + 10*NUM_GROUP + 3)
#define	FPGA_PIN_PK4 (GPIOS_EXPANDER_BASE + 10*NUM_GROUP + 4)
#define	FPGA_PIN_PK5 (GPIOS_EXPANDER_BASE + 10*NUM_GROUP + 5)
#define	FPGA_PIN_PK6 (GPIOS_EXPANDER_BASE + 10*NUM_GROUP + 6)
#define	FPGA_PIN_PK7 (GPIOS_EXPANDER_BASE + 10*NUM_GROUP + 7)

#define	FPGA_PIN_PL0 (GPIOS_EXPANDER_BASE + 11*NUM_GROUP + 0)
#define	FPGA_PIN_PL1 (GPIOS_EXPANDER_BASE + 11*NUM_GROUP + 1)
#define	FPGA_PIN_PL2 (GPIOS_EXPANDER_BASE + 11*NUM_GROUP + 2)
#define	FPGA_PIN_PL3 (GPIOS_EXPANDER_BASE + 11*NUM_GROUP + 3)
#define	FPGA_PIN_PL4 (GPIOS_EXPANDER_BASE + 11*NUM_GROUP + 4)
#define	FPGA_PIN_PL5 (GPIOS_EXPANDER_BASE + 11*NUM_GROUP + 5)
#define	FPGA_PIN_PL6 (GPIOS_EXPANDER_BASE + 11*NUM_GROUP + 6)
#define	FPGA_PIN_PL7 (GPIOS_EXPANDER_BASE + 11*NUM_GROUP + 7)

#endif
#ifndef __ASSEMBLY__
extern void __init rk2818_gpio_init(struct rk2818_gpio_bank *data, int nr_banks);
extern void __init rk2818_gpio_irq_setup(void);
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
	return __gpio_to_irq(gpio);
}

static inline int irq_to_gpio(unsigned irq)
{
	if(irq<NR_AIC_IRQS)
	return -ENXIO;

    if((irq - __gpio_to_irq(RK2818_PIN_PA0)) < NUM_GROUP)
    {
        return (RK2818_PIN_PA0 + (irq - __gpio_to_irq(RK2818_PIN_PA0)));
    } 
    else if((irq - __gpio_to_irq(RK2818_PIN_PA0)) < 2*NUM_GROUP)
    {
        return (RK2818_PIN_PE0 + (irq - __gpio_to_irq(RK2818_PIN_PE0)));
    }
#if defined(CONFIG_SPI_GPIO)
   else if((irq - __gpio_to_irq(FPGA_PIN_PA0)) <2*NUM_GROUP)
   {
	return (FPGA_PIN_PA0 + (irq - __gpio_to_irq(FPGA_PIN_PA0)));
    }
#endif
    else
    {
        return -ENXIO;
    }        
}

#endif	/* __ASSEMBLY__ */

#endif

