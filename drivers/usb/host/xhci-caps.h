/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xHCI Host Controller Capability Registers.
 * xHCI Specification Section 5.3, Revision 1.2.
 */

#include <linux/bits.h>

/* hc_capbase - bitmasks */
/* bits 7:0 - Capability Registers Length */
#define HC_LENGTH(p)		((p) & 0xff)
/* bits 15:8 - Rsvd */
/* bits 31:16 - Host Controller Interface Version Number */
#define HC_VERSION(p)		(((p) >> 16) & 0xffff)

/* HCSPARAMS1 - hcs_params1 - bitmasks */
/* bits 7:0 - Number of Device Slots */
#define HCS_MAX_SLOTS(p)	(((p) >> 0) & 0xff)
#define HCS_SLOTS_MASK		0xff
/* bits 18:8 - Number of Interrupters, max values is 1024 */
#define HCS_MAX_INTRS(p)	(((p) >> 8) & 0x7ff)
/* bits 31:24, Max Ports - max value is 255 */
#define HCS_MAX_PORTS(p)	(((p) >> 24) & 0xff)

/* HCSPARAMS2 - hcs_params2 - bitmasks */
/*
 * bits 3:0 - Isochronous Scheduling Threshold, frames or uframes that SW
 * needs to queue transactions ahead of the HW to meet periodic deadlines.
 * - Bits 2:0: Threshold value
 * - Bit 3: Unit indicator
 *   - '1': Threshold in Frames
 *   - '0': Threshold in Microframes (uframes)
 * Note: 1 Frame = 8 Microframes
 * xHCI specification section 5.3.4.
 */
#define HCS_IST_VALUE(p)	((p) & 0x7)
#define HCS_IST_UNIT		BIT(3)
/* bits 7:4 - Event Ring Segment Table Max, 2^(n) */
#define HCS_ERST_MAX(p)		(((p) >> 4) & 0xf)
/* bits 20:8 - Rsvd */
/* bits 25:21 - Max Scratchpad Buffers (Hi), 5 Most significant bits */
#define HCS_MAX_SP_HI(p)	(((p) >> 21) & 0x1f)
/* bit 26 - Scratchpad restore, for save/restore HW state */
/* bits 31:27 - Max Scratchpad Buffers (Lo), 5 Least significant bits */
#define HCS_MAX_SP_LO(p)	(((p) >> 27) & 0x1f)
#define HCS_MAX_SCRATCHPAD(p)	(HCS_MAX_SP_HI(p) << 5 | HCS_MAX_SP_LO(p))

/* HCSPARAMS3 - hcs_params3 - bitmasks */
/* bits 7:0 - U1 Device Exit Latency, Max U1 to U0 latency for the roothub ports */
#define HCS_U1_LATENCY(p)	(((p) >> 0) & 0xff)
/* bits 15:8 - Rsvd */
/* bits 31:16 - U2 Device Exit Latency, Max U2 to U0 latency for the roothub ports */
#define HCS_U2_LATENCY(p)	(((p) >> 16) & 0xffff)

/* HCCPARAMS1 - hcc_params - bitmasks */
/* bit 0 - 64-bit Addressing Capability */
#define HCC_64BIT_ADDR		BIT(0)
/* bit 1 - BW Negotiation Capability */
#define HCC_BANDWIDTH_NEG	BIT(1)
/* bit 2 - Context Size */
#define HCC_64BYTE_CONTEXT	BIT(2)
#define CTX_SIZE(_hcc)		(_hcc & HCC_64BYTE_CONTEXT ? 64 : 32)
/* bit 3 - Port Power Control */
#define HCC_PPC			BIT(3)
/* bit 4 - Port Indicators */
#define HCS_INDICATOR		BIT(4)
/* bit 5 - Light HC Reset Capability */
#define HCC_LIGHT_RESET		BIT(5)
/* bit 6 - Latency Tolerance Messaging Capability */
#define HCC_LTC			BIT(6)
/* bit 7 - No Secondary Stream ID Support */
#define HCC_NSS			BIT(7)
/* bit 8 - Parse All Event Data */
/* bit 9 - Short Packet Capability */
#define HCC_SPC			BIT(9)
/* bit 10 - Stopped EDTLA Capability */
/* bit 11 - Contiguous Frame ID Capability */
#define HCC_CFC			BIT(11)
/* bits 15:12 - Max size for Primary Stream Arrays, 2^(n+1) */
#define HCC_MAX_PSA(p)		(1 << ((((p) >> 12) & 0xf) + 1))
/* bits 31:16 - xHCI Extended Capabilities Pointer, from PCI base: 2^(n) */
#define HCC_EXT_CAPS(p)		(((p) >> 16) & 0xffff)

/* DBOFF - db_off - bitmasks */
/* bits 1:0 - Rsvd */
/* bits 31:2 - Doorbell Array Offset */
#define	DBOFF_MASK	(0xfffffffc)

/* RTSOFF - run_regs_off - bitmasks */
/* bits 4:0 - Rsvd */
/* bits 31:5 - Runtime Register Space Offse */
#define	RTSOFF_MASK	(~0x1f)

/* HCCPARAMS2 - hcc_params2 - bitmasks */
/* bit 0 - U3 Entry Capability */
#define	HCC2_U3C		BIT(0)
/* bit 1 - Configure Endpoint Command Max Exit Latency Too Large Capability */
#define	HCC2_CMC		BIT(1)
/* bit 2 - Force Save Context Capabilitu */
#define	HCC2_FSC		BIT(2)
/* bit 3 - Compliance Transition Capability, false: compliance is enabled by default */
#define	HCC2_CTC		BIT(3)
/* bit 4 - Large ESIT Payload Capability, true: HC support ESIT payload > 48k */
#define	HCC2_LEC		BIT(4)
/* bit 5 - Configuration Information Capability */
#define	HCC2_CIC		BIT(5)
/* bit 6 - Extended TBC Capability, true: Isoc burst count > 65535 */
#define	HCC2_ETC		BIT(6)
/* bit 7 - Extended TBC TRB Status Capability */
#define HCC2_ETC_TSC		BIT(7)
/* bit 8 - Get/Set Extended Property Capability */
#define HCC2_GSC		BIT(8)
/* bit 9 - Virtualization Based Trusted I/O Capability */
#define HCC2_VTC		BIT(9)
/* bit 10 - Rsvd */
/* bit 11 - HC support Double BW on a eUSB2 HS ISOC EP */
#define HCC2_EUSB2_DIC		BIT(11)
/* bit 12 - HC support eUSB2V2 capability */
#define HCC2_E2V2C		BIT(12)
/* bits 31:13 - Rsvd */
