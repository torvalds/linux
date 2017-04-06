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
#include <linux/bitops.h>

#include <linux/uaccess.h>
#include <asm/io.h>
#ifdef CONFIG_NET_POLL_CONTROLLER
#include <asm/irq.h>
#endif

MODULE_DESCRIPTION("Xircom Cardbus ethernet driver");
MODULE_AUTHOR("Arjan van de Ven <arjanv@redhat.com>");
MODULE_LICENSE("GPL");

#define xw32(reg, val)	iowrite32(val, ioaddr + (reg))
#define xr32(reg)	ioread32(ioaddr + (reg))
#define xr8(reg)	ioread8(ioaddr + (reg))

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

	void __iomem *ioaddr;
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



static const struct pci_device_id xircom_pci_table[] = {
	{ PCI_VDEVICE(XIRCOM, 0x0003), },
	{0,},
};
MODULE_DEVICE_TABLE(pci, xircom_pci_table);

static struct pci_driver xircom_ops = {
	.name		= "xircom_cb",
	.id_table	= xircom_pci_table,
	.probe		= xircom_probe,
	.remove		= xircom_remove,
};


#if defined DEBUG && DEBUG > 1
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
	pr_debug("%s\n",buffer);
}
#endif

static const struct net_device_ops netdev_ops = {
	.ndo_open		= xircom_open,
	.ndo_stop		= xircom_close,
	.ndo_start_xmit		= xircom_start_xmit,
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
static int xircom_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *d = &pdev->dev;
	struct net_device *dev = NULL;
	struct xircom_private *private;
	unsigned long flags;
	unsigned short tmp16;
	int rc;

	/* First do the PCI initialisation */

	rc = pci_enable_device(pdev);
	if (rc < 0)
		goto out;

	/* disable all powermanagement */
	pci_write_config_dword(pdev, PCI_POWERMGMT, 0x0000);

	pci_set_master(pdev); /* Why isn't this done by pci_enable_device ?*/

	/* clear PCI status, if any */
	pci_read_config_word (pdev,PCI_STATUS, &tmp16);
	pci_write_config_word (pdev, PCI_STATUS,tmp16);

	rc = pci_request_regions(pdev, "xircom_cb");
	if (rc < 0) {
		pr_err("%s: failed to allocate io-region\n", __func__);
		goto err_disable;
	}

	rc = -ENOMEM;
	/*
	   Before changing the hardware, allocate the memory.
	   This way, we can fail gracefully if not enough memory
	   is available.
	 */
	dev = alloc_etherdev(sizeof(struct xircom_private));
	if (!dev)
		goto err_release;

	private = netdev_priv(dev);

	/* Allocate the send/receive buffers */
	private->rx_buffer = dma_alloc_coherent(d, 8192,
						&private->rx_dma_handle,
						GFP_KERNEL);
	if (private->rx_buffer == NULL)
		goto rx_buf_fail;

	private->tx_buffer = dma_alloc_coherent(d, 8192,
						&private->tx_dma_handle,
						GFP_KERNEL);
	if (private->tx_buffer == NULL)
		goto tx_buf_fail;

	SET_NETDEV_DEV(dev, &pdev->dev);


	private->dev = dev;
	private->pdev = pdev;

	/* IO range. */
	private->ioaddr = pci_iomap(pdev, 0, 0);
	if (!private->ioaddr)
		goto reg_fail;

	spin_lock_init(&private->lock);

	initialize_card(private);
	read_mac_address(private);
	setup_descriptors(private);

	dev->netdev_ops = &netdev_ops;
	pci_set_drvdata(pdev, dev);

	rc = register_netdev(dev);
	if (rc < 0) {
		pr_err("%s: netdevice registration failed\n", __func__);
		goto err_unmap;
	}

	netdev_info(dev, "Xircom cardbus revision %i at irq %i\n",
		    pdev->revision, pdev->irq);
	/* start the transmitter to get a heartbeat */
	/* TODO: send 2 dummy packets here */
	transceiver_voodoo(private);

	spin_lock_irqsave(&private->lock,flags);
	activate_transmitter(private);
	activate_receiver(private);
	spin_unlock_irqrestore(&private->lock,flags);

	trigger_receive(private);
out:
	return rc;

err_unmap:
	pci_iounmap(pdev, private->ioaddr);
reg_fail:
	dma_free_coherent(d, 8192, private->tx_buffer, private->tx_dma_handle);
tx_buf_fail:
	dma_free_coherent(d, 8192, private->rx_buffer, private->rx_dma_handle);
rx_buf_fail:
	free_netdev(dev);
err_release:
	pci_release_regions(pdev);
err_disable:
	pci_disable_device(pdev);
	goto out;
}


/*
 xircom_remove is called on module-unload or on device-eject.
 it unregisters the irq, io-region and network device.
 Interrupts and such are already stopped in the "ifconfig ethX down"
 code.
 */
static void xircom_remove(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct xircom_private *card = netdev_priv(dev);
	struct device *d = &pdev->dev;

	unregister_netdev(dev);
	pci_iounmap(pdev, card->ioaddr);
	dma_free_coherent(d, 8192, card->tx_buffer, card->tx_dma_handle);
	dma_free_coherent(d, 8192, card->rx_buffer, card->rx_dma_handle);
	free_netdev(dev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static irqreturn_t xircom_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct xircom_private *card = netdev_priv(dev);
	void __iomem *ioaddr = card->ioaddr;
	unsigned int status;
	int i;

	spin_lock(&card->lock);
	status = xr32(CSR5);

#if defined DEBUG && DEBUG > 1
	print_binary(status);
	pr_debug("tx status 0x%08x 0x%08x\n",
		 card->tx_buffer[0], card->tx_buffer[4]);
	pr_debug("rx status 0x%08x 0x%08x\n",
		 card->rx_buffer[0], card->rx_buffer[4]);
#endif
	/* Handle shared irq and hotplug */
	if (status == 0 || status == 0xffffffff) {
		spin_unlock(&card->lock);
		return IRQ_NONE;
	}

	if (link_status_changed(card)) {
		int newlink;
		netdev_dbg(dev, "Link status has changed\n");
		newlink = link_status(card);
		netdev_info(dev, "Link is %d mbit\n", newlink);
		if (newlink)
			netif_carrier_on(dev);
		else
			netif_carrier_off(dev);

	}

	/* Clear all remaining interrupts */
	status |= 0xffffffff; /* FIXME: make this clear only the
				        real existing bits */
	xw32(CSR5, status);


	for (i=0;i<NUMDESCRIPTORS;i++)
		investigate_write_descriptor(dev,card,i,bufferoffsets[i]);
	for (i=0;i<NUMDESCRIPTORS;i++)
		investigate_read_descriptor(dev,card,i,bufferoffsets[i]);

	spin_unlock(&card->lock);
	return IRQ_HANDLED;
}

static netdev_tx_t xircom_start_xmit(struct sk_buff *skb,
					   struct net_device *dev)
{
	struct xircom_private *card;
	unsigned long flags;
	int nextdescriptor;
	int desc;

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
	const int irq = xp->pdev->irq;
	int retval;

	netdev_info(dev, "xircom cardbus adaptor found, using irq %i\n", irq);
	retval = request_irq(irq, xircom_interrupt, IRQF_SHARED, dev->name, dev);
	if (retval)
		return retval;

	xircom_up(xp);
	xp->open = 1;

	return 0;
}

static int xircom_close(struct net_device *dev)
{
	struct xircom_private *card;
	unsigned long flags;

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
	free_irq(card->pdev->irq, dev);

	return 0;

}


#ifdef CONFIG_NET_POLL_CONTROLLER
static void xircom_poll_controller(struct net_device *dev)
{
	struct xircom_private *xp = netdev_priv(dev);
	const int irq = xp->pdev->irq;

	disable_irq(irq);
	xircom_interrupt(irq, dev);
	enable_irq(irq);
}
#endif


static void initialize_card(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&card->lock, flags);

	/* First: reset the card */
	val = xr32(CSR0);
	val |= 0x01;		/* Software reset */
	xw32(CSR0, val);

	udelay(100);		/* give the card some time to reset */

	val = xr32(CSR0);
	val &= ~0x01;		/* disable Software reset */
	xw32(CSR0, val);


	val = 0;		/* Value 0x00 is a safe and conservative value
				   for the PCI configuration settings */
	xw32(CSR0, val);


	disable_all_interrupts(card);
	deactivate_receiver(card);
	deactivate_transmitter(card);

	spin_unlock_irqrestore(&card->lock, flags);
}

/*
trigger_transmit causes the card to check for frames to be transmitted.
This is accomplished by writing to the CSR1 port. The documentation
claims that the act of writing is sufficient and that the value is
ignored; I chose zero.
*/
static void trigger_transmit(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;

	xw32(CSR1, 0);
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
	void __iomem *ioaddr = card->ioaddr;

	xw32(CSR2, 0);
}

/*
setup_descriptors initializes the send and receive buffers to be valid
descriptors and programs the addresses into the card.
*/
static void setup_descriptors(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	u32 address;
	int i;

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
	xw32(CSR3, address);	/* Receive descr list address */


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
	xw32(CSR4, address);	/* xmit descr list address */
}

/*
remove_descriptors informs the card the descriptors are no longer
valid by setting the address in the card to 0x00.
*/
static void remove_descriptors(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	unsigned int val;

	val = 0;
	xw32(CSR3, val);	/* Receive descriptor address */
	xw32(CSR4, val);	/* Send descriptor address */
}

/*
link_status_changed returns 1 if the card has indicated that
the link status has changed. The new link status has to be read from CSR12.

This function also clears the status-bit.
*/
static int link_status_changed(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	unsigned int val;

	val = xr32(CSR5);	/* Status register */
	if (!(val & (1 << 27)))	/* no change */
		return 0;

	/* clear the event by writing a 1 to the bit in the
	   status register. */
	val = (1 << 27);
	xw32(CSR5, val);

	return 1;
}


/*
transmit_active returns 1 if the transmitter on the card is
in a non-stopped state.
*/
static int transmit_active(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;

	if (!(xr32(CSR5) & (7 << 20)))	/* transmitter disabled */
		return 0;

	return 1;
}

/*
receive_active returns 1 if the receiver on the card is
in a non-stopped state.
*/
static int receive_active(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;

	if (!(xr32(CSR5) & (7 << 17)))	/* receiver disabled */
		return 0;

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
	void __iomem *ioaddr = card->ioaddr;
	unsigned int val;
	int counter;

	val = xr32(CSR6);	/* Operation mode */

	/* If the "active" bit is set and the receiver is already
	   active, no need to do the expensive thing */
	if ((val&2) && (receive_active(card)))
		return;


	val = val & ~2;		/* disable the receiver */
	xw32(CSR6, val);

	counter = 10;
	while (counter > 0) {
		if (!receive_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			netdev_err(card->dev, "Receiver failed to deactivate\n");
	}

	/* enable the receiver */
	val = xr32(CSR6);	/* Operation mode */
	val = val | 2;		/* enable the receiver */
	xw32(CSR6, val);

	/* now wait for the card to activate again */
	counter = 10;
	while (counter > 0) {
		if (receive_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			netdev_err(card->dev,
				   "Receiver failed to re-activate\n");
	}
}

/*
deactivate_receiver disables the receiver on the card.
To achieve this this code disables the receiver first;
then it waits for the receiver to become inactive.

must be called with the lock held and interrupts disabled.
*/
static void deactivate_receiver(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	unsigned int val;
	int counter;

	val = xr32(CSR6);	/* Operation mode */
	val = val & ~2;		/* disable the receiver */
	xw32(CSR6, val);

	counter = 10;
	while (counter > 0) {
		if (!receive_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			netdev_err(card->dev, "Receiver failed to deactivate\n");
	}
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
	void __iomem *ioaddr = card->ioaddr;
	unsigned int val;
	int counter;

	val = xr32(CSR6);	/* Operation mode */

	/* If the "active" bit is set and the receiver is already
	   active, no need to do the expensive thing */
	if ((val&(1<<13)) && (transmit_active(card)))
		return;

	val = val & ~(1 << 13);	/* disable the transmitter */
	xw32(CSR6, val);

	counter = 10;
	while (counter > 0) {
		if (!transmit_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			netdev_err(card->dev,
				   "Transmitter failed to deactivate\n");
	}

	/* enable the transmitter */
	val = xr32(CSR6);	/* Operation mode */
	val = val | (1 << 13);	/* enable the transmitter */
	xw32(CSR6, val);

	/* now wait for the card to activate again */
	counter = 10;
	while (counter > 0) {
		if (transmit_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			netdev_err(card->dev,
				   "Transmitter failed to re-activate\n");
	}
}

/*
deactivate_transmitter disables the transmitter on the card.
To achieve this this code disables the transmitter first;
then it waits for the transmitter to become inactive.

must be called with the lock held and interrupts disabled.
*/
static void deactivate_transmitter(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	unsigned int val;
	int counter;

	val = xr32(CSR6);	/* Operation mode */
	val = val & ~2;		/* disable the transmitter */
	xw32(CSR6, val);

	counter = 20;
	while (counter > 0) {
		if (!transmit_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			netdev_err(card->dev,
				   "Transmitter failed to deactivate\n");
	}
}


/*
enable_transmit_interrupt enables the transmit interrupt

must be called with the lock held and interrupts disabled.
*/
static void enable_transmit_interrupt(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	unsigned int val;

	val = xr32(CSR7);	/* Interrupt enable register */
	val |= 1;		/* enable the transmit interrupt */
	xw32(CSR7, val);
}


/*
enable_receive_interrupt enables the receive interrupt

must be called with the lock held and interrupts disabled.
*/
static void enable_receive_interrupt(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	unsigned int val;

	val = xr32(CSR7);	/* Interrupt enable register */
	val = val | (1 << 6);	/* enable the receive interrupt */
	xw32(CSR7, val);
}

/*
enable_link_interrupt enables the link status change interrupt

must be called with the lock held and interrupts disabled.
*/
static void enable_link_interrupt(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	unsigned int val;

	val = xr32(CSR7);	/* Interrupt enable register */
	val = val | (1 << 27);	/* enable the link status chage interrupt */
	xw32(CSR7, val);
}



/*
disable_all_interrupts disables all interrupts

must be called with the lock held and interrupts disabled.
*/
static void disable_all_interrupts(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;

	xw32(CSR7, 0);
}

/*
enable_common_interrupts enables several weird interrupts

must be called with the lock held and interrupts disabled.
*/
static void enable_common_interrupts(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	unsigned int val;

	val = xr32(CSR7);	/* Interrupt enable register */
	val |= (1<<16); /* Normal Interrupt Summary */
	val |= (1<<15); /* Abnormal Interrupt Summary */
	val |= (1<<13); /* Fatal bus error */
	val |= (1<<8);  /* Receive Process Stopped */
	val |= (1<<7);  /* Receive Buffer Unavailable */
	val |= (1<<5);  /* Transmit Underflow */
	val |= (1<<2);  /* Transmit Buffer Unavailable */
	val |= (1<<1);  /* Transmit Process Stopped */
	xw32(CSR7, val);
}

/*
enable_promisc starts promisc mode

must be called with the lock held and interrupts disabled.
*/
static int enable_promisc(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	unsigned int val;

	val = xr32(CSR6);
	val = val | (1 << 6);
	xw32(CSR6, val);

	return 1;
}




/*
link_status() checks the links status and will return 0 for no link, 10 for 10mbit link and 100 for.. guess what.

Must be called in locked state with interrupts disabled
*/
static int link_status(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	u8 val;

	val = xr8(CSR12);

	/* bit 2 is 0 for 10mbit link, 1 for not an 10mbit link */
	if (!(val & (1 << 2)))
		return 10;
	/* bit 1 is 0 for 100mbit link, 1 for not an 100mbit link */
	if (!(val & (1 << 1)))
		return 100;

	/* If we get here -> no link at all */

	return 0;
}





/*
  read_mac_address() reads the MAC address from the NIC and stores it in the "dev" structure.

  This function will take the spinlock itself and can, as a result, not be called with the lock helt.
 */
static void read_mac_address(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	unsigned long flags;
	u8 link;
	int i;

	spin_lock_irqsave(&card->lock, flags);

	xw32(CSR9, 1 << 12);	/* enable boot rom access */
	for (i = 0x100; i < 0x1f7; i += link + 2) {
		u8 tuple, data_id, data_count;

		xw32(CSR10, i);
		tuple = xr32(CSR9);
		xw32(CSR10, i + 1);
		link = xr32(CSR9);
		xw32(CSR10, i + 2);
		data_id = xr32(CSR9);
		xw32(CSR10, i + 3);
		data_count = xr32(CSR9);
		if ((tuple == 0x22) && (data_id == 0x04) && (data_count == 0x06)) {
			int j;

			for (j = 0; j < 6; j++) {
				xw32(CSR10, i + j + 4);
				card->dev->dev_addr[j] = xr32(CSR9) & 0xff;
			}
			break;
		} else if (link == 0) {
			break;
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);
	pr_debug(" %pM\n", card->dev->dev_addr);
}


/*
 transceiver_voodoo() enables the external UTP plug thingy.
 it's called voodoo as I stole this code and cannot cross-reference
 it with the specification.
 */
static void transceiver_voodoo(struct xircom_private *card)
{
	void __iomem *ioaddr = card->ioaddr;
	unsigned long flags;

	/* disable all powermanagement */
	pci_write_config_dword(card->pdev, PCI_POWERMGMT, 0x0000);

	setup_descriptors(card);

	spin_lock_irqsave(&card->lock, flags);

	xw32(CSR15, 0x0008);
	udelay(25);
	xw32(CSR15, 0xa8050000);
	udelay(25);
	xw32(CSR15, 0xa00f0000);
	udelay(25);

	spin_unlock_irqrestore(&card->lock, flags);

	netif_start_queue(card->dev);
}


static void xircom_up(struct xircom_private *card)
{
	unsigned long flags;
	int i;

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
}

/* Bufferoffset is in BYTES */
static void
investigate_read_descriptor(struct net_device *dev, struct xircom_private *card,
			    int descnr, unsigned int bufferoffset)
{
	int status;

	status = le32_to_cpu(card->rx_buffer[4*descnr]);

	if (status > 0) {		/* packet received */

		/* TODO: discard error packets */

		short pkt_len = ((status >> 16) & 0x7ff) - 4;
					/* minus 4, we don't want the CRC */
		struct sk_buff *skb;

		if (pkt_len > 1518) {
			netdev_err(dev, "Packet length %i is bogus\n", pkt_len);
			pkt_len = 1518;
		}

		skb = netdev_alloc_skb(dev, pkt_len + 2);
		if (skb == NULL) {
			dev->stats.rx_dropped++;
			goto out;
		}
		skb_reserve(skb, 2);
		skb_copy_to_linear_data(skb,
					&card->rx_buffer[bufferoffset / 4],
					pkt_len);
		skb_put(skb, pkt_len);
		skb->protocol = eth_type_trans(skb, dev);
		netif_rx(skb);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += pkt_len;

out:
		/* give the buffer back to the card */
		card->rx_buffer[4*descnr] = cpu_to_le32(0x80000000);
		trigger_receive(card);
	}
}


/* Bufferoffset is in BYTES */
static void
investigate_write_descriptor(struct net_device *dev,
			     struct xircom_private *card,
			     int descnr, unsigned int bufferoffset)
{
	int status;

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
		if (status & (1 << 8))
			dev->stats.collisions++;
		card->tx_buffer[4*descnr] = 0; /* descriptor is free again */
		netif_wake_queue (dev);
		dev->stats.tx_packets++;
	}
}

module_pci_driver(xircom_ops);
