/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Board specific pci fixups.
 *
 * Copyright 2001, 2002, 2003 MontaVista Software Inc.
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

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

static void ddb5477_fixup(struct pci_dev *dev)
{
	u8 old;

	printk(KERN_NOTICE "Enabling ALI M1533/35 PS2 keyboard/mouse.\n");
	pci_read_config_byte(dev, 0x41, &old);
	pci_write_config_byte(dev, 0x41, old | 0xd0);
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533,
	  ddb5477_fixup);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1535,
	  ddb5477_fixup);

/*
 * Fixup baseboard AMD chip so that tx does not underflow.
 *      bcr_18 |= 0x0800
 * This sets NOUFLO bit which makes tx not start until whole pkt
 * is fetched to the chip.
 */
#define PCNET32_WIO_RDP		0x10
#define PCNET32_WIO_RAP		0x12
#define PCNET32_WIO_RESET	0x14
#define PCNET32_WIO_BDP		0x16

static void ddb5477_amd_lance_fixup(struct pci_dev *dev)
{
	unsigned long ioaddr;
	u16 temp;

	ioaddr = pci_resource_start(dev, 0);

	inw(ioaddr + PCNET32_WIO_RESET);	/* reset chip */
                                                                                
	/* bcr_18 |= 0x0800 */
	outw(18, ioaddr + PCNET32_WIO_RAP);
	temp = inw(ioaddr + PCNET32_WIO_BDP);
	temp |= 0x0800;
	outw(18, ioaddr + PCNET32_WIO_RAP);
	outw(temp, ioaddr + PCNET32_WIO_BDP);
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_LANCE,
	  ddb5477_amd_lance_fixup);
