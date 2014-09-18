/*********************************************************************
 *
 *	vlsi_ir.c:	VLSI82C147 PCI IrDA controller driver for Linux
 *
 *	Copyright (c) 2001-2003 Martin Diehl
 *
 *	This program is free software; you can redistribute it and/or 
 *	modify it under the terms of the GNU General Public License as 
 *	published by the Free Software Foundation; either version 2 of 
 *	the License, or (at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License 
 *	along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 ********************************************************************/

#include <linux/module.h>
 
#define DRIVER_NAME 		"vlsi_ir"
#define DRIVER_VERSION		"v0.5"
#define DRIVER_DESCRIPTION	"IrDA SIR/MIR/FIR driver for VLSI 82C147"
#define DRIVER_AUTHOR		"Martin Diehl <info@mdiehl.de>"

MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");

/********************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>
#include <net/irda/wrapper.h>
#include <net/irda/crc.h>

#include "vlsi_ir.h"

/********************************************************/

static /* const */ char drivername[] = DRIVER_NAME;

static const struct pci_device_id vlsi_irda_table[] = {
	{
		.class =        PCI_CLASS_WIRELESS_IRDA << 8,
		.class_mask =	PCI_CLASS_SUBCLASS_MASK << 8, 
		.vendor =       PCI_VENDOR_ID_VLSI,
		.device =       PCI_DEVICE_ID_VLSI_82C147,
		.subvendor = 	PCI_ANY_ID,
		.subdevice =	PCI_ANY_ID,
	},
	{ /* all zeroes */ }
};

MODULE_DEVICE_TABLE(pci, vlsi_irda_table);

/********************************************************/

/*	clksrc: which clock source to be used
 *		0: auto - try PLL, fallback to 40MHz XCLK
 *		1: on-chip 48MHz PLL
 *		2: external 48MHz XCLK
 *		3: external 40MHz XCLK (HP OB-800)
 */

static int clksrc = 0;			/* default is 0(auto) */
module_param(clksrc, int, 0);
MODULE_PARM_DESC(clksrc, "clock input source selection");

/*	ringsize: size of the tx and rx descriptor rings
 *		independent for tx and rx
 *		specify as ringsize=tx[,rx]
 *		allowed values: 4, 8, 16, 32, 64
 *		Due to the IrDA 1.x max. allowed window size=7,
 *		there should be no gain when using rings larger than 8
 */

static int ringsize[] = {8,8};		/* default is tx=8 / rx=8 */
module_param_array(ringsize, int, NULL, 0);
MODULE_PARM_DESC(ringsize, "TX, RX ring descriptor size");

/*	sirpulse: tuning of the SIR pulse width within IrPHY 1.3 limits
 *		0: very short, 1.5us (exception: 6us at 2.4 kbaud)
 *		1: nominal 3/16 bittime width
 *	note: IrDA compliant peer devices should be happy regardless
 *		which one is used. Primary goal is to save some power
 *		on the sender's side - at 9.6kbaud for example the short
 *		pulse width saves more than 90% of the transmitted IR power.
 */

static int sirpulse = 1;		/* default is 3/16 bittime */
module_param(sirpulse, int, 0);
MODULE_PARM_DESC(sirpulse, "SIR pulse width tuning");

/*	qos_mtt_bits: encoded min-turn-time value we require the peer device
 *		 to use before transmitting to us. "Type 1" (per-station)
 *		 bitfield according to IrLAP definition (section 6.6.8)
 *		 Don't know which transceiver is used by my OB800 - the
 *		 pretty common HP HDLS-1100 requires 1 msec - so lets use this.
 */

static int qos_mtt_bits = 0x07;		/* default is 1 ms or more */
module_param(qos_mtt_bits, int, 0);
MODULE_PARM_DESC(qos_mtt_bits, "IrLAP bitfield representing min-turn-time");

/********************************************************/

static void vlsi_reg_debug(unsigned iobase, const char *s)
{
	int	i;

	printk(KERN_DEBUG "%s: ", s);
	for (i = 0; i < 0x20; i++)
		printk("%02x", (unsigned)inb((iobase+i)));
	printk("\n");
}

static void vlsi_ring_debug(struct vlsi_ring *r)
{
	struct ring_descr *rd;
	unsigned i;

	printk(KERN_DEBUG "%s - ring %p / size %u / mask 0x%04x / len %u / dir %d / hw %p\n",
		__func__, r, r->size, r->mask, r->len, r->dir, r->rd[0].hw);
	printk(KERN_DEBUG "%s - head = %d / tail = %d\n", __func__,
		atomic_read(&r->head) & r->mask, atomic_read(&r->tail) & r->mask);
	for (i = 0; i < r->size; i++) {
		rd = &r->rd[i];
		printk(KERN_DEBUG "%s - ring descr %u: ", __func__, i);
		printk("skb=%p data=%p hw=%p\n", rd->skb, rd->buf, rd->hw);
		printk(KERN_DEBUG "%s - hw: status=%02x count=%u addr=0x%08x\n",
			__func__, (unsigned) rd_get_status(rd),
			(unsigned) rd_get_count(rd), (unsigned) rd_get_addr(rd));
	}
}

/********************************************************/

/* needed regardless of CONFIG_PROC_FS */
static struct proc_dir_entry *vlsi_proc_root = NULL;

#ifdef CONFIG_PROC_FS

static void vlsi_proc_pdev(struct seq_file *seq, struct pci_dev *pdev)
{
	unsigned iobase = pci_resource_start(pdev, 0);
	unsigned i;

	seq_printf(seq, "\n%s (vid/did: [%04x:%04x])\n",
		   pci_name(pdev), (int)pdev->vendor, (int)pdev->device);
	seq_printf(seq, "pci-power-state: %u\n", (unsigned) pdev->current_state);
	seq_printf(seq, "resources: irq=%u / io=0x%04x / dma_mask=0x%016Lx\n",
		   pdev->irq, (unsigned)pci_resource_start(pdev, 0), (unsigned long long)pdev->dma_mask);
	seq_printf(seq, "hw registers: ");
	for (i = 0; i < 0x20; i++)
		seq_printf(seq, "%02x", (unsigned)inb((iobase+i)));
	seq_printf(seq, "\n");
}
		
static void vlsi_proc_ndev(struct seq_file *seq, struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = netdev_priv(ndev);
	u8 byte;
	u16 word;
	unsigned delta1, delta2;
	struct timeval now;
	unsigned iobase = ndev->base_addr;

	seq_printf(seq, "\n%s link state: %s / %s / %s / %s\n", ndev->name,
		netif_device_present(ndev) ? "attached" : "detached", 
		netif_running(ndev) ? "running" : "not running",
		netif_carrier_ok(ndev) ? "carrier ok" : "no carrier",
		netif_queue_stopped(ndev) ? "queue stopped" : "queue running");

	if (!netif_running(ndev))
		return;

	seq_printf(seq, "\nhw-state:\n");
	pci_read_config_byte(idev->pdev, VLSI_PCI_IRMISC, &byte);
	seq_printf(seq, "IRMISC:%s%s%s uart%s",
		(byte&IRMISC_IRRAIL) ? " irrail" : "",
		(byte&IRMISC_IRPD) ? " irpd" : "",
		(byte&IRMISC_UARTTST) ? " uarttest" : "",
		(byte&IRMISC_UARTEN) ? "@" : " disabled\n");
	if (byte&IRMISC_UARTEN) {
		seq_printf(seq, "0x%s\n",
			(byte&2) ? ((byte&1) ? "3e8" : "2e8")
				 : ((byte&1) ? "3f8" : "2f8"));
	}
	pci_read_config_byte(idev->pdev, VLSI_PCI_CLKCTL, &byte);
	seq_printf(seq, "CLKCTL: PLL %s%s%s / clock %s / wakeup %s\n",
		(byte&CLKCTL_PD_INV) ? "powered" : "down",
		(byte&CLKCTL_LOCK) ? " locked" : "",
		(byte&CLKCTL_EXTCLK) ? ((byte&CLKCTL_XCKSEL)?" / 40 MHz XCLK":" / 48 MHz XCLK") : "",
		(byte&CLKCTL_CLKSTP) ? "stopped" : "running",
		(byte&CLKCTL_WAKE) ? "enabled" : "disabled");
	pci_read_config_byte(idev->pdev, VLSI_PCI_MSTRPAGE, &byte);
	seq_printf(seq, "MSTRPAGE: 0x%02x\n", (unsigned)byte);

	byte = inb(iobase+VLSI_PIO_IRINTR);
	seq_printf(seq, "IRINTR:%s%s%s%s%s%s%s%s\n",
		(byte&IRINTR_ACTEN) ? " ACTEN" : "",
		(byte&IRINTR_RPKTEN) ? " RPKTEN" : "",
		(byte&IRINTR_TPKTEN) ? " TPKTEN" : "",
		(byte&IRINTR_OE_EN) ? " OE_EN" : "",
		(byte&IRINTR_ACTIVITY) ? " ACTIVITY" : "",
		(byte&IRINTR_RPKTINT) ? " RPKTINT" : "",
		(byte&IRINTR_TPKTINT) ? " TPKTINT" : "",
		(byte&IRINTR_OE_INT) ? " OE_INT" : "");
	word = inw(iobase+VLSI_PIO_RINGPTR);
	seq_printf(seq, "RINGPTR: rx=%u / tx=%u\n", RINGPTR_GET_RX(word), RINGPTR_GET_TX(word));
	word = inw(iobase+VLSI_PIO_RINGBASE);
	seq_printf(seq, "RINGBASE: busmap=0x%08x\n",
		((unsigned)word << 10)|(MSTRPAGE_VALUE<<24));
	word = inw(iobase+VLSI_PIO_RINGSIZE);
	seq_printf(seq, "RINGSIZE: rx=%u / tx=%u\n", RINGSIZE_TO_RXSIZE(word),
		RINGSIZE_TO_TXSIZE(word));

	word = inw(iobase+VLSI_PIO_IRCFG);
	seq_printf(seq, "IRCFG:%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
		(word&IRCFG_LOOP) ? " LOOP" : "",
		(word&IRCFG_ENTX) ? " ENTX" : "",
		(word&IRCFG_ENRX) ? " ENRX" : "",
		(word&IRCFG_MSTR) ? " MSTR" : "",
		(word&IRCFG_RXANY) ? " RXANY" : "",
		(word&IRCFG_CRC16) ? " CRC16" : "",
		(word&IRCFG_FIR) ? " FIR" : "",
		(word&IRCFG_MIR) ? " MIR" : "",
		(word&IRCFG_SIR) ? " SIR" : "",
		(word&IRCFG_SIRFILT) ? " SIRFILT" : "",
		(word&IRCFG_SIRTEST) ? " SIRTEST" : "",
		(word&IRCFG_TXPOL) ? " TXPOL" : "",
		(word&IRCFG_RXPOL) ? " RXPOL" : "");
	word = inw(iobase+VLSI_PIO_IRENABLE);
	seq_printf(seq, "IRENABLE:%s%s%s%s%s%s%s%s\n",
		(word&IRENABLE_PHYANDCLOCK) ? " PHYANDCLOCK" : "",
		(word&IRENABLE_CFGER) ? " CFGERR" : "",
		(word&IRENABLE_FIR_ON) ? " FIR_ON" : "",
		(word&IRENABLE_MIR_ON) ? " MIR_ON" : "",
		(word&IRENABLE_SIR_ON) ? " SIR_ON" : "",
		(word&IRENABLE_ENTXST) ? " ENTXST" : "",
		(word&IRENABLE_ENRXST) ? " ENRXST" : "",
		(word&IRENABLE_CRC16_ON) ? " CRC16_ON" : "");
	word = inw(iobase+VLSI_PIO_PHYCTL);
	seq_printf(seq, "PHYCTL: baud-divisor=%u / pulsewidth=%u / preamble=%u\n",
		(unsigned)PHYCTL_TO_BAUD(word),
		(unsigned)PHYCTL_TO_PLSWID(word),
		(unsigned)PHYCTL_TO_PREAMB(word));
	word = inw(iobase+VLSI_PIO_NPHYCTL);
	seq_printf(seq, "NPHYCTL: baud-divisor=%u / pulsewidth=%u / preamble=%u\n",
		(unsigned)PHYCTL_TO_BAUD(word),
		(unsigned)PHYCTL_TO_PLSWID(word),
		(unsigned)PHYCTL_TO_PREAMB(word));
	word = inw(iobase+VLSI_PIO_MAXPKT);
	seq_printf(seq, "MAXPKT: max. rx packet size = %u\n", word);
	word = inw(iobase+VLSI_PIO_RCVBCNT) & RCVBCNT_MASK;
	seq_printf(seq, "RCVBCNT: rx-fifo filling level = %u\n", word);

	seq_printf(seq, "\nsw-state:\n");
	seq_printf(seq, "IrPHY setup: %d baud - %s encoding\n", idev->baud, 
		(idev->mode==IFF_SIR)?"SIR":((idev->mode==IFF_MIR)?"MIR":"FIR"));
	do_gettimeofday(&now);
	if (now.tv_usec >= idev->last_rx.tv_usec) {
		delta2 = now.tv_usec - idev->last_rx.tv_usec;
		delta1 = 0;
	}
	else {
		delta2 = 1000000 + now.tv_usec - idev->last_rx.tv_usec;
		delta1 = 1;
	}
	seq_printf(seq, "last rx: %lu.%06u sec\n",
		now.tv_sec - idev->last_rx.tv_sec - delta1, delta2);	

	seq_printf(seq, "RX: packets=%lu / bytes=%lu / errors=%lu / dropped=%lu",
		ndev->stats.rx_packets, ndev->stats.rx_bytes, ndev->stats.rx_errors,
		ndev->stats.rx_dropped);
	seq_printf(seq, " / overrun=%lu / length=%lu / frame=%lu / crc=%lu\n",
		ndev->stats.rx_over_errors, ndev->stats.rx_length_errors,
		ndev->stats.rx_frame_errors, ndev->stats.rx_crc_errors);
	seq_printf(seq, "TX: packets=%lu / bytes=%lu / errors=%lu / dropped=%lu / fifo=%lu\n",
		ndev->stats.tx_packets, ndev->stats.tx_bytes, ndev->stats.tx_errors,
		ndev->stats.tx_dropped, ndev->stats.tx_fifo_errors);

}
		
static void vlsi_proc_ring(struct seq_file *seq, struct vlsi_ring *r)
{
	struct ring_descr *rd;
	unsigned i, j;
	int h, t;

	seq_printf(seq, "size %u / mask 0x%04x / len %u / dir %d / hw %p\n",
		r->size, r->mask, r->len, r->dir, r->rd[0].hw);
	h = atomic_read(&r->head) & r->mask;
	t = atomic_read(&r->tail) & r->mask;
	seq_printf(seq, "head = %d / tail = %d ", h, t);
	if (h == t)
		seq_printf(seq, "(empty)\n");
	else {
		if (((t+1)&r->mask) == h)
			seq_printf(seq, "(full)\n");
		else
			seq_printf(seq, "(level = %d)\n", ((unsigned)(t-h) & r->mask)); 
		rd = &r->rd[h];
		j = (unsigned) rd_get_count(rd);
		seq_printf(seq, "current: rd = %d / status = %02x / len = %u\n",
				h, (unsigned)rd_get_status(rd), j);
		if (j > 0) {
			seq_printf(seq, "   data:");
			if (j > 20)
				j = 20;
			for (i = 0; i < j; i++)
				seq_printf(seq, " %02x", (unsigned)((unsigned char *)rd->buf)[i]);
			seq_printf(seq, "\n");
		}
	}
	for (i = 0; i < r->size; i++) {
		rd = &r->rd[i];
		seq_printf(seq, "> ring descr %u: ", i);
		seq_printf(seq, "skb=%p data=%p hw=%p\n", rd->skb, rd->buf, rd->hw);
		seq_printf(seq, "  hw: status=%02x count=%u busaddr=0x%08x\n",
			(unsigned) rd_get_status(rd),
			(unsigned) rd_get_count(rd), (unsigned) rd_get_addr(rd));
	}
}

static int vlsi_seq_show(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	vlsi_irda_dev_t *idev = netdev_priv(ndev);
	unsigned long flags;

	seq_printf(seq, "\n%s %s\n\n", DRIVER_NAME, DRIVER_VERSION);
	seq_printf(seq, "clksrc: %s\n", 
		(clksrc>=2) ? ((clksrc==3)?"40MHz XCLK":"48MHz XCLK")
			    : ((clksrc==1)?"48MHz PLL":"autodetect"));
	seq_printf(seq, "ringsize: tx=%d / rx=%d\n",
		ringsize[0], ringsize[1]);
	seq_printf(seq, "sirpulse: %s\n", (sirpulse)?"3/16 bittime":"short");
	seq_printf(seq, "qos_mtt_bits: 0x%02x\n", (unsigned)qos_mtt_bits);

	spin_lock_irqsave(&idev->lock, flags);
	if (idev->pdev != NULL) {
		vlsi_proc_pdev(seq, idev->pdev);

		if (idev->pdev->current_state == 0)
			vlsi_proc_ndev(seq, ndev);
		else
			seq_printf(seq, "\nPCI controller down - resume_ok = %d\n",
				idev->resume_ok);
		if (netif_running(ndev) && idev->rx_ring && idev->tx_ring) {
			seq_printf(seq, "\n--------- RX ring -----------\n\n");
			vlsi_proc_ring(seq, idev->rx_ring);
			seq_printf(seq, "\n--------- TX ring -----------\n\n");
			vlsi_proc_ring(seq, idev->tx_ring);
		}
	}
	seq_printf(seq, "\n");
	spin_unlock_irqrestore(&idev->lock, flags);

	return 0;
}

static int vlsi_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, vlsi_seq_show, PDE_DATA(inode));
}

static const struct file_operations vlsi_proc_fops = {
	.owner	 = THIS_MODULE,
	.open    = vlsi_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

#define VLSI_PROC_FOPS		(&vlsi_proc_fops)

#else	/* CONFIG_PROC_FS */
#define VLSI_PROC_FOPS		NULL
#endif

/********************************************************/

static struct vlsi_ring *vlsi_alloc_ring(struct pci_dev *pdev, struct ring_descr_hw *hwmap,
						unsigned size, unsigned len, int dir)
{
	struct vlsi_ring *r;
	struct ring_descr *rd;
	unsigned	i, j;
	dma_addr_t	busaddr;

	if (!size  ||  ((size-1)&size)!=0)	/* must be >0 and power of 2 */
		return NULL;

	r = kmalloc(sizeof(*r) + size * sizeof(struct ring_descr), GFP_KERNEL);
	if (!r)
		return NULL;
	memset(r, 0, sizeof(*r));

	r->pdev = pdev;
	r->dir = dir;
	r->len = len;
	r->rd = (struct ring_descr *)(r+1);
	r->mask = size - 1;
	r->size = size;
	atomic_set(&r->head, 0);
	atomic_set(&r->tail, 0);

	for (i = 0; i < size; i++) {
		rd = r->rd + i;
		memset(rd, 0, sizeof(*rd));
		rd->hw = hwmap + i;
		rd->buf = kmalloc(len, GFP_KERNEL|GFP_DMA);
		if (rd->buf == NULL ||
		    !(busaddr = pci_map_single(pdev, rd->buf, len, dir))) {
			if (rd->buf) {
				IRDA_ERROR("%s: failed to create PCI-MAP for %p",
					   __func__, rd->buf);
				kfree(rd->buf);
				rd->buf = NULL;
			}
			for (j = 0; j < i; j++) {
				rd = r->rd + j;
				busaddr = rd_get_addr(rd);
				rd_set_addr_status(rd, 0, 0);
				if (busaddr)
					pci_unmap_single(pdev, busaddr, len, dir);
				kfree(rd->buf);
				rd->buf = NULL;
			}
			kfree(r);
			return NULL;
		}
		rd_set_addr_status(rd, busaddr, 0);
		/* initially, the dma buffer is owned by the CPU */
		rd->skb = NULL;
	}
	return r;
}

static int vlsi_free_ring(struct vlsi_ring *r)
{
	struct ring_descr *rd;
	unsigned	i;
	dma_addr_t	busaddr;

	for (i = 0; i < r->size; i++) {
		rd = r->rd + i;
		if (rd->skb)
			dev_kfree_skb_any(rd->skb);
		busaddr = rd_get_addr(rd);
		rd_set_addr_status(rd, 0, 0);
		if (busaddr)
			pci_unmap_single(r->pdev, busaddr, r->len, r->dir);
		kfree(rd->buf);
	}
	kfree(r);
	return 0;
}

static int vlsi_create_hwif(vlsi_irda_dev_t *idev)
{
	char 			*ringarea;
	struct ring_descr_hw	*hwmap;

	idev->virtaddr = NULL;
	idev->busaddr = 0;

	ringarea = pci_zalloc_consistent(idev->pdev, HW_RING_AREA_SIZE,
					 &idev->busaddr);
	if (!ringarea) {
		IRDA_ERROR("%s: insufficient memory for descriptor rings\n",
			   __func__);
		goto out;
	}

	hwmap = (struct ring_descr_hw *)ringarea;
	idev->rx_ring = vlsi_alloc_ring(idev->pdev, hwmap, ringsize[1],
					XFER_BUF_SIZE, PCI_DMA_FROMDEVICE);
	if (idev->rx_ring == NULL)
		goto out_unmap;

	hwmap += MAX_RING_DESCR;
	idev->tx_ring = vlsi_alloc_ring(idev->pdev, hwmap, ringsize[0],
					XFER_BUF_SIZE, PCI_DMA_TODEVICE);
	if (idev->tx_ring == NULL)
		goto out_free_rx;

	idev->virtaddr = ringarea;
	return 0;

out_free_rx:
	vlsi_free_ring(idev->rx_ring);
out_unmap:
	idev->rx_ring = idev->tx_ring = NULL;
	pci_free_consistent(idev->pdev, HW_RING_AREA_SIZE, ringarea, idev->busaddr);
	idev->busaddr = 0;
out:
	return -ENOMEM;
}

static int vlsi_destroy_hwif(vlsi_irda_dev_t *idev)
{
	vlsi_free_ring(idev->rx_ring);
	vlsi_free_ring(idev->tx_ring);
	idev->rx_ring = idev->tx_ring = NULL;

	if (idev->busaddr)
		pci_free_consistent(idev->pdev,HW_RING_AREA_SIZE,idev->virtaddr,idev->busaddr);

	idev->virtaddr = NULL;
	idev->busaddr = 0;

	return 0;
}

/********************************************************/

static int vlsi_process_rx(struct vlsi_ring *r, struct ring_descr *rd)
{
	u16		status;
	int		crclen, len = 0;
	struct sk_buff	*skb;
	int		ret = 0;
	struct net_device *ndev = pci_get_drvdata(r->pdev);
	vlsi_irda_dev_t *idev = netdev_priv(ndev);

	pci_dma_sync_single_for_cpu(r->pdev, rd_get_addr(rd), r->len, r->dir);
	/* dma buffer now owned by the CPU */
	status = rd_get_status(rd);
	if (status & RD_RX_ERROR) {
		if (status & RD_RX_OVER)  
			ret |= VLSI_RX_OVER;
		if (status & RD_RX_LENGTH)  
			ret |= VLSI_RX_LENGTH;
		if (status & RD_RX_PHYERR)  
			ret |= VLSI_RX_FRAME;
		if (status & RD_RX_CRCERR)  
			ret |= VLSI_RX_CRC;
		goto done;
	}

	len = rd_get_count(rd);
	crclen = (idev->mode==IFF_FIR) ? sizeof(u32) : sizeof(u16);
	len -= crclen;		/* remove trailing CRC */
	if (len <= 0) {
		IRDA_DEBUG(0, "%s: strange frame (len=%d)\n", __func__, len);
		ret |= VLSI_RX_DROP;
		goto done;
	}

	if (idev->mode == IFF_SIR) {	/* hw checks CRC in MIR, FIR mode */

		/* rd->buf is a streaming PCI_DMA_FROMDEVICE map. Doing the
		 * endian-adjustment there just in place will dirty a cache line
		 * which belongs to the map and thus we must be sure it will
		 * get flushed before giving the buffer back to hardware.
		 * vlsi_fill_rx() will do this anyway - but here we rely on.
		 */
		le16_to_cpus(rd->buf+len);
		if (irda_calc_crc16(INIT_FCS,rd->buf,len+crclen) != GOOD_FCS) {
			IRDA_DEBUG(0, "%s: crc error\n", __func__);
			ret |= VLSI_RX_CRC;
			goto done;
		}
	}

	if (!rd->skb) {
		IRDA_WARNING("%s: rx packet lost\n", __func__);
		ret |= VLSI_RX_DROP;
		goto done;
	}

	skb = rd->skb;
	rd->skb = NULL;
	skb->dev = ndev;
	memcpy(skb_put(skb,len), rd->buf, len);
	skb_reset_mac_header(skb);
	if (in_interrupt())
		netif_rx(skb);
	else
		netif_rx_ni(skb);

done:
	rd_set_status(rd, 0);
	rd_set_count(rd, 0);
	/* buffer still owned by CPU */

	return (ret) ? -ret : len;
}

static void vlsi_fill_rx(struct vlsi_ring *r)
{
	struct ring_descr *rd;

	for (rd = ring_last(r); rd != NULL; rd = ring_put(r)) {
		if (rd_is_active(rd)) {
			IRDA_WARNING("%s: driver bug: rx descr race with hw\n",
				     __func__);
			vlsi_ring_debug(r);
			break;
		}
		if (!rd->skb) {
			rd->skb = dev_alloc_skb(IRLAP_SKB_ALLOCSIZE);
			if (rd->skb) {
				skb_reserve(rd->skb,1);
				rd->skb->protocol = htons(ETH_P_IRDA);
			}
			else
				break;	/* probably not worth logging? */
		}
		/* give dma buffer back to busmaster */
		pci_dma_sync_single_for_device(r->pdev, rd_get_addr(rd), r->len, r->dir);
		rd_activate(rd);
	}
}

static void vlsi_rx_interrupt(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = netdev_priv(ndev);
	struct vlsi_ring *r = idev->rx_ring;
	struct ring_descr *rd;
	int ret;

	for (rd = ring_first(r); rd != NULL; rd = ring_get(r)) {

		if (rd_is_active(rd))
			break;

		ret = vlsi_process_rx(r, rd);

		if (ret < 0) {
			ret = -ret;
			ndev->stats.rx_errors++;
			if (ret & VLSI_RX_DROP)  
				ndev->stats.rx_dropped++;
			if (ret & VLSI_RX_OVER)  
				ndev->stats.rx_over_errors++;
			if (ret & VLSI_RX_LENGTH)  
				ndev->stats.rx_length_errors++;
			if (ret & VLSI_RX_FRAME)  
				ndev->stats.rx_frame_errors++;
			if (ret & VLSI_RX_CRC)  
				ndev->stats.rx_crc_errors++;
		}
		else if (ret > 0) {
			ndev->stats.rx_packets++;
			ndev->stats.rx_bytes += ret;
		}
	}

	do_gettimeofday(&idev->last_rx); /* remember "now" for later mtt delay */

	vlsi_fill_rx(r);

	if (ring_first(r) == NULL) {
		/* we are in big trouble, if this should ever happen */
		IRDA_ERROR("%s: rx ring exhausted!\n", __func__);
		vlsi_ring_debug(r);
	}
	else
		outw(0, ndev->base_addr+VLSI_PIO_PROMPT);
}

/* caller must have stopped the controller from busmastering */

static void vlsi_unarm_rx(vlsi_irda_dev_t *idev)
{
	struct net_device *ndev = pci_get_drvdata(idev->pdev);
	struct vlsi_ring *r = idev->rx_ring;
	struct ring_descr *rd;
	int ret;

	for (rd = ring_first(r); rd != NULL; rd = ring_get(r)) {

		ret = 0;
		if (rd_is_active(rd)) {
			rd_set_status(rd, 0);
			if (rd_get_count(rd)) {
				IRDA_DEBUG(0, "%s - dropping rx packet\n", __func__);
				ret = -VLSI_RX_DROP;
			}
			rd_set_count(rd, 0);
			pci_dma_sync_single_for_cpu(r->pdev, rd_get_addr(rd), r->len, r->dir);
			if (rd->skb) {
				dev_kfree_skb_any(rd->skb);
				rd->skb = NULL;
			}
		}
		else
			ret = vlsi_process_rx(r, rd);

		if (ret < 0) {
			ret = -ret;
			ndev->stats.rx_errors++;
			if (ret & VLSI_RX_DROP)  
				ndev->stats.rx_dropped++;
			if (ret & VLSI_RX_OVER)  
				ndev->stats.rx_over_errors++;
			if (ret & VLSI_RX_LENGTH)  
				ndev->stats.rx_length_errors++;
			if (ret & VLSI_RX_FRAME)  
				ndev->stats.rx_frame_errors++;
			if (ret & VLSI_RX_CRC)  
				ndev->stats.rx_crc_errors++;
		}
		else if (ret > 0) {
			ndev->stats.rx_packets++;
			ndev->stats.rx_bytes += ret;
		}
	}
}

/********************************************************/

static int vlsi_process_tx(struct vlsi_ring *r, struct ring_descr *rd)
{
	u16		status;
	int		len;
	int		ret;

	pci_dma_sync_single_for_cpu(r->pdev, rd_get_addr(rd), r->len, r->dir);
	/* dma buffer now owned by the CPU */
	status = rd_get_status(rd);
	if (status & RD_TX_UNDRN)
		ret = VLSI_TX_FIFO;
	else
		ret = 0;
	rd_set_status(rd, 0);

	if (rd->skb) {
		len = rd->skb->len;
		dev_kfree_skb_any(rd->skb);
		rd->skb = NULL;
	}
	else	/* tx-skb already freed? - should never happen */
		len = rd_get_count(rd);		/* incorrect for SIR! (due to wrapping) */

	rd_set_count(rd, 0);
	/* dma buffer still owned by the CPU */

	return (ret) ? -ret : len;
}

static int vlsi_set_baud(vlsi_irda_dev_t *idev, unsigned iobase)
{
	u16 nphyctl;
	u16 config;
	unsigned mode;
	int	ret;
	int	baudrate;
	int	fifocnt;

	baudrate = idev->new_baud;
	IRDA_DEBUG(2, "%s: %d -> %d\n", __func__, idev->baud, idev->new_baud);
	if (baudrate == 4000000) {
		mode = IFF_FIR;
		config = IRCFG_FIR;
		nphyctl = PHYCTL_FIR;
	}
	else if (baudrate == 1152000) {
		mode = IFF_MIR;
		config = IRCFG_MIR | IRCFG_CRC16;
		nphyctl = PHYCTL_MIR(clksrc==3);
	}
	else {
		mode = IFF_SIR;
		config = IRCFG_SIR | IRCFG_SIRFILT  | IRCFG_RXANY;
		switch(baudrate) {
			default:
				IRDA_WARNING("%s: undefined baudrate %d - fallback to 9600!\n",
					     __func__, baudrate);
				baudrate = 9600;
				/* fallthru */
			case 2400:
			case 9600:
			case 19200:
			case 38400:
			case 57600:
			case 115200:
				nphyctl = PHYCTL_SIR(baudrate,sirpulse,clksrc==3);
				break;
		}
	}
	config |= IRCFG_MSTR | IRCFG_ENRX;

	fifocnt = inw(iobase+VLSI_PIO_RCVBCNT) & RCVBCNT_MASK;
	if (fifocnt != 0) {
		IRDA_DEBUG(0, "%s: rx fifo not empty(%d)\n", __func__, fifocnt);
	}

	outw(0, iobase+VLSI_PIO_IRENABLE);
	outw(config, iobase+VLSI_PIO_IRCFG);
	outw(nphyctl, iobase+VLSI_PIO_NPHYCTL);
	wmb();
	outw(IRENABLE_PHYANDCLOCK, iobase+VLSI_PIO_IRENABLE);
	mb();

	udelay(1);	/* chip applies IRCFG on next rising edge of its 8MHz clock */

	/* read back settings for validation */

	config = inw(iobase+VLSI_PIO_IRENABLE) & IRENABLE_MASK;

	if (mode == IFF_FIR)
		config ^= IRENABLE_FIR_ON;
	else if (mode == IFF_MIR)
		config ^= (IRENABLE_MIR_ON|IRENABLE_CRC16_ON);
	else
		config ^= IRENABLE_SIR_ON;

	if (config != (IRENABLE_PHYANDCLOCK|IRENABLE_ENRXST)) {
		IRDA_WARNING("%s: failed to set %s mode!\n", __func__,
			(mode==IFF_SIR)?"SIR":((mode==IFF_MIR)?"MIR":"FIR"));
		ret = -1;
	}
	else {
		if (inw(iobase+VLSI_PIO_PHYCTL) != nphyctl) {
			IRDA_WARNING("%s: failed to apply baudrate %d\n",
				     __func__, baudrate);
			ret = -1;
		}
		else {
			idev->mode = mode;
			idev->baud = baudrate;
			idev->new_baud = 0;
			ret = 0;
		}
	}

	if (ret)
		vlsi_reg_debug(iobase,__func__);

	return ret;
}

static netdev_tx_t vlsi_hard_start_xmit(struct sk_buff *skb,
					      struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = netdev_priv(ndev);
	struct vlsi_ring	*r = idev->tx_ring;
	struct ring_descr *rd;
	unsigned long flags;
	unsigned iobase = ndev->base_addr;
	u8 status;
	u16 config;
	int mtt;
	int len, speed;
	struct timeval  now, ready;
	char *msg = NULL;

	speed = irda_get_next_speed(skb);
	spin_lock_irqsave(&idev->lock, flags);
	if (speed != -1  &&  speed != idev->baud) {
		netif_stop_queue(ndev);
		idev->new_baud = speed;
		status = RD_TX_CLRENTX;  /* stop tx-ring after this frame */
	}
	else
		status = 0;

	if (skb->len == 0) {
		/* handle zero packets - should be speed change */
		if (status == 0) {
			msg = "bogus zero-length packet";
			goto drop_unlock;
		}

		/* due to the completely asynch tx operation we might have
		 * IrLAP racing with the hardware here, f.e. if the controller
		 * is just sending the last packet with current speed while
		 * the LAP is already switching the speed using synchronous
		 * len=0 packet. Immediate execution would lead to hw lockup
		 * requiring a powercycle to reset. Good candidate to trigger
		 * this is the final UA:RSP packet after receiving a DISC:CMD
		 * when getting the LAP down.
		 * Note that we are not protected by the queue_stop approach
		 * because the final UA:RSP arrives _without_ request to apply
		 * new-speed-after-this-packet - hence the driver doesn't know
		 * this was the last packet and doesn't stop the queue. So the
		 * forced switch to default speed from LAP gets through as fast
		 * as only some 10 usec later while the UA:RSP is still processed
		 * by the hardware and we would get screwed.
		 */

		if (ring_first(idev->tx_ring) == NULL) {
			/* no race - tx-ring already empty */
			vlsi_set_baud(idev, iobase);
			netif_wake_queue(ndev);
		}
		else
			;
			/* keep the speed change pending like it would
			 * for any len>0 packet. tx completion interrupt
			 * will apply it when the tx ring becomes empty.
			 */
		spin_unlock_irqrestore(&idev->lock, flags);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* sanity checks - simply drop the packet */

	rd = ring_last(r);
	if (!rd) {
		msg = "ring full, but queue wasn't stopped";
		goto drop_unlock;
	}

	if (rd_is_active(rd)) {
		msg = "entry still owned by hw";
		goto drop_unlock;
	}

	if (!rd->buf) {
		msg = "tx ring entry without pci buffer";
		goto drop_unlock;
	}

	if (rd->skb) {
		msg = "ring entry with old skb still attached";
		goto drop_unlock;
	}

	/* no need for serialization or interrupt disable during mtt */
	spin_unlock_irqrestore(&idev->lock, flags);

	if ((mtt = irda_get_mtt(skb)) > 0) {
	
		ready.tv_usec = idev->last_rx.tv_usec + mtt;
		ready.tv_sec = idev->last_rx.tv_sec;
		if (ready.tv_usec >= 1000000) {
			ready.tv_usec -= 1000000;
			ready.tv_sec++;		/* IrLAP 1.1: mtt always < 1 sec */
		}
		for(;;) {
			do_gettimeofday(&now);
			if (now.tv_sec > ready.tv_sec ||
			    (now.tv_sec==ready.tv_sec && now.tv_usec>=ready.tv_usec))
			    	break;
			udelay(100);
			/* must not sleep here - called under netif_tx_lock! */
		}
	}

	/* tx buffer already owned by CPU due to pci_dma_sync_single_for_cpu()
	 * after subsequent tx-completion
	 */

	if (idev->mode == IFF_SIR) {
		status |= RD_TX_DISCRC;		/* no hw-crc creation */
		len = async_wrap_skb(skb, rd->buf, r->len);

		/* Some rare worst case situation in SIR mode might lead to
		 * potential buffer overflow. The wrapper detects this, returns
		 * with a shortened frame (without FCS/EOF) but doesn't provide
		 * any error indication about the invalid packet which we are
		 * going to transmit.
		 * Therefore we log if the buffer got filled to the point, where the
		 * wrapper would abort, i.e. when there are less than 5 bytes left to
		 * allow appending the FCS/EOF.
		 */

		if (len >= r->len-5)
			 IRDA_WARNING("%s: possible buffer overflow with SIR wrapping!\n",
				      __func__);
	}
	else {
		/* hw deals with MIR/FIR mode wrapping */
		status |= RD_TX_PULSE;		/* send 2 us highspeed indication pulse */
		len = skb->len;
		if (len > r->len) {
			msg = "frame exceeds tx buffer length";
			goto drop;
		}
		else
			skb_copy_from_linear_data(skb, rd->buf, len);
	}

	rd->skb = skb;			/* remember skb for tx-complete stats */

	rd_set_count(rd, len);
	rd_set_status(rd, status);	/* not yet active! */

	/* give dma buffer back to busmaster-hw (flush caches to make
	 * CPU-driven changes visible from the pci bus).
	 */

	pci_dma_sync_single_for_device(r->pdev, rd_get_addr(rd), r->len, r->dir);

/*	Switching to TX mode here races with the controller
 *	which may stop TX at any time when fetching an inactive descriptor
 *	or one with CLR_ENTX set. So we switch on TX only, if TX was not running
 *	_after_ the new descriptor was activated on the ring. This ensures
 *	we will either find TX already stopped or we can be sure, there
 *	will be a TX-complete interrupt even if the chip stopped doing
 *	TX just after we found it still running. The ISR will then find
 *	the non-empty ring and restart TX processing. The enclosing
 *	spinlock provides the correct serialization to prevent race with isr.
 */

	spin_lock_irqsave(&idev->lock,flags);

	rd_activate(rd);

	if (!(inw(iobase+VLSI_PIO_IRENABLE) & IRENABLE_ENTXST)) {
		int fifocnt;

		fifocnt = inw(ndev->base_addr+VLSI_PIO_RCVBCNT) & RCVBCNT_MASK;
		if (fifocnt != 0) {
			IRDA_DEBUG(0, "%s: rx fifo not empty(%d)\n", __func__, fifocnt);
		}

		config = inw(iobase+VLSI_PIO_IRCFG);
		mb();
		outw(config | IRCFG_ENTX, iobase+VLSI_PIO_IRCFG);
		wmb();
		outw(0, iobase+VLSI_PIO_PROMPT);
	}

	if (ring_put(r) == NULL) {
		netif_stop_queue(ndev);
		IRDA_DEBUG(3, "%s: tx ring full - queue stopped\n", __func__);
	}
	spin_unlock_irqrestore(&idev->lock, flags);

	return NETDEV_TX_OK;

drop_unlock:
	spin_unlock_irqrestore(&idev->lock, flags);
drop:
	IRDA_WARNING("%s: dropping packet - %s\n", __func__, msg);
	dev_kfree_skb_any(skb);
	ndev->stats.tx_errors++;
	ndev->stats.tx_dropped++;
	/* Don't even think about returning NET_XMIT_DROP (=1) here!
	 * In fact any retval!=0 causes the packet scheduler to requeue the
	 * packet for later retry of transmission - which isn't exactly
	 * what we want after we've just called dev_kfree_skb_any ;-)
	 */
	return NETDEV_TX_OK;
}

static void vlsi_tx_interrupt(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = netdev_priv(ndev);
	struct vlsi_ring	*r = idev->tx_ring;
	struct ring_descr	*rd;
	unsigned	iobase;
	int	ret;
	u16	config;

	for (rd = ring_first(r); rd != NULL; rd = ring_get(r)) {

		if (rd_is_active(rd))
			break;

		ret = vlsi_process_tx(r, rd);

		if (ret < 0) {
			ret = -ret;
			ndev->stats.tx_errors++;
			if (ret & VLSI_TX_DROP)
				ndev->stats.tx_dropped++;
			if (ret & VLSI_TX_FIFO)
				ndev->stats.tx_fifo_errors++;
		}
		else if (ret > 0){
			ndev->stats.tx_packets++;
			ndev->stats.tx_bytes += ret;
		}
	}

	iobase = ndev->base_addr;

	if (idev->new_baud  &&  rd == NULL)	/* tx ring empty and speed change pending */
		vlsi_set_baud(idev, iobase);

	config = inw(iobase+VLSI_PIO_IRCFG);
	if (rd == NULL)			/* tx ring empty: re-enable rx */
		outw((config & ~IRCFG_ENTX) | IRCFG_ENRX, iobase+VLSI_PIO_IRCFG);

	else if (!(inw(iobase+VLSI_PIO_IRENABLE) & IRENABLE_ENTXST)) {
		int fifocnt;

		fifocnt = inw(iobase+VLSI_PIO_RCVBCNT) & RCVBCNT_MASK;
		if (fifocnt != 0) {
			IRDA_DEBUG(0, "%s: rx fifo not empty(%d)\n",
				__func__, fifocnt);
		}
		outw(config | IRCFG_ENTX, iobase+VLSI_PIO_IRCFG);
	}

	outw(0, iobase+VLSI_PIO_PROMPT);

	if (netif_queue_stopped(ndev)  &&  !idev->new_baud) {
		netif_wake_queue(ndev);
		IRDA_DEBUG(3, "%s: queue awoken\n", __func__);
	}
}

/* caller must have stopped the controller from busmastering */

static void vlsi_unarm_tx(vlsi_irda_dev_t *idev)
{
	struct net_device *ndev = pci_get_drvdata(idev->pdev);
	struct vlsi_ring *r = idev->tx_ring;
	struct ring_descr *rd;
	int ret;

	for (rd = ring_first(r); rd != NULL; rd = ring_get(r)) {

		ret = 0;
		if (rd_is_active(rd)) {
			rd_set_status(rd, 0);
			rd_set_count(rd, 0);
			pci_dma_sync_single_for_cpu(r->pdev, rd_get_addr(rd), r->len, r->dir);
			if (rd->skb) {
				dev_kfree_skb_any(rd->skb);
				rd->skb = NULL;
			}
			IRDA_DEBUG(0, "%s - dropping tx packet\n", __func__);
			ret = -VLSI_TX_DROP;
		}
		else
			ret = vlsi_process_tx(r, rd);

		if (ret < 0) {
			ret = -ret;
			ndev->stats.tx_errors++;
			if (ret & VLSI_TX_DROP)
				ndev->stats.tx_dropped++;
			if (ret & VLSI_TX_FIFO)
				ndev->stats.tx_fifo_errors++;
		}
		else if (ret > 0){
			ndev->stats.tx_packets++;
			ndev->stats.tx_bytes += ret;
		}
	}

}

/********************************************************/

static int vlsi_start_clock(struct pci_dev *pdev)
{
	u8	clkctl, lock;
	int	i, count;

	if (clksrc < 2) { /* auto or PLL: try PLL */
		clkctl = CLKCTL_PD_INV | CLKCTL_CLKSTP;
		pci_write_config_byte(pdev, VLSI_PCI_CLKCTL, clkctl);

		/* procedure to detect PLL lock synchronisation:
		 * after 0.5 msec initial delay we expect to find 3 PLL lock
		 * indications within 10 msec for successful PLL detection.
		 */
		udelay(500);
		count = 0;
		for (i = 500; i <= 10000; i += 50) { /* max 10 msec */
			pci_read_config_byte(pdev, VLSI_PCI_CLKCTL, &lock);
			if (lock&CLKCTL_LOCK) {
				if (++count >= 3)
					break;
			}
			udelay(50);
		}
		if (count < 3) {
			if (clksrc == 1) { /* explicitly asked for PLL hence bail out */
				IRDA_ERROR("%s: no PLL or failed to lock!\n",
					   __func__);
				clkctl = CLKCTL_CLKSTP;
				pci_write_config_byte(pdev, VLSI_PCI_CLKCTL, clkctl);
				return -1;
			}
			else			/* was: clksrc=0(auto) */
				clksrc = 3;	/* fallback to 40MHz XCLK (OB800) */

			IRDA_DEBUG(0, "%s: PLL not locked, fallback to clksrc=%d\n",
				__func__, clksrc);
		}
		else
			clksrc = 1;	/* got successful PLL lock */
	}

	if (clksrc != 1) {
		/* we get here if either no PLL detected in auto-mode or
		   an external clock source was explicitly specified */

		clkctl = CLKCTL_EXTCLK | CLKCTL_CLKSTP;
		if (clksrc == 3)
			clkctl |= CLKCTL_XCKSEL;	
		pci_write_config_byte(pdev, VLSI_PCI_CLKCTL, clkctl);

		/* no way to test for working XCLK */
	}
	else
		pci_read_config_byte(pdev, VLSI_PCI_CLKCTL, &clkctl);

	/* ok, now going to connect the chip with the clock source */

	clkctl &= ~CLKCTL_CLKSTP;
	pci_write_config_byte(pdev, VLSI_PCI_CLKCTL, clkctl);

	return 0;
}

static void vlsi_stop_clock(struct pci_dev *pdev)
{
	u8	clkctl;

	/* disconnect chip from clock source */
	pci_read_config_byte(pdev, VLSI_PCI_CLKCTL, &clkctl);
	clkctl |= CLKCTL_CLKSTP;
	pci_write_config_byte(pdev, VLSI_PCI_CLKCTL, clkctl);

	/* disable all clock sources */
	clkctl &= ~(CLKCTL_EXTCLK | CLKCTL_PD_INV);
	pci_write_config_byte(pdev, VLSI_PCI_CLKCTL, clkctl);
}

/********************************************************/

/* writing all-zero to the VLSI PCI IO register area seems to prevent
 * some occasional situations where the hardware fails (symptoms are 
 * what appears as stalled tx/rx state machines, i.e. everything ok for
 * receive or transmit but hw makes no progress or is unable to access
 * the bus memory locations).
 * Best place to call this is immediately after/before the internal clock
 * gets started/stopped.
 */

static inline void vlsi_clear_regs(unsigned iobase)
{
	unsigned	i;
	const unsigned	chip_io_extent = 32;

	for (i = 0; i < chip_io_extent; i += sizeof(u16))
		outw(0, iobase + i);
}

static int vlsi_init_chip(struct pci_dev *pdev)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	vlsi_irda_dev_t *idev = netdev_priv(ndev);
	unsigned	iobase;
	u16 ptr;

	/* start the clock and clean the registers */

	if (vlsi_start_clock(pdev)) {
		IRDA_ERROR("%s: no valid clock source\n", __func__);
		return -1;
	}
	iobase = ndev->base_addr;
	vlsi_clear_regs(iobase);

	outb(IRINTR_INT_MASK, iobase+VLSI_PIO_IRINTR); /* w/c pending IRQ, disable all INT */

	outw(0, iobase+VLSI_PIO_IRENABLE);	/* disable IrPHY-interface */

	/* disable everything, particularly IRCFG_MSTR - (also resetting the RING_PTR) */

	outw(0, iobase+VLSI_PIO_IRCFG);
	wmb();

	outw(MAX_PACKET_LENGTH, iobase+VLSI_PIO_MAXPKT);  /* max possible value=0x0fff */

	outw(BUS_TO_RINGBASE(idev->busaddr), iobase+VLSI_PIO_RINGBASE);

	outw(TX_RX_TO_RINGSIZE(idev->tx_ring->size, idev->rx_ring->size),
		iobase+VLSI_PIO_RINGSIZE);	

	ptr = inw(iobase+VLSI_PIO_RINGPTR);
	atomic_set(&idev->rx_ring->head, RINGPTR_GET_RX(ptr));
	atomic_set(&idev->rx_ring->tail, RINGPTR_GET_RX(ptr));
	atomic_set(&idev->tx_ring->head, RINGPTR_GET_TX(ptr));
	atomic_set(&idev->tx_ring->tail, RINGPTR_GET_TX(ptr));

	vlsi_set_baud(idev, iobase);	/* idev->new_baud used as provided by caller */

	outb(IRINTR_INT_MASK, iobase+VLSI_PIO_IRINTR);	/* just in case - w/c pending IRQ's */
	wmb();

	/* DO NOT BLINDLY ENABLE IRINTR_ACTEN!
	 * basically every received pulse fires an ACTIVITY-INT
	 * leading to >>1000 INT's per second instead of few 10
	 */

	outb(IRINTR_RPKTEN|IRINTR_TPKTEN, iobase+VLSI_PIO_IRINTR);

	return 0;
}

static int vlsi_start_hw(vlsi_irda_dev_t *idev)
{
	struct pci_dev *pdev = idev->pdev;
	struct net_device *ndev = pci_get_drvdata(pdev);
	unsigned iobase = ndev->base_addr;
	u8 byte;

	/* we don't use the legacy UART, disable its address decoding */

	pci_read_config_byte(pdev, VLSI_PCI_IRMISC, &byte);
	byte &= ~(IRMISC_UARTEN | IRMISC_UARTTST);
	pci_write_config_byte(pdev, VLSI_PCI_IRMISC, byte);

	/* enable PCI busmaster access to our 16MB page */

	pci_write_config_byte(pdev, VLSI_PCI_MSTRPAGE, MSTRPAGE_VALUE);
	pci_set_master(pdev);

	if (vlsi_init_chip(pdev) < 0) {
		pci_disable_device(pdev);
		return -1;
	}

	vlsi_fill_rx(idev->rx_ring);

	do_gettimeofday(&idev->last_rx);	/* first mtt may start from now on */

	outw(0, iobase+VLSI_PIO_PROMPT);	/* kick hw state machine */

	return 0;
}

static int vlsi_stop_hw(vlsi_irda_dev_t *idev)
{
	struct pci_dev *pdev = idev->pdev;
	struct net_device *ndev = pci_get_drvdata(pdev);
	unsigned iobase = ndev->base_addr;
	unsigned long flags;

	spin_lock_irqsave(&idev->lock,flags);
	outw(0, iobase+VLSI_PIO_IRENABLE);
	outw(0, iobase+VLSI_PIO_IRCFG);			/* disable everything */

	/* disable and w/c irqs */
	outb(0, iobase+VLSI_PIO_IRINTR);
	wmb();
	outb(IRINTR_INT_MASK, iobase+VLSI_PIO_IRINTR);
	spin_unlock_irqrestore(&idev->lock,flags);

	vlsi_unarm_tx(idev);
	vlsi_unarm_rx(idev);

	vlsi_clear_regs(iobase);
	vlsi_stop_clock(pdev);

	pci_disable_device(pdev);

	return 0;
}

/**************************************************************/

static void vlsi_tx_timeout(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = netdev_priv(ndev);


	vlsi_reg_debug(ndev->base_addr, __func__);
	vlsi_ring_debug(idev->tx_ring);

	if (netif_running(ndev))
		netif_stop_queue(ndev);

	vlsi_stop_hw(idev);

	/* now simply restart the whole thing */

	if (!idev->new_baud)
		idev->new_baud = idev->baud;		/* keep current baudrate */

	if (vlsi_start_hw(idev))
		IRDA_ERROR("%s: failed to restart hw - %s(%s) unusable!\n",
			   __func__, pci_name(idev->pdev), ndev->name);
	else
		netif_start_queue(ndev);
}

static int vlsi_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	vlsi_irda_dev_t *idev = netdev_priv(ndev);
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	unsigned long flags;
	u16 fifocnt;
	int ret = 0;

	switch (cmd) {
		case SIOCSBANDWIDTH:
			if (!capable(CAP_NET_ADMIN)) {
				ret = -EPERM;
				break;
			}
			spin_lock_irqsave(&idev->lock, flags);
			idev->new_baud = irq->ifr_baudrate;
			/* when called from userland there might be a minor race window here
			 * if the stack tries to change speed concurrently - which would be
			 * pretty strange anyway with the userland having full control...
			 */
			vlsi_set_baud(idev, ndev->base_addr);
			spin_unlock_irqrestore(&idev->lock, flags);
			break;
		case SIOCSMEDIABUSY:
			if (!capable(CAP_NET_ADMIN)) {
				ret = -EPERM;
				break;
			}
			irda_device_set_media_busy(ndev, TRUE);
			break;
		case SIOCGRECEIVING:
			/* the best we can do: check whether there are any bytes in rx fifo.
			 * The trustable window (in case some data arrives just afterwards)
			 * may be as short as 1usec or so at 4Mbps.
			 */
			fifocnt = inw(ndev->base_addr+VLSI_PIO_RCVBCNT) & RCVBCNT_MASK;
			irq->ifr_receiving = (fifocnt!=0) ? 1 : 0;
			break;
		default:
			IRDA_WARNING("%s: notsupp - cmd=%04x\n",
				     __func__, cmd);
			ret = -EOPNOTSUPP;
	}	
	
	return ret;
}

/********************************************************/

static irqreturn_t vlsi_interrupt(int irq, void *dev_instance)
{
	struct net_device *ndev = dev_instance;
	vlsi_irda_dev_t *idev = netdev_priv(ndev);
	unsigned	iobase;
	u8		irintr;
	int 		boguscount = 5;
	unsigned long	flags;
	int		handled = 0;

	iobase = ndev->base_addr;
	spin_lock_irqsave(&idev->lock,flags);
	do {
		irintr = inb(iobase+VLSI_PIO_IRINTR);
		mb();
		outb(irintr, iobase+VLSI_PIO_IRINTR);	/* acknowledge asap */

		if (!(irintr&=IRINTR_INT_MASK))		/* not our INT - probably shared */
			break;

		handled = 1;

		if (unlikely(!(irintr & ~IRINTR_ACTIVITY)))
			break;				/* nothing todo if only activity */

		if (irintr&IRINTR_RPKTINT)
			vlsi_rx_interrupt(ndev);

		if (irintr&IRINTR_TPKTINT)
			vlsi_tx_interrupt(ndev);

	} while (--boguscount > 0);
	spin_unlock_irqrestore(&idev->lock,flags);

	if (boguscount <= 0)
		IRDA_MESSAGE("%s: too much work in interrupt!\n",
			     __func__);
	return IRQ_RETVAL(handled);
}

/********************************************************/

static int vlsi_open(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = netdev_priv(ndev);
	int	err = -EAGAIN;
	char	hwname[32];

	if (pci_request_regions(idev->pdev, drivername)) {
		IRDA_WARNING("%s: io resource busy\n", __func__);
		goto errout;
	}
	ndev->base_addr = pci_resource_start(idev->pdev,0);
	ndev->irq = idev->pdev->irq;

	/* under some rare occasions the chip apparently comes up with
	 * IRQ's pending. We better w/c pending IRQ and disable them all
	 */

	outb(IRINTR_INT_MASK, ndev->base_addr+VLSI_PIO_IRINTR);

	if (request_irq(ndev->irq, vlsi_interrupt, IRQF_SHARED,
			drivername, ndev)) {
		IRDA_WARNING("%s: couldn't get IRQ: %d\n",
			     __func__, ndev->irq);
		goto errout_io;
	}

	if ((err = vlsi_create_hwif(idev)) != 0)
		goto errout_irq;

	sprintf(hwname, "VLSI-FIR @ 0x%04x", (unsigned)ndev->base_addr);
	idev->irlap = irlap_open(ndev,&idev->qos,hwname);
	if (!idev->irlap)
		goto errout_free_ring;

	do_gettimeofday(&idev->last_rx);  /* first mtt may start from now on */

	idev->new_baud = 9600;		/* start with IrPHY using 9600(SIR) mode */

	if ((err = vlsi_start_hw(idev)) != 0)
		goto errout_close_irlap;

	netif_start_queue(ndev);

	IRDA_MESSAGE("%s: device %s operational\n", __func__, ndev->name);

	return 0;

errout_close_irlap:
	irlap_close(idev->irlap);
errout_free_ring:
	vlsi_destroy_hwif(idev);
errout_irq:
	free_irq(ndev->irq,ndev);
errout_io:
	pci_release_regions(idev->pdev);
errout:
	return err;
}

static int vlsi_close(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = netdev_priv(ndev);

	netif_stop_queue(ndev);

	if (idev->irlap)
		irlap_close(idev->irlap);
	idev->irlap = NULL;

	vlsi_stop_hw(idev);

	vlsi_destroy_hwif(idev);

	free_irq(ndev->irq,ndev);

	pci_release_regions(idev->pdev);

	IRDA_MESSAGE("%s: device %s stopped\n", __func__, ndev->name);

	return 0;
}

static const struct net_device_ops vlsi_netdev_ops = {
	.ndo_open       = vlsi_open,
	.ndo_stop       = vlsi_close,
	.ndo_start_xmit = vlsi_hard_start_xmit,
	.ndo_do_ioctl   = vlsi_ioctl,
	.ndo_tx_timeout = vlsi_tx_timeout,
};

static int vlsi_irda_init(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = netdev_priv(ndev);
	struct pci_dev *pdev = idev->pdev;

	ndev->irq = pdev->irq;
	ndev->base_addr = pci_resource_start(pdev,0);

	/* PCI busmastering
	 * see include file for details why we need these 2 masks, in this order!
	 */

	if (pci_set_dma_mask(pdev,DMA_MASK_USED_BY_HW) ||
	    pci_set_dma_mask(pdev,DMA_MASK_MSTRPAGE)) {
		IRDA_ERROR("%s: aborting due to PCI BM-DMA address limitations\n", __func__);
		return -1;
	}

	irda_init_max_qos_capabilies(&idev->qos);

	/* the VLSI82C147 does not support 576000! */

	idev->qos.baud_rate.bits = IR_2400 | IR_9600
		| IR_19200 | IR_38400 | IR_57600 | IR_115200
		| IR_1152000 | (IR_4000000 << 8);

	idev->qos.min_turn_time.bits = qos_mtt_bits;

	irda_qos_bits_to_value(&idev->qos);

	/* currently no public media definitions for IrDA */

	ndev->flags |= IFF_PORTSEL | IFF_AUTOMEDIA;
	ndev->if_port = IF_PORT_UNKNOWN;
 
	ndev->netdev_ops = &vlsi_netdev_ops;
	ndev->watchdog_timeo  = 500*HZ/1000;	/* max. allowed turn time for IrLAP */

	SET_NETDEV_DEV(ndev, &pdev->dev);

	return 0;
}	

/**************************************************************/

static int
vlsi_irda_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net_device	*ndev;
	vlsi_irda_dev_t		*idev;

	if (pci_enable_device(pdev))
		goto out;
	else
		pdev->current_state = 0; /* hw must be running now */

	IRDA_MESSAGE("%s: IrDA PCI controller %s detected\n",
		     drivername, pci_name(pdev));

	if ( !pci_resource_start(pdev,0) ||
	     !(pci_resource_flags(pdev,0) & IORESOURCE_IO) ) {
		IRDA_ERROR("%s: bar 0 invalid", __func__);
		goto out_disable;
	}

	ndev = alloc_irdadev(sizeof(*idev));
	if (ndev==NULL) {
		IRDA_ERROR("%s: Unable to allocate device memory.\n",
			   __func__);
		goto out_disable;
	}

	idev = netdev_priv(ndev);

	spin_lock_init(&idev->lock);
	mutex_init(&idev->mtx);
	mutex_lock(&idev->mtx);
	idev->pdev = pdev;

	if (vlsi_irda_init(ndev) < 0)
		goto out_freedev;

	if (register_netdev(ndev) < 0) {
		IRDA_ERROR("%s: register_netdev failed\n", __func__);
		goto out_freedev;
	}

	if (vlsi_proc_root != NULL) {
		struct proc_dir_entry *ent;

		ent = proc_create_data(ndev->name, S_IFREG|S_IRUGO,
				       vlsi_proc_root, VLSI_PROC_FOPS, ndev);
		if (!ent) {
			IRDA_WARNING("%s: failed to create proc entry\n",
				     __func__);
		} else {
			proc_set_size(ent, 0);
		}
		idev->proc_entry = ent;
	}
	IRDA_MESSAGE("%s: registered device %s\n", drivername, ndev->name);

	pci_set_drvdata(pdev, ndev);
	mutex_unlock(&idev->mtx);

	return 0;

out_freedev:
	mutex_unlock(&idev->mtx);
	free_netdev(ndev);
out_disable:
	pci_disable_device(pdev);
out:
	return -ENODEV;
}

static void vlsi_irda_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	vlsi_irda_dev_t *idev;

	if (!ndev) {
		IRDA_ERROR("%s: lost netdevice?\n", drivername);
		return;
	}

	unregister_netdev(ndev);

	idev = netdev_priv(ndev);
	mutex_lock(&idev->mtx);
	if (idev->proc_entry) {
		remove_proc_entry(ndev->name, vlsi_proc_root);
		idev->proc_entry = NULL;
	}
	mutex_unlock(&idev->mtx);

	free_netdev(ndev);

	IRDA_MESSAGE("%s: %s removed\n", drivername, pci_name(pdev));
}

#ifdef CONFIG_PM

/* The Controller doesn't provide PCI PM capabilities as defined by PCI specs.
 * Some of the Linux PCI-PM code however depends on this, for example in
 * pci_set_power_state(). So we have to take care to perform the required
 * operations on our own (particularly reflecting the pdev->current_state)
 * otherwise we might get cheated by pci-pm.
 */


static int vlsi_irda_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	vlsi_irda_dev_t *idev;

	if (!ndev) {
		IRDA_ERROR("%s - %s: no netdevice\n",
			   __func__, pci_name(pdev));
		return 0;
	}
	idev = netdev_priv(ndev);
	mutex_lock(&idev->mtx);
	if (pdev->current_state != 0) {			/* already suspended */
		if (state.event > pdev->current_state) {	/* simply go deeper */
			pci_set_power_state(pdev, pci_choose_state(pdev, state));
			pdev->current_state = state.event;
		}
		else
			IRDA_ERROR("%s - %s: invalid suspend request %u -> %u\n", __func__, pci_name(pdev), pdev->current_state, state.event);
		mutex_unlock(&idev->mtx);
		return 0;
	}

	if (netif_running(ndev)) {
		netif_device_detach(ndev);
		vlsi_stop_hw(idev);
		pci_save_state(pdev);
		if (!idev->new_baud)
			/* remember speed settings to restore on resume */
			idev->new_baud = idev->baud;
	}

	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	pdev->current_state = state.event;
	idev->resume_ok = 1;
	mutex_unlock(&idev->mtx);
	return 0;
}

static int vlsi_irda_resume(struct pci_dev *pdev)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	vlsi_irda_dev_t	*idev;

	if (!ndev) {
		IRDA_ERROR("%s - %s: no netdevice\n",
			   __func__, pci_name(pdev));
		return 0;
	}
	idev = netdev_priv(ndev);
	mutex_lock(&idev->mtx);
	if (pdev->current_state == 0) {
		mutex_unlock(&idev->mtx);
		IRDA_WARNING("%s - %s: already resumed\n",
			     __func__, pci_name(pdev));
		return 0;
	}
	
	pci_set_power_state(pdev, PCI_D0);
	pdev->current_state = PM_EVENT_ON;

	if (!idev->resume_ok) {
		/* should be obsolete now - but used to happen due to:
		 * - pci layer initially setting pdev->current_state = 4 (unknown)
		 * - pci layer did not walk the save_state-tree (might be APM problem)
		 *   so we could not refuse to suspend from undefined state
		 * - vlsi_irda_suspend detected invalid state and refused to save
		 *   configuration for resume - but was too late to stop suspending
		 * - vlsi_irda_resume got screwed when trying to resume from garbage
		 *
		 * now we explicitly set pdev->current_state = 0 after enabling the
		 * device and independently resume_ok should catch any garbage config.
		 */
		IRDA_WARNING("%s - hm, nothing to resume?\n", __func__);
		mutex_unlock(&idev->mtx);
		return 0;
	}

	if (netif_running(ndev)) {
		pci_restore_state(pdev);
		vlsi_start_hw(idev);
		netif_device_attach(ndev);
	}
	idev->resume_ok = 0;
	mutex_unlock(&idev->mtx);
	return 0;
}

#endif /* CONFIG_PM */

/*********************************************************/

static struct pci_driver vlsi_irda_driver = {
	.name		= drivername,
	.id_table	= vlsi_irda_table,
	.probe		= vlsi_irda_probe,
	.remove		= vlsi_irda_remove,
#ifdef CONFIG_PM
	.suspend	= vlsi_irda_suspend,
	.resume		= vlsi_irda_resume,
#endif
};

#define PROC_DIR ("driver/" DRIVER_NAME)

static int __init vlsi_mod_init(void)
{
	int	i, ret;

	if (clksrc < 0  ||  clksrc > 3) {
		IRDA_ERROR("%s: invalid clksrc=%d\n", drivername, clksrc);
		return -1;
	}

	for (i = 0; i < 2; i++) {
		switch(ringsize[i]) {
			case 4:
			case 8:
			case 16:
			case 32:
			case 64:
				break;
			default:
				IRDA_WARNING("%s: invalid %s ringsize %d, using default=8", drivername, (i)?"rx":"tx", ringsize[i]);
				ringsize[i] = 8;
				break;
		}
	} 

	sirpulse = !!sirpulse;

	/* proc_mkdir returns NULL if !CONFIG_PROC_FS.
	 * Failure to create the procfs entry is handled like running
	 * without procfs - it's not required for the driver to work.
	 */
	vlsi_proc_root = proc_mkdir(PROC_DIR, NULL);

	ret = pci_register_driver(&vlsi_irda_driver);

	if (ret && vlsi_proc_root)
		remove_proc_entry(PROC_DIR, NULL);
	return ret;

}

static void __exit vlsi_mod_exit(void)
{
	pci_unregister_driver(&vlsi_irda_driver);
	if (vlsi_proc_root)
		remove_proc_entry(PROC_DIR, NULL);
}

module_init(vlsi_mod_init);
module_exit(vlsi_mod_exit);
