/*
 * Copyright (C) 2003 - 2006 NetXen, Inc.
 * All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 * 
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 * 
 * Contact Information:
 *    info@netxen.com
 * NetXen,
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 */

#ifndef __NIC_PHAN_REG_H_
#define __NIC_PHAN_REG_H_

/* 
 * CRB Registers or queue message done only at initialization time.
 */
#define NIC_CRB_BASE               NETXEN_CAM_RAM(0x200)
#define NETXEN_NIC_REG(X)             (NIC_CRB_BASE+(X))

#define CRB_PHAN_CNTRL_LO_OFFSET    NETXEN_NIC_REG(0x00)
#define CRB_PHAN_CNTRL_HI_OFFSET    NETXEN_NIC_REG(0x04)
#define CRB_CMD_PRODUCER_OFFSET     NETXEN_NIC_REG(0x08)
#define CRB_CMD_CONSUMER_OFFSET     NETXEN_NIC_REG(0x0c)
#define CRB_PAUSE_ADDR_LO           NETXEN_NIC_REG(0x10)	/* C0 EPG BUG  */
#define CRB_PAUSE_ADDR_HI           NETXEN_NIC_REG(0x14)
#define CRB_HOST_CMD_ADDR_HI        NETXEN_NIC_REG(0x18)	/* host add:cmd ring */
#define CRB_HOST_CMD_ADDR_LO        NETXEN_NIC_REG(0x1c)
#define CRB_CMD_INTR_LOOP           NETXEN_NIC_REG(0x20)	/* 4 regs for perf */
#define CRB_CMD_DMA_LOOP            NETXEN_NIC_REG(0x24)
#define CRB_RCV_INTR_LOOP           NETXEN_NIC_REG(0x28)
#define CRB_RCV_DMA_LOOP            NETXEN_NIC_REG(0x2c)
#define CRB_ENABLE_TX_INTR          NETXEN_NIC_REG(0x30)	/* phantom init status */
#define CRB_MMAP_ADDR_3             NETXEN_NIC_REG(0x34)
#define CRB_CMDPEG_CMDRING          NETXEN_NIC_REG(0x38)
#define CRB_HOST_DUMMY_BUF_ADDR_HI  NETXEN_NIC_REG(0x3c)
#define CRB_HOST_DUMMY_BUF_ADDR_LO  NETXEN_NIC_REG(0x40)
#define CRB_MMAP_ADDR_0             NETXEN_NIC_REG(0x44)
#define CRB_MMAP_ADDR_1             NETXEN_NIC_REG(0x48)
#define CRB_MMAP_ADDR_2             NETXEN_NIC_REG(0x4c)
#define CRB_CMDPEG_STATE            NETXEN_NIC_REG(0x50)
#define CRB_MMAP_SIZE_0             NETXEN_NIC_REG(0x54)
#define CRB_MMAP_SIZE_1             NETXEN_NIC_REG(0x58)
#define CRB_MMAP_SIZE_2             NETXEN_NIC_REG(0x5c)
#define CRB_MMAP_SIZE_3             NETXEN_NIC_REG(0x60)
#define CRB_GLOBAL_INT_COAL         NETXEN_NIC_REG(0x64)	/* interrupt coalescing */
#define CRB_INT_COAL_MODE           NETXEN_NIC_REG(0x68)
#define CRB_MAX_RCV_BUFS            NETXEN_NIC_REG(0x6c)
#define CRB_TX_INT_THRESHOLD        NETXEN_NIC_REG(0x70)
#define CRB_RX_PKT_TIMER            NETXEN_NIC_REG(0x74)
#define CRB_TX_PKT_TIMER            NETXEN_NIC_REG(0x78)
#define CRB_RX_PKT_CNT              NETXEN_NIC_REG(0x7c)
#define CRB_RX_TMR_CNT              NETXEN_NIC_REG(0x80)
#define CRB_RX_LRO_TIMER            NETXEN_NIC_REG(0x84)
#define CRB_RX_LRO_MID_TIMER        NETXEN_NIC_REG(0x88)
#define CRB_DMA_MAX_RCV_BUFS        NETXEN_NIC_REG(0x8c)
#define CRB_MAX_DMA_ENTRIES         NETXEN_NIC_REG(0x90)
#define CRB_XG_STATE                NETXEN_NIC_REG(0x94)	/* XG Link status */
#define CRB_AGENT_GO                NETXEN_NIC_REG(0x98)	/* NIC pkt gen agent */
#define CRB_AGENT_TX_SIZE           NETXEN_NIC_REG(0x9c)
#define CRB_AGENT_TX_TYPE           NETXEN_NIC_REG(0xa0)
#define CRB_AGENT_TX_ADDR           NETXEN_NIC_REG(0xa4)
#define CRB_AGENT_TX_MSS            NETXEN_NIC_REG(0xa8)
#define CRB_TX_STATE                NETXEN_NIC_REG(0xac)	/* Debug -performance */
#define CRB_TX_COUNT                NETXEN_NIC_REG(0xb0)
#define CRB_RX_STATE                NETXEN_NIC_REG(0xb4)
#define CRB_RX_PERF_DEBUG_1         NETXEN_NIC_REG(0xb8)
#define CRB_RX_LRO_CONTROL          NETXEN_NIC_REG(0xbc)	/* LRO On/OFF */
#define CRB_RX_LRO_START_NUM        NETXEN_NIC_REG(0xc0)
#define CRB_MPORT_MODE              NETXEN_NIC_REG(0xc4)	/* Multiport Mode */
#define CRB_CMD_RING_SIZE           NETXEN_NIC_REG(0xc8)
#define CRB_INT_VECTOR              NETXEN_NIC_REG(0xd4)
#define CRB_CTX_RESET               NETXEN_NIC_REG(0xd8)
#define CRB_HOST_STS_PROD           NETXEN_NIC_REG(0xdc)
#define CRB_HOST_STS_CONS           NETXEN_NIC_REG(0xe0)
#define CRB_PEG_CMD_PROD            NETXEN_NIC_REG(0xe4)
#define CRB_PEG_CMD_CONS            NETXEN_NIC_REG(0xe8)
#define CRB_HOST_BUFFER_PROD        NETXEN_NIC_REG(0xec)
#define CRB_HOST_BUFFER_CONS        NETXEN_NIC_REG(0xf0)
#define CRB_JUMBO_BUFFER_PROD       NETXEN_NIC_REG(0xf4)
#define CRB_JUMBO_BUFFER_CONS       NETXEN_NIC_REG(0xf8)

#define CRB_CMD_PRODUCER_OFFSET_1   NETXEN_NIC_REG(0x1ac)
#define CRB_CMD_CONSUMER_OFFSET_1   NETXEN_NIC_REG(0x1b0)
#define CRB_TEMP_STATE              NETXEN_NIC_REG(0x1b4)

/* used for ethtool tests */
#define CRB_SCRATCHPAD_TEST	    NETXEN_NIC_REG(0x280)

/*
 * CrbPortPhanCntrHi/Lo is used to pass the address of HostPhantomIndex address
 * which can be read by the Phantom host to get producer/consumer indexes from
 * Phantom/Casper. If it is not HOST_SHARED_MEMORY, then the following
 * registers will be used for the addresses of the ring's shared memory
 * on the Phantom.
 */

#define nx_get_temp_val(x)		((x) >> 16)
#define nx_get_temp_state(x)		((x) & 0xffff)
#define nx_encode_temp(val, state)	(((val) << 16) | (state))

/* CRB registers per Rcv Descriptor ring */
struct netxen_rcv_desc_crb {
	u32 crb_rcv_producer_offset __attribute__ ((aligned(512)));
	u32 crb_rcv_consumer_offset;
	u32 crb_globalrcv_ring;
	u32 crb_rcv_ring_size;
};

/*
 * CRB registers used by the receive peg logic.
 */

struct netxen_recv_crb {
	struct netxen_rcv_desc_crb rcv_desc_crb[NUM_RCV_DESC_RINGS];
	u32 crb_rcvstatus_ring;
	u32 crb_rcv_status_producer;
	u32 crb_rcv_status_consumer;
	u32 crb_rcvpeg_state;
	u32 crb_status_ring_size;
};

#if defined(DEFINE_GLOBAL_RECV_CRB)
struct netxen_recv_crb recv_crb_registers[] = {
	/*
	 * Instance 0.
	 */
	{
	 /* rcv_desc_crb: */
	 {
	  {
	   /* crb_rcv_producer_offset: */
	   NETXEN_NIC_REG(0x100),
	   /* crb_rcv_consumer_offset: */
	   NETXEN_NIC_REG(0x104),
	   /* crb_gloablrcv_ring: */
	   NETXEN_NIC_REG(0x108),
	   /* crb_rcv_ring_size */
	   NETXEN_NIC_REG(0x10c),

	   },
	  /* Jumbo frames */
	  {
	   /* crb_rcv_producer_offset: */
	   NETXEN_NIC_REG(0x110),
	   /* crb_rcv_consumer_offset: */
	   NETXEN_NIC_REG(0x114),
	   /* crb_gloablrcv_ring: */
	   NETXEN_NIC_REG(0x118),
	   /* crb_rcv_ring_size */
	   NETXEN_NIC_REG(0x11c),
	   },
	  /* LRO */
	  {
	   /* crb_rcv_producer_offset: */
	   NETXEN_NIC_REG(0x120),
	   /* crb_rcv_consumer_offset: */
	   NETXEN_NIC_REG(0x124),
	   /* crb_gloablrcv_ring: */
	   NETXEN_NIC_REG(0x128),
	   /* crb_rcv_ring_size */
	   NETXEN_NIC_REG(0x12c),
	   }
	  },
	 /* crb_rcvstatus_ring: */
	 NETXEN_NIC_REG(0x130),
	 /* crb_rcv_status_producer: */
	 NETXEN_NIC_REG(0x134),
	 /* crb_rcv_status_consumer: */
	 NETXEN_NIC_REG(0x138),
	 /* crb_rcvpeg_state: */
	 NETXEN_NIC_REG(0x13c),
	 /* crb_status_ring_size */
	 NETXEN_NIC_REG(0x140),

	 },
	/*
	 * Instance 1,
	 */
	{
	 /* rcv_desc_crb: */
	 {
	  {
	   /* crb_rcv_producer_offset: */
	   NETXEN_NIC_REG(0x144),
	   /* crb_rcv_consumer_offset: */
	   NETXEN_NIC_REG(0x148),
	   /* crb_globalrcv_ring: */
	   NETXEN_NIC_REG(0x14c),
	   /* crb_rcv_ring_size */
	   NETXEN_NIC_REG(0x150),

	   },
	  /* Jumbo frames */
	  {
	   /* crb_rcv_producer_offset: */
	   NETXEN_NIC_REG(0x154),
	   /* crb_rcv_consumer_offset: */
	   NETXEN_NIC_REG(0x158),
	   /* crb_globalrcv_ring: */
	   NETXEN_NIC_REG(0x15c),
	   /* crb_rcv_ring_size */
	   NETXEN_NIC_REG(0x160),
	   },
	  /* LRO */
	  {
	   /* crb_rcv_producer_offset: */
	   NETXEN_NIC_REG(0x164),
	   /* crb_rcv_consumer_offset: */
	   NETXEN_NIC_REG(0x168),
	   /* crb_globalrcv_ring: */
	   NETXEN_NIC_REG(0x16c),
	   /* crb_rcv_ring_size */
	   NETXEN_NIC_REG(0x170),
	   }

	  },
	 /* crb_rcvstatus_ring: */
	 NETXEN_NIC_REG(0x174),
	 /* crb_rcv_status_producer: */
	 NETXEN_NIC_REG(0x178),
	 /* crb_rcv_status_consumer: */
	 NETXEN_NIC_REG(0x17c),
	 /* crb_rcvpeg_state: */
	 NETXEN_NIC_REG(0x180),
	 /* crb_status_ring_size */
	 NETXEN_NIC_REG(0x184),

	 },
};

u64 ctx_addr_sig_regs[][3] = {
	{NETXEN_NIC_REG(0x188), NETXEN_NIC_REG(0x18c), NETXEN_NIC_REG(0x1c0)},
	{NETXEN_NIC_REG(0x190), NETXEN_NIC_REG(0x194), NETXEN_NIC_REG(0x1c4)},
	{NETXEN_NIC_REG(0x198), NETXEN_NIC_REG(0x19c), NETXEN_NIC_REG(0x1c8)},
	{NETXEN_NIC_REG(0x1a0), NETXEN_NIC_REG(0x1a4), NETXEN_NIC_REG(0x1cc)}
};

#else
extern struct netxen_recv_crb recv_crb_registers[];
extern u64 ctx_addr_sig_regs[][3];
#define CRB_CTX_ADDR_REG_LO            (ctx_addr_sig_regs[0][0])
#define CRB_CTX_ADDR_REG_HI            (ctx_addr_sig_regs[0][2])
#define CRB_CTX_SIGNATURE_REG       (ctx_addr_sig_regs[0][1])
#endif				/* DEFINE_GLOBAL_RECEIVE_CRB */

/*
 * Temperature control.
 */
enum {
	NX_TEMP_NORMAL = 0x1,	/* Normal operating range */
	NX_TEMP_WARN,		/* Sound alert, temperature getting high */
	NX_TEMP_PANIC		/* Fatal error, hardware has shut down. */
};

#endif				/* __NIC_PHAN_REG_H_ */
