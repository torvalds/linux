/*
 * ohci1394.h - driver for OHCI 1394 boards
 * Copyright (C)1999,2000 Sebastien Rougeaux <sebastien.rougeaux@anu.edu.au>
 *                        Gord Peters <GordPeters@smarttech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _OHCI1394_H
#define _OHCI1394_H

#include "ieee1394_types.h"
#include <asm/io.h>

#define OHCI1394_DRIVER_NAME      "ohci1394"

#define OHCI1394_MAX_AT_REQ_RETRIES	0x2
#define OHCI1394_MAX_AT_RESP_RETRIES	0x2
#define OHCI1394_MAX_PHYS_RESP_RETRIES	0x8
#define OHCI1394_MAX_SELF_ID_ERRORS	16

#define AR_REQ_NUM_DESC		4		/* number of AR req descriptors */
#define AR_REQ_BUF_SIZE		PAGE_SIZE	/* size of AR req buffers */
#define AR_REQ_SPLIT_BUF_SIZE	PAGE_SIZE	/* split packet buffer */

#define AR_RESP_NUM_DESC	4		/* number of AR resp descriptors */
#define AR_RESP_BUF_SIZE	PAGE_SIZE	/* size of AR resp buffers */
#define AR_RESP_SPLIT_BUF_SIZE	PAGE_SIZE	/* split packet buffer */

#define IR_NUM_DESC		16		/* number of IR descriptors */
#define IR_BUF_SIZE		PAGE_SIZE	/* 4096 bytes/buffer */
#define IR_SPLIT_BUF_SIZE	PAGE_SIZE	/* split packet buffer */

#define IT_NUM_DESC		16	/* number of IT descriptors */

#define AT_REQ_NUM_DESC		32	/* number of AT req descriptors */
#define AT_RESP_NUM_DESC	32	/* number of AT resp descriptors */

#define OHCI_LOOP_COUNT		100	/* Number of loops for reg read waits */

#define OHCI_CONFIG_ROM_LEN	1024	/* Length of the mapped configrom space */

#define OHCI1394_SI_DMA_BUF_SIZE	8192 /* length of the selfid buffer */

/* PCI configuration space addresses */
#define OHCI1394_PCI_HCI_Control 0x40

struct dma_cmd {
        u32 control;
        u32 address;
        u32 branchAddress;
        u32 status;
};

/*
 * FIXME:
 * It is important that a single at_dma_prg does not cross a page boundary
 * The proper way to do it would be to do the check dynamically as the
 * programs are inserted into the AT fifo.
 */
struct at_dma_prg {
	struct dma_cmd begin;
	quadlet_t data[4];
	struct dma_cmd end;
	quadlet_t pad[4]; /* FIXME: quick hack for memory alignment */
};

/* identify whether a DMA context is asynchronous or isochronous */
enum context_type { DMA_CTX_ASYNC_REQ, DMA_CTX_ASYNC_RESP, DMA_CTX_ISO };

/* DMA receive context */
struct dma_rcv_ctx {
	struct ti_ohci *ohci;
	enum context_type type;
	int ctx;
	unsigned int num_desc;

	unsigned int buf_size;
	unsigned int split_buf_size;

	/* dma block descriptors */
        struct dma_cmd **prg_cpu;
        dma_addr_t *prg_bus;
	struct pci_pool *prg_pool;

	/* dma buffers */
        quadlet_t **buf_cpu;
        dma_addr_t *buf_bus;

        unsigned int buf_ind;
        unsigned int buf_offset;
        quadlet_t *spb;
        spinlock_t lock;
        struct tasklet_struct task;
	int ctrlClear;
	int ctrlSet;
	int cmdPtr;
	int ctxtMatch;
};

/* DMA transmit context */
struct dma_trm_ctx {
	struct ti_ohci *ohci;
	enum context_type type;
	int ctx;
	unsigned int num_desc;

	/* dma block descriptors */
        struct at_dma_prg **prg_cpu;
	dma_addr_t *prg_bus;
	struct pci_pool *prg_pool;

        unsigned int prg_ind;
        unsigned int sent_ind;
	int free_prgs;
        quadlet_t *branchAddrPtr;

	/* list of packets inserted in the AT FIFO */
	struct list_head fifo_list;

	/* list of pending packets to be inserted in the AT FIFO */
	struct list_head pending_list;

        spinlock_t lock;
        struct tasklet_struct task;
	int ctrlClear;
	int ctrlSet;
	int cmdPtr;
};

struct ohci1394_iso_tasklet {
	struct tasklet_struct tasklet;
	struct list_head link;
	int context;
	enum { OHCI_ISO_TRANSMIT, OHCI_ISO_RECEIVE,
	       OHCI_ISO_MULTICHANNEL_RECEIVE } type;
};

struct ti_ohci {
        struct pci_dev *dev;

	enum {
		OHCI_INIT_ALLOC_HOST,
		OHCI_INIT_HAVE_MEM_REGION,
		OHCI_INIT_HAVE_IOMAPPING,
		OHCI_INIT_HAVE_CONFIG_ROM_BUFFER,
		OHCI_INIT_HAVE_SELFID_BUFFER,
		OHCI_INIT_HAVE_TXRX_BUFFERS__MAYBE,
		OHCI_INIT_HAVE_IRQ,
		OHCI_INIT_DONE,
	} init_state;

        /* remapped memory spaces */
        void __iomem *registers;

	/* dma buffer for self-id packets */
        quadlet_t *selfid_buf_cpu;
        dma_addr_t selfid_buf_bus;

	/* buffer for csr config rom */
        quadlet_t *csr_config_rom_cpu;
        dma_addr_t csr_config_rom_bus;
	int csr_config_rom_length;

	unsigned int max_packet_size;

        /* async receive */
	struct dma_rcv_ctx ar_resp_context;
	struct dma_rcv_ctx ar_req_context;

	/* async transmit */
	struct dma_trm_ctx at_resp_context;
	struct dma_trm_ctx at_req_context;

        /* iso receive */
	int nb_iso_rcv_ctx;
	unsigned long ir_ctx_usage; /* use test_and_set_bit() for atomicity */
	unsigned long ir_multichannel_used; /* ditto */
        spinlock_t IR_channel_lock;

	/* iso receive (legacy API) */
	u64 ir_legacy_channels; /* note: this differs from ISO_channel_usage;
				   it only accounts for channels listened to
				   by the legacy API, so that we can know when
				   it is safe to free the legacy API context */

	struct dma_rcv_ctx ir_legacy_context;
	struct ohci1394_iso_tasklet ir_legacy_tasklet;

        /* iso transmit */
	int nb_iso_xmit_ctx;
	unsigned long it_ctx_usage; /* use test_and_set_bit() for atomicity */

	/* iso transmit (legacy API) */
	struct dma_trm_ctx it_legacy_context;
	struct ohci1394_iso_tasklet it_legacy_tasklet;

        u64 ISO_channel_usage;

        /* IEEE-1394 part follows */
        struct hpsb_host *host;

        int phyid, isroot;

        spinlock_t phy_reg_lock;
	spinlock_t event_lock;

	int self_id_errors;

	/* Tasklets for iso receive and transmit, used by video1394,
	 * amdtp and dv1394 */

	struct list_head iso_tasklet_list;
	spinlock_t iso_tasklet_list_lock;

	/* Swap the selfid buffer? */
	unsigned int selfid_swap:1;
	/* Some Apple chipset seem to swap incoming headers for us */
	unsigned int no_swap_incoming:1;

	/* Force extra paranoia checking on bus-reset handling */
	unsigned int check_busreset:1;
};

static inline int cross_bound(unsigned long addr, unsigned int size)
{
	if (size > PAGE_SIZE)
		return 1;

	if (addr >> PAGE_SHIFT != (addr + size - 1) >> PAGE_SHIFT)
		return 1;

	return 0;
}

/*
 * Register read and write helper functions.
 */
static inline void reg_write(const struct ti_ohci *ohci, int offset, u32 data)
{
        writel(data, ohci->registers + offset);
}

static inline u32 reg_read(const struct ti_ohci *ohci, int offset)
{
        return readl(ohci->registers + offset);
}


/* 2 KiloBytes of register space */
#define OHCI1394_REGISTER_SIZE                0x800

/* Offsets relative to context bases defined below */

#define OHCI1394_ContextControlSet            0x000
#define OHCI1394_ContextControlClear          0x004
#define OHCI1394_ContextCommandPtr            0x00C

/* register map */
#define OHCI1394_Version                      0x000
#define OHCI1394_GUID_ROM                     0x004
#define OHCI1394_ATRetries                    0x008
#define OHCI1394_CSRData                      0x00C
#define OHCI1394_CSRCompareData               0x010
#define OHCI1394_CSRControl                   0x014
#define OHCI1394_ConfigROMhdr                 0x018
#define OHCI1394_BusID                        0x01C
#define OHCI1394_BusOptions                   0x020
#define OHCI1394_GUIDHi                       0x024
#define OHCI1394_GUIDLo                       0x028
#define OHCI1394_ConfigROMmap                 0x034
#define OHCI1394_PostedWriteAddressLo         0x038
#define OHCI1394_PostedWriteAddressHi         0x03C
#define OHCI1394_VendorID                     0x040
#define OHCI1394_HCControlSet                 0x050
#define OHCI1394_HCControlClear               0x054
#define  OHCI1394_HCControl_noByteSwap		0x40000000
#define  OHCI1394_HCControl_programPhyEnable	0x00800000
#define  OHCI1394_HCControl_aPhyEnhanceEnable	0x00400000
#define  OHCI1394_HCControl_LPS			0x00080000
#define  OHCI1394_HCControl_postedWriteEnable	0x00040000
#define  OHCI1394_HCControl_linkEnable		0x00020000
#define  OHCI1394_HCControl_softReset		0x00010000
#define OHCI1394_SelfIDBuffer                 0x064
#define OHCI1394_SelfIDCount                  0x068
#define OHCI1394_IRMultiChanMaskHiSet         0x070
#define OHCI1394_IRMultiChanMaskHiClear       0x074
#define OHCI1394_IRMultiChanMaskLoSet         0x078
#define OHCI1394_IRMultiChanMaskLoClear       0x07C
#define OHCI1394_IntEventSet                  0x080
#define OHCI1394_IntEventClear                0x084
#define OHCI1394_IntMaskSet                   0x088
#define OHCI1394_IntMaskClear                 0x08C
#define OHCI1394_IsoXmitIntEventSet           0x090
#define OHCI1394_IsoXmitIntEventClear         0x094
#define OHCI1394_IsoXmitIntMaskSet            0x098
#define OHCI1394_IsoXmitIntMaskClear          0x09C
#define OHCI1394_IsoRecvIntEventSet           0x0A0
#define OHCI1394_IsoRecvIntEventClear         0x0A4
#define OHCI1394_IsoRecvIntMaskSet            0x0A8
#define OHCI1394_IsoRecvIntMaskClear          0x0AC
#define OHCI1394_InitialBandwidthAvailable    0x0B0
#define OHCI1394_InitialChannelsAvailableHi   0x0B4
#define OHCI1394_InitialChannelsAvailableLo   0x0B8
#define OHCI1394_FairnessControl              0x0DC
#define OHCI1394_LinkControlSet               0x0E0
#define OHCI1394_LinkControlClear             0x0E4
#define  OHCI1394_LinkControl_RcvSelfID		0x00000200
#define  OHCI1394_LinkControl_RcvPhyPkt		0x00000400
#define  OHCI1394_LinkControl_CycleTimerEnable	0x00100000
#define  OHCI1394_LinkControl_CycleMaster	0x00200000
#define  OHCI1394_LinkControl_CycleSource	0x00400000
#define OHCI1394_NodeID                       0x0E8
#define OHCI1394_PhyControl                   0x0EC
#define OHCI1394_IsochronousCycleTimer        0x0F0
#define OHCI1394_AsReqFilterHiSet             0x100
#define OHCI1394_AsReqFilterHiClear           0x104
#define OHCI1394_AsReqFilterLoSet             0x108
#define OHCI1394_AsReqFilterLoClear           0x10C
#define OHCI1394_PhyReqFilterHiSet            0x110
#define OHCI1394_PhyReqFilterHiClear          0x114
#define OHCI1394_PhyReqFilterLoSet            0x118
#define OHCI1394_PhyReqFilterLoClear          0x11C
#define OHCI1394_PhyUpperBound                0x120

#define OHCI1394_AsReqTrContextBase           0x180
#define OHCI1394_AsReqTrContextControlSet     0x180
#define OHCI1394_AsReqTrContextControlClear   0x184
#define OHCI1394_AsReqTrCommandPtr            0x18C

#define OHCI1394_AsRspTrContextBase           0x1A0
#define OHCI1394_AsRspTrContextControlSet     0x1A0
#define OHCI1394_AsRspTrContextControlClear   0x1A4
#define OHCI1394_AsRspTrCommandPtr            0x1AC

#define OHCI1394_AsReqRcvContextBase          0x1C0
#define OHCI1394_AsReqRcvContextControlSet    0x1C0
#define OHCI1394_AsReqRcvContextControlClear  0x1C4
#define OHCI1394_AsReqRcvCommandPtr           0x1CC

#define OHCI1394_AsRspRcvContextBase          0x1E0
#define OHCI1394_AsRspRcvContextControlSet    0x1E0
#define OHCI1394_AsRspRcvContextControlClear  0x1E4
#define OHCI1394_AsRspRcvCommandPtr           0x1EC

/* Isochronous transmit registers */
/* Add (16 * n) for context n */
#define OHCI1394_IsoXmitContextBase           0x200
#define OHCI1394_IsoXmitContextControlSet     0x200
#define OHCI1394_IsoXmitContextControlClear   0x204
#define OHCI1394_IsoXmitCommandPtr            0x20C

/* Isochronous receive registers */
/* Add (32 * n) for context n */
#define OHCI1394_IsoRcvContextBase            0x400
#define OHCI1394_IsoRcvContextControlSet      0x400
#define OHCI1394_IsoRcvContextControlClear    0x404
#define OHCI1394_IsoRcvCommandPtr             0x40C
#define OHCI1394_IsoRcvContextMatch           0x410

/* Interrupts Mask/Events */

#define OHCI1394_reqTxComplete           0x00000001
#define OHCI1394_respTxComplete          0x00000002
#define OHCI1394_ARRQ                    0x00000004
#define OHCI1394_ARRS                    0x00000008
#define OHCI1394_RQPkt                   0x00000010
#define OHCI1394_RSPkt                   0x00000020
#define OHCI1394_isochTx                 0x00000040
#define OHCI1394_isochRx                 0x00000080
#define OHCI1394_postedWriteErr          0x00000100
#define OHCI1394_lockRespErr             0x00000200
#define OHCI1394_selfIDComplete          0x00010000
#define OHCI1394_busReset                0x00020000
#define OHCI1394_phy                     0x00080000
#define OHCI1394_cycleSynch              0x00100000
#define OHCI1394_cycle64Seconds          0x00200000
#define OHCI1394_cycleLost               0x00400000
#define OHCI1394_cycleInconsistent       0x00800000
#define OHCI1394_unrecoverableError      0x01000000
#define OHCI1394_cycleTooLong            0x02000000
#define OHCI1394_phyRegRcvd              0x04000000
#define OHCI1394_masterIntEnable         0x80000000

/* DMA Control flags */
#define DMA_CTL_OUTPUT_MORE              0x00000000
#define DMA_CTL_OUTPUT_LAST              0x10000000
#define DMA_CTL_INPUT_MORE               0x20000000
#define DMA_CTL_INPUT_LAST               0x30000000
#define DMA_CTL_UPDATE                   0x08000000
#define DMA_CTL_IMMEDIATE                0x02000000
#define DMA_CTL_IRQ                      0x00300000
#define DMA_CTL_BRANCH                   0x000c0000
#define DMA_CTL_WAIT                     0x00030000

/* OHCI evt_* error types, table 3-2 of the OHCI 1.1 spec. */
#define EVT_NO_STATUS		0x0	/* No event status */
#define EVT_RESERVED_A		0x1	/* Reserved, not used !!! */
#define EVT_LONG_PACKET		0x2	/* The revc data was longer than the buf */
#define EVT_MISSING_ACK		0x3	/* A subaction gap was detected before an ack
					   arrived, or recv'd ack had a parity error */
#define EVT_UNDERRUN		0x4	/* Underrun on corresponding FIFO, packet
					   truncated */
#define EVT_OVERRUN		0x5	/* A recv FIFO overflowed on reception of ISO
					   packet */
#define EVT_DESCRIPTOR_READ	0x6	/* An unrecoverable error occurred while host was
					   reading a descriptor block */
#define EVT_DATA_READ		0x7	/* An error occurred while host controller was
					   attempting to read from host memory in the data
					   stage of descriptor processing */
#define EVT_DATA_WRITE		0x8	/* An error occurred while host controller was
					   attempting to write either during the data stage
					   of descriptor processing, or when processing a single
					   16-bit host memory write */
#define EVT_BUS_RESET		0x9	/* Identifies a PHY packet in the recv buffer as
					   being a synthesized bus reset packet */
#define EVT_TIMEOUT		0xa	/* Indicates that the asynchronous transmit response
					   packet expired and was not transmitted, or that an
					   IT DMA context experienced a skip processing overflow */
#define EVT_TCODE_ERR		0xb	/* A bad tCode is associated with this packet.
					   The packet was flushed */
#define EVT_RESERVED_B		0xc	/* Reserved, not used !!! */
#define EVT_RESERVED_C		0xd	/* Reserved, not used !!! */
#define EVT_UNKNOWN		0xe	/* An error condition has occurred that cannot be
					   represented by any other event codes defined herein. */
#define EVT_FLUSHED		0xf	/* Send by the link side of output FIFO when asynchronous
					   packets are being flushed due to a bus reset. */

#define OHCI1394_TCODE_PHY               0xE

void ohci1394_init_iso_tasklet(struct ohci1394_iso_tasklet *tasklet,
			       int type,
			       void (*func)(unsigned long),
			       unsigned long data);
int ohci1394_register_iso_tasklet(struct ti_ohci *ohci,
				  struct ohci1394_iso_tasklet *tasklet);
void ohci1394_unregister_iso_tasklet(struct ti_ohci *ohci,
				     struct ohci1394_iso_tasklet *tasklet);

/* returns zero if successful, one if DMA context is locked up */
int ohci1394_stop_context      (struct ti_ohci *ohci, int reg, char *msg);
struct ti_ohci *ohci1394_get_struct(int card_num);

#endif
