/* orinoco_plx.c
 *
 * Driver for Prism II devices which would usually be driven by orinoco_cs,
 * but are connected to the PCI bus by a PLX9052.
 *
 * Current maintainers are:
 *	Pavel Roskin <proski AT gnu.org>
 * and	David Gibson <hermes AT gibson.dropbear.id.au>
 *
 * (C) Copyright David Gibson, IBM Corp. 2001-2003.
 * Copyright (C) 2001 Daniel Barlow
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 *
 * Here's the general details on how the PLX9052 adapter works:
 *
 * - Two PCI I/O address spaces, one 0x80 long which contains the
 * PLX9052 registers, and one that's 0x40 long mapped to the PCMCIA
 * slot I/O address space.
 *
 * - One PCI memory address space, mapped to the PCMCIA attribute space
 * (containing the CIS).
 *
 * Using the later, you can read through the CIS data to make sure the
 * card is compatible with the driver. Keep in mind that the PCMCIA
 * spec specifies the CIS as the lower 8 bits of each word read from
 * the CIS, so to read the bytes of the CIS, read every other byte
 * (0,2,4,...). Passing that test, you need to enable the I/O address
 * space on the PCMCIA card via the PCMCIA COR register. This is the
 * first byte following the CIS. In my case (which may not have any
 * relation to what's on the PRISM2 cards), COR was at offset 0x800
 * within the PCI memory space. Write 0x41 to the COR register to
 * enable I/O mode and to select level triggered interrupts. To
 * confirm you actually succeeded, read the COR register back and make
 * sure it actually got set to 0x41, in case you have an unexpected
 * card inserted.
 *
 * Following that, you can treat the second PCI I/O address space (the
 * one that's not 0x80 in length) as the PCMCIA I/O space.
 *
 * Note that in the Eumitcom's source for their drivers, they register
 * the interrupt as edge triggered when registering it with the
 * Windows kernel. I don't recall how to register edge triggered on
 * Linux (if it can be done at all). But in some experimentation, I
 * don't see much operational difference between using either
 * interrupt mode. Don't mess with the interrupt mode in the COR
 * register though, as the PLX9052 wants level triggers with the way
 * the serial EEPROM configures it on the WL11000.
 *
 * There's some other little quirks related to timing that I bumped
 * into, but I don't recall right now. Also, there's two variants of
 * the WL11000 I've seen, revision A1 and T2. These seem to differ
 * slightly in the timings configured in the wait-state generator in
 * the PLX9052. There have also been some comments from Eumitcom that
 * cards shouldn't be hot swapped, apparently due to risk of cooking
 * the PLX9052. I'm unsure why they believe this, as I can't see
 * anything in the design that would really cause a problem, except
 * for crashing drivers not written to expect it. And having developed
 * drivers for the WL11000, I'd say it's quite tricky to write code
 * that will successfully deal with a hot unplug. Very odd things
 * happen on the I/O side of things. But anyway, be warned. Despite
 * that, I've hot-swapped a number of times during debugging and
 * driver development for various reasons (stuck WAIT# line after the
 * radio card's firmware locks up).
 */

#define DRIVER_NAME "orinoco_plx"
#define PFX DRIVER_NAME ": "

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <pcmcia/cisreg.h>

#include "orinoco.h"
#include "orinoco_pci.h"

#define COR_OFFSET	(0x3e0)	/* COR attribute offset of Prism2 PC card */
#define COR_VALUE	(COR_LEVEL_REQ | COR_FUNC_ENA) /* Enable PC card with interrupt in level trigger */
#define COR_RESET     (0x80)	/* reset bit in the COR register */
#define PLX_RESET_TIME	(500)	/* milliseconds */

#define PLX_INTCSR		0x4c /* Interrupt Control & Status Register */
#define PLX_INTCSR_INTEN	(1 << 6) /* Interrupt Enable bit */

/*
 * Do a soft reset of the card using the Configuration Option Register
 */
static int orinoco_plx_cor_reset(struct orinoco_private *priv)
{
	struct hermes *hw = &priv->hw;
	struct orinoco_pci_card *card = priv->card;
	unsigned long timeout;
	u16 reg;

	iowrite8(COR_VALUE | COR_RESET, card->attr_io + COR_OFFSET);
	mdelay(1);

	iowrite8(COR_VALUE, card->attr_io + COR_OFFSET);
	mdelay(1);

	/* Just in case, wait more until the card is no longer busy */
	timeout = jiffies + (PLX_RESET_TIME * HZ / 1000);
	reg = hermes_read_regn(hw, CMD);
	while (time_before(jiffies, timeout) && (reg & HERMES_CMD_BUSY)) {
		mdelay(1);
		reg = hermes_read_regn(hw, CMD);
	}

	/* Still busy? */
	if (reg & HERMES_CMD_BUSY) {
		printk(KERN_ERR PFX "Busy timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int orinoco_plx_hw_init(struct orinoco_pci_card *card)
{
	int i;
	u32 csr_reg;
	static const u8 cis_magic[] = {
		0x01, 0x03, 0x00, 0x00, 0xff, 0x17, 0x04, 0x67
	};

	printk(KERN_DEBUG PFX "CIS: ");
	for (i = 0; i < 16; i++)
		printk("%02X:", ioread8(card->attr_io + (i << 1)));
	printk("\n");

	/* Verify whether a supported PC card is present */
	/* FIXME: we probably need to be smarted about this */
	for (i = 0; i < sizeof(cis_magic); i++) {
		if (cis_magic[i] != ioread8(card->attr_io + (i << 1))) {
			printk(KERN_ERR PFX "The CIS value of Prism2 PC "
			       "card is unexpected\n");
			return -ENODEV;
		}
	}

	/* bjoern: We need to tell the card to enable interrupts, in
	   case the serial eprom didn't do this already.  See the
	   PLX9052 data book, p8-1 and 8-24 for reference. */
	csr_reg = ioread32(card->bridge_io + PLX_INTCSR);
	if (!(csr_reg & PLX_INTCSR_INTEN)) {
		csr_reg |= PLX_INTCSR_INTEN;
		iowrite32(csr_reg, card->bridge_io + PLX_INTCSR);
		csr_reg = ioread32(card->bridge_io + PLX_INTCSR);
		if (!(csr_reg & PLX_INTCSR_INTEN)) {
			printk(KERN_ERR PFX "Cannot enable interrupts\n");
			return -EIO;
		}
	}

	return 0;
}

static int orinoco_plx_init_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	int err;
	struct orinoco_private *priv;
	struct orinoco_pci_card *card;
	void __iomem *hermes_io, *attr_io, *bridge_io;

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR PFX "Cannot enable PCI device\n");
		return err;
	}

	err = pci_request_regions(pdev, DRIVER_NAME);
	if (err) {
		printk(KERN_ERR PFX "Cannot obtain PCI resources\n");
		goto fail_resources;
	}

	bridge_io = pci_iomap(pdev, 1, 0);
	if (!bridge_io) {
		printk(KERN_ERR PFX "Cannot map bridge registers\n");
		err = -EIO;
		goto fail_map_bridge;
	}

	attr_io = pci_iomap(pdev, 2, 0);
	if (!attr_io) {
		printk(KERN_ERR PFX "Cannot map PCMCIA attributes\n");
		err = -EIO;
		goto fail_map_attr;
	}

	hermes_io = pci_iomap(pdev, 3, 0);
	if (!hermes_io) {
		printk(KERN_ERR PFX "Cannot map chipset registers\n");
		err = -EIO;
		goto fail_map_hermes;
	}

	/* Allocate network device */
	priv = alloc_orinocodev(sizeof(*card), &pdev->dev,
				orinoco_plx_cor_reset, NULL);
	if (!priv) {
		printk(KERN_ERR PFX "Cannot allocate network device\n");
		err = -ENOMEM;
		goto fail_alloc;
	}

	card = priv->card;
	card->bridge_io = bridge_io;
	card->attr_io = attr_io;

	hermes_struct_init(&priv->hw, hermes_io, HERMES_16BIT_REGSPACING);

	err = request_irq(pdev->irq, orinoco_interrupt, IRQF_SHARED,
			  DRIVER_NAME, priv);
	if (err) {
		printk(KERN_ERR PFX "Cannot allocate IRQ %d\n", pdev->irq);
		err = -EBUSY;
		goto fail_irq;
	}

	err = orinoco_plx_hw_init(card);
	if (err) {
		printk(KERN_ERR PFX "Hardware initialization failed\n");
		goto fail;
	}

	err = orinoco_plx_cor_reset(priv);
	if (err) {
		printk(KERN_ERR PFX "Initial reset failed\n");
		goto fail;
	}

	err = orinoco_init(priv);
	if (err) {
		printk(KERN_ERR PFX "orinoco_init() failed\n");
		goto fail;
	}

	err = orinoco_if_add(priv, 0, 0, NULL);
	if (err) {
		printk(KERN_ERR PFX "orinoco_if_add() failed\n");
		goto fail;
	}

	pci_set_drvdata(pdev, priv);

	return 0;

 fail:
	free_irq(pdev->irq, priv);

 fail_irq:
	pci_set_drvdata(pdev, NULL);
	free_orinocodev(priv);

 fail_alloc:
	pci_iounmap(pdev, hermes_io);

 fail_map_hermes:
	pci_iounmap(pdev, attr_io);

 fail_map_attr:
	pci_iounmap(pdev, bridge_io);

 fail_map_bridge:
	pci_release_regions(pdev);

 fail_resources:
	pci_disable_device(pdev);

	return err;
}

static void __devexit orinoco_plx_remove_one(struct pci_dev *pdev)
{
	struct orinoco_private *priv = pci_get_drvdata(pdev);
	struct orinoco_pci_card *card = priv->card;

	orinoco_if_del(priv);
	free_irq(pdev->irq, priv);
	pci_set_drvdata(pdev, NULL);
	free_orinocodev(priv);
	pci_iounmap(pdev, priv->hw.iobase);
	pci_iounmap(pdev, card->attr_io);
	pci_iounmap(pdev, card->bridge_io);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static DEFINE_PCI_DEVICE_TABLE(orinoco_plx_id_table) = {
	{0x111a, 0x1023, PCI_ANY_ID, PCI_ANY_ID,},	/* Siemens SpeedStream SS1023 */
	{0x1385, 0x4100, PCI_ANY_ID, PCI_ANY_ID,},	/* Netgear MA301 */
	{0x15e8, 0x0130, PCI_ANY_ID, PCI_ANY_ID,},	/* Correga  - does this work? */
	{0x1638, 0x1100, PCI_ANY_ID, PCI_ANY_ID,},	/* SMC EZConnect SMC2602W,
							   Eumitcom PCI WL11000,
							   Addtron AWA-100 */
	{0x16ab, 0x1100, PCI_ANY_ID, PCI_ANY_ID,},	/* Global Sun Tech GL24110P */
	{0x16ab, 0x1101, PCI_ANY_ID, PCI_ANY_ID,},	/* Reported working, but unknown */
	{0x16ab, 0x1102, PCI_ANY_ID, PCI_ANY_ID,},	/* Linksys WDT11 */
	{0x16ec, 0x3685, PCI_ANY_ID, PCI_ANY_ID,},	/* USR 2415 */
	{0xec80, 0xec00, PCI_ANY_ID, PCI_ANY_ID,},	/* Belkin F5D6000 tested by
							   Brendan W. McAdams <rit AT jacked-in.org> */
	{0x10b7, 0x7770, PCI_ANY_ID, PCI_ANY_ID,},	/* 3Com AirConnect PCI tested by
							   Damien Persohn <damien AT persohn.net> */
	{0,},
};

MODULE_DEVICE_TABLE(pci, orinoco_plx_id_table);

static struct pci_driver orinoco_plx_driver = {
	.name		= DRIVER_NAME,
	.id_table	= orinoco_plx_id_table,
	.probe		= orinoco_plx_init_one,
	.remove		= __devexit_p(orinoco_plx_remove_one),
	.suspend	= orinoco_pci_suspend,
	.resume		= orinoco_pci_resume,
};

static char version[] __initdata = DRIVER_NAME " " DRIVER_VERSION
	" (Pavel Roskin <proski@gnu.org>,"
	" David Gibson <hermes@gibson.dropbear.id.au>,"
	" Daniel Barlow <dan@telent.net>)";
MODULE_AUTHOR("Daniel Barlow <dan@telent.net>");
MODULE_DESCRIPTION("Driver for wireless LAN cards using the PLX9052 PCI bridge");
MODULE_LICENSE("Dual MPL/GPL");

static int __init orinoco_plx_init(void)
{
	printk(KERN_DEBUG "%s\n", version);
	return pci_register_driver(&orinoco_plx_driver);
}

static void __exit orinoco_plx_exit(void)
{
	pci_unregister_driver(&orinoco_plx_driver);
}

module_init(orinoco_plx_init);
module_exit(orinoco_plx_exit);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
