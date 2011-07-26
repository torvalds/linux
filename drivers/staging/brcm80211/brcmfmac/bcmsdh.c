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

#include <defs.h>
#include <brcm_hw_ids.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include <soc.h>
#include "dhd.h"
#include "dhd_bus.h"
#include "sdio_host.h"

#define SDIOH_API_ACCESS_RETRY_LIMIT	2

#define BRCMF_SD_ERROR_VAL	0x0001	/* Error */
#define BRCMF_SD_INFO_VAL		0x0002	/* Info */


#ifdef BCMDBG
#define BRCMF_SD_ERROR(x) \
	do { \
		if ((brcmf_sdio_msglevel & BRCMF_SD_ERROR_VAL) && \
		    net_ratelimit()) \
			printk x; \
	} while (0)
#define BRCMF_SD_INFO(x)	\
	do { \
		if ((brcmf_sdio_msglevel & BRCMF_SD_INFO_VAL) && \
		    net_ratelimit()) \
			printk x; \
	} while (0)
#else				/* BCMDBG */
#define BRCMF_SD_ERROR(x)
#define BRCMF_SD_INFO(x)
#endif				/* BCMDBG */

/* debugging macros */
#define SDLX_MSG(x)

#define SDIOH_CMD_TYPE_NORMAL   0	/* Normal command */
#define SDIOH_CMD_TYPE_APPEND   1	/* Append command */
#define SDIOH_CMD_TYPE_CUTTHRU  2	/* Cut-through command */

#define SDIOH_DATA_PIO          0	/* PIO mode */
#define SDIOH_DATA_DMA          1	/* DMA mode */

struct brcmf_sdio_card {
	bool init_success;	/* underlying driver successfully attached */
	void *sdioh;		/* handler for sdioh */
	u32 vendevid;	/* Target Vendor and Device ID on SD bus */
	bool regfail;		/* Save status of last
				 reg_read/reg_write call */
	u32 sbwad;		/* Save backplane window address */
};

/**
 * SDIO Host Controller info
 */
struct sdio_hc {
	struct sdio_hc *next;
	struct device *dev;	/* platform device handle */
	void *regs;		/* SDIO Host Controller address */
	struct brcmf_sdio_card *card;
	void *ch;
	unsigned int oob_irq;
	unsigned long oob_flags;	/* OOB Host specifiction
					as edge and etc */
	bool oob_irq_registered;
};

/* local copy of bcm sd handler */
static struct brcmf_sdio_card *l_card;

const uint brcmf_sdio_msglevel = BRCMF_SD_ERROR_VAL;

static struct sdio_hc *sdhcinfo;

/* driver info, initialized when brcmf_sdio_register is called */
static struct brcmf_sdioh_driver drvinfo = { NULL, NULL };

/* Module parameters specific to each host-controller driver */

module_param(sd_msglevel, uint, 0);

extern uint sd_f2_blocksize;
module_param(sd_f2_blocksize, int, 0);

/* forward declarations */
int brcmf_sdio_probe(struct device *dev);
EXPORT_SYMBOL(brcmf_sdio_probe);

int brcmf_sdio_remove(struct device *dev);
EXPORT_SYMBOL(brcmf_sdio_remove);

struct brcmf_sdio_card*
brcmf_sdcard_attach(void *cfghdl, u32 *regsva, uint irq)
{
	struct brcmf_sdio_card *card;

	card = kzalloc(sizeof(struct brcmf_sdio_card), GFP_ATOMIC);
	if (card == NULL) {
		BRCMF_SD_ERROR(("sdcard_attach: out of memory"));
		return NULL;
	}

	/* save the handler locally */
	l_card = card;

	card->sdioh = brcmf_sdioh_attach(cfghdl, irq);
	if (!card->sdioh) {
		brcmf_sdcard_detach(card);
		return NULL;
	}

	card->init_success = true;

	*regsva = SI_ENUM_BASE;

	/* Report the BAR, to fix if needed */
	card->sbwad = SI_ENUM_BASE;
	return card;
}

int brcmf_sdcard_detach(struct brcmf_sdio_card *card)
{
	if (card != NULL) {
		if (card->sdioh) {
			brcmf_sdioh_detach(card->sdioh);
			card->sdioh = NULL;
		}
		kfree(card);
	}

	l_card = NULL;
	return 0;
}

int
brcmf_sdcard_iovar_op(struct brcmf_sdio_card *card, const char *name,
		void *params, int plen, void *arg, int len, bool set)
{
	return brcmf_sdioh_iovar_op(card->sdioh, name, params, plen, arg,
				    len, set);
}

int brcmf_sdcard_intr_enable(struct brcmf_sdio_card *card)
{
	return brcmf_sdioh_interrupt_set(card->sdioh, true);
}

int brcmf_sdcard_intr_disable(struct brcmf_sdio_card *card)
{
	return brcmf_sdioh_interrupt_set(card->sdioh, false);
}

int brcmf_sdcard_intr_reg(struct brcmf_sdio_card *card,
			  void (*fn)(void *), void *argh)
{
	return brcmf_sdioh_interrupt_register(card->sdioh, fn, argh);
}

int brcmf_sdcard_intr_dereg(struct brcmf_sdio_card *card)
{
	return brcmf_sdioh_interrupt_deregister(card->sdioh);
}

u8 brcmf_sdcard_cfg_read(struct brcmf_sdio_card *card, uint fnc_num, u32 addr,
			 int *err)
{
	int status;
	s32 retry = 0;
	u8 data = 0;

	if (!card)
		card = l_card;

	do {
		if (retry)	/* wait for 1 ms till bus get settled down */
			udelay(1000);
		status =
		    brcmf_sdioh_cfg_read(card->sdioh, fnc_num, addr,
				   (u8 *) &data);
	} while (status != 0
		 && (retry++ < SDIOH_API_ACCESS_RETRY_LIMIT));
	if (err)
		*err = status;

	BRCMF_SD_INFO(("%s:fun = %d, addr = 0x%x, u8data = 0x%x\n",
		     __func__, fnc_num, addr, data));

	return data;
}

void
brcmf_sdcard_cfg_write(struct brcmf_sdio_card *card, uint fnc_num, u32 addr,
		       u8 data, int *err)
{
	int status;
	s32 retry = 0;

	if (!card)
		card = l_card;

	do {
		if (retry)	/* wait for 1 ms till bus get settled down */
			udelay(1000);
		status =
		    brcmf_sdioh_cfg_write(card->sdioh, fnc_num, addr,
				    (u8 *) &data);
	} while (status != 0
		 && (retry++ < SDIOH_API_ACCESS_RETRY_LIMIT));
	if (err)
		*err = status;

	BRCMF_SD_INFO(("%s:fun = %d, addr = 0x%x, u8data = 0x%x\n",
		     __func__, fnc_num, addr, data));
}

u32 brcmf_sdcard_cfg_read_word(struct brcmf_sdio_card *card, uint fnc_num,
			       u32 addr, int *err)
{
	int status;
	u32 data = 0;

	if (!card)
		card = l_card;

	status = brcmf_sdioh_request_word(card->sdioh, SDIOH_CMD_TYPE_NORMAL,
		SDIOH_READ, fnc_num, addr, &data, 4);

	if (err)
		*err = status;

	BRCMF_SD_INFO(("%s:fun = %d, addr = 0x%x, u32data = 0x%x\n",
		     __func__, fnc_num, addr, data));

	return data;
}

void
brcmf_sdcard_cfg_write_word(struct brcmf_sdio_card *card, uint fnc_num,
			    u32 addr, u32 data, int *err)
{
	int status;

	if (!card)
		card = l_card;

	status =
	    brcmf_sdioh_request_word(card->sdioh, SDIOH_CMD_TYPE_NORMAL,
			       SDIOH_WRITE, fnc_num, addr, &data, 4);

	if (err)
		*err = status;

	BRCMF_SD_INFO(("%s:fun = %d, addr = 0x%x, u32data = 0x%x\n",
		     __func__, fnc_num, addr, data));
}

int brcmf_sdcard_cis_read(struct brcmf_sdio_card *card, uint func, u8 * cis,
			  uint length)
{
	int status;

	u8 *tmp_buf, *tmp_ptr;
	u8 *ptr;
	bool ascii = func & ~0xf;
	func &= 0x7;

	if (!card)
		card = l_card;

	status = brcmf_sdioh_cis_read(card->sdioh, func, cis, length);

	if (ascii) {
		/* Move binary bits to tmp and format them
			 into the provided buffer. */
		tmp_buf = kmalloc(length, GFP_ATOMIC);
		if (tmp_buf == NULL) {
			BRCMF_SD_ERROR(("%s: out of memory\n", __func__));
			return -ENOMEM;
		}
		memcpy(tmp_buf, cis, length);
		for (tmp_ptr = tmp_buf, ptr = cis; ptr < (cis + length - 4);
		     tmp_ptr++) {
			ptr += sprintf((char *)ptr, "%.2x ", *tmp_ptr & 0xff);
			if ((((tmp_ptr - tmp_buf) + 1) & 0xf) == 0)
				ptr += sprintf((char *)ptr, "\n");
		}
		kfree(tmp_buf);
	}

	return status;
}

static int
brcmf_sdcard_set_sbaddr_window(struct brcmf_sdio_card *card, u32 address)
{
	int err = 0;
	brcmf_sdcard_cfg_write(card, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRLOW,
			 (address >> 8) & SBSDIO_SBADDRLOW_MASK, &err);
	if (!err)
		brcmf_sdcard_cfg_write(card, SDIO_FUNC_1,
				       SBSDIO_FUNC1_SBADDRMID,
				       (address >> 16) & SBSDIO_SBADDRMID_MASK,
				       &err);
	if (!err)
		brcmf_sdcard_cfg_write(card, SDIO_FUNC_1,
				       SBSDIO_FUNC1_SBADDRHIGH,
				       (address >> 24) & SBSDIO_SBADDRHIGH_MASK,
				       &err);

	return err;
}

u32 brcmf_sdcard_reg_read(struct brcmf_sdio_card *card, u32 addr, uint size)
{
	int status;
	u32 word = 0;
	uint bar0 = addr & ~SBSDIO_SB_OFT_ADDR_MASK;

	BRCMF_SD_INFO(("%s:fun = 1, addr = 0x%x, ", __func__, addr));

	if (!card)
		card = l_card;

	if (bar0 != card->sbwad) {
		if (brcmf_sdcard_set_sbaddr_window(card, bar0))
			return 0xFFFFFFFF;

		card->sbwad = bar0;
	}

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	if (size == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	status = brcmf_sdioh_request_word(card->sdioh, SDIOH_CMD_TYPE_NORMAL,
				    SDIOH_READ, SDIO_FUNC_1, addr, &word, size);

	card->regfail = (status != 0);

	BRCMF_SD_INFO(("u32data = 0x%x\n", word));

	/* if ok, return appropriately masked word */
	if (status == 0) {
		switch (size) {
		case sizeof(u8):
			return word & 0xff;
		case sizeof(u16):
			return word & 0xffff;
		case sizeof(u32):
			return word;
		default:
			card->regfail = true;

		}
	}

	/* otherwise, bad sdio access or invalid size */
	BRCMF_SD_ERROR(("%s: error reading addr 0x%04x size %d\n", __func__,
		      addr, size));
	return 0xFFFFFFFF;
}

u32 brcmf_sdcard_reg_write(struct brcmf_sdio_card *card, u32 addr, uint size,
			   u32 data)
{
	int status;
	uint bar0 = addr & ~SBSDIO_SB_OFT_ADDR_MASK;
	int err = 0;

	BRCMF_SD_INFO(("%s:fun = 1, addr = 0x%x, uint%ddata = 0x%x\n",
		     __func__, addr, size * 8, data));

	if (!card)
		card = l_card;

	if (bar0 != card->sbwad) {
		err = brcmf_sdcard_set_sbaddr_window(card, bar0);
		if (err)
			return err;

		card->sbwad = bar0;
	}

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	if (size == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;
	status =
	    brcmf_sdioh_request_word(card->sdioh, SDIOH_CMD_TYPE_NORMAL,
			       SDIOH_WRITE, SDIO_FUNC_1, addr, &data, size);
	card->regfail = (status != 0);

	if (status == 0)
		return 0;

	BRCMF_SD_ERROR(("%s: error writing 0x%08x to addr 0x%04x size %d\n",
		      __func__, data, addr, size));
	return 0xFFFFFFFF;
}

bool brcmf_sdcard_regfail(struct brcmf_sdio_card *card)
{
	return card->regfail;
}

int
brcmf_sdcard_recv_buf(struct brcmf_sdio_card *card, u32 addr, uint fn,
		      uint flags,
		      u8 *buf, uint nbytes, struct sk_buff *pkt,
		      void (*complete)(void *handle, int status,
				       bool sync_waiting),
		      void *handle)
{
	int status;
	uint incr_fix;
	uint width;
	uint bar0 = addr & ~SBSDIO_SB_OFT_ADDR_MASK;
	int err = 0;

	BRCMF_SD_INFO(("%s:fun = %d, addr = 0x%x, size = %d\n",
		     __func__, fn, addr, nbytes));

	/* Async not implemented yet */
	if (flags & SDIO_REQ_ASYNC)
		return -ENOTSUPP;

	if (bar0 != card->sbwad) {
		err = brcmf_sdcard_set_sbaddr_window(card, bar0);
		if (err)
			return err;

		card->sbwad = bar0;
	}

	addr &= SBSDIO_SB_OFT_ADDR_MASK;

	incr_fix = (flags & SDIO_REQ_FIXED) ? SDIOH_DATA_FIX : SDIOH_DATA_INC;
	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	if (width == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	status = brcmf_sdioh_request_buffer(card->sdioh, SDIOH_DATA_PIO,
		incr_fix, SDIOH_READ, fn, addr, width, nbytes, buf, pkt);

	return status;
}

int
brcmf_sdcard_send_buf(struct brcmf_sdio_card *card, u32 addr, uint fn,
		      uint flags, u8 *buf, uint nbytes, void *pkt,
		      void (*complete)(void *handle, int status,
				       bool sync_waiting),
		      void *handle)
{
	uint incr_fix;
	uint width;
	uint bar0 = addr & ~SBSDIO_SB_OFT_ADDR_MASK;
	int err = 0;

	BRCMF_SD_INFO(("%s:fun = %d, addr = 0x%x, size = %d\n",
		     __func__, fn, addr, nbytes));

	/* Async not implemented yet */
	if (flags & SDIO_REQ_ASYNC)
		return -ENOTSUPP;

	if (bar0 != card->sbwad) {
		err = brcmf_sdcard_set_sbaddr_window(card, bar0);
		if (err)
			return err;

		card->sbwad = bar0;
	}

	addr &= SBSDIO_SB_OFT_ADDR_MASK;

	incr_fix = (flags & SDIO_REQ_FIXED) ? SDIOH_DATA_FIX : SDIOH_DATA_INC;
	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	if (width == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	return brcmf_sdioh_request_buffer(card->sdioh, SDIOH_DATA_PIO,
		incr_fix, SDIOH_WRITE, fn, addr, width, nbytes, buf, pkt);
}

int brcmf_sdcard_rwdata(struct brcmf_sdio_card *card, uint rw, u32 addr,
			u8 *buf, uint nbytes)
{
	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	return brcmf_sdioh_request_buffer(card->sdioh, SDIOH_DATA_PIO,
		SDIOH_DATA_INC, (rw ? SDIOH_WRITE : SDIOH_READ), SDIO_FUNC_1,
		addr, 4, nbytes, buf, NULL);
}

int brcmf_sdcard_abort(struct brcmf_sdio_card *card, uint fn)
{
	return brcmf_sdioh_abort(card->sdioh, fn);
}

int brcmf_sdcard_query_device(struct brcmf_sdio_card *card)
{
	card->vendevid = (PCI_VENDOR_ID_BROADCOM << 16) | 0;
	return card->vendevid;
}

u32 brcmf_sdcard_cur_sbwad(struct brcmf_sdio_card *card)
{
	if (!card)
		card = l_card;

	return card->sbwad;
}

int brcmf_sdio_probe(struct device *dev)
{
	struct sdio_hc *sdhc = NULL;
	u32 regs = 0;
	struct brcmf_sdio_card *card = NULL;
	int irq = 0;
	u32 vendevid;
	unsigned long irq_flags = 0;

	/* allocate SDIO Host Controller state info */
	sdhc = kzalloc(sizeof(struct sdio_hc), GFP_ATOMIC);
	if (!sdhc) {
		SDLX_MSG(("%s: out of memory\n", __func__));
		goto err;
	}
	sdhc->dev = (void *)dev;

	card = brcmf_sdcard_attach((void *)0, &regs, irq);
	if (!card) {
		SDLX_MSG(("%s: attach failed\n", __func__));
		goto err;
	}

	sdhc->card = card;
	sdhc->oob_irq = irq;
	sdhc->oob_flags = irq_flags;
	sdhc->oob_irq_registered = false;	/* to make sure.. */

	/* chain SDIO Host Controller info together */
	sdhc->next = sdhcinfo;
	sdhcinfo = sdhc;
	/* Read the vendor/device ID from the CIS */
	vendevid = brcmf_sdcard_query_device(card);

	/* try to attach to the target device */
	sdhc->ch = drvinfo.attach((vendevid >> 16), (vendevid & 0xFFFF),
				  0, 0, 0, 0, regs, card);
	if (!sdhc->ch) {
		SDLX_MSG(("%s: device attach failed\n", __func__));
		goto err;
	}

	return 0;

	/* error handling */
err:
	if (sdhc) {
		if (sdhc->card)
			brcmf_sdcard_detach(sdhc->card);
		kfree(sdhc);
	}

	return -ENODEV;
}

int brcmf_sdio_remove(struct device *dev)
{
	struct sdio_hc *sdhc, *prev;

	sdhc = sdhcinfo;
	drvinfo.detach(sdhc->ch);
	brcmf_sdcard_detach(sdhc->card);
	/* find the SDIO Host Controller state for this pdev
		 and take it out from the list */
	for (sdhc = sdhcinfo, prev = NULL; sdhc; sdhc = sdhc->next) {
		if (sdhc->dev == (void *)dev) {
			if (prev)
				prev->next = sdhc->next;
			else
				sdhcinfo = NULL;
			break;
		}
		prev = sdhc;
	}
	if (!sdhc) {
		SDLX_MSG(("%s: failed\n", __func__));
		return 0;
	}

	/* release SDIO Host Controller info */
	kfree(sdhc);
	return 0;
}

int brcmf_sdio_register(struct brcmf_sdioh_driver *driver)
{
	drvinfo = *driver;

	SDLX_MSG(("Linux Kernel SDIO/MMC Driver\n"));
	return brcmf_sdio_function_init();
}

void brcmf_sdio_unregister(void)
{
	brcmf_sdio_function_cleanup();
}

void brcmf_sdio_wdtmr_enable(bool enable)
{
	if (enable)
		brcmf_sdbrcm_wd_timer(sdhcinfo->ch, brcmf_watchdog_ms);
	else
		brcmf_sdbrcm_wd_timer(sdhcinfo->ch, 0);
}
