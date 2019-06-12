/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * wanXL serial card driver for Linux
 * definitions common to host driver and card firmware
 *
 * Copyright (C) 2003 Krzysztof Halasa <khc@pm.waw.pl>
 */

#define RESET_WHILE_LOADING 0

/* you must rebuild the firmware if any of the following is changed */
#define DETECT_RAM 0		/* needed for > 4MB RAM, 16 MB maximum */
#define QUICC_MEMCPY_USES_PLX 1	/* must be used if the host has > 256 MB RAM */


#define STATUS_CABLE_V35	2
#define STATUS_CABLE_X21	3
#define STATUS_CABLE_V24	4
#define STATUS_CABLE_EIA530	5
#define STATUS_CABLE_INVALID	6
#define STATUS_CABLE_NONE	7

#define STATUS_CABLE_DCE	0x8000
#define STATUS_CABLE_DSR	0x0010
#define STATUS_CABLE_DCD	0x0008
#define STATUS_CABLE_PM_SHIFT	5

#define PDM_OFFSET 0x1000

#define TX_BUFFERS 10		/* per port */
#define RX_BUFFERS 30
#define RX_QUEUE_LENGTH 40	/* card->host queue length - per card */

#define PACKET_EMPTY		0x00
#define PACKET_FULL		0x10
#define PACKET_SENT		0x20 /* TX only */
#define PACKET_UNDERRUN		0x30 /* TX only */
#define PACKET_PORT_MASK	0x03 /* RX only */

/* bit numbers in PLX9060 doorbell registers */
#define DOORBELL_FROM_CARD_TX_0		0 /* packet sent by the card */
#define DOORBELL_FROM_CARD_TX_1		1
#define DOORBELL_FROM_CARD_TX_2		2
#define DOORBELL_FROM_CARD_TX_3		3
#define DOORBELL_FROM_CARD_RX		4
#define DOORBELL_FROM_CARD_CABLE_0	5 /* cable/PM/etc. changed */
#define DOORBELL_FROM_CARD_CABLE_1	6
#define DOORBELL_FROM_CARD_CABLE_2	7
#define DOORBELL_FROM_CARD_CABLE_3	8

#define DOORBELL_TO_CARD_OPEN_0		0
#define DOORBELL_TO_CARD_OPEN_1		1
#define DOORBELL_TO_CARD_OPEN_2		2
#define DOORBELL_TO_CARD_OPEN_3		3
#define DOORBELL_TO_CARD_CLOSE_0	4
#define DOORBELL_TO_CARD_CLOSE_1	5
#define DOORBELL_TO_CARD_CLOSE_2	6
#define DOORBELL_TO_CARD_CLOSE_3	7
#define DOORBELL_TO_CARD_TX_0		8 /* outbound packet queued */
#define DOORBELL_TO_CARD_TX_1		9
#define DOORBELL_TO_CARD_TX_2		10
#define DOORBELL_TO_CARD_TX_3		11

/* firmware-only status bits, starting from last DOORBELL_TO_CARD + 1 */
#define TASK_SCC_0			12
#define TASK_SCC_1			13
#define TASK_SCC_2			14
#define TASK_SCC_3			15

#define ALIGN32(x) (((x) + 3) & 0xFFFFFFFC)
#define BUFFER_LENGTH	ALIGN32(HDLC_MAX_MRU + 4) /* 4 bytes for 32-bit CRC */

/* Address of TX and RX buffers in 68360 address space */
#define BUFFERS_ADDR	0x4000	/* 16 KB */

#ifndef __ASSEMBLER__
#define PLX_OFFSET		0
#else
#define PLX_OFFSET		PLX + 0x80
#endif

#define PLX_MAILBOX_0		(PLX_OFFSET + 0x40)
#define PLX_MAILBOX_1		(PLX_OFFSET + 0x44)
#define PLX_MAILBOX_2		(PLX_OFFSET + 0x48)
#define PLX_MAILBOX_3		(PLX_OFFSET + 0x4C)
#define PLX_MAILBOX_4		(PLX_OFFSET + 0x50)
#define PLX_MAILBOX_5		(PLX_OFFSET + 0x54)
#define PLX_MAILBOX_6		(PLX_OFFSET + 0x58)
#define PLX_MAILBOX_7		(PLX_OFFSET + 0x5C)
#define PLX_DOORBELL_TO_CARD	(PLX_OFFSET + 0x60)
#define PLX_DOORBELL_FROM_CARD	(PLX_OFFSET + 0x64)
#define PLX_INTERRUPT_CS	(PLX_OFFSET + 0x68)
#define PLX_CONTROL		(PLX_OFFSET + 0x6C)

#ifdef __ASSEMBLER__
#define PLX_DMA_0_MODE		(PLX + 0x100)
#define PLX_DMA_0_PCI		(PLX + 0x104)
#define PLX_DMA_0_LOCAL		(PLX + 0x108)
#define PLX_DMA_0_LENGTH	(PLX + 0x10C)
#define PLX_DMA_0_DESC		(PLX + 0x110)
#define PLX_DMA_1_MODE		(PLX + 0x114)
#define PLX_DMA_1_PCI		(PLX + 0x118)
#define PLX_DMA_1_LOCAL		(PLX + 0x11C)
#define PLX_DMA_1_LENGTH	(PLX + 0x120)
#define PLX_DMA_1_DESC		(PLX + 0x124)
#define PLX_DMA_CMD_STS		(PLX + 0x128)
#define PLX_DMA_ARBITR_0	(PLX + 0x12C)
#define PLX_DMA_ARBITR_1	(PLX + 0x130)
#endif

#define DESC_LENGTH 12

/* offsets from start of status_t */
/* card to host */
#define STATUS_OPEN		0
#define STATUS_CABLE		(STATUS_OPEN + 4)
#define STATUS_RX_OVERRUNS	(STATUS_CABLE + 4)
#define STATUS_RX_FRAME_ERRORS	(STATUS_RX_OVERRUNS + 4)

/* host to card */
#define STATUS_PARITY		(STATUS_RX_FRAME_ERRORS + 4)
#define STATUS_ENCODING		(STATUS_PARITY + 4)
#define STATUS_CLOCKING		(STATUS_ENCODING + 4)
#define STATUS_TX_DESCS		(STATUS_CLOCKING + 4)

#ifndef __ASSEMBLER__

typedef struct {
	volatile u32 stat;
	u32 address;		/* PCI address */
	volatile u32 length;
}desc_t;


typedef struct {
// Card to host
	volatile u32 open;
	volatile u32 cable;
	volatile u32 rx_overruns;
	volatile u32 rx_frame_errors;

// Host to card
	u32 parity;
	u32 encoding;
	u32 clocking;
	desc_t tx_descs[TX_BUFFERS];
}port_status_t;

#endif /* __ASSEMBLER__ */
