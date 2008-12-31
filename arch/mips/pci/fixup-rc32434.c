/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	stevel@mvista.com or source@mvista.com
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
#include <asm/mach-rc32434/irq.h>

static int __devinitdata irq_map[2][12] = {
	{0, 0, 2, 3, 2, 3, 0, 0, 0, 0, 0, 1},
	{0, 0, 1, 3, 0, 2, 1, 3, 0, 2, 1, 3}
};

int __devinit pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq = 0;

	if (dev->bus->number < 2 && PCI_SLOT(dev->devfn) < 12)
		irq = irq_map[dev->bus->number][PCI_SLOT(dev->devfn)];

	return irq + GROUP4_IRQ_BASE + 4;
}

static void rc32434_pci_early_fixup(struct pci_dev *dev)
{
	if (PCI_SLOT(dev->devfn) == 6 && dev->bus->number == 0) {
		/* disable prefetched memory range */
		pci_write_config_word(dev, PCI_PREF_MEMORY_LIMIT, 0);
		pci_write_config_word(dev, PCI_PREF_MEMORY_BASE, 0x10);

		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 4);
	}
}

/*
 * The fixup applies to both the IDT and VIA devices present on the board
 */
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, rc32434_pci_early_fixup);

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
