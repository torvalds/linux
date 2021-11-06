
/*
 * Firmware trace implementation common header file between DHD and the firmware.
 *
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2020,
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties,
 * copied or duplicated in any form, in whole or in part, without
 * the prior written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Secret:>>
 */

#ifndef _bcm_fwtrace_h
#define _bcm_fwtrace_h

#include <bcmpcie.h>
#include <bcmmsgbuf.h>

#define FWTRACE_VERSION		1u

/*
 * Number of trace entries per trace buffer.
 * Both DHD and FW must use same number.
 */
#define FWTRACE_NUM_ENTRIES		(2u * 1024u) /* 2KB, power of 2 */
/*
 * Number of buffers provided by the host.
 * DHD may allocate smaller number of trace buffers based on continuous memory availability.
 */
#define FWTRACE_NUM_HOST_BUFFERS	32u

/* Magic value to differentiate between regural trace data Vs other blobs */
#define FWTRACE_BLOB_MAGIC              (0xFFu)
#define FWTRACE_BLOB_MGIC_SHIFT         (24u)

/* The lower 24 bits of the fwtrace_entry->func_ptr is used to push different type of
 * information to host such as ACK bitmap, interrupts DPC is going to process etc.
 */
#define FWTRACE_BLOB_TYPE_MASK          (0xFFFFFFu)
#define FWTRACE_BLOB_TYPE_SHIFT         (0)

#define FWTRACE_BLOB_TYPE_NUM_PKTS         (0x1u)
#define FWTRACE_BLOB_TYPE_ACK_BMAP1        (0x2u)  /* Ack bits (0-31 )   */
#define FWTRACE_BLOB_TYPE_ACK_BMAP2        (0x4u)  /* Ack bits (32-63)   */
#define FWTRACE_BLOB_TYPE_ACK_BMAP3        (0x8u)  /* Ack bits (64-95)   */
#define FWTRACE_BLOB_TYPE_ACK_BMAP4        (0x10u) /* Ack bits (96-127)  */
#define FWTRACE_BLOB_TYPE_INTR1            (0x20u) /* interrupts the DPC is going to process */
#define FWTRACE_BLOB_TYPE_INTR2            (0x40u) /* interrupts the DPC is going to process */
/* The blob data for LFRAGS_INFO will contain
 * Bit31-16: Available buffer/lfrags info
 * Bit15-0 : # of lfrags requested by FW in the fetch request
 */
#define FWTRACE_BLOB_TYPE_LFRAGS_INFO      (0x80u) /* Available and fetch requested lfrags */

#define FWTRACE_BLOB_DATA_MASK             (0xFFFFFu)

#define FWTRACE_BLOB_ADD_CUR       (0)  /* updates with in the existing trace entry */
#define FWTRACE_BLOB_ADD_NEW       (1u) /* Creates new trace entry */

/*
 * Host sends host memory location to FW via iovar.
 * FW will push trace information here.
 */
typedef struct fwtrace_hostaddr_info {
	bcm_addr64_t haddr;	/* host address for the firmware to DMA trace data */
	uint32       buf_len;
	uint32       num_bufs;	/* Number of trace buffers */
} fwtrace_hostaddr_info_t;

/*
 * Eact trace info buffer pushed to host will have this header.
 */
typedef struct fwtrace_dma_header_info {
	uint16 length;	/* length in bytes */
	uint16 seq_num;	/* sequence number */
	uint32 version;
	uint32 hostmem_addr;
} fwtrace_dma_header_info_t;

/*
 * Content of each trace entry
 */
typedef struct fwtrace_entry {
	uint32 func_ptr;
	/* How the pkts_cycle being used?
	 * Bit31-23: (If present) Used to indicate the number of packets processed by the
	 * current function
	 * Bit22-1 : Used to indicate the CPU cycles(in units of 2Cycles). So to get the actual
	 * cycles multiply the cycles by 2.
	 * Bit0    : Used to indicate whether this entry is valid or not
	 */
	uint32 pkts_cycles;
} fwtrace_entry_t;

#define FWTRACE_CYCLES_VALID	(1u << 0u)

/*
 * Format of firmware trace buffer pushed to host memory
 */
typedef struct fwtrace_buf {
	fwtrace_dma_header_info_t info;	/* includes the sequence number and the length */
	fwtrace_entry_t           entry[FWTRACE_NUM_ENTRIES];
} fwtrace_buf_t;

void fwtracing_add_blob(uint32 update_type, uint32 trace_type, uint32 blob);
#endif	/* _bcm_fwtrace_h */
