/*
 * QLogic QLA3xxx NIC HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla3xxx for copyright and licensing details.
 */
#ifndef _QLA3XXX_H_
#define _QLA3XXX_H_

/*
 * IOCB Definitions...
 */
#pragma pack(1)

#define OPCODE_OB_MAC_IOCB_FN0          0x01
#define OPCODE_OB_MAC_IOCB_FN2          0x21

#define OPCODE_IB_MAC_IOCB          0xF9
#define OPCODE_IB_3032_MAC_IOCB     0x09
#define OPCODE_IB_IP_IOCB           0xFA
#define OPCODE_IB_3032_IP_IOCB      0x0A

#define OPCODE_FUNC_ID_MASK                 0x30
#define OUTBOUND_MAC_IOCB                   0x01	/* plus function bits */

#define FN0_MA_BITS_MASK    0x00
#define FN1_MA_BITS_MASK    0x80

struct ob_mac_iocb_req {
	u8 opcode;
	u8 flags;
#define OB_MAC_IOCB_REQ_MA  0xe0
#define OB_MAC_IOCB_REQ_F   0x10
#define OB_MAC_IOCB_REQ_X   0x08
#define OB_MAC_IOCB_REQ_D   0x02
#define OB_MAC_IOCB_REQ_I   0x01
	u8 flags1;
#define OB_3032MAC_IOCB_REQ_IC	0x04
#define OB_3032MAC_IOCB_REQ_TC	0x02
#define OB_3032MAC_IOCB_REQ_UC	0x01
	u8 reserved0;

	u32 transaction_id;	/* opaque for hardware */
	__le16 data_len;
	u8 ip_hdr_off;
	u8 ip_hdr_len;
	__le32 reserved1;
	__le32 reserved2;
	__le32 buf_addr0_low;
	__le32 buf_addr0_high;
	__le32 buf_0_len;
	__le32 buf_addr1_low;
	__le32 buf_addr1_high;
	__le32 buf_1_len;
	__le32 buf_addr2_low;
	__le32 buf_addr2_high;
	__le32 buf_2_len;
	__le32 reserved3;
	__le32 reserved4;
};
/*
 * The following constants define control bits for buffer
 * length fields for all IOCB's.
 */
#define OB_MAC_IOCB_REQ_E   0x80000000	/* Last valid buffer in list. */
#define OB_MAC_IOCB_REQ_C   0x40000000	/* points to an OAL. (continuation) */
#define OB_MAC_IOCB_REQ_L   0x20000000	/* Auburn local address pointer. */
#define OB_MAC_IOCB_REQ_R   0x10000000	/* 32-bit address pointer. */

struct ob_mac_iocb_rsp {
	u8 opcode;
	u8 flags;
#define OB_MAC_IOCB_RSP_P   0x08
#define OB_MAC_IOCB_RSP_L   0x04
#define OB_MAC_IOCB_RSP_S   0x02
#define OB_MAC_IOCB_RSP_I   0x01

	__le16 reserved0;
	u32 transaction_id;	/* opaque for hardware */
	__le32 reserved1;
	__le32 reserved2;
};

struct ib_mac_iocb_rsp {
	u8 opcode;
#define IB_MAC_IOCB_RSP_V   0x80
	u8 flags;
#define IB_MAC_IOCB_RSP_S   0x80
#define IB_MAC_IOCB_RSP_H1  0x40
#define IB_MAC_IOCB_RSP_H0  0x20
#define IB_MAC_IOCB_RSP_B   0x10
#define IB_MAC_IOCB_RSP_M   0x08
#define IB_MAC_IOCB_RSP_MA  0x07

	__le16 length;
	__le32 reserved;
	__le32 ial_low;
	__le32 ial_high;

};

struct ob_ip_iocb_req {
	u8 opcode;
	__le16 flags;
#define OB_IP_IOCB_REQ_O        0x100
#define OB_IP_IOCB_REQ_H        0x008
#define OB_IP_IOCB_REQ_U        0x004
#define OB_IP_IOCB_REQ_D        0x002
#define OB_IP_IOCB_REQ_I        0x001

	u8 reserved0;

	__le32 transaction_id;
	__le16 data_len;
	__le16 reserved1;
	__le32 hncb_ptr_low;
	__le32 hncb_ptr_high;
	__le32 buf_addr0_low;
	__le32 buf_addr0_high;
	__le32 buf_0_len;
	__le32 buf_addr1_low;
	__le32 buf_addr1_high;
	__le32 buf_1_len;
	__le32 buf_addr2_low;
	__le32 buf_addr2_high;
	__le32 buf_2_len;
	__le32 reserved2;
	__le32 reserved3;
};

/* defines for BufferLength fields above */
#define OB_IP_IOCB_REQ_E    0x80000000
#define OB_IP_IOCB_REQ_C    0x40000000
#define OB_IP_IOCB_REQ_L    0x20000000
#define OB_IP_IOCB_REQ_R    0x10000000

struct ob_ip_iocb_rsp {
	u8 opcode;
	u8 flags;
#define OB_MAC_IOCB_RSP_H       0x10
#define OB_MAC_IOCB_RSP_E       0x08
#define OB_MAC_IOCB_RSP_L       0x04
#define OB_MAC_IOCB_RSP_S       0x02
#define OB_MAC_IOCB_RSP_I       0x01

	__le16 reserved0;
	__le32 transaction_id;
	__le32 reserved1;
	__le32 reserved2;
};

struct ib_ip_iocb_rsp {
	u8 opcode;
#define IB_IP_IOCB_RSP_3032_V   0x80
#define IB_IP_IOCB_RSP_3032_O   0x40
#define IB_IP_IOCB_RSP_3032_I   0x20
#define IB_IP_IOCB_RSP_3032_R   0x10
	u8 flags;
#define IB_IP_IOCB_RSP_S        0x80
#define IB_IP_IOCB_RSP_H1       0x40
#define IB_IP_IOCB_RSP_H0       0x20
#define IB_IP_IOCB_RSP_B        0x10
#define IB_IP_IOCB_RSP_M        0x08
#define IB_IP_IOCB_RSP_MA       0x07

	__le16 length;
	__le16 checksum;
#define IB_IP_IOCB_RSP_3032_ICE		0x01
#define IB_IP_IOCB_RSP_3032_CE		0x02
#define IB_IP_IOCB_RSP_3032_NUC		0x04
#define IB_IP_IOCB_RSP_3032_UDP		0x08
#define IB_IP_IOCB_RSP_3032_TCP		0x10
#define IB_IP_IOCB_RSP_3032_IPE		0x20
	__le16 reserved;
#define IB_IP_IOCB_RSP_R        0x01
	__le32 ial_low;
	__le32 ial_high;
};

struct net_rsp_iocb {
	u8 opcode;
	u8 flags;
	__le16 reserved0;
	__le32 reserved[3];
};
#pragma pack()

/*
 * Register Definitions...
 */
#define PORT0_PHY_ADDRESS   0x1e00
#define PORT1_PHY_ADDRESS   0x1f00

#define ETHERNET_CRC_SIZE   4

#define MII_SCAN_REGISTER 0x00000001

#define PHY_ID_0_REG    2
#define PHY_ID_1_REG    3

#define PHY_OUI_1_MASK       0xfc00
#define PHY_MODEL_MASK       0x03f0

/*  Address for the Agere Phy */
#define MII_AGERE_ADDR_1  0x00001000
#define MII_AGERE_ADDR_2  0x00001100

/* 32-bit ispControlStatus */
enum {
	ISP_CONTROL_NP_MASK = 0x0003,
	ISP_CONTROL_NP_PCSR = 0x0000,
	ISP_CONTROL_NP_HMCR = 0x0001,
	ISP_CONTROL_NP_LRAMCR = 0x0002,
	ISP_CONTROL_NP_PSR = 0x0003,
	ISP_CONTROL_RI = 0x0008,
	ISP_CONTROL_CI = 0x0010,
	ISP_CONTROL_PI = 0x0020,
	ISP_CONTROL_IN = 0x0040,
	ISP_CONTROL_BE = 0x0080,
	ISP_CONTROL_FN_MASK = 0x0700,
	ISP_CONTROL_FN0_NET = 0x0400,
	ISP_CONTROL_FN0_SCSI = 0x0500,
	ISP_CONTROL_FN1_NET = 0x0600,
	ISP_CONTROL_FN1_SCSI = 0x0700,
	ISP_CONTROL_LINK_DN_0 = 0x0800,
	ISP_CONTROL_LINK_DN_1 = 0x1000,
	ISP_CONTROL_FSR = 0x2000,
	ISP_CONTROL_FE = 0x4000,
	ISP_CONTROL_SR = 0x8000,
};

/* 32-bit ispInterruptMaskReg */
enum {
	ISP_IMR_ENABLE_INT = 0x0004,
	ISP_IMR_DISABLE_RESET_INT = 0x0008,
	ISP_IMR_DISABLE_CMPL_INT = 0x0010,
	ISP_IMR_DISABLE_PROC_INT = 0x0020,
};

/* 32-bit serialPortInterfaceReg */
enum {
	ISP_SERIAL_PORT_IF_CLK = 0x0001,
	ISP_SERIAL_PORT_IF_CS = 0x0002,
	ISP_SERIAL_PORT_IF_D0 = 0x0004,
	ISP_SERIAL_PORT_IF_DI = 0x0008,
	ISP_NVRAM_MASK = (0x000F << 16),
	ISP_SERIAL_PORT_IF_WE = 0x0010,
	ISP_SERIAL_PORT_IF_NVR_MASK = 0x001F,
	ISP_SERIAL_PORT_IF_SCI = 0x0400,
	ISP_SERIAL_PORT_IF_SC0 = 0x0800,
	ISP_SERIAL_PORT_IF_SCE = 0x1000,
	ISP_SERIAL_PORT_IF_SDI = 0x2000,
	ISP_SERIAL_PORT_IF_SDO = 0x4000,
	ISP_SERIAL_PORT_IF_SDE = 0x8000,
	ISP_SERIAL_PORT_IF_I2C_MASK = 0xFC00,
};

/* semaphoreReg */
enum {
	QL_RESOURCE_MASK_BASE_CODE = 0x7,
	QL_RESOURCE_BITS_BASE_CODE = 0x4,
	QL_DRVR_SEM_BITS = (QL_RESOURCE_BITS_BASE_CODE << 1),
	QL_DDR_RAM_SEM_BITS = (QL_RESOURCE_BITS_BASE_CODE << 4),
	QL_PHY_GIO_SEM_BITS = (QL_RESOURCE_BITS_BASE_CODE << 7),
	QL_NVRAM_SEM_BITS = (QL_RESOURCE_BITS_BASE_CODE << 10),
	QL_FLASH_SEM_BITS = (QL_RESOURCE_BITS_BASE_CODE << 13),
	QL_DRVR_SEM_MASK = (QL_RESOURCE_MASK_BASE_CODE << (1 + 16)),
	QL_DDR_RAM_SEM_MASK = (QL_RESOURCE_MASK_BASE_CODE << (4 + 16)),
	QL_PHY_GIO_SEM_MASK = (QL_RESOURCE_MASK_BASE_CODE << (7 + 16)),
	QL_NVRAM_SEM_MASK = (QL_RESOURCE_MASK_BASE_CODE << (10 + 16)),
	QL_FLASH_SEM_MASK = (QL_RESOURCE_MASK_BASE_CODE << (13 + 16)),
};

 /*
  * QL3XXX memory-mapped registers
  * QL3XXX has 4 "pages" of registers, each page occupying
  * 256 bytes.  Each page has a "common" area at the start and then
  * page-specific registers after that.
  */
struct ql3xxx_common_registers {
	u32 MB0;		/* Offset 0x00 */
	u32 MB1;		/* Offset 0x04 */
	u32 MB2;		/* Offset 0x08 */
	u32 MB3;		/* Offset 0x0c */
	u32 MB4;		/* Offset 0x10 */
	u32 MB5;		/* Offset 0x14 */
	u32 MB6;		/* Offset 0x18 */
	u32 MB7;		/* Offset 0x1c */
	u32 flashBiosAddr;
	u32 flashBiosData;
	u32 ispControlStatus;
	u32 ispInterruptMaskReg;
	u32 serialPortInterfaceReg;
	u32 semaphoreReg;
	u32 reqQProducerIndex;
	u32 rspQConsumerIndex;

	u32 rxLargeQProducerIndex;
	u32 rxSmallQProducerIndex;
	u32 arcMadiCommand;
	u32 arcMadiData;
};

enum {
	EXT_HW_CONFIG_SP_MASK = 0x0006,
	EXT_HW_CONFIG_SP_NONE = 0x0000,
	EXT_HW_CONFIG_SP_BYTE_PARITY = 0x0002,
	EXT_HW_CONFIG_SP_ECC = 0x0004,
	EXT_HW_CONFIG_SP_ECCx = 0x0006,
	EXT_HW_CONFIG_SIZE_MASK = 0x0060,
	EXT_HW_CONFIG_SIZE_128M = 0x0000,
	EXT_HW_CONFIG_SIZE_256M = 0x0020,
	EXT_HW_CONFIG_SIZE_512M = 0x0040,
	EXT_HW_CONFIG_SIZE_INVALID = 0x0060,
	EXT_HW_CONFIG_PD = 0x0080,
	EXT_HW_CONFIG_FW = 0x0200,
	EXT_HW_CONFIG_US = 0x0400,
	EXT_HW_CONFIG_DCS_MASK = 0x1800,
	EXT_HW_CONFIG_DCS_9MA = 0x0000,
	EXT_HW_CONFIG_DCS_15MA = 0x0800,
	EXT_HW_CONFIG_DCS_18MA = 0x1000,
	EXT_HW_CONFIG_DCS_24MA = 0x1800,
	EXT_HW_CONFIG_DDS_MASK = 0x6000,
	EXT_HW_CONFIG_DDS_9MA = 0x0000,
	EXT_HW_CONFIG_DDS_15MA = 0x2000,
	EXT_HW_CONFIG_DDS_18MA = 0x4000,
	EXT_HW_CONFIG_DDS_24MA = 0x6000,
};

/* InternalChipConfig */
enum {
	INTERNAL_CHIP_DM = 0x0001,
	INTERNAL_CHIP_SD = 0x0002,
	INTERNAL_CHIP_RAP_MASK = 0x000C,
	INTERNAL_CHIP_RAP_RR = 0x0000,
	INTERNAL_CHIP_RAP_NRM = 0x0004,
	INTERNAL_CHIP_RAP_ERM = 0x0008,
	INTERNAL_CHIP_RAP_ERMx = 0x000C,
	INTERNAL_CHIP_WE = 0x0010,
	INTERNAL_CHIP_EF = 0x0020,
	INTERNAL_CHIP_FR = 0x0040,
	INTERNAL_CHIP_FW = 0x0080,
	INTERNAL_CHIP_FI = 0x0100,
	INTERNAL_CHIP_FT = 0x0200,
};

/* portControl */
enum {
	PORT_CONTROL_DS = 0x0001,
	PORT_CONTROL_HH = 0x0002,
	PORT_CONTROL_EI = 0x0004,
	PORT_CONTROL_ET = 0x0008,
	PORT_CONTROL_EF = 0x0010,
	PORT_CONTROL_DRM = 0x0020,
	PORT_CONTROL_RLB = 0x0040,
	PORT_CONTROL_RCB = 0x0080,
	PORT_CONTROL_MAC = 0x0100,
	PORT_CONTROL_IPV = 0x0200,
	PORT_CONTROL_IFP = 0x0400,
	PORT_CONTROL_ITP = 0x0800,
	PORT_CONTROL_FI = 0x1000,
	PORT_CONTROL_DFP = 0x2000,
	PORT_CONTROL_OI = 0x4000,
	PORT_CONTROL_CC = 0x8000,
};

/* portStatus */
enum {
	PORT_STATUS_SM0 = 0x0001,
	PORT_STATUS_SM1 = 0x0002,
	PORT_STATUS_X = 0x0008,
	PORT_STATUS_DL = 0x0080,
	PORT_STATUS_IC = 0x0200,
	PORT_STATUS_MRC = 0x0400,
	PORT_STATUS_NL = 0x0800,
	PORT_STATUS_REV_ID_MASK = 0x7000,
	PORT_STATUS_REV_ID_1 = 0x1000,
	PORT_STATUS_REV_ID_2 = 0x2000,
	PORT_STATUS_REV_ID_3 = 0x3000,
	PORT_STATUS_64 = 0x8000,
	PORT_STATUS_UP0 = 0x10000,
	PORT_STATUS_AC0 = 0x20000,
	PORT_STATUS_AE0 = 0x40000,
	PORT_STATUS_UP1 = 0x100000,
	PORT_STATUS_AC1 = 0x200000,
	PORT_STATUS_AE1 = 0x400000,
	PORT_STATUS_F0_ENABLED = 0x1000000,
	PORT_STATUS_F1_ENABLED = 0x2000000,
	PORT_STATUS_F2_ENABLED = 0x4000000,
	PORT_STATUS_F3_ENABLED = 0x8000000,
};

/* macMIIMgmtControlReg */
enum {
	MAC_ADDR_INDIRECT_PTR_REG_RP_MASK = 0x0003,
	MAC_ADDR_INDIRECT_PTR_REG_RP_PRI_LWR = 0x0000,
	MAC_ADDR_INDIRECT_PTR_REG_RP_PRI_UPR = 0x0001,
	MAC_ADDR_INDIRECT_PTR_REG_RP_SEC_LWR = 0x0002,
	MAC_ADDR_INDIRECT_PTR_REG_RP_SEC_UPR = 0x0003,
	MAC_ADDR_INDIRECT_PTR_REG_PR = 0x0008,
	MAC_ADDR_INDIRECT_PTR_REG_SS = 0x0010,
	MAC_ADDR_INDIRECT_PTR_REG_SE = 0x0020,
	MAC_ADDR_INDIRECT_PTR_REG_SP = 0x0040,
	MAC_ADDR_INDIRECT_PTR_REG_PE = 0x0080,
};

/* macMIIMgmtControlReg */
enum {
	MAC_MII_CONTROL_RC = 0x0001,
	MAC_MII_CONTROL_SC = 0x0002,
	MAC_MII_CONTROL_AS = 0x0004,
	MAC_MII_CONTROL_NP = 0x0008,
	MAC_MII_CONTROL_CLK_SEL_MASK = 0x0070,
	MAC_MII_CONTROL_CLK_SEL_DIV2 = 0x0000,
	MAC_MII_CONTROL_CLK_SEL_DIV4 = 0x0010,
	MAC_MII_CONTROL_CLK_SEL_DIV6 = 0x0020,
	MAC_MII_CONTROL_CLK_SEL_DIV8 = 0x0030,
	MAC_MII_CONTROL_CLK_SEL_DIV10 = 0x0040,
	MAC_MII_CONTROL_CLK_SEL_DIV14 = 0x0050,
	MAC_MII_CONTROL_CLK_SEL_DIV20 = 0x0060,
	MAC_MII_CONTROL_CLK_SEL_DIV28 = 0x0070,
	MAC_MII_CONTROL_RM = 0x8000,
};

/* macMIIStatusReg */
enum {
	MAC_MII_STATUS_BSY = 0x0001,
	MAC_MII_STATUS_SC = 0x0002,
	MAC_MII_STATUS_NV = 0x0004,
};

enum {
	MAC_CONFIG_REG_PE = 0x0001,
	MAC_CONFIG_REG_TF = 0x0002,
	MAC_CONFIG_REG_RF = 0x0004,
	MAC_CONFIG_REG_FD = 0x0008,
	MAC_CONFIG_REG_GM = 0x0010,
	MAC_CONFIG_REG_LB = 0x0020,
	MAC_CONFIG_REG_SR = 0x8000,
};

enum {
	MAC_HALF_DUPLEX_REG_ED = 0x10000,
	MAC_HALF_DUPLEX_REG_NB = 0x20000,
	MAC_HALF_DUPLEX_REG_BNB = 0x40000,
	MAC_HALF_DUPLEX_REG_ALT = 0x80000,
};

enum {
	IP_ADDR_INDEX_REG_MASK = 0x000f,
	IP_ADDR_INDEX_REG_FUNC_0_PRI = 0x0000,
	IP_ADDR_INDEX_REG_FUNC_0_SEC = 0x0001,
	IP_ADDR_INDEX_REG_FUNC_1_PRI = 0x0002,
	IP_ADDR_INDEX_REG_FUNC_1_SEC = 0x0003,
	IP_ADDR_INDEX_REG_FUNC_2_PRI = 0x0004,
	IP_ADDR_INDEX_REG_FUNC_2_SEC = 0x0005,
	IP_ADDR_INDEX_REG_FUNC_3_PRI = 0x0006,
	IP_ADDR_INDEX_REG_FUNC_3_SEC = 0x0007,
	IP_ADDR_INDEX_REG_6 = 0x0008,
	IP_ADDR_INDEX_REG_OFFSET_MASK = 0x0030,
	IP_ADDR_INDEX_REG_E = 0x0040,
};
enum {
	QL3032_PORT_CONTROL_DS = 0x0001,
	QL3032_PORT_CONTROL_HH = 0x0002,
	QL3032_PORT_CONTROL_EIv6 = 0x0004,
	QL3032_PORT_CONTROL_EIv4 = 0x0008,
	QL3032_PORT_CONTROL_ET = 0x0010,
	QL3032_PORT_CONTROL_EF = 0x0020,
	QL3032_PORT_CONTROL_DRM = 0x0040,
	QL3032_PORT_CONTROL_RLB = 0x0080,
	QL3032_PORT_CONTROL_RCB = 0x0100,
	QL3032_PORT_CONTROL_KIE = 0x0200,
};

enum {
	PROBE_MUX_ADDR_REG_MUX_SEL_MASK = 0x003f,
	PROBE_MUX_ADDR_REG_SYSCLK = 0x0000,
	PROBE_MUX_ADDR_REG_PCICLK = 0x0040,
	PROBE_MUX_ADDR_REG_NRXCLK = 0x0080,
	PROBE_MUX_ADDR_REG_CPUCLK = 0x00C0,
	PROBE_MUX_ADDR_REG_MODULE_SEL_MASK = 0x3f00,
	PROBE_MUX_ADDR_REG_UP = 0x4000,
	PROBE_MUX_ADDR_REG_RE = 0x8000,
};

enum {
	STATISTICS_INDEX_REG_MASK = 0x01ff,
	STATISTICS_INDEX_REG_MAC0_TX_FRAME = 0x0000,
	STATISTICS_INDEX_REG_MAC0_TX_BYTES = 0x0001,
	STATISTICS_INDEX_REG_MAC0_TX_STAT1 = 0x0002,
	STATISTICS_INDEX_REG_MAC0_TX_STAT2 = 0x0003,
	STATISTICS_INDEX_REG_MAC0_TX_STAT3 = 0x0004,
	STATISTICS_INDEX_REG_MAC0_TX_STAT4 = 0x0005,
	STATISTICS_INDEX_REG_MAC0_TX_STAT5 = 0x0006,
	STATISTICS_INDEX_REG_MAC0_RX_FRAME = 0x0007,
	STATISTICS_INDEX_REG_MAC0_RX_BYTES = 0x0008,
	STATISTICS_INDEX_REG_MAC0_RX_STAT1 = 0x0009,
	STATISTICS_INDEX_REG_MAC0_RX_STAT2 = 0x000a,
	STATISTICS_INDEX_REG_MAC0_RX_STAT3 = 0x000b,
	STATISTICS_INDEX_REG_MAC0_RX_ERR_CRC = 0x000c,
	STATISTICS_INDEX_REG_MAC0_RX_ERR_ENC = 0x000d,
	STATISTICS_INDEX_REG_MAC0_RX_ERR_LEN = 0x000e,
	STATISTICS_INDEX_REG_MAC0_RX_STAT4 = 0x000f,
	STATISTICS_INDEX_REG_MAC1_TX_FRAME = 0x0010,
	STATISTICS_INDEX_REG_MAC1_TX_BYTES = 0x0011,
	STATISTICS_INDEX_REG_MAC1_TX_STAT1 = 0x0012,
	STATISTICS_INDEX_REG_MAC1_TX_STAT2 = 0x0013,
	STATISTICS_INDEX_REG_MAC1_TX_STAT3 = 0x0014,
	STATISTICS_INDEX_REG_MAC1_TX_STAT4 = 0x0015,
	STATISTICS_INDEX_REG_MAC1_TX_STAT5 = 0x0016,
	STATISTICS_INDEX_REG_MAC1_RX_FRAME = 0x0017,
	STATISTICS_INDEX_REG_MAC1_RX_BYTES = 0x0018,
	STATISTICS_INDEX_REG_MAC1_RX_STAT1 = 0x0019,
	STATISTICS_INDEX_REG_MAC1_RX_STAT2 = 0x001a,
	STATISTICS_INDEX_REG_MAC1_RX_STAT3 = 0x001b,
	STATISTICS_INDEX_REG_MAC1_RX_ERR_CRC = 0x001c,
	STATISTICS_INDEX_REG_MAC1_RX_ERR_ENC = 0x001d,
	STATISTICS_INDEX_REG_MAC1_RX_ERR_LEN = 0x001e,
	STATISTICS_INDEX_REG_MAC1_RX_STAT4 = 0x001f,
	STATISTICS_INDEX_REG_IP_TX_PKTS = 0x0020,
	STATISTICS_INDEX_REG_IP_TX_BYTES = 0x0021,
	STATISTICS_INDEX_REG_IP_TX_FRAG = 0x0022,
	STATISTICS_INDEX_REG_IP_RX_PKTS = 0x0023,
	STATISTICS_INDEX_REG_IP_RX_BYTES = 0x0024,
	STATISTICS_INDEX_REG_IP_RX_FRAG = 0x0025,
	STATISTICS_INDEX_REG_IP_DGRM_REASSEMBLY = 0x0026,
	STATISTICS_INDEX_REG_IP_V6_RX_PKTS = 0x0027,
	STATISTICS_INDEX_REG_IP_RX_PKTERR = 0x0028,
	STATISTICS_INDEX_REG_IP_REASSEMBLY_ERR = 0x0029,
	STATISTICS_INDEX_REG_TCP_TX_SEG = 0x0030,
	STATISTICS_INDEX_REG_TCP_TX_BYTES = 0x0031,
	STATISTICS_INDEX_REG_TCP_RX_SEG = 0x0032,
	STATISTICS_INDEX_REG_TCP_RX_BYTES = 0x0033,
	STATISTICS_INDEX_REG_TCP_TIMER_EXP = 0x0034,
	STATISTICS_INDEX_REG_TCP_RX_ACK = 0x0035,
	STATISTICS_INDEX_REG_TCP_TX_ACK = 0x0036,
	STATISTICS_INDEX_REG_TCP_RX_ERR = 0x0037,
	STATISTICS_INDEX_REG_TCP_RX_WIN_PROBE = 0x0038,
	STATISTICS_INDEX_REG_TCP_ECC_ERR_CORR = 0x003f,
};

enum {
	PORT_FATAL_ERROR_STATUS_OFB_RE_MAC0 = 0x00000001,
	PORT_FATAL_ERROR_STATUS_OFB_RE_MAC1 = 0x00000002,
	PORT_FATAL_ERROR_STATUS_OFB_WE = 0x00000004,
	PORT_FATAL_ERROR_STATUS_IFB_RE = 0x00000008,
	PORT_FATAL_ERROR_STATUS_IFB_WE_MAC0 = 0x00000010,
	PORT_FATAL_ERROR_STATUS_IFB_WE_MAC1 = 0x00000020,
	PORT_FATAL_ERROR_STATUS_ODE_RE = 0x00000040,
	PORT_FATAL_ERROR_STATUS_ODE_WE = 0x00000080,
	PORT_FATAL_ERROR_STATUS_IDE_RE = 0x00000100,
	PORT_FATAL_ERROR_STATUS_IDE_WE = 0x00000200,
	PORT_FATAL_ERROR_STATUS_SDE_RE = 0x00000400,
	PORT_FATAL_ERROR_STATUS_SDE_WE = 0x00000800,
	PORT_FATAL_ERROR_STATUS_BLE = 0x00001000,
	PORT_FATAL_ERROR_STATUS_SPE = 0x00002000,
	PORT_FATAL_ERROR_STATUS_EP0 = 0x00004000,
	PORT_FATAL_ERROR_STATUS_EP1 = 0x00008000,
	PORT_FATAL_ERROR_STATUS_ICE = 0x00010000,
	PORT_FATAL_ERROR_STATUS_ILE = 0x00020000,
	PORT_FATAL_ERROR_STATUS_OPE = 0x00040000,
	PORT_FATAL_ERROR_STATUS_TA = 0x00080000,
	PORT_FATAL_ERROR_STATUS_MA = 0x00100000,
	PORT_FATAL_ERROR_STATUS_SCE = 0x00200000,
	PORT_FATAL_ERROR_STATUS_RPE = 0x00400000,
	PORT_FATAL_ERROR_STATUS_MPE = 0x00800000,
	PORT_FATAL_ERROR_STATUS_OCE = 0x01000000,
};

/*
 *  port control and status page - page 0
 */

struct ql3xxx_port_registers {
	struct ql3xxx_common_registers CommonRegs;

	u32 ExternalHWConfig;
	u32 InternalChipConfig;
	u32 portControl;
	u32 portStatus;
	u32 macAddrIndirectPtrReg;
	u32 macAddrDataReg;
	u32 macMIIMgmtControlReg;
	u32 macMIIMgmtAddrReg;
	u32 macMIIMgmtDataReg;
	u32 macMIIStatusReg;
	u32 mac0ConfigReg;
	u32 mac0IpgIfgReg;
	u32 mac0HalfDuplexReg;
	u32 mac0MaxFrameLengthReg;
	u32 mac0PauseThresholdReg;
	u32 mac1ConfigReg;
	u32 mac1IpgIfgReg;
	u32 mac1HalfDuplexReg;
	u32 mac1MaxFrameLengthReg;
	u32 mac1PauseThresholdReg;
	u32 ipAddrIndexReg;
	u32 ipAddrDataReg;
	u32 ipReassemblyTimeout;
	u32 tcpMaxWindow;
	u32 currentTcpTimestamp[2];
	u32 internalRamRWAddrReg;
	u32 internalRamWDataReg;
	u32 reclaimedBufferAddrRegLow;
	u32 reclaimedBufferAddrRegHigh;
	u32 tcpConfiguration;
	u32 functionControl;
	u32 fpgaRevID;
	u32 localRamAddr;
	u32 localRamDataAutoIncr;
	u32 localRamDataNonIncr;
	u32 gpOutput;
	u32 gpInput;
	u32 probeMuxAddr;
	u32 probeMuxData;
	u32 statisticsIndexReg;
	u32 statisticsReadDataRegAutoIncr;
	u32 statisticsReadDataRegNoIncr;
	u32 PortFatalErrStatus;
};

/*
 * port host memory config page - page 1
 */
struct ql3xxx_host_memory_registers {
	struct ql3xxx_common_registers CommonRegs;

	u32 reserved[12];

	/* Network Request Queue */
	u32 reqConsumerIndex;
	u32 reqConsumerIndexAddrLow;
	u32 reqConsumerIndexAddrHigh;
	u32 reqBaseAddrLow;
	u32 reqBaseAddrHigh;
	u32 reqLength;

	/* Network Completion Queue */
	u32 rspProducerIndex;
	u32 rspProducerIndexAddrLow;
	u32 rspProducerIndexAddrHigh;
	u32 rspBaseAddrLow;
	u32 rspBaseAddrHigh;
	u32 rspLength;

	/* RX Large Buffer Queue */
	u32 rxLargeQConsumerIndex;
	u32 rxLargeQBaseAddrLow;
	u32 rxLargeQBaseAddrHigh;
	u32 rxLargeQLength;
	u32 rxLargeBufferLength;

	/* RX Small Buffer Queue */
	u32 rxSmallQConsumerIndex;
	u32 rxSmallQBaseAddrLow;
	u32 rxSmallQBaseAddrHigh;
	u32 rxSmallQLength;
	u32 rxSmallBufferLength;

};

/*
 *  port local RAM page - page 2
 */
struct ql3xxx_local_ram_registers {
	struct ql3xxx_common_registers CommonRegs;
	u32 bufletSize;
	u32 maxBufletCount;
	u32 currentBufletCount;
	u32 reserved;
	u32 freeBufletThresholdLow;
	u32 freeBufletThresholdHigh;
	u32 ipHashTableBase;
	u32 ipHashTableCount;
	u32 tcpHashTableBase;
	u32 tcpHashTableCount;
	u32 ncbBase;
	u32 maxNcbCount;
	u32 currentNcbCount;
	u32 drbBase;
	u32 maxDrbCount;
	u32 currentDrbCount;
};

/*
 * definitions for Semaphore bits in Semaphore/Serial NVRAM interface register
 */

#define LS_64BITS(x)    (u32)(0xffffffff & ((u64)x))
#define MS_64BITS(x)    (u32)(0xffffffff & (((u64)x)>>16>>16) )

/*
 * I/O register
 */

enum {
	CONTROL_REG = 0,
	STATUS_REG = 1,
	PHY_STAT_LINK_UP = 0x0004,
	PHY_CTRL_LOOPBACK = 0x4000,

	PETBI_CONTROL_REG = 0x00,
	PETBI_CTRL_ALL_PARAMS = 0x7140,
	PETBI_CTRL_SOFT_RESET = 0x8000,
	PETBI_CTRL_AUTO_NEG = 0x1000,
	PETBI_CTRL_RESTART_NEG = 0x0200,
	PETBI_CTRL_FULL_DUPLEX = 0x0100,
	PETBI_CTRL_SPEED_1000 = 0x0040,

	PETBI_STATUS_REG = 0x01,
	PETBI_STAT_NEG_DONE = 0x0020,
	PETBI_STAT_LINK_UP = 0x0004,

	PETBI_NEG_ADVER = 0x04,
	PETBI_NEG_PAUSE = 0x0080,
	PETBI_NEG_PAUSE_MASK = 0x0180,
	PETBI_NEG_DUPLEX = 0x0020,
	PETBI_NEG_DUPLEX_MASK = 0x0060,

	PETBI_NEG_PARTNER = 0x05,
	PETBI_NEG_ERROR_MASK = 0x3000,

	PETBI_EXPANSION_REG = 0x06,
	PETBI_EXP_PAGE_RX = 0x0002,

	PHY_GIG_CONTROL = 9,
	PHY_GIG_ENABLE_MAN = 0x1000,  /* Enable Master/Slave Manual Config*/
	PHY_GIG_SET_MASTER = 0x0800,  /* Set Master (slave if clear)*/
	PHY_GIG_ALL_PARAMS = 0x0300,
	PHY_GIG_ADV_1000F = 0x0200,
	PHY_GIG_ADV_1000H = 0x0100,

	PHY_NEG_ADVER = 4,
	PHY_NEG_ALL_PARAMS = 0x0fe0,
	PHY_NEG_ASY_PAUSE =  0x0800,
	PHY_NEG_SYM_PAUSE =  0x0400,
	PHY_NEG_ADV_SPEED =  0x01e0,
	PHY_NEG_ADV_100F =   0x0100,
	PHY_NEG_ADV_100H =   0x0080,
	PHY_NEG_ADV_10F =    0x0040,
	PHY_NEG_ADV_10H =    0x0020,

	PETBI_TBI_CTRL = 0x11,
	PETBI_TBI_RESET = 0x8000,
	PETBI_TBI_AUTO_SENSE = 0x0100,
	PETBI_TBI_SERDES_MODE = 0x0010,
	PETBI_TBI_SERDES_WRAP = 0x0002,

	AUX_CONTROL_STATUS = 0x1c,
	PHY_AUX_NEG_DONE = 0x8000,
	PHY_NEG_PARTNER = 5,
	PHY_AUX_DUPLEX_STAT = 0x0020,
	PHY_AUX_SPEED_STAT = 0x0018,
	PHY_AUX_NO_HW_STRAP = 0x0004,
	PHY_AUX_RESET_STICK = 0x0002,
	PHY_NEG_PAUSE = 0x0400,
	PHY_CTRL_SOFT_RESET = 0x8000,
	PHY_CTRL_AUTO_NEG = 0x1000,
	PHY_CTRL_RESTART_NEG = 0x0200,
};
enum {
/* AM29LV Flash definitions	*/
	FM93C56A_START = 0x1,
/* Commands */
	FM93C56A_READ = 0x2,
	FM93C56A_WEN = 0x0,
	FM93C56A_WRITE = 0x1,
	FM93C56A_WRITE_ALL = 0x0,
	FM93C56A_WDS = 0x0,
	FM93C56A_ERASE = 0x3,
	FM93C56A_ERASE_ALL = 0x0,
/* Command Extentions */
	FM93C56A_WEN_EXT = 0x3,
	FM93C56A_WRITE_ALL_EXT = 0x1,
	FM93C56A_WDS_EXT = 0x0,
	FM93C56A_ERASE_ALL_EXT = 0x2,
/* Special Bits */
	FM93C56A_READ_DUMMY_BITS = 1,
	FM93C56A_READY = 0,
	FM93C56A_BUSY = 1,
	FM93C56A_CMD_BITS = 2,
/* AM29LV Flash definitions	*/
	FM93C56A_SIZE_8 = 0x100,
	FM93C56A_SIZE_16 = 0x80,
	FM93C66A_SIZE_8 = 0x200,
	FM93C66A_SIZE_16 = 0x100,
	FM93C86A_SIZE_16 = 0x400,
/* Address Bits */
	FM93C56A_NO_ADDR_BITS_16 = 8,
	FM93C56A_NO_ADDR_BITS_8 = 9,
	FM93C86A_NO_ADDR_BITS_16 = 10,
/* Data Bits */
	FM93C56A_DATA_BITS_16 = 16,
	FM93C56A_DATA_BITS_8 = 8,
};
enum {
/* Auburn Bits */
	    AUBURN_EEPROM_DI = 0x8,
	AUBURN_EEPROM_DI_0 = 0x0,
	AUBURN_EEPROM_DI_1 = 0x8,
	AUBURN_EEPROM_DO = 0x4,
	AUBURN_EEPROM_DO_0 = 0x0,
	AUBURN_EEPROM_DO_1 = 0x4,
	AUBURN_EEPROM_CS = 0x2,
	AUBURN_EEPROM_CS_0 = 0x0,
	AUBURN_EEPROM_CS_1 = 0x2,
	AUBURN_EEPROM_CLK_RISE = 0x1,
	AUBURN_EEPROM_CLK_FALL = 0x0,
};
enum {EEPROM_SIZE = FM93C86A_SIZE_16,
	EEPROM_NO_ADDR_BITS = FM93C86A_NO_ADDR_BITS_16,
	EEPROM_NO_DATA_BITS = FM93C56A_DATA_BITS_16,
};

/*
 *  MAC Config data structure
 */
    struct eeprom_port_cfg {
	u16 etherMtu_mac;
	u16 pauseThreshold_mac;
	u16 resumeThreshold_mac;
	u16 portConfiguration;
#define PORT_CONFIG_DEFAULT                 0xf700
#define PORT_CONFIG_AUTO_NEG_ENABLED        0x8000
#define PORT_CONFIG_SYM_PAUSE_ENABLED       0x4000
#define PORT_CONFIG_FULL_DUPLEX_ENABLED     0x2000
#define PORT_CONFIG_HALF_DUPLEX_ENABLED     0x1000
#define PORT_CONFIG_1000MB_SPEED            0x0400
#define PORT_CONFIG_100MB_SPEED             0x0200
#define PORT_CONFIG_10MB_SPEED              0x0100
#define PORT_CONFIG_LINK_SPEED_MASK         0x0F00
	u16 reserved[12];

};

/*
 * BIOS data structure
 */
struct eeprom_bios_cfg {
	u16 SpinDlyEn:1, disBios:1, EnMemMap:1, EnSelectBoot:1, Reserved:12;

	u8 bootID0:7, boodID0Valid:1;
	u8 bootLun0[8];

	u8 bootID1:7, boodID1Valid:1;
	u8 bootLun1[8];

	u16 MaxLunsTrgt;
	u8 reserved[10];
};

/*
 *  Function Specific Data structure
 */
struct eeprom_function_cfg {
	u8 reserved[30];
	u16 macAddress[3];
	u16 macAddressSecondary[3];

	u16 subsysVendorId;
	u16 subsysDeviceId;
};

/*
 *  EEPROM format
 */
struct eeprom_data {
	u8 asicId[4];
	u16 version_and_numPorts; /* together to avoid endianness crap */
	u16 boardId;

#define EEPROM_BOARDID_STR_SIZE   16
#define EEPROM_SERIAL_NUM_SIZE    16

	u8 boardIdStr[16];
	u8 serialNumber[16];
	u16 extHwConfig;
	struct eeprom_port_cfg macCfg_port0;
	struct eeprom_port_cfg macCfg_port1;
	u16 bufletSize;
	u16 bufletCount;
	u16 tcpWindowThreshold50;
	u16 tcpWindowThreshold25;
	u16 tcpWindowThreshold0;
	u16 ipHashTableBaseHi;
	u16 ipHashTableBaseLo;
	u16 ipHashTableSize;
	u16 tcpHashTableBaseHi;
	u16 tcpHashTableBaseLo;
	u16 tcpHashTableSize;
	u16 ncbTableBaseHi;
	u16 ncbTableBaseLo;
	u16 ncbTableSize;
	u16 drbTableBaseHi;
	u16 drbTableBaseLo;
	u16 drbTableSize;
	u16 reserved_142[4];
	u16 ipReassemblyTimeout;
	u16 tcpMaxWindowSize;
	u16 ipSecurity;
#define IPSEC_CONFIG_PRESENT 0x0001
	u8 reserved_156[294];
	u16 qDebug[8];
	struct eeprom_function_cfg funcCfg_fn0;
	u16 reserved_510;
	u8 oemSpace[432];
	struct eeprom_bios_cfg biosCfg_fn1;
	struct eeprom_function_cfg funcCfg_fn1;
	u16 reserved_1022;
	u8 reserved_1024[464];
	struct eeprom_function_cfg funcCfg_fn2;
	u16 reserved_1534;
	u8 reserved_1536[432];
	struct eeprom_bios_cfg biosCfg_fn3;
	struct eeprom_function_cfg funcCfg_fn3;
	u16 checksum;
};

/*
 * General definitions...
 */

/*
 * Below are a number compiler switches for controlling driver behavior.
 * Some are not supported under certain conditions and are notated as such.
 */

#define QL3XXX_VENDOR_ID    0x1077
#define QL3022_DEVICE_ID    0x3022
#define QL3032_DEVICE_ID    0x3032

/* MTU & Frame Size stuff */
#define NORMAL_MTU_SIZE 		ETH_DATA_LEN
#define JUMBO_MTU_SIZE 			9000
#define VLAN_ID_LEN			    2

/* Request Queue Related Definitions */
#define NUM_REQ_Q_ENTRIES   256	/* so that 64 * 64  = 4096 (1 page) */

/* Response Queue Related Definitions */
#define NUM_RSP_Q_ENTRIES   256	/* so that 256 * 16  = 4096 (1 page) */

/* Transmit and Receive Buffers */
#define NUM_LBUFQ_ENTRIES   	128
#define JUMBO_NUM_LBUFQ_ENTRIES 32
#define NUM_SBUFQ_ENTRIES   	64
#define QL_SMALL_BUFFER_SIZE    32
#define QL_ADDR_ELE_PER_BUFQ_ENTRY \
(sizeof(struct lrg_buf_q_entry) / sizeof(struct bufq_addr_element))
    /* Each send has at least control block.  This is how many we keep. */
#define NUM_SMALL_BUFFERS     	NUM_SBUFQ_ENTRIES * QL_ADDR_ELE_PER_BUFQ_ENTRY

#define QL_HEADER_SPACE 32	/* make header space at top of skb. */
/*
 * Large & Small Buffers for Receives
 */
struct lrg_buf_q_entry {

	__le32 addr0_lower;
#define IAL_LAST_ENTRY 0x00000001
#define IAL_CONT_ENTRY 0x00000002
#define IAL_FLAG_MASK  0x00000003
	__le32 addr0_upper;
	__le32 addr1_lower;
	__le32 addr1_upper;
	__le32 addr2_lower;
	__le32 addr2_upper;
	__le32 addr3_lower;
	__le32 addr3_upper;
	__le32 addr4_lower;
	__le32 addr4_upper;
	__le32 addr5_lower;
	__le32 addr5_upper;
	__le32 addr6_lower;
	__le32 addr6_upper;
	__le32 addr7_lower;
	__le32 addr7_upper;

};

struct bufq_addr_element {
	__le32 addr_low;
	__le32 addr_high;
};

#define QL_NO_RESET			0
#define QL_DO_RESET			1

enum link_state_t {
	LS_UNKNOWN = 0,
	LS_DOWN,
	LS_DEGRADE,
	LS_RECOVER,
	LS_UP,
};

struct ql_rcv_buf_cb {
	struct ql_rcv_buf_cb *next;
	struct sk_buff *skb;
	DEFINE_DMA_UNMAP_ADDR(mapaddr);
	DEFINE_DMA_UNMAP_LEN(maplen);
	__le32 buf_phy_addr_low;
	__le32 buf_phy_addr_high;
	int index;
};

/*
 * Original IOCB has 3 sg entries:
 * first points to skb-data area
 * second points to first frag
 * third points to next oal.
 * OAL has 5 entries:
 * 1 thru 4 point to frags
 * fifth points to next oal.
 */
#define MAX_OAL_CNT ((MAX_SKB_FRAGS-1)/4 + 1)

struct oal_entry {
	__le32 dma_lo;
	__le32 dma_hi;
	__le32 len;
#define OAL_LAST_ENTRY   0x80000000	/* Last valid buffer in list. */
#define OAL_CONT_ENTRY   0x40000000	/* points to an OAL. (continuation) */
};

struct oal {
	struct oal_entry oal_entry[5];
};

struct map_list {
	DEFINE_DMA_UNMAP_ADDR(mapaddr);
	DEFINE_DMA_UNMAP_LEN(maplen);
};

struct ql_tx_buf_cb {
	struct sk_buff *skb;
	struct ob_mac_iocb_req *queue_entry ;
	int seg_count;
	struct oal *oal;
	struct map_list map[MAX_SKB_FRAGS+1];
};

/* definitions for type field */
#define QL_BUF_TYPE_MACIOCB 0x01
#define QL_BUF_TYPE_IPIOCB  0x02
#define QL_BUF_TYPE_TCPIOCB 0x03

/* qdev->flags definitions. */
enum { QL_RESET_DONE = 1,	/* Reset finished. */
	QL_RESET_ACTIVE = 2,	/* Waiting for reset to finish. */
	QL_RESET_START = 3,	/* Please reset the chip. */
	QL_RESET_PER_SCSI = 4,	/* SCSI driver requests reset. */
	QL_TX_TIMEOUT = 5,	/* Timeout in progress. */
	QL_LINK_MASTER = 6,	/* This driver controls the link. */
	QL_ADAPTER_UP = 7,	/* Adapter has been brought up. */
	QL_THREAD_UP = 8,	/* This flag is available. */
	QL_LINK_UP = 9,	/* Link Status. */
	QL_ALLOC_REQ_RSP_Q_DONE = 10,
	QL_ALLOC_BUFQS_DONE = 11,
	QL_ALLOC_SMALL_BUF_DONE = 12,
	QL_LINK_OPTICAL = 13,
	QL_MSI_ENABLED = 14,
};

/*
 * ql3_adapter - The main Adapter structure definition.
 * This structure has all fields relevant to the hardware.
 */

struct ql3_adapter {
	u32 reserved_00;
	unsigned long flags;

	/* PCI Configuration information for this device */
	struct pci_dev *pdev;
	struct net_device *ndev;	/* Parent NET device */

	struct napi_struct napi;

	/* Hardware information */
	u8 chip_rev_id;
	u8 pci_slot;
	u8 pci_width;
	u8 pci_x;
	u32 msi;
	int index;
	struct timer_list adapter_timer;	/* timer used for various functions */

	spinlock_t adapter_lock;
	spinlock_t hw_lock;

	/* PCI Bus Relative Register Addresses */
	u8 __iomem *mmap_virt_base;	/* stores return value from ioremap() */
	struct ql3xxx_port_registers __iomem *mem_map_registers;
	u32 current_page;	/* tracks current register page */

	u32 msg_enable;
	u8 reserved_01[2];
	u8 reserved_02[2];

	/* Page for Shadow Registers */
	void *shadow_reg_virt_addr;
	dma_addr_t shadow_reg_phy_addr;

	/* Net Request Queue */
	u32 req_q_size;
	u32 reserved_03;
	struct ob_mac_iocb_req *req_q_virt_addr;
	dma_addr_t req_q_phy_addr;
	u16 req_producer_index;
	u16 reserved_04;
	u16 *preq_consumer_index;
	u32 req_consumer_index_phy_addr_high;
	u32 req_consumer_index_phy_addr_low;
	atomic_t tx_count;
	struct ql_tx_buf_cb tx_buf[NUM_REQ_Q_ENTRIES];

	/* Net Response Queue */
	u32 rsp_q_size;
	u32 eeprom_cmd_data;
	struct net_rsp_iocb *rsp_q_virt_addr;
	dma_addr_t rsp_q_phy_addr;
	struct net_rsp_iocb *rsp_current;
	u16 rsp_consumer_index;
	u16 reserved_06;
	volatile __le32 *prsp_producer_index;
	u32 rsp_producer_index_phy_addr_high;
	u32 rsp_producer_index_phy_addr_low;

	/* Large Buffer Queue */
	u32 lrg_buf_q_alloc_size;
	u32 lrg_buf_q_size;
	void *lrg_buf_q_alloc_virt_addr;
	void *lrg_buf_q_virt_addr;
	dma_addr_t lrg_buf_q_alloc_phy_addr;
	dma_addr_t lrg_buf_q_phy_addr;
	u32 lrg_buf_q_producer_index;
	u32 lrg_buf_release_cnt;
	struct bufq_addr_element *lrg_buf_next_free;
	u32 num_large_buffers;
	u32 num_lbufq_entries;

	/* Large (Receive) Buffers */
	struct ql_rcv_buf_cb *lrg_buf;
	struct ql_rcv_buf_cb *lrg_buf_free_head;
	struct ql_rcv_buf_cb *lrg_buf_free_tail;
	u32 lrg_buf_free_count;
	u32 lrg_buffer_len;
	u32 lrg_buf_index;
	u32 lrg_buf_skb_check;

	/* Small Buffer Queue */
	u32 small_buf_q_alloc_size;
	u32 small_buf_q_size;
	u32 small_buf_q_producer_index;
	void *small_buf_q_alloc_virt_addr;
	void *small_buf_q_virt_addr;
	dma_addr_t small_buf_q_alloc_phy_addr;
	dma_addr_t small_buf_q_phy_addr;
	u32 small_buf_index;

	/* Small (Receive) Buffers */
	void *small_buf_virt_addr;
	dma_addr_t small_buf_phy_addr;
	u32 small_buf_phy_addr_low;
	u32 small_buf_phy_addr_high;
	u32 small_buf_release_cnt;
	u32 small_buf_total_size;

	struct eeprom_data nvram_data;
	u32 port_link_state;

	/* 4022 specific */
	u32 mac_index;		/* Driver's MAC number can be 0 or 1 for first and second networking functions respectively */
	u32 PHYAddr;		/* Address of PHY 0x1e00 Port 0 and 0x1f00 Port 1 */
	u32 mac_ob_opcode;	/* Opcode to use on mac transmission */
	u32 mb_bit_mask;	/* MA Bits mask to use on transmission */
	u32 numPorts;
	struct workqueue_struct *workqueue;
	struct delayed_work reset_work;
	struct delayed_work tx_timeout_work;
	struct delayed_work link_state_work;
	u32 max_frame_size;
	u32 device_id;
	u16 phyType;
};

#endif				/* _QLA3XXX_H_ */
