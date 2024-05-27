// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */

#include <linux/cacheflush.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "ipu6-bus.h"
#include "ipu6-fw-com.h"
#include "ipu6-isys.h"
#include "ipu6-platform-isys-csi2-reg.h"
#include "ipu6-platform-regs.h"

static const char send_msg_types[N_IPU6_FW_ISYS_SEND_TYPE][32] = {
	"STREAM_OPEN",
	"STREAM_START",
	"STREAM_START_AND_CAPTURE",
	"STREAM_CAPTURE",
	"STREAM_STOP",
	"STREAM_FLUSH",
	"STREAM_CLOSE"
};

static int handle_proxy_response(struct ipu6_isys *isys, unsigned int req_id)
{
	struct device *dev = &isys->adev->auxdev.dev;
	struct ipu6_fw_isys_proxy_resp_info_abi *resp;
	int ret;

	resp = ipu6_recv_get_token(isys->fwcom, IPU6_BASE_PROXY_RECV_QUEUES);
	if (!resp)
		return 1;

	dev_dbg(dev, "Proxy response: id %u, error %u, details %u\n",
		resp->request_id, resp->error_info.error,
		resp->error_info.error_details);

	ret = req_id == resp->request_id ? 0 : -EIO;

	ipu6_recv_put_token(isys->fwcom, IPU6_BASE_PROXY_RECV_QUEUES);

	return ret;
}

int ipu6_fw_isys_send_proxy_token(struct ipu6_isys *isys,
				  unsigned int req_id,
				  unsigned int index,
				  unsigned int offset, u32 value)
{
	struct ipu6_fw_com_context *ctx = isys->fwcom;
	struct device *dev = &isys->adev->auxdev.dev;
	struct ipu6_fw_proxy_send_queue_token *token;
	unsigned int timeout = 1000;
	int ret;

	dev_dbg(dev,
		"proxy send: req_id 0x%x, index %d, offset 0x%x, value 0x%x\n",
		req_id, index, offset, value);

	token = ipu6_send_get_token(ctx, IPU6_BASE_PROXY_SEND_QUEUES);
	if (!token)
		return -EBUSY;

	token->request_id = req_id;
	token->region_index = index;
	token->offset = offset;
	token->value = value;
	ipu6_send_put_token(ctx, IPU6_BASE_PROXY_SEND_QUEUES);

	do {
		usleep_range(100, 110);
		ret = handle_proxy_response(isys, req_id);
		if (!ret)
			break;
		if (ret == -EIO) {
			dev_err(dev, "Proxy respond with unexpected id\n");
			break;
		}
		timeout--;
	} while (ret && timeout);

	if (!timeout)
		dev_err(dev, "Proxy response timed out\n");

	return ret;
}

int ipu6_fw_isys_complex_cmd(struct ipu6_isys *isys,
			     const unsigned int stream_handle,
			     void *cpu_mapped_buf,
			     dma_addr_t dma_mapped_buf,
			     size_t size, u16 send_type)
{
	struct ipu6_fw_com_context *ctx = isys->fwcom;
	struct device *dev = &isys->adev->auxdev.dev;
	struct ipu6_fw_send_queue_token *token;

	if (send_type >= N_IPU6_FW_ISYS_SEND_TYPE)
		return -EINVAL;

	dev_dbg(dev, "send_token: %s\n", send_msg_types[send_type]);

	/*
	 * Time to flush cache in case we have some payload. Not all messages
	 * have that
	 */
	if (cpu_mapped_buf)
		clflush_cache_range(cpu_mapped_buf, size);

	token = ipu6_send_get_token(ctx,
				    stream_handle + IPU6_BASE_MSG_SEND_QUEUES);
	if (!token)
		return -EBUSY;

	token->payload = dma_mapped_buf;
	token->buf_handle = (unsigned long)cpu_mapped_buf;
	token->send_type = send_type;

	ipu6_send_put_token(ctx, stream_handle + IPU6_BASE_MSG_SEND_QUEUES);

	return 0;
}

int ipu6_fw_isys_simple_cmd(struct ipu6_isys *isys,
			    const unsigned int stream_handle, u16 send_type)
{
	return ipu6_fw_isys_complex_cmd(isys, stream_handle, NULL, 0, 0,
					send_type);
}

int ipu6_fw_isys_close(struct ipu6_isys *isys)
{
	struct device *dev = &isys->adev->auxdev.dev;
	int retry = IPU6_ISYS_CLOSE_RETRY;
	unsigned long flags;
	void *fwcom;
	int ret;

	/*
	 * Stop the isys fw. Actual close takes
	 * some time as the FW must stop its actions including code fetch
	 * to SP icache.
	 * spinlock to wait the interrupt handler to be finished
	 */
	spin_lock_irqsave(&isys->power_lock, flags);
	ret = ipu6_fw_com_close(isys->fwcom);
	fwcom = isys->fwcom;
	isys->fwcom = NULL;
	spin_unlock_irqrestore(&isys->power_lock, flags);
	if (ret)
		dev_err(dev, "Device close failure: %d\n", ret);

	/* release probably fails if the close failed. Let's try still */
	do {
		usleep_range(400, 500);
		ret = ipu6_fw_com_release(fwcom, 0);
		retry--;
	} while (ret && retry);

	if (ret) {
		dev_err(dev, "Device release time out %d\n", ret);
		spin_lock_irqsave(&isys->power_lock, flags);
		isys->fwcom = fwcom;
		spin_unlock_irqrestore(&isys->power_lock, flags);
	}

	return ret;
}

void ipu6_fw_isys_cleanup(struct ipu6_isys *isys)
{
	int ret;

	ret = ipu6_fw_com_release(isys->fwcom, 1);
	if (ret < 0)
		dev_warn(&isys->adev->auxdev.dev,
			 "Device busy, fw_com release failed.");
	isys->fwcom = NULL;
}

static void start_sp(struct ipu6_bus_device *adev)
{
	struct ipu6_isys *isys = ipu6_bus_get_drvdata(adev);
	void __iomem *spc_regs_base = isys->pdata->base +
		isys->pdata->ipdata->hw_variant.spc_offset;
	u32 val = IPU6_ISYS_SPC_STATUS_START |
		IPU6_ISYS_SPC_STATUS_RUN |
		IPU6_ISYS_SPC_STATUS_CTRL_ICACHE_INVALIDATE;

	val |= isys->icache_prefetch ? IPU6_ISYS_SPC_STATUS_ICACHE_PREFETCH : 0;

	writel(val, spc_regs_base + IPU6_ISYS_REG_SPC_STATUS_CTRL);
}

static int query_sp(struct ipu6_bus_device *adev)
{
	struct ipu6_isys *isys = ipu6_bus_get_drvdata(adev);
	void __iomem *spc_regs_base = isys->pdata->base +
		isys->pdata->ipdata->hw_variant.spc_offset;
	u32 val;

	val = readl(spc_regs_base + IPU6_ISYS_REG_SPC_STATUS_CTRL);
	/* return true when READY == 1, START == 0 */
	val &= IPU6_ISYS_SPC_STATUS_READY | IPU6_ISYS_SPC_STATUS_START;

	return val == IPU6_ISYS_SPC_STATUS_READY;
}

static int ipu6_isys_fwcom_cfg_init(struct ipu6_isys *isys,
				    struct ipu6_fw_com_cfg *fwcom,
				    unsigned int num_streams)
{
	unsigned int max_send_queues, max_sram_blocks, max_devq_size;
	struct ipu6_fw_syscom_queue_config *input_queue_cfg;
	struct ipu6_fw_syscom_queue_config *output_queue_cfg;
	struct device *dev = &isys->adev->auxdev.dev;
	int type_proxy = IPU6_FW_ISYS_QUEUE_TYPE_PROXY;
	int type_dev = IPU6_FW_ISYS_QUEUE_TYPE_DEV;
	int type_msg = IPU6_FW_ISYS_QUEUE_TYPE_MSG;
	int base_dev_send = IPU6_BASE_DEV_SEND_QUEUES;
	int base_msg_send = IPU6_BASE_MSG_SEND_QUEUES;
	int base_msg_recv = IPU6_BASE_MSG_RECV_QUEUES;
	struct ipu6_fw_isys_fw_config *isys_fw_cfg;
	u32 num_in_message_queues;
	unsigned int max_streams;
	unsigned int size;
	unsigned int i;

	max_streams = isys->pdata->ipdata->max_streams;
	max_send_queues = isys->pdata->ipdata->max_send_queues;
	max_sram_blocks = isys->pdata->ipdata->max_sram_blocks;
	max_devq_size = isys->pdata->ipdata->max_devq_size;
	num_in_message_queues = clamp(num_streams, 1U, max_streams);
	isys_fw_cfg = devm_kzalloc(dev, sizeof(*isys_fw_cfg), GFP_KERNEL);
	if (!isys_fw_cfg)
		return -ENOMEM;

	isys_fw_cfg->num_send_queues[type_proxy] = IPU6_N_MAX_PROXY_SEND_QUEUES;
	isys_fw_cfg->num_send_queues[type_dev] = IPU6_N_MAX_DEV_SEND_QUEUES;
	isys_fw_cfg->num_send_queues[type_msg] = num_in_message_queues;
	isys_fw_cfg->num_recv_queues[type_proxy] = IPU6_N_MAX_PROXY_RECV_QUEUES;
	/* Common msg/dev return queue */
	isys_fw_cfg->num_recv_queues[type_dev] = 0;
	isys_fw_cfg->num_recv_queues[type_msg] = 1;

	size = sizeof(*input_queue_cfg) * max_send_queues;
	input_queue_cfg = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!input_queue_cfg)
		return -ENOMEM;

	size = sizeof(*output_queue_cfg) * IPU6_N_MAX_RECV_QUEUES;
	output_queue_cfg = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!output_queue_cfg)
		return -ENOMEM;

	fwcom->input = input_queue_cfg;
	fwcom->output = output_queue_cfg;

	fwcom->num_input_queues = isys_fw_cfg->num_send_queues[type_proxy] +
		isys_fw_cfg->num_send_queues[type_dev] +
		isys_fw_cfg->num_send_queues[type_msg];

	fwcom->num_output_queues = isys_fw_cfg->num_recv_queues[type_proxy] +
		isys_fw_cfg->num_recv_queues[type_dev] +
		isys_fw_cfg->num_recv_queues[type_msg];

	/* SRAM partitioning. Equal partitioning is set. */
	for (i = 0; i < max_sram_blocks; i++) {
		if (i < num_in_message_queues)
			isys_fw_cfg->buffer_partition.num_gda_pages[i] =
				(IPU6_DEVICE_GDA_NR_PAGES *
				 IPU6_DEVICE_GDA_VIRT_FACTOR) /
				num_in_message_queues;
		else
			isys_fw_cfg->buffer_partition.num_gda_pages[i] = 0;
	}

	/* FW assumes proxy interface at fwcom queue 0 */
	for (i = 0; i < isys_fw_cfg->num_send_queues[type_proxy]; i++) {
		input_queue_cfg[i].token_size =
			sizeof(struct ipu6_fw_proxy_send_queue_token);
		input_queue_cfg[i].queue_size = IPU6_ISYS_SIZE_PROXY_SEND_QUEUE;
	}

	for (i = 0; i < isys_fw_cfg->num_send_queues[type_dev]; i++) {
		input_queue_cfg[base_dev_send + i].token_size =
			sizeof(struct ipu6_fw_send_queue_token);
		input_queue_cfg[base_dev_send + i].queue_size = max_devq_size;
	}

	for (i = 0; i < isys_fw_cfg->num_send_queues[type_msg]; i++) {
		input_queue_cfg[base_msg_send + i].token_size =
			sizeof(struct ipu6_fw_send_queue_token);
		input_queue_cfg[base_msg_send + i].queue_size =
			IPU6_ISYS_SIZE_SEND_QUEUE;
	}

	for (i = 0; i < isys_fw_cfg->num_recv_queues[type_proxy]; i++) {
		output_queue_cfg[i].token_size =
			sizeof(struct ipu6_fw_proxy_resp_queue_token);
		output_queue_cfg[i].queue_size =
			IPU6_ISYS_SIZE_PROXY_RECV_QUEUE;
	}
	/* There is no recv DEV queue */
	for (i = 0; i < isys_fw_cfg->num_recv_queues[type_msg]; i++) {
		output_queue_cfg[base_msg_recv + i].token_size =
			sizeof(struct ipu6_fw_resp_queue_token);
		output_queue_cfg[base_msg_recv + i].queue_size =
			IPU6_ISYS_SIZE_RECV_QUEUE;
	}

	fwcom->dmem_addr = isys->pdata->ipdata->hw_variant.dmem_offset;
	fwcom->specific_addr = isys_fw_cfg;
	fwcom->specific_size = sizeof(*isys_fw_cfg);

	return 0;
}

int ipu6_fw_isys_init(struct ipu6_isys *isys, unsigned int num_streams)
{
	struct device *dev = &isys->adev->auxdev.dev;
	int retry = IPU6_ISYS_OPEN_RETRY;
	struct ipu6_fw_com_cfg fwcom = {
		.cell_start = start_sp,
		.cell_ready = query_sp,
		.buttress_boot_offset = SYSCOM_BUTTRESS_FW_PARAMS_ISYS_OFFSET,
	};
	int ret;

	ipu6_isys_fwcom_cfg_init(isys, &fwcom, num_streams);

	isys->fwcom = ipu6_fw_com_prepare(&fwcom, isys->adev,
					  isys->pdata->base);
	if (!isys->fwcom) {
		dev_err(dev, "isys fw com prepare failed\n");
		return -EIO;
	}

	ret = ipu6_fw_com_open(isys->fwcom);
	if (ret) {
		dev_err(dev, "isys fw com open failed %d\n", ret);
		return ret;
	}

	do {
		usleep_range(400, 500);
		if (ipu6_fw_com_ready(isys->fwcom))
			break;
		retry--;
	} while (retry > 0);

	if (!retry) {
		dev_err(dev, "isys port open ready failed %d\n", ret);
		ipu6_fw_isys_close(isys);
		ret = -EIO;
	}

	return ret;
}

struct ipu6_fw_isys_resp_info_abi *
ipu6_fw_isys_get_resp(void *context, unsigned int queue)
{
	return ipu6_recv_get_token(context, queue);
}

void ipu6_fw_isys_put_resp(void *context, unsigned int queue)
{
	ipu6_recv_put_token(context, queue);
}

void ipu6_fw_isys_dump_stream_cfg(struct device *dev,
				  struct ipu6_fw_isys_stream_cfg_data_abi *cfg)
{
	unsigned int i;

	dev_dbg(dev, "-----------------------------------------------------\n");
	dev_dbg(dev, "IPU6_FW_ISYS_STREAM_CFG_DATA\n");

	dev_dbg(dev, "compfmt = %d\n", cfg->vc);
	dev_dbg(dev, "src = %d\n", cfg->src);
	dev_dbg(dev, "vc = %d\n", cfg->vc);
	dev_dbg(dev, "isl_use = %d\n", cfg->isl_use);
	dev_dbg(dev, "sensor_type = %d\n", cfg->sensor_type);

	dev_dbg(dev, "send_irq_sof_discarded = %d\n",
		cfg->send_irq_sof_discarded);
	dev_dbg(dev, "send_irq_eof_discarded = %d\n",
		cfg->send_irq_eof_discarded);
	dev_dbg(dev, "send_resp_sof_discarded = %d\n",
		cfg->send_resp_sof_discarded);
	dev_dbg(dev, "send_resp_eof_discarded = %d\n",
		cfg->send_resp_eof_discarded);

	dev_dbg(dev, "crop:\n");
	dev_dbg(dev, "\t.left_top = [%d, %d]\n", cfg->crop.left_offset,
		cfg->crop.top_offset);
	dev_dbg(dev, "\t.right_bottom = [%d, %d]\n", cfg->crop.right_offset,
		cfg->crop.bottom_offset);

	dev_dbg(dev, "nof_input_pins = %d\n", cfg->nof_input_pins);
	for (i = 0; i < cfg->nof_input_pins; i++) {
		dev_dbg(dev, "input pin[%d]:\n", i);
		dev_dbg(dev, "\t.dt = 0x%0x\n", cfg->input_pins[i].dt);
		dev_dbg(dev, "\t.mipi_store_mode = %d\n",
			cfg->input_pins[i].mipi_store_mode);
		dev_dbg(dev, "\t.bits_per_pix = %d\n",
			cfg->input_pins[i].bits_per_pix);
		dev_dbg(dev, "\t.mapped_dt = 0x%0x\n",
			cfg->input_pins[i].mapped_dt);
		dev_dbg(dev, "\t.input_res = %dx%d\n",
			cfg->input_pins[i].input_res.width,
			cfg->input_pins[i].input_res.height);
		dev_dbg(dev, "\t.mipi_decompression = %d\n",
			cfg->input_pins[i].mipi_decompression);
		dev_dbg(dev, "\t.capture_mode = %d\n",
			cfg->input_pins[i].capture_mode);
	}

	dev_dbg(dev, "nof_output_pins = %d\n", cfg->nof_output_pins);
	for (i = 0; i < cfg->nof_output_pins; i++) {
		dev_dbg(dev, "output_pin[%d]:\n", i);
		dev_dbg(dev, "\t.input_pin_id = %d\n",
			cfg->output_pins[i].input_pin_id);
		dev_dbg(dev, "\t.output_res = %dx%d\n",
			cfg->output_pins[i].output_res.width,
			cfg->output_pins[i].output_res.height);
		dev_dbg(dev, "\t.stride = %d\n", cfg->output_pins[i].stride);
		dev_dbg(dev, "\t.pt = %d\n", cfg->output_pins[i].pt);
		dev_dbg(dev, "\t.payload_buf_size = %d\n",
			cfg->output_pins[i].payload_buf_size);
		dev_dbg(dev, "\t.ft = %d\n", cfg->output_pins[i].ft);
		dev_dbg(dev, "\t.watermark_in_lines = %d\n",
			cfg->output_pins[i].watermark_in_lines);
		dev_dbg(dev, "\t.send_irq = %d\n",
			cfg->output_pins[i].send_irq);
		dev_dbg(dev, "\t.reserve_compression = %d\n",
			cfg->output_pins[i].reserve_compression);
		dev_dbg(dev, "\t.snoopable = %d\n",
			cfg->output_pins[i].snoopable);
		dev_dbg(dev, "\t.error_handling_enable = %d\n",
			cfg->output_pins[i].error_handling_enable);
		dev_dbg(dev, "\t.sensor_type = %d\n",
			cfg->output_pins[i].sensor_type);
	}
	dev_dbg(dev, "-----------------------------------------------------\n");
}

void
ipu6_fw_isys_dump_frame_buff_set(struct device *dev,
				 struct ipu6_fw_isys_frame_buff_set_abi *buf,
				 unsigned int outputs)
{
	unsigned int i;

	dev_dbg(dev, "-----------------------------------------------------\n");
	dev_dbg(dev, "IPU6_FW_ISYS_FRAME_BUFF_SET\n");

	for (i = 0; i < outputs; i++) {
		dev_dbg(dev, "output_pin[%d]:\n", i);
		dev_dbg(dev, "\t.out_buf_id = %llu\n",
			buf->output_pins[i].out_buf_id);
		dev_dbg(dev, "\t.addr = 0x%x\n", buf->output_pins[i].addr);
		dev_dbg(dev, "\t.compress = %d\n",
			buf->output_pins[i].compress);
	}

	dev_dbg(dev, "send_irq_sof = 0x%x\n", buf->send_irq_sof);
	dev_dbg(dev, "send_irq_eof = 0x%x\n", buf->send_irq_eof);
	dev_dbg(dev, "send_resp_sof = 0x%x\n", buf->send_resp_sof);
	dev_dbg(dev, "send_resp_eof = 0x%x\n", buf->send_resp_eof);
	dev_dbg(dev, "send_irq_capture_ack = 0x%x\n",
		buf->send_irq_capture_ack);
	dev_dbg(dev, "send_irq_capture_done = 0x%x\n",
		buf->send_irq_capture_done);
	dev_dbg(dev, "send_resp_capture_ack = 0x%x\n",
		buf->send_resp_capture_ack);
	dev_dbg(dev, "send_resp_capture_done = 0x%x\n",
		buf->send_resp_capture_done);

	dev_dbg(dev, "-----------------------------------------------------\n");
}
