/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/* ****************** SDIO CARD Interface Functions **************************/

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/scatterlist.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/platform_data/brcmfmac-sdio.h>

#include <defs.h>
#include <brcm_hw_ids.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include <soc.h>
#include "dhd_bus.h"
#include "dhd_dbg.h"
#include "sdio_host.h"

#define SDIOH_API_ACCESS_RETRY_LIMIT	2


static irqreturn_t brcmf_sdio_oob_irqhandler(int irq, void *dev_id)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev_id);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;

	brcmf_dbg(INTR, "OOB intr triggered\n");

	/* out-of-band interrupt is level-triggered which won't
	 * be cleared until dpc
	 */
	if (sdiodev->irq_en) {
		disable_irq_nosync(irq);
		sdiodev->irq_en = false;
	}

	brcmf_sdbrcm_isr(sdiodev->bus);

	return IRQ_HANDLED;
}

static void brcmf_sdio_ib_irqhandler(struct sdio_func *func)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(&func->dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;

	brcmf_dbg(INTR, "IB intr triggered\n");

	brcmf_sdbrcm_isr(sdiodev->bus);
}

/* dummy handler for SDIO function 2 interrupt */
static void brcmf_sdio_dummy_irqhandler(struct sdio_func *func)
{
}

int brcmf_sdio_intr_register(struct brcmf_sdio_dev *sdiodev)
{
	int ret = 0;
	u8 data;
	unsigned long flags;

	if ((sdiodev->pdata) && (sdiodev->pdata->oob_irq_supported)) {
		brcmf_dbg(SDIO, "Enter, register OOB IRQ %d\n",
			  sdiodev->pdata->oob_irq_nr);
		ret = request_irq(sdiodev->pdata->oob_irq_nr,
				  brcmf_sdio_oob_irqhandler,
				  sdiodev->pdata->oob_irq_flags,
				  "brcmf_oob_intr",
				  &sdiodev->func[1]->dev);
		if (ret != 0) {
			brcmf_err("request_irq failed %d\n", ret);
			return ret;
		}
		sdiodev->oob_irq_requested = true;
		spin_lock_init(&sdiodev->irq_en_lock);
		spin_lock_irqsave(&sdiodev->irq_en_lock, flags);
		sdiodev->irq_en = true;
		spin_unlock_irqrestore(&sdiodev->irq_en_lock, flags);

		ret = enable_irq_wake(sdiodev->pdata->oob_irq_nr);
		if (ret != 0) {
			brcmf_err("enable_irq_wake failed %d\n", ret);
			return ret;
		}
		sdiodev->irq_wake = true;

		sdio_claim_host(sdiodev->func[1]);

		/* must configure SDIO_CCCR_IENx to enable irq */
		data = brcmf_sdio_regrb(sdiodev, SDIO_CCCR_IENx, &ret);
		data |= 1 << SDIO_FUNC_1 | 1 << SDIO_FUNC_2 | 1;
		brcmf_sdio_regwb(sdiodev, SDIO_CCCR_IENx, data, &ret);

		/* redirect, configure and enable io for interrupt signal */
		data = SDIO_SEPINT_MASK | SDIO_SEPINT_OE;
		if (sdiodev->pdata->oob_irq_flags & IRQF_TRIGGER_HIGH)
			data |= SDIO_SEPINT_ACT_HI;
		brcmf_sdio_regwb(sdiodev, SDIO_CCCR_BRCM_SEPINT, data, &ret);

		sdio_release_host(sdiodev->func[1]);
	} else {
		brcmf_dbg(SDIO, "Entering\n");
		sdio_claim_host(sdiodev->func[1]);
		sdio_claim_irq(sdiodev->func[1], brcmf_sdio_ib_irqhandler);
		sdio_claim_irq(sdiodev->func[2], brcmf_sdio_dummy_irqhandler);
		sdio_release_host(sdiodev->func[1]);
	}

	return 0;
}

int brcmf_sdio_intr_unregister(struct brcmf_sdio_dev *sdiodev)
{
	brcmf_dbg(SDIO, "Entering\n");

	if ((sdiodev->pdata) && (sdiodev->pdata->oob_irq_supported)) {
		sdio_claim_host(sdiodev->func[1]);
		brcmf_sdio_regwb(sdiodev, SDIO_CCCR_BRCM_SEPINT, 0, NULL);
		brcmf_sdio_regwb(sdiodev, SDIO_CCCR_IENx, 0, NULL);
		sdio_release_host(sdiodev->func[1]);

		if (sdiodev->oob_irq_requested) {
			sdiodev->oob_irq_requested = false;
			if (sdiodev->irq_wake) {
				disable_irq_wake(sdiodev->pdata->oob_irq_nr);
				sdiodev->irq_wake = false;
			}
			free_irq(sdiodev->pdata->oob_irq_nr,
				 &sdiodev->func[1]->dev);
			sdiodev->irq_en = false;
		}
	} else {
		sdio_claim_host(sdiodev->func[1]);
		sdio_release_irq(sdiodev->func[2]);
		sdio_release_irq(sdiodev->func[1]);
		sdio_release_host(sdiodev->func[1]);
	}

	return 0;
}

static int
brcmf_sdcard_set_sbaddr_window(struct brcmf_sdio_dev *sdiodev, u32 address)
{
	int err = 0, i;
	u8 addr[3];
	s32 retry;

	addr[0] = (address >> 8) & SBSDIO_SBADDRLOW_MASK;
	addr[1] = (address >> 16) & SBSDIO_SBADDRMID_MASK;
	addr[2] = (address >> 24) & SBSDIO_SBADDRHIGH_MASK;

	for (i = 0; i < 3; i++) {
		retry = 0;
		do {
			if (retry)
				usleep_range(1000, 2000);
			err = brcmf_sdioh_request_byte(sdiodev, SDIOH_WRITE,
					SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRLOW + i,
					&addr[i]);
		} while (err != 0 && retry++ < SDIOH_API_ACCESS_RETRY_LIMIT);

		if (err) {
			brcmf_err("failed at addr:0x%0x\n",
				  SBSDIO_FUNC1_SBADDRLOW + i);
			break;
		}
	}

	return err;
}

static int
brcmf_sdio_addrprep(struct brcmf_sdio_dev *sdiodev, uint width, u32 *addr)
{
	uint bar0 = *addr & ~SBSDIO_SB_OFT_ADDR_MASK;
	int err = 0;

	if (bar0 != sdiodev->sbwad) {
		err = brcmf_sdcard_set_sbaddr_window(sdiodev, bar0);
		if (err)
			return err;

		sdiodev->sbwad = bar0;
	}

	*addr &= SBSDIO_SB_OFT_ADDR_MASK;

	if (width == 4)
		*addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	return 0;
}

int
brcmf_sdio_regrw_helper(struct brcmf_sdio_dev *sdiodev, u32 addr,
			void *data, bool write)
{
	u8 func_num, reg_size;
	s32 retry = 0;
	int ret;

	/*
	 * figure out how to read the register based on address range
	 * 0x00 ~ 0x7FF: function 0 CCCR and FBR
	 * 0x10000 ~ 0x1FFFF: function 1 miscellaneous registers
	 * The rest: function 1 silicon backplane core registers
	 */
	if ((addr & ~REG_F0_REG_MASK) == 0) {
		func_num = SDIO_FUNC_0;
		reg_size = 1;
	} else if ((addr & ~REG_F1_MISC_MASK) == 0) {
		func_num = SDIO_FUNC_1;
		reg_size = 1;
	} else {
		func_num = SDIO_FUNC_1;
		reg_size = 4;

		ret = brcmf_sdio_addrprep(sdiodev, reg_size, &addr);
		if (ret)
			goto done;
	}

	do {
		if (!write)
			memset(data, 0, reg_size);
		if (retry)	/* wait for 1 ms till bus get settled down */
			usleep_range(1000, 2000);
		if (reg_size == 1)
			ret = brcmf_sdioh_request_byte(sdiodev, write,
						       func_num, addr, data);
		else
			ret = brcmf_sdioh_request_word(sdiodev, write,
						       func_num, addr, data, 4);
	} while (ret != 0 && retry++ < SDIOH_API_ACCESS_RETRY_LIMIT);

done:
	if (ret != 0)
		brcmf_err("failed with %d\n", ret);

	return ret;
}

u8 brcmf_sdio_regrb(struct brcmf_sdio_dev *sdiodev, u32 addr, int *ret)
{
	u8 data;
	int retval;

	brcmf_dbg(SDIO, "addr:0x%08x\n", addr);
	retval = brcmf_sdio_regrw_helper(sdiodev, addr, &data, false);
	brcmf_dbg(SDIO, "data:0x%02x\n", data);

	if (ret)
		*ret = retval;

	return data;
}

u32 brcmf_sdio_regrl(struct brcmf_sdio_dev *sdiodev, u32 addr, int *ret)
{
	u32 data;
	int retval;

	brcmf_dbg(SDIO, "addr:0x%08x\n", addr);
	retval = brcmf_sdio_regrw_helper(sdiodev, addr, &data, false);
	brcmf_dbg(SDIO, "data:0x%08x\n", data);

	if (ret)
		*ret = retval;

	return data;
}

void brcmf_sdio_regwb(struct brcmf_sdio_dev *sdiodev, u32 addr,
		      u8 data, int *ret)
{
	int retval;

	brcmf_dbg(SDIO, "addr:0x%08x, data:0x%02x\n", addr, data);
	retval = brcmf_sdio_regrw_helper(sdiodev, addr, &data, true);

	if (ret)
		*ret = retval;
}

void brcmf_sdio_regwl(struct brcmf_sdio_dev *sdiodev, u32 addr,
		      u32 data, int *ret)
{
	int retval;

	brcmf_dbg(SDIO, "addr:0x%08x, data:0x%08x\n", addr, data);
	retval = brcmf_sdio_regrw_helper(sdiodev, addr, &data, true);

	if (ret)
		*ret = retval;
}

static int brcmf_sdio_buffrw(struct brcmf_sdio_dev *sdiodev, uint fn,
			     bool write, u32 addr, struct sk_buff *pkt)
{
	unsigned int req_sz;

	brcmf_pm_resume_wait(sdiodev, &sdiodev->request_buffer_wait);
	if (brcmf_pm_resume_error(sdiodev))
		return -EIO;

	/* Single skb use the standard mmc interface */
	req_sz = pkt->len + 3;
	req_sz &= (uint)~3;

	if (write)
		return sdio_memcpy_toio(sdiodev->func[fn], addr,
					((u8 *)(pkt->data)),
					req_sz);
	else if (fn == 1)
		return sdio_memcpy_fromio(sdiodev->func[fn],
					  ((u8 *)(pkt->data)),
					  addr, req_sz);
	else
		/* function 2 read is FIFO operation */
		return sdio_readsb(sdiodev->func[fn],
				   ((u8 *)(pkt->data)), addr,
				   req_sz);
}

/**
 * brcmf_sdio_sglist_rw - SDIO interface function for block data access
 * @sdiodev: brcmfmac sdio device
 * @fn: SDIO function number
 * @write: direction flag
 * @addr: dongle memory address as source/destination
 * @pkt: skb pointer
 *
 * This function takes the respbonsibility as the interface function to MMC
 * stack for block data access. It assumes that the skb passed down by the
 * caller has already been padded and aligned.
 */
static int brcmf_sdio_sglist_rw(struct brcmf_sdio_dev *sdiodev, uint fn,
				bool write, u32 addr,
				struct sk_buff_head *pktlist)
{
	unsigned int req_sz, func_blk_sz, sg_cnt, sg_data_sz, pkt_offset;
	unsigned int max_req_sz, orig_offset, dst_offset;
	unsigned short max_seg_cnt, seg_sz;
	unsigned char *pkt_data, *orig_data, *dst_data;
	struct sk_buff *pkt_next = NULL, *local_pkt_next;
	struct sk_buff_head local_list, *target_list;
	struct mmc_request mmc_req;
	struct mmc_command mmc_cmd;
	struct mmc_data mmc_dat;
	struct sg_table st;
	struct scatterlist *sgl;
	int ret = 0;

	if (!pktlist->qlen)
		return -EINVAL;

	brcmf_pm_resume_wait(sdiodev, &sdiodev->request_buffer_wait);
	if (brcmf_pm_resume_error(sdiodev))
		return -EIO;

	target_list = pktlist;
	/* for host with broken sg support, prepare a page aligned list */
	__skb_queue_head_init(&local_list);
	if (sdiodev->pdata && sdiodev->pdata->broken_sg_support && !write) {
		req_sz = 0;
		skb_queue_walk(pktlist, pkt_next)
			req_sz += pkt_next->len;
		req_sz = ALIGN(req_sz, sdiodev->func[fn]->cur_blksize);
		while (req_sz > PAGE_SIZE) {
			pkt_next = brcmu_pkt_buf_get_skb(PAGE_SIZE);
			if (pkt_next == NULL) {
				ret = -ENOMEM;
				goto exit;
			}
			__skb_queue_tail(&local_list, pkt_next);
			req_sz -= PAGE_SIZE;
		}
		pkt_next = brcmu_pkt_buf_get_skb(req_sz);
		if (pkt_next == NULL) {
			ret = -ENOMEM;
			goto exit;
		}
		__skb_queue_tail(&local_list, pkt_next);
		target_list = &local_list;
	}

	func_blk_sz = sdiodev->func[fn]->cur_blksize;
	max_req_sz = sdiodev->max_request_size;
	max_seg_cnt = min_t(unsigned short, sdiodev->max_segment_count,
			    target_list->qlen);
	seg_sz = target_list->qlen;
	pkt_offset = 0;
	pkt_next = target_list->next;

	if (sg_alloc_table(&st, max_seg_cnt, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto exit;
	}

	memset(&mmc_req, 0, sizeof(struct mmc_request));
	memset(&mmc_cmd, 0, sizeof(struct mmc_command));
	memset(&mmc_dat, 0, sizeof(struct mmc_data));

	mmc_dat.sg = st.sgl;
	mmc_dat.blksz = func_blk_sz;
	mmc_dat.flags = write ? MMC_DATA_WRITE : MMC_DATA_READ;
	mmc_cmd.opcode = SD_IO_RW_EXTENDED;
	mmc_cmd.arg = write ? 1<<31 : 0;	/* write flag  */
	mmc_cmd.arg |= (fn & 0x7) << 28;	/* SDIO func num */
	mmc_cmd.arg |= 1<<27;			/* block mode */
	/* for function 1 the addr will be incremented */
	mmc_cmd.arg |= (fn == 1) ? 1<<26 : 0;
	mmc_cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_ADTC;
	mmc_req.cmd = &mmc_cmd;
	mmc_req.data = &mmc_dat;

	while (seg_sz) {
		req_sz = 0;
		sg_cnt = 0;
		sgl = st.sgl;
		/* prep sg table */
		while (pkt_next != (struct sk_buff *)target_list) {
			pkt_data = pkt_next->data + pkt_offset;
			sg_data_sz = pkt_next->len - pkt_offset;
			if (sg_data_sz > sdiodev->max_segment_size)
				sg_data_sz = sdiodev->max_segment_size;
			if (sg_data_sz > max_req_sz - req_sz)
				sg_data_sz = max_req_sz - req_sz;

			sg_set_buf(sgl, pkt_data, sg_data_sz);

			sg_cnt++;
			sgl = sg_next(sgl);
			req_sz += sg_data_sz;
			pkt_offset += sg_data_sz;
			if (pkt_offset == pkt_next->len) {
				pkt_offset = 0;
				pkt_next = pkt_next->next;
			}

			if (req_sz >= max_req_sz || sg_cnt >= max_seg_cnt)
				break;
		}
		seg_sz -= sg_cnt;

		if (req_sz % func_blk_sz != 0) {
			brcmf_err("sg request length %u is not %u aligned\n",
				  req_sz, func_blk_sz);
			ret = -ENOTBLK;
			goto exit;
		}

		mmc_dat.sg_len = sg_cnt;
		mmc_dat.blocks = req_sz / func_blk_sz;
		mmc_cmd.arg |= (addr & 0x1FFFF) << 9;	/* address */
		mmc_cmd.arg |= mmc_dat.blocks & 0x1FF;	/* block count */
		/* incrementing addr for function 1 */
		if (fn == 1)
			addr += req_sz;

		mmc_set_data_timeout(&mmc_dat, sdiodev->func[fn]->card);
		mmc_wait_for_req(sdiodev->func[fn]->card->host, &mmc_req);

		ret = mmc_cmd.error ? mmc_cmd.error : mmc_dat.error;
		if (ret != 0) {
			brcmf_err("CMD53 sg block %s failed %d\n",
				  write ? "write" : "read", ret);
			ret = -EIO;
			break;
		}
	}

	if (sdiodev->pdata && sdiodev->pdata->broken_sg_support && !write) {
		local_pkt_next = local_list.next;
		orig_offset = 0;
		skb_queue_walk(pktlist, pkt_next) {
			dst_offset = 0;
			do {
				req_sz = local_pkt_next->len - orig_offset;
				req_sz = min_t(uint, pkt_next->len - dst_offset,
					       req_sz);
				orig_data = local_pkt_next->data + orig_offset;
				dst_data = pkt_next->data + dst_offset;
				memcpy(dst_data, orig_data, req_sz);
				orig_offset += req_sz;
				dst_offset += req_sz;
				if (orig_offset == local_pkt_next->len) {
					orig_offset = 0;
					local_pkt_next = local_pkt_next->next;
				}
				if (dst_offset == pkt_next->len)
					break;
			} while (!skb_queue_empty(&local_list));
		}
	}

exit:
	sg_free_table(&st);
	while ((pkt_next = __skb_dequeue(&local_list)) != NULL)
		brcmu_pkt_buf_free_skb(pkt_next);

	return ret;
}

int
brcmf_sdcard_recv_buf(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags, u8 *buf, uint nbytes)
{
	struct sk_buff *mypkt;
	int err;

	mypkt = brcmu_pkt_buf_get_skb(nbytes);
	if (!mypkt) {
		brcmf_err("brcmu_pkt_buf_get_skb failed: len %d\n",
			  nbytes);
		return -EIO;
	}

	err = brcmf_sdcard_recv_pkt(sdiodev, addr, fn, flags, mypkt);
	if (!err)
		memcpy(buf, mypkt->data, nbytes);

	brcmu_pkt_buf_free_skb(mypkt);
	return err;
}

int
brcmf_sdcard_recv_pkt(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags, struct sk_buff *pkt)
{
	uint width;
	int err = 0;

	brcmf_dbg(SDIO, "fun = %d, addr = 0x%x, size = %d\n",
		  fn, addr, pkt->len);

	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	err = brcmf_sdio_addrprep(sdiodev, width, &addr);
	if (err)
		goto done;

	err = brcmf_sdio_buffrw(sdiodev, fn, false, addr, pkt);

done:
	return err;
}

int brcmf_sdcard_recv_chain(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
			    uint flags, struct sk_buff_head *pktq, uint totlen)
{
	struct sk_buff *glom_skb;
	struct sk_buff *skb;
	uint width;
	int err = 0;

	brcmf_dbg(SDIO, "fun = %d, addr = 0x%x, size = %d\n",
		  fn, addr, pktq->qlen);

	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	err = brcmf_sdio_addrprep(sdiodev, width, &addr);
	if (err)
		goto done;

	if (pktq->qlen == 1)
		err = brcmf_sdio_buffrw(sdiodev, fn, false, addr, pktq->next);
	else if (!sdiodev->sg_support) {
		glom_skb = brcmu_pkt_buf_get_skb(totlen);
		if (!glom_skb)
			return -ENOMEM;
		err = brcmf_sdio_buffrw(sdiodev, fn, false, addr, glom_skb);
		if (err)
			goto done;

		skb_queue_walk(pktq, skb) {
			memcpy(skb->data, glom_skb->data, skb->len);
			skb_pull(glom_skb, skb->len);
		}
	} else
		err = brcmf_sdio_sglist_rw(sdiodev, fn, false, addr, pktq);

done:
	return err;
}

int
brcmf_sdcard_send_buf(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags, u8 *buf, uint nbytes)
{
	struct sk_buff *mypkt;
	uint width;
	int err;

	mypkt = brcmu_pkt_buf_get_skb(nbytes);
	if (!mypkt) {
		brcmf_err("brcmu_pkt_buf_get_skb failed: len %d\n",
			  nbytes);
		return -EIO;
	}

	memcpy(mypkt->data, buf, nbytes);

	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	err = brcmf_sdio_addrprep(sdiodev, width, &addr);

	if (!err)
		err = brcmf_sdio_buffrw(sdiodev, fn, true, addr, mypkt);

	brcmu_pkt_buf_free_skb(mypkt);
	return err;

}

int
brcmf_sdcard_send_pkt(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags, struct sk_buff_head *pktq)
{
	struct sk_buff *skb;
	uint width;
	int err;

	brcmf_dbg(SDIO, "fun = %d, addr = 0x%x, size = %d\n",
		  fn, addr, pktq->qlen);

	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	err = brcmf_sdio_addrprep(sdiodev, width, &addr);
	if (err)
		return err;

	if (pktq->qlen == 1 || !sdiodev->sg_support)
		skb_queue_walk(pktq, skb) {
			err = brcmf_sdio_buffrw(sdiodev, fn, true, addr, skb);
			if (err)
				break;
		}
	else
		err = brcmf_sdio_sglist_rw(sdiodev, fn, true, addr, pktq);

	return err;
}

int
brcmf_sdio_ramrw(struct brcmf_sdio_dev *sdiodev, bool write, u32 address,
		 u8 *data, uint size)
{
	int bcmerror = 0;
	struct sk_buff *pkt;
	u32 sdaddr;
	uint dsize;

	dsize = min_t(uint, SBSDIO_SB_OFT_ADDR_LIMIT, size);
	pkt = dev_alloc_skb(dsize);
	if (!pkt) {
		brcmf_err("dev_alloc_skb failed: len %d\n", dsize);
		return -EIO;
	}
	pkt->priority = 0;

	/* Determine initial transfer parameters */
	sdaddr = address & SBSDIO_SB_OFT_ADDR_MASK;
	if ((sdaddr + size) & SBSDIO_SBWINDOW_MASK)
		dsize = (SBSDIO_SB_OFT_ADDR_LIMIT - sdaddr);
	else
		dsize = size;

	sdio_claim_host(sdiodev->func[1]);

	/* Do the transfer(s) */
	while (size) {
		/* Set the backplane window to include the start address */
		bcmerror = brcmf_sdcard_set_sbaddr_window(sdiodev, address);
		if (bcmerror)
			break;

		brcmf_dbg(SDIO, "%s %d bytes at offset 0x%08x in window 0x%08x\n",
			  write ? "write" : "read", dsize,
			  sdaddr, address & SBSDIO_SBWINDOW_MASK);

		sdaddr &= SBSDIO_SB_OFT_ADDR_MASK;
		sdaddr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

		skb_put(pkt, dsize);
		if (write)
			memcpy(pkt->data, data, dsize);
		bcmerror = brcmf_sdio_buffrw(sdiodev, SDIO_FUNC_1, write,
					     sdaddr, pkt);
		if (bcmerror) {
			brcmf_err("membytes transfer failed\n");
			break;
		}
		if (!write)
			memcpy(data, pkt->data, dsize);
		skb_trim(pkt, dsize);

		/* Adjust for next transfer (if any) */
		size -= dsize;
		if (size) {
			data += dsize;
			address += dsize;
			sdaddr = 0;
			dsize = min_t(uint, SBSDIO_SB_OFT_ADDR_LIMIT, size);
		}
	}

	dev_kfree_skb(pkt);

	/* Return the window to backplane enumeration space for core access */
	if (brcmf_sdcard_set_sbaddr_window(sdiodev, sdiodev->sbwad))
		brcmf_err("FAILED to set window back to 0x%x\n",
			  sdiodev->sbwad);

	sdio_release_host(sdiodev->func[1]);

	return bcmerror;
}

int brcmf_sdcard_abort(struct brcmf_sdio_dev *sdiodev, uint fn)
{
	char t_func = (char)fn;
	brcmf_dbg(SDIO, "Enter\n");

	/* issue abort cmd52 command through F0 */
	brcmf_sdioh_request_byte(sdiodev, SDIOH_WRITE, SDIO_FUNC_0,
				 SDIO_CCCR_ABORT, &t_func);

	brcmf_dbg(SDIO, "Exit\n");
	return 0;
}

int brcmf_sdio_probe(struct brcmf_sdio_dev *sdiodev)
{
	u32 regs = 0;
	int ret = 0;

	ret = brcmf_sdioh_attach(sdiodev);
	if (ret)
		goto out;

	regs = SI_ENUM_BASE;

	/* try to attach to the target device */
	sdiodev->bus = brcmf_sdbrcm_probe(regs, sdiodev);
	if (!sdiodev->bus) {
		brcmf_err("device attach failed\n");
		ret = -ENODEV;
		goto out;
	}

out:
	if (ret)
		brcmf_sdio_remove(sdiodev);

	return ret;
}
EXPORT_SYMBOL(brcmf_sdio_probe);

int brcmf_sdio_remove(struct brcmf_sdio_dev *sdiodev)
{
	sdiodev->bus_if->state = BRCMF_BUS_DOWN;

	if (sdiodev->bus) {
		brcmf_sdbrcm_disconnect(sdiodev->bus);
		sdiodev->bus = NULL;
	}

	brcmf_sdioh_detach(sdiodev);

	sdiodev->sbwad = 0;

	return 0;
}
EXPORT_SYMBOL(brcmf_sdio_remove);

void brcmf_sdio_wdtmr_enable(struct brcmf_sdio_dev *sdiodev, bool enable)
{
	if (enable)
		brcmf_sdbrcm_wd_timer(sdiodev->bus, BRCMF_WD_POLL_MS);
	else
		brcmf_sdbrcm_wd_timer(sdiodev->bus, 0);
}
