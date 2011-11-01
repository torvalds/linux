/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
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

#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sd.h>
#include "hif.h"
#include "hif-ops.h"
#include "target.h"
#include "debug.h"
#include "cfg80211.h"

struct ath6kl_sdio {
	struct sdio_func *func;

	spinlock_t lock;

	/* free list */
	struct list_head bus_req_freeq;

	/* available bus requests */
	struct bus_request bus_req[BUS_REQUEST_MAX_NUM];

	struct ath6kl *ar;
	u8 *dma_buffer;

	/* scatter request list head */
	struct list_head scat_req;

	spinlock_t scat_lock;
	bool scatter_enabled;

	bool is_disabled;
	atomic_t irq_handling;
	const struct sdio_device_id *id;
	struct work_struct wr_async_work;
	struct list_head wr_asyncq;
	spinlock_t wr_async_lock;
};

#define CMD53_ARG_READ          0
#define CMD53_ARG_WRITE         1
#define CMD53_ARG_BLOCK_BASIS   1
#define CMD53_ARG_FIXED_ADDRESS 0
#define CMD53_ARG_INCR_ADDRESS  1

static inline struct ath6kl_sdio *ath6kl_sdio_priv(struct ath6kl *ar)
{
	return ar->hif_priv;
}

/*
 * Macro to check if DMA buffer is WORD-aligned and DMA-able.
 * Most host controllers assume the buffer is DMA'able and will
 * bug-check otherwise (i.e. buffers on the stack). virt_addr_valid
 * check fails on stack memory.
 */
static inline bool buf_needs_bounce(u8 *buf)
{
	return ((unsigned long) buf & 0x3) || !virt_addr_valid(buf);
}

static void ath6kl_sdio_set_mbox_info(struct ath6kl *ar)
{
	struct ath6kl_mbox_info *mbox_info = &ar->mbox_info;

	/* EP1 has an extended range */
	mbox_info->htc_addr = HIF_MBOX_BASE_ADDR;
	mbox_info->htc_ext_addr = HIF_MBOX0_EXT_BASE_ADDR;
	mbox_info->htc_ext_sz = HIF_MBOX0_EXT_WIDTH;
	mbox_info->block_size = HIF_MBOX_BLOCK_SIZE;
	mbox_info->gmbox_addr = HIF_GMBOX_BASE_ADDR;
	mbox_info->gmbox_sz = HIF_GMBOX_WIDTH;
}

static inline void ath6kl_sdio_set_cmd53_arg(u32 *arg, u8 rw, u8 func,
					     u8 mode, u8 opcode, u32 addr,
					     u16 blksz)
{
	*arg = (((rw & 1) << 31) |
		((func & 0x7) << 28) |
		((mode & 1) << 27) |
		((opcode & 1) << 26) |
		((addr & 0x1FFFF) << 9) |
		(blksz & 0x1FF));
}

static inline void ath6kl_sdio_set_cmd52_arg(u32 *arg, u8 write, u8 raw,
					     unsigned int address,
					     unsigned char val)
{
	const u8 func = 0;

	*arg = ((write & 1) << 31) |
	       ((func & 0x7) << 28) |
	       ((raw & 1) << 27) |
	       (1 << 26) |
	       ((address & 0x1FFFF) << 9) |
	       (1 << 8) |
	       (val & 0xFF);
}

static int ath6kl_sdio_func0_cmd52_wr_byte(struct mmc_card *card,
					   unsigned int address,
					   unsigned char byte)
{
	struct mmc_command io_cmd;

	memset(&io_cmd, 0, sizeof(io_cmd));
	ath6kl_sdio_set_cmd52_arg(&io_cmd.arg, 1, 0, address, byte);
	io_cmd.opcode = SD_IO_RW_DIRECT;
	io_cmd.flags = MMC_RSP_R5 | MMC_CMD_AC;

	return mmc_wait_for_cmd(card->host, &io_cmd, 0);
}

static int ath6kl_sdio_io(struct sdio_func *func, u32 request, u32 addr,
			  u8 *buf, u32 len)
{
	int ret = 0;

	sdio_claim_host(func);

	if (request & HIF_WRITE) {
		/* FIXME: looks like ugly workaround for something */
		if (addr >= HIF_MBOX_BASE_ADDR &&
		    addr <= HIF_MBOX_END_ADDR)
			addr += (HIF_MBOX_WIDTH - len);

		/* FIXME: this also looks like ugly workaround */
		if (addr == HIF_MBOX0_EXT_BASE_ADDR)
			addr += HIF_MBOX0_EXT_WIDTH - len;

		if (request & HIF_FIXED_ADDRESS)
			ret = sdio_writesb(func, addr, buf, len);
		else
			ret = sdio_memcpy_toio(func, addr, buf, len);
	} else {
		if (request & HIF_FIXED_ADDRESS)
			ret = sdio_readsb(func, buf, addr, len);
		else
			ret = sdio_memcpy_fromio(func, buf, addr, len);
	}

	sdio_release_host(func);

	ath6kl_dbg(ATH6KL_DBG_SDIO, "%s addr 0x%x%s buf 0x%p len %d\n",
		   request & HIF_WRITE ? "wr" : "rd", addr,
		   request & HIF_FIXED_ADDRESS ? " (fixed)" : "", buf, len);
	ath6kl_dbg_dump(ATH6KL_DBG_SDIO_DUMP, NULL, "sdio ", buf, len);

	return ret;
}

static struct bus_request *ath6kl_sdio_alloc_busreq(struct ath6kl_sdio *ar_sdio)
{
	struct bus_request *bus_req;

	spin_lock_bh(&ar_sdio->lock);

	if (list_empty(&ar_sdio->bus_req_freeq)) {
		spin_unlock_bh(&ar_sdio->lock);
		return NULL;
	}

	bus_req = list_first_entry(&ar_sdio->bus_req_freeq,
				   struct bus_request, list);
	list_del(&bus_req->list);

	spin_unlock_bh(&ar_sdio->lock);
	ath6kl_dbg(ATH6KL_DBG_SCATTER, "%s: bus request 0x%p\n",
		   __func__, bus_req);

	return bus_req;
}

static void ath6kl_sdio_free_bus_req(struct ath6kl_sdio *ar_sdio,
				     struct bus_request *bus_req)
{
	ath6kl_dbg(ATH6KL_DBG_SCATTER, "%s: bus request 0x%p\n",
		   __func__, bus_req);

	spin_lock_bh(&ar_sdio->lock);
	list_add_tail(&bus_req->list, &ar_sdio->bus_req_freeq);
	spin_unlock_bh(&ar_sdio->lock);
}

static void ath6kl_sdio_setup_scat_data(struct hif_scatter_req *scat_req,
					struct mmc_data *data)
{
	struct scatterlist *sg;
	int i;

	data->blksz = HIF_MBOX_BLOCK_SIZE;
	data->blocks = scat_req->len / HIF_MBOX_BLOCK_SIZE;

	ath6kl_dbg(ATH6KL_DBG_SCATTER,
		   "hif-scatter: (%s) addr: 0x%X, (block len: %d, block count: %d) , (tot:%d,sg:%d)\n",
		   (scat_req->req & HIF_WRITE) ? "WR" : "RD", scat_req->addr,
		   data->blksz, data->blocks, scat_req->len,
		   scat_req->scat_entries);

	data->flags = (scat_req->req & HIF_WRITE) ? MMC_DATA_WRITE :
						    MMC_DATA_READ;

	/* fill SG entries */
	sg = scat_req->sgentries;
	sg_init_table(sg, scat_req->scat_entries);

	/* assemble SG list */
	for (i = 0; i < scat_req->scat_entries; i++, sg++) {
		ath6kl_dbg(ATH6KL_DBG_SCATTER, "%d: addr:0x%p, len:%d\n",
			   i, scat_req->scat_list[i].buf,
			   scat_req->scat_list[i].len);

		sg_set_buf(sg, scat_req->scat_list[i].buf,
			   scat_req->scat_list[i].len);
	}

	/* set scatter-gather table for request */
	data->sg = scat_req->sgentries;
	data->sg_len = scat_req->scat_entries;
}

static int ath6kl_sdio_scat_rw(struct ath6kl_sdio *ar_sdio,
			       struct bus_request *req)
{
	struct mmc_request mmc_req;
	struct mmc_command cmd;
	struct mmc_data data;
	struct hif_scatter_req *scat_req;
	u8 opcode, rw;
	int status, len;

	scat_req = req->scat_req;

	if (scat_req->virt_scat) {
		len = scat_req->len;
		if (scat_req->req & HIF_BLOCK_BASIS)
			len = round_down(len, HIF_MBOX_BLOCK_SIZE);

		status = ath6kl_sdio_io(ar_sdio->func, scat_req->req,
					scat_req->addr, scat_req->virt_dma_buf,
					len);
		goto scat_complete;
	}

	memset(&mmc_req, 0, sizeof(struct mmc_request));
	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&data, 0, sizeof(struct mmc_data));

	ath6kl_sdio_setup_scat_data(scat_req, &data);

	opcode = (scat_req->req & HIF_FIXED_ADDRESS) ?
		  CMD53_ARG_FIXED_ADDRESS : CMD53_ARG_INCR_ADDRESS;

	rw = (scat_req->req & HIF_WRITE) ? CMD53_ARG_WRITE : CMD53_ARG_READ;

	/* Fixup the address so that the last byte will fall on MBOX EOM */
	if (scat_req->req & HIF_WRITE) {
		if (scat_req->addr == HIF_MBOX_BASE_ADDR)
			scat_req->addr += HIF_MBOX_WIDTH - scat_req->len;
		else
			/* Uses extended address range */
			scat_req->addr += HIF_MBOX0_EXT_WIDTH - scat_req->len;
	}

	/* set command argument */
	ath6kl_sdio_set_cmd53_arg(&cmd.arg, rw, ar_sdio->func->num,
				  CMD53_ARG_BLOCK_BASIS, opcode, scat_req->addr,
				  data.blocks);

	cmd.opcode = SD_IO_RW_EXTENDED;
	cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_ADTC;

	mmc_req.cmd = &cmd;
	mmc_req.data = &data;

	sdio_claim_host(ar_sdio->func);

	mmc_set_data_timeout(&data, ar_sdio->func->card);
	/* synchronous call to process request */
	mmc_wait_for_req(ar_sdio->func->card->host, &mmc_req);

	sdio_release_host(ar_sdio->func);

	status = cmd.error ? cmd.error : data.error;

scat_complete:
	scat_req->status = status;

	if (scat_req->status)
		ath6kl_err("Scatter write request failed:%d\n",
			   scat_req->status);

	if (scat_req->req & HIF_ASYNCHRONOUS)
		scat_req->complete(ar_sdio->ar->htc_target, scat_req);

	return status;
}

static int ath6kl_sdio_alloc_prep_scat_req(struct ath6kl_sdio *ar_sdio,
					   int n_scat_entry, int n_scat_req,
					   bool virt_scat)
{
	struct hif_scatter_req *s_req;
	struct bus_request *bus_req;
	int i, scat_req_sz, scat_list_sz, sg_sz, buf_sz;
	u8 *virt_buf;

	scat_list_sz = (n_scat_entry - 1) * sizeof(struct hif_scatter_item);
	scat_req_sz = sizeof(*s_req) + scat_list_sz;

	if (!virt_scat)
		sg_sz = sizeof(struct scatterlist) * n_scat_entry;
	else
		buf_sz =  2 * L1_CACHE_BYTES +
			  ATH6KL_MAX_TRANSFER_SIZE_PER_SCATTER;

	for (i = 0; i < n_scat_req; i++) {
		/* allocate the scatter request */
		s_req = kzalloc(scat_req_sz, GFP_KERNEL);
		if (!s_req)
			return -ENOMEM;

		if (virt_scat) {
			virt_buf = kzalloc(buf_sz, GFP_KERNEL);
			if (!virt_buf) {
				kfree(s_req);
				return -ENOMEM;
			}

			s_req->virt_dma_buf =
				(u8 *)L1_CACHE_ALIGN((unsigned long)virt_buf);
		} else {
			/* allocate sglist */
			s_req->sgentries = kzalloc(sg_sz, GFP_KERNEL);

			if (!s_req->sgentries) {
				kfree(s_req);
				return -ENOMEM;
			}
		}

		/* allocate a bus request for this scatter request */
		bus_req = ath6kl_sdio_alloc_busreq(ar_sdio);
		if (!bus_req) {
			kfree(s_req->sgentries);
			kfree(s_req->virt_dma_buf);
			kfree(s_req);
			return -ENOMEM;
		}

		/* assign the scatter request to this bus request */
		bus_req->scat_req = s_req;
		s_req->busrequest = bus_req;

		s_req->virt_scat = virt_scat;

		/* add it to the scatter pool */
		hif_scatter_req_add(ar_sdio->ar, s_req);
	}

	return 0;
}

static int ath6kl_sdio_read_write_sync(struct ath6kl *ar, u32 addr, u8 *buf,
				       u32 len, u32 request)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	u8  *tbuf = NULL;
	int ret;
	bool bounced = false;

	if (request & HIF_BLOCK_BASIS)
		len = round_down(len, HIF_MBOX_BLOCK_SIZE);

	if (buf_needs_bounce(buf)) {
		if (!ar_sdio->dma_buffer)
			return -ENOMEM;
		tbuf = ar_sdio->dma_buffer;
		memcpy(tbuf, buf, len);
		bounced = true;
	} else
		tbuf = buf;

	ret = ath6kl_sdio_io(ar_sdio->func, request, addr, tbuf, len);
	if ((request & HIF_READ) && bounced)
		memcpy(buf, tbuf, len);

	return ret;
}

static void __ath6kl_sdio_write_async(struct ath6kl_sdio *ar_sdio,
				      struct bus_request *req)
{
	if (req->scat_req)
		ath6kl_sdio_scat_rw(ar_sdio, req);
	else {
		void *context;
		int status;

		status = ath6kl_sdio_read_write_sync(ar_sdio->ar, req->address,
						     req->buffer, req->length,
						     req->request);
		context = req->packet;
		ath6kl_sdio_free_bus_req(ar_sdio, req);
		ath6kl_hif_rw_comp_handler(context, status);
	}
}

static void ath6kl_sdio_write_async_work(struct work_struct *work)
{
	struct ath6kl_sdio *ar_sdio;
	struct bus_request *req, *tmp_req;

	ar_sdio = container_of(work, struct ath6kl_sdio, wr_async_work);

	spin_lock_bh(&ar_sdio->wr_async_lock);
	list_for_each_entry_safe(req, tmp_req, &ar_sdio->wr_asyncq, list) {
		list_del(&req->list);
		spin_unlock_bh(&ar_sdio->wr_async_lock);
		__ath6kl_sdio_write_async(ar_sdio, req);
		spin_lock_bh(&ar_sdio->wr_async_lock);
	}
	spin_unlock_bh(&ar_sdio->wr_async_lock);
}

static void ath6kl_sdio_irq_handler(struct sdio_func *func)
{
	int status;
	struct ath6kl_sdio *ar_sdio;

	ath6kl_dbg(ATH6KL_DBG_SDIO, "irq\n");

	ar_sdio = sdio_get_drvdata(func);
	atomic_set(&ar_sdio->irq_handling, 1);

	/*
	 * Release the host during interrups so we can pick it back up when
	 * we process commands.
	 */
	sdio_release_host(ar_sdio->func);

	status = ath6kl_hif_intr_bh_handler(ar_sdio->ar);
	sdio_claim_host(ar_sdio->func);
	atomic_set(&ar_sdio->irq_handling, 0);
	WARN_ON(status && status != -ECANCELED);
}

static int ath6kl_sdio_power_on(struct ath6kl *ar)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	struct sdio_func *func = ar_sdio->func;
	int ret = 0;

	if (!ar_sdio->is_disabled)
		return 0;

	ath6kl_dbg(ATH6KL_DBG_BOOT, "sdio power on\n");

	sdio_claim_host(func);

	ret = sdio_enable_func(func);
	if (ret) {
		ath6kl_err("Unable to enable sdio func: %d)\n", ret);
		sdio_release_host(func);
		return ret;
	}

	sdio_release_host(func);

	/*
	 * Wait for hardware to initialise. It should take a lot less than
	 * 10 ms but let's be conservative here.
	 */
	msleep(10);

	ar_sdio->is_disabled = false;

	return ret;
}

static int ath6kl_sdio_power_off(struct ath6kl *ar)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	int ret;

	if (ar_sdio->is_disabled)
		return 0;

	ath6kl_dbg(ATH6KL_DBG_BOOT, "sdio power off\n");

	/* Disable the card */
	sdio_claim_host(ar_sdio->func);
	ret = sdio_disable_func(ar_sdio->func);
	sdio_release_host(ar_sdio->func);

	if (ret)
		return ret;

	ar_sdio->is_disabled = true;

	return ret;
}

static int ath6kl_sdio_write_async(struct ath6kl *ar, u32 address, u8 *buffer,
				   u32 length, u32 request,
				   struct htc_packet *packet)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	struct bus_request *bus_req;

	bus_req = ath6kl_sdio_alloc_busreq(ar_sdio);

	if (!bus_req)
		return -ENOMEM;

	bus_req->address = address;
	bus_req->buffer = buffer;
	bus_req->length = length;
	bus_req->request = request;
	bus_req->packet = packet;

	spin_lock_bh(&ar_sdio->wr_async_lock);
	list_add_tail(&bus_req->list, &ar_sdio->wr_asyncq);
	spin_unlock_bh(&ar_sdio->wr_async_lock);
	queue_work(ar->ath6kl_wq, &ar_sdio->wr_async_work);

	return 0;
}

static void ath6kl_sdio_irq_enable(struct ath6kl *ar)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	int ret;

	sdio_claim_host(ar_sdio->func);

	/* Register the isr */
	ret =  sdio_claim_irq(ar_sdio->func, ath6kl_sdio_irq_handler);
	if (ret)
		ath6kl_err("Failed to claim sdio irq: %d\n", ret);

	sdio_release_host(ar_sdio->func);
}

static void ath6kl_sdio_irq_disable(struct ath6kl *ar)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	int ret;

	sdio_claim_host(ar_sdio->func);

	/* Mask our function IRQ */
	while (atomic_read(&ar_sdio->irq_handling)) {
		sdio_release_host(ar_sdio->func);
		schedule_timeout(HZ / 10);
		sdio_claim_host(ar_sdio->func);
	}

	ret = sdio_release_irq(ar_sdio->func);
	if (ret)
		ath6kl_err("Failed to release sdio irq: %d\n", ret);

	sdio_release_host(ar_sdio->func);
}

static struct hif_scatter_req *ath6kl_sdio_scatter_req_get(struct ath6kl *ar)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	struct hif_scatter_req *node = NULL;

	spin_lock_bh(&ar_sdio->scat_lock);

	if (!list_empty(&ar_sdio->scat_req)) {
		node = list_first_entry(&ar_sdio->scat_req,
					struct hif_scatter_req, list);
		list_del(&node->list);
	}

	spin_unlock_bh(&ar_sdio->scat_lock);

	return node;
}

static void ath6kl_sdio_scatter_req_add(struct ath6kl *ar,
					struct hif_scatter_req *s_req)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);

	spin_lock_bh(&ar_sdio->scat_lock);

	list_add_tail(&s_req->list, &ar_sdio->scat_req);

	spin_unlock_bh(&ar_sdio->scat_lock);

}

/* scatter gather read write request */
static int ath6kl_sdio_async_rw_scatter(struct ath6kl *ar,
					struct hif_scatter_req *scat_req)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	u32 request = scat_req->req;
	int status = 0;

	if (!scat_req->len)
		return -EINVAL;

	ath6kl_dbg(ATH6KL_DBG_SCATTER,
		"hif-scatter: total len: %d scatter entries: %d\n",
		scat_req->len, scat_req->scat_entries);

	if (request & HIF_SYNCHRONOUS)
		status = ath6kl_sdio_scat_rw(ar_sdio, scat_req->busrequest);
	else {
		spin_lock_bh(&ar_sdio->wr_async_lock);
		list_add_tail(&scat_req->busrequest->list, &ar_sdio->wr_asyncq);
		spin_unlock_bh(&ar_sdio->wr_async_lock);
		queue_work(ar->ath6kl_wq, &ar_sdio->wr_async_work);
	}

	return status;
}

/* clean up scatter support */
static void ath6kl_sdio_cleanup_scatter(struct ath6kl *ar)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	struct hif_scatter_req *s_req, *tmp_req;

	/* empty the free list */
	spin_lock_bh(&ar_sdio->scat_lock);
	list_for_each_entry_safe(s_req, tmp_req, &ar_sdio->scat_req, list) {
		list_del(&s_req->list);
		spin_unlock_bh(&ar_sdio->scat_lock);

		/*
		 * FIXME: should we also call completion handler with
		 * ath6kl_hif_rw_comp_handler() with status -ECANCELED so
		 * that the packet is properly freed?
		 */
		if (s_req->busrequest)
			ath6kl_sdio_free_bus_req(ar_sdio, s_req->busrequest);
		kfree(s_req->virt_dma_buf);
		kfree(s_req->sgentries);
		kfree(s_req);

		spin_lock_bh(&ar_sdio->scat_lock);
	}
	spin_unlock_bh(&ar_sdio->scat_lock);
}

/* setup of HIF scatter resources */
static int ath6kl_sdio_enable_scatter(struct ath6kl *ar)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	struct htc_target *target = ar->htc_target;
	int ret;
	bool virt_scat = false;

	if (ar_sdio->scatter_enabled)
		return 0;

	ar_sdio->scatter_enabled = true;

	/* check if host supports scatter and it meets our requirements */
	if (ar_sdio->func->card->host->max_segs < MAX_SCATTER_ENTRIES_PER_REQ) {
		ath6kl_err("host only supports scatter of :%d entries, need: %d\n",
			   ar_sdio->func->card->host->max_segs,
			   MAX_SCATTER_ENTRIES_PER_REQ);
		virt_scat = true;
	}

	if (!virt_scat) {
		ret = ath6kl_sdio_alloc_prep_scat_req(ar_sdio,
				MAX_SCATTER_ENTRIES_PER_REQ,
				MAX_SCATTER_REQUESTS, virt_scat);

		if (!ret) {
			ath6kl_dbg(ATH6KL_DBG_BOOT,
				   "hif-scatter enabled requests %d entries %d\n",
				   MAX_SCATTER_REQUESTS,
				   MAX_SCATTER_ENTRIES_PER_REQ);

			target->max_scat_entries = MAX_SCATTER_ENTRIES_PER_REQ;
			target->max_xfer_szper_scatreq =
						MAX_SCATTER_REQ_TRANSFER_SIZE;
		} else {
			ath6kl_sdio_cleanup_scatter(ar);
			ath6kl_warn("hif scatter resource setup failed, trying virtual scatter method\n");
		}
	}

	if (virt_scat || ret) {
		ret = ath6kl_sdio_alloc_prep_scat_req(ar_sdio,
				ATH6KL_SCATTER_ENTRIES_PER_REQ,
				ATH6KL_SCATTER_REQS, virt_scat);

		if (ret) {
			ath6kl_err("failed to alloc virtual scatter resources !\n");
			ath6kl_sdio_cleanup_scatter(ar);
			return ret;
		}

		ath6kl_dbg(ATH6KL_DBG_BOOT,
			   "virtual scatter enabled requests %d entries %d\n",
			   ATH6KL_SCATTER_REQS, ATH6KL_SCATTER_ENTRIES_PER_REQ);

		target->max_scat_entries = ATH6KL_SCATTER_ENTRIES_PER_REQ;
		target->max_xfer_szper_scatreq =
					ATH6KL_MAX_TRANSFER_SIZE_PER_SCATTER;
	}

	return 0;
}

static int ath6kl_sdio_config(struct ath6kl *ar)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	struct sdio_func *func = ar_sdio->func;
	int ret;

	sdio_claim_host(func);

	if ((ar_sdio->id->device & MANUFACTURER_ID_ATH6KL_BASE_MASK) >=
	    MANUFACTURER_ID_AR6003_BASE) {
		/* enable 4-bit ASYNC interrupt on AR6003 or later */
		ret = ath6kl_sdio_func0_cmd52_wr_byte(func->card,
						CCCR_SDIO_IRQ_MODE_REG,
						SDIO_IRQ_MODE_ASYNC_4BIT_IRQ);
		if (ret) {
			ath6kl_err("Failed to enable 4-bit async irq mode %d\n",
				   ret);
			goto out;
		}

		ath6kl_dbg(ATH6KL_DBG_BOOT, "4-bit async irq mode enabled\n");
	}

	/* give us some time to enable, in ms */
	func->enable_timeout = 100;

	ret = sdio_set_block_size(func, HIF_MBOX_BLOCK_SIZE);
	if (ret) {
		ath6kl_err("Set sdio block size %d failed: %d)\n",
			   HIF_MBOX_BLOCK_SIZE, ret);
		sdio_release_host(func);
		goto out;
	}

out:
	sdio_release_host(func);

	return ret;
}

static int ath6kl_sdio_suspend(struct ath6kl *ar)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	struct sdio_func *func = ar_sdio->func;
	mmc_pm_flag_t flags;
	int ret;

	flags = sdio_get_host_pm_caps(func);

	if (!(flags & MMC_PM_KEEP_POWER)) {
		/* as host doesn't support keep power we need to bail out */
		ath6kl_dbg(ATH6KL_DBG_SDIO,
			   "func %d doesn't support MMC_PM_KEEP_POWER\n",
			   func->num);
		return -EINVAL;
	}

	ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	if (ret) {
		printk(KERN_ERR "ath6kl: set sdio pm flags failed: %d\n",
		       ret);
		return ret;
	}

	ath6kl_cfg80211_suspend(ar, ATH6KL_CFG_SUSPEND_DEEPSLEEP);

	return 0;
}

static int ath6kl_sdio_resume(struct ath6kl *ar)
{
	ath6kl_cfg80211_resume(ar);

	return 0;
}

static void ath6kl_sdio_stop(struct ath6kl *ar)
{
	struct ath6kl_sdio *ar_sdio = ath6kl_sdio_priv(ar);
	struct bus_request *req, *tmp_req;
	void *context;

	/* FIXME: make sure that wq is not queued again */

	cancel_work_sync(&ar_sdio->wr_async_work);

	spin_lock_bh(&ar_sdio->wr_async_lock);

	list_for_each_entry_safe(req, tmp_req, &ar_sdio->wr_asyncq, list) {
		list_del(&req->list);

		if (req->scat_req) {
			/* this is a scatter gather request */
			req->scat_req->status = -ECANCELED;
			req->scat_req->complete(ar_sdio->ar->htc_target,
						req->scat_req);
		} else {
			context = req->packet;
			ath6kl_sdio_free_bus_req(ar_sdio, req);
			ath6kl_hif_rw_comp_handler(context, -ECANCELED);
		}
	}

	spin_unlock_bh(&ar_sdio->wr_async_lock);

	WARN_ON(get_queue_depth(&ar_sdio->scat_req) != 4);
}

static const struct ath6kl_hif_ops ath6kl_sdio_ops = {
	.read_write_sync = ath6kl_sdio_read_write_sync,
	.write_async = ath6kl_sdio_write_async,
	.irq_enable = ath6kl_sdio_irq_enable,
	.irq_disable = ath6kl_sdio_irq_disable,
	.scatter_req_get = ath6kl_sdio_scatter_req_get,
	.scatter_req_add = ath6kl_sdio_scatter_req_add,
	.enable_scatter = ath6kl_sdio_enable_scatter,
	.scat_req_rw = ath6kl_sdio_async_rw_scatter,
	.cleanup_scatter = ath6kl_sdio_cleanup_scatter,
	.suspend = ath6kl_sdio_suspend,
	.resume = ath6kl_sdio_resume,
	.power_on = ath6kl_sdio_power_on,
	.power_off = ath6kl_sdio_power_off,
	.stop = ath6kl_sdio_stop,
};

static int ath6kl_sdio_probe(struct sdio_func *func,
			     const struct sdio_device_id *id)
{
	int ret;
	struct ath6kl_sdio *ar_sdio;
	struct ath6kl *ar;
	int count;

	ath6kl_dbg(ATH6KL_DBG_BOOT,
		   "sdio new func %d vendor 0x%x device 0x%x block 0x%x/0x%x\n",
		   func->num, func->vendor, func->device,
		   func->max_blksize, func->cur_blksize);

	ar_sdio = kzalloc(sizeof(struct ath6kl_sdio), GFP_KERNEL);
	if (!ar_sdio)
		return -ENOMEM;

	ar_sdio->dma_buffer = kzalloc(HIF_DMA_BUFFER_SIZE, GFP_KERNEL);
	if (!ar_sdio->dma_buffer) {
		ret = -ENOMEM;
		goto err_hif;
	}

	ar_sdio->func = func;
	sdio_set_drvdata(func, ar_sdio);

	ar_sdio->id = id;
	ar_sdio->is_disabled = true;

	spin_lock_init(&ar_sdio->lock);
	spin_lock_init(&ar_sdio->scat_lock);
	spin_lock_init(&ar_sdio->wr_async_lock);

	INIT_LIST_HEAD(&ar_sdio->scat_req);
	INIT_LIST_HEAD(&ar_sdio->bus_req_freeq);
	INIT_LIST_HEAD(&ar_sdio->wr_asyncq);

	INIT_WORK(&ar_sdio->wr_async_work, ath6kl_sdio_write_async_work);

	for (count = 0; count < BUS_REQUEST_MAX_NUM; count++)
		ath6kl_sdio_free_bus_req(ar_sdio, &ar_sdio->bus_req[count]);

	ar = ath6kl_core_alloc(&ar_sdio->func->dev);
	if (!ar) {
		ath6kl_err("Failed to alloc ath6kl core\n");
		ret = -ENOMEM;
		goto err_dma;
	}

	ar_sdio->ar = ar;
	ar->hif_priv = ar_sdio;
	ar->hif_ops = &ath6kl_sdio_ops;

	ath6kl_sdio_set_mbox_info(ar);

	ret = ath6kl_sdio_config(ar);
	if (ret) {
		ath6kl_err("Failed to config sdio: %d\n", ret);
		goto err_core_alloc;
	}

	ret = ath6kl_core_init(ar);
	if (ret) {
		ath6kl_err("Failed to init ath6kl core\n");
		goto err_core_alloc;
	}

	return ret;

err_core_alloc:
	ath6kl_core_free(ar_sdio->ar);
err_dma:
	kfree(ar_sdio->dma_buffer);
err_hif:
	kfree(ar_sdio);

	return ret;
}

static void ath6kl_sdio_remove(struct sdio_func *func)
{
	struct ath6kl_sdio *ar_sdio;

	ath6kl_dbg(ATH6KL_DBG_BOOT,
		   "sdio removed func %d vendor 0x%x device 0x%x\n",
		   func->num, func->vendor, func->device);

	ar_sdio = sdio_get_drvdata(func);

	ath6kl_stop_txrx(ar_sdio->ar);
	cancel_work_sync(&ar_sdio->wr_async_work);

	ath6kl_core_cleanup(ar_sdio->ar);

	kfree(ar_sdio->dma_buffer);
	kfree(ar_sdio);
}

static const struct sdio_device_id ath6kl_sdio_devices[] = {
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6003_BASE | 0x0))},
	{SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_AR6003_BASE | 0x1))},
	{},
};

MODULE_DEVICE_TABLE(sdio, ath6kl_sdio_devices);

static struct sdio_driver ath6kl_sdio_driver = {
	.name = "ath6kl",
	.id_table = ath6kl_sdio_devices,
	.probe = ath6kl_sdio_probe,
	.remove = ath6kl_sdio_remove,
};

static int __init ath6kl_sdio_init(void)
{
	int ret;

	ret = sdio_register_driver(&ath6kl_sdio_driver);
	if (ret)
		ath6kl_err("sdio driver registration failed: %d\n", ret);

	return ret;
}

static void __exit ath6kl_sdio_exit(void)
{
	sdio_unregister_driver(&ath6kl_sdio_driver);
}

module_init(ath6kl_sdio_init);
module_exit(ath6kl_sdio_exit);

MODULE_AUTHOR("Atheros Communications, Inc.");
MODULE_DESCRIPTION("Driver support for Atheros AR600x SDIO devices");
MODULE_LICENSE("Dual BSD/GPL");

MODULE_FIRMWARE(AR6003_REV2_OTP_FILE);
MODULE_FIRMWARE(AR6003_REV2_FIRMWARE_FILE);
MODULE_FIRMWARE(AR6003_REV2_PATCH_FILE);
MODULE_FIRMWARE(AR6003_REV2_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6003_REV2_DEFAULT_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6003_REV3_OTP_FILE);
MODULE_FIRMWARE(AR6003_REV3_FIRMWARE_FILE);
MODULE_FIRMWARE(AR6003_REV3_PATCH_FILE);
MODULE_FIRMWARE(AR6003_REV3_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6003_REV3_DEFAULT_BOARD_DATA_FILE);
