/* drivers/atm/zatm.c - ZeitNet ZN122x device driver */
 
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
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/uio.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/atm_zatm.h>
#include <linux/capability.h>
#include <linux/bitops.h>
#include <linux/wait.h>
#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/string.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>

#include "uPD98401.h"
#include "uPD98402.h"
#include "zeprom.h"
#include "zatm.h"


/*
 * TODO:
 *
 * Minor features
 *  - support 64 kB SDUs (will have to use multibuffer batches then :-( )
 *  - proper use of CDV, credit = max(1,CDVT*PCR)
 *  - AAL0
 *  - better receive timestamps
 *  - OAM
 */

#define ZATM_COPPER	1

#if 0
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif

#ifndef CONFIG_ATM_ZATM_DEBUG


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
  if ((unsigned long) (x) < 0x30) printk(KERN_CRIT #x "==0x%x\n", (int) (x))

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

	printk(KERN_NOTICE "----- event dump follows -----\n");
	for (n = 0; n < EV; n++) {
		i = (ec+n) % EV;
		printk(KERN_NOTICE);
		printk(ev[i] ? ev[i] : "(null)",ev_a[i],ev_b[i]);
	}
	printk(KERN_NOTICE "----- event dump ends here -----\n");
}


#endif /* CONFIG_ATM_ZATM_DEBUG */


#define RING_BUSY	1	/* indication from do_tx that PDU has to be
				   backlogged */

static struct atm_dev *zatm_boards = NULL;
static unsigned long dummy[2] = {0,0};


#define zin_n(r) inl(zatm_dev->base+r*4)
#define zin(r) inl(zatm_dev->base+uPD98401_##r*4)
#define zout(v,r) outl(v,zatm_dev->base+uPD98401_##r*4)
#define zwait while (zin(CMR) & uPD98401_BUSY)

/* RX0, RX1, TX0, TX1 */
static const int mbx_entries[NR_MBX] = { 1024,1024,1024,1024 };
static const int mbx_esize[NR_MBX] = { 16,16,4,4 }; /* entry size in bytes */

#define MBX_SIZE(i) (mbx_entries[i]*mbx_esize[i])


/*-------------------------------- utilities --------------------------------*/


static void zpokel(struct zatm_dev *zatm_dev,u32 value,u32 addr)
{
	zwait;
	zout(value,CER);
	zout(uPD98401_IND_ACC | uPD98401_IA_BALL |
	    (uPD98401_IA_TGT_CM << uPD98401_IA_TGT_SHIFT) | addr,CMR);
}


static u32 zpeekl(struct zatm_dev *zatm_dev,u32 addr)
{
	zwait;
	zout(uPD98401_IND_ACC | uPD98401_IA_BALL | uPD98401_IA_RW |
	  (uPD98401_IA_TGT_CM << uPD98401_IA_TGT_SHIFT) | addr,CMR);
	zwait;
	return zin(CER);
}


/*------------------------------- free lists --------------------------------*/


/*
 * Free buffer head structure:
 *   [0] pointer to buffer (for SAR)
 *   [1] buffer descr link pointer (for SAR)
 *   [2] back pointer to skb (for poll_rx)
 *   [3] data
 *   ...
 */

struct rx_buffer_head {
	u32		buffer;	/* pointer to buffer (for SAR) */
	u32		link;	/* buffer descriptor link pointer (for SAR) */
	struct sk_buff	*skb;	/* back pointer to skb (for poll_rx) */
};


static void refill_pool(struct atm_dev *dev,int pool)
{
	struct zatm_dev *zatm_dev;
	struct sk_buff *skb;
	struct rx_buffer_head *first;
	unsigned long flags;
	int align,offset,free,count,size;

	EVENT("refill_pool\n",0,0);
	zatm_dev = ZATM_DEV(dev);
	size = (64 << (pool <= ZATM_AAL5_POOL_BASE ? 0 :
	    pool-ZATM_AAL5_POOL_BASE))+sizeof(struct rx_buffer_head);
	if (size < PAGE_SIZE) {
		align = 32; /* for 32 byte alignment */
		offset = sizeof(struct rx_buffer_head);
	}
	else {
		align = 4096;
		offset = zatm_dev->pool_info[pool].offset+
		    sizeof(struct rx_buffer_head);
	}
	size += align;
	spin_lock_irqsave(&zatm_dev->lock, flags);
	free = zpeekl(zatm_dev,zatm_dev->pool_base+2*pool) &
	    uPD98401_RXFP_REMAIN;
	spin_unlock_irqrestore(&zatm_dev->lock, flags);
	if (free >= zatm_dev->pool_info[pool].low_water) return;
	EVENT("starting ... POOL: 0x%x, 0x%x\n",
	    zpeekl(zatm_dev,zatm_dev->pool_base+2*pool),
	    zpeekl(zatm_dev,zatm_dev->pool_base+2*pool+1));
	EVENT("dummy: 0x%08lx, 0x%08lx\n",dummy[0],dummy[1]);
	count = 0;
	first = NULL;
	while (free < zatm_dev->pool_info[pool].high_water) {
		struct rx_buffer_head *head;

		skb = alloc_skb(size,GFP_ATOMIC);
		if (!skb) {
			printk(KERN_WARNING DEV_LABEL "(Itf %d): got no new "
			    "skb (%d) with %d free\n",dev->number,size,free);
			break;
		}
		skb_reserve(skb,(unsigned char *) ((((unsigned long) skb->data+
		    align+offset-1) & ~(unsigned long) (align-1))-offset)-
		    skb->data);
		head = (struct rx_buffer_head *) skb->data;
		skb_reserve(skb,sizeof(struct rx_buffer_head));
		if (!first) first = head;
		count++;
		head->buffer = virt_to_bus(skb->data);
		head->link = 0;
		head->skb = skb;
		EVENT("enq skb 0x%08lx/0x%08lx\n",(unsigned long) skb,
		    (unsigned long) head);
		spin_lock_irqsave(&zatm_dev->lock, flags);
		if (zatm_dev->last_free[pool])
			((struct rx_buffer_head *) (zatm_dev->last_free[pool]->
			    data))[-1].link = virt_to_bus(head);
		zatm_dev->last_free[pool] = skb;
		skb_queue_tail(&zatm_dev->pool[pool],skb);
		spin_unlock_irqrestore(&zatm_dev->lock, flags);
		free++;
	}
	if (first) {
		spin_lock_irqsave(&zatm_dev->lock, flags);
		zwait;
		zout(virt_to_bus(first),CER);
		zout(uPD98401_ADD_BAT | (pool << uPD98401_POOL_SHIFT) | count,
		    CMR);
		spin_unlock_irqrestore(&zatm_dev->lock, flags);
		EVENT ("POOL: 0x%x, 0x%x\n",
		    zpeekl(zatm_dev,zatm_dev->pool_base+2*pool),
		    zpeekl(zatm_dev,zatm_dev->pool_base+2*pool+1));
		EVENT("dummy: 0x%08lx, 0x%08lx\n",dummy[0],dummy[1]);
	}
}


static void drain_free(struct atm_dev *dev,int pool)
{
	skb_queue_purge(&ZATM_DEV(dev)->pool[pool]);
}


static int pool_index(int max_pdu)
{
	int i;

	if (max_pdu % ATM_CELL_PAYLOAD)
		printk(KERN_ERR DEV_LABEL ": driver error in pool_index: "
		    "max_pdu is %d\n",max_pdu);
	if (max_pdu > 65536) return -1;
	for (i = 0; (64 << i) < max_pdu; i++);
	return i+ZATM_AAL5_POOL_BASE;
}


/* use_pool isn't reentrant */


static void use_pool(struct atm_dev *dev,int pool)
{
	struct zatm_dev *zatm_dev;
	unsigned long flags;
	int size;

	zatm_dev = ZATM_DEV(dev);
	if (!(zatm_dev->pool_info[pool].ref_count++)) {
		skb_queue_head_init(&zatm_dev->pool[pool]);
		size = pool-ZATM_AAL5_POOL_BASE;
		if (size < 0) size = 0; /* 64B... */
		else if (size > 10) size = 10; /* ... 64kB */
		spin_lock_irqsave(&zatm_dev->lock, flags);
		zpokel(zatm_dev,((zatm_dev->pool_info[pool].low_water/4) <<
		    uPD98401_RXFP_ALERT_SHIFT) |
		    (1 << uPD98401_RXFP_BTSZ_SHIFT) |
		    (size << uPD98401_RXFP_BFSZ_SHIFT),
		    zatm_dev->pool_base+pool*2);
		zpokel(zatm_dev,(unsigned long) dummy,zatm_dev->pool_base+
		    pool*2+1);
		spin_unlock_irqrestore(&zatm_dev->lock, flags);
		zatm_dev->last_free[pool] = NULL;
		refill_pool(dev,pool);
	}
	DPRINTK("pool %d: %d\n",pool,zatm_dev->pool_info[pool].ref_count);
}


static void unuse_pool(struct atm_dev *dev,int pool)
{
	if (!(--ZATM_DEV(dev)->pool_info[pool].ref_count))
		drain_free(dev,pool);
}

/*----------------------------------- RX ------------------------------------*/


#if 0
static void exception(struct atm_vcc *vcc)
{
   static int count = 0;
   struct zatm_dev *zatm_dev = ZATM_DEV(vcc->dev);
   struct zatm_vcc *zatm_vcc = ZATM_VCC(vcc);
   unsigned long *qrp;
   int i;

   if (count++ > 2) return;
   for (i = 0; i < 8; i++)
	printk("TX%d: 0x%08lx\n",i,
	  zpeekl(zatm_dev,zatm_vcc->tx_chan*VC_SIZE/4+i));
   for (i = 0; i < 5; i++)
	printk("SH%d: 0x%08lx\n",i,
	  zpeekl(zatm_dev,uPD98401_IM(zatm_vcc->shaper)+16*i));
   qrp = (unsigned long *) zpeekl(zatm_dev,zatm_vcc->tx_chan*VC_SIZE/4+
     uPD98401_TXVC_QRP);
   printk("qrp=0x%08lx\n",(unsigned long) qrp);
   for (i = 0; i < 4; i++) printk("QRP[%d]: 0x%08lx",i,qrp[i]);
}
#endif


static const char *err_txt[] = {
	"No error",
	"RX buf underflow",
	"RX FIFO overrun",
	"Maximum len violation",
	"CRC error",
	"User abort",
	"Length violation",
	"T1 error",
	"Deactivated",
	"???",
	"???",
	"???",
	"???",
	"???",
	"???",
	"???"
};


static void poll_rx(struct atm_dev *dev,int mbx)
{
	struct zatm_dev *zatm_dev;
	unsigned long pos;
	u32 x;
	int error;

	EVENT("poll_rx\n",0,0);
	zatm_dev = ZATM_DEV(dev);
	pos = (zatm_dev->mbx_start[mbx] & ~0xffffUL) | zin(MTA(mbx));
	while (x = zin(MWA(mbx)), (pos & 0xffff) != x) {
		u32 *here;
		struct sk_buff *skb;
		struct atm_vcc *vcc;
		int cells,size,chan;

		EVENT("MBX: host 0x%lx, nic 0x%x\n",pos,x);
		here = (u32 *) pos;
		if (((pos += 16) & 0xffff) == zatm_dev->mbx_end[mbx])
			pos = zatm_dev->mbx_start[mbx];
		cells = here[0] & uPD98401_AAL5_SIZE;
#if 0
printk("RX IND: 0x%x, 0x%x, 0x%x, 0x%x\n",here[0],here[1],here[2],here[3]);
{
unsigned long *x;
		printk("POOL: 0x%08x, 0x%08x\n",zpeekl(zatm_dev,
		      zatm_dev->pool_base),
		      zpeekl(zatm_dev,zatm_dev->pool_base+1));
		x = (unsigned long *) here[2];
		printk("[0..3] = 0x%08lx, 0x%08lx, 0x%08lx, 0x%08lx\n",
		    x[0],x[1],x[2],x[3]);
}
#endif
		error = 0;
		if (here[3] & uPD98401_AAL5_ERR) {
			error = (here[3] & uPD98401_AAL5_ES) >>
			    uPD98401_AAL5_ES_SHIFT;
			if (error == uPD98401_AAL5_ES_DEACT ||
			    error == uPD98401_AAL5_ES_FREE) continue;
		}
EVENT("error code 0x%x/0x%x\n",(here[3] & uPD98401_AAL5_ES) >>
  uPD98401_AAL5_ES_SHIFT,error);
		skb = ((struct rx_buffer_head *) bus_to_virt(here[2]))->skb;
		__net_timestamp(skb);
#if 0
printk("[-3..0] 0x%08lx 0x%08lx 0x%08lx 0x%08lx\n",((unsigned *) skb->data)[-3],
  ((unsigned *) skb->data)[-2],((unsigned *) skb->data)[-1],
  ((unsigned *) skb->data)[0]);
#endif
		EVENT("skb 0x%lx, here 0x%lx\n",(unsigned long) skb,
		    (unsigned long) here);
#if 0
printk("dummy: 0x%08lx, 0x%08lx\n",dummy[0],dummy[1]);
#endif
		size = error ? 0 : ntohs(((__be16 *) skb->data)[cells*
		    ATM_CELL_PAYLOAD/sizeof(u16)-3]);
		EVENT("got skb 0x%lx, size %d\n",(unsigned long) skb,size);
		chan = (here[3] & uPD98401_AAL5_CHAN) >>
		    uPD98401_AAL5_CHAN_SHIFT;
		if (chan < zatm_dev->chans && zatm_dev->rx_map[chan]) {
			int pos;
			vcc = zatm_dev->rx_map[chan];
			pos = ZATM_VCC(vcc)->pool;
			if (skb == zatm_dev->last_free[pos])
				zatm_dev->last_free[pos] = NULL;
			skb_unlink(skb, zatm_dev->pool + pos);
		}
		else {
			printk(KERN_ERR DEV_LABEL "(itf %d): RX indication "
			    "for non-existing channel\n",dev->number);
			size = 0;
			vcc = NULL;
			event_dump();
		}
		if (error) {
			static unsigned long silence = 0;
			static int last_error = 0;

			if (error != last_error ||
			    time_after(jiffies, silence)  || silence == 0){
				printk(KERN_WARNING DEV_LABEL "(itf %d): "
				    "chan %d error %s\n",dev->number,chan,
				    err_txt[error]);
				last_error = error;
				silence = (jiffies+2*HZ)|1;
			}
			size = 0;
		}
		if (size && (size > cells*ATM_CELL_PAYLOAD-ATM_AAL5_TRAILER ||
		    size <= (cells-1)*ATM_CELL_PAYLOAD-ATM_AAL5_TRAILER)) {
			printk(KERN_ERR DEV_LABEL "(itf %d): size %d with %d "
			    "cells\n",dev->number,size,cells);
			size = 0;
			event_dump();
		}
		if (size > ATM_MAX_AAL5_PDU) {
			printk(KERN_ERR DEV_LABEL "(itf %d): size too big "
			    "(%d)\n",dev->number,size);
			size = 0;
			event_dump();
		}
		if (!size) {
			dev_kfree_skb_irq(skb);
			if (vcc) atomic_inc(&vcc->stats->rx_err);
			continue;
		}
		if (!atm_charge(vcc,skb->truesize)) {
			dev_kfree_skb_irq(skb);
			continue;
		}
		skb->len = size;
		ATM_SKB(skb)->vcc = vcc;
		vcc->push(vcc,skb);
		atomic_inc(&vcc->stats->rx);
	}
	zout(pos & 0xffff,MTA(mbx));
#if 0 /* probably a stupid idea */
	refill_pool(dev,zatm_vcc->pool);
		/* maybe this saves us a few interrupts */
#endif
}


static int open_rx_first(struct atm_vcc *vcc)
{
	struct zatm_dev *zatm_dev;
	struct zatm_vcc *zatm_vcc;
	unsigned long flags;
	unsigned short chan;
	int cells;

	DPRINTK("open_rx_first (0x%x)\n",inb_p(0xc053));
	zatm_dev = ZATM_DEV(vcc->dev);
	zatm_vcc = ZATM_VCC(vcc);
	zatm_vcc->rx_chan = 0;
	if (vcc->qos.rxtp.traffic_class == ATM_NONE) return 0;
	if (vcc->qos.aal == ATM_AAL5) {
		if (vcc->qos.rxtp.max_sdu > 65464)
			vcc->qos.rxtp.max_sdu = 65464;
			/* fix this - we may want to receive 64kB SDUs
			   later */
		cells = DIV_ROUND_UP(vcc->qos.rxtp.max_sdu + ATM_AAL5_TRAILER,
				ATM_CELL_PAYLOAD);
		zatm_vcc->pool = pool_index(cells*ATM_CELL_PAYLOAD);
	}
	else {
		cells = 1;
		zatm_vcc->pool = ZATM_AAL0_POOL;
	}
	if (zatm_vcc->pool < 0) return -EMSGSIZE;
	spin_lock_irqsave(&zatm_dev->lock, flags);
	zwait;
	zout(uPD98401_OPEN_CHAN,CMR);
	zwait;
	DPRINTK("0x%x 0x%x\n",zin(CMR),zin(CER));
	chan = (zin(CMR) & uPD98401_CHAN_ADDR) >> uPD98401_CHAN_ADDR_SHIFT;
	spin_unlock_irqrestore(&zatm_dev->lock, flags);
	DPRINTK("chan is %d\n",chan);
	if (!chan) return -EAGAIN;
	use_pool(vcc->dev,zatm_vcc->pool);
	DPRINTK("pool %d\n",zatm_vcc->pool);
	/* set up VC descriptor */
	spin_lock_irqsave(&zatm_dev->lock, flags);
	zpokel(zatm_dev,zatm_vcc->pool << uPD98401_RXVC_POOL_SHIFT,
	    chan*VC_SIZE/4);
	zpokel(zatm_dev,uPD98401_RXVC_OD | (vcc->qos.aal == ATM_AAL5 ?
	    uPD98401_RXVC_AR : 0) | cells,chan*VC_SIZE/4+1);
	zpokel(zatm_dev,0,chan*VC_SIZE/4+2);
	zatm_vcc->rx_chan = chan;
	zatm_dev->rx_map[chan] = vcc;
	spin_unlock_irqrestore(&zatm_dev->lock, flags);
	return 0;
}


static int open_rx_second(struct atm_vcc *vcc)
{
	struct zatm_dev *zatm_dev;
	struct zatm_vcc *zatm_vcc;
	unsigned long flags;
	int pos,shift;

	DPRINTK("open_rx_second (0x%x)\n",inb_p(0xc053));
	zatm_dev = ZATM_DEV(vcc->dev);
	zatm_vcc = ZATM_VCC(vcc);
	if (!zatm_vcc->rx_chan) return 0;
	spin_lock_irqsave(&zatm_dev->lock, flags);
	/* should also handle VPI @@@ */
	pos = vcc->vci >> 1;
	shift = (1-(vcc->vci & 1)) << 4;
	zpokel(zatm_dev,(zpeekl(zatm_dev,pos) & ~(0xffff << shift)) |
	    ((zatm_vcc->rx_chan | uPD98401_RXLT_ENBL) << shift),pos);
	spin_unlock_irqrestore(&zatm_dev->lock, flags);
	return 0;
}


static void close_rx(struct atm_vcc *vcc)
{
	struct zatm_dev *zatm_dev;
	struct zatm_vcc *zatm_vcc;
	unsigned long flags;
	int pos,shift;

	zatm_vcc = ZATM_VCC(vcc);
	zatm_dev = ZATM_DEV(vcc->dev);
	if (!zatm_vcc->rx_chan) return;
	DPRINTK("close_rx\n");
	/* disable receiver */
	if (vcc->vpi != ATM_VPI_UNSPEC && vcc->vci != ATM_VCI_UNSPEC) {
		spin_lock_irqsave(&zatm_dev->lock, flags);
		pos = vcc->vci >> 1;
		shift = (1-(vcc->vci & 1)) << 4;
		zpokel(zatm_dev,zpeekl(zatm_dev,pos) & ~(0xffff << shift),pos);
		zwait;
		zout(uPD98401_NOP,CMR);
		zwait;
		zout(uPD98401_NOP,CMR);
		spin_unlock_irqrestore(&zatm_dev->lock, flags);
	}
	spin_lock_irqsave(&zatm_dev->lock, flags);
	zwait;
	zout(uPD98401_DEACT_CHAN | uPD98401_CHAN_RT | (zatm_vcc->rx_chan <<
	    uPD98401_CHAN_ADDR_SHIFT),CMR);
	zwait;
	udelay(10); /* why oh why ... ? */
	zout(uPD98401_CLOSE_CHAN | uPD98401_CHAN_RT | (zatm_vcc->rx_chan <<
	    uPD98401_CHAN_ADDR_SHIFT),CMR);
	zwait;
	if (!(zin(CMR) & uPD98401_CHAN_ADDR))
		printk(KERN_CRIT DEV_LABEL "(itf %d): can't close RX channel "
		    "%d\n",vcc->dev->number,zatm_vcc->rx_chan);
	spin_unlock_irqrestore(&zatm_dev->lock, flags);
	zatm_dev->rx_map[zatm_vcc->rx_chan] = NULL;
	zatm_vcc->rx_chan = 0;
	unuse_pool(vcc->dev,zatm_vcc->pool);
}


static int start_rx(struct atm_dev *dev)
{
	struct zatm_dev *zatm_dev;
	int size,i;

DPRINTK("start_rx\n");
	zatm_dev = ZATM_DEV(dev);
	size = sizeof(struct atm_vcc *)*zatm_dev->chans;
	zatm_dev->rx_map =  kzalloc(size,GFP_KERNEL);
	if (!zatm_dev->rx_map) return -ENOMEM;
	/* set VPI/VCI split (use all VCIs and give what's left to VPIs) */
	zpokel(zatm_dev,(1 << dev->ci_range.vci_bits)-1,uPD98401_VRR);
	/* prepare free buffer pools */
	for (i = 0; i <= ZATM_LAST_POOL; i++) {
		zatm_dev->pool_info[i].ref_count = 0;
		zatm_dev->pool_info[i].rqa_count = 0;
		zatm_dev->pool_info[i].rqu_count = 0;
		zatm_dev->pool_info[i].low_water = LOW_MARK;
		zatm_dev->pool_info[i].high_water = HIGH_MARK;
		zatm_dev->pool_info[i].offset = 0;
		zatm_dev->pool_info[i].next_off = 0;
		zatm_dev->pool_info[i].next_cnt = 0;
		zatm_dev->pool_info[i].next_thres = OFF_CNG_THRES;
	}
	return 0;
}


/*----------------------------------- TX ------------------------------------*/


static int do_tx(struct sk_buff *skb)
{
	struct atm_vcc *vcc;
	struct zatm_dev *zatm_dev;
	struct zatm_vcc *zatm_vcc;
	u32 *dsc;
	unsigned long flags;

	EVENT("do_tx\n",0,0);
	DPRINTK("sending skb %p\n",skb);
	vcc = ATM_SKB(skb)->vcc;
	zatm_dev = ZATM_DEV(vcc->dev);
	zatm_vcc = ZATM_VCC(vcc);
	EVENT("iovcnt=%d\n",skb_shinfo(skb)->nr_frags,0);
	spin_lock_irqsave(&zatm_dev->lock, flags);
	if (!skb_shinfo(skb)->nr_frags) {
		if (zatm_vcc->txing == RING_ENTRIES-1) {
			spin_unlock_irqrestore(&zatm_dev->lock, flags);
			return RING_BUSY;
		}
		zatm_vcc->txing++;
		dsc = zatm_vcc->ring+zatm_vcc->ring_curr;
		zatm_vcc->ring_curr = (zatm_vcc->ring_curr+RING_WORDS) &
		    (RING_ENTRIES*RING_WORDS-1);
		dsc[1] = 0;
		dsc[2] = skb->len;
		dsc[3] = virt_to_bus(skb->data);
		mb();
		dsc[0] = uPD98401_TXPD_V | uPD98401_TXPD_DP | uPD98401_TXPD_SM
		    | (vcc->qos.aal == ATM_AAL5 ? uPD98401_TXPD_AAL5 : 0 |
		    (ATM_SKB(skb)->atm_options & ATM_ATMOPT_CLP ?
		    uPD98401_CLPM_1 : uPD98401_CLPM_0));
		EVENT("dsc (0x%lx)\n",(unsigned long) dsc,0);
	}
	else {
printk("NONONONOO!!!!\n");
		dsc = NULL;
#if 0
		u32 *put;
		int i;

		dsc = kmalloc(uPD98401_TXPD_SIZE * 2 +
			uPD98401_TXBD_SIZE * ATM_SKB(skb)->iovcnt, GFP_ATOMIC);
		if (!dsc) {
			if (vcc->pop)
				vcc->pop(vcc, skb);
			else
				dev_kfree_skb_irq(skb);
			return -EAGAIN;
		}
		/* @@@ should check alignment */
		put = dsc+8;
		dsc[0] = uPD98401_TXPD_V | uPD98401_TXPD_DP |
		    (vcc->aal == ATM_AAL5 ? uPD98401_TXPD_AAL5 : 0 |
		    (ATM_SKB(skb)->atm_options & ATM_ATMOPT_CLP ?
		    uPD98401_CLPM_1 : uPD98401_CLPM_0));
		dsc[1] = 0;
		dsc[2] = ATM_SKB(skb)->iovcnt * uPD98401_TXBD_SIZE;
		dsc[3] = virt_to_bus(put);
		for (i = 0; i < ATM_SKB(skb)->iovcnt; i++) {
			*put++ = ((struct iovec *) skb->data)[i].iov_len;
			*put++ = virt_to_bus(((struct iovec *)
			    skb->data)[i].iov_base);
		}
		put[-2] |= uPD98401_TXBD_LAST;
#endif
	}
	ZATM_PRV_DSC(skb) = dsc;
	skb_queue_tail(&zatm_vcc->tx_queue,skb);
	DPRINTK("QRP=0x%08lx\n",zpeekl(zatm_dev,zatm_vcc->tx_chan*VC_SIZE/4+
	  uPD98401_TXVC_QRP));
	zwait;
	zout(uPD98401_TX_READY | (zatm_vcc->tx_chan <<
	    uPD98401_CHAN_ADDR_SHIFT),CMR);
	spin_unlock_irqrestore(&zatm_dev->lock, flags);
	EVENT("done\n",0,0);
	return 0;
}


static inline void dequeue_tx(struct atm_vcc *vcc)
{
	struct zatm_vcc *zatm_vcc;
	struct sk_buff *skb;

	EVENT("dequeue_tx\n",0,0);
	zatm_vcc = ZATM_VCC(vcc);
	skb = skb_dequeue(&zatm_vcc->tx_queue);
	if (!skb) {
		printk(KERN_CRIT DEV_LABEL "(itf %d): dequeue_tx but not "
		    "txing\n",vcc->dev->number);
		return;
	}
#if 0 /* @@@ would fail on CLP */
if (*ZATM_PRV_DSC(skb) != (uPD98401_TXPD_V | uPD98401_TXPD_DP |
  uPD98401_TXPD_SM | uPD98401_TXPD_AAL5)) printk("@#*$!!!!  (%08x)\n",
  *ZATM_PRV_DSC(skb));
#endif
	*ZATM_PRV_DSC(skb) = 0; /* mark as invalid */
	zatm_vcc->txing--;
	if (vcc->pop) vcc->pop(vcc,skb);
	else dev_kfree_skb_irq(skb);
	while ((skb = skb_dequeue(&zatm_vcc->backlog)))
		if (do_tx(skb) == RING_BUSY) {
			skb_queue_head(&zatm_vcc->backlog,skb);
			break;
		}
	atomic_inc(&vcc->stats->tx);
	wake_up(&zatm_vcc->tx_wait);
}


static void poll_tx(struct atm_dev *dev,int mbx)
{
	struct zatm_dev *zatm_dev;
	unsigned long pos;
	u32 x;

	EVENT("poll_tx\n",0,0);
	zatm_dev = ZATM_DEV(dev);
	pos = (zatm_dev->mbx_start[mbx] & ~0xffffUL) | zin(MTA(mbx));
	while (x = zin(MWA(mbx)), (pos & 0xffff) != x) {
		int chan;

#if 1
		u32 data,*addr;

		EVENT("MBX: host 0x%lx, nic 0x%x\n",pos,x);
		addr = (u32 *) pos;
		data = *addr;
		chan = (data & uPD98401_TXI_CONN) >> uPD98401_TXI_CONN_SHIFT;
		EVENT("addr = 0x%lx, data = 0x%08x,",(unsigned long) addr,
		    data);
		EVENT("chan = %d\n",chan,0);
#else
NO !
		chan = (zatm_dev->mbx_start[mbx][pos >> 2] & uPD98401_TXI_CONN)
		>> uPD98401_TXI_CONN_SHIFT;
#endif
		if (chan < zatm_dev->chans && zatm_dev->tx_map[chan])
			dequeue_tx(zatm_dev->tx_map[chan]);
		else {
			printk(KERN_CRIT DEV_LABEL "(itf %d): TX indication "
			    "for non-existing channel %d\n",dev->number,chan);
			event_dump();
		}
		if (((pos += 4) & 0xffff) == zatm_dev->mbx_end[mbx])
			pos = zatm_dev->mbx_start[mbx];
	}
	zout(pos & 0xffff,MTA(mbx));
}


/*
 * BUG BUG BUG: Doesn't handle "new-style" rate specification yet.
 */

static int alloc_shaper(struct atm_dev *dev,int *pcr,int min,int max,int ubr)
{
	struct zatm_dev *zatm_dev;
	unsigned long flags;
	unsigned long i,m,c;
	int shaper;

	DPRINTK("alloc_shaper (min = %d, max = %d)\n",min,max);
	zatm_dev = ZATM_DEV(dev);
	if (!zatm_dev->free_shapers) return -EAGAIN;
	for (shaper = 0; !((zatm_dev->free_shapers >> shaper) & 1); shaper++);
	zatm_dev->free_shapers &= ~1 << shaper;
	if (ubr) {
		c = 5;
		i = m = 1;
		zatm_dev->ubr_ref_cnt++;
		zatm_dev->ubr = shaper;
		*pcr = 0;
	}
	else {
		if (min) {
			if (min <= 255) {
				i = min;
				m = ATM_OC3_PCR;
			}
			else {
				i = 255;
				m = ATM_OC3_PCR*255/min;
			}
		}
		else {
			if (max > zatm_dev->tx_bw) max = zatm_dev->tx_bw;
			if (max <= 255) {
				i = max;
				m = ATM_OC3_PCR;
			}
			else {
				i = 255;
				m = DIV_ROUND_UP(ATM_OC3_PCR*255, max);
			}
		}
		if (i > m) {
			printk(KERN_CRIT DEV_LABEL "shaper algorithm botched "
			    "[%d,%d] -> i=%ld,m=%ld\n",min,max,i,m);
			m = i;
		}
		*pcr = i*ATM_OC3_PCR/m;
		c = 20; /* @@@ should use max_cdv ! */
		if ((min && *pcr < min) || (max && *pcr > max)) return -EINVAL;
		if (zatm_dev->tx_bw < *pcr) return -EAGAIN;
		zatm_dev->tx_bw -= *pcr;
	}
	spin_lock_irqsave(&zatm_dev->lock, flags);
	DPRINTK("i = %d, m = %d, PCR = %d\n",i,m,*pcr);
	zpokel(zatm_dev,(i << uPD98401_IM_I_SHIFT) | m,uPD98401_IM(shaper));
	zpokel(zatm_dev,c << uPD98401_PC_C_SHIFT,uPD98401_PC(shaper));
	zpokel(zatm_dev,0,uPD98401_X(shaper));
	zpokel(zatm_dev,0,uPD98401_Y(shaper));
	zpokel(zatm_dev,uPD98401_PS_E,uPD98401_PS(shaper));
	spin_unlock_irqrestore(&zatm_dev->lock, flags);
	return shaper;
}


static void dealloc_shaper(struct atm_dev *dev,int shaper)
{
	struct zatm_dev *zatm_dev;
	unsigned long flags;

	zatm_dev = ZATM_DEV(dev);
	if (shaper == zatm_dev->ubr) {
		if (--zatm_dev->ubr_ref_cnt) return;
		zatm_dev->ubr = -1;
	}
	spin_lock_irqsave(&zatm_dev->lock, flags);
	zpokel(zatm_dev,zpeekl(zatm_dev,uPD98401_PS(shaper)) & ~uPD98401_PS_E,
	    uPD98401_PS(shaper));
	spin_unlock_irqrestore(&zatm_dev->lock, flags);
	zatm_dev->free_shapers |= 1 << shaper;
}


static void close_tx(struct atm_vcc *vcc)
{
	struct zatm_dev *zatm_dev;
	struct zatm_vcc *zatm_vcc;
	unsigned long flags;
	int chan;

	zatm_vcc = ZATM_VCC(vcc);
	zatm_dev = ZATM_DEV(vcc->dev);
	chan = zatm_vcc->tx_chan;
	if (!chan) return;
	DPRINTK("close_tx\n");
	if (skb_peek(&zatm_vcc->backlog)) {
		printk("waiting for backlog to drain ...\n");
		event_dump();
		wait_event(zatm_vcc->tx_wait, !skb_peek(&zatm_vcc->backlog));
	}
	if (skb_peek(&zatm_vcc->tx_queue)) {
		printk("waiting for TX queue to drain ...\n");
		event_dump();
		wait_event(zatm_vcc->tx_wait, !skb_peek(&zatm_vcc->tx_queue));
	}
	spin_lock_irqsave(&zatm_dev->lock, flags);
#if 0
	zwait;
	zout(uPD98401_DEACT_CHAN | (chan << uPD98401_CHAN_ADDR_SHIFT),CMR);
#endif
	zwait;
	zout(uPD98401_CLOSE_CHAN | (chan << uPD98401_CHAN_ADDR_SHIFT),CMR);
	zwait;
	if (!(zin(CMR) & uPD98401_CHAN_ADDR))
		printk(KERN_CRIT DEV_LABEL "(itf %d): can't close TX channel "
		    "%d\n",vcc->dev->number,chan);
	spin_unlock_irqrestore(&zatm_dev->lock, flags);
	zatm_vcc->tx_chan = 0;
	zatm_dev->tx_map[chan] = NULL;
	if (zatm_vcc->shaper != zatm_dev->ubr) {
		zatm_dev->tx_bw += vcc->qos.txtp.min_pcr;
		dealloc_shaper(vcc->dev,zatm_vcc->shaper);
	}
	kfree(zatm_vcc->ring);
}


static int open_tx_first(struct atm_vcc *vcc)
{
	struct zatm_dev *zatm_dev;
	struct zatm_vcc *zatm_vcc;
	unsigned long flags;
	u32 *loop;
	unsigned short chan;
	int unlimited;

	DPRINTK("open_tx_first\n");
	zatm_dev = ZATM_DEV(vcc->dev);
	zatm_vcc = ZATM_VCC(vcc);
	zatm_vcc->tx_chan = 0;
	if (vcc->qos.txtp.traffic_class == ATM_NONE) return 0;
	spin_lock_irqsave(&zatm_dev->lock, flags);
	zwait;
	zout(uPD98401_OPEN_CHAN,CMR);
	zwait;
	DPRINTK("0x%x 0x%x\n",zin(CMR),zin(CER));
	chan = (zin(CMR) & uPD98401_CHAN_ADDR) >> uPD98401_CHAN_ADDR_SHIFT;
	spin_unlock_irqrestore(&zatm_dev->lock, flags);
	DPRINTK("chan is %d\n",chan);
	if (!chan) return -EAGAIN;
	unlimited = vcc->qos.txtp.traffic_class == ATM_UBR &&
	    (!vcc->qos.txtp.max_pcr || vcc->qos.txtp.max_pcr == ATM_MAX_PCR ||
	    vcc->qos.txtp.max_pcr >= ATM_OC3_PCR);
	if (unlimited && zatm_dev->ubr != -1) zatm_vcc->shaper = zatm_dev->ubr;
	else {
		int uninitialized_var(pcr);

		if (unlimited) vcc->qos.txtp.max_sdu = ATM_MAX_AAL5_PDU;
		if ((zatm_vcc->shaper = alloc_shaper(vcc->dev,&pcr,
		    vcc->qos.txtp.min_pcr,vcc->qos.txtp.max_pcr,unlimited))
		    < 0) {
			close_tx(vcc);
			return zatm_vcc->shaper;
		}
		if (pcr > ATM_OC3_PCR) pcr = ATM_OC3_PCR;
		vcc->qos.txtp.min_pcr = vcc->qos.txtp.max_pcr = pcr;
	}
	zatm_vcc->tx_chan = chan;
	skb_queue_head_init(&zatm_vcc->tx_queue);
	init_waitqueue_head(&zatm_vcc->tx_wait);
	/* initialize ring */
	zatm_vcc->ring = kzalloc(RING_SIZE,GFP_KERNEL);
	if (!zatm_vcc->ring) return -ENOMEM;
	loop = zatm_vcc->ring+RING_ENTRIES*RING_WORDS;
	loop[0] = uPD98401_TXPD_V;
	loop[1] = loop[2] = 0;
	loop[3] = virt_to_bus(zatm_vcc->ring);
	zatm_vcc->ring_curr = 0;
	zatm_vcc->txing = 0;
	skb_queue_head_init(&zatm_vcc->backlog);
	zpokel(zatm_dev,virt_to_bus(zatm_vcc->ring),
	    chan*VC_SIZE/4+uPD98401_TXVC_QRP);
	return 0;
}


static int open_tx_second(struct atm_vcc *vcc)
{
	struct zatm_dev *zatm_dev;
	struct zatm_vcc *zatm_vcc;
	unsigned long flags;

	DPRINTK("open_tx_second\n");
	zatm_dev = ZATM_DEV(vcc->dev);
	zatm_vcc = ZATM_VCC(vcc);
	if (!zatm_vcc->tx_chan) return 0;
	/* set up VC descriptor */
	spin_lock_irqsave(&zatm_dev->lock, flags);
	zpokel(zatm_dev,0,zatm_vcc->tx_chan*VC_SIZE/4);
	zpokel(zatm_dev,uPD98401_TXVC_L | (zatm_vcc->shaper <<
	    uPD98401_TXVC_SHP_SHIFT) | (vcc->vpi << uPD98401_TXVC_VPI_SHIFT) |
	    vcc->vci,zatm_vcc->tx_chan*VC_SIZE/4+1);
	zpokel(zatm_dev,0,zatm_vcc->tx_chan*VC_SIZE/4+2);
	spin_unlock_irqrestore(&zatm_dev->lock, flags);
	zatm_dev->tx_map[zatm_vcc->tx_chan] = vcc;
	return 0;
}


static int start_tx(struct atm_dev *dev)
{
	struct zatm_dev *zatm_dev;
	int i;

	DPRINTK("start_tx\n");
	zatm_dev = ZATM_DEV(dev);
	zatm_dev->tx_map = kmalloc(sizeof(struct atm_vcc *)*
	    zatm_dev->chans,GFP_KERNEL);
	if (!zatm_dev->tx_map) return -ENOMEM;
	zatm_dev->tx_bw = ATM_OC3_PCR;
	zatm_dev->free_shapers = (1 << NR_SHAPERS)-1;
	zatm_dev->ubr = -1;
	zatm_dev->ubr_ref_cnt = 0;
	/* initialize shapers */
	for (i = 0; i < NR_SHAPERS; i++) zpokel(zatm_dev,0,uPD98401_PS(i));
	return 0;
}


/*------------------------------- interrupts --------------------------------*/


static irqreturn_t zatm_int(int irq,void *dev_id)
{
	struct atm_dev *dev;
	struct zatm_dev *zatm_dev;
	u32 reason;
	int handled = 0;

	dev = dev_id;
	zatm_dev = ZATM_DEV(dev);
	while ((reason = zin(GSR))) {
		handled = 1;
		EVENT("reason 0x%x\n",reason,0);
		if (reason & uPD98401_INT_PI) {
			EVENT("PHY int\n",0,0);
			dev->phy->interrupt(dev);
		}
		if (reason & uPD98401_INT_RQA) {
			unsigned long pools;
			int i;

			pools = zin(RQA);
			EVENT("RQA (0x%08x)\n",pools,0);
			for (i = 0; pools; i++) {
				if (pools & 1) {
					refill_pool(dev,i);
					zatm_dev->pool_info[i].rqa_count++;
				}
				pools >>= 1;
			}
		}
		if (reason & uPD98401_INT_RQU) {
			unsigned long pools;
			int i;
			pools = zin(RQU);
			printk(KERN_WARNING DEV_LABEL "(itf %d): RQU 0x%08lx\n",
			    dev->number,pools);
			event_dump();
			for (i = 0; pools; i++) {
				if (pools & 1) {
					refill_pool(dev,i);
					zatm_dev->pool_info[i].rqu_count++;
				}
				pools >>= 1;
			}
		}
		/* don't handle RD */
		if (reason & uPD98401_INT_SPE)
			printk(KERN_ALERT DEV_LABEL "(itf %d): system parity "
			    "error at 0x%08x\n",dev->number,zin(ADDR));
		if (reason & uPD98401_INT_CPE)
			printk(KERN_ALERT DEV_LABEL "(itf %d): control memory "
			    "parity error at 0x%08x\n",dev->number,zin(ADDR));
		if (reason & uPD98401_INT_SBE) {
			printk(KERN_ALERT DEV_LABEL "(itf %d): system bus "
			    "error at 0x%08x\n",dev->number,zin(ADDR));
			event_dump();
		}
		/* don't handle IND */
		if (reason & uPD98401_INT_MF) {
			printk(KERN_CRIT DEV_LABEL "(itf %d): mailbox full "
			    "(0x%x)\n",dev->number,(reason & uPD98401_INT_MF)
			    >> uPD98401_INT_MF_SHIFT);
			event_dump();
			    /* @@@ should try to recover */
		}
		if (reason & uPD98401_INT_MM) {
			if (reason & 1) poll_rx(dev,0);
			if (reason & 2) poll_rx(dev,1);
			if (reason & 4) poll_tx(dev,2);
			if (reason & 8) poll_tx(dev,3);
		}
		/* @@@ handle RCRn */
	}
	return IRQ_RETVAL(handled);
}


/*----------------------------- (E)EPROM access -----------------------------*/


static void __devinit eprom_set(struct zatm_dev *zatm_dev,unsigned long value,
    unsigned short cmd)
{
	int error;

	if ((error = pci_write_config_dword(zatm_dev->pci_dev,cmd,value)))
		printk(KERN_ERR DEV_LABEL ": PCI write failed (0x%02x)\n",
		    error);
}


static unsigned long __devinit eprom_get(struct zatm_dev *zatm_dev,
    unsigned short cmd)
{
	unsigned int value;
	int error;

	if ((error = pci_read_config_dword(zatm_dev->pci_dev,cmd,&value)))
		printk(KERN_ERR DEV_LABEL ": PCI read failed (0x%02x)\n",
		    error);
	return value;
}


static void __devinit eprom_put_bits(struct zatm_dev *zatm_dev,
    unsigned long data,int bits,unsigned short cmd)
{
	unsigned long value;
	int i;

	for (i = bits-1; i >= 0; i--) {
		value = ZEPROM_CS | (((data >> i) & 1) ? ZEPROM_DI : 0);
		eprom_set(zatm_dev,value,cmd);
		eprom_set(zatm_dev,value | ZEPROM_SK,cmd);
		eprom_set(zatm_dev,value,cmd);
	}
}


static void __devinit eprom_get_byte(struct zatm_dev *zatm_dev,
    unsigned char *byte,unsigned short cmd)
{
	int i;

	*byte = 0;
	for (i = 8; i; i--) {
		eprom_set(zatm_dev,ZEPROM_CS,cmd);
		eprom_set(zatm_dev,ZEPROM_CS | ZEPROM_SK,cmd);
		*byte <<= 1;
		if (eprom_get(zatm_dev,cmd) & ZEPROM_DO) *byte |= 1;
		eprom_set(zatm_dev,ZEPROM_CS,cmd);
	}
}


static unsigned char __devinit eprom_try_esi(struct atm_dev *dev,
    unsigned short cmd,int offset,int swap)
{
	unsigned char buf[ZEPROM_SIZE];
	struct zatm_dev *zatm_dev;
	int i;

	zatm_dev = ZATM_DEV(dev);
	for (i = 0; i < ZEPROM_SIZE; i += 2) {
		eprom_set(zatm_dev,ZEPROM_CS,cmd); /* select EPROM */
		eprom_put_bits(zatm_dev,ZEPROM_CMD_READ,ZEPROM_CMD_LEN,cmd);
		eprom_put_bits(zatm_dev,i >> 1,ZEPROM_ADDR_LEN,cmd);
		eprom_get_byte(zatm_dev,buf+i+swap,cmd);
		eprom_get_byte(zatm_dev,buf+i+1-swap,cmd);
		eprom_set(zatm_dev,0,cmd); /* deselect EPROM */
	}
	memcpy(dev->esi,buf+offset,ESI_LEN);
	return memcmp(dev->esi,"\0\0\0\0\0",ESI_LEN); /* assumes ESI_LEN == 6 */
}


static void __devinit eprom_get_esi(struct atm_dev *dev)
{
	if (eprom_try_esi(dev,ZEPROM_V1_REG,ZEPROM_V1_ESI_OFF,1)) return;
	(void) eprom_try_esi(dev,ZEPROM_V2_REG,ZEPROM_V2_ESI_OFF,0);
}


/*--------------------------------- entries ---------------------------------*/


static int __devinit zatm_init(struct atm_dev *dev)
{
	struct zatm_dev *zatm_dev;
	struct pci_dev *pci_dev;
	unsigned short command;
	int error,i,last;
	unsigned long t0,t1,t2;

	DPRINTK(">zatm_init\n");
	zatm_dev = ZATM_DEV(dev);
	spin_lock_init(&zatm_dev->lock);
	pci_dev = zatm_dev->pci_dev;
	zatm_dev->base = pci_resource_start(pci_dev, 0);
	zatm_dev->irq = pci_dev->irq;
	if ((error = pci_read_config_word(pci_dev,PCI_COMMAND,&command))) {
		printk(KERN_ERR DEV_LABEL "(itf %d): init error 0x%02x\n",
		    dev->number,error);
		return -EINVAL;
	}
	if ((error = pci_write_config_word(pci_dev,PCI_COMMAND,
	    command | PCI_COMMAND_IO | PCI_COMMAND_MASTER))) {
		printk(KERN_ERR DEV_LABEL "(itf %d): can't enable IO (0x%02x)"
		    "\n",dev->number,error);
		return -EIO;
	}
	eprom_get_esi(dev);
	printk(KERN_NOTICE DEV_LABEL "(itf %d): rev.%d,base=0x%x,irq=%d,",
	    dev->number,pci_dev->revision,zatm_dev->base,zatm_dev->irq);
	/* reset uPD98401 */
	zout(0,SWR);
	while (!(zin(GSR) & uPD98401_INT_IND));
	zout(uPD98401_GMR_ONE /*uPD98401_BURST4*/,GMR);
	last = MAX_CRAM_SIZE;
	for (i = last-RAM_INCREMENT; i >= 0; i -= RAM_INCREMENT) {
		zpokel(zatm_dev,0x55555555,i);
		if (zpeekl(zatm_dev,i) != 0x55555555) last = i;
		else {
			zpokel(zatm_dev,0xAAAAAAAA,i);
			if (zpeekl(zatm_dev,i) != 0xAAAAAAAA) last = i;
			else zpokel(zatm_dev,i,i);
		}
	}
	for (i = 0; i < last; i += RAM_INCREMENT)
		if (zpeekl(zatm_dev,i) != i) break;
	zatm_dev->mem = i << 2;
	while (i) zpokel(zatm_dev,0,--i);
	/* reset again to rebuild memory pointers */
	zout(0,SWR);
	while (!(zin(GSR) & uPD98401_INT_IND));
	zout(uPD98401_GMR_ONE | uPD98401_BURST8 | uPD98401_BURST4 |
	    uPD98401_BURST2 | uPD98401_GMR_PM | uPD98401_GMR_DR,GMR);
	/* TODO: should shrink allocation now */
	printk("mem=%dkB,%s (",zatm_dev->mem >> 10,zatm_dev->copper ? "UTP" :
	    "MMF");
	for (i = 0; i < ESI_LEN; i++)
		printk("%02X%s",dev->esi[i],i == ESI_LEN-1 ? ")\n" : "-");
	do {
		unsigned long flags;

		spin_lock_irqsave(&zatm_dev->lock, flags);
		t0 = zpeekl(zatm_dev,uPD98401_TSR);
		udelay(10);
		t1 = zpeekl(zatm_dev,uPD98401_TSR);
		udelay(1010);
		t2 = zpeekl(zatm_dev,uPD98401_TSR);
		spin_unlock_irqrestore(&zatm_dev->lock, flags);
	}
	while (t0 > t1 || t1 > t2); /* loop if wrapping ... */
	zatm_dev->khz = t2-2*t1+t0;
	printk(KERN_NOTICE DEV_LABEL "(itf %d): uPD98401 %d.%d at %d.%03d "
	    "MHz\n",dev->number,
	    (zin(VER) & uPD98401_MAJOR) >> uPD98401_MAJOR_SHIFT,
            zin(VER) & uPD98401_MINOR,zatm_dev->khz/1000,zatm_dev->khz % 1000);
	return uPD98402_init(dev);
}


static int __devinit zatm_start(struct atm_dev *dev)
{
	struct zatm_dev *zatm_dev = ZATM_DEV(dev);
	struct pci_dev *pdev = zatm_dev->pci_dev;
	unsigned long curr;
	int pools,vccs,rx;
	int error, i, ld;

	DPRINTK("zatm_start\n");
	zatm_dev->rx_map = zatm_dev->tx_map = NULL;
 	for (i = 0; i < NR_MBX; i++)
 		zatm_dev->mbx_start[i] = 0;
 	error = request_irq(zatm_dev->irq, zatm_int, IRQF_SHARED, DEV_LABEL, dev);
	if (error < 0) {
 		printk(KERN_ERR DEV_LABEL "(itf %d): IRQ%d is already in use\n",
 		    dev->number,zatm_dev->irq);
		goto done;
	}
	/* define memory regions */
	pools = NR_POOLS;
	if (NR_SHAPERS*SHAPER_SIZE > pools*POOL_SIZE)
		pools = NR_SHAPERS*SHAPER_SIZE/POOL_SIZE;
	vccs = (zatm_dev->mem-NR_SHAPERS*SHAPER_SIZE-pools*POOL_SIZE)/
	    (2*VC_SIZE+RX_SIZE);
	ld = -1;
	for (rx = 1; rx < vccs; rx <<= 1) ld++;
	dev->ci_range.vpi_bits = 0; /* @@@ no VPI for now */
	dev->ci_range.vci_bits = ld;
	dev->link_rate = ATM_OC3_PCR;
	zatm_dev->chans = vccs; /* ??? */
	curr = rx*RX_SIZE/4;
	DPRINTK("RX pool 0x%08lx\n",curr);
	zpokel(zatm_dev,curr,uPD98401_PMA); /* receive pool */
	zatm_dev->pool_base = curr;
	curr += pools*POOL_SIZE/4;
	DPRINTK("Shapers 0x%08lx\n",curr);
	zpokel(zatm_dev,curr,uPD98401_SMA); /* shapers */
	curr += NR_SHAPERS*SHAPER_SIZE/4;
	DPRINTK("Free    0x%08lx\n",curr);
	zpokel(zatm_dev,curr,uPD98401_TOS); /* free pool */
	printk(KERN_INFO DEV_LABEL "(itf %d): %d shapers, %d pools, %d RX, "
	    "%ld VCs\n",dev->number,NR_SHAPERS,pools,rx,
	    (zatm_dev->mem-curr*4)/VC_SIZE);
	/* create mailboxes */
	for (i = 0; i < NR_MBX; i++) {
		void *mbx;
		dma_addr_t mbx_dma;

		if (!mbx_entries[i])
			continue;
		mbx = pci_alloc_consistent(pdev, 2*MBX_SIZE(i), &mbx_dma);
		if (!mbx) {
			error = -ENOMEM;
			goto out;
		}
		/*
		 * Alignment provided by pci_alloc_consistent() isn't enough
		 * for this device.
		 */
		if (((unsigned long)mbx ^ mbx_dma) & 0xffff) {
			printk(KERN_ERR DEV_LABEL "(itf %d): system "
			       "bus incompatible with driver\n", dev->number);
			pci_free_consistent(pdev, 2*MBX_SIZE(i), mbx, mbx_dma);
			error = -ENODEV;
			goto out;
		}
		DPRINTK("mbx@0x%08lx-0x%08lx\n", mbx, mbx + MBX_SIZE(i));
		zatm_dev->mbx_start[i] = (unsigned long)mbx;
		zatm_dev->mbx_dma[i] = mbx_dma;
		zatm_dev->mbx_end[i] = (zatm_dev->mbx_start[i] + MBX_SIZE(i)) &
					0xffff;
		zout(mbx_dma >> 16, MSH(i));
		zout(mbx_dma, MSL(i));
		zout(zatm_dev->mbx_end[i], MBA(i));
		zout((unsigned long)mbx & 0xffff, MTA(i));
		zout((unsigned long)mbx & 0xffff, MWA(i));
	}
	error = start_tx(dev);
	if (error)
		goto out;
	error = start_rx(dev);
	if (error)
		goto out_tx;
	error = dev->phy->start(dev);
	if (error)
		goto out_rx;
	zout(0xffffffff,IMR); /* enable interrupts */
	/* enable TX & RX */
	zout(zin(GMR) | uPD98401_GMR_SE | uPD98401_GMR_RE,GMR);
done:
	return error;

out_rx:
	kfree(zatm_dev->rx_map);
out_tx:
	kfree(zatm_dev->tx_map);
out:
	while (i-- > 0) {
		pci_free_consistent(pdev, 2*MBX_SIZE(i), 
				    (void *)zatm_dev->mbx_start[i],
				    zatm_dev->mbx_dma[i]);
	}
	free_irq(zatm_dev->irq, dev);
	goto done;
}


static void zatm_close(struct atm_vcc *vcc)
{
        DPRINTK(">zatm_close\n");
        if (!ZATM_VCC(vcc)) return;
	clear_bit(ATM_VF_READY,&vcc->flags);
        close_rx(vcc);
	EVENT("close_tx\n",0,0);
        close_tx(vcc);
        DPRINTK("zatm_close: done waiting\n");
        /* deallocate memory */
        kfree(ZATM_VCC(vcc));
	vcc->dev_data = NULL;
	clear_bit(ATM_VF_ADDR,&vcc->flags);
}


static int zatm_open(struct atm_vcc *vcc)
{
	struct zatm_dev *zatm_dev;
	struct zatm_vcc *zatm_vcc;
	short vpi = vcc->vpi;
	int vci = vcc->vci;
	int error;

	DPRINTK(">zatm_open\n");
	zatm_dev = ZATM_DEV(vcc->dev);
	if (!test_bit(ATM_VF_PARTIAL,&vcc->flags))
		vcc->dev_data = NULL;
	if (vci != ATM_VPI_UNSPEC && vpi != ATM_VCI_UNSPEC)
		set_bit(ATM_VF_ADDR,&vcc->flags);
	if (vcc->qos.aal != ATM_AAL5) return -EINVAL; /* @@@ AAL0 */
	DPRINTK(DEV_LABEL "(itf %d): open %d.%d\n",vcc->dev->number,vcc->vpi,
	    vcc->vci);
	if (!test_bit(ATM_VF_PARTIAL,&vcc->flags)) {
		zatm_vcc = kmalloc(sizeof(struct zatm_vcc),GFP_KERNEL);
		if (!zatm_vcc) {
			clear_bit(ATM_VF_ADDR,&vcc->flags);
			return -ENOMEM;
		}
		vcc->dev_data = zatm_vcc;
		ZATM_VCC(vcc)->tx_chan = 0; /* for zatm_close after open_rx */
		if ((error = open_rx_first(vcc))) {
	                zatm_close(vcc);
	                return error;
	        }
		if ((error = open_tx_first(vcc))) {
			zatm_close(vcc);
			return error;
	        }
	}
	if (vci == ATM_VPI_UNSPEC || vpi == ATM_VCI_UNSPEC) return 0;
	if ((error = open_rx_second(vcc))) {
		zatm_close(vcc);
		return error;
        }
	if ((error = open_tx_second(vcc))) {
		zatm_close(vcc);
		return error;
        }
	set_bit(ATM_VF_READY,&vcc->flags);
        return 0;
}


static int zatm_change_qos(struct atm_vcc *vcc,struct atm_qos *qos,int flags)
{
	printk("Not yet implemented\n");
	return -ENOSYS;
	/* @@@ */
}


static int zatm_ioctl(struct atm_dev *dev,unsigned int cmd,void __user *arg)
{
	struct zatm_dev *zatm_dev;
	unsigned long flags;

	zatm_dev = ZATM_DEV(dev);
	switch (cmd) {
		case ZATM_GETPOOLZ:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			/* fall through */
		case ZATM_GETPOOL:
			{
				struct zatm_pool_info info;
				int pool;

				if (get_user(pool,
				    &((struct zatm_pool_req __user *) arg)->pool_num))
					return -EFAULT;
				if (pool < 0 || pool > ZATM_LAST_POOL)
					return -EINVAL;
				spin_lock_irqsave(&zatm_dev->lock, flags);
				info = zatm_dev->pool_info[pool];
				if (cmd == ZATM_GETPOOLZ) {
					zatm_dev->pool_info[pool].rqa_count = 0;
					zatm_dev->pool_info[pool].rqu_count = 0;
				}
				spin_unlock_irqrestore(&zatm_dev->lock, flags);
				return copy_to_user(
				    &((struct zatm_pool_req __user *) arg)->info,
				    &info,sizeof(info)) ? -EFAULT : 0;
			}
		case ZATM_SETPOOL:
			{
				struct zatm_pool_info info;
				int pool;

				if (!capable(CAP_NET_ADMIN)) return -EPERM;
				if (get_user(pool,
				    &((struct zatm_pool_req __user *) arg)->pool_num))
					return -EFAULT;
				if (pool < 0 || pool > ZATM_LAST_POOL)
					return -EINVAL;
				if (copy_from_user(&info,
				    &((struct zatm_pool_req __user *) arg)->info,
				    sizeof(info))) return -EFAULT;
				if (!info.low_water)
					info.low_water = zatm_dev->
					    pool_info[pool].low_water;
				if (!info.high_water)
					info.high_water = zatm_dev->
					    pool_info[pool].high_water;
				if (!info.next_thres)
					info.next_thres = zatm_dev->
					    pool_info[pool].next_thres;
				if (info.low_water >= info.high_water ||
				    info.low_water < 0)
					return -EINVAL;
				spin_lock_irqsave(&zatm_dev->lock, flags);
				zatm_dev->pool_info[pool].low_water =
				    info.low_water;
				zatm_dev->pool_info[pool].high_water =
				    info.high_water;
				zatm_dev->pool_info[pool].next_thres =
				    info.next_thres;
				spin_unlock_irqrestore(&zatm_dev->lock, flags);
				return 0;
			}
		default:
        		if (!dev->phy->ioctl) return -ENOIOCTLCMD;
		        return dev->phy->ioctl(dev,cmd,arg);
	}
}


static int zatm_getsockopt(struct atm_vcc *vcc,int level,int optname,
    void __user *optval,int optlen)
{
	return -EINVAL;
}


static int zatm_setsockopt(struct atm_vcc *vcc,int level,int optname,
    void __user *optval,int optlen)
{
	return -EINVAL;
}

static int zatm_send(struct atm_vcc *vcc,struct sk_buff *skb)
{
	int error;

	EVENT(">zatm_send 0x%lx\n",(unsigned long) skb,0);
	if (!ZATM_VCC(vcc)->tx_chan || !test_bit(ATM_VF_READY,&vcc->flags)) {
		if (vcc->pop) vcc->pop(vcc,skb);
		else dev_kfree_skb(skb);
		return -EINVAL;
	}
	if (!skb) {
		printk(KERN_CRIT "!skb in zatm_send ?\n");
		if (vcc->pop) vcc->pop(vcc,skb);
		return -EINVAL;
	}
	ATM_SKB(skb)->vcc = vcc;
	error = do_tx(skb);
	if (error != RING_BUSY) return error;
	skb_queue_tail(&ZATM_VCC(vcc)->backlog,skb);
	return 0;
}


static void zatm_phy_put(struct atm_dev *dev,unsigned char value,
    unsigned long addr)
{
	struct zatm_dev *zatm_dev;

	zatm_dev = ZATM_DEV(dev);
	zwait;
	zout(value,CER);
	zout(uPD98401_IND_ACC | uPD98401_IA_B0 |
	    (uPD98401_IA_TGT_PHY << uPD98401_IA_TGT_SHIFT) | addr,CMR);
}


static unsigned char zatm_phy_get(struct atm_dev *dev,unsigned long addr)
{
	struct zatm_dev *zatm_dev;

	zatm_dev = ZATM_DEV(dev);
	zwait;
	zout(uPD98401_IND_ACC | uPD98401_IA_B0 | uPD98401_IA_RW |
	  (uPD98401_IA_TGT_PHY << uPD98401_IA_TGT_SHIFT) | addr,CMR);
	zwait;
	return zin(CER) & 0xff;
}


static const struct atmdev_ops ops = {
	.open		= zatm_open,
	.close		= zatm_close,
	.ioctl		= zatm_ioctl,
	.getsockopt	= zatm_getsockopt,
	.setsockopt	= zatm_setsockopt,
	.send		= zatm_send,
	.phy_put	= zatm_phy_put,
	.phy_get	= zatm_phy_get,
	.change_qos	= zatm_change_qos,
};

static int __devinit zatm_init_one(struct pci_dev *pci_dev,
				   const struct pci_device_id *ent)
{
	struct atm_dev *dev;
	struct zatm_dev *zatm_dev;
	int ret = -ENOMEM;

	zatm_dev = kmalloc(sizeof(*zatm_dev), GFP_KERNEL);
	if (!zatm_dev) {
		printk(KERN_EMERG "%s: memory shortage\n", DEV_LABEL);
		goto out;
	}

	dev = atm_dev_register(DEV_LABEL, &ops, -1, NULL);
	if (!dev)
		goto out_free;

	ret = pci_enable_device(pci_dev);
	if (ret < 0)
		goto out_deregister;

	ret = pci_request_regions(pci_dev, DEV_LABEL);
	if (ret < 0)
		goto out_disable;

	zatm_dev->pci_dev = pci_dev;
	dev->dev_data = zatm_dev;
	zatm_dev->copper = (int)ent->driver_data;
	if ((ret = zatm_init(dev)) || (ret = zatm_start(dev)))
		goto out_release;

	pci_set_drvdata(pci_dev, dev);
	zatm_dev->more = zatm_boards;
	zatm_boards = dev;
	ret = 0;
out:
	return ret;

out_release:
	pci_release_regions(pci_dev);
out_disable:
	pci_disable_device(pci_dev);
out_deregister:
	atm_dev_deregister(dev);
out_free:
	kfree(zatm_dev);
	goto out;
}


MODULE_LICENSE("GPL");

static struct pci_device_id zatm_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_ZEITNET, PCI_DEVICE_ID_ZEITNET_1221,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, ZATM_COPPER },
	{ PCI_VENDOR_ID_ZEITNET, PCI_DEVICE_ID_ZEITNET_1225,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, zatm_pci_tbl);

static struct pci_driver zatm_driver = {
	.name =		DEV_LABEL,
	.id_table =	zatm_pci_tbl,
	.probe =	zatm_init_one,
};

static int __init zatm_init_module(void)
{
	return pci_register_driver(&zatm_driver);
}

module_init(zatm_init_module);
/* module_exit not defined so not unloadable */
