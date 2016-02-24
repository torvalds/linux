/*
 * Broadcom PCIE
 * Software-specific definitions shared between device and host side
 * Explains the shared area between host and dongle
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 * $Id: bcmpcie.h 452261 2014-01-29 19:30:23Z $
 */

#ifndef	_bcmpcie_h_
#define	_bcmpcie_h_

#include <circularbuf.h>

#define ADDR_64(x)			(x.addr)
#define HIGH_ADDR_32(x)     ((uint32) (((sh_addr_t) x).high_addr))
#define LOW_ADDR_32(x)      ((uint32) (((sh_addr_t) x).low_addr))

typedef struct {
	uint32 low_addr;
	uint32 high_addr;
} sh_addr_t;

#define PCIE_SHARED_VERSION       0x0003
#define PCIE_SHARED_VERSION_MASK  0x00FF
#define PCIE_SHARED_ASSERT_BUILT  0x0100
#define PCIE_SHARED_ASSERT        0x0200
#define PCIE_SHARED_TRAP          0x0400
#define PCIE_SHARED_IN_BRPT       0x0800
#define PCIE_SHARED_SET_BRPT      0x1000
#define PCIE_SHARED_PENDING_BRPT  0x2000
#define PCIE_SHARED_HTOD_SPLIT    0x4000
#define PCIE_SHARED_DTOH_SPLIT    0x8000

typedef struct ring_mem {
	uint8 idx;
	uint8 rsvd;
	uint16 size;
	sh_addr_t base_addr;
} ring_mem_t;

#define RINGSTATE_INITED	1

typedef struct ring_state {
	uint8 idx;
	uint8 state;
	uint16 r_offset;
	uint16 w_offset;
	uint16 e_offset;
} ring_state_t;


typedef struct ring_info {
	uint8		h2d_ring_count;
	uint8		d2h_ring_count;
	uint8		rsvd[2];
	/* locations in the TCM where the ringmem is and ringstate are defined */
	uint32		ringmem_ptr; 	/* h2d_ring_count + d2h_ring_count */
	uint32		ring_state_ptr;	/* h2d_ring_count + d2h_ring_count */
} ring_info_t;

typedef struct {
	/* shared area version captured at flags 7:0 */
	uint32	flags;

	uint32  trap_addr;
	uint32  assert_exp_addr;
	uint32  assert_file_addr;
	uint32  assert_line;
	uint32	console_addr;		/* Address of hndrte_cons_t */
	uint32  msgtrace_addr;
	uint32  fwid;

	/* Used for debug/flow control */
	uint16  total_lfrag_pkt_cnt;
	uint16  max_host_rxbufs;
	uint32  rsvd1;

	uint32 dma_rxoffset;

	/* these will be used for sleep request/ack, d3 req/ack */
	uint32  h2d_mb_data_ptr;
	uint32  d2h_mb_data_ptr;

	/* information pertinent to host IPC/msgbuf channels */
	/* location in the TCM memory which has the ring_info */
	uint32	rings_info_ptr;

	/* block of host memory for the dongle to push the status into */
	sh_addr_t	device_rings_stsblk;
	uint32		device_rings_stsblk_len;

} pciedev_shared_t;


/* H2D mail box Data */
#define H2D_HOST_D3_INFORM	0x00000001
#define H2D_HOST_DS_ACK		0x00000002

/* D2H mail box Data */
#define D2H_DEV_D3_ACK		0x00000001
#define D2H_DEV_DS_ENTER_REQ	0x00000002
#define D2H_DEV_DS_EXIT_NOTE	0x00000004


extern pciedev_shared_t pciedev_shared;
#define NEXTTXP(i, d)           ((((i)+1) >= (d)) ? 0 : ((i)+1))
#define NTXPACTIVE(r, w, d)     (((r) <= (w)) ? ((w)-(r)) : ((d)-(r)+(w)))
#define NTXPAVAIL(r, w, d)      (((d) - NTXPACTIVE((r), (w), (d))) > 1)

#endif	/* _bcmpcie_h_ */
