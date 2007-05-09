/* airport.c
 *
 * A driver for "Hermes" chipset based Apple Airport wireless
 * card.
 *
 * Copyright notice & release notes in file orinoco.c
 * 
 * Note specific to airport stub:
 * 
 *  0.05 : first version of the new split driver
 *  0.06 : fix possible hang on powerup, add sleep support
 */

#define DRIVER_NAME "airport"
#define PFX DRIVER_NAME ": "

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/pmac_feature.h>

#include "orinoco.h"

#define AIRPORT_IO_LEN	(0x1000)	/* one page */

struct airport {
	struct macio_dev *mdev;
	void __iomem *vaddr;
	int irq_requested;
	int ndev_registered;
};

static int
airport_suspend(struct macio_dev *mdev, pm_message_t state)
{
	struct net_device *dev = dev_get_drvdata(&mdev->ofdev.dev);
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;
	int err;

	printk(KERN_DEBUG "%s: Airport entering sleep mode\n", dev->name);

	err = orinoco_lock(priv, &flags);
	if (err) {
		printk(KERN_ERR "%s: hw_unavailable on PBOOK_SLEEP_NOW\n",
		       dev->name);
		return 0;
	}

	err = __orinoco_down(dev);
	if (err)
		printk(KERN_WARNING "%s: PBOOK_SLEEP_NOW: Error %d downing interface\n",
		       dev->name, err);

	netif_device_detach(dev);

	priv->hw_unavailable++;

	orinoco_unlock(priv, &flags);

	disable_irq(dev->irq);
	pmac_call_feature(PMAC_FTR_AIRPORT_ENABLE, macio_get_of_node(mdev), 0, 0);

	return 0;
}

static int
airport_resume(struct macio_dev *mdev)
{
	struct net_device *dev = dev_get_drvdata(&mdev->ofdev.dev);
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;
	int err;

	printk(KERN_DEBUG "%s: Airport waking up\n", dev->name);

	pmac_call_feature(PMAC_FTR_AIRPORT_ENABLE, macio_get_of_node(mdev), 0, 1);
	msleep(200);

	enable_irq(dev->irq);

	err = orinoco_reinit_firmware(dev);
	if (err) {
		printk(KERN_ERR "%s: Error %d re-initializing firmware on PBOOK_WAKE\n",
		       dev->name, err);
		return 0;
	}

	spin_lock_irqsave(&priv->lock, flags);

	netif_device_attach(dev);

	priv->hw_unavailable--;

	if (priv->open && (! priv->hw_unavailable)) {
		err = __orinoco_up(dev);
		if (err)
			printk(KERN_ERR "%s: Error %d restarting card on PBOOK_WAKE\n",
			       dev->name, err);
	}


	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int
airport_detach(struct macio_dev *mdev)
{
	struct net_device *dev = dev_get_drvdata(&mdev->ofdev.dev);
	struct orinoco_private *priv = netdev_priv(dev);
	struct airport *card = priv->card;

	if (card->ndev_registered)
		unregister_netdev(dev);
	card->ndev_registered = 0;

	if (card->irq_requested)
		free_irq(dev->irq, dev);
	card->irq_requested = 0;

	if (card->vaddr)
		iounmap(card->vaddr);
	card->vaddr = NULL;

	macio_release_resource(mdev, 0);

	pmac_call_feature(PMAC_FTR_AIRPORT_ENABLE, macio_get_of_node(mdev), 0, 0);
	ssleep(1);

	macio_set_drvdata(mdev, NULL);
	free_orinocodev(dev);

	return 0;
}

static int airport_hard_reset(struct orinoco_private *priv)
{
	/* It would be nice to power cycle the Airport for a real hard
	 * reset, but for some reason although it appears to
	 * re-initialize properly, it falls in a screaming heap
	 * shortly afterwards. */
#if 0
	struct net_device *dev = priv->ndev;
	struct airport *card = priv->card;

	/* Vitally important.  If we don't do this it seems we get an
	 * interrupt somewhere during the power cycle, since
	 * hw_unavailable is already set it doesn't get ACKed, we get
	 * into an interrupt loop and the PMU decides to turn us
	 * off. */
	disable_irq(dev->irq);

	pmac_call_feature(PMAC_FTR_AIRPORT_ENABLE, macio_get_of_node(card->mdev), 0, 0);
	ssleep(1);
	pmac_call_feature(PMAC_FTR_AIRPORT_ENABLE, macio_get_of_node(card->mdev), 0, 1);
	ssleep(1);

	enable_irq(dev->irq);
	ssleep(1);
#endif

	return 0;
}

static int
airport_attach(struct macio_dev *mdev, const struct of_device_id *match)
{
	struct orinoco_private *priv;
	struct net_device *dev;
	struct airport *card;
	unsigned long phys_addr;
	hermes_t *hw;

	if (macio_resource_count(mdev) < 1 || macio_irq_count(mdev) < 1) {
		printk(KERN_ERR PFX "Wrong interrupt/addresses in OF tree\n");
		return -ENODEV;
	}

	/* Allocate space for private device-specific data */
	dev = alloc_orinocodev(sizeof(*card), airport_hard_reset);
	if (! dev) {
		printk(KERN_ERR PFX "Cannot allocate network device\n");
		return -ENODEV;
	}
	priv = netdev_priv(dev);
	card = priv->card;

	hw = &priv->hw;
	card->mdev = mdev;

	if (macio_request_resource(mdev, 0, "airport")) {
		printk(KERN_ERR PFX "can't request IO resource !\n");
		free_orinocodev(dev);
		return -EBUSY;
	}

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &mdev->ofdev.dev);

	macio_set_drvdata(mdev, dev);

	/* Setup interrupts & base address */
	dev->irq = macio_irq(mdev, 0);
	phys_addr = macio_resource_start(mdev, 0);  /* Physical address */
	printk(KERN_DEBUG PFX "Physical address %lx\n", phys_addr);
	dev->base_addr = phys_addr;
	card->vaddr = ioremap(phys_addr, AIRPORT_IO_LEN);
	if (!card->vaddr) {
		printk(KERN_ERR PFX "ioremap() failed\n");
		goto failed;
	}

	hermes_struct_init(hw, card->vaddr, HERMES_16BIT_REGSPACING);
		
	/* Power up card */
	pmac_call_feature(PMAC_FTR_AIRPORT_ENABLE, macio_get_of_node(mdev), 0, 1);
	ssleep(1);

	/* Reset it before we get the interrupt */
	hermes_init(hw);

	if (request_irq(dev->irq, orinoco_interrupt, 0, dev->name, dev)) {
		printk(KERN_ERR PFX "Couldn't get IRQ %d\n", dev->irq);
		goto failed;
	}
	card->irq_requested = 1;

	/* Tell the stack we exist */
	if (register_netdev(dev) != 0) {
		printk(KERN_ERR PFX "register_netdev() failed\n");
		goto failed;
	}
	printk(KERN_DEBUG PFX "Card registered for interface %s\n", dev->name);
	card->ndev_registered = 1;
	return 0;
 failed:
	airport_detach(mdev);
	return -ENODEV;
}				/* airport_attach */


static char version[] __initdata = DRIVER_NAME " " DRIVER_VERSION
	" (Benjamin Herrenschmidt <benh@kernel.crashing.org>)";
MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("Driver for the Apple Airport wireless card.");
MODULE_LICENSE("Dual MPL/GPL");

static struct of_device_id airport_match[] = 
{
	{
	.name 		= "radio",
	},
	{},
};

MODULE_DEVICE_TABLE (of, airport_match);

static struct macio_driver airport_driver = 
{
	.name 		= DRIVER_NAME,
	.match_table	= airport_match,
	.probe		= airport_attach,
	.remove		= airport_detach,
	.suspend	= airport_suspend,
	.resume		= airport_resume,
};

static int __init
init_airport(void)
{
	printk(KERN_DEBUG "%s\n", version);

	return macio_register_driver(&airport_driver);
}

static void __exit
exit_airport(void)
{
	return macio_unregister_driver(&airport_driver);
}

module_init(init_airport);
module_exit(exit_airport);
