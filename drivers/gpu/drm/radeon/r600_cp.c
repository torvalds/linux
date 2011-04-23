/*
 * Copyright 2008-2009 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *     Alex Deucher <alexander.deucher@amd.com>
 */

#include "drmP.h"
#include "drm.h"
#include "radeon_drm.h"
#include "radeon_drv.h"

#define PFP_UCODE_SIZE 576
#define PM4_UCODE_SIZE 1792
#define R700_PFP_UCODE_SIZE 848
#define R700_PM4_UCODE_SIZE 1360

/* Firmware Names */
MODULE_FIRMWARE("radeon/R600_pfp.bin");
MODULE_FIRMWARE("radeon/R600_me.bin");
MODULE_FIRMWARE("radeon/RV610_pfp.bin");
MODULE_FIRMWARE("radeon/RV610_me.bin");
MODULE_FIRMWARE("radeon/RV630_pfp.bin");
MODULE_FIRMWARE("radeon/RV630_me.bin");
MODULE_FIRMWARE("radeon/RV620_pfp.bin");
MODULE_FIRMWARE("radeon/RV620_me.bin");
MODULE_FIRMWARE("radeon/RV635_pfp.bin");
MODULE_FIRMWARE("radeon/RV635_me.bin");
MODULE_FIRMWARE("radeon/RV670_pfp.bin");
MODULE_FIRMWARE("radeon/RV670_me.bin");
MODULE_FIRMWARE("radeon/RS780_pfp.bin");
MODULE_FIRMWARE("radeon/RS780_me.bin");
MODULE_FIRMWARE("radeon/RV770_pfp.bin");
MODULE_FIRMWARE("radeon/RV770_me.bin");
MODULE_FIRMWARE("radeon/RV730_pfp.bin");
MODULE_FIRMWARE("radeon/RV730_me.bin");
MODULE_FIRMWARE("radeon/RV710_pfp.bin");
MODULE_FIRMWARE("radeon/RV710_me.bin");


int r600_cs_legacy(struct drm_device *dev, void *data, struct drm_file *filp,
			unsigned family, u32 *ib, int *l);
void r600_cs_legacy_init(void);


# define ATI_PCIGART_PAGE_SIZE		4096	/**< PCI GART page size */
# define ATI_PCIGART_PAGE_MASK		(~(ATI_PCIGART_PAGE_SIZE-1))

#define R600_PTE_VALID     (1 << 0)
#define R600_PTE_SYSTEM    (1 << 1)
#define R600_PTE_SNOOPED   (1 << 2)
#define R600_PTE_READABLE  (1 << 5)
#define R600_PTE_WRITEABLE (1 << 6)

/* MAX values used for gfx init */
#define R6XX_MAX_SH_GPRS           256
#define R6XX_MAX_TEMP_GPRS         16
#define R6XX_MAX_SH_THREADS        256
#define R6XX_MAX_SH_STACK_ENTRIES  4096
#define R6XX_MAX_BACKENDS          8
#define R6XX_MAX_BACKENDS_MASK     0xff
#define R6XX_MAX_SIMDS             8
#define R6XX_MAX_SIMDS_MASK        0xff
#define R6XX_MAX_PIPES             8
#define R6XX_MAX_PIPES_MASK        0xff

#define R7XX_MAX_SH_GPRS           256
#define R7XX_MAX_TEMP_GPRS         16
#define R7XX_MAX_SH_THREADS        256
#define R7XX_MAX_SH_STACK_ENTRIES  4096
#define R7XX_MAX_BACKENDS          8
#define R7XX_MAX_BACKENDS_MASK     0xff
#define R7XX_MAX_SIMDS             16
#define R7XX_MAX_SIMDS_MASK        0xffff
#define R7XX_MAX_PIPES             8
#define R7XX_MAX_PIPES_MASK        0xff

static int r600_do_wait_for_fifo(drm_radeon_private_t *dev_priv, int entries)
{
	int i;

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		int slots;
		if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RV770)
			slots = (RADEON_READ(R600_GRBM_STATUS)
				 & R700_CMDFIFO_AVAIL_MASK);
		else
			slots = (RADEON_READ(R600_GRBM_STATUS)
				 & R600_CMDFIFO_AVAIL_MASK);
		if (slots >= entries)
			return 0;
		DRM_UDELAY(1);
	}
	DRM_INFO("wait for fifo failed status : 0x%08X 0x%08X\n",
		 RADEON_READ(R600_GRBM_STATUS),
		 RADEON_READ(R600_GRBM_STATUS2));

	return -EBUSY;
}

static int r600_do_wait_for_idle(drm_radeon_private_t *dev_priv)
{
	int i, ret;

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RV770)
		ret = r600_do_wait_for_fifo(dev_priv, 8);
	else
		ret = r600_do_wait_for_fifo(dev_priv, 16);
	if (ret)
		return ret;
	for (i = 0; i < dev_priv->usec_timeout; i++) {
		if (!(RADEON_READ(R600_GRBM_STATUS) & R600_GUI_ACTIVE))
			return 0;
		DRM_UDELAY(1);
	}
	DRM_INFO("wait idle failed status : 0x%08X 0x%08X\n",
		 RADEON_READ(R600_GRBM_STATUS),
		 RADEON_READ(R600_GRBM_STATUS2));

	return -EBUSY;
}

void r600_page_table_cleanup(struct drm_device *dev, struct drm_ati_pcigart_info *gart_info)
{
	struct drm_sg_mem *entry = dev->sg;
	int max_pages;
	int pages;
	int i;

	if (!entry)
		return;

	if (gart_info->bus_addr) {
		max_pages = (gart_info->table_size / sizeof(u64));
		pages = (entry->pages <= max_pages)
		  ? entry->pages : max_pages;

		for (i = 0; i < pages; i++) {
			if (!entry->busaddr[i])
				break;
			pci_unmap_page(dev->pdev, entry->busaddr[i],
				       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		}
		if (gart_info->gart_table_location == DRM_ATI_GART_MAIN)
			gart_info->bus_addr = 0;
	}
}

/* R600 has page table setup */
int r600_page_table_init(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	struct drm_ati_pcigart_info *gart_info = &dev_priv->gart_info;
	struct drm_local_map *map = &gart_info->mapping;
	struct drm_sg_mem *entry = dev->sg;
	int ret = 0;
	int i, j;
	int pages;
	u64 page_base;
	dma_addr_t entry_addr;
	int max_ati_pages, max_real_pages, gart_idx;

	/* okay page table is available - lets rock */
	max_ati_pages = (gart_info->table_size / sizeof(u64));
	max_real_pages = max_ati_pages / (PAGE_SIZE / ATI_PCIGART_PAGE_SIZE);

	pages = (entry->pages <= max_real_pages) ?
		entry->pages : max_real_pages;

	memset_io((void __iomem *)map->handle, 0, max_ati_pages * sizeof(u64));

	gart_idx = 0;
	for (i = 0; i < pages; i++) {
		entry->busaddr[i] = pci_map_page(dev->pdev,
						 entry->pagelist[i], 0,
						 PAGE_SIZE,
						 PCI_DMA_BIDIRECTIONAL);
		if (pci_dma_mapping_error(dev->pdev, entry->busaddr[i])) {
			DRM_ERROR("unable to map PCIGART pages!\n");
			r600_page_table_cleanup(dev, gart_info);
			goto done;
		}
		entry_addr = entry->busaddr[i];
		for (j = 0; j < (PAGE_SIZE / ATI_PCIGART_PAGE_SIZE); j++) {
			page_base = (u64) entry_addr & ATI_PCIGART_PAGE_MASK;
			page_base |= R600_PTE_VALID | R600_PTE_SYSTEM | R600_PTE_SNOOPED;
			page_base |= R600_PTE_READABLE | R600_PTE_WRITEABLE;

			DRM_WRITE64(map, gart_idx * sizeof(u64), page_base);

			gart_idx++;

			if ((i % 128) == 0)
				DRM_DEBUG("page entry %d: 0x%016llx\n",
				    i, (unsigned long long)page_base);
			entry_addr += ATI_PCIGART_PAGE_SIZE;
		}
	}
	ret = 1;
done:
	return ret;
}

static void r600_vm_flush_gart_range(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	u32 resp, countdown = 1000;
	RADEON_WRITE(R600_VM_CONTEXT0_INVALIDATION_LOW_ADDR, dev_priv->gart_vm_start >> 12);
	RADEON_WRITE(R600_VM_CONTEXT0_INVALIDATION_HIGH_ADDR, (dev_priv->gart_vm_start + dev_priv->gart_size - 1) >> 12);
	RADEON_WRITE(R600_VM_CONTEXT0_REQUEST_RESPONSE, 2);

	do {
		resp = RADEON_READ(R600_VM_CONTEXT0_REQUEST_RESPONSE);
		countdown--;
		DRM_UDELAY(1);
	} while (((resp & 0xf0) == 0) && countdown);
}

static void r600_vm_init(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	/* initialise the VM to use the page table we constructed up there */
	u32 vm_c0, i;
	u32 mc_rd_a;
	u32 vm_l2_cntl, vm_l2_cntl3;
	/* okay set up the PCIE aperture type thingo */
	RADEON_WRITE(R600_MC_VM_SYSTEM_APERTURE_LOW_ADDR, dev_priv->gart_vm_start >> 12);
	RADEON_WRITE(R600_MC_VM_SYSTEM_APERTURE_HIGH_ADDR, (dev_priv->gart_vm_start + dev_priv->gart_size - 1) >> 12);
	RADEON_WRITE(R600_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR, 0);

	/* setup MC RD a */
	mc_rd_a = R600_MCD_L1_TLB | R600_MCD_L1_FRAG_PROC | R600_MCD_SYSTEM_ACCESS_MODE_IN_SYS |
		R600_MCD_SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU | R600_MCD_EFFECTIVE_L1_TLB_SIZE(5) |
		R600_MCD_EFFECTIVE_L1_QUEUE_SIZE(5) | R600_MCD_WAIT_L2_QUERY;

	RADEON_WRITE(R600_MCD_RD_A_CNTL, mc_rd_a);
	RADEON_WRITE(R600_MCD_RD_B_CNTL, mc_rd_a);

	RADEON_WRITE(R600_MCD_WR_A_CNTL, mc_rd_a);
	RADEON_WRITE(R600_MCD_WR_B_CNTL, mc_rd_a);

	RADEON_WRITE(R600_MCD_RD_GFX_CNTL, mc_rd_a);
	RADEON_WRITE(R600_MCD_WR_GFX_CNTL, mc_rd_a);

	RADEON_WRITE(R600_MCD_RD_SYS_CNTL, mc_rd_a);
	RADEON_WRITE(R600_MCD_WR_SYS_CNTL, mc_rd_a);

	RADEON_WRITE(R600_MCD_RD_HDP_CNTL, mc_rd_a | R600_MCD_L1_STRICT_ORDERING);
	RADEON_WRITE(R600_MCD_WR_HDP_CNTL, mc_rd_a /*| R600_MCD_L1_STRICT_ORDERING*/);

	RADEON_WRITE(R600_MCD_RD_PDMA_CNTL, mc_rd_a);
	RADEON_WRITE(R600_MCD_WR_PDMA_CNTL, mc_rd_a);

	RADEON_WRITE(R600_MCD_RD_SEM_CNTL, mc_rd_a | R600_MCD_SEMAPHORE_MODE);
	RADEON_WRITE(R600_MCD_WR_SEM_CNTL, mc_rd_a);

	vm_l2_cntl = R600_VM_L2_CACHE_EN | R600_VM_L2_FRAG_PROC | R600_VM_ENABLE_PTE_CACHE_LRU_W;
	vm_l2_cntl |= R600_VM_L2_CNTL_QUEUE_SIZE(7);
	RADEON_WRITE(R600_VM_L2_CNTL, vm_l2_cntl);

	RADEON_WRITE(R600_VM_L2_CNTL2, 0);
	vm_l2_cntl3 = (R600_VM_L2_CNTL3_BANK_SELECT_0(0) |
		       R600_VM_L2_CNTL3_BANK_SELECT_1(1) |
		       R600_VM_L2_CNTL3_CACHE_UPDATE_MODE(2));
	RADEON_WRITE(R600_VM_L2_CNTL3, vm_l2_cntl3);

	vm_c0 = R600_VM_ENABLE_CONTEXT | R600_VM_PAGE_TABLE_DEPTH_FLAT;

	RADEON_WRITE(R600_VM_CONTEXT0_CNTL, vm_c0);

	vm_c0 &= ~R600_VM_ENABLE_CONTEXT;

	/* disable all other contexts */
	for (i = 1; i < 8; i++)
		RADEON_WRITE(R600_VM_CONTEXT0_CNTL + (i * 4), vm_c0);

	RADEON_WRITE(R600_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR, dev_priv->gart_info.bus_addr >> 12);
	RADEON_WRITE(R600_VM_CONTEXT0_PAGE_TABLE_START_ADDR, dev_priv->gart_vm_start >> 12);
	RADEON_WRITE(R600_VM_CONTEXT0_PAGE_TABLE_END_ADDR, (dev_priv->gart_vm_start + dev_priv->gart_size - 1) >> 12);

	r600_vm_flush_gart_range(dev);
}

static int r600_cp_init_microcode(drm_radeon_private_t *dev_priv)
{
	struct platform_device *pdev;
	const char *chip_name;
	size_t pfp_req_size, me_req_size;
	char fw_name[30];
	int err;

	pdev = platform_device_register_simple("r600_cp", 0, NULL, 0);
	err = IS_ERR(pdev);
	if (err) {
		printk(KERN_ERR "r600_cp: Failed to register firmware\n");
		return -EINVAL;
	}

	switch (dev_priv->flags & RADEON_FAMILY_MASK) {
	case CHIP_R600:  chip_name = "R600";  break;
	case CHIP_RV610: chip_name = "RV610"; break;
	case CHIP_RV630: chip_name = "RV630"; break;
	case CHIP_RV620: chip_name = "RV620"; break;
	case CHIP_RV635: chip_name = "RV635"; break;
	case CHIP_RV670: chip_name = "RV670"; break;
	case CHIP_RS780:
	case CHIP_RS880: chip_name = "RS780"; break;
	case CHIP_RV770: chip_name = "RV770"; break;
	case CHIP_RV730:
	case CHIP_RV740: chip_name = "RV730"; break;
	case CHIP_RV710: chip_name = "RV710"; break;
	default:         BUG();
	}

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RV770) {
		pfp_req_size = R700_PFP_UCODE_SIZE * 4;
		me_req_size = R700_PM4_UCODE_SIZE * 4;
	} else {
		pfp_req_size = PFP_UCODE_SIZE * 4;
		me_req_size = PM4_UCODE_SIZE * 12;
	}

	DRM_INFO("Loading %s CP Microcode\n", chip_name);

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_pfp.bin", chip_name);
	err = request_firmware(&dev_priv->pfp_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (dev_priv->pfp_fw->size != pfp_req_size) {
		printk(KERN_ERR
		       "r600_cp: Bogus length %zu in firmware \"%s\"\n",
		       dev_priv->pfp_fw->size, fw_name);
		err = -EINVAL;
		goto out;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_me.bin", chip_name);
	err = request_firmware(&dev_priv->me_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (dev_priv->me_fw->size != me_req_size) {
		printk(KERN_ERR
		       "r600_cp: Bogus length %zu in firmware \"%s\"\n",
		       dev_priv->me_fw->size, fw_name);
		err = -EINVAL;
	}
out:
	platform_device_unregister(pdev);

	if (err) {
		if (err != -EINVAL)
			printk(KERN_ERR
			       "r600_cp: Failed to load firmware \"%s\"\n",
			       fw_name);
		release_firmware(dev_priv->pfp_fw);
		dev_priv->pfp_fw = NULL;
		release_firmware(dev_priv->me_fw);
		dev_priv->me_fw = NULL;
	}
	return err;
}

static void r600_cp_load_microcode(drm_radeon_private_t *dev_priv)
{
	const __be32 *fw_data;
	int i;

	if (!dev_priv->me_fw || !dev_priv->pfp_fw)
		return;

	r600_do_cp_stop(dev_priv);

	RADEON_WRITE(R600_CP_RB_CNTL,
#ifdef __BIG_ENDIAN
		     R600_BUF_SWAP_32BIT |
#endif
		     R600_RB_NO_UPDATE |
		     R600_RB_BLKSZ(15) |
		     R600_RB_BUFSZ(3));

	RADEON_WRITE(R600_GRBM_SOFT_RESET, R600_SOFT_RESET_CP);
	RADEON_READ(R600_GRBM_SOFT_RESET);
	DRM_UDELAY(15000);
	RADEON_WRITE(R600_GRBM_SOFT_RESET, 0);

	fw_data = (const __be32 *)dev_priv->me_fw->data;
	RADEON_WRITE(R600_CP_ME_RAM_WADDR, 0);
	for (i = 0; i < PM4_UCODE_SIZE * 3; i++)
		RADEON_WRITE(R600_CP_ME_RAM_DATA,
			     be32_to_cpup(fw_data++));

	fw_data = (const __be32 *)dev_priv->pfp_fw->data;
	RADEON_WRITE(R600_CP_PFP_UCODE_ADDR, 0);
	for (i = 0; i < PFP_UCODE_SIZE; i++)
		RADEON_WRITE(R600_CP_PFP_UCODE_DATA,
			     be32_to_cpup(fw_data++));

	RADEON_WRITE(R600_CP_PFP_UCODE_ADDR, 0);
	RADEON_WRITE(R600_CP_ME_RAM_WADDR, 0);
	RADEON_WRITE(R600_CP_ME_RAM_RADDR, 0);

}

static void r700_vm_init(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	/* initialise the VM to use the page table we constructed up there */
	u32 vm_c0, i;
	u32 mc_vm_md_l1;
	u32 vm_l2_cntl, vm_l2_cntl3;
	/* okay set up the PCIE aperture type thingo */
	RADEON_WRITE(R700_MC_VM_SYSTEM_APERTURE_LOW_ADDR, dev_priv->gart_vm_start >> 12);
	RADEON_WRITE(R700_MC_VM_SYSTEM_APERTURE_HIGH_ADDR, (dev_priv->gart_vm_start + dev_priv->gart_size - 1) >> 12);
	RADEON_WRITE(R700_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR, 0);

	mc_vm_md_l1 = R700_ENABLE_L1_TLB |
	    R700_ENABLE_L1_FRAGMENT_PROCESSING |
	    R700_SYSTEM_ACCESS_MODE_IN_SYS |
	    R700_SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU |
	    R700_EFFECTIVE_L1_TLB_SIZE(5) |
	    R700_EFFECTIVE_L1_QUEUE_SIZE(5);

	RADEON_WRITE(R700_MC_VM_MD_L1_TLB0_CNTL, mc_vm_md_l1);
	RADEON_WRITE(R700_MC_VM_MD_L1_TLB1_CNTL, mc_vm_md_l1);
	RADEON_WRITE(R700_MC_VM_MD_L1_TLB2_CNTL, mc_vm_md_l1);
	RADEON_WRITE(R700_MC_VM_MB_L1_TLB0_CNTL, mc_vm_md_l1);
	RADEON_WRITE(R700_MC_VM_MB_L1_TLB1_CNTL, mc_vm_md_l1);
	RADEON_WRITE(R700_MC_VM_MB_L1_TLB2_CNTL, mc_vm_md_l1);
	RADEON_WRITE(R700_MC_VM_MB_L1_TLB3_CNTL, mc_vm_md_l1);

	vm_l2_cntl = R600_VM_L2_CACHE_EN | R600_VM_L2_FRAG_PROC | R600_VM_ENABLE_PTE_CACHE_LRU_W;
	vm_l2_cntl |= R700_VM_L2_CNTL_QUEUE_SIZE(7);
	RADEON_WRITE(R600_VM_L2_CNTL, vm_l2_cntl);

	RADEON_WRITE(R600_VM_L2_CNTL2, 0);
	vm_l2_cntl3 = R700_VM_L2_CNTL3_BANK_SELECT(0) | R700_VM_L2_CNTL3_CACHE_UPDATE_MODE(2);
	RADEON_WRITE(R600_VM_L2_CNTL3, vm_l2_cntl3);

	vm_c0 = R600_VM_ENABLE_CONTEXT | R600_VM_PAGE_TABLE_DEPTH_FLAT;

	RADEON_WRITE(R600_VM_CONTEXT0_CNTL, vm_c0);

	vm_c0 &= ~R600_VM_ENABLE_CONTEXT;

	/* disable all other contexts */
	for (i = 1; i < 8; i++)
		RADEON_WRITE(R600_VM_CONTEXT0_CNTL + (i * 4), vm_c0);

	RADEON_WRITE(R700_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR, dev_priv->gart_info.bus_addr >> 12);
	RADEON_WRITE(R700_VM_CONTEXT0_PAGE_TABLE_START_ADDR, dev_priv->gart_vm_start >> 12);
	RADEON_WRITE(R700_VM_CONTEXT0_PAGE_TABLE_END_ADDR, (dev_priv->gart_vm_start + dev_priv->gart_size - 1) >> 12);

	r600_vm_flush_gart_range(dev);
}

static void r700_cp_load_microcode(drm_radeon_private_t *dev_priv)
{
	const __be32 *fw_data;
	int i;

	if (!dev_priv->me_fw || !dev_priv->pfp_fw)
		return;

	r600_do_cp_stop(dev_priv);

	RADEON_WRITE(R600_CP_RB_CNTL,
#ifdef __BIG_ENDIAN
		     R600_BUF_SWAP_32BIT |
#endif
		     R600_RB_NO_UPDATE |
		     R600_RB_BLKSZ(15) |
		     R600_RB_BUFSZ(3));

	RADEON_WRITE(R600_GRBM_SOFT_RESET, R600_SOFT_RESET_CP);
	RADEON_READ(R600_GRBM_SOFT_RESET);
	DRM_UDELAY(15000);
	RADEON_WRITE(R600_GRBM_SOFT_RESET, 0);

	fw_data = (const __be32 *)dev_priv->pfp_fw->data;
	RADEON_WRITE(R600_CP_PFP_UCODE_ADDR, 0);
	for (i = 0; i < R700_PFP_UCODE_SIZE; i++)
		RADEON_WRITE(R600_CP_PFP_UCODE_DATA, be32_to_cpup(fw_data++));
	RADEON_WRITE(R600_CP_PFP_UCODE_ADDR, 0);

	fw_data = (const __be32 *)dev_priv->me_fw->data;
	RADEON_WRITE(R600_CP_ME_RAM_WADDR, 0);
	for (i = 0; i < R700_PM4_UCODE_SIZE; i++)
		RADEON_WRITE(R600_CP_ME_RAM_DATA, be32_to_cpup(fw_data++));
	RADEON_WRITE(R600_CP_ME_RAM_WADDR, 0);

	RADEON_WRITE(R600_CP_PFP_UCODE_ADDR, 0);
	RADEON_WRITE(R600_CP_ME_RAM_WADDR, 0);
	RADEON_WRITE(R600_CP_ME_RAM_RADDR, 0);

}

static void r600_test_writeback(drm_radeon_private_t *dev_priv)
{
	u32 tmp;

	/* Start with assuming that writeback doesn't work */
	dev_priv->writeback_works = 0;

	/* Writeback doesn't seem to work everywhere, test it here and possibly
	 * enable it if it appears to work
	 */
	radeon_write_ring_rptr(dev_priv, R600_SCRATCHOFF(1), 0);

	RADEON_WRITE(R600_SCRATCH_REG1, 0xdeadbeef);

	for (tmp = 0; tmp < dev_priv->usec_timeout; tmp++) {
		u32 val;

		val = radeon_read_ring_rptr(dev_priv, R600_SCRATCHOFF(1));
		if (val == 0xdeadbeef)
			break;
		DRM_UDELAY(1);
	}

	if (tmp < dev_priv->usec_timeout) {
		dev_priv->writeback_works = 1;
		DRM_INFO("writeback test succeeded in %d usecs\n", tmp);
	} else {
		dev_priv->writeback_works = 0;
		DRM_INFO("writeback test failed\n");
	}
	if (radeon_no_wb == 1) {
		dev_priv->writeback_works = 0;
		DRM_INFO("writeback forced off\n");
	}

	if (!dev_priv->writeback_works) {
		/* Disable writeback to avoid unnecessary bus master transfer */
		RADEON_WRITE(R600_CP_RB_CNTL,
#ifdef __BIG_ENDIAN
			     R600_BUF_SWAP_32BIT |
#endif
			     RADEON_READ(R600_CP_RB_CNTL) |
			     R600_RB_NO_UPDATE);
		RADEON_WRITE(R600_SCRATCH_UMSK, 0);
	}
}

int r600_do_engine_reset(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	u32 cp_ptr, cp_me_cntl, cp_rb_cntl;

	DRM_INFO("Resetting GPU\n");

	cp_ptr = RADEON_READ(R600_CP_RB_WPTR);
	cp_me_cntl = RADEON_READ(R600_CP_ME_CNTL);
	RADEON_WRITE(R600_CP_ME_CNTL, R600_CP_ME_HALT);

	RADEON_WRITE(R600_GRBM_SOFT_RESET, 0x7fff);
	RADEON_READ(R600_GRBM_SOFT_RESET);
	DRM_UDELAY(50);
	RADEON_WRITE(R600_GRBM_SOFT_RESET, 0);
	RADEON_READ(R600_GRBM_SOFT_RESET);

	RADEON_WRITE(R600_CP_RB_WPTR_DELAY, 0);
	cp_rb_cntl = RADEON_READ(R600_CP_RB_CNTL);
	RADEON_WRITE(R600_CP_RB_CNTL,
#ifdef __BIG_ENDIAN
		     R600_BUF_SWAP_32BIT |
#endif
		     R600_RB_RPTR_WR_ENA);

	RADEON_WRITE(R600_CP_RB_RPTR_WR, cp_ptr);
	RADEON_WRITE(R600_CP_RB_WPTR, cp_ptr);
	RADEON_WRITE(R600_CP_RB_CNTL, cp_rb_cntl);
	RADEON_WRITE(R600_CP_ME_CNTL, cp_me_cntl);

	/* Reset the CP ring */
	r600_do_cp_reset(dev_priv);

	/* The CP is no longer running after an engine reset */
	dev_priv->cp_running = 0;

	/* Reset any pending vertex, indirect buffers */
	radeon_freelist_reset(dev);

	return 0;

}

static u32 r600_get_tile_pipe_to_backend_map(u32 num_tile_pipes,
					     u32 num_backends,
					     u32 backend_disable_mask)
{
	u32 backend_map = 0;
	u32 enabled_backends_mask;
	u32 enabled_backends_count;
	u32 cur_pipe;
	u32 swizzle_pipe[R6XX_MAX_PIPES];
	u32 cur_backend;
	u32 i;

	if (num_tile_pipes > R6XX_MAX_PIPES)
		num_tile_pipes = R6XX_MAX_PIPES;
	if (num_tile_pipes < 1)
		num_tile_pipes = 1;
	if (num_backends > R6XX_MAX_BACKENDS)
		num_backends = R6XX_MAX_BACKENDS;
	if (num_backends < 1)
		num_backends = 1;

	enabled_backends_mask = 0;
	enabled_backends_count = 0;
	for (i = 0; i < R6XX_MAX_BACKENDS; ++i) {
		if (((backend_disable_mask >> i) & 1) == 0) {
			enabled_backends_mask |= (1 << i);
			++enabled_backends_count;
		}
		if (enabled_backends_count == num_backends)
			break;
	}

	if (enabled_backends_count == 0) {
		enabled_backends_mask = 1;
		enabled_backends_count = 1;
	}

	if (enabled_backends_count != num_backends)
		num_backends = enabled_backends_count;

	memset((uint8_t *)&swizzle_pipe[0], 0, sizeof(u32) * R6XX_MAX_PIPES);
	switch (num_tile_pipes) {
	case 1:
		swizzle_pipe[0] = 0;
		break;
	case 2:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 1;
		break;
	case 3:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 1;
		swizzle_pipe[2] = 2;
		break;
	case 4:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 1;
		swizzle_pipe[2] = 2;
		swizzle_pipe[3] = 3;
		break;
	case 5:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 1;
		swizzle_pipe[2] = 2;
		swizzle_pipe[3] = 3;
		swizzle_pipe[4] = 4;
		break;
	case 6:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 2;
		swizzle_pipe[2] = 4;
		swizzle_pipe[3] = 5;
		swizzle_pipe[4] = 1;
		swizzle_pipe[5] = 3;
		break;
	case 7:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 2;
		swizzle_pipe[2] = 4;
		swizzle_pipe[3] = 6;
		swizzle_pipe[4] = 1;
		swizzle_pipe[5] = 3;
		swizzle_pipe[6] = 5;
		break;
	case 8:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 2;
		swizzle_pipe[2] = 4;
		swizzle_pipe[3] = 6;
		swizzle_pipe[4] = 1;
		swizzle_pipe[5] = 3;
		swizzle_pipe[6] = 5;
		swizzle_pipe[7] = 7;
		break;
	}

	cur_backend = 0;
	for (cur_pipe = 0; cur_pipe < num_tile_pipes; ++cur_pipe) {
		while (((1 << cur_backend) & enabled_backends_mask) == 0)
			cur_backend = (cur_backend + 1) % R6XX_MAX_BACKENDS;

		backend_map |= (u32)(((cur_backend & 3) << (swizzle_pipe[cur_pipe] * 2)));

		cur_backend = (cur_backend + 1) % R6XX_MAX_BACKENDS;
	}

	return backend_map;
}

static int r600_count_pipe_bits(uint32_t val)
{
	int i, ret = 0;
	for (i = 0; i < 32; i++) {
		ret += val & 1;
		val >>= 1;
	}
	return ret;
}

static void r600_gfx_init(struct drm_device *dev,
			  drm_radeon_private_t *dev_priv)
{
	int i, j, num_qd_pipes;
	u32 sx_debug_1;
	u32 tc_cntl;
	u32 arb_pop;
	u32 num_gs_verts_per_thread;
	u32 vgt_gs_per_es;
	u32 gs_prim_buffer_depth = 0;
	u32 sq_ms_fifo_sizes;
	u32 sq_config;
	u32 sq_gpr_resource_mgmt_1 = 0;
	u32 sq_gpr_resource_mgmt_2 = 0;
	u32 sq_thread_resource_mgmt = 0;
	u32 sq_stack_resource_mgmt_1 = 0;
	u32 sq_stack_resource_mgmt_2 = 0;
	u32 hdp_host_path_cntl;
	u32 backend_map;
	u32 gb_tiling_config = 0;
	u32 cc_rb_backend_disable;
	u32 cc_gc_shader_pipe_config;
	u32 ramcfg;

	/* setup chip specs */
	switch (dev_priv->flags & RADEON_FAMILY_MASK) {
	case CHIP_R600:
		dev_priv->r600_max_pipes = 4;
		dev_priv->r600_max_tile_pipes = 8;
		dev_priv->r600_max_simds = 4;
		dev_priv->r600_max_backends = 4;
		dev_priv->r600_max_gprs = 256;
		dev_priv->r600_max_threads = 192;
		dev_priv->r600_max_stack_entries = 256;
		dev_priv->r600_max_hw_contexts = 8;
		dev_priv->r600_max_gs_threads = 16;
		dev_priv->r600_sx_max_export_size = 128;
		dev_priv->r600_sx_max_export_pos_size = 16;
		dev_priv->r600_sx_max_export_smx_size = 128;
		dev_priv->r600_sq_num_cf_insts = 2;
		break;
	case CHIP_RV630:
	case CHIP_RV635:
		dev_priv->r600_max_pipes = 2;
		dev_priv->r600_max_tile_pipes = 2;
		dev_priv->r600_max_simds = 3;
		dev_priv->r600_max_backends = 1;
		dev_priv->r600_max_gprs = 128;
		dev_priv->r600_max_threads = 192;
		dev_priv->r600_max_stack_entries = 128;
		dev_priv->r600_max_hw_contexts = 8;
		dev_priv->r600_max_gs_threads = 4;
		dev_priv->r600_sx_max_export_size = 128;
		dev_priv->r600_sx_max_export_pos_size = 16;
		dev_priv->r600_sx_max_export_smx_size = 128;
		dev_priv->r600_sq_num_cf_insts = 2;
		break;
	case CHIP_RV610:
	case CHIP_RS780:
	case CHIP_RS880:
	case CHIP_RV620:
		dev_priv->r600_max_pipes = 1;
		dev_priv->r600_max_tile_pipes = 1;
		dev_priv->r600_max_simds = 2;
		dev_priv->r600_max_backends = 1;
		dev_priv->r600_max_gprs = 128;
		dev_priv->r600_max_threads = 192;
		dev_priv->r600_max_stack_entries = 128;
		dev_priv->r600_max_hw_contexts = 4;
		dev_priv->r600_max_gs_threads = 4;
		dev_priv->r600_sx_max_export_size = 128;
		dev_priv->r600_sx_max_export_pos_size = 16;
		dev_priv->r600_sx_max_export_smx_size = 128;
		dev_priv->r600_sq_num_cf_insts = 1;
		break;
	case CHIP_RV670:
		dev_priv->r600_max_pipes = 4;
		dev_priv->r600_max_tile_pipes = 4;
		dev_priv->r600_max_simds = 4;
		dev_priv->r600_max_backends = 4;
		dev_priv->r600_max_gprs = 192;
		dev_priv->r600_max_threads = 192;
		dev_priv->r600_max_stack_entries = 256;
		dev_priv->r600_max_hw_contexts = 8;
		dev_priv->r600_max_gs_threads = 16;
		dev_priv->r600_sx_max_export_size = 128;
		dev_priv->r600_sx_max_export_pos_size = 16;
		dev_priv->r600_sx_max_export_smx_size = 128;
		dev_priv->r600_sq_num_cf_insts = 2;
		break;
	default:
		break;
	}

	/* Initialize HDP */
	j = 0;
	for (i = 0; i < 32; i++) {
		RADEON_WRITE((0x2c14 + j), 0x00000000);
		RADEON_WRITE((0x2c18 + j), 0x00000000);
		RADEON_WRITE((0x2c1c + j), 0x00000000);
		RADEON_WRITE((0x2c20 + j), 0x00000000);
		RADEON_WRITE((0x2c24 + j), 0x00000000);
		j += 0x18;
	}

	RADEON_WRITE(R600_GRBM_CNTL, R600_GRBM_READ_TIMEOUT(0xff));

	/* setup tiling, simd, pipe config */
	ramcfg = RADEON_READ(R600_RAMCFG);

	switch (dev_priv->r600_max_tile_pipes) {
	case 1:
		gb_tiling_config |= R600_PIPE_TILING(0);
		break;
	case 2:
		gb_tiling_config |= R600_PIPE_TILING(1);
		break;
	case 4:
		gb_tiling_config |= R600_PIPE_TILING(2);
		break;
	case 8:
		gb_tiling_config |= R600_PIPE_TILING(3);
		break;
	default:
		break;
	}

	gb_tiling_config |= R600_BANK_TILING((ramcfg >> R600_NOOFBANK_SHIFT) & R600_NOOFBANK_MASK);

	gb_tiling_config |= R600_GROUP_SIZE(0);

	if (((ramcfg >> R600_NOOFROWS_SHIFT) & R600_NOOFROWS_MASK) > 3) {
		gb_tiling_config |= R600_ROW_TILING(3);
		gb_tiling_config |= R600_SAMPLE_SPLIT(3);
	} else {
		gb_tiling_config |=
			R600_ROW_TILING(((ramcfg >> R600_NOOFROWS_SHIFT) & R600_NOOFROWS_MASK));
		gb_tiling_config |=
			R600_SAMPLE_SPLIT(((ramcfg >> R600_NOOFROWS_SHIFT) & R600_NOOFROWS_MASK));
	}

	gb_tiling_config |= R600_BANK_SWAPS(1);

	cc_rb_backend_disable = RADEON_READ(R600_CC_RB_BACKEND_DISABLE) & 0x00ff0000;
	cc_rb_backend_disable |=
		R600_BACKEND_DISABLE((R6XX_MAX_BACKENDS_MASK << dev_priv->r600_max_backends) & R6XX_MAX_BACKENDS_MASK);

	cc_gc_shader_pipe_config = RADEON_READ(R600_CC_GC_SHADER_PIPE_CONFIG) & 0xffffff00;
	cc_gc_shader_pipe_config |=
		R600_INACTIVE_QD_PIPES((R6XX_MAX_PIPES_MASK << dev_priv->r600_max_pipes) & R6XX_MAX_PIPES_MASK);
	cc_gc_shader_pipe_config |=
		R600_INACTIVE_SIMDS((R6XX_MAX_SIMDS_MASK << dev_priv->r600_max_simds) & R6XX_MAX_SIMDS_MASK);

	backend_map = r600_get_tile_pipe_to_backend_map(dev_priv->r600_max_tile_pipes,
							(R6XX_MAX_BACKENDS -
							 r600_count_pipe_bits((cc_rb_backend_disable &
									       R6XX_MAX_BACKENDS_MASK) >> 16)),
							(cc_rb_backend_disable >> 16));
	gb_tiling_config |= R600_BACKEND_MAP(backend_map);

	RADEON_WRITE(R600_GB_TILING_CONFIG,      gb_tiling_config);
	RADEON_WRITE(R600_DCP_TILING_CONFIG,    (gb_tiling_config & 0xffff));
	RADEON_WRITE(R600_HDP_TILING_CONFIG,    (gb_tiling_config & 0xffff));
	if (gb_tiling_config & 0xc0) {
		dev_priv->r600_group_size = 512;
	} else {
		dev_priv->r600_group_size = 256;
	}
	dev_priv->r600_npipes = 1 << ((gb_tiling_config >> 1) & 0x7);
	if (gb_tiling_config & 0x30) {
		dev_priv->r600_nbanks = 8;
	} else {
		dev_priv->r600_nbanks = 4;
	}

	RADEON_WRITE(R600_CC_RB_BACKEND_DISABLE,      cc_rb_backend_disable);
	RADEON_WRITE(R600_CC_GC_SHADER_PIPE_CONFIG,   cc_gc_shader_pipe_config);
	RADEON_WRITE(R600_GC_USER_SHADER_PIPE_CONFIG, cc_gc_shader_pipe_config);

	num_qd_pipes =
		R6XX_MAX_PIPES - r600_count_pipe_bits((cc_gc_shader_pipe_config & R600_INACTIVE_QD_PIPES_MASK) >> 8);
	RADEON_WRITE(R600_VGT_OUT_DEALLOC_CNTL, (num_qd_pipes * 4) & R600_DEALLOC_DIST_MASK);
	RADEON_WRITE(R600_VGT_VERTEX_REUSE_BLOCK_CNTL, ((num_qd_pipes * 4) - 2) & R600_VTX_REUSE_DEPTH_MASK);

	/* set HW defaults for 3D engine */
	RADEON_WRITE(R600_CP_QUEUE_THRESHOLDS, (R600_ROQ_IB1_START(0x16) |
						R600_ROQ_IB2_START(0x2b)));

	RADEON_WRITE(R600_CP_MEQ_THRESHOLDS, (R600_MEQ_END(0x40) |
					      R600_ROQ_END(0x40)));

	RADEON_WRITE(R600_TA_CNTL_AUX, (R600_DISABLE_CUBE_ANISO |
					R600_SYNC_GRADIENT |
					R600_SYNC_WALKER |
					R600_SYNC_ALIGNER));

	if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV670)
		RADEON_WRITE(R600_ARB_GDEC_RD_CNTL, 0x00000021);

	sx_debug_1 = RADEON_READ(R600_SX_DEBUG_1);
	sx_debug_1 |= R600_SMX_EVENT_RELEASE;
	if (((dev_priv->flags & RADEON_FAMILY_MASK) > CHIP_R600))
		sx_debug_1 |= R600_ENABLE_NEW_SMX_ADDRESS;
	RADEON_WRITE(R600_SX_DEBUG_1, sx_debug_1);

	if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_R600) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV630) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV610) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV620) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS780) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS880))
		RADEON_WRITE(R600_DB_DEBUG, R600_PREZ_MUST_WAIT_FOR_POSTZ_DONE);
	else
		RADEON_WRITE(R600_DB_DEBUG, 0);

	RADEON_WRITE(R600_DB_WATERMARKS, (R600_DEPTH_FREE(4) |
					  R600_DEPTH_FLUSH(16) |
					  R600_DEPTH_PENDING_FREE(4) |
					  R600_DEPTH_CACHELINE_FREE(16)));
	RADEON_WRITE(R600_PA_SC_MULTI_CHIP_CNTL, 0);
	RADEON_WRITE(R600_VGT_NUM_INSTANCES, 0);

	RADEON_WRITE(R600_SPI_CONFIG_CNTL, R600_GPR_WRITE_PRIORITY(0));
	RADEON_WRITE(R600_SPI_CONFIG_CNTL_1, R600_VTX_DONE_DELAY(0));

	sq_ms_fifo_sizes = RADEON_READ(R600_SQ_MS_FIFO_SIZES);
	if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV610) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV620) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS780) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS880)) {
		sq_ms_fifo_sizes = (R600_CACHE_FIFO_SIZE(0xa) |
				    R600_FETCH_FIFO_HIWATER(0xa) |
				    R600_DONE_FIFO_HIWATER(0xe0) |
				    R600_ALU_UPDATE_FIFO_HIWATER(0x8));
	} else if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_R600) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV630)) {
		sq_ms_fifo_sizes &= ~R600_DONE_FIFO_HIWATER(0xff);
		sq_ms_fifo_sizes |= R600_DONE_FIFO_HIWATER(0x4);
	}
	RADEON_WRITE(R600_SQ_MS_FIFO_SIZES, sq_ms_fifo_sizes);

	/* SQ_CONFIG, SQ_GPR_RESOURCE_MGMT, SQ_THREAD_RESOURCE_MGMT, SQ_STACK_RESOURCE_MGMT
	 * should be adjusted as needed by the 2D/3D drivers.  This just sets default values
	 */
	sq_config = RADEON_READ(R600_SQ_CONFIG);
	sq_config &= ~(R600_PS_PRIO(3) |
		       R600_VS_PRIO(3) |
		       R600_GS_PRIO(3) |
		       R600_ES_PRIO(3));
	sq_config |= (R600_DX9_CONSTS |
		      R600_VC_ENABLE |
		      R600_PS_PRIO(0) |
		      R600_VS_PRIO(1) |
		      R600_GS_PRIO(2) |
		      R600_ES_PRIO(3));

	if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_R600) {
		sq_gpr_resource_mgmt_1 = (R600_NUM_PS_GPRS(124) |
					  R600_NUM_VS_GPRS(124) |
					  R600_NUM_CLAUSE_TEMP_GPRS(4));
		sq_gpr_resource_mgmt_2 = (R600_NUM_GS_GPRS(0) |
					  R600_NUM_ES_GPRS(0));
		sq_thread_resource_mgmt = (R600_NUM_PS_THREADS(136) |
					   R600_NUM_VS_THREADS(48) |
					   R600_NUM_GS_THREADS(4) |
					   R600_NUM_ES_THREADS(4));
		sq_stack_resource_mgmt_1 = (R600_NUM_PS_STACK_ENTRIES(128) |
					    R600_NUM_VS_STACK_ENTRIES(128));
		sq_stack_resource_mgmt_2 = (R600_NUM_GS_STACK_ENTRIES(0) |
					    R600_NUM_ES_STACK_ENTRIES(0));
	} else if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV610) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV620) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS780) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS880)) {
		/* no vertex cache */
		sq_config &= ~R600_VC_ENABLE;

		sq_gpr_resource_mgmt_1 = (R600_NUM_PS_GPRS(44) |
					  R600_NUM_VS_GPRS(44) |
					  R600_NUM_CLAUSE_TEMP_GPRS(2));
		sq_gpr_resource_mgmt_2 = (R600_NUM_GS_GPRS(17) |
					  R600_NUM_ES_GPRS(17));
		sq_thread_resource_mgmt = (R600_NUM_PS_THREADS(79) |
					   R600_NUM_VS_THREADS(78) |
					   R600_NUM_GS_THREADS(4) |
					   R600_NUM_ES_THREADS(31));
		sq_stack_resource_mgmt_1 = (R600_NUM_PS_STACK_ENTRIES(40) |
					    R600_NUM_VS_STACK_ENTRIES(40));
		sq_stack_resource_mgmt_2 = (R600_NUM_GS_STACK_ENTRIES(32) |
					    R600_NUM_ES_STACK_ENTRIES(16));
	} else if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV630) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV635)) {
		sq_gpr_resource_mgmt_1 = (R600_NUM_PS_GPRS(44) |
					  R600_NUM_VS_GPRS(44) |
					  R600_NUM_CLAUSE_TEMP_GPRS(2));
		sq_gpr_resource_mgmt_2 = (R600_NUM_GS_GPRS(18) |
					  R600_NUM_ES_GPRS(18));
		sq_thread_resource_mgmt = (R600_NUM_PS_THREADS(79) |
					   R600_NUM_VS_THREADS(78) |
					   R600_NUM_GS_THREADS(4) |
					   R600_NUM_ES_THREADS(31));
		sq_stack_resource_mgmt_1 = (R600_NUM_PS_STACK_ENTRIES(40) |
					    R600_NUM_VS_STACK_ENTRIES(40));
		sq_stack_resource_mgmt_2 = (R600_NUM_GS_STACK_ENTRIES(32) |
					    R600_NUM_ES_STACK_ENTRIES(16));
	} else if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV670) {
		sq_gpr_resource_mgmt_1 = (R600_NUM_PS_GPRS(44) |
					  R600_NUM_VS_GPRS(44) |
					  R600_NUM_CLAUSE_TEMP_GPRS(2));
		sq_gpr_resource_mgmt_2 = (R600_NUM_GS_GPRS(17) |
					  R600_NUM_ES_GPRS(17));
		sq_thread_resource_mgmt = (R600_NUM_PS_THREADS(79) |
					   R600_NUM_VS_THREADS(78) |
					   R600_NUM_GS_THREADS(4) |
					   R600_NUM_ES_THREADS(31));
		sq_stack_resource_mgmt_1 = (R600_NUM_PS_STACK_ENTRIES(64) |
					    R600_NUM_VS_STACK_ENTRIES(64));
		sq_stack_resource_mgmt_2 = (R600_NUM_GS_STACK_ENTRIES(64) |
					    R600_NUM_ES_STACK_ENTRIES(64));
	}

	RADEON_WRITE(R600_SQ_CONFIG, sq_config);
	RADEON_WRITE(R600_SQ_GPR_RESOURCE_MGMT_1,  sq_gpr_resource_mgmt_1);
	RADEON_WRITE(R600_SQ_GPR_RESOURCE_MGMT_2,  sq_gpr_resource_mgmt_2);
	RADEON_WRITE(R600_SQ_THREAD_RESOURCE_MGMT, sq_thread_resource_mgmt);
	RADEON_WRITE(R600_SQ_STACK_RESOURCE_MGMT_1, sq_stack_resource_mgmt_1);
	RADEON_WRITE(R600_SQ_STACK_RESOURCE_MGMT_2, sq_stack_resource_mgmt_2);

	if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV610) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV620) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS780) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS880))
		RADEON_WRITE(R600_VGT_CACHE_INVALIDATION, R600_CACHE_INVALIDATION(R600_TC_ONLY));
	else
		RADEON_WRITE(R600_VGT_CACHE_INVALIDATION, R600_CACHE_INVALIDATION(R600_VC_AND_TC));

	RADEON_WRITE(R600_PA_SC_AA_SAMPLE_LOCS_2S, (R600_S0_X(0xc) |
						    R600_S0_Y(0x4) |
						    R600_S1_X(0x4) |
						    R600_S1_Y(0xc)));
	RADEON_WRITE(R600_PA_SC_AA_SAMPLE_LOCS_4S, (R600_S0_X(0xe) |
						    R600_S0_Y(0xe) |
						    R600_S1_X(0x2) |
						    R600_S1_Y(0x2) |
						    R600_S2_X(0xa) |
						    R600_S2_Y(0x6) |
						    R600_S3_X(0x6) |
						    R600_S3_Y(0xa)));
	RADEON_WRITE(R600_PA_SC_AA_SAMPLE_LOCS_8S_WD0, (R600_S0_X(0xe) |
							R600_S0_Y(0xb) |
							R600_S1_X(0x4) |
							R600_S1_Y(0xc) |
							R600_S2_X(0x1) |
							R600_S2_Y(0x6) |
							R600_S3_X(0xa) |
							R600_S3_Y(0xe)));
	RADEON_WRITE(R600_PA_SC_AA_SAMPLE_LOCS_8S_WD1, (R600_S4_X(0x6) |
							R600_S4_Y(0x1) |
							R600_S5_X(0x0) |
							R600_S5_Y(0x0) |
							R600_S6_X(0xb) |
							R600_S6_Y(0x4) |
							R600_S7_X(0x7) |
							R600_S7_Y(0x8)));


	switch (dev_priv->flags & RADEON_FAMILY_MASK) {
	case CHIP_R600:
	case CHIP_RV630:
	case CHIP_RV635:
		gs_prim_buffer_depth = 0;
		break;
	case CHIP_RV610:
	case CHIP_RS780:
	case CHIP_RS880:
	case CHIP_RV620:
		gs_prim_buffer_depth = 32;
		break;
	case CHIP_RV670:
		gs_prim_buffer_depth = 128;
		break;
	default:
		break;
	}

	num_gs_verts_per_thread = dev_priv->r600_max_pipes * 16;
	vgt_gs_per_es = gs_prim_buffer_depth + num_gs_verts_per_thread;
	/* Max value for this is 256 */
	if (vgt_gs_per_es > 256)
		vgt_gs_per_es = 256;

	RADEON_WRITE(R600_VGT_ES_PER_GS, 128);
	RADEON_WRITE(R600_VGT_GS_PER_ES, vgt_gs_per_es);
	RADEON_WRITE(R600_VGT_GS_PER_VS, 2);
	RADEON_WRITE(R600_VGT_GS_VERTEX_REUSE, 16);

	/* more default values. 2D/3D driver should adjust as needed */
	RADEON_WRITE(R600_PA_SC_LINE_STIPPLE_STATE, 0);
	RADEON_WRITE(R600_VGT_STRMOUT_EN, 0);
	RADEON_WRITE(R600_SX_MISC, 0);
	RADEON_WRITE(R600_PA_SC_MODE_CNTL, 0);
	RADEON_WRITE(R600_PA_SC_AA_CONFIG, 0);
	RADEON_WRITE(R600_PA_SC_LINE_STIPPLE, 0);
	RADEON_WRITE(R600_SPI_INPUT_Z, 0);
	RADEON_WRITE(R600_SPI_PS_IN_CONTROL_0, R600_NUM_INTERP(2));
	RADEON_WRITE(R600_CB_COLOR7_FRAG, 0);

	/* clear render buffer base addresses */
	RADEON_WRITE(R600_CB_COLOR0_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR1_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR2_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR3_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR4_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR5_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR6_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR7_BASE, 0);

	switch (dev_priv->flags & RADEON_FAMILY_MASK) {
	case CHIP_RV610:
	case CHIP_RS780:
	case CHIP_RS880:
	case CHIP_RV620:
		tc_cntl = R600_TC_L2_SIZE(8);
		break;
	case CHIP_RV630:
	case CHIP_RV635:
		tc_cntl = R600_TC_L2_SIZE(4);
		break;
	case CHIP_R600:
		tc_cntl = R600_TC_L2_SIZE(0) | R600_L2_DISABLE_LATE_HIT;
		break;
	default:
		tc_cntl = R600_TC_L2_SIZE(0);
		break;
	}

	RADEON_WRITE(R600_TC_CNTL, tc_cntl);

	hdp_host_path_cntl = RADEON_READ(R600_HDP_HOST_PATH_CNTL);
	RADEON_WRITE(R600_HDP_HOST_PATH_CNTL, hdp_host_path_cntl);

	arb_pop = RADEON_READ(R600_ARB_POP);
	arb_pop |= R600_ENABLE_TC128;
	RADEON_WRITE(R600_ARB_POP, arb_pop);

	RADEON_WRITE(R600_PA_SC_MULTI_CHIP_CNTL, 0);
	RADEON_WRITE(R600_PA_CL_ENHANCE, (R600_CLIP_VTX_REORDER_ENA |
					  R600_NUM_CLIP_SEQ(3)));
	RADEON_WRITE(R600_PA_SC_ENHANCE, R600_FORCE_EOV_MAX_CLK_CNT(4095));

}

static u32 r700_get_tile_pipe_to_backend_map(drm_radeon_private_t *dev_priv,
					     u32 num_tile_pipes,
					     u32 num_backends,
					     u32 backend_disable_mask)
{
	u32 backend_map = 0;
	u32 enabled_backends_mask;
	u32 enabled_backends_count;
	u32 cur_pipe;
	u32 swizzle_pipe[R7XX_MAX_PIPES];
	u32 cur_backend;
	u32 i;
	bool force_no_swizzle;

	if (num_tile_pipes > R7XX_MAX_PIPES)
		num_tile_pipes = R7XX_MAX_PIPES;
	if (num_tile_pipes < 1)
		num_tile_pipes = 1;
	if (num_backends > R7XX_MAX_BACKENDS)
		num_backends = R7XX_MAX_BACKENDS;
	if (num_backends < 1)
		num_backends = 1;

	enabled_backends_mask = 0;
	enabled_backends_count = 0;
	for (i = 0; i < R7XX_MAX_BACKENDS; ++i) {
		if (((backend_disable_mask >> i) & 1) == 0) {
			enabled_backends_mask |= (1 << i);
			++enabled_backends_count;
		}
		if (enabled_backends_count == num_backends)
			break;
	}

	if (enabled_backends_count == 0) {
		enabled_backends_mask = 1;
		enabled_backends_count = 1;
	}

	if (enabled_backends_count != num_backends)
		num_backends = enabled_backends_count;

	switch (dev_priv->flags & RADEON_FAMILY_MASK) {
	case CHIP_RV770:
	case CHIP_RV730:
		force_no_swizzle = false;
		break;
	case CHIP_RV710:
	case CHIP_RV740:
	default:
		force_no_swizzle = true;
		break;
	}

	memset((uint8_t *)&swizzle_pipe[0], 0, sizeof(u32) * R7XX_MAX_PIPES);
	switch (num_tile_pipes) {
	case 1:
		swizzle_pipe[0] = 0;
		break;
	case 2:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 1;
		break;
	case 3:
		if (force_no_swizzle) {
			swizzle_pipe[0] = 0;
			swizzle_pipe[1] = 1;
			swizzle_pipe[2] = 2;
		} else {
			swizzle_pipe[0] = 0;
			swizzle_pipe[1] = 2;
			swizzle_pipe[2] = 1;
		}
		break;
	case 4:
		if (force_no_swizzle) {
			swizzle_pipe[0] = 0;
			swizzle_pipe[1] = 1;
			swizzle_pipe[2] = 2;
			swizzle_pipe[3] = 3;
		} else {
			swizzle_pipe[0] = 0;
			swizzle_pipe[1] = 2;
			swizzle_pipe[2] = 3;
			swizzle_pipe[3] = 1;
		}
		break;
	case 5:
		if (force_no_swizzle) {
			swizzle_pipe[0] = 0;
			swizzle_pipe[1] = 1;
			swizzle_pipe[2] = 2;
			swizzle_pipe[3] = 3;
			swizzle_pipe[4] = 4;
		} else {
			swizzle_pipe[0] = 0;
			swizzle_pipe[1] = 2;
			swizzle_pipe[2] = 4;
			swizzle_pipe[3] = 1;
			swizzle_pipe[4] = 3;
		}
		break;
	case 6:
		if (force_no_swizzle) {
			swizzle_pipe[0] = 0;
			swizzle_pipe[1] = 1;
			swizzle_pipe[2] = 2;
			swizzle_pipe[3] = 3;
			swizzle_pipe[4] = 4;
			swizzle_pipe[5] = 5;
		} else {
			swizzle_pipe[0] = 0;
			swizzle_pipe[1] = 2;
			swizzle_pipe[2] = 4;
			swizzle_pipe[3] = 5;
			swizzle_pipe[4] = 3;
			swizzle_pipe[5] = 1;
		}
		break;
	case 7:
		if (force_no_swizzle) {
			swizzle_pipe[0] = 0;
			swizzle_pipe[1] = 1;
			swizzle_pipe[2] = 2;
			swizzle_pipe[3] = 3;
			swizzle_pipe[4] = 4;
			swizzle_pipe[5] = 5;
			swizzle_pipe[6] = 6;
		} else {
			swizzle_pipe[0] = 0;
			swizzle_pipe[1] = 2;
			swizzle_pipe[2] = 4;
			swizzle_pipe[3] = 6;
			swizzle_pipe[4] = 3;
			swizzle_pipe[5] = 1;
			swizzle_pipe[6] = 5;
		}
		break;
	case 8:
		if (force_no_swizzle) {
			swizzle_pipe[0] = 0;
			swizzle_pipe[1] = 1;
			swizzle_pipe[2] = 2;
			swizzle_pipe[3] = 3;
			swizzle_pipe[4] = 4;
			swizzle_pipe[5] = 5;
			swizzle_pipe[6] = 6;
			swizzle_pipe[7] = 7;
		} else {
			swizzle_pipe[0] = 0;
			swizzle_pipe[1] = 2;
			swizzle_pipe[2] = 4;
			swizzle_pipe[3] = 6;
			swizzle_pipe[4] = 3;
			swizzle_pipe[5] = 1;
			swizzle_pipe[6] = 7;
			swizzle_pipe[7] = 5;
		}
		break;
	}

	cur_backend = 0;
	for (cur_pipe = 0; cur_pipe < num_tile_pipes; ++cur_pipe) {
		while (((1 << cur_backend) & enabled_backends_mask) == 0)
			cur_backend = (cur_backend + 1) % R7XX_MAX_BACKENDS;

		backend_map |= (u32)(((cur_backend & 3) << (swizzle_pipe[cur_pipe] * 2)));

		cur_backend = (cur_backend + 1) % R7XX_MAX_BACKENDS;
	}

	return backend_map;
}

static void r700_gfx_init(struct drm_device *dev,
			  drm_radeon_private_t *dev_priv)
{
	int i, j, num_qd_pipes;
	u32 ta_aux_cntl;
	u32 sx_debug_1;
	u32 smx_dc_ctl0;
	u32 db_debug3;
	u32 num_gs_verts_per_thread;
	u32 vgt_gs_per_es;
	u32 gs_prim_buffer_depth = 0;
	u32 sq_ms_fifo_sizes;
	u32 sq_config;
	u32 sq_thread_resource_mgmt;
	u32 hdp_host_path_cntl;
	u32 sq_dyn_gpr_size_simd_ab_0;
	u32 backend_map;
	u32 gb_tiling_config = 0;
	u32 cc_rb_backend_disable;
	u32 cc_gc_shader_pipe_config;
	u32 mc_arb_ramcfg;
	u32 db_debug4;

	/* setup chip specs */
	switch (dev_priv->flags & RADEON_FAMILY_MASK) {
	case CHIP_RV770:
		dev_priv->r600_max_pipes = 4;
		dev_priv->r600_max_tile_pipes = 8;
		dev_priv->r600_max_simds = 10;
		dev_priv->r600_max_backends = 4;
		dev_priv->r600_max_gprs = 256;
		dev_priv->r600_max_threads = 248;
		dev_priv->r600_max_stack_entries = 512;
		dev_priv->r600_max_hw_contexts = 8;
		dev_priv->r600_max_gs_threads = 16 * 2;
		dev_priv->r600_sx_max_export_size = 128;
		dev_priv->r600_sx_max_export_pos_size = 16;
		dev_priv->r600_sx_max_export_smx_size = 112;
		dev_priv->r600_sq_num_cf_insts = 2;

		dev_priv->r700_sx_num_of_sets = 7;
		dev_priv->r700_sc_prim_fifo_size = 0xF9;
		dev_priv->r700_sc_hiz_tile_fifo_size = 0x30;
		dev_priv->r700_sc_earlyz_tile_fifo_fize = 0x130;
		break;
	case CHIP_RV730:
		dev_priv->r600_max_pipes = 2;
		dev_priv->r600_max_tile_pipes = 4;
		dev_priv->r600_max_simds = 8;
		dev_priv->r600_max_backends = 2;
		dev_priv->r600_max_gprs = 128;
		dev_priv->r600_max_threads = 248;
		dev_priv->r600_max_stack_entries = 256;
		dev_priv->r600_max_hw_contexts = 8;
		dev_priv->r600_max_gs_threads = 16 * 2;
		dev_priv->r600_sx_max_export_size = 256;
		dev_priv->r600_sx_max_export_pos_size = 32;
		dev_priv->r600_sx_max_export_smx_size = 224;
		dev_priv->r600_sq_num_cf_insts = 2;

		dev_priv->r700_sx_num_of_sets = 7;
		dev_priv->r700_sc_prim_fifo_size = 0xf9;
		dev_priv->r700_sc_hiz_tile_fifo_size = 0x30;
		dev_priv->r700_sc_earlyz_tile_fifo_fize = 0x130;
		if (dev_priv->r600_sx_max_export_pos_size > 16) {
			dev_priv->r600_sx_max_export_pos_size -= 16;
			dev_priv->r600_sx_max_export_smx_size += 16;
		}
		break;
	case CHIP_RV710:
		dev_priv->r600_max_pipes = 2;
		dev_priv->r600_max_tile_pipes = 2;
		dev_priv->r600_max_simds = 2;
		dev_priv->r600_max_backends = 1;
		dev_priv->r600_max_gprs = 256;
		dev_priv->r600_max_threads = 192;
		dev_priv->r600_max_stack_entries = 256;
		dev_priv->r600_max_hw_contexts = 4;
		dev_priv->r600_max_gs_threads = 8 * 2;
		dev_priv->r600_sx_max_export_size = 128;
		dev_priv->r600_sx_max_export_pos_size = 16;
		dev_priv->r600_sx_max_export_smx_size = 112;
		dev_priv->r600_sq_num_cf_insts = 1;

		dev_priv->r700_sx_num_of_sets = 7;
		dev_priv->r700_sc_prim_fifo_size = 0x40;
		dev_priv->r700_sc_hiz_tile_fifo_size = 0x30;
		dev_priv->r700_sc_earlyz_tile_fifo_fize = 0x130;
		break;
	case CHIP_RV740:
		dev_priv->r600_max_pipes = 4;
		dev_priv->r600_max_tile_pipes = 4;
		dev_priv->r600_max_simds = 8;
		dev_priv->r600_max_backends = 4;
		dev_priv->r600_max_gprs = 256;
		dev_priv->r600_max_threads = 248;
		dev_priv->r600_max_stack_entries = 512;
		dev_priv->r600_max_hw_contexts = 8;
		dev_priv->r600_max_gs_threads = 16 * 2;
		dev_priv->r600_sx_max_export_size = 256;
		dev_priv->r600_sx_max_export_pos_size = 32;
		dev_priv->r600_sx_max_export_smx_size = 224;
		dev_priv->r600_sq_num_cf_insts = 2;

		dev_priv->r700_sx_num_of_sets = 7;
		dev_priv->r700_sc_prim_fifo_size = 0x100;
		dev_priv->r700_sc_hiz_tile_fifo_size = 0x30;
		dev_priv->r700_sc_earlyz_tile_fifo_fize = 0x130;

		if (dev_priv->r600_sx_max_export_pos_size > 16) {
			dev_priv->r600_sx_max_export_pos_size -= 16;
			dev_priv->r600_sx_max_export_smx_size += 16;
		}
		break;
	default:
		break;
	}

	/* Initialize HDP */
	j = 0;
	for (i = 0; i < 32; i++) {
		RADEON_WRITE((0x2c14 + j), 0x00000000);
		RADEON_WRITE((0x2c18 + j), 0x00000000);
		RADEON_WRITE((0x2c1c + j), 0x00000000);
		RADEON_WRITE((0x2c20 + j), 0x00000000);
		RADEON_WRITE((0x2c24 + j), 0x00000000);
		j += 0x18;
	}

	RADEON_WRITE(R600_GRBM_CNTL, R600_GRBM_READ_TIMEOUT(0xff));

	/* setup tiling, simd, pipe config */
	mc_arb_ramcfg = RADEON_READ(R700_MC_ARB_RAMCFG);

	switch (dev_priv->r600_max_tile_pipes) {
	case 1:
		gb_tiling_config |= R600_PIPE_TILING(0);
		break;
	case 2:
		gb_tiling_config |= R600_PIPE_TILING(1);
		break;
	case 4:
		gb_tiling_config |= R600_PIPE_TILING(2);
		break;
	case 8:
		gb_tiling_config |= R600_PIPE_TILING(3);
		break;
	default:
		break;
	}

	if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV770)
		gb_tiling_config |= R600_BANK_TILING(1);
	else
		gb_tiling_config |= R600_BANK_TILING((mc_arb_ramcfg >> R700_NOOFBANK_SHIFT) & R700_NOOFBANK_MASK);

	gb_tiling_config |= R600_GROUP_SIZE(0);

	if (((mc_arb_ramcfg >> R700_NOOFROWS_SHIFT) & R700_NOOFROWS_MASK) > 3) {
		gb_tiling_config |= R600_ROW_TILING(3);
		gb_tiling_config |= R600_SAMPLE_SPLIT(3);
	} else {
		gb_tiling_config |=
			R600_ROW_TILING(((mc_arb_ramcfg >> R700_NOOFROWS_SHIFT) & R700_NOOFROWS_MASK));
		gb_tiling_config |=
			R600_SAMPLE_SPLIT(((mc_arb_ramcfg >> R700_NOOFROWS_SHIFT) & R700_NOOFROWS_MASK));
	}

	gb_tiling_config |= R600_BANK_SWAPS(1);

	cc_rb_backend_disable = RADEON_READ(R600_CC_RB_BACKEND_DISABLE) & 0x00ff0000;
	cc_rb_backend_disable |=
		R600_BACKEND_DISABLE((R7XX_MAX_BACKENDS_MASK << dev_priv->r600_max_backends) & R7XX_MAX_BACKENDS_MASK);

	cc_gc_shader_pipe_config = RADEON_READ(R600_CC_GC_SHADER_PIPE_CONFIG) & 0xffffff00;
	cc_gc_shader_pipe_config |=
		R600_INACTIVE_QD_PIPES((R7XX_MAX_PIPES_MASK << dev_priv->r600_max_pipes) & R7XX_MAX_PIPES_MASK);
	cc_gc_shader_pipe_config |=
		R600_INACTIVE_SIMDS((R7XX_MAX_SIMDS_MASK << dev_priv->r600_max_simds) & R7XX_MAX_SIMDS_MASK);

	if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV740)
		backend_map = 0x28;
	else
		backend_map = r700_get_tile_pipe_to_backend_map(dev_priv,
								dev_priv->r600_max_tile_pipes,
								(R7XX_MAX_BACKENDS -
								 r600_count_pipe_bits((cc_rb_backend_disable &
										       R7XX_MAX_BACKENDS_MASK) >> 16)),
								(cc_rb_backend_disable >> 16));
	gb_tiling_config |= R600_BACKEND_MAP(backend_map);

	RADEON_WRITE(R600_GB_TILING_CONFIG,      gb_tiling_config);
	RADEON_WRITE(R600_DCP_TILING_CONFIG,    (gb_tiling_config & 0xffff));
	RADEON_WRITE(R600_HDP_TILING_CONFIG,    (gb_tiling_config & 0xffff));
	if (gb_tiling_config & 0xc0) {
		dev_priv->r600_group_size = 512;
	} else {
		dev_priv->r600_group_size = 256;
	}
	dev_priv->r600_npipes = 1 << ((gb_tiling_config >> 1) & 0x7);
	if (gb_tiling_config & 0x30) {
		dev_priv->r600_nbanks = 8;
	} else {
		dev_priv->r600_nbanks = 4;
	}

	RADEON_WRITE(R600_CC_RB_BACKEND_DISABLE,      cc_rb_backend_disable);
	RADEON_WRITE(R600_CC_GC_SHADER_PIPE_CONFIG,   cc_gc_shader_pipe_config);
	RADEON_WRITE(R600_GC_USER_SHADER_PIPE_CONFIG, cc_gc_shader_pipe_config);

	RADEON_WRITE(R700_CC_SYS_RB_BACKEND_DISABLE, cc_rb_backend_disable);
	RADEON_WRITE(R700_CGTS_SYS_TCC_DISABLE, 0);
	RADEON_WRITE(R700_CGTS_TCC_DISABLE, 0);
	RADEON_WRITE(R700_CGTS_USER_SYS_TCC_DISABLE, 0);
	RADEON_WRITE(R700_CGTS_USER_TCC_DISABLE, 0);

	num_qd_pipes =
		R7XX_MAX_PIPES - r600_count_pipe_bits((cc_gc_shader_pipe_config & R600_INACTIVE_QD_PIPES_MASK) >> 8);
	RADEON_WRITE(R600_VGT_OUT_DEALLOC_CNTL, (num_qd_pipes * 4) & R600_DEALLOC_DIST_MASK);
	RADEON_WRITE(R600_VGT_VERTEX_REUSE_BLOCK_CNTL, ((num_qd_pipes * 4) - 2) & R600_VTX_REUSE_DEPTH_MASK);

	/* set HW defaults for 3D engine */
	RADEON_WRITE(R600_CP_QUEUE_THRESHOLDS, (R600_ROQ_IB1_START(0x16) |
						R600_ROQ_IB2_START(0x2b)));

	RADEON_WRITE(R600_CP_MEQ_THRESHOLDS, R700_STQ_SPLIT(0x30));

	ta_aux_cntl = RADEON_READ(R600_TA_CNTL_AUX);
	RADEON_WRITE(R600_TA_CNTL_AUX, ta_aux_cntl | R600_DISABLE_CUBE_ANISO);

	sx_debug_1 = RADEON_READ(R700_SX_DEBUG_1);
	sx_debug_1 |= R700_ENABLE_NEW_SMX_ADDRESS;
	RADEON_WRITE(R700_SX_DEBUG_1, sx_debug_1);

	smx_dc_ctl0 = RADEON_READ(R600_SMX_DC_CTL0);
	smx_dc_ctl0 &= ~R700_CACHE_DEPTH(0x1ff);
	smx_dc_ctl0 |= R700_CACHE_DEPTH((dev_priv->r700_sx_num_of_sets * 64) - 1);
	RADEON_WRITE(R600_SMX_DC_CTL0, smx_dc_ctl0);

	if ((dev_priv->flags & RADEON_FAMILY_MASK) != CHIP_RV740)
		RADEON_WRITE(R700_SMX_EVENT_CTL, (R700_ES_FLUSH_CTL(4) |
						  R700_GS_FLUSH_CTL(4) |
						  R700_ACK_FLUSH_CTL(3) |
						  R700_SYNC_FLUSH_CTL));

	db_debug3 = RADEON_READ(R700_DB_DEBUG3);
	db_debug3 &= ~R700_DB_CLK_OFF_DELAY(0x1f);
	switch (dev_priv->flags & RADEON_FAMILY_MASK) {
	case CHIP_RV770:
	case CHIP_RV740:
		db_debug3 |= R700_DB_CLK_OFF_DELAY(0x1f);
		break;
	case CHIP_RV710:
	case CHIP_RV730:
	default:
		db_debug3 |= R700_DB_CLK_OFF_DELAY(2);
		break;
	}
	RADEON_WRITE(R700_DB_DEBUG3, db_debug3);

	if ((dev_priv->flags & RADEON_FAMILY_MASK) != CHIP_RV770) {
		db_debug4 = RADEON_READ(RV700_DB_DEBUG4);
		db_debug4 |= RV700_DISABLE_TILE_COVERED_FOR_PS_ITER;
		RADEON_WRITE(RV700_DB_DEBUG4, db_debug4);
	}

	RADEON_WRITE(R600_SX_EXPORT_BUFFER_SIZES, (R600_COLOR_BUFFER_SIZE((dev_priv->r600_sx_max_export_size / 4) - 1) |
						   R600_POSITION_BUFFER_SIZE((dev_priv->r600_sx_max_export_pos_size / 4) - 1) |
						   R600_SMX_BUFFER_SIZE((dev_priv->r600_sx_max_export_smx_size / 4) - 1)));

	RADEON_WRITE(R700_PA_SC_FIFO_SIZE_R7XX, (R700_SC_PRIM_FIFO_SIZE(dev_priv->r700_sc_prim_fifo_size) |
						 R700_SC_HIZ_TILE_FIFO_SIZE(dev_priv->r700_sc_hiz_tile_fifo_size) |
						 R700_SC_EARLYZ_TILE_FIFO_SIZE(dev_priv->r700_sc_earlyz_tile_fifo_fize)));

	RADEON_WRITE(R600_PA_SC_MULTI_CHIP_CNTL, 0);

	RADEON_WRITE(R600_VGT_NUM_INSTANCES, 1);

	RADEON_WRITE(R600_SPI_CONFIG_CNTL, R600_GPR_WRITE_PRIORITY(0));

	RADEON_WRITE(R600_SPI_CONFIG_CNTL_1, R600_VTX_DONE_DELAY(4));

	RADEON_WRITE(R600_CP_PERFMON_CNTL, 0);

	sq_ms_fifo_sizes = (R600_CACHE_FIFO_SIZE(16 * dev_priv->r600_sq_num_cf_insts) |
			    R600_DONE_FIFO_HIWATER(0xe0) |
			    R600_ALU_UPDATE_FIFO_HIWATER(0x8));
	switch (dev_priv->flags & RADEON_FAMILY_MASK) {
	case CHIP_RV770:
	case CHIP_RV730:
	case CHIP_RV710:
		sq_ms_fifo_sizes |= R600_FETCH_FIFO_HIWATER(0x1);
		break;
	case CHIP_RV740:
	default:
		sq_ms_fifo_sizes |= R600_FETCH_FIFO_HIWATER(0x4);
		break;
	}
	RADEON_WRITE(R600_SQ_MS_FIFO_SIZES, sq_ms_fifo_sizes);

	/* SQ_CONFIG, SQ_GPR_RESOURCE_MGMT, SQ_THREAD_RESOURCE_MGMT, SQ_STACK_RESOURCE_MGMT
	 * should be adjusted as needed by the 2D/3D drivers.  This just sets default values
	 */
	sq_config = RADEON_READ(R600_SQ_CONFIG);
	sq_config &= ~(R600_PS_PRIO(3) |
		       R600_VS_PRIO(3) |
		       R600_GS_PRIO(3) |
		       R600_ES_PRIO(3));
	sq_config |= (R600_DX9_CONSTS |
		      R600_VC_ENABLE |
		      R600_EXPORT_SRC_C |
		      R600_PS_PRIO(0) |
		      R600_VS_PRIO(1) |
		      R600_GS_PRIO(2) |
		      R600_ES_PRIO(3));
	if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV710)
		/* no vertex cache */
		sq_config &= ~R600_VC_ENABLE;

	RADEON_WRITE(R600_SQ_CONFIG, sq_config);

	RADEON_WRITE(R600_SQ_GPR_RESOURCE_MGMT_1,  (R600_NUM_PS_GPRS((dev_priv->r600_max_gprs * 24)/64) |
						    R600_NUM_VS_GPRS((dev_priv->r600_max_gprs * 24)/64) |
						    R600_NUM_CLAUSE_TEMP_GPRS(((dev_priv->r600_max_gprs * 24)/64)/2)));

	RADEON_WRITE(R600_SQ_GPR_RESOURCE_MGMT_2,  (R600_NUM_GS_GPRS((dev_priv->r600_max_gprs * 7)/64) |
						    R600_NUM_ES_GPRS((dev_priv->r600_max_gprs * 7)/64)));

	sq_thread_resource_mgmt = (R600_NUM_PS_THREADS((dev_priv->r600_max_threads * 4)/8) |
				   R600_NUM_VS_THREADS((dev_priv->r600_max_threads * 2)/8) |
				   R600_NUM_ES_THREADS((dev_priv->r600_max_threads * 1)/8));
	if (((dev_priv->r600_max_threads * 1) / 8) > dev_priv->r600_max_gs_threads)
		sq_thread_resource_mgmt |= R600_NUM_GS_THREADS(dev_priv->r600_max_gs_threads);
	else
		sq_thread_resource_mgmt |= R600_NUM_GS_THREADS((dev_priv->r600_max_gs_threads * 1)/8);
	RADEON_WRITE(R600_SQ_THREAD_RESOURCE_MGMT, sq_thread_resource_mgmt);

	RADEON_WRITE(R600_SQ_STACK_RESOURCE_MGMT_1, (R600_NUM_PS_STACK_ENTRIES((dev_priv->r600_max_stack_entries * 1)/4) |
						     R600_NUM_VS_STACK_ENTRIES((dev_priv->r600_max_stack_entries * 1)/4)));

	RADEON_WRITE(R600_SQ_STACK_RESOURCE_MGMT_2, (R600_NUM_GS_STACK_ENTRIES((dev_priv->r600_max_stack_entries * 1)/4) |
						     R600_NUM_ES_STACK_ENTRIES((dev_priv->r600_max_stack_entries * 1)/4)));

	sq_dyn_gpr_size_simd_ab_0 = (R700_SIMDA_RING0((dev_priv->r600_max_gprs * 38)/64) |
				     R700_SIMDA_RING1((dev_priv->r600_max_gprs * 38)/64) |
				     R700_SIMDB_RING0((dev_priv->r600_max_gprs * 38)/64) |
				     R700_SIMDB_RING1((dev_priv->r600_max_gprs * 38)/64));

	RADEON_WRITE(R700_SQ_DYN_GPR_SIZE_SIMD_AB_0, sq_dyn_gpr_size_simd_ab_0);
	RADEON_WRITE(R700_SQ_DYN_GPR_SIZE_SIMD_AB_1, sq_dyn_gpr_size_simd_ab_0);
	RADEON_WRITE(R700_SQ_DYN_GPR_SIZE_SIMD_AB_2, sq_dyn_gpr_size_simd_ab_0);
	RADEON_WRITE(R700_SQ_DYN_GPR_SIZE_SIMD_AB_3, sq_dyn_gpr_size_simd_ab_0);
	RADEON_WRITE(R700_SQ_DYN_GPR_SIZE_SIMD_AB_4, sq_dyn_gpr_size_simd_ab_0);
	RADEON_WRITE(R700_SQ_DYN_GPR_SIZE_SIMD_AB_5, sq_dyn_gpr_size_simd_ab_0);
	RADEON_WRITE(R700_SQ_DYN_GPR_SIZE_SIMD_AB_6, sq_dyn_gpr_size_simd_ab_0);
	RADEON_WRITE(R700_SQ_DYN_GPR_SIZE_SIMD_AB_7, sq_dyn_gpr_size_simd_ab_0);

	RADEON_WRITE(R700_PA_SC_FORCE_EOV_MAX_CNTS, (R700_FORCE_EOV_MAX_CLK_CNT(4095) |
						     R700_FORCE_EOV_MAX_REZ_CNT(255)));

	if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV710)
		RADEON_WRITE(R600_VGT_CACHE_INVALIDATION, (R600_CACHE_INVALIDATION(R600_TC_ONLY) |
							   R700_AUTO_INVLD_EN(R700_ES_AND_GS_AUTO)));
	else
		RADEON_WRITE(R600_VGT_CACHE_INVALIDATION, (R600_CACHE_INVALIDATION(R600_VC_AND_TC) |
							   R700_AUTO_INVLD_EN(R700_ES_AND_GS_AUTO)));

	switch (dev_priv->flags & RADEON_FAMILY_MASK) {
	case CHIP_RV770:
	case CHIP_RV730:
	case CHIP_RV740:
		gs_prim_buffer_depth = 384;
		break;
	case CHIP_RV710:
		gs_prim_buffer_depth = 128;
		break;
	default:
		break;
	}

	num_gs_verts_per_thread = dev_priv->r600_max_pipes * 16;
	vgt_gs_per_es = gs_prim_buffer_depth + num_gs_verts_per_thread;
	/* Max value for this is 256 */
	if (vgt_gs_per_es > 256)
		vgt_gs_per_es = 256;

	RADEON_WRITE(R600_VGT_ES_PER_GS, 128);
	RADEON_WRITE(R600_VGT_GS_PER_ES, vgt_gs_per_es);
	RADEON_WRITE(R600_VGT_GS_PER_VS, 2);

	/* more default values. 2D/3D driver should adjust as needed */
	RADEON_WRITE(R600_VGT_GS_VERTEX_REUSE, 16);
	RADEON_WRITE(R600_PA_SC_LINE_STIPPLE_STATE, 0);
	RADEON_WRITE(R600_VGT_STRMOUT_EN, 0);
	RADEON_WRITE(R600_SX_MISC, 0);
	RADEON_WRITE(R600_PA_SC_MODE_CNTL, 0);
	RADEON_WRITE(R700_PA_SC_EDGERULE, 0xaaaaaaaa);
	RADEON_WRITE(R600_PA_SC_AA_CONFIG, 0);
	RADEON_WRITE(R600_PA_SC_CLIPRECT_RULE, 0xffff);
	RADEON_WRITE(R600_PA_SC_LINE_STIPPLE, 0);
	RADEON_WRITE(R600_SPI_INPUT_Z, 0);
	RADEON_WRITE(R600_SPI_PS_IN_CONTROL_0, R600_NUM_INTERP(2));
	RADEON_WRITE(R600_CB_COLOR7_FRAG, 0);

	/* clear render buffer base addresses */
	RADEON_WRITE(R600_CB_COLOR0_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR1_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR2_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR3_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR4_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR5_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR6_BASE, 0);
	RADEON_WRITE(R600_CB_COLOR7_BASE, 0);

	RADEON_WRITE(R700_TCP_CNTL, 0);

	hdp_host_path_cntl = RADEON_READ(R600_HDP_HOST_PATH_CNTL);
	RADEON_WRITE(R600_HDP_HOST_PATH_CNTL, hdp_host_path_cntl);

	RADEON_WRITE(R600_PA_SC_MULTI_CHIP_CNTL, 0);

	RADEON_WRITE(R600_PA_CL_ENHANCE, (R600_CLIP_VTX_REORDER_ENA |
					  R600_NUM_CLIP_SEQ(3)));

}

static void r600_cp_init_ring_buffer(struct drm_device *dev,
				       drm_radeon_private_t *dev_priv,
				       struct drm_file *file_priv)
{
	struct drm_radeon_master_private *master_priv;
	u32 ring_start;
	u64 rptr_addr;

	if (((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RV770))
		r700_gfx_init(dev, dev_priv);
	else
		r600_gfx_init(dev, dev_priv);

	RADEON_WRITE(R600_GRBM_SOFT_RESET, R600_SOFT_RESET_CP);
	RADEON_READ(R600_GRBM_SOFT_RESET);
	DRM_UDELAY(15000);
	RADEON_WRITE(R600_GRBM_SOFT_RESET, 0);


	/* Set ring buffer size */
#ifdef __BIG_ENDIAN
	RADEON_WRITE(R600_CP_RB_CNTL,
		     RADEON_BUF_SWAP_32BIT |
		     RADEON_RB_NO_UPDATE |
		     (dev_priv->ring.rptr_update_l2qw << 8) |
		     dev_priv->ring.size_l2qw);
#else
	RADEON_WRITE(R600_CP_RB_CNTL,
		     RADEON_RB_NO_UPDATE |
		     (dev_priv->ring.rptr_update_l2qw << 8) |
		     dev_priv->ring.size_l2qw);
#endif

	RADEON_WRITE(R600_CP_SEM_WAIT_TIMER, 0x4);

	/* Set the write pointer delay */
	RADEON_WRITE(R600_CP_RB_WPTR_DELAY, 0);

#ifdef __BIG_ENDIAN
	RADEON_WRITE(R600_CP_RB_CNTL,
		     RADEON_BUF_SWAP_32BIT |
		     RADEON_RB_NO_UPDATE |
		     RADEON_RB_RPTR_WR_ENA |
		     (dev_priv->ring.rptr_update_l2qw << 8) |
		     dev_priv->ring.size_l2qw);
#else
	RADEON_WRITE(R600_CP_RB_CNTL,
		     RADEON_RB_NO_UPDATE |
		     RADEON_RB_RPTR_WR_ENA |
		     (dev_priv->ring.rptr_update_l2qw << 8) |
		     dev_priv->ring.size_l2qw);
#endif

	/* Initialize the ring buffer's read and write pointers */
	RADEON_WRITE(R600_CP_RB_RPTR_WR, 0);
	RADEON_WRITE(R600_CP_RB_WPTR, 0);
	SET_RING_HEAD(dev_priv, 0);
	dev_priv->ring.tail = 0;

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		rptr_addr = dev_priv->ring_rptr->offset
			- dev->agp->base +
			dev_priv->gart_vm_start;
	} else
#endif
	{
		rptr_addr = dev_priv->ring_rptr->offset
			- ((unsigned long) dev->sg->virtual)
			+ dev_priv->gart_vm_start;
	}
	RADEON_WRITE(R600_CP_RB_RPTR_ADDR,
#ifdef __BIG_ENDIAN
		     (2 << 0) |
#endif
		     (rptr_addr & 0xfffffffc));
	RADEON_WRITE(R600_CP_RB_RPTR_ADDR_HI,
		     upper_32_bits(rptr_addr));

#ifdef __BIG_ENDIAN
	RADEON_WRITE(R600_CP_RB_CNTL,
		     RADEON_BUF_SWAP_32BIT |
		     (dev_priv->ring.rptr_update_l2qw << 8) |
		     dev_priv->ring.size_l2qw);
#else
	RADEON_WRITE(R600_CP_RB_CNTL,
		     (dev_priv->ring.rptr_update_l2qw << 8) |
		     dev_priv->ring.size_l2qw);
#endif

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		/* XXX */
		radeon_write_agp_base(dev_priv, dev->agp->base);

		/* XXX */
		radeon_write_agp_location(dev_priv,
			     (((dev_priv->gart_vm_start - 1 +
				dev_priv->gart_size) & 0xffff0000) |
			      (dev_priv->gart_vm_start >> 16)));

		ring_start = (dev_priv->cp_ring->offset
			      - dev->agp->base
			      + dev_priv->gart_vm_start);
	} else
#endif
		ring_start = (dev_priv->cp_ring->offset
			      - (unsigned long)dev->sg->virtual
			      + dev_priv->gart_vm_start);

	RADEON_WRITE(R600_CP_RB_BASE, ring_start >> 8);

	RADEON_WRITE(R600_CP_ME_CNTL, 0xff);

	RADEON_WRITE(R600_CP_DEBUG, (1 << 27) | (1 << 28));

	/* Initialize the scratch register pointer.  This will cause
	 * the scratch register values to be written out to memory
	 * whenever they are updated.
	 *
	 * We simply put this behind the ring read pointer, this works
	 * with PCI GART as well as (whatever kind of) AGP GART
	 */
	{
		u64 scratch_addr;

		scratch_addr = RADEON_READ(R600_CP_RB_RPTR_ADDR) & 0xFFFFFFFC;
		scratch_addr |= ((u64)RADEON_READ(R600_CP_RB_RPTR_ADDR_HI)) << 32;
		scratch_addr += R600_SCRATCH_REG_OFFSET;
		scratch_addr >>= 8;
		scratch_addr &= 0xffffffff;

		RADEON_WRITE(R600_SCRATCH_ADDR, (uint32_t)scratch_addr);
	}

	RADEON_WRITE(R600_SCRATCH_UMSK, 0x7);

	/* Turn on bus mastering */
	radeon_enable_bm(dev_priv);

	radeon_write_ring_rptr(dev_priv, R600_SCRATCHOFF(0), 0);
	RADEON_WRITE(R600_LAST_FRAME_REG, 0);

	radeon_write_ring_rptr(dev_priv, R600_SCRATCHOFF(1), 0);
	RADEON_WRITE(R600_LAST_DISPATCH_REG, 0);

	radeon_write_ring_rptr(dev_priv, R600_SCRATCHOFF(2), 0);
	RADEON_WRITE(R600_LAST_CLEAR_REG, 0);

	/* reset sarea copies of these */
	master_priv = file_priv->master->driver_priv;
	if (master_priv->sarea_priv) {
		master_priv->sarea_priv->last_frame = 0;
		master_priv->sarea_priv->last_dispatch = 0;
		master_priv->sarea_priv->last_clear = 0;
	}

	r600_do_wait_for_idle(dev_priv);

}

int r600_do_cleanup_cp(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG("\n");

	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq_enabled)
		drm_irq_uninstall(dev);

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		if (dev_priv->cp_ring != NULL) {
			drm_core_ioremapfree(dev_priv->cp_ring, dev);
			dev_priv->cp_ring = NULL;
		}
		if (dev_priv->ring_rptr != NULL) {
			drm_core_ioremapfree(dev_priv->ring_rptr, dev);
			dev_priv->ring_rptr = NULL;
		}
		if (dev->agp_buffer_map != NULL) {
			drm_core_ioremapfree(dev->agp_buffer_map, dev);
			dev->agp_buffer_map = NULL;
		}
	} else
#endif
	{

		if (dev_priv->gart_info.bus_addr)
			r600_page_table_cleanup(dev, &dev_priv->gart_info);

		if (dev_priv->gart_info.gart_table_location == DRM_ATI_GART_FB) {
			drm_core_ioremapfree(&dev_priv->gart_info.mapping, dev);
			dev_priv->gart_info.addr = NULL;
		}
	}
	/* only clear to the start of flags */
	memset(dev_priv, 0, offsetof(drm_radeon_private_t, flags));

	return 0;
}

int r600_do_init_cp(struct drm_device *dev, drm_radeon_init_t *init,
		    struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	struct drm_radeon_master_private *master_priv = file_priv->master->driver_priv;

	DRM_DEBUG("\n");

	mutex_init(&dev_priv->cs_mutex);
	r600_cs_legacy_init();
	/* if we require new memory map but we don't have it fail */
	if ((dev_priv->flags & RADEON_NEW_MEMMAP) && !dev_priv->new_memmap) {
		DRM_ERROR("Cannot initialise DRM on this card\nThis card requires a new X.org DDX for 3D\n");
		r600_do_cleanup_cp(dev);
		return -EINVAL;
	}

	if (init->is_pci && (dev_priv->flags & RADEON_IS_AGP)) {
		DRM_DEBUG("Forcing AGP card to PCI mode\n");
		dev_priv->flags &= ~RADEON_IS_AGP;
		/* The writeback test succeeds, but when writeback is enabled,
		 * the ring buffer read ptr update fails after first 128 bytes.
		 */
		radeon_no_wb = 1;
	} else if (!(dev_priv->flags & (RADEON_IS_AGP | RADEON_IS_PCI | RADEON_IS_PCIE))
		 && !init->is_pci) {
		DRM_DEBUG("Restoring AGP flag\n");
		dev_priv->flags |= RADEON_IS_AGP;
	}

	dev_priv->usec_timeout = init->usec_timeout;
	if (dev_priv->usec_timeout < 1 ||
	    dev_priv->usec_timeout > RADEON_MAX_USEC_TIMEOUT) {
		DRM_DEBUG("TIMEOUT problem!\n");
		r600_do_cleanup_cp(dev);
		return -EINVAL;
	}

	/* Enable vblank on CRTC1 for older X servers
	 */
	dev_priv->vblank_crtc = DRM_RADEON_VBLANK_CRTC1;
	dev_priv->do_boxes = 0;
	dev_priv->cp_mode = init->cp_mode;

	/* We don't support anything other than bus-mastering ring mode,
	 * but the ring can be in either AGP or PCI space for the ring
	 * read pointer.
	 */
	if ((init->cp_mode != RADEON_CSQ_PRIBM_INDDIS) &&
	    (init->cp_mode != RADEON_CSQ_PRIBM_INDBM)) {
		DRM_DEBUG("BAD cp_mode (%x)!\n", init->cp_mode);
		r600_do_cleanup_cp(dev);
		return -EINVAL;
	}

	switch (init->fb_bpp) {
	case 16:
		dev_priv->color_fmt = RADEON_COLOR_FORMAT_RGB565;
		break;
	case 32:
	default:
		dev_priv->color_fmt = RADEON_COLOR_FORMAT_ARGB8888;
		break;
	}
	dev_priv->front_offset = init->front_offset;
	dev_priv->front_pitch = init->front_pitch;
	dev_priv->back_offset = init->back_offset;
	dev_priv->back_pitch = init->back_pitch;

	dev_priv->ring_offset = init->ring_offset;
	dev_priv->ring_rptr_offset = init->ring_rptr_offset;
	dev_priv->buffers_offset = init->buffers_offset;
	dev_priv->gart_textures_offset = init->gart_textures_offset;

	master_priv->sarea = drm_getsarea(dev);
	if (!master_priv->sarea) {
		DRM_ERROR("could not find sarea!\n");
		r600_do_cleanup_cp(dev);
		return -EINVAL;
	}

	dev_priv->cp_ring = drm_core_findmap(dev, init->ring_offset);
	if (!dev_priv->cp_ring) {
		DRM_ERROR("could not find cp ring region!\n");
		r600_do_cleanup_cp(dev);
		return -EINVAL;
	}
	dev_priv->ring_rptr = drm_core_findmap(dev, init->ring_rptr_offset);
	if (!dev_priv->ring_rptr) {
		DRM_ERROR("could not find ring read pointer!\n");
		r600_do_cleanup_cp(dev);
		return -EINVAL;
	}
	dev->agp_buffer_token = init->buffers_offset;
	dev->agp_buffer_map = drm_core_findmap(dev, init->buffers_offset);
	if (!dev->agp_buffer_map) {
		DRM_ERROR("could not find dma buffer region!\n");
		r600_do_cleanup_cp(dev);
		return -EINVAL;
	}

	if (init->gart_textures_offset) {
		dev_priv->gart_textures =
		    drm_core_findmap(dev, init->gart_textures_offset);
		if (!dev_priv->gart_textures) {
			DRM_ERROR("could not find GART texture region!\n");
			r600_do_cleanup_cp(dev);
			return -EINVAL;
		}
	}

#if __OS_HAS_AGP
	/* XXX */
	if (dev_priv->flags & RADEON_IS_AGP) {
		drm_core_ioremap_wc(dev_priv->cp_ring, dev);
		drm_core_ioremap_wc(dev_priv->ring_rptr, dev);
		drm_core_ioremap_wc(dev->agp_buffer_map, dev);
		if (!dev_priv->cp_ring->handle ||
		    !dev_priv->ring_rptr->handle ||
		    !dev->agp_buffer_map->handle) {
			DRM_ERROR("could not find ioremap agp regions!\n");
			r600_do_cleanup_cp(dev);
			return -EINVAL;
		}
	} else
#endif
	{
		dev_priv->cp_ring->handle = (void *)(unsigned long)dev_priv->cp_ring->offset;
		dev_priv->ring_rptr->handle =
			(void *)(unsigned long)dev_priv->ring_rptr->offset;
		dev->agp_buffer_map->handle =
			(void *)(unsigned long)dev->agp_buffer_map->offset;

		DRM_DEBUG("dev_priv->cp_ring->handle %p\n",
			  dev_priv->cp_ring->handle);
		DRM_DEBUG("dev_priv->ring_rptr->handle %p\n",
			  dev_priv->ring_rptr->handle);
		DRM_DEBUG("dev->agp_buffer_map->handle %p\n",
			  dev->agp_buffer_map->handle);
	}

	dev_priv->fb_location = (radeon_read_fb_location(dev_priv) & 0xffff) << 24;
	dev_priv->fb_size =
		(((radeon_read_fb_location(dev_priv) & 0xffff0000u) << 8) + 0x1000000)
		- dev_priv->fb_location;

	dev_priv->front_pitch_offset = (((dev_priv->front_pitch / 64) << 22) |
					((dev_priv->front_offset
					  + dev_priv->fb_location) >> 10));

	dev_priv->back_pitch_offset = (((dev_priv->back_pitch / 64) << 22) |
				       ((dev_priv->back_offset
					 + dev_priv->fb_location) >> 10));

	dev_priv->depth_pitch_offset = (((dev_priv->depth_pitch / 64) << 22) |
					((dev_priv->depth_offset
					  + dev_priv->fb_location) >> 10));

	dev_priv->gart_size = init->gart_size;

	/* New let's set the memory map ... */
	if (dev_priv->new_memmap) {
		u32 base = 0;

		DRM_INFO("Setting GART location based on new memory map\n");

		/* If using AGP, try to locate the AGP aperture at the same
		 * location in the card and on the bus, though we have to
		 * align it down.
		 */
#if __OS_HAS_AGP
		/* XXX */
		if (dev_priv->flags & RADEON_IS_AGP) {
			base = dev->agp->base;
			/* Check if valid */
			if ((base + dev_priv->gart_size - 1) >= dev_priv->fb_location &&
			    base < (dev_priv->fb_location + dev_priv->fb_size - 1)) {
				DRM_INFO("Can't use AGP base @0x%08lx, won't fit\n",
					 dev->agp->base);
				base = 0;
			}
		}
#endif
		/* If not or if AGP is at 0 (Macs), try to put it elsewhere */
		if (base == 0) {
			base = dev_priv->fb_location + dev_priv->fb_size;
			if (base < dev_priv->fb_location ||
			    ((base + dev_priv->gart_size) & 0xfffffffful) < base)
				base = dev_priv->fb_location
					- dev_priv->gart_size;
		}
		dev_priv->gart_vm_start = base & 0xffc00000u;
		if (dev_priv->gart_vm_start != base)
			DRM_INFO("GART aligned down from 0x%08x to 0x%08x\n",
				 base, dev_priv->gart_vm_start);
	}

#if __OS_HAS_AGP
	/* XXX */
	if (dev_priv->flags & RADEON_IS_AGP)
		dev_priv->gart_buffers_offset = (dev->agp_buffer_map->offset
						 - dev->agp->base
						 + dev_priv->gart_vm_start);
	else
#endif
		dev_priv->gart_buffers_offset = (dev->agp_buffer_map->offset
						 - (unsigned long)dev->sg->virtual
						 + dev_priv->gart_vm_start);

	DRM_DEBUG("fb 0x%08x size %d\n",
		  (unsigned int) dev_priv->fb_location,
		  (unsigned int) dev_priv->fb_size);
	DRM_DEBUG("dev_priv->gart_size %d\n", dev_priv->gart_size);
	DRM_DEBUG("dev_priv->gart_vm_start 0x%08x\n",
		  (unsigned int) dev_priv->gart_vm_start);
	DRM_DEBUG("dev_priv->gart_buffers_offset 0x%08lx\n",
		  dev_priv->gart_buffers_offset);

	dev_priv->ring.start = (u32 *) dev_priv->cp_ring->handle;
	dev_priv->ring.end = ((u32 *) dev_priv->cp_ring->handle
			      + init->ring_size / sizeof(u32));
	dev_priv->ring.size = init->ring_size;
	dev_priv->ring.size_l2qw = drm_order(init->ring_size / 8);

	dev_priv->ring.rptr_update = /* init->rptr_update */ 4096;
	dev_priv->ring.rptr_update_l2qw = drm_order(/* init->rptr_update */ 4096 / 8);

	dev_priv->ring.fetch_size = /* init->fetch_size */ 32;
	dev_priv->ring.fetch_size_l2ow = drm_order(/* init->fetch_size */ 32 / 16);

	dev_priv->ring.tail_mask = (dev_priv->ring.size / sizeof(u32)) - 1;

	dev_priv->ring.high_mark = RADEON_RING_HIGH_MARK;

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		/* XXX turn off pcie gart */
	} else
#endif
	{
		dev_priv->gart_info.table_mask = DMA_BIT_MASK(32);
		/* if we have an offset set from userspace */
		if (!dev_priv->pcigart_offset_set) {
			DRM_ERROR("Need gart offset from userspace\n");
			r600_do_cleanup_cp(dev);
			return -EINVAL;
		}

		DRM_DEBUG("Using gart offset 0x%08lx\n", dev_priv->pcigart_offset);

		dev_priv->gart_info.bus_addr =
			dev_priv->pcigart_offset + dev_priv->fb_location;
		dev_priv->gart_info.mapping.offset =
			dev_priv->pcigart_offset + dev_priv->fb_aper_offset;
		dev_priv->gart_info.mapping.size =
			dev_priv->gart_info.table_size;

		drm_core_ioremap_wc(&dev_priv->gart_info.mapping, dev);
		if (!dev_priv->gart_info.mapping.handle) {
			DRM_ERROR("ioremap failed.\n");
			r600_do_cleanup_cp(dev);
			return -EINVAL;
		}

		dev_priv->gart_info.addr =
			dev_priv->gart_info.mapping.handle;

		DRM_DEBUG("Setting phys_pci_gart to %p %08lX\n",
			  dev_priv->gart_info.addr,
			  dev_priv->pcigart_offset);

		if (!r600_page_table_init(dev)) {
			DRM_ERROR("Failed to init GART table\n");
			r600_do_cleanup_cp(dev);
			return -EINVAL;
		}

		if (((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RV770))
			r700_vm_init(dev);
		else
			r600_vm_init(dev);
	}

	if (!dev_priv->me_fw || !dev_priv->pfp_fw) {
		int err = r600_cp_init_microcode(dev_priv);
		if (err) {
			DRM_ERROR("Failed to load firmware!\n");
			r600_do_cleanup_cp(dev);
			return err;
		}
	}
	if (((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RV770))
		r700_cp_load_microcode(dev_priv);
	else
		r600_cp_load_microcode(dev_priv);

	r600_cp_init_ring_buffer(dev, dev_priv, file_priv);

	dev_priv->last_buf = 0;

	r600_do_engine_reset(dev);
	r600_test_writeback(dev_priv);

	return 0;
}

int r600_do_resume_cp(struct drm_device *dev, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG("\n");
	if (((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RV770)) {
		r700_vm_init(dev);
		r700_cp_load_microcode(dev_priv);
	} else {
		r600_vm_init(dev);
		r600_cp_load_microcode(dev_priv);
	}
	r600_cp_init_ring_buffer(dev, dev_priv, file_priv);
	r600_do_engine_reset(dev);

	return 0;
}

/* Wait for the CP to go idle.
 */
int r600_do_cp_idle(drm_radeon_private_t *dev_priv)
{
	RING_LOCALS;
	DRM_DEBUG("\n");

	BEGIN_RING(5);
	OUT_RING(CP_PACKET3(R600_IT_EVENT_WRITE, 0));
	OUT_RING(R600_CACHE_FLUSH_AND_INV_EVENT);
	/* wait for 3D idle clean */
	OUT_RING(CP_PACKET3(R600_IT_SET_CONFIG_REG, 1));
	OUT_RING((R600_WAIT_UNTIL - R600_SET_CONFIG_REG_OFFSET) >> 2);
	OUT_RING(RADEON_WAIT_3D_IDLE | RADEON_WAIT_3D_IDLECLEAN);

	ADVANCE_RING();
	COMMIT_RING();

	return r600_do_wait_for_idle(dev_priv);
}

/* Start the Command Processor.
 */
void r600_do_cp_start(drm_radeon_private_t *dev_priv)
{
	u32 cp_me;
	RING_LOCALS;
	DRM_DEBUG("\n");

	BEGIN_RING(7);
	OUT_RING(CP_PACKET3(R600_IT_ME_INITIALIZE, 5));
	OUT_RING(0x00000001);
	if (((dev_priv->flags & RADEON_FAMILY_MASK) < CHIP_RV770))
		OUT_RING(0x00000003);
	else
		OUT_RING(0x00000000);
	OUT_RING((dev_priv->r600_max_hw_contexts - 1));
	OUT_RING(R600_ME_INITIALIZE_DEVICE_ID(1));
	OUT_RING(0x00000000);
	OUT_RING(0x00000000);
	ADVANCE_RING();
	COMMIT_RING();

	/* set the mux and reset the halt bit */
	cp_me = 0xff;
	RADEON_WRITE(R600_CP_ME_CNTL, cp_me);

	dev_priv->cp_running = 1;

}

void r600_do_cp_reset(drm_radeon_private_t *dev_priv)
{
	u32 cur_read_ptr;
	DRM_DEBUG("\n");

	cur_read_ptr = RADEON_READ(R600_CP_RB_RPTR);
	RADEON_WRITE(R600_CP_RB_WPTR, cur_read_ptr);
	SET_RING_HEAD(dev_priv, cur_read_ptr);
	dev_priv->ring.tail = cur_read_ptr;
}

void r600_do_cp_stop(drm_radeon_private_t *dev_priv)
{
	uint32_t cp_me;

	DRM_DEBUG("\n");

	cp_me = 0xff | R600_CP_ME_HALT;

	RADEON_WRITE(R600_CP_ME_CNTL, cp_me);

	dev_priv->cp_running = 0;
}

int r600_cp_dispatch_indirect(struct drm_device *dev,
			      struct drm_buf *buf, int start, int end)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	if (start != end) {
		unsigned long offset = (dev_priv->gart_buffers_offset
					+ buf->offset + start);
		int dwords = (end - start + 3) / sizeof(u32);

		DRM_DEBUG("dwords:%d\n", dwords);
		DRM_DEBUG("offset 0x%lx\n", offset);


		/* Indirect buffer data must be a multiple of 16 dwords.
		 * pad the data with a Type-2 CP packet.
		 */
		while (dwords & 0xf) {
			u32 *data = (u32 *)
			    ((char *)dev->agp_buffer_map->handle
			     + buf->offset + start);
			data[dwords++] = RADEON_CP_PACKET2;
		}

		/* Fire off the indirect buffer */
		BEGIN_RING(4);
		OUT_RING(CP_PACKET3(R600_IT_INDIRECT_BUFFER, 2));
		OUT_RING((offset & 0xfffffffc));
		OUT_RING((upper_32_bits(offset) & 0xff));
		OUT_RING(dwords);
		ADVANCE_RING();
	}

	return 0;
}

void r600_cp_dispatch_swap(struct drm_device *dev, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	struct drm_master *master = file_priv->master;
	struct drm_radeon_master_private *master_priv = master->driver_priv;
	drm_radeon_sarea_t *sarea_priv = master_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	struct drm_clip_rect *pbox = sarea_priv->boxes;
	int i, cpp, src_pitch, dst_pitch;
	uint64_t src, dst;
	RING_LOCALS;
	DRM_DEBUG("\n");

	if (dev_priv->color_fmt == RADEON_COLOR_FORMAT_ARGB8888)
		cpp = 4;
	else
		cpp = 2;

	if (sarea_priv->pfCurrentPage == 0) {
		src_pitch = dev_priv->back_pitch;
		dst_pitch = dev_priv->front_pitch;
		src = dev_priv->back_offset + dev_priv->fb_location;
		dst = dev_priv->front_offset + dev_priv->fb_location;
	} else {
		src_pitch = dev_priv->front_pitch;
		dst_pitch = dev_priv->back_pitch;
		src = dev_priv->front_offset + dev_priv->fb_location;
		dst = dev_priv->back_offset + dev_priv->fb_location;
	}

	if (r600_prepare_blit_copy(dev, file_priv)) {
		DRM_ERROR("unable to allocate vertex buffer for swap buffer\n");
		return;
	}
	for (i = 0; i < nbox; i++) {
		int x = pbox[i].x1;
		int y = pbox[i].y1;
		int w = pbox[i].x2 - x;
		int h = pbox[i].y2 - y;

		DRM_DEBUG("%d,%d-%d,%d\n", x, y, w, h);

		r600_blit_swap(dev,
			       src, dst,
			       x, y, x, y, w, h,
			       src_pitch, dst_pitch, cpp);
	}
	r600_done_blit_copy(dev);

	/* Increment the frame counter.  The client-side 3D driver must
	 * throttle the framerate by waiting for this value before
	 * performing the swapbuffer ioctl.
	 */
	sarea_priv->last_frame++;

	BEGIN_RING(3);
	R600_FRAME_AGE(sarea_priv->last_frame);
	ADVANCE_RING();
}

int r600_cp_dispatch_texture(struct drm_device *dev,
			     struct drm_file *file_priv,
			     drm_radeon_texture_t *tex,
			     drm_radeon_tex_image_t *image)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	struct drm_buf *buf;
	u32 *buffer;
	const u8 __user *data;
	int size, pass_size;
	u64 src_offset, dst_offset;

	if (!radeon_check_offset(dev_priv, tex->offset)) {
		DRM_ERROR("Invalid destination offset\n");
		return -EINVAL;
	}

	/* this might fail for zero-sized uploads - are those illegal? */
	if (!radeon_check_offset(dev_priv, tex->offset + tex->height * tex->pitch - 1)) {
		DRM_ERROR("Invalid final destination offset\n");
		return -EINVAL;
	}

	size = tex->height * tex->pitch;

	if (size == 0)
		return 0;

	dst_offset = tex->offset;

	if (r600_prepare_blit_copy(dev, file_priv)) {
		DRM_ERROR("unable to allocate vertex buffer for swap buffer\n");
		return -EAGAIN;
	}
	do {
		data = (const u8 __user *)image->data;
		pass_size = size;

		buf = radeon_freelist_get(dev);
		if (!buf) {
			DRM_DEBUG("EAGAIN\n");
			if (DRM_COPY_TO_USER(tex->image, image, sizeof(*image)))
				return -EFAULT;
			return -EAGAIN;
		}

		if (pass_size > buf->total)
			pass_size = buf->total;

		/* Dispatch the indirect buffer.
		 */
		buffer =
		    (u32 *) ((char *)dev->agp_buffer_map->handle + buf->offset);

		if (DRM_COPY_FROM_USER(buffer, data, pass_size)) {
			DRM_ERROR("EFAULT on pad, %d bytes\n", pass_size);
			return -EFAULT;
		}

		buf->file_priv = file_priv;
		buf->used = pass_size;
		src_offset = dev_priv->gart_buffers_offset + buf->offset;

		r600_blit_copy(dev, src_offset, dst_offset, pass_size);

		radeon_cp_discard_buffer(dev, file_priv->master, buf);

		/* Update the input parameters for next time */
		image->data = (const u8 __user *)image->data + pass_size;
		dst_offset += pass_size;
		size -= pass_size;
	} while (size > 0);
	r600_done_blit_copy(dev);

	return 0;
}

/*
 * Legacy cs ioctl
 */
static u32 radeon_cs_id_get(struct drm_radeon_private *radeon)
{
	/* FIXME: check if wrap affect last reported wrap & sequence */
	radeon->cs_id_scnt = (radeon->cs_id_scnt + 1) & 0x00FFFFFF;
	if (!radeon->cs_id_scnt) {
		/* increment wrap counter */
		radeon->cs_id_wcnt += 0x01000000;
		/* valid sequence counter start at 1 */
		radeon->cs_id_scnt = 1;
	}
	return (radeon->cs_id_scnt | radeon->cs_id_wcnt);
}

static void r600_cs_id_emit(drm_radeon_private_t *dev_priv, u32 *id)
{
	RING_LOCALS;

	*id = radeon_cs_id_get(dev_priv);

	/* SCRATCH 2 */
	BEGIN_RING(3);
	R600_CLEAR_AGE(*id);
	ADVANCE_RING();
	COMMIT_RING();
}

static int r600_ib_get(struct drm_device *dev,
			struct drm_file *fpriv,
			struct drm_buf **buffer)
{
	struct drm_buf *buf;

	*buffer = NULL;
	buf = radeon_freelist_get(dev);
	if (!buf) {
		return -EBUSY;
	}
	buf->file_priv = fpriv;
	*buffer = buf;
	return 0;
}

static void r600_ib_free(struct drm_device *dev, struct drm_buf *buf,
			struct drm_file *fpriv, int l, int r)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	if (buf) {
		if (!r)
			r600_cp_dispatch_indirect(dev, buf, 0, l * 4);
		radeon_cp_discard_buffer(dev, fpriv->master, buf);
		COMMIT_RING();
	}
}

int r600_cs_legacy_ioctl(struct drm_device *dev, void *data, struct drm_file *fpriv)
{
	struct drm_radeon_private *dev_priv = dev->dev_private;
	struct drm_radeon_cs *cs = data;
	struct drm_buf *buf;
	unsigned family;
	int l, r = 0;
	u32 *ib, cs_id = 0;

	if (dev_priv == NULL) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}
	family = dev_priv->flags & RADEON_FAMILY_MASK;
	if (family < CHIP_R600) {
		DRM_ERROR("cs ioctl valid only for R6XX & R7XX in legacy mode\n");
		return -EINVAL;
	}
	mutex_lock(&dev_priv->cs_mutex);
	/* get ib */
	r = r600_ib_get(dev, fpriv, &buf);
	if (r) {
		DRM_ERROR("ib_get failed\n");
		goto out;
	}
	ib = dev->agp_buffer_map->handle + buf->offset;
	/* now parse command stream */
	r = r600_cs_legacy(dev, data,  fpriv, family, ib, &l);
	if (r) {
		goto out;
	}

out:
	r600_ib_free(dev, buf, fpriv, l, r);
	/* emit cs id sequence */
	r600_cs_id_emit(dev_priv, &cs_id);
	cs->cs_id = cs_id;
	mutex_unlock(&dev_priv->cs_mutex);
	return r;
}

void r600_cs_legacy_get_tiling_conf(struct drm_device *dev, u32 *npipes, u32 *nbanks, u32 *group_size)
{
	struct drm_radeon_private *dev_priv = dev->dev_private;

	*npipes = dev_priv->r600_npipes;
	*nbanks = dev_priv->r600_nbanks;
	*group_size = dev_priv->r600_group_size;
}
