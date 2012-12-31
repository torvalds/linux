/*
 * Samsung Exynos4 SoC series FIMC-IS slave interface driver
 *
 * v4l2 subdev driver interface
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 * Contact: Younghwan Joo, <yhwan.joo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/memory.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/workqueue.h>

#include <linux/videodev2.h>
#include <linux/videodev2_samsung.h>
#include <media/videobuf2-core.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-mediabus.h>

#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>

#include "fimc-is-core.h"
#include "fimc-is-regs.h"
#include "fimc-is-param.h"
#include "fimc-is-cmd.h"
#include "fimc-is-err.h"

/* Binary load functions */
static int fimc_is_request_firmware(struct fimc_is_dev *dev)
{
	int ret;
	struct firmware *fw_blob;
	u8 *buf = NULL;
#ifdef SDCARD_FW
	struct file *fp;
	mm_segment_t old_fs;
	long fsize, nread;
	int fw_requested = 1;

	ret = 0;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	fp = filp_open(FIMC_IS_FW_SDCARD, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		dbg("failed to open %s\n", FIMC_IS_FW_SDCARD);
		goto request_fw;
	}
	fw_requested = 0;
	fsize = fp->f_path.dentry->d_inode->i_size;
	dbg("start, file path %s, size %ld Bytes\n", FIMC_IS_FW_SDCARD, fsize);
	buf = vmalloc(fsize);
	if (!buf) {
		err("failed to allocate memory\n");
		ret = -ENOMEM;
		goto out;
	}
	nread = vfs_read(fp, (char __user *)buf, fsize, &fp->f_pos);
	if (nread != fsize) {
		err("failed to read firmware file, %ld Bytes\n", nread);
		ret = -EIO;
		goto out;
	}

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
	memcpy((void *)phys_to_virt(dev->mem.base), (void *)buf, fsize);
	fimc_is_mem_cache_clean((void *)phys_to_virt(dev->mem.base),
		fsize + 1);
	if (dev->mem.fw_ref_base > 0) {
		memcpy((void *)phys_to_virt(dev->mem.fw_ref_base),
							(void *)buf, fsize);
		fimc_is_mem_cache_clean(
			(void *)phys_to_virt(dev->mem.fw_ref_base), fsize + 1);
		dev->fw.size = fsize;
	}
#elif defined(CONFIG_VIDEOBUF2_ION)
	if (dev->mem.bitproc_buf == 0) {
		err("failed to load FIMC-IS F/W, FIMC-IS will not working\n");
	} else {
		memcpy(dev->mem.kvaddr, (void *)buf, fsize);
		fimc_is_mem_cache_clean((void *)dev->mem.kvaddr, fsize + 1);
	}
#endif
	vfs_llseek(fp, -FIMC_IS_FW_VERSION_LENGTH, SEEK_END);
	vfs_read(fp, (char __user *)dev->fw.fw_version,
				(FIMC_IS_FW_VERSION_LENGTH - 1), &fp->f_pos);
	dev->fw.fw_version[FIMC_IS_FW_VERSION_LENGTH - 1] = '\0';
	vfs_llseek(fp, -(FIMC_IS_FW_INFO_LENGTH +
				FIMC_IS_FW_VERSION_LENGTH - 1), SEEK_END);
	vfs_read(fp, (char __user *)dev->fw.fw_info,
					(FIMC_IS_FW_INFO_LENGTH-1), &fp->f_pos);
	dev->fw.fw_info[FIMC_IS_FW_INFO_LENGTH - 1] = '\0';
	dev->fw.state = 1;
request_fw:
	if (fw_requested) {
		set_fs(old_fs);
#endif
		ret = request_firmware((const struct firmware **)&fw_blob,
					FIMC_IS_FW, &dev->pdev->dev);
		if (ret) {
			dev_err(&dev->pdev->dev,
				"could not load firmware (err=%d)\n", ret);
			return -EINVAL;
		}
#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
		memcpy((void *)phys_to_virt(dev->mem.base),
				fw_blob->data, fw_blob->size);
		fimc_is_mem_cache_clean((void *)phys_to_virt(dev->mem.base),
							fw_blob->size + 1);
		if (dev->mem.fw_ref_base > 0) {
			memcpy((void *)phys_to_virt(dev->mem.fw_ref_base),
						fw_blob->data, fw_blob->size);
			fimc_is_mem_cache_clean(
				(void *)phys_to_virt(dev->mem.fw_ref_base),
				fw_blob->size + 1);
			dev->fw.size = fw_blob->size;
		}
#elif defined(CONFIG_VIDEOBUF2_ION)
		if (dev->mem.bitproc_buf == 0) {
			err("failed to load FIMC-IS F/W\n");
			return -EINVAL;
		} else {
			memcpy(dev->mem.kvaddr, fw_blob->data, fw_blob->size);
			fimc_is_mem_cache_clean(
				(void *)dev->mem.kvaddr, fw_blob->size + 1);
			dbg(
		"FIMC_IS F/W loaded successfully - size:%d\n", fw_blob->size);
		}
#endif
		memcpy((void *)dev->fw.fw_info,
			(fw_blob->data + fw_blob->size -
			(FIMC_IS_FW_INFO_LENGTH + FIMC_IS_FW_VERSION_LENGTH-1)),
			(FIMC_IS_FW_INFO_LENGTH - 1));
		dev->fw.fw_info[FIMC_IS_FW_INFO_LENGTH - 1] = '\0';
		memcpy((void *)dev->fw.fw_version,
			(fw_blob->data + fw_blob->size -
				FIMC_IS_FW_VERSION_LENGTH),
					(FIMC_IS_FW_VERSION_LENGTH - 1));
		dev->fw.fw_version[FIMC_IS_FW_VERSION_LENGTH - 1] = '\0';
		dev->fw.state = 1;
		dbg("FIMC_IS F/W loaded successfully - size:%d\n",
							fw_blob->size);
		release_firmware(fw_blob);
#ifdef SDCARD_FW
	}
#endif

out:
#ifdef SDCARD_FW
	if (!fw_requested) {
		vfree(buf);
		filp_close(fp, current->files);
		set_fs(old_fs);
	}
#endif
	printk(KERN_INFO "FIMC-IS FW loaded = 0x%08x\n", dev->mem.base);
	return ret;
}

static int fimc_is_load_setfile(struct fimc_is_dev *dev)
{
	int ret;
	struct firmware *fw_blob;
	u8 *buf = NULL;
#ifdef SDCARD_FW
	struct file *fp;
	mm_segment_t old_fs;
	long fsize, nread;
	int fw_requested = 1;

	ret = 0;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	if ((dev->sensor.sensor_type == SENSOR_S5K3H7_CSI_A) ||
		(dev->sensor.sensor_type == SENSOR_S5K3H7_CSI_B))
		fp = filp_open(FIMC_IS_SETFILE_SDCARD_3H7, O_RDONLY, 0);
	else
		fp = filp_open(FIMC_IS_SETFILE_SDCARD_6A3, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		dbg("failed to open %s\n", FIMC_IS_SETFILE_SDCARD);
		goto request_fw;
	}
	fw_requested = 0;
	fsize = fp->f_path.dentry->d_inode->i_size;
	if ((dev->sensor.sensor_type == SENSOR_S5K3H7_CSI_A) ||
		(dev->sensor.sensor_type == SENSOR_S5K3H7_CSI_B))
		dbg("start, file path %s, size %ld Bytes\n",
			FIMC_IS_SETFILE_SDCARD_3H7, fsize);
	else
		dbg("start, file path %s, size %ld Bytes\n",
			FIMC_IS_SETFILE_SDCARD_6A3, fsize);
	buf = vmalloc(fsize);
	if (!buf) {
		err("failed to allocate memory\n");
		ret = -ENOMEM;
		goto out;
	}
	nread = vfs_read(fp, (char __user *)buf, fsize, &fp->f_pos);
	if (nread != fsize) {
		err("failed to read firmware file, %ld Bytes\n", nread);
		ret = -EIO;
		goto out;
	}
#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
	memcpy((void *)phys_to_virt(dev->mem.base + dev->setfile.base),
							(void *)buf, fsize);
	fimc_is_mem_cache_clean(
		(void *)phys_to_virt(dev->mem.base + dev->setfile.base),
		fsize + 1);
	if (dev->mem.setfile_ref_base > 0) {
		memcpy((void *)phys_to_virt(dev->mem.setfile_ref_base),
							(void *)buf, fsize);
		fimc_is_mem_cache_clean(
			(void *)phys_to_virt(dev->mem.setfile_ref_base),
			fsize + 1);
		dev->setfile.size = fsize;
	}
#elif defined(CONFIG_VIDEOBUF2_ION)
	if (dev->mem.bitproc_buf == 0) {
		err("failed to load FIMC-IS F/W, FIMC-IS will not working\n");
	} else {
		memcpy((dev->mem.kvaddr + dev->setfile.base),
						(void *)buf, fsize);
		fimc_is_mem_cache_clean((void *)dev->mem.kvaddr, fsize + 1);
		dbg("FIMC_IS Setfile loaded successfully - size:%ld\n", fsize);
	}
#endif
	vfs_llseek(fp, -FIMC_IS_SETFILE_INFO_LENGTH, SEEK_END);
	vfs_read(fp, (char __user *)dev->fw.setfile_info,
				FIMC_IS_SETFILE_INFO_LENGTH, &fp->f_pos);
	dev->setfile.state = 1;
request_fw:
	if (fw_requested) {
		set_fs(old_fs);
#endif
		if ((dev->sensor.sensor_type == SENSOR_S5K3H7_CSI_A) ||
			(dev->sensor.sensor_type == SENSOR_S5K3H7_CSI_B))
			ret = request_firmware(
					(const struct firmware **)&fw_blob,
					FIMC_IS_SETFILE_3H7, &dev->pdev->dev);
		else
			ret = request_firmware(
					(const struct firmware **)&fw_blob,
					FIMC_IS_SETFILE_6A3, &dev->pdev->dev);
		if (ret) {
			dev_err(&dev->pdev->dev,
				"could not load firmware (err=%d)\n", ret);
			return -EINVAL;
		}
#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
		memcpy((void *)phys_to_virt(dev->mem.base + dev->setfile.base),
			fw_blob->data, fw_blob->size);
		fimc_is_mem_cache_clean(
			(void *)phys_to_virt(dev->mem.base + dev->setfile.base),
			fw_blob->size + 1);
		if (dev->mem.setfile_ref_base > 0) {
			memcpy((void *)phys_to_virt(dev->mem.setfile_ref_base),
					fw_blob->data, fw_blob->size);
			fimc_is_mem_cache_clean(
				(void *)phys_to_virt(dev->mem.setfile_ref_base),
				fw_blob->size + 1);
			dev->setfile.size = fw_blob->size;
		}
#elif defined(CONFIG_VIDEOBUF2_ION)
		if (dev->mem.bitproc_buf == 0) {
			err("failed to load FIMC-IS F/W\n");
			return -EINVAL;
		} else {
			memcpy((dev->mem.kvaddr + dev->setfile.base),
						fw_blob->data, fw_blob->size);
			fimc_is_mem_cache_clean((void *)dev->mem.kvaddr,
						fw_blob->size + 1);
			dbg(
		"FIMC_IS F/W loaded successfully - size:%d\n", fw_blob->size);
		}
#endif
		memcpy((void *)dev->fw.setfile_info,
			(fw_blob->data + fw_blob->size -
				FIMC_IS_SETFILE_INFO_LENGTH),
				(FIMC_IS_SETFILE_INFO_LENGTH - 1));
		dev->fw.setfile_info[FIMC_IS_SETFILE_INFO_LENGTH - 1] = '\0';
		dev->setfile.state = 1;
		dbg("FIMC_IS setfile loaded successfully - size:%d\n",
								fw_blob->size);
		release_firmware(fw_blob);
#ifdef SDCARD_FW
	}
#endif

	dbg("A5 mem base  = 0x%08x\n", dev->mem.base);
	dbg("Setfile base  = 0x%08x\n", dev->setfile.base);
out:
#ifdef SDCARD_FW
	if (!fw_requested) {
		vfree(buf);
		filp_close(fp, current->files);
		set_fs(old_fs);
	}
#endif
	return ret;
}

/* v4l2 subdev core operations
*/
static int fimc_is_load_fw(struct v4l2_subdev *sd)
{
	int ret = 0;
	struct fimc_is_dev *dev = to_fimc_is_dev(sd);
	dbg("+++ fimc_is_load_fw\n");
	if (!test_bit(IS_ST_IDLE, &dev->state)) {
		err("FW was already loaded!!\n");
		return ret;
	}
	/* 1. Load IS firmware */
	if (dev->fw.state && (dev->mem.fw_ref_base > 0)) {
		memcpy((void *)phys_to_virt(dev->mem.base),
			(void *)phys_to_virt(dev->mem.fw_ref_base),
			dev->fw.size);
		fimc_is_mem_cache_clean((void *)phys_to_virt(dev->mem.base),
							dev->fw.size + 1);
	} else {
		ret = fimc_is_request_firmware(dev);
		if (ret) {
			err("failed to fimc_is_request_firmware (%d)\n", ret);
			return -EINVAL;
		}
	}
	/* 2. Init GPIO (UART) */
	ret = fimc_is_hw_io_init(dev);
	if (ret) {
		dev_err(&dev->pdev->dev, "failed to init GPIO config\n");
		return -EINVAL;
	}
	/* 3. Chip ID and Revision */
	dev->is_shared_region->chip_id = 0xe4412;
	dev->is_shared_region->chip_rev_no = 1;
	fimc_is_mem_cache_clean((void *)IS_SHARED,
		(unsigned long)(sizeof(struct is_share_region)));
	/* 4. A5 power on */
	printk(KERN_INFO "FIMC-IS A5 power on\n");
	fimc_is_hw_a5_power(dev, 1);
	ret = wait_event_timeout(dev->irq_queue1,
		test_bit(IS_ST_FW_LOADED, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		dev_err(&dev->pdev->dev,
			"wait timeout A5 power on: %s\n", __func__);
		fimc_is_hw_set_low_poweroff(dev, true);
		return -EINVAL;
	}
	clear_bit(IS_ST_IDLE, &dev->state);
	dbg("--- fimc_is_load_fw end\n");
	printk(KERN_INFO "FIMC-IS FW info = %s\n", dev->fw.fw_info);
	printk(KERN_INFO "FIMC-IS FW ver = %s\n", dev->fw.fw_version);
	return ret;
}

int fimc_is_s_power(struct v4l2_subdev *sd, int on)
{
	struct fimc_is_dev *is_dev = to_fimc_is_dev(sd);
	struct device *dev = &is_dev->pdev->dev;
	int ret = 0;

	dbg("fimc_is_s_power\n");
	if (on) {
		if (test_bit(IS_PWR_ST_POWERON, &is_dev->power)) {
			err("FIMC-IS was already power on state!!\n");
			return ret;
		}
#if defined(CONFIG_BUSFREQ_OPP) || defined(CONFIG_BUSFREQ_LOCK_WRAPPER)
		/* lock bus frequency */
		dev_lock(is_dev->bus_dev, dev, BUS_LOCK_FREQ_L0);
#endif
		fimc_is_hw_set_low_poweroff(is_dev, false);
		ret = pm_runtime_get_sync(dev);
		set_bit(IS_ST_A5_PWR_ON, &is_dev->state);
	} else {
		if (test_bit(IS_PWR_ST_POWEROFF, &is_dev->power)) {
			err("FIMC-IS was already power off state!!\n");
			err("Close sensor - %d\n", is_dev->sensor.id);
			fimc_is_hw_close_sensor(is_dev, 0);
			ret = wait_event_timeout(is_dev->irq_queue1,
				!test_bit(IS_ST_OPEN_SENSOR,
				&is_dev->power), FIMC_IS_SHUTDOWN_TIMEOUT);
			if (!ret) {
				err("Timeout-close sensor:%s\n", __func__);
				fimc_is_hw_set_low_poweroff(is_dev, true);
			} else {
				is_dev->p_region_index1 = 0;
				is_dev->p_region_index2 = 0;
				atomic_set(&is_dev->p_region_num, 0);
				return ret;
			}
		}

		if (!test_bit(IS_PWR_SUB_IP_POWER_OFF, &is_dev->power)) {
			fimc_is_hw_subip_poweroff(is_dev);
			ret = wait_event_timeout(is_dev->irq_queue1,
				test_bit(IS_PWR_SUB_IP_POWER_OFF,
				&is_dev->power), FIMC_IS_SHUTDOWN_TIMEOUT);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				fimc_is_hw_set_low_poweroff(is_dev, true);
			}
		}

		printk(KERN_INFO "FIMC-IS A5 power off\n");
		fimc_is_hw_a5_power(is_dev, 0);
		ret = pm_runtime_put_sync(dev);

		is_dev->sensor.id = 0;
		is_dev->sensor.framerate_update = false;
		is_dev->p_region_index1 = 0;
		is_dev->p_region_index2 = 0;
		atomic_set(&is_dev->p_region_num, 0);
		is_dev->state = 0;
		set_bit(IS_ST_IDLE, &is_dev->state);
		is_dev->power = 0;
		is_dev->af.af_state = FIMC_IS_AF_IDLE;
		is_dev->af.mode = IS_FOCUS_MODE_IDLE;
		set_bit(IS_PWR_ST_POWEROFF, &is_dev->power);
	}
	return ret;
}

static int fimc_is_init_set(struct v4l2_subdev *sd, u32 val)
{
	int ret = 0;
	struct fimc_is_dev *dev = to_fimc_is_dev(sd);
	dev->sensor.sensor_type = val;
	dev->sensor.id = 0;
	dbg("fimc_is_init\n");
	if (!test_bit(IS_ST_A5_PWR_ON, &dev->state)) {
		err("A5 is not power on state!!\n");
		return -EINVAL;
	}
	/* Init sequence 1: Open sensor */
	dbg("v4l2 : open sensor : %d\n", val);
	fimc_is_hw_open_sensor(dev, dev->sensor.id, val);
	ret = wait_event_timeout(dev->irq_queue1,
		test_bit(IS_ST_OPEN_SENSOR, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		err("wait timeout - open sensor\n");
		fimc_is_hw_set_low_poweroff(dev, true);
		return -EINVAL;
	}
	/* Init sequence 2: Load setfile */
	/* Get setfile address */
	dbg("v4l2 : setfile address\n");
	fimc_is_hw_get_setfile_addr(dev);
	ret = wait_event_timeout(dev->irq_queue1,
		test_bit(IS_ST_SETFILE_LOADED, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		err("wait timeout - get setfile address\n");
		fimc_is_hw_set_low_poweroff(dev, true);
		return -EINVAL;
	}
	dbg("v4l2 : load setfile\n");
	if (dev->setfile.state && (dev->mem.setfile_ref_base > 0)) {
		memcpy((void *)phys_to_virt(dev->mem.base + dev->setfile.base),
			(void *)phys_to_virt(dev->mem.setfile_ref_base),
			dev->setfile.size);
		fimc_is_mem_cache_clean(
			(void *)phys_to_virt(dev->mem.base + dev->setfile.base),
			dev->setfile.size + 1);
	} else {
		fimc_is_load_setfile(dev);
	}
	clear_bit(IS_ST_SETFILE_LOADED, &dev->state);
	fimc_is_hw_load_setfile(dev);
	ret = wait_event_timeout(dev->irq_queue1,
		test_bit(IS_ST_SETFILE_LOADED, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		err("wait timeout - load setfile\n");
		fimc_is_hw_set_low_poweroff(dev, true);
		return -EINVAL;
	}
	printk(KERN_INFO "FIMC-IS Setfile info = %s\n", dev->fw.setfile_info);
	dbg("v4l2 : Load set file end\n");
	/* Check magic number */
	if (dev->is_p_region->shared[MAX_SHARED_COUNT-1] != MAGIC_NUMBER)
		err("!!! MAGIC NUMBER ERROR !!!\n");
	/* Display region information (DEBUG only) */
	dbg("Parameter region addr = 0x%08x\n", virt_to_phys(dev->is_p_region));
	dbg("ISP region addr = 0x%08x\n",
				virt_to_phys(&dev->is_p_region->parameter.isp));
	dbg("Shared region addr = 0x%08x\n",
				virt_to_phys(&dev->is_p_region->shared));
	dev->frame_count = 0;
	dev->setfile.sub_index = 0;
	/* Init sequence 3: Stream off */
	dbg("Stream Off\n");
	clear_bit(IS_ST_STREAM_OFF, &dev->state);
	fimc_is_hw_set_stream(dev, 0); /*stream off */
	ret = wait_event_timeout(dev->irq_queue1,
		test_bit(IS_ST_STREAM_OFF, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		err("wait timeout - stream off\n");
		fimc_is_hw_set_low_poweroff(dev, true);
		return -EINVAL;
	}
	/* Init sequence 4: Set init value - PREVIEW_STILL mode */
	dbg("Default setting : preview_still\n");
	dev->scenario_id = ISS_PREVIEW_STILL;
	fimc_is_hw_set_init(dev);
	fimc_is_mem_cache_clean((void *)dev->is_p_region, IS_PARAM_SIZE);
	clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state); /* BLOCK I/F Mode*/
	fimc_is_hw_set_param(dev);
	ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
	if (!ret) {
		err("wait timeout : init values (PREVIEW_STILL)\n");
		fimc_is_hw_set_low_poweroff(dev, true);
		return -EINVAL;
	}
	/* Init sequence 5: Set init value - PREVIEW_VIDEO mode */
	dbg("Default setting : preview_video\n");
	dev->scenario_id = ISS_PREVIEW_VIDEO;
	fimc_is_hw_set_init(dev);
	fimc_is_mem_cache_clean((void *)dev->is_p_region, IS_PARAM_SIZE);
	clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state); /* BLOCK I/F Mode*/
	fimc_is_hw_set_param(dev);
	ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
	if (!ret) {
		err("wait timeout : init values (PREVIEW_VIDEO)\n");
		fimc_is_hw_set_low_poweroff(dev, true);
		return -EINVAL;
	}
	/* Init sequence 6: Set init value - CAPTURE_STILL mode */
	dbg("Default setting : capture_still\n");
	dev->scenario_id = ISS_CAPTURE_STILL;
	fimc_is_hw_set_init(dev);
	fimc_is_mem_cache_clean((void *)dev->is_p_region, IS_PARAM_SIZE);
	clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state); /* BLOCK I/F Mode*/
	fimc_is_hw_set_param(dev);
	ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
	if (!ret) {
		err("wait timeout : init values (CAPTURE_STILL)\n");
		fimc_is_hw_set_low_poweroff(dev, true);
		return -EINVAL;
	}
	/* Init sequence 6: Set init value - CAPTURE_VIDEO mode */
	dbg("Default setting : capture_video\n");
	dev->scenario_id = ISS_CAPTURE_VIDEO;
	fimc_is_hw_set_init(dev);
	fimc_is_mem_cache_clean((void *)dev->is_p_region, IS_PARAM_SIZE);
	clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state); /* BLOCK I/F Mode*/
	fimc_is_hw_set_param(dev);
	ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
	if (!ret) {
		err("wait timeout : init values (CAPTURE_VIDEO)\n");
		fimc_is_hw_set_low_poweroff(dev, true);
		return -EINVAL;
	}
	set_bit(IS_ST_INIT_DONE, &dev->state);
	dbg("Init sequence completed!! Ready to use\n");
#ifdef MSG_CONFIG_COTROL
	fimc_is_hw_set_debug_level(dev, FIMC_IS_DEBUG_MSG, FIMC_IS_DEBUG_LEVEL);
#endif
	return ret;
}

static int fimc_is_reset(struct v4l2_subdev *sd, u32 val)
{
	struct fimc_is_dev *is_dev = to_fimc_is_dev(sd);
	struct device *dev = &is_dev->pdev->dev;
	int ret = 0;

	dbg("fimc_is_reset\n");
	if (!val)
		return -EINVAL;
	dbg("hard reset start\n");
	/* Power off */
	fimc_is_hw_subip_poweroff(is_dev);
	ret = wait_event_timeout(is_dev->irq_queue1,
		test_bit(IS_PWR_SUB_IP_POWER_OFF, &is_dev->power), (HZ));
	fimc_is_hw_a5_power(is_dev, 0);
	dbg("A5 power off\n");
	fimc_is_hw_set_low_poweroff(is_dev, true);
	ret = pm_runtime_put_sync(dev);

	is_dev->sensor.id = 0;
	is_dev->p_region_index1 = 0;
	is_dev->p_region_index2 = 0;
	atomic_set(&is_dev->p_region_num, 0);
	is_dev->state = 0;
	set_bit(IS_ST_IDLE, &is_dev->state);
	is_dev->power = 0;
	is_dev->af.af_state = FIMC_IS_AF_IDLE;
	is_dev->af.mode = IS_FOCUS_MODE_IDLE;
	set_bit(IS_PWR_ST_POWEROFF, &is_dev->power);
	/* Restart */
#if defined(CONFIG_BUSFREQ_OPP) || defined(CONFIG_BUSFREQ_LOCK_WRAPPER)
	/* lock bus frequency */
	dev_lock(is_dev->bus_dev, dev, BUS_LOCK_FREQ_L0);
#endif
	fimc_is_hw_set_low_poweroff(is_dev, false);
	ret = pm_runtime_get_sync(dev);
	set_bit(IS_ST_A5_PWR_ON, &is_dev->state);
	/* Re- init */
	ret = fimc_is_init_set(sd, is_dev->sensor.sensor_type);
	return 0;
}

static int fimc_is_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret = 0;
	int i, max, tmp = 0;
	struct fimc_is_dev *dev = to_fimc_is_dev(sd);

	switch (ctrl->id) {
	/* EXIF information */
	case V4L2_CID_IS_CAMERA_EXIF_EXPTIME:
	case V4L2_CID_CAMERA_EXIF_EXPTIME: /* Exposure Time */
		fimc_is_mem_cache_inv((void *)IS_HEADER,
			(unsigned long)(sizeof(struct is_frame_header)*4));
		ctrl->value = dev->is_p_region->header[0].
			exif.exposure_time.den;
		break;
	case V4L2_CID_IS_CAMERA_EXIF_FLASH:
	case V4L2_CID_CAMERA_EXIF_FLASH: /* Flash */
		fimc_is_mem_cache_inv((void *)IS_HEADER,
			(unsigned long)(sizeof(struct is_frame_header)*4));
		ctrl->value = dev->is_p_region->header[0].exif.flash;
		break;
	case V4L2_CID_IS_CAMERA_EXIF_ISO:
	case V4L2_CID_CAMERA_EXIF_ISO: /* ISO Speed Rating */
		fimc_is_mem_cache_inv((void *)IS_HEADER,
			(unsigned long)(sizeof(struct is_frame_header)*4));
		ctrl->value = dev->is_p_region->header[0].
			exif.iso_speed_rating;
		break;
	case V4L2_CID_IS_CAMERA_EXIF_SHUTTERSPEED:
	case V4L2_CID_CAMERA_EXIF_TV: /* Shutter Speed */
		fimc_is_mem_cache_inv((void *)IS_HEADER,
			(unsigned long)(sizeof(struct is_frame_header)*4));
		/* Exposure time = shutter speed by FW */
		ctrl->value = dev->is_p_region->header[0].
			exif.exposure_time.den;
		break;
	case V4L2_CID_IS_CAMERA_EXIF_BRIGHTNESS:
	case V4L2_CID_CAMERA_EXIF_BV: /* Brightness */
		fimc_is_mem_cache_inv((void *)IS_HEADER,
			(unsigned long)(sizeof(struct is_frame_header)*4));
		ctrl->value = dev->is_p_region->header[0].exif.brightness.num;
		break;
	case V4L2_CID_CAMERA_EXIF_EBV: /* exposure bias */
		fimc_is_mem_cache_inv((void *)IS_HEADER,
			(unsigned long)(sizeof(struct is_frame_header)*4));
		ctrl->value = dev->is_p_region->header[0].exif.brightness.den;
		break;
	/* Get x and y offset of sensor  */
	case V4L2_CID_IS_GET_SENSOR_OFFSET_X:
		ctrl->value = dev->sensor.offset_x;
		break;
	case V4L2_CID_IS_GET_SENSOR_OFFSET_Y:
		ctrl->value = dev->sensor.offset_y;
		break;
	/* Get current sensor size  */
	case V4L2_CID_IS_GET_SENSOR_WIDTH:
		switch (dev->scenario_id) {
		case ISS_PREVIEW_STILL:
			ctrl->value = dev->sensor.width_prev;
			break;
		case ISS_PREVIEW_VIDEO:
			ctrl->value = dev->sensor.width_prev_cam;
			break;
		case ISS_CAPTURE_STILL:
			ctrl->value = dev->sensor.width_cap;
			break;
		case ISS_CAPTURE_VIDEO:
			ctrl->value = dev->sensor.width_cam;
			break;
		}
		break;
	case V4L2_CID_IS_GET_SENSOR_HEIGHT:
		switch (dev->scenario_id) {
		case ISS_PREVIEW_STILL:
			ctrl->value = dev->sensor.height_prev;
			break;
		case ISS_PREVIEW_VIDEO:
			ctrl->value = dev->sensor.height_prev_cam;
			break;
		case ISS_CAPTURE_STILL:
			ctrl->value = dev->sensor.height_cap;
			break;
		case ISS_CAPTURE_VIDEO:
			ctrl->value = dev->sensor.height_cam;
			break;
		}
		break;
	/* Get information related to frame management  */
	case V4L2_CID_IS_GET_FRAME_VALID:
		fimc_is_mem_cache_inv((void *)IS_HEADER,
			(unsigned long)(sizeof(struct is_frame_header)*4));
		if ((dev->scenario_id == ISS_PREVIEW_STILL) ||
			(dev->scenario_id == ISS_PREVIEW_VIDEO)) {
			ctrl->value = dev->is_p_region->header
			[dev->frame_count%MAX_FRAME_COUNT_PREVIEW].valid;
		} else {
			ctrl->value = dev->is_p_region->header[0].valid;
		}
		break;
	case V4L2_CID_IS_GET_FRAME_BADMARK:
		break;
	case V4L2_CID_IS_GET_FRAME_NUMBER:
		fimc_is_mem_cache_inv((void *)IS_HEADER,
			(unsigned long)(sizeof(struct is_frame_header)*4));
		if ((dev->scenario_id == ISS_PREVIEW_STILL) ||
			(dev->scenario_id == ISS_PREVIEW_VIDEO)) {
			ctrl->value =
				dev->is_p_region->header
				[dev->frame_count%MAX_FRAME_COUNT_PREVIEW].
				frame_number;
		} else {
			ctrl->value =
				dev->is_p_region->header[0].frame_number;
		}
		break;
	case V4L2_CID_IS_GET_LOSTED_FRAME_NUMBER:
		fimc_is_mem_cache_inv((void *)IS_HEADER,
			(unsigned long)(sizeof(struct is_frame_header)*4));
		if (dev->scenario_id == ISS_CAPTURE_STILL) {
			ctrl->value =
				dev->is_p_region->header[0].frame_number;
		} else if (dev->scenario_id == ISS_CAPTURE_VIDEO) {
			ctrl->value =
				dev->is_p_region->header[0].frame_number + 1;
		} else {
			max = dev->is_p_region->header[0].frame_number;
			for (i = 1; i < MAX_FRAME_COUNT_PREVIEW; i++) {
				if (max <
				dev->is_p_region->header[i].frame_number)
					max =
				dev->is_p_region->header[i].frame_number;
			}
			ctrl->value = max;
		}
		dev->frame_count = ctrl->value;
		break;
	case V4L2_CID_IS_GET_FRAME_CAPTURED:
		fimc_is_mem_cache_inv((void *)IS_HEADER,
			(unsigned long)(sizeof(struct is_frame_header)*4));
		ctrl->value =
			dev->is_p_region->header
			[dev->frame_count%MAX_FRAME_COUNT_PREVIEW].captured;
		break;
	case V4L2_CID_IS_FD_GET_DATA:
		ctrl->value = dev->fd_header.count;
		fimc_is_mem_cache_inv((void *)IS_FACE,
		(unsigned long)(sizeof(struct is_face_marker)*MAX_FACE_COUNT));
		memcpy((void *)dev->fd_header.target_addr,
			&dev->is_p_region->face[dev->fd_header.index],
			(sizeof(struct is_face_marker)*dev->fd_header.count));
		break;
	/* AF result */
	case V4L2_CID_CAMERA_AUTO_FOCUS_RESULT:
		if (!is_af_use(dev))
			ctrl->value = 0x02;
		else
			ctrl->value = dev->af.af_lock_state;
		break;
	case V4L2_CID_IS_ZOOM_STATE:
		if (test_bit(IS_ST_SET_ZOOM, &dev->state))
			ctrl->value = 1;
		else
			ctrl->value  = 0;
		break;
	case V4L2_CID_IS_ZOOM_MAX_LEVEL:
		switch (dev->scenario_id) {
		case ISS_PREVIEW_STILL:
			tmp = dev->sensor.width_prev;
			break;
		case ISS_PREVIEW_VIDEO:
			tmp = dev->sensor.width_prev_cam;
			break;
		case ISS_CAPTURE_STILL:
			tmp = dev->sensor.width_cap;
			break;
		case ISS_CAPTURE_VIDEO:
			tmp = dev->sensor.width_cam;
			break;
		}
		i = 0;
		while ((tmp - (16*i)) > (tmp/4) && (tmp - (16*i)) > 200)
			i++;
		ctrl->value = i;
		break;
	/* F/W debug region address */
	case V4L2_CID_IS_FW_DEBUG_REGION_ADDR:
		ctrl->value = dev->mem.base + FIMC_IS_DEBUG_REGION_ADDR;
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static int fimc_is_v4l2_digital_zoom(struct fimc_is_dev *dev, int zoom_factor)
{
	u32 ori_width = 0, ori_height = 0;
	u32 crop_offset_x = 0, crop_offset_y = 0;
	u32 crop_width = 0, crop_height = 0;
	u32 mode = 0;
	int tmp, ret = 0;

	clear_bit(IS_ST_SET_ZOOM, &dev->state);

	/* 1. Get current width and height */
	switch (dev->scenario_id) {
	case ISS_PREVIEW_STILL:
		mode = IS_MODE_PREVIEW_STILL;
		ori_width = dev->sensor.width_prev;
		ori_height = dev->sensor.height_prev;
		tmp = fimc_is_hw_get_sensor_max_framerate(dev);
		IS_SENSOR_SET_FRAME_RATE(dev, tmp);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		break;
	case ISS_PREVIEW_VIDEO:
		mode = IS_MODE_PREVIEW_VIDEO;
		ori_width = dev->sensor.width_prev_cam;
		ori_height = dev->sensor.height_prev_cam;
		break;
	case ISS_CAPTURE_STILL:
		mode = IS_MODE_CAPTURE_STILL;
		ori_width = dev->sensor.width_cap;
		ori_height = dev->sensor.height_cap;
		break;
	case ISS_CAPTURE_VIDEO:
		mode = IS_MODE_CAPTURE_VIDEO;
		ori_width = dev->sensor.width_cam;
		ori_height = dev->sensor.height_cam;
		break;
	}

	/* calculate the offset and size */
	if (!zoom_factor) {
		crop_offset_x = 0;
		crop_offset_y = 0;
		crop_width = 0;
		crop_height = 0;
		dev->sensor.zoom_out_width = ori_width;
		dev->sensor.zoom_out_height = ori_height;
	} else {
		crop_width = ori_width - (16 * zoom_factor);
		crop_height = (crop_width * ori_height) / ori_width;
		/* bayer crop contraint */
		switch (crop_height%4) {
		case 1:
			crop_height--;
			break;
		case 2:
			crop_height += 2;
			break;
		case 3:
			crop_height++;
			break;
		}
		if ((crop_height < (ori_height / 4)) ||
			(crop_width < (ori_width / 4))) {
			crop_width = ori_width/4;
			crop_height = ori_height/4;
		}
		crop_offset_x = (ori_width - crop_width)/2;
		crop_offset_y = (ori_height - crop_height)/2;
		dev->sensor.zoom_out_width = crop_width;
		dev->sensor.zoom_out_height = crop_height;
	}

	dbg("Zoom out offset = %d, %d\n", crop_offset_x, crop_offset_y);
	dbg("Zoom out = %d, %d\n", dev->sensor.zoom_out_width,
					dev->sensor.zoom_out_height);

	/* 2. stream off */
	clear_bit(IS_ST_STREAM_ON, &dev->state);
	fimc_is_hw_set_stream(dev, 0);
	ret = wait_event_timeout(dev->irq_queue1,
		test_bit(IS_ST_STREAM_OFF, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		dev_err(&dev->pdev->dev, "wait timeout : %s\n", __func__);
		return -EBUSY;
	}
	clear_bit(IS_ST_STREAM_OFF, &dev->state);

	/* 3. update input and output size of ISP,DRC and FD */
	IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, OTF_INPUT_COMMAND_ENABLE);
	IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, ori_width);
	IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, ori_height);
	IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev, crop_offset_x);
	IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev, crop_offset_y);
	IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev, crop_width);
	IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev, crop_height);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);
	IS_ISP_SET_PARAM_OTF_OUTPUT_CMD(dev, OTF_OUTPUT_COMMAND_ENABLE);
	IS_ISP_SET_PARAM_OTF_OUTPUT_WIDTH(dev, dev->sensor.zoom_out_width);
	IS_ISP_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, dev->sensor.zoom_out_height);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_OUTPUT);
	IS_INC_PARAM_NUM(dev);
	IS_ISP_SET_PARAM_DMA_OUTPUT1_WIDTH(dev, dev->sensor.zoom_out_width);
	IS_ISP_SET_PARAM_DMA_OUTPUT1_HEIGHT(dev, dev->sensor.zoom_out_height);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA1_OUTPUT);
	IS_INC_PARAM_NUM(dev);
	IS_ISP_SET_PARAM_DMA_OUTPUT2_WIDTH(dev, dev->sensor.zoom_out_width);
	IS_ISP_SET_PARAM_DMA_OUTPUT2_HEIGHT(dev, dev->sensor.zoom_out_height);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA2_OUTPUT);
	IS_INC_PARAM_NUM(dev);
	/* DRC input / output*/
	IS_DRC_SET_PARAM_OTF_INPUT_WIDTH(dev, dev->sensor.zoom_out_width);
	IS_DRC_SET_PARAM_OTF_INPUT_HEIGHT(dev, dev->sensor.zoom_out_height);
	IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);
	IS_DRC_SET_PARAM_OTF_OUTPUT_WIDTH(dev, dev->sensor.zoom_out_width);
	IS_DRC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, dev->sensor.zoom_out_height);
	IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_OUTPUT);
	IS_INC_PARAM_NUM(dev);
	/* FD input / output*/
	IS_FD_SET_PARAM_OTF_INPUT_WIDTH(dev, dev->sensor.zoom_out_width);
	IS_FD_SET_PARAM_OTF_INPUT_HEIGHT(dev, dev->sensor.zoom_out_height);
	IS_SET_PARAM_BIT(dev, PARAM_FD_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);
	/* 4. Set parameter */
	fimc_is_mem_cache_clean((void *)dev->is_p_region, IS_PARAM_SIZE);
	clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
	fimc_is_hw_set_param(dev);
	ret = wait_event_timeout(dev->irq_queue1,
		test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		dev_err(&dev->pdev->dev, "wait timeout : %s\n", __func__);
		return -EBUSY;
	}
	/* 5. Mode change for getting CAC margin */
	clear_bit(IS_ST_CHANGE_MODE, &dev->state);
	fimc_is_hw_change_mode(dev, mode);
	ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_CHANGE_MODE, &dev->state),
				FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		dev_err(&dev->pdev->dev,
			"Mode change timeout:%s\n", __func__);
		return -EBUSY;
	}
	set_bit(IS_ST_SET_ZOOM, &dev->state);
	return 0;
}

static int fimc_is_v4l2_isp_scene_mode(struct fimc_is_dev *dev, int mode)
{
	int ret = 0;
	switch (mode) {
	case SCENE_MODE_NONE:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
				ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_AUTO);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_ENABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	case SCENE_MODE_PORTRAIT:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, -1);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, -1);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_AUTO);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_ENABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	case SCENE_MODE_LANDSCAPE:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_MATRIX);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 1);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 1);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 1);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	case SCENE_MODE_SPORTS:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 400);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	case SCENE_MODE_PARTY_INDOOR:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 200);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 1);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_AUTO);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_ENABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	case SCENE_MODE_BEACH_SNOW:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 50);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 1);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 1);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	case SCENE_MODE_SUNSET:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev,
				ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	case SCENE_MODE_DUSK_DAWN:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
				ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev,
				ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_FLUORESCENT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	case SCENE_MODE_FALL_COLOR:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
				ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 2);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	case SCENE_MODE_NIGHTSHOT:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
				ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	case SCENE_MODE_BACK_LIGHT:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
				ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	/* FIXME add with SCENE_MODE_BACK_LIGHT (FLASH mode) */
	case SCENE_MODE_FIREWORKS:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 50);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
				ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	case SCENE_MODE_TEXT:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
			ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 2);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 2);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
		IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	case SCENE_MODE_CANDLE_LIGHT:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
				ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		break;
	default:
		break;
	}
	return ret;
}

int fimc_is_wait_af_done(struct fimc_is_dev *dev)
{
	unsigned long timeo = jiffies + 600; /* timeout of 3sec */

	while (time_before(jiffies, timeo)) {
		fimc_is_mem_cache_inv((void *)IS_SHARED,
			(unsigned long)(sizeof(struct is_share_region)));
		if (dev->is_shared_region->af_status) {
			dbg("AF done : %d ms\n",
				jiffies_to_msecs(jiffies - timeo + 600));
			return 0;
		}
		msleep(20);
	}
	err("AF wait time out: %d ms\n",
		jiffies_to_msecs(jiffies - timeo + 600));

	return 0;
}

int fimc_is_af_face(struct fimc_is_dev *dev)
{
	int ret = 0, max_confidence = 0, i = 0;
	int width, height;
	u32 touch_x = 0, touch_y = 0;

	for (i = dev->fd_header.index;
		i < (dev->fd_header.index + dev->fd_header.count); i++) {
		if (max_confidence < dev->is_p_region->face[i].confidence) {
			max_confidence = dev->is_p_region->face[i].confidence;
			touch_x = dev->is_p_region->face[i].face.offset_x +
				(dev->is_p_region->face[i].face.width / 2);
			touch_y = dev->is_p_region->face[i].face.offset_y +
				(dev->is_p_region->face[i].face.height / 2);
		}
	}
	width = fimc_is_hw_get_sensor_size_width(dev);
	height = fimc_is_hw_get_sensor_size_height(dev);
	touch_x = 1024 *  touch_x / (u32)width;
	touch_y = 1024 *  touch_y / (u32)height;

	if ((touch_x == 0) || (touch_y == 0) || (max_confidence < 50))
		return ret;

	if (dev->af.prev_pos_x == 0 && dev->af.prev_pos_y == 0) {
		dev->af.prev_pos_x = touch_x;
		dev->af.prev_pos_y = touch_y;
	} else {
		if (abs(dev->af.prev_pos_x - touch_x) < 100 &&
			abs(dev->af.prev_pos_y - touch_y) < 100) {
			return ret;
		}
		dbg("AF Face level = %d\n", max_confidence);
		dbg("AF Face = <%d, %d>\n", touch_x, touch_y);
		dbg("AF Face = prev <%d, %d>\n",
				dev->af.prev_pos_x, dev->af.prev_pos_y);
		dev->af.prev_pos_x = touch_x;
		dev->af.prev_pos_y = touch_y;
	}

	IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
	IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
	IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_TOUCH);
	IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
	IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
	IS_ISP_SET_PARAM_AA_TOUCH_X(dev, touch_x);
	IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, touch_y);
	IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
	IS_INC_PARAM_NUM(dev);
	dev->af.af_state = FIMC_IS_AF_SETCONFIG;
	fimc_is_mem_cache_clean((void *)dev->is_p_region, IS_PARAM_SIZE);
	fimc_is_hw_set_param(dev);

	return ret;
}

static int fimc_is_v4l2_af_mode(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case FOCUS_MODE_AUTO:
		dev->af.mode = IS_FOCUS_MODE_AUTO;
		break;
	case FOCUS_MODE_MACRO:
		dev->af.mode = IS_FOCUS_MODE_MACRO;
		break;
	case FOCUS_MODE_INFINITY:
		dev->af.mode = IS_FOCUS_MODE_INFINITY;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_MANUAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case FOCUS_MODE_CONTINOUS:
		dev->af.mode = IS_FOCUS_MODE_CONTINUOUS;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_CONTINUOUS);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_ENABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		dev->af.af_lock_state = 0;
		dev->af.ae_lock_state = 0;
		dev->af.awb_lock_state = 0;
		dev->af.prev_pos_x = 0;
		dev->af.prev_pos_y = 0;
		break;
	case FOCUS_MODE_TOUCH:
		dev->af.mode = IS_FOCUS_MODE_TOUCH;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_TOUCH);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, dev->af.pos_x);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, dev->af.pos_y);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		dev->af.af_lock_state = 0;
		dev->af.ae_lock_state = 0;
		dev->af.awb_lock_state = 0;
		break;
	case FOCUS_MODE_FACEDETECT:
		dev->af.mode = IS_FOCUS_MODE_FACEDETECT;
		dev->af.af_lock_state = 0;
		dev->af.ae_lock_state = 0;
		dev->af.awb_lock_state = 0;
		dev->af.prev_pos_x = 0;
		dev->af.prev_pos_y = 0;
		break;
	default:
		return ret;
	}
	return ret;
}

static int fimc_is_v4l2_af_start_stop(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case AUTO_FOCUS_OFF:
		if (!is_af_use(dev)) {
			/* 6A3 can't support AF */
			dev->af.af_state = FIMC_IS_AF_IDLE;
		} else {
			if (dev->af.af_state == FIMC_IS_AF_IDLE)
				return ret;
			/* Abort or lock AF */
			dev->af.af_state = FIMC_IS_AF_ABORT;
			IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_STOP);
			IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
			switch (dev->af.mode) {
			case IS_FOCUS_MODE_AUTO:
				IS_ISP_SET_PARAM_AA_MODE(dev,
					ISP_AF_MODE_SINGLE);
				IS_ISP_SET_PARAM_AA_SCENE(dev,
					ISP_AF_SCENE_NORMAL);
				IS_ISP_SET_PARAM_AA_SLEEP(dev,
					ISP_AF_SLEEP_OFF);
				IS_ISP_SET_PARAM_AA_FACE(dev,
					ISP_AF_FACE_DISABLE);
				IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
				IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
				IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
				IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
				IS_INC_PARAM_NUM(dev);
				fimc_is_mem_cache_clean(
					(void *)dev->is_p_region,
					IS_PARAM_SIZE);
				fimc_is_hw_set_param(dev);
					break;
			case IS_FOCUS_MODE_MACRO:
				IS_ISP_SET_PARAM_AA_MODE(dev,
					ISP_AF_MODE_SINGLE);
				IS_ISP_SET_PARAM_AA_SCENE(dev,
					ISP_AF_SCENE_MACRO);
				IS_ISP_SET_PARAM_AA_SLEEP(dev,
					ISP_AF_SLEEP_OFF);
				IS_ISP_SET_PARAM_AA_FACE(dev,
					ISP_AF_FACE_DISABLE);
				IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
				IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
				IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
				IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
				IS_INC_PARAM_NUM(dev);
				fimc_is_mem_cache_clean(
					(void *)dev->is_p_region,
					IS_PARAM_SIZE);
				fimc_is_hw_set_param(dev);
				ret = wait_event_timeout(dev->irq_queue1,
				(dev->af.af_state == FIMC_IS_AF_IDLE), HZ/5);
				if (!ret) {
					dev_err(&dev->pdev->dev,
					"Focus change timeout:%s\n", __func__);
					return -EBUSY;
				}
				break;
			case IS_FOCUS_MODE_CONTINUOUS:
				IS_ISP_SET_PARAM_AA_MODE(dev,
					ISP_AF_MODE_CONTINUOUS);
				IS_ISP_SET_PARAM_AA_SCENE(dev,
					ISP_AF_SCENE_NORMAL);
				IS_ISP_SET_PARAM_AA_SLEEP(dev,
						ISP_AF_SLEEP_OFF);
				IS_ISP_SET_PARAM_AA_FACE(dev,
					ISP_AF_FACE_DISABLE);
				IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
				IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
				IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
				IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
				IS_INC_PARAM_NUM(dev);
				fimc_is_mem_cache_clean(
					(void *)dev->is_p_region,
					IS_PARAM_SIZE);
				fimc_is_hw_set_param(dev);
				ret = wait_event_timeout(dev->irq_queue1,
				(dev->af.af_state == FIMC_IS_AF_IDLE), HZ/5);
				if (!ret) {
					dev_err(&dev->pdev->dev,
					"Focus change timeout:%s\n", __func__);
					return -EBUSY;
				}
				break;
			default:
				/* If other AF mode, there is no
				cancelation process*/
				break;
			}
			dev->af.mode = IS_FOCUS_MODE_IDLE;
		}
		break;
	case AUTO_FOCUS_ON:
		if (!is_af_use(dev)) {
			/* 6A3 can't support AF */
			dev->af.af_state = FIMC_IS_AF_LOCK;
			dev->af.af_lock_state = FIMC_IS_AF_LOCKED;
			dev->is_shared_region->af_status = 1;
			fimc_is_mem_cache_clean((void *)IS_SHARED,
			(unsigned long)(sizeof(struct is_share_region)));
		} else {
			dev->af.af_lock_state = 0;
			dev->af.ae_lock_state = 0;
			dev->af.awb_lock_state = 0;
			dev->is_shared_region->af_status = 0;
			fimc_is_mem_cache_clean((void *)IS_SHARED,
			(unsigned long)(sizeof(struct is_share_region)));
			IS_ISP_SET_PARAM_AA_CMD(dev,
			ISP_AA_COMMAND_START);
			IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
			IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
			IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
			IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
			IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
			IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
			IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
			switch (dev->af.mode) {
			case IS_FOCUS_MODE_AUTO:
				if (dev->af.mode == IS_FOCUS_MODE_FACEDETECT)
					IS_ISP_SET_PARAM_AA_FACE(dev,
							ISP_AF_FACE_ENABLE);
				IS_ISP_SET_PARAM_AA_SCENE(dev,
					ISP_AF_SCENE_NORMAL);
				IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
				IS_INC_PARAM_NUM(dev);
				dev->af.af_state =
						FIMC_IS_AF_SETCONFIG;
				fimc_is_mem_cache_clean(
					(void *)dev->is_p_region,
					IS_PARAM_SIZE);
				fimc_is_hw_set_param(dev);
				ret = wait_event_timeout(dev->irq_queue1,
				(dev->af.af_state == FIMC_IS_AF_RUNNING), HZ/5);
				if (!ret) {
					dev_err(&dev->pdev->dev,
					"Focus change timeout:%s\n", __func__);
					return -EBUSY;
				}
				break;
			case IS_FOCUS_MODE_MACRO:
				IS_ISP_SET_PARAM_AA_SCENE(dev,
					ISP_AF_SCENE_MACRO);
				IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
				IS_INC_PARAM_NUM(dev);
				dev->af.af_state =
						FIMC_IS_AF_SETCONFIG;
				fimc_is_mem_cache_clean(
					(void *)dev->is_p_region,
					IS_PARAM_SIZE);
				fimc_is_hw_set_param(dev);
				ret = wait_event_timeout(dev->irq_queue1,
				(dev->af.af_state == FIMC_IS_AF_RUNNING), HZ/5);
				if (!ret) {
					dev_err(&dev->pdev->dev,
					"Focus change timeout:%s\n", __func__);
					return -EBUSY;
				}
				break;
			default:
				break;
			}
		}
		break;
	default:
		break;
	}
	return ret;
}

static int fimc_is_v4l2_isp_iso(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case ISO_AUTO:
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		break;
	case ISO_100:
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 100);
		break;
	case ISO_200:
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 200);
		break;
	case ISO_400:
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 400);
		break;
	case ISO_800:
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 800);
		break;
	case ISO_1600:
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 1600);
		break;
	default:
		return ret;
	}
	if (value >= ISO_AUTO && value < ISO_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
	}
	return ret;
}

static int fimc_is_v4l2_isp_effect(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_IMAGE_EFFECT_DISABLE:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev, ISP_IMAGE_EFFECT_DISABLE);
		break;
	case IS_IMAGE_EFFECT_MONOCHROME:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev, ISP_IMAGE_EFFECT_MONOCHROME);
		break;
	case IS_IMAGE_EFFECT_NEGATIVE_MONO:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev,
			ISP_IMAGE_EFFECT_NEGATIVE_MONO);
		break;
	case IS_IMAGE_EFFECT_NEGATIVE_COLOR:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev,
			ISP_IMAGE_EFFECT_NEGATIVE_COLOR);
		break;
	case IS_IMAGE_EFFECT_SEPIA:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev, ISP_IMAGE_EFFECT_SEPIA);
		break;
	}
	/* only ISP effect in Pegasus */
	if (value >= 0 && value < 5) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_IMAGE_EFFECT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
	}
	return ret;
}

static int fimc_is_v4l2_isp_effect_legacy(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IMAGE_EFFECT_NONE:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev, ISP_IMAGE_EFFECT_DISABLE);
		break;
	case IMAGE_EFFECT_BNW:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev, ISP_IMAGE_EFFECT_MONOCHROME);
		break;
	case IMAGE_EFFECT_NEGATIVE:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev,
			ISP_IMAGE_EFFECT_NEGATIVE_COLOR);
		break;
	case IMAGE_EFFECT_SEPIA:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev, ISP_IMAGE_EFFECT_SEPIA);
		break;
	default:
		return ret;
	}
	/* only ISP effect in Pegasus */
	if (value > IMAGE_EFFECT_BASE && value < IMAGE_EFFECT_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_IMAGE_EFFECT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return 0;
		}
	}
	return ret;
}

static int fimc_is_v4l2_isp_flash_mode(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case FLASH_MODE_OFF:
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		break;
	case FLASH_MODE_AUTO:
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_AUTO);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_ENABLE);
		break;
	case FLASH_MODE_ON:
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_MANUALON);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		break;
	case FLASH_MODE_TORCH:
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_TORCH);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		break;
	default:
		return ret;
	}
	if (value > FLASH_MODE_BASE && value < FLASH_MODE_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

static int fimc_is_v4l2_awb_mode(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_AWB_AUTO:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev, 0);
		break;
	case IS_AWB_DAYLIGHT:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		break;
	case IS_AWB_CLOUDY:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_CLOUDY);
		break;
	case IS_AWB_TUNGSTEN:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_TUNGSTEN);
		break;
	case IS_AWB_FLUORESCENT:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_FLUORESCENT);
		break;
	}
	if (value >= IS_AWB_AUTO && value < IS_AWB_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
	}
	return ret;
}

static int fimc_is_v4l2_awb_mode_legacy(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case WHITE_BALANCE_AUTO:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev, 0);
		break;
	case WHITE_BALANCE_SUNNY:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		break;
	case WHITE_BALANCE_CLOUDY:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_CLOUDY);
		break;
	case WHITE_BALANCE_TUNGSTEN:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_TUNGSTEN);
		break;
	case WHITE_BALANCE_FLUORESCENT:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_FLUORESCENT);
		break;
	}
	if (value > WHITE_BALANCE_BASE && value < WHITE_BALANCE_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return 0;
		}
	}
	return ret;
}

static int fimc_is_v4l2_isp_contrast(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_CONTRAST_AUTO:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		break;
	case IS_CONTRAST_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, -2);
		break;
	case IS_CONTRAST_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, -1);
		break;
	case IS_CONTRAST_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		break;
	case IS_CONTRAST_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 1);
		break;
	case IS_CONTRAST_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 2);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_CONTRAST_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
	}
	return ret;
}

static int fimc_is_v4l2_isp_contrast_legacy(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case CONTRAST_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, -2);
		break;
	case CONTRAST_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, -1);
		break;
	case CONTRAST_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		break;
	case CONTRAST_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 1);
		break;
	case CONTRAST_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 2);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < CONTRAST_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

static int fimc_is_v4l2_isp_saturation(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case SATURATION_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SATURATION);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, -2);
		break;
	case SATURATION_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SATURATION);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, -1);
		break;
	case SATURATION_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SATURATION);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		break;
	case SATURATION_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SATURATION);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 1);
		break;
	case SATURATION_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SATURATION);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 2);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < SATURATION_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
	}
	return ret;
}

static int fimc_is_v4l2_isp_sharpness(struct fimc_is_dev *dev, int value)
{
	int ret = 0;

	switch (value) {
	case SHARPNESS_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SHARPNESS);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, -2);
		break;
	case SHARPNESS_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SHARPNESS);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, -1);
		break;
	case SHARPNESS_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SHARPNESS);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		break;
	case SHARPNESS_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SHARPNESS);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 1);
		break;
	case SHARPNESS_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SHARPNESS);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 2);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < SHARPNESS_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
	}
	return ret;
}

static int fimc_is_v4l2_isp_exposure(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_EXPOSURE_MINUS_4:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, -4);
		break;
	case IS_EXPOSURE_MINUS_3:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, -3);
		break;
	case IS_EXPOSURE_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, -2);
		break;
	case IS_EXPOSURE_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, -1);
		break;
	case IS_EXPOSURE_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		break;
	case IS_EXPOSURE_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 1);
		break;
	case IS_EXPOSURE_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 2);
		break;
	case IS_EXPOSURE_PLUS_3:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 3);
		break;
	case IS_EXPOSURE_PLUS_4:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 4);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_EXPOSURE_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
	}
	return ret;
}

static int fimc_is_v4l2_isp_exposure_legacy(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	if (value >= -4 && value < 5) {
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

static int fimc_is_v4l2_isp_brightness(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_BRIGHTNESS_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, -2);
		break;
	case IS_BRIGHTNESS_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, -1);
		break;
	case IS_BRIGHTNESS_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		break;
	case IS_BRIGHTNESS_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 1);
		break;
	case IS_BRIGHTNESS_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 2);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_BRIGHTNESS_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
	}
	return ret;
}

static int fimc_is_v4l2_isp_hue(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_HUE_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_HUE);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, -2);
		break;
	case IS_HUE_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_HUE);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, -1);
		break;
	case IS_HUE_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_HUE);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		break;
	case IS_HUE_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_HUE);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 1);
		break;
	case IS_HUE_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_HUE);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 2);
		break;
	default:
		return ret;
	}
	if (value >= IS_HUE_MINUS_2 && value < IS_HUE_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
	}
	return ret;
}

static int fimc_is_v4l2_isp_metering(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_METERING_AVERAGE:
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			ISP_METERING_COMMAND_AVERAGE);
		break;
	case IS_METERING_SPOT:
		IS_ISP_SET_PARAM_METERING_CMD(dev, ISP_METERING_COMMAND_SPOT);
		break;
	case IS_METERING_MATRIX:
		IS_ISP_SET_PARAM_METERING_CMD(dev, ISP_METERING_COMMAND_MATRIX);
		break;
	case IS_METERING_CENTER:
		IS_ISP_SET_PARAM_METERING_CMD(dev, ISP_METERING_COMMAND_CENTER);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_METERING_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
	}
	return ret;
}

static int fimc_is_v4l2_isp_metering_legacy(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case METERING_CENTER:
		IS_ISP_SET_PARAM_METERING_CMD(dev, ISP_METERING_COMMAND_CENTER);
		break;
	case METERING_SPOT:
		IS_ISP_SET_PARAM_METERING_CMD(dev, ISP_METERING_COMMAND_SPOT);
		break;
	case METERING_MATRIX:
		IS_ISP_SET_PARAM_METERING_CMD(dev, ISP_METERING_COMMAND_MATRIX);
		break;
	default:
		return ret;
	}
	if (value > METERING_BASE && value < METERING_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

static int fimc_is_v4l2_isp_afc(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_AFC_DISABLE:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, 0);
		break;
	case IS_AFC_AUTO:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, 0);
		break;
	case IS_AFC_MANUAL_50HZ:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, ISP_AFC_MANUAL_50HZ);
		break;
	case IS_AFC_MANUAL_60HZ:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, ISP_AFC_MANUAL_60HZ);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_AFC_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AFC);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
	}
	return ret;
}

static int fimc_is_v4l2_isp_afc_legacy(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case ANTI_BANDING_OFF:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, 0);
		break;
	case ANTI_BANDING_AUTO:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, 0);
		break;
	case ANTI_BANDING_50HZ:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, ISP_AFC_MANUAL_50HZ);
		break;
	case ANTI_BANDING_60HZ:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, ISP_AFC_MANUAL_60HZ);
		break;
	default:
		return ret;
	}
	if (value >= ANTI_BANDING_OFF && value <= ANTI_BANDING_60HZ) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AFC);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

static int fimc_is_v4l2_fd_angle_mode(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_FD_ROLL_ANGLE_BASIC:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_ROLL_ANGLE);
		IS_FD_SET_PARAM_FD_CONFIG_ROLL_ANGLE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_YAW_ANGLE:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_YAW_ANGLE);
		IS_FD_SET_PARAM_FD_CONFIG_YAW_ANGLE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_SMILE_MODE:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_SMILE_MODE);
		IS_FD_SET_PARAM_FD_CONFIG_SMILE_MODE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_BLINK_MODE:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_BLINK_MODE);
		IS_FD_SET_PARAM_FD_CONFIG_BLINK_MODE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_EYE_DETECT_MODE:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_EYES_DETECT);
		IS_FD_SET_PARAM_FD_CONFIG_EYE_DETECT(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_MOUTH_DETECT_MODE:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_MOUTH_DETECT);
		IS_FD_SET_PARAM_FD_CONFIG_MOUTH_DETECT(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_ORIENTATION_MODE:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_ORIENTATION);
		IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_ORIENTATION:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_ORIENTATION_VALUE);
		IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION_VALUE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	default:
		break;
	}
	return ret;
}

static int fimc_is_v4l2_frame_rate(struct fimc_is_dev *dev, int value)
{
	int i, ret = 0;
	int width, height, format;

	width = fimc_is_hw_get_sensor_size_width(dev);
	height = fimc_is_hw_get_sensor_size_height(dev);
	format = fimc_is_hw_get_sensor_format(dev);
	dev->sensor.framerate_update = true;

	switch (value) {
	case FRAME_RATE_AUTO: /* FRAME_RATE_AUTO */
		i = fimc_is_hw_get_sensor_max_framerate(dev);
		IS_SENSOR_SET_FRAME_RATE(dev, i);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, OTF_INPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, height);
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev, format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
						OTF_INPUT_BIT_WIDTH_10BIT);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
						OTF_INPUT_ORDER_BAYER_GR_BG);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev, 66666);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev,
						CONTROL_COMMAND_START);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		break;
	case FRAME_RATE_7: /* FRAME_RATE_7 */
		IS_SENSOR_SET_FRAME_RATE(dev, 7);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, OTF_INPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, height);
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev, format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
						OTF_INPUT_BIT_WIDTH_10BIT);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
						OTF_INPUT_ORDER_BAYER_GR_BG);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev, 124950);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev,
						CONTROL_COMMAND_START);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		break;
	case FRAME_RATE_15: /* FRAME_RATE_15 */
		IS_SENSOR_SET_FRAME_RATE(dev, 15);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, OTF_INPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, height);
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev, format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
						OTF_INPUT_BIT_WIDTH_10BIT);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
						OTF_INPUT_ORDER_BAYER_GR_BG);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev, 66666);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev,
						CONTROL_COMMAND_START);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		break;
	case FRAME_RATE_20: /* FRAME_RATE_20 */
		IS_SENSOR_SET_FRAME_RATE(dev, 20);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, OTF_INPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, height);
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev, format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
						OTF_INPUT_BIT_WIDTH_10BIT);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
						OTF_INPUT_ORDER_BAYER_GR_BG);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev, 50000);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev,
						CONTROL_COMMAND_START);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		break;
	case FRAME_RATE_30: /* FRAME_RATE_30 */
		IS_SENSOR_SET_FRAME_RATE(dev, 30);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, OTF_INPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, height);
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev, format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
						OTF_INPUT_BIT_WIDTH_10BIT);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
						OTF_INPUT_ORDER_BAYER_GR_BG);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev, 33333);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev,
						CONTROL_COMMAND_START);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		break;
	case FRAME_RATE_60: /* FRAME_RATE_60 */
		IS_SENSOR_SET_FRAME_RATE(dev, 60);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, OTF_INPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, height);
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev, format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
						OTF_INPUT_BIT_WIDTH_10BIT);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
						OTF_INPUT_ORDER_BAYER_GR_BG);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev, 16666);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev,
						CONTROL_COMMAND_START);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		break;
	default:
		IS_SENSOR_SET_FRAME_RATE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, OTF_INPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, height);
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev, format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
						OTF_INPUT_BIT_WIDTH_10BIT);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
						OTF_INPUT_ORDER_BAYER_GR_BG);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev,
							(u32)(1000000/value));
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev,
						CONTROL_COMMAND_START);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		break;
	}

	return ret;
}

static int fimc_is_v4l2_ae_awb_lockunlock(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case AE_UNLOCK_AWB_UNLOCK:
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AE |
						ISP_AA_TARGET_AWB);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case AE_LOCK_AWB_UNLOCK:
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_STOP);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AE);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AWB);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case AE_UNLOCK_AWB_LOCK:
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AE);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_STOP);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AWB);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case AE_LOCK_AWB_LOCK:
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_STOP);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AE |
						ISP_AA_TARGET_AWB);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	default:
		break;
	}
	return ret;
}

static int fimc_is_v4l2_set_isp(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_ISP_BYPASS_DISABLE:
		IS_ISP_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_DISABLE);
		break;
	case IS_ISP_BYPASS_ENABLE:
		IS_ISP_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_ENABLE);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_ISP_BYPASS_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

static int fimc_is_v4l2_set_drc(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_DRC_BYPASS_DISABLE:
		IS_DRC_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_DISABLE);
		IS_DRC_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_START);
		break;
	case IS_DRC_BYPASS_ENABLE:
		IS_DRC_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_ENABLE);
		IS_DRC_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_START);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_DRC_BYPASS_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_DRC_CONTROL);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

static int fimc_is_v4l2_cmd_isp(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_ISP_COMMAND_STOP:
		IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
		break;
	case IS_ISP_COMMAND_START:
		IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_START);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_ISP_COMMAND_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

static int fimc_is_v4l2_cmd_drc(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_DRC_COMMAND_STOP:
		IS_DRC_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
		break;
	case IS_DRC_COMMAND_START:
		IS_DRC_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_START);
		break;
	}
	if (value >= 0 && value < IS_ISP_COMMAND_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_DRC_CONTROL);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

static int fimc_is_v4l2_cmd_fd(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_FD_COMMAND_STOP:
		dbg("IS_FD_COMMAND_STOP\n");
		IS_FD_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
		break;
	case IS_FD_COMMAND_START:
		dbg("IS_FD_COMMAND_START\n");
		IS_FD_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_START);
		break;
	}
	if (value >= 0 && value < IS_ISP_COMMAND_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONTROL);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

static int fimc_is_v4l2_shot_mode(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	IS_SET_PARAM_GLOBAL_SHOTMODE_CMD(dev, value);
	IS_SET_PARAM_BIT(dev, PARAM_GLOBAL_SHOTMODE);
	IS_INC_PARAM_NUM(dev);
	fimc_is_mem_cache_clean((void *)dev->is_p_region, IS_PARAM_SIZE);
	fimc_is_hw_set_param(dev);
	return ret;
}

static int fimc_is_v4l2_mode_change(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	if (!test_bit(IS_ST_INIT_DONE, &dev->state)) {
		err("Not init done state!!\n");
		return -EINVAL;
	}
	clear_bit(IS_ST_CHANGE_MODE, &dev->state);
	fimc_is_hw_change_mode(dev, value);
	ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_CHANGE_MODE, &dev->state),
						FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		err("Mode change timeout !!\n");
		fimc_is_hw_set_low_poweroff(dev, true);
		return -EINVAL;
	}
	printk(KERN_INFO "CAC margin - %d, %d\n", dev->sensor.offset_x,
							dev->sensor.offset_y);
	return ret;
}

static int fimc_is_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret = 0;
	int i;
	struct fimc_is_dev *dev = to_fimc_is_dev(sd);

	switch (ctrl->id) {
	case V4L2_CID_IS_S_SCENARIO_MODE:
		ret = fimc_is_v4l2_mode_change(dev, ctrl->value);
		break;
	case V4L2_CID_IS_S_FORMAT_SCENARIO:
		/* Set default value between still and video mode change */
		/* This is optional part */
		if ((dev->scenario_id + ctrl->value) == 1) {
			IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
			IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev, 0);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
			IS_INC_PARAM_NUM(dev);
			IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
			IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		switch (ctrl->value) {
		case IS_MODE_PREVIEW_STILL:
			dev->scenario_id = ISS_PREVIEW_STILL;
			break;
		case IS_MODE_PREVIEW_VIDEO:
			dev->scenario_id = ISS_PREVIEW_VIDEO;
			break;
		case IS_MODE_CAPTURE_STILL:
			dev->scenario_id = ISS_CAPTURE_STILL;
			break;
		case IS_MODE_CAPTURE_VIDEO:
			dev->scenario_id = ISS_CAPTURE_VIDEO;
			break;
		default:
			return -EBUSY;
		}
		break;
	case V4L2_CID_IS_CAMERA_SHOT_MODE_NORMAL:
		ret = fimc_is_v4l2_shot_mode(dev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_FRAME_RATE:
		ret = fimc_is_v4l2_frame_rate(dev, ctrl->value);
		break;
	/* Focus */
	case V4L2_CID_IS_CAMERA_OBJECT_POSITION_X:
	case V4L2_CID_CAMERA_OBJECT_POSITION_X:
		dev->af.pos_x = ctrl->value;
		break;
	case V4L2_CID_IS_CAMERA_OBJECT_POSITION_Y:
	case V4L2_CID_CAMERA_OBJECT_POSITION_Y:
		dev->af.pos_y = ctrl->value;
		break;
	case V4L2_CID_CAMERA_FOCUS_MODE:
		ret = fimc_is_v4l2_af_mode(dev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_SET_AUTO_FOCUS:
		ret = fimc_is_v4l2_af_start_stop(dev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_TOUCH_AF_START_STOP:
		switch (ctrl->value) {
		case TOUCH_AF_STOP:
			break;
		case TOUCH_AF_START:
			break;
		default:
			break;
		}
		break;
	case V4L2_CID_CAMERA_CAF_START_STOP:
		switch (ctrl->value) {
		case CAF_STOP:
			break;
		case CAF_START:
			break;
		default:
			break;
		}
		break;
	/* AWB, AE Lock/Unlock */
	case V4L2_CID_CAMERA_AEAWB_LOCK_UNLOCK:
		ret = fimc_is_v4l2_ae_awb_lockunlock(dev, ctrl->value);
		break;
	/* FLASH */
	case V4L2_CID_CAMERA_FLASH_MODE:
		ret = fimc_is_v4l2_isp_flash_mode(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_AWB_MODE:
		ret = fimc_is_v4l2_awb_mode(dev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_WHITE_BALANCE:
		ret = fimc_is_v4l2_awb_mode_legacy(dev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_EFFECT:
		ret = fimc_is_v4l2_isp_effect_legacy(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_IMAGE_EFFECT:
		ret = fimc_is_v4l2_isp_effect(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_ISO:
	case V4L2_CID_CAMERA_ISO:
		ret = fimc_is_v4l2_isp_iso(dev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_CONTRAST:
		ret = fimc_is_v4l2_isp_contrast_legacy(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_CONTRAST:
		ret = fimc_is_v4l2_isp_contrast(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_SATURATION:
	case V4L2_CID_CAMERA_SATURATION:
		ret = fimc_is_v4l2_isp_saturation(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_SHARPNESS:
	case V4L2_CID_CAMERA_SHARPNESS:
		ret = fimc_is_v4l2_isp_sharpness(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_EXPOSURE:
		ret = fimc_is_v4l2_isp_exposure(dev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_BRIGHTNESS:
		ret = fimc_is_v4l2_isp_exposure_legacy(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_BRIGHTNESS:
		ret = fimc_is_v4l2_isp_brightness(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_HUE:
		ret = fimc_is_v4l2_isp_hue(dev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_METERING:
		ret = fimc_is_v4l2_isp_metering_legacy(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_METERING:
		ret = fimc_is_v4l2_isp_metering(dev, ctrl->value);
		break;
	/* Ony valid at SPOT Mode */
	case V4L2_CID_IS_CAMERA_METERING_POSITION_X:
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_METERING_POSITION_Y:
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_METERING_WINDOW_X:
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_METERING_WINDOW_Y:
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_ANTI_BANDING:
		ret = fimc_is_v4l2_isp_afc_legacy(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_AFC_MODE:
		ret = fimc_is_v4l2_isp_afc(dev, ctrl->value);
		break;
	case V4L2_CID_IS_FD_SET_MAX_FACE_NUMBER:
		if (ctrl->value >= 0) {
			IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
				FD_CONFIG_COMMAND_MAXIMUM_NUMBER);
			IS_FD_SET_PARAM_FD_CONFIG_MAX_NUMBER(dev, ctrl->value);
			IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
				IS_PARAM_SIZE);
			fimc_is_hw_set_param(dev);
		}
		break;
	case V4L2_CID_IS_FD_SET_ROLL_ANGLE:
		ret = fimc_is_v4l2_fd_angle_mode(dev, ctrl->value);
		break;
	case V4L2_CID_IS_FD_SET_DATA_ADDRESS:
		dev->fd_header.target_addr = ctrl->value;
		break;
	case V4L2_CID_IS_SET_ISP:
		ret = fimc_is_v4l2_set_isp(dev, ctrl->value);
		break;
	case V4L2_CID_IS_SET_DRC:
		ret = fimc_is_v4l2_set_drc(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CMD_ISP:
		ret = fimc_is_v4l2_cmd_isp(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CMD_DRC:
		ret = fimc_is_v4l2_cmd_drc(dev, ctrl->value);
		break;
	case V4L2_CID_IS_CMD_FD:
		ret = fimc_is_v4l2_cmd_fd(dev, ctrl->value);
		break;
	case V4L2_CID_IS_SET_FRAME_NUMBER:
		dev->frame_count = ctrl->value + 1;
		dev->is_p_region->header[0].valid = 0;
		dev->is_p_region->header[1].valid = 0;
		dev->is_p_region->header[2].valid = 0;
		dev->is_p_region->header[3].valid = 0;
		fimc_is_mem_cache_clean((void *)IS_HEADER, IS_PARAM_SIZE);
		break;
	case V4L2_CID_IS_SET_FRAME_VALID:
		if ((dev->scenario_id == ISS_CAPTURE_STILL)
			|| (dev->scenario_id == ISS_CAPTURE_VIDEO)) {
			dev->is_p_region->header[0].valid = ctrl->value;
			dev->is_p_region->header[0].bad_mark = ctrl->value;
			dev->is_p_region->header[0].captured = ctrl->value;
		} else {
			dev->is_p_region->header[dev->frame_count%
				MAX_FRAME_COUNT_PREVIEW].valid = ctrl->value;
			dev->is_p_region->header[dev->frame_count%
			MAX_FRAME_COUNT_PREVIEW].bad_mark = ctrl->value;
			dev->is_p_region->header[dev->frame_count%
			MAX_FRAME_COUNT_PREVIEW].captured = ctrl->value;
		}
		dev->frame_count++;
		fimc_is_mem_cache_clean((void *)IS_HEADER, IS_PARAM_SIZE);
		break;
	case V4L2_CID_IS_SET_FRAME_BADMARK:
		break;
	case V4L2_CID_IS_SET_FRAME_CAPTURED:
		break;
	case V4L2_CID_IS_CLEAR_FRAME_NUMBER:
		if (dev->scenario_id == ISS_CAPTURE_STILL) {
			dev->is_p_region->header[0].valid = 0;
			dev->is_p_region->header[0].bad_mark = 0;
			dev->is_p_region->header[0].captured = 0;
		} else if (dev->scenario_id == ISS_CAPTURE_VIDEO) {
			dev->is_p_region->header[0].valid = 0;
			dev->is_p_region->header[0].bad_mark = 0;
			dev->is_p_region->header[0].captured = 0;
		} else {
			for (i = 0; i < MAX_FRAME_COUNT_PREVIEW; i++) {
				if (dev->is_p_region->header[i].frame_number <
					dev->frame_count) {
					dev->is_p_region->header[i].valid = 0;
					dev->is_p_region->header[i].
						bad_mark = 0;
					dev->is_p_region->header[i].
						captured = 0;
				}
			}
		}
		fimc_is_mem_cache_clean((void *)IS_HEADER, IS_PARAM_SIZE);
		break;
	case V4L2_CID_CAMERA_SCENE_MODE:
		ret = fimc_is_v4l2_isp_scene_mode(dev, ctrl->value);
		break;
	case V4L2_CID_IS_ZOOM:
		ret = fimc_is_v4l2_digital_zoom(dev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_VT_MODE:
		dev->setfile.sub_index = ctrl->value;
		printk(KERN_INFO "VT mode(%d) is selected\n",
						dev->setfile.sub_index);
		break;
	case V4L2_CID_CAMERA_VGA_BLUR:
		break;
	default:
		dbg("Invalid control\n");
		return -EINVAL;
	}

	return ret;
}

static int fimc_is_g_ext_ctrls_handler(struct fimc_is_dev *dev,
	struct v4l2_ext_control *ctrl, int index)
{
	int ret = 0;
	u32 tmp = 0;
	switch (ctrl->id) {
	/* Face Detection CID handler */
	/* 1. Overall information */
	case V4L2_CID_IS_FD_GET_FACE_COUNT:
		ctrl->value = dev->fd_header.count;
		break;
	case V4L2_CID_IS_FD_GET_FACE_FRAME_NUMBER:
		if (dev->fd_header.offset < dev->fd_header.count) {
			ctrl->value =
				dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].frame_number;
		} else {
			ctrl->value = 0;
			return -255;
		}
		break;
	case V4L2_CID_IS_FD_GET_FACE_CONFIDENCE:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
					+ dev->fd_header.offset].confidence;
		break;
	case V4L2_CID_IS_FD_GET_FACE_SMILE_LEVEL:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
					+ dev->fd_header.offset].smile_level;
		break;
	case V4L2_CID_IS_FD_GET_FACE_BLINK_LEVEL:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
					+ dev->fd_header.offset].blink_level;
		break;
	/* 2. Face information */
	case V4L2_CID_IS_FD_GET_FACE_TOPLEFT_X:
		tmp = dev->is_p_region->face[dev->fd_header.index
					+ dev->fd_header.offset].face.offset_x;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.width;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	case V4L2_CID_IS_FD_GET_FACE_TOPLEFT_Y:
		tmp = dev->is_p_region->face[dev->fd_header.index
					+ dev->fd_header.offset].face.offset_y;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.height;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	case V4L2_CID_IS_FD_GET_FACE_BOTTOMRIGHT_X:
		tmp = dev->is_p_region->face[dev->fd_header.index
					+ dev->fd_header.offset].face.offset_x
			+ dev->is_p_region->face[dev->fd_header.index
					+ dev->fd_header.offset].face.width;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.width;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	case V4L2_CID_IS_FD_GET_FACE_BOTTOMRIGHT_Y:
		tmp = dev->is_p_region->face[dev->fd_header.index
					+ dev->fd_header.offset].face.offset_y
			+ dev->is_p_region->face[dev->fd_header.index
					+ dev->fd_header.offset].face.height;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.height;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	/* 3. Left eye information */
	case V4L2_CID_IS_FD_GET_LEFT_EYE_TOPLEFT_X:
		tmp = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].left_eye.offset_x;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.width;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	case V4L2_CID_IS_FD_GET_LEFT_EYE_TOPLEFT_Y:
		tmp = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].left_eye.offset_y;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.height;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	case V4L2_CID_IS_FD_GET_LEFT_EYE_BOTTOMRIGHT_X:
		tmp = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].left_eye.offset_x
			+ dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].left_eye.width;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.width;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	case V4L2_CID_IS_FD_GET_LEFT_EYE_BOTTOMRIGHT_Y:
		tmp = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].left_eye.offset_y
			+ dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].left_eye.height;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.height;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	/* 4. Right eye information */
	case V4L2_CID_IS_FD_GET_RIGHT_EYE_TOPLEFT_X:
		tmp = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].right_eye.offset_x;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.width;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	case V4L2_CID_IS_FD_GET_RIGHT_EYE_TOPLEFT_Y:
		tmp = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].right_eye.offset_y;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.height;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	case V4L2_CID_IS_FD_GET_RIGHT_EYE_BOTTOMRIGHT_X:
		tmp = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].right_eye.offset_x
			+ dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].right_eye.width;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.width;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	case V4L2_CID_IS_FD_GET_RIGHT_EYE_BOTTOMRIGHT_Y:
		tmp = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].right_eye.offset_y
			+ dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].right_eye.height;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.height;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	/* 5. Mouth eye information */
	case V4L2_CID_IS_FD_GET_MOUTH_TOPLEFT_X:
		tmp = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].mouth.offset_x;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.width;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	case V4L2_CID_IS_FD_GET_MOUTH_TOPLEFT_Y:
		tmp = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].mouth.offset_y;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.height;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	case V4L2_CID_IS_FD_GET_MOUTH_BOTTOMRIGHT_X:
		tmp = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].mouth.offset_x
			+ dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].mouth.width;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.width;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	case V4L2_CID_IS_FD_GET_MOUTH_BOTTOMRIGHT_Y:
		tmp = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].mouth.offset_y
			+ dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].mouth.height;
		tmp = (tmp * 2 * GED_FD_RANGE) /  dev->fd_header.height;
		ctrl->value = (s32)tmp - GED_FD_RANGE;
		break;
	/* 6. Angle information */
	case V4L2_CID_IS_FD_GET_ANGLE:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].roll_angle;
		break;
	case V4L2_CID_IS_FD_GET_YAW_ANGLE:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].yaw_angle;
		break;
	/* 7. Update next face information */
	case V4L2_CID_IS_FD_GET_NEXT:
		dev->fd_header.offset++;
		break;
	case V4L2_CID_CAM_SENSOR_FW_VER:
		strcpy(ctrl->string, dev->fw.fw_version);
		break;
	default:
		return 255;
		break;
	}
	return ret;
}

static int fimc_is_g_ext_ctrls(struct v4l2_subdev *sd,
	struct v4l2_ext_controls *ctrls)
{
	struct fimc_is_dev *dev = to_fimc_is_dev(sd);
	struct v4l2_ext_control *ctrl;
	int i, ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&dev->slock, flags);
	ctrl = ctrls->controls;
	if (ctrls->ctrl_class != V4L2_CTRL_CLASS_CAMERA)
		return -EINVAL;

	fimc_is_mem_cache_inv((void *)IS_FACE,
		(unsigned long)(sizeof(struct is_face_marker)*MAX_FACE_COUNT));

	dev->fd_header.offset = 0;
	/* get width and height at the current scenario */
	switch (dev->scenario_id) {
	case ISS_PREVIEW_STILL:
		dev->fd_header.width = (s32)dev->sensor.width_prev;
		dev->fd_header.height = (s32)dev->sensor.height_prev;
		break;
	case ISS_PREVIEW_VIDEO:
		dev->fd_header.width = (s32)dev->sensor.width_prev_cam;
		dev->fd_header.height = (s32)dev->sensor.height_prev_cam;
		break;
	case ISS_CAPTURE_STILL:
		dev->fd_header.width = (s32)dev->sensor.width_cap;
		dev->fd_header.height = (s32)dev->sensor.height_cap;
		break;
	case ISS_CAPTURE_VIDEO:
		dev->fd_header.width = (s32)dev->sensor.width_cam;
		dev->fd_header.height = (s32)dev->sensor.height_cam;
		break;
	}
	for (i = 0; i < ctrls->count; i++) {
		ctrl = ctrls->controls + i;
		ret = fimc_is_g_ext_ctrls_handler(dev, ctrl, i);
		if (ret > 0) {
			ctrls->error_idx = i;
			break;
		} else if (ret < 0) {
			ret = 0;
			break;
		}
	}

	dev->fd_header.index = 0;
	dev->fd_header.count = 0;
	spin_unlock_irqrestore(&dev->slock, flags);
	return ret;
}

static int fimc_is_s_ext_ctrls_handler(struct fimc_is_dev *dev,
	struct v4l2_ext_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_IS_TUNE_SEL_ENTRY:
		dev->h2i_cmd.entry_id = (0x1 << ctrl->value);
		break;
	case V4L2_CID_IS_TUNE_SENSOR_EXPOSURE:
		IS_SENSOR_SET_TUNE_EXPOSURE(dev, ctrl->value);
		break;
	case V4L2_CID_IS_TUNE_SENSOR_ANALOG_GAIN:
		IS_SENSOR_SET_TUNE_ANALOG_GAIN(dev, ctrl->value);
		break;
	case V4L2_CID_IS_TUNE_SENSOR_FRAME_RATE:
		IS_SENSOR_SET_TUNE_FRAME_RATE(dev, ctrl->value);
		break;
	case V4L2_CID_IS_TUNE_SENSOR_ACTUATOR_POS:
		if (!is_af_use(dev))
			IS_SENSOR_SET_TUNE_ACTUATOR_POSITION(dev, 0);
		else
			IS_SENSOR_SET_TUNE_ACTUATOR_POSITION(dev, ctrl->value);
		break;
	default:
		dbg("Invalid control\n");
		return -EINVAL;
	}
	return 0;
}

static int fimc_is_s_ext_ctrls(struct v4l2_subdev *sd,
	struct v4l2_ext_controls *ctrls)
{
	struct fimc_is_dev *dev = to_fimc_is_dev(sd);
	struct v4l2_ext_control *ctrl = ctrls->controls;
	int i, ret = 0;

	dbg("S_EXT_CTRLS - %d\n", ctrls->count);
	if (ctrls->ctrl_class != V4L2_CTRL_CLASS_CAMERA)
		return -EINVAL;

	dev->h2i_cmd.cmd_type = 0;
	dev->h2i_cmd.entry_id = 0;
	for (i = 0; i < ctrls->count; i++, ctrl++) {
		ret = fimc_is_s_ext_ctrls_handler(dev, ctrl);
		if (ret) {
			ctrls->error_idx = i;
			break;
		}
	}

	fimc_is_mem_cache_clean((void *)dev->is_p_region, IS_PARAM_SIZE);
	fimc_is_hw_set_tune(dev);

	return ret;
}

/* v4l2 subdev video operations
*/
static int fimc_is_try_mbus_fmt(struct v4l2_subdev *sd,
struct v4l2_mbus_framefmt *mf)
{
	struct fimc_is_dev *dev = to_fimc_is_dev(sd);
	dbg("fimc_is_try_mbus_fmt - %d, %d\n", mf->width, mf->height);
	switch (dev->scenario_id) {
	case ISS_PREVIEW_STILL:
		dev->sensor.width_prev = mf->width;
		dev->sensor.height_prev = mf->height;
		break;
	case ISS_PREVIEW_VIDEO:
		dev->sensor.width_prev_cam = mf->width;
		dev->sensor.height_prev_cam = mf->height;
		break;
	case ISS_CAPTURE_STILL:
		dev->sensor.width_cap = mf->width;
		dev->sensor.height_cap = mf->height;
		break;
	case ISS_CAPTURE_VIDEO:
		dev->sensor.width_cam = mf->width;
		dev->sensor.height_cam = mf->height;
		break;
	}
	/* for otf, only one image format is available */
	IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, mf->width);
	IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, mf->height);
	IS_ISP_SET_PARAM_OTF_OUTPUT_WIDTH(dev, mf->width);
	IS_ISP_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, mf->height);

	IS_DRC_SET_PARAM_OTF_INPUT_WIDTH(dev, mf->width);
	IS_DRC_SET_PARAM_OTF_INPUT_HEIGHT(dev, mf->height);
	IS_DRC_SET_PARAM_OTF_OUTPUT_WIDTH(dev, mf->width);
	IS_DRC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, mf->height);

	IS_FD_SET_PARAM_OTF_INPUT_WIDTH(dev, mf->width);
	IS_FD_SET_PARAM_OTF_INPUT_HEIGHT(dev, mf->height);
	return 0;
}

static int fimc_is_g_mbus_fmt(struct v4l2_subdev *sd,
	struct v4l2_mbus_framefmt *mf)
{
	struct fimc_is_dev *dev = to_fimc_is_dev(sd);
	dbg("fimc_is_g_mbus_fmt\n");
	/* for otf, only one image format is available */
	IS_DRC_GET_PARAM_OTF_OUTPUT_WIDTH(dev, mf->width);
	IS_DRC_GET_PARAM_OTF_OUTPUT_HEIGHT(dev, mf->height);
	mf->code = V4L2_MBUS_FMT_YUYV8_2X8;
	mf->field = 0;
	return 0;
}

static int fimc_is_s_mbus_fmt(struct v4l2_subdev *sd,
	struct v4l2_mbus_framefmt *mf)
{
	struct fimc_is_dev *dev = to_fimc_is_dev(sd);
	int ret = 0, format;
	u32 frametime_max = 0;

	printk(KERN_INFO "FIMC-IS s_fmt = %d,%d\n", mf->width, mf->height);
	/* scenario ID setting */
	switch (mf->field) {
	case 0:
		dev->scenario_id = ISS_PREVIEW_STILL;
		dev->sensor.width_prev = mf->width;
		dev->sensor.height_prev = mf->height;
		if (!dev->sensor.framerate_update)
			frametime_max = dev->sensor.frametime_max_prev;
		break;
	case 1:
		dev->scenario_id = ISS_PREVIEW_VIDEO;
		dev->sensor.width_prev_cam = mf->width;
		dev->sensor.height_prev_cam = mf->height;
		if (!dev->sensor.framerate_update)
			frametime_max = dev->sensor.frametime_max_prev_cam;
		break;
	case 2:
		dev->scenario_id = ISS_CAPTURE_STILL;
		dev->sensor.width_cap = mf->width;
		dev->sensor.height_cap = mf->height;
		if (!dev->sensor.framerate_update)
			frametime_max = dev->sensor.frametime_max_cap;
		break;
	case 3:
		dev->scenario_id = ISS_CAPTURE_VIDEO;
		dev->sensor.width_cam = mf->width;
		dev->sensor.height_cam = mf->height;
		if (!dev->sensor.framerate_update)
			frametime_max = dev->sensor.frametime_max_cam;
		break;
	default:
		return ret;
	}

	format = fimc_is_hw_get_sensor_format(dev);
	/* 1. ISP input / output*/
	IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, mf->width);
	IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, mf->height);
	IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev, format);
	IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev, OTF_INPUT_BIT_WIDTH_10BIT);
	IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev, OTF_INPUT_ORDER_BAYER_GR_BG);
	IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev, 0);
	IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev, 0);
	IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev, 0);
	IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev, 0);
	if (!dev->sensor.framerate_update) {
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev, frametime_max);
	}
	IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);
	IS_ISP_SET_PARAM_OTF_OUTPUT_WIDTH(dev, mf->width);
	IS_ISP_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, mf->height);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_OUTPUT);
	IS_INC_PARAM_NUM(dev);
	IS_ISP_SET_PARAM_DMA_OUTPUT1_WIDTH(dev, mf->width);
	IS_ISP_SET_PARAM_DMA_OUTPUT1_HEIGHT(dev, mf->height);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA1_OUTPUT);
	IS_INC_PARAM_NUM(dev);
	IS_ISP_SET_PARAM_DMA_OUTPUT2_WIDTH(dev, mf->width);
	IS_ISP_SET_PARAM_DMA_OUTPUT2_HEIGHT(dev, mf->height);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA2_OUTPUT);
	IS_INC_PARAM_NUM(dev);
	/* 2. DRC input / output*/
	IS_DRC_SET_PARAM_OTF_INPUT_WIDTH(dev, mf->width);
	IS_DRC_SET_PARAM_OTF_INPUT_HEIGHT(dev, mf->height);
	IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);
	IS_DRC_SET_PARAM_OTF_OUTPUT_WIDTH(dev, mf->width);
	IS_DRC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, mf->height);
	IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_OUTPUT);
	IS_INC_PARAM_NUM(dev);
	/* 3. FD input / output*/
	IS_FD_SET_PARAM_OTF_INPUT_WIDTH(dev, mf->width);
	IS_FD_SET_PARAM_OTF_INPUT_HEIGHT(dev, mf->height);
	IS_SET_PARAM_BIT(dev, PARAM_FD_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);

	fimc_is_mem_cache_clean((void *)dev->is_p_region, IS_PARAM_SIZE);
	clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
	fimc_is_hw_set_param(dev);
	/* Below sequence is for preventing system hang
		due to size mis-match */
	ret = wait_event_timeout(dev->irq_queue1,
		test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		err("wait timeout : Set format - %d, %d\n",
						mf->width, mf->height);
		fimc_is_hw_set_low_poweroff(dev, true);
		return -EINVAL;
	}
	dev->sensor.framerate_update = false;
	return 0;
}

static int fimc_is_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;
	struct fimc_is_dev *dev = to_fimc_is_dev(sd);

	if (enable) {
		dbg("IS Stream On\n");
		if (!test_bit(IS_ST_INIT_DONE, &dev->state)) {
			err("Not ready state!!\n");
			return -EBUSY;
		}
		clear_bit(IS_ST_STREAM_ON, &dev->state);
		fimc_is_hw_set_stream(dev, enable);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_STREAM_ON, &dev->state),
				FIMC_IS_SHUTDOWN_TIMEOUT);
		if (!ret) {
			err("wait timeout : Stream on\n");
			fimc_is_hw_set_low_poweroff(dev, true);
			return -EINVAL;
		}
	} else {
		dbg("IS Stream Off\n");
		if (!test_bit(IS_ST_INIT_DONE, &dev->state)) {
			err("Not ready state!!\n");
			return -EBUSY;
		}
		clear_bit(IS_ST_STREAM_OFF, &dev->state);
		fimc_is_hw_set_stream(dev, enable);
		ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_STREAM_OFF, &dev->state),
						FIMC_IS_SHUTDOWN_TIMEOUT);
		if (!ret) {
			err("wait timeout : Stream off\n");
			printk(KERN_ERR "Low power off\n");
			fimc_is_hw_set_low_poweroff(dev, true);
			return -EINVAL;
		}
		dev->setfile.sub_index = 0;
	}
	return ret;
}

static int fimc_is_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct fimc_is_dev *dev = to_fimc_is_dev(sd);
	u32 fps = 0;
	int width, height, format;
	int i, ret = 0;

	if (a->parm.capture.timeperframe.numerator == 0)
		fps = 0; /* prevent divide-by-0 error case */
	else
		fps = a->parm.capture.timeperframe.denominator /
					a->parm.capture.timeperframe.numerator;

	if (!test_bit(IS_ST_INIT_DONE, &dev->state)) {
		printk(KERN_ERR "FIMC_IS ins not ready!!\n");
		return -EBUSY;
	}

	width = fimc_is_hw_get_sensor_size_width(dev);
	height = fimc_is_hw_get_sensor_size_height(dev);
	format = fimc_is_hw_get_sensor_format(dev);
	dev->sensor.framerate_update = true;
	if (fps > 0) {
		IS_SENSOR_SET_FRAME_RATE(dev, fps);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, OTF_INPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, height);
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev, format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
						OTF_INPUT_BIT_WIDTH_10BIT);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
						OTF_INPUT_ORDER_BAYER_GR_BG);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev,
							(u32)(1000000/fps));
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev,
						CONTROL_COMMAND_START);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
	} else {
		/* Auto mode */
		i = fimc_is_hw_get_sensor_max_framerate(dev);
		IS_SENSOR_SET_FRAME_RATE(dev, i);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, OTF_INPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, height);
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev, format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
						OTF_INPUT_BIT_WIDTH_10BIT);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
						OTF_INPUT_ORDER_BAYER_GR_BG);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev, 66666);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue1,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(IS_ST_STREAM_ON, &dev->state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev,
						CONTROL_COMMAND_START);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue1,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout : %s\n", __func__);
				return -EINVAL;
			}
		}
	}
	return ret;
}

const struct v4l2_subdev_core_ops fimc_is_core_ops = {
	.load_fw = fimc_is_load_fw,
	.init = fimc_is_init_set,
	.reset = fimc_is_reset,
	.s_power = fimc_is_s_power,
	.g_ctrl = fimc_is_g_ctrl,
	.s_ctrl = fimc_is_s_ctrl,
	.g_ext_ctrls = fimc_is_g_ext_ctrls,
	.s_ext_ctrls = fimc_is_s_ext_ctrls,
};

const struct v4l2_subdev_video_ops fimc_is_video_ops = {
	.try_mbus_fmt	= fimc_is_try_mbus_fmt,
	.g_mbus_fmt	= fimc_is_g_mbus_fmt,
	.s_mbus_fmt	= fimc_is_s_mbus_fmt,
	.s_stream	= fimc_is_s_stream,
	.s_parm		= fimc_is_s_parm,
};

const struct v4l2_subdev_ops fimc_is_subdev_ops = {
	.core	= &fimc_is_core_ops,
	.video	= &fimc_is_video_ops,
};
