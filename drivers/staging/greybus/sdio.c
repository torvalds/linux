/*
 * SD/MMC Greybus driver.
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>

#include "greybus.h"
#include "gbphy.h"

struct gb_sdio_host {
	struct gb_connection	*connection;
	struct gbphy_device	*gbphy_dev;
	struct mmc_host		*mmc;
	struct mmc_request	*mrq;
	struct mutex		lock;	/* lock for this host */
	size_t			data_max;
	spinlock_t		xfer;	/* lock to cancel ongoing transfer */
	bool			xfer_stop;
	struct workqueue_struct	*mrq_workqueue;
	struct work_struct	mrqwork;
	u8			queued_events;
	bool			removed;
	bool			card_present;
	bool			read_only;
};


#define GB_SDIO_RSP_R1_R5_R6_R7	(GB_SDIO_RSP_PRESENT | GB_SDIO_RSP_CRC | \
				 GB_SDIO_RSP_OPCODE)
#define GB_SDIO_RSP_R3_R4	(GB_SDIO_RSP_PRESENT)
#define GB_SDIO_RSP_R2		(GB_SDIO_RSP_PRESENT | GB_SDIO_RSP_CRC | \
				 GB_SDIO_RSP_136)
#define GB_SDIO_RSP_R1B		(GB_SDIO_RSP_PRESENT | GB_SDIO_RSP_CRC | \
				 GB_SDIO_RSP_OPCODE | GB_SDIO_RSP_BUSY)

/* kernel vdd starts at 0x80 and we need to translate to greybus ones 0x01 */
#define GB_SDIO_VDD_SHIFT	8

static inline bool single_op(struct mmc_command *cmd)
{
	uint32_t opcode = cmd->opcode;

	return opcode == MMC_WRITE_BLOCK ||
	       opcode == MMC_READ_SINGLE_BLOCK;
}

static void _gb_sdio_set_host_caps(struct gb_sdio_host *host, u32 r)
{
	u32 caps = 0;
	u32 caps2 = 0;

	caps = ((r & GB_SDIO_CAP_NONREMOVABLE) ? MMC_CAP_NONREMOVABLE : 0) |
		((r & GB_SDIO_CAP_4_BIT_DATA) ? MMC_CAP_4_BIT_DATA : 0) |
		((r & GB_SDIO_CAP_8_BIT_DATA) ? MMC_CAP_8_BIT_DATA : 0) |
		((r & GB_SDIO_CAP_MMC_HS) ? MMC_CAP_MMC_HIGHSPEED : 0) |
		((r & GB_SDIO_CAP_SD_HS) ? MMC_CAP_SD_HIGHSPEED : 0) |
		((r & GB_SDIO_CAP_ERASE) ? MMC_CAP_ERASE : 0) |
		((r & GB_SDIO_CAP_1_2V_DDR) ? MMC_CAP_1_2V_DDR : 0) |
		((r & GB_SDIO_CAP_1_8V_DDR) ? MMC_CAP_1_8V_DDR : 0) |
		((r & GB_SDIO_CAP_POWER_OFF_CARD) ? MMC_CAP_POWER_OFF_CARD : 0) |
		((r & GB_SDIO_CAP_UHS_SDR12) ? MMC_CAP_UHS_SDR12 : 0) |
		((r & GB_SDIO_CAP_UHS_SDR25) ? MMC_CAP_UHS_SDR25 : 0) |
		((r & GB_SDIO_CAP_UHS_SDR50) ? MMC_CAP_UHS_SDR50 : 0) |
		((r & GB_SDIO_CAP_UHS_SDR104) ? MMC_CAP_UHS_SDR104 : 0) |
		((r & GB_SDIO_CAP_UHS_DDR50) ? MMC_CAP_UHS_DDR50 : 0) |
		((r & GB_SDIO_CAP_DRIVER_TYPE_A) ? MMC_CAP_DRIVER_TYPE_A : 0) |
		((r & GB_SDIO_CAP_DRIVER_TYPE_C) ? MMC_CAP_DRIVER_TYPE_C : 0) |
		((r & GB_SDIO_CAP_DRIVER_TYPE_D) ? MMC_CAP_DRIVER_TYPE_D : 0);

	caps2 = ((r & GB_SDIO_CAP_HS200_1_2V) ? MMC_CAP2_HS200_1_2V_SDR : 0) |
#ifdef MMC_HS400_SUPPORTED
		((r & GB_SDIO_CAP_HS400_1_2V) ? MMC_CAP2_HS400_1_2V : 0) |
		((r & GB_SDIO_CAP_HS400_1_8V) ? MMC_CAP2_HS400_1_8V : 0) |
#endif
		((r & GB_SDIO_CAP_HS200_1_8V) ? MMC_CAP2_HS200_1_8V_SDR : 0);

	host->mmc->caps = caps | MMC_CAP_NEEDS_POLL;
	host->mmc->caps2 = caps2;

	if (caps & MMC_CAP_NONREMOVABLE)
		host->card_present = true;
}

static u32 _gb_sdio_get_host_ocr(u32 ocr)
{
	return (((ocr & GB_SDIO_VDD_165_195) ? MMC_VDD_165_195 : 0) |
		((ocr & GB_SDIO_VDD_20_21) ? MMC_VDD_20_21 : 0) |
		((ocr & GB_SDIO_VDD_21_22) ? MMC_VDD_21_22 : 0) |
		((ocr & GB_SDIO_VDD_22_23) ? MMC_VDD_22_23 : 0) |
		((ocr & GB_SDIO_VDD_23_24) ? MMC_VDD_23_24 : 0) |
		((ocr & GB_SDIO_VDD_24_25) ? MMC_VDD_24_25 : 0) |
		((ocr & GB_SDIO_VDD_25_26) ? MMC_VDD_25_26 : 0) |
		((ocr & GB_SDIO_VDD_26_27) ? MMC_VDD_26_27 : 0) |
		((ocr & GB_SDIO_VDD_27_28) ? MMC_VDD_27_28 : 0) |
		((ocr & GB_SDIO_VDD_28_29) ? MMC_VDD_28_29 : 0) |
		((ocr & GB_SDIO_VDD_29_30) ? MMC_VDD_29_30 : 0) |
		((ocr & GB_SDIO_VDD_30_31) ? MMC_VDD_30_31 : 0) |
		((ocr & GB_SDIO_VDD_31_32) ? MMC_VDD_31_32 : 0) |
		((ocr & GB_SDIO_VDD_32_33) ? MMC_VDD_32_33 : 0) |
		((ocr & GB_SDIO_VDD_33_34) ? MMC_VDD_33_34 : 0) |
		((ocr & GB_SDIO_VDD_34_35) ? MMC_VDD_34_35 : 0) |
		((ocr & GB_SDIO_VDD_35_36) ? MMC_VDD_35_36 : 0)
		);
}

static int gb_sdio_get_caps(struct gb_sdio_host *host)
{
	struct gb_sdio_get_caps_response response;
	struct mmc_host *mmc = host->mmc;
	u16 data_max;
	u32 blksz;
	u32 ocr;
	u32 r;
	int ret;

	ret = gb_operation_sync(host->connection, GB_SDIO_TYPE_GET_CAPABILITIES,
				NULL, 0, &response, sizeof(response));
	if (ret < 0)
		return ret;
	r = le32_to_cpu(response.caps);

	_gb_sdio_set_host_caps(host, r);

	/* get the max block size that could fit our payload */
	data_max = gb_operation_get_payload_size_max(host->connection);
	data_max = min(data_max - sizeof(struct gb_sdio_transfer_request),
		       data_max - sizeof(struct gb_sdio_transfer_response));

	blksz = min(le16_to_cpu(response.max_blk_size), data_max);
	blksz = max_t(u32, 512, blksz);

	mmc->max_blk_size = rounddown_pow_of_two(blksz);
	mmc->max_blk_count = le16_to_cpu(response.max_blk_count);
	host->data_max = data_max;

	/* get ocr supported values */
	ocr = _gb_sdio_get_host_ocr(le32_to_cpu(response.ocr));
	mmc->ocr_avail = ocr;
	mmc->ocr_avail_sdio = mmc->ocr_avail;
	mmc->ocr_avail_sd = mmc->ocr_avail;
	mmc->ocr_avail_mmc = mmc->ocr_avail;

	/* get frequency range values */
	mmc->f_min = le32_to_cpu(response.f_min);
	mmc->f_max = le32_to_cpu(response.f_max);

	return 0;
}

static void _gb_queue_event(struct gb_sdio_host *host, u8 event)
{
	if (event & GB_SDIO_CARD_INSERTED)
		host->queued_events &= ~GB_SDIO_CARD_REMOVED;
	else if (event & GB_SDIO_CARD_REMOVED)
		host->queued_events &= ~GB_SDIO_CARD_INSERTED;

	host->queued_events |= event;
}

static int _gb_sdio_process_events(struct gb_sdio_host *host, u8 event)
{
	u8 state_changed = 0;

	if (event & GB_SDIO_CARD_INSERTED) {
		if (host->mmc->caps & MMC_CAP_NONREMOVABLE)
			return 0;
		if (host->card_present)
			return 0;
		host->card_present = true;
		state_changed = 1;
	}

	if (event & GB_SDIO_CARD_REMOVED) {
		if (host->mmc->caps & MMC_CAP_NONREMOVABLE)
			return 0;
		if (!(host->card_present))
			return 0;
		host->card_present = false;
		state_changed = 1;
	}

	if (event & GB_SDIO_WP) {
		host->read_only = true;
	}

	if (state_changed) {
		dev_info(mmc_dev(host->mmc), "card %s now event\n",
			 (host->card_present ?  "inserted" : "removed"));
		mmc_detect_change(host->mmc, 0);
	}

	return 0;
}

static int gb_sdio_request_handler(struct gb_operation *op)
{
	struct gb_sdio_host *host = gb_connection_get_data(op->connection);
	struct gb_message *request;
	struct gb_sdio_event_request *payload;
	u8 type = op->type;
	int ret =  0;
	u8 event;

	if (type != GB_SDIO_TYPE_EVENT) {
		dev_err(mmc_dev(host->mmc),
			"unsupported unsolicited event: %u\n", type);
		return -EINVAL;
	}

	request = op->request;

	if (request->payload_size < sizeof(*payload)) {
		dev_err(mmc_dev(host->mmc), "wrong event size received (%zu < %zu)\n",
			request->payload_size, sizeof(*payload));
		return -EINVAL;
	}

	payload = request->payload;
	event = payload->event;

	if (host->removed)
		_gb_queue_event(host, event);
	else
		ret = _gb_sdio_process_events(host, event);

	return ret;
}

static int gb_sdio_set_ios(struct gb_sdio_host *host,
			   struct gb_sdio_set_ios_request *request)
{
	return gb_operation_sync(host->connection, GB_SDIO_TYPE_SET_IOS,
				 request, sizeof(*request), NULL, 0);
}

static int _gb_sdio_send(struct gb_sdio_host *host, struct mmc_data *data,
			 size_t len, u16 nblocks, off_t skip)
{
	struct gb_sdio_transfer_request *request;
	struct gb_sdio_transfer_response *response;
	struct gb_operation *operation;
	struct scatterlist *sg = data->sg;
	unsigned int sg_len = data->sg_len;
	size_t copied;
	u16 send_blksz;
	u16 send_blocks;
	int ret;

	WARN_ON(len > host->data_max);

	operation = gb_operation_create(host->connection, GB_SDIO_TYPE_TRANSFER,
					len + sizeof(*request),
					sizeof(*response), GFP_KERNEL);
	if (!operation)
		return -ENOMEM;

	request = operation->request->payload;
	request->data_flags = (data->flags >> 8);
	request->data_blocks = cpu_to_le16(nblocks);
	request->data_blksz = cpu_to_le16(data->blksz);

	copied = sg_pcopy_to_buffer(sg, sg_len, &request->data[0], len, skip);

	if (copied != len) {
		ret = -EINVAL;
		goto err_put_operation;
	}

	ret = gb_operation_request_send_sync(operation);
	if (ret < 0)
		goto err_put_operation;

	response = operation->response->payload;

	send_blocks = le16_to_cpu(response->data_blocks);
	send_blksz = le16_to_cpu(response->data_blksz);

	if (len != send_blksz * send_blocks) {
		dev_err(mmc_dev(host->mmc), "send: size received: %zu != %d\n",
			len, send_blksz * send_blocks);
		ret = -EINVAL;
	}

err_put_operation:
	gb_operation_put(operation);

	return ret;
}

static int _gb_sdio_recv(struct gb_sdio_host *host, struct mmc_data *data,
			 size_t len, u16 nblocks, off_t skip)
{
	struct gb_sdio_transfer_request *request;
	struct gb_sdio_transfer_response *response;
	struct gb_operation *operation;
	struct scatterlist *sg = data->sg;
	unsigned int sg_len = data->sg_len;
	size_t copied;
	u16 recv_blksz;
	u16 recv_blocks;
	int ret;

	WARN_ON(len > host->data_max);

	operation = gb_operation_create(host->connection, GB_SDIO_TYPE_TRANSFER,
					sizeof(*request),
					len + sizeof(*response), GFP_KERNEL);
	if (!operation)
		return -ENOMEM;

	request = operation->request->payload;
	request->data_flags = (data->flags >> 8);
	request->data_blocks = cpu_to_le16(nblocks);
	request->data_blksz = cpu_to_le16(data->blksz);

	ret = gb_operation_request_send_sync(operation);
	if (ret < 0)
		goto err_put_operation;

	response = operation->response->payload;
	recv_blocks = le16_to_cpu(response->data_blocks);
	recv_blksz = le16_to_cpu(response->data_blksz);

	if (len != recv_blksz * recv_blocks) {
		dev_err(mmc_dev(host->mmc), "recv: size received: %d != %zu\n",
			recv_blksz * recv_blocks, len);
		ret = -EINVAL;
		goto err_put_operation;
	}

	copied = sg_pcopy_from_buffer(sg, sg_len, &response->data[0], len,
				      skip);
	if (copied != len)
		ret = -EINVAL;

err_put_operation:
	gb_operation_put(operation);

	return ret;
}

static int gb_sdio_transfer(struct gb_sdio_host *host, struct mmc_data *data)
{
	size_t left, len;
	off_t skip = 0;
	int ret = 0;
	u16 nblocks;

	if (single_op(data->mrq->cmd) && data->blocks > 1) {
		ret = -ETIMEDOUT;
		goto out;
	}

	left = data->blksz * data->blocks;

	while (left) {
		/* check is a stop transmission is pending */
		spin_lock(&host->xfer);
		if (host->xfer_stop) {
			host->xfer_stop = false;
			spin_unlock(&host->xfer);
			ret = -EINTR;
			goto out;
		}
		spin_unlock(&host->xfer);
		len = min(left, host->data_max);
		nblocks = len / data->blksz;
		len = nblocks * data->blksz;

		if (data->flags & MMC_DATA_READ) {
			ret = _gb_sdio_recv(host, data, len, nblocks, skip);
			if (ret < 0)
				goto out;
		} else {
			ret = _gb_sdio_send(host, data, len, nblocks, skip);
			if (ret < 0)
				goto out;
		}
		data->bytes_xfered += len;
		left -= len;
		skip += len;
	}

out:
	data->error = ret;
	return ret;
}

static int gb_sdio_command(struct gb_sdio_host *host, struct mmc_command *cmd)
{
	struct gb_sdio_command_request request = {0};
	struct gb_sdio_command_response response;
	struct mmc_data *data = host->mrq->data;
	u8 cmd_flags;
	u8 cmd_type;
	int i;
	int ret;

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		cmd_flags = GB_SDIO_RSP_NONE;
		break;
	case MMC_RSP_R1:
		cmd_flags = GB_SDIO_RSP_R1_R5_R6_R7;
		break;
	case MMC_RSP_R1B:
		cmd_flags = GB_SDIO_RSP_R1B;
		break;
	case MMC_RSP_R2:
		cmd_flags = GB_SDIO_RSP_R2;
		break;
	case MMC_RSP_R3:
		cmd_flags = GB_SDIO_RSP_R3_R4;
		break;
	default:
		dev_err(mmc_dev(host->mmc), "cmd flag invalid 0x%04x\n",
			mmc_resp_type(cmd));
		ret = -EINVAL;
		goto out;
	}

	switch (mmc_cmd_type(cmd)) {
	case MMC_CMD_BC:
		cmd_type = GB_SDIO_CMD_BC;
		break;
	case MMC_CMD_BCR:
		cmd_type = GB_SDIO_CMD_BCR;
		break;
	case MMC_CMD_AC:
		cmd_type = GB_SDIO_CMD_AC;
		break;
	case MMC_CMD_ADTC:
		cmd_type = GB_SDIO_CMD_ADTC;
		break;
	default:
		dev_err(mmc_dev(host->mmc), "cmd type invalid 0x%04x\n",
			mmc_cmd_type(cmd));
		ret = -EINVAL;
		goto out;
	}

	request.cmd = cmd->opcode;
	request.cmd_flags = cmd_flags;
	request.cmd_type = cmd_type;
	request.cmd_arg = cpu_to_le32(cmd->arg);
	/* some controllers need to know at command time data details */
	if (data) {
		request.data_blocks = cpu_to_le16(data->blocks);
		request.data_blksz = cpu_to_le16(data->blksz);
	}

	ret = gb_operation_sync(host->connection, GB_SDIO_TYPE_COMMAND,
				&request, sizeof(request), &response,
				sizeof(response));
	if (ret < 0)
		goto out;

	/* no response expected */
	if (cmd_flags & GB_SDIO_RSP_NONE)
		goto out;

	/* long response expected */
	if (cmd_flags & GB_SDIO_RSP_R2)
		for (i = 0; i < 4; i++)
			cmd->resp[i] = le32_to_cpu(response.resp[i]);
	else
		cmd->resp[0] = le32_to_cpu(response.resp[0]);

out:
	cmd->error = ret;
	return ret;
}

static void gb_sdio_mrq_work(struct work_struct *work)
{
	struct gb_sdio_host *host;
	struct mmc_request *mrq;
	int ret;

	host = container_of(work, struct gb_sdio_host, mrqwork);

	mutex_lock(&host->lock);
	mrq = host->mrq;
	if (!mrq) {
		mutex_unlock(&host->lock);
		dev_err(mmc_dev(host->mmc), "mmc request is NULL");
		return;
	}

	if (host->removed) {
		mrq->cmd->error = -ESHUTDOWN;
		goto done;
	}

	if (mrq->sbc) {
		ret = gb_sdio_command(host, mrq->sbc);
		if (ret < 0)
			goto done;
	}

	ret = gb_sdio_command(host, mrq->cmd);
	if (ret < 0)
		goto done;

	if (mrq->data) {
		ret = gb_sdio_transfer(host, mrq->data);
		if (ret < 0)
			goto done;
	}

	if (mrq->stop) {
		ret = gb_sdio_command(host, mrq->stop);
		if (ret < 0)
			goto done;
	}

done:
	host->mrq = NULL;
	mutex_unlock(&host->lock);
	mmc_request_done(host->mmc, mrq);
}

static void gb_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct gb_sdio_host *host = mmc_priv(mmc);
	struct mmc_command *cmd = mrq->cmd;

	/* Check if it is a cancel to ongoing transfer */
	if (cmd->opcode == MMC_STOP_TRANSMISSION) {
		spin_lock(&host->xfer);
		host->xfer_stop = true;
		spin_unlock(&host->xfer);
	}

	mutex_lock(&host->lock);

	WARN_ON(host->mrq);
	host->mrq = mrq;

	if (host->removed) {
		mrq->cmd->error = -ESHUTDOWN;
		goto out;
	}
	if (!host->card_present) {
		mrq->cmd->error = -ENOMEDIUM;
		goto out;
	}

	queue_work(host->mrq_workqueue, &host->mrqwork);

	mutex_unlock(&host->lock);
	return;

out:
	host->mrq = NULL;
	mutex_unlock(&host->lock);
	mmc_request_done(mmc, mrq);
}

static void gb_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct gb_sdio_host *host = mmc_priv(mmc);
	struct gb_sdio_set_ios_request request;
	int ret;
	u8 power_mode;
	u8 bus_width;
	u8 timing;
	u8 signal_voltage;
	u8 drv_type;
	u32 vdd = 0;

	mutex_lock(&host->lock);
	request.clock = cpu_to_le32(ios->clock);

	if (ios->vdd)
		vdd = 1 << (ios->vdd - GB_SDIO_VDD_SHIFT);
	request.vdd = cpu_to_le32(vdd);

	request.bus_mode = (ios->bus_mode == MMC_BUSMODE_OPENDRAIN ?
			    GB_SDIO_BUSMODE_OPENDRAIN :
			    GB_SDIO_BUSMODE_PUSHPULL);

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
	default:
		power_mode = GB_SDIO_POWER_OFF;
		break;
	case MMC_POWER_UP:
		power_mode = GB_SDIO_POWER_UP;
		break;
	case MMC_POWER_ON:
		power_mode = GB_SDIO_POWER_ON;
		break;
#ifdef MMC_POWER_UNDEFINED_SUPPORTED
	case MMC_POWER_UNDEFINED:
		power_mode = GB_SDIO_POWER_UNDEFINED;
		break;
#endif
	}
	request.power_mode = power_mode;

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		bus_width = GB_SDIO_BUS_WIDTH_1;
		break;
	case MMC_BUS_WIDTH_4:
	default:
		bus_width = GB_SDIO_BUS_WIDTH_4;
		break;
	case MMC_BUS_WIDTH_8:
		bus_width = GB_SDIO_BUS_WIDTH_8;
		break;
	}
	request.bus_width = bus_width;

	switch (ios->timing) {
	case MMC_TIMING_LEGACY:
	default:
		timing = GB_SDIO_TIMING_LEGACY;
		break;
	case MMC_TIMING_MMC_HS:
		timing = GB_SDIO_TIMING_MMC_HS;
		break;
	case MMC_TIMING_SD_HS:
		timing = GB_SDIO_TIMING_SD_HS;
		break;
	case MMC_TIMING_UHS_SDR12:
		timing = GB_SDIO_TIMING_UHS_SDR12;
		break;
	case MMC_TIMING_UHS_SDR25:
		timing = GB_SDIO_TIMING_UHS_SDR25;
		break;
	case MMC_TIMING_UHS_SDR50:
		timing = GB_SDIO_TIMING_UHS_SDR50;
		break;
	case MMC_TIMING_UHS_SDR104:
		timing = GB_SDIO_TIMING_UHS_SDR104;
		break;
	case MMC_TIMING_UHS_DDR50:
		timing = GB_SDIO_TIMING_UHS_DDR50;
		break;
#ifdef MMC_DDR52_DEFINED
	case MMC_TIMING_MMC_DDR52:
		timing = GB_SDIO_TIMING_MMC_DDR52;
		break;
#endif
	case MMC_TIMING_MMC_HS200:
		timing = GB_SDIO_TIMING_MMC_HS200;
		break;
#ifdef MMC_HS400_SUPPORTED
	case MMC_TIMING_MMC_HS400:
		timing = GB_SDIO_TIMING_MMC_HS400;
		break;
#endif
	}
	request.timing = timing;

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		signal_voltage = GB_SDIO_SIGNAL_VOLTAGE_330;
		break;
	case MMC_SIGNAL_VOLTAGE_180:
	default:
		signal_voltage = GB_SDIO_SIGNAL_VOLTAGE_180;
		break;
	case MMC_SIGNAL_VOLTAGE_120:
		signal_voltage = GB_SDIO_SIGNAL_VOLTAGE_120;
		break;
	}
	request.signal_voltage = signal_voltage;

	switch (ios->drv_type) {
	case MMC_SET_DRIVER_TYPE_A:
		drv_type = GB_SDIO_SET_DRIVER_TYPE_A;
		break;
	case MMC_SET_DRIVER_TYPE_C:
		drv_type = GB_SDIO_SET_DRIVER_TYPE_C;
		break;
	case MMC_SET_DRIVER_TYPE_D:
		drv_type = GB_SDIO_SET_DRIVER_TYPE_D;
		break;
	case MMC_SET_DRIVER_TYPE_B:
	default:
		drv_type = GB_SDIO_SET_DRIVER_TYPE_B;
		break;
	}
	request.drv_type = drv_type;

	ret = gb_sdio_set_ios(host, &request);
	if (ret < 0)
		goto out;

	memcpy(&mmc->ios, ios, sizeof(mmc->ios));

out:
	mutex_unlock(&host->lock);
}

static int gb_mmc_get_ro(struct mmc_host *mmc)
{
	struct gb_sdio_host *host = mmc_priv(mmc);

	mutex_lock(&host->lock);
	if (host->removed) {
		mutex_unlock(&host->lock);
		return -ESHUTDOWN;
	}
	mutex_unlock(&host->lock);

	return host->read_only;
}

static int gb_mmc_get_cd(struct mmc_host *mmc)
{
	struct gb_sdio_host *host = mmc_priv(mmc);

	mutex_lock(&host->lock);
	if (host->removed) {
		mutex_unlock(&host->lock);
		return -ESHUTDOWN;
	}
	mutex_unlock(&host->lock);

	return host->card_present;
}

static const struct mmc_host_ops gb_sdio_ops = {
	.request	= gb_mmc_request,
	.set_ios	= gb_mmc_set_ios,
	.get_ro		= gb_mmc_get_ro,
	.get_cd		= gb_mmc_get_cd,
};

static int gb_sdio_probe(struct gbphy_device *gbphy_dev,
			 const struct gbphy_device_id *id)
{
	struct gb_connection *connection;
	struct mmc_host *mmc;
	struct gb_sdio_host *host;
	int ret = 0;

	mmc = mmc_alloc_host(sizeof(*host), &gbphy_dev->dev);
	if (!mmc)
		return -ENOMEM;

	connection = gb_connection_create(gbphy_dev->bundle,
					  le16_to_cpu(gbphy_dev->cport_desc->id),
					  gb_sdio_request_handler);
	if (IS_ERR(connection)) {
		ret = PTR_ERR(connection);
		goto exit_mmc_free;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->removed = true;

	host->connection = connection;
	gb_connection_set_data(connection, host);
	host->gbphy_dev = gbphy_dev;
	gb_gbphy_set_data(gbphy_dev, host);

	ret = gb_connection_enable_tx(connection);
	if (ret)
		goto exit_connection_destroy;

	ret = gb_sdio_get_caps(host);
	if (ret < 0)
		goto exit_connection_disable;

	mmc->ops = &gb_sdio_ops;

	/* for now we just make a map 1:1 between max blocks and segments */
	mmc->max_segs = host->mmc->max_blk_count;
	mmc->max_seg_size = host->mmc->max_blk_size;

	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;

	mutex_init(&host->lock);
	spin_lock_init(&host->xfer);
	host->mrq_workqueue = alloc_workqueue("mmc-%s", 0, 1,
					      dev_name(&gbphy_dev->dev));
	if (!host->mrq_workqueue) {
		ret = -ENOMEM;
		goto exit_connection_disable;
	}
	INIT_WORK(&host->mrqwork, gb_sdio_mrq_work);

	ret = gb_connection_enable(connection);
	if (ret)
		goto exit_wq_destroy;

	ret = mmc_add_host(mmc);
	if (ret < 0)
		goto exit_wq_destroy;
	host->removed = false;
	ret = _gb_sdio_process_events(host, host->queued_events);
	host->queued_events = 0;

	return ret;

exit_wq_destroy:
	destroy_workqueue(host->mrq_workqueue);
exit_connection_disable:
	gb_connection_disable(connection);
exit_connection_destroy:
	gb_connection_destroy(connection);
exit_mmc_free:
	mmc_free_host(mmc);

	return ret;
}

static void gb_sdio_remove(struct gbphy_device *gbphy_dev)
{
	struct gb_sdio_host *host = gb_gbphy_get_data(gbphy_dev);
	struct gb_connection *connection = host->connection;
	struct mmc_host *mmc;

	mutex_lock(&host->lock);
	host->removed = true;
	mmc = host->mmc;
	gb_connection_set_data(connection, NULL);
	mutex_unlock(&host->lock);

	flush_workqueue(host->mrq_workqueue);
	destroy_workqueue(host->mrq_workqueue);
	gb_connection_disable_rx(connection);
	mmc_remove_host(mmc);
	gb_connection_disable(connection);
	gb_connection_destroy(connection);
	mmc_free_host(mmc);
}

static const struct gbphy_device_id gb_sdio_id_table[] = {
	{ GBPHY_PROTOCOL(GREYBUS_PROTOCOL_SDIO) },
	{ },
};
MODULE_DEVICE_TABLE(gbphy, gb_sdio_id_table);

static struct gbphy_driver sdio_driver = {
	.name		= "sdio",
	.probe		= gb_sdio_probe,
	.remove		= gb_sdio_remove,
	.id_table	= gb_sdio_id_table,
};

module_gbphy_driver(sdio_driver);
MODULE_LICENSE("GPL v2");
