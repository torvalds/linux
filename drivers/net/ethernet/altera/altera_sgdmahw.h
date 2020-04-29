/* SPDX-License-Identifier: GPL-2.0-only */
/* Altera TSE SGDMA and MSGDMA Linux driver
 * Copyright (C) 2014 Altera Corporation. All rights reserved
 */

#ifndef __ALTERA_SGDMAHW_H__
#define __ALTERA_SGDMAHW_H__

/* SGDMA descriptor structure */
struct sgdma_descrip {
	u32	raddr; /* address of data to be read */
	u32	pad1;
	u32	waddr;
	u32	pad2;
	u32	next;
	u32	pad3;
	u16	bytes;
	u8	rburst;
	u8	wburst;
	u16	bytes_xferred;	/* 16 bits, bytes xferred */

	/* bit 0: error
	 * bit 1: length error
	 * bit 2: crc error
	 * bit 3: truncated error
	 * bit 4: phy error
	 * bit 5: collision error
	 * bit 6: reserved
	 * bit 7: status eop for recv case
	 */
	u8	status;

	/* bit 0: eop
	 * bit 1: read_fixed
	 * bit 2: write fixed
	 * bits 3,4,5,6: Channel (always 0)
	 * bit 7: hardware owned
	 */
	u8	control;
} __packed;

#define SGDMA_DESC_LEN	sizeof(struct sgdma_descrip)

#define SGDMA_STATUS_ERR		BIT(0)
#define SGDMA_STATUS_LENGTH_ERR		BIT(1)
#define SGDMA_STATUS_CRC_ERR		BIT(2)
#define SGDMA_STATUS_TRUNC_ERR		BIT(3)
#define SGDMA_STATUS_PHY_ERR		BIT(4)
#define SGDMA_STATUS_COLL_ERR		BIT(5)
#define SGDMA_STATUS_EOP		BIT(7)

#define SGDMA_CONTROL_EOP		BIT(0)
#define SGDMA_CONTROL_RD_FIXED		BIT(1)
#define SGDMA_CONTROL_WR_FIXED		BIT(2)

/* Channel is always 0, so just zero initialize it */

#define SGDMA_CONTROL_HW_OWNED		BIT(7)

/* SGDMA register space */
struct sgdma_csr {
	/* bit 0: error
	 * bit 1: eop
	 * bit 2: descriptor completed
	 * bit 3: chain completed
	 * bit 4: busy
	 * remainder reserved
	 */
	u32	status;
	u32	pad1[3];

	/* bit 0: interrupt on error
	 * bit 1: interrupt on eop
	 * bit 2: interrupt after every descriptor
	 * bit 3: interrupt after last descrip in a chain
	 * bit 4: global interrupt enable
	 * bit 5: starts descriptor processing
	 * bit 6: stop core on dma error
	 * bit 7: interrupt on max descriptors
	 * bits 8-15: max descriptors to generate interrupt
	 * bit 16: Software reset
	 * bit 17: clears owned by hardware if 0, does not clear otherwise
	 * bit 18: enables descriptor polling mode
	 * bit 19-26: clocks before polling again
	 * bit 27-30: reserved
	 * bit 31: clear interrupt
	 */
	u32	control;
	u32	pad2[3];
	u32	next_descrip;
	u32	pad3[3];
};

#define sgdma_csroffs(a) (offsetof(struct sgdma_csr, a))
#define sgdma_descroffs(a) (offsetof(struct sgdma_descrip, a))

#define SGDMA_STSREG_ERR	BIT(0) /* Error */
#define SGDMA_STSREG_EOP	BIT(1) /* EOP */
#define SGDMA_STSREG_DESCRIP	BIT(2) /* Descriptor completed */
#define SGDMA_STSREG_CHAIN	BIT(3) /* Chain completed */
#define SGDMA_STSREG_BUSY	BIT(4) /* Controller busy */

#define SGDMA_CTRLREG_IOE	BIT(0) /* Interrupt on error */
#define SGDMA_CTRLREG_IOEOP	BIT(1) /* Interrupt on EOP */
#define SGDMA_CTRLREG_IDESCRIP	BIT(2) /* Interrupt after every descriptor */
#define SGDMA_CTRLREG_ILASTD	BIT(3) /* Interrupt after last descriptor */
#define SGDMA_CTRLREG_INTEN	BIT(4) /* Global Interrupt enable */
#define SGDMA_CTRLREG_START	BIT(5) /* starts descriptor processing */
#define SGDMA_CTRLREG_STOPERR	BIT(6) /* stop on dma error */
#define SGDMA_CTRLREG_INTMAX	BIT(7) /* Interrupt on max descriptors */
#define SGDMA_CTRLREG_RESET	BIT(16)/* Software reset */
#define SGDMA_CTRLREG_COBHW	BIT(17)/* Clears owned by hardware */
#define SGDMA_CTRLREG_POLL	BIT(18)/* enables descriptor polling mode */
#define SGDMA_CTRLREG_CLRINT	BIT(31)/* Clears interrupt */

#endif /* __ALTERA_SGDMAHW_H__ */
