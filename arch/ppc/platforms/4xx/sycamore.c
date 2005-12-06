/*
 * arch/ppc/platforms/4xx/sycamore.c
 *
 * Architecture- / platform-specific boot-time initialization code for
 * IBM PowerPC 4xx based boards.
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2000-2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/rtc.h>

#include <asm/ocp.h>
#include <asm/ppc4xx_pic.h>
#include <asm/system.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/ibm_ocp_pci.h>
#include <asm/todc.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

void *kb_cs;
void *kb_data;
void *sycamore_rtc_base;

/*
 * Define external IRQ senses and polarities.
 */
unsigned char ppc4xx_uic_ext_irq_cfg[] __initdata = {
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 7 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 8 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 9 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 10 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 11 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 12 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 0 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 3 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 4 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 5 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 6 */
};


/* Some IRQs unique to Sycamore.
 * Used by the generic 405 PCI setup functions in ppc4xx_pci.c
 */
int __init
ppc405_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	    /*
	     *      PCI IDSEL/INTPIN->INTLINE
	     *      A       B       C       D
	     */
	{
		{28, 28, 28, 28},	/* IDSEL 1 - PCI slot 1 */
		{29, 29, 29, 29},	/* IDSEL 2 - PCI slot 2 */
		{30, 30, 30, 30},	/* IDSEL 3 - PCI slot 3 */
		{31, 31, 31, 31},	/* IDSEL 4 - PCI slot 4 */
	};

	const long min_idsel = 1, max_idsel = 4, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
};

void __init
sycamore_setup_arch(void)
{
	void *fpga_brdc;
	unsigned char fpga_brdc_data;
	void *fpga_enable;
	void *fpga_polarity;
	void *fpga_status;
	void *fpga_trigger;

	ppc4xx_setup_arch();

	ibm_ocp_set_emac(0, 0);

	kb_data = ioremap(SYCAMORE_PS2_BASE, 8);
	if (!kb_data) {
		printk(KERN_CRIT
		       "sycamore_setup_arch() kb_data ioremap failed\n");
		return;
	}

	kb_cs = kb_data + 1;

	fpga_status = ioremap(PPC40x_FPGA_BASE, 8);
	if (!fpga_status) {
		printk(KERN_CRIT
		       "sycamore_setup_arch() fpga_status ioremap failed\n");
		return;
	}

	fpga_enable = fpga_status + 1;
	fpga_polarity = fpga_status + 2;
	fpga_trigger = fpga_status + 3;
	fpga_brdc = fpga_status + 4;

	/* split the keyboard and mouse interrupts */
	fpga_brdc_data = readb(fpga_brdc);
	fpga_brdc_data |= 0x80;
	writeb(fpga_brdc_data, fpga_brdc);

	writeb(0x3, fpga_enable);

	writeb(0x3, fpga_polarity);

	writeb(0x3, fpga_trigger);

	/* RTC step for the sycamore */
	sycamore_rtc_base = (void *) SYCAMORE_RTC_VADDR;
	TODC_INIT(TODC_TYPE_DS1743, sycamore_rtc_base, sycamore_rtc_base,
		  sycamore_rtc_base, 8);

	/* Identify the system */
	printk(KERN_INFO "IBM Sycamore (IBM405GPr) Platform\n");
	printk(KERN_INFO
	       "Port by MontaVista Software, Inc. (source@mvista.com)\n");
}

void __init
bios_fixup(struct pci_controller *hose, struct pcil0_regs *pcip)
{
#ifdef CONFIG_PCI
	unsigned int bar_response, bar;
	/*
	 * Expected PCI mapping:
	 *
	 *  PLB addr             PCI memory addr
	 *  ---------------------       ---------------------
	 *  0000'0000 - 7fff'ffff <---  0000'0000 - 7fff'ffff
	 *  8000'0000 - Bfff'ffff --->  8000'0000 - Bfff'ffff
	 *
	 *  PLB addr             PCI io addr
	 *  ---------------------       ---------------------
	 *  e800'0000 - e800'ffff --->  0000'0000 - 0001'0000
	 *
	 * The following code is simplified by assuming that the bootrom
	 * has been well behaved in following this mapping.
	 */

#ifdef DEBUG
	int i;

	printk("ioremap PCLIO_BASE = 0x%x\n", pcip);
	printk("PCI bridge regs before fixup \n");
	for (i = 0; i <= 3; i++) {
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].ma)));
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].la)));
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].pcila)));
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].pciha)));
	}
	printk(" ptm1ms\t0x%x\n", in_le32(&(pcip->ptm1ms)));
	printk(" ptm1la\t0x%x\n", in_le32(&(pcip->ptm1la)));
	printk(" ptm2ms\t0x%x\n", in_le32(&(pcip->ptm2ms)));
	printk(" ptm2la\t0x%x\n", in_le32(&(pcip->ptm2la)));

#endif

	/* added for IBM boot rom version 1.15 bios bar changes  -AK */

	/* Disable region first */
	out_le32((void *) &(pcip->pmm[0].ma), 0x00000000);
	/* PLB starting addr, PCI: 0x80000000 */
	out_le32((void *) &(pcip->pmm[0].la), 0x80000000);
	/* PCI start addr, 0x80000000 */
	out_le32((void *) &(pcip->pmm[0].pcila), PPC405_PCI_MEM_BASE);
	/* 512MB range of PLB to PCI */
	out_le32((void *) &(pcip->pmm[0].pciha), 0x00000000);
	/* Enable no pre-fetch, enable region */
	out_le32((void *) &(pcip->pmm[0].ma), ((0xffffffff -
						(PPC405_PCI_UPPER_MEM -
						 PPC405_PCI_MEM_BASE)) | 0x01));

	/* Enable inbound region one - 1GB size */
	out_le32((void *) &(pcip->ptm1ms), 0xc0000001);

	/* Disable outbound region one */
	out_le32((void *) &(pcip->pmm[1].ma), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].la), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].pcila), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].pciha), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].ma), 0x00000000);

	/* Disable inbound region two */
	out_le32((void *) &(pcip->ptm2ms), 0x00000000);

	/* Disable outbound region two */
	out_le32((void *) &(pcip->pmm[2].ma), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].la), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].pcila), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].pciha), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].ma), 0x00000000);

	/* Zero config bars */
	for (bar = PCI_BASE_ADDRESS_1; bar <= PCI_BASE_ADDRESS_2; bar += 4) {
		early_write_config_dword(hose, hose->first_busno,
					 PCI_FUNC(hose->first_busno), bar,
					 0x00000000);
		early_read_config_dword(hose, hose->first_busno,
					PCI_FUNC(hose->first_busno), bar,
					&bar_response);
		DBG("BUS %d, device %d, Function %d bar 0x%8.8x is 0x%8.8x\n",
		    hose->first_busno, PCI_SLOT(hose->first_busno),
		    PCI_FUNC(hose->first_busno), bar, bar_response);
	}
	/* end work arround */

#ifdef DEBUG
	printk("PCI bridge regs after fixup \n");
	for (i = 0; i <= 3; i++) {
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].ma)));
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].la)));
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].pcila)));
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].pciha)));
	}
	printk(" ptm1ms\t0x%x\n", in_le32(&(pcip->ptm1ms)));
	printk(" ptm1la\t0x%x\n", in_le32(&(pcip->ptm1la)));
	printk(" ptm2ms\t0x%x\n", in_le32(&(pcip->ptm2ms)));
	printk(" ptm2la\t0x%x\n", in_le32(&(pcip->ptm2la)));

#endif
#endif

}

void __init
sycamore_map_io(void)
{
	ppc4xx_map_io();
	io_block_mapping(SYCAMORE_RTC_VADDR,
			 SYCAMORE_RTC_PADDR, SYCAMORE_RTC_SIZE, _PAGE_IO);
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = sycamore_setup_arch;
	ppc_md.setup_io_mappings = sycamore_map_io;

#ifdef CONFIG_GEN_RTC
	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;
#endif
}
