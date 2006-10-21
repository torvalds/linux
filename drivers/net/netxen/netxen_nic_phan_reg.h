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

/*
 * The following 2 are the base adresses for the CRB registers and their
 * offsets will be added to get addresses for the index addresses.
 */
#define NIC_CRB_BASE_PORT1	NETXEN_CAM_RAM(0x200)
#define NIC_CRB_BASE_PORT2	NETXEN_CAM_RAM(0x250)

#define NETXEN_NIC_REG(X)	(NIC_CRB_BASE_PORT1+(X))

/*
 * CrbPortPhanCntrHi/Lo is used to pass the address of HostPhantomIndex address
 * which can be read by the Phantom host to get producer/consumer indexes from
 * Phantom/Casper. If it is not HOST_SHARED_MEMORY, then the following
 * registers will be used for the addresses of the ring's shared memory
 * on the Phantom.
 */

#define CRB_PHAN_CNTRL_LO_OFFSET	NETXEN_NIC_REG(0x00)
#define CRB_PHAN_CNTRL_HI_OFFSET	NETXEN_NIC_REG(0x04)

/* point to the indexes */
#define CRB_CMD_PRODUCER_OFFSET		NETXEN_NIC_REG(0x08)
#define CRB_CMD_CONSUMER_OFFSET		NETXEN_NIC_REG(0x0c)

/* address of command descriptors in the host memory */
#define CRB_HOST_CMD_ADDR_HI		NETXEN_NIC_REG(0x30)
#define CRB_HOST_CMD_ADDR_LO		NETXEN_NIC_REG(0x34)

/* The following 4 CRB registers are for doing performance coal */
#define CRB_CMD_INTR_LOOP		NETXEN_NIC_REG(0x38)
#define CRB_CMD_DMA_LOOP		NETXEN_NIC_REG(0x3c)
#define CRB_RCV_INTR_LOOP		NETXEN_NIC_REG(0x40)
#define CRB_RCV_DMA_LOOP		NETXEN_NIC_REG(0x44)

/* Needed by the host to find out the state of Phantom's initialization */
#define CRB_ENABLE_TX_INTR		NETXEN_NIC_REG(0x4c)
#define CRB_CMDPEG_STATE		NETXEN_NIC_REG(0x50)
#define CRB_CMDPEG_CMDRING		NETXEN_NIC_REG(0x54)

/* Interrupt coalescing parameters */
#define CRB_GLOBAL_INT_COAL		NETXEN_NIC_REG(0x80)
#define CRB_INT_COAL_MODE		NETXEN_NIC_REG(0x84)
#define CRB_MAX_RCV_BUFS		NETXEN_NIC_REG(0x88)
#define CRB_TX_INT_THRESHOLD		NETXEN_NIC_REG(0x8c)
#define CRB_RX_PKT_TIMER		NETXEN_NIC_REG(0x90)
#define CRB_TX_PKT_TIMER		NETXEN_NIC_REG(0x94)
#define CRB_RX_PKT_CNT			NETXEN_NIC_REG(0x98)
#define CRB_RX_TMR_CNT			NETXEN_NIC_REG(0x9c)

/* Register for communicating XG link status */
#define CRB_XG_STATE			NETXEN_NIC_REG(0xa0)

/* Debug registers for controlling NIC pkt gen agent */
#define CRB_AGENT_GO			NETXEN_NIC_REG(0xb0)
#define CRB_AGENT_TX_SIZE		NETXEN_NIC_REG(0xb4)
#define CRB_AGENT_TX_TYPE		NETXEN_NIC_REG(0xb8)
#define CRB_AGENT_TX_ADDR		NETXEN_NIC_REG(0xbc)
#define CRB_AGENT_TX_MSS		NETXEN_NIC_REG(0xc0)

/* Debug registers for observing NIC performance */
#define CRB_TX_STATE			NETXEN_NIC_REG(0xd0)
#define CRB_TX_COUNT			NETXEN_NIC_REG(0xd4)
#define CRB_RX_STATE			NETXEN_NIC_REG(0xd8)

/* CRB registers per Rcv Descriptor ring */
struct netxen_rcv_desc_crb {
	u32 crb_rcv_producer_offset __attribute__ ((aligned(512)));
	u32 crb_rcv_consumer_offset;
	u32 crb_globalrcv_ring;
};

/*
 * CRB registers used by the receive peg logic. One instance of these
 * needs to be instantiated per instance of the receive peg.
 */

struct netxen_recv_crb {
	struct netxen_rcv_desc_crb rcv_desc_crb[NUM_RCV_DESC_RINGS];
	u32 crb_rcvstatus_ring;
	u32 crb_rcv_status_producer;
	u32 crb_rcv_status_consumer;
	u32 crb_rcvpeg_state;
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
	   NETXEN_NIC_REG(0x18),
	   /* crb_rcv_consumer_offset: */
	   NETXEN_NIC_REG(0x1c),
	   /* crb_gloablrcv_ring: */
	   NETXEN_NIC_REG(0x20),
	   },
	  /* Jumbo frames */
	  {
	   /* crb_rcv_producer_offset: */
	   NETXEN_NIC_REG(0x100),
	   /* crb_rcv_consumer_offset: */
	   NETXEN_NIC_REG(0x104),
	   /* crb_gloablrcv_ring: */
	   NETXEN_NIC_REG(0x108),
	   }
	  },
	 /* crb_rcvstatus_ring: */
	 NETXEN_NIC_REG(0x24),
	 /* crb_rcv_status_producer: */
	 NETXEN_NIC_REG(0x28),
	 /* crb_rcv_status_consumer: */
	 NETXEN_NIC_REG(0x2c),
	 /* crb_rcvpeg_state: */
	 NETXEN_NIC_REG(0x48),

	 },
	/*
	 * Instance 1,
	 */
	{
	 /* rcv_desc_crb: */
	 {
	  {
	   /* crb_rcv_producer_offset: */
	   NETXEN_NIC_REG(0x80),
	   /* crb_rcv_consumer_offset: */
	   NETXEN_NIC_REG(0x84),
	   /* crb_globalrcv_ring: */
	   NETXEN_NIC_REG(0x88),
	   },
	  /* Jumbo frames */
	  {
	   /* crb_rcv_producer_offset: */
	   NETXEN_NIC_REG(0x10C),
	   /* crb_rcv_consumer_offset: */
	   NETXEN_NIC_REG(0x110),
	   /* crb_globalrcv_ring: */
	   NETXEN_NIC_REG(0x114),
	   }
	  },
	 /* crb_rcvstatus_ring: */
	 NETXEN_NIC_REG(0x8c),
	 /* crb_rcv_status_producer: */
	 NETXEN_NIC_REG(0x90),
	 /* crb_rcv_status_consumer: */
	 NETXEN_NIC_REG(0x94),
	 /* crb_rcvpeg_state: */
	 NETXEN_NIC_REG(0x98),
	 },
};
#else
extern struct netxen_recv_crb recv_crb_registers[];
#endif				/* DEFINE_GLOBAL_RECEIVE_CRB */

#endif				/* __NIC_PHAN_REG_H_ */
