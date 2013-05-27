/*
 * Altera Triple-Speed Ethernet MAC driver
 *
 * Copyright (C) 2008-2013 Altera Corporation
 *
 * Contributors:
 *   Dalon Westergreen
 *   Thomas Chou
 *   Ian Abbott
 *   Yuriy Kozlov
 *   Tobias Klauser
 *
 * Original driver contributed by SLS.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _ALTERA_TSE_H_
#define _ALTERA_TSE_H_

#include <linux/netdevice.h>
#include <linux/phy.h>

#define __packed_1_	__attribute__((packed, aligned(1)))

/*** Define architecture specific parameters ***/

#define ALT_TSE_TOTAL_SGDMA_DESC_COUNT	128	/* Maximum number of descriptors
						   for TX and RX
						 */
#define ALT_TSE_TX_SGDMA_DESC_COUNT	64	/* Maximum number of descriptors
						   for TX
						 */
#define ALT_TSE_RX_SGDMA_DESC_COUNT	64	/* Maximum number of descriptors
						   for RX
						 */
#define ALT_TSE_TOTAL_SGDMA_DESC_SIZE	(ALT_TSE_TOTAL_SGDMA_DESC_COUNT*0x20)
#define ALT_TX_RING_MOD_MASK		(ALT_TSE_TX_SGDMA_DESC_COUNT - 1)
#define ALT_RX_RING_MOD_MASK		(ALT_TSE_RX_SGDMA_DESC_COUNT - 1)

/************************************************************************/
/*                                                                      */
/* Altera Triple Speed Ethernet MAC IP related definitions              */
/*                                                                      */
/************************************************************************/

#define ENABLE_PHY_LOOPBACK		0	/* set 1 for enable loopback
						 */

/* Assume TSE_MAC_FIFO_WIDTH == 32 from SOPC builder */
#define ALT_TSE_MAC_FIFO_WIDTH			4	/* TX/RX FIFO width in
							   bytes
							 */

#define ALT_TSE_DEFAULT_DUPLEX_MODE		1
#define ALT_TSE_DEFAULT_SPEED			1

#define ALT_TSE_SW_RESET_WATCHDOG_CNTR		10000
#define ALT_TSE_SGDMA_BUSY_WATCHDOG_CNTR	5000000
#define ALT_AUTONEG_TIMEOUT_THRESHOLD		250000
#define ALT_CHECKLINK_TIMEOUT_THRESHOLD		1000000
#define ALT_NOMDIO_TIMEOUT_THRESHOLD		1000000
#define ALT_DISGIGA_TIMEOUT_THRESHOLD		5000000
#define ALT_ALARM_TIMEOUT_THRESHOLD		0xFFFFFFFF

/* Command_Config Register Bit Definitions */

typedef volatile union __alt_tse_command_config {
unsigned int image;
struct {
unsigned int transmit_enable:1,	/* bit 0 enables transmit datapath */
	receive_enable:1,	/* bit 1 enables receive datapath */
	pause_frame_xon_gen:1,	/* bit 2 generates a pause frame with pause
				   quanta 0
				 */
	ethernet_speed:1, /* bit 3 speed control for MAC : 10/100/1000 Mb*/
	promiscuous_enable:1,	/* bit 4 enables mac promiscuous operation */
	pad_enable:1,	/* bit 5 removes pad field from received frame */
	crc_forward:1,	/* bit 6 removes/forwards CRC field from received frame
			   before forwarding it to application
			 */
	pause_frame_forward:1, /* bit 7 terminates or forwards pause frame */
	pause_frame_ignore:1, /* bit 8 ignore pause frame quanta */
	set_mac_address_on_tx:1, /* bit 9 overwrite source MAC address in
				    transmit frame as per setting in MAC core
				  */
	halfduplex_enable:1, /* bit 10 enable half duplex mode */
	excessive_collision:1, /* bit 11 discard frame after detecting
				  collision on 16 consecutive retransmitions
				*/
	late_collision:1, /* bit 12 detects a collision after 64 bytes
			     are transmitted, and discards the frame
			   */
	software_reset:1, /* bit 13 disable the transmit and receive
			     logic, flush the receive FIFO, and reset
			     the statistics counters.
			   */
	multicast_hash_mode_sel:1, /* bit 14 select multicasts
				      address-resolution hash-code mode.
				    */
	loopback_enable:1,	/* bit 15 enables a loopback.*/
	src_mac_addr_sel_on_tx:3, /* bit 18:16 determines which address the
				     MAC function selects to overwrite the
				     source MAC address
				   */
	magic_packet_detect:1, /* bit 19 Enable magic packet detection or
				  Wake-on-LAN
				*/
	sleep_mode_enable:1, /* bit 20 1 puts the MAC function into sleep mode*/
	wake_up_request:1, /* bit 21 indicate node wake up request */
	pause_frame_xoff_gen:1, /* bit 22 generate a pause frame with the
				   pause quanta set to the value configured
				   in the pause_quant register
				 */
	control_frame_enable:1, /* bit 23 enable to accept and forward MAC
				   control frame
				 */
	payload_len_chk_disable:1, /* bit 24 check the actual payload length
				      of received frames against the length
				      field in the received frames
				    */
	enable_10mbps_intf:1,	/* bit 25 enables the 10Mbps interface*/
	rx_error_discard_enable:1, /* bit 26 discards erroneous frames received
				    */
	reserved_bits:4,	/* bit 30:27 reserve   */
	self_clear_counter_reset:1; /* bit 31 clears the statistics counters*/
} __packed_1_ bits;
} __packed_1_ alt_tse_command_config;


#define ALTERA_TSE_CMD_TX_ENA_MSK		(0x00000001)
#define ALTERA_TSE_CMD_RX_ENA_MSK		(0x00000002)
#define ALTERA_TSE_CMD_XON_GEN_MSK		(0x00000004)
#define ALTERA_TSE_CMD_ETH_SPEED_MSK		(0x00000008)
#define ALTERA_TSE_CMD_PROMIS_EN_MSK		(0x00000010)
#define ALTERA_TSE_CMD_PAD_EN_MSK		(0x00000020)
#define ALTERA_TSE_CMD_CRC_FWD_MSK		(0x00000040)
#define ALTERA_TSE_CMD_PAUSE_FWD_MSK		(0x00000080)
#define ALTERA_TSE_CMD_PAUSE_IGNORE_MSK		(0x00000100)
#define ALTERA_TSE_CMD_TX_ADDR_INS_MSK		(0x00000200)
#define ALTERA_TSE_CMD_HD_ENA_MSK		(0x00000400)
#define ALTERA_TSE_CMD_EXCESS_COL_MSK		(0x00000800)
#define ALTERA_TSE_CMD_LATE_COL_MSK		(0x00001000)
#define ALTERA_TSE_CMD_SW_RESET_MSK		(0x00002000)
#define ALTERA_TSE_CMD_MHASH_SEL_MSK		(0x00004000)
#define ALTERA_TSE_CMD_LOOPBACK_MSK		(0x00008000)
/* Bits (18:16) = address select */
#define ALTERA_TSE_CMD_TX_ADDR_SEL_MSK		(0x00070000)
#define ALTERA_TSE_CMD_MAGIC_ENA_MSK		(0x00080000)
#define ALTERA_TSE_CMD_SLEEP_MSK		(0x00100000)
#define ALTERA_TSE_CMD_WAKEUP_MSK		(0x00200000)
#define ALTERA_TSE_CMD_XOFF_GEN_MSK		(0x00400000)
#define ALTERA_TSE_CMD_CNTL_FRM_ENA_MSK		(0x00800000)
#define ALTERA_TSE_CMD_NO_LENGTH_CHECK_MSK	(0x01000000)
#define ALTERA_TSE_CMD_ENA_10_MSK		(0x02000000)
#define ALTERA_TSE_CMD_RX_ERR_DISC_MSK		(0x04000000)
/* Bits (30..27) reserved */
#define ALTERA_TSE_CMD_CNT_RESET_MSK		(0x80000000)

/* Tx_Cmd_Stat Register Bit Definitions */

typedef volatile union __alt_tse_tx_cmd_stat {
	unsigned int image;
	struct {
	unsigned int reserved_lsbs:17,	/* bit 16:0 */
		omit_crc:1,	/* bit 17    enable or disable to calculate and
					     append the CRC to the frame in
					     MAC
				 */
		tx_shift16:1,	/* bit 18    remove the first two bytes from the
					     frame before transmitting it for
					     excepting 32 bits word aligned
					     frames
				 */
		reserved_msbs:13;	/* bit 31:19 */

	} __packed_1_ bits;
} alt_tse_tx_cmd_stat;
#define ALTERA_TSE_TX_CMD_STAT_TX_SHIFT16   (0x00040000)
#define ALTERA_TSE_TX_CMD_STAT_OMIT_CRC     (0x00020000)

/* Rx_Cmd_Stat Register Bit Definitions */

typedef volatile union __alt_tse_rx_cmd_stat {
	unsigned int image;
	struct {
	unsigned int  reserved_lsbs:25,	/* bit 24:0  */
		rx_shift16:1,	/* bit 25   shifts the beginning of the packet
					    to the right by 2 bytes and inserts
					    zeros in the empty bytes to word
					    align the packet
				 */
		reserved_msbs:6;	/* bit 31:26 */
	} __packed_1_ bits;
} alt_tse_rx_cmd_stat;
#define ALTERA_TSE_RX_CMD_STAT_RX_SHIFT16   (0x02000000)

/* MDIO registers within MAC register Space */

struct alt_tse_mdio {
	unsigned int control;	/* PHY device operation control register */
	unsigned int status;	/* PHY device operation status register */
	unsigned int phy_id1;	/* Bits 31:16 of PHY identifier. */
	unsigned int phy_id2;	/* Bits 15:0 of PHY identifier. */
	unsigned int auto_negotiation_advertisement;/* Auto-negotiation
						      advertisement register.
						     */
	unsigned int remote_partner_base_page_ability;

	unsigned int reg6;
	unsigned int reg7;
	unsigned int reg8;
	unsigned int reg9;
	unsigned int rega;
	unsigned int regb;
	unsigned int regc;
	unsigned int regd;
	unsigned int rege;
	unsigned int regf;
	unsigned int reg10;
	unsigned int reg11;
	unsigned int reg12;
	unsigned int reg13;
	unsigned int reg14;
	unsigned int reg15;
	unsigned int reg16;
	unsigned int reg17;
	unsigned int reg18;
	unsigned int reg19;
	unsigned int reg1a;
	unsigned int reg1b;
	unsigned int reg1c;
	unsigned int reg1d;
	unsigned int reg1e;
	unsigned int reg1f;
} ;



/* MAC register Space */

typedef volatile struct {
	/* Bits 15:0: MegaCore function revision (0x0800). Bit 31:16: Customer
	   specific revision
	 */
	unsigned int	megacore_revision;
	/* Provides a memory location for user applications to test the device
	  memory operation.
	 */
	unsigned int	scratch_pad;
	/* The host processor uses this register to control and configure the
	  MAC block.
	 */
	alt_tse_command_config	command_config;
	/* 32-bit primary MAC address word 0 bits 0 to 31 of the primary
	  MAC address.
	 */
	unsigned int	mac_addr_0;
	/* 32-bit primary MAC address word 1 bits 32 to 47 of the primary
	  MAC address.
	 */
	unsigned int	mac_addr_1;
	/* 14-bit maximum frame length. The MAC receive logic */
	unsigned int	max_frame_length;
	/* The pause quanta is used in each pause frame sent to a remote
	   Ethernet device, in increments of 512 Ethernet bit times.
	 */
	unsigned int	pause_quanta;
	/* 12-bit receive FIFO section-empty threshold. */
	unsigned int	rx_sel_empty_threshold;
	/* 12-bit receive FIFO section-full threshold */
	unsigned int	rx_sel_full_threshold;
	/* 12-bit transmit FIFO section-empty threshold. */
	unsigned int	tx_sel_empty_threshold;
	/* 12-bit transmit FIFO section-full threshold. */
	unsigned int	tx_sel_full_threshold;
	/* 12-bit receive FIFO almost-empty threshold */
	unsigned int	rx_almost_empty_threshold;
	/* 12-bit receive FIFO almost-full threshold. */
	unsigned int	rx_almost_full_threshold;
	/* 12-bit transmit FIFO almost-empty threshold */
	unsigned int	tx_almost_empty_threshold;
	/* 12-bit transmit FIFO almost-full threshold */
	unsigned int	tx_almost_full_threshold;
	/* MDIO address of PHY Device 0. Bits 0 to 4 hold a 5-bit PHY address.*/
	unsigned int	mdio_phy0_addr;
	/* MDIO address of PHY Device 1. Bits 0 to 4 hold a 5-bit PHY address.*/
	unsigned int	mdio_phy1_addr;

	/* only if 100/1000 BaseX PCS, reserved otherwise */
	unsigned int	reservedx44[5];
	/* This register is used to check the correct completion of register
	  read access
	 */
	unsigned int	reg_read_access_status;
	/* Minimum IPG between consecutive transmit frame in terms of bytes */
	unsigned int	min_tx_ipg_length;

	/* IEEE 802.3 oEntity Managed Object Support */
	unsigned int	aMACID_1;	/*The MAC addresses*/
	unsigned int	aMACID_2;
	/* Number of frames transmitted without error including pause frames.*/
	unsigned int	aFramesTransmittedOK;
	/* Number of frames received without error including pause frames.*/
	unsigned int	aFramesReceivedOK;
	/* Number of frames received with a CRC error.*/
	unsigned int	aFramesCheckSequenceErrors;
	/* Frame received with an alignment error.*/
	unsigned int	aAlignmentErrors;
	/* Sum of payload and padding octets of frames transmitted without
	  error.
	 */
	unsigned int	aOctetsTransmittedOK;
	/*Sum of payload and padding octets of frames received without error.*/
	unsigned int	aOctetsReceivedOK;

	/* IEEE 802.3 oPausedEntity Managed Object Support */
	unsigned int	aTxPAUSEMACCtrlFrames;	/*Number of transmitted pause
						  frames.
						 */
	unsigned int	aRxPAUSEMACCtrlFrames;	/*Number of Received pause
						  frames.
						 */

	/* IETF MIB (MIB-II) Object Support */
	unsigned int	ifInErrors; /*Number of frames received with error*/
	unsigned int	ifOutErrors; /*Number of frames transmitted with error*/
	unsigned int	ifInUcastPkts; /*Number of valid received unicast
					 frames.
					*/
	unsigned int	ifInMulticastPkts; /*Number of valid received multicasts
					     frames (without pause).
					    */
	unsigned int	ifInBroadcastPkts;	/*Number of valid received
						  broadcast frames.
						 */
	unsigned int	ifOutDiscards;
	unsigned int	ifOutUcastPkts;
	unsigned int	ifOutMulticastPkts;
	unsigned int	ifOutBroadcastPkts;

	/* IETF RMON MIB Object Support */
	unsigned int	etherStatsDropEvent;	/* Counts the number of dropped
						  packets due to internal errors
						  of the MAC client.
						 */
	unsigned int	etherStatsOctets;	/* Total number of bytes
						  received. Good and bad
						  frames.
						 */
	unsigned int	etherStatsPkts;		/* Total number of packets
						  received. Counts good and bad
						  packets.
						 */
	unsigned int	etherStatsUndersizePkts; /* Number of packets received
						    with less than 64 bytes.
						  */
	unsigned int	etherStatsOversizePkts;	/* Number of each well-formed
						   packet that exceeds the valid
						   maximum programmed frame
						   length
						 */
	unsigned int	etherStatsPkts64Octets;	/*Number of received packet with
						  64 bytes
						 */
	/* Frames (good and bad) with 65 to 127 bytes */
	unsigned int	etherStatsPkts65to127Octets;
	/* Frames (good and bad) with 128 to 255 bytes */
	unsigned int	etherStatsPkts128to255Octets;
	/* Frames (good and bad) with 256 to 511 bytes */
	unsigned int	etherStatsPkts256to511Octets;
	/* Frames (good and bad) with 512 to 1023 bytes */
	unsigned int	etherStatsPkts512to1023Octets;
	/* Frames (good and bad) with 1024 to 1518 bytes */
	unsigned int	etherStatsPkts1024to1518Octets;

	/* Any frame length from 1519 to the maximum length configured in the
	   frm_length register, if it is greater than 1518.
	 */
	unsigned int	etherStatsPkts1519toXOctets;
	/* Too long frames with CRC error. */
	unsigned int	etherStatsJabbers;
	/* Too short frames with CRC error. */
	unsigned int	etherStatsFragments;

	unsigned int	reservedxE4;

	/* FIFO control register. */
	alt_tse_tx_cmd_stat	tx_cmd_stat;
	alt_tse_rx_cmd_stat	rx_cmd_stat;

	unsigned int		ipaccTxConf;	/* TX configuration */
	unsigned int		ipaccRxConf;	/* RX configuration */
	unsigned int		ipaccRxStat;	/* IP status */
	unsigned int		ipaccRxStatSum;	/* current frame's IP payload
						   sum result
						 */

	/* Multicast address resolution table, mapped in the controller address
	  space.
	 */
	unsigned int		hash_table[64];

	/* Registers 0 to 31 within PHY device 0/1 connected to the MDIO PHY
	  management interface.
	 */
	struct alt_tse_mdio	mdio_phy0;
	struct alt_tse_mdio	mdio_phy1;

	/*4 Supplemental MAC Addresses*/
	unsigned int		supp_mac_addr_0_0;
	unsigned int		supp_mac_addr_0_1;
	unsigned int		supp_mac_addr_1_0;
	unsigned int		supp_mac_addr_1_1;
	unsigned int		supp_mac_addr_2_0;
	unsigned int		supp_mac_addr_2_1;
	unsigned int		supp_mac_addr_3_0;
	unsigned int		supp_mac_addr_3_1;

	unsigned int		reservedx320[56];
} alt_tse_mac;

/* This structure is private to each device. It is used to pass
 * packets in and out, so there is place for a packet
 */
struct alt_tse_private {
	struct net_device *dev;

/* NAPI struct for NAPI interface */
	struct napi_struct napi;

	alt_tse_mac *mac_dev;

	volatile struct alt_sgdma_registers *rx_sgdma_dev;
	volatile struct alt_sgdma_registers *tx_sgdma_dev;

	void __iomem *desc_mem_base;	/* Descriptor memory base address */

	/* Interrupts */
	unsigned int tx_irq;
	unsigned int rx_irq;

	unsigned char tx_shift_16_ok;
	unsigned char rx_shift_16_ok;
	unsigned char last_tx_shift_16;
	unsigned char last_rx_shift_16;

	/* RX/TX FIFO depths */
	unsigned int tse_tx_depth;
	unsigned int tse_rx_depth;

	/* Location for the SGDMA Descriptors */
	volatile struct alt_sgdma_descriptor *desc;
	volatile struct alt_sgdma_descriptor *sgdma_rx_desc;
	volatile struct alt_sgdma_descriptor *sgdma_tx_desc;

	unsigned int current_mtu;

	volatile unsigned int rx_sgdma_imask;
	volatile unsigned int tx_sgdma_imask;

	unsigned int rx_sgdma_descriptor_tail;
	unsigned int rx_sgdma_descriptor_head;

	unsigned int tx_sgdma_descriptor_tail;
	unsigned int tx_sgdma_descriptor_head;

	struct sk_buff *rx_skb[ALT_TSE_RX_SGDMA_DESC_COUNT];
	struct sk_buff *tx_skb[ALT_TSE_TX_SGDMA_DESC_COUNT];

	spinlock_t rx_lock;
	spinlock_t tx_lock;

	/* PHY */
	unsigned int mii_id;
	int phy_addr;		/* PHY's MDIO address, -1 for autodetection */
	phy_interface_t phy_iface;
	struct mii_bus *mdio;
	struct phy_device *phydev;
	int oldspeed;
	int oldduplex;
	int oldlink;

	u32 msg_enable;
};

/************************************************************************/
/*                                                                      */
/* Altera SG-DMA related definitions                                    */
/*                                                                      */
/************************************************************************/



#define ALT_SGDMA_STATUS_ERROR_MSK				(0x00000001)
#define ALT_SGDMA_STATUS_EOP_ENCOUNTERED_MSK			(0x00000002)
#define ALT_SGDMA_STATUS_DESC_COMPLETED_MSK			(0x00000004)
#define ALT_SGDMA_STATUS_CHAIN_COMPLETED_MSK			(0x00000008)
#define ALT_SGDMA_STATUS_BUSY_MSK				(0x00000010)

#define ALT_SGDMA_CONTROL_IE_ERROR_MSK				(0x00000001)
#define ALT_SGDMA_CONTROL_IE_EOP_ENCOUNTERED_MSK		(0x00000002)
#define ALT_SGDMA_CONTROL_IE_DESC_COMPLETED_MSK			(0x00000004)
#define ALT_SGDMA_CONTROL_IE_CHAIN_COMPLETED_MSK		(0x00000008)
#define ALT_SGDMA_CONTROL_IE_GLOBAL_MSK				(0x00000010)
#define ALT_SGDMA_CONTROL_RUN_MSK				(0x00000020)
#define ALT_SGDMA_CONTROL_STOP_DMA_ER_MSK			(0x00000040)
#define ALT_SGDMA_CONTROL_IE_MAX_DESC_PROCESSED_MSK		(0x00000080)
#define ALT_SGDMA_CONTROL_MAX_DESC_PROCESSED_MSK		(0x0000FF00)
#define ALT_SGDMA_CONTROL_SOFTWARERESET_MSK			(0x00010000)
#define ALT_SGDMA_CONTROL_PARK_MSK				(0x00020000)
#define ALT_SGDMA_CONTROL_CLEAR_INTERRUPT_MSK			(0x80000000)

#define ALTERA_TSE_SGDMA_INTR_MASK			\
				(ALT_SGDMA_CONTROL_IE_CHAIN_COMPLETED_MSK \
				| ALT_SGDMA_STATUS_DESC_COMPLETED_MSK	\
				| ALT_SGDMA_CONTROL_IE_GLOBAL_MSK)

/* Buffer Descriptor data structure
 *
 * The SGDMA controller buffer descriptor allocates
 * 64 bits for each address. To support ANSI C, the
 * struct implementing a descriptor places 32-bits
 * of padding directly above each address; each pad must
 * be cleared when initializing a descriptor.
 */
struct alt_sgdma_descriptor {
	/*Specifies the address of data to be read.*/
	unsigned int	*source;
	unsigned int	source_pad;
	/*Specifies the address to which data should be written.*/
	unsigned int	*destination;
	unsigned int	destination_pad;
	/*Specifies the next descriptor in the linked list.*/
	unsigned int	*next;
	unsigned int	next_pad;

	/*Specifies the number of bytes to transfer*/
	unsigned short	bytes_to_transfer;
	unsigned char	read_burst;
	unsigned char	write_burst;

	/* Specifies the number of bytes that are successfully transferred by
	   the DMA hardware
	 */
	unsigned short	actual_bytes_transferred;
	unsigned char	descriptor_status;
	unsigned char	descriptor_control;

} __packed_1_;


/* Descriptor control bit masks & offsets
 *
 * Note: The control byte physically occupies bits [31:24] in memory.
 *       The following bit-offsets are expressed relative to the LSB of
 *       the control register bitfield.
 */
#define ALT_SGDMA_DESCRIPTOR_CONTROL_GENERATE_EOP_MSK		(0x00000001)
#define ALT_SGDMA_DESCRIPTOR_CONTROL_READ_FIXED_ADDRESS_MSK	(0x00000002)
#define ALT_SGDMA_DESCRIPTOR_CONTROL_WRITE_FIXED_ADDRESS_MSK	(0x00000004)
#define ALT_SGDMA_DESCRIPTOR_CONTROL_ATLANTIC_CHANNEL_MSK	(0x00000008)
#define ALT_SGDMA_DESCRIPTOR_CONTROL_OWNED_BY_HW_MSK		(0x00000080)

/* Descriptor status bit masks & offsets
 *
 * Note: The status byte physically occupies bits [23:16] in memory.
 *       The following bit-offsets are expressed relative to the LSB of
 *       the status register bitfield.
 */
#define ALT_SGDMA_DESCRIPTOR_STATUS_E_CRC_MSK			(0x00000001)
#define ALT_SGDMA_DESCRIPTOR_STATUS_E_PARITY_MSK		(0x00000002)
#define ALT_SGDMA_DESCRIPTOR_STATUS_E_OVERFLOW_MSK		(0x00000004)
#define ALT_SGDMA_DESCRIPTOR_STATUS_E_SYNC_MSK			(0x00000008)
#define ALT_SGDMA_DESCRIPTOR_STATUS_E_UEOP_MSK			(0x00000010)
#define ALT_SGDMA_DESCRIPTOR_STATUS_E_MEOP_MSK			(0x00000020)
#define ALT_SGDMA_DESCRIPTOR_STATUS_E_MSOP_MSK			(0x00000040)
#define ALT_SGDMA_DESCRIPTOR_STATUS_TERMINATED_BY_EOP_MSK	(0x00000080)
#define ALT_SGDMA_DESCRIPTOR_STATUS_ERROR_MSK			(0x0000007F)

/* SG-DMA Control/Status Slave registers map */

struct alt_sgdma_registers {
	unsigned int	status;
	unsigned int	status_pad[3];
	unsigned int	control;
	unsigned int	control_pad[3];
	unsigned int	next_descriptor_pointer;
	unsigned int	descriptor_pad[3];
};

/* Function prototypes */

extern void tse_set_ethtool_ops(struct net_device *netdev);

#endif /* _ALTERA_TSE_H_ */
