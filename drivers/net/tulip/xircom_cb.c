/*
 * xircom_cb: A driver for the (tulip-like) Xircom Cardbus ethernet cards
 *
 * This software is (C) by the respective authors, and licensed under the GPL
 * License.
 *
 * Written by Arjan van de Ven for Red Hat, Inc.
 * Based on work by Jeff Garzik, Doug Ledford and Donald Becker
 *
 *  	This software may be used and distributed according to the terms
 *      of the GNU General Public License, incorporated herein by reference.
 *
 *
 * 	$Id: xircom_cb.c,v 1.33 2001/03/19 14:02:07 arjanv Exp $
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ethtool.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#ifdef CONFIG_NET_POLL_CONTROLLER
#include <asm/irq.h>
#endif

#ifdef DEBUG
#define enter(x)   printk("Enter: %s, %s line %i\n",x,__FILE__,__LINE__)
#define leave(x)   printk("Leave: %s, %s line %i\n",x,__FILE__,__LINE__)
#else
#define enter(x)   do {} while (0)
#define leave(x)   do {} while (0)
#endif


MODULE_DESCRIPTION("Xircom Cardbus ethernet driver");
MODULE_AUTHOR("Arjan van de Ven <arjanv@redhat.com>");
MODULE_LICENSE("GPL");



/* IO registers on the card, offsets */
#define CSR0	0x00
#define CSR1	0x08
#define CSR2	0x10
#define CSR3	0x18
#define CSR4	0x20
#define CSR5	0x28
#define CSR6	0x30
#define CSR7	0x38
#define CSR8	0x40
#define CSR9	0x48
#define CSR10	0x50
#define CSR11	0x58
#define CSR12	0x60
#define CSR13	0x68
#define CSR14	0x70
#define CSR15	0x78
#define CSR16	0x80

/* PCI registers */
#define PCI_POWERMGMT 	0x40

/* Offsets of the buffers within the descriptor pages, in bytes */

#define NUMDESCRIPTORS 4

static int bufferoffsets[NUMDESCRIPTORS] = {128,2048,4096,6144};


struct xircom_private {
	/* Send and receive buffers, kernel-addressable and dma addressable forms */

	__le32 *rx_buffer;
	__le32 *tx_buffer;

	dma_addr_t rx_dma_handle;
	dma_addr_t tx_dma_handle;

	struct sk_buff *tx_skb[4];

	unsigned long io_port;
	int open;

	/* transmit_used is the rotating counter that indicates which transmit
	   descriptor has to be used next */
	int transmit_used;

	/* Spinlock to serialize register operations.
	   It must be helt while manipulating the following registers:
	   CSR0, CSR6, CSR7, CSR9, CSR10, CSR15
	 */
	spinlock_t lock;

	struct pci_dev *pdev;
	struct net_device *dev;
};


/* Function prototypes */
static int xircom_probe(struct pci_dev *pdev, const struct pci_device_id *id);
static void xircom_remove(struct pci_dev *pdev);
static irqreturn_t xircom_interrupt(int irq, void *dev_instance);
static netdev_tx_t xircom_start_xmit(struct sk_buff *skb,
					   struct net_device *dev);
static int xircom_open(struct net_device *dev);
static int xircom_close(struct net_device *dev);
static void xircom_up(struct xircom_private *card);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void xircom_poll_controller(struct net_device *dev);
#endif

static void investigate_read_descriptor(struct net_device *dev,struct xircom_private *card, int descnr, unsigned int bufferoffset);
static void investigate_write_descriptor(struct net_device *dev, struct xircom_private *card, int descnr, unsigned int bufferoffset);
static void read_mac_address(struct xircom_private *card);
static void transceiver_voodoo(struct xircom_private *card);
static void initialize_card(struct xircom_private *card);
static void trigger_transmit(struct xircom_private *card);
static void trigger_receive(struct xircom_private *card);
static void setup_descriptors(struct xircom_private *card);
static void remove_descriptors(struct xircom_private *card);
static int link_status_changed(struct xircom_private *card);
static void activate_receiver(struct xircom_private *card);
static void deactivate_receiver(struct xircom_private *card);
static void activate_transmitter(struct xircom_private *card);
static void deactivate_transmitter(struct xircom_private *card);
static void enable_transmit_interrupt(struct xircom_private *card);
static void enable_receive_interrupt(struct xircom_private *card);
static void enable_link_interrupt(struct xircom_private *card);
static void disable_all_interrupts(struct xircom_private *card);
static int link_status(struct xircom_private *card);



static DEFINE_PCI_DEVICE_TABLE(xircom_pci_table) = {
	{0x115D, 0x0003, PCI_ANY_ID, PCI_ANY_ID,},
	{0,},
};
MODULE_DEVICE_TABLE(pci, xircom_pci_table);

static struct pci_driver xircom_ops = {
	.name		= "xircom_cb",
	.id_table	= xircom_pci_table,
	.probe		= xircom_probe,
	.remove		= xircom_remove,
	.suspend =NULL,
	.resume =NULL
};


#ifdef DEBUG
static void print_binary(unsigned int number)
{
	int i,i2;
	char buffer[64];
	memset(buffer,0,64);
	i2=0;
	for (i=31;i>=0;i--) {
		if (number & (1<<i))
			buffer[i2++]='1';
		else
			buffer[i2++]='0';
		if ((i&3)==0)
			buffer[i2++]=' ';
	}
	printk("%s\n",buffer);
}
#endif

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	struct xircom_private *private = netdev_priv(dev);

	strcpy(info->driver, "xircom_cb");
	strcpy(info->bus_info, pci_name(private->pdev));
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
};

static const struct net_device_ops netdev_ops = {
	.ndo_open		= xircom_open,
	.ndo_stop		= xircom_close,
	.ndo_start_xmit		= xircom_start_xmit,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= xircom_poll_controller,
#endif
};

/* xircom_probe is the code that gets called on device insertion.
   it sets up the hardware and registers the device to the networklayer.

   TODO: Send 1 or 2 "dummy" packets here as the card seems to discard the
         first two packets that get send, and pump hates that.

 */
static int __devinit xircom_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net_device *dev = NULL;
	struct xircom_private *private;
	unsigned long flags;
	unsigned short tmp16;
	enter("xircom_probe");

	/* First do the PCI initialisation */

	if (pci_enable_device(pdev))
		return -ENODEV;

	/* disable all powermanagement */
	pci_write_config_dword(pdev, PCI_POWERMGMT, 0x0000);

	pci_set_master(pdev); /* Why isn't this done by pci_enable_device ?*/

	/* clear PCI status, if any */
	pci_read_config_word (pdev,PCI_STATUS, &tmp16);
	pci_write_config_word (pdev, PCI_STATUS,tmp16);

	if (!request_region(pci_resource_start(pdev, 0), 128, "xircom_cb")) {
		pr_err("%s: failed to allocate io-region\n", __func__);
		return -ENODEV;
	}

	/*
	   Before changing the hardware, allocate the memory.
	   This way, we can fail gracefully if not enough memory
	   is available.
	 */
	dev = alloc_etherdev(sizeof(struct xircom_private));
	if (!dev) {
		pr_err("%s: failed to allocate etherdev\n", __func__);
		goto device_fail;
	}
	private = netdev_priv(dev);

	/* Allocate the send/receive buffers */
	private->rx_buffer = pci_alloc_consistent(pdev,8192,&private->rx_dma_handle);
	if (private->rx_buffer == NULL) {
		pr_err("%s: no memory for rx buffer\n", __func__);
		goto rx_buf_fail;
	}
	private->tx_buffer = pci_alloc_consistent(pdev,8192,&private->tx_dma_handle);
	if (private->tx_buffer == NULL) {
		pr_err("%s: no memory for tx buffer\n", __func__);
		goto tx_buf_fail;
	}

	SET_NETDEV_DEV(dev, &pdev->dev);


	private->dev = dev;
	private->pdev = pdev;
	private->io_port = pci_resource_start(pdev, 0);
	spin_lock_init(&private->lock);
	dev->irq = pdev->irq;
	dev->base_addr = private->io_port;

	initialize_card(private);
	read_mac_address(private);
	setup_descriptors(private);

	dev->netdev_ops = &netdev_ops;
	SET_ETHTOOL_OPS(dev, &netdev_ethtool_ops);
	pci_set_drvdata(pdev, dev);

	if (register_netdev(dev)) {
		pr_err("%s: netdevice registration failed\n", __func__);
		goto reg_fail;
	}

	dev_info(&dev->dev, "Xircom cardbus revision %i at irq %i\n",
		 pdev->revision, pdev->irq);
	/* start the transmitter to get a heartbeat */
	/* TODO: send 2 dummy packets here */
	transceiver_voodoo(private);

	spin_lock_irqsave(&private->lock,flags);
	activate_transmitter(private);
	activate_receiver(private);
	spin_unlock_irqrestore(&private->lock,flags);

	trigger_receive(private);

	leave("xircom_probe");
	return 0;

reg_fail:
	kfree(private->tx_buffer);
tx_buf_fail:
	kfree(private->rx_buffer);
rx_buf_fail:
	free_netdev(dev);
device_fail:
	return -ENODEV;
}


/*
 xircom_remove is called on module-unload or on device-eject.
 it unregisters the irq, io-region and network device.
 Interrupts and such are already stopped in the "ifconfig ethX down"
 code.
 */
static void __devexit xircom_remove(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct xircom_private *card = netdev_priv(dev);

	enter("xircom_remove");
	pci_free_consistent(pdev,8192,card->rx_buffer,card->rx_dma_handle);
	pci_free_consistent(pdev,8192,card->tx_buffer,card->tx_dma_handle);

	release_region(dev->base_addr, 128);
	unregister_netdev(dev);
	free_netdev(dev);
	pci_set_drvdata(pdev, NULL);
	leave("xircom_remove");
}

static irqreturn_t xircom_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct xircom_private *card = netdev_priv(dev);
	unsigned int status;
	int i;

	enter("xircom_interrupt\n");

	spin_lock(&card->lock);
	status = inl(card->io_port+CSR5);

#ifdef DEBUG
	print_binary(status);
	printk("tx status 0x%08x 0x%08x\n",
	       card->tx_buffer[0], card->tx_buffer[4]);
	printk("rx status 0x%08x 0x%08x\n",
	       card->rx_buffer[0], card->rx_buffer[4]);
#endif
	/* Handle shared irq and hotplug */
	if (status == 0 || status == 0xffffffff) {
		spin_unlock(&card->lock);
		return IRQ_NONE;
	}

	if (link_status_changed(card)) {
		int newlink;
		printk(KERN_DEBUG "xircom_cb: Link status has changed\n");
		newlink = link_status(card);
		dev_info(&dev->dev, "Link is %i mbit\n", newlink);
		if (newlink)
			netif_carrier_on(dev);
		else
			netif_carrier_off(dev);

	}

	/* Clear all remaining interrupts */
	status |= 0xffffffff; /* FIXME: make this clear only the
				        real existing bits */
	outl(status,card->io_port+CSR5);


	for (i=0;i<NUMDESCRIPTORS;i++)
		investigate_write_descriptor(dev,card,i,bufferoffsets[i]);
	for (i=0;i<NUMDESCRIPTORS;i++)
		investigate_read_descriptor(dev,card,i,bufferoffsets[i]);


	spin_unlock(&card->lock);
	leave("xircom_interrupt");
	return IRQ_HANDLED;
}

static netdev_tx_t xircom_start_xmit(struct sk_buff *skb,
					   struct net_device *dev)
{
	struct xircom_private *card;
	unsigned long flags;
	int nextdescriptor;
	int desc;
	enter("xircom_start_xmit");

	card = netdev_priv(dev);
	spin_lock_irqsave(&card->lock,flags);

	/* First see if we can free some descriptors */
	for (desc=0;desc<NUMDESCRIPTORS;desc++)
		investigate_write_descriptor(dev,card,desc,bufferoffsets[desc]);


	nextdescriptor = (card->transmit_used +1) % (NUMDESCRIPTORS);
	desc = card->transmit_used;

	/* only send the packet if the descriptor is free */
	if (card->tx_buffer[4*desc]==0) {
			/* Copy the packet data; zero the memory first as the card
			   sometimes sends more than you ask it to. */

			memset(&card->tx_buffer[bufferoffsets[desc]/4],0,1536);
			skb_copy_from_linear_data(skb,
				  &(card->tx_buffer[bufferoffsets[desc] / 4]),
						  skb->len);
			/* FIXME: The specification tells us that the length we send HAS to be a multiple of
			   4 bytes. */

			card->tx_buffer[4*desc+1] = cpu_to_le32(skb->len);
			if (desc == NUMDESCRIPTORS - 1) /* bit 25: last descriptor of the ring */
				card->tx_buffer[4*desc+1] |= cpu_to_le32(1<<25);  

			card->tx_buffer[4*desc+1] |= cpu_to_le32(0xF0000000);
						 /* 0xF0... means want interrupts*/
			card->tx_skb[desc] = skb;

			wmb();
			/* This gives the descriptor to the card */
			card->tx_buffer[4*desc] = cpu_to_le32(0x80000000);
			trigger_transmit(card);
			if (card->tx_buffer[nextdescriptor*4] & cpu_to_le32(0x8000000)) {
				/* next descriptor is occupied... */
				netif_stop_queue(dev);
			}
			card->transmit_used = nextdescriptor;
			leave("xircom-start_xmit - sent");
			spin_unlock_irqrestore(&card->lock,flags);
			return NETDEV_TX_OK;
	}



	/* Uh oh... no free descriptor... drop the packet */
	netif_stop_queue(dev);
	spin_unlock_irqrestore(&card->lock,flags);
	trigger_transmit(card);

	return NETDEV_TX_BUSY;
}




static int xircom_open(struct net_device *dev)
{
	struct xircom_private *xp = netdev_priv(dev);
	int retval;
	enter("xircom_open");
	pr_info("xircom cardbus adaptor found, registering as %s, using irq %i\n",
		dev->name, dev->irq);
	retval = request_irq(dev->irq, xircom_interrupt, IRQF_SHARED, dev->name, dev);
	if (retval) {
		leave("xircom_open - No IRQ");
		return retval;
	}

	xircom_up(xp);
	xp->open = 1;
	leave("xircom_open");
	return 0;
}

static int xircom_close(struct net_device *dev)
{
	struct xircom_private *card;
	unsigned long flags;

	enter("xircom_close");
	card = netdev_priv(dev);
	netif_stop_queue(dev); /* we don't want new packets */


	spin_lock_irqsave(&card->lock,flags);

	disable_all_interrupts(card);
#if 0
	/* We can enable this again once we send dummy packets on ifconfig ethX up */
	deactivate_receiver(card);
	deactivate_transmitter(card);
#endif
	remove_descriptors(card);

	spin_unlock_irqrestore(&card->lock,flags);

	card->open = 0;
	free_irq(dev->irq,dev);

	leave("xircom_close");

	return 0;

}


#ifdef CONFIG_NET_POLL_CONTROLLER
static void xircom_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	xircom_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif


static void initialize_card(struct xircom_private *card)
{
	unsigned int val;
	unsigned long flags;
	enter("initialize_card");


	spin_lock_irqsave(&card->lock, flags);

	/* First: reset the card */
	val = inl(card->io_port + CSR0);
	val |= 0x01;		/* Software reset */
	outl(val, card->io_port + CSR0);

	udelay(100);		/* give the card some time to reset */

	val = inl(card->io_port + CSR0);
	val &= ~0x01;		/* disable Software reset */
	outl(val, card->io_port + CSR0);


	val = 0;		/* Value 0x00 is a safe and conservative value
				   for the PCI configuration settings */
	outl(val, card->io_port + CSR0);


	disable_all_interrupts(card);
	deactivate_receiver(card);
	deactivate_transmitter(card);

	spin_unlock_irqrestore(&card->lock, flags);

	leave("initialize_card");
}

/*
trigger_transmit causes the card to check for frames to be transmitted.
This is accomplished by writing to the CSR1 port. The documentation
claims that the act of writing is sufficient and that the value is
ignored; I chose zero.
*/
static void trigger_transmit(struct xircom_private *card)
{
	unsigned int val;
	enter("trigger_transmit");

	val = 0;
	outl(val, card->io_port + CSR1);

	leave("trigger_transmit");
}

/*
trigger_receive causes the card to check for empty frames in the
descriptor list in which packets can be received.
This is accomplished by writing to the CSR2 port. The documentation
claims that the act of writing is sufficient and that the value is
ignored; I chose zero.
*/
static void trigger_receive(struct xircom_private *card)
{
	unsigned int val;
	enter("trigger_receive");

	val = 0;
	outl(val, card->io_port + CSR2);

	leave("trigger_receive");
}

/*
setup_descriptors initializes the send and receive buffers to be valid
descriptors and programs the addresses into the card.
*/
static void setup_descriptors(struct xircom_private *card)
{
	u32 address;
	int i;
	enter("setup_descriptors");


	BUG_ON(card->rx_buffer == NULL);
	BUG_ON(card->tx_buffer == NULL);

	/* Receive descriptors */
	memset(card->rx_buffer, 0, 128);	/* clear the descriptors */
	for (i=0;i<NUMDESCRIPTORS;i++ ) {

		/* Rx Descr0: It's empty, let the card own it, no errors -> 0x80000000 */
		card->rx_buffer[i*4 + 0] = cpu_to_le32(0x80000000);
		/* Rx Descr1: buffer 1 is 1536 bytes, buffer 2 is 0 bytes */
		card->rx_buffer[i*4 + 1] = cpu_to_le32(1536);
		if (i == NUMDESCRIPTORS - 1) /* bit 25 is "last descriptor" */
			card->rx_buffer[i*4 + 1] |= cpu_to_le32(1 << 25);

		/* Rx Descr2: address of the buffer
		   we store the buffer at the 2nd half of the page */

		address = card->rx_dma_handle;
		card->rx_buffer[i*4 + 2] = cpu_to_le32(address + bufferoffsets[i]);
		/* Rx Desc3: address of 2nd buffer -> 0 */
		card->rx_buffer[i*4 + 3] = 0;
	}

	wmb();
	/* Write the receive descriptor ring address to the card */
	address = card->rx_dma_handle;
	outl(address, card->io_port + CSR3);	/* Receive descr list address */


	/* transmit descriptors */
	memset(card->tx_buffer, 0, 128);	/* clear the descriptors */

	for (i=0;i<NUMDESCRIPTORS;i++ ) {
		/* Tx Descr0: Empty, we own it, no errors -> 0x00000000 */
		card->tx_buffer[i*4 + 0] = 0x00000000;
		/* Tx Descr1: buffer 1 is 1536 bytes, buffer 2 is 0 bytes */
		card->tx_buffer[i*4 + 1] = cpu_to_le32(1536);
		if (i == NUMDESCRIPTORS - 1) /* bit 25 is "last descriptor" */
			card->tx_buffer[i*4 + 1] |= cpu_to_le32(1 << 25);

		/* Tx Descr2: address of the buffer
		   we store the buffer at the 2nd half of the page */
		address = card->tx_dma_handle;
		card->tx_buffer[i*4 + 2] = cpu_to_le32(address + bufferoffsets[i]);
		/* Tx Desc3: address of 2nd buffer -> 0 */
		card->tx_buffer[i*4 + 3] = 0;
	}

	wmb();
	/* wite the transmit descriptor ring to the card */
	address = card->tx_dma_handle;
	outl(address, card->io_port + CSR4);	/* xmit descr list address */

	leave("setup_descriptors");
}

/*
remove_descriptors informs the card the descriptors are no longer
valid by setting the address in the card to 0x00.
*/
static void remove_descriptors(struct xircom_private *card)
{
	unsigned int val;
	enter("remove_descriptors");

	val = 0;
	outl(val, card->io_port + CSR3);	/* Receive descriptor address */
	outl(val, card->io_port + CSR4);	/* Send descriptor address */

	leave("remove_descriptors");
}

/*
link_status_changed returns 1 if the card has indicated that
the link status has changed. The new link status has to be read from CSR12.

This function also clears the status-bit.
*/
static int link_status_changed(struct xircom_private *card)
{
	unsigned int val;
	enter("link_status_changed");

	val = inl(card->io_port + CSR5);	/* Status register */

	if ((val & (1 << 27)) == 0) {	/* no change */
		leave("link_status_changed - nochange");
		return 0;
	}

	/* clear the event by writing a 1 to the bit in the
	   status register. */
	val = (1 << 27);
	outl(val, card->io_port + CSR5);

	leave("link_status_changed - changed");
	return 1;
}


/*
transmit_active returns 1 if the transmitter on the card is
in a non-stopped state.
*/
static int transmit_active(struct xircom_private *card)
{
	unsigned int val;
	enter("transmit_active");

	val = inl(card->io_port + CSR5);	/* Status register */

	if ((val & (7 << 20)) == 0) {	/* transmitter disabled */
		leave("transmit_active - inactive");
		return 0;
	}

	leave("transmit_active - active");
	return 1;
}

/*
receive_active returns 1 if the receiver on the card is
in a non-stopped state.
*/
static int receive_active(struct xircom_private *card)
{
	unsigned int val;
	enter("receive_active");


	val = inl(card->io_port + CSR5);	/* Status register */

	if ((val & (7 << 17)) == 0) {	/* receiver disabled */
		leave("receive_active - inactive");
		return 0;
	}

	leave("receive_active - active");
	return 1;
}

/*
activate_receiver enables the receiver on the card.
Before being allowed to active the receiver, the receiver
must be completely de-activated. To achieve this,
this code actually disables the receiver first; then it waits for the
receiver to become inactive, then it activates the receiver and then
it waits for the receiver to be active.

must be called with the lock held and interrupts disabled.
*/
static void activate_receiver(struct xircom_private *card)
{
	unsigned int val;
	int counter;
	enter("activate_receiver");


	val = inl(card->io_port + CSR6);	/* Operation mode */

	/* If the "active" bit is set and the receiver is already
	   active, no need to do the expensive thing */
	if ((val&2) && (receive_active(card)))
		return;


	val = val & ~2;		/* disable the receiver */
	outl(val, card->io_port + CSR6);

	counter = 10;
	while (counter > 0) {
		if (!receive_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			pr_err("Receiver failed to deactivate\n");
	}

	/* enable the receiver */
	val = inl(card->io_port + CSR6);	/* Operation mode */
	val = val | 2;				/* enable the receiver */
	outl(val, card->io_port + CSR6);

	/* now wait for the card to activate again */
	counter = 10;
	while (counter > 0) {
		if (receive_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			pr_err("Receiver failed to re-activate\n");
	}

	leave("activate_receiver");
}

/*
deactivate_receiver disables the receiver on the card.
To achieve this this code disables the receiver first;
then it waits for the receiver to become inactive.

must be called with the lock held and interrupts disabled.
*/
static void deactivate_receiver(struct xircom_private *card)
{
	unsigned int val;
	int counter;
	enter("deactivate_receiver");

	val = inl(card->io_port + CSR6);	/* Operation mode */
	val = val & ~2;				/* disable the receiver */
	outl(val, card->io_port + CSR6);

	counter = 10;
	while (counter > 0) {
		if (!receive_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			pr_err("Receiver failed to deactivate\n");
	}


	leave("deactivate_receiver");
}


/*
activate_transmitter enables the transmitter on the card.
Before being allowed to active the transmitter, the transmitter
must be completely de-activated. To achieve this,
this code actually disables the transmitter first; then it waits for the
transmitter to become inactive, then it activates the transmitter and then
it waits for the transmitter to be active again.

must be called with the lock held and interrupts disabled.
*/
static void activate_transmitter(struct xircom_private *card)
{
	unsigned int val;
	int counter;
	enter("activate_transmitter");


	val = inl(card->io_port + CSR6);	/* Operation mode */

	/* If the "active" bit is set and the receiver is already
	   active, no need to do the expensive thing */
	if ((val&(1<<13)) && (transmit_active(card)))
		return;

	val = val & ~(1 << 13);	/* disable the transmitter */
	outl(val, card->io_port + CSR6);

	counter = 10;
	while (counter > 0) {
		if (!transmit_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			pr_err("Transmitter failed to deactivate\n");
	}

	/* enable the transmitter */
	val = inl(card->io_port + CSR6);	/* Operation mode */
	val = val | (1 << 13);	/* enable the transmitter */
	outl(val, card->io_port + CSR6);

	/* now wait for the card to activate again */
	counter = 10;
	while (counter > 0) {
		if (transmit_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			pr_err("Transmitter failed to re-activate\n");
	}

	leave("activate_transmitter");
}

/*
deactivate_transmitter disables the transmitter on the card.
To achieve this this code disables the transmitter first;
then it waits for the transmitter to become inactive.

must be called with the lock held and interrupts disabled.
*/
static void deactivate_transmitter(struct xircom_private *card)
{
	unsigned int val;
	int counter;
	enter("deactivate_transmitter");

	val = inl(card->io_port + CSR6);	/* Operation mode */
	val = val & ~2;		/* disable the transmitter */
	outl(val, card->io_port + CSR6);

	counter = 20;
	while (counter > 0) {
		if (!transmit_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			pr_err("Transmitter failed to deactivate\n");
	}


	leave("deactivate_transmitter");
}


/*
enable_transmit_interrupt enables the transmit interrupt

must be called with the lock held and interrupts disabled.
*/
static void enable_transmit_interrupt(struct xircom_private *card)
{
	unsigned int val;
	enter("enable_transmit_interrupt");

	val = inl(card->io_port + CSR7);	/* Interrupt enable register */
	val |= 1;				/* enable the transmit interrupt */
	outl(val, card->io_port + CSR7);

	leave("enable_transmit_interrupt");
}


/*
enable_receive_interrupt enables the receive interrupt

must be called with the lock held and interrupts disabled.
*/
static void enable_receive_interrupt(struct xircom_private *card)
{
	unsigned int val;
	enter("enable_receive_interrupt");

	val = inl(card->io_port + CSR7);	/* Interrupt enable register */
	val = val | (1 << 6);			/* enable the receive interrupt */
	outl(val, card->io_port + CSR7);

	leave("enable_receive_interrupt");
}

/*
enable_link_interrupt enables the link status change interrupt

must be called with the lock held and interrupts disabled.
*/
static void enable_link_interrupt(struct xircom_private *card)
{
	unsigned int val;
	enter("enable_link_interrupt");

	val = inl(card->io_port + CSR7);	/* Interrupt enable register */
	val = val | (1 << 27);			/* enable the link status chage interrupt */
	outl(val, card->io_port + CSR7);

	leave("enable_link_interrupt");
}



/*
disable_all_interrupts disables all interrupts

must be called with the lock held and interrupts disabled.
*/
static void disable_all_interrupts(struct xircom_private *card)
{
	unsigned int val;
	enter("enable_all_interrupts");

	val = 0;				/* disable all interrupts */
	outl(val, card->io_port + CSR7);

	leave("disable_all_interrupts");
}

/*
enable_common_interrupts enables several weird interrupts

must be called with the lock held and interrupts disabled.
*/
static void enable_common_interrupts(struct xircom_private *card)
{
	unsigned int val;
	enter("enable_link_interrupt");

	val = inl(card->io_port + CSR7);	/* Interrupt enable register */
	val |= (1<<16); /* Normal Interrupt Summary */
	val |= (1<<15); /* Abnormal Interrupt Summary */
	val |= (1<<13); /* Fatal bus error */
	val |= (1<<8);  /* Receive Process Stopped */
	val |= (1<<7);  /* Receive Buffer Unavailable */
	val |= (1<<5);  /* Transmit Underflow */
	val |= (1<<2);  /* Transmit Buffer Unavailable */
	val |= (1<<1);  /* Transmit Process Stopped */
	outl(val, card->io_port + CSR7);

	leave("enable_link_interrupt");
}

/*
enable_promisc starts promisc mode

must be called with the lock held and interrupts disabled.
*/
static int enable_promisc(struct xircom_private *card)
{
	unsigned int val;
	enter("enable_promisc");

	val = inl(card->io_port + CSR6);
	val = val | (1 << 6);
	outl(val, card->io_port + CSR6);

	leave("enable_promisc");
	return 1;
}




/*
link_status() checks the links status and will return 0 for no link, 10 for 10mbit link and 100 for.. guess what.

Must be called in locked state with interrupts disabled
*/
static int link_status(struct xircom_private *card)
{
	unsigned int val;
	enter("link_status");

	val = inb(card->io_port + CSR12);

	if (!(val&(1<<2)))  /* bit 2 is 0 for 10mbit link, 1 for not an 10mbit link */
		return 10;
	if (!(val&(1<<1)))  /* bit 1 is 0 for 100mbit link, 1 for not an 100mbit link */
		return 100;

	/* If we get here -> no link at all */

	leave("link_status");
	return 0;
}





/*
  read_mac_address() reads the MAC address from the NIC and stores it in the "dev" structure.

  This function will take the spinlock itself and can, as a result, not be called with the lock helt.
 */
static void read_mac_address(struct xircom_private *card)
{
	unsigned char j, tuple, link, data_id, data_count;
	unsigned long flags;
	int i;

	enter("read_mac_address");

	spin_lock_irqsave(&card->lock, flags);

	outl(1 << 12, card->io_port + CSR9);	/* enable boot rom access */
	for (i = 0x100; i < 0x1f7; i += link + 2) {
		outl(i, card->io_port + CSR10);
		tuple = inl(card->io_port + CSR9) & 0xff;
		outl(i + 1, card->io_port + CSR10);
		link = inl(card->io_port + CSR9) & 0xff;
		outl(i + 2, card->io_port + CSR10);
		data_id = inl(card->io_port + CSR9) & 0xff;
		outl(i + 3, card->io_port + CSR10);
		data_count = inl(card->io_port + CSR9) & 0xff;
		if ((tuple == 0x22) && (data_id == 0x04) && (data_count == 0x06)) {
			/*
			 * This is it.  We have the data we want.
			 */
			for (j = 0; j < 6; j++) {
				outl(i + j + 4, card->io_port + CSR10);
				card->dev->dev_addr[j] = inl(card->io_port + CSR9) & 0xff;
			}
			break;
		} else if (link == 0) {
			break;
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);
	pr_debug(" %pM\n", card->dev->dev_addr);
	leave("read_mac_address");
}


/*
 transceiver_voodoo() enables the external UTP plug thingy.
 it's called voodoo as I stole this code and cannot cross-reference
 it with the specification.
 */
static void transceiver_voodoo(struct xircom_private *card)
{
	unsigned long flags;

	enter("transceiver_voodoo");

	/* disable all powermanagement */
	pci_write_config_dword(card->pdev, PCI_POWERMGMT, 0x0000);

	setup_descriptors(card);

	spin_lock_irqsave(&card->lock, flags);

	outl(0x0008, card->io_port + CSR15);
        udelay(25);
        outl(0xa8050000, card->io_port + CSR15);
        udelay(25);
        outl(0xa00f0000, card->io_port + CSR15);
        udelay(25);

        spin_unlock_irqrestore(&card->lock, flags);

	netif_start_queue(card->dev);
	leave("transceiver_voodoo");
}


static void xircom_up(struct xircom_private *card)
{
	unsigned long flags;
	int i;

	enter("xircom_up");

	/* disable all powermanagement */
	pci_write_config_dword(card->pdev, PCI_POWERMGMT, 0x0000);

	setup_descriptors(card);

	spin_lock_irqsave(&card->lock, flags);


	enable_link_interrupt(card);
	enable_transmit_interrupt(card);
	enable_receive_interrupt(card);
	enable_common_interrupts(card);
	enable_promisc(card);

	/* The card can have received packets already, read them away now */
	for (i=0;i<NUMDESCRIPTORS;i++)
		investigate_read_descriptor(card->dev,card,i,bufferoffsets[i]);


	spin_unlock_irqrestore(&card->lock, flags);
	trigger_receive(card);
	trigger_transmit(card);
	netif_start_queue(card->dev);
	leave("xircom_up");
}

/* Bufferoffset is in BYTES */
static void investigate_read_descriptor(struct net_device *dev,struct xircom_private *card, int descnr, unsigned int bufferoffset)
{
		int status;

		enter("investigate_read_descriptor");
		status = le32_to_cpu(card->rx_buffer[4*descnr]);

		if ((status > 0)) {	/* packet received */

			/* TODO: discard error packets */

			short pkt_len = ((status >> 16) & 0x7ff) - 4;	/* minus 4, we don't want the CRC */
			struct sk_buff *skb;

			if (pkt_len > 1518) {
				pr_err("Packet length %i is bogus\n", pkt_len);
				pkt_len = 1518;
			}

			skb = dev_alloc_skb(pkt_len + 2);
			if (skb == NULL) {
				dev->stats.rx_dropped++;
				goto out;
			}
			skb_reserve(skb, 2);
			skb_copy_to_linear_data(skb, (unsigned char*)&card->rx_buffer[bufferoffset / 4], pkt_len);
			skb_put(skb, pkt_len);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->stats.rx_packets++;
			dev->stats.rx_bytes += pkt_len;

		      out:
			/* give the buffer back to the card */
			card->rx_buffer[4*descnr] =  cpu_to_le32(0x80000000);
			trigger_receive(card);
		}

		leave("investigate_read_descriptor");

}


/* Bufferoffset is in BYTES */
static void investigate_write_descriptor(struct net_device *dev, struct xircom_private *card, int descnr, unsigned int bufferoffset)
{
		int status;

		enter("investigate_write_descriptor");

		status = le32_to_cpu(card->tx_buffer[4*descnr]);
#if 0
		if (status & 0x8000) {	/* Major error */
			pr_err("Major transmit error status %x\n", status);
			card->tx_buffer[4*descnr] = 0;
			netif_wake_queue (dev);
		}
#endif
		if (status > 0) {	/* bit 31 is 0 when done */
			if (card->tx_skb[descnr]!=NULL) {
				dev->stats.tx_bytes += card->tx_skb[descnr]->len;
				dev_kfree_skb_irq(card->tx_skb[descnr]);
			}
			card->tx_skb[descnr] = NULL;
			/* Bit 8 in the status field is 1 if there was a collision */
			if (status&(1<<8))
				dev->stats.collisions++;
			card->tx_buffer[4*descnr] = 0; /* descriptor is free again */
			netif_wake_queue (dev);
			dev->stats.tx_packets++;
		}

		leave("investigate_write_descriptor");

}


static int __init xircom_init(void)
{
	return pci_register_driver(&xircom_ops);
}

static void __exit xircom_exit(void)
{
	pci_unregister_driver(&xircom_ops);
}

module_init(xircom_init)
module_exit(xircom_exit)

