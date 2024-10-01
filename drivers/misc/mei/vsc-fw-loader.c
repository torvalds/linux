// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Intel Corporation.
 * Intel Visual Sensing Controller Transport Layer Linux driver
 */

#include <linux/acpi.h>
#include <linux/align.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/firmware.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>
#include <linux/types.h>

#include <asm-generic/unaligned.h>

#include "vsc-tp.h"

#define VSC_MAGIC_NUM			0x49505343 /* IPSC */
#define VSC_MAGIC_FW			0x49574653 /* IWFS */
#define VSC_MAGIC_FILE			0x46564353 /* FVCS */

#define VSC_ADDR_BASE			0xE0030000
#define VSC_EFUSE_ADDR			(VSC_ADDR_BASE + 0x038)
#define VSC_STRAP_ADDR			(VSC_ADDR_BASE + 0x100)

#define VSC_MAINSTEPPING_VERSION_MASK	GENMASK(7, 4)
#define VSC_MAINSTEPPING_VERSION_A	0

#define VSC_SUBSTEPPING_VERSION_MASK	GENMASK(3, 0)
#define VSC_SUBSTEPPING_VERSION_0	0
#define VSC_SUBSTEPPING_VERSION_1	2

#define VSC_BOOT_IMG_OPTION_MASK	GENMASK(15, 0)

#define VSC_SKU_CFG_LOCATION		0x5001A000
#define VSC_SKU_MAX_SIZE		4100u

#define VSC_ACE_IMG_CNT			2
#define VSC_CSI_IMG_CNT			4
#define VSC_IMG_CNT_MAX			6

#define VSC_ROM_PKG_SIZE		256u
#define VSC_FW_PKG_SIZE			512u

#define VSC_IMAGE_DIR			"intel/vsc/"

#define VSC_CSI_IMAGE_NAME		VSC_IMAGE_DIR "ivsc_fw.bin"
#define VSC_ACE_IMAGE_NAME_FMT		VSC_IMAGE_DIR "ivsc_pkg_%s_0.bin"
#define VSC_CFG_IMAGE_NAME_FMT		VSC_IMAGE_DIR "ivsc_skucfg_%s_0_1.bin"

#define VSC_IMAGE_PATH_MAX_LEN		64

#define VSC_SENSOR_NAME_MAX_LEN		16

/* command id */
enum {
	VSC_CMD_QUERY = 0,
	VSC_CMD_DL_SET = 1,
	VSC_CMD_DL_START = 2,
	VSC_CMD_DL_CONT = 3,
	VSC_CMD_DUMP_MEM = 4,
	VSC_CMD_GET_CONT = 8,
	VSC_CMD_CAM_BOOT = 10,
};

/* command ack token */
enum {
	VSC_TOKEN_BOOTLOADER_REQ = 1,
	VSC_TOKEN_DUMP_RESP = 4,
	VSC_TOKEN_ERROR = 7,
};

/* image type */
enum {
	VSC_IMG_BOOTLOADER_TYPE = 1,
	VSC_IMG_CSI_EM7D_TYPE,
	VSC_IMG_CSI_SEM_TYPE,
	VSC_IMG_CSI_RUNTIME_TYPE,
	VSC_IMG_ACE_VISION_TYPE,
	VSC_IMG_ACE_CFG_TYPE,
	VSC_IMG_SKU_CFG_TYPE,
};

/* image fragments */
enum {
	VSC_IMG_BOOTLOADER_FRAG,
	VSC_IMG_CSI_SEM_FRAG,
	VSC_IMG_CSI_RUNTIME_FRAG,
	VSC_IMG_ACE_VISION_FRAG,
	VSC_IMG_ACE_CFG_FRAG,
	VSC_IMG_CSI_EM7D_FRAG,
	VSC_IMG_SKU_CFG_FRAG,
	VSC_IMG_FRAG_MAX
};

struct vsc_rom_cmd {
	__le32 magic;
	__u8 cmd_id;
	union {
		/* download start */
		struct {
			__u8 img_type;
			__le16 option;
			__le32 img_len;
			__le32 img_loc;
			__le32 crc;
			DECLARE_FLEX_ARRAY(__u8, res);
		} __packed dl_start;
		/* download set */
		struct {
			__u8 option;
			__le16 img_cnt;
			DECLARE_FLEX_ARRAY(__le32, payload);
		} __packed dl_set;
		/* download continue */
		struct {
			__u8 end_flag;
			__le16 len;
			/* 8 is the offset of payload */
			__u8 payload[VSC_ROM_PKG_SIZE - 8];
		} __packed dl_cont;
		/* dump memory */
		struct {
			__u8 res;
			__le16 len;
			__le32 addr;
			DECLARE_FLEX_ARRAY(__u8, payload);
		} __packed dump_mem;
		/* 5 is the offset of padding */
		__u8 padding[VSC_ROM_PKG_SIZE - 5];
	} data;
};

struct vsc_rom_cmd_ack {
	__le32 magic;
	__u8 token;
	__u8 type;
	__u8 res[2];
	__u8 payload[];
};

struct vsc_fw_cmd {
	__le32 magic;
	__u8 cmd_id;
	union {
		struct {
			__le16 option;
			__u8 img_type;
			__le32 img_len;
			__le32 img_loc;
			__le32 crc;
			DECLARE_FLEX_ARRAY(__u8, res);
		} __packed dl_start;
		struct {
			__le16 option;
			__u8 img_cnt;
			DECLARE_FLEX_ARRAY(__le32, payload);
		} __packed dl_set;
		struct {
			__le32 addr;
			__u8 len;
			DECLARE_FLEX_ARRAY(__u8, payload);
		} __packed dump_mem;
		struct {
			__u8 resv[3];
			__le32 crc;
			DECLARE_FLEX_ARRAY(__u8, payload);
		} __packed boot;
		/* 5 is the offset of padding */
		__u8 padding[VSC_FW_PKG_SIZE - 5];
	} data;
};

struct vsc_img {
	__le32 magic;
	__le32 option;
	__le32 image_count;
	__le32 image_location[VSC_IMG_CNT_MAX];
};

struct vsc_fw_sign {
	__le32 magic;
	__le32 image_size;
	__u8 image[];
};

struct vsc_image_code_data {
	/* fragment index */
	u8 frag_index;
	/* image type */
	u8 image_type;
};

struct vsc_img_frag {
	u8 type;
	u32 location;
	const u8 *data;
	u32 size;
};

/**
 * struct vsc_fw_loader - represent vsc firmware loader
 * @dev: device used to request firmware
 * @tp: transport layer used with the firmware loader
 * @csi: CSI image
 * @ace: ACE image
 * @cfg: config image
 * @tx_buf: tx buffer
 * @rx_buf: rx buffer
 * @option: command option
 * @count: total image count
 * @sensor_name: camera sensor name
 * @frags: image fragments
 */
struct vsc_fw_loader {
	struct device *dev;
	struct vsc_tp *tp;

	const struct firmware *csi;
	const struct firmware *ace;
	const struct firmware *cfg;

	void *tx_buf;
	void *rx_buf;

	u16 option;
	u16 count;

	char sensor_name[VSC_SENSOR_NAME_MAX_LEN];

	struct vsc_img_frag frags[VSC_IMG_FRAG_MAX];
};

static inline u32 vsc_sum_crc(void *data, size_t size)
{
	u32 crc = 0;
	size_t i;

	for (i = 0; i < size; i++)
		crc += *((u8 *)data + i);

	return crc;
}

/* get sensor name to construct image name */
static int vsc_get_sensor_name(struct vsc_fw_loader *fw_loader,
			       struct device *dev)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER };
	union acpi_object obj = {
		.integer.type = ACPI_TYPE_INTEGER,
		.integer.value = 1,
	};
	struct acpi_object_list arg_list = {
		.count = 1,
		.pointer = &obj,
	};
	union acpi_object *ret_obj;
	acpi_handle handle;
	acpi_status status;
	int ret = 0;

	handle = ACPI_HANDLE(dev);
	if (!handle)
		return -EINVAL;

	status = acpi_evaluate_object(handle, "SID", &arg_list, &buffer);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "can't evaluate SID method: %d\n", status);
		return -ENODEV;
	}

	ret_obj = buffer.pointer;
	if (!ret_obj) {
		dev_err(dev, "can't locate ACPI buffer\n");
		return -ENODEV;
	}

	if (ret_obj->type != ACPI_TYPE_STRING) {
		dev_err(dev, "found non-string entry\n");
		ret = -ENODEV;
		goto out_free_buff;
	}

	/* string length excludes trailing NUL */
	if (ret_obj->string.length >= sizeof(fw_loader->sensor_name)) {
		dev_err(dev, "sensor name buffer too small\n");
		ret = -EINVAL;
		goto out_free_buff;
	}

	memcpy(fw_loader->sensor_name, ret_obj->string.pointer,
	       ret_obj->string.length);

	string_lower(fw_loader->sensor_name, fw_loader->sensor_name);

out_free_buff:
	ACPI_FREE(buffer.pointer);

	return ret;
}

static int vsc_identify_silicon(struct vsc_fw_loader *fw_loader)
{
	struct vsc_rom_cmd_ack *ack = fw_loader->rx_buf;
	struct vsc_rom_cmd *cmd = fw_loader->tx_buf;
	u8 version, sub_version;
	int ret;

	/* identify stepping information */
	cmd->magic = cpu_to_le32(VSC_MAGIC_NUM);
	cmd->cmd_id = VSC_CMD_DUMP_MEM;
	cmd->data.dump_mem.addr = cpu_to_le32(VSC_EFUSE_ADDR);
	cmd->data.dump_mem.len = cpu_to_le16(sizeof(__le32));
	ret = vsc_tp_rom_xfer(fw_loader->tp, cmd, ack, VSC_ROM_PKG_SIZE);
	if (ret)
		return ret;
	if (ack->token == VSC_TOKEN_ERROR)
		return -EINVAL;

	cmd->magic = cpu_to_le32(VSC_MAGIC_NUM);
	cmd->cmd_id = VSC_CMD_GET_CONT;
	ret = vsc_tp_rom_xfer(fw_loader->tp, cmd, ack, VSC_ROM_PKG_SIZE);
	if (ret)
		return ret;
	if (ack->token != VSC_TOKEN_DUMP_RESP)
		return -EINVAL;

	version = FIELD_GET(VSC_MAINSTEPPING_VERSION_MASK, ack->payload[0]);
	sub_version = FIELD_GET(VSC_SUBSTEPPING_VERSION_MASK, ack->payload[0]);

	if (version != VSC_MAINSTEPPING_VERSION_A)
		return -EINVAL;

	if (sub_version != VSC_SUBSTEPPING_VERSION_0 &&
	    sub_version != VSC_SUBSTEPPING_VERSION_1)
		return -EINVAL;

	dev_info(fw_loader->dev, "silicon stepping version is %u:%u\n",
		 version, sub_version);

	/* identify strap information */
	cmd->magic = cpu_to_le32(VSC_MAGIC_NUM);
	cmd->cmd_id = VSC_CMD_DUMP_MEM;
	cmd->data.dump_mem.addr = cpu_to_le32(VSC_STRAP_ADDR);
	cmd->data.dump_mem.len = cpu_to_le16(sizeof(__le32));
	ret = vsc_tp_rom_xfer(fw_loader->tp, cmd, ack, VSC_ROM_PKG_SIZE);
	if (ret)
		return ret;
	if (ack->token == VSC_TOKEN_ERROR)
		return -EINVAL;

	cmd->magic = cpu_to_le32(VSC_MAGIC_NUM);
	cmd->cmd_id = VSC_CMD_GET_CONT;
	ret = vsc_tp_rom_xfer(fw_loader->tp, cmd, ack, VSC_ROM_PKG_SIZE);
	if (ret)
		return ret;
	if (ack->token != VSC_TOKEN_DUMP_RESP)
		return -EINVAL;

	return 0;
}

static int vsc_identify_csi_image(struct vsc_fw_loader *fw_loader)
{
	const struct firmware *image;
	struct vsc_fw_sign *sign;
	struct vsc_img *img;
	unsigned int i;
	int ret;

	ret = request_firmware(&image, VSC_CSI_IMAGE_NAME, fw_loader->dev);
	if (ret)
		return ret;

	img = (struct vsc_img *)image->data;
	if (!img) {
		ret = -ENOENT;
		goto err_release_image;
	}

	if (le32_to_cpu(img->magic) != VSC_MAGIC_FILE) {
		ret = -EINVAL;
		goto err_release_image;
	}

	if (le32_to_cpu(img->image_count) != VSC_CSI_IMG_CNT) {
		ret = -EINVAL;
		goto err_release_image;
	}
	fw_loader->count += le32_to_cpu(img->image_count) - 1;

	fw_loader->option =
		FIELD_GET(VSC_BOOT_IMG_OPTION_MASK, le32_to_cpu(img->option));

	sign = (struct vsc_fw_sign *)
		(img->image_location + le32_to_cpu(img->image_count));

	for (i = 0; i < VSC_CSI_IMG_CNT; i++) {
		/* mapping from CSI image index to image code data */
		static const struct vsc_image_code_data csi_image_map[] = {
			{ VSC_IMG_BOOTLOADER_FRAG, VSC_IMG_BOOTLOADER_TYPE },
			{ VSC_IMG_CSI_SEM_FRAG, VSC_IMG_CSI_SEM_TYPE },
			{ VSC_IMG_CSI_RUNTIME_FRAG, VSC_IMG_CSI_RUNTIME_TYPE },
			{ VSC_IMG_CSI_EM7D_FRAG, VSC_IMG_CSI_EM7D_TYPE },
		};
		struct vsc_img_frag *frag;

		if ((u8 *)sign + sizeof(*sign) > image->data + image->size) {
			ret = -EINVAL;
			goto err_release_image;
		}

		if (le32_to_cpu(sign->magic) != VSC_MAGIC_FW) {
			ret = -EINVAL;
			goto err_release_image;
		}

		if (!le32_to_cpu(img->image_location[i])) {
			ret = -EINVAL;
			goto err_release_image;
		}

		frag = &fw_loader->frags[csi_image_map[i].frag_index];

		frag->data = sign->image;
		frag->size = le32_to_cpu(sign->image_size);
		frag->location = le32_to_cpu(img->image_location[i]);
		frag->type = csi_image_map[i].image_type;

		sign = (struct vsc_fw_sign *)
			(sign->image + le32_to_cpu(sign->image_size));
	}

	fw_loader->csi = image;

	return 0;

err_release_image:
	release_firmware(image);

	return ret;
}

static int vsc_identify_ace_image(struct vsc_fw_loader *fw_loader)
{
	char path[VSC_IMAGE_PATH_MAX_LEN];
	const struct firmware *image;
	struct vsc_fw_sign *sign;
	struct vsc_img *img;
	unsigned int i;
	int ret;

	snprintf(path, sizeof(path), VSC_ACE_IMAGE_NAME_FMT,
		 fw_loader->sensor_name);

	ret = request_firmware(&image, path, fw_loader->dev);
	if (ret)
		return ret;

	img = (struct vsc_img *)image->data;
	if (!img) {
		ret = -ENOENT;
		goto err_release_image;
	}

	if (le32_to_cpu(img->magic) != VSC_MAGIC_FILE) {
		ret = -EINVAL;
		goto err_release_image;
	}

	if (le32_to_cpu(img->image_count) != VSC_ACE_IMG_CNT) {
		ret = -EINVAL;
		goto err_release_image;
	}
	fw_loader->count += le32_to_cpu(img->image_count);

	sign = (struct vsc_fw_sign *)
		(img->image_location + le32_to_cpu(img->image_count));

	for (i = 0; i < VSC_ACE_IMG_CNT; i++) {
		/* mapping from ACE image index to image code data */
		static const struct vsc_image_code_data ace_image_map[] = {
			{ VSC_IMG_ACE_VISION_FRAG, VSC_IMG_ACE_VISION_TYPE },
			{ VSC_IMG_ACE_CFG_FRAG, VSC_IMG_ACE_CFG_TYPE },
		};
		struct vsc_img_frag *frag, *last_frag;
		u8 frag_index;

		if ((u8 *)sign + sizeof(*sign) > image->data + image->size) {
			ret = -EINVAL;
			goto err_release_image;
		}

		if (le32_to_cpu(sign->magic) != VSC_MAGIC_FW) {
			ret = -EINVAL;
			goto err_release_image;
		}

		frag_index = ace_image_map[i].frag_index;
		frag = &fw_loader->frags[frag_index];

		frag->data = sign->image;
		frag->size = le32_to_cpu(sign->image_size);
		frag->location = le32_to_cpu(img->image_location[i]);
		frag->type = ace_image_map[i].image_type;

		if (!frag->location) {
			last_frag = &fw_loader->frags[frag_index - 1];
			frag->location =
				ALIGN(last_frag->location + last_frag->size, SZ_4K);
		}

		sign = (struct vsc_fw_sign *)
			(sign->image + le32_to_cpu(sign->image_size));
	}

	fw_loader->ace = image;

	return 0;

err_release_image:
	release_firmware(image);

	return ret;
}

static int vsc_identify_cfg_image(struct vsc_fw_loader *fw_loader)
{
	struct vsc_img_frag *frag = &fw_loader->frags[VSC_IMG_SKU_CFG_FRAG];
	char path[VSC_IMAGE_PATH_MAX_LEN];
	const struct firmware *image;
	u32 size;
	int ret;

	snprintf(path, sizeof(path), VSC_CFG_IMAGE_NAME_FMT,
		 fw_loader->sensor_name);

	ret = request_firmware(&image, path, fw_loader->dev);
	if (ret)
		return ret;

	/* identify image size */
	if (image->size <= sizeof(u32) || image->size > VSC_SKU_MAX_SIZE) {
		ret = -EINVAL;
		goto err_release_image;
	}

	size = le32_to_cpu(*((__le32 *)image->data)) + sizeof(u32);
	if (image->size != size) {
		ret = -EINVAL;
		goto err_release_image;
	}

	frag->data = image->data;
	frag->size = image->size;
	frag->type = VSC_IMG_SKU_CFG_TYPE;
	frag->location = VSC_SKU_CFG_LOCATION;

	fw_loader->cfg = image;

	return 0;

err_release_image:
	release_firmware(image);

	return ret;
}

static int vsc_download_bootloader(struct vsc_fw_loader *fw_loader)
{
	struct vsc_img_frag *frag = &fw_loader->frags[VSC_IMG_BOOTLOADER_FRAG];
	struct vsc_rom_cmd_ack *ack = fw_loader->rx_buf;
	struct vsc_rom_cmd *cmd = fw_loader->tx_buf;
	u32 len, c_len;
	size_t remain;
	const u8 *p;
	int ret;

	cmd->magic = cpu_to_le32(VSC_MAGIC_NUM);
	cmd->cmd_id = VSC_CMD_QUERY;
	ret = vsc_tp_rom_xfer(fw_loader->tp, cmd, ack, VSC_ROM_PKG_SIZE);
	if (ret)
		return ret;
	if (ack->token != VSC_TOKEN_DUMP_RESP &&
	    ack->token != VSC_TOKEN_BOOTLOADER_REQ)
		return -EINVAL;

	cmd->magic = cpu_to_le32(VSC_MAGIC_NUM);
	cmd->cmd_id = VSC_CMD_DL_START;
	cmd->data.dl_start.option = cpu_to_le16(fw_loader->option);
	cmd->data.dl_start.img_type = frag->type;
	cmd->data.dl_start.img_len = cpu_to_le32(frag->size);
	cmd->data.dl_start.img_loc = cpu_to_le32(frag->location);

	c_len = offsetof(struct vsc_rom_cmd, data.dl_start.crc);
	cmd->data.dl_start.crc = cpu_to_le32(vsc_sum_crc(cmd, c_len));

	ret = vsc_tp_rom_xfer(fw_loader->tp, cmd, NULL, VSC_ROM_PKG_SIZE);
	if (ret)
		return ret;

	p = frag->data;
	remain = frag->size;

	/* download image data */
	while (remain > 0) {
		len = min(remain, sizeof(cmd->data.dl_cont.payload));

		cmd->magic = cpu_to_le32(VSC_MAGIC_NUM);
		cmd->cmd_id = VSC_CMD_DL_CONT;
		cmd->data.dl_cont.len = cpu_to_le16(len);
		cmd->data.dl_cont.end_flag = remain == len;
		memcpy(cmd->data.dl_cont.payload, p, len);

		ret = vsc_tp_rom_xfer(fw_loader->tp, cmd, NULL, VSC_ROM_PKG_SIZE);
		if (ret)
			return ret;

		p += len;
		remain -= len;
	}

	return 0;
}

static int vsc_download_firmware(struct vsc_fw_loader *fw_loader)
{
	struct vsc_fw_cmd *cmd = fw_loader->tx_buf;
	unsigned int i, index = 0;
	u32 c_len;
	int ret;

	cmd->magic = cpu_to_le32(VSC_MAGIC_NUM);
	cmd->cmd_id = VSC_CMD_DL_SET;
	cmd->data.dl_set.img_cnt = cpu_to_le16(fw_loader->count);
	put_unaligned_le16(fw_loader->option, &cmd->data.dl_set.option);

	for (i = VSC_IMG_CSI_SEM_FRAG; i <= VSC_IMG_CSI_EM7D_FRAG; i++) {
		struct vsc_img_frag *frag = &fw_loader->frags[i];

		cmd->data.dl_set.payload[index++] = cpu_to_le32(frag->location);
		cmd->data.dl_set.payload[index++] = cpu_to_le32(frag->size);
	}

	c_len = offsetof(struct vsc_fw_cmd, data.dl_set.payload[index]);
	cmd->data.dl_set.payload[index] = cpu_to_le32(vsc_sum_crc(cmd, c_len));

	ret = vsc_tp_rom_xfer(fw_loader->tp, cmd, NULL, VSC_FW_PKG_SIZE);
	if (ret)
		return ret;

	for (i = VSC_IMG_CSI_SEM_FRAG; i < VSC_IMG_FRAG_MAX; i++) {
		struct vsc_img_frag *frag = &fw_loader->frags[i];
		const u8 *p;
		u32 remain;

		cmd->magic = cpu_to_le32(VSC_MAGIC_NUM);
		cmd->cmd_id = VSC_CMD_DL_START;
		cmd->data.dl_start.img_type = frag->type;
		cmd->data.dl_start.img_len = cpu_to_le32(frag->size);
		cmd->data.dl_start.img_loc = cpu_to_le32(frag->location);
		put_unaligned_le16(fw_loader->option, &cmd->data.dl_start.option);

		c_len = offsetof(struct vsc_fw_cmd, data.dl_start.crc);
		cmd->data.dl_start.crc = cpu_to_le32(vsc_sum_crc(cmd, c_len));

		ret = vsc_tp_rom_xfer(fw_loader->tp, cmd, NULL, VSC_FW_PKG_SIZE);
		if (ret)
			return ret;

		p = frag->data;
		remain = frag->size;

		/* download image data */
		while (remain > 0) {
			u32 len = min(remain, VSC_FW_PKG_SIZE);

			memcpy(fw_loader->tx_buf, p, len);
			memset(fw_loader->tx_buf + len, 0, VSC_FW_PKG_SIZE - len);

			ret = vsc_tp_rom_xfer(fw_loader->tp, fw_loader->tx_buf,
					      NULL, VSC_FW_PKG_SIZE);
			if (ret)
				break;

			p += len;
			remain -= len;
		}
	}

	cmd->magic = cpu_to_le32(VSC_MAGIC_NUM);
	cmd->cmd_id = VSC_CMD_CAM_BOOT;

	c_len = offsetof(struct vsc_fw_cmd, data.dl_start.crc);
	cmd->data.boot.crc = cpu_to_le32(vsc_sum_crc(cmd, c_len));

	return vsc_tp_rom_xfer(fw_loader->tp, cmd, NULL, VSC_FW_PKG_SIZE);
}

/**
 * vsc_tp_init - init vsc_tp
 * @tp: vsc_tp device handle
 * @dev: device node for mei vsc device
 * Return: 0 in case of success, negative value in case of error
 */
int vsc_tp_init(struct vsc_tp *tp, struct device *dev)
{
	struct vsc_fw_loader *fw_loader __free(kfree) = NULL;
	void *tx_buf __free(kfree) = NULL;
	void *rx_buf __free(kfree) = NULL;
	int ret;

	fw_loader = kzalloc(sizeof(*fw_loader), GFP_KERNEL);
	if (!fw_loader)
		return -ENOMEM;

	tx_buf = kzalloc(VSC_FW_PKG_SIZE, GFP_KERNEL);
	if (!tx_buf)
		return -ENOMEM;

	rx_buf = kzalloc(VSC_FW_PKG_SIZE, GFP_KERNEL);
	if (!rx_buf)
		return -ENOMEM;

	fw_loader->tx_buf = tx_buf;
	fw_loader->rx_buf = rx_buf;

	fw_loader->tp = tp;
	fw_loader->dev = dev;

	ret = vsc_get_sensor_name(fw_loader, dev);
	if (ret)
		return ret;

	ret = vsc_identify_silicon(fw_loader);
	if (ret)
		return ret;

	ret = vsc_identify_csi_image(fw_loader);
	if (ret)
		return ret;

	ret = vsc_identify_ace_image(fw_loader);
	if (ret)
		goto err_release_csi;

	ret = vsc_identify_cfg_image(fw_loader);
	if (ret)
		goto err_release_ace;

	ret = vsc_download_bootloader(fw_loader);
	if (!ret)
		ret = vsc_download_firmware(fw_loader);

	release_firmware(fw_loader->cfg);

err_release_ace:
	release_firmware(fw_loader->ace);

err_release_csi:
	release_firmware(fw_loader->csi);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(vsc_tp_init, VSC_TP);
