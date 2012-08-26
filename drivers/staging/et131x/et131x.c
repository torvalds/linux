/*
 * Agere Systems Inc.
 * 10/100/1000 Base-T Ethernet Driver for the ET1301 and ET131x series MACs
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 * Copyright (c) 2011 Mark Einon <mark.einon@gmail.com>
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/io.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/crc32.h>
#include <linux/random.h>
#include <linux/phy.h>

#include "et131x.h"

MODULE_AUTHOR("Victor Soriano <vjsoriano@agere.com>");
MODULE_AUTHOR("Mark Einon <mark.einon@gmail.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("10/100/1000 Base-T Ethernet Driver for the ET1310 by Agere Systems");

/* EEPROM defines */
#define MAX_NUM_REGISTER_POLLS          1000
#define MAX_NUM_WRITE_RETRIES           2

/* MAC defines */
#define COUNTER_WRAP_16_BIT 0x10000
#define COUNTER_WRAP_12_BIT 0x1000

/* PCI defines */
#define INTERNAL_MEM_SIZE       0x400	/* 1024 of internal memory */
#define INTERNAL_MEM_RX_OFFSET  0x1FF	/* 50%   Tx, 50%   Rx */

/* ISR defines */
/*
 * For interrupts, normal running is:
 *       rxdma_xfr_done, phy_interrupt, mac_stat_interrupt,
 *       watchdog_interrupt & txdma_xfer_done
 *
 * In both cases, when flow control is enabled for either Tx or bi-direction,
 * we additional enable rx_fbr0_low and rx_fbr1_low, so we know when the
 * buffer rings are running low.
 */
#define INT_MASK_DISABLE            0xffffffff

/* NOTE: Masking out MAC_STAT Interrupt for now...
 * #define INT_MASK_ENABLE             0xfff6bf17
 * #define INT_MASK_ENABLE_NO_FLOW     0xfff6bfd7
 */
#define INT_MASK_ENABLE             0xfffebf17
#define INT_MASK_ENABLE_NO_FLOW     0xfffebfd7

/* General defines */
/* Packet and header sizes */
#define NIC_MIN_PACKET_SIZE	60

/* Multicast list size */
#define NIC_MAX_MCAST_LIST	128

/* Supported Filters */
#define ET131X_PACKET_TYPE_DIRECTED		0x0001
#define ET131X_PACKET_TYPE_MULTICAST		0x0002
#define ET131X_PACKET_TYPE_BROADCAST		0x0004
#define ET131X_PACKET_TYPE_PROMISCUOUS		0x0008
#define ET131X_PACKET_TYPE_ALL_MULTICAST	0x0010

/* Tx Timeout */
#define ET131X_TX_TIMEOUT	(1 * HZ)
#define NIC_SEND_HANG_THRESHOLD	0

/* MP_TCB flags */
#define fMP_DEST_MULTI			0x00000001
#define fMP_DEST_BROAD			0x00000002

/* MP_ADAPTER flags */
#define fMP_ADAPTER_RECV_LOOKASIDE	0x00000004
#define fMP_ADAPTER_INTERRUPT_IN_USE	0x00000008

/* MP_SHARED flags */
#define fMP_ADAPTER_LOWER_POWER		0x00200000

#define fMP_ADAPTER_NON_RECOVER_ERROR	0x00800000
#define fMP_ADAPTER_HARDWARE_ERROR	0x04000000

#define fMP_ADAPTER_FAIL_SEND_MASK	0x3ff00000

/* Some offsets in PCI config space that are actually used. */
#define ET1310_PCI_MAC_ADDRESS		0xA4
#define ET1310_PCI_EEPROM_STATUS	0xB2
#define ET1310_PCI_ACK_NACK		0xC0
#define ET1310_PCI_REPLAY		0xC2
#define ET1310_PCI_L0L1LATENCY		0xCF

/* PCI Product IDs */
#define ET131X_PCI_DEVICE_ID_GIG	0xED00	/* ET1310 1000 Base-T 8 */
#define ET131X_PCI_DEVICE_ID_FAST	0xED01	/* ET1310 100  Base-T */

/* Define order of magnitude converter */
#define NANO_IN_A_MICRO	1000

#define PARM_RX_NUM_BUFS_DEF    4
#define PARM_RX_TIME_INT_DEF    10
#define PARM_RX_MEM_END_DEF     0x2bc
#define PARM_TX_TIME_INT_DEF    40
#define PARM_TX_NUM_BUFS_DEF    4
#define PARM_DMA_CACHE_DEF      0

/* RX defines */
#define USE_FBR0 1
#define FBR_CHUNKS 32
#define MAX_DESC_PER_RING_RX         1024

/* number of RFDs - default and min */
#ifdef USE_FBR0
#define RFD_LOW_WATER_MARK	40
#define NIC_DEFAULT_NUM_RFD	1024
#define NUM_FBRS		2
#else
#define RFD_LOW_WATER_MARK	20
#define NIC_DEFAULT_NUM_RFD	256
#define NUM_FBRS		1
#endif

#define NIC_MIN_NUM_RFD		64
#define NUM_PACKETS_HANDLED	256

#define ALCATEL_MULTICAST_PKT	0x01000000
#define ALCATEL_BROADCAST_PKT	0x02000000

/* typedefs for Free Buffer Descriptors */
struct fbr_desc {
	u32 addr_lo;
	u32 addr_hi;
	u32 word2;		/* Bits 10-31 reserved, 0-9 descriptor */
};

/* Packet Status Ring Descriptors
 *
 * Word 0:
 *
 * top 16 bits are from the Alcatel Status Word as enumerated in
 * PE-MCXMAC Data Sheet IPD DS54 0210-1 (also IPD-DS80 0205-2)
 *
 * 0: hp			hash pass
 * 1: ipa			IP checksum assist
 * 2: ipp			IP checksum pass
 * 3: tcpa			TCP checksum assist
 * 4: tcpp			TCP checksum pass
 * 5: wol			WOL Event
 * 6: rxmac_error		RXMAC Error Indicator
 * 7: drop			Drop packet
 * 8: ft			Frame Truncated
 * 9: jp			Jumbo Packet
 * 10: vp			VLAN Packet
 * 11-15: unused
 * 16: asw_prev_pkt_dropped	e.g. IFG too small on previous
 * 17: asw_RX_DV_event		short receive event detected
 * 18: asw_false_carrier_event	bad carrier since last good packet
 * 19: asw_code_err		one or more nibbles signalled as errors
 * 20: asw_CRC_err		CRC error
 * 21: asw_len_chk_err		frame length field incorrect
 * 22: asw_too_long		frame length > 1518 bytes
 * 23: asw_OK			valid CRC + no code error
 * 24: asw_multicast		has a multicast address
 * 25: asw_broadcast		has a broadcast address
 * 26: asw_dribble_nibble	spurious bits after EOP
 * 27: asw_control_frame	is a control frame
 * 28: asw_pause_frame		is a pause frame
 * 29: asw_unsupported_op	unsupported OP code
 * 30: asw_VLAN_tag		VLAN tag detected
 * 31: asw_long_evt		Rx long event
 *
 * Word 1:
 * 0-15: length			length in bytes
 * 16-25: bi			Buffer Index
 * 26-27: ri			Ring Index
 * 28-31: reserved
 */

struct pkt_stat_desc {
	u32 word0;
	u32 word1;
};

/* Typedefs for the RX DMA status word */

/*
 * rx status word 0 holds part of the status bits of the Rx DMA engine
 * that get copied out to memory by the ET-1310.  Word 0 is a 32 bit word
 * which contains the Free Buffer ring 0 and 1 available offset.
 *
 * bit 0-9 FBR1 offset
 * bit 10 Wrap flag for FBR1
 * bit 16-25 FBR0 offset
 * bit 26 Wrap flag for FBR0
 */

/*
 * RXSTAT_WORD1_t structure holds part of the status bits of the Rx DMA engine
 * that get copied out to memory by the ET-1310.  Word 3 is a 32 bit word
 * which contains the Packet Status Ring available offset.
 *
 * bit 0-15 reserved
 * bit 16-27 PSRoffset
 * bit 28 PSRwrap
 * bit 29-31 unused
 */

/*
 * struct rx_status_block is a structure representing the status of the Rx
 * DMA engine it sits in free memory, and is pointed to by 0x101c / 0x1020
 */
struct rx_status_block {
	u32 word0;
	u32 word1;
};

/*
 * Structure for look-up table holding free buffer ring pointers, addresses
 * and state.
 */
struct fbr_lookup {
	void		*virt[MAX_DESC_PER_RING_RX];
	void		*buffer1[MAX_DESC_PER_RING_RX];
	void		*buffer2[MAX_DESC_PER_RING_RX];
	u32		 bus_high[MAX_DESC_PER_RING_RX];
	u32		 bus_low[MAX_DESC_PER_RING_RX];
	void		*ring_virtaddr;
	dma_addr_t	 ring_physaddr;
	void		*mem_virtaddrs[MAX_DESC_PER_RING_RX / FBR_CHUNKS];
	dma_addr_t	 mem_physaddrs[MAX_DESC_PER_RING_RX / FBR_CHUNKS];
	u64		 real_physaddr;
	u64		 offset;
	u32		 local_full;
	u32		 num_entries;
	u32		 buffsize;
};

/*
 * struct rx_ring is the sructure representing the adaptor's local
 * reference(s) to the rings
 *
 ******************************************************************************
 * IMPORTANT NOTE :- fbr_lookup *fbr[NUM_FBRS] uses index 0 to refer to FBR1
 *			and index 1 to refer to FRB0
 ******************************************************************************
 */
struct rx_ring {
	struct fbr_lookup *fbr[NUM_FBRS];
	void *ps_ring_virtaddr;
	dma_addr_t ps_ring_physaddr;
	u32 local_psr_full;
	u32 psr_num_entries;

	struct rx_status_block *rx_status_block;
	dma_addr_t rx_status_bus;

	/* RECV */
	struct list_head recv_list;
	u32 num_ready_recv;

	u32 num_rfd;

	bool unfinished_receives;

	/* lookaside lists */
	struct kmem_cache *recv_lookaside;
};

/* TX defines */
/*
 * word 2 of the control bits in the Tx Descriptor ring for the ET-1310
 *
 * 0-15: length of packet
 * 16-27: VLAN tag
 * 28: VLAN CFI
 * 29-31: VLAN priority
 *
 * word 3 of the control bits in the Tx Descriptor ring for the ET-1310
 *
 * 0: last packet in the sequence
 * 1: first packet in the sequence
 * 2: interrupt the processor when this pkt sent
 * 3: Control word - no packet data
 * 4: Issue half-duplex backpressure : XON/XOFF
 * 5: send pause frame
 * 6: Tx frame has error
 * 7: append CRC
 * 8: MAC override
 * 9: pad packet
 * 10: Packet is a Huge packet
 * 11: append VLAN tag
 * 12: IP checksum assist
 * 13: TCP checksum assist
 * 14: UDP checksum assist
 */

/* struct tx_desc represents each descriptor on the ring */
struct tx_desc {
	u32 addr_hi;
	u32 addr_lo;
	u32 len_vlan;	/* control words how to xmit the */
	u32 flags;	/* data (detailed above) */
};

/*
 * The status of the Tx DMA engine it sits in free memory, and is pointed to
 * by 0x101c / 0x1020. This is a DMA10 type
 */

/* TCB (Transmit Control Block: Host Side) */
struct tcb {
	struct tcb *next;	/* Next entry in ring */
	u32 flags;		/* Our flags for the packet */
	u32 count;		/* Used to spot stuck/lost packets */
	u32 stale;		/* Used to spot stuck/lost packets */
	struct sk_buff *skb;	/* Network skb we are tied to */
	u32 index;		/* Ring indexes */
	u32 index_start;
};

/* Structure representing our local reference(s) to the ring */
struct tx_ring {
	/* TCB (Transmit Control Block) memory and lists */
	struct tcb *tcb_ring;

	/* List of TCBs that are ready to be used */
	struct tcb *tcb_qhead;
	struct tcb *tcb_qtail;

	/* list of TCBs that are currently being sent.  NOTE that access to all
	 * three of these (including used) are controlled via the
	 * TCBSendQLock.  This lock should be secured prior to incementing /
	 * decrementing used, or any queue manipulation on send_head /
	 * tail
	 */
	struct tcb *send_head;
	struct tcb *send_tail;
	int used;

	/* The actual descriptor ring */
	struct tx_desc *tx_desc_ring;
	dma_addr_t tx_desc_ring_pa;

	/* send_idx indicates where we last wrote to in the descriptor ring. */
	u32 send_idx;

	/* The location of the write-back status block */
	u32 *tx_status;
	dma_addr_t tx_status_pa;

	/* Packets since the last IRQ: used for interrupt coalescing */
	int since_irq;
};

/*
 * Do not change these values: if changed, then change also in respective
 * TXdma and Rxdma engines
 */
#define NUM_DESC_PER_RING_TX         512    /* TX Do not change these values */
#define NUM_TCB                      64

/*
 * These values are all superseded by registry entries to facilitate tuning.
 * Once the desired performance has been achieved, the optimal registry values
 * should be re-populated to these #defines:
 */
#define TX_ERROR_PERIOD             1000

#define LO_MARK_PERCENT_FOR_PSR     15
#define LO_MARK_PERCENT_FOR_RX      15

/* RFD (Receive Frame Descriptor) */
struct rfd {
	struct list_head list_node;
	struct sk_buff *skb;
	u32 len;	/* total size of receive frame */
	u16 bufferindex;
	u8 ringindex;
};

/* Flow Control */
#define FLOW_BOTH	0
#define FLOW_TXONLY	1
#define FLOW_RXONLY	2
#define FLOW_NONE	3

/* Struct to define some device statistics */
struct ce_stats {
	/* MIB II variables
	 *
	 * NOTE: atomic_t types are only guaranteed to store 24-bits; if we
	 * MUST have 32, then we'll need another way to perform atomic
	 * operations
	 */
	u32		unicast_pkts_rcvd;
	atomic_t	unicast_pkts_xmtd;
	u32		multicast_pkts_rcvd;
	atomic_t	multicast_pkts_xmtd;
	u32		broadcast_pkts_rcvd;
	atomic_t	broadcast_pkts_xmtd;
	u32		rcvd_pkts_dropped;

	/* Tx Statistics. */
	u32		tx_underflows;

	u32		tx_collisions;
	u32		tx_excessive_collisions;
	u32		tx_first_collisions;
	u32		tx_late_collisions;
	u32		tx_max_pkt_errs;
	u32		tx_deferred;

	/* Rx Statistics. */
	u32		rx_overflows;

	u32		rx_length_errs;
	u32		rx_align_errs;
	u32		rx_crc_errs;
	u32		rx_code_violations;
	u32		rx_other_errs;

	u32		synchronous_iterations;
	u32		interrupt_status;
};

/* The private adapter structure */
struct et131x_adapter {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct mii_bus *mii_bus;
	struct phy_device *phydev;
	struct work_struct task;

	/* Flags that indicate current state of the adapter */
	u32 flags;

	/* local link state, to determine if a state change has occurred */
	int link;

	/* Configuration  */
	u8 rom_addr[ETH_ALEN];
	u8 addr[ETH_ALEN];
	bool has_eeprom;
	u8 eeprom_data[2];

	/* Spinlocks */
	spinlock_t lock;

	spinlock_t tcb_send_qlock;
	spinlock_t tcb_ready_qlock;
	spinlock_t send_hw_lock;

	spinlock_t rcv_lock;
	spinlock_t rcv_pend_lock;
	spinlock_t fbr_lock;

	spinlock_t phy_lock;

	/* Packet Filter and look ahead size */
	u32 packet_filter;

	/* multicast list */
	u32 multicast_addr_count;
	u8 multicast_list[NIC_MAX_MCAST_LIST][ETH_ALEN];

	/* Pointer to the device's PCI register space */
	struct address_map __iomem *regs;

	/* Registry parameters */
	u8 wanted_flow;		/* Flow we want for 802.3x flow control */
	u32 registry_jumbo_packet;	/* Max supported ethernet packet size */

	/* Derived from the registry: */
	u8 flowcontrol;		/* flow control validated by the far-end */

	/* Minimize init-time */
	struct timer_list error_timer;

	/* variable putting the phy into coma mode when boot up with no cable
	 * plugged in after 5 seconds
	 */
	u8 boot_coma;

	/* Next two used to save power information at power down. This
	 * information will be used during power up to set up parts of Power
	 * Management in JAGCore
	 */
	u16 pdown_speed;
	u8 pdown_duplex;

	/* Tx Memory Variables */
	struct tx_ring tx_ring;

	/* Rx Memory Variables */
	struct rx_ring rx_ring;

	/* Stats */
	struct ce_stats stats;

	struct net_device_stats net_stats;
};

static int eeprom_wait_ready(struct pci_dev *pdev, u32 *status)
{
	u32 reg;
	int i;

	/*
	 * 1. Check LBCIF Status Register for bits 6 & 3:2 all equal to 0 and
	 *    bits 7,1:0 both equal to 1, at least once after reset.
	 *    Subsequent operations need only to check that bits 1:0 are equal
	 *    to 1 prior to starting a single byte read/write
	 */

	for (i = 0; i < MAX_NUM_REGISTER_POLLS; i++) {
		/* Read registers grouped in DWORD1 */
		if (pci_read_config_dword(pdev, LBCIF_DWORD1_GROUP, &reg))
			return -EIO;

		/* I2C idle and Phy Queue Avail both true */
		if ((reg & 0x3000) == 0x3000) {
			if (status)
				*status = reg;
			return reg & 0xFF;
		}
	}
	return -ETIMEDOUT;
}


/**
 * eeprom_write - Write a byte to the ET1310's EEPROM
 * @adapter: pointer to our private adapter structure
 * @addr: the address to write
 * @data: the value to write
 *
 * Returns 1 for a successful write.
 */
static int eeprom_write(struct et131x_adapter *adapter, u32 addr, u8 data)
{
	struct pci_dev *pdev = adapter->pdev;
	int index = 0;
	int retries;
	int err = 0;
	int i2c_wack = 0;
	int writeok = 0;
	u32 status;
	u32 val = 0;

	/*
	 * For an EEPROM, an I2C single byte write is defined as a START
	 * condition followed by the device address, EEPROM address, one byte
	 * of data and a STOP condition.  The STOP condition will trigger the
	 * EEPROM's internally timed write cycle to the nonvolatile memory.
	 * All inputs are disabled during this write cycle and the EEPROM will
	 * not respond to any access until the internal write is complete.
	 */

	err = eeprom_wait_ready(pdev, NULL);
	if (err)
		return err;

	 /*
	 * 2. Write to the LBCIF Control Register:  bit 7=1, bit 6=1, bit 3=0,
	 *    and bits 1:0 both =0.  Bit 5 should be set according to the
	 *    type of EEPROM being accessed (1=two byte addressing, 0=one
	 *    byte addressing).
	 */
	if (pci_write_config_byte(pdev, LBCIF_CONTROL_REGISTER,
			LBCIF_CONTROL_LBCIF_ENABLE | LBCIF_CONTROL_I2C_WRITE))
		return -EIO;

	i2c_wack = 1;

	/* Prepare EEPROM address for Step 3 */

	for (retries = 0; retries < MAX_NUM_WRITE_RETRIES; retries++) {
		/* Write the address to the LBCIF Address Register */
		if (pci_write_config_dword(pdev, LBCIF_ADDRESS_REGISTER, addr))
			break;
		/*
		 * Write the data to the LBCIF Data Register (the I2C write
		 * will begin).
		 */
		if (pci_write_config_byte(pdev, LBCIF_DATA_REGISTER, data))
			break;
		/*
		 * Monitor bit 1:0 of the LBCIF Status Register.  When bits
		 * 1:0 are both equal to 1, the I2C write has completed and the
		 * internal write cycle of the EEPROM is about to start.
		 * (bits 1:0 = 01 is a legal state while waiting from both
		 * equal to 1, but bits 1:0 = 10 is invalid and implies that
		 * something is broken).
		 */
		err = eeprom_wait_ready(pdev, &status);
		if (err < 0)
			return 0;

		/*
		 * Check bit 3 of the LBCIF Status Register.  If  equal to 1,
		 * an error has occurred.Don't break here if we are revision
		 * 1, this is so we do a blind write for load bug.
		 */
		if ((status & LBCIF_STATUS_GENERAL_ERROR)
			&& adapter->pdev->revision == 0)
			break;

		/*
		 * Check bit 2 of the LBCIF Status Register.  If equal to 1 an
		 * ACK error has occurred on the address phase of the write.
		 * This could be due to an actual hardware failure or the
		 * EEPROM may still be in its internal write cycle from a
		 * previous write. This write operation was ignored and must be
		  *repeated later.
		 */
		if (status & LBCIF_STATUS_ACK_ERROR) {
			/*
			 * This could be due to an actual hardware failure
			 * or the EEPROM may still be in its internal write
			 * cycle from a previous write. This write operation
			 * was ignored and must be repeated later.
			 */
			udelay(10);
			continue;
		}

		writeok = 1;
		break;
	}

	/*
	 * Set bit 6 of the LBCIF Control Register = 0.
	 */
	udelay(10);

	while (i2c_wack) {
		if (pci_write_config_byte(pdev, LBCIF_CONTROL_REGISTER,
			LBCIF_CONTROL_LBCIF_ENABLE))
			writeok = 0;

		/* Do read until internal ACK_ERROR goes away meaning write
		 * completed
		 */
		do {
			pci_write_config_dword(pdev,
					       LBCIF_ADDRESS_REGISTER,
					       addr);
			do {
				pci_read_config_dword(pdev,
					LBCIF_DATA_REGISTER, &val);
			} while ((val & 0x00010000) == 0);
		} while (val & 0x00040000);

		if ((val & 0xFF00) != 0xC000 || index == 10000)
			break;
		index++;
	}
	return writeok ? 0 : -EIO;
}

/**
 * eeprom_read - Read a byte from the ET1310's EEPROM
 * @adapter: pointer to our private adapter structure
 * @addr: the address from which to read
 * @pdata: a pointer to a byte in which to store the value of the read
 * @eeprom_id: the ID of the EEPROM
 * @addrmode: how the EEPROM is to be accessed
 *
 * Returns 1 for a successful read
 */
static int eeprom_read(struct et131x_adapter *adapter, u32 addr, u8 *pdata)
{
	struct pci_dev *pdev = adapter->pdev;
	int err;
	u32 status;

	/*
	 * A single byte read is similar to the single byte write, with the
	 * exception of the data flow:
	 */

	err = eeprom_wait_ready(pdev, NULL);
	if (err)
		return err;
	/*
	 * Write to the LBCIF Control Register:  bit 7=1, bit 6=0, bit 3=0,
	 * and bits 1:0 both =0.  Bit 5 should be set according to the type
	 * of EEPROM being accessed (1=two byte addressing, 0=one byte
	 * addressing).
	 */
	if (pci_write_config_byte(pdev, LBCIF_CONTROL_REGISTER,
				  LBCIF_CONTROL_LBCIF_ENABLE))
		return -EIO;
	/*
	 * Write the address to the LBCIF Address Register (I2C read will
	 * begin).
	 */
	if (pci_write_config_dword(pdev, LBCIF_ADDRESS_REGISTER, addr))
		return -EIO;
	/*
	 * Monitor bit 0 of the LBCIF Status Register.  When = 1, I2C read
	 * is complete. (if bit 1 =1 and bit 0 stays = 0, a hardware failure
	 * has occurred).
	 */
	err = eeprom_wait_ready(pdev, &status);
	if (err < 0)
		return err;
	/*
	 * Regardless of error status, read data byte from LBCIF Data
	 * Register.
	 */
	*pdata = err;
	/*
	 * Check bit 2 of the LBCIF Status Register.  If = 1,
	 * then an error has occurred.
	 */
	return (status & LBCIF_STATUS_ACK_ERROR) ? -EIO : 0;
}

static int et131x_init_eeprom(struct et131x_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	u8 eestatus;

	/* We first need to check the EEPROM Status code located at offset
	 * 0xB2 of config space
	 */
	pci_read_config_byte(pdev, ET1310_PCI_EEPROM_STATUS,
				      &eestatus);

	/* THIS IS A WORKAROUND:
	 * I need to call this function twice to get my card in a
	 * LG M1 Express Dual running. I tried also a msleep before this
	 * function, because I thought there could be some time condidions
	 * but it didn't work. Call the whole function twice also work.
	 */
	if (pci_read_config_byte(pdev, ET1310_PCI_EEPROM_STATUS, &eestatus)) {
		dev_err(&pdev->dev,
		       "Could not read PCI config space for EEPROM Status\n");
		return -EIO;
	}

	/* Determine if the error(s) we care about are present. If they are
	 * present we need to fail.
	 */
	if (eestatus & 0x4C) {
		int write_failed = 0;
		if (pdev->revision == 0x01) {
			int	i;
			static const u8 eedata[4] = { 0xFE, 0x13, 0x10, 0xFF };

			/* Re-write the first 4 bytes if we have an eeprom
			 * present and the revision id is 1, this fixes the
			 * corruption seen with 1310 B Silicon
			 */
			for (i = 0; i < 3; i++)
				if (eeprom_write(adapter, i, eedata[i]) < 0)
					write_failed = 1;
		}
		if (pdev->revision  != 0x01 || write_failed) {
			dev_err(&pdev->dev,
			    "Fatal EEPROM Status Error - 0x%04x\n", eestatus);

			/* This error could mean that there was an error
			 * reading the eeprom or that the eeprom doesn't exist.
			 * We will treat each case the same and not try to
			 * gather additional information that normally would
			 * come from the eeprom, like MAC Address
			 */
			adapter->has_eeprom = 0;
			return -EIO;
		}
	}
	adapter->has_eeprom = 1;

	/* Read the EEPROM for information regarding LED behavior. Refer to
	 * ET1310_phy.c, et131x_xcvr_init(), for its use.
	 */
	eeprom_read(adapter, 0x70, &adapter->eeprom_data[0]);
	eeprom_read(adapter, 0x71, &adapter->eeprom_data[1]);

	if (adapter->eeprom_data[0] != 0xcd)
		/* Disable all optional features */
		adapter->eeprom_data[1] = 0x00;

	return 0;
}

/**
 * et131x_rx_dma_enable - re-start of Rx_DMA on the ET1310.
 * @adapter: pointer to our adapter structure
 */
static void et131x_rx_dma_enable(struct et131x_adapter *adapter)
{
	/* Setup the receive dma configuration register for normal operation */
	u32 csr =  0x2000;	/* FBR1 enable */

	if (adapter->rx_ring.fbr[0]->buffsize == 4096)
		csr |= 0x0800;
	else if (adapter->rx_ring.fbr[0]->buffsize == 8192)
		csr |= 0x1000;
	else if (adapter->rx_ring.fbr[0]->buffsize == 16384)
		csr |= 0x1800;
#ifdef USE_FBR0
	csr |= 0x0400;		/* FBR0 enable */
	if (adapter->rx_ring.fbr[1]->buffsize == 256)
		csr |= 0x0100;
	else if (adapter->rx_ring.fbr[1]->buffsize == 512)
		csr |= 0x0200;
	else if (adapter->rx_ring.fbr[1]->buffsize == 1024)
		csr |= 0x0300;
#endif
	writel(csr, &adapter->regs->rxdma.csr);

	csr = readl(&adapter->regs->rxdma.csr);
	if ((csr & 0x00020000) != 0) {
		udelay(5);
		csr = readl(&adapter->regs->rxdma.csr);
		if ((csr & 0x00020000) != 0) {
			dev_err(&adapter->pdev->dev,
			    "RX Dma failed to exit halt state.  CSR 0x%08x\n",
				csr);
		}
	}
}

/**
 * et131x_rx_dma_disable - Stop of Rx_DMA on the ET1310
 * @adapter: pointer to our adapter structure
 */
static void et131x_rx_dma_disable(struct et131x_adapter *adapter)
{
	u32 csr;
	/* Setup the receive dma configuration register */
	writel(0x00002001, &adapter->regs->rxdma.csr);
	csr = readl(&adapter->regs->rxdma.csr);
	if ((csr & 0x00020000) == 0) {	/* Check halt status (bit 17) */
		udelay(5);
		csr = readl(&adapter->regs->rxdma.csr);
		if ((csr & 0x00020000) == 0)
			dev_err(&adapter->pdev->dev,
			"RX Dma failed to enter halt state. CSR 0x%08x\n",
				csr);
	}
}

/**
 * et131x_tx_dma_enable - re-start of Tx_DMA on the ET1310.
 * @adapter: pointer to our adapter structure
 *
 * Mainly used after a return to the D0 (full-power) state from a lower state.
 */
static void et131x_tx_dma_enable(struct et131x_adapter *adapter)
{
	/* Setup the transmit dma configuration register for normal
	 * operation
	 */
	writel(ET_TXDMA_SNGL_EPKT|(PARM_DMA_CACHE_DEF << ET_TXDMA_CACHE_SHIFT),
					&adapter->regs->txdma.csr);
}

static inline void add_10bit(u32 *v, int n)
{
	*v = INDEX10(*v + n) | (*v & ET_DMA10_WRAP);
}

static inline void add_12bit(u32 *v, int n)
{
	*v = INDEX12(*v + n) | (*v & ET_DMA12_WRAP);
}

/**
 * et1310_config_mac_regs1 - Initialize the first part of MAC regs
 * @adapter: pointer to our adapter structure
 */
static void et1310_config_mac_regs1(struct et131x_adapter *adapter)
{
	struct mac_regs __iomem *macregs = &adapter->regs->mac;
	u32 station1;
	u32 station2;
	u32 ipg;

	/* First we need to reset everything.  Write to MAC configuration
	 * register 1 to perform reset.
	 */
	writel(0xC00F0000, &macregs->cfg1);

	/* Next lets configure the MAC Inter-packet gap register */
	ipg = 0x38005860;		/* IPG1 0x38 IPG2 0x58 B2B 0x60 */
	ipg |= 0x50 << 8;		/* ifg enforce 0x50 */
	writel(ipg, &macregs->ipg);

	/* Next lets configure the MAC Half Duplex register */
	/* BEB trunc 0xA, Ex Defer, Rexmit 0xF Coll 0x37 */
	writel(0x00A1F037, &macregs->hfdp);

	/* Next lets configure the MAC Interface Control register */
	writel(0, &macregs->if_ctrl);

	/* Let's move on to setting up the mii management configuration */
	writel(0x07, &macregs->mii_mgmt_cfg);	/* Clock reset 0x7 */

	/* Next lets configure the MAC Station Address register.  These
	 * values are read from the EEPROM during initialization and stored
	 * in the adapter structure.  We write what is stored in the adapter
	 * structure to the MAC Station Address registers high and low.  This
	 * station address is used for generating and checking pause control
	 * packets.
	 */
	station2 = (adapter->addr[1] << ET_MAC_STATION_ADDR2_OC2_SHIFT) |
		   (adapter->addr[0] << ET_MAC_STATION_ADDR2_OC1_SHIFT);
	station1 = (adapter->addr[5] << ET_MAC_STATION_ADDR1_OC6_SHIFT) |
		   (adapter->addr[4] << ET_MAC_STATION_ADDR1_OC5_SHIFT) |
		   (adapter->addr[3] << ET_MAC_STATION_ADDR1_OC4_SHIFT) |
		    adapter->addr[2];
	writel(station1, &macregs->station_addr_1);
	writel(station2, &macregs->station_addr_2);

	/* Max ethernet packet in bytes that will be passed by the mac without
	 * being truncated.  Allow the MAC to pass 4 more than our max packet
	 * size.  This is 4 for the Ethernet CRC.
	 *
	 * Packets larger than (registry_jumbo_packet) that do not contain a
	 * VLAN ID will be dropped by the Rx function.
	 */
	writel(adapter->registry_jumbo_packet + 4, &macregs->max_fm_len);

	/* clear out MAC config reset */
	writel(0, &macregs->cfg1);
}

/**
 * et1310_config_mac_regs2 - Initialize the second part of MAC regs
 * @adapter: pointer to our adapter structure
 */
static void et1310_config_mac_regs2(struct et131x_adapter *adapter)
{
	int32_t delay = 0;
	struct mac_regs __iomem *mac = &adapter->regs->mac;
	struct phy_device *phydev = adapter->phydev;
	u32 cfg1;
	u32 cfg2;
	u32 ifctrl;
	u32 ctl;

	ctl = readl(&adapter->regs->txmac.ctl);
	cfg1 = readl(&mac->cfg1);
	cfg2 = readl(&mac->cfg2);
	ifctrl = readl(&mac->if_ctrl);

	/* Set up the if mode bits */
	cfg2 &= ~0x300;
	if (phydev && phydev->speed == SPEED_1000) {
		cfg2 |= 0x200;
		/* Phy mode bit */
		ifctrl &= ~(1 << 24);
	} else {
		cfg2 |= 0x100;
		ifctrl |= (1 << 24);
	}

	/* We need to enable Rx/Tx */
	cfg1 |= CFG1_RX_ENABLE | CFG1_TX_ENABLE | CFG1_TX_FLOW;
	/* Initialize loop back to off */
	cfg1 &= ~(CFG1_LOOPBACK | CFG1_RX_FLOW);
	if (adapter->flowcontrol == FLOW_RXONLY ||
				adapter->flowcontrol == FLOW_BOTH)
		cfg1 |= CFG1_RX_FLOW;
	writel(cfg1, &mac->cfg1);

	/* Now we need to initialize the MAC Configuration 2 register */
	/* preamble 7, check length, huge frame off, pad crc, crc enable
	   full duplex off */
	cfg2 |= 0x7016;
	cfg2 &= ~0x0021;

	/* Turn on duplex if needed */
	if (phydev && phydev->duplex == DUPLEX_FULL)
		cfg2 |= 0x01;

	ifctrl &= ~(1 << 26);
	if (phydev && phydev->duplex == DUPLEX_HALF)
		ifctrl |= (1<<26);	/* Enable ghd */

	writel(ifctrl, &mac->if_ctrl);
	writel(cfg2, &mac->cfg2);

	do {
		udelay(10);
		delay++;
		cfg1 = readl(&mac->cfg1);
	} while ((cfg1 & CFG1_WAIT) != CFG1_WAIT && delay < 100);

	if (delay == 100) {
		dev_warn(&adapter->pdev->dev,
		    "Syncd bits did not respond correctly cfg1 word 0x%08x\n",
			cfg1);
	}

	/* Enable txmac */
	ctl |= 0x09;	/* TX mac enable, FC disable */
	writel(ctl, &adapter->regs->txmac.ctl);

	/* Ready to start the RXDMA/TXDMA engine */
	if (adapter->flags & fMP_ADAPTER_LOWER_POWER) {
		et131x_rx_dma_enable(adapter);
		et131x_tx_dma_enable(adapter);
	}
}

/**
 * et1310_in_phy_coma - check if the device is in phy coma
 * @adapter: pointer to our adapter structure
 *
 * Returns 0 if the device is not in phy coma, 1 if it is in phy coma
 */
static int et1310_in_phy_coma(struct et131x_adapter *adapter)
{
	u32 pmcsr;

	pmcsr = readl(&adapter->regs->global.pm_csr);

	return ET_PM_PHY_SW_COMA & pmcsr ? 1 : 0;
}

static void et1310_setup_device_for_multicast(struct et131x_adapter *adapter)
{
	struct rxmac_regs __iomem *rxmac = &adapter->regs->rxmac;
	u32 hash1 = 0;
	u32 hash2 = 0;
	u32 hash3 = 0;
	u32 hash4 = 0;
	u32 pm_csr;

	/* If ET131X_PACKET_TYPE_MULTICAST is specified, then we provision
	 * the multi-cast LIST.  If it is NOT specified, (and "ALL" is not
	 * specified) then we should pass NO multi-cast addresses to the
	 * driver.
	 */
	if (adapter->packet_filter & ET131X_PACKET_TYPE_MULTICAST) {
		int i;

		/* Loop through our multicast array and set up the device */
		for (i = 0; i < adapter->multicast_addr_count; i++) {
			u32 result;

			result = ether_crc(6, adapter->multicast_list[i]);

			result = (result & 0x3F800000) >> 23;

			if (result < 32) {
				hash1 |= (1 << result);
			} else if ((31 < result) && (result < 64)) {
				result -= 32;
				hash2 |= (1 << result);
			} else if ((63 < result) && (result < 96)) {
				result -= 64;
				hash3 |= (1 << result);
			} else {
				result -= 96;
				hash4 |= (1 << result);
			}
		}
	}

	/* Write out the new hash to the device */
	pm_csr = readl(&adapter->regs->global.pm_csr);
	if (!et1310_in_phy_coma(adapter)) {
		writel(hash1, &rxmac->multi_hash1);
		writel(hash2, &rxmac->multi_hash2);
		writel(hash3, &rxmac->multi_hash3);
		writel(hash4, &rxmac->multi_hash4);
	}
}

static void et1310_setup_device_for_unicast(struct et131x_adapter *adapter)
{
	struct rxmac_regs __iomem *rxmac = &adapter->regs->rxmac;
	u32 uni_pf1;
	u32 uni_pf2;
	u32 uni_pf3;
	u32 pm_csr;

	/* Set up unicast packet filter reg 3 to be the first two octets of
	 * the MAC address for both address
	 *
	 * Set up unicast packet filter reg 2 to be the octets 2 - 5 of the
	 * MAC address for second address
	 *
	 * Set up unicast packet filter reg 3 to be the octets 2 - 5 of the
	 * MAC address for first address
	 */
	uni_pf3 = (adapter->addr[0] << ET_UNI_PF_ADDR2_1_SHIFT) |
		  (adapter->addr[1] << ET_UNI_PF_ADDR2_2_SHIFT) |
		  (adapter->addr[0] << ET_UNI_PF_ADDR1_1_SHIFT) |
		   adapter->addr[1];

	uni_pf2 = (adapter->addr[2] << ET_UNI_PF_ADDR2_3_SHIFT) |
		  (adapter->addr[3] << ET_UNI_PF_ADDR2_4_SHIFT) |
		  (adapter->addr[4] << ET_UNI_PF_ADDR2_5_SHIFT) |
		   adapter->addr[5];

	uni_pf1 = (adapter->addr[2] << ET_UNI_PF_ADDR1_3_SHIFT) |
		  (adapter->addr[3] << ET_UNI_PF_ADDR1_4_SHIFT) |
		  (adapter->addr[4] << ET_UNI_PF_ADDR1_5_SHIFT) |
		   adapter->addr[5];

	pm_csr = readl(&adapter->regs->global.pm_csr);
	if (!et1310_in_phy_coma(adapter)) {
		writel(uni_pf1, &rxmac->uni_pf_addr1);
		writel(uni_pf2, &rxmac->uni_pf_addr2);
		writel(uni_pf3, &rxmac->uni_pf_addr3);
	}
}

static void et1310_config_rxmac_regs(struct et131x_adapter *adapter)
{
	struct rxmac_regs __iomem *rxmac = &adapter->regs->rxmac;
	struct phy_device *phydev = adapter->phydev;
	u32 sa_lo;
	u32 sa_hi = 0;
	u32 pf_ctrl = 0;

	/* Disable the MAC while it is being configured (also disable WOL) */
	writel(0x8, &rxmac->ctrl);

	/* Initialize WOL to disabled. */
	writel(0, &rxmac->crc0);
	writel(0, &rxmac->crc12);
	writel(0, &rxmac->crc34);

	/* We need to set the WOL mask0 - mask4 next.  We initialize it to
	 * its default Values of 0x00000000 because there are not WOL masks
	 * as of this time.
	 */
	writel(0, &rxmac->mask0_word0);
	writel(0, &rxmac->mask0_word1);
	writel(0, &rxmac->mask0_word2);
	writel(0, &rxmac->mask0_word3);

	writel(0, &rxmac->mask1_word0);
	writel(0, &rxmac->mask1_word1);
	writel(0, &rxmac->mask1_word2);
	writel(0, &rxmac->mask1_word3);

	writel(0, &rxmac->mask2_word0);
	writel(0, &rxmac->mask2_word1);
	writel(0, &rxmac->mask2_word2);
	writel(0, &rxmac->mask2_word3);

	writel(0, &rxmac->mask3_word0);
	writel(0, &rxmac->mask3_word1);
	writel(0, &rxmac->mask3_word2);
	writel(0, &rxmac->mask3_word3);

	writel(0, &rxmac->mask4_word0);
	writel(0, &rxmac->mask4_word1);
	writel(0, &rxmac->mask4_word2);
	writel(0, &rxmac->mask4_word3);

	/* Lets setup the WOL Source Address */
	sa_lo = (adapter->addr[2] << ET_WOL_LO_SA3_SHIFT) |
		(adapter->addr[3] << ET_WOL_LO_SA4_SHIFT) |
		(adapter->addr[4] << ET_WOL_LO_SA5_SHIFT) |
		 adapter->addr[5];
	writel(sa_lo, &rxmac->sa_lo);

	sa_hi = (u32) (adapter->addr[0] << ET_WOL_HI_SA1_SHIFT) |
		       adapter->addr[1];
	writel(sa_hi, &rxmac->sa_hi);

	/* Disable all Packet Filtering */
	writel(0, &rxmac->pf_ctrl);

	/* Let's initialize the Unicast Packet filtering address */
	if (adapter->packet_filter & ET131X_PACKET_TYPE_DIRECTED) {
		et1310_setup_device_for_unicast(adapter);
		pf_ctrl |= 4;	/* Unicast filter */
	} else {
		writel(0, &rxmac->uni_pf_addr1);
		writel(0, &rxmac->uni_pf_addr2);
		writel(0, &rxmac->uni_pf_addr3);
	}

	/* Let's initialize the Multicast hash */
	if (!(adapter->packet_filter & ET131X_PACKET_TYPE_ALL_MULTICAST)) {
		pf_ctrl |= 2;	/* Multicast filter */
		et1310_setup_device_for_multicast(adapter);
	}

	/* Runt packet filtering.  Didn't work in version A silicon. */
	pf_ctrl |= (NIC_MIN_PACKET_SIZE + 4) << 16;
	pf_ctrl |= 8;	/* Fragment filter */

	if (adapter->registry_jumbo_packet > 8192)
		/* In order to transmit jumbo packets greater than 8k, the
		 * FIFO between RxMAC and RxDMA needs to be reduced in size
		 * to (16k - Jumbo packet size).  In order to implement this,
		 * we must use "cut through" mode in the RxMAC, which chops
		 * packets down into segments which are (max_size * 16).  In
		 * this case we selected 256 bytes, since this is the size of
		 * the PCI-Express TLP's that the 1310 uses.
		 *
		 * seg_en on, fc_en off, size 0x10
		 */
		writel(0x41, &rxmac->mcif_ctrl_max_seg);
	else
		writel(0, &rxmac->mcif_ctrl_max_seg);

	/* Initialize the MCIF water marks */
	writel(0, &rxmac->mcif_water_mark);

	/*  Initialize the MIF control */
	writel(0, &rxmac->mif_ctrl);

	/* Initialize the Space Available Register */
	writel(0, &rxmac->space_avail);

	/* Initialize the the mif_ctrl register
	 * bit 3:  Receive code error. One or more nibbles were signaled as
	 *	   errors  during the reception of the packet.  Clear this
	 *	   bit in Gigabit, set it in 100Mbit.  This was derived
	 *	   experimentally at UNH.
	 * bit 4:  Receive CRC error. The packet's CRC did not match the
	 *	   internally generated CRC.
	 * bit 5:  Receive length check error. Indicates that frame length
	 *	   field value in the packet does not match the actual data
	 *	   byte length and is not a type field.
	 * bit 16: Receive frame truncated.
	 * bit 17: Drop packet enable
	 */
	if (phydev && phydev->speed == SPEED_100)
		writel(0x30038, &rxmac->mif_ctrl);
	else
		writel(0x30030, &rxmac->mif_ctrl);

	/* Finally we initialize RxMac to be enabled & WOL disabled.  Packet
	 * filter is always enabled since it is where the runt packets are
	 * supposed to be dropped.  For version A silicon, runt packet
	 * dropping doesn't work, so it is disabled in the pf_ctrl register,
	 * but we still leave the packet filter on.
	 */
	writel(pf_ctrl, &rxmac->pf_ctrl);
	writel(0x9, &rxmac->ctrl);
}

static void et1310_config_txmac_regs(struct et131x_adapter *adapter)
{
	struct txmac_regs __iomem *txmac = &adapter->regs->txmac;

	/* We need to update the Control Frame Parameters
	 * cfpt - control frame pause timer set to 64 (0x40)
	 * cfep - control frame extended pause timer set to 0x0
	 */
	if (adapter->flowcontrol == FLOW_NONE)
		writel(0, &txmac->cf_param);
	else
		writel(0x40, &txmac->cf_param);
}

static void et1310_config_macstat_regs(struct et131x_adapter *adapter)
{
	struct macstat_regs __iomem *macstat =
		&adapter->regs->macstat;

	/* Next we need to initialize all the macstat registers to zero on
	 * the device.
	 */
	writel(0, &macstat->txrx_0_64_byte_frames);
	writel(0, &macstat->txrx_65_127_byte_frames);
	writel(0, &macstat->txrx_128_255_byte_frames);
	writel(0, &macstat->txrx_256_511_byte_frames);
	writel(0, &macstat->txrx_512_1023_byte_frames);
	writel(0, &macstat->txrx_1024_1518_byte_frames);
	writel(0, &macstat->txrx_1519_1522_gvln_frames);

	writel(0, &macstat->rx_bytes);
	writel(0, &macstat->rx_packets);
	writel(0, &macstat->rx_fcs_errs);
	writel(0, &macstat->rx_multicast_packets);
	writel(0, &macstat->rx_broadcast_packets);
	writel(0, &macstat->rx_control_frames);
	writel(0, &macstat->rx_pause_frames);
	writel(0, &macstat->rx_unknown_opcodes);
	writel(0, &macstat->rx_align_errs);
	writel(0, &macstat->rx_frame_len_errs);
	writel(0, &macstat->rx_code_errs);
	writel(0, &macstat->rx_carrier_sense_errs);
	writel(0, &macstat->rx_undersize_packets);
	writel(0, &macstat->rx_oversize_packets);
	writel(0, &macstat->rx_fragment_packets);
	writel(0, &macstat->rx_jabbers);
	writel(0, &macstat->rx_drops);

	writel(0, &macstat->tx_bytes);
	writel(0, &macstat->tx_packets);
	writel(0, &macstat->tx_multicast_packets);
	writel(0, &macstat->tx_broadcast_packets);
	writel(0, &macstat->tx_pause_frames);
	writel(0, &macstat->tx_deferred);
	writel(0, &macstat->tx_excessive_deferred);
	writel(0, &macstat->tx_single_collisions);
	writel(0, &macstat->tx_multiple_collisions);
	writel(0, &macstat->tx_late_collisions);
	writel(0, &macstat->tx_excessive_collisions);
	writel(0, &macstat->tx_total_collisions);
	writel(0, &macstat->tx_pause_honored_frames);
	writel(0, &macstat->tx_drops);
	writel(0, &macstat->tx_jabbers);
	writel(0, &macstat->tx_fcs_errs);
	writel(0, &macstat->tx_control_frames);
	writel(0, &macstat->tx_oversize_frames);
	writel(0, &macstat->tx_undersize_frames);
	writel(0, &macstat->tx_fragments);
	writel(0, &macstat->carry_reg1);
	writel(0, &macstat->carry_reg2);

	/* Unmask any counters that we want to track the overflow of.
	 * Initially this will be all counters.  It may become clear later
	 * that we do not need to track all counters.
	 */
	writel(0xFFFFBE32, &macstat->carry_reg1_mask);
	writel(0xFFFE7E8B, &macstat->carry_reg2_mask);
}

/**
 * et131x_phy_mii_read - Read from the PHY through the MII Interface on the MAC
 * @adapter: pointer to our private adapter structure
 * @addr: the address of the transceiver
 * @reg: the register to read
 * @value: pointer to a 16-bit value in which the value will be stored
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
static int et131x_phy_mii_read(struct et131x_adapter *adapter, u8 addr,
	      u8 reg, u16 *value)
{
	struct mac_regs __iomem *mac = &adapter->regs->mac;
	int status = 0;
	u32 delay = 0;
	u32 mii_addr;
	u32 mii_cmd;
	u32 mii_indicator;

	/* Save a local copy of the registers we are dealing with so we can
	 * set them back
	 */
	mii_addr = readl(&mac->mii_mgmt_addr);
	mii_cmd = readl(&mac->mii_mgmt_cmd);

	/* Stop the current operation */
	writel(0, &mac->mii_mgmt_cmd);

	/* Set up the register we need to read from on the correct PHY */
	writel(MII_ADDR(addr, reg), &mac->mii_mgmt_addr);

	writel(0x1, &mac->mii_mgmt_cmd);

	do {
		udelay(50);
		delay++;
		mii_indicator = readl(&mac->mii_mgmt_indicator);
	} while ((mii_indicator & MGMT_WAIT) && delay < 50);

	/* If we hit the max delay, we could not read the register */
	if (delay == 50) {
		dev_warn(&adapter->pdev->dev,
			    "reg 0x%08x could not be read\n", reg);
		dev_warn(&adapter->pdev->dev, "status is  0x%08x\n",
			    mii_indicator);

		status = -EIO;
	}

	/* If we hit here we were able to read the register and we need to
	 * return the value to the caller */
	*value = readl(&mac->mii_mgmt_stat) & 0xFFFF;

	/* Stop the read operation */
	writel(0, &mac->mii_mgmt_cmd);

	/* set the registers we touched back to the state at which we entered
	 * this function
	 */
	writel(mii_addr, &mac->mii_mgmt_addr);
	writel(mii_cmd, &mac->mii_mgmt_cmd);

	return status;
}

static int et131x_mii_read(struct et131x_adapter *adapter, u8 reg, u16 *value)
{
	struct phy_device *phydev = adapter->phydev;

	if (!phydev)
		return -EIO;

	return et131x_phy_mii_read(adapter, phydev->addr, reg, value);
}

/**
 * et131x_mii_write - Write to a PHY register through the MII interface of the MAC
 * @adapter: pointer to our private adapter structure
 * @reg: the register to read
 * @value: 16-bit value to write
 *
 * FIXME: one caller in netdev still
 *
 * Return 0 on success, errno on failure (as defined in errno.h)
 */
static int et131x_mii_write(struct et131x_adapter *adapter, u8 reg, u16 value)
{
	struct mac_regs __iomem *mac = &adapter->regs->mac;
	struct phy_device *phydev = adapter->phydev;
	int status = 0;
	u8 addr;
	u32 delay = 0;
	u32 mii_addr;
	u32 mii_cmd;
	u32 mii_indicator;

	if (!phydev)
		return -EIO;

	addr = phydev->addr;

	/* Save a local copy of the registers we are dealing with so we can
	 * set them back
	 */
	mii_addr = readl(&mac->mii_mgmt_addr);
	mii_cmd = readl(&mac->mii_mgmt_cmd);

	/* Stop the current operation */
	writel(0, &mac->mii_mgmt_cmd);

	/* Set up the register we need to write to on the correct PHY */
	writel(MII_ADDR(addr, reg), &mac->mii_mgmt_addr);

	/* Add the value to write to the registers to the mac */
	writel(value, &mac->mii_mgmt_ctrl);

	do {
		udelay(50);
		delay++;
		mii_indicator = readl(&mac->mii_mgmt_indicator);
	} while ((mii_indicator & MGMT_BUSY) && delay < 100);

	/* If we hit the max delay, we could not write the register */
	if (delay == 100) {
		u16 tmp;

		dev_warn(&adapter->pdev->dev,
		    "reg 0x%08x could not be written", reg);
		dev_warn(&adapter->pdev->dev, "status is  0x%08x\n",
			    mii_indicator);
		dev_warn(&adapter->pdev->dev, "command is  0x%08x\n",
			    readl(&mac->mii_mgmt_cmd));

		et131x_mii_read(adapter, reg, &tmp);

		status = -EIO;
	}
	/* Stop the write operation */
	writel(0, &mac->mii_mgmt_cmd);

	/*
	 * set the registers we touched back to the state at which we entered
	 * this function
	 */
	writel(mii_addr, &mac->mii_mgmt_addr);
	writel(mii_cmd, &mac->mii_mgmt_cmd);

	return status;
}

/* Still used from _mac for BIT_READ */
static void et1310_phy_access_mii_bit(struct et131x_adapter *adapter,
				      u16 action, u16 regnum, u16 bitnum,
				      u8 *value)
{
	u16 reg;
	u16 mask = 0x0001 << bitnum;

	/* Read the requested register */
	et131x_mii_read(adapter, regnum, &reg);

	switch (action) {
	case TRUEPHY_BIT_READ:
		*value = (reg & mask) >> bitnum;
		break;

	case TRUEPHY_BIT_SET:
		et131x_mii_write(adapter, regnum, reg | mask);
		break;

	case TRUEPHY_BIT_CLEAR:
		et131x_mii_write(adapter, regnum, reg & ~mask);
		break;

	default:
		break;
	}
}

static void et1310_config_flow_control(struct et131x_adapter *adapter)
{
	struct phy_device *phydev = adapter->phydev;

	if (phydev->duplex == DUPLEX_HALF) {
		adapter->flowcontrol = FLOW_NONE;
	} else {
		char remote_pause, remote_async_pause;

		et1310_phy_access_mii_bit(adapter,
				TRUEPHY_BIT_READ, 5, 10, &remote_pause);
		et1310_phy_access_mii_bit(adapter,
				TRUEPHY_BIT_READ, 5, 11,
				&remote_async_pause);

		if ((remote_pause == TRUEPHY_BIT_SET) &&
		    (remote_async_pause == TRUEPHY_BIT_SET)) {
			adapter->flowcontrol = adapter->wanted_flow;
		} else if ((remote_pause == TRUEPHY_BIT_SET) &&
			   (remote_async_pause == TRUEPHY_BIT_CLEAR)) {
			if (adapter->wanted_flow == FLOW_BOTH)
				adapter->flowcontrol = FLOW_BOTH;
			else
				adapter->flowcontrol = FLOW_NONE;
		} else if ((remote_pause == TRUEPHY_BIT_CLEAR) &&
			   (remote_async_pause == TRUEPHY_BIT_CLEAR)) {
			adapter->flowcontrol = FLOW_NONE;
		} else {/* if (remote_pause == TRUEPHY_CLEAR_BIT &&
			       remote_async_pause == TRUEPHY_SET_BIT) */
			if (adapter->wanted_flow == FLOW_BOTH)
				adapter->flowcontrol = FLOW_RXONLY;
			else
				adapter->flowcontrol = FLOW_NONE;
		}
	}
}

/**
 * et1310_update_macstat_host_counters - Update the local copy of the statistics
 * @adapter: pointer to the adapter structure
 */
static void et1310_update_macstat_host_counters(struct et131x_adapter *adapter)
{
	struct ce_stats *stats = &adapter->stats;
	struct macstat_regs __iomem *macstat =
		&adapter->regs->macstat;

	stats->tx_collisions	       += readl(&macstat->tx_total_collisions);
	stats->tx_first_collisions     += readl(&macstat->tx_single_collisions);
	stats->tx_deferred	       += readl(&macstat->tx_deferred);
	stats->tx_excessive_collisions +=
				readl(&macstat->tx_multiple_collisions);
	stats->tx_late_collisions      += readl(&macstat->tx_late_collisions);
	stats->tx_underflows	       += readl(&macstat->tx_undersize_frames);
	stats->tx_max_pkt_errs	       += readl(&macstat->tx_oversize_frames);

	stats->rx_align_errs        += readl(&macstat->rx_align_errs);
	stats->rx_crc_errs          += readl(&macstat->rx_code_errs);
	stats->rcvd_pkts_dropped    += readl(&macstat->rx_drops);
	stats->rx_overflows         += readl(&macstat->rx_oversize_packets);
	stats->rx_code_violations   += readl(&macstat->rx_fcs_errs);
	stats->rx_length_errs       += readl(&macstat->rx_frame_len_errs);
	stats->rx_other_errs        += readl(&macstat->rx_fragment_packets);
}

/**
 * et1310_handle_macstat_interrupt
 * @adapter: pointer to the adapter structure
 *
 * One of the MACSTAT counters has wrapped.  Update the local copy of
 * the statistics held in the adapter structure, checking the "wrap"
 * bit for each counter.
 */
static void et1310_handle_macstat_interrupt(struct et131x_adapter *adapter)
{
	u32 carry_reg1;
	u32 carry_reg2;

	/* Read the interrupt bits from the register(s).  These are Clear On
	 * Write.
	 */
	carry_reg1 = readl(&adapter->regs->macstat.carry_reg1);
	carry_reg2 = readl(&adapter->regs->macstat.carry_reg2);

	writel(carry_reg1, &adapter->regs->macstat.carry_reg1);
	writel(carry_reg2, &adapter->regs->macstat.carry_reg2);

	/* We need to do update the host copy of all the MAC_STAT counters.
	 * For each counter, check it's overflow bit.  If the overflow bit is
	 * set, then increment the host version of the count by one complete
	 * revolution of the counter.  This routine is called when the counter
	 * block indicates that one of the counters has wrapped.
	 */
	if (carry_reg1 & (1 << 14))
		adapter->stats.rx_code_violations	+= COUNTER_WRAP_16_BIT;
	if (carry_reg1 & (1 << 8))
		adapter->stats.rx_align_errs	+= COUNTER_WRAP_12_BIT;
	if (carry_reg1 & (1 << 7))
		adapter->stats.rx_length_errs	+= COUNTER_WRAP_16_BIT;
	if (carry_reg1 & (1 << 2))
		adapter->stats.rx_other_errs	+= COUNTER_WRAP_16_BIT;
	if (carry_reg1 & (1 << 6))
		adapter->stats.rx_crc_errs	+= COUNTER_WRAP_16_BIT;
	if (carry_reg1 & (1 << 3))
		adapter->stats.rx_overflows	+= COUNTER_WRAP_16_BIT;
	if (carry_reg1 & (1 << 0))
		adapter->stats.rcvd_pkts_dropped	+= COUNTER_WRAP_16_BIT;
	if (carry_reg2 & (1 << 16))
		adapter->stats.tx_max_pkt_errs	+= COUNTER_WRAP_12_BIT;
	if (carry_reg2 & (1 << 15))
		adapter->stats.tx_underflows	+= COUNTER_WRAP_12_BIT;
	if (carry_reg2 & (1 << 6))
		adapter->stats.tx_first_collisions += COUNTER_WRAP_12_BIT;
	if (carry_reg2 & (1 << 8))
		adapter->stats.tx_deferred	+= COUNTER_WRAP_12_BIT;
	if (carry_reg2 & (1 << 5))
		adapter->stats.tx_excessive_collisions += COUNTER_WRAP_12_BIT;
	if (carry_reg2 & (1 << 4))
		adapter->stats.tx_late_collisions	+= COUNTER_WRAP_12_BIT;
	if (carry_reg2 & (1 << 2))
		adapter->stats.tx_collisions	+= COUNTER_WRAP_12_BIT;
}

static int et131x_mdio_read(struct mii_bus *bus, int phy_addr, int reg)
{
	struct net_device *netdev = bus->priv;
	struct et131x_adapter *adapter = netdev_priv(netdev);
	u16 value;
	int ret;

	ret = et131x_phy_mii_read(adapter, phy_addr, reg, &value);

	if (ret < 0)
		return ret;
	else
		return value;
}

static int et131x_mdio_write(struct mii_bus *bus, int phy_addr,
			     int reg, u16 value)
{
	struct net_device *netdev = bus->priv;
	struct et131x_adapter *adapter = netdev_priv(netdev);

	return et131x_mii_write(adapter, reg, value);
}

static int et131x_mdio_reset(struct mii_bus *bus)
{
	struct net_device *netdev = bus->priv;
	struct et131x_adapter *adapter = netdev_priv(netdev);

	et131x_mii_write(adapter, MII_BMCR, BMCR_RESET);

	return 0;
}

/**
 *	et1310_phy_power_down	-	PHY power control
 *	@adapter: device to control
 *	@down: true for off/false for back on
 *
 *	one hundred, ten, one thousand megs
 *	How would you like to have your LAN accessed
 *	Can't you see that this code processed
 *	Phy power, phy power..
 */
static void et1310_phy_power_down(struct et131x_adapter *adapter, bool down)
{
	u16 data;

	et131x_mii_read(adapter, MII_BMCR, &data);
	data &= ~BMCR_PDOWN;
	if (down)
		data |= BMCR_PDOWN;
	et131x_mii_write(adapter, MII_BMCR, data);
}

/**
 * et131x_xcvr_init - Init the phy if we are setting it into force mode
 * @adapter: pointer to our private adapter structure
 *
 */
static void et131x_xcvr_init(struct et131x_adapter *adapter)
{
	u16 imr;
	u16 isr;
	u16 lcr2;

	et131x_mii_read(adapter, PHY_INTERRUPT_STATUS, &isr);
	et131x_mii_read(adapter, PHY_INTERRUPT_MASK, &imr);

	/* Set the link status interrupt only.  Bad behavior when link status
	 * and auto neg are set, we run into a nested interrupt problem
	 */
	imr |= (ET_PHY_INT_MASK_AUTONEGSTAT |
		ET_PHY_INT_MASK_LINKSTAT |
		ET_PHY_INT_MASK_ENABLE);

	et131x_mii_write(adapter, PHY_INTERRUPT_MASK, imr);

	/* Set the LED behavior such that LED 1 indicates speed (off =
	 * 10Mbits, blink = 100Mbits, on = 1000Mbits) and LED 2 indicates
	 * link and activity (on for link, blink off for activity).
	 *
	 * NOTE: Some customizations have been added here for specific
	 * vendors; The LED behavior is now determined by vendor data in the
	 * EEPROM. However, the above description is the default.
	 */
	if ((adapter->eeprom_data[1] & 0x4) == 0) {
		et131x_mii_read(adapter, PHY_LED_2, &lcr2);

		lcr2 &= (ET_LED2_LED_100TX | ET_LED2_LED_1000T);
		lcr2 |= (LED_VAL_LINKON_ACTIVE << LED_LINK_SHIFT);

		if ((adapter->eeprom_data[1] & 0x8) == 0)
			lcr2 |= (LED_VAL_1000BT_100BTX << LED_TXRX_SHIFT);
		else
			lcr2 |= (LED_VAL_LINKON << LED_TXRX_SHIFT);

		et131x_mii_write(adapter, PHY_LED_2, lcr2);
	}
}

/**
 * et131x_configure_global_regs	-	configure JAGCore global regs
 * @adapter: pointer to our adapter structure
 *
 * Used to configure the global registers on the JAGCore
 */
static void et131x_configure_global_regs(struct et131x_adapter *adapter)
{
	struct global_regs __iomem *regs = &adapter->regs->global;

	writel(0, &regs->rxq_start_addr);
	writel(INTERNAL_MEM_SIZE - 1, &regs->txq_end_addr);

	if (adapter->registry_jumbo_packet < 2048) {
		/* Tx / RxDMA and Tx/Rx MAC interfaces have a 1k word
		 * block of RAM that the driver can split between Tx
		 * and Rx as it desires.  Our default is to split it
		 * 50/50:
		 */
		writel(PARM_RX_MEM_END_DEF, &regs->rxq_end_addr);
		writel(PARM_RX_MEM_END_DEF + 1, &regs->txq_start_addr);
	} else if (adapter->registry_jumbo_packet < 8192) {
		/* For jumbo packets > 2k but < 8k, split 50-50. */
		writel(INTERNAL_MEM_RX_OFFSET, &regs->rxq_end_addr);
		writel(INTERNAL_MEM_RX_OFFSET + 1, &regs->txq_start_addr);
	} else {
		/* 9216 is the only packet size greater than 8k that
		 * is available. The Tx buffer has to be big enough
		 * for one whole packet on the Tx side. We'll make
		 * the Tx 9408, and give the rest to Rx
		 */
		writel(0x01b3, &regs->rxq_end_addr);
		writel(0x01b4, &regs->txq_start_addr);
	}

	/* Initialize the loopback register. Disable all loopbacks. */
	writel(0, &regs->loopback);

	/* MSI Register */
	writel(0, &regs->msi_config);

	/* By default, disable the watchdog timer.  It will be enabled when
	 * a packet is queued.
	 */
	writel(0, &regs->watchdog_timer);
}

/**
 * et131x_config_rx_dma_regs - Start of Rx_DMA init sequence
 * @adapter: pointer to our adapter structure
 */
static void et131x_config_rx_dma_regs(struct et131x_adapter *adapter)
{
	struct rxdma_regs __iomem *rx_dma = &adapter->regs->rxdma;
	struct rx_ring *rx_local = &adapter->rx_ring;
	struct fbr_desc *fbr_entry;
	u32 entry;
	u32 psr_num_des;
	unsigned long flags;

	/* Halt RXDMA to perform the reconfigure.  */
	et131x_rx_dma_disable(adapter);

	/* Load the completion writeback physical address
	 *
	 * NOTE : dma_alloc_coherent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here
	 * before storing the adjusted address.
	 */
	writel((u32) ((u64)rx_local->rx_status_bus >> 32),
	       &rx_dma->dma_wb_base_hi);
	writel((u32) rx_local->rx_status_bus, &rx_dma->dma_wb_base_lo);

	memset(rx_local->rx_status_block, 0, sizeof(struct rx_status_block));

	/* Set the address and parameters of the packet status ring into the
	 * 1310's registers
	 */
	writel((u32) ((u64)rx_local->ps_ring_physaddr >> 32),
	       &rx_dma->psr_base_hi);
	writel((u32) rx_local->ps_ring_physaddr, &rx_dma->psr_base_lo);
	writel(rx_local->psr_num_entries - 1, &rx_dma->psr_num_des);
	writel(0, &rx_dma->psr_full_offset);

	psr_num_des = readl(&rx_dma->psr_num_des) & 0xFFF;
	writel((psr_num_des * LO_MARK_PERCENT_FOR_PSR) / 100,
	       &rx_dma->psr_min_des);

	spin_lock_irqsave(&adapter->rcv_lock, flags);

	/* These local variables track the PSR in the adapter structure */
	rx_local->local_psr_full = 0;

	/* Now's the best time to initialize FBR1 contents */
	fbr_entry = (struct fbr_desc *) rx_local->fbr[0]->ring_virtaddr;
	for (entry = 0; entry < rx_local->fbr[0]->num_entries; entry++) {
		fbr_entry->addr_hi = rx_local->fbr[0]->bus_high[entry];
		fbr_entry->addr_lo = rx_local->fbr[0]->bus_low[entry];
		fbr_entry->word2 = entry;
		fbr_entry++;
	}

	/* Set the address and parameters of Free buffer ring 1 (and 0 if
	 * required) into the 1310's registers
	 */
	writel((u32) (rx_local->fbr[0]->real_physaddr >> 32),
	       &rx_dma->fbr1_base_hi);
	writel((u32) rx_local->fbr[0]->real_physaddr, &rx_dma->fbr1_base_lo);
	writel(rx_local->fbr[0]->num_entries - 1, &rx_dma->fbr1_num_des);
	writel(ET_DMA10_WRAP, &rx_dma->fbr1_full_offset);

	/* This variable tracks the free buffer ring 1 full position, so it
	 * has to match the above.
	 */
	rx_local->fbr[0]->local_full = ET_DMA10_WRAP;
	writel(
	   ((rx_local->fbr[0]->num_entries * LO_MARK_PERCENT_FOR_RX) / 100) - 1,
	   &rx_dma->fbr1_min_des);

#ifdef USE_FBR0
	/* Now's the best time to initialize FBR0 contents */
	fbr_entry = (struct fbr_desc *) rx_local->fbr[1]->ring_virtaddr;
	for (entry = 0; entry < rx_local->fbr[1]->num_entries; entry++) {
		fbr_entry->addr_hi = rx_local->fbr[1]->bus_high[entry];
		fbr_entry->addr_lo = rx_local->fbr[1]->bus_low[entry];
		fbr_entry->word2 = entry;
		fbr_entry++;
	}

	writel((u32) (rx_local->fbr[1]->real_physaddr >> 32),
	       &rx_dma->fbr0_base_hi);
	writel((u32) rx_local->fbr[1]->real_physaddr, &rx_dma->fbr0_base_lo);
	writel(rx_local->fbr[1]->num_entries - 1, &rx_dma->fbr0_num_des);
	writel(ET_DMA10_WRAP, &rx_dma->fbr0_full_offset);

	/* This variable tracks the free buffer ring 0 full position, so it
	 * has to match the above.
	 */
	rx_local->fbr[1]->local_full = ET_DMA10_WRAP;
	writel(
	   ((rx_local->fbr[1]->num_entries * LO_MARK_PERCENT_FOR_RX) / 100) - 1,
	   &rx_dma->fbr0_min_des);
#endif

	/* Program the number of packets we will receive before generating an
	 * interrupt.
	 * For version B silicon, this value gets updated once autoneg is
	 *complete.
	 */
	writel(PARM_RX_NUM_BUFS_DEF, &rx_dma->num_pkt_done);

	/* The "time_done" is not working correctly to coalesce interrupts
	 * after a given time period, but rather is giving us an interrupt
	 * regardless of whether we have received packets.
	 * This value gets updated once autoneg is complete.
	 */
	writel(PARM_RX_TIME_INT_DEF, &rx_dma->max_pkt_time);

	spin_unlock_irqrestore(&adapter->rcv_lock, flags);
}

/**
 * et131x_config_tx_dma_regs - Set up the tx dma section of the JAGCore.
 * @adapter: pointer to our private adapter structure
 *
 * Configure the transmit engine with the ring buffers we have created
 * and prepare it for use.
 */
static void et131x_config_tx_dma_regs(struct et131x_adapter *adapter)
{
	struct txdma_regs __iomem *txdma = &adapter->regs->txdma;

	/* Load the hardware with the start of the transmit descriptor ring. */
	writel((u32) ((u64)adapter->tx_ring.tx_desc_ring_pa >> 32),
	       &txdma->pr_base_hi);
	writel((u32) adapter->tx_ring.tx_desc_ring_pa,
	       &txdma->pr_base_lo);

	/* Initialise the transmit DMA engine */
	writel(NUM_DESC_PER_RING_TX - 1, &txdma->pr_num_des);

	/* Load the completion writeback physical address */
	writel((u32)((u64)adapter->tx_ring.tx_status_pa >> 32),
						&txdma->dma_wb_base_hi);
	writel((u32)adapter->tx_ring.tx_status_pa, &txdma->dma_wb_base_lo);

	*adapter->tx_ring.tx_status = 0;

	writel(0, &txdma->service_request);
	adapter->tx_ring.send_idx = 0;
}

/**
 * et131x_adapter_setup - Set the adapter up as per cassini+ documentation
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
static void et131x_adapter_setup(struct et131x_adapter *adapter)
{
	/* Configure the JAGCore */
	et131x_configure_global_regs(adapter);

	et1310_config_mac_regs1(adapter);

	/* Configure the MMC registers */
	/* All we need to do is initialize the Memory Control Register */
	writel(ET_MMC_ENABLE, &adapter->regs->mmc.mmc_ctrl);

	et1310_config_rxmac_regs(adapter);
	et1310_config_txmac_regs(adapter);

	et131x_config_rx_dma_regs(adapter);
	et131x_config_tx_dma_regs(adapter);

	et1310_config_macstat_regs(adapter);

	et1310_phy_power_down(adapter, 0);
	et131x_xcvr_init(adapter);
}

/**
 * et131x_soft_reset - Issue a soft reset to the hardware, complete for ET1310
 * @adapter: pointer to our private adapter structure
 */
static void et131x_soft_reset(struct et131x_adapter *adapter)
{
	/* Disable MAC Core */
	writel(0xc00f0000, &adapter->regs->mac.cfg1);

	/* Set everything to a reset value */
	writel(0x7F, &adapter->regs->global.sw_reset);
	writel(0x000f0000, &adapter->regs->mac.cfg1);
	writel(0x00000000, &adapter->regs->mac.cfg1);
}

/**
 *	et131x_enable_interrupts	-	enable interrupt
 *	@adapter: et131x device
 *
 *	Enable the appropriate interrupts on the ET131x according to our
 *	configuration
 */
static void et131x_enable_interrupts(struct et131x_adapter *adapter)
{
	u32 mask;

	/* Enable all global interrupts */
	if (adapter->flowcontrol == FLOW_TXONLY ||
			    adapter->flowcontrol == FLOW_BOTH)
		mask = INT_MASK_ENABLE;
	else
		mask = INT_MASK_ENABLE_NO_FLOW;

	writel(mask, &adapter->regs->global.int_mask);
}

/**
 *	et131x_disable_interrupts	-	interrupt disable
 *	@adapter: et131x device
 *
 *	Block all interrupts from the et131x device at the device itself
 */
static void et131x_disable_interrupts(struct et131x_adapter *adapter)
{
	/* Disable all global interrupts */
	writel(INT_MASK_DISABLE, &adapter->regs->global.int_mask);
}

/**
 * et131x_tx_dma_disable - Stop of Tx_DMA on the ET1310
 * @adapter: pointer to our adapter structure
 */
static void et131x_tx_dma_disable(struct et131x_adapter *adapter)
{
	/* Setup the tramsmit dma configuration register */
	writel(ET_TXDMA_CSR_HALT|ET_TXDMA_SNGL_EPKT,
					&adapter->regs->txdma.csr);
}

/**
 * et131x_enable_txrx - Enable tx/rx queues
 * @netdev: device to be enabled
 */
static void et131x_enable_txrx(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	/* Enable the Tx and Rx DMA engines (if not already enabled) */
	et131x_rx_dma_enable(adapter);
	et131x_tx_dma_enable(adapter);

	/* Enable device interrupts */
	if (adapter->flags & fMP_ADAPTER_INTERRUPT_IN_USE)
		et131x_enable_interrupts(adapter);

	/* We're ready to move some data, so start the queue */
	netif_start_queue(netdev);
}

/**
 * et131x_disable_txrx - Disable tx/rx queues
 * @netdev: device to be disabled
 */
static void et131x_disable_txrx(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	/* First thing is to stop the queue */
	netif_stop_queue(netdev);

	/* Stop the Tx and Rx DMA engines */
	et131x_rx_dma_disable(adapter);
	et131x_tx_dma_disable(adapter);

	/* Disable device interrupts */
	et131x_disable_interrupts(adapter);
}

/**
 * et131x_init_send - Initialize send data structures
 * @adapter: pointer to our private adapter structure
 */
static void et131x_init_send(struct et131x_adapter *adapter)
{
	struct tcb *tcb;
	u32 ct;
	struct tx_ring *tx_ring;

	/* Setup some convenience pointers */
	tx_ring = &adapter->tx_ring;
	tcb = adapter->tx_ring.tcb_ring;

	tx_ring->tcb_qhead = tcb;

	memset(tcb, 0, sizeof(struct tcb) * NUM_TCB);

	/* Go through and set up each TCB */
	for (ct = 0; ct++ < NUM_TCB; tcb++)
		/* Set the link pointer in HW TCB to the next TCB in the
		 * chain
		 */
		tcb->next = tcb + 1;

	/* Set the  tail pointer */
	tcb--;
	tx_ring->tcb_qtail = tcb;
	tcb->next = NULL;
	/* Curr send queue should now be empty */
	tx_ring->send_head = NULL;
	tx_ring->send_tail = NULL;
}

/**
 * et1310_enable_phy_coma - called when network cable is unplugged
 * @adapter: pointer to our adapter structure
 *
 * driver receive an phy status change interrupt while in D0 and check that
 * phy_status is down.
 *
 *          -- gate off JAGCore;
 *          -- set gigE PHY in Coma mode
 *          -- wake on phy_interrupt; Perform software reset JAGCore,
 *             re-initialize jagcore and gigE PHY
 *
 *      Add D0-ASPM-PhyLinkDown Support:
 *          -- while in D0, when there is a phy_interrupt indicating phy link
 *             down status, call the MPSetPhyComa routine to enter this active
 *             state power saving mode
 *          -- while in D0-ASPM-PhyLinkDown mode, when there is a phy_interrupt
 *       indicating linkup status, call the MPDisablePhyComa routine to
 *             restore JAGCore and gigE PHY
 */
static void et1310_enable_phy_coma(struct et131x_adapter *adapter)
{
	unsigned long flags;
	u32 pmcsr;

	pmcsr = readl(&adapter->regs->global.pm_csr);

	/* Save the GbE PHY speed and duplex modes. Need to restore this
	 * when cable is plugged back in
	 */
	/*
	 * TODO - when PM is re-enabled, check if we need to
	 * perform a similar task as this -
	 * adapter->pdown_speed = adapter->ai_force_speed;
	 * adapter->pdown_duplex = adapter->ai_force_duplex;
	 */

	/* Stop sending packets. */
	spin_lock_irqsave(&adapter->send_hw_lock, flags);
	adapter->flags |= fMP_ADAPTER_LOWER_POWER;
	spin_unlock_irqrestore(&adapter->send_hw_lock, flags);

	/* Wait for outstanding Receive packets */

	et131x_disable_txrx(adapter->netdev);

	/* Gate off JAGCore 3 clock domains */
	pmcsr &= ~ET_PMCSR_INIT;
	writel(pmcsr, &adapter->regs->global.pm_csr);

	/* Program gigE PHY in to Coma mode */
	pmcsr |= ET_PM_PHY_SW_COMA;
	writel(pmcsr, &adapter->regs->global.pm_csr);
}

/**
 * et1310_disable_phy_coma - Disable the Phy Coma Mode
 * @adapter: pointer to our adapter structure
 */
static void et1310_disable_phy_coma(struct et131x_adapter *adapter)
{
	u32 pmcsr;

	pmcsr = readl(&adapter->regs->global.pm_csr);

	/* Disable phy_sw_coma register and re-enable JAGCore clocks */
	pmcsr |= ET_PMCSR_INIT;
	pmcsr &= ~ET_PM_PHY_SW_COMA;
	writel(pmcsr, &adapter->regs->global.pm_csr);

	/* Restore the GbE PHY speed and duplex modes;
	 * Reset JAGCore; re-configure and initialize JAGCore and gigE PHY
	 */
	/* TODO - when PM is re-enabled, check if we need to
	 * perform a similar task as this -
	 * adapter->ai_force_speed = adapter->pdown_speed;
	 * adapter->ai_force_duplex = adapter->pdown_duplex;
	 */

	/* Re-initialize the send structures */
	et131x_init_send(adapter);

	/* Bring the device back to the state it was during init prior to
	 * autonegotiation being complete.  This way, when we get the auto-neg
	 * complete interrupt, we can complete init by calling ConfigMacREGS2.
	 */
	et131x_soft_reset(adapter);

	/* setup et1310 as per the documentation ?? */
	et131x_adapter_setup(adapter);

	/* Allow Tx to restart */
	adapter->flags &= ~fMP_ADAPTER_LOWER_POWER;

	et131x_enable_txrx(adapter->netdev);
}

static inline u32 bump_free_buff_ring(u32 *free_buff_ring, u32 limit)
{
	u32 tmp_free_buff_ring = *free_buff_ring;
	tmp_free_buff_ring++;
	/* This works for all cases where limit < 1024. The 1023 case
	   works because 1023++ is 1024 which means the if condition is not
	   taken but the carry of the bit into the wrap bit toggles the wrap
	   value correctly */
	if ((tmp_free_buff_ring & ET_DMA10_MASK) > limit) {
		tmp_free_buff_ring &= ~ET_DMA10_MASK;
		tmp_free_buff_ring ^= ET_DMA10_WRAP;
	}
	/* For the 1023 case */
	tmp_free_buff_ring &= (ET_DMA10_MASK|ET_DMA10_WRAP);
	*free_buff_ring = tmp_free_buff_ring;
	return tmp_free_buff_ring;
}

/**
 * et131x_align_allocated_memory - Align allocated memory on a given boundary
 * @adapter: pointer to our adapter structure
 * @phys_addr: pointer to Physical address
 * @offset: pointer to the offset variable
 * @mask: correct mask
 */
static void et131x_align_allocated_memory(struct et131x_adapter *adapter,
					  u64 *phys_addr, u64 *offset,
					  u64 mask)
{
	u64 new_addr = *phys_addr & ~mask;

	*offset = 0;

	if (new_addr != *phys_addr) {
		/* Move to next aligned block */
		new_addr += mask + 1;
		/* Return offset for adjusting virt addr */
		*offset = new_addr - *phys_addr;
		/* Return new physical address */
		*phys_addr = new_addr;
	}
}

/**
 * et131x_rx_dma_memory_alloc
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success and errno on failure (as defined in errno.h)
 *
 * Allocates Free buffer ring 1 for sure, free buffer ring 0 if required,
 * and the Packet Status Ring.
 */
static int et131x_rx_dma_memory_alloc(struct et131x_adapter *adapter)
{
	u32 i, j;
	u32 bufsize;
	u32 pktstat_ringsize, fbr_chunksize;
	struct rx_ring *rx_ring;

	/* Setup some convenience pointers */
	rx_ring = &adapter->rx_ring;

	/* Alloc memory for the lookup table */
#ifdef USE_FBR0
	rx_ring->fbr[1] = kmalloc(sizeof(struct fbr_lookup), GFP_KERNEL);
#endif
	rx_ring->fbr[0] = kmalloc(sizeof(struct fbr_lookup), GFP_KERNEL);

	/* The first thing we will do is configure the sizes of the buffer
	 * rings. These will change based on jumbo packet support.  Larger
	 * jumbo packets increases the size of each entry in FBR0, and the
	 * number of entries in FBR0, while at the same time decreasing the
	 * number of entries in FBR1.
	 *
	 * FBR1 holds "large" frames, FBR0 holds "small" frames.  If FBR1
	 * entries are huge in order to accommodate a "jumbo" frame, then it
	 * will have less entries.  Conversely, FBR1 will now be relied upon
	 * to carry more "normal" frames, thus it's entry size also increases
	 * and the number of entries goes up too (since it now carries
	 * "small" + "regular" packets.
	 *
	 * In this scheme, we try to maintain 512 entries between the two
	 * rings. Also, FBR1 remains a constant size - when it's size doubles
	 * the number of entries halves.  FBR0 increases in size, however.
	 */

	if (adapter->registry_jumbo_packet < 2048) {
#ifdef USE_FBR0
		rx_ring->fbr[1]->buffsize = 256;
		rx_ring->fbr[1]->num_entries = 512;
#endif
		rx_ring->fbr[0]->buffsize = 2048;
		rx_ring->fbr[0]->num_entries = 512;
	} else if (adapter->registry_jumbo_packet < 4096) {
#ifdef USE_FBR0
		rx_ring->fbr[1]->buffsize = 512;
		rx_ring->fbr[1]->num_entries = 1024;
#endif
		rx_ring->fbr[0]->buffsize = 4096;
		rx_ring->fbr[0]->num_entries = 512;
	} else {
#ifdef USE_FBR0
		rx_ring->fbr[1]->buffsize = 1024;
		rx_ring->fbr[1]->num_entries = 768;
#endif
		rx_ring->fbr[0]->buffsize = 16384;
		rx_ring->fbr[0]->num_entries = 128;
	}

#ifdef USE_FBR0
	adapter->rx_ring.psr_num_entries =
				adapter->rx_ring.fbr[1]->num_entries +
				adapter->rx_ring.fbr[0]->num_entries;
#else
	adapter->rx_ring.psr_num_entries = adapter->rx_ring.fbr[0]->num_entries;
#endif

	/* Allocate an area of memory for Free Buffer Ring 1 */
	bufsize = (sizeof(struct fbr_desc) * rx_ring->fbr[0]->num_entries) +
									0xfff;
	rx_ring->fbr[0]->ring_virtaddr = dma_alloc_coherent(&adapter->pdev->dev,
					bufsize,
					&rx_ring->fbr[0]->ring_physaddr,
					GFP_KERNEL);
	if (!rx_ring->fbr[0]->ring_virtaddr) {
		dev_err(&adapter->pdev->dev,
			  "Cannot alloc memory for Free Buffer Ring 1\n");
		return -ENOMEM;
	}

	/* Save physical address
	 *
	 * NOTE: dma_alloc_coherent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here
	 * before storing the adjusted address.
	 */
	rx_ring->fbr[0]->real_physaddr = rx_ring->fbr[0]->ring_physaddr;

	/* Align Free Buffer Ring 1 on a 4K boundary */
	et131x_align_allocated_memory(adapter,
				      &rx_ring->fbr[0]->real_physaddr,
				      &rx_ring->fbr[0]->offset, 0x0FFF);

	rx_ring->fbr[0]->ring_virtaddr =
			(void *)((u8 *) rx_ring->fbr[0]->ring_virtaddr +
			rx_ring->fbr[0]->offset);

#ifdef USE_FBR0
	/* Allocate an area of memory for Free Buffer Ring 0 */
	bufsize = (sizeof(struct fbr_desc) * rx_ring->fbr[1]->num_entries) +
									0xfff;
	rx_ring->fbr[1]->ring_virtaddr = dma_alloc_coherent(&adapter->pdev->dev,
						bufsize,
						&rx_ring->fbr[1]->ring_physaddr,
						GFP_KERNEL);
	if (!rx_ring->fbr[1]->ring_virtaddr) {
		dev_err(&adapter->pdev->dev,
			  "Cannot alloc memory for Free Buffer Ring 0\n");
		return -ENOMEM;
	}

	/* Save physical address
	 *
	 * NOTE: dma_alloc_coherent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here before
	 * storing the adjusted address.
	 */
	rx_ring->fbr[1]->real_physaddr = rx_ring->fbr[1]->ring_physaddr;

	/* Align Free Buffer Ring 0 on a 4K boundary */
	et131x_align_allocated_memory(adapter,
				      &rx_ring->fbr[1]->real_physaddr,
				      &rx_ring->fbr[1]->offset, 0x0FFF);

	rx_ring->fbr[1]->ring_virtaddr =
			(void *)((u8 *) rx_ring->fbr[1]->ring_virtaddr +
			rx_ring->fbr[1]->offset);
#endif
	for (i = 0; i < (rx_ring->fbr[0]->num_entries / FBR_CHUNKS); i++) {
		u64 fbr1_tmp_physaddr;
		u64 fbr1_offset;
		u32 fbr1_align;

		/* This code allocates an area of memory big enough for N
		 * free buffers + (buffer_size - 1) so that the buffers can
		 * be aligned on 4k boundaries.  If each buffer were aligned
		 * to a buffer_size boundary, the effect would be to double
		 * the size of FBR0.  By allocating N buffers at once, we
		 * reduce this overhead.
		 */
		if (rx_ring->fbr[0]->buffsize > 4096)
			fbr1_align = 4096;
		else
			fbr1_align = rx_ring->fbr[0]->buffsize;

		fbr_chunksize =
		    (FBR_CHUNKS * rx_ring->fbr[0]->buffsize) + fbr1_align - 1;
		rx_ring->fbr[0]->mem_virtaddrs[i] =
		    dma_alloc_coherent(&adapter->pdev->dev, fbr_chunksize,
				       &rx_ring->fbr[0]->mem_physaddrs[i],
				       GFP_KERNEL);

		if (!rx_ring->fbr[0]->mem_virtaddrs[i]) {
			dev_err(&adapter->pdev->dev,
				"Could not alloc memory\n");
			return -ENOMEM;
		}

		/* See NOTE in "Save Physical Address" comment above */
		fbr1_tmp_physaddr = rx_ring->fbr[0]->mem_physaddrs[i];

		et131x_align_allocated_memory(adapter,
					      &fbr1_tmp_physaddr,
					      &fbr1_offset, (fbr1_align - 1));

		for (j = 0; j < FBR_CHUNKS; j++) {
			u32 index = (i * FBR_CHUNKS) + j;

			/* Save the Virtual address of this index for quick
			 * access later
			 */
			rx_ring->fbr[0]->virt[index] =
			    (u8 *) rx_ring->fbr[0]->mem_virtaddrs[i] +
			    (j * rx_ring->fbr[0]->buffsize) + fbr1_offset;

			/* now store the physical address in the descriptor
			 * so the device can access it
			 */
			rx_ring->fbr[0]->bus_high[index] =
			    (u32) (fbr1_tmp_physaddr >> 32);
			rx_ring->fbr[0]->bus_low[index] =
			    (u32) fbr1_tmp_physaddr;

			fbr1_tmp_physaddr += rx_ring->fbr[0]->buffsize;

			rx_ring->fbr[0]->buffer1[index] =
			    rx_ring->fbr[0]->virt[index];
			rx_ring->fbr[0]->buffer2[index] =
			    rx_ring->fbr[0]->virt[index] - 4;
		}
	}

#ifdef USE_FBR0
	/* Same for FBR0 (if in use) */
	for (i = 0; i < (rx_ring->fbr[1]->num_entries / FBR_CHUNKS); i++) {
		u64 fbr0_tmp_physaddr;
		u64 fbr0_offset;

		fbr_chunksize =
		    ((FBR_CHUNKS + 1) * rx_ring->fbr[1]->buffsize) - 1;
		rx_ring->fbr[1]->mem_virtaddrs[i] =
		    dma_alloc_coherent(&adapter->pdev->dev, fbr_chunksize,
				       &rx_ring->fbr[1]->mem_physaddrs[i],
				       GFP_KERNEL);

		if (!rx_ring->fbr[1]->mem_virtaddrs[i]) {
			dev_err(&adapter->pdev->dev,
				"Could not alloc memory\n");
			return -ENOMEM;
		}

		/* See NOTE in "Save Physical Address" comment above */
		fbr0_tmp_physaddr = rx_ring->fbr[1]->mem_physaddrs[i];

		et131x_align_allocated_memory(adapter,
					      &fbr0_tmp_physaddr,
					      &fbr0_offset,
					      rx_ring->fbr[1]->buffsize - 1);

		for (j = 0; j < FBR_CHUNKS; j++) {
			u32 index = (i * FBR_CHUNKS) + j;

			rx_ring->fbr[1]->virt[index] =
			    (u8 *) rx_ring->fbr[1]->mem_virtaddrs[i] +
			    (j * rx_ring->fbr[1]->buffsize) + fbr0_offset;

			rx_ring->fbr[1]->bus_high[index] =
			    (u32) (fbr0_tmp_physaddr >> 32);
			rx_ring->fbr[1]->bus_low[index] =
			    (u32) fbr0_tmp_physaddr;

			fbr0_tmp_physaddr += rx_ring->fbr[1]->buffsize;

			rx_ring->fbr[1]->buffer1[index] =
			    rx_ring->fbr[1]->virt[index];
			rx_ring->fbr[1]->buffer2[index] =
			    rx_ring->fbr[1]->virt[index] - 4;
		}
	}
#endif

	/* Allocate an area of memory for FIFO of Packet Status ring entries */
	pktstat_ringsize =
	    sizeof(struct pkt_stat_desc) * adapter->rx_ring.psr_num_entries;

	rx_ring->ps_ring_virtaddr = dma_alloc_coherent(&adapter->pdev->dev,
						  pktstat_ringsize,
						  &rx_ring->ps_ring_physaddr,
						  GFP_KERNEL);

	if (!rx_ring->ps_ring_virtaddr) {
		dev_err(&adapter->pdev->dev,
			  "Cannot alloc memory for Packet Status Ring\n");
		return -ENOMEM;
	}
	pr_info("Packet Status Ring %llx\n",
		(unsigned long long) rx_ring->ps_ring_physaddr);

	/*
	 * NOTE : dma_alloc_coherent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here before
	 * storing the adjusted address.
	 */

	/* Allocate an area of memory for writeback of status information */
	rx_ring->rx_status_block = dma_alloc_coherent(&adapter->pdev->dev,
					    sizeof(struct rx_status_block),
					    &rx_ring->rx_status_bus,
					    GFP_KERNEL);
	if (!rx_ring->rx_status_block) {
		dev_err(&adapter->pdev->dev,
			  "Cannot alloc memory for Status Block\n");
		return -ENOMEM;
	}
	rx_ring->num_rfd = NIC_DEFAULT_NUM_RFD;
	pr_info("PRS %llx\n", (unsigned long long)rx_ring->rx_status_bus);

	/* Recv
	 * kmem_cache_create initializes a lookaside list. After successful
	 * creation, nonpaged fixed-size blocks can be allocated from and
	 * freed to the lookaside list.
	 * RFDs will be allocated from this pool.
	 */
	rx_ring->recv_lookaside = kmem_cache_create(adapter->netdev->name,
						   sizeof(struct rfd),
						   0,
						   SLAB_CACHE_DMA |
						   SLAB_HWCACHE_ALIGN,
						   NULL);

	adapter->flags |= fMP_ADAPTER_RECV_LOOKASIDE;

	/* The RFDs are going to be put on lists later on, so initialize the
	 * lists now.
	 */
	INIT_LIST_HEAD(&rx_ring->recv_list);
	return 0;
}

/**
 * et131x_rx_dma_memory_free - Free all memory allocated within this module.
 * @adapter: pointer to our private adapter structure
 */
static void et131x_rx_dma_memory_free(struct et131x_adapter *adapter)
{
	u32 index;
	u32 bufsize;
	u32 pktstat_ringsize;
	struct rfd *rfd;
	struct rx_ring *rx_ring;

	/* Setup some convenience pointers */
	rx_ring = &adapter->rx_ring;

	/* Free RFDs and associated packet descriptors */
	WARN_ON(rx_ring->num_ready_recv != rx_ring->num_rfd);

	while (!list_empty(&rx_ring->recv_list)) {
		rfd = (struct rfd *) list_entry(rx_ring->recv_list.next,
				struct rfd, list_node);

		list_del(&rfd->list_node);
		rfd->skb = NULL;
		kmem_cache_free(adapter->rx_ring.recv_lookaside, rfd);
	}

	/* Free Free Buffer Ring 1 */
	if (rx_ring->fbr[0]->ring_virtaddr) {
		/* First the packet memory */
		for (index = 0; index <
		     (rx_ring->fbr[0]->num_entries / FBR_CHUNKS); index++) {
			if (rx_ring->fbr[0]->mem_virtaddrs[index]) {
				u32 fbr1_align;

				if (rx_ring->fbr[0]->buffsize > 4096)
					fbr1_align = 4096;
				else
					fbr1_align = rx_ring->fbr[0]->buffsize;

				bufsize =
				    (rx_ring->fbr[0]->buffsize * FBR_CHUNKS) +
				    fbr1_align - 1;

				dma_free_coherent(&adapter->pdev->dev,
					bufsize,
					rx_ring->fbr[0]->mem_virtaddrs[index],
					rx_ring->fbr[0]->mem_physaddrs[index]);

				rx_ring->fbr[0]->mem_virtaddrs[index] = NULL;
			}
		}

		/* Now the FIFO itself */
		rx_ring->fbr[0]->ring_virtaddr = (void *)((u8 *)
		    rx_ring->fbr[0]->ring_virtaddr - rx_ring->fbr[0]->offset);

		bufsize =
		    (sizeof(struct fbr_desc) * rx_ring->fbr[0]->num_entries) +
									0xfff;

		dma_free_coherent(&adapter->pdev->dev, bufsize,
				    rx_ring->fbr[0]->ring_virtaddr,
				    rx_ring->fbr[0]->ring_physaddr);

		rx_ring->fbr[0]->ring_virtaddr = NULL;
	}

#ifdef USE_FBR0
	/* Now the same for Free Buffer Ring 0 */
	if (rx_ring->fbr[1]->ring_virtaddr) {
		/* First the packet memory */
		for (index = 0; index <
		     (rx_ring->fbr[1]->num_entries / FBR_CHUNKS); index++) {
			if (rx_ring->fbr[1]->mem_virtaddrs[index]) {
				bufsize =
				    (rx_ring->fbr[1]->buffsize *
				     (FBR_CHUNKS + 1)) - 1;

				dma_free_coherent(&adapter->pdev->dev,
					bufsize,
					rx_ring->fbr[1]->mem_virtaddrs[index],
					rx_ring->fbr[1]->mem_physaddrs[index]);

				rx_ring->fbr[1]->mem_virtaddrs[index] = NULL;
			}
		}

		/* Now the FIFO itself */
		rx_ring->fbr[1]->ring_virtaddr = (void *)((u8 *)
		    rx_ring->fbr[1]->ring_virtaddr - rx_ring->fbr[1]->offset);

		bufsize =
		    (sizeof(struct fbr_desc) * rx_ring->fbr[1]->num_entries) +
									0xfff;

		dma_free_coherent(&adapter->pdev->dev,
				  bufsize,
				  rx_ring->fbr[1]->ring_virtaddr,
				  rx_ring->fbr[1]->ring_physaddr);

		rx_ring->fbr[1]->ring_virtaddr = NULL;
	}
#endif

	/* Free Packet Status Ring */
	if (rx_ring->ps_ring_virtaddr) {
		pktstat_ringsize =
		    sizeof(struct pkt_stat_desc) *
		    adapter->rx_ring.psr_num_entries;

		dma_free_coherent(&adapter->pdev->dev, pktstat_ringsize,
				    rx_ring->ps_ring_virtaddr,
				    rx_ring->ps_ring_physaddr);

		rx_ring->ps_ring_virtaddr = NULL;
	}

	/* Free area of memory for the writeback of status information */
	if (rx_ring->rx_status_block) {
		dma_free_coherent(&adapter->pdev->dev,
			sizeof(struct rx_status_block),
			rx_ring->rx_status_block, rx_ring->rx_status_bus);
		rx_ring->rx_status_block = NULL;
	}

	/* Destroy the lookaside (RFD) pool */
	if (adapter->flags & fMP_ADAPTER_RECV_LOOKASIDE) {
		kmem_cache_destroy(rx_ring->recv_lookaside);
		adapter->flags &= ~fMP_ADAPTER_RECV_LOOKASIDE;
	}

	/* Free the FBR Lookup Table */
#ifdef USE_FBR0
	kfree(rx_ring->fbr[1]);
#endif

	kfree(rx_ring->fbr[0]);

	/* Reset Counters */
	rx_ring->num_ready_recv = 0;
}

/**
 * et131x_init_recv - Initialize receive data structures.
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success and errno on failure (as defined in errno.h)
 */
static int et131x_init_recv(struct et131x_adapter *adapter)
{
	int status = -ENOMEM;
	struct rfd *rfd = NULL;
	u32 rfdct;
	u32 numrfd = 0;
	struct rx_ring *rx_ring;

	/* Setup some convenience pointers */
	rx_ring = &adapter->rx_ring;

	/* Setup each RFD */
	for (rfdct = 0; rfdct < rx_ring->num_rfd; rfdct++) {
		rfd = kmem_cache_alloc(rx_ring->recv_lookaside,
						     GFP_ATOMIC | GFP_DMA);

		if (!rfd) {
			dev_err(&adapter->pdev->dev,
				  "Couldn't alloc RFD out of kmem_cache\n");
			status = -ENOMEM;
			continue;
		}

		rfd->skb = NULL;

		/* Add this RFD to the recv_list */
		list_add_tail(&rfd->list_node, &rx_ring->recv_list);

		/* Increment both the available RFD's, and the total RFD's. */
		rx_ring->num_ready_recv++;
		numrfd++;
	}

	if (numrfd > NIC_MIN_NUM_RFD)
		status = 0;

	rx_ring->num_rfd = numrfd;

	if (status != 0) {
		kmem_cache_free(rx_ring->recv_lookaside, rfd);
		dev_err(&adapter->pdev->dev,
			  "Allocation problems in et131x_init_recv\n");
	}
	return status;
}

/**
 * et131x_set_rx_dma_timer - Set the heartbeat timer according to line rate.
 * @adapter: pointer to our adapter structure
 */
static void et131x_set_rx_dma_timer(struct et131x_adapter *adapter)
{
	struct phy_device *phydev = adapter->phydev;

	if (!phydev)
		return;

	/* For version B silicon, we do not use the RxDMA timer for 10 and 100
	 * Mbits/s line rates. We do not enable and RxDMA interrupt coalescing.
	 */
	if ((phydev->speed == SPEED_100) || (phydev->speed == SPEED_10)) {
		writel(0, &adapter->regs->rxdma.max_pkt_time);
		writel(1, &adapter->regs->rxdma.num_pkt_done);
	}
}

/**
 * NICReturnRFD - Recycle a RFD and put it back onto the receive list
 * @adapter: pointer to our adapter
 * @rfd: pointer to the RFD
 */
static void nic_return_rfd(struct et131x_adapter *adapter, struct rfd *rfd)
{
	struct rx_ring *rx_local = &adapter->rx_ring;
	struct rxdma_regs __iomem *rx_dma = &adapter->regs->rxdma;
	u16 buff_index = rfd->bufferindex;
	u8 ring_index = rfd->ringindex;
	unsigned long flags;

	/* We don't use any of the OOB data besides status. Otherwise, we
	 * need to clean up OOB data
	 */
	if (
#ifdef USE_FBR0
	    (ring_index == 0 && buff_index < rx_local->fbr[1]->num_entries) ||
#endif
	    (ring_index == 1 && buff_index < rx_local->fbr[0]->num_entries)) {
		spin_lock_irqsave(&adapter->fbr_lock, flags);

		if (ring_index == 1) {
			struct fbr_desc *next = (struct fbr_desc *)
					(rx_local->fbr[0]->ring_virtaddr) +
					INDEX10(rx_local->fbr[0]->local_full);

			/* Handle the Free Buffer Ring advancement here. Write
			 * the PA / Buffer Index for the returned buffer into
			 * the oldest (next to be freed)FBR entry
			 */
			next->addr_hi = rx_local->fbr[0]->bus_high[buff_index];
			next->addr_lo = rx_local->fbr[0]->bus_low[buff_index];
			next->word2 = buff_index;

			writel(bump_free_buff_ring(
					&rx_local->fbr[0]->local_full,
					rx_local->fbr[0]->num_entries - 1),
					&rx_dma->fbr1_full_offset);
		}
#ifdef USE_FBR0
		else {
			struct fbr_desc *next = (struct fbr_desc *)
				rx_local->fbr[1]->ring_virtaddr +
				    INDEX10(rx_local->fbr[1]->local_full);

			/* Handle the Free Buffer Ring advancement here. Write
			 * the PA / Buffer Index for the returned buffer into
			 * the oldest (next to be freed) FBR entry
			 */
			next->addr_hi = rx_local->fbr[1]->bus_high[buff_index];
			next->addr_lo = rx_local->fbr[1]->bus_low[buff_index];
			next->word2 = buff_index;

			writel(bump_free_buff_ring(
					&rx_local->fbr[1]->local_full,
					rx_local->fbr[1]->num_entries - 1),
			       &rx_dma->fbr0_full_offset);
		}
#endif
		spin_unlock_irqrestore(&adapter->fbr_lock, flags);
	} else {
		dev_err(&adapter->pdev->dev,
			  "%s illegal Buffer Index returned\n", __func__);
	}

	/* The processing on this RFD is done, so put it back on the tail of
	 * our list
	 */
	spin_lock_irqsave(&adapter->rcv_lock, flags);
	list_add_tail(&rfd->list_node, &rx_local->recv_list);
	rx_local->num_ready_recv++;
	spin_unlock_irqrestore(&adapter->rcv_lock, flags);

	WARN_ON(rx_local->num_ready_recv > rx_local->num_rfd);
}

/**
 * nic_rx_pkts - Checks the hardware for available packets
 * @adapter: pointer to our adapter
 *
 * Returns rfd, a pointer to our MPRFD.
 *
 * Checks the hardware for available packets, using completion ring
 * If packets are available, it gets an RFD from the recv_list, attaches
 * the packet to it, puts the RFD in the RecvPendList, and also returns
 * the pointer to the RFD.
 */
static struct rfd *nic_rx_pkts(struct et131x_adapter *adapter)
{
	struct rx_ring *rx_local = &adapter->rx_ring;
	struct rx_status_block *status;
	struct pkt_stat_desc *psr;
	struct rfd *rfd;
	u32 i;
	u8 *buf;
	unsigned long flags;
	struct list_head *element;
	u8 ring_index;
	u16 buff_index;
	u32 len;
	u32 word0;
	u32 word1;

	/* RX Status block is written by the DMA engine prior to every
	 * interrupt. It contains the next to be used entry in the Packet
	 * Status Ring, and also the two Free Buffer rings.
	 */
	status = rx_local->rx_status_block;
	word1 = status->word1 >> 16;	/* Get the useful bits */

	/* Check the PSR and wrap bits do not match */
	if ((word1 & 0x1FFF) == (rx_local->local_psr_full & 0x1FFF))
		/* Looks like this ring is not updated yet */
		return NULL;

	/* The packet status ring indicates that data is available. */
	psr = (struct pkt_stat_desc *) (rx_local->ps_ring_virtaddr) +
			(rx_local->local_psr_full & 0xFFF);

	/* Grab any information that is required once the PSR is
	 * advanced, since we can no longer rely on the memory being
	 * accurate
	 */
	len = psr->word1 & 0xFFFF;
	ring_index = (psr->word1 >> 26) & 0x03;
	buff_index = (psr->word1 >> 16) & 0x3FF;
	word0 = psr->word0;

	/* Indicate that we have used this PSR entry. */
	/* FIXME wrap 12 */
	add_12bit(&rx_local->local_psr_full, 1);
	if (
	  (rx_local->local_psr_full & 0xFFF) > rx_local->psr_num_entries - 1) {
		/* Clear psr full and toggle the wrap bit */
		rx_local->local_psr_full &=  ~0xFFF;
		rx_local->local_psr_full ^= 0x1000;
	}

	writel(rx_local->local_psr_full,
	       &adapter->regs->rxdma.psr_full_offset);

#ifndef USE_FBR0
	if (ring_index != 1)
		return NULL;
#endif

#ifdef USE_FBR0
	if (ring_index > 1 ||
		(ring_index == 0 &&
		buff_index > rx_local->fbr[1]->num_entries - 1) ||
		(ring_index == 1 &&
		buff_index > rx_local->fbr[0]->num_entries - 1)) {
#else
	if (ring_index != 1 || buff_index > rx_local->fbr[0]->num_entries - 1) {
#endif
		/* Illegal buffer or ring index cannot be used by S/W*/
		dev_err(&adapter->pdev->dev,
			  "NICRxPkts PSR Entry %d indicates "
			  "length of %d and/or bad bi(%d)\n",
			  rx_local->local_psr_full & 0xFFF,
			  len, buff_index);
		return NULL;
	}

	/* Get and fill the RFD. */
	spin_lock_irqsave(&adapter->rcv_lock, flags);

	rfd = NULL;
	element = rx_local->recv_list.next;
	rfd = (struct rfd *) list_entry(element, struct rfd, list_node);

	if (rfd == NULL) {
		spin_unlock_irqrestore(&adapter->rcv_lock, flags);
		return NULL;
	}

	list_del(&rfd->list_node);
	rx_local->num_ready_recv--;

	spin_unlock_irqrestore(&adapter->rcv_lock, flags);

	rfd->bufferindex = buff_index;
	rfd->ringindex = ring_index;

	/* In V1 silicon, there is a bug which screws up filtering of
	 * runt packets.  Therefore runt packet filtering is disabled
	 * in the MAC and the packets are dropped here.  They are
	 * also counted here.
	 */
	if (len < (NIC_MIN_PACKET_SIZE + 4)) {
		adapter->stats.rx_other_errs++;
		len = 0;
	}

	if (len) {
		/* Determine if this is a multicast packet coming in */
		if ((word0 & ALCATEL_MULTICAST_PKT) &&
		    !(word0 & ALCATEL_BROADCAST_PKT)) {
			/* Promiscuous mode and Multicast mode are
			 * not mutually exclusive as was first
			 * thought.  I guess Promiscuous is just
			 * considered a super-set of the other
			 * filters. Generally filter is 0x2b when in
			 * promiscuous mode.
			 */
			if ((adapter->packet_filter &
					ET131X_PACKET_TYPE_MULTICAST)
			    && !(adapter->packet_filter &
					ET131X_PACKET_TYPE_PROMISCUOUS)
			    && !(adapter->packet_filter &
					ET131X_PACKET_TYPE_ALL_MULTICAST)) {
				/*
				 * Note - ring_index for fbr[] array is reversed
				 * 1 for FBR0 etc
				 */
				buf = rx_local->fbr[(ring_index == 0 ? 1 : 0)]->
						virt[buff_index];

				/* Loop through our list to see if the
				 * destination address of this packet
				 * matches one in our list.
				 */
				for (i = 0; i < adapter->multicast_addr_count;
				     i++) {
					if (buf[0] ==
						adapter->multicast_list[i][0]
					    && buf[1] ==
						adapter->multicast_list[i][1]
					    && buf[2] ==
						adapter->multicast_list[i][2]
					    && buf[3] ==
						adapter->multicast_list[i][3]
					    && buf[4] ==
						adapter->multicast_list[i][4]
					    && buf[5] ==
						adapter->multicast_list[i][5]) {
						break;
					}
				}

				/* If our index is equal to the number
				 * of Multicast address we have, then
				 * this means we did not find this
				 * packet's matching address in our
				 * list.  Set the len to zero,
				 * so we free our RFD when we return
				 * from this function.
				 */
				if (i == adapter->multicast_addr_count)
					len = 0;
			}

			if (len > 0)
				adapter->stats.multicast_pkts_rcvd++;
		} else if (word0 & ALCATEL_BROADCAST_PKT)
			adapter->stats.broadcast_pkts_rcvd++;
		else
			/* Not sure what this counter measures in
			 * promiscuous mode. Perhaps we should check
			 * the MAC address to see if it is directed
			 * to us in promiscuous mode.
			 */
			adapter->stats.unicast_pkts_rcvd++;
	}

	if (len > 0) {
		struct sk_buff *skb = NULL;

		/*rfd->len = len - 4; */
		rfd->len = len;

		skb = dev_alloc_skb(rfd->len + 2);
		if (!skb) {
			dev_err(&adapter->pdev->dev,
				  "Couldn't alloc an SKB for Rx\n");
			return NULL;
		}

		adapter->net_stats.rx_bytes += rfd->len;

		/*
		 * Note - ring_index for fbr[] array is reversed,
		 * 1 for FBR0 etc
		 */
		memcpy(skb_put(skb, rfd->len),
		    rx_local->fbr[(ring_index == 0 ? 1 : 0)]->virt[buff_index],
		    rfd->len);

		skb->dev = adapter->netdev;
		skb->protocol = eth_type_trans(skb, adapter->netdev);
		skb->ip_summed = CHECKSUM_NONE;

		netif_rx_ni(skb);
	} else {
		rfd->len = 0;
	}

	nic_return_rfd(adapter, rfd);
	return rfd;
}

/**
 * et131x_handle_recv_interrupt - Interrupt handler for receive processing
 * @adapter: pointer to our adapter
 *
 * Assumption, Rcv spinlock has been acquired.
 */
static void et131x_handle_recv_interrupt(struct et131x_adapter *adapter)
{
	struct rfd *rfd = NULL;
	u32 count = 0;
	bool done = true;

	/* Process up to available RFD's */
	while (count < NUM_PACKETS_HANDLED) {
		if (list_empty(&adapter->rx_ring.recv_list)) {
			WARN_ON(adapter->rx_ring.num_ready_recv != 0);
			done = false;
			break;
		}

		rfd = nic_rx_pkts(adapter);

		if (rfd == NULL)
			break;

		/* Do not receive any packets until a filter has been set.
		 * Do not receive any packets until we have link.
		 * If length is zero, return the RFD in order to advance the
		 * Free buffer ring.
		 */
		if (!adapter->packet_filter ||
		    !netif_carrier_ok(adapter->netdev) ||
		    rfd->len == 0)
			continue;

		/* Increment the number of packets we received */
		adapter->net_stats.rx_packets++;

		/* Set the status on the packet, either resources or success */
		if (adapter->rx_ring.num_ready_recv < RFD_LOW_WATER_MARK) {
			dev_warn(&adapter->pdev->dev,
				    "RFD's are running out\n");
		}
		count++;
	}

	if (count == NUM_PACKETS_HANDLED || !done) {
		adapter->rx_ring.unfinished_receives = true;
		writel(PARM_TX_TIME_INT_DEF * NANO_IN_A_MICRO,
		       &adapter->regs->global.watchdog_timer);
	} else
		/* Watchdog timer will disable itself if appropriate. */
		adapter->rx_ring.unfinished_receives = false;
}

/**
 * et131x_tx_dma_memory_alloc
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success and errno on failure (as defined in errno.h).
 *
 * Allocates memory that will be visible both to the device and to the CPU.
 * The OS will pass us packets, pointers to which we will insert in the Tx
 * Descriptor queue. The device will read this queue to find the packets in
 * memory. The device will update the "status" in memory each time it xmits a
 * packet.
 */
static int et131x_tx_dma_memory_alloc(struct et131x_adapter *adapter)
{
	int desc_size = 0;
	struct tx_ring *tx_ring = &adapter->tx_ring;

	/* Allocate memory for the TCB's (Transmit Control Block) */
	adapter->tx_ring.tcb_ring =
		kcalloc(NUM_TCB, sizeof(struct tcb), GFP_ATOMIC | GFP_DMA);
	if (!adapter->tx_ring.tcb_ring) {
		dev_err(&adapter->pdev->dev, "Cannot alloc memory for TCBs\n");
		return -ENOMEM;
	}

	/* Allocate enough memory for the Tx descriptor ring, and allocate
	 * some extra so that the ring can be aligned on a 4k boundary.
	 */
	desc_size = (sizeof(struct tx_desc) * NUM_DESC_PER_RING_TX) + 4096 - 1;
	tx_ring->tx_desc_ring =
	    (struct tx_desc *) dma_alloc_coherent(&adapter->pdev->dev,
						  desc_size,
						  &tx_ring->tx_desc_ring_pa,
						  GFP_KERNEL);
	if (!adapter->tx_ring.tx_desc_ring) {
		dev_err(&adapter->pdev->dev,
			"Cannot alloc memory for Tx Ring\n");
		return -ENOMEM;
	}

	/* Save physical address
	 *
	 * NOTE: dma_alloc_coherent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here before
	 * storing the adjusted address.
	 */
	/* Allocate memory for the Tx status block */
	tx_ring->tx_status = dma_alloc_coherent(&adapter->pdev->dev,
						    sizeof(u32),
						    &tx_ring->tx_status_pa,
						    GFP_KERNEL);
	if (!adapter->tx_ring.tx_status_pa) {
		dev_err(&adapter->pdev->dev,
				  "Cannot alloc memory for Tx status block\n");
		return -ENOMEM;
	}
	return 0;
}

/**
 * et131x_tx_dma_memory_free - Free all memory allocated within this module
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success and errno on failure (as defined in errno.h).
 */
static void et131x_tx_dma_memory_free(struct et131x_adapter *adapter)
{
	int desc_size = 0;

	if (adapter->tx_ring.tx_desc_ring) {
		/* Free memory relating to Tx rings here */
		desc_size = (sizeof(struct tx_desc) * NUM_DESC_PER_RING_TX)
								+ 4096 - 1;
		dma_free_coherent(&adapter->pdev->dev,
				    desc_size,
				    adapter->tx_ring.tx_desc_ring,
				    adapter->tx_ring.tx_desc_ring_pa);
		adapter->tx_ring.tx_desc_ring = NULL;
	}

	/* Free memory for the Tx status block */
	if (adapter->tx_ring.tx_status) {
		dma_free_coherent(&adapter->pdev->dev,
				    sizeof(u32),
				    adapter->tx_ring.tx_status,
				    adapter->tx_ring.tx_status_pa);

		adapter->tx_ring.tx_status = NULL;
	}
	/* Free the memory for the tcb structures */
	kfree(adapter->tx_ring.tcb_ring);
}

/**
 * nic_send_packet - NIC specific send handler for version B silicon.
 * @adapter: pointer to our adapter
 * @tcb: pointer to struct tcb
 *
 * Returns 0 or errno.
 */
static int nic_send_packet(struct et131x_adapter *adapter, struct tcb *tcb)
{
	u32 i;
	struct tx_desc desc[24];	/* 24 x 16 byte */
	u32 frag = 0;
	u32 thiscopy, remainder;
	struct sk_buff *skb = tcb->skb;
	u32 nr_frags = skb_shinfo(skb)->nr_frags + 1;
	struct skb_frag_struct *frags = &skb_shinfo(skb)->frags[0];
	unsigned long flags;
	struct phy_device *phydev = adapter->phydev;

	/* Part of the optimizations of this send routine restrict us to
	 * sending 24 fragments at a pass.  In practice we should never see
	 * more than 5 fragments.
	 *
	 * NOTE: The older version of this function (below) can handle any
	 * number of fragments. If needed, we can call this function,
	 * although it is less efficient.
	 */
	if (nr_frags > 23)
		return -EIO;

	memset(desc, 0, sizeof(struct tx_desc) * (nr_frags + 1));

	for (i = 0; i < nr_frags; i++) {
		/* If there is something in this element, lets get a
		 * descriptor from the ring and get the necessary data
		 */
		if (i == 0) {
			/* If the fragments are smaller than a standard MTU,
			 * then map them to a single descriptor in the Tx
			 * Desc ring. However, if they're larger, as is
			 * possible with support for jumbo packets, then
			 * split them each across 2 descriptors.
			 *
			 * This will work until we determine why the hardware
			 * doesn't seem to like large fragments.
			 */
			if ((skb->len - skb->data_len) <= 1514) {
				desc[frag].addr_hi = 0;
				/* Low 16bits are length, high is vlan and
				   unused currently so zero */
				desc[frag].len_vlan =
					skb->len - skb->data_len;

				/* NOTE: Here, the dma_addr_t returned from
				 * dma_map_single() is implicitly cast as a
				 * u32. Although dma_addr_t can be
				 * 64-bit, the address returned by
				 * dma_map_single() is always 32-bit
				 * addressable (as defined by the pci/dma
				 * subsystem)
				 */
				desc[frag++].addr_lo =
				    dma_map_single(&adapter->pdev->dev,
						   skb->data,
						   skb->len -
						   skb->data_len,
						   DMA_TO_DEVICE);
			} else {
				desc[frag].addr_hi = 0;
				desc[frag].len_vlan =
				    (skb->len - skb->data_len) / 2;

				/* NOTE: Here, the dma_addr_t returned from
				 * dma_map_single() is implicitly cast as a
				 * u32. Although dma_addr_t can be
				 * 64-bit, the address returned by
				 * dma_map_single() is always 32-bit
				 * addressable (as defined by the pci/dma
				 * subsystem)
				 */
				desc[frag++].addr_lo =
				    dma_map_single(&adapter->pdev->dev,
						   skb->data,
						   ((skb->len -
						     skb->data_len) / 2),
						   DMA_TO_DEVICE);
				desc[frag].addr_hi = 0;

				desc[frag].len_vlan =
				    (skb->len - skb->data_len) / 2;

				/* NOTE: Here, the dma_addr_t returned from
				 * dma_map_single() is implicitly cast as a
				 * u32. Although dma_addr_t can be
				 * 64-bit, the address returned by
				 * dma_map_single() is always 32-bit
				 * addressable (as defined by the pci/dma
				 * subsystem)
				 */
				desc[frag++].addr_lo =
				    dma_map_single(&adapter->pdev->dev,
						   skb->data +
						   ((skb->len -
						     skb->data_len) / 2),
						   ((skb->len -
						     skb->data_len) / 2),
						   DMA_TO_DEVICE);
			}
		} else {
			desc[frag].addr_hi = 0;
			desc[frag].len_vlan =
					frags[i - 1].size;

			/* NOTE: Here, the dma_addr_t returned from
			 * dma_map_page() is implicitly cast as a u32.
			 * Although dma_addr_t can be 64-bit, the address
			 * returned by dma_map_page() is always 32-bit
			 * addressable (as defined by the pci/dma subsystem)
			 */
			desc[frag++].addr_lo = skb_frag_dma_map(
							&adapter->pdev->dev,
							&frags[i - 1],
							0,
							frags[i - 1].size,
							DMA_TO_DEVICE);
		}
	}

	if (phydev && phydev->speed == SPEED_1000) {
		if (++adapter->tx_ring.since_irq == PARM_TX_NUM_BUFS_DEF) {
			/* Last element & Interrupt flag */
			desc[frag - 1].flags = 0x5;
			adapter->tx_ring.since_irq = 0;
		} else { /* Last element */
			desc[frag - 1].flags = 0x1;
		}
	} else
		desc[frag - 1].flags = 0x5;

	desc[0].flags |= 2;	/* First element flag */

	tcb->index_start = adapter->tx_ring.send_idx;
	tcb->stale = 0;

	spin_lock_irqsave(&adapter->send_hw_lock, flags);

	thiscopy = NUM_DESC_PER_RING_TX -
				INDEX10(adapter->tx_ring.send_idx);

	if (thiscopy >= frag) {
		remainder = 0;
		thiscopy = frag;
	} else {
		remainder = frag - thiscopy;
	}

	memcpy(adapter->tx_ring.tx_desc_ring +
	       INDEX10(adapter->tx_ring.send_idx), desc,
	       sizeof(struct tx_desc) * thiscopy);

	add_10bit(&adapter->tx_ring.send_idx, thiscopy);

	if (INDEX10(adapter->tx_ring.send_idx) == 0 ||
		  INDEX10(adapter->tx_ring.send_idx) == NUM_DESC_PER_RING_TX) {
		adapter->tx_ring.send_idx &= ~ET_DMA10_MASK;
		adapter->tx_ring.send_idx ^= ET_DMA10_WRAP;
	}

	if (remainder) {
		memcpy(adapter->tx_ring.tx_desc_ring,
		       desc + thiscopy,
		       sizeof(struct tx_desc) * remainder);

		add_10bit(&adapter->tx_ring.send_idx, remainder);
	}

	if (INDEX10(adapter->tx_ring.send_idx) == 0) {
		if (adapter->tx_ring.send_idx)
			tcb->index = NUM_DESC_PER_RING_TX - 1;
		else
			tcb->index = ET_DMA10_WRAP|(NUM_DESC_PER_RING_TX - 1);
	} else
		tcb->index = adapter->tx_ring.send_idx - 1;

	spin_lock(&adapter->tcb_send_qlock);

	if (adapter->tx_ring.send_tail)
		adapter->tx_ring.send_tail->next = tcb;
	else
		adapter->tx_ring.send_head = tcb;

	adapter->tx_ring.send_tail = tcb;

	WARN_ON(tcb->next != NULL);

	adapter->tx_ring.used++;

	spin_unlock(&adapter->tcb_send_qlock);

	/* Write the new write pointer back to the device. */
	writel(adapter->tx_ring.send_idx,
	       &adapter->regs->txdma.service_request);

	/* For Gig only, we use Tx Interrupt coalescing.  Enable the software
	 * timer to wake us up if this packet isn't followed by N more.
	 */
	if (phydev && phydev->speed == SPEED_1000) {
		writel(PARM_TX_TIME_INT_DEF * NANO_IN_A_MICRO,
		       &adapter->regs->global.watchdog_timer);
	}
	spin_unlock_irqrestore(&adapter->send_hw_lock, flags);

	return 0;
}

/**
 * send_packet - Do the work to send a packet
 * @skb: the packet(s) to send
 * @adapter: a pointer to the device's private adapter structure
 *
 * Return 0 in almost all cases; non-zero value in extreme hard failure only.
 *
 * Assumption: Send spinlock has been acquired
 */
static int send_packet(struct sk_buff *skb, struct et131x_adapter *adapter)
{
	int status;
	struct tcb *tcb = NULL;
	u16 *shbufva;
	unsigned long flags;

	/* All packets must have at least a MAC address and a protocol type */
	if (skb->len < ETH_HLEN)
		return -EIO;

	/* Get a TCB for this packet */
	spin_lock_irqsave(&adapter->tcb_ready_qlock, flags);

	tcb = adapter->tx_ring.tcb_qhead;

	if (tcb == NULL) {
		spin_unlock_irqrestore(&adapter->tcb_ready_qlock, flags);
		return -ENOMEM;
	}

	adapter->tx_ring.tcb_qhead = tcb->next;

	if (adapter->tx_ring.tcb_qhead == NULL)
		adapter->tx_ring.tcb_qtail = NULL;

	spin_unlock_irqrestore(&adapter->tcb_ready_qlock, flags);

	tcb->skb = skb;

	if (skb->data != NULL && skb->len - skb->data_len >= 6) {
		shbufva = (u16 *) skb->data;

		if ((shbufva[0] == 0xffff) &&
		    (shbufva[1] == 0xffff) && (shbufva[2] == 0xffff)) {
			tcb->flags |= fMP_DEST_BROAD;
		} else if ((shbufva[0] & 0x3) == 0x0001) {
			tcb->flags |=  fMP_DEST_MULTI;
		}
	}

	tcb->next = NULL;

	/* Call the NIC specific send handler. */
	status = nic_send_packet(adapter, tcb);

	if (status != 0) {
		spin_lock_irqsave(&adapter->tcb_ready_qlock, flags);

		if (adapter->tx_ring.tcb_qtail)
			adapter->tx_ring.tcb_qtail->next = tcb;
		else
			/* Apparently ready Q is empty. */
			adapter->tx_ring.tcb_qhead = tcb;

		adapter->tx_ring.tcb_qtail = tcb;
		spin_unlock_irqrestore(&adapter->tcb_ready_qlock, flags);
		return status;
	}
	WARN_ON(adapter->tx_ring.used > NUM_TCB);
	return 0;
}

/**
 * et131x_send_packets - This function is called by the OS to send packets
 * @skb: the packet(s) to send
 * @netdev:device on which to TX the above packet(s)
 *
 * Return 0 in almost all cases; non-zero value in extreme hard failure only
 */
static int et131x_send_packets(struct sk_buff *skb, struct net_device *netdev)
{
	int status = 0;
	struct et131x_adapter *adapter = netdev_priv(netdev);

	/* Send these packets
	 *
	 * NOTE: The Linux Tx entry point is only given one packet at a time
	 * to Tx, so the PacketCount and it's array used makes no sense here
	 */

	/* TCB is not available */
	if (adapter->tx_ring.used >= NUM_TCB) {
		/* NOTE: If there's an error on send, no need to queue the
		 * packet under Linux; if we just send an error up to the
		 * netif layer, it will resend the skb to us.
		 */
		status = -ENOMEM;
	} else {
		/* We need to see if the link is up; if it's not, make the
		 * netif layer think we're good and drop the packet
		 */
		if ((adapter->flags & fMP_ADAPTER_FAIL_SEND_MASK) ||
					!netif_carrier_ok(netdev)) {
			dev_kfree_skb_any(skb);
			skb = NULL;

			adapter->net_stats.tx_dropped++;
		} else {
			status = send_packet(skb, adapter);
			if (status != 0 && status != -ENOMEM) {
				/* On any other error, make netif think we're
				 * OK and drop the packet
				 */
				dev_kfree_skb_any(skb);
				skb = NULL;
				adapter->net_stats.tx_dropped++;
			}
		}
	}
	return status;
}

/**
 * free_send_packet - Recycle a struct tcb
 * @adapter: pointer to our adapter
 * @tcb: pointer to struct tcb
 *
 * Complete the packet if necessary
 * Assumption - Send spinlock has been acquired
 */
static inline void free_send_packet(struct et131x_adapter *adapter,
						struct tcb *tcb)
{
	unsigned long flags;
	struct tx_desc *desc = NULL;
	struct net_device_stats *stats = &adapter->net_stats;

	if (tcb->flags & fMP_DEST_BROAD)
		atomic_inc(&adapter->stats.broadcast_pkts_xmtd);
	else if (tcb->flags & fMP_DEST_MULTI)
		atomic_inc(&adapter->stats.multicast_pkts_xmtd);
	else
		atomic_inc(&adapter->stats.unicast_pkts_xmtd);

	if (tcb->skb) {
		stats->tx_bytes += tcb->skb->len;

		/* Iterate through the TX descriptors on the ring
		 * corresponding to this packet and umap the fragments
		 * they point to
		 */
		do {
			desc = (struct tx_desc *)
				    (adapter->tx_ring.tx_desc_ring +
						INDEX10(tcb->index_start));

			dma_unmap_single(&adapter->pdev->dev,
					 desc->addr_lo,
					 desc->len_vlan, DMA_TO_DEVICE);

			add_10bit(&tcb->index_start, 1);
			if (INDEX10(tcb->index_start) >=
							NUM_DESC_PER_RING_TX) {
				tcb->index_start &= ~ET_DMA10_MASK;
				tcb->index_start ^= ET_DMA10_WRAP;
			}
		} while (desc != (adapter->tx_ring.tx_desc_ring +
				INDEX10(tcb->index)));

		dev_kfree_skb_any(tcb->skb);
	}

	memset(tcb, 0, sizeof(struct tcb));

	/* Add the TCB to the Ready Q */
	spin_lock_irqsave(&adapter->tcb_ready_qlock, flags);

	adapter->net_stats.tx_packets++;

	if (adapter->tx_ring.tcb_qtail)
		adapter->tx_ring.tcb_qtail->next = tcb;
	else
		/* Apparently ready Q is empty. */
		adapter->tx_ring.tcb_qhead = tcb;

	adapter->tx_ring.tcb_qtail = tcb;

	spin_unlock_irqrestore(&adapter->tcb_ready_qlock, flags);
	WARN_ON(adapter->tx_ring.used < 0);
}

/**
 * et131x_free_busy_send_packets - Free and complete the stopped active sends
 * @adapter: pointer to our adapter
 *
 * Assumption - Send spinlock has been acquired
 */
static void et131x_free_busy_send_packets(struct et131x_adapter *adapter)
{
	struct tcb *tcb;
	unsigned long flags;
	u32 freed = 0;

	/* Any packets being sent? Check the first TCB on the send list */
	spin_lock_irqsave(&adapter->tcb_send_qlock, flags);

	tcb = adapter->tx_ring.send_head;

	while (tcb != NULL && freed < NUM_TCB) {
		struct tcb *next = tcb->next;

		adapter->tx_ring.send_head = next;

		if (next == NULL)
			adapter->tx_ring.send_tail = NULL;

		adapter->tx_ring.used--;

		spin_unlock_irqrestore(&adapter->tcb_send_qlock, flags);

		freed++;
		free_send_packet(adapter, tcb);

		spin_lock_irqsave(&adapter->tcb_send_qlock, flags);

		tcb = adapter->tx_ring.send_head;
	}

	WARN_ON(freed == NUM_TCB);

	spin_unlock_irqrestore(&adapter->tcb_send_qlock, flags);

	adapter->tx_ring.used = 0;
}

/**
 * et131x_handle_send_interrupt - Interrupt handler for sending processing
 * @adapter: pointer to our adapter
 *
 * Re-claim the send resources, complete sends and get more to send from
 * the send wait queue.
 *
 * Assumption - Send spinlock has been acquired
 */
static void et131x_handle_send_interrupt(struct et131x_adapter *adapter)
{
	unsigned long flags;
	u32 serviced;
	struct tcb *tcb;
	u32 index;

	serviced = readl(&adapter->regs->txdma.new_service_complete);
	index = INDEX10(serviced);

	/* Has the ring wrapped?  Process any descriptors that do not have
	 * the same "wrap" indicator as the current completion indicator
	 */
	spin_lock_irqsave(&adapter->tcb_send_qlock, flags);

	tcb = adapter->tx_ring.send_head;

	while (tcb &&
	       ((serviced ^ tcb->index) & ET_DMA10_WRAP) &&
	       index < INDEX10(tcb->index)) {
		adapter->tx_ring.used--;
		adapter->tx_ring.send_head = tcb->next;
		if (tcb->next == NULL)
			adapter->tx_ring.send_tail = NULL;

		spin_unlock_irqrestore(&adapter->tcb_send_qlock, flags);
		free_send_packet(adapter, tcb);
		spin_lock_irqsave(&adapter->tcb_send_qlock, flags);

		/* Goto the next packet */
		tcb = adapter->tx_ring.send_head;
	}
	while (tcb &&
	       !((serviced ^ tcb->index) & ET_DMA10_WRAP)
	       && index > (tcb->index & ET_DMA10_MASK)) {
		adapter->tx_ring.used--;
		adapter->tx_ring.send_head = tcb->next;
		if (tcb->next == NULL)
			adapter->tx_ring.send_tail = NULL;

		spin_unlock_irqrestore(&adapter->tcb_send_qlock, flags);
		free_send_packet(adapter, tcb);
		spin_lock_irqsave(&adapter->tcb_send_qlock, flags);

		/* Goto the next packet */
		tcb = adapter->tx_ring.send_head;
	}

	/* Wake up the queue when we hit a low-water mark */
	if (adapter->tx_ring.used <= NUM_TCB / 3)
		netif_wake_queue(adapter->netdev);

	spin_unlock_irqrestore(&adapter->tcb_send_qlock, flags);
}

static int et131x_get_settings(struct net_device *netdev,
			       struct ethtool_cmd *cmd)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	return phy_ethtool_gset(adapter->phydev, cmd);
}

static int et131x_set_settings(struct net_device *netdev,
			       struct ethtool_cmd *cmd)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	return phy_ethtool_sset(adapter->phydev, cmd);
}

static int et131x_get_regs_len(struct net_device *netdev)
{
#define ET131X_REGS_LEN 256
	return ET131X_REGS_LEN * sizeof(u32);
}

static void et131x_get_regs(struct net_device *netdev,
			    struct ethtool_regs *regs, void *regs_data)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);
	struct address_map __iomem *aregs = adapter->regs;
	u32 *regs_buff = regs_data;
	u32 num = 0;

	memset(regs_data, 0, et131x_get_regs_len(netdev));

	regs->version = (1 << 24) | (adapter->pdev->revision << 16) |
			adapter->pdev->device;

	/* PHY regs */
	et131x_mii_read(adapter, MII_BMCR, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_BMSR, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_PHYSID1, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_PHYSID2, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_ADVERTISE, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_LPA, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_EXPANSION, (u16 *)&regs_buff[num++]);
	/* Autoneg next page transmit reg */
	et131x_mii_read(adapter, 0x07, (u16 *)&regs_buff[num++]);
	/* Link partner next page reg */
	et131x_mii_read(adapter, 0x08, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_CTRL1000, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_STAT1000, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_ESTATUS, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_INDEX_REG, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_DATA_REG, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_MPHY_CONTROL_REG,
			(u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_LOOPBACK_CONTROL,
			(u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_LOOPBACK_CONTROL+1,
			(u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_REGISTER_MGMT_CONTROL,
			(u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_CONFIG, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_PHY_CONTROL, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_INTERRUPT_MASK, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_INTERRUPT_STATUS,
			(u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_PHY_STATUS, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_LED_1, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_LED_2, (u16 *)&regs_buff[num++]);

	/* Global regs */
	regs_buff[num++] = readl(&aregs->global.txq_start_addr);
	regs_buff[num++] = readl(&aregs->global.txq_end_addr);
	regs_buff[num++] = readl(&aregs->global.rxq_start_addr);
	regs_buff[num++] = readl(&aregs->global.rxq_end_addr);
	regs_buff[num++] = readl(&aregs->global.pm_csr);
	regs_buff[num++] = adapter->stats.interrupt_status;
	regs_buff[num++] = readl(&aregs->global.int_mask);
	regs_buff[num++] = readl(&aregs->global.int_alias_clr_en);
	regs_buff[num++] = readl(&aregs->global.int_status_alias);
	regs_buff[num++] = readl(&aregs->global.sw_reset);
	regs_buff[num++] = readl(&aregs->global.slv_timer);
	regs_buff[num++] = readl(&aregs->global.msi_config);
	regs_buff[num++] = readl(&aregs->global.loopback);
	regs_buff[num++] = readl(&aregs->global.watchdog_timer);

	/* TXDMA regs */
	regs_buff[num++] = readl(&aregs->txdma.csr);
	regs_buff[num++] = readl(&aregs->txdma.pr_base_hi);
	regs_buff[num++] = readl(&aregs->txdma.pr_base_lo);
	regs_buff[num++] = readl(&aregs->txdma.pr_num_des);
	regs_buff[num++] = readl(&aregs->txdma.txq_wr_addr);
	regs_buff[num++] = readl(&aregs->txdma.txq_wr_addr_ext);
	regs_buff[num++] = readl(&aregs->txdma.txq_rd_addr);
	regs_buff[num++] = readl(&aregs->txdma.dma_wb_base_hi);
	regs_buff[num++] = readl(&aregs->txdma.dma_wb_base_lo);
	regs_buff[num++] = readl(&aregs->txdma.service_request);
	regs_buff[num++] = readl(&aregs->txdma.service_complete);
	regs_buff[num++] = readl(&aregs->txdma.cache_rd_index);
	regs_buff[num++] = readl(&aregs->txdma.cache_wr_index);
	regs_buff[num++] = readl(&aregs->txdma.tx_dma_error);
	regs_buff[num++] = readl(&aregs->txdma.desc_abort_cnt);
	regs_buff[num++] = readl(&aregs->txdma.payload_abort_cnt);
	regs_buff[num++] = readl(&aregs->txdma.writeback_abort_cnt);
	regs_buff[num++] = readl(&aregs->txdma.desc_timeout_cnt);
	regs_buff[num++] = readl(&aregs->txdma.payload_timeout_cnt);
	regs_buff[num++] = readl(&aregs->txdma.writeback_timeout_cnt);
	regs_buff[num++] = readl(&aregs->txdma.desc_error_cnt);
	regs_buff[num++] = readl(&aregs->txdma.payload_error_cnt);
	regs_buff[num++] = readl(&aregs->txdma.writeback_error_cnt);
	regs_buff[num++] = readl(&aregs->txdma.dropped_tlp_cnt);
	regs_buff[num++] = readl(&aregs->txdma.new_service_complete);
	regs_buff[num++] = readl(&aregs->txdma.ethernet_packet_cnt);

	/* RXDMA regs */
	regs_buff[num++] = readl(&aregs->rxdma.csr);
	regs_buff[num++] = readl(&aregs->rxdma.dma_wb_base_hi);
	regs_buff[num++] = readl(&aregs->rxdma.dma_wb_base_lo);
	regs_buff[num++] = readl(&aregs->rxdma.num_pkt_done);
	regs_buff[num++] = readl(&aregs->rxdma.max_pkt_time);
	regs_buff[num++] = readl(&aregs->rxdma.rxq_rd_addr);
	regs_buff[num++] = readl(&aregs->rxdma.rxq_rd_addr_ext);
	regs_buff[num++] = readl(&aregs->rxdma.rxq_wr_addr);
	regs_buff[num++] = readl(&aregs->rxdma.psr_base_hi);
	regs_buff[num++] = readl(&aregs->rxdma.psr_base_lo);
	regs_buff[num++] = readl(&aregs->rxdma.psr_num_des);
	regs_buff[num++] = readl(&aregs->rxdma.psr_avail_offset);
	regs_buff[num++] = readl(&aregs->rxdma.psr_full_offset);
	regs_buff[num++] = readl(&aregs->rxdma.psr_access_index);
	regs_buff[num++] = readl(&aregs->rxdma.psr_min_des);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_base_lo);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_base_hi);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_num_des);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_avail_offset);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_full_offset);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_rd_index);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_min_des);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_base_lo);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_base_hi);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_num_des);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_avail_offset);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_full_offset);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_rd_index);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_min_des);
}

#define ET131X_DRVINFO_LEN 32 /* value from ethtool.h */
static void et131x_get_drvinfo(struct net_device *netdev,
			       struct ethtool_drvinfo *info)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	strncpy(info->driver, DRIVER_NAME, ET131X_DRVINFO_LEN);
	strncpy(info->version, DRIVER_VERSION, ET131X_DRVINFO_LEN);
	strncpy(info->bus_info, pci_name(adapter->pdev), ET131X_DRVINFO_LEN);
}

static struct ethtool_ops et131x_ethtool_ops = {
	.get_settings	= et131x_get_settings,
	.set_settings	= et131x_set_settings,
	.get_drvinfo	= et131x_get_drvinfo,
	.get_regs_len	= et131x_get_regs_len,
	.get_regs	= et131x_get_regs,
	.get_link = ethtool_op_get_link,
};
/**
 * et131x_hwaddr_init - set up the MAC Address on the ET1310
 * @adapter: pointer to our private adapter structure
 */
static void et131x_hwaddr_init(struct et131x_adapter *adapter)
{
	/* If have our default mac from init and no mac address from
	 * EEPROM then we need to generate the last octet and set it on the
	 * device
	 */
	if (is_zero_ether_addr(adapter->rom_addr)) {
		/*
		 * We need to randomly generate the last octet so we
		 * decrease our chances of setting the mac address to
		 * same as another one of our cards in the system
		 */
		get_random_bytes(&adapter->addr[5], 1);
		/*
		 * We have the default value in the register we are
		 * working with so we need to copy the current
		 * address into the permanent address
		 */
		memcpy(adapter->rom_addr,
			adapter->addr, ETH_ALEN);
	} else {
		/* We do not have an override address, so set the
		 * current address to the permanent address and add
		 * it to the device
		 */
		memcpy(adapter->addr,
		       adapter->rom_addr, ETH_ALEN);
	}
}

/**
 * et131x_pci_init	 - initial PCI setup
 * @adapter: pointer to our private adapter structure
 * @pdev: our PCI device
 *
 * Perform the initial setup of PCI registers and if possible initialise
 * the MAC address. At this point the I/O registers have yet to be mapped
 */
static int et131x_pci_init(struct et131x_adapter *adapter,
						struct pci_dev *pdev)
{
	int cap = pci_pcie_cap(pdev);
	u16 max_payload;
	u16 ctl;
	int i, rc;

	rc = et131x_init_eeprom(adapter);
	if (rc < 0)
		goto out;

	if (!cap) {
		dev_err(&pdev->dev, "Missing PCIe capabilities\n");
		goto err_out;
	}

	/* Let's set up the PORT LOGIC Register.  First we need to know what
	 * the max_payload_size is
	 */
	if (pci_read_config_word(pdev, cap + PCI_EXP_DEVCAP, &max_payload)) {
		dev_err(&pdev->dev,
		    "Could not read PCI config space for Max Payload Size\n");
		goto err_out;
	}

	/* Program the Ack/Nak latency and replay timers */
	max_payload &= 0x07;

	if (max_payload < 2) {
		static const u16 acknak[2] = { 0x76, 0xD0 };
		static const u16 replay[2] = { 0x1E0, 0x2ED };

		if (pci_write_config_word(pdev, ET1310_PCI_ACK_NACK,
					       acknak[max_payload])) {
			dev_err(&pdev->dev,
			  "Could not write PCI config space for ACK/NAK\n");
			goto err_out;
		}
		if (pci_write_config_word(pdev, ET1310_PCI_REPLAY,
					       replay[max_payload])) {
			dev_err(&pdev->dev,
			  "Could not write PCI config space for Replay Timer\n");
			goto err_out;
		}
	}

	/* l0s and l1 latency timers.  We are using default values.
	 * Representing 001 for L0s and 010 for L1
	 */
	if (pci_write_config_byte(pdev, ET1310_PCI_L0L1LATENCY, 0x11)) {
		dev_err(&pdev->dev,
		  "Could not write PCI config space for Latency Timers\n");
		goto err_out;
	}

	/* Change the max read size to 2k */
	if (pci_read_config_word(pdev, cap + PCI_EXP_DEVCTL, &ctl)) {
		dev_err(&pdev->dev,
			"Could not read PCI config space for Max read size\n");
		goto err_out;
	}

	ctl = (ctl & ~PCI_EXP_DEVCTL_READRQ) | (0x04 << 12);

	if (pci_write_config_word(pdev, cap + PCI_EXP_DEVCTL, ctl)) {
		dev_err(&pdev->dev,
		      "Could not write PCI config space for Max read size\n");
		goto err_out;
	}

	/* Get MAC address from config space if an eeprom exists, otherwise
	 * the MAC address there will not be valid
	 */
	if (!adapter->has_eeprom) {
		et131x_hwaddr_init(adapter);
		return 0;
	}

	for (i = 0; i < ETH_ALEN; i++) {
		if (pci_read_config_byte(pdev, ET1310_PCI_MAC_ADDRESS + i,
					adapter->rom_addr + i)) {
			dev_err(&pdev->dev, "Could not read PCI config space for MAC address\n");
			goto err_out;
		}
	}
	memcpy(adapter->addr, adapter->rom_addr, ETH_ALEN);
out:
	return rc;
err_out:
	rc = -EIO;
	goto out;
}

/**
 * et131x_error_timer_handler
 * @data: timer-specific variable; here a pointer to our adapter structure
 *
 * The routine called when the error timer expires, to track the number of
 * recurring errors.
 */
static void et131x_error_timer_handler(unsigned long data)
{
	struct et131x_adapter *adapter = (struct et131x_adapter *) data;
	struct phy_device *phydev = adapter->phydev;

	if (et1310_in_phy_coma(adapter)) {
		/* Bring the device immediately out of coma, to
		 * prevent it from sleeping indefinitely, this
		 * mechanism could be improved! */
		et1310_disable_phy_coma(adapter);
		adapter->boot_coma = 20;
	} else {
		et1310_update_macstat_host_counters(adapter);
	}

	if (!phydev->link && adapter->boot_coma < 11)
		adapter->boot_coma++;

	if (adapter->boot_coma == 10) {
		if (!phydev->link) {
			if (!et1310_in_phy_coma(adapter)) {
				/* NOTE - This was originally a 'sync with
				 *  interrupt'. How to do that under Linux?
				 */
				et131x_enable_interrupts(adapter);
				et1310_enable_phy_coma(adapter);
			}
		}
	}

	/* This is a periodic timer, so reschedule */
	mod_timer(&adapter->error_timer, jiffies +
					  TX_ERROR_PERIOD * HZ / 1000);
}

/**
 * et131x_adapter_memory_alloc
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success, errno on failure (as defined in errno.h).
 *
 * Allocate all the memory blocks for send, receive and others.
 */
static int et131x_adapter_memory_alloc(struct et131x_adapter *adapter)
{
	int status;

	/* Allocate memory for the Tx Ring */
	status = et131x_tx_dma_memory_alloc(adapter);
	if (status != 0) {
		dev_err(&adapter->pdev->dev,
			  "et131x_tx_dma_memory_alloc FAILED\n");
		return status;
	}
	/* Receive buffer memory allocation */
	status = et131x_rx_dma_memory_alloc(adapter);
	if (status != 0) {
		dev_err(&adapter->pdev->dev,
			  "et131x_rx_dma_memory_alloc FAILED\n");
		et131x_tx_dma_memory_free(adapter);
		return status;
	}

	/* Init receive data structures */
	status = et131x_init_recv(adapter);
	if (status != 0) {
		dev_err(&adapter->pdev->dev,
			"et131x_init_recv FAILED\n");
		et131x_tx_dma_memory_free(adapter);
		et131x_rx_dma_memory_free(adapter);
	}
	return status;
}

/**
 * et131x_adapter_memory_free - Free all memory allocated for use by Tx & Rx
 * @adapter: pointer to our private adapter structure
 */
static void et131x_adapter_memory_free(struct et131x_adapter *adapter)
{
	/* Free DMA memory */
	et131x_tx_dma_memory_free(adapter);
	et131x_rx_dma_memory_free(adapter);
}

static void et131x_adjust_link(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);
	struct  phy_device *phydev = adapter->phydev;

	if (netif_carrier_ok(netdev)) {
		adapter->boot_coma = 20;

		if (phydev && phydev->speed == SPEED_10) {
			/*
			 * NOTE - Is there a way to query this without
			 * TruePHY?
			 * && TRU_QueryCoreType(adapter->hTruePhy, 0)==
			 * EMI_TRUEPHY_A13O) {
			 */
			u16 register18;

			et131x_mii_read(adapter, PHY_MPHY_CONTROL_REG,
					 &register18);
			et131x_mii_write(adapter, PHY_MPHY_CONTROL_REG,
					 register18 | 0x4);
			et131x_mii_write(adapter, PHY_INDEX_REG,
					 register18 | 0x8402);
			et131x_mii_write(adapter, PHY_DATA_REG,
					 register18 | 511);
			et131x_mii_write(adapter, PHY_MPHY_CONTROL_REG,
					 register18);
		}

		et1310_config_flow_control(adapter);

		if (phydev && phydev->speed == SPEED_1000 &&
				adapter->registry_jumbo_packet > 2048) {
			u16 reg;

			et131x_mii_read(adapter, PHY_CONFIG, &reg);
			reg &= ~ET_PHY_CONFIG_TX_FIFO_DEPTH;
			reg |= ET_PHY_CONFIG_FIFO_DEPTH_32;
			et131x_mii_write(adapter, PHY_CONFIG, reg);
		}

		et131x_set_rx_dma_timer(adapter);
		et1310_config_mac_regs2(adapter);
	}

	if (phydev && phydev->link != adapter->link) {
		/*
		 * Check to see if we are in coma mode and if
		 * so, disable it because we will not be able
		 * to read PHY values until we are out.
		 */
		if (et1310_in_phy_coma(adapter))
			et1310_disable_phy_coma(adapter);

		if (phydev->link) {
			adapter->boot_coma = 20;
		} else {
			dev_warn(&adapter->pdev->dev,
			    "Link down - cable problem ?\n");
			adapter->boot_coma = 0;

			if (phydev->speed == SPEED_10) {
				/* NOTE - Is there a way to query this without
				 * TruePHY?
				 * && TRU_QueryCoreType(adapter->hTruePhy, 0) ==
				 * EMI_TRUEPHY_A13O)
				 */
				u16 register18;

				et131x_mii_read(adapter, PHY_MPHY_CONTROL_REG,
						 &register18);
				et131x_mii_write(adapter, PHY_MPHY_CONTROL_REG,
						 register18 | 0x4);
				et131x_mii_write(adapter, PHY_INDEX_REG,
						 register18 | 0x8402);
				et131x_mii_write(adapter, PHY_DATA_REG,
						 register18 | 511);
				et131x_mii_write(adapter, PHY_MPHY_CONTROL_REG,
						 register18);
			}

			/* Free the packets being actively sent & stopped */
			et131x_free_busy_send_packets(adapter);

			/* Re-initialize the send structures */
			et131x_init_send(adapter);

			/*
			 * Bring the device back to the state it was during
			 * init prior to autonegotiation being complete. This
			 * way, when we get the auto-neg complete interrupt,
			 * we can complete init by calling config_mac_regs2.
			 */
			et131x_soft_reset(adapter);

			/* Setup ET1310 as per the documentation */
			et131x_adapter_setup(adapter);

			/* perform reset of tx/rx */
			et131x_disable_txrx(netdev);
			et131x_enable_txrx(netdev);
		}

		adapter->link = phydev->link;

		phy_print_status(phydev);
	}
}

static int et131x_mii_probe(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);
	struct  phy_device *phydev = NULL;

	phydev = phy_find_first(adapter->mii_bus);
	if (!phydev) {
		dev_err(&adapter->pdev->dev, "no PHY found\n");
		return -ENODEV;
	}

	phydev = phy_connect(netdev, dev_name(&phydev->dev),
			&et131x_adjust_link, 0, PHY_INTERFACE_MODE_MII);

	if (IS_ERR(phydev)) {
		dev_err(&adapter->pdev->dev, "Could not attach to PHY\n");
		return PTR_ERR(phydev);
	}

	phydev->supported &= (SUPPORTED_10baseT_Half
				| SUPPORTED_10baseT_Full
				| SUPPORTED_100baseT_Half
				| SUPPORTED_100baseT_Full
				| SUPPORTED_Autoneg
				| SUPPORTED_MII
				| SUPPORTED_TP);

	if (adapter->pdev->device != ET131X_PCI_DEVICE_ID_FAST)
		phydev->supported |= SUPPORTED_1000baseT_Full;

	phydev->advertising = phydev->supported;
	adapter->phydev = phydev;

	dev_info(&adapter->pdev->dev, "attached PHY driver [%s] (mii_bus:phy_addr=%s)\n",
		 phydev->drv->name, dev_name(&phydev->dev));

	return 0;
}

/**
 * et131x_adapter_init
 * @adapter: pointer to the private adapter struct
 * @pdev: pointer to the PCI device
 *
 * Initialize the data structures for the et131x_adapter object and link
 * them together with the platform provided device structures.
 */
static struct et131x_adapter *et131x_adapter_init(struct net_device *netdev,
		struct pci_dev *pdev)
{
	static const u8 default_mac[] = { 0x00, 0x05, 0x3d, 0x00, 0x02, 0x00 };

	struct et131x_adapter *adapter;

	/* Allocate private adapter struct and copy in relevant information */
	adapter = netdev_priv(netdev);
	adapter->pdev = pci_dev_get(pdev);
	adapter->netdev = netdev;

	/* Initialize spinlocks here */
	spin_lock_init(&adapter->lock);
	spin_lock_init(&adapter->tcb_send_qlock);
	spin_lock_init(&adapter->tcb_ready_qlock);
	spin_lock_init(&adapter->send_hw_lock);
	spin_lock_init(&adapter->rcv_lock);
	spin_lock_init(&adapter->rcv_pend_lock);
	spin_lock_init(&adapter->fbr_lock);
	spin_lock_init(&adapter->phy_lock);

	adapter->registry_jumbo_packet = 1514;	/* 1514-9216 */

	/* Set the MAC address to a default */
	memcpy(adapter->addr, default_mac, ETH_ALEN);

	return adapter;
}

/**
 * et131x_pci_remove
 * @pdev: a pointer to the device's pci_dev structure
 *
 * Registered in the pci_driver structure, this function is called when the
 * PCI subsystem detects that a PCI device which matches the information
 * contained in the pci_device_id table has been removed.
 */
static void __devexit et131x_pci_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct et131x_adapter *adapter = netdev_priv(netdev);

	unregister_netdev(netdev);
	phy_disconnect(adapter->phydev);
	mdiobus_unregister(adapter->mii_bus);
	kfree(adapter->mii_bus->irq);
	mdiobus_free(adapter->mii_bus);

	et131x_adapter_memory_free(adapter);
	iounmap(adapter->regs);
	pci_dev_put(pdev);

	free_netdev(netdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

/**
 * et131x_up - Bring up a device for use.
 * @netdev: device to be opened
 */
static void et131x_up(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	et131x_enable_txrx(netdev);
	phy_start(adapter->phydev);
}

/**
 * et131x_down - Bring down the device
 * @netdev: device to be brought down
 */
static void et131x_down(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	/* Save the timestamp for the TX watchdog, prevent a timeout */
	netdev->trans_start = jiffies;

	phy_stop(adapter->phydev);
	et131x_disable_txrx(netdev);
}

#ifdef CONFIG_PM_SLEEP
static int et131x_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);

	if (netif_running(netdev)) {
		netif_device_detach(netdev);
		et131x_down(netdev);
		pci_save_state(pdev);
	}

	return 0;
}

static int et131x_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);

	if (netif_running(netdev)) {
		pci_restore_state(pdev);
		et131x_up(netdev);
		netif_device_attach(netdev);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(et131x_pm_ops, et131x_suspend, et131x_resume);
#define ET131X_PM_OPS (&et131x_pm_ops)
#else
#define ET131X_PM_OPS NULL
#endif

/**
 * et131x_isr - The Interrupt Service Routine for the driver.
 * @irq: the IRQ on which the interrupt was received.
 * @dev_id: device-specific info (here a pointer to a net_device struct)
 *
 * Returns a value indicating if the interrupt was handled.
 */
irqreturn_t et131x_isr(int irq, void *dev_id)
{
	bool handled = true;
	struct net_device *netdev = (struct net_device *)dev_id;
	struct et131x_adapter *adapter = NULL;
	u32 status;

	if (!netif_device_present(netdev)) {
		handled = false;
		goto out;
	}

	adapter = netdev_priv(netdev);

	/* If the adapter is in low power state, then it should not
	 * recognize any interrupt
	 */

	/* Disable Device Interrupts */
	et131x_disable_interrupts(adapter);

	/* Get a copy of the value in the interrupt status register
	 * so we can process the interrupting section
	 */
	status = readl(&adapter->regs->global.int_status);

	if (adapter->flowcontrol == FLOW_TXONLY ||
	    adapter->flowcontrol == FLOW_BOTH) {
		status &= ~INT_MASK_ENABLE;
	} else {
		status &= ~INT_MASK_ENABLE_NO_FLOW;
	}

	/* Make sure this is our interrupt */
	if (!status) {
		handled = false;
		et131x_enable_interrupts(adapter);
		goto out;
	}

	/* This is our interrupt, so process accordingly */

	if (status & ET_INTR_WATCHDOG) {
		struct tcb *tcb = adapter->tx_ring.send_head;

		if (tcb)
			if (++tcb->stale > 1)
				status |= ET_INTR_TXDMA_ISR;

		if (adapter->rx_ring.unfinished_receives)
			status |= ET_INTR_RXDMA_XFR_DONE;
		else if (tcb == NULL)
			writel(0, &adapter->regs->global.watchdog_timer);

		status &= ~ET_INTR_WATCHDOG;
	}

	if (status == 0) {
		/* This interrupt has in some way been "handled" by
		 * the ISR. Either it was a spurious Rx interrupt, or
		 * it was a Tx interrupt that has been filtered by
		 * the ISR.
		 */
		et131x_enable_interrupts(adapter);
		goto out;
	}

	/* We need to save the interrupt status value for use in our
	 * DPC. We will clear the software copy of that in that
	 * routine.
	 */
	adapter->stats.interrupt_status = status;

	/* Schedule the ISR handler as a bottom-half task in the
	 * kernel's tq_immediate queue, and mark the queue for
	 * execution
	 */
	schedule_work(&adapter->task);
out:
	return IRQ_RETVAL(handled);
}

/**
 * et131x_isr_handler - The ISR handler
 * @p_adapter, a pointer to the device's private adapter structure
 *
 * scheduled to run in a deferred context by the ISR. This is where the ISR's
 * work actually gets done.
 */
static void et131x_isr_handler(struct work_struct *work)
{
	struct et131x_adapter *adapter =
		container_of(work, struct et131x_adapter, task);
	u32 status = adapter->stats.interrupt_status;
	struct address_map __iomem *iomem = adapter->regs;

	/*
	 * These first two are by far the most common.  Once handled, we clear
	 * their two bits in the status word.  If the word is now zero, we
	 * exit.
	 */
	/* Handle all the completed Transmit interrupts */
	if (status & ET_INTR_TXDMA_ISR)
		et131x_handle_send_interrupt(adapter);

	/* Handle all the completed Receives interrupts */
	if (status & ET_INTR_RXDMA_XFR_DONE)
		et131x_handle_recv_interrupt(adapter);

	status &= 0xffffffd7;

	if (status) {
		/* Handle the TXDMA Error interrupt */
		if (status & ET_INTR_TXDMA_ERR) {
			u32 txdma_err;

			/* Following read also clears the register (COR) */
			txdma_err = readl(&iomem->txdma.tx_dma_error);

			dev_warn(&adapter->pdev->dev,
				    "TXDMA_ERR interrupt, error = %d\n",
				    txdma_err);
		}

		/* Handle Free Buffer Ring 0 and 1 Low interrupt */
		if (status &
		    (ET_INTR_RXDMA_FB_R0_LOW | ET_INTR_RXDMA_FB_R1_LOW)) {
			/*
			 * This indicates the number of unused buffers in
			 * RXDMA free buffer ring 0 is <= the limit you
			 * programmed. Free buffer resources need to be
			 * returned.  Free buffers are consumed as packets
			 * are passed from the network to the host. The host
			 * becomes aware of the packets from the contents of
			 * the packet status ring. This ring is queried when
			 * the packet done interrupt occurs. Packets are then
			 * passed to the OS. When the OS is done with the
			 * packets the resources can be returned to the
			 * ET1310 for re-use. This interrupt is one method of
			 * returning resources.
			 */

			/* If the user has flow control on, then we will
			 * send a pause packet, otherwise just exit
			 */
			if (adapter->flowcontrol == FLOW_TXONLY ||
			    adapter->flowcontrol == FLOW_BOTH) {
				u32 pm_csr;

				/* Tell the device to send a pause packet via
				 * the back pressure register (bp req  and
				 * bp xon/xoff)
				 */
				pm_csr = readl(&iomem->global.pm_csr);
				if (!et1310_in_phy_coma(adapter))
					writel(3, &iomem->txmac.bp_ctrl);
			}
		}

		/* Handle Packet Status Ring Low Interrupt */
		if (status & ET_INTR_RXDMA_STAT_LOW) {

			/*
			 * Same idea as with the two Free Buffer Rings.
			 * Packets going from the network to the host each
			 * consume a free buffer resource and a packet status
			 * resource.  These resoures are passed to the OS.
			 * When the OS is done with the resources, they need
			 * to be returned to the ET1310. This is one method
			 * of returning the resources.
			 */
		}

		/* Handle RXDMA Error Interrupt */
		if (status & ET_INTR_RXDMA_ERR) {
			/*
			 * The rxdma_error interrupt is sent when a time-out
			 * on a request issued by the JAGCore has occurred or
			 * a completion is returned with an un-successful
			 * status.  In both cases the request is considered
			 * complete. The JAGCore will automatically re-try the
			 * request in question. Normally information on events
			 * like these are sent to the host using the "Advanced
			 * Error Reporting" capability. This interrupt is
			 * another way of getting similar information. The
			 * only thing required is to clear the interrupt by
			 * reading the ISR in the global resources. The
			 * JAGCore will do a re-try on the request.  Normally
			 * you should never see this interrupt. If you start
			 * to see this interrupt occurring frequently then
			 * something bad has occurred. A reset might be the
			 * thing to do.
			 */
			/* TRAP();*/

			dev_warn(&adapter->pdev->dev,
				    "RxDMA_ERR interrupt, error %x\n",
				    readl(&iomem->txmac.tx_test));
		}

		/* Handle the Wake on LAN Event */
		if (status & ET_INTR_WOL) {
			/*
			 * This is a secondary interrupt for wake on LAN.
			 * The driver should never see this, if it does,
			 * something serious is wrong. We will TRAP the
			 * message when we are in DBG mode, otherwise we
			 * will ignore it.
			 */
			dev_err(&adapter->pdev->dev, "WAKE_ON_LAN interrupt\n");
		}

		/* Let's move on to the TxMac */
		if (status & ET_INTR_TXMAC) {
			u32 err = readl(&iomem->txmac.err);

			/*
			 * When any of the errors occur and TXMAC generates
			 * an interrupt to report these errors, it usually
			 * means that TXMAC has detected an error in the data
			 * stream retrieved from the on-chip Tx Q. All of
			 * these errors are catastrophic and TXMAC won't be
			 * able to recover data when these errors occur.  In
			 * a nutshell, the whole Tx path will have to be reset
			 * and re-configured afterwards.
			 */
			dev_warn(&adapter->pdev->dev,
				    "TXMAC interrupt, error 0x%08x\n",
				    err);

			/* If we are debugging, we want to see this error,
			 * otherwise we just want the device to be reset and
			 * continue
			 */
		}

		/* Handle RXMAC Interrupt */
		if (status & ET_INTR_RXMAC) {
			/*
			 * These interrupts are catastrophic to the device,
			 * what we need to do is disable the interrupts and
			 * set the flag to cause us to reset so we can solve
			 * this issue.
			 */
			/* MP_SET_FLAG( adapter,
						fMP_ADAPTER_HARDWARE_ERROR); */

			dev_warn(&adapter->pdev->dev,
			  "RXMAC interrupt, error 0x%08x.  Requesting reset\n",
				    readl(&iomem->rxmac.err_reg));

			dev_warn(&adapter->pdev->dev,
				    "Enable 0x%08x, Diag 0x%08x\n",
				    readl(&iomem->rxmac.ctrl),
				    readl(&iomem->rxmac.rxq_diag));

			/*
			 * If we are debugging, we want to see this error,
			 * otherwise we just want the device to be reset and
			 * continue
			 */
		}

		/* Handle MAC_STAT Interrupt */
		if (status & ET_INTR_MAC_STAT) {
			/*
			 * This means at least one of the un-masked counters
			 * in the MAC_STAT block has rolled over.  Use this
			 * to maintain the top, software managed bits of the
			 * counter(s).
			 */
			et1310_handle_macstat_interrupt(adapter);
		}

		/* Handle SLV Timeout Interrupt */
		if (status & ET_INTR_SLV_TIMEOUT) {
			/*
			 * This means a timeout has occurred on a read or
			 * write request to one of the JAGCore registers. The
			 * Global Resources block has terminated the request
			 * and on a read request, returned a "fake" value.
			 * The most likely reasons are: Bad Address or the
			 * addressed module is in a power-down state and
			 * can't respond.
			 */
		}
	}
	et131x_enable_interrupts(adapter);
}

/**
 * et131x_stats - Return the current device statistics.
 * @netdev: device whose stats are being queried
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
static struct net_device_stats *et131x_stats(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);
	struct net_device_stats *stats = &adapter->net_stats;
	struct ce_stats *devstat = &adapter->stats;

	stats->rx_errors = devstat->rx_length_errs +
			   devstat->rx_align_errs +
			   devstat->rx_crc_errs +
			   devstat->rx_code_violations +
			   devstat->rx_other_errs;
	stats->tx_errors = devstat->tx_max_pkt_errs;
	stats->multicast = devstat->multicast_pkts_rcvd;
	stats->collisions = devstat->tx_collisions;

	stats->rx_length_errors = devstat->rx_length_errs;
	stats->rx_over_errors = devstat->rx_overflows;
	stats->rx_crc_errors = devstat->rx_crc_errs;

	/* NOTE: These stats don't have corresponding values in CE_STATS,
	 * so we're going to have to update these directly from within the
	 * TX/RX code
	 */
	/* stats->rx_bytes            = 20; devstat->; */
	/* stats->tx_bytes            = 20;  devstat->; */
	/* stats->rx_dropped          = devstat->; */
	/* stats->tx_dropped          = devstat->; */

	/*  NOTE: Not used, can't find analogous statistics */
	/* stats->rx_frame_errors     = devstat->; */
	/* stats->rx_fifo_errors      = devstat->; */
	/* stats->rx_missed_errors    = devstat->; */

	/* stats->tx_aborted_errors   = devstat->; */
	/* stats->tx_carrier_errors   = devstat->; */
	/* stats->tx_fifo_errors      = devstat->; */
	/* stats->tx_heartbeat_errors = devstat->; */
	/* stats->tx_window_errors    = devstat->; */
	return stats;
}

/**
 * et131x_open - Open the device for use.
 * @netdev: device to be opened
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
static int et131x_open(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);
	struct pci_dev *pdev = adapter->pdev;
	unsigned int irq = pdev->irq;
	int result;

	/* Start the timer to track NIC errors */
	init_timer(&adapter->error_timer);
	adapter->error_timer.expires = jiffies + TX_ERROR_PERIOD * HZ / 1000;
	adapter->error_timer.function = et131x_error_timer_handler;
	adapter->error_timer.data = (unsigned long)adapter;
	add_timer(&adapter->error_timer);

	result = request_irq(irq, et131x_isr,
			     IRQF_SHARED, netdev->name, netdev);
	if (result) {
		dev_err(&pdev->dev, "could not register IRQ %d\n", irq);
		return result;
	}

	adapter->flags |= fMP_ADAPTER_INTERRUPT_IN_USE;

	et131x_up(netdev);

	return result;
}

/**
 * et131x_close - Close the device
 * @netdev: device to be closed
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
static int et131x_close(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	et131x_down(netdev);

	adapter->flags &= ~fMP_ADAPTER_INTERRUPT_IN_USE;
	free_irq(adapter->pdev->irq, netdev);

	/* Stop the error timer */
	return del_timer_sync(&adapter->error_timer);
}

/**
 * et131x_ioctl - The I/O Control handler for the driver
 * @netdev: device on which the control request is being made
 * @reqbuf: a pointer to the IOCTL request buffer
 * @cmd: the IOCTL command code
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
static int et131x_ioctl(struct net_device *netdev, struct ifreq *reqbuf,
			int cmd)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	if (!adapter->phydev)
		return -EINVAL;

	return phy_mii_ioctl(adapter->phydev, reqbuf, cmd);
}

/**
 * et131x_set_packet_filter - Configures the Rx Packet filtering on the device
 * @adapter: pointer to our private adapter structure
 *
 * FIXME: lot of dups with MAC code
 *
 * Returns 0 on success, errno on failure
 */
static int et131x_set_packet_filter(struct et131x_adapter *adapter)
{
	int filter = adapter->packet_filter;
	int status = 0;
	u32 ctrl;
	u32 pf_ctrl;

	ctrl = readl(&adapter->regs->rxmac.ctrl);
	pf_ctrl = readl(&adapter->regs->rxmac.pf_ctrl);

	/* Default to disabled packet filtering.  Enable it in the individual
	 * case statements that require the device to filter something
	 */
	ctrl |= 0x04;

	/* Set us to be in promiscuous mode so we receive everything, this
	 * is also true when we get a packet filter of 0
	 */
	if ((filter & ET131X_PACKET_TYPE_PROMISCUOUS) || filter == 0)
		pf_ctrl &= ~7;	/* Clear filter bits */
	else {
		/*
		 * Set us up with Multicast packet filtering.  Three cases are
		 * possible - (1) we have a multi-cast list, (2) we receive ALL
		 * multicast entries or (3) we receive none.
		 */
		if (filter & ET131X_PACKET_TYPE_ALL_MULTICAST)
			pf_ctrl &= ~2;	/* Multicast filter bit */
		else {
			et1310_setup_device_for_multicast(adapter);
			pf_ctrl |= 2;
			ctrl &= ~0x04;
		}

		/* Set us up with Unicast packet filtering */
		if (filter & ET131X_PACKET_TYPE_DIRECTED) {
			et1310_setup_device_for_unicast(adapter);
			pf_ctrl |= 4;
			ctrl &= ~0x04;
		}

		/* Set us up with Broadcast packet filtering */
		if (filter & ET131X_PACKET_TYPE_BROADCAST) {
			pf_ctrl |= 1;	/* Broadcast filter bit */
			ctrl &= ~0x04;
		} else
			pf_ctrl &= ~1;

		/* Setup the receive mac configuration registers - Packet
		 * Filter control + the enable / disable for packet filter
		 * in the control reg.
		 */
		writel(pf_ctrl, &adapter->regs->rxmac.pf_ctrl);
		writel(ctrl, &adapter->regs->rxmac.ctrl);
	}
	return status;
}

/**
 * et131x_multicast - The handler to configure multicasting on the interface
 * @netdev: a pointer to a net_device struct representing the device
 */
static void et131x_multicast(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);
	int packet_filter;
	unsigned long flags;
	struct netdev_hw_addr *ha;
	int i;

	spin_lock_irqsave(&adapter->lock, flags);

	/* Before we modify the platform-independent filter flags, store them
	 * locally. This allows us to determine if anything's changed and if
	 * we even need to bother the hardware
	 */
	packet_filter = adapter->packet_filter;

	/* Clear the 'multicast' flag locally; because we only have a single
	 * flag to check multicast, and multiple multicast addresses can be
	 * set, this is the easiest way to determine if more than one
	 * multicast address is being set.
	 */
	packet_filter &= ~ET131X_PACKET_TYPE_MULTICAST;

	/* Check the net_device flags and set the device independent flags
	 * accordingly
	 */

	if (netdev->flags & IFF_PROMISC)
		adapter->packet_filter |= ET131X_PACKET_TYPE_PROMISCUOUS;
	else
		adapter->packet_filter &= ~ET131X_PACKET_TYPE_PROMISCUOUS;

	if (netdev->flags & IFF_ALLMULTI)
		adapter->packet_filter |= ET131X_PACKET_TYPE_ALL_MULTICAST;

	if (netdev_mc_count(netdev) > NIC_MAX_MCAST_LIST)
		adapter->packet_filter |= ET131X_PACKET_TYPE_ALL_MULTICAST;

	if (netdev_mc_count(netdev) < 1) {
		adapter->packet_filter &= ~ET131X_PACKET_TYPE_ALL_MULTICAST;
		adapter->packet_filter &= ~ET131X_PACKET_TYPE_MULTICAST;
	} else
		adapter->packet_filter |= ET131X_PACKET_TYPE_MULTICAST;

	/* Set values in the private adapter struct */
	i = 0;
	netdev_for_each_mc_addr(ha, netdev) {
		if (i == NIC_MAX_MCAST_LIST)
			break;
		memcpy(adapter->multicast_list[i++], ha->addr, ETH_ALEN);
	}
	adapter->multicast_addr_count = i;

	/* Are the new flags different from the previous ones? If not, then no
	 * action is required
	 *
	 * NOTE - This block will always update the multicast_list with the
	 *        hardware, even if the addresses aren't the same.
	 */
	if (packet_filter != adapter->packet_filter) {
		/* Call the device's filter function */
		et131x_set_packet_filter(adapter);
	}
	spin_unlock_irqrestore(&adapter->lock, flags);
}

/**
 * et131x_tx - The handler to tx a packet on the device
 * @skb: data to be Tx'd
 * @netdev: device on which data is to be Tx'd
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
static int et131x_tx(struct sk_buff *skb, struct net_device *netdev)
{
	int status = 0;
	struct et131x_adapter *adapter = netdev_priv(netdev);

	/* stop the queue if it's getting full */
	if (adapter->tx_ring.used >= NUM_TCB - 1 &&
	    !netif_queue_stopped(netdev))
		netif_stop_queue(netdev);

	/* Save the timestamp for the TX timeout watchdog */
	netdev->trans_start = jiffies;

	/* Call the device-specific data Tx routine */
	status = et131x_send_packets(skb, netdev);

	/* Check status and manage the netif queue if necessary */
	if (status != 0) {
		if (status == -ENOMEM)
			status = NETDEV_TX_BUSY;
		else
			status = NETDEV_TX_OK;
	}
	return status;
}

/**
 * et131x_tx_timeout - Timeout handler
 * @netdev: a pointer to a net_device struct representing the device
 *
 * The handler called when a Tx request times out. The timeout period is
 * specified by the 'tx_timeo" element in the net_device structure (see
 * et131x_alloc_device() to see how this value is set).
 */
static void et131x_tx_timeout(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);
	struct tcb *tcb;
	unsigned long flags;

	/* If the device is closed, ignore the timeout */
	if (~(adapter->flags & fMP_ADAPTER_INTERRUPT_IN_USE))
		return;

	/* Any nonrecoverable hardware error?
	 * Checks adapter->flags for any failure in phy reading
	 */
	if (adapter->flags & fMP_ADAPTER_NON_RECOVER_ERROR)
		return;

	/* Hardware failure? */
	if (adapter->flags & fMP_ADAPTER_HARDWARE_ERROR) {
		dev_err(&adapter->pdev->dev, "hardware error - reset\n");
		return;
	}

	/* Is send stuck? */
	spin_lock_irqsave(&adapter->tcb_send_qlock, flags);

	tcb = adapter->tx_ring.send_head;

	if (tcb != NULL) {
		tcb->count++;

		if (tcb->count > NIC_SEND_HANG_THRESHOLD) {
			spin_unlock_irqrestore(&adapter->tcb_send_qlock,
					       flags);

			dev_warn(&adapter->pdev->dev,
				"Send stuck - reset.  tcb->WrIndex %x, flags 0x%08x\n",
				tcb->index,
				tcb->flags);

			adapter->net_stats.tx_errors++;

			/* perform reset of tx/rx */
			et131x_disable_txrx(netdev);
			et131x_enable_txrx(netdev);
			return;
		}
	}

	spin_unlock_irqrestore(&adapter->tcb_send_qlock, flags);
}

/**
 * et131x_change_mtu - The handler called to change the MTU for the device
 * @netdev: device whose MTU is to be changed
 * @new_mtu: the desired MTU
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
static int et131x_change_mtu(struct net_device *netdev, int new_mtu)
{
	int result = 0;
	struct et131x_adapter *adapter = netdev_priv(netdev);

	/* Make sure the requested MTU is valid */
	if (new_mtu < 64 || new_mtu > 9216)
		return -EINVAL;

	et131x_disable_txrx(netdev);
	et131x_handle_send_interrupt(adapter);
	et131x_handle_recv_interrupt(adapter);

	/* Set the new MTU */
	netdev->mtu = new_mtu;

	/* Free Rx DMA memory */
	et131x_adapter_memory_free(adapter);

	/* Set the config parameter for Jumbo Packet support */
	adapter->registry_jumbo_packet = new_mtu + 14;
	et131x_soft_reset(adapter);

	/* Alloc and init Rx DMA memory */
	result = et131x_adapter_memory_alloc(adapter);
	if (result != 0) {
		dev_warn(&adapter->pdev->dev,
			"Change MTU failed; couldn't re-alloc DMA memory\n");
		return result;
	}

	et131x_init_send(adapter);

	et131x_hwaddr_init(adapter);
	memcpy(netdev->dev_addr, adapter->addr, ETH_ALEN);

	/* Init the device with the new settings */
	et131x_adapter_setup(adapter);

	et131x_enable_txrx(netdev);

	return result;
}

/**
 * et131x_set_mac_addr - handler to change the MAC address for the device
 * @netdev: device whose MAC is to be changed
 * @new_mac: the desired MAC address
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 *
 * IMPLEMENTED BY : blux http://berndlux.de 22.01.2007 21:14
 */
static int et131x_set_mac_addr(struct net_device *netdev, void *new_mac)
{
	int result = 0;
	struct et131x_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *address = new_mac;

	/* begin blux */

	if (adapter == NULL)
		return -ENODEV;

	/* Make sure the requested MAC is valid */
	if (!is_valid_ether_addr(address->sa_data))
		return -EADDRNOTAVAIL;

	et131x_disable_txrx(netdev);
	et131x_handle_send_interrupt(adapter);
	et131x_handle_recv_interrupt(adapter);

	/* Set the new MAC */
	/* netdev->set_mac_address  = &new_mac; */

	memcpy(netdev->dev_addr, address->sa_data, netdev->addr_len);

	netdev_info(netdev, "Setting MAC address to %pM\n",
		    netdev->dev_addr);

	/* Free Rx DMA memory */
	et131x_adapter_memory_free(adapter);

	et131x_soft_reset(adapter);

	/* Alloc and init Rx DMA memory */
	result = et131x_adapter_memory_alloc(adapter);
	if (result != 0) {
		dev_err(&adapter->pdev->dev,
			"Change MAC failed; couldn't re-alloc DMA memory\n");
		return result;
	}

	et131x_init_send(adapter);

	et131x_hwaddr_init(adapter);

	/* Init the device with the new settings */
	et131x_adapter_setup(adapter);

	et131x_enable_txrx(netdev);

	return result;
}

static const struct net_device_ops et131x_netdev_ops = {
	.ndo_open		= et131x_open,
	.ndo_stop		= et131x_close,
	.ndo_start_xmit		= et131x_tx,
	.ndo_set_rx_mode	= et131x_multicast,
	.ndo_tx_timeout		= et131x_tx_timeout,
	.ndo_change_mtu		= et131x_change_mtu,
	.ndo_set_mac_address	= et131x_set_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_get_stats		= et131x_stats,
	.ndo_do_ioctl		= et131x_ioctl,
};

/**
 * et131x_pci_setup - Perform device initialization
 * @pdev: a pointer to the device's pci_dev structure
 * @ent: this device's entry in the pci_device_id table
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 *
 * Registered in the pci_driver structure, this function is called when the
 * PCI subsystem finds a new PCI device which matches the information
 * contained in the pci_device_id table. This routine is the equivalent to
 * a device insertion routine.
 */
static int __devinit et131x_pci_setup(struct pci_dev *pdev,
			       const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct et131x_adapter *adapter;
	int rc;
	int ii;

	rc = pci_enable_device(pdev);
	if (rc < 0) {
		dev_err(&pdev->dev, "pci_enable_device() failed\n");
		goto out;
	}

	/* Perform some basic PCI checks */
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "Can't find PCI device's base address\n");
		rc = -ENODEV;
		goto err_disable;
	}

	rc = pci_request_regions(pdev, DRIVER_NAME);
	if (rc < 0) {
		dev_err(&pdev->dev, "Can't get PCI resources\n");
		goto err_disable;
	}

	pci_set_master(pdev);

	/* Check the DMA addressing support of this device */
	if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))) {
		rc = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
		if (rc < 0) {
			dev_err(&pdev->dev,
			  "Unable to obtain 64 bit DMA for consistent allocations\n");
			goto err_release_res;
		}
	} else if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
		rc = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (rc < 0) {
			dev_err(&pdev->dev,
			  "Unable to obtain 32 bit DMA for consistent allocations\n");
			goto err_release_res;
		}
	} else {
		dev_err(&pdev->dev, "No usable DMA addressing method\n");
		rc = -EIO;
		goto err_release_res;
	}

	/* Allocate netdev and private adapter structs */
	netdev = alloc_etherdev(sizeof(struct et131x_adapter));
	if (!netdev) {
		dev_err(&pdev->dev, "Couldn't alloc netdev struct\n");
		rc = -ENOMEM;
		goto err_release_res;
	}

	netdev->watchdog_timeo = ET131X_TX_TIMEOUT;
	netdev->netdev_ops     = &et131x_netdev_ops;

	SET_NETDEV_DEV(netdev, &pdev->dev);
	SET_ETHTOOL_OPS(netdev, &et131x_ethtool_ops);

	adapter = et131x_adapter_init(netdev, pdev);

	rc = et131x_pci_init(adapter, pdev);
	if (rc < 0)
		goto err_free_dev;

	/* Map the bus-relative registers to system virtual memory */
	adapter->regs = pci_ioremap_bar(pdev, 0);
	if (!adapter->regs) {
		dev_err(&pdev->dev, "Cannot map device registers\n");
		rc = -ENOMEM;
		goto err_free_dev;
	}

	/* If Phy COMA mode was enabled when we went down, disable it here. */
	writel(ET_PMCSR_INIT,  &adapter->regs->global.pm_csr);

	/* Issue a global reset to the et1310 */
	et131x_soft_reset(adapter);

	/* Disable all interrupts (paranoid) */
	et131x_disable_interrupts(adapter);

	/* Allocate DMA memory */
	rc = et131x_adapter_memory_alloc(adapter);
	if (rc < 0) {
		dev_err(&pdev->dev, "Could not alloc adapater memory (DMA)\n");
		goto err_iounmap;
	}

	/* Init send data structures */
	et131x_init_send(adapter);

	/* Set up the task structure for the ISR's deferred handler */
	INIT_WORK(&adapter->task, et131x_isr_handler);

	/* Copy address into the net_device struct */
	memcpy(netdev->dev_addr, adapter->addr, ETH_ALEN);

	/* Init variable for counting how long we do not have link status */
	adapter->boot_coma = 0;
	et1310_disable_phy_coma(adapter);

	rc = -ENOMEM;

	/* Setup the mii_bus struct */
	adapter->mii_bus = mdiobus_alloc();
	if (!adapter->mii_bus) {
		dev_err(&pdev->dev, "Alloc of mii_bus struct failed\n");
		goto err_mem_free;
	}

	adapter->mii_bus->name = "et131x_eth_mii";
	snprintf(adapter->mii_bus->id, MII_BUS_ID_SIZE, "%x",
		(adapter->pdev->bus->number << 8) | adapter->pdev->devfn);
	adapter->mii_bus->priv = netdev;
	adapter->mii_bus->read = et131x_mdio_read;
	adapter->mii_bus->write = et131x_mdio_write;
	adapter->mii_bus->reset = et131x_mdio_reset;
	adapter->mii_bus->irq = kmalloc(sizeof(int)*PHY_MAX_ADDR, GFP_KERNEL);
	if (!adapter->mii_bus->irq) {
		dev_err(&pdev->dev, "mii_bus irq allocation failed\n");
		goto err_mdio_free;
	}

	for (ii = 0; ii < PHY_MAX_ADDR; ii++)
		adapter->mii_bus->irq[ii] = PHY_POLL;

	rc = mdiobus_register(adapter->mii_bus);
	if (rc < 0) {
		dev_err(&pdev->dev, "failed to register MII bus\n");
		goto err_mdio_free_irq;
	}

	rc = et131x_mii_probe(netdev);
	if (rc < 0) {
		dev_err(&pdev->dev, "failed to probe MII bus\n");
		goto err_mdio_unregister;
	}

	/* Setup et1310 as per the documentation */
	et131x_adapter_setup(adapter);

	/* We can enable interrupts now
	 *
	 *  NOTE - Because registration of interrupt handler is done in the
	 *         device's open(), defer enabling device interrupts to that
	 *         point
	 */

	/* Register the net_device struct with the Linux network layer */
	rc = register_netdev(netdev);
	if (rc < 0) {
		dev_err(&pdev->dev, "register_netdev() failed\n");
		goto err_phy_disconnect;
	}

	/* Register the net_device struct with the PCI subsystem. Save a copy
	 * of the PCI config space for this device now that the device has
	 * been initialized, just in case it needs to be quickly restored.
	 */
	pci_set_drvdata(pdev, netdev);
out:
	return rc;

err_phy_disconnect:
	phy_disconnect(adapter->phydev);
err_mdio_unregister:
	mdiobus_unregister(adapter->mii_bus);
err_mdio_free_irq:
	kfree(adapter->mii_bus->irq);
err_mdio_free:
	mdiobus_free(adapter->mii_bus);
err_mem_free:
	et131x_adapter_memory_free(adapter);
err_iounmap:
	iounmap(adapter->regs);
err_free_dev:
	pci_dev_put(pdev);
	free_netdev(netdev);
err_release_res:
	pci_release_regions(pdev);
err_disable:
	pci_disable_device(pdev);
	goto out;
}

static DEFINE_PCI_DEVICE_TABLE(et131x_pci_table) = {
	{ PCI_VDEVICE(ATT, ET131X_PCI_DEVICE_ID_GIG), 0UL},
	{ PCI_VDEVICE(ATT, ET131X_PCI_DEVICE_ID_FAST), 0UL},
	{0,}
};
MODULE_DEVICE_TABLE(pci, et131x_pci_table);

static struct pci_driver et131x_driver = {
	.name		= DRIVER_NAME,
	.id_table	= et131x_pci_table,
	.probe		= et131x_pci_setup,
	.remove		= __devexit_p(et131x_pci_remove),
	.driver.pm	= ET131X_PM_OPS,
};

module_pci_driver(et131x_driver);
