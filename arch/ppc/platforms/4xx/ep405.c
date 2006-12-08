/*
 * Embedded Planet 405GP board
 * http://www.embeddedplanet.com
 *
 * Author: Matthew Locke <mlocke@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/system.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/todc.h>
#include <asm/ocp.h>
#include <asm/ibm_ocp_pci.h>

#undef DEBUG
#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

u8 *ep405_bcsr;
u8 *ep405_nvram;

static struct {
	u8 cpld_xirq_select;
	int pci_idsel;
	int irq;
} ep405_devtable[] = {
#ifdef CONFIG_EP405PC
	{0x07, 0x0E, 25},		/* EP405PC: USB */
#endif
};

int __init
ppc405_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	int i;

	/* AFAICT this is only called a few times during PCI setup, so
	   performance is not critical */
	for (i = 0; i < ARRAY_SIZE(ep405_devtable); i++) {
		if (idsel == ep405_devtable[i].pci_idsel)
			return ep405_devtable[i].irq;
	}
	return -1;
};

void __init
ep405_setup_arch(void)
{
	ppc4xx_setup_arch();

	ibm_ocp_set_emac(0, 0);

	if (__res.bi_nvramsize == 512*1024) {
		/* FIXME: we should properly handle NVRTCs of different sizes */
		TODC_INIT(TODC_TYPE_DS1557, ep405_nvram, ep405_nvram, ep405_nvram, 8);
	}
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
	 */

	/* Disable region zero first */
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

	/* Disable region one */
	out_le32((void *) &(pcip->pmm[1].ma), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].la), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].pcila), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].pciha), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].ma), 0x00000000);
	out_le32((void *) &(pcip->ptm1ms), 0x00000000);

	/* Disable region two */
	out_le32((void *) &(pcip->pmm[2].ma), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].la), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].pcila), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].pciha), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].ma), 0x00000000);
	out_le32((void *) &(pcip->ptm2ms), 0x00000000);

	/* Configure PTM (PCI->PLB) region 1 */
	out_le32((void *) &(pcip->ptm1la), 0x00000000); /* PLB base address */
	/* Disable PTM region 2 */
	out_le32((void *) &(pcip->ptm2ms), 0x00000000);

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
#endif
}

void __init
ep405_map_io(void)
{
	bd_t *bip = &__res;

	ppc4xx_map_io();

	ep405_bcsr = ioremap(EP405_BCSR_PADDR, EP405_BCSR_SIZE);

	if (bip->bi_nvramsize > 0) {
		ep405_nvram = ioremap(EP405_NVRAM_PADDR, bip->bi_nvramsize);
	}
}

void __init
ep405_init_IRQ(void)
{
	int i;

	ppc4xx_init_IRQ();

	/* Workaround for a bug in the firmware it incorrectly sets
	   the IRQ polarities for XIRQ0 and XIRQ1 */
	mtdcr(DCRN_UIC_PR(DCRN_UIC0_BASE), 0xffffff80); /* set the polarity */
	mtdcr(DCRN_UIC_SR(DCRN_UIC0_BASE), 0x00000060); /* clear bogus interrupts */

	/* Activate the XIRQs from the CPLD */
	writeb(0xf0, ep405_bcsr+10);

	/* Set up IRQ routing */
	for (i = 0; i < ARRAY_SIZE(ep405_devtable); i++) {
		if ( (ep405_devtable[i].irq >= 25)
		     && (ep405_devtable[i].irq) <= 31) {
			writeb(ep405_devtable[i].cpld_xirq_select, ep405_bcsr+5);
			writeb(ep405_devtable[i].irq - 25, ep405_bcsr+6);
		}
	}
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = ep405_setup_arch;
	ppc_md.setup_io_mappings = ep405_map_io;
	ppc_md.init_IRQ = ep405_init_IRQ;

	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;

	if (__res.bi_nvramsize == 512*1024) {
		ppc_md.time_init = todc_time_init;
		ppc_md.set_rtc_time = todc_set_rtc_time;
		ppc_md.get_rtc_time = todc_get_rtc_time;
	} else {
		printk("EP405: NVRTC size is not 512k (not a DS1557).  Not sure what to do with it\n");
	}
}
