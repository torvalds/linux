/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * FacetimeHD camera driver
 *
 * Copyright (C) 2014 Patrik Jakobsson (patrik.r.jakobsson@gmail.com)
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/acpi.h>
#include <linux/firmware.h>
#include <linux/dmi.h>
#include "fthd_drv.h"
#include "fthd_hw.h"
#include "fthd_reg.h"
#include "fthd_ringbuf.h"
#include "fthd_isp.h"

int isp_mem_init(struct fthd_private *dev_priv)
{
	struct resource *root = &dev_priv->pdev->resource[FTHD_PCI_S2_MEM];

        dev_priv->mem = kzalloc(sizeof(struct resource), GFP_KERNEL);
	if (!dev_priv->mem)
	    return -ENOMEM;

	dev_priv->mem->start = root->start;
	dev_priv->mem->end = root->end;

	/* Preallocate 8mb for the firmware */
	dev_priv->firmware = isp_mem_create(dev_priv, FTHD_MEM_FIRMWARE,
					    FTHD_MEM_FW_SIZE);

	if (!dev_priv->firmware) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to preallocate firmware memory\n");
		return -ENOMEM;
	}
	return 0;
}

struct isp_mem_obj *isp_mem_create(struct fthd_private *dev_priv,
				   unsigned int type, resource_size_t size)
{
	struct isp_mem_obj *obj;
	struct resource *root = dev_priv->mem;
	int ret;

	obj = kzalloc(sizeof(struct isp_mem_obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	obj->type = type;
	obj->base.name = "S2 ISP";
	ret = allocate_resource(root, &obj->base, size, root->start, root->end,
				PAGE_SIZE, NULL, NULL);
	if (ret) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to allocate resource (size: %Ld, start: %Ld, end: %Ld)\n",
			size, root->start, root->end);
		kfree(obj);
		obj = NULL;
	}

	obj->offset = obj->base.start - root->start;
	obj->size = size;
	obj->size_aligned = obj->base.end - obj->base.start;
	return obj;
}

int isp_mem_destroy(struct isp_mem_obj *obj)
{
	if (obj) {
		release_resource(&obj->base);
		kfree(obj);
		obj = NULL;
	}

	return 0;
}

static int isp_acpi_set_power(struct fthd_private *dev_priv, int power)
{
	acpi_status status;
	acpi_handle handle;
	struct acpi_object_list arg_list;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object args[1];
	union acpi_object *result;
	int ret = 0;


	handle = ACPI_HANDLE(&dev_priv->pdev->dev);
	if(!handle) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to get S2 CMPE ACPI handle\n");
		ret = -ENODEV;
		goto out;
	}

	args[0].type = ACPI_TYPE_INTEGER;
	args[0].integer.value = power;

	arg_list.count = 1;
	arg_list.pointer = args;

	status = acpi_evaluate_object(handle, "CMPE", &arg_list, &buffer);
	if (ACPI_FAILURE(status)) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to execute S2 CMPE ACPI method\n");
		ret = -ENODEV;
		goto out;
	}

	result = buffer.pointer;

	if (result->type != ACPI_TYPE_INTEGER || result->integer.value != 0) {
		dev_err(&dev_priv->pdev->dev,
			"Invalid ACPI response (len: %Ld)\n", buffer.length);
		ret = -EINVAL;
	}

out:
	kfree(buffer.pointer);
	return ret;
}

static int isp_enable_sensor(struct fthd_private *dev_priv)
{
	return 0;
}

static int isp_load_firmware(struct fthd_private *dev_priv)
{
	const struct firmware *fw;
	int ret = 0;

	ret = request_firmware(&fw, "facetimehd/firmware.bin", &dev_priv->pdev->dev);
	if (ret)
		return ret;

	/* Firmware memory is preallocated at init time */
	if (!dev_priv->firmware)
		return -ENOMEM;

	if (dev_priv->firmware->base.start != dev_priv->mem->start) {
		dev_err(&dev_priv->pdev->dev,
			"Misaligned firmware memory object (offset: %lu)\n",
			dev_priv->firmware->offset);
		isp_mem_destroy(dev_priv->firmware);
		dev_priv->firmware = NULL;
		return -EBUSY;
	}

	FTHD_S2_MEMCPY_TOIO(dev_priv->firmware->offset, fw->data, fw->size);

	/* Might need a flush here if we map ISP memory cached */

	dev_info(&dev_priv->pdev->dev, "Loaded firmware, size: %lukb\n",
		 fw->size / 1024);

	release_firmware(fw);

	return ret;
}

static void isp_free_channel_info(struct fthd_private *priv)
{
	struct fw_channel *chan;
	int i;
	for(i = 0; i < priv->num_channels; i++) {
		chan = priv->channels[i];
		if (!chan)
			continue;

		kfree(chan->name);
		kfree(chan);
		priv->channels[i] = NULL;
	}
	kfree(priv->channels);
	priv->channels = NULL;
}

static struct fw_channel *isp_get_chan_index(struct fthd_private *priv, const char *name)
{
	int i;
	for(i = 0; i < priv->num_channels; i++) {
		if (!strcasecmp(priv->channels[i]->name, name))
			return priv->channels[i];
	}
	return NULL;
}

static int isp_fill_channel_info(struct fthd_private *dev_priv, int offset, int num_channels)
{
	struct isp_channel_info info;
	struct fw_channel *chan;
	int i;

	if (!num_channels)
		return -EINVAL;

	dev_priv->channels = kzalloc(num_channels * sizeof(struct fw_channel *), GFP_KERNEL);
	if (!dev_priv->channels)
		goto out;

	dev_priv->num_channels = num_channels;

	for(i = 0; i < num_channels; i++) {
		FTHD_S2_MEMCPY_FROMIO(&info, offset + i * 256, sizeof(info));

		chan = kzalloc(sizeof(struct fw_channel), GFP_KERNEL);
		if (!chan)
			goto out;

		dev_priv->channels[i] = chan;

		pr_debug("Channel %d: %s, type %d, source %d, size %d, offset %x\n",
			 i, info.name, info.type, info.source, info.size, info.offset);

		chan->name = kstrdup(info.name, GFP_KERNEL);
		if (!chan->name)
			goto out;

		chan->type = info.type;
		chan->source = info.source;
		chan->size = info.size;
		chan->offset = info.offset;
		spin_lock_init(&chan->lock);
		init_waitqueue_head(&chan->wq);
	}

	dev_priv->channel_terminal = isp_get_chan_index(dev_priv, "TERMINAL");
	dev_priv->channel_debug = isp_get_chan_index(dev_priv, "DEBUG");
	dev_priv->channel_shared_malloc = isp_get_chan_index(dev_priv, "SHAREDMALLOC");
	dev_priv->channel_io = isp_get_chan_index(dev_priv, "IO");
	dev_priv->channel_buf_h2t = isp_get_chan_index(dev_priv, "BUF_H2T");
	dev_priv->channel_buf_t2h = isp_get_chan_index(dev_priv, "BUF_T2H");
	dev_priv->channel_io_t2h = isp_get_chan_index(dev_priv, "IO_T2H");

	if (!dev_priv->channel_terminal || !dev_priv->channel_debug
	    || !dev_priv->channel_shared_malloc || !dev_priv->channel_io
	    || !dev_priv->channel_buf_h2t || !dev_priv->channel_buf_t2h
	    || !dev_priv->channel_io_t2h) {
		dev_err(&dev_priv->pdev->dev, "did not find all of the required channels\n");
		goto out;
	}
	return 0;
out:
	isp_free_channel_info(dev_priv);
	return -ENOMEM;
}

static int fthd_isp_cmd(struct fthd_private *dev_priv, enum fthd_isp_cmds command, void *buf,
			int request_len, int *response_len)
{
	struct isp_mem_obj *request;
	struct isp_cmd_hdr cmd;
	u32 address, request_size, response_size;
	u32 entry;
	int len, ret;

	memset(&cmd, 0, sizeof(cmd));

	if (response_len) {
		len = max(request_len, *response_len);
	} else {
		len = request_len;
	}
	len += sizeof(struct isp_cmd_hdr);

	pr_debug("sending cmd %d to firmware\n", command);

	request = isp_mem_create(dev_priv, FTHD_MEM_CMD, len);
	if (!request) {
		dev_err(&dev_priv->pdev->dev, "failed to allocate cmd memory object\n");
		return -ENOMEM;
	}

	cmd.opcode = command;

	FTHD_S2_MEMCPY_TOIO(request->offset, &cmd, sizeof(struct isp_cmd_hdr));
	if (request_len)
		FTHD_S2_MEMCPY_TOIO(request->offset + sizeof(struct isp_cmd_hdr), buf, request_len);

	ret = fthd_channel_ringbuf_send(dev_priv, dev_priv->channel_io,
					  request->offset, request_len + 8, (response_len ? *response_len : 0) + 8, &entry);
	if (ret)
		goto out;

	if (entry == (u32)-1) {
		ret = -EIO;
		goto out;
	}

	if (command == CISP_CMD_POWER_DOWN) {
		/* powerdown doesn't seem to generate a response */
		ret = 0;
		goto out;
	}

        ret = fthd_channel_wait_ready(dev_priv, dev_priv->channel_io, entry, 2000);
	if (ret) {
		if (response_len)
			*response_len = 0;
		goto out;
	}

	FTHD_S2_MEMCPY_FROMIO(&cmd, request->offset, sizeof(struct isp_cmd_hdr));
	address = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_ADDRESS_FLAGS);
	request_size = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_REQUEST_SIZE);
	response_size = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_RESPONSE_SIZE);

	/* XXX: response size in the ringbuf is zero after command completion, how is buffer size
	        verification done? */
	if (response_len && *response_len)
		FTHD_S2_MEMCPY_FROMIO(buf, (address & ~3) + sizeof(struct isp_cmd_hdr),
				     *response_len);

	pr_debug("status %04x, request_len %d response len %d address_flags %x\n", cmd.status,
		request_size, response_size, address);

	ret = cmd.status ? -EIO : 0;
out:
	isp_mem_destroy(request);
	return ret;
}

int fthd_isp_debug_cmd(struct fthd_private *dev_priv, enum fthd_isp_cmds command, void *buf,
			int request_len, int *response_len)
{
	struct isp_mem_obj *request;
	struct isp_cmd_hdr cmd;
	u32 address, request_size, response_size;
	u32 entry;
	int len, ret;

	memset(&cmd, 0, sizeof(cmd));

	if (response_len) {
		len = max(request_len, *response_len);
	} else {
		len = request_len;
	}
	len += sizeof(struct isp_cmd_hdr);

	pr_debug("sending debug cmd %d to firmware\n", command);

	request = isp_mem_create(dev_priv, FTHD_MEM_CMD, len);
	if (!request) {
		dev_err(&dev_priv->pdev->dev, "failed to allocate cmd memory object\n");
		return -ENOMEM;
	}

	cmd.opcode = command;

	FTHD_S2_MEMCPY_TOIO(request->offset, &cmd, sizeof(struct isp_cmd_hdr));
	if (request_len)
		FTHD_S2_MEMCPY_TOIO(request->offset + sizeof(struct isp_cmd_hdr), buf, request_len);

	ret = fthd_channel_ringbuf_send(dev_priv, dev_priv->channel_debug,
					  request->offset, request_len + 8, (response_len ? *response_len : 0) + 8, &entry);
	if (ret)
		goto out;

	if (entry == (u32)-1) {
		ret = -EIO;
		goto out;
	}

        ret = fthd_channel_wait_ready(dev_priv, dev_priv->channel_debug, entry, 20000);
	if (ret) {
		if (response_len)
			*response_len = 0;
		goto out;
	}

	FTHD_S2_MEMCPY_FROMIO(&cmd, request->offset, sizeof(struct isp_cmd_hdr));
	address = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_ADDRESS_FLAGS);
	request_size = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_REQUEST_SIZE);
	response_size = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_RESPONSE_SIZE);

	/* XXX: response size in the ringbuf is zero after command completion, how is buffer size
	        verification done? */
	if (response_len && *response_len)
		FTHD_S2_MEMCPY_FROMIO(buf, (address & ~3) + sizeof(struct isp_cmd_hdr),
				     *response_len);

	pr_info("status %04x, request_len %d response len %d address_flags %x\n", cmd.status,
		request_size, response_size, address);

	ret = 0;
out:
	isp_mem_destroy(request);
	return ret;
}



int fthd_isp_cmd_start(struct fthd_private *dev_priv)
{
	pr_debug("sending start cmd to firmware\n");
	return fthd_isp_cmd(dev_priv, CISP_CMD_START, NULL, 0, NULL);
}

int fthd_isp_cmd_channel_start(struct fthd_private *dev_priv)
{
	struct isp_cmd_channel_start cmd;
	pr_debug("sending channel start cmd to firmware\n");

	cmd.channel = 0;
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_START, &cmd, sizeof(cmd), NULL);
}

int fthd_isp_cmd_channel_stop(struct fthd_private *dev_priv)
{
	struct isp_cmd_channel_stop cmd;

	cmd.channel = 0;
	pr_debug("sending channel stop cmd to firmware\n");
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_STOP, &cmd, sizeof(cmd), NULL);
}

int fthd_isp_cmd_stop(struct fthd_private *dev_priv)
{
	return fthd_isp_cmd(dev_priv, CISP_CMD_STOP, NULL, 0, NULL);
}

static int fthd_isp_cmd_powerdown(struct fthd_private *dev_priv)
{
	return fthd_isp_cmd(dev_priv, CISP_CMD_POWER_DOWN, NULL, 0, NULL);
}

static void isp_free_set_file(struct fthd_private *dev_priv)
{
	if (dev_priv->set_file)
		isp_mem_destroy(dev_priv->set_file);
}

int isp_powerdown(struct fthd_private *dev_priv)
{
	int retries;
	u32 reg;

	FTHD_ISP_REG_WRITE(0xf7fbdff9, 0xc3000);
	fthd_isp_cmd_powerdown(dev_priv);

	for (retries = 0; retries < 100; retries++) {
		reg = FTHD_ISP_REG_READ(0xc3000);
		if (reg == 0x8042006)
			break;
		mdelay(10);
	}

	if (retries >= 100) {
		dev_info(&dev_priv->pdev->dev, "deinit failed!\n");
		return -EIO;
	}
	return 0;
}

int isp_uninit(struct fthd_private *dev_priv)
{
	FTHD_ISP_REG_WRITE(0x00000000, 0x40004);
	FTHD_ISP_REG_WRITE(0x00000000, ISP_IRQ_ENABLE);
	FTHD_ISP_REG_WRITE(0xffffffff, 0xc0008);
	FTHD_ISP_REG_WRITE(0xffffffff, 0xc000c);
	FTHD_ISP_REG_WRITE(0xffffffff, 0xc0010);
	FTHD_ISP_REG_WRITE(0x00000000, 0xc1004);
	FTHD_ISP_REG_WRITE(0xffffffff, 0xc100c);
	FTHD_ISP_REG_WRITE(0xffffffff, 0xc1014);
	FTHD_ISP_REG_WRITE(0xffffffff, 0xc101c);
	FTHD_ISP_REG_WRITE(0xffffffff, 0xc1024);
	mdelay(1);

	FTHD_ISP_REG_WRITE(0, 0xc0000);
	FTHD_ISP_REG_WRITE(0, 0xc0004);
	FTHD_ISP_REG_WRITE(0, 0xc0008);
	FTHD_ISP_REG_WRITE(0, 0xc000c);
	FTHD_ISP_REG_WRITE(0, 0xc0010);
	FTHD_ISP_REG_WRITE(0, 0xc0014);
	FTHD_ISP_REG_WRITE(0, 0xc0018);
	FTHD_ISP_REG_WRITE(0, 0xc001c);
	FTHD_ISP_REG_WRITE(0, 0xc0020);
	FTHD_ISP_REG_WRITE(0, 0xc0024);

	FTHD_ISP_REG_WRITE(0xffffffff, ISP_IRQ_CLEAR);
	isp_free_channel_info(dev_priv);
	isp_free_set_file(dev_priv);
	isp_mem_destroy(dev_priv->firmware);
	kfree(dev_priv->mem);
	return 0;
}


int fthd_isp_cmd_print_enable(struct fthd_private *dev_priv, int enable)
{
	struct isp_cmd_print_enable cmd;

	cmd.enable = enable;

	return fthd_isp_cmd(dev_priv, CISP_CMD_PRINT_ENABLE, &cmd, sizeof(cmd), NULL);
}

int fthd_isp_cmd_set_loadfile(struct fthd_private *dev_priv)
{
	struct isp_cmd_set_loadfile cmd;
	struct isp_mem_obj *file;
	const struct firmware *fw;
	const char *filename = NULL;
	const char *vendor, *board;
	int ret = 0;

	pr_debug("set loadfile\n");

	vendor = dmi_get_system_info(DMI_BOARD_VENDOR);
	board = dmi_get_system_info(DMI_BOARD_NAME);

	memset(&cmd, 0, sizeof(cmd));

	switch(dev_priv->sensor_id1) {
	case 0x164:
		filename = "facetimehd/8221_01XX.dat";
		break;
	case 0x190:
		filename = "facetimehd/1222_01XX.dat";
		break;
	case 0x8830:
		filename = "facetimehd/9112_01XX.dat";
		break;
	case 0x9770:
		if (vendor && board && !strcmp(vendor, "Apple Inc.") &&
		    !strncmp(board, "MacBookAir", sizeof("MacBookAir")-1)) {
			filename = "facetimehd/1771_01XX.dat";
			break;
		}

		switch(dev_priv->sensor_id0) {
		case 4:
			filename = "facetimehd/1874_01XX.dat";
			break;
		default:
			filename = "facetimehd/1871_01XX.dat";
			break;
		}
		break;
	case 0x9774:
		switch(dev_priv->sensor_id0) {
		case 4:
			filename = "facetimehd/1674_01XX.dat";
			break;
		case 5:
			filename = "facetimehd/1675_01XX.dat";
			break;
		default:
			filename = "facetimehd/1671_01XX.dat";
			break;
		}
		break;
	default:
		break;

	}

	if (!filename) {
		pr_debug("no set file for sensorid %04x %04x found\n",
			 dev_priv->sensor_id0, dev_priv->sensor_id1);
		return 0;
	}

	/* The set file is allowed to be missing but we don't get calibration */
	ret = request_firmware(&fw, filename, &dev_priv->pdev->dev);
	if (ret)
		return 0;

	/* Firmware memory is preallocated at init time */
	BUG_ON(dev_priv->set_file);

	file = isp_mem_create(dev_priv, FTHD_MEM_SET_FILE, fw->size);
	FTHD_S2_MEMCPY_TOIO(file->offset, fw->data, fw->size);

	release_firmware(fw);

	dev_priv->set_file = file;
	pr_debug("set file: addr %08lx, size %d\n", file->offset, (int)file->size);
	cmd.addr = file->offset;
	cmd.length = file->size;
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_SET_FILE_LOAD, &cmd, sizeof(cmd), NULL);
}

int fthd_isp_cmd_channel_info(struct fthd_private *dev_priv)
{
	struct isp_cmd_channel_info cmd;
	int ret, len;

	pr_debug("sending ch info\n");

	memset(&cmd, 0, sizeof(cmd));
	len = sizeof(cmd);
	ret = fthd_isp_cmd(dev_priv, CISP_CMD_CH_INFO_GET, &cmd, sizeof(cmd), &len);
	print_hex_dump_bytes("CHINFO ", DUMP_PREFIX_OFFSET, &cmd, sizeof(cmd));
	pr_debug("sensor id: %04x %04x\n", cmd.sensorid0, cmd.sensorid1);
	pr_debug("sensor count: %d\n", cmd.sensor_count);
	pr_debug("camera module serial number string: %s\n", cmd.camera_module_serial_number);
	pr_debug("sensor serial number: %02X%02X%02X%02X%02X%02X%02X%02X\n",
		 cmd.sensor_serial_number[0], cmd.sensor_serial_number[1],
		 cmd.sensor_serial_number[2], cmd.sensor_serial_number[3],
		 cmd.sensor_serial_number[4], cmd.sensor_serial_number[5],
		 cmd.sensor_serial_number[6], cmd.sensor_serial_number[7]);
	dev_priv->sensor_id0 = cmd.sensorid0;
	dev_priv->sensor_id1 = cmd.sensorid1;
	dev_priv->sensor_count = cmd.sensor_count;
	return ret;
}

int fthd_isp_cmd_camera_config(struct fthd_private *dev_priv)
{
	struct isp_cmd_config cmd;
	int ret, len;

	pr_debug("sending camera config\n");

	memset(&cmd, 0, sizeof(cmd));

	len = sizeof(cmd);
	ret = fthd_isp_cmd(dev_priv, CISP_CMD_CONFIG_GET, &cmd, sizeof(cmd), &len);
	if (!ret)
		print_hex_dump_bytes("CAMINFO ", DUMP_PREFIX_OFFSET, &cmd, sizeof(cmd));
	return ret;
}

int fthd_isp_cmd_channel_camera_config(struct fthd_private *dev_priv)
{
	struct isp_cmd_channel_camera_config cmd;
	int ret, len, i;
	char prefix[16];
	pr_debug("sending ch camera config\n");

	memset(&cmd, 0, sizeof(cmd));
	for(i = 0; i < dev_priv->sensor_count; i++) {
		cmd.channel = i;

		len = sizeof(cmd);
		ret = fthd_isp_cmd(dev_priv, CISP_CMD_CH_CAMERA_CONFIG_GET, &cmd, sizeof(cmd), &len);
		if (ret)
			break;
		snprintf(prefix, sizeof(prefix)-1, "CAMCONF%d ", i);
		print_hex_dump_bytes(prefix, DUMP_PREFIX_OFFSET, &cmd, sizeof(cmd));
	}
	return ret;
}

int fthd_isp_cmd_channel_camera_config_select(struct fthd_private *dev_priv, int channel, int config)
{
	struct isp_cmd_channel_camera_config_select cmd;
	int len;

	pr_debug("set camera config: %d\n", config);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.config = config;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_CAMERA_CONFIG_SELECT, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_crop_set(struct fthd_private *dev_priv, int channel,
				  int x1, int y1, int x2, int y2)
{
	struct isp_cmd_channel_set_crop cmd;
	int len;

	pr_debug("set crop: [%d, %d] -> [%d, %d]\n", x1, y1, x2, y2);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.x1 = x1;
	cmd.y2 = y2;
	cmd.x2 = x2;
	cmd.y2 = y2;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_CROP_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_output_config_set(struct fthd_private *dev_priv, int channel, int x, int y, int pixelformat)
{
	struct isp_cmd_channel_output_config cmd;
	int len;

	pr_debug("output config: [%d, %d]\n", x, y);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.x1 = x; /* Y size */
	cmd.x2 = x * 2; /* Chroma size? */
	cmd.x3 = x;
	cmd.y1 = y;

	/* pixel formats:
	 * 0 - plane 0 Y plane 1 UV
	   1 - YUYV
	   2 - YVYU
	*/
	cmd.pixelformat = pixelformat;
	cmd.unknown3 = 0;
	cmd.unknown5 = 0x7ff;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_OUTPUT_CONFIG_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_recycle_mode(struct fthd_private *dev_priv, int channel, int mode)
{
	struct isp_cmd_channel_recycle_mode cmd;
	int len;

	pr_debug("set recycle mode %d\n", mode);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.mode = mode;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_BUFFER_RECYCLE_MODE_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_buffer_return(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_buffer_return cmd;
	int len;

	pr_debug("buffer return\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_BUFFER_RETURN, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_recycle_start(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_recycle_mode cmd;
	int len;

	pr_debug("start recycle\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_BUFFER_RECYCLE_START, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_drc_start(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_drc_start cmd;
	int len;

	pr_debug("start drc\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_DRC_START, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_tone_curve_adaptation_start(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_tone_curve_adaptation_start cmd;
	int len;

	pr_debug("tone curve adaptation start\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_APPLE_CH_TONE_CURVE_ADAPTATION_START, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_sif_pixel_format(struct fthd_private *dev_priv, int channel, int param1, int param2)
{
	struct isp_cmd_channel_sif_format_set cmd;
	int len;

	pr_debug("set pixel format %d, %d\n", param1, param2);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.param1 = param1;
	cmd.param2 = param2;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_SIF_PIXEL_FORMAT_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_error_handling_config(struct fthd_private *dev_priv, int channel, int param1, int param2)
{
	struct isp_cmd_channel_camera_err_handle_config cmd;
	int len;

	pr_debug("set error handling config %d, %d\n", param1, param2);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.param1 = param1;
	cmd.param2 = param2;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_CAMERA_ERR_HANDLE_CONFIG, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_streaming_mode(struct fthd_private *dev_priv, int channel, int mode)
{
	struct isp_cmd_channel_streaming_mode cmd;
	int len;

	pr_debug("set streaming mode %d\n", mode);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.mode = mode;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_APPLE_CH_STREAMING_MODE_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_frame_rate_min(struct fthd_private *dev_priv, int channel, int rate)
{
	struct isp_cmd_channel_frame_rate_set cmd;
	int len;

	pr_debug("set ae frame rate min %d\n", rate);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.rate = rate;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_AE_FRAME_RATE_MIN_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_frame_rate_max(struct fthd_private *dev_priv, int channel, int rate)
{
	struct isp_cmd_channel_frame_rate_set cmd;
	int len;

	pr_debug("set ae frame rate max %d\n", rate);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.rate = rate;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_AE_FRAME_RATE_MAX_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_ae_speed_set(struct fthd_private *dev_priv, int channel, int speed)
{
	struct isp_cmd_channel_ae_speed_set cmd;
	int len;

	pr_debug("set ae speed %d\n", speed);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.speed = speed;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_AE_SPEED_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_ae_stability_set(struct fthd_private *dev_priv, int channel, int stability)
{
	struct isp_cmd_channel_ae_stability_set cmd;
	int len;

	pr_debug("set ae stability %d\n", stability);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.stability = stability;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_AE_STABILITY_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_ae_stability_to_stable_set(struct fthd_private *dev_priv, int channel, int value)
{
	struct isp_cmd_channel_ae_stability_to_stable_set cmd;
	int len;

	pr_debug("set ae stability to stable %d\n", value);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.value = value;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_AE_STABILITY_TO_STABLE_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_face_detection_start(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_face_detection_start cmd;
	int len;

	pr_debug("face detection start\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_FACE_DETECTION_START, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_face_detection_stop(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_face_detection_stop cmd;
	int len;

	pr_debug("face detection stop\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_FACE_DETECTION_STOP, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_face_detection_enable(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_face_detection_enable cmd;
	int len;

	pr_debug("face detection enable\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_FACE_DETECTION_ENABLE, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_face_detection_disable(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_face_detection_disable cmd;
	int len;

	pr_debug("face detection disable\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_FACE_DETECTION_DISABLE, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_temporal_filter_start(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_temporal_filter_start cmd;
	int len;

	pr_debug("temporal filter start\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_APPLE_CH_TEMPORAL_FILTER_START, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_temporal_filter_stop(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_temporal_filter_stop cmd;
	int len;

	pr_debug("temporal filter stop\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_APPLE_CH_TEMPORAL_FILTER_STOP, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_temporal_filter_enable(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_temporal_filter_enable cmd;
	int len;

	pr_debug("temporal filter enable\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_APPLE_CH_TEMPORAL_FILTER_ENABLE, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_temporal_filter_disable(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_temporal_filter_disable cmd;
	int len;

	pr_debug("temporal filter disable\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_APPLE_CH_TEMPORAL_FILTER_DISABLE, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_motion_history_start(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_motion_history_start cmd;
	int len;

	pr_debug("motion history start\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_APPLE_CH_MOTION_HISTORY_START, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_motion_history_stop(struct fthd_private *dev_priv, int channel)
{
	struct isp_cmd_channel_motion_history_stop cmd;
	int len;

	pr_debug("motion history stop\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_APPLE_CH_MOTION_HISTORY_STOP, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_ae_metering_mode_set(struct fthd_private *dev_priv, int channel, int mode)
{
	struct isp_cmd_channel_ae_metering_mode_set cmd;
	int len;

	pr_debug("ae metering mode %d\n", mode);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.mode = mode;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_APPLE_CH_AE_METERING_MODE_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_brightness_set(struct fthd_private *dev_priv, int channel, int brightness)
{
	struct isp_cmd_channel_brightness_set cmd;
	int len;

	pr_debug("set brightness %d\n", brightness);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.brightness = brightness;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_SCALER_BRIGHTNESS_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_contrast_set(struct fthd_private *dev_priv, int channel, int contrast)
{
	struct isp_cmd_channel_contrast_set cmd;
	int len;

	pr_debug("set contrast %d\n", contrast);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.contrast = contrast;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_SCALER_CONTRAST_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_saturation_set(struct fthd_private *dev_priv, int channel, int saturation)
{
	struct isp_cmd_channel_saturation_set cmd;
	int len;

	pr_debug("set saturation %d\n", saturation);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.contrast = saturation;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_SCALER_SATURATION_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_hue_set(struct fthd_private *dev_priv, int channel, int hue)
{
	struct isp_cmd_channel_hue_set cmd;
	int len;

	pr_debug("set hue %d\n", hue);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = channel;
	cmd.contrast = hue;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, CISP_CMD_CH_SCALER_HUE_SET, &cmd, sizeof(cmd), &len);
}

int fthd_isp_cmd_channel_awb(struct fthd_private *dev_priv, int channel, int enable)
{
	struct isp_cmd_channel cmd;
	enum fthd_isp_cmds op;
	int len;

	pr_debug("set awb %s\n", enable ? "on" : "off");

	cmd.channel = channel;
	op = enable ? CISP_CMD_CH_AWB_START : CISP_CMD_CH_AWB_STOP;
	len = sizeof(cmd);
	return fthd_isp_cmd(dev_priv, op, &cmd, sizeof(cmd), &len);
}

int fthd_start_channel(struct fthd_private *dev_priv, int channel)
{
	int ret, x1 = 0, x2 = 0, pixelformat;

	ret = fthd_isp_cmd_channel_camera_config(dev_priv);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_camera_config_select(dev_priv, 0, 0);
	if (ret)
		return ret;

	if (dev_priv->fmt.fmt.width < 1280 ||
	    dev_priv->fmt.fmt.height < 720) {
		x1 = 160;
		x2 = 960;
	} else {
		x1 = 0;
		x2 = 1280;
	}

	ret = fthd_isp_cmd_channel_crop_set(dev_priv, 0, x1, 0, x2, 720);
	if (ret)
		return ret;

	switch(dev_priv->fmt.fmt.pixelformat) {
	case V4L2_PIX_FMT_YUYV:
		pixelformat = 1;
		break;
	case V4L2_PIX_FMT_YVYU:
		pixelformat = 2;
		break;
	case V4L2_PIX_FMT_NV16:
		pixelformat = 0;
		break;
	default:
		pixelformat = 1;
		WARN_ON(1);
	}
	ret = fthd_isp_cmd_channel_output_config_set(dev_priv, 0,
						     dev_priv->fmt.fmt.width,
						     dev_priv->fmt.fmt.height,
						     pixelformat);
	if (ret)
		return ret;

	ret = fthd_isp_cmd_channel_recycle_mode(dev_priv, 0, 1);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_recycle_start(dev_priv, 0);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_ae_metering_mode_set(dev_priv, 0, 3);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_drc_start(dev_priv, 0);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_tone_curve_adaptation_start(dev_priv, 0);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_ae_speed_set(dev_priv, 0, 60);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_ae_stability_set(dev_priv, 0, 75);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_ae_stability_to_stable_set(dev_priv, 0, 8);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_sif_pixel_format(dev_priv, 0, 1, 1);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_error_handling_config(dev_priv, 0, 2, 1);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_face_detection_enable(dev_priv, 0);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_face_detection_start(dev_priv, 0);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_frame_rate_max(dev_priv, 0, dev_priv->frametime * 256);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_frame_rate_min(dev_priv, 0, dev_priv->frametime * 256);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_temporal_filter_start(dev_priv, 0);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_motion_history_start(dev_priv, 0);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_temporal_filter_enable(dev_priv, 0);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_streaming_mode(dev_priv, 0, 0);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_brightness_set(dev_priv, 0, 0x80);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_contrast_set(dev_priv, 0, 0x80);
	if (ret)
		return ret;
	ret = fthd_isp_cmd_channel_start(dev_priv);
	if (ret)
		return ret;
	mdelay(1000); /* Needed to settle AE */
	return 0;
}

int fthd_stop_channel(struct fthd_private *dev_priv, int channel)
{
	int ret;

	ret = fthd_isp_cmd_channel_stop(dev_priv);
	if (ret)
		return ret;

	ret = fthd_isp_cmd_channel_buffer_return(dev_priv, 0);
	if (ret)
		return ret;

	ret = fthd_isp_cmd_channel_face_detection_stop(dev_priv, 0);
	if (ret)
		return ret;

	ret = fthd_isp_cmd_channel_face_detection_disable(dev_priv, 0);
	if (ret)
		return ret;

	ret = fthd_isp_cmd_channel_temporal_filter_disable(dev_priv, 0);
	if (ret)
		return ret;

	ret = fthd_isp_cmd_channel_motion_history_stop(dev_priv, 0);
	if (ret)
		return ret;

	return fthd_isp_cmd_channel_temporal_filter_stop(dev_priv, 0);
}

int isp_init(struct fthd_private *dev_priv)
{
	struct isp_mem_obj *fw_queue, *heap, *fw_args;
	struct isp_fw_args fw_args_data;
	u32 num_channels, queue_size, heap_size, reg, offset;
	int i, retries, ret;

	ret = isp_mem_init(dev_priv);
	if (ret)
		return ret;

	ret = isp_load_firmware(dev_priv);
	if (ret)
		return ret;

	isp_acpi_set_power(dev_priv, 1);
	mdelay(20);

	pci_set_power_state(dev_priv->pdev, PCI_D0);
	mdelay(10);

	isp_enable_sensor(dev_priv);
	FTHD_ISP_REG_WRITE(0, ISP_FW_CHAN_CTRL);
	FTHD_ISP_REG_WRITE(0, ISP_FW_QUEUE_CTRL);
	FTHD_ISP_REG_WRITE(0, ISP_FW_SIZE);
	FTHD_ISP_REG_WRITE(0, ISP_FW_HEAP_SIZE);
	FTHD_ISP_REG_WRITE(0, ISP_FW_HEAP_ADDR);
	FTHD_ISP_REG_WRITE(0, ISP_FW_HEAP_SIZE2);
	FTHD_ISP_REG_WRITE(0, ISP_REG_C3018);
	FTHD_ISP_REG_WRITE(0, ISP_REG_C301C);

	FTHD_ISP_REG_WRITE(0xffffffff, ISP_IRQ_CLEAR);

	/*
	 * Probably the IPC queue
	 * FIXME: Check if we can do 64bit writes on PCIe
	 */
	for (i = ISP_FW_CHAN_START; i <= ISP_FW_CHAN_END; i += 8) {
		FTHD_ISP_REG_WRITE(0xffffffff, i);
		FTHD_ISP_REG_WRITE(0, i + 4);
	}

	FTHD_ISP_REG_WRITE(0x80000000, ISP_REG_40008);
	FTHD_ISP_REG_WRITE(0x1, ISP_REG_40004);

	for (retries = 0; retries < 1000; retries++) {
		reg = FTHD_ISP_REG_READ(ISP_IRQ_STATUS);
		if ((reg & 0xf0) > 0)
			break;
		mdelay(10);
	}

	if (retries >= 1000) {
		dev_info(&dev_priv->pdev->dev, "Init failed! No wake signal\n");
		return -EIO;
	}

	dev_info(&dev_priv->pdev->dev, "ISP woke up after %dms\n",
		 (retries - 1) * 10);

	FTHD_ISP_REG_WRITE(0xffffffff, ISP_IRQ_CLEAR);

	num_channels = FTHD_ISP_REG_READ(ISP_FW_CHAN_CTRL);
	queue_size = FTHD_ISP_REG_READ(ISP_FW_QUEUE_CTRL) + 1;

	dev_info(&dev_priv->pdev->dev,
		 "Number of IPC channels: %u, queue size: %u\n",
		 num_channels, queue_size);

	if (num_channels > 32) {
		dev_info(&dev_priv->pdev->dev, "Too many IPC channels: %u\n",
			 num_channels);
		return -EIO;
	}

	fw_queue = isp_mem_create(dev_priv, FTHD_MEM_FW_QUEUE, queue_size);
	if (!fw_queue)
		return -ENOMEM;

	/* Firmware heap max size is 4mb */
	heap_size = FTHD_ISP_REG_READ(ISP_FW_HEAP_SIZE);

	if (heap_size == 0) {
		FTHD_ISP_REG_WRITE(0, ISP_FW_CHAN_CTRL);
		FTHD_ISP_REG_WRITE(fw_queue->offset, ISP_FW_QUEUE_CTRL);
		FTHD_ISP_REG_WRITE(dev_priv->firmware->size_aligned, ISP_FW_SIZE);
		FTHD_ISP_REG_WRITE(0x10000000 - dev_priv->firmware->size_aligned,
				   ISP_FW_HEAP_SIZE);
		FTHD_ISP_REG_WRITE(0, ISP_FW_HEAP_ADDR);
		FTHD_ISP_REG_WRITE(0, ISP_FW_HEAP_SIZE2);
	} else {
		/* Must be at least 0x1000 bytes */
		heap_size = (heap_size < 0x1000) ? 0x1000 : heap_size;

		if (heap_size > 0x400000) {
			dev_info(&dev_priv->pdev->dev,
				 "Firmware heap request size too big (%ukb)\n",
				 heap_size / 1024);
			return -ENOMEM;
		}

		dev_info(&dev_priv->pdev->dev, "Firmware requested heap size: %ukb\n",
			 heap_size / 1024);

		heap = isp_mem_create(dev_priv, FTHD_MEM_HEAP, heap_size);
		if (!heap)
			return -ENOMEM;

		FTHD_ISP_REG_WRITE(0, ISP_FW_CHAN_CTRL);

		/* Set IPC queue base addr */
		FTHD_ISP_REG_WRITE(fw_queue->offset, ISP_FW_QUEUE_CTRL);

		FTHD_ISP_REG_WRITE(FTHD_MEM_FW_SIZE, ISP_FW_SIZE);

		FTHD_ISP_REG_WRITE(0x10000000 - FTHD_MEM_FW_SIZE, ISP_FW_HEAP_SIZE);

		FTHD_ISP_REG_WRITE(heap->offset, ISP_FW_HEAP_ADDR);

		FTHD_ISP_REG_WRITE(heap->size, ISP_FW_HEAP_SIZE2);

		/* Set FW args */
		fw_args = isp_mem_create(dev_priv, FTHD_MEM_FW_ARGS, sizeof(struct isp_fw_args));
		if (!fw_args)
			return -ENOMEM;

		fw_args_data.__unknown = 2;
		fw_args_data.fw_arg = 0;
		fw_args_data.full_stats_mode = 0;

		FTHD_S2_MEMCPY_TOIO(fw_args->offset, &fw_args_data, sizeof(fw_args_data));

		FTHD_ISP_REG_WRITE(fw_args->offset, ISP_REG_C301C);

		FTHD_ISP_REG_WRITE(0x10, ISP_REG_41020);

		for (retries = 0; retries < 1000; retries++) {
			reg = FTHD_ISP_REG_READ(ISP_IRQ_STATUS);
			if ((reg & 0xf0) > 0)
				break;
			mdelay(10);
		}

		if (retries >= 1000) {
			dev_info(&dev_priv->pdev->dev, "Init failed! No second int\n");
			return -EIO;
		} /* FIXME: free on error path */

		dev_info(&dev_priv->pdev->dev, "ISP second int after %dms\n",
			 (retries - 1) * 10);

		offset = FTHD_ISP_REG_READ(ISP_FW_CHAN_CTRL);
		dev_info(&dev_priv->pdev->dev, "Channel description table at %08x\n", offset);
		ret = isp_fill_channel_info(dev_priv, offset, num_channels);
		if (ret)
			return ret;

		fthd_channel_ringbuf_init(dev_priv, dev_priv->channel_terminal);
		fthd_channel_ringbuf_init(dev_priv, dev_priv->channel_io);
		fthd_channel_ringbuf_init(dev_priv, dev_priv->channel_debug);
		fthd_channel_ringbuf_init(dev_priv, dev_priv->channel_buf_h2t);
		fthd_channel_ringbuf_init(dev_priv, dev_priv->channel_buf_t2h);
		fthd_channel_ringbuf_init(dev_priv, dev_priv->channel_shared_malloc);
		fthd_channel_ringbuf_init(dev_priv, dev_priv->channel_io_t2h);

		FTHD_ISP_REG_WRITE(0x8042006, ISP_FW_HEAP_SIZE);

		for (retries = 0; retries < 1000; retries++) {
			reg = FTHD_ISP_REG_READ(ISP_FW_HEAP_SIZE);
			if (!reg)
				break;
			mdelay(10);
		}

		if (retries >= 1000) {
			dev_info(&dev_priv->pdev->dev, "Init failed! No magic value\n");
			isp_uninit(dev_priv);
			return -EIO;
		} /* FIXME: free on error path */
		dev_info(&dev_priv->pdev->dev, "magic value: %08x after %d ms\n", reg, (retries - 1) * 10);
	}

	return 0;
}
