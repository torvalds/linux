/* SPDX-License-Identifier: GPL-2.0+ */
/************************************************************************
 *
 *	USBVEND.H		Vendor-specific USB definitions
 *
 *	NOTE: This must be kept in sync with the Edgeport firmware and
 *	must be kept backward-compatible with older firmware.
 *
 ************************************************************************
 *
 *	Copyright (C) 1998 Inside Out Networks, Inc.
 *
 ************************************************************************/

#if !defined(_USBVEND_H)
#define	_USBVEND_H

/************************************************************************
 *
 *		D e f i n e s   /   T y p e d e f s
 *
 ************************************************************************/

//
// Definitions of USB product IDs
//

#define	USB_VENDOR_ID_ION	0x1608		// Our VID
#define	USB_VENDOR_ID_TI	0x0451		// TI VID
#define USB_VENDOR_ID_AXIOHM	0x05D9		/* Axiohm VID */

//
// Definitions of USB product IDs (PID)
// We break the USB-defined PID into an OEM Id field (upper 6 bits)
// and a Device Id (bottom 10 bits). The Device Id defines what
// device this actually is regardless of what the OEM wants to
// call it.
//

// ION-device OEM IDs
#define	ION_OEM_ID_ION		0		// 00h Inside Out Networks
#define	ION_OEM_ID_NLYNX	1		// 01h NLynx Systems
#define	ION_OEM_ID_GENERIC	2		// 02h Generic OEM
#define	ION_OEM_ID_MAC		3		// 03h Mac Version
#define	ION_OEM_ID_MEGAWOLF	4		// 04h Lupusb OEM Mac version (MegaWolf)
#define	ION_OEM_ID_MULTITECH	5		// 05h Multitech Rapidports
#define	ION_OEM_ID_AGILENT	6		// 06h AGILENT board


// ION-device Device IDs
// Product IDs - assigned to match middle digit of serial number (No longer true)

#define ION_DEVICE_ID_80251_NETCHIP	0x020	// This bit is set in the PID if this edgeport hardware$
						// is based on the 80251+Netchip.

#define ION_DEVICE_ID_GENERATION_1	0x00	// Value for 930 based edgeports
#define ION_DEVICE_ID_GENERATION_2	0x01	// Value for 80251+Netchip.
#define ION_DEVICE_ID_GENERATION_3	0x02	// Value for Texas Instruments TUSB5052 chip
#define ION_DEVICE_ID_GENERATION_4	0x03	// Watchport Family of products
#define ION_GENERATION_MASK		0x03

#define ION_DEVICE_ID_HUB_MASK		0x0080	// This bit in the PID designates a HUB device
						// for example 8C would be a 421 4 port hub
						// and 8D would be a 2 port embedded hub

#define EDGEPORT_DEVICE_ID_MASK			0x0ff	// Not including OEM or GENERATION fields

#define	ION_DEVICE_ID_UNCONFIGURED_EDGE_DEVICE	0x000	// In manufacturing only
#define ION_DEVICE_ID_EDGEPORT_4		0x001	// Edgeport/4 RS232
#define	ION_DEVICE_ID_EDGEPORT_8R		0x002	// Edgeport with RJ45 no Ring
#define ION_DEVICE_ID_RAPIDPORT_4		0x003	// Rapidport/4
#define ION_DEVICE_ID_EDGEPORT_4T		0x004	// Edgeport/4 RS232 for Telxon (aka "Fleetport")
#define ION_DEVICE_ID_EDGEPORT_2		0x005	// Edgeport/2 RS232
#define ION_DEVICE_ID_EDGEPORT_4I		0x006	// Edgeport/4 RS422
#define ION_DEVICE_ID_EDGEPORT_2I		0x007	// Edgeport/2 RS422/RS485
#define	ION_DEVICE_ID_EDGEPORT_8RR		0x008	// Edgeport with RJ45 with Data and RTS/CTS only
//	ION_DEVICE_ID_EDGEPORT_8_HANDBUILT	0x009	// Hand-built Edgeport/8 (Placeholder, used in middle digit of serial number only!)
//	ION_DEVICE_ID_MULTIMODEM_4X56		0x00A	// MultiTech version of RP/4 (Placeholder, used in middle digit of serial number only!)
#define	ION_DEVICE_ID_EDGEPORT_PARALLEL_PORT	0x00B	// Edgeport/(4)21 Parallel port (USS720)
#define	ION_DEVICE_ID_EDGEPORT_421		0x00C	// Edgeport/421 Hub+RS232+Parallel
#define	ION_DEVICE_ID_EDGEPORT_21		0x00D	// Edgeport/21  RS232+Parallel
#define ION_DEVICE_ID_EDGEPORT_8_DUAL_CPU	0x00E	// Half of an Edgeport/8 (the kind with 2 EP/4s on 1 PCB)
#define ION_DEVICE_ID_EDGEPORT_8		0x00F	// Edgeport/8 (single-CPU)
#define ION_DEVICE_ID_EDGEPORT_2_DIN		0x010	// Edgeport/2 RS232 with Apple DIN connector
#define ION_DEVICE_ID_EDGEPORT_4_DIN		0x011	// Edgeport/4 RS232 with Apple DIN connector
#define ION_DEVICE_ID_EDGEPORT_16_DUAL_CPU	0x012	// Half of an Edgeport/16 (the kind with 2 EP/8s)
#define ION_DEVICE_ID_EDGEPORT_COMPATIBLE	0x013	// Edgeport Compatible, for NCR, Axiohm etc. testing
#define ION_DEVICE_ID_EDGEPORT_8I		0x014	// Edgeport/8 RS422 (single-CPU)
#define ION_DEVICE_ID_EDGEPORT_1		0x015	// Edgeport/1 RS232
#define ION_DEVICE_ID_EPOS44			0x016	// Half of an EPOS/44 (TIUMP BASED)
#define ION_DEVICE_ID_EDGEPORT_42		0x017	// Edgeport/42
#define ION_DEVICE_ID_EDGEPORT_412_8		0x018	// Edgeport/412 8 port part
#define ION_DEVICE_ID_EDGEPORT_412_4		0x019	// Edgeport/412	4 port part
#define ION_DEVICE_ID_EDGEPORT_22I		0x01A	// Edgeport/22I is an Edgeport/4 with ports 1&2 RS422 and ports 3&4 RS232

// Compact Form factor TI based devices  2c, 21c, 22c, 221c
#define ION_DEVICE_ID_EDGEPORT_2C		0x01B	// Edgeport/2c is a TI based Edgeport/2 - Small I2c
#define ION_DEVICE_ID_EDGEPORT_221C		0x01C	// Edgeport/221c is a TI based Edgeport/2 with lucent chip and
							// 2 external hub ports - Large I2C
#define ION_DEVICE_ID_EDGEPORT_22C		0x01D	// Edgeport/22c is a TI based Edgeport/2 with
							// 2 external hub ports - Large I2C
#define ION_DEVICE_ID_EDGEPORT_21C		0x01E	// Edgeport/21c is a TI based Edgeport/2 with lucent chip
							// Small I2C


/*
 *  DANGER DANGER The 0x20 bit was used to indicate a 8251/netchip GEN 2 device.
 *  Since the MAC, Linux, and Optimal drivers still used the old code
 *  I suggest that you skip the 0x20 bit when creating new PIDs
 */


// Generation 3 devices -- 3410 based edgport/1 (256 byte I2C)
#define ION_DEVICE_ID_TI3410_EDGEPORT_1		0x040	// Edgeport/1 RS232
#define ION_DEVICE_ID_TI3410_EDGEPORT_1I	0x041	// Edgeport/1i- RS422 model

// Ti based software switchable RS232/RS422/RS485 devices
#define ION_DEVICE_ID_EDGEPORT_4S		0x042	// Edgeport/4s - software switchable model
#define ION_DEVICE_ID_EDGEPORT_8S		0x043	// Edgeport/8s - software switchable model

// Usb to Ethernet dongle
#define ION_DEVICE_ID_EDGEPORT_E		0x0E0	// Edgeport/E Usb to Ethernet

// Edgeport TI based devices
#define ION_DEVICE_ID_TI_EDGEPORT_4		0x0201	// Edgeport/4 RS232
#define ION_DEVICE_ID_TI_EDGEPORT_2		0x0205	// Edgeport/2 RS232
#define ION_DEVICE_ID_TI_EDGEPORT_4I		0x0206	// Edgeport/4i RS422
#define ION_DEVICE_ID_TI_EDGEPORT_2I		0x0207	// Edgeport/2i RS422/RS485
#define ION_DEVICE_ID_TI_EDGEPORT_421		0x020C	// Edgeport/421 4 hub 2 RS232 + Parallel (lucent on a different hub port)
#define ION_DEVICE_ID_TI_EDGEPORT_21		0x020D	// Edgeport/21 2 RS232 + Parallel (lucent on a different hub port)
#define ION_DEVICE_ID_TI_EDGEPORT_416		0x0212  // Edgeport/416
#define ION_DEVICE_ID_TI_EDGEPORT_1		0x0215	// Edgeport/1 RS232
#define ION_DEVICE_ID_TI_EDGEPORT_42		0x0217	// Edgeport/42 4 hub 2 RS232
#define ION_DEVICE_ID_TI_EDGEPORT_22I		0x021A	// Edgeport/22I is an Edgeport/4 with ports 1&2 RS422 and ports 3&4 RS232
#define ION_DEVICE_ID_TI_EDGEPORT_2C		0x021B	// Edgeport/2c RS232
#define ION_DEVICE_ID_TI_EDGEPORT_221C		0x021C	// Edgeport/221c is a TI based Edgeport/2 with lucent chip and
							// 2 external hub ports - Large I2C
#define ION_DEVICE_ID_TI_EDGEPORT_22C		0x021D	// Edgeport/22c is a TI based Edgeport/2 with
							// 2 external hub ports - Large I2C
#define ION_DEVICE_ID_TI_EDGEPORT_21C		0x021E	// Edgeport/21c is a TI based Edgeport/2 with lucent chip

// Generation 3 devices -- 3410 based edgport/1 (256 byte I2C)
#define ION_DEVICE_ID_TI_TI3410_EDGEPORT_1	0x0240	// Edgeport/1 RS232
#define ION_DEVICE_ID_TI_TI3410_EDGEPORT_1I	0x0241	// Edgeport/1i- RS422 model

// Ti based software switchable RS232/RS422/RS485 devices
#define ION_DEVICE_ID_TI_EDGEPORT_4S		0x0242	// Edgeport/4s - software switchable model
#define ION_DEVICE_ID_TI_EDGEPORT_8S		0x0243	// Edgeport/8s - software switchable model
#define ION_DEVICE_ID_TI_EDGEPORT_8		0x0244	// Edgeport/8 (single-CPU)
#define ION_DEVICE_ID_TI_EDGEPORT_416B		0x0247	// Edgeport/416


/************************************************************************
 *
 *                        Generation 4 devices
 *
 ************************************************************************/

// Watchport based on 3410 both 1-wire and binary products (16K I2C)
#define ION_DEVICE_ID_WP_UNSERIALIZED		0x300	// Watchport based on 3410 both 1-wire and binary products
#define ION_DEVICE_ID_WP_PROXIMITY		0x301	// Watchport/P Discontinued
#define ION_DEVICE_ID_WP_MOTION			0x302	// Watchport/M
#define ION_DEVICE_ID_WP_MOISTURE		0x303	// Watchport/W
#define ION_DEVICE_ID_WP_TEMPERATURE		0x304	// Watchport/T
#define ION_DEVICE_ID_WP_HUMIDITY		0x305	// Watchport/H

#define ION_DEVICE_ID_WP_POWER			0x306	// Watchport
#define ION_DEVICE_ID_WP_LIGHT			0x307	// Watchport
#define ION_DEVICE_ID_WP_RADIATION		0x308	// Watchport
#define ION_DEVICE_ID_WP_ACCELERATION		0x309	// Watchport/A
#define ION_DEVICE_ID_WP_DISTANCE		0x30A	// Watchport/D Discontinued
#define ION_DEVICE_ID_WP_PROX_DIST		0x30B	// Watchport/D uses distance sensor
							// Default to /P function

#define ION_DEVICE_ID_PLUS_PWR_HP4CD		0x30C	// 5052 Plus Power HubPort/4CD+ (for Dell)
#define ION_DEVICE_ID_PLUS_PWR_HP4C		0x30D	// 5052 Plus Power HubPort/4C+
#define ION_DEVICE_ID_PLUS_PWR_PCI		0x30E	// 3410 Plus Power PCI Host Controller 4 port


//
// Definitions for AXIOHM USB product IDs
//
#define	USB_VENDOR_ID_AXIOHM			0x05D9	// Axiohm VID

#define AXIOHM_DEVICE_ID_MASK			0xffff
#define AXIOHM_DEVICE_ID_EPIC_A758		0xA758
#define AXIOHM_DEVICE_ID_EPIC_A794		0xA794
#define AXIOHM_DEVICE_ID_EPIC_A225		0xA225


//
// Definitions for NCR USB product IDs
//
#define	USB_VENDOR_ID_NCR			0x0404	// NCR VID

#define NCR_DEVICE_ID_MASK			0xffff
#define NCR_DEVICE_ID_EPIC_0202			0x0202
#define NCR_DEVICE_ID_EPIC_0203			0x0203
#define NCR_DEVICE_ID_EPIC_0310			0x0310
#define NCR_DEVICE_ID_EPIC_0311			0x0311
#define NCR_DEVICE_ID_EPIC_0312			0x0312


//
// Definitions for SYMBOL USB product IDs
//
#define USB_VENDOR_ID_SYMBOL			0x05E0	// Symbol VID
#define SYMBOL_DEVICE_ID_MASK			0xffff
#define SYMBOL_DEVICE_ID_KEYFOB			0x0700


//
// Definitions for other product IDs
#define ION_DEVICE_ID_MT4X56USB			0x1403	// OEM device
#define ION_DEVICE_ID_E5805A			0x1A01  // OEM device (rebranded Edgeport/4)


#define	GENERATION_ID_FROM_USB_PRODUCT_ID(ProductId)				\
			((__u16) ((ProductId >> 8) & (ION_GENERATION_MASK)))

#define	MAKE_USB_PRODUCT_ID(OemId, DeviceId)					\
			((__u16) (((OemId) << 10) || (DeviceId)))

#define	DEVICE_ID_FROM_USB_PRODUCT_ID(ProductId)				\
			((__u16) ((ProductId) & (EDGEPORT_DEVICE_ID_MASK)))

#define	OEM_ID_FROM_USB_PRODUCT_ID(ProductId)					\
			((__u16) (((ProductId) >> 10) & 0x3F))

//
// Definitions of parameters for download code. Note that these are
// specific to a given version of download code and must change if the
// corresponding download code changes.
//

// TxCredits value below which driver won't bother sending (to prevent too many small writes).
// Send only if above 25%
#define EDGE_FW_GET_TX_CREDITS_SEND_THRESHOLD(InitialCredit, MaxPacketSize) (max(((InitialCredit) / 4), (MaxPacketSize)))

#define	EDGE_FW_BULK_MAX_PACKET_SIZE		64	// Max Packet Size for Bulk In Endpoint (EP1)
#define EDGE_FW_BULK_READ_BUFFER_SIZE		1024	// Size to use for Bulk reads

#define	EDGE_FW_INT_MAX_PACKET_SIZE		32	// Max Packet Size for Interrupt In Endpoint
							// Note that many units were shipped with MPS=16, we
							// force an upgrade to this value).
#define EDGE_FW_INT_INTERVAL			2	// 2ms polling on IntPipe


//
// Definitions of I/O Networks vendor-specific requests
// for default endpoint
//
//	bmRequestType = 01000000	Set vendor-specific, to device
//	bmRequestType = 11000000	Get vendor-specific, to device
//
// These are the definitions for the bRequest field for the
// above bmRequestTypes.
//
// For the read/write Edgeport memory commands, the parameters
// are as follows:
//		wValue = 16-bit address
//		wIndex = unused (though we could put segment 00: or FF: here)
//		wLength = # bytes to read/write (max 64)
//

#define USB_REQUEST_ION_RESET_DEVICE	0	// Warm reboot Edgeport, retaining USB address
#define USB_REQUEST_ION_GET_EPIC_DESC	1	// Get Edgeport Compatibility Descriptor
// unused				2	// Unused, available
#define USB_REQUEST_ION_READ_RAM	3	// Read  EdgePort RAM at specified addr
#define USB_REQUEST_ION_WRITE_RAM	4	// Write EdgePort RAM at specified addr
#define USB_REQUEST_ION_READ_ROM	5	// Read  EdgePort ROM at specified addr
#define USB_REQUEST_ION_WRITE_ROM	6	// Write EdgePort ROM at specified addr
#define USB_REQUEST_ION_EXEC_DL_CODE	7	// Begin execution of RAM-based download
						// code by jumping to address in wIndex:wValue
//					8	// Unused, available
#define USB_REQUEST_ION_ENABLE_SUSPEND	9	// Enable/Disable suspend feature
						// (wValue != 0: Enable; wValue = 0: Disable)

#define USB_REQUEST_ION_SEND_IOSP	10	// Send an IOSP command to the edgeport over the control pipe
#define USB_REQUEST_ION_RECV_IOSP	11	// Receive an IOSP command from the edgeport over the control pipe


#define USB_REQUEST_ION_DIS_INT_TIMER	0x80	// Sent to Axiohm to enable/ disable
						// interrupt token timer
						// wValue = 1, enable (default)
						// wValue = 0, disable

//
// Define parameter values for our vendor-specific commands
//

//
// Edgeport Compatibility Descriptor
//
// This descriptor is only returned by Edgeport-compatible devices
// supporting the EPiC spec. True ION devices do not return this
// descriptor, but instead return STALL on receipt of the
// GET_EPIC_DESC command. The driver interprets a STALL to mean that
// this is a "real" Edgeport.
//

struct edge_compatibility_bits {
	// This __u32 defines which Vendor-specific commands/functionality
	// the device supports on the default EP0 pipe.

	__u32	VendEnableSuspend	:  1;	// 0001 Set if device supports ION_ENABLE_SUSPEND
	__u32	VendUnused		: 31;	// Available for future expansion, must be 0

	// This __u32 defines which IOSP commands are supported over the
	// bulk pipe EP1.

											// xxxx Set if device supports:
	__u32	IOSPOpen		:  1;	// 0001	OPEN / OPEN_RSP (Currently must be 1)
	__u32	IOSPClose		:  1;	// 0002	CLOSE
	__u32	IOSPChase		:  1;	// 0004	CHASE / CHASE_RSP
	__u32	IOSPSetRxFlow		:  1;	// 0008	SET_RX_FLOW
	__u32	IOSPSetTxFlow		:  1;	// 0010	SET_TX_FLOW
	__u32	IOSPSetXChar		:  1;	// 0020	SET_XON_CHAR/SET_XOFF_CHAR
	__u32	IOSPRxCheck		:  1;	// 0040	RX_CHECK_REQ/RX_CHECK_RSP
	__u32	IOSPSetClrBreak		:  1;	// 0080	SET_BREAK/CLEAR_BREAK
	__u32	IOSPWriteMCR		:  1;	// 0100	MCR register writes (set/clr DTR/RTS)
	__u32	IOSPWriteLCR		:  1;	// 0200	LCR register writes (wordlen/stop/parity)
	__u32	IOSPSetBaudRate		:  1;	// 0400	setting Baud rate (writes to LCR.80h and DLL/DLM register)
	__u32	IOSPDisableIntPipe	:  1;	// 0800 Do not use the interrupt pipe for TxCredits or RxButesAvailable
	__u32	IOSPRxDataAvail		:  1;   // 1000 Return status of RX Fifo (Data available in Fifo)
	__u32	IOSPTxPurge		:  1;	// 2000 Purge TXBuffer and/or Fifo in Edgeport hardware
	__u32	IOSPUnused		: 18;	// Available for future expansion, must be 0

	// This __u32 defines which 'general' features are supported

	__u32	TrueEdgeport		:  1;	// 0001	Set if device is a 'real' Edgeport
											// (Used only by driver, NEVER set by an EPiC device)
	__u32	GenUnused		: 31;	// Available for future expansion, must be 0
};

#define EDGE_COMPATIBILITY_MASK0	0x0001
#define EDGE_COMPATIBILITY_MASK1	0x3FFF
#define EDGE_COMPATIBILITY_MASK2	0x0001

struct edge_compatibility_descriptor {
	__u8	Length;				// Descriptor Length (per USB spec)
	__u8	DescType;			// Descriptor Type (per USB spec, =DEVICE type)
	__u8	EpicVer;			// Version of EPiC spec supported
						// (Currently must be 1)
	__u8	NumPorts;			// Number of serial ports supported
	__u8	iDownloadFile;			// Index of string containing download code filename
						// 0=no download, FF=download compiled into driver.
	__u8	Unused[3];			// Available for future expansion, must be 0
						// (Currently must be 0).
	__u8	MajorVersion;			// Firmware version: xx.
	__u8	MinorVersion;			//  yy.
	__le16	BuildNumber;			//  zzzz (LE format)

	// The following structure contains __u32s, with each bit
	// specifying whether the EPiC device supports the given
	// command or functionality.
	struct edge_compatibility_bits	Supports;
};

// Values for iDownloadFile
#define	EDGE_DOWNLOAD_FILE_NONE		0	// No download requested
#define	EDGE_DOWNLOAD_FILE_INTERNAL	0xFF	// Download the file compiled into driver (930 version)
#define	EDGE_DOWNLOAD_FILE_I930		0xFF	// Download the file compiled into driver (930 version)
#define	EDGE_DOWNLOAD_FILE_80251	0xFE	// Download the file compiled into driver (80251 version)



/*
 *	Special addresses for READ/WRITE_RAM/ROM
 */

// Version 1 (original) format of DeviceParams
#define	EDGE_MANUF_DESC_ADDR_V1		0x00FF7F00
#define	EDGE_MANUF_DESC_LEN_V1		sizeof(EDGE_MANUF_DESCRIPTOR_V1)

// Version 2 format of DeviceParams. This format is longer (3C0h)
// and starts lower in memory, at the uppermost 1K in ROM.
#define	EDGE_MANUF_DESC_ADDR		0x00FF7C00
#define	EDGE_MANUF_DESC_LEN		sizeof(struct edge_manuf_descriptor)

// Boot params descriptor
#define	EDGE_BOOT_DESC_ADDR		0x00FF7FC0
#define	EDGE_BOOT_DESC_LEN		sizeof(struct edge_boot_descriptor)

// Define the max block size that may be read or written
// in a read/write RAM/ROM command.
#define	MAX_SIZE_REQ_ION_READ_MEM	((__u16)64)
#define	MAX_SIZE_REQ_ION_WRITE_MEM	((__u16)64)


//
// Notes for the following two ION vendor-specific param descriptors:
//
//	1.	These have a standard USB descriptor header so they look like a
//		normal descriptor.
//	2.	Any strings in the structures are in USB-defined string
//		descriptor format, so that they may be separately retrieved,
//		if necessary, with a minimum of work on the 930. This also
//		requires them to be in UNICODE format, which, for English at
//		least, simply means extending each __u8 into a __u16.
//	3.	For all fields, 00 means 'uninitialized'.
//	4.	All unused areas should be set to 00 for future expansion.
//

// This structure is ver 2 format. It contains ALL USB descriptors as
// well as the configuration parameters that were in the original V1
// structure. It is NOT modified when new boot code is downloaded; rather,
// these values are set or modified by manufacturing. It is located at
// xC00-xFBF (length 3C0h) in the ROM.
// This structure is a superset of the v1 structure and is arranged so
// that all of the v1 fields remain at the same address. We are just
// adding more room to the front of the structure to hold the descriptors.
//
// The actual contents of this structure are defined in a 930 assembly
// file, converted to a binary image, and then written by the serialization
// program. The C definition of this structure just defines a dummy
// area for general USB descriptors and the descriptor tables (the root
// descriptor starts at xC00). At the bottom of the structure are the
// fields inherited from the v1 structure.

#define MAX_SERIALNUMBER_LEN	12
#define MAX_ASSEMBLYNUMBER_LEN	14

struct edge_manuf_descriptor {

	__u16	RootDescTable[0x10];			// C00 Root of descriptor tables (just a placeholder)
	__u8	DescriptorArea[0x2E0];			// C20 Descriptors go here, up to 2E0h (just a placeholder)

							//     Start of v1-compatible section
	__u8	Length;					// F00 Desc length for what follows, per USB (= C0h )
	__u8	DescType;				// F01 Desc type, per USB (=DEVICE type)
	__u8	DescVer;				// F02 Desc version/format (currently 2)
	__u8	NumRootDescEntries;			// F03 # entries in RootDescTable

	__u8	RomSize;				// F04 Size of ROM/E2PROM in K
	__u8	RamSize;				// F05 Size of external RAM in K
	__u8	CpuRev;					// F06 CPU revision level (chg only if s/w visible)
	__u8	BoardRev;				// F07 PCB revision level (chg only if s/w visible)

	__u8	NumPorts;				// F08 Number of ports
	__u8	DescDate[3];				// F09 MM/DD/YY when descriptor template was compiler,
							//     so host can track changes to USB-only descriptors.

	__u8	SerNumLength;				// F0C USB string descriptor len
	__u8	SerNumDescType;				// F0D USB descriptor type (=STRING type)
	__le16	SerialNumber[MAX_SERIALNUMBER_LEN];	// F0E "01-01-000100" Unicode Serial Number

	__u8	AssemblyNumLength;			// F26 USB string descriptor len
	__u8	AssemblyNumDescType;			// F27 USB descriptor type (=STRING type)
	__le16	AssemblyNumber[MAX_ASSEMBLYNUMBER_LEN];	// F28 "350-1000-01-A " assembly number

	__u8	OemAssyNumLength;			// F44 USB string descriptor len
	__u8	OemAssyNumDescType;			// F45 USB descriptor type (=STRING type)
	__le16	OemAssyNumber[MAX_ASSEMBLYNUMBER_LEN];	// F46 "xxxxxxxxxxxxxx" OEM assembly number

	__u8	ManufDateLength;			// F62 USB string descriptor len
	__u8	ManufDateDescType;			// F63 USB descriptor type (=STRING type)
	__le16	ManufDate[6];				// F64 "MMDDYY" manufacturing date

	__u8	Reserved3[0x4D];			// F70 -- unused, set to 0 --

	__u8	UartType;				// FBD Uart Type
	__u8	IonPid;					// FBE Product ID, == LSB of USB DevDesc.PID
							//      (Note: Edgeport/4s before 11/98 will have
							//       00 here instead of 01)
	__u8	IonConfig;				// FBF Config byte for ION manufacturing use
							// FBF end of structure, total len = 3C0h

};


#define MANUF_DESC_VER_1	1	// Original definition of MANUF_DESC
#define MANUF_DESC_VER_2	2	// Ver 2, starts at xC00h len 3C0h


// Uart Types
// Note: Since this field was added only recently, all Edgeport/4 units
// shipped before 11/98 will have 00 in this field. Therefore,
// both 00 and 01 values mean '654.
#define MANUF_UART_EXAR_654_EARLY	0	// Exar 16C654 in Edgeport/4s before 11/98
#define MANUF_UART_EXAR_654		1	// Exar 16C654
#define MANUF_UART_EXAR_2852		2	// Exar 16C2852

//
// Note: The CpuRev and BoardRev values do not conform to manufacturing
// revisions; they are to be incremented only when the CPU or hardware
// changes in a software-visible way, such that the 930 software or
// the host driver needs to handle the hardware differently.
//

// Values of bottom 5 bits of CpuRev & BoardRev for
// Implementation 0 (ie, 930-based)
#define	MANUF_CPU_REV_AD4		1	// 930 AD4, with EP1 Rx bug (needs RXSPM)
#define	MANUF_CPU_REV_AD5		2	// 930 AD5, with above bug (supposedly) fixed
#define	MANUF_CPU_80251			0x20	// Intel 80251


#define MANUF_BOARD_REV_A		1	// Original version, == Manuf Rev A
#define MANUF_BOARD_REV_B		2	// Manuf Rev B, wakeup interrupt works
#define MANUF_BOARD_REV_C		3	// Manuf Rev C, 2/4 ports, rs232/rs422
#define MANUF_BOARD_REV_GENERATION_2	0x20	// Second generaiton edgeport


// Values of bottom 5 bits of CpuRev & BoardRev for
// Implementation 1 (ie, 251+Netchip-based)
#define	MANUF_CPU_REV_1			1	// C251TB Rev 1 (Need actual Intel rev here)

#define MANUF_BOARD_REV_A		1	// First rev of 251+Netchip design

#define	MANUF_SERNUM_LENGTH		sizeof(((struct edge_manuf_descriptor *)0)->SerialNumber)
#define	MANUF_ASSYNUM_LENGTH		sizeof(((struct edge_manuf_descriptor *)0)->AssemblyNumber)
#define	MANUF_OEMASSYNUM_LENGTH		sizeof(((struct edge_manuf_descriptor *)0)->OemAssyNumber)
#define	MANUF_MANUFDATE_LENGTH		sizeof(((struct edge_manuf_descriptor *)0)->ManufDate)

#define	MANUF_ION_CONFIG_DIAG_NO_LOOP	0x20	// As below but no ext loopback test
#define	MANUF_ION_CONFIG_DIAG		0x40	// 930 based device: 1=Run h/w diags, 0=norm
						// TIUMP Device    : 1=IONSERIAL needs to run Final Test
#define	MANUF_ION_CONFIG_MASTER		0x80	// 930 based device:  1=Master mode, 0=Normal
						// TIUMP Device    :  1=First device on a multi TIUMP Device

//
// This structure describes parameters for the boot code, and
// is programmed along with new boot code. These are values
// which are specific to a given build of the boot code. It
// is exactly 64 bytes long and is fixed at address FF:xFC0
// - FF:xFFF. Note that the 930-mandated UCONFIG bytes are
// included in this structure.
//
struct edge_boot_descriptor {
	__u8		Length;			// C0 Desc length, per USB (= 40h)
	__u8		DescType;		// C1 Desc type, per USB (= DEVICE type)
	__u8		DescVer;		// C2 Desc version/format
	__u8		Reserved1;		// C3 -- unused, set to 0 --

	__le16		BootCodeLength;		// C4 Boot code goes from FF:0000 to FF:(len-1)
						//	  (LE format)

	__u8		MajorVersion;		// C6 Firmware version: xx.
	__u8		MinorVersion;		// C7			yy.
	__le16		BuildNumber;		// C8			zzzz (LE format)

	__u16		EnumRootDescTable;	// CA Root of ROM-based descriptor table
	__u8		NumDescTypes;		// CC Number of supported descriptor types

	__u8		Reserved4;		// CD Fix Compiler Packing

	__le16		Capabilities;		// CE-CF Capabilities flags (LE format)
	__u8		Reserved2[0x28];	// D0 -- unused, set to 0 --
	__u8		UConfig0;		// F8 930-defined CPU configuration byte 0
	__u8		UConfig1;		// F9 930-defined CPU configuration byte 1
	__u8		Reserved3[6];		// FA -- unused, set to 0 --
						// FF end of structure, total len = 80
};


#define BOOT_DESC_VER_1		1	// Original definition of BOOT_PARAMS
#define BOOT_DESC_VER_2		2	// 2nd definition, descriptors not included in boot


	// Capabilities flags

#define	BOOT_CAP_RESET_CMD	0x0001	// If set, boot correctly supports ION_RESET_DEVICE


/************************************************************************
                 T I   U M P   D E F I N I T I O N S
 ***********************************************************************/

// Chip definitions in I2C
#define UMP5152			0x52
#define UMP3410			0x10


//************************************************************************
//	TI I2C Format Definitions
//************************************************************************
#define I2C_DESC_TYPE_INFO_BASIC	0x01
#define I2C_DESC_TYPE_FIRMWARE_BASIC	0x02
#define I2C_DESC_TYPE_DEVICE		0x03
#define I2C_DESC_TYPE_CONFIG		0x04
#define I2C_DESC_TYPE_STRING		0x05
#define I2C_DESC_TYPE_FIRMWARE_AUTO	0x07	// for 3410 download
#define I2C_DESC_TYPE_CONFIG_KLUDGE	0x14	// for 3410
#define I2C_DESC_TYPE_WATCHPORT_VERSION	0x15	// firmware version number for watchport
#define I2C_DESC_TYPE_WATCHPORT_CALIBRATION_DATA 0x16	// Watchport Calibration Data

#define I2C_DESC_TYPE_FIRMWARE_BLANK	0xf2

// Special section defined by ION
#define I2C_DESC_TYPE_ION		0	// Not defined by TI


struct ti_i2c_desc {
	__u8	Type;			// Type of descriptor
	__le16	Size;			// Size of data only not including header
	__u8	CheckSum;		// Checksum (8 bit sum of data only)
	__u8	Data[];		// Data starts here
} __attribute__((packed));

// for 5152 devices only (type 2 record)
// for 3410 the version is stored in the WATCHPORT_FIRMWARE_VERSION descriptor
struct ti_i2c_firmware_rec {
	__u8	Ver_Major;		// Firmware Major version number
	__u8	Ver_Minor;		// Firmware Minor version number
	__u8	Data[];		// Download starts here
} __attribute__((packed));


struct watchport_firmware_version {
// Added 2 bytes for version number
	__u8	Version_Major;		//  Download Version (for Watchport)
	__u8	Version_Minor;
} __attribute__((packed));


// Structure of header of download image in fw_down.h
struct ti_i2c_image_header {
	__le16	Length;
	__u8	CheckSum;
} __attribute__((packed));

struct ti_basic_descriptor {
	__u8	Power;		// Self powered
				// bit 7: 1 - power switching supported
				//        0 - power switching not supported
				//
				// bit 0: 1 - self powered
				//        0 - bus powered
				//
				//
	__u16	HubVid;		// VID HUB
	__u16	HubPid;		// PID HUB
	__u16	DevPid;		// PID Edgeport
	__u8	HubTime;	// Time for power on to power good
	__u8	HubCurrent;	// HUB Current = 100ma
} __attribute__((packed));


// CPU / Board Rev Definitions
#define TI_CPU_REV_5052			2	// 5052 based edgeports
#define TI_CPU_REV_3410			3	// 3410 based edgeports

#define TI_BOARD_REV_TI_EP		0	// Basic ti based edgeport
#define TI_BOARD_REV_COMPACT		1	// Compact board
#define TI_BOARD_REV_WATCHPORT		2	// Watchport


#define TI_GET_CPU_REVISION(x)		(__u8)((((x)>>4)&0x0f))
#define TI_GET_BOARD_REVISION(x)	(__u8)(((x)&0x0f))

#define TI_I2C_SIZE_MASK		0x1f  // 5 bits
#define TI_GET_I2C_SIZE(x)		((((x) & TI_I2C_SIZE_MASK)+1)*256)

#define TI_MAX_I2C_SIZE			(16 * 1024)

#define TI_MANUF_VERSION_0		0

// IonConig2 flags
#define TI_CONFIG2_RS232		0x01
#define TI_CONFIG2_RS422		0x02
#define TI_CONFIG2_RS485		0x04
#define TI_CONFIG2_SWITCHABLE		0x08

#define TI_CONFIG2_WATCHPORT		0x10


struct edge_ti_manuf_descriptor {
	__u8 IonConfig;		//  Config byte for ION manufacturing use
	__u8 IonConfig2;	//  Expansion
	__u8 Version;		//  Version
	__u8 CpuRev_BoardRev;	//  CPU revision level (0xF0) and Board Rev Level (0x0F)
	__u8 NumPorts;		//  Number of ports	for this UMP
	__u8 NumVirtualPorts;	//  Number of Virtual ports
	__u8 HubConfig1;	//  Used to configure the Hub
	__u8 HubConfig2;	//  Used to configure the Hub
	__u8 TotalPorts;	//  Total Number of Com Ports for the entire device (All UMPs)
	__u8 Reserved;		//  Reserved
} __attribute__((packed));


#endif		// if !defined(_USBVEND_H)
