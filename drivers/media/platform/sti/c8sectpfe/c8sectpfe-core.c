/*
 * c8sectpfe-core.c - C8SECTPFE STi DVB driver
 *
 * Copyright (c) STMicroelectronics 2015
 *
 *   Author:Peter Bennett <peter.bennett@st.com>
 *	    Peter Griffin <peter.griffin@linaro.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */
#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/pinctrl/pinctrl.h>

#include "c8sectpfe-core.h"
#include "c8sectpfe-common.h"
#include "c8sectpfe-debugfs.h"
#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"

#define FIRMWARE_MEMDMA "pti_memdma_h407.elf"
MODULE_FIRMWARE(FIRMWARE_MEMDMA);

#define PID_TABLE_SIZE 1024
#define POLL_MSECS 50

static int load_c8sectpfe_fw(struct c8sectpfei *fei);

#define TS_PKT_SIZE 188
#define HEADER_SIZE (4)
#define PACKET_SIZE (TS_PKT_SIZE+HEADER_SIZE)

#define FEI_ALIGNMENT (32)
/* hw requires minimum of 8*PACKET_SIZE and padded to 8byte boundary */
#define FEI_BUFFER_SIZE (8*PACKET_SIZE*340)

#define FIFO_LEN 1024

static void c8sectpfe_timer_interrupt(unsigned long ac8sectpfei)
{
	struct c8sectpfei *fei = (struct c8sectpfei *)ac8sectpfei;
	struct channel_info *channel;
	int chan_num;

	/* iterate through input block channels */
	for (chan_num = 0; chan_num < fei->tsin_count; chan_num++) {
		channel = fei->channel_data[chan_num];

		/* is this descriptor initialised and TP enabled */
		if (channel->irec && readl(channel->irec + DMA_PRDS_TPENABLE))
			tasklet_schedule(&channel->tsklet);
	}

	fei->timer.expires = jiffies +	msecs_to_jiffies(POLL_MSECS);
	add_timer(&fei->timer);
}

static void channel_swdemux_tsklet(unsigned long data)
{
	struct channel_info *channel = (struct channel_info *)data;
	struct c8sectpfei *fei;
	unsigned long wp, rp;
	int pos, num_packets, n, size;
	u8 *buf;

	if (unlikely(!channel || !channel->irec))
		return;

	fei = channel->fei;

	wp = readl(channel->irec + DMA_PRDS_BUSWP_TP(0));
	rp = readl(channel->irec + DMA_PRDS_BUSRP_TP(0));

	pos = rp - channel->back_buffer_busaddr;

	/* has it wrapped */
	if (wp < rp)
		wp = channel->back_buffer_busaddr + FEI_BUFFER_SIZE;

	size = wp - rp;
	num_packets = size / PACKET_SIZE;

	/* manage cache so data is visible to CPU */
	dma_sync_single_for_cpu(fei->dev,
				rp,
				size,
				DMA_FROM_DEVICE);

	buf = (u8 *) channel->back_buffer_aligned;

	dev_dbg(fei->dev,
		"chan=%d channel=%p num_packets = %d, buf = %p, pos = 0x%x\n\trp=0x%lx, wp=0x%lx\n",
		channel->tsin_id, channel, num_packets, buf, pos, rp, wp);

	for (n = 0; n < num_packets; n++) {
		dvb_dmx_swfilter_packets(
			&fei->c8sectpfe[0]->
				demux[channel->demux_mapping].dvb_demux,
			&buf[pos], 1);

		pos += PACKET_SIZE;
	}

	/* advance the read pointer */
	if (wp == (channel->back_buffer_busaddr + FEI_BUFFER_SIZE))
		writel(channel->back_buffer_busaddr, channel->irec +
			DMA_PRDS_BUSRP_TP(0));
	else
		writel(wp, channel->irec + DMA_PRDS_BUSRP_TP(0));
}

static int c8sectpfe_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *demux = dvbdmxfeed->demux;
	struct stdemux *stdemux = (struct stdemux *)demux->priv;
	struct c8sectpfei *fei = stdemux->c8sectpfei;
	struct channel_info *channel;
	u32 tmp;
	unsigned long *bitmap;
	int ret;

	switch (dvbdmxfeed->type) {
	case DMX_TYPE_TS:
		break;
	case DMX_TYPE_SEC:
		break;
	default:
		dev_err(fei->dev, "%s:%d Error bailing\n"
			, __func__, __LINE__);
		return -EINVAL;
	}

	if (dvbdmxfeed->type == DMX_TYPE_TS) {
		switch (dvbdmxfeed->pes_type) {
		case DMX_PES_VIDEO:
		case DMX_PES_AUDIO:
		case DMX_PES_TELETEXT:
		case DMX_PES_PCR:
		case DMX_PES_OTHER:
			break;
		default:
			dev_err(fei->dev, "%s:%d Error bailing\n"
				, __func__, __LINE__);
			return -EINVAL;
		}
	}

	if (!atomic_read(&fei->fw_loaded)) {
		ret = load_c8sectpfe_fw(fei);
		if (ret)
			return ret;
	}

	mutex_lock(&fei->lock);

	channel = fei->channel_data[stdemux->tsin_index];

	bitmap = (unsigned long *) channel->pid_buffer_aligned;

	/* 8192 is a special PID */
	if (dvbdmxfeed->pid == 8192) {
		tmp = readl(fei->io + C8SECTPFE_IB_PID_SET(channel->tsin_id));
		tmp &= ~C8SECTPFE_PID_ENABLE;
		writel(tmp, fei->io + C8SECTPFE_IB_PID_SET(channel->tsin_id));

	} else {
		bitmap_set(bitmap, dvbdmxfeed->pid, 1);
	}

	/* manage cache so PID bitmap is visible to HW */
	dma_sync_single_for_device(fei->dev,
					channel->pid_buffer_busaddr,
					PID_TABLE_SIZE,
					DMA_TO_DEVICE);

	channel->active = 1;

	if (fei->global_feed_count == 0) {
		fei->timer.expires = jiffies +
			msecs_to_jiffies(msecs_to_jiffies(POLL_MSECS));

		add_timer(&fei->timer);
	}

	if (stdemux->running_feed_count == 0) {

		dev_dbg(fei->dev, "Starting channel=%p\n", channel);

		tasklet_init(&channel->tsklet, channel_swdemux_tsklet,
			     (unsigned long) channel);

		/* Reset the internal inputblock sram pointers */
		writel(channel->fifo,
			fei->io + C8SECTPFE_IB_BUFF_STRT(channel->tsin_id));
		writel(channel->fifo + FIFO_LEN - 1,
			fei->io + C8SECTPFE_IB_BUFF_END(channel->tsin_id));

		writel(channel->fifo,
			fei->io + C8SECTPFE_IB_READ_PNT(channel->tsin_id));
		writel(channel->fifo,
			fei->io + C8SECTPFE_IB_WRT_PNT(channel->tsin_id));


		/* reset read / write memdma ptrs for this channel */
		writel(channel->back_buffer_busaddr, channel->irec +
			DMA_PRDS_BUSBASE_TP(0));

		tmp = channel->back_buffer_busaddr + FEI_BUFFER_SIZE - 1;
		writel(tmp, channel->irec + DMA_PRDS_BUSTOP_TP(0));

		writel(channel->back_buffer_busaddr, channel->irec +
			DMA_PRDS_BUSWP_TP(0));

		/* Issue a reset and enable InputBlock */
		writel(C8SECTPFE_SYS_ENABLE | C8SECTPFE_SYS_RESET
			, fei->io + C8SECTPFE_IB_SYS(channel->tsin_id));

		/* and enable the tp */
		writel(0x1, channel->irec + DMA_PRDS_TPENABLE);

		dev_dbg(fei->dev, "%s:%d Starting DMA feed on stdemux=%p\n"
			, __func__, __LINE__, stdemux);
	}

	stdemux->running_feed_count++;
	fei->global_feed_count++;

	mutex_unlock(&fei->lock);

	return 0;
}

static int c8sectpfe_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{

	struct dvb_demux *demux = dvbdmxfeed->demux;
	struct stdemux *stdemux = (struct stdemux *)demux->priv;
	struct c8sectpfei *fei = stdemux->c8sectpfei;
	struct channel_info *channel;
	int idlereq;
	u32 tmp;
	int ret;
	unsigned long *bitmap;

	if (!atomic_read(&fei->fw_loaded)) {
		ret = load_c8sectpfe_fw(fei);
		if (ret)
			return ret;
	}

	mutex_lock(&fei->lock);

	channel = fei->channel_data[stdemux->tsin_index];

	bitmap = (unsigned long *) channel->pid_buffer_aligned;

	if (dvbdmxfeed->pid == 8192) {
		tmp = readl(fei->io + C8SECTPFE_IB_PID_SET(channel->tsin_id));
		tmp |= C8SECTPFE_PID_ENABLE;
		writel(tmp, fei->io + C8SECTPFE_IB_PID_SET(channel->tsin_id));
	} else {
		bitmap_clear(bitmap, dvbdmxfeed->pid, 1);
	}

	/* manage cache so data is visible to HW */
	dma_sync_single_for_device(fei->dev,
					channel->pid_buffer_busaddr,
					PID_TABLE_SIZE,
					DMA_TO_DEVICE);

	if (--stdemux->running_feed_count == 0) {

		channel = fei->channel_data[stdemux->tsin_index];

		/* TP re-configuration on page 168 of functional spec */

		/* disable IB (prevents more TS data going to memdma) */
		writel(0, fei->io + C8SECTPFE_IB_SYS(channel->tsin_id));

		/* disable this channels descriptor */
		writel(0,  channel->irec + DMA_PRDS_TPENABLE);

		tasklet_disable(&channel->tsklet);

		/* now request memdma channel goes idle */
		idlereq = (1 << channel->tsin_id) | IDLEREQ;
		writel(idlereq, fei->io + DMA_IDLE_REQ);

		/* wait for idle irq handler to signal completion */
		ret = wait_for_completion_timeout(&channel->idle_completion,
						msecs_to_jiffies(100));

		if (ret == 0)
			dev_warn(fei->dev,
				"Timeout waiting for idle irq on tsin%d\n",
				channel->tsin_id);

		reinit_completion(&channel->idle_completion);

		/* reset read / write ptrs for this channel */

		writel(channel->back_buffer_busaddr,
			channel->irec + DMA_PRDS_BUSBASE_TP(0));

		tmp = channel->back_buffer_busaddr + FEI_BUFFER_SIZE - 1;
		writel(tmp, channel->irec + DMA_PRDS_BUSTOP_TP(0));

		writel(channel->back_buffer_busaddr,
			channel->irec + DMA_PRDS_BUSWP_TP(0));

		dev_dbg(fei->dev,
			"%s:%d stopping DMA feed on stdemux=%p channel=%d\n",
			__func__, __LINE__, stdemux, channel->tsin_id);

		/* turn off all PIDS in the bitmap */
		memset((void *)channel->pid_buffer_aligned
			, 0x00, PID_TABLE_SIZE);

		/* manage cache so data is visible to HW */
		dma_sync_single_for_device(fei->dev,
					channel->pid_buffer_busaddr,
					PID_TABLE_SIZE,
					DMA_TO_DEVICE);

		channel->active = 0;
	}

	if (--fei->global_feed_count == 0) {
		dev_dbg(fei->dev, "%s:%d global_feed_count=%d\n"
			, __func__, __LINE__, fei->global_feed_count);

		del_timer(&fei->timer);
	}

	mutex_unlock(&fei->lock);

	return 0;
}

static struct channel_info *find_channel(struct c8sectpfei *fei, int tsin_num)
{
	int i;

	for (i = 0; i < C8SECTPFE_MAX_TSIN_CHAN; i++) {
		if (!fei->channel_data[i])
			continue;

		if (fei->channel_data[i]->tsin_id == tsin_num)
			return fei->channel_data[i];
	}

	return NULL;
}

static void c8sectpfe_getconfig(struct c8sectpfei *fei)
{
	struct c8sectpfe_hw *hw = &fei->hw_stats;

	hw->num_ib = readl(fei->io + SYS_CFG_NUM_IB);
	hw->num_mib = readl(fei->io + SYS_CFG_NUM_MIB);
	hw->num_swts = readl(fei->io + SYS_CFG_NUM_SWTS);
	hw->num_tsout = readl(fei->io + SYS_CFG_NUM_TSOUT);
	hw->num_ccsc = readl(fei->io + SYS_CFG_NUM_CCSC);
	hw->num_ram = readl(fei->io + SYS_CFG_NUM_RAM);
	hw->num_tp = readl(fei->io + SYS_CFG_NUM_TP);

	dev_info(fei->dev, "C8SECTPFE hw supports the following:\n");
	dev_info(fei->dev, "Input Blocks: %d\n", hw->num_ib);
	dev_info(fei->dev, "Merged Input Blocks: %d\n", hw->num_mib);
	dev_info(fei->dev, "Software Transport Stream Inputs: %d\n"
				, hw->num_swts);
	dev_info(fei->dev, "Transport Stream Output: %d\n", hw->num_tsout);
	dev_info(fei->dev, "Cable Card Converter: %d\n", hw->num_ccsc);
	dev_info(fei->dev, "RAMs supported by C8SECTPFE: %d\n", hw->num_ram);
	dev_info(fei->dev, "Tango TPs supported by C8SECTPFE: %d\n"
			, hw->num_tp);
}

static irqreturn_t c8sectpfe_idle_irq_handler(int irq, void *priv)
{
	struct c8sectpfei *fei = priv;
	struct channel_info *chan;
	int bit;
	unsigned long tmp = readl(fei->io + DMA_IDLE_REQ);

	/* page 168 of functional spec: Clear the idle request
	   by writing 0 to the C8SECTPFE_DMA_IDLE_REQ register. */

	/* signal idle completion */
	for_each_set_bit(bit, &tmp, fei->hw_stats.num_ib) {

		chan = find_channel(fei, bit);

		if (chan)
			complete(&chan->idle_completion);
	}

	writel(0, fei->io + DMA_IDLE_REQ);

	return IRQ_HANDLED;
}


static void free_input_block(struct c8sectpfei *fei, struct channel_info *tsin)
{
	if (!fei || !tsin)
		return;

	if (tsin->back_buffer_busaddr)
		if (!dma_mapping_error(fei->dev, tsin->back_buffer_busaddr))
			dma_unmap_single(fei->dev, tsin->back_buffer_busaddr,
				FEI_BUFFER_SIZE, DMA_BIDIRECTIONAL);

	kfree(tsin->back_buffer_start);

	if (tsin->pid_buffer_busaddr)
		if (!dma_mapping_error(fei->dev, tsin->pid_buffer_busaddr))
			dma_unmap_single(fei->dev, tsin->pid_buffer_busaddr,
				PID_TABLE_SIZE, DMA_BIDIRECTIONAL);

	kfree(tsin->pid_buffer_start);
}

#define MAX_NAME 20

static int configure_memdma_and_inputblock(struct c8sectpfei *fei,
				struct channel_info *tsin)
{
	int ret;
	u32 tmp;
	char tsin_pin_name[MAX_NAME];

	if (!fei || !tsin)
		return -EINVAL;

	dev_dbg(fei->dev, "%s:%d Configuring channel=%p tsin=%d\n"
		, __func__, __LINE__, tsin, tsin->tsin_id);

	init_completion(&tsin->idle_completion);

	tsin->back_buffer_start = kzalloc(FEI_BUFFER_SIZE +
					FEI_ALIGNMENT, GFP_KERNEL);

	if (!tsin->back_buffer_start) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	/* Ensure backbuffer is 32byte aligned */
	tsin->back_buffer_aligned = tsin->back_buffer_start
		+ FEI_ALIGNMENT;

	tsin->back_buffer_aligned = (void *)
		(((uintptr_t) tsin->back_buffer_aligned) & ~0x1F);

	tsin->back_buffer_busaddr = dma_map_single(fei->dev,
					(void *)tsin->back_buffer_aligned,
					FEI_BUFFER_SIZE,
					DMA_BIDIRECTIONAL);

	if (dma_mapping_error(fei->dev, tsin->back_buffer_busaddr)) {
		dev_err(fei->dev, "failed to map back_buffer\n");
		ret = -EFAULT;
		goto err_unmap;
	}

	/*
	 * The pid buffer can be configured (in hw) for byte or bit
	 * per pid. By powers of deduction we conclude stih407 family
	 * is configured (at SoC design stage) for bit per pid.
	 */
	tsin->pid_buffer_start = kzalloc(2048, GFP_KERNEL);

	if (!tsin->pid_buffer_start) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	/*
	 * PID buffer needs to be aligned to size of the pid table
	 * which at bit per pid is 1024 bytes (8192 pids / 8).
	 * PIDF_BASE register enforces this alignment when writing
	 * the register.
	 */

	tsin->pid_buffer_aligned = tsin->pid_buffer_start +
		PID_TABLE_SIZE;

	tsin->pid_buffer_aligned = (void *)
		(((uintptr_t) tsin->pid_buffer_aligned) & ~0x3ff);

	tsin->pid_buffer_busaddr = dma_map_single(fei->dev,
						tsin->pid_buffer_aligned,
						PID_TABLE_SIZE,
						DMA_BIDIRECTIONAL);

	if (dma_mapping_error(fei->dev, tsin->pid_buffer_busaddr)) {
		dev_err(fei->dev, "failed to map pid_bitmap\n");
		ret = -EFAULT;
		goto err_unmap;
	}

	/* manage cache so pid bitmap is visible to HW */
	dma_sync_single_for_device(fei->dev,
				tsin->pid_buffer_busaddr,
				PID_TABLE_SIZE,
				DMA_TO_DEVICE);

	snprintf(tsin_pin_name, MAX_NAME, "tsin%d-%s", tsin->tsin_id,
		(tsin->serial_not_parallel ? "serial" : "parallel"));

	tsin->pstate = pinctrl_lookup_state(fei->pinctrl, tsin_pin_name);
	if (IS_ERR(tsin->pstate)) {
		dev_err(fei->dev, "%s: pinctrl_lookup_state couldn't find %s state\n"
			, __func__, tsin_pin_name);
		ret = PTR_ERR(tsin->pstate);
		goto err_unmap;
	}

	ret = pinctrl_select_state(fei->pinctrl, tsin->pstate);

	if (ret) {
		dev_err(fei->dev, "%s: pinctrl_select_state failed\n"
			, __func__);
		goto err_unmap;
	}

	/* Enable this input block */
	tmp = readl(fei->io + SYS_INPUT_CLKEN);
	tmp |= BIT(tsin->tsin_id);
	writel(tmp, fei->io + SYS_INPUT_CLKEN);

	if (tsin->serial_not_parallel)
		tmp |= C8SECTPFE_SERIAL_NOT_PARALLEL;

	if (tsin->invert_ts_clk)
		tmp |= C8SECTPFE_INVERT_TSCLK;

	if (tsin->async_not_sync)
		tmp |= C8SECTPFE_ASYNC_NOT_SYNC;

	tmp |= C8SECTPFE_ALIGN_BYTE_SOP | C8SECTPFE_BYTE_ENDIANNESS_MSB;

	writel(tmp, fei->io + C8SECTPFE_IB_IP_FMT_CFG(tsin->tsin_id));

	writel(C8SECTPFE_SYNC(0x9) |
		C8SECTPFE_DROP(0x9) |
		C8SECTPFE_TOKEN(0x47),
		fei->io + C8SECTPFE_IB_SYNCLCKDRP_CFG(tsin->tsin_id));

	writel(TS_PKT_SIZE, fei->io + C8SECTPFE_IB_PKT_LEN(tsin->tsin_id));

	/* Place the FIFO's at the end of the irec descriptors */

	tsin->fifo = (tsin->tsin_id * FIFO_LEN);

	writel(tsin->fifo, fei->io + C8SECTPFE_IB_BUFF_STRT(tsin->tsin_id));
	writel(tsin->fifo + FIFO_LEN - 1,
		fei->io + C8SECTPFE_IB_BUFF_END(tsin->tsin_id));

	writel(tsin->fifo, fei->io + C8SECTPFE_IB_READ_PNT(tsin->tsin_id));
	writel(tsin->fifo, fei->io + C8SECTPFE_IB_WRT_PNT(tsin->tsin_id));

	writel(tsin->pid_buffer_busaddr,
		fei->io + PIDF_BASE(tsin->tsin_id));

	dev_dbg(fei->dev, "chan=%d PIDF_BASE=0x%x pid_bus_addr=%pad\n",
		tsin->tsin_id, readl(fei->io + PIDF_BASE(tsin->tsin_id)),
		&tsin->pid_buffer_busaddr);

	/* Configure and enable HW PID filtering */

	/*
	 * The PID value is created by assembling the first 8 bytes of
	 * the TS packet into a 64-bit word in big-endian format. A
	 * slice of that 64-bit word is taken from
	 * (PID_OFFSET+PID_NUM_BITS-1) to PID_OFFSET.
	 */
	tmp = (C8SECTPFE_PID_ENABLE | C8SECTPFE_PID_NUMBITS(13)
		| C8SECTPFE_PID_OFFSET(40));

	writel(tmp, fei->io + C8SECTPFE_IB_PID_SET(tsin->tsin_id));

	dev_dbg(fei->dev, "chan=%d setting wp: %d, rp: %d, buf: %d-%d\n",
		tsin->tsin_id,
		readl(fei->io + C8SECTPFE_IB_WRT_PNT(tsin->tsin_id)),
		readl(fei->io + C8SECTPFE_IB_READ_PNT(tsin->tsin_id)),
		readl(fei->io + C8SECTPFE_IB_BUFF_STRT(tsin->tsin_id)),
		readl(fei->io + C8SECTPFE_IB_BUFF_END(tsin->tsin_id)));

	/* Get base addpress of pointer record block from DMEM */
	tsin->irec = fei->io + DMA_MEMDMA_OFFSET + DMA_DMEM_OFFSET +
			readl(fei->io + DMA_PTRREC_BASE);

	/* fill out pointer record data structure */

	/* advance pointer record block to our channel */
	tsin->irec += (tsin->tsin_id * DMA_PRDS_SIZE);

	writel(tsin->fifo, tsin->irec + DMA_PRDS_MEMBASE);

	writel(tsin->fifo + FIFO_LEN - 1, tsin->irec + DMA_PRDS_MEMTOP);

	writel((188 + 7)&~7, tsin->irec + DMA_PRDS_PKTSIZE);

	writel(0x1, tsin->irec + DMA_PRDS_TPENABLE);

	/* read/write pointers with physical bus address */

	writel(tsin->back_buffer_busaddr, tsin->irec + DMA_PRDS_BUSBASE_TP(0));

	tmp = tsin->back_buffer_busaddr + FEI_BUFFER_SIZE - 1;
	writel(tmp, tsin->irec + DMA_PRDS_BUSTOP_TP(0));

	writel(tsin->back_buffer_busaddr, tsin->irec + DMA_PRDS_BUSWP_TP(0));
	writel(tsin->back_buffer_busaddr, tsin->irec + DMA_PRDS_BUSRP_TP(0));

	/* initialize tasklet */
	tasklet_init(&tsin->tsklet, channel_swdemux_tsklet,
		(unsigned long) tsin);

	return 0;

err_unmap:
	free_input_block(fei, tsin);
	return ret;
}

static irqreturn_t c8sectpfe_error_irq_handler(int irq, void *priv)
{
	struct c8sectpfei *fei = priv;

	dev_err(fei->dev, "%s: error handling not yet implemented\n"
		, __func__);

	/*
	 * TODO FIXME we should detect some error conditions here
	 * and ideally so something about them!
	 */

	return IRQ_HANDLED;
}

static int c8sectpfe_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *child, *np = dev->of_node;
	struct c8sectpfei *fei;
	struct resource *res;
	int ret, index = 0;
	struct channel_info *tsin;

	/* Allocate the c8sectpfei structure */
	fei = devm_kzalloc(dev, sizeof(struct c8sectpfei), GFP_KERNEL);
	if (!fei)
		return -ENOMEM;

	fei->dev = dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "c8sectpfe");
	fei->io = devm_ioremap_resource(dev, res);
	if (IS_ERR(fei->io))
		return PTR_ERR(fei->io);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					"c8sectpfe-ram");
	fei->sram = devm_ioremap_resource(dev, res);
	if (IS_ERR(fei->sram))
		return PTR_ERR(fei->sram);

	fei->sram_size = res->end - res->start;

	fei->idle_irq = platform_get_irq_byname(pdev, "c8sectpfe-idle-irq");
	if (fei->idle_irq < 0) {
		dev_err(dev, "Can't get c8sectpfe-idle-irq\n");
		return fei->idle_irq;
	}

	fei->error_irq = platform_get_irq_byname(pdev, "c8sectpfe-error-irq");
	if (fei->error_irq < 0) {
		dev_err(dev, "Can't get c8sectpfe-error-irq\n");
		return fei->error_irq;
	}

	platform_set_drvdata(pdev, fei);

	fei->c8sectpfeclk = devm_clk_get(dev, "c8sectpfe");
	if (IS_ERR(fei->c8sectpfeclk)) {
		dev_err(dev, "c8sectpfe clk not found\n");
		return PTR_ERR(fei->c8sectpfeclk);
	}

	ret = clk_prepare_enable(fei->c8sectpfeclk);
	if (ret) {
		dev_err(dev, "Failed to enable c8sectpfe clock\n");
		return ret;
	}

	/* to save power disable all IP's (on by default) */
	writel(0, fei->io + SYS_INPUT_CLKEN);

	/* Enable memdma clock */
	writel(MEMDMAENABLE, fei->io + SYS_OTHER_CLKEN);

	/* clear internal sram */
	memset_io(fei->sram, 0x0, fei->sram_size);

	c8sectpfe_getconfig(fei);

	ret = devm_request_irq(dev, fei->idle_irq, c8sectpfe_idle_irq_handler,
			0, "c8sectpfe-idle-irq", fei);
	if (ret) {
		dev_err(dev, "Can't register c8sectpfe-idle-irq IRQ.\n");
		goto err_clk_disable;
	}

	ret = devm_request_irq(dev, fei->error_irq,
				c8sectpfe_error_irq_handler, 0,
				"c8sectpfe-error-irq", fei);
	if (ret) {
		dev_err(dev, "Can't register c8sectpfe-error-irq IRQ.\n");
		goto err_clk_disable;
	}

	fei->tsin_count = of_get_child_count(np);

	if (fei->tsin_count > C8SECTPFE_MAX_TSIN_CHAN ||
		fei->tsin_count > fei->hw_stats.num_ib) {

		dev_err(dev, "More tsin declared than exist on SoC!\n");
		ret = -EINVAL;
		goto err_clk_disable;
	}

	fei->pinctrl = devm_pinctrl_get(dev);

	if (IS_ERR(fei->pinctrl)) {
		dev_err(dev, "Error getting tsin pins\n");
		ret = PTR_ERR(fei->pinctrl);
		goto err_clk_disable;
	}

	for_each_child_of_node(np, child) {
		struct device_node *i2c_bus;

		fei->channel_data[index] = devm_kzalloc(dev,
						sizeof(struct channel_info),
						GFP_KERNEL);

		if (!fei->channel_data[index]) {
			ret = -ENOMEM;
			goto err_clk_disable;
		}

		tsin = fei->channel_data[index];

		tsin->fei = fei;

		ret = of_property_read_u32(child, "tsin-num", &tsin->tsin_id);
		if (ret) {
			dev_err(&pdev->dev, "No tsin_num found\n");
			goto err_clk_disable;
		}

		/* sanity check value */
		if (tsin->tsin_id > fei->hw_stats.num_ib) {
			dev_err(&pdev->dev,
				"tsin-num %d specified greater than number\n\tof input block hw in SoC! (%d)",
				tsin->tsin_id, fei->hw_stats.num_ib);
			ret = -EINVAL;
			goto err_clk_disable;
		}

		tsin->invert_ts_clk = of_property_read_bool(child,
							"invert-ts-clk");

		tsin->serial_not_parallel = of_property_read_bool(child,
							"serial-not-parallel");

		tsin->async_not_sync = of_property_read_bool(child,
							"async-not-sync");

		ret = of_property_read_u32(child, "dvb-card",
					&tsin->dvb_card);
		if (ret) {
			dev_err(&pdev->dev, "No dvb-card found\n");
			goto err_clk_disable;
		}

		i2c_bus = of_parse_phandle(child, "i2c-bus", 0);
		if (!i2c_bus) {
			dev_err(&pdev->dev, "No i2c-bus found\n");
			ret = -ENODEV;
			goto err_clk_disable;
		}
		tsin->i2c_adapter =
			of_find_i2c_adapter_by_node(i2c_bus);
		if (!tsin->i2c_adapter) {
			dev_err(&pdev->dev, "No i2c adapter found\n");
			of_node_put(i2c_bus);
			ret = -ENODEV;
			goto err_clk_disable;
		}
		of_node_put(i2c_bus);

		tsin->rst_gpio = of_get_named_gpio(child, "reset-gpios", 0);

		ret = gpio_is_valid(tsin->rst_gpio);
		if (!ret) {
			dev_err(dev,
				"reset gpio for tsin%d not valid (gpio=%d)\n",
				tsin->tsin_id, tsin->rst_gpio);
			goto err_clk_disable;
		}

		ret = devm_gpio_request_one(dev, tsin->rst_gpio,
					GPIOF_OUT_INIT_LOW, "NIM reset");
		if (ret && ret != -EBUSY) {
			dev_err(dev, "Can't request tsin%d reset gpio\n"
				, fei->channel_data[index]->tsin_id);
			goto err_clk_disable;
		}

		if (!ret) {
			/* toggle reset lines */
			gpio_direction_output(tsin->rst_gpio, 0);
			usleep_range(3500, 5000);
			gpio_direction_output(tsin->rst_gpio, 1);
			usleep_range(3000, 5000);
		}

		tsin->demux_mapping = index;

		dev_dbg(fei->dev,
			"channel=%p n=%d tsin_num=%d, invert-ts-clk=%d\n\tserial-not-parallel=%d pkt-clk-valid=%d dvb-card=%d\n",
			fei->channel_data[index], index,
			tsin->tsin_id, tsin->invert_ts_clk,
			tsin->serial_not_parallel, tsin->async_not_sync,
			tsin->dvb_card);

		index++;
	}

	/* Setup timer interrupt */
	setup_timer(&fei->timer, c8sectpfe_timer_interrupt,
		    (unsigned long)fei);

	mutex_init(&fei->lock);

	/* Get the configuration information about the tuners */
	ret = c8sectpfe_tuner_register_frontend(&fei->c8sectpfe[0],
					(void *)fei,
					c8sectpfe_start_feed,
					c8sectpfe_stop_feed);
	if (ret) {
		dev_err(dev, "c8sectpfe_tuner_register_frontend failed (%d)\n",
			ret);
		goto err_clk_disable;
	}

	c8sectpfe_debugfs_init(fei);

	return 0;

err_clk_disable:
	clk_disable_unprepare(fei->c8sectpfeclk);
	return ret;
}

static int c8sectpfe_remove(struct platform_device *pdev)
{
	struct c8sectpfei *fei = platform_get_drvdata(pdev);
	struct channel_info *channel;
	int i;

	wait_for_completion(&fei->fw_ack);

	c8sectpfe_tuner_unregister_frontend(fei->c8sectpfe[0], fei);

	/*
	 * Now loop through and un-configure each of the InputBlock resources
	 */
	for (i = 0; i < fei->tsin_count; i++) {
		channel = fei->channel_data[i];
		free_input_block(fei, channel);
	}

	c8sectpfe_debugfs_exit(fei);

	dev_info(fei->dev, "Stopping memdma SLIM core\n");
	if (readl(fei->io + DMA_CPU_RUN))
		writel(0x0,  fei->io + DMA_CPU_RUN);

	/* unclock all internal IP's */
	if (readl(fei->io + SYS_INPUT_CLKEN))
		writel(0, fei->io + SYS_INPUT_CLKEN);

	if (readl(fei->io + SYS_OTHER_CLKEN))
		writel(0, fei->io + SYS_OTHER_CLKEN);

	if (fei->c8sectpfeclk)
		clk_disable_unprepare(fei->c8sectpfeclk);

	return 0;
}


static int configure_channels(struct c8sectpfei *fei)
{
	int index = 0, ret;
	struct channel_info *tsin;
	struct device_node *child, *np = fei->dev->of_node;

	/* iterate round each tsin and configure memdma descriptor and IB hw */
	for_each_child_of_node(np, child) {

		tsin = fei->channel_data[index];

		ret = configure_memdma_and_inputblock(fei,
						fei->channel_data[index]);

		if (ret) {
			dev_err(fei->dev,
				"configure_memdma_and_inputblock failed\n");
			goto err_unmap;
		}
		index++;
	}

	return 0;

err_unmap:
	for (index = 0; index < fei->tsin_count; index++) {
		tsin = fei->channel_data[index];
		free_input_block(fei, tsin);
	}
	return ret;
}

static int
c8sectpfe_elf_sanity_check(struct c8sectpfei *fei, const struct firmware *fw)
{
	struct elf32_hdr *ehdr;
	char class;

	if (!fw) {
		dev_err(fei->dev, "failed to load %s\n", FIRMWARE_MEMDMA);
		return -EINVAL;
	}

	if (fw->size < sizeof(struct elf32_hdr)) {
		dev_err(fei->dev, "Image is too small\n");
		return -EINVAL;
	}

	ehdr = (struct elf32_hdr *)fw->data;

	/* We only support ELF32 at this point */
	class = ehdr->e_ident[EI_CLASS];
	if (class != ELFCLASS32) {
		dev_err(fei->dev, "Unsupported class: %d\n", class);
		return -EINVAL;
	}

	if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
		dev_err(fei->dev, "Unsupported firmware endianness\n");
		return -EINVAL;
	}

	if (fw->size < ehdr->e_shoff + sizeof(struct elf32_shdr)) {
		dev_err(fei->dev, "Image is too small\n");
		return -EINVAL;
	}

	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG)) {
		dev_err(fei->dev, "Image is corrupted (bad magic)\n");
		return -EINVAL;
	}

	/* Check ELF magic */
	ehdr = (Elf32_Ehdr *)fw->data;
	if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
	    ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
	    ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
	    ehdr->e_ident[EI_MAG3] != ELFMAG3) {
		dev_err(fei->dev, "Invalid ELF magic\n");
		return -EINVAL;
	}

	if (ehdr->e_type != ET_EXEC) {
		dev_err(fei->dev, "Unsupported ELF header type\n");
		return -EINVAL;
	}

	if (ehdr->e_phoff > fw->size) {
		dev_err(fei->dev, "Firmware size is too small\n");
		return -EINVAL;
	}

	return 0;
}


static void load_imem_segment(struct c8sectpfei *fei, Elf32_Phdr *phdr,
			const struct firmware *fw, u8 __iomem *dest,
			int seg_num)
{
	const u8 *imem_src = fw->data + phdr->p_offset;
	int i;

	/*
	 * For IMEM segments, the segment contains 24-bit
	 * instructions which must be padded to 32-bit
	 * instructions before being written. The written
	 * segment is padded with NOP instructions.
	 */

	dev_dbg(fei->dev,
		"Loading IMEM segment %d 0x%08x\n\t (0x%x bytes) -> 0x%p (0x%x bytes)\n",
seg_num,
		phdr->p_paddr, phdr->p_filesz,
		dest, phdr->p_memsz + phdr->p_memsz / 3);

	for (i = 0; i < phdr->p_filesz; i++) {

		writeb(readb((void __iomem *)imem_src), (void __iomem *)dest);

		/* Every 3 bytes, add an additional
		 * padding zero in destination */
		if (i % 3 == 2) {
			dest++;
			writeb(0x00, (void __iomem *)dest);
		}

		dest++;
		imem_src++;
	}
}

static void load_dmem_segment(struct c8sectpfei *fei, Elf32_Phdr *phdr,
			const struct firmware *fw, u8 __iomem *dst, int seg_num)
{
	/*
	 * For DMEM segments copy the segment data from the ELF
	 * file and pad segment with zeroes
	 */

	dev_dbg(fei->dev,
		"Loading DMEM segment %d 0x%08x\n\t(0x%x bytes) -> 0x%p (0x%x bytes)\n",
		seg_num, phdr->p_paddr, phdr->p_filesz,
		dst, phdr->p_memsz);

	memcpy((void __force *)dst, (void *)fw->data + phdr->p_offset,
		phdr->p_filesz);

	memset((void __force *)dst + phdr->p_filesz, 0,
		phdr->p_memsz - phdr->p_filesz);
}

static int load_slim_core_fw(const struct firmware *fw, struct c8sectpfei *fei)
{
	Elf32_Ehdr *ehdr;
	Elf32_Phdr *phdr;
	u8 __iomem *dst;
	int err = 0, i;

	if (!fw || !fei)
		return -EINVAL;

	ehdr = (Elf32_Ehdr *)fw->data;
	phdr = (Elf32_Phdr *)(fw->data + ehdr->e_phoff);

	/* go through the available ELF segments */
	for (i = 0; i < ehdr->e_phnum; i++, phdr++) {

		/* Only consider LOAD segments */
		if (phdr->p_type != PT_LOAD)
			continue;

		/*
		 * Check segment is contained within the fw->data buffer
		 */
		if (phdr->p_offset + phdr->p_filesz > fw->size) {
			dev_err(fei->dev,
				"Segment %d is outside of firmware file\n", i);
			err = -EINVAL;
			break;
		}

		/*
		 * MEMDMA IMEM has executable flag set, otherwise load
		 * this segment into DMEM.
		 *
		 */

		if (phdr->p_flags & PF_X) {
			dst = (u8 __iomem *) fei->io + DMA_MEMDMA_IMEM;
			/*
			 * The Slim ELF file uses 32-bit word addressing for
			 * load offsets.
			 */
			dst += (phdr->p_paddr & 0xFFFFF) * sizeof(unsigned int);
			load_imem_segment(fei, phdr, fw, dst, i);
		} else {
			dst = (u8 __iomem *) fei->io + DMA_MEMDMA_DMEM;
			/*
			 * The Slim ELF file uses 32-bit word addressing for
			 * load offsets.
			 */
			dst += (phdr->p_paddr & 0xFFFFF) * sizeof(unsigned int);
			load_dmem_segment(fei, phdr, fw, dst, i);
		}
	}

	release_firmware(fw);
	return err;
}

static int load_c8sectpfe_fw(struct c8sectpfei *fei)
{
	const struct firmware *fw;
	int err;

	dev_info(fei->dev, "Loading firmware: %s\n", FIRMWARE_MEMDMA);

	err = request_firmware(&fw, FIRMWARE_MEMDMA, fei->dev);
	if (err)
		return err;

	err = c8sectpfe_elf_sanity_check(fei, fw);
	if (err) {
		dev_err(fei->dev, "c8sectpfe_elf_sanity_check failed err=(%d)\n"
			, err);
		release_firmware(fw);
		return err;
	}

	err = load_slim_core_fw(fw, fei);
	if (err) {
		dev_err(fei->dev, "load_slim_core_fw failed err=(%d)\n", err);
		return err;
	}

	/* now the firmware is loaded configure the input blocks */
	err = configure_channels(fei);
	if (err) {
		dev_err(fei->dev, "configure_channels failed err=(%d)\n", err);
		return err;
	}

	/*
	 * STBus target port can access IMEM and DMEM ports
	 * without waiting for CPU
	 */
	writel(0x1, fei->io + DMA_PER_STBUS_SYNC);

	dev_info(fei->dev, "Boot the memdma SLIM core\n");
	writel(0x1,  fei->io + DMA_CPU_RUN);

	atomic_set(&fei->fw_loaded, 1);

	return 0;
}

static const struct of_device_id c8sectpfe_match[] = {
	{ .compatible = "st,stih407-c8sectpfe" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, c8sectpfe_match);

static struct platform_driver c8sectpfe_driver = {
	.driver = {
		.name = "c8sectpfe",
		.of_match_table = of_match_ptr(c8sectpfe_match),
	},
	.probe	= c8sectpfe_probe,
	.remove	= c8sectpfe_remove,
};

module_platform_driver(c8sectpfe_driver);

MODULE_AUTHOR("Peter Bennett <peter.bennett@st.com>");
MODULE_AUTHOR("Peter Griffin <peter.griffin@linaro.org>");
MODULE_DESCRIPTION("C8SECTPFE STi DVB Driver");
MODULE_LICENSE("GPL");
