/* SPDX-License-Identifier: GPL-2.0 */

/* hc_capbase bitmasks */
/* bits 7:0 - how long is the Capabilities register */
#define HC_LENGTH(p)		XHCI_HC_LENGTH(p)
/* bits 31:16	*/
#define HC_VERSION(p)		(((p) >> 16) & 0xffff)

/* HCSPARAMS1 - hcs_params1 - bitmasks */
/* bits 0:7, Max Device Slots */
#define HCS_MAX_SLOTS(p)	(((p) >> 0) & 0xff)
#define HCS_SLOTS_MASK		0xff
/* bits 8:18, Max Interrupters */
#define HCS_MAX_INTRS(p)	(((p) >> 8) & 0x7ff)
/* bits 24:31, Max Ports - max value is 0x7F = 127 ports */
#define HCS_MAX_PORTS(p)	(((p) >> 24) & 0x7f)

/* HCSPARAMS2 - hcs_params2 - bitmasks */
/* bits 0:3, frames or uframes that SW needs to queue transactions
 * ahead of the HW to meet periodic deadlines */
#define HCS_IST(p)		(((p) >> 0) & 0xf)
/* bits 4:7, max number of Event Ring segments */
#define HCS_ERST_MAX(p)		(((p) >> 4) & 0xf)
/* bits 21:25 Hi 5 bits of Scratchpad buffers SW must allocate for the HW */
/* bit 26 Scratchpad restore - for save/restore HW state - not used yet */
/* bits 27:31 Lo 5 bits of Scratchpad buffers SW must allocate for the HW */
#define HCS_MAX_SCRATCHPAD(p)   ((((p) >> 16) & 0x3e0) | (((p) >> 27) & 0x1f))

/* HCSPARAMS3 - hcs_params3 - bitmasks */
/* bits 0:7, Max U1 to U0 latency for the roothub ports */
#define HCS_U1_LATENCY(p)	(((p) >> 0) & 0xff)
/* bits 16:31, Max U2 to U0 latency for the roothub ports */
#define HCS_U2_LATENCY(p)	(((p) >> 16) & 0xffff)

/* HCCPARAMS - hcc_params - bitmasks */
/* true: HC can use 64-bit address pointers */
#define HCC_64BIT_ADDR(p)	((p) & (1 << 0))
/* true: HC can do bandwidth negotiation */
#define HCC_BANDWIDTH_NEG(p)	((p) & (1 << 1))
/* true: HC uses 64-byte Device Context structures
 * FIXME 64-byte context structures aren't supported yet.
 */
#define HCC_64BYTE_CONTEXT(p)	((p) & (1 << 2))
/* true: HC has port power switches */
#define HCC_PPC(p)		((p) & (1 << 3))
/* true: HC has port indicators */
#define HCS_INDICATOR(p)	((p) & (1 << 4))
/* true: HC has Light HC Reset Capability */
#define HCC_LIGHT_RESET(p)	((p) & (1 << 5))
/* true: HC supports latency tolerance messaging */
#define HCC_LTC(p)		((p) & (1 << 6))
/* true: no secondary Stream ID Support */
#define HCC_NSS(p)		((p) & (1 << 7))
/* true: HC supports Stopped - Short Packet */
#define HCC_SPC(p)		((p) & (1 << 9))
/* true: HC has Contiguous Frame ID Capability */
#define HCC_CFC(p)		((p) & (1 << 11))
/* Max size for Primary Stream Arrays - 2^(n+1), where n is bits 12:15 */
#define HCC_MAX_PSA(p)		(1 << ((((p) >> 12) & 0xf) + 1))
/* Extended Capabilities pointer from PCI base - section 5.3.6 */
#define HCC_EXT_CAPS(p)		XHCI_HCC_EXT_CAPS(p)

#define CTX_SIZE(_hcc)		(HCC_64BYTE_CONTEXT(_hcc) ? 64 : 32)

/* db_off bitmask - bits 31:2 Doorbell Array Offset */
#define	DBOFF_MASK	(0xfffffffc)

/* run_regs_off bitmask - bits 0:4 reserved */
#define	RTSOFF_MASK	(~0x1f)

/* HCCPARAMS2 - hcc_params2 - bitmasks */
/* true: HC supports U3 entry Capability */
#define	HCC2_U3C(p)		((p) & (1 << 0))
/* true: HC supports Configure endpoint command Max exit latency too large */
#define	HCC2_CMC(p)		((p) & (1 << 1))
/* true: HC supports Force Save context Capability */
#define	HCC2_FSC(p)		((p) & (1 << 2))
/* true: HC supports Compliance Transition Capability */
#define	HCC2_CTC(p)		((p) & (1 << 3))
/* true: HC support Large ESIT payload Capability > 48k */
#define	HCC2_LEC(p)		((p) & (1 << 4))
/* true: HC support Configuration Information Capability */
#define	HCC2_CIC(p)		((p) & (1 << 5))
/* true: HC support Extended TBC Capability, Isoc burst count > 65535 */
#define	HCC2_ETC(p)		((p) & (1 << 6))
/* true: HC support Extended TBC TRB Status Capability */
#define HCC2_ETC_TSC(p)         ((p) & (1 << 7))
/* true: HC support Get/Set Extended Property Capability */
#define HCC2_GSC(p)             ((p) & (1 << 8))
/* true: HC support Virtualization Based Trusted I/O Capability */
#define HCC2_VTC(p)             ((p) & (1 << 9))
/* true: HC support Double BW on a eUSB2 HS ISOC EP */
#define HCC2_EUSB2_DIC(p)       ((p) & (1 << 11))
