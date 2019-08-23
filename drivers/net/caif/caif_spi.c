// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:  Daniel Martensson
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/if_arp.h>
#include <net/caif/caif_layer.h>
#include <net/caif/caif_spi.h>

#ifndef CONFIG_CAIF_SPI_SYNC
#define FLAVOR "Flavour: Vanilla.\n"
#else
#define FLAVOR "Flavour: Master CMD&LEN at start.\n"
#endif /* CONFIG_CAIF_SPI_SYNC */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Martensson");
MODULE_DESCRIPTION("CAIF SPI driver");

/* Returns the number of padding bytes for alignment. */
#define PAD_POW2(x, pow) ((((x)&((pow)-1))==0) ? 0 : (((pow)-((x)&((pow)-1)))))

static bool spi_loop;
module_param(spi_loop, bool, 0444);
MODULE_PARM_DESC(spi_loop, "SPI running in loopback mode.");

/* SPI frame alignment. */
module_param(spi_frm_align, int, 0444);
MODULE_PARM_DESC(spi_frm_align, "SPI frame alignment.");

/*
 * SPI padding options.
 * Warning: must be a base of 2 (& operation used) and can not be zero !
 */
module_param(spi_up_head_align, int, 0444);
MODULE_PARM_DESC(spi_up_head_align, "SPI uplink head alignment.");

module_param(spi_up_tail_align, int, 0444);
MODULE_PARM_DESC(spi_up_tail_align, "SPI uplink tail alignment.");

module_param(spi_down_head_align, int, 0444);
MODULE_PARM_DESC(spi_down_head_align, "SPI downlink head alignment.");

module_param(spi_down_tail_align, int, 0444);
MODULE_PARM_DESC(spi_down_tail_align, "SPI downlink tail alignment.");

#ifdef CONFIG_ARM
#define BYTE_HEX_FMT "%02X"
#else
#define BYTE_HEX_FMT "%02hhX"
#endif

#define SPI_MAX_PAYLOAD_SIZE 4096
/*
 * Threshold values for the SPI packet queue. Flowcontrol will be asserted
 * when the number of packets exceeds HIGH_WATER_MARK. It will not be
 * deasserted before the number of packets drops below LOW_WATER_MARK.
 */
#define LOW_WATER_MARK   100
#define HIGH_WATER_MARK  (LOW_WATER_MARK*5)

#ifndef CONFIG_HAS_DMA

/*
 * We sometimes use UML for debugging, but it cannot handle
 * dma_alloc_coherent so we have to wrap it.
 */
static inline void *dma_alloc(struct cfspi *cfspi, dma_addr_t *daddr)
{
	return kmalloc(SPI_DMA_BUF_LEN, GFP_KERNEL);
}

static inline void dma_free(struct cfspi *cfspi, void *cpu_addr,
		dma_addr_t handle)
{
	kfree(cpu_addr);
}

#else

static inline void *dma_alloc(struct cfspi *cfspi, dma_addr_t *daddr)
{
	return dma_alloc_coherent(&cfspi->pdev->dev, SPI_DMA_BUF_LEN, daddr,
				GFP_KERNEL);
}

static inline void dma_free(struct cfspi *cfspi, void *cpu_addr,
		dma_addr_t handle)
{
	dma_free_coherent(&cfspi->pdev->dev, SPI_DMA_BUF_LEN, cpu_addr, handle);
}
#endif	/* CONFIG_HAS_DMA */

#ifdef CONFIG_DEBUG_FS

#define DEBUGFS_BUF_SIZE	4096

static struct dentry *dbgfs_root;

static inline void driver_debugfs_create(void)
{
	dbgfs_root = debugfs_create_dir(cfspi_spi_driver.driver.name, NULL);
}

static inline void driver_debugfs_remove(void)
{
	debugfs_remove(dbgfs_root);
}

static inline void dev_debugfs_rem(struct cfspi *cfspi)
{
	debugfs_remove(cfspi->dbgfs_frame);
	debugfs_remove(cfspi->dbgfs_state);
	debugfs_remove(cfspi->dbgfs_dir);
}

static ssize_t dbgfs_state(struct file *file, char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	char *buf;
	int len = 0;
	ssize_t size;
	struct cfspi *cfspi = file->private_data;

	buf = kzalloc(DEBUGFS_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return 0;

	/* Print out debug information. */
	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"CAIF SPI debug information:\n");

	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len), FLAVOR);

	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"STATE: %d\n", cfspi->dbg_state);
	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"Previous CMD: 0x%x\n", cfspi->pcmd);
	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"Current CMD: 0x%x\n", cfspi->cmd);
	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"Previous TX len: %d\n", cfspi->tx_ppck_len);
	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"Previous RX len: %d\n", cfspi->rx_ppck_len);
	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"Current TX len: %d\n", cfspi->tx_cpck_len);
	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"Current RX len: %d\n", cfspi->rx_cpck_len);
	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"Next TX len: %d\n", cfspi->tx_npck_len);
	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"Next RX len: %d\n", cfspi->rx_npck_len);

	if (len > DEBUGFS_BUF_SIZE)
		len = DEBUGFS_BUF_SIZE;

	size = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return size;
}

static ssize_t print_frame(char *buf, size_t size, char *frm,
			   size_t count, size_t cut)
{
	int len = 0;
	int i;
	for (i = 0; i < count; i++) {
		len += snprintf((buf + len), (size - len),
					"[0x" BYTE_HEX_FMT "]",
					frm[i]);
		if ((i == cut) && (count > (cut * 2))) {
			/* Fast forward. */
			i = count - cut;
			len += snprintf((buf + len), (size - len),
					"--- %zu bytes skipped ---\n",
					count - (cut * 2));
		}

		if ((!(i % 10)) && i) {
			len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
					"\n");
		}
	}
	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len), "\n");
	return len;
}

static ssize_t dbgfs_frame(struct file *file, char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	char *buf;
	int len = 0;
	ssize_t size;
	struct cfspi *cfspi;

	cfspi = file->private_data;
	buf = kzalloc(DEBUGFS_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return 0;

	/* Print out debug information. */
	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"Current frame:\n");

	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"Tx data (Len: %d):\n", cfspi->tx_cpck_len);

	len += print_frame((buf + len), (DEBUGFS_BUF_SIZE - len),
			   cfspi->xfer.va_tx[0],
			   (cfspi->tx_cpck_len + SPI_CMD_SZ), 100);

	len += snprintf((buf + len), (DEBUGFS_BUF_SIZE - len),
			"Rx data (Len: %d):\n", cfspi->rx_cpck_len);

	len += print_frame((buf + len), (DEBUGFS_BUF_SIZE - len),
			   cfspi->xfer.va_rx,
			   (cfspi->rx_cpck_len + SPI_CMD_SZ), 100);

	size = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return size;
}

static const struct file_operations dbgfs_state_fops = {
	.open = simple_open,
	.read = dbgfs_state,
	.owner = THIS_MODULE
};

static const struct file_operations dbgfs_frame_fops = {
	.open = simple_open,
	.read = dbgfs_frame,
	.owner = THIS_MODULE
};

static inline void dev_debugfs_add(struct cfspi *cfspi)
{
	cfspi->dbgfs_dir = debugfs_create_dir(cfspi->pdev->name, dbgfs_root);
	cfspi->dbgfs_state = debugfs_create_file("state", 0444,
						 cfspi->dbgfs_dir, cfspi,
						 &dbgfs_state_fops);
	cfspi->dbgfs_frame = debugfs_create_file("frame", 0444,
						 cfspi->dbgfs_dir, cfspi,
						 &dbgfs_frame_fops);
}

inline void cfspi_dbg_state(struct cfspi *cfspi, int state)
{
	cfspi->dbg_state = state;
};
#else

static inline void driver_debugfs_create(void)
{
}

static inline void driver_debugfs_remove(void)
{
}

static inline void dev_debugfs_add(struct cfspi *cfspi)
{
}

static inline void dev_debugfs_rem(struct cfspi *cfspi)
{
}

inline void cfspi_dbg_state(struct cfspi *cfspi, int state)
{
}
#endif				/* CONFIG_DEBUG_FS */

static LIST_HEAD(cfspi_list);
static spinlock_t cfspi_list_lock;

/* SPI uplink head alignment. */
static ssize_t up_head_align_show(struct device_driver *driver, char *buf)
{
	return sprintf(buf, "%d\n", spi_up_head_align);
}

static DRIVER_ATTR_RO(up_head_align);

/* SPI uplink tail alignment. */
static ssize_t up_tail_align_show(struct device_driver *driver, char *buf)
{
	return sprintf(buf, "%d\n", spi_up_tail_align);
}

static DRIVER_ATTR_RO(up_tail_align);

/* SPI downlink head alignment. */
static ssize_t down_head_align_show(struct device_driver *driver, char *buf)
{
	return sprintf(buf, "%d\n", spi_down_head_align);
}

static DRIVER_ATTR_RO(down_head_align);

/* SPI downlink tail alignment. */
static ssize_t down_tail_align_show(struct device_driver *driver, char *buf)
{
	return sprintf(buf, "%d\n", spi_down_tail_align);
}

static DRIVER_ATTR_RO(down_tail_align);

/* SPI frame alignment. */
static ssize_t frame_align_show(struct device_driver *driver, char *buf)
{
	return sprintf(buf, "%d\n", spi_frm_align);
}

static DRIVER_ATTR_RO(frame_align);

int cfspi_xmitfrm(struct cfspi *cfspi, u8 *buf, size_t len)
{
	u8 *dst = buf;
	caif_assert(buf);

	if (cfspi->slave && !cfspi->slave_talked)
		cfspi->slave_talked = true;

	do {
		struct sk_buff *skb;
		struct caif_payload_info *info;
		int spad = 0;
		int epad;

		skb = skb_dequeue(&cfspi->chead);
		if (!skb)
			break;

		/*
		 * Calculate length of frame including SPI padding.
		 * The payload position is found in the control buffer.
		 */
		info = (struct caif_payload_info *)&skb->cb;

		/*
		 * Compute head offset i.e. number of bytes to add to
		 * get the start of the payload aligned.
		 */
		if (spi_up_head_align > 1) {
			spad = 1 + PAD_POW2((info->hdr_len + 1), spi_up_head_align);
			*dst = (u8)(spad - 1);
			dst += spad;
		}

		/* Copy in CAIF frame. */
		skb_copy_bits(skb, 0, dst, skb->len);
		dst += skb->len;
		cfspi->ndev->stats.tx_packets++;
		cfspi->ndev->stats.tx_bytes += skb->len;

		/*
		 * Compute tail offset i.e. number of bytes to add to
		 * get the complete CAIF frame aligned.
		 */
		epad = PAD_POW2((skb->len + spad), spi_up_tail_align);
		dst += epad;

		dev_kfree_skb(skb);

	} while ((dst - buf) < len);

	return dst - buf;
}

int cfspi_xmitlen(struct cfspi *cfspi)
{
	struct sk_buff *skb = NULL;
	int frm_len = 0;
	int pkts = 0;

	/*
	 * Decommit previously committed frames.
	 * skb_queue_splice_tail(&cfspi->chead,&cfspi->qhead)
	 */
	while (skb_peek(&cfspi->chead)) {
		skb = skb_dequeue_tail(&cfspi->chead);
		skb_queue_head(&cfspi->qhead, skb);
	}

	do {
		struct caif_payload_info *info = NULL;
		int spad = 0;
		int epad = 0;

		skb = skb_dequeue(&cfspi->qhead);
		if (!skb)
			break;

		/*
		 * Calculate length of frame including SPI padding.
		 * The payload position is found in the control buffer.
		 */
		info = (struct caif_payload_info *)&skb->cb;

		/*
		 * Compute head offset i.e. number of bytes to add to
		 * get the start of the payload aligned.
		 */
		if (spi_up_head_align > 1)
			spad = 1 + PAD_POW2((info->hdr_len + 1), spi_up_head_align);

		/*
		 * Compute tail offset i.e. number of bytes to add to
		 * get the complete CAIF frame aligned.
		 */
		epad = PAD_POW2((skb->len + spad), spi_up_tail_align);

		if ((skb->len + spad + epad + frm_len) <= CAIF_MAX_SPI_FRAME) {
			skb_queue_tail(&cfspi->chead, skb);
			pkts++;
			frm_len += skb->len + spad + epad;
		} else {
			/* Put back packet. */
			skb_queue_head(&cfspi->qhead, skb);
			break;
		}
	} while (pkts <= CAIF_MAX_SPI_PKTS);

	/*
	 * Send flow on if previously sent flow off
	 * and now go below the low water mark
	 */
	if (cfspi->flow_off_sent && cfspi->qhead.qlen < cfspi->qd_low_mark &&
		cfspi->cfdev.flowctrl) {
		cfspi->flow_off_sent = 0;
		cfspi->cfdev.flowctrl(cfspi->ndev, 1);
	}

	return frm_len;
}

static void cfspi_ss_cb(bool assert, struct cfspi_ifc *ifc)
{
	struct cfspi *cfspi = (struct cfspi *)ifc->priv;

	/*
	 * The slave device is the master on the link. Interrupts before the
	 * slave has transmitted are considered spurious.
	 */
	if (cfspi->slave && !cfspi->slave_talked) {
		printk(KERN_WARNING "CFSPI: Spurious SS interrupt.\n");
		return;
	}

	if (!in_interrupt())
		spin_lock(&cfspi->lock);
	if (assert) {
		set_bit(SPI_SS_ON, &cfspi->state);
		set_bit(SPI_XFER, &cfspi->state);
	} else {
		set_bit(SPI_SS_OFF, &cfspi->state);
	}
	if (!in_interrupt())
		spin_unlock(&cfspi->lock);

	/* Wake up the xfer thread. */
	if (assert)
		wake_up_interruptible(&cfspi->wait);
}

static void cfspi_xfer_done_cb(struct cfspi_ifc *ifc)
{
	struct cfspi *cfspi = (struct cfspi *)ifc->priv;

	/* Transfer done, complete work queue */
	complete(&cfspi->comp);
}

static int cfspi_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct cfspi *cfspi = NULL;
	unsigned long flags;
	if (!dev)
		return -EINVAL;

	cfspi = netdev_priv(dev);

	skb_queue_tail(&cfspi->qhead, skb);

	spin_lock_irqsave(&cfspi->lock, flags);
	if (!test_and_set_bit(SPI_XFER, &cfspi->state)) {
		/* Wake up xfer thread. */
		wake_up_interruptible(&cfspi->wait);
	}
	spin_unlock_irqrestore(&cfspi->lock, flags);

	/* Send flow off if number of bytes is above high water mark */
	if (!cfspi->flow_off_sent &&
		cfspi->qhead.qlen > cfspi->qd_high_mark &&
		cfspi->cfdev.flowctrl) {
		cfspi->flow_off_sent = 1;
		cfspi->cfdev.flowctrl(cfspi->ndev, 0);
	}

	return 0;
}

int cfspi_rxfrm(struct cfspi *cfspi, u8 *buf, size_t len)
{
	u8 *src = buf;

	caif_assert(buf != NULL);

	do {
		int res;
		struct sk_buff *skb = NULL;
		int spad = 0;
		int epad = 0;
		int pkt_len = 0;

		/*
		 * Compute head offset i.e. number of bytes added to
		 * get the start of the payload aligned.
		 */
		if (spi_down_head_align > 1) {
			spad = 1 + *src;
			src += spad;
		}

		/* Read length of CAIF frame (little endian). */
		pkt_len = *src;
		pkt_len |= ((*(src+1)) << 8) & 0xFF00;
		pkt_len += 2;	/* Add FCS fields. */

		/* Get a suitable caif packet and copy in data. */

		skb = netdev_alloc_skb(cfspi->ndev, pkt_len + 1);
		caif_assert(skb != NULL);

		skb_put_data(skb, src, pkt_len);
		src += pkt_len;

		skb->protocol = htons(ETH_P_CAIF);
		skb_reset_mac_header(skb);

		/*
		 * Push received packet up the stack.
		 */
		if (!spi_loop)
			res = netif_rx_ni(skb);
		else
			res = cfspi_xmit(skb, cfspi->ndev);

		if (!res) {
			cfspi->ndev->stats.rx_packets++;
			cfspi->ndev->stats.rx_bytes += pkt_len;
		} else
			cfspi->ndev->stats.rx_dropped++;

		/*
		 * Compute tail offset i.e. number of bytes added to
		 * get the complete CAIF frame aligned.
		 */
		epad = PAD_POW2((pkt_len + spad), spi_down_tail_align);
		src += epad;
	} while ((src - buf) < len);

	return src - buf;
}

static int cfspi_open(struct net_device *dev)
{
	netif_wake_queue(dev);
	return 0;
}

static int cfspi_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static int cfspi_init(struct net_device *dev)
{
	int res = 0;
	struct cfspi *cfspi = netdev_priv(dev);

	/* Set flow info. */
	cfspi->flow_off_sent = 0;
	cfspi->qd_low_mark = LOW_WATER_MARK;
	cfspi->qd_high_mark = HIGH_WATER_MARK;

	/* Set slave info. */
	if (!strncmp(cfspi_spi_driver.driver.name, "cfspi_sspi", 10)) {
		cfspi->slave = true;
		cfspi->slave_talked = false;
	} else {
		cfspi->slave = false;
		cfspi->slave_talked = false;
	}

	/* Allocate DMA buffers. */
	cfspi->xfer.va_tx[0] = dma_alloc(cfspi, &cfspi->xfer.pa_tx[0]);
	if (!cfspi->xfer.va_tx[0]) {
		res = -ENODEV;
		goto err_dma_alloc_tx_0;
	}

	cfspi->xfer.va_rx = dma_alloc(cfspi, &cfspi->xfer.pa_rx);

	if (!cfspi->xfer.va_rx) {
		res = -ENODEV;
		goto err_dma_alloc_rx;
	}

	/* Initialize the work queue. */
	INIT_WORK(&cfspi->work, cfspi_xfer);

	/* Initialize spin locks. */
	spin_lock_init(&cfspi->lock);

	/* Initialize flow control state. */
	cfspi->flow_stop = false;

	/* Initialize wait queue. */
	init_waitqueue_head(&cfspi->wait);

	/* Create work thread. */
	cfspi->wq = create_singlethread_workqueue(dev->name);
	if (!cfspi->wq) {
		printk(KERN_WARNING "CFSPI: failed to create work queue.\n");
		res = -ENODEV;
		goto err_create_wq;
	}

	/* Initialize work queue. */
	init_completion(&cfspi->comp);

	/* Create debugfs entries. */
	dev_debugfs_add(cfspi);

	/* Set up the ifc. */
	cfspi->ifc.ss_cb = cfspi_ss_cb;
	cfspi->ifc.xfer_done_cb = cfspi_xfer_done_cb;
	cfspi->ifc.priv = cfspi;

	/* Add CAIF SPI device to list. */
	spin_lock(&cfspi_list_lock);
	list_add_tail(&cfspi->list, &cfspi_list);
	spin_unlock(&cfspi_list_lock);

	/* Schedule the work queue. */
	queue_work(cfspi->wq, &cfspi->work);

	return 0;

 err_create_wq:
	dma_free(cfspi, cfspi->xfer.va_rx, cfspi->xfer.pa_rx);
 err_dma_alloc_rx:
	dma_free(cfspi, cfspi->xfer.va_tx[0], cfspi->xfer.pa_tx[0]);
 err_dma_alloc_tx_0:
	return res;
}

static void cfspi_uninit(struct net_device *dev)
{
	struct cfspi *cfspi = netdev_priv(dev);

	/* Remove from list. */
	spin_lock(&cfspi_list_lock);
	list_del(&cfspi->list);
	spin_unlock(&cfspi_list_lock);

	cfspi->ndev = NULL;
	/* Free DMA buffers. */
	dma_free(cfspi, cfspi->xfer.va_rx, cfspi->xfer.pa_rx);
	dma_free(cfspi, cfspi->xfer.va_tx[0], cfspi->xfer.pa_tx[0]);
	set_bit(SPI_TERMINATE, &cfspi->state);
	wake_up_interruptible(&cfspi->wait);
	destroy_workqueue(cfspi->wq);
	/* Destroy debugfs directory and files. */
	dev_debugfs_rem(cfspi);
	return;
}

static const struct net_device_ops cfspi_ops = {
	.ndo_open = cfspi_open,
	.ndo_stop = cfspi_close,
	.ndo_init = cfspi_init,
	.ndo_uninit = cfspi_uninit,
	.ndo_start_xmit = cfspi_xmit
};

static void cfspi_setup(struct net_device *dev)
{
	struct cfspi *cfspi = netdev_priv(dev);
	dev->features = 0;
	dev->netdev_ops = &cfspi_ops;
	dev->type = ARPHRD_CAIF;
	dev->flags = IFF_NOARP | IFF_POINTOPOINT;
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->mtu = SPI_MAX_PAYLOAD_SIZE;
	dev->needs_free_netdev = true;
	skb_queue_head_init(&cfspi->qhead);
	skb_queue_head_init(&cfspi->chead);
	cfspi->cfdev.link_select = CAIF_LINK_HIGH_BANDW;
	cfspi->cfdev.use_frag = false;
	cfspi->cfdev.use_stx = false;
	cfspi->cfdev.use_fcs = false;
	cfspi->ndev = dev;
}

int cfspi_spi_probe(struct platform_device *pdev)
{
	struct cfspi *cfspi = NULL;
	struct net_device *ndev;
	struct cfspi_dev *dev;
	int res;
	dev = (struct cfspi_dev *)pdev->dev.platform_data;

	if (!dev)
		return -ENODEV;

	ndev = alloc_netdev(sizeof(struct cfspi), "cfspi%d",
			    NET_NAME_UNKNOWN, cfspi_setup);
	if (!ndev)
		return -ENOMEM;

	cfspi = netdev_priv(ndev);
	netif_stop_queue(ndev);
	cfspi->ndev = ndev;
	cfspi->pdev = pdev;

	/* Assign the SPI device. */
	cfspi->dev = dev;
	/* Assign the device ifc to this SPI interface. */
	dev->ifc = &cfspi->ifc;

	/* Register network device. */
	res = register_netdev(ndev);
	if (res) {
		printk(KERN_ERR "CFSPI: Reg. error: %d.\n", res);
		goto err_net_reg;
	}
	return res;

 err_net_reg:
	free_netdev(ndev);

	return res;
}

int cfspi_spi_remove(struct platform_device *pdev)
{
	/* Everything is done in cfspi_uninit(). */
	return 0;
}

static void __exit cfspi_exit_module(void)
{
	struct list_head *list_node;
	struct list_head *n;
	struct cfspi *cfspi = NULL;

	list_for_each_safe(list_node, n, &cfspi_list) {
		cfspi = list_entry(list_node, struct cfspi, list);
		unregister_netdev(cfspi->ndev);
	}

	/* Destroy sysfs files. */
	driver_remove_file(&cfspi_spi_driver.driver,
			   &driver_attr_up_head_align);
	driver_remove_file(&cfspi_spi_driver.driver,
			   &driver_attr_up_tail_align);
	driver_remove_file(&cfspi_spi_driver.driver,
			   &driver_attr_down_head_align);
	driver_remove_file(&cfspi_spi_driver.driver,
			   &driver_attr_down_tail_align);
	driver_remove_file(&cfspi_spi_driver.driver, &driver_attr_frame_align);
	/* Unregister platform driver. */
	platform_driver_unregister(&cfspi_spi_driver);
	/* Destroy debugfs root directory. */
	driver_debugfs_remove();
}

static int __init cfspi_init_module(void)
{
	int result;

	/* Initialize spin lock. */
	spin_lock_init(&cfspi_list_lock);

	/* Register platform driver. */
	result = platform_driver_register(&cfspi_spi_driver);
	if (result) {
		printk(KERN_ERR "Could not register platform SPI driver.\n");
		goto err_dev_register;
	}

	/* Create sysfs files. */
	result =
	    driver_create_file(&cfspi_spi_driver.driver,
			       &driver_attr_up_head_align);
	if (result) {
		printk(KERN_ERR "Sysfs creation failed 1.\n");
		goto err_create_up_head_align;
	}

	result =
	    driver_create_file(&cfspi_spi_driver.driver,
			       &driver_attr_up_tail_align);
	if (result) {
		printk(KERN_ERR "Sysfs creation failed 2.\n");
		goto err_create_up_tail_align;
	}

	result =
	    driver_create_file(&cfspi_spi_driver.driver,
			       &driver_attr_down_head_align);
	if (result) {
		printk(KERN_ERR "Sysfs creation failed 3.\n");
		goto err_create_down_head_align;
	}

	result =
	    driver_create_file(&cfspi_spi_driver.driver,
			       &driver_attr_down_tail_align);
	if (result) {
		printk(KERN_ERR "Sysfs creation failed 4.\n");
		goto err_create_down_tail_align;
	}

	result =
	    driver_create_file(&cfspi_spi_driver.driver,
			       &driver_attr_frame_align);
	if (result) {
		printk(KERN_ERR "Sysfs creation failed 5.\n");
		goto err_create_frame_align;
	}
	driver_debugfs_create();
	return result;

 err_create_frame_align:
	driver_remove_file(&cfspi_spi_driver.driver,
			   &driver_attr_down_tail_align);
 err_create_down_tail_align:
	driver_remove_file(&cfspi_spi_driver.driver,
			   &driver_attr_down_head_align);
 err_create_down_head_align:
	driver_remove_file(&cfspi_spi_driver.driver,
			   &driver_attr_up_tail_align);
 err_create_up_tail_align:
	driver_remove_file(&cfspi_spi_driver.driver,
			   &driver_attr_up_head_align);
 err_create_up_head_align:
	platform_driver_unregister(&cfspi_spi_driver);
 err_dev_register:
	return result;
}

module_init(cfspi_init_module);
module_exit(cfspi_exit_module);
