/*
 *  skisa.c: A network driver for SK-NET TMS380-based ISA token ring cards.
 *
 *  Based on tmspci written 1999 by Adam Fritzler
 *  
 *  Written 2000 by Jochen Friedrich
 *  Dedicated to my girlfriend Steffi Bopp
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU General Public License, incorporated herein by reference.
 *
 *  This driver module supports the following cards:
 *	- SysKonnect TR4/16(+) ISA	(SK-4190)
 *
 *  Maintainer(s):
 *    AF        Adam Fritzler           mid@auk.cx
 *    JF	Jochen Friedrich	jochen@scram.de
 *
 *  Modification History:
 *	14-Jan-01	JF	Created
 *	28-Oct-02	JF	Fixed probe of card for static compilation.
 *				Fixed module init to not make hotplug go wild.
 *	09-Nov-02	JF	Fixed early bail out on out of memory
 *				situations if multiple cards are found.
 *				Cleaned up some unnecessary console SPAM.
 *	09-Dec-02	JF	Fixed module reference counting.
 *	02-Jan-03	JF	Renamed to skisa.c
 *
 */
static const char version[] = "skisa.c: v1.03 09/12/2002 by Jochen Friedrich\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/trdevice.h>
#include <linux/platform_device.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pci.h>
#include <asm/dma.h>

#include "tms380tr.h"

#define SK_ISA_IO_EXTENT 32

/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int portlist[] __initdata = {
	0x0A20, 0x1A20, 0x0B20, 0x1B20, 0x0980, 0x1980, 0x0900, 0x1900,// SK
	0
};

/* A zero-terminated list of IRQs to be probed. 
 * Used again after initial probe for sktr_chipset_init, called from sktr_open.
 */
static const unsigned short irqlist[] = {
	3, 5, 9, 10, 11, 12, 15,
	0
};

/* A zero-terminated list of DMAs to be probed. */
static int dmalist[] __initdata = {
	5, 6, 7,
	0
};

static char isa_cardname[] = "SK NET TR 4/16 ISA\0";
static u64 dma_mask = ISA_MAX_ADDRESS;
static int sk_isa_open(struct net_device *dev);
static void sk_isa_read_eeprom(struct net_device *dev);
static unsigned short sk_isa_setnselout_pins(struct net_device *dev);

static unsigned short sk_isa_sifreadb(struct net_device *dev, unsigned short reg)
{
	return inb(dev->base_addr + reg);
}

static unsigned short sk_isa_sifreadw(struct net_device *dev, unsigned short reg)
{
	return inw(dev->base_addr + reg);
}

static void sk_isa_sifwriteb(struct net_device *dev, unsigned short val, unsigned short reg)
{
	outb(val, dev->base_addr + reg);
}

static void sk_isa_sifwritew(struct net_device *dev, unsigned short val, unsigned short reg)
{
	outw(val, dev->base_addr + reg);
}


static int __init sk_isa_probe1(struct net_device *dev, int ioaddr)
{
	unsigned char old, chk1, chk2;

	if (!request_region(ioaddr, SK_ISA_IO_EXTENT, isa_cardname))
		return -ENODEV;

	old = inb(ioaddr + SIFADR);	/* Get the old SIFADR value */

	chk1 = 0;	/* Begin with check value 0 */
	do {
		/* Write new SIFADR value */
		outb(chk1, ioaddr + SIFADR);

		/* Read, invert and write */
		chk2 = inb(ioaddr + SIFADD);
		chk2 ^= 0x0FE;
		outb(chk2, ioaddr + SIFADR);

		/* Read, invert and compare */
		chk2 = inb(ioaddr + SIFADD);
		chk2 ^= 0x0FE;

		if(chk1 != chk2) {
			release_region(ioaddr, SK_ISA_IO_EXTENT);
			return -ENODEV;
		}

		chk1 -= 2;
	} while(chk1 != 0);	/* Repeat 128 times (all byte values) */

    	/* Restore the SIFADR value */
	outb(old, ioaddr + SIFADR);

	dev->base_addr = ioaddr;
	return 0;
}

static int __init setup_card(struct net_device *dev, struct device *pdev)
{
	struct net_local *tp;
        static int versionprinted;
	const unsigned *port;
	int j, err = 0;
	DECLARE_MAC_BUF(mac);

	if (!dev)
		return -ENOMEM;

	if (dev->base_addr)	/* probe specific location */
		err = sk_isa_probe1(dev, dev->base_addr);
	else {
		for (port = portlist; *port; port++) {
			err = sk_isa_probe1(dev, *port);
			if (!err)
				break;
		}
	}
	if (err)
		goto out5;

	/* At this point we have found a valid card. */

	if (versionprinted++ == 0)
		printk(KERN_DEBUG "%s", version);

	err = -EIO;
	pdev->dma_mask = &dma_mask;
	if (tmsdev_init(dev, pdev))
		goto out4;

	dev->base_addr &= ~3; 
		
	sk_isa_read_eeprom(dev);

	printk(KERN_DEBUG "skisa.c:    Ring Station Address: %s\n",
	       print_mac(mac, dev->dev_addr));
		
	tp = netdev_priv(dev);
	tp->setnselout = sk_isa_setnselout_pins;
		
	tp->sifreadb = sk_isa_sifreadb;
	tp->sifreadw = sk_isa_sifreadw;
	tp->sifwriteb = sk_isa_sifwriteb;
	tp->sifwritew = sk_isa_sifwritew;
	
	memcpy(tp->ProductID, isa_cardname, PROD_ID_SIZE + 1);

	tp->tmspriv = NULL;

	dev->open = sk_isa_open;
	dev->stop = tms380tr_close;

	if (dev->irq == 0)
	{
		for(j = 0; irqlist[j] != 0; j++)
		{
			dev->irq = irqlist[j];
			if (!request_irq(dev->irq, tms380tr_interrupt, 0, 
				isa_cardname, dev))
				break;
                }
		
                if(irqlist[j] == 0)
                {
                        printk(KERN_INFO "skisa.c: AutoSelect no IRQ available\n");
			goto out3;
		}
	}
	else
	{
		for(j = 0; irqlist[j] != 0; j++)
			if (irqlist[j] == dev->irq)
				break;
		if (irqlist[j] == 0)
		{
			printk(KERN_INFO "skisa.c: Illegal IRQ %d specified\n",
				dev->irq);
			goto out3;
		}
		if (request_irq(dev->irq, tms380tr_interrupt, 0, 
			isa_cardname, dev))
		{
                        printk(KERN_INFO "skisa.c: Selected IRQ %d not available\n",
				dev->irq);
			goto out3;
		}
	}

	if (dev->dma == 0)
	{
		for(j = 0; dmalist[j] != 0; j++)
		{
			dev->dma = dmalist[j];
                        if (!request_dma(dev->dma, isa_cardname))
				break;
		}

		if(dmalist[j] == 0)
		{
			printk(KERN_INFO "skisa.c: AutoSelect no DMA available\n");
			goto out2;
		}
	}
	else
	{
		for(j = 0; dmalist[j] != 0; j++)
			if (dmalist[j] == dev->dma)
				break;
		if (dmalist[j] == 0)
		{
                        printk(KERN_INFO "skisa.c: Illegal DMA %d specified\n",
				dev->dma);
			goto out2;
		}
		if (request_dma(dev->dma, isa_cardname))
		{
                        printk(KERN_INFO "skisa.c: Selected DMA %d not available\n",
				dev->dma);
			goto out2;
		}
	}

	err = register_netdev(dev);
	if (err)
		goto out;

	printk(KERN_DEBUG "%s:    IO: %#4lx  IRQ: %d  DMA: %d\n",
	       dev->name, dev->base_addr, dev->irq, dev->dma);

	return 0;
out:
	free_dma(dev->dma);
out2:
	free_irq(dev->irq, dev);
out3:
	tmsdev_term(dev);
out4:
	release_region(dev->base_addr, SK_ISA_IO_EXTENT);
out5:
	return err;
}

/*
 * Reads MAC address from adapter RAM, which should've read it from
 * the onboard ROM.  
 *
 * Calling this on a board that does not support it can be a very
 * dangerous thing.  The Madge board, for instance, will lock your
 * machine hard when this is called.  Luckily, its supported in a
 * separate driver.  --ASF
 */
static void sk_isa_read_eeprom(struct net_device *dev)
{
	int i;
	
	/* Address: 0000:0000 */
	sk_isa_sifwritew(dev, 0, SIFADX);
	sk_isa_sifwritew(dev, 0, SIFADR);	
	
	/* Read six byte MAC address data */
	dev->addr_len = 6;
	for(i = 0; i < 6; i++)
		dev->dev_addr[i] = sk_isa_sifreadw(dev, SIFINC) >> 8;
}

unsigned short sk_isa_setnselout_pins(struct net_device *dev)
{
	return 0;
}

static int sk_isa_open(struct net_device *dev)
{  
	struct net_local *tp = netdev_priv(dev);
	unsigned short val = 0;
	unsigned short oldval;
	int i;

	val = 0;
	for(i = 0; irqlist[i] != 0; i++)
	{
		if(irqlist[i] == dev->irq)
			break;
	}

	val |= CYCLE_TIME << 2;
	val |= i << 4;
	i = dev->dma - 5;
	val |= i;
	if(tp->DataRate == SPEED_4)
		val |= LINE_SPEED_BIT;
	else
		val &= ~LINE_SPEED_BIT;
	oldval = sk_isa_sifreadb(dev, POSREG);
	/* Leave cycle bits alone */
	oldval |= 0xf3;
	val &= oldval;
	sk_isa_sifwriteb(dev, val, POSREG);

	return tms380tr_open(dev);
}

#define ISATR_MAX_ADAPTERS 3

static int io[ISATR_MAX_ADAPTERS];
static int irq[ISATR_MAX_ADAPTERS];
static int dma[ISATR_MAX_ADAPTERS];

MODULE_LICENSE("GPL");

module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param_array(dma, int, NULL, 0);

static struct platform_device *sk_isa_dev[ISATR_MAX_ADAPTERS];

static struct platform_driver sk_isa_driver = {
	.driver		= {
		.name	= "skisa",
	},
};

static int __init sk_isa_init(void)
{
	struct net_device *dev;
	struct platform_device *pdev;
	int i, num = 0, err = 0;

	err = platform_driver_register(&sk_isa_driver);
	if (err)
		return err;

	for (i = 0; i < ISATR_MAX_ADAPTERS ; i++) {
		dev = alloc_trdev(sizeof(struct net_local));
		if (!dev)
			continue;

		dev->base_addr = io[i];
		dev->irq = irq[i];
		dev->dma = dma[i];
		pdev = platform_device_register_simple("skisa",
			i, NULL, 0);
		if (IS_ERR(pdev)) {
			free_netdev(dev);
			continue;
		}
		err = setup_card(dev, &pdev->dev);
		if (!err) {
			sk_isa_dev[i] = pdev;
			platform_set_drvdata(sk_isa_dev[i], dev);
			++num;
		} else {
			platform_device_unregister(pdev);
			free_netdev(dev);
		}
	}

	printk(KERN_NOTICE "skisa.c: %d cards found.\n", num);
	/* Probe for cards. */
	if (num == 0) {
		printk(KERN_NOTICE "skisa.c: No cards found.\n");
		platform_driver_unregister(&sk_isa_driver);
		return -ENODEV;
	}
	return 0;
}

static void __exit sk_isa_cleanup(void)
{
	struct net_device *dev;
	int i;

	for (i = 0; i < ISATR_MAX_ADAPTERS ; i++) {
		struct platform_device *pdev = sk_isa_dev[i];

		if (!pdev)
			continue;
		dev = platform_get_drvdata(pdev);
		unregister_netdev(dev);
		release_region(dev->base_addr, SK_ISA_IO_EXTENT);
		free_irq(dev->irq, dev);
		free_dma(dev->dma);
		tmsdev_term(dev);
		free_netdev(dev);
		platform_set_drvdata(pdev, NULL);
		platform_device_unregister(pdev);
	}
	platform_driver_unregister(&sk_isa_driver);
}

module_init(sk_isa_init);
module_exit(sk_isa_cleanup);
