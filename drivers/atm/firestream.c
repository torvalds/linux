
/* drivers/atm/firestream.c - FireStream 155 (MB86697) and
 *                            FireStream  50 (MB86695) device driver 
 */
 
/* Written & (C) 2000 by R.E.Wolff@BitWizard.nl 
 * Copied snippets from zatm.c by Werner Almesberger, EPFL LRC/ICA 
 * and ambassador.c Copyright (C) 1995-1999  Madge Networks Ltd 
 */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  The GNU GPL is contained in /usr/doc/copyright/GPL on a Debian
  system and in the file COPYING in the Linux kernel source.
*/


#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/poison.h>
#include <linux/errno.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/sonet.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/ioport.h> /* for request_region */
#include <linux/uio.h>
#include <linux/init.h>
#include <linux/capability.h>
#include <linux/bitops.h>
#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/string.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <linux/wait.h>

#include "firestream.h"

static int loopback = 0;
static int num=0x5a;

/* According to measurements (but they look suspicious to me!) done in
 * '97, 37% of the packets are one cell in size. So it pays to have
 * buffers allocated at that size. A large jump in percentage of
 * packets occurs at packets around 536 bytes in length. So it also
 * pays to have those pre-allocated. Unfortunately, we can't fully
 * take advantage of this as the majority of the packets is likely to
 * be TCP/IP (As where obviously the measurement comes from) There the
 * link would be opened with say a 1500 byte MTU, and we can't handle
 * smaller buffers more efficiently than the larger ones. -- REW
 */

/* Due to the way Linux memory management works, specifying "576" as
 * an allocation size here isn't going to help. They are allocated
 * from 1024-byte regions anyway. With the size of the sk_buffs (quite
 * large), it doesn't pay to allocate the smallest size (64) -- REW */

/* This is all guesswork. Hard numbers to back this up or disprove this, 
 * are appreciated. -- REW */

/* The last entry should be about 64k. However, the "buffer size" is
 * passed to the chip in a 16 bit field. I don't know how "65536"
 * would be interpreted. -- REW */

#define NP FS_NR_FREE_POOLS
static int rx_buf_sizes[NP]  = {128,  256,  512, 1024, 2048, 4096, 16384, 65520};
/* log2:                 7     8     9    10    11    12    14     16 */

#if 0
static int rx_pool_sizes[NP] = {1024, 1024, 512, 256,  128,  64,   32,    32};
#else
/* debug */
static int rx_pool_sizes[NP] = {128,  128,  128, 64,   64,   64,   32,    32};
#endif
/* log2:                 10    10    9    8     7     6     5      5  */
/* sumlog2:              17    18    18   18    18    18    19     21 */
/* mem allocated:        128k  256k  256k 256k  256k  256k  512k   2M */
/* tot mem: almost 4M */

/* NP is shorter, so that it fits on a single line. */
#undef NP


/* Small hardware gotcha:

   The FS50 CAM (VP/VC match registers) always take the lowest channel
   number that matches. This is not a problem.

   However, they also ignore whether the channel is enabled or
   not. This means that if you allocate channel 0 to 1.2 and then
   channel 1 to 0.0, then disabeling channel 0 and writing 0 to the
   match channel for channel 0 will "steal" the traffic from channel
   1, even if you correctly disable channel 0.

   Workaround: 

   - When disabling channels, write an invalid VP/VC value to the
   match register. (We use 0xffffffff, which in the worst case 
   matches VP/VC = <maxVP>/<maxVC>, but I expect it not to match
   anything as some "when not in use, program to 0" bits are now
   programmed to 1...)

   - Don't initialize the match registers to 0, as 0.0 is a valid
   channel.
*/


/* Optimization hints and tips.

   The FireStream chips are very capable of reducing the amount of
   "interrupt-traffic" for the CPU. This driver requests an interrupt on EVERY
   action. You could try to minimize this a bit. 

   Besides that, the userspace->kernel copy and the PCI bus are the
   performance limiting issues for this driver.

   You could queue up a bunch of outgoing packets without telling the
   FireStream. I'm not sure that's going to win you much though. The
   Linux layer won't tell us in advance when it's not going to give us
   any more packets in a while. So this is tricky to implement right without
   introducing extra delays. 
  
   -- REW
 */




/* The strings that define what the RX queue entry is all about. */
/* Fujitsu: Please tell me which ones can have a pointer to a 
   freepool descriptor! */
static char *res_strings[] = {
	"RX OK: streaming not EOP", 
	"RX OK: streaming EOP", 
	"RX OK: Single buffer packet", 
	"RX OK: packet mode", 
	"RX OK: F4 OAM (end to end)", 
	"RX OK: F4 OAM (Segment)", 
	"RX OK: F5 OAM (end to end)", 
	"RX OK: F5 OAM (Segment)", 
	"RX OK: RM cell", 
	"RX OK: TRANSP cell", 
	"RX OK: TRANSPC cell", 
	"Unmatched cell", 
	"reserved 12", 
	"reserved 13", 
	"reserved 14", 
	"Unrecognized cell", 
	"reserved 16", 
	"reassemby abort: AAL5 abort", 
	"packet purged", 
	"packet ageing timeout", 
	"channel ageing timeout", 
	"calculated length error", 
	"programmed length limit error", 
	"aal5 crc32 error", 
	"oam transp or transpc crc10 error", 
	"reserved 25", 
	"reserved 26", 
	"reserved 27", 
	"reserved 28", 
	"reserved 29", 
	"reserved 30", 
	"reassembly abort: no buffers", 
	"receive buffer overflow", 
	"change in GFC", 
	"receive buffer full", 
	"low priority discard - no receive descriptor", 
	"low priority discard - missing end of packet", 
	"reserved 41", 
	"reserved 42", 
	"reserved 43", 
	"reserved 44", 
	"reserved 45", 
	"reserved 46", 
	"reserved 47", 
	"reserved 48", 
	"reserved 49", 
	"reserved 50", 
	"reserved 51", 
	"reserved 52", 
	"reserved 53", 
	"reserved 54", 
	"reserved 55", 
	"reserved 56", 
	"reserved 57", 
	"reserved 58", 
	"reserved 59", 
	"reserved 60", 
	"reserved 61", 
	"reserved 62", 
	"reserved 63", 
};  

static char *irq_bitname[] = {
	"LPCO",
	"DPCO",
	"RBRQ0_W",
	"RBRQ1_W",
	"RBRQ2_W",
	"RBRQ3_W",
	"RBRQ0_NF",
	"RBRQ1_NF",
	"RBRQ2_NF",
	"RBRQ3_NF",
	"BFP_SC",
	"INIT",
	"INIT_ERR",
	"USCEO",
	"UPEC0",
	"VPFCO",
	"CRCCO",
	"HECO",
	"TBRQ_W",
	"TBRQ_NF",
	"CTPQ_E",
	"GFC_C0",
	"PCI_FTL",
	"CSQ_W",
	"CSQ_NF",
	"EXT_INT",
	"RXDMA_S"
};


#define PHY_EOF -1
#define PHY_CLEARALL -2

struct reginit_item {
	int reg, val;
};


static struct reginit_item PHY_NTC_INIT[] __devinitdata = {
	{ PHY_CLEARALL, 0x40 }, 
	{ 0x12,  0x0001 },
	{ 0x13,  0x7605 },
	{ 0x1A,  0x0001 },
	{ 0x1B,  0x0005 },
	{ 0x38,  0x0003 },
	{ 0x39,  0x0006 },   /* changed here to make loopback */
	{ 0x01,  0x5262 },
	{ 0x15,  0x0213 },
	{ 0x00,  0x0003 },
	{ PHY_EOF, 0},    /* -1 signals end of list */
};


/* Safetyfeature: If the card interrupts more than this number of times
   in a jiffy (1/100th of a second) then we just disable the interrupt and
   print a message. This prevents the system from hanging. 

   150000 packets per second is close to the limit a PC is going to have
   anyway. We therefore have to disable this for production. -- REW */
#undef IRQ_RATE_LIMIT // 100

/* Interrupts work now. Unlike serial cards, ATM cards don't work all
   that great without interrupts. -- REW */
#undef FS_POLL_FREQ // 100

/* 
   This driver can spew a whole lot of debugging output at you. If you
   need maximum performance, you should disable the DEBUG define. To
   aid in debugging in the field, I'm leaving the compile-time debug
   features enabled, and disable them "runtime". That allows me to
   instruct people with problems to enable debugging without requiring
   them to recompile... -- REW
*/
#define DEBUG

#ifdef DEBUG
#define fs_dprintk(f, str...) if (fs_debug & f) printk (str)
#else
#define fs_dprintk(f, str...) /* nothing */
#endif


static int fs_keystream = 0;

#ifdef DEBUG
/* I didn't forget to set this to zero before shipping. Hit me with a stick 
   if you get this with the debug default not set to zero again. -- REW */
static int fs_debug = 0;
#else
#define fs_debug 0
#endif

#ifdef MODULE
#ifdef DEBUG 
module_param(fs_debug, int, 0644);
#endif
module_param(loopback, int, 0);
module_param(num, int, 0);
module_param(fs_keystream, int, 0);
/* XXX Add rx_buf_sizes, and rx_pool_sizes As per request Amar. -- REW */
#endif


#define FS_DEBUG_FLOW    0x00000001
#define FS_DEBUG_OPEN    0x00000002
#define FS_DEBUG_QUEUE   0x00000004
#define FS_DEBUG_IRQ     0x00000008
#define FS_DEBUG_INIT    0x00000010
#define FS_DEBUG_SEND    0x00000020
#define FS_DEBUG_PHY     0x00000040
#define FS_DEBUG_CLEANUP 0x00000080
#define FS_DEBUG_QOS     0x00000100
#define FS_DEBUG_TXQ     0x00000200
#define FS_DEBUG_ALLOC   0x00000400
#define FS_DEBUG_TXMEM   0x00000800
#define FS_DEBUG_QSIZE   0x00001000


#define func_enter() fs_dprintk(FS_DEBUG_FLOW, "fs: enter %s\n", __func__)
#define func_exit()  fs_dprintk(FS_DEBUG_FLOW, "fs: exit  %s\n", __func__)


static struct fs_dev *fs_boards = NULL;

#ifdef DEBUG

static void my_hd (void *addr, int len)
{
	int j, ch;
	unsigned char *ptr = addr;

	while (len > 0) {
		printk ("%p ", ptr);
		for (j=0;j < ((len < 16)?len:16);j++) {
			printk ("%02x %s", ptr[j], (j==7)?" ":"");
		}
		for (  ;j < 16;j++) {
			printk ("   %s", (j==7)?" ":"");
		}
		for (j=0;j < ((len < 16)?len:16);j++) {
			ch = ptr[j];
			printk ("%c", (ch < 0x20)?'.':((ch > 0x7f)?'.':ch));
		}
		printk ("\n");
		ptr += 16;
		len -= 16;
	}
}
#else /* DEBUG */
static void my_hd (void *addr, int len){}
#endif /* DEBUG */

/********** free an skb (as per ATM device driver documentation) **********/

/* Hmm. If this is ATM specific, why isn't there an ATM routine for this?
 * I copied it over from the ambassador driver. -- REW */

static inline void fs_kfree_skb (struct sk_buff * skb) 
{
	if (ATM_SKB(skb)->vcc->pop)
		ATM_SKB(skb)->vcc->pop (ATM_SKB(skb)->vcc, skb);
	else
		dev_kfree_skb_any (skb);
}




/* It seems the ATM forum recommends this horribly complicated 16bit
 * floating point format. Turns out the Ambassador uses the exact same
 * encoding. I just copied it over. If Mitch agrees, I'll move it over
 * to the atm_misc file or something like that. (and remove it from 
 * here and the ambassador driver) -- REW
 */

/* The good thing about this format is that it is monotonic. So, 
   a conversion routine need not be very complicated. To be able to
   round "nearest" we need to take along a few extra bits. Lets
   put these after 16 bits, so that we can just return the top 16
   bits of the 32bit number as the result:

   int mr (unsigned int rate, int r) 
     {
     int e = 16+9;
     static int round[4]={0, 0, 0xffff, 0x8000};
     if (!rate) return 0;
     while (rate & 0xfc000000) {
       rate >>= 1;
       e++;
     }
     while (! (rate & 0xfe000000)) {
       rate <<= 1;
       e--;
     }

// Now the mantissa is in positions bit 16-25. Excepf for the "hidden 1" that's in bit 26.
     rate &= ~0x02000000;
// Next add in the exponent
     rate |= e << (16+9);
// And perform the rounding:
     return (rate + round[r]) >> 16;
   }

   14 lines-of-code. Compare that with the 120 that the Ambassador
   guys needed. (would be 8 lines shorter if I'd try to really reduce
   the number of lines:

   int mr (unsigned int rate, int r) 
   {
     int e = 16+9;
     static int round[4]={0, 0, 0xffff, 0x8000};
     if (!rate) return 0;
     for (;  rate & 0xfc000000 ;rate >>= 1, e++);
     for (;!(rate & 0xfe000000);rate <<= 1, e--);
     return ((rate & ~0x02000000) | (e << (16+9)) + round[r]) >> 16;
   }

   Exercise for the reader: Remove one more line-of-code, without
   cheating. (Just joining two lines is cheating). (I know it's
   possible, don't think you've beat me if you found it... If you
   manage to lose two lines or more, keep me updated! ;-)

   -- REW */


#define ROUND_UP      1
#define ROUND_DOWN    2
#define ROUND_NEAREST 3
/********** make rate (not quite as much fun as Horizon) **********/

static unsigned int make_rate (unsigned int rate, int r,
			       u16 * bits, unsigned int * actual) 
{
	unsigned char exp = -1; /* hush gcc */
	unsigned int man = -1;  /* hush gcc */
  
	fs_dprintk (FS_DEBUG_QOS, "make_rate %u", rate);
  
	/* rates in cells per second, ITU format (nasty 16-bit floating-point)
	   given 5-bit e and 9-bit m:
	   rate = EITHER (1+m/2^9)*2^e    OR 0
	   bits = EITHER 1<<14 | e<<9 | m OR 0
	   (bit 15 is "reserved", bit 14 "non-zero")
	   smallest rate is 0 (special representation)
	   largest rate is (1+511/512)*2^31 = 4290772992 (< 2^32-1)
	   smallest non-zero rate is (1+0/512)*2^0 = 1 (> 0)
	   simple algorithm:
	   find position of top bit, this gives e
	   remove top bit and shift (rounding if feeling clever) by 9-e
	*/
	/* Ambassador ucode bug: please don't set bit 14! so 0 rate not
	   representable. // This should move into the ambassador driver
	   when properly merged. -- REW */
  
	if (rate > 0xffc00000U) {
		/* larger than largest representable rate */
    
		if (r == ROUND_UP) {
			return -EINVAL;
		} else {
			exp = 31;
			man = 511;
		}
    
	} else if (rate) {
		/* representable rate */
    
		exp = 31;
		man = rate;
    
		/* invariant: rate = man*2^(exp-31) */
		while (!(man & (1<<31))) {
			exp = exp - 1;
			man = man<<1;
		}
    
		/* man has top bit set
		   rate = (2^31+(man-2^31))*2^(exp-31)
		   rate = (1+(man-2^31)/2^31)*2^exp 
		*/
		man = man<<1;
		man &= 0xffffffffU; /* a nop on 32-bit systems */
		/* rate = (1+man/2^32)*2^exp
    
		   exp is in the range 0 to 31, man is in the range 0 to 2^32-1
		   time to lose significance... we want m in the range 0 to 2^9-1
		   rounding presents a minor problem... we first decide which way
		   we are rounding (based on given rounding direction and possibly
		   the bits of the mantissa that are to be discarded).
		*/

		switch (r) {
		case ROUND_DOWN: {
			/* just truncate */
			man = man>>(32-9);
			break;
		}
		case ROUND_UP: {
			/* check all bits that we are discarding */
			if (man & (~0U>>9)) {
				man = (man>>(32-9)) + 1;
				if (man == (1<<9)) {
					/* no need to check for round up outside of range */
					man = 0;
					exp += 1;
				}
			} else {
				man = (man>>(32-9));
			}
			break;
		}
		case ROUND_NEAREST: {
			/* check msb that we are discarding */
			if (man & (1<<(32-9-1))) {
				man = (man>>(32-9)) + 1;
				if (man == (1<<9)) {
					/* no need to check for round up outside of range */
					man = 0;
					exp += 1;
				}
			} else {
				man = (man>>(32-9));
			}
			break;
		}
		}
    
	} else {
		/* zero rate - not representable */
    
		if (r == ROUND_DOWN) {
			return -EINVAL;
		} else {
			exp = 0;
			man = 0;
		}
	}
  
	fs_dprintk (FS_DEBUG_QOS, "rate: man=%u, exp=%hu", man, exp);
  
	if (bits)
		*bits = /* (1<<14) | */ (exp<<9) | man;
  
	if (actual)
		*actual = (exp >= 9)
			? (1 << exp) + (man << (exp-9))
			: (1 << exp) + ((man + (1<<(9-exp-1))) >> (9-exp));
  
	return 0;
}




/* FireStream access routines */
/* For DEEP-DOWN debugging these can be rigged to intercept accesses to
   certain registers or to just log all accesses. */

static inline void write_fs (struct fs_dev *dev, int offset, u32 val)
{
	writel (val, dev->base + offset);
}


static inline u32  read_fs (struct fs_dev *dev, int offset)
{
	return readl (dev->base + offset);
}



static inline struct FS_QENTRY *get_qentry (struct fs_dev *dev, struct queue *q)
{
	return bus_to_virt (read_fs (dev, Q_WP(q->offset)) & Q_ADDR_MASK);
}


static void submit_qentry (struct fs_dev *dev, struct queue *q, struct FS_QENTRY *qe)
{
	u32 wp;
	struct FS_QENTRY *cqe;

	/* XXX Sanity check: the write pointer can be checked to be 
	   still the same as the value passed as qe... -- REW */
	/*  udelay (5); */
	while ((wp = read_fs (dev, Q_WP (q->offset))) & Q_FULL) {
		fs_dprintk (FS_DEBUG_TXQ, "Found queue at %x full. Waiting.\n", 
			    q->offset);
		schedule ();
	}

	wp &= ~0xf;
	cqe = bus_to_virt (wp);
	if (qe != cqe) {
		fs_dprintk (FS_DEBUG_TXQ, "q mismatch! %p %p\n", qe, cqe);
	}

	write_fs (dev, Q_WP(q->offset), Q_INCWRAP);

	{
		static int c;
		if (!(c++ % 100))
			{
				int rp, wp;
				rp =  read_fs (dev, Q_RP(q->offset));
				wp =  read_fs (dev, Q_WP(q->offset));
				fs_dprintk (FS_DEBUG_TXQ, "q at %d: %x-%x: %x entries.\n", 
					    q->offset, rp, wp, wp-rp);
			}
	}
}

#ifdef DEBUG_EXTRA
static struct FS_QENTRY pq[60];
static int qp;

static struct FS_BPENTRY dq[60];
static int qd;
static void *da[60];
#endif 

static void submit_queue (struct fs_dev *dev, struct queue *q, 
			  u32 cmd, u32 p1, u32 p2, u32 p3)
{
	struct FS_QENTRY *qe;

	qe = get_qentry (dev, q);
	qe->cmd = cmd;
	qe->p0 = p1;
	qe->p1 = p2;
	qe->p2 = p3;
	submit_qentry (dev,  q, qe);

#ifdef DEBUG_EXTRA
	pq[qp].cmd = cmd;
	pq[qp].p0 = p1;
	pq[qp].p1 = p2;
	pq[qp].p2 = p3;
	qp++;
	if (qp >= 60) qp = 0;
#endif
}

/* Test the "other" way one day... -- REW */
#if 1
#define submit_command submit_queue
#else

static void submit_command (struct fs_dev *dev, struct queue *q, 
			    u32 cmd, u32 p1, u32 p2, u32 p3)
{
	write_fs (dev, CMDR0, cmd);
	write_fs (dev, CMDR1, p1);
	write_fs (dev, CMDR2, p2);
	write_fs (dev, CMDR3, p3);
}
#endif



static void process_return_queue (struct fs_dev *dev, struct queue *q)
{
	long rq;
	struct FS_QENTRY *qe;
	void *tc;
  
	while (!((rq = read_fs (dev, Q_RP(q->offset))) & Q_EMPTY)) {
		fs_dprintk (FS_DEBUG_QUEUE, "reaping return queue entry at %lx\n", rq); 
		qe = bus_to_virt (rq);
    
		fs_dprintk (FS_DEBUG_QUEUE, "queue entry: %08x %08x %08x %08x. (%d)\n", 
			    qe->cmd, qe->p0, qe->p1, qe->p2, STATUS_CODE (qe));

		switch (STATUS_CODE (qe)) {
		case 5:
			tc = bus_to_virt (qe->p0);
			fs_dprintk (FS_DEBUG_ALLOC, "Free tc: %p\n", tc);
			kfree (tc);
			break;
		}
    
		write_fs (dev, Q_RP(q->offset), Q_INCWRAP);
	}
}


static void process_txdone_queue (struct fs_dev *dev, struct queue *q)
{
	long rq;
	long tmp;
	struct FS_QENTRY *qe;
	struct sk_buff *skb;
	struct FS_BPENTRY *td;

	while (!((rq = read_fs (dev, Q_RP(q->offset))) & Q_EMPTY)) {
		fs_dprintk (FS_DEBUG_QUEUE, "reaping txdone entry at %lx\n", rq); 
		qe = bus_to_virt (rq);
    
		fs_dprintk (FS_DEBUG_QUEUE, "queue entry: %08x %08x %08x %08x: %d\n", 
			    qe->cmd, qe->p0, qe->p1, qe->p2, STATUS_CODE (qe));

		if (STATUS_CODE (qe) != 2)
			fs_dprintk (FS_DEBUG_TXMEM, "queue entry: %08x %08x %08x %08x: %d\n", 
				    qe->cmd, qe->p0, qe->p1, qe->p2, STATUS_CODE (qe));


		switch (STATUS_CODE (qe)) {
		case 0x01: /* This is for AAL0 where we put the chip in streaming mode */
			/* Fall through */
		case 0x02:
			/* Process a real txdone entry. */
			tmp = qe->p0;
			if (tmp & 0x0f)
				printk (KERN_WARNING "td not aligned: %ld\n", tmp);
			tmp &= ~0x0f;
			td = bus_to_virt (tmp);

			fs_dprintk (FS_DEBUG_QUEUE, "Pool entry: %08x %08x %08x %08x %p.\n", 
				    td->flags, td->next, td->bsa, td->aal_bufsize, td->skb );
      
			skb = td->skb;
			if (skb == FS_VCC (ATM_SKB(skb)->vcc)->last_skb) {
				wake_up_interruptible (& FS_VCC (ATM_SKB(skb)->vcc)->close_wait);
				FS_VCC (ATM_SKB(skb)->vcc)->last_skb = NULL;
			}
			td->dev->ntxpckts--;

			{
				static int c=0;
	
				if (!(c++ % 100)) {
					fs_dprintk (FS_DEBUG_QSIZE, "[%d]", td->dev->ntxpckts);
				}
			}

			atomic_inc(&ATM_SKB(skb)->vcc->stats->tx);

			fs_dprintk (FS_DEBUG_TXMEM, "i");
			fs_dprintk (FS_DEBUG_ALLOC, "Free t-skb: %p\n", skb);
			fs_kfree_skb (skb);

			fs_dprintk (FS_DEBUG_ALLOC, "Free trans-d: %p\n", td); 
			memset (td, ATM_POISON_FREE, sizeof(struct FS_BPENTRY));
			kfree (td);
			break;
		default:
			/* Here we get the tx purge inhibit command ... */
			/* Action, I believe, is "don't do anything". -- REW */
			;
		}
    
		write_fs (dev, Q_RP(q->offset), Q_INCWRAP);
	}
}


static void process_incoming (struct fs_dev *dev, struct queue *q)
{
	long rq;
	struct FS_QENTRY *qe;
	struct FS_BPENTRY *pe;    
	struct sk_buff *skb;
	unsigned int channo;
	struct atm_vcc *atm_vcc;

	while (!((rq = read_fs (dev, Q_RP(q->offset))) & Q_EMPTY)) {
		fs_dprintk (FS_DEBUG_QUEUE, "reaping incoming queue entry at %lx\n", rq); 
		qe = bus_to_virt (rq);
    
		fs_dprintk (FS_DEBUG_QUEUE, "queue entry: %08x %08x %08x %08x.  ", 
			    qe->cmd, qe->p0, qe->p1, qe->p2);

		fs_dprintk (FS_DEBUG_QUEUE, "-> %x: %s\n", 
			    STATUS_CODE (qe), 
			    res_strings[STATUS_CODE(qe)]);

		pe = bus_to_virt (qe->p0);
		fs_dprintk (FS_DEBUG_QUEUE, "Pool entry: %08x %08x %08x %08x %p %p.\n", 
			    pe->flags, pe->next, pe->bsa, pe->aal_bufsize, 
			    pe->skb, pe->fp);
      
		channo = qe->cmd & 0xffff;

		if (channo < dev->nchannels)
			atm_vcc = dev->atm_vccs[channo];
		else
			atm_vcc = NULL;

		/* Single buffer packet */
		switch (STATUS_CODE (qe)) {
		case 0x1:
			/* Fall through for streaming mode */
		case 0x2:/* Packet received OK.... */
			if (atm_vcc) {
				skb = pe->skb;
				pe->fp->n--;
#if 0
				fs_dprintk (FS_DEBUG_QUEUE, "Got skb: %p\n", skb);
				if (FS_DEBUG_QUEUE & fs_debug) my_hd (bus_to_virt (pe->bsa), 0x20);
#endif
				skb_put (skb, qe->p1 & 0xffff); 
				ATM_SKB(skb)->vcc = atm_vcc;
				atomic_inc(&atm_vcc->stats->rx);
				__net_timestamp(skb);
				fs_dprintk (FS_DEBUG_ALLOC, "Free rec-skb: %p (pushed)\n", skb);
				atm_vcc->push (atm_vcc, skb);
				fs_dprintk (FS_DEBUG_ALLOC, "Free rec-d: %p\n", pe);
				kfree (pe);
			} else {
				printk (KERN_ERR "Got a receive on a non-open channel %d.\n", channo);
			}
			break;
		case 0x17:/* AAL 5 CRC32 error. IFF the length field is nonzero, a buffer
			     has been consumed and needs to be processed. -- REW */
			if (qe->p1 & 0xffff) {
				pe = bus_to_virt (qe->p0);
				pe->fp->n--;
				fs_dprintk (FS_DEBUG_ALLOC, "Free rec-skb: %p\n", pe->skb);
				dev_kfree_skb_any (pe->skb);
				fs_dprintk (FS_DEBUG_ALLOC, "Free rec-d: %p\n", pe);
				kfree (pe);
			}
			if (atm_vcc)
				atomic_inc(&atm_vcc->stats->rx_drop);
			break;
		case 0x1f: /*  Reassembly abort: no buffers. */
			/* Silently increment error counter. */
			if (atm_vcc)
				atomic_inc(&atm_vcc->stats->rx_drop);
			break;
		default: /* Hmm. Haven't written the code to handle the others yet... -- REW */
			printk (KERN_WARNING "Don't know what to do with RX status %x: %s.\n", 
				STATUS_CODE(qe), res_strings[STATUS_CODE (qe)]);
		}
		write_fs (dev, Q_RP(q->offset), Q_INCWRAP);
	}
}



#define DO_DIRECTION(tp) ((tp)->traffic_class != ATM_NONE)

static int fs_open(struct atm_vcc *atm_vcc)
{
	struct fs_dev *dev;
	struct fs_vcc *vcc;
	struct fs_transmit_config *tc;
	struct atm_trafprm * txtp;
	struct atm_trafprm * rxtp;
	/*  struct fs_receive_config *rc;*/
	/*  struct FS_QENTRY *qe; */
	int error;
	int bfp;
	int to;
	unsigned short tmc0;
	short vpi = atm_vcc->vpi;
	int vci = atm_vcc->vci;

	func_enter ();

	dev = FS_DEV(atm_vcc->dev);
	fs_dprintk (FS_DEBUG_OPEN, "fs: open on dev: %p, vcc at %p\n", 
		    dev, atm_vcc);

	if (vci != ATM_VPI_UNSPEC && vpi != ATM_VCI_UNSPEC)
		set_bit(ATM_VF_ADDR, &atm_vcc->flags);

	if ((atm_vcc->qos.aal != ATM_AAL5) &&
	    (atm_vcc->qos.aal != ATM_AAL2))
	  return -EINVAL; /* XXX AAL0 */

	fs_dprintk (FS_DEBUG_OPEN, "fs: (itf %d): open %d.%d\n", 
		    atm_vcc->dev->number, atm_vcc->vpi, atm_vcc->vci);	

	/* XXX handle qos parameters (rate limiting) ? */

	vcc = kmalloc(sizeof(struct fs_vcc), GFP_KERNEL);
	fs_dprintk (FS_DEBUG_ALLOC, "Alloc VCC: %p(%Zd)\n", vcc, sizeof(struct fs_vcc));
	if (!vcc) {
		clear_bit(ATM_VF_ADDR, &atm_vcc->flags);
		return -ENOMEM;
	}
  
	atm_vcc->dev_data = vcc;
	vcc->last_skb = NULL;

	init_waitqueue_head (&vcc->close_wait);

	txtp = &atm_vcc->qos.txtp;
	rxtp = &atm_vcc->qos.rxtp;

	if (!test_bit(ATM_VF_PARTIAL, &atm_vcc->flags)) {
		if (IS_FS50(dev)) {
			/* Increment the channel numer: take a free one next time.  */
			for (to=33;to;to--, dev->channo++) {
				/* We only have 32 channels */
				if (dev->channo >= 32)
					dev->channo = 0;
				/* If we need to do RX, AND the RX is inuse, try the next */
				if (DO_DIRECTION(rxtp) && dev->atm_vccs[dev->channo])
					continue;
				/* If we need to do TX, AND the TX is inuse, try the next */
				if (DO_DIRECTION(txtp) && test_bit (dev->channo, dev->tx_inuse))
					continue;
				/* Ok, both are free! (or not needed) */
				break;
			}
			if (!to) {
				printk ("No more free channels for FS50..\n");
				return -EBUSY;
			}
			vcc->channo = dev->channo;
			dev->channo &= dev->channel_mask;
      
		} else {
			vcc->channo = (vpi << FS155_VCI_BITS) | (vci);
			if (((DO_DIRECTION(rxtp) && dev->atm_vccs[vcc->channo])) ||
			    ( DO_DIRECTION(txtp) && test_bit (vcc->channo, dev->tx_inuse))) {
				printk ("Channel is in use for FS155.\n");
				return -EBUSY;
			}
		}
		fs_dprintk (FS_DEBUG_OPEN, "OK. Allocated channel %x(%d).\n", 
			    vcc->channo, vcc->channo);
	}

	if (DO_DIRECTION (txtp)) {
		tc = kmalloc (sizeof (struct fs_transmit_config), GFP_KERNEL);
		fs_dprintk (FS_DEBUG_ALLOC, "Alloc tc: %p(%Zd)\n",
			    tc, sizeof (struct fs_transmit_config));
		if (!tc) {
			fs_dprintk (FS_DEBUG_OPEN, "fs: can't alloc transmit_config.\n");
			return -ENOMEM;
		}

		/* Allocate the "open" entry from the high priority txq. This makes
		   it most likely that the chip will notice it. It also prevents us
		   from having to wait for completion. On the other hand, we may
		   need to wait for completion anyway, to see if it completed
		   successfully. */

		switch (atm_vcc->qos.aal) {
		case ATM_AAL2:
		case ATM_AAL0:
		  tc->flags = 0
		    | TC_FLAGS_TRANSPARENT_PAYLOAD
		    | TC_FLAGS_PACKET
		    | (1 << 28)
		    | TC_FLAGS_TYPE_UBR /* XXX Change to VBR -- PVDL */
		    | TC_FLAGS_CAL0;
		  break;
		case ATM_AAL5:
		  tc->flags = 0
			| TC_FLAGS_AAL5
			| TC_FLAGS_PACKET  /* ??? */
			| TC_FLAGS_TYPE_CBR
			| TC_FLAGS_CAL0;
		  break;
		default:
			printk ("Unknown aal: %d\n", atm_vcc->qos.aal);
			tc->flags = 0;
		}
		/* Docs are vague about this atm_hdr field. By the way, the FS
		 * chip makes odd errors if lower bits are set.... -- REW */
		tc->atm_hdr =  (vpi << 20) | (vci << 4); 
		{
			int pcr = atm_pcr_goal (txtp);

			fs_dprintk (FS_DEBUG_OPEN, "pcr = %d.\n", pcr);

			/* XXX Hmm. officially we're only allowed to do this if rounding 
			   is round_down -- REW */
			if (IS_FS50(dev)) {
				if (pcr > 51840000/53/8)  pcr = 51840000/53/8;
			} else {
				if (pcr > 155520000/53/8) pcr = 155520000/53/8;
			}
			if (!pcr) {
				/* no rate cap */
				tmc0 = IS_FS50(dev)?0x61BE:0x64c9; /* Just copied over the bits from Fujitsu -- REW */
			} else {
				int r;
				if (pcr < 0) {
					r = ROUND_DOWN;
					pcr = -pcr;
				} else {
					r = ROUND_UP;
				}
				error = make_rate (pcr, r, &tmc0, NULL);
				if (error) {
					kfree(tc);
					return error;
				}
			}
			fs_dprintk (FS_DEBUG_OPEN, "pcr = %d.\n", pcr);
		}
      
		tc->TMC[0] = tmc0 | 0x4000;
		tc->TMC[1] = 0; /* Unused */
		tc->TMC[2] = 0; /* Unused */
		tc->TMC[3] = 0; /* Unused */
    
		tc->spec = 0;    /* UTOPIA address, UDF, HEC: Unused -> 0 */
		tc->rtag[0] = 0; /* What should I do with routing tags??? 
				    -- Not used -- AS -- Thanks -- REW*/
		tc->rtag[1] = 0;
		tc->rtag[2] = 0;

		if (fs_debug & FS_DEBUG_OPEN) {
			fs_dprintk (FS_DEBUG_OPEN, "TX config record:\n");
			my_hd (tc, sizeof (*tc));
		}

		/* We now use the "submit_command" function to submit commands to
		   the firestream. There is a define up near the definition of
		   that routine that switches this routine between immediate write
		   to the immediate comamnd registers and queuing the commands in
		   the HPTXQ for execution. This last technique might be more
		   efficient if we know we're going to submit a whole lot of
		   commands in one go, but this driver is not setup to be able to
		   use such a construct. So it probably doen't matter much right
		   now. -- REW */
    
		/* The command is IMMediate and INQueue. The parameters are out-of-line.. */
		submit_command (dev, &dev->hp_txq, 
				QE_CMD_CONFIG_TX | QE_CMD_IMM_INQ | vcc->channo,
				virt_to_bus (tc), 0, 0);

		submit_command (dev, &dev->hp_txq, 
				QE_CMD_TX_EN | QE_CMD_IMM_INQ | vcc->channo,
				0, 0, 0);
		set_bit (vcc->channo, dev->tx_inuse);
	}

	if (DO_DIRECTION (rxtp)) {
		dev->atm_vccs[vcc->channo] = atm_vcc;

		for (bfp = 0;bfp < FS_NR_FREE_POOLS; bfp++)
			if (atm_vcc->qos.rxtp.max_sdu <= dev->rx_fp[bfp].bufsize) break;
		if (bfp >= FS_NR_FREE_POOLS) {
			fs_dprintk (FS_DEBUG_OPEN, "No free pool fits sdu: %d.\n", 
				    atm_vcc->qos.rxtp.max_sdu);
			/* XXX Cleanup? -- Would just calling fs_close work??? -- REW */

			/* XXX clear tx inuse. Close TX part? */
			dev->atm_vccs[vcc->channo] = NULL;
			kfree (vcc);
			return -EINVAL;
		}

		switch (atm_vcc->qos.aal) {
		case ATM_AAL0:
		case ATM_AAL2:
			submit_command (dev, &dev->hp_txq,
					QE_CMD_CONFIG_RX | QE_CMD_IMM_INQ | vcc->channo,
					RC_FLAGS_TRANSP |
					RC_FLAGS_BFPS_BFP * bfp |
					RC_FLAGS_RXBM_PSB, 0, 0);
			break;
		case ATM_AAL5:
			submit_command (dev, &dev->hp_txq,
					QE_CMD_CONFIG_RX | QE_CMD_IMM_INQ | vcc->channo,
					RC_FLAGS_AAL5 |
					RC_FLAGS_BFPS_BFP * bfp |
					RC_FLAGS_RXBM_PSB, 0, 0);
			break;
		};
		if (IS_FS50 (dev)) {
			submit_command (dev, &dev->hp_txq, 
					QE_CMD_REG_WR | QE_CMD_IMM_INQ,
					0x80 + vcc->channo,
					(vpi << 16) | vci, 0 ); /* XXX -- Use defines. */
		}
		submit_command (dev, &dev->hp_txq, 
				QE_CMD_RX_EN | QE_CMD_IMM_INQ | vcc->channo,
				0, 0, 0);
	}
    
	/* Indicate we're done! */
	set_bit(ATM_VF_READY, &atm_vcc->flags);

	func_exit ();
	return 0;
}


static void fs_close(struct atm_vcc *atm_vcc)
{
	struct fs_dev *dev = FS_DEV (atm_vcc->dev);
	struct fs_vcc *vcc = FS_VCC (atm_vcc);
	struct atm_trafprm * txtp;
	struct atm_trafprm * rxtp;

	func_enter ();

	clear_bit(ATM_VF_READY, &atm_vcc->flags);

	fs_dprintk (FS_DEBUG_QSIZE, "--==**[%d]**==--", dev->ntxpckts);
	if (vcc->last_skb) {
		fs_dprintk (FS_DEBUG_QUEUE, "Waiting for skb %p to be sent.\n", 
			    vcc->last_skb);
		/* We're going to wait for the last packet to get sent on this VC. It would
		   be impolite not to send them don't you think? 
		   XXX
		   We don't know which packets didn't get sent. So if we get interrupted in 
		   this sleep_on, we'll lose any reference to these packets. Memory leak!
		   On the other hand, it's awfully convenient that we can abort a "close" that
		   is taking too long. Maybe just use non-interruptible sleep on? -- REW */
		interruptible_sleep_on (& vcc->close_wait);
	}

	txtp = &atm_vcc->qos.txtp;
	rxtp = &atm_vcc->qos.rxtp;
  

	/* See App note XXX (Unpublished as of now) for the reason for the 
	   removal of the "CMD_IMM_INQ" part of the TX_PURGE_INH... -- REW */

	if (DO_DIRECTION (txtp)) {
		submit_command (dev,  &dev->hp_txq,
				QE_CMD_TX_PURGE_INH | /*QE_CMD_IMM_INQ|*/ vcc->channo, 0,0,0);
		clear_bit (vcc->channo, dev->tx_inuse);
	}

	if (DO_DIRECTION (rxtp)) {
		submit_command (dev,  &dev->hp_txq,
				QE_CMD_RX_PURGE_INH | QE_CMD_IMM_INQ | vcc->channo, 0,0,0);
		dev->atm_vccs [vcc->channo] = NULL;
  
		/* This means that this is configured as a receive channel */
		if (IS_FS50 (dev)) {
			/* Disable the receive filter. Is 0/0 indeed an invalid receive
			   channel? -- REW.  Yes it is. -- Hang. Ok. I'll use -1
			   (0xfff...) -- REW */
			submit_command (dev, &dev->hp_txq, 
					QE_CMD_REG_WR | QE_CMD_IMM_INQ,
					0x80 + vcc->channo, -1, 0 ); 
		}
	}

	fs_dprintk (FS_DEBUG_ALLOC, "Free vcc: %p\n", vcc);
	kfree (vcc);

	func_exit ();
}


static int fs_send (struct atm_vcc *atm_vcc, struct sk_buff *skb)
{
	struct fs_dev *dev = FS_DEV (atm_vcc->dev);
	struct fs_vcc *vcc = FS_VCC (atm_vcc);
	struct FS_BPENTRY *td;

	func_enter ();

	fs_dprintk (FS_DEBUG_TXMEM, "I");
	fs_dprintk (FS_DEBUG_SEND, "Send: atm_vcc %p skb %p vcc %p dev %p\n", 
		    atm_vcc, skb, vcc, dev);

	fs_dprintk (FS_DEBUG_ALLOC, "Alloc t-skb: %p (atm_send)\n", skb);

	ATM_SKB(skb)->vcc = atm_vcc;

	vcc->last_skb = skb;

	td = kmalloc (sizeof (struct FS_BPENTRY), GFP_ATOMIC);
	fs_dprintk (FS_DEBUG_ALLOC, "Alloc transd: %p(%Zd)\n", td, sizeof (struct FS_BPENTRY));
	if (!td) {
		/* Oops out of mem */
		return -ENOMEM;
	}

	fs_dprintk (FS_DEBUG_SEND, "first word in buffer: %x\n", 
		    *(int *) skb->data);

	td->flags =  TD_EPI | TD_DATA | skb->len;
	td->next = 0;
	td->bsa  = virt_to_bus (skb->data);
	td->skb = skb;
	td->dev = dev;
	dev->ntxpckts++;

#ifdef DEBUG_EXTRA
	da[qd] = td;
	dq[qd].flags = td->flags;
	dq[qd].next  = td->next;
	dq[qd].bsa   = td->bsa;
	dq[qd].skb   = td->skb;
	dq[qd].dev   = td->dev;
	qd++;
	if (qd >= 60) qd = 0;
#endif

	submit_queue (dev, &dev->hp_txq, 
		      QE_TRANSMIT_DE | vcc->channo,
		      virt_to_bus (td), 0, 
		      virt_to_bus (td));

	fs_dprintk (FS_DEBUG_QUEUE, "in send: txq %d txrq %d\n", 
		    read_fs (dev, Q_EA (dev->hp_txq.offset)) -
		    read_fs (dev, Q_SA (dev->hp_txq.offset)),
		    read_fs (dev, Q_EA (dev->tx_relq.offset)) -
		    read_fs (dev, Q_SA (dev->tx_relq.offset)));

	func_exit ();
	return 0;
}


/* Some function placeholders for functions we don't yet support. */

#if 0
static int fs_ioctl(struct atm_dev *dev,unsigned int cmd,void __user *arg)
{
	func_enter ();
	func_exit ();
	return -ENOIOCTLCMD;
}


static int fs_getsockopt(struct atm_vcc *vcc,int level,int optname,
			 void __user *optval,int optlen)
{
	func_enter ();
	func_exit ();
	return 0;
}


static int fs_setsockopt(struct atm_vcc *vcc,int level,int optname,
			 void __user *optval,int optlen)
{
	func_enter ();
	func_exit ();
	return 0;
}


static void fs_phy_put(struct atm_dev *dev,unsigned char value,
		       unsigned long addr)
{
	func_enter ();
	func_exit ();
}


static unsigned char fs_phy_get(struct atm_dev *dev,unsigned long addr)
{
	func_enter ();
	func_exit ();
	return 0;
}


static int fs_change_qos(struct atm_vcc *vcc,struct atm_qos *qos,int flags)
{
	func_enter ();
	func_exit ();
	return 0;
};

#endif


static const struct atmdev_ops ops = {
	.open =         fs_open,
	.close =        fs_close,
	.send =         fs_send,
	.owner =        THIS_MODULE,
	/* ioctl:          fs_ioctl, */
	/* getsockopt:     fs_getsockopt, */
	/* setsockopt:     fs_setsockopt, */
	/* change_qos:     fs_change_qos, */

	/* For now implement these internally here... */  
	/* phy_put:        fs_phy_put, */
	/* phy_get:        fs_phy_get, */
};


static void __devinit undocumented_pci_fix (struct pci_dev *pdev)
{
	u32 tint;

	/* The Windows driver says: */
	/* Switch off FireStream Retry Limit Threshold 
	 */

	/* The register at 0x28 is documented as "reserved", no further
	   comments. */

	pci_read_config_dword (pdev, 0x28, &tint);
	if (tint != 0x80) {
		tint = 0x80;
		pci_write_config_dword (pdev, 0x28, tint);
	}
}



/**************************************************************************
 *                              PHY routines                              *
 **************************************************************************/

static void __devinit write_phy (struct fs_dev *dev, int regnum, int val)
{
	submit_command (dev,  &dev->hp_txq, QE_CMD_PRP_WR | QE_CMD_IMM_INQ,
			regnum, val, 0);
}

static int __devinit init_phy (struct fs_dev *dev, struct reginit_item *reginit)
{
	int i;

	func_enter ();
	while (reginit->reg != PHY_EOF) {
		if (reginit->reg == PHY_CLEARALL) {
			/* "PHY_CLEARALL means clear all registers. Numregisters is in "val". */
			for (i=0;i<reginit->val;i++) {
				write_phy (dev, i, 0);
			}
		} else {
			write_phy (dev, reginit->reg, reginit->val);
		}
		reginit++;
	}
	func_exit ();
	return 0;
}

static void reset_chip (struct fs_dev *dev)
{
	int i;

	write_fs (dev, SARMODE0, SARMODE0_SRTS0);

	/* Undocumented delay */
	udelay (128);

	/* The "internal registers are documented to all reset to zero, but 
	   comments & code in the Windows driver indicates that the pools are
	   NOT reset. */
	for (i=0;i < FS_NR_FREE_POOLS;i++) {
		write_fs (dev, FP_CNF (RXB_FP(i)), 0);
		write_fs (dev, FP_SA  (RXB_FP(i)), 0);
		write_fs (dev, FP_EA  (RXB_FP(i)), 0);
		write_fs (dev, FP_CNT (RXB_FP(i)), 0);
		write_fs (dev, FP_CTU (RXB_FP(i)), 0);
	}

	/* The same goes for the match channel registers, although those are
	   NOT documented that way in the Windows driver. -- REW */
	/* The Windows driver DOES write 0 to these registers somewhere in
	   the init sequence. However, a small hardware-feature, will
	   prevent reception of data on VPI/VCI = 0/0 (Unless the channel
	   allocated happens to have no disabled channels that have a lower
	   number. -- REW */

	/* Clear the match channel registers. */
	if (IS_FS50 (dev)) {
		for (i=0;i<FS50_NR_CHANNELS;i++) {
			write_fs (dev, 0x200 + i * 4, -1);
		}
	}
}

static void __devinit *aligned_kmalloc (int size, gfp_t flags, int alignment)
{
	void  *t;

	if (alignment <= 0x10) {
		t = kmalloc (size, flags);
		if ((unsigned long)t & (alignment-1)) {
			printk ("Kmalloc doesn't align things correctly! %p\n", t);
			kfree (t);
			return aligned_kmalloc (size, flags, alignment * 4);
		}
		return t;
	}
	printk (KERN_ERR "Request for > 0x10 alignment not yet implemented (hard!)\n");
	return NULL;
}

static int __devinit init_q (struct fs_dev *dev, 
			  struct queue *txq, int queue, int nentries, int is_rq)
{
	int sz = nentries * sizeof (struct FS_QENTRY);
	struct FS_QENTRY *p;

	func_enter ();

	fs_dprintk (FS_DEBUG_INIT, "Inititing queue at %x: %d entries:\n", 
		    queue, nentries);

	p = aligned_kmalloc (sz, GFP_KERNEL, 0x10);
	fs_dprintk (FS_DEBUG_ALLOC, "Alloc queue: %p(%d)\n", p, sz);

	if (!p) return 0;

	write_fs (dev, Q_SA(queue), virt_to_bus(p));
	write_fs (dev, Q_EA(queue), virt_to_bus(p+nentries-1));
	write_fs (dev, Q_WP(queue), virt_to_bus(p));
	write_fs (dev, Q_RP(queue), virt_to_bus(p));
	if (is_rq) {
		/* Configuration for the receive queue: 0: interrupt immediately,
		   no pre-warning to empty queues: We do our best to keep the
		   queue filled anyway. */
		write_fs (dev, Q_CNF(queue), 0 ); 
	}

	txq->sa = p;
	txq->ea = p;
	txq->offset = queue; 

	func_exit ();
	return 1;
}


static int __devinit init_fp (struct fs_dev *dev, 
			   struct freepool *fp, int queue, int bufsize, int nr_buffers)
{
	func_enter ();

	fs_dprintk (FS_DEBUG_INIT, "Inititing free pool at %x:\n", queue);

	write_fs (dev, FP_CNF(queue), (bufsize * RBFP_RBS) | RBFP_RBSVAL | RBFP_CME);
	write_fs (dev, FP_SA(queue),  0);
	write_fs (dev, FP_EA(queue),  0);
	write_fs (dev, FP_CTU(queue), 0);
	write_fs (dev, FP_CNT(queue), 0);

	fp->offset = queue; 
	fp->bufsize = bufsize;
	fp->nr_buffers = nr_buffers;

	func_exit ();
	return 1;
}


static inline int nr_buffers_in_freepool (struct fs_dev *dev, struct freepool *fp)
{
#if 0
	/* This seems to be unreliable.... */
	return read_fs (dev, FP_CNT (fp->offset));
#else
	return fp->n;
#endif
}


/* Check if this gets going again if a pool ever runs out.  -- Yes, it
   does. I've seen "receive abort: no buffers" and things started
   working again after that...  -- REW */

static void top_off_fp (struct fs_dev *dev, struct freepool *fp,
			gfp_t gfp_flags)
{
	struct FS_BPENTRY *qe, *ne;
	struct sk_buff *skb;
	int n = 0;
	u32 qe_tmp;

	fs_dprintk (FS_DEBUG_QUEUE, "Topping off queue at %x (%d-%d/%d)\n", 
		    fp->offset, read_fs (dev, FP_CNT (fp->offset)), fp->n, 
		    fp->nr_buffers);
	while (nr_buffers_in_freepool(dev, fp) < fp->nr_buffers) {

		skb = alloc_skb (fp->bufsize, gfp_flags);
		fs_dprintk (FS_DEBUG_ALLOC, "Alloc rec-skb: %p(%d)\n", skb, fp->bufsize);
		if (!skb) break;
		ne = kmalloc (sizeof (struct FS_BPENTRY), gfp_flags);
		fs_dprintk (FS_DEBUG_ALLOC, "Alloc rec-d: %p(%Zd)\n", ne, sizeof (struct FS_BPENTRY));
		if (!ne) {
			fs_dprintk (FS_DEBUG_ALLOC, "Free rec-skb: %p\n", skb);
			dev_kfree_skb_any (skb);
			break;
		}

		fs_dprintk (FS_DEBUG_QUEUE, "Adding skb %p desc %p -> %p(%p) ", 
			    skb, ne, skb->data, skb->head);
		n++;
		ne->flags = FP_FLAGS_EPI | fp->bufsize;
		ne->next  = virt_to_bus (NULL);
		ne->bsa   = virt_to_bus (skb->data);
		ne->aal_bufsize = fp->bufsize;
		ne->skb = skb;
		ne->fp = fp;

		/*
		 * FIXME: following code encodes and decodes
		 * machine pointers (could be 64-bit) into a
		 * 32-bit register.
		 */

		qe_tmp = read_fs (dev, FP_EA(fp->offset));
		fs_dprintk (FS_DEBUG_QUEUE, "link at %x\n", qe_tmp);
		if (qe_tmp) {
			qe = bus_to_virt ((long) qe_tmp);
			qe->next = virt_to_bus(ne);
			qe->flags &= ~FP_FLAGS_EPI;
		} else
			write_fs (dev, FP_SA(fp->offset), virt_to_bus(ne));

		write_fs (dev, FP_EA(fp->offset), virt_to_bus (ne));
		fp->n++;   /* XXX Atomic_inc? */
		write_fs (dev, FP_CTU(fp->offset), 1);
	}

	fs_dprintk (FS_DEBUG_QUEUE, "Added %d entries. \n", n);
}

static void __devexit free_queue (struct fs_dev *dev, struct queue *txq)
{
	func_enter ();

	write_fs (dev, Q_SA(txq->offset), 0);
	write_fs (dev, Q_EA(txq->offset), 0);
	write_fs (dev, Q_RP(txq->offset), 0);
	write_fs (dev, Q_WP(txq->offset), 0);
	/* Configuration ? */

	fs_dprintk (FS_DEBUG_ALLOC, "Free queue: %p\n", txq->sa);
	kfree (txq->sa);

	func_exit ();
}

static void __devexit free_freepool (struct fs_dev *dev, struct freepool *fp)
{
	func_enter ();

	write_fs (dev, FP_CNF(fp->offset), 0);
	write_fs (dev, FP_SA (fp->offset), 0);
	write_fs (dev, FP_EA (fp->offset), 0);
	write_fs (dev, FP_CNT(fp->offset), 0);
	write_fs (dev, FP_CTU(fp->offset), 0);

	func_exit ();
}



static irqreturn_t fs_irq (int irq, void *dev_id) 
{
	int i;
	u32 status;
	struct fs_dev *dev = dev_id;

	status = read_fs (dev, ISR);
	if (!status)
		return IRQ_NONE;

	func_enter ();

#ifdef IRQ_RATE_LIMIT
	/* Aaargh! I'm ashamed. This costs more lines-of-code than the actual 
	   interrupt routine!. (Well, used to when I wrote that comment) -- REW */
	{
		static int lastjif;
		static int nintr=0;
    
		if (lastjif == jiffies) {
			if (++nintr > IRQ_RATE_LIMIT) {
				free_irq (dev->irq, dev_id);
				printk (KERN_ERR "fs: Too many interrupts. Turning off interrupt %d.\n", 
					dev->irq);
			}
		} else {
			lastjif = jiffies;
			nintr = 0;
		}
	}
#endif
	fs_dprintk (FS_DEBUG_QUEUE, "in intr: txq %d txrq %d\n", 
		    read_fs (dev, Q_EA (dev->hp_txq.offset)) -
		    read_fs (dev, Q_SA (dev->hp_txq.offset)),
		    read_fs (dev, Q_EA (dev->tx_relq.offset)) -
		    read_fs (dev, Q_SA (dev->tx_relq.offset)));

	/* print the bits in the ISR register. */
	if (fs_debug & FS_DEBUG_IRQ) {
		/* The FS_DEBUG things are unnecessary here. But this way it is
		   clear for grep that these are debug prints. */
		fs_dprintk (FS_DEBUG_IRQ,  "IRQ status:");
		for (i=0;i<27;i++) 
			if (status & (1 << i)) 
				fs_dprintk (FS_DEBUG_IRQ, " %s", irq_bitname[i]);
		fs_dprintk (FS_DEBUG_IRQ, "\n");
	}
  
	if (status & ISR_RBRQ0_W) {
		fs_dprintk (FS_DEBUG_IRQ, "Iiiin-coming (0)!!!!\n");
		process_incoming (dev, &dev->rx_rq[0]);
		/* items mentioned on RBRQ0 are from FP 0 or 1. */
		top_off_fp (dev, &dev->rx_fp[0], GFP_ATOMIC);
		top_off_fp (dev, &dev->rx_fp[1], GFP_ATOMIC);
	}

	if (status & ISR_RBRQ1_W) {
		fs_dprintk (FS_DEBUG_IRQ, "Iiiin-coming (1)!!!!\n");
		process_incoming (dev, &dev->rx_rq[1]);
		top_off_fp (dev, &dev->rx_fp[2], GFP_ATOMIC);
		top_off_fp (dev, &dev->rx_fp[3], GFP_ATOMIC);
	}

	if (status & ISR_RBRQ2_W) {
		fs_dprintk (FS_DEBUG_IRQ, "Iiiin-coming (2)!!!!\n");
		process_incoming (dev, &dev->rx_rq[2]);
		top_off_fp (dev, &dev->rx_fp[4], GFP_ATOMIC);
		top_off_fp (dev, &dev->rx_fp[5], GFP_ATOMIC);
	}

	if (status & ISR_RBRQ3_W) {
		fs_dprintk (FS_DEBUG_IRQ, "Iiiin-coming (3)!!!!\n");
		process_incoming (dev, &dev->rx_rq[3]);
		top_off_fp (dev, &dev->rx_fp[6], GFP_ATOMIC);
		top_off_fp (dev, &dev->rx_fp[7], GFP_ATOMIC);
	}

	if (status & ISR_CSQ_W) {
		fs_dprintk (FS_DEBUG_IRQ, "Command executed ok!\n");
		process_return_queue (dev, &dev->st_q);
	}

	if (status & ISR_TBRQ_W) {
		fs_dprintk (FS_DEBUG_IRQ, "Data tramsitted!\n");
		process_txdone_queue (dev, &dev->tx_relq);
	}

	func_exit ();
	return IRQ_HANDLED;
}


#ifdef FS_POLL_FREQ
static void fs_poll (unsigned long data)
{
	struct fs_dev *dev = (struct fs_dev *) data;
  
	fs_irq (0, dev);
	dev->timer.expires = jiffies + FS_POLL_FREQ;
	add_timer (&dev->timer);
}
#endif

static int __devinit fs_init (struct fs_dev *dev)
{
	struct pci_dev  *pci_dev;
	int isr, to;
	int i;

	func_enter ();
	pci_dev = dev->pci_dev;

	printk (KERN_INFO "found a FireStream %d card, base %16llx, irq%d.\n",
		IS_FS50(dev)?50:155,
		(unsigned long long)pci_resource_start(pci_dev, 0),
		dev->pci_dev->irq);

	if (fs_debug & FS_DEBUG_INIT)
		my_hd ((unsigned char *) dev, sizeof (*dev));

	undocumented_pci_fix (pci_dev);

	dev->hw_base = pci_resource_start(pci_dev, 0);

	dev->base = ioremap(dev->hw_base, 0x1000);

	reset_chip (dev);
  
	write_fs (dev, SARMODE0, 0 
		  | (0 * SARMODE0_SHADEN) /* We don't use shadow registers. */
		  | (1 * SARMODE0_INTMODE_READCLEAR)
		  | (1 * SARMODE0_CWRE)
		  | IS_FS50(dev)?SARMODE0_PRPWT_FS50_5: 
		                 SARMODE0_PRPWT_FS155_3
		  | (1 * SARMODE0_CALSUP_1)
		  | IS_FS50 (dev)?(0
				   | SARMODE0_RXVCS_32
				   | SARMODE0_ABRVCS_32 
				   | SARMODE0_TXVCS_32):
		                  (0
				   | SARMODE0_RXVCS_1k
				   | SARMODE0_ABRVCS_1k 
				   | SARMODE0_TXVCS_1k));

	/* 10ms * 100 is 1 second. That should be enough, as AN3:9 says it takes
	   1ms. */
	to = 100;
	while (--to) {
		isr = read_fs (dev, ISR);

		/* This bit is documented as "RESERVED" */
		if (isr & ISR_INIT_ERR) {
			printk (KERN_ERR "Error initializing the FS... \n");
			goto unmap;
		}
		if (isr & ISR_INIT) {
			fs_dprintk (FS_DEBUG_INIT, "Ha! Initialized OK!\n");
			break;
		}

		/* Try again after 10ms. */
		msleep(10);
	}

	if (!to) {
		printk (KERN_ERR "timeout initializing the FS... \n");
		goto unmap;
	}

	/* XXX fix for fs155 */
	dev->channel_mask = 0x1f; 
	dev->channo = 0;

	/* AN3: 10 */
	write_fs (dev, SARMODE1, 0 
		  | (fs_keystream * SARMODE1_DEFHEC) /* XXX PHY */
		  | ((loopback == 1) * SARMODE1_TSTLP) /* XXX Loopback mode enable... */
		  | (1 * SARMODE1_DCRM)
		  | (1 * SARMODE1_DCOAM)
		  | (0 * SARMODE1_OAMCRC)
		  | (0 * SARMODE1_DUMPE)
		  | (0 * SARMODE1_GPLEN) 
		  | (0 * SARMODE1_GNAM)
		  | (0 * SARMODE1_GVAS)
		  | (0 * SARMODE1_GPAS)
		  | (1 * SARMODE1_GPRI)
		  | (0 * SARMODE1_PMS)
		  | (0 * SARMODE1_GFCR)
		  | (1 * SARMODE1_HECM2)
		  | (1 * SARMODE1_HECM1)
		  | (1 * SARMODE1_HECM0)
		  | (1 << 12) /* That's what hang's driver does. Program to 0 */
		  | (0 * 0xff) /* XXX FS155 */);


	/* Cal prescale etc */

	/* AN3: 11 */
	write_fs (dev, TMCONF, 0x0000000f);
	write_fs (dev, CALPRESCALE, 0x01010101 * num);
	write_fs (dev, 0x80, 0x000F00E4);

	/* AN3: 12 */
	write_fs (dev, CELLOSCONF, 0
		  | (   0 * CELLOSCONF_CEN)
		  | (       CELLOSCONF_SC1)
		  | (0x80 * CELLOSCONF_COBS)
		  | (num  * CELLOSCONF_COPK)  /* Changed from 0xff to 0x5a */
		  | (num  * CELLOSCONF_COST));/* after a hint from Hang. 
					       * performance jumped 50->70... */

	/* Magic value by Hang */
	write_fs (dev, CELLOSCONF_COST, 0x0B809191);

	if (IS_FS50 (dev)) {
		write_fs (dev, RAS0, RAS0_DCD_XHLT);
		dev->atm_dev->ci_range.vpi_bits = 12;
		dev->atm_dev->ci_range.vci_bits = 16;
		dev->nchannels = FS50_NR_CHANNELS;
	} else {
		write_fs (dev, RAS0, RAS0_DCD_XHLT 
			  | (((1 << FS155_VPI_BITS) - 1) * RAS0_VPSEL)
			  | (((1 << FS155_VCI_BITS) - 1) * RAS0_VCSEL));
		/* We can chose the split arbitarily. We might be able to 
		   support more. Whatever. This should do for now. */
		dev->atm_dev->ci_range.vpi_bits = FS155_VPI_BITS;
		dev->atm_dev->ci_range.vci_bits = FS155_VCI_BITS;
    
		/* Address bits we can't use should be compared to 0. */
		write_fs (dev, RAC, 0);

		/* Manual (AN9, page 6) says ASF1=0 means compare Utopia address
		 * too.  I can't find ASF1 anywhere. Anyway, we AND with just the
		 * other bits, then compare with 0, which is exactly what we
		 * want. */
		write_fs (dev, RAM, (1 << (28 - FS155_VPI_BITS - FS155_VCI_BITS)) - 1);
		dev->nchannels = FS155_NR_CHANNELS;
	}
	dev->atm_vccs = kcalloc (dev->nchannels, sizeof (struct atm_vcc *),
				 GFP_KERNEL);
	fs_dprintk (FS_DEBUG_ALLOC, "Alloc atmvccs: %p(%Zd)\n",
		    dev->atm_vccs, dev->nchannels * sizeof (struct atm_vcc *));

	if (!dev->atm_vccs) {
		printk (KERN_WARNING "Couldn't allocate memory for VCC buffers. Woops!\n");
		/* XXX Clean up..... */
		goto unmap;
	}

	dev->tx_inuse = kzalloc (dev->nchannels / 8 /* bits/byte */ , GFP_KERNEL);
	fs_dprintk (FS_DEBUG_ALLOC, "Alloc tx_inuse: %p(%d)\n", 
		    dev->atm_vccs, dev->nchannels / 8);

	if (!dev->tx_inuse) {
		printk (KERN_WARNING "Couldn't allocate memory for tx_inuse bits!\n");
		/* XXX Clean up..... */
		goto unmap;
	}
	/* -- RAS1 : FS155 and 50 differ. Default (0) should be OK for both */
	/* -- RAS2 : FS50 only: Default is OK. */

	/* DMAMODE, default should be OK. -- REW */
	write_fs (dev, DMAMR, DMAMR_TX_MODE_FULL);

	init_q (dev, &dev->hp_txq, TX_PQ(TXQ_HP), TXQ_NENTRIES, 0);
	init_q (dev, &dev->lp_txq, TX_PQ(TXQ_LP), TXQ_NENTRIES, 0);
	init_q (dev, &dev->tx_relq, TXB_RQ, TXQ_NENTRIES, 1);
	init_q (dev, &dev->st_q, ST_Q, TXQ_NENTRIES, 1);

	for (i=0;i < FS_NR_FREE_POOLS;i++) {
		init_fp (dev, &dev->rx_fp[i], RXB_FP(i), 
			 rx_buf_sizes[i], rx_pool_sizes[i]);
		top_off_fp (dev, &dev->rx_fp[i], GFP_KERNEL);
	}


	for (i=0;i < FS_NR_RX_QUEUES;i++)
		init_q (dev, &dev->rx_rq[i], RXB_RQ(i), RXRQ_NENTRIES, 1);

	dev->irq = pci_dev->irq;
	if (request_irq (dev->irq, fs_irq, IRQF_SHARED, "firestream", dev)) {
		printk (KERN_WARNING "couldn't get irq %d for firestream.\n", pci_dev->irq);
		/* XXX undo all previous stuff... */
		goto unmap;
	}
	fs_dprintk (FS_DEBUG_INIT, "Grabbed irq %d for dev at %p.\n", dev->irq, dev);
  
	/* We want to be notified of most things. Just the statistics count
	   overflows are not interesting */
	write_fs (dev, IMR, 0
		  | ISR_RBRQ0_W 
		  | ISR_RBRQ1_W 
		  | ISR_RBRQ2_W 
		  | ISR_RBRQ3_W 
		  | ISR_TBRQ_W
		  | ISR_CSQ_W);

	write_fs (dev, SARMODE0, 0 
		  | (0 * SARMODE0_SHADEN) /* We don't use shadow registers. */
		  | (1 * SARMODE0_GINT)
		  | (1 * SARMODE0_INTMODE_READCLEAR)
		  | (0 * SARMODE0_CWRE)
		  | (IS_FS50(dev)?SARMODE0_PRPWT_FS50_5: 
		                  SARMODE0_PRPWT_FS155_3)
		  | (1 * SARMODE0_CALSUP_1)
		  | (IS_FS50 (dev)?(0
				    | SARMODE0_RXVCS_32
				    | SARMODE0_ABRVCS_32 
				    | SARMODE0_TXVCS_32):
		                   (0
				    | SARMODE0_RXVCS_1k
				    | SARMODE0_ABRVCS_1k 
				    | SARMODE0_TXVCS_1k))
		  | (1 * SARMODE0_RUN));

	init_phy (dev, PHY_NTC_INIT);

	if (loopback == 2) {
		write_phy (dev, 0x39, 0x000e);
	}

#ifdef FS_POLL_FREQ
	init_timer (&dev->timer);
	dev->timer.data = (unsigned long) dev;
	dev->timer.function = fs_poll;
	dev->timer.expires = jiffies + FS_POLL_FREQ;
	add_timer (&dev->timer);
#endif

	dev->atm_dev->dev_data = dev;
  
	func_exit ();
	return 0;
unmap:
	iounmap(dev->base);
	return 1;
}

static int __devinit firestream_init_one (struct pci_dev *pci_dev,
				       const struct pci_device_id *ent) 
{
	struct atm_dev *atm_dev;
	struct fs_dev *fs_dev;
	
	if (pci_enable_device(pci_dev)) 
		goto err_out;

	fs_dev = kzalloc (sizeof (struct fs_dev), GFP_KERNEL);
	fs_dprintk (FS_DEBUG_ALLOC, "Alloc fs-dev: %p(%Zd)\n",
		    fs_dev, sizeof (struct fs_dev));
	if (!fs_dev)
		goto err_out;
	atm_dev = atm_dev_register("fs", &ops, -1, NULL);
	if (!atm_dev)
		goto err_out_free_fs_dev;
  
	fs_dev->pci_dev = pci_dev;
	fs_dev->atm_dev = atm_dev;
	fs_dev->flags = ent->driver_data;

	if (fs_init(fs_dev))
		goto err_out_free_atm_dev;

	fs_dev->next = fs_boards;
	fs_boards = fs_dev;
	return 0;

 err_out_free_atm_dev:
	atm_dev_deregister(atm_dev);
 err_out_free_fs_dev:
 	kfree(fs_dev);
 err_out:
	return -ENODEV;
}

static void __devexit firestream_remove_one (struct pci_dev *pdev)
{
	int i;
	struct fs_dev *dev, *nxtdev;
	struct fs_vcc *vcc;
	struct FS_BPENTRY *fp, *nxt;
  
	func_enter ();

#if 0
	printk ("hptxq:\n");
	for (i=0;i<60;i++) {
		printk ("%d: %08x %08x %08x %08x \n", 
			i, pq[qp].cmd, pq[qp].p0, pq[qp].p1, pq[qp].p2);
		qp++;
		if (qp >= 60) qp = 0;
	}

	printk ("descriptors:\n");
	for (i=0;i<60;i++) {
		printk ("%d: %p: %08x %08x %p %p\n", 
			i, da[qd], dq[qd].flags, dq[qd].bsa, dq[qd].skb, dq[qd].dev);
		qd++;
		if (qd >= 60) qd = 0;
	}
#endif

	for (dev = fs_boards;dev != NULL;dev=nxtdev) {
		fs_dprintk (FS_DEBUG_CLEANUP, "Releasing resources for dev at %p.\n", dev);

		/* XXX Hit all the tx channels too! */

		for (i=0;i < dev->nchannels;i++) {
			if (dev->atm_vccs[i]) {
				vcc = FS_VCC (dev->atm_vccs[i]);
				submit_command (dev,  &dev->hp_txq,
						QE_CMD_TX_PURGE_INH | QE_CMD_IMM_INQ | vcc->channo, 0,0,0);
				submit_command (dev,  &dev->hp_txq,
						QE_CMD_RX_PURGE_INH | QE_CMD_IMM_INQ | vcc->channo, 0,0,0);

			}
		}

		/* XXX Wait a while for the chip to release all buffers. */

		for (i=0;i < FS_NR_FREE_POOLS;i++) {
			for (fp=bus_to_virt (read_fs (dev, FP_SA(dev->rx_fp[i].offset)));
			     !(fp->flags & FP_FLAGS_EPI);fp = nxt) {
				fs_dprintk (FS_DEBUG_ALLOC, "Free rec-skb: %p\n", fp->skb);
				dev_kfree_skb_any (fp->skb);
				nxt = bus_to_virt (fp->next);
				fs_dprintk (FS_DEBUG_ALLOC, "Free rec-d: %p\n", fp);
				kfree (fp);
			}
			fs_dprintk (FS_DEBUG_ALLOC, "Free rec-skb: %p\n", fp->skb);
			dev_kfree_skb_any (fp->skb);
			fs_dprintk (FS_DEBUG_ALLOC, "Free rec-d: %p\n", fp);
			kfree (fp);
		}

		/* Hang the chip in "reset", prevent it clobbering memory that is
		   no longer ours. */
		reset_chip (dev);

		fs_dprintk (FS_DEBUG_CLEANUP, "Freeing irq%d.\n", dev->irq);
		free_irq (dev->irq, dev);
		del_timer (&dev->timer);

		atm_dev_deregister(dev->atm_dev);
		free_queue (dev, &dev->hp_txq);
		free_queue (dev, &dev->lp_txq);
		free_queue (dev, &dev->tx_relq);
		free_queue (dev, &dev->st_q);

		fs_dprintk (FS_DEBUG_ALLOC, "Free atmvccs: %p\n", dev->atm_vccs);
		kfree (dev->atm_vccs);

		for (i=0;i< FS_NR_FREE_POOLS;i++)
			free_freepool (dev, &dev->rx_fp[i]);
    
		for (i=0;i < FS_NR_RX_QUEUES;i++)
			free_queue (dev, &dev->rx_rq[i]);

		iounmap(dev->base);
		fs_dprintk (FS_DEBUG_ALLOC, "Free fs-dev: %p\n", dev);
		nxtdev = dev->next;
		kfree (dev);
	}

	func_exit ();
}

static struct pci_device_id firestream_pci_tbl[] = {
	{ PCI_VENDOR_ID_FUJITSU_ME, PCI_DEVICE_ID_FUJITSU_FS50, 
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, FS_IS50},
	{ PCI_VENDOR_ID_FUJITSU_ME, PCI_DEVICE_ID_FUJITSU_FS155, 
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, FS_IS155},
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, firestream_pci_tbl);

static struct pci_driver firestream_driver = {
	.name		= "firestream",
	.id_table	= firestream_pci_tbl,
	.probe		= firestream_init_one,
	.remove		= __devexit_p(firestream_remove_one),
};

static int __init firestream_init_module (void)
{
	int error;

	func_enter ();
	error = pci_register_driver(&firestream_driver);
	func_exit ();
	return error;
}

static void __exit firestream_cleanup_module(void)
{
	pci_unregister_driver(&firestream_driver);
}

module_init(firestream_init_module);
module_exit(firestream_cleanup_module);

MODULE_LICENSE("GPL");



