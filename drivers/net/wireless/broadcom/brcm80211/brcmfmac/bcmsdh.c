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
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/scatterlist.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/core.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/pm_runtime.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <net/cfg80211.h>

#include <defs.h>
#include <brcm_hw_ids.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include <chipcommon.h>
#include <soc.h>
#include "chip.h"
#include "bus.h"
#include "debug.h"
#include "sdio.h"
#include "core.h"
#include "common.h"

#define SDIOH_API_ACCESS_RETRY_LIMIT	2

#define DMA_ALIGN_MASK	0x03

#define SDIO_FUNC1_BLOCKSIZE		64
#define SDIO_FUNC2_BLOCKSIZE		512
/* Maximum milliseconds to wait for F2 to come up */
#define SDIO_WAIT_F2RDY	3000

#define BRCMF_DEFAULT_RXGLOM_SIZE	32  /* max rx frames in glom chain */

struct brcmf_sdiod_freezer {
	atomic_t freezing;
	atomic_t thread_count;
	u32 frozen_count;
	wait_queue_head_t thread_freeze;
	struct completion resumed;
};

static irqreturn_t brcmf_sdiod_oob_irqhandler(int irq, void *dev_id)
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

	brcmf_sdio_isr(sdiodev->bus);

	return IRQ_HANDLED;
}

static void brcmf_sdiod_ib_irqhandler(struct sdio_func *func)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(&func->dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;

	brcmf_dbg(INTR, "IB intr triggered\n");

	brcmf_sdio_isr(sdiodev->bus);
}

/* dummy handler for SDIO function 2 interrupt */
static void brcmf_sdiod_dummy_irqhandler(struct sdio_func *func)
{
}

int brcmf_sdiod_intr_register(struct brcmf_sdio_dev *sdiodev)
{
	struct brcmfmac_sdio_pd *pdata;
	int ret = 0;
	u8 data;
	u32 addr, gpiocontrol;

	pdata = &sdiodev->settings->bus.sdio;
	if (pdata->oob_irq_supported) {
		brcmf_dbg(SDIO, "Enter, register OOB IRQ %d\n",
			  pdata->oob_irq_nr);
		spin_lock_init(&sdiodev->irq_en_lock);
		sdiodev->irq_en = true;

		ret = request_irq(pdata->oob_irq_nr, brcmf_sdiod_oob_irqhandler,
				  pdata->oob_irq_flags, "brcmf_oob_intr",
				  &sdiodev->func1->dev);
		if (ret != 0) {
			brcmf_err("request_irq failed %d\n", ret);
			return ret;
		}
		sdiodev->oob_irq_requested = true;

		ret = enable_irq_wake(pdata->oob_irq_nr);
		if (ret != 0) {
			brcmf_err("enable_irq_wake failed %d\n", ret);
			return ret;
		}
		sdiodev->irq_wake = true;

		sdio_claim_host(sdiodev->func1);

		if (sdiodev->bus_if->chip == BRCM_CC_43362_CHIP_ID) {
			/* assign GPIO to SDIO core */
			addr = CORE_CC_REG(SI_ENUM_BASE, gpiocontrol);
			gpiocontrol = brcmf_sdiod_readl(sdiodev, addr, &ret);
			gpiocontrol |= 0x2;
			brcmf_sdiod_writel(sdiodev, addr, gpiocontrol, &ret);

			brcmf_sdiod_writeb(sdiodev, SBSDIO_GPIO_SELECT,
					   0xf, &ret);
			brcmf_sdiod_writeb(sdiodev, SBSDIO_GPIO_OUT, 0, &ret);
			brcmf_sdiod_writeb(sdiodev, SBSDIO_GPIO_EN, 0x2, &ret);
		}

		/* must configure SDIO_CCCR_IENx to enable irq */
		data = brcmf_sdiod_func0_rb(sdiodev, SDIO_CCCR_IENx, &ret);
		data |= SDIO_CCCR_IEN_FUNC1 | SDIO_CCCR_IEN_FUNC2 |
			SDIO_CCCR_IEN_FUNC0;
		brcmf_sdiod_func0_wb(sdiodev, SDIO_CCCR_IENx, data, &ret);

		/* redirect, configure and enable io for interrupt signal */
		data = SDIO_CCCR_BRCM_SEPINT_MASK | SDIO_CCCR_BRCM_SEPINT_OE;
		if (pdata->oob_irq_flags & IRQF_TRIGGER_HIGH)
			data |= SDIO_CCCR_BRCM_SEPINT_ACT_HI;
		brcmf_sdiod_func0_wb(sdiodev, SDIO_CCCR_BRCM_SEPINT,
				     data, &ret);
		sdio_release_host(sdiodev->func1);
	} else {
		brcmf_dbg(SDIO, "Entering\n");
		sdio_claim_host(sdiodev->func1);
		sdio_claim_irq(sdiodev->func1, brcmf_sdiod_ib_irqhandler);
		sdio_claim_irq(sdiodev->func2, brcmf_sdiod_dummy_irqhandler);
		sdio_release_host(sdiodev->func1);
		sdiodev->sd_irq_requested = true;
	}

	return 0;
}

void brcmf_sdiod_intr_unregister(struct brcmf_sdio_dev *sdiodev)
{

	brcmf_dbg(SDIO, "Entering oob=%d sd=%d\n",
		  sdiodev->oob_irq_requested,
		  sdiodev->sd_irq_requested);

	if (sdiodev->oob_irq_requested) {
		struct brcmfmac_sdio_pd *pdata;

		pdata = &sdiodev->settings->bus.sdio;
		sdio_claim_host(sdiodev->func1);
		brcmf_sdiod_func0_wb(sdiodev, SDIO_CCCR_BRCM_SEPINT, 0, NULL);
		brcmf_sdiod_func0_wb(sdiodev, SDIO_CCCR_IENx, 0, NULL);
		sdio_release_host(sdiodev->func1);

		sdiodev->oob_irq_requested = false;
		if (sdiodev->irq_wake) {
			disable_irq_wake(pdata->oob_irq_nr);
			sdiodev->irq_wake = false;
		}
		free_irq(pdata->oob_irq_nr, &sdiodev->func1->dev);
		sdiodev->irq_en = false;
		sdiodev->oob_irq_requested = false;
	}

	if (sdiodev->sd_irq_requested) {
		sdio_claim_host(sdiodev->func1);
		sdio_release_irq(sdiodev->func2);
		sdio_release_irq(sdiodev->func1);
		sdio_release_host(sdiodev->func1);
		sdiodev->sd_irq_requested = false;
	}
}

void brcmf_sdiod_change_state(struct brcmf_sdio_dev *sdiodev,
			      enum brcmf_sdiod_state state)
{
	if (sdiodev->state == BRCMF_SDIOD_NOMEDIUM ||
	    state == sdiodev->state)
		return;

	brcmf_dbg(TRACE, "%d -> %d\n", sdiodev->state, state);
	switch (sdiodev->state) {
	case BRCMF_SDIOD_DATA:
		/* any other state means bus interface is down */
		brcmf_bus_change_state(sdiodev->bus_if, BRCMF_BUS_DOWN);
		break;
	case BRCMF_SDIOD_DOWN:
		/* transition from DOWN to DATA means bus interface is up */
		if (state == BRCMF_SDIOD_DATA)
			brcmf_bus_change_state(sdiodev->bus_if, BRCMF_BUS_UP);
		break;
	default:
		break;
	}
	sdiodev->state = state;
}

static int brcmf_sdiod_set_backplane_window(struct brcmf_sdio_dev *sdiodev,
					    u32 addr)
{
	u32 v, bar0 = addr & SBSDIO_SBWINDOW_MASK;
	int err = 0, i;

	if (bar0 == sdiodev->sbwad)
		return 0;

	v = bar0 >> 8;

	for (i = 0 ; i < 3 && !err ; i++, v >>= 8)
		brcmf_sdiod_writeb(sdiodev, SBSDIO_FUNC1_SBADDRLOW + i,
				   v & 0xff, &err);

	if (!err)
		sdiodev->sbwad = bar0;

	return err;
}

u32 brcmf_sdiod_readl(struct brcmf_sdio_dev *sdiodev, u32 addr, int *ret)
{
	u32 data = 0;
	int retval;

	retval = brcmf_sdiod_set_backplane_window(sdiodev, addr);
	if (retval)
		goto out;

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	data = sdio_readl(sdiodev->func1, addr, &retval);

out:
	if (ret)
		*ret = retval;

	return data;
}

void brcmf_sdiod_writel(struct brcmf_sdio_dev *sdiodev, u32 addr,
			u32 data, int *ret)
{
	int retval;

	retval = brcmf_sdiod_set_backplane_window(sdiodev, addr);
	if (retval)
		goto out;

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	sdio_writel(sdiodev->func1, data, addr, &retval);

out:
	if (ret)
		*ret = retval;
}

static int brcmf_sdiod_skbuff_read(struct brcmf_sdio_dev *sdiodev,
				   struct sdio_func *func, u32 addr,
				   struct sk_buff *skb)
{
	unsigned int req_sz;
	int err;

	/* Single skb use the standard mmc interface */
	req_sz = skb->len + 3;
	req_sz &= (uint)~3;

	switch (func->num) {
	case 1:
		err = sdio_memcpy_fromio(func, ((u8 *)(skb->data)), addr,
					 req_sz);
		break;
	case 2:
		err = sdio_readsb(func, ((u8 *)(skb->data)), addr, req_sz);
		break;
	default:
		/* bail out as things are really fishy here */
		WARN(1, "invalid sdio function number: %d\n", func->num);
		err = -ENOMEDIUM;
	};

	if (err == -ENOMEDIUM)
		brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);

	return err;
}

static int brcmf_sdiod_skbuff_write(struct brcmf_sdio_dev *sdiodev,
				    struct sdio_func *func, u32 addr,
				    struct sk_buff *skb)
{
	unsigned int req_sz;
	int err;

	/* Single skb use the standard mmc interface */
	req_sz = skb->len + 3;
	req_sz &= (uint)~3;

	err = sdio_memcpy_toio(func, addr, ((u8 *)(skb->data)), req_sz);

	if (err == -ENOMEDIUM)
		brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);

	return err;
}

/**
 * brcmf_sdiod_sglist_rw - SDIO interface function for block data access
 * @sdiodev: brcmfmac sdio device
 * @func: SDIO function
 * @write: direction flag
 * @addr: dongle memory address as source/destination
 * @pkt: skb pointer
 *
 * This function takes the respbonsibility as the interface function to MMC
 * stack for block data access. It assumes that the skb passed down by the
 * caller has already been padded and aligned.
 */
static int brcmf_sdiod_sglist_rw(struct brcmf_sdio_dev *sdiodev,
				 struct sdio_func *func,
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
	struct scatterlist *sgl;
	int ret = 0;

	if (!pktlist->qlen)
		return -EINVAL;

	target_list = pktlist;
	/* for host with broken sg support, prepare a page aligned list */
	__skb_queue_head_init(&local_list);
	if (!write && sdiodev->settings->bus.sdio.broken_sg_support) {
		req_sz = 0;
		skb_queue_walk(pktlist, pkt_next)
			req_sz += pkt_next->len;
		req_sz = ALIGN(req_sz, func->cur_blksize);
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

	func_blk_sz = func->cur_blksize;
	max_req_sz = sdiodev->max_request_size;
	max_seg_cnt = min_t(unsigned short, sdiodev->max_segment_count,
			    target_list->qlen);
	seg_sz = target_list->qlen;
	pkt_offset = 0;
	pkt_next = target_list->next;

	memset(&mmc_req, 0, sizeof(struct mmc_request));
	memset(&mmc_cmd, 0, sizeof(struct mmc_command));
	memset(&mmc_dat, 0, sizeof(struct mmc_data));

	mmc_dat.sg = sdiodev->sgtable.sgl;
	mmc_dat.blksz = func_blk_sz;
	mmc_dat.flags = write ? MMC_DATA_WRITE : MMC_DATA_READ;
	mmc_cmd.opcode = SD_IO_RW_EXTENDED;
	mmc_cmd.arg = write ? 1<<31 : 0;	/* write flag  */
	mmc_cmd.arg |= (func->num & 0x7) << 28;	/* SDIO func num */
	mmc_cmd.arg |= 1 << 27;			/* block mode */
	/* for function 1 the addr will be incremented */
	mmc_cmd.arg |= (func->num == 1) ? 1 << 26 : 0;
	mmc_cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_ADTC;
	mmc_req.cmd = &mmc_cmd;
	mmc_req.data = &mmc_dat;

	while (seg_sz) {
		req_sz = 0;
		sg_cnt = 0;
		sgl = sdiodev->sgtable.sgl;
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
		if (func->num == 1)
			addr += req_sz;

		mmc_set_data_timeout(&mmc_dat, func->card);
		mmc_wait_for_req(func->card->host, &mmc_req);

		ret = mmc_cmd.error ? mmc_cmd.error : mmc_dat.error;
		if (ret == -ENOMEDIUM) {
			brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);
			break;
		} else if (ret != 0) {
			brcmf_err("CMD53 sg block %s failed %d\n",
				  write ? "write" : "read", ret);
			ret = -EIO;
			break;
		}
	}

	if (!write && sdiodev->settings->bus.sdio.broken_sg_support) {
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
	sg_init_table(sdiodev->sgtable.sgl, sdiodev->sgtable.orig_nents);
	while ((pkt_next = __skb_dequeue(&local_list)) != NULL)
		brcmu_pkt_buf_free_skb(pkt_next);

	return ret;
}

int brcmf_sdiod_recv_buf(struct brcmf_sdio_dev *sdiodev, u8 *buf, uint nbytes)
{
	struct sk_buff *mypkt;
	int err;

	mypkt = brcmu_pkt_buf_get_skb(nbytes);
	if (!mypkt) {
		brcmf_err("brcmu_pkt_buf_get_skb failed: len %d\n",
			  nbytes);
		return -EIO;
	}

	err = brcmf_sdiod_recv_pkt(sdiodev, mypkt);
	if (!err)
		memcpy(buf, mypkt->data, nbytes);

	brcmu_pkt_buf_free_skb(mypkt);
	return err;
}

int brcmf_sdiod_recv_pkt(struct brcmf_sdio_dev *sdiodev, struct sk_buff *pkt)
{
	u32 addr = sdiodev->cc_core->base;
	int err = 0;

	brcmf_dbg(SDIO, "addr = 0x%x, size = %d\n", addr, pkt->len);

	err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
	if (err)
		goto done;

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	err = brcmf_sdiod_skbuff_read(sdiodev, sdiodev->func2, addr, pkt);

done:
	return err;
}

int brcmf_sdiod_recv_chain(struct brcmf_sdio_dev *sdiodev,
			   struct sk_buff_head *pktq, uint totlen)
{
	struct sk_buff *glom_skb = NULL;
	struct sk_buff *skb;
	u32 addr = sdiodev->cc_core->base;
	int err = 0;

	brcmf_dbg(SDIO, "addr = 0x%x, size = %d\n",
		  addr, pktq->qlen);

	err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
	if (err)
		goto done;

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	if (pktq->qlen == 1)
		err = brcmf_sdiod_skbuff_read(sdiodev, sdiodev->func2, addr,
					      __skb_peek(pktq));
	else if (!sdiodev->sg_support) {
		glom_skb = brcmu_pkt_buf_get_skb(totlen);
		if (!glom_skb)
			return -ENOMEM;
		err = brcmf_sdiod_skbuff_read(sdiodev, sdiodev->func2, addr,
					      glom_skb);
		if (err)
			goto done;

		skb_queue_walk(pktq, skb) {
			memcpy(skb->data, glom_skb->data, skb->len);
			skb_pull(glom_skb, skb->len);
		}
	} else
		err = brcmf_sdiod_sglist_rw(sdiodev, sdiodev->func2, false,
					    addr, pktq);

done:
	brcmu_pkt_buf_free_skb(glom_skb);
	return err;
}

int brcmf_sdiod_send_buf(struct brcmf_sdio_dev *sdiodev, u8 *buf, uint nbytes)
{
	struct sk_buff *mypkt;
	u32 addr = sdiodev->cc_core->base;
	int err;

	mypkt = brcmu_pkt_buf_get_skb(nbytes);

	if (!mypkt) {
		brcmf_err("brcmu_pkt_buf_get_skb failed: len %d\n",
			  nbytes);
		return -EIO;
	}

	memcpy(mypkt->data, buf, nbytes);

	err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
	if (err)
		return err;

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	if (!err)
		err = brcmf_sdiod_skbuff_write(sdiodev, sdiodev->func2, addr,
					       mypkt);

	brcmu_pkt_buf_free_skb(mypkt);

	return err;
}

int brcmf_sdiod_send_pkt(struct brcmf_sdio_dev *sdiodev,
			 struct sk_buff_head *pktq)
{
	struct sk_buff *skb;
	u32 addr = sdiodev->cc_core->base;
	int err;

	brcmf_dbg(SDIO, "addr = 0x%x, size = %d\n", addr, pktq->qlen);

	err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
	if (err)
		return err;

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	if (pktq->qlen == 1 || !sdiodev->sg_support) {
		skb_queue_walk(pktq, skb) {
			err = brcmf_sdiod_skbuff_write(sdiodev, sdiodev->func2,
						       addr, skb);
			if (err)
				break;
		}
	} else {
		err = brcmf_sdiod_sglist_rw(sdiodev, sdiodev->func2, true,
					    addr, pktq);
	}

	return err;
}

int
brcmf_sdiod_ramrw(struct brcmf_sdio_dev *sdiodev, bool write, u32 address,
		  u8 *data, uint size)
{
	int err = 0;
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

	sdio_claim_host(sdiodev->func1);

	/* Do the transfer(s) */
	while (size) {
		/* Set the backplane window to include the start address */
		err = brcmf_sdiod_set_backplane_window(sdiodev, address);
		if (err)
			break;

		brcmf_dbg(SDIO, "%s %d bytes at offset 0x%08x in window 0x%08x\n",
			  write ? "write" : "read", dsize,
			  sdaddr, address & SBSDIO_SBWINDOW_MASK);

		sdaddr &= SBSDIO_SB_OFT_ADDR_MASK;
		sdaddr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

		skb_put(pkt, dsize);

		if (write) {
			memcpy(pkt->data, data, dsize);
			err = brcmf_sdiod_skbuff_write(sdiodev, sdiodev->func1,
						       sdaddr, pkt);
		} else {
			err = brcmf_sdiod_skbuff_read(sdiodev, sdiodev->func1,
						      sdaddr, pkt);
		}

		if (err) {
			brcmf_err("membytes transfer failed\n");
			break;
		}
		if (!write)
			memcpy(data, pkt->data, dsize);
		skb_trim(pkt, 0);

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

	sdio_release_host(sdiodev->func1);

	return err;
}

int brcmf_sdiod_abort(struct brcmf_sdio_dev *sdiodev, struct sdio_func *func)
{
	brcmf_dbg(SDIO, "Enter\n");

	/* Issue abort cmd52 command through F0 */
	brcmf_sdiod_func0_wb(sdiodev, SDIO_CCCR_ABORT, func->num, NULL);

	brcmf_dbg(SDIO, "Exit\n");
	return 0;
}

void brcmf_sdiod_sgtable_alloc(struct brcmf_sdio_dev *sdiodev)
{
	struct sdio_func *func;
	struct mmc_host *host;
	uint max_blocks;
	uint nents;
	int err;

	func = sdiodev->func2;
	host = func->card->host;
	sdiodev->sg_support = host->max_segs > 1;
	max_blocks = min_t(uint, host->max_blk_count, 511u);
	sdiodev->max_request_size = min_t(uint, host->max_req_size,
					  max_blocks * func->cur_blksize);
	sdiodev->max_segment_count = min_t(uint, host->max_segs,
					   SG_MAX_SINGLE_ALLOC);
	sdiodev->max_segment_size = host->max_seg_size;

	if (!sdiodev->sg_support)
		return;

	nents = max_t(uint, BRCMF_DEFAULT_RXGLOM_SIZE,
		      sdiodev->settings->bus.sdio.txglomsz);
	nents += (nents >> 4) + 1;

	WARN_ON(nents > sdiodev->max_segment_count);

	brcmf_dbg(TRACE, "nents=%d\n", nents);
	err = sg_alloc_table(&sdiodev->sgtable, nents, GFP_KERNEL);
	if (err < 0) {
		brcmf_err("allocation failed: disable scatter-gather");
		sdiodev->sg_support = false;
	}

	sdiodev->txglomsz = sdiodev->settings->bus.sdio.txglomsz;
}

#ifdef CONFIG_PM_SLEEP
static int brcmf_sdiod_freezer_attach(struct brcmf_sdio_dev *sdiodev)
{
	sdiodev->freezer = kzalloc(sizeof(*sdiodev->freezer), GFP_KERNEL);
	if (!sdiodev->freezer)
		return -ENOMEM;
	atomic_set(&sdiodev->freezer->thread_count, 0);
	atomic_set(&sdiodev->freezer->freezing, 0);
	init_waitqueue_head(&sdiodev->freezer->thread_freeze);
	init_completion(&sdiodev->freezer->resumed);
	return 0;
}

static void brcmf_sdiod_freezer_detach(struct brcmf_sdio_dev *sdiodev)
{
	if (sdiodev->freezer) {
		WARN_ON(atomic_read(&sdiodev->freezer->freezing));
		kfree(sdiodev->freezer);
	}
}

static int brcmf_sdiod_freezer_on(struct brcmf_sdio_dev *sdiodev)
{
	atomic_t *expect = &sdiodev->freezer->thread_count;
	int res = 0;

	sdiodev->freezer->frozen_count = 0;
	reinit_completion(&sdiodev->freezer->resumed);
	atomic_set(&sdiodev->freezer->freezing, 1);
	brcmf_sdio_trigger_dpc(sdiodev->bus);
	wait_event(sdiodev->freezer->thread_freeze,
		   atomic_read(expect) == sdiodev->freezer->frozen_count);
	sdio_claim_host(sdiodev->func1);
	res = brcmf_sdio_sleep(sdiodev->bus, true);
	sdio_release_host(sdiodev->func1);
	return res;
}

static void brcmf_sdiod_freezer_off(struct brcmf_sdio_dev *sdiodev)
{
	sdio_claim_host(sdiodev->func1);
	brcmf_sdio_sleep(sdiodev->bus, false);
	sdio_release_host(sdiodev->func1);
	atomic_set(&sdiodev->freezer->freezing, 0);
	complete_all(&sdiodev->freezer->resumed);
}

bool brcmf_sdiod_freezing(struct brcmf_sdio_dev *sdiodev)
{
	return atomic_read(&sdiodev->freezer->freezing);
}

void brcmf_sdiod_try_freeze(struct brcmf_sdio_dev *sdiodev)
{
	if (!brcmf_sdiod_freezing(sdiodev))
		return;
	sdiodev->freezer->frozen_count++;
	wake_up(&sdiodev->freezer->thread_freeze);
	wait_for_completion(&sdiodev->freezer->resumed);
}

void brcmf_sdiod_freezer_count(struct brcmf_sdio_dev *sdiodev)
{
	atomic_inc(&sdiodev->freezer->thread_count);
}

void brcmf_sdiod_freezer_uncount(struct brcmf_sdio_dev *sdiodev)
{
	atomic_dec(&sdiodev->freezer->thread_count);
}
#else
static int brcmf_sdiod_freezer_attach(struct brcmf_sdio_dev *sdiodev)
{
	return 0;
}

static void brcmf_sdiod_freezer_detach(struct brcmf_sdio_dev *sdiodev)
{
}
#endif /* CONFIG_PM_SLEEP */

static int brcmf_sdiod_remove(struct brcmf_sdio_dev *sdiodev)
{
	sdiodev->state = BRCMF_SDIOD_DOWN;
	if (sdiodev->bus) {
		brcmf_sdio_remove(sdiodev->bus);
		sdiodev->bus = NULL;
	}

	brcmf_sdiod_freezer_detach(sdiodev);

	/* Disable Function 2 */
	sdio_claim_host(sdiodev->func2);
	sdio_disable_func(sdiodev->func2);
	sdio_release_host(sdiodev->func2);

	/* Disable Function 1 */
	sdio_claim_host(sdiodev->func1);
	sdio_disable_func(sdiodev->func1);
	sdio_release_host(sdiodev->func1);

	sg_free_table(&sdiodev->sgtable);
	sdiodev->sbwad = 0;

	pm_runtime_allow(sdiodev->func1->card->host->parent);
	return 0;
}

static void brcmf_sdiod_host_fixup(struct mmc_host *host)
{
	/* runtime-pm powers off the device */
	pm_runtime_forbid(host->parent);
	/* avoid removal detection upon resume */
	host->caps |= MMC_CAP_NONREMOVABLE;
}

static int brcmf_sdiod_probe(struct brcmf_sdio_dev *sdiodev)
{
	int ret = 0;

	sdio_claim_host(sdiodev->func1);

	ret = sdio_set_block_size(sdiodev->func1, SDIO_FUNC1_BLOCKSIZE);
	if (ret) {
		brcmf_err("Failed to set F1 blocksize\n");
		sdio_release_host(sdiodev->func1);
		goto out;
	}
	ret = sdio_set_block_size(sdiodev->func2, SDIO_FUNC2_BLOCKSIZE);
	if (ret) {
		brcmf_err("Failed to set F2 blocksize\n");
		sdio_release_host(sdiodev->func1);
		goto out;
	}

	/* increase F2 timeout */
	sdiodev->func2->enable_timeout = SDIO_WAIT_F2RDY;

	/* Enable Function 1 */
	ret = sdio_enable_func(sdiodev->func1);
	sdio_release_host(sdiodev->func1);
	if (ret) {
		brcmf_err("Failed to enable F1: err=%d\n", ret);
		goto out;
	}

	ret = brcmf_sdiod_freezer_attach(sdiodev);
	if (ret)
		goto out;

	/* try to attach to the target device */
	sdiodev->bus = brcmf_sdio_probe(sdiodev);
	if (!sdiodev->bus) {
		ret = -ENODEV;
		goto out;
	}
	brcmf_sdiod_host_fixup(sdiodev->func2->card->host);
out:
	if (ret)
		brcmf_sdiod_remove(sdiodev);

	return ret;
}

#define BRCMF_SDIO_DEVICE(dev_id)	\
	{SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, dev_id)}

/* devices we support, null terminated */
static const struct sdio_device_id brcmf_sdmmc_ids[] = {
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43143),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43241),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4329),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4330),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4334),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43340),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43341),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43362),
 	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43364),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4335_4339),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4339),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43430),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4345),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43455),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4354),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4356),
	BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_CYPRESS_4373),
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(sdio, brcmf_sdmmc_ids);


static void brcmf_sdiod_acpi_set_power_manageable(struct device *dev,
						  int val)
{
#if IS_ENABLED(CONFIG_ACPI)
	struct acpi_device *adev;

	adev = ACPI_COMPANION(dev);
	if (adev)
		adev->flags.power_manageable = 0;
#endif
}

static int brcmf_ops_sdio_probe(struct sdio_func *func,
				const struct sdio_device_id *id)
{
	int err;
	struct brcmf_sdio_dev *sdiodev;
	struct brcmf_bus *bus_if;
	struct device *dev;

	brcmf_dbg(SDIO, "Enter\n");
	brcmf_dbg(SDIO, "Class=%x\n", func->class);
	brcmf_dbg(SDIO, "sdio vendor ID: 0x%04x\n", func->vendor);
	brcmf_dbg(SDIO, "sdio device ID: 0x%04x\n", func->device);
	brcmf_dbg(SDIO, "Function#: %d\n", func->num);

	dev = &func->dev;

	/* Set MMC_QUIRK_LENIENT_FN0 for this card */
	func->card->quirks |= MMC_QUIRK_LENIENT_FN0;

	/* prohibit ACPI power management for this device */
	brcmf_sdiod_acpi_set_power_manageable(dev, 0);

	/* Consume func num 1 but dont do anything with it. */
	if (func->num == 1)
		return 0;

	/* Ignore anything but func 2 */
	if (func->num != 2)
		return -ENODEV;

	bus_if = kzalloc(sizeof(struct brcmf_bus), GFP_KERNEL);
	if (!bus_if)
		return -ENOMEM;
	sdiodev = kzalloc(sizeof(struct brcmf_sdio_dev), GFP_KERNEL);
	if (!sdiodev) {
		kfree(bus_if);
		return -ENOMEM;
	}

	/* store refs to functions used. mmc_card does
	 * not hold the F0 function pointer.
	 */
	sdiodev->func1 = func->card->sdio_func[0];
	sdiodev->func2 = func;

	sdiodev->bus_if = bus_if;
	bus_if->bus_priv.sdio = sdiodev;
	bus_if->proto_type = BRCMF_PROTO_BCDC;
	dev_set_drvdata(&func->dev, bus_if);
	dev_set_drvdata(&sdiodev->func1->dev, bus_if);
	sdiodev->dev = &sdiodev->func1->dev;

	brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_DOWN);

	brcmf_dbg(SDIO, "F2 found, calling brcmf_sdiod_probe...\n");
	err = brcmf_sdiod_probe(sdiodev);
	if (err) {
		brcmf_err("F2 error, probe failed %d...\n", err);
		goto fail;
	}

	brcmf_dbg(SDIO, "F2 init completed...\n");
	return 0;

fail:
	dev_set_drvdata(&func->dev, NULL);
	dev_set_drvdata(&sdiodev->func1->dev, NULL);
	kfree(sdiodev);
	kfree(bus_if);
	return err;
}

static void brcmf_ops_sdio_remove(struct sdio_func *func)
{
	struct brcmf_bus *bus_if;
	struct brcmf_sdio_dev *sdiodev;

	brcmf_dbg(SDIO, "Enter\n");
	brcmf_dbg(SDIO, "sdio vendor ID: 0x%04x\n", func->vendor);
	brcmf_dbg(SDIO, "sdio device ID: 0x%04x\n", func->device);
	brcmf_dbg(SDIO, "Function: %d\n", func->num);

	bus_if = dev_get_drvdata(&func->dev);
	if (bus_if) {
		sdiodev = bus_if->bus_priv.sdio;

		/* start by unregistering irqs */
		brcmf_sdiod_intr_unregister(sdiodev);

		if (func->num != 1)
			return;

		/* only proceed with rest of cleanup if func 1 */
		brcmf_sdiod_remove(sdiodev);

		dev_set_drvdata(&sdiodev->func1->dev, NULL);
		dev_set_drvdata(&sdiodev->func2->dev, NULL);

		kfree(bus_if);
		kfree(sdiodev);
	}

	brcmf_dbg(SDIO, "Exit\n");
}

void brcmf_sdio_wowl_config(struct device *dev, bool enabled)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;

	brcmf_dbg(SDIO, "Configuring WOWL, enabled=%d\n", enabled);
	sdiodev->wowl_enabled = enabled;
}

#ifdef CONFIG_PM_SLEEP
static int brcmf_ops_sdio_suspend(struct device *dev)
{
	struct sdio_func *func;
	struct brcmf_bus *bus_if;
	struct brcmf_sdio_dev *sdiodev;
	mmc_pm_flag_t sdio_flags;

	func = container_of(dev, struct sdio_func, dev);
	brcmf_dbg(SDIO, "Enter: F%d\n", func->num);
	if (func->num != 1)
		return 0;


	bus_if = dev_get_drvdata(dev);
	sdiodev = bus_if->bus_priv.sdio;

	brcmf_sdiod_freezer_on(sdiodev);
	brcmf_sdio_wd_timer(sdiodev->bus, 0);

	sdio_flags = MMC_PM_KEEP_POWER;
	if (sdiodev->wowl_enabled) {
		if (sdiodev->settings->bus.sdio.oob_irq_supported)
			enable_irq_wake(sdiodev->settings->bus.sdio.oob_irq_nr);
		else
			sdio_flags |= MMC_PM_WAKE_SDIO_IRQ;
	}
	if (sdio_set_host_pm_flags(sdiodev->func1, sdio_flags))
		brcmf_err("Failed to set pm_flags %x\n", sdio_flags);
	return 0;
}

static int brcmf_ops_sdio_resume(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct sdio_func *func = container_of(dev, struct sdio_func, dev);

	brcmf_dbg(SDIO, "Enter: F%d\n", func->num);
	if (func->num != 2)
		return 0;

	brcmf_sdiod_freezer_off(sdiodev);
	return 0;
}

static const struct dev_pm_ops brcmf_sdio_pm_ops = {
	.suspend	= brcmf_ops_sdio_suspend,
	.resume		= brcmf_ops_sdio_resume,
};
#endif	/* CONFIG_PM_SLEEP */

static struct sdio_driver brcmf_sdmmc_driver = {
	.probe = brcmf_ops_sdio_probe,
	.remove = brcmf_ops_sdio_remove,
	.name = KBUILD_MODNAME,
	.id_table = brcmf_sdmmc_ids,
	.drv = {
		.owner = THIS_MODULE,
#ifdef CONFIG_PM_SLEEP
		.pm = &brcmf_sdio_pm_ops,
#endif	/* CONFIG_PM_SLEEP */
		.coredump = brcmf_dev_coredump,
	},
};

void brcmf_sdio_register(void)
{
	int ret;

	ret = sdio_register_driver(&brcmf_sdmmc_driver);
	if (ret)
		brcmf_err("sdio_register_driver failed: %d\n", ret);
}

void brcmf_sdio_exit(void)
{
	brcmf_dbg(SDIO, "Enter\n");

	sdio_unregister_driver(&brcmf_sdmmc_driver);
}

