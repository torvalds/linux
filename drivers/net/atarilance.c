/* atarilance.c: Ethernet driver for VME Lance cards on the Atari */
/*
	Written 1995/96 by Roman Hodek (Roman.Hodek@informatik.uni-erlangen.de)

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	This drivers was written with the following sources of reference:
	 - The driver for the Riebl Lance card by the TU Vienna.
	 - The modified TUW driver for PAM's VME cards
	 - The PC-Linux driver for Lance cards (but this is for bus master
       cards, not the shared memory ones)
	 - The Amiga Ariadne driver

	v1.0: (in 1.2.13pl4/0.9.13)
	      Initial version
	v1.1: (in 1.2.13pl5)
	      more comments
		  deleted some debugging stuff
		  optimized register access (keep AREG pointing to CSR0)
		  following AMD, CSR0_STRT should be set only after IDON is detected
		  use memcpy() for data transfers, that also employs long word moves
		  better probe procedure for 24-bit systems
          non-VME-RieblCards need extra delays in memcpy
		  must also do write test, since 0xfxe00000 may hit ROM
		  use 8/32 tx/rx buffers, which should give better NFS performance;
		    this is made possible by shifting the last packet buffer after the
		    RieblCard reserved area
    v1.2: (in 1.2.13pl8)
	      again fixed probing for the Falcon; 0xfe01000 hits phys. 0x00010000
		  and thus RAM, in case of no Lance found all memory contents have to
		  be restored!
		  Now possible to compile as module.
	v1.3: 03/30/96 Jes Sorensen, Roman (in 1.3)
	      Several little 1.3 adaptions
		  When the lance is stopped it jumps back into little-endian
		  mode. It is therefore necessary to put it back where it
		  belongs, in big endian mode, in order to make things work.
		  This might be the reason why multicast-mode didn't work
		  before, but I'm not able to test it as I only got an Amiga
		  (we had similar problems with the A2065 driver).

*/

static char version[] = "atarilance.c: v1.3 04/04/96 "
					   "Roman.Hodek@informatik.uni-erlangen.de\n";

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/io.h>

/* Debug level:
 *  0 = silent, print only serious errors
 *  1 = normal, print error messages
 *  2 = debug, print debug infos
 *  3 = debug, print even more debug infos (packet data)
 */

#define	LANCE_DEBUG	1

#ifdef LANCE_DEBUG
static int lance_debug = LANCE_DEBUG;
#else
static int lance_debug = 1;
#endif
module_param(lance_debug, int, 0);
MODULE_PARM_DESC(lance_debug, "atarilance debug level (0-3)");
MODULE_LICENSE("GPL");

/* Print debug messages on probing? */
#undef LANCE_DEBUG_PROBE

#define	DPRINTK(n,a)							\
	do {										\
		if (lance_debug >= n)					\
			printk a;							\
	} while( 0 )

#ifdef LANCE_DEBUG_PROBE
# define PROBE_PRINT(a)	printk a
#else
# define PROBE_PRINT(a)
#endif

/* These define the number of Rx and Tx buffers as log2. (Only powers
 * of two are valid)
 * Much more rx buffers (32) are reserved than tx buffers (8), since receiving
 * is more time critical then sending and packets may have to remain in the
 * board's memory when main memory is low.
 */

#define TX_LOG_RING_SIZE			3
#define RX_LOG_RING_SIZE			5

/* These are the derived values */

#define TX_RING_SIZE			(1 << TX_LOG_RING_SIZE)
#define TX_RING_LEN_BITS		(TX_LOG_RING_SIZE << 5)
#define	TX_RING_MOD_MASK		(TX_RING_SIZE - 1)

#define RX_RING_SIZE			(1 << RX_LOG_RING_SIZE)
#define RX_RING_LEN_BITS		(RX_LOG_RING_SIZE << 5)
#define	RX_RING_MOD_MASK		(RX_RING_SIZE - 1)

#define TX_TIMEOUT	20

/* The LANCE Rx and Tx ring descriptors. */
struct lance_rx_head {
	unsigned short			base;		/* Low word of base addr */
	volatile unsigned char	flag;
	unsigned char			base_hi;	/* High word of base addr (unused) */
	short					buf_length;	/* This length is 2s complement! */
	volatile short			msg_length;	/* This length is "normal". */
};

struct lance_tx_head {
	unsigned short			base;		/* Low word of base addr */
	volatile unsigned char	flag;
	unsigned char			base_hi;	/* High word of base addr (unused) */
	short					length;		/* Length is 2s complement! */
	volatile short			misc;
};

struct ringdesc {
	unsigned short	adr_lo;		/* Low 16 bits of address */
	unsigned char	len;		/* Length bits */
	unsigned char	adr_hi;		/* High 8 bits of address (unused) */
};

/* The LANCE initialization block, described in databook. */
struct lance_init_block {
	unsigned short	mode;		/* Pre-set mode */
	unsigned char	hwaddr[6];	/* Physical ethernet address */
	unsigned		filter[2];	/* Multicast filter (unused). */
	/* Receive and transmit ring base, along with length bits. */
	struct ringdesc	rx_ring;
	struct ringdesc	tx_ring;
};

/* The whole layout of the Lance shared memory */
struct lance_memory {
	struct lance_init_block	init;
	struct lance_tx_head	tx_head[TX_RING_SIZE];
	struct lance_rx_head	rx_head[RX_RING_SIZE];
	char					packet_area[0];	/* packet data follow after the
											 * init block and the ring
											 * descriptors and are located
											 * at runtime */
};

/* RieblCard specifics:
 * The original TOS driver for these cards reserves the area from offset
 * 0xee70 to 0xeebb for storing configuration data. Of interest to us is the
 * Ethernet address there, and the magic for verifying the data's validity.
 * The reserved area isn't touch by packet buffers. Furthermore, offset 0xfffe
 * is reserved for the interrupt vector number.
 */
#define	RIEBL_RSVD_START	0xee70
#define	RIEBL_RSVD_END		0xeec0
#define RIEBL_MAGIC			0x09051990
#define RIEBL_MAGIC_ADDR	((unsigned long *)(((char *)MEM) + 0xee8a))
#define RIEBL_HWADDR_ADDR	((unsigned char *)(((char *)MEM) + 0xee8e))
#define RIEBL_IVEC_ADDR		((unsigned short *)(((char *)MEM) + 0xfffe))

/* This is a default address for the old RieblCards without a battery
 * that have no ethernet address at boot time. 00:00:36:04 is the
 * prefix for Riebl cards, the 00:00 at the end is arbitrary.
 */

static unsigned char OldRieblDefHwaddr[6] = {
	0x00, 0x00, 0x36, 0x04, 0x00, 0x00
};


/* I/O registers of the Lance chip */

struct lance_ioreg {
/* base+0x0 */	volatile unsigned short	data;
/* base+0x2 */	volatile unsigned short	addr;
				unsigned char			_dummy1[3];
/* base+0x7 */	volatile unsigned char	ivec;
				unsigned char			_dummy2[5];
/* base+0xd */	volatile unsigned char	eeprom;
				unsigned char			_dummy3;
/* base+0xf */	volatile unsigned char	mem;
};

/* Types of boards this driver supports */

enum lance_type {
	OLD_RIEBL,		/* old Riebl card without battery */
	NEW_RIEBL,		/* new Riebl card with battery */
	PAM_CARD		/* PAM card with EEPROM */
};

static char *lance_names[] = {
	"Riebl-Card (without battery)",
	"Riebl-Card (with battery)",
	"PAM intern card"
};

/* The driver's private device structure */

struct lance_private {
	enum lance_type		cardtype;
	struct lance_ioreg	*iobase;
	struct lance_memory	*mem;
	int		 	cur_rx, cur_tx;	/* The next free ring entry */
	int			dirty_tx;		/* Ring entries to be freed. */
				/* copy function */
	void			*(*memcpy_f)( void *, const void *, size_t );
/* This must be long for set_bit() */
	long			tx_full;
	spinlock_t		devlock;
};

/* I/O register access macros */

#define	MEM		lp->mem
#define	DREG	IO->data
#define	AREG	IO->addr
#define	REGA(a)	(*( AREG = (a), &DREG ))

/* Definitions for packet buffer access: */
#define PKT_BUF_SZ		1544
/* Get the address of a packet buffer corresponding to a given buffer head */
#define	PKTBUF_ADDR(head)	(((unsigned char *)(MEM)) + (head)->base)

/* Possible memory/IO addresses for probing */

static struct lance_addr {
	unsigned long	memaddr;
	unsigned long	ioaddr;
	int				slow_flag;
} lance_addr_list[] = {
	{ 0xfe010000, 0xfe00fff0, 0 },	/* RieblCard VME in TT */
	{ 0xffc10000, 0xffc0fff0, 0 },	/* RieblCard VME in MegaSTE
									   (highest byte stripped) */
	{ 0xffe00000, 0xffff7000, 1 },	/* RieblCard in ST
									   (highest byte stripped) */
	{ 0xffd00000, 0xffff7000, 1 },	/* RieblCard in ST with hw modif. to
									   avoid conflict with ROM
									   (highest byte stripped) */
	{ 0xffcf0000, 0xffcffff0, 0 },	/* PAMCard VME in TT and MSTE
									   (highest byte stripped) */
	{ 0xfecf0000, 0xfecffff0, 0 },	/* Rhotron's PAMCard VME in TT and MSTE
									   (highest byte stripped) */
};

#define	N_LANCE_ADDR	ARRAY_SIZE(lance_addr_list)


/* Definitions for the Lance */

/* tx_head flags */
#define TMD1_ENP		0x01	/* end of packet */
#define TMD1_STP		0x02	/* start of packet */
#define TMD1_DEF		0x04	/* deferred */
#define TMD1_ONE		0x08	/* one retry needed */
#define TMD1_MORE		0x10	/* more than one retry needed */
#define TMD1_ERR		0x40	/* error summary */
#define TMD1_OWN 		0x80	/* ownership (set: chip owns) */

#define TMD1_OWN_CHIP	TMD1_OWN
#define TMD1_OWN_HOST	0

/* tx_head misc field */
#define TMD3_TDR		0x03FF	/* Time Domain Reflectometry counter */
#define TMD3_RTRY		0x0400	/* failed after 16 retries */
#define TMD3_LCAR		0x0800	/* carrier lost */
#define TMD3_LCOL		0x1000	/* late collision */
#define TMD3_UFLO		0x4000	/* underflow (late memory) */
#define TMD3_BUFF		0x8000	/* buffering error (no ENP) */

/* rx_head flags */
#define RMD1_ENP		0x01	/* end of packet */
#define RMD1_STP		0x02	/* start of packet */
#define RMD1_BUFF		0x04	/* buffer error */
#define RMD1_CRC		0x08	/* CRC error */
#define RMD1_OFLO		0x10	/* overflow */
#define RMD1_FRAM		0x20	/* framing error */
#define RMD1_ERR		0x40	/* error summary */
#define RMD1_OWN 		0x80	/* ownership (set: ship owns) */

#define RMD1_OWN_CHIP	RMD1_OWN
#define RMD1_OWN_HOST	0

/* register names */
#define CSR0	0		/* mode/status */
#define CSR1	1		/* init block addr (low) */
#define CSR2	2		/* init block addr (high) */
#define CSR3	3		/* misc */
#define CSR8	8	  	/* address filter */
#define CSR15	15		/* promiscuous mode */

/* CSR0 */
/* (R=readable, W=writeable, S=set on write, C=clear on write) */
#define CSR0_INIT	0x0001		/* initialize (RS) */
#define CSR0_STRT	0x0002		/* start (RS) */
#define CSR0_STOP	0x0004		/* stop (RS) */
#define CSR0_TDMD	0x0008		/* transmit demand (RS) */
#define CSR0_TXON	0x0010		/* transmitter on (R) */
#define CSR0_RXON	0x0020		/* receiver on (R) */
#define CSR0_INEA	0x0040		/* interrupt enable (RW) */
#define CSR0_INTR	0x0080		/* interrupt active (R) */
#define CSR0_IDON	0x0100		/* initialization done (RC) */
#define CSR0_TINT	0x0200		/* transmitter interrupt (RC) */
#define CSR0_RINT	0x0400		/* receiver interrupt (RC) */
#define CSR0_MERR	0x0800		/* memory error (RC) */
#define CSR0_MISS	0x1000		/* missed frame (RC) */
#define CSR0_CERR	0x2000		/* carrier error (no heartbeat :-) (RC) */
#define CSR0_BABL	0x4000		/* babble: tx-ed too many bits (RC) */
#define CSR0_ERR	0x8000		/* error (RC) */

/* CSR3 */
#define CSR3_BCON	0x0001		/* byte control */
#define CSR3_ACON	0x0002		/* ALE control */
#define CSR3_BSWP	0x0004		/* byte swap (1=big endian) */



/***************************** Prototypes *****************************/

static unsigned long lance_probe1( struct net_device *dev, struct lance_addr
                                   *init_rec );
static int lance_open( struct net_device *dev );
static void lance_init_ring( struct net_device *dev );
static int lance_start_xmit( struct sk_buff *skb, struct net_device *dev );
static irqreturn_t lance_interrupt( int irq, void *dev_id );
static int lance_rx( struct net_device *dev );
static int lance_close( struct net_device *dev );
static void set_multicast_list( struct net_device *dev );
static int lance_set_mac_address( struct net_device *dev, void *addr );
static void lance_tx_timeout (struct net_device *dev);

/************************* End of Prototypes **************************/





static void *slow_memcpy( void *dst, const void *src, size_t len )

{	char *cto = dst;
	const char *cfrom = src;

	while( len-- ) {
		*cto++ = *cfrom++;
		MFPDELAY();
	}
	return( dst );
}


struct net_device * __init atarilance_probe(int unit)
{
	int i;
	static int found;
	struct net_device *dev;
	int err = -ENODEV;

	if (!MACH_IS_ATARI || found)
		/* Assume there's only one board possible... That seems true, since
		 * the Riebl/PAM board's address cannot be changed. */
		return ERR_PTR(-ENODEV);

	dev = alloc_etherdev(sizeof(struct lance_private));
	if (!dev)
		return ERR_PTR(-ENOMEM);
	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
	}

	for( i = 0; i < N_LANCE_ADDR; ++i ) {
		if (lance_probe1( dev, &lance_addr_list[i] )) {
			found = 1;
			err = register_netdev(dev);
			if (!err)
				return dev;
			free_irq(dev->irq, dev);
			break;
		}
	}
	free_netdev(dev);
	return ERR_PTR(err);
}


/* Derived from hwreg_present() in atari/config.c: */

static noinline int __init addr_accessible(volatile void *regp, int wordflag,
					   int writeflag)
{
	int		ret;
	long	flags;
	long	*vbr, save_berr;

	local_irq_save(flags);

	__asm__ __volatile__ ( "movec	%/vbr,%0" : "=r" (vbr) : );
	save_berr = vbr[2];

	__asm__ __volatile__
	(	"movel	%/sp,%/d1\n\t"
		"movel	#Lberr,%2@\n\t"
		"moveq	#0,%0\n\t"
		"tstl   %3\n\t"
		"bne	1f\n\t"
		"moveb	%1@,%/d0\n\t"
		"nop	\n\t"
		"bra	2f\n"
"1:		 movew	%1@,%/d0\n\t"
		"nop	\n"
"2:		 tstl   %4\n\t"
		"beq	2f\n\t"
		"tstl	%3\n\t"
		"bne	1f\n\t"
		"clrb	%1@\n\t"
		"nop	\n\t"
		"moveb	%/d0,%1@\n\t"
		"nop	\n\t"
		"bra	2f\n"
"1:		 clrw	%1@\n\t"
		"nop	\n\t"
		"movew	%/d0,%1@\n\t"
		"nop	\n"
"2:		 moveq	#1,%0\n"
"Lberr:	 movel	%/d1,%/sp"
		: "=&d" (ret)
		: "a" (regp), "a" (&vbr[2]), "rm" (wordflag), "rm" (writeflag)
		: "d0", "d1", "memory"
	);

	vbr[2] = save_berr;
	local_irq_restore(flags);

	return( ret );
}

static const struct net_device_ops lance_netdev_ops = {
	.ndo_open		= lance_open,
	.ndo_stop		= lance_close,
	.ndo_start_xmit		= lance_start_xmit,
	.ndo_set_multicast_list	= set_multicast_list,
	.ndo_set_mac_address	= lance_set_mac_address,
	.ndo_tx_timeout		= lance_tx_timeout,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

static unsigned long __init lance_probe1( struct net_device *dev,
					   struct lance_addr *init_rec )
{
	volatile unsigned short *memaddr =
		(volatile unsigned short *)init_rec->memaddr;
	volatile unsigned short *ioaddr =
		(volatile unsigned short *)init_rec->ioaddr;
	struct lance_private	*lp;
	struct lance_ioreg		*IO;
	int 					i;
	static int 				did_version;
	unsigned short			save1, save2;

	PROBE_PRINT(( "Probing for Lance card at mem %#lx io %#lx\n",
				  (long)memaddr, (long)ioaddr ));

	/* Test whether memory readable and writable */
	PROBE_PRINT(( "lance_probe1: testing memory to be accessible\n" ));
	if (!addr_accessible( memaddr, 1, 1 )) goto probe_fail;

	/* Written values should come back... */
	PROBE_PRINT(( "lance_probe1: testing memory to be writable (1)\n" ));
	save1 = *memaddr;
	*memaddr = 0x0001;
	if (*memaddr != 0x0001) goto probe_fail;
	PROBE_PRINT(( "lance_probe1: testing memory to be writable (2)\n" ));
	*memaddr = 0x0000;
	if (*memaddr != 0x0000) goto probe_fail;
	*memaddr = save1;

	/* First port should be readable and writable */
	PROBE_PRINT(( "lance_probe1: testing ioport to be accessible\n" ));
	if (!addr_accessible( ioaddr, 1, 1 )) goto probe_fail;

	/* and written values should be readable */
	PROBE_PRINT(( "lance_probe1: testing ioport to be writeable\n" ));
	save2 = ioaddr[1];
	ioaddr[1] = 0x0001;
	if (ioaddr[1] != 0x0001) goto probe_fail;

	/* The CSR0_INIT bit should not be readable */
	PROBE_PRINT(( "lance_probe1: testing CSR0 register function (1)\n" ));
	save1 = ioaddr[0];
	ioaddr[1] = CSR0;
	ioaddr[0] = CSR0_INIT | CSR0_STOP;
	if (ioaddr[0] != CSR0_STOP) {
		ioaddr[0] = save1;
		ioaddr[1] = save2;
		goto probe_fail;
	}
	PROBE_PRINT(( "lance_probe1: testing CSR0 register function (2)\n" ));
	ioaddr[0] = CSR0_STOP;
	if (ioaddr[0] != CSR0_STOP) {
		ioaddr[0] = save1;
		ioaddr[1] = save2;
		goto probe_fail;
	}

	/* Now ok... */
	PROBE_PRINT(( "lance_probe1: Lance card detected\n" ));
	goto probe_ok;

  probe_fail:
	return( 0 );

  probe_ok:
	lp = netdev_priv(dev);
	MEM = (struct lance_memory *)memaddr;
	IO = lp->iobase = (struct lance_ioreg *)ioaddr;
	dev->base_addr = (unsigned long)ioaddr; /* informational only */
	lp->memcpy_f = init_rec->slow_flag ? slow_memcpy : memcpy;

	REGA( CSR0 ) = CSR0_STOP;

	/* Now test for type: If the eeprom I/O port is readable, it is a
	 * PAM card */
	if (addr_accessible( &(IO->eeprom), 0, 0 )) {
		/* Switch back to Ram */
		i = IO->mem;
		lp->cardtype = PAM_CARD;
	}
	else if (*RIEBL_MAGIC_ADDR == RIEBL_MAGIC) {
		lp->cardtype = NEW_RIEBL;
	}
	else
		lp->cardtype = OLD_RIEBL;

	if (lp->cardtype == PAM_CARD ||
		memaddr == (unsigned short *)0xffe00000) {
		/* PAMs card and Riebl on ST use level 5 autovector */
		if (request_irq(IRQ_AUTO_5, lance_interrupt, IRQ_TYPE_PRIO,
		            "PAM/Riebl-ST Ethernet", dev)) {
			printk( "Lance: request for irq %d failed\n", IRQ_AUTO_5 );
			return( 0 );
		}
		dev->irq = (unsigned short)IRQ_AUTO_5;
	}
	else {
		/* For VME-RieblCards, request a free VME int;
		 * (This must be unsigned long, since dev->irq is short and the
		 * IRQ_MACHSPEC bit would be cut off...)
		 */
		unsigned long irq = atari_register_vme_int();
		if (!irq) {
			printk( "Lance: request for VME interrupt failed\n" );
			return( 0 );
		}
		if (request_irq(irq, lance_interrupt, IRQ_TYPE_PRIO,
		            "Riebl-VME Ethernet", dev)) {
			printk( "Lance: request for irq %ld failed\n", irq );
			return( 0 );
		}
		dev->irq = irq;
	}

	printk("%s: %s at io %#lx, mem %#lx, irq %d%s, hwaddr ",
		   dev->name, lance_names[lp->cardtype],
		   (unsigned long)ioaddr,
		   (unsigned long)memaddr,
		   dev->irq,
		   init_rec->slow_flag ? " (slow memcpy)" : "" );

	/* Get the ethernet address */
	switch( lp->cardtype ) {
	  case OLD_RIEBL:
		/* No ethernet address! (Set some default address) */
		memcpy( dev->dev_addr, OldRieblDefHwaddr, 6 );
		break;
	  case NEW_RIEBL:
		lp->memcpy_f( dev->dev_addr, RIEBL_HWADDR_ADDR, 6 );
		break;
	  case PAM_CARD:
		i = IO->eeprom;
		for( i = 0; i < 6; ++i )
			dev->dev_addr[i] =
				((((unsigned short *)MEM)[i*2] & 0x0f) << 4) |
				((((unsigned short *)MEM)[i*2+1] & 0x0f));
		i = IO->mem;
		break;
	}
	printk("%pM\n", dev->dev_addr);
	if (lp->cardtype == OLD_RIEBL) {
		printk( "%s: Warning: This is a default ethernet address!\n",
				dev->name );
		printk( "      Use \"ifconfig hw ether ...\" to set the address.\n" );
	}

	spin_lock_init(&lp->devlock);

	MEM->init.mode = 0x0000;		/* Disable Rx and Tx. */
	for( i = 0; i < 6; i++ )
		MEM->init.hwaddr[i] = dev->dev_addr[i^1]; /* <- 16 bit swap! */
	MEM->init.filter[0] = 0x00000000;
	MEM->init.filter[1] = 0x00000000;
	MEM->init.rx_ring.adr_lo = offsetof( struct lance_memory, rx_head );
	MEM->init.rx_ring.adr_hi = 0;
	MEM->init.rx_ring.len    = RX_RING_LEN_BITS;
	MEM->init.tx_ring.adr_lo = offsetof( struct lance_memory, tx_head );
	MEM->init.tx_ring.adr_hi = 0;
	MEM->init.tx_ring.len    = TX_RING_LEN_BITS;

	if (lp->cardtype == PAM_CARD)
		IO->ivec = IRQ_SOURCE_TO_VECTOR(dev->irq);
	else
		*RIEBL_IVEC_ADDR = IRQ_SOURCE_TO_VECTOR(dev->irq);

	if (did_version++ == 0)
		DPRINTK( 1, ( version ));

	dev->netdev_ops = &lance_netdev_ops;

	/* XXX MSch */
	dev->watchdog_timeo = TX_TIMEOUT;

	return( 1 );
}


static int lance_open( struct net_device *dev )
{
	struct lance_private *lp = netdev_priv(dev);
	struct lance_ioreg	 *IO = lp->iobase;
	int i;

	DPRINTK( 2, ( "%s: lance_open()\n", dev->name ));

	lance_init_ring(dev);
	/* Re-initialize the LANCE, and start it when done. */

	REGA( CSR3 ) = CSR3_BSWP | (lp->cardtype == PAM_CARD ? CSR3_ACON : 0);
	REGA( CSR2 ) = 0;
	REGA( CSR1 ) = 0;
	REGA( CSR0 ) = CSR0_INIT;
	/* From now on, AREG is kept to point to CSR0 */

	i = 1000000;
	while (--i > 0)
		if (DREG & CSR0_IDON)
			break;
	if (i <= 0 || (DREG & CSR0_ERR)) {
		DPRINTK( 2, ( "lance_open(): opening %s failed, i=%d, csr0=%04x\n",
					  dev->name, i, DREG ));
		DREG = CSR0_STOP;
		return( -EIO );
	}
	DREG = CSR0_IDON;
	DREG = CSR0_STRT;
	DREG = CSR0_INEA;

	netif_start_queue (dev);

	DPRINTK( 2, ( "%s: LANCE is open, csr0 %04x\n", dev->name, DREG ));

	return( 0 );
}


/* Initialize the LANCE Rx and Tx rings. */

static void lance_init_ring( struct net_device *dev )
{
	struct lance_private *lp = netdev_priv(dev);
	int i;
	unsigned offset;

	lp->tx_full = 0;
	lp->cur_rx = lp->cur_tx = 0;
	lp->dirty_tx = 0;

	offset = offsetof( struct lance_memory, packet_area );

/* If the packet buffer at offset 'o' would conflict with the reserved area
 * of RieblCards, advance it */
#define	CHECK_OFFSET(o)														 \
	do {																	 \
		if (lp->cardtype == OLD_RIEBL || lp->cardtype == NEW_RIEBL) {		 \
			if (((o) < RIEBL_RSVD_START) ? (o)+PKT_BUF_SZ > RIEBL_RSVD_START \
										 : (o) < RIEBL_RSVD_END)			 \
				(o) = RIEBL_RSVD_END;										 \
		}																	 \
	} while(0)

	for( i = 0; i < TX_RING_SIZE; i++ ) {
		CHECK_OFFSET(offset);
		MEM->tx_head[i].base = offset;
		MEM->tx_head[i].flag = TMD1_OWN_HOST;
 		MEM->tx_head[i].base_hi = 0;
		MEM->tx_head[i].length = 0;
		MEM->tx_head[i].misc = 0;
		offset += PKT_BUF_SZ;
	}

	for( i = 0; i < RX_RING_SIZE; i++ ) {
		CHECK_OFFSET(offset);
		MEM->rx_head[i].base = offset;
		MEM->rx_head[i].flag = TMD1_OWN_CHIP;
		MEM->rx_head[i].base_hi = 0;
		MEM->rx_head[i].buf_length = -PKT_BUF_SZ;
		MEM->rx_head[i].msg_length = 0;
		offset += PKT_BUF_SZ;
	}
}


/* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */


static void lance_tx_timeout (struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	struct lance_ioreg	 *IO = lp->iobase;

	AREG = CSR0;
	DPRINTK( 1, ( "%s: transmit timed out, status %04x, resetting.\n",
			  dev->name, DREG ));
	DREG = CSR0_STOP;
	/*
	 * Always set BSWP after a STOP as STOP puts it back into
	 * little endian mode.
	 */
	REGA( CSR3 ) = CSR3_BSWP | (lp->cardtype == PAM_CARD ? CSR3_ACON : 0);
	dev->stats.tx_errors++;
#ifndef final_version
		{	int i;
			DPRINTK( 2, ( "Ring data: dirty_tx %d cur_tx %d%s cur_rx %d\n",
						  lp->dirty_tx, lp->cur_tx,
						  lp->tx_full ? " (full)" : "",
						  lp->cur_rx ));
			for( i = 0 ; i < RX_RING_SIZE; i++ )
				DPRINTK( 2, ( "rx #%d: base=%04x blen=%04x mlen=%04x\n",
							  i, MEM->rx_head[i].base,
							  -MEM->rx_head[i].buf_length,
							  MEM->rx_head[i].msg_length ));
			for( i = 0 ; i < TX_RING_SIZE; i++ )
				DPRINTK( 2, ( "tx #%d: base=%04x len=%04x misc=%04x\n",
							  i, MEM->tx_head[i].base,
							  -MEM->tx_head[i].length,
							  MEM->tx_head[i].misc ));
		}
#endif
	/* XXX MSch: maybe purge/reinit ring here */
	/* lance_restart, essentially */
	lance_init_ring(dev);
	REGA( CSR0 ) = CSR0_INEA | CSR0_INIT | CSR0_STRT;
	dev->trans_start = jiffies;
	netif_wake_queue (dev);
}

/* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */

static int lance_start_xmit( struct sk_buff *skb, struct net_device *dev )
{
	struct lance_private *lp = netdev_priv(dev);
	struct lance_ioreg	 *IO = lp->iobase;
	int entry, len;
	struct lance_tx_head *head;
	unsigned long flags;

	DPRINTK( 2, ( "%s: lance_start_xmit() called, csr0 %4.4x.\n",
				  dev->name, DREG ));


	/* The old LANCE chips doesn't automatically pad buffers to min. size. */
	len = skb->len;
	if (len < ETH_ZLEN)
		len = ETH_ZLEN;
	/* PAM-Card has a bug: Can only send packets with even number of bytes! */
	else if (lp->cardtype == PAM_CARD && (len & 1))
		++len;

	if (len > skb->len) {
		if (skb_padto(skb, len))
			return NETDEV_TX_OK;
	}

	netif_stop_queue (dev);

	/* Fill in a Tx ring entry */
	if (lance_debug >= 3) {
		printk( "%s: TX pkt type 0x%04x from %pM to %pM"
				" data at 0x%08x len %d\n",
				dev->name, ((u_short *)skb->data)[6],
				&skb->data[6], skb->data,
				(int)skb->data, (int)skb->len );
	}

	/* We're not prepared for the int until the last flags are set/reset. And
	 * the int may happen already after setting the OWN_CHIP... */
	spin_lock_irqsave (&lp->devlock, flags);

	/* Mask to ring buffer boundary. */
	entry = lp->cur_tx & TX_RING_MOD_MASK;
	head  = &(MEM->tx_head[entry]);

	/* Caution: the write order is important here, set the "ownership" bits
	 * last.
	 */


	head->length = -len;
	head->misc = 0;
	lp->memcpy_f( PKTBUF_ADDR(head), (void *)skb->data, skb->len );
	head->flag = TMD1_OWN_CHIP | TMD1_ENP | TMD1_STP;
	dev->stats.tx_bytes += skb->len;
	dev_kfree_skb( skb );
	lp->cur_tx++;
	while( lp->cur_tx >= TX_RING_SIZE && lp->dirty_tx >= TX_RING_SIZE ) {
		lp->cur_tx -= TX_RING_SIZE;
		lp->dirty_tx -= TX_RING_SIZE;
	}

	/* Trigger an immediate send poll. */
	DREG = CSR0_INEA | CSR0_TDMD;
	dev->trans_start = jiffies;

	if ((MEM->tx_head[(entry+1) & TX_RING_MOD_MASK].flag & TMD1_OWN) ==
		TMD1_OWN_HOST)
		netif_start_queue (dev);
	else
		lp->tx_full = 1;
	spin_unlock_irqrestore (&lp->devlock, flags);

	return NETDEV_TX_OK;
}

/* The LANCE interrupt handler. */

static irqreturn_t lance_interrupt( int irq, void *dev_id )
{
	struct net_device *dev = dev_id;
	struct lance_private *lp;
	struct lance_ioreg	 *IO;
	int csr0, boguscnt = 10;
	int handled = 0;

	if (dev == NULL) {
		DPRINTK( 1, ( "lance_interrupt(): interrupt for unknown device.\n" ));
		return IRQ_NONE;
	}

	lp = netdev_priv(dev);
	IO = lp->iobase;
	spin_lock (&lp->devlock);

	AREG = CSR0;

	while( ((csr0 = DREG) & (CSR0_ERR | CSR0_TINT | CSR0_RINT)) &&
		   --boguscnt >= 0) {
		handled = 1;
		/* Acknowledge all of the current interrupt sources ASAP. */
		DREG = csr0 & ~(CSR0_INIT | CSR0_STRT | CSR0_STOP |
									CSR0_TDMD | CSR0_INEA);

		DPRINTK( 2, ( "%s: interrupt  csr0=%04x new csr=%04x.\n",
					  dev->name, csr0, DREG ));

		if (csr0 & CSR0_RINT)			/* Rx interrupt */
			lance_rx( dev );

		if (csr0 & CSR0_TINT) {			/* Tx-done interrupt */
			int dirty_tx = lp->dirty_tx;

			while( dirty_tx < lp->cur_tx) {
				int entry = dirty_tx & TX_RING_MOD_MASK;
				int status = MEM->tx_head[entry].flag;

				if (status & TMD1_OWN_CHIP)
					break;			/* It still hasn't been Txed */

				MEM->tx_head[entry].flag = 0;

				if (status & TMD1_ERR) {
					/* There was an major error, log it. */
					int err_status = MEM->tx_head[entry].misc;
					dev->stats.tx_errors++;
					if (err_status & TMD3_RTRY) dev->stats.tx_aborted_errors++;
					if (err_status & TMD3_LCAR) dev->stats.tx_carrier_errors++;
					if (err_status & TMD3_LCOL) dev->stats.tx_window_errors++;
					if (err_status & TMD3_UFLO) {
						/* Ackk!  On FIFO errors the Tx unit is turned off! */
						dev->stats.tx_fifo_errors++;
						/* Remove this verbosity later! */
						DPRINTK( 1, ( "%s: Tx FIFO error! Status %04x\n",
									  dev->name, csr0 ));
						/* Restart the chip. */
						DREG = CSR0_STRT;
					}
				} else {
					if (status & (TMD1_MORE | TMD1_ONE | TMD1_DEF))
						dev->stats.collisions++;
					dev->stats.tx_packets++;
				}

				/* XXX MSch: free skb?? */
				dirty_tx++;
			}

#ifndef final_version
			if (lp->cur_tx - dirty_tx >= TX_RING_SIZE) {
				DPRINTK( 0, ( "out-of-sync dirty pointer,"
							  " %d vs. %d, full=%ld.\n",
							  dirty_tx, lp->cur_tx, lp->tx_full ));
				dirty_tx += TX_RING_SIZE;
			}
#endif

			if (lp->tx_full && (netif_queue_stopped(dev)) &&
				dirty_tx > lp->cur_tx - TX_RING_SIZE + 2) {
				/* The ring is no longer full, clear tbusy. */
				lp->tx_full = 0;
				netif_wake_queue (dev);
			}

			lp->dirty_tx = dirty_tx;
		}

		/* Log misc errors. */
		if (csr0 & CSR0_BABL) dev->stats.tx_errors++; /* Tx babble. */
		if (csr0 & CSR0_MISS) dev->stats.rx_errors++; /* Missed a Rx frame. */
		if (csr0 & CSR0_MERR) {
			DPRINTK( 1, ( "%s: Bus master arbitration failure (?!?), "
						  "status %04x.\n", dev->name, csr0 ));
			/* Restart the chip. */
			DREG = CSR0_STRT;
		}
	}

    /* Clear any other interrupt, and set interrupt enable. */
	DREG = CSR0_BABL | CSR0_CERR | CSR0_MISS | CSR0_MERR |
		   CSR0_IDON | CSR0_INEA;

	DPRINTK( 2, ( "%s: exiting interrupt, csr0=%#04x.\n",
				  dev->name, DREG ));

	spin_unlock (&lp->devlock);
	return IRQ_RETVAL(handled);
}


static int lance_rx( struct net_device *dev )
{
	struct lance_private *lp = netdev_priv(dev);
	int entry = lp->cur_rx & RX_RING_MOD_MASK;
	int i;

	DPRINTK( 2, ( "%s: rx int, flag=%04x\n", dev->name,
				  MEM->rx_head[entry].flag ));

	/* If we own the next entry, it's a new packet. Send it up. */
	while( (MEM->rx_head[entry].flag & RMD1_OWN) == RMD1_OWN_HOST ) {
		struct lance_rx_head *head = &(MEM->rx_head[entry]);
		int status = head->flag;

		if (status != (RMD1_ENP|RMD1_STP)) {		/* There was an error. */
			/* There is a tricky error noted by John Murphy,
			   <murf@perftech.com> to Russ Nelson: Even with full-sized
			   buffers it's possible for a jabber packet to use two
			   buffers, with only the last correctly noting the error. */
			if (status & RMD1_ENP)	/* Only count a general error at the */
				dev->stats.rx_errors++; /* end of a packet.*/
			if (status & RMD1_FRAM) dev->stats.rx_frame_errors++;
			if (status & RMD1_OFLO) dev->stats.rx_over_errors++;
			if (status & RMD1_CRC) dev->stats.rx_crc_errors++;
			if (status & RMD1_BUFF) dev->stats.rx_fifo_errors++;
			head->flag &= (RMD1_ENP|RMD1_STP);
		} else {
			/* Malloc up new buffer, compatible with net-3. */
			short pkt_len = head->msg_length & 0xfff;
			struct sk_buff *skb;

			if (pkt_len < 60) {
				printk( "%s: Runt packet!\n", dev->name );
				dev->stats.rx_errors++;
			}
			else {
				skb = dev_alloc_skb( pkt_len+2 );
				if (skb == NULL) {
					DPRINTK( 1, ( "%s: Memory squeeze, deferring packet.\n",
								  dev->name ));
					for( i = 0; i < RX_RING_SIZE; i++ )
						if (MEM->rx_head[(entry+i) & RX_RING_MOD_MASK].flag &
							RMD1_OWN_CHIP)
							break;

					if (i > RX_RING_SIZE - 2) {
						dev->stats.rx_dropped++;
						head->flag |= RMD1_OWN_CHIP;
						lp->cur_rx++;
					}
					break;
				}

				if (lance_debug >= 3) {
					u_char *data = PKTBUF_ADDR(head);

					printk(KERN_DEBUG "%s: RX pkt type 0x%04x from %pM to %pM "
						   "data %02x %02x %02x %02x %02x %02x %02x %02x "
						   "len %d\n",
						   dev->name, ((u_short *)data)[6],
						   &data[6], data,
						   data[15], data[16], data[17], data[18],
						   data[19], data[20], data[21], data[22],
						   pkt_len);
				}

				skb_reserve( skb, 2 );	/* 16 byte align */
				skb_put( skb, pkt_len );	/* Make room */
				lp->memcpy_f( skb->data, PKTBUF_ADDR(head), pkt_len );
				skb->protocol = eth_type_trans( skb, dev );
				netif_rx( skb );
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += pkt_len;
			}
		}

		head->flag |= RMD1_OWN_CHIP;
		entry = (++lp->cur_rx) & RX_RING_MOD_MASK;
	}
	lp->cur_rx &= RX_RING_MOD_MASK;

	/* From lance.c (Donald Becker): */
	/* We should check that at least two ring entries are free.	 If not,
	   we should free one and mark stats->rx_dropped++. */

	return 0;
}


static int lance_close( struct net_device *dev )
{
	struct lance_private *lp = netdev_priv(dev);
	struct lance_ioreg	 *IO = lp->iobase;

	netif_stop_queue (dev);

	AREG = CSR0;

	DPRINTK( 2, ( "%s: Shutting down ethercard, status was %2.2x.\n",
				  dev->name, DREG ));

	/* We stop the LANCE here -- it occasionally polls
	   memory if we don't. */
	DREG = CSR0_STOP;

	return 0;
}


/* Set or clear the multicast filter for this adaptor.
   num_addrs == -1		Promiscuous mode, receive all packets
   num_addrs == 0		Normal mode, clear multicast list
   num_addrs > 0		Multicast mode, receive normal and MC packets, and do
						best-effort filtering.
 */

static void set_multicast_list( struct net_device *dev )
{
	struct lance_private *lp = netdev_priv(dev);
	struct lance_ioreg	 *IO = lp->iobase;

	if (netif_running(dev))
		/* Only possible if board is already started */
		return;

	/* We take the simple way out and always enable promiscuous mode. */
	DREG = CSR0_STOP; /* Temporarily stop the lance. */

	if (dev->flags & IFF_PROMISC) {
		/* Log any net taps. */
		DPRINTK( 2, ( "%s: Promiscuous mode enabled.\n", dev->name ));
		REGA( CSR15 ) = 0x8000; /* Set promiscuous mode */
	} else {
		short multicast_table[4];
		int num_addrs = netdev_mc_count(dev);
		int i;
		/* We don't use the multicast table, but rely on upper-layer
		 * filtering. */
		memset( multicast_table, (num_addrs == 0) ? 0 : -1,
				sizeof(multicast_table) );
		for( i = 0; i < 4; i++ )
			REGA( CSR8+i ) = multicast_table[i];
		REGA( CSR15 ) = 0; /* Unset promiscuous mode */
	}

	/*
	 * Always set BSWP after a STOP as STOP puts it back into
	 * little endian mode.
	 */
	REGA( CSR3 ) = CSR3_BSWP | (lp->cardtype == PAM_CARD ? CSR3_ACON : 0);

	/* Resume normal operation and reset AREG to CSR0 */
	REGA( CSR0 ) = CSR0_IDON | CSR0_INEA | CSR0_STRT;
}


/* This is needed for old RieblCards and possible for new RieblCards */

static int lance_set_mac_address( struct net_device *dev, void *addr )
{
	struct lance_private *lp = netdev_priv(dev);
	struct sockaddr *saddr = addr;
	int i;

	if (lp->cardtype != OLD_RIEBL && lp->cardtype != NEW_RIEBL)
		return( -EOPNOTSUPP );

	if (netif_running(dev)) {
		/* Only possible while card isn't started */
		DPRINTK( 1, ( "%s: hwaddr can be set only while card isn't open.\n",
					  dev->name ));
		return( -EIO );
	}

	memcpy( dev->dev_addr, saddr->sa_data, dev->addr_len );
	for( i = 0; i < 6; i++ )
		MEM->init.hwaddr[i] = dev->dev_addr[i^1]; /* <- 16 bit swap! */
	lp->memcpy_f( RIEBL_HWADDR_ADDR, dev->dev_addr, 6 );
	/* set also the magic for future sessions */
	*RIEBL_MAGIC_ADDR = RIEBL_MAGIC;

	return( 0 );
}


#ifdef MODULE
static struct net_device *atarilance_dev;

static int __init atarilance_module_init(void)
{
	atarilance_dev = atarilance_probe(-1);
	if (IS_ERR(atarilance_dev))
		return PTR_ERR(atarilance_dev);
	return 0;
}

static void __exit atarilance_module_exit(void)
{
	unregister_netdev(atarilance_dev);
	free_irq(atarilance_dev->irq, atarilance_dev);
	free_netdev(atarilance_dev);
}
module_init(atarilance_module_init);
module_exit(atarilance_module_exit);
#endif /* MODULE */


/*
 * Local variables:
 *  c-indent-level: 4
 *  tab-width: 4
 * End:
 */
