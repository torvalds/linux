/*
 * Copyright (C) 2000, 2005  MIPS Technologies, Inc.  All rights reserved.
 *	Authors: Carsten Langgaard <carstenl@mips.com>
 *		 Maciej W. Rozycki <macro@mips.com>
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * SAA9730 ethernet driver description.
 *
 */
#ifndef _SAA9730_H
#define _SAA9730_H


/* Number of 6-byte entries in the CAM. */
#define LAN_SAA9730_CAM_ENTRIES              10
#define	LAN_SAA9730_CAM_DWORDS               ((LAN_SAA9730_CAM_ENTRIES*6)/4)

/* TX and RX packet size: fixed to 2048 bytes, according to HW requirements. */
#define LAN_SAA9730_PACKET_SIZE                       2048

/* 
 * Number of TX buffers = number of RX buffers = 2, which is fixed according 
 * to HW requirements. 
 */
#define LAN_SAA9730_BUFFERS                           2

/* Number of RX packets per RX buffer. */
#define LAN_SAA9730_RCV_Q_SIZE                        15

/* Number of TX packets per TX buffer. */
#define LAN_SAA9730_TXM_Q_SIZE                        15

/*
 * We get an interrupt for each LAN_SAA9730_DEFAULT_RCV_Q_INT_THRESHOLD 
 * packets received. 
 * If however we receive less than  LAN_SAA9730_DEFAULT_RCV_Q_INT_THRESHOLD
 * packets, the hardware can timeout after a certain time and still tell 
 * us packets have arrived.
 * The timeout value in unit of 32 PCI clocks (33Mhz).
 * The value 200 approximates 0.0002 seconds.
 */
#define LAN_SAA9730_RCV_Q_INT_THRESHOLD               1
#define LAN_SAA9730_DEFAULT_TIME_OUT_CNT              10

#define RXSF_NDIS                       0
#define RXSF_READY                      2
#define RXSF_HWDONE                     3

#define TXSF_EMPTY                      0
#define TXSF_READY                      2
#define TXSF_HWDONE                     3

#define LANEND_LITTLE                   0
#define LANEND_BIG_2143                 1
#define LANEND_BIG_4321                 2

#define LANMB_ANY                       0
#define LANMB_8                         1
#define LANMB_32                        2
#define LANMB_64                        3

#define MACCM_AUTOMATIC                 0
#define MACCM_10MB                      1
#define MACCM_MII                       2

/* 
 * PHY definitions for Basic registers of QS6612 (used on MIPS ATLAS board) 
 */
#define PHY_CONTROL                     0x0
#define PHY_STATUS                      0x1
#define PHY_STATUS_LINK_UP              0x4
#define PHY_CONTROL_RESET               0x8000
#define PHY_CONTROL_AUTO_NEG            0x1000
#define PHY_CONTROL_RESTART_AUTO_NEG    0x0200
#define PHY_ADDRESS                     0x0

/* PK_COUNT register. */
#define PK_COUNT_TX_A_SHF               24
#define PK_COUNT_TX_A_MSK               (0xff << PK_COUNT_TX_A_SHF)
#define PK_COUNT_TX_B_SHF               16
#define PK_COUNT_TX_B_MSK               (0xff << PK_COUNT_TX_B_SHF)
#define PK_COUNT_RX_A_SHF               8
#define PK_COUNT_RX_A_MSK               (0xff << PK_COUNT_RX_A_SHF)
#define PK_COUNT_RX_B_SHF               0
#define PK_COUNT_RX_B_MSK               (0xff << PK_COUNT_RX_B_SHF)

/* OK2USE register. */
#define OK2USE_TX_A                     0x8
#define OK2USE_TX_B                     0x4
#define OK2USE_RX_A                     0x2
#define OK2USE_RX_B                     0x1

/* LAN DMA CONTROL register. */
#define DMA_CTL_BLK_INT                 0x80000000
#define DMA_CTL_MAX_XFER_SHF            18
#define DMA_CTL_MAX_XFER_MSK            (0x3 << LAN_DMA_CTL_MAX_XFER_SHF)
#define DMA_CTL_ENDIAN_SHF              16
#define DMA_CTL_ENDIAN_MSK              (0x3 << LAN_DMA_CTL_ENDIAN_SHF)
#define DMA_CTL_RX_INT_COUNT_SHF        8
#define DMA_CTL_RX_INT_COUNT_MSK        (0xff << LAN_DMA_CTL_RX_INT_COUNT_SHF)
#define DMA_CTL_EN_TX_DMA               0x00000080
#define DMA_CTL_EN_RX_DMA               0x00000040
#define DMA_CTL_RX_INT_BUFFUL_EN        0x00000020
#define DMA_CTL_RX_INT_TO_EN            0x00000010
#define DMA_CTL_RX_INT_EN               0x00000008
#define DMA_CTL_TX_INT_EN               0x00000004
#define DMA_CTL_MAC_TX_INT_EN           0x00000002
#define DMA_CTL_MAC_RX_INT_EN           0x00000001

/* DMA STATUS register. */
#define DMA_STATUS_BAD_ADDR_SHF         16
#define DMA_STATUS_BAD_ADDR_MSK         (0xf << DMA_STATUS_BAD_ADDR_SHF)
#define DMA_STATUS_RX_PKTS_RECEIVED_SHF 8
#define DMA_STATUS_RX_PKTS_RECEIVED_MSK (0xff << DMA_STATUS_RX_PKTS_RECEIVED_SHF)
#define DMA_STATUS_TX_EN_SYNC           0x00000080
#define DMA_STATUS_RX_BUF_A_FUL         0x00000040
#define DMA_STATUS_RX_BUF_B_FUL         0x00000020
#define DMA_STATUS_RX_TO_INT            0x00000010
#define DMA_STATUS_RX_INT               0x00000008
#define DMA_STATUS_TX_INT               0x00000004
#define DMA_STATUS_MAC_TX_INT           0x00000002
#define DMA_STATUS_MAC_RX_INT           0x00000001

/* DMA TEST/PANIC SWITHES register. */
#define DMA_TEST_LOOPBACK               0x01000000
#define DMA_TEST_SW_RESET               0x00000001

/* MAC CONTROL register. */
#define MAC_CONTROL_EN_MISS_ROLL        0x00002000
#define MAC_CONTROL_MISS_ROLL           0x00000400
#define MAC_CONTROL_LOOP10              0x00000080
#define MAC_CONTROL_CONN_SHF            5
#define MAC_CONTROL_CONN_MSK            (0x3 << MAC_CONTROL_CONN_SHF)
#define MAC_CONTROL_MAC_LOOP            0x00000010
#define MAC_CONTROL_FULL_DUP            0x00000008
#define MAC_CONTROL_RESET               0x00000004
#define MAC_CONTROL_HALT_IMM            0x00000002
#define MAC_CONTROL_HALT_REQ            0x00000001

/* CAM CONTROL register. */
#define CAM_CONTROL_COMP_EN             0x00000010
#define CAM_CONTROL_NEG_CAM             0x00000008
#define CAM_CONTROL_BROAD_ACC           0x00000004
#define CAM_CONTROL_GROUP_ACC           0x00000002
#define CAM_CONTROL_STATION_ACC         0x00000001

/* TRANSMIT CONTROL register. */
#define TX_CTL_EN_COMP                  0x00004000
#define TX_CTL_EN_TX_PAR                0x00002000
#define TX_CTL_EN_LATE_COLL             0x00001000
#define TX_CTL_EN_EX_COLL               0x00000800
#define TX_CTL_EN_L_CARR                0x00000400
#define TX_CTL_EN_EX_DEFER              0x00000200
#define TX_CTL_EN_UNDER                 0x00000100
#define TX_CTL_MII10                    0x00000080
#define TX_CTL_SD_PAUSE                 0x00000040
#define TX_CTL_NO_EX_DEF0               0x00000020
#define TX_CTL_F_BACK                   0x00000010
#define TX_CTL_NO_CRC                   0x00000008
#define TX_CTL_NO_PAD                   0x00000004
#define TX_CTL_TX_HALT                  0x00000002
#define TX_CTL_TX_EN                    0x00000001

/* TRANSMIT STATUS register. */
#define TX_STATUS_SQ_ERR                0x00010000
#define TX_STATUS_TX_HALTED             0x00008000
#define TX_STATUS_COMP                  0x00004000
#define TX_STATUS_TX_PAR                0x00002000
#define TX_STATUS_LATE_COLL             0x00001000
#define TX_STATUS_TX10_STAT             0x00000800
#define TX_STATUS_L_CARR                0x00000400
#define TX_STATUS_EX_DEFER              0x00000200
#define TX_STATUS_UNDER                 0x00000100
#define TX_STATUS_IN_TX                 0x00000080
#define TX_STATUS_PAUSED                0x00000040
#define TX_STATUS_TX_DEFERRED           0x00000020
#define TX_STATUS_EX_COLL               0x00000010
#define TX_STATUS_TX_COLL_SHF           0
#define TX_STATUS_TX_COLL_MSK           (0xf << TX_STATUS_TX_COLL_SHF)

/* RECEIVE CONTROL register. */
#define RX_CTL_EN_GOOD                  0x00004000
#define RX_CTL_EN_RX_PAR                0x00002000
#define RX_CTL_EN_LONG_ERR              0x00000800
#define RX_CTL_EN_OVER                  0x00000400
#define RX_CTL_EN_CRC_ERR               0x00000200
#define RX_CTL_EN_ALIGN                 0x00000100
#define RX_CTL_IGNORE_CRC               0x00000040
#define RX_CTL_PASS_CTL                 0x00000020
#define RX_CTL_STRIP_CRC                0x00000010
#define RX_CTL_SHORT_EN                 0x00000008
#define RX_CTL_LONG_EN                  0x00000004
#define RX_CTL_RX_HALT                  0x00000002
#define RX_CTL_RX_EN                    0x00000001

/* RECEIVE STATUS register. */
#define RX_STATUS_RX_HALTED             0x00008000
#define RX_STATUS_GOOD                  0x00004000
#define RX_STATUS_RX_PAR                0x00002000
#define RX_STATUS_LONG_ERR              0x00000800
#define RX_STATUS_OVERFLOW              0x00000400
#define RX_STATUS_CRC_ERR               0x00000200
#define RX_STATUS_ALIGN_ERR             0x00000100
#define RX_STATUS_RX10_STAT             0x00000080
#define RX_STATUS_INT_RX                0x00000040
#define RX_STATUS_CTL_RECD              0x00000020

/* MD_CA register. */
#define MD_CA_PRE_SUP                   0x00001000
#define MD_CA_BUSY                      0x00000800
#define MD_CA_WR                        0x00000400
#define MD_CA_PHY_SHF                   5
#define MD_CA_PHY_MSK                   (0x1f << MD_CA_PHY_SHF)
#define MD_CA_ADDR_SHF                  0
#define MD_CA_ADDR_MSK                  (0x1f << MD_CA_ADDR_SHF)

/* Tx Status/Control. */
#define TX_STAT_CTL_OWNER_SHF           30
#define TX_STAT_CTL_OWNER_MSK           (0x3 << TX_STAT_CTL_OWNER_SHF)
#define TX_STAT_CTL_FRAME_SHF           27
#define TX_STAT_CTL_FRAME_MSK           (0x7 << TX_STAT_CTL_FRAME_SHF)
#define TX_STAT_CTL_STATUS_SHF          11
#define TX_STAT_CTL_STATUS_MSK          (0x1ffff << TX_STAT_CTL_STATUS_SHF)
#define TX_STAT_CTL_LENGTH_SHF          0
#define TX_STAT_CTL_LENGTH_MSK          (0x7ff << TX_STAT_CTL_LENGTH_SHF)

#define TX_STAT_CTL_ERROR_MSK           ((TX_STATUS_SQ_ERR      |     \
					  TX_STATUS_TX_HALTED   |     \
					  TX_STATUS_TX_PAR      |     \
					  TX_STATUS_LATE_COLL   |     \
					  TX_STATUS_L_CARR      |     \
					  TX_STATUS_EX_DEFER    |     \
					  TX_STATUS_UNDER       |     \
					  TX_STATUS_PAUSED      |     \
					  TX_STATUS_TX_DEFERRED |     \
					  TX_STATUS_EX_COLL     |     \
					  TX_STATUS_TX_COLL_MSK)      \
                                                    << TX_STAT_CTL_STATUS_SHF)
#define TX_STAT_CTL_INT_AFTER_TX        0x4

/* Rx Status/Control. */
#define RX_STAT_CTL_OWNER_SHF           30
#define RX_STAT_CTL_OWNER_MSK           (0x3 << RX_STAT_CTL_OWNER_SHF)
#define RX_STAT_CTL_STATUS_SHF          11
#define RX_STAT_CTL_STATUS_MSK          (0xffff << RX_STAT_CTL_STATUS_SHF)
#define RX_STAT_CTL_LENGTH_SHF          0
#define RX_STAT_CTL_LENGTH_MSK          (0x7ff << RX_STAT_CTL_LENGTH_SHF)



/* The SAA9730 (LAN) controller register map, as seen via the PCI-bus. */
#define SAA9730_LAN_REGS_ADDR   0x20400
#define SAA9730_LAN_REGS_SIZE   0x00400

struct lan_saa9730_regmap {
	volatile unsigned int TxBuffA;			/* 0x20400 */
	volatile unsigned int TxBuffB;			/* 0x20404 */
	volatile unsigned int RxBuffA;			/* 0x20408 */
	volatile unsigned int RxBuffB;			/* 0x2040c */
	volatile unsigned int PacketCount;		/* 0x20410 */
	volatile unsigned int Ok2Use;			/* 0x20414 */
	volatile unsigned int LanDmaCtl;		/* 0x20418 */
	volatile unsigned int Timeout;			/* 0x2041c */
	volatile unsigned int DmaStatus;		/* 0x20420 */
	volatile unsigned int DmaTest;			/* 0x20424 */
	volatile unsigned char filler20428[0x20430 - 0x20428];
	volatile unsigned int PauseCount;		/* 0x20430 */
	volatile unsigned int RemotePauseCount;		/* 0x20434 */
	volatile unsigned char filler20438[0x20440 - 0x20438];
	volatile unsigned int MacCtl;			/* 0x20440 */
	volatile unsigned int CamCtl;			/* 0x20444 */
	volatile unsigned int TxCtl;			/* 0x20448 */
	volatile unsigned int TxStatus;			/* 0x2044c */
	volatile unsigned int RxCtl;			/* 0x20450 */
	volatile unsigned int RxStatus;			/* 0x20454 */
	volatile unsigned int StationMgmtData;		/* 0x20458 */
	volatile unsigned int StationMgmtCtl;		/* 0x2045c */
	volatile unsigned int CamAddress;		/* 0x20460 */
	volatile unsigned int CamData;			/* 0x20464 */
	volatile unsigned int CamEnable;		/* 0x20468 */
	volatile unsigned char filler2046c[0x20500 - 0x2046c];
	volatile unsigned int DebugPCIMasterAddr;	/* 0x20500 */
	volatile unsigned int DebugLanTxStateMachine;	/* 0x20504 */
	volatile unsigned int DebugLanRxStateMachine;	/* 0x20508 */
	volatile unsigned int DebugLanTxFifoPointers;	/* 0x2050c */
	volatile unsigned int DebugLanRxFifoPointers;	/* 0x20510 */
	volatile unsigned int DebugLanCtlStateMachine;	/* 0x20514 */
};
typedef volatile struct lan_saa9730_regmap t_lan_saa9730_regmap;


/* EVM interrupt control registers. */
#define EVM_LAN_INT                     0x00010000
#define EVM_MASTER_EN                   0x00000001

/* The SAA9730 (EVM) controller register map, as seen via the PCI-bus. */
#define SAA9730_EVM_REGS_ADDR   0x02000
#define SAA9730_EVM_REGS_SIZE   0x00400

struct evm_saa9730_regmap {
	volatile unsigned int InterruptStatus1;		/* 0x2000 */
	volatile unsigned int InterruptEnable1;		/* 0x2004 */
	volatile unsigned int InterruptMonitor1;	/* 0x2008 */
	volatile unsigned int Counter;			/* 0x200c */
	volatile unsigned int CounterThreshold;		/* 0x2010 */
	volatile unsigned int CounterControl;		/* 0x2014 */
	volatile unsigned int GpioControl1;		/* 0x2018 */
	volatile unsigned int InterruptStatus2;		/* 0x201c */
	volatile unsigned int InterruptEnable2;		/* 0x2020 */
	volatile unsigned int InterruptMonitor2;	/* 0x2024 */
	volatile unsigned int GpioControl2;		/* 0x2028 */
	volatile unsigned int InterruptBlock1;		/* 0x202c */
	volatile unsigned int InterruptBlock2;		/* 0x2030 */
};
typedef volatile struct evm_saa9730_regmap t_evm_saa9730_regmap;


struct lan_saa9730_private {
	/*
	 * Rx/Tx packet buffers.
	 * The Rx and Tx packets must be PACKET_SIZE aligned.
	 */
	void		*buffer_start;
	unsigned int	buffer_size;

	/*
	 * DMA address of beginning of this object, returned
	 * by pci_alloc_consistent().
	 */
	dma_addr_t	dma_addr;

	/* Pointer to the associated pci device structure */
	struct pci_dev	*pci_dev;

	/* Pointer for the SAA9730 LAN controller register set. */
	t_lan_saa9730_regmap *lan_saa9730_regs;

	/* Pointer to the SAA9730 EVM register. */
	t_evm_saa9730_regmap *evm_saa9730_regs;

	/* Rcv buffer Index. */
	unsigned char NextRcvPacketIndex;
	/* Next buffer index. */
	unsigned char NextRcvBufferIndex;

	/* Index of next packet to use in that buffer. */
	unsigned char NextTxmPacketIndex;
	/* Next buffer index. */
	unsigned char NextTxmBufferIndex;

	/* Index of first pending packet ready to send. */
	unsigned char PendingTxmPacketIndex;
	/* Pending buffer index. */
	unsigned char PendingTxmBufferIndex;

	unsigned char DmaRcvPackets;
	unsigned char DmaTxmPackets;

	void	      *TxmBuffer[LAN_SAA9730_BUFFERS][LAN_SAA9730_TXM_Q_SIZE];
	void	      *RcvBuffer[LAN_SAA9730_BUFFERS][LAN_SAA9730_RCV_Q_SIZE];
	unsigned int TxBufferFree[LAN_SAA9730_BUFFERS];

	unsigned char PhysicalAddress[LAN_SAA9730_CAM_ENTRIES][6];

	struct net_device_stats stats;
	spinlock_t lock;
};

#endif /* _SAA9730_H */
