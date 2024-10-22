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

#include <linux/firmware.h>

#include "amdgpu.h"
#include "amdgpu_discovery.h"
#include "soc15_hw_ip.h"
#include "discovery.h"
#include "amdgpu_ras.h"

#include "soc15.h"
#include "gfx_v9_0.h"
#include "gfx_v9_4_3.h"
#include "gmc_v9_0.h"
#include "df_v1_7.h"
#include "df_v3_6.h"
#include "df_v4_3.h"
#include "df_v4_6_2.h"
#include "df_v4_15.h"
#include "nbio_v6_1.h"
#include "nbio_v7_0.h"
#include "nbio_v7_4.h"
#include "nbio_v7_9.h"
#include "nbio_v7_11.h"
#include "hdp_v4_0.h"
#include "vega10_ih.h"
#include "vega20_ih.h"
#include "sdma_v4_0.h"
#include "sdma_v4_4_2.h"
#include "uvd_v7_0.h"
#include "vce_v4_0.h"
#include "vcn_v1_0.h"
#include "vcn_v2_5.h"
#include "jpeg_v2_5.h"
#include "smuio_v9_0.h"
#include "gmc_v10_0.h"
#include "gmc_v11_0.h"
#include "gmc_v12_0.h"
#include "gfxhub_v2_0.h"
#include "mmhub_v2_0.h"
#include "nbio_v2_3.h"
#include "nbio_v4_3.h"
#include "nbio_v7_2.h"
#include "nbio_v7_7.h"
#include "nbif_v6_3_1.h"
#include "hdp_v5_0.h"
#include "hdp_v5_2.h"
#include "hdp_v6_0.h"
#include "hdp_v7_0.h"
#include "nv.h"
#include "soc21.h"
#include "soc24.h"
#include "navi10_ih.h"
#include "ih_v6_0.h"
#include "ih_v6_1.h"
#include "ih_v7_0.h"
#include "gfx_v10_0.h"
#include "gfx_v11_0.h"
#include "gfx_v12_0.h"
#include "sdma_v5_0.h"
#include "sdma_v5_2.h"
#include "sdma_v6_0.h"
#include "sdma_v7_0.h"
#include "lsdma_v6_0.h"
#include "lsdma_v7_0.h"
#include "vcn_v2_0.h"
#include "jpeg_v2_0.h"
#include "vcn_v3_0.h"
#include "jpeg_v3_0.h"
#include "vcn_v4_0.h"
#include "jpeg_v4_0.h"
#include "vcn_v4_0_3.h"
#include "jpeg_v4_0_3.h"
#include "vcn_v4_0_5.h"
#include "jpeg_v4_0_5.h"
#include "amdgpu_vkms.h"
#include "mes_v11_0.h"
#include "mes_v12_0.h"
#include "smuio_v11_0.h"
#include "smuio_v11_0_6.h"
#include "smuio_v13_0.h"
#include "smuio_v13_0_3.h"
#include "smuio_v13_0_6.h"
#include "smuio_v14_0_2.h"
#include "vcn_v5_0_0.h"
#include "jpeg_v5_0_0.h"

#include "amdgpu_vpe.h"
#if defined(CONFIG_DRM_AMD_ISP)
#include "amdgpu_isp.h"
#endif

#define FIRMWARE_IP_DISCOVERY "amdgpu/ip_discovery.bin"
MODULE_FIRMWARE(FIRMWARE_IP_DISCOVERY);

#define mmIP_DISCOVERY_VERSION  0x16A00
#define mmRCC_CONFIG_MEMSIZE	0xde3
#define mmMP0_SMN_C2PMSG_33	0x16061
#define mmMM_INDEX		0x0
#define mmMM_INDEX_HI		0x6
#define mmMM_DATA		0x1

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
	[SDMA2_HWID]		= "SDMA2",
	[SDMA3_HWID]		= "SDMA3",
	[LSDMA_HWID]		= "LSDMA",
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
	[VPE_HWID]		= "VPE",
};

static int hw_id_map[MAX_HWIP] = {
	[GC_HWIP]	= GC_HWID,
	[HDP_HWIP]	= HDP_HWID,
	[SDMA0_HWIP]	= SDMA0_HWID,
	[SDMA1_HWIP]	= SDMA1_HWID,
	[SDMA2_HWIP]    = SDMA2_HWID,
	[SDMA3_HWIP]    = SDMA3_HWID,
	[LSDMA_HWIP]    = LSDMA_HWID,
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
	[XGMI_HWIP]	= XGMI_HWID,
	[DCI_HWIP]	= DCI_HWID,
	[PCIE_HWIP]	= PCIE_HWID,
	[VPE_HWIP]	= VPE_HWID,
	[ISP_HWIP]	= ISP_HWID,
};

static int amdgpu_discovery_read_binary_from_sysmem(struct amdgpu_device *adev, uint8_t *binary)
{
	u64 tmr_offset, tmr_size, pos;
	void *discv_regn;
	int ret;

	ret = amdgpu_acpi_get_tmr_info(adev, &tmr_offset, &tmr_size);
	if (ret)
		return ret;

	pos = tmr_offset + tmr_size - DISCOVERY_TMR_OFFSET;

	/* This region is read-only and reserved from system use */
	discv_regn = memremap(pos, adev->mman.discovery_tmr_size, MEMREMAP_WC);
	if (discv_regn) {
		memcpy(binary, discv_regn, adev->mman.discovery_tmr_size);
		memunmap(discv_regn);
		return 0;
	}

	return -ENOENT;
}

#define IP_DISCOVERY_V2		2
#define IP_DISCOVERY_V4		4

static int amdgpu_discovery_read_binary_from_mem(struct amdgpu_device *adev,
						 uint8_t *binary)
{
	uint64_t vram_size;
	u32 msg;
	int i, ret = 0;

	if (!amdgpu_sriov_vf(adev)) {
		/* It can take up to a second for IFWI init to complete on some dGPUs,
		 * but generally it should be in the 60-100ms range.  Normally this starts
		 * as soon as the device gets power so by the time the OS loads this has long
		 * completed.  However, when a card is hotplugged via e.g., USB4, we need to
		 * wait for this to complete.  Once the C2PMSG is updated, we can
		 * continue.
		 */

		for (i = 0; i < 1000; i++) {
			msg = RREG32(mmMP0_SMN_C2PMSG_33);
			if (msg & 0x80000000)
				break;
			msleep(1);
		}
	}

	vram_size = (uint64_t)RREG32(mmRCC_CONFIG_MEMSIZE) << 20;

	if (vram_size) {
		uint64_t pos = vram_size - DISCOVERY_TMR_OFFSET;
		amdgpu_device_vram_access(adev, pos, (uint32_t *)binary,
					  adev->mman.discovery_tmr_size, false);
	} else {
		ret = amdgpu_discovery_read_binary_from_sysmem(adev, binary);
	}

	return ret;
}

static int amdgpu_discovery_read_binary_from_file(struct amdgpu_device *adev, uint8_t *binary)
{
	const struct firmware *fw;
	const char *fw_name;
	int r;

	switch (amdgpu_discovery) {
	case 2:
		fw_name = FIRMWARE_IP_DISCOVERY;
		break;
	default:
		dev_warn(adev->dev, "amdgpu_discovery is not set properly\n");
		return -EINVAL;
	}

	r = request_firmware(&fw, fw_name, adev->dev);
	if (r) {
		dev_err(adev->dev, "can't load firmware \"%s\"\n",
			fw_name);
		return r;
	}

	memcpy((u8 *)binary, (u8 *)fw->data, fw->size);
	release_firmware(fw);

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

static inline bool amdgpu_discovery_verify_binary_signature(uint8_t *binary)
{
	struct binary_header *bhdr;
	bhdr = (struct binary_header *)binary;

	return (le32_to_cpu(bhdr->binary_signature) == BINARY_SIGNATURE);
}

static void amdgpu_discovery_harvest_config_quirk(struct amdgpu_device *adev)
{
	/*
	 * So far, apply this quirk only on those Navy Flounder boards which
	 * have a bad harvest table of VCN config.
	 */
	if ((amdgpu_ip_version(adev, UVD_HWIP, 1) == IP_VERSION(3, 0, 1)) &&
	    (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(10, 3, 2))) {
		switch (adev->pdev->revision) {
		case 0xC1:
		case 0xC2:
		case 0xC3:
		case 0xC5:
		case 0xC7:
		case 0xCF:
		case 0xDF:
			adev->vcn.harvest_config |= AMDGPU_VCN_HARVEST_VCN1;
			adev->vcn.inst_mask &= ~AMDGPU_VCN_HARVEST_VCN1;
			break;
		default:
			break;
		}
	}
}

static int amdgpu_discovery_verify_npsinfo(struct amdgpu_device *adev,
					   struct binary_header *bhdr)
{
	struct table_info *info;
	uint16_t checksum;
	uint16_t offset;

	info = &bhdr->table_list[NPS_INFO];
	offset = le16_to_cpu(info->offset);
	checksum = le16_to_cpu(info->checksum);

	struct nps_info_header *nhdr =
		(struct nps_info_header *)(adev->mman.discovery_bin + offset);

	if (le32_to_cpu(nhdr->table_id) != NPS_INFO_TABLE_ID) {
		dev_dbg(adev->dev, "invalid ip discovery nps info table id\n");
		return -EINVAL;
	}

	if (!amdgpu_discovery_verify_checksum(adev->mman.discovery_bin + offset,
					      le32_to_cpu(nhdr->size_bytes),
					      checksum)) {
		dev_dbg(adev->dev, "invalid nps info data table checksum\n");
		return -EINVAL;
	}

	return 0;
}

static int amdgpu_discovery_init(struct amdgpu_device *adev)
{
	struct table_info *info;
	struct binary_header *bhdr;
	uint16_t offset;
	uint16_t size;
	uint16_t checksum;
	int r;

	adev->mman.discovery_tmr_size = DISCOVERY_TMR_SIZE;
	adev->mman.discovery_bin = kzalloc(adev->mman.discovery_tmr_size, GFP_KERNEL);
	if (!adev->mman.discovery_bin)
		return -ENOMEM;

	/* Read from file if it is the preferred option */
	if (amdgpu_discovery == 2) {
		dev_info(adev->dev, "use ip discovery information from file");
		r = amdgpu_discovery_read_binary_from_file(adev, adev->mman.discovery_bin);

		if (r) {
			dev_err(adev->dev, "failed to read ip discovery binary from file\n");
			r = -EINVAL;
			goto out;
		}

	} else {
		r = amdgpu_discovery_read_binary_from_mem(
			adev, adev->mman.discovery_bin);
		if (r)
			goto out;
	}

	/* check the ip discovery binary signature */
	if (!amdgpu_discovery_verify_binary_signature(adev->mman.discovery_bin)) {
		dev_err(adev->dev,
			"get invalid ip discovery binary signature\n");
		r = -EINVAL;
		goto out;
	}

	bhdr = (struct binary_header *)adev->mman.discovery_bin;

	offset = offsetof(struct binary_header, binary_checksum) +
		sizeof(bhdr->binary_checksum);
	size = le16_to_cpu(bhdr->binary_size) - offset;
	checksum = le16_to_cpu(bhdr->binary_checksum);

	if (!amdgpu_discovery_verify_checksum(adev->mman.discovery_bin + offset,
					      size, checksum)) {
		dev_err(adev->dev, "invalid ip discovery binary checksum\n");
		r = -EINVAL;
		goto out;
	}

	info = &bhdr->table_list[IP_DISCOVERY];
	offset = le16_to_cpu(info->offset);
	checksum = le16_to_cpu(info->checksum);

	if (offset) {
		struct ip_discovery_header *ihdr =
			(struct ip_discovery_header *)(adev->mman.discovery_bin + offset);
		if (le32_to_cpu(ihdr->signature) != DISCOVERY_TABLE_SIGNATURE) {
			dev_err(adev->dev, "invalid ip discovery data table signature\n");
			r = -EINVAL;
			goto out;
		}

		if (!amdgpu_discovery_verify_checksum(adev->mman.discovery_bin + offset,
						      le16_to_cpu(ihdr->size), checksum)) {
			dev_err(adev->dev, "invalid ip discovery data table checksum\n");
			r = -EINVAL;
			goto out;
		}
	}

	info = &bhdr->table_list[GC];
	offset = le16_to_cpu(info->offset);
	checksum = le16_to_cpu(info->checksum);

	if (offset) {
		struct gpu_info_header *ghdr =
			(struct gpu_info_header *)(adev->mman.discovery_bin + offset);

		if (le32_to_cpu(ghdr->table_id) != GC_TABLE_ID) {
			dev_err(adev->dev, "invalid ip discovery gc table id\n");
			r = -EINVAL;
			goto out;
		}

		if (!amdgpu_discovery_verify_checksum(adev->mman.discovery_bin + offset,
						      le32_to_cpu(ghdr->size), checksum)) {
			dev_err(adev->dev, "invalid gc data table checksum\n");
			r = -EINVAL;
			goto out;
		}
	}

	info = &bhdr->table_list[HARVEST_INFO];
	offset = le16_to_cpu(info->offset);
	checksum = le16_to_cpu(info->checksum);

	if (offset) {
		struct harvest_info_header *hhdr =
			(struct harvest_info_header *)(adev->mman.discovery_bin + offset);

		if (le32_to_cpu(hhdr->signature) != HARVEST_TABLE_SIGNATURE) {
			dev_err(adev->dev, "invalid ip discovery harvest table signature\n");
			r = -EINVAL;
			goto out;
		}

		if (!amdgpu_discovery_verify_checksum(adev->mman.discovery_bin + offset,
						      sizeof(struct harvest_table), checksum)) {
			dev_err(adev->dev, "invalid harvest data table checksum\n");
			r = -EINVAL;
			goto out;
		}
	}

	info = &bhdr->table_list[VCN_INFO];
	offset = le16_to_cpu(info->offset);
	checksum = le16_to_cpu(info->checksum);

	if (offset) {
		struct vcn_info_header *vhdr =
			(struct vcn_info_header *)(adev->mman.discovery_bin + offset);

		if (le32_to_cpu(vhdr->table_id) != VCN_INFO_TABLE_ID) {
			dev_err(adev->dev, "invalid ip discovery vcn table id\n");
			r = -EINVAL;
			goto out;
		}

		if (!amdgpu_discovery_verify_checksum(adev->mman.discovery_bin + offset,
						      le32_to_cpu(vhdr->size_bytes), checksum)) {
			dev_err(adev->dev, "invalid vcn data table checksum\n");
			r = -EINVAL;
			goto out;
		}
	}

	info = &bhdr->table_list[MALL_INFO];
	offset = le16_to_cpu(info->offset);
	checksum = le16_to_cpu(info->checksum);

	if (0 && offset) {
		struct mall_info_header *mhdr =
			(struct mall_info_header *)(adev->mman.discovery_bin + offset);

		if (le32_to_cpu(mhdr->table_id) != MALL_INFO_TABLE_ID) {
			dev_err(adev->dev, "invalid ip discovery mall table id\n");
			r = -EINVAL;
			goto out;
		}

		if (!amdgpu_discovery_verify_checksum(adev->mman.discovery_bin + offset,
						      le32_to_cpu(mhdr->size_bytes), checksum)) {
			dev_err(adev->dev, "invalid mall data table checksum\n");
			r = -EINVAL;
			goto out;
		}
	}

	return 0;

out:
	kfree(adev->mman.discovery_bin);
	adev->mman.discovery_bin = NULL;
	if ((amdgpu_discovery != 2) &&
	    (RREG32(mmIP_DISCOVERY_VERSION) == 4))
		amdgpu_ras_query_boot_status(adev, 4);
	return r;
}

static void amdgpu_discovery_sysfs_fini(struct amdgpu_device *adev);

void amdgpu_discovery_fini(struct amdgpu_device *adev)
{
	amdgpu_discovery_sysfs_fini(adev);
	kfree(adev->mman.discovery_bin);
	adev->mman.discovery_bin = NULL;
}

static int amdgpu_discovery_validate_ip(const struct ip_v4 *ip)
{
	if (ip->instance_number >= HWIP_MAX_INSTANCE) {
		DRM_ERROR("Unexpected instance_number (%d) from ip discovery blob\n",
			  ip->instance_number);
		return -EINVAL;
	}
	if (le16_to_cpu(ip->hw_id) >= HW_ID_MAX) {
		DRM_ERROR("Unexpected hw_id (%d) from ip discovery blob\n",
			  le16_to_cpu(ip->hw_id));
		return -EINVAL;
	}

	return 0;
}

static void amdgpu_discovery_read_harvest_bit_per_ip(struct amdgpu_device *adev,
						uint32_t *vcn_harvest_count)
{
	struct binary_header *bhdr;
	struct ip_discovery_header *ihdr;
	struct die_header *dhdr;
	struct ip_v4 *ip;
	uint16_t die_offset, ip_offset, num_dies, num_ips;
	int i, j;

	bhdr = (struct binary_header *)adev->mman.discovery_bin;
	ihdr = (struct ip_discovery_header *)(adev->mman.discovery_bin +
			le16_to_cpu(bhdr->table_list[IP_DISCOVERY].offset));
	num_dies = le16_to_cpu(ihdr->num_dies);

	/* scan harvest bit of all IP data structures */
	for (i = 0; i < num_dies; i++) {
		die_offset = le16_to_cpu(ihdr->die_info[i].die_offset);
		dhdr = (struct die_header *)(adev->mman.discovery_bin + die_offset);
		num_ips = le16_to_cpu(dhdr->num_ips);
		ip_offset = die_offset + sizeof(*dhdr);

		for (j = 0; j < num_ips; j++) {
			ip = (struct ip_v4 *)(adev->mman.discovery_bin + ip_offset);

			if (amdgpu_discovery_validate_ip(ip))
				goto next_ip;

			if (le16_to_cpu(ip->variant) == 1) {
				switch (le16_to_cpu(ip->hw_id)) {
				case VCN_HWID:
					(*vcn_harvest_count)++;
					if (ip->instance_number == 0) {
						adev->vcn.harvest_config |= AMDGPU_VCN_HARVEST_VCN0;
						adev->vcn.inst_mask &=
							~AMDGPU_VCN_HARVEST_VCN0;
						adev->jpeg.inst_mask &=
							~AMDGPU_VCN_HARVEST_VCN0;
					} else {
						adev->vcn.harvest_config |= AMDGPU_VCN_HARVEST_VCN1;
						adev->vcn.inst_mask &=
							~AMDGPU_VCN_HARVEST_VCN1;
						adev->jpeg.inst_mask &=
							~AMDGPU_VCN_HARVEST_VCN1;
					}
					break;
				case DMU_HWID:
					adev->harvest_ip_mask |= AMD_HARVEST_IP_DMU_MASK;
					break;
				default:
					break;
				}
			}
next_ip:
			if (ihdr->base_addr_64_bit)
				ip_offset += struct_size(ip, base_address_64, ip->num_base_address);
			else
				ip_offset += struct_size(ip, base_address, ip->num_base_address);
		}
	}
}

static void amdgpu_discovery_read_from_harvest_table(struct amdgpu_device *adev,
						     uint32_t *vcn_harvest_count,
						     uint32_t *umc_harvest_count)
{
	struct binary_header *bhdr;
	struct harvest_table *harvest_info;
	u16 offset;
	int i;
	uint32_t umc_harvest_config = 0;

	bhdr = (struct binary_header *)adev->mman.discovery_bin;
	offset = le16_to_cpu(bhdr->table_list[HARVEST_INFO].offset);

	if (!offset) {
		dev_err(adev->dev, "invalid harvest table offset\n");
		return;
	}

	harvest_info = (struct harvest_table *)(adev->mman.discovery_bin + offset);

	for (i = 0; i < 32; i++) {
		if (le16_to_cpu(harvest_info->list[i].hw_id) == 0)
			break;

		switch (le16_to_cpu(harvest_info->list[i].hw_id)) {
		case VCN_HWID:
			(*vcn_harvest_count)++;
			adev->vcn.harvest_config |=
				(1 << harvest_info->list[i].number_instance);
			adev->jpeg.harvest_config |=
				(1 << harvest_info->list[i].number_instance);

			adev->vcn.inst_mask &=
				~(1U << harvest_info->list[i].number_instance);
			adev->jpeg.inst_mask &=
				~(1U << harvest_info->list[i].number_instance);
			break;
		case DMU_HWID:
			adev->harvest_ip_mask |= AMD_HARVEST_IP_DMU_MASK;
			break;
		case UMC_HWID:
			umc_harvest_config |=
				1 << (le16_to_cpu(harvest_info->list[i].number_instance));
			(*umc_harvest_count)++;
			break;
		case GC_HWID:
			adev->gfx.xcc_mask &=
				~(1U << harvest_info->list[i].number_instance);
			break;
		case SDMA0_HWID:
			adev->sdma.sdma_mask &=
				~(1U << harvest_info->list[i].number_instance);
			break;
#if defined(CONFIG_DRM_AMD_ISP)
		case ISP_HWID:
			adev->isp.harvest_config |=
				~(1U << harvest_info->list[i].number_instance);
			break;
#endif
		default:
			break;
		}
	}

	adev->umc.active_mask = ((1 << adev->umc.node_inst_num) - 1) &
				~umc_harvest_config;
}

/* ================================================== */

struct ip_hw_instance {
	struct kobject kobj; /* ip_discovery/die/#die/#hw_id/#instance/<attrs...> */

	int hw_id;
	u8  num_instance;
	u8  major, minor, revision;
	u8  harvest;

	int num_base_addresses;
	u32 base_addr[] __counted_by(num_base_addresses);
};

struct ip_hw_id {
	struct kset hw_id_kset;  /* ip_discovery/die/#die/#hw_id/, contains ip_hw_instance */
	int hw_id;
};

struct ip_die_entry {
	struct kset ip_kset;     /* ip_discovery/die/#die/, contains ip_hw_id  */
	u16 num_ips;
};

/* -------------------------------------------------- */

struct ip_hw_instance_attr {
	struct attribute attr;
	ssize_t (*show)(struct ip_hw_instance *ip_hw_instance, char *buf);
};

static ssize_t hw_id_show(struct ip_hw_instance *ip_hw_instance, char *buf)
{
	return sysfs_emit(buf, "%d\n", ip_hw_instance->hw_id);
}

static ssize_t num_instance_show(struct ip_hw_instance *ip_hw_instance, char *buf)
{
	return sysfs_emit(buf, "%d\n", ip_hw_instance->num_instance);
}

static ssize_t major_show(struct ip_hw_instance *ip_hw_instance, char *buf)
{
	return sysfs_emit(buf, "%d\n", ip_hw_instance->major);
}

static ssize_t minor_show(struct ip_hw_instance *ip_hw_instance, char *buf)
{
	return sysfs_emit(buf, "%d\n", ip_hw_instance->minor);
}

static ssize_t revision_show(struct ip_hw_instance *ip_hw_instance, char *buf)
{
	return sysfs_emit(buf, "%d\n", ip_hw_instance->revision);
}

static ssize_t harvest_show(struct ip_hw_instance *ip_hw_instance, char *buf)
{
	return sysfs_emit(buf, "0x%01X\n", ip_hw_instance->harvest);
}

static ssize_t num_base_addresses_show(struct ip_hw_instance *ip_hw_instance, char *buf)
{
	return sysfs_emit(buf, "%d\n", ip_hw_instance->num_base_addresses);
}

static ssize_t base_addr_show(struct ip_hw_instance *ip_hw_instance, char *buf)
{
	ssize_t res, at;
	int ii;

	for (res = at = ii = 0; ii < ip_hw_instance->num_base_addresses; ii++) {
		/* Here we satisfy the condition that, at + size <= PAGE_SIZE.
		 */
		if (at + 12 > PAGE_SIZE)
			break;
		res = sysfs_emit_at(buf, at, "0x%08X\n",
				    ip_hw_instance->base_addr[ii]);
		if (res <= 0)
			break;
		at += res;
	}

	return res < 0 ? res : at;
}

static struct ip_hw_instance_attr ip_hw_attr[] = {
	__ATTR_RO(hw_id),
	__ATTR_RO(num_instance),
	__ATTR_RO(major),
	__ATTR_RO(minor),
	__ATTR_RO(revision),
	__ATTR_RO(harvest),
	__ATTR_RO(num_base_addresses),
	__ATTR_RO(base_addr),
};

static struct attribute *ip_hw_instance_attrs[ARRAY_SIZE(ip_hw_attr) + 1];
ATTRIBUTE_GROUPS(ip_hw_instance);

#define to_ip_hw_instance(x) container_of(x, struct ip_hw_instance, kobj)
#define to_ip_hw_instance_attr(x) container_of(x, struct ip_hw_instance_attr, attr)

static ssize_t ip_hw_instance_attr_show(struct kobject *kobj,
					struct attribute *attr,
					char *buf)
{
	struct ip_hw_instance *ip_hw_instance = to_ip_hw_instance(kobj);
	struct ip_hw_instance_attr *ip_hw_attr = to_ip_hw_instance_attr(attr);

	if (!ip_hw_attr->show)
		return -EIO;

	return ip_hw_attr->show(ip_hw_instance, buf);
}

static const struct sysfs_ops ip_hw_instance_sysfs_ops = {
	.show = ip_hw_instance_attr_show,
};

static void ip_hw_instance_release(struct kobject *kobj)
{
	struct ip_hw_instance *ip_hw_instance = to_ip_hw_instance(kobj);

	kfree(ip_hw_instance);
}

static const struct kobj_type ip_hw_instance_ktype = {
	.release = ip_hw_instance_release,
	.sysfs_ops = &ip_hw_instance_sysfs_ops,
	.default_groups = ip_hw_instance_groups,
};

/* -------------------------------------------------- */

#define to_ip_hw_id(x)  container_of(to_kset(x), struct ip_hw_id, hw_id_kset)

static void ip_hw_id_release(struct kobject *kobj)
{
	struct ip_hw_id *ip_hw_id = to_ip_hw_id(kobj);

	if (!list_empty(&ip_hw_id->hw_id_kset.list))
		DRM_ERROR("ip_hw_id->hw_id_kset is not empty");
	kfree(ip_hw_id);
}

static const struct kobj_type ip_hw_id_ktype = {
	.release = ip_hw_id_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

/* -------------------------------------------------- */

static void die_kobj_release(struct kobject *kobj);
static void ip_disc_release(struct kobject *kobj);

struct ip_die_entry_attribute {
	struct attribute attr;
	ssize_t (*show)(struct ip_die_entry *ip_die_entry, char *buf);
};

#define to_ip_die_entry_attr(x)  container_of(x, struct ip_die_entry_attribute, attr)

static ssize_t num_ips_show(struct ip_die_entry *ip_die_entry, char *buf)
{
	return sysfs_emit(buf, "%d\n", ip_die_entry->num_ips);
}

/* If there are more ip_die_entry attrs, other than the number of IPs,
 * we can make this intro an array of attrs, and then initialize
 * ip_die_entry_attrs in a loop.
 */
static struct ip_die_entry_attribute num_ips_attr =
	__ATTR_RO(num_ips);

static struct attribute *ip_die_entry_attrs[] = {
	&num_ips_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ip_die_entry); /* ip_die_entry_groups */

#define to_ip_die_entry(x) container_of(to_kset(x), struct ip_die_entry, ip_kset)

static ssize_t ip_die_entry_attr_show(struct kobject *kobj,
				      struct attribute *attr,
				      char *buf)
{
	struct ip_die_entry_attribute *ip_die_entry_attr = to_ip_die_entry_attr(attr);
	struct ip_die_entry *ip_die_entry = to_ip_die_entry(kobj);

	if (!ip_die_entry_attr->show)
		return -EIO;

	return ip_die_entry_attr->show(ip_die_entry, buf);
}

static void ip_die_entry_release(struct kobject *kobj)
{
	struct ip_die_entry *ip_die_entry = to_ip_die_entry(kobj);

	if (!list_empty(&ip_die_entry->ip_kset.list))
		DRM_ERROR("ip_die_entry->ip_kset is not empty");
	kfree(ip_die_entry);
}

static const struct sysfs_ops ip_die_entry_sysfs_ops = {
	.show = ip_die_entry_attr_show,
};

static const struct kobj_type ip_die_entry_ktype = {
	.release = ip_die_entry_release,
	.sysfs_ops = &ip_die_entry_sysfs_ops,
	.default_groups = ip_die_entry_groups,
};

static const struct kobj_type die_kobj_ktype = {
	.release = die_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

static const struct kobj_type ip_discovery_ktype = {
	.release = ip_disc_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

struct ip_discovery_top {
	struct kobject kobj;    /* ip_discovery/ */
	struct kset die_kset;   /* ip_discovery/die/, contains ip_die_entry */
	struct amdgpu_device *adev;
};

static void die_kobj_release(struct kobject *kobj)
{
	struct ip_discovery_top *ip_top = container_of(to_kset(kobj),
						       struct ip_discovery_top,
						       die_kset);
	if (!list_empty(&ip_top->die_kset.list))
		DRM_ERROR("ip_top->die_kset is not empty");
}

static void ip_disc_release(struct kobject *kobj)
{
	struct ip_discovery_top *ip_top = container_of(kobj, struct ip_discovery_top,
						       kobj);
	struct amdgpu_device *adev = ip_top->adev;

	adev->ip_top = NULL;
	kfree(ip_top);
}

static uint8_t amdgpu_discovery_get_harvest_info(struct amdgpu_device *adev,
						 uint16_t hw_id, uint8_t inst)
{
	uint8_t harvest = 0;

	/* Until a uniform way is figured, get mask based on hwid */
	switch (hw_id) {
	case VCN_HWID:
		harvest = ((1 << inst) & adev->vcn.inst_mask) == 0;
		break;
	case DMU_HWID:
		if (adev->harvest_ip_mask & AMD_HARVEST_IP_DMU_MASK)
			harvest = 0x1;
		break;
	case UMC_HWID:
		/* TODO: It needs another parsing; for now, ignore.*/
		break;
	case GC_HWID:
		harvest = ((1 << inst) & adev->gfx.xcc_mask) == 0;
		break;
	case SDMA0_HWID:
		harvest = ((1 << inst) & adev->sdma.sdma_mask) == 0;
		break;
	default:
		break;
	}

	return harvest;
}

static int amdgpu_discovery_sysfs_ips(struct amdgpu_device *adev,
				      struct ip_die_entry *ip_die_entry,
				      const size_t _ip_offset, const int num_ips,
				      bool reg_base_64)
{
	int ii, jj, kk, res;

	DRM_DEBUG("num_ips:%d", num_ips);

	/* Find all IPs of a given HW ID, and add their instance to
	 * #die/#hw_id/#instance/<attributes>
	 */
	for (ii = 0; ii < HW_ID_MAX; ii++) {
		struct ip_hw_id *ip_hw_id = NULL;
		size_t ip_offset = _ip_offset;

		for (jj = 0; jj < num_ips; jj++) {
			struct ip_v4 *ip;
			struct ip_hw_instance *ip_hw_instance;

			ip = (struct ip_v4 *)(adev->mman.discovery_bin + ip_offset);
			if (amdgpu_discovery_validate_ip(ip) ||
			    le16_to_cpu(ip->hw_id) != ii)
				goto next_ip;

			DRM_DEBUG("match:%d @ ip_offset:%zu", ii, ip_offset);

			/* We have a hw_id match; register the hw
			 * block if not yet registered.
			 */
			if (!ip_hw_id) {
				ip_hw_id = kzalloc(sizeof(*ip_hw_id), GFP_KERNEL);
				if (!ip_hw_id)
					return -ENOMEM;
				ip_hw_id->hw_id = ii;

				kobject_set_name(&ip_hw_id->hw_id_kset.kobj, "%d", ii);
				ip_hw_id->hw_id_kset.kobj.kset = &ip_die_entry->ip_kset;
				ip_hw_id->hw_id_kset.kobj.ktype = &ip_hw_id_ktype;
				res = kset_register(&ip_hw_id->hw_id_kset);
				if (res) {
					DRM_ERROR("Couldn't register ip_hw_id kset");
					kfree(ip_hw_id);
					return res;
				}
				if (hw_id_names[ii]) {
					res = sysfs_create_link(&ip_die_entry->ip_kset.kobj,
								&ip_hw_id->hw_id_kset.kobj,
								hw_id_names[ii]);
					if (res) {
						DRM_ERROR("Couldn't create IP link %s in IP Die:%s\n",
							  hw_id_names[ii],
							  kobject_name(&ip_die_entry->ip_kset.kobj));
					}
				}
			}

			/* Now register its instance.
			 */
			ip_hw_instance = kzalloc(struct_size(ip_hw_instance,
							     base_addr,
							     ip->num_base_address),
						 GFP_KERNEL);
			if (!ip_hw_instance) {
				DRM_ERROR("no memory for ip_hw_instance");
				return -ENOMEM;
			}
			ip_hw_instance->hw_id = le16_to_cpu(ip->hw_id); /* == ii */
			ip_hw_instance->num_instance = ip->instance_number;
			ip_hw_instance->major = ip->major;
			ip_hw_instance->minor = ip->minor;
			ip_hw_instance->revision = ip->revision;
			ip_hw_instance->harvest =
				amdgpu_discovery_get_harvest_info(
					adev, ip_hw_instance->hw_id,
					ip_hw_instance->num_instance);
			ip_hw_instance->num_base_addresses = ip->num_base_address;

			for (kk = 0; kk < ip_hw_instance->num_base_addresses; kk++) {
				if (reg_base_64)
					ip_hw_instance->base_addr[kk] =
						lower_32_bits(le64_to_cpu(ip->base_address_64[kk])) & 0x3FFFFFFF;
				else
					ip_hw_instance->base_addr[kk] = ip->base_address[kk];
			}

			kobject_init(&ip_hw_instance->kobj, &ip_hw_instance_ktype);
			ip_hw_instance->kobj.kset = &ip_hw_id->hw_id_kset;
			res = kobject_add(&ip_hw_instance->kobj, NULL,
					  "%d", ip_hw_instance->num_instance);
next_ip:
			if (reg_base_64)
				ip_offset += struct_size(ip, base_address_64,
							 ip->num_base_address);
			else
				ip_offset += struct_size(ip, base_address,
							 ip->num_base_address);
		}
	}

	return 0;
}

static int amdgpu_discovery_sysfs_recurse(struct amdgpu_device *adev)
{
	struct binary_header *bhdr;
	struct ip_discovery_header *ihdr;
	struct die_header *dhdr;
	struct kset *die_kset = &adev->ip_top->die_kset;
	u16 num_dies, die_offset, num_ips;
	size_t ip_offset;
	int ii, res;

	bhdr = (struct binary_header *)adev->mman.discovery_bin;
	ihdr = (struct ip_discovery_header *)(adev->mman.discovery_bin +
					      le16_to_cpu(bhdr->table_list[IP_DISCOVERY].offset));
	num_dies = le16_to_cpu(ihdr->num_dies);

	DRM_DEBUG("number of dies: %d\n", num_dies);

	for (ii = 0; ii < num_dies; ii++) {
		struct ip_die_entry *ip_die_entry;

		die_offset = le16_to_cpu(ihdr->die_info[ii].die_offset);
		dhdr = (struct die_header *)(adev->mman.discovery_bin + die_offset);
		num_ips = le16_to_cpu(dhdr->num_ips);
		ip_offset = die_offset + sizeof(*dhdr);

		/* Add the die to the kset.
		 *
		 * dhdr->die_id == ii, which was checked in
		 * amdgpu_discovery_reg_base_init().
		 */

		ip_die_entry = kzalloc(sizeof(*ip_die_entry), GFP_KERNEL);
		if (!ip_die_entry)
			return -ENOMEM;

		ip_die_entry->num_ips = num_ips;

		kobject_set_name(&ip_die_entry->ip_kset.kobj, "%d", le16_to_cpu(dhdr->die_id));
		ip_die_entry->ip_kset.kobj.kset = die_kset;
		ip_die_entry->ip_kset.kobj.ktype = &ip_die_entry_ktype;
		res = kset_register(&ip_die_entry->ip_kset);
		if (res) {
			DRM_ERROR("Couldn't register ip_die_entry kset");
			kfree(ip_die_entry);
			return res;
		}

		amdgpu_discovery_sysfs_ips(adev, ip_die_entry, ip_offset, num_ips, !!ihdr->base_addr_64_bit);
	}

	return 0;
}

static int amdgpu_discovery_sysfs_init(struct amdgpu_device *adev)
{
	struct kset *die_kset;
	int res, ii;

	if (!adev->mman.discovery_bin)
		return -EINVAL;

	adev->ip_top = kzalloc(sizeof(*adev->ip_top), GFP_KERNEL);
	if (!adev->ip_top)
		return -ENOMEM;

	adev->ip_top->adev = adev;

	res = kobject_init_and_add(&adev->ip_top->kobj, &ip_discovery_ktype,
				   &adev->dev->kobj, "ip_discovery");
	if (res) {
		DRM_ERROR("Couldn't init and add ip_discovery/");
		goto Err;
	}

	die_kset = &adev->ip_top->die_kset;
	kobject_set_name(&die_kset->kobj, "%s", "die");
	die_kset->kobj.parent = &adev->ip_top->kobj;
	die_kset->kobj.ktype = &die_kobj_ktype;
	res = kset_register(&adev->ip_top->die_kset);
	if (res) {
		DRM_ERROR("Couldn't register die_kset");
		goto Err;
	}

	for (ii = 0; ii < ARRAY_SIZE(ip_hw_attr); ii++)
		ip_hw_instance_attrs[ii] = &ip_hw_attr[ii].attr;
	ip_hw_instance_attrs[ii] = NULL;

	res = amdgpu_discovery_sysfs_recurse(adev);

	return res;
Err:
	kobject_put(&adev->ip_top->kobj);
	return res;
}

/* -------------------------------------------------- */

#define list_to_kobj(el) container_of(el, struct kobject, entry)

static void amdgpu_discovery_sysfs_ip_hw_free(struct ip_hw_id *ip_hw_id)
{
	struct list_head *el, *tmp;
	struct kset *hw_id_kset;

	hw_id_kset = &ip_hw_id->hw_id_kset;
	spin_lock(&hw_id_kset->list_lock);
	list_for_each_prev_safe(el, tmp, &hw_id_kset->list) {
		list_del_init(el);
		spin_unlock(&hw_id_kset->list_lock);
		/* kobject is embedded in ip_hw_instance */
		kobject_put(list_to_kobj(el));
		spin_lock(&hw_id_kset->list_lock);
	}
	spin_unlock(&hw_id_kset->list_lock);
	kobject_put(&ip_hw_id->hw_id_kset.kobj);
}

static void amdgpu_discovery_sysfs_die_free(struct ip_die_entry *ip_die_entry)
{
	struct list_head *el, *tmp;
	struct kset *ip_kset;

	ip_kset = &ip_die_entry->ip_kset;
	spin_lock(&ip_kset->list_lock);
	list_for_each_prev_safe(el, tmp, &ip_kset->list) {
		list_del_init(el);
		spin_unlock(&ip_kset->list_lock);
		amdgpu_discovery_sysfs_ip_hw_free(to_ip_hw_id(list_to_kobj(el)));
		spin_lock(&ip_kset->list_lock);
	}
	spin_unlock(&ip_kset->list_lock);
	kobject_put(&ip_die_entry->ip_kset.kobj);
}

static void amdgpu_discovery_sysfs_fini(struct amdgpu_device *adev)
{
	struct list_head *el, *tmp;
	struct kset *die_kset;

	die_kset = &adev->ip_top->die_kset;
	spin_lock(&die_kset->list_lock);
	list_for_each_prev_safe(el, tmp, &die_kset->list) {
		list_del_init(el);
		spin_unlock(&die_kset->list_lock);
		amdgpu_discovery_sysfs_die_free(to_ip_die_entry(list_to_kobj(el)));
		spin_lock(&die_kset->list_lock);
	}
	spin_unlock(&die_kset->list_lock);
	kobject_put(&adev->ip_top->die_kset.kobj);
	kobject_put(&adev->ip_top->kobj);
}

/* ================================================== */

static int amdgpu_discovery_reg_base_init(struct amdgpu_device *adev)
{
	uint8_t num_base_address, subrev, variant;
	struct binary_header *bhdr;
	struct ip_discovery_header *ihdr;
	struct die_header *dhdr;
	struct ip_v4 *ip;
	uint16_t die_offset;
	uint16_t ip_offset;
	uint16_t num_dies;
	uint16_t num_ips;
	int hw_ip;
	int i, j, k;
	int r;

	r = amdgpu_discovery_init(adev);
	if (r) {
		DRM_ERROR("amdgpu_discovery_init failed\n");
		return r;
	}

	adev->gfx.xcc_mask = 0;
	adev->sdma.sdma_mask = 0;
	adev->vcn.inst_mask = 0;
	adev->jpeg.inst_mask = 0;
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
			ip = (struct ip_v4 *)(adev->mman.discovery_bin + ip_offset);

			if (amdgpu_discovery_validate_ip(ip))
				goto next_ip;

			num_base_address = ip->num_base_address;

			DRM_DEBUG("%s(%d) #%d v%d.%d.%d:\n",
				  hw_id_names[le16_to_cpu(ip->hw_id)],
				  le16_to_cpu(ip->hw_id),
				  ip->instance_number,
				  ip->major, ip->minor,
				  ip->revision);

			if (le16_to_cpu(ip->hw_id) == VCN_HWID) {
				/* Bit [5:0]: original revision value
				 * Bit [7:6]: en/decode capability:
				 *     0b00 : VCN function normally
				 *     0b10 : encode is disabled
				 *     0b01 : decode is disabled
				 */
				if (adev->vcn.num_vcn_inst <
				    AMDGPU_MAX_VCN_INSTANCES) {
					adev->vcn.vcn_config[adev->vcn.num_vcn_inst] =
						ip->revision & 0xc0;
					adev->vcn.num_vcn_inst++;
					adev->vcn.inst_mask |=
						(1U << ip->instance_number);
					adev->jpeg.inst_mask |=
						(1U << ip->instance_number);
				} else {
					dev_err(adev->dev, "Too many VCN instances: %d vs %d\n",
						adev->vcn.num_vcn_inst + 1,
						AMDGPU_MAX_VCN_INSTANCES);
				}
				ip->revision &= ~0xc0;
			}
			if (le16_to_cpu(ip->hw_id) == SDMA0_HWID ||
			    le16_to_cpu(ip->hw_id) == SDMA1_HWID ||
			    le16_to_cpu(ip->hw_id) == SDMA2_HWID ||
			    le16_to_cpu(ip->hw_id) == SDMA3_HWID) {
				if (adev->sdma.num_instances <
				    AMDGPU_MAX_SDMA_INSTANCES) {
					adev->sdma.num_instances++;
					adev->sdma.sdma_mask |=
						(1U << ip->instance_number);
				} else {
					dev_err(adev->dev, "Too many SDMA instances: %d vs %d\n",
						adev->sdma.num_instances + 1,
						AMDGPU_MAX_SDMA_INSTANCES);
				}
			}

			if (le16_to_cpu(ip->hw_id) == VPE_HWID) {
				if (adev->vpe.num_instances < AMDGPU_MAX_VPE_INSTANCES)
					adev->vpe.num_instances++;
				else
					dev_err(adev->dev, "Too many VPE instances: %d vs %d\n",
						adev->vpe.num_instances + 1,
						AMDGPU_MAX_VPE_INSTANCES);
			}

			if (le16_to_cpu(ip->hw_id) == UMC_HWID) {
				adev->gmc.num_umc++;
				adev->umc.node_inst_num++;
			}

			if (le16_to_cpu(ip->hw_id) == GC_HWID)
				adev->gfx.xcc_mask |=
					(1U << ip->instance_number);

			for (k = 0; k < num_base_address; k++) {
				/*
				 * convert the endianness of base addresses in place,
				 * so that we don't need to convert them when accessing adev->reg_offset.
				 */
				if (ihdr->base_addr_64_bit)
					/* Truncate the 64bit base address from ip discovery
					 * and only store lower 32bit ip base in reg_offset[].
					 * Bits > 32 follows ASIC specific format, thus just
					 * discard them and handle it within specific ASIC.
					 * By this way reg_offset[] and related helpers can
					 * stay unchanged.
					 * The base address is in dwords, thus clear the
					 * highest 2 bits to store.
					 */
					ip->base_address[k] =
						lower_32_bits(le64_to_cpu(ip->base_address_64[k])) & 0x3FFFFFFF;
				else
					ip->base_address[k] = le32_to_cpu(ip->base_address[k]);
				DRM_DEBUG("\t0x%08x\n", ip->base_address[k]);
			}

			for (hw_ip = 0; hw_ip < MAX_HWIP; hw_ip++) {
				if (hw_id_map[hw_ip] == le16_to_cpu(ip->hw_id) &&
				    hw_id_map[hw_ip] != 0) {
					DRM_DEBUG("set register base offset for %s\n",
							hw_id_names[le16_to_cpu(ip->hw_id)]);
					adev->reg_offset[hw_ip][ip->instance_number] =
						ip->base_address;
					/* Instance support is somewhat inconsistent.
					 * SDMA is a good example.  Sienna cichlid has 4 total
					 * SDMA instances, each enumerated separately (HWIDs
					 * 42, 43, 68, 69).  Arcturus has 8 total SDMA instances,
					 * but they are enumerated as multiple instances of the
					 * same HWIDs (4x HWID 42, 4x HWID 43).  UMC is another
					 * example.  On most chips there are multiple instances
					 * with the same HWID.
					 */

					if (ihdr->version < 3) {
						subrev = 0;
						variant = 0;
					} else {
						subrev = ip->sub_revision;
						variant = ip->variant;
					}

					adev->ip_versions[hw_ip]
							 [ip->instance_number] =
						IP_VERSION_FULL(ip->major,
								ip->minor,
								ip->revision,
								variant,
								subrev);
				}
			}

next_ip:
			if (ihdr->base_addr_64_bit)
				ip_offset += struct_size(ip, base_address_64, ip->num_base_address);
			else
				ip_offset += struct_size(ip, base_address, ip->num_base_address);
		}
	}

	return 0;
}

static void amdgpu_discovery_harvest_ip(struct amdgpu_device *adev)
{
	int vcn_harvest_count = 0;
	int umc_harvest_count = 0;

	/*
	 * Harvest table does not fit Navi1x and legacy GPUs,
	 * so read harvest bit per IP data structure to set
	 * harvest configuration.
	 */
	if (amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(10, 2, 0) &&
	    amdgpu_ip_version(adev, GC_HWIP, 0) != IP_VERSION(9, 4, 3) &&
	    amdgpu_ip_version(adev, GC_HWIP, 0) != IP_VERSION(9, 4, 4)) {
		if ((adev->pdev->device == 0x731E &&
			(adev->pdev->revision == 0xC6 ||
			 adev->pdev->revision == 0xC7)) ||
			(adev->pdev->device == 0x7340 &&
			 adev->pdev->revision == 0xC9) ||
			(adev->pdev->device == 0x7360 &&
			 adev->pdev->revision == 0xC7))
			amdgpu_discovery_read_harvest_bit_per_ip(adev,
				&vcn_harvest_count);
	} else {
		amdgpu_discovery_read_from_harvest_table(adev,
							 &vcn_harvest_count,
							 &umc_harvest_count);
	}

	amdgpu_discovery_harvest_config_quirk(adev);

	if (vcn_harvest_count == adev->vcn.num_vcn_inst) {
		adev->harvest_ip_mask |= AMD_HARVEST_IP_VCN_MASK;
		adev->harvest_ip_mask |= AMD_HARVEST_IP_JPEG_MASK;
	}

	if (umc_harvest_count < adev->gmc.num_umc) {
		adev->gmc.num_umc -= umc_harvest_count;
	}
}

union gc_info {
	struct gc_info_v1_0 v1;
	struct gc_info_v1_1 v1_1;
	struct gc_info_v1_2 v1_2;
	struct gc_info_v1_3 v1_3;
	struct gc_info_v2_0 v2;
	struct gc_info_v2_1 v2_1;
};

static int amdgpu_discovery_get_gfx_info(struct amdgpu_device *adev)
{
	struct binary_header *bhdr;
	union gc_info *gc_info;
	u16 offset;

	if (!adev->mman.discovery_bin) {
		DRM_ERROR("ip discovery uninitialized\n");
		return -EINVAL;
	}

	bhdr = (struct binary_header *)adev->mman.discovery_bin;
	offset = le16_to_cpu(bhdr->table_list[GC].offset);

	if (!offset)
		return 0;

	gc_info = (union gc_info *)(adev->mman.discovery_bin + offset);

	switch (le16_to_cpu(gc_info->v1.header.version_major)) {
	case 1:
		adev->gfx.config.max_shader_engines = le32_to_cpu(gc_info->v1.gc_num_se);
		adev->gfx.config.max_cu_per_sh = 2 * (le32_to_cpu(gc_info->v1.gc_num_wgp0_per_sa) +
						      le32_to_cpu(gc_info->v1.gc_num_wgp1_per_sa));
		adev->gfx.config.max_sh_per_se = le32_to_cpu(gc_info->v1.gc_num_sa_per_se);
		adev->gfx.config.max_backends_per_se = le32_to_cpu(gc_info->v1.gc_num_rb_per_se);
		adev->gfx.config.max_texture_channel_caches = le32_to_cpu(gc_info->v1.gc_num_gl2c);
		adev->gfx.config.max_gprs = le32_to_cpu(gc_info->v1.gc_num_gprs);
		adev->gfx.config.max_gs_threads = le32_to_cpu(gc_info->v1.gc_num_max_gs_thds);
		adev->gfx.config.gs_vgt_table_depth = le32_to_cpu(gc_info->v1.gc_gs_table_depth);
		adev->gfx.config.gs_prim_buffer_depth = le32_to_cpu(gc_info->v1.gc_gsprim_buff_depth);
		adev->gfx.config.double_offchip_lds_buf = le32_to_cpu(gc_info->v1.gc_double_offchip_lds_buffer);
		adev->gfx.cu_info.wave_front_size = le32_to_cpu(gc_info->v1.gc_wave_size);
		adev->gfx.cu_info.max_waves_per_simd = le32_to_cpu(gc_info->v1.gc_max_waves_per_simd);
		adev->gfx.cu_info.max_scratch_slots_per_cu = le32_to_cpu(gc_info->v1.gc_max_scratch_slots_per_cu);
		adev->gfx.cu_info.lds_size = le32_to_cpu(gc_info->v1.gc_lds_size);
		adev->gfx.config.num_sc_per_sh = le32_to_cpu(gc_info->v1.gc_num_sc_per_se) /
			le32_to_cpu(gc_info->v1.gc_num_sa_per_se);
		adev->gfx.config.num_packer_per_sc = le32_to_cpu(gc_info->v1.gc_num_packer_per_sc);
		if (le16_to_cpu(gc_info->v1.header.version_minor) >= 1) {
			adev->gfx.config.gc_num_tcp_per_sa = le32_to_cpu(gc_info->v1_1.gc_num_tcp_per_sa);
			adev->gfx.config.gc_num_sdp_interface = le32_to_cpu(gc_info->v1_1.gc_num_sdp_interface);
			adev->gfx.config.gc_num_tcps = le32_to_cpu(gc_info->v1_1.gc_num_tcps);
		}
		if (le16_to_cpu(gc_info->v1.header.version_minor) >= 2) {
			adev->gfx.config.gc_num_tcp_per_wpg = le32_to_cpu(gc_info->v1_2.gc_num_tcp_per_wpg);
			adev->gfx.config.gc_tcp_l1_size = le32_to_cpu(gc_info->v1_2.gc_tcp_l1_size);
			adev->gfx.config.gc_num_sqc_per_wgp = le32_to_cpu(gc_info->v1_2.gc_num_sqc_per_wgp);
			adev->gfx.config.gc_l1_instruction_cache_size_per_sqc = le32_to_cpu(gc_info->v1_2.gc_l1_instruction_cache_size_per_sqc);
			adev->gfx.config.gc_l1_data_cache_size_per_sqc = le32_to_cpu(gc_info->v1_2.gc_l1_data_cache_size_per_sqc);
			adev->gfx.config.gc_gl1c_per_sa = le32_to_cpu(gc_info->v1_2.gc_gl1c_per_sa);
			adev->gfx.config.gc_gl1c_size_per_instance = le32_to_cpu(gc_info->v1_2.gc_gl1c_size_per_instance);
			adev->gfx.config.gc_gl2c_per_gpu = le32_to_cpu(gc_info->v1_2.gc_gl2c_per_gpu);
		}
		if (le16_to_cpu(gc_info->v1.header.version_minor) >= 3) {
			adev->gfx.config.gc_tcp_size_per_cu = le32_to_cpu(gc_info->v1_3.gc_tcp_size_per_cu);
			adev->gfx.config.gc_tcp_cache_line_size = le32_to_cpu(gc_info->v1_3.gc_tcp_cache_line_size);
			adev->gfx.config.gc_instruction_cache_size_per_sqc = le32_to_cpu(gc_info->v1_3.gc_instruction_cache_size_per_sqc);
			adev->gfx.config.gc_instruction_cache_line_size = le32_to_cpu(gc_info->v1_3.gc_instruction_cache_line_size);
			adev->gfx.config.gc_scalar_data_cache_size_per_sqc = le32_to_cpu(gc_info->v1_3.gc_scalar_data_cache_size_per_sqc);
			adev->gfx.config.gc_scalar_data_cache_line_size = le32_to_cpu(gc_info->v1_3.gc_scalar_data_cache_line_size);
			adev->gfx.config.gc_tcc_size = le32_to_cpu(gc_info->v1_3.gc_tcc_size);
			adev->gfx.config.gc_tcc_cache_line_size = le32_to_cpu(gc_info->v1_3.gc_tcc_cache_line_size);
		}
		break;
	case 2:
		adev->gfx.config.max_shader_engines = le32_to_cpu(gc_info->v2.gc_num_se);
		adev->gfx.config.max_cu_per_sh = le32_to_cpu(gc_info->v2.gc_num_cu_per_sh);
		adev->gfx.config.max_sh_per_se = le32_to_cpu(gc_info->v2.gc_num_sh_per_se);
		adev->gfx.config.max_backends_per_se = le32_to_cpu(gc_info->v2.gc_num_rb_per_se);
		adev->gfx.config.max_texture_channel_caches = le32_to_cpu(gc_info->v2.gc_num_tccs);
		adev->gfx.config.max_gprs = le32_to_cpu(gc_info->v2.gc_num_gprs);
		adev->gfx.config.max_gs_threads = le32_to_cpu(gc_info->v2.gc_num_max_gs_thds);
		adev->gfx.config.gs_vgt_table_depth = le32_to_cpu(gc_info->v2.gc_gs_table_depth);
		adev->gfx.config.gs_prim_buffer_depth = le32_to_cpu(gc_info->v2.gc_gsprim_buff_depth);
		adev->gfx.config.double_offchip_lds_buf = le32_to_cpu(gc_info->v2.gc_double_offchip_lds_buffer);
		adev->gfx.cu_info.wave_front_size = le32_to_cpu(gc_info->v2.gc_wave_size);
		adev->gfx.cu_info.max_waves_per_simd = le32_to_cpu(gc_info->v2.gc_max_waves_per_simd);
		adev->gfx.cu_info.max_scratch_slots_per_cu = le32_to_cpu(gc_info->v2.gc_max_scratch_slots_per_cu);
		adev->gfx.cu_info.lds_size = le32_to_cpu(gc_info->v2.gc_lds_size);
		adev->gfx.config.num_sc_per_sh = le32_to_cpu(gc_info->v2.gc_num_sc_per_se) /
			le32_to_cpu(gc_info->v2.gc_num_sh_per_se);
		adev->gfx.config.num_packer_per_sc = le32_to_cpu(gc_info->v2.gc_num_packer_per_sc);
		if (le16_to_cpu(gc_info->v2.header.version_minor) == 1) {
			adev->gfx.config.gc_num_tcp_per_sa = le32_to_cpu(gc_info->v2_1.gc_num_tcp_per_sh);
			adev->gfx.config.gc_tcp_size_per_cu = le32_to_cpu(gc_info->v2_1.gc_tcp_size_per_cu);
			adev->gfx.config.gc_num_sdp_interface = le32_to_cpu(gc_info->v2_1.gc_num_sdp_interface); /* per XCD */
			adev->gfx.config.gc_num_cu_per_sqc = le32_to_cpu(gc_info->v2_1.gc_num_cu_per_sqc);
			adev->gfx.config.gc_l1_instruction_cache_size_per_sqc = le32_to_cpu(gc_info->v2_1.gc_instruction_cache_size_per_sqc);
			adev->gfx.config.gc_l1_data_cache_size_per_sqc = le32_to_cpu(gc_info->v2_1.gc_scalar_data_cache_size_per_sqc);
			adev->gfx.config.gc_tcc_size = le32_to_cpu(gc_info->v2_1.gc_tcc_size); /* per XCD */
		}
		break;
	default:
		dev_err(adev->dev,
			"Unhandled GC info table %d.%d\n",
			le16_to_cpu(gc_info->v1.header.version_major),
			le16_to_cpu(gc_info->v1.header.version_minor));
		return -EINVAL;
	}
	return 0;
}

union mall_info {
	struct mall_info_v1_0 v1;
	struct mall_info_v2_0 v2;
};

static int amdgpu_discovery_get_mall_info(struct amdgpu_device *adev)
{
	struct binary_header *bhdr;
	union mall_info *mall_info;
	u32 u, mall_size_per_umc, m_s_present, half_use;
	u64 mall_size;
	u16 offset;

	if (!adev->mman.discovery_bin) {
		DRM_ERROR("ip discovery uninitialized\n");
		return -EINVAL;
	}

	bhdr = (struct binary_header *)adev->mman.discovery_bin;
	offset = le16_to_cpu(bhdr->table_list[MALL_INFO].offset);

	if (!offset)
		return 0;

	mall_info = (union mall_info *)(adev->mman.discovery_bin + offset);

	switch (le16_to_cpu(mall_info->v1.header.version_major)) {
	case 1:
		mall_size = 0;
		mall_size_per_umc = le32_to_cpu(mall_info->v1.mall_size_per_m);
		m_s_present = le32_to_cpu(mall_info->v1.m_s_present);
		half_use = le32_to_cpu(mall_info->v1.m_half_use);
		for (u = 0; u < adev->gmc.num_umc; u++) {
			if (m_s_present & (1 << u))
				mall_size += mall_size_per_umc * 2;
			else if (half_use & (1 << u))
				mall_size += mall_size_per_umc / 2;
			else
				mall_size += mall_size_per_umc;
		}
		adev->gmc.mall_size = mall_size;
		adev->gmc.m_half_use = half_use;
		break;
	case 2:
		mall_size_per_umc = le32_to_cpu(mall_info->v2.mall_size_per_umc);
		adev->gmc.mall_size = (uint64_t)mall_size_per_umc * adev->gmc.num_umc;
		break;
	default:
		dev_err(adev->dev,
			"Unhandled MALL info table %d.%d\n",
			le16_to_cpu(mall_info->v1.header.version_major),
			le16_to_cpu(mall_info->v1.header.version_minor));
		return -EINVAL;
	}
	return 0;
}

union vcn_info {
	struct vcn_info_v1_0 v1;
};

static int amdgpu_discovery_get_vcn_info(struct amdgpu_device *adev)
{
	struct binary_header *bhdr;
	union vcn_info *vcn_info;
	u16 offset;
	int v;

	if (!adev->mman.discovery_bin) {
		DRM_ERROR("ip discovery uninitialized\n");
		return -EINVAL;
	}

	/* num_vcn_inst is currently limited to AMDGPU_MAX_VCN_INSTANCES
	 * which is smaller than VCN_INFO_TABLE_MAX_NUM_INSTANCES
	 * but that may change in the future with new GPUs so keep this
	 * check for defensive purposes.
	 */
	if (adev->vcn.num_vcn_inst > VCN_INFO_TABLE_MAX_NUM_INSTANCES) {
		dev_err(adev->dev, "invalid vcn instances\n");
		return -EINVAL;
	}

	bhdr = (struct binary_header *)adev->mman.discovery_bin;
	offset = le16_to_cpu(bhdr->table_list[VCN_INFO].offset);

	if (!offset)
		return 0;

	vcn_info = (union vcn_info *)(adev->mman.discovery_bin + offset);

	switch (le16_to_cpu(vcn_info->v1.header.version_major)) {
	case 1:
		/* num_vcn_inst is currently limited to AMDGPU_MAX_VCN_INSTANCES
		 * so this won't overflow.
		 */
		for (v = 0; v < adev->vcn.num_vcn_inst; v++) {
			adev->vcn.vcn_codec_disable_mask[v] =
				le32_to_cpu(vcn_info->v1.instance_info[v].fuse_data.all_bits);
		}
		break;
	default:
		dev_err(adev->dev,
			"Unhandled VCN info table %d.%d\n",
			le16_to_cpu(vcn_info->v1.header.version_major),
			le16_to_cpu(vcn_info->v1.header.version_minor));
		return -EINVAL;
	}
	return 0;
}

union nps_info {
	struct nps_info_v1_0 v1;
};

int amdgpu_discovery_get_nps_info(struct amdgpu_device *adev,
				  uint32_t *nps_type,
				  struct amdgpu_gmc_memrange **ranges,
				  int *range_cnt)
{
	struct amdgpu_gmc_memrange *mem_ranges;
	struct binary_header *bhdr;
	union nps_info *nps_info;
	u16 offset;
	int i;

	if (!nps_type || !range_cnt || !ranges)
		return -EINVAL;

	if (!adev->mman.discovery_bin) {
		dev_err(adev->dev,
			"fetch mem range failed, ip discovery uninitialized\n");
		return -EINVAL;
	}

	bhdr = (struct binary_header *)adev->mman.discovery_bin;
	offset = le16_to_cpu(bhdr->table_list[NPS_INFO].offset);

	if (!offset)
		return -ENOENT;

	/* If verification fails, return as if NPS table doesn't exist */
	if (amdgpu_discovery_verify_npsinfo(adev, bhdr))
		return -ENOENT;

	nps_info = (union nps_info *)(adev->mman.discovery_bin + offset);

	switch (le16_to_cpu(nps_info->v1.header.version_major)) {
	case 1:
		*nps_type = nps_info->v1.nps_type;
		*range_cnt = nps_info->v1.count;
		mem_ranges = kvzalloc(
			*range_cnt * sizeof(struct amdgpu_gmc_memrange),
			GFP_KERNEL);
		for (i = 0; i < *range_cnt; i++) {
			mem_ranges[i].base_address =
				nps_info->v1.instance_info[i].base_address;
			mem_ranges[i].limit_address =
				nps_info->v1.instance_info[i].limit_address;
			mem_ranges[i].nid_mask = -1;
			mem_ranges[i].flags = 0;
		}
		*ranges = mem_ranges;
		break;
	default:
		dev_err(adev->dev, "Unhandled NPS info table %d.%d\n",
			le16_to_cpu(nps_info->v1.header.version_major),
			le16_to_cpu(nps_info->v1.header.version_minor));
		return -EINVAL;
	}

	return 0;
}

static int amdgpu_discovery_set_common_ip_blocks(struct amdgpu_device *adev)
{
	/* what IP to use for this? */
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 0, 1):
	case IP_VERSION(9, 1, 0):
	case IP_VERSION(9, 2, 1):
	case IP_VERSION(9, 2, 2):
	case IP_VERSION(9, 3, 0):
	case IP_VERSION(9, 4, 0):
	case IP_VERSION(9, 4, 1):
	case IP_VERSION(9, 4, 2):
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		amdgpu_device_ip_block_add(adev, &vega10_common_ip_block);
		break;
	case IP_VERSION(10, 1, 10):
	case IP_VERSION(10, 1, 1):
	case IP_VERSION(10, 1, 2):
	case IP_VERSION(10, 1, 3):
	case IP_VERSION(10, 1, 4):
	case IP_VERSION(10, 3, 0):
	case IP_VERSION(10, 3, 1):
	case IP_VERSION(10, 3, 2):
	case IP_VERSION(10, 3, 3):
	case IP_VERSION(10, 3, 4):
	case IP_VERSION(10, 3, 5):
	case IP_VERSION(10, 3, 6):
	case IP_VERSION(10, 3, 7):
		amdgpu_device_ip_block_add(adev, &nv_common_ip_block);
		break;
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 1):
	case IP_VERSION(11, 0, 2):
	case IP_VERSION(11, 0, 3):
	case IP_VERSION(11, 0, 4):
	case IP_VERSION(11, 5, 0):
	case IP_VERSION(11, 5, 1):
	case IP_VERSION(11, 5, 2):
		amdgpu_device_ip_block_add(adev, &soc21_common_ip_block);
		break;
	case IP_VERSION(12, 0, 0):
	case IP_VERSION(12, 0, 1):
		amdgpu_device_ip_block_add(adev, &soc24_common_ip_block);
		break;
	default:
		dev_err(adev->dev,
			"Failed to add common ip block(GC_HWIP:0x%x)\n",
			amdgpu_ip_version(adev, GC_HWIP, 0));
		return -EINVAL;
	}
	return 0;
}

static int amdgpu_discovery_set_gmc_ip_blocks(struct amdgpu_device *adev)
{
	/* use GC or MMHUB IP version */
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 0, 1):
	case IP_VERSION(9, 1, 0):
	case IP_VERSION(9, 2, 1):
	case IP_VERSION(9, 2, 2):
	case IP_VERSION(9, 3, 0):
	case IP_VERSION(9, 4, 0):
	case IP_VERSION(9, 4, 1):
	case IP_VERSION(9, 4, 2):
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		amdgpu_device_ip_block_add(adev, &gmc_v9_0_ip_block);
		break;
	case IP_VERSION(10, 1, 10):
	case IP_VERSION(10, 1, 1):
	case IP_VERSION(10, 1, 2):
	case IP_VERSION(10, 1, 3):
	case IP_VERSION(10, 1, 4):
	case IP_VERSION(10, 3, 0):
	case IP_VERSION(10, 3, 1):
	case IP_VERSION(10, 3, 2):
	case IP_VERSION(10, 3, 3):
	case IP_VERSION(10, 3, 4):
	case IP_VERSION(10, 3, 5):
	case IP_VERSION(10, 3, 6):
	case IP_VERSION(10, 3, 7):
		amdgpu_device_ip_block_add(adev, &gmc_v10_0_ip_block);
		break;
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 1):
	case IP_VERSION(11, 0, 2):
	case IP_VERSION(11, 0, 3):
	case IP_VERSION(11, 0, 4):
	case IP_VERSION(11, 5, 0):
	case IP_VERSION(11, 5, 1):
	case IP_VERSION(11, 5, 2):
		amdgpu_device_ip_block_add(adev, &gmc_v11_0_ip_block);
		break;
	case IP_VERSION(12, 0, 0):
	case IP_VERSION(12, 0, 1):
		amdgpu_device_ip_block_add(adev, &gmc_v12_0_ip_block);
		break;
	default:
		dev_err(adev->dev, "Failed to add gmc ip block(GC_HWIP:0x%x)\n",
			amdgpu_ip_version(adev, GC_HWIP, 0));
		return -EINVAL;
	}
	return 0;
}

static int amdgpu_discovery_set_ih_ip_blocks(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, OSSSYS_HWIP, 0)) {
	case IP_VERSION(4, 0, 0):
	case IP_VERSION(4, 0, 1):
	case IP_VERSION(4, 1, 0):
	case IP_VERSION(4, 1, 1):
	case IP_VERSION(4, 3, 0):
		amdgpu_device_ip_block_add(adev, &vega10_ih_ip_block);
		break;
	case IP_VERSION(4, 2, 0):
	case IP_VERSION(4, 2, 1):
	case IP_VERSION(4, 4, 0):
	case IP_VERSION(4, 4, 2):
	case IP_VERSION(4, 4, 5):
		amdgpu_device_ip_block_add(adev, &vega20_ih_ip_block);
		break;
	case IP_VERSION(5, 0, 0):
	case IP_VERSION(5, 0, 1):
	case IP_VERSION(5, 0, 2):
	case IP_VERSION(5, 0, 3):
	case IP_VERSION(5, 2, 0):
	case IP_VERSION(5, 2, 1):
		amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		break;
	case IP_VERSION(6, 0, 0):
	case IP_VERSION(6, 0, 1):
	case IP_VERSION(6, 0, 2):
		amdgpu_device_ip_block_add(adev, &ih_v6_0_ip_block);
		break;
	case IP_VERSION(6, 1, 0):
		amdgpu_device_ip_block_add(adev, &ih_v6_1_ip_block);
		break;
	case IP_VERSION(7, 0, 0):
		amdgpu_device_ip_block_add(adev, &ih_v7_0_ip_block);
		break;
	default:
		dev_err(adev->dev,
			"Failed to add ih ip block(OSSSYS_HWIP:0x%x)\n",
			amdgpu_ip_version(adev, OSSSYS_HWIP, 0));
		return -EINVAL;
	}
	return 0;
}

static int amdgpu_discovery_set_psp_ip_blocks(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, MP0_HWIP, 0)) {
	case IP_VERSION(9, 0, 0):
		amdgpu_device_ip_block_add(adev, &psp_v3_1_ip_block);
		break;
	case IP_VERSION(10, 0, 0):
	case IP_VERSION(10, 0, 1):
		amdgpu_device_ip_block_add(adev, &psp_v10_0_ip_block);
		break;
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 2):
	case IP_VERSION(11, 0, 4):
	case IP_VERSION(11, 0, 5):
	case IP_VERSION(11, 0, 9):
	case IP_VERSION(11, 0, 7):
	case IP_VERSION(11, 0, 11):
	case IP_VERSION(11, 0, 12):
	case IP_VERSION(11, 0, 13):
	case IP_VERSION(11, 5, 0):
		amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
		break;
	case IP_VERSION(11, 0, 8):
		amdgpu_device_ip_block_add(adev, &psp_v11_0_8_ip_block);
		break;
	case IP_VERSION(11, 0, 3):
	case IP_VERSION(12, 0, 1):
		amdgpu_device_ip_block_add(adev, &psp_v12_0_ip_block);
		break;
	case IP_VERSION(13, 0, 0):
	case IP_VERSION(13, 0, 1):
	case IP_VERSION(13, 0, 2):
	case IP_VERSION(13, 0, 3):
	case IP_VERSION(13, 0, 5):
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 7):
	case IP_VERSION(13, 0, 8):
	case IP_VERSION(13, 0, 10):
	case IP_VERSION(13, 0, 11):
	case IP_VERSION(13, 0, 14):
	case IP_VERSION(14, 0, 0):
	case IP_VERSION(14, 0, 1):
	case IP_VERSION(14, 0, 4):
		amdgpu_device_ip_block_add(adev, &psp_v13_0_ip_block);
		break;
	case IP_VERSION(13, 0, 4):
		amdgpu_device_ip_block_add(adev, &psp_v13_0_4_ip_block);
		break;
	case IP_VERSION(14, 0, 2):
	case IP_VERSION(14, 0, 3):
		amdgpu_device_ip_block_add(adev, &psp_v14_0_ip_block);
		break;
	default:
		dev_err(adev->dev,
			"Failed to add psp ip block(MP0_HWIP:0x%x)\n",
			amdgpu_ip_version(adev, MP0_HWIP, 0));
		return -EINVAL;
	}
	return 0;
}

static int amdgpu_discovery_set_smu_ip_blocks(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, MP1_HWIP, 0)) {
	case IP_VERSION(9, 0, 0):
	case IP_VERSION(10, 0, 0):
	case IP_VERSION(10, 0, 1):
	case IP_VERSION(11, 0, 2):
		if (adev->asic_type == CHIP_ARCTURUS)
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		else
			amdgpu_device_ip_block_add(adev, &pp_smu_ip_block);
		break;
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 5):
	case IP_VERSION(11, 0, 9):
	case IP_VERSION(11, 0, 7):
	case IP_VERSION(11, 0, 8):
	case IP_VERSION(11, 0, 11):
	case IP_VERSION(11, 0, 12):
	case IP_VERSION(11, 0, 13):
	case IP_VERSION(11, 5, 0):
		amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		break;
	case IP_VERSION(12, 0, 0):
	case IP_VERSION(12, 0, 1):
		amdgpu_device_ip_block_add(adev, &smu_v12_0_ip_block);
		break;
	case IP_VERSION(13, 0, 0):
	case IP_VERSION(13, 0, 1):
	case IP_VERSION(13, 0, 2):
	case IP_VERSION(13, 0, 3):
	case IP_VERSION(13, 0, 4):
	case IP_VERSION(13, 0, 5):
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 7):
	case IP_VERSION(13, 0, 8):
	case IP_VERSION(13, 0, 10):
	case IP_VERSION(13, 0, 11):
	case IP_VERSION(13, 0, 14):
		amdgpu_device_ip_block_add(adev, &smu_v13_0_ip_block);
		break;
	case IP_VERSION(14, 0, 0):
	case IP_VERSION(14, 0, 1):
	case IP_VERSION(14, 0, 2):
	case IP_VERSION(14, 0, 3):
	case IP_VERSION(14, 0, 4):
		amdgpu_device_ip_block_add(adev, &smu_v14_0_ip_block);
		break;
	default:
		dev_err(adev->dev,
			"Failed to add smu ip block(MP1_HWIP:0x%x)\n",
			amdgpu_ip_version(adev, MP1_HWIP, 0));
		return -EINVAL;
	}
	return 0;
}

#if defined(CONFIG_DRM_AMD_DC)
static void amdgpu_discovery_set_sriov_display(struct amdgpu_device *adev)
{
	amdgpu_device_set_sriov_virtual_display(adev);
	amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
}
#endif

static int amdgpu_discovery_set_display_ip_blocks(struct amdgpu_device *adev)
{
	if (adev->enable_virtual_display) {
		amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
		return 0;
	}

	if (!amdgpu_device_has_dc_support(adev))
		return 0;

#if defined(CONFIG_DRM_AMD_DC)
	if (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(1, 0, 0):
		case IP_VERSION(1, 0, 1):
		case IP_VERSION(2, 0, 2):
		case IP_VERSION(2, 0, 0):
		case IP_VERSION(2, 0, 3):
		case IP_VERSION(2, 1, 0):
		case IP_VERSION(3, 0, 0):
		case IP_VERSION(3, 0, 2):
		case IP_VERSION(3, 0, 3):
		case IP_VERSION(3, 0, 1):
		case IP_VERSION(3, 1, 2):
		case IP_VERSION(3, 1, 3):
		case IP_VERSION(3, 1, 4):
		case IP_VERSION(3, 1, 5):
		case IP_VERSION(3, 1, 6):
		case IP_VERSION(3, 2, 0):
		case IP_VERSION(3, 2, 1):
		case IP_VERSION(3, 5, 0):
		case IP_VERSION(3, 5, 1):
		case IP_VERSION(4, 1, 0):
			/* TODO: Fix IP version. DC code expects version 4.0.1 */
			if (adev->ip_versions[DCE_HWIP][0] == IP_VERSION(4, 1, 0))
				adev->ip_versions[DCE_HWIP][0] = IP_VERSION(4, 0, 1);

			if (amdgpu_sriov_vf(adev))
				amdgpu_discovery_set_sriov_display(adev);
			else
				amdgpu_device_ip_block_add(adev, &dm_ip_block);
			break;
		default:
			dev_err(adev->dev,
				"Failed to add dm ip block(DCE_HWIP:0x%x)\n",
				amdgpu_ip_version(adev, DCE_HWIP, 0));
			return -EINVAL;
		}
	} else if (amdgpu_ip_version(adev, DCI_HWIP, 0)) {
		switch (amdgpu_ip_version(adev, DCI_HWIP, 0)) {
		case IP_VERSION(12, 0, 0):
		case IP_VERSION(12, 0, 1):
		case IP_VERSION(12, 1, 0):
			if (amdgpu_sriov_vf(adev))
				amdgpu_discovery_set_sriov_display(adev);
			else
				amdgpu_device_ip_block_add(adev, &dm_ip_block);
			break;
		default:
			dev_err(adev->dev,
				"Failed to add dm ip block(DCI_HWIP:0x%x)\n",
				amdgpu_ip_version(adev, DCI_HWIP, 0));
			return -EINVAL;
		}
	}
#endif
	return 0;
}

static int amdgpu_discovery_set_gc_ip_blocks(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 0, 1):
	case IP_VERSION(9, 1, 0):
	case IP_VERSION(9, 2, 1):
	case IP_VERSION(9, 2, 2):
	case IP_VERSION(9, 3, 0):
	case IP_VERSION(9, 4, 0):
	case IP_VERSION(9, 4, 1):
	case IP_VERSION(9, 4, 2):
		amdgpu_device_ip_block_add(adev, &gfx_v9_0_ip_block);
		break;
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		amdgpu_device_ip_block_add(adev, &gfx_v9_4_3_ip_block);
		break;
	case IP_VERSION(10, 1, 10):
	case IP_VERSION(10, 1, 2):
	case IP_VERSION(10, 1, 1):
	case IP_VERSION(10, 1, 3):
	case IP_VERSION(10, 1, 4):
	case IP_VERSION(10, 3, 0):
	case IP_VERSION(10, 3, 2):
	case IP_VERSION(10, 3, 1):
	case IP_VERSION(10, 3, 4):
	case IP_VERSION(10, 3, 5):
	case IP_VERSION(10, 3, 6):
	case IP_VERSION(10, 3, 3):
	case IP_VERSION(10, 3, 7):
		amdgpu_device_ip_block_add(adev, &gfx_v10_0_ip_block);
		break;
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 1):
	case IP_VERSION(11, 0, 2):
	case IP_VERSION(11, 0, 3):
	case IP_VERSION(11, 0, 4):
	case IP_VERSION(11, 5, 0):
	case IP_VERSION(11, 5, 1):
	case IP_VERSION(11, 5, 2):
		amdgpu_device_ip_block_add(adev, &gfx_v11_0_ip_block);
		break;
	case IP_VERSION(12, 0, 0):
	case IP_VERSION(12, 0, 1):
		amdgpu_device_ip_block_add(adev, &gfx_v12_0_ip_block);
		break;
	default:
		dev_err(adev->dev, "Failed to add gfx ip block(GC_HWIP:0x%x)\n",
			amdgpu_ip_version(adev, GC_HWIP, 0));
		return -EINVAL;
	}
	return 0;
}

static int amdgpu_discovery_set_sdma_ip_blocks(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, SDMA0_HWIP, 0)) {
	case IP_VERSION(4, 0, 0):
	case IP_VERSION(4, 0, 1):
	case IP_VERSION(4, 1, 0):
	case IP_VERSION(4, 1, 1):
	case IP_VERSION(4, 1, 2):
	case IP_VERSION(4, 2, 0):
	case IP_VERSION(4, 2, 2):
	case IP_VERSION(4, 4, 0):
		amdgpu_device_ip_block_add(adev, &sdma_v4_0_ip_block);
		break;
	case IP_VERSION(4, 4, 2):
	case IP_VERSION(4, 4, 5):
		amdgpu_device_ip_block_add(adev, &sdma_v4_4_2_ip_block);
		break;
	case IP_VERSION(5, 0, 0):
	case IP_VERSION(5, 0, 1):
	case IP_VERSION(5, 0, 2):
	case IP_VERSION(5, 0, 5):
		amdgpu_device_ip_block_add(adev, &sdma_v5_0_ip_block);
		break;
	case IP_VERSION(5, 2, 0):
	case IP_VERSION(5, 2, 2):
	case IP_VERSION(5, 2, 4):
	case IP_VERSION(5, 2, 5):
	case IP_VERSION(5, 2, 6):
	case IP_VERSION(5, 2, 3):
	case IP_VERSION(5, 2, 1):
	case IP_VERSION(5, 2, 7):
		amdgpu_device_ip_block_add(adev, &sdma_v5_2_ip_block);
		break;
	case IP_VERSION(6, 0, 0):
	case IP_VERSION(6, 0, 1):
	case IP_VERSION(6, 0, 2):
	case IP_VERSION(6, 0, 3):
	case IP_VERSION(6, 1, 0):
	case IP_VERSION(6, 1, 1):
	case IP_VERSION(6, 1, 2):
		amdgpu_device_ip_block_add(adev, &sdma_v6_0_ip_block);
		break;
	case IP_VERSION(7, 0, 0):
	case IP_VERSION(7, 0, 1):
		amdgpu_device_ip_block_add(adev, &sdma_v7_0_ip_block);
		break;
	default:
		dev_err(adev->dev,
			"Failed to add sdma ip block(SDMA0_HWIP:0x%x)\n",
			amdgpu_ip_version(adev, SDMA0_HWIP, 0));
		return -EINVAL;
	}
	return 0;
}

static int amdgpu_discovery_set_mm_ip_blocks(struct amdgpu_device *adev)
{
	if (amdgpu_ip_version(adev, VCE_HWIP, 0)) {
		switch (amdgpu_ip_version(adev, UVD_HWIP, 0)) {
		case IP_VERSION(7, 0, 0):
		case IP_VERSION(7, 2, 0):
			/* UVD is not supported on vega20 SR-IOV */
			if (!(adev->asic_type == CHIP_VEGA20 && amdgpu_sriov_vf(adev)))
				amdgpu_device_ip_block_add(adev, &uvd_v7_0_ip_block);
			break;
		default:
			dev_err(adev->dev,
				"Failed to add uvd v7 ip block(UVD_HWIP:0x%x)\n",
				amdgpu_ip_version(adev, UVD_HWIP, 0));
			return -EINVAL;
		}
		switch (amdgpu_ip_version(adev, VCE_HWIP, 0)) {
		case IP_VERSION(4, 0, 0):
		case IP_VERSION(4, 1, 0):
			/* VCE is not supported on vega20 SR-IOV */
			if (!(adev->asic_type == CHIP_VEGA20 && amdgpu_sriov_vf(adev)))
				amdgpu_device_ip_block_add(adev, &vce_v4_0_ip_block);
			break;
		default:
			dev_err(adev->dev,
				"Failed to add VCE v4 ip block(VCE_HWIP:0x%x)\n",
				amdgpu_ip_version(adev, VCE_HWIP, 0));
			return -EINVAL;
		}
	} else {
		switch (amdgpu_ip_version(adev, UVD_HWIP, 0)) {
		case IP_VERSION(1, 0, 0):
		case IP_VERSION(1, 0, 1):
			amdgpu_device_ip_block_add(adev, &vcn_v1_0_ip_block);
			break;
		case IP_VERSION(2, 0, 0):
		case IP_VERSION(2, 0, 2):
		case IP_VERSION(2, 2, 0):
			amdgpu_device_ip_block_add(adev, &vcn_v2_0_ip_block);
			if (!amdgpu_sriov_vf(adev))
				amdgpu_device_ip_block_add(adev, &jpeg_v2_0_ip_block);
			break;
		case IP_VERSION(2, 0, 3):
			break;
		case IP_VERSION(2, 5, 0):
			amdgpu_device_ip_block_add(adev, &vcn_v2_5_ip_block);
			amdgpu_device_ip_block_add(adev, &jpeg_v2_5_ip_block);
			break;
		case IP_VERSION(2, 6, 0):
			amdgpu_device_ip_block_add(adev, &vcn_v2_6_ip_block);
			amdgpu_device_ip_block_add(adev, &jpeg_v2_6_ip_block);
			break;
		case IP_VERSION(3, 0, 0):
		case IP_VERSION(3, 0, 16):
		case IP_VERSION(3, 1, 1):
		case IP_VERSION(3, 1, 2):
		case IP_VERSION(3, 0, 2):
			amdgpu_device_ip_block_add(adev, &vcn_v3_0_ip_block);
			if (!amdgpu_sriov_vf(adev))
				amdgpu_device_ip_block_add(adev, &jpeg_v3_0_ip_block);
			break;
		case IP_VERSION(3, 0, 33):
			amdgpu_device_ip_block_add(adev, &vcn_v3_0_ip_block);
			break;
		case IP_VERSION(4, 0, 0):
		case IP_VERSION(4, 0, 2):
		case IP_VERSION(4, 0, 4):
			amdgpu_device_ip_block_add(adev, &vcn_v4_0_ip_block);
			amdgpu_device_ip_block_add(adev, &jpeg_v4_0_ip_block);
			break;
		case IP_VERSION(4, 0, 3):
			amdgpu_device_ip_block_add(adev, &vcn_v4_0_3_ip_block);
			amdgpu_device_ip_block_add(adev, &jpeg_v4_0_3_ip_block);
			break;
		case IP_VERSION(4, 0, 5):
		case IP_VERSION(4, 0, 6):
			amdgpu_device_ip_block_add(adev, &vcn_v4_0_5_ip_block);
			amdgpu_device_ip_block_add(adev, &jpeg_v4_0_5_ip_block);
			break;
		case IP_VERSION(5, 0, 0):
			amdgpu_device_ip_block_add(adev, &vcn_v5_0_0_ip_block);
			amdgpu_device_ip_block_add(adev, &jpeg_v5_0_0_ip_block);
			break;
		default:
			dev_err(adev->dev,
				"Failed to add vcn/jpeg ip block(UVD_HWIP:0x%x)\n",
				amdgpu_ip_version(adev, UVD_HWIP, 0));
			return -EINVAL;
		}
	}
	return 0;
}

static int amdgpu_discovery_set_mes_ip_blocks(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 1):
	case IP_VERSION(11, 0, 2):
	case IP_VERSION(11, 0, 3):
	case IP_VERSION(11, 0, 4):
	case IP_VERSION(11, 5, 0):
	case IP_VERSION(11, 5, 1):
	case IP_VERSION(11, 5, 2):
		amdgpu_device_ip_block_add(adev, &mes_v11_0_ip_block);
		adev->enable_mes = true;
		adev->enable_mes_kiq = true;
		break;
	case IP_VERSION(12, 0, 0):
	case IP_VERSION(12, 0, 1):
		amdgpu_device_ip_block_add(adev, &mes_v12_0_ip_block);
		adev->enable_mes = true;
		adev->enable_mes_kiq = true;
		if (amdgpu_uni_mes)
			adev->enable_uni_mes = true;
		break;
	default:
		break;
	}
	return 0;
}

static void amdgpu_discovery_init_soc_config(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		aqua_vanjaram_init_soc_config(adev);
		break;
	default:
		break;
	}
}

static int amdgpu_discovery_set_vpe_ip_blocks(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, VPE_HWIP, 0)) {
	case IP_VERSION(6, 1, 0):
	case IP_VERSION(6, 1, 1):
	case IP_VERSION(6, 1, 3):
		amdgpu_device_ip_block_add(adev, &vpe_v6_1_ip_block);
		break;
	default:
		break;
	}

	return 0;
}

static int amdgpu_discovery_set_umsch_mm_ip_blocks(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, VCN_HWIP, 0)) {
	case IP_VERSION(4, 0, 5):
	case IP_VERSION(4, 0, 6):
		if (amdgpu_umsch_mm & 0x1) {
			amdgpu_device_ip_block_add(adev, &umsch_mm_v4_0_ip_block);
			adev->enable_umsch_mm = true;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int amdgpu_discovery_set_isp_ip_blocks(struct amdgpu_device *adev)
{
#if defined(CONFIG_DRM_AMD_ISP)
	switch (amdgpu_ip_version(adev, ISP_HWIP, 0)) {
	case IP_VERSION(4, 1, 0):
		amdgpu_device_ip_block_add(adev, &isp_v4_1_0_ip_block);
		break;
	case IP_VERSION(4, 1, 1):
		amdgpu_device_ip_block_add(adev, &isp_v4_1_1_ip_block);
		break;
	default:
		break;
	}
#endif

	return 0;
}

int amdgpu_discovery_set_ip_blocks(struct amdgpu_device *adev)
{
	int r;

	switch (adev->asic_type) {
	case CHIP_VEGA10:
		vega10_reg_base_init(adev);
		adev->sdma.num_instances = 2;
		adev->gmc.num_umc = 4;
		adev->ip_versions[MMHUB_HWIP][0] = IP_VERSION(9, 0, 0);
		adev->ip_versions[ATHUB_HWIP][0] = IP_VERSION(9, 0, 0);
		adev->ip_versions[OSSSYS_HWIP][0] = IP_VERSION(4, 0, 0);
		adev->ip_versions[HDP_HWIP][0] = IP_VERSION(4, 0, 0);
		adev->ip_versions[SDMA0_HWIP][0] = IP_VERSION(4, 0, 0);
		adev->ip_versions[SDMA1_HWIP][0] = IP_VERSION(4, 0, 0);
		adev->ip_versions[DF_HWIP][0] = IP_VERSION(2, 1, 0);
		adev->ip_versions[NBIO_HWIP][0] = IP_VERSION(6, 1, 0);
		adev->ip_versions[UMC_HWIP][0] = IP_VERSION(6, 0, 0);
		adev->ip_versions[MP0_HWIP][0] = IP_VERSION(9, 0, 0);
		adev->ip_versions[MP1_HWIP][0] = IP_VERSION(9, 0, 0);
		adev->ip_versions[THM_HWIP][0] = IP_VERSION(9, 0, 0);
		adev->ip_versions[SMUIO_HWIP][0] = IP_VERSION(9, 0, 0);
		adev->ip_versions[GC_HWIP][0] = IP_VERSION(9, 0, 1);
		adev->ip_versions[UVD_HWIP][0] = IP_VERSION(7, 0, 0);
		adev->ip_versions[VCE_HWIP][0] = IP_VERSION(4, 0, 0);
		adev->ip_versions[DCI_HWIP][0] = IP_VERSION(12, 0, 0);
		break;
	case CHIP_VEGA12:
		vega10_reg_base_init(adev);
		adev->sdma.num_instances = 2;
		adev->gmc.num_umc = 4;
		adev->ip_versions[MMHUB_HWIP][0] = IP_VERSION(9, 3, 0);
		adev->ip_versions[ATHUB_HWIP][0] = IP_VERSION(9, 3, 0);
		adev->ip_versions[OSSSYS_HWIP][0] = IP_VERSION(4, 0, 1);
		adev->ip_versions[HDP_HWIP][0] = IP_VERSION(4, 0, 1);
		adev->ip_versions[SDMA0_HWIP][0] = IP_VERSION(4, 0, 1);
		adev->ip_versions[SDMA1_HWIP][0] = IP_VERSION(4, 0, 1);
		adev->ip_versions[DF_HWIP][0] = IP_VERSION(2, 5, 0);
		adev->ip_versions[NBIO_HWIP][0] = IP_VERSION(6, 2, 0);
		adev->ip_versions[UMC_HWIP][0] = IP_VERSION(6, 1, 0);
		adev->ip_versions[MP0_HWIP][0] = IP_VERSION(9, 0, 0);
		adev->ip_versions[MP1_HWIP][0] = IP_VERSION(9, 0, 0);
		adev->ip_versions[THM_HWIP][0] = IP_VERSION(9, 0, 0);
		adev->ip_versions[SMUIO_HWIP][0] = IP_VERSION(9, 0, 1);
		adev->ip_versions[GC_HWIP][0] = IP_VERSION(9, 2, 1);
		adev->ip_versions[UVD_HWIP][0] = IP_VERSION(7, 0, 0);
		adev->ip_versions[VCE_HWIP][0] = IP_VERSION(4, 0, 0);
		adev->ip_versions[DCI_HWIP][0] = IP_VERSION(12, 0, 1);
		break;
	case CHIP_RAVEN:
		vega10_reg_base_init(adev);
		adev->sdma.num_instances = 1;
		adev->vcn.num_vcn_inst = 1;
		adev->gmc.num_umc = 2;
		if (adev->apu_flags & AMD_APU_IS_RAVEN2) {
			adev->ip_versions[MMHUB_HWIP][0] = IP_VERSION(9, 2, 0);
			adev->ip_versions[ATHUB_HWIP][0] = IP_VERSION(9, 2, 0);
			adev->ip_versions[OSSSYS_HWIP][0] = IP_VERSION(4, 1, 1);
			adev->ip_versions[HDP_HWIP][0] = IP_VERSION(4, 1, 1);
			adev->ip_versions[SDMA0_HWIP][0] = IP_VERSION(4, 1, 1);
			adev->ip_versions[DF_HWIP][0] = IP_VERSION(2, 1, 1);
			adev->ip_versions[NBIO_HWIP][0] = IP_VERSION(7, 0, 1);
			adev->ip_versions[UMC_HWIP][0] = IP_VERSION(7, 5, 0);
			adev->ip_versions[MP0_HWIP][0] = IP_VERSION(10, 0, 1);
			adev->ip_versions[MP1_HWIP][0] = IP_VERSION(10, 0, 1);
			adev->ip_versions[THM_HWIP][0] = IP_VERSION(10, 1, 0);
			adev->ip_versions[SMUIO_HWIP][0] = IP_VERSION(10, 0, 1);
			adev->ip_versions[GC_HWIP][0] = IP_VERSION(9, 2, 2);
			adev->ip_versions[UVD_HWIP][0] = IP_VERSION(1, 0, 1);
			adev->ip_versions[DCE_HWIP][0] = IP_VERSION(1, 0, 1);
		} else {
			adev->ip_versions[MMHUB_HWIP][0] = IP_VERSION(9, 1, 0);
			adev->ip_versions[ATHUB_HWIP][0] = IP_VERSION(9, 1, 0);
			adev->ip_versions[OSSSYS_HWIP][0] = IP_VERSION(4, 1, 0);
			adev->ip_versions[HDP_HWIP][0] = IP_VERSION(4, 1, 0);
			adev->ip_versions[SDMA0_HWIP][0] = IP_VERSION(4, 1, 0);
			adev->ip_versions[DF_HWIP][0] = IP_VERSION(2, 1, 0);
			adev->ip_versions[NBIO_HWIP][0] = IP_VERSION(7, 0, 0);
			adev->ip_versions[UMC_HWIP][0] = IP_VERSION(7, 0, 0);
			adev->ip_versions[MP0_HWIP][0] = IP_VERSION(10, 0, 0);
			adev->ip_versions[MP1_HWIP][0] = IP_VERSION(10, 0, 0);
			adev->ip_versions[THM_HWIP][0] = IP_VERSION(10, 0, 0);
			adev->ip_versions[SMUIO_HWIP][0] = IP_VERSION(10, 0, 0);
			adev->ip_versions[GC_HWIP][0] = IP_VERSION(9, 1, 0);
			adev->ip_versions[UVD_HWIP][0] = IP_VERSION(1, 0, 0);
			adev->ip_versions[DCE_HWIP][0] = IP_VERSION(1, 0, 0);
		}
		break;
	case CHIP_VEGA20:
		vega20_reg_base_init(adev);
		adev->sdma.num_instances = 2;
		adev->gmc.num_umc = 8;
		adev->ip_versions[MMHUB_HWIP][0] = IP_VERSION(9, 4, 0);
		adev->ip_versions[ATHUB_HWIP][0] = IP_VERSION(9, 4, 0);
		adev->ip_versions[OSSSYS_HWIP][0] = IP_VERSION(4, 2, 0);
		adev->ip_versions[HDP_HWIP][0] = IP_VERSION(4, 2, 0);
		adev->ip_versions[SDMA0_HWIP][0] = IP_VERSION(4, 2, 0);
		adev->ip_versions[SDMA1_HWIP][0] = IP_VERSION(4, 2, 0);
		adev->ip_versions[DF_HWIP][0] = IP_VERSION(3, 6, 0);
		adev->ip_versions[NBIO_HWIP][0] = IP_VERSION(7, 4, 0);
		adev->ip_versions[UMC_HWIP][0] = IP_VERSION(6, 1, 1);
		adev->ip_versions[MP0_HWIP][0] = IP_VERSION(11, 0, 2);
		adev->ip_versions[MP1_HWIP][0] = IP_VERSION(11, 0, 2);
		adev->ip_versions[THM_HWIP][0] = IP_VERSION(11, 0, 2);
		adev->ip_versions[SMUIO_HWIP][0] = IP_VERSION(11, 0, 2);
		adev->ip_versions[GC_HWIP][0] = IP_VERSION(9, 4, 0);
		adev->ip_versions[UVD_HWIP][0] = IP_VERSION(7, 2, 0);
		adev->ip_versions[UVD_HWIP][1] = IP_VERSION(7, 2, 0);
		adev->ip_versions[VCE_HWIP][0] = IP_VERSION(4, 1, 0);
		adev->ip_versions[DCI_HWIP][0] = IP_VERSION(12, 1, 0);
		break;
	case CHIP_ARCTURUS:
		arct_reg_base_init(adev);
		adev->sdma.num_instances = 8;
		adev->vcn.num_vcn_inst = 2;
		adev->gmc.num_umc = 8;
		adev->ip_versions[MMHUB_HWIP][0] = IP_VERSION(9, 4, 1);
		adev->ip_versions[ATHUB_HWIP][0] = IP_VERSION(9, 4, 1);
		adev->ip_versions[OSSSYS_HWIP][0] = IP_VERSION(4, 2, 1);
		adev->ip_versions[HDP_HWIP][0] = IP_VERSION(4, 2, 1);
		adev->ip_versions[SDMA0_HWIP][0] = IP_VERSION(4, 2, 2);
		adev->ip_versions[SDMA1_HWIP][0] = IP_VERSION(4, 2, 2);
		adev->ip_versions[SDMA1_HWIP][1] = IP_VERSION(4, 2, 2);
		adev->ip_versions[SDMA1_HWIP][2] = IP_VERSION(4, 2, 2);
		adev->ip_versions[SDMA1_HWIP][3] = IP_VERSION(4, 2, 2);
		adev->ip_versions[SDMA1_HWIP][4] = IP_VERSION(4, 2, 2);
		adev->ip_versions[SDMA1_HWIP][5] = IP_VERSION(4, 2, 2);
		adev->ip_versions[SDMA1_HWIP][6] = IP_VERSION(4, 2, 2);
		adev->ip_versions[DF_HWIP][0] = IP_VERSION(3, 6, 1);
		adev->ip_versions[NBIO_HWIP][0] = IP_VERSION(7, 4, 1);
		adev->ip_versions[UMC_HWIP][0] = IP_VERSION(6, 1, 2);
		adev->ip_versions[MP0_HWIP][0] = IP_VERSION(11, 0, 4);
		adev->ip_versions[MP1_HWIP][0] = IP_VERSION(11, 0, 2);
		adev->ip_versions[THM_HWIP][0] = IP_VERSION(11, 0, 3);
		adev->ip_versions[SMUIO_HWIP][0] = IP_VERSION(11, 0, 3);
		adev->ip_versions[GC_HWIP][0] = IP_VERSION(9, 4, 1);
		adev->ip_versions[UVD_HWIP][0] = IP_VERSION(2, 5, 0);
		adev->ip_versions[UVD_HWIP][1] = IP_VERSION(2, 5, 0);
		break;
	case CHIP_ALDEBARAN:
		aldebaran_reg_base_init(adev);
		adev->sdma.num_instances = 5;
		adev->vcn.num_vcn_inst = 2;
		adev->gmc.num_umc = 4;
		adev->ip_versions[MMHUB_HWIP][0] = IP_VERSION(9, 4, 2);
		adev->ip_versions[ATHUB_HWIP][0] = IP_VERSION(9, 4, 2);
		adev->ip_versions[OSSSYS_HWIP][0] = IP_VERSION(4, 4, 0);
		adev->ip_versions[HDP_HWIP][0] = IP_VERSION(4, 4, 0);
		adev->ip_versions[SDMA0_HWIP][0] = IP_VERSION(4, 4, 0);
		adev->ip_versions[SDMA0_HWIP][1] = IP_VERSION(4, 4, 0);
		adev->ip_versions[SDMA0_HWIP][2] = IP_VERSION(4, 4, 0);
		adev->ip_versions[SDMA0_HWIP][3] = IP_VERSION(4, 4, 0);
		adev->ip_versions[SDMA0_HWIP][4] = IP_VERSION(4, 4, 0);
		adev->ip_versions[DF_HWIP][0] = IP_VERSION(3, 6, 2);
		adev->ip_versions[NBIO_HWIP][0] = IP_VERSION(7, 4, 4);
		adev->ip_versions[UMC_HWIP][0] = IP_VERSION(6, 7, 0);
		adev->ip_versions[MP0_HWIP][0] = IP_VERSION(13, 0, 2);
		adev->ip_versions[MP1_HWIP][0] = IP_VERSION(13, 0, 2);
		adev->ip_versions[THM_HWIP][0] = IP_VERSION(13, 0, 2);
		adev->ip_versions[SMUIO_HWIP][0] = IP_VERSION(13, 0, 2);
		adev->ip_versions[GC_HWIP][0] = IP_VERSION(9, 4, 2);
		adev->ip_versions[UVD_HWIP][0] = IP_VERSION(2, 6, 0);
		adev->ip_versions[UVD_HWIP][1] = IP_VERSION(2, 6, 0);
		adev->ip_versions[XGMI_HWIP][0] = IP_VERSION(6, 1, 0);
		break;
	default:
		r = amdgpu_discovery_reg_base_init(adev);
		if (r)
			return -EINVAL;

		amdgpu_discovery_harvest_ip(adev);
		amdgpu_discovery_get_gfx_info(adev);
		amdgpu_discovery_get_mall_info(adev);
		amdgpu_discovery_get_vcn_info(adev);
		break;
	}

	amdgpu_discovery_init_soc_config(adev);
	amdgpu_discovery_sysfs_init(adev);

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 0, 1):
	case IP_VERSION(9, 2, 1):
	case IP_VERSION(9, 4, 0):
	case IP_VERSION(9, 4, 1):
	case IP_VERSION(9, 4, 2):
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		adev->family = AMDGPU_FAMILY_AI;
		break;
	case IP_VERSION(9, 1, 0):
	case IP_VERSION(9, 2, 2):
	case IP_VERSION(9, 3, 0):
		adev->family = AMDGPU_FAMILY_RV;
		break;
	case IP_VERSION(10, 1, 10):
	case IP_VERSION(10, 1, 1):
	case IP_VERSION(10, 1, 2):
	case IP_VERSION(10, 1, 3):
	case IP_VERSION(10, 1, 4):
	case IP_VERSION(10, 3, 0):
	case IP_VERSION(10, 3, 2):
	case IP_VERSION(10, 3, 4):
	case IP_VERSION(10, 3, 5):
		adev->family = AMDGPU_FAMILY_NV;
		break;
	case IP_VERSION(10, 3, 1):
		adev->family = AMDGPU_FAMILY_VGH;
		adev->apu_flags |= AMD_APU_IS_VANGOGH;
		break;
	case IP_VERSION(10, 3, 3):
		adev->family = AMDGPU_FAMILY_YC;
		break;
	case IP_VERSION(10, 3, 6):
		adev->family = AMDGPU_FAMILY_GC_10_3_6;
		break;
	case IP_VERSION(10, 3, 7):
		adev->family = AMDGPU_FAMILY_GC_10_3_7;
		break;
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 2):
	case IP_VERSION(11, 0, 3):
		adev->family = AMDGPU_FAMILY_GC_11_0_0;
		break;
	case IP_VERSION(11, 0, 1):
	case IP_VERSION(11, 0, 4):
		adev->family = AMDGPU_FAMILY_GC_11_0_1;
		break;
	case IP_VERSION(11, 5, 0):
	case IP_VERSION(11, 5, 1):
	case IP_VERSION(11, 5, 2):
		adev->family = AMDGPU_FAMILY_GC_11_5_0;
		break;
	case IP_VERSION(12, 0, 0):
	case IP_VERSION(12, 0, 1):
		adev->family = AMDGPU_FAMILY_GC_12_0_0;
		break;
	default:
		return -EINVAL;
	}

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 1, 0):
	case IP_VERSION(9, 2, 2):
	case IP_VERSION(9, 3, 0):
	case IP_VERSION(10, 1, 3):
	case IP_VERSION(10, 1, 4):
	case IP_VERSION(10, 3, 1):
	case IP_VERSION(10, 3, 3):
	case IP_VERSION(10, 3, 6):
	case IP_VERSION(10, 3, 7):
	case IP_VERSION(11, 0, 1):
	case IP_VERSION(11, 0, 4):
	case IP_VERSION(11, 5, 0):
	case IP_VERSION(11, 5, 1):
	case IP_VERSION(11, 5, 2):
		adev->flags |= AMD_IS_APU;
		break;
	default:
		break;
	}

	if (amdgpu_ip_version(adev, XGMI_HWIP, 0) == IP_VERSION(4, 8, 0))
		adev->gmc.xgmi.supported = true;

	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 4))
		adev->ip_versions[XGMI_HWIP][0] = IP_VERSION(6, 4, 0);

	/* set NBIO version */
	switch (amdgpu_ip_version(adev, NBIO_HWIP, 0)) {
	case IP_VERSION(6, 1, 0):
	case IP_VERSION(6, 2, 0):
		adev->nbio.funcs = &nbio_v6_1_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v6_1_hdp_flush_reg;
		break;
	case IP_VERSION(7, 0, 0):
	case IP_VERSION(7, 0, 1):
	case IP_VERSION(2, 5, 0):
		adev->nbio.funcs = &nbio_v7_0_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v7_0_hdp_flush_reg;
		break;
	case IP_VERSION(7, 4, 0):
	case IP_VERSION(7, 4, 1):
	case IP_VERSION(7, 4, 4):
		adev->nbio.funcs = &nbio_v7_4_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v7_4_hdp_flush_reg;
		break;
	case IP_VERSION(7, 9, 0):
		adev->nbio.funcs = &nbio_v7_9_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v7_9_hdp_flush_reg;
		break;
	case IP_VERSION(7, 11, 0):
	case IP_VERSION(7, 11, 1):
	case IP_VERSION(7, 11, 3):
		adev->nbio.funcs = &nbio_v7_11_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v7_11_hdp_flush_reg;
		break;
	case IP_VERSION(7, 2, 0):
	case IP_VERSION(7, 2, 1):
	case IP_VERSION(7, 3, 0):
	case IP_VERSION(7, 5, 0):
	case IP_VERSION(7, 5, 1):
		adev->nbio.funcs = &nbio_v7_2_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v7_2_hdp_flush_reg;
		break;
	case IP_VERSION(2, 1, 1):
	case IP_VERSION(2, 3, 0):
	case IP_VERSION(2, 3, 1):
	case IP_VERSION(2, 3, 2):
	case IP_VERSION(3, 3, 0):
	case IP_VERSION(3, 3, 1):
	case IP_VERSION(3, 3, 2):
	case IP_VERSION(3, 3, 3):
		adev->nbio.funcs = &nbio_v2_3_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v2_3_hdp_flush_reg;
		break;
	case IP_VERSION(4, 3, 0):
	case IP_VERSION(4, 3, 1):
		if (amdgpu_sriov_vf(adev))
			adev->nbio.funcs = &nbio_v4_3_sriov_funcs;
		else
			adev->nbio.funcs = &nbio_v4_3_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v4_3_hdp_flush_reg;
		break;
	case IP_VERSION(7, 7, 0):
	case IP_VERSION(7, 7, 1):
		adev->nbio.funcs = &nbio_v7_7_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v7_7_hdp_flush_reg;
		break;
	case IP_VERSION(6, 3, 1):
		adev->nbio.funcs = &nbif_v6_3_1_funcs;
		adev->nbio.hdp_flush_reg = &nbif_v6_3_1_hdp_flush_reg;
		break;
	default:
		break;
	}

	switch (amdgpu_ip_version(adev, HDP_HWIP, 0)) {
	case IP_VERSION(4, 0, 0):
	case IP_VERSION(4, 0, 1):
	case IP_VERSION(4, 1, 0):
	case IP_VERSION(4, 1, 1):
	case IP_VERSION(4, 1, 2):
	case IP_VERSION(4, 2, 0):
	case IP_VERSION(4, 2, 1):
	case IP_VERSION(4, 4, 0):
	case IP_VERSION(4, 4, 2):
	case IP_VERSION(4, 4, 5):
		adev->hdp.funcs = &hdp_v4_0_funcs;
		break;
	case IP_VERSION(5, 0, 0):
	case IP_VERSION(5, 0, 1):
	case IP_VERSION(5, 0, 2):
	case IP_VERSION(5, 0, 3):
	case IP_VERSION(5, 0, 4):
	case IP_VERSION(5, 2, 0):
		adev->hdp.funcs = &hdp_v5_0_funcs;
		break;
	case IP_VERSION(5, 2, 1):
		adev->hdp.funcs = &hdp_v5_2_funcs;
		break;
	case IP_VERSION(6, 0, 0):
	case IP_VERSION(6, 0, 1):
	case IP_VERSION(6, 1, 0):
		adev->hdp.funcs = &hdp_v6_0_funcs;
		break;
	case IP_VERSION(7, 0, 0):
		adev->hdp.funcs = &hdp_v7_0_funcs;
		break;
	default:
		break;
	}

	switch (amdgpu_ip_version(adev, DF_HWIP, 0)) {
	case IP_VERSION(3, 6, 0):
	case IP_VERSION(3, 6, 1):
	case IP_VERSION(3, 6, 2):
		adev->df.funcs = &df_v3_6_funcs;
		break;
	case IP_VERSION(2, 1, 0):
	case IP_VERSION(2, 1, 1):
	case IP_VERSION(2, 5, 0):
	case IP_VERSION(3, 5, 1):
	case IP_VERSION(3, 5, 2):
		adev->df.funcs = &df_v1_7_funcs;
		break;
	case IP_VERSION(4, 3, 0):
		adev->df.funcs = &df_v4_3_funcs;
		break;
	case IP_VERSION(4, 6, 2):
		adev->df.funcs = &df_v4_6_2_funcs;
		break;
	case IP_VERSION(4, 15, 0):
	case IP_VERSION(4, 15, 1):
		adev->df.funcs = &df_v4_15_funcs;
		break;
	default:
		break;
	}

	switch (amdgpu_ip_version(adev, SMUIO_HWIP, 0)) {
	case IP_VERSION(9, 0, 0):
	case IP_VERSION(9, 0, 1):
	case IP_VERSION(10, 0, 0):
	case IP_VERSION(10, 0, 1):
	case IP_VERSION(10, 0, 2):
		adev->smuio.funcs = &smuio_v9_0_funcs;
		break;
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 2):
	case IP_VERSION(11, 0, 3):
	case IP_VERSION(11, 0, 4):
	case IP_VERSION(11, 0, 7):
	case IP_VERSION(11, 0, 8):
		adev->smuio.funcs = &smuio_v11_0_funcs;
		break;
	case IP_VERSION(11, 0, 6):
	case IP_VERSION(11, 0, 10):
	case IP_VERSION(11, 0, 11):
	case IP_VERSION(11, 5, 0):
	case IP_VERSION(13, 0, 1):
	case IP_VERSION(13, 0, 9):
	case IP_VERSION(13, 0, 10):
		adev->smuio.funcs = &smuio_v11_0_6_funcs;
		break;
	case IP_VERSION(13, 0, 2):
		adev->smuio.funcs = &smuio_v13_0_funcs;
		break;
	case IP_VERSION(13, 0, 3):
		adev->smuio.funcs = &smuio_v13_0_3_funcs;
		if (adev->smuio.funcs->get_pkg_type(adev) == AMDGPU_PKG_TYPE_APU) {
			adev->flags |= AMD_IS_APU;
		}
		break;
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 8):
	case IP_VERSION(14, 0, 0):
	case IP_VERSION(14, 0, 1):
		adev->smuio.funcs = &smuio_v13_0_6_funcs;
		break;
	case IP_VERSION(14, 0, 2):
		adev->smuio.funcs = &smuio_v14_0_2_funcs;
		break;
	default:
		break;
	}

	switch (amdgpu_ip_version(adev, LSDMA_HWIP, 0)) {
	case IP_VERSION(6, 0, 0):
	case IP_VERSION(6, 0, 1):
	case IP_VERSION(6, 0, 2):
	case IP_VERSION(6, 0, 3):
		adev->lsdma.funcs = &lsdma_v6_0_funcs;
		break;
	case IP_VERSION(7, 0, 0):
	case IP_VERSION(7, 0, 1):
		adev->lsdma.funcs = &lsdma_v7_0_funcs;
		break;
	default:
		break;
	}

	r = amdgpu_discovery_set_common_ip_blocks(adev);
	if (r)
		return r;

	r = amdgpu_discovery_set_gmc_ip_blocks(adev);
	if (r)
		return r;

	/* For SR-IOV, PSP needs to be initialized before IH */
	if (amdgpu_sriov_vf(adev)) {
		r = amdgpu_discovery_set_psp_ip_blocks(adev);
		if (r)
			return r;
		r = amdgpu_discovery_set_ih_ip_blocks(adev);
		if (r)
			return r;
	} else {
		r = amdgpu_discovery_set_ih_ip_blocks(adev);
		if (r)
			return r;

		if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP)) {
			r = amdgpu_discovery_set_psp_ip_blocks(adev);
			if (r)
				return r;
		}
	}

	if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP)) {
		r = amdgpu_discovery_set_smu_ip_blocks(adev);
		if (r)
			return r;
	}

	r = amdgpu_discovery_set_display_ip_blocks(adev);
	if (r)
		return r;

	r = amdgpu_discovery_set_gc_ip_blocks(adev);
	if (r)
		return r;

	r = amdgpu_discovery_set_sdma_ip_blocks(adev);
	if (r)
		return r;

	if ((adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT &&
	     !amdgpu_sriov_vf(adev)) ||
	    (adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO && amdgpu_dpm == 1)) {
		r = amdgpu_discovery_set_smu_ip_blocks(adev);
		if (r)
			return r;
	}

	r = amdgpu_discovery_set_mm_ip_blocks(adev);
	if (r)
		return r;

	r = amdgpu_discovery_set_mes_ip_blocks(adev);
	if (r)
		return r;

	r = amdgpu_discovery_set_vpe_ip_blocks(adev);
	if (r)
		return r;

	r = amdgpu_discovery_set_umsch_mm_ip_blocks(adev);
	if (r)
		return r;

	r = amdgpu_discovery_set_isp_ip_blocks(adev);
	if (r)
		return r;
	return 0;
}

