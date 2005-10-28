/* myri_sbus.h: Defines for MyriCOM MyriNET SBUS card driver.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _MYRI_SBUS_H
#define _MYRI_SBUS_H

/* LANAI Registers */
#define LANAI_IPF0	0x00UL		/* Context zero state registers.*/
#define LANAI_CUR0	0x04UL
#define LANAI_PREV0	0x08UL
#define LANAI_DATA0	0x0cUL
#define LANAI_DPF0	0x10UL
#define LANAI_IPF1	0x14UL		/* Context one state registers.	*/
#define LANAI_CUR1	0x18UL
#define LANAI_PREV1	0x1cUL
#define LANAI_DATA1	0x20UL
#define LANAI_DPF1	0x24UL
#define LANAI_ISTAT	0x28UL		/* Interrupt status.		*/
#define LANAI_EIMASK	0x2cUL		/* External IRQ mask.		*/
#define LANAI_ITIMER	0x30UL		/* IRQ timer.			*/
#define LANAI_RTC	0x34UL		/* Real Time Clock		*/
#define LANAI_CSUM	0x38UL		/* Checksum.			*/
#define LANAI_DMAXADDR	0x3cUL		/* SBUS DMA external address.	*/
#define LANAI_DMALADDR	0x40UL		/* SBUS DMA local address.	*/
#define LANAI_DMACTR	0x44UL		/* SBUS DMA counter.		*/
#define LANAI_RXDMAPTR	0x48UL		/* Receive DMA pointer.		*/
#define LANAI_RXDMALIM	0x4cUL		/* Receive DMA limit.		*/
#define LANAI_TXDMAPTR	0x50UL		/* Transmit DMA pointer.	*/
#define LANAI_TXDMALIM	0x54UL		/* Transmit DMA limit.		*/
#define LANAI_TXDMALIMT	0x58UL		/* Transmit DMA limit w/tail.	*/
	/* 0x5cUL, reserved */
#define LANAI_RBYTE	0x60UL		/* Receive byte.		*/
	/* 0x64-->0x6c, reserved */
#define LANAI_RHALF	0x70UL		/* Receive half-word.		*/
	/* 0x72UL, reserved */
#define LANAI_RWORD	0x74UL		/* Receive word.		*/
#define LANAI_SALIGN	0x78UL		/* Send align.			*/
#define LANAI_SBYTE	0x7cUL		/* SingleSend send-byte.	*/
#define LANAI_SHALF	0x80UL		/* SingleSend send-halfword.	*/
#define LANAI_SWORD	0x84UL		/* SingleSend send-word.	*/
#define LANAI_SSENDT	0x88UL		/* SingleSend special.		*/
#define LANAI_DMADIR	0x8cUL		/* DMA direction.		*/
#define LANAI_DMASTAT	0x90UL		/* DMA status.			*/
#define LANAI_TIMEO	0x94UL		/* Timeout register.		*/
#define LANAI_MYRINET	0x98UL		/* XXX MAGIC myricom thing	*/
#define LANAI_HWDEBUG	0x9cUL		/* Hardware debugging reg.	*/
#define LANAI_LEDS	0xa0UL		/* LED control.			*/
#define LANAI_VERS	0xa4UL		/* Version register.		*/
#define LANAI_LINKON	0xa8UL		/* Link activation reg.		*/
	/* 0xac-->0x104, reserved */
#define LANAI_CVAL	0x108UL		/* Clock value register.	*/
#define LANAI_REG_SIZE	0x10cUL

/* Interrupt status bits. */
#define ISTAT_DEBUG	0x80000000
#define ISTAT_HOST	0x40000000
#define ISTAT_LAN7	0x00800000
#define ISTAT_LAN6	0x00400000
#define ISTAT_LAN5	0x00200000
#define ISTAT_LAN4	0x00100000
#define ISTAT_LAN3	0x00080000
#define ISTAT_LAN2	0x00040000
#define ISTAT_LAN1	0x00020000
#define ISTAT_LAN0	0x00010000
#define ISTAT_WRDY	0x00008000
#define ISTAT_HRDY	0x00004000
#define ISTAT_SRDY	0x00002000
#define ISTAT_LINK	0x00001000
#define ISTAT_FRES	0x00000800
#define ISTAT_NRES	0x00000800
#define ISTAT_WAKE	0x00000400
#define ISTAT_OB2	0x00000200
#define ISTAT_OB1	0x00000100
#define ISTAT_TAIL	0x00000080
#define ISTAT_WDOG	0x00000040
#define ISTAT_TIME	0x00000020
#define ISTAT_DMA	0x00000010
#define ISTAT_SEND	0x00000008
#define ISTAT_BUF	0x00000004
#define ISTAT_RECV	0x00000002
#define ISTAT_BRDY	0x00000001

/* MYRI Registers */
#define MYRI_RESETOFF	0x00UL
#define MYRI_RESETON	0x04UL
#define MYRI_IRQOFF	0x08UL
#define MYRI_IRQON	0x0cUL
#define MYRI_WAKEUPOFF	0x10UL
#define MYRI_WAKEUPON	0x14UL
#define MYRI_IRQREAD	0x18UL
	/* 0x1c-->0x3ffc, reserved */
#define MYRI_LOCALMEM	0x4000UL
#define MYRI_REG_SIZE	0x25000UL

/* Shared memory interrupt mask. */
#define SHMEM_IMASK_RX		0x00000002
#define SHMEM_IMASK_TX		0x00000001

/* Just to make things readable. */
#define KERNEL_CHANNEL		0

/* The size of this must be >= 129 bytes. */
struct myri_eeprom {
	unsigned int		cval;
	unsigned short		cpuvers;
	unsigned char		id[6];
	unsigned int		ramsz;
	unsigned char		fvers[32];
	unsigned char		mvers[16];
	unsigned short		dlval;
	unsigned short		brd_type;
	unsigned short		bus_type;
	unsigned short		prod_code;
	unsigned int		serial_num;
	unsigned short		_reserved[24];
	unsigned int		_unused[2];
};

/* EEPROM bus types, only SBUS is valid in this driver. */
#define BUS_TYPE_SBUS		1

/* EEPROM CPU revisions. */
#define CPUVERS_2_3		0x0203
#define CPUVERS_3_0		0x0300
#define CPUVERS_3_1		0x0301
#define CPUVERS_3_2		0x0302
#define CPUVERS_4_0		0x0400
#define CPUVERS_4_1		0x0401
#define CPUVERS_4_2		0x0402
#define CPUVERS_5_0		0x0500

/* MYRI Control Registers */
#define MYRICTRL_CTRL		0x00UL
#define MYRICTRL_IRQLVL		0x02UL
#define MYRICTRL_REG_SIZE	0x04UL

/* Global control register defines. */
#define CONTROL_ROFF		0x8000	/* Reset OFF.		*/
#define CONTROL_RON		0x4000	/* Reset ON.		*/
#define CONTROL_EIRQ		0x2000	/* Enable IRQ's.	*/
#define CONTROL_DIRQ		0x1000	/* Disable IRQ's.	*/
#define CONTROL_WON		0x0800	/* Wake-up ON.		*/

#define MYRI_SCATTER_ENTRIES	8
#define MYRI_GATHER_ENTRIES	16

struct myri_sglist {
	u32 addr;
	u32 len;
};

struct myri_rxd {
	struct myri_sglist myri_scatters[MYRI_SCATTER_ENTRIES];	/* DMA scatter list.*/
	u32 csum;	/* HW computed checksum.    */
	u32 ctx;
	u32 num_sg;	/* Total scatter entries.   */
};

struct myri_txd {
	struct myri_sglist myri_gathers[MYRI_GATHER_ENTRIES]; /* DMA scatter list.  */
	u32 num_sg;	/* Total scatter entries.   */
	u16 addr[4];	/* XXX address              */
	u32 chan;
	u32 len;	/* Total length of packet.  */
	u32 csum_off;	/* Where data to csum is.   */
	u32 csum_field;	/* Where csum goes in pkt.  */
};

#define MYRINET_MTU        8432
#define RX_ALLOC_SIZE      8448
#define MYRI_PAD_LEN       2
#define RX_COPY_THRESHOLD  256

/* These numbers are cast in stone, new firmware is needed if
 * you want to change them.
 */
#define TX_RING_MAXSIZE    16
#define RX_RING_MAXSIZE    16

#define TX_RING_SIZE       16
#define RX_RING_SIZE       16

/* GRRR... */
static __inline__ int NEXT_RX(int num)
{
	/* XXX >=??? */
	if(++num > RX_RING_SIZE)
		num = 0;
	return num;
}

static __inline__ int PREV_RX(int num)
{
	if(--num < 0)
		num = RX_RING_SIZE;
	return num;
}

#define NEXT_TX(num)	(((num) + 1) & (TX_RING_SIZE - 1))
#define PREV_TX(num)	(((num) - 1) & (TX_RING_SIZE - 1))

#define TX_BUFFS_AVAIL(head, tail)		\
	((head) <= (tail) ?			\
	 (head) + (TX_RING_SIZE - 1) - (tail) :	\
	 (head) - (tail) - 1)

struct sendq {
	u32	tail;
	u32	head;
	u32	hdebug;
	u32	mdebug;
	struct myri_txd	myri_txd[TX_RING_MAXSIZE];
};

struct recvq {
	u32	head;
	u32	tail;
	u32	hdebug;
	u32	mdebug;
	struct myri_rxd	myri_rxd[RX_RING_MAXSIZE + 1];
};

#define MYRI_MLIST_SIZE 8

struct mclist {
	u32 maxlen;
	u32 len;
	u32 cache;
	struct pair {
		u8 addr[8];
		u32 val;
	} mc_pairs[MYRI_MLIST_SIZE];
	u8 bcast_addr[8];
};

struct myri_channel {
	u32		state;		/* State of the channel.	*/
	u32		busy;		/* Channel is busy.		*/
	struct sendq	sendq;		/* Device tx queue.		*/
	struct recvq	recvq;		/* Device rx queue.		*/
	struct recvq	recvqa;		/* Device rx queue acked.	*/
	u32		rbytes;		/* Receive bytes.		*/
	u32		sbytes;		/* Send bytes.			*/
	u32		rmsgs;		/* Receive messages.		*/
	u32		smsgs;		/* Send messages.		*/
	struct mclist	mclist;		/* Device multicast list.	*/
};

/* Values for per-channel state. */
#define STATE_WFH	0		/* Waiting for HOST.		*/
#define STATE_WFN	1		/* Waiting for NET.		*/
#define STATE_READY	2		/* Ready.			*/

struct myri_shmem {
	u8	addr[8];		/* Board's address.		*/
	u32	nchan;			/* Number of channels.		*/
	u32	burst;			/* SBUS dma burst enable.	*/
	u32	shakedown;		/* DarkkkkStarrr Crashesss...	*/
	u32	send;			/* Send wanted.			*/
	u32	imask;			/* Interrupt enable mask.	*/
	u32	mlevel;			/* Map level.			*/
	u32	debug[4];		/* Misc. debug areas.		*/
	struct myri_channel channel;	/* Only one channel on a host.	*/
};

struct myri_eth {
	/* These are frequently accessed, keep together
	 * to obtain good cache hit rates.
	 */
	spinlock_t			irq_lock;
	struct myri_shmem __iomem	*shmem;		/* Shared data structures.    */
	void __iomem			*cregs;		/* Control register space.    */
	struct recvq __iomem		*rqack;		/* Where we ack rx's.         */
	struct recvq __iomem		*rq;		/* Where we put buffers.      */
	struct sendq __iomem		*sq;		/* Where we stuff tx's.       */
	struct net_device		*dev;		/* Linux/NET dev struct.      */
	int				tx_old;		/* To speed up tx cleaning.   */
	void __iomem			*lregs;		/* Quick ptr to LANAI regs.   */
	struct sk_buff	       *rx_skbs[RX_RING_SIZE+1];/* RX skb's                   */
	struct sk_buff	       *tx_skbs[TX_RING_SIZE];  /* TX skb's                   */
	struct net_device_stats		enet_stats;	/* Interface stats.           */

	/* These are less frequently accessed. */
	void __iomem			*regs;          /* MyriCOM register space.    */
	void __iomem			*lanai;		/* View 2 of register space.  */
	unsigned int			myri_bursts;	/* SBUS bursts.               */
	struct myri_eeprom		eeprom;		/* Local copy of EEPROM.      */
	unsigned int			reg_size;	/* Size of register space.    */
	unsigned int			shmem_base;	/* Offset to shared ram.      */
	struct sbus_dev			*myri_sdev;	/* Our SBUS device struct.    */
	struct myri_eth			*next_module;	/* Next in adapter chain.     */
};

/* We use this to acquire receive skb's that we can DMA directly into. */
#define ALIGNED_RX_SKB_ADDR(addr) \
        ((((unsigned long)(addr) + (64 - 1)) & ~(64 - 1)) - (unsigned long)(addr))
static inline struct sk_buff *myri_alloc_skb(unsigned int length, gfp_t gfp_flags)
{
	struct sk_buff *skb;

	skb = alloc_skb(length + 64, gfp_flags);
	if(skb) {
		int offset = ALIGNED_RX_SKB_ADDR(skb->data);

		if(offset)
			skb_reserve(skb, offset);
	}
	return skb;
}

#endif /* !(_MYRI_SBUS_H) */
