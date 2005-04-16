/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Globespan IVR board-specific pci fixups.
 *
 * Copyright 2000 MontaVista Software Inc.
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
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/it8172/it8172.h>
#include <asm/it8172/it8172_pci.h>
#include <asm/it8172/it8172_int.h>

/*
 * Shortcuts
 */
#define INTA	IT8172_PCI_INTA_IRQ
#define INTB	IT8172_PCI_INTB_IRQ
#define INTC	IT8172_PCI_INTC_IRQ
#define INTD	IT8172_PCI_INTD_IRQ

static const int internal_func_irqs[7] __initdata = {
	IT8172_AC97_IRQ,
	IT8172_DMA_IRQ,
	IT8172_CDMA_IRQ,
	IT8172_USB_IRQ,
	IT8172_BRIDGE_MASTER_IRQ,
	IT8172_IDE_IRQ,
	IT8172_MC68K_IRQ
};

static char irq_tab_ivr[][5] __initdata = {
 [0x11] = { INTC, INTC, INTD, INTA, INTB },	/* Realtek RTL-8139	*/
 [0x12] = { INTB, INTB, INTB, INTC, INTC },	/* IVR slot		*/
 [0x13] = { INTA, INTA, INTB, INTC, INTD }	/* Expansion slot	*/
};

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	if (slot == 1)
		return internal_func_irqs[PCI_FUNC(dev->devfn)];

	return irq_tab_ivr[slot][pin];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
