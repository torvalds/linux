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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/platform_device.h>
#include <net/cfg80211.h>

#include <defs.h>
#include <brcm_hw_ids.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include "sdio_host.h"
#include "dhd_dbg.h"
#include "dhd_bus.h"

#define SDIO_VENDOR_ID_BROADCOM		0x02d0

#define DMA_ALIGN_MASK	0x03

#define SDIO_DEVICE_ID_BROADCOM_4329	0x4329
#define SDIO_DEVICE_ID_BROADCOM_4330	0x4330

#define SDIO_FUNC1_BLOCKSIZE		64
#define SDIO_FUNC2_BLOCKSIZE		512

/* devices we support, null terminated */
static const struct sdio_device_id brcmf_sdmmc_ids[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_4329)},
	{SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_4330)},
	{ /* end: all zeroes */ },
};
MODULE_DEVICE_TABLE(sdio, brcmf_sdmmc_ids);

#ifdef CONFIG_BRCMFMAC_SDIO_OOB
static struct list_head oobirq_lh;
struct brcmf_sdio_oobirq {
	unsigned int irq;
	unsigned long flags;
	struct list_head list;
};
#endif		/* CONFIG_BRCMFMAC_SDIO_OOB */

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
	} else if ((regaddr == SDIO_CCCR_ABORT) ||
		   (regaddr == SDIO_CCCR_IENx)) {
		sdfunc = kmemdup(sdiodev->func[0], sizeof(struct sdio_func),
				 GFP_KERNEL);
		if (!sdfunc)
			return -ENOMEM;
		sdfunc->num = 0;
		sdio_claim_host(sdfunc);
		sdio_writeb(sdfunc, *byte, regaddr, &err_ret);
		sdio_release_host(sdfunc);
		kfree(sdfunc);
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

/* precondition: host controller is claimed */
static int
brcmf_sdioh_request_data(struct brcmf_sdio_dev *sdiodev, uint write, bool fifo,
			 uint func, uint addr, struct sk_buff *pkt, uint pktlen)
{
	int err_ret = 0;

	if ((write) && (!fifo)) {
		err_ret = sdio_memcpy_toio(sdiodev->func[func], addr,
					   ((u8 *) (pkt->data)), pktlen);
	} else if (write) {
		err_ret = sdio_memcpy_toio(sdiodev->func[func], addr,
					   ((u8 *) (pkt->data)), pktlen);
	} else if (fifo) {
		err_ret = sdio_readsb(sdiodev->func[func],
				      ((u8 *) (pkt->data)), addr, pktlen);
	} else {
		err_ret = sdio_memcpy_fromio(sdiodev->func[func],
					     ((u8 *) (pkt->data)),
					     addr, pktlen);
	}

	return err_ret;
}

/*
 * This function takes a queue of packets. The packets on the queue
 * are assumed to be properly aligned by the caller.
 */
int
brcmf_sdioh_request_chain(struct brcmf_sdio_dev *sdiodev, uint fix_inc,
			  uint write, uint func, uint addr,
			  struct sk_buff_head *pktq)
{
	bool fifo = (fix_inc == SDIOH_DATA_FIX);
	u32 SGCount = 0;
	int err_ret = 0;

	struct sk_buff *pkt;

	brcmf_dbg(TRACE, "Enter\n");

	brcmf_pm_resume_wait(sdiodev, &sdiodev->request_chain_wait);
	if (brcmf_pm_resume_error(sdiodev))
		return -EIO;

	/* Claim host controller */
	sdio_claim_host(sdiodev->func[func]);

	skb_queue_walk(pktq, pkt) {
		uint pkt_len = pkt->len;
		pkt_len += 3;
		pkt_len &= 0xFFFFFFFC;

		err_ret = brcmf_sdioh_request_data(sdiodev, write, fifo, func,
						   addr, pkt, pkt_len);
		if (err_ret) {
			brcmf_dbg(ERROR, "%s FAILED %p[%d], addr=0x%05x, pkt_len=%d, ERR=0x%08x\n",
				  write ? "TX" : "RX", pkt, SGCount, addr,
				  pkt_len, err_ret);
		} else {
			brcmf_dbg(TRACE, "%s xfr'd %p[%d], addr=0x%05x, len=%d\n",
				  write ? "TX" : "RX", pkt, SGCount, addr,
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
 * This function takes a single DMA-able packet.
 */
int brcmf_sdioh_request_buffer(struct brcmf_sdio_dev *sdiodev,
			       uint fix_inc, uint write, uint func, uint addr,
			       struct sk_buff *pkt)
{
	int status;
	uint pkt_len;
	bool fifo = (fix_inc == SDIOH_DATA_FIX);

	brcmf_dbg(TRACE, "Enter\n");

	if (pkt == NULL)
		return -EINVAL;
	pkt_len = pkt->len;

	brcmf_pm_resume_wait(sdiodev, &sdiodev->request_buffer_wait);
	if (brcmf_pm_resume_error(sdiodev))
		return -EIO;

	/* Claim host controller */
	sdio_claim_host(sdiodev->func[func]);

	pkt_len += 3;
	pkt_len &= (uint)~3;

	status = brcmf_sdioh_request_data(sdiodev, write, fifo, func,
					   addr, pkt, pkt_len);
	if (status) {
		brcmf_dbg(ERROR, "%s FAILED %p, addr=0x%05x, pkt_len=%d, ERR=0x%08x\n",
			  write ? "TX" : "RX", pkt, addr, pkt_len, status);
	} else {
		brcmf_dbg(TRACE, "%s xfr'd %p, addr=0x%05x, len=%d\n",
			  write ? "TX" : "RX", pkt, addr, pkt_len);
	}

	/* Release host controller */
	sdio_release_host(sdiodev->func[func]);

	return status;
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

#ifdef CONFIG_BRCMFMAC_SDIO_OOB
static int brcmf_sdio_getintrcfg(struct brcmf_sdio_dev *sdiodev)
{
	struct brcmf_sdio_oobirq *oobirq_entry;

	if (list_empty(&oobirq_lh)) {
		brcmf_dbg(ERROR, "no valid oob irq resource\n");
		return -ENXIO;
	}

	oobirq_entry = list_first_entry(&oobirq_lh, struct brcmf_sdio_oobirq,
					list);

	sdiodev->irq = oobirq_entry->irq;
	sdiodev->irq_flags = oobirq_entry->flags;
	list_del(&oobirq_entry->list);
	kfree(oobirq_entry);

	return 0;
}
#else
static inline int brcmf_sdio_getintrcfg(struct brcmf_sdio_dev *sdiodev)
{
	return 0;
}
#endif		/* CONFIG_BRCMFMAC_SDIO_OOB */

static int brcmf_ops_sdio_probe(struct sdio_func *func,
			      const struct sdio_device_id *id)
{
	int ret = 0;
	struct brcmf_sdio_dev *sdiodev;
	struct brcmf_bus *bus_if;

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
		bus_if = kzalloc(sizeof(struct brcmf_bus), GFP_KERNEL);
		if (!bus_if)
			return -ENOMEM;
		sdiodev = kzalloc(sizeof(struct brcmf_sdio_dev), GFP_KERNEL);
		if (!sdiodev) {
			kfree(bus_if);
			return -ENOMEM;
		}
		sdiodev->func[0] = func;
		sdiodev->func[1] = func;
		sdiodev->bus_if = bus_if;
		bus_if->bus_priv.sdio = sdiodev;
		bus_if->type = SDIO_BUS;
		bus_if->align = BRCMF_SDALIGN;
		dev_set_drvdata(&func->card->dev, sdiodev);

		atomic_set(&sdiodev->suspend, false);
		init_waitqueue_head(&sdiodev->request_byte_wait);
		init_waitqueue_head(&sdiodev->request_word_wait);
		init_waitqueue_head(&sdiodev->request_chain_wait);
		init_waitqueue_head(&sdiodev->request_buffer_wait);
	}

	if (func->num == 2) {
		sdiodev = dev_get_drvdata(&func->card->dev);
		if ((!sdiodev) || (sdiodev->func[1]->card != func->card))
			return -ENODEV;

		ret = brcmf_sdio_getintrcfg(sdiodev);
		if (ret)
			return ret;
		sdiodev->func[2] = func;

		bus_if = sdiodev->bus_if;
		sdiodev->dev = &func->dev;
		dev_set_drvdata(&func->dev, bus_if);

		brcmf_dbg(TRACE, "F2 found, calling brcmf_sdio_probe...\n");
		ret = brcmf_sdio_probe(sdiodev);
	}

	return ret;
}

static void brcmf_ops_sdio_remove(struct sdio_func *func)
{
	struct brcmf_bus *bus_if;
	struct brcmf_sdio_dev *sdiodev;
	brcmf_dbg(TRACE, "Enter\n");
	brcmf_dbg(INFO, "func->class=%x\n", func->class);
	brcmf_dbg(INFO, "sdio_vendor: 0x%04x\n", func->vendor);
	brcmf_dbg(INFO, "sdio_device: 0x%04x\n", func->device);
	brcmf_dbg(INFO, "Function#: 0x%04x\n", func->num);

	if (func->num == 2) {
		bus_if = dev_get_drvdata(&func->dev);
		sdiodev = bus_if->bus_priv.sdio;
		brcmf_dbg(TRACE, "F2 found, calling brcmf_sdio_remove...\n");
		brcmf_sdio_remove(sdiodev);
		dev_set_drvdata(&func->card->dev, NULL);
		dev_set_drvdata(&func->dev, NULL);
		kfree(bus_if);
		kfree(sdiodev);
	}
}

#ifdef CONFIG_PM_SLEEP
static int brcmf_sdio_suspend(struct device *dev)
{
	mmc_pm_flag_t sdio_flags;
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct brcmf_sdio_dev *sdiodev = dev_get_drvdata(&func->card->dev);
	int ret = 0;

	brcmf_dbg(TRACE, "\n");

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
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct brcmf_sdio_dev *sdiodev = dev_get_drvdata(&func->card->dev);

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

#ifdef CONFIG_BRCMFMAC_SDIO_OOB
static int brcmf_sdio_pd_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct brcmf_sdio_oobirq *oobirq_entry;
	int i, ret;

	INIT_LIST_HEAD(&oobirq_lh);

	for (i = 0; ; i++) {
		res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		if (!res)
			break;

		oobirq_entry = kzalloc(sizeof(struct brcmf_sdio_oobirq),
				       GFP_KERNEL);
		oobirq_entry->irq = res->start;
		oobirq_entry->flags = res->flags & IRQF_TRIGGER_MASK;
		list_add_tail(&oobirq_entry->list, &oobirq_lh);
	}
	if (i == 0)
		return -ENXIO;

	ret = sdio_register_driver(&brcmf_sdmmc_driver);

	if (ret)
		brcmf_dbg(ERROR, "sdio_register_driver failed: %d\n", ret);

	return ret;
}

static struct platform_driver brcmf_sdio_pd = {
	.probe		= brcmf_sdio_pd_probe,
	.driver		= {
		.name	= "brcmf_sdio_pd"
	}
};

void brcmf_sdio_exit(void)
{
	brcmf_dbg(TRACE, "Enter\n");

	sdio_unregister_driver(&brcmf_sdmmc_driver);

	platform_driver_unregister(&brcmf_sdio_pd);
}

void brcmf_sdio_init(void)
{
	int ret;

	brcmf_dbg(TRACE, "Enter\n");

	ret = platform_driver_register(&brcmf_sdio_pd);

	if (ret)
		brcmf_dbg(ERROR, "platform_driver_register failed: %d\n", ret);
}
#else
void brcmf_sdio_exit(void)
{
	brcmf_dbg(TRACE, "Enter\n");

	sdio_unregister_driver(&brcmf_sdmmc_driver);
}

void brcmf_sdio_init(void)
{
	int ret;

	brcmf_dbg(TRACE, "Enter\n");

	ret = sdio_register_driver(&brcmf_sdmmc_driver);

	if (ret)
		brcmf_dbg(ERROR, "sdio_register_driver failed: %d\n", ret);
}
#endif		/* CONFIG_BRCMFMAC_SDIO_OOB */
