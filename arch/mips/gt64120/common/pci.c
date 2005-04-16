/*
 * BRIEF MODULE DESCRIPTION
 * Galileo Evaluation Boards PCI support.
 *
 * The general-purpose functions to read/write and configure the GT64120A's
 * PCI registers (function names start with pci0 or pci1) are either direct
 * copies of functions written by Galileo Technology, or are modifications
 * of their functions to work with Linux 2.4 vs Linux 2.2.  These functions
 * are Copyright - Galileo Technology.
 *
 * Other functions are derived from other MIPS PCI implementations, or were
 * written by RidgeRun, Inc,  Copyright (C) 2000 RidgeRun, Inc.
 *   glonnon@ridgerun.com, skranz@ridgerun.com, stevej@ridgerun.com
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
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <asm/gt64120.h>

#define SELF 0

/*
 * pciXReadConfigReg  - Read from a PCI configuration register
 *                    - Make sure the GT is configured as a master before
 *                      reading from another device on the PCI.
 *                   - The function takes care of Big/Little endian conversion.
 * INPUTS:   regOffset: The register offset as it apears in the GT spec (or PCI
 *                        spec)
 *           pciDevNum: The device number needs to be addressed.
 * RETURNS: data , if the data == 0xffffffff check the master abort bit in the
 *                 cause register to make sure the data is valid
 *
 *  Configuration Address 0xCF8:
 *
 *       31 30    24 23  16 15  11 10     8 7      2  0     <=bit Number
 *  |congif|Reserved|  Bus |Device|Function|Register|00|
 *  |Enable|        |Number|Number| Number | Number |  |    <=field Name
 *
 */
static unsigned int pci0ReadConfigReg(int offset, struct pci_dev *device)
{
	unsigned int DataForRegCf8;
	unsigned int data;

	DataForRegCf8 = ((PCI_SLOT(device->devfn) << 11) |
			 (PCI_FUNC(device->devfn) << 8) |
			 (offset & ~0x3)) | 0x80000000;
	GT_WRITE(GT_PCI0_CFGADDR_OFS, DataForRegCf8);

	/*
	 * The casual observer might wonder why the READ is duplicated here,
	 * rather than immediately following the WRITE, and just have the swap
	 * in the "if".  That's because there is a latency problem with trying
	 * to read immediately after setting up the address register.  The "if"
	 * check gives enough time for the address to stabilize, so the READ
	 * can work.
	 */
	if (PCI_SLOT(device->devfn) == SELF)	/* This board */
		return GT_READ(GT_PCI0_CFGDATA_OFS);
	else		/* PCI is little endian so swap the Data. */
		return __GT_READ(GT_PCI0_CFGDATA_OFS);
}

/*
 * pciXWriteConfigReg - Write to a PCI configuration register
 *                    - Make sure the GT is configured as a master before
 *                      writingto another device on the PCI.
 *                    - The function takes care of Big/Little endian conversion.
 * Inputs:   unsigned int regOffset: The register offset as it apears in the
 *           GT spec
 *                   (or any other PCI device spec)
 *           pciDevNum: The device number needs to be addressed.
 *
 *  Configuration Address 0xCF8:
 *
 *       31 30    24 23  16 15  11 10     8 7      2  0     <=bit Number
 *  |congif|Reserved|  Bus |Device|Function|Register|00|
 *  |Enable|        |Number|Number| Number | Number |  |    <=field Name
 *
 */
static void pci0WriteConfigReg(unsigned int offset,
			       struct pci_dev *device, unsigned int data)
{
	unsigned int DataForRegCf8;

	DataForRegCf8 = ((PCI_SLOT(device->devfn) << 11) |
			 (PCI_FUNC(device->devfn) << 8) |
			 (offset & ~0x3)) | 0x80000000;
	GT_WRITE(GT_PCI0_CFGADDR_OFS, DataForRegCf8);

	if (PCI_SLOT(device->devfn) == SELF) 	/* This board */
		GT_WRITE(GT_PCI0_CFGDATA_OFS, data);
	else 			/* configuration Transaction over the pci. */
		__GT_WRITE(GT_PCI0_CFGDATA_OFS, data);
}

extern struct pci_ops gt64120_pci_ops;

void __init pcibios_init(void)
{
	u32 tmp;
	struct pci_dev controller;

	controller.devfn = SELF;

	tmp = GT_READ(GT_PCI0_CMD_OFS);		/* Huh??? -- Ralf  */
	tmp = GT_READ(GT_PCI0_BARE_OFS);

	/*
	 * You have to enable bus mastering to configure any other
	 * card on the bus.
	 */
	tmp = pci0ReadConfigReg(PCI_COMMAND, &controller);
	tmp |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	pci0WriteConfigReg(PCI_COMMAND, &controller, tmp);

	/*
	 *  Reset PCI I/O and PCI MEM values to ones supported by EVM.
	 */
	ioport_resource.start	= GT_PCI_IO_BASE;
	ioport_resource.end	= GT_PCI_IO_BASE + GT_PCI_IO_SIZE - 1;
	iomem_resource.start	= GT_PCI_MEM_BASE;
	iomem_resource.end	= GT_PCI_MEM_BASE + GT_PCI_MEM_SIZE - 1;

	pci_scan_bus(0, &gt64120_pci_ops, NULL);
}
