/*
 * jazzsonic.c
 *
 * (C) 2005 Finn Thain
 *
 * Converted to DMA API, and (from the mac68k project) introduced
 * dhd's support for 16-bit cards.
 *
 * (C) 1996,1998 by Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 *
 * This driver is based on work from Andreas Busse, but most of
 * the code is rewritten.
 *
 * (C) 1995 by Andreas Busse (andy@waldorf-gmbh.de)
 *
 * A driver for the onboard Sonic ethernet controller on Mips Jazz
 * systems (Acer Pica-61, Mips Magnum 4000, Olivetti M700 and
 * perhaps others, too)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <asm/bootinfo.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/jazz.h>
#include <asm/jazzdma.h>

static char jazz_sonic_string[] = "jazzsonic";

#define SONIC_MEM_SIZE	0x100

#include "sonic.h"

/*
 * Macros to access SONIC registers
 */
#define SONIC_READ(reg) (*((volatile unsigned int *)dev->base_addr+reg))

#define SONIC_WRITE(reg,val)						\
do {									\
	*((volatile unsigned int *)dev->base_addr+(reg)) = (val);		\
} while (0)


/* use 0 for production, 1 for verification, >1 for debug */
#ifdef SONIC_DEBUG
static unsigned int sonic_debug = SONIC_DEBUG;
#else
static unsigned int sonic_debug = 1;
#endif

/*
 * We cannot use station (ethernet) address prefixes to detect the
 * sonic controller since these are board manufacturer depended.
 * So we check for known Silicon Revision IDs instead.
 */
static unsigned short known_revisions[] =
{
	0x04,			/* Mips Magnum 4000 */
	0xffff			/* end of list */
};

static int jazzsonic_open(struct net_device* dev)
{
	if (request_irq(dev->irq, &sonic_interrupt, IRQF_DISABLED, "sonic", dev)) {
		printk(KERN_ERR "%s: unable to get IRQ %d.\n", dev->name, dev->irq);
		return -EAGAIN;
	}
	return sonic_open(dev);
}

static int jazzsonic_close(struct net_device* dev)
{
	int err;
	err = sonic_close(dev);
	free_irq(dev->irq, dev);
	return err;
}

static int __init sonic_probe1(struct net_device *dev)
{
	static unsigned version_printed;
	unsigned int silicon_revision;
	unsigned int val;
	struct sonic_local *lp = netdev_priv(dev);
	int err = -ENODEV;
	int i;

	if (!request_mem_region(dev->base_addr, SONIC_MEM_SIZE, jazz_sonic_string))
		return -EBUSY;

	/*
	 * get the Silicon Revision ID. If this is one of the known
	 * one assume that we found a SONIC ethernet controller at
	 * the expected location.
	 */
	silicon_revision = SONIC_READ(SONIC_SR);
	if (sonic_debug > 1)
		printk("SONIC Silicon Revision = 0x%04x\n",silicon_revision);

	i = 0;
	while (known_revisions[i] != 0xffff
	       && known_revisions[i] != silicon_revision)
		i++;

	if (known_revisions[i] == 0xffff) {
		printk("SONIC ethernet controller not found (0x%4x)\n",
		       silicon_revision);
		goto out;
	}

	if (sonic_debug  &&  version_printed++ == 0)
		printk(version);

	printk(KERN_INFO "%s: Sonic ethernet found at 0x%08lx, ", lp->device->bus_id, dev->base_addr);

	/*
	 * Put the sonic into software reset, then
	 * retrieve and print the ethernet address.
	 */
	SONIC_WRITE(SONIC_CMD,SONIC_CR_RST);
	SONIC_WRITE(SONIC_CEP,0);
	for (i=0; i<3; i++) {
		val = SONIC_READ(SONIC_CAP0-i);
		dev->dev_addr[i*2] = val;
		dev->dev_addr[i*2+1] = val >> 8;
	}

	err = -ENOMEM;

	/* Initialize the device structure. */

	lp->dma_bitmode = SONIC_BITMODE32;

	/* Allocate the entire chunk of memory for the descriptors.
           Note that this cannot cross a 64K boundary. */
	if ((lp->descriptors = dma_alloc_coherent(lp->device,
				SIZEOF_SONIC_DESC * SONIC_BUS_SCALE(lp->dma_bitmode),
				&lp->descriptors_laddr, GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "%s: couldn't alloc DMA memory for descriptors.\n", lp->device->bus_id);
		goto out;
	}

	/* Now set up the pointers to point to the appropriate places */
	lp->cda = lp->descriptors;
	lp->tda = lp->cda + (SIZEOF_SONIC_CDA
	                     * SONIC_BUS_SCALE(lp->dma_bitmode));
	lp->rda = lp->tda + (SIZEOF_SONIC_TD * SONIC_NUM_TDS
	                     * SONIC_BUS_SCALE(lp->dma_bitmode));
	lp->rra = lp->rda + (SIZEOF_SONIC_RD * SONIC_NUM_RDS
	                     * SONIC_BUS_SCALE(lp->dma_bitmode));

	lp->cda_laddr = lp->descriptors_laddr;
	lp->tda_laddr = lp->cda_laddr + (SIZEOF_SONIC_CDA
	                     * SONIC_BUS_SCALE(lp->dma_bitmode));
	lp->rda_laddr = lp->tda_laddr + (SIZEOF_SONIC_TD * SONIC_NUM_TDS
	                     * SONIC_BUS_SCALE(lp->dma_bitmode));
	lp->rra_laddr = lp->rda_laddr + (SIZEOF_SONIC_RD * SONIC_NUM_RDS
	                     * SONIC_BUS_SCALE(lp->dma_bitmode));

	dev->open = jazzsonic_open;
	dev->stop = jazzsonic_close;
	dev->hard_start_xmit = sonic_send_packet;
	dev->get_stats = sonic_get_stats;
	dev->set_multicast_list = &sonic_multicast_list;
	dev->tx_timeout = sonic_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;

	/*
	 * clear tally counter
	 */
	SONIC_WRITE(SONIC_CRCT,0xffff);
	SONIC_WRITE(SONIC_FAET,0xffff);
	SONIC_WRITE(SONIC_MPT,0xffff);

	return 0;
out:
	release_region(dev->base_addr, SONIC_MEM_SIZE);
	return err;
}

/*
 * Probe for a SONIC ethernet controller on a Mips Jazz board.
 * Actually probing is superfluous but we're paranoid.
 */
static int __init jazz_sonic_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct sonic_local *lp;
	struct resource *res;
	int err = 0;
	int i;
	DECLARE_MAC_BUF(mac);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	dev = alloc_etherdev(sizeof(struct sonic_local));
	if (!dev)
		return -ENOMEM;

	lp = netdev_priv(dev);
	lp->device = &pdev->dev;
	SET_NETDEV_DEV(dev, &pdev->dev);

	netdev_boot_setup_check(dev);

	dev->base_addr = res->start;
	dev->irq = platform_get_irq(pdev, 0);
	err = sonic_probe1(dev);
	if (err)
		goto out;
	err = register_netdev(dev);
	if (err)
		goto out1;

	printk("%s: MAC %s IRQ %d\n",
	       dev->name, print_mac(mac, dev->dev_addr), dev->irq);

	return 0;

out1:
	release_region(dev->base_addr, SONIC_MEM_SIZE);
out:
	free_netdev(dev);

	return err;
}

MODULE_DESCRIPTION("Jazz SONIC ethernet driver");
module_param(sonic_debug, int, 0);
MODULE_PARM_DESC(sonic_debug, "jazzsonic debug level (1-4)");

#include "sonic.c"

static int __devexit jazz_sonic_device_remove (struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct sonic_local* lp = netdev_priv(dev);

	unregister_netdev(dev);
	dma_free_coherent(lp->device, SIZEOF_SONIC_DESC * SONIC_BUS_SCALE(lp->dma_bitmode),
	                  lp->descriptors, lp->descriptors_laddr);
	release_region (dev->base_addr, SONIC_MEM_SIZE);
	free_netdev(dev);

	return 0;
}

static struct platform_driver jazz_sonic_driver = {
	.probe	= jazz_sonic_probe,
	.remove	= __devexit_p(jazz_sonic_device_remove),
	.driver	= {
		.name	= jazz_sonic_string,
	},
};

static int __init jazz_sonic_init_module(void)
{
	return platform_driver_register(&jazz_sonic_driver);
}

static void __exit jazz_sonic_cleanup_module(void)
{
	platform_driver_unregister(&jazz_sonic_driver);
}

module_init(jazz_sonic_init_module);
module_exit(jazz_sonic_cleanup_module);
