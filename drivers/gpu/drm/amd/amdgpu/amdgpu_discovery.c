/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#include "amdgpu.h"
#include "amdgpu_discovery.h"
#include "soc15_hw_ip.h"
#include "discovery.h"

#define mmRCC_CONFIG_MEMSIZE	0xde3
#define mmMM_INDEX		0x0
#define mmMM_INDEX_HI		0x6
#define mmMM_DATA		0x1
#define HW_ID_MAX		300

static const char *hw_id_names[HW_ID_MAX] = {
	[MP1_HWID]		= "MP1",
	[MP2_HWID]		= "MP2",
	[THM_HWID]		= "THM",
	[SMUIO_HWID]		= "SMUIO",
	[FUSE_HWID]		= "FUSE",
	[CLKA_HWID]		= "CLKA",
	[PWR_HWID]		= "PWR",
	[GC_HWID]		= "GC",
	[UVD_HWID]		= "UVD",
	[AUDIO_AZ_HWID]		= "AUDIO_AZ",
	[ACP_HWID]		= "ACP",
	[DCI_HWID]		= "DCI",
	[DMU_HWID]		= "DMU",
	[DCO_HWID]		= "DCO",
	[DIO_HWID]		= "DIO",
	[XDMA_HWID]		= "XDMA",
	[DCEAZ_HWID]		= "DCEAZ",
	[DAZ_HWID]		= "DAZ",
	[SDPMUX_HWID]		= "SDPMUX",
	[NTB_HWID]		= "NTB",
	[IOHC_HWID]		= "IOHC",
	[L2IMU_HWID]		= "L2IMU",
	[VCE_HWID]		= "VCE",
	[MMHUB_HWID]		= "MMHUB",
	[ATHUB_HWID]		= "ATHUB",
	[DBGU_NBIO_HWID]	= "DBGU_NBIO",
	[DFX_HWID]		= "DFX",
	[DBGU0_HWID]		= "DBGU0",
	[DBGU1_HWID]		= "DBGU1",
	[OSSSYS_HWID]		= "OSSSYS",
	[HDP_HWID]		= "HDP",
	[SDMA0_HWID]		= "SDMA0",
	[SDMA1_HWID]		= "SDMA1",
	[ISP_HWID]		= "ISP",
	[DBGU_IO_HWID]		= "DBGU_IO",
	[DF_HWID]		= "DF",
	[CLKB_HWID]		= "CLKB",
	[FCH_HWID]		= "FCH",
	[DFX_DAP_HWID]		= "DFX_DAP",
	[L1IMU_PCIE_HWID]	= "L1IMU_PCIE",
	[L1IMU_NBIF_HWID]	= "L1IMU_NBIF",
	[L1IMU_IOAGR_HWID]	= "L1IMU_IOAGR",
	[L1IMU3_HWID]		= "L1IMU3",
	[L1IMU4_HWID]		= "L1IMU4",
	[L1IMU5_HWID]		= "L1IMU5",
	[L1IMU6_HWID]		= "L1IMU6",
	[L1IMU7_HWID]		= "L1IMU7",
	[L1IMU8_HWID]		= "L1IMU8",
	[L1IMU9_HWID]		= "L1IMU9",
	[L1IMU10_HWID]		= "L1IMU10",
	[L1IMU11_HWID]		= "L1IMU11",
	[L1IMU12_HWID]		= "L1IMU12",
	[L1IMU13_HWID]		= "L1IMU13",
	[L1IMU14_HWID]		= "L1IMU14",
	[L1IMU15_HWID]		= "L1IMU15",
	[WAFLC_HWID]		= "WAFLC",
	[FCH_USB_PD_HWID]	= "FCH_USB_PD",
	[PCIE_HWID]		= "PCIE",
	[PCS_HWID]		= "PCS",
	[DDCL_HWID]		= "DDCL",
	[SST_HWID]		= "SST",
	[IOAGR_HWID]		= "IOAGR",
	[NBIF_HWID]		= "NBIF",
	[IOAPIC_HWID]		= "IOAPIC",
	[SYSTEMHUB_HWID]	= "SYSTEMHUB",
	[NTBCCP_HWID]		= "NTBCCP",
	[UMC_HWID]		= "UMC",
	[SATA_HWID]		= "SATA",
	[USB_HWID]		= "USB",
	[CCXSEC_HWID]		= "CCXSEC",
	[XGMI_HWID]		= "XGMI",
	[XGBE_HWID]		= "XGBE",
	[MP0_HWID]		= "MP0",
};

static int hw_id_map[MAX_HWIP] = {
	[GC_HWIP]	= GC_HWID,
	[HDP_HWIP]	= HDP_HWID,
	[SDMA0_HWIP]	= SDMA0_HWID,
	[SDMA1_HWIP]	= SDMA1_HWID,
	[MMHUB_HWIP]	= MMHUB_HWID,
	[ATHUB_HWIP]	= ATHUB_HWID,
	[NBIO_HWIP]	= NBIF_HWID,
	[MP0_HWIP]	= MP0_HWID,
	[MP1_HWIP]	= MP1_HWID,
	[UVD_HWIP]	= UVD_HWID,
	[VCE_HWIP]	= VCE_HWID,
	[DF_HWIP]	= DF_HWID,
	[DCE_HWIP]	= DMU_HWID,
	[OSSSYS_HWIP]	= OSSSYS_HWID,
	[SMUIO_HWIP]	= SMUIO_HWID,
	[PWR_HWIP]	= PWR_HWID,
	[NBIF_HWIP]	= NBIF_HWID,
	[THM_HWIP]	= THM_HWID,
	[CLK_HWIP]	= CLKA_HWID,
	[UMC_HWIP]	= UMC_HWID,
};

static int amdgpu_discovery_read_binary(struct amdgpu_device *adev, uint8_t *binary)
{
	uint64_t vram_size = (uint64_t)RREG32(mmRCC_CONFIG_MEMSIZE) << 20;
	uint64_t pos = vram_size - DISCOVERY_TMR_OFFSET;

	amdgpu_device_vram_access(adev, pos, (uint32_t *)binary,
				  adev->mman.discovery_tmr_size, false);
	return 0;
}

static uint16_t amdgpu_discovery_calculate_checksum(uint8_t *data, uint32_t size)
{
	uint16_t checksum = 0;
	int i;

	for (i = 0; i < size; i++)
		checksum += data[i];

	return checksum;
}

static inline bool amdgpu_discovery_verify_checksum(uint8_t *data, uint32_t size,
						    uint16_t expected)
{
	return !!(amdgpu_discovery_calculate_checksum(data, size) == expected);
}

static int amdgpu_discovery_init(struct amdgpu_device *adev)
{
	struct table_info *info;
	struct binary_header *bhdr;
	struct ip_discovery_header *ihdr;
	struct gpu_info_header *ghdr;
	uint16_t offset;
	uint16_t size;
	uint16_t checksum;
	int r;

	adev->mman.discovery_tmr_size = DISCOVERY_TMR_SIZE;
	adev->mman.discovery_bin = kzalloc(adev->mman.discovery_tmr_size, GFP_KERNEL);
	if (!adev->mman.discovery_bin)
		return -ENOMEM;

	r = amdgpu_discovery_read_binary(adev, adev->mman.discovery_bin);
	if (r) {
		DRM_ERROR("failed to read ip discovery binary\n");
		goto out;
	}

	bhdr = (struct binary_header *)adev->mman.discovery_bin;

	if (le32_to_cpu(bhdr->binary_signature) != BINARY_SIGNATURE) {
		DRM_ERROR("invalid ip discovery binary signature\n");
		r = -EINVAL;
		goto out;
	}

	offset = offsetof(struct binary_header, binary_checksum) +
		sizeof(bhdr->binary_checksum);
	size = bhdr->binary_size - offset;
	checksum = bhdr->binary_checksum;

	if (!amdgpu_discovery_verify_checksum(adev->mman.discovery_bin + offset,
					      size, checksum)) {
		DRM_ERROR("invalid ip discovery binary checksum\n");
		r = -EINVAL;
		goto out;
	}

	info = &bhdr->table_list[IP_DISCOVERY];
	offset = le16_to_cpu(info->offset);
	checksum = le16_to_cpu(info->checksum);
	ihdr = (struct ip_discovery_header *)(adev->mman.discovery_bin + offset);

	if (le32_to_cpu(ihdr->signature) != DISCOVERY_TABLE_SIGNATURE) {
		DRM_ERROR("invalid ip discovery data table signature\n");
		r = -EINVAL;
		goto out;
	}

	if (!amdgpu_discovery_verify_checksum(adev->mman.discovery_bin + offset,
					      ihdr->size, checksum)) {
		DRM_ERROR("invalid ip discovery data table checksum\n");
		r = -EINVAL;
		goto out;
	}

	info = &bhdr->table_list[GC];
	offset = le16_to_cpu(info->offset);
	checksum = le16_to_cpu(info->checksum);
	ghdr = (struct gpu_info_header *)(adev->mman.discovery_bin + offset);

	if (!amdgpu_discovery_verify_checksum(adev->mman.discovery_bin + offset,
				              ghdr->size, checksum)) {
		DRM_ERROR("invalid gc data table checksum\n");
		r = -EINVAL;
		goto out;
	}

	return 0;

out:
	kfree(adev->mman.discovery_bin);
	adev->mman.discovery_bin = NULL;

	return r;
}

void amdgpu_discovery_fini(struct amdgpu_device *adev)
{
	kfree(adev->mman.discovery_bin);
	adev->mman.discovery_bin = NULL;
}

int amdgpu_discovery_reg_base_init(struct amdgpu_device *adev)
{
	struct binary_header *bhdr;
	struct ip_discovery_header *ihdr;
	struct die_header *dhdr;
	struct ip *ip;
	uint16_t die_offset;
	uint16_t ip_offset;
	uint16_t num_dies;
	uint16_t num_ips;
	uint8_t num_base_address;
	int hw_ip;
	int i, j, k;
	int r;

	r = amdgpu_discovery_init(adev);
	if (r) {
		DRM_ERROR("amdgpu_discovery_init failed\n");
		return r;
	}

	bhdr = (struct binary_header *)adev->mman.discovery_bin;
	ihdr = (struct ip_discovery_header *)(adev->mman.discovery_bin +
			le16_to_cpu(bhdr->table_list[IP_DISCOVERY].offset));
	num_dies = le16_to_cpu(ihdr->num_dies);

	DRM_DEBUG("number of dies: %d\n", num_dies);

	for (i = 0; i < num_dies; i++) {
		die_offset = le16_to_cpu(ihdr->die_info[i].die_offset);
		dhdr = (struct die_header *)(adev->mman.discovery_bin + die_offset);
		num_ips = le16_to_cpu(dhdr->num_ips);
		ip_offset = die_offset + sizeof(*dhdr);

		if (le16_to_cpu(dhdr->die_id) != i) {
			DRM_ERROR("invalid die id %d, expected %d\n",
					le16_to_cpu(dhdr->die_id), i);
			return -EINVAL;
		}

		DRM_DEBUG("number of hardware IPs on die%d: %d\n",
				le16_to_cpu(dhdr->die_id), num_ips);

		for (j = 0; j < num_ips; j++) {
			ip = (struct ip *)(adev->mman.discovery_bin + ip_offset);
			num_base_address = ip->num_base_address;

			DRM_DEBUG("%s(%d) #%d v%d.%d.%d:\n",
				  hw_id_names[le16_to_cpu(ip->hw_id)],
				  le16_to_cpu(ip->hw_id),
				  ip->number_instance,
				  ip->major, ip->minor,
				  ip->revision);

			if (le16_to_cpu(ip->hw_id) == VCN_HWID)
				adev->vcn.num_vcn_inst++;

			for (k = 0; k < num_base_address; k++) {
				/*
				 * convert the endianness of base addresses in place,
				 * so that we don't need to convert them when accessing adev->reg_offset.
				 */
				ip->base_address[k] = le32_to_cpu(ip->base_address[k]);
				DRM_DEBUG("\t0x%08x\n", ip->base_address[k]);
			}

			for (hw_ip = 0; hw_ip < MAX_HWIP; hw_ip++) {
				if (hw_id_map[hw_ip] == le16_to_cpu(ip->hw_id)) {
					DRM_DEBUG("set register base offset for %s\n",
							hw_id_names[le16_to_cpu(ip->hw_id)]);
					adev->reg_offset[hw_ip][ip->number_instance] =
						ip->base_address;
				}

			}

			ip_offset += sizeof(*ip) + 4 * (ip->num_base_address - 1);
		}
	}

	return 0;
}

int amdgpu_discovery_get_ip_version(struct amdgpu_device *adev, int hw_id, int number_instance,
				    int *major, int *minor, int *revision)
{
	struct binary_header *bhdr;
	struct ip_discovery_header *ihdr;
	struct die_header *dhdr;
	struct ip *ip;
	uint16_t die_offset;
	uint16_t ip_offset;
	uint16_t num_dies;
	uint16_t num_ips;
	int i, j;

	if (!adev->mman.discovery_bin) {
		DRM_ERROR("ip discovery uninitialized\n");
		return -EINVAL;
	}

	bhdr = (struct binary_header *)adev->mman.discovery_bin;
	ihdr = (struct ip_discovery_header *)(adev->mman.discovery_bin +
			le16_to_cpu(bhdr->table_list[IP_DISCOVERY].offset));
	num_dies = le16_to_cpu(ihdr->num_dies);

	for (i = 0; i < num_dies; i++) {
		die_offset = le16_to_cpu(ihdr->die_info[i].die_offset);
		dhdr = (struct die_header *)(adev->mman.discovery_bin + die_offset);
		num_ips = le16_to_cpu(dhdr->num_ips);
		ip_offset = die_offset + sizeof(*dhdr);

		for (j = 0; j < num_ips; j++) {
			ip = (struct ip *)(adev->mman.discovery_bin + ip_offset);

			if ((le16_to_cpu(ip->hw_id) == hw_id) && (ip->number_instance == number_instance)) {
				if (major)
					*major = ip->major;
				if (minor)
					*minor = ip->minor;
				if (revision)
					*revision = ip->revision;
				return 0;
			}
			ip_offset += sizeof(*ip) + 4 * (ip->num_base_address - 1);
		}
	}

	return -EINVAL;
}


int amdgpu_discovery_get_vcn_version(struct amdgpu_device *adev, int vcn_instance,
				     int *major, int *minor, int *revision)
{
	return amdgpu_discovery_get_ip_version(adev, VCN_HWID,
					       vcn_instance, major, minor, revision);
}

void amdgpu_discovery_harvest_ip(struct amdgpu_device *adev)
{
	struct binary_header *bhdr;
	struct harvest_table *harvest_info;
	int i, vcn_harvest_count = 0;

	bhdr = (struct binary_header *)adev->mman.discovery_bin;
	harvest_info = (struct harvest_table *)(adev->mman.discovery_bin +
			le16_to_cpu(bhdr->table_list[HARVEST_INFO].offset));

	for (i = 0; i < 32; i++) {
		if (le32_to_cpu(harvest_info->list[i].hw_id) == 0)
			break;

		switch (le32_to_cpu(harvest_info->list[i].hw_id)) {
		case VCN_HWID:
			vcn_harvest_count++;
			break;
		case DMU_HWID:
			adev->harvest_ip_mask |= AMD_HARVEST_IP_DMU_MASK;
			break;
		default:
			break;
		}
	}
	if (vcn_harvest_count == adev->vcn.num_vcn_inst) {
		adev->harvest_ip_mask |= AMD_HARVEST_IP_VCN_MASK;
		adev->harvest_ip_mask |= AMD_HARVEST_IP_JPEG_MASK;
	}
}

int amdgpu_discovery_get_gfx_info(struct amdgpu_device *adev)
{
	struct binary_header *bhdr;
	struct gc_info_v1_0 *gc_info;

	if (!adev->mman.discovery_bin) {
		DRM_ERROR("ip discovery uninitialized\n");
		return -EINVAL;
	}

	bhdr = (struct binary_header *)adev->mman.discovery_bin;
	gc_info = (struct gc_info_v1_0 *)(adev->mman.discovery_bin +
			le16_to_cpu(bhdr->table_list[GC].offset));

	adev->gfx.config.max_shader_engines = le32_to_cpu(gc_info->gc_num_se);
	adev->gfx.config.max_cu_per_sh = 2 * (le32_to_cpu(gc_info->gc_num_wgp0_per_sa) +
					      le32_to_cpu(gc_info->gc_num_wgp1_per_sa));
	adev->gfx.config.max_sh_per_se = le32_to_cpu(gc_info->gc_num_sa_per_se);
	adev->gfx.config.max_backends_per_se = le32_to_cpu(gc_info->gc_num_rb_per_se);
	adev->gfx.config.max_texture_channel_caches = le32_to_cpu(gc_info->gc_num_gl2c);
	adev->gfx.config.max_gprs = le32_to_cpu(gc_info->gc_num_gprs);
	adev->gfx.config.max_gs_threads = le32_to_cpu(gc_info->gc_num_max_gs_thds);
	adev->gfx.config.gs_vgt_table_depth = le32_to_cpu(gc_info->gc_gs_table_depth);
	adev->gfx.config.gs_prim_buffer_depth = le32_to_cpu(gc_info->gc_gsprim_buff_depth);
	adev->gfx.config.double_offchip_lds_buf = le32_to_cpu(gc_info->gc_double_offchip_lds_buffer);
	adev->gfx.cu_info.wave_front_size = le32_to_cpu(gc_info->gc_wave_size);
	adev->gfx.cu_info.max_waves_per_simd = le32_to_cpu(gc_info->gc_max_waves_per_simd);
	adev->gfx.cu_info.max_scratch_slots_per_cu = le32_to_cpu(gc_info->gc_max_scratch_slots_per_cu);
	adev->gfx.cu_info.lds_size = le32_to_cpu(gc_info->gc_lds_size);
	adev->gfx.config.num_sc_per_sh = le32_to_cpu(gc_info->gc_num_sc_per_se) /
					 le32_to_cpu(gc_info->gc_num_sa_per_se);
	adev->gfx.config.num_packer_per_sc = le32_to_cpu(gc_info->gc_num_packer_per_sc);

	return 0;
}
