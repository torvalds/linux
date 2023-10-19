// SPDX-License-Identifier: GPL-2.0+
/*
 * Faraday FOTG210 USB OTG controller
 *
 * Copyright (C) 2013 Faraday Technology Corporation
 * Author: Yuan-Hsin Chen <yhchen@faraday-tech.com>
 */

#include <linux/kernel.h>

#define FOTG210_MAX_NUM_EP	5 /* ep0...ep4 */
#define FOTG210_MAX_FIFO_NUM	4 /* fifo0...fifo4 */

/* Global Mask of HC/OTG/DEV interrupt Register(0xC4) */
#define FOTG210_GMIR		0xC4
#define GMIR_INT_POLARITY	0x8 /*Active High*/
#define GMIR_MHC_INT		0x4
#define GMIR_MOTG_INT		0x2
#define GMIR_MDEV_INT		0x1

/*  Device Main Control Register(0x100) */
#define FOTG210_DMCR		0x100
#define DMCR_HS_EN		(1 << 6)
#define DMCR_CHIP_EN		(1 << 5)
#define DMCR_SFRST		(1 << 4)
#define DMCR_GOSUSP		(1 << 3)
#define DMCR_GLINT_EN		(1 << 2)
#define DMCR_HALF_SPEED		(1 << 1)
#define DMCR_CAP_RMWAKUP	(1 << 0)

/* Device Address Register(0x104) */
#define FOTG210_DAR		0x104
#define DAR_AFT_CONF		(1 << 7)

/* Device Test Register(0x108) */
#define FOTG210_DTR		0x108
#define DTR_TST_CLRFF		(1 << 0)

/* PHY Test Mode Selector register(0x114) */
#define FOTG210_PHYTMSR		0x114
#define PHYTMSR_TST_PKT		(1 << 4)
#define PHYTMSR_TST_SE0NAK	(1 << 3)
#define PHYTMSR_TST_KSTA	(1 << 2)
#define PHYTMSR_TST_JSTA	(1 << 1)
#define PHYTMSR_UNPLUG		(1 << 0)

/* Cx configuration and FIFO Empty Status register(0x120) */
#define FOTG210_DCFESR		0x120
#define DCFESR_FIFO_EMPTY(fifo)	(1 << 8 << (fifo))
#define DCFESR_CX_EMP		(1 << 5)
#define DCFESR_CX_CLR		(1 << 3)
#define DCFESR_CX_STL		(1 << 2)
#define DCFESR_TST_PKDONE	(1 << 1)
#define DCFESR_CX_DONE		(1 << 0)

/* Device IDLE Counter Register(0x124) */
#define FOTG210_DICR		0x124

/* Device Mask of Interrupt Group Register (0x130) */
#define FOTG210_DMIGR		0x130
#define DMIGR_MINT_G0		(1 << 0)

/* Device Mask of Interrupt Source Group 0(0x134) */
#define FOTG210_DMISGR0		0x134
#define DMISGR0_MCX_COMEND	(1 << 3)
#define DMISGR0_MCX_OUT_INT	(1 << 2)
#define DMISGR0_MCX_IN_INT	(1 << 1)
#define DMISGR0_MCX_SETUP_INT	(1 << 0)

/* Device Mask of Interrupt Source Group 1 Register(0x138)*/
#define FOTG210_DMISGR1		0x138
#define DMISGR1_MF3_IN_INT	(1 << 19)
#define DMISGR1_MF2_IN_INT	(1 << 18)
#define DMISGR1_MF1_IN_INT	(1 << 17)
#define DMISGR1_MF0_IN_INT	(1 << 16)
#define DMISGR1_MF_IN_INT(fifo)	(1 << (16 + (fifo)))
#define DMISGR1_MF3_SPK_INT	(1 << 7)
#define DMISGR1_MF3_OUT_INT	(1 << 6)
#define DMISGR1_MF2_SPK_INT	(1 << 5)
#define DMISGR1_MF2_OUT_INT	(1 << 4)
#define DMISGR1_MF1_SPK_INT	(1 << 3)
#define DMISGR1_MF1_OUT_INT	(1 << 2)
#define DMISGR1_MF0_SPK_INT	(1 << 1)
#define DMISGR1_MF0_OUT_INT	(1 << 0)
#define DMISGR1_MF_OUTSPK_INT(fifo)	(0x3 << (fifo) * 2)

/* Device Mask of Interrupt Source Group 2 Register (0x13C) */
#define FOTG210_DMISGR2		0x13C
#define DMISGR2_MDMA_ERROR	(1 << 8)
#define DMISGR2_MDMA_CMPLT	(1 << 7)

/* Device Interrupt group Register (0x140) */
#define FOTG210_DIGR		0x140
#define DIGR_INT_G2		(1 << 2)
#define DIGR_INT_G1		(1 << 1)
#define DIGR_INT_G0		(1 << 0)

/* Device Interrupt Source Group 0 Register (0x144) */
#define FOTG210_DISGR0		0x144
#define DISGR0_CX_COMABT_INT	(1 << 5)
#define DISGR0_CX_COMFAIL_INT	(1 << 4)
#define DISGR0_CX_COMEND_INT	(1 << 3)
#define DISGR0_CX_OUT_INT	(1 << 2)
#define DISGR0_CX_IN_INT	(1 << 1)
#define DISGR0_CX_SETUP_INT	(1 << 0)

/* Device Interrupt Source Group 1 Register (0x148) */
#define FOTG210_DISGR1		0x148
#define DISGR1_OUT_INT(fifo)	(1 << ((fifo) * 2))
#define DISGR1_SPK_INT(fifo)	(1 << 1 << ((fifo) * 2))
#define DISGR1_IN_INT(fifo)	(1 << 16 << (fifo))

/* Device Interrupt Source Group 2 Register (0x14C) */
#define FOTG210_DISGR2		0x14C
#define DISGR2_DMA_ERROR	(1 << 8)
#define DISGR2_DMA_CMPLT	(1 << 7)
#define DISGR2_RX0BYTE_INT	(1 << 6)
#define DISGR2_TX0BYTE_INT	(1 << 5)
#define DISGR2_ISO_SEQ_ABORT_INT	(1 << 4)
#define DISGR2_ISO_SEQ_ERR_INT	(1 << 3)
#define DISGR2_RESM_INT		(1 << 2)
#define DISGR2_SUSP_INT		(1 << 1)
#define DISGR2_USBRST_INT	(1 << 0)

/* Device Receive Zero-Length Data Packet Register (0x150)*/
#define FOTG210_RX0BYTE		0x150
#define RX0BYTE_EP8		(1 << 7)
#define RX0BYTE_EP7		(1 << 6)
#define RX0BYTE_EP6		(1 << 5)
#define RX0BYTE_EP5		(1 << 4)
#define RX0BYTE_EP4		(1 << 3)
#define RX0BYTE_EP3		(1 << 2)
#define RX0BYTE_EP2		(1 << 1)
#define RX0BYTE_EP1		(1 << 0)

/* Device Transfer Zero-Length Data Packet Register (0x154)*/
#define FOTG210_TX0BYTE		0x154
#define TX0BYTE_EP8		(1 << 7)
#define TX0BYTE_EP7		(1 << 6)
#define TX0BYTE_EP6		(1 << 5)
#define TX0BYTE_EP5		(1 << 4)
#define TX0BYTE_EP4		(1 << 3)
#define TX0BYTE_EP3		(1 << 2)
#define TX0BYTE_EP2		(1 << 1)
#define TX0BYTE_EP1		(1 << 0)

/* Device IN Endpoint x MaxPacketSize Register(0x160+4*(x-1)) */
#define FOTG210_INEPMPSR(ep)	(0x160 + 4 * ((ep) - 1))
#define INOUTEPMPSR_MPS(mps)	((mps) & 0x2FF)
#define INOUTEPMPSR_STL_EP	(1 << 11)
#define INOUTEPMPSR_RESET_TSEQ	(1 << 12)

/* Device OUT Endpoint x MaxPacketSize Register(0x180+4*(x-1)) */
#define FOTG210_OUTEPMPSR(ep)	(0x180 + 4 * ((ep) - 1))

/* Device Endpoint 1~4 Map Register (0x1A0) */
#define FOTG210_EPMAP		0x1A0
#define EPMAP_FIFONO(ep, dir)		\
	((((ep) - 1) << ((ep) - 1) * 8) << ((dir) ? 0 : 4))
#define EPMAP_FIFONOMSK(ep, dir)	\
	((3 << ((ep) - 1) * 8) << ((dir) ? 0 : 4))

/* Device FIFO Map Register (0x1A8) */
#define FOTG210_FIFOMAP		0x1A8
#define FIFOMAP_DIROUT(fifo)	(0x0 << 4 << (fifo) * 8)
#define FIFOMAP_DIRIN(fifo)	(0x1 << 4 << (fifo) * 8)
#define FIFOMAP_BIDIR(fifo)	(0x2 << 4 << (fifo) * 8)
#define FIFOMAP_NA(fifo)	(0x3 << 4 << (fifo) * 8)
#define FIFOMAP_EPNO(ep)	((ep) << ((ep) - 1) * 8)
#define FIFOMAP_EPNOMSK(ep)	(0xF << ((ep) - 1) * 8)

/* Device FIFO Confuguration Register (0x1AC) */
#define FOTG210_FIFOCF		0x1AC
#define FIFOCF_TYPE(type, fifo)	((type) << (fifo) * 8)
#define FIFOCF_BLK_SIN(fifo)	(0x0 << (fifo) * 8 << 2)
#define FIFOCF_BLK_DUB(fifo)	(0x1 << (fifo) * 8 << 2)
#define FIFOCF_BLK_TRI(fifo)	(0x2 << (fifo) * 8 << 2)
#define FIFOCF_BLKSZ_512(fifo)	(0x0 << (fifo) * 8 << 4)
#define FIFOCF_BLKSZ_1024(fifo)	(0x1 << (fifo) * 8 << 4)
#define FIFOCF_FIFO_EN(fifo)	(0x1 << (fifo) * 8 << 5)

/* Device FIFO n Instruction and Byte Count Register (0x1B0+4*n) */
#define FOTG210_FIBCR(fifo)	(0x1B0 + (fifo) * 4)
#define FIBCR_BCFX		0x7FF
#define FIBCR_FFRST		(1 << 12)

/* Device DMA Target FIFO Number Register (0x1C0) */
#define FOTG210_DMATFNR		0x1C0
#define DMATFNR_ACC_CXF		(1 << 4)
#define DMATFNR_ACC_F3		(1 << 3)
#define DMATFNR_ACC_F2		(1 << 2)
#define DMATFNR_ACC_F1		(1 << 1)
#define DMATFNR_ACC_F0		(1 << 0)
#define DMATFNR_ACC_FN(fifo)	(1 << (fifo))
#define DMATFNR_DISDMA		0

/* Device DMA Controller Parameter setting 1 Register (0x1C8) */
#define FOTG210_DMACPSR1	0x1C8
#define DMACPSR1_DMA_LEN(len)	(((len) & 0xFFFF) << 8)
#define DMACPSR1_DMA_ABORT	(1 << 3)
#define DMACPSR1_DMA_TYPE(dir_in)	(((dir_in) ? 1 : 0) << 1)
#define DMACPSR1_DMA_START	(1 << 0)

/* Device DMA Controller Parameter setting 2 Register (0x1CC) */
#define FOTG210_DMACPSR2	0x1CC

/* Device DMA Controller Parameter setting 3 Register (0x1CC) */
#define FOTG210_CXPORT		0x1D0

struct fotg210_request {
	struct usb_request	req;
	struct list_head	queue;
};

struct fotg210_ep {
	struct usb_ep		ep;
	struct fotg210_udc	*fotg210;

	struct list_head	queue;
	unsigned		stall:1;
	unsigned		wedged:1;
	unsigned		use_dma:1;

	unsigned char		epnum;
	unsigned char		type;
	unsigned char		dir_in;
	unsigned int		maxp;
	const struct usb_endpoint_descriptor	*desc;
};

struct fotg210_udc {
	spinlock_t		lock; /* protect the struct */
	void __iomem		*reg;

	unsigned long		irq_trigger;

	struct usb_gadget		gadget;
	struct usb_gadget_driver	*driver;

	struct fotg210_ep	*ep[FOTG210_MAX_NUM_EP];

	struct usb_request	*ep0_req;	/* for internal request */
	__le16			ep0_data;
	u8			ep0_dir;	/* 0/0x80  out/in */

	u8			reenum;		/* if re-enumeration */
};

#define gadget_to_fotg210(g)	container_of((g), struct fotg210_udc, gadget)
