/*
 *  arch/arm/plat-meson/gic-irq.c
 *
 *  Copyright (C) 2013 AMLOGIC, INC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach/irq.h>
#include <asm/hardware/gic.h>
#include <plat/io.h>
#include <mach/io.h>
#ifdef CONFIG_OF
#include <linux/mod_devicetable.h>
#include <linux/of_irq.h>
#endif

#define MESON_GIC_IRQ_LEVEL 0x3;
#define MESON_GIC_FIQ_LEVEL 0x0;
static unsigned irq_level=MESON_GIC_IRQ_LEVEL;

static void meson_gic_unmask(struct irq_data *data)
{
    /**
     * Set irq to edge rising and proi to low
     */
    uint32_t dist_base=(uint32_t)(IO_PERIPH_BASE+0x1000);
    int edge = 0x3;//edge

    int irq=data->irq;
    if(irq<32)
        return;

    /**
      * Deal with IRQ type: IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH
      */
    if(data->state_use_accessors & IRQ_TYPE_LEVEL_MASK)
	edge = 0x1;//level

     if((irq == 62)||(irq == 63))    
	 edge = 0x1;//level

    /**
     * set irq to edge rising .
     */
    aml_set_reg32_bits(dist_base+GIC_DIST_CONFIG + (irq/16)*4,edge,(irq%16)*2,2);
    /**
     * Set prority
     */

    aml_set_reg32_bits(dist_base+GIC_DIST_PRI + (irq  / 4)* 4,0xff,(irq%4)*8,irq_level);

}
#ifdef CONFIG_OF
static const struct of_device_id mesnon_dt_irq_match[] __initconst = {
	{ .compatible = "arm,cortex-a9-gic", .data = gic_of_init },
	{ }
};
#endif
/* ARM Interrupt Controller Initialization */
void __init meson_init_gic_irq(void)
{
    gic_arch_extn.irq_unmask=meson_gic_unmask;
#ifdef CONFIG_OF
    of_irq_init(mesnon_dt_irq_match);
#else
    gic_init(0,29,(void __iomem *)(IO_PERIPH_BASE+0x1000),(void __iomem *)(IO_PERIPH_BASE+0x100));
#endif

    aml_write_reg32(IO_PERIPH_BASE+0x100 +GIC_CPU_PRIMASK,0xff);

#ifdef CONFIG_MESON_ARM_GIC_FIQ
extern void init_fiq(void)	;
	init_fiq();
#endif
}

#if 1 //ndef CONFIG_MESON_ARM_GIC_FIQ
static void (*fiq_isr[NR_IRQS])(void);

static irqreturn_t
fake_fiq_handler(int irq, void *dev_id)
{
    if(fiq_isr[irq])
        fiq_isr[irq]();
    return IRQ_HANDLED;
}

static struct irqaction fake_fiq = {
        .name           = "amlogic-fake-fiq",
        .flags          = IRQF_VALID,
        .handler        = fake_fiq_handler
};


static DEFINE_SPINLOCK(lock);
void request_fiq(unsigned int fiq, void (*isr)(void))
{
    ulong flags;

    BUG_ON(fiq >= NR_IRQS) ;
    BUG_ON(fiq_isr[fiq]!=NULL);
    spin_lock_irqsave(&lock, flags);
    irq_level=MESON_GIC_FIQ_LEVEL;
    fiq_isr[fiq]=isr;
    setup_irq(fiq,&fake_fiq);
    irq_level=MESON_GIC_IRQ_LEVEL;
#ifdef CONFIG_ARM_GIC
    irq_set_affinity(fiq,cpumask_of(1));
#else
#endif
    spin_unlock_irqrestore(&lock, flags);
}
EXPORT_SYMBOL(request_fiq);
void free_fiq(unsigned int fiq, void (*isr)(void))
{
    ulong flags;

    spin_lock_irqsave(&lock, flags);
    fiq_isr[fiq]=NULL;
    remove_irq(fiq,&fake_fiq);
    spin_unlock_irqrestore(&lock, flags);
}
EXPORT_SYMBOL(free_fiq);
#endif  // end of CONFIG_MESON_ARM_GIC_FIQ
