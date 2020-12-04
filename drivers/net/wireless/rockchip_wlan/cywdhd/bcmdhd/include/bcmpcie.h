/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom PCIE
 * Software-specific definitions shared between device and host side
 * Explains the shared area between host and dongle
 *
 * Copyright (C) 1999-2019, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcmpcie.h 542048 2015-03-18 15:37:26Z $
 */


#ifndef	_bcmpcie_h_
#define	_bcmpcie_h_

#include <bcmutils.h>

#define ADDR_64(x)			(x.addr)
#define HIGH_ADDR_32(x)     ((uint32) (((sh_addr_t) x).high_addr))
#define LOW_ADDR_32(x)      ((uint32) (((sh_addr_t) x).low_addr))

typedef struct {
	uint32 low_addr;
	uint32 high_addr;
} sh_addr_t;


/* May be overridden by 43xxxxx-roml.mk */
#if !defined(BCMPCIE_MAX_TX_FLOWS)
#define BCMPCIE_MAX_TX_FLOWS	40
#endif /* ! BCMPCIE_MAX_TX_FLOWS */

/**
 * Feature flags enabled in dongle. Advertised by dongle to DHD via the PCIe Shared structure that
 * is located in device memory.
 */
#define PCIE_SHARED_VERSION		0x00005
#define PCIE_SHARED_VERSION_MASK	0x000FF
#define PCIE_SHARED_ASSERT_BUILT	0x00100
#define PCIE_SHARED_ASSERT		0x00200
#define PCIE_SHARED_TRAP		0x00400
#define PCIE_SHARED_IN_BRPT		0x00800
#define PCIE_SHARED_SET_BRPT		0x01000
#define PCIE_SHARED_PENDING_BRPT	0x02000
#define PCIE_SHARED_TXPUSH_SPRT		0x04000
#define PCIE_SHARED_EVT_SEQNUM		0x08000
#define PCIE_SHARED_DMA_INDEX		0x10000

/**
 * There are host types where a device interrupt can 'race ahead' of data written by the device into
 * host memory. The dongle can avoid this condition using a variety of techniques (read barrier,
 * using PCIe Message Signalled Interrupts, or by using the PCIE_DMA_INDEX feature). Unfortunately
 * these techniques have drawbacks on router platforms. For these platforms, it was decided to not
 * avoid the condition, but to detect the condition instead and act on it.
 * D2H M2M DMA Complete Sync mechanism: Modulo-253-SeqNum or XORCSUM
 */
#define PCIE_SHARED_D2H_SYNC_SEQNUM     0x20000
#define PCIE_SHARED_D2H_SYNC_XORCSUM    0x40000
#define PCIE_SHARED_D2H_SYNC_MODE_MASK \
	(PCIE_SHARED_D2H_SYNC_SEQNUM | PCIE_SHARED_D2H_SYNC_XORCSUM)
#define PCIE_SHARED_IDLE_FLOW_RING		0x80000
#define PCIE_SHARED_2BYTE_INDICES       0x100000


#define PCIE_SHARED_D2H_MAGIC		0xFEDCBA09
#define PCIE_SHARED_H2D_MAGIC		0x12345678

/**
 * Message rings convey messages between host and device. They are unidirectional, and are located
 * in host memory.
 *
 * This is the minimal set of message rings, known as 'common message rings':
 */
#define BCMPCIE_H2D_MSGRING_CONTROL_SUBMIT		0
#define BCMPCIE_H2D_MSGRING_RXPOST_SUBMIT		1
#define BCMPCIE_D2H_MSGRING_CONTROL_COMPLETE		2
#define BCMPCIE_D2H_MSGRING_TX_COMPLETE			3
#define BCMPCIE_D2H_MSGRING_RX_COMPLETE			4
#define BCMPCIE_COMMON_MSGRING_MAX_ID			4

#define BCMPCIE_H2D_COMMON_MSGRINGS			2
#define BCMPCIE_D2H_COMMON_MSGRINGS			3
#define BCMPCIE_COMMON_MSGRINGS				5

#define BCMPCIE_H2D_MSGRINGS(max_tx_flows) \
	(BCMPCIE_H2D_COMMON_MSGRINGS + (max_tx_flows))

/**
 * H2D and D2H, WR and RD index, are maintained in the following arrays:
 * - Array of all H2D WR Indices
 * - Array of all H2D RD Indices
 * - Array of all D2H WR Indices
 * - Array of all D2H RD Indices
 *
 * The offset of the WR or RD indexes (for common rings) in these arrays are
 * listed below. Arrays ARE NOT indexed by a ring's id.
 *
 * D2H common rings WR and RD index start from 0, even though their ringids
 * start from BCMPCIE_H2D_COMMON_MSGRINGS
 */

#define BCMPCIE_H2D_RING_IDX(h2d_ring_id) (h2d_ring_id)

enum h2dring_idx {
	/* H2D common rings */
	BCMPCIE_H2D_MSGRING_CONTROL_SUBMIT_IDX =
		BCMPCIE_H2D_RING_IDX(BCMPCIE_H2D_MSGRING_CONTROL_SUBMIT),
	BCMPCIE_H2D_MSGRING_RXPOST_SUBMIT_IDX =
		BCMPCIE_H2D_RING_IDX(BCMPCIE_H2D_MSGRING_RXPOST_SUBMIT),

	/* First TxPost's WR or RD index starts after all H2D common rings */
	BCMPCIE_H2D_MSGRING_TXFLOW_IDX_START =
		BCMPCIE_H2D_RING_IDX(BCMPCIE_H2D_COMMON_MSGRINGS)
};

#define BCMPCIE_D2H_RING_IDX(d2h_ring_id) \
	((d2h_ring_id) - BCMPCIE_H2D_COMMON_MSGRINGS)

enum d2hring_idx {
	/* D2H Common Rings */
	BCMPCIE_D2H_MSGRING_CONTROL_COMPLETE_IDX =
		BCMPCIE_D2H_RING_IDX(BCMPCIE_D2H_MSGRING_CONTROL_COMPLETE),
	BCMPCIE_D2H_MSGRING_TX_COMPLETE_IDX =
		BCMPCIE_D2H_RING_IDX(BCMPCIE_D2H_MSGRING_TX_COMPLETE),
	BCMPCIE_D2H_MSGRING_RX_COMPLETE_IDX =
		BCMPCIE_D2H_RING_IDX(BCMPCIE_D2H_MSGRING_RX_COMPLETE)
};

/**
 * Macros for managing arrays of RD WR indices:
 * rw_index_sz:
 *    - in dongle, rw_index_sz is known at compile time
 *    - in host/DHD, rw_index_sz is derived from advertized pci_shared flags
 *
 *  ring_idx: See h2dring_idx and d2hring_idx
 */

/** Offset of a RD or WR index in H2D or D2H indices array */
#define BCMPCIE_RW_INDEX_OFFSET(rw_index_sz, ring_idx) \
	((rw_index_sz) * (ring_idx))

/** Fetch the address of RD or WR index in H2D or D2H indices array */
#define BCMPCIE_RW_INDEX_ADDR(indices_array_base, rw_index_sz, ring_idx) \
	(void *)((uint32)(indices_array_base) + \
	BCMPCIE_RW_INDEX_OFFSET((rw_index_sz), (ring_idx)))

/** H2D DMA Indices array size: given max flow rings */
#define BCMPCIE_H2D_RW_INDEX_ARRAY_SZ(rw_index_sz, max_tx_flows) \
	((rw_index_sz) * BCMPCIE_H2D_MSGRINGS(max_tx_flows))

/** D2H DMA Indices array size */
#define BCMPCIE_D2H_RW_INDEX_ARRAY_SZ(rw_index_sz) \
	((rw_index_sz) * BCMPCIE_D2H_COMMON_MSGRINGS)

/**
 * This type is used by a 'message buffer' (which is a FIFO for messages). Message buffers are used
 * for host<->device communication and are instantiated on both sides. ring_mem_t is instantiated
 * both in host as well as device memory.
 */
typedef struct ring_mem {
	uint16		idx;       /* ring id */
	uint8		type;
	uint8		rsvd;
	uint16		max_item;  /* Max number of items in flow ring */
	uint16		len_items; /* Items are fixed size. Length in bytes of one item */
	sh_addr_t	base_addr; /* 64 bits address, either in host or device memory */
} ring_mem_t;


/**
 * Per flow ring, information is maintained in device memory, e.g. at what address the ringmem and
 * ringstate are located. The flow ring itself can be instantiated in either host or device memory.
 *
 * Perhaps this type should be renamed to make clear that it resides in device memory only.
 */
typedef struct ring_info {
	uint32		ringmem_ptr; /* ring mem location in dongle memory */

	/* Following arrays are indexed using h2dring_idx and d2hring_idx, and not
	 * by a ringid.
	 */

	/* 32bit ptr to arrays of WR or RD indices for all rings in dongle memory */
	uint32		h2d_w_idx_ptr; /* Array of all H2D ring's WR indices */
	uint32		h2d_r_idx_ptr; /* Array of all H2D ring's RD indices */
	uint32		d2h_w_idx_ptr; /* Array of all D2H ring's WR indices */
	uint32		d2h_r_idx_ptr; /* Array of all D2H ring's RD indices */

	/* PCIE_DMA_INDEX feature: Dongle uses mem2mem DMA to sync arrays in host.
	 * Host may directly fetch WR and RD indices from these host-side arrays.
	 *
	 * 64bit ptr to arrays of WR or RD indices for all rings in host memory.
	 */
	sh_addr_t	h2d_w_idx_hostaddr; /* Array of all H2D ring's WR indices */
	sh_addr_t	h2d_r_idx_hostaddr; /* Array of all H2D ring's RD indices */
	sh_addr_t	d2h_w_idx_hostaddr; /* Array of all D2H ring's WR indices */
	sh_addr_t	d2h_r_idx_hostaddr; /* Array of all D2H ring's RD indices */

	uint16		max_sub_queues; /* maximum number of H2D rings: common + flow */
	uint16		rsvd;
} ring_info_t;

/**
 * A structure located in TCM that is shared between host and device, primarily used during
 * initialization.
 */
typedef struct {
	/** shared area version captured at flags 7:0 */
	uint32	flags;

	uint32  trap_addr;
	uint32  assert_exp_addr;
	uint32  assert_file_addr;
	uint32  assert_line;
	uint32	console_addr;		/**< Address of hnd_cons_t */

	uint32  msgtrace_addr;

	uint32  fwid;

	/* Used for debug/flow control */
	uint16  total_lfrag_pkt_cnt;
	uint16  max_host_rxbufs; /* rsvd in spec */

	uint32 dma_rxoffset; /* rsvd in spec */

	/** these will be used for sleep request/ack, d3 req/ack */
	uint32  h2d_mb_data_ptr;
	uint32  d2h_mb_data_ptr;

	/* information pertinent to host IPC/msgbuf channels */
	/** location in the TCM memory which has the ring_info */
	uint32	rings_info_ptr;

	/** block of host memory for the scratch buffer */
	uint32		host_dma_scratch_buffer_len;
	sh_addr_t	host_dma_scratch_buffer;

	/** block of host memory for the dongle to push the status into */
	uint32		device_rings_stsblk_len;
	sh_addr_t	device_rings_stsblk;

	uint32	buzzz;	/* BUZZZ state format strings and trace buffer */

} pciedev_shared_t;

extern pciedev_shared_t pciedev_shared;

/**
 * Mailboxes notify a remote party that an event took place, using interrupts. They use hardware
 * support.
 */

/* H2D mail box Data */
#define H2D_HOST_D3_INFORM	0x00000001
#define H2D_HOST_DS_ACK		0x00000002
#define H2D_HOST_DS_NAK		0x00000004
#define H2D_HOST_CONS_INT	0x80000000	/**< h2d int for console cmds  */
#define H2D_FW_TRAP		0x20000000	/**< h2d force TRAP */
#define H2D_HOST_D0_INFORM_IN_USE	0x00000008
#define H2D_HOST_D0_INFORM	0x00000010

/* D2H mail box Data */
#define D2H_DEV_D3_ACK		0x00000001
#define D2H_DEV_DS_ENTER_REQ	0x00000002
#define D2H_DEV_DS_EXIT_NOTE	0x00000004
#define D2H_DEV_FWHALT		0x10000000
#define D2H_DEV_MB_MASK		(D2H_DEV_D3_ACK | D2H_DEV_DS_ENTER_REQ | \
				D2H_DEV_DS_EXIT_NOTE | D2H_DEV_FWHALT)
#define D2H_DEV_MB_INVALIDATED(x)	((!x) || (x & ~D2H_DEV_MB_MASK))

/** These macro's operate on type 'inuse_lclbuf_pool_t' and are used by firmware only */
#define NEXTTXP(i, d)           ((((i)+1) >= (d)) ? 0 : ((i)+1))
#define NTXPACTIVE(r, w, d)     (((r) <= (w)) ? ((w)-(r)) : ((d)-(r)+(w)))
#define NTXPAVAIL(r, w, d)      (((d) - NTXPACTIVE((r), (w), (d))) > 1)

/* Function can be used to notify host of FW halt */
#define READ_AVAIL_SPACE(w, r, d)		\
			((w >= r) ? (w - r) : (d - r))

#define WRITE_SPACE_AVAIL_CONTINUOUS(r, w, d)		((w >= r) ? (d - w) : (r - w))
#define WRITE_SPACE_AVAIL(r, w, d)	(d - (NTXPACTIVE(r, w, d)) - 1)
#define CHECK_WRITE_SPACE(r, w, d)	\
		MIN(WRITE_SPACE_AVAIL(r, w, d), WRITE_SPACE_AVAIL_CONTINUOUS(r, w, d))


#define WRT_PEND(x)	((x)->wr_pending)
#define DNGL_RING_WPTR(msgbuf)		(*((msgbuf)->tcm_rs_w_ptr))
#define BCMMSGBUF_RING_SET_W_PTR(msgbuf, a)	(DNGL_RING_WPTR(msgbuf) = (a))

#define DNGL_RING_RPTR(msgbuf)		(*((msgbuf)->tcm_rs_r_ptr))
#define BCMMSGBUF_RING_SET_R_PTR(msgbuf, a)	(DNGL_RING_RPTR(msgbuf) = (a))

#define RING_START_PTR(x)	((x)->ringmem->base_addr.low_addr)
#define RING_MAX_ITEM(x)	((x)->ringmem->max_item)
#define RING_LEN_ITEMS(x)	((x)->ringmem->len_items)

#endif	/* _bcmpcie_h_ */
