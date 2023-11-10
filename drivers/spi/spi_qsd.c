// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2008-2018, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/*
 * SPI driver for Qualcomm Technologies, Inc. MSM platforms
 *
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/pm_runtime.h>
#include <linux/spi/qcom-spi.h>
#include <linux/msm-sps.h>
#include <linux/interconnect.h>
#include "spi_qsd.h"

#define SPI_MAX_BYTES_PER_WORD			(4)

static int msm_spi_pm_resume_runtime(struct device *device);
static int msm_spi_pm_suspend_runtime(struct device *device);
static inline void msm_spi_dma_unmap_buffers(struct msm_spi *dd);
static int get_local_resources(struct msm_spi *dd);
static void put_local_resources(struct msm_spi *dd);

static inline int msm_spi_configure_gsbi(struct msm_spi *dd,
					struct platform_device *pdev)
{
	struct resource *resource;
	unsigned long   gsbi_mem_phys_addr;
	size_t          gsbi_mem_size;
	void __iomem    *gsbi_base;

	resource  = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!resource)
		return 0;

	gsbi_mem_phys_addr = resource->start;
	gsbi_mem_size = resource_size(resource);
	if (!devm_request_mem_region(&pdev->dev, gsbi_mem_phys_addr,
					gsbi_mem_size, SPI_DRV_NAME))
		return -ENXIO;

	gsbi_base = devm_ioremap(&pdev->dev, gsbi_mem_phys_addr,
					gsbi_mem_size);
	if (!gsbi_base)
		return -ENXIO;

	/* Set GSBI to SPI mode */
	writel_relaxed(GSBI_SPI_CONFIG, gsbi_base + GSBI_CTRL_REG);

	return 0;
}

static inline void msm_spi_register_init(struct msm_spi *dd)
{
	writel_relaxed(0x00000001, dd->base + SPI_SW_RESET);
	msm_spi_set_state(dd, SPI_OP_STATE_RESET);
	writel_relaxed(0x00000000, dd->base + SPI_OPERATIONAL);
	writel_relaxed(0x00000000, dd->base + SPI_CONFIG);
	writel_relaxed(0x00000000, dd->base + SPI_IO_MODES);
	if (dd->qup_ver)
		writel_relaxed(0x00000000, dd->base + QUP_OPERATIONAL_MASK);
}

static int msm_spi_pinctrl_init(struct msm_spi *dd)
{
	dd->pinctrl = devm_pinctrl_get(dd->dev);
	if (IS_ERR_OR_NULL(dd->pinctrl)) {
		dev_err(dd->dev, "Failed to get pin ctrl\n");
		return PTR_ERR(dd->pinctrl);
	}
	dd->pins_active = pinctrl_lookup_state(dd->pinctrl,
				SPI_PINCTRL_STATE_DEFAULT);
	if (IS_ERR_OR_NULL(dd->pins_active)) {
		dev_err(dd->dev, "Failed to lookup pinctrl default state\n");
		return PTR_ERR(dd->pins_active);
	}

	dd->pins_sleep = pinctrl_lookup_state(dd->pinctrl,
				SPI_PINCTRL_STATE_SLEEP);
	if (IS_ERR_OR_NULL(dd->pins_sleep)) {
		dev_err(dd->dev, "Failed to lookup pinctrl sleep state\n");
		return PTR_ERR(dd->pins_sleep);
	}

	return 0;
}

static inline int msm_spi_request_gpios(struct msm_spi *dd)
{
	int i = 0;
	int result = 0;

	if (!dd->pdata->use_pinctrl) {
		for (i = 0; i < ARRAY_SIZE(spi_rsrcs); ++i) {
			if (dd->spi_gpios[i] >= 0) {
				result = gpio_request(dd->spi_gpios[i],
						spi_rsrcs[i]);
				if (result) {
					dev_err(dd->dev,
					"error %d gpio_request for pin %d\n",
					result, dd->spi_gpios[i]);
					goto error;
				}
			}
		}
	} else {
		result = pinctrl_select_state(dd->pinctrl, dd->pins_active);
		if (result) {
			dev_err(dd->dev, "%s: Can not set %s pins\n",
			__func__, SPI_PINCTRL_STATE_DEFAULT);
			goto error;
		}
	}
	return 0;
error:
	if (!dd->pdata->use_pinctrl) {
		for (; --i >= 0;) {
			if (dd->spi_gpios[i] >= 0)
				gpio_free(dd->spi_gpios[i]);
		}
	}
	return result;
}

static inline void msm_spi_free_gpios(struct msm_spi *dd)
{
	int i;
	int result = 0;

	if (!dd->pdata->use_pinctrl) {
		for (i = 0; i < ARRAY_SIZE(spi_rsrcs); ++i) {
			if (dd->spi_gpios[i] >= 0)
				gpio_free(dd->spi_gpios[i]);
			}

		for (i = 0; i < ARRAY_SIZE(spi_cs_rsrcs); ++i) {
			if (dd->cs_gpios[i].valid) {
				gpio_free(dd->cs_gpios[i].gpio_num);
				dd->cs_gpios[i].valid = false;
			}
		}
	} else {
		result = pinctrl_select_state(dd->pinctrl, dd->pins_sleep);
		if (result)
			dev_err(dd->dev, "%s: Can not set %s pins\n",
			__func__, SPI_PINCTRL_STATE_SLEEP);
	}
}

static inline int msm_spi_request_cs_gpio(struct msm_spi *dd)
{
	int cs_num;
	int rc;

	cs_num = dd->spi->chip_select;
	if (!(dd->spi->mode & SPI_LOOP)) {
		if (!dd->pdata->use_pinctrl) {
			if ((!(dd->cs_gpios[cs_num].valid)) &&
				(dd->cs_gpios[cs_num].gpio_num >= 0)) {
				rc = gpio_request(dd->cs_gpios[cs_num].gpio_num,
					spi_cs_rsrcs[cs_num]);

				if (rc) {
					dev_err(dd->dev,
					"gpio_request for pin %d failed,error %d\n",
					dd->cs_gpios[cs_num].gpio_num, rc);
					return rc;
				}
				dd->cs_gpios[cs_num].valid = true;
			}
		}
	}
	return 0;
}

static inline void msm_spi_free_cs_gpio(struct msm_spi *dd)
{
	int cs_num;

	cs_num = dd->spi->chip_select;
	if (!dd->pdata->use_pinctrl) {
		if (dd->cs_gpios[cs_num].valid) {
			gpio_free(dd->cs_gpios[cs_num].gpio_num);
			dd->cs_gpios[cs_num].valid = false;
		}
	}
}


/**
 * msm_spi_clk_max_rate: finds the nearest lower rate for a clk
 * @clk the clock for which to find nearest lower rate
 * @rate clock frequency in Hz
 * @return nearest lower rate or negative error value
 *
 * Public clock API extends clk_round_rate which is a ceiling function. This
 * function is a floor function implemented as a binary search using the
 * ceiling function.
 */
static long msm_spi_clk_max_rate(struct clk *clk, unsigned long rate)
{
	long lowest_available, nearest_low, step_size, cur;
	long step_direction = -1;
	long guess = rate;
	int  max_steps = 10;

	cur =  clk_round_rate(clk, rate);
	if (cur == rate)
		return rate;

	/* if we got here then: cur > rate */
	lowest_available =  clk_round_rate(clk, 0);
	if (lowest_available > rate)
		return -EINVAL;

	step_size = (rate - lowest_available) >> 1;
	nearest_low = lowest_available;

	while (max_steps-- && step_size) {
		guess += step_size * step_direction;

		cur =  clk_round_rate(clk, guess);

		if ((cur < rate) && (cur > nearest_low))
			nearest_low = cur;

		/*
		 * if we stepped too far, then start stepping in the other
		 * direction with half the step size
		 */
		if (((cur > rate) && (step_direction > 0))
		 || ((cur < rate) && (step_direction < 0))) {
			step_direction = -step_direction;
			step_size >>= 1;
		}
	}
	return nearest_low;
}

static void msm_spi_clock_set(struct msm_spi *dd, int speed)
{
	long rate;
	int rc;

	rate = msm_spi_clk_max_rate(dd->clk, speed);
	if (rate < 0) {
		dev_err(dd->dev,
		"%s: no match found for requested clock frequency:%d\n",
			__func__, speed);
		return;
	}

	rc = clk_set_rate(dd->clk, rate);
	if (!rc)
		dd->clock_speed = rate;
}

static void msm_spi_clk_path_vote(struct msm_spi *dd, u32 rate)
{
	if (dd->icc_path) {
		u64 ib = rate * dd->pdata->bus_width;

		icc_set_bw(dd->icc_path, 0, ib);
	}
}

static void msm_spi_clk_path_teardown(struct msm_spi *dd)
{
	msm_spi_clk_path_vote(dd, 0);

	if (dd->icc_path) {
		icc_put(dd->icc_path);
		dd->icc_path = NULL;
	}
}

static int msm_spi_calculate_size(int *fifo_size,
				  int *block_size,
				  int block,
				  int mult)
{
	int words;

	switch (block) {
	case 0:
		words = 1; /* 4 bytes */
		break;
	case 1:
		words = 4; /* 16 bytes */
		break;
	case 2:
		words = 8; /* 32 bytes */
		break;
	default:
		return -EINVAL;
	}

	switch (mult) {
	case 0:
		*fifo_size = words * 2;
		break;
	case 1:
		*fifo_size = words * 4;
		break;
	case 2:
		*fifo_size = words * 8;
		break;
	case 3:
		*fifo_size = words * 16;
		break;
	default:
		return -EINVAL;
	}

	*block_size = words * sizeof(u32); /* in bytes */
	return 0;
}

static void msm_spi_calculate_fifo_size(struct msm_spi *dd)
{
	u32 spi_iom;
	int block;
	int mult;

	spi_iom = readl_relaxed(dd->base + SPI_IO_MODES);

	block = (spi_iom & SPI_IO_M_INPUT_BLOCK_SIZE) >> INPUT_BLOCK_SZ_SHIFT;
	mult = (spi_iom & SPI_IO_M_INPUT_FIFO_SIZE) >> INPUT_FIFO_SZ_SHIFT;
	if (msm_spi_calculate_size(&dd->input_fifo_size, &dd->input_block_size,
				   block, mult)) {
		goto fifo_size_err;
	}

	block = (spi_iom & SPI_IO_M_OUTPUT_BLOCK_SIZE) >> OUTPUT_BLOCK_SZ_SHIFT;
	mult = (spi_iom & SPI_IO_M_OUTPUT_FIFO_SIZE) >> OUTPUT_FIFO_SZ_SHIFT;
	if (msm_spi_calculate_size(&dd->output_fifo_size,
				   &dd->output_block_size, block, mult)) {
		goto fifo_size_err;
	}
	if (dd->qup_ver == SPI_QUP_VERSION_NONE) {
		/* DM mode is not available for this block size */
		if (dd->input_block_size == 4 || dd->output_block_size == 4)
			dd->use_dma = false;

		if (dd->use_dma) {
			dd->input_burst_size = max(dd->input_block_size,
						DM_BURST_SIZE);
			dd->output_burst_size = max(dd->output_block_size,
						DM_BURST_SIZE);
		}
	}

	return;

fifo_size_err:
	dd->use_dma = false;
	pr_err("%s: invalid FIFO size, SPI_IO_MODES=0x%x\n", __func__, spi_iom);
}

static void msm_spi_read_word_from_fifo(struct msm_spi *dd)
{
	u32   data_in;
	int   i;
	int   shift;
	int   read_bytes = (dd->pack_words ?
				SPI_MAX_BYTES_PER_WORD : dd->bytes_per_word);

	data_in = readl_relaxed(dd->base + SPI_INPUT_FIFO);
	if (dd->read_buf) {
		for (i = 0; (i < read_bytes) &&
			     dd->rx_bytes_remaining; i++) {
			/* The data format depends on bytes_per_word:
			 * 4 bytes: 0x12345678
			 * 3 bytes: 0x00123456
			 * 2 bytes: 0x00001234
			 * 1 byte : 0x00000012
			 */
			shift = BITS_PER_BYTE * i;
			*dd->read_buf++ = (data_in & (0xFF << shift)) >> shift;
			dd->rx_bytes_remaining--;
		}
	} else {
		if (dd->rx_bytes_remaining >= read_bytes)
			dd->rx_bytes_remaining -= read_bytes;
		else
			dd->rx_bytes_remaining = 0;
	}

	dd->read_xfr_cnt++;
}

static inline bool msm_spi_is_valid_state(struct msm_spi *dd)
{
	u32 spi_op = readl_relaxed(dd->base + SPI_STATE);

	return spi_op & SPI_OP_STATE_VALID;
}

static inline void msm_spi_udelay(unsigned int delay_usecs)
{
	/*
	 * For smaller values of delay, context switch time
	 * would negate the usage of usleep
	 */
	if (delay_usecs > 20)
		usleep_range(delay_usecs, delay_usecs + 1);
	else if (delay_usecs)
		udelay(delay_usecs);
}

static inline int msm_spi_wait_valid(struct msm_spi *dd)
{
	unsigned int delay = 0;
	unsigned long timeout = 0;

	if (dd->clock_speed == 0)
		return -EINVAL;
	/*
	 * Based on the SPI clock speed, sufficient time
	 * should be given for the SPI state transition
	 * to occur
	 */
	delay = (10 * USEC_PER_SEC) / dd->clock_speed;
	/*
	 * For small delay values, the default timeout would
	 * be one jiffy
	 */
	if (delay < SPI_DELAY_THRESHOLD)
		delay = SPI_DELAY_THRESHOLD;

	/* Adding one to round off to the nearest jiffy */
	timeout = jiffies + msecs_to_jiffies(delay * SPI_DEFAULT_TIMEOUT) + 1;
	while (!msm_spi_is_valid_state(dd)) {
		if (time_after(jiffies, timeout)) {
			if (!msm_spi_is_valid_state(dd)) {
				dev_err(dd->dev, "Invalid SPI operational state\n");
				return -ETIMEDOUT;
			} else
				return 0;
		}
		msm_spi_udelay(delay);
	}
	return 0;
}

static inline int msm_spi_set_state(struct msm_spi *dd,
				    enum msm_spi_state state)
{
	enum msm_spi_state cur_state;

	if (msm_spi_wait_valid(dd))
		return -EIO;
	cur_state = readl_relaxed(dd->base + SPI_STATE);
	/* Per spec:
	 * For PAUSE_STATE to RESET_STATE, two writes of (10) are required
	 */
	if (((cur_state & SPI_OP_STATE) == SPI_OP_STATE_PAUSE) &&
			(state == SPI_OP_STATE_RESET)) {
		writel_relaxed(SPI_OP_STATE_CLEAR_BITS, dd->base + SPI_STATE);
		writel_relaxed(SPI_OP_STATE_CLEAR_BITS, dd->base + SPI_STATE);
	} else {
		writel_relaxed((cur_state & ~SPI_OP_STATE) | state,
		       dd->base + SPI_STATE);
	}
	if (msm_spi_wait_valid(dd))
		return -EIO;

	return 0;
}

/**
 * msm_spi_set_bpw_and_no_io_flags: configure N, and no-input/no-output flags
 */
static inline void
msm_spi_set_bpw_and_no_io_flags(struct msm_spi *dd, u32 *config, int n)
{
	*config &= ~(SPI_NO_INPUT|SPI_NO_OUTPUT);

	if (n != (*config & SPI_CFG_N))
		*config = (*config & ~SPI_CFG_N) | n;

	if (dd->tx_mode == SPI_BAM_MODE) {
		if (dd->read_buf == NULL)
			*config |= SPI_NO_INPUT;
		if (dd->write_buf == NULL)
			*config |= SPI_NO_OUTPUT;
	}
}

/**
 * msm_spi_calc_spi_config_loopback_and_input_first: Calculate the values that
 * should be updated into SPI_CONFIG's LOOPBACK and INPUT_FIRST flags
 * @return calculatd value for SPI_CONFIG
 */
static u32
msm_spi_calc_spi_config_loopback_and_input_first(u32 spi_config, u8 mode)
{
	if (mode & SPI_LOOP)
		spi_config |= SPI_CFG_LOOPBACK;
	else
		spi_config &= ~SPI_CFG_LOOPBACK;

	if (mode & SPI_CPHA)
		spi_config &= ~SPI_CFG_INPUT_FIRST;
	else
		spi_config |= SPI_CFG_INPUT_FIRST;

	return spi_config;
}

/**
 * msm_spi_set_spi_config: prepares register SPI_CONFIG to process the
 * next transfer
 */
static void msm_spi_set_spi_config(struct msm_spi *dd, int bpw)
{
	u32 spi_config = readl_relaxed(dd->base + SPI_CONFIG);

	spi_config = msm_spi_calc_spi_config_loopback_and_input_first(
					spi_config, dd->spi->mode);

	if (dd->qup_ver == SPI_QUP_VERSION_NONE)
		/* flags removed from SPI_CONFIG in QUP version-2 */
		msm_spi_set_bpw_and_no_io_flags(dd, &spi_config, bpw-1);

	/*
	 * HS_MODE improves signal stability for spi-clk high rates
	 * but is invalid in LOOPBACK mode.
	 */
	if ((dd->clock_speed >= SPI_HS_MIN_RATE) &&
	   !(dd->spi->mode & SPI_LOOP))
		spi_config |= SPI_CFG_HS_MODE;
	else
		spi_config &= ~SPI_CFG_HS_MODE;

	writel_relaxed(spi_config, dd->base + SPI_CONFIG);
}

/**
 * msm_spi_set_mx_counts: set SPI_MX_INPUT_COUNT and SPI_MX_INPUT_COUNT
 * for FIFO-mode. set SPI_MX_INPUT_COUNT and SPI_MX_OUTPUT_COUNT for
 * BAM and DMOV modes.
 * @n_words The number of reads/writes of size N.
 */
static void msm_spi_set_mx_counts(struct msm_spi *dd, u32 n_words)
{
	/*
	 * For FIFO mode:
	 *   - Set the MX_OUTPUT_COUNT/MX_INPUT_COUNT registers to 0
	 *   - Set the READ/WRITE_COUNT registers to 0 (infinite mode)
	 *     or num bytes (finite mode) if less than fifo worth of data.
	 * For Block mode:
	 *  - Set the MX_OUTPUT/MX_INPUT_COUNT registers to num xfer bytes.
	 *  - Set the READ/WRITE_COUNT registers to 0.
	 */
	if (dd->tx_mode != SPI_BAM_MODE) {
		if (dd->tx_mode == SPI_FIFO_MODE) {
			if (n_words <= dd->input_fifo_size)
				msm_spi_set_write_count(dd, n_words);
			else
				msm_spi_set_write_count(dd, 0);
			writel_relaxed(0, dd->base + SPI_MX_OUTPUT_COUNT);
		} else
			writel_relaxed(n_words, dd->base + SPI_MX_OUTPUT_COUNT);

		if (dd->rx_mode == SPI_FIFO_MODE) {
			if (n_words <= dd->input_fifo_size)
				writel_relaxed(n_words,
						dd->base + SPI_MX_READ_COUNT);
			else
				writel_relaxed(0,
						dd->base + SPI_MX_READ_COUNT);
			writel_relaxed(0, dd->base + SPI_MX_INPUT_COUNT);
		} else
			writel_relaxed(n_words, dd->base + SPI_MX_INPUT_COUNT);
	} else {
		/* must be zero for BAM and DMOV */
		writel_relaxed(0, dd->base + SPI_MX_READ_COUNT);
		msm_spi_set_write_count(dd, 0);

		/*
		 * for DMA transfers, both QUP_MX_INPUT_COUNT and
		 * QUP_MX_OUTPUT_COUNT must be zero to all cases but one.
		 * That case is a non-balanced transfer when there is
		 * only a read_buf.
		 */
		if (dd->qup_ver == SPI_QUP_VERSION_BFAM) {
			if (dd->write_buf)
				writel_relaxed(0,
						dd->base + SPI_MX_INPUT_COUNT);
			else
				writel_relaxed(n_words,
						dd->base + SPI_MX_INPUT_COUNT);

			writel_relaxed(0, dd->base + SPI_MX_OUTPUT_COUNT);
		}
	}
}

static int msm_spi_bam_pipe_disconnect(struct msm_spi *dd,
						struct msm_spi_bam_pipe  *pipe)
{
	int ret = sps_disconnect(pipe->handle);

	if (ret) {
		dev_dbg(dd->dev, "%s disconnect bam %s pipe failed\n",
			__func__, pipe->name);
		return ret;
	}
	return 0;
}

static int msm_spi_bam_pipe_connect(struct msm_spi *dd,
		struct msm_spi_bam_pipe  *pipe, struct sps_connect *config)
{
	int ret;
	struct sps_register_event event  = {
		.mode      = SPS_TRIGGER_WAIT,
		.options   = SPS_O_EOT,
	};

	if (pipe == &dd->bam.prod)
		event.xfer_done = &dd->rx_transfer_complete;
	else if (pipe == &dd->bam.cons)
		event.xfer_done = &dd->tx_transfer_complete;

	ret = sps_connect(pipe->handle, config);
	if (ret) {
		dev_err(dd->dev, "%s: sps_connect(%s:0x%pK):%d\n",
				__func__, pipe->name, pipe->handle, ret);
		return ret;
	}

	ret = sps_register_event(pipe->handle, &event);
	if (ret) {
		dev_err(dd->dev, "%s sps_register_event(hndl:0x%pK %s):%d\n",
				__func__, pipe->handle, pipe->name, ret);
		msm_spi_bam_pipe_disconnect(dd, pipe);
		return ret;
	}

	pipe->teardown_required = true;
	return 0;
}


static void msm_spi_bam_pipe_flush(struct msm_spi *dd,
					enum msm_spi_pipe_direction pipe_dir)
{
	struct msm_spi_bam_pipe *pipe = (pipe_dir == SPI_BAM_CONSUMER_PIPE) ?
					(&dd->bam.prod) : (&dd->bam.cons);
	struct sps_connect           config  = pipe->config;
	int    ret;

	ret = msm_spi_bam_pipe_disconnect(dd, pipe);
	if (ret)
		return;

	ret = msm_spi_bam_pipe_connect(dd, pipe, &config);
	if (ret)
		return;
}

static void msm_spi_bam_flush(struct msm_spi *dd)
{
	dev_dbg(dd->dev, "%s flushing bam for recovery\n", __func__);

	msm_spi_bam_pipe_flush(dd, SPI_BAM_CONSUMER_PIPE);
	msm_spi_bam_pipe_flush(dd, SPI_BAM_PRODUCER_PIPE);
}

static int
msm_spi_bam_process_rx(struct msm_spi *dd, u32 *bytes_to_send, u32 desc_cnt)
{
	int ret = 0;
	u32 data_xfr_size = 0, rem_bc = 0;
	u32 prod_flags = 0;

	rem_bc = dd->cur_rx_transfer->len - dd->bam.curr_rx_bytes_recvd;
	data_xfr_size = (rem_bc < *bytes_to_send) ? rem_bc : *bytes_to_send;

	/*
	 * set flags for last descriptor only
	 */
	if ((desc_cnt == 1)
		|| (*bytes_to_send == data_xfr_size))
		prod_flags = (dd->write_buf)
			? 0 : (SPS_IOVEC_FLAG_EOT | SPS_IOVEC_FLAG_NWD);

	/*
	 * enqueue read buffer in BAM
	 */
	ret = sps_transfer_one(dd->bam.prod.handle,
			dd->cur_rx_transfer->rx_dma
				+ dd->bam.curr_rx_bytes_recvd,
			data_xfr_size, dd, prod_flags);
	if (ret < 0) {
		dev_err(dd->dev,
		"%s: Failed to queue producer BAM transfer\n",
		__func__);
		return ret;
	}

	dd->bam.curr_rx_bytes_recvd += data_xfr_size;
	*bytes_to_send -= data_xfr_size;
	dd->bam.bam_rx_len -= data_xfr_size;
	return data_xfr_size;
}

static int
msm_spi_bam_process_tx(struct msm_spi *dd, u32 *bytes_to_send, u32 desc_cnt)
{
	int ret = 0;
	u32 data_xfr_size = 0, rem_bc = 0;
	u32 cons_flags = 0;

	rem_bc = dd->cur_tx_transfer->len - dd->bam.curr_tx_bytes_sent;
	data_xfr_size = (rem_bc < *bytes_to_send) ? rem_bc : *bytes_to_send;

	/*
	 * set flags for last descriptor only
	 */
	if ((desc_cnt == 1)
		|| (*bytes_to_send == data_xfr_size))
		cons_flags = SPS_IOVEC_FLAG_EOT | SPS_IOVEC_FLAG_NWD;

	/*
	 * enqueue write buffer in BAM
	 */
	ret = sps_transfer_one(dd->bam.cons.handle,
			dd->cur_tx_transfer->tx_dma
				+ dd->bam.curr_tx_bytes_sent,
			data_xfr_size, dd, cons_flags);
	if (ret < 0) {
		dev_err(dd->dev,
		"%s: Failed to queue consumer BAM transfer\n",
		__func__);
		return ret;
	}

	dd->bam.curr_tx_bytes_sent	+= data_xfr_size;
	*bytes_to_send	-= data_xfr_size;
	dd->bam.bam_tx_len -= data_xfr_size;
	return data_xfr_size;
}


/**
 * msm_spi_bam_begin_transfer: transfer dd->tx_bytes_remaining bytes
 * using BAM.
 * @brief BAM can transfer SPI_MAX_TRFR_BTWN_RESETS byte at a single
 * transfer. Between transfer QUP must change to reset state. A loop is
 * issuing a single BAM transfer at a time.
 * @return zero on success
 */
static int
msm_spi_bam_begin_transfer(struct msm_spi *dd)
{
	u32 tx_bytes_to_send = 0, rx_bytes_to_recv = 0;
	u32 n_words_xfr;
	s32 ret = 0;
	u32 prod_desc_cnt = SPI_BAM_MAX_DESC_NUM - 1;
	u32 cons_desc_cnt = SPI_BAM_MAX_DESC_NUM - 1;
	u32 byte_count = 0;

	rx_bytes_to_recv = min_t(u32, dd->bam.bam_rx_len,
				SPI_MAX_TRFR_BTWN_RESETS);
	tx_bytes_to_send = min_t(u32, dd->bam.bam_tx_len,
				SPI_MAX_TRFR_BTWN_RESETS);
	n_words_xfr = DIV_ROUND_UP(rx_bytes_to_recv,
				dd->bytes_per_word);

	msm_spi_set_mx_counts(dd, n_words_xfr);
	ret = msm_spi_set_state(dd, SPI_OP_STATE_RUN);
	if (ret < 0) {
		dev_err(dd->dev,
			"%s: Failed to set QUP state to run\n",
			__func__);
		goto xfr_err;
	}

	while ((rx_bytes_to_recv + tx_bytes_to_send) &&
		((cons_desc_cnt + prod_desc_cnt) > 0)) {
		struct spi_transfer *t = NULL;

		if (dd->read_buf && (prod_desc_cnt > 0)) {
			ret = msm_spi_bam_process_rx(dd, &rx_bytes_to_recv,
							prod_desc_cnt);
			if (ret < 0)
				goto xfr_err;

			if (!(dd->cur_rx_transfer->len
				- dd->bam.curr_rx_bytes_recvd))
				t = dd->cur_rx_transfer;
			prod_desc_cnt--;
		}

		if (dd->write_buf && (cons_desc_cnt > 0)) {
			ret = msm_spi_bam_process_tx(dd, &tx_bytes_to_send,
							cons_desc_cnt);
			if (ret < 0)
				goto xfr_err;

			if (!(dd->cur_tx_transfer->len
				- dd->bam.curr_tx_bytes_sent))
				t = dd->cur_tx_transfer;
			cons_desc_cnt--;
		}

		byte_count += ret;
	}

	dd->tx_bytes_remaining -= min_t(u32, byte_count,
						SPI_MAX_TRFR_BTWN_RESETS);
	return 0;
xfr_err:
	return ret;
}

static int
msm_spi_bam_next_transfer(struct msm_spi *dd)
{
	if (dd->tx_mode != SPI_BAM_MODE)
		return 0;

	if (dd->tx_bytes_remaining > 0) {
		if (msm_spi_set_state(dd, SPI_OP_STATE_RESET))
			return 0;
		if ((msm_spi_bam_begin_transfer(dd)) < 0) {
			dev_err(dd->dev, "%s: BAM transfer setup failed\n",
				__func__);
			return 0;
		}
		return 1;
	}
	return 0;
}

static int msm_spi_dma_send_next(struct msm_spi *dd)
{
	int ret = 0;

	if (dd->tx_mode == SPI_BAM_MODE)
		ret = msm_spi_bam_next_transfer(dd);
	return ret;
}

static inline void msm_spi_ack_transfer(struct msm_spi *dd)
{
	writel_relaxed(SPI_OP_MAX_INPUT_DONE_FLAG |
		       SPI_OP_MAX_OUTPUT_DONE_FLAG,
		       dd->base + SPI_OPERATIONAL);
	/* Ensure done flag was cleared before proceeding further */
	mb();
}

/* Figure which irq occurred and call the relevant functions */
static inline irqreturn_t msm_spi_qup_irq(int irq, void *dev_id)
{
	u32 op, ret = IRQ_NONE;
	struct msm_spi *dd = dev_id;

	if (pm_runtime_suspended(dd->dev)) {
		dev_warn(dd->dev, "QUP: pm runtime suspend, irq:%d\n", irq);
		return ret;
	}
	if (readl_relaxed(dd->base + SPI_ERROR_FLAGS) ||
	    readl_relaxed(dd->base + QUP_ERROR_FLAGS)) {
		struct spi_master *master = dev_get_drvdata(dd->dev);

		ret |= msm_spi_error_irq(irq, master);
	}

	op = readl_relaxed(dd->base + SPI_OPERATIONAL);
	writel_relaxed(op, dd->base + SPI_OPERATIONAL);
	/*
	 * Ensure service flag was cleared before further
	 * processing of interrupt.
	 */
	mb();
	if (op & SPI_OP_INPUT_SERVICE_FLAG)
		ret |= msm_spi_input_irq(irq, dev_id);

	if (op & SPI_OP_OUTPUT_SERVICE_FLAG)
		ret |= msm_spi_output_irq(irq, dev_id);

	if (dd->tx_mode != SPI_BAM_MODE) {
		if (!dd->rx_done) {
			if (dd->rx_bytes_remaining == 0)
				dd->rx_done = true;
		}
		if (!dd->tx_done) {
			if (!dd->tx_bytes_remaining &&
					(op & SPI_OP_IP_FIFO_NOT_EMPTY)) {
				dd->tx_done = true;
			}
		}
	}
	if (dd->tx_done && dd->rx_done) {
		msm_spi_set_state(dd, SPI_OP_STATE_RESET);
		dd->tx_done = false;
		dd->rx_done = false;
		complete(&dd->rx_transfer_complete);
		complete(&dd->tx_transfer_complete);
	}
	return ret;
}

static irqreturn_t msm_spi_input_irq(int irq, void *dev_id)
{
	struct msm_spi	       *dd = dev_id;

	dd->stat_rx++;

	if (dd->rx_mode == SPI_MODE_NONE)
		return IRQ_HANDLED;

	if (dd->rx_mode == SPI_FIFO_MODE) {
		while ((readl_relaxed(dd->base + SPI_OPERATIONAL) &
			SPI_OP_IP_FIFO_NOT_EMPTY) &&
			(dd->rx_bytes_remaining > 0)) {
			msm_spi_read_word_from_fifo(dd);
		}
	} else if (dd->rx_mode == SPI_BLOCK_MODE) {
		int count = 0;

		while (dd->rx_bytes_remaining &&
				(count < dd->input_block_size)) {
			msm_spi_read_word_from_fifo(dd);
			count += SPI_MAX_BYTES_PER_WORD;
		}
	}

	return IRQ_HANDLED;
}

static void msm_spi_write_word_to_fifo(struct msm_spi *dd)
{
	u32    word;
	u8     byte;
	int    i;
	int   write_bytes =
		(dd->pack_words ? SPI_MAX_BYTES_PER_WORD : dd->bytes_per_word);

	word = 0;
	if (dd->write_buf) {
		for (i = 0; (i < write_bytes) &&
			     dd->tx_bytes_remaining; i++) {
			dd->tx_bytes_remaining--;
			byte = *dd->write_buf++;
			word |= (byte << (BITS_PER_BYTE * i));
		}
	} else
		if (dd->tx_bytes_remaining > write_bytes)
			dd->tx_bytes_remaining -= write_bytes;
		else
			dd->tx_bytes_remaining = 0;
	dd->write_xfr_cnt++;

	writel_relaxed(word, dd->base + SPI_OUTPUT_FIFO);
}

static inline void msm_spi_write_rmn_to_fifo(struct msm_spi *dd)
{
	int count = 0;

	if (dd->tx_mode == SPI_FIFO_MODE) {
		while ((dd->tx_bytes_remaining > 0) &&
			(count < dd->input_fifo_size) &&
		       !(readl_relaxed(dd->base + SPI_OPERATIONAL)
						& SPI_OP_OUTPUT_FIFO_FULL)) {
			msm_spi_write_word_to_fifo(dd);
			count++;
		}
	}

	if (dd->tx_mode == SPI_BLOCK_MODE) {
		while (dd->tx_bytes_remaining &&
				(count < dd->output_block_size)) {
			msm_spi_write_word_to_fifo(dd);
			count += SPI_MAX_BYTES_PER_WORD;
		}
	}
}

static irqreturn_t msm_spi_output_irq(int irq, void *dev_id)
{
	struct msm_spi	       *dd = dev_id;

	dd->stat_tx++;

	if (dd->tx_mode == SPI_MODE_NONE)
		return IRQ_HANDLED;

	/* Output FIFO is empty. Transmit any outstanding write data. */
	if ((dd->tx_mode == SPI_FIFO_MODE) || (dd->tx_mode == SPI_BLOCK_MODE))
		msm_spi_write_rmn_to_fifo(dd);

	return IRQ_HANDLED;
}

static irqreturn_t msm_spi_error_irq(int irq, void *dev_id)
{
	struct spi_master	*master = dev_id;
	struct msm_spi          *dd = spi_master_get_devdata(master);
	u32                      spi_err;

	spi_err = readl_relaxed(dd->base + SPI_ERROR_FLAGS);
	if (spi_err & SPI_ERR_OUTPUT_OVER_RUN_ERR)
		dev_warn(master->dev.parent, "SPI output overrun error\n");
	if (spi_err & SPI_ERR_INPUT_UNDER_RUN_ERR)
		dev_warn(master->dev.parent, "SPI input underrun error\n");
	if (spi_err & SPI_ERR_OUTPUT_UNDER_RUN_ERR)
		dev_warn(master->dev.parent, "SPI output underrun error\n");
	msm_spi_get_clk_err(dd, &spi_err);
	if (spi_err & SPI_ERR_CLK_OVER_RUN_ERR)
		dev_warn(master->dev.parent, "SPI clock overrun error\n");
	if (spi_err & SPI_ERR_CLK_UNDER_RUN_ERR)
		dev_warn(master->dev.parent, "SPI clock underrun error\n");
	msm_spi_clear_error_flags(dd);
	msm_spi_ack_clk_err(dd);
	/* Ensure clearing of QUP_ERROR_FLAGS was completed */
	mb();
	return IRQ_HANDLED;
}

static int msm_spi_bam_map_buffers(struct msm_spi *dd)
{
	int ret = -EINVAL;
	struct device *dev;
	struct spi_transfer *xfr;
	void *tx_buf, *rx_buf;
	u32 tx_len, rx_len;

	dev = dd->dev;
	xfr = dd->cur_transfer;

	tx_buf = (void *)xfr->tx_buf;
	rx_buf = xfr->rx_buf;
	tx_len = rx_len = xfr->len;
	if (tx_buf != NULL) {
		xfr->tx_dma = dma_map_single(dev, tx_buf,
						tx_len, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, xfr->tx_dma)) {
			ret = -ENOMEM;
			goto error;
		}
	}

	if (rx_buf != NULL) {
		xfr->rx_dma = dma_map_single(dev, rx_buf, rx_len,
						DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, xfr->rx_dma)) {
			if (tx_buf != NULL)
				dma_unmap_single(dev,
						xfr->tx_dma,
						tx_len, DMA_TO_DEVICE);
			ret = -ENOMEM;
			goto error;
		}
	}

	return 0;
error:
	msm_spi_dma_unmap_buffers(dd);
	return ret;
}

static int msm_spi_dma_map_buffers(struct msm_spi *dd)
{
	int ret = 0;

	if (dd->tx_mode == SPI_BAM_MODE)
		ret = msm_spi_bam_map_buffers(dd);
	return ret;
}

static void msm_spi_bam_unmap_buffers(struct msm_spi *dd)
{
	struct device *dev;
	struct spi_transfer *xfr;
	void *tx_buf, *rx_buf;
	u32  tx_len, rx_len;

	dev = dd->dev;
	xfr = dd->cur_transfer;

	tx_buf = (void *)xfr->tx_buf;
	rx_buf = xfr->rx_buf;
	tx_len = rx_len = xfr->len;
	if (tx_buf != NULL)
		dma_unmap_single(dev, xfr->tx_dma,
				tx_len, DMA_TO_DEVICE);

	if (rx_buf != NULL)
		dma_unmap_single(dev, xfr->rx_dma,
				rx_len, DMA_FROM_DEVICE);
}

static inline void msm_spi_dma_unmap_buffers(struct msm_spi *dd)
{
	if (dd->tx_mode == SPI_BAM_MODE)
		msm_spi_bam_unmap_buffers(dd);
}

/**
 * msm_spi_use_dma - decides whether to use Data-Mover or BAM for
 * the given transfer
 * @dd:       device
 * @tr:       transfer
 *
 * Start using DMA if:
 * 1. Is supported by HW
 * 2. Is not disabled by platform data
 * 3. Transfer size is greater than 3*block size.
 * 4. Buffers are aligned to cache line.
 * 5. Bytes-per-word is 8,16 or 32.
 */
static inline bool
msm_spi_use_dma(struct msm_spi *dd, struct spi_transfer *tr, u8 bpw)
{
	if (!dd->use_dma)
		return false;

	/* check constraints from platform data */
	if ((dd->qup_ver == SPI_QUP_VERSION_BFAM) && !dd->pdata->use_bam)
		return false;

	if (dd->cur_msg_len < 3*dd->input_block_size)
		return false;

	if ((dd->qup_ver != SPI_QUP_VERSION_BFAM) &&
		 !dd->read_len && !dd->write_len)
		return false;

	if (dd->qup_ver == SPI_QUP_VERSION_NONE) {
		u32 cache_line = dma_get_cache_alignment();

		if (tr->tx_buf) {
			if (!IS_ALIGNED((size_t)tr->tx_buf, cache_line))
				return false;
		}
		if (tr->rx_buf) {
			if (!IS_ALIGNED((size_t)tr->rx_buf, cache_line))
				return false;
		}

		if (tr->cs_change &&
		   ((bpw != 8) && (bpw != 16) && (bpw != 32)))
			return false;
	}

	return true;
}

/**
 * msm_spi_set_transfer_mode: Chooses optimal transfer mode. Sets dd->mode and
 * prepares to process a transfer.
 */
static void
msm_spi_set_transfer_mode(struct msm_spi *dd, u8 bpw, u32 read_count)
{
	if (msm_spi_use_dma(dd, dd->cur_transfer, bpw)) {
		dd->tx_mode = SPI_BAM_MODE;
		dd->rx_mode = SPI_BAM_MODE;
	} else {
		dd->rx_mode = SPI_FIFO_MODE;
		dd->tx_mode = SPI_FIFO_MODE;
		dd->read_len = dd->cur_transfer->len;
		dd->write_len = dd->cur_transfer->len;
	}
}

/**
 * msm_spi_set_qup_io_modes: prepares register QUP_IO_MODES to process a
 * transfer
 */
static void msm_spi_set_qup_io_modes(struct msm_spi *dd)
{
	u32 spi_iom;

	spi_iom = readl_relaxed(dd->base + SPI_IO_MODES);
	/* Set input and output transfer mode: FIFO, DMOV, or BAM */
	spi_iom &= ~(SPI_IO_M_INPUT_MODE | SPI_IO_M_OUTPUT_MODE);
	spi_iom = (spi_iom | (dd->tx_mode << OUTPUT_MODE_SHIFT));
	spi_iom = (spi_iom | (dd->rx_mode << INPUT_MODE_SHIFT));

	/* Always enable packing for the BAM mode and for non BAM mode only
	 * if bpw is % 8 and transfer length is % 4 Bytes.
	 */
	if (dd->tx_mode == SPI_BAM_MODE ||
		((dd->cur_msg_len % SPI_MAX_BYTES_PER_WORD == 0) &&
		(dd->cur_transfer->bits_per_word) &&
		(dd->cur_transfer->bits_per_word <= 32) &&
		(dd->cur_transfer->bits_per_word % 8 == 0))) {
		spi_iom |= SPI_IO_M_PACK_EN | SPI_IO_M_UNPACK_EN;
		dd->pack_words = true;
	} else {
		spi_iom &= ~(SPI_IO_M_PACK_EN | SPI_IO_M_UNPACK_EN);
		spi_iom |= SPI_IO_M_OUTPUT_BIT_SHIFT_EN;
		dd->pack_words = false;
	}

	writel_relaxed(spi_iom, dd->base + SPI_IO_MODES);
}

static u32 msm_spi_calc_spi_ioc_clk_polarity(u32 spi_ioc, u8 mode)
{
	if (mode & SPI_CPOL)
		spi_ioc |= SPI_IO_C_CLK_IDLE_HIGH;
	else
		spi_ioc &= ~SPI_IO_C_CLK_IDLE_HIGH;
	return spi_ioc;
}

/**
 * msm_spi_set_spi_io_control: prepares register SPI_IO_CONTROL to process the
 * next transfer
 * @return the new set value of SPI_IO_CONTROL
 */
static u32 msm_spi_set_spi_io_control(struct msm_spi *dd)
{
	u32 spi_ioc, spi_ioc_orig, chip_select;

	spi_ioc = readl_relaxed(dd->base + SPI_IO_CONTROL);
	spi_ioc_orig = spi_ioc;
	spi_ioc = msm_spi_calc_spi_ioc_clk_polarity(spi_ioc
						, dd->spi->mode);
	/* Set chip-select */
	chip_select = dd->spi->chip_select << 2;
	if ((spi_ioc & SPI_IO_C_CS_SELECT) != chip_select)
		spi_ioc = (spi_ioc & ~SPI_IO_C_CS_SELECT) | chip_select;
	if (!dd->cur_transfer->cs_change)
		spi_ioc |= SPI_IO_C_MX_CS_MODE;

	if (spi_ioc != spi_ioc_orig)
		writel_relaxed(spi_ioc, dd->base + SPI_IO_CONTROL);

	/*
	 * Ensure that the IO control mode register gets written
	 * before proceeding with the transfer.
	 */
	mb();
	return spi_ioc;
}

/**
 * msm_spi_set_qup_op_mask: prepares register QUP_OPERATIONAL_MASK to process
 * the next transfer
 */
static void msm_spi_set_qup_op_mask(struct msm_spi *dd)
{
	/* mask INPUT and OUTPUT service flags in to prevent IRQs on FIFO status
	 * change in BAM mode
	 */
	u32 mask = (dd->tx_mode == SPI_BAM_MODE) ?
		QUP_OP_MASK_OUTPUT_SERVICE_FLAG | QUP_OP_MASK_INPUT_SERVICE_FLAG
		: 0;
	writel_relaxed(mask, dd->base + QUP_OPERATIONAL_MASK);
}

static void get_transfer_length(struct msm_spi *dd)
{
	struct spi_transfer *xfer = dd->cur_transfer;

	dd->cur_msg_len = 0;
	dd->read_len = dd->write_len = 0;
	dd->bam.bam_tx_len = dd->bam.bam_rx_len = 0;

	if (xfer->tx_buf)
		dd->bam.bam_tx_len = dd->write_len = xfer->len;
	if (xfer->rx_buf)
		dd->bam.bam_rx_len = dd->read_len = xfer->len;
	dd->cur_msg_len = xfer->len;
}

static int msm_spi_process_transfer(struct msm_spi *dd)
{
	u8  bpw;
	u32 max_speed;
	u32 read_count;
	u32 timeout;
	u32 spi_ioc;
	u32 int_loopback = 0;
	int ret;
	int status = 0;

	get_transfer_length(dd);
	dd->cur_tx_transfer = dd->cur_transfer;
	dd->cur_rx_transfer = dd->cur_transfer;
	dd->bam.curr_rx_bytes_recvd = dd->bam.curr_tx_bytes_sent = 0;
	dd->write_xfr_cnt = dd->read_xfr_cnt = 0;
	dd->tx_bytes_remaining = dd->cur_msg_len;
	dd->rx_bytes_remaining = dd->cur_msg_len;
	dd->read_buf           = dd->cur_transfer->rx_buf;
	dd->write_buf          = dd->cur_transfer->tx_buf;
	dd->tx_done = false;
	dd->rx_done = false;
	init_completion(&dd->tx_transfer_complete);
	init_completion(&dd->rx_transfer_complete);
	if (dd->cur_transfer->bits_per_word)
		bpw = dd->cur_transfer->bits_per_word;
	else
		bpw = 8;
	dd->bytes_per_word = (bpw + 7) / 8;

	if (dd->cur_transfer->speed_hz)
		max_speed = dd->cur_transfer->speed_hz;
	else
		max_speed = dd->spi->max_speed_hz;
	if (!dd->clock_speed || max_speed != dd->clock_speed)
		msm_spi_clock_set(dd, max_speed);

	timeout = 100 * msecs_to_jiffies(
			DIV_ROUND_UP(dd->cur_msg_len * 8,
			DIV_ROUND_UP(max_speed, MSEC_PER_SEC)));

	read_count = DIV_ROUND_UP(dd->cur_msg_len, dd->bytes_per_word);
	if (dd->spi->mode & SPI_LOOP)
		int_loopback = 1;

	ret = msm_spi_set_state(dd, SPI_OP_STATE_RESET);
	if (ret < 0) {
		dev_err(dd->dev,
			"%s: Error setting QUP to reset-state\n",
			__func__);
		return ret;
	}

	msm_spi_set_transfer_mode(dd, bpw, read_count);
	msm_spi_set_mx_counts(dd, read_count);
	if (dd->tx_mode == SPI_BAM_MODE) {
		ret = msm_spi_dma_map_buffers(dd);
		if (ret < 0) {
			pr_err("%s(): Error Mapping DMA buffers\n", __func__);
			dd->tx_mode = SPI_MODE_NONE;
			dd->rx_mode = SPI_MODE_NONE;
			return ret;
		}
	}
	msm_spi_set_qup_io_modes(dd);
	msm_spi_set_spi_config(dd, bpw);
	msm_spi_set_qup_config(dd, bpw);
	spi_ioc = msm_spi_set_spi_io_control(dd);
	msm_spi_set_qup_op_mask(dd);

	/* The output fifo interrupt handler will handle all writes after
	 * the first. Restricting this to one write avoids contention
	 * issues and race conditions between this thread and the int handler
	 */
	if (dd->tx_mode != SPI_BAM_MODE) {
		if (msm_spi_prepare_for_write(dd))
			goto transfer_end;
		msm_spi_start_write(dd, read_count);
	} else {
		if ((msm_spi_bam_begin_transfer(dd)) < 0) {
			dev_err(dd->dev, "%s: BAM transfer setup failed\n",
				__func__);
			status = -EIO;
			goto transfer_end;
		}
	}

	/*
	 * On BAM mode, current state here is run.
	 * Only enter the RUN state after the first word is written into
	 * the output FIFO. Otherwise, the output FIFO EMPTY interrupt
	 * might fire before the first word is written resulting in a
	 * possible race condition.
	 */
	if (dd->tx_mode != SPI_BAM_MODE)
		if (msm_spi_set_state(dd, SPI_OP_STATE_RUN)) {
			dev_warn(dd->dev,
				"%s: Failed to set QUP to run-state. Mode:%d\n",
				__func__, dd->tx_mode);
			goto transfer_end;
		}

	/* Assume success, this might change later upon transaction result */
	do {
		if (dd->write_buf &&
		    !wait_for_completion_timeout(&dd->tx_transfer_complete,
		    timeout)) {
			dev_err(dd->dev, "%s: SPI Tx transaction timeout\n",
				__func__);
			status = -EIO;
			break;
		}

		if (dd->read_buf &&
		    !wait_for_completion_timeout(&dd->rx_transfer_complete,
		    timeout)) {
			dev_err(dd->dev, "%s: SPI Rx transaction timeout\n",
				__func__);
			status = -EIO;
			break;
		}
	} while (msm_spi_dma_send_next(dd));

	msm_spi_udelay(dd->xfrs_delay_usec);

transfer_end:
	if ((dd->tx_mode == SPI_BAM_MODE) && status)
		msm_spi_bam_flush(dd);
	msm_spi_dma_unmap_buffers(dd);
	dd->tx_mode = SPI_MODE_NONE;
	dd->rx_mode = SPI_MODE_NONE;

	msm_spi_set_state(dd, SPI_OP_STATE_RESET);
	if (!dd->cur_transfer->cs_change)
		writel_relaxed(spi_ioc & ~SPI_IO_C_MX_CS_MODE,
		       dd->base + SPI_IO_CONTROL);
	return status;
}


static inline void msm_spi_set_cs(struct spi_device *spi, bool set_flag)
{
	struct msm_spi *dd = spi_master_get_devdata(spi->master);
	u32 spi_ioc;
	u32 spi_ioc_orig;
	int rc = 0;

	rc = pm_runtime_get_sync(dd->dev);
	if (rc < 0) {
		dev_err(dd->dev, "Failure during runtime get,rc=%d\n", rc);
		return;
	}

	if (dd->pdata->is_shared) {
		rc = get_local_resources(dd);
		if (rc)
			return;
	}

	msm_spi_clk_path_vote(dd, spi->max_speed_hz);

	if (!(spi->mode & SPI_CS_HIGH))
		set_flag = !set_flag;

	/* Serve only under mutex lock as RT suspend may cause a race */
	mutex_lock(&dd->core_lock);
	if (dd->suspended) {
		dev_err(dd->dev, "%s: SPI operational state=%d Invalid\n",
			__func__, dd->suspended);
		mutex_unlock(&dd->core_lock);
		return;
	}

	spi_ioc = readl_relaxed(dd->base + SPI_IO_CONTROL);
	spi_ioc_orig = spi_ioc;
	if (set_flag)
		spi_ioc |= SPI_IO_C_FORCE_CS;
	else
		spi_ioc &= ~SPI_IO_C_FORCE_CS;

	if (spi_ioc != spi_ioc_orig)
		writel_relaxed(spi_ioc, dd->base + SPI_IO_CONTROL);
	if (dd->pdata->is_shared)
		put_local_resources(dd);
	mutex_unlock(&dd->core_lock);

	pm_runtime_mark_last_busy(dd->dev);
	pm_runtime_put_autosuspend(dd->dev);
}

static void reset_core(struct msm_spi *dd)
{
	u32 spi_ioc;

	msm_spi_register_init(dd);
	/*
	 * The SPI core generates a bogus input overrun error on some targets,
	 * when a transition from run to reset state occurs and if the FIFO has
	 * an odd number of entries. Hence we disable the INPUT_OVER_RUN_ERR_EN
	 * bit.
	 */
	msm_spi_enable_error_flags(dd);

	spi_ioc = readl_relaxed(dd->base + SPI_IO_CONTROL);
	spi_ioc |= SPI_IO_C_NO_TRI_STATE;
	writel_relaxed(spi_ioc, dd->base + SPI_IO_CONTROL);
	/*
	 * Ensure that the IO control is written to before returning.
	 */
	mb();
	msm_spi_set_state(dd, SPI_OP_STATE_RESET);
}

static void put_local_resources(struct msm_spi *dd)
{
	msm_spi_disable_irqs(dd);
	clk_disable_unprepare(dd->clk);
	dd->clock_speed = 0;
	clk_disable_unprepare(dd->pclk);

	/* Free  the spi clk, miso, mosi, cs gpio */
	if (dd->pdata && dd->pdata->gpio_release)
		dd->pdata->gpio_release();

	msm_spi_free_gpios(dd);
}

static int get_local_resources(struct msm_spi *dd)
{
	int ret = -EINVAL;

	/* Configure the spi clk, miso, mosi and cs gpio */
	if (dd->pdata->gpio_config) {
		ret = dd->pdata->gpio_config();
		if (ret) {
			dev_err(dd->dev,
					"%s: error configuring GPIOs\n",
					__func__);
			return ret;
		}
	}

	ret = msm_spi_request_gpios(dd);
	if (ret)
		return ret;

	ret = clk_prepare_enable(dd->clk);
	if (ret)
		goto clk0_err;
	ret = clk_prepare_enable(dd->pclk);
	if (ret)
		goto clk1_err;
	msm_spi_enable_irqs(dd);

	return 0;

clk1_err:
	clk_disable_unprepare(dd->clk);
clk0_err:
	msm_spi_free_gpios(dd);
	return ret;
}

/**
 * msm_spi_transfer_one: To process one spi transfer at a time
 * @master: spi master controller reference
 * @msg: one multi-segment SPI transaction
 * @return zero on success or negative error value
 *
 */
static int msm_spi_transfer_one(struct spi_master *master,
				struct spi_device *spi,
				struct spi_transfer *xfer)
{
	struct msm_spi	*dd;
	unsigned long        flags;
	u32	status_error = 0;

	dd = spi_master_get_devdata(master);

	/* Check message parameters */
	if (xfer->speed_hz > dd->pdata->max_clock_speed ||
	    (xfer->bits_per_word &&
	     (xfer->bits_per_word < 4 || xfer->bits_per_word > 32)) ||
	    (xfer->tx_buf == NULL && xfer->rx_buf == NULL)) {
		dev_err(dd->dev,
			"Invalid transfer: %d Hz, %d bpw tx=%pK, rx=%pK\n",
			xfer->speed_hz, xfer->bits_per_word,
			xfer->tx_buf, xfer->rx_buf);
		return -EINVAL;
	}
	dd->spi = spi;
	dd->cur_transfer = xfer;

	mutex_lock(&dd->core_lock);

	spin_lock_irqsave(&dd->queue_lock, flags);
	dd->transfer_pending = true;
	spin_unlock_irqrestore(&dd->queue_lock, flags);
	/*
	 * get local resources for each transfer to ensure we're in a good
	 * state and not interfering with other EE's using this device
	 */
	if (dd->pdata->is_shared) {
		if (get_local_resources(dd)) {
			mutex_unlock(&dd->core_lock);
			spi_finalize_current_message(master);
			return -EINVAL;
		}

		reset_core(dd);
		if (dd->use_dma) {
			msm_spi_bam_pipe_connect(dd, &dd->bam.prod,
					&dd->bam.prod.config);
			msm_spi_bam_pipe_connect(dd, &dd->bam.cons,
					&dd->bam.cons.config);
		}
	}

	if (dd->suspended || !msm_spi_is_valid_state(dd)) {
		dev_err(dd->dev, "%s: SPI operational state not valid\n",
			__func__);
		status_error = 1;
	}


	if (!status_error)
		status_error =
			msm_spi_process_transfer(dd);

	spin_lock_irqsave(&dd->queue_lock, flags);
	dd->transfer_pending = false;
	spin_unlock_irqrestore(&dd->queue_lock, flags);

	/*
	 * Put local resources prior to calling finalize to ensure the hw
	 * is in a known state before notifying the calling thread (which is a
	 * different context since we're running in the spi kthread here) to
	 * prevent race conditions between us and any other EE's using this hw.
	 */
	if (dd->pdata->is_shared) {
		if (dd->use_dma) {
			msm_spi_bam_pipe_disconnect(dd, &dd->bam.prod);
			msm_spi_bam_pipe_disconnect(dd, &dd->bam.cons);
		}
		put_local_resources(dd);
	}
	mutex_unlock(&dd->core_lock);
	if (dd->suspended)
		wake_up_interruptible(&dd->continue_suspend);
	return status_error;
}

static int msm_spi_prepare_transfer_hardware(struct spi_master *master)
{
	struct msm_spi	*dd = spi_master_get_devdata(master);
	int resume_state = 0;

	/*
	 * Counter-part of system-suspend when runtime-pm is not enabled.
	 * This way, resume can be left empty and device will be put in
	 * active mode only if client requests anything on the bus
	 */
	if (!pm_runtime_enabled(dd->dev)) {
		resume_state = msm_spi_pm_resume_runtime(dd->dev);
		if (resume_state < 0)
			goto spi_finalize;
	} else {
		resume_state = pm_runtime_get_sync(dd->dev);
		if (resume_state < 0)
			goto spi_finalize;
	}
	if (dd->suspended) {
		resume_state = -EBUSY;
		goto spi_finalize;
	}
	return 0;

spi_finalize:
	spi_finalize_current_message(master);
	return resume_state;
}

static int msm_spi_unprepare_transfer_hardware(struct spi_master *master)
{
	struct msm_spi	*dd = spi_master_get_devdata(master);

	pm_runtime_mark_last_busy(dd->dev);
	pm_runtime_put_autosuspend(dd->dev);
	return 0;
}

static int msm_spi_setup(struct spi_device *spi)
{
	struct msm_spi	*dd;
	int              rc = 0;
	u32              spi_ioc;
	u32              spi_config;
	u32              mask;

	if (spi->bits_per_word < 4 || spi->bits_per_word > 32) {
		dev_err(&spi->dev, "%s: invalid bits_per_word %d\n",
			__func__, spi->bits_per_word);
		return -EINVAL;
	}
	if (spi->chip_select > SPI_NUM_CHIPSELECTS-1) {
		dev_err(&spi->dev, "%s, chip select %d exceeds max value %d\n",
			__func__, spi->chip_select, SPI_NUM_CHIPSELECTS - 1);
		return -EINVAL;
	}

	dd = spi_master_get_devdata(spi->master);

	rc = pm_runtime_get_sync(dd->dev);
	if (rc < 0 && !dd->is_init_complete &&
			pm_runtime_enabled(dd->dev)) {
		pm_runtime_set_suspended(dd->dev);
		pm_runtime_put_sync(dd->dev);
		rc = 0;
		goto err_setup_exit;
	} else
		rc = 0;

	mutex_lock(&dd->core_lock);

	/* Counter-part of system-suspend when runtime-pm is not enabled. */
	if (!pm_runtime_enabled(dd->dev)) {
		rc = msm_spi_pm_resume_runtime(dd->dev);
		if (rc < 0 && !dd->is_init_complete) {
			rc = 0;
			mutex_unlock(&dd->core_lock);
			goto err_setup_exit;
		}
	}

	if (dd->suspended) {
		rc = -EBUSY;
		mutex_unlock(&dd->core_lock);
		goto err_setup_exit;
	}

	if (dd->pdata->is_shared) {
		rc = get_local_resources(dd);
		if (rc)
			goto no_resources;
	}

	spi_ioc = readl_relaxed(dd->base + SPI_IO_CONTROL);
	mask = SPI_IO_C_CS_N_POLARITY_0 << spi->chip_select;
	if (spi->mode & SPI_CS_HIGH)
		spi_ioc |= mask;
	else
		spi_ioc &= ~mask;
	spi_ioc = msm_spi_calc_spi_ioc_clk_polarity(spi_ioc, spi->mode);

	writel_relaxed(spi_ioc, dd->base + SPI_IO_CONTROL);

	spi_config = readl_relaxed(dd->base + SPI_CONFIG);
	spi_config = msm_spi_calc_spi_config_loopback_and_input_first(
							spi_config, spi->mode);
	writel_relaxed(spi_config, dd->base + SPI_CONFIG);

	/* Ensure previous write completed before disabling the clocks */
	mb();
	if (dd->pdata->is_shared)
		put_local_resources(dd);
	/* Counter-part of system-resume when runtime-pm is not enabled. */
	if (!pm_runtime_enabled(dd->dev))
		msm_spi_pm_suspend_runtime(dd->dev);

no_resources:
	mutex_unlock(&dd->core_lock);
	pm_runtime_mark_last_busy(dd->dev);
	pm_runtime_put_autosuspend(dd->dev);

err_setup_exit:
	return rc;
}

#ifdef CONFIG_DEBUG_FS
static int debugfs_iomem_x32_set(void *data, u64 val)
{
	struct msm_spi_debugfs_data *reg = (struct msm_spi_debugfs_data *)data;
	struct msm_spi *dd = reg->dd;
	int ret;

	ret = pm_runtime_get_sync(dd->dev);
	if (ret < 0)
		return ret;

	writel_relaxed(val, (dd->base + reg->offset));
	/* Ensure the previous write completed. */
	mb();

	pm_runtime_mark_last_busy(dd->dev);
	pm_runtime_put_autosuspend(dd->dev);
	return 0;
}

static int debugfs_iomem_x32_get(void *data, u64 *val)
{
	struct msm_spi_debugfs_data *reg = (struct msm_spi_debugfs_data *)data;
	struct msm_spi *dd = reg->dd;
	int ret;

	ret = pm_runtime_get_sync(dd->dev);
	if (ret < 0)
		return ret;
	*val = readl_relaxed(dd->base + reg->offset);
	/* Ensure the previous read completed. */
	mb();

	pm_runtime_mark_last_busy(dd->dev);
	pm_runtime_put_autosuspend(dd->dev);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_iomem_x32, debugfs_iomem_x32_get,
			debugfs_iomem_x32_set, "0x%08llx\n");

static void spi_debugfs_init(struct msm_spi *dd)
{
	char dir_name[20];

	scnprintf(dir_name, sizeof(dir_name), "%s_dbg", dev_name(dd->dev));
	dd->dent_spi = debugfs_create_dir(dir_name, NULL);
	if (dd->dent_spi) {
		int i;

		for (i = 0; i < ARRAY_SIZE(debugfs_spi_regs); i++) {
			dd->reg_data[i].offset = debugfs_spi_regs[i].offset;
			dd->reg_data[i].dd = dd;
			dd->debugfs_spi_regs[i] =
			   debugfs_create_file(
			       debugfs_spi_regs[i].name,
			       debugfs_spi_regs[i].mode,
			       dd->dent_spi, &dd->reg_data[i],
			       &fops_iomem_x32);
		}
	}
}

static void spi_debugfs_exit(struct msm_spi *dd)
{
	if (dd->dent_spi) {
		int i;

		debugfs_remove_recursive(dd->dent_spi);
		dd->dent_spi = NULL;
		for (i = 0; i < ARRAY_SIZE(debugfs_spi_regs); i++)
			dd->debugfs_spi_regs[i] = NULL;
	}
}
#else
static void spi_debugfs_init(struct msm_spi *dd) {}
static void spi_debugfs_exit(struct msm_spi *dd) {}
#endif

/* ===Device attributes begin=== */
static ssize_t stats_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct msm_spi *dd =  spi_master_get_devdata(master);

	return scnprintf(buf, PAGE_SIZE,
			"Device       %s\n"
			"rx fifo_size = %d spi words\n"
			"tx fifo_size = %d spi words\n"
			"use_dma ?    %s\n"
			"rx block size = %d bytes\n"
			"tx block size = %d bytes\n"
			"input burst size = %d bytes\n"
			"output burst size = %d bytes\n"
			"DMA configuration:\n"
			"tx_ch=%d, rx_ch=%d, tx_crci= %d, rx_crci=%d\n"
			"--statistics--\n"
			"Rx isrs  = %d\n"
			"Tx isrs  = %d\n"
			"--debug--\n"
			"NA yet\n",
			dev_name(dev),
			dd->input_fifo_size,
			dd->output_fifo_size,
			dd->use_dma ? "yes" : "no",
			dd->input_block_size,
			dd->output_block_size,
			dd->input_burst_size,
			dd->output_burst_size,
			dd->tx_dma_chan,
			dd->rx_dma_chan,
			dd->tx_dma_crci,
			dd->rx_dma_crci,
			dd->stat_rx,
			dd->stat_tx
			);
}

/* Reset statistics on write */
static ssize_t stats_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct msm_spi *dd = dev_get_drvdata(dev);

	dd->stat_rx = 0;
	dd->stat_tx = 0;
	return count;
}

static DEVICE_ATTR_RW(stats);

static struct attribute *dev_attrs[] = {
	&dev_attr_stats.attr,
	NULL,
};

static struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};
/* ===Device attributes end=== */

static void msm_spi_bam_pipe_teardown(struct msm_spi *dd,
					enum msm_spi_pipe_direction pipe_dir)
{
	struct msm_spi_bam_pipe *pipe = (pipe_dir == SPI_BAM_CONSUMER_PIPE) ?
					(&dd->bam.prod) : (&dd->bam.cons);
	if (!pipe->teardown_required)
		return;

	msm_spi_bam_pipe_disconnect(dd, pipe);
	dma_free_coherent(dd->dev, pipe->config.desc.size,
		pipe->config.desc.base, pipe->config.desc.phys_base);
	sps_free_endpoint(pipe->handle);
	pipe->handle = NULL;
	pipe->teardown_required = false;
}

static int msm_spi_bam_pipe_init(struct msm_spi *dd,
					enum msm_spi_pipe_direction pipe_dir)
{
	int rc = 0;
	struct sps_pipe *pipe_handle;
	struct msm_spi_bam_pipe *pipe = (pipe_dir == SPI_BAM_CONSUMER_PIPE) ?
					(&dd->bam.prod) : (&dd->bam.cons);
	struct sps_connect *pipe_conf = &pipe->config;

	pipe->name   = (pipe_dir == SPI_BAM_CONSUMER_PIPE) ? "cons" : "prod";
	pipe->handle = NULL;
	pipe_handle  = sps_alloc_endpoint();
	if (!pipe_handle) {
		dev_err(dd->dev, "%s: Failed to allocate BAM endpoint\n"
								, __func__);
		return -ENOMEM;
	}

	memset(pipe_conf, 0, sizeof(*pipe_conf));
	rc = sps_get_config(pipe_handle, pipe_conf);
	if (rc) {
		dev_err(dd->dev, "%s: Failed to get BAM pipe config\n"
			, __func__);
		goto config_err;
	}

	if (pipe_dir == SPI_BAM_CONSUMER_PIPE) {
		pipe_conf->source          = dd->bam.handle;
		pipe_conf->destination     = SPS_DEV_HANDLE_MEM;
		pipe_conf->mode            = SPS_MODE_SRC;
		pipe_conf->src_pipe_index  =
					dd->pdata->bam_producer_pipe_index;
		pipe_conf->dest_pipe_index = 0;
	} else {
		pipe_conf->source          = SPS_DEV_HANDLE_MEM;
		pipe_conf->destination     = dd->bam.handle;
		pipe_conf->mode            = SPS_MODE_DEST;
		pipe_conf->src_pipe_index  = 0;
		pipe_conf->dest_pipe_index =
					dd->pdata->bam_consumer_pipe_index;
	}
	pipe_conf->options = SPS_O_EOT | SPS_O_AUTO_ENABLE;
	pipe_conf->desc.size = SPI_BAM_MAX_DESC_NUM * sizeof(struct sps_iovec);
	pipe_conf->desc.base = dma_alloc_coherent(dd->dev,
				pipe_conf->desc.size,
				&pipe_conf->desc.phys_base,
				GFP_KERNEL);
	if (!pipe_conf->desc.base) {
		dev_err(dd->dev, "%s: Failed allocate BAM pipe memory\n"
			, __func__);
		rc = -ENOMEM;
		goto config_err;
	}

	pipe->handle = pipe_handle;

	return 0;

config_err:
	sps_free_endpoint(pipe_handle);

	return rc;
}

static void msm_spi_bam_teardown(struct msm_spi *dd)
{
	msm_spi_bam_pipe_teardown(dd, SPI_BAM_PRODUCER_PIPE);
	msm_spi_bam_pipe_teardown(dd, SPI_BAM_CONSUMER_PIPE);

	if (dd->bam.deregister_required) {
		sps_deregister_bam_device(dd->bam.handle);
		dd->bam.deregister_required = false;
	}
}

static int msm_spi_bam_init(struct msm_spi *dd)
{
	struct sps_bam_props bam_props = {0};
	uintptr_t bam_handle;
	int rc = 0;

	rc = sps_phy2h(dd->bam.phys_addr, &bam_handle);
	if (rc || !bam_handle) {
		bam_props.phys_addr = dd->bam.phys_addr;
		bam_props.virt_addr = dd->bam.base;
		bam_props.irq       = dd->bam.irq;
		bam_props.manage    = SPS_BAM_MGR_DEVICE_REMOTE;
		bam_props.summing_threshold = 0x10;

		rc = sps_register_bam_device(&bam_props, &bam_handle);
		if (rc) {
			dev_err(dd->dev,
				"%s: Failed to register BAM device\n",
				__func__);
			return rc;
		}
		dd->bam.deregister_required = true;
	}

	dd->bam.handle = bam_handle;

	rc = msm_spi_bam_pipe_init(dd, SPI_BAM_PRODUCER_PIPE);
	if (rc) {
		dev_err(dd->dev,
			"%s: Failed to init producer BAM-pipe\n",
			__func__);
		goto bam_init_error;
	}

	rc = msm_spi_bam_pipe_init(dd, SPI_BAM_CONSUMER_PIPE);
	if (rc) {
		dev_err(dd->dev,
			"%s: Failed to init consumer BAM-pipe\n",
			__func__);
		goto bam_init_error;
	}

	return 0;

bam_init_error:
	msm_spi_bam_teardown(dd);
	return rc;
}

enum msm_spi_dt_entry_status {
	DT_REQ,  /* Required:  fail if missing */
	DT_SGST, /* Suggested: warn if missing */
	DT_OPT,  /* Optional:  don't warn if missing */
};

enum msm_spi_dt_entry_type {
	DT_U32,
	DT_GPIO,
	DT_BOOL,
};

struct msm_spi_dt_to_pdata_map {
	const char                  *dt_name;
	void                        *ptr_data;
	enum msm_spi_dt_entry_status status;
	enum msm_spi_dt_entry_type   type;
	int                          default_val;
};

static int msm_spi_dt_to_pdata_populate(struct platform_device *pdev,
					struct msm_spi_platform_data *pdata,
					struct msm_spi_dt_to_pdata_map  *itr)
{
	int  ret, err = 0;
	struct device_node *node = pdev->dev.of_node;

	for (; itr->dt_name; ++itr) {
		switch (itr->type) {
		case DT_GPIO:
			ret = of_get_named_gpio(node, itr->dt_name, 0);
			if (ret >= 0) {
				*((int *) itr->ptr_data) = ret;
				ret = 0;
			}
			break;
		case DT_U32:
			ret = of_property_read_u32(node, itr->dt_name,
							 (u32 *) itr->ptr_data);
			break;
		case DT_BOOL:
			*((bool *) itr->ptr_data) =
				of_property_read_bool(node, itr->dt_name);
			ret = 0;
			break;
		default:
			dev_err(&pdev->dev, "%d is an unknown DT entry type\n",
								itr->type);
			ret = -EBADE;
		}

		dev_dbg(&pdev->dev, "DT entry ret:%d name:%s val:%d\n",
				ret, itr->dt_name, *((int *)itr->ptr_data));

		if (ret) {
			*((int *)itr->ptr_data) = itr->default_val;

			if (itr->status < DT_OPT) {
				dev_err(&pdev->dev, "Missing '%s' DT entry\n",
								itr->dt_name);

				/* cont on err to dump all missing entries */
				if (itr->status == DT_REQ && !err)
					err = ret;
			}
		}
	}

	return err;
}

/**
 * msm_spi_dt_to_pdata: create pdata and read gpio config from device tree
 */
static struct msm_spi_platform_data *msm_spi_dt_to_pdata(
			struct platform_device *pdev, struct msm_spi *dd)
{
	struct msm_spi_platform_data *pdata;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (pdata) {
		struct msm_spi_dt_to_pdata_map map[] = {
		{"spi-max-frequency",
			&pdata->max_clock_speed,         DT_SGST, DT_U32,   0},
		{"qcom,infinite-mode",
			&pdata->infinite_mode,           DT_OPT,  DT_U32,   0},
		{"qcom,master-id",
			&pdata->master_id,               DT_SGST, DT_U32,   0},
		{"qcom,bus-width",
			&pdata->bus_width,               DT_OPT, DT_U32,   8},
		{"qcom,ver-reg-exists",
			&pdata->ver_reg_exists,          DT_OPT,  DT_BOOL,  0},
		{"qcom,use-bam",
			&pdata->use_bam,                 DT_OPT,  DT_BOOL,  0},
		{"qcom,use-pinctrl",
			&pdata->use_pinctrl,             DT_OPT,  DT_BOOL,  0},
		{"qcom,bam-consumer-pipe-index",
			&pdata->bam_consumer_pipe_index, DT_OPT,  DT_U32,   0},
		{"qcom,bam-producer-pipe-index",
			&pdata->bam_producer_pipe_index, DT_OPT,  DT_U32,   0},
		{"qcom,gpio-clk",
			&dd->spi_gpios[0],               DT_OPT,  DT_GPIO, -1},
		{"qcom,gpio-miso",
			&dd->spi_gpios[1],               DT_OPT,  DT_GPIO, -1},
		{"qcom,gpio-mosi",
			&dd->spi_gpios[2],               DT_OPT,  DT_GPIO, -1},
		{"qcom,gpio-cs0",
			&dd->cs_gpios[0].gpio_num,       DT_OPT,  DT_GPIO, -1},
		{"qcom,gpio-cs1",
			&dd->cs_gpios[1].gpio_num,       DT_OPT,  DT_GPIO, -1},
		{"qcom,gpio-cs2",
			&dd->cs_gpios[2].gpio_num,       DT_OPT,  DT_GPIO, -1},
		{"qcom,gpio-cs3",
			&dd->cs_gpios[3].gpio_num,       DT_OPT,  DT_GPIO, -1},
		{"qcom,rt-priority",
			&pdata->rt_priority,		 DT_OPT,  DT_BOOL,  0},
		{"qcom,shared",
			&pdata->is_shared,		 DT_OPT,  DT_BOOL,  0},
		{NULL,  NULL,                            0,       0,        0},
		};

		if (msm_spi_dt_to_pdata_populate(pdev, pdata, map))
			return NULL;
	}

	if (pdata->use_bam) {
		if (!pdata->bam_consumer_pipe_index) {
			dev_warn(&pdev->dev,
			"missing qcom,bam-consumer-pipe-index entry in device-tree\n");
			pdata->use_bam = false;
		}

		if (!pdata->bam_producer_pipe_index) {
			dev_warn(&pdev->dev,
			"missing qcom,bam-producer-pipe-index entry in device-tree\n");
			pdata->use_bam = false;
		}
	}
	return pdata;
}

static int msm_spi_get_qup_hw_ver(struct device *dev, struct msm_spi *dd)
{
	u32 data = readl_relaxed(dd->base + QUP_HARDWARE_VER);

	return (data >= QUP_HARDWARE_VER_2_1_1) ? SPI_QUP_VERSION_BFAM
						: SPI_QUP_VERSION_NONE;
}

static int msm_spi_bam_get_resources(struct msm_spi *dd,
	struct platform_device *pdev, struct spi_master *master)
{
	struct resource *resource;
	size_t bam_mem_size;

	resource = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"spi_bam_physical");
	if (!resource) {
		dev_warn(&pdev->dev,
			"%s: Missing spi_bam_physical entry in DT\n",
			__func__);
		return -ENXIO;
	}

	dd->bam.phys_addr = resource->start;
	bam_mem_size = resource_size(resource);
	dd->bam.base = devm_ioremap(&pdev->dev, dd->bam.phys_addr,
					bam_mem_size);
	if (!dd->bam.base) {
		dev_warn(&pdev->dev,
			"%s: Failed to ioremap(spi_bam_physical)\n",
			__func__);
		return -ENXIO;
	}

	dd->bam.irq = platform_get_irq_byname(pdev, "spi_bam_irq");
	if (dd->bam.irq < 0) {
		dev_warn(&pdev->dev, "%s: Missing spi_bam_irq entry in DT\n",
			__func__);
		return -EINVAL;
	}

	dd->dma_init = msm_spi_bam_init;
	dd->dma_teardown = msm_spi_bam_teardown;
	return 0;
}

static int init_resources(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct msm_spi	  *dd;
	int               rc = -ENXIO;
	int               clk_enabled = 0;
	int               pclk_enabled = 0;

	dd = spi_master_get_devdata(master);

	if (dd->pdata && dd->pdata->use_pinctrl) {
		rc = msm_spi_pinctrl_init(dd);
		if (rc) {
			dev_err(&pdev->dev, "%s: pinctrl init failed\n",
					 __func__);
			return rc;
		}
	}

	mutex_lock(&dd->core_lock);

	dd->clk = clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(dd->clk)) {
		dev_err(&pdev->dev, "%s: unable to get core_clk\n", __func__);
		rc = PTR_ERR(dd->clk);
		goto err_clk_get;
	}

	dd->pclk = clk_get(&pdev->dev, "iface_clk");
	if (IS_ERR(dd->pclk)) {
		dev_err(&pdev->dev, "%s: unable to get iface_clk\n", __func__);
		rc = PTR_ERR(dd->pclk);
		goto err_pclk_get;
	}

	if (dd->pdata && dd->pdata->max_clock_speed)
		msm_spi_clock_set(dd, dd->pdata->max_clock_speed);

	rc = clk_prepare_enable(dd->clk);
	if (rc) {
		dev_err(&pdev->dev, "%s: unable to enable core_clk\n",
			__func__);
		goto err_clk_enable;
	}

	clk_enabled = 1;
	rc = clk_prepare_enable(dd->pclk);
	if (rc) {
		dev_err(&pdev->dev, "%s: unable to enable iface_clk\n",
		__func__);
		goto err_pclk_enable;
	}

	pclk_enabled = 1;

	if (dd->pdata && dd->pdata->ver_reg_exists) {
		enum msm_spi_qup_version ver =
					msm_spi_get_qup_hw_ver(&pdev->dev, dd);
		if (dd->qup_ver != ver)
			dev_warn(&pdev->dev,
			"%s: HW version different then assumed by probe\n",
			__func__);
	}

	/* GSBI dose not exists on B-family MSM-chips */
	if (dd->qup_ver != SPI_QUP_VERSION_BFAM) {
		rc = msm_spi_configure_gsbi(dd, pdev);
		if (rc)
			goto err_config_gsbi;
	}

	msm_spi_calculate_fifo_size(dd);
	if (dd->use_dma) {
		rc = dd->dma_init(dd);
		if (rc) {
			dev_err(&pdev->dev,
				"%s: failed to init DMA. Disabling DMA mode\n",
				__func__);
			dd->use_dma = false;
		}
	}

	msm_spi_register_init(dd);
	/*
	 * The SPI core generates a bogus input overrun error on some targets,
	 * when a transition from run to reset state occurs and if the FIFO has
	 * an odd number of entries. Hence we disable the INPUT_OVER_RUN_ERR_EN
	 * bit.
	 */
	msm_spi_enable_error_flags(dd);

	writel_relaxed(SPI_IO_C_NO_TRI_STATE, dd->base + SPI_IO_CONTROL);
	rc = msm_spi_set_state(dd, SPI_OP_STATE_RESET);
	if (rc)
		goto err_spi_state;

	clk_disable_unprepare(dd->clk);
	clk_disable_unprepare(dd->pclk);
	clk_enabled = 0;
	pclk_enabled = 0;

	dd->transfer_pending = false;
	dd->tx_mode = SPI_MODE_NONE;
	dd->rx_mode = SPI_MODE_NONE;

	rc = msm_spi_request_irq(dd, pdev, master);
	if (rc)
		goto err_irq;

	msm_spi_disable_irqs(dd);

	mutex_unlock(&dd->core_lock);
	return 0;

err_irq:
err_spi_state:
	if (dd->use_dma && dd->dma_teardown)
		dd->dma_teardown(dd);
err_config_gsbi:
	if (pclk_enabled)
		clk_disable_unprepare(dd->pclk);
err_pclk_enable:
	if (clk_enabled)
		clk_disable_unprepare(dd->clk);
err_clk_enable:
	clk_put(dd->pclk);
err_pclk_get:
	clk_put(dd->clk);
err_clk_get:
	mutex_unlock(&dd->core_lock);
	return rc;
}

/**
 * msm_spi_get_icc_path() - get icc path.
 * @dd:   Pointer to msm_spi structure.
 * @pdev: Pointer to platform_device structure.
 *
 * This function is used to get the ICC path.
 * Based on return value of of_icc_get, assign
 * the ret value.
 *
 * Return: zero on success, negative error code on failure.
 */
static int msm_spi_get_icc_path(struct msm_spi *dd,
				struct platform_device *pdev)
{
	int ret;

	dd->icc_path = of_icc_get(&pdev->dev, "blsp-ddr");
	if (IS_ERR_OR_NULL(dd->icc_path)) {
		ret = dd->icc_path ?
		PTR_ERR(dd->icc_path) : -ENOENT;
		dev_err(dd->dev, "%s(): failed to get ICC path: %d\n", __func__, ret);
		return ret;
	}
	return 0;
}

static int msm_spi_probe(struct platform_device *pdev)
{
	struct spi_master      *master;
	struct msm_spi	       *dd;
	struct resource	       *resource;
	int			i = 0;
	int                     rc = -ENXIO;
	struct msm_spi_platform_data *pdata;

	master = spi_alloc_master(&pdev->dev, sizeof(struct msm_spi));
	if (!master) {
		rc = -ENOMEM;
		dev_err(&pdev->dev, "master allocation failed\n");
		goto err_probe_exit;
	}

	master->bus_num        = pdev->id;
	master->mode_bits      = SPI_SUPPORTED_MODES;
	master->num_chipselect = SPI_NUM_CHIPSELECTS;
	master->set_cs	       = msm_spi_set_cs;
	master->setup          = msm_spi_setup;
	master->prepare_transfer_hardware = msm_spi_prepare_transfer_hardware;
	master->transfer_one = msm_spi_transfer_one;
	master->unprepare_transfer_hardware
			= msm_spi_unprepare_transfer_hardware;

	platform_set_drvdata(pdev, master);
	dd = spi_master_get_devdata(master);

	if (pdev->dev.of_node) {
		dd->qup_ver = SPI_QUP_VERSION_BFAM;
		master->dev.of_node = pdev->dev.of_node;
		pdata = msm_spi_dt_to_pdata(pdev, dd);
		if (!pdata) {
			dev_err(&pdev->dev, "platform data allocation failed\n");
			rc = -ENOMEM;
			goto err_probe_exit;
		}

		rc = of_alias_get_id(pdev->dev.of_node, "spi");
		if (rc < 0)
			dev_warn(&pdev->dev,
				"using default bus_num %d\n", pdev->id);
		else
			master->bus_num = pdev->id = rc;
	} else {
		pdata = pdev->dev.platform_data;
		dd->qup_ver = SPI_QUP_VERSION_NONE;

		for (i = 0; i < ARRAY_SIZE(spi_rsrcs); ++i) {
			resource = platform_get_resource(pdev, IORESOURCE_IO,
							i);
			dd->spi_gpios[i] = resource ? resource->start : -1;
		}

		for (i = 0; i < ARRAY_SIZE(spi_cs_rsrcs); ++i) {
			resource = platform_get_resource(pdev, IORESOURCE_IO,
						i + ARRAY_SIZE(spi_rsrcs));
			dd->cs_gpios[i].gpio_num = resource ?
							resource->start : -1;
		}
	}

	for (i = 0; i < ARRAY_SIZE(spi_cs_rsrcs); ++i)
		dd->cs_gpios[i].valid = false;

	dd->pdata = pdata;
	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!resource) {
		rc = -ENXIO;
		goto err_probe_res;
	}

	dd->mem_phys_addr = resource->start;
	dd->mem_size = resource_size(resource);
	dd->dev = &pdev->dev;

	if (pdata) {
		master->rt = pdata->rt_priority;
		if (pdata->dma_config) {
			rc = pdata->dma_config();
			if (rc) {
				dev_warn(&pdev->dev,
					"%s: DM mode not supported\n",
					__func__);
				dd->use_dma = false;
				goto skip_dma_resources;
			}
		}
		if (!dd->pdata->use_bam)
			goto skip_dma_resources;

		rc = msm_spi_bam_get_resources(dd, pdev, master);
		if (rc) {
			dev_warn(dd->dev,
					"%s: Failed to get BAM resources\n",
					__func__);
			goto skip_dma_resources;
		}
		dd->use_dma = true;
	}

	spi_dma_mask(&pdev->dev);
skip_dma_resources:

	spin_lock_init(&dd->queue_lock);
	mutex_init(&dd->core_lock);
	init_waitqueue_head(&dd->continue_suspend);

	if (!devm_request_mem_region(&pdev->dev, dd->mem_phys_addr,
					dd->mem_size, SPI_DRV_NAME)) {
		rc = -ENXIO;
		goto err_probe_reqmem;
	}

	dd->base = devm_ioremap(&pdev->dev, dd->mem_phys_addr, dd->mem_size);
	if (!dd->base) {
		rc = -ENOMEM;
		goto err_probe_reqmem;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, MSEC_PER_SEC);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	dd->suspended = true;
	rc = spi_register_master(master);
	if (rc)
		goto err_probe_reg_master;

	rc = sysfs_create_group(&(dd->dev->kobj), &dev_attr_grp);
	if (rc) {
		dev_err(&pdev->dev, "failed to create dev. attrs : %d\n", rc);
		goto err_attrs;
	}
	spi_debugfs_init(dd);

	return 0;

err_attrs:
	spi_unregister_master(master);
err_probe_reg_master:
	pm_runtime_disable(&pdev->dev);
err_probe_reqmem:
err_probe_res:
	spi_master_put(master);
err_probe_exit:
	return rc;
}

static int msm_spi_pm_suspend_runtime(struct device *device)
{
	struct platform_device *pdev = to_platform_device(device);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct msm_spi	  *dd;
	unsigned long	   flags;

	dev_dbg(device, "pm_runtime: suspending...\n");
	if (!master)
		goto suspend_exit;
	dd = spi_master_get_devdata(master);
	if (!dd)
		goto suspend_exit;

	if (dd->suspended)
		return 0;

	/*
	 * Make sure nothing is added to the queue while we're
	 * suspending
	 */
	spin_lock_irqsave(&dd->queue_lock, flags);
	dd->suspended = true;
	spin_unlock_irqrestore(&dd->queue_lock, flags);

	/* Wait for transactions to end, or time out */
	wait_event_interruptible(dd->continue_suspend,
		!dd->transfer_pending);

	mutex_lock(&dd->core_lock);
	if (dd->pdata && !dd->pdata->is_shared && dd->use_dma) {
		msm_spi_bam_pipe_disconnect(dd, &dd->bam.prod);
		msm_spi_bam_pipe_disconnect(dd, &dd->bam.cons);
	}
	if (dd->pdata && !dd->pdata->is_shared)
		put_local_resources(dd);

	if (dd->pdata)
		msm_spi_clk_path_vote(dd, 0);
	mutex_unlock(&dd->core_lock);

suspend_exit:
	return 0;
}

static int msm_spi_pm_resume_runtime(struct device *device)
{
	struct platform_device *pdev = to_platform_device(device);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct msm_spi	  *dd;
	int               ret = 0;

	dev_dbg(device, "pm_runtime: resuming...\n");
	if (!master)
		goto resume_exit;
	dd = spi_master_get_devdata(master);
	if (!dd)
		goto resume_exit;

	if (!dd->suspended)
		return 0;
	if (!dd->is_init_complete) {
		ret = init_resources(pdev);
		if (ret != 0)
			return ret;

		dd->is_init_complete = true;
	}
	msm_spi_get_icc_path(dd, pdev);
	msm_spi_clk_path_vote(dd, dd->pdata->max_clock_speed);

	if (!dd->pdata->is_shared) {
		ret = get_local_resources(dd);
		if (ret)
			return ret;
	}
	if (!dd->pdata->is_shared && dd->use_dma) {
		msm_spi_bam_pipe_connect(dd, &dd->bam.prod,
				&dd->bam.prod.config);
		msm_spi_bam_pipe_connect(dd, &dd->bam.cons,
				&dd->bam.cons.config);
	}
	dd->suspended = false;

resume_exit:
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int msm_spi_suspend(struct device *device)
{
	if (!pm_runtime_enabled(device) || !pm_runtime_suspended(device)) {
		struct platform_device *pdev = to_platform_device(device);
		struct spi_master *master = platform_get_drvdata(pdev);
		struct msm_spi   *dd;

		dev_dbg(device, "system suspend\n");
		if (!master)
			goto suspend_exit;
		dd = spi_master_get_devdata(master);
		if (!dd)
			goto suspend_exit;
		msm_spi_pm_suspend_runtime(device);

		/*
		 * set the device's runtime PM status to 'suspended'
		 */
		pm_runtime_disable(device);
		pm_runtime_set_suspended(device);
		pm_runtime_enable(device);
	}
suspend_exit:
	return 0;
}

static int msm_spi_resume(struct device *device)
{
	/*
	 * Rely on runtime-PM to call resume in case it is enabled
	 * Even if it's not enabled, rely on 1st client transaction to do
	 * clock ON and gpio configuration
	 */
	dev_dbg(device, "system resume\n");
	return 0;
}
#else
#define msm_spi_suspend NULL
#define msm_spi_resume NULL
#endif


static int msm_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct msm_spi    *dd = spi_master_get_devdata(master);

	spi_debugfs_exit(dd);
	sysfs_remove_group(&pdev->dev.kobj, &dev_attr_grp);

	if (dd->dma_teardown)
		dd->dma_teardown(dd);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	clk_put(dd->clk);
	clk_put(dd->pclk);
	msm_spi_clk_path_teardown(dd);
	platform_set_drvdata(pdev, NULL);
	spi_unregister_master(master);
	spi_master_put(master);

	return 0;
}

static const struct of_device_id msm_spi_dt_match[] = {
	{
		.compatible = "qcom,spi-qup-v2",
	},
	{}
};

static const struct dev_pm_ops msm_spi_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(msm_spi_suspend, msm_spi_resume)
	SET_RUNTIME_PM_OPS(msm_spi_pm_suspend_runtime,
			msm_spi_pm_resume_runtime, NULL)
};

static struct platform_driver msm_spi_driver = {
	.driver		= {
		.name	= SPI_DRV_NAME,
		.pm		= &msm_spi_dev_pm_ops,
		.of_match_table = msm_spi_dt_match,
	},
	.probe		= msm_spi_probe,
	.remove		= msm_spi_remove,
};

module_platform_driver(msm_spi_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:"SPI_DRV_NAME);
