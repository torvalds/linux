/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2012,2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2016-2017 Erik Stromdahl <erik.stromdahl@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/module.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sd.h>
#include <linux/bitfield.h>
#include "core.h"
#include "bmi.h"
#include "debug.h"
#include "hif.h"
#include "htc.h"
#include "mac.h"
#include "targaddrs.h"
#include "trace.h"
#include "sdio.h"

/* inlined helper functions */

static inline int ath10k_sdio_calc_txrx_padded_len(struct ath10k_sdio *ar_sdio,
						   size_t len)
{
	return __ALIGN_MASK((len), ar_sdio->mbox_info.block_mask);
}

static inline enum ath10k_htc_ep_id pipe_id_to_eid(u8 pipe_id)
{
	return (enum ath10k_htc_ep_id)pipe_id;
}

static inline void ath10k_sdio_mbox_free_rx_pkt(struct ath10k_sdio_rx_data *pkt)
{
	dev_kfree_skb(pkt->skb);
	pkt->skb = NULL;
	pkt->alloc_len = 0;
	pkt->act_len = 0;
	pkt->trailer_only = false;
}

static inline int ath10k_sdio_mbox_alloc_rx_pkt(struct ath10k_sdio_rx_data *pkt,
						size_t act_len, size_t full_len,
						bool part_of_bundle,
						bool last_in_bundle)
{
	pkt->skb = dev_alloc_skb(full_len);
	if (!pkt->skb)
		return -ENOMEM;

	pkt->act_len = act_len;
	pkt->alloc_len = full_len;
	pkt->part_of_bundle = part_of_bundle;
	pkt->last_in_bundle = last_in_bundle;
	pkt->trailer_only = false;

	return 0;
}

static inline bool is_trailer_only_msg(struct ath10k_sdio_rx_data *pkt)
{
	bool trailer_only = false;
	struct ath10k_htc_hdr *htc_hdr =
		(struct ath10k_htc_hdr *)pkt->skb->data;
	u16 len = __le16_to_cpu(htc_hdr->len);

	if (len == htc_hdr->trailer_len)
		trailer_only = true;

	return trailer_only;
}

/* sdio/mmc functions */

static inline void ath10k_sdio_set_cmd52_arg(u32 *arg, u8 write, u8 raw,
					     unsigned int address,
					     unsigned char val)
{
	*arg = FIELD_PREP(BIT(31), write) |
	       FIELD_PREP(BIT(27), raw) |
	       FIELD_PREP(BIT(26), 1) |
	       FIELD_PREP(GENMASK(25, 9), address) |
	       FIELD_PREP(BIT(8), 1) |
	       FIELD_PREP(GENMASK(7, 0), val);
}

static int ath10k_sdio_func0_cmd52_wr_byte(struct mmc_card *card,
					   unsigned int address,
					   unsigned char byte)
{
	struct mmc_command io_cmd;

	memset(&io_cmd, 0, sizeof(io_cmd));
	ath10k_sdio_set_cmd52_arg(&io_cmd.arg, 1, 0, address, byte);
	io_cmd.opcode = SD_IO_RW_DIRECT;
	io_cmd.flags = MMC_RSP_R5 | MMC_CMD_AC;

	return mmc_wait_for_cmd(card->host, &io_cmd, 0);
}

static int ath10k_sdio_func0_cmd52_rd_byte(struct mmc_card *card,
					   unsigned int address,
					   unsigned char *byte)
{
	struct mmc_command io_cmd;
	int ret;

	memset(&io_cmd, 0, sizeof(io_cmd));
	ath10k_sdio_set_cmd52_arg(&io_cmd.arg, 0, 0, address, 0);
	io_cmd.opcode = SD_IO_RW_DIRECT;
	io_cmd.flags = MMC_RSP_R5 | MMC_CMD_AC;

	ret = mmc_wait_for_cmd(card->host, &io_cmd, 0);
	if (!ret)
		*byte = io_cmd.resp[0];

	return ret;
}

static int ath10k_sdio_config(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct sdio_func *func = ar_sdio->func;
	unsigned char byte, asyncintdelay = 2;
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "sdio configuration\n");

	sdio_claim_host(func);

	byte = 0;
	ret = ath10k_sdio_func0_cmd52_rd_byte(func->card,
					      SDIO_CCCR_DRIVE_STRENGTH,
					      &byte);

	byte &= ~ATH10K_SDIO_DRIVE_DTSX_MASK;
	byte |= FIELD_PREP(ATH10K_SDIO_DRIVE_DTSX_MASK,
			   ATH10K_SDIO_DRIVE_DTSX_TYPE_D);

	ret = ath10k_sdio_func0_cmd52_wr_byte(func->card,
					      SDIO_CCCR_DRIVE_STRENGTH,
					      byte);

	byte = 0;
	ret = ath10k_sdio_func0_cmd52_rd_byte(
		func->card,
		CCCR_SDIO_DRIVER_STRENGTH_ENABLE_ADDR,
		&byte);

	byte |= (CCCR_SDIO_DRIVER_STRENGTH_ENABLE_A |
		 CCCR_SDIO_DRIVER_STRENGTH_ENABLE_C |
		 CCCR_SDIO_DRIVER_STRENGTH_ENABLE_D);

	ret = ath10k_sdio_func0_cmd52_wr_byte(func->card,
					      CCCR_SDIO_DRIVER_STRENGTH_ENABLE_ADDR,
					      byte);
	if (ret) {
		ath10k_warn(ar, "failed to enable driver strength: %d\n", ret);
		goto out;
	}

	byte = 0;
	ret = ath10k_sdio_func0_cmd52_rd_byte(func->card,
					      CCCR_SDIO_IRQ_MODE_REG_SDIO3,
					      &byte);

	byte |= SDIO_IRQ_MODE_ASYNC_4BIT_IRQ_SDIO3;

	ret = ath10k_sdio_func0_cmd52_wr_byte(func->card,
					      CCCR_SDIO_IRQ_MODE_REG_SDIO3,
					      byte);
	if (ret) {
		ath10k_warn(ar, "failed to enable 4-bit async irq mode: %d\n",
			    ret);
		goto out;
	}

	byte = 0;
	ret = ath10k_sdio_func0_cmd52_rd_byte(func->card,
					      CCCR_SDIO_ASYNC_INT_DELAY_ADDRESS,
					      &byte);

	byte &= ~CCCR_SDIO_ASYNC_INT_DELAY_MASK;
	byte |= FIELD_PREP(CCCR_SDIO_ASYNC_INT_DELAY_MASK, asyncintdelay);

	ret = ath10k_sdio_func0_cmd52_wr_byte(func->card,
					      CCCR_SDIO_ASYNC_INT_DELAY_ADDRESS,
					      byte);

	/* give us some time to enable, in ms */
	func->enable_timeout = 100;

	ret = sdio_set_block_size(func, ar_sdio->mbox_info.block_size);
	if (ret) {
		ath10k_warn(ar, "failed to set sdio block size to %d: %d\n",
			    ar_sdio->mbox_info.block_size, ret);
		goto out;
	}

out:
	sdio_release_host(func);
	return ret;
}

static int ath10k_sdio_write32(struct ath10k *ar, u32 addr, u32 val)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct sdio_func *func = ar_sdio->func;
	int ret;

	sdio_claim_host(func);

	sdio_writel(func, val, addr, &ret);
	if (ret) {
		ath10k_warn(ar, "failed to write 0x%x to address 0x%x: %d\n",
			    val, addr, ret);
		goto out;
	}

	ath10k_dbg(ar, ATH10K_DBG_SDIO, "sdio write32 addr 0x%x val 0x%x\n",
		   addr, val);

out:
	sdio_release_host(func);

	return ret;
}

static int ath10k_sdio_writesb32(struct ath10k *ar, u32 addr, u32 val)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct sdio_func *func = ar_sdio->func;
	__le32 *buf;
	int ret;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	*buf = cpu_to_le32(val);

	sdio_claim_host(func);

	ret = sdio_writesb(func, addr, buf, sizeof(*buf));
	if (ret) {
		ath10k_warn(ar, "failed to write value 0x%x to fixed sb address 0x%x: %d\n",
			    val, addr, ret);
		goto out;
	}

	ath10k_dbg(ar, ATH10K_DBG_SDIO, "sdio writesb32 addr 0x%x val 0x%x\n",
		   addr, val);

out:
	sdio_release_host(func);

	kfree(buf);

	return ret;
}

static int ath10k_sdio_read32(struct ath10k *ar, u32 addr, u32 *val)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct sdio_func *func = ar_sdio->func;
	int ret;

	sdio_claim_host(func);
	*val = sdio_readl(func, addr, &ret);
	if (ret) {
		ath10k_warn(ar, "failed to read from address 0x%x: %d\n",
			    addr, ret);
		goto out;
	}

	ath10k_dbg(ar, ATH10K_DBG_SDIO, "sdio read32 addr 0x%x val 0x%x\n",
		   addr, *val);

out:
	sdio_release_host(func);

	return ret;
}

static int ath10k_sdio_read(struct ath10k *ar, u32 addr, void *buf, size_t len)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct sdio_func *func = ar_sdio->func;
	int ret;

	sdio_claim_host(func);

	ret = sdio_memcpy_fromio(func, buf, addr, len);
	if (ret) {
		ath10k_warn(ar, "failed to read from address 0x%x: %d\n",
			    addr, ret);
		goto out;
	}

	ath10k_dbg(ar, ATH10K_DBG_SDIO, "sdio read addr 0x%x buf 0x%p len %zu\n",
		   addr, buf, len);
	ath10k_dbg_dump(ar, ATH10K_DBG_SDIO_DUMP, NULL, "sdio read ", buf, len);

out:
	sdio_release_host(func);

	return ret;
}

static int ath10k_sdio_write(struct ath10k *ar, u32 addr, const void *buf, size_t len)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct sdio_func *func = ar_sdio->func;
	int ret;

	sdio_claim_host(func);

	/* For some reason toio() doesn't have const for the buffer, need
	 * an ugly hack to workaround that.
	 */
	ret = sdio_memcpy_toio(func, addr, (void *)buf, len);
	if (ret) {
		ath10k_warn(ar, "failed to write to address 0x%x: %d\n",
			    addr, ret);
		goto out;
	}

	ath10k_dbg(ar, ATH10K_DBG_SDIO, "sdio write addr 0x%x buf 0x%p len %zu\n",
		   addr, buf, len);
	ath10k_dbg_dump(ar, ATH10K_DBG_SDIO_DUMP, NULL, "sdio write ", buf, len);

out:
	sdio_release_host(func);

	return ret;
}

static int ath10k_sdio_readsb(struct ath10k *ar, u32 addr, void *buf, size_t len)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct sdio_func *func = ar_sdio->func;
	int ret;

	sdio_claim_host(func);

	len = round_down(len, ar_sdio->mbox_info.block_size);

	ret = sdio_readsb(func, buf, addr, len);
	if (ret) {
		ath10k_warn(ar, "failed to read from fixed (sb) address 0x%x: %d\n",
			    addr, ret);
		goto out;
	}

	ath10k_dbg(ar, ATH10K_DBG_SDIO, "sdio readsb addr 0x%x buf 0x%p len %zu\n",
		   addr, buf, len);
	ath10k_dbg_dump(ar, ATH10K_DBG_SDIO_DUMP, NULL, "sdio readsb ", buf, len);

out:
	sdio_release_host(func);

	return ret;
}

/* HIF mbox functions */

static int ath10k_sdio_mbox_rx_process_packet(struct ath10k *ar,
					      struct ath10k_sdio_rx_data *pkt,
					      u32 *lookaheads,
					      int *n_lookaheads)
{
	struct ath10k_htc *htc = &ar->htc;
	struct sk_buff *skb = pkt->skb;
	struct ath10k_htc_hdr *htc_hdr = (struct ath10k_htc_hdr *)skb->data;
	bool trailer_present = htc_hdr->flags & ATH10K_HTC_FLAG_TRAILER_PRESENT;
	enum ath10k_htc_ep_id eid;
	u8 *trailer;
	int ret;

	if (trailer_present) {
		trailer = skb->data + skb->len - htc_hdr->trailer_len;

		eid = pipe_id_to_eid(htc_hdr->eid);

		ret = ath10k_htc_process_trailer(htc,
						 trailer,
						 htc_hdr->trailer_len,
						 eid,
						 lookaheads,
						 n_lookaheads);
		if (ret)
			return ret;

		if (is_trailer_only_msg(pkt))
			pkt->trailer_only = true;

		skb_trim(skb, skb->len - htc_hdr->trailer_len);
	}

	skb_pull(skb, sizeof(*htc_hdr));

	return 0;
}

static int ath10k_sdio_mbox_rx_process_packets(struct ath10k *ar,
					       u32 lookaheads[],
					       int *n_lookahead)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_htc *htc = &ar->htc;
	struct ath10k_sdio_rx_data *pkt;
	struct ath10k_htc_ep *ep;
	enum ath10k_htc_ep_id id;
	int ret, i, *n_lookahead_local;
	u32 *lookaheads_local;
	int lookahead_idx = 0;

	for (i = 0; i < ar_sdio->n_rx_pkts; i++) {
		lookaheads_local = lookaheads;
		n_lookahead_local = n_lookahead;

		id = ((struct ath10k_htc_hdr *)
		      &lookaheads[lookahead_idx++])->eid;

		if (id >= ATH10K_HTC_EP_COUNT) {
			ath10k_warn(ar, "invalid endpoint in look-ahead: %d\n",
				    id);
			ret = -ENOMEM;
			goto out;
		}

		ep = &htc->endpoint[id];

		if (ep->service_id == 0) {
			ath10k_warn(ar, "ep %d is not connected\n", id);
			ret = -ENOMEM;
			goto out;
		}

		pkt = &ar_sdio->rx_pkts[i];

		if (pkt->part_of_bundle && !pkt->last_in_bundle) {
			/* Only read lookahead's from RX trailers
			 * for the last packet in a bundle.
			 */
			lookahead_idx--;
			lookaheads_local = NULL;
			n_lookahead_local = NULL;
		}

		ret = ath10k_sdio_mbox_rx_process_packet(ar,
							 pkt,
							 lookaheads_local,
							 n_lookahead_local);
		if (ret)
			goto out;

		if (!pkt->trailer_only)
			ep->ep_ops.ep_rx_complete(ar_sdio->ar, pkt->skb);
		else
			kfree_skb(pkt->skb);

		/* The RX complete handler now owns the skb...*/
		pkt->skb = NULL;
		pkt->alloc_len = 0;
	}

	ret = 0;

out:
	/* Free all packets that was not passed on to the RX completion
	 * handler...
	 */
	for (; i < ar_sdio->n_rx_pkts; i++)
		ath10k_sdio_mbox_free_rx_pkt(&ar_sdio->rx_pkts[i]);

	return ret;
}

static int ath10k_sdio_mbox_alloc_pkt_bundle(struct ath10k *ar,
					     struct ath10k_sdio_rx_data *rx_pkts,
					     struct ath10k_htc_hdr *htc_hdr,
					     size_t full_len, size_t act_len,
					     size_t *bndl_cnt)
{
	int ret, i;

	*bndl_cnt = FIELD_GET(ATH10K_HTC_FLAG_BUNDLE_MASK, htc_hdr->flags);

	if (*bndl_cnt > HTC_HOST_MAX_MSG_PER_RX_BUNDLE) {
		ath10k_warn(ar,
			    "HTC bundle length %u exceeds maximum %u\n",
			    le16_to_cpu(htc_hdr->len),
			    HTC_HOST_MAX_MSG_PER_RX_BUNDLE);
		return -ENOMEM;
	}

	/* Allocate bndl_cnt extra skb's for the bundle.
	 * The package containing the
	 * ATH10K_HTC_FLAG_BUNDLE_MASK flag is not included
	 * in bndl_cnt. The skb for that packet will be
	 * allocated separately.
	 */
	for (i = 0; i < *bndl_cnt; i++) {
		ret = ath10k_sdio_mbox_alloc_rx_pkt(&rx_pkts[i],
						    act_len,
						    full_len,
						    true,
						    false);
		if (ret)
			return ret;
	}

	return 0;
}

static int ath10k_sdio_mbox_rx_alloc(struct ath10k *ar,
				     u32 lookaheads[], int n_lookaheads)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_htc_hdr *htc_hdr;
	size_t full_len, act_len;
	bool last_in_bundle;
	int ret, i;

	if (n_lookaheads > ATH10K_SDIO_MAX_RX_MSGS) {
		ath10k_warn(ar,
			    "the total number of pkgs to be fetched (%u) exceeds maximum %u\n",
			    n_lookaheads,
			    ATH10K_SDIO_MAX_RX_MSGS);
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < n_lookaheads; i++) {
		htc_hdr = (struct ath10k_htc_hdr *)&lookaheads[i];
		last_in_bundle = false;

		if (le16_to_cpu(htc_hdr->len) >
		    ATH10K_HTC_MBOX_MAX_PAYLOAD_LENGTH) {
			ath10k_warn(ar,
				    "payload length %d exceeds max htc length: %zu\n",
				    le16_to_cpu(htc_hdr->len),
				    ATH10K_HTC_MBOX_MAX_PAYLOAD_LENGTH);
			ret = -ENOMEM;
			goto err;
		}

		act_len = le16_to_cpu(htc_hdr->len) + sizeof(*htc_hdr);
		full_len = ath10k_sdio_calc_txrx_padded_len(ar_sdio, act_len);

		if (full_len > ATH10K_SDIO_MAX_BUFFER_SIZE) {
			ath10k_warn(ar,
				    "rx buffer requested with invalid htc_hdr length (%d, 0x%x): %d\n",
				    htc_hdr->eid, htc_hdr->flags,
				    le16_to_cpu(htc_hdr->len));
			ret = -EINVAL;
			goto err;
		}

		if (htc_hdr->flags & ATH10K_HTC_FLAG_BUNDLE_MASK) {
			/* HTC header indicates that every packet to follow
			 * has the same padded length so that it can be
			 * optimally fetched as a full bundle.
			 */
			size_t bndl_cnt;

			ret = ath10k_sdio_mbox_alloc_pkt_bundle(ar,
								&ar_sdio->rx_pkts[i],
								htc_hdr,
								full_len,
								act_len,
								&bndl_cnt);

			n_lookaheads += bndl_cnt;
			i += bndl_cnt;
			/*Next buffer will be the last in the bundle */
			last_in_bundle = true;
		}

		/* Allocate skb for packet. If the packet had the
		 * ATH10K_HTC_FLAG_BUNDLE_MASK flag set, all bundled
		 * packet skb's have been allocated in the previous step.
		 */
		if (htc_hdr->flags & ATH10K_HTC_FLAGS_RECV_1MORE_BLOCK)
			full_len += ATH10K_HIF_MBOX_BLOCK_SIZE;

		ret = ath10k_sdio_mbox_alloc_rx_pkt(&ar_sdio->rx_pkts[i],
						    act_len,
						    full_len,
						    last_in_bundle,
						    last_in_bundle);
		if (ret) {
			ath10k_warn(ar, "alloc_rx_pkt error %d\n", ret);
			goto err;
		}
	}

	ar_sdio->n_rx_pkts = i;

	return 0;

err:
	for (i = 0; i < ATH10K_SDIO_MAX_RX_MSGS; i++) {
		if (!ar_sdio->rx_pkts[i].alloc_len)
			break;
		ath10k_sdio_mbox_free_rx_pkt(&ar_sdio->rx_pkts[i]);
	}

	return ret;
}

static int ath10k_sdio_mbox_rx_packet(struct ath10k *ar,
				      struct ath10k_sdio_rx_data *pkt)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct sk_buff *skb = pkt->skb;
	struct ath10k_htc_hdr *htc_hdr;
	int ret;

	ret = ath10k_sdio_readsb(ar, ar_sdio->mbox_info.htc_addr,
				 skb->data, pkt->alloc_len);
	if (ret)
		goto out;

	/* Update actual length. The original length may be incorrect,
	 * as the FW will bundle multiple packets as long as their sizes
	 * fit within the same aligned length (pkt->alloc_len).
	 */
	htc_hdr = (struct ath10k_htc_hdr *)skb->data;
	pkt->act_len = le16_to_cpu(htc_hdr->len) + sizeof(*htc_hdr);
	if (pkt->act_len > pkt->alloc_len) {
		ath10k_warn(ar, "rx packet too large (%zu > %zu)\n",
			    pkt->act_len, pkt->alloc_len);
		ret = -EMSGSIZE;
		goto out;
	}

	skb_put(skb, pkt->act_len);

out:
	pkt->status = ret;

	return ret;
}

static int ath10k_sdio_mbox_rx_fetch(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	int ret, i;

	for (i = 0; i < ar_sdio->n_rx_pkts; i++) {
		ret = ath10k_sdio_mbox_rx_packet(ar,
						 &ar_sdio->rx_pkts[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	/* Free all packets that was not successfully fetched. */
	for (; i < ar_sdio->n_rx_pkts; i++)
		ath10k_sdio_mbox_free_rx_pkt(&ar_sdio->rx_pkts[i]);

	return ret;
}

/* This is the timeout for mailbox processing done in the sdio irq
 * handler. The timeout is deliberately set quite high since SDIO dump logs
 * over serial port can/will add a substantial overhead to the processing
 * (if enabled).
 */
#define SDIO_MBOX_PROCESSING_TIMEOUT_HZ (20 * HZ)

static int ath10k_sdio_mbox_rxmsg_pending_handler(struct ath10k *ar,
						  u32 msg_lookahead, bool *done)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	u32 lookaheads[ATH10K_SDIO_MAX_RX_MSGS];
	int n_lookaheads = 1;
	unsigned long timeout;
	int ret;

	*done = true;

	/* Copy the lookahead obtained from the HTC register table into our
	 * temp array as a start value.
	 */
	lookaheads[0] = msg_lookahead;

	timeout = jiffies + SDIO_MBOX_PROCESSING_TIMEOUT_HZ;
	do {
		/* Try to allocate as many HTC RX packets indicated by
		 * n_lookaheads.
		 */
		ret = ath10k_sdio_mbox_rx_alloc(ar, lookaheads,
						n_lookaheads);
		if (ret)
			break;

		if (ar_sdio->n_rx_pkts >= 2)
			/* A recv bundle was detected, force IRQ status
			 * re-check again.
			 */
			*done = false;

		ret = ath10k_sdio_mbox_rx_fetch(ar);

		/* Process fetched packets. This will potentially update
		 * n_lookaheads depending on if the packets contain lookahead
		 * reports.
		 */
		n_lookaheads = 0;
		ret = ath10k_sdio_mbox_rx_process_packets(ar,
							  lookaheads,
							  &n_lookaheads);

		if (!n_lookaheads || ret)
			break;

		/* For SYNCH processing, if we get here, we are running
		 * through the loop again due to updated lookaheads. Set
		 * flag that we should re-check IRQ status registers again
		 * before leaving IRQ processing, this can net better
		 * performance in high throughput situations.
		 */
		*done = false;
	} while (time_before(jiffies, timeout));

	if (ret && (ret != -ECANCELED))
		ath10k_warn(ar, "failed to get pending recv messages: %d\n",
			    ret);

	return ret;
}

static int ath10k_sdio_mbox_proc_dbg_intr(struct ath10k *ar)
{
	u32 val;
	int ret;

	/* TODO: Add firmware crash handling */
	ath10k_warn(ar, "firmware crashed\n");

	/* read counter to clear the interrupt, the debug error interrupt is
	 * counter 0.
	 */
	ret = ath10k_sdio_read32(ar, MBOX_COUNT_DEC_ADDRESS, &val);
	if (ret)
		ath10k_warn(ar, "failed to clear debug interrupt: %d\n", ret);

	return ret;
}

static int ath10k_sdio_mbox_proc_counter_intr(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_sdio_irq_data *irq_data = &ar_sdio->irq_data;
	u8 counter_int_status;
	int ret;

	mutex_lock(&irq_data->mtx);
	counter_int_status = irq_data->irq_proc_reg->counter_int_status &
			     irq_data->irq_en_reg->cntr_int_status_en;

	/* NOTE: other modules like GMBOX may use the counter interrupt for
	 * credit flow control on other counters, we only need to check for
	 * the debug assertion counter interrupt.
	 */
	if (counter_int_status & ATH10K_SDIO_TARGET_DEBUG_INTR_MASK)
		ret = ath10k_sdio_mbox_proc_dbg_intr(ar);
	else
		ret = 0;

	mutex_unlock(&irq_data->mtx);

	return ret;
}

static int ath10k_sdio_mbox_proc_err_intr(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_sdio_irq_data *irq_data = &ar_sdio->irq_data;
	u8 error_int_status;
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_SDIO, "sdio error interrupt\n");

	error_int_status = irq_data->irq_proc_reg->error_int_status & 0x0F;
	if (!error_int_status) {
		ath10k_warn(ar, "invalid error interrupt status: 0x%x\n",
			    error_int_status);
		return -EIO;
	}

	ath10k_dbg(ar, ATH10K_DBG_SDIO,
		   "sdio error_int_status 0x%x\n", error_int_status);

	if (FIELD_GET(MBOX_ERROR_INT_STATUS_WAKEUP_MASK,
		      error_int_status))
		ath10k_dbg(ar, ATH10K_DBG_SDIO, "sdio interrupt error wakeup\n");

	if (FIELD_GET(MBOX_ERROR_INT_STATUS_RX_UNDERFLOW_MASK,
		      error_int_status))
		ath10k_warn(ar, "rx underflow interrupt error\n");

	if (FIELD_GET(MBOX_ERROR_INT_STATUS_TX_OVERFLOW_MASK,
		      error_int_status))
		ath10k_warn(ar, "tx overflow interrupt error\n");

	/* Clear the interrupt */
	irq_data->irq_proc_reg->error_int_status &= ~error_int_status;

	/* set W1C value to clear the interrupt, this hits the register first */
	ret = ath10k_sdio_writesb32(ar, MBOX_ERROR_INT_STATUS_ADDRESS,
				    error_int_status);
	if (ret) {
		ath10k_warn(ar, "unable to write to error int status address: %d\n",
			    ret);
		return ret;
	}

	return 0;
}

static int ath10k_sdio_mbox_proc_cpu_intr(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_sdio_irq_data *irq_data = &ar_sdio->irq_data;
	u8 cpu_int_status;
	int ret;

	mutex_lock(&irq_data->mtx);
	cpu_int_status = irq_data->irq_proc_reg->cpu_int_status &
			 irq_data->irq_en_reg->cpu_int_status_en;
	if (!cpu_int_status) {
		ath10k_warn(ar, "CPU interrupt status is zero\n");
		ret = -EIO;
		goto out;
	}

	/* Clear the interrupt */
	irq_data->irq_proc_reg->cpu_int_status &= ~cpu_int_status;

	/* Set up the register transfer buffer to hit the register 4 times,
	 * this is done to make the access 4-byte aligned to mitigate issues
	 * with host bus interconnects that restrict bus transfer lengths to
	 * be a multiple of 4-bytes.
	 *
	 * Set W1C value to clear the interrupt, this hits the register first.
	 */
	ret = ath10k_sdio_writesb32(ar, MBOX_CPU_INT_STATUS_ADDRESS,
				    cpu_int_status);
	if (ret) {
		ath10k_warn(ar, "unable to write to cpu interrupt status address: %d\n",
			    ret);
		goto out;
	}

out:
	mutex_unlock(&irq_data->mtx);
	return ret;
}

static int ath10k_sdio_mbox_read_int_status(struct ath10k *ar,
					    u8 *host_int_status,
					    u32 *lookahead)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_sdio_irq_data *irq_data = &ar_sdio->irq_data;
	struct ath10k_sdio_irq_proc_regs *irq_proc_reg = irq_data->irq_proc_reg;
	struct ath10k_sdio_irq_enable_regs *irq_en_reg = irq_data->irq_en_reg;
	u8 htc_mbox = FIELD_PREP(ATH10K_HTC_MAILBOX_MASK, 1);
	int ret;

	mutex_lock(&irq_data->mtx);

	*lookahead = 0;
	*host_int_status = 0;

	/* int_status_en is supposed to be non zero, otherwise interrupts
	 * shouldn't be enabled. There is however a short time frame during
	 * initialization between the irq register and int_status_en init
	 * where this can happen.
	 * We silently ignore this condition.
	 */
	if (!irq_en_reg->int_status_en) {
		ret = 0;
		goto out;
	}

	/* Read the first sizeof(struct ath10k_irq_proc_registers)
	 * bytes of the HTC register table. This
	 * will yield us the value of different int status
	 * registers and the lookahead registers.
	 */
	ret = ath10k_sdio_read(ar, MBOX_HOST_INT_STATUS_ADDRESS,
			       irq_proc_reg, sizeof(*irq_proc_reg));
	if (ret)
		goto out;

	/* Update only those registers that are enabled */
	*host_int_status = irq_proc_reg->host_int_status &
			   irq_en_reg->int_status_en;

	/* Look at mbox status */
	if (!(*host_int_status & htc_mbox)) {
		*lookahead = 0;
		ret = 0;
		goto out;
	}

	/* Mask out pending mbox value, we use look ahead as
	 * the real flag for mbox processing.
	 */
	*host_int_status &= ~htc_mbox;
	if (irq_proc_reg->rx_lookahead_valid & htc_mbox) {
		*lookahead = le32_to_cpu(
			irq_proc_reg->rx_lookahead[ATH10K_HTC_MAILBOX]);
		if (!*lookahead)
			ath10k_warn(ar, "sdio mbox lookahead is zero\n");
	}

out:
	mutex_unlock(&irq_data->mtx);
	return ret;
}

static int ath10k_sdio_mbox_proc_pending_irqs(struct ath10k *ar,
					      bool *done)
{
	u8 host_int_status;
	u32 lookahead;
	int ret;

	/* NOTE: HIF implementation guarantees that the context of this
	 * call allows us to perform SYNCHRONOUS I/O, that is we can block,
	 * sleep or call any API that can block or switch thread/task
	 * contexts. This is a fully schedulable context.
	 */

	ret = ath10k_sdio_mbox_read_int_status(ar,
					       &host_int_status,
					       &lookahead);
	if (ret) {
		*done = true;
		goto out;
	}

	if (!host_int_status && !lookahead) {
		ret = 0;
		*done = true;
		goto out;
	}

	if (lookahead) {
		ath10k_dbg(ar, ATH10K_DBG_SDIO,
			   "sdio pending mailbox msg lookahead 0x%08x\n",
			   lookahead);

		ret = ath10k_sdio_mbox_rxmsg_pending_handler(ar,
							     lookahead,
							     done);
		if (ret)
			goto out;
	}

	/* now, handle the rest of the interrupts */
	ath10k_dbg(ar, ATH10K_DBG_SDIO,
		   "sdio host_int_status 0x%x\n", host_int_status);

	if (FIELD_GET(MBOX_HOST_INT_STATUS_CPU_MASK, host_int_status)) {
		/* CPU Interrupt */
		ret = ath10k_sdio_mbox_proc_cpu_intr(ar);
		if (ret)
			goto out;
	}

	if (FIELD_GET(MBOX_HOST_INT_STATUS_ERROR_MASK, host_int_status)) {
		/* Error Interrupt */
		ret = ath10k_sdio_mbox_proc_err_intr(ar);
		if (ret)
			goto out;
	}

	if (FIELD_GET(MBOX_HOST_INT_STATUS_COUNTER_MASK, host_int_status))
		/* Counter Interrupt */
		ret = ath10k_sdio_mbox_proc_counter_intr(ar);

	ret = 0;

out:
	/* An optimization to bypass reading the IRQ status registers
	 * unecessarily which can re-wake the target, if upper layers
	 * determine that we are in a low-throughput mode, we can rely on
	 * taking another interrupt rather than re-checking the status
	 * registers which can re-wake the target.
	 *
	 * NOTE : for host interfaces that makes use of detecting pending
	 * mbox messages at hif can not use this optimization due to
	 * possible side effects, SPI requires the host to drain all
	 * messages from the mailbox before exiting the ISR routine.
	 */

	ath10k_dbg(ar, ATH10K_DBG_SDIO,
		   "sdio pending irqs done %d status %d",
		   *done, ret);

	return ret;
}

static void ath10k_sdio_set_mbox_info(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_mbox_info *mbox_info = &ar_sdio->mbox_info;
	u16 device = ar_sdio->func->device, dev_id_base, dev_id_chiprev;

	mbox_info->htc_addr = ATH10K_HIF_MBOX_BASE_ADDR;
	mbox_info->block_size = ATH10K_HIF_MBOX_BLOCK_SIZE;
	mbox_info->block_mask = ATH10K_HIF_MBOX_BLOCK_SIZE - 1;
	mbox_info->gmbox_addr = ATH10K_HIF_GMBOX_BASE_ADDR;
	mbox_info->gmbox_sz = ATH10K_HIF_GMBOX_WIDTH;

	mbox_info->ext_info[0].htc_ext_addr = ATH10K_HIF_MBOX0_EXT_BASE_ADDR;

	dev_id_base = FIELD_GET(QCA_MANUFACTURER_ID_BASE, device);
	dev_id_chiprev = FIELD_GET(QCA_MANUFACTURER_ID_REV_MASK, device);
	switch (dev_id_base) {
	case QCA_MANUFACTURER_ID_AR6005_BASE:
		if (dev_id_chiprev < 4)
			mbox_info->ext_info[0].htc_ext_sz =
				ATH10K_HIF_MBOX0_EXT_WIDTH;
		else
			/* from QCA6174 2.0(0x504), the width has been extended
			 * to 56K
			 */
			mbox_info->ext_info[0].htc_ext_sz =
				ATH10K_HIF_MBOX0_EXT_WIDTH_ROME_2_0;
		break;
	case QCA_MANUFACTURER_ID_QCA9377_BASE:
		mbox_info->ext_info[0].htc_ext_sz =
			ATH10K_HIF_MBOX0_EXT_WIDTH_ROME_2_0;
		break;
	default:
		mbox_info->ext_info[0].htc_ext_sz =
				ATH10K_HIF_MBOX0_EXT_WIDTH;
	}

	mbox_info->ext_info[1].htc_ext_addr =
		mbox_info->ext_info[0].htc_ext_addr +
		mbox_info->ext_info[0].htc_ext_sz +
		ATH10K_HIF_MBOX_DUMMY_SPACE_SIZE;
	mbox_info->ext_info[1].htc_ext_sz = ATH10K_HIF_MBOX1_EXT_WIDTH;
}

/* BMI functions */

static int ath10k_sdio_bmi_credits(struct ath10k *ar)
{
	u32 addr, cmd_credits;
	unsigned long timeout;
	int ret;

	/* Read the counter register to get the command credits */
	addr = MBOX_COUNT_DEC_ADDRESS + ATH10K_HIF_MBOX_NUM_MAX * 4;
	timeout = jiffies + BMI_COMMUNICATION_TIMEOUT_HZ;
	cmd_credits = 0;

	while (time_before(jiffies, timeout) && !cmd_credits) {
		/* Hit the credit counter with a 4-byte access, the first byte
		 * read will hit the counter and cause a decrement, while the
		 * remaining 3 bytes has no effect. The rationale behind this
		 * is to make all HIF accesses 4-byte aligned.
		 */
		ret = ath10k_sdio_read32(ar, addr, &cmd_credits);
		if (ret) {
			ath10k_warn(ar,
				    "unable to decrement the command credit count register: %d\n",
				    ret);
			return ret;
		}

		/* The counter is only 8 bits.
		 * Ignore anything in the upper 3 bytes
		 */
		cmd_credits &= 0xFF;
	}

	if (!cmd_credits) {
		ath10k_warn(ar, "bmi communication timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int ath10k_sdio_bmi_get_rx_lookahead(struct ath10k *ar)
{
	unsigned long timeout;
	u32 rx_word;
	int ret;

	timeout = jiffies + BMI_COMMUNICATION_TIMEOUT_HZ;
	rx_word = 0;

	while ((time_before(jiffies, timeout)) && !rx_word) {
		ret = ath10k_sdio_read32(ar,
					 MBOX_HOST_INT_STATUS_ADDRESS,
					 &rx_word);
		if (ret) {
			ath10k_warn(ar, "unable to read RX_LOOKAHEAD_VALID: %d\n", ret);
			return ret;
		}

		 /* all we really want is one bit */
		rx_word &= 1;
	}

	if (!rx_word) {
		ath10k_warn(ar, "bmi_recv_buf FIFO empty\n");
		return -EINVAL;
	}

	return ret;
}

static int ath10k_sdio_bmi_exchange_msg(struct ath10k *ar,
					void *req, u32 req_len,
					void *resp, u32 *resp_len)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	u32 addr;
	int ret;

	if (req) {
		ret = ath10k_sdio_bmi_credits(ar);
		if (ret)
			return ret;

		addr = ar_sdio->mbox_info.htc_addr;

		memcpy(ar_sdio->bmi_buf, req, req_len);
		ret = ath10k_sdio_write(ar, addr, ar_sdio->bmi_buf, req_len);
		if (ret) {
			ath10k_warn(ar,
				    "unable to send the bmi data to the device: %d\n",
				    ret);
			return ret;
		}
	}

	if (!resp || !resp_len)
		/* No response expected */
		return 0;

	/* During normal bootup, small reads may be required.
	 * Rather than issue an HIF Read and then wait as the Target
	 * adds successive bytes to the FIFO, we wait here until
	 * we know that response data is available.
	 *
	 * This allows us to cleanly timeout on an unexpected
	 * Target failure rather than risk problems at the HIF level.
	 * In particular, this avoids SDIO timeouts and possibly garbage
	 * data on some host controllers.  And on an interconnect
	 * such as Compact Flash (as well as some SDIO masters) which
	 * does not provide any indication on data timeout, it avoids
	 * a potential hang or garbage response.
	 *
	 * Synchronization is more difficult for reads larger than the
	 * size of the MBOX FIFO (128B), because the Target is unable
	 * to push the 129th byte of data until AFTER the Host posts an
	 * HIF Read and removes some FIFO data.  So for large reads the
	 * Host proceeds to post an HIF Read BEFORE all the data is
	 * actually available to read.  Fortunately, large BMI reads do
	 * not occur in practice -- they're supported for debug/development.
	 *
	 * So Host/Target BMI synchronization is divided into these cases:
	 *  CASE 1: length < 4
	 *        Should not happen
	 *
	 *  CASE 2: 4 <= length <= 128
	 *        Wait for first 4 bytes to be in FIFO
	 *        If CONSERVATIVE_BMI_READ is enabled, also wait for
	 *        a BMI command credit, which indicates that the ENTIRE
	 *        response is available in the the FIFO
	 *
	 *  CASE 3: length > 128
	 *        Wait for the first 4 bytes to be in FIFO
	 *
	 * For most uses, a small timeout should be sufficient and we will
	 * usually see a response quickly; but there may be some unusual
	 * (debug) cases of BMI_EXECUTE where we want an larger timeout.
	 * For now, we use an unbounded busy loop while waiting for
	 * BMI_EXECUTE.
	 *
	 * If BMI_EXECUTE ever needs to support longer-latency execution,
	 * especially in production, this code needs to be enhanced to sleep
	 * and yield.  Also note that BMI_COMMUNICATION_TIMEOUT is currently
	 * a function of Host processor speed.
	 */
	ret = ath10k_sdio_bmi_get_rx_lookahead(ar);
	if (ret)
		return ret;

	/* We always read from the start of the mbox address */
	addr = ar_sdio->mbox_info.htc_addr;
	ret = ath10k_sdio_read(ar, addr, ar_sdio->bmi_buf, *resp_len);
	if (ret) {
		ath10k_warn(ar,
			    "unable to read the bmi data from the device: %d\n",
			    ret);
		return ret;
	}

	memcpy(resp, ar_sdio->bmi_buf, *resp_len);

	return 0;
}

/* sdio async handling functions */

static struct ath10k_sdio_bus_request
*ath10k_sdio_alloc_busreq(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_sdio_bus_request *bus_req;

	spin_lock_bh(&ar_sdio->lock);

	if (list_empty(&ar_sdio->bus_req_freeq)) {
		bus_req = NULL;
		goto out;
	}

	bus_req = list_first_entry(&ar_sdio->bus_req_freeq,
				   struct ath10k_sdio_bus_request, list);
	list_del(&bus_req->list);

out:
	spin_unlock_bh(&ar_sdio->lock);
	return bus_req;
}

static void ath10k_sdio_free_bus_req(struct ath10k *ar,
				     struct ath10k_sdio_bus_request *bus_req)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);

	memset(bus_req, 0, sizeof(*bus_req));

	spin_lock_bh(&ar_sdio->lock);
	list_add_tail(&bus_req->list, &ar_sdio->bus_req_freeq);
	spin_unlock_bh(&ar_sdio->lock);
}

static void __ath10k_sdio_write_async(struct ath10k *ar,
				      struct ath10k_sdio_bus_request *req)
{
	struct ath10k_htc_ep *ep;
	struct sk_buff *skb;
	int ret;

	skb = req->skb;
	ret = ath10k_sdio_write(ar, req->address, skb->data, skb->len);
	if (ret)
		ath10k_warn(ar, "failed to write skb to 0x%x asynchronously: %d",
			    req->address, ret);

	if (req->htc_msg) {
		ep = &ar->htc.endpoint[req->eid];
		ath10k_htc_notify_tx_completion(ep, skb);
	} else if (req->comp) {
		complete(req->comp);
	}

	ath10k_sdio_free_bus_req(ar, req);
}

static void ath10k_sdio_write_async_work(struct work_struct *work)
{
	struct ath10k_sdio *ar_sdio = container_of(work, struct ath10k_sdio,
						   wr_async_work);
	struct ath10k *ar = ar_sdio->ar;
	struct ath10k_sdio_bus_request *req, *tmp_req;

	spin_lock_bh(&ar_sdio->wr_async_lock);

	list_for_each_entry_safe(req, tmp_req, &ar_sdio->wr_asyncq, list) {
		list_del(&req->list);
		spin_unlock_bh(&ar_sdio->wr_async_lock);
		__ath10k_sdio_write_async(ar, req);
		spin_lock_bh(&ar_sdio->wr_async_lock);
	}

	spin_unlock_bh(&ar_sdio->wr_async_lock);
}

static int ath10k_sdio_prep_async_req(struct ath10k *ar, u32 addr,
				      struct sk_buff *skb,
				      struct completion *comp,
				      bool htc_msg, enum ath10k_htc_ep_id eid)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_sdio_bus_request *bus_req;

	/* Allocate a bus request for the message and queue it on the
	 * SDIO workqueue.
	 */
	bus_req = ath10k_sdio_alloc_busreq(ar);
	if (!bus_req) {
		ath10k_warn(ar,
			    "unable to allocate bus request for async request\n");
		return -ENOMEM;
	}

	bus_req->skb = skb;
	bus_req->eid = eid;
	bus_req->address = addr;
	bus_req->htc_msg = htc_msg;
	bus_req->comp = comp;

	spin_lock_bh(&ar_sdio->wr_async_lock);
	list_add_tail(&bus_req->list, &ar_sdio->wr_asyncq);
	spin_unlock_bh(&ar_sdio->wr_async_lock);

	return 0;
}

/* IRQ handler */

static void ath10k_sdio_irq_handler(struct sdio_func *func)
{
	struct ath10k_sdio *ar_sdio = sdio_get_drvdata(func);
	struct ath10k *ar = ar_sdio->ar;
	unsigned long timeout;
	bool done = false;
	int ret;

	/* Release the host during interrupts so we can pick it back up when
	 * we process commands.
	 */
	sdio_release_host(ar_sdio->func);

	timeout = jiffies + ATH10K_SDIO_HIF_COMMUNICATION_TIMEOUT_HZ;
	do {
		ret = ath10k_sdio_mbox_proc_pending_irqs(ar, &done);
		if (ret)
			break;
	} while (time_before(jiffies, timeout) && !done);

	ath10k_mac_tx_push_pending(ar);

	sdio_claim_host(ar_sdio->func);

	if (ret && ret != -ECANCELED)
		ath10k_warn(ar, "failed to process pending SDIO interrupts: %d\n",
			    ret);
}

/* sdio HIF functions */

static int ath10k_sdio_hif_disable_intrs(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_sdio_irq_data *irq_data = &ar_sdio->irq_data;
	struct ath10k_sdio_irq_enable_regs *regs = irq_data->irq_en_reg;
	int ret;

	mutex_lock(&irq_data->mtx);

	memset(regs, 0, sizeof(*regs));
	ret = ath10k_sdio_write(ar, MBOX_INT_STATUS_ENABLE_ADDRESS,
				&regs->int_status_en, sizeof(*regs));
	if (ret)
		ath10k_warn(ar, "unable to disable sdio interrupts: %d\n", ret);

	mutex_unlock(&irq_data->mtx);

	return ret;
}

static int ath10k_sdio_hif_power_up(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct sdio_func *func = ar_sdio->func;
	int ret;

	if (!ar_sdio->is_disabled)
		return 0;

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "sdio power on\n");

	sdio_claim_host(func);

	ret = sdio_enable_func(func);
	if (ret) {
		ath10k_warn(ar, "unable to enable sdio function: %d)\n", ret);
		sdio_release_host(func);
		return ret;
	}

	sdio_release_host(func);

	/* Wait for hardware to initialise. It should take a lot less than
	 * 20 ms but let's be conservative here.
	 */
	msleep(20);

	ar_sdio->is_disabled = false;

	ret = ath10k_sdio_hif_disable_intrs(ar);
	if (ret)
		return ret;

	return 0;
}

static void ath10k_sdio_hif_power_down(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	int ret;

	if (ar_sdio->is_disabled)
		return;

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "sdio power off\n");

	/* Disable the card */
	sdio_claim_host(ar_sdio->func);
	ret = sdio_disable_func(ar_sdio->func);
	sdio_release_host(ar_sdio->func);

	if (ret)
		ath10k_warn(ar, "unable to disable sdio function: %d\n", ret);

	ar_sdio->is_disabled = true;
}

static int ath10k_sdio_hif_tx_sg(struct ath10k *ar, u8 pipe_id,
				 struct ath10k_hif_sg_item *items, int n_items)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	enum ath10k_htc_ep_id eid;
	struct sk_buff *skb;
	int ret, i;

	eid = pipe_id_to_eid(pipe_id);

	for (i = 0; i < n_items; i++) {
		size_t padded_len;
		u32 address;

		skb = items[i].transfer_context;
		padded_len = ath10k_sdio_calc_txrx_padded_len(ar_sdio,
							      skb->len);
		skb_trim(skb, padded_len);

		/* Write TX data to the end of the mbox address space */
		address = ar_sdio->mbox_addr[eid] + ar_sdio->mbox_size[eid] -
			  skb->len;
		ret = ath10k_sdio_prep_async_req(ar, address, skb,
						 NULL, true, eid);
		if (ret)
			return ret;
	}

	queue_work(ar_sdio->workqueue, &ar_sdio->wr_async_work);

	return 0;
}

static int ath10k_sdio_hif_enable_intrs(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_sdio_irq_data *irq_data = &ar_sdio->irq_data;
	struct ath10k_sdio_irq_enable_regs *regs = irq_data->irq_en_reg;
	int ret;

	mutex_lock(&irq_data->mtx);

	/* Enable all but CPU interrupts */
	regs->int_status_en = FIELD_PREP(MBOX_INT_STATUS_ENABLE_ERROR_MASK, 1) |
			      FIELD_PREP(MBOX_INT_STATUS_ENABLE_CPU_MASK, 1) |
			      FIELD_PREP(MBOX_INT_STATUS_ENABLE_COUNTER_MASK, 1);

	/* NOTE: There are some cases where HIF can do detection of
	 * pending mbox messages which is disabled now.
	 */
	regs->int_status_en |=
		FIELD_PREP(MBOX_INT_STATUS_ENABLE_MBOX_DATA_MASK, 1);

	/* Set up the CPU Interrupt status Register */
	regs->cpu_int_status_en = 0;

	/* Set up the Error Interrupt status Register */
	regs->err_int_status_en =
		FIELD_PREP(MBOX_ERROR_STATUS_ENABLE_RX_UNDERFLOW_MASK, 1) |
		FIELD_PREP(MBOX_ERROR_STATUS_ENABLE_TX_OVERFLOW_MASK, 1);

	/* Enable Counter interrupt status register to get fatal errors for
	 * debugging.
	 */
	regs->cntr_int_status_en =
		FIELD_PREP(MBOX_COUNTER_INT_STATUS_ENABLE_BIT_MASK,
			   ATH10K_SDIO_TARGET_DEBUG_INTR_MASK);

	ret = ath10k_sdio_write(ar, MBOX_INT_STATUS_ENABLE_ADDRESS,
				&regs->int_status_en, sizeof(*regs));
	if (ret)
		ath10k_warn(ar,
			    "failed to update mbox interrupt status register : %d\n",
			    ret);

	mutex_unlock(&irq_data->mtx);
	return ret;
}

static int ath10k_sdio_hif_set_mbox_sleep(struct ath10k *ar, bool enable_sleep)
{
	u32 val;
	int ret;

	ret = ath10k_sdio_read32(ar, ATH10K_FIFO_TIMEOUT_AND_CHIP_CONTROL, &val);
	if (ret) {
		ath10k_warn(ar, "failed to read fifo/chip control register: %d\n",
			    ret);
		return ret;
	}

	if (enable_sleep)
		val &= ATH10K_FIFO_TIMEOUT_AND_CHIP_CONTROL_DISABLE_SLEEP_OFF;
	else
		val |= ATH10K_FIFO_TIMEOUT_AND_CHIP_CONTROL_DISABLE_SLEEP_ON;

	ret = ath10k_sdio_write32(ar, ATH10K_FIFO_TIMEOUT_AND_CHIP_CONTROL, val);
	if (ret) {
		ath10k_warn(ar, "failed to write to FIFO_TIMEOUT_AND_CHIP_CONTROL: %d",
			    ret);
		return ret;
	}

	return 0;
}

/* HIF diagnostics */

static int ath10k_sdio_hif_diag_read(struct ath10k *ar, u32 address, void *buf,
				     size_t buf_len)
{
	int ret;
	void *mem;

	mem = kzalloc(buf_len, GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	/* set window register to start read cycle */
	ret = ath10k_sdio_write32(ar, MBOX_WINDOW_READ_ADDR_ADDRESS, address);
	if (ret) {
		ath10k_warn(ar, "failed to set mbox window read address: %d", ret);
		goto out;
	}

	/* read the data */
	ret = ath10k_sdio_read(ar, MBOX_WINDOW_DATA_ADDRESS, mem, buf_len);
	if (ret) {
		ath10k_warn(ar, "failed to read from mbox window data address: %d\n",
			    ret);
		goto out;
	}

	memcpy(buf, mem, buf_len);

out:
	kfree(mem);

	return ret;
}

static int ath10k_sdio_hif_diag_read32(struct ath10k *ar, u32 address,
				       u32 *value)
{
	__le32 *val;
	int ret;

	val = kzalloc(sizeof(*val), GFP_KERNEL);
	if (!val)
		return -ENOMEM;

	ret = ath10k_sdio_hif_diag_read(ar, address, val, sizeof(*val));
	if (ret)
		goto out;

	*value = __le32_to_cpu(*val);

out:
	kfree(val);

	return ret;
}

static int ath10k_sdio_hif_diag_write_mem(struct ath10k *ar, u32 address,
					  const void *data, int nbytes)
{
	int ret;

	/* set write data */
	ret = ath10k_sdio_write(ar, MBOX_WINDOW_DATA_ADDRESS, data, nbytes);
	if (ret) {
		ath10k_warn(ar,
			    "failed to write 0x%p to mbox window data address: %d\n",
			    data, ret);
		return ret;
	}

	/* set window register, which starts the write cycle */
	ret = ath10k_sdio_write32(ar, MBOX_WINDOW_WRITE_ADDR_ADDRESS, address);
	if (ret) {
		ath10k_warn(ar, "failed to set mbox window write address: %d", ret);
		return ret;
	}

	return 0;
}

/* HIF start/stop */

static int ath10k_sdio_hif_start(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	u32 addr, val;
	int ret;

	/* Sleep 20 ms before HIF interrupts are disabled.
	 * This will give target plenty of time to process the BMI done
	 * request before interrupts are disabled.
	 */
	msleep(20);
	ret = ath10k_sdio_hif_disable_intrs(ar);
	if (ret)
		return ret;

	/* eid 0 always uses the lower part of the extended mailbox address
	 * space (ext_info[0].htc_ext_addr).
	 */
	ar_sdio->mbox_addr[0] = ar_sdio->mbox_info.ext_info[0].htc_ext_addr;
	ar_sdio->mbox_size[0] = ar_sdio->mbox_info.ext_info[0].htc_ext_sz;

	sdio_claim_host(ar_sdio->func);

	/* Register the isr */
	ret =  sdio_claim_irq(ar_sdio->func, ath10k_sdio_irq_handler);
	if (ret) {
		ath10k_warn(ar, "failed to claim sdio interrupt: %d\n", ret);
		sdio_release_host(ar_sdio->func);
		return ret;
	}

	sdio_release_host(ar_sdio->func);

	ret = ath10k_sdio_hif_enable_intrs(ar);
	if (ret)
		ath10k_warn(ar, "failed to enable sdio interrupts: %d\n", ret);

	addr = host_interest_item_address(HI_ITEM(hi_acs_flags));

	ret = ath10k_sdio_hif_diag_read32(ar, addr, &val);
	if (ret) {
		ath10k_warn(ar, "unable to read hi_acs_flags address: %d\n", ret);
		return ret;
	}

	if (val & HI_ACS_FLAGS_SDIO_SWAP_MAILBOX_FW_ACK) {
		ath10k_dbg(ar, ATH10K_DBG_SDIO,
			   "sdio mailbox swap service enabled\n");
		ar_sdio->swap_mbox = true;
	}

	/* Enable sleep and then disable it again */
	ret = ath10k_sdio_hif_set_mbox_sleep(ar, true);
	if (ret)
		return ret;

	/* Wait for 20ms for the written value to take effect */
	msleep(20);

	ret = ath10k_sdio_hif_set_mbox_sleep(ar, false);
	if (ret)
		return ret;

	return 0;
}

#define SDIO_IRQ_DISABLE_TIMEOUT_HZ (3 * HZ)

static void ath10k_sdio_irq_disable(struct ath10k *ar)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_sdio_irq_data *irq_data = &ar_sdio->irq_data;
	struct ath10k_sdio_irq_enable_regs *regs = irq_data->irq_en_reg;
	struct sk_buff *skb;
	struct completion irqs_disabled_comp;
	int ret;

	skb = dev_alloc_skb(sizeof(*regs));
	if (!skb)
		return;

	mutex_lock(&irq_data->mtx);

	memset(regs, 0, sizeof(*regs)); /* disable all interrupts */
	memcpy(skb->data, regs, sizeof(*regs));
	skb_put(skb, sizeof(*regs));

	mutex_unlock(&irq_data->mtx);

	init_completion(&irqs_disabled_comp);
	ret = ath10k_sdio_prep_async_req(ar, MBOX_INT_STATUS_ENABLE_ADDRESS,
					 skb, &irqs_disabled_comp, false, 0);
	if (ret)
		goto out;

	queue_work(ar_sdio->workqueue, &ar_sdio->wr_async_work);

	/* Wait for the completion of the IRQ disable request.
	 * If there is a timeout we will try to disable irq's anyway.
	 */
	ret = wait_for_completion_timeout(&irqs_disabled_comp,
					  SDIO_IRQ_DISABLE_TIMEOUT_HZ);
	if (!ret)
		ath10k_warn(ar, "sdio irq disable request timed out\n");

	sdio_claim_host(ar_sdio->func);

	ret = sdio_release_irq(ar_sdio->func);
	if (ret)
		ath10k_warn(ar, "failed to release sdio interrupt: %d\n", ret);

	sdio_release_host(ar_sdio->func);

out:
	kfree_skb(skb);
}

static void ath10k_sdio_hif_stop(struct ath10k *ar)
{
	struct ath10k_sdio_bus_request *req, *tmp_req;
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);

	ath10k_sdio_irq_disable(ar);

	cancel_work_sync(&ar_sdio->wr_async_work);

	spin_lock_bh(&ar_sdio->wr_async_lock);

	/* Free all bus requests that have not been handled */
	list_for_each_entry_safe(req, tmp_req, &ar_sdio->wr_asyncq, list) {
		struct ath10k_htc_ep *ep;

		list_del(&req->list);

		if (req->htc_msg) {
			ep = &ar->htc.endpoint[req->eid];
			ath10k_htc_notify_tx_completion(ep, req->skb);
		} else if (req->skb) {
			kfree_skb(req->skb);
		}
		ath10k_sdio_free_bus_req(ar, req);
	}

	spin_unlock_bh(&ar_sdio->wr_async_lock);
}

#ifdef CONFIG_PM

static int ath10k_sdio_hif_suspend(struct ath10k *ar)
{
	return -EOPNOTSUPP;
}

static int ath10k_sdio_hif_resume(struct ath10k *ar)
{
	switch (ar->state) {
	case ATH10K_STATE_OFF:
		ath10k_dbg(ar, ATH10K_DBG_SDIO,
			   "sdio resume configuring sdio\n");

		/* need to set sdio settings after power is cut from sdio */
		ath10k_sdio_config(ar);
		break;

	case ATH10K_STATE_ON:
	default:
		break;
	}

	return 0;
}
#endif

static int ath10k_sdio_hif_map_service_to_pipe(struct ath10k *ar,
					       u16 service_id,
					       u8 *ul_pipe, u8 *dl_pipe)
{
	struct ath10k_sdio *ar_sdio = ath10k_sdio_priv(ar);
	struct ath10k_htc *htc = &ar->htc;
	u32 htt_addr, wmi_addr, htt_mbox_size, wmi_mbox_size;
	enum ath10k_htc_ep_id eid;
	bool ep_found = false;
	int i;

	/* For sdio, we are interested in the mapping between eid
	 * and pipeid rather than service_id to pipe_id.
	 * First we find out which eid has been allocated to the
	 * service...
	 */
	for (i = 0; i < ATH10K_HTC_EP_COUNT; i++) {
		if (htc->endpoint[i].service_id == service_id) {
			eid = htc->endpoint[i].eid;
			ep_found = true;
			break;
		}
	}

	if (!ep_found)
		return -EINVAL;

	/* Then we create the simplest mapping possible between pipeid
	 * and eid
	 */
	*ul_pipe = *dl_pipe = (u8)eid;

	/* Normally, HTT will use the upper part of the extended
	 * mailbox address space (ext_info[1].htc_ext_addr) and WMI ctrl
	 * the lower part (ext_info[0].htc_ext_addr).
	 * If fw wants swapping of mailbox addresses, the opposite is true.
	 */
	if (ar_sdio->swap_mbox) {
		htt_addr = ar_sdio->mbox_info.ext_info[0].htc_ext_addr;
		wmi_addr = ar_sdio->mbox_info.ext_info[1].htc_ext_addr;
		htt_mbox_size = ar_sdio->mbox_info.ext_info[0].htc_ext_sz;
		wmi_mbox_size = ar_sdio->mbox_info.ext_info[1].htc_ext_sz;
	} else {
		htt_addr = ar_sdio->mbox_info.ext_info[1].htc_ext_addr;
		wmi_addr = ar_sdio->mbox_info.ext_info[0].htc_ext_addr;
		htt_mbox_size = ar_sdio->mbox_info.ext_info[1].htc_ext_sz;
		wmi_mbox_size = ar_sdio->mbox_info.ext_info[0].htc_ext_sz;
	}

	switch (service_id) {
	case ATH10K_HTC_SVC_ID_RSVD_CTRL:
		/* HTC ctrl ep mbox address has already been setup in
		 * ath10k_sdio_hif_start
		 */
		break;
	case ATH10K_HTC_SVC_ID_WMI_CONTROL:
		ar_sdio->mbox_addr[eid] = wmi_addr;
		ar_sdio->mbox_size[eid] = wmi_mbox_size;
		ath10k_dbg(ar, ATH10K_DBG_SDIO,
			   "sdio wmi ctrl mbox_addr 0x%x mbox_size %d\n",
			   ar_sdio->mbox_addr[eid], ar_sdio->mbox_size[eid]);
		break;
	case ATH10K_HTC_SVC_ID_HTT_DATA_MSG:
		ar_sdio->mbox_addr[eid] = htt_addr;
		ar_sdio->mbox_size[eid] = htt_mbox_size;
		ath10k_dbg(ar, ATH10K_DBG_SDIO,
			   "sdio htt data mbox_addr 0x%x mbox_size %d\n",
			   ar_sdio->mbox_addr[eid], ar_sdio->mbox_size[eid]);
		break;
	default:
		ath10k_warn(ar, "unsupported HTC service id: %d\n",
			    service_id);
		return -EINVAL;
	}

	return 0;
}

static void ath10k_sdio_hif_get_default_pipe(struct ath10k *ar,
					     u8 *ul_pipe, u8 *dl_pipe)
{
	ath10k_dbg(ar, ATH10K_DBG_SDIO, "sdio hif get default pipe\n");

	/* HTC ctrl ep (SVC id 1) always has eid (and pipe_id in our
	 * case) == 0
	 */
	*ul_pipe = 0;
	*dl_pipe = 0;
}

/* This op is currently only used by htc_wait_target if the HTC ready
 * message times out. It is not applicable for SDIO since there is nothing
 * we can do if the HTC ready message does not arrive in time.
 * TODO: Make this op non mandatory by introducing a NULL check in the
 * hif op wrapper.
 */
static void ath10k_sdio_hif_send_complete_check(struct ath10k *ar,
						u8 pipe, int force)
{
}

static const struct ath10k_hif_ops ath10k_sdio_hif_ops = {
	.tx_sg			= ath10k_sdio_hif_tx_sg,
	.diag_read		= ath10k_sdio_hif_diag_read,
	.diag_write		= ath10k_sdio_hif_diag_write_mem,
	.exchange_bmi_msg	= ath10k_sdio_bmi_exchange_msg,
	.start			= ath10k_sdio_hif_start,
	.stop			= ath10k_sdio_hif_stop,
	.map_service_to_pipe	= ath10k_sdio_hif_map_service_to_pipe,
	.get_default_pipe	= ath10k_sdio_hif_get_default_pipe,
	.send_complete_check	= ath10k_sdio_hif_send_complete_check,
	.power_up		= ath10k_sdio_hif_power_up,
	.power_down		= ath10k_sdio_hif_power_down,
#ifdef CONFIG_PM
	.suspend		= ath10k_sdio_hif_suspend,
	.resume			= ath10k_sdio_hif_resume,
#endif
};

#ifdef CONFIG_PM_SLEEP

/* Empty handlers so that mmc subsystem doesn't remove us entirely during
 * suspend. We instead follow cfg80211 suspend/resume handlers.
 */
static int ath10k_sdio_pm_suspend(struct device *device)
{
	return 0;
}

static int ath10k_sdio_pm_resume(struct device *device)
{
	return 0;
}

static SIMPLE_DEV_PM_OPS(ath10k_sdio_pm_ops, ath10k_sdio_pm_suspend,
			 ath10k_sdio_pm_resume);

#define ATH10K_SDIO_PM_OPS (&ath10k_sdio_pm_ops)

#else

#define ATH10K_SDIO_PM_OPS NULL

#endif /* CONFIG_PM_SLEEP */

static int ath10k_sdio_probe(struct sdio_func *func,
			     const struct sdio_device_id *id)
{
	struct ath10k_sdio *ar_sdio;
	struct ath10k *ar;
	enum ath10k_hw_rev hw_rev;
	u32 chip_id, dev_id_base;
	int ret, i;

	/* Assumption: All SDIO based chipsets (so far) are QCA6174 based.
	 * If there will be newer chipsets that does not use the hw reg
	 * setup as defined in qca6174_regs and qca6174_values, this
	 * assumption is no longer valid and hw_rev must be setup differently
	 * depending on chipset.
	 */
	hw_rev = ATH10K_HW_QCA6174;

	ar = ath10k_core_create(sizeof(*ar_sdio), &func->dev, ATH10K_BUS_SDIO,
				hw_rev, &ath10k_sdio_hif_ops);
	if (!ar) {
		dev_err(&func->dev, "failed to allocate core\n");
		return -ENOMEM;
	}

	ath10k_dbg(ar, ATH10K_DBG_BOOT,
		   "sdio new func %d vendor 0x%x device 0x%x block 0x%x/0x%x\n",
		   func->num, func->vendor, func->device,
		   func->max_blksize, func->cur_blksize);

	ar_sdio = ath10k_sdio_priv(ar);

	ar_sdio->irq_data.irq_proc_reg =
		devm_kzalloc(ar->dev, sizeof(struct ath10k_sdio_irq_proc_regs),
			     GFP_KERNEL);
	if (!ar_sdio->irq_data.irq_proc_reg) {
		ret = -ENOMEM;
		goto err_core_destroy;
	}

	ar_sdio->irq_data.irq_en_reg =
		devm_kzalloc(ar->dev, sizeof(struct ath10k_sdio_irq_enable_regs),
			     GFP_KERNEL);
	if (!ar_sdio->irq_data.irq_en_reg) {
		ret = -ENOMEM;
		goto err_core_destroy;
	}

	ar_sdio->bmi_buf = devm_kzalloc(ar->dev, BMI_MAX_CMDBUF_SIZE, GFP_KERNEL);
	if (!ar_sdio->bmi_buf) {
		ret = -ENOMEM;
		goto err_core_destroy;
	}

	ar_sdio->func = func;
	sdio_set_drvdata(func, ar_sdio);

	ar_sdio->is_disabled = true;
	ar_sdio->ar = ar;

	spin_lock_init(&ar_sdio->lock);
	spin_lock_init(&ar_sdio->wr_async_lock);
	mutex_init(&ar_sdio->irq_data.mtx);

	INIT_LIST_HEAD(&ar_sdio->bus_req_freeq);
	INIT_LIST_HEAD(&ar_sdio->wr_asyncq);

	INIT_WORK(&ar_sdio->wr_async_work, ath10k_sdio_write_async_work);
	ar_sdio->workqueue = create_singlethread_workqueue("ath10k_sdio_wq");
	if (!ar_sdio->workqueue) {
		ret = -ENOMEM;
		goto err_core_destroy;
	}

	for (i = 0; i < ATH10K_SDIO_BUS_REQUEST_MAX_NUM; i++)
		ath10k_sdio_free_bus_req(ar, &ar_sdio->bus_req[i]);

	dev_id_base = FIELD_GET(QCA_MANUFACTURER_ID_BASE, id->device);
	switch (dev_id_base) {
	case QCA_MANUFACTURER_ID_AR6005_BASE:
	case QCA_MANUFACTURER_ID_QCA9377_BASE:
		ar->dev_id = QCA9377_1_0_DEVICE_ID;
		break;
	default:
		ret = -ENODEV;
		ath10k_err(ar, "unsupported device id %u (0x%x)\n",
			   dev_id_base, id->device);
		goto err_free_wq;
	}

	ar->id.vendor = id->vendor;
	ar->id.device = id->device;

	ath10k_sdio_set_mbox_info(ar);

	ret = ath10k_sdio_config(ar);
	if (ret) {
		ath10k_err(ar, "failed to config sdio: %d\n", ret);
		goto err_free_wq;
	}

	/* TODO: don't know yet how to get chip_id with SDIO */
	chip_id = 0;
	ret = ath10k_core_register(ar, chip_id);
	if (ret) {
		ath10k_err(ar, "failed to register driver core: %d\n", ret);
		goto err_free_wq;
	}

	/* TODO: remove this once SDIO support is fully implemented */
	ath10k_warn(ar, "WARNING: ath10k SDIO support is incomplete, don't expect anything to work!\n");

	return 0;

err_free_wq:
	destroy_workqueue(ar_sdio->workqueue);
err_core_destroy:
	ath10k_core_destroy(ar);

	return ret;
}

static void ath10k_sdio_remove(struct sdio_func *func)
{
	struct ath10k_sdio *ar_sdio = sdio_get_drvdata(func);
	struct ath10k *ar = ar_sdio->ar;

	ath10k_dbg(ar, ATH10K_DBG_BOOT,
		   "sdio removed func %d vendor 0x%x device 0x%x\n",
		   func->num, func->vendor, func->device);

	(void)ath10k_sdio_hif_disable_intrs(ar);
	cancel_work_sync(&ar_sdio->wr_async_work);
	ath10k_core_unregister(ar);
	ath10k_core_destroy(ar);

	flush_workqueue(ar_sdio->workqueue);
	destroy_workqueue(ar_sdio->workqueue);
}

static const struct sdio_device_id ath10k_sdio_devices[] = {
	{SDIO_DEVICE(QCA_MANUFACTURER_CODE,
		     (QCA_SDIO_ID_AR6005_BASE | 0xA))},
	{SDIO_DEVICE(QCA_MANUFACTURER_CODE,
		     (QCA_SDIO_ID_QCA9377_BASE | 0x1))},
	{},
};

MODULE_DEVICE_TABLE(sdio, ath10k_sdio_devices);

static struct sdio_driver ath10k_sdio_driver = {
	.name = "ath10k_sdio",
	.id_table = ath10k_sdio_devices,
	.probe = ath10k_sdio_probe,
	.remove = ath10k_sdio_remove,
	.drv.pm = ATH10K_SDIO_PM_OPS,
};

static int __init ath10k_sdio_init(void)
{
	int ret;

	ret = sdio_register_driver(&ath10k_sdio_driver);
	if (ret)
		pr_err("sdio driver registration failed: %d\n", ret);

	return ret;
}

static void __exit ath10k_sdio_exit(void)
{
	sdio_unregister_driver(&ath10k_sdio_driver);
}

module_init(ath10k_sdio_init);
module_exit(ath10k_sdio_exit);

MODULE_AUTHOR("Qualcomm Atheros");
MODULE_DESCRIPTION("Driver support for Qualcomm Atheros 802.11ac WLAN SDIO devices");
MODULE_LICENSE("Dual BSD/GPL");
