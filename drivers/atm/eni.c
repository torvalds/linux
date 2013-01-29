/* drivers/atm/eni.c - Efficient Networks ENI155P device driver */
 
/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */
 

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/sonet.h>
#include <linux/skbuff.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/uio.h>
#include <linux/init.h>
#include <linux/atm_eni.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/atomic.h>
#include <asm/uaccess.h>
#include <asm/string.h>
#include <asm/byteorder.h>

#include "tonga.h"
#include "midway.h"
#include "suni.h"
#include "eni.h"

#if !defined(__i386__) && !defined(__x86_64__)
#ifndef ioremap_nocache
#define ioremap_nocache(X,Y) ioremap(X,Y)
#endif 
#endif

/*
 * TODO:
 *
 * Show stoppers
 *  none
 *
 * Minor
 *  - OAM support
 *  - fix bugs listed below
 */

/*
 * KNOWN BUGS:
 *
 * - may run into JK-JK bug and deadlock
 * - should allocate UBR channel first
 * - buffer space allocation algorithm is stupid
 *   (RX: should be maxSDU+maxdelay*rate
 *    TX: should be maxSDU+min(maxSDU,maxdelay*rate) )
 * - doesn't support OAM cells
 * - eni_put_free may hang if not putting memory fragments that _complete_
 *   2^n block (never happens in real life, though)
 */


#if 0
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif


#ifndef CONFIG_ATM_ENI_TUNE_BURST
#define CONFIG_ATM_ENI_BURST_TX_8W
#define CONFIG_ATM_ENI_BURST_RX_4W
#endif


#ifndef CONFIG_ATM_ENI_DEBUG


#define NULLCHECK(x)

#define EVENT(s,a,b)


static void event_dump(void)
{
}


#else


/* 
 * NULL pointer checking
 */

#define NULLCHECK(x) \
	if ((unsigned long) (x) < 0x30) \
		printk(KERN_CRIT #x "==0x%lx\n",(unsigned long) (x))

/*
 * Very extensive activity logging. Greatly improves bug detection speed but
 * costs a few Mbps if enabled.
 */

#define EV 64

static const char *ev[EV];
static unsigned long ev_a[EV],ev_b[EV];
static int ec = 0;


static void EVENT(const char *s,unsigned long a,unsigned long b)
{
	ev[ec] = s; 
	ev_a[ec] = a;
	ev_b[ec] = b;
	ec = (ec+1) % EV;
}


static void event_dump(void)
{
	int n,i;

	for (n = 0; n < EV; n++) {
		i = (ec+n) % EV;
		printk(KERN_NOTICE);
		printk(ev[i] ? ev[i] : "(null)",ev_a[i],ev_b[i]);
	}
}


#endif /* CONFIG_ATM_ENI_DEBUG */


/*
 * NExx   must not be equal at end
 * EExx   may be equal at end
 * xxPJOK verify validity of pointer jumps
 * xxPMOK operating on a circular buffer of "c" words
 */

#define NEPJOK(a0,a1,b) \
    ((a0) < (a1) ? (b) <= (a0) || (b) > (a1) : (b) <= (a0) && (b) > (a1))
#define EEPJOK(a0,a1,b) \
    ((a0) < (a1) ? (b) < (a0) || (b) >= (a1) : (b) < (a0) && (b) >= (a1))
#define NEPMOK(a0,d,b,c) NEPJOK(a0,(a0+d) & (c-1),b)
#define EEPMOK(a0,d,b,c) EEPJOK(a0,(a0+d) & (c-1),b)


static int tx_complete = 0,dma_complete = 0,queued = 0,requeued = 0,
  backlogged = 0,rx_enqueued = 0,rx_dequeued = 0,pushed = 0,submitted = 0,
  putting = 0;

static struct atm_dev *eni_boards = NULL;

/* Read/write registers on card */
#define eni_in(r)	readl(eni_dev->reg+(r)*4)
#define eni_out(v,r)	writel((v),eni_dev->reg+(r)*4)


/*-------------------------------- utilities --------------------------------*/


static void dump_mem(struct eni_dev *eni_dev)
{
	int i;

	for (i = 0; i < eni_dev->free_len; i++)
		printk(KERN_DEBUG "  %d: %p %d\n",i,
		    eni_dev->free_list[i].start,
		    1 << eni_dev->free_list[i].order);
}


static void dump(struct atm_dev *dev)
{
	struct eni_dev *eni_dev;

	int i;

	eni_dev = ENI_DEV(dev);
	printk(KERN_NOTICE "Free memory\n");
	dump_mem(eni_dev);
	printk(KERN_NOTICE "TX buffers\n");
	for (i = 0; i < NR_CHAN; i++)
		if (eni_dev->tx[i].send)
			printk(KERN_NOTICE "  TX %d @ %p: %ld\n",i,
			    eni_dev->tx[i].send,eni_dev->tx[i].words*4);
	printk(KERN_NOTICE "RX buffers\n");
	for (i = 0; i < 1024; i++)
		if (eni_dev->rx_map[i] && ENI_VCC(eni_dev->rx_map[i])->rx)
			printk(KERN_NOTICE "  RX %d @ %p: %ld\n",i,
			    ENI_VCC(eni_dev->rx_map[i])->recv,
			    ENI_VCC(eni_dev->rx_map[i])->words*4);
	printk(KERN_NOTICE "----\n");
}


static void eni_put_free(struct eni_dev *eni_dev, void __iomem *start,
    unsigned long size)
{
	struct eni_free *list;
	int len,order;

	DPRINTK("init 0x%lx+%ld(0x%lx)\n",start,size,size);
	start += eni_dev->base_diff;
	list = eni_dev->free_list;
	len = eni_dev->free_len;
	while (size) {
		if (len >= eni_dev->free_list_size) {
			printk(KERN_CRIT "eni_put_free overflow (%p,%ld)\n",
			    start,size);
			break;
		}
		for (order = 0; !(((unsigned long)start | size) & (1 << order)); order++);
		if (MID_MIN_BUF_SIZE > (1 << order)) {
			printk(KERN_CRIT "eni_put_free: order %d too small\n",
			    order);
			break;
		}
		list[len].start = (void __iomem *) start;
		list[len].order = order;
		len++;
		start += 1 << order;
		size -= 1 << order;
	}
	eni_dev->free_len = len;
	/*dump_mem(eni_dev);*/
}


static void __iomem *eni_alloc_mem(struct eni_dev *eni_dev, unsigned long *size)
{
	struct eni_free *list;
	void __iomem *start;
	int len,i,order,best_order,index;

	list = eni_dev->free_list;
	len = eni_dev->free_len;
	if (*size < MID_MIN_BUF_SIZE) *size = MID_MIN_BUF_SIZE;
	if (*size > MID_MAX_BUF_SIZE) return NULL;
	for (order = 0; (1 << order) < *size; order++);
	DPRINTK("trying: %ld->%d\n",*size,order);
	best_order = 65; /* we don't have more than 2^64 of anything ... */
	index = 0; /* silence GCC */
	for (i = 0; i < len; i++)
		if (list[i].order == order) {
			best_order = order;
			index = i;
			break;
		}
		else if (best_order > list[i].order && list[i].order > order) {
				best_order = list[i].order;
				index = i;
			}
	if (best_order == 65) return NULL;
	start = list[index].start-eni_dev->base_diff;
	list[index] = list[--len];
	eni_dev->free_len = len;
	*size = 1 << order;
	eni_put_free(eni_dev,start+*size,(1 << best_order)-*size);
	DPRINTK("%ld bytes (order %d) at 0x%lx\n",*size,order,start);
	memset_io(start,0,*size);       /* never leak data */
	/*dump_mem(eni_dev);*/
	return start;
}


static void eni_free_mem(struct eni_dev *eni_dev, void __iomem *start,
    unsigned long size)
{
	struct eni_free *list;
	int len,i,order;

	start += eni_dev->base_diff;
	list = eni_dev->free_list;
	len = eni_dev->free_len;
	for (order = -1; size; order++) size >>= 1;
	DPRINTK("eni_free_mem: %p+0x%lx (order %d)\n",start,size,order);
	for (i = 0; i < len; i++)
		if (((unsigned long) list[i].start) == ((unsigned long)start^(1 << order)) &&
		    list[i].order == order) {
			DPRINTK("match[%d]: 0x%lx/0x%lx(0x%x), %d/%d\n",i,
			    list[i].start,start,1 << order,list[i].order,order);
			list[i] = list[--len];
			start = (void __iomem *) ((unsigned long) start & ~(unsigned long) (1 << order));
			order++;
			i = -1;
			continue;
		}
	if (len >= eni_dev->free_list_size) {
		printk(KERN_ALERT "eni_free_mem overflow (%p,%d)\n",start,
		    order);
		return;
	}
	list[len].start = start;
	list[len].order = order;
	eni_dev->free_len = len+1;
	/*dump_mem(eni_dev);*/
}


/*----------------------------------- RX ------------------------------------*/


#define ENI_VCC_NOS ((struct atm_vcc *) 1)


static void rx_ident_err(struct atm_vcc *vcc)
{
	struct atm_dev *dev;
	struct eni_dev *eni_dev;
	struct eni_vcc *eni_vcc;

	dev = vcc->dev;
	eni_dev = ENI_DEV(dev);
	/* immediately halt adapter */
	eni_out(eni_in(MID_MC_S) &
	    ~(MID_DMA_ENABLE | MID_TX_ENABLE | MID_RX_ENABLE),MID_MC_S);
	/* dump useful information */
	eni_vcc = ENI_VCC(vcc);
	printk(KERN_ALERT DEV_LABEL "(itf %d): driver error - RX ident "
	    "mismatch\n",dev->number);
	printk(KERN_ALERT "  VCI %d, rxing %d, words %ld\n",vcc->vci,
	    eni_vcc->rxing,eni_vcc->words);
	printk(KERN_ALERT "  host descr 0x%lx, rx pos 0x%lx, descr value "
	    "0x%x\n",eni_vcc->descr,eni_vcc->rx_pos,
	    (unsigned) readl(eni_vcc->recv+eni_vcc->descr*4));
	printk(KERN_ALERT "  last %p, servicing %d\n",eni_vcc->last,
	    eni_vcc->servicing);
	EVENT("---dump ends here---\n",0,0);
	printk(KERN_NOTICE "---recent events---\n");
	event_dump();
	ENI_DEV(dev)->fast = NULL; /* really stop it */
	ENI_DEV(dev)->slow = NULL;
	skb_queue_head_init(&ENI_DEV(dev)->rx_queue);
}


static int do_rx_dma(struct atm_vcc *vcc,struct sk_buff *skb,
    unsigned long skip,unsigned long size,unsigned long eff)
{
	struct eni_dev *eni_dev;
	struct eni_vcc *eni_vcc;
	u32 dma_rd,dma_wr;
	u32 dma[RX_DMA_BUF*2];
	dma_addr_t paddr;
	unsigned long here;
	int i,j;

	eni_dev = ENI_DEV(vcc->dev);
	eni_vcc = ENI_VCC(vcc);
	paddr = 0; /* GCC, shut up */
	if (skb) {
		paddr = pci_map_single(eni_dev->pci_dev,skb->data,skb->len,
		    PCI_DMA_FROMDEVICE);
		ENI_PRV_PADDR(skb) = paddr;
		if (paddr & 3)
			printk(KERN_CRIT DEV_LABEL "(itf %d): VCI %d has "
			    "mis-aligned RX data (0x%lx)\n",vcc->dev->number,
			    vcc->vci,(unsigned long) paddr);
		ENI_PRV_SIZE(skb) = size+skip;
		    /* PDU plus descriptor */
		ATM_SKB(skb)->vcc = vcc;
	}
	j = 0;
	if ((eff && skip) || 1) { /* @@@ actually, skip is always == 1 ... */
		here = (eni_vcc->descr+skip) & (eni_vcc->words-1);
		dma[j++] = (here << MID_DMA_COUNT_SHIFT) | (vcc->vci
		    << MID_DMA_VCI_SHIFT) | MID_DT_JK;
		j++;
	}
	here = (eni_vcc->descr+size+skip) & (eni_vcc->words-1);
	if (!eff) size += skip;
	else {
		unsigned long words;

		if (!size) {
			DPRINTK("strange things happen ...\n");
			EVENT("strange things happen ... (skip=%ld,eff=%ld)\n",
			    size,eff);
		}
		words = eff;
		if (paddr & 15) {
			unsigned long init;

			init = 4-((paddr & 15) >> 2);
			if (init > words) init = words;
			dma[j++] = MID_DT_WORD | (init << MID_DMA_COUNT_SHIFT) |
			    (vcc->vci << MID_DMA_VCI_SHIFT);
			dma[j++] = paddr;
			paddr += init << 2;
			words -= init;
		}
#ifdef CONFIG_ATM_ENI_BURST_RX_16W /* may work with some PCI chipsets ... */
		if (words & ~15) {
			dma[j++] = MID_DT_16W | ((words >> 4) <<
			    MID_DMA_COUNT_SHIFT) | (vcc->vci <<
			    MID_DMA_VCI_SHIFT);
			dma[j++] = paddr;
			paddr += (words & ~15) << 2;
			words &= 15;
		}
#endif
#ifdef CONFIG_ATM_ENI_BURST_RX_8W  /* works only with *some* PCI chipsets ... */
		if (words & ~7) {
			dma[j++] = MID_DT_8W | ((words >> 3) <<
			    MID_DMA_COUNT_SHIFT) | (vcc->vci <<
			    MID_DMA_VCI_SHIFT);
			dma[j++] = paddr;
			paddr += (words & ~7) << 2;
			words &= 7;
		}
#endif
#ifdef CONFIG_ATM_ENI_BURST_RX_4W /* recommended */
		if (words & ~3) {
			dma[j++] = MID_DT_4W | ((words >> 2) <<
			    MID_DMA_COUNT_SHIFT) | (vcc->vci <<
			    MID_DMA_VCI_SHIFT);
			dma[j++] = paddr;
			paddr += (words & ~3) << 2;
			words &= 3;
		}
#endif
#ifdef CONFIG_ATM_ENI_BURST_RX_2W /* probably useless if RX_4W, RX_8W, ... */
		if (words & ~1) {
			dma[j++] = MID_DT_2W | ((words >> 1) <<
			    MID_DMA_COUNT_SHIFT) | (vcc->vci <<
			    MID_DMA_VCI_SHIFT);
			dma[j++] = paddr;
			paddr += (words & ~1) << 2;
			words &= 1;
		}
#endif
		if (words) {
			dma[j++] = MID_DT_WORD | (words << MID_DMA_COUNT_SHIFT)
			    | (vcc->vci << MID_DMA_VCI_SHIFT);
			dma[j++] = paddr;
		}
	}
	if (size != eff) {
		dma[j++] = (here << MID_DMA_COUNT_SHIFT) |
		    (vcc->vci << MID_DMA_VCI_SHIFT) | MID_DT_JK;
		j++;
	}
	if (!j || j > 2*RX_DMA_BUF) {
		printk(KERN_CRIT DEV_LABEL "!j or j too big!!!\n");
		goto trouble;
	}
	dma[j-2] |= MID_DMA_END;
	j = j >> 1;
	dma_wr = eni_in(MID_DMA_WR_RX);
	dma_rd = eni_in(MID_DMA_RD_RX);
	/*
	 * Can I move the dma_wr pointer by 2j+1 positions without overwriting
	 * data that hasn't been read (position of dma_rd) yet ?
	 */
	if (!NEPMOK(dma_wr,j+j+1,dma_rd,NR_DMA_RX)) { /* @@@ +1 is ugly */
		printk(KERN_WARNING DEV_LABEL "(itf %d): RX DMA full\n",
		    vcc->dev->number);
		goto trouble;
	}
        for (i = 0; i < j; i++) {
		writel(dma[i*2],eni_dev->rx_dma+dma_wr*8);
		writel(dma[i*2+1],eni_dev->rx_dma+dma_wr*8+4);
		dma_wr = (dma_wr+1) & (NR_DMA_RX-1);
        }
	if (skb) {
		ENI_PRV_POS(skb) = eni_vcc->descr+size+1;
		skb_queue_tail(&eni_dev->rx_queue,skb);
		eni_vcc->last = skb;
rx_enqueued++;
	}
	eni_vcc->descr = here;
	eni_out(dma_wr,MID_DMA_WR_RX);
	return 0;

trouble:
	if (paddr)
		pci_unmap_single(eni_dev->pci_dev,paddr,skb->len,
		    PCI_DMA_FROMDEVICE);
	if (skb) dev_kfree_skb_irq(skb);
	return -1;
}


static void discard(struct atm_vcc *vcc,unsigned long size)
{
	struct eni_vcc *eni_vcc;

	eni_vcc = ENI_VCC(vcc);
	EVENT("discard (size=%ld)\n",size,0);
	while (do_rx_dma(vcc,NULL,1,size,0)) EVENT("BUSY LOOP",0,0);
	    /* could do a full fallback, but that might be more expensive */
	if (eni_vcc->rxing) ENI_PRV_POS(eni_vcc->last) += size+1;
	else eni_vcc->rx_pos = (eni_vcc->rx_pos+size+1) & (eni_vcc->words-1);
}


/*
 * TODO: should check whether direct copies (without DMA setup, dequeuing on
 * interrupt, etc.) aren't much faster for AAL0
 */

static int rx_aal0(struct atm_vcc *vcc)
{
	struct eni_vcc *eni_vcc;
	unsigned long descr;
	unsigned long length;
	struct sk_buff *skb;

	DPRINTK(">rx_aal0\n");
	eni_vcc = ENI_VCC(vcc);
	descr = readl(eni_vcc->recv+eni_vcc->descr*4);
	if ((descr & MID_RED_IDEN) != (MID_RED_RX_ID << MID_RED_SHIFT)) {
		rx_ident_err(vcc);
		return 1;
	}
	if (descr & MID_RED_T) {
		DPRINTK(DEV_LABEL "(itf %d): trashing empty cell\n",
		    vcc->dev->number);
		length = 0;
		atomic_inc(&vcc->stats->rx_err);
	}
	else {
		length = ATM_CELL_SIZE-1; /* no HEC */
	}
	skb = length ? atm_alloc_charge(vcc,length,GFP_ATOMIC) : NULL;
	if (!skb) {
		discard(vcc,length >> 2);
		return 0;
	}
	skb_put(skb,length);
	skb->tstamp = eni_vcc->timestamp;
	DPRINTK("got len %ld\n",length);
	if (do_rx_dma(vcc,skb,1,length >> 2,length >> 2)) return 1;
	eni_vcc->rxing++;
	return 0;
}


static int rx_aal5(struct atm_vcc *vcc)
{
	struct eni_vcc *eni_vcc;
	unsigned long descr;
	unsigned long size,eff,length;
	struct sk_buff *skb;

	EVENT("rx_aal5\n",0,0);
	DPRINTK(">rx_aal5\n");
	eni_vcc = ENI_VCC(vcc);
	descr = readl(eni_vcc->recv+eni_vcc->descr*4);
	if ((descr & MID_RED_IDEN) != (MID_RED_RX_ID << MID_RED_SHIFT)) {
		rx_ident_err(vcc);
		return 1;
	}
	if (descr & (MID_RED_T | MID_RED_CRC_ERR)) {
		if (descr & MID_RED_T) {
			EVENT("empty cell (descr=0x%lx)\n",descr,0);
			DPRINTK(DEV_LABEL "(itf %d): trashing empty cell\n",
			    vcc->dev->number);
			size = 0;
		}
		else {
			static unsigned long silence = 0;

			if (time_after(jiffies, silence) || silence == 0) {
				printk(KERN_WARNING DEV_LABEL "(itf %d): "
				    "discarding PDU(s) with CRC error\n",
				    vcc->dev->number);
				silence = (jiffies+2*HZ)|1;
			}
			size = (descr & MID_RED_COUNT)*(ATM_CELL_PAYLOAD >> 2);
			EVENT("CRC error (descr=0x%lx,size=%ld)\n",descr,
			    size);
		}
		eff = length = 0;
		atomic_inc(&vcc->stats->rx_err);
	}
	else {
		size = (descr & MID_RED_COUNT)*(ATM_CELL_PAYLOAD >> 2);
		DPRINTK("size=%ld\n",size);
		length = readl(eni_vcc->recv+(((eni_vcc->descr+size-1) &
		    (eni_vcc->words-1)))*4) & 0xffff;
				/* -trailer(2)+header(1) */
		if (length && length <= (size << 2)-8 && length <=
		  ATM_MAX_AAL5_PDU) eff = (length+3) >> 2;
		else {				 /* ^ trailer length (8) */
			EVENT("bad PDU (descr=0x08%lx,length=%ld)\n",descr,
			    length);
			printk(KERN_ERR DEV_LABEL "(itf %d): bad AAL5 PDU "
			    "(VCI=%d,length=%ld,size=%ld (descr 0x%lx))\n",
			    vcc->dev->number,vcc->vci,length,size << 2,descr);
			length = eff = 0;
			atomic_inc(&vcc->stats->rx_err);
		}
	}
	skb = eff ? atm_alloc_charge(vcc,eff << 2,GFP_ATOMIC) : NULL;
	if (!skb) {
		discard(vcc,size);
		return 0;
	}
	skb_put(skb,length);
	DPRINTK("got len %ld\n",length);
	if (do_rx_dma(vcc,skb,1,size,eff)) return 1;
	eni_vcc->rxing++;
	return 0;
}


static inline int rx_vcc(struct atm_vcc *vcc)
{
	void __iomem *vci_dsc;
	unsigned long tmp;
	struct eni_vcc *eni_vcc;

	eni_vcc = ENI_VCC(vcc);
	vci_dsc = ENI_DEV(vcc->dev)->vci+vcc->vci*16;
	EVENT("rx_vcc(1)\n",0,0);
	while (eni_vcc->descr != (tmp = (readl(vci_dsc+4) & MID_VCI_DESCR) >>
	    MID_VCI_DESCR_SHIFT)) {
		EVENT("rx_vcc(2: host dsc=0x%lx, nic dsc=0x%lx)\n",
		    eni_vcc->descr,tmp);
		DPRINTK("CB_DESCR %ld REG_DESCR %d\n",ENI_VCC(vcc)->descr,
		    (((unsigned) readl(vci_dsc+4) & MID_VCI_DESCR) >>
		    MID_VCI_DESCR_SHIFT));
		if (ENI_VCC(vcc)->rx(vcc)) return 1;
	}
	/* clear IN_SERVICE flag */
	writel(readl(vci_dsc) & ~MID_VCI_IN_SERVICE,vci_dsc);
	/*
	 * If new data has arrived between evaluating the while condition and
	 * clearing IN_SERVICE, we wouldn't be notified until additional data
	 * follows. So we have to loop again to be sure.
	 */
	EVENT("rx_vcc(3)\n",0,0);
	while (ENI_VCC(vcc)->descr != (tmp = (readl(vci_dsc+4) & MID_VCI_DESCR)
	    >> MID_VCI_DESCR_SHIFT)) {
		EVENT("rx_vcc(4: host dsc=0x%lx, nic dsc=0x%lx)\n",
		    eni_vcc->descr,tmp);
		DPRINTK("CB_DESCR %ld REG_DESCR %d\n",ENI_VCC(vcc)->descr,
		    (((unsigned) readl(vci_dsc+4) & MID_VCI_DESCR) >>
		    MID_VCI_DESCR_SHIFT));
		if (ENI_VCC(vcc)->rx(vcc)) return 1;
	}
	return 0;
}


static void poll_rx(struct atm_dev *dev)
{
	struct eni_dev *eni_dev;
	struct atm_vcc *curr;

	eni_dev = ENI_DEV(dev);
	while ((curr = eni_dev->fast)) {
		EVENT("poll_rx.fast\n",0,0);
		if (rx_vcc(curr)) return;
		eni_dev->fast = ENI_VCC(curr)->next;
		ENI_VCC(curr)->next = ENI_VCC_NOS;
		barrier();
		ENI_VCC(curr)->servicing--;
	}
	while ((curr = eni_dev->slow)) {
		EVENT("poll_rx.slow\n",0,0);
		if (rx_vcc(curr)) return;
		eni_dev->slow = ENI_VCC(curr)->next;
		ENI_VCC(curr)->next = ENI_VCC_NOS;
		barrier();
		ENI_VCC(curr)->servicing--;
	}
}


static void get_service(struct atm_dev *dev)
{
	struct eni_dev *eni_dev;
	struct atm_vcc *vcc;
	unsigned long vci;

	DPRINTK(">get_service\n");
	eni_dev = ENI_DEV(dev);
	while (eni_in(MID_SERV_WRITE) != eni_dev->serv_read) {
		vci = readl(eni_dev->service+eni_dev->serv_read*4);
		eni_dev->serv_read = (eni_dev->serv_read+1) & (NR_SERVICE-1);
		vcc = eni_dev->rx_map[vci & 1023];
		if (!vcc) {
			printk(KERN_CRIT DEV_LABEL "(itf %d): VCI %ld not "
			    "found\n",dev->number,vci);
			continue; /* nasty but we try to go on anyway */
			/* @@@ nope, doesn't work */
		}
		EVENT("getting from service\n",0,0);
		if (ENI_VCC(vcc)->next != ENI_VCC_NOS) {
			EVENT("double service\n",0,0);
			DPRINTK("Grr, servicing VCC %ld twice\n",vci);
			continue;
		}
		ENI_VCC(vcc)->timestamp = ktime_get_real();
		ENI_VCC(vcc)->next = NULL;
		if (vcc->qos.rxtp.traffic_class == ATM_CBR) {
			if (eni_dev->fast)
				ENI_VCC(eni_dev->last_fast)->next = vcc;
			else eni_dev->fast = vcc;
			eni_dev->last_fast = vcc;
		}
		else {
			if (eni_dev->slow)
				ENI_VCC(eni_dev->last_slow)->next = vcc;
			else eni_dev->slow = vcc;
			eni_dev->last_slow = vcc;
		}
putting++;
		ENI_VCC(vcc)->servicing++;
	}
}


static void dequeue_rx(struct atm_dev *dev)
{
	struct eni_dev *eni_dev;
	struct eni_vcc *eni_vcc;
	struct atm_vcc *vcc;
	struct sk_buff *skb;
	void __iomem *vci_dsc;
	int first;

	eni_dev = ENI_DEV(dev);
	first = 1;
	while (1) {
		skb = skb_dequeue(&eni_dev->rx_queue);
		if (!skb) {
			if (first) {
				DPRINTK(DEV_LABEL "(itf %d): RX but not "
				    "rxing\n",dev->number);
				EVENT("nothing to dequeue\n",0,0);
			}
			break;
		}
		EVENT("dequeued (size=%ld,pos=0x%lx)\n",ENI_PRV_SIZE(skb),
		    ENI_PRV_POS(skb));
rx_dequeued++;
		vcc = ATM_SKB(skb)->vcc;
		eni_vcc = ENI_VCC(vcc);
		first = 0;
		vci_dsc = eni_dev->vci+vcc->vci*16;
		if (!EEPMOK(eni_vcc->rx_pos,ENI_PRV_SIZE(skb),
		    (readl(vci_dsc+4) & MID_VCI_READ) >> MID_VCI_READ_SHIFT,
		    eni_vcc->words)) {
			EVENT("requeuing\n",0,0);
			skb_queue_head(&eni_dev->rx_queue,skb);
			break;
		}
		eni_vcc->rxing--;
		eni_vcc->rx_pos = ENI_PRV_POS(skb) & (eni_vcc->words-1);
		pci_unmap_single(eni_dev->pci_dev,ENI_PRV_PADDR(skb),skb->len,
		    PCI_DMA_TODEVICE);
		if (!skb->len) dev_kfree_skb_irq(skb);
		else {
			EVENT("pushing (len=%ld)\n",skb->len,0);
			if (vcc->qos.aal == ATM_AAL0)
				*(unsigned long *) skb->data =
				    ntohl(*(unsigned long *) skb->data);
			memset(skb->cb,0,sizeof(struct eni_skb_prv));
			vcc->push(vcc,skb);
			pushed++;
		}
		atomic_inc(&vcc->stats->rx);
	}
	wake_up(&eni_dev->rx_wait);
}


static int open_rx_first(struct atm_vcc *vcc)
{
	struct eni_dev *eni_dev;
	struct eni_vcc *eni_vcc;
	unsigned long size;

	DPRINTK("open_rx_first\n");
	eni_dev = ENI_DEV(vcc->dev);
	eni_vcc = ENI_VCC(vcc);
	eni_vcc->rx = NULL;
	if (vcc->qos.rxtp.traffic_class == ATM_NONE) return 0;
	size = vcc->qos.rxtp.max_sdu*eni_dev->rx_mult/100;
	if (size > MID_MAX_BUF_SIZE && vcc->qos.rxtp.max_sdu <=
	    MID_MAX_BUF_SIZE)
		size = MID_MAX_BUF_SIZE;
	eni_vcc->recv = eni_alloc_mem(eni_dev,&size);
	DPRINTK("rx at 0x%lx\n",eni_vcc->recv);
	eni_vcc->words = size >> 2;
	if (!eni_vcc->recv) return -ENOBUFS;
	eni_vcc->rx = vcc->qos.aal == ATM_AAL5 ? rx_aal5 : rx_aal0;
	eni_vcc->descr = 0;
	eni_vcc->rx_pos = 0;
	eni_vcc->rxing = 0;
	eni_vcc->servicing = 0;
	eni_vcc->next = ENI_VCC_NOS;
	return 0;
}


static int open_rx_second(struct atm_vcc *vcc)
{
	void __iomem *here;
	struct eni_dev *eni_dev;
	struct eni_vcc *eni_vcc;
	unsigned long size;
	int order;

	DPRINTK("open_rx_second\n");
	eni_dev = ENI_DEV(vcc->dev);
	eni_vcc = ENI_VCC(vcc);
	if (!eni_vcc->rx) return 0;
	/* set up VCI descriptor */
	here = eni_dev->vci+vcc->vci*16;
	DPRINTK("loc 0x%x\n",(unsigned) (eni_vcc->recv-eni_dev->ram)/4);
	size = eni_vcc->words >> 8;
	for (order = -1; size; order++) size >>= 1;
	writel(0,here+4); /* descr, read = 0 */
	writel(0,here+8); /* write, state, count = 0 */
	if (eni_dev->rx_map[vcc->vci])
		printk(KERN_CRIT DEV_LABEL "(itf %d): BUG - VCI %d already "
		    "in use\n",vcc->dev->number,vcc->vci);
	eni_dev->rx_map[vcc->vci] = vcc; /* now it counts */
	writel(((vcc->qos.aal != ATM_AAL5 ? MID_MODE_RAW : MID_MODE_AAL5) <<
	    MID_VCI_MODE_SHIFT) | MID_VCI_PTI_MODE |
	    (((eni_vcc->recv-eni_dev->ram) >> (MID_LOC_SKIP+2)) <<
	    MID_VCI_LOCATION_SHIFT) | (order << MID_VCI_SIZE_SHIFT),here);
	return 0;
}


static void close_rx(struct atm_vcc *vcc)
{
	DECLARE_WAITQUEUE(wait,current);
	void __iomem *here;
	struct eni_dev *eni_dev;
	struct eni_vcc *eni_vcc;

	eni_vcc = ENI_VCC(vcc);
	if (!eni_vcc->rx) return;
	eni_dev = ENI_DEV(vcc->dev);
	if (vcc->vpi != ATM_VPI_UNSPEC && vcc->vci != ATM_VCI_UNSPEC) {
		here = eni_dev->vci+vcc->vci*16;
		/* block receiver */
		writel((readl(here) & ~MID_VCI_MODE) | (MID_MODE_TRASH <<
		    MID_VCI_MODE_SHIFT),here);
		/* wait for receiver to become idle */
		udelay(27);
		/* discard pending cell */
		writel(readl(here) & ~MID_VCI_IN_SERVICE,here);
		/* don't accept any new ones */
		eni_dev->rx_map[vcc->vci] = NULL;
		/* wait for RX queue to drain */
		DPRINTK("eni_close: waiting for RX ...\n");
		EVENT("RX closing\n",0,0);
		add_wait_queue(&eni_dev->rx_wait,&wait);
		set_current_state(TASK_UNINTERRUPTIBLE);
		barrier();
		for (;;) {
			/* transition service->rx: rxing++, servicing-- */
			if (!eni_vcc->servicing) {
				barrier();
				if (!eni_vcc->rxing) break;
			}
			EVENT("drain PDUs (rx %ld, serv %ld)\n",eni_vcc->rxing,
			    eni_vcc->servicing);
			printk(KERN_INFO "%d+%d RX left\n",eni_vcc->servicing,
			    eni_vcc->rxing);
			schedule();
			set_current_state(TASK_UNINTERRUPTIBLE);
		}
		for (;;) {
			int at_end;
			u32 tmp;

			tasklet_disable(&eni_dev->task);
			tmp = readl(eni_dev->vci+vcc->vci*16+4) & MID_VCI_READ;
			at_end = eni_vcc->rx_pos == tmp >> MID_VCI_READ_SHIFT;
			tasklet_enable(&eni_dev->task);
			if (at_end) break;
			EVENT("drain discard (host 0x%lx, nic 0x%lx)\n",
			    eni_vcc->rx_pos,tmp);
			printk(KERN_INFO "draining RX: host 0x%lx, nic 0x%x\n",
			    eni_vcc->rx_pos,tmp);
			schedule();
			set_current_state(TASK_UNINTERRUPTIBLE);
		}
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&eni_dev->rx_wait,&wait);
	}
	eni_free_mem(eni_dev,eni_vcc->recv,eni_vcc->words << 2);
	eni_vcc->rx = NULL;
}


static int start_rx(struct atm_dev *dev)
{
	struct eni_dev *eni_dev;

	eni_dev = ENI_DEV(dev);
	eni_dev->rx_map = (struct atm_vcc **) get_zeroed_page(GFP_KERNEL);
	if (!eni_dev->rx_map) {
		printk(KERN_ERR DEV_LABEL "(itf %d): couldn't get free page\n",
		    dev->number);
		free_page((unsigned long) eni_dev->free_list);
		return -ENOMEM;
	}
	eni_dev->rx_mult = DEFAULT_RX_MULT;
	eni_dev->fast = eni_dev->last_fast = NULL;
	eni_dev->slow = eni_dev->last_slow = NULL;
	init_waitqueue_head(&eni_dev->rx_wait);
	skb_queue_head_init(&eni_dev->rx_queue);
	eni_dev->serv_read = eni_in(MID_SERV_WRITE);
	eni_out(0,MID_DMA_WR_RX);
	return 0;
}


/*----------------------------------- TX ------------------------------------*/


enum enq_res { enq_ok,enq_next,enq_jam };


static inline void put_dma(int chan,u32 *dma,int *j,dma_addr_t paddr,
    u32 size)
{
	u32 init,words;

	DPRINTK("put_dma: 0x%lx+0x%x\n",(unsigned long) paddr,size);
	EVENT("put_dma: 0x%lx+0x%lx\n",(unsigned long) paddr,size);
#if 0 /* don't complain anymore */
	if (paddr & 3)
		printk(KERN_ERR "put_dma: unaligned addr (0x%lx)\n",paddr);
	if (size & 3)
		printk(KERN_ERR "put_dma: unaligned size (0x%lx)\n",size);
#endif
	if (paddr & 3) {
		init = 4-(paddr & 3);
		if (init > size || size < 7) init = size;
		DPRINTK("put_dma: %lx DMA: %d/%d bytes\n",
		    (unsigned long) paddr,init,size);
		dma[(*j)++] = MID_DT_BYTE | (init << MID_DMA_COUNT_SHIFT) |
		    (chan << MID_DMA_CHAN_SHIFT);
		dma[(*j)++] = paddr;
		paddr += init;
		size -= init;
	}
	words = size >> 2;
	size &= 3;
	if (words && (paddr & 31)) {
		init = 8-((paddr & 31) >> 2);
		if (init > words) init = words;
		DPRINTK("put_dma: %lx DMA: %d/%d words\n",
		    (unsigned long) paddr,init,words);
		dma[(*j)++] = MID_DT_WORD | (init << MID_DMA_COUNT_SHIFT) |
		    (chan << MID_DMA_CHAN_SHIFT);
		dma[(*j)++] = paddr;
		paddr += init << 2;
		words -= init;
	}
#ifdef CONFIG_ATM_ENI_BURST_TX_16W /* may work with some PCI chipsets ... */
	if (words & ~15) {
		DPRINTK("put_dma: %lx DMA: %d*16/%d words\n",
		    (unsigned long) paddr,words >> 4,words);
		dma[(*j)++] = MID_DT_16W | ((words >> 4) << MID_DMA_COUNT_SHIFT)
		    | (chan << MID_DMA_CHAN_SHIFT);
		dma[(*j)++] = paddr;
		paddr += (words & ~15) << 2;
		words &= 15;
	}
#endif
#ifdef CONFIG_ATM_ENI_BURST_TX_8W /* recommended */
	if (words & ~7) {
		DPRINTK("put_dma: %lx DMA: %d*8/%d words\n",
		    (unsigned long) paddr,words >> 3,words);
		dma[(*j)++] = MID_DT_8W | ((words >> 3) << MID_DMA_COUNT_SHIFT)
		    | (chan << MID_DMA_CHAN_SHIFT);
		dma[(*j)++] = paddr;
		paddr += (words & ~7) << 2;
		words &= 7;
	}
#endif
#ifdef CONFIG_ATM_ENI_BURST_TX_4W /* probably useless if TX_8W or TX_16W */
	if (words & ~3) {
		DPRINTK("put_dma: %lx DMA: %d*4/%d words\n",
		    (unsigned long) paddr,words >> 2,words);
		dma[(*j)++] = MID_DT_4W | ((words >> 2) << MID_DMA_COUNT_SHIFT)
		    | (chan << MID_DMA_CHAN_SHIFT);
		dma[(*j)++] = paddr;
		paddr += (words & ~3) << 2;
		words &= 3;
	}
#endif
#ifdef CONFIG_ATM_ENI_BURST_TX_2W /* probably useless if TX_4W, TX_8W, ... */
	if (words & ~1) {
		DPRINTK("put_dma: %lx DMA: %d*2/%d words\n",
		    (unsigned long) paddr,words >> 1,words);
		dma[(*j)++] = MID_DT_2W | ((words >> 1) << MID_DMA_COUNT_SHIFT)
		    | (chan << MID_DMA_CHAN_SHIFT);
		dma[(*j)++] = paddr;
		paddr += (words & ~1) << 2;
		words &= 1;
	}
#endif
	if (words) {
		DPRINTK("put_dma: %lx DMA: %d words\n",(unsigned long) paddr,
		    words);
		dma[(*j)++] = MID_DT_WORD | (words << MID_DMA_COUNT_SHIFT) |
		    (chan << MID_DMA_CHAN_SHIFT);
		dma[(*j)++] = paddr;
		paddr += words << 2;
	}
	if (size) {
		DPRINTK("put_dma: %lx DMA: %d bytes\n",(unsigned long) paddr,
		    size);
		dma[(*j)++] = MID_DT_BYTE | (size << MID_DMA_COUNT_SHIFT) |
		    (chan << MID_DMA_CHAN_SHIFT);
		dma[(*j)++] = paddr;
	}
}


static enum enq_res do_tx(struct sk_buff *skb)
{
	struct atm_vcc *vcc;
	struct eni_dev *eni_dev;
	struct eni_vcc *eni_vcc;
	struct eni_tx *tx;
	dma_addr_t paddr;
	u32 dma_rd,dma_wr;
	u32 size; /* in words */
	int aal5,dma_size,i,j;

	DPRINTK(">do_tx\n");
	NULLCHECK(skb);
	EVENT("do_tx: skb=0x%lx, %ld bytes\n",(unsigned long) skb,skb->len);
	vcc = ATM_SKB(skb)->vcc;
	NULLCHECK(vcc);
	eni_dev = ENI_DEV(vcc->dev);
	NULLCHECK(eni_dev);
	eni_vcc = ENI_VCC(vcc);
	tx = eni_vcc->tx;
	NULLCHECK(tx);
#if 0 /* Enable this for testing with the "align" program */
	{
		unsigned int hack = *((char *) skb->data)-'0';

		if (hack < 8) {
			skb->data += hack;
			skb->len -= hack;
		}
	}
#endif
#if 0 /* should work now */
	if ((unsigned long) skb->data & 3)
		printk(KERN_ERR DEV_LABEL "(itf %d): VCI %d has mis-aligned "
		    "TX data\n",vcc->dev->number,vcc->vci);
#endif
	/*
	 * Potential future IP speedup: make hard_header big enough to put
	 * segmentation descriptor directly into PDU. Saves: 4 slave writes,
	 * 1 DMA xfer & 2 DMA'ed bytes (protocol layering is for wimps :-)
	 */

	aal5 = vcc->qos.aal == ATM_AAL5;
	/* check space in buffer */
	if (!aal5)
		size = (ATM_CELL_PAYLOAD >> 2)+TX_DESCR_SIZE;
			/* cell without HEC plus segmentation header (includes
			   four-byte cell header) */
	else {
		size = skb->len+4*AAL5_TRAILER+ATM_CELL_PAYLOAD-1;
			/* add AAL5 trailer */
		size = ((size-(size % ATM_CELL_PAYLOAD)) >> 2)+TX_DESCR_SIZE;
						/* add segmentation header */
	}
	/*
	 * Can I move tx_pos by size bytes without getting closer than TX_GAP
	 * to the read pointer ? TX_GAP means to leave some space for what
	 * the manual calls "too close".
	 */
	if (!NEPMOK(tx->tx_pos,size+TX_GAP,
	    eni_in(MID_TX_RDPTR(tx->index)),tx->words)) {
		DPRINTK(DEV_LABEL "(itf %d): TX full (size %d)\n",
		    vcc->dev->number,size);
		return enq_next;
	}
	/* check DMA */
	dma_wr = eni_in(MID_DMA_WR_TX);
	dma_rd = eni_in(MID_DMA_RD_TX);
	dma_size = 3; /* JK for descriptor and final fill, plus final size
			 mis-alignment fix */
DPRINTK("iovcnt = %d\n",skb_shinfo(skb)->nr_frags);
	if (!skb_shinfo(skb)->nr_frags) dma_size += 5;
	else dma_size += 5*(skb_shinfo(skb)->nr_frags+1);
	if (dma_size > TX_DMA_BUF) {
		printk(KERN_CRIT DEV_LABEL "(itf %d): needs %d DMA entries "
		    "(got only %d)\n",vcc->dev->number,dma_size,TX_DMA_BUF);
	}
	DPRINTK("dma_wr is %d, tx_pos is %ld\n",dma_wr,tx->tx_pos);
	if (dma_wr != dma_rd && ((dma_rd+NR_DMA_TX-dma_wr) & (NR_DMA_TX-1)) <
	     dma_size) {
		printk(KERN_WARNING DEV_LABEL "(itf %d): TX DMA full\n",
		    vcc->dev->number);
		return enq_jam;
	}
	paddr = pci_map_single(eni_dev->pci_dev,skb->data,skb->len,
	    PCI_DMA_TODEVICE);
	ENI_PRV_PADDR(skb) = paddr;
	/* prepare DMA queue entries */
	j = 0;
	eni_dev->dma[j++] = (((tx->tx_pos+TX_DESCR_SIZE) & (tx->words-1)) <<
	     MID_DMA_COUNT_SHIFT) | (tx->index << MID_DMA_CHAN_SHIFT) |
	     MID_DT_JK;
	j++;
	if (!skb_shinfo(skb)->nr_frags)
		if (aal5) put_dma(tx->index,eni_dev->dma,&j,paddr,skb->len);
		else put_dma(tx->index,eni_dev->dma,&j,paddr+4,skb->len-4);
	else {
DPRINTK("doing direct send\n"); /* @@@ well, this doesn't work anyway */
		for (i = -1; i < skb_shinfo(skb)->nr_frags; i++)
			if (i == -1)
				put_dma(tx->index,eni_dev->dma,&j,(unsigned long)
				    skb->data,
				    skb_headlen(skb));
			else
				put_dma(tx->index,eni_dev->dma,&j,(unsigned long)
				    skb_frag_page(&skb_shinfo(skb)->frags[i]) +
					skb_shinfo(skb)->frags[i].page_offset,
				    skb_frag_size(&skb_shinfo(skb)->frags[i]));
	}
	if (skb->len & 3) {
		put_dma(tx->index, eni_dev->dma, &j, eni_dev->zero.dma,
			4 - (skb->len & 3));
	}
	/* JK for AAL5 trailer - AAL0 doesn't need it, but who cares ... */
	eni_dev->dma[j++] = (((tx->tx_pos+size) & (tx->words-1)) <<
	     MID_DMA_COUNT_SHIFT) | (tx->index << MID_DMA_CHAN_SHIFT) |
	     MID_DMA_END | MID_DT_JK;
	j++;
	DPRINTK("DMA at end: %d\n",j);
	/* store frame */
	writel((MID_SEG_TX_ID << MID_SEG_ID_SHIFT) |
	    (aal5 ? MID_SEG_AAL5 : 0) | (tx->prescaler << MID_SEG_PR_SHIFT) |
	    (tx->resolution << MID_SEG_RATE_SHIFT) |
	    (size/(ATM_CELL_PAYLOAD/4)),tx->send+tx->tx_pos*4);
/*printk("dsc = 0x%08lx\n",(unsigned long) readl(tx->send+tx->tx_pos*4));*/
	writel((vcc->vci << MID_SEG_VCI_SHIFT) |
            (aal5 ? 0 : (skb->data[3] & 0xf)) |
	    (ATM_SKB(skb)->atm_options & ATM_ATMOPT_CLP ? MID_SEG_CLP : 0),
	    tx->send+((tx->tx_pos+1) & (tx->words-1))*4);
	DPRINTK("size: %d, len:%d\n",size,skb->len);
	if (aal5)
		writel(skb->len,tx->send+
                    ((tx->tx_pos+size-AAL5_TRAILER) & (tx->words-1))*4);
	j = j >> 1;
	for (i = 0; i < j; i++) {
		writel(eni_dev->dma[i*2],eni_dev->tx_dma+dma_wr*8);
		writel(eni_dev->dma[i*2+1],eni_dev->tx_dma+dma_wr*8+4);
		dma_wr = (dma_wr+1) & (NR_DMA_TX-1);
	}
	ENI_PRV_POS(skb) = tx->tx_pos;
	ENI_PRV_SIZE(skb) = size;
	ENI_VCC(vcc)->txing += size;
	tx->tx_pos = (tx->tx_pos+size) & (tx->words-1);
	DPRINTK("dma_wr set to %d, tx_pos is now %ld\n",dma_wr,tx->tx_pos);
	eni_out(dma_wr,MID_DMA_WR_TX);
	skb_queue_tail(&eni_dev->tx_queue,skb);
queued++;
	return enq_ok;
}


static void poll_tx(struct atm_dev *dev)
{
	struct eni_tx *tx;
	struct sk_buff *skb;
	enum enq_res res;
	int i;

	DPRINTK(">poll_tx\n");
	for (i = NR_CHAN-1; i >= 0; i--) {
		tx = &ENI_DEV(dev)->tx[i];
		if (tx->send)
			while ((skb = skb_dequeue(&tx->backlog))) {
				res = do_tx(skb);
				if (res == enq_ok) continue;
				DPRINTK("re-queuing TX PDU\n");
				skb_queue_head(&tx->backlog,skb);
requeued++;
				if (res == enq_jam) return;
				break;
			}
	}
}


static void dequeue_tx(struct atm_dev *dev)
{
	struct eni_dev *eni_dev;
	struct atm_vcc *vcc;
	struct sk_buff *skb;
	struct eni_tx *tx;

	NULLCHECK(dev);
	eni_dev = ENI_DEV(dev);
	NULLCHECK(eni_dev);
	while ((skb = skb_dequeue(&eni_dev->tx_queue))) {
		vcc = ATM_SKB(skb)->vcc;
		NULLCHECK(vcc);
		tx = ENI_VCC(vcc)->tx;
		NULLCHECK(ENI_VCC(vcc)->tx);
		DPRINTK("dequeue_tx: next 0x%lx curr 0x%x\n",ENI_PRV_POS(skb),
		    (unsigned) eni_in(MID_TX_DESCRSTART(tx->index)));
		if (ENI_VCC(vcc)->txing < tx->words && ENI_PRV_POS(skb) ==
		    eni_in(MID_TX_DESCRSTART(tx->index))) {
			skb_queue_head(&eni_dev->tx_queue,skb);
			break;
		}
		ENI_VCC(vcc)->txing -= ENI_PRV_SIZE(skb);
		pci_unmap_single(eni_dev->pci_dev,ENI_PRV_PADDR(skb),skb->len,
		    PCI_DMA_TODEVICE);
		if (vcc->pop) vcc->pop(vcc,skb);
		else dev_kfree_skb_irq(skb);
		atomic_inc(&vcc->stats->tx);
		wake_up(&eni_dev->tx_wait);
dma_complete++;
	}
}


static struct eni_tx *alloc_tx(struct eni_dev *eni_dev,int ubr)
{
	int i;

	for (i = !ubr; i < NR_CHAN; i++)
		if (!eni_dev->tx[i].send) return eni_dev->tx+i;
	return NULL;
}


static int comp_tx(struct eni_dev *eni_dev,int *pcr,int reserved,int *pre,
    int *res,int unlimited)
{
	static const int pre_div[] = { 4,16,128,2048 };
	    /* 2^(((x+2)^2-(x+2))/2+1) */

	if (unlimited) *pre = *res = 0;
	else {
		if (*pcr > 0) {
			int div;

			for (*pre = 0; *pre < 3; (*pre)++)
				if (TS_CLOCK/pre_div[*pre]/64 <= *pcr) break;
			div = pre_div[*pre]**pcr;
			DPRINTK("min div %d\n",div);
			*res = TS_CLOCK/div-1;
		}
		else {
			int div;

			if (!*pcr) *pcr = eni_dev->tx_bw+reserved;
			for (*pre = 3; *pre >= 0; (*pre)--)
				if (TS_CLOCK/pre_div[*pre]/64 > -*pcr) break;
			if (*pre < 3) (*pre)++; /* else fail later */
			div = pre_div[*pre]*-*pcr;
			DPRINTK("max div %d\n",div);
			*res = DIV_ROUND_UP(TS_CLOCK, div)-1;
		}
		if (*res < 0) *res = 0;
		if (*res > MID_SEG_MAX_RATE) *res = MID_SEG_MAX_RATE;
	}
	*pcr = TS_CLOCK/pre_div[*pre]/(*res+1);
	DPRINTK("out pcr: %d (%d:%d)\n",*pcr,*pre,*res);
	return 0;
}


static int reserve_or_set_tx(struct atm_vcc *vcc,struct atm_trafprm *txtp,
    int set_rsv,int set_shp)
{
	struct eni_dev *eni_dev = ENI_DEV(vcc->dev);
	struct eni_vcc *eni_vcc = ENI_VCC(vcc);
	struct eni_tx *tx;
	unsigned long size;
	void __iomem *mem;
	int rate,ubr,unlimited,new_tx;
	int pre,res,order;
	int error;

	rate = atm_pcr_goal(txtp);
	ubr = txtp->traffic_class == ATM_UBR;
	unlimited = ubr && (!rate || rate <= -ATM_OC3_PCR ||
	    rate >= ATM_OC3_PCR);
	if (!unlimited) {
		size = txtp->max_sdu*eni_dev->tx_mult/100;
		if (size > MID_MAX_BUF_SIZE && txtp->max_sdu <=
		    MID_MAX_BUF_SIZE)
			size = MID_MAX_BUF_SIZE;
	}
	else {
		if (eni_dev->ubr) {
			eni_vcc->tx = eni_dev->ubr;
			txtp->pcr = ATM_OC3_PCR;
			return 0;
		}
		size = UBR_BUFFER;
	}
	new_tx = !eni_vcc->tx;
	mem = NULL; /* for gcc */
	if (!new_tx) tx = eni_vcc->tx;
	else {
		mem = eni_alloc_mem(eni_dev,&size);
		if (!mem) return -ENOBUFS;
		tx = alloc_tx(eni_dev,unlimited);
		if (!tx) {
			eni_free_mem(eni_dev,mem,size);
			return -EBUSY;
		}
		DPRINTK("got chan %d\n",tx->index);
		tx->reserved = tx->shaping = 0;
		tx->send = mem;
		tx->words = size >> 2;
		skb_queue_head_init(&tx->backlog);
		for (order = 0; size > (1 << (order+10)); order++);
		eni_out((order << MID_SIZE_SHIFT) |
		    ((tx->send-eni_dev->ram) >> (MID_LOC_SKIP+2)),
		    MID_TX_PLACE(tx->index));
		tx->tx_pos = eni_in(MID_TX_DESCRSTART(tx->index)) &
		    MID_DESCR_START;
	}
	error = comp_tx(eni_dev,&rate,tx->reserved,&pre,&res,unlimited);
	if (!error  && txtp->min_pcr > rate) error = -EINVAL;
	if (!error && txtp->max_pcr && txtp->max_pcr != ATM_MAX_PCR &&
	    txtp->max_pcr < rate) error = -EINVAL;
	if (!error && !ubr && rate > eni_dev->tx_bw+tx->reserved)
		error = -EINVAL;
	if (!error && set_rsv && !set_shp && rate < tx->shaping)
		error = -EINVAL;
	if (!error && !set_rsv && rate > tx->reserved && !ubr)
		error = -EINVAL;
	if (error) {
		if (new_tx) {
			tx->send = NULL;
			eni_free_mem(eni_dev,mem,size);
		}
		return error;
	}
	txtp->pcr = rate;
	if (set_rsv && !ubr) {
		eni_dev->tx_bw += tx->reserved;
		tx->reserved = rate;
		eni_dev->tx_bw -= rate;
	}
	if (set_shp || (unlimited && new_tx)) {
		if (unlimited && new_tx) eni_dev->ubr = tx;
		tx->prescaler = pre;
		tx->resolution = res;
		tx->shaping = rate;
	}
	if (set_shp) eni_vcc->tx = tx;
	DPRINTK("rsv %d shp %d\n",tx->reserved,tx->shaping);
	return 0;
}


static int open_tx_first(struct atm_vcc *vcc)
{
	ENI_VCC(vcc)->tx = NULL;
	if (vcc->qos.txtp.traffic_class == ATM_NONE) return 0;
	ENI_VCC(vcc)->txing = 0;
	return reserve_or_set_tx(vcc,&vcc->qos.txtp,1,1);
}


static int open_tx_second(struct atm_vcc *vcc)
{
	return 0; /* nothing to do */
}


static void close_tx(struct atm_vcc *vcc)
{
	DECLARE_WAITQUEUE(wait,current);
	struct eni_dev *eni_dev;
	struct eni_vcc *eni_vcc;

	eni_vcc = ENI_VCC(vcc);
	if (!eni_vcc->tx) return;
	eni_dev = ENI_DEV(vcc->dev);
	/* wait for TX queue to drain */
	DPRINTK("eni_close: waiting for TX ...\n");
	add_wait_queue(&eni_dev->tx_wait,&wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	for (;;) {
		int txing;

		tasklet_disable(&eni_dev->task);
		txing = skb_peek(&eni_vcc->tx->backlog) || eni_vcc->txing;
		tasklet_enable(&eni_dev->task);
		if (!txing) break;
		DPRINTK("%d TX left\n",eni_vcc->txing);
		schedule();
		set_current_state(TASK_UNINTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&eni_dev->tx_wait,&wait);
	if (eni_vcc->tx != eni_dev->ubr) {
		/*
		 * Looping a few times in here is probably far cheaper than
		 * keeping track of TX completions all the time, so let's poll
		 * a bit ...
		 */
		while (eni_in(MID_TX_RDPTR(eni_vcc->tx->index)) !=
		    eni_in(MID_TX_DESCRSTART(eni_vcc->tx->index)))
			schedule();
		eni_free_mem(eni_dev,eni_vcc->tx->send,eni_vcc->tx->words << 2);
		eni_vcc->tx->send = NULL;
		eni_dev->tx_bw += eni_vcc->tx->reserved;
	}
	eni_vcc->tx = NULL;
}


static int start_tx(struct atm_dev *dev)
{
	struct eni_dev *eni_dev;
	int i;

	eni_dev = ENI_DEV(dev);
	eni_dev->lost = 0;
	eni_dev->tx_bw = ATM_OC3_PCR;
	eni_dev->tx_mult = DEFAULT_TX_MULT;
	init_waitqueue_head(&eni_dev->tx_wait);
	eni_dev->ubr = NULL;
	skb_queue_head_init(&eni_dev->tx_queue);
	eni_out(0,MID_DMA_WR_TX);
	for (i = 0; i < NR_CHAN; i++) {
		eni_dev->tx[i].send = NULL;
		eni_dev->tx[i].index = i;
	}
	return 0;
}


/*--------------------------------- common ----------------------------------*/


#if 0 /* may become useful again when tuning things */

static void foo(void)
{
printk(KERN_INFO
  "tx_complete=%d,dma_complete=%d,queued=%d,requeued=%d,sub=%d,\n"
  "backlogged=%d,rx_enqueued=%d,rx_dequeued=%d,putting=%d,pushed=%d\n",
  tx_complete,dma_complete,queued,requeued,submitted,backlogged,
  rx_enqueued,rx_dequeued,putting,pushed);
if (eni_boards) printk(KERN_INFO "loss: %ld\n",ENI_DEV(eni_boards)->lost);
}

#endif


static void bug_int(struct atm_dev *dev,unsigned long reason)
{
	DPRINTK(">bug_int\n");
	if (reason & MID_DMA_ERR_ACK)
		printk(KERN_CRIT DEV_LABEL "(itf %d): driver error - DMA "
		    "error\n",dev->number);
	if (reason & MID_TX_IDENT_MISM)
		printk(KERN_CRIT DEV_LABEL "(itf %d): driver error - ident "
		    "mismatch\n",dev->number);
	if (reason & MID_TX_DMA_OVFL)
		printk(KERN_CRIT DEV_LABEL "(itf %d): driver error - DMA "
		    "overflow\n",dev->number);
	EVENT("---dump ends here---\n",0,0);
	printk(KERN_NOTICE "---recent events---\n");
	event_dump();
}


static irqreturn_t eni_int(int irq,void *dev_id)
{
	struct atm_dev *dev;
	struct eni_dev *eni_dev;
	u32 reason;

	DPRINTK(">eni_int\n");
	dev = dev_id;
	eni_dev = ENI_DEV(dev);
	reason = eni_in(MID_ISA);
	DPRINTK(DEV_LABEL ": int 0x%lx\n",(unsigned long) reason);
	/*
	 * Must handle these two right now, because reading ISA doesn't clear
	 * them, so they re-occur and we never make it to the tasklet. Since
	 * they're rare, we don't mind the occasional invocation of eni_tasklet
	 * with eni_dev->events == 0.
	 */
	if (reason & MID_STAT_OVFL) {
		EVENT("stat overflow\n",0,0);
		eni_dev->lost += eni_in(MID_STAT) & MID_OVFL_TRASH;
	}
	if (reason & MID_SUNI_INT) {
		EVENT("SUNI int\n",0,0);
		dev->phy->interrupt(dev);
#if 0
		foo();
#endif
	}
	spin_lock(&eni_dev->lock);
	eni_dev->events |= reason;
	spin_unlock(&eni_dev->lock);
	tasklet_schedule(&eni_dev->task);
	return IRQ_HANDLED;
}


static void eni_tasklet(unsigned long data)
{
	struct atm_dev *dev = (struct atm_dev *) data;
	struct eni_dev *eni_dev = ENI_DEV(dev);
	unsigned long flags;
	u32 events;

	DPRINTK("eni_tasklet (dev %p)\n",dev);
	spin_lock_irqsave(&eni_dev->lock,flags);
	events = xchg(&eni_dev->events,0);
	spin_unlock_irqrestore(&eni_dev->lock,flags);
	if (events & MID_RX_DMA_COMPLETE) {
		EVENT("INT: RX DMA complete, starting dequeue_rx\n",0,0);
		dequeue_rx(dev);
		EVENT("dequeue_rx done, starting poll_rx\n",0,0);
		poll_rx(dev);
		EVENT("poll_rx done\n",0,0);
		/* poll_tx ? */
	}
	if (events & MID_SERVICE) {
		EVENT("INT: service, starting get_service\n",0,0);
		get_service(dev);
		EVENT("get_service done, starting poll_rx\n",0,0);
		poll_rx(dev);
		EVENT("poll_rx done\n",0,0);
	}
 	if (events & MID_TX_DMA_COMPLETE) {
		EVENT("INT: TX DMA COMPLETE\n",0,0);
		dequeue_tx(dev);
	}
	if (events & MID_TX_COMPLETE) {
		EVENT("INT: TX COMPLETE\n",0,0);
tx_complete++;
		wake_up(&eni_dev->tx_wait);
		/* poll_rx ? */
	}
	if (events & (MID_DMA_ERR_ACK | MID_TX_IDENT_MISM | MID_TX_DMA_OVFL)) {
		EVENT("bug interrupt\n",0,0);
		bug_int(dev,events);
	}
	poll_tx(dev);
}


/*--------------------------------- entries ---------------------------------*/


static char * const media_name[] = {
    "MMF", "SMF", "MMF", "03?", /*  0- 3 */
    "UTP", "05?", "06?", "07?", /*  4- 7 */
    "TAXI","09?", "10?", "11?", /*  8-11 */
    "12?", "13?", "14?", "15?", /* 12-15 */
    "MMF", "SMF", "18?", "19?", /* 16-19 */
    "UTP", "21?", "22?", "23?", /* 20-23 */
    "24?", "25?", "26?", "27?", /* 24-27 */
    "28?", "29?", "30?", "31?"  /* 28-31 */
};


#define SET_SEPROM \
  ({ if (!error && !pci_error) { \
    pci_error = pci_write_config_byte(eni_dev->pci_dev,PCI_TONGA_CTRL,tonga); \
    udelay(10); /* 10 usecs */ \
  } })
#define GET_SEPROM \
  ({ if (!error && !pci_error) { \
    pci_error = pci_read_config_byte(eni_dev->pci_dev,PCI_TONGA_CTRL,&tonga); \
    udelay(10); /* 10 usecs */ \
  } })


static int get_esi_asic(struct atm_dev *dev)
{
	struct eni_dev *eni_dev;
	unsigned char tonga;
	int error,failed,pci_error;
	int address,i,j;

	eni_dev = ENI_DEV(dev);
	error = pci_error = 0;
	tonga = SEPROM_MAGIC | SEPROM_DATA | SEPROM_CLK;
	SET_SEPROM;
	for (i = 0; i < ESI_LEN && !error && !pci_error; i++) {
		/* start operation */
		tonga |= SEPROM_DATA;
		SET_SEPROM;
		tonga |= SEPROM_CLK;
		SET_SEPROM;
		tonga &= ~SEPROM_DATA;
		SET_SEPROM;
		tonga &= ~SEPROM_CLK;
		SET_SEPROM;
		/* send address */
		address = ((i+SEPROM_ESI_BASE) << 1)+1;
		for (j = 7; j >= 0; j--) {
			tonga = (address >> j) & 1 ? tonga | SEPROM_DATA :
			    tonga & ~SEPROM_DATA;
			SET_SEPROM;
			tonga |= SEPROM_CLK;
			SET_SEPROM;
			tonga &= ~SEPROM_CLK;
			SET_SEPROM;
		}
		/* get ack */
		tonga |= SEPROM_DATA;
		SET_SEPROM;
		tonga |= SEPROM_CLK;
		SET_SEPROM;
		GET_SEPROM;
		failed = tonga & SEPROM_DATA;
		tonga &= ~SEPROM_CLK;
		SET_SEPROM;
		tonga |= SEPROM_DATA;
		SET_SEPROM;
		if (failed) error = -EIO;
		else {
			dev->esi[i] = 0;
			for (j = 7; j >= 0; j--) {
				dev->esi[i] <<= 1;
				tonga |= SEPROM_DATA;
				SET_SEPROM;
				tonga |= SEPROM_CLK;
				SET_SEPROM;
				GET_SEPROM;
				if (tonga & SEPROM_DATA) dev->esi[i] |= 1;
				tonga &= ~SEPROM_CLK;
				SET_SEPROM;
				tonga |= SEPROM_DATA;
				SET_SEPROM;
			}
			/* get ack */
			tonga |= SEPROM_DATA;
			SET_SEPROM;
			tonga |= SEPROM_CLK;
			SET_SEPROM;
			GET_SEPROM;
			if (!(tonga & SEPROM_DATA)) error = -EIO;
			tonga &= ~SEPROM_CLK;
			SET_SEPROM;
			tonga |= SEPROM_DATA;
			SET_SEPROM;
		}
		/* stop operation */
		tonga &= ~SEPROM_DATA;
		SET_SEPROM;
		tonga |= SEPROM_CLK;
		SET_SEPROM;
		tonga |= SEPROM_DATA;
		SET_SEPROM;
	}
	if (pci_error) {
		printk(KERN_ERR DEV_LABEL "(itf %d): error reading ESI "
		    "(0x%02x)\n",dev->number,pci_error);
		error = -EIO;
	}
	return error;
}


#undef SET_SEPROM
#undef GET_SEPROM


static int get_esi_fpga(struct atm_dev *dev, void __iomem *base)
{
	void __iomem *mac_base;
	int i;

	mac_base = base+EPROM_SIZE-sizeof(struct midway_eprom);
	for (i = 0; i < ESI_LEN; i++) dev->esi[i] = readb(mac_base+(i^3));
	return 0;
}


static int eni_do_init(struct atm_dev *dev)
{
	struct midway_eprom __iomem *eprom;
	struct eni_dev *eni_dev;
	struct pci_dev *pci_dev;
	unsigned long real_base;
	void __iomem *base;
	int error,i,last;

	DPRINTK(">eni_init\n");
	dev->ci_range.vpi_bits = 0;
	dev->ci_range.vci_bits = NR_VCI_LD;
	dev->link_rate = ATM_OC3_PCR;
	eni_dev = ENI_DEV(dev);
	pci_dev = eni_dev->pci_dev;
	real_base = pci_resource_start(pci_dev, 0);
	eni_dev->irq = pci_dev->irq;
	if ((error = pci_write_config_word(pci_dev,PCI_COMMAND,
	    PCI_COMMAND_MEMORY |
	    (eni_dev->asic ? PCI_COMMAND_PARITY | PCI_COMMAND_SERR : 0)))) {
		printk(KERN_ERR DEV_LABEL "(itf %d): can't enable memory "
		    "(0x%02x)\n",dev->number,error);
		return -EIO;
	}
	printk(KERN_NOTICE DEV_LABEL "(itf %d): rev.%d,base=0x%lx,irq=%d,",
	    dev->number,pci_dev->revision,real_base,eni_dev->irq);
	if (!(base = ioremap_nocache(real_base,MAP_MAX_SIZE))) {
		printk("\n");
		printk(KERN_ERR DEV_LABEL "(itf %d): can't set up page "
		    "mapping\n",dev->number);
		return error;
	}
	eni_dev->ioaddr = base;
	eni_dev->base_diff = real_base - (unsigned long) base;
	/* id may not be present in ASIC Tonga boards - check this @@@ */
	if (!eni_dev->asic) {
		eprom = (base+EPROM_SIZE-sizeof(struct midway_eprom));
		if (readl(&eprom->magic) != ENI155_MAGIC) {
			printk("\n");
			printk(KERN_ERR DEV_LABEL
			       "(itf %d): bad magic - expected 0x%x, got 0x%x\n",
			       dev->number, ENI155_MAGIC,
			       (unsigned)readl(&eprom->magic));
			error = -EINVAL;
			goto unmap;
		}
	}
	eni_dev->phy = base+PHY_BASE;
	eni_dev->reg = base+REG_BASE;
	eni_dev->ram = base+RAM_BASE;
	last = MAP_MAX_SIZE-RAM_BASE;
	for (i = last-RAM_INCREMENT; i >= 0; i -= RAM_INCREMENT) {
		writel(0x55555555,eni_dev->ram+i);
		if (readl(eni_dev->ram+i) != 0x55555555) last = i;
		else {
			writel(0xAAAAAAAA,eni_dev->ram+i);
			if (readl(eni_dev->ram+i) != 0xAAAAAAAA) last = i;
			else writel(i,eni_dev->ram+i);
		}
	}
	for (i = 0; i < last; i += RAM_INCREMENT)
		if (readl(eni_dev->ram+i) != i) break;
	eni_dev->mem = i;
	memset_io(eni_dev->ram,0,eni_dev->mem);
	/* TODO: should shrink allocation now */
	printk("mem=%dkB (",eni_dev->mem >> 10);
	/* TODO: check for non-SUNI, check for TAXI ? */
	if (!(eni_in(MID_RES_ID_MCON) & 0x200) != !eni_dev->asic) {
		printk(")\n");
		printk(KERN_ERR DEV_LABEL "(itf %d): ERROR - wrong id 0x%x\n",
		    dev->number,(unsigned) eni_in(MID_RES_ID_MCON));
		error = -EINVAL;
		goto unmap;
	}
	error = eni_dev->asic ? get_esi_asic(dev) : get_esi_fpga(dev,base);
	if (error)
		goto unmap;
	for (i = 0; i < ESI_LEN; i++)
		printk("%s%02X",i ? "-" : "",dev->esi[i]);
	printk(")\n");
	printk(KERN_NOTICE DEV_LABEL "(itf %d): %s,%s\n",dev->number,
	    eni_in(MID_RES_ID_MCON) & 0x200 ? "ASIC" : "FPGA",
	    media_name[eni_in(MID_RES_ID_MCON) & DAUGTHER_ID]);

	error = suni_init(dev);
	if (error)
		goto unmap;
out:
	return error;
unmap:
	iounmap(base);
	goto out;
}

static void eni_do_release(struct atm_dev *dev)
{
	struct eni_dev *ed = ENI_DEV(dev);

	dev->phy->stop(dev);
	dev->phy = NULL;
	iounmap(ed->ioaddr);
}

static int eni_start(struct atm_dev *dev)
{
	struct eni_dev *eni_dev;
	
	void __iomem *buf;
	unsigned long buffer_mem;
	int error;

	DPRINTK(">eni_start\n");
	eni_dev = ENI_DEV(dev);
	if (request_irq(eni_dev->irq,&eni_int,IRQF_SHARED,DEV_LABEL,dev)) {
		printk(KERN_ERR DEV_LABEL "(itf %d): IRQ%d is already in use\n",
		    dev->number,eni_dev->irq);
		error = -EAGAIN;
		goto out;
	}
	pci_set_master(eni_dev->pci_dev);
	if ((error = pci_write_config_word(eni_dev->pci_dev,PCI_COMMAND,
	    PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
	    (eni_dev->asic ? PCI_COMMAND_PARITY | PCI_COMMAND_SERR : 0)))) {
		printk(KERN_ERR DEV_LABEL "(itf %d): can't enable memory+"
		    "master (0x%02x)\n",dev->number,error);
		goto free_irq;
	}
	if ((error = pci_write_config_byte(eni_dev->pci_dev,PCI_TONGA_CTRL,
	    END_SWAP_DMA))) {
		printk(KERN_ERR DEV_LABEL "(itf %d): can't set endian swap "
		    "(0x%02x)\n",dev->number,error);
		goto free_irq;
	}
	/* determine addresses of internal tables */
	eni_dev->vci = eni_dev->ram;
	eni_dev->rx_dma = eni_dev->ram+NR_VCI*16;
	eni_dev->tx_dma = eni_dev->rx_dma+NR_DMA_RX*8;
	eni_dev->service = eni_dev->tx_dma+NR_DMA_TX*8;
	buf = eni_dev->service+NR_SERVICE*4;
	DPRINTK("vci 0x%lx,rx 0x%lx, tx 0x%lx,srv 0x%lx,buf 0x%lx\n",
	     eni_dev->vci,eni_dev->rx_dma,eni_dev->tx_dma,
	     eni_dev->service,buf);
	spin_lock_init(&eni_dev->lock);
	tasklet_init(&eni_dev->task,eni_tasklet,(unsigned long) dev);
	eni_dev->events = 0;
	/* initialize memory management */
	buffer_mem = eni_dev->mem - (buf - eni_dev->ram);
	eni_dev->free_list_size = buffer_mem/MID_MIN_BUF_SIZE/2;
	eni_dev->free_list = kmalloc(
	    sizeof(struct eni_free)*(eni_dev->free_list_size+1),GFP_KERNEL);
	if (!eni_dev->free_list) {
		printk(KERN_ERR DEV_LABEL "(itf %d): couldn't get free page\n",
		    dev->number);
		error = -ENOMEM;
		goto free_irq;
	}
	eni_dev->free_len = 0;
	eni_put_free(eni_dev,buf,buffer_mem);
	memset_io(eni_dev->vci,0,16*NR_VCI); /* clear VCI table */
	/*
	 * byte_addr  free (k)
	 * 0x00000000     512  VCI table
	 * 0x00004000	  496  RX DMA
	 * 0x00005000	  492  TX DMA
	 * 0x00006000	  488  service list
	 * 0x00007000	  484  buffers
	 * 0x00080000	    0  end (512kB)
	 */
	eni_out(0xffffffff,MID_IE);
	error = start_tx(dev);
	if (error) goto free_list;
	error = start_rx(dev);
	if (error) goto free_list;
	error = dev->phy->start(dev);
	if (error) goto free_list;
	eni_out(eni_in(MID_MC_S) | (1 << MID_INT_SEL_SHIFT) |
	    MID_TX_LOCK_MODE | MID_DMA_ENABLE | MID_TX_ENABLE | MID_RX_ENABLE,
	    MID_MC_S);
	    /* Tonga uses SBus INTReq1 */
	(void) eni_in(MID_ISA); /* clear Midway interrupts */
	return 0;

free_list:
	kfree(eni_dev->free_list);

free_irq:
	free_irq(eni_dev->irq, dev);

out:
	return error;
}


static void eni_close(struct atm_vcc *vcc)
{
	DPRINTK(">eni_close\n");
	if (!ENI_VCC(vcc)) return;
	clear_bit(ATM_VF_READY,&vcc->flags);
	close_rx(vcc);
	close_tx(vcc);
	DPRINTK("eni_close: done waiting\n");
	/* deallocate memory */
	kfree(ENI_VCC(vcc));
	vcc->dev_data = NULL;
	clear_bit(ATM_VF_ADDR,&vcc->flags);
	/*foo();*/
}


static int eni_open(struct atm_vcc *vcc)
{
	struct eni_vcc *eni_vcc;
	int error;
	short vpi = vcc->vpi;
	int vci = vcc->vci;

	DPRINTK(">eni_open\n");
	EVENT("eni_open\n",0,0);
	if (!test_bit(ATM_VF_PARTIAL,&vcc->flags))
		vcc->dev_data = NULL;
	if (vci != ATM_VPI_UNSPEC && vpi != ATM_VCI_UNSPEC)
		set_bit(ATM_VF_ADDR,&vcc->flags);
	if (vcc->qos.aal != ATM_AAL0 && vcc->qos.aal != ATM_AAL5)
		return -EINVAL;
	DPRINTK(DEV_LABEL "(itf %d): open %d.%d\n",vcc->dev->number,vcc->vpi,
	    vcc->vci);
	if (!test_bit(ATM_VF_PARTIAL,&vcc->flags)) {
		eni_vcc = kmalloc(sizeof(struct eni_vcc),GFP_KERNEL);
		if (!eni_vcc) return -ENOMEM;
		vcc->dev_data = eni_vcc;
		eni_vcc->tx = NULL; /* for eni_close after open_rx */
		if ((error = open_rx_first(vcc))) {
			eni_close(vcc);
			return error;
		}
		if ((error = open_tx_first(vcc))) {
			eni_close(vcc);
			return error;
		}
	}
	if (vci == ATM_VPI_UNSPEC || vpi == ATM_VCI_UNSPEC) return 0;
	if ((error = open_rx_second(vcc))) {
		eni_close(vcc);
		return error;
	}
	if ((error = open_tx_second(vcc))) {
		eni_close(vcc);
		return error;
	}
	set_bit(ATM_VF_READY,&vcc->flags);
	/* should power down SUNI while !ref_count @@@ */
	return 0;
}


static int eni_change_qos(struct atm_vcc *vcc,struct atm_qos *qos,int flgs)
{
	struct eni_dev *eni_dev = ENI_DEV(vcc->dev);
	struct eni_tx *tx = ENI_VCC(vcc)->tx;
	struct sk_buff *skb;
	int error,rate,rsv,shp;

	if (qos->txtp.traffic_class == ATM_NONE) return 0;
	if (tx == eni_dev->ubr) return -EBADFD;
	rate = atm_pcr_goal(&qos->txtp);
	if (rate < 0) rate = -rate;
	rsv = shp = 0;
	if ((flgs & ATM_MF_DEC_RSV) && rate && rate < tx->reserved) rsv = 1;
	if ((flgs & ATM_MF_INC_RSV) && (!rate || rate > tx->reserved)) rsv = 1;
	if ((flgs & ATM_MF_DEC_SHP) && rate && rate < tx->shaping) shp = 1;
	if ((flgs & ATM_MF_INC_SHP) && (!rate || rate > tx->shaping)) shp = 1;
	if (!rsv && !shp) return 0;
	error = reserve_or_set_tx(vcc,&qos->txtp,rsv,shp);
	if (error) return error;
	if (shp && !(flgs & ATM_MF_IMMED)) return 0;
	/*
	 * Walk through the send buffer and patch the rate information in all
	 * segmentation buffer descriptors of this VCC.
	 */
	tasklet_disable(&eni_dev->task);
	skb_queue_walk(&eni_dev->tx_queue, skb) {
		void __iomem *dsc;

		if (ATM_SKB(skb)->vcc != vcc) continue;
		dsc = tx->send+ENI_PRV_POS(skb)*4;
		writel((readl(dsc) & ~(MID_SEG_RATE | MID_SEG_PR)) |
		    (tx->prescaler << MID_SEG_PR_SHIFT) |
		    (tx->resolution << MID_SEG_RATE_SHIFT), dsc);
	}
	tasklet_enable(&eni_dev->task);
	return 0;
}


static int eni_ioctl(struct atm_dev *dev,unsigned int cmd,void __user *arg)
{
	struct eni_dev *eni_dev = ENI_DEV(dev);

	if (cmd == ENI_MEMDUMP) {
		if (!capable(CAP_NET_ADMIN)) return -EPERM;
		printk(KERN_WARNING "Please use /proc/atm/" DEV_LABEL ":%d "
		    "instead of obsolete ioctl ENI_MEMDUMP\n",dev->number);
		dump(dev);
		return 0;
	}
	if (cmd == ENI_SETMULT) {
		struct eni_multipliers mult;

		if (!capable(CAP_NET_ADMIN)) return -EPERM;
		if (copy_from_user(&mult, arg,
		    sizeof(struct eni_multipliers)))
			return -EFAULT;
		if ((mult.tx && mult.tx <= 100) || (mult.rx &&mult.rx <= 100) ||
		    mult.tx > 65536 || mult.rx > 65536)
			return -EINVAL;
		if (mult.tx) eni_dev->tx_mult = mult.tx;
		if (mult.rx) eni_dev->rx_mult = mult.rx;
		return 0;
	}
	if (cmd == ATM_SETCIRANGE) {
		struct atm_cirange ci;

		if (copy_from_user(&ci, arg,sizeof(struct atm_cirange)))
			return -EFAULT;
		if ((ci.vpi_bits == 0 || ci.vpi_bits == ATM_CI_MAX) &&
		    (ci.vci_bits == NR_VCI_LD || ci.vpi_bits == ATM_CI_MAX))
		    return 0;
		return -EINVAL;
	}
	if (!dev->phy->ioctl) return -ENOIOCTLCMD;
	return dev->phy->ioctl(dev,cmd,arg);
}


static int eni_getsockopt(struct atm_vcc *vcc,int level,int optname,
    void __user *optval,int optlen)
{
	return -EINVAL;
}


static int eni_setsockopt(struct atm_vcc *vcc,int level,int optname,
    void __user *optval,unsigned int optlen)
{
	return -EINVAL;
}


static int eni_send(struct atm_vcc *vcc,struct sk_buff *skb)
{
	enum enq_res res;

	DPRINTK(">eni_send\n");
	if (!ENI_VCC(vcc)->tx) {
		if (vcc->pop) vcc->pop(vcc,skb);
		else dev_kfree_skb(skb);
		return -EINVAL;
	}
	if (!skb) {
		printk(KERN_CRIT "!skb in eni_send ?\n");
		if (vcc->pop) vcc->pop(vcc,skb);
		return -EINVAL;
	}
	if (vcc->qos.aal == ATM_AAL0) {
		if (skb->len != ATM_CELL_SIZE-1) {
			if (vcc->pop) vcc->pop(vcc,skb);
			else dev_kfree_skb(skb);
			return -EINVAL;
		}
		*(u32 *) skb->data = htonl(*(u32 *) skb->data);
	}
submitted++;
	ATM_SKB(skb)->vcc = vcc;
	tasklet_disable(&ENI_DEV(vcc->dev)->task);
	res = do_tx(skb);
	tasklet_enable(&ENI_DEV(vcc->dev)->task);
	if (res == enq_ok) return 0;
	skb_queue_tail(&ENI_VCC(vcc)->tx->backlog,skb);
backlogged++;
	tasklet_schedule(&ENI_DEV(vcc->dev)->task);
	return 0;
}

static void eni_phy_put(struct atm_dev *dev,unsigned char value,
    unsigned long addr)
{
	writel(value,ENI_DEV(dev)->phy+addr*4);
}



static unsigned char eni_phy_get(struct atm_dev *dev,unsigned long addr)
{
	return readl(ENI_DEV(dev)->phy+addr*4);
}


static int eni_proc_read(struct atm_dev *dev,loff_t *pos,char *page)
{
	struct hlist_node *node;
	struct sock *s;
	static const char *signal[] = { "LOST","unknown","okay" };
	struct eni_dev *eni_dev = ENI_DEV(dev);
	struct atm_vcc *vcc;
	int left,i;

	left = *pos;
	if (!left)
		return sprintf(page,DEV_LABEL "(itf %d) signal %s, %dkB, "
		    "%d cps remaining\n",dev->number,signal[(int) dev->signal],
		    eni_dev->mem >> 10,eni_dev->tx_bw);
	if (!--left)
		return sprintf(page,"%4sBursts: TX"
#if !defined(CONFIG_ATM_ENI_BURST_TX_16W) && \
    !defined(CONFIG_ATM_ENI_BURST_TX_8W) && \
    !defined(CONFIG_ATM_ENI_BURST_TX_4W) && \
    !defined(CONFIG_ATM_ENI_BURST_TX_2W)
		    " none"
#endif
#ifdef CONFIG_ATM_ENI_BURST_TX_16W
		    " 16W"
#endif
#ifdef CONFIG_ATM_ENI_BURST_TX_8W
		    " 8W"
#endif
#ifdef CONFIG_ATM_ENI_BURST_TX_4W
		    " 4W"
#endif
#ifdef CONFIG_ATM_ENI_BURST_TX_2W
		    " 2W"
#endif
		    ", RX"
#if !defined(CONFIG_ATM_ENI_BURST_RX_16W) && \
    !defined(CONFIG_ATM_ENI_BURST_RX_8W) && \
    !defined(CONFIG_ATM_ENI_BURST_RX_4W) && \
    !defined(CONFIG_ATM_ENI_BURST_RX_2W)
		    " none"
#endif
#ifdef CONFIG_ATM_ENI_BURST_RX_16W
		    " 16W"
#endif
#ifdef CONFIG_ATM_ENI_BURST_RX_8W
		    " 8W"
#endif
#ifdef CONFIG_ATM_ENI_BURST_RX_4W
		    " 4W"
#endif
#ifdef CONFIG_ATM_ENI_BURST_RX_2W
		    " 2W"
#endif
#ifndef CONFIG_ATM_ENI_TUNE_BURST
		    " (default)"
#endif
		    "\n","");
	if (!--left) 
		return sprintf(page,"%4sBuffer multipliers: tx %d%%, rx %d%%\n",
		    "",eni_dev->tx_mult,eni_dev->rx_mult);
	for (i = 0; i < NR_CHAN; i++) {
		struct eni_tx *tx = eni_dev->tx+i;

		if (!tx->send) continue;
		if (!--left) {
			return sprintf(page,"tx[%d]:    0x%ld-0x%ld "
			    "(%6ld bytes), rsv %d cps, shp %d cps%s\n",i,
			    (unsigned long) (tx->send - eni_dev->ram),
			    tx->send-eni_dev->ram+tx->words*4-1,tx->words*4,
			    tx->reserved,tx->shaping,
			    tx == eni_dev->ubr ? " (UBR)" : "");
		}
		if (--left) continue;
		return sprintf(page,"%10sbacklog %u packets\n","",
		    skb_queue_len(&tx->backlog));
	}
	read_lock(&vcc_sklist_lock);
	for(i = 0; i < VCC_HTABLE_SIZE; ++i) {
		struct hlist_head *head = &vcc_hash[i];

		sk_for_each(s, node, head) {
			struct eni_vcc *eni_vcc;
			int length;

			vcc = atm_sk(s);
			if (vcc->dev != dev)
				continue;
			eni_vcc = ENI_VCC(vcc);
			if (--left) continue;
			length = sprintf(page,"vcc %4d: ",vcc->vci);
			if (eni_vcc->rx) {
				length += sprintf(page+length,"0x%ld-0x%ld "
				    "(%6ld bytes)",
				    (unsigned long) (eni_vcc->recv - eni_dev->ram),
				    eni_vcc->recv-eni_dev->ram+eni_vcc->words*4-1,
				    eni_vcc->words*4);
				if (eni_vcc->tx) length += sprintf(page+length,", ");
			}
			if (eni_vcc->tx)
				length += sprintf(page+length,"tx[%d], txing %d bytes",
				    eni_vcc->tx->index,eni_vcc->txing);
			page[length] = '\n';
			read_unlock(&vcc_sklist_lock);
			return length+1;
		}
	}
	read_unlock(&vcc_sklist_lock);
	for (i = 0; i < eni_dev->free_len; i++) {
		struct eni_free *fe = eni_dev->free_list+i;
		unsigned long offset;

		if (--left) continue;
		offset = (unsigned long) eni_dev->ram+eni_dev->base_diff;
		return sprintf(page,"free      %p-%p (%6d bytes)\n",
		    fe->start-offset,fe->start-offset+(1 << fe->order)-1,
		    1 << fe->order);
	}
	return 0;
}


static const struct atmdev_ops ops = {
	.open		= eni_open,
	.close		= eni_close,
	.ioctl		= eni_ioctl,
	.getsockopt	= eni_getsockopt,
	.setsockopt	= eni_setsockopt,
	.send		= eni_send,
	.phy_put	= eni_phy_put,
	.phy_get	= eni_phy_get,
	.change_qos	= eni_change_qos,
	.proc_read	= eni_proc_read
};


static int eni_init_one(struct pci_dev *pci_dev,
			const struct pci_device_id *ent)
{
	struct atm_dev *dev;
	struct eni_dev *eni_dev;
	struct eni_zero *zero;
	int rc;

	rc = pci_enable_device(pci_dev);
	if (rc < 0)
		goto out;

	rc = -ENOMEM;
	eni_dev = kmalloc(sizeof(struct eni_dev), GFP_KERNEL);
	if (!eni_dev)
		goto err_disable;

	zero = &eni_dev->zero;
	zero->addr = pci_alloc_consistent(pci_dev, ENI_ZEROES_SIZE, &zero->dma);
	if (!zero->addr)
		goto err_kfree;

	dev = atm_dev_register(DEV_LABEL, &pci_dev->dev, &ops, -1, NULL);
	if (!dev)
		goto err_free_consistent;

	dev->dev_data = eni_dev;
	pci_set_drvdata(pci_dev, dev);
	eni_dev->pci_dev = pci_dev;
	eni_dev->asic = ent->driver_data;

	rc = eni_do_init(dev);
	if (rc < 0)
		goto err_unregister;

	rc = eni_start(dev);
	if (rc < 0)
		goto err_eni_release;

	eni_dev->more = eni_boards;
	eni_boards = dev;
out:
	return rc;

err_eni_release:
	eni_do_release(dev);
err_unregister:
	atm_dev_deregister(dev);
err_free_consistent:
	pci_free_consistent(pci_dev, ENI_ZEROES_SIZE, zero->addr, zero->dma);
err_kfree:
	kfree(eni_dev);
err_disable:
	pci_disable_device(pci_dev);
	goto out;
}


static struct pci_device_id eni_pci_tbl[] = {
	{ PCI_VDEVICE(EF, PCI_DEVICE_ID_EF_ATM_FPGA), 0 /* FPGA */ },
	{ PCI_VDEVICE(EF, PCI_DEVICE_ID_EF_ATM_ASIC), 1 /* ASIC */ },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci,eni_pci_tbl);


static void eni_remove_one(struct pci_dev *pdev)
{
	struct atm_dev *dev = pci_get_drvdata(pdev);
	struct eni_dev *ed = ENI_DEV(dev);
	struct eni_zero *zero = &ed->zero;

	eni_do_release(dev);
	atm_dev_deregister(dev);
	pci_free_consistent(pdev, ENI_ZEROES_SIZE, zero->addr, zero->dma);
	kfree(ed);
	pci_disable_device(pdev);
}


static struct pci_driver eni_driver = {
	.name		= DEV_LABEL,
	.id_table	= eni_pci_tbl,
	.probe		= eni_init_one,
	.remove		= eni_remove_one,
};


static int __init eni_init(void)
{
	struct sk_buff *skb; /* dummy for sizeof */

	if (sizeof(skb->cb) < sizeof(struct eni_skb_prv)) {
		printk(KERN_ERR "eni_detect: skb->cb is too small (%Zd < %Zd)\n",
		    sizeof(skb->cb),sizeof(struct eni_skb_prv));
		return -EIO;
	}
	return pci_register_driver(&eni_driver);
}


module_init(eni_init);
/* @@@ since exit routine not defined, this module can not be unloaded */

MODULE_LICENSE("GPL");
