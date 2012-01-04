/*
 *  Copyright (C) 2008 Aurelien Jarno <aurelien@aurel32.net>
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
#include <linux/ssb/ssb.h>
#include <bcm47xx.h>

int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return 0;
}

int pcibios_plat_dev_init(struct pci_dev *dev)
{
#ifdef CONFIG_BCM47XX_SSB
	int res;
	u8 slot, pin;

	if (bcm47xx_bus_type !=  BCM47XX_BUS_TYPE_SSB)
		return 0;

	res = ssb_pcibios_plat_dev_init(dev);
	if (res < 0) {
		printk(KERN_ALERT "PCI: Failed to init device %s\n",
		       pci_name(dev));
		return res;
	}

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
	slot = PCI_SLOT(dev->devfn);
	res = ssb_pcibios_map_irq(dev, slot, pin);

	/* IRQ-0 and IRQ-1 are software interrupts. */
	if (res < 2) {
		printk(KERN_ALERT "PCI: Failed to map IRQ of device %s\n",
		       pci_name(dev));
		return res;
	}

	dev->irq = res;
#endif
	return 0;
}
