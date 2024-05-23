// SPDX-License-Identifier: GPL-2.0-only
/*
 * ISHTP firmware loader function
 *
 * Copyright (c) 2024, Intel Corporation.
 *
 * This module implements the functionality to load the main ISH firmware from the host, starting
 * with the Lunar Lake generation. It leverages a new method that enhances space optimization and
 * flexibility by dividing the ISH firmware into a bootloader and main firmware.
 *
 * Please refer to the [Documentation](Documentation/hid/intel-ish-hid.rst) for the details on
 * flows.
 *
 * Additionally, address potential error scenarios to ensure graceful failure handling.
 * - Firmware Image Not Found:
 *   Occurs when `request_firmware()` cannot locate the firmware image. The ISH firmware will
 *   remain in a state awaiting firmware loading from the host, with no further action from
 *   the ISHTP driver.
 *   Recovery: Re-insmod the ISH drivers allows for a retry of the firmware loading from the host.
 *
 * - DMA Buffer Allocation Failure:
 *   This happens if allocating a DMA buffer during `prepare_dma_bufs()` fails. The ISH firmware
 *   will stay in a waiting state, and the ISHTP driver will release any allocated DMA buffers and
 *   firmware without further actions.
 *   Recovery: Re-insmod the ISH drivers allows for a retry of the firmware loading from the host.
 *
 * - Incorrect Firmware Image:
 *   Using an incorrect firmware image will initiate the firmware loading process but will
 *   eventually be refused by the ISH firmware after three unsuccessful attempts, indicated by
 *   returning an error code. The ISHTP driver will stop attempting after three tries.
 *   Recovery: A platform reset is required to retry firmware loading from the host.
 */

#define dev_fmt(fmt) "ISH loader: " fmt

#include <linux/cacheflush.h>
#include <linux/container_of.h>
#include <linux/dev_printk.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/gfp_types.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/pfn.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "hbm.h"
#include "loader.h"

/**
 * loader_write_message() - Write a message to the ISHTP device
 * @dev: The ISHTP device
 * @buf: The buffer containing the message
 * @len: The length of the message
 *
 * Return: 0 on success, negative error code on failure
 */
static int loader_write_message(struct ishtp_device *dev, void *buf, int len)
{
	struct ishtp_msg_hdr ishtp_hdr = {
		.fw_addr = ISHTP_LOADER_CLIENT_ADDR,
		.length = len,
		.msg_complete = 1,
	};

	dev->fw_loader_received = false;

	return ishtp_write_message(dev, &ishtp_hdr, buf);
}

/**
 * loader_xfer_cmd() - Transfer a command to the ISHTP device
 * @dev: The ISHTP device
 * @req: The request buffer
 * @req_len: The length of the request
 * @resp: The response buffer
 * @resp_len: The length of the response
 *
 * Return: 0 on success, negative error code on failure
 */
static int loader_xfer_cmd(struct ishtp_device *dev, void *req, int req_len,
			   void *resp, int resp_len)
{
	struct loader_msg_header *req_hdr = req;
	struct loader_msg_header *resp_hdr = resp;
	struct device *devc = dev->devc;
	int rv;

	dev->fw_loader_rx_buf = resp;
	dev->fw_loader_rx_size = resp_len;

	rv = loader_write_message(dev, req, req_len);
	if (rv < 0) {
		dev_err(devc, "write cmd %u failed:%d\n", req_hdr->command, rv);
		return rv;
	}

	/* Wait the ACK */
	wait_event_interruptible_timeout(dev->wait_loader_recvd_msg, dev->fw_loader_received,
					 ISHTP_LOADER_TIMEOUT);
	dev->fw_loader_rx_size = 0;
	dev->fw_loader_rx_buf = NULL;
	if (!dev->fw_loader_received) {
		dev_err(devc, "wait response of cmd %u timeout\n", req_hdr->command);
		return -ETIMEDOUT;
	}

	if (!resp_hdr->is_response) {
		dev_err(devc, "not a response for %u\n", req_hdr->command);
		return -EBADMSG;
	}

	if (req_hdr->command != resp_hdr->command) {
		dev_err(devc, "unexpected cmd response %u:%u\n", req_hdr->command,
			resp_hdr->command);
		return -EBADMSG;
	}

	if (resp_hdr->status) {
		dev_err(devc, "cmd %u failed %u\n", req_hdr->command, resp_hdr->status);
		return -EIO;
	}

	return 0;
}

/**
 * release_dma_bufs() - Release the DMA buffer for transferring firmware fragments
 * @dev: The ISHTP device
 * @fragment: The ISHTP firmware fragment descriptor
 * @dma_bufs: The array of DMA fragment buffers
 * @fragment_size: The size of a single DMA fragment
 */
static void release_dma_bufs(struct ishtp_device *dev,
			     struct loader_xfer_dma_fragment *fragment,
			     void **dma_bufs, u32 fragment_size)
{
	dma_addr_t dma_addr;
	int i;

	for (i = 0; i < FRAGMENT_MAX_NUM; i++) {
		if (dma_bufs[i]) {
			dma_addr = le64_to_cpu(fragment->fragment_tbl[i].ddr_adrs);
			dma_free_coherent(dev->devc, fragment_size, dma_bufs[i], dma_addr);
			dma_bufs[i] = NULL;
		}
	}
}

/**
 * prepare_dma_bufs() - Prepare the DMA buffer for transferring firmware fragments
 * @dev: The ISHTP device
 * @ish_fw: The ISH firmware
 * @fragment: The ISHTP firmware fragment descriptor
 * @dma_bufs: The array of DMA fragment buffers
 * @fragment_size: The size of a single DMA fragment
 *
 * Return: 0 on success, negative error code on failure
 */
static int prepare_dma_bufs(struct ishtp_device *dev,
			    const struct firmware *ish_fw,
			    struct loader_xfer_dma_fragment *fragment,
			    void **dma_bufs, u32 fragment_size)
{
	dma_addr_t dma_addr;
	u32 offset = 0;
	int i;

	for (i = 0; i < fragment->fragment_cnt && offset < ish_fw->size; i++) {
		dma_bufs[i] = dma_alloc_coherent(dev->devc, fragment_size, &dma_addr, GFP_KERNEL);
		if (!dma_bufs[i])
			return -ENOMEM;

		fragment->fragment_tbl[i].ddr_adrs = cpu_to_le64(dma_addr);
		fragment->fragment_tbl[i].length = clamp(ish_fw->size - offset, 0, fragment_size);
		fragment->fragment_tbl[i].fw_off = offset;
		memcpy(dma_bufs[i], ish_fw->data + offset, fragment->fragment_tbl[i].length);
		clflush_cache_range(dma_bufs[i], fragment_size);

		offset += fragment->fragment_tbl[i].length;
	}

	return 0;
}

/**
 * ishtp_loader_work() - Load the ISHTP firmware
 * @work: The work structure
 *
 * The ISH Loader attempts to load firmware by sending a series of commands
 * to the ISH device. If a command fails to be acknowledged by the ISH device,
 * the loader will retry sending the command, up to a maximum of
 * ISHTP_LOADER_RETRY_TIMES.
 *
 * After the maximum number of retries has been reached without success, the
 * ISH bootloader will return an error status code and will no longer respond
 * to the driver's commands. This behavior indicates that the ISH Loader has
 * encountered a critical error during the firmware loading process.
 *
 * In such a case, where the ISH bootloader is unresponsive after all retries
 * have been exhausted, a platform reset is required to restore communication
 * with the ISH device and to recover from this error state.
 */
void ishtp_loader_work(struct work_struct *work)
{
	DEFINE_RAW_FLEX(struct loader_xfer_dma_fragment, fragment, fragment_tbl, FRAGMENT_MAX_NUM);
	struct ishtp_device *dev = container_of(work, struct ishtp_device, work_fw_loader);
	struct loader_xfer_query query = {
		.header.command = LOADER_CMD_XFER_QUERY,
	};
	struct loader_start start = {
		.header.command = LOADER_CMD_START,
	};
	union loader_recv_message recv_msg;
	char *filename = dev->driver_data->fw_filename;
	const struct firmware *ish_fw;
	void *dma_bufs[FRAGMENT_MAX_NUM] = {};
	u32 fragment_size;
	int retry = ISHTP_LOADER_RETRY_TIMES;
	int rv;

	rv = request_firmware(&ish_fw, filename, dev->devc);
	if (rv < 0) {
		dev_err(dev->devc, "request firmware %s failed:%d\n", filename, rv);
		return;
	}

	fragment->fragment.header.command = LOADER_CMD_XFER_FRAGMENT;
	fragment->fragment.xfer_mode = LOADER_XFER_MODE_DMA;
	fragment->fragment.is_last = 1;
	fragment->fragment.size = ish_fw->size;
	/* Calculate the size of a single DMA fragment */
	fragment_size = PFN_ALIGN(DIV_ROUND_UP(ish_fw->size, FRAGMENT_MAX_NUM));
	/* Calculate the count of DMA fragments */
	fragment->fragment_cnt = DIV_ROUND_UP(ish_fw->size, fragment_size);

	rv = prepare_dma_bufs(dev, ish_fw, fragment, dma_bufs, fragment_size);
	if (rv) {
		dev_err(dev->devc, "prepare DMA buffer failed.\n");
		goto out;
	}

	do {
		query.image_size = ish_fw->size;
		rv = loader_xfer_cmd(dev, &query, sizeof(query), recv_msg.raw_data,
				     sizeof(struct loader_xfer_query_ack));
		if (rv)
			continue; /* try again if failed */

		dev_dbg(dev->devc, "ISH Version %u.%u.%u.%u\n",
			recv_msg.query_ack.version_major,
			recv_msg.query_ack.version_minor,
			recv_msg.query_ack.version_hotfix,
			recv_msg.query_ack.version_build);

		rv = loader_xfer_cmd(dev, fragment,
				     struct_size(fragment, fragment_tbl, fragment->fragment_cnt),
				     recv_msg.raw_data, sizeof(struct loader_xfer_fragment_ack));
		if (rv)
			continue; /* try again if failed */

		rv = loader_xfer_cmd(dev, &start, sizeof(start), recv_msg.raw_data,
				     sizeof(struct loader_start_ack));
		if (rv)
			continue; /* try again if failed */

		dev_info(dev->devc, "firmware loaded. size:%zu\n", ish_fw->size);
		break;
	} while (--retry);

out:
	release_dma_bufs(dev, fragment, dma_bufs, fragment_size);
	release_firmware(ish_fw);
}
