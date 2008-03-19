/**************************************************************************
 *
 * Copyright (c) 2000-2002 Alacritech, Inc.  All rights reserved.
 *
 * $Id: slic.h,v 1.3 2006/07/14 16:43:02 mook Exp $
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


struct slic_spinlock {
	spinlock_t	lock;
	unsigned long	flags;
};

#define SLIC_RSPQ_PAGES_GB        10
#define SLIC_RSPQ_BUFSINPAGE      (PAGE_SIZE / SLIC_RSPBUF_SIZE)

typedef struct _slic_rspqueue_t {
    ulong32             offset;
    ulong32             pageindex;
    ulong32             num_pages;
    p_slic_rspbuf_t     rspbuf;
    pulong32            vaddr[SLIC_RSPQ_PAGES_GB];
    dma_addr_t          paddr[SLIC_RSPQ_PAGES_GB];
}    slic_rspqueue_t, *p_slic_rspqueue_t;

#define SLIC_RCVQ_EXPANSION         1
#define SLIC_RCVQ_ENTRIES           (256 * SLIC_RCVQ_EXPANSION)
#define SLIC_RCVQ_MINENTRIES        (SLIC_RCVQ_ENTRIES / 2)
#define SLIC_RCVQ_MAX_PROCESS_ISR   ((SLIC_RCVQ_ENTRIES * 4))
#define SLIC_RCVQ_RCVBUFSIZE        2048
#define SLIC_RCVQ_FILLENTRIES       (16 * SLIC_RCVQ_EXPANSION)
#define SLIC_RCVQ_FILLTHRESH        (SLIC_RCVQ_ENTRIES - SLIC_RCVQ_FILLENTRIES)

typedef struct _slic_rcvqueue_t {
    struct sk_buff    *head;
    struct sk_buff    *tail;
    ulong32            count;
    ulong32            size;
    ulong32            errors;
} slic_rcvqueue_t, *p_slic_rcvqueue_t;

typedef struct _slic_rcvbuf_info_t {
    ulong32     id;
    ulong32     starttime;
    ulong32     stoptime;
    ulong32     slicworld;
    ulong32     lasttime;
    ulong32     lastid;
}  slic_rcvbuf_info_t, *pslic_rcvbuf_info_t;
/*
 SLIC Handle structure.  Used to restrict handle values to
 32 bits by using an index rather than an address.
 Simplifies ucode in 64-bit systems
*/
typedef struct _slic_handle_word_t {
	union {
		struct {
			ushort      index;
			ushort      bottombits; /* to denote num bufs to card */
		}  parts;
		ulong32         whole;
	}  handle;
}  slic_handle_word_t, *pslic_handle_word_t;

typedef struct _slic_handle_t {
    slic_handle_word_t          token;   /* token passed between host and card*/
    ushort                      type;
    pvoid                       address;    /* actual address of the object*/
    ushort                      offset;
    struct _slic_handle_t       *other_handle;
    struct _slic_handle_t       *next;
} slic_handle_t, *pslic_handle_t;

#define SLIC_HANDLE_FREE        0x0000
#define SLIC_HANDLE_DATA        0x0001
#define SLIC_HANDLE_CMD         0x0002
#define SLIC_HANDLE_CONTEXT     0x0003
#define SLIC_HANDLE_TEAM        0x0004

#define handle_index        handle.parts.index
#define handle_bottom       handle.parts.bottombits
#define handle_token        handle.whole

#define SLIC_HOSTCMD_SIZE    512

typedef struct _slic_hostcmd_t {
    slic_host64_cmd_t          cmd64;
    ulong32                    type;
    struct sk_buff            *skb;
    ulong32                    paddrl;
    ulong32                    paddrh;
    ulong32                    busy;
    ulong32                    cmdsize;
    ushort                     numbufs;
    pslic_handle_t             pslic_handle;/* handle associated with command */
    struct _slic_hostcmd_t    *next;
    struct _slic_hostcmd_t    *next_all;
} slic_hostcmd_t, *p_slic_hostcmd_t;

#define SLIC_CMDQ_CMDSINPAGE    (PAGE_SIZE / SLIC_HOSTCMD_SIZE)
#define SLIC_CMD_DUMB            3
#define SLIC_CMDQ_INITCMDS       256
#define SLIC_CMDQ_MAXCMDS        256
#define SLIC_CMDQ_MAXOUTSTAND    SLIC_CMDQ_MAXCMDS
#define SLIC_CMDQ_MAXPAGES       (SLIC_CMDQ_MAXCMDS / SLIC_CMDQ_CMDSINPAGE)
#define SLIC_CMDQ_INITPAGES      (SLIC_CMDQ_INITCMDS / SLIC_CMDQ_CMDSINPAGE)

typedef struct _slic_cmdqmem_t {
    int               pagecnt;
    pulong32          pages[SLIC_CMDQ_MAXPAGES];
    dma_addr_t        dma_pages[SLIC_CMDQ_MAXPAGES];
}          slic_cmdqmem_t, *p_slic_cmdqmem_t;

typedef struct _slic_cmdqueue_t {
    p_slic_hostcmd_t   head;
    p_slic_hostcmd_t   tail;
    int                count;
    struct slic_spinlock	lock;
}    slic_cmdqueue_t, *p_slic_cmdqueue_t;

#ifdef STATUS_SUCCESS
#undef STATUS_SUCCESS
#endif

#define STATUS_SUCCESS              0
#define STATUS_PENDING              0
#define STATUS_FAILURE             -1
#define STATUS_ERROR               -2
#define STATUS_NOT_SUPPORTED       -3
#define STATUS_BUFFER_TOO_SHORT    -4

#define SLIC_MAX_CARDS              32
#define SLIC_MAX_PORTS              4        /* Max # of ports per card   */
#if SLIC_DUMP_ENABLED
/*
Dump buffer size

This cannot be bigger than the max DMA size the card supports,
given the current code structure in the host and ucode.
Mojave supports 16K, Oasis supports 16K-1, so
just set this at 15K, shouldnt make that much of a diff.
*/
#define DUMP_BUF_SIZE               0x3C00
#endif


typedef struct _mcast_address_t {
    uchar                     address[6];
    struct _mcast_address_t   *next;
}  mcast_address_t, *p_mcast_address_t;

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

typedef struct _slic_iface_stats {
    /*
     * Stats
     */
    ulong64        xmt_bytes;
    ulong64        xmt_ucast;
    ulong64        xmt_mcast;
    ulong64        xmt_bcast;
    ulong64        xmt_errors;
    ulong64        xmt_discards;
    ulong64        xmit_collisions;
    ulong64        xmit_excess_xmit_collisions;
    ulong64        rcv_bytes;
    ulong64        rcv_ucast;
    ulong64        rcv_mcast;
    ulong64        rcv_bcast;
    ulong64        rcv_errors;
    ulong64        rcv_discards;
} slic_iface_stats_t, *p_slic_iface_stats_t;

typedef struct _slic_tcp_stats {
    ulong64        xmit_tcp_segs;
    ulong64        xmit_tcp_bytes;
    ulong64        rcv_tcp_segs;
    ulong64        rcv_tcp_bytes;
} slic_tcp_stats_t, *p_slic_tcp_stats_t;

typedef struct _slicnet_stats {
    slic_tcp_stats_t        tcp;
    slic_iface_stats_t      iface;

} slicnet_stats_t, *p_slicnet_stats_t;

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

typedef struct _ether_header {
    uchar    ether_dhost[6];
    uchar    ether_shost[6];
    ushort   ether_type;
} ether_header, *p_ether_header;

typedef struct _sliccard_t {
    uint              busnumber;
    uint              slotnumber;
    uint              state;
    uint              cardnum;
    uint              card_size;
    uint              adapters_activated;
    uint              adapters_allocated;
    uint              adapters_sleeping;
    uint              gennumber;
    ulong32           events;
    ulong32           loadlevel_current;
    ulong32           load;
    uint              reset_in_progress;
    ulong32           pingstatus;
    ulong32           bad_pingstatus;
    struct timer_list loadtimer;
    ulong32           loadtimerset;
    uint              config_set;
    slic_config_t     config;
    struct dentry      *debugfs_dir;
    struct dentry      *debugfs_cardinfo;
    struct _adapter_t  *master;
    struct _adapter_t  *adapter[SLIC_MAX_PORTS];
    struct _sliccard_t *next;
    ulong32             error_interrupts;
    ulong32             error_rmiss_interrupts;
    ulong32             rcv_interrupts;
    ulong32             xmit_interrupts;
    ulong32             num_isrs;
    ulong32             false_interrupts;
    ulong32             max_isr_rcvs;
    ulong32             max_isr_xmits;
    ulong32             rcv_interrupt_yields;
    ulong32             tx_packets;
#if SLIC_DUMP_ENABLED
    ulong32             dumpstatus;           /* Result of dump UPR */
    pvoid               cmdbuffer;

    ulong               cmdbuffer_phys;
    ulong32             cmdbuffer_physl;
    ulong32             cmdbuffer_physh;

    ulong32             dump_count;
    struct task_struct *dump_task_id;
    ulong32             dump_wait_count;
    uint                dumpthread_running; /* has a dump thread been init'd  */
    uint                dump_requested;     /* 0 no, 1 = reqstd 2=curr 3=done */
    ulong32             dumptime_start;
    ulong32             dumptime_complete;
    ulong32             dumptime_delta;
    pvoid               dumpbuffer;
    ulong               dumpbuffer_phys;
    ulong32             dumpbuffer_physl;
    ulong32             dumpbuffer_physh;
    wait_queue_head_t   dump_wq;
    struct file        *dumphandle;
    mm_segment_t        dumpfile_fs;
#endif
    ulong32             debug_ix;
    ushort              reg_type[32];
    ushort              reg_offset[32];
    ulong32             reg_value[32];
    ulong32             reg_valueh[32];
} sliccard_t, *p_sliccard_t;

#define NUM_CFG_SPACES      2
#define NUM_CFG_REGS        64
#define NUM_CFG_REG_ULONGS  (NUM_CFG_REGS / sizeof(ulong32))

typedef struct _physcard_t {
    struct _adapter_t  *adapter[SLIC_MAX_PORTS];
    struct _physcard_t *next;
    uint                adapters_allocd;

 /*  the following is not currently needed
    ulong32               bridge_busnum;
    ulong32               bridge_cfg[NUM_CFG_SPACES][NUM_CFG_REG_ULONGS];
 */
} physcard_t, *p_physcard_t;

typedef struct _base_driver {
    struct slic_spinlock driver_lock;
    ulong32              num_slic_cards;
    ulong32              num_slic_ports;
    ulong32              num_slic_ports_active;
    ulong32              dynamic_intagg;
    p_sliccard_t         slic_card;
    p_physcard_t         phys_card;
    uint                 cardnuminuse[SLIC_MAX_CARDS];
} base_driver_t, *p_base_driver_t;

extern base_driver_t   slic_global;

typedef struct _slic_shmem_t {
    volatile ulong32          isr;
    volatile ulong32          linkstatus;
    volatile slic_stats_t     inicstats;
} slic_shmem_t, *p_slic_shmem_t;

typedef struct _slic_reg_params_t {
    ulong32       linkspeed;
    ulong32       linkduplex;
    ulong32       fail_on_bad_eeprom;
} slic_reg_params_t, *p_reg_params_t;

typedef struct _slic_upr_t {
    uint                  adapter;
    ulong32               upr_request;
    ulong32               upr_data;
    ulong32               upr_data_h;
    ulong32               upr_buffer;
    ulong32               upr_buffer_h;
    struct _slic_upr_t *next;

} slic_upr_t, *p_slic_upr_t;

typedef struct _slic_ifevents_ti {
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
} slic_ifevents_t;

typedef struct _adapter_t {
    pvoid               ifp;
    p_sliccard_t        card;
    uint                port;
    p_physcard_t        physcard;
    uint                physport;
    uint                cardindex;
    uint                card_size;
    uint                chipid;
    struct net_device        *netdev;
    struct net_device        *next_netdevice;
    struct slic_spinlock     adapter_lock;
    struct slic_spinlock     reset_lock;
    struct pci_dev     *pcidev;
    uint                busnumber;
    uint                slotnumber;
    uint                functionnumber;
    ushort              vendid;
    ushort              devid;
    ushort              subsysid;
    ulong32             irq;
    void __iomem *memorybase;
    ulong32             memorylength;
    ulong32             drambase;
    ulong32             dramlength;
    uint                queues_initialized;
    uint                allocated;
    uint                activated;
    ulong32             intrregistered;
    uint                isp_initialized;
    uint                gennumber;
    ulong32             curaddrupper;
    p_slic_shmem_t      pshmem;
    dma_addr_t          phys_shmem;
    ulong32             isrcopy;
    p_slic_regs_t       slic_regs;
    uchar               state;
    uchar               linkstate;
    uchar               linkspeed;
    uchar               linkduplex;
    uint                flags;
    uchar               macaddr[6];
    uchar               currmacaddr[6];
    ulong32             macopts;
    ushort              devflags_prev;
    ulong64             mcastmask;
    p_mcast_address_t   mcastaddrs;
    p_slic_upr_t        upr_list;
    uint                upr_busy;
    struct timer_list   pingtimer;
    ulong32             pingtimerset;
    struct timer_list   statstimer;
    ulong32             statstimerset;
    struct timer_list   loadtimer;
    ulong32             loadtimerset;
    struct dentry      *debugfs_entry;
    struct slic_spinlock     upr_lock;
    struct slic_spinlock     bit64reglock;
    slic_rspqueue_t     rspqueue;
    slic_rcvqueue_t     rcvqueue;
    slic_cmdqueue_t     cmdq_free;
    slic_cmdqueue_t     cmdq_done;
    slic_cmdqueue_t     cmdq_all;
    slic_cmdqmem_t      cmdqmem;
    /*
     *  SLIC Handles
    */
    slic_handle_t       slic_handles[SLIC_CMDQ_MAXCMDS+1]; /* Object handles*/
    pslic_handle_t      pfree_slic_handles;             /* Free object handles*/
    struct slic_spinlock     handle_lock;           /* Object handle list lock*/
    ushort              slic_handle_ix;

    ulong32             xmitq_full;
    ulong32             all_reg_writes;
    ulong32             icr_reg_writes;
    ulong32             isr_reg_writes;
    ulong32             error_interrupts;
    ulong32             error_rmiss_interrupts;
    ulong32             rx_errors;
    ulong32             rcv_drops;
    ulong32             rcv_interrupts;
    ulong32             xmit_interrupts;
    ulong32             linkevent_interrupts;
    ulong32             upr_interrupts;
    ulong32             num_isrs;
    ulong32             false_interrupts;
    ulong32             tx_packets;
    ulong32             xmit_completes;
    ulong32             tx_drops;
    ulong32             rcv_broadcasts;
    ulong32             rcv_multicasts;
    ulong32             rcv_unicasts;
    ulong32             max_isr_rcvs;
    ulong32             max_isr_xmits;
    ulong32             rcv_interrupt_yields;
    ulong32             intagg_period;
    p_inicpm_state_t    inicpm_info;
    pvoid               pinicpm_info;
    slic_reg_params_t   reg_params;
    slic_ifevents_t     if_events;
    slic_stats_t        inicstats_prev;
    slicnet_stats_t     slic_stats;
    struct net_device_stats stats;
} adapter_t, *p_adapter_t;

#if SLIC_DUMP_ENABLED
#define SLIC_DUMP_REQUESTED      1
#define SLIC_DUMP_IN_PROGRESS    2
#define SLIC_DUMP_DONE           3

/****************************************************************************
 *
 * Microcode crash information structure.  This
 * structure is written out to the card's SRAM when the microcode panic's.
 *
 ****************************************************************************/
typedef struct _slic_crash_info {
    ushort  cpu_id;
    ushort  crash_pc;
} slic_crash_info, *p_slic_crash_info;

#define CRASH_INFO_OFFSET   0x155C

#endif

#define UPDATE_STATS(largestat, newstat, oldstat)                        \
{                                                                        \
    if ((newstat) < (oldstat))                                           \
	(largestat) += ((newstat) + (0xFFFFFFFF - oldstat + 1));         \
    else                                                                 \
	(largestat) += ((newstat) - (oldstat));                          \
}

#define UPDATE_STATS_GB(largestat, newstat, oldstat)                     \
{                                                                        \
    (largestat) += ((newstat) - (oldstat));                              \
}

#define ETHER_EQ_ADDR(_AddrA, _AddrB, _Result)                           \
{                                                                        \
    _Result = TRUE;                                                      \
    if (*(pulong32)(_AddrA) != *(pulong32)(_AddrB))                          \
	_Result = FALSE;                                                 \
    if (*(pushort)(&((_AddrA)[4])) != *(pushort)(&((_AddrB)[4])))        \
	_Result = FALSE;                                                 \
}

#if defined(CONFIG_X86_64) || defined(CONFIG_IA64)
#define   SLIC_GET_ADDR_LOW(_addr)  (ulong32)((ulong64)(_addr) & \
	0x00000000FFFFFFFF)
#define   SLIC_GET_ADDR_HIGH(_addr)  (ulong32)(((ulong64)(_addr) >> 32) & \
	0x00000000FFFFFFFF)
#else
#define   SLIC_GET_ADDR_LOW(_addr)   (ulong32)_addr
#define   SLIC_GET_ADDR_HIGH(_addr)  (ulong32)0
#endif

#define FLUSH       TRUE
#define DONT_FLUSH  FALSE

#define SIOCSLICDUMPCARD         (SIOCDEVPRIVATE+9)
#define SIOCSLICSETINTAGG        (SIOCDEVPRIVATE+10)
#define SIOCSLICTRACEDUMP        (SIOCDEVPRIVATE+11)

#endif /*  __SLIC_DRIVER_H__ */
