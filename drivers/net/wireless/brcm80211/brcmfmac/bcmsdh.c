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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>

#include <defs.h>
#include <brcm_hw_ids.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include <soc.h>
#include "dhd_bus.h"
#include "dhd_dbg.h"
#include "sdio_host.h"

#define SDIOH_API_ACCESS_RETRY_LIMIT	2

#ifdef CONFIG_BRCMFMAC_SDIO_OOB
static irqreturn_t brcmf_sdio_irqhandler(int irq, void *dev_id)
{
	struct brcmf_sdio_dev *sdiodev = dev_get_drvdata(dev_id);

	brcmf_dbg(INTR, "oob intr triggered\n");

	/*
	 * out-of-band interrupt is level-triggered which won't
	 * be cleared until dpc
	 */
	if (sdiodev->irq_en) {
		disable_irq_nosync(irq);
		sdiodev->irq_en = false;
	}

	brcmf_sdbrcm_isr(sdiodev->bus);

	return IRQ_HANDLED;
}

int brcmf_sdio_intr_register(struct brcmf_sdio_dev *sdiodev)
{
	int ret = 0;
	u8 data;
	unsigned long flags;

	brcmf_dbg(TRACE, "Entering\n");

	brcmf_dbg(ERROR, "requesting irq %d\n", sdiodev->irq);
	ret = request_irq(sdiodev->irq, brcmf_sdio_irqhandler,
			  sdiodev->irq_flags, "brcmf_oob_intr",
			  &sdiodev->func[1]->card->dev);
	if (ret != 0)
		return ret;
	spin_lock_init(&sdiodev->irq_en_lock);
	spin_lock_irqsave(&sdiodev->irq_en_lock, flags);
	sdiodev->irq_en = true;
	spin_unlock_irqrestore(&sdiodev->irq_en_lock, flags);

	ret = enable_irq_wake(sdiodev->irq);
	if (ret != 0)
		return ret;
	sdiodev->irq_wake = true;

	/* must configure SDIO_CCCR_IENx to enable irq */
	data = brcmf_sdcard_cfg_read(sdiodev, SDIO_FUNC_0,
				     SDIO_CCCR_IENx, &ret);
	data |= 1 << SDIO_FUNC_1 | 1 << SDIO_FUNC_2 | 1;
	brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_0, SDIO_CCCR_IENx,
			       data, &ret);

	/* redirect, configure ane enable io for interrupt signal */
	data = SDIO_SEPINT_MASK | SDIO_SEPINT_OE;
	if (sdiodev->irq_flags | IRQF_TRIGGER_HIGH)
		data |= SDIO_SEPINT_ACT_HI;
	brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_0, SDIO_CCCR_BRCM_SEPINT,
			       data, &ret);

	return 0;
}

int brcmf_sdio_intr_unregister(struct brcmf_sdio_dev *sdiodev)
{
	brcmf_dbg(TRACE, "Entering\n");

	brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_0, SDIO_CCCR_BRCM_SEPINT,
			       0, NULL);
	brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_0, SDIO_CCCR_IENx, 0, NULL);

	if (sdiodev->irq_wake) {
		disable_irq_wake(sdiodev->irq);
		sdiodev->irq_wake = false;
	}
	free_irq(sdiodev->irq, &sdiodev->func[1]->card->dev);
	sdiodev->irq_en = false;

	return 0;
}
#else		/* CONFIG_BRCMFMAC_SDIO_OOB */
static void brcmf_sdio_irqhandler(struct sdio_func *func)
{
	struct brcmf_sdio_dev *sdiodev = dev_get_drvdata(&func->card->dev);

	brcmf_dbg(INTR, "ib intr triggered\n");

	brcmf_sdbrcm_isr(sdiodev->bus);
}

/* dummy handler for SDIO function 2 interrupt */
static void brcmf_sdio_dummy_irqhandler(struct sdio_func *func)
{
}

int brcmf_sdio_intr_register(struct brcmf_sdio_dev *sdiodev)
{
	brcmf_dbg(TRACE, "Entering\n");

	sdio_claim_host(sdiodev->func[1]);
	sdio_claim_irq(sdiodev->func[1], brcmf_sdio_irqhandler);
	sdio_claim_irq(sdiodev->func[2], brcmf_sdio_dummy_irqhandler);
	sdio_release_host(sdiodev->func[1]);

	return 0;
}

int brcmf_sdio_intr_unregister(struct brcmf_sdio_dev *sdiodev)
{
	brcmf_dbg(TRACE, "Entering\n");

	sdio_claim_host(sdiodev->func[1]);
	sdio_release_irq(sdiodev->func[2]);
	sdio_release_irq(sdiodev->func[1]);
	sdio_release_host(sdiodev->func[1]);

	return 0;
}
#endif		/* CONFIG_BRCMFMAC_SDIO_OOB */

u8 brcmf_sdcard_cfg_read(struct brcmf_sdio_dev *sdiodev, uint fnc_num, u32 addr,
			 int *err)
{
	int status;
	s32 retry = 0;
	u8 data = 0;

	do {
		if (retry)	/* wait for 1 ms till bus get settled down */
			udelay(1000);
		status = brcmf_sdioh_request_byte(sdiodev, SDIOH_READ, fnc_num,
						  addr, (u8 *) &data);
	} while (status != 0
		 && (retry++ < SDIOH_API_ACCESS_RETRY_LIMIT));
	if (err)
		*err = status;

	brcmf_dbg(INFO, "fun = %d, addr = 0x%x, u8data = 0x%x\n",
		  fnc_num, addr, data);

	return data;
}

void
brcmf_sdcard_cfg_write(struct brcmf_sdio_dev *sdiodev, uint fnc_num, u32 addr,
		       u8 data, int *err)
{
	int status;
	s32 retry = 0;

	do {
		if (retry)	/* wait for 1 ms till bus get settled down */
			udelay(1000);
		status = brcmf_sdioh_request_byte(sdiodev, SDIOH_WRITE, fnc_num,
						  addr, (u8 *) &data);
	} while (status != 0
		 && (retry++ < SDIOH_API_ACCESS_RETRY_LIMIT));
	if (err)
		*err = status;

	brcmf_dbg(INFO, "fun = %d, addr = 0x%x, u8data = 0x%x\n",
		  fnc_num, addr, data);
}

int
brcmf_sdcard_set_sbaddr_window(struct brcmf_sdio_dev *sdiodev, u32 address)
{
	int err = 0;
	brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRLOW,
			 (address >> 8) & SBSDIO_SBADDRLOW_MASK, &err);
	if (!err)
		brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_1,
				       SBSDIO_FUNC1_SBADDRMID,
				       (address >> 16) & SBSDIO_SBADDRMID_MASK,
				       &err);
	if (!err)
		brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_1,
				       SBSDIO_FUNC1_SBADDRHIGH,
				       (address >> 24) & SBSDIO_SBADDRHIGH_MASK,
				       &err);

	return err;
}

u32 brcmf_sdcard_reg_read(struct brcmf_sdio_dev *sdiodev, u32 addr)
{
	int status;
	u32 word = 0;
	uint bar0 = addr & ~SBSDIO_SB_OFT_ADDR_MASK;

	brcmf_dbg(INFO, "fun = 1, addr = 0x%x\n", addr);

	if (bar0 != sdiodev->sbwad) {
		if (brcmf_sdcard_set_sbaddr_window(sdiodev, bar0))
			return 0xFFFFFFFF;

		sdiodev->sbwad = bar0;
	}

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	status = brcmf_sdioh_request_word(sdiodev, SDIOH_READ, SDIO_FUNC_1,
					  addr, &word, 4);

	sdiodev->regfail = (status != 0);

	if (status == 0) {
		brcmf_dbg(INFO, "data = 0x%x\n", word);
		return word;
	} else {
		brcmf_dbg(ERROR, "failed %d at addr 0x%04x\n", status, addr);
		return 0xFFFFFFFF;
	}
}

u32 brcmf_sdcard_reg_write(struct brcmf_sdio_dev *sdiodev, u32 addr, uint size,
			   u32 data)
{
	int status;
	uint bar0 = addr & ~SBSDIO_SB_OFT_ADDR_MASK;
	int err = 0;

	brcmf_dbg(INFO, "fun = 1, addr = 0x%x, uint%ddata = 0x%x\n",
		  addr, size * 8, data);

	if (bar0 != sdiodev->sbwad) {
		err = brcmf_sdcard_set_sbaddr_window(sdiodev, bar0);
		if (err)
			return err;

		sdiodev->sbwad = bar0;
	}

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	if (size == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;
	status =
	    brcmf_sdioh_request_word(sdiodev, SDIOH_WRITE, SDIO_FUNC_1,
				     addr, &data, size);
	sdiodev->regfail = (status != 0);

	if (status == 0)
		return 0;

	brcmf_dbg(ERROR, "error writing 0x%08x to addr 0x%04x size %d\n",
		  data, addr, size);
	return 0xFFFFFFFF;
}

bool brcmf_sdcard_regfail(struct brcmf_sdio_dev *sdiodev)
{
	return sdiodev->regfail;
}

static int brcmf_sdcard_recv_prepare(struct brcmf_sdio_dev *sdiodev, uint fn,
				     uint flags, uint width, u32 *addr)
{
	uint bar0 = *addr & ~SBSDIO_SB_OFT_ADDR_MASK;
	int err = 0;

	/* Async not implemented yet */
	if (flags & SDIO_REQ_ASYNC)
		return -ENOTSUPP;

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
brcmf_sdcard_recv_buf(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags, u8 *buf, uint nbytes)
{
	struct sk_buff *mypkt;
	int err;

	mypkt = brcmu_pkt_buf_get_skb(nbytes);
	if (!mypkt) {
		brcmf_dbg(ERROR, "brcmu_pkt_buf_get_skb failed: len %d\n",
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
	uint incr_fix;
	uint width;
	int err = 0;

	brcmf_dbg(INFO, "fun = %d, addr = 0x%x, size = %d\n",
		  fn, addr, pkt->len);

	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	err = brcmf_sdcard_recv_prepare(sdiodev, fn, flags, width, &addr);
	if (err)
		return err;

	incr_fix = (flags & SDIO_REQ_FIXED) ? SDIOH_DATA_FIX : SDIOH_DATA_INC;
	err = brcmf_sdioh_request_buffer(sdiodev, incr_fix, SDIOH_READ,
					 fn, addr, pkt);

	return err;
}

int brcmf_sdcard_recv_chain(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
			    uint flags, struct sk_buff_head *pktq)
{
	uint incr_fix;
	uint width;
	int err = 0;

	brcmf_dbg(INFO, "fun = %d, addr = 0x%x, size = %d\n",
		  fn, addr, pktq->qlen);

	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	err = brcmf_sdcard_recv_prepare(sdiodev, fn, flags, width, &addr);
	if (err)
		return err;

	incr_fix = (flags & SDIO_REQ_FIXED) ? SDIOH_DATA_FIX : SDIOH_DATA_INC;
	err = brcmf_sdioh_request_chain(sdiodev, incr_fix, SDIOH_READ, fn, addr,
					pktq);

	return err;
}

int
brcmf_sdcard_send_buf(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags, u8 *buf, uint nbytes)
{
	struct sk_buff *mypkt;
	int err;

	mypkt = brcmu_pkt_buf_get_skb(nbytes);
	if (!mypkt) {
		brcmf_dbg(ERROR, "brcmu_pkt_buf_get_skb failed: len %d\n",
			  nbytes);
		return -EIO;
	}

	memcpy(mypkt->data, buf, nbytes);
	err = brcmf_sdcard_send_pkt(sdiodev, addr, fn, flags, mypkt);

	brcmu_pkt_buf_free_skb(mypkt);
	return err;

}

int
brcmf_sdcard_send_pkt(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags, struct sk_buff *pkt)
{
	uint incr_fix;
	uint width;
	uint bar0 = addr & ~SBSDIO_SB_OFT_ADDR_MASK;
	int err = 0;

	brcmf_dbg(INFO, "fun = %d, addr = 0x%x, size = %d\n",
		  fn, addr, pkt->len);

	/* Async not implemented yet */
	if (flags & SDIO_REQ_ASYNC)
		return -ENOTSUPP;

	if (bar0 != sdiodev->sbwad) {
		err = brcmf_sdcard_set_sbaddr_window(sdiodev, bar0);
		if (err)
			return err;

		sdiodev->sbwad = bar0;
	}

	addr &= SBSDIO_SB_OFT_ADDR_MASK;

	incr_fix = (flags & SDIO_REQ_FIXED) ? SDIOH_DATA_FIX : SDIOH_DATA_INC;
	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	if (width == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	return brcmf_sdioh_request_buffer(sdiodev, incr_fix, SDIOH_WRITE, fn,
					  addr, pkt);
}

int brcmf_sdcard_rwdata(struct brcmf_sdio_dev *sdiodev, uint rw, u32 addr,
			u8 *buf, uint nbytes)
{
	struct sk_buff *mypkt;
	bool write = rw ? SDIOH_WRITE : SDIOH_READ;
	int err;

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	mypkt = brcmu_pkt_buf_get_skb(nbytes);
	if (!mypkt) {
		brcmf_dbg(ERROR, "brcmu_pkt_buf_get_skb failed: len %d\n",
			  nbytes);
		return -EIO;
	}

	/* For a write, copy the buffer data into the packet. */
	if (write)
		memcpy(mypkt->data, buf, nbytes);

	err = brcmf_sdioh_request_buffer(sdiodev, SDIOH_DATA_INC, write,
					 SDIO_FUNC_1, addr, mypkt);

	/* For a read, copy the packet data back to the buffer. */
	if (!err && !write)
		memcpy(buf, mypkt->data, nbytes);

	brcmu_pkt_buf_free_skb(mypkt);
	return err;
}

int brcmf_sdcard_abort(struct brcmf_sdio_dev *sdiodev, uint fn)
{
	char t_func = (char)fn;
	brcmf_dbg(TRACE, "Enter\n");

	/* issue abort cmd52 command through F0 */
	brcmf_sdioh_request_byte(sdiodev, SDIOH_WRITE, SDIO_FUNC_0,
				 SDIO_CCCR_ABORT, &t_func);

	brcmf_dbg(TRACE, "Exit\n");
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

	/* Report the BAR, to fix if needed */
	sdiodev->sbwad = SI_ENUM_BASE;

	/* try to attach to the target device */
	sdiodev->bus = brcmf_sdbrcm_probe(regs, sdiodev);
	if (!sdiodev->bus) {
		brcmf_dbg(ERROR, "device attach failed\n");
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
