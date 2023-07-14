/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/gpio.h>

#include <bcm63xx_regs.h>
#include <bcm63xx_io.h>
#include "bcm63xx_pcmcia.h"

#define PFX	"bcm63xx_pcmcia: "

#ifdef CONFIG_CARDBUS
/* if cardbus is used, platform device needs reference to actual pci
 * device */
static struct pci_dev *bcm63xx_cb_dev;
#endif

/*
 * read/write helper for pcmcia regs
 */
static inline u32 pcmcia_readl(struct bcm63xx_pcmcia_socket *skt, u32 off)
{
	return bcm_readl(skt->base + off);
}

static inline void pcmcia_writel(struct bcm63xx_pcmcia_socket *skt,
				 u32 val, u32 off)
{
	bcm_writel(val, skt->base + off);
}

/*
 * This callback should (re-)initialise the socket, turn on status
 * interrupts and PCMCIA bus, and wait for power to stabilise so that
 * the card status signals report correctly.
 *
 * Hardware cannot do that.
 */
static int bcm63xx_pcmcia_sock_init(struct pcmcia_socket *sock)
{
	return 0;
}

/*
 * This callback should remove power on the socket, disable IRQs from
 * the card, turn off status interrupts, and disable the PCMCIA bus.
 *
 * Hardware cannot do that.
 */
static int bcm63xx_pcmcia_suspend(struct pcmcia_socket *sock)
{
	return 0;
}

/*
 * Implements the set_socket() operation for the in-kernel PCMCIA
 * service (formerly SS_SetSocket in Card Services). We more or
 * less punt all of this work and let the kernel handle the details
 * of power configuration, reset, &c. We also record the value of
 * `state' in order to regurgitate it to the PCMCIA core later.
 */
static int bcm63xx_pcmcia_set_socket(struct pcmcia_socket *sock,
				     socket_state_t *state)
{
	struct bcm63xx_pcmcia_socket *skt;
	unsigned long flags;
	u32 val;

	skt = sock->driver_data;

	spin_lock_irqsave(&skt->lock, flags);

	/* note: hardware cannot control socket power, so we will
	 * always report SS_POWERON */

	/* apply socket reset */
	val = pcmcia_readl(skt, PCMCIA_C1_REG);
	if (state->flags & SS_RESET)
		val |= PCMCIA_C1_RESET_MASK;
	else
		val &= ~PCMCIA_C1_RESET_MASK;

	/* reverse reset logic for cardbus card */
	if (skt->card_detected && (skt->card_type & CARD_CARDBUS))
		val ^= PCMCIA_C1_RESET_MASK;

	pcmcia_writel(skt, val, PCMCIA_C1_REG);

	/* keep requested state for event reporting */
	skt->requested_state = *state;

	spin_unlock_irqrestore(&skt->lock, flags);

	return 0;
}

/*
 * identity cardtype from VS[12] input, CD[12] input while only VS2 is
 * floating, and CD[12] input while only VS1 is floating
 */
enum {
	IN_VS1 = (1 << 0),
	IN_VS2 = (1 << 1),
	IN_CD1_VS2H = (1 << 2),
	IN_CD2_VS2H = (1 << 3),
	IN_CD1_VS1H = (1 << 4),
	IN_CD2_VS1H = (1 << 5),
};

static const u8 vscd_to_cardtype[] = {

	/* VS1 float, VS2 float */
	[IN_VS1 | IN_VS2] = (CARD_PCCARD | CARD_5V),

	/* VS1 grounded, VS2 float */
	[IN_VS2] = (CARD_PCCARD | CARD_5V | CARD_3V),

	/* VS1 grounded, VS2 grounded */
	[0] = (CARD_PCCARD | CARD_5V | CARD_3V | CARD_XV),

	/* VS1 tied to CD1, VS2 float */
	[IN_VS1 | IN_VS2 | IN_CD1_VS1H] = (CARD_CARDBUS | CARD_3V),

	/* VS1 grounded, VS2 tied to CD2 */
	[IN_VS2 | IN_CD2_VS2H] = (CARD_CARDBUS | CARD_3V | CARD_XV),

	/* VS1 tied to CD2, VS2 grounded */
	[IN_VS1 | IN_CD2_VS1H] = (CARD_CARDBUS | CARD_3V | CARD_XV | CARD_YV),

	/* VS1 float, VS2 grounded */
	[IN_VS1] = (CARD_PCCARD | CARD_XV),

	/* VS1 float, VS2 tied to CD2 */
	[IN_VS1 | IN_VS2 | IN_CD2_VS2H] = (CARD_CARDBUS | CARD_3V),

	/* VS1 float, VS2 tied to CD1 */
	[IN_VS1 | IN_VS2 | IN_CD1_VS2H] = (CARD_CARDBUS | CARD_XV | CARD_YV),

	/* VS1 tied to CD2, VS2 float */
	[IN_VS1 | IN_VS2 | IN_CD2_VS1H] = (CARD_CARDBUS | CARD_YV),

	/* VS2 grounded, VS1 is tied to CD1, CD2 is grounded */
	[IN_VS1 | IN_CD1_VS1H] = 0, /* ignore cardbay */
};

/*
 * poll hardware to check card insertion status
 */
static unsigned int __get_socket_status(struct bcm63xx_pcmcia_socket *skt)
{
	unsigned int stat;
	u32 val;

	stat = 0;

	/* check CD for card presence */
	val = pcmcia_readl(skt, PCMCIA_C1_REG);

	if (!(val & PCMCIA_C1_CD1_MASK) && !(val & PCMCIA_C1_CD2_MASK))
		stat |= SS_DETECT;

	/* if new insertion, detect cardtype */
	if ((stat & SS_DETECT) && !skt->card_detected) {
		unsigned int stat = 0;

		/* float VS1, float VS2 */
		val |= PCMCIA_C1_VS1OE_MASK;
		val |= PCMCIA_C1_VS2OE_MASK;
		pcmcia_writel(skt, val, PCMCIA_C1_REG);

		/* wait for output to stabilize and read VS[12] */
		udelay(10);
		val = pcmcia_readl(skt, PCMCIA_C1_REG);
		stat |= (val & PCMCIA_C1_VS1_MASK) ? IN_VS1 : 0;
		stat |= (val & PCMCIA_C1_VS2_MASK) ? IN_VS2 : 0;

		/* drive VS1 low, float VS2 */
		val &= ~PCMCIA_C1_VS1OE_MASK;
		val |= PCMCIA_C1_VS2OE_MASK;
		pcmcia_writel(skt, val, PCMCIA_C1_REG);

		/* wait for output to stabilize and read CD[12] */
		udelay(10);
		val = pcmcia_readl(skt, PCMCIA_C1_REG);
		stat |= (val & PCMCIA_C1_CD1_MASK) ? IN_CD1_VS2H : 0;
		stat |= (val & PCMCIA_C1_CD2_MASK) ? IN_CD2_VS2H : 0;

		/* float VS1, drive VS2 low */
		val |= PCMCIA_C1_VS1OE_MASK;
		val &= ~PCMCIA_C1_VS2OE_MASK;
		pcmcia_writel(skt, val, PCMCIA_C1_REG);

		/* wait for output to stabilize and read CD[12] */
		udelay(10);
		val = pcmcia_readl(skt, PCMCIA_C1_REG);
		stat |= (val & PCMCIA_C1_CD1_MASK) ? IN_CD1_VS1H : 0;
		stat |= (val & PCMCIA_C1_CD2_MASK) ? IN_CD2_VS1H : 0;

		/* guess cardtype from all this */
		skt->card_type = vscd_to_cardtype[stat];
		if (!skt->card_type)
			dev_err(&skt->socket.dev, "unsupported card type\n");

		/* drive both VS pin to 0 again */
		val &= ~(PCMCIA_C1_VS1OE_MASK | PCMCIA_C1_VS2OE_MASK);

		/* enable correct logic */
		val &= ~(PCMCIA_C1_EN_PCMCIA_MASK | PCMCIA_C1_EN_CARDBUS_MASK);
		if (skt->card_type & CARD_PCCARD)
			val |= PCMCIA_C1_EN_PCMCIA_MASK;
		else
			val |= PCMCIA_C1_EN_CARDBUS_MASK;

		pcmcia_writel(skt, val, PCMCIA_C1_REG);
	}
	skt->card_detected = (stat & SS_DETECT) ? 1 : 0;

	/* report card type/voltage */
	if (skt->card_type & CARD_CARDBUS)
		stat |= SS_CARDBUS;
	if (skt->card_type & CARD_3V)
		stat |= SS_3VCARD;
	if (skt->card_type & CARD_XV)
		stat |= SS_XVCARD;
	stat |= SS_POWERON;

	if (gpio_get_value(skt->pd->ready_gpio))
		stat |= SS_READY;

	return stat;
}

/*
 * core request to get current socket status
 */
static int bcm63xx_pcmcia_get_status(struct pcmcia_socket *sock,
				     unsigned int *status)
{
	struct bcm63xx_pcmcia_socket *skt;

	skt = sock->driver_data;

	spin_lock_bh(&skt->lock);
	*status = __get_socket_status(skt);
	spin_unlock_bh(&skt->lock);

	return 0;
}

/*
 * socket polling timer callback
 */
static void bcm63xx_pcmcia_poll(struct timer_list *t)
{
	struct bcm63xx_pcmcia_socket *skt;
	unsigned int stat, events;

	skt = from_timer(skt, t, timer);

	spin_lock_bh(&skt->lock);

	stat = __get_socket_status(skt);

	/* keep only changed bits, and mask with required one from the
	 * core */
	events = (stat ^ skt->old_status) & skt->requested_state.csc_mask;
	skt->old_status = stat;
	spin_unlock_bh(&skt->lock);

	if (events)
		pcmcia_parse_events(&skt->socket, events);

	mod_timer(&skt->timer,
		  jiffies + msecs_to_jiffies(BCM63XX_PCMCIA_POLL_RATE));
}

static int bcm63xx_pcmcia_set_io_map(struct pcmcia_socket *sock,
				     struct pccard_io_map *map)
{
	/* this doesn't seem to be called by pcmcia layer if static
	 * mapping is used */
	return 0;
}

static int bcm63xx_pcmcia_set_mem_map(struct pcmcia_socket *sock,
				      struct pccard_mem_map *map)
{
	struct bcm63xx_pcmcia_socket *skt;
	struct resource *res;

	skt = sock->driver_data;
	if (map->flags & MAP_ATTRIB)
		res = skt->attr_res;
	else
		res = skt->common_res;

	map->static_start = res->start + map->card_start;
	return 0;
}

static struct pccard_operations bcm63xx_pcmcia_operations = {
	.init			= bcm63xx_pcmcia_sock_init,
	.suspend		= bcm63xx_pcmcia_suspend,
	.get_status		= bcm63xx_pcmcia_get_status,
	.set_socket		= bcm63xx_pcmcia_set_socket,
	.set_io_map		= bcm63xx_pcmcia_set_io_map,
	.set_mem_map		= bcm63xx_pcmcia_set_mem_map,
};

/*
 * register pcmcia socket to core
 */
static int bcm63xx_drv_pcmcia_probe(struct platform_device *pdev)
{
	struct bcm63xx_pcmcia_socket *skt;
	struct pcmcia_socket *sock;
	struct resource *res;
	unsigned int regmem_size = 0, iomem_size = 0;
	u32 val;
	int ret;
	int irq;

	skt = kzalloc(sizeof(*skt), GFP_KERNEL);
	if (!skt)
		return -ENOMEM;
	spin_lock_init(&skt->lock);
	sock = &skt->socket;
	sock->driver_data = skt;

	/* make sure we have all resources we need */
	skt->common_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	skt->attr_res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	irq = platform_get_irq(pdev, 0);
	skt->pd = pdev->dev.platform_data;
	if (!skt->common_res || !skt->attr_res || (irq < 0) || !skt->pd) {
		ret = -EINVAL;
		goto err;
	}

	/* remap pcmcia registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regmem_size = resource_size(res);
	if (!request_mem_region(res->start, regmem_size, "bcm63xx_pcmcia")) {
		ret = -EINVAL;
		goto err;
	}
	skt->reg_res = res;

	skt->base = ioremap(res->start, regmem_size);
	if (!skt->base) {
		ret = -ENOMEM;
		goto err;
	}

	/* remap io registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	iomem_size = resource_size(res);
	skt->io_base = ioremap(res->start, iomem_size);
	if (!skt->io_base) {
		ret = -ENOMEM;
		goto err;
	}

	/* resources are static */
	sock->resource_ops = &pccard_static_ops;
	sock->ops = &bcm63xx_pcmcia_operations;
	sock->owner = THIS_MODULE;
	sock->dev.parent = &pdev->dev;
	sock->features = SS_CAP_STATIC_MAP | SS_CAP_PCCARD;
	sock->io_offset = (unsigned long)skt->io_base;
	sock->pci_irq = irq;

#ifdef CONFIG_CARDBUS
	sock->cb_dev = bcm63xx_cb_dev;
	if (bcm63xx_cb_dev)
		sock->features |= SS_CAP_CARDBUS;
#endif

	/* assume common & attribute memory have the same size */
	sock->map_size = resource_size(skt->common_res);

	/* initialize polling timer */
	timer_setup(&skt->timer, bcm63xx_pcmcia_poll, 0);

	/* initialize  pcmcia  control register,  drive  VS[12] to  0,
	 * leave CB IDSEL to the old  value since it is set by the PCI
	 * layer */
	val = pcmcia_readl(skt, PCMCIA_C1_REG);
	val &= PCMCIA_C1_CBIDSEL_MASK;
	val |= PCMCIA_C1_EN_PCMCIA_GPIO_MASK;
	pcmcia_writel(skt, val, PCMCIA_C1_REG);

	/*
	 * Hardware has only one set of timings registers, not one for
	 * each memory access type, so we configure them for the
	 * slowest one: attribute memory.
	 */
	val = PCMCIA_C2_DATA16_MASK;
	val |= 10 << PCMCIA_C2_RWCOUNT_SHIFT;
	val |= 6 << PCMCIA_C2_INACTIVE_SHIFT;
	val |= 3 << PCMCIA_C2_SETUP_SHIFT;
	val |= 3 << PCMCIA_C2_HOLD_SHIFT;
	pcmcia_writel(skt, val, PCMCIA_C2_REG);

	ret = pcmcia_register_socket(sock);
	if (ret)
		goto err;

	/* start polling socket */
	mod_timer(&skt->timer,
		  jiffies + msecs_to_jiffies(BCM63XX_PCMCIA_POLL_RATE));

	platform_set_drvdata(pdev, skt);
	return 0;

err:
	if (skt->io_base)
		iounmap(skt->io_base);
	if (skt->base)
		iounmap(skt->base);
	if (skt->reg_res)
		release_mem_region(skt->reg_res->start, regmem_size);
	kfree(skt);
	return ret;
}

static int bcm63xx_drv_pcmcia_remove(struct platform_device *pdev)
{
	struct bcm63xx_pcmcia_socket *skt;
	struct resource *res;

	skt = platform_get_drvdata(pdev);
	timer_shutdown_sync(&skt->timer);
	iounmap(skt->base);
	iounmap(skt->io_base);
	res = skt->reg_res;
	release_mem_region(res->start, resource_size(res));
	kfree(skt);
	return 0;
}

struct platform_driver bcm63xx_pcmcia_driver = {
	.probe	= bcm63xx_drv_pcmcia_probe,
	.remove	= bcm63xx_drv_pcmcia_remove,
	.driver	= {
		.name	= "bcm63xx_pcmcia",
		.owner  = THIS_MODULE,
	},
};

#ifdef CONFIG_CARDBUS
static int bcm63xx_cb_probe(struct pci_dev *dev,
				      const struct pci_device_id *id)
{
	/* keep pci device */
	bcm63xx_cb_dev = dev;
	return platform_driver_register(&bcm63xx_pcmcia_driver);
}

static void bcm63xx_cb_exit(struct pci_dev *dev)
{
	platform_driver_unregister(&bcm63xx_pcmcia_driver);
	bcm63xx_cb_dev = NULL;
}

static const struct pci_device_id bcm63xx_cb_table[] = {
	{
		.vendor		= PCI_VENDOR_ID_BROADCOM,
		.device		= BCM6348_CPU_ID,
		.subvendor	= PCI_VENDOR_ID_BROADCOM,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_BRIDGE_CARDBUS << 8,
		.class_mask	= ~0,
	},

	{
		.vendor		= PCI_VENDOR_ID_BROADCOM,
		.device		= BCM6358_CPU_ID,
		.subvendor	= PCI_VENDOR_ID_BROADCOM,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_BRIDGE_CARDBUS << 8,
		.class_mask	= ~0,
	},

	{ },
};

MODULE_DEVICE_TABLE(pci, bcm63xx_cb_table);

static struct pci_driver bcm63xx_cardbus_driver = {
	.name		= "bcm63xx_cardbus",
	.id_table	= bcm63xx_cb_table,
	.probe		= bcm63xx_cb_probe,
	.remove		= bcm63xx_cb_exit,
};
#endif

/*
 * if cardbus support is enabled, register our platform device after
 * our fake cardbus bridge has been registered
 */
static int __init bcm63xx_pcmcia_init(void)
{
#ifdef CONFIG_CARDBUS
	return pci_register_driver(&bcm63xx_cardbus_driver);
#else
	return platform_driver_register(&bcm63xx_pcmcia_driver);
#endif
}

static void __exit bcm63xx_pcmcia_exit(void)
{
#ifdef CONFIG_CARDBUS
	return pci_unregister_driver(&bcm63xx_cardbus_driver);
#else
	platform_driver_unregister(&bcm63xx_pcmcia_driver);
#endif
}

module_init(bcm63xx_pcmcia_init);
module_exit(bcm63xx_pcmcia_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Maxime Bizon <mbizon@freebox.fr>");
MODULE_DESCRIPTION("Linux PCMCIA Card Services: bcm63xx Socket Controller");
