/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
*
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 * Younghwan Joo <yhwan.joo@samsung.com>
 *
 * All rights reserved.
 */
#define DEBUG
#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/bug.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <plat/gpio-cfg.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "fimc-mdevice.h"
#include "fimc-is.h"
#include "fimc-is-regs.h"
#include "fimc-is-cmd.h"
#include "fimc-is-errno.h"
#include "fimc-is-param.h"
#include "fimc-is-config.h"

#include <asm/cacheflush.h>

#include <plat/clock.h>

/* TODO: revisit */
#define FIMC_IS_FW_ADDR_MASK	((1 << 26) - 1)
#define FIMC_IS_FW_SIZE_MAX	(SZ_4M)
#define FIMC_IS_FW_SIZE_MIN	(SZ_32K)

static char *fimc_is_clocks[FIMC_IS_MAX_CLOCKS] = {
	"mout_mpll_user",
	"ppmuisp",
	"sclk_uart_isp",
};

static void fimc_is_clk_put(struct fimc_is *fimc)
{
	int i;
	for (i = 0; i < FIMC_IS_MAX_CLOCKS; i++) {
		if (IS_ERR_OR_NULL(fimc->clocks[i]))
			continue;
		clk_unprepare(fimc->clocks[i]);
		clk_put(fimc->clocks[i]);
		fimc->clocks[i] = NULL;
	}
}

static int fimc_is_clk_get(struct fimc_is *is)
{
	int i, ret;

	for (i = 0; i < FIMC_IS_MAX_CLOCKS; i++) {
		is->clocks[i] = clk_get(&is->pdev->dev, fimc_is_clocks[i]);
		if (IS_ERR(is->clocks[i]))
			goto err;
		ret = clk_prepare(is->clocks[i]);
		if (ret < 0) {
			clk_put(is->clocks[i]);
			is->clocks[i] = NULL;
			goto err;
		}
	}

	/* FIXME: ISP UART parent clocks setup */
	clk_set_parent(is->clocks[2], is->clocks[0]);
	clk_set_rate(is->clocks[2], 50 * 1000000);

	return 0;
err:
	fimc_is_clk_put(is);
	dev_err(&is->pdev->dev, "failed to get clock: %s\n",
		fimc_is_clocks[i]);
	return -ENXIO;
}

int fimc_is_clk_enable(struct fimc_is *is)
{
	int i, ret;

	for (i = 0; i < FIMC_IS_MAX_CLOCKS; i++) {
		if (IS_ERR(is->clocks[i]))
			continue;
		pr_info("enabling clock %s", is->clocks[i]->name);
		ret = clk_enable(is->clocks[i]);
		if (ret < 0) {
			for (--i; i >= 0; i--)
				clk_disable(is->clocks[i]);
			pr_err("clk_enable failed!");
			return -EINVAL;
		}
	}
	return 0;
}

void fimc_is_clk_disable(struct fimc_is *is)
{
	int i;

	for (i = 0; i < FIMC_IS_MAX_CLOCKS; i++) {
		if (!IS_ERR(is->clocks[i])) {
			pr_info("disabling clock %s", is->clocks[i]->name);
			clk_disable(is->clocks[i]);
		}
	}
}

static int fimc_is_create_subdevs(struct fimc_is *is,
				  struct fimc_is_platform_data *pdata)
{
	int i, ret;
	pr_info("\n");

	ret = fimc_isp_subdev_create(&is->isp);
	if (ret < 0)
		return ret;

	for (i = 0; i < pdata->num_sensors; i++) {
		is->sensor[i].is = is;
		ret = fimc_is_sensor_subdev_create(&is->sensor[i],
							pdata->sensors[i]);
	}

	return ret;
}

static int fimc_is_unregister_subdevs(struct fimc_is *is)
{
	int i;
	pr_info("\n");

	fimc_isp_subdev_destroy(&is->isp);
	for (i = 0; i < is->pdata->num_sensors; i++)
		fimc_is_sensor_subdev_destroy(&is->sensor[i]);

	return 0;
}

#ifdef VIDEOBUF2_DMA_CONTIG
void fimc_is_mem_cache_clean(const void *start_addr, unsigned long size)
{
	unsigned long paddr;

	dmac_map_area(start_addr, size, DMA_TO_DEVICE);
	/*
	 * virtual & physical addresses mapped directly, so we can convert
	 * the address just using offset
	 */
	paddr = __pa((unsigned long)start_addr);
	outer_clean_range(paddr, paddr + size);
}

void fimc_is_mem_cache_inv(const void *start_addr, unsigned long size)
{
	unsigned long paddr = __pa((unsigned long)start_addr);

	outer_inv_range(paddr, paddr + size);
	dmac_unmap_area(start_addr, size, DMA_FROM_DEVICE);
}
#else
void fimc_is_cache_flush(struct fimc_is *is, u32 offset, u32 size)
{
	vb2_ion_sync_for_device(is->memory.fw_cookie,
		offset,
		size,
		DMA_BIDIRECTIONAL);
}

void fimc_is_region_invalid(struct fimc_is *is)
{
	vb2_ion_sync_for_device(
		is->memory.fw_cookie,
		(is->memory.kvaddr_region - is->memory.kvaddr),
		sizeof(struct is_region),
		DMA_FROM_DEVICE);
}

void fimc_is_region_flush(struct fimc_is *is)
{
	vb2_ion_sync_for_device(
		is->memory.fw_cookie,
		(is->memory.kvaddr_region - is->memory.kvaddr),
		sizeof(struct is_region),
		DMA_TO_DEVICE);
}

void fimc_is_shared_region_invalid(struct fimc_is *is)
{
	vb2_ion_sync_for_device(
		is->memory.fw_cookie,
		(is->memory.kvaddr_fshared - is->memory.kvaddr),
		sizeof(struct is_share_region),
		DMA_FROM_DEVICE);
}

void fimc_is_shared_region_flush(struct fimc_is *is)
{
	vb2_ion_sync_for_device(
		is->memory.fw_cookie,
		(is->memory.kvaddr_fshared - is->memory.kvaddr),
		sizeof(struct is_share_region),
		DMA_TO_DEVICE);
}
#endif

int fimc_is_load_setfile(struct fimc_is *is)
{
	const struct firmware *fw;
#ifdef VIDEOBUF2_DMA_CONTIG
	u8 *buf;
#endif
	int ret;

	ret = request_firmware(&fw, FIMC_IS_SETFILE_6A3, &is->pdev->dev);
	if (ret < 0) {
		dev_err(&is->pdev->dev, "firmware request failed (%d)\n", ret);
		return -EINVAL;
	}
#ifdef VIDEOBUF2_DMA_CONTIG
	buf = is->memory.vaddr + is->setfile.base;

	memcpy(buf, fw->data, fw->size);
	fimc_is_mem_cache_clean(buf, fw->size + 1);
#else
	memcpy((void *)(is->memory.kvaddr + is->setfile.base),
						fw->data, fw->size);
	fimc_is_cache_flush(is, is->setfile.base, fw->size + 1);
#endif
	is->setfile.size = fw->size;

	memcpy((void *)is->fw.setfile_info,
		(fw->data + fw->size - FIMC_IS_SETFILE_INFO_LEN),
		(FIMC_IS_SETFILE_INFO_LEN - 1));

	is->fw.setfile_info[FIMC_IS_SETFILE_INFO_LEN - 1] = '\0';
	is->setfile.state = 1;
	release_firmware(fw);
	pr_debug("Setfile base: %#x\n", is->setfile.base);
	return ret;
}

void fimc_is_cpu_set_power(struct fimc_is *is, int on)
{
	u32 timeout;

	pr_info("\n");
	if (on) {
		/* watchdog disable */
		pr_info("disabling watchdog\n");
		writel(0x0, is->regs + WDT_ISP);

		/* 1. Cortex-A5 start address setting */
		pr_info("setting BBOAR\n");
		pr_info("memory.kvaddr = %#x\n", is->memory.kvaddr);
		pr_info("memory.dvaddr = %#x\n", is->memory.dvaddr);
#ifdef VIDEOBUF2_DMA_CONTIG
		writel(is->memory.paddr, is->regs + BBOAR);
#else
		writel(is->memory.dvaddr, is->regs + BBOAR);
#endif

		/* 3. enable Cortex-A5 */
		pr_info("setting PMUREG_ISP_ARM_OPTION\n");
		writel(0x00018000, PMUREG_ISP_ARM_OPTION);
		/* 4. turn-on Cortex-A5 */
		pr_info("setting PMUREG_ISP_ARM_CONFIGURATION\n");
		writel(0x1, PMUREG_ISP_ARM_CONFIGURATION);
		return;
	}

	/* 2. A5 power off */
	writel(0x10000, PMUREG_ISP_ARM_OPTION);
	writel(0x0, PMUREG_ISP_ARM_CONFIGURATION);
	/* 3. Check A5 power off status register */
	pr_info("checking A5 power off status\n");
	timeout = 1000;
	while (__raw_readl(PMUREG_ISP_ARM_STATUS) & 1) {
		if (timeout == 0)
			pr_err("Low power off\n");
		pr_info("Wait A5 power off\n");
		timeout--;
		udelay(1);
	}
	pr_info("PMUREG_ISP_ARM_STATUS: %#x\n",
		__raw_readl(PMUREG_ISP_ARM_STATUS));
}

int fimc_is_hw_io_init(struct fimc_is *is)
{
	struct platform_device *pdev = to_platform_device(&is->pdev->dev);
	struct device *dev = &is->pdev->dev;
	int ret = 0;

	if (is->pdata->cfg_gpio) {
		is->pdata->cfg_gpio(pdev);
	} else {
		dev_err(dev, "#### failed to Config GPIO ####\n");
		ret = -EINVAL;
	}
	return ret;
}

/* Allocate working memory for the FIMC-IS CPU */
#ifdef VIDEOBUF2_DMA_CONTIG
static int fimc_is_alloc_cpu_memory(struct fimc_is *is)
{
	struct device *dev = &is->pdev->dev;

	is->memory.vaddr = dma_alloc_coherent(dev, FIMC_IS_CPU_MEM_SIZE,
					      &is->memory.paddr, GFP_KERNEL);
	if (is->memory.vaddr == NULL)
		return -ENOMEM;

	is->memory.size = FIMC_IS_CPU_MEM_SIZE;
	memset(is->memory.vaddr, 0, is->memory.size);

	dev_info(dev, "FIMC-IS CPU memory base: %#x\n", (u32)is->memory.paddr);

	if (((u32)is->memory.paddr) & FIMC_IS_FW_ADDR_MASK) {
		dev_err(dev, "invalid firmware memory alignment: %#x\n",
			(u32)is->memory.paddr);
		dma_free_coherent(dev, is->memory.size, is->memory.vaddr,
				  is->memory.paddr);
		return -EIO;
	}

	is->is_p_region = (struct is_region *)(phys_to_virt(is->memory.paddr +
				FIMC_IS_CPU_MEM_SIZE - FIMC_IS_REGION_SIZE));
	is->is_shared_region = (struct is_share_region *)(phys_to_virt(
		       is->memory.paddr + FIMC_IS_SHARED_REGION_OFFSET));

	return 0;
}
#else
static int fimc_is_alloc_cpu_memory(struct fimc_is *is)
{
	int ret = 0;
	void *fw_cookie;
	struct device *dev = &is->pdev->dev;

	fw_cookie = vb2_ion_private_alloc(is->alloc_ctx, FIMC_IS_CPU_MEM_SIZE);
	if (IS_ERR(fw_cookie)) {
		dev_err(dev, "Allocating bitprocessor buffer failed");
		fw_cookie = NULL;
		ret = -ENOMEM;
		goto exit;
	}

	ret = vb2_ion_dma_address(fw_cookie, &is->memory.dvaddr);
	if ((ret < 0) || (is->memory.dvaddr  & FIMC_IS_CPU_BASE_MASK)) {
		dev_err(dev, "The base memory is not aligned to 64MB.");
		vb2_ion_private_free(fw_cookie);
		is->memory.dvaddr = 0;
		fw_cookie = NULL;
		ret = -EIO;
		goto exit;
	}
	dev_info(dev, "Device vaddr = %08x , size = %08x\n",
		is->memory.dvaddr, FIMC_IS_CPU_MEM_SIZE);

	is->memory.kvaddr = (u32)vb2_ion_private_vaddr(fw_cookie);
	if (IS_ERR((void *)is->memory.kvaddr)) {
		dev_err(dev, "Bitprocessor memory remap failed");
		vb2_ion_private_free(fw_cookie);
		is->memory.kvaddr = 0;
		fw_cookie = NULL;
		ret = -EIO;
		goto exit;
	}
exit:
	dev_info(dev, "Virtual address for FW: %08lx\n",
		(long unsigned int)is->memory.kvaddr);
	is->memory.fw_cookie = fw_cookie;
	return ret;
}

static int fimc_is_init_cpu_memory(struct fimc_is *is)
{
	int ret = 0;
	u32 offset;

	offset = FIMC_IS_SHARED_REGION_OFFSET;
	is->memory.dvaddr_fshared = is->memory.dvaddr + offset;
	is->memory.kvaddr_fshared = is->memory.kvaddr + offset;

	offset = FIMC_IS_CPU_MEM_SIZE - FIMC_IS_REGION_SIZE;
	is->memory.dvaddr_region = is->memory.dvaddr + offset;
	is->memory.kvaddr_region = is->memory.kvaddr + offset;

	is->is_p_region = (struct is_region *)is->memory.kvaddr_region;
	is->is_shared_region = (struct is_share_region *)
						is->memory.kvaddr_fshared;
	return ret;
}
#endif

int fimc_is_request_firmware(struct fimc_is *is, const char *fw_name)
{
	const struct firmware *fw;
	struct device *dev = &is->pdev->dev;
	void *buf;
	int ret;

	ret = request_firmware(&fw, fw_name, &is->pdev->dev);
	if (ret < 0) {
		dev_err(dev, "firmware request failed (%d)\n", ret);
		return -EINVAL;
	}

	mutex_lock(&is->lock);

	/* 1. Load IS firmware */
	if (fw->size < FIMC_IS_FW_SIZE_MIN || fw->size > FIMC_IS_FW_SIZE_MAX) {
		dev_err(dev, "wrong firmware size: %d\n", fw->size);
		goto done;
	}

	is->fw.size = fw->size;
#ifdef VIDEOBUF2_DMA_CONTIG
	ret = fimc_is_alloc_cpu_memory(is);
	if (ret < 0) {
		dev_err(dev, "failed to allocate FIMC-IS CPU memory\n");
		goto done;
	}

	memcpy(is->memory.vaddr, fw->data, fw->size);
	wmb();

	/* Read firmware description */
	buf = (void *)(is->memory.vaddr + fw->size - FIMC_IS_FW_DESC_LEN);
	memcpy(&is->fw.info, buf, FIMC_IS_FW_INFO_LEN);
	is->fw.info[FIMC_IS_FW_INFO_LEN] = 0;

	buf = (void *)(is->memory.vaddr + fw->size - FIMC_IS_FW_VER_LEN);
	memcpy(&is->fw.version, buf, FIMC_IS_FW_VER_LEN);
	is->fw.version[FIMC_IS_FW_VER_LEN - 1] = 0;

#else
	memcpy((void *)is->memory.kvaddr, fw->data, fw->size);
	fimc_is_cache_flush(is, 0, fw->size + 1);

	/* Read firmware description */
	buf = (void *)(is->memory.kvaddr + fw->size - FIMC_IS_FW_DESC_LEN);
	memcpy((void *)&is->fw.info, buf, FIMC_IS_FW_INFO_LEN);
	is->fw.info[FIMC_IS_FW_INFO_LEN] = 0;

	buf = (void *)(is->memory.kvaddr + fw->size - FIMC_IS_FW_VER_LEN);
	memcpy((void *)&is->fw.version, buf, FIMC_IS_FW_VER_LEN);
	is->fw.version[FIMC_IS_FW_VER_LEN - 1] = 0;
#endif

	is->fw.state = 1;

#ifdef VIDEOBUF2_DMA_CONTIG
	dev_info(dev, "loaded firmware: %s, rev. %s\n",
		 is->fw.info, is->fw.version);
	pr_info("FW size: %d, paddr: %#x\n", fw->size, is->memory.paddr);
#else
	pr_info("loaded firmware: %s, rev. %s\n", is->fw.info, is->fw.version);
	pr_info("FW size: %d, kvaddr: %#x\n", fw->size, is->memory.kvaddr);
#endif

	is->is_shared_region->chip_id = 0xe4412;
	is->is_shared_region->chip_rev_no = 1;

#ifdef VIDEOBUF2_DMA_CONTIG
	fimc_is_mem_cache_clean((void *)is->is_shared_region,
				sizeof(struct is_share_region));
#else
	fimc_is_shared_region_flush(is);
#endif

done:
	mutex_unlock(&is->lock);
	release_firmware(fw);

	return ret;
}

/* Main FIMC-IS interrupt handler */
static irqreturn_t fimc_is_irq_handler(int irq, void *priv)
{
	struct fimc_is *is = priv;
	unsigned long flags;
	u32 status;

	static unsigned int counter;

	spin_lock_irqsave(&is->slock, flags);
	counter++;

	status = readl(is->regs + INTSR1);

	/* ISP interrupt */
	if (status & (1 << 1)) {
		fimc_isp_irq_handler(is);
		goto unlock;
	}

	if (!unlikely((status & (1 << 0)))) {
		spin_unlock_irqrestore(&is->slock, flags);
		return IRQ_NONE;
	}

	/* General IS interrupt */
	is->i2h_cmd.cmd = readl(is->regs + ISSR(10));

	switch (is->i2h_cmd.cmd) {
	case IHC_GET_SENSOR_NUMBER:
		pr_info("IHC_GET_SENSOR_NUMBER\n");
		fimc_is_hw_get_params(is, 1);
		pr_info("ISP FW version: %d\n", is->i2h_cmd.args[0]);

		/* FIXME: busy waiting in interrupt handler! */
		fimc_is_hw_wait_intmsr0_intmsd0(is);
		fimc_is_hw_set_sensor_num(is);
		break;
	case IHC_SET_FACE_MARK:
	case IHC_FRAME_DONE:
		fimc_is_hw_get_params(is, 2);
		break;
	case IHC_SET_SHOT_MARK:
	case IHC_AA_DONE:
	case ISR_DONE:
		fimc_is_hw_get_params(is, 3);
		break;
	case ISR_NDONE:
		fimc_is_hw_get_params(is, 4);
		break;
	case IHC_NOT_READY:
		break;
	default:
		pr_info("Unknown command: %#x\n", is->i2h_cmd.cmd);
	}

	fimc_is_fw_clear_irq1(is, 0);

	switch (is->i2h_cmd.cmd) {
	case IHC_GET_SENSOR_NUMBER:
		fimc_is_hw_set_intgr0_gd0(is);
		set_bit(IS_ST_A5_PWR_ON, &is->state);
		break;
	case IHC_SET_SHOT_MARK:
		break;
	case IHC_SET_FACE_MARK:
		is->fd_header.count = is->i2h_cmd.args[0];
		is->fd_header.index = is->i2h_cmd.args[1];
		is->fd_header.offset = 0;
		break;

	case IHC_FRAME_DONE:
		break;

	case IHC_AA_DONE:
		pr_debug("AA_DONE - %d, %d, %d\n", is->i2h_cmd.args[0],
			 is->i2h_cmd.args[1], is->i2h_cmd.args[2]);
		switch (is->i2h_cmd.args[0]) {
		/* SEARCH: Occurs when search is requested at continuous AF */
		case 2:
			break;
		/* INFOCUS: Occurs when focus is found. */
		case 3:
			if (is->af.af_state == FIMC_IS_AF_RUNNING)
				is->af.af_state = FIMC_IS_AF_LOCK;
			is->af.af_lock_state = FIMC_IS_AF_LOCKED;
			break;
		/* OUTOFFOCUS: Occurs when focus is not found. */
		case 4:
			if (is->af.af_state == FIMC_IS_AF_RUNNING)
				is->af.af_state = FIMC_IS_AF_FAILED;
			is->af.af_lock_state = FIMC_IS_AF_UNLOCKED;
			break;
		}
		break;

	case ISR_DONE:
		pr_info("ISR_DONE: args[0]: %#x\n", is->i2h_cmd.args[0]);

		switch (is->i2h_cmd.args[0]) {
		case HIC_PREVIEW_STILL:
		case HIC_PREVIEW_VIDEO:
		case HIC_CAPTURE_STILL:
		case HIC_CAPTURE_VIDEO:
			set_bit(IS_ST_CHANGE_MODE, &is->state);
			/* Get CAC margin */
			is->sensor[is->sensor_index].offset_x =
							is->i2h_cmd.args[1];
			is->sensor[is->sensor_index].offset_y =
							is->i2h_cmd.args[2];
			pr_info("sensor offset (x,y): (%d,%d)\n",
				is->sensor[is->sensor_index].offset_x,
				is->sensor[is->sensor_index].offset_y);
			break;

		case HIC_STREAM_ON:
			clear_bit(IS_ST_STREAM_OFF, &is->state);
			set_bit(IS_ST_STREAM_ON, &is->state);
			break;

		case HIC_STREAM_OFF:
			clear_bit(IS_ST_STREAM_ON, &is->state);
			set_bit(IS_ST_STREAM_OFF, &is->state);
			break;

		case HIC_SET_PARAMETER:
			pr_info("HIC_SET_PARAMETER\n");
			is->cfg_param[is->scenario_id].p_region_index1 = 0;
			is->cfg_param[is->scenario_id].p_region_index2 = 0;
			atomic_set(&is->cfg_param[is->scenario_id].p_region_num, 0);
			set_bit(IS_ST_BLOCK_CMD_CLEARED, &is->state);

			if (is->af.af_state == FIMC_IS_AF_SETCONFIG)
				is->af.af_state = FIMC_IS_AF_RUNNING;
			else if (is->af.af_state == FIMC_IS_AF_ABORT)
				is->af.af_state = FIMC_IS_AF_IDLE;
			break;

		case HIC_GET_PARAMETER:
			break;

		case HIC_SET_TUNE:
			break;

		case HIC_GET_STATUS:
			pr_info("HIC_GET_STATUS\n");
			break;

		case HIC_OPEN_SENSOR:
			set_bit(IS_ST_OPEN_SENSOR, &is->state);
			pr_info("FIMC-IS Lane= %d, Settle line= %d\n",
				is->i2h_cmd.args[2], is->i2h_cmd.args[1]);
			break;

		case HIC_CLOSE_SENSOR:
			clear_bit(IS_ST_OPEN_SENSOR, &is->state);
			break;

		case HIC_MSG_TEST:
			pr_debug("Config MSG level was done\n");
			break;

		case HIC_POWER_DOWN:
			pr_info("IS-Sensor: HIC_POWER_DOWN\n");
			clear_bit(IS_ST_PWR_SUBIP_ON, &is->state);
			break;

		case HIC_GET_SET_FILE_ADDR:
			is->setfile.base = is->i2h_cmd.args[1];
			set_bit(IS_ST_SETFILE_LOADED, &is->state);
			break;

		case HIC_LOAD_SET_FILE:
			set_bit(IS_ST_SETFILE_LOADED, &is->state);
			break;
		}
		break;

	case ISR_NDONE:
		pr_err("ISR_NDONE: %d: %#x\n", is->i2h_cmd.args[0],
			is->i2h_cmd.args[1]);

		pr_err("%s\n", fimc_is_strerr(is->i2h_cmd.args[1]));

		if (is->i2h_cmd.args[1] & IS_ERROR_TIME_OUT_FLAG)
			pr_err("IS_ERROR_TIME_OUT\n");

		switch (is->i2h_cmd.args[1]) {
		case IS_ERROR_SET_PARAMETER:
#ifdef VIDEOBUF2_DMA_CONTIG
			fimc_is_mem_cache_inv((void *)is->is_p_region,
					      IS_PARAM_SIZE);
#else
			fimc_is_region_invalid(is);
#endif
			fimc_is_param_err_checker(is->is_p_region);
			break;
		}

		switch (is->i2h_cmd.args[0]) {
		case HIC_SET_PARAMETER:
			is->cfg_param[is->scenario_id].p_region_index1 = 0;
			is->cfg_param[is->scenario_id].p_region_index2 = 0;
			atomic_set(&is->cfg_param[is->scenario_id].p_region_num, 0);
			set_bit(IS_ST_BLOCK_CMD_CLEARED, &is->state);
			break;
		}
		break;

	case IHC_NOT_READY:
		pr_err("IS control sequence error: Not Ready\n");
		break;
	}

	wake_up(&is->irq_queue);
unlock:
	spin_unlock_irqrestore(&is->slock, flags);
	return IRQ_HANDLED;
}

int fimc_is_hw_open_sensor(struct fimc_is *is, u32 id, u32 sensor_index)
{
	struct sensor_open_extended *soe = NULL;
	u32 region_shared_addr;

#ifdef VIDEOBUF2_DMA_CONTIG
	region_shared_addr = virt_to_phys((void *)&is->is_p_region->shared);
#else
	region_shared_addr = (u32)&is->is_p_region->shared - is->memory.kvaddr;
	region_shared_addr = region_shared_addr + is->memory.dvaddr;
#endif
	fimc_is_hw_wait_intmsr0_intmsd0(is);
	writel(HIC_OPEN_SENSOR, is->regs + ISSR(0));
	writel(id, is->regs + ISSR(1));

	soe = (struct sensor_open_extended *)&is->is_p_region->shared;
	if (samsung_rev() >= EXYNOS4412_REV_2_0)
		soe->i2c_sclk = 88000000;
	else
		soe->i2c_sclk = 80000000;

	switch (sensor_index) {
	case SENSOR_ID(ID_S5K3H2, ID_CSI_A):
		soe->actuator_type = 1;
		soe->mclk = 0;
		soe->mipi_lane_num = 0;
		soe->mipi_speed = 0;
		soe->fast_open_sensor = 0;
		soe->self_calibration_mode = 0;

		is->af.use_af = 1;
		writel(SENSOR_NAME_S5K3H2, is->regs + ISSR(2));
		writel(SENSOR_CONTROL_I2C0, is->regs + ISSR(3));
		writel(region_shared_addr, is->regs + ISSR(4));
		break;
	case SENSOR_ID(ID_S5K3H2, ID_CSI_B):
		soe->actuator_type = 1;
		soe->mclk = 0;
		soe->mipi_lane_num = 0;
		soe->mipi_speed = 0;
		soe->fast_open_sensor = 0;
		soe->self_calibration_mode = 0;

		is->af.use_af = 1;
		writel(SENSOR_NAME_S5K3H2, is->regs + ISSR(2));
		writel(SENSOR_CONTROL_I2C1, is->regs + ISSR(3));
		writel(region_shared_addr, is->regs + ISSR(4));
		break;
	case SENSOR_ID(ID_S5K3H7, ID_CSI_A):
		soe->actuator_type = 3;
		soe->mclk = 0;
		soe->mipi_lane_num = 0;
		soe->mipi_speed = 0;
		soe->fast_open_sensor = 0;
		soe->self_calibration_mode = 0;

		is->af.use_af = 1;
		writel(SENSOR_NAME_S5K3H7, is->regs + ISSR(2));
		writel(SENSOR_CONTROL_I2C0, is->regs + ISSR(3));
		writel(region_shared_addr, is->regs + ISSR(4));
		break;
	case SENSOR_ID(ID_S5K3H7, ID_CSI_B):
		soe->actuator_type = 3;
		soe->mclk = 0;
		soe->mipi_lane_num = 0;
		soe->mipi_speed = 0;
		soe->fast_open_sensor = 0;
		soe->self_calibration_mode = 0;

		is->af.use_af = 1;
		writel(SENSOR_NAME_S5K3H7, is->regs + ISSR(2));
		writel(SENSOR_CONTROL_I2C1, is->regs + ISSR(3));
		writel(region_shared_addr, is->regs + ISSR(4));
		break;
	case SENSOR_ID(ID_S5K4E5, ID_CSI_A):
		soe->actuator_type = 1;
		soe->mclk = 0;
		soe->mipi_lane_num = 0;
		soe->mipi_speed = 0;
		soe->fast_open_sensor = 0;
		soe->self_calibration_mode = 0;

		is->af.use_af = 1;
		writel(SENSOR_NAME_S5K4E5, is->regs + ISSR(2));
		writel(SENSOR_CONTROL_I2C0, is->regs + ISSR(3));
		writel(region_shared_addr, is->regs + ISSR(4));
		break;
	case SENSOR_ID(ID_S5K4E5, ID_CSI_B):
		soe->actuator_type = 1;
		soe->mclk = 0;
		soe->mipi_lane_num = 0;
		soe->mipi_speed = 0;
		soe->fast_open_sensor = 0;
		soe->self_calibration_mode = 0;

		is->af.use_af = 1;
		writel(SENSOR_NAME_S5K4E5, is->regs + ISSR(2));
		writel(SENSOR_CONTROL_I2C1, is->regs + ISSR(3));
		writel(region_shared_addr, is->regs + ISSR(4));
		break;
	case SENSOR_ID(ID_S5K6A3, ID_CSI_A):
		soe->actuator_type = 0;
		soe->mclk = 0;
		soe->mipi_lane_num = 0;
		soe->mipi_speed = 0;
		soe->fast_open_sensor = 0;
		soe->self_calibration_mode = 1;

		is->af.use_af = 0;
		writel(SENSOR_NAME_S5K6A3, is->regs + ISSR(2));
		writel(SENSOR_CONTROL_I2C0, is->regs + ISSR(3));
		writel(region_shared_addr, is->regs + ISSR(4));
		break;
	case SENSOR_ID(ID_S5K6A3, ID_CSI_B):
		soe->actuator_type = 0;
		soe->mclk = 0;
		soe->mipi_lane_num = 0;
		soe->mipi_speed = 0;
		soe->fast_open_sensor = 0;
		soe->self_calibration_mode = 1;

		is->af.use_af = 0;
		writel(SENSOR_NAME_S5K6A3, is->regs + ISSR(2));
		writel(SENSOR_CONTROL_I2C1, is->regs + ISSR(3));
		writel(region_shared_addr, is->regs + ISSR(4));
		break;
	case 200:
		soe->actuator_type = 0;
		soe->mclk = 0;
		soe->mipi_lane_num = 0;
		soe->mipi_speed = 0;
		soe->fast_open_sensor = 6;
		soe->self_calibration_mode = 1;

		is->af.use_af = 0;
		writel(SENSOR_NAME_S5K6A3, is->regs + ISSR(2));
		writel(SENSOR_CONTROL_I2C1, is->regs + ISSR(3));
		writel(region_shared_addr, is->regs + ISSR(4));
		break;
	default:
		dev_err(&is->pdev->dev, "Wrong sensor index: %d\n",
			sensor_index);
		return -EINVAL;
	}

#ifdef VIDEOBUF2_DMA_CONTIG
	fimc_is_mem_cache_clean(is->is_p_region, IS_PARAM_SIZE);
#else
	fimc_is_region_flush(is);
#endif
	fimc_is_hw_set_intgr0_gd0(is);

	return 0;
}

static int __devinit fimc_is_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimc_is_platform_data *pdata;
	struct fimc_is *is;
	struct resource *res;
	int irq, ret;

	is = devm_kzalloc(&pdev->dev, sizeof(*is), GFP_KERNEL);
	if (!is) {
		dev_err(&pdev->dev, "Not enough memory for FIMC-IS device.\n");
		return -ENOMEM;
	}
	is->pdev = pdev;
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "Platform data not set\n");
		return -EINVAL;
	}
	is->pdata = pdata;

	init_waitqueue_head(&is->irq_queue);
	spin_lock_init(&is->slock);
	mutex_init(&is->lock);

	/* I/O remap */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	is->regs = devm_request_and_ioremap(dev, res);
	if (is->regs == NULL) {
		dev_err(&pdev->dev, "Failed to obtain io memory\n");
		return -ENOENT;
	}

	/* initialize IRQ , FIMC-IS IRQ : ISP[0] */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		return irq;
	}

	ret = devm_request_irq(dev, irq, fimc_is_irq_handler,
			       0, dev_name(dev), is);
	if (ret) {
		dev_err(dev, "Failed to install irq (%d)\n", ret);
		return -ENOENT;
	}

	if (is->pdata->clk_get) {
		is->pdata->clk_get(pdev);
	} else {
		dev_err(dev, "#### failed to Get Clock####\n");
		goto err_clk;
	}

	if (is->pdata->cfg_gpio) {
		is->pdata->cfg_gpio(pdev);
	} else {
		dev_err(dev, "#### failed to Config GPIO####\n");
		goto err_clk;
	}

	platform_set_drvdata(pdev, is);

	pm_runtime_enable(dev);

	/* initialize memory manager */
#ifdef VIDEOBUF2_DMA_CONTIG
	is->alloc_ctx = vb2_dma_contig_init_ctx(dev, VB2_CREATE_VADDR);
	if (IS_ERR(is->alloc_ctx)) {
		ret = PTR_ERR(is->alloc_ctx);
		goto err_clk;
	}
#else
	/* TODO : make function */
	is->alloc_ctx = vb2_ion_create_context(&pdev->dev, SZ_4K,
					VB2ION_CTX_IOMMU | VB2ION_CTX_VMCONTIG);

	ret = fimc_is_alloc_cpu_memory(is);
	if (ret) {
		dev_err(dev, "fimc_is_ishcain_initmem is fail(%d)\n", ret);
		goto err_clk;
	}
	ret = fimc_is_init_cpu_memory(is);
#endif

	/*
	 * Video nodes will be created within the subdev's registered()
	 * callback
	 */
	ret = fimc_is_create_subdevs(is, dev->platform_data);
	if (ret)
		goto err_vb;

	/* FIXME: ISP power domain cannot be enabled again after disabled. */

	/* TODO: what is this, why is this needed ??? */
	is->state = 0;
	is->fw.state = 0;
	is->setfile.state = 0;

	pr_info("FIMC-IS registered successfully\n");
	pr_info("isp: %p, is: %p, is->regs: %p\n", &is->isp, is, is->regs);
	return 0;

err_vb:
	vb2_dma_contig_cleanup_ctx(is->alloc_ctx);
err_clk:
	free_irq(irq, dev);
	dev_err(dev, "failed to install\n");
	return ret;
}

static int fimc_is_runtime_resume(struct device *dev)
{
	struct fimc_is *is = dev_get_drvdata(dev);

	/* 1. Clock setting */
	if (is->pdata->clk_get) {
		is->pdata->clk_cfg(is->pdev);
	} else {
		dev_err(dev, "failed to Clock CONFIG\n");
		return -EINVAL;
	}

	if (is->pdata->clk_get) {
		is->pdata->clk_on(is->pdev);
	} else {
		dev_err(dev, "failed to Clock On\n");
		return -EINVAL;
	}

#if defined(CONFIG_VIDEOBUF2_ION)
	if (is->alloc_ctx)
		vb2_ion_attach_iommu(is->alloc_ctx);
#endif
	return 0;
}

static int fimc_is_runtime_suspend(struct device *dev)
{
	/* TODO: power down sequence */
	struct fimc_is *is = dev_get_drvdata(dev);

#if defined(CONFIG_VIDEOBUF2_ION)
	if (is->alloc_ctx)
		vb2_ion_detach_iommu(is->alloc_ctx);
#endif

	if (is->pdata->clk_get) {
		is->pdata->clk_off(is->pdev);
	} else {
		dev_err(dev, "failed to Clock Off\n");
		return -EINVAL;
	}
	return 0;
}

int fimc_is_resume(struct device *dev)
{
	fimc_is_runtime_resume(dev);
	return 0;
}

int fimc_is_suspend(struct device *dev)
{
	fimc_is_runtime_suspend(dev);
	return 0;
}

static int __devexit fimc_is_remove(struct platform_device *pdev)
{
	struct fimc_is *is = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	fimc_is_unregister_subdevs(is);
	vb2_dma_contig_cleanup_ctx(is->alloc_ctx);
	fimc_is_clk_put(is);

	return 0;
}

static struct platform_device_id fimc_is_driver_ids[] = {
	{
		.name		= "exynos4-fimc-is",
		.driver_data	= 0,
	},
};
MODULE_DEVICE_TABLE(platform, fimc_is_driver_ids);

static const struct dev_pm_ops fimc_is_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fimc_is_suspend, fimc_is_resume)
	SET_RUNTIME_PM_OPS(fimc_is_runtime_suspend, fimc_is_runtime_resume,
			   NULL)
};

static struct platform_driver fimc_is_driver = {
	.probe		= fimc_is_probe,
	.remove		= __devexit_p(fimc_is_remove),
	.id_table	= fimc_is_driver_ids,
	.driver = {
		.name	= FIMC_IS_DRV_NAME,
		.owner	= THIS_MODULE,
		.pm	= &fimc_is_pm_ops,
	}
};
module_platform_driver(fimc_is_driver);
MODULE_AUTHOR("Younghwan Joo, <yhwan.joo@samsung.com>");
MODULE_DESCRIPTION("Exynos4 series FIMC-IS slave driver");
MODULE_ALIAS("platform:" FIMC_IS_DRV_NAME);
