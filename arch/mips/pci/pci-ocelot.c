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
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
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
#include <linux/slab.h>
#include <linux/cache.h>
#include <asm/pci.h>
#include <asm/io.h>
#include <asm/gt64120.h>

static inline unsigned int pci0ReadConfigReg(unsigned int offset)
{
	unsigned int DataForRegCf8;
	unsigned int data;

	DataForRegCf8 = ((PCI_SLOT(device->devfn) << 11) |
			 (PCI_FUNC(device->devfn) << 8) |
			 (offset & ~0x3)) | 0x80000000;
	GT_WRITE(GT_PCI0_CFGADDR_OFS, DataForRegCf8);
	GT_READ(GT_PCI0_CFGDATA_OFS, &data);

	return data;
}

static inline void pci0WriteConfigReg(unsigned int offset, unsigned int data)
{
	unsigned int DataForRegCf8;

	DataForRegCf8 = ((PCI_SLOT(device->devfn) << 11) |
			 (PCI_FUNC(device->devfn) << 8) |
			 (offset & ~0x3)) | 0x80000000;
	GT_WRITE(GT_PCI0_CFGADDR_OFS, DataForRegCf8);
	GT_WRITE(GT_PCI0_CFGDATA_OFS, data);
}

static struct resource ocelot_mem_resource = {
	.start	= GT_PCI_MEM_BASE,
	.end	= GT_PCI_MEM_BASE + GT_PCI_MEM_BASE - 1,
};

static struct resource ocelot_io_resource = {
	.start	= GT_PCI_IO_BASE,
	.end	= GT_PCI_IO_BASE + GT_PCI_IO_SIZE - 1,
};

static struct pci_controller ocelot_pci_controller = {
	.pci_ops	= gt64xxx_pci0_ops,
	.mem_resource	= &ocelot_mem_resource,
	.io_resource	= &ocelot_io_resource,
};

static int __init ocelot_pcibios_init(void)
{
	u32 tmp;

	GT_READ(GT_PCI0_CMD_OFS, &tmp);
	GT_READ(GT_PCI0_BARE_OFS, &tmp);

	/*
	 * You have to enable bus mastering to configure any other
	 * card on the bus.
	 */
	tmp = pci0ReadConfigReg(PCI_COMMAND);
	tmp |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	pci0WriteConfigReg(PCI_COMMAND, tmp);

	register_pci_controller(&ocelot_pci_controller);
}

arch_initcall(ocelot_pcibios_init);
