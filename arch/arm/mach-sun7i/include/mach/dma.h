/*
 * arch/arm/mach-sun7i/include/mach/dma.h
 * (C) Copyright 2010-2015
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * liugang <liugang@reuuimllatech.com>
 *
 * sun7i dma driver header file
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __SW_DMA_H
#define __SW_DMA_H

#include <mach/hardware.h>

/* dma channel irq type */
typedef enum {
	CHAN_IRQ_NO 	= (0b000	),	/* none irq */
	CHAN_IRQ_HD	= (0b001	),	/* buf half done irq */
	CHAN_IRQ_FD	= (0b010	),	/* buf full done irq */
}dma_chan_irq_type;

/* wait clock cycles and data block size */
typedef struct {
	u32 src_wait_cyc 	: 8; /* bit0~7: source wait clock cycles n */
	u32 src_blk_sz 		: 8; /* bit8~15: source data block size n */
	u32 dst_wait_cyc 	: 8; /* bit16~23: dest wait clock cycles n */
	u32 dst_blk_sz 		: 8; /* bit24~31: dest data block size n */
}dma_para_t;

/* data width and burst */
#define DATA_WIDTH_8BIT		0
#define DATA_WIDTH_16BIT	1
#define DATA_WIDTH_32BIT	2
#define DATA_BRST_1 		0
#define DATA_BRST_4		1
#define DATA_BRST_8		2
typedef struct {
	u8 src_data_width;	/* src data width */
	u8 src_bst_len;		/* src burst length */
	u8 dst_data_width;	/* dst data width */
	u8 dst_bst_len;		/* dst burst length */
}xferunit_t;

/* address mode */
#define NDMA_ADDR_INCREMENT	0
#define NDMA_ADDR_NOCHANGE	1
#define DDMA_ADDR_LINEAR	0
#define DDMA_ADDR_IO		1
#define DDMA_ADDR_HORI_PAGE	2
#define DDMA_ADDR_VERT_PAGE	3
typedef struct {
	u16 src_addr_mode;	/* src address mode */
	u16 dst_addr_mode;	/* dst address mode */
}addrtype_t;

/* normal channel src drq type */
#define N_SRC_IR0_RX		0b00000
#define N_SRC_IR1_RX		0b00001
#define N_SRC_SPDIF_RX		0b00010
#define N_SRC_IIS0_RX		0b00011
#define N_SRC_IIS1_RX		0b00100
#define N_SRC_AC97_RX		0b00101
#define N_SRC_IIS2_RX		0b00110
/* resv: 1 */
#define N_SRC_UART0_RX		0b01000
#define N_SRC_UART1_RX		0b01001
#define N_SRC_UART2_RX		0b01010
#define N_SRC_UART3_RX		0b01011
#define N_SRC_UART4_RX		0b01100
#define N_SRC_UART5_RX		0b01101
#define N_SRC_UART6_RX		0b01110
#define N_SRC_UART7_RX		0b01111
#define N_SRC_HDMI_DDC_RX	0b10000
#define N_SRC_USB_EP1		0b10001
/* resv: 1 */
#define N_SRC_AUDIO_CODEC_AD	0b10011
/* resv: 1 */
#define N_SRC_SRAM		0b10101
#define N_SRC_SDRAM		0b10110
#define N_SRC_TP_AD		0b10111
#define N_SRC_SPI0_RX		0b11000
#define N_SRC_SPI1_RX		0b11001
#define N_SRC_SPI2_RX		0b11010
#define N_SRC_SPI3_RX		0b11011
#define N_SRC_USB_EP2		0b11100
#define N_SRC_USB_EP3		0b11101
#define N_SRC_USB_EP4		0b11110
#define N_SRC_USB_EP5		0b11111

/* normal channel dst drq type */
#define N_DST_IR0_TX		0b00000
#define N_DST_IR1_TX		0b00001
#define N_DST_SPDIF_TX		0b00010
#define N_DST_IIS0_TX		0b00011
#define N_DST_IIS1_TX		0b00100
#define N_DST_AC97_TX		0b00101
#define N_DST_IIS2_TX		0b00110
/* resv: 1 */
#define N_DST_UART0_TX		0b01000
#define N_DST_UART1_TX		0b01001
#define N_DST_UART2_TX		0b01010
#define N_DST_UART3_TX		0b01011
#define N_DST_UART4_TX		0b01100
#define N_DST_UART5_TX		0b01101
#define N_DST_UART6_TX		0b01110
#define N_DST_UART7_TX		0b01111
#define N_DST_HDMI_DDC_TX	0b10000
#define N_DST_USB_EP1		0b10001
/* resv: 1 */
#define N_DST_AUDIO_CODEC_DA	0b10011
/* resv: 1 */
#define N_DST_SRAM		0b10101
#define N_DST_SDRAM		0b10110
#define N_DST_TP_AD		0b10111
#define N_DST_SPI0_TX		0b11000
#define N_DST_SPI1_TX		0b11001
#define N_DST_SPI2_TX		0b11010
#define N_DST_SPI3_TX		0b11011
#define N_DST_USB_EP2		0b11100
#define N_DST_USB_EP3		0b11101
#define N_DST_USB_EP4		0b11110
#define N_DST_USB_EP5		0b11111

/* dedicate channel src drq type */
#define D_SRC_SRAM		0b00000
#define D_SRC_SDRAM		0b00001
/* resv: 1 */
#define D_SRC_NAND		0b00011
#define D_SRC_USB0		0b00100
/* resv: 2 */
#define D_SRC_EMAC_RX		0b00111
/* resv: 1 */
#define D_SRC_SPI1_RX		0b01001
/* resv: 1 */
#define D_SRC_SS_RX		0b01011 /* security system rx */
/* resv: 15 */
#define D_SRC_SPI0_RX		0b11011
/* resv: 1 */
#define D_SRC_SPI2_RX		0b11101
/* resv: 1 */
#define D_SRC_SPI3_RX		0b11111

/* dedicate channel dst drq type */
#define D_DST_SRAM		0b00000
#define D_DST_SDRAM		0b00001
/* resv: 1 */
#define D_DST_NAND		0b00011
#define D_DST_USB0		0b00100
/* resv: 1 */
#define D_DST_EMAC_TX		0b00110
/* resv: 1 */
#define D_DST_SPI1_TX		0b01000
/* resv: 1 */
#define D_DST_SS_TX		0b01010 /* security system tx */
/* resv: 3 */
#define D_DST_TCON0		0b01110
#define D_DST_TCON1		0b01111
/* resv: 7 */
#define D_DST_MSC		0b10111
#define D_DST_HDMI_AUD		0b11000
/* resv: 1 */
#define D_DST_SPI0_TX		0b11010
/* resv: 1 */
#define D_DST_SPI2_TX		0b11100
/* resv: 1 */
#define D_DST_SPI3_TX		0b11110
/* resv: 1 */

/* security define */
#define SRC_SECU_DST_SECU		0
#define SRC_SECU_DST_NON_SECU		1
#define SRC_NON_SECU_DST_SECU		2
#define SRC_NON_SECU_DST_NON_SECU	3

/* dma config para */
typedef struct {
	/*
	 * paras for dma ctrl reg
	 */
	xferunit_t	xfer_type;	/* dsta width and burst length */
	addrtype_t	address_type;	/* address type */
	bool		bconti_mode;	/* continue mode, true is continue mode, false not */
	u8		src_drq_type;	/* src drq type */
	u8		dst_drq_type;	/* dst drq type */
	/*
	 * other paras
	 */
	u32 		irq_spt;	/* channel irq supported, eg: CHAN_IRQ_HD | CHAN_IRQ_FD */
	/*
	 * these not always need set, so move to sw_dma_ctl
	 */
	//u8 		src_secu;	/* dma src security, 0: secure, 1: non-secure */
	//u8 		dst_secu;	/* dma dst security, 0: secure, 1: non-secure */
	//u16		wait_state;	/* for normal dma only */
	//dma_para_t	para;		/* dma para reg */
}dma_config_t;

/* dma operation type */
typedef enum {
	DMA_OP_START,  			/* start dma */
	DMA_OP_STOP,  			/* stop dma */
	DMA_OP_GET_BYTECNT_LEFT,  	/* get byte cnt left */
	DMA_OP_SET_SECURITY,  		/* set security */
	DMA_OP_SET_HD_CB,		/* set half done callback */
	DMA_OP_SET_FD_CB,		/* set full done callback */
	/*
	 * only for dedicate dma below
	 */
	DMA_OP_GET_STATUS,  		/* get channel status: idle/busy */
	DMA_OP_SET_PARA_REG,  		/* set para reg */
	/*
	 * only for normal dma below
	 */
	DMA_OP_SET_WAIT_STATE,  	/* set wait state status, 0~7 */
}dma_op_type_e;

/* dma handle type defination */
typedef void * dma_hdl_t;

/* irq callback func defination */
typedef void (* dma_cb)(dma_hdl_t dma_hdl, void *parg);

/* dma callback struct */
typedef struct {
	dma_cb 		func;	/* callback fuction */
	void 		*parg;	/* args of func */
}dma_cb_t;

/* dma channel type */
typedef enum {
	CHAN_NORMAL,		/* normal channel, id 0~7 */
	CHAN_DEDICATE,		/* dedicate channel, id 8~15 */
}dma_chan_type_e;

/* dma export fuction */
dma_hdl_t sw_dma_request(char * name, dma_chan_type_e type);
u32 sw_dma_release(dma_hdl_t dma_hdl);
u32 sw_dma_enqueue(dma_hdl_t dma_hdl, u32 src_addr, u32 dst_addr, u32 byte_cnt);
u32 sw_dma_config(dma_hdl_t dma_hdl, dma_config_t *pcfg);
u32 sw_dma_ctl(dma_hdl_t dma_hdl, dma_op_type_e op, void *parg);
int sw_dma_getposition(dma_hdl_t dma_hdl, u32 *psrc, u32 *pdst);
void sw_dma_dump_chan(dma_hdl_t dma_hdl);

#endif /* __SW_DMA_H */

