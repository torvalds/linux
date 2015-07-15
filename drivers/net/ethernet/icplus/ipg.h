/*
 * Include file for Gigabit Ethernet device driver for Network
 * Interface Cards (NICs) utilizing the Tamarack Microelectronics
 * Inc. IPG Gigabit or Triple Speed Ethernet Media Access
 * Controller.
 */
#ifndef __LINUX_IPG_H
#define __LINUX_IPG_H

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <asm/bitops.h>

/*
 *	Constants
 */

/* GMII based PHY IDs */
#define		NS				0x2000
#define		MARVELL				0x0141
#define		ICPLUS_PHY			0x243

/* NIC Physical Layer Device MII register fields. */
#define         MII_PHY_SELECTOR_IEEE8023       0x0001
#define         MII_PHY_TECHABILITYFIELD        0x1FE0

/* GMII_PHY_1000 need to set to prefer master */
#define         GMII_PHY_1000BASETCONTROL_PreferMaster 0x0400

/* NIC Physical Layer Device GMII constants. */
#define         GMII_PREAMBLE                    0xFFFFFFFF
#define         GMII_ST                          0x1
#define         GMII_READ                        0x2
#define         GMII_WRITE                       0x1
#define         GMII_TA_READ_MASK                0x1
#define         GMII_TA_WRITE                    0x2

/* I/O register offsets. */
enum ipg_regs {
	DMA_CTRL		= 0x00,
	RX_DMA_STATUS		= 0x08, /* Unused + reserved */
	TFD_LIST_PTR_0		= 0x10,
	TFD_LIST_PTR_1		= 0x14,
	TX_DMA_BURST_THRESH	= 0x18,
	TX_DMA_URGENT_THRESH	= 0x19,
	TX_DMA_POLL_PERIOD	= 0x1a,
	RFD_LIST_PTR_0		= 0x1c,
	RFD_LIST_PTR_1		= 0x20,
	RX_DMA_BURST_THRESH	= 0x24,
	RX_DMA_URGENT_THRESH	= 0x25,
	RX_DMA_POLL_PERIOD	= 0x26,
	DEBUG_CTRL		= 0x2c,
	ASIC_CTRL		= 0x30,
	FIFO_CTRL		= 0x38, /* Unused */
	FLOW_OFF_THRESH		= 0x3c,
	FLOW_ON_THRESH		= 0x3e,
	EEPROM_DATA		= 0x48,
	EEPROM_CTRL		= 0x4a,
	EXPROM_ADDR		= 0x4c, /* Unused */
	EXPROM_DATA		= 0x50, /* Unused */
	WAKE_EVENT		= 0x51, /* Unused */
	COUNTDOWN		= 0x54, /* Unused */
	INT_STATUS_ACK		= 0x5a,
	INT_ENABLE		= 0x5c,
	INT_STATUS		= 0x5e, /* Unused */
	TX_STATUS		= 0x60,
	MAC_CTRL		= 0x6c,
	VLAN_TAG		= 0x70, /* Unused */
	PHY_SET			= 0x75,
	PHY_CTRL		= 0x76,
	STATION_ADDRESS_0	= 0x78,
	STATION_ADDRESS_1	= 0x7a,
	STATION_ADDRESS_2	= 0x7c,
	MAX_FRAME_SIZE		= 0x86,
	RECEIVE_MODE		= 0x88,
	HASHTABLE_0		= 0x8c,
	HASHTABLE_1		= 0x90,
	RMON_STATISTICS_MASK	= 0x98,
	STATISTICS_MASK		= 0x9c,
	RX_JUMBO_FRAMES		= 0xbc, /* Unused */
	TCP_CHECKSUM_ERRORS	= 0xc0, /* Unused */
	IP_CHECKSUM_ERRORS	= 0xc2, /* Unused */
	UDP_CHECKSUM_ERRORS	= 0xc4, /* Unused */
	TX_JUMBO_FRAMES		= 0xf4  /* Unused */
};

/* Ethernet MIB statistic register offsets. */
#define	IPG_OCTETRCVOK			0xA8
#define	IPG_MCSTOCTETRCVDOK		0xAC
#define	IPG_BCSTOCTETRCVOK		0xB0
#define	IPG_FRAMESRCVDOK		0xB4
#define	IPG_MCSTFRAMESRCVDOK		0xB8
#define	IPG_BCSTFRAMESRCVDOK		0xBE
#define	IPG_MACCONTROLFRAMESRCVD	0xC6
#define	IPG_FRAMETOOLONGERRORS		0xC8
#define	IPG_INRANGELENGTHERRORS		0xCA
#define	IPG_FRAMECHECKSEQERRORS		0xCC
#define	IPG_FRAMESLOSTRXERRORS		0xCE
#define	IPG_OCTETXMTOK			0xD0
#define	IPG_MCSTOCTETXMTOK		0xD4
#define	IPG_BCSTOCTETXMTOK		0xD8
#define	IPG_FRAMESXMTDOK		0xDC
#define	IPG_MCSTFRAMESXMTDOK		0xE0
#define	IPG_FRAMESWDEFERREDXMT		0xE4
#define	IPG_LATECOLLISIONS		0xE8
#define	IPG_MULTICOLFRAMES		0xEC
#define	IPG_SINGLECOLFRAMES		0xF0
#define	IPG_BCSTFRAMESXMTDOK		0xF6
#define	IPG_CARRIERSENSEERRORS		0xF8
#define	IPG_MACCONTROLFRAMESXMTDOK	0xFA
#define	IPG_FRAMESABORTXSCOLLS		0xFC
#define	IPG_FRAMESWEXDEFERRAL		0xFE

/* RMON statistic register offsets. */
#define	IPG_ETHERSTATSCOLLISIONS			0x100
#define	IPG_ETHERSTATSOCTETSTRANSMIT			0x104
#define	IPG_ETHERSTATSPKTSTRANSMIT			0x108
#define	IPG_ETHERSTATSPKTS64OCTESTSTRANSMIT		0x10C
#define	IPG_ETHERSTATSPKTS65TO127OCTESTSTRANSMIT	0x110
#define	IPG_ETHERSTATSPKTS128TO255OCTESTSTRANSMIT	0x114
#define	IPG_ETHERSTATSPKTS256TO511OCTESTSTRANSMIT	0x118
#define	IPG_ETHERSTATSPKTS512TO1023OCTESTSTRANSMIT	0x11C
#define	IPG_ETHERSTATSPKTS1024TO1518OCTESTSTRANSMIT	0x120
#define	IPG_ETHERSTATSCRCALIGNERRORS			0x124
#define	IPG_ETHERSTATSUNDERSIZEPKTS			0x128
#define	IPG_ETHERSTATSFRAGMENTS				0x12C
#define	IPG_ETHERSTATSJABBERS				0x130
#define	IPG_ETHERSTATSOCTETS				0x134
#define	IPG_ETHERSTATSPKTS				0x138
#define	IPG_ETHERSTATSPKTS64OCTESTS			0x13C
#define	IPG_ETHERSTATSPKTS65TO127OCTESTS		0x140
#define	IPG_ETHERSTATSPKTS128TO255OCTESTS		0x144
#define	IPG_ETHERSTATSPKTS256TO511OCTESTS		0x148
#define	IPG_ETHERSTATSPKTS512TO1023OCTESTS		0x14C
#define	IPG_ETHERSTATSPKTS1024TO1518OCTESTS		0x150

/* RMON statistic register equivalents. */
#define	IPG_ETHERSTATSMULTICASTPKTSTRANSMIT		0xE0
#define	IPG_ETHERSTATSBROADCASTPKTSTRANSMIT		0xF6
#define	IPG_ETHERSTATSMULTICASTPKTS			0xB8
#define	IPG_ETHERSTATSBROADCASTPKTS			0xBE
#define	IPG_ETHERSTATSOVERSIZEPKTS			0xC8
#define	IPG_ETHERSTATSDROPEVENTS			0xCE

/* Serial EEPROM offsets */
#define	IPG_EEPROM_CONFIGPARAM		0x00
#define	IPG_EEPROM_ASICCTRL		0x01
#define	IPG_EEPROM_SUBSYSTEMVENDORID	0x02
#define	IPG_EEPROM_SUBSYSTEMID		0x03
#define	IPG_EEPROM_STATIONADDRESS0	0x10
#define	IPG_EEPROM_STATIONADDRESS1	0x11
#define	IPG_EEPROM_STATIONADDRESS2	0x12

/* Register & data structure bit masks */

/* PCI register masks. */

/* IOBaseAddress */
#define         IPG_PIB_RSVD_MASK		0xFFFFFE01
#define         IPG_PIB_IOBASEADDRESS		0xFFFFFF00
#define         IPG_PIB_IOBASEADDRIND		0x00000001

/* MemBaseAddress */
#define         IPG_PMB_RSVD_MASK		0xFFFFFE07
#define         IPG_PMB_MEMBASEADDRIND		0x00000001
#define         IPG_PMB_MEMMAPTYPE		0x00000006
#define         IPG_PMB_MEMMAPTYPE0		0x00000002
#define         IPG_PMB_MEMMAPTYPE1		0x00000004
#define         IPG_PMB_MEMBASEADDRESS		0xFFFFFE00

/* ConfigStatus */
#define IPG_CS_RSVD_MASK                0xFFB0
#define IPG_CS_CAPABILITIES             0x0010
#define IPG_CS_66MHZCAPABLE             0x0020
#define IPG_CS_FASTBACK2BACK            0x0080
#define IPG_CS_DATAPARITYREPORTED       0x0100
#define IPG_CS_DEVSELTIMING             0x0600
#define IPG_CS_SIGNALEDTARGETABORT      0x0800
#define IPG_CS_RECEIVEDTARGETABORT      0x1000
#define IPG_CS_RECEIVEDMASTERABORT      0x2000
#define IPG_CS_SIGNALEDSYSTEMERROR      0x4000
#define IPG_CS_DETECTEDPARITYERROR      0x8000

/* TFD data structure masks. */

/* TFDList, TFC */
#define	IPG_TFC_RSVD_MASK			0x0000FFFF9FFFFFFFULL
#define	IPG_TFC_FRAMEID				0x000000000000FFFFULL
#define	IPG_TFC_WORDALIGN			0x0000000000030000ULL
#define	IPG_TFC_WORDALIGNTODWORD		0x0000000000000000ULL
#define	IPG_TFC_WORDALIGNTOWORD			0x0000000000020000ULL
#define	IPG_TFC_WORDALIGNDISABLED		0x0000000000030000ULL
#define	IPG_TFC_TCPCHECKSUMENABLE		0x0000000000040000ULL
#define	IPG_TFC_UDPCHECKSUMENABLE		0x0000000000080000ULL
#define	IPG_TFC_IPCHECKSUMENABLE		0x0000000000100000ULL
#define	IPG_TFC_FCSAPPENDDISABLE		0x0000000000200000ULL
#define	IPG_TFC_TXINDICATE			0x0000000000400000ULL
#define	IPG_TFC_TXDMAINDICATE			0x0000000000800000ULL
#define	IPG_TFC_FRAGCOUNT			0x000000000F000000ULL
#define	IPG_TFC_VLANTAGINSERT			0x0000000010000000ULL
#define	IPG_TFC_TFDDONE				0x0000000080000000ULL
#define	IPG_TFC_VID				0x00000FFF00000000ULL
#define	IPG_TFC_CFI				0x0000100000000000ULL
#define	IPG_TFC_USERPRIORITY			0x0000E00000000000ULL

/* TFDList, FragInfo */
#define	IPG_TFI_RSVD_MASK			0xFFFF00FFFFFFFFFFULL
#define	IPG_TFI_FRAGADDR			0x000000FFFFFFFFFFULL
#define	IPG_TFI_FRAGLEN				0xFFFF000000000000ULL

/* RFD data structure masks. */

/* RFDList, RFS */
#define	IPG_RFS_RSVD_MASK			0x0000FFFFFFFFFFFFULL
#define	IPG_RFS_RXFRAMELEN			0x000000000000FFFFULL
#define	IPG_RFS_RXFIFOOVERRUN			0x0000000000010000ULL
#define	IPG_RFS_RXRUNTFRAME			0x0000000000020000ULL
#define	IPG_RFS_RXALIGNMENTERROR		0x0000000000040000ULL
#define	IPG_RFS_RXFCSERROR			0x0000000000080000ULL
#define	IPG_RFS_RXOVERSIZEDFRAME		0x0000000000100000ULL
#define	IPG_RFS_RXLENGTHERROR			0x0000000000200000ULL
#define	IPG_RFS_VLANDETECTED			0x0000000000400000ULL
#define	IPG_RFS_TCPDETECTED			0x0000000000800000ULL
#define	IPG_RFS_TCPERROR			0x0000000001000000ULL
#define	IPG_RFS_UDPDETECTED			0x0000000002000000ULL
#define	IPG_RFS_UDPERROR			0x0000000004000000ULL
#define	IPG_RFS_IPDETECTED			0x0000000008000000ULL
#define	IPG_RFS_IPERROR				0x0000000010000000ULL
#define	IPG_RFS_FRAMESTART			0x0000000020000000ULL
#define	IPG_RFS_FRAMEEND			0x0000000040000000ULL
#define	IPG_RFS_RFDDONE				0x0000000080000000ULL
#define	IPG_RFS_TCI				0x0000FFFF00000000ULL

/* RFDList, FragInfo */
#define	IPG_RFI_RSVD_MASK			0xFFFF00FFFFFFFFFFULL
#define	IPG_RFI_FRAGADDR			0x000000FFFFFFFFFFULL
#define	IPG_RFI_FRAGLEN				0xFFFF000000000000ULL

/* I/O Register masks. */

/* RMON Statistics Mask */
#define	IPG_RZ_ALL					0x0FFFFFFF

/* Statistics Mask */
#define	IPG_SM_ALL					0x0FFFFFFF
#define	IPG_SM_OCTETRCVOK_FRAMESRCVDOK			0x00000001
#define	IPG_SM_MCSTOCTETRCVDOK_MCSTFRAMESRCVDOK		0x00000002
#define	IPG_SM_BCSTOCTETRCVDOK_BCSTFRAMESRCVDOK		0x00000004
#define	IPG_SM_RXJUMBOFRAMES				0x00000008
#define	IPG_SM_TCPCHECKSUMERRORS			0x00000010
#define	IPG_SM_IPCHECKSUMERRORS				0x00000020
#define	IPG_SM_UDPCHECKSUMERRORS			0x00000040
#define	IPG_SM_MACCONTROLFRAMESRCVD			0x00000080
#define	IPG_SM_FRAMESTOOLONGERRORS			0x00000100
#define	IPG_SM_INRANGELENGTHERRORS			0x00000200
#define	IPG_SM_FRAMECHECKSEQERRORS			0x00000400
#define	IPG_SM_FRAMESLOSTRXERRORS			0x00000800
#define	IPG_SM_OCTETXMTOK_FRAMESXMTOK			0x00001000
#define	IPG_SM_MCSTOCTETXMTOK_MCSTFRAMESXMTDOK		0x00002000
#define	IPG_SM_BCSTOCTETXMTOK_BCSTFRAMESXMTDOK		0x00004000
#define	IPG_SM_FRAMESWDEFERREDXMT			0x00008000
#define	IPG_SM_LATECOLLISIONS				0x00010000
#define	IPG_SM_MULTICOLFRAMES				0x00020000
#define	IPG_SM_SINGLECOLFRAMES				0x00040000
#define	IPG_SM_TXJUMBOFRAMES				0x00080000
#define	IPG_SM_CARRIERSENSEERRORS			0x00100000
#define	IPG_SM_MACCONTROLFRAMESXMTD			0x00200000
#define	IPG_SM_FRAMESABORTXSCOLLS			0x00400000
#define	IPG_SM_FRAMESWEXDEFERAL				0x00800000

/* Countdown */
#define	IPG_CD_RSVD_MASK		0x0700FFFF
#define	IPG_CD_COUNT			0x0000FFFF
#define	IPG_CD_COUNTDOWNSPEED		0x01000000
#define	IPG_CD_COUNTDOWNMODE		0x02000000
#define	IPG_CD_COUNTINTENABLED		0x04000000

/* TxDMABurstThresh */
#define IPG_TB_RSVD_MASK                0xFF

/* TxDMAUrgentThresh */
#define IPG_TU_RSVD_MASK                0xFF

/* TxDMAPollPeriod */
#define IPG_TP_RSVD_MASK                0xFF

/* RxDMAUrgentThresh */
#define IPG_RU_RSVD_MASK                0xFF

/* RxDMAPollPeriod */
#define IPG_RP_RSVD_MASK                0xFF

/* ReceiveMode */
#define IPG_RM_RSVD_MASK                0x3F
#define IPG_RM_RECEIVEUNICAST           0x01
#define IPG_RM_RECEIVEMULTICAST         0x02
#define IPG_RM_RECEIVEBROADCAST         0x04
#define IPG_RM_RECEIVEALLFRAMES         0x08
#define IPG_RM_RECEIVEMULTICASTHASH     0x10
#define IPG_RM_RECEIVEIPMULTICAST       0x20

/* PhySet */
#define IPG_PS_MEM_LENB9B               0x01
#define IPG_PS_MEM_LEN9                 0x02
#define IPG_PS_NON_COMPDET              0x04

/* PhyCtrl */
#define IPG_PC_RSVD_MASK                0xFF
#define IPG_PC_MGMTCLK_LO               0x00
#define IPG_PC_MGMTCLK_HI               0x01
#define IPG_PC_MGMTCLK                  0x01
#define IPG_PC_MGMTDATA                 0x02
#define IPG_PC_MGMTDIR                  0x04
#define IPG_PC_DUPLEX_POLARITY          0x08
#define IPG_PC_DUPLEX_STATUS            0x10
#define IPG_PC_LINK_POLARITY            0x20
#define IPG_PC_LINK_SPEED               0xC0
#define IPG_PC_LINK_SPEED_10MBPS        0x40
#define IPG_PC_LINK_SPEED_100MBPS       0x80
#define IPG_PC_LINK_SPEED_1000MBPS      0xC0

/* DMACtrl */
#define IPG_DC_RSVD_MASK                0xC07D9818
#define IPG_DC_RX_DMA_COMPLETE          0x00000008
#define IPG_DC_RX_DMA_POLL_NOW          0x00000010
#define IPG_DC_TX_DMA_COMPLETE          0x00000800
#define IPG_DC_TX_DMA_POLL_NOW          0x00001000
#define IPG_DC_TX_DMA_IN_PROG           0x00008000
#define IPG_DC_RX_EARLY_DISABLE         0x00010000
#define IPG_DC_MWI_DISABLE              0x00040000
#define IPG_DC_TX_WRITE_BACK_DISABLE    0x00080000
#define IPG_DC_TX_BURST_LIMIT           0x00700000
#define IPG_DC_TARGET_ABORT             0x40000000
#define IPG_DC_MASTER_ABORT             0x80000000

/* ASICCtrl */
#define IPG_AC_RSVD_MASK                0x07FFEFF2
#define IPG_AC_EXP_ROM_SIZE             0x00000002
#define IPG_AC_PHY_SPEED10              0x00000010
#define IPG_AC_PHY_SPEED100             0x00000020
#define IPG_AC_PHY_SPEED1000            0x00000040
#define IPG_AC_PHY_MEDIA                0x00000080
#define IPG_AC_FORCED_CFG               0x00000700
#define IPG_AC_D3RESETDISABLE           0x00000800
#define IPG_AC_SPEED_UP_MODE            0x00002000
#define IPG_AC_LED_MODE                 0x00004000
#define IPG_AC_RST_OUT_POLARITY         0x00008000
#define IPG_AC_GLOBAL_RESET             0x00010000
#define IPG_AC_RX_RESET                 0x00020000
#define IPG_AC_TX_RESET                 0x00040000
#define IPG_AC_DMA                      0x00080000
#define IPG_AC_FIFO                     0x00100000
#define IPG_AC_NETWORK                  0x00200000
#define IPG_AC_HOST                     0x00400000
#define IPG_AC_AUTO_INIT                0x00800000
#define IPG_AC_RST_OUT                  0x01000000
#define IPG_AC_INT_REQUEST              0x02000000
#define IPG_AC_RESET_BUSY               0x04000000
#define IPG_AC_LED_SPEED                0x08000000
#define IPG_AC_LED_MODE_BIT_1           0x20000000

/* EepromCtrl */
#define IPG_EC_RSVD_MASK                0x83FF
#define IPG_EC_EEPROM_ADDR              0x00FF
#define IPG_EC_EEPROM_OPCODE            0x0300
#define IPG_EC_EEPROM_SUBCOMMAD         0x0000
#define IPG_EC_EEPROM_WRITEOPCODE       0x0100
#define IPG_EC_EEPROM_READOPCODE        0x0200
#define IPG_EC_EEPROM_ERASEOPCODE       0x0300
#define IPG_EC_EEPROM_BUSY              0x8000

/* FIFOCtrl */
#define IPG_FC_RSVD_MASK                0xC001
#define IPG_FC_RAM_TEST_MODE            0x0001
#define IPG_FC_TRANSMITTING             0x4000
#define IPG_FC_RECEIVING                0x8000

/* TxStatus */
#define IPG_TS_RSVD_MASK                0xFFFF00DD
#define IPG_TS_TX_ERROR                 0x00000001
#define IPG_TS_LATE_COLLISION           0x00000004
#define IPG_TS_TX_MAX_COLL              0x00000008
#define IPG_TS_TX_UNDERRUN              0x00000010
#define IPG_TS_TX_IND_REQD              0x00000040
#define IPG_TS_TX_COMPLETE              0x00000080
#define IPG_TS_TX_FRAMEID               0xFFFF0000

/* WakeEvent */
#define IPG_WE_WAKE_PKT_ENABLE          0x01
#define IPG_WE_MAGIC_PKT_ENABLE         0x02
#define IPG_WE_LINK_EVT_ENABLE          0x04
#define IPG_WE_WAKE_POLARITY            0x08
#define IPG_WE_WAKE_PKT_EVT             0x10
#define IPG_WE_MAGIC_PKT_EVT            0x20
#define IPG_WE_LINK_EVT                 0x40
#define IPG_WE_WOL_ENABLE               0x80

/* IntEnable */
#define IPG_IE_RSVD_MASK                0x1FFE
#define IPG_IE_HOST_ERROR               0x0002
#define IPG_IE_TX_COMPLETE              0x0004
#define IPG_IE_MAC_CTRL_FRAME           0x0008
#define IPG_IE_RX_COMPLETE              0x0010
#define IPG_IE_RX_EARLY                 0x0020
#define IPG_IE_INT_REQUESTED            0x0040
#define IPG_IE_UPDATE_STATS             0x0080
#define IPG_IE_LINK_EVENT               0x0100
#define IPG_IE_TX_DMA_COMPLETE          0x0200
#define IPG_IE_RX_DMA_COMPLETE          0x0400
#define IPG_IE_RFD_LIST_END             0x0800
#define IPG_IE_RX_DMA_PRIORITY          0x1000

/* IntStatus */
#define IPG_IS_RSVD_MASK                0x1FFF
#define IPG_IS_INTERRUPT_STATUS         0x0001
#define IPG_IS_HOST_ERROR               0x0002
#define IPG_IS_TX_COMPLETE              0x0004
#define IPG_IS_MAC_CTRL_FRAME           0x0008
#define IPG_IS_RX_COMPLETE              0x0010
#define IPG_IS_RX_EARLY                 0x0020
#define IPG_IS_INT_REQUESTED            0x0040
#define IPG_IS_UPDATE_STATS             0x0080
#define IPG_IS_LINK_EVENT               0x0100
#define IPG_IS_TX_DMA_COMPLETE          0x0200
#define IPG_IS_RX_DMA_COMPLETE          0x0400
#define IPG_IS_RFD_LIST_END             0x0800
#define IPG_IS_RX_DMA_PRIORITY          0x1000

/* MACCtrl */
#define IPG_MC_RSVD_MASK                0x7FE33FA3
#define IPG_MC_IFS_SELECT               0x00000003
#define IPG_MC_IFS_4352BIT              0x00000003
#define IPG_MC_IFS_1792BIT              0x00000002
#define IPG_MC_IFS_1024BIT              0x00000001
#define IPG_MC_IFS_96BIT                0x00000000
#define IPG_MC_DUPLEX_SELECT            0x00000020
#define IPG_MC_DUPLEX_SELECT_FD         0x00000020
#define IPG_MC_DUPLEX_SELECT_HD         0x00000000
#define IPG_MC_TX_FLOW_CONTROL_ENABLE   0x00000080
#define IPG_MC_RX_FLOW_CONTROL_ENABLE   0x00000100
#define IPG_MC_RCV_FCS                  0x00000200
#define IPG_MC_FIFO_LOOPBACK            0x00000400
#define IPG_MC_MAC_LOOPBACK             0x00000800
#define IPG_MC_AUTO_VLAN_TAGGING        0x00001000
#define IPG_MC_AUTO_VLAN_UNTAGGING      0x00002000
#define IPG_MC_COLLISION_DETECT         0x00010000
#define IPG_MC_CARRIER_SENSE            0x00020000
#define IPG_MC_STATISTICS_ENABLE        0x00200000
#define IPG_MC_STATISTICS_DISABLE       0x00400000
#define IPG_MC_STATISTICS_ENABLED       0x00800000
#define IPG_MC_TX_ENABLE                0x01000000
#define IPG_MC_TX_DISABLE               0x02000000
#define IPG_MC_TX_ENABLED               0x04000000
#define IPG_MC_RX_ENABLE                0x08000000
#define IPG_MC_RX_DISABLE               0x10000000
#define IPG_MC_RX_ENABLED               0x20000000
#define IPG_MC_PAUSED                   0x40000000

/*
 *	Tune
 */

/* Assign IPG_APPEND_FCS_ON_TX > 0 for auto FCS append on TX. */
#define         IPG_APPEND_FCS_ON_TX         1

/* Assign IPG_APPEND_FCS_ON_TX > 0 for auto FCS strip on RX. */
#define         IPG_STRIP_FCS_ON_RX          1

/* Assign IPG_DROP_ON_RX_ETH_ERRORS > 0 to drop RX frames with
 * Ethernet errors.
 */
#define         IPG_DROP_ON_RX_ETH_ERRORS    1

/* Assign IPG_INSERT_MANUAL_VLAN_TAG > 0 to insert VLAN tags manually
 * (via TFC).
 */
#define		IPG_INSERT_MANUAL_VLAN_TAG   0

/* Assign IPG_ADD_IPCHECKSUM_ON_TX > 0 for auto IP checksum on TX. */
#define         IPG_ADD_IPCHECKSUM_ON_TX     0

/* Assign IPG_ADD_TCPCHECKSUM_ON_TX > 0 for auto TCP checksum on TX.
 * DO NOT USE FOR SILICON REVISIONS B3 AND EARLIER.
 */
#define         IPG_ADD_TCPCHECKSUM_ON_TX    0

/* Assign IPG_ADD_UDPCHECKSUM_ON_TX > 0 for auto UDP checksum on TX.
 * DO NOT USE FOR SILICON REVISIONS B3 AND EARLIER.
 */
#define         IPG_ADD_UDPCHECKSUM_ON_TX    0

/* If inserting VLAN tags manually, assign the IPG_MANUAL_VLAN_xx
 * constants as desired.
 */
#define		IPG_MANUAL_VLAN_VID		0xABC
#define		IPG_MANUAL_VLAN_CFI		0x1
#define		IPG_MANUAL_VLAN_USERPRIORITY 0x5

#define         IPG_IO_REG_RANGE		0xFF
#define         IPG_MEM_REG_RANGE		0x154
#define         IPG_DRIVER_NAME		"Sundance Technology IPG Triple-Speed Ethernet"
#define         IPG_NIC_PHY_ADDRESS          0x01
#define		IPG_DMALIST_ALIGN_PAD	0x07
#define		IPG_MULTICAST_HASHTABLE_SIZE	0x40

/* Number of milliseconds to wait after issuing a software reset.
 * 0x05 <= IPG_AC_RESETWAIT to account for proper 10Mbps operation.
 */
#define         IPG_AC_RESETWAIT             0x05

/* Number of IPG_AC_RESETWAIT timeperiods before declaring timeout. */
#define         IPG_AC_RESET_TIMEOUT         0x0A

/* Minimum number of nanoseconds used to toggle MDC clock during
 * MII/GMII register access.
 */
#define		IPG_PC_PHYCTRLWAIT_NS		200

#define		IPG_TFDLIST_LENGTH		0x100

/* Number of frames between TxDMAComplete interrupt.
 * 0 < IPG_FRAMESBETWEENTXDMACOMPLETES <= IPG_TFDLIST_LENGTH
 */
#define		IPG_FRAMESBETWEENTXDMACOMPLETES 0x1

#define		IPG_RFDLIST_LENGTH		0x100

/* Maximum number of RFDs to process per interrupt.
 * 1 < IPG_MAXRFDPROCESS_COUNT < IPG_RFDLIST_LENGTH
 */
#define		IPG_MAXRFDPROCESS_COUNT	0x80

/* Minimum margin between last freed RFD, and current RFD.
 * 1 < IPG_MINUSEDRFDSTOFREE < IPG_RFDLIST_LENGTH
 */
#define		IPG_MINUSEDRFDSTOFREE	0x80

/* specify the jumbo frame maximum size
 * per unit is 0x600 (the rx_buffer size that one RFD can carry)
 */
#define     MAX_JUMBOSIZE	        0x8	/* max is 12K */

/* Key register values loaded at driver start up. */

/* TXDMAPollPeriod is specified in 320ns increments.
 *
 * Value	Time
 * ---------------------
 * 0x00-0x01	320ns
 * 0x03		~1us
 * 0x1F		~10us
 * 0xFF		~82us
 */
#define		IPG_TXDMAPOLLPERIOD_VALUE	0x26

/* TxDMAUrgentThresh specifies the minimum amount of
 * data in the transmit FIFO before asserting an
 * urgent transmit DMA request.
 *
 * Value	Min TxFIFO occupied space before urgent TX request
 * ---------------------------------------------------------------
 * 0x00-0x04	128 bytes (1024 bits)
 * 0x27		1248 bytes (~10000 bits)
 * 0x30		1536 bytes (12288 bits)
 * 0xFF		8192 bytes (65535 bits)
 */
#define		IPG_TXDMAURGENTTHRESH_VALUE	0x04

/* TxDMABurstThresh specifies the minimum amount of
 * free space in the transmit FIFO before asserting an
 * transmit DMA request.
 *
 * Value	Min TxFIFO free space before TX request
 * ----------------------------------------------------
 * 0x00-0x08	256 bytes
 * 0x30		1536 bytes
 * 0xFF		8192 bytes
 */
#define		IPG_TXDMABURSTTHRESH_VALUE	0x30

/* RXDMAPollPeriod is specified in 320ns increments.
 *
 * Value	Time
 * ---------------------
 * 0x00-0x01	320ns
 * 0x03		~1us
 * 0x1F		~10us
 * 0xFF		~82us
 */
#define		IPG_RXDMAPOLLPERIOD_VALUE	0x01

/* RxDMAUrgentThresh specifies the minimum amount of
 * free space within the receive FIFO before asserting
 * a urgent receive DMA request.
 *
 * Value	Min RxFIFO free space before urgent RX request
 * ---------------------------------------------------------------
 * 0x00-0x04	128 bytes (1024 bits)
 * 0x27		1248 bytes (~10000 bits)
 * 0x30		1536 bytes (12288 bits)
 * 0xFF		8192 bytes (65535 bits)
 */
#define		IPG_RXDMAURGENTTHRESH_VALUE	0x30

/* RxDMABurstThresh specifies the minimum amount of
 * occupied space within the receive FIFO before asserting
 * a receive DMA request.
 *
 * Value	Min TxFIFO free space before TX request
 * ----------------------------------------------------
 * 0x00-0x08	256 bytes
 * 0x30		1536 bytes
 * 0xFF		8192 bytes
 */
#define		IPG_RXDMABURSTTHRESH_VALUE	0x30

/* FlowOnThresh specifies the maximum amount of occupied
 * space in the receive FIFO before a PAUSE frame with
 * maximum pause time transmitted.
 *
 * Value	Max RxFIFO occupied space before PAUSE
 * ---------------------------------------------------
 * 0x0000	0 bytes
 * 0x0740	29,696 bytes
 * 0x07FF	32,752 bytes
 */
#define		IPG_FLOWONTHRESH_VALUE	0x0740

/* FlowOffThresh specifies the minimum amount of occupied
 * space in the receive FIFO before a PAUSE frame with
 * zero pause time is transmitted.
 *
 * Value	Max RxFIFO occupied space before PAUSE
 * ---------------------------------------------------
 * 0x0000	0 bytes
 * 0x00BF	3056 bytes
 * 0x07FF	32,752 bytes
 */
#define		IPG_FLOWOFFTHRESH_VALUE	0x00BF

/*
 * Miscellaneous macros.
 */

/* Macros for printing debug statements. */
#ifdef IPG_DEBUG
#  define IPG_DEBUG_MSG(fmt, args...)			\
do {							\
	if (0)						\
		printk(KERN_DEBUG "IPG: " fmt, ##args);	\
} while (0)
#  define IPG_DDEBUG_MSG(fmt, args...)			\
	printk(KERN_DEBUG "IPG: " fmt, ##args)
#  define IPG_DUMPRFDLIST(args) ipg_dump_rfdlist(args)
#  define IPG_DUMPTFDLIST(args) ipg_dump_tfdlist(args)
#else
#  define IPG_DEBUG_MSG(fmt, args...)			\
do {							\
	if (0)						\
		printk(KERN_DEBUG "IPG: " fmt, ##args);	\
} while (0)
#  define IPG_DDEBUG_MSG(fmt, args...)			\
do {							\
	if (0)						\
		printk(KERN_DEBUG "IPG: " fmt, ##args);	\
} while (0)
#  define IPG_DUMPRFDLIST(args)
#  define IPG_DUMPTFDLIST(args)
#endif

/*
 * End miscellaneous macros.
 */

/* Transmit Frame Descriptor. The IPG supports 15 fragments,
 * however Linux requires only a single fragment. Note, each
 * TFD field is 64 bits wide.
 */
struct ipg_tx {
	__le64 next_desc;
	__le64 tfc;
	__le64 frag_info;
};

/* Receive Frame Descriptor. Note, each RFD field is 64 bits wide.
 */
struct ipg_rx {
	__le64 next_desc;
	__le64 rfs;
	__le64 frag_info;
};

struct ipg_jumbo {
	int found_start;
	int current_size;
	struct sk_buff *skb;
};

/* Structure of IPG NIC specific data. */
struct ipg_nic_private {
	void __iomem *ioaddr;
	struct ipg_tx *txd;
	struct ipg_rx *rxd;
	dma_addr_t txd_map;
	dma_addr_t rxd_map;
	struct sk_buff *tx_buff[IPG_TFDLIST_LENGTH];
	struct sk_buff *rx_buff[IPG_RFDLIST_LENGTH];
	unsigned int tx_current;
	unsigned int tx_dirty;
	unsigned int rx_current;
	unsigned int rx_dirty;
	bool is_jumbo;
	struct ipg_jumbo jumbo;
	unsigned long rxfrag_size;
	unsigned long rxsupport_size;
	unsigned long max_rxframe_size;
	unsigned int rx_buf_sz;
	struct pci_dev *pdev;
	struct net_device *dev;
	struct net_device_stats stats;
	spinlock_t lock;
	int tenmbpsmode;

	u16 led_mode;
	u16 station_addr[3];	/* Station Address in EEPROM Reg 0x10..0x12 */

	struct mutex		mii_mutex;
	struct mii_if_info	mii_if;
	int reset_current_tfd;
#ifdef IPG_DEBUG
	int RFDlistendCount;
	int RFDListCheckedCount;
	int EmptyRFDListCount;
#endif
	struct delayed_work task;
};

#endif				/* __LINUX_IPG_H */
