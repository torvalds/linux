/*
 * PLX NET2272 high/full speed USB device controller
 *
 * Copyright (C) 2005-2006 PLX Technology, Inc.
 * Copyright (C) 2006-2011 Analog Devices, Inc.
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

#ifndef __NET2272_H__
#define __NET2272_H__

/* Main Registers */
#define REGADDRPTR			0x00
#define REGDATA				0x01
#define IRQSTAT0			0x02
#define 	ENDPOINT_0_INTERRUPT			0
#define 	ENDPOINT_A_INTERRUPT			1
#define 	ENDPOINT_B_INTERRUPT			2
#define 	ENDPOINT_C_INTERRUPT			3
#define 	VIRTUALIZED_ENDPOINT_INTERRUPT		4
#define 	SETUP_PACKET_INTERRUPT			5
#define 	DMA_DONE_INTERRUPT			6
#define 	SOF_INTERRUPT				7
#define IRQSTAT1			0x03
#define 	CONTROL_STATUS_INTERRUPT		1
#define 	VBUS_INTERRUPT				2
#define 	SUSPEND_REQUEST_INTERRUPT		3
#define 	SUSPEND_REQUEST_CHANGE_INTERRUPT	4
#define 	RESUME_INTERRUPT			5
#define 	ROOT_PORT_RESET_INTERRUPT		6
#define 	RESET_STATUS				7
#define PAGESEL				0x04
#define DMAREQ				0x1c
#define 	DMA_ENDPOINT_SELECT			0
#define 	DREQ_POLARITY				1
#define 	DACK_POLARITY				2
#define 	EOT_POLARITY				3
#define 	DMA_CONTROL_DACK			4
#define 	DMA_REQUEST_ENABLE			5
#define 	DMA_REQUEST				6
#define 	DMA_BUFFER_VALID			7
#define SCRATCH				0x1d
#define IRQENB0				0x20
#define 	ENDPOINT_0_INTERRUPT_ENABLE		0
#define 	ENDPOINT_A_INTERRUPT_ENABLE		1
#define 	ENDPOINT_B_INTERRUPT_ENABLE		2
#define 	ENDPOINT_C_INTERRUPT_ENABLE		3
#define 	VIRTUALIZED_ENDPOINT_INTERRUPT_ENABLE	4
#define 	SETUP_PACKET_INTERRUPT_ENABLE		5
#define 	DMA_DONE_INTERRUPT_ENABLE		6
#define 	SOF_INTERRUPT_ENABLE			7
#define IRQENB1				0x21
#define 	VBUS_INTERRUPT_ENABLE			2
#define 	SUSPEND_REQUEST_INTERRUPT_ENABLE	3
#define 	SUSPEND_REQUEST_CHANGE_INTERRUPT_ENABLE	4
#define 	RESUME_INTERRUPT_ENABLE			5
#define 	ROOT_PORT_RESET_INTERRUPT_ENABLE	6
#define LOCCTL				0x22
#define 	DATA_WIDTH				0
#define 	LOCAL_CLOCK_OUTPUT			1
#define 		LOCAL_CLOCK_OUTPUT_OFF			0
#define 		LOCAL_CLOCK_OUTPUT_3_75MHZ		1
#define 		LOCAL_CLOCK_OUTPUT_7_5MHZ		2
#define 		LOCAL_CLOCK_OUTPUT_15MHZ		3
#define 		LOCAL_CLOCK_OUTPUT_30MHZ		4
#define 		LOCAL_CLOCK_OUTPUT_60MHZ		5
#define 	DMA_SPLIT_BUS_MODE			4
#define 	BYTE_SWAP				5
#define 	BUFFER_CONFIGURATION			6
#define 		BUFFER_CONFIGURATION_EPA512_EPB512	0
#define 		BUFFER_CONFIGURATION_EPA1024_EPB512	1
#define 		BUFFER_CONFIGURATION_EPA1024_EPB1024	2
#define 		BUFFER_CONFIGURATION_EPA1024DB		3
#define CHIPREV_LEGACY			0x23
#define 		NET2270_LEGACY_REV			0x40
#define LOCCTL1				0x24
#define 	DMA_MODE				0
#define 		SLOW_DREQ				0
#define 		FAST_DREQ				1
#define 		BURST_MODE				2
#define 	DMA_DACK_ENABLE				2
#define CHIPREV_2272			0x25
#define 		CHIPREV_NET2272_R1			0x10
#define 		CHIPREV_NET2272_R1A			0x11
/* USB Registers */
#define USBCTL0				0x18
#define 	IO_WAKEUP_ENABLE			1
#define 	USB_DETECT_ENABLE			3
#define 	USB_ROOT_PORT_WAKEUP_ENABLE		5
#define USBCTL1				0x19
#define 	VBUS_PIN				0
#define 		USB_FULL_SPEED				1
#define 		USB_HIGH_SPEED				2
#define 	GENERATE_RESUME				3
#define 	VIRTUAL_ENDPOINT_ENABLE			4
#define FRAME0				0x1a
#define FRAME1				0x1b
#define OURADDR				0x30
#define 	FORCE_IMMEDIATE				7
#define USBDIAG				0x31
#define 	FORCE_TRANSMIT_CRC_ERROR		0
#define 	PREVENT_TRANSMIT_BIT_STUFF		1
#define 	FORCE_RECEIVE_ERROR			2
#define 	FAST_TIMES				4
#define USBTEST				0x32
#define 	TEST_MODE_SELECT			0
#define 		NORMAL_OPERATION			0
#define 		TEST_J					1
#define 		TEST_K					2
#define 		TEST_SE0_NAK				3
#define 		TEST_PACKET				4
#define 		TEST_FORCE_ENABLE			5
#define XCVRDIAG			0x33
#define 	FORCE_FULL_SPEED			2
#define 	FORCE_HIGH_SPEED			3
#define 	OPMODE					4
#define 		NORMAL_OPERATION			0
#define 		NON_DRIVING				1
#define 		DISABLE_BITSTUFF_AND_NRZI_ENCODE	2
#define 	LINESTATE				6
#define 		SE0_STATE				0
#define 		J_STATE					1
#define 		K_STATE					2
#define 		SE1_STATE				3
#define VIRTOUT0			0x34
#define VIRTOUT1			0x35
#define VIRTIN0				0x36
#define VIRTIN1				0x37
#define SETUP0				0x40
#define SETUP1				0x41
#define SETUP2				0x42
#define SETUP3				0x43
#define SETUP4				0x44
#define SETUP5				0x45
#define SETUP6				0x46
#define SETUP7				0x47
/* Endpoint Registers (Paged via PAGESEL) */
#define EP_DATA				0x05
#define EP_STAT0			0x06
#define 	DATA_IN_TOKEN_INTERRUPT			0
#define 	DATA_OUT_TOKEN_INTERRUPT		1
#define 	DATA_PACKET_TRANSMITTED_INTERRUPT	2
#define 	DATA_PACKET_RECEIVED_INTERRUPT		3
#define 	SHORT_PACKET_TRANSFERRED_INTERRUPT	4
#define 	NAK_OUT_PACKETS				5
#define 	BUFFER_EMPTY				6
#define 	BUFFER_FULL				7
#define EP_STAT1			0x07
#define 	TIMEOUT					0
#define 	USB_OUT_ACK_SENT			1
#define 	USB_OUT_NAK_SENT			2
#define 	USB_IN_ACK_RCVD				3
#define 	USB_IN_NAK_SENT				4
#define 	USB_STALL_SENT				5
#define 	LOCAL_OUT_ZLP				6
#define 	BUFFER_FLUSH				7
#define EP_TRANSFER0			0x08
#define EP_TRANSFER1			0x09
#define EP_TRANSFER2			0x0a
#define EP_IRQENB			0x0b
#define 	DATA_IN_TOKEN_INTERRUPT_ENABLE		0
#define 	DATA_OUT_TOKEN_INTERRUPT_ENABLE		1
#define 	DATA_PACKET_TRANSMITTED_INTERRUPT_ENABLE	2
#define 	DATA_PACKET_RECEIVED_INTERRUPT_ENABLE	3
#define 	SHORT_PACKET_TRANSFERRED_INTERRUPT_ENABLE	4
#define EP_AVAIL0			0x0c
#define EP_AVAIL1			0x0d
#define EP_RSPCLR			0x0e
#define EP_RSPSET			0x0f
#define 	ENDPOINT_HALT				0
#define 	ENDPOINT_TOGGLE				1
#define 	NAK_OUT_PACKETS_MODE			2
#define 	CONTROL_STATUS_PHASE_HANDSHAKE		3
#define 	INTERRUPT_MODE				4
#define 	AUTOVALIDATE				5
#define 	HIDE_STATUS_PHASE			6
#define 	ALT_NAK_OUT_PACKETS			7
#define EP_MAXPKT0			0x28
#define EP_MAXPKT1			0x29
#define 	ADDITIONAL_TRANSACTION_OPPORTUNITIES	3
#define 		NONE_ADDITIONAL_TRANSACTION		0
#define 		ONE_ADDITIONAL_TRANSACTION		1
#define 		TWO_ADDITIONAL_TRANSACTION		2
#define EP_CFG				0x2a
#define 	ENDPOINT_NUMBER				0
#define 	ENDPOINT_DIRECTION			4
#define 	ENDPOINT_TYPE				5
#define 	ENDPOINT_ENABLE				7
#define EP_HBW				0x2b
#define 	HIGH_BANDWIDTH_OUT_TRANSACTION_PID	0
#define 		DATA0_PID				0
#define 		DATA1_PID				1
#define 		DATA2_PID				2
#define 		MDATA_PID				3
#define EP_BUFF_STATES			0x2c
#define 	BUFFER_A_STATE				0
#define 	BUFFER_B_STATE				2
#define 		BUFF_FREE				0
#define 		BUFF_VALID				1
#define 		BUFF_LCL				2
#define 		BUFF_USB				3

/*---------------------------------------------------------------------------*/

#define PCI_DEVICE_ID_RDK1	0x9054

/* PCI-RDK EPLD Registers */
#define RDK_EPLD_IO_REGISTER1		0x00000000
#define 	RDK_EPLD_USB_RESET				0
#define 	RDK_EPLD_USB_POWERDOWN				1
#define 	RDK_EPLD_USB_WAKEUP				2
#define 	RDK_EPLD_USB_EOT				3
#define 	RDK_EPLD_DPPULL					4
#define RDK_EPLD_IO_REGISTER2		0x00000004
#define 	RDK_EPLD_BUSWIDTH				0
#define 	RDK_EPLD_USER					2
#define 	RDK_EPLD_RESET_INTERRUPT_ENABLE			3
#define 	RDK_EPLD_DMA_TIMEOUT_ENABLE			4
#define RDK_EPLD_STATUS_REGISTER	0x00000008
#define 	RDK_EPLD_USB_LRESET				0
#define RDK_EPLD_REVISION_REGISTER	0x0000000c

/* PCI-RDK PLX 9054 Registers */
#define INTCSR				0x68
#define 	PCI_INTERRUPT_ENABLE				8
#define 	LOCAL_INTERRUPT_INPUT_ENABLE			11
#define 	LOCAL_INPUT_INTERRUPT_ACTIVE			15
#define 	LOCAL_DMA_CHANNEL_0_INTERRUPT_ENABLE		18
#define 	LOCAL_DMA_CHANNEL_1_INTERRUPT_ENABLE		19
#define 	DMA_CHANNEL_0_INTERRUPT_ACTIVE			21
#define 	DMA_CHANNEL_1_INTERRUPT_ACTIVE			22
#define CNTRL				0x6C
#define 	RELOAD_CONFIGURATION_REGISTERS			29
#define 	PCI_ADAPTER_SOFTWARE_RESET			30
#define DMAMODE0			0x80
#define 	LOCAL_BUS_WIDTH					0
#define 	INTERNAL_WAIT_STATES				2
#define 	TA_READY_INPUT_ENABLE				6
#define 	LOCAL_BURST_ENABLE				8
#define 	SCATTER_GATHER_MODE				9
#define 	DONE_INTERRUPT_ENABLE				10
#define 	LOCAL_ADDRESSING_MODE				11
#define 	DEMAND_MODE					12
#define 	DMA_EOT_ENABLE					14
#define 	FAST_SLOW_TERMINATE_MODE_SELECT			15
#define 	DMA_CHANNEL_INTERRUPT_SELECT			17
#define DMAPADR0			0x84
#define DMALADR0			0x88
#define DMASIZ0				0x8c
#define DMADPR0				0x90
#define 	DESCRIPTOR_LOCATION				0
#define 	END_OF_CHAIN					1
#define 	INTERRUPT_AFTER_TERMINAL_COUNT			2
#define 	DIRECTION_OF_TRANSFER				3
#define DMACSR0				0xa8
#define 	CHANNEL_ENABLE					0
#define 	CHANNEL_START					1
#define 	CHANNEL_ABORT					2
#define 	CHANNEL_CLEAR_INTERRUPT				3
#define 	CHANNEL_DONE					4
#define DMATHR				0xb0
#define LBRD1				0xf8
#define 	MEMORY_SPACE_LOCAL_BUS_WIDTH			0
#define 	W8_BIT						0
#define 	W16_BIT						1

/* Special OR'ing of INTCSR bits */
#define LOCAL_INTERRUPT_TEST \
	((1 << LOCAL_INPUT_INTERRUPT_ACTIVE) | \
	 (1 << LOCAL_INTERRUPT_INPUT_ENABLE))

#define DMA_CHANNEL_0_TEST \
	((1 << DMA_CHANNEL_0_INTERRUPT_ACTIVE) | \
	 (1 << LOCAL_DMA_CHANNEL_0_INTERRUPT_ENABLE))

#define DMA_CHANNEL_1_TEST \
	((1 << DMA_CHANNEL_1_INTERRUPT_ACTIVE) | \
	 (1 << LOCAL_DMA_CHANNEL_1_INTERRUPT_ENABLE))

/* EPLD Registers */
#define RDK_EPLD_IO_REGISTER1			0x00000000
#define 	RDK_EPLD_USB_RESET			0
#define 	RDK_EPLD_USB_POWERDOWN			1
#define 	RDK_EPLD_USB_WAKEUP			2
#define 	RDK_EPLD_USB_EOT			3
#define 	RDK_EPLD_DPPULL				4
#define RDK_EPLD_IO_REGISTER2			0x00000004
#define 	RDK_EPLD_BUSWIDTH			0
#define 	RDK_EPLD_USER				2
#define 	RDK_EPLD_RESET_INTERRUPT_ENABLE		3
#define 	RDK_EPLD_DMA_TIMEOUT_ENABLE		4
#define RDK_EPLD_STATUS_REGISTER		0x00000008
#define RDK_EPLD_USB_LRESET				0
#define RDK_EPLD_REVISION_REGISTER		0x0000000c

#define EPLD_IO_CONTROL_REGISTER		0x400
#define 	NET2272_RESET				0
#define 	BUSWIDTH				1
#define 	MPX_MODE				3
#define 	USER					4
#define 	DMA_TIMEOUT_ENABLE			5
#define 	DMA_CTL_DACK				6
#define 	EPLD_DMA_ENABLE				7
#define EPLD_DMA_CONTROL_REGISTER		0x800
#define 	SPLIT_DMA_MODE				0
#define 	SPLIT_DMA_DIRECTION			1
#define 	SPLIT_DMA_ENABLE			2
#define 	SPLIT_DMA_INTERRUPT_ENABLE		3
#define 	SPLIT_DMA_INTERRUPT			4
#define 	EPLD_DMA_MODE				5
#define 	EPLD_DMA_CONTROLLER_ENABLE		7
#define SPLIT_DMA_ADDRESS_LOW			0xc00
#define SPLIT_DMA_ADDRESS_HIGH			0x1000
#define SPLIT_DMA_BYTE_COUNT_LOW		0x1400
#define SPLIT_DMA_BYTE_COUNT_HIGH		0x1800
#define EPLD_REVISION_REGISTER			0x1c00
#define SPLIT_DMA_RAM				0x4000
#define DMA_RAM_SIZE				0x1000

/*---------------------------------------------------------------------------*/

#define PCI_DEVICE_ID_RDK2	0x3272

/* PCI-RDK version 2 registers */

/* Main Control Registers */

#define RDK2_IRQENB			0x00
#define RDK2_IRQSTAT			0x04
#define 	PB7				23
#define 	PB6				22
#define 	PB5				21
#define 	PB4				20
#define 	PB3				19
#define 	PB2				18
#define 	PB1				17
#define 	PB0				16
#define 	GP3				23
#define 	GP2				23
#define 	GP1				23
#define 	GP0				23
#define 	DMA_RETRY_ABORT			6
#define 	DMA_PAUSE_DONE			5
#define 	DMA_ABORT_DONE			4
#define 	DMA_OUT_FIFO_TRANSFER_DONE	3
#define 	DMA_LOCAL_DONE			2
#define 	DMA_PCI_DONE			1
#define 	NET2272_PCI_IRQ			0

#define RDK2_LOCCTLRDK			0x08
#define 	CHIP_RESET			3
#define 	SPLIT_DMA			2
#define 	MULTIPLEX_MODE			1
#define 	BUS_WIDTH			0

#define RDK2_GPIOCTL			0x10
#define 	GP3_OUT_ENABLE					7
#define 	GP2_OUT_ENABLE					6
#define 	GP1_OUT_ENABLE					5
#define 	GP0_OUT_ENABLE					4
#define 	GP3_DATA					3
#define 	GP2_DATA					2
#define 	GP1_DATA					1
#define 	GP0_DATA					0

#define RDK2_LEDSW			0x14
#define 	LED3				27
#define 	LED2				26
#define 	LED1				25
#define 	LED0				24
#define 	PBUTTON				16
#define 	DIPSW				0

#define RDK2_DIAG			0x18
#define 	RDK2_FAST_TIMES				2
#define 	FORCE_PCI_SERR				1
#define 	FORCE_PCI_INT				0
#define RDK2_FPGAREV			0x1C

/* Dma Control registers */
#define RDK2_DMACTL			0x80
#define 	ADDR_HOLD				24
#define 	RETRY_COUNT				16	/* 23:16 */
#define 	FIFO_THRESHOLD				11	/* 15:11 */
#define 	MEM_WRITE_INVALIDATE			10
#define 	READ_MULTIPLE				9
#define 	READ_LINE				8
#define 	RDK2_DMA_MODE				6	/* 7:6 */
#define 	CONTROL_DACK				5
#define 	EOT_ENABLE				4
#define 	EOT_POLARITY				3
#define 	DACK_POLARITY				2
#define 	DREQ_POLARITY				1
#define 	DMA_ENABLE				0

#define RDK2_DMASTAT			0x84
#define 	GATHER_COUNT				12	/* 14:12 */
#define 	FIFO_COUNT				6	/* 11:6 */
#define 	FIFO_FLUSH				5
#define 	FIFO_TRANSFER				4
#define 	PAUSE_DONE				3
#define 	ABORT_DONE				2
#define 	DMA_ABORT				1
#define 	DMA_START				0

#define RDK2_DMAPCICOUNT		0x88
#define 	DMA_DIRECTION				31
#define 	DMA_PCI_BYTE_COUNT			0	/* 0:23 */

#define RDK2_DMALOCCOUNT		0x8C	/* 0:23 dma local byte count */

#define RDK2_DMAADDR			0x90	/* 2:31 PCI bus starting address */

/*---------------------------------------------------------------------------*/

#define REG_INDEXED_THRESHOLD	(1 << 5)

/* DRIVER DATA STRUCTURES and UTILITIES */
struct net2272_ep {
	struct usb_ep ep;
	struct net2272 *dev;
	unsigned long irqs;

	/* analogous to a host-side qh */
	struct list_head queue;
	const struct usb_endpoint_descriptor *desc;
	unsigned num:8,
	         fifo_size:12,
	         stopped:1,
	         wedged:1,
	         is_in:1,
	         is_iso:1,
	         dma:1,
	         not_empty:1;
};

struct net2272 {
	/* each device provides one gadget, several endpoints */
	struct usb_gadget gadget;
	struct device *dev;
	unsigned short dev_id;

	spinlock_t lock;
	struct net2272_ep ep[4];
	struct usb_gadget_driver *driver;
	unsigned protocol_stall:1,
	         softconnect:1,
	         wakeup:1,
	         dma_eot_polarity:1,
	         dma_dack_polarity:1,
	         dma_dreq_polarity:1,
	         dma_busy:1;
	u16 chiprev;
	u8 pagesel;

	unsigned int irq;
	unsigned short fifo_mode;

	unsigned int base_shift;
	u16 __iomem *base_addr;
	union {
#ifdef CONFIG_PCI
		struct {
			void __iomem *plx9054_base_addr;
			void __iomem *epld_base_addr;
		} rdk1;
		struct {
			/* Bar0, Bar1 is base_addr both mem-mapped */
			void __iomem *fpga_base_addr;
		} rdk2;
#endif
	};
};

static void __iomem *
net2272_reg_addr(struct net2272 *dev, unsigned int reg)
{
	return dev->base_addr + (reg << dev->base_shift);
}

static void
net2272_write(struct net2272 *dev, unsigned int reg, u8 value)
{
	if (reg >= REG_INDEXED_THRESHOLD) {
		/*
		 * Indexed register; use REGADDRPTR/REGDATA
		 *  - Save and restore REGADDRPTR. This prevents REGADDRPTR from
		 *    changes between other code sections, but it is time consuming.
		 *  - Performance tips: either do not save and restore REGADDRPTR (if it
		 *    is safe) or do save/restore operations only in critical sections.
		u8 tmp = readb(dev->base_addr + REGADDRPTR);
		 */
		writeb((u8)reg, net2272_reg_addr(dev, REGADDRPTR));
		writeb(value, net2272_reg_addr(dev, REGDATA));
		/* writeb(tmp, net2272_reg_addr(dev, REGADDRPTR)); */
	} else
		writeb(value, net2272_reg_addr(dev, reg));
}

static u8
net2272_read(struct net2272 *dev, unsigned int reg)
{
	u8 ret;

	if (reg >= REG_INDEXED_THRESHOLD) {
		/*
		 * Indexed register; use REGADDRPTR/REGDATA
		 *  - Save and restore REGADDRPTR. This prevents REGADDRPTR from
		 *    changes between other code sections, but it is time consuming.
		 *  - Performance tips: either do not save and restore REGADDRPTR (if it
		 *    is safe) or do save/restore operations only in critical sections.
		u8 tmp = readb(dev->base_addr + REGADDRPTR);
		 */
		writeb((u8)reg, net2272_reg_addr(dev, REGADDRPTR));
		ret = readb(net2272_reg_addr(dev, REGDATA));
		/* writeb(tmp, net2272_reg_addr(dev, REGADDRPTR)); */
	} else
		ret = readb(net2272_reg_addr(dev, reg));

	return ret;
}

static void
net2272_ep_write(struct net2272_ep *ep, unsigned int reg, u8 value)
{
	struct net2272 *dev = ep->dev;

	if (dev->pagesel != ep->num) {
		net2272_write(dev, PAGESEL, ep->num);
		dev->pagesel = ep->num;
	}
	net2272_write(dev, reg, value);
}

static u8
net2272_ep_read(struct net2272_ep *ep, unsigned int reg)
{
	struct net2272 *dev = ep->dev;

	if (dev->pagesel != ep->num) {
		net2272_write(dev, PAGESEL, ep->num);
		dev->pagesel = ep->num;
	}
	return net2272_read(dev, reg);
}

static void allow_status(struct net2272_ep *ep)
{
	/* ep0 only */
	net2272_ep_write(ep, EP_RSPCLR,
		(1 << CONTROL_STATUS_PHASE_HANDSHAKE) |
		(1 << ALT_NAK_OUT_PACKETS) |
		(1 << NAK_OUT_PACKETS_MODE));
	ep->stopped = 1;
}

static void set_halt(struct net2272_ep *ep)
{
	/* ep0 and bulk/intr endpoints */
	net2272_ep_write(ep, EP_RSPCLR, 1 << CONTROL_STATUS_PHASE_HANDSHAKE);
	net2272_ep_write(ep, EP_RSPSET, 1 << ENDPOINT_HALT);
}

static void clear_halt(struct net2272_ep *ep)
{
	/* ep0 and bulk/intr endpoints */
	net2272_ep_write(ep, EP_RSPCLR,
		(1 << ENDPOINT_HALT) | (1 << ENDPOINT_TOGGLE));
}

/* count (<= 4) bytes in the next fifo write will be valid */
static void set_fifo_bytecount(struct net2272_ep *ep, unsigned count)
{
	/* net2272_ep_write will truncate to u8 for us */
	net2272_ep_write(ep, EP_TRANSFER2, count >> 16);
	net2272_ep_write(ep, EP_TRANSFER1, count >> 8);
	net2272_ep_write(ep, EP_TRANSFER0, count);
}

struct net2272_request {
	struct usb_request req;
	struct list_head queue;
	unsigned mapped:1,
	         valid:1;
};

#endif
