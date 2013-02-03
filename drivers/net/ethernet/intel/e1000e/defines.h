/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2013 Intel Corporation.

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
  Linux NICS <linux.nics@intel.com>
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
#define E1000_WUC_APME       0x00000001 /* APM Enable */
#define E1000_WUC_PME_EN     0x00000002 /* PME Enable */
#define E1000_WUC_PHY_WAKE   0x00000100 /* if PHY supports wakeup */

/* Wake Up Filter Control */
#define E1000_WUFC_LNKC 0x00000001 /* Link Status Change Wakeup Enable */
#define E1000_WUFC_MAG  0x00000002 /* Magic Packet Wakeup Enable */
#define E1000_WUFC_EX   0x00000004 /* Directed Exact Wakeup Enable */
#define E1000_WUFC_MC   0x00000008 /* Directed Multicast Wakeup Enable */
#define E1000_WUFC_BC   0x00000010 /* Broadcast Wakeup Enable */
#define E1000_WUFC_ARP  0x00000020 /* ARP Request Packet Wakeup Enable */

/* Wake Up Status */
#define E1000_WUS_LNKC         E1000_WUFC_LNKC
#define E1000_WUS_MAG          E1000_WUFC_MAG
#define E1000_WUS_EX           E1000_WUFC_EX
#define E1000_WUS_MC           E1000_WUFC_MC
#define E1000_WUS_BC           E1000_WUFC_BC

/* Extended Device Control */
#define E1000_CTRL_EXT_LPCD  0x00000004     /* LCD Power Cycle Done */
#define E1000_CTRL_EXT_SDP3_DATA 0x00000080 /* Value of SW Definable Pin 3 */
#define E1000_CTRL_EXT_FORCE_SMBUS 0x00000800 /* Force SMBus mode */
#define E1000_CTRL_EXT_EE_RST    0x00002000 /* Reinitialize from EEPROM */
#define E1000_CTRL_EXT_SPD_BYPS  0x00008000 /* Speed Select Bypass */
#define E1000_CTRL_EXT_RO_DIS    0x00020000 /* Relaxed Ordering disable */
#define E1000_CTRL_EXT_DMA_DYN_CLK_EN 0x00080000 /* DMA Dynamic Clock Gating */
#define E1000_CTRL_EXT_LINK_MODE_MASK 0x00C00000
#define E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES  0x00C00000
#define E1000_CTRL_EXT_EIAME          0x01000000
#define E1000_CTRL_EXT_DRV_LOAD       0x10000000 /* Driver loaded bit for FW */
#define E1000_CTRL_EXT_IAME           0x08000000 /* Interrupt acknowledge Auto-mask */
#define E1000_CTRL_EXT_PBA_CLR        0x80000000 /* PBA Clear */
#define E1000_CTRL_EXT_LSECCK         0x00001000
#define E1000_CTRL_EXT_PHYPDEN        0x00100000

/* Receive Descriptor bit definitions */
#define E1000_RXD_STAT_DD       0x01    /* Descriptor Done */
#define E1000_RXD_STAT_EOP      0x02    /* End of Packet */
#define E1000_RXD_STAT_IXSM     0x04    /* Ignore checksum */
#define E1000_RXD_STAT_VP       0x08    /* IEEE VLAN Packet */
#define E1000_RXD_STAT_UDPCS    0x10    /* UDP xsum calculated */
#define E1000_RXD_STAT_TCPCS    0x20    /* TCP xsum calculated */
#define E1000_RXD_ERR_CE        0x01    /* CRC Error */
#define E1000_RXD_ERR_SE        0x02    /* Symbol Error */
#define E1000_RXD_ERR_SEQ       0x04    /* Sequence Error */
#define E1000_RXD_ERR_CXE       0x10    /* Carrier Extension Error */
#define E1000_RXD_ERR_TCPE      0x20    /* TCP/UDP Checksum Error */
#define E1000_RXD_ERR_IPE       0x40    /* IP Checksum Error */
#define E1000_RXD_ERR_RXE       0x80    /* Rx Data Error */
#define E1000_RXD_SPC_VLAN_MASK 0x0FFF  /* VLAN ID is in lower 12 bits */

#define E1000_RXDEXT_STATERR_TST   0x00000100	/* Time Stamp taken */
#define E1000_RXDEXT_STATERR_CE    0x01000000
#define E1000_RXDEXT_STATERR_SE    0x02000000
#define E1000_RXDEXT_STATERR_SEQ   0x04000000
#define E1000_RXDEXT_STATERR_CXE   0x10000000
#define E1000_RXDEXT_STATERR_RXE   0x80000000

/* mask to determine if packets should be dropped due to frame errors */
#define E1000_RXD_ERR_FRAME_ERR_MASK ( \
	E1000_RXD_ERR_CE  |		\
	E1000_RXD_ERR_SE  |		\
	E1000_RXD_ERR_SEQ |		\
	E1000_RXD_ERR_CXE |		\
	E1000_RXD_ERR_RXE)

/* Same mask, but for extended and packet split descriptors */
#define E1000_RXDEXT_ERR_FRAME_ERR_MASK ( \
	E1000_RXDEXT_STATERR_CE  |	\
	E1000_RXDEXT_STATERR_SE  |	\
	E1000_RXDEXT_STATERR_SEQ |	\
	E1000_RXDEXT_STATERR_CXE |	\
	E1000_RXDEXT_STATERR_RXE)

#define E1000_MRQC_RSS_FIELD_MASK              0xFFFF0000
#define E1000_MRQC_RSS_FIELD_IPV4_TCP          0x00010000
#define E1000_MRQC_RSS_FIELD_IPV4              0x00020000
#define E1000_MRQC_RSS_FIELD_IPV6_TCP_EX       0x00040000
#define E1000_MRQC_RSS_FIELD_IPV6              0x00100000
#define E1000_MRQC_RSS_FIELD_IPV6_TCP          0x00200000

#define E1000_RXDPS_HDRSTAT_HDRSP              0x00008000

/* Management Control */
#define E1000_MANC_SMBUS_EN      0x00000001 /* SMBus Enabled - RO */
#define E1000_MANC_ASF_EN        0x00000002 /* ASF Enabled - RO */
#define E1000_MANC_ARP_EN        0x00002000 /* Enable ARP Request Filtering */
#define E1000_MANC_RCV_TCO_EN    0x00020000 /* Receive TCO Packets Enabled */
#define E1000_MANC_BLK_PHY_RST_ON_IDE   0x00040000 /* Block phy resets */
/* Enable MAC address filtering */
#define E1000_MANC_EN_MAC_ADDR_FILTER   0x00100000
/* Enable MNG packets to host memory */
#define E1000_MANC_EN_MNG2HOST   0x00200000

#define E1000_MANC2H_PORT_623    0x00000020 /* Port 0x26f */
#define E1000_MANC2H_PORT_664    0x00000040 /* Port 0x298 */
#define E1000_MDEF_PORT_623      0x00000800 /* Port 0x26f */
#define E1000_MDEF_PORT_664      0x00000400 /* Port 0x298 */

/* Receive Control */
#define E1000_RCTL_EN             0x00000002    /* enable */
#define E1000_RCTL_SBP            0x00000004    /* store bad packet */
#define E1000_RCTL_UPE            0x00000008    /* unicast promiscuous enable */
#define E1000_RCTL_MPE            0x00000010    /* multicast promiscuous enab */
#define E1000_RCTL_LPE            0x00000020    /* long packet enable */
#define E1000_RCTL_LBM_NO         0x00000000    /* no loopback mode */
#define E1000_RCTL_LBM_MAC        0x00000040    /* MAC loopback mode */
#define E1000_RCTL_LBM_TCVR       0x000000C0    /* tcvr loopback mode */
#define E1000_RCTL_DTYP_PS        0x00000400    /* Packet Split descriptor */
#define E1000_RCTL_RDMTS_HALF     0x00000000    /* Rx desc min threshold size */
#define E1000_RCTL_MO_SHIFT       12            /* multicast offset shift */
#define E1000_RCTL_MO_3           0x00003000    /* multicast offset 15:4 */
#define E1000_RCTL_BAM            0x00008000    /* broadcast enable */
/* these buffer sizes are valid if E1000_RCTL_BSEX is 0 */
#define E1000_RCTL_SZ_2048        0x00000000    /* Rx buffer size 2048 */
#define E1000_RCTL_SZ_1024        0x00010000    /* Rx buffer size 1024 */
#define E1000_RCTL_SZ_512         0x00020000    /* Rx buffer size 512 */
#define E1000_RCTL_SZ_256         0x00030000    /* Rx buffer size 256 */
/* these buffer sizes are valid if E1000_RCTL_BSEX is 1 */
#define E1000_RCTL_SZ_16384       0x00010000    /* Rx buffer size 16384 */
#define E1000_RCTL_SZ_8192        0x00020000    /* Rx buffer size 8192 */
#define E1000_RCTL_SZ_4096        0x00030000    /* Rx buffer size 4096 */
#define E1000_RCTL_VFE            0x00040000    /* vlan filter enable */
#define E1000_RCTL_CFIEN          0x00080000    /* canonical form enable */
#define E1000_RCTL_CFI            0x00100000    /* canonical form indicator */
#define E1000_RCTL_DPF            0x00400000    /* Discard Pause Frames */
#define E1000_RCTL_PMCF           0x00800000    /* pass MAC control frames */
#define E1000_RCTL_BSEX           0x02000000    /* Buffer size extension */
#define E1000_RCTL_SECRC          0x04000000    /* Strip Ethernet CRC */

/* Use byte values for the following shift parameters
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
#define E1000_SWFW_CSR_SM   0x8

/* Device Control */
#define E1000_CTRL_FD       0x00000001  /* Full duplex.0=half; 1=full */
#define E1000_CTRL_GIO_MASTER_DISABLE 0x00000004 /*Blocks new Master requests */
#define E1000_CTRL_LRST     0x00000008  /* Link reset. 0=normal,1=reset */
#define E1000_CTRL_ASDE     0x00000020  /* Auto-speed detect enable */
#define E1000_CTRL_SLU      0x00000040  /* Set link up (Force Link) */
#define E1000_CTRL_ILOS     0x00000080  /* Invert Loss-Of Signal */
#define E1000_CTRL_SPD_SEL  0x00000300  /* Speed Select Mask */
#define E1000_CTRL_SPD_10   0x00000000  /* Force 10Mb */
#define E1000_CTRL_SPD_100  0x00000100  /* Force 100Mb */
#define E1000_CTRL_SPD_1000 0x00000200  /* Force 1Gb */
#define E1000_CTRL_FRCSPD   0x00000800  /* Force Speed */
#define E1000_CTRL_FRCDPX   0x00001000  /* Force Duplex */
#define E1000_CTRL_LANPHYPC_OVERRIDE 0x00010000 /* SW control of LANPHYPC */
#define E1000_CTRL_LANPHYPC_VALUE    0x00020000 /* SW value of LANPHYPC */
#define E1000_CTRL_MEHE     0x00080000  /* Memory Error Handling Enable */
#define E1000_CTRL_SWDPIN0  0x00040000  /* SWDPIN 0 value */
#define E1000_CTRL_SWDPIN1  0x00080000  /* SWDPIN 1 value */
#define E1000_CTRL_SWDPIO0  0x00400000  /* SWDPIN 0 Input or output */
#define E1000_CTRL_RST      0x04000000  /* Global reset */
#define E1000_CTRL_RFCE     0x08000000  /* Receive Flow Control enable */
#define E1000_CTRL_TFCE     0x10000000  /* Transmit flow control enable */
#define E1000_CTRL_VME      0x40000000  /* IEEE VLAN mode enable */
#define E1000_CTRL_PHY_RST  0x80000000  /* PHY Reset */

#define E1000_PCS_LCTL_FORCE_FCTRL	0x80

#define E1000_PCS_LSTS_AN_COMPLETE	0x10000

/* Device Status */
#define E1000_STATUS_FD         0x00000001      /* Full duplex.0=half,1=full */
#define E1000_STATUS_LU         0x00000002      /* Link up.0=no,1=link */
#define E1000_STATUS_FUNC_MASK  0x0000000C      /* PCI Function Mask */
#define E1000_STATUS_FUNC_SHIFT 2
#define E1000_STATUS_FUNC_1     0x00000004      /* Function 1 */
#define E1000_STATUS_TXOFF      0x00000010      /* transmission paused */
#define E1000_STATUS_SPEED_10   0x00000000      /* Speed 10Mb/s */
#define E1000_STATUS_SPEED_100  0x00000040      /* Speed 100Mb/s */
#define E1000_STATUS_SPEED_1000 0x00000080      /* Speed 1000Mb/s */
#define E1000_STATUS_LAN_INIT_DONE 0x00000200   /* Lan Init Completion by NVM */
#define E1000_STATUS_PHYRA      0x00000400      /* PHY Reset Asserted */
#define E1000_STATUS_GIO_MASTER_ENABLE 0x00080000 /* Status of Master requests. */

#define HALF_DUPLEX 1
#define FULL_DUPLEX 2


#define ADVERTISE_10_HALF                 0x0001
#define ADVERTISE_10_FULL                 0x0002
#define ADVERTISE_100_HALF                0x0004
#define ADVERTISE_100_FULL                0x0008
#define ADVERTISE_1000_HALF               0x0010 /* Not used, just FYI */
#define ADVERTISE_1000_FULL               0x0020

/* 1000/H is not supported, nor spec-compliant. */
#define E1000_ALL_SPEED_DUPLEX	( \
	ADVERTISE_10_HALF | ADVERTISE_10_FULL | ADVERTISE_100_HALF | \
	ADVERTISE_100_FULL | ADVERTISE_1000_FULL)
#define E1000_ALL_NOT_GIG	( \
	ADVERTISE_10_HALF | ADVERTISE_10_FULL | ADVERTISE_100_HALF | \
	ADVERTISE_100_FULL)
#define E1000_ALL_100_SPEED	(ADVERTISE_100_HALF | ADVERTISE_100_FULL)
#define E1000_ALL_10_SPEED	(ADVERTISE_10_HALF | ADVERTISE_10_FULL)
#define E1000_ALL_HALF_DUPLEX	(ADVERTISE_10_HALF | ADVERTISE_100_HALF)

#define AUTONEG_ADVERTISE_SPEED_DEFAULT   E1000_ALL_SPEED_DUPLEX

/* LED Control */
#define E1000_PHY_LED0_MODE_MASK          0x00000007
#define E1000_PHY_LED0_IVRT               0x00000008
#define E1000_PHY_LED0_MASK               0x0000001F

#define E1000_LEDCTL_LED0_MODE_MASK       0x0000000F
#define E1000_LEDCTL_LED0_MODE_SHIFT      0
#define E1000_LEDCTL_LED0_IVRT            0x00000040
#define E1000_LEDCTL_LED0_BLINK           0x00000080

#define E1000_LEDCTL_MODE_LINK_UP       0x2
#define E1000_LEDCTL_MODE_LED_ON        0xE
#define E1000_LEDCTL_MODE_LED_OFF       0xF

/* Transmit Descriptor bit definitions */
#define E1000_TXD_DTYP_D     0x00100000 /* Data Descriptor */
#define E1000_TXD_POPTS_IXSM 0x01       /* Insert IP checksum */
#define E1000_TXD_POPTS_TXSM 0x02       /* Insert TCP/UDP checksum */
#define E1000_TXD_CMD_EOP    0x01000000 /* End of Packet */
#define E1000_TXD_CMD_IFCS   0x02000000 /* Insert FCS (Ethernet CRC) */
#define E1000_TXD_CMD_IC     0x04000000 /* Insert Checksum */
#define E1000_TXD_CMD_RS     0x08000000 /* Report Status */
#define E1000_TXD_CMD_RPS    0x10000000 /* Report Packet Sent */
#define E1000_TXD_CMD_DEXT   0x20000000 /* Descriptor extension (0 = legacy) */
#define E1000_TXD_CMD_VLE    0x40000000 /* Add VLAN tag */
#define E1000_TXD_CMD_IDE    0x80000000 /* Enable Tidv register */
#define E1000_TXD_STAT_DD    0x00000001 /* Descriptor Done */
#define E1000_TXD_STAT_EC    0x00000002 /* Excess Collisions */
#define E1000_TXD_STAT_LC    0x00000004 /* Late Collisions */
#define E1000_TXD_STAT_TU    0x00000008 /* Transmit underrun */
#define E1000_TXD_CMD_TCP    0x01000000 /* TCP packet */
#define E1000_TXD_CMD_IP     0x02000000 /* IP packet */
#define E1000_TXD_CMD_TSE    0x04000000 /* TCP Seg enable */
#define E1000_TXD_STAT_TC    0x00000004 /* Tx Underrun */
#define E1000_TXD_EXTCMD_TSTAMP	0x00000010 /* IEEE1588 Timestamp packet */

/* Transmit Control */
#define E1000_TCTL_EN     0x00000002    /* enable Tx */
#define E1000_TCTL_PSP    0x00000008    /* pad short packets */
#define E1000_TCTL_CT     0x00000ff0    /* collision threshold */
#define E1000_TCTL_COLD   0x003ff000    /* collision distance */
#define E1000_TCTL_RTLC   0x01000000    /* Re-transmit on late collision */
#define E1000_TCTL_MULR   0x10000000    /* Multiple request support */

/* SerDes Control */
#define E1000_SCTL_DISABLE_SERDES_LOOPBACK 0x0400

/* Receive Checksum Control */
#define E1000_RXCSUM_TUOFL     0x00000200   /* TCP / UDP checksum offload */
#define E1000_RXCSUM_IPPCSE    0x00001000   /* IP payload checksum enable */
#define E1000_RXCSUM_PCSD      0x00002000   /* packet checksum disabled */

/* Header split receive */
#define E1000_RFCTL_NFSW_DIS            0x00000040
#define E1000_RFCTL_NFSR_DIS            0x00000080
#define E1000_RFCTL_ACK_DIS             0x00001000
#define E1000_RFCTL_EXTEN               0x00008000
#define E1000_RFCTL_IPV6_EX_DIS         0x00010000
#define E1000_RFCTL_NEW_IPV6_EXT_DIS    0x00020000

/* Collision related configuration parameters */
#define E1000_COLLISION_THRESHOLD       15
#define E1000_CT_SHIFT                  4
#define E1000_COLLISION_DISTANCE        63
#define E1000_COLD_SHIFT                12

/* Default values for the transmit IPG register */
#define DEFAULT_82543_TIPG_IPGT_COPPER 8

#define E1000_TIPG_IPGT_MASK  0x000003FF

#define DEFAULT_82543_TIPG_IPGR1 8
#define E1000_TIPG_IPGR1_SHIFT  10

#define DEFAULT_82543_TIPG_IPGR2 6
#define DEFAULT_80003ES2LAN_TIPG_IPGR2 7
#define E1000_TIPG_IPGR2_SHIFT  20

#define MAX_JUMBO_FRAME_SIZE    0x3F00

/* Extended Configuration Control and Size */
#define E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP      0x00000020
#define E1000_EXTCNF_CTRL_LCD_WRITE_ENABLE       0x00000001
#define E1000_EXTCNF_CTRL_OEM_WRITE_ENABLE       0x00000008
#define E1000_EXTCNF_CTRL_SWFLAG                 0x00000020
#define E1000_EXTCNF_CTRL_GATE_PHY_CFG           0x00000080
#define E1000_EXTCNF_SIZE_EXT_PCIE_LENGTH_MASK   0x00FF0000
#define E1000_EXTCNF_SIZE_EXT_PCIE_LENGTH_SHIFT          16
#define E1000_EXTCNF_CTRL_EXT_CNF_POINTER_MASK   0x0FFF0000
#define E1000_EXTCNF_CTRL_EXT_CNF_POINTER_SHIFT          16

#define E1000_PHY_CTRL_D0A_LPLU           0x00000002
#define E1000_PHY_CTRL_NOND0A_LPLU        0x00000004
#define E1000_PHY_CTRL_NOND0A_GBE_DISABLE 0x00000008
#define E1000_PHY_CTRL_GBE_DISABLE        0x00000040

#define E1000_KABGTXD_BGSQLBIAS           0x00050000

/* Low Power IDLE Control */
#define E1000_LPIC_LPIET_SHIFT		24	/* Low Power Idle Entry Time */

/* PBA constants */
#define E1000_PBA_8K  0x0008    /* 8KB */
#define E1000_PBA_16K 0x0010    /* 16KB */

#define E1000_PBA_RXA_MASK	0xFFFF

#define E1000_PBS_16K E1000_PBA_16K

/* Uncorrectable/correctable ECC Error counts and enable bits */
#define E1000_PBECCSTS_CORR_ERR_CNT_MASK	0x000000FF
#define E1000_PBECCSTS_UNCORR_ERR_CNT_MASK	0x0000FF00
#define E1000_PBECCSTS_UNCORR_ERR_CNT_SHIFT	8
#define E1000_PBECCSTS_ECC_ENABLE		0x00010000

#define IFS_MAX       80
#define IFS_MIN       40
#define IFS_RATIO     4
#define IFS_STEP      10
#define MIN_NUM_XMITS 1000

/* SW Semaphore Register */
#define E1000_SWSM_SMBI         0x00000001 /* Driver Semaphore bit */
#define E1000_SWSM_SWESMBI      0x00000002 /* FW Semaphore bit */
#define E1000_SWSM_DRV_LOAD     0x00000008 /* Driver Loaded Bit */

#define E1000_SWSM2_LOCK        0x00000002 /* Secondary driver semaphore bit */

/* Interrupt Cause Read */
#define E1000_ICR_TXDW          0x00000001 /* Transmit desc written back */
#define E1000_ICR_LSC           0x00000004 /* Link Status Change */
#define E1000_ICR_RXSEQ         0x00000008 /* Rx sequence error */
#define E1000_ICR_RXDMT0        0x00000010 /* Rx desc min. threshold (0) */
#define E1000_ICR_RXT0          0x00000080 /* Rx timer intr (ring 0) */
#define E1000_ICR_ECCER         0x00400000 /* Uncorrectable ECC Error */
#define E1000_ICR_INT_ASSERTED  0x80000000 /* If this bit asserted, the driver should claim the interrupt */
#define E1000_ICR_RXQ0          0x00100000 /* Rx Queue 0 Interrupt */
#define E1000_ICR_RXQ1          0x00200000 /* Rx Queue 1 Interrupt */
#define E1000_ICR_TXQ0          0x00400000 /* Tx Queue 0 Interrupt */
#define E1000_ICR_TXQ1          0x00800000 /* Tx Queue 1 Interrupt */
#define E1000_ICR_OTHER         0x01000000 /* Other Interrupts */

/* PBA ECC Register */
#define E1000_PBA_ECC_COUNTER_MASK  0xFFF00000 /* ECC counter mask */
#define E1000_PBA_ECC_COUNTER_SHIFT 20         /* ECC counter shift value */
#define E1000_PBA_ECC_CORR_EN       0x00000001 /* ECC correction enable */
#define E1000_PBA_ECC_STAT_CLR      0x00000002 /* Clear ECC error counter */
#define E1000_PBA_ECC_INT_EN        0x00000004 /* Enable ICR bit 5 for ECC */

/* This defines the bits that are set in the Interrupt Mask
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
#define E1000_IMS_RXSEQ     E1000_ICR_RXSEQ     /* Rx sequence error */
#define E1000_IMS_RXDMT0    E1000_ICR_RXDMT0    /* Rx desc min. threshold */
#define E1000_IMS_RXT0      E1000_ICR_RXT0      /* Rx timer intr */
#define E1000_IMS_ECCER     E1000_ICR_ECCER     /* Uncorrectable ECC Error */
#define E1000_IMS_RXQ0      E1000_ICR_RXQ0      /* Rx Queue 0 Interrupt */
#define E1000_IMS_RXQ1      E1000_ICR_RXQ1      /* Rx Queue 1 Interrupt */
#define E1000_IMS_TXQ0      E1000_ICR_TXQ0      /* Tx Queue 0 Interrupt */
#define E1000_IMS_TXQ1      E1000_ICR_TXQ1      /* Tx Queue 1 Interrupt */
#define E1000_IMS_OTHER     E1000_ICR_OTHER     /* Other Interrupts */

/* Interrupt Cause Set */
#define E1000_ICS_LSC       E1000_ICR_LSC       /* Link Status Change */
#define E1000_ICS_RXSEQ     E1000_ICR_RXSEQ     /* Rx sequence error */
#define E1000_ICS_RXDMT0    E1000_ICR_RXDMT0    /* Rx desc min. threshold */

/* Transmit Descriptor Control */
#define E1000_TXDCTL_PTHRESH 0x0000003F /* TXDCTL Prefetch Threshold */
#define E1000_TXDCTL_HTHRESH 0x00003F00 /* TXDCTL Host Threshold */
#define E1000_TXDCTL_WTHRESH 0x003F0000 /* TXDCTL Writeback Threshold */
#define E1000_TXDCTL_GRAN    0x01000000 /* TXDCTL Granularity */
#define E1000_TXDCTL_FULL_TX_DESC_WB 0x01010000 /* GRAN=1, WTHRESH=1 */
#define E1000_TXDCTL_MAX_TX_DESC_PREFETCH 0x0100001F /* GRAN=1, PTHRESH=31 */
/* Enable the counting of desc. still to be processed. */
#define E1000_TXDCTL_COUNT_DESC 0x00400000

/* Flow Control Constants */
#define FLOW_CONTROL_ADDRESS_LOW  0x00C28001
#define FLOW_CONTROL_ADDRESS_HIGH 0x00000100
#define FLOW_CONTROL_TYPE         0x8808

/* 802.1q VLAN Packet Size */
#define E1000_VLAN_FILTER_TBL_SIZE 128  /* VLAN Filter Table (4096 bits) */

/* Receive Address
 * Number of high/low register pairs in the RAR. The RAR (Receive Address
 * Registers) holds the directed and multicast addresses that we monitor.
 * Technically, we have 16 spots.  However, we reserve one of these spots
 * (RAR[15]) for our directed address used by controllers with
 * manageability enabled, allowing us room for 15 multicast addresses.
 */
#define E1000_RAR_ENTRIES     15
#define E1000_RAH_AV  0x80000000        /* Receive descriptor valid */
#define E1000_RAL_MAC_ADDR_LEN 4
#define E1000_RAH_MAC_ADDR_LEN 2

/* Error Codes */
#define E1000_ERR_NVM      1
#define E1000_ERR_PHY      2
#define E1000_ERR_CONFIG   3
#define E1000_ERR_PARAM    4
#define E1000_ERR_MAC_INIT 5
#define E1000_ERR_PHY_TYPE 6
#define E1000_ERR_RESET   9
#define E1000_ERR_MASTER_REQUESTS_PENDING 10
#define E1000_ERR_HOST_INTERFACE_COMMAND 11
#define E1000_BLK_PHY_RESET   12
#define E1000_ERR_SWFW_SYNC 13
#define E1000_NOT_IMPLEMENTED 14
#define E1000_ERR_INVALID_ARGUMENT  16
#define E1000_ERR_NO_SPACE          17
#define E1000_ERR_NVM_PBA_SECTION   18

/* Loop limit on how long we wait for auto-negotiation to complete */
#define FIBER_LINK_UP_LIMIT               50
#define COPPER_LINK_UP_LIMIT              10
#define PHY_AUTO_NEG_LIMIT                45
#define PHY_FORCE_LIMIT                   20
/* Number of 100 microseconds we wait for PCI Express master disable */
#define MASTER_DISABLE_TIMEOUT      800
/* Number of milliseconds we wait for PHY configuration done after MAC reset */
#define PHY_CFG_TIMEOUT             100
/* Number of 2 milliseconds we wait for acquiring MDIO ownership. */
#define MDIO_OWNERSHIP_TIMEOUT      10
/* Number of milliseconds for NVM auto read done after MAC reset. */
#define AUTO_READ_DONE_TIMEOUT      10

/* Flow Control */
#define E1000_FCRTH_RTH  0x0000FFF8     /* Mask Bits[15:3] for RTH */
#define E1000_FCRTL_RTL  0x0000FFF8     /* Mask Bits[15:3] for RTL */
#define E1000_FCRTL_XONE 0x80000000     /* Enable XON frame transmission */

/* Transmit Configuration Word */
#define E1000_TXCW_FD         0x00000020        /* TXCW full duplex */
#define E1000_TXCW_PAUSE      0x00000080        /* TXCW sym pause request */
#define E1000_TXCW_ASM_DIR    0x00000100        /* TXCW astm pause direction */
#define E1000_TXCW_PAUSE_MASK 0x00000180        /* TXCW pause request mask */
#define E1000_TXCW_ANE        0x80000000        /* Auto-neg enable */

/* Receive Configuration Word */
#define E1000_RXCW_CW         0x0000ffff        /* RxConfigWord mask */
#define E1000_RXCW_IV         0x08000000        /* Receive config invalid */
#define E1000_RXCW_C          0x20000000        /* Receive config */
#define E1000_RXCW_SYNCH      0x40000000        /* Receive config synch */

#define E1000_TSYNCTXCTL_VALID		0x00000001 /* Tx timestamp valid */
#define E1000_TSYNCTXCTL_ENABLED	0x00000010 /* enable Tx timestamping */

#define E1000_TSYNCRXCTL_VALID		0x00000001 /* Rx timestamp valid */
#define E1000_TSYNCRXCTL_TYPE_MASK	0x0000000E /* Rx type mask */
#define E1000_TSYNCRXCTL_TYPE_L2_V2	0x00
#define E1000_TSYNCRXCTL_TYPE_L4_V1	0x02
#define E1000_TSYNCRXCTL_TYPE_L2_L4_V2	0x04
#define E1000_TSYNCRXCTL_TYPE_ALL	0x08
#define E1000_TSYNCRXCTL_TYPE_EVENT_V2	0x0A
#define E1000_TSYNCRXCTL_ENABLED	0x00000010 /* enable Rx timestamping */
#define E1000_TSYNCRXCTL_SYSCFI		0x00000020 /* Sys clock frequency */

#define E1000_RXMTRL_PTP_V1_SYNC_MESSAGE	0x00000000
#define E1000_RXMTRL_PTP_V1_DELAY_REQ_MESSAGE	0x00010000

#define E1000_RXMTRL_PTP_V2_SYNC_MESSAGE	0x00000000
#define E1000_RXMTRL_PTP_V2_DELAY_REQ_MESSAGE	0x01000000

#define E1000_TIMINCA_INCPERIOD_SHIFT	24
#define E1000_TIMINCA_INCVALUE_MASK	0x00FFFFFF

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

/* NVM Control */
#define E1000_EECD_SK        0x00000001 /* NVM Clock */
#define E1000_EECD_CS        0x00000002 /* NVM Chip Select */
#define E1000_EECD_DI        0x00000004 /* NVM Data In */
#define E1000_EECD_DO        0x00000008 /* NVM Data Out */
#define E1000_EECD_REQ       0x00000040 /* NVM Access Request */
#define E1000_EECD_GNT       0x00000080 /* NVM Access Grant */
#define E1000_EECD_PRES      0x00000100 /* NVM Present */
#define E1000_EECD_SIZE      0x00000200 /* NVM Size (0=64 word 1=256 word) */
/* NVM Addressing bits based on type (0-small, 1-large) */
#define E1000_EECD_ADDR_BITS 0x00000400
#define E1000_NVM_GRANT_ATTEMPTS   1000 /* NVM # attempts to gain grant */
#define E1000_EECD_AUTO_RD          0x00000200  /* NVM Auto Read done */
#define E1000_EECD_SIZE_EX_MASK     0x00007800  /* NVM Size */
#define E1000_EECD_SIZE_EX_SHIFT     11
#define E1000_EECD_FLUPD     0x00080000 /* Update FLASH */
#define E1000_EECD_AUPDEN    0x00100000 /* Enable Autonomous FLASH update */
#define E1000_EECD_SEC1VAL   0x00400000 /* Sector One Valid */
#define E1000_EECD_SEC1VAL_VALID_MASK (E1000_EECD_AUTO_RD | E1000_EECD_PRES)

#define E1000_NVM_RW_REG_DATA   16   /* Offset to data in NVM read/write registers */
#define E1000_NVM_RW_REG_DONE   2    /* Offset to READ/WRITE done bit */
#define E1000_NVM_RW_REG_START  1    /* Start operation */
#define E1000_NVM_RW_ADDR_SHIFT 2    /* Shift to the address bits */
#define E1000_NVM_POLL_WRITE    1    /* Flag for polling for write complete */
#define E1000_NVM_POLL_READ     0    /* Flag for polling for read complete */
#define E1000_FLASH_UPDATES  2000

/* NVM Word Offsets */
#define NVM_COMPAT                 0x0003
#define NVM_ID_LED_SETTINGS        0x0004
#define NVM_FUTURE_INIT_WORD1      0x0019
#define NVM_COMPAT_VALID_CSUM      0x0001
#define NVM_FUTURE_INIT_WORD1_VALID_CSUM	0x0040

#define NVM_INIT_CONTROL2_REG      0x000F
#define NVM_INIT_CONTROL3_PORT_B   0x0014
#define NVM_INIT_3GIO_3            0x001A
#define NVM_INIT_CONTROL3_PORT_A   0x0024
#define NVM_CFG                    0x0012
#define NVM_ALT_MAC_ADDR_PTR       0x0037
#define NVM_CHECKSUM_REG           0x003F

#define E1000_NVM_INIT_CTRL2_MNGM 0x6000 /* Manageability Operation Mode mask */

#define E1000_NVM_CFG_DONE_PORT_0  0x40000 /* MNG config cycle done */
#define E1000_NVM_CFG_DONE_PORT_1  0x80000 /* ...for second port */

/* Mask bits for fields in Word 0x0f of the NVM */
#define NVM_WORD0F_PAUSE_MASK       0x3000
#define NVM_WORD0F_PAUSE            0x1000
#define NVM_WORD0F_ASM_DIR          0x2000

/* Mask bits for fields in Word 0x1a of the NVM */
#define NVM_WORD1A_ASPM_MASK  0x000C

/* Mask bits for fields in Word 0x03 of the EEPROM */
#define NVM_COMPAT_LOM    0x0800

/* length of string needed to store PBA number */
#define E1000_PBANUM_LENGTH             11

/* For checksumming, the sum of all words in the NVM should equal 0xBABA. */
#define NVM_SUM                    0xBABA

/* PBA (printed board assembly) number words */
#define NVM_PBA_OFFSET_0           8
#define NVM_PBA_OFFSET_1           9
#define NVM_PBA_PTR_GUARD          0xFAFA
#define NVM_WORD_SIZE_BASE_SHIFT   6

/* NVM Commands - SPI */
#define NVM_MAX_RETRY_SPI          5000 /* Max wait of 5ms, for RDY signal */
#define NVM_READ_OPCODE_SPI        0x03 /* NVM read opcode */
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

/* Bit definitions for valid PHY IDs.
 * I = Integrated
 * E = External
 */
#define M88E1000_E_PHY_ID    0x01410C50
#define M88E1000_I_PHY_ID    0x01410C30
#define M88E1011_I_PHY_ID    0x01410C20
#define IGP01E1000_I_PHY_ID  0x02A80380
#define M88E1111_I_PHY_ID    0x01410CC0
#define GG82563_E_PHY_ID     0x01410CA0
#define IGP03E1000_E_PHY_ID  0x02A80390
#define IFE_E_PHY_ID         0x02A80330
#define IFE_PLUS_E_PHY_ID    0x02A80320
#define IFE_C_E_PHY_ID       0x02A80310
#define BME1000_E_PHY_ID     0x01410CB0
#define BME1000_E_PHY_ID_R2  0x01410CB1
#define I82577_E_PHY_ID      0x01540050
#define I82578_E_PHY_ID      0x004DD040
#define I82579_E_PHY_ID      0x01540090
#define I217_E_PHY_ID        0x015400A0

/* M88E1000 Specific Registers */
#define M88E1000_PHY_SPEC_CTRL     0x10  /* PHY Specific Control Register */
#define M88E1000_PHY_SPEC_STATUS   0x11  /* PHY Specific Status Register */
#define M88E1000_EXT_PHY_SPEC_CTRL 0x14  /* Extended PHY Specific Control */

#define M88E1000_PHY_PAGE_SELECT   0x1D  /* Reg 29 for page number setting */
#define M88E1000_PHY_GEN_CONTROL   0x1E  /* Its meaning depends on reg 29 */

/* M88E1000 PHY Specific Control Register */
#define M88E1000_PSCR_POLARITY_REVERSAL 0x0002 /* 1=Polarity Reversal enabled */
#define M88E1000_PSCR_MDI_MANUAL_MODE  0x0000  /* MDI Crossover Mode bits 6:5 */
					       /* Manual MDI configuration */
#define M88E1000_PSCR_MDIX_MANUAL_MODE 0x0020  /* Manual MDIX configuration */
/* 1000BASE-T: Auto crossover, 100BASE-TX/10BASE-T: MDI Mode */
#define M88E1000_PSCR_AUTO_X_1000T     0x0040
/* Auto crossover enabled all speeds */
#define M88E1000_PSCR_AUTO_X_MODE      0x0060
#define M88E1000_PSCR_ASSERT_CRS_ON_TX 0x0800 /* 1=Assert CRS on Transmit */

/* M88E1000 PHY Specific Status Register */
#define M88E1000_PSSR_REV_POLARITY       0x0002 /* 1=Polarity reversed */
#define M88E1000_PSSR_DOWNSHIFT          0x0020 /* 1=Downshifted */
#define M88E1000_PSSR_MDIX               0x0040 /* 1=MDIX; 0=MDI */
/* 0=<50M; 1=50-80M; 2=80-110M; 3=110-140M; 4=>140M */
#define M88E1000_PSSR_CABLE_LENGTH       0x0380
#define M88E1000_PSSR_SPEED              0xC000 /* Speed, bits 14:15 */
#define M88E1000_PSSR_1000MBS            0x8000 /* 10=1000Mbs */

#define M88E1000_PSSR_CABLE_LENGTH_SHIFT 7

/* Number of times we will attempt to autonegotiate before downshifting if we
 * are the master
 */
#define M88E1000_EPSCR_MASTER_DOWNSHIFT_MASK 0x0C00
#define M88E1000_EPSCR_MASTER_DOWNSHIFT_1X   0x0000
/* Number of times we will attempt to autonegotiate before downshifting if we
 * are the slave
 */
#define M88E1000_EPSCR_SLAVE_DOWNSHIFT_MASK  0x0300
#define M88E1000_EPSCR_SLAVE_DOWNSHIFT_1X    0x0100
#define M88E1000_EPSCR_TX_CLK_25      0x0070 /* 25  MHz TX_CLK */

/* M88EC018 Rev 2 specific DownShift settings */
#define M88EC018_EPSCR_DOWNSHIFT_COUNTER_MASK  0x0E00
#define M88EC018_EPSCR_DOWNSHIFT_COUNTER_5X    0x0800

#define I82578_EPSCR_DOWNSHIFT_ENABLE          0x0020
#define I82578_EPSCR_DOWNSHIFT_COUNTER_MASK    0x001C

/* BME1000 PHY Specific Control Register */
#define BME1000_PSCR_ENABLE_DOWNSHIFT   0x0800 /* 1 = enable downshift */

/* PHY Low Power Idle Control */
#define I82579_LPI_CTRL				PHY_REG(772, 20)
#define I82579_LPI_CTRL_100_ENABLE		0x2000
#define I82579_LPI_CTRL_1000_ENABLE		0x4000
#define I82579_LPI_CTRL_ENABLE_MASK		0x6000
#define I82579_LPI_CTRL_FORCE_PLL_LOCK_COUNT	0x80

/* Extended Management Interface (EMI) Registers */
#define I82579_EMI_ADDR		0x10
#define I82579_EMI_DATA		0x11
#define I82579_LPI_UPDATE_TIMER	0x4805	/* in 40ns units + 40 ns base value */
#define I82579_MSE_THRESHOLD	0x084F	/* 82579 Mean Square Error Threshold */
#define I82577_MSE_THRESHOLD	0x0887	/* 82577 Mean Square Error Threshold */
#define I82579_MSE_LINK_DOWN	0x2411	/* MSE count before dropping link */
#define I82579_EEE_PCS_STATUS	0x182D	/* IEEE MMD Register 3.1 >> 8 */
#define I82579_EEE_CAPABILITY	0x0410	/* IEEE MMD Register 3.20 */
#define I82579_EEE_ADVERTISEMENT	0x040E	/* IEEE MMD Register 7.60 */
#define I82579_EEE_LP_ABILITY		0x040F	/* IEEE MMD Register 7.61 */
#define I82579_EEE_100_SUPPORTED	(1 << 1) /* 100BaseTx EEE supported */
#define I82579_EEE_1000_SUPPORTED	(1 << 2) /* 1000BaseTx EEE supported */
#define I217_EEE_PCS_STATUS	0x9401	/* IEEE MMD Register 3.1 */
#define I217_EEE_CAPABILITY	0x8000	/* IEEE MMD Register 3.20 */
#define I217_EEE_ADVERTISEMENT	0x8001	/* IEEE MMD Register 7.60 */
#define I217_EEE_LP_ABILITY	0x8002	/* IEEE MMD Register 7.61 */

#define E1000_EEE_RX_LPI_RCVD	0x0400	/* Tx LP idle received */
#define E1000_EEE_TX_LPI_RCVD	0x0800	/* Rx LP idle received */

#define PHY_PAGE_SHIFT 5
#define PHY_REG(page, reg) (((page) << PHY_PAGE_SHIFT) | \
                           ((reg) & MAX_PHY_REG_ADDRESS))

/* Bits...
 * 15-5: page
 * 4-0: register offset
 */
#define GG82563_PAGE_SHIFT        5
#define GG82563_REG(page, reg)    \
	(((page) << GG82563_PAGE_SHIFT) | ((reg) & MAX_PHY_REG_ADDRESS))
#define GG82563_MIN_ALT_REG       30

/* GG82563 Specific Registers */
#define GG82563_PHY_SPEC_CTRL           \
	GG82563_REG(0, 16) /* PHY Specific Control */
#define GG82563_PHY_PAGE_SELECT         \
	GG82563_REG(0, 22) /* Page Select */
#define GG82563_PHY_SPEC_CTRL_2         \
	GG82563_REG(0, 26) /* PHY Specific Control 2 */
#define GG82563_PHY_PAGE_SELECT_ALT     \
	GG82563_REG(0, 29) /* Alternate Page Select */

#define GG82563_PHY_MAC_SPEC_CTRL       \
	GG82563_REG(2, 21) /* MAC Specific Control Register */

#define GG82563_PHY_DSP_DISTANCE    \
	GG82563_REG(5, 26) /* DSP Distance */

/* Page 193 - Port Control Registers */
#define GG82563_PHY_KMRN_MODE_CTRL   \
	GG82563_REG(193, 16) /* Kumeran Mode Control */
#define GG82563_PHY_PWR_MGMT_CTRL       \
	GG82563_REG(193, 20) /* Power Management Control */

/* Page 194 - KMRN Registers */
#define GG82563_PHY_INBAND_CTRL         \
	GG82563_REG(194, 18) /* Inband Control */

/* MDI Control */
#define E1000_MDIC_REG_SHIFT 16
#define E1000_MDIC_PHY_SHIFT 21
#define E1000_MDIC_OP_WRITE  0x04000000
#define E1000_MDIC_OP_READ   0x08000000
#define E1000_MDIC_READY     0x10000000
#define E1000_MDIC_ERROR     0x40000000

/* SerDes Control */
#define E1000_GEN_POLL_TIMEOUT          640

/* FW Semaphore */
#define E1000_FWSM_WLOCK_MAC_MASK	0x0380
#define E1000_FWSM_WLOCK_MAC_SHIFT	7

#endif /* _E1000_DEFINES_H_ */
