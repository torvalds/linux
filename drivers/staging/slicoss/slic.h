/**************************************************************************
 *
 * Copyright (c) 2000-2002 Alacritech, Inc.  All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ALACRITECH, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ALACRITECH, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Alacritech, Inc.
 *
 **************************************************************************/

/*
 * FILENAME: slic.h
 *
 * This is the base set of header definitions for the SLICOSS driver.
 */
#ifndef __SLIC_DRIVER_H__
#define __SLIC_DRIVER_H__

/* firmware stuff */
#define OASIS_UCODE_VERS_STRING	"1.2"
#define OASIS_UCODE_VERS_DATE	"2006/03/27 15:10:37"
#define OASIS_UCODE_HOSTIF_ID	3

#define MOJAVE_UCODE_VERS_STRING	"1.2"
#define MOJAVE_UCODE_VERS_DATE		"2006/03/27 15:12:22"
#define MOJAVE_UCODE_HOSTIF_ID		3

#define GB_RCVUCODE_VERS_STRING	"1.2"
#define GB_RCVUCODE_VERS_DATE	"2006/03/27 15:12:15"
static u32 OasisRcvUCodeLen = 512;
static u32 GBRcvUCodeLen = 512;
#define SECTION_SIZE 65536

#define SLIC_RSPQ_PAGES_GB        10
#define SLIC_RSPQ_BUFSINPAGE      (PAGE_SIZE / SLIC_RSPBUF_SIZE)

struct slic_rspqueue {
	u32             offset;
	u32             pageindex;
	u32             num_pages;
	struct slic_rspbuf *rspbuf;
	u32 *vaddr[SLIC_RSPQ_PAGES_GB];
	dma_addr_t          paddr[SLIC_RSPQ_PAGES_GB];
};

#define SLIC_RCVQ_EXPANSION         1
#define SLIC_RCVQ_ENTRIES           (256 * SLIC_RCVQ_EXPANSION)
#define SLIC_RCVQ_MINENTRIES        (SLIC_RCVQ_ENTRIES / 2)
#define SLIC_RCVQ_MAX_PROCESS_ISR   ((SLIC_RCVQ_ENTRIES * 4))
#define SLIC_RCVQ_RCVBUFSIZE        2048
#define SLIC_RCVQ_FILLENTRIES       (16 * SLIC_RCVQ_EXPANSION)
#define SLIC_RCVQ_FILLTHRESH        (SLIC_RCVQ_ENTRIES - SLIC_RCVQ_FILLENTRIES)

struct slic_rcvqueue {
	struct sk_buff    *head;
	struct sk_buff    *tail;
	u32            count;
	u32            size;
	u32            errors;
};

struct slic_rcvbuf_info {
	u32     id;
	u32     starttime;
	u32     stoptime;
	u32     slicworld;
	u32     lasttime;
	u32     lastid;
};
/*
 * SLIC Handle structure.  Used to restrict handle values to
 * 32 bits by using an index rather than an address.
 * Simplifies ucode in 64-bit systems
 */
struct slic_handle_word {
	union {
		struct {
			ushort      index;
			ushort      bottombits; /* to denote num bufs to card */
		}  parts;
		u32         whole;
	}  handle;
};

struct slic_handle {
	struct slic_handle_word  token;  /* token passed between host and card*/
	ushort                      type;
	void *address;    /* actual address of the object*/
	ushort                      offset;
	struct slic_handle       *other_handle;
	struct slic_handle       *next;
};

#define SLIC_HANDLE_FREE        0x0000
#define SLIC_HANDLE_DATA        0x0001
#define SLIC_HANDLE_CMD         0x0002
#define SLIC_HANDLE_CONTEXT     0x0003
#define SLIC_HANDLE_TEAM        0x0004

#define handle_index        handle.parts.index
#define handle_bottom       handle.parts.bottombits
#define handle_token        handle.whole

#define SLIC_HOSTCMD_SIZE    512

struct slic_hostcmd {
	struct slic_host64_cmd  cmd64;
	u32                    type;
	struct sk_buff            *skb;
	u32                    paddrl;
	u32                    paddrh;
	u32                    busy;
	u32                    cmdsize;
	ushort                     numbufs;
	struct slic_handle    *pslic_handle;/* handle associated with command */
	struct slic_hostcmd    *next;
	struct slic_hostcmd    *next_all;
};

#define SLIC_CMDQ_CMDSINPAGE    (PAGE_SIZE / SLIC_HOSTCMD_SIZE)
#define SLIC_CMD_DUMB            3
#define SLIC_CMDQ_INITCMDS       256
#define SLIC_CMDQ_MAXCMDS        256
#define SLIC_CMDQ_MAXOUTSTAND    SLIC_CMDQ_MAXCMDS
#define SLIC_CMDQ_MAXPAGES       (SLIC_CMDQ_MAXCMDS / SLIC_CMDQ_CMDSINPAGE)
#define SLIC_CMDQ_INITPAGES      (SLIC_CMDQ_INITCMDS / SLIC_CMDQ_CMDSINPAGE)

struct slic_cmdqmem {
	int pagecnt;
	u32 *pages[SLIC_CMDQ_MAXPAGES];
	dma_addr_t dma_pages[SLIC_CMDQ_MAXPAGES];
};

struct slic_cmdqueue {
	struct slic_hostcmd *head;
	struct slic_hostcmd *tail;
	int count;
	spinlock_t lock;
};

#define SLIC_MAX_CARDS              32
#define SLIC_MAX_PORTS              4        /* Max # of ports per card   */

struct mcast_address {
	unsigned char address[6];
	struct mcast_address *next;
};

#define CARD_DOWN        0x00000000
#define CARD_UP          0x00000001
#define CARD_FAIL        0x00000002
#define CARD_DIAG        0x00000003
#define CARD_SLEEP       0x00000004

#define ADAPT_DOWN             0x00
#define ADAPT_UP               0x01
#define ADAPT_FAIL             0x02
#define ADAPT_RESET            0x03
#define ADAPT_SLEEP            0x04

#define ADAPT_FLAGS_BOOTTIME            0x0001
#define ADAPT_FLAGS_IS64BIT             0x0002
#define ADAPT_FLAGS_PENDINGLINKDOWN     0x0004
#define ADAPT_FLAGS_FIBERMEDIA          0x0008
#define ADAPT_FLAGS_LOCKS_ALLOCED       0x0010
#define ADAPT_FLAGS_INT_REGISTERED      0x0020
#define ADAPT_FLAGS_LOAD_TIMER_SET      0x0040
#define ADAPT_FLAGS_STATS_TIMER_SET     0x0080
#define ADAPT_FLAGS_RESET_TIMER_SET     0x0100

#define LINK_DOWN              0x00
#define LINK_CONFIG            0x01
#define LINK_UP                0x02

#define LINK_10MB              0x00
#define LINK_100MB             0x01
#define LINK_AUTOSPEED         0x02
#define LINK_1000MB            0x03
#define LINK_10000MB           0x04

#define LINK_HALFD             0x00
#define LINK_FULLD             0x01
#define LINK_AUTOD             0x02

#define MAC_DIRECTED     0x00000001
#define MAC_BCAST        0x00000002
#define MAC_MCAST        0x00000004
#define MAC_PROMISC      0x00000008
#define MAC_LOOPBACK     0x00000010
#define MAC_ALLMCAST     0x00000020

#define SLIC_DUPLEX(x)    ((x == LINK_FULLD) ? "FDX" : "HDX")
#define SLIC_SPEED(x)     ((x == LINK_100MB) ? "100Mb" : ((x == LINK_1000MB) ?\
				"1000Mb" : " 10Mb"))
#define SLIC_LINKSTATE(x) ((x == LINK_DOWN) ? "Down" : "Up  ")
#define SLIC_ADAPTER_STATE(x) ((x == ADAPT_UP) ? "UP" : "Down")
#define SLIC_CARD_STATE(x)    ((x == CARD_UP) ? "UP" : "Down")

struct slic_iface_stats {
	/*
	* Stats
	*/
	u64        xmt_bytes;
	u64        xmt_ucast;
	u64        xmt_mcast;
	u64        xmt_bcast;
	u64        xmt_errors;
	u64        xmt_discards;
	u64        xmit_collisions;
	u64        xmit_excess_xmit_collisions;
	u64        rcv_bytes;
	u64        rcv_ucast;
	u64        rcv_mcast;
	u64        rcv_bcast;
	u64        rcv_errors;
	u64        rcv_discards;
};

struct sliccp_stats {
	u64        xmit_tcp_segs;
	u64        xmit_tcp_bytes;
	u64        rcv_tcp_segs;
	u64        rcv_tcp_bytes;
};

struct slicnet_stats {
	struct sliccp_stats        tcp;
	struct slic_iface_stats      iface;
};

#define SLIC_LOADTIMER_PERIOD     1
#define SLIC_INTAGG_DEFAULT       200
#define SLIC_LOAD_0               0
#define SLIC_INTAGG_0             0
#define SLIC_LOAD_1               8000
#define SLIC_LOAD_2               10000
#define SLIC_LOAD_3               12000
#define SLIC_LOAD_4               14000
#define SLIC_LOAD_5               16000
#define SLIC_INTAGG_1             50
#define SLIC_INTAGG_2             100
#define SLIC_INTAGG_3             150
#define SLIC_INTAGG_4             200
#define SLIC_INTAGG_5             250
#define SLIC_LOAD_1GB             3000
#define SLIC_LOAD_2GB             6000
#define SLIC_LOAD_3GB             12000
#define SLIC_LOAD_4GB             24000
#define SLIC_LOAD_5GB             48000
#define SLIC_INTAGG_1GB           50
#define SLIC_INTAGG_2GB           75
#define SLIC_INTAGG_3GB           100
#define SLIC_INTAGG_4GB           100
#define SLIC_INTAGG_5GB           100

struct ether_header {
	unsigned char    ether_dhost[6];
	unsigned char    ether_shost[6];
	ushort   ether_type;
};

struct sliccard {
	uint              busnumber;
	uint              slotnumber;
	uint              state;
	uint              cardnum;
	uint              card_size;
	uint              adapters_activated;
	uint              adapters_allocated;
	uint              adapters_sleeping;
	uint              gennumber;
	u32           events;
	u32           loadlevel_current;
	u32           load;
	uint              reset_in_progress;
	u32           pingstatus;
	u32           bad_pingstatus;
	struct timer_list loadtimer;
	u32           loadtimerset;
	uint              config_set;
	struct slic_config  config;
	struct adapter  *master;
	struct adapter  *adapter[SLIC_MAX_PORTS];
	struct sliccard *next;
	u32             error_interrupts;
	u32             error_rmiss_interrupts;
	u32             rcv_interrupts;
	u32             xmit_interrupts;
	u32             num_isrs;
	u32             false_interrupts;
	u32             max_isr_rcvs;
	u32             max_isr_xmits;
	u32             rcv_interrupt_yields;
	u32             tx_packets;
	u32             debug_ix;
	ushort              reg_type[32];
	ushort              reg_offset[32];
	u32             reg_value[32];
	u32             reg_valueh[32];
};

#define NUM_CFG_SPACES      2
#define NUM_CFG_REGS        64
#define NUM_CFG_REG_ULONGS  (NUM_CFG_REGS / sizeof(u32))

struct physcard {
	struct adapter  *adapter[SLIC_MAX_PORTS];
	struct physcard *next;
	uint                adapters_allocd;

/*
 * the following is not currently needed
 *	u32              bridge_busnum;
 *	u32              bridge_cfg[NUM_CFG_SPACES][NUM_CFG_REG_ULONGS];
 */
};

struct base_driver {
	spinlock_t       driver_lock;
	u32              num_slic_cards;
	u32              num_slic_ports;
	u32              num_slic_ports_active;
	u32              dynamic_intagg;
	struct sliccard  *slic_card;
	struct physcard  *phys_card;
	uint                 cardnuminuse[SLIC_MAX_CARDS];
};

struct slic_shmem {
	volatile u32          isr;
	volatile u32          linkstatus;
	volatile struct slic_stats     inicstats;
};

struct slic_upr {
	uint               adapter;
	u32            upr_request;
	u32            upr_data;
	u32            upr_data_h;
	u32            upr_buffer;
	u32            upr_buffer_h;
	struct slic_upr *next;
};

struct slic_ifevents {
	uint        oflow802;
	uint        uflow802;
	uint        Tprtoflow;
	uint        rcvearly;
	uint        Bufov;
	uint        Carre;
	uint        Longe;
	uint        Invp;
	uint        Crc;
	uint        Drbl;
	uint        Code;
	uint        IpHlen;
	uint        IpLen;
	uint        IpCsum;
	uint        TpCsum;
	uint        TpHlen;
};

struct adapter {
	void *ifp;
	struct sliccard *card;
	uint                port;
	struct physcard *physcard;
	uint                physport;
	uint                cardindex;
	uint                card_size;
	uint                chipid;
	struct net_device  *netdev;
	spinlock_t          adapter_lock;
	spinlock_t          reset_lock;
	struct pci_dev     *pcidev;
	uint                busnumber;
	uint                slotnumber;
	uint                functionnumber;
	ushort              vendid;
	ushort              devid;
	ushort              subsysid;
	u32             irq;
	u32             drambase;
	u32             dramlength;
	uint                queues_initialized;
	uint                allocated;
	uint                activated;
	u32             intrregistered;
	uint                isp_initialized;
	uint                gennumber;
	struct slic_shmem      *pshmem;
	dma_addr_t          phys_shmem;
	u32             isrcopy;
	__iomem struct slic_regs       *slic_regs;
	void __iomem *regs;
	unsigned char               state;
	unsigned char               linkstate;
	unsigned char               linkspeed;
	unsigned char               linkduplex;
	uint                flags;
	unsigned char               macaddr[6];
	unsigned char               currmacaddr[6];
	u32             macopts;
	ushort              devflags_prev;
	u64             mcastmask;
	struct mcast_address   *mcastaddrs;
	struct slic_upr   *upr_list;
	uint                upr_busy;
	struct timer_list   pingtimer;
	u32             pingtimerset;
	struct timer_list   loadtimer;
	u32             loadtimerset;
	spinlock_t               upr_lock;
	spinlock_t               bit64reglock;
	struct slic_rspqueue     rspqueue;
	struct slic_rcvqueue     rcvqueue;
	struct slic_cmdqueue     cmdq_free;
	struct slic_cmdqueue     cmdq_done;
	struct slic_cmdqueue     cmdq_all;
	struct slic_cmdqmem      cmdqmem;
	/*
	*  SLIC Handles
	*/
	/* Object handles*/
	struct slic_handle slic_handles[SLIC_CMDQ_MAXCMDS + 1];
	/* Free object handles*/
	struct slic_handle *pfree_slic_handles;
	/* Object handle list lock*/
	spinlock_t          handle_lock;
	ushort              slic_handle_ix;

	u32             xmitq_full;
	u32             all_reg_writes;
	u32             icr_reg_writes;
	u32             isr_reg_writes;
	u32             error_interrupts;
	u32             error_rmiss_interrupts;
	u32             rx_errors;
	u32             rcv_drops;
	u32             rcv_interrupts;
	u32             xmit_interrupts;
	u32             linkevent_interrupts;
	u32             upr_interrupts;
	u32             num_isrs;
	u32             false_interrupts;
	u32             tx_packets;
	u32             xmit_completes;
	u32             tx_drops;
	u32             rcv_broadcasts;
	u32             rcv_multicasts;
	u32             rcv_unicasts;
	u32             max_isr_rcvs;
	u32             max_isr_xmits;
	u32             rcv_interrupt_yields;
	u32             intagg_period;
	u32             intagg_delay;
	u32             dynamic_intagg;
	struct inicpm_state    *inicpm_info;
	void *pinicpm_info;
	struct slic_ifevents  if_events;
	struct slic_stats        inicstats_prev;
	struct slicnet_stats     slic_stats;
};

static inline u32 slic_read32(struct adapter *adapter, unsigned int reg)
{
	return ioread32(adapter->regs + reg);
}

static inline void slic_write32(struct adapter *adapter, unsigned int reg,
				u32 val)
{
	iowrite32(val, adapter->regs + reg);
}

static inline void slic_write64(struct adapter *adapter, unsigned int reg,
				u32 val, u32 hiaddr)
{
	unsigned long flags;

	spin_lock_irqsave(&adapter->bit64reglock, flags);
	slic_write32(adapter, SLIC_REG_ADDR_UPPER, hiaddr);
	slic_write32(adapter, reg, val);
	mmiowb();
	spin_unlock_irqrestore(&adapter->bit64reglock, flags);
}

#define UPDATE_STATS(largestat, newstat, oldstat)                        \
{                                                                        \
	if ((newstat) < (oldstat))                                       \
		(largestat) += ((newstat) + (0xFFFFFFFF - oldstat + 1)); \
	else                                                             \
		(largestat) += ((newstat) - (oldstat));                  \
}

#define UPDATE_STATS_GB(largestat, newstat, oldstat)                     \
{                                                                        \
	(largestat) += ((newstat) - (oldstat));                          \
}

#if BITS_PER_LONG == 64
#define   SLIC_GET_ADDR_LOW(_addr)  (u32)((u64)(_addr) & \
	0x00000000FFFFFFFF)
#define   SLIC_GET_ADDR_HIGH(_addr)  (u32)(((u64)(_addr) >> 32) & \
	0x00000000FFFFFFFF)
#elif BITS_PER_LONG == 32
#define   SLIC_GET_ADDR_LOW(_addr)   (u32)(_addr)
#define   SLIC_GET_ADDR_HIGH(_addr)  (u32)0
#else
#error BITS_PER_LONG must be 32 or 64
#endif

#define FLUSH		true
#define DONT_FLUSH	false

#define SIOCSLICSETINTAGG        (SIOCDEVPRIVATE + 10)

#endif /*  __SLIC_DRIVER_H__ */
