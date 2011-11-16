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
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/core.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/card.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/sched.h>	/* request_irq() */
#include <linux/module.h>
#include <net/cfg80211.h>

#include <defs.h>
#include <brcm_hw_ids.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include "sdio_host.h"
#include "dhd.h"
#include "dhd_dbg.h"
#include "wl_cfg80211.h"

#define SDIO_VENDOR_ID_BROADCOM		0x02d0

#define DMA_ALIGN_MASK	0x03

#define SDIO_DEVICE_ID_BROADCOM_4329	0x4329

#define SDIO_FUNC1_BLOCKSIZE		64
#define SDIO_FUNC2_BLOCKSIZE		512

/* devices we support, null terminated */
static const struct sdio_device_id brcmf_sdmmc_ids[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_4329)},
	{ /* end: all zeroes */ },
};
MODULE_DEVICE_TABLE(sdio, brcmf_sdmmc_ids);

static bool
brcmf_pm_resume_error(struct brcmf_sdio_dev *sdiodev)
{
	bool is_err = false;
#ifdef CONFIG_PM_SLEEP
	is_err = atomic_read(&sdiodev->suspend);
#endif
	return is_err;
}

static void
brcmf_pm_resume_wait(struct brcmf_sdio_dev *sdiodev, wait_queue_head_t *wq)
{
#ifdef CONFIG_PM_SLEEP
	int retry = 0;
	while (atomic_read(&sdiodev->suspend) && retry++ != 30)
		wait_event_timeout(*wq, false, HZ/100);
#endif
}

static inline int brcmf_sdioh_f0_write_byte(struct brcmf_sdio_dev *sdiodev,
					    uint regaddr, u8 *byte)
{
	struct sdio_func *sdfunc = sdiodev->func[0];
	int err_ret;

	/*
	 * Can only directly write to some F0 registers.
	 * Handle F2 enable/disable and Abort command
	 * as a special case.
	 */
	if (regaddr == SDIO_CCCR_IOEx) {
		sdfunc = sdiodev->func[2];
		if (sdfunc) {
			sdio_claim_host(sdfunc);
			if (*byte & SDIO_FUNC_ENABLE_2) {
				/* Enable Function 2 */
				err_ret = sdio_enable_func(sdfunc);
				if (err_ret)
					brcmf_dbg(ERROR,
						  "enable F2 failed:%d\n",
						  err_ret);
			} else {
				/* Disable Function 2 */
				err_ret = sdio_disable_func(sdfunc);
				if (err_ret)
					brcmf_dbg(ERROR,
						  "Disable F2 failed:%d\n",
						  err_ret);
			}
			sdio_release_host(sdfunc);
		}
	} else if (regaddr == SDIO_CCCR_ABORT) {
		sdio_claim_host(sdfunc);
		sdio_writeb(sdfunc, *byte, regaddr, &err_ret);
		sdio_release_host(sdfunc);
	} else if (regaddr < 0xF0) {
		brcmf_dbg(ERROR, "F0 Wr:0x%02x: write disallowed\n", regaddr);
		err_ret = -EPERM;
	} else {
		sdio_claim_host(sdfunc);
		sdio_f0_writeb(sdfunc, *byte, regaddr, &err_ret);
		sdio_release_host(sdfunc);
	}

	return err_ret;
}

int brcmf_sdioh_request_byte(struct brcmf_sdio_dev *sdiodev, uint rw, uint func,
			     uint regaddr, u8 *byte)
{
	int err_ret;

	brcmf_dbg(INFO, "rw=%d, func=%d, addr=0x%05x\n", rw, func, regaddr);

	brcmf_pm_resume_wait(sdiodev, &sdiodev->request_byte_wait);
	if (brcmf_pm_resume_error(sdiodev))
		return -EIO;

	if (rw && func == 0) {
		/* handle F0 separately */
		err_ret = brcmf_sdioh_f0_write_byte(sdiodev, regaddr, byte);
	} else {
		sdio_claim_host(sdiodev->func[func]);
		if (rw) /* CMD52 Write */
			sdio_writeb(sdiodev->func[func], *byte, regaddr,
				    &err_ret);
		else if (func == 0) {
			*byte = sdio_f0_readb(sdiodev->func[func], regaddr,
					      &err_ret);
		} else {
			*byte = sdio_readb(sdiodev->func[func], regaddr,
					   &err_ret);
		}
		sdio_release_host(sdiodev->func[func]);
	}

	if (err_ret)
		brcmf_dbg(ERROR, "Failed to %s byte F%d:@0x%05x=%02x, Err: %d\n",
			  rw ? "write" : "read", func, regaddr, *byte, err_ret);

	return err_ret;
}

int brcmf_sdioh_request_word(struct brcmf_sdio_dev *sdiodev,
			     uint rw, uint func, uint addr, u32 *word,
			     uint nbytes)
{
	int err_ret = -EIO;

	if (func == 0) {
		brcmf_dbg(ERROR, "Only CMD52 allowed to F0\n");
		return -EINVAL;
	}

	brcmf_dbg(INFO, "rw=%d, func=%d, addr=0x%05x, nbytes=%d\n",
		  rw, func, addr, nbytes);

	brcmf_pm_resume_wait(sdiodev, &sdiodev->request_word_wait);
	if (brcmf_pm_resume_error(sdiodev))
		return -EIO;
	/* Claim host controller */
	sdio_claim_host(sdiodev->func[func]);

	if (rw) {		/* CMD52 Write */
		if (nbytes == 4)
			sdio_writel(sdiodev->func[func], *word, addr,
				    &err_ret);
		else if (nbytes == 2)
			sdio_writew(sdiodev->func[func], (*word & 0xFFFF),
				    addr, &err_ret);
		else
			brcmf_dbg(ERROR, "Invalid nbytes: %d\n", nbytes);
	} else {		/* CMD52 Read */
		if (nbytes == 4)
			*word = sdio_readl(sdiodev->func[func], addr, &err_ret);
		else if (nbytes == 2)
			*word = sdio_readw(sdiodev->func[func], addr,
					   &err_ret) & 0xFFFF;
		else
			brcmf_dbg(ERROR, "Invalid nbytes: %d\n", nbytes);
	}

	/* Release host controller */
	sdio_release_host(sdiodev->func[func]);

	if (err_ret)
		brcmf_dbg(ERROR, "Failed to %s word, Err: 0x%08x\n",
			  rw ? "write" : "read", err_ret);

	return err_ret;
}

static int
brcmf_sdioh_request_packet(struct brcmf_sdio_dev *sdiodev, uint fix_inc,
			   uint write, uint func, uint addr,
			   struct sk_buff *pkt)
{
	bool fifo = (fix_inc == SDIOH_DATA_FIX);
	u32 SGCount = 0;
	int err_ret = 0;

	struct sk_buff *pnext;

	brcmf_dbg(TRACE, "Enter\n");

	brcmf_pm_resume_wait(sdiodev, &sdiodev->request_packet_wait);
	if (brcmf_pm_resume_error(sdiodev))
		return -EIO;

	/* Claim host controller */
	sdio_claim_host(sdiodev->func[func]);
	for (pnext = pkt; pnext; pnext = pnext->next) {
		uint pkt_len = pnext->len;
		pkt_len += 3;
		pkt_len &= 0xFFFFFFFC;

		if ((write) && (!fifo)) {
			err_ret = sdio_memcpy_toio(sdiodev->func[func], addr,
						   ((u8 *) (pnext->data)),
						   pkt_len);
		} else if (write) {
			err_ret = sdio_memcpy_toio(sdiodev->func[func], addr,
						   ((u8 *) (pnext->data)),
						   pkt_len);
		} else if (fifo) {
			err_ret = sdio_readsb(sdiodev->func[func],
					      ((u8 *) (pnext->data)),
					      addr, pkt_len);
		} else {
			err_ret = sdio_memcpy_fromio(sdiodev->func[func],
						     ((u8 *) (pnext->data)),
						     addr, pkt_len);
		}

		if (err_ret) {
			brcmf_dbg(ERROR, "%s FAILED %p[%d], addr=0x%05x, pkt_len=%d, ERR=0x%08x\n",
				  write ? "TX" : "RX", pnext, SGCount, addr,
				  pkt_len, err_ret);
		} else {
			brcmf_dbg(TRACE, "%s xfr'd %p[%d], addr=0x%05x, len=%d\n",
				  write ? "TX" : "RX", pnext, SGCount, addr,
				  pkt_len);
		}

		if (!fifo)
			addr += pkt_len;
		SGCount++;

	}

	/* Release host controller */
	sdio_release_host(sdiodev->func[func]);

	brcmf_dbg(TRACE, "Exit\n");
	return err_ret;
}

/*
 * This function takes a buffer or packet, and fixes everything up
 * so that in the end, a DMA-able packet is created.
 *
 * A buffer does not have an associated packet pointer,
 * and may or may not be aligned.
 * A packet may consist of a single packet, or a packet chain.
 * If it is a packet chain, then all the packets in the chain
 * must be properly aligned.
 *
 * If the packet data is not aligned, then there may only be
 * one packet, and in this case,  it is copied to a new
 * aligned packet.
 *
 */
int brcmf_sdioh_request_buffer(struct brcmf_sdio_dev *sdiodev,
			       uint fix_inc, uint write, uint func, uint addr,
			       uint reg_width, uint buflen_u, u8 *buffer,
			       struct sk_buff *pkt)
{
	int Status;
	struct sk_buff *mypkt = NULL;

	brcmf_dbg(TRACE, "Enter\n");

	brcmf_pm_resume_wait(sdiodev, &sdiodev->request_buffer_wait);
	if (brcmf_pm_resume_error(sdiodev))
		return -EIO;
	/* Case 1: we don't have a packet. */
	if (pkt == NULL) {
		brcmf_dbg(DATA, "Creating new %s Packet, len=%d\n",
			  write ? "TX" : "RX", buflen_u);
		mypkt = brcmu_pkt_buf_get_skb(buflen_u);
		if (!mypkt) {
			brcmf_dbg(ERROR, "brcmu_pkt_buf_get_skb failed: len %d\n",
				  buflen_u);
			return -EIO;
		}

		/* For a write, copy the buffer data into the packet. */
		if (write)
			memcpy(mypkt->data, buffer, buflen_u);

		Status = brcmf_sdioh_request_packet(sdiodev, fix_inc, write,
						    func, addr, mypkt);

		/* For a read, copy the packet data back to the buffer. */
		if (!write)
			memcpy(buffer, mypkt->data, buflen_u);

		brcmu_pkt_buf_free_skb(mypkt);
	} else if (((ulong) (pkt->data) & DMA_ALIGN_MASK) != 0) {
		/*
		 * Case 2: We have a packet, but it is unaligned.
		 * In this case, we cannot have a chain (pkt->next == NULL)
		 */
		brcmf_dbg(DATA, "Creating aligned %s Packet, len=%d\n",
			  write ? "TX" : "RX", pkt->len);
		mypkt = brcmu_pkt_buf_get_skb(pkt->len);
		if (!mypkt) {
			brcmf_dbg(ERROR, "brcmu_pkt_buf_get_skb failed: len %d\n",
				  pkt->len);
			return -EIO;
		}

		/* For a write, copy the buffer data into the packet. */
		if (write)
			memcpy(mypkt->data, pkt->data, pkt->len);

		Status = brcmf_sdioh_request_packet(sdiodev, fix_inc, write,
						    func, addr, mypkt);

		/* For a read, copy the packet data back to the buffer. */
		if (!write)
			memcpy(pkt->data, mypkt->data, mypkt->len);

		brcmu_pkt_buf_free_skb(mypkt);
	} else {		/* case 3: We have a packet and
				 it is aligned. */
		brcmf_dbg(DATA, "Aligned %s Packet, direct DMA\n",
			  write ? "Tx" : "Rx");
		Status = brcmf_sdioh_request_packet(sdiodev, fix_inc, write,
						    func, addr, pkt);
	}

	return Status;
}

/* Read client card reg */
static int
brcmf_sdioh_card_regread(struct brcmf_sdio_dev *sdiodev, int func, u32 regaddr,
			 int regsize, u32 *data)
{

	if ((func == 0) || (regsize == 1)) {
		u8 temp = 0;

		brcmf_sdioh_request_byte(sdiodev, SDIOH_READ, func, regaddr,
					 &temp);
		*data = temp;
		*data &= 0xff;
		brcmf_dbg(DATA, "byte read data=0x%02x\n", *data);
	} else {
		brcmf_sdioh_request_word(sdiodev, SDIOH_READ, func, regaddr,
					 data, regsize);
		if (regsize == 2)
			*data &= 0xffff;

		brcmf_dbg(DATA, "word read data=0x%08x\n", *data);
	}

	return SUCCESS;
}

static int brcmf_sdioh_get_cisaddr(struct brcmf_sdio_dev *sdiodev, u32 regaddr)
{
	/* read 24 bits and return valid 17 bit addr */
	int i;
	u32 scratch, regdata;
	__le32 scratch_le;
	u8 *ptr = (u8 *)&scratch_le;

	for (i = 0; i < 3; i++) {
		if ((brcmf_sdioh_card_regread(sdiodev, 0, regaddr, 1,
				&regdata)) != SUCCESS)
			brcmf_dbg(ERROR, "Can't read!\n");

		*ptr++ = (u8) regdata;
		regaddr++;
	}

	/* Only the lower 17-bits are valid */
	scratch = le32_to_cpu(scratch_le);
	scratch &= 0x0001FFFF;
	return scratch;
}

static int brcmf_sdioh_enablefuncs(struct brcmf_sdio_dev *sdiodev)
{
	int err_ret;
	u32 fbraddr;
	u8 func;

	brcmf_dbg(TRACE, "\n");

	/* Get the Card's common CIS address */
	sdiodev->func_cis_ptr[0] = brcmf_sdioh_get_cisaddr(sdiodev,
							   SDIO_CCCR_CIS);
	brcmf_dbg(INFO, "Card's Common CIS Ptr = 0x%x\n",
		  sdiodev->func_cis_ptr[0]);

	/* Get the Card's function CIS (for each function) */
	for (fbraddr = SDIO_FBR_BASE(1), func = 1;
	     func <= sdiodev->num_funcs; func++, fbraddr += SDIOD_FBR_SIZE) {
		sdiodev->func_cis_ptr[func] =
		    brcmf_sdioh_get_cisaddr(sdiodev, SDIO_FBR_CIS + fbraddr);
		brcmf_dbg(INFO, "Function %d CIS Ptr = 0x%x\n",
			  func, sdiodev->func_cis_ptr[func]);
	}

	/* Enable Function 1 */
	sdio_claim_host(sdiodev->func[1]);
	err_ret = sdio_enable_func(sdiodev->func[1]);
	sdio_release_host(sdiodev->func[1]);
	if (err_ret)
		brcmf_dbg(ERROR, "Failed to enable F1 Err: 0x%08x\n", err_ret);

	return false;
}

/*
 *	Public entry points & extern's
 */
int brcmf_sdioh_attach(struct brcmf_sdio_dev *sdiodev)
{
	int err_ret = 0;

	brcmf_dbg(TRACE, "\n");

	sdiodev->num_funcs = 2;

	sdio_claim_host(sdiodev->func[1]);
	err_ret = sdio_set_block_size(sdiodev->func[1], SDIO_FUNC1_BLOCKSIZE);
	sdio_release_host(sdiodev->func[1]);
	if (err_ret) {
		brcmf_dbg(ERROR, "Failed to set F1 blocksize\n");
		goto out;
	}

	sdio_claim_host(sdiodev->func[2]);
	err_ret = sdio_set_block_size(sdiodev->func[2], SDIO_FUNC2_BLOCKSIZE);
	sdio_release_host(sdiodev->func[2]);
	if (err_ret) {
		brcmf_dbg(ERROR, "Failed to set F2 blocksize\n");
		goto out;
	}

	brcmf_sdioh_enablefuncs(sdiodev);

out:
	brcmf_dbg(TRACE, "Done\n");
	return err_ret;
}

void brcmf_sdioh_detach(struct brcmf_sdio_dev *sdiodev)
{
	brcmf_dbg(TRACE, "\n");

	/* Disable Function 2 */
	sdio_claim_host(sdiodev->func[2]);
	sdio_disable_func(sdiodev->func[2]);
	sdio_release_host(sdiodev->func[2]);

	/* Disable Function 1 */
	sdio_claim_host(sdiodev->func[1]);
	sdio_disable_func(sdiodev->func[1]);
	sdio_release_host(sdiodev->func[1]);

}

static int brcmf_ops_sdio_probe(struct sdio_func *func,
			      const struct sdio_device_id *id)
{
	int ret = 0;
	struct brcmf_sdio_dev *sdiodev;
	brcmf_dbg(TRACE, "Enter\n");
	brcmf_dbg(TRACE, "func->class=%x\n", func->class);
	brcmf_dbg(TRACE, "sdio_vendor: 0x%04x\n", func->vendor);
	brcmf_dbg(TRACE, "sdio_device: 0x%04x\n", func->device);
	brcmf_dbg(TRACE, "Function#: 0x%04x\n", func->num);

	if (func->num == 1) {
		if (dev_get_drvdata(&func->card->dev)) {
			brcmf_dbg(ERROR, "card private drvdata occupied\n");
			return -ENXIO;
		}
		sdiodev = kzalloc(sizeof(struct brcmf_sdio_dev), GFP_KERNEL);
		if (!sdiodev)
			return -ENOMEM;
		sdiodev->func[0] = func->card->sdio_func[0];
		sdiodev->func[1] = func;
		dev_set_drvdata(&func->card->dev, sdiodev);

		atomic_set(&sdiodev->suspend, false);
		init_waitqueue_head(&sdiodev->request_byte_wait);
		init_waitqueue_head(&sdiodev->request_word_wait);
		init_waitqueue_head(&sdiodev->request_packet_wait);
		init_waitqueue_head(&sdiodev->request_buffer_wait);
	}

	if (func->num == 2) {
		sdiodev = dev_get_drvdata(&func->card->dev);
		if ((!sdiodev) || (sdiodev->func[1]->card != func->card))
			return -ENODEV;
		sdiodev->func[2] = func;

		brcmf_dbg(TRACE, "F2 found, calling brcmf_sdio_probe...\n");
		ret = brcmf_sdio_probe(sdiodev);
	}

	return ret;
}

static void brcmf_ops_sdio_remove(struct sdio_func *func)
{
	struct brcmf_sdio_dev *sdiodev;
	brcmf_dbg(TRACE, "Enter\n");
	brcmf_dbg(INFO, "func->class=%x\n", func->class);
	brcmf_dbg(INFO, "sdio_vendor: 0x%04x\n", func->vendor);
	brcmf_dbg(INFO, "sdio_device: 0x%04x\n", func->device);
	brcmf_dbg(INFO, "Function#: 0x%04x\n", func->num);

	if (func->num == 2) {
		sdiodev = dev_get_drvdata(&func->card->dev);
		brcmf_dbg(TRACE, "F2 found, calling brcmf_sdio_remove...\n");
		brcmf_sdio_remove(sdiodev);
		dev_set_drvdata(&func->card->dev, NULL);
		kfree(sdiodev);
	}
}

#ifdef CONFIG_PM_SLEEP
static int brcmf_sdio_suspend(struct device *dev)
{
	mmc_pm_flag_t sdio_flags;
	struct brcmf_sdio_dev *sdiodev;
	struct sdio_func *func = dev_to_sdio_func(dev);
	int ret = 0;

	brcmf_dbg(TRACE, "\n");

	sdiodev = dev_get_drvdata(&func->card->dev);

	atomic_set(&sdiodev->suspend, true);

	sdio_flags = sdio_get_host_pm_caps(sdiodev->func[1]);
	if (!(sdio_flags & MMC_PM_KEEP_POWER)) {
		brcmf_dbg(ERROR, "Host can't keep power while suspended\n");
		return -EINVAL;
	}

	ret = sdio_set_host_pm_flags(sdiodev->func[1], MMC_PM_KEEP_POWER);
	if (ret) {
		brcmf_dbg(ERROR, "Failed to set pm_flags\n");
		return ret;
	}

	brcmf_sdio_wdtmr_enable(sdiodev, false);

	return ret;
}

static int brcmf_sdio_resume(struct device *dev)
{
	struct brcmf_sdio_dev *sdiodev;
	struct sdio_func *func = dev_to_sdio_func(dev);

	sdiodev = dev_get_drvdata(&func->card->dev);
	brcmf_sdio_wdtmr_enable(sdiodev, true);
	atomic_set(&sdiodev->suspend, false);
	return 0;
}

static const struct dev_pm_ops brcmf_sdio_pm_ops = {
	.suspend	= brcmf_sdio_suspend,
	.resume		= brcmf_sdio_resume,
};
#endif	/* CONFIG_PM_SLEEP */

static struct sdio_driver brcmf_sdmmc_driver = {
	.probe = brcmf_ops_sdio_probe,
	.remove = brcmf_ops_sdio_remove,
	.name = "brcmfmac",
	.id_table = brcmf_sdmmc_ids,
#ifdef CONFIG_PM_SLEEP
	.drv = {
		.pm = &brcmf_sdio_pm_ops,
	},
#endif	/* CONFIG_PM_SLEEP */
};

/* bus register interface */
int brcmf_bus_register(void)
{
	brcmf_dbg(TRACE, "Enter\n");

	return sdio_register_driver(&brcmf_sdmmc_driver);
}

void brcmf_bus_unregister(void)
{
	brcmf_dbg(TRACE, "Enter\n");

	sdio_unregister_driver(&brcmf_sdmmc_driver);
}
