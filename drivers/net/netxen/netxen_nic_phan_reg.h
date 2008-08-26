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
#define NX_CDRP_CRB_OFFSET          NETXEN_NIC_REG(0x18)
#define NX_ARG1_CRB_OFFSET          NETXEN_NIC_REG(0x1c)
#define NX_ARG2_CRB_OFFSET          NETXEN_NIC_REG(0x20)
#define NX_ARG3_CRB_OFFSET          NETXEN_NIC_REG(0x24)
#define NX_SIGN_CRB_OFFSET          NETXEN_NIC_REG(0x28)
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
#define CRB_XG_STATE                NETXEN_NIC_REG(0x94) /* XG Link status */
#define CRB_XG_STATE_P3             NETXEN_NIC_REG(0x98) /* XG PF Link status */
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
#define CRB_PF_LINK_SPEED_1         NETXEN_NIC_REG(0xe8)
#define CRB_PF_LINK_SPEED_2         NETXEN_NIC_REG(0xec)
#define CRB_HOST_BUFFER_CONS        NETXEN_NIC_REG(0xf0)
#define CRB_JUMBO_BUFFER_PROD       NETXEN_NIC_REG(0xf4)
#define CRB_JUMBO_BUFFER_CONS       NETXEN_NIC_REG(0xf8)
#define CRB_HOST_DUMMY_BUF          NETXEN_NIC_REG(0xfc)

#define CRB_RCVPEG_STATE            NETXEN_NIC_REG(0x13c)
#define CRB_CMD_PRODUCER_OFFSET_1   NETXEN_NIC_REG(0x1ac)
#define CRB_CMD_CONSUMER_OFFSET_1   NETXEN_NIC_REG(0x1b0)
#define CRB_CMD_PRODUCER_OFFSET_2   NETXEN_NIC_REG(0x1b8)
#define CRB_CMD_CONSUMER_OFFSET_2   NETXEN_NIC_REG(0x1bc)

// 1c0 to 1cc used for signature reg
#define CRB_CMD_PRODUCER_OFFSET_3   NETXEN_NIC_REG(0x1d0)
#define CRB_CMD_CONSUMER_OFFSET_3   NETXEN_NIC_REG(0x1d4)
#define CRB_TEMP_STATE              NETXEN_NIC_REG(0x1b4)

#define CRB_V2P_0		    NETXEN_NIC_REG(0x290)
#define CRB_V2P_1		    NETXEN_NIC_REG(0x294)
#define CRB_V2P_2		    NETXEN_NIC_REG(0x298)
#define CRB_V2P_3		    NETXEN_NIC_REG(0x29c)
#define CRB_V2P(port)		    (CRB_V2P_0+((port)*4))
#define CRB_DRIVER_VERSION	    NETXEN_NIC_REG(0x2a0)
/* sw int status/mask registers */
#define CRB_SW_INT_MASK_0	   NETXEN_NIC_REG(0x1d8)
#define CRB_SW_INT_MASK_1	   NETXEN_NIC_REG(0x1e0)
#define CRB_SW_INT_MASK_2	   NETXEN_NIC_REG(0x1e4)
#define CRB_SW_INT_MASK_3	   NETXEN_NIC_REG(0x1e8)

#define CRB_MAC_BLOCK_START        NETXEN_CAM_RAM(0x1c0)

/*
 * capabilities register, can be used to selectively enable/disable features
 * for backward compability
 */
#define CRB_NIC_CAPABILITIES_HOST	NETXEN_NIC_REG(0x1a8)
#define CRB_NIC_CAPABILITIES_FW	  	NETXEN_NIC_REG(0x1dc)
#define CRB_NIC_MSI_MODE_HOST		NETXEN_NIC_REG(0x270)
#define CRB_NIC_MSI_MODE_FW	  		NETXEN_NIC_REG(0x274)

#define INTR_SCHEME_PERPORT	      	0x1
#define MSI_MODE_MULTIFUNC	      	0x1

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

/*
 * CRB registers used by the receive peg logic.
 */

struct netxen_recv_crb {
	u32 crb_rcv_producer[NUM_RCV_DESC_RINGS];
	u32 crb_sts_consumer;
};

/*
 * Temperature control.
 */
enum {
	NX_TEMP_NORMAL = 0x1,	/* Normal operating range */
	NX_TEMP_WARN,		/* Sound alert, temperature getting high */
	NX_TEMP_PANIC		/* Fatal error, hardware has shut down. */
};

#endif				/* __NIC_PHAN_REG_H_ */
