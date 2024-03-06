// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/string.h> /* for memcpy() */
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "hmm.h"

#include <math_support.h>
#include "platform_support.h"
#include "sh_css_firmware.h"

#include "sh_css_defs.h"
#include "ia_css_debug.h"
#include "sh_css_internal.h"
#include "ia_css_isp_param.h"

#include "assert_support.h"

#include "isp.h"				/* PMEM_WIDTH_LOG2 */

#include "ia_css_isp_params.h"
#include "ia_css_isp_configs.h"
#include "ia_css_isp_states.h"

#define _STR(x) #x
#define STR(x) _STR(x)

struct firmware_header {
	struct sh_css_fw_bi_file_h file_header;
	struct ia_css_fw_info      binary_header;
};

struct fw_param {
	const char *name;
	const void *buffer;
};

static struct firmware_header *firmware_header;

/*
 * The string STR is a place holder
 * which will be replaced with the actual RELEASE_VERSION
 * during package generation. Please do not modify
 */
static const char *release_version_2401 = STR(irci_stable_candrpv_0415_20150521_0458);
static const char *release_version_2400 = STR(irci_stable_candrpv_0415_20150423_1753);

#define MAX_FW_REL_VER_NAME	300
static char FW_rel_ver_name[MAX_FW_REL_VER_NAME] = "---";

struct ia_css_fw_info	  sh_css_sp_fw;
struct ia_css_blob_descr *sh_css_blob_info; /* Only ISP blob info (no SP) */
unsigned int sh_css_num_binaries; /* This includes 1 SP binary */

static struct fw_param *fw_minibuffer;

char *sh_css_get_fw_version(void)
{
	return FW_rel_ver_name;
}

/*
 * Split the loaded firmware into blobs
 */

/* Setup sp/sp1 binary */
static int
setup_binary(struct ia_css_fw_info *fw, const char *fw_data,
	     struct ia_css_fw_info *sh_css_fw, unsigned int binary_id)
{
	const char *blob_data;

	if ((!fw) || (!fw_data))
		return -EINVAL;

	blob_data = fw_data + fw->blob.offset;

	*sh_css_fw = *fw;

	sh_css_fw->blob.code = vmalloc(fw->blob.size);
	if (!sh_css_fw->blob.code)
		return -ENOMEM;

	memcpy((void *)sh_css_fw->blob.code, blob_data, fw->blob.size);
	sh_css_fw->blob.data = (char *)sh_css_fw->blob.code + fw->blob.data_source;
	fw_minibuffer[binary_id].buffer = sh_css_fw->blob.code;

	return 0;
}

int
sh_css_load_blob_info(const char *fw, const struct ia_css_fw_info *bi,
		      struct ia_css_blob_descr *bd,
		      unsigned int index)
{
	const char *name;
	const unsigned char *blob;

	if ((!fw) || (!bd))
		return -EINVAL;

	/* Special case: only one binary in fw */
	if (!bi)
		bi = (const struct ia_css_fw_info *)fw;

	name = fw + bi->blob.prog_name_offset;
	blob = (const unsigned char *)fw + bi->blob.offset;

	/* sanity check */
	if (bi->blob.size !=
		bi->blob.text_size + bi->blob.icache_size +
			bi->blob.data_size + bi->blob.padding_size) {
		/* sanity check, note the padding bytes added for section to DDR alignment */
		return -EINVAL;
	}

	if ((bi->blob.offset % (1UL << (ISP_PMEM_WIDTH_LOG2 - 3))) != 0)
		return -EINVAL;

	bd->blob = blob;
	bd->header = *bi;

	if (bi->type == ia_css_isp_firmware || bi->type == ia_css_sp_firmware) {
		char *namebuffer;

		namebuffer = kstrdup(name, GFP_KERNEL);
		if (!namebuffer)
			return -ENOMEM;
		bd->name = fw_minibuffer[index].name = namebuffer;
	} else {
		bd->name = name;
	}

	if (bi->type == ia_css_isp_firmware) {
		size_t paramstruct_size = sizeof(struct ia_css_memory_offsets);
		size_t configstruct_size = sizeof(struct ia_css_config_memory_offsets);
		size_t statestruct_size = sizeof(struct ia_css_state_memory_offsets);

		char *parambuf = kmalloc(paramstruct_size + configstruct_size +
					 statestruct_size,
					 GFP_KERNEL);
		if (!parambuf)
			return -ENOMEM;

		bd->mem_offsets.array[IA_CSS_PARAM_CLASS_PARAM].ptr = NULL;
		bd->mem_offsets.array[IA_CSS_PARAM_CLASS_CONFIG].ptr = NULL;
		bd->mem_offsets.array[IA_CSS_PARAM_CLASS_STATE].ptr = NULL;

		fw_minibuffer[index].buffer = parambuf;

		/* copy ia_css_memory_offsets */
		memcpy(parambuf, (void *)(fw +
					  bi->blob.memory_offsets.offsets[IA_CSS_PARAM_CLASS_PARAM]),
		       paramstruct_size);
		bd->mem_offsets.array[IA_CSS_PARAM_CLASS_PARAM].ptr = parambuf;

		/* copy ia_css_config_memory_offsets */
		memcpy(parambuf + paramstruct_size,
		       (void *)(fw + bi->blob.memory_offsets.offsets[IA_CSS_PARAM_CLASS_CONFIG]),
		       configstruct_size);
		bd->mem_offsets.array[IA_CSS_PARAM_CLASS_CONFIG].ptr = parambuf +
		paramstruct_size;

		/* copy ia_css_state_memory_offsets */
		memcpy(parambuf + paramstruct_size + configstruct_size,
		       (void *)(fw + bi->blob.memory_offsets.offsets[IA_CSS_PARAM_CLASS_STATE]),
		       statestruct_size);
		bd->mem_offsets.array[IA_CSS_PARAM_CLASS_STATE].ptr = parambuf +
		paramstruct_size + configstruct_size;
	}
	return 0;
}

bool
sh_css_check_firmware_version(struct device *dev, const char *fw_data)
{
	const char *release_version;
	struct sh_css_fw_bi_file_h *file_header;

	if (IS_ISP2401)
		release_version = release_version_2401;
	else
		release_version = release_version_2400;

	firmware_header = (struct firmware_header *)fw_data;
	file_header = &firmware_header->file_header;

	if (strcmp(file_header->version, release_version) != 0) {
		dev_err(dev, "Firmware version may not be compatible with this driver\n");
		dev_err(dev, "Expecting version '%s', but firmware is '%s'.\n",
			release_version, file_header->version);
	}

	/* For now, let's just accept a wrong version, even if wrong */
	return false;
}

static const char * const fw_type_name[] = {
	[ia_css_sp_firmware]		= "SP",
	[ia_css_isp_firmware]		= "ISP",
	[ia_css_bootloader_firmware]	= "BootLoader",
	[ia_css_acc_firmware]		= "accel",
};

static const char * const fw_acc_type_name[] = {
	[IA_CSS_ACC_NONE] =		"Normal",
	[IA_CSS_ACC_OUTPUT] =		"Accel for output",
	[IA_CSS_ACC_VIEWFINDER] =	"Accel for viewfinder",
	[IA_CSS_ACC_STANDALONE] =	"Stand-alone accel",
};

int
sh_css_load_firmware(struct device *dev, const char *fw_data,
		     unsigned int fw_size)
{
	unsigned int i;
	const char *release_version;
	struct ia_css_fw_info *binaries;
	struct sh_css_fw_bi_file_h *file_header;
	int ret;

	/* some sanity checks */
	if (!fw_data || fw_size < sizeof(struct sh_css_fw_bi_file_h))
		return -EINVAL;

	firmware_header = (struct firmware_header *)fw_data;
	file_header = &firmware_header->file_header;

	if (file_header->h_size != sizeof(struct sh_css_fw_bi_file_h))
		return -EINVAL;

	binaries = &firmware_header->binary_header;
	strscpy(FW_rel_ver_name, file_header->version,
		min(sizeof(FW_rel_ver_name), sizeof(file_header->version)));
	if (IS_ISP2401)
		release_version = release_version_2401;
	else
		release_version = release_version_2400;
	ret = sh_css_check_firmware_version(dev, fw_data);
	if (ret) {
		IA_CSS_ERROR("CSS code version (%s) and firmware version (%s) mismatch!",
			     file_header->version, release_version);
		return -EINVAL;
	} else {
		IA_CSS_LOG("successfully load firmware version %s", release_version);
	}

	sh_css_num_binaries = file_header->binary_nr;
	/* Only allocate memory for ISP blob info */
	if (sh_css_num_binaries > NUM_OF_SPS) {
		sh_css_blob_info = kmalloc(
		    (sh_css_num_binaries - NUM_OF_SPS) *
		    sizeof(*sh_css_blob_info), GFP_KERNEL);
		if (!sh_css_blob_info)
			return -ENOMEM;
	} else {
		sh_css_blob_info = NULL;
	}

	fw_minibuffer = kcalloc(sh_css_num_binaries, sizeof(struct fw_param),
				GFP_KERNEL);
	if (!fw_minibuffer)
		return -ENOMEM;

	for (i = 0; i < sh_css_num_binaries; i++) {
		struct ia_css_fw_info *bi = &binaries[i];
		/*
		 * note: the var below is made static as it is quite large;
		 * if it is not static it ends up on the stack which could
		 * cause issues for drivers
		 */
		static struct ia_css_blob_descr bd;
		int err;

		err = sh_css_load_blob_info(fw_data, bi, &bd, i);

		if (err)
			return -EINVAL;

		if (bi->blob.offset + bi->blob.size > fw_size)
			return -EINVAL;

		switch (bd.header.type) {
		case ia_css_isp_firmware:
			if (bd.header.info.isp.type > IA_CSS_ACC_STANDALONE) {
				dev_err(dev, "binary #%2d: invalid SP type\n",
					i);
				return -EINVAL;
			}

			dev_dbg(dev,
				"binary #%-2d type %s (%s), binary id is %2d: %s\n",
				i,
				fw_type_name[bd.header.type],
				fw_acc_type_name[bd.header.info.isp.type],
				bd.header.info.isp.sp.id,
				bd.name);
			break;
		case ia_css_sp_firmware:
		case ia_css_bootloader_firmware:
		case ia_css_acc_firmware:
			dev_dbg(dev,
				"binary #%-2d type %s: %s\n",
				i, fw_type_name[bd.header.type],
				bd.name);
			break;
		default:
			if (bd.header.info.isp.type > IA_CSS_ACC_STANDALONE) {
				dev_err(dev,
					"binary #%2d: invalid firmware type\n",
					i);
				return -EINVAL;
			}
			break;
		}

		if (bi->type == ia_css_sp_firmware) {
			if (i != SP_FIRMWARE)
				return -EINVAL;
			err = setup_binary(bi, fw_data, &sh_css_sp_fw, i);
			if (err)
				return err;

		} else {
			/*
			 * All subsequent binaries
			 * (including bootloaders) (i>NUM_OF_SPS)
			 * are ISP firmware
			 */
			if (i < NUM_OF_SPS)
				return -EINVAL;

			if (bi->type != ia_css_isp_firmware)
				return -EINVAL;
			if (!sh_css_blob_info) /* cannot happen but KW does not see this */
				return -EINVAL;
			sh_css_blob_info[i - NUM_OF_SPS] = bd;
		}
	}

	return 0;
}

void sh_css_unload_firmware(void)
{
	/* release firmware minibuffer */
	if (fw_minibuffer) {
		unsigned int i = 0;

		for (i = 0; i < sh_css_num_binaries; i++) {
			kfree(fw_minibuffer[i].name);
			kvfree(fw_minibuffer[i].buffer);
		}
		kfree(fw_minibuffer);
		fw_minibuffer = NULL;
	}

	memset(&sh_css_sp_fw, 0, sizeof(sh_css_sp_fw));
	kfree(sh_css_blob_info);
	sh_css_blob_info = NULL;
	sh_css_num_binaries = 0;
}

ia_css_ptr
sh_css_load_blob(const unsigned char *blob, unsigned int size)
{
	ia_css_ptr target_addr = hmm_alloc(size);
	/*
	 * this will allocate memory aligned to a DDR word boundary which
	 * is required for the CSS DMA to read the instructions.
	 */

	assert(blob);
	if (target_addr)
		hmm_store(target_addr, blob, size);
	return target_addr;
}
