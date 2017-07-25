/* Freescale QUICC Engine HDLC Device Driver
 *
 * Copyright 2014 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _UCC_HDLC_H_
#define _UCC_HDLC_H_

#include <linux/kernel.h>
#include <linux/list.h>

#include <soc/fsl/qe/immap_qe.h>
#include <soc/fsl/qe/qe.h>

#include <soc/fsl/qe/ucc.h>
#include <soc/fsl/qe/ucc_fast.h>

/* UCC HDLC event register */
#define UCCE_HDLC_RX_EVENTS	\
(UCC_HDLC_UCCE_RXF | UCC_HDLC_UCCE_RXB | UCC_HDLC_UCCE_BSY)
#define UCCE_HDLC_TX_EVENTS	(UCC_HDLC_UCCE_TXB | UCC_HDLC_UCCE_TXE)

struct ucc_hdlc_param {
	__be16 riptr;
	__be16 tiptr;
	__be16 res0;
	__be16 mrblr;
	__be32 rstate;
	__be32 rbase;
	__be16 rbdstat;
	__be16 rbdlen;
	__be32 rdptr;
	__be32 tstate;
	__be32 tbase;
	__be16 tbdstat;
	__be16 tbdlen;
	__be32 tdptr;
	__be32 rbptr;
	__be32 tbptr;
	__be32 rcrc;
	__be32 res1;
	__be32 tcrc;
	__be32 res2;
	__be32 res3;
	__be32 c_mask;
	__be32 c_pres;
	__be16 disfc;
	__be16 crcec;
	__be16 abtsc;
	__be16 nmarc;
	__be32 max_cnt;
	__be16 mflr;
	__be16 rfthr;
	__be16 rfcnt;
	__be16 hmask;
	__be16 haddr1;
	__be16 haddr2;
	__be16 haddr3;
	__be16 haddr4;
	__be16 ts_tmp;
	__be16 tmp_mb;
};

struct ucc_hdlc_private {
	struct ucc_tdm	*utdm;
	struct ucc_tdm_info *ut_info;
	struct ucc_fast_private *uccf;
	struct device *dev;
	struct net_device *ndev;
	struct napi_struct napi;
	struct ucc_fast __iomem *uf_regs;	/* UCC Fast registers */
	struct ucc_hdlc_param __iomem *ucc_pram;
	u16 tsa;
	bool hdlc_busy;
	bool loopback;
	bool hdlc_bus;

	u8 *tx_buffer;
	u8 *rx_buffer;
	dma_addr_t dma_tx_addr;
	dma_addr_t dma_rx_addr;

	struct qe_bd *tx_bd_base;
	struct qe_bd *rx_bd_base;
	dma_addr_t dma_tx_bd;
	dma_addr_t dma_rx_bd;
	struct qe_bd *curtx_bd;
	struct qe_bd *currx_bd;
	struct qe_bd *dirty_tx;
	u16 currx_bdnum;

	struct sk_buff **tx_skbuff;
	struct sk_buff **rx_skbuff;
	u16 skb_curtx;
	u16 skb_currx;
	unsigned short skb_dirtytx;

	unsigned short tx_ring_size;
	unsigned short rx_ring_size;
	u32 ucc_pram_offset;

	unsigned short encoding;
	unsigned short parity;
	u32 clocking;
	spinlock_t lock;	/* lock for Tx BD and Tx buffer */
#ifdef CONFIG_PM
	struct ucc_hdlc_param *ucc_pram_bak;
	u32 gumr;
	u8 guemr;
	u32 cmxsi1cr_l, cmxsi1cr_h;
	u32 cmxsi1syr;
	u32 cmxucr[4];
#endif
};

#define TX_BD_RING_LEN	0x10
#define RX_BD_RING_LEN	0x20
#define RX_CLEAN_MAX	0x10
#define NUM_OF_BUF	4
#define MAX_RX_BUF_LENGTH	(48 * 0x20)
#define MAX_FRAME_LENGTH	(MAX_RX_BUF_LENGTH + 8)
#define ALIGNMENT_OF_UCC_HDLC_PRAM	64
#define SI_BANK_SIZE	128
#define MAX_HDLC_NUM	4
#define HDLC_HEAD_LEN	2
#define HDLC_CRC_SIZE	2
#define TX_RING_MOD_MASK(size) (size - 1)
#define RX_RING_MOD_MASK(size) (size - 1)

#define HDLC_HEAD_MASK		0x0000
#define DEFAULT_HDLC_HEAD	0xff44
#define DEFAULT_ADDR_MASK	0x00ff
#define DEFAULT_HDLC_ADDR	0x00ff

#define BMR_GBL			0x20000000
#define BMR_BIG_ENDIAN		0x10000000
#define CRC_16BIT_MASK		0x0000F0B8
#define CRC_16BIT_PRES		0x0000FFFF
#define DEFAULT_RFTHR		1

#define DEFAULT_PPP_HEAD    0xff03

#endif
