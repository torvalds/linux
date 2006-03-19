/*
 * NetChip 2280 high/full speed USB device controller.
 * Unlike many such controllers, this one talks PCI.
 */

/*
 * Copyright (C) 2002 NetChip Technology, Inc. (http://www.netchip.com)
 * Copyright (C) 2003 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*-------------------------------------------------------------------------*/

/* NET2280 MEMORY MAPPED REGISTERS
 *
 * The register layout came from the chip documentation, and the bit
 * number definitions were extracted from chip specification.
 *
 * Use the shift operator ('<<') to build bit masks, with readl/writel
 * to access the registers through PCI.
 */

/* main registers, BAR0 + 0x0000 */
struct net2280_regs {
	// offset 0x0000
	u32		devinit;
#define     LOCAL_CLOCK_FREQUENCY                               8
#define     FORCE_PCI_RESET                                     7
#define     PCI_ID                                              6
#define     PCI_ENABLE                                          5
#define     FIFO_SOFT_RESET                                     4
#define     CFG_SOFT_RESET                                      3
#define     PCI_SOFT_RESET                                      2
#define     USB_SOFT_RESET                                      1
#define     M8051_RESET                                         0
	u32		eectl;
#define     EEPROM_ADDRESS_WIDTH                                23
#define     EEPROM_CHIP_SELECT_ACTIVE                           22
#define     EEPROM_PRESENT                                      21
#define     EEPROM_VALID                                        20
#define     EEPROM_BUSY                                         19
#define     EEPROM_CHIP_SELECT_ENABLE                           18
#define     EEPROM_BYTE_READ_START                              17
#define     EEPROM_BYTE_WRITE_START                             16
#define     EEPROM_READ_DATA                                    8
#define     EEPROM_WRITE_DATA                                   0
	u32		eeclkfreq;
	u32		_unused0;
	// offset 0x0010

	u32		pciirqenb0;		/* interrupt PCI master ... */
#define     SETUP_PACKET_INTERRUPT_ENABLE                       7
#define     ENDPOINT_F_INTERRUPT_ENABLE                         6
#define     ENDPOINT_E_INTERRUPT_ENABLE                         5
#define     ENDPOINT_D_INTERRUPT_ENABLE                         4
#define     ENDPOINT_C_INTERRUPT_ENABLE                         3
#define     ENDPOINT_B_INTERRUPT_ENABLE                         2
#define     ENDPOINT_A_INTERRUPT_ENABLE                         1
#define     ENDPOINT_0_INTERRUPT_ENABLE                         0
	u32		pciirqenb1;
#define     PCI_INTERRUPT_ENABLE                                31
#define     POWER_STATE_CHANGE_INTERRUPT_ENABLE                 27
#define     PCI_ARBITER_TIMEOUT_INTERRUPT_ENABLE                26
#define     PCI_PARITY_ERROR_INTERRUPT_ENABLE                   25
#define     PCI_MASTER_ABORT_RECEIVED_INTERRUPT_ENABLE          20
#define     PCI_TARGET_ABORT_RECEIVED_INTERRUPT_ENABLE          19
#define     PCI_TARGET_ABORT_ASSERTED_INTERRUPT_ENABLE          18
#define     PCI_RETRY_ABORT_INTERRUPT_ENABLE                    17
#define     PCI_MASTER_CYCLE_DONE_INTERRUPT_ENABLE              16
#define     GPIO_INTERRUPT_ENABLE                               13
#define     DMA_D_INTERRUPT_ENABLE                              12
#define     DMA_C_INTERRUPT_ENABLE                              11
#define     DMA_B_INTERRUPT_ENABLE                              10
#define     DMA_A_INTERRUPT_ENABLE                              9
#define     EEPROM_DONE_INTERRUPT_ENABLE                        8
#define     VBUS_INTERRUPT_ENABLE                               7
#define     CONTROL_STATUS_INTERRUPT_ENABLE                     6
#define     ROOT_PORT_RESET_INTERRUPT_ENABLE                    4
#define     SUSPEND_REQUEST_INTERRUPT_ENABLE                    3
#define     SUSPEND_REQUEST_CHANGE_INTERRUPT_ENABLE             2
#define     RESUME_INTERRUPT_ENABLE                             1
#define     SOF_INTERRUPT_ENABLE                                0
	u32		cpu_irqenb0;		/* ... or onboard 8051 */
#define     SETUP_PACKET_INTERRUPT_ENABLE                       7
#define     ENDPOINT_F_INTERRUPT_ENABLE                         6
#define     ENDPOINT_E_INTERRUPT_ENABLE                         5
#define     ENDPOINT_D_INTERRUPT_ENABLE                         4
#define     ENDPOINT_C_INTERRUPT_ENABLE                         3
#define     ENDPOINT_B_INTERRUPT_ENABLE                         2
#define     ENDPOINT_A_INTERRUPT_ENABLE                         1
#define     ENDPOINT_0_INTERRUPT_ENABLE                         0
	u32		cpu_irqenb1;
#define     CPU_INTERRUPT_ENABLE                                31
#define     POWER_STATE_CHANGE_INTERRUPT_ENABLE                 27
#define     PCI_ARBITER_TIMEOUT_INTERRUPT_ENABLE                26
#define     PCI_PARITY_ERROR_INTERRUPT_ENABLE                   25
#define     PCI_INTA_INTERRUPT_ENABLE                           24
#define     PCI_PME_INTERRUPT_ENABLE                            23
#define     PCI_SERR_INTERRUPT_ENABLE                           22
#define     PCI_PERR_INTERRUPT_ENABLE                           21
#define     PCI_MASTER_ABORT_RECEIVED_INTERRUPT_ENABLE          20
#define     PCI_TARGET_ABORT_RECEIVED_INTERRUPT_ENABLE          19
#define     PCI_RETRY_ABORT_INTERRUPT_ENABLE                    17
#define     PCI_MASTER_CYCLE_DONE_INTERRUPT_ENABLE              16
#define     GPIO_INTERRUPT_ENABLE                               13
#define     DMA_D_INTERRUPT_ENABLE                              12
#define     DMA_C_INTERRUPT_ENABLE                              11
#define     DMA_B_INTERRUPT_ENABLE                              10
#define     DMA_A_INTERRUPT_ENABLE                              9
#define     EEPROM_DONE_INTERRUPT_ENABLE                        8
#define     VBUS_INTERRUPT_ENABLE                               7
#define     CONTROL_STATUS_INTERRUPT_ENABLE                     6
#define     ROOT_PORT_RESET_INTERRUPT_ENABLE                    4
#define     SUSPEND_REQUEST_INTERRUPT_ENABLE                    3
#define     SUSPEND_REQUEST_CHANGE_INTERRUPT_ENABLE             2
#define     RESUME_INTERRUPT_ENABLE                             1
#define     SOF_INTERRUPT_ENABLE                                0

	// offset 0x0020
	u32		_unused1;
	u32		usbirqenb1;
#define     USB_INTERRUPT_ENABLE                                31
#define     POWER_STATE_CHANGE_INTERRUPT_ENABLE                 27
#define     PCI_ARBITER_TIMEOUT_INTERRUPT_ENABLE                26
#define     PCI_PARITY_ERROR_INTERRUPT_ENABLE                   25
#define     PCI_INTA_INTERRUPT_ENABLE                           24
#define     PCI_PME_INTERRUPT_ENABLE                            23
#define     PCI_SERR_INTERRUPT_ENABLE                           22
#define     PCI_PERR_INTERRUPT_ENABLE                           21
#define     PCI_MASTER_ABORT_RECEIVED_INTERRUPT_ENABLE          20
#define     PCI_TARGET_ABORT_RECEIVED_INTERRUPT_ENABLE          19
#define     PCI_RETRY_ABORT_INTERRUPT_ENABLE                    17
#define     PCI_MASTER_CYCLE_DONE_INTERRUPT_ENABLE              16
#define     GPIO_INTERRUPT_ENABLE                               13
#define     DMA_D_INTERRUPT_ENABLE                              12
#define     DMA_C_INTERRUPT_ENABLE                              11
#define     DMA_B_INTERRUPT_ENABLE                              10
#define     DMA_A_INTERRUPT_ENABLE                              9
#define     EEPROM_DONE_INTERRUPT_ENABLE                        8
#define     VBUS_INTERRUPT_ENABLE                               7
#define     CONTROL_STATUS_INTERRUPT_ENABLE                     6
#define     ROOT_PORT_RESET_INTERRUPT_ENABLE                    4
#define     SUSPEND_REQUEST_INTERRUPT_ENABLE                    3
#define     SUSPEND_REQUEST_CHANGE_INTERRUPT_ENABLE             2
#define     RESUME_INTERRUPT_ENABLE                             1
#define     SOF_INTERRUPT_ENABLE                                0
	u32		irqstat0;
#define     INTA_ASSERTED                                       12
#define     SETUP_PACKET_INTERRUPT                              7
#define     ENDPOINT_F_INTERRUPT                                6
#define     ENDPOINT_E_INTERRUPT                                5
#define     ENDPOINT_D_INTERRUPT                                4
#define     ENDPOINT_C_INTERRUPT                                3
#define     ENDPOINT_B_INTERRUPT                                2
#define     ENDPOINT_A_INTERRUPT                                1
#define     ENDPOINT_0_INTERRUPT                                0
	u32		irqstat1;
#define     POWER_STATE_CHANGE_INTERRUPT                        27
#define     PCI_ARBITER_TIMEOUT_INTERRUPT                       26
#define     PCI_PARITY_ERROR_INTERRUPT                          25
#define     PCI_INTA_INTERRUPT                                  24
#define     PCI_PME_INTERRUPT                                   23
#define     PCI_SERR_INTERRUPT                                  22
#define     PCI_PERR_INTERRUPT                                  21
#define     PCI_MASTER_ABORT_RECEIVED_INTERRUPT                 20
#define     PCI_TARGET_ABORT_RECEIVED_INTERRUPT                 19
#define     PCI_RETRY_ABORT_INTERRUPT                           17
#define     PCI_MASTER_CYCLE_DONE_INTERRUPT                     16
#define     SOF_DOWN_INTERRUPT                                  14
#define     GPIO_INTERRUPT                                      13
#define     DMA_D_INTERRUPT                                     12
#define     DMA_C_INTERRUPT                                     11
#define     DMA_B_INTERRUPT                                     10
#define     DMA_A_INTERRUPT                                     9
#define     EEPROM_DONE_INTERRUPT                               8
#define     VBUS_INTERRUPT                                      7
#define     CONTROL_STATUS_INTERRUPT                            6
#define     ROOT_PORT_RESET_INTERRUPT                           4
#define     SUSPEND_REQUEST_INTERRUPT                           3
#define     SUSPEND_REQUEST_CHANGE_INTERRUPT                    2
#define     RESUME_INTERRUPT                                    1
#define     SOF_INTERRUPT                                       0
	// offset 0x0030
	u32		idxaddr;
	u32		idxdata;
	u32		fifoctl;
#define     PCI_BASE2_RANGE                                     16
#define     IGNORE_FIFO_AVAILABILITY                            3
#define     PCI_BASE2_SELECT                                    2
#define     FIFO_CONFIGURATION_SELECT                           0
	u32		_unused2;
	// offset 0x0040
	u32		memaddr;
#define     START                                               28
#define     DIRECTION                                           27
#define     FIFO_DIAGNOSTIC_SELECT                              24
#define     MEMORY_ADDRESS                                      0
	u32		memdata0;
	u32		memdata1;
	u32		_unused3;
	// offset 0x0050
	u32		gpioctl;
#define     GPIO3_LED_SELECT                                    12
#define     GPIO3_INTERRUPT_ENABLE                              11
#define     GPIO2_INTERRUPT_ENABLE                              10
#define     GPIO1_INTERRUPT_ENABLE                              9
#define     GPIO0_INTERRUPT_ENABLE                              8
#define     GPIO3_OUTPUT_ENABLE                                 7
#define     GPIO2_OUTPUT_ENABLE                                 6
#define     GPIO1_OUTPUT_ENABLE                                 5
#define     GPIO0_OUTPUT_ENABLE                                 4
#define     GPIO3_DATA                                          3
#define     GPIO2_DATA                                          2
#define     GPIO1_DATA                                          1
#define     GPIO0_DATA                                          0
	u32		gpiostat;
#define     GPIO3_INTERRUPT                                     3
#define     GPIO2_INTERRUPT                                     2
#define     GPIO1_INTERRUPT                                     1
#define     GPIO0_INTERRUPT                                     0
} __attribute__ ((packed));

/* usb control, BAR0 + 0x0080 */
struct net2280_usb_regs {
	// offset 0x0080
	u32		stdrsp;
#define     STALL_UNSUPPORTED_REQUESTS                          31
#define     SET_TEST_MODE                                       16
#define     GET_OTHER_SPEED_CONFIGURATION                       15
#define     GET_DEVICE_QUALIFIER                                14
#define     SET_ADDRESS                                         13
#define     ENDPOINT_SET_CLEAR_HALT                             12
#define     DEVICE_SET_CLEAR_DEVICE_REMOTE_WAKEUP               11
#define     GET_STRING_DESCRIPTOR_2                             10
#define     GET_STRING_DESCRIPTOR_1                             9
#define     GET_STRING_DESCRIPTOR_0                             8
#define     GET_SET_INTERFACE                                   6
#define     GET_SET_CONFIGURATION                               5
#define     GET_CONFIGURATION_DESCRIPTOR                        4
#define     GET_DEVICE_DESCRIPTOR                               3
#define     GET_ENDPOINT_STATUS                                 2
#define     GET_INTERFACE_STATUS                                1
#define     GET_DEVICE_STATUS                                   0
	u32		prodvendid;
#define     PRODUCT_ID                                          16
#define     VENDOR_ID                                           0
	u32		relnum;
	u32		usbctl;
#define     SERIAL_NUMBER_INDEX                                 16
#define     PRODUCT_ID_STRING_ENABLE                            13
#define     VENDOR_ID_STRING_ENABLE                             12
#define     USB_ROOT_PORT_WAKEUP_ENABLE                         11
#define     VBUS_PIN                                            10
#define     TIMED_DISCONNECT                                    9
#define     SUSPEND_IMMEDIATELY                                 7
#define     SELF_POWERED_USB_DEVICE                             6
#define     REMOTE_WAKEUP_SUPPORT                               5
#define     PME_POLARITY                                        4
#define     USB_DETECT_ENABLE                                   3
#define     PME_WAKEUP_ENABLE                                   2
#define     DEVICE_REMOTE_WAKEUP_ENABLE                         1
#define     SELF_POWERED_STATUS                                 0
	// offset 0x0090
	u32		usbstat;
#define     HIGH_SPEED                                          7
#define     FULL_SPEED                                          6
#define     GENERATE_RESUME                                     5
#define     GENERATE_DEVICE_REMOTE_WAKEUP                       4
	u32		xcvrdiag;
#define     FORCE_HIGH_SPEED_MODE                               31
#define     FORCE_FULL_SPEED_MODE                               30
#define     USB_TEST_MODE                                       24
#define     LINE_STATE                                          16
#define     TRANSCEIVER_OPERATION_MODE                          2
#define     TRANSCEIVER_SELECT                                  1
#define     TERMINATION_SELECT                                  0
	u32		setup0123;
	u32		setup4567;
	// offset 0x0090
	u32		_unused0;
	u32		ouraddr;
#define     FORCE_IMMEDIATE                                     7
#define     OUR_USB_ADDRESS                                     0
	u32		ourconfig;
} __attribute__ ((packed));

/* pci control, BAR0 + 0x0100 */
struct net2280_pci_regs {
	// offset 0x0100
	u32		 pcimstctl;
#define     PCI_ARBITER_PARK_SELECT                             13
#define     PCI_MULTI LEVEL_ARBITER                             12
#define     PCI_RETRY_ABORT_ENABLE                              11
#define     DMA_MEMORY_WRITE_AND_INVALIDATE_ENABLE              10
#define     DMA_READ_MULTIPLE_ENABLE                            9
#define     DMA_READ_LINE_ENABLE                                8
#define     PCI_MASTER_COMMAND_SELECT                           6
#define         MEM_READ_OR_WRITE                                   0
#define         IO_READ_OR_WRITE                                    1
#define         CFG_READ_OR_WRITE                                   2
#define     PCI_MASTER_START                                    5
#define     PCI_MASTER_READ_WRITE                               4
#define         PCI_MASTER_WRITE                                    0
#define         PCI_MASTER_READ                                     1
#define     PCI_MASTER_BYTE_WRITE_ENABLES                       0
	u32		 pcimstaddr;
	u32		 pcimstdata;
	u32		 pcimststat;
#define     PCI_ARBITER_CLEAR                                   2
#define     PCI_EXTERNAL_ARBITER                                1
#define     PCI_HOST_MODE                                       0
} __attribute__ ((packed));

/* dma control, BAR0 + 0x0180 ... array of four structs like this,
 * for channels 0..3.  see also struct net2280_dma:  descriptor
 * that can be loaded into some of these registers.
 */
struct net2280_dma_regs {	/* [11.7] */
	// offset 0x0180, 0x01a0, 0x01c0, 0x01e0, 
	u32		dmactl;
#define     DMA_SCATTER_GATHER_DONE_INTERRUPT_ENABLE            25
#define     DMA_CLEAR_COUNT_ENABLE                              21
#define     DESCRIPTOR_POLLING_RATE                             19
#define         POLL_CONTINUOUS                                     0
#define         POLL_1_USEC                                         1
#define         POLL_100_USEC                                       2
#define         POLL_1_MSEC                                         3
#define     DMA_VALID_BIT_POLLING_ENABLE                        18
#define     DMA_VALID_BIT_ENABLE                                17
#define     DMA_SCATTER_GATHER_ENABLE                           16
#define     DMA_OUT_AUTO_START_ENABLE                           4
#define     DMA_PREEMPT_ENABLE                                  3
#define     DMA_FIFO_VALIDATE                                   2
#define     DMA_ENABLE                                          1
#define     DMA_ADDRESS_HOLD                                    0
	u32		dmastat;
#define     DMA_ABORT_DONE_INTERRUPT                            27
#define     DMA_SCATTER_GATHER_DONE_INTERRUPT                   25
#define     DMA_TRANSACTION_DONE_INTERRUPT                      24
#define     DMA_ABORT                                           1
#define     DMA_START                                           0
	u32		_unused0 [2];
	// offset 0x0190, 0x01b0, 0x01d0, 0x01f0, 
	u32		dmacount;
#define     VALID_BIT                                           31
#define     DMA_DIRECTION                                       30
#define     DMA_DONE_INTERRUPT_ENABLE                           29
#define     END_OF_CHAIN                                        28
#define         DMA_BYTE_COUNT_MASK                                 ((1<<24)-1)
#define     DMA_BYTE_COUNT                                      0
	u32		dmaaddr;
	u32		dmadesc;
	u32		_unused1;
} __attribute__ ((packed));

/* dedicated endpoint registers, BAR0 + 0x0200 */

struct net2280_dep_regs {	/* [11.8] */
	// offset 0x0200, 0x0210, 0x220, 0x230, 0x240
	u32		dep_cfg;
	// offset 0x0204, 0x0214, 0x224, 0x234, 0x244
	u32		dep_rsp;
	u32		_unused [2];
} __attribute__ ((packed));

/* configurable endpoint registers, BAR0 + 0x0300 ... array of seven structs
 * like this, for ep0 then the configurable endpoints A..F
 * ep0 reserved for control; E and F have only 64 bytes of fifo
 */
struct net2280_ep_regs {	/* [11.9] */
	// offset 0x0300, 0x0320, 0x0340, 0x0360, 0x0380, 0x03a0, 0x03c0
	u32		ep_cfg;
#define     ENDPOINT_BYTE_COUNT                                 16
#define     ENDPOINT_ENABLE                                     10
#define     ENDPOINT_TYPE                                       8
#define     ENDPOINT_DIRECTION                                  7
#define     ENDPOINT_NUMBER                                     0
	u32		ep_rsp;
#define     SET_NAK_OUT_PACKETS                                 15
#define     SET_EP_HIDE_STATUS_PHASE                            14
#define     SET_EP_FORCE_CRC_ERROR                              13
#define     SET_INTERRUPT_MODE                                  12
#define     SET_CONTROL_STATUS_PHASE_HANDSHAKE                  11
#define     SET_NAK_OUT_PACKETS_MODE                            10
#define     SET_ENDPOINT_TOGGLE                                 9
#define     SET_ENDPOINT_HALT                                   8
#define     CLEAR_NAK_OUT_PACKETS                               7
#define     CLEAR_EP_HIDE_STATUS_PHASE                          6
#define     CLEAR_EP_FORCE_CRC_ERROR                            5
#define     CLEAR_INTERRUPT_MODE                                4
#define     CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE                3
#define     CLEAR_NAK_OUT_PACKETS_MODE                          2
#define     CLEAR_ENDPOINT_TOGGLE                               1
#define     CLEAR_ENDPOINT_HALT                                 0
	u32		ep_irqenb;
#define     SHORT_PACKET_OUT_DONE_INTERRUPT_ENABLE              6
#define     SHORT_PACKET_TRANSFERRED_INTERRUPT_ENABLE           5
#define     DATA_PACKET_RECEIVED_INTERRUPT_ENABLE               3
#define     DATA_PACKET_TRANSMITTED_INTERRUPT_ENABLE            2
#define     DATA_OUT_PING_TOKEN_INTERRUPT_ENABLE                1
#define     DATA_IN_TOKEN_INTERRUPT_ENABLE                      0
	u32		ep_stat;
#define     FIFO_VALID_COUNT                                    24
#define     HIGH_BANDWIDTH_OUT_TRANSACTION_PID                  22
#define     TIMEOUT                                             21
#define     USB_STALL_SENT                                      20
#define     USB_IN_NAK_SENT                                     19
#define     USB_IN_ACK_RCVD                                     18
#define     USB_OUT_PING_NAK_SENT                               17
#define     USB_OUT_ACK_SENT                                    16
#define     FIFO_OVERFLOW                                       13
#define     FIFO_UNDERFLOW                                      12
#define     FIFO_FULL                                           11
#define     FIFO_EMPTY                                          10
#define     FIFO_FLUSH                                          9
#define     SHORT_PACKET_OUT_DONE_INTERRUPT                     6
#define     SHORT_PACKET_TRANSFERRED_INTERRUPT                  5
#define     NAK_OUT_PACKETS                                     4
#define     DATA_PACKET_RECEIVED_INTERRUPT                      3
#define     DATA_PACKET_TRANSMITTED_INTERRUPT                   2
#define     DATA_OUT_PING_TOKEN_INTERRUPT                       1
#define     DATA_IN_TOKEN_INTERRUPT                             0
	// offset 0x0310, 0x0330, 0x0350, 0x0370, 0x0390, 0x03b0, 0x03d0
	u32		ep_avail;
	u32		ep_data;
	u32		_unused0 [2];
} __attribute__ ((packed));

/*-------------------------------------------------------------------------*/

#ifdef	__KERNEL__

/* indexed registers [11.10] are accessed indirectly
 * caller must own the device lock.
 */

static inline u32
get_idx_reg (struct net2280_regs __iomem *regs, u32 index)
{
	writel (index, &regs->idxaddr);
	/* NOTE:  synchs device/cpu memory views */
	return readl (&regs->idxdata);
}

static inline void
set_idx_reg (struct net2280_regs __iomem *regs, u32 index, u32 value)
{
	writel (index, &regs->idxaddr);
	writel (value, &regs->idxdata);
	/* posted, may not be visible yet */
}

#endif	/* __KERNEL__ */


#define REG_DIAG		0x0
#define     RETRY_COUNTER                                       16
#define     FORCE_PCI_SERR                                      11
#define     FORCE_PCI_INTERRUPT                                 10
#define     FORCE_USB_INTERRUPT                                 9
#define     FORCE_CPU_INTERRUPT                                 8
#define     ILLEGAL_BYTE_ENABLES                                5
#define     FAST_TIMES                                          4
#define     FORCE_RECEIVE_ERROR                                 2
#define     FORCE_TRANSMIT_CRC_ERROR                            0
#define REG_FRAME		0x02	/* from last sof */
#define REG_CHIPREV		0x03	/* in bcd */
#define	REG_HS_NAK_RATE		0x0a	/* NAK per N uframes */

#define	CHIPREV_1	0x0100
#define	CHIPREV_1A	0x0110

#ifdef	__KERNEL__

/* ep a-f highspeed and fullspeed maxpacket, addresses
 * computed from ep->num
 */
#define REG_EP_MAXPKT(dev,num) (((num) + 1) * 0x10 + \
		(((dev)->gadget.speed == USB_SPEED_HIGH) ? 0 : 1))

/*-------------------------------------------------------------------------*/

/* [8.3] for scatter/gather i/o
 * use struct net2280_dma_regs bitfields
 */
struct net2280_dma {
	__le32		dmacount;
	__le32		dmaaddr;		/* the buffer */
	__le32		dmadesc;		/* next dma descriptor */
	__le32		_reserved;
} __attribute__ ((aligned (16)));

/*-------------------------------------------------------------------------*/

/* DRIVER DATA STRUCTURES and UTILITIES */

struct net2280_ep {
	struct usb_ep				ep;
	struct net2280_ep_regs			__iomem *regs;
	struct net2280_dma_regs			__iomem *dma;
	struct net2280_dma			*dummy;
	dma_addr_t				td_dma;	/* of dummy */
	struct net2280				*dev;
	unsigned long				irqs;

	/* analogous to a host-side qh */
	struct list_head			queue;
	const struct usb_endpoint_descriptor	*desc;
	unsigned				num : 8,
						fifo_size : 12,
						in_fifo_validate : 1,
						out_overflow : 1,
						stopped : 1,
						is_in : 1,
						is_iso : 1;
};

static inline void allow_status (struct net2280_ep *ep)
{
	/* ep0 only */
	writel (  (1 << CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE)
		| (1 << CLEAR_NAK_OUT_PACKETS)
		| (1 << CLEAR_NAK_OUT_PACKETS_MODE)
		, &ep->regs->ep_rsp);
	ep->stopped = 1;
}

/* count (<= 4) bytes in the next fifo write will be valid */
static inline void set_fifo_bytecount (struct net2280_ep *ep, unsigned count)
{
	writeb (count, 2 + (u8 __iomem *) &ep->regs->ep_cfg);
}

struct net2280_request {
	struct usb_request		req;
	struct net2280_dma		*td;
	dma_addr_t			td_dma;
	struct list_head		queue;
	unsigned			mapped : 1,
					valid : 1;
};

struct net2280 {
	/* each pci device provides one gadget, several endpoints */
	struct usb_gadget		gadget;
	spinlock_t			lock;
	struct net2280_ep		ep [7];
	struct usb_gadget_driver 	*driver;
	unsigned			enabled : 1,
					protocol_stall : 1,
					softconnect : 1,
					got_irq : 1,
					region : 1;
	u16				chiprev;

	/* pci state used to access those endpoints */
	struct pci_dev			*pdev;
	struct net2280_regs		__iomem *regs;
	struct net2280_usb_regs		__iomem *usb;
	struct net2280_pci_regs		__iomem *pci;
	struct net2280_dma_regs		__iomem *dma;
	struct net2280_dep_regs		__iomem *dep;
	struct net2280_ep_regs		__iomem *epregs;

	struct pci_pool			*requests;
	// statistics...
};

static inline void set_halt (struct net2280_ep *ep)
{
	/* ep0 and bulk/intr endpoints */
	writel (  (1 << CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE)
		    /* set NAK_OUT for erratum 0114 */
		| ((ep->dev->chiprev == CHIPREV_1) << SET_NAK_OUT_PACKETS)
		| (1 << SET_ENDPOINT_HALT)
		, &ep->regs->ep_rsp);
}

static inline void clear_halt (struct net2280_ep *ep)
{
	/* ep0 and bulk/intr endpoints */
	writel (  (1 << CLEAR_ENDPOINT_HALT)
		| (1 << CLEAR_ENDPOINT_TOGGLE)
		    /* unless the gadget driver left a short packet in the
		     * fifo, this reverses the erratum 0114 workaround.
		     */
		| ((ep->dev->chiprev == CHIPREV_1) << CLEAR_NAK_OUT_PACKETS)
		, &ep->regs->ep_rsp);
}

#ifdef USE_RDK_LEDS

static inline void net2280_led_init (struct net2280 *dev)
{
	/* LED3 (green) is on during USB activity. note erratum 0113. */
	writel ((1 << GPIO3_LED_SELECT)
		| (1 << GPIO3_OUTPUT_ENABLE)
		| (1 << GPIO2_OUTPUT_ENABLE)
		| (1 << GPIO1_OUTPUT_ENABLE)
		| (1 << GPIO0_OUTPUT_ENABLE)
		, &dev->regs->gpioctl);
}

/* indicate speed with bi-color LED 0/1 */
static inline
void net2280_led_speed (struct net2280 *dev, enum usb_device_speed speed)
{
	u32	val = readl (&dev->regs->gpioctl);
	switch (speed) {
	case USB_SPEED_HIGH:		/* green */
		val &= ~(1 << GPIO0_DATA);
		val |= (1 << GPIO1_DATA);
		break;
	case USB_SPEED_FULL:		/* red */
		val &= ~(1 << GPIO1_DATA);
		val |= (1 << GPIO0_DATA);
		break;
	default:			/* (off/black) */
		val &= ~((1 << GPIO1_DATA) | (1 << GPIO0_DATA));
		break;
	}
	writel (val, &dev->regs->gpioctl);
}

/* indicate power with LED 2 */
static inline void net2280_led_active (struct net2280 *dev, int is_active)
{
	u32	val = readl (&dev->regs->gpioctl);

	// FIXME this LED never seems to turn on.
	if (is_active)
		val |= GPIO2_DATA;
	else
		val &= ~GPIO2_DATA;
	writel (val, &dev->regs->gpioctl);
}
static inline void net2280_led_shutdown (struct net2280 *dev)
{
	/* turn off all four GPIO*_DATA bits */
	writel (readl (&dev->regs->gpioctl) & ~0x0f,
			&dev->regs->gpioctl);
}

#else

#define net2280_led_init(dev)		do { } while (0)
#define net2280_led_speed(dev, speed)	do { } while (0)
#define net2280_led_shutdown(dev)	do { } while (0)

#endif

/*-------------------------------------------------------------------------*/

#define xprintk(dev,level,fmt,args...) \
	printk(level "%s %s: " fmt , driver_name , \
			pci_name(dev->pdev) , ## args)

#ifdef DEBUG
#undef DEBUG
#define DEBUG(dev,fmt,args...) \
	xprintk(dev , KERN_DEBUG , fmt , ## args)
#else
#define DEBUG(dev,fmt,args...) \
	do { } while (0)
#endif /* DEBUG */

#ifdef VERBOSE
#define VDEBUG DEBUG
#else
#define VDEBUG(dev,fmt,args...) \
	do { } while (0)
#endif	/* VERBOSE */

#define ERROR(dev,fmt,args...) \
	xprintk(dev , KERN_ERR , fmt , ## args)
#define WARN(dev,fmt,args...) \
	xprintk(dev , KERN_WARNING , fmt , ## args)
#define INFO(dev,fmt,args...) \
	xprintk(dev , KERN_INFO , fmt , ## args)

/*-------------------------------------------------------------------------*/

static inline void start_out_naking (struct net2280_ep *ep)
{
	/* NOTE:  hardware races lurk here, and PING protocol issues */
	writel ((1 << SET_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
	/* synch with device */
	readl (&ep->regs->ep_rsp);
}

#ifdef DEBUG
static inline void assert_out_naking (struct net2280_ep *ep, const char *where)
{
	u32	tmp = readl (&ep->regs->ep_stat);

	if ((tmp & (1 << NAK_OUT_PACKETS)) == 0) {
		DEBUG (ep->dev, "%s %s %08x !NAK\n",
				ep->ep.name, where, tmp);
		writel ((1 << SET_NAK_OUT_PACKETS),
			&ep->regs->ep_rsp);
	}
}
#define ASSERT_OUT_NAKING(ep) assert_out_naking(ep,__FUNCTION__)
#else
#define ASSERT_OUT_NAKING(ep) do {} while (0)
#endif

static inline void stop_out_naking (struct net2280_ep *ep)
{
	u32	tmp;

	tmp = readl (&ep->regs->ep_stat);
	if ((tmp & (1 << NAK_OUT_PACKETS)) != 0)
		writel ((1 << CLEAR_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
}

#endif	/* __KERNEL__ */
