/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Galileo EV96100 board specific pci support.
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * This file was derived from Carsten Langgaard's
 * arch/mips/mips-boards/generic/pci.c
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
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

#include <asm/delay.h>
#include <asm/gt64120.h>
#include <asm/galileo-boards/ev96100.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

static int static gt96100_config_access(unsigned char access_type,
	struct pci_bus *bus, unsigned int devfn, int where, u32 * data)
{
	unsigned char bus = bus->number;
	u32 intr;

	/*
	 * Because of a bug in the galileo (for slot 31).
	 */
	if (bus == 0 && devfn >= PCI_DEVFN(31, 0))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Clear cause register bits */
	GT_WRITE(GT_INTRCAUSE_OFS, ~(GT_INTRCAUSE_MASABORT0_BIT |
				     GT_INTRCAUSE_TARABORT0_BIT));

	/* Setup address */
	GT_WRITE(GT_PCI0_CFGADDR_OFS,
		 (bus << GT_PCI0_CFGADDR_BUSNUM_SHF) |
		 (devfn << GT_PCI0_CFGADDR_FUNCTNUM_SHF) |
		 ((where / 4) << GT_PCI0_CFGADDR_REGNUM_SHF) |
		 GT_PCI0_CFGADDR_CONFIGEN_BIT);
	udelay(2);


	if (access_type == PCI_ACCESS_WRITE) {
		if (devfn != 0)
			*data = le32_to_cpu(*data);
		GT_WRITE(GT_PCI0_CFGDATA_OFS, *data);
	} else {
		*data = GT_READ(GT_PCI0_CFGDATA_OFS);
		if (devfn != 0)
			*data = le32_to_cpu(*data);
	}

	udelay(2);

	/* Check for master or target abort */
	intr = GT_READ(GT_INTRCAUSE_OFS);

	if (intr & (GT_INTRCAUSE_MASABORT0_BIT | GT_INTRCAUSE_TARABORT0_BIT)) {
		/* Error occured */

		/* Clear bits */
		GT_WRITE(GT_INTRCAUSE_OFS, ~(GT_INTRCAUSE_MASABORT0_BIT |
					     GT_INTRCAUSE_TARABORT0_BIT));
		return -1;
	}
	return 0;
}

/*
 * We can't address 8 and 16 bit words directly.  Instead we have to
 * read/write a 32bit word and mask/modify the data we actually want.
 */
static int gt96100_pcibios_read(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 * val)
{
	u32 data = 0;

	if (gt96100_config_access(PCI_ACCESS_READ, bus, devfn, where, &data))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		*val = (data >> ((where & 3) << 3)) & 0xff;
		break;

	case 2:
		*val = (data >> ((where & 3) << 3)) & 0xffff;
		break;

	case 4:
		*val = data;
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int gt96100_pcibios_write(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 val)
{
	u32 data = 0;

	switch (size) {
	case 1:
		if (gt96100_config_access(PCI_ACCESS_READ, bus, devfn, where, &data))
			return -1;

		data = (data & ~(0xff << ((where & 3) << 3))) |
		       (val << ((where & 3) << 3));

		if (gt96100_config_access(PCI_ACCESS_WRITE, bus, devfn, where, &data))
			return -1;

		return PCIBIOS_SUCCESSFUL;

	case 2:
		if (gt96100_config_access(PCI_ACCESS_READ, bus, devfn, where, &data))
			return -1;

		data = (data & ~(0xffff << ((where & 3) << 3))) |
		       (val << ((where & 3) << 3));

		if (gt96100_config_access(PCI_ACCESS_WRITE, dev, where, &data))
			return -1;


		return PCIBIOS_SUCCESSFUL;

	case 4:
		if (gt96100_config_access(PCI_ACCESS_WRITE, dev, where, &val))
			return -1;

		return PCIBIOS_SUCCESSFUL;
	}
}

struct pci_ops gt96100_pci_ops = {
	.read	= gt96100_pcibios_read,
	.write	= gt96100_pcibios_write
};
