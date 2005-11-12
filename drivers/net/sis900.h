/* sis900.h Definitions for SiS ethernet controllers including 7014/7016 and 900 
 * Copyright 1999 Silicon Integrated System Corporation
 * References:
 *   SiS 7016 Fast Ethernet PCI Bus 10/100 Mbps LAN Controller with OnNow Support,
 *	preliminary Rev. 1.0 Jan. 14, 1998
 *   SiS 900 Fast Ethernet PCI Bus 10/100 Mbps LAN Single Chip with OnNow Support,
 *	preliminary Rev. 1.0 Nov. 10, 1998
 *   SiS 7014 Single Chip 100BASE-TX/10BASE-T Physical Layer Solution,
 *	preliminary Rev. 1.0 Jan. 18, 1998
 *   http://www.sis.com.tw/support/databook.htm
 */

/*
 * SiS 7016 and SiS 900 ethernet controller registers
 */

/* The I/O extent, SiS 900 needs 256 bytes of io address */
#define SIS900_TOTAL_SIZE 0x100

/* Symbolic offsets to registers. */
enum sis900_registers {
	cr=0x0,                 //Command Register
	cfg=0x4,                //Configuration Register
	mear=0x8,               //EEPROM Access Register
	ptscr=0xc,              //PCI Test Control Register
	isr=0x10,               //Interrupt Status Register
	imr=0x14,               //Interrupt Mask Register
	ier=0x18,               //Interrupt Enable Register
	epar=0x18,              //Enhanced PHY Access Register
	txdp=0x20,              //Transmit Descriptor Pointer Register
        txcfg=0x24,             //Transmit Configuration Register
        rxdp=0x30,              //Receive Descriptor Pointer Register
        rxcfg=0x34,             //Receive Configuration Register
        flctrl=0x38,            //Flow Control Register
        rxlen=0x3c,             //Receive Packet Length Register
        cfgpmcsr=0x44,          //Configuration Power Management Control/Status Register
        rfcr=0x48,              //Receive Filter Control Register
        rfdr=0x4C,              //Receive Filter Data Register
        pmctrl=0xB0,            //Power Management Control Register
        pmer=0xB4               //Power Management Wake-up Event Register
};

/* Symbolic names for bits in various registers */
enum sis900_command_register_bits {
	RELOAD  = 0x00000400, ACCESSMODE = 0x00000200,/* ET */
	RESET   = 0x00000100, SWI = 0x00000080, RxRESET = 0x00000020,
	TxRESET = 0x00000010, RxDIS = 0x00000008, RxENA = 0x00000004,
	TxDIS   = 0x00000002, TxENA = 0x00000001
};

enum sis900_configuration_register_bits {
	DESCRFMT = 0x00000100 /* 7016 specific */, REQALG = 0x00000080,
	SB    = 0x00000040, POW = 0x00000020, EXD = 0x00000010, 
	PESEL = 0x00000008, LPM = 0x00000004, BEM = 0x00000001,
	/* 635 & 900B Specific */
	RND_CNT = 0x00000400, FAIR_BACKOFF = 0x00000200,
	EDB_MASTER_EN = 0x00002000
};

enum sis900_eeprom_access_reigster_bits {
	MDC  = 0x00000040, MDDIR = 0x00000020, MDIO = 0x00000010, /* 7016 specific */ 
	EECS = 0x00000008, EECLK = 0x00000004, EEDO = 0x00000002,
	EEDI = 0x00000001
};

enum sis900_interrupt_register_bits {
	WKEVT  = 0x10000000, TxPAUSEEND = 0x08000000, TxPAUSE = 0x04000000,
	TxRCMP = 0x02000000, RxRCMP = 0x01000000, DPERR = 0x00800000,
	SSERR  = 0x00400000, RMABT  = 0x00200000, RTABT = 0x00100000,
	RxSOVR = 0x00010000, HIBERR = 0x00008000, SWINT = 0x00001000,
	MIBINT = 0x00000800, TxURN  = 0x00000400, TxIDLE  = 0x00000200,
	TxERR  = 0x00000100, TxDESC = 0x00000080, TxOK  = 0x00000040,
	RxORN  = 0x00000020, RxIDLE = 0x00000010, RxEARLY = 0x00000008,
	RxERR  = 0x00000004, RxDESC = 0x00000002, RxOK  = 0x00000001
};

enum sis900_interrupt_enable_reigster_bits {
	IE = 0x00000001
};

/* maximum dma burst for transmission and receive */
#define MAX_DMA_RANGE	7	/* actually 0 means MAXIMUM !! */
#define TxMXDMA_shift   	20
#define RxMXDMA_shift    20

enum sis900_tx_rx_dma{
	DMA_BURST_512 = 0,	DMA_BURST_64 = 5
};

/* transmit FIFO thresholds */
#define TX_FILL_THRESH   16	/* 1/4 FIFO size */
#define TxFILLT_shift   	8
#define TxDRNT_shift    	0
#define TxDRNT_100      	48	/* 3/4 FIFO size */
#define TxDRNT_10		16 	/* 1/2 FIFO size */

enum sis900_transmit_config_register_bits {
	TxCSI = 0x80000000, TxHBI = 0x40000000, TxMLB = 0x20000000,
	TxATP = 0x10000000, TxIFG = 0x0C000000, TxFILLT = 0x00003F00,
	TxDRNT = 0x0000003F
};

/* recevie FIFO thresholds */
#define RxDRNT_shift     1
#define RxDRNT_100	16	/* 1/2 FIFO size */
#define RxDRNT_10		24 	/* 3/4 FIFO size */

enum sis900_reveive_config_register_bits {
	RxAEP  = 0x80000000, RxARP = 0x40000000, RxATX = 0x10000000,
	RxAJAB = 0x08000000, RxDRNT = 0x0000007F
};

#define RFAA_shift      28
#define RFADDR_shift    16

enum sis900_receive_filter_control_register_bits {
	RFEN  = 0x80000000, RFAAB = 0x40000000, RFAAM = 0x20000000,
	RFAAP = 0x10000000, RFPromiscuous = (RFAAB|RFAAM|RFAAP)
};

enum sis900_reveive_filter_data_mask {
	RFDAT =  0x0000FFFF
};

/* EEPROM Addresses */
enum sis900_eeprom_address {
	EEPROMSignature = 0x00, EEPROMVendorID = 0x02, EEPROMDeviceID = 0x03,
	EEPROMMACAddr   = 0x08, EEPROMChecksum = 0x0b
};

/* The EEPROM commands include the alway-set leading bit. Refer to NM93Cxx datasheet */
enum sis900_eeprom_command {
	EEread     = 0x0180, EEwrite    = 0x0140, EEerase = 0x01C0, 
	EEwriteEnable = 0x0130, EEwriteDisable = 0x0100,
	EEeraseAll = 0x0120, EEwriteAll = 0x0110, 
	EEaddrMask = 0x013F, EEcmdShift = 16
};

/* For SiS962 or SiS963, request the eeprom software access */
enum sis96x_eeprom_command {
	EEREQ = 0x00000400, EEDONE = 0x00000200, EEGNT = 0x00000100
};

/* PCI Registers */
enum sis900_pci_registers {
	CFGPMC 	 = 0x40,
	CFGPMCSR = 0x44
};

/* Power management capabilities bits */
enum sis900_cfgpmc_register_bits {
	PMVER	= 0x00070000, 
	DSI	= 0x00100000,
	PMESP	= 0xf8000000
};

enum sis900_pmesp_bits {
	PME_D0 = 0x1,
	PME_D1 = 0x2,
	PME_D2 = 0x4,
	PME_D3H = 0x8,
	PME_D3C = 0x10
};

/* Power management control/status bits */
enum sis900_cfgpmcsr_register_bits {
	PMESTS = 0x00004000,
	PME_EN = 0x00000100, // Power management enable
	PWR_STA = 0x00000003 // Current power state
};

/* Wake-on-LAN support. */
enum sis900_power_management_control_register_bits {
	LINKLOSS  = 0x00000001,
	LINKON    = 0x00000002,
	MAGICPKT  = 0x00000400,
	ALGORITHM = 0x00000800,
	FRM1EN    = 0x00100000,
	FRM2EN    = 0x00200000,
	FRM3EN    = 0x00400000,
	FRM1ACS   = 0x01000000,
	FRM2ACS   = 0x02000000,
	FRM3ACS   = 0x04000000,
	WAKEALL   = 0x40000000,
	GATECLK   = 0x80000000
};

/* Management Data I/O (mdio) frame */
#define MIIread         0x6000
#define MIIwrite        0x5002
#define MIIpmdShift     7
#define MIIregShift     2
#define MIIcmdLen       16
#define MIIcmdShift     16

/* Buffer Descriptor Status*/
enum sis900_buffer_status {
	OWN    = 0x80000000, MORE   = 0x40000000, INTR = 0x20000000,
	SUPCRC = 0x10000000, INCCRC = 0x10000000,
	OK     = 0x08000000, DSIZE  = 0x00000FFF
};
/* Status for TX Buffers */
enum sis900_tx_buffer_status {
	ABORT   = 0x04000000, UNDERRUN = 0x02000000, NOCARRIER = 0x01000000,
	DEFERD  = 0x00800000, EXCDEFER = 0x00400000, OWCOLL    = 0x00200000,
	EXCCOLL = 0x00100000, COLCNT   = 0x000F0000
};

enum sis900_rx_bufer_status {
	OVERRUN = 0x02000000, DEST = 0x00800000,     BCAST = 0x01800000,
	MCAST   = 0x01000000, UNIMATCH = 0x00800000, TOOLONG = 0x00400000,
	RUNT    = 0x00200000, RXISERR  = 0x00100000, CRCERR  = 0x00080000,
	FAERR   = 0x00040000, LOOPBK   = 0x00020000, RXCOL   = 0x00010000
};

/* MII register offsets */
enum mii_registers {
	MII_CONTROL = 0x0000, MII_STATUS = 0x0001, MII_PHY_ID0 = 0x0002,
	MII_PHY_ID1 = 0x0003, MII_ANADV  = 0x0004, MII_ANLPAR  = 0x0005,
	MII_ANEXT   = 0x0006
};

/* mii registers specific to SiS 900 */
enum sis_mii_registers {
	MII_CONFIG1 = 0x0010, MII_CONFIG2 = 0x0011, MII_STSOUT = 0x0012,
	MII_MASK    = 0x0013, MII_RESV    = 0x0014
};

/* mii registers specific to ICS 1893 */
enum ics_mii_registers {
	MII_EXTCTRL  = 0x0010, MII_QPDSTS = 0x0011, MII_10BTOP = 0x0012,
	MII_EXTCTRL2 = 0x0013
};

/* mii registers specific to AMD 79C901 */
enum amd_mii_registers {
	MII_STATUS_SUMMARY = 0x0018
};

/* MII Control register bit definitions. */
enum mii_control_register_bits {
	MII_CNTL_FDX     = 0x0100, MII_CNTL_RST_AUTO = 0x0200, 
	MII_CNTL_ISOLATE = 0x0400, MII_CNTL_PWRDWN   = 0x0800,
	MII_CNTL_AUTO    = 0x1000, MII_CNTL_SPEED    = 0x2000,
	MII_CNTL_LPBK    = 0x4000, MII_CNTL_RESET    = 0x8000
};

/* MII Status register bit  */
enum mii_status_register_bits {
	MII_STAT_EXT    = 0x0001, MII_STAT_JAB        = 0x0002, 
	MII_STAT_LINK   = 0x0004, MII_STAT_CAN_AUTO   = 0x0008, 
	MII_STAT_FAULT  = 0x0010, MII_STAT_AUTO_DONE  = 0x0020,
	MII_STAT_CAN_T  = 0x0800, MII_STAT_CAN_T_FDX  = 0x1000,
	MII_STAT_CAN_TX = 0x2000, MII_STAT_CAN_TX_FDX = 0x4000,
	MII_STAT_CAN_T4 = 0x8000
};

#define		MII_ID1_OUI_LO		0xFC00	/* low bits of OUI mask */
#define		MII_ID1_MODEL		0x03F0	/* model number */
#define		MII_ID1_REV		0x000F	/* model number */

/* MII NWAY Register Bits ...
   valid for the ANAR (Auto-Negotiation Advertisement) and
   ANLPAR (Auto-Negotiation Link Partner) registers */
enum mii_nway_register_bits {
	MII_NWAY_NODE_SEL = 0x001f, MII_NWAY_CSMA_CD = 0x0001,
	MII_NWAY_T	  = 0x0020, MII_NWAY_T_FDX   = 0x0040,
	MII_NWAY_TX       = 0x0080, MII_NWAY_TX_FDX  = 0x0100,
	MII_NWAY_T4       = 0x0200, MII_NWAY_PAUSE   = 0x0400,
	MII_NWAY_RF       = 0x2000, MII_NWAY_ACK     = 0x4000,
	MII_NWAY_NP       = 0x8000
};

enum mii_stsout_register_bits {
	MII_STSOUT_LINK_FAIL = 0x4000,
	MII_STSOUT_SPD       = 0x0080, MII_STSOUT_DPLX = 0x0040
};

enum mii_stsics_register_bits {
	MII_STSICS_SPD  = 0x8000, MII_STSICS_DPLX = 0x4000,
	MII_STSICS_LINKSTS = 0x0001
};

enum mii_stssum_register_bits {
	MII_STSSUM_LINK = 0x0008, MII_STSSUM_DPLX = 0x0004,
	MII_STSSUM_AUTO = 0x0002, MII_STSSUM_SPD  = 0x0001
};

enum sis900_revision_id {
	SIS630A_900_REV = 0x80,		SIS630E_900_REV = 0x81,
	SIS630S_900_REV = 0x82,		SIS630EA1_900_REV = 0x83,
	SIS630ET_900_REV = 0x84,	SIS635A_900_REV = 0x90,
	SIS96x_900_REV = 0X91,		SIS900B_900_REV = 0x03
};

enum sis630_revision_id {
	SIS630A0    = 0x00, SIS630A1      = 0x01,
	SIS630B0    = 0x10, SIS630B1      = 0x11
};

#define FDX_CAPABLE_DUPLEX_UNKNOWN      0
#define FDX_CAPABLE_HALF_SELECTED       1
#define FDX_CAPABLE_FULL_SELECTED       2

#define HW_SPEED_UNCONFIG		0
#define HW_SPEED_HOME		1
#define HW_SPEED_10_MBPS        	10
#define HW_SPEED_100_MBPS       	100
#define HW_SPEED_DEFAULT        	(HW_SPEED_100_MBPS)

#define CRC_SIZE                4
#define MAC_HEADER_SIZE         14

#define TX_BUF_SIZE     1536
#define RX_BUF_SIZE     1536

#define NUM_TX_DESC     16      	/* Number of Tx descriptor registers. */
#define NUM_RX_DESC     16       	/* Number of Rx descriptor registers. */
#define TX_TOTAL_SIZE	NUM_TX_DESC*sizeof(BufferDesc)
#define RX_TOTAL_SIZE	NUM_RX_DESC*sizeof(BufferDesc)

/* PCI stuff, should be move to pci.h */
#define SIS630_VENDOR_ID        0x1039
#define SIS630_DEVICE_ID        0x0630
