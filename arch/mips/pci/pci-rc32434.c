/*
 *  BRIEF MODULE DESCRIPTION
 *     PCI initialization for IDT EB434 board
 *
 *  Copyright 2004 IDT Inc. (rischelp@idt.com)
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

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mach-rc32434/rc32434.h>
#include <asm/mach-rc32434/pci.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

/* define an unsigned array for the PCI registers */
static unsigned int korina_cnfg_regs[25] = {
	KORINA_CNFG1, KORINA_CNFG2, KORINA_CNFG3, KORINA_CNFG4,
	KORINA_CNFG5, KORINA_CNFG6, KORINA_CNFG7, KORINA_CNFG8,
	KORINA_CNFG9, KORINA_CNFG10, KORINA_CNFG11, KORINA_CNFG12,
	KORINA_CNFG13, KORINA_CNFG14, KORINA_CNFG15, KORINA_CNFG16,
	KORINA_CNFG17, KORINA_CNFG18, KORINA_CNFG19, KORINA_CNFG20,
	KORINA_CNFG21, KORINA_CNFG22, KORINA_CNFG23, KORINA_CNFG24
};
static struct resource rc32434_res_pci_mem1;
static struct resource rc32434_res_pci_mem2;

static struct resource rc32434_res_pci_mem1 = {
	.name = "PCI MEM1",
	.start = 0x50000000,
	.end = 0x5FFFFFFF,
	.flags = IORESOURCE_MEM,
	.parent = &rc32434_res_pci_mem1,
	.sibling = NULL,
	.child = &rc32434_res_pci_mem2
};

static struct resource rc32434_res_pci_mem2 = {
	.name = "PCI Mem2",
	.start = 0x60000000,
	.end = 0x6FFFFFFF,
	.flags = IORESOURCE_MEM,
	.parent = &rc32434_res_pci_mem1,
	.sibling = NULL,
	.child = NULL
};

static struct resource rc32434_res_pci_io1 = {
	.name = "PCI I/O1",
	.start = 0x18800000,
	.end = 0x188FFFFF,
	.flags = IORESOURCE_IO,
};

extern struct pci_ops rc32434_pci_ops;

#define PCI_MEM1_START	PCI_ADDR_START
#define PCI_MEM1_END	(PCI_ADDR_START + CPUTOPCI_MEM_WIN - 1)
#define PCI_MEM2_START	(PCI_ADDR_START + CPUTOPCI_MEM_WIN)
#define PCI_MEM2_END	(PCI_ADDR_START + (2 * CPUTOPCI_MEM_WIN)  - 1)
#define PCI_IO1_START	(PCI_ADDR_START + (2 * CPUTOPCI_MEM_WIN))
#define PCI_IO1_END 							\
	(PCI_ADDR_START + (2 * CPUTOPCI_MEM_WIN) + CPUTOPCI_IO_WIN - 1)
#define PCI_IO2_START							\
	(PCI_ADDR_START + (2 * CPUTOPCI_MEM_WIN) + CPUTOPCI_IO_WIN)
#define PCI_IO2_END 							\
	(PCI_ADDR_START + (2 * CPUTOPCI_MEM_WIN) + (2 * CPUTOPCI_IO_WIN) - 1)

struct pci_controller rc32434_controller2;

struct pci_controller rc32434_controller = {
	.pci_ops = &rc32434_pci_ops,
	.mem_resource = &rc32434_res_pci_mem1,
	.io_resource = &rc32434_res_pci_io1,
	.mem_offset = 0,
	.io_offset = 0,

};

#ifdef __MIPSEB__
#define PCI_ENDIAN_FLAG PCILBAC_sb_m
#else
#define PCI_ENDIAN_FLAG 0
#endif

static int __init rc32434_pcibridge_init(void)
{
	unsigned int pcicvalue, pcicdata = 0;
	unsigned int dummyread, pcicntlval;
	int loopCount;
	unsigned int pci_config_addr;

	pcicvalue = rc32434_pci->pcic;
	pcicvalue = (pcicvalue >> PCIM_SHFT) & PCIM_BIT_LEN;
	if (!((pcicvalue == PCIM_H_EA) ||
	      (pcicvalue == PCIM_H_IA_FIX) ||
	      (pcicvalue == PCIM_H_IA_RR))) {
		pr_err(KERN_ERR "PCI init error!!!\n");
		/* Not in Host Mode, return ERROR */
		return -1;
	}
	/* Enables the Idle Grant mode, Arbiter Parking */
	pcicdata |= (PCI_CTL_IGM | PCI_CTL_EAP | PCI_CTL_EN);
	rc32434_pci->pcic = pcicdata;	/* Enable the PCI bus Interface */
	/* Zero out the PCI status & PCI Status Mask */
	for (;;) {
		pcicdata = rc32434_pci->pcis;
		if (!(pcicdata & PCI_STAT_RIP))
			break;
	}

	rc32434_pci->pcis = 0;
	rc32434_pci->pcism = 0xFFFFFFFF;
	/* Zero out the PCI decoupled registers */
	rc32434_pci->pcidac = 0;	/*
					 * disable PCI decoupled accesses at
					 * initialization
					 */
	rc32434_pci->pcidas = 0;	/* clear the status */
	rc32434_pci->pcidasm = 0x0000007F;	/* Mask all the interrupts */
	/* Mask PCI Messaging Interrupts */
	rc32434_pci_msg->pciiic = 0;
	rc32434_pci_msg->pciiim = 0xFFFFFFFF;
	rc32434_pci_msg->pciioic = 0;
	rc32434_pci_msg->pciioim = 0;


	/* Setup PCILB0 as Memory Window */
	rc32434_pci->pcilba[0].address = (unsigned int) (PCI_ADDR_START);

	/* setup the PCI map address as same as the local address */

	rc32434_pci->pcilba[0].mapping = (unsigned int) (PCI_ADDR_START);


	/* Setup PCILBA1 as MEM */
	rc32434_pci->pcilba[0].control =
	    (((SIZE_256MB & 0x1f) << PCI_LBAC_SIZE_BIT) | PCI_ENDIAN_FLAG);
	dummyread = rc32434_pci->pcilba[0].control;	/* flush the CPU write Buffers */
	rc32434_pci->pcilba[1].address = 0x60000000;
	rc32434_pci->pcilba[1].mapping = 0x60000000;

	/* setup PCILBA2 as IO Window */
	rc32434_pci->pcilba[1].control =
	    (((SIZE_256MB & 0x1f) << PCI_LBAC_SIZE_BIT) | PCI_ENDIAN_FLAG);
	dummyread = rc32434_pci->pcilba[1].control;	/* flush the CPU write Buffers */
	rc32434_pci->pcilba[2].address = 0x18C00000;
	rc32434_pci->pcilba[2].mapping = 0x18FFFFFF;

	/* setup PCILBA2 as IO Window */
	rc32434_pci->pcilba[2].control =
	    (((SIZE_4MB & 0x1f) << PCI_LBAC_SIZE_BIT) | PCI_ENDIAN_FLAG);
	dummyread = rc32434_pci->pcilba[2].control;	/* flush the CPU write Buffers */

	/* Setup PCILBA3 as IO Window */
	rc32434_pci->pcilba[3].address = 0x18800000;
	rc32434_pci->pcilba[3].mapping = 0x18800000;
	rc32434_pci->pcilba[3].control =
	    ((((SIZE_1MB & 0x1ff) << PCI_LBAC_SIZE_BIT) | PCI_LBAC_MSI) |
	     PCI_ENDIAN_FLAG);
	dummyread = rc32434_pci->pcilba[3].control;	/* flush the CPU write Buffers */

	pci_config_addr = (unsigned int) (0x80000004);
	for (loopCount = 0; loopCount < 24; loopCount++) {
		rc32434_pci->pcicfga = pci_config_addr;
		dummyread = rc32434_pci->pcicfga;
		rc32434_pci->pcicfgd = korina_cnfg_regs[loopCount];
		dummyread = rc32434_pci->pcicfgd;
		pci_config_addr += 4;
	}
	rc32434_pci->pcitc =
	    (unsigned int) ((PCITC_RTIMER_VAL & 0xff) << PCI_TC_RTIMER_BIT) |
	    ((PCITC_DTIMER_VAL & 0xff) << PCI_TC_DTIMER_BIT);

	pcicntlval = rc32434_pci->pcic;
	pcicntlval &= ~PCI_CTL_TNR;
	rc32434_pci->pcic = pcicntlval;
	pcicntlval = rc32434_pci->pcic;

	return 0;
}

static int __init rc32434_pci_init(void)
{
	pr_info("PCI: Initializing PCI\n");

	ioport_resource.start = rc32434_res_pci_io1.start;
	ioport_resource.end = rc32434_res_pci_io1.end;

	rc32434_pcibridge_init();

	register_pci_controller(&rc32434_controller);
	rc32434_sync();

	return 0;
}

arch_initcall(rc32434_pci_init);
