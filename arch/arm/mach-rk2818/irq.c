/* linux/arch/arm/mach-rk2818/irq.c
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


#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>

//#include <asm/hardware.h>
//#include <asm/irq.h>
#include <asm/mach-types.h>
//#include <asm/setup.h>

//#include <asm/arch/typedef.h>
//#include <asm/arch/hardware.h>
#include <mach/irqs.h>
#include <mach/rk2818_iomap.h>

//#include "include/mach/irqs.h"
//#include "include/mach/rk2818_iomap.h"

//#include <asm/mach/arch.h>
#include <linux/irq.h>
//#include <asm/mach/map.h>
//#include <asm/hw_irq.h>

#include <asm/io.h>


#define which_irq_l(irq)         (0x01u << (irq)) 
#define which_irq_h(irq)         (0x01u << ((irq) & 0x1f)) 

#define write_irq_reg(addr, val)        __raw_writel(val, addr+(RK2818_INTC_BASE)) 
#define read_irq_reg(addr)              __raw_readl(addr+(RK2818_INTC_BASE)) 
#define set_irq_reg(addr, val)          write_irq_reg(addr, ((val) | read_irq_reg(addr)))
#define clear_irq_reg(addr, val)        write_irq_reg(addr, (~(val) & read_irq_reg(addr)))



u32 int_priority[NR_AIC_IRQS]={
    /*   priority      name     number    */
            0,     //IRQ_DWDMA,   0  -- low
            0,     //IRQ_UHI,     1  -- USB Host Interface                     
            0,     //IRQ_NANDC,   2
            0,     //IRQ_LCDC,    3
            0,     //IRQ_SDMMC0,  4
            0,     //IRQ_VIP,     5
            0,     //IRQ_GPIO0,   6
            0,     //IRQ_GPIO1,   7
            0,     //IRQ_OTG,     8  -- USB OTG
            0,     //IRQ_ABTARMD, 9  -- Arbiter in ARMD BUS
            0,     //IRQ_ABTEXP,  10 -- Arbiter in EXP BUS
            0,     //IRQ_I2C0,    11
            0,     //IRQ_I2C1,    12
            0,     //IRQ_I2S,     13
            0,     //IRQ_SPIM,    14 -- SPI Master
            0,     //IRQ_SPIS,    15 -- SPI Slave
            0,     //IRQ_TIMER1,  16
            0,     //IRQ_TIMER2,  17
            0,     //IRQ_TIMER3,  18
            0,     //IRQ_UART0,   19
            0,     //IRQ_UART1,   20
            0,     //IRQ_WDT,     21
            0,     //IRQ_PWM0,    22
            0,     //IRQ_PWM1,    23
            0,     //IRQ_PWM2,    24
            0,     //IRQ_PWM3,    25
            0,     //IRQ_ADC,     26
            0,     //IRQ_RTC,     27
            0,     //IRQ_PIUSEM0, 28 -- PIU Semphore 0
            0,     //IRQ_PIUSEM1, 29
            0,     //IRQ_PIUSEM3, 30
            0,     //IRQ_PIUCMD,  31 -- PIU command/reply
            0,     //IRQ_XDMA,    32
            0,     //IRQ_SDMMC1,  33
            0,     //IRQ_DSPSEI,  34 -- DSP slave interface error interrupt
            0,     //IRQ_DSPSWI,  35 -- DSP interrupt by software set
            0,     //IRQ_SCU,     36
            0,     //IRQ_SWI,     37 -- Software Interrupt
            0,     //IRQ_DSPMEI,  38 -- DSP master interface error interrupt
            0,     //IRQ_DSPSAEI, 39 -- DSP system access error interrupt
            0      //IRQ_MAXNUM   40  -- interrupt 
};

static void rk2818_irq_ack(u32 irq)
{
//rk28 no irq ack
}

static void rk2818_irq_mask(u32 irq)
{
    if (irq >= 32)
    {
        set_irq_reg(IRQ_REG_INTMASK_H, which_irq_h(irq));
    }
    else
    {
        set_irq_reg(IRQ_REG_INTMASK_L, which_irq_l(irq));
    }

}

static void rk2818_irq_unmask(u32 irq)
{
    if (irq >= 32)
    {
       clear_irq_reg(IRQ_REG_INTMASK_H, which_irq_h(irq));
    }
    else
    {
       clear_irq_reg(IRQ_REG_INTMASK_L, which_irq_l(irq));
    }
}

static s32 rk2818_irq_wake(u32 irq, u32 value)
{
//rk28 no irq wake
	return 0;
}


static struct irq_chip rk2818_irq_chip = {
	.name		= "rk2818_irq",
	.ack		= rk2818_irq_ack,
	.mask		= rk2818_irq_mask,
	.unmask		= rk2818_irq_unmask,
  .set_wake   = rk2818_irq_wake,
};


/*
 * Initialize the AIC interrupt controller.
 */
void __init rk2818_init_irq(u32 priority[NR_AIC_IRQS])
{
	u32 i;
	
	write_irq_reg(IRQ_REG_INTEN_L, 0xffffffff);//enable irq interrupt
	write_irq_reg(IRQ_REG_INTEN_H, 0xffffffff);
	write_irq_reg(IRQ_REG_INTMASK_L, 0xffffffff); //mask all irq interrupt
	write_irq_reg(IRQ_REG_INTMASK_H, 0xffffffff);
	write_irq_reg(IRQ_REG_INTFORCE_L, 0);
	write_irq_reg(IRQ_REG_INTFORCE_H, 0);
	write_irq_reg(FIQ_REG_INTEN, 0x03); //enable fiq interrupt
	write_irq_reg(FIQ_REG_INTMASK, 0x03); //mask fiq interrupt
	write_irq_reg(IRQ_REG_PLEVEL, 0);

	for (i = 0; i < NR_AIC_IRQS; i++) {
		set_irq_chip(i, &rk2818_irq_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);//no probe and auto enable		
	}
}
