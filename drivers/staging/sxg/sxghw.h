/*
 * Copyright © 1997-2007 Alacritech, Inc. All rights reserved
 *
 * $Id: sxghw.h,v 1.2 2008/07/24 17:24:23 chris Exp $
 *
 * sxghw.h:
 *
 * This file contains structures and definitions for the
 * Alacritech Sahara hardware
 */


/*******************************************************************************
 * Configuration space
 *******************************************************************************/
//  PCI Vendor ID
#define SXG_VENDOR_ID			0x139A	// Alacritech's Vendor ID

//  PCI Device ID
#define SXG_DEVICE_ID			0x0009	// Sahara Device ID

//
// Subsystem IDs.
//
// The subsystem ID value is broken into bit fields as follows:
//		Bits [15:12] - Function
//		Bits [11:8]  - OEM and/or operating system.
//		Bits [7:0]   - Base SID.
//
// SSID field (bit) masks
#define SSID_BASE_MASK					0x00FF	// Base subsystem ID mask
#define SSID_OEM_MASK					0x0F00	// Subsystem OEM mask
#define SSID_FUNC_MASK					0xF000	// Subsystem function mask

// Base SSID's
#define SSID_SAHARA_PROTO				0x0018	// 100022 Sahara prototype (XenPak) board
#define SSID_SAHARA_FIBER				0x0019	// 100023 Sahara 1-port fiber board
#define SSID_SAHARA_COPPER				0x001A	// 100024 Sahara 1-port copper board

// Useful SSID macros
#define	SSID_BASE(ssid)			((ssid) & SSID_BASE_MASK)		// isolate base SSID bits
#define	SSID_OEM(ssid)			((ssid) & SSID_OEM_MASK)		// isolate SSID OEM bits
#define	SSID_FUNC(ssid)			((ssid) & SSID_FUNC_MASK)		// isolate SSID function bits

/*******************************************************************************
 * HW Register Space
 *******************************************************************************/
#define SXG_HWREG_MEMSIZE	0x4000		// 16k

#pragma pack(push, 1)
typedef struct _SXG_HW_REGS {
	u32		Reset;				// Write 0xdead to invoke soft reset
	u32		Pad1;				// No register defined at offset 4
	u32		InterruptMask0;		// Deassert legacy interrupt on function 0
	u32		InterruptMask1;		// Deassert legacy interrupt on function 1
	u32		UcodeDataLow;		// Store microcode instruction bits 31-0
	u32		UcodeDataMiddle;	// Store microcode instruction bits 63-32
	u32		UcodeDataHigh;		// Store microcode instruction bits 95-64
	u32		UcodeAddr;			// Store microcode address - See flags below
	u32		PadTo0x80[24];		// Pad to Xcv configuration registers
	u32		MacConfig0;			// 0x80 - AXGMAC Configuration Register 0
	u32		MacConfig1;			// 0x84 - AXGMAC Configuration Register 1
	u32		MacConfig2;			// 0x88 - AXGMAC Configuration Register 2
	u32		MacConfig3;			// 0x8C - AXGMAC Configuration Register 3
	u32		MacAddressLow;		// 0x90 - AXGMAC MAC Station Address - octets 1-4
	u32		MacAddressHigh;		// 0x94 - AXGMAC MAC Station Address - octets 5-6
	u32		MacReserved1[2];	// 0x98 - AXGMAC Reserved
	u32		MacMaxFrameLen;		// 0xA0 - AXGMAC Maximum Frame Length
	u32		MacReserved2[2];	// 0xA4 - AXGMAC Reserved
	u32		MacRevision;		// 0xAC - AXGMAC Revision Level Register
	u32		MacReserved3[4];	// 0xB0 - AXGMAC Reserved
	u32		MacAmiimCmd;		// 0xC0 - AXGMAC AMIIM Command Register
	u32		MacAmiimField;		// 0xC4 - AXGMAC AMIIM Field Register
	u32		MacAmiimConfig;		// 0xC8 - AXGMAC AMIIM Configuration Register
	u32		MacAmiimLink;		// 0xCC - AXGMAC AMIIM Link Fail Vector Register
	u32		MacAmiimIndicator;	// 0xD0 - AXGMAC AMIIM Indicator Registor
	u32		PadTo0x100[11];		// 0xD4 - 0x100 - Pad
	u32		XmtConfig;			// 0x100 - Transmit Configuration Register
	u32		RcvConfig;			// 0x104 - Receive Configuration Register 1
	u32		LinkAddress0Low;	// 0x108 - Link address 0
	u32		LinkAddress0High;	// 0x10C - Link address 0
	u32		LinkAddress1Low;	// 0x110 - Link address 1
	u32		LinkAddress1High;	// 0x114 - Link address 1
	u32		LinkAddress2Low;	// 0x118 - Link address 2
	u32		LinkAddress2High;	// 0x11C - Link address 2
	u32		LinkAddress3Low;	// 0x120 - Link address 3
	u32		LinkAddress3High;	// 0x124 - Link address 3
	u32		ToeplitzKey[10];	// 0x128 - 0x150 - Toeplitz key
	u32		SocketKey[10];		// 0x150 - 0x178 - Socket Key
	u32		LinkStatus;			// 0x178 - Link status
	u32		ClearStats;			// 0x17C - Clear Stats
	u32		XmtErrorsLow;		// 0x180 - Transmit stats - errors
	u32		XmtErrorsHigh;		// 0x184 - Transmit stats - errors
	u32		XmtFramesLow;		// 0x188 - Transmit stats - frame count
	u32		XmtFramesHigh;		// 0x18C - Transmit stats - frame count
	u32		XmtBytesLow;		// 0x190 - Transmit stats - byte count
	u32		XmtBytesHigh;		// 0x194 - Transmit stats - byte count
	u32		XmtTcpSegmentsLow;	// 0x198 - Transmit stats - TCP segments
	u32		XmtTcpSegmentsHigh;	// 0x19C - Transmit stats - TCP segments
	u32		XmtTcpBytesLow;		// 0x1A0 - Transmit stats - TCP bytes
	u32		XmtTcpBytesHigh;	// 0x1A4 - Transmit stats - TCP bytes
	u32		RcvErrorsLow;		// 0x1A8 - Receive stats - errors
	u32		RcvErrorsHigh;		// 0x1AC - Receive stats - errors
	u32		RcvFramesLow;		// 0x1B0 - Receive stats - frame count
	u32		RcvFramesHigh;		// 0x1B4 - Receive stats - frame count
	u32		RcvBytesLow;		// 0x1B8 - Receive stats - byte count
	u32		RcvBytesHigh;		// 0x1BC - Receive stats - byte count
	u32		RcvTcpSegmentsLow;	// 0x1C0 - Receive stats - TCP segments
	u32		RcvTcpSegmentsHigh;	// 0x1C4 - Receive stats - TCP segments
	u32		RcvTcpBytesLow;		// 0x1C8 - Receive stats - TCP bytes
	u32		RcvTcpBytesHigh;	// 0x1CC - Receive stats - TCP bytes
	u32		PadTo0x200[12];		// 0x1D0 - 0x200 - Pad
	u32		Software[1920];		// 0x200 - 0x2000 - Software defined (not used)
	u32		MsixTable[1024];	// 0x2000 - 0x3000 - MSIX Table
	u32		MsixBitArray[1024];	// 0x3000 - 0x4000 - MSIX Pending Bit Array
} SXG_HW_REGS, *PSXG_HW_REGS;
#pragma pack(pop)

// Microcode Address Flags
#define	MICROCODE_ADDRESS_GO		0x80000000	// Start microcode
#define	MICROCODE_ADDRESS_WRITE		0x40000000	// Store microcode
#define	MICROCODE_ADDRESS_READ		0x20000000	// Read microcode
#define	MICROCODE_ADDRESS_PARITY	0x10000000	// Parity error detected
#define	MICROCODE_ADDRESS_MASK		0x00001FFF	// Address bits

// Link Address Registers
#define LINK_ADDRESS_ENABLE			0x80000000	// Applied to link address high

// Microsoft register space size
#define SXG_UCODEREG_MEMSIZE	0x40000		// 256k

// Sahara microcode register address format.  The command code,
// extended command code, and associated processor are encoded in
// the address bits as follows
#define SXG_ADDRESS_CODE_SHIFT		2			// Base command code
#define SXG_ADDRESS_CODE_MASK		0x0000003C
#define SXG_ADDRESS_EXCODE_SHIFT	6			// Extended (or sub) command code
#define SXG_ADDRESS_EXCODE_MASK		0x00001FC0
#define	SXG_ADDRESS_CPUID_SHIFT		13			// CPU
#define SXG_ADDRESS_CPUID_MASK		0x0003E000
#define SXG_REGISTER_SIZE_PER_CPU	0x00002000	// Used to sanity check UCODE_REGS structure

// Sahara receive sequencer status values
#define SXG_RCV_STATUS_ATTN					0x80000000	// Attention
#define SXG_RCV_STATUS_TRANSPORT_MASK		0x3F000000	// Transport mask
#define SXG_RCV_STATUS_TRANSPORT_ERROR		0x20000000	// Transport error
#define SXG_RCV_STATUS_TRANSPORT_CSUM		0x23000000	// Transport cksum error
#define SXG_RCV_STATUS_TRANSPORT_UFLOW		0x22000000	// Transport underflow
#define SXG_RCV_STATUS_TRANSPORT_HDRLEN		0x20000000	// Transport header length
#define SXG_RCV_STATUS_TRANSPORT_FLAGS		0x10000000	// Transport flags detected
#define SXG_RCV_STATUS_TRANSPORT_OPTS		0x08000000	// Transport options detected
#define SXG_RCV_STATUS_TRANSPORT_SESS_MASK	0x07000000	// Transport DDP
#define SXG_RCV_STATUS_TRANSPORT_DDP		0x06000000	// Transport DDP
#define SXG_RCV_STATUS_TRANSPORT_iSCSI		0x05000000	// Transport iSCSI
#define SXG_RCV_STATUS_TRANSPORT_NFS		0x04000000	// Transport NFS
#define SXG_RCV_STATUS_TRANSPORT_FTP		0x03000000	// Transport FTP
#define SXG_RCV_STATUS_TRANSPORT_HTTP		0x02000000	// Transport HTTP
#define SXG_RCV_STATUS_TRANSPORT_SMB		0x01000000	// Transport SMB
#define SXG_RCV_STATUS_NETWORK_MASK			0x00FF0000	// Network mask
#define SXG_RCV_STATUS_NETWORK_ERROR		0x00800000	// Network error
#define SXG_RCV_STATUS_NETWORK_CSUM			0x00830000	// Network cksum error
#define SXG_RCV_STATUS_NETWORK_UFLOW		0x00820000	// Network underflow error
#define SXG_RCV_STATUS_NETWORK_HDRLEN		0x00800000	// Network header length
#define SXG_RCV_STATUS_NETWORK_OFLOW		0x00400000	// Network overflow detected
#define SXG_RCV_STATUS_NETWORK_MCAST		0x00200000	// Network multicast detected
#define SXG_RCV_STATUS_NETWORK_OPTIONS		0x00100000	// Network options detected
#define SXG_RCV_STATUS_NETWORK_OFFSET		0x00080000	// Network offset detected
#define SXG_RCV_STATUS_NETWORK_FRAGMENT		0x00040000	// Network fragment detected
#define SXG_RCV_STATUS_NETWORK_TRANS_MASK	0x00030000	// Network transport type mask
#define SXG_RCV_STATUS_NETWORK_UDP			0x00020000	// UDP
#define SXG_RCV_STATUS_NETWORK_TCP			0x00010000	// TCP
#define SXG_RCV_STATUS_IPONLY				0x00008000	// IP-only not TCP
#define SXG_RCV_STATUS_PKT_PRI				0x00006000	// Receive priority
#define SXG_RCV_STATUS_PKT_PRI_SHFT					13	// Receive priority shift
#define SXG_RCV_STATUS_PARITY				0x00001000	// MAC Receive RAM parity error
#define SXG_RCV_STATUS_ADDRESS_MASK			0x00000F00	// Link address detection mask
#define SXG_RCV_STATUS_ADDRESS_D			0x00000B00	// Link address D
#define SXG_RCV_STATUS_ADDRESS_C			0x00000A00	// Link address C
#define SXG_RCV_STATUS_ADDRESS_B			0x00000900	// Link address B
#define SXG_RCV_STATUS_ADDRESS_A			0x00000800	// Link address A
#define SXG_RCV_STATUS_ADDRESS_BCAST		0x00000300	// Link address broadcast
#define SXG_RCV_STATUS_ADDRESS_MCAST		0x00000200	// Link address multicast
#define SXG_RCV_STATUS_ADDRESS_CMCAST		0x00000100	// Link control multicast
#define SXG_RCV_STATUS_LINK_MASK			0x000000FF	// Link status mask
#define SXG_RCV_STATUS_LINK_ERROR			0x00000080	// Link error
#define SXG_RCV_STATUS_LINK_MASK			0x000000FF	// Link status mask
#define SXG_RCV_STATUS_LINK_PARITY			0x00000087	// RcvMacQ parity error
#define SXG_RCV_STATUS_LINK_EARLY			0x00000086	// Data early
#define SXG_RCV_STATUS_LINK_BUFOFLOW		0x00000085	// Buffer overflow
#define SXG_RCV_STATUS_LINK_CODE			0x00000084	// Link code error
#define SXG_RCV_STATUS_LINK_DRIBBLE			0x00000083	// Dribble nibble
#define SXG_RCV_STATUS_LINK_CRC				0x00000082	// CRC error
#define SXG_RCV_STATUS_LINK_OFLOW			0x00000081	// Link overflow
#define SXG_RCV_STATUS_LINK_UFLOW			0x00000080	// Link underflow
#define SXG_RCV_STATUS_LINK_8023			0x00000020	// 802.3
#define SXG_RCV_STATUS_LINK_SNAP			0x00000010	// Snap
#define SXG_RCV_STATUS_LINK_VLAN			0x00000008	// VLAN
#define SXG_RCV_STATUS_LINK_TYPE_MASK		0x00000007	// Network type mask
#define SXG_RCV_STATUS_LINK_CONTROL			0x00000003	// Control packet
#define SXG_RCV_STATUS_LINK_IPV6			0x00000002	// IPv6 packet
#define SXG_RCV_STATUS_LINK_IPV4			0x00000001	// IPv4 packet

/***************************************************************************
 * Sahara receive and transmit configuration registers
 ***************************************************************************/
#define	RCV_CONFIG_RESET			0x80000000	// RcvConfig register reset
#define	RCV_CONFIG_ENABLE			0x40000000	// Enable the receive logic
#define	RCV_CONFIG_ENPARSE			0x20000000	// Enable the receive parser
#define	RCV_CONFIG_SOCKET			0x10000000	// Enable the socket detector
#define	RCV_CONFIG_RCVBAD			0x08000000	// Receive all bad frames
#define	RCV_CONFIG_CONTROL			0x04000000	// Receive all control frames
#define	RCV_CONFIG_RCVPAUSE			0x02000000	// Enable pause transmit when attn
#define	RCV_CONFIG_TZIPV6			0x01000000	// Include TCP port w/ IPv6 toeplitz
#define	RCV_CONFIG_TZIPV4			0x00800000	// Include TCP port w/ IPv4 toeplitz
#define	RCV_CONFIG_FLUSH			0x00400000	// Flush buffers
#define	RCV_CONFIG_PRIORITY_MASK	0x00300000	// Priority level
#define	RCV_CONFIG_HASH_MASK		0x00030000	// Hash depth
#define	RCV_CONFIG_HASH_8			0x00000000	// Hash depth 8
#define	RCV_CONFIG_HASH_16			0x00010000	// Hash depth 16
#define	RCV_CONFIG_HASH_4			0x00020000	// Hash depth 4
#define	RCV_CONFIG_HASH_2			0x00030000	// Hash depth 2
#define	RCV_CONFIG_BUFLEN_MASK		0x0000FFF0	// Buffer length bits 15:4. ie multiple of 16.
#define RCV_CONFIG_SKT_DIS			0x00000008	// Disable socket detection on attn
// Macro to determine RCV_CONFIG_BUFLEN based on maximum frame size.
// We add 18 bytes for Sahara receive status and padding, plus 4 bytes for CRC,
// and round up to nearest 16 byte boundary
#define RCV_CONFIG_BUFSIZE(_MaxFrame) ((((_MaxFrame) + 22) + 15) & RCV_CONFIG_BUFLEN_MASK)

#define	XMT_CONFIG_RESET			0x80000000	// XmtConfig register reset
#define	XMT_CONFIG_ENABLE			0x40000000	// Enable transmit logic
#define	XMT_CONFIG_MAC_PARITY		0x20000000	// Inhibit MAC RAM parity error
#define	XMT_CONFIG_BUF_PARITY		0x10000000	// Inhibit D2F buffer parity error
#define	XMT_CONFIG_MEM_PARITY		0x08000000	// Inhibit 1T SRAM parity error
#define	XMT_CONFIG_INVERT_PARITY	0x04000000	// Invert MAC RAM parity
#define	XMT_CONFIG_INITIAL_IPID		0x0000FFFF	// Initial IPID

/***************************************************************************
 * A-XGMAC Registers - Occupy 0x80 - 0xD4 of the SXG_HW_REGS
 *
 * Full register descriptions can be found in axgmac.pdf
 ***************************************************************************/
// A-XGMAC Configuration Register 0
#define AXGMAC_CFG0_SUB_RESET		0x80000000		// Sub module reset
#define AXGMAC_CFG0_RCNTRL_RESET	0x00400000		// Receive control reset
#define AXGMAC_CFG0_RFUNC_RESET		0x00200000		// Receive function reset
#define AXGMAC_CFG0_TCNTRL_RESET	0x00040000		// Transmit control reset
#define AXGMAC_CFG0_TFUNC_RESET		0x00020000		// Transmit function reset
#define AXGMAC_CFG0_MII_RESET		0x00010000		// MII Management reset

// A-XGMAC Configuration Register 1
#define AXGMAC_CFG1_XMT_PAUSE		0x80000000		// Allow the sending of Pause frames
#define AXGMAC_CFG1_XMT_EN			0x40000000		// Enable transmit
#define AXGMAC_CFG1_RCV_PAUSE		0x20000000		// Allow the detection of Pause frames
#define AXGMAC_CFG1_RCV_EN			0x10000000		// Enable receive
#define AXGMAC_CFG1_XMT_STATE		0x04000000		// Current transmit state - READ ONLY
#define AXGMAC_CFG1_RCV_STATE		0x01000000		// Current receive state - READ ONLY
#define AXGMAC_CFG1_XOFF_SHORT		0x00001000		// Only pause for 64 slot on XOFF
#define AXGMAC_CFG1_XMG_FCS1		0x00000400		// Delay transmit FCS 1 4-byte word
#define AXGMAC_CFG1_XMG_FCS2		0x00000800		// Delay transmit FCS 2 4-byte words
#define AXGMAC_CFG1_XMG_FCS3		0x00000C00		// Delay transmit FCS 3 4-byte words
#define AXGMAC_CFG1_RCV_FCS1		0x00000100		// Delay receive FCS 1 4-byte word
#define AXGMAC_CFG1_RCV_FCS2		0x00000200		// Delay receive FCS 2 4-byte words
#define AXGMAC_CFG1_RCV_FCS3		0x00000300		// Delay receive FCS 3 4-byte words
#define AXGMAC_CFG1_PKT_OVERRIDE	0x00000080		// Per-packet override enable
#define AXGMAC_CFG1_SWAP			0x00000040		// Byte swap enable
#define AXGMAC_CFG1_SHORT_ASSERT	0x00000020		// ASSERT srdrpfrm on short frame (<64)
#define AXGMAC_CFG1_RCV_STRICT		0x00000010		// RCV only 802.3AE when CLEAR
#define AXGMAC_CFG1_CHECK_LEN		0x00000008		// Verify frame length
#define AXGMAC_CFG1_GEN_FCS			0x00000004		// Generate FCS
#define AXGMAC_CFG1_PAD_MASK		0x00000003		// Mask for pad bits
#define AXGMAC_CFG1_PAD_64			0x00000001		// Pad frames to 64 bytes
#define AXGMAC_CFG1_PAD_VLAN		0x00000002		// Detect VLAN and pad to 68 bytes
#define AXGMAC_CFG1_PAD_68			0x00000003		// Pad to 68 bytes

// A-XGMAC Configuration Register 2
#define AXGMAC_CFG2_GEN_PAUSE		0x80000000		// Generate single pause frame (test)
#define AXGMAC_CFG2_LF_MANUAL		0x08000000		// Manual link fault sequence
#define AXGMAC_CFG2_LF_AUTO			0x04000000		// Auto link fault sequence
#define AXGMAC_CFG2_LF_REMOTE		0x02000000		// Remote link fault (READ ONLY)
#define AXGMAC_CFG2_LF_LOCAL		0x01000000		// Local link fault (READ ONLY)
#define AXGMAC_CFG2_IPG_MASK		0x001F0000		// Inter packet gap
#define AXGMAC_CFG2_IPG_SHIFT		16
#define AXGMAC_CFG2_PAUSE_XMT		0x00008000		// Pause transmit module
#define AXGMAC_CFG2_IPG_EXTEN		0x00000020		// Enable IPG extension algorithm
#define AXGMAC_CFG2_IPGEX_MASK		0x0000001F		// IPG extension

// A-XGMAC Configuration Register 3
#define AXGMAC_CFG3_RCV_DROP		0xFFFF0000		// Receive frame drop filter
#define AXGMAC_CFG3_RCV_DONT_CARE	0x0000FFFF		// Receive frame don't care filter

// A-XGMAC Station Address Register - Octets 1-4
#define AXGMAC_SARLOW_OCTET_ONE		0xFF000000		// First octet
#define AXGMAC_SARLOW_OCTET_TWO		0x00FF0000		// Second octet
#define AXGMAC_SARLOW_OCTET_THREE	0x0000FF00		// Third octet
#define AXGMAC_SARLOW_OCTET_FOUR	0x000000FF		// Fourth octet

// A-XGMAC Station Address Register - Octets 5-6
#define AXGMAC_SARHIGH_OCTET_FIVE	0xFF000000		// Fifth octet
#define AXGMAC_SARHIGH_OCTET_SIX	0x00FF0000		// Sixth octet

// A-XGMAC Maximum frame length register
#define AXGMAC_MAXFRAME_XMT			0x3FFF0000		// Maximum transmit frame length
#define AXGMAC_MAXFRAME_XMT_SHIFT	16
#define AXGMAC_MAXFRAME_RCV			0x0000FFFF		// Maximum receive frame length
// This register doesn't need to be written for standard MTU.
// For jumbo, I'll just statically define the value here.  This
// value sets the receive byte count to 9036 (0x234C) and the
// transmit WORD count to 2259 (0x8D3).  These values include 22
// bytes of padding beyond the jumbo MTU of 9014
#define AXGMAC_MAXFRAME_JUMBO		0x08D3234C

// A-XGMAC Revision level
#define AXGMAC_REVISION_MASK		0x0000FFFF		// Revision level

// A-XGMAC AMIIM Command Register
#define AXGMAC_AMIIM_CMD_START		0x00000008		// Command start
#define AXGMAC_AMIIM_CMD_MASK		0x00000007		// Command
#define AXGMAC_AMIIM_CMD_LEGACY_WRITE		1		// 10/100/1000 Mbps Phy Write
#define AXGMAC_AMIIM_CMD_LEGACY_READ		2		// 10/100/1000 Mbps Phy Read
#define AXGMAC_AMIIM_CMD_MONITOR_SINGLE		3		// Monitor single PHY
#define AXGMAC_AMIIM_CMD_MONITOR_MULTIPLE	4		// Monitor multiple contiguous PHYs
#define AXGMAC_AMIIM_CMD_10G_OPERATION		5		// Present AMIIM Field Reg
#define AXGMAC_AMIIM_CMD_CLEAR_LINK_FAIL	6		// Clear Link Fail Bit in MIIM

// A-XGMAC AMIIM Field Register
#define AXGMAC_AMIIM_FIELD_ST		0xC0000000		// 2-bit ST field
#define AXGMAC_AMIIM_FIELD_ST_SHIFT			30
#define AXGMAC_AMIIM_FIELD_OP		0x30000000		// 2-bit OP field
#define AXGMAC_AMIIM_FIELD_OP_SHIFT			28
#define AXGMAC_AMIIM_FIELD_PORT_ADDR 0x0F800000		// Port address field (hstphyadx in spec)
#define AXGMAC_AMIIM_FIELD_PORT_SHIFT		23
#define AXGMAC_AMIIM_FIELD_DEV_ADDR	0x007C0000		// Device address field (hstregadx in spec)
#define AXGMAC_AMIIM_FIELD_DEV_SHIFT		18
#define AXGMAC_AMIIM_FIELD_TA		0x00030000		// 2-bit TA field
#define AXGMAC_AMIIM_FIELD_TA_SHIFT			16
#define AXGMAC_AMIIM_FIELD_DATA		0x0000FFFF		// Data field

// Values for the AXGMAC_AMIIM_FIELD_OP field in the A-XGMAC AMIIM Field Register
#define	MIIM_OP_ADDR						0		// MIIM Address set operation
#define	MIIM_OP_WRITE						1		// MIIM Write register operation
#define	MIIM_OP_READ						2		// MIIM Read register operation
#define	MIIM_OP_ADDR_SHIFT	(MIIM_OP_ADDR << AXGMAC_AMIIM_FIELD_OP_SHIFT)

// Values for the AXGMAC_AMIIM_FIELD_PORT_ADDR field in the A-XGMAC AMIIM Field Register
#define	MIIM_PORT_NUM						1		// All Sahara MIIM modules use port 1

// Values for the AXGMAC_AMIIM_FIELD_DEV_ADDR field in the A-XGMAC AMIIM Field Register
#define	MIIM_DEV_PHY_PMA					1		// PHY PMA/PMD module MIIM device number
#define	MIIM_DEV_PHY_PCS					3		// PHY PCS module MIIM device number
#define	MIIM_DEV_PHY_XS						4		// PHY XS module MIIM device number
#define	MIIM_DEV_XGXS						5		// XGXS MIIM device number

// Values for the AXGMAC_AMIIM_FIELD_TA field in the A-XGMAC AMIIM Field Register
#define	MIIM_TA_10GB						2		// set to 2 for 10 GB operation

// A-XGMAC AMIIM Configuration Register
#define AXGMAC_AMIIM_CFG_NOPREAM	0x00000080		// Bypass preamble of mngmt frame
#define AXGMAC_AMIIM_CFG_HALF_CLOCK	0x0000007F		// half-clock duration of MDC output

// A-XGMAC AMIIM Indicator Register
#define AXGMAC_AMIIM_INDC_LINK		0x00000010		// Link status from legacy PHY or MMD
#define AXGMAC_AMIIM_INDC_MPHY		0x00000008		// Multiple phy operation in progress
#define AXGMAC_AMIIM_INDC_SPHY		0x00000004		// Single phy operation in progress
#define AXGMAC_AMIIM_INDC_MON		0x00000002		// Single or multiple monitor cmd
#define AXGMAC_AMIIM_INDC_BUSY		0x00000001		// Set until cmd operation complete

// Link Status and Control Register
#define	LS_PHY_CLR_RESET			0x80000000		// Clear reset signal to PHY
#define	LS_SERDES_POWER_DOWN		0x40000000		// Power down the Sahara Serdes
#define	LS_XGXS_ENABLE				0x20000000		// Enable the XAUI XGXS logic
#define	LS_XGXS_CTL					0x10000000		// Hold XAUI XGXS logic reset until Serdes is up
#define	LS_SERDES_DOWN				0x08000000		// When 0, XAUI Serdes is up and initialization is complete
#define	LS_TRACE_DOWN				0x04000000		// When 0, Trace Serdes is up and initialization is complete
#define	LS_PHY_CLK_25MHZ			0x02000000		// Set PHY clock to 25 MHz (else 156.125 MHz)
#define	LS_PHY_CLK_EN				0x01000000		// Enable clock to PHY
#define	LS_XAUI_LINK_UP				0x00000010		// XAUI link is up
#define	LS_XAUI_LINK_CHNG			0x00000008		// XAUI link status has changed
#define	LS_LINK_ALARM				0x00000004		// Link alarm pin
#define	LS_ATTN_CTRL_MASK			0x00000003		// Mask link attention control bits
#define	LS_ATTN_ALARM				0x00000000		// 00 => Attn on link alarm
#define	LS_ATTN_ALARM_OR_STAT_CHNG	0x00000001		// 01 => Attn on link alarm or status change
#define	LS_ATTN_STAT_CHNG			0x00000002		// 10 => Attn on link status change
#define	LS_ATTN_NONE				0x00000003		// 11 => no Attn

// Link Address High Registers
#define	LINK_ADDR_ENABLE			0x80000000		// Enable this link address


/***************************************************************************
 * XGXS XAUI XGMII Extender registers
 *
 * Full register descriptions can be found in mxgxs.pdf
 ***************************************************************************/
// XGXS Register Map
#define XGXS_ADDRESS_CONTROL1		0x0000			// XS Control 1
#define XGXS_ADDRESS_STATUS1		0x0001			// XS Status 1
#define XGXS_ADDRESS_DEVID_LOW		0x0002			// XS Device ID (low)
#define XGXS_ADDRESS_DEVID_HIGH		0x0003			// XS Device ID (high)
#define XGXS_ADDRESS_SPEED			0x0004			// XS Speed ability
#define XGXS_ADDRESS_DEV_LOW		0x0005			// XS Devices in package
#define XGXS_ADDRESS_DEV_HIGH		0x0006			// XS Devices in package
#define XGXS_ADDRESS_STATUS2		0x0008			// XS Status 2
#define XGXS_ADDRESS_PKGID_lOW		0x000E			// XS Package Identifier
#define XGXS_ADDRESS_PKGID_HIGH		0x000F			// XS Package Identifier
#define XGXS_ADDRESS_LANE_STATUS	0x0018			// 10G XGXS Lane Status
#define XGXS_ADDRESS_TEST_CTRL		0x0019			// 10G XGXS Test Control
#define XGXS_ADDRESS_RESET_LO1		0x8000			// Vendor-Specific Reset Lo 1
#define XGXS_ADDRESS_RESET_LO2		0x8001			// Vendor-Specific Reset Lo 2
#define XGXS_ADDRESS_RESET_HI1		0x8002			// Vendor-Specific Reset Hi 1
#define XGXS_ADDRESS_RESET_HI2		0x8003			// Vendor-Specific Reset Hi 2

// XS Control 1 register bit definitions
#define XGXS_CONTROL1_RESET			0x8000			// Reset - self clearing
#define XGXS_CONTROL1_LOOPBACK		0x4000			// Enable loopback
#define XGXS_CONTROL1_SPEED1		0x2000			// 0 = unspecified, 1 = 10Gb+
#define XGXS_CONTROL1_LOWPOWER		0x0400			// 1 = Low power mode
#define XGXS_CONTROL1_SPEED2		0x0040			// Same as SPEED1 (?)
#define XGXS_CONTROL1_SPEED			0x003C			// Everything reserved except zero (?)

// XS Status 1 register bit definitions
#define XGXS_STATUS1_FAULT			0x0080			// Fault detected
#define XGXS_STATUS1_LINK			0x0004			// 1 = Link up
#define XGXS_STATUS1_LOWPOWER		0x0002			// 1 = Low power supported

// XS Speed register bit definitions
#define XGXS_SPEED_10G				0x0001			// 1 = 10G capable

// XS Devices register bit definitions
#define XGXS_DEVICES_DTE			0x0020			// DTE XS Present
#define XGXS_DEVICES_PHY			0x0010			// PHY XS Present
#define XGXS_DEVICES_PCS			0x0008			// PCS Present
#define XGXS_DEVICES_WIS			0x0004			// WIS Present
#define XGXS_DEVICES_PMD			0x0002			// PMD/PMA Present
#define XGXS_DEVICES_CLAUSE22		0x0001			// Clause 22 registers present

// XS Devices High register bit definitions
#define XGXS_DEVICES_VENDOR2		0x8000			// Vendor specific device 2
#define XGXS_DEVICES_VENDOR1		0x4000			// Vendor specific device 1

// XS Status 2 register bit definitions
#define XGXS_STATUS2_DEV_MASK		0xC000			// Device present mask
#define XGXS_STATUS2_DEV_RESPOND	0x8000			// Device responding
#define XGXS_STATUS2_XMT_FAULT		0x0800			// Transmit fault
#define XGXS_STATUS2_RCV_FAULT		0x0400			// Receive fault

// XS Package ID High register bit definitions
#define XGXS_PKGID_HIGH_ORG			0xFC00			// Organizationally Unique
#define XGXS_PKGID_HIGH_MFG			0x03F0			// Manufacturer Model
#define XGXS_PKGID_HIGH_REV			0x000F			// Revision Number

// XS Lane Status register bit definitions
#define XGXS_LANE_PHY				0x1000			// PHY/DTE lane alignment status
#define XGXS_LANE_PATTERN			0x0800			// Pattern testing ability
#define XGXS_LANE_LOOPBACK			0x0400			// PHY loopback ability
#define XGXS_LANE_SYNC3				0x0008			// Lane 3 sync
#define XGXS_LANE_SYNC2				0x0004			// Lane 2 sync
#define XGXS_LANE_SYNC1				0x0002			// Lane 1 sync
#define XGXS_LANE_SYNC0				0x0001			// Lane 0 sync

// XS Test Control register bit definitions
#define XGXS_TEST_PATTERN_ENABLE	0x0004			// Test pattern enabled
#define XGXS_TEST_PATTERN_MASK		0x0003			// Test patterns
#define XGXS_TEST_PATTERN_RSVD		0x0003			// Test pattern - reserved
#define XGXS_TEST_PATTERN_MIX		0x0002			// Test pattern - mixed
#define XGXS_TEST_PATTERN_LOW		0x0001			// Test pattern - low
#define XGXS_TEST_PATTERN_HIGH		0x0001			// Test pattern - high

/***************************************************************************
 * External MDIO Bus Registers
 *
 * Full register descriptions can be found in PHY/XENPAK/IEEE specs
 ***************************************************************************/
// LASI (Link Alarm Status Interrupt) Registers (located in MIIM_DEV_PHY_PMA device)
#define LASI_RX_ALARM_CONTROL		0x9000			// LASI RX_ALARM Control
#define LASI_TX_ALARM_CONTROL		0x9001			// LASI TX_ALARM Control
#define LASI_CONTROL				0x9002			// LASI Control
#define LASI_RX_ALARM_STATUS		0x9003			// LASI RX_ALARM Status
#define LASI_TX_ALARM_STATUS		0x9004			// LASI TX_ALARM Status
#define LASI_STATUS					0x9005			// LASI Status

// LASI_CONTROL bit definitions
#define	LASI_CTL_RX_ALARM_ENABLE	0x0004			// Enable RX_ALARM interrupts
#define	LASI_CTL_TX_ALARM_ENABLE	0x0002			// Enable TX_ALARM interrupts
#define	LASI_CTL_LS_ALARM_ENABLE	0x0001			// Enable Link Status interrupts

// LASI_STATUS bit definitions
#define	LASI_STATUS_RX_ALARM		0x0004			// RX_ALARM status
#define	LASI_STATUS_TX_ALARM		0x0002			// TX_ALARM status
#define	LASI_STATUS_LS_ALARM		0x0001			// Link Status

// PHY registers - PMA/PMD (device 1)
#define	PHY_PMA_CONTROL1			0x0000			// PMA/PMD Control 1
#define	PHY_PMA_STATUS1				0x0001			// PMA/PMD Status 1
#define	PHY_PMA_RCV_DET				0x000A			// PMA/PMD Receive Signal Detect
		// other PMA/PMD registers exist and can be defined as needed

// PHY registers - PCS (device 3)
#define	PHY_PCS_CONTROL1			0x0000			// PCS Control 1
#define	PHY_PCS_STATUS1				0x0001			// PCS Status 1
#define	PHY_PCS_10G_STATUS1			0x0020			// PCS 10GBASE-R Status 1
		// other PCS registers exist and can be defined as needed

// PHY registers - XS (device 4)
#define	PHY_XS_CONTROL1				0x0000			// XS Control 1
#define	PHY_XS_STATUS1				0x0001			// XS Status 1
#define	PHY_XS_LANE_STATUS			0x0018			// XS Lane Status
		// other XS registers exist and can be defined as needed

// PHY_PMA_CONTROL1 register bit definitions
#define	PMA_CONTROL1_RESET			0x8000			// PMA/PMD reset

// PHY_PMA_RCV_DET register bit definitions
#define	PMA_RCV_DETECT				0x0001			// PMA/PMD receive signal detect

// PHY_PCS_10G_STATUS1 register bit definitions
#define	PCS_10B_BLOCK_LOCK			0x0001			// PCS 10GBASE-R locked to receive blocks

// PHY_XS_LANE_STATUS register bit definitions
#define	XS_LANE_ALIGN				0x1000			// XS transmit lanes aligned

// PHY Microcode download data structure
typedef struct _PHY_UCODE {
	ushort	Addr;
	ushort	Data;
} PHY_UCODE, *PPHY_UCODE;


/*****************************************************************************
 * Transmit Sequencer Command Descriptor definitions
 *****************************************************************************/

// This descriptor must be placed in GRAM.  The address of this descriptor
// (along with a couple of control bits) is pushed onto the PxhCmdQ or PxlCmdQ
// (Proxy high or low command queue).  This data is read by the Proxy Sequencer,
// which pushes it onto the XmtCmdQ, which is (eventually) read by the Transmit
// Sequencer, causing a packet to be transmitted.  Not all fields are valid for
// all commands - see the Sahara spec for details.  Note that this structure is
// only valid when compiled on a little endian machine.
#pragma pack(push, 1)
typedef struct _XMT_DESC {
	ushort	XmtLen;			// word 0, bits [15:0] -  transmit length
	unsigned char	XmtCtl;			// word 0, bits [23:16] - transmit control byte
	unsigned char	Cmd;			// word 0, bits [31:24] - transmit command plus misc.
	u32	XmtBufId;		// word 1, bits [31:0] -  transmit buffer ID
	unsigned char	TcpStrt;		// word 2, bits [7:0] -   byte address of TCP header
	unsigned char	IpStrt;			// word 2, bits [15:8] -  byte address of IP header
	ushort	IpCkSum;		// word 2, bits [31:16] - partial IP checksum
	ushort	TcpCkSum;		// word 3, bits [15:0] -  partial TCP checksum
	ushort	Rsvd1;			// word 3, bits [31:16] - PAD
	u32	Rsvd2;			// word 4, bits [31:0] -  PAD
	u32	Rsvd3;			// word 5, bits [31:0] -  PAD
	u32	Rsvd4;		    // word 6, bits [31:0] -  PAD
	u32	Rsvd5;		    // word 7, bits [31:0] -  PAD
} XMT_DESC, *PXMT_DESC;
#pragma pack(pop)

// XMT_DESC Cmd byte definitions
		// command codes
#define XMT_DESC_CMD_RAW_SEND		0		// raw send descriptor
#define XMT_DESC_CMD_CSUM_INSERT	1		// checksum insert descriptor
#define XMT_DESC_CMD_FORMAT			2		// format descriptor
#define XMT_DESC_CMD_PRIME			3		// prime descriptor
#define XMT_DESC_CMD_CODE_SHFT		6		// comand code shift (shift to bits [31:30] in word 0)
		// shifted command codes
#define XMT_RAW_SEND		(XMT_DESC_CMD_RAW_SEND    << XMT_DESC_CMD_CODE_SHFT)
#define XMT_CSUM_INSERT		(XMT_DESC_CMD_CSUM_INSERT << XMT_DESC_CMD_CODE_SHFT)
#define XMT_FORMAT			(XMT_DESC_CMD_FORMAT      << XMT_DESC_CMD_CODE_SHFT)
#define XMT_PRIME			(XMT_DESC_CMD_PRIME       << XMT_DESC_CMD_CODE_SHFT)

// XMT_DESC Control Byte (XmtCtl) definitions
// NOTE:  These bits do not work on Sahara (Rev A)!
#define	XMT_CTL_PAUSE_FRAME			0x80	// current frame is a pause control frame (for statistics)
#define	XMT_CTL_CONTROL_FRAME		0x40	// current frame is a control frame (for statistics)
#define	XMT_CTL_PER_PKT_QUAL		0x20	// per packet qualifier
#define	XMT_CTL_PAD_MODE_NONE		0x00	// do not pad frame
#define	XMT_CTL_PAD_MODE_64			0x08	// pad frame to 64 bytes
#define	XMT_CTL_PAD_MODE_VLAN_68	0x10	// pad frame to 64 bytes, and VLAN frames to 68 bytes
#define	XMT_CTL_PAD_MODE_68			0x18	// pad frame to 68 bytes
#define	XMT_CTL_GEN_FCS				0x04	// generate FCS (CRC) for this frame
#define	XMT_CTL_DELAY_FCS_0			0x00	// do not delay FCS calcution
#define	XMT_CTL_DELAY_FCS_1			0x01	// delay FCS calculation by 1 (4-byte) word
#define	XMT_CTL_DELAY_FCS_2			0x02	// delay FCS calculation by 2 (4-byte) words
#define	XMT_CTL_DELAY_FCS_3			0x03	// delay FCS calculation by 3 (4-byte) words

// XMT_DESC XmtBufId definition
#define XMT_BUF_ID_SHFT		8			// The Xmt buffer ID is formed by dividing
										// the buffer (DRAM) address by 256 (or << 8)

/*****************************************************************************
 * Receiver Sequencer Definitions
 *****************************************************************************/

// Receive Event Queue (queues 3 - 6) bit definitions
#define	RCV_EVTQ_RBFID_MASK		0x0000FFFF	// bit mask for the Receive Buffer ID

// Receive Buffer ID definition
#define RCV_BUF_ID_SHFT		5			// The Rcv buffer ID is formed by dividing
										// the buffer (DRAM) address by 32 (or << 5)

// Format of the 18 byte Receive Buffer returned by the
// Receive Sequencer for received packets
#pragma pack(push, 1)
typedef struct _RCV_BUF_HDR {
	u32	Status;				// Status word from Rcv Seq Parser
	ushort	Length;				// Rcv packet byte count
	union {
		ushort		TcpCsum;	// TCP checksum
		struct {
			unsigned char	TcpCsumL;	// lower 8 bits of the TCP checksum
			unsigned char	LinkHash;	// Link hash (multicast frames only)
		};
	};
	ushort	SktHash;			// Socket hash
	unsigned char	TcpHdrOffset;		// TCP header offset into packet
	unsigned char	IpHdrOffset;		// IP header offset into packet
	u32	TpzHash;			// Toeplitz hash
	ushort	Reserved;			// Reserved
} RCV_BUF_HDR, *PRCV_BUF_HDR;
#pragma pack(pop)


/*****************************************************************************
 * Queue definitions
 *****************************************************************************/

// Ingress (read only) queue numbers
#define PXY_BUF_Q		0		// Proxy Buffer Queue
#define HST_EVT_Q		1		// Host Event Queue
#define XMT_BUF_Q		2		// Transmit Buffer Queue
#define SKT_EVL_Q		3		// RcvSqr Socket Event Low Priority Queue
#define RCV_EVL_Q		4		// RcvSqr Rcv Event Low Priority Queue
#define SKT_EVH_Q		5		// RcvSqr Socket Event High Priority Queue
#define RCV_EVH_Q		6		// RcvSqr Rcv Event High Priority Queue
#define DMA_RSP_Q		7		// Dma Response Queue - one per CPU context
// Local (read/write) queue numbers
#define LOCAL_A_Q		8		// Spare local Queue
#define LOCAL_B_Q		9		// Spare local Queue
#define LOCAL_C_Q		10		// Spare local Queue
#define FSM_EVT_Q		11		// Finite-State-Machine Event Queue
#define SBF_PAL_Q		12		// System Buffer Physical Address (low) Queue
#define SBF_PAH_Q		13		// System Buffer Physical Address (high) Queue
#define SBF_VAL_Q		14		// System Buffer Virtual Address (low) Queue
#define SBF_VAH_Q		15		// System Buffer Virtual Address (high) Queue
// Egress (write only) queue numbers
#define H2G_CMD_Q		16		// Host to GlbRam DMA Command Queue
#define H2D_CMD_Q		17		// Host to DRAM DMA Command Queue
#define G2H_CMD_Q		18		// GlbRam to Host DMA Command Queue
#define G2D_CMD_Q		19		// GlbRam to DRAM DMA Command Queue
#define D2H_CMD_Q		20		// DRAM to Host DMA Command Queue
#define D2G_CMD_Q		21		// DRAM to GlbRam DMA Command Queue
#define D2D_CMD_Q		22		// DRAM to DRAM DMA Command Queue
#define PXL_CMD_Q		23		// Low Priority Proxy Command Queue
#define PXH_CMD_Q		24		// High Priority Proxy Command Queue
#define RSQ_CMD_Q		25		// Receive Sequencer Command Queue
#define RCV_BUF_Q		26		// Receive Buffer Queue

// Bit definitions for the Proxy Command queues (PXL_CMD_Q and PXH_CMD_Q)
#define PXY_COPY_EN		0x00200000		// enable copy of xmt descriptor to xmt command queue
#define PXY_SIZE_16		0x00000000		// copy 16 bytes
#define PXY_SIZE_32		0x00100000		// copy 32 bytes

/*****************************************************************************
 * SXG EEPROM/Flash Configuration Definitions
 *****************************************************************************/
#pragma pack(push, 1)

//
typedef struct _HW_CFG_DATA {
	ushort		Addr;
	union {
		ushort	Data;
		ushort	Checksum;
	};
} HW_CFG_DATA, *PHW_CFG_DATA;

//
#define	NUM_HW_CFG_ENTRIES	((128/sizeof(HW_CFG_DATA)) - 4)

// MAC address
typedef struct _SXG_CONFIG_MAC {
	unsigned char		MacAddr[6];			// MAC Address
} SXG_CONFIG_MAC, *PSXG_CONFIG_MAC;

//
typedef struct _ATK_FRU {
	unsigned char		PartNum[6];
	unsigned char		Revision[2];
	unsigned char		Serial[14];
} ATK_FRU, *PATK_FRU;

// OEM FRU Format types
#define	ATK_FRU_FORMAT		0x0000
#define CPQ_FRU_FORMAT		0x0001
#define DELL_FRU_FORMAT		0x0002
#define HP_FRU_FORMAT		0x0003
#define IBM_FRU_FORMAT		0x0004
#define EMC_FRU_FORMAT		0x0005
#define NO_FRU_FORMAT		0xFFFF

// EEPROM/Flash Format
typedef struct _SXG_CONFIG {
	//
	// Section 1 (128 bytes)
	//
	ushort			MagicWord;			// EEPROM/FLASH Magic code 'A5A5'
	ushort			SpiClks;			// SPI bus clock dividers
	HW_CFG_DATA		HwCfg[NUM_HW_CFG_ENTRIES];
	//
	//
	//
	ushort			Version;			// EEPROM format version
	SXG_CONFIG_MAC	MacAddr[4];			// space for 4 MAC addresses
	ATK_FRU			AtkFru;				// FRU information
	ushort			OemFruFormat;		// OEM FRU format type
	unsigned char			OemFru[76];			// OEM FRU information (optional)
	ushort			Checksum;			// Checksum of section 2
	// CS info XXXTODO
} SXG_CONFIG, *PSXG_CONFIG;
#pragma pack(pop)

/*****************************************************************************
 * Miscellaneous Hardware definitions
 *****************************************************************************/

// Sahara (ASIC level) defines
#define SAHARA_GRAM_SIZE			0x020000		// GRAM size - 128 KB
#define SAHARA_DRAM_SIZE			0x200000		// DRAM size - 2 MB
#define SAHARA_QRAM_SIZE			0x004000		// QRAM size - 16K entries (64 KB)
#define SAHARA_WCS_SIZE				0x002000		// WCS - 8K instructions (x 108 bits)

// Arabia (board level) defines
#define	FLASH_SIZE				0x080000		// 512 KB (4 Mb)
#define	EEPROM_SIZE_XFMR		512				// true EEPROM size (bytes), including xfmr area
#define	EEPROM_SIZE_NO_XFMR		256				// EEPROM size excluding xfmr area
