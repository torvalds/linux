/*
 * BRIEF MODULE DESCRIPTION
 *	Board specific pci fixups.
 *
 * Copyright 2001-2003 MontaVista Software Inc.
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
#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mach-au1x00/au1000.h>

/*
 * Shortcut
 */
#ifdef CONFIG_SOC_AU1500
#define INTA AU1000_PCI_INTA
#define INTB AU1000_PCI_INTB
#define INTC AU1000_PCI_INTC
#define INTD AU1000_PCI_INTD
#endif

#ifdef CONFIG_SOC_AU1550
#define INTA AU1550_PCI_INTA
#define INTB AU1550_PCI_INTB
#define INTC AU1550_PCI_INTC
#define INTD AU1550_PCI_INTD
#endif

#define INTX    0xFF /* not valid */

#ifdef CONFIG_MIPS_DB1500
static char irq_tab_alchemy[][5] __initdata = {
 [12] =	{ -1, INTA, INTX, INTX, INTX},   /* IDSEL 12 - HPT371   */
 [13] =	{ -1, INTA, INTB, INTC, INTD},   /* IDSEL 13 - PCI slot */
};
#endif

#ifdef CONFIG_MIPS_BOSPORUS
static char irq_tab_alchemy[][5] __initdata = {
 [11] =	{ -1, INTA, INTB, INTX, INTX},   /* IDSEL 11 - miniPCI  */
 [12] =	{ -1, INTA, INTX, INTX, INTX},   /* IDSEL 12 - SN1741   */
 [13] =	{ -1, INTA, INTB, INTC, INTD},   /* IDSEL 13 - PCI slot */
};
#endif

#ifdef CONFIG_MIPS_MIRAGE
static char irq_tab_alchemy[][5] __initdata = {
 [11] =	{ -1, INTD, INTX, INTX, INTX},   /* IDSEL 11 - SMI VGX */
 [12] =	{ -1, INTX, INTX, INTC, INTX},   /* IDSEL 12 - PNX1300 */
 [13] =	{ -1, INTA, INTB, INTX, INTX},   /* IDSEL 13 - miniPCI */
};
#endif

#ifdef CONFIG_MIPS_DB1550
static char irq_tab_alchemy[][5] __initdata = {
 [11] =	{ -1, INTC, INTX, INTX, INTX},   /* IDSEL 11 - on-board HPT371    */
 [12] =	{ -1, INTB, INTC, INTD, INTA},   /* IDSEL 12 - PCI slot 2 (left)  */
 [13] =	{ -1, INTA, INTB, INTC, INTD},   /* IDSEL 13 - PCI slot 1 (right) */
};
#endif

#ifdef CONFIG_MIPS_PB1500
static char irq_tab_alchemy[][5] __initdata = {
 [12] = { -1, INTA, INTX, INTX, INTX},   /* IDSEL 12 - HPT370   */
 [13] = { -1, INTA, INTB, INTC, INTD},   /* IDSEL 13 - PCI slot */
};
#endif

#ifdef CONFIG_MIPS_PB1550
static char irq_tab_alchemy[][5] __initdata = {
 [12] =	{ -1, INTB, INTC, INTD, INTA},   /* IDSEL 12 - PCI slot 2 (left)  */
 [13] =	{ -1, INTA, INTB, INTC, INTD},   /* IDSEL 13 - PCI slot 1 (right) */
};
#endif

#ifdef CONFIG_MIPS_MTX1
static char irq_tab_alchemy[][5] __initdata = {
 [0] = { -1, INTA, INTB, INTX, INTX},   /* IDSEL 00 - AdapterA-Slot0 (top)    */
 [1] = { -1, INTB, INTA, INTX, INTX},   /* IDSEL 01 - AdapterA-Slot1 (bottom) */
 [2] = { -1, INTC, INTD, INTX, INTX},   /* IDSEL 02 - AdapterB-Slot0 (top)    */
 [3] = { -1, INTD, INTC, INTX, INTX},   /* IDSEL 03 - AdapterB-Slot1 (bottom) */
 [4] = { -1, INTA, INTB, INTX, INTX},   /* IDSEL 04 - AdapterC-Slot0 (top)    */
 [5] = { -1, INTB, INTA, INTX, INTX},   /* IDSEL 05 - AdapterC-Slot1 (bottom) */
 [6] = { -1, INTC, INTD, INTX, INTX},   /* IDSEL 06 - AdapterD-Slot0 (top)    */
 [7] = { -1, INTD, INTC, INTX, INTX},   /* IDSEL 07 - AdapterD-Slot1 (bottom) */
};
#endif

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return irq_tab_alchemy[slot][pin];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
