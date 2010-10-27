/*
 * Wireless Host Controller (WHC) initialization.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>
#include <linux/uwb/umc.h>

#include "../../wusbcore/wusbhc.h"

#include "whcd.h"

/*
 * Reset the host controller.
 */
static void whc_hw_reset(struct whc *whc)
{
	le_writel(WUSBCMD_WHCRESET, whc->base + WUSBCMD);
	whci_wait_for(&whc->umc->dev, whc->base + WUSBCMD, WUSBCMD_WHCRESET, 0,
		      100, "reset");
}

static void whc_hw_init_di_buf(struct whc *whc)
{
	int d;

	/* Disable all entries in the Device Information buffer. */
	for (d = 0; d < whc->n_devices; d++)
		whc->di_buf[d].addr_sec_info = WHC_DI_DISABLE;

	le_writeq(whc->di_buf_dma, whc->base + WUSBDEVICEINFOADDR);
}

static void whc_hw_init_dn_buf(struct whc *whc)
{
	/* Clear the Device Notification buffer to ensure the V (valid)
	 * bits are clear.  */
	memset(whc->dn_buf, 0, 4096);

	le_writeq(whc->dn_buf_dma, whc->base + WUSBDNTSBUFADDR);
}

int whc_init(struct whc *whc)
{
	u32 whcsparams;
	int ret, i;
	resource_size_t start, len;

	spin_lock_init(&whc->lock);
	mutex_init(&whc->mutex);
	init_waitqueue_head(&whc->cmd_wq);
	init_waitqueue_head(&whc->async_list_wq);
	init_waitqueue_head(&whc->periodic_list_wq);
	whc->workqueue = create_singlethread_workqueue(dev_name(&whc->umc->dev));
	if (whc->workqueue == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	INIT_WORK(&whc->dn_work, whc_dn_work);

	INIT_WORK(&whc->async_work, scan_async_work);
	INIT_LIST_HEAD(&whc->async_list);
	INIT_LIST_HEAD(&whc->async_removed_list);

	INIT_WORK(&whc->periodic_work, scan_periodic_work);
	for (i = 0; i < 5; i++)
		INIT_LIST_HEAD(&whc->periodic_list[i]);
	INIT_LIST_HEAD(&whc->periodic_removed_list);

	/* Map HC registers. */
	start = whc->umc->resource.start;
	len   = whc->umc->resource.end - start + 1;
	if (!request_mem_region(start, len, "whci-hc")) {
		dev_err(&whc->umc->dev, "can't request HC region\n");
		ret = -EBUSY;
		goto error;
	}
	whc->base_phys = start;
	whc->base = ioremap(start, len);
	if (!whc->base) {
		dev_err(&whc->umc->dev, "ioremap\n");
		ret = -ENOMEM;
		goto error;
	}

	whc_hw_reset(whc);

	/* Read maximum number of devices, keys and MMC IEs. */
	whcsparams = le_readl(whc->base + WHCSPARAMS);
	whc->n_devices = WHCSPARAMS_TO_N_DEVICES(whcsparams);
	whc->n_keys    = WHCSPARAMS_TO_N_KEYS(whcsparams);
	whc->n_mmc_ies = WHCSPARAMS_TO_N_MMC_IES(whcsparams);

	dev_dbg(&whc->umc->dev, "N_DEVICES = %d, N_KEYS = %d, N_MMC_IES = %d\n",
		whc->n_devices, whc->n_keys, whc->n_mmc_ies);

	whc->qset_pool = dma_pool_create("qset", &whc->umc->dev,
					 sizeof(struct whc_qset), 64, 0);
	if (whc->qset_pool == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	ret = asl_init(whc);
	if (ret < 0)
		goto error;
	ret = pzl_init(whc);
	if (ret < 0)
		goto error;

	/* Allocate and initialize a buffer for generic commands, the
	   Device Information buffer, and the Device Notification
	   buffer. */

	whc->gen_cmd_buf = dma_alloc_coherent(&whc->umc->dev, WHC_GEN_CMD_DATA_LEN,
					      &whc->gen_cmd_buf_dma, GFP_KERNEL);
	if (whc->gen_cmd_buf == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	whc->dn_buf = dma_alloc_coherent(&whc->umc->dev,
					 sizeof(struct dn_buf_entry) * WHC_N_DN_ENTRIES,
					 &whc->dn_buf_dma, GFP_KERNEL);
	if (!whc->dn_buf) {
		ret = -ENOMEM;
		goto error;
	}
	whc_hw_init_dn_buf(whc);

	whc->di_buf = dma_alloc_coherent(&whc->umc->dev,
					 sizeof(struct di_buf_entry) * whc->n_devices,
					 &whc->di_buf_dma, GFP_KERNEL);
	if (!whc->di_buf) {
		ret = -ENOMEM;
		goto error;
	}
	whc_hw_init_di_buf(whc);

	return 0;

error:
	whc_clean_up(whc);
	return ret;
}

void whc_clean_up(struct whc *whc)
{
	resource_size_t len;

	if (whc->di_buf)
		dma_free_coherent(&whc->umc->dev, sizeof(struct di_buf_entry) * whc->n_devices,
				  whc->di_buf, whc->di_buf_dma);
	if (whc->dn_buf)
		dma_free_coherent(&whc->umc->dev, sizeof(struct dn_buf_entry) * WHC_N_DN_ENTRIES,
				  whc->dn_buf, whc->dn_buf_dma);
	if (whc->gen_cmd_buf)
		dma_free_coherent(&whc->umc->dev, WHC_GEN_CMD_DATA_LEN,
				  whc->gen_cmd_buf, whc->gen_cmd_buf_dma);

	pzl_clean_up(whc);
	asl_clean_up(whc);

	if (whc->qset_pool)
		dma_pool_destroy(whc->qset_pool);

	len   = whc->umc->resource.end - whc->umc->resource.start + 1;
	if (whc->base)
		iounmap(whc->base);
	if (whc->base_phys)
		release_mem_region(whc->base_phys, len);

	if (whc->workqueue)
		destroy_workqueue(whc->workqueue);
}
