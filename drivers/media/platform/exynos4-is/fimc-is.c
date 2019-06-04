// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *
 * Authors: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *          Younghwan Joo <yhwan.joo@samsung.com>
 */
#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dma-contiguous.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <media/videobuf2-dma-contig.h>

#include "media-dev.h"
#include "fimc-is.h"
#include "fimc-is-command.h"
#include "fimc-is-errno.h"
#include "fimc-is-i2c.h"
#include "fimc-is-param.h"
#include "fimc-is-regs.h"


static char *fimc_is_clocks[ISS_CLKS_MAX] = {
	[ISS_CLK_PPMUISPX]		= "ppmuispx",
	[ISS_CLK_PPMUISPMX]		= "ppmuispmx",
	[ISS_CLK_LITE0]			= "lite0",
	[ISS_CLK_LITE1]			= "lite1",
	[ISS_CLK_MPLL]			= "mpll",
	[ISS_CLK_ISP]			= "isp",
	[ISS_CLK_DRC]			= "drc",
	[ISS_CLK_FD]			= "fd",
	[ISS_CLK_MCUISP]		= "mcuisp",
	[ISS_CLK_GICISP]		= "gicisp",
	[ISS_CLK_PWM_ISP]		= "pwm_isp",
	[ISS_CLK_MCUCTL_ISP]		= "mcuctl_isp",
	[ISS_CLK_UART]			= "uart",
	[ISS_CLK_ISP_DIV0]		= "ispdiv0",
	[ISS_CLK_ISP_DIV1]		= "ispdiv1",
	[ISS_CLK_MCUISP_DIV0]		= "mcuispdiv0",
	[ISS_CLK_MCUISP_DIV1]		= "mcuispdiv1",
	[ISS_CLK_ACLK200]		= "aclk200",
	[ISS_CLK_ACLK200_DIV]		= "div_aclk200",
	[ISS_CLK_ACLK400MCUISP]		= "aclk400mcuisp",
	[ISS_CLK_ACLK400MCUISP_DIV]	= "div_aclk400mcuisp",
};

static void fimc_is_put_clocks(struct fimc_is *is)
{
	int i;

	for (i = 0; i < ISS_CLKS_MAX; i++) {
		if (IS_ERR(is->clocks[i]))
			continue;
		clk_put(is->clocks[i]);
		is->clocks[i] = ERR_PTR(-EINVAL);
	}
}

static int fimc_is_get_clocks(struct fimc_is *is)
{
	int i, ret;

	for (i = 0; i < ISS_CLKS_MAX; i++)
		is->clocks[i] = ERR_PTR(-EINVAL);

	for (i = 0; i < ISS_CLKS_MAX; i++) {
		is->clocks[i] = clk_get(&is->pdev->dev, fimc_is_clocks[i]);
		if (IS_ERR(is->clocks[i])) {
			ret = PTR_ERR(is->clocks[i]);
			goto err;
		}
	}

	return 0;
err:
	fimc_is_put_clocks(is);
	dev_err(&is->pdev->dev, "failed to get clock: %s\n",
		fimc_is_clocks[i]);
	return ret;
}

static int fimc_is_setup_clocks(struct fimc_is *is)
{
	int ret;

	ret = clk_set_parent(is->clocks[ISS_CLK_ACLK200],
					is->clocks[ISS_CLK_ACLK200_DIV]);
	if (ret < 0)
		return ret;

	ret = clk_set_parent(is->clocks[ISS_CLK_ACLK400MCUISP],
					is->clocks[ISS_CLK_ACLK400MCUISP_DIV]);
	if (ret < 0)
		return ret;

	ret = clk_set_rate(is->clocks[ISS_CLK_ISP_DIV0], ACLK_AXI_FREQUENCY);
	if (ret < 0)
		return ret;

	ret = clk_set_rate(is->clocks[ISS_CLK_ISP_DIV1], ACLK_AXI_FREQUENCY);
	if (ret < 0)
		return ret;

	ret = clk_set_rate(is->clocks[ISS_CLK_MCUISP_DIV0],
					ATCLK_MCUISP_FREQUENCY);
	if (ret < 0)
		return ret;

	return clk_set_rate(is->clocks[ISS_CLK_MCUISP_DIV1],
					ATCLK_MCUISP_FREQUENCY);
}

static int fimc_is_enable_clocks(struct fimc_is *is)
{
	int i, ret;

	for (i = 0; i < ISS_GATE_CLKS_MAX; i++) {
		if (IS_ERR(is->clocks[i]))
			continue;
		ret = clk_prepare_enable(is->clocks[i]);
		if (ret < 0) {
			dev_err(&is->pdev->dev, "clock %s enable failed\n",
				fimc_is_clocks[i]);
			for (--i; i >= 0; i--)
				clk_disable(is->clocks[i]);
			return ret;
		}
		pr_debug("enabled clock: %s\n", fimc_is_clocks[i]);
	}
	return 0;
}

static void fimc_is_disable_clocks(struct fimc_is *is)
{
	int i;

	for (i = 0; i < ISS_GATE_CLKS_MAX; i++) {
		if (!IS_ERR(is->clocks[i])) {
			clk_disable_unprepare(is->clocks[i]);
			pr_debug("disabled clock: %s\n", fimc_is_clocks[i]);
		}
	}
}

static int fimc_is_parse_sensor_config(struct fimc_is *is, unsigned int index,
						struct device_node *node)
{
	struct fimc_is_sensor *sensor = &is->sensor[index];
	struct device_node *ep, *port;
	u32 tmp = 0;
	int ret;

	sensor->drvdata = fimc_is_sensor_get_drvdata(node);
	if (!sensor->drvdata) {
		dev_err(&is->pdev->dev, "no driver data found for: %pOF\n",
							 node);
		return -EINVAL;
	}

	ep = of_graph_get_next_endpoint(node, NULL);
	if (!ep)
		return -ENXIO;

	port = of_graph_get_remote_port(ep);
	of_node_put(ep);
	if (!port)
		return -ENXIO;

	/* Use MIPI-CSIS channel id to determine the ISP I2C bus index. */
	ret = of_property_read_u32(port, "reg", &tmp);
	if (ret < 0) {
		dev_err(&is->pdev->dev, "reg property not found at: %pOF\n",
							 port);
		of_node_put(port);
		return ret;
	}

	of_node_put(port);
	sensor->i2c_bus = tmp - FIMC_INPUT_MIPI_CSI2_0;
	return 0;
}

static int fimc_is_register_subdevs(struct fimc_is *is)
{
	struct device_node *i2c_bus, *child;
	int ret, index = 0;

	ret = fimc_isp_subdev_create(&is->isp);
	if (ret < 0)
		return ret;

	for_each_compatible_node(i2c_bus, NULL, FIMC_IS_I2C_COMPATIBLE) {
		for_each_available_child_of_node(i2c_bus, child) {
			ret = fimc_is_parse_sensor_config(is, index, child);

			if (ret < 0 || index >= FIMC_IS_SENSORS_NUM) {
				of_node_put(child);
				return ret;
			}
			index++;
		}
	}
	return 0;
}

static int fimc_is_unregister_subdevs(struct fimc_is *is)
{
	fimc_isp_subdev_destroy(&is->isp);
	return 0;
}

static int fimc_is_load_setfile(struct fimc_is *is, char *file_name)
{
	const struct firmware *fw;
	void *buf;
	int ret;

	ret = request_firmware(&fw, file_name, &is->pdev->dev);
	if (ret < 0) {
		dev_err(&is->pdev->dev, "firmware request failed (%d)\n", ret);
		return ret;
	}
	buf = is->memory.vaddr + is->setfile.base;
	memcpy(buf, fw->data, fw->size);
	fimc_is_mem_barrier();
	is->setfile.size = fw->size;

	pr_debug("mem vaddr: %p, setfile buf: %p\n", is->memory.vaddr, buf);

	memcpy(is->fw.setfile_info,
		fw->data + fw->size - FIMC_IS_SETFILE_INFO_LEN,
		FIMC_IS_SETFILE_INFO_LEN - 1);

	is->fw.setfile_info[FIMC_IS_SETFILE_INFO_LEN - 1] = '\0';
	is->setfile.state = 1;

	pr_debug("FIMC-IS setfile loaded: base: %#x, size: %zu B\n",
		 is->setfile.base, fw->size);

	release_firmware(fw);
	return ret;
}

int fimc_is_cpu_set_power(struct fimc_is *is, int on)
{
	unsigned int timeout = FIMC_IS_POWER_ON_TIMEOUT;

	if (on) {
		/* Disable watchdog */
		mcuctl_write(0, is, REG_WDT_ISP);

		/* Cortex-A5 start address setting */
		mcuctl_write(is->memory.paddr, is, MCUCTL_REG_BBOAR);

		/* Enable and start Cortex-A5 */
		pmuisp_write(0x18000, is, REG_PMU_ISP_ARM_OPTION);
		pmuisp_write(0x1, is, REG_PMU_ISP_ARM_CONFIGURATION);
	} else {
		/* A5 power off */
		pmuisp_write(0x10000, is, REG_PMU_ISP_ARM_OPTION);
		pmuisp_write(0x0, is, REG_PMU_ISP_ARM_CONFIGURATION);

		while (pmuisp_read(is, REG_PMU_ISP_ARM_STATUS) & 1) {
			if (timeout == 0)
				return -ETIME;
			timeout--;
			udelay(1);
		}
	}

	return 0;
}

/* Wait until @bit of @is->state is set to @state in the interrupt handler. */
int fimc_is_wait_event(struct fimc_is *is, unsigned long bit,
		       unsigned int state, unsigned int timeout)
{

	int ret = wait_event_timeout(is->irq_queue,
				     !state ^ test_bit(bit, &is->state),
				     timeout);
	if (ret == 0) {
		dev_WARN(&is->pdev->dev, "%s() timed out\n", __func__);
		return -ETIME;
	}
	return 0;
}

int fimc_is_start_firmware(struct fimc_is *is)
{
	struct device *dev = &is->pdev->dev;
	int ret;

	if (is->fw.f_w == NULL) {
		dev_err(dev, "firmware is not loaded\n");
		return -EINVAL;
	}

	memcpy(is->memory.vaddr, is->fw.f_w->data, is->fw.f_w->size);
	wmb();

	ret = fimc_is_cpu_set_power(is, 1);
	if (ret < 0)
		return ret;

	ret = fimc_is_wait_event(is, IS_ST_A5_PWR_ON, 1,
				 msecs_to_jiffies(FIMC_IS_FW_LOAD_TIMEOUT));
	if (ret < 0)
		dev_err(dev, "FIMC-IS CPU power on failed\n");

	return ret;
}

/* Allocate working memory for the FIMC-IS CPU. */
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

	is->is_p_region = (struct is_region *)(is->memory.vaddr +
				FIMC_IS_CPU_MEM_SIZE - FIMC_IS_REGION_SIZE);

	is->is_dma_p_region = is->memory.paddr +
				FIMC_IS_CPU_MEM_SIZE - FIMC_IS_REGION_SIZE;

	is->is_shared_region = (struct is_share_region *)(is->memory.vaddr +
				FIMC_IS_SHARED_REGION_OFFSET);
	return 0;
}

static void fimc_is_free_cpu_memory(struct fimc_is *is)
{
	struct device *dev = &is->pdev->dev;

	if (is->memory.vaddr == NULL)
		return;

	dma_free_coherent(dev, is->memory.size, is->memory.vaddr,
			  is->memory.paddr);
}

static void fimc_is_load_firmware(const struct firmware *fw, void *context)
{
	struct fimc_is *is = context;
	struct device *dev = &is->pdev->dev;
	void *buf;
	int ret;

	if (fw == NULL) {
		dev_err(dev, "firmware request failed\n");
		return;
	}
	mutex_lock(&is->lock);

	if (fw->size < FIMC_IS_FW_SIZE_MIN || fw->size > FIMC_IS_FW_SIZE_MAX) {
		dev_err(dev, "wrong firmware size: %zu\n", fw->size);
		goto done;
	}

	is->fw.size = fw->size;

	ret = fimc_is_alloc_cpu_memory(is);
	if (ret < 0) {
		dev_err(dev, "failed to allocate FIMC-IS CPU memory\n");
		goto done;
	}

	memcpy(is->memory.vaddr, fw->data, fw->size);
	wmb();

	/* Read firmware description. */
	buf = (void *)(is->memory.vaddr + fw->size - FIMC_IS_FW_DESC_LEN);
	memcpy(&is->fw.info, buf, FIMC_IS_FW_INFO_LEN);
	is->fw.info[FIMC_IS_FW_INFO_LEN] = 0;

	buf = (void *)(is->memory.vaddr + fw->size - FIMC_IS_FW_VER_LEN);
	memcpy(&is->fw.version, buf, FIMC_IS_FW_VER_LEN);
	is->fw.version[FIMC_IS_FW_VER_LEN - 1] = 0;

	is->fw.state = 1;

	dev_info(dev, "loaded firmware: %s, rev. %s\n",
		 is->fw.info, is->fw.version);
	dev_dbg(dev, "FW size: %zu, paddr: %pad\n", fw->size, &is->memory.paddr);

	is->is_shared_region->chip_id = 0xe4412;
	is->is_shared_region->chip_rev_no = 1;

	fimc_is_mem_barrier();

	/*
	 * FIXME: The firmware is not being released for now, as it is
	 * needed around for copying to the IS working memory every
	 * time before the Cortex-A5 is restarted.
	 */
	release_firmware(is->fw.f_w);
	is->fw.f_w = fw;
done:
	mutex_unlock(&is->lock);
}

static int fimc_is_request_firmware(struct fimc_is *is, const char *fw_name)
{
	return request_firmware_nowait(THIS_MODULE,
				FW_ACTION_HOTPLUG, fw_name, &is->pdev->dev,
				GFP_KERNEL, is, fimc_is_load_firmware);
}

/* General IS interrupt handler */
static void fimc_is_general_irq_handler(struct fimc_is *is)
{
	is->i2h_cmd.cmd = mcuctl_read(is, MCUCTL_REG_ISSR(10));

	switch (is->i2h_cmd.cmd) {
	case IHC_GET_SENSOR_NUM:
		fimc_is_hw_get_params(is, 1);
		fimc_is_hw_wait_intmsr0_intmsd0(is);
		fimc_is_hw_set_sensor_num(is);
		pr_debug("ISP FW version: %#x\n", is->i2h_cmd.args[0]);
		break;
	case IHC_SET_FACE_MARK:
	case IHC_FRAME_DONE:
		fimc_is_hw_get_params(is, 2);
		break;
	case IHC_SET_SHOT_MARK:
	case IHC_AA_DONE:
	case IH_REPLY_DONE:
		fimc_is_hw_get_params(is, 3);
		break;
	case IH_REPLY_NOT_DONE:
		fimc_is_hw_get_params(is, 4);
		break;
	case IHC_NOT_READY:
		break;
	default:
		pr_info("unknown command: %#x\n", is->i2h_cmd.cmd);
	}

	fimc_is_fw_clear_irq1(is, FIMC_IS_INT_GENERAL);

	switch (is->i2h_cmd.cmd) {
	case IHC_GET_SENSOR_NUM:
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
		break;

	case IH_REPLY_DONE:
		pr_debug("ISR_DONE: args[0]: %#x\n", is->i2h_cmd.args[0]);

		switch (is->i2h_cmd.args[0]) {
		case HIC_PREVIEW_STILL...HIC_CAPTURE_VIDEO:
			/* Get CAC margin */
			set_bit(IS_ST_CHANGE_MODE, &is->state);
			is->isp.cac_margin_x = is->i2h_cmd.args[1];
			is->isp.cac_margin_y = is->i2h_cmd.args[2];
			pr_debug("CAC margin (x,y): (%d,%d)\n",
				 is->isp.cac_margin_x, is->isp.cac_margin_y);
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
			is->config[is->config_index].p_region_index[0] = 0;
			is->config[is->config_index].p_region_index[1] = 0;
			set_bit(IS_ST_BLOCK_CMD_CLEARED, &is->state);
			pr_debug("HIC_SET_PARAMETER\n");
			break;

		case HIC_GET_PARAMETER:
			break;

		case HIC_SET_TUNE:
			break;

		case HIC_GET_STATUS:
			break;

		case HIC_OPEN_SENSOR:
			set_bit(IS_ST_OPEN_SENSOR, &is->state);
			pr_debug("data lanes: %d, settle line: %d\n",
				 is->i2h_cmd.args[2], is->i2h_cmd.args[1]);
			break;

		case HIC_CLOSE_SENSOR:
			clear_bit(IS_ST_OPEN_SENSOR, &is->state);
			is->sensor_index = 0;
			break;

		case HIC_MSG_TEST:
			pr_debug("config MSG level completed\n");
			break;

		case HIC_POWER_DOWN:
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

	case IH_REPLY_NOT_DONE:
		pr_err("ISR_NDONE: %d: %#x, %s\n", is->i2h_cmd.args[0],
		       is->i2h_cmd.args[1],
		       fimc_is_strerr(is->i2h_cmd.args[1]));

		if (is->i2h_cmd.args[1] & IS_ERROR_TIME_OUT_FLAG)
			pr_err("IS_ERROR_TIME_OUT\n");

		switch (is->i2h_cmd.args[1]) {
		case IS_ERROR_SET_PARAMETER:
			fimc_is_mem_barrier();
		}

		switch (is->i2h_cmd.args[0]) {
		case HIC_SET_PARAMETER:
			is->config[is->config_index].p_region_index[0] = 0;
			is->config[is->config_index].p_region_index[1] = 0;
			set_bit(IS_ST_BLOCK_CMD_CLEARED, &is->state);
			break;
		}
		break;

	case IHC_NOT_READY:
		pr_err("IS control sequence error: Not Ready\n");
		break;
	}

	wake_up(&is->irq_queue);
}

static irqreturn_t fimc_is_irq_handler(int irq, void *priv)
{
	struct fimc_is *is = priv;
	unsigned long flags;
	u32 status;

	spin_lock_irqsave(&is->slock, flags);
	status = mcuctl_read(is, MCUCTL_REG_INTSR1);

	if (status & (1UL << FIMC_IS_INT_GENERAL))
		fimc_is_general_irq_handler(is);

	if (status & (1UL << FIMC_IS_INT_FRAME_DONE_ISP))
		fimc_isp_irq_handler(is);

	spin_unlock_irqrestore(&is->slock, flags);
	return IRQ_HANDLED;
}

static int fimc_is_hw_open_sensor(struct fimc_is *is,
				  struct fimc_is_sensor *sensor)
{
	struct sensor_open_extended *soe = (void *)&is->is_p_region->shared;

	fimc_is_hw_wait_intmsr0_intmsd0(is);

	soe->self_calibration_mode = 1;
	soe->actuator_type = 0;
	soe->mipi_lane_num = 0;
	soe->mclk = 0;
	soe->mipi_speed	= 0;
	soe->fast_open_sensor = 0;
	soe->i2c_sclk = 88000000;

	fimc_is_mem_barrier();

	/*
	 * Some user space use cases hang up here without this
	 * empirically chosen delay.
	 */
	udelay(100);

	mcuctl_write(HIC_OPEN_SENSOR, is, MCUCTL_REG_ISSR(0));
	mcuctl_write(is->sensor_index, is, MCUCTL_REG_ISSR(1));
	mcuctl_write(sensor->drvdata->id, is, MCUCTL_REG_ISSR(2));
	mcuctl_write(sensor->i2c_bus, is, MCUCTL_REG_ISSR(3));
	mcuctl_write(is->is_dma_p_region, is, MCUCTL_REG_ISSR(4));

	fimc_is_hw_set_intgr0_gd0(is);

	return fimc_is_wait_event(is, IS_ST_OPEN_SENSOR, 1,
				  sensor->drvdata->open_timeout);
}


int fimc_is_hw_initialize(struct fimc_is *is)
{
	static const int config_ids[] = {
		IS_SC_PREVIEW_STILL, IS_SC_PREVIEW_VIDEO,
		IS_SC_CAPTURE_STILL, IS_SC_CAPTURE_VIDEO
	};
	struct device *dev = &is->pdev->dev;
	u32 prev_id;
	int i, ret;

	/* Sensor initialization. Only one sensor is currently supported. */
	ret = fimc_is_hw_open_sensor(is, &is->sensor[0]);
	if (ret < 0)
		return ret;

	/* Get the setfile address. */
	fimc_is_hw_get_setfile_addr(is);

	ret = fimc_is_wait_event(is, IS_ST_SETFILE_LOADED, 1,
				 FIMC_IS_CONFIG_TIMEOUT);
	if (ret < 0) {
		dev_err(dev, "get setfile address timed out\n");
		return ret;
	}
	pr_debug("setfile.base: %#x\n", is->setfile.base);

	/* Load the setfile. */
	fimc_is_load_setfile(is, FIMC_IS_SETFILE_6A3);
	clear_bit(IS_ST_SETFILE_LOADED, &is->state);
	fimc_is_hw_load_setfile(is);
	ret = fimc_is_wait_event(is, IS_ST_SETFILE_LOADED, 1,
				 FIMC_IS_CONFIG_TIMEOUT);
	if (ret < 0) {
		dev_err(dev, "loading setfile timed out\n");
		return ret;
	}

	pr_debug("setfile: base: %#x, size: %d\n",
		 is->setfile.base, is->setfile.size);
	pr_info("FIMC-IS Setfile info: %s\n", is->fw.setfile_info);

	/* Check magic number. */
	if (is->is_p_region->shared[MAX_SHARED_COUNT - 1] !=
	    FIMC_IS_MAGIC_NUMBER) {
		dev_err(dev, "magic number error!\n");
		return -EIO;
	}

	pr_debug("shared region: %pad, parameter region: %pad\n",
		 &is->memory.paddr + FIMC_IS_SHARED_REGION_OFFSET,
		 &is->is_dma_p_region);

	is->setfile.sub_index = 0;

	/* Stream off. */
	fimc_is_hw_stream_off(is);
	ret = fimc_is_wait_event(is, IS_ST_STREAM_OFF, 1,
				 FIMC_IS_CONFIG_TIMEOUT);
	if (ret < 0) {
		dev_err(dev, "stream off timeout\n");
		return ret;
	}

	/* Preserve previous mode. */
	prev_id = is->config_index;

	/* Set initial parameter values. */
	for (i = 0; i < ARRAY_SIZE(config_ids); i++) {
		is->config_index = config_ids[i];
		fimc_is_set_initial_params(is);
		ret = fimc_is_itf_s_param(is, true);
		if (ret < 0) {
			is->config_index = prev_id;
			return ret;
		}
	}
	is->config_index = prev_id;

	set_bit(IS_ST_INIT_DONE, &is->state);
	dev_info(dev, "initialization sequence completed (%d)\n",
						is->config_index);
	return 0;
}

static int fimc_is_show(struct seq_file *s, void *data)
{
	struct fimc_is *is = s->private;
	const u8 *buf = is->memory.vaddr + FIMC_IS_DEBUG_REGION_OFFSET;

	if (is->memory.vaddr == NULL) {
		dev_err(&is->pdev->dev, "firmware memory is not initialized\n");
		return -EIO;
	}

	seq_printf(s, "%s\n", buf);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(fimc_is);

static void fimc_is_debugfs_remove(struct fimc_is *is)
{
	debugfs_remove_recursive(is->debugfs_entry);
	is->debugfs_entry = NULL;
}

static int fimc_is_debugfs_create(struct fimc_is *is)
{
	struct dentry *dentry;

	is->debugfs_entry = debugfs_create_dir("fimc_is", NULL);

	dentry = debugfs_create_file("fw_log", S_IRUGO, is->debugfs_entry,
				     is, &fimc_is_fops);
	if (!dentry)
		fimc_is_debugfs_remove(is);

	return is->debugfs_entry == NULL ? -EIO : 0;
}

static int fimc_is_runtime_resume(struct device *dev);
static int fimc_is_runtime_suspend(struct device *dev);

static int fimc_is_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimc_is *is;
	struct resource res;
	struct device_node *node;
	int ret;

	is = devm_kzalloc(&pdev->dev, sizeof(*is), GFP_KERNEL);
	if (!is)
		return -ENOMEM;

	is->pdev = pdev;
	is->isp.pdev = pdev;

	init_waitqueue_head(&is->irq_queue);
	spin_lock_init(&is->slock);
	mutex_init(&is->lock);

	ret = of_address_to_resource(dev->of_node, 0, &res);
	if (ret < 0)
		return ret;

	is->regs = devm_ioremap_resource(dev, &res);
	if (IS_ERR(is->regs))
		return PTR_ERR(is->regs);

	node = of_get_child_by_name(dev->of_node, "pmu");
	if (!node)
		return -ENODEV;

	is->pmu_regs = of_iomap(node, 0);
	if (!is->pmu_regs)
		return -ENOMEM;

	is->irq = irq_of_parse_and_map(dev->of_node, 0);
	if (!is->irq) {
		dev_err(dev, "no irq found\n");
		ret = -EINVAL;
		goto err_iounmap;
	}

	ret = fimc_is_get_clocks(is);
	if (ret < 0)
		goto err_iounmap;

	platform_set_drvdata(pdev, is);

	ret = request_irq(is->irq, fimc_is_irq_handler, 0, dev_name(dev), is);
	if (ret < 0) {
		dev_err(dev, "irq request failed\n");
		goto err_clk;
	}
	pm_runtime_enable(dev);

	if (!pm_runtime_enabled(dev)) {
		ret = fimc_is_runtime_resume(dev);
		if (ret < 0)
			goto err_irq;
	}

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto err_pm;

	vb2_dma_contig_set_max_seg_size(dev, DMA_BIT_MASK(32));

	ret = devm_of_platform_populate(dev);
	if (ret < 0)
		goto err_pm;

	/*
	 * Register FIMC-IS V4L2 subdevs to this driver. The video nodes
	 * will be created within the subdev's registered() callback.
	 */
	ret = fimc_is_register_subdevs(is);
	if (ret < 0)
		goto err_pm;

	ret = fimc_is_debugfs_create(is);
	if (ret < 0)
		goto err_sd;

	ret = fimc_is_request_firmware(is, FIMC_IS_FW_FILENAME);
	if (ret < 0)
		goto err_dfs;

	pm_runtime_put_sync(dev);

	dev_dbg(dev, "FIMC-IS registered successfully\n");
	return 0;

err_dfs:
	fimc_is_debugfs_remove(is);
err_sd:
	fimc_is_unregister_subdevs(is);
err_pm:
	if (!pm_runtime_enabled(dev))
		fimc_is_runtime_suspend(dev);
err_irq:
	free_irq(is->irq, is);
err_clk:
	fimc_is_put_clocks(is);
err_iounmap:
	iounmap(is->pmu_regs);
	return ret;
}

static int fimc_is_runtime_resume(struct device *dev)
{
	struct fimc_is *is = dev_get_drvdata(dev);
	int ret;

	ret = fimc_is_setup_clocks(is);
	if (ret)
		return ret;

	return fimc_is_enable_clocks(is);
}

static int fimc_is_runtime_suspend(struct device *dev)
{
	struct fimc_is *is = dev_get_drvdata(dev);

	fimc_is_disable_clocks(is);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int fimc_is_resume(struct device *dev)
{
	/* TODO: */
	return 0;
}

static int fimc_is_suspend(struct device *dev)
{
	struct fimc_is *is = dev_get_drvdata(dev);

	/* TODO: */
	if (test_bit(IS_ST_A5_PWR_ON, &is->state))
		return -EBUSY;

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static int fimc_is_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimc_is *is = dev_get_drvdata(dev);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	if (!pm_runtime_status_suspended(dev))
		fimc_is_runtime_suspend(dev);
	free_irq(is->irq, is);
	fimc_is_unregister_subdevs(is);
	vb2_dma_contig_clear_max_seg_size(dev);
	fimc_is_put_clocks(is);
	iounmap(is->pmu_regs);
	fimc_is_debugfs_remove(is);
	release_firmware(is->fw.f_w);
	fimc_is_free_cpu_memory(is);

	return 0;
}

static const struct of_device_id fimc_is_of_match[] = {
	{ .compatible = "samsung,exynos4212-fimc-is" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, fimc_is_of_match);

static const struct dev_pm_ops fimc_is_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fimc_is_suspend, fimc_is_resume)
	SET_RUNTIME_PM_OPS(fimc_is_runtime_suspend, fimc_is_runtime_resume,
			   NULL)
};

static struct platform_driver fimc_is_driver = {
	.probe		= fimc_is_probe,
	.remove		= fimc_is_remove,
	.driver = {
		.of_match_table	= fimc_is_of_match,
		.name		= FIMC_IS_DRV_NAME,
		.pm		= &fimc_is_pm_ops,
	}
};

static int fimc_is_module_init(void)
{
	int ret;

	ret = fimc_is_register_i2c_driver();
	if (ret < 0)
		return ret;

	ret = platform_driver_register(&fimc_is_driver);

	if (ret < 0)
		fimc_is_unregister_i2c_driver();

	return ret;
}

static void fimc_is_module_exit(void)
{
	fimc_is_unregister_i2c_driver();
	platform_driver_unregister(&fimc_is_driver);
}

module_init(fimc_is_module_init);
module_exit(fimc_is_module_exit);

MODULE_ALIAS("platform:" FIMC_IS_DRV_NAME);
MODULE_AUTHOR("Younghwan Joo <yhwan.joo@samsung.com>");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL v2");
