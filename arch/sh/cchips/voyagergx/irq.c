/* -------------------------------------------------------------------- */
/* setup_voyagergx.c:                                                     */
/* -------------------------------------------------------------------- */
/*  This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Copyright 2003 (c) Lineo uSolutions,Inc.
*/
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/voyagergx.h>
#include <asm/rts7751r2d.h>

enum {
	UNUSED = 0,

	/* voyager specific interrupt sources */
	UP, G54, G53, G52, G51, G50, G49, G48,
	I2C, PW, DMA, PCI, I2S, AC, US,
	U1, U0, CV, MC, S1, S0,
	UH, TWOD, ZD, PV, CI,
};

static struct intc_vect vectors[] __initdata = {
	INTC_IRQ(UP, IRQ_SM501_UP), INTC_IRQ(G54, IRQ_SM501_G54),
	INTC_IRQ(G53, IRQ_SM501_G53), INTC_IRQ(G52, IRQ_SM501_G52),
	INTC_IRQ(G51, IRQ_SM501_G51), INTC_IRQ(G50, IRQ_SM501_G50),
	INTC_IRQ(G49, IRQ_SM501_G49), INTC_IRQ(G48, IRQ_SM501_G48),
	INTC_IRQ(I2C, IRQ_SM501_I2C), INTC_IRQ(PW, IRQ_SM501_PW),
	INTC_IRQ(DMA, IRQ_SM501_DMA), INTC_IRQ(PCI, IRQ_SM501_PCI),
	INTC_IRQ(I2S, IRQ_SM501_I2S), INTC_IRQ(AC, IRQ_SM501_AC),
	INTC_IRQ(US, IRQ_SM501_US), INTC_IRQ(U1, IRQ_SM501_U1),
	INTC_IRQ(U0, IRQ_SM501_U0), INTC_IRQ(CV, IRQ_SM501_CV),
	INTC_IRQ(MC, IRQ_SM501_MC), INTC_IRQ(S1, IRQ_SM501_S1),
	INTC_IRQ(S0, IRQ_SM501_S0), INTC_IRQ(UH, IRQ_SM501_UH),
	INTC_IRQ(TWOD, IRQ_SM501_2D), INTC_IRQ(ZD, IRQ_SM501_ZD),
	INTC_IRQ(PV, IRQ_SM501_PV), INTC_IRQ(CI, IRQ_SM501_CI),
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ VOYAGER_INT_MASK, 0, 32, /* "Interrupt Mask", MMIO_base + 0x30 */
	  { UP, G54, G53, G52, G51, G50, G49, G48,
	    I2C, PW, 0, DMA, PCI, I2S, AC, US,
	    0, 0, U1, U0, CV, MC, S1, S0,
	    0, UH, 0, 0, TWOD, ZD, PV, CI } },
};

static DECLARE_INTC_DESC(intc_desc, "voyagergx", vectors,
			 NULL, NULL, mask_registers, NULL, NULL);

static unsigned int voyagergx_stat2irq[32] = {
	IRQ_SM501_CI, IRQ_SM501_PV, IRQ_SM501_ZD, IRQ_SM501_2D,
	0, 0, IRQ_SM501_UH, 0,
	IRQ_SM501_S0, IRQ_SM501_S1, IRQ_SM501_MC, IRQ_SM501_CV,
	IRQ_SM501_U0, IRQ_SM501_U1, 0, 0,
	IRQ_SM501_US, IRQ_SM501_AC, IRQ_SM501_I2S, IRQ_SM501_PCI,
	IRQ_SM501_DMA, 0, IRQ_SM501_PW, IRQ_SM501_I2C,
	IRQ_SM501_G48, IRQ_SM501_G49, IRQ_SM501_G50, IRQ_SM501_G51,
	IRQ_SM501_G52, IRQ_SM501_G53, IRQ_SM501_G54, IRQ_SM501_UP
};

static void voyagergx_irq_demux(unsigned int irq, struct irq_desc *desc)
{
	unsigned long intv = ctrl_inl(INT_STATUS);
	struct irq_desc *ext_desc;
	unsigned int ext_irq;
	unsigned int k = 0;

	while (intv) {
		ext_irq = voyagergx_stat2irq[k];
		if (ext_irq && (intv & 1)) {
			ext_desc = irq_desc + ext_irq;
			handle_level_irq(ext_irq, ext_desc);
		}
		intv >>= 1;
		k++;
	}
}

void __init setup_voyagergx_irq(void)
{
	printk(KERN_INFO "VoyagerGX on irq %d (mapped into %d to %d)\n",
	       IRQ_VOYAGER,
	       VOYAGER_IRQ_BASE,
	       VOYAGER_IRQ_BASE + VOYAGER_IRQ_NUM - 1);

	register_intc_controller(&intc_desc);
	set_irq_chained_handler(IRQ_VOYAGER, voyagergx_irq_demux);
}
