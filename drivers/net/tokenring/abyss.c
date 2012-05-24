/*
 *  abyss.c: Network driver for the Madge Smart 16/4 PCI Mk2 token ring card.
 *
 *  Written 1999-2000 by Adam Fritzler
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU General Public License, incorporated herein by reference.
 *
 *  This driver module supports the following cards:
 *      - Madge Smart 16/4 PCI Mk2
 *
 *  Maintainer(s):
 *    AF	Adam Fritzler
 *
 *  Modification History:
 *	30-Dec-99	AF	Split off from the tms380tr driver.
 *	22-Jan-00	AF	Updated to use indirect read/writes 
 *	23-Nov-00	JG	New PCI API, cleanups
 *
 *
 *  TODO:
 *	1. See if we can use MMIO instead of inb/outb/inw/outw
 *	2. Add support for Mk1 (has AT24 attached to the PCI
 *		config registers)
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/trdevice.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "tms380tr.h"
#include "abyss.h"            /* Madge-specific constants */

static char version[] __devinitdata =
"abyss.c: v1.02 23/11/2000 by Adam Fritzler\n";

#define ABYSS_IO_EXTENT 64

static DEFINE_PCI_DEVICE_TABLE(abyss_pci_tbl) = {
	{ PCI_VENDOR_ID_MADGE, PCI_DEVICE_ID_MADGE_MK2,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_NETWORK_TOKEN_RING << 8, 0x00ffffff, },
	{ }			/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, abyss_pci_tbl);

MODULE_LICENSE("GPL");

static int abyss_open(struct net_device *dev);
static int abyss_close(struct net_device *dev);
static void abyss_enable(struct net_device *dev);
static int abyss_chipset_init(struct net_device *dev);
static void abyss_read_eeprom(struct net_device *dev);
static unsigned short abyss_setnselout_pins(struct net_device *dev);

static void at24_writedatabyte(unsigned long regaddr, unsigned char byte);
static int at24_sendfullcmd(unsigned long regaddr, unsigned char cmd, unsigned char addr);
static int at24_sendcmd(unsigned long regaddr, unsigned char cmd);
static unsigned char at24_readdatabit(unsigned long regaddr);
static unsigned char at24_readdatabyte(unsigned long regaddr);
static int at24_waitforack(unsigned long regaddr);
static int at24_waitfornack(unsigned long regaddr);
static void at24_setlines(unsigned long regaddr, unsigned char clock, unsigned char data);
static void at24_start(unsigned long regaddr);
static unsigned char at24_readb(unsigned long regaddr, unsigned char addr);

static unsigned short abyss_sifreadb(struct net_device *dev, unsigned short reg)
{
	return inb(dev->base_addr + reg);
}

static unsigned short abyss_sifreadw(struct net_device *dev, unsigned short reg)
{
	return inw(dev->base_addr + reg);
}

static void abyss_sifwriteb(struct net_device *dev, unsigned short val, unsigned short reg)
{
	outb(val, dev->base_addr + reg);
}

static void abyss_sifwritew(struct net_device *dev, unsigned short val, unsigned short reg)
{
	outw(val, dev->base_addr + reg);
}

static struct net_device_ops abyss_netdev_ops;

static int __devinit abyss_attach(struct pci_dev *pdev, const struct pci_device_id *ent)
{	
	static int versionprinted;
	struct net_device *dev;
	struct net_local *tp;
	int ret, pci_irq_line;
	unsigned long pci_ioaddr;
	
	if (versionprinted++ == 0)
		printk("%s", version);

	if (pci_enable_device(pdev))
		return -EIO;

	/* Remove I/O space marker in bit 0. */
	pci_irq_line = pdev->irq;
	pci_ioaddr = pci_resource_start (pdev, 0);
		
	/* At this point we have found a valid card. */
		
	dev = alloc_trdev(sizeof(struct net_local));
	if (!dev)
		return -ENOMEM;

	if (!request_region(pci_ioaddr, ABYSS_IO_EXTENT, dev->name)) {
		ret = -EBUSY;
		goto err_out_trdev;
	}
		
	ret = request_irq(pdev->irq, tms380tr_interrupt, IRQF_SHARED,
			  dev->name, dev);
	if (ret)
		goto err_out_region;
		
	dev->base_addr	= pci_ioaddr;
	dev->irq	= pci_irq_line;
		
	printk("%s: Madge Smart 16/4 PCI Mk2 (Abyss)\n", dev->name);
	printk("%s:    IO: %#4lx  IRQ: %d\n",
	       dev->name, pci_ioaddr, dev->irq);
	/*
	 * The TMS SIF registers lay 0x10 above the card base address.
	 */
	dev->base_addr += 0x10;
		
	ret = tmsdev_init(dev, &pdev->dev);
	if (ret) {
		printk("%s: unable to get memory for dev->priv.\n", 
		       dev->name);
		goto err_out_irq;
	}

	abyss_read_eeprom(dev);

	printk("%s:    Ring Station Address: %pM\n", dev->name, dev->dev_addr);

	tp = netdev_priv(dev);
	tp->setnselout = abyss_setnselout_pins;
	tp->sifreadb = abyss_sifreadb;
	tp->sifreadw = abyss_sifreadw;
	tp->sifwriteb = abyss_sifwriteb;
	tp->sifwritew = abyss_sifwritew;

	memcpy(tp->ProductID, "Madge PCI 16/4 Mk2", PROD_ID_SIZE + 1);
		
	dev->netdev_ops = &abyss_netdev_ops;

	pci_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	ret = register_netdev(dev);
	if (ret)
		goto err_out_tmsdev;
	return 0;

err_out_tmsdev:
	pci_set_drvdata(pdev, NULL);
	tmsdev_term(dev);
err_out_irq:
	free_irq(pdev->irq, dev);
err_out_region:
	release_region(pci_ioaddr, ABYSS_IO_EXTENT);
err_out_trdev:
	free_netdev(dev);
	return ret;
}

static unsigned short abyss_setnselout_pins(struct net_device *dev)
{
	unsigned short val = 0;
	struct net_local *tp = netdev_priv(dev);
	
	if(tp->DataRate == SPEED_4)
		val |= 0x01;  /* Set 4Mbps */
	else
		val |= 0x00;  /* Set 16Mbps */
	
	return val;
}

/*
 * The following Madge boards should use this code:
 *   - Smart 16/4 PCI Mk2 (Abyss)
 *   - Smart 16/4 PCI Mk1 (PCI T)
 *   - Smart 16/4 Client Plus PnP (Big Apple)
 *   - Smart 16/4 Cardbus Mk2
 *
 * These access an Atmel AT24 SEEPROM using their glue chip registers. 
 *
 */
static void at24_writedatabyte(unsigned long regaddr, unsigned char byte)
{
	int i;
	
	for (i = 0; i < 8; i++) {
		at24_setlines(regaddr, 0, (byte >> (7-i))&0x01);
		at24_setlines(regaddr, 1, (byte >> (7-i))&0x01);
		at24_setlines(regaddr, 0, (byte >> (7-i))&0x01);
	}
}

static int at24_sendfullcmd(unsigned long regaddr, unsigned char cmd, unsigned char addr)
{
	if (at24_sendcmd(regaddr, cmd)) {
		at24_writedatabyte(regaddr, addr);
		return at24_waitforack(regaddr);
	}
	return 0;
}

static int at24_sendcmd(unsigned long regaddr, unsigned char cmd)
{
	int i;
	
	for (i = 0; i < 10; i++) {
		at24_start(regaddr);
		at24_writedatabyte(regaddr, cmd);
		if (at24_waitforack(regaddr))
			return 1;
	}
	return 0;
}

static unsigned char at24_readdatabit(unsigned long regaddr)
{
	unsigned char val;

	at24_setlines(regaddr, 0, 1);
	at24_setlines(regaddr, 1, 1);
	val = (inb(regaddr) & AT24_DATA)?1:0;
	at24_setlines(regaddr, 1, 1);
	at24_setlines(regaddr, 0, 1);
	return val;
}

static unsigned char at24_readdatabyte(unsigned long regaddr)
{
	unsigned char data = 0;
	int i;
	
	for (i = 0; i < 8; i++) {
		data <<= 1;
		data |= at24_readdatabit(regaddr);
	}

	return data;
}

static int at24_waitforack(unsigned long regaddr)
{
	int i;
	
	for (i = 0; i < 10; i++) {
		if ((at24_readdatabit(regaddr) & 0x01) == 0x00)
			return 1;
	}
	return 0;
}

static int at24_waitfornack(unsigned long regaddr)
{
	int i;
	for (i = 0; i < 10; i++) {
		if ((at24_readdatabit(regaddr) & 0x01) == 0x01)
			return 1;
	}
	return 0;
}

static void at24_setlines(unsigned long regaddr, unsigned char clock, unsigned char data)
{
	unsigned char val = AT24_ENABLE;
	if (clock)
		val |= AT24_CLOCK;
	if (data)
		val |= AT24_DATA;

	outb(val, regaddr); 
	tms380tr_wait(20); /* Very necessary. */
}

static void at24_start(unsigned long regaddr)
{
	at24_setlines(regaddr, 0, 1);
	at24_setlines(regaddr, 1, 1);
	at24_setlines(regaddr, 1, 0);
	at24_setlines(regaddr, 0, 1);
}

static unsigned char at24_readb(unsigned long regaddr, unsigned char addr)
{
	unsigned char data = 0xff;
	
	if (at24_sendfullcmd(regaddr, AT24_WRITE, addr)) {
		if (at24_sendcmd(regaddr, AT24_READ)) {
			data = at24_readdatabyte(regaddr);
			if (!at24_waitfornack(regaddr))
				data = 0xff;
		}
	}
	return data;
}


/*
 * Enable basic functions of the Madge chipset needed
 * for initialization.
 */
static void abyss_enable(struct net_device *dev)
{
	unsigned char reset_reg;
	unsigned long ioaddr;
	
	ioaddr = dev->base_addr;
	reset_reg = inb(ioaddr + PCIBM2_RESET_REG);
	reset_reg |= PCIBM2_RESET_REG_CHIP_NRES;
	outb(reset_reg, ioaddr + PCIBM2_RESET_REG);
	tms380tr_wait(100);
}

/*
 * Enable the functions of the Madge chipset needed for
 * full working order. 
 */
static int abyss_chipset_init(struct net_device *dev)
{
	unsigned char reset_reg;
	unsigned long ioaddr;
	
	ioaddr = dev->base_addr;
	
	reset_reg = inb(ioaddr + PCIBM2_RESET_REG);
	
	reset_reg |= PCIBM2_RESET_REG_CHIP_NRES;
	outb(reset_reg, ioaddr + PCIBM2_RESET_REG);
	
	reset_reg &= ~(PCIBM2_RESET_REG_CHIP_NRES |
		       PCIBM2_RESET_REG_FIFO_NRES | 
		       PCIBM2_RESET_REG_SIF_NRES);
	outb(reset_reg, ioaddr + PCIBM2_RESET_REG);
	
	tms380tr_wait(100);
	
	reset_reg |= PCIBM2_RESET_REG_CHIP_NRES;
	outb(reset_reg, ioaddr + PCIBM2_RESET_REG);
	
	reset_reg |= PCIBM2_RESET_REG_SIF_NRES;
	outb(reset_reg, ioaddr + PCIBM2_RESET_REG);

	reset_reg |= PCIBM2_RESET_REG_FIFO_NRES;
	outb(reset_reg, ioaddr + PCIBM2_RESET_REG);

	outb(PCIBM2_INT_CONTROL_REG_SINTEN | 
	     PCIBM2_INT_CONTROL_REG_PCI_ERR_ENABLE, 
	     ioaddr + PCIBM2_INT_CONTROL_REG);
  
	outb(30, ioaddr + PCIBM2_FIFO_THRESHOLD);
	
	return 0;
}

static inline void abyss_chipset_close(struct net_device *dev)
{
	unsigned long ioaddr;
	
	ioaddr = dev->base_addr;
	outb(0, ioaddr + PCIBM2_RESET_REG);
}

/*
 * Read configuration data from the AT24 SEEPROM on Madge cards.
 *
 */
static void abyss_read_eeprom(struct net_device *dev)
{
	struct net_local *tp;
	unsigned long ioaddr;
	unsigned short val;
	int i;
	
	tp = netdev_priv(dev);
	ioaddr = dev->base_addr;
	
	/* Must enable glue chip first */
	abyss_enable(dev);
	
	val = at24_readb(ioaddr + PCIBM2_SEEPROM_REG, 
			 PCIBM2_SEEPROM_RING_SPEED);
	tp->DataRate = val?SPEED_4:SPEED_16; /* set open speed */
	printk("%s:    SEEPROM: ring speed: %dMb/sec\n", dev->name, tp->DataRate);
	
	val = at24_readb(ioaddr + PCIBM2_SEEPROM_REG,
			 PCIBM2_SEEPROM_RAM_SIZE) * 128;
	printk("%s:    SEEPROM: adapter RAM: %dkb\n", dev->name, val);
	
	dev->addr_len = 6;
	for (i = 0; i < 6; i++) 
		dev->dev_addr[i] = at24_readb(ioaddr + PCIBM2_SEEPROM_REG, 
					      PCIBM2_SEEPROM_BIA+i);
}

static int abyss_open(struct net_device *dev)
{  
	abyss_chipset_init(dev);
	tms380tr_open(dev);
	return 0;
}

static int abyss_close(struct net_device *dev)
{
	tms380tr_close(dev);
	abyss_chipset_close(dev);
	return 0;
}

static void __devexit abyss_detach (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	
	BUG_ON(!dev);
	unregister_netdev(dev);
	release_region(dev->base_addr-0x10, ABYSS_IO_EXTENT);
	free_irq(dev->irq, dev);
	tmsdev_term(dev);
	free_netdev(dev);
	pci_set_drvdata(pdev, NULL);
}

static struct pci_driver abyss_driver = {
	.name		= "abyss",
	.id_table	= abyss_pci_tbl,
	.probe		= abyss_attach,
	.remove		= __devexit_p(abyss_detach),
};

static int __init abyss_init (void)
{
	abyss_netdev_ops = tms380tr_netdev_ops;

	abyss_netdev_ops.ndo_open = abyss_open;
	abyss_netdev_ops.ndo_stop = abyss_close;

	return pci_register_driver(&abyss_driver);
}

static void __exit abyss_rmmod (void)
{
	pci_unregister_driver (&abyss_driver);
}

module_init(abyss_init);
module_exit(abyss_rmmod);

