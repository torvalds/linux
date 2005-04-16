/*
 *   lanstreamer.h -- driver for the IBM Auto LANStreamer PCI Adapter
 *
 *  Written By: Mike Sullivan, IBM Corporation
 *
 *  Copyright (C) 1999 IBM Corporation
 *
 *  Linux driver for IBM PCI tokenring cards based on the LanStreamer MPC
 *  chipset. 
 *
 *  This driver is based on the olympic driver for IBM PCI TokenRing cards (Pit/Pit-Phy/Olympic
 *  chipsets) written  by:
 *      1999 Peter De Schrijver All Rights Reserved
 *	1999 Mike Phillips (phillim@amtrak.com)
 *
 *  Base Driver Skeleton:
 *      Written 1993-94 by Donald Becker.
 *
 *      Copyright 1993 United States Government as represented by the
 *      Director, National Security Agency.
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
 * NO WARRANTY                                                               
 * THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR        
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT      
 * LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,      
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is    
 * solely responsible for determining the appropriateness of using and       
 * distributing the Program and assumes all risks associated with its        
 * exercise of rights under this Agreement, including but not limited to     
 * the risks and costs of program errors, damage to or loss of data,         
 * programs or equipment, and unavailability or interruption of operations.  
 *                                                                           
 * DISCLAIMER OF LIABILITY                                                   
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY   
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL        
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND   
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR     
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE    
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED  
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES             
 *                                                                           
 * You should have received a copy of the GNU General Public License         
 * along with this program; if not, write to the Free Software               
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 *                                                                           
 * 
 *  12/10/99 - Alpha Release 0.1.0
 *            First release to the public
 *  08/15/01 - Added ioctl() definitions and others - Kent Yoder <yoder1@us.ibm.com>
 *
 */

#include <linux/version.h>

#if STREAMER_IOCTL && (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
#include <asm/ioctl.h>
#define IOCTL_PRINT_RX_BUFS   SIOCDEVPRIVATE
#define IOCTL_PRINT_TX_BUFS   SIOCDEVPRIVATE+1
#define IOCTL_RX_CMD          SIOCDEVPRIVATE+2
#define IOCTL_TX_CMD          SIOCDEVPRIVATE+3
#define IOCTL_PRINT_REGISTERS SIOCDEVPRIVATE+4
#define IOCTL_PRINT_BDAS      SIOCDEVPRIVATE+5
#define IOCTL_SPIN_LOCK_TEST  SIOCDEVPRIVATE+6
#define IOCTL_SISR_MASK       SIOCDEVPRIVATE+7
#endif

/* MAX_INTR - the maximum number of times we can loop
 * inside the interrupt function before returning
 * control to the OS (maximum value is 256)
 */
#define MAX_INTR 5

#define CLS 0x0C
#define MLR 0x86
#define LTR 0x0D

#define BCTL 0x60
#define BCTL_SOFTRESET (1<<15)
#define BCTL_RX_FIFO_8 (1<<1)
#define BCTL_TX_FIFO_8 (1<<3)

#define GPR 0x4a
#define GPR_AUTOSENSE (1<<2)
#define GPR_16MBPS (1<<3)

#define LISR 0x10
#define LISR_SUM 0x12
#define LISR_RUM 0x14

#define LISR_LIE (1<<15)
#define LISR_SLIM (1<<13)
#define LISR_SLI (1<<12)
#define LISR_BPEI (1<<9)
#define LISR_BPE (1<<8)
#define LISR_SRB_CMD (1<<5)
#define LISR_ASB_REPLY (1<<4)
#define LISR_ASB_FREE_REQ (1<<2)
#define LISR_ARB_FREE (1<<1)
#define LISR_TRB_FRAME (1<<0)

#define SISR 0x16
#define SISR_SUM 0x18
#define SISR_RUM 0x1A
#define SISR_MASK 0x54
#define SISR_MASK_SUM 0x56
#define SISR_MASK_RUM 0x58

#define SISR_MI (1<<15)
#define SISR_SERR_ERR (1<<14)
#define SISR_TIMER (1<<11)
#define SISR_LAP_PAR_ERR (1<<10)
#define SISR_LAP_ACC_ERR (1<<9)
#define SISR_PAR_ERR (1<<8)
#define SISR_ADAPTER_CHECK (1<<6)
#define SISR_SRB_REPLY (1<<5)
#define SISR_ASB_FREE (1<<4)
#define SISR_ARB_CMD (1<<3)
#define SISR_TRB_REPLY (1<<2)

#define MISR_RUM 0x5A
#define MISR_MASK 0x5C
#define MISR_MASK_RUM 0x5E

#define MISR_TX2_IDLE (1<<15)
#define MISR_TX2_NO_STATUS (1<<14)
#define MISR_TX2_HALT (1<<13)
#define MISR_TX2_EOF (1<<12)
#define MISR_TX1_IDLE (1<<11)
#define MISR_TX1_NO_STATUS (1<<10)
#define MISR_TX1_HALT (1<<9)
#define MISR_TX1_EOF (1<<8)
#define MISR_RX_NOBUF (1<<5)
#define MISR_RX_EOB (1<<4)
#define MISR_RX_NO_STATUS (1<<2)
#define MISR_RX_HALT (1<<1)
#define MISR_RX_EOF (1<<0)

#define LAPA 0x62
#define LAPE 0x64
#define LAPD 0x66
#define LAPDINC 0x68
#define LAPWWO 0x6A
#define LAPWWC 0x6C
#define LAPCTL 0x6E

#define TIMER 0x4E4

#define BMCTL_SUM 0x50
#define BMCTL_RUM 0x52
#define BMCTL_TX1_DIS (1<<14)
#define BMCTL_TX2_DIS (1<<10)
#define BMCTL_RX_DIS (1<<6)
#define BMCTL_RX_ENABLED  (1<<5)

#define RXLBDA  0x90
#define RXBDA   0x94
#define RXSTAT  0x98
#define RXDBA   0x9C

#define TX1LFDA 0xA0
#define TX1FDA  0xA4
#define TX1STAT 0xA8
#define TX1DBA  0xAC
#define TX2LFDA 0xB0
#define TX2FDA  0xB4
#define TX2STAT 0xB8
#define TX2DBA  0xBC

#define STREAMER_IO_SPACE 256

#define SRB_COMMAND_SIZE 50

#define STREAMER_MAX_ADAPTERS 8	/* 0x08 __MODULE_STRING can't hand 0xnn */

/* Defines for LAN STATUS CHANGE reports */
#define LSC_SIG_LOSS 0x8000
#define LSC_HARD_ERR 0x4000
#define LSC_SOFT_ERR 0x2000
#define LSC_TRAN_BCN 0x1000
#define LSC_LWF      0x0800
#define LSC_ARW      0x0400
#define LSC_FPE      0x0200
#define LSC_RR       0x0100
#define LSC_CO       0x0080
#define LSC_SS       0x0040
#define LSC_RING_REC 0x0020
#define LSC_SR_CO    0x0010
#define LSC_FDX_MODE 0x0004

/* Defines for OPEN ADAPTER command */

#define OPEN_ADAPTER_EXT_WRAP (1<<15)
#define OPEN_ADAPTER_DIS_HARDEE (1<<14)
#define OPEN_ADAPTER_DIS_SOFTERR (1<<13)
#define OPEN_ADAPTER_PASS_ADC_MAC (1<<12)
#define OPEN_ADAPTER_PASS_ATT_MAC (1<<11)
#define OPEN_ADAPTER_ENABLE_EC (1<<10)
#define OPEN_ADAPTER_CONTENDER (1<<8)
#define OPEN_ADAPTER_PASS_BEACON (1<<7)
#define OPEN_ADAPTER_ENABLE_FDX (1<<6)
#define OPEN_ADAPTER_ENABLE_RPL (1<<5)
#define OPEN_ADAPTER_INHIBIT_ETR (1<<4)
#define OPEN_ADAPTER_INTERNAL_WRAP (1<<3)


/* Defines for SRB Commands */
#define SRB_CLOSE_ADAPTER 0x04
#define SRB_CONFIGURE_BRIDGE 0x0c
#define SRB_CONFIGURE_HP_CHANNEL 0x13
#define SRB_MODIFY_BRIDGE_PARMS 0x15
#define SRB_MODIFY_OPEN_OPTIONS 0x01
#define SRB_MODIFY_RECEIVE_OPTIONS 0x17
#define SRB_NO_OPERATION 0x00
#define SRB_OPEN_ADAPTER 0x03
#define SRB_READ_LOG 0x08
#define SRB_READ_SR_COUNTERS 0x16
#define SRB_RESET_GROUP_ADDRESS 0x02
#define SRB_RESET_TARGET_SEGMETN 0x14
#define SRB_SAVE_CONFIGURATION 0x1b
#define SRB_SET_BRIDGE_PARMS 0x09
#define SRB_SET_FUNC_ADDRESS 0x07
#define SRB_SET_GROUP_ADDRESS 0x06
#define SRB_SET_TARGET_SEGMENT 0x05

/* Clear return code */
#define STREAMER_CLEAR_RET_CODE 0xfe

/* ARB Commands */
#define ARB_RECEIVE_DATA 0x81
#define ARB_LAN_CHANGE_STATUS 0x84

/* ASB Response commands */
#define ASB_RECEIVE_DATA 0x81


/* Streamer defaults for buffers */

#define STREAMER_RX_RING_SIZE 16	/* should be a power of 2 */
/* Setting the number of TX descriptors to 1 is a workaround for an
 * undocumented hardware problem with the lanstreamer board. Setting
 * this to something higher may slightly increase the throughput you
 * can get from the card, but at the risk of locking up the box. - 
 * <yoder1@us.ibm.com>
 */
#define STREAMER_TX_RING_SIZE 1	/* should be a power of 2 */

#define PKT_BUF_SZ 4096		/* Default packet size */

/* Streamer data structures */

struct streamer_tx_desc {
	__u32 forward;
	__u32 status;
	__u32 bufcnt_framelen;
	__u32 buffer;
	__u32 buflen;
	__u32 rsvd1;
	__u32 rsvd2;
	__u32 rsvd3;
};

struct streamer_rx_desc {
	__u32 forward;
	__u32 status;
	__u32 buffer;
	__u32 framelen_buflen;
};

struct mac_receive_buffer {
	__u16 next;
	__u8 padding;
	__u8 frame_status;
	__u16 buffer_length;
	__u8 frame_data;
};

struct streamer_private {

	__u16 srb;
	__u16 trb;
	__u16 arb;
	__u16 asb;

        struct streamer_private *next;
        struct pci_dev *pci_dev;
	__u8 __iomem *streamer_mmio;
        char *streamer_card_name;
 
        spinlock_t streamer_lock;

	volatile int srb_queued;	/* True if an SRB is still posted */
	wait_queue_head_t srb_wait;

	volatile int asb_queued;	/* True if an ASB is posted */

	volatile int trb_queued;	/* True if a TRB is posted */
	wait_queue_head_t trb_wait;

	struct streamer_rx_desc *streamer_rx_ring;
	struct streamer_tx_desc *streamer_tx_ring;
	struct sk_buff *tx_ring_skb[STREAMER_TX_RING_SIZE],
	    *rx_ring_skb[STREAMER_RX_RING_SIZE];
	int tx_ring_free, tx_ring_last_status, rx_ring_last_received,
	    free_tx_ring_entries;

	struct net_device_stats streamer_stats;
	__u16 streamer_lan_status;
	__u8 streamer_ring_speed;
	__u16 pkt_buf_sz;
	__u8 streamer_receive_options, streamer_copy_all_options,
	    streamer_message_level;
	__u16 streamer_addr_table_addr, streamer_parms_addr;
	__u16 mac_rx_buffer;
	__u8 streamer_laa[6];
};

struct streamer_adapter_addr_table {

	__u8 node_addr[6];
	__u8 reserved[4];
	__u8 func_addr[4];
};

struct streamer_parameters_table {

	__u8 phys_addr[4];
	__u8 up_node_addr[6];
	__u8 up_phys_addr[4];
	__u8 poll_addr[6];
	__u16 reserved;
	__u16 acc_priority;
	__u16 auth_source_class;
	__u16 att_code;
	__u8 source_addr[6];
	__u16 beacon_type;
	__u16 major_vector;
	__u16 lan_status;
	__u16 soft_error_time;
	__u16 reserved1;
	__u16 local_ring;
	__u16 mon_error;
	__u16 beacon_transmit;
	__u16 beacon_receive;
	__u16 frame_correl;
	__u8 beacon_naun[6];
	__u32 reserved2;
	__u8 beacon_phys[4];
};
