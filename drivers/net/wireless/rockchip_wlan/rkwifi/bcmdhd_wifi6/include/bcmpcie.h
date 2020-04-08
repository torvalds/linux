/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom PCIE
 * Software-specific definitions shared between device and host side
 * Explains the shared area between host and dongle
 *
 * Copyright (C) 1999-2019, Broadcom.
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
 * $Id: bcmpcie.h 821465 2019-05-23 19:50:00Z $
 */

#ifndef	_bcmpcie_h_
#define	_bcmpcie_h_

#include <typedefs.h>

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

#define PCIE_SHARED_VERSION_7		0x00007
#define PCIE_SHARED_VERSION_6		0x00006 /* rev6 is compatible with rev 5 */
#define PCIE_SHARED_VERSION_5		0x00005 /* rev6 is compatible with rev 5 */
/**
 * Feature flags enabled in dongle. Advertised by dongle to DHD via the PCIe Shared structure that
 * is located in device memory.
 */
#define PCIE_SHARED_VERSION_MASK	0x000FF
#define PCIE_SHARED_ASSERT_BUILT	0x00100
#define PCIE_SHARED_ASSERT		0x00200
#define PCIE_SHARED_TRAP		0x00400
#define PCIE_SHARED_IN_BRPT		0x00800
#define PCIE_SHARED_SET_BRPT		0x01000
#define PCIE_SHARED_PENDING_BRPT	0x02000
/* BCMPCIE_SUPPORT_TX_PUSH_RING		0x04000 obsolete */
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

#define PCIE_SHARED_FAST_DELETE_RING	0x00000020      /* Fast Delete Ring */
#define PCIE_SHARED_EVENT_BUF_POOL_MAX	0x000000c0      /* event buffer pool max bits */
#define PCIE_SHARED_EVENT_BUF_POOL_MAX_POS     6       /* event buffer pool max bit position */

/* dongle supports fatal buf log collection */
#define PCIE_SHARED_FATAL_LOGBUG_VALID	0x200000

/* Implicit DMA with corerev 19 and after */
#define PCIE_SHARED_IDMA		0x400000

/* MSI support */
#define PCIE_SHARED_D2H_MSI_MULTI_MSG   0x800000

/* IFRM with corerev 19 and after */
#define PCIE_SHARED_IFRM		0x1000000

/**
 * From Rev6 and above, suspend/resume can be done using two handshake methods.
 * 1. Using ctrl post/ctrl cmpl messages (Default rev6)
 * 2. Using Mailbox data (old method as used in rev5)
 * This shared flag indicates whether to overide rev6 default method and use mailbox for
 * suspend/resume.
 */
#define PCIE_SHARED_USE_MAILBOX		0x2000000

/* Firmware compiled for mfgbuild purposes */
#define PCIE_SHARED_MFGBUILD_FW		0x4000000

/* Firmware could use DB0 value as host timestamp */
#define PCIE_SHARED_TIMESTAMP_DB0	0x8000000
/* Firmware could use Hostready (IPC rev7) */
#define PCIE_SHARED_HOSTRDY_SUPPORT	0x10000000

/* When set, Firmwar does not support OOB Device Wake based DS protocol */
#define PCIE_SHARED_NO_OOB_DW		0x20000000

/* When set, Firmwar supports Inband DS protocol */
#define PCIE_SHARED_INBAND_DS		0x40000000

/* use DAR registers */
#define PCIE_SHARED_DAR			0x80000000

/**
 * Following are the shared2 flags. All bits in flags have been used. A flags2
 * field got added and the definition for these flags come here:
 */
/* WAR: D11 txstatus through unused status field of PCIe completion header */
#define PCIE_SHARED2_EXTENDED_TRAP_DATA	0x00000001	/* using flags2 in shared area */
#define PCIE_SHARED2_TXSTATUS_METADATA	0x00000002
#define PCIE_SHARED2_BT_LOGGING		0x00000004	/* BT logging support */
#define PCIE_SHARED2_SNAPSHOT_UPLOAD	0x00000008	/* BT/WLAN snapshot upload support */
#define PCIE_SHARED2_SUBMIT_COUNT_WAR	0x00000010	/* submission count WAR */
#define PCIE_SHARED2_FAST_DELETE_RING	0x00000020	/* Fast Delete ring support */
#define PCIE_SHARED2_EVTBUF_MAX_MASK	0x000000C0	/* 0:32, 1:64, 2:128, 3: 256 */

/* using flags2 to indicate firmware support added to reuse timesync to update PKT txstatus */
#define PCIE_SHARED2_PKT_TX_STATUS	0x00000100
#define PCIE_SHARED2_FW_SMALL_MEMDUMP	0x00000200	/* FW small memdump */
#define PCIE_SHARED2_FW_HC_ON_TRAP	0x00000400
#define PCIE_SHARED2_HSCB		0x00000800	/* Host SCB support */

#define PCIE_SHARED2_EDL_RING			0x00001000	/* Support Enhanced Debug Lane */
#define PCIE_SHARED2_DEBUG_BUF_DEST		0x00002000	/* debug buf dest support */
#define PCIE_SHARED2_PCIE_ENUM_RESET_FLR	0x00004000	/* BT producer index reset WAR */
#define PCIE_SHARED2_PKT_TIMESTAMP		0x00008000	/* Timestamp in packet */

#define PCIE_SHARED2_HP2P		0x00010000u	/* HP2P feature */
#define PCIE_SHARED2_HWA		0x00020000u	/* HWA feature */
#define PCIE_SHARED2_TRAP_ON_HOST_DB7	0x00040000u	/* can take a trap on DB7 from host */

#define PCIE_SHARED2_DURATION_SCALE	0x00100000u

#define PCIE_SHARED2_D2H_D11_TX_STATUS	0x40000000
#define PCIE_SHARED2_H2D_D11_TX_STATUS	0x80000000

#define PCIE_SHARED_D2H_MAGIC		0xFEDCBA09
#define PCIE_SHARED_H2D_MAGIC		0x12345678

typedef uint16			pcie_hwa_db_index_t;	/* 16 bit HWA index (IPC Rev 7) */
#define PCIE_HWA_DB_INDEX_SZ	(2u)			/* 2 bytes  sizeof(pcie_hwa_db_index_t) */

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

/* different ring types */
#define BCMPCIE_H2D_RING_TYPE_CTRL_SUBMIT		0x1
#define BCMPCIE_H2D_RING_TYPE_TXFLOW_RING		0x2
#define BCMPCIE_H2D_RING_TYPE_RXBUFPOST			0x3
#define BCMPCIE_H2D_RING_TYPE_TXSUBMIT			0x4
#define BCMPCIE_H2D_RING_TYPE_DBGBUF_SUBMIT		0x5
#define BCMPCIE_H2D_RING_TYPE_BTLOG_SUBMIT		0x6

#define BCMPCIE_D2H_RING_TYPE_CTRL_CPL			0x1
#define BCMPCIE_D2H_RING_TYPE_TX_CPL			0x2
#define BCMPCIE_D2H_RING_TYPE_RX_CPL			0x3
#define BCMPCIE_D2H_RING_TYPE_DBGBUF_CPL		0x4
#define BCMPCIE_D2H_RING_TYPE_AC_RX_COMPLETE		0x5
#define BCMPCIE_D2H_RING_TYPE_BTLOG_CPL			0x6
#define BCMPCIE_D2H_RING_TYPE_EDL                       0x7
#define BCMPCIE_D2H_RING_TYPE_HPP_TX_CPL		0x8
#define BCMPCIE_D2H_RING_TYPE_HPP_RX_CPL		0x9

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
 * Per flow ring, information is maintained in device memory, eg at what address the ringmem and
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

	uint16		max_tx_flowrings; /* maximum number of H2D rings: common + flow */
	uint16		max_submission_queues; /* maximum number of H2D rings: common + flow */
	uint16		max_completion_rings; /* maximum number of H2D rings: common + flow */
	uint16		max_vdevs; /* max number of virtual interfaces supported */

	sh_addr_t	ifrm_w_idx_hostaddr; /* Array of all H2D ring's WR indices for IFRM */

	/* 32bit ptr to arrays of HWA DB indices for all rings in dongle memory */
	uint32		h2d_hwa_db_idx_ptr; /* Array of all H2D ring's HWA DB indices */
	uint32		d2h_hwa_db_idx_ptr; /* Array of all D2H ring's HWA DB indices */

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

	/* location in host memory for scb host offload structures */
	sh_addr_t	host_scb_addr;
	uint32		host_scb_size;

	/* anonymous union for overloading fields in structure */
	union {
		uint32	buzz_dbg_ptr;	/* BUZZZ state format strings and trace buffer */
		struct {
			/* Host provided trap buffer length in words */
			uint16	device_trap_debug_buffer_len;
			uint16	rsvd2;
		};
	};

	/* rev6 compatible changes */
	uint32          flags2;
	uint32          host_cap;

	/* location in the host address space to write trap indication.
	* At this point for the current rev of the spec, firmware will
	* support only indications to 32 bit host addresses.
	* This essentially is device_trap_debug_buffer_addr
	*/
	sh_addr_t       host_trap_addr;

	/* location for host fatal error log buffer start address */
	uint32		device_fatal_logbuf_start;

	/* location in host memory for offloaded modules */
	sh_addr_t	hoffload_addr;
	uint32		flags3;
	uint32		host_cap2;
	uint32		host_cap3;
} pciedev_shared_t;

/* Device F/W provides the following access function:
 * pciedev_shared_t *hnd_get_pciedev_shared(void);
 */

/* host capabilities */
#define HOSTCAP_PCIEAPI_VERSION_MASK		0x000000FF
#define HOSTCAP_H2D_VALID_PHASE			0x00000100
#define HOSTCAP_H2D_ENABLE_TRAP_ON_BADPHASE	0x00000200
#define HOSTCAP_H2D_ENABLE_HOSTRDY		0x00000400
#define HOSTCAP_DB0_TIMESTAMP			0x00000800
#define HOSTCAP_DS_NO_OOB_DW			0x00001000
#define HOSTCAP_DS_INBAND_DW			0x00002000
#define HOSTCAP_H2D_IDMA			0x00004000
#define HOSTCAP_H2D_IFRM			0x00008000
#define HOSTCAP_H2D_DAR				0x00010000
#define HOSTCAP_EXTENDED_TRAP_DATA		0x00020000
#define HOSTCAP_TXSTATUS_METADATA		0x00040000
#define HOSTCAP_BT_LOGGING			0x00080000
#define HOSTCAP_SNAPSHOT_UPLOAD			0x00100000
#define HOSTCAP_FAST_DELETE_RING		0x00200000
#define HOSTCAP_PKT_TXSTATUS			0x00400000
#define HOSTCAP_UR_FW_NO_TRAP			0x00800000 /* Don't trap on UR */
#define HOSTCAP_HSCB				0x02000000
/* Host support for extended device trap debug buffer */
#define HOSTCAP_EXT_TRAP_DBGBUF			0x04000000
/* Host support for enhanced debug lane */
#define HOSTCAP_EDL_RING			0x10000000
#define HOSTCAP_PKT_TIMESTAMP			0x20000000
#define HOSTCAP_PKT_HP2P			0x40000000
#define HOSTCAP_HWA				0x80000000
#define HOSTCAP2_DURATION_SCALE_MASK            0x0000003Fu

/* extended trap debug buffer allocation sizes. Note that this buffer can be used for
 * other trap related purposes also.
 */
#define BCMPCIE_HOST_EXT_TRAP_DBGBUF_LEN_MIN	(64u * 1024u)
#define BCMPCIE_HOST_EXT_TRAP_DBGBUF_LEN_MAX	(256u * 1024u)

/**
 * Mailboxes notify a remote party that an event took place, using interrupts. They use hardware
 * support.
 */

/* H2D mail box Data */
#define H2D_HOST_D3_INFORM		0x00000001
#define H2D_HOST_DS_ACK		0x00000002
#define H2D_HOST_DS_NAK		0x00000004
#define H2D_HOST_D0_INFORM_IN_USE	0x00000008
#define H2D_HOST_D0_INFORM		0x00000010
#define H2DMB_DS_ACTIVE			0x00000020
#define H2DMB_DS_DEVICE_WAKE	0x00000040
#define H2D_HOST_IDMA_INITED	0x00000080
#define H2D_HOST_ACK_NOINT		0x00010000 /* d2h_ack interrupt ignore */
#define H2D_HOST_CONS_INT	0x80000000	/**< h2d int for console cmds  */
#define H2D_FW_TRAP		0x20000000	/**< h2d force TRAP */
#define H2DMB_DS_HOST_SLEEP_INFORM H2D_HOST_D3_INFORM
#define H2DMB_DS_DEVICE_SLEEP_ACK  H2D_HOST_DS_ACK
#define H2DMB_DS_DEVICE_SLEEP_NAK  H2D_HOST_DS_NAK
#define H2DMB_D0_INFORM_IN_USE     H2D_HOST_D0_INFORM_IN_USE
#define H2DMB_D0_INFORM            H2D_HOST_D0_INFORM
#define H2DMB_FW_TRAP              H2D_FW_TRAP
#define H2DMB_HOST_CONS_INT        H2D_HOST_CONS_INT
#define H2DMB_DS_DEVICE_WAKE_ASSERT		H2DMB_DS_DEVICE_WAKE
#define H2DMB_DS_DEVICE_WAKE_DEASSERT	H2DMB_DS_ACTIVE

/* D2H mail box Data */
#define D2H_DEV_D3_ACK					0x00000001
#define D2H_DEV_DS_ENTER_REQ				0x00000002
#define D2H_DEV_DS_EXIT_NOTE				0x00000004
#define D2HMB_DS_HOST_SLEEP_EXIT_ACK			0x00000008
#define D2H_DEV_IDMA_INITED				0x00000010
#define D2HMB_DS_HOST_SLEEP_ACK         D2H_DEV_D3_ACK
#define D2HMB_DS_DEVICE_SLEEP_ENTER_REQ D2H_DEV_DS_ENTER_REQ
#define D2HMB_DS_DEVICE_SLEEP_EXIT      D2H_DEV_DS_EXIT_NOTE

#define D2H_DEV_MB_MASK		(D2H_DEV_D3_ACK | D2H_DEV_DS_ENTER_REQ | \
				D2H_DEV_DS_EXIT_NOTE | D2H_DEV_IDMA_INITED)
#define D2H_DEV_MB_INVALIDATED(x)	((!x) || (x & ~D2H_DEV_MB_MASK))

/* trap data codes */
#define D2H_DEV_FWHALT					0x10000000
#define D2H_DEV_EXT_TRAP_DATA				0x20000000
#define D2H_DEV_TRAP_IN_TRAP				0x40000000
#define D2H_DEV_TRAP_HOSTDB				0x80000000 /* trap as set by host DB */
#define D2H_DEV_TRAP_DUE_TO_BT				0x01000000
/* Indicates trap due to HMAP violation */
#define D2H_DEV_TRAP_DUE_TO_HMAP			0x02000000
/* Indicates whether HMAP violation was Write */
#define D2H_DEV_TRAP_HMAP_WRITE				0x04000000
#define D2H_DEV_TRAP_PING_HOST_FAILURE			0x08000000
#define D2H_FWTRAP_MASK		0x0000001F	/* Adding maskbits for TRAP information */

#define D2HMB_FWHALT                    D2H_DEV_FWHALT
#define D2HMB_TRAP_IN_TRAP              D2H_DEV_TRAP_IN_TRAP
#define D2HMB_EXT_TRAP_DATA             D2H_DEV_EXT_TRAP_DATA

/* Size of Extended Trap data Buffer */
#define BCMPCIE_EXT_TRAP_DATA_MAXLEN  4096

/** These macro's operate on type 'inuse_lclbuf_pool_t' and are used by firmware only */
#define PREVTXP(i, d)           (((i) == 0) ? ((d) - 1) : ((i) - 1))
#define NEXTTXP(i, d)           ((((i)+1) >= (d)) ? 0 : ((i)+1))
#define NEXTNTXP(i, n, d)       ((((i)+(n)) >= (d)) ? 0 : ((i)+(n)))
#define NTXPACTIVE(r, w, d)     (((r) <= (w)) ? ((w)-(r)) : ((d)-(r)+(w)))
#define NTXPAVAIL(r, w, d)      (((d) - NTXPACTIVE((r), (w), (d))) > 1)

/* Function can be used to notify host of FW halt */
#define READ_AVAIL_SPACE(w, r, d) ((w >= r) ? (uint32)(w - r) : (uint32)(d - r))
#define WRITE_SPACE_AVAIL_CONTINUOUS(r, w, d) ((w >= r) ? (d - w) : (r - w))
#define WRITE_SPACE_AVAIL(r, w, d) (d - (NTXPACTIVE(r, w, d)) - 1)
#define CHECK_WRITE_SPACE(r, w, d) ((r) > (w)) ? \
	(uint32)((r) - (w) - 1) : ((r) == 0 || (w) == 0) ? \
	(uint32)((d) - (w) - 1) : (uint32)((d) - (w))

#define CHECK_NOWRITE_SPACE(r, w, d) \
	(((uint32)(r) == (uint32)((w) + 1)) || (((r) == 0) && ((w) == ((d) - 1))))

#define WRT_PEND(x)	((x)->wr_pending)
#define DNGL_RING_WPTR(msgbuf)		(*((msgbuf)->tcm_rs_w_ptr)) /**< advanced by producer */
#define BCMMSGBUF_RING_SET_W_PTR(msgbuf, a)	(DNGL_RING_WPTR(msgbuf) = (a))

#define DNGL_RING_RPTR(msgbuf)		(*((msgbuf)->tcm_rs_r_ptr)) /**< advanced by consumer */
#define BCMMSGBUF_RING_SET_R_PTR(msgbuf, a)	(DNGL_RING_RPTR(msgbuf) = (a))

#define MODULO_RING_IDX(x, y)	((x) % (y)->bitmap_size)

#define  RING_READ_PTR(x)	((x)->ringstate->r_offset)
#define  RING_WRITE_PTR(x)	((x)->ringstate->w_offset)
#define  RING_START_PTR(x)	((x)->ringmem->base_addr.low_addr)
#define  RING_MAX_ITEM(x)	((x)->ringmem->max_item)
#define  RING_LEN_ITEMS(x)	((x)->ringmem->len_items)
#define	 HOST_RING_BASE(x)	((x)->dma_buf.va)
#define	 HOST_RING_END(x)	((uint8 *)HOST_RING_BASE((x)) + \
					((RING_MAX_ITEM((x))-1)*RING_LEN_ITEMS((x))))

/* Trap types copied in the pciedev_shared.trap_addr */
#define	FW_INITIATED_TRAP_TYPE	(0x1 << 7)
#define	HEALTHCHECK_NODS_TRAP_TYPE	(0x1 << 6)

#endif	/* _bcmpcie_h_ */
