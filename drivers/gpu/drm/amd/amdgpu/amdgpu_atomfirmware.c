/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "atom.h"
#include "atombios.h"
#include "soc15_hw_ip.h"

union firmware_info {
	struct atom_firmware_info_v3_1 v31;
	struct atom_firmware_info_v3_2 v32;
	struct atom_firmware_info_v3_3 v33;
	struct atom_firmware_info_v3_4 v34;
};

/*
 * Helper function to query firmware capability
 *
 * @adev: amdgpu_device pointer
 *
 * Return firmware_capability in firmwareinfo table on success or 0 if not
 */
uint32_t amdgpu_atomfirmware_query_firmware_capability(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index;
	u16 data_offset, size;
	union firmware_info *firmware_info;
	u8 frev, crev;
	u32 fw_cap = 0;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
			firmwareinfo);

	if (amdgpu_atom_parse_data_header(adev->mode_info.atom_context,
				index, &size, &frev, &crev, &data_offset)) {
		/* support firmware_info 3.1 + */
		if ((frev == 3 && crev >=1) || (frev > 3)) {
			firmware_info = (union firmware_info *)
				(mode_info->atom_context->bios + data_offset);
			fw_cap = le32_to_cpu(firmware_info->v31.firmware_capability);
		}
	}

	return fw_cap;
}

/*
 * Helper function to query gpu virtualizaiton capability
 *
 * @adev: amdgpu_device pointer
 *
 * Return true if gpu virtualization is supported or false if not
 */
bool amdgpu_atomfirmware_gpu_virtualization_supported(struct amdgpu_device *adev)
{
	u32 fw_cap;

	fw_cap = adev->mode_info.firmware_flags;

	return (fw_cap & ATOM_FIRMWARE_CAP_GPU_VIRTUALIZATION) ? true : false;
}

void amdgpu_atomfirmware_scratch_regs_init(struct amdgpu_device *adev)
{
	int index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
						firmwareinfo);
	uint16_t data_offset;

	if (amdgpu_atom_parse_data_header(adev->mode_info.atom_context, index, NULL,
					  NULL, NULL, &data_offset)) {
		struct atom_firmware_info_v3_1 *firmware_info =
			(struct atom_firmware_info_v3_1 *)(adev->mode_info.atom_context->bios +
							   data_offset);

		adev->bios_scratch_reg_offset =
			le32_to_cpu(firmware_info->bios_scratch_reg_startaddr);
	}
}

static int amdgpu_atomfirmware_allocate_fb_v2_1(struct amdgpu_device *adev,
	struct vram_usagebyfirmware_v2_1 *fw_usage, int *usage_bytes)
{
	u32 start_addr, fw_size, drv_size;

	start_addr = le32_to_cpu(fw_usage->start_address_in_kb);
	fw_size = le16_to_cpu(fw_usage->used_by_firmware_in_kb);
	drv_size = le16_to_cpu(fw_usage->used_by_driver_in_kb);

	DRM_DEBUG("atom firmware v2_1 requested %08x %dkb fw %dkb drv\n",
			  start_addr,
			  fw_size,
			  drv_size);

	if ((start_addr & ATOM_VRAM_OPERATION_FLAGS_MASK) ==
		(u32)(ATOM_VRAM_BLOCK_SRIOV_MSG_SHARE_RESERVATION <<
		ATOM_VRAM_OPERATION_FLAGS_SHIFT)) {
		/* Firmware request VRAM reservation for SR-IOV */
		adev->mman.fw_vram_usage_start_offset = (start_addr &
			(~ATOM_VRAM_OPERATION_FLAGS_MASK)) << 10;
		adev->mman.fw_vram_usage_size = fw_size << 10;
		/* Use the default scratch size */
		*usage_bytes = 0;
	} else {
		*usage_bytes = drv_size << 10;
	}
	return 0;
}

static int amdgpu_atomfirmware_allocate_fb_v2_2(struct amdgpu_device *adev,
		struct vram_usagebyfirmware_v2_2 *fw_usage, int *usage_bytes)
{
	u32 fw_start_addr, fw_size, drv_start_addr, drv_size;

	fw_start_addr = le32_to_cpu(fw_usage->fw_region_start_address_in_kb);
	fw_size = le16_to_cpu(fw_usage->used_by_firmware_in_kb);

	drv_start_addr = le32_to_cpu(fw_usage->driver_region0_start_address_in_kb);
	drv_size = le32_to_cpu(fw_usage->used_by_driver_region0_in_kb);

	DRM_DEBUG("atom requested fw start at %08x %dkb and drv start at %08x %dkb\n",
			  fw_start_addr,
			  fw_size,
			  drv_start_addr,
			  drv_size);

	if (amdgpu_sriov_vf(adev) &&
	    ((fw_start_addr & (ATOM_VRAM_BLOCK_NEEDS_NO_RESERVATION <<
		ATOM_VRAM_OPERATION_FLAGS_SHIFT)) == 0)) {
		/* Firmware request VRAM reservation for SR-IOV */
		adev->mman.fw_vram_usage_start_offset = (fw_start_addr &
			(~ATOM_VRAM_OPERATION_FLAGS_MASK)) << 10;
		adev->mman.fw_vram_usage_size = fw_size << 10;
	}

	if (amdgpu_sriov_vf(adev) &&
	    ((drv_start_addr & (ATOM_VRAM_BLOCK_NEEDS_NO_RESERVATION <<
		ATOM_VRAM_OPERATION_FLAGS_SHIFT)) == 0)) {
		/* driver request VRAM reservation for SR-IOV */
		adev->mman.drv_vram_usage_start_offset = (drv_start_addr &
			(~ATOM_VRAM_OPERATION_FLAGS_MASK)) << 10;
		adev->mman.drv_vram_usage_size = drv_size << 10;
	}

	*usage_bytes = 0;
	return 0;
}

int amdgpu_atomfirmware_allocate_fb_scratch(struct amdgpu_device *adev)
{
	struct atom_context *ctx = adev->mode_info.atom_context;
	int index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
						vram_usagebyfirmware);
	struct vram_usagebyfirmware_v2_1 *fw_usage_v2_1;
	struct vram_usagebyfirmware_v2_2 *fw_usage_v2_2;
	u16 data_offset;
	u8 frev, crev;
	int usage_bytes = 0;

	if (amdgpu_atom_parse_data_header(ctx, index, NULL, &frev, &crev, &data_offset)) {
		if (frev == 2 && crev == 1) {
			fw_usage_v2_1 =
				(struct vram_usagebyfirmware_v2_1 *)(ctx->bios + data_offset);
			amdgpu_atomfirmware_allocate_fb_v2_1(adev,
					fw_usage_v2_1,
					&usage_bytes);
		} else if (frev >= 2 && crev >= 2) {
			fw_usage_v2_2 =
				(struct vram_usagebyfirmware_v2_2 *)(ctx->bios + data_offset);
			amdgpu_atomfirmware_allocate_fb_v2_2(adev,
					fw_usage_v2_2,
					&usage_bytes);
		}
	}

	ctx->scratch_size_bytes = 0;
	if (usage_bytes == 0)
		usage_bytes = 20 * 1024;
	/* allocate some scratch memory */
	ctx->scratch = kzalloc(usage_bytes, GFP_KERNEL);
	if (!ctx->scratch)
		return -ENOMEM;
	ctx->scratch_size_bytes = usage_bytes;
	return 0;
}

union igp_info {
	struct atom_integrated_system_info_v1_11 v11;
	struct atom_integrated_system_info_v1_12 v12;
	struct atom_integrated_system_info_v2_1 v21;
};

union umc_info {
	struct atom_umc_info_v3_1 v31;
	struct atom_umc_info_v3_2 v32;
	struct atom_umc_info_v3_3 v33;
};

union vram_info {
	struct atom_vram_info_header_v2_3 v23;
	struct atom_vram_info_header_v2_4 v24;
	struct atom_vram_info_header_v2_5 v25;
	struct atom_vram_info_header_v2_6 v26;
	struct atom_vram_info_header_v3_0 v30;
};

union vram_module {
	struct atom_vram_module_v9 v9;
	struct atom_vram_module_v10 v10;
	struct atom_vram_module_v11 v11;
	struct atom_vram_module_v3_0 v30;
};

static int convert_atom_mem_type_to_vram_type(struct amdgpu_device *adev,
					      int atom_mem_type)
{
	int vram_type;

	if (adev->flags & AMD_IS_APU) {
		switch (atom_mem_type) {
		case Ddr2MemType:
		case LpDdr2MemType:
			vram_type = AMDGPU_VRAM_TYPE_DDR2;
			break;
		case Ddr3MemType:
		case LpDdr3MemType:
			vram_type = AMDGPU_VRAM_TYPE_DDR3;
			break;
		case Ddr4MemType:
			vram_type = AMDGPU_VRAM_TYPE_DDR4;
			break;
		case LpDdr4MemType:
			vram_type = AMDGPU_VRAM_TYPE_LPDDR4;
			break;
		case Ddr5MemType:
			vram_type = AMDGPU_VRAM_TYPE_DDR5;
			break;
		case LpDdr5MemType:
			vram_type = AMDGPU_VRAM_TYPE_LPDDR5;
			break;
		default:
			vram_type = AMDGPU_VRAM_TYPE_UNKNOWN;
			break;
		}
	} else {
		switch (atom_mem_type) {
		case ATOM_DGPU_VRAM_TYPE_GDDR5:
			vram_type = AMDGPU_VRAM_TYPE_GDDR5;
			break;
		case ATOM_DGPU_VRAM_TYPE_HBM2:
		case ATOM_DGPU_VRAM_TYPE_HBM2E:
		case ATOM_DGPU_VRAM_TYPE_HBM3:
			vram_type = AMDGPU_VRAM_TYPE_HBM;
			break;
		case ATOM_DGPU_VRAM_TYPE_GDDR6:
			vram_type = AMDGPU_VRAM_TYPE_GDDR6;
			break;
		default:
			vram_type = AMDGPU_VRAM_TYPE_UNKNOWN;
			break;
		}
	}

	return vram_type;
}


int
amdgpu_atomfirmware_get_vram_info(struct amdgpu_device *adev,
				  int *vram_width, int *vram_type,
				  int *vram_vendor)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index, i = 0;
	u16 data_offset, size;
	union igp_info *igp_info;
	union vram_info *vram_info;
	union vram_module *vram_module;
	u8 frev, crev;
	u8 mem_type;
	u8 mem_vendor;
	u32 mem_channel_number;
	u32 mem_channel_width;
	u32 module_id;

	if (adev->flags & AMD_IS_APU)
		index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
						    integratedsysteminfo);
	else
		index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
						    vram_info);

	if (amdgpu_atom_parse_data_header(mode_info->atom_context,
					  index, &size,
					  &frev, &crev, &data_offset)) {
		if (adev->flags & AMD_IS_APU) {
			igp_info = (union igp_info *)
				(mode_info->atom_context->bios + data_offset);
			switch (frev) {
			case 1:
				switch (crev) {
				case 11:
				case 12:
					mem_channel_number = igp_info->v11.umachannelnumber;
					if (!mem_channel_number)
						mem_channel_number = 1;
					mem_type = igp_info->v11.memorytype;
					if (mem_type == LpDdr5MemType)
						mem_channel_width = 32;
					else
						mem_channel_width = 64;
					if (vram_width)
						*vram_width = mem_channel_number * mem_channel_width;
					if (vram_type)
						*vram_type = convert_atom_mem_type_to_vram_type(adev, mem_type);
					break;
				default:
					return -EINVAL;
				}
				break;
			case 2:
				switch (crev) {
				case 1:
				case 2:
					mem_channel_number = igp_info->v21.umachannelnumber;
					if (!mem_channel_number)
						mem_channel_number = 1;
					mem_type = igp_info->v21.memorytype;
					if (mem_type == LpDdr5MemType)
						mem_channel_width = 32;
					else
						mem_channel_width = 64;
					if (vram_width)
						*vram_width = mem_channel_number * mem_channel_width;
					if (vram_type)
						*vram_type = convert_atom_mem_type_to_vram_type(adev, mem_type);
					break;
				default:
					return -EINVAL;
				}
				break;
			default:
				return -EINVAL;
			}
		} else {
			vram_info = (union vram_info *)
				(mode_info->atom_context->bios + data_offset);
			module_id = (RREG32(adev->bios_scratch_reg_offset + 4) & 0x00ff0000) >> 16;
			if (frev == 3) {
				switch (crev) {
				/* v30 */
				case 0:
					vram_module = (union vram_module *)vram_info->v30.vram_module;
					mem_vendor = (vram_module->v30.dram_vendor_id) & 0xF;
					if (vram_vendor)
						*vram_vendor = mem_vendor;
					mem_type = vram_info->v30.memory_type;
					if (vram_type)
						*vram_type = convert_atom_mem_type_to_vram_type(adev, mem_type);
					mem_channel_number = vram_info->v30.channel_num;
					mem_channel_width = vram_info->v30.channel_width;
					if (vram_width)
						*vram_width = mem_channel_number * (1 << mem_channel_width);
					break;
				default:
					return -EINVAL;
				}
			} else if (frev == 2) {
				switch (crev) {
				/* v23 */
				case 3:
					if (module_id > vram_info->v23.vram_module_num)
						module_id = 0;
					vram_module = (union vram_module *)vram_info->v23.vram_module;
					while (i < module_id) {
						vram_module = (union vram_module *)
							((u8 *)vram_module + vram_module->v9.vram_module_size);
						i++;
					}
					mem_type = vram_module->v9.memory_type;
					if (vram_type)
						*vram_type = convert_atom_mem_type_to_vram_type(adev, mem_type);
					mem_channel_number = vram_module->v9.channel_num;
					mem_channel_width = vram_module->v9.channel_width;
					if (vram_width)
						*vram_width = mem_channel_number * (1 << mem_channel_width);
					mem_vendor = (vram_module->v9.vender_rev_id) & 0xF;
					if (vram_vendor)
						*vram_vendor = mem_vendor;
					break;
				/* v24 */
				case 4:
					if (module_id > vram_info->v24.vram_module_num)
						module_id = 0;
					vram_module = (union vram_module *)vram_info->v24.vram_module;
					while (i < module_id) {
						vram_module = (union vram_module *)
							((u8 *)vram_module + vram_module->v10.vram_module_size);
						i++;
					}
					mem_type = vram_module->v10.memory_type;
					if (vram_type)
						*vram_type = convert_atom_mem_type_to_vram_type(adev, mem_type);
					mem_channel_number = vram_module->v10.channel_num;
					mem_channel_width = vram_module->v10.channel_width;
					if (vram_width)
						*vram_width = mem_channel_number * (1 << mem_channel_width);
					mem_vendor = (vram_module->v10.vender_rev_id) & 0xF;
					if (vram_vendor)
						*vram_vendor = mem_vendor;
					break;
				/* v25 */
				case 5:
					if (module_id > vram_info->v25.vram_module_num)
						module_id = 0;
					vram_module = (union vram_module *)vram_info->v25.vram_module;
					while (i < module_id) {
						vram_module = (union vram_module *)
							((u8 *)vram_module + vram_module->v11.vram_module_size);
						i++;
					}
					mem_type = vram_module->v11.memory_type;
					if (vram_type)
						*vram_type = convert_atom_mem_type_to_vram_type(adev, mem_type);
					mem_channel_number = vram_module->v11.channel_num;
					mem_channel_width = vram_module->v11.channel_width;
					if (vram_width)
						*vram_width = mem_channel_number * (1 << mem_channel_width);
					mem_vendor = (vram_module->v11.vender_rev_id) & 0xF;
					if (vram_vendor)
						*vram_vendor = mem_vendor;
					break;
				/* v26 */
				case 6:
					if (module_id > vram_info->v26.vram_module_num)
						module_id = 0;
					vram_module = (union vram_module *)vram_info->v26.vram_module;
					while (i < module_id) {
						vram_module = (union vram_module *)
							((u8 *)vram_module + vram_module->v9.vram_module_size);
						i++;
					}
					mem_type = vram_module->v9.memory_type;
					if (vram_type)
						*vram_type = convert_atom_mem_type_to_vram_type(adev, mem_type);
					mem_channel_number = vram_module->v9.channel_num;
					mem_channel_width = vram_module->v9.channel_width;
					if (vram_width)
						*vram_width = mem_channel_number * (1 << mem_channel_width);
					mem_vendor = (vram_module->v9.vender_rev_id) & 0xF;
					if (vram_vendor)
						*vram_vendor = mem_vendor;
					break;
				default:
					return -EINVAL;
				}
			} else {
				/* invalid frev */
				return -EINVAL;
			}
		}

	}

	return 0;
}

/*
 * Return true if vbios enabled ecc by default, if umc info table is available
 * or false if ecc is not enabled or umc info table is not available
 */
bool amdgpu_atomfirmware_mem_ecc_supported(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index;
	u16 data_offset, size;
	union umc_info *umc_info;
	u8 frev, crev;
	bool ecc_default_enabled = false;
	u8 umc_config;
	u32 umc_config1;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
			umc_info);

	if (amdgpu_atom_parse_data_header(mode_info->atom_context,
				index, &size, &frev, &crev, &data_offset)) {
		if (frev == 3) {
			umc_info = (union umc_info *)
				(mode_info->atom_context->bios + data_offset);
			switch (crev) {
			case 1:
				umc_config = le32_to_cpu(umc_info->v31.umc_config);
				ecc_default_enabled =
					(umc_config & UMC_CONFIG__DEFAULT_MEM_ECC_ENABLE) ? true : false;
				break;
			case 2:
				umc_config = le32_to_cpu(umc_info->v32.umc_config);
				ecc_default_enabled =
					(umc_config & UMC_CONFIG__DEFAULT_MEM_ECC_ENABLE) ? true : false;
				break;
			case 3:
				umc_config = le32_to_cpu(umc_info->v33.umc_config);
				umc_config1 = le32_to_cpu(umc_info->v33.umc_config1);
				ecc_default_enabled =
					((umc_config & UMC_CONFIG__DEFAULT_MEM_ECC_ENABLE) ||
					 (umc_config1 & UMC_CONFIG1__ENABLE_ECC_CAPABLE)) ? true : false;
				break;
			default:
				/* unsupported crev */
				return false;
			}
		}
	}

	return ecc_default_enabled;
}

/*
 * Helper function to query sram ecc capablity
 *
 * @adev: amdgpu_device pointer
 *
 * Return true if vbios supports sram ecc or false if not
 */
bool amdgpu_atomfirmware_sram_ecc_supported(struct amdgpu_device *adev)
{
	u32 fw_cap;

	fw_cap = adev->mode_info.firmware_flags;

	return (fw_cap & ATOM_FIRMWARE_CAP_SRAM_ECC) ? true : false;
}

/*
 * Helper function to query dynamic boot config capability
 *
 * @adev: amdgpu_device pointer
 *
 * Return true if vbios supports dynamic boot config or false if not
 */
bool amdgpu_atomfirmware_dynamic_boot_config_supported(struct amdgpu_device *adev)
{
	u32 fw_cap;

	fw_cap = adev->mode_info.firmware_flags;

	return (fw_cap & ATOM_FIRMWARE_CAP_DYNAMIC_BOOT_CFG_ENABLE) ? true : false;
}

/**
 * amdgpu_atomfirmware_ras_rom_addr -- Get the RAS EEPROM addr from VBIOS
 * @adev: amdgpu_device pointer
 * @i2c_address: pointer to u8; if not NULL, will contain
 *    the RAS EEPROM address if the function returns true
 *
 * Return true if VBIOS supports RAS EEPROM address reporting,
 * else return false. If true and @i2c_address is not NULL,
 * will contain the RAS ROM address.
 */
bool amdgpu_atomfirmware_ras_rom_addr(struct amdgpu_device *adev,
				      u8 *i2c_address)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index;
	u16 data_offset, size;
	union firmware_info *firmware_info;
	u8 frev, crev;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    firmwareinfo);

	if (amdgpu_atom_parse_data_header(adev->mode_info.atom_context,
					  index, &size, &frev, &crev,
					  &data_offset)) {
		/* support firmware_info 3.4 + */
		if ((frev == 3 && crev >=4) || (frev > 3)) {
			firmware_info = (union firmware_info *)
				(mode_info->atom_context->bios + data_offset);
			/* The ras_rom_i2c_slave_addr should ideally
			 * be a 19-bit EEPROM address, which would be
			 * used as is by the driver; see top of
			 * amdgpu_eeprom.c.
			 *
			 * When this is the case, 0 is of course a
			 * valid RAS EEPROM address, in which case,
			 * we'll drop the first "if (firm...)" and only
			 * leave the check for the pointer.
			 *
			 * The reason this works right now is because
			 * ras_rom_i2c_slave_addr contains the EEPROM
			 * device type qualifier 1010b in the top 4
			 * bits.
			 */
			if (firmware_info->v34.ras_rom_i2c_slave_addr) {
				if (i2c_address)
					*i2c_address = firmware_info->v34.ras_rom_i2c_slave_addr;
				return true;
			}
		}
	}

	return false;
}


union smu_info {
	struct atom_smu_info_v3_1 v31;
	struct atom_smu_info_v4_0 v40;
};

union gfx_info {
	struct atom_gfx_info_v2_2 v22;
	struct atom_gfx_info_v2_4 v24;
	struct atom_gfx_info_v2_7 v27;
	struct atom_gfx_info_v3_0 v30;
};

int amdgpu_atomfirmware_get_clock_info(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	struct amdgpu_pll *spll = &adev->clock.spll;
	struct amdgpu_pll *mpll = &adev->clock.mpll;
	uint8_t frev, crev;
	uint16_t data_offset;
	int ret = -EINVAL, index;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    firmwareinfo);
	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		union firmware_info *firmware_info =
			(union firmware_info *)(mode_info->atom_context->bios +
						data_offset);

		adev->clock.default_sclk =
			le32_to_cpu(firmware_info->v31.bootup_sclk_in10khz);
		adev->clock.default_mclk =
			le32_to_cpu(firmware_info->v31.bootup_mclk_in10khz);

		adev->pm.current_sclk = adev->clock.default_sclk;
		adev->pm.current_mclk = adev->clock.default_mclk;

		ret = 0;
	}

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    smu_info);
	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		union smu_info *smu_info =
			(union smu_info *)(mode_info->atom_context->bios +
					   data_offset);

		/* system clock */
		if (frev == 3)
			spll->reference_freq = le32_to_cpu(smu_info->v31.core_refclk_10khz);
		else if (frev == 4)
			spll->reference_freq = le32_to_cpu(smu_info->v40.core_refclk_10khz);

		spll->reference_div = 0;
		spll->min_post_div = 1;
		spll->max_post_div = 1;
		spll->min_ref_div = 2;
		spll->max_ref_div = 0xff;
		spll->min_feedback_div = 4;
		spll->max_feedback_div = 0xff;
		spll->best_vco = 0;

		ret = 0;
	}

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    umc_info);
	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		union umc_info *umc_info =
			(union umc_info *)(mode_info->atom_context->bios +
					   data_offset);

		/* memory clock */
		mpll->reference_freq = le32_to_cpu(umc_info->v31.mem_refclk_10khz);

		mpll->reference_div = 0;
		mpll->min_post_div = 1;
		mpll->max_post_div = 1;
		mpll->min_ref_div = 2;
		mpll->max_ref_div = 0xff;
		mpll->min_feedback_div = 4;
		mpll->max_feedback_div = 0xff;
		mpll->best_vco = 0;

		ret = 0;
	}

	/* if asic is Navi+, the rlc reference clock is used for system clock
	 * from vbios gfx_info table */
	if (adev->asic_type >= CHIP_NAVI10) {
		index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
						   gfx_info);
		if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
					  &frev, &crev, &data_offset)) {
			union gfx_info *gfx_info = (union gfx_info *)
				(mode_info->atom_context->bios + data_offset);
			if ((frev == 3) ||
			    (frev == 2 && crev == 6)) {
				spll->reference_freq = le32_to_cpu(gfx_info->v30.golden_tsc_count_lower_refclk);
				ret = 0;
			} else if ((frev == 2) &&
				   (crev >= 2) &&
				   (crev != 6)) {
				spll->reference_freq = le32_to_cpu(gfx_info->v22.rlc_gpu_timer_refclk);
				ret = 0;
			} else {
				BUG();
			}
		}
	}

	return ret;
}

int amdgpu_atomfirmware_get_gfx_info(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index;
	uint8_t frev, crev;
	uint16_t data_offset;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    gfx_info);
	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		union gfx_info *gfx_info = (union gfx_info *)
			(mode_info->atom_context->bios + data_offset);
		if (frev == 2) {
			switch (crev) {
			case 4:
				adev->gfx.config.max_shader_engines = gfx_info->v24.max_shader_engines;
				adev->gfx.config.max_cu_per_sh = gfx_info->v24.max_cu_per_sh;
				adev->gfx.config.max_sh_per_se = gfx_info->v24.max_sh_per_se;
				adev->gfx.config.max_backends_per_se = gfx_info->v24.max_backends_per_se;
				adev->gfx.config.max_texture_channel_caches = gfx_info->v24.max_texture_channel_caches;
				adev->gfx.config.max_gprs = le16_to_cpu(gfx_info->v24.gc_num_gprs);
				adev->gfx.config.max_gs_threads = gfx_info->v24.gc_num_max_gs_thds;
				adev->gfx.config.gs_vgt_table_depth = gfx_info->v24.gc_gs_table_depth;
				adev->gfx.config.gs_prim_buffer_depth =
					le16_to_cpu(gfx_info->v24.gc_gsprim_buff_depth);
				adev->gfx.config.double_offchip_lds_buf =
					gfx_info->v24.gc_double_offchip_lds_buffer;
				adev->gfx.cu_info.wave_front_size = le16_to_cpu(gfx_info->v24.gc_wave_size);
				adev->gfx.cu_info.max_waves_per_simd = le16_to_cpu(gfx_info->v24.gc_max_waves_per_simd);
				adev->gfx.cu_info.max_scratch_slots_per_cu = gfx_info->v24.gc_max_scratch_slots_per_cu;
				adev->gfx.cu_info.lds_size = le16_to_cpu(gfx_info->v24.gc_lds_size);
				return 0;
			case 7:
				adev->gfx.config.max_shader_engines = gfx_info->v27.max_shader_engines;
				adev->gfx.config.max_cu_per_sh = gfx_info->v27.max_cu_per_sh;
				adev->gfx.config.max_sh_per_se = gfx_info->v27.max_sh_per_se;
				adev->gfx.config.max_backends_per_se = gfx_info->v27.max_backends_per_se;
				adev->gfx.config.max_texture_channel_caches = gfx_info->v27.max_texture_channel_caches;
				adev->gfx.config.max_gprs = le16_to_cpu(gfx_info->v27.gc_num_gprs);
				adev->gfx.config.max_gs_threads = gfx_info->v27.gc_num_max_gs_thds;
				adev->gfx.config.gs_vgt_table_depth = gfx_info->v27.gc_gs_table_depth;
				adev->gfx.config.gs_prim_buffer_depth = le16_to_cpu(gfx_info->v27.gc_gsprim_buff_depth);
				adev->gfx.config.double_offchip_lds_buf = gfx_info->v27.gc_double_offchip_lds_buffer;
				adev->gfx.cu_info.wave_front_size = le16_to_cpu(gfx_info->v27.gc_wave_size);
				adev->gfx.cu_info.max_waves_per_simd = le16_to_cpu(gfx_info->v27.gc_max_waves_per_simd);
				adev->gfx.cu_info.max_scratch_slots_per_cu = gfx_info->v27.gc_max_scratch_slots_per_cu;
				adev->gfx.cu_info.lds_size = le16_to_cpu(gfx_info->v27.gc_lds_size);
				return 0;
			default:
				return -EINVAL;
			}
		} else if (frev == 3) {
			switch (crev) {
			case 0:
				adev->gfx.config.max_shader_engines = gfx_info->v30.max_shader_engines;
				adev->gfx.config.max_cu_per_sh = gfx_info->v30.max_cu_per_sh;
				adev->gfx.config.max_sh_per_se = gfx_info->v30.max_sh_per_se;
				adev->gfx.config.max_backends_per_se = gfx_info->v30.max_backends_per_se;
				adev->gfx.config.max_texture_channel_caches = gfx_info->v30.max_texture_channel_caches;
				return 0;
			default:
				return -EINVAL;
			}
		} else {
			return -EINVAL;
		}

	}
	return -EINVAL;
}

/*
 * Helper function to query two stage mem training capability
 *
 * @adev: amdgpu_device pointer
 *
 * Return true if two stage mem training is supported or false if not
 */
bool amdgpu_atomfirmware_mem_training_supported(struct amdgpu_device *adev)
{
	u32 fw_cap;

	fw_cap = adev->mode_info.firmware_flags;

	return (fw_cap & ATOM_FIRMWARE_CAP_ENABLE_2STAGE_BIST_TRAINING) ? true : false;
}

int amdgpu_atomfirmware_get_fw_reserved_fb_size(struct amdgpu_device *adev)
{
	struct atom_context *ctx = adev->mode_info.atom_context;
	union firmware_info *firmware_info;
	int index;
	u16 data_offset, size;
	u8 frev, crev;
	int fw_reserved_fb_size;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
			firmwareinfo);

	if (!amdgpu_atom_parse_data_header(ctx, index, &size,
				&frev, &crev, &data_offset))
		/* fail to parse data_header */
		return 0;

	firmware_info = (union firmware_info *)(ctx->bios + data_offset);

	if (frev !=3)
		return -EINVAL;

	switch (crev) {
	case 4:
		fw_reserved_fb_size =
			(firmware_info->v34.fw_reserved_size_in_kb << 10);
		break;
	default:
		fw_reserved_fb_size = 0;
		break;
	}

	return fw_reserved_fb_size;
}

/*
 * Helper function to execute asic_init table
 *
 * @adev: amdgpu_device pointer
 * @fb_reset: flag to indicate whether fb is reset or not
 *
 * Return 0 if succeed, otherwise failed
 */
int amdgpu_atomfirmware_asic_init(struct amdgpu_device *adev, bool fb_reset)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	struct atom_context *ctx;
	uint8_t frev, crev;
	uint16_t data_offset;
	uint32_t bootup_sclk_in10khz, bootup_mclk_in10khz;
	struct asic_init_ps_allocation_v2_1 asic_init_ps_v2_1;
	int index;

	if (!mode_info)
		return -EINVAL;

	ctx = mode_info->atom_context;
	if (!ctx)
		return -EINVAL;

	/* query bootup sclk/mclk from firmware_info table */
	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    firmwareinfo);
	if (amdgpu_atom_parse_data_header(ctx, index, NULL,
				&frev, &crev, &data_offset)) {
		union firmware_info *firmware_info =
			(union firmware_info *)(ctx->bios +
						data_offset);

		bootup_sclk_in10khz =
			le32_to_cpu(firmware_info->v31.bootup_sclk_in10khz);
		bootup_mclk_in10khz =
			le32_to_cpu(firmware_info->v31.bootup_mclk_in10khz);
	} else {
		return -EINVAL;
	}

	index = get_index_into_master_table(atom_master_list_of_command_functions_v2_1,
                                            asic_init);
	if (amdgpu_atom_parse_cmd_header(mode_info->atom_context, index, &frev, &crev)) {
		if (frev == 2 && crev >= 1) {
			memset(&asic_init_ps_v2_1, 0, sizeof(asic_init_ps_v2_1));
			asic_init_ps_v2_1.param.engineparam.sclkfreqin10khz = bootup_sclk_in10khz;
			asic_init_ps_v2_1.param.memparam.mclkfreqin10khz = bootup_mclk_in10khz;
			asic_init_ps_v2_1.param.engineparam.engineflag = b3NORMAL_ENGINE_INIT;
			if (!fb_reset)
				asic_init_ps_v2_1.param.memparam.memflag = b3DRAM_SELF_REFRESH_EXIT;
			else
				asic_init_ps_v2_1.param.memparam.memflag = 0;
		} else {
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	return amdgpu_atom_execute_table(ctx, ATOM_CMD_INIT, (uint32_t *)&asic_init_ps_v2_1);
}
