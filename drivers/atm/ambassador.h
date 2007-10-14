/*
  Madge Ambassador ATM Adapter driver.
  Copyright (C) 1995-1999  Madge Networks Ltd.

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

#ifndef AMBASSADOR_H
#define AMBASSADOR_H


#ifdef CONFIG_ATM_AMBASSADOR_DEBUG
#define DEBUG_AMBASSADOR
#endif

#define DEV_LABEL                          "amb"

#ifndef PCI_VENDOR_ID_MADGE
#define PCI_VENDOR_ID_MADGE                0x10B6
#endif
#ifndef PCI_VENDOR_ID_MADGE_AMBASSADOR
#define PCI_DEVICE_ID_MADGE_AMBASSADOR     0x1001
#endif
#ifndef PCI_VENDOR_ID_MADGE_AMBASSADOR_BAD
#define PCI_DEVICE_ID_MADGE_AMBASSADOR_BAD 0x1002
#endif

// diagnostic output

#define PRINTK(severity,format,args...) \
  printk(severity DEV_LABEL ": " format "\n" , ## args)

#ifdef DEBUG_AMBASSADOR

#define DBG_ERR  0x0001
#define DBG_WARN 0x0002
#define DBG_INFO 0x0004
#define DBG_INIT 0x0008
#define DBG_LOAD 0x0010
#define DBG_VCC  0x0020
#define DBG_QOS  0x0040
#define DBG_CMD  0x0080
#define DBG_TX   0x0100
#define DBG_RX   0x0200
#define DBG_SKB  0x0400
#define DBG_POOL 0x0800
#define DBG_IRQ  0x1000
#define DBG_FLOW 0x2000
#define DBG_REGS 0x4000
#define DBG_DATA 0x8000
#define DBG_MASK 0xffff

/* the ## prevents the annoying double expansion of the macro arguments */
/* KERN_INFO is used since KERN_DEBUG often does not make it to the console */
#define PRINTDB(bits,format,args...) \
  ( (debug & (bits)) ? printk (KERN_INFO DEV_LABEL ": " format , ## args) : 1 )
#define PRINTDM(bits,format,args...) \
  ( (debug & (bits)) ? printk (format , ## args) : 1 )
#define PRINTDE(bits,format,args...) \
  ( (debug & (bits)) ? printk (format "\n" , ## args) : 1 )
#define PRINTD(bits,format,args...) \
  ( (debug & (bits)) ? printk (KERN_INFO DEV_LABEL ": " format "\n" , ## args) : 1 )

#else

#define PRINTD(bits,format,args...)
#define PRINTDB(bits,format,args...)
#define PRINTDM(bits,format,args...)
#define PRINTDE(bits,format,args...)

#endif

#define PRINTDD(bits,format,args...)
#define PRINTDDB(sec,fmt,args...)
#define PRINTDDM(sec,fmt,args...)
#define PRINTDDE(sec,fmt,args...)

// tunable values (?)

/* MUST be powers of two -- why ? */
#define COM_Q_ENTRIES        8
#define TX_Q_ENTRIES        32
#define RX_Q_ENTRIES        64

// fixed values

// guessing
#define AMB_EXTENT         0x80

// Minimum allowed size for an Ambassador queue
#define MIN_QUEUE_SIZE     2

// Ambassador microcode allows 1 to 4 pools, we use 4 (simpler)
#define NUM_RX_POOLS	   4

// minimum RX buffers required to cope with replenishing delay
#define MIN_RX_BUFFERS	   1

// minimum PCI latency we will tolerate (32 IS TOO SMALL)
#define MIN_PCI_LATENCY   64 // 255

// VCs supported by card (VPI always 0)
#define NUM_VPI_BITS       0
#define NUM_VCI_BITS      10
#define NUM_VCS         1024

/* The status field bits defined so far. */
#define RX_ERR		0x8000 // always present if there is an error (hmm)
#define CRC_ERR		0x4000 // AAL5 CRC error
#define LEN_ERR		0x2000 // overlength frame
#define ABORT_ERR	0x1000 // zero length field in received frame
#define UNUSED_ERR	0x0800 // buffer returned unused

// Adaptor commands

#define SRB_OPEN_VC		0
/* par_0: dwordswap(VC_number) */
/* par_1: dwordswap(flags<<16) or wordswap(flags)*/ 
/* flags:		*/

/* LANE:	0x0004		*/
/* NOT_UBR:	0x0008		*/
/* ABR:		0x0010		*/

/* RxPool0:	0x0000		*/
/* RxPool1:	0x0020		*/
/* RxPool2:	0x0040		*/
/* RxPool3:	0x0060		*/

/* par_2: dwordswap(fp_rate<<16) or wordswap(fp_rate) */

#define	SRB_CLOSE_VC		1
/* par_0: dwordswap(VC_number) */

#define	SRB_GET_BIA		2
/* returns 		*/
/* par_0: dwordswap(half BIA) */
/* par_1: dwordswap(half BIA) */

#define	SRB_GET_SUNI_STATS	3
/* par_0: dwordswap(physical_host_address) */

#define	SRB_SET_BITS_8		4
#define	SRB_SET_BITS_16		5
#define	SRB_SET_BITS_32		6
#define	SRB_CLEAR_BITS_8	7
#define	SRB_CLEAR_BITS_16	8
#define	SRB_CLEAR_BITS_32	9
/* par_0: dwordswap(ATMizer address)	*/
/* par_1: dwordswap(mask) */

#define	SRB_SET_8		10
#define	SRB_SET_16		11
#define	SRB_SET_32		12
/* par_0: dwordswap(ATMizer address)	*/
/* par_1: dwordswap(data) */

#define	SRB_GET_32		13
/* par_0: dwordswap(ATMizer address)	*/
/* returns			*/
/* par_1: dwordswap(ATMizer data) */

#define SRB_GET_VERSION		14
/* returns 		*/
/* par_0: dwordswap(Major Version) */
/* par_1: dwordswap(Minor Version) */

#define SRB_FLUSH_BUFFER_Q	15
/* Only flags to define which buffer pool; all others must be zero */
/* par_0: dwordswap(flags<<16) or wordswap(flags)*/ 

#define	SRB_GET_DMA_SPEEDS	16
/* returns 		*/
/* par_0: dwordswap(Read speed (bytes/sec)) */
/* par_1: dwordswap(Write speed (bytes/sec)) */

#define SRB_MODIFY_VC_RATE	17
/* par_0: dwordswap(VC_number) */
/* par_1: dwordswap(fp_rate<<16) or wordswap(fp_rate) */

#define SRB_MODIFY_VC_FLAGS	18
/* par_0: dwordswap(VC_number) */
/* par_1: dwordswap(flags<<16) or wordswap(flags)*/ 

/* flags:		*/

/* LANE:	0x0004		*/
/* NOT_UBR:	0x0008		*/
/* ABR:		0x0010		*/

/* RxPool0:	0x0000		*/
/* RxPool1:	0x0020		*/
/* RxPool2:	0x0040		*/
/* RxPool3:	0x0060		*/

#define SRB_RATE_SHIFT          16
#define SRB_POOL_SHIFT          (SRB_FLAGS_SHIFT+5)
#define SRB_FLAGS_SHIFT         16

#define	SRB_STOP_TASKING	19
#define	SRB_START_TASKING	20
#define SRB_SHUT_DOWN		21
#define MAX_SRB			21

#define SRB_COMPLETE		0xffffffff

#define TX_FRAME          	0x80000000

// number of types of SRB MUST be a power of two -- why?
#define NUM_OF_SRB	32

// number of bits of period info for rate
#define MAX_RATE_BITS	6

#define TX_UBR          0x0000
#define TX_UBR_CAPPED   0x0008
#define TX_ABR          0x0018
#define TX_FRAME_NOTCAP 0x0000
#define TX_FRAME_CAPPED 0x8000

#define FP_155_RATE	0x24b1
#define FP_25_RATE	0x1f9d

/* #define VERSION_NUMBER 0x01000000 // initial release */
/* #define VERSION_NUMBER 0x01010000 // fixed startup probs PLX MB0 not cleared */
/* #define VERSION_NUMBER 0x01020000 // changed SUNI reset timings; allowed r/w onchip */

/* #define VERSION_NUMBER 0x01030000 // clear local doorbell int reg on reset */
/* #define VERSION_NUMBER 0x01040000 // PLX bug work around version PLUS */
/* remove race conditions on basic interface */
/* indicate to the host that diagnostics */
/* have finished; if failed, how and what  */
/* failed */
/* fix host memory test to fix PLX bug */
/* allow flash upgrade and BIA upgrade directly */
/*  */
#define VERSION_NUMBER 0x01050025 /* Jason's first hacked version. */
/* Change in download algorithm */

#define DMA_VALID 0xb728e149 /* completely random */

#define FLASH_BASE 0xa0c00000
#define FLASH_SIZE 0x00020000			/* 128K */
#define BIA_BASE (FLASH_BASE+0x0001c000)	/* Flash Sector 7 */
#define BIA_ADDRESS ((void *)0xa0c1c000)
#define PLX_BASE 0xe0000000

typedef enum {
  host_memory_test = 1,
  read_adapter_memory,
  write_adapter_memory,
  adapter_start,
  get_version_number,
  interrupt_host,
  flash_erase_sector,
  adap_download_block = 0x20,
  adap_erase_flash,
  adap_run_in_iram,
  adap_end_download
} loader_command;

#define BAD_COMMAND                     (-1)
#define COMMAND_IN_PROGRESS             1
#define COMMAND_PASSED_TEST             2
#define COMMAND_FAILED_TEST             3
#define COMMAND_READ_DATA_OK            4
#define COMMAND_READ_BAD_ADDRESS        5
#define COMMAND_WRITE_DATA_OK           6
#define COMMAND_WRITE_BAD_ADDRESS       7
#define COMMAND_WRITE_FLASH_FAILURE     8
#define COMMAND_COMPLETE                9
#define COMMAND_FLASH_ERASE_FAILURE	10
#define COMMAND_WRITE_BAD_DATA		11

/* bit fields for mailbox[0] return values */

#define GPINT_TST_FAILURE               0x00000001      
#define SUNI_DATA_PATTERN_FAILURE       0x00000002
#define SUNI_DATA_BITS_FAILURE          0x00000004
#define SUNI_UTOPIA_FAILURE             0x00000008
#define SUNI_FIFO_FAILURE               0x00000010
#define SRAM_FAILURE                    0x00000020
#define SELF_TEST_FAILURE               0x0000003f

/* mailbox[1] = 0 in progress, -1 on completion */
/* mailbox[2] = current test 00 00 test(8 bit) phase(8 bit) */
/* mailbox[3] = last failure, 00 00 test(8 bit) phase(8 bit) */
/* mailbox[4],mailbox[5],mailbox[6] random failure values */

/* PLX/etc. memory map including command structure */

/* These registers may also be memory mapped in PCI memory */

#define UNUSED_LOADER_MAILBOXES 6

typedef struct {
  u32 stuff[16];
  union {
    struct {
      u32 result;
      u32 ready;
      u32 stuff[UNUSED_LOADER_MAILBOXES];
    } loader;
    struct {
      u32 cmd_address;
      u32 tx_address;
      u32 rx_address[NUM_RX_POOLS];
      u32 gen_counter;
      u32 spare;
    } adapter;
  } mb;
  u32 doorbell;
  u32 interrupt;
  u32 interrupt_control;
  u32 reset_control;
} amb_mem;

/* RESET bit, IRQ (card to host) and doorbell (host to card) enable bits */
#define AMB_RESET_BITS	   0x40000000
#define AMB_INTERRUPT_BITS 0x00000300
#define AMB_DOORBELL_BITS  0x00030000

/* loader commands */

#define MAX_COMMAND_DATA 13
#define MAX_TRANSFER_DATA 11

typedef struct {
  __be32 address;
  __be32 count;
  __be32 data[MAX_TRANSFER_DATA];
} transfer_block;

typedef struct {
  __be32 result;
  __be32 command;
  union {
    transfer_block transfer;
    __be32 version;
    __be32 start;
    __be32 data[MAX_COMMAND_DATA];
  } payload;
  __be32 valid;
} loader_block;

/* command queue */

/* Again all data are BIG ENDIAN */

typedef	struct {
  union {
    struct {
      __be32 vc;
      __be32 flags;
      __be32 rate;
    } open;
    struct {
      __be32 vc;
      __be32 rate;
    } modify_rate;
    struct {
      __be32 vc;
      __be32 flags;
    } modify_flags;
    struct {
      __be32 vc;
    } close;
    struct {
      __be32 lower4;
      __be32 upper2;
    } bia;
    struct {
      __be32 address;
    } suni;
    struct {
      __be32 major;
      __be32 minor;
    } version;
    struct {
      __be32 read;
      __be32 write;
    } speed;
    struct {
      __be32 flags;
    } flush;
    struct {
      __be32 address;
      __be32 data;
    } memory;
    __be32 par[3];
  } args;
  __be32 request;
} command;

/* transmit queues and associated structures */

/* The hosts transmit structure. All BIG ENDIAN; host address
   restricted to first 1GByte, but address passed to the card must
   have the top MS bit or'ed in. -- check this */

/* TX is described by 1+ tx_frags followed by a tx_frag_end */

typedef struct {
  __be32 bytes;
  __be32 address;
} tx_frag;

/* apart from handle the fields here are for the adapter to play with
   and should be set to zero */

typedef struct {
  u32	handle;
  u16	vc;
  u16	next_descriptor_length;
  u32	next_descriptor;
#ifdef AMB_NEW_MICROCODE
  u8    cpcs_uu;
  u8    cpi;
  u16   pad;
#endif
} tx_frag_end;

typedef struct {
  tx_frag tx_frag;
  tx_frag_end tx_frag_end;
  struct sk_buff * skb;
} tx_simple;

#if 0
typedef union {
  tx_frag	fragment;
  tx_frag_end	end_of_list;
} tx_descr;
#endif

/* this "points" to the sequence of fragments and trailer */

typedef	struct {
  __be16	vc;
  __be16	tx_descr_length;
  __be32	tx_descr_addr;
} tx_in;

/* handle is the handle from tx_in */

typedef	struct {
  u32 handle;
} tx_out;

/* receive frame structure */

/* All BIG ENDIAN; handle is as passed from host; length is zero for
   aborted frames, and frames with errors. Header is actually VC
   number, lec-id is NOT yet supported. */

typedef struct {
  u32  handle;
  __be16  vc;
  __be16  lec_id; // unused
  __be16  status;
  __be16  length;
} rx_out;

/* buffer supply structure */

typedef	struct {
  u32 handle;
  __be32 host_address;
} rx_in;

/* This first structure is the area in host memory where the adapter
   writes its pointer values. These pointer values are BIG ENDIAN and
   reside in the same 4MB 'page' as this structure. The host gives the
   adapter the address of this block by sending a doorbell interrupt
   to the adapter after downloading the code and setting it going. The
   addresses have the top 10 bits set to 1010000010b -- really?
   
   The host must initialise these before handing the block to the
   adapter. */

typedef struct {
  __be32 command_start;		/* SRB commands completions */
  __be32 command_end;		/* SRB commands completions */
  __be32 tx_start;
  __be32 tx_end;
  __be32 txcom_start;		/* tx completions */
  __be32 txcom_end;		/* tx completions */
  struct {
    __be32 buffer_start;
    __be32 buffer_end;
    u32 buffer_q_get;
    u32 buffer_q_end;
    u32 buffer_aptr;
    __be32 rx_start;		/* rx completions */
    __be32 rx_end;
    u32 rx_ptr;
    __be32 buffer_size;		/* size of host buffer */
  } rec_struct[NUM_RX_POOLS];
#ifdef AMB_NEW_MICROCODE
  u16 init_flags;
  u16 talk_block_spare;
#endif
} adap_talk_block;

/* This structure must be kept in line with the vcr image in sarmain.h
   
   This is the structure in the host filled in by the adapter by
   GET_SUNI_STATS */

typedef struct {
  u8	racp_chcs;
  u8	racp_uhcs;
  u16	spare;
  u32	racp_rcell;
  u32	tacp_tcell;
  u32	flags;
  u32	dropped_cells;
  u32	dropped_frames;
} suni_stats;

typedef enum {
  dead
} amb_flags;

#define NEXTQ(current,start,limit) \
  ( (current)+1 < (limit) ? (current)+1 : (start) ) 

typedef struct {
  command * start;
  command * in;
  command * out;
  command * limit;
} amb_cq_ptrs;

typedef struct {
  spinlock_t lock;
  unsigned int pending;
  unsigned int high;
  unsigned int filled;
  unsigned int maximum; // size - 1 (q implementation)
  amb_cq_ptrs ptrs;
} amb_cq;

typedef struct {
  spinlock_t lock;
  unsigned int pending;
  unsigned int high;
  unsigned int filled;
  unsigned int maximum; // size - 1 (q implementation)
  struct {
    tx_in * start;
    tx_in * ptr;
    tx_in * limit;
  } in;
  struct {
    tx_out * start;
    tx_out * ptr;
    tx_out * limit;
  } out;
} amb_txq;

typedef struct {
  spinlock_t lock;
  unsigned int pending;
  unsigned int low;
  unsigned int emptied;
  unsigned int maximum; // size - 1 (q implementation)
  struct {
    rx_in * start;
    rx_in * ptr;
    rx_in * limit;
  } in;
  struct {
    rx_out * start;
    rx_out * ptr;
    rx_out * limit;
  } out;
  unsigned int buffers_wanted;
  unsigned int buffer_size;
} amb_rxq;

typedef struct {
  unsigned long tx_ok;
  struct {
    unsigned long ok;
    unsigned long error;
    unsigned long badcrc;
    unsigned long toolong;
    unsigned long aborted;
    unsigned long unused;
  } rx;
} amb_stats;

// a single struct pointed to by atm_vcc->dev_data

typedef struct {
  u8               tx_vc_bits:7;
  u8               tx_present:1;
} amb_tx_info;

typedef struct {
  unsigned char    pool;
} amb_rx_info;

typedef struct {
  amb_rx_info      rx_info;
  u16              tx_frame_bits;
  unsigned int     tx_rate;
  unsigned int     rx_rate;
} amb_vcc;

struct amb_dev {
  u8               irq;
  unsigned long	   flags;
  u32              iobase;
  u32 *            membase;

#ifdef FILL_RX_POOLS_IN_BH
  struct work_struct bh;
#endif
  
  amb_cq           cq;
  amb_txq          txq;
  amb_rxq          rxq[NUM_RX_POOLS];
  
  struct semaphore vcc_sf;
  amb_tx_info      txer[NUM_VCS];
  struct atm_vcc * rxer[NUM_VCS];
  unsigned int     tx_avail;
  unsigned int     rx_avail;
  
  amb_stats        stats;
  
  struct atm_dev * atm_dev;
  struct pci_dev * pci_dev;
  struct timer_list housekeeping;
};

typedef struct amb_dev amb_dev;

#define AMB_DEV(atm_dev) ((amb_dev *) (atm_dev)->dev_data)
#define AMB_VCC(atm_vcc) ((amb_vcc *) (atm_vcc)->dev_data)

/* the microcode */

typedef struct {
  u32 start;
  unsigned int count;
} region;

static region ucode_regions[];
static u32 ucode_data[];
static u32 ucode_start;

/* rate rounding */

typedef enum {
  round_up,
  round_down,
  round_nearest
} rounding;

#endif
