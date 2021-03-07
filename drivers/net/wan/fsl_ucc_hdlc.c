// SPDX-License-Identifier: GPL-2.0-or-later
/* Freescale QUICC Engine HDLC Device Driver
 *
 * Copyright 2016 Freescale Semiconductor Inc.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/hdlc.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <soc/fsl/qe/qe_tdm.h>
#include <uapi/linux/if_arp.h>

#include "fsl_ucc_hdlc.h"

#define DRV_DESC "Freescale QE UCC HDLC Driver"
#define DRV_NAME "ucc_hdlc"

#define TDM_PPPOHT_SLIC_MAXIN
#define RX_BD_ERRORS (R_CD_S | R_OV_S | R_CR_S | R_AB_S | R_NO_S | R_LG_S)

static struct ucc_tdm_info utdm_primary_info = {
	.uf_info = {
		.tsa = 0,
		.cdp = 0,
		.cds = 1,
		.ctsp = 1,
		.ctss = 1,
		.revd = 0,
		.urfs = 256,
		.utfs = 256,
		.urfet = 128,
		.urfset = 192,
		.utfet = 128,
		.utftt = 0x40,
		.ufpt = 256,
		.mode = UCC_FAST_PROTOCOL_MODE_HDLC,
		.ttx_trx = UCC_FAST_GUMR_TRANSPARENT_TTX_TRX_NORMAL,
		.tenc = UCC_FAST_TX_ENCODING_NRZ,
		.renc = UCC_FAST_RX_ENCODING_NRZ,
		.tcrc = UCC_FAST_16_BIT_CRC,
		.synl = UCC_FAST_SYNC_LEN_NOT_USED,
	},

	.si_info = {
#ifdef TDM_PPPOHT_SLIC_MAXIN
		.simr_rfsd = 1,
		.simr_tfsd = 2,
#else
		.simr_rfsd = 0,
		.simr_tfsd = 0,
#endif
		.simr_crt = 0,
		.simr_sl = 0,
		.simr_ce = 1,
		.simr_fe = 1,
		.simr_gm = 0,
	},
};

static struct ucc_tdm_info utdm_info[UCC_MAX_NUM];

static int uhdlc_init(struct ucc_hdlc_private *priv)
{
	struct ucc_tdm_info *ut_info;
	struct ucc_fast_info *uf_info;
	u32 cecr_subblock;
	u16 bd_status;
	int ret, i;
	void *bd_buffer;
	dma_addr_t bd_dma_addr;
	s32 riptr;
	s32 tiptr;
	u32 gumr;

	ut_info = priv->ut_info;
	uf_info = &ut_info->uf_info;

	if (priv->tsa) {
		uf_info->tsa = 1;
		uf_info->ctsp = 1;
		uf_info->cds = 1;
		uf_info->ctss = 1;
	} else {
		uf_info->cds = 0;
		uf_info->ctsp = 0;
		uf_info->ctss = 0;
	}

	/* This sets HPM register in CMXUCR register which configures a
	 * open drain connected HDLC bus
	 */
	if (priv->hdlc_bus)
		uf_info->brkpt_support = 1;

	uf_info->uccm_mask = ((UCC_HDLC_UCCE_RXB | UCC_HDLC_UCCE_RXF |
				UCC_HDLC_UCCE_TXB) << 16);

	ret = ucc_fast_init(uf_info, &priv->uccf);
	if (ret) {
		dev_err(priv->dev, "Failed to init uccf.");
		return ret;
	}

	priv->uf_regs = priv->uccf->uf_regs;
	ucc_fast_disable(priv->uccf, COMM_DIR_RX | COMM_DIR_TX);

	/* Loopback mode */
	if (priv->loopback) {
		dev_info(priv->dev, "Loopback Mode\n");
		/* use the same clock when work in loopback */
		qe_setbrg(ut_info->uf_info.rx_clock, 20000000, 1);

		gumr = ioread32be(&priv->uf_regs->gumr);
		gumr |= (UCC_FAST_GUMR_LOOPBACK | UCC_FAST_GUMR_CDS |
			 UCC_FAST_GUMR_TCI);
		gumr &= ~(UCC_FAST_GUMR_CTSP | UCC_FAST_GUMR_RSYN);
		iowrite32be(gumr, &priv->uf_regs->gumr);
	}

	/* Initialize SI */
	if (priv->tsa)
		ucc_tdm_init(priv->utdm, priv->ut_info);

	/* Write to QE CECR, UCCx channel to Stop Transmission */
	cecr_subblock = ucc_fast_get_qe_cr_subblock(uf_info->ucc_num);
	ret = qe_issue_cmd(QE_STOP_TX, cecr_subblock,
			   QE_CR_PROTOCOL_UNSPECIFIED, 0);

	/* Set UPSMR normal mode (need fixed)*/
	iowrite32be(0, &priv->uf_regs->upsmr);

	/* hdlc_bus mode */
	if (priv->hdlc_bus) {
		u32 upsmr;

		dev_info(priv->dev, "HDLC bus Mode\n");
		upsmr = ioread32be(&priv->uf_regs->upsmr);

		/* bus mode and retransmit enable, with collision window
		 * set to 8 bytes
		 */
		upsmr |= UCC_HDLC_UPSMR_RTE | UCC_HDLC_UPSMR_BUS |
				UCC_HDLC_UPSMR_CW8;
		iowrite32be(upsmr, &priv->uf_regs->upsmr);

		/* explicitly disable CDS & CTSP */
		gumr = ioread32be(&priv->uf_regs->gumr);
		gumr &= ~(UCC_FAST_GUMR_CDS | UCC_FAST_GUMR_CTSP);
		/* set automatic sync to explicitly ignore CD signal */
		gumr |= UCC_FAST_GUMR_SYNL_AUTO;
		iowrite32be(gumr, &priv->uf_regs->gumr);
	}

	priv->rx_ring_size = RX_BD_RING_LEN;
	priv->tx_ring_size = TX_BD_RING_LEN;
	/* Alloc Rx BD */
	priv->rx_bd_base = dma_alloc_coherent(priv->dev,
			RX_BD_RING_LEN * sizeof(struct qe_bd),
			&priv->dma_rx_bd, GFP_KERNEL);

	if (!priv->rx_bd_base) {
		dev_err(priv->dev, "Cannot allocate MURAM memory for RxBDs\n");
		ret = -ENOMEM;
		goto free_uccf;
	}

	/* Alloc Tx BD */
	priv->tx_bd_base = dma_alloc_coherent(priv->dev,
			TX_BD_RING_LEN * sizeof(struct qe_bd),
			&priv->dma_tx_bd, GFP_KERNEL);

	if (!priv->tx_bd_base) {
		dev_err(priv->dev, "Cannot allocate MURAM memory for TxBDs\n");
		ret = -ENOMEM;
		goto free_rx_bd;
	}

	/* Alloc parameter ram for ucc hdlc */
	priv->ucc_pram_offset = qe_muram_alloc(sizeof(struct ucc_hdlc_param),
				ALIGNMENT_OF_UCC_HDLC_PRAM);

	if (priv->ucc_pram_offset < 0) {
		dev_err(priv->dev, "Can not allocate MURAM for hdlc parameter.\n");
		ret = -ENOMEM;
		goto free_tx_bd;
	}

	priv->rx_skbuff = kcalloc(priv->rx_ring_size,
				  sizeof(*priv->rx_skbuff),
				  GFP_KERNEL);
	if (!priv->rx_skbuff) {
		ret = -ENOMEM;
		goto free_ucc_pram;
	}

	priv->tx_skbuff = kcalloc(priv->tx_ring_size,
				  sizeof(*priv->tx_skbuff),
				  GFP_KERNEL);
	if (!priv->tx_skbuff) {
		ret = -ENOMEM;
		goto free_rx_skbuff;
	}

	priv->skb_curtx = 0;
	priv->skb_dirtytx = 0;
	priv->curtx_bd = priv->tx_bd_base;
	priv->dirty_tx = priv->tx_bd_base;
	priv->currx_bd = priv->rx_bd_base;
	priv->currx_bdnum = 0;

	/* init parameter base */
	cecr_subblock = ucc_fast_get_qe_cr_subblock(uf_info->ucc_num);
	ret = qe_issue_cmd(QE_ASSIGN_PAGE_TO_DEVICE, cecr_subblock,
			   QE_CR_PROTOCOL_UNSPECIFIED, priv->ucc_pram_offset);

	priv->ucc_pram = (struct ucc_hdlc_param __iomem *)
					qe_muram_addr(priv->ucc_pram_offset);

	/* Zero out parameter ram */
	memset_io(priv->ucc_pram, 0, sizeof(struct ucc_hdlc_param));

	/* Alloc riptr, tiptr */
	riptr = qe_muram_alloc(32, 32);
	if (riptr < 0) {
		dev_err(priv->dev, "Cannot allocate MURAM mem for Receive internal temp data pointer\n");
		ret = -ENOMEM;
		goto free_tx_skbuff;
	}

	tiptr = qe_muram_alloc(32, 32);
	if (tiptr < 0) {
		dev_err(priv->dev, "Cannot allocate MURAM mem for Transmit internal temp data pointer\n");
		ret = -ENOMEM;
		goto free_riptr;
	}
	if (riptr != (u16)riptr || tiptr != (u16)tiptr) {
		dev_err(priv->dev, "MURAM allocation out of addressable range\n");
		ret = -ENOMEM;
		goto free_tiptr;
	}

	/* Set RIPTR, TIPTR */
	iowrite16be(riptr, &priv->ucc_pram->riptr);
	iowrite16be(tiptr, &priv->ucc_pram->tiptr);

	/* Set MRBLR */
	iowrite16be(MAX_RX_BUF_LENGTH, &priv->ucc_pram->mrblr);

	/* Set RBASE, TBASE */
	iowrite32be(priv->dma_rx_bd, &priv->ucc_pram->rbase);
	iowrite32be(priv->dma_tx_bd, &priv->ucc_pram->tbase);

	/* Set RSTATE, TSTATE */
	iowrite32be(BMR_GBL | BMR_BIG_ENDIAN, &priv->ucc_pram->rstate);
	iowrite32be(BMR_GBL | BMR_BIG_ENDIAN, &priv->ucc_pram->tstate);

	/* Set C_MASK, C_PRES for 16bit CRC */
	iowrite32be(CRC_16BIT_MASK, &priv->ucc_pram->c_mask);
	iowrite32be(CRC_16BIT_PRES, &priv->ucc_pram->c_pres);

	iowrite16be(MAX_FRAME_LENGTH, &priv->ucc_pram->mflr);
	iowrite16be(DEFAULT_RFTHR, &priv->ucc_pram->rfthr);
	iowrite16be(DEFAULT_RFTHR, &priv->ucc_pram->rfcnt);
	iowrite16be(priv->hmask, &priv->ucc_pram->hmask);
	iowrite16be(DEFAULT_HDLC_ADDR, &priv->ucc_pram->haddr1);
	iowrite16be(DEFAULT_HDLC_ADDR, &priv->ucc_pram->haddr2);
	iowrite16be(DEFAULT_HDLC_ADDR, &priv->ucc_pram->haddr3);
	iowrite16be(DEFAULT_HDLC_ADDR, &priv->ucc_pram->haddr4);

	/* Get BD buffer */
	bd_buffer = dma_alloc_coherent(priv->dev,
				       (RX_BD_RING_LEN + TX_BD_RING_LEN) * MAX_RX_BUF_LENGTH,
				       &bd_dma_addr, GFP_KERNEL);

	if (!bd_buffer) {
		dev_err(priv->dev, "Could not allocate buffer descriptors\n");
		ret = -ENOMEM;
		goto free_tiptr;
	}

	priv->rx_buffer = bd_buffer;
	priv->tx_buffer = bd_buffer + RX_BD_RING_LEN * MAX_RX_BUF_LENGTH;

	priv->dma_rx_addr = bd_dma_addr;
	priv->dma_tx_addr = bd_dma_addr + RX_BD_RING_LEN * MAX_RX_BUF_LENGTH;

	for (i = 0; i < RX_BD_RING_LEN; i++) {
		if (i < (RX_BD_RING_LEN - 1))
			bd_status = R_E_S | R_I_S;
		else
			bd_status = R_E_S | R_I_S | R_W_S;

		iowrite16be(bd_status, &priv->rx_bd_base[i].status);
		iowrite32be(priv->dma_rx_addr + i * MAX_RX_BUF_LENGTH,
			    &priv->rx_bd_base[i].buf);
	}

	for (i = 0; i < TX_BD_RING_LEN; i++) {
		if (i < (TX_BD_RING_LEN - 1))
			bd_status =  T_I_S | T_TC_S;
		else
			bd_status =  T_I_S | T_TC_S | T_W_S;

		iowrite16be(bd_status, &priv->tx_bd_base[i].status);
		iowrite32be(priv->dma_tx_addr + i * MAX_RX_BUF_LENGTH,
			    &priv->tx_bd_base[i].buf);
	}

	return 0;

free_tiptr:
	qe_muram_free(tiptr);
free_riptr:
	qe_muram_free(riptr);
free_tx_skbuff:
	kfree(priv->tx_skbuff);
free_rx_skbuff:
	kfree(priv->rx_skbuff);
free_ucc_pram:
	qe_muram_free(priv->ucc_pram_offset);
free_tx_bd:
	dma_free_coherent(priv->dev,
			  TX_BD_RING_LEN * sizeof(struct qe_bd),
			  priv->tx_bd_base, priv->dma_tx_bd);
free_rx_bd:
	dma_free_coherent(priv->dev,
			  RX_BD_RING_LEN * sizeof(struct qe_bd),
			  priv->rx_bd_base, priv->dma_rx_bd);
free_uccf:
	ucc_fast_free(priv->uccf);

	return ret;
}

static netdev_tx_t ucc_hdlc_tx(struct sk_buff *skb, struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct ucc_hdlc_private *priv = (struct ucc_hdlc_private *)hdlc->priv;
	struct qe_bd __iomem *bd;
	u16 bd_status;
	unsigned long flags;
	u16 *proto_head;

	switch (dev->type) {
	case ARPHRD_RAWHDLC:
		if (skb_headroom(skb) < HDLC_HEAD_LEN) {
			dev->stats.tx_dropped++;
			dev_kfree_skb(skb);
			netdev_err(dev, "No enough space for hdlc head\n");
			return -ENOMEM;
		}

		skb_push(skb, HDLC_HEAD_LEN);

		proto_head = (u16 *)skb->data;
		*proto_head = htons(DEFAULT_HDLC_HEAD);

		dev->stats.tx_bytes += skb->len;
		break;

	case ARPHRD_PPP:
		proto_head = (u16 *)skb->data;
		if (*proto_head != htons(DEFAULT_PPP_HEAD)) {
			dev->stats.tx_dropped++;
			dev_kfree_skb(skb);
			netdev_err(dev, "Wrong ppp header\n");
			return -ENOMEM;
		}

		dev->stats.tx_bytes += skb->len;
		break;

	case ARPHRD_ETHER:
		dev->stats.tx_bytes += skb->len;
		break;

	default:
		dev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		return -ENOMEM;
	}
	netdev_sent_queue(dev, skb->len);
	spin_lock_irqsave(&priv->lock, flags);

	/* Start from the next BD that should be filled */
	bd = priv->curtx_bd;
	bd_status = ioread16be(&bd->status);
	/* Save the skb pointer so we can free it later */
	priv->tx_skbuff[priv->skb_curtx] = skb;

	/* Update the current skb pointer (wrapping if this was the last) */
	priv->skb_curtx =
	    (priv->skb_curtx + 1) & TX_RING_MOD_MASK(TX_BD_RING_LEN);

	/* copy skb data to tx buffer for sdma processing */
	memcpy(priv->tx_buffer + (be32_to_cpu(bd->buf) - priv->dma_tx_addr),
	       skb->data, skb->len);

	/* set bd status and length */
	bd_status = (bd_status & T_W_S) | T_R_S | T_I_S | T_L_S | T_TC_S;

	iowrite16be(skb->len, &bd->length);
	iowrite16be(bd_status, &bd->status);

	/* Move to next BD in the ring */
	if (!(bd_status & T_W_S))
		bd += 1;
	else
		bd = priv->tx_bd_base;

	if (bd == priv->dirty_tx) {
		if (!netif_queue_stopped(dev))
			netif_stop_queue(dev);
	}

	priv->curtx_bd = bd;

	spin_unlock_irqrestore(&priv->lock, flags);

	return NETDEV_TX_OK;
}

static int hdlc_tx_restart(struct ucc_hdlc_private *priv)
{
	u32 cecr_subblock;

	cecr_subblock =
		ucc_fast_get_qe_cr_subblock(priv->ut_info->uf_info.ucc_num);

	qe_issue_cmd(QE_RESTART_TX, cecr_subblock,
		     QE_CR_PROTOCOL_UNSPECIFIED, 0);
	return 0;
}

static int hdlc_tx_done(struct ucc_hdlc_private *priv)
{
	/* Start from the next BD that should be filled */
	struct net_device *dev = priv->ndev;
	unsigned int bytes_sent = 0;
	int howmany = 0;
	struct qe_bd *bd;		/* BD pointer */
	u16 bd_status;
	int tx_restart = 0;

	bd = priv->dirty_tx;
	bd_status = ioread16be(&bd->status);

	/* Normal processing. */
	while ((bd_status & T_R_S) == 0) {
		struct sk_buff *skb;

		if (bd_status & T_UN_S) { /* Underrun */
			dev->stats.tx_fifo_errors++;
			tx_restart = 1;
		}
		if (bd_status & T_CT_S) { /* Carrier lost */
			dev->stats.tx_carrier_errors++;
			tx_restart = 1;
		}

		/* BD contains already transmitted buffer.   */
		/* Handle the transmitted buffer and release */
		/* the BD to be used with the current frame  */

		skb = priv->tx_skbuff[priv->skb_dirtytx];
		if (!skb)
			break;
		howmany++;
		bytes_sent += skb->len;
		dev->stats.tx_packets++;
		memset(priv->tx_buffer +
		       (be32_to_cpu(bd->buf) - priv->dma_tx_addr),
		       0, skb->len);
		dev_consume_skb_irq(skb);

		priv->tx_skbuff[priv->skb_dirtytx] = NULL;
		priv->skb_dirtytx =
		    (priv->skb_dirtytx +
		     1) & TX_RING_MOD_MASK(TX_BD_RING_LEN);

		/* We freed a buffer, so now we can restart transmission */
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);

		/* Advance the confirmation BD pointer */
		if (!(bd_status & T_W_S))
			bd += 1;
		else
			bd = priv->tx_bd_base;
		bd_status = ioread16be(&bd->status);
	}
	priv->dirty_tx = bd;

	if (tx_restart)
		hdlc_tx_restart(priv);

	netdev_completed_queue(dev, howmany, bytes_sent);
	return 0;
}

static int hdlc_rx_done(struct ucc_hdlc_private *priv, int rx_work_limit)
{
	struct net_device *dev = priv->ndev;
	struct sk_buff *skb = NULL;
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct qe_bd *bd;
	u16 bd_status;
	u16 length, howmany = 0;
	u8 *bdbuffer;

	bd = priv->currx_bd;
	bd_status = ioread16be(&bd->status);

	/* while there are received buffers and BD is full (~R_E) */
	while (!((bd_status & (R_E_S)) || (--rx_work_limit < 0))) {
		if (bd_status & (RX_BD_ERRORS)) {
			dev->stats.rx_errors++;

			if (bd_status & R_CD_S)
				dev->stats.collisions++;
			if (bd_status & R_OV_S)
				dev->stats.rx_fifo_errors++;
			if (bd_status & R_CR_S)
				dev->stats.rx_crc_errors++;
			if (bd_status & R_AB_S)
				dev->stats.rx_over_errors++;
			if (bd_status & R_NO_S)
				dev->stats.rx_frame_errors++;
			if (bd_status & R_LG_S)
				dev->stats.rx_length_errors++;

			goto recycle;
		}
		bdbuffer = priv->rx_buffer +
			(priv->currx_bdnum * MAX_RX_BUF_LENGTH);
		length = ioread16be(&bd->length);

		switch (dev->type) {
		case ARPHRD_RAWHDLC:
			bdbuffer += HDLC_HEAD_LEN;
			length -= (HDLC_HEAD_LEN + HDLC_CRC_SIZE);

			skb = dev_alloc_skb(length);
			if (!skb) {
				dev->stats.rx_dropped++;
				return -ENOMEM;
			}

			skb_put(skb, length);
			skb->len = length;
			skb->dev = dev;
			memcpy(skb->data, bdbuffer, length);
			break;

		case ARPHRD_PPP:
		case ARPHRD_ETHER:
			length -= HDLC_CRC_SIZE;

			skb = dev_alloc_skb(length);
			if (!skb) {
				dev->stats.rx_dropped++;
				return -ENOMEM;
			}

			skb_put(skb, length);
			skb->len = length;
			skb->dev = dev;
			memcpy(skb->data, bdbuffer, length);
			break;
		}

		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;
		howmany++;
		if (hdlc->proto)
			skb->protocol = hdlc_type_trans(skb, dev);
		netif_receive_skb(skb);

recycle:
		iowrite16be((bd_status & R_W_S) | R_E_S | R_I_S, &bd->status);

		/* update to point at the next bd */
		if (bd_status & R_W_S) {
			priv->currx_bdnum = 0;
			bd = priv->rx_bd_base;
		} else {
			if (priv->currx_bdnum < (RX_BD_RING_LEN - 1))
				priv->currx_bdnum += 1;
			else
				priv->currx_bdnum = RX_BD_RING_LEN - 1;

			bd += 1;
		}

		bd_status = ioread16be(&bd->status);
	}

	priv->currx_bd = bd;
	return howmany;
}

static int ucc_hdlc_poll(struct napi_struct *napi, int budget)
{
	struct ucc_hdlc_private *priv = container_of(napi,
						     struct ucc_hdlc_private,
						     napi);
	int howmany;

	/* Tx event processing */
	spin_lock(&priv->lock);
	hdlc_tx_done(priv);
	spin_unlock(&priv->lock);

	howmany = 0;
	howmany += hdlc_rx_done(priv, budget - howmany);

	if (howmany < budget) {
		napi_complete_done(napi, howmany);
		qe_setbits_be32(priv->uccf->p_uccm,
				(UCCE_HDLC_RX_EVENTS | UCCE_HDLC_TX_EVENTS) << 16);
	}

	return howmany;
}

static irqreturn_t ucc_hdlc_irq_handler(int irq, void *dev_id)
{
	struct ucc_hdlc_private *priv = (struct ucc_hdlc_private *)dev_id;
	struct net_device *dev = priv->ndev;
	struct ucc_fast_private *uccf;
	u32 ucce;
	u32 uccm;

	uccf = priv->uccf;

	ucce = ioread32be(uccf->p_ucce);
	uccm = ioread32be(uccf->p_uccm);
	ucce &= uccm;
	iowrite32be(ucce, uccf->p_ucce);
	if (!ucce)
		return IRQ_NONE;

	if ((ucce >> 16) & (UCCE_HDLC_RX_EVENTS | UCCE_HDLC_TX_EVENTS)) {
		if (napi_schedule_prep(&priv->napi)) {
			uccm &= ~((UCCE_HDLC_RX_EVENTS | UCCE_HDLC_TX_EVENTS)
				  << 16);
			iowrite32be(uccm, uccf->p_uccm);
			__napi_schedule(&priv->napi);
		}
	}

	/* Errors and other events */
	if (ucce >> 16 & UCC_HDLC_UCCE_BSY)
		dev->stats.rx_missed_errors++;
	if (ucce >> 16 & UCC_HDLC_UCCE_TXE)
		dev->stats.tx_errors++;

	return IRQ_HANDLED;
}

static int uhdlc_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	const size_t size = sizeof(te1_settings);
	te1_settings line;
	struct ucc_hdlc_private *priv = netdev_priv(dev);

	if (cmd != SIOCWANDEV)
		return hdlc_ioctl(dev, ifr, cmd);

	switch (ifr->ifr_settings.type) {
	case IF_GET_IFACE:
		ifr->ifr_settings.type = IF_IFACE_E1;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; /* data size wanted */
			return -ENOBUFS;
		}
		memset(&line, 0, sizeof(line));
		line.clock_type = priv->clocking;

		if (copy_to_user(ifr->ifr_settings.ifs_ifsu.sync, &line, size))
			return -EFAULT;
		return 0;

	default:
		return hdlc_ioctl(dev, ifr, cmd);
	}
}

static int uhdlc_open(struct net_device *dev)
{
	u32 cecr_subblock;
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct ucc_hdlc_private *priv = hdlc->priv;
	struct ucc_tdm *utdm = priv->utdm;

	if (priv->hdlc_busy != 1) {
		if (request_irq(priv->ut_info->uf_info.irq,
				ucc_hdlc_irq_handler, 0, "hdlc", priv))
			return -ENODEV;

		cecr_subblock = ucc_fast_get_qe_cr_subblock(
					priv->ut_info->uf_info.ucc_num);

		qe_issue_cmd(QE_INIT_TX_RX, cecr_subblock,
			     QE_CR_PROTOCOL_UNSPECIFIED, 0);

		ucc_fast_enable(priv->uccf, COMM_DIR_RX | COMM_DIR_TX);

		/* Enable the TDM port */
		if (priv->tsa)
			utdm->si_regs->siglmr1_h |= (0x1 << utdm->tdm_port);

		priv->hdlc_busy = 1;
		netif_device_attach(priv->ndev);
		napi_enable(&priv->napi);
		netdev_reset_queue(dev);
		netif_start_queue(dev);
		hdlc_open(dev);
	}

	return 0;
}

static void uhdlc_memclean(struct ucc_hdlc_private *priv)
{
	qe_muram_free(ioread16be(&priv->ucc_pram->riptr));
	qe_muram_free(ioread16be(&priv->ucc_pram->tiptr));

	if (priv->rx_bd_base) {
		dma_free_coherent(priv->dev,
				  RX_BD_RING_LEN * sizeof(struct qe_bd),
				  priv->rx_bd_base, priv->dma_rx_bd);

		priv->rx_bd_base = NULL;
		priv->dma_rx_bd = 0;
	}

	if (priv->tx_bd_base) {
		dma_free_coherent(priv->dev,
				  TX_BD_RING_LEN * sizeof(struct qe_bd),
				  priv->tx_bd_base, priv->dma_tx_bd);

		priv->tx_bd_base = NULL;
		priv->dma_tx_bd = 0;
	}

	if (priv->ucc_pram) {
		qe_muram_free(priv->ucc_pram_offset);
		priv->ucc_pram = NULL;
		priv->ucc_pram_offset = 0;
	 }

	kfree(priv->rx_skbuff);
	priv->rx_skbuff = NULL;

	kfree(priv->tx_skbuff);
	priv->tx_skbuff = NULL;

	if (priv->uf_regs) {
		iounmap(priv->uf_regs);
		priv->uf_regs = NULL;
	}

	if (priv->uccf) {
		ucc_fast_free(priv->uccf);
		priv->uccf = NULL;
	}

	if (priv->rx_buffer) {
		dma_free_coherent(priv->dev,
				  RX_BD_RING_LEN * MAX_RX_BUF_LENGTH,
				  priv->rx_buffer, priv->dma_rx_addr);
		priv->rx_buffer = NULL;
		priv->dma_rx_addr = 0;
	}

	if (priv->tx_buffer) {
		dma_free_coherent(priv->dev,
				  TX_BD_RING_LEN * MAX_RX_BUF_LENGTH,
				  priv->tx_buffer, priv->dma_tx_addr);
		priv->tx_buffer = NULL;
		priv->dma_tx_addr = 0;
	}
}

static int uhdlc_close(struct net_device *dev)
{
	struct ucc_hdlc_private *priv = dev_to_hdlc(dev)->priv;
	struct ucc_tdm *utdm = priv->utdm;
	u32 cecr_subblock;

	napi_disable(&priv->napi);
	cecr_subblock = ucc_fast_get_qe_cr_subblock(
				priv->ut_info->uf_info.ucc_num);

	qe_issue_cmd(QE_GRACEFUL_STOP_TX, cecr_subblock,
		     (u8)QE_CR_PROTOCOL_UNSPECIFIED, 0);
	qe_issue_cmd(QE_CLOSE_RX_BD, cecr_subblock,
		     (u8)QE_CR_PROTOCOL_UNSPECIFIED, 0);

	if (priv->tsa)
		utdm->si_regs->siglmr1_h &= ~(0x1 << utdm->tdm_port);

	ucc_fast_disable(priv->uccf, COMM_DIR_RX | COMM_DIR_TX);

	free_irq(priv->ut_info->uf_info.irq, priv);
	netif_stop_queue(dev);
	netdev_reset_queue(dev);
	priv->hdlc_busy = 0;

	return 0;
}

static int ucc_hdlc_attach(struct net_device *dev, unsigned short encoding,
			   unsigned short parity)
{
	struct ucc_hdlc_private *priv = dev_to_hdlc(dev)->priv;

	if (encoding != ENCODING_NRZ &&
	    encoding != ENCODING_NRZI)
		return -EINVAL;

	if (parity != PARITY_NONE &&
	    parity != PARITY_CRC32_PR1_CCITT &&
	    parity != PARITY_CRC16_PR0_CCITT &&
	    parity != PARITY_CRC16_PR1_CCITT)
		return -EINVAL;

	priv->encoding = encoding;
	priv->parity = parity;

	return 0;
}

#ifdef CONFIG_PM
static void store_clk_config(struct ucc_hdlc_private *priv)
{
	struct qe_mux *qe_mux_reg = &qe_immr->qmx;

	/* store si clk */
	priv->cmxsi1cr_h = ioread32be(&qe_mux_reg->cmxsi1cr_h);
	priv->cmxsi1cr_l = ioread32be(&qe_mux_reg->cmxsi1cr_l);

	/* store si sync */
	priv->cmxsi1syr = ioread32be(&qe_mux_reg->cmxsi1syr);

	/* store ucc clk */
	memcpy_fromio(priv->cmxucr, qe_mux_reg->cmxucr, 4 * sizeof(u32));
}

static void resume_clk_config(struct ucc_hdlc_private *priv)
{
	struct qe_mux *qe_mux_reg = &qe_immr->qmx;

	memcpy_toio(qe_mux_reg->cmxucr, priv->cmxucr, 4 * sizeof(u32));

	iowrite32be(priv->cmxsi1cr_h, &qe_mux_reg->cmxsi1cr_h);
	iowrite32be(priv->cmxsi1cr_l, &qe_mux_reg->cmxsi1cr_l);

	iowrite32be(priv->cmxsi1syr, &qe_mux_reg->cmxsi1syr);
}

static int uhdlc_suspend(struct device *dev)
{
	struct ucc_hdlc_private *priv = dev_get_drvdata(dev);
	struct ucc_fast __iomem *uf_regs;

	if (!priv)
		return -EINVAL;

	if (!netif_running(priv->ndev))
		return 0;

	netif_device_detach(priv->ndev);
	napi_disable(&priv->napi);

	uf_regs = priv->uf_regs;

	/* backup gumr guemr*/
	priv->gumr = ioread32be(&uf_regs->gumr);
	priv->guemr = ioread8(&uf_regs->guemr);

	priv->ucc_pram_bak = kmalloc(sizeof(*priv->ucc_pram_bak),
					GFP_KERNEL);
	if (!priv->ucc_pram_bak)
		return -ENOMEM;

	/* backup HDLC parameter */
	memcpy_fromio(priv->ucc_pram_bak, priv->ucc_pram,
		      sizeof(struct ucc_hdlc_param));

	/* store the clk configuration */
	store_clk_config(priv);

	/* save power */
	ucc_fast_disable(priv->uccf, COMM_DIR_RX | COMM_DIR_TX);

	return 0;
}

static int uhdlc_resume(struct device *dev)
{
	struct ucc_hdlc_private *priv = dev_get_drvdata(dev);
	struct ucc_tdm *utdm;
	struct ucc_tdm_info *ut_info;
	struct ucc_fast __iomem *uf_regs;
	struct ucc_fast_private *uccf;
	struct ucc_fast_info *uf_info;
	int i;
	u32 cecr_subblock;
	u16 bd_status;

	if (!priv)
		return -EINVAL;

	if (!netif_running(priv->ndev))
		return 0;

	utdm = priv->utdm;
	ut_info = priv->ut_info;
	uf_info = &ut_info->uf_info;
	uf_regs = priv->uf_regs;
	uccf = priv->uccf;

	/* restore gumr guemr */
	iowrite8(priv->guemr, &uf_regs->guemr);
	iowrite32be(priv->gumr, &uf_regs->gumr);

	/* Set Virtual Fifo registers */
	iowrite16be(uf_info->urfs, &uf_regs->urfs);
	iowrite16be(uf_info->urfet, &uf_regs->urfet);
	iowrite16be(uf_info->urfset, &uf_regs->urfset);
	iowrite16be(uf_info->utfs, &uf_regs->utfs);
	iowrite16be(uf_info->utfet, &uf_regs->utfet);
	iowrite16be(uf_info->utftt, &uf_regs->utftt);
	/* utfb, urfb are offsets from MURAM base */
	iowrite32be(uccf->ucc_fast_tx_virtual_fifo_base_offset, &uf_regs->utfb);
	iowrite32be(uccf->ucc_fast_rx_virtual_fifo_base_offset, &uf_regs->urfb);

	/* Rx Tx and sync clock routing */
	resume_clk_config(priv);

	iowrite32be(uf_info->uccm_mask, &uf_regs->uccm);
	iowrite32be(0xffffffff, &uf_regs->ucce);

	ucc_fast_disable(priv->uccf, COMM_DIR_RX | COMM_DIR_TX);

	/* rebuild SIRAM */
	if (priv->tsa)
		ucc_tdm_init(priv->utdm, priv->ut_info);

	/* Write to QE CECR, UCCx channel to Stop Transmission */
	cecr_subblock = ucc_fast_get_qe_cr_subblock(uf_info->ucc_num);
	qe_issue_cmd(QE_STOP_TX, cecr_subblock,
		     (u8)QE_CR_PROTOCOL_UNSPECIFIED, 0);

	/* Set UPSMR normal mode */
	iowrite32be(0, &uf_regs->upsmr);

	/* init parameter base */
	cecr_subblock = ucc_fast_get_qe_cr_subblock(uf_info->ucc_num);
	qe_issue_cmd(QE_ASSIGN_PAGE_TO_DEVICE, cecr_subblock,
		     QE_CR_PROTOCOL_UNSPECIFIED, priv->ucc_pram_offset);

	priv->ucc_pram = (struct ucc_hdlc_param __iomem *)
				qe_muram_addr(priv->ucc_pram_offset);

	/* restore ucc parameter */
	memcpy_toio(priv->ucc_pram, priv->ucc_pram_bak,
		    sizeof(struct ucc_hdlc_param));
	kfree(priv->ucc_pram_bak);

	/* rebuild BD entry */
	for (i = 0; i < RX_BD_RING_LEN; i++) {
		if (i < (RX_BD_RING_LEN - 1))
			bd_status = R_E_S | R_I_S;
		else
			bd_status = R_E_S | R_I_S | R_W_S;

		iowrite16be(bd_status, &priv->rx_bd_base[i].status);
		iowrite32be(priv->dma_rx_addr + i * MAX_RX_BUF_LENGTH,
			    &priv->rx_bd_base[i].buf);
	}

	for (i = 0; i < TX_BD_RING_LEN; i++) {
		if (i < (TX_BD_RING_LEN - 1))
			bd_status =  T_I_S | T_TC_S;
		else
			bd_status =  T_I_S | T_TC_S | T_W_S;

		iowrite16be(bd_status, &priv->tx_bd_base[i].status);
		iowrite32be(priv->dma_tx_addr + i * MAX_RX_BUF_LENGTH,
			    &priv->tx_bd_base[i].buf);
	}

	/* if hdlc is busy enable TX and RX */
	if (priv->hdlc_busy == 1) {
		cecr_subblock = ucc_fast_get_qe_cr_subblock(
					priv->ut_info->uf_info.ucc_num);

		qe_issue_cmd(QE_INIT_TX_RX, cecr_subblock,
			     (u8)QE_CR_PROTOCOL_UNSPECIFIED, 0);

		ucc_fast_enable(priv->uccf, COMM_DIR_RX | COMM_DIR_TX);

		/* Enable the TDM port */
		if (priv->tsa)
			utdm->si_regs->siglmr1_h |= (0x1 << utdm->tdm_port);
	}

	napi_enable(&priv->napi);
	netif_device_attach(priv->ndev);

	return 0;
}

static const struct dev_pm_ops uhdlc_pm_ops = {
	.suspend = uhdlc_suspend,
	.resume = uhdlc_resume,
	.freeze = uhdlc_suspend,
	.thaw = uhdlc_resume,
};

#define HDLC_PM_OPS (&uhdlc_pm_ops)

#else

#define HDLC_PM_OPS NULL

#endif
static void uhdlc_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	netdev_err(ndev, "%s\n", __func__);
}

static const struct net_device_ops uhdlc_ops = {
	.ndo_open       = uhdlc_open,
	.ndo_stop       = uhdlc_close,
	.ndo_start_xmit = hdlc_start_xmit,
	.ndo_do_ioctl   = uhdlc_ioctl,
	.ndo_tx_timeout	= uhdlc_tx_timeout,
};

static int hdlc_map_iomem(char *name, int init_flag, void __iomem **ptr)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct resource *res;
	static int siram_init_flag;
	int ret = 0;

	np = of_find_compatible_node(NULL, NULL, name);
	if (!np)
		return -EINVAL;

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		pr_err("%pOFn: failed to lookup pdev\n", np);
		of_node_put(np);
		return -EINVAL;
	}

	of_node_put(np);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -EINVAL;
		goto error_put_device;
	}
	*ptr = ioremap(res->start, resource_size(res));
	if (!*ptr) {
		ret = -ENOMEM;
		goto error_put_device;
	}

	/* We've remapped the addresses, and we don't need the device any
	 * more, so we should release it.
	 */
	put_device(&pdev->dev);

	if (init_flag && siram_init_flag == 0) {
		memset_io(*ptr, 0, resource_size(res));
		siram_init_flag = 1;
	}
	return  0;

error_put_device:
	put_device(&pdev->dev);

	return ret;
}

static int ucc_hdlc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ucc_hdlc_private *uhdlc_priv = NULL;
	struct ucc_tdm_info *ut_info;
	struct ucc_tdm *utdm = NULL;
	struct resource res;
	struct net_device *dev;
	hdlc_device *hdlc;
	int ucc_num;
	const char *sprop;
	int ret;
	u32 val;

	ret = of_property_read_u32_index(np, "cell-index", 0, &val);
	if (ret) {
		dev_err(&pdev->dev, "Invalid ucc property\n");
		return -ENODEV;
	}

	ucc_num = val - 1;
	if (ucc_num > (UCC_MAX_NUM - 1) || ucc_num < 0) {
		dev_err(&pdev->dev, ": Invalid UCC num\n");
		return -EINVAL;
	}

	memcpy(&utdm_info[ucc_num], &utdm_primary_info,
	       sizeof(utdm_primary_info));

	ut_info = &utdm_info[ucc_num];
	ut_info->uf_info.ucc_num = ucc_num;

	sprop = of_get_property(np, "rx-clock-name", NULL);
	if (sprop) {
		ut_info->uf_info.rx_clock = qe_clock_source(sprop);
		if ((ut_info->uf_info.rx_clock < QE_CLK_NONE) ||
		    (ut_info->uf_info.rx_clock > QE_CLK24)) {
			dev_err(&pdev->dev, "Invalid rx-clock-name property\n");
			return -EINVAL;
		}
	} else {
		dev_err(&pdev->dev, "Invalid rx-clock-name property\n");
		return -EINVAL;
	}

	sprop = of_get_property(np, "tx-clock-name", NULL);
	if (sprop) {
		ut_info->uf_info.tx_clock = qe_clock_source(sprop);
		if ((ut_info->uf_info.tx_clock < QE_CLK_NONE) ||
		    (ut_info->uf_info.tx_clock > QE_CLK24)) {
			dev_err(&pdev->dev, "Invalid tx-clock-name property\n");
			return -EINVAL;
		}
	} else {
		dev_err(&pdev->dev, "Invalid tx-clock-name property\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		return -EINVAL;

	ut_info->uf_info.regs = res.start;
	ut_info->uf_info.irq = irq_of_parse_and_map(np, 0);

	uhdlc_priv = kzalloc(sizeof(*uhdlc_priv), GFP_KERNEL);
	if (!uhdlc_priv) {
		return -ENOMEM;
	}

	dev_set_drvdata(&pdev->dev, uhdlc_priv);
	uhdlc_priv->dev = &pdev->dev;
	uhdlc_priv->ut_info = ut_info;

	if (of_get_property(np, "fsl,tdm-interface", NULL))
		uhdlc_priv->tsa = 1;

	if (of_get_property(np, "fsl,ucc-internal-loopback", NULL))
		uhdlc_priv->loopback = 1;

	if (of_get_property(np, "fsl,hdlc-bus", NULL))
		uhdlc_priv->hdlc_bus = 1;

	if (uhdlc_priv->tsa == 1) {
		utdm = kzalloc(sizeof(*utdm), GFP_KERNEL);
		if (!utdm) {
			ret = -ENOMEM;
			dev_err(&pdev->dev, "No mem to alloc ucc tdm data\n");
			goto free_uhdlc_priv;
		}
		uhdlc_priv->utdm = utdm;
		ret = ucc_of_parse_tdm(np, utdm, ut_info);
		if (ret)
			goto free_utdm;

		ret = hdlc_map_iomem("fsl,t1040-qe-si", 0,
				     (void __iomem **)&utdm->si_regs);
		if (ret)
			goto free_utdm;
		ret = hdlc_map_iomem("fsl,t1040-qe-siram", 1,
				     (void __iomem **)&utdm->siram);
		if (ret)
			goto unmap_si_regs;
	}

	if (of_property_read_u16(np, "fsl,hmask", &uhdlc_priv->hmask))
		uhdlc_priv->hmask = DEFAULT_ADDR_MASK;

	ret = uhdlc_init(uhdlc_priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init uhdlc\n");
		goto undo_uhdlc_init;
	}

	dev = alloc_hdlcdev(uhdlc_priv);
	if (!dev) {
		ret = -ENOMEM;
		pr_err("ucc_hdlc: unable to allocate memory\n");
		goto undo_uhdlc_init;
	}

	uhdlc_priv->ndev = dev;
	hdlc = dev_to_hdlc(dev);
	dev->tx_queue_len = 16;
	dev->netdev_ops = &uhdlc_ops;
	dev->watchdog_timeo = 2 * HZ;
	hdlc->attach = ucc_hdlc_attach;
	hdlc->xmit = ucc_hdlc_tx;
	netif_napi_add(dev, &uhdlc_priv->napi, ucc_hdlc_poll, 32);
	if (register_hdlc_device(dev)) {
		ret = -ENOBUFS;
		pr_err("ucc_hdlc: unable to register hdlc device\n");
		goto free_dev;
	}

	return 0;

free_dev:
	free_netdev(dev);
undo_uhdlc_init:
	iounmap(utdm->siram);
unmap_si_regs:
	iounmap(utdm->si_regs);
free_utdm:
	if (uhdlc_priv->tsa)
		kfree(utdm);
free_uhdlc_priv:
	kfree(uhdlc_priv);
	return ret;
}

static int ucc_hdlc_remove(struct platform_device *pdev)
{
	struct ucc_hdlc_private *priv = dev_get_drvdata(&pdev->dev);

	uhdlc_memclean(priv);

	if (priv->utdm->si_regs) {
		iounmap(priv->utdm->si_regs);
		priv->utdm->si_regs = NULL;
	}

	if (priv->utdm->siram) {
		iounmap(priv->utdm->siram);
		priv->utdm->siram = NULL;
	}
	kfree(priv);

	dev_info(&pdev->dev, "UCC based hdlc module removed\n");

	return 0;
}

static const struct of_device_id fsl_ucc_hdlc_of_match[] = {
	{
	.compatible = "fsl,ucc-hdlc",
	},
	{},
};

MODULE_DEVICE_TABLE(of, fsl_ucc_hdlc_of_match);

static struct platform_driver ucc_hdlc_driver = {
	.probe	= ucc_hdlc_probe,
	.remove	= ucc_hdlc_remove,
	.driver	= {
		.name		= DRV_NAME,
		.pm		= HDLC_PM_OPS,
		.of_match_table	= fsl_ucc_hdlc_of_match,
	},
};

module_platform_driver(ucc_hdlc_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRV_DESC);
