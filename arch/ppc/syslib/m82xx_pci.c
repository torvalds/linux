/*
 *
 * (C) Copyright 2003
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * (C) Copyright 2004 Red Hat, Inc.
 *
 * 2005 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/immap_cpm2.h>
#include <asm/mpc8260.h>
#include <asm/cpm2.h>

#include "m82xx_pci.h"

/*
 * Interrupt routing
 */

static inline int
pq2pci_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *	PCI IDSEL/INTPIN->INTLINE
	 * 	  A      B      C      D
	 */
	{
		{ PIRQA, PIRQB, PIRQC, PIRQD },	/* IDSEL 22 - PCI slot 0 */
		{ PIRQD, PIRQA, PIRQB, PIRQC },	/* IDSEL 23 - PCI slot 1 */
		{ PIRQC, PIRQD, PIRQA, PIRQB },	/* IDSEL 24 - PCI slot 2 */
	};

	const long min_idsel = 22, max_idsel = 24, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

static void
pq2pci_mask_irq(unsigned int irq)
{
	int bit = irq - NR_CPM_INTS;

	*(volatile unsigned long *) PCI_INT_MASK_REG |= (1 << (31 - bit));
	return;
}

static void
pq2pci_unmask_irq(unsigned int irq)
{
	int bit = irq - NR_CPM_INTS;

	*(volatile unsigned long *) PCI_INT_MASK_REG &= ~(1 << (31 - bit));
	return;
}

static void
pq2pci_mask_and_ack(unsigned int irq)
{
	int bit = irq - NR_CPM_INTS;

	*(volatile unsigned long *) PCI_INT_MASK_REG |= (1 << (31 - bit));
	return;
}

static void
pq2pci_end_irq(unsigned int irq)
{
	int bit = irq - NR_CPM_INTS;

	*(volatile unsigned long *) PCI_INT_MASK_REG &= ~(1 << (31 - bit));
	return;
}

struct hw_interrupt_type pq2pci_ic = {
	"PQ2 PCI",
	NULL,
	NULL,
	pq2pci_unmask_irq,
	pq2pci_mask_irq,
	pq2pci_mask_and_ack,
	pq2pci_end_irq,
	0
};

static irqreturn_t
pq2pci_irq_demux(int irq, void *dev_id)
{
	unsigned long stat, mask, pend;
	int bit;

	for(;;) {
		stat = *(volatile unsigned long *) PCI_INT_STAT_REG;
		mask = *(volatile unsigned long *) PCI_INT_MASK_REG;
		pend = stat & ~mask & 0xf0000000;
		if (!pend)
			break;
		for (bit = 0; pend != 0; ++bit, pend <<= 1) {
			if (pend & 0x80000000)
				__do_IRQ(NR_CPM_INTS + bit);
		}
	}

	return IRQ_HANDLED;
}

static struct irqaction pq2pci_irqaction = {
	.handler = pq2pci_irq_demux,
	.flags 	 = IRQF_DISABLED,
	.mask	 = CPU_MASK_NONE,
	.name	 = "PQ2 PCI cascade",
};


void
pq2pci_init_irq(void)
{
	int irq;
	volatile cpm2_map_t *immap = cpm2_immr;
	for (irq = NR_CPM_INTS; irq < NR_CPM_INTS + 4; irq++)
		irq_desc[irq].chip = &pq2pci_ic;

	/* make PCI IRQ level sensitive */
	immap->im_intctl.ic_siexr &=
		~(1 << (14 - (PCI_INT_TO_SIU - SIU_INT_IRQ1)));

	/* mask all PCI interrupts */
	*(volatile unsigned long *) PCI_INT_MASK_REG |= 0xfff00000;

	/* install the demultiplexer for the PCI cascade interrupt */
	setup_irq(PCI_INT_TO_SIU, &pq2pci_irqaction);
	return;
}

static int
pq2pci_exclude_device(u_char bus, u_char devfn)
{
	return PCIBIOS_SUCCESSFUL;
}

/* PCI bus configuration registers.
 */
static void
pq2ads_setup_pci(struct pci_controller *hose)
{
	__u32 val;
	volatile cpm2_map_t *immap = cpm2_immr;
	bd_t* binfo = (bd_t*) __res;
	u32 sccr = immap->im_clkrst.car_sccr;
	uint pci_div,freq,time;
		/* PCI int lowest prio */
	/* Each 4 bits is a device bus request	and the MS 4bits
	 is highest priority */
	/* Bus                4bit value
	   ---                ----------
	   CPM high      	0b0000
	   CPM middle           0b0001
	   CPM low       	0b0010
	   PCI request          0b0011
	   Reserved      	0b0100
	   Reserved      	0b0101
	   Internal Core     	0b0110
	   External Master 1 	0b0111
	   External Master 2 	0b1000
	   External Master 3 	0b1001
	   The rest are reserved
	 */
	immap->im_siu_conf.siu_82xx.sc_ppc_alrh = 0x61207893;
	/* park bus on core */
	immap->im_siu_conf.siu_82xx.sc_ppc_acr = PPC_ACR_BUS_PARK_CORE;
	/*
	 * Set up master windows that allow the CPU to access PCI space. These
	 * windows are set up using the two SIU PCIBR registers.
	 */

	immap->im_memctl.memc_pcimsk0 = M82xx_PCI_PRIM_WND_SIZE;
	immap->im_memctl.memc_pcibr0  = M82xx_PCI_PRIM_WND_BASE | PCIBR_ENABLE;

#ifdef M82xx_PCI_SEC_WND_SIZE
	immap->im_memctl.memc_pcimsk1 = M82xx_PCI_SEC_WND_SIZE;
	immap->im_memctl.memc_pcibr1  = M82xx_PCI_SEC_WND_BASE | PCIBR_ENABLE;
#endif

	/* Enable PCI  */
	immap->im_pci.pci_gcr = cpu_to_le32(PCIGCR_PCI_BUS_EN);

	pci_div = ( (sccr & SCCR_PCI_MODCK) ? 2 : 1) *
			( ( (sccr & SCCR_PCIDF_MSK) >> SCCR_PCIDF_SHIFT) + 1);
	freq = (uint)((2*binfo->bi_cpmfreq)/(pci_div));
	time = (int)66666666/freq;

	/* due to PCI Local Bus spec, some devices needs to wait such a long
	time after RST 	deassertion. More specifically, 0.508s for 66MHz & twice more for 33 */
	printk("%s: The PCI bus is %d Mhz.\nWaiting %s after deasserting RST...\n",__FILE__,freq,
	(time==1) ? "0.5 seconds":"1 second" );

	{
		int i;
		for(i=0;i<(500*time);i++)
			udelay(1000);
	}

	/* setup ATU registers */
	immap->im_pci.pci_pocmr0 = cpu_to_le32(POCMR_ENABLE | POCMR_PCI_IO |
				((~(M82xx_PCI_IO_SIZE - 1U)) >> POTA_ADDR_SHIFT));
	immap->im_pci.pci_potar0 = cpu_to_le32(M82xx_PCI_LOWER_IO >> POTA_ADDR_SHIFT);
	immap->im_pci.pci_pobar0 = cpu_to_le32(M82xx_PCI_IO_BASE >> POTA_ADDR_SHIFT);

	/* Set-up non-prefetchable window */
	immap->im_pci.pci_pocmr1 = cpu_to_le32(POCMR_ENABLE | ((~(M82xx_PCI_MMIO_SIZE-1U)) >> POTA_ADDR_SHIFT));
	immap->im_pci.pci_potar1 = cpu_to_le32(M82xx_PCI_LOWER_MMIO >> POTA_ADDR_SHIFT);
	immap->im_pci.pci_pobar1 = cpu_to_le32((M82xx_PCI_LOWER_MMIO - M82xx_PCI_MMIO_OFFSET) >> POTA_ADDR_SHIFT);

	/* Set-up prefetchable window */
	immap->im_pci.pci_pocmr2 = cpu_to_le32(POCMR_ENABLE |POCMR_PREFETCH_EN |
		(~(M82xx_PCI_MEM_SIZE-1U) >> POTA_ADDR_SHIFT));
	immap->im_pci.pci_potar2 = cpu_to_le32(M82xx_PCI_LOWER_MEM >> POTA_ADDR_SHIFT);
	immap->im_pci.pci_pobar2 = cpu_to_le32((M82xx_PCI_LOWER_MEM - M82xx_PCI_MEM_OFFSET) >> POTA_ADDR_SHIFT);

 	/* Inbound transactions from PCI memory space */
	immap->im_pci.pci_picmr0 = cpu_to_le32(PICMR_ENABLE | PICMR_PREFETCH_EN |
					((~(M82xx_PCI_SLAVE_MEM_SIZE-1U)) >> PITA_ADDR_SHIFT));
	immap->im_pci.pci_pibar0 = cpu_to_le32(M82xx_PCI_SLAVE_MEM_BUS  >> PITA_ADDR_SHIFT);
	immap->im_pci.pci_pitar0 = cpu_to_le32(M82xx_PCI_SLAVE_MEM_LOCAL>> PITA_ADDR_SHIFT);

	/* park bus on PCI */
	immap->im_siu_conf.siu_82xx.sc_ppc_acr = PPC_ACR_BUS_PARK_PCI;

	/* Enable bus mastering and inbound memory transactions */
	early_read_config_dword(hose, hose->first_busno, 0, PCI_COMMAND, &val);
	val &= 0xffff0000;
	val |= PCI_COMMAND_MEMORY|PCI_COMMAND_MASTER;
	early_write_config_dword(hose, hose->first_busno, 0, PCI_COMMAND, val);

}

void __init pq2_find_bridges(void)
{
	extern int pci_assign_all_buses;
	struct pci_controller * hose;
	int host_bridge;

	pci_assign_all_buses = 1;

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	ppc_md.pci_swizzle = common_swizzle;

	hose->first_busno = 0;
	hose->bus_offset = 0;
	hose->last_busno = 0xff;

	setup_m8260_indirect_pci(hose,
				 (unsigned long)&cpm2_immr->im_pci.pci_cfg_addr,
				 (unsigned long)&cpm2_immr->im_pci.pci_cfg_data);

	/* Make sure it is a supported bridge */
	early_read_config_dword(hose,
				0,
				PCI_DEVFN(0,0),
				PCI_VENDOR_ID,
				&host_bridge);
	switch (host_bridge) {
		case PCI_DEVICE_ID_MPC8265:
			break;
		case PCI_DEVICE_ID_MPC8272:
			break;
		default:
			printk("Attempting to use unrecognized host bridge ID"
				" 0x%08x.\n", host_bridge);
			break;
	}

	pq2ads_setup_pci(hose);

	hose->io_space.start =	M82xx_PCI_LOWER_IO;
	hose->io_space.end = M82xx_PCI_UPPER_IO;
	hose->mem_space.start = M82xx_PCI_LOWER_MEM;
	hose->mem_space.end = M82xx_PCI_UPPER_MMIO;
	hose->pci_mem_offset = M82xx_PCI_MEM_OFFSET;

	isa_io_base =
	(unsigned long) ioremap(M82xx_PCI_IO_BASE,
					M82xx_PCI_IO_SIZE);
	hose->io_base_virt = (void *) isa_io_base;

	/* setup resources */
	pci_init_resource(&hose->mem_resources[0],
			M82xx_PCI_LOWER_MEM,
			M82xx_PCI_UPPER_MEM,
			IORESOURCE_MEM|IORESOURCE_PREFETCH, "PCI prefetchable memory");

	pci_init_resource(&hose->mem_resources[1],
			M82xx_PCI_LOWER_MMIO,
			M82xx_PCI_UPPER_MMIO,
			IORESOURCE_MEM, "PCI memory");

	pci_init_resource(&hose->io_resource,
			M82xx_PCI_LOWER_IO,
			M82xx_PCI_UPPER_IO,
			IORESOURCE_IO | 1, "PCI I/O");

	ppc_md.pci_exclude_device = pq2pci_exclude_device;
	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pci_map_irq = pq2pci_map_irq;
	ppc_md.pcibios_fixup = NULL;
	ppc_md.pcibios_fixup_bus = NULL;

}
