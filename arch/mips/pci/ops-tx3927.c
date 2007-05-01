/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              ahennessy@mvista.com
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 *
 * Based on arch/mips/ddb5xxx/ddb5477/pci_ops.c
 *
 *     Define the pci_ops for JMR3927.
 *
 * Much of the code is derived from the original DDB5074 port by
 * Geert Uytterhoeven <geert@sonycom.com>
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

#include <asm/addrspace.h>
#include <asm/jmr3927/jmr3927.h>

static inline int mkaddr(unsigned char bus, unsigned char dev_fn,
	unsigned char where)
{
	if (bus == 0 && dev_fn >= PCI_DEVFN(TX3927_PCIC_MAX_DEVNU, 0))
		return PCIBIOS_DEVICE_NOT_FOUND;

	tx3927_pcicptr->ica = ((bus & 0xff) << 0x10) |
	                      ((dev_fn & 0xff) << 0x08) |
	                      (where & 0xfc);

	/* clear M_ABORT and Disable M_ABORT Int. */
	tx3927_pcicptr->pcistat |= PCI_STATUS_REC_MASTER_ABORT;
	tx3927_pcicptr->pcistatim &= ~PCI_STATUS_REC_MASTER_ABORT;

	return PCIBIOS_SUCCESSFUL;
}

static inline int check_abort(void)
{
	if (tx3927_pcicptr->pcistat & PCI_STATUS_REC_MASTER_ABORT)
		tx3927_pcicptr->pcistat |= PCI_STATUS_REC_MASTER_ABORT;
		tx3927_pcicptr->pcistatim |= PCI_STATUS_REC_MASTER_ABORT;
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int jmr3927_pci_read_config(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 * val)
{
	int ret;

	ret = mkaddr(bus->number, devfn, where);
	if (ret)
		return ret;

	switch (size) {
	case 1:
		*val = *(volatile u8 *) ((unsigned long) & tx3927_pcicptr->icd | (where & 3));
		break;

	case 2:
		*val = le16_to_cpu(*(volatile u16 *) ((unsigned long) & tx3927_pcicptr->icd | (where & 3)));
		break;

	case 4:
		*val = le32_to_cpu(tx3927_pcicptr->icd);
		break;
	}

	return check_abort();
}

static int jmr3927_pci_write_config(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 val)
{
	int ret;

	ret = mkaddr(bus->number, devfn, where);
	if (ret)
		return ret;

	switch (size) {
	case 1:
		*(volatile u8 *) ((unsigned long) & tx3927_pcicptr->icd | (where & 3)) = val;
		break;

	case 2:
		*(volatile u16 *) ((unsigned long) & tx3927_pcicptr->icd | (where & 2)) =
	    cpu_to_le16(val);
		break;

	case 4:
		tx3927_pcicptr->icd = cpu_to_le32(val);
	}

	if (tx3927_pcicptr->pcistat & PCI_STATUS_REC_MASTER_ABORT)
		tx3927_pcicptr->pcistat |= PCI_STATUS_REC_MASTER_ABORT;
		tx3927_pcicptr->pcistatim |= PCI_STATUS_REC_MASTER_ABORT;
		return PCIBIOS_DEVICE_NOT_FOUND;

	return check_abort();
}

struct pci_ops jmr3927_pci_ops = {
	jmr3927_pci_read_config,
	jmr3927_pci_write_config,
};
