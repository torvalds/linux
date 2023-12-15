// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/uaccess.h>
#include "q2spi-msm.h"
#include "q2spi-slave-reg.h"

#define CREATE_TRACE_POINTS
#include "q2spi-trace.h"

static int q2spi_slave_init(struct q2spi_geni *q2spi);
static int q2spi_gsi_submit(struct q2spi_packet *q2spi_pkt);

/* FTRACE Logging */
void q2spi_trace_log(struct device *dev, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};

	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	trace_q2spi_log_info(dev_name(dev), &vaf);
	va_end(args);
}

/**
 * q2spi_kzalloc - allocate kernel memory
 * @q2spi: Pointer to main q2spi_geni structure
 * @size: Size of the memory to allocate
 * @line: line number from where allocation is invoked
 *
 * Return: Pointer to allocated memory on success, NULL on failure.
 */
void *q2spi_kzalloc(struct q2spi_geni *q2spi, int size, int line)
{
	void *ptr = kzalloc(size, GFP_ATOMIC);

	if (ptr) {
		atomic_inc(&q2spi->alloc_count);
		Q2SPI_DEBUG(q2spi, "Allocated 0x%p at %d, count:%d\n",
			    ptr, line, atomic_read(&q2spi->alloc_count));
	}
	return ptr;
}

/**
 * q2spi_kfree - free kernel memory allocated by q2spi_kzalloc()
 * @q2spi: Pointer to main q2spi_geni structure
 * @ptr: address to be freed
 * @line: line number from where free is invoked
 *
 * Return: None
 */
void q2spi_kfree(struct q2spi_geni *q2spi, void *ptr, int line)
{
	if (ptr) {
		atomic_dec(&q2spi->alloc_count);
		kfree(ptr);
	}
	Q2SPI_DEBUG(q2spi, "Freeing 0x%p from %d, count:%d\n",
		    ptr, line, atomic_read(&q2spi->alloc_count));
	ptr = NULL;
}

void __q2spi_dump_ipc(struct q2spi_geni *q2spi, char *prefix,
		      char *str, int total, int offset, int size)
{
	char buf[DATA_BYTES_PER_LINE * 5];
	char data[DATA_BYTES_PER_LINE * 5];
	int len = min(size, DATA_BYTES_PER_LINE);

	hex_dump_to_buffer(str, len, DATA_BYTES_PER_LINE, 1, buf, sizeof(buf), false);
	scnprintf(data, sizeof(data), "%s[%d-%d of %d]: %s", prefix, offset + 1,
		  offset + len, total, buf);
	Q2SPI_DEBUG(q2spi, "%s: %s\n", __func__, data);
}

/**
 * q2spi_dump_ipc - Log dump function for debugging
 * @q2spi: Pointer to main q2spi_geni structure
 * @ipc_ctx: IPC context pointer to dump logs in IPC
 * @Prefix: Prefix to use in log
 * @str: String to dump in log
 * @Size: Size of data bytes per line
 * free bulk dma mapped buffers allocated by q2spi_pre_alloc_buffers api.
 *
 */
void q2spi_dump_ipc(struct q2spi_geni *q2spi, void *ipc_ctx, char *prefix,
		    char *str, int size)
{
	int offset = 0, total_bytes = size;

	if (!str) {
		Q2SPI_ERROR(q2spi, "%s: Err str is NULL\n", __func__);
		return;
	}

	if (q2spi->max_data_dump_size > 0 && size > q2spi->max_data_dump_size)
		size = q2spi->max_data_dump_size;

	while (size > Q2SPI_DATA_DUMP_SIZE) {
		__q2spi_dump_ipc(q2spi, prefix, (char *)str + offset, total_bytes,
				 offset, Q2SPI_DATA_DUMP_SIZE);
		offset += Q2SPI_DATA_DUMP_SIZE;
		size -= Q2SPI_DATA_DUMP_SIZE;
	}
	__q2spi_dump_ipc(q2spi, prefix, (char *)str + offset, total_bytes, offset, size);
}

/*
 * max_dump_size_show() - Prints the value stored in max_dump_size sysfs entry
 *
 * @dev: pointer to device
 * @attr: device attributes
 * @buf: buffer to store the max_dump_size value
 *
 * Return: prints max_dump_size value
 */
static ssize_t max_dump_size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct q2spi_geni *q2spi = platform_get_drvdata(pdev);

	return scnprintf(buf, sizeof(int), "%d\n", q2spi->max_data_dump_size);
}

/*
 * max_dump_size_store() - store the max_dump_size sysfs value
 *
 * @uport: pointer to device
 * @attr: device attributes
 * @buf: buffer which contains the max_dump_size in string format
 * @size: returns the value of size
 *
 * Return: Size copied in the buffer
 */
static ssize_t max_dump_size_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct q2spi_geni *q2spi = platform_get_drvdata(pdev);

	if (kstrtoint(buf, 0, &q2spi->max_data_dump_size)) {
		dev_err(dev, "%s Invalid input\n", __func__);
		return -EINVAL;
	}

	if (q2spi->max_data_dump_size <= 0)
		q2spi->max_data_dump_size = Q2SPI_DATA_DUMP_SIZE;
	return size;
}

static DEVICE_ATTR_RW(max_dump_size);

/**
 * q2spi_free_bulk_buf - free bulk buffers from pool
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * free bulk dma mapped buffers allocated by q2spi_pre_alloc_buffers api.
 *
 * Return: 0 for success, negative number if buffer is not found
 */
static int q2spi_free_bulk_buf(struct q2spi_geni *q2spi)
{
	void *buf;
	dma_addr_t dma_addr;
	int i;
	size_t size;

	for (i = 0; i < Q2SPI_MAX_BUF; i++) {
		if (!q2spi->bulk_buf[i])
			continue;
		if (q2spi->bulk_buf_used[i])
			return -1;
		buf = q2spi->bulk_buf[i];
		dma_addr = q2spi->bulk_dma_buf[i];
		size = sizeof(struct q2spi_client_bulk_access_pkt);
		geni_se_common_iommu_free_buf(q2spi->wrapper_dev, &dma_addr, buf, size);
	}
	return 0;
}

/**
 * q2spi_free_cr_buf - free cr buffers from pool
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * free cr dma mapped buffers allocated by q2spi_pre_alloc_buffers api.
 *
 * Return: 0 for success, negative number if buffer is not found
 */
static int q2spi_free_cr_buf(struct q2spi_geni *q2spi)
{
	void *buf;
	dma_addr_t dma_addr;
	int i;
	size_t size;

	for (i = 0; i < Q2SPI_MAX_BUF; i++) {
		if (!q2spi->cr_buf[i])
			continue;
		if (q2spi->cr_buf_used[i])
			return -1;
		buf = q2spi->cr_buf[i];
		dma_addr = q2spi->cr_dma_buf[i];
		size = sizeof(struct q2spi_client_dma_pkt);
		geni_se_common_iommu_free_buf(q2spi->wrapper_dev, &dma_addr, buf, size);
	}
	return 0;
}

/**
 * q2spi_free_var5_buf - free var5 buffers from pool
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * free var5 dma mapped buffers allocated by q2spi_pre_alloc_buffers api.
 *
 * Return: 0 for success, negative number if buffer is not found
 */
static int q2spi_free_var5_buf(struct q2spi_geni *q2spi)
{
	void *buf;
	dma_addr_t dma_addr;
	int i;
	size_t size;

	for (i = 0; i < Q2SPI_MAX_BUF; i++) {
		if (!q2spi->var5_buf[i])
			continue;
		if (q2spi->var5_buf_used[i])
			return -1;
		buf = q2spi->var5_buf[i];
		dma_addr = q2spi->var5_dma_buf[i];
		size = sizeof(struct q2spi_host_variant4_5_pkt);
		geni_se_common_iommu_free_buf(q2spi->wrapper_dev, &dma_addr, buf, size);
	}
	return 0;
}

/**
 * q2spi_free_var1_buf - free var1 buffers from pool
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * free var1 dma mapped buffers allocated by q2spi_pre_alloc_buffers api.
 *
 * Return: 0 for success, negative number if buffer is not found
 */
static int q2spi_free_var1_buf(struct q2spi_geni *q2spi)
{
	void *buf;
	dma_addr_t dma_addr;
	int i;
	size_t size;

	for (i = 0; i < Q2SPI_MAX_BUF; i++) {
		if (!q2spi->var1_buf[i])
			continue;
		if (q2spi->var1_buf_used[i])
			return -1;
		buf = q2spi->var1_buf[i];
		dma_addr = q2spi->var1_dma_buf[i];
		size = sizeof(struct q2spi_host_variant1_pkt);
		geni_se_common_iommu_free_buf(q2spi->wrapper_dev, &dma_addr, buf, size);
	}
	return 0;
}

/**
 * q2spi_free_dma_buf - free dma mapped buffers
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * free dma mapped buffers allocated by q2spi_pre_alloc_buffers api.
 *
 * Return: None
 */
static void q2spi_free_dma_buf(struct q2spi_geni *q2spi)
{
	if (q2spi_free_bulk_buf(q2spi))
		Q2SPI_ERROR(q2spi, "%s Err free bulk buf fail\n", __func__);

	if (q2spi_free_cr_buf(q2spi))
		Q2SPI_ERROR(q2spi, "%s Err free cr buf fail\n", __func__);

	if (q2spi_free_var5_buf(q2spi))
		Q2SPI_ERROR(q2spi, "%s Err free var5 buf fail\n", __func__);

	if (q2spi_free_var1_buf(q2spi))
		Q2SPI_ERROR(q2spi, "%s Err free var1 buf fail\n", __func__);
}

/**
 * q2spi_pre_alloc_buffers - Allocate iommu mapped buffres
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * This function allocates Q2SPI_MAX_BUF buffers of Variant_1 type
 * packets and Q2SPI_MAX_BUF bufferes of Variant_5 type packets and
 * Q2SPI_MAX_BUF bufferes of CR type 3.
 * This function will allocate and map into QUPV3 core context bank.
 *
 * Return: 0 for success, negative number for error condition.
 */
static int q2spi_pre_alloc_buffers(struct q2spi_geni *q2spi)
{
	int i;

	for (i = 0; i < Q2SPI_MAX_BUF; i++) {
		q2spi->var1_buf[i] =
			geni_se_common_iommu_alloc_buf(q2spi->wrapper_dev, &q2spi->var1_dma_buf[i],
						       sizeof(struct q2spi_host_variant1_pkt));
		if (IS_ERR_OR_NULL(q2spi->var1_buf[i])) {
			Q2SPI_ERROR(q2spi, "%s Err var1 buf alloc fail\n", __func__);
			goto exit_dealloc;
		}

		q2spi->var5_buf[i] =
			geni_se_common_iommu_alloc_buf(q2spi->wrapper_dev,
						       &q2spi->var5_dma_buf[i], (SMA_BUF_SIZE +
						       sizeof(struct q2spi_host_variant4_5_pkt)));
		if (IS_ERR_OR_NULL(q2spi->var5_buf[i])) {
			Q2SPI_ERROR(q2spi, "%s Err var5 buf alloc fail\n", __func__);
			goto exit_dealloc;
		}

		q2spi->cr_buf[i] =
			geni_se_common_iommu_alloc_buf(q2spi->wrapper_dev, &q2spi->cr_dma_buf[i],
						       RX_DMA_CR_BUF_SIZE);
		if (IS_ERR_OR_NULL(q2spi->cr_buf[i])) {
			Q2SPI_ERROR(q2spi, "%s Err cr buf alloc fail\n", __func__);
			goto exit_dealloc;
		}
		memset(q2spi->cr_buf[i], 0xFF, RX_DMA_CR_BUF_SIZE);

		q2spi->bulk_buf[i] =
			geni_se_common_iommu_alloc_buf(q2spi->wrapper_dev, &q2spi->bulk_dma_buf[i],
						       sizeof(struct
						       q2spi_client_bulk_access_pkt));
		if (IS_ERR_OR_NULL(q2spi->bulk_buf[i])) {
			Q2SPI_ERROR(q2spi, "%s Err bulk buf alloc fail\n", __func__);
			goto exit_dealloc;
		}
		Q2SPI_DEBUG(q2spi, "%s var1_buf[%d] virt:%p phy:%p\n",
			    __func__, i, (void *)q2spi->var1_buf[i], q2spi->var1_dma_buf[i]);
		Q2SPI_DEBUG(q2spi, "%s var5_buf[%d] virt:%p phy:%p\n",
			    __func__, i, (void *)q2spi->var5_buf[i], q2spi->var5_dma_buf[i]);
		Q2SPI_DEBUG(q2spi, "%s cr_buf[%d] virt:%p phy:%p\n",
			    __func__, i, (void *)q2spi->cr_buf[i], q2spi->cr_dma_buf[i]);
		Q2SPI_DEBUG(q2spi, "%s bulk_buf[%d] virt:%p phy:%p\n",
			    __func__, i, (void *)q2spi->bulk_buf[i], q2spi->bulk_dma_buf[i]);
	}
	return 0;
exit_dealloc:
	q2spi_free_dma_buf(q2spi);
	return -ENOMEM;
}

/**
 * q2spi_unmap_dma_buf_used - Unmap dma buffer used
 * @q2spi: Pointer to main q2spi_geni structure
 * @tx_dma: TX dma pointer
 * @rx_dma: RX dma pointer
 *
 * This function marks buffer used to free so that we are reuse the buffers.
 *
 */
static void
q2spi_unmap_dma_buf_used(struct q2spi_geni *q2spi, dma_addr_t tx_dma, dma_addr_t rx_dma)
{
	int i = 0;

	if (!tx_dma && !rx_dma) {
		Q2SPI_ERROR(q2spi, "%s Err TX/RX dma buffer NULL\n", __func__);
		return;
	}

	Q2SPI_DEBUG(q2spi, "%s PID:%d for tx_dma:%p rx_dma:%p\n",
		    __func__, current->pid, tx_dma, rx_dma);

	for (i = 0; i < Q2SPI_MAX_BUF; i++) {
		Q2SPI_DEBUG(q2spi, "%s var1_dma_buf[%d]=%p var5_dma_buf[%d]=%p\n",
			    __func__, i, q2spi->var1_dma_buf[i], i, q2spi->var5_dma_buf[i]);
		Q2SPI_DEBUG(q2spi, "%s cr_dma_buf[%d]=%p bulk_dma_buf[%d]=%p\n",
			    __func__, i, q2spi->cr_dma_buf[i], i, q2spi->bulk_dma_buf[i]);
		if (tx_dma == q2spi->var1_dma_buf[i]) {
			if (q2spi->var1_buf_used[i]) {
				Q2SPI_DEBUG(q2spi, "%s UNMAP var1_buf[%d] virt:%p phy:%p\n",
					    __func__, i, q2spi->var1_buf[i],
					    q2spi->var1_dma_buf[i]);
				q2spi->var1_buf_used[i] = NULL;
			}
		} else if (tx_dma == q2spi->var5_dma_buf[i]) {
			if (q2spi->var5_buf_used[i]) {
				Q2SPI_DEBUG(q2spi, "%s UNMAP var5_buf[%d] virt:%p phy:%p\n",
					    __func__, i, q2spi->var5_buf[i],
					    q2spi->var5_dma_buf[i]);
				q2spi->var5_buf_used[i] = NULL;
			}
		}
		if (rx_dma == q2spi->cr_dma_buf[i]) {
			if (q2spi->cr_buf_used[i]) {
				Q2SPI_DEBUG(q2spi, "%s UNMAP cr_buf[%d] virt:%p phy:%p\n",
					    __func__, i, q2spi->cr_buf[i], q2spi->cr_dma_buf[i]);
				q2spi->cr_buf_used[i] = NULL;
			}
		} else if (rx_dma == q2spi->bulk_dma_buf[i]) {
			if (q2spi->bulk_buf_used[i]) {
				Q2SPI_DEBUG(q2spi, "%s UNMAP bulk_buf[%d] virt:%p phy:%p\n",
					    __func__, i, q2spi->bulk_buf[i],
					    q2spi->bulk_dma_buf[i]);
				q2spi->bulk_buf_used[i] = NULL;
			}
		}
	}
	Q2SPI_DEBUG(q2spi, "%s End PID=%d\n", __func__, current->pid);
}

/**
 * q2spi_get_doorbell_rx_buf - allocate RX DMA buffer to GSI
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * This function will get one RX buffer from pool of buffers
 * allocated using q2spi_pre_alloc_buffers() and prepare RX DMA
 * descriptor and map to GSI.
 * This RX buffer is used to receive doorbell from GSI.
 *
 * Return: 0 for success, negative number for error condition.
 */
static int q2spi_get_doorbell_rx_buf(struct q2spi_geni *q2spi)
{
	struct q2spi_dma_transfer *xfer = q2spi->db_xfer;
	int i, ret = 0;

	/* Pick rx buffers from pre allocated pool */
	for (i = 0; i < Q2SPI_MAX_BUF; i++) {
		if (!q2spi->cr_buf_used[i])
			break;
	}
	if (i < Q2SPI_MAX_BUF) {
		Q2SPI_DEBUG(q2spi, "%s q2spi:%p q2spi_xfer:%p\n", __func__, q2spi, q2spi->xfer);
		xfer->rx_buf = q2spi->cr_buf[i];
		xfer->rx_dma = q2spi->cr_dma_buf[i];
		q2spi->cr_buf_used[i] = q2spi->cr_buf[i];
		q2spi->rx_buf = xfer->rx_buf;
		Q2SPI_DEBUG(q2spi, "ALLOC %s rx_buf:%p rx_dma:%p\n",
			    __func__, xfer->rx_buf, xfer->rx_dma);
		memset(xfer->rx_buf, 0xFF, RX_DMA_CR_BUF_SIZE);
	}
	if (!xfer->rx_buf || !xfer->rx_dma) {
		Q2SPI_ERROR(q2spi, "%s Err RX dma alloc failed\n", __func__);
		ret = -ENOMEM;
	}
	return ret;
}

/**
 * q2spi_alloc_rx_buf - allocate RX DMA buffers
 * @q2spi: Pointer to main q2spi_geni structure
 * @len: size of the memory to be allocate
 *
 * This function will allocate RX dma_alloc_coherant memory
 * of the length specified. This RX buffer is used to
 * receive rx data from slave.
 *
 * Return: 0 for success, negative number for error condition.
 */
static int q2spi_alloc_rx_buf(struct q2spi_geni *q2spi, int len)
{
	struct q2spi_dma_transfer *xfer = q2spi->xfer;
	int ret = 0;

	Q2SPI_DEBUG(q2spi, "%s len:%d\n", __func__, len);
	if (!len) {
		Q2SPI_ERROR(q2spi, "%s Err Zero length for alloc\n", __func__);
		ret = -EINVAL;
		goto fail;
	}

	xfer->rx_buf = geni_se_common_iommu_alloc_buf(q2spi->wrapper_dev, &xfer->rx_dma, len);
	if (IS_ERR_OR_NULL(xfer->rx_buf)) {
		Q2SPI_ERROR(q2spi, "%s Err iommu alloc buf failed\n", __func__);
		ret = -ENOMEM;
		goto fail;
	}
	Q2SPI_DEBUG(q2spi, "%s rx_buf=%p rx_dma=%p\n", __func__, xfer->rx_buf, xfer->rx_dma);
	memset(xfer->rx_buf, 0xFF, len);
fail:
	return ret;
}

/**
 * q2spi_hrf_entry_format - prepare HRF entry for HRF flow
 * @q2spi: Pointer to main q2spi_geni structure
 * @q2spi_req: structure for q2spi_request
 * @q2spi_hrf_req: pointer to q2spi hrf type of q2spi_request
 *
 * This function hrf entry as per the format defined in spec.
 *
 * Return: 0 for success, negative number for error condition.
 */
static int q2spi_hrf_entry_format(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req,
				  struct q2spi_request **q2spi_hrf_req_ptr)
{
	struct q2spi_request *q2spi_hrf_req = NULL;
	struct q2spi_mc_hrf_entry hrf_entry;
	int flow_id;

	q2spi_hrf_req = q2spi_kzalloc(q2spi, sizeof(struct q2spi_request), __LINE__);
	if (!q2spi_hrf_req) {
		Q2SPI_ERROR(q2spi, "%s Err alloc hrf req failed\n", __func__);
		return -ENOMEM;
	}
	q2spi_hrf_req->data_buff =
		q2spi_kzalloc(q2spi, sizeof(struct q2spi_mc_hrf_entry), __LINE__);

	if (!q2spi_hrf_req->data_buff) {
		Q2SPI_ERROR(q2spi, "%s Err alloc hrf data_buff failed\n", __func__);
		return -ENOMEM;
	}
	*q2spi_hrf_req_ptr = q2spi_hrf_req;
	if (q2spi_req.cmd == HRF_WRITE) {
		hrf_entry.cmd = 3;
		hrf_entry.parity = 1;
	} else if (q2spi_req.cmd == HRF_READ) {
		hrf_entry.cmd = 4;
		hrf_entry.parity = 0;
	}
	hrf_entry.flow = HRF_ENTRY_FLOW;
	hrf_entry.type = HRF_ENTRY_TYPE;
	flow_id = q2spi_alloc_xfer_tid(q2spi);
	if (flow_id < 0) {
		Q2SPI_ERROR(q2spi, "%s Err failed to alloc flow_id", __func__);
		return -EINVAL;
	}
	hrf_entry.flow_id = flow_id;
	Q2SPI_DEBUG(q2spi, "%s flow_id:%d len:%d", __func__, hrf_entry.flow_id, q2spi_req.data_len);
	if (q2spi_req.data_len % 4) {
		hrf_entry.dwlen_part1 = (q2spi_req.data_len / 4) & 0xF;
		hrf_entry.dwlen_part2 = ((q2spi_req.data_len / 4) >> 4) & 0xFF;
		hrf_entry.dwlen_part3 = ((q2spi_req.data_len / 4) >> 12) & 0xFF;
	} else {
		hrf_entry.dwlen_part1 = (q2spi_req.data_len / 4 - 1) & 0xF;
		hrf_entry.dwlen_part2 = ((q2spi_req.data_len / 4 - 1) >> 4) & 0xFF;
		hrf_entry.dwlen_part3 = ((q2spi_req.data_len / 4 - 1) >> 12) & 0xFF;
	}
	Q2SPI_DEBUG(q2spi, "%s hrf_entry dwlen part1:%d part2:%d part3:%d\n",
		    __func__, hrf_entry.dwlen_part1, hrf_entry.dwlen_part2, hrf_entry.dwlen_part3);
	hrf_entry.arg2 = q2spi_req.end_point;
	hrf_entry.arg3 = q2spi_req.proto_ind;
	q2spi_hrf_req->addr = q2spi_req.addr;
	q2spi_hrf_req->data_len = HRF_ENTRY_DATA_LEN;
	q2spi_hrf_req->cmd = HRF_WRITE;
	q2spi_hrf_req->flow_id = hrf_entry.flow_id;
	q2spi_hrf_req->end_point = q2spi_req.end_point;
	q2spi_hrf_req->proto_ind = q2spi_req.proto_ind;
	memcpy(q2spi_hrf_req->data_buff, &hrf_entry, sizeof(struct q2spi_mc_hrf_entry));
	Q2SPI_DEBUG(q2spi, "%s End q2spi_req:%d q2spi_hrf_req:%p *q2spi_hrf_req:%d\n",
		    __func__, q2spi_req, q2spi_hrf_req, *q2spi_hrf_req);
	return 0;
}

/**
 * q2spi_map_doorbell_rx_buf - map rx dma buffer to receive doorbell
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * This function get one rx buffer using q2spi_get_doorbell_rx_buf and map to
 * gsi so that SW can receive doorbell
 *
 * Return: 0 for success, negative number for error condition.
 */
int q2spi_map_doorbell_rx_buf(struct q2spi_geni *q2spi)
{
	struct q2spi_packet *q2spi_pkt;
	int ret = 0;

	Q2SPI_DEBUG(q2spi, "%s Enter PID=%d\n", __func__, current->pid);
	if (q2spi->db_xfer->rx_dma) {
		Q2SPI_DEBUG(q2spi, "%s Doorbell buffer already mapped\n", __func__);
		return 0;
	}
	q2spi_pkt = q2spi_kzalloc(q2spi, sizeof(struct q2spi_packet), __LINE__);
	if (!q2spi_pkt) {
		Q2SPI_ERROR(q2spi, "%s Err q2spi_pkt alloc fail\n", __func__);
		return -ENOMEM;
	}

	q2spi_pkt->m_cmd_param = Q2SPI_RX_ONLY;
	memset(q2spi->db_xfer, 0, sizeof(struct q2spi_dma_transfer));
	/* RX DMA buffer allocated to map to GSI to Receive Doorbell */
	/* Alloc RX DMA buf and map to gsi so that SW can receive Doorbell */
	ret = q2spi_get_doorbell_rx_buf(q2spi);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err failed to alloc RX DMA buf", __func__);
		return ret;
	}
	/* Map RX DMA descriptor on RX channel */
	q2spi->db_xfer->cmd = Q2SPI_RX_ONLY;
	q2spi->db_xfer->rx_data_len = RX_DMA_CR_BUF_SIZE; /* 96 byte for 4 crs in doorbell */
	q2spi->db_xfer->rx_len = RX_DMA_CR_BUF_SIZE;
	q2spi->db_xfer->q2spi_pkt = q2spi_pkt;
	q2spi_pkt->q2spi = q2spi;
	mutex_lock(&q2spi->gsi_lock);
	ret = q2spi_setup_gsi_xfer(q2spi_pkt);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err q2spi_setup_gsi_xfer failed: %d\n", __func__, ret);
		mutex_unlock(&q2spi->gsi_lock);
		return ret;
	}
	mutex_unlock(&q2spi->gsi_lock);
	q2spi->doorbell_setup = true;
	/* Todo unmap_buff and tid */
	Q2SPI_DEBUG(q2spi, "%s End PID=%d\n", __func__, current->pid);
	return ret;
}

/**
 * q2spi_alloc_host_variant - allocate memory for host variant
 * @q2spi: Pointer to main q2spi_geni structure
 * @len: size of the memory to be allocate
 *
 * This function will allocate dma_alloc_coherant memory
 * of the length specified.
 *
 * Return: address of the buffer on success, NULL or ERR_PTR on
 * failure/error.
 */
void *q2spi_alloc_host_variant(struct q2spi_geni *q2spi, int len)
{
	void *ptr = NULL;

	ptr = geni_se_common_iommu_alloc_buf(q2spi->wrapper_dev, &q2spi->dma_buf, len);

	return ptr;
}

/**
 * q2spi_doorbell - q2spi doorbell to handle CR events from q2spi slave
 * @q2spi_cr_hdr_event: Pointer to q2spi_cr_hdr_event
 *
 * If Doorbell interrupt to Host is enabled, then Host will get doorbell interrupt upon
 * any error or new CR events from q2spi slave. This function used to parse CR header event
 * part of doorbell and prepare CR packet and add to CR queue list. Also map new RX
 * dma buffer to receive next doorbell.
 *
 * Return: 0 for success, negative number for error condition.
 */
void q2spi_doorbell(struct q2spi_geni *q2spi,
		    const struct qup_q2spi_cr_header_event *q2spi_cr_hdr_event)
{
	Q2SPI_DEBUG(q2spi, "%s Enter PID=%d\n", __func__, current->pid);
	memcpy(&q2spi->q2spi_cr_hdr_event, q2spi_cr_hdr_event,
	       sizeof(struct qup_q2spi_cr_header_event));
	queue_work(q2spi->doorbell_wq, &q2spi->q2spi_doorbell_work);
	Q2SPI_DEBUG(q2spi, "%s End work queued PID=%d\n", __func__, current->pid);
}

/**
 * q2spi_prepare_cr_pkt - Prepares CR packet as part of doorbell processing
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Return: 0 for success, negative number on failure
 */
static int q2spi_prepare_cr_pkt(struct q2spi_geni *q2spi)
{
	struct q2spi_cr_packet *q2spi_cr_pkt = NULL;
	const struct qup_q2spi_cr_header_event *q2spi_cr_hdr_event = NULL;
	unsigned long flags;
	int ret = 0, i = 0;

	q2spi_cr_hdr_event = &q2spi->q2spi_cr_hdr_event;
	q2spi_cr_pkt = q2spi_kzalloc(q2spi, sizeof(struct q2spi_cr_packet), __LINE__);
	if (!q2spi_cr_pkt) {
		Q2SPI_ERROR(q2spi, "%s Err q2spi_cr_pkt alloc failed\n", __func__);
		return -ENOMEM;
	}
	q2spi->cr_pkt = q2spi_cr_pkt;
	spin_lock_irqsave(&q2spi->cr_queue_lock, flags);
	q2spi_cr_pkt->no_of_valid_crs = q2spi_cr_hdr_event->byte0_len;
	Q2SPI_DEBUG(q2spi, "%s q2spi_cr_pkt:%p hdr_0:0x%x no_of_crs=%d\n", __func__,
		    q2spi_cr_pkt, q2spi_cr_hdr_event->cr_hdr_0, q2spi_cr_pkt->no_of_valid_crs);

	if (q2spi_cr_hdr_event->byte0_err)
		Q2SPI_DEBUG(q2spi, "%s Error: q2spi_cr_hdr_event->byte0_err=%d\n",
			    __func__, q2spi_cr_hdr_event->byte0_err);

	for (i = 0; i < q2spi_cr_hdr_event->byte0_len; i++) {
		if (i == 0) {
			q2spi_cr_pkt->cr_hdr[i].cmd = (q2spi_cr_hdr_event->cr_hdr_0) & 0xF;
			q2spi_cr_pkt->cr_hdr[i].flow = (q2spi_cr_hdr_event->cr_hdr_0 >> 4) & 0x1;
			q2spi_cr_pkt->cr_hdr[i].type = (q2spi_cr_hdr_event->cr_hdr_0 >> 5) & 0x3;
			q2spi_cr_pkt->cr_hdr[i].parity = (q2spi_cr_hdr_event->cr_hdr_0 >> 7) & 0x1;
		} else if (i == 1) {
			q2spi_cr_pkt->cr_hdr[i].cmd = (q2spi_cr_hdr_event->cr_hdr_1) & 0xF;
			q2spi_cr_pkt->cr_hdr[i].flow = (q2spi_cr_hdr_event->cr_hdr_1 >> 4) & 0x1;
			q2spi_cr_pkt->cr_hdr[i].type = (q2spi_cr_hdr_event->cr_hdr_1 >> 5) & 0x3;
			q2spi_cr_pkt->cr_hdr[i].parity = (q2spi_cr_hdr_event->cr_hdr_1 >> 7) & 0x1;
		} else if (i == 2) {
			q2spi_cr_pkt->cr_hdr[i].cmd = (q2spi_cr_hdr_event->cr_hdr_2) & 0xF;
			q2spi_cr_pkt->cr_hdr[i].flow = (q2spi_cr_hdr_event->cr_hdr_2 >> 4) & 0x1;
			q2spi_cr_pkt->cr_hdr[i].type = (q2spi_cr_hdr_event->cr_hdr_2 >> 5) & 0x3;
			q2spi_cr_pkt->cr_hdr[i].parity = (q2spi_cr_hdr_event->cr_hdr_2 >> 7) & 0x1;
		} else if (i == 3) {
			q2spi_cr_pkt->cr_hdr[i].cmd = (q2spi_cr_hdr_event->cr_hdr_3) & 0xF;
			q2spi_cr_pkt->cr_hdr[i].flow = (q2spi_cr_hdr_event->cr_hdr_3 >> 4) & 0x1;
			q2spi_cr_pkt->cr_hdr[i].type = (q2spi_cr_hdr_event->cr_hdr_3 >> 5) & 0x3;
			q2spi_cr_pkt->cr_hdr[i].parity = (q2spi_cr_hdr_event->cr_hdr_3 >> 7) & 0x1;
		}
		Q2SPI_DEBUG(q2spi, "%s CR HDR[%d] cmd/opcode:%d mc_flow:%d type:%d parity:%d\n",
			    __func__, i, q2spi_cr_pkt->cr_hdr[i].cmd,
			    q2spi_cr_pkt->cr_hdr[i].flow, q2spi_cr_pkt->cr_hdr[i].type,
			    q2spi_cr_pkt->cr_hdr[i].parity);
	}
	Q2SPI_DEBUG(q2spi, "%s q2spi->xfer:%p\n", __func__, q2spi->xfer);
	q2spi_cr_pkt->xfer = q2spi->xfer;
	spin_unlock_irqrestore(&q2spi->cr_queue_lock, flags);
	return ret;
}

static int q2spi_open(struct inode *inode, struct file *filp)
{
	struct cdev *cdev;
	struct q2spi_chrdev *q2spi_cdev;
	struct q2spi_geni *q2spi;
	int ret = 0, rc = 0;

	rc = iminor(inode);
	cdev = inode->i_cdev;
	q2spi_cdev = container_of(cdev, struct q2spi_chrdev, cdev[rc]);
	if (!q2spi_cdev) {
		pr_err("%s Err q2spi_cdev NULL\n", __func__);
		return -EINVAL;
	}

	q2spi = container_of(q2spi_cdev, struct q2spi_geni, chrdev);
	if (!q2spi) {
		pr_err("%s Err q2spi NULL\n", __func__);
		return -EINVAL;
	}
	q2spi->port_release = false;
	Q2SPI_DEBUG(q2spi, "%s PID:%d, allocs=%d\n",
		    __func__, current->pid, atomic_read(&q2spi->alloc_count));
	if (q2spi->hw_state_is_bad) {
		Q2SPI_ERROR(q2spi, "%s Err Retries failed, check HW state\n", __func__);
		return -EPIPE;
	}

	if (q2spi_geni_resources_on(q2spi))
		return -EIO;

	/* Q2SPI slave HPG 2.1 Initialization */
	ret = q2spi_slave_init(q2spi);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err Failed to init q2spi slave %d\n",
			    __func__, ret);
		return ret;
	}
	if (!q2spi->doorbell_setup) {
		ret = q2spi_map_doorbell_rx_buf(q2spi);
		if (ret) {
			Q2SPI_ERROR(q2spi, "%s Err failed to alloc RX DMA buf\n", __func__);
			return ret;
		}
	}
	filp->private_data = q2spi;
	Q2SPI_DEBUG(q2spi, "%s End PID:%d, allocs:%d\n",
		    __func__, current->pid, atomic_read(&q2spi->alloc_count));
	return 0;
}

/**
 * q2spi_get_variant_buf - Get one buffer allocated from pre allocated buffers
 * @q2spi: Pointer to main q2spi_geni structure
 * @q2spi_pkt: pointer to q2spi packet
 * @vtype: variant type in q2spi_pkt
 *
 * This function get one buffer allocated using q2spi_pre_alloc_buffers() based on variant type
 * specified in q2spi packet.
 *
 * Return: 0 for success, negative number for error condition.
 */
static inline void *q2spi_get_variant_buf(struct q2spi_geni *q2spi,
					  struct q2spi_packet *q2spi_pkt, enum var_type vtype)
{
	int i;

	if (vtype != VARIANT_1_LRA && vtype != VARIANT_5) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid variant:%d!\n", __func__, vtype);
		return NULL;
	}

	/* Pick buffers from pre allocated pool */
	if (vtype == VARIANT_1_LRA) {
		for (i = 0; i < Q2SPI_MAX_BUF; i++) {
			if (!q2spi->var1_buf_used[i])
				break;
		}
		if (i < Q2SPI_MAX_BUF) {
			q2spi->var1_buf_used[i] = q2spi->var1_buf[i];
			q2spi_pkt->var1_tx_dma = q2spi->var1_dma_buf[i];
			Q2SPI_DEBUG(q2spi, "%s ALLOC var1 i:%d vir1_buf:%p phy_dma_buf:%p\n",
				    __func__, i, (void *)q2spi->var1_buf[i],
				    q2spi->var1_dma_buf[i]);
			return (void *)q2spi->var1_buf[i];
		}
	} else if (vtype == VARIANT_5) {
		for (i = 0; i < Q2SPI_MAX_BUF; i++) {
			if (!q2spi->var5_buf_used[i])
				break;
		}
		if (i < Q2SPI_MAX_BUF) {
			q2spi->var5_buf_used[i] = q2spi->var5_buf[i];
			q2spi_pkt->var5_tx_dma = q2spi->var5_dma_buf[i];
			Q2SPI_DEBUG(q2spi, "%s ALLOC var5 i:%d vir5_buf:%p phy_dma_buf:%p\n",
				    __func__, i, (void *)q2spi->var5_buf[i],
				    q2spi->var5_dma_buf[i]);
			return (void *)q2spi->var5_buf[i];
		}
	}
	Q2SPI_ERROR(q2spi, "%s Err Short of buffers for variant:%d!\n", __func__, vtype);
	return NULL;
}

/**
 * q2spi_alloc_xfer_tid() - Allocate a tid to q2spi transfer request
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Return: zero on success with valid xfer->tid and error code on failures.
 */
int q2spi_alloc_xfer_tid(struct q2spi_geni *q2spi)
{
	unsigned long flags;
	int tid = 0;

	spin_lock_irqsave(&q2spi->txn_lock, flags);
	tid = idr_alloc_cyclic(&q2spi->tid_idr, q2spi, Q2SPI_START_TID_ID,
			       Q2SPI_END_TID_ID, GFP_ATOMIC);
	if (tid < Q2SPI_START_TID_ID || tid > Q2SPI_END_TID_ID) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid tid:%d\n", __func__, tid);
		spin_unlock_irqrestore(&q2spi->txn_lock, flags);
		return -EINVAL;
	}
	Q2SPI_DEBUG(q2spi, "%s tid:%d ret:%d\n", __func__, tid, tid);
	spin_unlock_irqrestore(&q2spi->txn_lock, flags);
	return tid;
}

/**
 * q2spi_free_xfer_tid() - Freee tid of xfer
 * @q2spi: Pointer to main q2spi_geni structure
 *
 */
void q2spi_free_xfer_tid(struct q2spi_geni *q2spi, int tid)
{
	unsigned long flags;

	spin_lock_irqsave(&q2spi->txn_lock, flags);
	Q2SPI_DEBUG(q2spi, "%s tid:%d\n", __func__, tid);
	if (tid < Q2SPI_START_TID_ID || tid > Q2SPI_END_TID_ID) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid tid:%d\n", __func__, tid);
		spin_unlock_irqrestore(&q2spi->txn_lock, flags);
	}
	idr_remove(&q2spi->tid_idr, tid);
	spin_unlock_irqrestore(&q2spi->txn_lock, flags);
}

static unsigned int
q2spi_get_dw_offset(struct q2spi_geni *q2spi, enum cmd_type c_type, unsigned int reg_offset)
{
	unsigned int offset = 0, remainder = 0, quotient = 0;

	offset = reg_offset / Q2SPI_OFFSET_MASK;
	Q2SPI_DEBUG(q2spi, "%s type:%d offset:%d remainder:%d quotient:%d\n",
		    __func__, c_type, offset, remainder, quotient);
	return offset;
}

int q2spi_frame_lra(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req,
		    struct q2spi_packet **q2spi_pkt_ptr, int vtype)
{
	struct q2spi_packet *q2spi_pkt;
	struct q2spi_host_variant1_pkt *q2spi_hc_var1;
	int ret;
	unsigned int dw_offset = 0;

	q2spi_pkt = q2spi_kzalloc(q2spi, sizeof(struct q2spi_packet), __LINE__);
	if (!q2spi_pkt) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid q2spi_pkt\n", __func__);
		return -ENOMEM;
	}
	memset(q2spi_pkt, 0, sizeof(struct q2spi_packet));
	*q2spi_pkt_ptr = q2spi_pkt;
	Q2SPI_DEBUG(q2spi, "%s q2spi_pkt->list:%p next:%p prev:%p\n", __func__, &q2spi_pkt->list,
		    &q2spi_pkt->list.next, &q2spi_pkt->list.prev);
	q2spi_hc_var1 = (struct q2spi_host_variant1_pkt *)
				q2spi_get_variant_buf(q2spi, q2spi_pkt, VARIANT_1_LRA);
	if (!q2spi_hc_var1) {
		Q2SPI_DEBUG(q2spi, "%s Err Invalid q2spi_hc_var1\n", __func__);
		return -ENOMEM;
	}
	Q2SPI_DEBUG(q2spi, "%s var_1:%p var_1_phy:%p q2spi_req:%p cmd:%d\n",
		    __func__, q2spi_hc_var1, q2spi_pkt->var1_tx_dma, q2spi_req, q2spi_req.cmd);
	if (q2spi_req.cmd == LOCAL_REG_READ || q2spi_req.cmd == HRF_READ) {
		q2spi_hc_var1->cmd = HC_DATA_READ;
		q2spi_pkt->m_cmd_param = Q2SPI_TX_RX;
		ret = q2spi_alloc_rx_buf(q2spi, q2spi_req.data_len);
		if (ret) {
			Q2SPI_ERROR(q2spi, "%s Err failed to alloc RX DMA buf", __func__);
			return -ENOMEM;
		}
	} else if (q2spi_req.cmd == LOCAL_REG_WRITE || q2spi_req.cmd == HRF_WRITE) {
		q2spi_hc_var1->cmd = HC_DATA_WRITE;
		q2spi_pkt->m_cmd_param = Q2SPI_TX_ONLY;
		q2spi_req.data_len = sizeof(q2spi_hc_var1->data_buf) <= q2spi_req.data_len ?
					sizeof(q2spi_hc_var1->data_buf) : q2spi_req.data_len;
		memcpy(q2spi_hc_var1->data_buf, q2spi_req.data_buff, q2spi_req.data_len);
		q2spi_kfree(q2spi, q2spi_req.data_buff, __LINE__);
	}
	q2spi_hc_var1->flow = MC_FLOW;
	q2spi_hc_var1->interrupt = CLIENT_INTERRUPT;
	q2spi_hc_var1->seg_last = SEGMENT_LST;
	if (q2spi_req.data_len % 4)
		q2spi_hc_var1->dw_len = (q2spi_req.data_len / 4);
	else
		q2spi_hc_var1->dw_len = (q2spi_req.data_len / 4) - 1;
	q2spi_hc_var1->access_type = LOCAL_REG_ACCESS;
	q2spi_hc_var1->address_mode = CLIENT_ADDRESS;
	Q2SPI_DEBUG(q2spi, "%s data_len:%d dw_len:%d req_flow_id:%d\n",
		    __func__, q2spi_req.data_len, q2spi_hc_var1->dw_len, q2spi_req.flow_id);
	if (!q2spi_req.flow_id && !q2spi->hrf_flow) {
		ret = q2spi_alloc_xfer_tid(q2spi);
		if (ret < 0) {
			Q2SPI_ERROR(q2spi, "%s Err failed to alloc xfer_tid\n", __func__);
			return -EINVAL;
		}
		q2spi_hc_var1->flow_id = ret;
	} else {
		q2spi_hc_var1->flow_id = q2spi_req.flow_id;
	}
	q2spi->xfer->tid = q2spi_hc_var1->flow_id;
	dw_offset = q2spi_get_dw_offset(q2spi, q2spi_req.cmd, q2spi_req.addr);
	q2spi_hc_var1->reg_offset = dw_offset;
	q2spi_pkt->var1_pkt = q2spi_hc_var1;
	q2spi_pkt->vtype = vtype;
	q2spi_pkt->valid = true;
	q2spi_pkt->sync = q2spi_req.sync;

	Q2SPI_DEBUG(q2spi, "%s *q2spi_pkt_ptr:%p End ret flow_id:%d\n",
		    __func__, *q2spi_pkt_ptr, q2spi_hc_var1->flow_id);
	return q2spi_hc_var1->flow_id;
}

int q2spi_sma_format(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req,
		     struct q2spi_packet *q2spi_pkt)
{
	struct q2spi_host_variant4_5_pkt *q2spi_hc_var5;
	int ret = 0, flow_id;

	if (!q2spi) {
		Q2SPI_ERROR(q2spi, "%s Err q2spi NULL\n", __func__);
		return -EINVAL;
	}
	if (!q2spi_pkt) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid q2spi_pkt\n", __func__);
		return -EINVAL;
	}
	Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p pkt_var_1:%p pkt_dma:%p pkt_var_5:%p\n",
		    __func__, q2spi_pkt, q2spi_pkt->var1_pkt, q2spi_pkt->var5_tx_dma,
		    q2spi_pkt->var5_pkt);
	Q2SPI_DEBUG(q2spi, "%s q2spi_req:%p req_cmd:%d req_addr:%d req_len:%d req_data_buf:%p\n",
		    __func__, q2spi_req, q2spi_req.cmd, q2spi_req.addr, q2spi_req.data_len,
		    q2spi_req.data_buff);

	q2spi_hc_var5 = (struct q2spi_host_variant4_5_pkt *)
			q2spi_get_variant_buf(q2spi, q2spi_pkt, VARIANT_5);
	if (!q2spi_hc_var5) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid q2spi_hc_var5\n", __func__);
		return -EINVAL;
	}
	Q2SPI_DEBUG(q2spi, "%s var_5:%p q2spi_req:%p cmd:%d\n",
		    __func__, q2spi_hc_var5, q2spi_req, q2spi_req.cmd);
	Q2SPI_DEBUG(q2spi, "%s pkt_var_1:%p pkt_dma:%p pkt_var_5:%p\n",
		    __func__, q2spi_pkt->var1_pkt, q2spi_pkt->var5_tx_dma, q2spi_pkt->var5_pkt);
	if (q2spi_req.data_len > Q2SPI_MAX_DATA_LEN) {
		Q2SPI_ERROR(q2spi, "%s Err (q2spi_req.data_len > Q2SPI_MAX_DATA_LEN) %d return\n",
			    __func__, q2spi_req.data_len);
		return -ENOMEM;
	}

	if (q2spi_req.cmd == DATA_READ || q2spi_req.cmd == HRF_READ) {
		q2spi_hc_var5->cmd = HC_SMA_READ;
		q2spi_pkt->m_cmd_param = Q2SPI_TX_RX;
		ret = q2spi_alloc_rx_buf(q2spi, q2spi_req.data_len);
		if (ret) {
			Q2SPI_ERROR(q2spi, "%s Err failed to alloc RX DMA buf\n", __func__);
			return -ENOMEM;
		}
	} else if (q2spi_req.cmd == DATA_WRITE || q2spi_req.cmd == HRF_WRITE) {
		q2spi_hc_var5->cmd = HC_SMA_WRITE;
		q2spi_pkt->m_cmd_param = Q2SPI_TX_ONLY;
		q2spi_req.data_len = sizeof(q2spi_hc_var5->data_buf) <= q2spi_req.data_len ?
					sizeof(q2spi_hc_var5->data_buf) : q2spi_req.data_len;
		memcpy(q2spi_hc_var5->data_buf, q2spi_req.data_buff, q2spi_req.data_len);
		q2spi_dump_ipc(q2spi, q2spi->ipc, "sma format q2spi_req data_buf",
			       (char *)q2spi_req.data_buff, q2spi_req.data_len);
		q2spi_dump_ipc(q2spi, q2spi->ipc, "sma format var5 data_buf",
			       (char *)q2spi_hc_var5->data_buf, q2spi_req.data_len);
		q2spi_kfree(q2spi, q2spi_req.data_buff, __LINE__);
	}
	if (q2spi_req.flow_id < Q2SPI_END_TID_ID)
		q2spi_hc_var5->flow = MC_FLOW;
	else
		q2spi_hc_var5->flow = CM_FLOW;
	q2spi_hc_var5->interrupt = CLIENT_INTERRUPT;
	q2spi_hc_var5->seg_last = SEGMENT_LST;
	q2spi_pkt->data_length = q2spi_req.data_len;
	if (q2spi_req.data_len % 4) {
		q2spi_hc_var5->dw_len_part1 = (q2spi_req.data_len / 4);
		q2spi_hc_var5->dw_len_part2 = (q2spi_req.data_len / 4) >> 2;
	} else {
		q2spi_hc_var5->dw_len_part1 = (q2spi_req.data_len / 4) - 1;
		q2spi_hc_var5->dw_len_part2 = ((q2spi_req.data_len / 4) - 1) >> 2;
	}
	Q2SPI_DEBUG(q2spi, "dw_len_part1:%d dw_len_part2:%d\n",
		    q2spi_hc_var5->dw_len_part1, q2spi_hc_var5->dw_len_part2);
	q2spi_hc_var5->access_type = SYSTEM_MEMORY_ACCESS;
	q2spi_hc_var5->address_mode = NO_CLIENT_ADDRESS;
	if (!q2spi_req.flow_id && !q2spi->hrf_flow) {
		flow_id = q2spi_alloc_xfer_tid(q2spi);
		if (flow_id < 0) {
			Q2SPI_ERROR(q2spi, "%s Err failed to alloc tid", __func__);
			return -EINVAL;
		}
		q2spi_hc_var5->flow_id = flow_id;
	} else {
		if (q2spi_req.flow_id < Q2SPI_END_TID_ID)
			q2spi_hc_var5->flow_id = q2spi_pkt->hrf_flow_id;
		else
			q2spi_hc_var5->flow_id = q2spi_req.flow_id;
	}
	q2spi->xfer->tid = q2spi_hc_var5->flow_id;
	q2spi_pkt->var5_pkt = q2spi_hc_var5;
	q2spi_pkt->vtype = VARIANT_5;
	q2spi_pkt->valid = true;
	q2spi_pkt->sync = q2spi_req.sync;
	Q2SPI_DEBUG(q2spi, "%s flow id:%d q2spi_pkt:%p pkt_var1:%p pkt_tx_dma:%p var5_pkt:%p\n",
		    __func__, q2spi_hc_var5->flow_id, q2spi_pkt,
		    q2spi_pkt->var1_pkt, q2spi_pkt->var5_tx_dma, q2spi_pkt->var5_pkt);
	q2spi_dump_ipc(q2spi, q2spi->ipc, "sma format var5(2) data_buf",
		       (char *)q2spi_hc_var5->data_buf, q2spi_req.data_len);
	return q2spi_hc_var5->flow_id;
}

static int q2spi_abort_command(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req,
			       struct q2spi_packet **q2spi_pkt_ptr)
{
	struct q2spi_host_abort_pkt *q2spi_abort_req;
	struct q2spi_packet *q2spi_pkt;

	if (!q2spi) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid q2spi\n", __func__);
		return -EINVAL;
	}
	Q2SPI_DEBUG(q2spi, "%s q2spi_req:%p cmd:%d addr:%d flow_id:%d data_len:%d\n",
		    __func__, q2spi_req, q2spi_req.cmd, q2spi_req.addr,
		    q2spi_req.flow_id, q2spi_req.data_len);
	q2spi_pkt = q2spi_kzalloc(q2spi, sizeof(struct q2spi_packet), __LINE__);
	if (!q2spi_pkt) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid q2spi_pkt\n", __func__);
		return -ENOMEM;
	}

	*q2spi_pkt_ptr = q2spi_pkt;

	q2spi_abort_req = q2spi_alloc_host_variant(q2spi, sizeof(struct q2spi_host_abort_pkt));
	if (!q2spi_abort_req) {
		Q2SPI_ERROR(q2spi, "%s Err alloc and map failed\n", __func__);
		return -EINVAL;
	}

	q2spi_abort_req->cmd = HC_ABORT;
	q2spi_abort_req->flow_id = q2spi_alloc_xfer_tid(q2spi);
	q2spi->xfer->tid = q2spi_abort_req->flow_id;
	q2spi_abort_req->code = 0;
	q2spi_pkt->abort_pkt = q2spi_abort_req;
	q2spi_pkt->vtype = VAR_ABORT;
	q2spi_pkt->m_cmd_param = Q2SPI_TX_ONLY;

	return q2spi_abort_req->flow_id;
}

static int q2spi_soft_reset(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req,
			    struct q2spi_packet **q2spi_pkt_ptr)
{
	struct q2spi_host_soft_reset_pkt *q2spi_softreset_req;
	struct q2spi_packet *q2spi_pkt;

	if (!q2spi) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid q2spi\n", __func__);
		return -EINVAL;
	}
	Q2SPI_DEBUG(q2spi, "%s q2spi_req:%p cmd:%d addr:%d flow_id:%d data_len:%d\n",
		    __func__, q2spi_req, q2spi_req.cmd, q2spi_req.addr,
				q2spi_req.flow_id, q2spi_req.data_len);
	q2spi_pkt = q2spi_kzalloc(q2spi, sizeof(struct q2spi_packet), __LINE__);
	if (!q2spi_pkt) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid q2spi_pkt\n", __func__);
		return -ENOMEM;
	}

	q2spi_softreset_req = q2spi_alloc_host_variant(q2spi,
						       sizeof(struct q2spi_host_soft_reset_pkt));
	if (!q2spi_softreset_req) {
		Q2SPI_ERROR(q2spi, "%s Err alloc and map failed\n", __func__);
		q2spi_kfree(q2spi, q2spi_pkt, __LINE__);
		return -EINVAL;
	}
	*q2spi_pkt_ptr = q2spi_pkt;
	q2spi_softreset_req->cmd = HC_SOFT_RESET;
	q2spi_softreset_req->flags = HC_SOFT_RESET_FLAGS;
	q2spi_softreset_req->code = HC_SOFT_RESET_CODE;
	q2spi_pkt->soft_reset_pkt = q2spi_softreset_req;
	q2spi_pkt->soft_reset_tx_dma = q2spi->dma_buf;
	q2spi_pkt->vtype = VAR_SOFT_RESET;
	q2spi_pkt->m_cmd_param = Q2SPI_TX_ONLY;

	return 0;
}

void q2spi_notify_data_avail_for_client(struct q2spi_geni *q2spi)
{
	Q2SPI_DEBUG(q2spi, "%s wake userspace\n", __func__);
	atomic_inc(&q2spi->rx_avail);
	wake_up_interruptible(&q2spi->readq);
	wake_up(&q2spi->read_wq);
}

int q2spi_hrf_flow(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req,
		   struct q2spi_packet **q2spi_pkt_ptr)
{
	struct q2spi_request *q2spi_hrf_req;
	struct q2spi_packet *q2spi_pkt = NULL;
	int ret = 0;

	q2spi->hrf_flow = true;
	ret = q2spi_hrf_entry_format(q2spi, q2spi_req, &q2spi_hrf_req);
	if (ret < 0) {
		Q2SPI_ERROR(q2spi, "%s Err q2spi_hrf_entry_format failed ret:%d\n", __func__, ret);
		return ret;
	}

	Q2SPI_DEBUG(q2spi, "%s q2spi req:%p cmd:%d flow_id:%d data_buff:%p\n",
		    __func__, q2spi_req, q2spi_req.cmd, q2spi_req.flow_id, q2spi_req.data_buff);
	Q2SPI_DEBUG(q2spi, "%s addr:0x%x proto:0x%x data_len:0x%x\n",
		    __func__, q2spi_req.addr, q2spi_req.proto_ind, q2spi_req.data_len);

	ret = q2spi_frame_lra(q2spi, *q2spi_hrf_req, &q2spi_pkt, VARIANT_1_HRF);
	Q2SPI_DEBUG(q2spi, "%s q2spi_hrf_req:%p q2spi_pkt:%p\n",
		    __func__, q2spi_hrf_req, q2spi_pkt);
	if (ret < 0) {
		Q2SPI_ERROR(q2spi, "%s Err q2spi_frame_lra failed ret:%d\n", __func__, ret);
		return ret;
	}

	q2spi_pkt->hrf_flow_id = ret;
	ret = q2spi_sma_format(q2spi, q2spi_req, q2spi_pkt);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_sma_format failed ret:%d\n", __func__, ret);
		return ret;
	}
	list_add_tail(&q2spi_pkt->list, &q2spi->tx_queue_list);
	q2spi_pkt->vtype = VARIANT_5_HRF;
	q2spi_kfree(q2spi, q2spi_hrf_req, __LINE__);
	*q2spi_pkt_ptr = q2spi_pkt;
	Q2SPI_DEBUG(q2spi, "%s q2spi_req:%p q2spi_pkt:%p\n", __func__, q2spi_req, q2spi_pkt);
	return ret;
}

void q2spi_print_req_cmd(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req)
{
	if (q2spi_req.cmd == LOCAL_REG_READ)
		Q2SPI_DEBUG(q2spi, "%s cmd:LOCAL_REG_READ\n", __func__);
	else if (q2spi_req.cmd == LOCAL_REG_WRITE)
		Q2SPI_DEBUG(q2spi, "%s cmd:LOCAL_REG_WRITE\n", __func__);
	else if (q2spi_req.cmd == HRF_READ)
		Q2SPI_DEBUG(q2spi, "%s cmd:HRF_READ\n", __func__);
	else if (q2spi_req.cmd == HRF_WRITE)
		Q2SPI_DEBUG(q2spi, "%s cmd:HRF_WRITE\n", __func__);
	else if (q2spi_req.cmd == DATA_READ)
		Q2SPI_DEBUG(q2spi, "%s cmd:DATA_READ\n", __func__);
	else if (q2spi_req.cmd == DATA_WRITE)
		Q2SPI_DEBUG(q2spi, "%s cmd:DATA_WRITE\n", __func__);
	else if (q2spi_req.cmd == SOFT_RESET)
		Q2SPI_DEBUG(q2spi, "%s cmd:SOFT_RESET\n", __func__);
	else
		Q2SPI_DEBUG(q2spi, "%s Invalid cmd:%d\n", __func__, q2spi_req.cmd);
}

/*
 * q2spi_del_pkt_from_tx_queue - Delete q2spi packets from tx_queue_list
 * @q2spi: pointer to q2spi_geni
 * @cur_q2spi_pkt: ponter to q2spi_packet
 *
 * This function iterates through the tx_queue_list and obtains the cur_q2spi_pkt
 * and delete the completed packet from the list if q2spi_pkt->in_use is under deletion.
 *
 * Return: Returns true if given packet is found in tx_queue_list and deleted, else returns false.
 */
bool q2spi_del_pkt_from_tx_queue(struct q2spi_geni *q2spi, struct q2spi_packet *cur_q2spi_pkt)
{
	struct q2spi_packet *q2spi_pkt, *q2spi_pkt_tmp;
	bool found = false;

	if (!cur_q2spi_pkt) {
		Q2SPI_DEBUG(q2spi, "%s q2spi_pkt NULL list_empty:%d\n",
			    __func__, list_empty(&q2spi->tx_queue_list));
		return found;
	}

	mutex_lock(&q2spi->queue_lock);
	list_for_each_entry_safe(q2spi_pkt, q2spi_pkt_tmp, &q2spi->tx_queue_list, list) {
		if (cur_q2spi_pkt == q2spi_pkt) {
			Q2SPI_DEBUG(q2spi, "%s Found q2spi_pkt:%p in_use:%d\n", __func__,
				    q2spi_pkt, q2spi_pkt->in_use);
			if (q2spi_pkt->in_use == IN_DELETION) {
				list_del(&q2spi_pkt->list);
				q2spi_pkt->in_use = DELETED;
				found = true;
				break;
			}
		}
		Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p in_use:%d\n", __func__,
			    q2spi_pkt, q2spi_pkt->in_use);
	}
	mutex_unlock(&q2spi->queue_lock);

	if (!found)
		Q2SPI_DEBUG(q2spi, "%s Couldn't find q2spi_pkt:%p\n", __func__, cur_q2spi_pkt);

	if (list_empty(&q2spi->tx_queue_list))
		Q2SPI_DEBUG(q2spi, "%s Tx queue list is empty\n", __func__);
	else
		Q2SPI_DEBUG(q2spi, "%s Tx queue list is NOT empty!!!\n", __func__);
	return found;
}

/*
 * q2spi_add_req_to_tx_queue - Add q2spi packets to tx_queue_list
 * @q2spi: pointer to q2spi_geni
 * @q2spi_pkt_ptr: ponter to q2spi_packet
 *
 * This function frames the Q2SPI host request based on request type
 * add the packet to tx_queue_list.
 *
 * Return: 0 on success. Error code on failure.
 */
int q2spi_add_req_to_tx_queue(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req,
			      struct q2spi_packet **q2spi_pkt_ptr)
{
	struct q2spi_packet *q2spi_pkt = NULL;
	int ret = -EINVAL;

	q2spi_print_req_cmd(q2spi, q2spi_req);
	Q2SPI_DEBUG(q2spi, "%s list_empty:%d\n",
		    __func__, list_empty(&q2spi->tx_queue_list));
	if (q2spi_req.cmd == LOCAL_REG_READ || q2spi_req.cmd == LOCAL_REG_WRITE) {
		ret = q2spi_frame_lra(q2spi, q2spi_req, &q2spi_pkt, VARIANT_1_LRA);
		if (ret < 0) {
			Q2SPI_DEBUG(q2spi, "q2spi_frame_lra failed ret:%d\n", ret);
			return ret;
		}
		list_add_tail(&q2spi_pkt->list, &q2spi->tx_queue_list);
	} else if (q2spi_req.cmd == DATA_READ || q2spi_req.cmd == DATA_WRITE) {
		q2spi_pkt = q2spi_kzalloc(q2spi, sizeof(struct q2spi_packet), __LINE__);
		if (!q2spi_pkt) {
			Q2SPI_DEBUG(q2spi, "%s Err Invalid q2spi_pkt\n", __func__);
			return -ENOMEM;
		}
		ret = q2spi_sma_format(q2spi, q2spi_req, q2spi_pkt);
		if (ret < 0) {
			Q2SPI_DEBUG(q2spi, "q2spi_sma_format failed ret:%d\n", ret);
			return ret;
		}
		Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p in_use=%d ret:%d\n",
			    __func__, q2spi_pkt, q2spi_pkt->in_use, ret);
		if (atomic_read(&q2spi->doorbell_pending))
			list_add(&q2spi_pkt->list, &q2spi->tx_queue_list);
		else
			list_add_tail(&q2spi_pkt->list, &q2spi->tx_queue_list);
	} else if (q2spi_req.cmd == HRF_READ || q2spi_req.cmd == HRF_WRITE) {
		ret = q2spi_hrf_flow(q2spi, q2spi_req, &q2spi_pkt);
		if (ret < 0) {
			Q2SPI_DEBUG(q2spi, "q2spi_hrf_flow failed ret:%d\n", ret);
			return ret;
		}
	} else if (q2spi_req.cmd == ABORT) {
		ret = q2spi_abort_command(q2spi, q2spi_req, &q2spi_pkt);
		if (ret < 0) {
			Q2SPI_DEBUG(q2spi, "abort_command failed ret:%d\n", ret);
			return ret;
		}
		list_add_tail(&q2spi_pkt->list, &q2spi->tx_queue_list);
	} else if (q2spi_req.cmd == SOFT_RESET) {
		ret = q2spi_soft_reset(q2spi, q2spi_req, &q2spi_pkt);
		if (ret < 0) {
			Q2SPI_DEBUG(q2spi, "soft_reset failed ret:%d\n", ret);
			return ret;
		}
		list_add_tail(&q2spi_pkt->list, &q2spi->tx_queue_list);
	} else {
		Q2SPI_ERROR(q2spi, "%s Err cmd:%d\n", __func__, q2spi_req.cmd);
		return -EINVAL;
	}

	if (q2spi_pkt) {
		*q2spi_pkt_ptr = q2spi_pkt;
		Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p req_cmd:%d ret:%d\n",
			    __func__, q2spi_pkt, q2spi_req.cmd, ret);
	} else {
		Q2SPI_DEBUG(q2spi, "%s req_cmd:%d ret:%d\n", __func__, q2spi_req.cmd, ret);
	}
	return ret;
}

/*
 * q2spi_cmd_type_valid - checks if q2spi_request command type is supported
 *
 * @q2spi: Pointer to main q2spi_geni structure
 * @q2spi_req: pointer to q2spi request
 *
 * Return: true if q2spi request command is of valid type, else false
 */
bool q2spi_cmd_type_valid(struct q2spi_geni *q2spi, struct q2spi_request *q2spi_req)
{
	if (q2spi_req->cmd != LOCAL_REG_READ &&
	    q2spi_req->cmd != LOCAL_REG_WRITE &&
	    q2spi_req->cmd != DATA_READ &&
	    q2spi_req->cmd != DATA_WRITE &&
	    q2spi_req->cmd != HRF_READ &&
	    q2spi_req->cmd != HRF_WRITE &&
	    q2spi_req->cmd != SOFT_RESET &&
	    q2spi_req->cmd != ABORT) {
		Q2SPI_DEBUG(q2spi, "%s Err Invalid cmd type %d\n", __func__, q2spi_req->cmd);
		return false;
	}

	if (q2spi_req->cmd != SOFT_RESET && !q2spi_req->data_len) {
		pr_err("%s Invalid data len %d bytes\n", __func__, q2spi_req->data_len);
		return false;
	}
	return true;
}

static int q2spi_check_var1_avail_buff(struct q2spi_geni *q2spi)
{
	unsigned int i, count = 0;

	for (i = 0; i < Q2SPI_MAX_BUF; i++) {
		if (!q2spi->var1_buf_used[i])
			count++;
		else
			Q2SPI_DEBUG(q2spi, "%s Var1 buffer in use %p\n",
				    __func__, q2spi->var1_buf_used[i]);
	}
	return count;
}

/*
 * __q2spi_transfer - Queues the work to transfer q2spi packet present in tx queue
 * and wait for its completion
 * @q2spi: pointer to q2spi_geni structure
 * @q2spi_req: Pointer to q2spi_request structure
 * @len: Represents transfer length of the q2spi request
 *
 * This function supports sync mode and queue the work to processor and
 * wait for completion of sync_wait.
 *
 * Return: returns length of data transferred on success. Failure code in case of async mode
 * or any failures.
 */
static int __q2spi_transfer(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req, size_t len)
{
	unsigned long timeout = 0, xfer_timeout = 0;

	if (!q2spi_req.sync) {
		Q2SPI_ERROR(q2spi, "%s async mode not supported\n", __func__);
		return -EINVAL;
	}

	reinit_completion(&q2spi->sync_wait);
	kthread_queue_work(q2spi->kworker, &q2spi->send_messages);

	xfer_timeout = msecs_to_jiffies(XFER_TIMEOUT_OFFSET);
	Q2SPI_DEBUG(q2spi, "%s waiting for sync_wait\n", __func__);
	timeout = wait_for_completion_interruptible_timeout
				(&q2spi->sync_wait, xfer_timeout);
	if (timeout <= 0) {
		Q2SPI_DEBUG(q2spi, "%s Err timeout for sync_wait\n", __func__);
		return -ETIMEDOUT;
	} else if (atomic_read(&q2spi->retry)) {
		atomic_dec(&q2spi->retry);
		Q2SPI_DEBUG(q2spi, "%s CR Doorbell Pending need to try again\n", __func__);
		if (atomic_read(&q2spi->doorbell_pending))
			usleep_range(10000, 20000);
		return 0;
	}

	Q2SPI_DEBUG(q2spi, "%s sync_wait completed free_buffers available:%d\n",
		    __func__, q2spi_check_var1_avail_buff(q2spi));
	if (q2spi_req.cmd == LOCAL_REG_READ) {
		if (copy_to_user(q2spi_req.data_buff, q2spi->xfer->rx_buf,
				 q2spi_req.data_len)) {
			Q2SPI_DEBUG(q2spi, "%s Err copy_to_user fail\n", __func__);
			return -EFAULT;
		}
		Q2SPI_DEBUG(q2spi, "%s ret data_len:%d\n", __func__, q2spi_req.data_len);
		return q2spi_req.data_len;
	}
	Q2SPI_DEBUG(q2spi, "%s ret len:%zu\n", __func__, len);
	return len;
}

/*
 * q2spi_transfer_soft_reset - Add soft-reset request in tx_queue list and submit q2spi transfer
 *
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Return: 0 on success. Error code on failure.
 */
static void q2spi_transfer_soft_reset(struct q2spi_geni *q2spi)
{
	struct q2spi_packet *cur_q2spi_sr_pkt;
	struct q2spi_request soft_reset_request;
	int ret = 0;

	soft_reset_request.cmd = SOFT_RESET;
	soft_reset_request.sync = 1;
	mutex_lock(&q2spi->queue_lock);
	ret = q2spi_add_req_to_tx_queue(q2spi, soft_reset_request,
					&cur_q2spi_sr_pkt);
	mutex_unlock(&q2spi->queue_lock);
	if (ret < 0) {
		Q2SPI_ERROR(q2spi, "%s Err q2spi_add_req_to_tx_queue ret:%d\n", __func__, ret);
		return;
	}
	__q2spi_transfer(q2spi, soft_reset_request, 0);
	cur_q2spi_sr_pkt->in_use = IN_DELETION;
	q2spi_del_pkt_from_tx_queue(q2spi, cur_q2spi_sr_pkt);
	q2spi_kfree(q2spi, cur_q2spi_sr_pkt->xfer, __LINE__);
}

/*
 * q2spi_transfer_check - checks if inputs from user are valid and populates q2spi_request passed
 *
 * @q2spi: Pointer to main q2spi_geni structure
 * @q2spi_req: pointer to q2spi request which need to be populated
 * @buf: Data buffer pointer passed from user space which is of type struct q2spi_transfer
 * @len: Represents transfer length of the transaction
 *
 * Return: 0 if user inputs are valid, else returns linux error codes
 */
static int q2spi_transfer_check(struct q2spi_geni *q2spi, struct q2spi_request *q2spi_req,
				const char __user *buf, size_t len)
{
	if (!q2spi)
		return -EINVAL;

	if (q2spi->hw_state_is_bad) {
		Q2SPI_ERROR(q2spi, "%s Err Retries failed, check HW state\n", __func__);
		return -EPIPE;
	}

	if (!q2spi_check_var1_avail_buff(q2spi)) {
		Q2SPI_ERROR(q2spi, "%s Err Short of var1 buffers\n", __func__);
		return -EAGAIN;
	}

	if (len != sizeof(struct q2spi_request)) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid length %d Expected %d\n",
			    __func__, len, sizeof(struct q2spi_request));
		return -EINVAL;
	}

	if (copy_from_user(q2spi_req, buf, sizeof(struct q2spi_request))) {
		Q2SPI_DEBUG(q2spi, "%s Err copy_from_user failed\n", __func__);
		return -EFAULT;
	}
	Q2SPI_DEBUG(q2spi, "%s cmd:%d data_len:%d addr:%d proto:%d ep:%d\n",
		    __func__, q2spi_req->cmd, q2spi_req->data_len, q2spi_req->addr,
		    q2spi_req->proto_ind, q2spi_req->end_point);
	Q2SPI_DEBUG(q2spi, "%s priority:%d flow_id:%d sync:%d\n",
		    __func__, q2spi_req->priority, q2spi_req->flow_id, q2spi_req->sync);

	if (!q2spi_cmd_type_valid(q2spi, q2spi_req))
		return -EINVAL;

	if (q2spi_req->addr > Q2SPI_SLAVE_END_ADDR) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid address:%x\n", __func__, q2spi_req->addr);
		return -EINVAL;
	}
	return 0;
}

/*
 * q2spi_transfer - write file operation
 * @filp: file pointer of q2spi device
 * @buf: Data buffer pointer passed from user space which is of type struct q2spi_transfer
 * @len: Represents transfer length of the transaction
 * @f_pos: file pointer position
 *
 * User space calls write api to initiate read/write transfer request from Q2SPI host.
 *
 * Return: returns length of data transferred on success. Failure code in case of any failures.
 */
static ssize_t q2spi_transfer(struct file *filp, const char __user *buf, size_t len, loff_t *f_pos)
{
	struct q2spi_geni *q2spi;
	struct q2spi_request q2spi_req;
	struct q2spi_packet *cur_q2spi_pkt, *q2spi_pkt = NULL;
	int i, ret = 0, flow_id = 0;
	void *data_buf = NULL;

	if (!filp || !buf || !len || !filp->private_data) {
		pr_err("%s Err Null pointer\n", __func__);
		return -EINVAL;
	}

	q2spi = filp->private_data;
	Q2SPI_DEBUG(q2spi, "%s Enter PID=%d free_buffers:%d\n",
		    __func__, current->pid, q2spi_check_var1_avail_buff(q2spi));

	if (q2spi_transfer_check(q2spi, &q2spi_req, buf, len))
		return -EINVAL;

	if (q2spi_req.cmd == HRF_WRITE) {
		q2spi_req.addr = Q2SPI_HRF_PUSH_ADDRESS;
		q2spi_req.sync = 1;
		q2spi_req.priority = 1;
		q2spi_req.data_len += ((q2spi_req.data_len % DATA_WORD_LEN) ?
				   (DATA_WORD_LEN - (q2spi_req.data_len % DATA_WORD_LEN)) : 0);
	}

	if (q2spi_req.cmd == LOCAL_REG_WRITE || q2spi_req.cmd == DATA_WRITE ||
	    q2spi_req.cmd == HRF_WRITE) {
		data_buf = q2spi_kzalloc(q2spi, q2spi_req.data_len, __LINE__);
		if (!data_buf) {
			Q2SPI_DEBUG(q2spi, "%s Err buffer alloc failed\n", __func__);
			return -ENOMEM;
		}

		if (copy_from_user(data_buf, q2spi_req.data_buff, q2spi_req.data_len)) {
			Q2SPI_DEBUG(q2spi, "%s Err copy_from_user failed\n", __func__);
			kfree(data_buf);
			return -EFAULT;
		}

		q2spi_dump_ipc(q2spi, q2spi->ipc, "q2spi_transfer", (char *)data_buf,
			       q2spi_req.data_len);
		q2spi_req.data_buff = data_buf;
	}

	if (atomic_read(&q2spi->doorbell_pending)) {
		Q2SPI_DEBUG(q2spi, "%s CR Doorbell Pending\n", __func__);
		usleep_range(10000, 20000);
	}

	mutex_lock(&q2spi->queue_lock);
	flow_id = q2spi_add_req_to_tx_queue(q2spi, q2spi_req, &cur_q2spi_pkt);
	mutex_unlock(&q2spi->queue_lock);
	Q2SPI_DEBUG(q2spi, "%s flow_id:%d\n", __func__, flow_id);
	if (flow_id < 0) {
		kfree(data_buf);
		Q2SPI_DEBUG(q2spi, "%s Err Failed to add tx request ret:%d\n", __func__, flow_id);
		return -ENOMEM;
	}
	for (i = 0; i <= Q2SPI_MAX_TX_RETRIES; i++) {
		ret = __q2spi_transfer(q2spi, q2spi_req, len);
		q2spi_free_xfer_tid(q2spi, flow_id);
		if (ret > 0 || i == Q2SPI_MAX_TX_RETRIES) {
			cur_q2spi_pkt->in_use = IN_DELETION;
			q2spi_del_pkt_from_tx_queue(q2spi, cur_q2spi_pkt);
			if (q2spi_req.cmd == LOCAL_REG_READ || q2spi_req.cmd == LOCAL_REG_WRITE)
				q2spi_kfree(q2spi, cur_q2spi_pkt->xfer, __LINE__);
			q2spi_kfree(q2spi, cur_q2spi_pkt, __LINE__);
			if (i == Q2SPI_MAX_TX_RETRIES) {
				/*
				 * Shouldn't reach here, retry of transfers failed,
				 * could be hw is in bad state.
				 */
				Q2SPI_DEBUG(q2spi, "%s %d retries failed, hw_state_is_bad\n",
					    __func__, i);
				q2spi->hw_state_is_bad = true;
				q2spi_dump_client_error_regs(q2spi);
			}
			return ret;
		} else if (ret == -ETIMEDOUT) {
			/* Upon transfer failure's retry here */
			Q2SPI_DEBUG(q2spi, "%s ret:%d retry_count:%d retrying cur_q2spi_pkt:%p\n",
				    __func__, ret, i + 1, cur_q2spi_pkt);
			/* Should not perform SOFT RESET when UWB sets reserved[0] bit 0 set */
			if (!(q2spi_req.reserved[0] & BIT(0)) && i == 0)
				q2spi_transfer_soft_reset(q2spi);
			q2spi_pkt = cur_q2spi_pkt;
			flow_id = q2spi_alloc_xfer_tid(q2spi);
			if (flow_id < 0) {
				Q2SPI_ERROR(q2spi, "%s Err failed to alloc xfer_tid flow_id:%d\n",
					    __func__, flow_id);
				return -EINVAL;
			}
			q2spi->xfer->tid = flow_id;
			q2spi_pkt->hrf_flow_id = flow_id;
			q2spi_pkt->var1_pkt->flow_id = flow_id;
			if (q2spi_req.cmd == LOCAL_REG_WRITE || q2spi_req.cmd == LOCAL_REG_READ)
				q2spi_pkt->vtype = VARIANT_1_LRA;
			else if (q2spi_req.cmd == HRF_WRITE)
				q2spi_pkt->vtype = VARIANT_5_HRF;
			else
				Q2SPI_DEBUG(q2spi, "%s Retry not supported for this cmd:%d\n",
					    __func__, q2spi_req.cmd);
			cur_q2spi_pkt->in_use = IN_USE_FALSE;
			Q2SPI_DEBUG(q2spi, "%s cur_q2spi_pkt=%p q2spi_pkt:%p\n",
				    __func__, cur_q2spi_pkt, q2spi_pkt);
		} else {
			/* Upon SW error break here */
			break;
		}
	}
	cur_q2spi_pkt->in_use = IN_DELETION;
	q2spi_del_pkt_from_tx_queue(q2spi, cur_q2spi_pkt);
	if (q2spi_req.cmd == LOCAL_REG_READ || q2spi_req.cmd == LOCAL_REG_WRITE)
		q2spi_kfree(q2spi, cur_q2spi_pkt->xfer, __LINE__);
	q2spi_kfree(q2spi, cur_q2spi_pkt, __LINE__);
	Q2SPI_DEBUG(q2spi, "%s End return ret:%d PID=%d\n", __func__, ret, current->pid);
	return ret;
}

static ssize_t q2spi_response(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	struct q2spi_geni *q2spi;
	struct q2spi_client_request cr_request;
	struct q2spi_cr_packet *q2spi_cr_pkt = NULL;
	struct q2spi_client_dma_pkt *q2spi_cr_var3;
	int ret = 0, dw_len = 0, i = 0, no_of_crs = 0;

	if (!filp || !buf || !count || !filp->private_data) {
		pr_err("%s Err Null pointer\n", __func__);
		return -EINVAL;
	}

	q2spi = filp->private_data;

	Q2SPI_DEBUG(q2spi, "%s Enter PID=%d\n", __func__, current->pid);
	if (q2spi->hw_state_is_bad) {
		Q2SPI_ERROR(q2spi, "%s Err Retries failed, check HW state\n", __func__);
		return -EPIPE;
	}
	Q2SPI_DEBUG(q2spi, "%s list_empty_tx_list:%d list_empty_rx_list:%d list_empty_cr_list:%d\n",
		    __func__, list_empty(&q2spi->tx_queue_list), list_empty(&q2spi->rx_queue_list),
		    list_empty(&q2spi->cr_queue_list));
	if (copy_from_user(&cr_request, buf, sizeof(struct q2spi_client_request)) != 0) {
		Q2SPI_ERROR(q2spi, "%s copy from user failed PID=%d\n", __func__, current->pid);
		return -EFAULT;
	}

	Q2SPI_DEBUG(q2spi, "%s waiting on wait_event_interruptible\n", __func__);
	/* Block on read until CR available in cr_queue_list */
	ret = wait_event_interruptible(q2spi->read_wq,
				       (!list_empty(&q2spi->cr_queue_list) &&
				       atomic_read(&q2spi->rx_avail)));
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s Err wait interrupted ret:%d\n", __func__, ret);
		return ret;
	}
	atomic_dec(&q2spi->rx_avail);
	Q2SPI_DEBUG(q2spi, "%s wait unblocked ret:%d\n", __func__, ret);
	if (!list_empty(&q2spi->cr_queue_list)) {
		q2spi_cr_pkt = list_first_entry(&q2spi->cr_queue_list,
						struct q2spi_cr_packet, list);
		no_of_crs = q2spi_cr_pkt->no_of_valid_crs;
		for (i = 0; i < no_of_crs; i++) {
			Q2SPI_DEBUG(q2spi, "%s cr_pkt:%p no_of_crs:%d i:%d type:0x%x\n",
				    __func__, q2spi_cr_pkt, no_of_crs, i, q2spi_cr_pkt->type);
			if (((q2spi_cr_pkt->type >> (2 * i)) & GENMASK(1, 0)) == 2) {
				q2spi_cr_var3 = &q2spi_cr_pkt->var3_pkt;
				Q2SPI_DEBUG(q2spi, "%s q2spi_cr_var3:%p\n",
					    __func__, q2spi_cr_var3);
				Q2SPI_DEBUG(q2spi, "q2spi_cr_var3 len_part1:%d len_part2:%d\n",
					    q2spi_cr_var3->dw_len_part1,
					    q2spi_cr_var3->dw_len_part2);
				Q2SPI_DEBUG(q2spi,
					    "q2spi_cr_var3 flow_id:%d arg1:0x%x arg2:0x%x arg3:0x%x\n",
					    q2spi_cr_var3->flow_id, q2spi_cr_var3->arg1,
					    q2spi_cr_var3->arg2, q2spi_cr_var3->arg3);
				/*
				 * Doorbell case tid will be updated by client.
				 * q2spi send the ID to userspce
				 * so that it will call HC with this flow id for async case
				 */
				cr_request.flow_id = q2spi_cr_var3->flow_id;
				cr_request.cmd = q2spi_cr_pkt->cr_hdr[i].cmd;
				dw_len = (((q2spi_cr_pkt->var3_pkt.dw_len_part3 << 12) & 0xFF) |
					((q2spi_cr_pkt->var3_pkt.dw_len_part2 << 4) & 0xFF) |
					q2spi_cr_pkt->var3_pkt.dw_len_part1);
				cr_request.data_len = (dw_len * 4) + 4;
				cr_request.end_point = q2spi_cr_var3->arg2;
				cr_request.proto_ind = q2spi_cr_var3->arg3;
				Q2SPI_DEBUG(q2spi,
					    "%s CR cmd:%d flow_id:%d data_len:%d ep:%d proto:%d status:%d\n",
					    __func__, cr_request.cmd, cr_request.flow_id,
					    cr_request.data_len, cr_request.end_point,
					    cr_request.proto_ind, cr_request.status);
			} else if ((q2spi_cr_pkt->type >> (2 * i) & GENMASK(1, 0)) == 1) {
				Q2SPI_DEBUG(q2spi, "%s cr_request.flow_id:%d status:%d\n",
					    __func__, cr_request.flow_id, cr_request.status);
			} else {
				Q2SPI_ERROR(q2spi, "%s Err Unsupported CR Type\n", __func__);
				return -EINVAL;
			}
		}
	}

	if (!q2spi_cr_pkt) {
		Q2SPI_ERROR(q2spi, "%s Err No q2spi_cr_pkt\n", __func__);
		return -EINVAL;
	}
	Q2SPI_DEBUG(q2spi, "data_len:%d ep:%d proto:%d cmd%d status%d flow_id:%d",
		    cr_request.data_len, cr_request.end_point, cr_request.proto_ind,
		    cr_request.cmd, cr_request.status, cr_request.flow_id);
	if (!q2spi_cr_pkt->xfer->rx_buf) {
		Q2SPI_ERROR(q2spi, "%s Err CR PKT rx_buf is NULL\n", __func__);
		return -EAGAIN;
	}

	q2spi_dump_ipc(q2spi, q2spi->ipc, "q2spi_response",
		       (char *)q2spi_cr_pkt->xfer->rx_buf, cr_request.data_len);
	ret = copy_to_user(buf, &cr_request, sizeof(struct q2spi_client_request));
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err copy_to_user failed ret:%d", __func__, ret);
		return -EAGAIN;
	}
	ret = copy_to_user(cr_request.data_buff,
			   (void *)q2spi_cr_pkt->xfer->rx_buf, cr_request.data_len);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err copy_to_user data_buff failed ret:%d", __func__, ret);
		return -EAGAIN;
	}
	ret = (sizeof(struct q2spi_client_request) - ret);

	Q2SPI_DEBUG(q2spi, "%s list_empty tx_list:%d rx_list:%d cr_list:%d\n",
		    __func__, list_empty(&q2spi->tx_queue_list), list_empty(&q2spi->rx_queue_list),
		    list_empty(&q2spi->cr_queue_list));
	Q2SPI_DEBUG(q2spi, "%s q2spi_cr_pkt:%p q2spi_pkt:%p in_use:%d\n", __func__, q2spi_cr_pkt,
		    q2spi_cr_pkt->q2spi_pkt, q2spi_cr_pkt->q2spi_pkt->in_use);
	q2spi_cr_pkt->q2spi_pkt->in_use = IN_DELETION;
	q2spi_del_pkt_from_tx_queue(q2spi, q2spi_cr_pkt->q2spi_pkt);

	spin_lock(&q2spi->cr_queue_lock);
	list_del(&q2spi_cr_pkt->list);
	spin_unlock(&q2spi->cr_queue_lock);
	q2spi_kfree(q2spi, q2spi_cr_pkt->xfer, __LINE__);
	q2spi_kfree(q2spi, q2spi_cr_pkt, __LINE__);
	Q2SPI_DEBUG(q2spi, "%s End ret:%d PID=%d", __func__, ret, current->pid);
	return ret;
}

static __poll_t q2spi_poll(struct file *filp, poll_table *wait)
{
	struct q2spi_geni *q2spi;
	__poll_t mask = 0;

	if (!filp || !filp->private_data) {
		pr_err("%s Err Null pointer\n", __func__);
		return -EINVAL;
	}

	q2spi = filp->private_data;
	Q2SPI_DEBUG(q2spi, "%s PID:%d\n", __func__, current->pid);
	poll_wait(filp, &q2spi->readq, wait);
	Q2SPI_DEBUG(q2spi, "%s after poll_wait\n", __func__);
	if (atomic_read(&q2spi->rx_avail)) {
		mask = (POLLIN | POLLRDNORM);
		Q2SPI_DEBUG(q2spi, "%s RX data available\n", __func__);
	}
	return mask;
}

static int q2spi_release(struct inode *inode, struct file *filp)
{
	struct q2spi_geni *q2spi;
	struct q2spi_cr_packet *q2spi_cr_pkt = NULL;

	if (!filp || !filp->private_data) {
		pr_err("%s Err close return\n", __func__);
		return -EINVAL;
	}
	q2spi = filp->private_data;
	q2spi->port_release = true;

	Q2SPI_DEBUG(q2spi, "%s rx_avail:%d, tx_queue:%d cr_queue:%d\n",
		    __func__, atomic_read(&q2spi->rx_avail),
		    !list_empty(&q2spi->tx_queue_list), !list_empty(&q2spi->cr_queue_list));
	/* Delay to ensure any pending CRs in progress are consumed */
	usleep_range(50000, 100000);

	if (atomic_read(&q2spi->rx_avail)) {
		while (!list_empty(&q2spi->cr_queue_list)) {
			q2spi_cr_pkt = list_first_entry(&q2spi->cr_queue_list,
							struct q2spi_cr_packet, list);
			if (q2spi_cr_pkt) {
				Q2SPI_DEBUG(q2spi, "%s Delete q2spi_cr_pkt\n", __func__);
				q2spi_cr_pkt->q2spi_pkt->in_use = IN_DELETION;
				q2spi_del_pkt_from_tx_queue(q2spi, q2spi_cr_pkt->q2spi_pkt);
				spin_lock(&q2spi->cr_queue_lock);
				list_del(&q2spi_cr_pkt->list);
				spin_unlock(&q2spi->cr_queue_lock);
				q2spi_kfree(q2spi, q2spi_cr_pkt->xfer, __LINE__);
				q2spi_kfree(q2spi, q2spi_cr_pkt, __LINE__);
			}
		}
		atomic_dec(&q2spi->rx_avail);
	}
	q2spi->doorbell_setup = false;
	q2spi_geni_resources_off(q2spi);
	Q2SPI_DEBUG(q2spi, "%s End PID:%d allocs:%d rx_avail:%d tx_queue:%d cr_queue:%d\n",
		    __func__, current->pid, atomic_read(&q2spi->alloc_count),
		    atomic_read(&q2spi->rx_avail), !list_empty(&q2spi->tx_queue_list),
		    !list_empty(&q2spi->cr_queue_list));
	return 0;
}

static const struct file_operations q2spi_fops = {
	.owner =	THIS_MODULE,
	.open =		q2spi_open,
	.write =	q2spi_transfer,
	.read =		q2spi_response,
	.poll =		q2spi_poll,
	.release =	q2spi_release,
};

static int q2spi_se_clk_cfg(u32 speed_hz, struct q2spi_geni *q2spi,
			    int *clk_idx, int *clk_div)
{
	unsigned long sclk_freq;
	unsigned long res_freq;
	struct geni_se *se = &q2spi->se;
	int ret = 0;

	ret = geni_se_clk_freq_match(&q2spi->se, (speed_hz * q2spi->oversampling), clk_idx,
				     &sclk_freq, false);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err Failed(%d) to find src clk for 0x%x\n",
			    __func__, ret, speed_hz);
		return ret;
	}

	*clk_div = DIV_ROUND_UP(sclk_freq, (q2spi->oversampling * speed_hz));

	if (!(*clk_div)) {
		Q2SPI_ERROR(q2spi, "%s Err sclk:%lu oversampling:%d speed:%u\n",
			    __func__, sclk_freq, q2spi->oversampling, speed_hz);
		return -EINVAL;
	}

	res_freq = (sclk_freq / (*clk_div));

	Q2SPI_DEBUG(q2spi, "%s req speed:%u resultant:%lu sclk:%lu, idx:%d, div:%d\n",
		    __func__, speed_hz, res_freq, sclk_freq, *clk_idx, *clk_div);

	ret = clk_set_rate(se->clk, sclk_freq);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err clk_set_rate failed %d\n", __func__, ret);
		return ret;
	}
	return 0;
}

/**
 * q2spi_set_clock - Q2SPI SE clock configuration
 * @q2spi_geni: controller to process queue
 * @clk_hz: SE clock in hz
 *
 * Set the Serial clock and dividers required as per the
 * desired speed.
 *
 * Return: 0 on success. Error code on failure.
 */
static int q2spi_set_clock(struct q2spi_geni *q2spi, unsigned long clk_hz)
{
	u32 clk_sel, m_clk_cfg, idx, div;
	struct geni_se *se = &q2spi->se;
	int ret;

	if (clk_hz == q2spi->cur_speed_hz)
		return 0;

	ret = q2spi_se_clk_cfg(clk_hz, q2spi, &idx, &div);
	if (ret) {
		Q2SPI_ERROR(q2spi, "Err setting clk to %lu: %d\n", clk_hz, ret);
		return ret;
	}

	/*
	 * Q2SPI core clock gets configured with the requested frequency
	 * or the frequency closer to the requested frequency.
	 * For that reason requested frequency is stored in the
	 * cur_speed_hz and referred in the consecutive transfer instead
	 * of calling clk_get_rate() API.
	 */
	q2spi->cur_speed_hz = clk_hz;

	clk_sel = idx & CLK_SEL_MSK;
	m_clk_cfg = (div << CLK_DIV_SHFT) | SER_CLK_EN;
	writel(clk_sel, se->base + SE_GENI_CLK_SEL);
	writel(m_clk_cfg, se->base + GENI_SER_M_CLK_CFG);

	Q2SPI_DEBUG(q2spi, "%s spee_hz:%u clk_sel:0x%x m_clk_cfg:0x%x div:%d\n",
		    __func__, q2spi->cur_speed_hz, clk_sel, m_clk_cfg, div);
	return ret;
}

void q2spi_geni_se_dump_regs(struct q2spi_geni *q2spi)
{
	Q2SPI_ERROR(q2spi, "GENI_STATUS: 0x%x\n", geni_read_reg(q2spi->base, SE_GENI_STATUS));
	Q2SPI_ERROR(q2spi, "SPI_TRANS_CFG: 0x%x\n", geni_read_reg(q2spi->base, SE_SPI_TRANS_CFG));
	Q2SPI_ERROR(q2spi, "SE_GENI_IOS: 0x%x\n", geni_read_reg(q2spi->base, SE_GENI_IOS));
	Q2SPI_ERROR(q2spi, "SE_GENI_M_CMD0: 0x%x\n", geni_read_reg(q2spi->base, SE_GENI_M_CMD0));
	Q2SPI_ERROR(q2spi, "GENI_M_CMD_CTRL_REG: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_GENI_M_CMD_CTRL_REG));
	Q2SPI_ERROR(q2spi, "GENI_M_IRQ_STATUS: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_GENI_M_IRQ_STATUS));
	Q2SPI_ERROR(q2spi, "GENI_M_IRQ_EN: 0x%x\n", geni_read_reg(q2spi->base, SE_GENI_M_IRQ_EN));
	Q2SPI_ERROR(q2spi, "GENI_TX_FIFO_STATUS: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_GENI_TX_FIFO_STATUS));
	Q2SPI_ERROR(q2spi, "GENI_RX_FIFO_STATUS: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_GENI_RX_FIFO_STATUS));
	Q2SPI_ERROR(q2spi, "DMA_TX_PTR_L: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_TX_PTR_L));
	Q2SPI_ERROR(q2spi, "DMA_TX_PTR_H: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_TX_PTR_H));
	Q2SPI_ERROR(q2spi, "DMA_TX_ATTR: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_TX_ATTR));
	Q2SPI_ERROR(q2spi, "DMA_TX_LEN: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_TX_LEN));
	Q2SPI_ERROR(q2spi, "DMA_TX_IRQ_STAT: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_DMA_TX_IRQ_STAT));
	Q2SPI_ERROR(q2spi, "DMA_TX_LEN_IN: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_TX_LEN_IN));
	Q2SPI_ERROR(q2spi, "DMA_RX_PTR_L: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_RX_PTR_L));
	Q2SPI_ERROR(q2spi, "DMA_RX_PTR_H: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_RX_PTR_H));
	Q2SPI_ERROR(q2spi, "DMA_RX_ATTR: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_RX_ATTR));
	Q2SPI_ERROR(q2spi, "DMA_RX_LEN: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_RX_LEN));
	Q2SPI_ERROR(q2spi, "DMA_RX_IRQ_STAT: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_DMA_RX_IRQ_STAT));
	Q2SPI_ERROR(q2spi, "DMA_RX_LEN_IN: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_RX_LEN_IN));
	Q2SPI_ERROR(q2spi, "DMA_DEBUG_REG0: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_DEBUG_REG0));
}

static irqreturn_t q2spi_geni_irq(int irq, void *data)
{
	struct q2spi_geni *q2spi = data;
	unsigned int m_irq_status;
	unsigned int s_irq_status;
	unsigned int dma_tx_status;
	unsigned int dma_rx_status;

	m_irq_status = geni_read_reg(q2spi->base, SE_GENI_M_IRQ_STATUS);
	s_irq_status = geni_read_reg(q2spi->base, SE_GENI_S_IRQ_STATUS);
	dma_tx_status = geni_read_reg(q2spi->base, SE_DMA_TX_IRQ_STAT);
	dma_rx_status = geni_read_reg(q2spi->base, SE_DMA_RX_IRQ_STAT);
	Q2SPI_DEBUG(q2spi, "%s sirq 0x%x mirq:0x%x dma_tx:0x%x dma_rx:0x%x\n",
		    __func__, s_irq_status, m_irq_status, dma_tx_status, dma_rx_status);
	geni_write_reg(m_irq_status, q2spi->base, SE_GENI_M_IRQ_CLEAR);
	geni_write_reg(s_irq_status, q2spi->base, SE_GENI_S_IRQ_CLEAR);
	geni_write_reg(dma_tx_status, q2spi->base, SE_DMA_TX_IRQ_CLR);
	geni_write_reg(dma_rx_status, q2spi->base, SE_DMA_RX_IRQ_CLR);

	return IRQ_HANDLED;
}

/*
 * q2spi_dump_client_error_regs - Dump Q2SPI slave error registers using LRA
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Read Q2SPI_SLAVE_ERROR register for errors encounted in slave
 * Read Q2SPI_HDR_ERROR register for error encountered in header parsing
 */
void q2spi_dump_client_error_regs(struct q2spi_geni *q2spi)
{
	int ret = 0;

	ret = q2spi_read_reg(q2spi, Q2SPI_SLAVE_ERROR);
	if (ret)
		Q2SPI_ERROR(q2spi, "Err SLAVE_ERROR Reg read failed: %d\n", ret);

	ret = q2spi_read_reg(q2spi, Q2SPI_HDR_ERROR);
	if (ret)
		Q2SPI_ERROR(q2spi, "Err HDR_ERROR Reg read failed: %d\n", ret);
}

static int q2spi_gsi_submit(struct q2spi_packet *q2spi_pkt)
{
	struct q2spi_geni *q2spi = q2spi_pkt->q2spi;
	struct q2spi_dma_transfer *xfer = q2spi->xfer;
	int ret = 0;

	Q2SPI_DEBUG(q2spi, "%s q2spi:%p xfer:%p\n", __func__, q2spi, xfer);
	mutex_lock(&q2spi->gsi_lock);
	ret = q2spi_setup_gsi_xfer(q2spi_pkt); /* Todo check it */
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err q2spi_setup_gsi_xfer failed: %d\n", __func__, ret);
		q2spi_geni_se_dump_regs(q2spi);
		gpi_dump_for_geni(q2spi->gsi->tx_c);
		goto unmap_buf;
	}
	Q2SPI_DEBUG(q2spi, "%s waiting check_gsi_transfer_completion\n", __func__);
	ret = check_gsi_transfer_completion(q2spi);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err completion timeout: %d\n", __func__, ret);
		q2spi_geni_se_dump_regs(q2spi);
		dev_err(q2spi->dev, "%s Err dump gsi regs\n", __func__);
		gpi_dump_for_geni(q2spi->gsi->tx_c);
		goto unmap_buf;
	}

	Q2SPI_DEBUG(q2spi, "%s flow_id:%d tx_dma:%p rx_dma:%p tid:%d\n",
		    __func__, q2spi->xfer->tid, xfer->tx_dma, xfer->rx_dma, q2spi->xfer->tid);
unmap_buf:
	q2spi_unmap_dma_buf_used(q2spi, xfer->tx_dma, xfer->rx_dma);
	mutex_unlock(&q2spi->gsi_lock);
	return ret;
}

/*
 * q2spi_prep_soft_reset_request - Prepare soft reset packet transfer
 * @q2spi: pointer to q2spi_geni
 * @q2spi_pkt: pointer to q2spi packet
 *
 * This function prepare the transfer for soft reset packet to submit to gsi.
 *
 * Return: 0 on success. Error code on failure.
 */
static int q2spi_prep_soft_reset_request(struct q2spi_geni *q2spi, struct q2spi_packet *q2spi_pkt)
{
	struct q2spi_host_soft_reset_pkt *reset_pkt;
	struct q2spi_dma_transfer *reset_xfer = NULL;

	Q2SPI_DEBUG(q2spi, "%s q2pi_pkt->soft_reset_pkt:%p &q2spi_pkt->soft_reset_pkt:%p\n",
		    __func__, q2spi_pkt->soft_reset_pkt, &q2spi_pkt->soft_reset_pkt);
	reset_xfer = q2spi_kzalloc(q2spi, sizeof(struct q2spi_dma_transfer), __LINE__);
	if (!reset_xfer) {
		Q2SPI_ERROR(q2spi, "%s Err reset_xfer alloc failed\n", __func__);
		return -ENOMEM;
	}
	reset_xfer->cmd = q2spi_pkt->m_cmd_param;
	reset_pkt = q2spi_pkt->soft_reset_pkt;
	reset_xfer->tx_buf = q2spi_pkt->soft_reset_pkt;
	reset_xfer->tx_dma = q2spi_pkt->soft_reset_tx_dma;
	reset_xfer->tx_data_len = 0;
	reset_xfer->tx_len = Q2SPI_HEADER_LEN;
	Q2SPI_DEBUG(q2spi, "%s var1_xfer->tx_len:%d var1_xfer->tx_data_len:%d\n",
		    __func__, reset_xfer->tx_len, reset_xfer->tx_data_len);

	Q2SPI_DEBUG(q2spi, "%s tx_buf:%p tx_dma:%p\n", __func__,
		    reset_xfer->tx_buf, reset_xfer->tx_dma);
	q2spi_dump_ipc(q2spi, q2spi->ipc, "Preparing reset tx_buf DMA TX",
		       (char *)reset_xfer->tx_buf, reset_xfer->tx_len);
	q2spi->xfer = reset_xfer;
	Q2SPI_DEBUG(q2spi, "%s xfer:%p\n", __func__, q2spi->xfer);
	return 0;
}

/*
 * q2spi_prep_var1_request - Prepare q2spi variant1 type packet transfer
 * @q2spi: pointer to q2spi_geni
 * @q2spi_pkt: pointer to q2spi packet
 *
 * This function prepares variant1 type transfer request to submit to gsi.
 *
 * Return: 0 on success. Error code on failure.
 */
static int q2spi_prep_var1_request(struct q2spi_geni *q2spi, struct q2spi_packet *q2spi_pkt)
{
	struct q2spi_host_variant1_pkt *q2spi_hc_var1;
	struct q2spi_dma_transfer *var1_xfer = NULL;

	Q2SPI_DEBUG(q2spi, "%s q2spi_pkt->var1_pkt:%p &q2spi_pkt->var1_pkt:%p\n",
		    __func__, q2spi_pkt->var1_pkt, &q2spi_pkt->var1_pkt);
	var1_xfer = q2spi_kzalloc(q2spi, sizeof(struct q2spi_dma_transfer), __LINE__);
	if (!var1_xfer) {
		Q2SPI_ERROR(q2spi, "%s Err var1_xfer alloc failed\n", __func__);
		return -ENOMEM;
	}
	var1_xfer->cmd = q2spi_pkt->m_cmd_param;
	q2spi_hc_var1 = q2spi_pkt->var1_pkt;
	var1_xfer->tx_buf = q2spi_pkt->var1_pkt;
	var1_xfer->tx_dma = q2spi_pkt->var1_tx_dma;
	var1_xfer->tx_data_len = (q2spi_pkt->var1_pkt->dw_len * 4) + 4;
	var1_xfer->tx_len = Q2SPI_HEADER_LEN + var1_xfer->tx_data_len;
	Q2SPI_DEBUG(q2spi, "%s var1_xfer->tx_len:%d var1_xfer->tx_data_len:%d\n",
		    __func__, var1_xfer->tx_len, var1_xfer->tx_data_len);
	var1_xfer->tid = q2spi_pkt->var1_pkt->flow_id;
	if (q2spi_pkt->m_cmd_param == Q2SPI_TX_RX) {
		var1_xfer->tx_len = Q2SPI_HEADER_LEN;
		Q2SPI_DEBUG(q2spi, "%s var1_xfer->tx_len:%d var1_xfer->tx_data_len:%d\n",
			    __func__, var1_xfer->tx_len, var1_xfer->tx_data_len);
		var1_xfer->rx_buf = q2spi->xfer->rx_buf;
		var1_xfer->rx_dma = q2spi->xfer->rx_dma;
		var1_xfer->rx_data_len = (q2spi_pkt->var1_pkt->dw_len * 4) + 4;
		var1_xfer->rx_len = var1_xfer->rx_data_len;
		Q2SPI_DEBUG(q2spi, "%s var1_xfer->rx_len:%d var1_xfer->rx_data_len:%d\n",
			    __func__, var1_xfer->rx_len, var1_xfer->rx_data_len);
	}

	Q2SPI_DEBUG(q2spi, "%s tx_buf:%p tx_dma:%p rx_buf:%p rx_dma:%p\n", __func__,
		    var1_xfer->tx_buf, var1_xfer->tx_dma, var1_xfer->rx_buf, var1_xfer->rx_dma);
	q2spi_dump_ipc(q2spi, q2spi->ipc, "Preparing var1 tx_buf DMA TX",
		       (char *)var1_xfer->tx_buf, var1_xfer->tx_len);
	q2spi->xfer = var1_xfer;
	q2spi_pkt->xfer = var1_xfer;
	Q2SPI_DEBUG(q2spi, "%s xfer:%p\n", __func__, q2spi->xfer);
	return 0;
}

/*
 * q2spi_prep_var5_request - Prepare q2spi variant5 type packet transfer
 * @q2spi: pointer to q2spi_geni
 * @q2spi_pkt: pointer to q2spi packet
 *
 * This function prepares variant5 type transfer request to submit to gsi.
 *
 * Return: 0 on success. Error code on failure.
 */
static int q2spi_prep_var5_request(struct q2spi_geni *q2spi, struct q2spi_packet *q2spi_pkt)
{
	struct q2spi_host_variant4_5_pkt *q2spi_hc_var5;
	struct q2spi_dma_transfer *var5_xfer = NULL;

	var5_xfer = q2spi_kzalloc(q2spi, sizeof(struct q2spi_dma_transfer), __LINE__);
	if (!var5_xfer) {
		Q2SPI_ERROR(q2spi, "%s Err var5_xfer alloc failed\n", __func__);
		return -ENOMEM;
	}
	Q2SPI_DEBUG(q2spi, "%s q2spi_pkt->var5_pkt:%p var5_tx_dma:%p\n",
		    __func__, q2spi_pkt->var5_pkt, q2spi_pkt->var5_tx_dma);
	q2spi_hc_var5 = q2spi_pkt->var5_pkt;
	var5_xfer->cmd = q2spi_pkt->m_cmd_param;
	var5_xfer->tx_buf = q2spi_pkt->var5_pkt;
	var5_xfer->tx_dma = q2spi_pkt->var5_tx_dma;
	var5_xfer->tid = q2spi_pkt->var5_pkt->flow_id;
	var5_xfer->tx_data_len = q2spi_pkt->data_length;
	var5_xfer->tx_len = Q2SPI_HEADER_LEN + var5_xfer->tx_data_len;
	Q2SPI_DEBUG(q2spi, "%s var5_xfer->tx_len:%d var5_xfer->tx_data_len:%d\n",
		    __func__, var5_xfer->tx_len, var5_xfer->tx_data_len);
	if (q2spi_pkt->m_cmd_param == Q2SPI_TX_RX) {
		var5_xfer->rx_buf = q2spi->xfer->rx_buf;
		var5_xfer->rx_dma = q2spi->xfer->rx_dma;
		var5_xfer->tx_len = Q2SPI_HEADER_LEN;
		var5_xfer->rx_len =
			((q2spi_pkt->var5_pkt->dw_len_part1 |
			q2spi_pkt->var5_pkt->dw_len_part2 << 2) * 4) + 4;
		var5_xfer->rx_data_len = q2spi_pkt->data_length;
		Q2SPI_DEBUG(q2spi, "%s var5_pkt:%p cmd:%d flow_id:0x%x len_part1:%d len_part2:%d\n",
			    __func__, q2spi_pkt->var5_pkt, q2spi_pkt->var5_pkt->cmd,
			    q2spi_pkt->var5_pkt->flow_id, q2spi_pkt->var5_pkt->dw_len_part1,
			    q2spi_pkt->var5_pkt->dw_len_part2);
		Q2SPI_DEBUG(q2spi, "%s var5_pkt data_buf:0x%x var5_xfer->rx_len:%d\n",
			    __func__, q2spi_pkt->var5_pkt->data_buf, var5_xfer->rx_len);
	}
	Q2SPI_DEBUG(q2spi, "%s tx_buf:%p tx_dma:%p rx_buf:%p rx_dma:%p\n",
		    __func__, var5_xfer->tx_buf,
		    var5_xfer->tx_dma, var5_xfer->rx_buf, var5_xfer->rx_dma);
	q2spi_dump_ipc(q2spi, q2spi->ipc, "Preparing var5 tx_buf DMA TX",
		       (char *)var5_xfer->tx_buf, Q2SPI_HEADER_LEN);
	if (q2spi_pkt->m_cmd_param == Q2SPI_TX_ONLY) {
		q2spi_dump_ipc(q2spi, q2spi->ipc, "Preparing var5 data_buf DMA TX",
			       (void *)q2spi_pkt->var5_pkt->data_buf, var5_xfer->tx_data_len);
	}
	q2spi->xfer = var5_xfer;
	q2spi_pkt->xfer = var5_xfer;
	return 0;
}

/*
 * q2spi_prep_hrf_request - Prepare q2spi HRF type packet transfer
 * @q2spi: pointer to q2spi_geni
 * @q2spi_pkt: pointer to q2spi packet
 *
 * This function prepares HRF type transfer request to submit to gsi.
 *
 * Return: 0 on success. Error code on failure.
 */
static int q2spi_prep_hrf_request(struct q2spi_geni *q2spi, struct q2spi_packet *q2spi_pkt)
{
	struct q2spi_host_variant1_pkt *q2spi_hc_var1;
	struct q2spi_dma_transfer *var1_xfer = NULL;

	var1_xfer = q2spi_kzalloc(q2spi, sizeof(struct q2spi_dma_transfer), __LINE__);
	if (!var1_xfer) {
		Q2SPI_ERROR(q2spi, "%s Err var1_xfer alloc failed\n", __func__);
		return -ENOMEM;
	}

	q2spi_hc_var1 = q2spi_pkt->var1_pkt;
	var1_xfer->cmd = Q2SPI_TX_ONLY;
	var1_xfer->tx_buf = q2spi_pkt->var1_pkt;
	var1_xfer->tx_dma = q2spi_pkt->var1_tx_dma;
	var1_xfer->tx_data_len = 16;
	var1_xfer->tx_len = Q2SPI_HEADER_LEN + var1_xfer->tx_data_len;
	var1_xfer->tid = q2spi_pkt->var1_pkt->flow_id;
	var1_xfer->rx_buf = q2spi->rx_buf;
	var1_xfer->rx_len = RX_DMA_CR_BUF_SIZE;
	Q2SPI_DEBUG(q2spi, "%s var1_pkt:%p var1_pkt_phy:%p cmd:%d addr:0x%x flow_id:0x%x\n",
		    __func__, q2spi_pkt->var1_pkt, q2spi_pkt->var1_tx_dma, q2spi_pkt->var1_pkt->cmd,
		    q2spi_pkt->var1_pkt->reg_offset, q2spi_pkt->var1_pkt->flow_id);
	Q2SPI_DEBUG(q2spi, "%s var1_pkt: len:%d data_buf %p\n",
		    __func__, q2spi_pkt->var1_pkt->dw_len, q2spi_pkt->var1_pkt->data_buf);
	Q2SPI_DEBUG(q2spi, "%s tx_buf:%p tx_dma:%p rx_buf:%p rx_dma:%p\n",
		    __func__, var1_xfer->tx_buf, var1_xfer->tx_dma,
		    var1_xfer->rx_buf, var1_xfer->rx_dma);
	q2spi_dump_ipc(q2spi, q2spi->ipc, "Preparing var1_HRF DMA TX",
		       (char *)var1_xfer->tx_buf, var1_xfer->tx_len);
	q2spi->xfer = var1_xfer;
	q2spi_pkt->xfer = var1_xfer;
	return 0;
}

static int
q2spi_process_hrf_flow_after_lra(struct q2spi_geni *q2spi, struct q2spi_packet *q2spi_pkt)
{
	struct q2spi_cr_packet *q2spi_cr_pkt;
	unsigned long timeout = 0, xfer_timeout = 0;
	int ret = -1;

	Q2SPI_DEBUG(q2spi, "%s VAR1 wait for doorbell\n", __func__);
	/* Make sure we get the doorbell before continuing for HRF flow */
	xfer_timeout = msecs_to_jiffies(XFER_TIMEOUT_OFFSET);
	timeout = wait_for_completion_interruptible_timeout(&q2spi->doorbell_up, xfer_timeout);
	if (timeout <= 0) {
		Q2SPI_ERROR(q2spi, "%s Err timeout for doorbell_wait\n", __func__);
		return ret;
	}

	if (!list_empty(&q2spi->hc_cr_queue_list)) {
		q2spi_cr_pkt = list_first_entry(&q2spi->hc_cr_queue_list,
						struct q2spi_cr_packet, list);
		Q2SPI_DEBUG(q2spi, "%s list_del q2spi_cr_pkt:%p\n", __func__, q2spi_cr_pkt);
		if (q2spi_cr_pkt) {
			spin_lock(&q2spi->cr_queue_lock);
			list_del(&q2spi_cr_pkt->list);
			spin_unlock(&q2spi->cr_queue_lock);
		}
		q2spi_kfree(q2spi, q2spi_pkt->xfer, __LINE__);
	} else {
		Q2SPI_DEBUG(q2spi, "%s CR queue_list is empty\n", __func__);
		return ret;
	}

	Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p hrf_id:%d cr_id:%d\n", __func__,
		    q2spi_pkt, q2spi_pkt->hrf_flow_id, q2spi_cr_pkt->var3_pkt.flow_id);
	if (q2spi_pkt->hrf_flow_id == q2spi_cr_pkt->var3_pkt.flow_id) {
		q2spi_pkt->vtype = VARIANT_5;
		q2spi_kfree(q2spi, q2spi_cr_pkt, __LINE__);
		ret = q2spi_prep_var5_request(q2spi, q2spi_pkt);
		if (ret)
			return ret;
	}
	ret = q2spi_gsi_submit(q2spi_pkt);
	if (ret) {
		Q2SPI_ERROR(q2spi, "q2spi_gsi_submit failed: %d\n", ret);
		return ret;
	}
	return ret;
}

/**
 * __q2spi_send_messages - function which processes q2spi message queue
 * @q2spi: pointer to q2spi_geni
 *
 * This function checks if there is any message in the queue that
 * needs processing and if so call out to the driver to initialize hardware
 * and transfer each message.
 *
 * Return: 0 on success, else error code
 */
static int __q2spi_send_messages(struct q2spi_geni *q2spi)
{
	struct q2spi_packet *q2spi_pkt = NULL, *q2spi_pkt_tmp;
	int ret = 0;
	bool cm_flow_pkt = false;

	/* Check if the queue is idle */
	if (list_empty(&q2spi->tx_queue_list)) {
		Q2SPI_DEBUG(q2spi, "%s Tx queue list is empty\n", __func__);
		return 0;
	}

	/* Check if we need take a lock and frame the Q2SPI packet */
	/* if the list is not empty call q2spi_gsi_transfer msg to submit the transfer to GSI */
	mutex_lock(&q2spi->queue_lock);
	list_for_each_entry_safe(q2spi_pkt, q2spi_pkt_tmp, &q2spi->tx_queue_list, list) {
		if (list_empty(&q2spi->tx_queue_list)) {
			Q2SPI_DEBUG(q2spi, "%s: list_empty break\n", __func__);
			break;
		}
		if (q2spi_pkt->in_use) {
			Q2SPI_DEBUG(q2spi, "%s q2spi_pkt %p in use\n", __func__, q2spi_pkt);
			continue;
		}
		q2spi_pkt->in_use = IN_USE_TRUE;
		break;
	}
	mutex_unlock(&q2spi->queue_lock);
	Q2SPI_DEBUG(q2spi, "%s send q2spi_pkt %p\n", __func__, q2spi_pkt);
	if (!q2spi_pkt) {
		Q2SPI_DEBUG(q2spi, "%s q2spi_pkt is NULL\n", __func__);
		return -EAGAIN;
	}
	if (q2spi_pkt->vtype == VARIANT_1_LRA || q2spi_pkt->vtype == VARIANT_1_HRF)
		ret = q2spi_prep_var1_request(q2spi, q2spi_pkt);
	else if (q2spi_pkt->vtype == VARIANT_5)
		ret = q2spi_prep_var5_request(q2spi, q2spi_pkt);
	else if (q2spi_pkt->vtype == VARIANT_5_HRF)
		ret = q2spi_prep_hrf_request(q2spi, q2spi_pkt);
	else if (q2spi_pkt->vtype == VAR_SOFT_RESET)
		ret = q2spi_prep_soft_reset_request(q2spi, q2spi_pkt);

	if (ret)
		return ret;
	Q2SPI_DEBUG(q2spi, "%s q2spi->xfer:%p\n", __func__, q2spi->xfer);
	q2spi_pkt->q2spi = q2spi;
	if (q2spi_pkt->vtype == VARIANT_5) {
		if (q2spi_pkt->var5_pkt->flow_id >= Q2SPI_END_TID_ID) {
			cm_flow_pkt = true;
			Q2SPI_DEBUG(q2spi, "%s flow_id:%d\n", __func__,
				    q2spi_pkt->var5_pkt->flow_id);
		}
	}
	if (!cm_flow_pkt && atomic_read(&q2spi->doorbell_pending)) {
		atomic_inc(&q2spi->retry);
		Q2SPI_DEBUG(q2spi, "%s doorbell pending retry\n", __func__);
		complete(&q2spi->sync_wait);
		return -ETIMEDOUT;
	}
	ret = q2spi_gsi_submit(q2spi_pkt);
	if (ret) {
		Q2SPI_ERROR(q2spi, "q2spi_gsi_submit failed: %d\n", ret);
		return ret;
	}

	if (q2spi_pkt->vtype == VARIANT_5_HRF) {
		ret = q2spi_process_hrf_flow_after_lra(q2spi, q2spi_pkt);
		if (ret) {
			Q2SPI_ERROR(q2spi, "%s Err hrf_flow sma write fail ret %d\n",
				    __func__, ret);
			return ret;
		}
	}
	Q2SPI_DEBUG(q2spi, "%s: line:%d End\n", __func__, __LINE__);
	return 0;
}

/**
 * q2spi_send_messages - kthread work function which processes q2spi message queue
 * @work: pointer to kthread work struct contained in the controller struct
 *
 */
static void q2spi_send_messages(struct kthread_work *work)
{
	struct q2spi_geni *q2spi = container_of(work, struct q2spi_geni, send_messages);
	int ret = 0;

	ret = __q2spi_send_messages(q2spi);
	if (ret)
		Q2SPI_DEBUG(q2spi, "%s Err send message failure ret=%d\n", __func__, ret);
}

/**
 * q2spi_proto_init - Q2SPI protocol specific initialization
 * @q2spi: pointer to q2spi_geni driver data
 *
 * This function adds Q2SPi protocol specific configuration for
 * cs less mode.
 *
 * Return: 0 on success. Error code on failure.
 */
static int q2spi_proto_init(struct q2spi_geni *q2spi)
{
	u32 q2spi_tx_cfg = geni_read_reg(q2spi->base, SE_SPI_TRANS_CFG);
	u32 io3_sel = geni_read_reg(q2spi->base, GENI_CFG_REG80);
	u32 pre_post_dly = geni_read_reg(q2spi->base, SE_SPI_PRE_POST_CMD_DLY);
	u32 word_len = geni_read_reg(q2spi->base, SE_SPI_WORD_LEN);
	u32 spi_delay_reg = geni_read_reg(q2spi->base, SPI_DELAYS_COUNTERS);
	u32 se_geni_cfg_95 = geni_read_reg(q2spi->base, SE_GENI_CFG_REG95);
	u32 se_geni_cfg_103 = geni_read_reg(q2spi->base, SE_GENI_CFG_REG103);
	u32 se_geni_cfg_104 = geni_read_reg(q2spi->base, SE_GENI_CFG_REG104);
	int ret = 0;

	/* 3.2.2.10.1 Q2SPI Protocol Specific Configuration */
	/* Configure SE CLK */
	ret = q2spi_set_clock(q2spi, q2spi->max_speed_hz);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s set clock failed\n", __func__);
		return ret;
	}
	q2spi_tx_cfg &= ~SPI_NOT_USED_CFG1;
	geni_write_reg(q2spi_tx_cfg, q2spi->base, SE_SPI_TRANS_CFG);
	io3_sel &= ~IO_MACRO_IO3_SEL;
	geni_write_reg(io3_sel, q2spi->base, GENI_CFG_REG80);
	spi_delay_reg |= (SPI_CS_CLK_DLY << M_GP_CNT5_TE2D_SHIFT) & M_GP_CNT5_TE2D;
	spi_delay_reg |= (SPI_PIPE_DLY_TPM << M_GP_CNT6_CN_SHIFT) & M_GP_CNT6_CN;
	spi_delay_reg |= SPI_INTER_WORDS_DLY & M_GP_CNT4_TAN;
	geni_write_reg(spi_delay_reg, q2spi->base, SPI_DELAYS_COUNTERS);
	se_geni_cfg_95 |= M_GP_CNT7_TSN & M_GP_CNT7;
	geni_write_reg(se_geni_cfg_95, q2spi->base, SE_GENI_CFG_REG95);
	Q2SPI_DEBUG(q2spi, "tx_cfg: 0x%x io3_sel:0x%x spi_delay: 0x%x cfg_95:0x%x\n",
		    geni_read_reg(q2spi->base, SE_SPI_TRANS_CFG),
		    geni_read_reg(q2spi->base, GENI_CFG_REG80),
		    geni_read_reg(q2spi->base, SPI_DELAYS_COUNTERS),
		    geni_read_reg(q2spi->base, SE_GENI_CFG_REG95));
	se_geni_cfg_103 |= (S_GP_CNT5_TDN << S_GP_CNT5_SHIFT) & S_GP_CNT5;
	se_geni_cfg_104 |= S_GP_CNT7_SSN & S_GP_CNT7;
	geni_write_reg(se_geni_cfg_103, q2spi->base, SE_GENI_CFG_REG103);
	geni_write_reg(se_geni_cfg_104, q2spi->base, SE_GENI_CFG_REG104);

	word_len &= ~WORD_LEN_MSK;
	word_len |= MIN_WORD_LEN & WORD_LEN_MSK;
	geni_write_reg(word_len, q2spi->base, SE_SPI_WORD_LEN);
	Q2SPI_DEBUG(q2spi, "cfg_103: 0x%x cfg_104:0x%x pre_post_dly;0x%x spi_word_len:0x%x\n",
		    geni_read_reg(q2spi->base, SE_GENI_CFG_REG103),
		    geni_read_reg(q2spi->base, SE_GENI_CFG_REG104),
		    pre_post_dly, geni_read_reg(q2spi->base, SE_SPI_WORD_LEN));
	io3_sel &= ~OTHER_IO_OE;
	io3_sel |= (IO_MACRO_IO3_DATA_IN_SEL << IO_MACRO_IO3_DATA_IN_SEL_SHIFT) &
				IO_MACRO_IO3_DATA_IN_SEL_MASK;
	Q2SPI_DEBUG(q2spi, "io3_sel:0x%x %x TPM:0x%x %d\n", io3_sel,
		    (IO_MACRO_IO3_DATA_IN_SEL & IO_MACRO_IO3_DATA_IN_SEL_MASK),
		    SPI_PIPE_DLY_TPM, SPI_PIPE_DLY_TPM << M_GP_CNT6_CN_SHIFT);
	return 0;
}

/**
 * q2spi_geni_init - Qupv3 and SE initialization
 * @q2spi: pointer to q2spi_geni driver data
 *
 * This is done once per session. Make sure this api
 * is called before any actual transfer begins as it involves
 * generic SW/HW and Q2SPI protocol specific intializations
 * required for a q2spi transfer.
 *
 * Return: 0 on success. Error code on failure.
 */
static int q2spi_geni_init(struct q2spi_geni *q2spi)
{
	int proto = 0;
	unsigned int major, minor;
	int ver = 0, ret = 0;

	/* make sure to turn on the resources before this ex: pm_runtime_get_sync(q2spi->dev); */
	proto = geni_se_read_proto(&q2spi->se);
	if (proto != GENI_SE_Q2SPI) {
		Q2SPI_ERROR(q2spi, "Err Invalid proto %d\n", proto);
		return -EINVAL;
	}

	ver = geni_se_get_qup_hw_version(&q2spi->se);
	major = GENI_SE_VERSION_MAJOR(ver);
	minor = GENI_SE_VERSION_MINOR(ver);
	Q2SPI_DEBUG(q2spi, "%s ver:0x%x major:%d minor:%d\n", __func__, ver, major, minor);

	if (major == 1 && minor == 0)
		q2spi->oversampling = 2;
	else
		q2spi->oversampling = 1;

	/* Qupv3 Q2SPI protocol specific Initialization */
	q2spi_proto_init(q2spi);

	q2spi->gsi_mode = (geni_read_reg(q2spi->base, GENI_IF_DISABLE_RO) & FIFO_IF_DISABLE);
	if (q2spi->gsi_mode) {
		q2spi->xfer_mode = GENI_GPI_DMA;
		geni_se_select_mode(&q2spi->se, GENI_GPI_DMA);
		ret = q2spi_geni_gsi_setup(q2spi);
	} else {
		Q2SPI_DEBUG(q2spi, "%s: Err GSI mode not supported!\n", __func__);
		return -EINVAL;
	}
	Q2SPI_DEBUG(q2spi, "%s gsi_mode:%d xfer_mode:%d ret:%d\n",
		    __func__, q2spi->gsi_mode, q2spi->xfer_mode, ret);
	return ret;
}

/**
 * q2spi_geni_resources_off - turns off geni resources
 * @q2spi: pointer to q2spi_geni driver data
 *
 * Return: none
 */
void q2spi_geni_resources_off(struct q2spi_geni *q2spi)
{
	int ret = 0;

	if (!q2spi->resources_on) {
		Q2SPI_DEBUG(q2spi, "%s: Err Resources already off\n", __func__);
		return;
	}

	/* Set pinctrl state to sleep configuration */
	if (!IS_ERR_OR_NULL(q2spi->geni_gpio_sleep)) {
		ret = pinctrl_select_state(q2spi->geni_pinctrl, q2spi->geni_gpio_sleep);
		if (ret)
			Q2SPI_DEBUG(q2spi, "%s: Err failed to set pinctrl state to sleep, ret:%d\n",
				    __func__, ret);
	}

	/* Disable m_ahb, s_ahb and se clks */
	geni_se_common_clks_off(q2spi->se.clk, q2spi->m_ahb_clk, q2spi->s_ahb_clk);

	/* Disable icc */
	ret = geni_icc_disable(&q2spi->se);
	if (ret)
		Q2SPI_DEBUG(q2spi, "%s: Err icc disable failed, ret:%d\n", __func__, ret);

	q2spi->resources_on = false;
	Q2SPI_DEBUG(q2spi, "%s: ret:%d\n", __func__, ret);
}

/**
 * q2spi_geni_resources_on - turns on geni resources
 * @q2spi: pointer to q2spi_geni driver data
 *
 * Return: 0 on success. Error code on failure.
 */
int q2spi_geni_resources_on(struct q2spi_geni *q2spi)
{
	int ret = 0;

	if (q2spi->resources_on) {
		Q2SPI_DEBUG(q2spi, "%s: Err Resources already on\n", __func__);
		return ret;
	}

	ret = geni_icc_enable(&q2spi->se);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s: Err icc enable failed, ret:%d\n", __func__, ret);
		return ret;
	}

	ret = pinctrl_select_state(q2spi->geni_pinctrl,	q2spi->geni_gpio_active);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s: Err failed to pinctrl state to active, ret:%d\n",
			    __func__, ret);
		return ret;
	}

	/* Enable m_ahb, s_ahb and se clks */
	ret = geni_se_common_clks_on(q2spi->se.clk, q2spi->m_ahb_clk, q2spi->s_ahb_clk);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s: Err set common_clk_on failed, ret:%d\n", __func__, ret);
		return ret;
	}

	q2spi->resources_on = true;
	Q2SPI_DEBUG(q2spi, "%s: ret:%d\n", __func__, ret);

	return 0;
}

/**
 * q2spi_get_icc_pinctrl - Enable ICC voting and pinctrl
 * @pdev: pointer to Platform device
 * @q2spi: pointer to q2spi_geni driver data
 *
 * This function will enable icc paths and add bandwidth voting
 * and also get pinctrl state from DTSI.
 *
 * Return: 0 on success. Error code on failure.
 */
static int q2spi_get_icc_pinctrl(struct platform_device *pdev,
				 struct q2spi_geni *q2spi)
{
	struct geni_se *q2spi_rsc;
	int ret = 0;

	q2spi_rsc = &q2spi->se;
	/* ICC get */
	ret = geni_se_common_resources_init(q2spi_rsc,
					    Q2SPI_CORE2X_VOTE, APPS_PROC_TO_QUP_VOTE,
					    (DEFAULT_SE_CLK * DEFAULT_BUS_WIDTH));
	if (ret) {
		Q2SPI_DEBUG(q2spi, "Error geni_se_resources_init\n");
		goto get_icc_pinctrl_err;
	}
	Q2SPI_DEBUG(q2spi, "%s GENI_TO_CORE:%d CPU_TO_GENI:%d GENI_TO_DDR:%d\n",
		    __func__, q2spi_rsc->icc_paths[GENI_TO_CORE].avg_bw,
		    q2spi_rsc->icc_paths[CPU_TO_GENI].avg_bw,
		    q2spi_rsc->icc_paths[GENI_TO_DDR].avg_bw);

	/* call set_bw for once, then do icc_enable/disable */
	ret = geni_icc_set_bw(q2spi_rsc);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s icc set bw failed ret:%d\n", __func__, ret);
		goto get_icc_pinctrl_err;
	}

	q2spi->geni_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(q2spi->geni_pinctrl)) {
		Q2SPI_DEBUG(q2spi, "No pinctrl config specified!\n");
		ret = PTR_ERR(q2spi->geni_pinctrl);
		goto get_icc_pinctrl_err;
	}

	q2spi->geni_gpio_active = pinctrl_lookup_state(q2spi->geni_pinctrl, PINCTRL_DEFAULT);
	if (IS_ERR_OR_NULL(q2spi->geni_gpio_active)) {
		Q2SPI_DEBUG(q2spi, "No default config specified!\n");
		ret = PTR_ERR(q2spi->geni_gpio_active);
		goto get_icc_pinctrl_err;
	}

	q2spi->geni_gpio_sleep = pinctrl_lookup_state(q2spi->geni_pinctrl, PINCTRL_SLEEP);
	if (IS_ERR_OR_NULL(q2spi->geni_gpio_sleep)) {
		Q2SPI_DEBUG(q2spi, "No sleep config specified!\n");
		ret = PTR_ERR(q2spi->geni_gpio_sleep);
		goto get_icc_pinctrl_err;
	}

get_icc_pinctrl_err:
	return ret;
}

/**
 * q2spi_pinctrl_config - Does Pinctrl configuration
 * @pdev: pointer to Platform device
 * @q2spi: pointer to q2spi_geni driver data
 *
 * Return: 0 on success. Error code on failure.
 */
static int q2spi_pinctrl_config(struct platform_device *pdev, struct q2spi_geni *q2spi)
{
	int ret = 0;

	/* ICC and PINCTRL initialization */
	ret = q2spi_get_icc_pinctrl(pdev, q2spi);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "pinctrl get failed %d\n", ret);
		return ret;
	}

	return ret;
}

/**
 * q2spi_chardev_create - Allocate two character devices dinamically.
 * @pdev: pointer to Platform device
 * @q2spi: pointer to q2spi_geni driver data
 *
 * Allocates a range of char device numbers and adds a char
 * device to the system and creates a device and registers
 * it with sysfs.
 *
 * Return: 0 on success. Error code on failure.
 */
static int q2spi_chardev_create(struct q2spi_geni *q2spi)
{
	int ret = 0, i;

	ret = alloc_chrdev_region(&q2spi->chrdev.q2spi_dev, 0, MAX_DEV, "q2spidev");
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s ret:%d\n", __func__, ret);
		return ret;
	}
	q2spi_cdev_major = MAJOR(q2spi->chrdev.q2spi_dev);
	q2spi->chrdev.q2spi_class = class_create(THIS_MODULE, "q2spidev");
	if (IS_ERR(q2spi->chrdev.q2spi_class)) {
		Q2SPI_DEBUG(q2spi, "%s ret:%d\n", __func__, PTR_ERR(q2spi->chrdev.q2spi_class));
		ret = PTR_ERR(q2spi->chrdev.q2spi_class);
		goto err_class_create;
	}

	for (i = 0; i < MAX_DEV; i++) {
		cdev_init(&q2spi->chrdev.cdev[i], &q2spi_fops);
		q2spi->chrdev.cdev[i].owner = THIS_MODULE;
		q2spi->chrdev.major = q2spi_cdev_major;
		q2spi->chrdev.minor = i;
		ret = cdev_add(&q2spi->chrdev.cdev[i], MKDEV(q2spi_cdev_major, i), 1);
		if (ret) {
			Q2SPI_DEBUG(q2spi, "cdev_add failed ret:%d\n", ret);
			goto err_cdev_add;
		}

		if (i)
			q2spi->chrdev.class_dev = device_create(q2spi->chrdev.q2spi_class, NULL,
								MKDEV(q2spi_cdev_major, i),
								NULL, "q2spibt");
		else
			q2spi->chrdev.class_dev = device_create(q2spi->chrdev.q2spi_class, NULL,
								MKDEV(q2spi_cdev_major, i),
								NULL, "q2spiuwb");

		if (IS_ERR(q2spi->chrdev.class_dev)) {
			ret = PTR_ERR(q2spi->chrdev.class_dev);
			Q2SPI_DEBUG(q2spi, "failed to create device\n");
			goto err_dev_create;
		}
		Q2SPI_DEBUG(q2spi, "%s q2spi:%p chrdev:%p cdev:%p i:%d\n",
			    __func__, q2spi, q2spi->chrdev, q2spi->chrdev.cdev[i], i);
	}

	return 0;
err_dev_create:
	for (i = 0; i < MAX_DEV; i++)
		cdev_del(&q2spi->chrdev.cdev[i]);
err_cdev_add:
	class_destroy(q2spi->chrdev.q2spi_class);
err_class_create:
	unregister_chrdev_region(MKDEV(q2spi_cdev_major, 0), MINORMASK);
	return ret;
}

/**
 * q2spi_read_reg - read a register of host accesible client register
 * @q2spi: Pointer to main q2spi_geni structure.
 * @reg_offset: specifies register address of the client to be read.
 *
 * This function used to read register of a client specified.
 * It frame local register access command and submit to gsi and
 * wait for gsi completion.
 *
 * Return: 0 for success, negative number for error condition.
 */
int q2spi_read_reg(struct q2spi_geni *q2spi, int reg_offset)
{
	struct q2spi_packet *q2spi_pkt = NULL;
	struct q2spi_dma_transfer *xfer;
	struct q2spi_request q2spi_req;
	unsigned long timeout = 0, xfer_timeout = 0;
	int ret = 0;

	q2spi_req.cmd = LOCAL_REG_READ;
	q2spi_req.addr = reg_offset;
	q2spi_req.data_len = 4; /* In bytes */

	Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p &q2spi_pkt=%p\n", __func__, q2spi_pkt, &q2spi_pkt);
	ret = q2spi_frame_lra(q2spi, q2spi_req, &q2spi_pkt, VARIANT_1_LRA);
	Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p flow_id:%d\n", __func__, q2spi_pkt, ret);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "q2spi_frame_lra failed ret:%d\n", ret);
		return ret;
	}
	xfer = q2spi_kzalloc(q2spi, sizeof(struct q2spi_dma_transfer), __LINE__);
	if (!xfer) {
		Q2SPI_DEBUG(q2spi, "%s Err alloc failed\n", __func__);
		ret = -ENOMEM;
		return ret;
	}
	xfer->tx_buf = q2spi_pkt->var1_pkt;
	xfer->tx_dma = q2spi_pkt->var1_tx_dma;
	xfer->rx_buf = q2spi->xfer->rx_buf;
	xfer->rx_dma = q2spi->xfer->rx_dma;
	xfer->cmd = q2spi_pkt->m_cmd_param;
	Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p cmd:%d\n", __func__, q2spi_pkt, xfer->cmd);
	xfer->tx_data_len = q2spi_req.data_len;
	xfer->tx_len = Q2SPI_HEADER_LEN;
	xfer->rx_data_len = q2spi_req.data_len;
	xfer->rx_len = xfer->rx_data_len;
	xfer->tid = q2spi_pkt->var1_pkt->flow_id;
	reinit_completion(&q2spi->sync_wait);

	Q2SPI_DEBUG(q2spi, "%s tx_buf:%p tx_dma:%p rx_buf:%p rx_dma:%p tx_len:%d rx_len:%d\n",
		    __func__, xfer->tx_buf, xfer->tx_dma, xfer->rx_buf, xfer->rx_dma, xfer->tx_len,
		    xfer->rx_len);
	q2spi_dump_ipc(q2spi, q2spi->ipc, "q2spi read reg tx_buf DMA TX",
		       (char *)xfer->tx_buf, xfer->tx_len);
	q2spi->xfer = xfer;
	q2spi_pkt->q2spi = q2spi;

	ret = q2spi_gsi_submit(q2spi_pkt);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "Err q2spi_gsi_submit failed: %d\n", ret);
		return ret;
	}
	xfer_timeout = msecs_to_jiffies(XFER_TIMEOUT_OFFSET);
	timeout = wait_for_completion_interruptible_timeout(&q2spi->sync_wait, xfer_timeout);
	if (timeout <= 0) {
		Q2SPI_ERROR(q2spi, "%s Err timeout for sync_wait\n", __func__);
		return -ETIMEDOUT;
	}
	q2spi_free_xfer_tid(q2spi, q2spi->xfer->tid);
	Q2SPI_DEBUG(q2spi, "Reg:0x%x Read Val = 0x%x\n", reg_offset, *(unsigned int *)xfer->rx_buf);
	return ret;
}

/**
 * q2spi_write_reg - write a register of host accesible client register
 * @q2spi: Pointer to main q2spi_geni structure.
 * @reg_offset: specifies register address of the client to be write.
 * @data: spefies value of the register to be write.
 *
 * This function used to write to a register of a client specified.
 * It frame local register access command and submit to gsi and
 * wait for gsi completion.
 *
 * Return: 0 for success, negative number for error condition.
 */
static int q2spi_write_reg(struct q2spi_geni *q2spi, int reg_offset, unsigned long data)
{
	struct q2spi_packet *q2spi_pkt;
	struct q2spi_dma_transfer *xfer;
	struct q2spi_request q2spi_req;
	unsigned long timeout = 0, xfer_timeout = 0;
	int ret = 0;

	q2spi_req.cmd = LOCAL_REG_WRITE;
	q2spi_req.addr = reg_offset;
	q2spi_req.data_len = 4;
	q2spi_req.data_buff = &data;
	ret = q2spi_frame_lra(q2spi, q2spi_req, &q2spi_pkt, VARIANT_1_LRA);
	if (ret < 0) {
		Q2SPI_ERROR(q2spi, "%s Err q2spi_frame_lra failed ret:%d\n", __func__, ret);
		return ret;
	}
	Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p\n", __func__, q2spi_pkt);
	xfer = q2spi_kzalloc(q2spi, sizeof(struct q2spi_dma_transfer), __LINE__);
	if (!xfer) {
		Q2SPI_ERROR(q2spi, "%s Err xfer alloc failed\n", __func__);
		ret = -ENOMEM;
		return ret;
	}
	xfer->tx_buf = q2spi_pkt->var1_pkt;
	xfer->tx_dma = q2spi_pkt->var1_tx_dma;
	xfer->cmd = q2spi_pkt->m_cmd_param;
	xfer->tx_data_len = q2spi_req.data_len;
	xfer->tx_len = Q2SPI_HEADER_LEN + xfer->tx_data_len;
	xfer->tid = q2spi_pkt->var1_pkt->flow_id;

	Q2SPI_DEBUG(q2spi, "%s tx_buf:%p tx_dma:%p rx_buf:%p rx_dma:%p tx_len:%d rx_len:%d\n",
		    __func__, xfer->tx_buf, xfer->tx_dma, xfer->rx_buf, xfer->rx_dma, xfer->tx_len,
		    xfer->rx_len);
	Q2SPI_DEBUG(q2spi, "%s q2spi_pkt->var1_pkt:0x%x q2spi_pkt->var1_pkt_add:0x%x\n",
		    __func__, q2spi_pkt->var1_pkt, &q2spi_pkt->var1_pkt);
	q2spi_dump_ipc(q2spi, q2spi->ipc, "q2spi_read_reg tx_buf DMA TX",
		       (char *)xfer->tx_buf, xfer->tx_len);
	q2spi->xfer = xfer;
	q2spi_pkt->q2spi = q2spi;
	reinit_completion(&q2spi->sync_wait);
	ret = q2spi_gsi_submit(q2spi_pkt);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "q2spi_gsi_submit failed: %d\n", ret);
		return ret;
	}
	Q2SPI_DEBUG(q2spi, "wait here\n");
	xfer_timeout = msecs_to_jiffies(XFER_TIMEOUT_OFFSET);
	timeout = wait_for_completion_interruptible_timeout(&q2spi->sync_wait, xfer_timeout);
	if (timeout <= 0) {
		Q2SPI_DEBUG(q2spi, "%s Err timeout for sync_wait\n", __func__);
		return -ETIMEDOUT;
	}

	q2spi_free_xfer_tid(q2spi, q2spi->xfer->tid);
	Q2SPI_DEBUG(q2spi, "%s write to reg success ret:%d\n", __func__, ret);
	return ret;
}

/**
 * q2spi_slave_init - Initialization sequence
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * This function performs init sequence with q2spi slave
 * send host command to check client enabled or not
 * read Q2SPI_HOST_CFG.DOORBELL_EN register info from slave
 * Write 1 to each bit of Q2SPI_ERROR_EN to enable error interrupt to Host using doorbell.
 *
 * Return: 0 for success, negative number for error condition.
 */
static int q2spi_slave_init(struct q2spi_geni *q2spi)
{
	unsigned long scratch_data = 0xAAAAAAAA;
	unsigned long error_en_data = 0xFFFFFFFF;
	int ret = 0, value = 0;
	int retries = RETRIES;

	Q2SPI_DEBUG(q2spi, "%s reg:0x%x\n", __func__, Q2SPI_SCRATCH0);
	return 0;
	/* Dummy SCRATCH register write */
	ret = q2spi_write_reg(q2spi, Q2SPI_SCRATCH0, scratch_data);
	if (ret) {
		Q2SPI_ERROR(q2spi, "scratch0 write failed: %d\n", ret);
		return ret;
	}

	/* Dummy SCRATCH register read */
	Q2SPI_DEBUG(q2spi, "%s reg: 0x%x\n", __func__, Q2SPI_SCRATCH0);
	ret = q2spi_read_reg(q2spi, Q2SPI_SCRATCH0);
	if (ret) {
		Q2SPI_ERROR(q2spi, "Err scratch0 read failed: %d\n", ret);
		return ret;
	}

	/*
	 * Send dummy Host command until Client is enabled.
	 * Dummy command can be reading Q2SPI_HW_VERSION register.
	 */
	while (retries > 0 && value <= 0) {
		value = q2spi_read_reg(q2spi, Q2SPI_HW_VERSION);
		Q2SPI_DEBUG(q2spi, "%s retries:%d value:%d\n", __func__, retries, value);
		if (value <= 0)
			Q2SPI_DEBUG(q2spi, "HW_Version read failed: %d\n", ret);
		retries--;
		Q2SPI_DEBUG(q2spi, "%s retries:%d value:%d\n", __func__, retries, value);
	}

	Q2SPI_DEBUG(q2spi, "%s reg:0x%x\n", __func__, Q2SPI_HOST_CFG);
	ret = q2spi_read_reg(q2spi, Q2SPI_HOST_CFG);
	if (ret) {
		Q2SPI_ERROR(q2spi, "Err HOST CFG read failed: %d\n", ret);
		return ret;
	}

	Q2SPI_DEBUG(q2spi, "%s reg:0x%x\n", __func__, Q2SPI_ERROR_EN);
	ret = q2spi_write_reg(q2spi, Q2SPI_ERROR_EN, error_en_data);
	if (ret) {
		Q2SPI_ERROR(q2spi, "Err Error_en reg write failed: %d\n", ret);
		return ret;
	}

	Q2SPI_DEBUG(q2spi, "%s reg:0x%x\n", __func__, Q2SPI_ERROR_EN);
	ret = q2spi_read_reg(q2spi, Q2SPI_ERROR_EN);
	if (ret) {
		Q2SPI_ERROR(q2spi, "Err HOST CFG read failed: %d\n", ret);
		return ret;
	}
	return 0;
}

/**
 * q2spi_clks_get - get SE and AHB clks
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * This function will get clock resources for SE, M-AHB and S_AHB clocks.
 *
 * Return: 0 for success, negative number for error condition.
 */
static int q2spi_clks_get(struct q2spi_geni *q2spi)
{
	int ret = 0;

	q2spi->se_clk = devm_clk_get(q2spi->dev, "se-clk");
	if (ret) {
		Q2SPI_ERROR(q2spi, "Err getting SE clk %d\n", ret);
		return ret;
	}
	q2spi->se.clk = q2spi->se_clk;

	q2spi->m_ahb_clk = devm_clk_get(q2spi->dev->parent, "m-ahb");
	if (IS_ERR(q2spi->m_ahb_clk)) {
		ret = PTR_ERR(q2spi->m_ahb_clk);
		Q2SPI_ERROR(q2spi, "Err getting Main AHB clk %d\n", ret);
		return ret;
	}

	q2spi->s_ahb_clk = devm_clk_get(q2spi->dev->parent, "s-ahb");
	if (IS_ERR(q2spi->s_ahb_clk)) {
		ret = PTR_ERR(q2spi->s_ahb_clk);
		Q2SPI_ERROR(q2spi, "Err getting Secondary AHB clk %d\n", ret);
		return ret;
	}
	return 0;
}

int q2spi_send_system_mem_access(struct q2spi_geni *q2spi, struct q2spi_packet **q2spi_pkt)
{
	struct q2spi_request q2spi_req;
	struct q2spi_cr_packet *q2spi_cr_pkt = q2spi->cr_pkt;
	int ret;
	unsigned int dw_len;

	dw_len = (((q2spi_cr_pkt->var3_pkt.dw_len_part3 << 12) & 0xFF) |
				((q2spi_cr_pkt->var3_pkt.dw_len_part2 << 4) & 0xFF) |
				q2spi_cr_pkt->var3_pkt.dw_len_part1);
	q2spi_req.data_len = (dw_len * 4) + 4;
	Q2SPI_DEBUG(q2spi, "%s dw_len:%d data_len:%d\n", __func__, dw_len, q2spi_req.data_len);
	q2spi_req.cmd = DATA_READ;
	q2spi_req.addr = 0;
	q2spi_req.end_point = 0;
	q2spi_req.proto_ind = 0;
	q2spi_req.priority = 0;
	q2spi_req.flow_id = q2spi->cr_pkt->var3_pkt.flow_id;
	q2spi_req.sync = 0;
	mutex_lock(&q2spi->queue_lock);
	ret = q2spi_add_req_to_tx_queue(q2spi, q2spi_req, q2spi_pkt);
	Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p cr_q2spi_pkt:%p\n",
		    __func__, q2spi_pkt, q2spi_cr_pkt->q2spi_pkt);
	mutex_unlock(&q2spi->queue_lock);
	kthread_queue_work(q2spi->kworker, &q2spi->send_messages);
	Q2SPI_DEBUG(q2spi, "%s End %d\n", __func__, __LINE__);
	return ret;
}

/*
 * q2spi_handle_doorbell_work() - worker function which handles doorbell flow for q2spi
 *
 * @work: pointer to work_struct
 *
 * Return: None
 */
static void q2spi_handle_doorbell_work(struct work_struct *work)
{
	struct q2spi_geni *q2spi =
		container_of(work, struct q2spi_geni, q2spi_doorbell_work);
	struct q2spi_cr_packet *q2spi_cr_pkt = NULL;
	struct q2spi_packet *q2spi_pkt;
	unsigned long flags;
	int ret = 0, i = 0, no_of_crs = 0;
	u8 *ptr;
	bool wakeup_hrf = true, map_doorbell_rx_buf = true;

	Q2SPI_DEBUG(q2spi, "%s Enter PID=%d q2spi:%p\n", __func__, current->pid, q2spi);
	ret = q2spi_prepare_cr_pkt(q2spi);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "q2spi_prepare_cr_pkt failed %d\n", ret);
		return;
	}
	q2spi_cr_pkt = q2spi->cr_pkt;
	/* wait for RX dma channel TCE 0x22 to get CR body in RX DMA buffer */
	ret = check_gsi_transfer_completion_rx(q2spi);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s completion timeout: %d\n", __func__, ret);
		return;
	}

	no_of_crs = q2spi_cr_pkt->no_of_valid_crs;
	Q2SPI_DEBUG(q2spi, "%s q2spi_cr_pkt:%p q2spi_db_xfer:%p db_xfer_rx_buf:%p\n",
		    __func__, q2spi_cr_pkt, q2spi->db_xfer, q2spi->db_xfer->rx_buf);

	q2spi_cr_pkt->type = 0;
	ptr = (u8 *)q2spi->db_xfer->rx_buf;
	for (i = 0; i < no_of_crs; i++) {
		if (q2spi_cr_pkt->cr_hdr[i].cmd == BULK_ACCESS_STATUS) {
			q2spi_cr_pkt->bulk_pkt.cmd = q2spi_cr_pkt->cr_hdr[i].cmd;
			q2spi_cr_pkt->bulk_pkt.flow = q2spi_cr_pkt->cr_hdr[i].flow;
			q2spi_cr_pkt->bulk_pkt.parity = q2spi_cr_pkt->cr_hdr[i].parity;
			q2spi_dump_ipc(q2spi, q2spi->ipc, "DB BULK DMA RX",
				       (char *)ptr, q2spi->db_xfer->rx_len);
			q2spi_cr_pkt->bulk_pkt.status = ptr[0] & 0xF;
			q2spi_cr_pkt->bulk_pkt.flow_id = ptr[0] >> 4;
			ptr += CR_BULK_DATA_size;
			q2spi_cr_pkt->type |= (1 << (2 * i));
			Q2SPI_DEBUG(q2spi, "%s i:%d q2spi_cr_pkt->type:0x%x flow_id:%d\n",
				    __func__, i, q2spi_cr_pkt->type,
				    q2spi_cr_pkt->bulk_pkt.flow_id);
		} else if ((q2spi_cr_pkt->cr_hdr[i].cmd == ADDR_LESS_WR_ACCESS) ||
					(q2spi_cr_pkt->cr_hdr[i].cmd == ADDR_LESS_RD_ACCESS)) {
			memcpy((void *)&q2spi_cr_pkt->var3_pkt, (void *)ptr,
			       sizeof(struct q2spi_client_dma_pkt));
			q2spi_dump_ipc(q2spi, q2spi->ipc, "DB VAR3 DMA RX",
				       (char *)ptr, q2spi->db_xfer->rx_len);
			ptr += CR_DMA_DATA_size;
			q2spi_cr_pkt->type |= (2 << (2 * i));
			Q2SPI_DEBUG(q2spi, "%s i:%d q2spi_cr_pkt->type:0x%x\n",
				    __func__, i, q2spi_cr_pkt->type);
			Q2SPI_DEBUG(q2spi, "%s var3_pkt:%p var3_flow_id:%d\n",
				    __func__, q2spi_cr_pkt->var3_pkt,
				    q2spi_cr_pkt->var3_pkt.flow_id);
			Q2SPI_DEBUG(q2spi, "%s len_part1:%d len_part2:%d\n", __func__,
				    q2spi_cr_pkt->var3_pkt.dw_len_part1,
				    q2spi_cr_pkt->var3_pkt.dw_len_part2);
		}
	}

	q2spi_unmap_dma_buf_used(q2spi, (dma_addr_t)NULL, q2spi->db_xfer->rx_dma);
	q2spi->db_xfer->rx_dma = (dma_addr_t)NULL;
	q2spi_kfree(q2spi, q2spi->db_xfer->q2spi_pkt, __LINE__);

	for (i = 0; i < no_of_crs; i++) {
		Q2SPI_DEBUG(q2spi, "%s i=%d CR Header CMD 0x%x\n",
			    __func__, i, q2spi_cr_pkt->cr_hdr[i].cmd);
		if (q2spi_cr_pkt->cr_hdr[i].cmd == ADDR_LESS_WR_ACCESS ||
		    q2spi_cr_pkt->cr_hdr[i].cmd == ADDR_LESS_RD_ACCESS) {
			spin_lock_irqsave(&q2spi->cr_queue_lock, flags);
			if (q2spi_cr_pkt->cr_hdr[i].flow) {
				Q2SPI_DEBUG(q2spi,
					    "%s Add cr_pkt to cr_queue_list q2spi_cr_pkt:%p opcode:%d\n",
					    __func__, q2spi_cr_pkt, q2spi_cr_pkt->cr_hdr[i].cmd);
				list_add_tail(&q2spi_cr_pkt->list, &q2spi->cr_queue_list);
			} else {
				Q2SPI_DEBUG(q2spi,
					    "%s Add cr_pkt to hc_cr_queue_list q2spi_cr_pkt:%p opcode:%d\n",
					    __func__, q2spi_cr_pkt, q2spi_cr_pkt->cr_hdr[i].cmd);
				list_add_tail(&q2spi_cr_pkt->list, &q2spi->hc_cr_queue_list);
			}
			spin_unlock_irqrestore(&q2spi->cr_queue_lock, flags);

			if (q2spi_cr_pkt->cr_hdr[i].flow) {
				Q2SPI_DEBUG(q2spi, "%s len_part1:%d len_part2:%d len_part3:%d\n",
					    __func__, q2spi_cr_pkt->var3_pkt.dw_len_part1,
					    q2spi_cr_pkt->var3_pkt.dw_len_part2,
					    q2spi_cr_pkt->var3_pkt.dw_len_part3);
				q2spi_send_system_mem_access(q2spi, &q2spi_pkt);
				q2spi_cr_pkt->q2spi_pkt = q2spi_pkt;
				Q2SPI_DEBUG(q2spi, "%s q2spi_cr_pkt:%p cr->q2spi_pkt:%p\n",
					    __func__, q2spi_cr_pkt, q2spi_cr_pkt->q2spi_pkt);
				/*
				 * wait for RX dma channel TCE 0x22 to
				 * get CR body in RX DMA buffer
				 */
				ret = check_gsi_transfer_completion_rx(q2spi);
				if (ret)
					Q2SPI_DEBUG(q2spi, "%s completion timeout: %d\n",
						    __func__, ret);
			} else {
				if (q2spi_cr_pkt->cr_hdr[i].cmd ==
					ADDR_LESS_WR_ACCESS && wakeup_hrf) {
					/* wakeup HRF flow which is waiting for this CR doorbell */
					complete_all(&q2spi->doorbell_up);
					Q2SPI_DEBUG(q2spi, "%s cmd: %d Got doorbell CR Host flow\n",
						    __func__, q2spi_cr_pkt->cr_hdr[i].cmd);
					wakeup_hrf = false;
				}
			}
		} else if (q2spi_cr_pkt->cr_hdr[i].cmd == BULK_ACCESS_STATUS) {
			if (q2spi_cr_pkt->bulk_pkt.flow_id >= 0x8) {
				Q2SPI_DEBUG(q2spi, "%s Bulk status with Client Flow ID\n",
					    __func__);
				Q2SPI_DEBUG(q2spi, "%s q2spi_cr_pkt:%p cr->q2spi_pkt:%p\n",
					    __func__, q2spi_cr_pkt, q2spi_cr_pkt->q2spi_pkt);
				q2spi_notify_data_avail_for_client(q2spi);
				if (q2spi->port_release)
					map_doorbell_rx_buf = false;
			} else {
				Q2SPI_DEBUG(q2spi, "%s Bulk status with host Flow ID:%d\n",
					    __func__, q2spi_cr_pkt->bulk_pkt.flow_id);
				complete_all(&q2spi->sync_wait);
				if (no_of_crs == 1)
					q2spi_kfree(q2spi, q2spi_cr_pkt, __LINE__);
			}
		}

		if (map_doorbell_rx_buf) {
			/*
			 * get one rx buffer from allocated pool and
			 * map to gsi to ready for next doorbell.
			 */
			ret = q2spi_map_doorbell_rx_buf(q2spi);
			if (ret) {
				Q2SPI_DEBUG(q2spi, "failed to alloc RX DMA buf");
				return;
			}
		}
		if (atomic_read(&q2spi->doorbell_pending))
			atomic_dec(&q2spi->doorbell_pending);
	}
	Q2SPI_DEBUG(q2spi, "%s End PID=%d\n", __func__, current->pid);
}

/*
 * q2spi_chardev_destroy - Destroys character devices which are created as part of probe
 *
 * @q2spi: pointer to q2spi_geni driver data
 *
 * Return: None
 */
static void q2spi_chardev_destroy(struct q2spi_geni *q2spi)
{
	int i;

	for (i = 0; i < MAX_DEV; i++) {
		device_destroy(q2spi->chrdev.q2spi_class, MKDEV(q2spi_cdev_major, i));
		cdev_del(&q2spi->chrdev.cdev[i]);
	}
	class_destroy(q2spi->chrdev.q2spi_class);
	unregister_chrdev_region(MKDEV(q2spi_cdev_major, 0), MINORMASK);
	Q2SPI_DEBUG(q2spi, "%s End %d\n", __func__, q2spi_cdev_major);
}

/**
 * q2spi_geni_probe - Q2SPI interface driver probe function
 * @pdev: Q2SPI Serial Engine to probe.
 *
 * Allocates basic resources for QUPv3 SE which supports q2spi
 * and then register a range of char device numbers. Also
 * invoke methods for Qupv3 SE and Q2SPI protocol
 * specific Initialization.
 *
 * Return: 0 for success, negative number for error condition.
 */
static int q2spi_geni_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct q2spi_geni *q2spi;
	int ret = 0;

	pr_info("boot_kpi: M - DRIVER GENI_Q2SPI Init\n");

	q2spi = devm_kzalloc(dev, sizeof(*q2spi), GFP_KERNEL);
	if (!q2spi) {
		ret = -ENOMEM;
		goto q2spi_err;
	}

	q2spi->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Err getting IO region\n");
		ret = -EINVAL;
		goto q2spi_err;
	}

	q2spi->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(q2spi->base)) {
		ret = PTR_ERR(q2spi->base);
		dev_err(dev, "Err ioremap fail %d\n", ret);
		goto q2spi_err;
	}

	q2spi->irq = platform_get_irq(pdev, 0);
	if (q2spi->irq < 0) {
		dev_err(dev, "Err for irq get %d\n", ret);
		ret = q2spi->irq;
		goto q2spi_err;
	}

	irq_set_status_flags(q2spi->irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(dev, q2spi->irq, q2spi_geni_irq,
			       IRQF_TRIGGER_HIGH, dev_name(dev), q2spi);
	if (ret) {
		dev_err(dev, "Err Failed to request irq %d\n", ret);
		goto q2spi_err;
	}

	q2spi->se.dev = dev;
	q2spi->se.wrapper = dev_get_drvdata(dev->parent);
	if (!q2spi->se.wrapper) {
		dev_err(dev, "Err SE Wrapper is NULL, deferring probe\n");
		ret = -EPROBE_DEFER;
		goto q2spi_err;
	}

	q2spi->ipc = ipc_log_context_create(15, dev_name(dev), 0);
	if (!q2spi->ipc && IS_ENABLED(CONFIG_IPC_LOGGING))
		dev_err(dev, "Error creating IPC logs\n");

	q2spi->se.base = q2spi->base;
	if (q2spi_max_speed) {
		q2spi->max_speed_hz = q2spi_max_speed;
	} else {
		if (of_property_read_u32(pdev->dev.of_node, "q2spi-max-frequency",
				 &q2spi->max_speed_hz)) {
			Q2SPI_ERROR(q2spi, "Err Max frequency not specified\n");
			ret = -EINVAL;
			goto q2spi_err;
		}
	}

	q2spi->wrapper_dev = dev->parent;
	Q2SPI_DEBUG(q2spi, "%s q2spi:0x%p q2spi_cdev:0x%p w_dev:0x%p, dev:0x%p, p_dev:0x%p",
		    __func__, q2spi, q2spi->chrdev, q2spi->wrapper_dev, dev, &pdev->dev);
	Q2SPI_INFO(q2spi, "%s dev:%s q2spi_max_freq:%uhz\n",
		   __func__, dev_name(q2spi->dev), q2spi->max_speed_hz);

	ret = dma_set_mask_and_coherent(dev, (u64)DMA_BIT_MASK(48));
	if (ret) {
		Q2SPI_INFO(q2spi, "%s dma_set_mask_and_coherent with DMA_BIT_MASK(48) failed",
			   __func__);
		ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret) {
			Q2SPI_ERROR(q2spi, "Err could not set DMA mask\n");
			goto q2spi_err;
		}
	}
	ret = q2spi_chardev_create(q2spi);
	if (ret)
		goto q2spi_err;

	ret = q2spi_clks_get(q2spi);
	if (ret) {
		Q2SPI_ERROR(q2spi, "Err clks get failed\n");
		goto chardev_destroy;
	}

	ret = q2spi_pinctrl_config(pdev, q2spi);
	if (ret)
		goto chardev_destroy;

	ret = q2spi_geni_resources_on(q2spi);
	if (ret)
		goto chardev_destroy;

	ret = q2spi_geni_init(q2spi);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "Geni init failed %d\n", ret);
		goto resources_off;
	}

	init_waitqueue_head(&q2spi->readq);
	init_waitqueue_head(&q2spi->read_wq);
	INIT_LIST_HEAD(&q2spi->tx_queue_list);
	INIT_LIST_HEAD(&q2spi->rx_queue_list);
	INIT_LIST_HEAD(&q2spi->cr_queue_list);
	INIT_LIST_HEAD(&q2spi->hc_cr_queue_list);
	mutex_init(&q2spi->gsi_lock);
	spin_lock_init(&q2spi->txn_lock);
	mutex_init(&q2spi->queue_lock);
	spin_lock_init(&q2spi->cr_queue_lock);

	q2spi->kworker = kthread_create_worker(0, "kthread_q2spi");
	if (IS_ERR(q2spi->kworker)) {
		Q2SPI_ERROR(q2spi, "Err failed to create message pump kworker\n");
		ret = PTR_ERR(q2spi->kworker);
		q2spi->kworker = NULL;
		goto geni_deinit;
	}
	kthread_init_work(&q2spi->send_messages, q2spi_send_messages);
	init_completion(&q2spi->tx_cb);
	init_completion(&q2spi->rx_cb);
	init_completion(&q2spi->doorbell_up);
	init_completion(&q2spi->sync_wait);
	idr_init(&q2spi->tid_idr);

	/* Pre allocate buffers for transfers */
	ret = q2spi_pre_alloc_buffers(q2spi);
	if (ret) {
		Q2SPI_ERROR(q2spi, "Err failed to alloc buffers");
		goto destroy_worker;
	}
	q2spi->xfer = devm_kzalloc(q2spi->dev, sizeof(struct q2spi_dma_transfer), GFP_KERNEL);
	if (!q2spi->xfer) {
		Q2SPI_ERROR(q2spi, "Err failed to alloc xfer buffer");
		ret = -ENOMEM;
		goto free_buf;
	}

	q2spi->db_xfer = devm_kzalloc(q2spi->dev, sizeof(struct q2spi_dma_transfer), GFP_KERNEL);
	if (!q2spi->db_xfer) {
		ret = -ENOMEM;
		Q2SPI_ERROR(q2spi, "Err failed to alloc db_xfer buffer");
		goto free_buf;
	}

	q2spi->doorbell_wq = alloc_workqueue("%s", WQ_HIGHPRI, 1, dev_name(dev));
	if (!q2spi->doorbell_wq) {
		ret = -ENOMEM;
		Q2SPI_ERROR(q2spi, "Err failed to allocate workqueue");
		goto free_buf;
	}
	INIT_WORK(&q2spi->q2spi_doorbell_work, q2spi_handle_doorbell_work);

	dev_dbg(dev, "Q2SPI GENI SE Driver probed\n");

	platform_set_drvdata(pdev, q2spi);

	if (device_create_file(dev, &dev_attr_max_dump_size))
		Q2SPI_INFO(q2spi, "Unable to create device file for max_dump_size\n");
	q2spi->max_data_dump_size = Q2SPI_DATA_DUMP_SIZE;

	Q2SPI_INFO(q2spi, "%s Q2SPI GENI SE Driver probed\n", __func__);

	pr_info("boot_kpi: M - DRIVER GENI_Q2SPI Ready\n");

	return 0;
free_buf:
	q2spi_free_dma_buf(q2spi);
destroy_worker:
	idr_destroy(&q2spi->tid_idr);
	if (q2spi->kworker) {
		kthread_destroy_worker(q2spi->kworker);
		q2spi->kworker = NULL;
	}
geni_deinit:
	q2spi_geni_gsi_release(q2spi);
resources_off:
	q2spi_geni_resources_off(q2spi);
chardev_destroy:
	q2spi_chardev_destroy(q2spi);
q2spi_err:
	Q2SPI_ERROR(q2spi, "%s: failed, ret:%d\n", __func__, ret);
	q2spi->base = NULL;
	return ret;
}

static int q2spi_geni_remove(struct platform_device *pdev)
{
	struct q2spi_geni *q2spi = platform_get_drvdata(pdev);

	pr_info("%s q2spi=0x%p\n", __func__, q2spi);

	if (!q2spi || !q2spi->base)
		return 0;

	device_remove_file(&pdev->dev, &dev_attr_max_dump_size);

	destroy_workqueue(q2spi->doorbell_wq);

	q2spi_free_dma_buf(q2spi);

	idr_destroy(&q2spi->tid_idr);

	if (q2spi->kworker) {
		kthread_destroy_worker(q2spi->kworker);
		q2spi->kworker = NULL;
	}

	q2spi_geni_gsi_release(q2spi);

	q2spi_geni_resources_off(q2spi);

	q2spi_chardev_destroy(q2spi);

	if (q2spi->ipc)
		ipc_log_context_destroy(q2spi->ipc);
	return 0;
}

static int q2spi_geni_runtime_suspend(struct device *dev)
{
	pr_err("%s PID=%d\n", __func__, current->pid);
	return 0;
}

static int q2spi_geni_runtime_resume(struct device *dev)
{
	pr_err("%s PID=%d\n", __func__, current->pid);
	return 0;
}

static int q2spi_geni_resume(struct device *dev)
{
	pr_err("%s PID=%d\n", __func__, current->pid);
	return 0;
}

static int q2spi_geni_suspend(struct device *dev)
{
	pr_err("%s PID=%d\n", __func__, current->pid);
	return 0;
}

static const struct dev_pm_ops q2spi_geni_pm_ops = {
	SET_RUNTIME_PM_OPS(q2spi_geni_runtime_suspend,
			   q2spi_geni_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(q2spi_geni_suspend, q2spi_geni_resume)
};

static const struct of_device_id q2spi_geni_dt_match[] = {
	{ .compatible = "qcom,q2spi-msm-geni" },
	{}
};
MODULE_DEVICE_TABLE(of, q2spi_geni_dt_match);

static struct platform_driver q2spi_geni_driver = {
	.probe = q2spi_geni_probe,
	.remove = q2spi_geni_remove,
	.driver = {
		.name = "q2spi_msm_geni",
		.pm = &q2spi_geni_pm_ops,
		.of_match_table = q2spi_geni_dt_match,
	},
};

static int __init q2spi_dev_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&q2spi_geni_driver);
	if (ret)
		pr_err("register platform driver failed, ret [%d]\n", ret);

	pr_info("%s end ret:%d\n", __func__, ret);
	return ret;
}

static void __exit q2spi_dev_exit(void)
{
	pr_info("%s PID=%d\n", __func__, current->pid);
	platform_driver_unregister(&q2spi_geni_driver);
}

module_param(q2spi_max_speed, uint, 0644);
MODULE_PARM_DESC(q2spi_max_speed, "Maximum speed setting\n");

module_init(q2spi_dev_init);
module_exit(q2spi_dev_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:q2spi_geni");
