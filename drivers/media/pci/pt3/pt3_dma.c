/*
 * Earthsoft PT3 driver
 *
 * Copyright (C) 2014 Akihiro Tsukada <tskd08@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include "pt3.h"

#define PT3_ACCESS_UNIT (TS_PACKET_SZ * 128)
#define PT3_BUF_CANARY  (0x74)

static u32 get_dma_base(int idx)
{
	int i;

	i = (idx == 1 || idx == 2) ? 3 - idx : idx;
	return REG_DMA_BASE + 0x18 * i;
}

int pt3_stop_dma(struct pt3_adapter *adap)
{
	struct pt3_board *pt3 = adap->dvb_adap.priv;
	u32 base;
	u32 stat;
	int retry;

	base = get_dma_base(adap->adap_idx);
	stat = ioread32(pt3->regs[0] + base + OFST_STATUS);
	if (!(stat & 0x01))
		return 0;

	iowrite32(0x02, pt3->regs[0] + base + OFST_DMA_CTL);
	for (retry = 0; retry < 5; retry++) {
		stat = ioread32(pt3->regs[0] + base + OFST_STATUS);
		if (!(stat & 0x01))
			return 0;
		msleep(50);
	}
	return -EIO;
}

int pt3_start_dma(struct pt3_adapter *adap)
{
	struct pt3_board *pt3 = adap->dvb_adap.priv;
	u32 base = get_dma_base(adap->adap_idx);

	iowrite32(0x02, pt3->regs[0] + base + OFST_DMA_CTL);
	iowrite32(lower_32_bits(adap->desc_buf[0].b_addr),
			pt3->regs[0] + base + OFST_DMA_DESC_L);
	iowrite32(upper_32_bits(adap->desc_buf[0].b_addr),
			pt3->regs[0] + base + OFST_DMA_DESC_H);
	iowrite32(0x01, pt3->regs[0] + base + OFST_DMA_CTL);
	return 0;
}


static u8 *next_unit(struct pt3_adapter *adap, int *idx, int *ofs)
{
	*ofs += PT3_ACCESS_UNIT;
	if (*ofs >= DATA_BUF_SZ) {
		*ofs -= DATA_BUF_SZ;
		(*idx)++;
		if (*idx == adap->num_bufs)
			*idx = 0;
	}
	return &adap->buffer[*idx].data[*ofs];
}

int pt3_proc_dma(struct pt3_adapter *adap)
{
	int idx, ofs;

	idx = adap->buf_idx;
	ofs = adap->buf_ofs;

	if (adap->buffer[idx].data[ofs] == PT3_BUF_CANARY)
		return 0;

	while (*next_unit(adap, &idx, &ofs) != PT3_BUF_CANARY) {
		u8 *p;

		p = &adap->buffer[adap->buf_idx].data[adap->buf_ofs];
		if (adap->num_discard > 0)
			adap->num_discard--;
		else if (adap->buf_ofs + PT3_ACCESS_UNIT > DATA_BUF_SZ) {
			dvb_dmx_swfilter_packets(&adap->demux, p,
				(DATA_BUF_SZ - adap->buf_ofs) / TS_PACKET_SZ);
			dvb_dmx_swfilter_packets(&adap->demux,
				adap->buffer[idx].data, ofs / TS_PACKET_SZ);
		} else
			dvb_dmx_swfilter_packets(&adap->demux, p,
				PT3_ACCESS_UNIT / TS_PACKET_SZ);

		*p = PT3_BUF_CANARY;
		adap->buf_idx = idx;
		adap->buf_ofs = ofs;
	}
	return 0;
}

void pt3_init_dmabuf(struct pt3_adapter *adap)
{
	int idx, ofs;
	u8 *p;

	idx = 0;
	ofs = 0;
	p = adap->buffer[0].data;
	/* mark the whole buffers as "not written yet" */
	while (idx < adap->num_bufs) {
		p[ofs] = PT3_BUF_CANARY;
		ofs += PT3_ACCESS_UNIT;
		if (ofs >= DATA_BUF_SZ) {
			ofs -= DATA_BUF_SZ;
			idx++;
			p = adap->buffer[idx].data;
		}
	}
	adap->buf_idx = 0;
	adap->buf_ofs = 0;
}

void pt3_free_dmabuf(struct pt3_adapter *adap)
{
	struct pt3_board *pt3;
	int i;

	pt3 = adap->dvb_adap.priv;
	for (i = 0; i < adap->num_bufs; i++)
		dma_free_coherent(&pt3->pdev->dev, DATA_BUF_SZ,
			adap->buffer[i].data, adap->buffer[i].b_addr);
	adap->num_bufs = 0;

	for (i = 0; i < adap->num_desc_bufs; i++)
		dma_free_coherent(&pt3->pdev->dev, PAGE_SIZE,
			adap->desc_buf[i].descs, adap->desc_buf[i].b_addr);
	adap->num_desc_bufs = 0;
}


int pt3_alloc_dmabuf(struct pt3_adapter *adap)
{
	struct pt3_board *pt3;
	void *p;
	int i, j;
	int idx, ofs;
	int num_desc_bufs;
	dma_addr_t data_addr, desc_addr;
	struct xfer_desc *d;

	pt3 = adap->dvb_adap.priv;
	adap->num_bufs = 0;
	adap->num_desc_bufs = 0;
	for (i = 0; i < pt3->num_bufs; i++) {
		p = dma_alloc_coherent(&pt3->pdev->dev, DATA_BUF_SZ,
					&adap->buffer[i].b_addr, GFP_KERNEL);
		if (p == NULL)
			goto failed;
		adap->buffer[i].data = p;
		adap->num_bufs++;
	}
	pt3_init_dmabuf(adap);

	/* build circular-linked pointers (xfer_desc) to the data buffers*/
	idx = 0;
	ofs = 0;
	num_desc_bufs =
		DIV_ROUND_UP(adap->num_bufs * DATA_BUF_XFERS, DESCS_IN_PAGE);
	for (i = 0; i < num_desc_bufs; i++) {
		p = dma_alloc_coherent(&pt3->pdev->dev, PAGE_SIZE,
					&desc_addr, GFP_KERNEL);
		if (p == NULL)
			goto failed;
		adap->num_desc_bufs++;
		adap->desc_buf[i].descs = p;
		adap->desc_buf[i].b_addr = desc_addr;

		if (i > 0) {
			d = &adap->desc_buf[i - 1].descs[DESCS_IN_PAGE - 1];
			d->next_l = lower_32_bits(desc_addr);
			d->next_h = upper_32_bits(desc_addr);
		}
		for (j = 0; j < DESCS_IN_PAGE; j++) {
			data_addr = adap->buffer[idx].b_addr + ofs;
			d = &adap->desc_buf[i].descs[j];
			d->addr_l = lower_32_bits(data_addr);
			d->addr_h = upper_32_bits(data_addr);
			d->size = DATA_XFER_SZ;

			desc_addr += sizeof(struct xfer_desc);
			d->next_l = lower_32_bits(desc_addr);
			d->next_h = upper_32_bits(desc_addr);

			ofs += DATA_XFER_SZ;
			if (ofs >= DATA_BUF_SZ) {
				ofs -= DATA_BUF_SZ;
				idx++;
				if (idx >= adap->num_bufs) {
					desc_addr = adap->desc_buf[0].b_addr;
					d->next_l = lower_32_bits(desc_addr);
					d->next_h = upper_32_bits(desc_addr);
					return 0;
				}
			}
		}
	}
	return 0;

failed:
	pt3_free_dmabuf(adap);
	return -ENOMEM;
}
