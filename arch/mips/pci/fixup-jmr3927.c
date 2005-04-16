/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Board specific pci fixups.
 *
 * Copyright 2001 MontaVista Software Inc.
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

#include <asm/jmr3927/jmr3927.h>

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	unsigned char irq = pin;

	/* IRQ rotation (PICMG) */
	irq--;			/* 0-3 */
	if (dev->bus->parent == NULL &&
	    slot == TX3927_PCIC_IDSEL_AD_TO_SLOT(23)) {
		/* PCI CardSlot (IDSEL=A23, DevNu=12) */
		/* PCIA => PCIC (IDSEL=A23) */
		/* NOTE: JMR3927 JP1 must be set to OPEN */
		irq = (irq + 2) % 4;
	} else if (dev->bus->parent == NULL &&
		   slot == TX3927_PCIC_IDSEL_AD_TO_SLOT(22)) {
		/* PCI CardSlot (IDSEL=A22, DevNu=11) */
		/* PCIA => PCIA (IDSEL=A22) */
		/* NOTE: JMR3927 JP1 must be set to OPEN */
		irq = (irq + 0) % 4;
	} else {
		/* PCI Backplane */
		irq = (irq + 3 + slot) % 4;
	}
	irq++;			/* 1-4 */

	switch (irq) {
	case 1:
		irq = JMR3927_IRQ_IOC_PCIA;
		break;
	case 2:
		// wrong for backplane irq = JMR3927_IRQ_IOC_PCIB;
		irq = JMR3927_IRQ_IOC_PCID;
		break;
	case 3:
		irq = JMR3927_IRQ_IOC_PCIC;
		break;
	case 4:
		// wrong for backplane irq = JMR3927_IRQ_IOC_PCID;
		irq = JMR3927_IRQ_IOC_PCIB;
		break;
	}

	/* Check OnBoard Ethernet (IDSEL=A24, DevNu=13) */
	if (dev->bus->parent == NULL &&
	    slot == TX3927_PCIC_IDSEL_AD_TO_SLOT(24)) {
		extern int jmr3927_ether1_irq;
		/* check this irq line was reserved for ether1 */
		if (jmr3927_ether1_irq != JMR3927_IRQ_ETHER0)
			irq = JMR3927_IRQ_ETHER0;
		else
			irq = 0;	/* disable */
	}
	return irq;
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	/* SMSC SLC90E66 IDE uses irq 14, 15 (default) */
	if (!(dev->vendor == PCI_VENDOR_ID_EFAR &&
	      dev->device == PCI_DEVICE_ID_EFAR_SLC90E66_1))
		return pci_get_irq(dev, pin);

	dev->irq = irq;
}
