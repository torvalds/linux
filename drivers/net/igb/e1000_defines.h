/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007 - 2008 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _E1000_DEFINES_H_
#define _E1000_DEFINES_H_

/* Number of Transmit and Receive Descriptors must be a multiple of 8 */
#define REQ_TX_DESCRIPTOR_MULTIPLE  8
#define REQ_RX_DESCRIPTOR_MULTIPLE  8

/* Definitions for power management and wakeup registers */
/* Wake Up Control */
#define E1000_WUC_PME_EN     0x00000002 /* PME Enable */

/* Wake Up Filter Control */
#define E1000_WUFC_LNKC 0x00000001 /* Link Status Change Wakeup Enable */
#define E1000_WUFC_MAG  0x00000002 /* Magic Packet Wakeup Enable */
#define E1000_WUFC_EX   0x00000004 /* Directed Exact Wakeup Enable */
#define E1000_WUFC_MC   0x00000008 /* Directed Multicast Wakeup Enable */
#define E1000_WUFC_BC   0x00000010 /* Broadcast Wakeup Enable */
#define E1000_WUFC_ARP  0x00000020 /* ARP Request Packet Wakeup Enable */
#define E1000_WUFC_IPV4 0x00000040 /* Directed IPv4 Packet Wakeup Enable */
#define E1000_WUFC_IPV6 0x00000080 /* Directed IPv6 Packet Wakeup Enable */
#define E1000_WUFC_FLX0 0x00010000 /* Flexible Filter 0 Enable */
#define E1000_WUFC_FLX1 0x00020000 /* Flexible Filter 1 Enable */
#define E1000_WUFC_FLX2 0x00040000 /* Flexible Filter 2 Enable */
#define E1000_WUFC_FLX3 0x00080000 /* Flexible Filter 3 Enable */
#define E1000_WUFC_FLX_FILTERS 0x000F0000 /* Mask for the 4 flexible filters */

/* Wake Up Status */

/* Wake Up Packet Length */

/* Four Flexible Filters are supported */
#define E1000_FLEXIBLE_FILTER_COUNT_MAX 4

/* Each Flexible Filter is at most 128 (0x80) bytes in length */
#define E1000_FLEXIBLE_FILTER_SIZE_MAX  128


/* Extended Device Control */
#define E1000_CTRL_EXT_GPI1_EN   0x00000002 /* Maps SDP5 to GPI1 */
#define E1000_CTRL_EXT_SDP4_DATA 0x00000010 /* Value of SW Defineable Pin 4 */
#define E1000_CTRL_EXT_SDP5_DATA 0x00000020 /* Value of SW Defineable Pin 5 */
#define E1000_CTRL_EXT_SDP7_DATA 0x00000080 /* Value of SW Defineable Pin 7 */
#define E1000_CTRL_EXT_SDP4_DIR  0x00000100 /* Direction of SDP4 0=in 1=out */
#define E1000_CTRL_EXT_EE_RST    0x00002000 /* Reinitialize from EEPROM */
#define E1000_CTRL_EXT_LINK_MODE_MASK 0x00C00000
#define E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES  0x00C00000
#define E1000_CTRL_EXT_LINK_MODE_SGMII   0x00800000
#define E1000_CTRL_EXT_EIAME          0x01000000
#define E1000_CTRL_EXT_IRCA           0x00000001
/* Interrupt delay cancellation */
/* Driver loaded bit for FW */
#define E1000_CTRL_EXT_DRV_LOAD       0x10000000
/* Interrupt acknowledge Auto-mask */
/* Clear Interrupt timers after IMS clear */
/* packet buffer parity error detection enabled */
/* descriptor FIFO parity error detection enable */
#define E1000_CTRL_EXT_PBA_CLR        0x80000000 /* PBA Clear */
#define E1000_I2CCMD_REG_ADDR_SHIFT   16
#define E1000_I2CCMD_PHY_ADDR_SHIFT   24
#define E1000_I2CCMD_OPCODE_READ      0x08000000
#define E1000_I2CCMD_OPCODE_WRITE     0x00000000
#define E1000_I2CCMD_READY            0x20000000
#define E1000_I2CCMD_ERROR            0x80000000
#define E1000_MAX_SGMII_PHY_REG_ADDR  255
#define E1000_I2CCMD_PHY_TIMEOUT      200
#define E1000_IVAR_VALID              0x80
#define E1000_GPIE_NSICR              0x00000001
#define E1000_GPIE_MSIX_MODE          0x00000010
#define E1000_GPIE_EIAME              0x40000000
#define E1000_GPIE_PBA                0x80000000

/* Receive Descriptor bit definitions */
#define E1000_RXD_STAT_DD       0x01    /* Descriptor Done */
#define E1000_RXD_STAT_EOP      0x02    /* End of Packet */
#define E1000_RXD_STAT_IXSM     0x04    /* Ignore checksum */
#define E1000_RXD_STAT_VP       0x08    /* IEEE VLAN Packet */
#define E1000_RXD_STAT_UDPCS    0x10    /* UDP xsum calculated */
#define E1000_RXD_STAT_TCPCS    0x20    /* TCP xsum calculated */
#define E1000_RXD_STAT_DYNINT   0x800   /* Pkt caused INT via DYNINT */
#define E1000_RXD_ERR_CE        0x01    /* CRC Error */
#define E1000_RXD_ERR_SE        0x02    /* Symbol Error */
#define E1000_RXD_ERR_SEQ       0x04    /* Sequence Error */
#define E1000_RXD_ERR_CXE       0x10    /* Carrier Extension Error */
#define E1000_RXD_ERR_RXE       0x80    /* Rx Data Error */
#define E1000_RXD_SPC_VLAN_MASK 0x0FFF  /* VLAN ID is in lower 12 bits */

#define E1000_RXDEXT_STATERR_CE    0x01000000
#define E1000_RXDEXT_STATERR_SE    0x02000000
#define E1000_RXDEXT_STATERR_SEQ   0x04000000
#define E1000_RXDEXT_STATERR_CXE   0x10000000
#define E1000_RXDEXT_STATERR_TCPE  0x20000000
#define E1000_RXDEXT_STATERR_IPE   0x40000000
#define E1000_RXDEXT_STATERR_RXE   0x80000000

/* mask to determine if packets should be dropped due to frame errors */
#define E1000_RXD_ERR_FRAME_ERR_MASK ( \
    E1000_RXD_ERR_CE  |                \
    E1000_RXD_ERR_SE  |                \
    E1000_RXD_ERR_SEQ |                \
    E1000_RXD_ERR_CXE |                \
    E1000_RXD_ERR_RXE)

/* Same mask, but for extended and packet split descriptors */
#define E1000_RXDEXT_ERR_FRAME_ERR_MASK ( \
    E1000_RXDEXT_STATERR_CE  |            \
    E1000_RXDEXT_STATERR_SE  |            \
    E1000_RXDEXT_STATERR_SEQ |            \
    E1000_RXDEXT_STATERR_CXE |            \
    E1000_RXDEXT_STATERR_RXE)

#define E1000_MRQC_RSS_FIELD_IPV4_TCP          0x00010000
#define E1000_MRQC_RSS_FIELD_IPV4              0x00020000
#define E1000_MRQC_RSS_FIELD_IPV6_TCP_EX       0x00040000
#define E1000_MRQC_RSS_FIELD_IPV6              0x00100000
#define E1000_MRQC_RSS_FIELD_IPV6_TCP          0x00200000


/* Management Control */
#define E1000_MANC_SMBUS_EN      0x00000001 /* SMBus Enabled - RO */
#define E1000_MANC_ASF_EN        0x00000002 /* ASF Enabled - RO */
#define E1000_MANC_ARP_EN        0x00002000 /* Enable ARP Request Filtering */
/* Enable Neighbor Discovery Filtering */
#define E1000_MANC_RCV_TCO_EN    0x00020000 /* Receive TCO Packets Enabled */
#define E1000_MANC_BLK_PHY_RST_ON_IDE   0x00040000 /* Block phy resets */
/* Enable MAC address filtering */
#define E1000_MANC_EN_MAC_ADDR_FILTER   0x00100000
/* Enable MNG packets to host memory */
#define E1000_MANC_EN_MNG2HOST   0x00200000
/* Enable IP address filtering */


/* Receive Control */
#define E1000_RCTL_EN             0x00000002    /* enable */
#define E1000_RCTL_SBP            0x00000004    /* store bad packet */
#define E1000_RCTL_UPE            0x00000008    /* unicast promiscuous enable */
#define E1000_RCTL_MPE            0x00000010    /* multicast promiscuous enab */
#define E1000_RCTL_LPE            0x00000020    /* long packet enable */
#define E1000_RCTL_LBM_NO         0x00000000    /* no loopback mode */
#define E1000_RCTL_LBM_MAC        0x00000040    /* MAC loopback mode */
#define E1000_RCTL_LBM_TCVR       0x000000C0    /* tcvr loopback mode */
#define E1000_RCTL_RDMTS_HALF     0x00000000    /* rx desc min threshold size */
#define E1000_RCTL_MO_SHIFT       12            /* multicast offset shift */
#define E1000_RCTL_BAM            0x00008000    /* broadcast enable */
/* these buffer sizes are valid if E1000_RCTL_BSEX is 0 */
#define E1000_RCTL_SZ_2048        0x00000000    /* rx buffer size 2048 */
#define E1000_RCTL_SZ_1024        0x00010000    /* rx buffer size 1024 */
#define E1000_RCTL_SZ_512         0x00020000    /* rx buffer size 512 */
#define E1000_RCTL_SZ_256         0x00030000    /* rx buffer size 256 */
/* these buffer sizes are valid if E1000_RCTL_BSEX is 1 */
#define E1000_RCTL_SZ_16384       0x00010000    /* rx buffer size 16384 */
#define E1000_RCTL_SZ_8192        0x00020000    /* rx buffer size 8192 */
#define E1000_RCTL_SZ_4096        0x00030000    /* rx buffer size 4096 */
#define E1000_RCTL_VFE            0x00040000    /* vlan filter enable */
#define E1000_RCTL_CFIEN          0x00080000    /* canonical form enable */
#define E1000_RCTL_BSEX           0x02000000    /* Buffer size extension */
#define E1000_RCTL_SECRC          0x04000000    /* Strip Ethernet CRC */

/*
 * Use byte values for the following shift parameters
 * Usage:
 *     psrctl |= (((ROUNDUP(value0, 128) >> E1000_PSRCTL_BSIZE0_SHIFT) &
 *                  E1000_PSRCTL_BSIZE0_MASK) |
 *                ((ROUNDUP(value1, 1024) >> E1000_PSRCTL_BSIZE1_SHIFT) &
 *                  E1000_PSRCTL_BSIZE1_MASK) |
 *                ((ROUNDUP(value2, 1024) << E1000_PSRCTL_BSIZE2_SHIFT) &
 *                  E1000_PSRCTL_BSIZE2_MASK) |
 *                ((ROUNDUP(value3, 1024) << E1000_PSRCTL_BSIZE3_SHIFT) |;
 *                  E1000_PSRCTL_BSIZE3_MASK))
 * where value0 = [128..16256],  default=256
 *       value1 = [1024..64512], default=4096
 *       value2 = [0..64512],    default=4096
 *       value3 = [0..64512],    default=0
 */

#define E1000_PSRCTL_BSIZE0_MASK   0x0000007F
#define E1000_PSRCTL_BSIZE1_MASK   0x00003F00
#define E1000_PSRCTL_BSIZE2_MASK   0x003F0000
#define E1000_PSRCTL_BSIZE3_MASK   0x3F000000

#define E1000_PSRCTL_BSIZE0_SHIFT  7            /* Shift _right_ 7 */
#define E1000_PSRCTL_BSIZE1_SHIFT  2            /* Shift _right_ 2 */
#define E1000_PSRCTL_BSIZE2_SHIFT  6            /* Shift _left_ 6 */
#define E1000_PSRCTL_BSIZE3_SHIFT 14            /* Shift _left_ 14 */

/* SWFW_SYNC Definitions */
#define E1000_SWFW_EEP_SM   0x1
#define E1000_SWFW_PHY0_SM  0x2
#define E1000_SWFW_PHY1_SM  0x4

/* FACTPS Definitions */
/* Device Control */
#define E1000_CTRL_FD       0x00000001  /* Full duplex.0=half; 1=full */
#define E1000_CTRL_GIO_MASTER_DISABLE 0x00000004 /*Blocks new Master requests */
#define E1000_CTRL_LRST     0x00000008  /* Link reset. 0=normal,1=reset */
#define E1000_CTRL_ASDE     0x00000020  /* Auto-speed detect enable */
#define E1000_CTRL_SLU      0x00000040  /* Set link up (Force Link) */
#define E1000_CTRL_ILOS     0x00000080  /* Invert Loss-Of Signal */
#define E1000_CTRL_SPD_SEL  0x00000300  /* Speed Select Mask */
#define E1000_CTRL_SPD_100  0x00000100  /* Force 100Mb */
#define E1000_CTRL_SPD_1000 0x00000200  /* Force 1Gb */
#define E1000_CTRL_FRCSPD   0x00000800  /* Force Speed */
#define E1000_CTRL_FRCDPX   0x00001000  /* Force Duplex */
/* Defined polarity of Dock/Undock indication in SDP[0] */
/* Reset both PHY ports, through PHYRST_N pin */
/* enable link status from external LINK_0 and LINK_1 pins */
#define E1000_CTRL_SWDPIN0  0x00040000  /* SWDPIN 0 value */
#define E1000_CTRL_SWDPIN1  0x00080000  /* SWDPIN 1 value */
#define E1000_CTRL_SWDPIN2  0x00100000  /* SWDPIN 2 value */
#define E1000_CTRL_SWDPIN3  0x00200000  /* SWDPIN 3 value */
#define E1000_CTRL_SWDPIO0  0x00400000  /* SWDPIN 0 Input or output */
#define E1000_CTRL_SWDPIO2  0x01000000  /* SWDPIN 2 input or output */
#define E1000_CTRL_SWDPIO3  0x02000000  /* SWDPIN 3 input or output */
#define E1000_CTRL_RST      0x04000000  /* Global reset */
#define E1000_CTRL_RFCE     0x08000000  /* Receive Flow Control enable */
#define E1000_CTRL_TFCE     0x10000000  /* Transmit flow control enable */
#define E1000_CTRL_VME      0x40000000  /* IEEE VLAN mode enable */
#define E1000_CTRL_PHY_RST  0x80000000  /* PHY Reset */
/* Initiate an interrupt to manageability engine */
#define E1000_CTRL_I2C_ENA  0x02000000  /* I2C enable */

/* Bit definitions for the Management Data IO (MDIO) and Management Data
 * Clock (MDC) pins in the Device Control Register.
 */

#define E1000_CONNSW_ENRGSRC             0x4
#define E1000_PCS_CFG_PCS_EN             8
#define E1000_PCS_LCTL_FLV_LINK_UP       1
#define E1000_PCS_LCTL_FSV_100           2
#define E1000_PCS_LCTL_FSV_1000          4
#define E1000_PCS_LCTL_FDV_FULL          8
#define E1000_PCS_LCTL_FSD               0x10
#define E1000_PCS_LCTL_FORCE_LINK        0x20
#define E1000_PCS_LCTL_FORCE_FCTRL       0x80
#define E1000_PCS_LCTL_AN_ENABLE         0x10000
#define E1000_PCS_LCTL_AN_RESTART        0x20000
#define E1000_PCS_LCTL_AN_TIMEOUT        0x40000
#define E1000_ENABLE_SERDES_LOOPBACK     0x0410

#define E1000_PCS_LSTS_LINK_OK           1
#define E1000_PCS_LSTS_SPEED_100         2
#define E1000_PCS_LSTS_SPEED_1000        4
#define E1000_PCS_LSTS_DUPLEX_FULL       8
#define E1000_PCS_LSTS_SYNK_OK           0x10

/* Device Status */
#define E1000_STATUS_FD         0x00000001      /* Full duplex.0=half,1=full */
#define E1000_STATUS_LU         0x00000002      /* Link up.0=no,1=link */
#define E1000_STATUS_FUNC_MASK  0x0000000C      /* PCI Function Mask */
#define E1000_STATUS_FUNC_SHIFT 2
#define E1000_STATUS_FUNC_1     0x00000004      /* Function 1 */
#define E1000_STATUS_TXOFF      0x00000010      /* transmission paused */
#define E1000_STATUS_SPEED_100  0x00000040      /* Speed 100Mb/s */
#define E1000_STATUS_SPEED_1000 0x00000080      /* Speed 1000Mb/s */
/* Change in Dock/Undock state. Clear on write '0'. */
/* Status of Master requests. */
#define E1000_STATUS_GIO_MASTER_ENABLE 0x00080000
/* BMC external code execution disabled */

/* Constants used to intrepret the masked PCI-X bus speed. */

#define SPEED_10    10
#define SPEED_100   100
#define SPEED_1000  1000
#define HALF_DUPLEX 1
#define FULL_DUPLEX 2


#define ADVERTISE_10_HALF                 0x0001
#define ADVERTISE_10_FULL                 0x0002
#define ADVERTISE_100_HALF                0x0004
#define ADVERTISE_100_FULL                0x0008
#define ADVERTISE_1000_HALF               0x0010 /* Not used, just FYI */
#define ADVERTISE_1000_FULL               0x0020

/* 1000/H is not supported, nor spec-compliant. */
#define E1000_ALL_SPEED_DUPLEX (ADVERTISE_10_HALF  |  ADVERTISE_10_FULL | \
				ADVERTISE_100_HALF |  ADVERTISE_100_FULL | \
						      ADVERTISE_1000_FULL)
#define E1000_ALL_NOT_GIG      (ADVERTISE_10_HALF  |  ADVERTISE_10_FULL | \
				ADVERTISE_100_HALF |  ADVERTISE_100_FULL)
#define E1000_ALL_100_SPEED    (ADVERTISE_100_HALF |  ADVERTISE_100_FULL)
#define E1000_ALL_10_SPEED     (ADVERTISE_10_HALF  |  ADVERTISE_10_FULL)
#define E1000_ALL_FULL_DUPLEX  (ADVERTISE_10_FULL  |  ADVERTISE_100_FULL | \
						      ADVERTISE_1000_FULL)
#define E1000_ALL_HALF_DUPLEX  (ADVERTISE_10_HALF  |  ADVERTISE_100_HALF)

#define AUTONEG_ADVERTISE_SPEED_DEFAULT   E1000_ALL_SPEED_DUPLEX

/* LED Control */
#define E1000_LEDCTL_LED0_MODE_MASK       0x0000000F
#define E1000_LEDCTL_LED0_MODE_SHIFT      0
#define E1000_LEDCTL_LED0_IVRT            0x00000040
#define E1000_LEDCTL_LED0_BLINK           0x00000080

#define E1000_LEDCTL_MODE_LED_ON        0xE
#define E1000_LEDCTL_MODE_LED_OFF       0xF

/* Transmit Descriptor bit definitions */
#define E1000_TXD_POPTS_IXSM 0x01       /* Insert IP checksum */
#define E1000_TXD_POPTS_TXSM 0x02       /* Insert TCP/UDP checksum */
#define E1000_TXD_CMD_EOP    0x01000000 /* End of Packet */
#define E1000_TXD_CMD_IFCS   0x02000000 /* Insert FCS (Ethernet CRC) */
#define E1000_TXD_CMD_RS     0x08000000 /* Report Status */
#define E1000_TXD_CMD_DEXT   0x20000000 /* Descriptor extension (0 = legacy) */
/* Extended desc bits for Linksec and timesync */

/* Transmit Control */
#define E1000_TCTL_EN     0x00000002    /* enable tx */
#define E1000_TCTL_PSP    0x00000008    /* pad short packets */
#define E1000_TCTL_CT     0x00000ff0    /* collision threshold */
#define E1000_TCTL_COLD   0x003ff000    /* collision distance */
#define E1000_TCTL_RTLC   0x01000000    /* Re-transmit on late collision */

/* Transmit Arbitration Count */

/* SerDes Control */
#define E1000_SCTL_DISABLE_SERDES_LOOPBACK 0x0400

/* Receive Checksum Control */
#define E1000_RXCSUM_TUOFL     0x00000200   /* TCP / UDP checksum offload */
#define E1000_RXCSUM_IPPCSE    0x00001000   /* IP payload checksum enable */
#define E1000_RXCSUM_PCSD      0x00002000   /* packet checksum disabled */

/* Header split receive */
#define E1000_RFCTL_LEF        0x00040000

/* Collision related configuration parameters */
#define E1000_COLLISION_THRESHOLD       15
#define E1000_CT_SHIFT                  4
#define E1000_COLLISION_DISTANCE        63
#define E1000_COLD_SHIFT                12

/* Ethertype field values */
#define ETHERNET_IEEE_VLAN_TYPE 0x8100  /* 802.3ac packet */

#define MAX_JUMBO_FRAME_SIZE    0x3F00

/* Extended Configuration Control and Size */
#define E1000_PHY_CTRL_GBE_DISABLE        0x00000040

/* PBA constants */
#define E1000_PBA_16K 0x0010    /* 16KB, default TX allocation */
#define E1000_PBA_24K 0x0018
#define E1000_PBA_34K 0x0022
#define E1000_PBA_64K 0x0040    /* 64KB */

#define IFS_MAX       80
#define IFS_MIN       40
#define IFS_RATIO     4
#define IFS_STEP      10
#define MIN_NUM_XMITS 1000

/* SW Semaphore Register */
#define E1000_SWSM_SMBI         0x00000001 /* Driver Semaphore bit */
#define E1000_SWSM_SWESMBI      0x00000002 /* FW Semaphore bit */

/* Interrupt Cause Read */
#define E1000_ICR_TXDW          0x00000001 /* Transmit desc written back */
#define E1000_ICR_TXQE          0x00000002 /* Transmit Queue empty */
#define E1000_ICR_LSC           0x00000004 /* Link Status Change */
#define E1000_ICR_RXSEQ         0x00000008 /* rx sequence error */
#define E1000_ICR_RXDMT0        0x00000010 /* rx desc min. threshold (0) */
#define E1000_ICR_RXO           0x00000040 /* rx overrun */
#define E1000_ICR_RXT0          0x00000080 /* rx timer intr (ring 0) */
#define E1000_ICR_MDAC          0x00000200 /* MDIO access complete */
#define E1000_ICR_RXCFG         0x00000400 /* Rx /c/ ordered set */
#define E1000_ICR_GPI_EN0       0x00000800 /* GP Int 0 */
#define E1000_ICR_GPI_EN1       0x00001000 /* GP Int 1 */
#define E1000_ICR_GPI_EN2       0x00002000 /* GP Int 2 */
#define E1000_ICR_GPI_EN3       0x00004000 /* GP Int 3 */
#define E1000_ICR_TXD_LOW       0x00008000
#define E1000_ICR_SRPD          0x00010000
#define E1000_ICR_ACK           0x00020000 /* Receive Ack frame */
#define E1000_ICR_MNG           0x00040000 /* Manageability event */
#define E1000_ICR_DOCK          0x00080000 /* Dock/Undock */
/* If this bit asserted, the driver should claim the interrupt */
#define E1000_ICR_INT_ASSERTED  0x80000000
/* queue 0 Rx descriptor FIFO parity error */
#define E1000_ICR_RXD_FIFO_PAR0 0x00100000
/* queue 0 Tx descriptor FIFO parity error */
#define E1000_ICR_TXD_FIFO_PAR0 0x00200000
/* host arb read buffer parity error */
#define E1000_ICR_HOST_ARB_PAR  0x00400000
#define E1000_ICR_PB_PAR        0x00800000 /* packet buffer parity error */
/* queue 1 Rx descriptor FIFO parity error */
#define E1000_ICR_RXD_FIFO_PAR1 0x01000000
/* queue 1 Tx descriptor FIFO parity error */
#define E1000_ICR_TXD_FIFO_PAR1 0x02000000
/* FW changed the status of DISSW bit in the FWSM */
#define E1000_ICR_DSW           0x00000020
/* LAN connected device generates an interrupt */
#define E1000_ICR_PHYINT        0x00001000
#define E1000_ICR_EPRST         0x00100000 /* ME handware reset occurs */

/* Extended Interrupt Cause Read */
#define E1000_EICR_RX_QUEUE0    0x00000001 /* Rx Queue 0 Interrupt */
#define E1000_EICR_RX_QUEUE1    0x00000002 /* Rx Queue 1 Interrupt */
#define E1000_EICR_RX_QUEUE2    0x00000004 /* Rx Queue 2 Interrupt */
#define E1000_EICR_RX_QUEUE3    0x00000008 /* Rx Queue 3 Interrupt */
#define E1000_EICR_TX_QUEUE0    0x00000100 /* Tx Queue 0 Interrupt */
#define E1000_EICR_TX_QUEUE1    0x00000200 /* Tx Queue 1 Interrupt */
#define E1000_EICR_TX_QUEUE2    0x00000400 /* Tx Queue 2 Interrupt */
#define E1000_EICR_TX_QUEUE3    0x00000800 /* Tx Queue 3 Interrupt */
#define E1000_EICR_TCP_TIMER    0x40000000 /* TCP Timer */
#define E1000_EICR_OTHER        0x80000000 /* Interrupt Cause Active */
/* TCP Timer */

/*
 * This defines the bits that are set in the Interrupt Mask
 * Set/Read Register.  Each bit is documented below:
 *   o RXT0   = Receiver Timer Interrupt (ring 0)
 *   o TXDW   = Transmit Descriptor Written Back
 *   o RXDMT0 = Receive Descriptor Minimum Threshold hit (ring 0)
 *   o RXSEQ  = Receive Sequence Error
 *   o LSC    = Link Status Change
 */
#define IMS_ENABLE_MASK ( \
    E1000_IMS_RXT0   |    \
    E1000_IMS_TXDW   |    \
    E1000_IMS_RXDMT0 |    \
    E1000_IMS_RXSEQ  |    \
    E1000_IMS_LSC)

/* Interrupt Mask Set */
#define E1000_IMS_TXDW      E1000_ICR_TXDW      /* Transmit desc written back */
#define E1000_IMS_LSC       E1000_ICR_LSC       /* Link Status Change */
#define E1000_IMS_RXSEQ     E1000_ICR_RXSEQ     /* rx sequence error */
#define E1000_IMS_RXDMT0    E1000_ICR_RXDMT0    /* rx desc min. threshold */
#define E1000_IMS_RXT0      E1000_ICR_RXT0      /* rx timer intr */

/* Extended Interrupt Mask Set */
#define E1000_EIMS_TCP_TIMER    E1000_EICR_TCP_TIMER /* TCP Timer */
#define E1000_EIMS_OTHER        E1000_EICR_OTHER   /* Interrupt Cause Active */

/* Interrupt Cause Set */
#define E1000_ICS_LSC       E1000_ICR_LSC       /* Link Status Change */
#define E1000_ICS_RXDMT0    E1000_ICR_RXDMT0    /* rx desc min. threshold */

/* Extended Interrupt Cause Set */

/* Transmit Descriptor Control */
/* Enable the counting of descriptors still to be processed. */

/* Flow Control Constants */
#define FLOW_CONTROL_ADDRESS_LOW  0x00C28001
#define FLOW_CONTROL_ADDRESS_HIGH 0x00000100
#define FLOW_CONTROL_TYPE         0x8808

/* 802.1q VLAN Packet Size */
#define VLAN_TAG_SIZE              4    /* 802.3ac tag (not DMA'd) */
#define E1000_VLAN_FILTER_TBL_SIZE 128  /* VLAN Filter Table (4096 bits) */

/* Receive Address */
/*
 * Number of high/low register pairs in the RAR. The RAR (Receive Address
 * Registers) holds the directed and multicast addresses that we monitor.
 * Technically, we have 16 spots.  However, we reserve one of these spots
 * (RAR[15]) for our directed address used by controllers with
 * manageability enabled, allowing us room for 15 multicast addresses.
 */
#define E1000_RAH_AV  0x80000000        /* Receive descriptor valid */

/* Error Codes */
#define E1000_ERR_NVM      1
#define E1000_ERR_PHY      2
#define E1000_ERR_CONFIG   3
#define E1000_ERR_PARAM    4
#define E1000_ERR_MAC_INIT 5
#define E1000_ERR_RESET   9
#define E1000_ERR_MASTER_REQUESTS_PENDING 10
#define E1000_ERR_HOST_INTERFACE_COMMAND 11
#define E1000_BLK_PHY_RESET   12
#define E1000_ERR_SWFW_SYNC 13
#define E1000_NOT_IMPLEMENTED 14

/* Loop limit on how long we wait for auto-negotiation to complete */
#define COPPER_LINK_UP_LIMIT              10
#define PHY_AUTO_NEG_LIMIT                45
#define PHY_FORCE_LIMIT                   20
/* Number of 100 microseconds we wait for PCI Express master disable */
#define MASTER_DISABLE_TIMEOUT      800
/* Number of milliseconds we wait for PHY configuration done after MAC reset */
#define PHY_CFG_TIMEOUT             100
/* Number of 2 milliseconds we wait for acquiring MDIO ownership. */
/* Number of milliseconds for NVM auto read done after MAC reset. */
#define AUTO_READ_DONE_TIMEOUT      10

/* Flow Control */
#define E1000_FCRTL_XONE 0x80000000     /* Enable XON frame transmission */

/* Transmit Configuration Word */
#define E1000_TXCW_ANE        0x80000000        /* Auto-neg enable */

/* Receive Configuration Word */

/* PCI Express Control */
#define E1000_GCR_RXD_NO_SNOOP          0x00000001
#define E1000_GCR_RXDSCW_NO_SNOOP       0x00000002
#define E1000_GCR_RXDSCR_NO_SNOOP       0x00000004
#define E1000_GCR_TXD_NO_SNOOP          0x00000008
#define E1000_GCR_TXDSCW_NO_SNOOP       0x00000010
#define E1000_GCR_TXDSCR_NO_SNOOP       0x00000020

#define PCIE_NO_SNOOP_ALL (E1000_GCR_RXD_NO_SNOOP         | \
			   E1000_GCR_RXDSCW_NO_SNOOP      | \
			   E1000_GCR_RXDSCR_NO_SNOOP      | \
			   E1000_GCR_TXD_NO_SNOOP         | \
			   E1000_GCR_TXDSCW_NO_SNOOP      | \
			   E1000_GCR_TXDSCR_NO_SNOOP)

/* PHY Control Register */
#define MII_CR_FULL_DUPLEX      0x0100  /* FDX =1, half duplex =0 */
#define MII_CR_RESTART_AUTO_NEG 0x0200  /* Restart auto negotiation */
#define MII_CR_POWER_DOWN       0x0800  /* Power down */
#define MII_CR_AUTO_NEG_EN      0x1000  /* Auto Neg Enable */
#define MII_CR_LOOPBACK         0x4000  /* 0 = normal, 1 = loopback */
#define MII_CR_RESET            0x8000  /* 0 = normal, 1 = PHY reset */
#define MII_CR_SPEED_1000       0x0040
#define MII_CR_SPEED_100        0x2000
#define MII_CR_SPEED_10         0x0000

/* PHY Status Register */
#define MII_SR_LINK_STATUS       0x0004 /* Link Status 1 = link */
#define MII_SR_AUTONEG_COMPLETE  0x0020 /* Auto Neg Complete */

/* Autoneg Advertisement Register */
#define NWAY_AR_10T_HD_CAPS      0x0020   /* 10T   Half Duplex Capable */
#define NWAY_AR_10T_FD_CAPS      0x0040   /* 10T   Full Duplex Capable */
#define NWAY_AR_100TX_HD_CAPS    0x0080   /* 100TX Half Duplex Capable */
#define NWAY_AR_100TX_FD_CAPS    0x0100   /* 100TX Full Duplex Capable */
#define NWAY_AR_PAUSE            0x0400   /* Pause operation desired */
#define NWAY_AR_ASM_DIR          0x0800   /* Asymmetric Pause Direction bit */

/* Link Partner Ability Register (Base Page) */
#define NWAY_LPAR_PAUSE          0x0400 /* LP Pause operation desired */
#define NWAY_LPAR_ASM_DIR        0x0800 /* LP Asymmetric Pause Direction bit */

/* Autoneg Expansion Register */

/* 1000BASE-T Control Register */
#define CR_1000T_HD_CAPS         0x0100 /* Advertise 1000T HD capability */
#define CR_1000T_FD_CAPS         0x0200 /* Advertise 1000T FD capability  */
#define CR_1000T_MS_VALUE        0x0800 /* 1=Configure PHY as Master */
					/* 0=Configure PHY as Slave */
#define CR_1000T_MS_ENABLE       0x1000 /* 1=Master/Slave manual config value */
					/* 0=Automatic Master/Slave config */

/* 1000BASE-T Status Register */
#define SR_1000T_REMOTE_RX_STATUS 0x1000 /* Remote receiver OK */
#define SR_1000T_LOCAL_RX_STATUS  0x2000 /* Local receiver OK */


/* PHY 1000 MII Register/Bit Definitions */
/* PHY Registers defined by IEEE */
#define PHY_CONTROL      0x00 /* Control Register */
#define PHY_STATUS       0x01 /* Status Register */
#define PHY_ID1          0x02 /* Phy Id Reg (word 1) */
#define PHY_ID2          0x03 /* Phy Id Reg (word 2) */
#define PHY_AUTONEG_ADV  0x04 /* Autoneg Advertisement */
#define PHY_LP_ABILITY   0x05 /* Link Partner Ability (Base Page) */
#define PHY_1000T_CTRL   0x09 /* 1000Base-T Control Reg */
#define PHY_1000T_STATUS 0x0A /* 1000Base-T Status Reg */

/* NVM Control */
#define E1000_EECD_SK        0x00000001 /* NVM Clock */
#define E1000_EECD_CS        0x00000002 /* NVM Chip Select */
#define E1000_EECD_DI        0x00000004 /* NVM Data In */
#define E1000_EECD_DO        0x00000008 /* NVM Data Out */
#define E1000_EECD_REQ       0x00000040 /* NVM Access Request */
#define E1000_EECD_GNT       0x00000080 /* NVM Access Grant */
#define E1000_EECD_PRES      0x00000100 /* NVM Present */
/* NVM Addressing bits based on type 0=small, 1=large */
#define E1000_EECD_ADDR_BITS 0x00000400
#define E1000_NVM_GRANT_ATTEMPTS   1000 /* NVM # attempts to gain grant */
#define E1000_EECD_AUTO_RD          0x00000200  /* NVM Auto Read done */
#define E1000_EECD_SIZE_EX_MASK     0x00007800  /* NVM Size */
#define E1000_EECD_SIZE_EX_SHIFT     11

/* Offset to data in NVM read/write registers */
#define E1000_NVM_RW_REG_DATA   16
#define E1000_NVM_RW_REG_DONE   2    /* Offset to READ/WRITE done bit */
#define E1000_NVM_RW_REG_START  1    /* Start operation */
#define E1000_NVM_RW_ADDR_SHIFT 2    /* Shift to the address bits */
#define E1000_NVM_POLL_READ     0    /* Flag for polling for read complete */

/* NVM Word Offsets */
#define NVM_ID_LED_SETTINGS        0x0004
/* For SERDES output amplitude adjustment. */
#define NVM_INIT_CONTROL2_REG      0x000F
#define NVM_INIT_CONTROL3_PORT_A   0x0024
#define NVM_ALT_MAC_ADDR_PTR       0x0037
#define NVM_CHECKSUM_REG           0x003F

#define E1000_NVM_CFG_DONE_PORT_0  0x40000 /* MNG config cycle done */
#define E1000_NVM_CFG_DONE_PORT_1  0x80000 /* ...for second port */

/* Mask bits for fields in Word 0x0f of the NVM */
#define NVM_WORD0F_PAUSE_MASK       0x3000
#define NVM_WORD0F_ASM_DIR          0x2000

/* Mask bits for fields in Word 0x1a of the NVM */

/* For checksumming, the sum of all words in the NVM should equal 0xBABA. */
#define NVM_SUM                    0xBABA

#define NVM_PBA_OFFSET_0           8
#define NVM_PBA_OFFSET_1           9
#define NVM_WORD_SIZE_BASE_SHIFT   6

/* NVM Commands - Microwire */

/* NVM Commands - SPI */
#define NVM_MAX_RETRY_SPI          5000 /* Max wait of 5ms, for RDY signal */
#define NVM_WRITE_OPCODE_SPI       0x02 /* NVM write opcode */
#define NVM_A8_OPCODE_SPI          0x08 /* opcode bit-3 = address bit-8 */
#define NVM_WREN_OPCODE_SPI        0x06 /* NVM set Write Enable latch */
#define NVM_RDSR_OPCODE_SPI        0x05 /* NVM read Status register */

/* SPI NVM Status Register */
#define NVM_STATUS_RDY_SPI         0x01

/* Word definitions for ID LED Settings */
#define ID_LED_RESERVED_0000 0x0000
#define ID_LED_RESERVED_FFFF 0xFFFF
#define ID_LED_DEFAULT       ((ID_LED_OFF1_ON2  << 12) | \
			      (ID_LED_OFF1_OFF2 <<  8) | \
			      (ID_LED_DEF1_DEF2 <<  4) | \
			      (ID_LED_DEF1_DEF2))
#define ID_LED_DEF1_DEF2     0x1
#define ID_LED_DEF1_ON2      0x2
#define ID_LED_DEF1_OFF2     0x3
#define ID_LED_ON1_DEF2      0x4
#define ID_LED_ON1_ON2       0x5
#define ID_LED_ON1_OFF2      0x6
#define ID_LED_OFF1_DEF2     0x7
#define ID_LED_OFF1_ON2      0x8
#define ID_LED_OFF1_OFF2     0x9

#define IGP_ACTIVITY_LED_MASK   0xFFFFF0FF
#define IGP_ACTIVITY_LED_ENABLE 0x0300
#define IGP_LED3_MODE           0x07000000

/* PCI/PCI-X/PCI-EX Config space */
#define PCI_HEADER_TYPE_REGISTER     0x0E
#define PCIE_LINK_STATUS             0x12

#define PCI_HEADER_TYPE_MULTIFUNC    0x80
#define PCIE_LINK_WIDTH_MASK         0x3F0
#define PCIE_LINK_WIDTH_SHIFT        4

#define PHY_REVISION_MASK      0xFFFFFFF0
#define MAX_PHY_REG_ADDRESS    0x1F  /* 5 bit address bus (0-0x1F) */
#define MAX_PHY_MULTI_PAGE_REG 0xF

/* Bit definitions for valid PHY IDs. */
/*
 * I = Integrated
 * E = External
 */
#define M88E1111_I_PHY_ID    0x01410CC0
#define IGP03E1000_E_PHY_ID  0x02A80390
#define M88_VENDOR           0x0141

/* M88E1000 Specific Registers */
#define M88E1000_PHY_SPEC_CTRL     0x10  /* PHY Specific Control Register */
#define M88E1000_PHY_SPEC_STATUS   0x11  /* PHY Specific Status Register */
#define M88E1000_EXT_PHY_SPEC_CTRL 0x14  /* Extended PHY Specific Control */

#define M88E1000_PHY_PAGE_SELECT   0x1D  /* Reg 29 for page number setting */
#define M88E1000_PHY_GEN_CONTROL   0x1E  /* Its meaning depends on reg 29 */

/* M88E1000 PHY Specific Control Register */
#define M88E1000_PSCR_POLARITY_REVERSAL 0x0002 /* 1=Polarity Reversal enabled */
/* 1=CLK125 low, 0=CLK125 toggling */
#define M88E1000_PSCR_MDI_MANUAL_MODE  0x0000  /* MDI Crossover Mode bits 6:5 */
					       /* Manual MDI configuration */
#define M88E1000_PSCR_MDIX_MANUAL_MODE 0x0020  /* Manual MDIX configuration */
/* 1000BASE-T: Auto crossover, 100BASE-TX/10BASE-T: MDI Mode */
#define M88E1000_PSCR_AUTO_X_1000T     0x0040
/* Auto crossover enabled all speeds */
#define M88E1000_PSCR_AUTO_X_MODE      0x0060
/*
 * 1=Enable Extended 10BASE-T distance (Lower 10BASE-T Rx Threshold
 * 0=Normal 10BASE-T Rx Threshold
 */
/* 1=5-bit interface in 100BASE-TX, 0=MII interface in 100BASE-TX */
#define M88E1000_PSCR_ASSERT_CRS_ON_TX     0x0800 /* 1=Assert CRS on Transmit */

/* M88E1000 PHY Specific Status Register */
#define M88E1000_PSSR_REV_POLARITY       0x0002 /* 1=Polarity reversed */
#define M88E1000_PSSR_DOWNSHIFT          0x0020 /* 1=Downshifted */
#define M88E1000_PSSR_MDIX               0x0040 /* 1=MDIX; 0=MDI */
/*
 * 0 = <50M
 * 1 = 50-80M
 * 2 = 80-110M
 * 3 = 110-140M
 * 4 = >140M
 */
#define M88E1000_PSSR_CABLE_LENGTH       0x0380
#define M88E1000_PSSR_SPEED              0xC000 /* Speed, bits 14:15 */
#define M88E1000_PSSR_1000MBS            0x8000 /* 10=1000Mbs */

#define M88E1000_PSSR_CABLE_LENGTH_SHIFT 7

/* M88E1000 Extended PHY Specific Control Register */
/*
 * 1 = Lost lock detect enabled.
 * Will assert lost lock and bring
 * link down if idle not seen
 * within 1ms in 1000BASE-T
 */
/*
 * Number of times we will attempt to autonegotiate before downshifting if we
 * are the master
 */
#define M88E1000_EPSCR_MASTER_DOWNSHIFT_MASK 0x0C00
#define M88E1000_EPSCR_MASTER_DOWNSHIFT_1X   0x0000
/*
 * Number of times we will attempt to autonegotiate before downshifting if we
 * are the slave
 */
#define M88E1000_EPSCR_SLAVE_DOWNSHIFT_MASK  0x0300
#define M88E1000_EPSCR_SLAVE_DOWNSHIFT_1X    0x0100
#define M88E1000_EPSCR_TX_CLK_25      0x0070 /* 25  MHz TX_CLK */

/* M88EC018 Rev 2 specific DownShift settings */
#define M88EC018_EPSCR_DOWNSHIFT_COUNTER_MASK  0x0E00
#define M88EC018_EPSCR_DOWNSHIFT_COUNTER_5X    0x0800

/* MDI Control */
#define E1000_MDIC_REG_SHIFT 16
#define E1000_MDIC_PHY_SHIFT 21
#define E1000_MDIC_OP_WRITE  0x04000000
#define E1000_MDIC_OP_READ   0x08000000
#define E1000_MDIC_READY     0x10000000
#define E1000_MDIC_ERROR     0x40000000

/* SerDes Control */
#define E1000_GEN_CTL_READY             0x80000000
#define E1000_GEN_CTL_ADDRESS_SHIFT     8
#define E1000_GEN_POLL_TIMEOUT          640

#endif
