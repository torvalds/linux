/*
 * Driver for the Solos PCI ADSL2+ card, designed to support Linux by
 *  Traverse Technologies -- http://www.traverse.com.au/
 *  Xrio Limited          -- http://www.xrio.com/
 *
 *
 * Copyright © 2008 Traverse Technologies
 * Copyright © 2008 Intel Corporation
 *
 * Authors: Nathan Williams <nathan@traverse.com.au>
 *          David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define DEBUG
#define VERBOSE_DEBUG

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/skbuff.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/kobject.h>

#define VERSION "0.04"
#define PTAG "solos-pci"

#define CONFIG_RAM_SIZE	128
#define FLAGS_ADDR	0x7C
#define IRQ_EN_ADDR	0x78
#define FPGA_VER	0x74
#define IRQ_CLEAR	0x70
#define BUG_FLAG	0x6C

#define DATA_RAM_SIZE	32768
#define BUF_SIZE	4096

#define RX_BUF(card, nr) ((card->buffers) + (nr)*BUF_SIZE*2)
#define TX_BUF(card, nr) ((card->buffers) + (nr)*BUF_SIZE*2 + BUF_SIZE)

static int debug = 0;
static int atmdebug = 0;

struct pkt_hdr {
	__le16 size;
	__le16 vpi;
	__le16 vci;
	__le16 type;
};

#define PKT_DATA	0
#define PKT_COMMAND	1
#define PKT_POPEN	3
#define PKT_PCLOSE	4

struct solos_card {
	void __iomem *config_regs;
	void __iomem *buffers;
	int nr_ports;
	struct pci_dev *dev;
	struct atm_dev *atmdev[4];
	struct tasklet_struct tlet;
	spinlock_t tx_lock;
	spinlock_t tx_queue_lock;
	spinlock_t cli_queue_lock;
	struct sk_buff_head tx_queue[4];
	struct sk_buff_head cli_queue[4];
};

#define SOLOS_CHAN(atmdev) ((int)(unsigned long)(atmdev)->phy_data)

MODULE_AUTHOR("Traverse Technologies <support@traverse.com.au>");
MODULE_DESCRIPTION("Solos PCI driver");
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_PARM_DESC(debug, "Enable Loopback");
MODULE_PARM_DESC(atmdebug, "Print ATM data");
module_param(debug, int, 0444);
module_param(atmdebug, int, 0444);

static int opens;

static void fpga_queue(struct solos_card *card, int port, struct sk_buff *skb,
		       struct atm_vcc *vcc);
static int fpga_tx(struct solos_card *);
static irqreturn_t solos_irq(int irq, void *dev_id);
static struct atm_vcc* find_vcc(struct atm_dev *dev, short vpi, int vci);
static int list_vccs(int vci);
static int atm_init(struct solos_card *);
static void atm_remove(struct solos_card *);
static int send_command(struct solos_card *card, int dev, const char *buf, size_t size);
static void solos_bh(unsigned long);
static int print_buffer(struct sk_buff *buf);

static inline void solos_pop(struct atm_vcc *vcc, struct sk_buff *skb)
{
        if (vcc->pop)
                vcc->pop(vcc, skb);
        else
                dev_kfree_skb_any(skb);
}

static ssize_t console_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct atm_dev *atmdev = container_of(dev, struct atm_dev, class_dev);
	struct solos_card *card = atmdev->dev_data;
	struct sk_buff *skb;

	spin_lock(&card->cli_queue_lock);
	skb = skb_dequeue(&card->cli_queue[SOLOS_CHAN(atmdev)]);
	spin_unlock(&card->cli_queue_lock);
	if(skb == NULL)
		return sprintf(buf, "No data.\n");

	memcpy(buf, skb->data, skb->len);
	dev_dbg(&card->dev->dev, "len: %d\n", skb->len);

	kfree_skb(skb);
	return skb->len;
}

static int send_command(struct solos_card *card, int dev, const char *buf, size_t size)
{
	struct sk_buff *skb;
	struct pkt_hdr *header;

//	dev_dbg(&card->dev->dev, "size: %d\n", size);

	if (size > (BUF_SIZE - sizeof(*header))) {
		dev_dbg(&card->dev->dev, "Command is too big.  Dropping request\n");
		return 0;
	}
	skb = alloc_skb(size + sizeof(*header), GFP_ATOMIC);
	if (!skb) {
		dev_warn(&card->dev->dev, "Failed to allocate sk_buff in send_command()\n");
		return 0;
	}

	header = (void *)skb_put(skb, sizeof(*header));

	header->size = cpu_to_le16(size);
	header->vpi = cpu_to_le16(0);
	header->vci = cpu_to_le16(0);
	header->type = cpu_to_le16(PKT_COMMAND);

	memcpy(skb_put(skb, size), buf, size);

	fpga_queue(card, dev, skb, NULL);

	return 0;
}

static ssize_t console_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct atm_dev *atmdev = container_of(dev, struct atm_dev, class_dev);
	struct solos_card *card = atmdev->dev_data;
	int err;

	err = send_command(card, SOLOS_CHAN(atmdev), buf, count);

	return err?:count;
}

static DEVICE_ATTR(console, 0644, console_show, console_store);

static irqreturn_t solos_irq(int irq, void *dev_id)
{
	struct solos_card *card = dev_id;
	int handled = 1;

	//ACK IRQ
	iowrite32(0, card->config_regs + IRQ_CLEAR);
	//Disable IRQs from FPGA
	iowrite32(0, card->config_regs + IRQ_EN_ADDR);

	/* If we only do it when the device is open, we lose console
	   messages */
	if (1 || opens)
		tasklet_schedule(&card->tlet);

	//Enable IRQs from FPGA
	iowrite32(1, card->config_regs + IRQ_EN_ADDR);
	return IRQ_RETVAL(handled);
}

void solos_bh(unsigned long card_arg)
{
	struct solos_card *card = (void *)card_arg;
	int port;
	uint32_t card_flags;
	uint32_t tx_mask;
	uint32_t rx_done = 0;

	card_flags = ioread32(card->config_regs + FLAGS_ADDR);

	/* The TX bits are set if the channel is busy; clear if not. We want to
	   invoke fpga_tx() unless _all_ the bits for active channels are set */
	tx_mask = (1 << card->nr_ports) - 1;
	if ((card_flags & tx_mask) != tx_mask)
		fpga_tx(card);

	for (port = 0; port < card->nr_ports; port++) {
		if (card_flags & (0x10 << port)) {
			struct pkt_hdr header;
			struct sk_buff *skb;
			struct atm_vcc *vcc;
			int size;

			rx_done |= 0x10 << port;

			memcpy_fromio(&header, RX_BUF(card, port), sizeof(header));

			size = le16_to_cpu(header.size);

			skb = alloc_skb(size, GFP_ATOMIC);
			if (!skb) {
				if (net_ratelimit())
					dev_warn(&card->dev->dev, "Failed to allocate sk_buff for RX\n");
				continue;
			}

			memcpy_fromio(skb_put(skb, size),
				      RX_BUF(card, port) + sizeof(header),
				      size);

			if (atmdebug) {
				dev_info(&card->dev->dev, "Received: device %d\n", port);
				dev_info(&card->dev->dev, "size: %d VPI: %d VCI: %d\n",
					 size, le16_to_cpu(header.vpi),
					 le16_to_cpu(header.vci));
				print_buffer(skb);
			}

			switch (le16_to_cpu(header.type)) {
			case PKT_DATA:
				vcc = find_vcc(card->atmdev[port], le16_to_cpu(header.vpi),
					       le16_to_cpu(header.vci));
				if (!vcc) {
					if (net_ratelimit())
						dev_warn(&card->dev->dev, "Received packet for unknown VCI.VPI %d.%d on port %d\n",
							 le16_to_cpu(header.vci), le16_to_cpu(header.vpi),
							 port);
					continue;
				}
				atm_charge(vcc, skb->truesize);
				vcc->push(vcc, skb);
				atomic_inc(&vcc->stats->rx);
				break;

			case PKT_COMMAND:
			default: /* FIXME: Not really, surely? */
				spin_lock(&card->cli_queue_lock);
				if (skb_queue_len(&card->cli_queue[port]) > 10) {
					if (net_ratelimit())
						dev_warn(&card->dev->dev, "Dropping console response on port %d\n",
							 port);
				} else
					skb_queue_tail(&card->cli_queue[port], skb);
				spin_unlock(&card->cli_queue_lock);
				break;
			}
		}
	}
	if (rx_done)
		iowrite32(rx_done, card->config_regs + FLAGS_ADDR);

	return;
}

static struct atm_vcc *find_vcc(struct atm_dev *dev, short vpi, int vci)
{
	struct hlist_head *head;
	struct atm_vcc *vcc = NULL;
	struct hlist_node *node;
	struct sock *s;

	read_lock(&vcc_sklist_lock);
	head = &vcc_hash[vci & (VCC_HTABLE_SIZE -1)];
	sk_for_each(s, node, head) {
		vcc = atm_sk(s);
		if (vcc->dev == dev && vcc->vci == vci &&
		    vcc->vpi == vpi && vcc->qos.rxtp.traffic_class != ATM_NONE)
			goto out;
	}
	vcc = NULL;
 out:
	read_unlock(&vcc_sklist_lock);
	return vcc;
}

static int list_vccs(int vci)
{
	struct hlist_head *head;
	struct atm_vcc *vcc;
	struct hlist_node *node;
	struct sock *s;
	int num_found = 0;
	int i;

	read_lock(&vcc_sklist_lock);
	if (vci != 0){
		head = &vcc_hash[vci & (VCC_HTABLE_SIZE -1)];
		sk_for_each(s, node, head) {
			num_found ++;
			vcc = atm_sk(s);
			printk(KERN_DEBUG "Device: %d Vpi: %d Vci: %d\n",
			       vcc->dev->number,
			       vcc->vpi,
			       vcc->vci);
		}
	} else {
		for(i=0; i<32; i++){
			head = &vcc_hash[i];
			sk_for_each(s, node, head) {
				num_found ++;
				vcc = atm_sk(s);
				printk(KERN_DEBUG "Device: %d Vpi: %d Vci: %d\n",
				       vcc->dev->number,
				       vcc->vpi,
				       vcc->vci);
			}
		}
	}
	read_unlock(&vcc_sklist_lock);
	return num_found;
}


static int popen(struct atm_vcc *vcc)
{
	struct solos_card *card = vcc->dev->dev_data;
	struct sk_buff *skb;
	struct pkt_hdr *header;

	skb = alloc_skb(sizeof(*header), GFP_ATOMIC);
	if (!skb && net_ratelimit()) {
		dev_warn(&card->dev->dev, "Failed to allocate sk_buff in popen()\n");
		return -ENOMEM;
	}
	header = (void *)skb_put(skb, sizeof(*header));

	header->size = cpu_to_le16(sizeof(*header));
	header->vpi = cpu_to_le16(vcc->vpi);
	header->vci = cpu_to_le16(vcc->vci);
	header->type = cpu_to_le16(PKT_POPEN);

	fpga_queue(card, SOLOS_CHAN(vcc->dev), skb, NULL);

//	dev_dbg(&card->dev->dev, "Open for vpi %d and vci %d on interface %d\n", vcc->vpi, vcc->vci, SOLOS_CHAN(vcc->dev));
	set_bit(ATM_VF_ADDR, &vcc->flags); // accept the vpi / vci
	set_bit(ATM_VF_READY, &vcc->flags);
	list_vccs(0);

	if (!opens)
		iowrite32(1, card->config_regs + IRQ_EN_ADDR);

	opens++; //count open PVCs

	return 0;
}

static void pclose(struct atm_vcc *vcc)
{
	struct solos_card *card = vcc->dev->dev_data;
	struct sk_buff *skb;
	struct pkt_hdr *header;

	skb = alloc_skb(sizeof(*header), GFP_ATOMIC);
	if (!skb) {
		dev_warn(&card->dev->dev, "Failed to allocate sk_buff in pclose()\n");
		return;
	}
	header = (void *)skb_put(skb, sizeof(*header));

	header->size = cpu_to_le16(sizeof(*header));
	header->vpi = cpu_to_le16(vcc->vpi);
	header->vci = cpu_to_le16(vcc->vci);
	header->type = cpu_to_le16(PKT_PCLOSE);

	fpga_queue(card, SOLOS_CHAN(vcc->dev), skb, NULL);

//	dev_dbg(&card->dev->dev, "Close for vpi %d and vci %d on interface %d\n", vcc->vpi, vcc->vci, SOLOS_CHAN(vcc->dev));
	if (!--opens)
		iowrite32(0, card->config_regs + IRQ_EN_ADDR);

	clear_bit(ATM_VF_ADDR, &vcc->flags);
	clear_bit(ATM_VF_READY, &vcc->flags);

	return;
}

static int print_buffer(struct sk_buff *buf)
{
	int len,i;
	char msg[500];
	char item[10];

	len = buf->len;
	for (i = 0; i < len; i++){
		if(i % 8 == 0)
			sprintf(msg, "%02X: ", i);

		sprintf(item,"%02X ",*(buf->data + i));
		strcat(msg, item);
		if(i % 8 == 7) {
			sprintf(item, "\n");
			strcat(msg, item);
			printk(KERN_DEBUG "%s", msg);
		}
	}
	if (i % 8 != 0) {
		sprintf(item, "\n");
		strcat(msg, item);
		printk(KERN_DEBUG "%s", msg);
	}
	printk(KERN_DEBUG "\n");

	return 0;
}

static void fpga_queue(struct solos_card *card, int port, struct sk_buff *skb,
		       struct atm_vcc *vcc)
{
	int old_len;

	*(void **)skb->cb = vcc;

	spin_lock(&card->tx_queue_lock);
	old_len = skb_queue_len(&card->tx_queue[port]);
	skb_queue_tail(&card->tx_queue[port], skb);
	spin_unlock(&card->tx_queue_lock);

	/* If TX might need to be started, do so */
	if (!old_len)
		fpga_tx(card);
}

static int fpga_tx(struct solos_card *card)
{
	uint32_t tx_pending;
	uint32_t tx_started = 0;
	struct sk_buff *skb;
	struct atm_vcc *vcc;
	unsigned char port;
	unsigned long flags;

	spin_lock_irqsave(&card->tx_lock, flags);

	tx_pending = ioread32(card->config_regs + FLAGS_ADDR);

	dev_vdbg(&card->dev->dev, "TX Flags are %X\n", tx_pending);

	for (port = 0; port < card->nr_ports; port++) {
		if (!(tx_pending & (1 << port))) {

			spin_lock(&card->tx_queue_lock);
			skb = skb_dequeue(&card->tx_queue[port]);
			spin_unlock(&card->tx_queue_lock);

			if (!skb)
				continue;

			if (atmdebug) {
				dev_info(&card->dev->dev, "Transmitted: port %d\n",
					 port);
				print_buffer(skb);
			}
			memcpy_toio(TX_BUF(card, port), skb->data, skb->len);

			vcc = *(void **)skb->cb;

			if (vcc) {
				atomic_inc(&vcc->stats->tx);
				solos_pop(vcc, skb);
			} else
				dev_kfree_skb_irq(skb);

			tx_started |= 1 << port; //Set TX full flag
		}
	}
	if (tx_started)
		iowrite32(tx_started, card->config_regs + FLAGS_ADDR);

	spin_unlock_irqrestore(&card->tx_lock, flags);
	return 0;
}

static int psend(struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct solos_card *card = vcc->dev->dev_data;
	struct sk_buff *skb2 = NULL;
	struct pkt_hdr *header;

	//dev_dbg(&card->dev->dev, "psend called.\n");
	//dev_dbg(&card->dev->dev, "dev,vpi,vci = %d,%d,%d\n",SOLOS_CHAN(vcc->dev),vcc->vpi,vcc->vci);

	if (debug) {
		skb2 = atm_alloc_charge(vcc, skb->len, GFP_ATOMIC);
		if (skb2) {
			memcpy(skb2->data, skb->data, skb->len);
			skb_put(skb2, skb->len);
			vcc->push(vcc, skb2);
			atomic_inc(&vcc->stats->rx);
		}
		atomic_inc(&vcc->stats->tx);
		solos_pop(vcc, skb);
		return 0;
	}

	if (skb->len > (BUF_SIZE - sizeof(*header))) {
		dev_warn(&card->dev->dev, "Length of PDU is too large. Dropping PDU.\n");
		solos_pop(vcc, skb);
		return 0;
	}

	if (!skb_clone_writable(skb, sizeof(*header))) {
		int expand_by = 0;
		int ret;

		if (skb_headroom(skb) < sizeof(*header))
			expand_by = sizeof(*header) - skb_headroom(skb);

		ret = pskb_expand_head(skb, expand_by, 0, GFP_ATOMIC);
		if (ret) {
			solos_pop(vcc, skb);
			return ret;
		}
	}

	header = (void *)skb_push(skb, sizeof(*header));

	header->size = cpu_to_le16(skb->len);
	header->vpi = cpu_to_le16(vcc->vpi);
	header->vci = cpu_to_le16(vcc->vci);
	header->type = cpu_to_le16(PKT_DATA);

	fpga_queue(card, SOLOS_CHAN(vcc->dev), skb, vcc);

	return 0;
}

static struct atmdev_ops fpga_ops = {
	.open =		popen,
	.close =	pclose,
	.ioctl =	NULL,
	.getsockopt =	NULL,
	.setsockopt =	NULL,
	.send =		psend,
	.send_oam =	NULL,
	.phy_put =	NULL,
	.phy_get =	NULL,
	.change_qos =	NULL,
	.proc_read =	NULL,
	.owner =	THIS_MODULE
};

static int fpga_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int err, i;
	uint16_t fpga_ver;
	uint8_t major_ver, minor_ver;
	uint32_t data32;
	struct solos_card *card;

	if (debug)
		return 0;

	card = kzalloc(sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->dev = dev;

	err = pci_enable_device(dev);
	if (err) {
		dev_warn(&dev->dev,  "Failed to enable PCI device\n");
		goto out;
	}

	err = pci_request_regions(dev, "solos");
	if (err) {
		dev_warn(&dev->dev, "Failed to request regions\n");
		goto out;
	}

	card->config_regs = pci_iomap(dev, 0, CONFIG_RAM_SIZE);
	if (!card->config_regs) {
		dev_warn(&dev->dev, "Failed to ioremap config registers\n");
		goto out_release_regions;
	}
	card->buffers = pci_iomap(dev, 1, DATA_RAM_SIZE);
	if (!card->buffers) {
		dev_warn(&dev->dev, "Failed to ioremap data buffers\n");
		goto out_unmap_config;
	}

//	for(i=0;i<64 ;i+=4){
//		data32=ioread32(card->buffers + i);
//		dev_dbg(&card->dev->dev, "%08lX\n",(unsigned long)data32);
//	}

	//Fill Config Mem with zeros
	for(i = 0; i < 128; i += 4)
		iowrite32(0, card->config_regs + i);

	//Set RX empty flags
	iowrite32(0xF0, card->config_regs + FLAGS_ADDR);

	data32 = ioread32(card->config_regs + FPGA_VER);
	fpga_ver = (data32 & 0x0000FFFF);
	major_ver = ((data32 & 0xFF000000) >> 24);
	minor_ver = ((data32 & 0x00FF0000) >> 16);
	dev_info(&dev->dev, "Solos FPGA Version %d.%02d svn-%d\n",
		 major_ver, minor_ver, fpga_ver);

	card->nr_ports = 2; /* FIXME: Detect daughterboard */

	err = atm_init(card);
	if (err)
		goto out_unmap_both;

	pci_set_drvdata(dev, card);
	tasklet_init(&card->tlet, solos_bh, (unsigned long)card);
	spin_lock_init(&card->tx_lock);
	spin_lock_init(&card->tx_queue_lock);
	spin_lock_init(&card->cli_queue_lock);
/*
	// Set Loopback mode
	data32 = 0x00010000;
	iowrite32(data32,card->config_regs + FLAGS_ADDR);
*/
/*
	// Fill Buffers with zeros
	for (i = 0; i < BUF_SIZE * 8; i += 4)
		iowrite32(0, card->buffers + i);
*/
/*
	for(i = 0; i < (BUF_SIZE * 1); i += 4)
		iowrite32(0x12345678, card->buffers + i + (0*BUF_SIZE));
	for(i = 0; i < (BUF_SIZE * 1); i += 4)
		iowrite32(0xabcdef98, card->buffers + i + (1*BUF_SIZE));

	// Read Config Memory
	printk(KERN_DEBUG "Reading Config MEM\n");
	i = 0;
	for(i = 0; i < 16; i++) {
		data32=ioread32(card->buffers + i*(BUF_SIZE/2));
		printk(KERN_ALERT "Addr: %lX Data: %08lX\n",
		       (unsigned long)(addr_start + i*(BUF_SIZE/2)),
		       (unsigned long)data32);
	}
*/
	//dev_dbg(&card->dev->dev, "Requesting IRQ: %d\n",dev->irq);
	err = request_irq(dev->irq, solos_irq, IRQF_DISABLED|IRQF_SHARED,
			  "solos-pci", card);
	if (err)
		dev_dbg(&card->dev->dev, "Failed to request interrupt IRQ: %d\n", dev->irq);

	// Enable IRQs
	iowrite32(1, card->config_regs + IRQ_EN_ADDR);

	return 0;

 out_unmap_both:
	pci_iounmap(dev, card->config_regs);
 out_unmap_config:
	pci_iounmap(dev, card->buffers);
 out_release_regions:
	pci_release_regions(dev);
 out:
	kfree(card);
	return err;
}

static int atm_init(struct solos_card *card)
{
	int i;

	opens = 0;

	for (i = 0; i < card->nr_ports; i++) {
		skb_queue_head_init(&card->tx_queue[i]);
		skb_queue_head_init(&card->cli_queue[i]);

		card->atmdev[i] = atm_dev_register("solos-pci", &fpga_ops, -1, NULL);
		if (!card->atmdev[i]) {
			dev_err(&card->dev->dev, "Could not register ATM device %d\n", i);
			atm_remove(card);
			return -ENODEV;
		}
		if (device_create_file(&card->atmdev[i]->class_dev, &dev_attr_console))
			dev_err(&card->dev->dev, "Could not register console for ATM device %d\n", i);

		dev_info(&card->dev->dev, "Registered ATM device %d\n", card->atmdev[i]->number);

		card->atmdev[i]->ci_range.vpi_bits = 8;
		card->atmdev[i]->ci_range.vci_bits = 16;
		card->atmdev[i]->dev_data = card;
		card->atmdev[i]->phy_data = (void *)(unsigned long)i;
	}
	return 0;
}

static void atm_remove(struct solos_card *card)
{
	int i;

	for (i = 0; i < card->nr_ports; i++) {
		if (card->atmdev[i]) {
			dev_info(&card->dev->dev, "Unregistering ATM device %d\n", card->atmdev[i]->number);
			atm_dev_deregister(card->atmdev[i]);
		}
	}
}

static void fpga_remove(struct pci_dev *dev)
{
	struct solos_card *card = pci_get_drvdata(dev);

	if (debug)
		return;

	atm_remove(card);

	dev_vdbg(&dev->dev, "Freeing IRQ\n");
	// Disable IRQs from FPGA
	iowrite32(0, card->config_regs + IRQ_EN_ADDR);
	free_irq(dev->irq, card);
	tasklet_kill(&card->tlet);

	//	iowrite32(0x01,pciregs);
	dev_vdbg(&dev->dev, "Unmapping PCI resource\n");
	pci_iounmap(dev, card->buffers);
	pci_iounmap(dev, card->config_regs);

	dev_vdbg(&dev->dev, "Releasing PCI Region\n");
	pci_release_regions(dev);
	pci_disable_device(dev);

	pci_set_drvdata(dev, NULL);
	kfree(card);
//	dev_dbg(&card->dev->dev, "fpga_remove\n");
	return;
}

static struct pci_device_id fpga_pci_tbl[] __devinitdata = {
	{ 0x10ee, 0x0300, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci,fpga_pci_tbl);

static struct pci_driver fpga_driver = {
	.name =		"solos",
	.id_table =	fpga_pci_tbl,
	.probe =	fpga_probe,
	.remove =	fpga_remove,
};


static int __init solos_pci_init(void)
{
	printk(KERN_INFO "Solos PCI Driver Version %s\n", VERSION);
	return pci_register_driver(&fpga_driver);
}

static void __exit solos_pci_exit(void)
{
	pci_unregister_driver(&fpga_driver);
	printk(KERN_INFO "Solos PCI Driver %s Unloaded\n", VERSION);
}

module_init(solos_pci_init);
module_exit(solos_pci_exit);
