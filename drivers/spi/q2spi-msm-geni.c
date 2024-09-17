// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>
#include "q2spi-msm.h"
#include "q2spi-slave-reg.h"

#define CREATE_TRACE_POINTS
#include "q2spi-trace.h"

static int q2spi_slave_init(struct q2spi_geni *q2spi, bool slave_init);
static int q2spi_gsi_submit(struct q2spi_packet *q2spi_pkt);
static struct q2spi_geni *get_q2spi(struct device *dev);
static int q2spi_geni_runtime_resume(struct device *dev);

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
		Q2SPI_DBG_2(q2spi, "Allocated 0x%p at %d, count:%d\n",
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
	Q2SPI_DBG_2(q2spi, "Freeing 0x%p from %d, count:%d\n",
		    ptr, line, atomic_read(&q2spi->alloc_count));
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
 * q2spi_dump_ipc_always - Log dump function
 * @q2spi: Pointer to main q2spi_geni structure
 * @prefix: Prefix to use in log
 * @str: String to dump in log
 * @size: Size of data bytes per line
 *
 * Return: none
 */
void q2spi_dump_ipc_always(struct q2spi_geni *q2spi, char *prefix, char *str, int size)
{
	int offset = 0, total_bytes = size;

	if (!str) {
		Q2SPI_DEBUG(q2spi, "%s: Err str is NULL\n", __func__);
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

/**
 * q2spi_dump_ipc - Log dump function with log level
 * @q2spi: Pointer to main q2spi_geni structure
 * @prefix: Prefix to use in log
 * @str: String to dump in log
 * @size: Size of data bytes per line
 *
 * Return: none
 */
void q2spi_dump_ipc(struct q2spi_geni *q2spi, char *prefix, char *str, int size)
{
	if (q2spi->q2spi_log_lvl < 1)
		return;

	q2spi_dump_ipc_always(q2spi, prefix, str, size);
}

/*
 * log_level_show() - Prints the value stored in log_level sysfs entry
 *
 * @dev: pointer to device
 * @attr: device attributes
 * @buf: buffer to store the log_level value
 *
 * Return: prints q2spi log level value
 */
static ssize_t log_level_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct q2spi_geni *q2spi = get_q2spi(dev);

	return scnprintf(buf, sizeof(int), "%d\n", q2spi->q2spi_log_lvl);
}

/*
 * log_level_store() - store the q2spi log_level sysfs value
 *
 * @dev: pointer to device
 * @attr: device attributes
 * @buf: buffer which contains the log_level in string format
 * @size: returns the value of size
 *
 * Return: Size copied in the buffer
 */
static ssize_t log_level_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t size)
{
	struct q2spi_geni *q2spi = get_q2spi(dev);

	if (kstrtoint(buf, 0, &q2spi->q2spi_log_lvl)) {
		dev_err(dev, "%s Invalid input\n", __func__);
		return -EINVAL;
	}

	return size;
}

static DEVICE_ATTR_RW(log_level);

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
	struct q2spi_geni *q2spi = get_q2spi(dev);
	return scnprintf(buf, sizeof(int), "%d\n", q2spi->max_data_dump_size);
}

/*
 * max_dump_size_store() - store the max_dump_size sysfs value
 *
 * @dev: pointer to device
 * @attr: device attributes
 * @buf: buffer which contains the max_dump_size in string format
 * @size: returns the value of size
 *
 * Return: Size copied in the buffer
 */
static ssize_t max_dump_size_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct q2spi_geni *q2spi = get_q2spi(dev);

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
 * q2spi_pkt_state - Returns q2spi packet state in string format
 * @q2spi_pkt: Pointer to q2spi_packet
 *
 * Return: q2spi packet state in string format
 */
const char *q2spi_pkt_state(struct q2spi_packet *q2spi_pkt)
{
	if (q2spi_pkt->state == NOT_IN_USE)
		return "NOT IN USE";
	else if (q2spi_pkt->state == IN_USE)
		return "IN_USE";
	else if (q2spi_pkt->state == DATA_AVAIL)
		return "DATA_AVAIL";
	else if (q2spi_pkt->state == IN_DELETION)
		return "IN_DELETION";
	else if (q2spi_pkt->state == DELETED)
		return "DELETED";
	else
		return "ERR UNKNOWN STATE";
}

/**
 * q2spi_tx_queue_status - Logs tx_queue list status empty/not-empty
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Return: None
 */
void q2spi_tx_queue_status(struct q2spi_geni *q2spi)
{
	if (list_empty(&q2spi->tx_queue_list))
		Q2SPI_DBG_2(q2spi, "%s tx_queue empty\n", __func__);
	else
		Q2SPI_DBG_2(q2spi, "%s tx_queue not empty!\n", __func__);
}

/**
 * q2spi_free_q2spi_pkt - Deallocates the q2spi_pkt
 * @q2spi_pkt: Pointer to q2spi_pkt to be deleted
 *
 * Return: None
 */
void q2spi_free_q2spi_pkt(struct q2spi_packet *q2spi_pkt, int line)
{
	if (q2spi_pkt->xfer) {
		Q2SPI_DBG_2(q2spi_pkt->q2spi, "%s q2spi_pkt=%p q2spi_pkt->xfer=%p\n",
			    __func__, q2spi_pkt, q2spi_pkt->xfer);
		q2spi_kfree(q2spi_pkt->q2spi, q2spi_pkt->xfer, line);
		q2spi_kfree(q2spi_pkt->q2spi, q2spi_pkt, line);
		q2spi_pkt = NULL;
	}
}

/**
 * q2spi_alloc_q2spi_pkt - Allocates memory for q2spi_pkt
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Return: Upon successful memory allocation returns pointer of q2spi_pkt, else NULL
 */
struct q2spi_packet *q2spi_alloc_q2spi_pkt(struct q2spi_geni *q2spi, int line)
{
	struct q2spi_packet *q2spi_pkt = q2spi_kzalloc(q2spi, sizeof(struct q2spi_packet), line);

	if (!q2spi_pkt) {
		Q2SPI_ERROR(q2spi, "%s Err q2spi_pkt alloc fail\n", __func__);
		return NULL;
	}
	q2spi_pkt->xfer = q2spi_kzalloc(q2spi, sizeof(struct q2spi_dma_transfer), line);
	if (!q2spi_pkt->xfer) {
		Q2SPI_ERROR(q2spi, "%s Err xfer alloc failed\n", __func__);
		q2spi_kfree(q2spi, q2spi_pkt, __LINE__);
		q2spi_pkt = NULL;
		return NULL;
	}
	Q2SPI_DBG_2(q2spi, "%s q2spi_pkt=%p PID=%d\n", __func__, q2spi_pkt, current->pid);
	init_completion(&q2spi_pkt->bulk_wait);
	init_completion(&q2spi_pkt->wait_for_db);
	q2spi_pkt->q2spi = q2spi;
	return q2spi_pkt;
}

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
 * q2spi_free_resp_buf - free resp buffers from pool
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * free response dma mapped buffers allocated by q2spi_pre_alloc_buffers api.
 *
 * Return: 0 for success, negative number if buffer is not found
 */
static int q2spi_free_resp_buf(struct q2spi_geni *q2spi)
{
	void *buf;
	dma_addr_t dma_addr;
	int i;
	size_t size;

	for (i = 0; i < Q2SPI_MAX_RESP_BUF; i++) {
		if (!q2spi->resp_buf[i])
			continue;
		if (q2spi->resp_buf_used[i])
			return -1;
		buf = q2spi->resp_buf[i];
		dma_addr = q2spi->resp_dma_buf[i];
		size = Q2SPI_RESP_BUF_SIZE;
		geni_se_common_iommu_free_buf(q2spi->wrapper_dev, &dma_addr, buf, size);
	}
	return 0;
}

/**
 * q2spi_free_dma_buf - free preallocated dma mapped buffers
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * free dma mapped buffers allocated by q2spi_pre_alloc_buffers api.
 *
 * Return: None
 */
static void q2spi_free_dma_buf(struct q2spi_geni *q2spi)
{
	if (q2spi_free_bulk_buf(q2spi))
		Q2SPI_DBG_1(q2spi, "%s Err free bulk buf fail\n", __func__);

	if (q2spi_free_cr_buf(q2spi))
		Q2SPI_DBG_1(q2spi, "%s Err free cr buf fail\n", __func__);

	if (q2spi_free_var5_buf(q2spi))
		Q2SPI_DBG_1(q2spi, "%s Err free var5 buf fail\n", __func__);

	if (q2spi_free_var1_buf(q2spi))
		Q2SPI_DBG_1(q2spi, "%s Err free var1 buf fail\n", __func__);

	if (q2spi_free_resp_buf(q2spi))
		Q2SPI_DBG_1(q2spi, "%s Err free resp buf fail\n", __func__);
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

		q2spi->bulk_buf[i] =
			geni_se_common_iommu_alloc_buf(q2spi->wrapper_dev, &q2spi->bulk_dma_buf[i],
						       sizeof(struct
						       q2spi_client_bulk_access_pkt));
		if (IS_ERR_OR_NULL(q2spi->bulk_buf[i])) {
			Q2SPI_ERROR(q2spi, "%s Err bulk buf alloc fail\n", __func__);
			goto exit_dealloc;
		}

		Q2SPI_DBG_2(q2spi, "%s var1_buf[%d] virt:%p phy:%p\n", __func__, i,
			    (void *)q2spi->var1_buf[i], (void *)q2spi->var1_dma_buf[i]);
		Q2SPI_DBG_2(q2spi, "%s var5_buf[%d] virt:%p phy:%p\n", __func__, i,
			    (void *)q2spi->var5_buf[i], (void *)q2spi->var5_dma_buf[i]);
		Q2SPI_DBG_2(q2spi, "%s cr_buf[%d] virt:%p phy:%p\n", __func__, i,
			    (void *)q2spi->cr_buf[i], (void *)q2spi->cr_dma_buf[i]);
		Q2SPI_DBG_2(q2spi, "%s bulk_buf[%d] virt:%p phy:%p\n", __func__, i,
			    (void *)q2spi->bulk_buf[i], (void *)q2spi->bulk_dma_buf[i]);
	}

	for (i = 0; i < Q2SPI_MAX_RESP_BUF; i++) {
		q2spi->resp_buf[i] =
			geni_se_common_iommu_alloc_buf(q2spi->wrapper_dev, &q2spi->resp_dma_buf[i],
						       Q2SPI_RESP_BUF_SIZE);
		if (IS_ERR_OR_NULL(q2spi->resp_buf[i])) {
			Q2SPI_ERROR(q2spi, "%s Err resp buf alloc fail\n", __func__);
			goto exit_dealloc;
		}

		Q2SPI_DBG_2(q2spi, "%s resp_buf[%d] virt:%p phy:%p\n", __func__, i,
			    (void *)q2spi->resp_buf[i], (void *)q2spi->resp_dma_buf[i]);
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
	bool unmapped = false;

	if (!tx_dma && !rx_dma) {
		Q2SPI_ERROR(q2spi, "%s Err TX/RX dma buffer NULL\n", __func__);
		return;
	}

	Q2SPI_DBG_2(q2spi, "%s PID:%d for tx_dma:%p rx_dma:%p\n", __func__,
		    current->pid, (void *)tx_dma, (void *)rx_dma);

	for (i = 0; i < Q2SPI_MAX_BUF; i++) {
		if (tx_dma == q2spi->var1_dma_buf[i]) {
			if (q2spi->var1_buf_used[i]) {
				Q2SPI_DBG_2(q2spi, "%s UNMAP var1_buf[%d] virt:%p phy:%p\n",
					    __func__, i, (void *)q2spi->var1_buf[i],
					    (void *)q2spi->var1_dma_buf[i]);
				q2spi->var1_buf_used[i] = NULL;
				unmapped = true;
			}
		} else if (tx_dma == q2spi->var5_dma_buf[i]) {
			if (q2spi->var5_buf_used[i]) {
				Q2SPI_DBG_2(q2spi, "%s UNMAP var5_buf[%d] virt:%p phy:%p\n",
					    __func__, i, (void *)q2spi->var5_buf[i],
					    (void *)q2spi->var5_dma_buf[i]);
				q2spi->var5_buf_used[i] = NULL;
				unmapped = true;
			}
		}
		if (rx_dma == q2spi->cr_dma_buf[i]) {
			if (q2spi->cr_buf_used[i]) {
				Q2SPI_DBG_2(q2spi, "%s UNMAP cr_buf[%d] virt:%p phy:%p\n",
					    __func__, i, (void *)q2spi->cr_buf[i],
					    (void *)q2spi->cr_dma_buf[i]);
				q2spi->cr_buf_used[i] = NULL;
				unmapped = true;
			}
		} else if (rx_dma == q2spi->bulk_dma_buf[i]) {
			if (q2spi->bulk_buf_used[i]) {
				Q2SPI_DBG_2(q2spi, "%s UNMAP bulk_buf[%d] virt:%p phy:%p\n",
					    __func__, i, (void *)q2spi->bulk_buf[i],
					    (void *)q2spi->bulk_dma_buf[i]);
				q2spi->bulk_buf_used[i] = NULL;
				unmapped = true;
			}
		}
	}
	if (!unmapped)
		Q2SPI_DBG_2(q2spi, "%s PID:%d Err unmap fail for tx_dma:%p rx_dma:%p\n",
			    __func__, current->pid, (void *)tx_dma, (void *)rx_dma);
	Q2SPI_DBG_2(q2spi, "%s End PID=%d\n", __func__, current->pid);
}

/**
 * q2spi_unmap_var_bufs - function which checks for q2spi variant type and
 * unmap the buffers
 * @q2spi: pointer to q2spi_geni
 * @q2spi_packet: pointer to q2spi_packet
 *
 * Return: None
 */
void q2spi_unmap_var_bufs(struct q2spi_geni *q2spi, struct q2spi_packet *q2spi_pkt)
{
	if (q2spi_pkt->vtype == VARIANT_1_LRA || q2spi_pkt->vtype == VARIANT_1_HRF) {
		Q2SPI_DBG_1(q2spi, "%s Unmapping Var1 buffers..\n", __func__);
		q2spi_unmap_dma_buf_used(q2spi, q2spi_pkt->var1_tx_dma,
					 q2spi_pkt->var1_rx_dma);
	} else if (q2spi_pkt->vtype == VARIANT_5) {
		Q2SPI_DBG_1(q2spi, "%s Unmapping Var5 buffers..\n", __func__);
		q2spi_unmap_dma_buf_used(q2spi, q2spi_pkt->var5_tx_dma,
					 q2spi_pkt->var5_rx_dma);
	} else if (q2spi_pkt->vtype == VARIANT_5_HRF) {
		Q2SPI_DBG_1(q2spi, "%s Unmapping Var1 and Var5 buffers..\n", __func__);
		q2spi_unmap_dma_buf_used(q2spi, q2spi_pkt->var1_tx_dma,
					 (dma_addr_t)NULL);
		q2spi_unmap_dma_buf_used(q2spi, q2spi_pkt->var5_tx_dma,
					 q2spi_pkt->var5_rx_dma);
	}
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
	int i;

	/* Pick rx buffers from pre allocated pool */
	for (i = 0; i < Q2SPI_MAX_BUF; i++) {
		if (!q2spi->cr_buf_used[i]) {
			xfer->rx_buf = q2spi->cr_buf[i];
			xfer->rx_dma = q2spi->cr_dma_buf[i];
			q2spi->cr_buf_used[i] = q2spi->cr_buf[i];
			q2spi->rx_buf = xfer->rx_buf;
			Q2SPI_DBG_2(q2spi, "ALLOC %s db xfer:%p rx_buf:%p rx_dma:%p\n",
				    __func__, q2spi->db_xfer, xfer->rx_buf, (void *)xfer->rx_dma);
			memset(xfer->rx_buf, 0xdb, RX_DMA_CR_BUF_SIZE);
			return 0;
		}
	}

	Q2SPI_DEBUG(q2spi, "%s Err DB RX dma alloc failed\n", __func__);
	return -ENOMEM;
}

/**
 * q2spi_unmap_rx_buf - release RX DMA buffers
 * @q2spi_pkt: Pointer to q2spi packet
 *
 * This function will release rx buffers back to preallocated pool
 *
 * Return: None
 */
static void q2spi_unmap_rx_buf(struct q2spi_packet *q2spi_pkt)
{
	struct q2spi_dma_transfer *xfer = q2spi_pkt->xfer;
	struct q2spi_geni *q2spi = q2spi_pkt->q2spi;
	int i = 0;
	bool unmapped = false;

	if (!xfer->rx_buf || !xfer->rx_dma) {
		Q2SPI_DEBUG(q2spi, "%s Err RX buffer NULL\n", __func__);
		return;
	}

	Q2SPI_DBG_1(q2spi, "%s PID:%d rx_buf %p %p\n", __func__,
		    current->pid, (void *)xfer->rx_buf, (void *)xfer->rx_dma);

	for (i = 0; i < Q2SPI_MAX_RESP_BUF; i++) {
		if (xfer->rx_dma == q2spi->resp_dma_buf[i]) {
			if (q2spi->resp_buf_used[i]) {
				Q2SPI_DBG_1(q2spi, "%s UNMAP rx_buf[%d] virt:%p phy:%p\n",
					    __func__, i, (void *)q2spi->resp_buf[i],
					    (void *)q2spi->resp_dma_buf[i]);
				q2spi->resp_buf_used[i] = NULL;
				unmapped = true;
			}
		}
	}
	if (!unmapped)
		Q2SPI_DBG_1(q2spi, "%s PID:%d Err unmap fail for rx_dma:%p\n",
			    __func__, current->pid, (void *)xfer->rx_dma);
	Q2SPI_DBG_2(q2spi, "%s End PID=%d\n", __func__, current->pid);
}

/**
 * q2spi_get_rx_buf - obtain RX DMA buffer from preallocated pool
 * @q2spi_pkt: Pointer to q2spi packet
 * @len: size of the memory to be allocate
 *
 * This function will allocate RX dma_alloc_coherant memory
 * of the length specified. This RX buffer is used to
 * receive rx data from slave.
 *
 * Return: 0 for success, negative number for error condition.
 */
static int q2spi_get_rx_buf(struct q2spi_packet *q2spi_pkt, int len)
{
	struct q2spi_geni *q2spi = q2spi_pkt->q2spi;
	struct q2spi_dma_transfer *xfer = q2spi_pkt->xfer;
	int i;

	if (!len) {
		Q2SPI_DEBUG(q2spi, "%s Err Zero length for alloc\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < Q2SPI_MAX_RESP_BUF; i++) {
		if (!q2spi->resp_buf_used[i]) {
			q2spi->resp_buf_used[i] = q2spi->resp_buf[i];
			xfer->rx_buf = q2spi->resp_buf[i];
			xfer->rx_dma = q2spi->resp_dma_buf[i];
			memset(xfer->rx_buf, 0xba, Q2SPI_RESP_BUF_SIZE);
			Q2SPI_DBG_1(q2spi, "%s ALLOC rx buf %p dma_buf:%p len:%d\n",
				    __func__, (void *)q2spi->resp_buf[i],
				    (void *)q2spi->resp_dma_buf[i], len);
			return 0;
		}
	}
	Q2SPI_DEBUG(q2spi, "%s: Err short of RX dma buffers\n", __func__);
	return -ENOMEM;
}

static int q2spi_hrf_entry_format_sleep(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req,
					struct q2spi_request **q2spi_hrf_req_ptr)
{
	struct q2spi_request *q2spi_hrf_req = NULL;
	struct q2spi_mc_hrf_entry hrf_entry = {0};

	q2spi_hrf_req = q2spi_kzalloc(q2spi, sizeof(struct q2spi_request), __LINE__);
	if (!q2spi_hrf_req) {
		Q2SPI_ERROR(q2spi, "%s Err alloc hrf req failed\n", __func__);
		return -ENOMEM;
	}
	q2spi_hrf_req->data_buff =
		q2spi_kzalloc(q2spi, sizeof(struct q2spi_mc_hrf_entry), __LINE__);

	if (!q2spi_hrf_req->data_buff) {
		Q2SPI_ERROR(q2spi, "%s Err alloc hrf data_buff failed\n", __func__);
		q2spi_kfree(q2spi, q2spi_hrf_req, __LINE__);
		return -ENOMEM;
	}
	*q2spi_hrf_req_ptr = q2spi_hrf_req;
	hrf_entry.cmd = Q2SPI_SLEEP_OPCODE;
	hrf_entry.parity = 0;
	hrf_entry.arg1 = Q2SPI_CLIENT_SLEEP_BYTE;
	hrf_entry.arg2 = 0;
	hrf_entry.arg3 = 0;
	q2spi_hrf_req->addr = Q2SPI_HRF_PUSH_ADDRESS;
	q2spi_hrf_req->data_len = HRF_ENTRY_DATA_LEN;
	q2spi_hrf_req->sync = 1;
	q2spi_hrf_req->priority = 1;
	q2spi_hrf_req->cmd = LOCAL_REG_WRITE;
	memcpy(q2spi_hrf_req->data_buff, &hrf_entry, sizeof(struct q2spi_mc_hrf_entry));

	Q2SPI_DBG_2(q2spi, "%s End q2spi_hrf_req:%p\n", __func__, q2spi_hrf_req);
	return 0;
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
	struct q2spi_mc_hrf_entry hrf_entry = {0};
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
		Q2SPI_DEBUG(q2spi, "%s Err failed to alloc flow_id", __func__);
		return -EINVAL;
	}
	hrf_entry.flow_id = flow_id;
	Q2SPI_DBG_1(q2spi, "%s flow_id:%d len:%d", __func__, hrf_entry.flow_id, q2spi_req.data_len);
	if (q2spi_req.data_len % 4) {
		hrf_entry.dwlen_part1 = (q2spi_req.data_len / 4) & 0xF;
		hrf_entry.dwlen_part2 = ((q2spi_req.data_len / 4) >> 4) & 0xFF;
		hrf_entry.dwlen_part3 = ((q2spi_req.data_len / 4) >> 12) & 0xFF;
	} else {
		hrf_entry.dwlen_part1 = (q2spi_req.data_len / 4 - 1) & 0xF;
		hrf_entry.dwlen_part2 = ((q2spi_req.data_len / 4 - 1) >> 4) & 0xFF;
		hrf_entry.dwlen_part3 = ((q2spi_req.data_len / 4 - 1) >> 12) & 0xFF;
	}
	Q2SPI_DBG_2(q2spi, "%s hrf_entry dwlen part1:%d part2:%d part3:%d\n",
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
	return 0;
}

/**
 * q2spi_wait_for_doorbell_setup_ready - wait for doorbell buffers are queued to hw
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Return: none
 */
void q2spi_wait_for_doorbell_setup_ready(struct q2spi_geni *q2spi)
{
	long timeout = 0;

	if (!q2spi->doorbell_setup) {
		Q2SPI_DBG_1(q2spi, "%s: Waiting for Doorbell buffers to be setup\n", __func__);
		reinit_completion(&q2spi->db_setup_wait);
		timeout = wait_for_completion_interruptible_timeout(&q2spi->db_setup_wait,
								    msecs_to_jiffies(50));
		if (timeout <= 0) {
			Q2SPI_DEBUG(q2spi, "%s Err timeout for DB buffers setup wait:%ld\n",
				    __func__, timeout);
			if (timeout == -ERESTARTSYS)
				q2spi_sys_restart = true;
		}
	}
}

/**
 * q2spi_unmap_doorbell_rx_buf - unmap rx dma buffer mapped by q2spi_map_doorbell_rx_buf
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Return: none
 */
void q2spi_unmap_doorbell_rx_buf(struct q2spi_geni *q2spi)
{
	if (!q2spi->db_xfer->rx_dma) {
		Q2SPI_DEBUG(q2spi, "%s Doorbell DMA buffer already unmapped\n", __func__);
		return;
	}
	q2spi_unmap_dma_buf_used(q2spi, (dma_addr_t)NULL, q2spi->db_xfer->rx_dma);
	q2spi->db_xfer->rx_dma = (dma_addr_t)NULL;
	q2spi->doorbell_setup = false;
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

	Q2SPI_DBG_1(q2spi, "%s Enter PID=%d\n", __func__, current->pid);
	if (q2spi_sys_restart)
		return -ERESTARTSYS;

	if (q2spi->port_release || atomic_read(&q2spi->is_suspend)) {
		Q2SPI_DBG_1(q2spi, "%s Port being closed or suspend return\n", __func__);
		return 0;
	}
	if (q2spi->db_xfer->rx_dma) {
		Q2SPI_DBG_1(q2spi, "%s Doorbell buffer already mapped\n", __func__);
		return 0;
	}

	memset(q2spi->db_q2spi_pkt, 0x00, sizeof(struct q2spi_packet));
	q2spi_pkt = q2spi->db_q2spi_pkt;
	q2spi_pkt->q2spi = q2spi;
	q2spi_pkt->m_cmd_param = Q2SPI_RX_ONLY;
	memset(q2spi->db_xfer, 0, sizeof(struct q2spi_dma_transfer));

	/* RX DMA buffer allocated to map to GSI to Receive Doorbell */
	/* Alloc RX DMA buf and map to gsi so that SW can receive Doorbell */
	ret = q2spi_get_doorbell_rx_buf(q2spi);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s Err failed to alloc RX DMA buf", __func__);
		return ret;
	}

	/* Map RX DMA descriptor on RX channel */
	q2spi->db_xfer->cmd = Q2SPI_RX_ONLY;
	q2spi->db_xfer->rx_data_len = RX_DMA_CR_BUF_SIZE; /* 96 byte for 4 crs in doorbell */
	q2spi->db_xfer->rx_len = RX_DMA_CR_BUF_SIZE;
	q2spi->db_xfer->q2spi_pkt = q2spi_pkt;
	q2spi_pkt->q2spi = q2spi;
	Q2SPI_DBG_2(q2spi, "%s PID=%d wait for gsi_lock\n", __func__, current->pid);
	mutex_lock(&q2spi->gsi_lock);
	Q2SPI_DBG_2(q2spi, "%s PID=%d acquired gsi_lock\n", __func__, current->pid);
	ret = q2spi_setup_gsi_xfer(q2spi_pkt);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_setup_gsi_xfer failed: %d\n", __func__, ret);
		mutex_unlock(&q2spi->gsi_lock);
		return ret;
	}
	Q2SPI_DBG_2(q2spi, "%s PID=%d release gsi_lock\n", __func__, current->pid);
	mutex_unlock(&q2spi->gsi_lock);
	q2spi->doorbell_setup = true;
	Q2SPI_DBG_2(q2spi, "%s End PID=%d\n", __func__, current->pid);
	complete_all(&q2spi->db_setup_wait);
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
	Q2SPI_DBG_2(q2spi, "%s Enter PID=%d\n", __func__, current->pid);
	if (q2spi_sys_restart)
		return;

	atomic_set(&q2spi->slave_in_sleep, 0);
	atomic_set(&q2spi->sleep_cmd_sent, 0);
	memcpy(&q2spi->q2spi_cr_hdr_event, q2spi_cr_hdr_event,
	       sizeof(struct qup_q2spi_cr_header_event));
	queue_work(q2spi->doorbell_wq, &q2spi->q2spi_doorbell_work);
}

/**
 * q2spi_prepare_cr_pkt - Allocates and populates CR packet as part of doorbell handling
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Return: pointer to allocated q2spi cr packet
 */
struct q2spi_cr_packet *q2spi_prepare_cr_pkt(struct q2spi_geni *q2spi)
{
	struct q2spi_cr_packet *q2spi_cr_pkt = NULL;
	const struct qup_q2spi_cr_header_event *q2spi_cr_hdr_event = NULL;
	unsigned long flags;
	int i = 0;
	u8 *ptr;

	q2spi_cr_hdr_event = &q2spi->q2spi_cr_hdr_event;
	if (q2spi_cr_hdr_event->byte0_len > 4) {
		Q2SPI_ERROR(q2spi, "%s Err num of valid crs:%d\n", __func__,
			    q2spi_cr_hdr_event->byte0_len);
		return NULL;
	}

	q2spi_cr_pkt = q2spi_kzalloc(q2spi, sizeof(struct q2spi_cr_packet), __LINE__);
	if (!q2spi_cr_pkt) {
		Q2SPI_ERROR(q2spi, "%s Err q2spi_cr_pkt alloc failed\n", __func__);
		return NULL;
	}
	spin_lock_irqsave(&q2spi->cr_queue_lock, flags);
	q2spi_cr_pkt->num_valid_crs = q2spi_cr_hdr_event->byte0_len;
	Q2SPI_DBG_2(q2spi, "%s q2spi_cr_pkt:%p hdr_0:0x%x no_of_crs=%d\n", __func__,
		    q2spi_cr_pkt, q2spi_cr_hdr_event->cr_hdr[0], q2spi_cr_pkt->num_valid_crs);

	if (q2spi_cr_hdr_event->byte0_err)
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_cr_hdr_event->byte0_err=%d\n",
			    __func__, q2spi_cr_hdr_event->byte0_err);

	for (i = 0; i < q2spi_cr_pkt->num_valid_crs; i++) {
		q2spi_cr_pkt->cr_hdr[i].cmd = (q2spi_cr_hdr_event->cr_hdr[i]) & 0xF;
		q2spi_cr_pkt->cr_hdr[i].flow = (q2spi_cr_hdr_event->cr_hdr[i] >> 4) & 0x1;
		q2spi_cr_pkt->cr_hdr[i].type = (q2spi_cr_hdr_event->cr_hdr[i] >> 5) & 0x3;
		q2spi_cr_pkt->cr_hdr[i].parity = (q2spi_cr_hdr_event->cr_hdr[i] >> 7) & 0x1;
		Q2SPI_DBG_1(q2spi, "%s CR HDR[%d]:0x%x cmd/opcode:%d C_flow:%d type:%d parity:%d\n",
			    __func__, i, q2spi_cr_hdr_event->cr_hdr[i], q2spi_cr_pkt->cr_hdr[i].cmd,
			    q2spi_cr_pkt->cr_hdr[i].flow, q2spi_cr_pkt->cr_hdr[i].type,
			    q2spi_cr_pkt->cr_hdr[i].parity);
		if ((q2spi_cr_hdr_event->cr_hdr[i] & 0xF) == CR_EXTENSION) {
			q2spi_cr_pkt->ext_cr_hdr.cmd = (q2spi_cr_hdr_event->cr_hdr[i]) & 0xF;
			q2spi_cr_pkt->ext_cr_hdr.dw_len =
							(q2spi_cr_hdr_event->cr_hdr[i] >> 4) & 0x3;
			q2spi_cr_pkt->ext_cr_hdr.parity =
							(q2spi_cr_hdr_event->cr_hdr[i] >> 7) & 0x1;
			Q2SPI_DBG_2(q2spi, "%s CR EXT HDR[%d] cmd/opcode:%d dw_len:%d parity:%d\n",
				    __func__, i, q2spi_cr_pkt->ext_cr_hdr.cmd,
				    q2spi_cr_pkt->ext_cr_hdr.dw_len,
				    q2spi_cr_pkt->ext_cr_hdr.parity);
		}
	}
	ptr = (u8 *)q2spi->db_xfer->rx_buf;
	for (i = 0; i < q2spi_cr_pkt->num_valid_crs; i++) {
		if (q2spi_cr_pkt->cr_hdr[i].cmd == BULK_ACCESS_STATUS) {
			q2spi_cr_pkt->bulk_pkt[i].cmd = q2spi_cr_pkt->cr_hdr[i].cmd;
			q2spi_cr_pkt->bulk_pkt[i].flow = q2spi_cr_pkt->cr_hdr[i].flow;
			q2spi_cr_pkt->bulk_pkt[i].parity = q2spi_cr_pkt->cr_hdr[i].parity;
			q2spi_dump_ipc(q2spi, "DB BULK DMA RX",
				       (char *)ptr, q2spi->db_xfer->rx_len);
			q2spi_cr_pkt->bulk_pkt[i].status = ptr[0] & 0xF;
			q2spi_cr_pkt->bulk_pkt[i].flow_id = ptr[0] >> 4;
			ptr += CR_BULK_DATA_SIZE;
			q2spi_cr_pkt->cr_hdr_type[i] = CR_HDR_BULK;
			Q2SPI_DBG_2(q2spi, "%s i:%d cr_hdr_type:0x%x flow_id:%d\n",
				    __func__, i, q2spi_cr_pkt->cr_hdr_type[i],
				    q2spi_cr_pkt->bulk_pkt[i].flow_id);
		} else if ((q2spi_cr_pkt->cr_hdr[i].cmd == ADDR_LESS_WR_ACCESS) ||
					(q2spi_cr_pkt->cr_hdr[i].cmd == ADDR_LESS_RD_ACCESS)) {
			memcpy((void *)&q2spi_cr_pkt->var3_pkt[i], (void *)ptr,
			       sizeof(struct q2spi_client_dma_pkt));
			q2spi_dump_ipc(q2spi, "DB VAR3 DMA RX",
				       (char *)ptr, q2spi->db_xfer->rx_len);
			ptr += CR_DMA_DATA_SIZE;
			q2spi_cr_pkt->cr_hdr_type[i] = CR_HDR_VAR3;
			Q2SPI_DBG_2(q2spi, "%s i:%d cr_hdr_type:0x%x\n",
				    __func__, i, q2spi_cr_pkt->cr_hdr_type[i]);
			if (q2spi_cr_pkt->var3_pkt[i].arg1 == Q2SPI_CR_TRANSACTION_ERROR) {
				Q2SPI_DBG_1(q2spi, "%s arg1:0x%x arg2:0x%x arg3:0x%x\n",
					    __func__, q2spi_cr_pkt->var3_pkt[i].arg1,
					    q2spi_cr_pkt->var3_pkt[i].arg2,
					    q2spi_cr_pkt->var3_pkt[i].arg3);
				q2spi->q2spi_cr_txn_err = true;
				spin_unlock_irqrestore(&q2spi->cr_queue_lock, flags);
				return 0;
			}
			Q2SPI_DBG_2(q2spi, "%s var3_pkt:%p flow_id:%d len_part1:%d part2:%d\n",
				    __func__, &q2spi_cr_pkt->var3_pkt[i],
				    q2spi_cr_pkt->var3_pkt[i].flow_id,
				    q2spi_cr_pkt->var3_pkt[i].dw_len_part1,
				    q2spi_cr_pkt->var3_pkt[i].dw_len_part2);
		} else if (q2spi_cr_pkt->cr_hdr[i].cmd == CR_EXTENSION) {
			complete_all(&q2spi->wait_for_ext_cr);
			q2spi_cr_pkt->extension_pkt.cmd = q2spi_cr_pkt->ext_cr_hdr.cmd;
			q2spi_cr_pkt->extension_pkt.dw_len = q2spi_cr_pkt->ext_cr_hdr.dw_len;
			q2spi_cr_pkt->extension_pkt.parity = q2spi_cr_pkt->ext_cr_hdr.parity;
			ptr += q2spi_cr_pkt->extension_pkt.dw_len * 4 + CR_EXTENSION_DATA_BYTES;
			Q2SPI_DBG_2(q2spi, "%s Extension CR cmd:%d dwlen:%d parity:%d\n", __func__,
				    q2spi_cr_pkt->extension_pkt.cmd,
				    q2spi_cr_pkt->extension_pkt.dw_len,
				    q2spi_cr_pkt->extension_pkt.parity);
			q2spi_cr_pkt->cr_hdr_type[i] = CR_HDR_VAR3;
		}
	}
	spin_unlock_irqrestore(&q2spi->cr_queue_lock, flags);
	return q2spi_cr_pkt;
}

static int q2spi_open(struct inode *inode, struct file *filp)
{
	struct cdev *cdev;
	struct q2spi_chrdev *q2spi_cdev;
	struct q2spi_geni *q2spi;
	int ret = 0, rc = 0;

	if (q2spi_sys_restart)
		return -ERESTARTSYS;

	rc = iminor(inode);
	if (rc >= Q2SPI_MAX_DEV) {
		pr_err("%s Err q2spi dev minor:%d\n", __func__, rc);
		return -ENODEV;
	}

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

	if (!q2spi->port_release) {
		Q2SPI_DEBUG(q2spi, "%s Err port already opened PID:%d\n", __func__, current->pid);
		return -EBUSY;
	}

	Q2SPI_DEBUG(q2spi, "%s PID:%d, allocs=%d\n",
		    __func__, current->pid, atomic_read(&q2spi->alloc_count));
	if (q2spi->hw_state_is_bad) {
		Q2SPI_DEBUG(q2spi, "%s Err Retries failed, check HW state\n", __func__);
		return -EPIPE;
	}

	mutex_lock(&q2spi->port_lock);
	atomic_set(&q2spi->slave_in_sleep, 0);
	if (q2spi_geni_resources_on(q2spi)) {
		ret = -EIO;
		goto err;
	}

	/* Q2SPI slave HPG 2.1 Initialization */
	ret = q2spi_slave_init(q2spi, false);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s Err Failed to init q2spi slave %d\n",
			    __func__, ret);
		goto err;
	}
	q2spi->port_release = false;
	if (!q2spi->doorbell_setup) {
		ret = q2spi_map_doorbell_rx_buf(q2spi);
		if (ret) {
			Q2SPI_DEBUG(q2spi, "%s Err failed to alloc RX DMA buf\n", __func__);
			q2spi->port_release = true;
			goto err;
		}
	}
	filp->private_data = q2spi;
	q2spi->q2spi_cr_txn_err = false;
	q2spi->q2spi_sleep_cmd_enable = false;
	q2spi->q2spi_cr_hdr_err = false;
	Q2SPI_DBG_2(q2spi, "%s End PID:%d, allocs:%d\n",
		    __func__, current->pid, atomic_read(&q2spi->alloc_count));
err:
	mutex_unlock(&q2spi->port_lock);
	return ret;
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
		Q2SPI_DEBUG(q2spi, "%s Err Invalid variant:%d!\n", __func__, vtype);
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
			Q2SPI_DBG_2(q2spi, "%s ALLOC var1 i:%d vir1_buf:%p phy_dma_buf:%p\n",
				    __func__, i, (void *)q2spi->var1_buf[i],
				    (void *)q2spi->var1_dma_buf[i]);
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
			Q2SPI_DBG_2(q2spi, "%s ALLOC var5 i:%d vir5_buf:%p phy_dma_buf:%p\n",
				    __func__, i, (void *)q2spi->var5_buf[i],
				    (void *)q2spi->var5_dma_buf[i]);
			return (void *)q2spi->var5_buf[i];
		}
	}
	Q2SPI_DEBUG(q2spi, "%s Err Short of buffers for variant:%d!\n", __func__, vtype);
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
	Q2SPI_DBG_2(q2spi, "%s tid:%d ret:%d\n", __func__, tid, tid);
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
	Q2SPI_DBG_2(q2spi, "%s tid:%d\n", __func__, tid);
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
	Q2SPI_DBG_2(q2spi, "%s type:%d offset:%d remainder:%d quotient:%d\n",
		    __func__, c_type, offset, remainder, quotient);
	return offset;
}

int q2spi_frame_lra(struct q2spi_geni *q2spi, struct q2spi_request *q2spi_req_ptr,
		    struct q2spi_packet **q2spi_pkt_ptr, int vtype)
{
	struct q2spi_packet *q2spi_pkt;
	struct q2spi_host_variant1_pkt *q2spi_hc_var1;
	struct q2spi_request q2spi_req = *q2spi_req_ptr;
	int ret;
	unsigned int dw_offset = 0;

	q2spi_pkt = q2spi_alloc_q2spi_pkt(q2spi, __LINE__);
	if (!q2spi_pkt)
		return -ENOMEM;
	*q2spi_pkt_ptr = q2spi_pkt;
	q2spi_hc_var1 = (struct q2spi_host_variant1_pkt *)
				q2spi_get_variant_buf(q2spi, q2spi_pkt, VARIANT_1_LRA);
	if (!q2spi_hc_var1) {
		Q2SPI_DEBUG(q2spi, "%s Err Invalid q2spi_hc_var1\n", __func__);
		return -ENOMEM;
	}
	Q2SPI_DBG_2(q2spi, "%s var_1:%p var_1_phy:%p cmd:%d\n",
		    __func__, q2spi_hc_var1, (void *)q2spi_pkt->var1_tx_dma, q2spi_req.cmd);
	if (q2spi_req.cmd == LOCAL_REG_READ || q2spi_req.cmd == HRF_READ) {
		q2spi_hc_var1->cmd = HC_DATA_READ;
		q2spi_pkt->m_cmd_param = Q2SPI_TX_RX;
		ret = q2spi_get_rx_buf(q2spi_pkt, q2spi_req.data_len);
		if (ret)
			return ret;
	} else if (q2spi_req.cmd == LOCAL_REG_WRITE || q2spi_req.cmd == HRF_WRITE) {
		q2spi_hc_var1->cmd = HC_DATA_WRITE;
		q2spi_pkt->m_cmd_param = Q2SPI_TX_ONLY;
		q2spi_req.data_len = sizeof(q2spi_hc_var1->data_buf) <= q2spi_req.data_len ?
					sizeof(q2spi_hc_var1->data_buf) : q2spi_req.data_len;
		memcpy(q2spi_hc_var1->data_buf, q2spi_req.data_buff, q2spi_req.data_len);
		q2spi_kfree(q2spi, q2spi_req.data_buff, __LINE__);
		q2spi_req_ptr->data_buff = NULL;
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
	Q2SPI_DBG_2(q2spi, "%s data_len:%d dw_len:%d req_flow_id:%d\n",
		    __func__, q2spi_req.data_len, q2spi_hc_var1->dw_len, q2spi_req.flow_id);
	if (!q2spi_req.flow_id && !q2spi->hrf_flow) {
		ret = q2spi_alloc_xfer_tid(q2spi);
		if (ret < 0) {
			Q2SPI_DEBUG(q2spi, "%s Err failed to alloc xfer_tid\n", __func__);
			return -EINVAL;
		}
		q2spi_hc_var1->flow_id = ret;
	} else {
		q2spi_hc_var1->flow_id = q2spi_req.flow_id;
	}
	dw_offset = q2spi_get_dw_offset(q2spi, q2spi_req.cmd, q2spi_req.addr);
	q2spi_hc_var1->reg_offset = dw_offset;
	q2spi_pkt->xfer->tid = q2spi_hc_var1->flow_id;
	q2spi_pkt->var1_pkt = q2spi_hc_var1;
	q2spi_pkt->vtype = vtype;
	q2spi_pkt->valid = true;
	q2spi_pkt->sync = q2spi_req.sync;

	Q2SPI_DBG_1(q2spi, "%s *q2spi_pkt_ptr:%p End ret flow_id:%d\n",
		    __func__, *q2spi_pkt_ptr, q2spi_hc_var1->flow_id);
	return q2spi_hc_var1->flow_id;
}

int q2spi_sma_format(struct q2spi_geni *q2spi, struct q2spi_request *q2spi_req_ptr,
		     struct q2spi_packet *q2spi_pkt)
{
	struct q2spi_host_variant4_5_pkt *q2spi_hc_var5;
	struct q2spi_request q2spi_req = *q2spi_req_ptr;
	int ret = 0, flow_id;

	if (!q2spi) {
		Q2SPI_ERROR(q2spi, "%s Err q2spi NULL\n", __func__);
		return -EINVAL;
	}
	if (!q2spi_pkt) {
		Q2SPI_ERROR(q2spi, "%s Err Invalid q2spi_pkt\n", __func__);
		return -EINVAL;
	}
	Q2SPI_DBG_2(q2spi, "%s q2spi_pkt:%p pkt_var_1:%p pkt_dma:%p pkt_var_5:%p\n",
		    __func__, q2spi_pkt, q2spi_pkt->var1_pkt,
		    (void *)q2spi_pkt->var5_tx_dma,
		    q2spi_pkt->var5_pkt);
	Q2SPI_DBG_2(q2spi, "%s req_cmd:%d req_addr:%d req_len:%d req_data_buf:%p\n",
		    __func__, q2spi_req.cmd, q2spi_req.addr, q2spi_req.data_len,
		    q2spi_req.data_buff);

	q2spi_hc_var5 = (struct q2spi_host_variant4_5_pkt *)
			q2spi_get_variant_buf(q2spi, q2spi_pkt, VARIANT_5);
	if (!q2spi_hc_var5) {
		Q2SPI_DEBUG(q2spi, "%s Err var5 buffer is not available\n", __func__);
		return -ENOMEM;
	}
	memset(q2spi_hc_var5->data_buf, 0xba, 4096);
	Q2SPI_DBG_2(q2spi, "%s var_5:%p cmd:%d\n", __func__, q2spi_hc_var5, q2spi_req.cmd);
	if (q2spi_req.data_len > Q2SPI_MAX_DATA_LEN) {
		Q2SPI_ERROR(q2spi, "%s Err (q2spi_req.data_len > Q2SPI_MAX_DATA_LEN) %d return\n",
			    __func__, q2spi_req.data_len);
		Q2SPI_DBG_1(q2spi, "%s Unmapping Var5 buffer\n", __func__);
		q2spi_unmap_dma_buf_used(q2spi, q2spi_pkt->var5_tx_dma, q2spi_pkt->var5_rx_dma);
		return -ENOMEM;
	}

	if (q2spi_req.cmd == DATA_READ || q2spi_req.cmd == HRF_READ) {
		q2spi_hc_var5->cmd = HC_SMA_READ;
		q2spi_pkt->m_cmd_param = Q2SPI_TX_RX;
		ret = q2spi_get_rx_buf(q2spi_pkt, q2spi_req.data_len);
		if (ret) {
			Q2SPI_DBG_1(q2spi, "%s Unmapping Var5 buffer\n", __func__);
			q2spi_unmap_dma_buf_used(q2spi, q2spi_pkt->var5_tx_dma,
						 q2spi_pkt->var5_rx_dma);
			return ret;
		}
	} else if (q2spi_req.cmd == DATA_WRITE || q2spi_req.cmd == HRF_WRITE) {
		q2spi_hc_var5->cmd = HC_SMA_WRITE;
		q2spi_pkt->m_cmd_param = Q2SPI_TX_ONLY;
		q2spi_req.data_len = sizeof(q2spi_hc_var5->data_buf) <= q2spi_req.data_len ?
					sizeof(q2spi_hc_var5->data_buf) : q2spi_req.data_len;
		memcpy(q2spi_hc_var5->data_buf, q2spi_req.data_buff, q2spi_req.data_len);
		q2spi_dump_ipc(q2spi, "sma format q2spi_req data_buf",
			       (char *)q2spi_req.data_buff, q2spi_req.data_len);
		q2spi_dump_ipc(q2spi, "sma format var5 data_buf",
			       (char *)q2spi_hc_var5->data_buf, q2spi_req.data_len);
		q2spi_kfree(q2spi, q2spi_req.data_buff, __LINE__);
		q2spi_req_ptr->data_buff = NULL;
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
	Q2SPI_DBG_2(q2spi, "%s dw_len_part1:%d dw_len_part2:%d\n",
		    __func__, q2spi_hc_var5->dw_len_part1, q2spi_hc_var5->dw_len_part2);
	q2spi_hc_var5->access_type = SYSTEM_MEMORY_ACCESS;
	q2spi_hc_var5->address_mode = NO_CLIENT_ADDRESS;
	if (!q2spi_req.flow_id && !q2spi->hrf_flow) {
		flow_id = q2spi_alloc_xfer_tid(q2spi);
		if (flow_id < 0) {
			Q2SPI_DEBUG(q2spi, "%s Err failed to alloc tid", __func__);
			return -EINVAL;
		}
		q2spi_hc_var5->flow_id = flow_id;
	} else {
		if (q2spi_req.flow_id < Q2SPI_END_TID_ID)
			q2spi_hc_var5->flow_id = q2spi_pkt->flow_id;
		else
			q2spi_hc_var5->flow_id = q2spi_req.flow_id;
	}
	q2spi_pkt->xfer->tid = q2spi_hc_var5->flow_id;
	q2spi_pkt->var5_pkt = q2spi_hc_var5;
	q2spi_pkt->vtype = VARIANT_5;
	q2spi_pkt->valid = true;
	q2spi_pkt->sync = q2spi_req.sync;
	q2spi_pkt->flow_id = q2spi_hc_var5->flow_id;
	Q2SPI_DBG_1(q2spi, "%s flow id:%d q2spi_pkt:%p pkt_var1:%p pkt_tx_dma:%p var5_pkt:%p\n",
		    __func__, q2spi_hc_var5->flow_id, q2spi_pkt,
		    q2spi_pkt->var1_pkt, (void *)q2spi_pkt->var5_tx_dma, q2spi_pkt->var5_pkt);
	q2spi_dump_ipc(q2spi, "sma format var5(2) data_buf",
		       (char *)q2spi_hc_var5->data_buf, q2spi_req.data_len);
	return q2spi_hc_var5->flow_id;
}

static int q2spi_abort_command(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req,
			       struct q2spi_packet **q2spi_pkt_ptr)
{
	struct q2spi_host_abort_pkt *q2spi_abort_req;
	struct q2spi_packet *q2spi_pkt;

	if (!q2spi) {
		Q2SPI_DEBUG(q2spi, "%s Err Invalid q2spi\n", __func__);
		return -EINVAL;
	}
	Q2SPI_DBG_1(q2spi, "%s cmd:%d addr:%d flow_id:%d data_len:%d\n",
		    __func__, q2spi_req.cmd, q2spi_req.addr,
		    q2spi_req.flow_id, q2spi_req.data_len);
	q2spi_pkt = q2spi_alloc_q2spi_pkt(q2spi, __LINE__);
	if (!q2spi_pkt)
		return -ENOMEM;
	*q2spi_pkt_ptr = q2spi_pkt;

	q2spi_abort_req = q2spi_alloc_host_variant(q2spi, sizeof(struct q2spi_host_abort_pkt));
	if (!q2spi_abort_req) {
		Q2SPI_ERROR(q2spi, "%s Err alloc and map failed\n", __func__);
		return -EINVAL;
	}

	q2spi_abort_req->cmd = HC_ABORT;
	q2spi_abort_req->flow_id = q2spi_alloc_xfer_tid(q2spi);
	q2spi_pkt->xfer->tid = q2spi_abort_req->flow_id;
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
	Q2SPI_DBG_1(q2spi, "%s cmd:%d addr:%d flow_id:%d data_len:%d\n",
		    __func__, q2spi_req.cmd, q2spi_req.addr,
		    q2spi_req.flow_id, q2spi_req.data_len);
	q2spi_pkt = q2spi_alloc_q2spi_pkt(q2spi, __LINE__);
	if (!q2spi_pkt)
		return -ENOMEM;
	*q2spi_pkt_ptr = q2spi_pkt;
	q2spi_softreset_req = q2spi_alloc_host_variant(q2spi,
						       sizeof(struct q2spi_host_soft_reset_pkt));
	if (!q2spi_softreset_req) {
		Q2SPI_ERROR(q2spi, "%s Err alloc and map failed\n", __func__);
		q2spi_free_q2spi_pkt(q2spi_pkt, __LINE__);
		q2spi_pkt = NULL;
		return -EINVAL;
	}
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
	atomic_inc(&q2spi->rx_avail);
	wake_up_interruptible(&q2spi->readq);
	wake_up(&q2spi->read_wq);
	Q2SPI_DBG_1(q2spi, "%s wake userspace rx_avail:%d\n", __func__,
		    atomic_read(&q2spi->rx_avail));
}

int q2spi_hrf_sleep(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req,
		    struct q2spi_packet **q2spi_pkt_ptr)
{
	struct q2spi_request *q2spi_hrf_req;
	struct q2spi_packet *q2spi_pkt = NULL;
	int ret = 0;

	ret = q2spi_hrf_entry_format_sleep(q2spi, q2spi_req, &q2spi_hrf_req);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_hrf_entry_format failed ret:%d\n", __func__, ret);
		return ret;
	}
	Q2SPI_DBG_1(q2spi, "%s hrf_req cmd:%d flow_id:%d data_buff:%p\n",
		    __func__, q2spi_hrf_req->cmd, q2spi_hrf_req->flow_id, q2spi_hrf_req->data_buff);

	ret = q2spi_frame_lra(q2spi, q2spi_hrf_req, &q2spi_pkt, VARIANT_1_LRA);
	Q2SPI_DBG_2(q2spi, "%s q2spi_hrf_req:%p q2spi_pkt:%p\n",
		    __func__, q2spi_hrf_req, q2spi_pkt);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_frame_lra failed ret:%d\n", __func__, ret);
		return ret;
	}
	list_add_tail(&q2spi_pkt->list, &q2spi->tx_queue_list);
	q2spi_kfree(q2spi, q2spi_hrf_req, __LINE__);
	*q2spi_pkt_ptr = q2spi_pkt;

	Q2SPI_DBG_1(q2spi, "%s End %d\n", __func__, __LINE__);
	return ret;
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
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_hrf_entry_format failed ret:%d\n", __func__, ret);
		return ret;
	}

	Q2SPI_DBG_1(q2spi, "%s cmd:%d flow_id:%d data_buff:%p\n",
		    __func__, q2spi_req.cmd, q2spi_req.flow_id, q2spi_req.data_buff);
	Q2SPI_DBG_2(q2spi, "%s addr:0x%x proto:0x%x data_len:0x%x\n",
		    __func__, q2spi_req.addr, q2spi_req.proto_ind, q2spi_req.data_len);

	ret = q2spi_frame_lra(q2spi, q2spi_hrf_req, &q2spi_pkt, VARIANT_1_HRF);
	Q2SPI_DBG_2(q2spi, "%s q2spi_hrf_req:%p q2spi_pkt:%p\n",
		    __func__, q2spi_hrf_req, q2spi_pkt);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_frame_lra failed ret:%d\n", __func__, ret);
		return ret;
	}

	q2spi_pkt->flow_id = ret;
	ret = q2spi_sma_format(q2spi, &q2spi_req, q2spi_pkt);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_sma_format failed ret:%d\n", __func__, ret);
		q2spi_unmap_var_bufs(q2spi, q2spi_pkt);
		q2spi_kfree(q2spi, q2spi_pkt, __LINE__);
		return ret;
	}
	list_add_tail(&q2spi_pkt->list, &q2spi->tx_queue_list);
	q2spi_pkt->vtype = VARIANT_5_HRF;
	q2spi_kfree(q2spi, q2spi_hrf_req, __LINE__);
	*q2spi_pkt_ptr = q2spi_pkt;
	q2spi->hrf_flow = false;

	Q2SPI_DBG_2(q2spi, "%s End q2spi_pkt:%p\n", __func__, q2spi_pkt);
	return ret;
}

void q2spi_print_req_cmd(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req)
{
	if (q2spi_req.cmd == LOCAL_REG_READ)
		Q2SPI_DBG_2(q2spi, "%s cmd:LOCAL_REG_READ\n", __func__);
	else if (q2spi_req.cmd == LOCAL_REG_WRITE)
		Q2SPI_DBG_2(q2spi, "%s cmd:LOCAL_REG_WRITE\n", __func__);
	else if (q2spi_req.cmd == HRF_READ)
		Q2SPI_DBG_2(q2spi, "%s cmd:HRF_READ\n", __func__);
	else if (q2spi_req.cmd == HRF_WRITE)
		Q2SPI_DBG_2(q2spi, "%s cmd:HRF_WRITE\n", __func__);
	else if (q2spi_req.cmd == DATA_READ)
		Q2SPI_DBG_2(q2spi, "%s cmd:DATA_READ\n", __func__);
	else if (q2spi_req.cmd == DATA_WRITE)
		Q2SPI_DBG_2(q2spi, "%s cmd:DATA_WRITE\n", __func__);
	else if (q2spi_req.cmd == SOFT_RESET)
		Q2SPI_DBG_2(q2spi, "%s cmd:SOFT_RESET\n", __func__);
	else if (q2spi_req.cmd == Q2SPI_HRF_SLEEP_CMD)
		Q2SPI_DBG_2(q2spi, "%s cmd:Sleep CMD to Client\n", __func__);
	else
		Q2SPI_DEBUG(q2spi, "%s Invalid cmd:%d\n", __func__, q2spi_req.cmd);
}

/*
 * q2spi_del_pkt_from_tx_queue - Delete q2spi packets from tx_queue_list
 * @q2spi: pointer to q2spi_geni
 * @cur_q2spi_pkt: ponter to q2spi_packet
 *
 * This function iterates through the tx_queue_list and obtains the cur_q2spi_pkt
 * and delete the completed packet from the list if q2spi_pkt->state is under deletion.
 *
 * Return: Returns true if given packet is found in tx_queue_list and deleted, else returns false.
 */
bool q2spi_del_pkt_from_tx_queue(struct q2spi_geni *q2spi, struct q2spi_packet *cur_q2spi_pkt)
{
	struct q2spi_packet *q2spi_pkt, *q2spi_pkt_tmp;
	bool found = false;

	if (!cur_q2spi_pkt) {
		Q2SPI_DEBUG(q2spi, "%s q2spi_pkt NULL\n", __func__);
		q2spi_tx_queue_status(q2spi);
		return found;
	}

	mutex_lock(&q2spi->queue_lock);
	list_for_each_entry_safe(q2spi_pkt, q2spi_pkt_tmp, &q2spi->tx_queue_list, list) {
		if (cur_q2spi_pkt == q2spi_pkt) {
			Q2SPI_DBG_1(q2spi, "%s Found q2spi_pkt:%p state:%s\n", __func__,
				    q2spi_pkt, q2spi_pkt_state(q2spi_pkt));
			if (q2spi_pkt->state == IN_DELETION) {
				list_del(&q2spi_pkt->list);
				q2spi_pkt->state = DELETED;
				found = true;
				break;
			}
		}
		Q2SPI_DBG_1(q2spi, "%s q2spi_pkt:%p state:%s\n",
			    __func__, q2spi_pkt, q2spi_pkt_state(q2spi_pkt));
	}
	mutex_unlock(&q2spi->queue_lock);

	if (!found)
		Q2SPI_DEBUG(q2spi, "%s Couldn't find q2spi_pkt:%p\n", __func__, cur_q2spi_pkt);

	q2spi_tx_queue_status(q2spi);
	return found;
}

/*
 * q2spi_add_req_to_tx_queue - Add q2spi packets to tx_queue_list
 * @q2spi: pointer to q2spi_geni
 * @q2spi_req_ptr: pointer to q2spi_request
 * @q2spi_pkt_ptr: ponter to q2spi_packet
 *
 * This function frames the Q2SPI host request based on request type
 * add the packet to tx_queue_list.
 *
 * Return: 0 on success. Error code on failure.
 */
int q2spi_add_req_to_tx_queue(struct q2spi_geni *q2spi, struct q2spi_request *q2spi_req_ptr,
			      struct q2spi_packet **q2spi_pkt_ptr)
{
	struct q2spi_packet *q2spi_pkt = NULL;
	struct q2spi_request q2spi_req = *q2spi_req_ptr;
	int ret = -EINVAL;

	if (q2spi->port_release) {
		Q2SPI_DEBUG(q2spi, "%s Err Port in closed state, return\n", __func__);
		return -ENOENT;
	}

	q2spi_tx_queue_status(q2spi);
	q2spi_print_req_cmd(q2spi, q2spi_req);
	if (q2spi_req.cmd == LOCAL_REG_READ || q2spi_req.cmd == LOCAL_REG_WRITE) {
		ret = q2spi_frame_lra(q2spi, q2spi_req_ptr, &q2spi_pkt, VARIANT_1_LRA);
		if (ret < 0) {
			Q2SPI_DEBUG(q2spi, "%s Err q2spi_frame_lra failed ret:%d\n",
				    __func__, ret);
			return ret;
		}
		list_add_tail(&q2spi_pkt->list, &q2spi->tx_queue_list);
	} else if (q2spi_req.cmd == DATA_READ || q2spi_req.cmd == DATA_WRITE) {
		q2spi_pkt = q2spi_alloc_q2spi_pkt(q2spi, __LINE__);
		if (!q2spi_pkt)
			return -ENOMEM;
		ret = q2spi_sma_format(q2spi, q2spi_req_ptr, q2spi_pkt);
		if (ret < 0) {
			Q2SPI_DEBUG(q2spi, "%s Err q2spi_sma_format failed ret:%d\n",
				    __func__, ret);
			q2spi_kfree(q2spi, q2spi_pkt, __LINE__);
			return ret;
		}
		Q2SPI_DBG_1(q2spi, "%s q2spi_pkt:%p state=%s ret:%d\n",
			    __func__, q2spi_pkt, q2spi_pkt_state(q2spi_pkt), ret);
		list_add_tail(&q2spi_pkt->list, &q2spi->tx_queue_list);
	} else if (q2spi_req.cmd == HRF_READ || q2spi_req.cmd == HRF_WRITE) {
		ret = q2spi_hrf_flow(q2spi, q2spi_req, &q2spi_pkt);
		if (ret < 0) {
			Q2SPI_DEBUG(q2spi, "%s Err q2spi_hrf_flow failed ret:%d\n", __func__, ret);
			return ret;
		}
	} else if (q2spi_req.cmd == ABORT) {
		ret = q2spi_abort_command(q2spi, q2spi_req, &q2spi_pkt);
		if (ret < 0) {
			Q2SPI_DEBUG(q2spi, "%s Err abort_command failed ret:%d\n", __func__, ret);
			return ret;
		}
		list_add_tail(&q2spi_pkt->list, &q2spi->tx_queue_list);
	} else if (q2spi_req.cmd == SOFT_RESET) {
		ret = q2spi_soft_reset(q2spi, q2spi_req, &q2spi_pkt);
		if (ret < 0) {
			Q2SPI_DEBUG(q2spi, "%s Err soft_reset failed ret:%d\n", __func__, ret);
			return ret;
		}
		list_add_tail(&q2spi_pkt->list, &q2spi->tx_queue_list);
	} else if (q2spi_req.cmd == Q2SPI_HRF_SLEEP_CMD) {
		q2spi_req.cmd = HRF_WRITE;
		ret = q2spi_hrf_sleep(q2spi, q2spi_req, &q2spi_pkt);
		if (ret < 0) {
			Q2SPI_DEBUG(q2spi, "%s Err q2spi_hrf_sleep failed ret:%d\n",
				    __func__, ret);
			return ret;
		}
	} else {
		Q2SPI_DEBUG(q2spi, "%s Err cmd:%d\n", __func__, q2spi_req.cmd);
		return -EINVAL;
	}

	if (q2spi_pkt) {
		*q2spi_pkt_ptr = q2spi_pkt;
		Q2SPI_DBG_1(q2spi, "%s q2spi_pkt:%p req_cmd:%d ret:%d\n",
			    __func__, q2spi_pkt, q2spi_req.cmd, ret);
	} else {
		Q2SPI_DBG_1(q2spi, "%s req_cmd:%d ret:%d\n", __func__, q2spi_req.cmd, ret);
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
		Q2SPI_DEBUG(q2spi, "%s Invalid data len %d bytes\n", __func__, q2spi_req->data_len);
		return false;
	}
	return true;
}

static int q2spi_check_resp_avail_buff(struct q2spi_geni *q2spi)
{
	unsigned int i, count = 0;

	for (i = 0; i < Q2SPI_MAX_RESP_BUF; i++) {
		if (!q2spi->resp_buf_used[i])
			count++;
		else
			Q2SPI_DEBUG(q2spi, "%s resp buffer in use %p\n",
				    __func__, q2spi->resp_buf_used[i]);
	}
	return count;
}

/*
 * q2spi_wakeup_hw_from_sleep - wakeup the slave hw and wait for extension CR
 * @q2spi: pointer to q2spi_geni structure
 *
 * Return: 0 on success. Error code on failure.
 */
static int q2spi_wakeup_hw_from_sleep(struct q2spi_geni *q2spi)
{
	unsigned long xfer_timeout = 0;
	long timeout = 0;
	int ret = 0;

	if (q2spi->q2spi_cr_txn_err || q2spi->q2spi_cr_hdr_err) {
		q2spi_transfer_abort(q2spi);
		q2spi->q2spi_cr_txn_err = false;
		q2spi->q2spi_cr_hdr_err = false;
		return 0;
	}

	xfer_timeout = msecs_to_jiffies(EXT_CR_TIMEOUT_MSECS);
	reinit_completion(&q2spi->wait_for_ext_cr);
	/* Send gpio wakeup signal on q2spi lines to hw */
	Q2SPI_DBG_1(q2spi, "%s Send wakeup_hw to wakeup client\n", __func__);
	ret = q2spi_wakeup_slave_through_gpio(q2spi);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_wakeup_slave_through_gpio\n", __func__);
		return ret;
	}
	Q2SPI_DBG_2(q2spi, "%s Waiting for Extended CR\n", __func__);
	timeout = wait_for_completion_interruptible_timeout(&q2spi->wait_for_ext_cr, xfer_timeout);
	if (timeout <= 0) {
		Q2SPI_DEBUG(q2spi, "%s Err timeout %ld for Extended CR\n", __func__, timeout);
		if (timeout == -ERESTARTSYS) {
			q2spi_sys_restart = true;
			return -ERESTARTSYS;
		}
	} else {
		Q2SPI_DBG_1(q2spi, "%s Received Extended CR\n", __func__);
	}

	return ret;
}

/*
 * __q2spi_transfer - Queues the work to transfer q2spi packet present in tx queue
 * and wait for its completion
 * @q2spi: pointer to q2spi_geni structure
 * @q2spi_req: Pointer to q2spi_request structure
 * @len: Represents transfer length of the q2spi request
 *
 * This function supports sync mode and queue the work to processor and
 * wait for completion of transfer.
 *
 * Return: returns length of data transferred on success. Failure code in case of async mode
 * or any failures.
 */
static int __q2spi_transfer(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req,
			    struct q2spi_packet *q2spi_pkt, size_t len)
{
	unsigned long xfer_timeout = 0;
	long timeout = 0;
	int ret = 0;

	if (!q2spi_req.sync) {
		Q2SPI_ERROR(q2spi, "%s async mode not supported\n", __func__);
		return -EINVAL;
	}

	if (q2spi->port_release) {
		Q2SPI_DEBUG(q2spi, "%s Err Port in closed state, return 0 for user to retry\n",
			    __func__);
		return -ENOENT;
	}

	Q2SPI_DBG_1(q2spi, "%s is slave_in_sleep:%d\n",
		    __func__, atomic_read(&q2spi->slave_in_sleep));

	ret = __q2spi_send_messages(q2spi, (void *)q2spi_pkt);
	if (ret == -ETIMEDOUT) {
		return -ETIMEDOUT;
	} else if (ret == -EAGAIN && atomic_read(&q2spi->retry)) {
		atomic_dec(&q2spi->retry);
		Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p CR Doorbell Pending try again\n",
			    __func__, q2spi_pkt);
		return 0;
	} else if (ret == -EINVAL) {
		return -EINVAL;
	} else if (ret) {
		Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p __q2spi_send_messages ret:%d\n",
			    __func__, q2spi_pkt, ret);
		/* return 0 to userspace to retry the transfer from application */
		return 0;
	}

	if (q2spi_pkt->vtype == VARIANT_5_HRF) {
		ret = q2spi_process_hrf_flow_after_lra(q2spi, q2spi_pkt);
		if (ret) {
			Q2SPI_DEBUG(q2spi, "%s Err hrf_flow sma write fail ret %d\n",
				    __func__, ret);
			q2spi_unmap_var_bufs(q2spi, q2spi_pkt);
			return ret;
		}
	}

	if (q2spi_pkt->is_client_sleep_pkt) {
		Q2SPI_DBG_1(q2spi, "%s q2spi_pkt:%p client sleep_cmd ret:%d",
			    __func__, q2spi_pkt, ret);
		return ret;
	}

	if (q2spi_req.cmd == HRF_WRITE) {
		/* HRF_WRITE */
		xfer_timeout = msecs_to_jiffies(XFER_TIMEOUT_OFFSET);
		Q2SPI_DBG_1(q2spi, "%s q2spi_pkt:%p waiting for bulk_wait\n", __func__, q2spi_pkt);
		timeout = wait_for_completion_interruptible_timeout
					(&q2spi_pkt->bulk_wait, xfer_timeout);
		if (timeout <= 0) {
			Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p Err timeout %ld for bulk_wait\n",
				    __func__, q2spi_pkt, timeout);
			if (timeout == -ERESTARTSYS) {
				q2spi_sys_restart = true;
				return -ERESTARTSYS;
			}
			return -ETIMEDOUT;
		}
	} else if (q2spi_req.cmd == LOCAL_REG_READ) {
		if (copy_to_user(q2spi_req.data_buff, q2spi_pkt->xfer->rx_buf,
				 q2spi_req.data_len)) {
			Q2SPI_DEBUG(q2spi, "%s Err copy_to_user fail\n", __func__);
			return -EFAULT;
		}
		Q2SPI_DBG_1(q2spi, "%s ret data_len:%d\n", __func__, q2spi_req.data_len);
		return q2spi_req.data_len;
	}
	return len;
}

/*
 * q2spi_transfer_with_retries -  queue the transfer to GSI and wait for completion. Also
 * retry the transfer for max count of Q2SPI_MAX_TX_RETRIES in case of transfer timeout
 * @q2spi: pointer to q2spi_geni structure
 * @q2spi_req: pointer to q2spi_request structure
 * @q2spi_pkt: pointer to q2spi packet
 * @len: represents transfer length of the q2spi request
 * @flow_id: Transfer id of q2spi transfer request.
 *
 * Return: size_of(struct q2spi_request) on success. Error code on failure.
 */
static int q2spi_transfer_with_retries(struct q2spi_geni *q2spi, struct q2spi_request q2spi_req,
				       struct q2spi_packet *cur_q2spi_pkt, size_t len,
				       int flow_id, void *user_buf)
{
	void *data_buf;
	int i, ret = 0, retry_count = Q2SPI_MAX_TX_RETRIES;

	if (q2spi_req.cmd == LOCAL_REG_READ)
		retry_count = 0;

	for (i = 0; i <= retry_count; i++) {
		/* Reset 100msec client sleep timer */
		mod_timer(&q2spi->slave_sleep_timer,
			  jiffies + msecs_to_jiffies(Q2SPI_SLAVE_SLEEP_TIME_MSECS));
		ret = __q2spi_transfer(q2spi, q2spi_req, cur_q2spi_pkt, len);
		Q2SPI_DBG_1(q2spi, "%s flow_id:%d ret:%d\n", __func__, flow_id, ret);
		q2spi_free_xfer_tid(q2spi, flow_id);
		if (ret > 0 || i == Q2SPI_MAX_TX_RETRIES) {
			if (ret == len)
				goto transfer_exit;
			if (i == Q2SPI_MAX_TX_RETRIES & ret < 0) {
				/*
				 * Shouldn't reach here, retry of transfers failed,
				 * could be hw is in bad state.
				 */
				Q2SPI_DEBUG(q2spi, "%s %d retries failed, hw_state_is_bad\n",
					    __func__, i);
				q2spi->hw_state_is_bad = true;
				q2spi_dump_client_error_regs(q2spi);
			}
			goto pm_put_exit;
		} else if (ret == -ERESTARTSYS) {
			Q2SPI_DEBUG(q2spi, "%s system is in restart\n", __func__);
			return ret;
		} else if (ret == -ETIMEDOUT) {
			/* Upon transfer failure's retry here */
			Q2SPI_DBG_1(q2spi, "%s ret:%d retry_count:%d q2spi_pkt:%p db_pending:%d\n",
				    __func__, ret, i + 1, cur_q2spi_pkt,
				    atomic_read(&q2spi->doorbell_pending));
			if (q2spi->gsi->qup_gsi_global_err) {
				Q2SPI_DEBUG(q2spi, "%s GSI global error, No retry\n", __func__);
				ret = -EIO;
				goto transfer_exit;
			}

			if (i == 0 && !atomic_read(&q2spi->doorbell_pending) &&
			    q2spi->is_start_seq_fail) {
				q2spi->is_start_seq_fail = false;
				ret = q2spi_wakeup_hw_from_sleep(q2spi);
				if (ret) {
					Q2SPI_DEBUG(q2spi, "%s Err q2spi_wakeup_hw_from_sleep\n",
						    __func__);
					goto pm_put_exit;
				}
			}
			q2spi->q2spi_cr_txn_err = false;
			q2spi->q2spi_cr_hdr_err = false;

			/* Reset 100msec client sleep timer */
			mod_timer(&q2spi->slave_sleep_timer,
				  jiffies + msecs_to_jiffies(Q2SPI_SLAVE_SLEEP_TIME_MSECS));
			/* Should not perform SOFT RESET when UWB sets reserved[0] bit 0 set */
			if (!q2spi->q2spi_cr_txn_err &&
			    (!(q2spi_req.reserved[0] & Q2SPI_SOFT_RESET_CMD_BIT)) && i == 1)
				q2spi_transfer_soft_reset(q2spi);

			cur_q2spi_pkt->state = IN_DELETION;
			q2spi_del_pkt_from_tx_queue(q2spi, cur_q2spi_pkt);
			q2spi_free_q2spi_pkt(cur_q2spi_pkt, __LINE__);
			/* Copy data from user buffer only for write request */
			if (q2spi_req.cmd == LOCAL_REG_WRITE || q2spi_req.cmd == DATA_WRITE ||
			    q2spi_req.cmd == HRF_WRITE) {
				data_buf = q2spi_kzalloc(q2spi, q2spi_req.data_len, __LINE__);
				if (!data_buf) {
					Q2SPI_DEBUG(q2spi, "%s Err buf2 alloc failed\n", __func__);
					ret = -ENOMEM;
					goto pm_put_exit;
				}
				if (copy_from_user(data_buf, user_buf, q2spi_req.data_len)) {
					Q2SPI_DEBUG(q2spi, "%s Err copy_from_user to buf2 failed\n",
						    __func__);
					q2spi_kfree(q2spi, data_buf, __LINE__);
					ret = -EFAULT;
					goto pm_put_exit;
				}
				q2spi_req.data_buff = data_buf;
			}
			mutex_lock(&q2spi->queue_lock);
			flow_id = q2spi_add_req_to_tx_queue(q2spi, &q2spi_req, &cur_q2spi_pkt);
			mutex_unlock(&q2spi->queue_lock);
			if (flow_id < 0) {
				q2spi_kfree(q2spi, data_buf, __LINE__);
				Q2SPI_DEBUG(q2spi, "%s Err Failed to add tx req to queue ret:%d\n",
					    __func__, flow_id);
				ret = -ENOMEM;
				goto pm_put_exit;
			}
			Q2SPI_DBG_1(q2spi, "%s cur_q2spi_pkt=%p\n", __func__, cur_q2spi_pkt);
		} else {
			/* Upon SW error break here */
			break;
		}
	}
transfer_exit:
	cur_q2spi_pkt->state = IN_DELETION;
	q2spi_del_pkt_from_tx_queue(q2spi, cur_q2spi_pkt);
	q2spi_free_q2spi_pkt(cur_q2spi_pkt, __LINE__);
pm_put_exit:
	pm_runtime_mark_last_busy(q2spi->dev);
	Q2SPI_DBG_2(q2spi, "%s PM put_autosuspend count:%d line:%d\n", __func__,
		    atomic_read(&q2spi->dev->power.usage_count), __LINE__);
	pm_runtime_put_autosuspend(q2spi->dev);
	Q2SPI_DBG_2(q2spi, "%s PM after put_autosuspend count:%d\n", __func__,
		    atomic_read(&q2spi->dev->power.usage_count));
	return ret;
}

/*
 * q2spi_transfer_abort - Add Abort request in tx_queue list and submit q2spi transfer
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Return: 0 on success. Error code on failure.
 */
void q2spi_transfer_abort(struct q2spi_geni *q2spi)
{
	struct q2spi_packet *cur_q2spi_abort_pkt;
	struct q2spi_request abort_request;
	int ret = 0;

	Q2SPI_DBG_2(q2spi, "%s ABORT\n", __func__);
	abort_request.cmd = ABORT;
	abort_request.sync = 1;
	mutex_lock(&q2spi->queue_lock);
	ret = q2spi_add_req_to_tx_queue(q2spi, &abort_request, &cur_q2spi_abort_pkt);
	mutex_unlock(&q2spi->queue_lock);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_add_req_to_tx_queue ret:%d\n", __func__, ret);
		return;
	}
	__q2spi_transfer(q2spi, abort_request, cur_q2spi_abort_pkt, 0);
	if (ret)
		Q2SPI_DEBUG(q2spi, "%s __q2spi_transfer q2spi_pkt:%p ret%d\n",
			    __func__, cur_q2spi_abort_pkt, ret);
	cur_q2spi_abort_pkt->state = IN_DELETION;
	q2spi_del_pkt_from_tx_queue(q2spi, cur_q2spi_abort_pkt);
	q2spi_kfree(q2spi, cur_q2spi_abort_pkt->xfer, __LINE__);
	cur_q2spi_abort_pkt->xfer = NULL;
}

/*
 * q2spi_transfer_soft_reset - Add soft-reset request in tx_queue list and submit q2spi transfer
 *
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Return: 0 on success. Error code on failure.
 */
void q2spi_transfer_soft_reset(struct q2spi_geni *q2spi)
{
	struct q2spi_packet *cur_q2spi_sr_pkt;
	struct q2spi_request soft_reset_request;
	int ret = 0;

	Q2SPI_DBG_2(q2spi, "%s SOFT_RESET\n", __func__);
	soft_reset_request.cmd = SOFT_RESET;
	soft_reset_request.sync = 1;
	mutex_lock(&q2spi->queue_lock);
	ret = q2spi_add_req_to_tx_queue(q2spi, &soft_reset_request, &cur_q2spi_sr_pkt);
	mutex_unlock(&q2spi->queue_lock);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_add_req_to_tx_queue ret:%d\n", __func__, ret);
		return;
	}
	__q2spi_transfer(q2spi, soft_reset_request, cur_q2spi_sr_pkt, 0);
	if (ret)
		Q2SPI_DEBUG(q2spi, "%s __q2spi_transfer q2spi_pkt:%p ret%d\n",
			    __func__, cur_q2spi_sr_pkt, ret);
	cur_q2spi_sr_pkt->state = IN_DELETION;
	q2spi_del_pkt_from_tx_queue(q2spi, cur_q2spi_sr_pkt);
	q2spi_kfree(q2spi, cur_q2spi_sr_pkt->xfer, __LINE__);
	cur_q2spi_sr_pkt->xfer = NULL;
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
	if (q2spi_sys_restart)
		return -ERESTARTSYS;

	if (!q2spi)
		return -EINVAL;

	if (q2spi->port_release) {
		Q2SPI_DEBUG(q2spi, "%s Err Port in closed state, return\n", __func__);
		return -ENOENT;
	}

	if (q2spi->hw_state_is_bad) {
		Q2SPI_DEBUG(q2spi, "%s Err Retries failed, check HW state\n", __func__);
		return -EPIPE;
	}

	if (!q2spi_check_resp_avail_buff(q2spi)) {
		Q2SPI_DEBUG(q2spi, "%s Err Short of resp buffers\n", __func__);
		return -EAGAIN;
	}

	if (len != sizeof(struct q2spi_request)) {
		Q2SPI_DEBUG(q2spi, "%s Err Invalid length %zx Expected %lx\n",
			    __func__, len, sizeof(struct q2spi_request));
		return -EINVAL;
	}

	if (copy_from_user(q2spi_req, buf, sizeof(struct q2spi_request))) {
		Q2SPI_DEBUG(q2spi, "%s Err copy_from_user failed\n", __func__);
		return -EFAULT;
	}

	Q2SPI_DBG_2(q2spi, "%s cmd:%d data_len:%d addr:%d proto:%d ep:%d\n",
		    __func__, q2spi_req->cmd, q2spi_req->data_len, q2spi_req->addr,
		    q2spi_req->proto_ind, q2spi_req->end_point);
	Q2SPI_DBG_2(q2spi, "%s priority:%d flow_id:%d sync:%d\n",
		    __func__, q2spi_req->priority, q2spi_req->flow_id, q2spi_req->sync);

	if (!q2spi_cmd_type_valid(q2spi, q2spi_req))
		return -EINVAL;

	if (q2spi_req->addr > Q2SPI_SLAVE_END_ADDR) {
		Q2SPI_DEBUG(q2spi, "%s Err Invalid address:%x\n", __func__, q2spi_req->addr);
		return -EINVAL;
	}

	if (q2spi_req->reserved[0] & Q2SPI_SLEEP_CMD_BIT) {
		Q2SPI_DBG_1(q2spi, "%s allow_sleep\n", __func__);
		if (q2spi->q2spi_log_lvl)
			q2spi->q2spi_log_lvl = q2spi->q2spi_log_lvl;
		else
			q2spi->q2spi_log_lvl = LOG_DBG_LEVEL1;
		q2spi->q2spi_sleep_cmd_enable = true;
	} else {
		q2spi->q2spi_sleep_cmd_enable = false;
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
	struct q2spi_packet *cur_q2spi_pkt;
	void *data_buf = NULL, *user_buf = NULL;
	int ret, flow_id = 0;

	if (!filp || !buf || !len || !filp->private_data) {
		pr_err("%s Err Null pointer\n", __func__);
		return -EINVAL;
	}
	q2spi = filp->private_data;
	Q2SPI_DBG_1(q2spi, "In %s Enter PID=%d\n", __func__, current->pid);
	mutex_lock(&q2spi->port_lock);

	ret = q2spi_transfer_check(q2spi, &q2spi_req, buf, len);
	if (ret)
		goto err;

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
			ret = -ENOMEM;
			goto err;
		}

		if (copy_from_user(data_buf, q2spi_req.data_buff, q2spi_req.data_len)) {
			Q2SPI_DEBUG(q2spi, "%s Err copy_from_user failed\n", __func__);
			q2spi_kfree(q2spi, data_buf, __LINE__);
			ret = -EFAULT;
			goto err;
		}
		user_buf = q2spi_req.data_buff;
		q2spi_dump_ipc_always(q2spi, "q2spi_transfer", (char *)data_buf,
				      q2spi_req.data_len);
		q2spi_req.data_buff = data_buf;
	}

	if (atomic_read(&q2spi->doorbell_pending)) {
		Q2SPI_DBG_1(q2spi, "%s CR Doorbell Pending\n", __func__);
		usleep_range(1000, 2000);
	}

	Q2SPI_DBG_2(q2spi, "%s PM get_sync count:%d\n", __func__,
		    atomic_read(&q2spi->dev->power.usage_count));
	ret = pm_runtime_get_sync(q2spi->dev);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s Err for PM get\n", __func__);
		pm_runtime_put_noidle(q2spi->dev);
		pm_runtime_set_suspended(q2spi->dev);
		goto err;
	}

	q2spi->is_start_seq_fail = false;
	reinit_completion(&q2spi->wait_comp_start_fail);
	Q2SPI_DBG_2(q2spi, "%s PM after get_sync count:%d\n", __func__,
		    atomic_read(&q2spi->dev->power.usage_count));
	q2spi_wait_for_doorbell_setup_ready(q2spi);
	mutex_lock(&q2spi->queue_lock);
	reinit_completion(&q2spi->sma_wr_comp);
	flow_id = q2spi_add_req_to_tx_queue(q2spi, &q2spi_req, &cur_q2spi_pkt);
	mutex_unlock(&q2spi->queue_lock);
	if (flow_id < 0) {
		if (q2spi_req.data_buff)
			q2spi_kfree(q2spi, data_buf, __LINE__);
		Q2SPI_DEBUG(q2spi, "%s Err Failed to add tx request ret:%d\n", __func__, flow_id);
		pm_runtime_mark_last_busy(q2spi->dev);
		pm_runtime_put_autosuspend(q2spi->dev);
		Q2SPI_DEBUG(q2spi, "%s PM after put_autosuspend count:%d\n",
			    __func__, atomic_read(&q2spi->dev->power.usage_count));
		ret = -ENOMEM;
		goto err;
	}

	ret = q2spi_transfer_with_retries(q2spi, q2spi_req, cur_q2spi_pkt, len, flow_id, user_buf);
	Q2SPI_DBG_1(q2spi, "%s transfer_with_retries ret:%d\n", __func__, ret);

err:
	mutex_unlock(&q2spi->port_lock);
	return ret;
}

static ssize_t q2spi_response(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	struct q2spi_geni *q2spi;
	struct q2spi_client_request cr_request;
	struct q2spi_client_dma_pkt *q2spi_cr_var3;
	struct q2spi_packet *q2spi_pkt = NULL, *q2spi_pkt_tmp1, *q2spi_pkt_tmp2;
	int ret = 0;
	long timeout = 0;

	if (q2spi_sys_restart)
		return -ERESTARTSYS;

	if (!filp || !buf || !count || !filp->private_data) {
		pr_err("%s Err Null pointer\n", __func__);
		return -EINVAL;
	}

	q2spi = filp->private_data;

	Q2SPI_DBG_1(q2spi, "%s Enter PID=%d\n", __func__, current->pid);
	if (q2spi->hw_state_is_bad) {
		Q2SPI_DEBUG(q2spi, "%s Err Retries failed, check HW state\n", __func__);
		ret = -EPIPE;
		goto err;
	}

	if (q2spi->port_release) {
		Q2SPI_DEBUG(q2spi, "%s Err Port in closed state, return\n", __func__);
		ret = -ENOENT;
		goto err;
	}

	Q2SPI_DBG_2(q2spi, "%s PM get_sync count:%d\n", __func__,
		    atomic_read(&q2spi->dev->power.usage_count));
	ret = pm_runtime_get_sync(q2spi->dev);
	if (ret < 0) {
		Q2SPI_ERROR(q2spi, "%s Err for PM get\n", __func__);
		pm_runtime_put_noidle(q2spi->dev);
		pm_runtime_set_suspended(q2spi->dev);
		goto err;
	}
	Q2SPI_DBG_2(q2spi, "%s PM after get_sync count:%d\n", __func__,
		    atomic_read(&q2spi->dev->power.usage_count));
	q2spi_tx_queue_status(q2spi);
	if (copy_from_user(&cr_request, buf, sizeof(struct q2spi_client_request)) != 0) {
		Q2SPI_ERROR(q2spi, "%s Err copy from user failed PID=%d\n", __func__, current->pid);
		ret = -EFAULT;
		goto err;
	}

	Q2SPI_DBG_1(q2spi, "%s waiting on wait_event_interruptible rx_avail:%d\n",
		    __func__, atomic_read(&q2spi->rx_avail));
	/* Wait for Rx data available with timeout */
	timeout = wait_event_interruptible_timeout(q2spi->read_wq, atomic_read(&q2spi->rx_avail),
						   msecs_to_jiffies(Q2SPI_RESPONSE_WAIT_TIMEOUT));
	if (timeout <= 0) {
		Q2SPI_DEBUG(q2spi, "%s Err wait interrupted timeout:%ld\n", __func__, timeout);
		ret = -ETIMEDOUT;
		goto err;
	}
	atomic_dec(&q2spi->rx_avail);
	mutex_lock(&q2spi->queue_lock);
	list_for_each_entry_safe(q2spi_pkt_tmp1, q2spi_pkt_tmp2, &q2spi->tx_queue_list, list) {
		if (q2spi_pkt_tmp1->state == DATA_AVAIL) {
			q2spi_pkt = q2spi_pkt_tmp1;
			Q2SPI_DBG_1(q2spi, "%s q2spi_pkt %p data avail for user\n",
				    __func__, q2spi_pkt);
			break;
		}
		Q2SPI_DBG_1(q2spi, "%s check q2spi_pkt %p state:%s\n",
			    __func__, q2spi_pkt_tmp1, q2spi_pkt_state(q2spi_pkt_tmp1));
	}
	mutex_unlock(&q2spi->queue_lock);

	if (!q2spi_pkt) {
		Q2SPI_ERROR(q2spi, "%s Err No q2spi_pkt available\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	Q2SPI_DBG_2(q2spi, "%s Found q2spi_pkt = %p, cr_hdr_type:0x%x\n",
		    __func__, q2spi_pkt, q2spi_pkt->cr_hdr_type);
	if (q2spi_pkt->cr_hdr_type == CR_HDR_VAR3) {
		q2spi_cr_var3 = &q2spi_pkt->cr_var3;
		Q2SPI_DBG_2(q2spi, "q2spi_cr_var3 len_part1:%d len_part2:%d\n",
			    q2spi_cr_var3->dw_len_part1, q2spi_cr_var3->dw_len_part2);
		Q2SPI_DBG_2(q2spi, "q2spi_cr_var3 flow_id:%d arg1:0x%x arg2:0x%x arg3:0x%x\n",
			    q2spi_cr_var3->flow_id, q2spi_cr_var3->arg1, q2spi_cr_var3->arg2,
			    q2spi_cr_var3->arg3);
		/*
		 * Doorbell case tid will be updated by client.
		 * q2spi send the ID to userspce
		 * so that it will call HC with this flow id for async case
		 */
		cr_request.flow_id = q2spi_cr_var3->flow_id;
		cr_request.cmd = q2spi_pkt->cr_hdr.cmd;
		cr_request.data_len = q2spi_pkt->var3_data_len;
		cr_request.end_point = q2spi_cr_var3->arg2;
		cr_request.proto_ind = q2spi_cr_var3->arg3;
		Q2SPI_DBG_2(q2spi, "%s CR cmd:%d flow_id:%d len:%d ep:%d proto:%d status:%d\n",
			    __func__, cr_request.cmd, cr_request.flow_id, cr_request.data_len,
			    cr_request.end_point, cr_request.proto_ind, cr_request.status);
	} else if (q2spi_pkt->cr_hdr_type == CR_HDR_BULK) {
		Q2SPI_DBG_1(q2spi, "%s cr_request.flow_id:%d status:%d\n",
			    __func__, cr_request.flow_id, cr_request.status);
	} else {
		Q2SPI_ERROR(q2spi, "%s Err Unsupported CR Type\n", __func__);
		ret = -EINVAL;
		goto err;
	}
	Q2SPI_DBG_1(q2spi, "%s data_len:%d ep:%d proto:%d cmd%d status%d flow_id:%d",
		    __func__, cr_request.data_len, cr_request.end_point, cr_request.proto_ind,
		    cr_request.cmd, cr_request.status, cr_request.flow_id);
	if (!q2spi_pkt->xfer || !q2spi_pkt->xfer->rx_buf) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_pkt rx_buf is NULL\n", __func__);
		ret = -EAGAIN;
		goto err;
	}

	q2spi_dump_ipc_always(q2spi, "q2spi_response",
			      (char *)q2spi_pkt->xfer->rx_buf, cr_request.data_len);
	ret = copy_to_user(buf, &cr_request, sizeof(struct q2spi_client_request));
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err copy_to_user failed ret:%d", __func__, ret);
		ret = -EAGAIN;
		goto err;
	}
	ret = copy_to_user(cr_request.data_buff,
			   (void *)q2spi_pkt->xfer->rx_buf, cr_request.data_len);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err copy_to_user data_buff failed ret:%d", __func__, ret);
		ret = -EAGAIN;
		goto err;
	}
	ret = (sizeof(struct q2spi_client_request) - ret);

	q2spi_tx_queue_status(q2spi);
	Q2SPI_DBG_1(q2spi, "%s q2spi_pkt:%p state:%s\n",
		    __func__, q2spi_pkt, q2spi_pkt_state(q2spi_pkt));
	q2spi_unmap_rx_buf(q2spi_pkt);
	q2spi_pkt->state = IN_DELETION;
	if (q2spi_del_pkt_from_tx_queue(q2spi, q2spi_pkt))
		q2spi_free_q2spi_pkt(q2spi_pkt, __LINE__);

	Q2SPI_DBG_2(q2spi, "%s PM put_autosuspend count:%d line:%d\n", __func__,
		    atomic_read(&q2spi->dev->power.usage_count), __LINE__);
	pm_runtime_mark_last_busy(q2spi->dev);
	pm_runtime_put_autosuspend(q2spi->dev);
	Q2SPI_DBG_2(q2spi, "%s PM after put_autosuspend count:%d\n", __func__,
		    atomic_read(&q2spi->dev->power.usage_count));
err:
	Q2SPI_DBG_1(q2spi, "%s End ret:%d PID=%d", __func__, ret, current->pid);
	return ret;
}

static __poll_t q2spi_poll(struct file *filp, poll_table *wait)
{
	struct q2spi_geni *q2spi;
	__poll_t mask = 0;

	if (q2spi_sys_restart)
		return -ERESTARTSYS;

	if (!filp || !filp->private_data) {
		pr_err("%s Err Null pointer\n", __func__);
		return -EINVAL;
	}

	q2spi = filp->private_data;
	poll_wait(filp, &q2spi->readq, wait);
	Q2SPI_DBG_2(q2spi, "%s PID:%d\n", __func__, current->pid);
	if (atomic_read(&q2spi->rx_avail)) {
		mask = (POLLIN | POLLRDNORM);
		Q2SPI_DBG_1(q2spi, "%s RX data available\n", __func__);
	}
	return mask;
}

/**
 * q2spi_flush_pending_crs - check any pending CRs to consume
 * @q2spi: pointer to q2spi_geni driver data
 *
 * Return: none
 */
static void q2spi_flush_pending_crs(struct q2spi_geni *q2spi)
{
	struct q2spi_packet *q2spi_pkt = NULL, *q2spi_pkt_tmp;

	Q2SPI_DBG_1(q2spi, "%s: PID=%d\n", __func__, current->pid);
	/* Delay to ensure any pending CRs in progress are consumed */
	usleep_range(10000, 20000);
	q2spi_tx_queue_status(q2spi);

	mutex_lock(&q2spi->queue_lock);
	list_for_each_entry_safe(q2spi_pkt, q2spi_pkt_tmp, &q2spi->tx_queue_list, list) {
		if (q2spi_pkt->state == DATA_AVAIL || q2spi_pkt->state == IN_USE) {
			Q2SPI_DBG_1(q2spi, "%s q2spi_pkt %p data avail, force delete\n",
				    __func__, q2spi_pkt);
			q2spi_unmap_rx_buf(q2spi_pkt);
			q2spi_pkt->state = IN_DELETION;
			atomic_dec(&q2spi->rx_avail);
			list_del(&q2spi_pkt->list);
			q2spi_free_q2spi_pkt(q2spi_pkt, __LINE__);
		} else {
			Q2SPI_DBG_1(q2spi, "%s Check q2spi_pkt %p state:%s!!!\n",
				    __func__, q2spi_pkt, q2spi_pkt_state(q2spi_pkt));
		}
	}
	mutex_unlock(&q2spi->queue_lock);
}

static int q2spi_release(struct inode *inode, struct file *filp)
{
	int retries = Q2SPI_RESP_BUF_RETRIES;
	struct q2spi_geni *q2spi;
	int ret = 0;

	if (q2spi_sys_restart)
		return -ERESTARTSYS;

	if (!filp || !filp->private_data) {
		pr_err("%s Err close return\n", __func__);
		return -EINVAL;
	}
	q2spi = filp->private_data;

	q2spi->q2spi_log_lvl = LOG_DBG_LEVEL0;
	Q2SPI_DEBUG(q2spi, "%s PID:%d allocs:%d\n",
		    __func__, current->pid, atomic_read(&q2spi->alloc_count));
	mutex_lock(&q2spi->port_lock);
	del_timer_sync(&q2spi->slave_sleep_timer);
	atomic_set(&q2spi->sma_wr_pending, 0);
	atomic_set(&q2spi->sma_rd_pending, 0);

	q2spi->hw_state_is_bad = false;

	if (mutex_is_locked(&q2spi->send_msgs_lock)) {
		Q2SPI_DBG_1(q2spi, "%s q2spi_transfer is in progress\n", __func__);
		usleep_range(200000, 250000);
	}

	q2spi_flush_pending_crs(q2spi);
	atomic_set(&q2spi->rx_avail, 0);
	q2spi->doorbell_setup = false;

	q2spi_tx_queue_status(q2spi);
	atomic_set(&q2spi->doorbell_pending, 0);
	atomic_set(&q2spi->retry, 0);
	while (retries--) {
		if (q2spi->sys_mem_read_in_progress) {
			/* sleep sometime to complete pending system memory read requests */
			usleep_range(150000, 200000);
		} else {
			break;
		}
	}
	mutex_unlock(&q2spi->port_lock);
	if (!atomic_read(&q2spi->is_suspend)) {
		ret = pm_runtime_suspend(q2spi->dev);
		Q2SPI_DBG_1(q2spi, "%s suspend ret:%d sys_mem_read_in_progress:%d\n",
			    __func__, ret,  q2spi->sys_mem_read_in_progress);
	}
	q2spi->port_release = true;

	ret = pinctrl_select_state(q2spi->geni_pinctrl, q2spi->geni_gpio_shutdown);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s: Err failed to pinctrl state to gpio, ret:%d\n",
			    __func__, ret);
	}

	Q2SPI_DBG_1(q2spi, "%s End allocs:%d rx_avail:%d retry:%d slave_in_sleep:%d\n",
		    __func__, atomic_read(&q2spi->alloc_count), atomic_read(&q2spi->rx_avail),
		    atomic_read(&q2spi->retry), atomic_read(&q2spi->slave_in_sleep));
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

	Q2SPI_DBG_1(q2spi, "%s req speed:%u resultant:%lu sclk:%lu, idx:%d, div:%d\n",
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
	u32 clk_sel, idx, div;
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
	q2spi->m_clk_cfg = (div << CLK_DIV_SHFT) | SER_CLK_EN;
	writel(clk_sel, se->base + SE_GENI_CLK_SEL);
	writel(q2spi->m_clk_cfg, se->base + GENI_SER_M_CLK_CFG);

	Q2SPI_DBG_1(q2spi, "%s speed_hz:%u clk_sel:0x%x m_clk_cfg:0x%x div:%d\n",
		    __func__, q2spi->cur_speed_hz, clk_sel, q2spi->m_clk_cfg, div);
	return ret;
}

void q2spi_geni_se_dump_regs(struct q2spi_geni *q2spi)
{
	mutex_lock(&q2spi->geni_resource_lock);
	if (!q2spi->resources_on) {
		Q2SPI_DEBUG(q2spi, "%s: Err cannot dump, resources are off!!!\n", __func__);
		mutex_unlock(&q2spi->geni_resource_lock);
		return;
	}
	Q2SPI_DBG_1(q2spi, "GENI_GENERAL_CFG: 0x%x\n",
		    geni_read_reg(q2spi->base, GENI_GENERAL_CFG));
	Q2SPI_DBG_1(q2spi, "GENI_OUTPUT_CTRL: 0x%x\n",
		    geni_read_reg(q2spi->base, GENI_OUTPUT_CTRL));
	Q2SPI_DBG_1(q2spi, "GENI_STATUS: 0x%x\n", geni_read_reg(q2spi->base, SE_GENI_STATUS));
	Q2SPI_DBG_1(q2spi, "GENI_CLK_CTRL_RO: 0x%x\n",
		    geni_read_reg(q2spi->base, GENI_CLK_CTRL_RO));
	Q2SPI_DBG_1(q2spi, "GENI_FW_MULTILOCK_MSA_RO: 0x%x\n",
		    geni_read_reg(q2spi->base, GENI_FW_MULTILOCK_MSA_RO));
	Q2SPI_DBG_1(q2spi, "GENI_IF_DISABLE_RO: 0x%x\n",
		    geni_read_reg(q2spi->base, GENI_IF_DISABLE_RO));
	Q2SPI_DBG_1(q2spi, "SE_GENI_CLK_SEL: 0x%x\n", geni_read_reg(q2spi->base, SE_GENI_CLK_SEL));
	Q2SPI_DBG_1(q2spi, "SPI_TRANS_CFG: 0x%x\n", geni_read_reg(q2spi->base, SE_SPI_TRANS_CFG));
	Q2SPI_DBG_1(q2spi, "SE_GENI_IOS: 0x%x\n", geni_read_reg(q2spi->base, SE_GENI_IOS));
	Q2SPI_DBG_1(q2spi, "SE_GENI_M_CMD0: 0x%x\n", geni_read_reg(q2spi->base, SE_GENI_M_CMD0));
	Q2SPI_DBG_1(q2spi, "GENI_M_CMD_CTRL_REG: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_GENI_M_CMD_CTRL_REG));
	Q2SPI_DBG_1(q2spi, "GENI_M_IRQ_STATUS: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_GENI_M_IRQ_STATUS));
	Q2SPI_DBG_1(q2spi, "GENI_M_IRQ_EN: 0x%x\n", geni_read_reg(q2spi->base, SE_GENI_M_IRQ_EN));
	Q2SPI_DBG_1(q2spi, "GENI_TX_FIFO_STATUS: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_GENI_TX_FIFO_STATUS));
	Q2SPI_DBG_1(q2spi, "GENI_RX_FIFO_STATUS: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_GENI_RX_FIFO_STATUS));
	Q2SPI_DBG_1(q2spi, "DMA_TX_PTR_L: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_TX_PTR_L));
	Q2SPI_DBG_1(q2spi, "DMA_TX_PTR_H: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_TX_PTR_H));
	Q2SPI_DBG_1(q2spi, "DMA_TX_ATTR: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_TX_ATTR));
	Q2SPI_DBG_1(q2spi, "DMA_TX_LEN: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_TX_LEN));
	Q2SPI_DBG_1(q2spi, "DMA_TX_IRQ_STAT: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_DMA_TX_IRQ_STAT));
	Q2SPI_DBG_1(q2spi, "DMA_TX_IRQ_EN: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_DMA_TX_IRQ_EN));
	Q2SPI_DBG_1(q2spi, "DMA_TX_LEN_IN: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_TX_LEN_IN));
	Q2SPI_DBG_1(q2spi, "DMA_RX_IRQ_EN: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_DMA_RX_IRQ_EN));
	Q2SPI_DBG_1(q2spi, "DMA_RX_PTR_L: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_RX_PTR_L));
	Q2SPI_DBG_1(q2spi, "DMA_RX_PTR_H: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_RX_PTR_H));
	Q2SPI_DBG_1(q2spi, "DMA_RX_ATTR: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_RX_ATTR));
	Q2SPI_DBG_1(q2spi, "DMA_RX_LEN: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_RX_LEN));
	Q2SPI_DBG_1(q2spi, "DMA_RX_IRQ_STAT: 0x%x\n",
		    geni_read_reg(q2spi->base, SE_DMA_RX_IRQ_STAT));
	Q2SPI_DBG_1(q2spi, "DMA_RX_LEN_IN: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_RX_LEN_IN));
	Q2SPI_DBG_1(q2spi, "DMA_DEBUG_REG0: 0x%x\n", geni_read_reg(q2spi->base, SE_DMA_DEBUG_REG0));
	Q2SPI_DBG_1(q2spi, "SE_GSI_EVENT_EN: 0x%x\n", geni_read_reg(q2spi->base, SE_GSI_EVENT_EN));
	Q2SPI_DBG_1(q2spi, "SE_IRQ_EN: 0x%x\n", geni_read_reg(q2spi->base, SE_IRQ_EN));
	Q2SPI_DBG_1(q2spi, "DMA_IF_EN_RO: 0x%x\n", geni_read_reg(q2spi->base, DMA_IF_EN_RO));
	mutex_unlock(&q2spi->geni_resource_lock);
}

static irqreturn_t q2spi_geni_wakeup_isr(int irq, void *data)
{
	struct q2spi_geni *q2spi = data;

	Q2SPI_DBG_1(q2spi, "%s PID:%d\n", __func__, current->pid);
	irq_set_irq_type(q2spi->doorbell_irq, IRQ_TYPE_EDGE_RISING);
	atomic_set(&q2spi->slave_in_sleep, 0);
	schedule_work(&q2spi->q2spi_wakeup_work);
	return IRQ_HANDLED;
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
	Q2SPI_DBG_1(q2spi, "%s sirq 0x%x mirq:0x%x dma_tx:0x%x dma_rx:0x%x\n",
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
		Q2SPI_DEBUG(q2spi, "Err SLAVE_ERROR Reg read failed: %d\n", ret);

	ret = q2spi_read_reg(q2spi, Q2SPI_HDR_ERROR);
	if (ret)
		Q2SPI_DEBUG(q2spi, "Err HDR_ERROR Reg read failed: %d\n", ret);
}

static int q2spi_gsi_submit(struct q2spi_packet *q2spi_pkt)
{
	struct q2spi_geni *q2spi = q2spi_pkt->q2spi;
	struct q2spi_dma_transfer *xfer = q2spi_pkt->xfer;
	int ret = 0;

	Q2SPI_DBG_2(q2spi, "%s PID:%d q2spi:%p xfer:%p wait for gsi_lock 2\n",
		    __func__, current->pid, q2spi, xfer);
	mutex_lock(&q2spi->gsi_lock);
	Q2SPI_DBG_2(q2spi, "%s PID=%d acquired gsi_lock 2\n", __func__, current->pid);
	ret = q2spi_setup_gsi_xfer(q2spi_pkt);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_setup_gsi_xfer failed: %d\n", __func__, ret);
		atomic_set(&q2spi->sma_wr_pending, 0);
		del_timer_sync(&q2spi->slave_sleep_timer);
		goto unmap_buf;
	}
	Q2SPI_DBG_2(q2spi, "%s PID:%d waiting check_gsi_transfer_completion\n",
		    __func__, current->pid);
	ret = check_gsi_transfer_completion(q2spi);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s PID:%d Err completion timeout: %d\n",
			    __func__, current->pid, ret);
		atomic_set(&q2spi->sma_wr_pending, 0);
		del_timer_sync(&q2spi->slave_sleep_timer);
		goto unmap_buf;
	}

	Q2SPI_DBG_1(q2spi, "%s End PID:%d flow_id:%d tx_dma:%p rx_dma:%p, relased gsi_lock 2",
		    __func__,  current->pid, q2spi_pkt->xfer->tid, (void *)xfer->tx_dma,
		    (void *)xfer->rx_dma);
unmap_buf:
	mutex_unlock(&q2spi->gsi_lock);
	q2spi_unmap_dma_buf_used(q2spi, xfer->tx_dma, xfer->rx_dma);
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
	struct q2spi_dma_transfer *reset_xfer = q2spi_pkt->xfer;

	Q2SPI_DBG_1(q2spi, "%s q2spi_pkt->soft_reset_pkt:%p &q2spi_pkt->soft_reset_pkt:%p\n",
		    __func__, q2spi_pkt->soft_reset_pkt, &q2spi_pkt->soft_reset_pkt);
	reset_xfer->cmd = q2spi_pkt->m_cmd_param;
	reset_pkt = q2spi_pkt->soft_reset_pkt;
	reset_xfer->tx_buf = q2spi_pkt->soft_reset_pkt;
	reset_xfer->tx_dma = q2spi_pkt->soft_reset_tx_dma;
	reset_xfer->tx_data_len = 0;
	reset_xfer->tx_len = Q2SPI_HEADER_LEN;
	Q2SPI_DBG_1(q2spi, "%s var1_xfer->tx_len:%d var1_xfer->tx_data_len:%d\n",
		    __func__, reset_xfer->tx_len, reset_xfer->tx_data_len);

	Q2SPI_DBG_1(q2spi, "%s tx_buf:%p tx_dma:%p\n", __func__,
		    reset_xfer->tx_buf, (void *)reset_xfer->tx_dma);
	q2spi_dump_ipc(q2spi, "Preparing soft reset tx_buf DMA TX",
		       (char *)reset_xfer->tx_buf, reset_xfer->tx_len);
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
	struct q2spi_dma_transfer *var1_xfer = q2spi_pkt->xfer;

	Q2SPI_DBG_2(q2spi, "%s q2spi_pkt->var1_pkt:%p\n", __func__, q2spi_pkt->var1_pkt);
	var1_xfer->cmd = q2spi_pkt->m_cmd_param;
	q2spi_hc_var1 = q2spi_pkt->var1_pkt;
	var1_xfer->tx_buf = q2spi_pkt->var1_pkt;
	var1_xfer->tx_dma = q2spi_pkt->var1_tx_dma;
	var1_xfer->tx_data_len = (q2spi_pkt->var1_pkt->dw_len * 4) + 4;
	var1_xfer->tx_len = Q2SPI_HEADER_LEN + var1_xfer->tx_data_len;
	Q2SPI_DBG_1(q2spi, "%s var1_xfer->tx_len:%d var1_xfer->tx_data_len:%d\n",
		    __func__, var1_xfer->tx_len, var1_xfer->tx_data_len);
	var1_xfer->tid = q2spi_pkt->var1_pkt->flow_id;
	if (q2spi_pkt->m_cmd_param == Q2SPI_TX_RX) {
		var1_xfer->tx_len = Q2SPI_HEADER_LEN;
		Q2SPI_DBG_2(q2spi, "%s var1_xfer->tx_len:%d var1_xfer->tx_data_len:%d\n",
			    __func__, var1_xfer->tx_len, var1_xfer->tx_data_len);
		var1_xfer->rx_buf = q2spi_pkt->xfer->rx_buf;
		var1_xfer->rx_dma = q2spi_pkt->xfer->rx_dma;
		q2spi_pkt->var1_rx_dma = var1_xfer->rx_dma;
		var1_xfer->rx_data_len = (q2spi_pkt->var1_pkt->dw_len * 4) + 4;
		var1_xfer->rx_len = var1_xfer->rx_data_len;
		Q2SPI_DBG_1(q2spi, "%s var1_xfer->rx_len:%d var1_xfer->rx_data_len:%d\n",
			    __func__, var1_xfer->rx_len, var1_xfer->rx_data_len);
	}

	Q2SPI_DBG_1(q2spi, "%s tx_buf:%p tx_dma:%p rx_buf:%p rx_dma:%p\n", __func__,
		    var1_xfer->tx_buf, (void *)var1_xfer->tx_dma,
		    var1_xfer->rx_buf, (void *)var1_xfer->rx_dma);
	q2spi_dump_ipc(q2spi, "Preparing var1 tx_buf DMA TX",
		       (char *)var1_xfer->tx_buf, var1_xfer->tx_len);
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
	struct q2spi_dma_transfer *var5_xfer = q2spi_pkt->xfer;

	Q2SPI_DBG_2(q2spi, "%s q2spi_pkt->var5_pkt:%p var5_tx_dma:%p\n",
		    __func__, q2spi_pkt->var5_pkt, (void *)q2spi_pkt->var5_tx_dma);
	q2spi_hc_var5 = q2spi_pkt->var5_pkt;
	var5_xfer->cmd = q2spi_pkt->m_cmd_param;
	var5_xfer->tx_buf = q2spi_pkt->var5_pkt;
	var5_xfer->tx_dma = q2spi_pkt->var5_tx_dma;
	var5_xfer->tid = q2spi_pkt->var5_pkt->flow_id;
	var5_xfer->tx_data_len = q2spi_pkt->data_length;
	var5_xfer->tx_len = Q2SPI_HEADER_LEN + var5_xfer->tx_data_len;
	Q2SPI_DBG_1(q2spi, "%s var5_xfer->tx_len:%d var5_xfer->tx_data_len:%d\n",
		    __func__, var5_xfer->tx_len, var5_xfer->tx_data_len);
	if (q2spi_pkt->m_cmd_param == Q2SPI_TX_RX) {
		var5_xfer->rx_buf = q2spi_pkt->xfer->rx_buf;
		var5_xfer->rx_dma = q2spi_pkt->xfer->rx_dma;
		q2spi_pkt->var5_rx_dma = var5_xfer->rx_dma;
		var5_xfer->tx_len = Q2SPI_HEADER_LEN;
		var5_xfer->rx_len =
			((q2spi_pkt->var5_pkt->dw_len_part1 |
			q2spi_pkt->var5_pkt->dw_len_part2 << 2) * 4) + 4;
		var5_xfer->rx_data_len = q2spi_pkt->data_length;
		Q2SPI_DBG_2(q2spi, "%s var5_pkt:%p cmd:%d flow_id:0x%x len_part1:%d len_part2:%d\n",
			    __func__, q2spi_pkt->var5_pkt, q2spi_pkt->var5_pkt->cmd,
			    q2spi_pkt->var5_pkt->flow_id, q2spi_pkt->var5_pkt->dw_len_part1,
			    q2spi_pkt->var5_pkt->dw_len_part2);
		Q2SPI_DBG_1(q2spi, "%s var5_pkt data_buf:%p var5_xfer->rx_len:%d\n",
			    __func__, q2spi_pkt->var5_pkt->data_buf, var5_xfer->rx_len);
	}
	Q2SPI_DBG_1(q2spi, "%s tx_buf:%p tx_dma:%p rx_buf:%p rx_dma:%p\n", __func__,
		    var5_xfer->tx_buf, (void *)var5_xfer->tx_dma,
		    var5_xfer->rx_buf, (void *)var5_xfer->rx_dma);
	q2spi_dump_ipc(q2spi, "Preparing var5 tx_buf DMA TX",
		       (char *)var5_xfer->tx_buf, Q2SPI_HEADER_LEN);
	if (q2spi_pkt->m_cmd_param == Q2SPI_TX_ONLY)
		q2spi_dump_ipc(q2spi, "Preparing var5 data_buf DMA TX",
			       (void *)q2spi_pkt->var5_pkt->data_buf,
			       var5_xfer->tx_data_len);

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
	struct q2spi_dma_transfer *var1_xfer = q2spi_pkt->xfer;

	q2spi_hc_var1 = q2spi_pkt->var1_pkt;
	var1_xfer->cmd = Q2SPI_TX_ONLY;
	var1_xfer->tx_buf = q2spi_pkt->var1_pkt;
	var1_xfer->tx_dma = q2spi_pkt->var1_tx_dma;
	var1_xfer->tx_data_len = 16;
	var1_xfer->tx_len = Q2SPI_HEADER_LEN + var1_xfer->tx_data_len;
	var1_xfer->tid = q2spi_pkt->var1_pkt->flow_id;
	var1_xfer->rx_len = RX_DMA_CR_BUF_SIZE;
	Q2SPI_DBG_2(q2spi, "%s var1_pkt:%p var1_pkt_phy:%p cmd:%d addr:0x%x flow_id:0x%x\n",
		    __func__, q2spi_pkt->var1_pkt,
		    (void *)q2spi_pkt->var1_tx_dma, q2spi_pkt->var1_pkt->cmd,
		    q2spi_pkt->var1_pkt->reg_offset, q2spi_pkt->var1_pkt->flow_id);
	Q2SPI_DBG_1(q2spi, "%s var1_pkt: len:%d data_buf %p\n",
		    __func__, q2spi_pkt->var1_pkt->dw_len, q2spi_pkt->var1_pkt->data_buf);
	Q2SPI_DBG_2(q2spi, "%s tx_buf:%p tx_dma:%p rx_buf:%p rx_dma:%p\n",
		    __func__, var1_xfer->tx_buf, (void *)var1_xfer->tx_dma,
		    var1_xfer->rx_buf, (void *)var1_xfer->rx_dma);
	q2spi_dump_ipc(q2spi, "Preparing var1_HRF DMA TX",
		       (char *)var1_xfer->tx_buf, var1_xfer->tx_len);
	return 0;
}

int q2spi_process_hrf_flow_after_lra(struct q2spi_geni *q2spi, struct q2spi_packet *q2spi_pkt)
{
	unsigned long xfer_timeout = 0;
	long timeout = 0;
	int ret = -1;

	Q2SPI_DBG_1(q2spi, "%s VAR1 wait for doorbell\n", __func__);
	/* Make sure we get the doorbell before continuing for HRF flow */
	xfer_timeout = msecs_to_jiffies(XFER_TIMEOUT_OFFSET);
	timeout = wait_for_completion_interruptible_timeout(&q2spi_pkt->wait_for_db, xfer_timeout);
	if (timeout <= 0) {
		Q2SPI_DEBUG(q2spi, "%s Err timeout for doorbell_wait timeout:%ld\n",
			    __func__, timeout);
		if (timeout == -ERESTARTSYS) {
			q2spi_sys_restart = true;
			return -ERESTARTSYS;
		}
		return -ETIMEDOUT;
	}

	Q2SPI_DBG_1(q2spi, "%s q2spi_pkt:%p flow_id:%d cr_flow_id:%d\n", __func__,
		    q2spi_pkt, q2spi_pkt->flow_id, q2spi_pkt->cr_var3.flow_id);
	if (q2spi_pkt->flow_id == q2spi_pkt->cr_var3.flow_id) {
		q2spi_pkt->vtype = VARIANT_5;
		ret = q2spi_prep_var5_request(q2spi, q2spi_pkt);
		if (ret)
			return ret;
		ret = q2spi_gsi_submit(q2spi_pkt);
		if (ret) {
			Q2SPI_DEBUG(q2spi, "%s Err q2spi_gsi_submit failed: %d\n", __func__, ret);
			return ret;
		}
		Q2SPI_DBG_2(q2spi, "%s wakeup sma_wr_comp\n", __func__);
		complete_all(&q2spi->sma_wr_comp);
		atomic_set(&q2spi->sma_wr_pending, 0);
	} else {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_pkt:%p flow_id:%d != cr_flow_id:%d\n",
			    __func__, q2spi_pkt, q2spi_pkt->flow_id, q2spi_pkt->cr_var3.flow_id);
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
int __q2spi_send_messages(struct q2spi_geni *q2spi, void *ptr)
{
	struct q2spi_packet *q2spi_pkt = NULL, *q2spi_pkt_tmp1, *q2spi_pkt_tmp2;
	int ret = 0;
	bool cm_flow_pkt = false;

	if (ptr)
		Q2SPI_DBG_2(q2spi, "Enter %s for %p\n", __func__, ptr);
	else
		Q2SPI_DBG_2(q2spi, "Enter %s PID %d\n", __func__, current->pid);

	if (q2spi->port_release) {
		Q2SPI_DEBUG(q2spi, "%s Err Port in closed state, return\n", __func__);
		return -ENOENT;
	}

	mutex_lock(&q2spi->send_msgs_lock);
	/* Check if the queue is idle */
	if (list_empty(&q2spi->tx_queue_list)) {
		Q2SPI_DEBUG(q2spi, "%s Tx queue list is empty\n", __func__);
		goto send_msg_exit;
	}

	/* Check if we need take a lock and frame the Q2SPI packet */
	/* if the list is not empty call q2spi_gsi_transfer msg to submit the transfer to GSI */
	mutex_lock(&q2spi->queue_lock);
	list_for_each_entry_safe(q2spi_pkt_tmp1, q2spi_pkt_tmp2, &q2spi->tx_queue_list, list) {
		if (q2spi_pkt_tmp1->state == NOT_IN_USE &&
		    q2spi_pkt_tmp1 == (struct q2spi_packet *)ptr) {
			q2spi_pkt = q2spi_pkt_tmp1;
			Q2SPI_DBG_1(q2spi, "%s sending q2spi_pkt %p state:%s\n",
				    __func__, q2spi_pkt, q2spi_pkt_state(q2spi_pkt));
			break;
		}
		Q2SPI_DBG_1(q2spi, "%s check q2spi_pkt %p state:%s\n",
			    __func__, q2spi_pkt_tmp1, q2spi_pkt_state(q2spi_pkt_tmp1));
	}
	mutex_unlock(&q2spi->queue_lock);

	if (!q2spi_pkt) {
		Q2SPI_DEBUG(q2spi, "%s Err couldnt find free q2spi pkt in tx queue!!!\n", __func__);
		goto send_msg_exit;
	}
	if (q2spi_pkt->is_client_sleep_pkt && atomic_read(&q2spi->sleep_cmd_sent)) {
		ret = -EINVAL;
		goto send_msg_exit;
	}

	q2spi_pkt->state = IN_USE;
	if (!q2spi_pkt) {
		Q2SPI_DEBUG(q2spi, "%s q2spi_pkt is NULL\n", __func__);
		ret = -EAGAIN;
		goto send_msg_exit;
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
		goto send_msg_exit;

	Q2SPI_DBG_1(q2spi, "%s q2spi_pkt->vtype=%d cr_hdr_type=%d\n",
		    __func__, q2spi_pkt->vtype, q2spi_pkt->cr_hdr_type);
	if (q2spi_pkt->vtype == VARIANT_5) {
		if (q2spi_pkt->var5_pkt->flow_id >= Q2SPI_END_TID_ID) {
			cm_flow_pkt = true;
			Q2SPI_DBG_1(q2spi, "%s flow_id:%d\n", __func__,
				    q2spi_pkt->var5_pkt->flow_id);
		}
	}

	if (!cm_flow_pkt && atomic_read(&q2spi->doorbell_pending))
		Q2SPI_DBG_1(q2spi, "%s cm_flow_pkt:%d doorbell_pending:%d\n",
			    __func__, cm_flow_pkt, atomic_read(&q2spi->doorbell_pending));

	ret = q2spi_gsi_submit(q2spi_pkt);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_gsi_submit failed: %d\n", __func__, ret);
		q2spi_unmap_var_bufs(q2spi, q2spi_pkt);
		goto send_msg_exit;
	}

	if (q2spi_pkt->vtype == VARIANT_5) {
		Q2SPI_DBG_2(q2spi, "%s wakeup sma_wait\n", __func__);
		complete_all(&q2spi->sma_wait);
		Q2SPI_DBG_2(q2spi, "%s wakeup sma_rd_comp\n", __func__);
		complete_all(&q2spi->sma_rd_comp);
		atomic_set(&q2spi->sma_rd_pending, 0);
	}

	/* add 2msec delay for slave to complete sleep process after it received a sleep packet */
	if (q2spi_pkt->is_client_sleep_pkt) {
		usleep_range(2000, 3000);
		atomic_set(&q2spi->sleep_cmd_sent, 1);
	}
send_msg_exit:
	mutex_unlock(&q2spi->send_msgs_lock);
	if (atomic_read(&q2spi->sma_rd_pending))
		atomic_set(&q2spi->sma_rd_pending, 0);
	Q2SPI_DBG_2(q2spi, "%s: line:%d End\n", __func__, __LINE__);
	return ret;
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

	ret = __q2spi_send_messages(q2spi, NULL);
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
		Q2SPI_DEBUG(q2spi, "%s Err set clock failed\n", __func__);
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
	Q2SPI_DBG_2(q2spi, "tx_cfg: 0x%x io3_sel:0x%x spi_delay: 0x%x cfg_95:0x%x\n",
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
	Q2SPI_DBG_2(q2spi, "cfg_103: 0x%x cfg_104:0x%x pre_post_dly;0x%x spi_word_len:0x%x\n",
		    geni_read_reg(q2spi->base, SE_GENI_CFG_REG103),
		    geni_read_reg(q2spi->base, SE_GENI_CFG_REG104),
		    pre_post_dly, geni_read_reg(q2spi->base, SE_SPI_WORD_LEN));
	io3_sel &= ~OTHER_IO_OE;
	io3_sel |= (IO_MACRO_IO3_DATA_IN_SEL << IO_MACRO_IO3_DATA_IN_SEL_SHIFT) &
				IO_MACRO_IO3_DATA_IN_SEL_MASK;
	Q2SPI_DBG_2(q2spi, "io3_sel:0x%x %lx TPM:0x%x %d\n", io3_sel,
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
	if (proto != GENI_SE_Q2SPI_PROTO) {
		Q2SPI_ERROR(q2spi, "Err Invalid proto %d\n", proto);
		return -EINVAL;
	}

	ver = geni_se_get_qup_hw_version(&q2spi->se);
	major = GENI_SE_VERSION_MAJOR(ver);
	minor = GENI_SE_VERSION_MINOR(ver);
	Q2SPI_DBG_2(q2spi, "%s ver:0x%x major:%d minor:%d\n", __func__, ver, major, minor);

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
	Q2SPI_DBG_1(q2spi, "%s gsi_mode:%d xfer_mode:%d ret:%d\n",
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
	struct geni_se *se = NULL;
	int ret = 0;

	if (q2spi_sys_restart)
		return;

	se = &q2spi->se;
	mutex_lock(&q2spi->geni_resource_lock);
	if (!q2spi->resources_on) {
		Q2SPI_DBG_1(q2spi, "%s: Resources already off\n", __func__);
		goto exit_resource_off;
	}

	q2spi->resources_on = false;

	writel(0x1, se->base + GENI_SER_M_CLK_CFG);
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

exit_resource_off:
	mutex_unlock(&q2spi->geni_resource_lock);
	Q2SPI_DBG_1(q2spi, "%s: ret:%d\n", __func__, ret);
}

/**
 * q2spi_geni_resources_on - turns on geni resources
 * @q2spi: pointer to q2spi_geni driver data
 *
 * Return: 0 on success. Error code on failure.
 */
int q2spi_geni_resources_on(struct q2spi_geni *q2spi)
{
	struct geni_se *se = &q2spi->se;
	int ret = 0;

	mutex_lock(&q2spi->geni_resource_lock);
	Q2SPI_DBG_2(q2spi, "%s PID=%d\n", __func__, current->pid);
	if (q2spi->resources_on) {
		Q2SPI_DBG_1(q2spi, "%s: Resources already on\n", __func__);
		goto exit_resource_on;
	}

	ret = geni_icc_enable(&q2spi->se);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s: Err icc enable failed, ret:%d\n", __func__, ret);
		goto exit_resource_on;
	}

	ret = pinctrl_select_state(q2spi->geni_pinctrl,	q2spi->geni_gpio_active);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s: Err failed to pinctrl state to active, ret:%d\n",
			    __func__, ret);
		goto exit_resource_on;
	}

	/* Enable m_ahb, s_ahb and se clks */
	ret = geni_se_common_clks_on(q2spi->se.clk, q2spi->m_ahb_clk, q2spi->s_ahb_clk);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s: Err set common_clk_on failed, ret:%d\n", __func__, ret);
		goto exit_resource_on;
	}

	writel(q2spi->m_clk_cfg, se->base + GENI_SER_M_CLK_CFG);
	q2spi->resources_on = true;

exit_resource_on:
	mutex_unlock(&q2spi->geni_resource_lock);
	Q2SPI_DBG_1(q2spi, "%s: ret:%d\n", __func__, ret);
	return ret;
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
		Q2SPI_DEBUG(q2spi, "Err geni_se_resources_init\n");
		goto get_icc_pinctrl_err;
	}
	Q2SPI_DBG_1(q2spi, "%s GENI_TO_CORE:%d CPU_TO_GENI:%d GENI_TO_DDR:%d\n",
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

	q2spi->geni_gpio_default = pinctrl_lookup_state(q2spi->geni_pinctrl, PINCTRL_DEFAULT);
	if (IS_ERR_OR_NULL(q2spi->geni_gpio_default)) {
		Q2SPI_DEBUG(q2spi, "No default config specified!\n");
		ret = PTR_ERR(q2spi->geni_gpio_default);
		goto get_icc_pinctrl_err;
	}

	q2spi->geni_gpio_active = pinctrl_lookup_state(q2spi->geni_pinctrl, PINCTRL_ACTIVE);
	if (IS_ERR_OR_NULL(q2spi->geni_gpio_active)) {
		Q2SPI_DEBUG(q2spi, "No active config specified!\n");
		ret = PTR_ERR(q2spi->geni_gpio_active);
		goto get_icc_pinctrl_err;
	}

	q2spi->geni_gpio_sleep = pinctrl_lookup_state(q2spi->geni_pinctrl, PINCTRL_SLEEP);
	if (IS_ERR_OR_NULL(q2spi->geni_gpio_sleep)) {
		Q2SPI_DEBUG(q2spi, "No sleep config specified!\n");
		ret = PTR_ERR(q2spi->geni_gpio_sleep);
		goto get_icc_pinctrl_err;
	}

	q2spi->geni_gpio_shutdown = pinctrl_lookup_state(q2spi->geni_pinctrl, PINCTRL_SHUTDOWN);
	if (IS_ERR_OR_NULL(q2spi->geni_gpio_shutdown)) {
		Q2SPI_DEBUG(q2spi, "No shutdown config specified!\n");
		ret = PTR_ERR(q2spi->geni_gpio_shutdown);
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

	ret = alloc_chrdev_region(&q2spi->chrdev.q2spi_dev, 0, Q2SPI_MAX_DEV, "q2spidev");
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s ret:%d\n", __func__, ret);
		return ret;
	}
	q2spi_cdev_major = MAJOR(q2spi->chrdev.q2spi_dev);
	q2spi->chrdev.q2spi_class = class_create(THIS_MODULE, "q2spidev");
	if (IS_ERR(q2spi->chrdev.q2spi_class)) {
		Q2SPI_DEBUG(q2spi, "%s ret:%lx\n", __func__, PTR_ERR(q2spi->chrdev.q2spi_class));
		ret = PTR_ERR(q2spi->chrdev.q2spi_class);
		goto err_class_create;
	}

	for (i = 0; i < Q2SPI_MAX_DEV; i++) {
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
		Q2SPI_DBG_2(q2spi, "%s q2spi:%p i:%d end\n", __func__, q2spi, i);
	}

	return 0;
err_dev_create:
	for (i = 0; i < Q2SPI_MAX_DEV; i++)
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
	int ret = 0;

	q2spi_req.cmd = LOCAL_REG_READ;
	q2spi_req.addr = reg_offset;
	q2spi_req.data_len = 4; /* In bytes */

	ret = q2spi_frame_lra(q2spi, &q2spi_req, &q2spi_pkt, VARIANT_1_LRA);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "Err q2spi_frame_lra failed ret:%d\n", ret);
		return ret;
	}
	xfer = q2spi_pkt->xfer;
	xfer->tx_buf = q2spi_pkt->var1_pkt;
	xfer->tx_dma = q2spi_pkt->var1_tx_dma;
	xfer->rx_buf = q2spi_pkt->xfer->rx_buf;
	xfer->rx_dma = q2spi_pkt->xfer->rx_dma;
	xfer->cmd = q2spi_pkt->m_cmd_param;
	Q2SPI_DBG_1(q2spi, "%s q2spi_pkt:%p cmd:%d\n", __func__, q2spi_pkt, xfer->cmd);
	xfer->tx_data_len = q2spi_req.data_len;
	xfer->tx_len = Q2SPI_HEADER_LEN;
	xfer->rx_data_len = q2spi_req.data_len;
	xfer->rx_len = xfer->rx_data_len;
	xfer->tid = q2spi_pkt->var1_pkt->flow_id;

	Q2SPI_DBG_1(q2spi, "%s tx_buf:%p tx_dma:%p rx_buf:%p rx_dma:%p tx_len:%d rx_len:%d\n",
		    __func__, xfer->tx_buf, (void *)xfer->tx_dma,
		    xfer->rx_buf, (void *)xfer->rx_dma,
		    xfer->tx_len, xfer->rx_len);
	q2spi_dump_ipc(q2spi, "q2spi read reg tx_buf DMA TX", (char *)xfer->tx_buf, xfer->tx_len);

	ret = q2spi_gsi_submit(q2spi_pkt);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_gsi_submit failed: %d\n", __func__, ret);
		return ret;
	}

	q2spi_free_xfer_tid(q2spi, q2spi_pkt->xfer->tid);
	Q2SPI_DBG_1(q2spi, "Reg:0x%x Read Val = 0x%x\n", reg_offset, *(unsigned int *)xfer->rx_buf);
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
	int ret = 0;

	q2spi_req.cmd = LOCAL_REG_WRITE;
	q2spi_req.addr = reg_offset;
	q2spi_req.data_len = 4;
	q2spi_req.data_buff = &data;
	ret = q2spi_frame_lra(q2spi, &q2spi_req, &q2spi_pkt, VARIANT_1_LRA);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_frame_lra failed ret:%d\n", __func__, ret);
		return ret;
	}
	Q2SPI_DBG_1(q2spi, "%s q2spi_pkt:%p\n", __func__, q2spi_pkt);
	xfer = q2spi_pkt->xfer;
	xfer->tx_buf = q2spi_pkt->var1_pkt;
	xfer->tx_dma = q2spi_pkt->var1_tx_dma;
	xfer->cmd = q2spi_pkt->m_cmd_param;
	xfer->tx_data_len = q2spi_req.data_len;
	xfer->tx_len = Q2SPI_HEADER_LEN + xfer->tx_data_len;
	xfer->tid = q2spi_pkt->var1_pkt->flow_id;

	Q2SPI_DBG_1(q2spi, "%s tx_buf:%p tx_dma:%p rx_buf:%p rx_dma:%p tx_len:%d rx_len:%d\n",
		    __func__, xfer->tx_buf, (void *)xfer->tx_dma,
		    xfer->rx_buf, (void *)xfer->rx_dma,
		    xfer->tx_len, xfer->rx_len);
	q2spi_dump_ipc(q2spi, "q2spi_read_reg tx_buf DMA TX", (char *)xfer->tx_buf, xfer->tx_len);

	ret = q2spi_gsi_submit(q2spi_pkt);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_gsi_submit failed: %d\n", __func__, ret);
		return ret;
	}

	q2spi_free_xfer_tid(q2spi, q2spi_pkt->xfer->tid);
	Q2SPI_DBG_1(q2spi, "%s write to reg success ret:%d\n", __func__, ret);
	return ret;
}

/**
 * q2spi_slave_init - Initialization sequence
 * @q2spi: Pointer to main q2spi_geni structure
 * @slave_init: Slave_init flag for slave initialization
 *
 * This function performs init sequence with q2spi slave
 * send host command to check client enabled or not
 * read Q2SPI_HOST_CFG.DOORBELL_EN register info from slave
 * Write 1 to each bit of Q2SPI_ERROR_EN to enable error interrupt to Host using doorbell.
 *
 * Return: 0 for success, negative number for error condition.
 */
static int q2spi_slave_init(struct q2spi_geni *q2spi, bool slave_init)
{
	unsigned long scratch_data = 0xAAAAAAAA;
	unsigned long error_en_data = 0xFFFFFFFF;
	int ret = 0, value = 0;
	int retries = RETRIES;

	if (!slave_init)
		return 0;

	/* Dummy SCRATCH register write */
	ret = q2spi_write_reg(q2spi, Q2SPI_SCRATCH0, scratch_data);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "scratch0 write failed: %d\n", ret);
		return ret;
	}

	/* Dummy SCRATCH register read */
	ret = q2spi_read_reg(q2spi, Q2SPI_SCRATCH0);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "Err scratch0 read failed: %d\n", ret);
		return ret;
	}

	/*
	 * Send dummy Host command until Client is enabled.
	 * Dummy command can be reading Q2SPI_HW_VERSION register.
	 */
	while (retries > 0 && value <= 0) {
		value = q2spi_read_reg(q2spi, Q2SPI_HW_VERSION);
		Q2SPI_DBG_1(q2spi, "%s retries:%d value:%d\n", __func__, retries, value);
		if (value <= 0)
			Q2SPI_DEBUG(q2spi, "HW_Version read failed: %d\n", ret);
		retries--;
		Q2SPI_DBG_1(q2spi, "%s retries:%d value:%d\n", __func__, retries, value);
	}

	Q2SPI_DBG_1(q2spi, "%s reg:0x%x\n", __func__, Q2SPI_HOST_CFG);
	ret = q2spi_read_reg(q2spi, Q2SPI_HOST_CFG);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "Err HOST CFG read failed: %d\n", ret);
		return ret;
	}

	Q2SPI_DBG_1(q2spi, "%s reg:0x%x\n", __func__, Q2SPI_ERROR_EN);
	ret = q2spi_write_reg(q2spi, Q2SPI_ERROR_EN, error_en_data);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "Err Error_en reg write failed: %d\n", ret);
		return ret;
	}

	Q2SPI_DBG_1(q2spi, "%s reg:0x%x\n", __func__, Q2SPI_ERROR_EN);
	ret = q2spi_read_reg(q2spi, Q2SPI_ERROR_EN);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "Err HOST CFG read failed: %d\n", ret);
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

/*
 * q2spi_copy_cr_data_to_pkt() - copies cr data to q2spi_pkt
 *
 * @q2spi_pkt: pointer to q2spi_packet
 * @cr_pkt: pointer to cr_pkt
 * @idx: index of cr data in cr_pkt
 *
 * @Return: None
 */
void
q2spi_copy_cr_data_to_pkt(struct q2spi_packet *q2spi_pkt, struct q2spi_cr_packet *cr_pkt, int idx)
{
	memcpy(&q2spi_pkt->cr_hdr, &cr_pkt->cr_hdr[idx], sizeof(struct q2spi_cr_header));
	memcpy(&q2spi_pkt->cr_var3, &cr_pkt->var3_pkt[idx], sizeof(struct q2spi_client_dma_pkt));
	memcpy(&q2spi_pkt->cr_bulk, &cr_pkt->bulk_pkt[idx],
	       sizeof(struct q2spi_client_bulk_access_pkt));
	q2spi_pkt->cr_hdr_type = cr_pkt->cr_hdr_type[idx];
	Q2SPI_DBG_1(q2spi_pkt->q2spi, "%s q2spi_pkt:%p cr_hdr_type:%d\n",
		    __func__, q2spi_pkt, q2spi_pkt->cr_hdr_type);
}

/*
 * q2spi_send_system_mem_access() - Sends system memory access read command
 *
 * @q2spi: pointer to q2spi_geni driver data
 * @q2spi_pkt: double pointer to q2spi_packet
 * @cr_pkt: pointer to cr_pkt
 * @idx: index of cr data in cr_pkt
 *
 * @Return: None
 */
int q2spi_send_system_mem_access(struct q2spi_geni *q2spi, struct q2spi_packet **q2spi_pkt,
				 struct q2spi_cr_packet *cr_pkt, int idx)
{
	unsigned long xfer_timeout = 0;
	long timeout = 0;
	struct q2spi_request q2spi_req;
	int ret = 0, retries = Q2SPI_RESP_BUF_RETRIES;
	unsigned int dw_len;
	u8 flow_id = cr_pkt->var3_pkt[idx].flow_id;

	q2spi->sys_mem_read_in_progress = true;
	dw_len = ((cr_pkt->var3_pkt[idx].dw_len_part3 << 12) & 0xFF) |
		  ((cr_pkt->var3_pkt[idx].dw_len_part2 << 4) & 0xFF) |
		   cr_pkt->var3_pkt[idx].dw_len_part1;
	q2spi_req.data_len = (dw_len * 4) + 4;
	Q2SPI_DBG_1(q2spi, "%s dw_len:%d data_len:%d\n", __func__, dw_len, q2spi_req.data_len);
	q2spi_req.cmd = DATA_READ;
	q2spi_req.addr = 0;
	q2spi_req.end_point = 0;
	q2spi_req.proto_ind = 0;
	q2spi_req.priority = 0;
	q2spi_req.flow_id = flow_id;
	q2spi_req.sync = 0;
	while (retries--) {
		mutex_lock(&q2spi->queue_lock);
		ret = q2spi_add_req_to_tx_queue(q2spi, &q2spi_req, q2spi_pkt);
		mutex_unlock(&q2spi->queue_lock);
		if (ret == -ENOMEM) {
			Q2SPI_DEBUG(q2spi, "%s Err ret:%d\n", __func__, ret);
			/* sleep sometime to let application consume the pending rx buffers */
			usleep_range(125000, 150000);
		} else {
			break;
		}
	}
	if (ret < 0) {
		q2spi->sys_mem_read_in_progress = false;
		Q2SPI_DEBUG(q2spi, "%s Err ret:%d\n", __func__, ret);
		return ret;
	}

	q2spi_copy_cr_data_to_pkt((struct q2spi_packet *)*q2spi_pkt, cr_pkt, idx);
	((struct q2spi_packet *)*q2spi_pkt)->var3_data_len = q2spi_req.data_len;
	if (atomic_read(&q2spi->sma_wr_pending)) {
		Q2SPI_DBG_1(q2spi, "%s sma write is pending wait\n", __func__);
		xfer_timeout = msecs_to_jiffies(XFER_TIMEOUT_OFFSET);
		timeout = wait_for_completion_interruptible_timeout(&q2spi->sma_wr_comp,
								    xfer_timeout);
		if (timeout <= 0) {
			Q2SPI_DEBUG(q2spi, "%s Err timeout %ld for sma write complete\n",
				    __func__, timeout);
			atomic_set(&q2spi->doorbell_pending, 0);
			if (timeout == -ERESTARTSYS) {
				q2spi_sys_restart = true;
				q2spi->sys_mem_read_in_progress = false;
				return -ERESTARTSYS;
			}
			q2spi->sys_mem_read_in_progress = false;
			return -ETIMEDOUT;
		}
	}
	ret = __q2spi_send_messages(q2spi, (void *)*q2spi_pkt);
	q2spi->sys_mem_read_in_progress = false;
	Q2SPI_DBG_1(q2spi, "%s End ret:%d %d\n", __func__, ret, __LINE__);
	return ret;
}

/*
 * q2spi_find_pkt_by_flow_id() - finds q2spi packet in tx_queue_list and copies cr data
 *
 * @q2spi: pointer to q2spi_geni driver data
 * @cr_pkt: pointer to cr_pkt
 * @idx: index of var3_pkt which contains flow_id received from target
 *
 * @Return: None
 */
void q2spi_find_pkt_by_flow_id(struct q2spi_geni *q2spi, struct q2spi_cr_packet *cr_pkt, int idx)
{
	struct q2spi_packet *q2spi_pkt = NULL, *q2spi_pkt_tmp1, *q2spi_pkt_tmp2;
	u8 flow_id = cr_pkt->var3_pkt[idx].flow_id;

	mutex_lock(&q2spi->queue_lock);
	list_for_each_entry_safe(q2spi_pkt_tmp1, q2spi_pkt_tmp2, &q2spi->tx_queue_list, list) {
		if (q2spi_pkt_tmp1->flow_id == flow_id) {
			q2spi_pkt = q2spi_pkt_tmp1;
			q2spi_copy_cr_data_to_pkt(q2spi_pkt, cr_pkt, idx);
			break;
		}
	}
	mutex_unlock(&q2spi->queue_lock);
	if (q2spi_pkt) {
		Q2SPI_DBG_1(q2spi, "%s Found q2spi_pkt %p with flow_id %d\n",
			    __func__, q2spi_pkt, flow_id);
		if (!atomic_read(&q2spi->sma_wr_pending)) {
			atomic_set(&q2spi->sma_wr_pending, 1);
			Q2SPI_DBG_1(q2spi, "%s sma_wr_pending set for prev DB\n", __func__);
		}

		/* wakeup HRF flow which is waiting for this CR doorbell */
		complete_all(&q2spi_pkt->wait_for_db);
		return;
	}
	Q2SPI_DEBUG(q2spi, "%s Err q2spi_pkt not found for flow_id %d\n", __func__, flow_id);
}

/*
 * q2spi_set_data_avail_in_pkt() - sets q2spi packet state to data availability
 *
 * @q2spi: pointer to q2spi_geni driver data
 * @cr_pkt: pointer to cr_pkt containing bulk_pkt
 * @idx: index of bulk_pkt which contains flow_id received from target
 *
 * @Return: None
 */
void q2spi_set_data_avail_in_pkt(struct q2spi_geni *q2spi, struct q2spi_cr_packet *cr_pkt, int idx)
{
	struct q2spi_packet *q2spi_pkt = NULL, *q2spi_pkt_tmp1, *q2spi_pkt_tmp2;
	u8 flow_id = cr_pkt->bulk_pkt[idx].flow_id;

	mutex_lock(&q2spi->queue_lock);
	list_for_each_entry_safe(q2spi_pkt_tmp1, q2spi_pkt_tmp2, &q2spi->tx_queue_list, list) {
		if (q2spi_pkt_tmp1->flow_id == flow_id) {
			if (q2spi_pkt_tmp1->cr_var3.flow_id == flow_id &&
			    q2spi_pkt_tmp1->state == IN_USE) {
				q2spi_pkt = q2spi_pkt_tmp1;
				Q2SPI_DBG_1(q2spi, "%s Found CR PKT for flow_id:%d",
					    __func__, flow_id);
				break;
			}
		}
	}
	mutex_unlock(&q2spi->queue_lock);

	if (q2spi_pkt) {
		Q2SPI_DBG_1(q2spi, "%s Found q2spi_pkt %p with flow_id %d",
			    __func__, q2spi_pkt, flow_id);
		q2spi_pkt->state = DATA_AVAIL;
	} else {
		Q2SPI_DBG_1(q2spi, "%s Err q2spi_pkt not found for flow_id %d\n",
			    __func__, flow_id);
	}
}

/*
 * q2spi_complete_bulk_status() - calls completion for q2spi packet waiting on bulk_wait
 *
 * @q2spi: pointer to q2spi_geni driver data
 * @cr_pkt: pointer to cr_pkt containing bulk_pkt
 * @idx: index of bulk_pkt which contains flow_id received from target
 *
 * @Return: None
 */
void q2spi_complete_bulk_status(struct q2spi_geni *q2spi, struct q2spi_cr_packet *cr_pkt, int idx)
{
	struct q2spi_packet *q2spi_pkt = NULL, *q2spi_pkt_tmp1, *q2spi_pkt_tmp2;
	u8 flow_id = cr_pkt->bulk_pkt[idx].flow_id;

	mutex_lock(&q2spi->queue_lock);
	list_for_each_entry_safe(q2spi_pkt_tmp1, q2spi_pkt_tmp2, &q2spi->tx_queue_list, list) {
		if (q2spi_pkt_tmp1->flow_id == flow_id) {
			q2spi_pkt = q2spi_pkt_tmp1;
			break;
		}
	}
	mutex_unlock(&q2spi->queue_lock);
	if (q2spi_pkt) {
		Q2SPI_DBG_1(q2spi, "%s Found q2spi_pkt %p with flow_id %d\n",
			    __func__, q2spi_pkt, flow_id);
		q2spi_copy_cr_data_to_pkt(q2spi_pkt, cr_pkt, idx);
		complete_all(&q2spi_pkt->bulk_wait);
	} else {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi_pkt not found for flow_id %d\n",
			    __func__, flow_id);
	}
}

/*
 * q2spi_handle_wakeup_work() - worker function which handles remote wakeup flow for q2spi
 * @work: pointer to work_struct
 *
 * Return: None
 */
static void q2spi_handle_wakeup_work(struct work_struct *work)
{
	struct q2spi_geni *q2spi =
		container_of(work, struct q2spi_geni, q2spi_wakeup_work);
	int ret = 0;

	Q2SPI_DBG_1(q2spi, "%s Enter PID=%d q2spi:%p\n", __func__, current->pid, q2spi);

	ret = q2spi_geni_runtime_resume(q2spi->dev);
	if (ret)
		Q2SPI_DEBUG(q2spi, "%s Runtime resume Failed:%d\n", __func__, ret);
}

/*
 * q2spi_sleep_work_func() - worker function which handles client sleep sequence for q2spi
 * @work: pointer to work_struct
 *
 * Return: None
 */
static void q2spi_sleep_work_func(struct work_struct *work)
{
	struct q2spi_geni *q2spi =
		container_of(work, struct q2spi_geni, q2spi_sleep_work);

	Q2SPI_DBG_1(q2spi, "%s: PID=%d\n", __func__, current->pid);
	if (q2spi_sys_restart || q2spi->port_release) {
		Q2SPI_DEBUG(q2spi, "%s Err Port in closed state or sys_restart\n", __func__);
		return;
	}
	q2spi_put_slave_to_sleep(q2spi);
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
	int ret = 0, i = 0, no_of_crs = 0;
	bool sys_mem_access = false;
	long timeout = 0;

	Q2SPI_DBG_1(q2spi, "%s Enter PID=%d q2spi:%p PM get_sync count:%d\n", __func__,
		    current->pid, q2spi, atomic_read(&q2spi->dev->power.usage_count));
	ret = pm_runtime_get_sync(q2spi->dev);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s Err for PM get\n", __func__);
		pm_runtime_put_noidle(q2spi->dev);
		pm_runtime_set_suspended(q2spi->dev);
		return;
	}
	Q2SPI_DBG_2(q2spi, "%s PM after get_sync count:%d\n", __func__,
		    atomic_read(&q2spi->dev->power.usage_count));
	/* wait for RX dma channel TCE 0x22 to get CR body in RX DMA buffer */
	ret = check_gsi_transfer_completion_db_rx(q2spi);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s db rx completion timeout: %d\n", __func__, ret);
		atomic_set(&q2spi->doorbell_pending, 0);
		q2spi_unmap_doorbell_rx_buf(q2spi);
		atomic_set(&q2spi->sma_wr_pending, 0);
		atomic_set(&q2spi->doorbell_pending, 0);
		q2spi_geni_se_dump_regs(q2spi);
		gpi_dump_for_geni(q2spi->gsi->tx_c);
		goto exit_doorbell_work;
	}

	/* Extract cr hdr info from doorbell rx dma buffer */
	q2spi_cr_pkt = q2spi_prepare_cr_pkt(q2spi);
	if (!q2spi_cr_pkt) {
		Q2SPI_DEBUG(q2spi, "Err q2spi_prepare_cr_pkt failed\n");
		atomic_set(&q2spi->doorbell_pending, 0);
		q2spi_unmap_doorbell_rx_buf(q2spi);
		goto exit_doorbell_work;
	}

	q2spi_unmap_doorbell_rx_buf(q2spi);

	reinit_completion(&q2spi->sma_wait);

	no_of_crs = q2spi_cr_pkt->num_valid_crs;
	Q2SPI_DBG_2(q2spi, "%s q2spi_cr_pkt:%p q2spi_db_xfer:%p db_xfer_rx_buf:%p\n",
		    __func__, q2spi_cr_pkt, q2spi->db_xfer, q2spi->db_xfer->rx_buf);

	for (i = 0; i < no_of_crs; i++) {
		Q2SPI_DBG_1(q2spi, "%s i=%d CR Header CMD 0x%x\n",
			    __func__, i, q2spi_cr_pkt->cr_hdr[i].cmd);
		if (q2spi_cr_pkt->cr_hdr[i].cmd == ADDR_LESS_WR_ACCESS ||
		    q2spi_cr_pkt->cr_hdr[i].cmd == ADDR_LESS_RD_ACCESS) {
			if (q2spi_cr_pkt->cr_hdr[i].flow) {
				/* C->M flow */
				Q2SPI_DBG_2(q2spi,
					    "%s cr_hdr ADDR_LESS_WR/RD_ACCESS with client flow opcode:%d\n",
					    __func__, q2spi_cr_pkt->cr_hdr[i].cmd);
				Q2SPI_DBG_2(q2spi, "%s len_part1:%d len_part2:%d len_part3:%d\n",
					    __func__, q2spi_cr_pkt->var3_pkt[i].dw_len_part1,
					    q2spi_cr_pkt->var3_pkt[i].dw_len_part2,
					    q2spi_cr_pkt->var3_pkt[i].dw_len_part3);

				if (!q2spi_send_system_mem_access(
							q2spi, &q2spi_pkt, q2spi_cr_pkt, i))
					sys_mem_access = true;
			} else {
				/* M->C flow */
				Q2SPI_DBG_2(q2spi,
					    "%s cr_hdr ADDR_LESS_WR/RD with Host flow, opcode:%d\n",
					    __func__, q2spi_cr_pkt->cr_hdr[i].cmd);
				if (q2spi_cr_pkt->cr_hdr[i].cmd == ADDR_LESS_WR_ACCESS) {
					q2spi_find_pkt_by_flow_id(q2spi, q2spi_cr_pkt, i);
					Q2SPI_DBG_1(q2spi, "%s cmd:%d doorbell CR for Host flow\n",
						    __func__, q2spi_cr_pkt->cr_hdr[i].cmd);
				}
			}
		} else if (q2spi_cr_pkt->cr_hdr[i].cmd == BULK_ACCESS_STATUS) {
			if (q2spi_cr_pkt->bulk_pkt[i].flow_id >= 0x8) {
				Q2SPI_DBG_1(q2spi, "%s Bulk status with Client Flow ID\n",
					    __func__);
				q2spi_set_data_avail_in_pkt(q2spi, q2spi_cr_pkt, i);
				q2spi_notify_data_avail_for_client(q2spi);
				if (!timer_pending(&q2spi->slave_sleep_timer)) {
					Q2SPI_DBG_1(q2spi, "%s sleep timer expired\n", __func__);
					q2spi_put_slave_to_sleep(q2spi);
				}
			} else {
				Q2SPI_DBG_1(q2spi, "%s Bulk status with host Flow ID:%d\n",
					    __func__, q2spi_cr_pkt->bulk_pkt[i].flow_id);
				q2spi_complete_bulk_status(q2spi, q2spi_cr_pkt, i);
			}
		} else if (q2spi_cr_pkt->cr_hdr[i].cmd == CR_EXTENSION) {
			Q2SPI_DBG_1(q2spi, "%s Extended CR from Client\n", __func__);
		}

		if (sys_mem_access) {
			Q2SPI_DBG_2(q2spi, "%s waiting on sma_wait\n", __func__);
			/* Block on read_wq until sma complete */
			timeout = wait_for_completion_interruptible_timeout
				(&q2spi->sma_wait, msecs_to_jiffies(XFER_TIMEOUT_OFFSET));
			if (timeout <= 0) {
				Q2SPI_DEBUG(q2spi, "%s Err wait interrupted timeout:%ld\n",
					    __func__, timeout);
				if (timeout == -ERESTARTSYS) {
					q2spi_sys_restart = true;
					return;
				}
				goto exit_doorbell_work;
			}
		}
	}
	q2spi_kfree(q2spi, q2spi_cr_pkt, __LINE__);
	/*
	 * get one rx buffer from allocated pool and
	 * map to gsi to ready for next doorbell.
	 */
	if (q2spi_map_doorbell_rx_buf(q2spi))
		Q2SPI_DEBUG(q2spi, "Err failed to alloc RX DMA buf");

exit_doorbell_work:
	pm_runtime_mark_last_busy(q2spi->dev);
	Q2SPI_DBG_2(q2spi, "%s PM before put_autosuspend count:%d\n",
		    __func__, atomic_read(&q2spi->dev->power.usage_count));
	pm_runtime_put_autosuspend(q2spi->dev);
	Q2SPI_DBG_1(q2spi, "%s End PID=%d PM after put_autosuspend count:%d\n",
		    __func__, current->pid, atomic_read(&q2spi->dev->power.usage_count));
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

	for (i = 0; i < Q2SPI_MAX_DEV; i++) {
		device_destroy(q2spi->chrdev.q2spi_class, MKDEV(q2spi_cdev_major, i));
		cdev_del(&q2spi->chrdev.cdev[i]);
	}
	class_destroy(q2spi->chrdev.q2spi_class);
	unregister_chrdev_region(MKDEV(q2spi_cdev_major, 0), MINORMASK);
	Q2SPI_DBG_2(q2spi, "%s End %d\n", __func__, q2spi_cdev_major);
}

/**
 * q2spi_sleep_config - Q2SPI sleep config
 *
 * @q2spi: pointer to q2spi_geni driver data
 * @pdev: pointer to platform device
 *
 * Return: 0 for success, negative number for error condition.
 */
static int q2spi_sleep_config(struct q2spi_geni *q2spi, struct platform_device *pdev)
{
	int ret = 0;

	q2spi->wake_clk_gpio = of_get_named_gpio(pdev->dev.of_node, "clk-pin", 0);
	if (!gpio_is_valid(q2spi->wake_clk_gpio)) {
		dev_err(&pdev->dev, "failed to parse clk gpio\n");
		return -EINVAL;
	}
	ret = devm_gpio_request(q2spi->dev, q2spi->wake_clk_gpio, "Q2SPI_CLK_GPIO");
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s:Err failed to request GPIO-%d\n", __func__,
			    q2spi->wake_clk_gpio);
		return ret;
	}
	q2spi->wake_mosi_gpio = of_get_named_gpio(pdev->dev.of_node, "mosi-pin", 0);
	if (!gpio_is_valid(q2spi->wake_mosi_gpio)) {
		dev_err(&pdev->dev, "failed to parse mosi gpio\n");
		return -EINVAL;
	}
	ret = devm_gpio_request(q2spi->dev, q2spi->wake_mosi_gpio, "Q2SPI_MOSI_GPIO");
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s:Err failed to request GPIO-%d\n", __func__,
			    q2spi->wake_mosi_gpio);
		return ret;
	}
	Q2SPI_DBG_1(q2spi, "%s Q2SPI clk_gpio:%d mosi_gpio:%d\n",
		    __func__, q2spi->wake_clk_gpio, q2spi->wake_mosi_gpio);

	q2spi->wakeup_wq = alloc_workqueue("%s", WQ_HIGHPRI, 1, dev_name(q2spi->dev));
	if (!q2spi->wakeup_wq) {
		Q2SPI_ERROR(q2spi, "Err failed to wakeup workqueue");
		return -ENOMEM;
	}
	INIT_WORK(&q2spi->q2spi_wakeup_work, q2spi_handle_wakeup_work);

	q2spi->sleep_wq = alloc_workqueue("q2spi_sleep_wq",  WQ_UNBOUND | WQ_HIGHPRI, 0);
	if (!q2spi->sleep_wq) {
		Q2SPI_ERROR(q2spi, "Err failed to allocate sleep_timer workqueue");
		return -ENOMEM;
	}
	INIT_WORK(&q2spi->q2spi_sleep_work, q2spi_sleep_work_func);

	/* To use the Doorbel pin as wakeup irq */
	q2spi->doorbell_irq = platform_get_irq(pdev, 1);
	Q2SPI_DBG_1(q2spi, "%s Q2SPI doorbell_irq:%d\n", __func__, q2spi->doorbell_irq);
	irq_set_status_flags(q2spi->doorbell_irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(q2spi->dev, q2spi->doorbell_irq,
			       q2spi_geni_wakeup_isr, IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
			       "doorbell_wakeup", q2spi);
	if (unlikely(ret)) {
		Q2SPI_ERROR(q2spi, "%s:Failed to get WakeIRQ ret%d\n", __func__, ret);
		return -ENOMEM;
	}

	q2spi_geni_resources_off(q2spi);
	pm_runtime_use_autosuspend(q2spi->dev);
	pm_runtime_set_autosuspend_delay(q2spi->dev, Q2SPI_AUTOSUSPEND_DELAY);
	pm_runtime_set_suspended(q2spi->dev);
	pm_runtime_enable(q2spi->dev);
	return ret;
}

/**
 * q2spi_client_sleep_timeout_handler - Q2SPI client sleep timeout handler
 * @timer_list: timer_list pointer
 *
 * Return: None
 */
void q2spi_client_sleep_timeout_handler(struct timer_list *t)
{
	struct q2spi_geni *q2spi = from_timer(q2spi, t, slave_sleep_timer);

	Q2SPI_DBG_1(q2spi, "%s: PID=%d\n", __func__, current->pid);
	if (q2spi_sys_restart || q2spi->port_release) {
		Q2SPI_DEBUG(q2spi, "%s Err Port in closed state or sys_restart\n", __func__);
		return;
	}
	queue_work(q2spi->sleep_wq, &q2spi->q2spi_sleep_work);
}

/**
 * q2spi_destroy_workqueue - Destroy q2spi work queues
 * @q2spi: Pointer to main q2spi_geni structure.
 *
 * Return: None
 */
void q2spi_destroy_workqueue(struct q2spi_geni *q2spi)
{
	if (q2spi->sleep_wq)
		destroy_workqueue(q2spi->sleep_wq);
	if (q2spi->wakeup_wq)
		destroy_workqueue(q2spi->wakeup_wq);
	if (q2spi->doorbell_wq)
		destroy_workqueue(q2spi->doorbell_wq);
}

/**
 * q2spi_geni_restart_cb - callback routine for reboot notifier
 * @nb: pointer to reboot notifier block chain
 * @action: value passed unmodified to notifier function
 * @data: pointer passed unmodified to notifier function
 *
 * Returns 0 for success and non-zero for failure.
 */
static int q2spi_geni_restart_cb(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct q2spi_geni *q2spi = container_of(nb, struct q2spi_geni, restart_handler);

	if (!q2spi) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi is NULL, PID=%d\n", __func__, current->pid);
		return -EINVAL;
	}
	Q2SPI_INFO(q2spi, "%s PID=%d\n", __func__, current->pid);
	q2spi_sys_restart = true;

	return 0;
}

static int q2spi_read_dtsi(struct platform_device *pdev, struct q2spi_geni *q2spi)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Err getting IO region\n");
		return -EINVAL;
	}

	q2spi->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(q2spi->base)) {
		ret = PTR_ERR(q2spi->base);
		dev_err(dev, "Err ioremap fail %d\n", ret);
		return ret;
	}

	q2spi->irq = platform_get_irq(pdev, 0);
	if (q2spi->irq < 0) {
		dev_err(dev, "Err for irq get %d\n", ret);
		ret = q2spi->irq;
		return ret;
	}

	irq_set_status_flags(q2spi->irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(dev, q2spi->irq, q2spi_geni_irq,
			       IRQF_TRIGGER_HIGH, dev_name(dev), q2spi);
	if (ret) {
		dev_err(dev, "Err Failed to request irq %d\n", ret);
		return ret;
	}

	q2spi->se.dev = dev;
	q2spi->se.wrapper = dev_get_drvdata(dev->parent);
	if (!q2spi->se.wrapper) {
		dev_err(dev, "Err SE Wrapper is NULL, deferring probe\n");
		return -EPROBE_DEFER;
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
			return -EINVAL;
		}
	}

	return ret;
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
	struct q2spi_geni *q2spi;
	int ret = 0;

	pr_info("boot_kpi: M - DRIVER GENI_Q2SPI Init\n");

	q2spi = devm_kzalloc(dev, sizeof(*q2spi), GFP_KERNEL);
	if (!q2spi) {
		ret = -ENOMEM;
		goto q2spi_err;
	}

	q2spi->dev = dev;
	ret = q2spi_read_dtsi(pdev, q2spi);
	if (ret)
		goto q2spi_err;

	if (device_create_file(dev, &dev_attr_log_level))
		Q2SPI_INFO(q2spi, "Unable to create device file for q2spi log level\n");

	q2spi->q2spi_log_lvl = LOG_DBG_LEVEL0;

	q2spi->wrapper_dev = dev->parent;
	Q2SPI_DBG_1(q2spi, "%s q2spi:0x%p w_dev:0x%p dev:0x%p, p_dev:0x%p",
		    __func__, q2spi, q2spi->wrapper_dev, dev, &pdev->dev);
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

	mutex_init(&q2spi->geni_resource_lock);
	ret = q2spi_geni_resources_on(q2spi);
	if (ret)
		goto chardev_destroy;

	ret = q2spi_geni_init(q2spi);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "Geni init failed %d\n", ret);
		goto resources_off;
	}

	idr_init(&q2spi->tid_idr);
	init_waitqueue_head(&q2spi->readq);
	init_waitqueue_head(&q2spi->read_wq);
	INIT_LIST_HEAD(&q2spi->tx_queue_list);
	mutex_init(&q2spi->gsi_lock);
	mutex_init(&q2spi->port_lock);
	spin_lock_init(&q2spi->txn_lock);
	mutex_init(&q2spi->queue_lock);
	mutex_init(&q2spi->send_msgs_lock);
	spin_lock_init(&q2spi->cr_queue_lock);
	q2spi->port_release = true;
	q2spi->q2spi_sleep_cmd_enable = false;

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
	init_completion(&q2spi->db_rx_cb);
	init_completion(&q2spi->db_setup_wait);
	init_completion(&q2spi->sma_wait);
	init_completion(&q2spi->wait_for_ext_cr);
	atomic_set(&q2spi->sma_wr_pending, 0);
	atomic_set(&q2spi->sma_rd_pending, 0);
	init_completion(&q2spi->sma_wr_comp);
	init_completion(&q2spi->sma_rd_comp);
	init_completion(&q2spi->wait_comp_start_fail);

	/* Pre allocate buffers for transfers */
	ret = q2spi_pre_alloc_buffers(q2spi);
	if (ret) {
		Q2SPI_ERROR(q2spi, "Err failed to alloc buffers");
		goto destroy_worker;
	}

	q2spi->db_q2spi_pkt = devm_kzalloc(q2spi->dev, sizeof(struct q2spi_packet), GFP_KERNEL);
	if (!q2spi->db_q2spi_pkt) {
		ret = -ENOMEM;
		Q2SPI_ERROR(q2spi, "%s Err failed to allocated db_q2spi_pkt\n", __func__);
		goto free_buf;
	}
	q2spi->db_q2spi_pkt->q2spi = q2spi;

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

	timer_setup(&q2spi->slave_sleep_timer, q2spi_client_sleep_timeout_handler, 0);
	dev_dbg(dev, "Q2SPI GENI SE Driver probed\n");

	platform_set_drvdata(pdev, q2spi);

	if (device_create_file(dev, &dev_attr_max_dump_size))
		Q2SPI_INFO(q2spi, "Unable to create device file for max_dump_size\n");
	q2spi->max_data_dump_size = Q2SPI_DATA_DUMP_SIZE;

	if (q2spi_sleep_config(q2spi, pdev))
		goto free_buf;

	ret = pinctrl_select_state(q2spi->geni_pinctrl, q2spi->geni_gpio_default);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s: Err failed to pinctrl state to gpio, ret:%d\n",
			    __func__, ret);
		goto free_buf;
	}

	q2spi->restart_handler.notifier_call = q2spi_geni_restart_cb;
	ret = register_reboot_notifier(&q2spi->restart_handler);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s: Err failed to register reboot notifier, ret:%d\n",
			    __func__, ret);
		goto free_buf;
	}

	Q2SPI_INFO(q2spi, "%s Q2SPI GENI SE Driver probe\n", __func__);
	pr_info("boot_kpi: M - DRIVER GENI_Q2SPI Ready\n");
	return 0;
free_buf:
	q2spi_destroy_workqueue(q2spi);
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
	if (q2spi && q2spi->ipc) {
		q2spi->base = NULL;
		ipc_log_context_destroy(q2spi->ipc);
	}

	pr_err("%s: failed ret:%d\n", __func__, ret);
	return ret;
}

static int q2spi_geni_remove(struct platform_device *pdev)
{
	struct q2spi_geni *q2spi = platform_get_drvdata(pdev);

	pr_info("%s q2spi=0x%p\n", __func__, q2spi);

	if (!q2spi || !q2spi->base)
		return 0;

	unregister_reboot_notifier(&q2spi->restart_handler);
	device_remove_file(&pdev->dev, &dev_attr_max_dump_size);

	destroy_workqueue(q2spi->sleep_wq);
	destroy_workqueue(q2spi->wakeup_wq);

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

/**
 * get_q2spi - get q2spi pointer from device
 * @dev: Device pointer
 *
 * Return: return q2spi pointer
 */
static struct q2spi_geni *get_q2spi(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct q2spi_geni *q2spi = platform_get_drvdata(pdev);

	return q2spi;
}

/*
 * q2spi_wakeup_slave_through_gpio - Preparing HW wake up through Mosi and clock GPIO's
 *
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Return: 0 for succes, else returns linux error codes
 */
int q2spi_wakeup_slave_through_gpio(struct q2spi_geni *q2spi)
{
	int ret = 0;

	Q2SPI_DBG_1(q2spi, "%s Sending disconnect doorbell only\n", __func__);
	atomic_set(&q2spi->slave_in_sleep, 0);

	ret = pinctrl_select_state(q2spi->geni_pinctrl, q2spi->geni_gpio_default);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s: Err failed to pinctrl state to gpio, ret:%d\n",
			    __func__, ret);
		return ret;
	}

	gpio_direction_output(q2spi->wake_clk_gpio, 0);

	/* Set Clock pin to Low */
	gpio_set_value(q2spi->wake_clk_gpio, 0);
	Q2SPI_DBG_1(q2spi, "%s:gpio(%d) value is %d\n", __func__,
		    q2spi->wake_clk_gpio, gpio_get_value(q2spi->wake_clk_gpio));

	gpio_direction_output(q2spi->wake_mosi_gpio, 0);

	/* Set Mosi pin to High */
	gpio_set_value(q2spi->wake_mosi_gpio, 1);
	Q2SPI_DBG_1(q2spi, "%s:gpio(%d) value is %d\n", __func__,
		    q2spi->wake_mosi_gpio, gpio_get_value(q2spi->wake_mosi_gpio));
	usleep_range(2000, 5000);

	/* Set back Mosi pin to Low */
	gpio_set_value(q2spi->wake_mosi_gpio, 0);
	Q2SPI_DBG_1(q2spi, "%s:gpio(%d) value is %d\n", __func__,
		    q2spi->wake_mosi_gpio, gpio_get_value(q2spi->wake_mosi_gpio));

	gpio_direction_input(q2spi->wake_mosi_gpio);
	gpio_direction_input(q2spi->wake_clk_gpio);

	/* Bring back to QUP mode by switching to the pinctrl active state */
	ret = pinctrl_select_state(q2spi->geni_pinctrl, q2spi->geni_gpio_active);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s: Err failed to pinctrl state to active, ret:%d\n",
			    __func__, ret);
		return ret;
	}

	/* add necessary delay to wake up the soc */
	usleep_range(5000, 6000);
	gpi_q2spi_terminate_all(q2spi->gsi->tx_c);
	return ret;
}

/*
 * q2spi_put_slave_to_sleep - Put HW to sleep by sending HRF sleep command
 *
 * @q2spi: Pointer to main q2spi_geni structure
 *
 * Return: 0 for succes;
 */
int q2spi_put_slave_to_sleep(struct q2spi_geni *q2spi)
{
	struct q2spi_packet *q2spi_pkt;
	struct q2spi_request q2spi_req;
	int ret = 0;

	Q2SPI_DBG_1(q2spi, "%s: PID=%d q2spi_sleep_cmd_enable:%d\n",
		    __func__, current->pid, q2spi->q2spi_sleep_cmd_enable);

	if (!q2spi->q2spi_sleep_cmd_enable)
		return 0;

	if (mutex_is_locked(&q2spi->port_lock) || q2spi->port_release) {
		Q2SPI_DEBUG(q2spi, "%s: port_lock acquired or release is in progress\n", __func__);
		return 0;
	}

	mutex_lock(&q2spi->queue_lock);
	if (atomic_read(&q2spi->slave_in_sleep)) {
		Q2SPI_DEBUG(q2spi, "%s: Client in sleep\n", __func__);
		mutex_unlock(&q2spi->queue_lock);
		return 0;
	}
	atomic_set(&q2spi->slave_in_sleep, 1);

	q2spi_req.cmd = Q2SPI_HRF_SLEEP_CMD;
	q2spi_req.sync = 1;

	ret = q2spi_add_req_to_tx_queue(q2spi, &q2spi_req, &q2spi_pkt);
	mutex_unlock(&q2spi->queue_lock);
	if (ret < 0) {
		Q2SPI_DEBUG(q2spi, "%s Err failed ret:%d\n", __func__, ret);
		atomic_set(&q2spi->slave_in_sleep, 0);
		return ret;
	}

	Q2SPI_DBG_1(q2spi, "%s q2spi_pkt:%p tid:%d\n", __func__, q2spi_pkt, q2spi_pkt->xfer->tid);
	q2spi_pkt->is_client_sleep_pkt = true;
	ret = __q2spi_transfer(q2spi, q2spi_req, q2spi_pkt, 0);
	if (ret) {
		atomic_set(&q2spi->slave_in_sleep, 0);
		Q2SPI_DEBUG(q2spi, "%s q2spi_pkt:%p ret: %d\n", __func__, q2spi_pkt, ret);
		if (ret == -ETIMEDOUT)
			gpi_q2spi_terminate_all(q2spi->gsi->tx_c);
		if (q2spi->port_release) {
			Q2SPI_DEBUG(q2spi, "%s Err Port in closed state, return\n", __func__);
			return -ENOENT;
		}
	}
	q2spi_pkt->state = IN_DELETION;
	q2spi_free_xfer_tid(q2spi, q2spi_pkt->xfer->tid);
	q2spi_del_pkt_from_tx_queue(q2spi, q2spi_pkt);
	q2spi_free_q2spi_pkt(q2spi_pkt, __LINE__);
	Q2SPI_DBG_1(q2spi, "%s: PID=%d End slave_in_sleep:%d\n", __func__, current->pid,
		    atomic_read(&q2spi->slave_in_sleep));
	return ret;
}

static void q2spi_geni_shutdown(struct platform_device *pdev)
{
	struct q2spi_geni *q2spi = platform_get_drvdata(pdev);

	pr_info("%s q2spi=0x%p\n", __func__, q2spi);

	if (!q2spi || !q2spi->base)
		return;

	q2spi_sys_restart = true;
	q2spi->port_release = true;
}

static int q2spi_geni_runtime_suspend(struct device *dev)
{
	struct q2spi_geni *q2spi = NULL;
	int ret = 0;

	if (q2spi_sys_restart)
		return -ERESTARTSYS;

	q2spi = get_q2spi(dev);
	if (!q2spi) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi is NULL, PID=%d\n", __func__, current->pid);
		return -EINVAL;
	}

	Q2SPI_DBG_1(q2spi, "%s PID=%d\n", __func__, current->pid);
	if (atomic_read(&q2spi->doorbell_pending)) {
		Q2SPI_DEBUG(q2spi, "%s CR Doorbell Pending\n", __func__);
		/* Update last access time of a device for autosuspend */
		pm_runtime_mark_last_busy(q2spi->dev);
		return -EBUSY;
	}

	if (!atomic_read(&q2spi->is_suspend)) {
		q2spi_put_slave_to_sleep(q2spi);

		q2spi_tx_queue_status(q2spi);

		q2spi_unmap_doorbell_rx_buf(q2spi);
		Q2SPI_DBG_1(q2spi, "%s Sending disconnect doorbell cmd\n", __func__);
		geni_gsi_disconnect_doorbell_stop_ch(q2spi->gsi->tx_c, true);
		irq_set_irq_type(q2spi->doorbell_irq, IRQ_TYPE_LEVEL_HIGH);
		ret = irq_set_irq_wake(q2spi->doorbell_irq, 1);
		if (unlikely(ret))
			Q2SPI_DEBUG(q2spi, "%s Err Failed to set IRQ wake\n", __func__);
		q2spi_geni_resources_off(q2spi);
		atomic_set(&q2spi->is_suspend, 1);
		if (!ret)
			enable_irq(q2spi->doorbell_irq);
		else
			Q2SPI_DEBUG(q2spi, "%s Err Failed to enable_irq\n", __func__);
	}
	return ret;
}

static int q2spi_geni_runtime_resume(struct device *dev)
{
	struct q2spi_geni *q2spi = NULL;
	int ret = 0;

	if (q2spi_sys_restart)
		return -ERESTARTSYS;

	q2spi = get_q2spi(dev);
	if (!q2spi) {
		Q2SPI_DEBUG(q2spi, "%s Err q2spi is NULL, PID=%d\n", __func__, current->pid);
		return -EINVAL;
	}

	Q2SPI_DBG_1(q2spi, "%s PID=%d\n", __func__, current->pid);
	if (atomic_read(&q2spi->is_suspend)) {
		if (q2spi_geni_resources_on(q2spi))
			return -EIO;
		disable_irq(q2spi->doorbell_irq);
		ret = irq_set_irq_wake(q2spi->doorbell_irq, 0);
		if (unlikely(ret))
			Q2SPI_DEBUG(q2spi, "%s Failed to set IRQ wake\n", __func__);

		geni_gsi_ch_start(q2spi->gsi->tx_c);
		geni_gsi_connect_doorbell(q2spi->gsi->tx_c);

		/* Clear is_suspend to map doorbell buffers */
		atomic_set(&q2spi->is_suspend, 0);
		ret = q2spi_map_doorbell_rx_buf(q2spi);
		Q2SPI_DBG_1(q2spi, "%s End ret:%d\n", __func__, ret);
	}
	return ret;
}

static int q2spi_geni_resume(struct device *dev)
{
	struct q2spi_geni *q2spi = get_q2spi(dev);

	Q2SPI_DBG_1(q2spi, "%s PID=%d\n", __func__, current->pid);
	Q2SPI_DBG_2(q2spi, "%s PM state:%d is_suspend:%d pm_enable:%d\n", __func__,
		    pm_runtime_status_suspended(dev), atomic_read(&q2spi->is_suspend),
		    pm_runtime_enabled(dev));

	return 0;
}

static int q2spi_geni_suspend(struct device *dev)
{
	struct q2spi_geni *q2spi = get_q2spi(dev);
	int ret = 0;

	Q2SPI_DBG_1(q2spi, "%s PID=%d\n", __func__, current->pid);
	Q2SPI_DBG_2(q2spi, "%s PM state:%d is_suspend:%d pm_enable:%d\n", __func__,
		    pm_runtime_status_suspended(dev), atomic_read(&q2spi->is_suspend),
		    pm_runtime_enabled(dev));
	if (pm_runtime_status_suspended(dev)) {
		Q2SPI_DBG_1(q2spi, "%s: suspended state\n", __func__);
		return ret;
	}

	if (q2spi && !atomic_read(&q2spi->is_suspend)) {
		Q2SPI_DBG_1(q2spi, "%s: PID=%d\n", __func__, current->pid);
		ret = q2spi_geni_runtime_suspend(dev);
		if (ret) {
			Q2SPI_DEBUG(q2spi, "%s: Err runtime_suspend fail\n", __func__);
		} else {
			pm_runtime_disable(dev);
			pm_runtime_set_suspended(dev);
			pm_runtime_enable(dev);
		}
	}
	return ret;
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
	.shutdown = q2spi_geni_shutdown,
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
