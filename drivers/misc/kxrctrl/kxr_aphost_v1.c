// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI controller driver for the nordic52832 SoCs
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "kxr_aphost.h"

static struct js_spi_client *gspi_client;
static struct cp_buffer_t *u_packet;

MODULE_IMPORT_NS(DMA_BUF);

static void d_packet_set_instance(struct cp_buffer_t *in)
{

	if (gspi_client == NULL) {
		pr_err("js %s: drv init err\n", __func__);
		return;
	}

	spin_lock(&gspi_client->smem_lock);
	if (in != NULL) {
		u_packet = in;
		u_packet->c_head = -1;
		u_packet->p_head = -1;
	} else
		u_packet = NULL;
	spin_unlock(&gspi_client->smem_lock);

	if (in == NULL)
		pr_debug("js %s:  release mem\n", __func__);
	else
		pr_debug("js %s:  alloc mem\n", __func__);

}

static ssize_t jsmem_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kxr_aphost *aphost = kxr_aphost_get_drv_data(dev);

	kxr_spi_xfer_mode_set(&aphost->xfer, KXR_SPI_WORK_MODE_OLD);
	return scnprintf(buf, PAGE_SIZE, "%d\n",
					gspi_client->memfd);
}

static ssize_t jsmem_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct cp_buffer_t *inbuf;
	struct iosys_map map;
	struct kxr_aphost *aphost = kxr_aphost_get_drv_data(dev);

	ret = kstrtoint(buf, 10, &gspi_client->memfd);
	if (ret < 0)
		return ret;

	mutex_lock(&gspi_client->js_sm_mutex);

	if (gspi_client->memfd == -1) {
		if (IS_ERR_OR_NULL(gspi_client->vaddr))
			goto end;

		d_packet_set_instance(NULL);
		dma_buf_vunmap(gspi_client->js_buf, gspi_client->vaddr);
		dma_buf_end_cpu_access(gspi_client->js_buf, DMA_BIDIRECTIONAL);
		dma_buf_put(gspi_client->js_buf);
		gspi_client->vaddr = NULL;
		gspi_client->js_buf = NULL;
	} else {
		gspi_client->js_buf = dma_buf_get(gspi_client->memfd);
		if (IS_ERR_OR_NULL(gspi_client->js_buf)) {
			ret = -ENOMEM;
			pr_err("[%s]dma_buf_get failed for fd: %d\n", __func__,
							gspi_client->memfd);
			goto end;
		}

		ret = dma_buf_begin_cpu_access(gspi_client->js_buf,
							DMA_BIDIRECTIONAL);
		if (ret) {
			pr_err("[%s]: dma_buf_begin_cpu_access failed\n",
							 __func__);
			dma_buf_put(gspi_client->js_buf);
			gspi_client->js_buf = NULL;
			goto end;
		}

		gspi_client->vsize = gspi_client->js_buf->size;
		dma_buf_vmap(gspi_client->js_buf, &map);
		gspi_client->vaddr = map.vaddr;

		if (IS_ERR_OR_NULL(gspi_client->vaddr)) {
			dma_buf_end_cpu_access(gspi_client->js_buf,
						DMA_BIDIRECTIONAL);
			dma_buf_put(gspi_client->js_buf);
			gspi_client->js_buf = NULL;
			pr_err("[%s]dma_buf_vmap failed for fd: %d\n", __func__,
							  gspi_client->memfd);
			goto end;
		}

		inbuf = (struct cp_buffer_t *)gspi_client->vaddr;
		d_packet_set_instance(inbuf);
	}

	kxr_spi_xfer_mode_set(&aphost->xfer, KXR_SPI_WORK_MODE_OLD);

end:
	mutex_unlock(&gspi_client->js_sm_mutex);

	return count;
}
static DEVICE_ATTR_RW(jsmem);

bool js_thread(struct kxr_aphost *aphost)
{
	unsigned char *pbuf;
	uint64_t tts;
	uint32_t tth[8];
	uint64_t tto[8];
	int num = 0;
	int pksz = 0;
	int index = 0;
	uint32_t hosttime;
	u8 *rx_buff = aphost->xchg.rx_buff;
	struct js_spi_client *spi_client = &aphost->js;

	kxr_spi_xchg_sync(aphost);

	/* Filtering dirty Data */
	if (rx_buff[4] == 0xff)
		return false;

	pksz = rx_buff[4];
	num = rx_buff[5];

	if (num == 0 || pksz != 30)
		return false;

	memcpy(&hosttime, &rx_buff[6], sizeof(hosttime));
	tts = ktime_to_ns(ktime_get_boottime());

	pbuf = &rx_buff[10];
	spin_lock(&gspi_client->smem_lock);
	for (index = 0; index < num; index++) {
		memcpy(&tth[index], pbuf, 4);
		tto[index] =
		tts - (hosttime-tth[index]) * 100000;
		if ((u_packet) && (spi_client->vaddr)) {
			int8_t p_head;
			struct d_packet_t *pdata;

			p_head = (u_packet->p_head + 1) % MAX_PACK_SIZE;
			pdata = &u_packet->data[p_head];
			pdata->ts = tto[index];
			pdata->size = pksz - 4;
			memcpy((void *)pdata->data, (void *)(pbuf+4), pksz-4);
			u_packet->p_head = p_head;
		}
		pbuf += pksz;
	}
	spin_unlock(&gspi_client->smem_lock);

	return false;
}

int js_spi_driver_probe(struct kxr_aphost *aphost)
{
	struct js_spi_client   *spi_client = &aphost->js;

	mutex_init(&(spi_client->js_sm_mutex));

	spin_lock_init(&spi_client->smem_lock);

	device_create_file(&aphost->xfer.spi->dev, &dev_attr_jsmem);
	spi_client->vaddr = NULL;

	gspi_client = spi_client;

	return 0;
}

int js_spi_driver_remove(struct kxr_aphost *aphost)
{
	struct js_spi_client *spi_client = &aphost->js;

	d_packet_set_instance(NULL);

	if (!IS_ERR_OR_NULL(spi_client->vaddr)) {
		dma_buf_end_cpu_access(spi_client->js_buf,
				DMA_BIDIRECTIONAL);
		dma_buf_put(spi_client->js_buf);
		spi_client->js_buf = NULL;
	}

	mutex_destroy(&(spi_client->js_sm_mutex));

	device_remove_file(&aphost->xfer.spi->dev, &dev_attr_jsmem);

	gspi_client = NULL;
	return 0;
}
