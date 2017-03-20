/*
 * Copyright (c) 2007-2016, Synaptics Incorporated
 * Copyright (C) 2016 Zodiac Inflight Innovations
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/rmi.h>
#include <linux/firmware.h>
#include <asm/unaligned.h>
#include <asm/unaligned.h>
#include <linux/bitops.h>

#include "rmi_driver.h"
#include "rmi_f34.h"

static int rmi_f34_write_bootloader_id(struct f34_data *f34)
{
	struct rmi_function *fn = f34->fn;
	struct rmi_device *rmi_dev = fn->rmi_dev;
	u8 bootloader_id[F34_BOOTLOADER_ID_LEN];
	int ret;

	ret = rmi_read_block(rmi_dev, fn->fd.query_base_addr,
			     bootloader_id, sizeof(bootloader_id));
	if (ret) {
		dev_err(&fn->dev, "%s: Reading bootloader ID failed: %d\n",
				__func__, ret);
		return ret;
	}

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "%s: writing bootloader id '%c%c'\n",
			__func__, bootloader_id[0], bootloader_id[1]);

	ret = rmi_write_block(rmi_dev,
			      fn->fd.data_base_addr + F34_BLOCK_DATA_OFFSET,
			      bootloader_id, sizeof(bootloader_id));
	if (ret) {
		dev_err(&fn->dev, "Failed to write bootloader ID: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rmi_f34_command(struct f34_data *f34, u8 command,
			   unsigned int timeout, bool write_bl_id)
{
	struct rmi_function *fn = f34->fn;
	struct rmi_device *rmi_dev = fn->rmi_dev;
	int ret;

	if (write_bl_id) {
		ret = rmi_f34_write_bootloader_id(f34);
		if (ret)
			return ret;
	}

	init_completion(&f34->v5.cmd_done);

	ret = rmi_read(rmi_dev, f34->v5.ctrl_address, &f34->v5.status);
	if (ret) {
		dev_err(&f34->fn->dev,
			"%s: Failed to read cmd register: %d (command %#02x)\n",
			__func__, ret, command);
		return ret;
	}

	f34->v5.status |= command & 0x0f;

	ret = rmi_write(rmi_dev, f34->v5.ctrl_address, f34->v5.status);
	if (ret < 0) {
		dev_err(&f34->fn->dev,
			"Failed to write F34 command %#02x: %d\n",
			command, ret);
		return ret;
	}

	if (!wait_for_completion_timeout(&f34->v5.cmd_done,
				msecs_to_jiffies(timeout))) {

		ret = rmi_read(rmi_dev, f34->v5.ctrl_address, &f34->v5.status);
		if (ret) {
			dev_err(&f34->fn->dev,
				"%s: cmd %#02x timed out: %d\n",
				__func__, command, ret);
			return ret;
		}

		if (f34->v5.status & 0x7f) {
			dev_err(&f34->fn->dev,
				"%s: cmd %#02x timed out, status: %#02x\n",
				__func__, command, f34->v5.status);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int rmi_f34_attention(struct rmi_function *fn, unsigned long *irq_bits)
{
	struct f34_data *f34 = dev_get_drvdata(&fn->dev);
	int ret;

	if (f34->bl_version != 5)
		return 0;

	ret = rmi_read(f34->fn->rmi_dev, f34->v5.ctrl_address, &f34->v5.status);
	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "%s: status: %#02x, ret: %d\n",
		__func__, f34->v5.status, ret);

	if (!ret && !(f34->v5.status & 0x7f))
		complete(&f34->v5.cmd_done);

	return 0;
}

static int rmi_f34_write_blocks(struct f34_data *f34, const void *data,
				int block_count, u8 command)
{
	struct rmi_function *fn = f34->fn;
	struct rmi_device *rmi_dev = fn->rmi_dev;
	u16 address = fn->fd.data_base_addr + F34_BLOCK_DATA_OFFSET;
	u8 start_address[] = { 0, 0 };
	int i;
	int ret;

	ret = rmi_write_block(rmi_dev, fn->fd.data_base_addr,
			      start_address, sizeof(start_address));
	if (ret) {
		dev_err(&fn->dev, "Failed to write initial zeros: %d\n", ret);
		return ret;
	}

	for (i = 0; i < block_count; i++) {
		ret = rmi_write_block(rmi_dev, address,
				      data, f34->v5.block_size);
		if (ret) {
			dev_err(&fn->dev,
				"failed to write block #%d: %d\n", i, ret);
			return ret;
		}

		ret = rmi_f34_command(f34, command, F34_IDLE_WAIT_MS, false);
		if (ret) {
			dev_err(&fn->dev,
				"Failed to write command for block #%d: %d\n",
				i, ret);
			return ret;
		}

		rmi_dbg(RMI_DEBUG_FN, &fn->dev, "wrote block %d of %d\n",
			i + 1, block_count);

		data += f34->v5.block_size;
		f34->update_progress += f34->v5.block_size;
		f34->update_status = (f34->update_progress * 100) /
			f34->update_size;
	}

	return 0;
}

static int rmi_f34_write_firmware(struct f34_data *f34, const void *data)
{
	return rmi_f34_write_blocks(f34, data, f34->v5.fw_blocks,
				    F34_WRITE_FW_BLOCK);
}

static int rmi_f34_write_config(struct f34_data *f34, const void *data)
{
	return rmi_f34_write_blocks(f34, data, f34->v5.config_blocks,
				    F34_WRITE_CONFIG_BLOCK);
}

static int rmi_f34_enable_flash(struct f34_data *f34)
{
	return rmi_f34_command(f34, F34_ENABLE_FLASH_PROG,
			       F34_ENABLE_WAIT_MS, true);
}

static int rmi_f34_flash_firmware(struct f34_data *f34,
				  const struct rmi_f34_firmware *syn_fw)
{
	struct rmi_function *fn = f34->fn;
	u32 image_size = le32_to_cpu(syn_fw->image_size);
	u32 config_size = le32_to_cpu(syn_fw->config_size);
	int ret;

	f34->update_progress = 0;
	f34->update_size = image_size + config_size;

	if (image_size) {
		dev_info(&fn->dev, "Erasing firmware...\n");
		ret = rmi_f34_command(f34, F34_ERASE_ALL,
				      F34_ERASE_WAIT_MS, true);
		if (ret)
			return ret;

		dev_info(&fn->dev, "Writing firmware (%d bytes)...\n",
			 image_size);
		ret = rmi_f34_write_firmware(f34, syn_fw->data);
		if (ret)
			return ret;
	}

	if (config_size) {
		/*
		 * We only need to erase config if we haven't updated
		 * firmware.
		 */
		if (!image_size) {
			dev_info(&fn->dev, "Erasing config...\n");
			ret = rmi_f34_command(f34, F34_ERASE_CONFIG,
					      F34_ERASE_WAIT_MS, true);
			if (ret)
				return ret;
		}

		dev_info(&fn->dev, "Writing config (%d bytes)...\n",
			 config_size);
		ret = rmi_f34_write_config(f34, &syn_fw->data[image_size]);
		if (ret)
			return ret;
	}

	return 0;
}

static int rmi_f34_update_firmware(struct f34_data *f34,
				   const struct firmware *fw)
{
	const struct rmi_f34_firmware *syn_fw =
				(const struct rmi_f34_firmware *)fw->data;
	u32 image_size = le32_to_cpu(syn_fw->image_size);
	u32 config_size = le32_to_cpu(syn_fw->config_size);
	int ret;

	BUILD_BUG_ON(offsetof(struct rmi_f34_firmware, data) !=
			F34_FW_IMAGE_OFFSET);

	rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
		"FW size:%zd, checksum:%08x, image_size:%d, config_size:%d\n",
		fw->size,
		le32_to_cpu(syn_fw->checksum),
		image_size, config_size);

	rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
		"FW bootloader_id:%02x, product_id:%.*s, info: %02x%02x\n",
		syn_fw->bootloader_version,
		(int)sizeof(syn_fw->product_id), syn_fw->product_id,
		syn_fw->product_info[0], syn_fw->product_info[1]);

	if (image_size && image_size != f34->v5.fw_blocks * f34->v5.block_size) {
		dev_err(&f34->fn->dev,
			"Bad firmware image: fw size %d, expected %d\n",
			image_size, f34->v5.fw_blocks * f34->v5.block_size);
		ret = -EILSEQ;
		goto out;
	}

	if (config_size &&
	    config_size != f34->v5.config_blocks * f34->v5.block_size) {
		dev_err(&f34->fn->dev,
			"Bad firmware image: config size %d, expected %d\n",
			config_size,
			f34->v5.config_blocks * f34->v5.block_size);
		ret = -EILSEQ;
		goto out;
	}

	if (image_size && !config_size) {
		dev_err(&f34->fn->dev, "Bad firmware image: no config data\n");
		ret = -EILSEQ;
		goto out;
	}

	dev_info(&f34->fn->dev, "Firmware image OK\n");
	mutex_lock(&f34->v5.flash_mutex);

	ret = rmi_f34_flash_firmware(f34, syn_fw);

	mutex_unlock(&f34->v5.flash_mutex);

out:
	return ret;
}

static int rmi_f34_status(struct rmi_function *fn)
{
	struct f34_data *f34 = dev_get_drvdata(&fn->dev);

	/*
	 * The status is the percentage complete, or once complete,
	 * zero for success or a negative return code.
	 */
	return f34->update_status;
}

static ssize_t rmi_driver_bootloader_id_show(struct device *dev,
					     struct device_attribute *dattr,
					     char *buf)
{
	struct rmi_driver_data *data = dev_get_drvdata(dev);
	struct rmi_function *fn = data->f34_container;
	struct f34_data *f34;

	if (fn) {
		f34 = dev_get_drvdata(&fn->dev);

		if (f34->bl_version == 5)
			return scnprintf(buf, PAGE_SIZE, "%c%c\n",
					 f34->bootloader_id[0],
					 f34->bootloader_id[1]);
		else
			return scnprintf(buf, PAGE_SIZE, "V%d.%d\n",
					 f34->bootloader_id[1],
					 f34->bootloader_id[0]);
	}

	return 0;
}

static DEVICE_ATTR(bootloader_id, 0444, rmi_driver_bootloader_id_show, NULL);

static ssize_t rmi_driver_configuration_id_show(struct device *dev,
						struct device_attribute *dattr,
						char *buf)
{
	struct rmi_driver_data *data = dev_get_drvdata(dev);
	struct rmi_function *fn = data->f34_container;
	struct f34_data *f34;

	if (fn) {
		f34 = dev_get_drvdata(&fn->dev);

		return scnprintf(buf, PAGE_SIZE, "%s\n", f34->configuration_id);
	}

	return 0;
}

static DEVICE_ATTR(configuration_id, 0444,
		   rmi_driver_configuration_id_show, NULL);

static int rmi_firmware_update(struct rmi_driver_data *data,
			       const struct firmware *fw)
{
	struct rmi_device *rmi_dev = data->rmi_dev;
	struct device *dev = &rmi_dev->dev;
	struct f34_data *f34;
	int ret;

	if (!data->f34_container) {
		dev_warn(dev, "%s: No F34 present!\n", __func__);
		return -EINVAL;
	}

	f34 = dev_get_drvdata(&data->f34_container->dev);

	if (f34->bl_version == 7) {
		if (data->pdt_props & HAS_BSR) {
			dev_err(dev, "%s: LTS not supported\n", __func__);
			return -ENODEV;
		}
	} else if (f34->bl_version != 5) {
		dev_warn(dev, "F34 V%d not supported!\n",
			 data->f34_container->fd.function_version);
		return -ENODEV;
	}

	/* Enter flash mode */
	if (f34->bl_version == 7)
		ret = rmi_f34v7_start_reflash(f34, fw);
	else
		ret = rmi_f34_enable_flash(f34);
	if (ret)
		return ret;

	rmi_disable_irq(rmi_dev, false);

	/* Tear down functions and re-probe */
	rmi_free_function_list(rmi_dev);

	ret = rmi_probe_interrupts(data);
	if (ret)
		return ret;

	ret = rmi_init_functions(data);
	if (ret)
		return ret;

	if (!data->bootloader_mode || !data->f34_container) {
		dev_warn(dev, "%s: No F34 present or not in bootloader!\n",
				__func__);
		return -EINVAL;
	}

	rmi_enable_irq(rmi_dev, false);

	f34 = dev_get_drvdata(&data->f34_container->dev);

	/* Perform firmware update */
	if (f34->bl_version == 7)
		ret = rmi_f34v7_do_reflash(f34, fw);
	else
		ret = rmi_f34_update_firmware(f34, fw);

	if (ret) {
		f34->update_status = ret;
		dev_err(&f34->fn->dev,
			"Firmware update failed, status: %d\n", ret);
	} else {
		dev_info(&f34->fn->dev, "Firmware update complete\n");
	}

	rmi_disable_irq(rmi_dev, false);

	/* Re-probe */
	rmi_dbg(RMI_DEBUG_FN, dev, "Re-probing device\n");
	rmi_free_function_list(rmi_dev);

	ret = rmi_scan_pdt(rmi_dev, NULL, rmi_initial_reset);
	if (ret < 0)
		dev_warn(dev, "RMI reset failed!\n");

	ret = rmi_probe_interrupts(data);
	if (ret)
		return ret;

	ret = rmi_init_functions(data);
	if (ret)
		return ret;

	rmi_enable_irq(rmi_dev, false);

	if (data->f01_container->dev.driver)
		/* Driver already bound, so enable ATTN now. */
		return rmi_enable_sensor(rmi_dev);

	rmi_dbg(RMI_DEBUG_FN, dev, "%s complete\n", __func__);

	return ret;
}

static ssize_t rmi_driver_update_fw_store(struct device *dev,
					  struct device_attribute *dattr,
					  const char *buf, size_t count)
{
	struct rmi_driver_data *data = dev_get_drvdata(dev);
	char fw_name[NAME_MAX];
	const struct firmware *fw;
	size_t copy_count = count;
	int ret;

	if (count == 0 || count >= NAME_MAX)
		return -EINVAL;

	if (buf[count - 1] == '\0' || buf[count - 1] == '\n')
		copy_count -= 1;

	strncpy(fw_name, buf, copy_count);
	fw_name[copy_count] = '\0';

	ret = request_firmware(&fw, fw_name, dev);
	if (ret)
		return ret;

	dev_info(dev, "Flashing %s\n", fw_name);

	ret = rmi_firmware_update(data, fw);

	release_firmware(fw);

	return ret ?: count;
}

static DEVICE_ATTR(update_fw, 0200, NULL, rmi_driver_update_fw_store);

static ssize_t rmi_driver_update_fw_status_show(struct device *dev,
						struct device_attribute *dattr,
						char *buf)
{
	struct rmi_driver_data *data = dev_get_drvdata(dev);
	int update_status = 0;

	if (data->f34_container)
		update_status = rmi_f34_status(data->f34_container);

	return scnprintf(buf, PAGE_SIZE, "%d\n", update_status);
}

static DEVICE_ATTR(update_fw_status, 0444,
		   rmi_driver_update_fw_status_show, NULL);

static struct attribute *rmi_firmware_attrs[] = {
	&dev_attr_bootloader_id.attr,
	&dev_attr_configuration_id.attr,
	&dev_attr_update_fw.attr,
	&dev_attr_update_fw_status.attr,
	NULL
};

static struct attribute_group rmi_firmware_attr_group = {
	.attrs = rmi_firmware_attrs,
};

static int rmi_f34_probe(struct rmi_function *fn)
{
	struct f34_data *f34;
	unsigned char f34_queries[9];
	bool has_config_id;
	u8 version = fn->fd.function_version;
	int ret;

	f34 = devm_kzalloc(&fn->dev, sizeof(struct f34_data), GFP_KERNEL);
	if (!f34)
		return -ENOMEM;

	f34->fn = fn;
	dev_set_drvdata(&fn->dev, f34);

	/* v5 code only supported version 0, try V7 probe */
	if (version > 0)
		return rmi_f34v7_probe(f34);

	f34->bl_version = 5;

	ret = rmi_read_block(fn->rmi_dev, fn->fd.query_base_addr,
			     f34_queries, sizeof(f34_queries));
	if (ret) {
		dev_err(&fn->dev, "%s: Failed to query properties\n",
			__func__);
		return ret;
	}

	snprintf(f34->bootloader_id, sizeof(f34->bootloader_id),
		 "%c%c", f34_queries[0], f34_queries[1]);

	mutex_init(&f34->v5.flash_mutex);
	init_completion(&f34->v5.cmd_done);

	f34->v5.block_size = get_unaligned_le16(&f34_queries[3]);
	f34->v5.fw_blocks = get_unaligned_le16(&f34_queries[5]);
	f34->v5.config_blocks = get_unaligned_le16(&f34_queries[7]);
	f34->v5.ctrl_address = fn->fd.data_base_addr + F34_BLOCK_DATA_OFFSET +
		f34->v5.block_size;
	has_config_id = f34_queries[2] & (1 << 2);

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "Bootloader ID: %s\n",
		f34->bootloader_id);
	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "Block size: %d\n",
		f34->v5.block_size);
	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "FW blocks: %d\n",
		f34->v5.fw_blocks);
	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "CFG blocks: %d\n",
		f34->v5.config_blocks);

	if (has_config_id) {
		ret = rmi_read_block(fn->rmi_dev, fn->fd.control_base_addr,
				     f34_queries, sizeof(f34_queries));
		if (ret) {
			dev_err(&fn->dev, "Failed to read F34 config ID\n");
			return ret;
		}

		snprintf(f34->configuration_id, sizeof(f34->configuration_id),
			 "%02x%02x%02x%02x",
			 f34_queries[0], f34_queries[1],
			 f34_queries[2], f34_queries[3]);

		rmi_dbg(RMI_DEBUG_FN, &fn->dev, "Configuration ID: %s\n",
			 f34->configuration_id);
	}

	return 0;
}

int rmi_f34_create_sysfs(struct rmi_device *rmi_dev)
{
	return sysfs_create_group(&rmi_dev->dev.kobj, &rmi_firmware_attr_group);
}

void rmi_f34_remove_sysfs(struct rmi_device *rmi_dev)
{
	sysfs_remove_group(&rmi_dev->dev.kobj, &rmi_firmware_attr_group);
}

struct rmi_function_handler rmi_f34_handler = {
	.driver = {
		.name = "rmi4_f34",
	},
	.func = 0x34,
	.probe = rmi_f34_probe,
	.attention = rmi_f34_attention,
};
