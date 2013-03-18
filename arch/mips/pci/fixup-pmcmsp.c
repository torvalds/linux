/*
 * PMC-Sierra MSP board specific pci fixups.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Copyright 2005-2007 PMC-Sierra, Inc
 *
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef CONFIG_PCI

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/byteorder.h>

#include <msp_pci.h>
#include <msp_cic_int.h>

/* PCI interrupt pins */
#define IRQ4	MSP_INT_EXT4
#define IRQ5	MSP_INT_EXT5
#define IRQ6	MSP_INT_EXT6

#if defined(CONFIG_PMC_MSP7120_GW)
/* Garibaldi Board IRQ wiring to PCI slots */
static char irq_tab[][5] __initdata = {
	/* INTA	   INTB	   INTC	   INTD */
	{0,	0,	0,	0,	0 },	/*    (AD[0]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[1]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[2]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[3]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[4]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[5]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[6]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[7]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[8]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[9]): Unused */
	{0,	0,	0,	0,	0 },	/*  0 (AD[10]): Unused */
	{0,	0,	0,	0,	0 },	/*  1 (AD[11]): Unused */
	{0,	0,	0,	0,	0 },	/*  2 (AD[12]): Unused */
	{0,	0,	0,	0,	0 },	/*  3 (AD[13]): Unused */
	{0,	0,	0,	0,	0 },	/*  4 (AD[14]): Unused */
	{0,	0,	0,	0,	0 },	/*  5 (AD[15]): Unused */
	{0,	0,	0,	0,	0 },	/*  6 (AD[16]): Unused */
	{0,	0,	0,	0,	0 },	/*  7 (AD[17]): Unused */
	{0,	0,	0,	0,	0 },	/*  8 (AD[18]): Unused */
	{0,	0,	0,	0,	0 },	/*  9 (AD[19]): Unused */
	{0,	0,	0,	0,	0 },	/* 10 (AD[20]): Unused */
	{0,	0,	0,	0,	0 },	/* 11 (AD[21]): Unused */
	{0,	0,	0,	0,	0 },	/* 12 (AD[22]): Unused */
	{0,	0,	0,	0,	0 },	/* 13 (AD[23]): Unused */
	{0,	0,	0,	0,	0 },	/* 14 (AD[24]): Unused */
	{0,	0,	0,	0,	0 },	/* 15 (AD[25]): Unused */
	{0,	0,	0,	0,	0 },	/* 16 (AD[26]): Unused */
	{0,	0,	0,	0,	0 },	/* 17 (AD[27]): Unused */
	{0,	IRQ4,	IRQ4,	0,	0 },	/* 18 (AD[28]): slot 0 */
	{0,	0,	0,	0,	0 },	/* 19 (AD[29]): Unused */
	{0,	IRQ5,	IRQ5,	0,	0 },	/* 20 (AD[30]): slot 1 */
	{0,	IRQ6,	IRQ6,	0,	0 }	/* 21 (AD[31]): slot 2 */
};

#elif defined(CONFIG_PMC_MSP7120_EVAL)

/* MSP7120 Eval Board IRQ wiring to PCI slots */
static char irq_tab[][5] __initdata = {
	/* INTA	   INTB	   INTC	   INTD */
	{0,	0,	0,	0,	0 },	/*    (AD[0]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[1]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[2]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[3]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[4]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[5]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[6]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[7]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[8]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[9]): Unused */
	{0,	0,	0,	0,	0 },	/*  0 (AD[10]): Unused */
	{0,	0,	0,	0,	0 },	/*  1 (AD[11]): Unused */
	{0,	0,	0,	0,	0 },	/*  2 (AD[12]): Unused */
	{0,	0,	0,	0,	0 },	/*  3 (AD[13]): Unused */
	{0,	0,	0,	0,	0 },	/*  4 (AD[14]): Unused */
	{0,	0,	0,	0,	0 },	/*  5 (AD[15]): Unused */
	{0,	IRQ6,	IRQ6,	0,	0 },	/*  6 (AD[16]): slot 3 (mini) */
	{0,	IRQ5,	IRQ5,	0,	0 },	/*  7 (AD[17]): slot 2 (mini) */
	{0,	IRQ4,	IRQ4,	IRQ4,	IRQ4},	/*  8 (AD[18]): slot 0 (PCI) */
	{0,	IRQ5,	IRQ5,	IRQ5,	IRQ5},	/*  9 (AD[19]): slot 1 (PCI) */
	{0,	0,	0,	0,	0 },	/* 10 (AD[20]): Unused */
	{0,	0,	0,	0,	0 },	/* 11 (AD[21]): Unused */
	{0,	0,	0,	0,	0 },	/* 12 (AD[22]): Unused */
	{0,	0,	0,	0,	0 },	/* 13 (AD[23]): Unused */
	{0,	0,	0,	0,	0 },	/* 14 (AD[24]): Unused */
	{0,	0,	0,	0,	0 },	/* 15 (AD[25]): Unused */
	{0,	0,	0,	0,	0 },	/* 16 (AD[26]): Unused */
	{0,	0,	0,	0,	0 },	/* 17 (AD[27]): Unused */
	{0,	0,	0,	0,	0 },	/* 18 (AD[28]): Unused */
	{0,	0,	0,	0,	0 },	/* 19 (AD[29]): Unused */
	{0,	0,	0,	0,	0 },	/* 20 (AD[30]): Unused */
	{0,	0,	0,	0,	0 }	/* 21 (AD[31]): Unused */
};

#else

/* Unknown board -- don't assign any IRQs */
static char irq_tab[][5] __initdata = {
	/* INTA	   INTB	   INTC	   INTD */
	{0,	0,	0,	0,	0 },	/*    (AD[0]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[1]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[2]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[3]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[4]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[5]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[6]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[7]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[8]): Unused */
	{0,	0,	0,	0,	0 },	/*    (AD[9]): Unused */
	{0,	0,	0,	0,	0 },	/*  0 (AD[10]): Unused */
	{0,	0,	0,	0,	0 },	/*  1 (AD[11]): Unused */
	{0,	0,	0,	0,	0 },	/*  2 (AD[12]): Unused */
	{0,	0,	0,	0,	0 },	/*  3 (AD[13]): Unused */
	{0,	0,	0,	0,	0 },	/*  4 (AD[14]): Unused */
	{0,	0,	0,	0,	0 },	/*  5 (AD[15]): Unused */
	{0,	0,	0,	0,	0 },	/*  6 (AD[16]): Unused */
	{0,	0,	0,	0,	0 },	/*  7 (AD[17]): Unused */
	{0,	0,	0,	0,	0 },	/*  8 (AD[18]): Unused */
	{0,	0,	0,	0,	0 },	/*  9 (AD[19]): Unused */
	{0,	0,	0,	0,	0 },	/* 10 (AD[20]): Unused */
	{0,	0,	0,	0,	0 },	/* 11 (AD[21]): Unused */
	{0,	0,	0,	0,	0 },	/* 12 (AD[22]): Unused */
	{0,	0,	0,	0,	0 },	/* 13 (AD[23]): Unused */
	{0,	0,	0,	0,	0 },	/* 14 (AD[24]): Unused */
	{0,	0,	0,	0,	0 },	/* 15 (AD[25]): Unused */
	{0,	0,	0,	0,	0 },	/* 16 (AD[26]): Unused */
	{0,	0,	0,	0,	0 },	/* 17 (AD[27]): Unused */
	{0,	0,	0,	0,	0 },	/* 18 (AD[28]): Unused */
	{0,	0,	0,	0,	0 },	/* 19 (AD[29]): Unused */
	{0,	0,	0,	0,	0 },	/* 20 (AD[30]): Unused */
	{0,	0,	0,	0,	0 }	/* 21 (AD[31]): Unused */
};
#endif

/*****************************************************************************
 *
 *  FUNCTION: pcibios_plat_dev_init
 *  _________________________________________________________________________
 *
 *  DESCRIPTION: Perform platform specific device initialization at
 *		 pci_enable_device() time.
 *		 None are needed for the MSP7120 PCI Controller.
 *
 *  INPUTS:	 dev	 - structure describing the PCI device
 *
 *  OUTPUTS:	 none
 *
 *  RETURNS:	 PCIBIOS_SUCCESSFUL
 *
 ****************************************************************************/
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return PCIBIOS_SUCCESSFUL;
}

/*****************************************************************************
 *
 *  FUNCTION: pcibios_map_irq
 *  _________________________________________________________________________
 *
 *  DESCRIPTION: Perform board supplied PCI IRQ mapping routine.
 *
 *  INPUTS:	 dev	 - unused
 *		 slot	 - PCI slot. Identified by which bit of the AD[] bus
 *			   drives the IDSEL line. AD[10] is 0, AD[31] is
 *			   slot 21.
 *		 pin	 - numbered using the scheme of the PCI_INTERRUPT_PIN
 *			   field of the config header.
 *
 *  OUTPUTS:	 none
 *
 *  RETURNS:	 IRQ number
 *
 ****************************************************************************/
int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
#if !defined(CONFIG_PMC_MSP7120_GW) && !defined(CONFIG_PMC_MSP7120_EVAL)
	printk(KERN_WARNING "PCI: unknown board, no PCI IRQs assigned.\n");
#endif
	printk(KERN_WARNING "PCI: irq_tab returned %d for slot=%d pin=%d\n",
		irq_tab[slot][pin], slot, pin);

	return irq_tab[slot][pin];
}

#endif	/* CONFIG_PCI */
