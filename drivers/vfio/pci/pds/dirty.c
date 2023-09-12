// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#include <linux/interval_tree.h>
#include <linux/vfio.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_adminq.h>

#include "vfio_dev.h"
#include "cmds.h"
#include "dirty.h"

#define READ_SEQ true
#define WRITE_ACK false

bool pds_vfio_dirty_is_enabled(struct pds_vfio_pci_device *pds_vfio)
{
	return pds_vfio->dirty.is_enabled;
}

void pds_vfio_dirty_set_enabled(struct pds_vfio_pci_device *pds_vfio)
{
	pds_vfio->dirty.is_enabled = true;
}

void pds_vfio_dirty_set_disabled(struct pds_vfio_pci_device *pds_vfio)
{
	pds_vfio->dirty.is_enabled = false;
}

static void
pds_vfio_print_guest_region_info(struct pds_vfio_pci_device *pds_vfio,
				 u8 max_regions)
{
	int len = max_regions * sizeof(struct pds_lm_dirty_region_info);
	struct pci_dev *pdev = pds_vfio->vfio_coredev.pdev;
	struct device *pdsc_dev = &pci_physfn(pdev)->dev;
	struct pds_lm_dirty_region_info *region_info;
	dma_addr_t regions_dma;
	u8 num_regions;
	int err;

	region_info = kcalloc(max_regions,
			      sizeof(struct pds_lm_dirty_region_info),
			      GFP_KERNEL);
	if (!region_info)
		return;

	regions_dma =
		dma_map_single(pdsc_dev, region_info, len, DMA_FROM_DEVICE);
	if (dma_mapping_error(pdsc_dev, regions_dma))
		goto out_free_region_info;

	err = pds_vfio_dirty_status_cmd(pds_vfio, regions_dma, &max_regions,
					&num_regions);
	dma_unmap_single(pdsc_dev, regions_dma, len, DMA_FROM_DEVICE);
	if (err)
		goto out_free_region_info;

	for (unsigned int i = 0; i < num_regions; i++)
		dev_dbg(&pdev->dev,
			"region_info[%d]: dma_base 0x%llx page_count %u page_size_log2 %u\n",
			i, le64_to_cpu(region_info[i].dma_base),
			le32_to_cpu(region_info[i].page_count),
			region_info[i].page_size_log2);

out_free_region_info:
	kfree(region_info);
}

static int pds_vfio_dirty_alloc_bitmaps(struct pds_vfio_dirty *dirty,
					unsigned long bytes)
{
	unsigned long *host_seq_bmp, *host_ack_bmp;

	host_seq_bmp = vzalloc(bytes);
	if (!host_seq_bmp)
		return -ENOMEM;

	host_ack_bmp = vzalloc(bytes);
	if (!host_ack_bmp) {
		bitmap_free(host_seq_bmp);
		return -ENOMEM;
	}

	dirty->host_seq.bmp = host_seq_bmp;
	dirty->host_ack.bmp = host_ack_bmp;

	return 0;
}

static void pds_vfio_dirty_free_bitmaps(struct pds_vfio_dirty *dirty)
{
	vfree(dirty->host_seq.bmp);
	vfree(dirty->host_ack.bmp);
	dirty->host_seq.bmp = NULL;
	dirty->host_ack.bmp = NULL;
}

static void __pds_vfio_dirty_free_sgl(struct pds_vfio_pci_device *pds_vfio,
				      struct pds_vfio_bmp_info *bmp_info)
{
	struct pci_dev *pdev = pds_vfio->vfio_coredev.pdev;
	struct device *pdsc_dev = &pci_physfn(pdev)->dev;

	dma_unmap_single(pdsc_dev, bmp_info->sgl_addr,
			 bmp_info->num_sge * sizeof(struct pds_lm_sg_elem),
			 DMA_BIDIRECTIONAL);
	kfree(bmp_info->sgl);

	bmp_info->num_sge = 0;
	bmp_info->sgl = NULL;
	bmp_info->sgl_addr = 0;
}

static void pds_vfio_dirty_free_sgl(struct pds_vfio_pci_device *pds_vfio)
{
	if (pds_vfio->dirty.host_seq.sgl)
		__pds_vfio_dirty_free_sgl(pds_vfio, &pds_vfio->dirty.host_seq);
	if (pds_vfio->dirty.host_ack.sgl)
		__pds_vfio_dirty_free_sgl(pds_vfio, &pds_vfio->dirty.host_ack);
}

static int __pds_vfio_dirty_alloc_sgl(struct pds_vfio_pci_device *pds_vfio,
				      struct pds_vfio_bmp_info *bmp_info,
				      u32 page_count)
{
	struct pci_dev *pdev = pds_vfio->vfio_coredev.pdev;
	struct device *pdsc_dev = &pci_physfn(pdev)->dev;
	struct pds_lm_sg_elem *sgl;
	dma_addr_t sgl_addr;
	size_t sgl_size;
	u32 max_sge;

	max_sge = DIV_ROUND_UP(page_count, PAGE_SIZE * 8);
	sgl_size = max_sge * sizeof(struct pds_lm_sg_elem);

	sgl = kzalloc(sgl_size, GFP_KERNEL);
	if (!sgl)
		return -ENOMEM;

	sgl_addr = dma_map_single(pdsc_dev, sgl, sgl_size, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(pdsc_dev, sgl_addr)) {
		kfree(sgl);
		return -EIO;
	}

	bmp_info->sgl = sgl;
	bmp_info->num_sge = max_sge;
	bmp_info->sgl_addr = sgl_addr;

	return 0;
}

static int pds_vfio_dirty_alloc_sgl(struct pds_vfio_pci_device *pds_vfio,
				    u32 page_count)
{
	struct pds_vfio_dirty *dirty = &pds_vfio->dirty;
	int err;

	err = __pds_vfio_dirty_alloc_sgl(pds_vfio, &dirty->host_seq,
					 page_count);
	if (err)
		return err;

	err = __pds_vfio_dirty_alloc_sgl(pds_vfio, &dirty->host_ack,
					 page_count);
	if (err) {
		__pds_vfio_dirty_free_sgl(pds_vfio, &dirty->host_seq);
		return err;
	}

	return 0;
}

static int pds_vfio_dirty_enable(struct pds_vfio_pci_device *pds_vfio,
				 struct rb_root_cached *ranges, u32 nnodes,
				 u64 *page_size)
{
	struct pci_dev *pdev = pds_vfio->vfio_coredev.pdev;
	struct device *pdsc_dev = &pci_physfn(pdev)->dev;
	struct pds_vfio_dirty *dirty = &pds_vfio->dirty;
	u64 region_start, region_size, region_page_size;
	struct pds_lm_dirty_region_info *region_info;
	struct interval_tree_node *node = NULL;
	u8 max_regions = 0, num_regions;
	dma_addr_t regions_dma = 0;
	u32 num_ranges = nnodes;
	u32 page_count;
	u16 len;
	int err;

	dev_dbg(&pdev->dev, "vf%u: Start dirty page tracking\n",
		pds_vfio->vf_id);

	if (pds_vfio_dirty_is_enabled(pds_vfio))
		return -EINVAL;

	/* find if dirty tracking is disabled, i.e. num_regions == 0 */
	err = pds_vfio_dirty_status_cmd(pds_vfio, 0, &max_regions,
					&num_regions);
	if (err < 0) {
		dev_err(&pdev->dev, "Failed to get dirty status, err %pe\n",
			ERR_PTR(err));
		return err;
	} else if (num_regions) {
		dev_err(&pdev->dev,
			"Dirty tracking already enabled for %d regions\n",
			num_regions);
		return -EEXIST;
	} else if (!max_regions) {
		dev_err(&pdev->dev,
			"Device doesn't support dirty tracking, max_regions %d\n",
			max_regions);
		return -EOPNOTSUPP;
	}

	/*
	 * Only support 1 region for now. If there are any large gaps in the
	 * VM's address regions, then this would be a waste of memory as we are
	 * generating 2 bitmaps (ack/seq) from the min address to the max
	 * address of the VM's address regions. In the future, if we support
	 * more than one region in the device/driver we can split the bitmaps
	 * on the largest address region gaps. We can do this split up to the
	 * max_regions times returned from the dirty_status command.
	 */
	max_regions = 1;
	if (num_ranges > max_regions) {
		vfio_combine_iova_ranges(ranges, nnodes, max_regions);
		num_ranges = max_regions;
	}

	node = interval_tree_iter_first(ranges, 0, ULONG_MAX);
	if (!node)
		return -EINVAL;

	region_size = node->last - node->start + 1;
	region_start = node->start;
	region_page_size = *page_size;

	len = sizeof(*region_info);
	region_info = kzalloc(len, GFP_KERNEL);
	if (!region_info)
		return -ENOMEM;

	page_count = DIV_ROUND_UP(region_size, region_page_size);

	region_info->dma_base = cpu_to_le64(region_start);
	region_info->page_count = cpu_to_le32(page_count);
	region_info->page_size_log2 = ilog2(region_page_size);

	regions_dma = dma_map_single(pdsc_dev, (void *)region_info, len,
				     DMA_BIDIRECTIONAL);
	if (dma_mapping_error(pdsc_dev, regions_dma)) {
		err = -ENOMEM;
		goto out_free_region_info;
	}

	err = pds_vfio_dirty_enable_cmd(pds_vfio, regions_dma, max_regions);
	dma_unmap_single(pdsc_dev, regions_dma, len, DMA_BIDIRECTIONAL);
	if (err)
		goto out_free_region_info;

	/*
	 * page_count might be adjusted by the device,
	 * update it before freeing region_info DMA
	 */
	page_count = le32_to_cpu(region_info->page_count);

	dev_dbg(&pdev->dev,
		"region_info: regions_dma 0x%llx dma_base 0x%llx page_count %u page_size_log2 %u\n",
		regions_dma, region_start, page_count,
		(u8)ilog2(region_page_size));

	err = pds_vfio_dirty_alloc_bitmaps(dirty, page_count / BITS_PER_BYTE);
	if (err) {
		dev_err(&pdev->dev, "Failed to alloc dirty bitmaps: %pe\n",
			ERR_PTR(err));
		goto out_free_region_info;
	}

	err = pds_vfio_dirty_alloc_sgl(pds_vfio, page_count);
	if (err) {
		dev_err(&pdev->dev, "Failed to alloc dirty sg lists: %pe\n",
			ERR_PTR(err));
		goto out_free_bitmaps;
	}

	dirty->region_start = region_start;
	dirty->region_size = region_size;
	dirty->region_page_size = region_page_size;
	pds_vfio_dirty_set_enabled(pds_vfio);

	pds_vfio_print_guest_region_info(pds_vfio, max_regions);

	kfree(region_info);

	return 0;

out_free_bitmaps:
	pds_vfio_dirty_free_bitmaps(dirty);
out_free_region_info:
	kfree(region_info);
	return err;
}

void pds_vfio_dirty_disable(struct pds_vfio_pci_device *pds_vfio, bool send_cmd)
{
	if (pds_vfio_dirty_is_enabled(pds_vfio)) {
		pds_vfio_dirty_set_disabled(pds_vfio);
		if (send_cmd)
			pds_vfio_dirty_disable_cmd(pds_vfio);
		pds_vfio_dirty_free_sgl(pds_vfio);
		pds_vfio_dirty_free_bitmaps(&pds_vfio->dirty);
	}

	if (send_cmd)
		pds_vfio_send_host_vf_lm_status_cmd(pds_vfio, PDS_LM_STA_NONE);
}

static int pds_vfio_dirty_seq_ack(struct pds_vfio_pci_device *pds_vfio,
				  struct pds_vfio_bmp_info *bmp_info,
				  u32 offset, u32 bmp_bytes, bool read_seq)
{
	const char *bmp_type_str = read_seq ? "read_seq" : "write_ack";
	u8 dma_dir = read_seq ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	struct pci_dev *pdev = pds_vfio->vfio_coredev.pdev;
	struct device *pdsc_dev = &pci_physfn(pdev)->dev;
	unsigned long long npages;
	struct sg_table sg_table;
	struct scatterlist *sg;
	struct page **pages;
	u32 page_offset;
	const void *bmp;
	size_t size;
	u16 num_sge;
	int err;
	int i;

	bmp = (void *)((u64)bmp_info->bmp + offset);
	page_offset = offset_in_page(bmp);
	bmp -= page_offset;

	/*
	 * Start and end of bitmap section to seq/ack might not be page
	 * aligned, so use the page_offset to account for that so there
	 * will be enough pages to represent the bmp_bytes
	 */
	npages = DIV_ROUND_UP_ULL(bmp_bytes + page_offset, PAGE_SIZE);
	pages = kmalloc_array(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (unsigned long long i = 0; i < npages; i++) {
		struct page *page = vmalloc_to_page(bmp);

		if (!page) {
			err = -EFAULT;
			goto out_free_pages;
		}

		pages[i] = page;
		bmp += PAGE_SIZE;
	}

	err = sg_alloc_table_from_pages(&sg_table, pages, npages, page_offset,
					bmp_bytes, GFP_KERNEL);
	if (err)
		goto out_free_pages;

	err = dma_map_sgtable(pdsc_dev, &sg_table, dma_dir, 0);
	if (err)
		goto out_free_sg_table;

	for_each_sgtable_dma_sg(&sg_table, sg, i) {
		struct pds_lm_sg_elem *sg_elem = &bmp_info->sgl[i];

		sg_elem->addr = cpu_to_le64(sg_dma_address(sg));
		sg_elem->len = cpu_to_le32(sg_dma_len(sg));
	}

	num_sge = sg_table.nents;
	size = num_sge * sizeof(struct pds_lm_sg_elem);
	dma_sync_single_for_device(pdsc_dev, bmp_info->sgl_addr, size, dma_dir);
	err = pds_vfio_dirty_seq_ack_cmd(pds_vfio, bmp_info->sgl_addr, num_sge,
					 offset, bmp_bytes, read_seq);
	if (err)
		dev_err(&pdev->dev,
			"Dirty bitmap %s failed offset %u bmp_bytes %u num_sge %u DMA 0x%llx: %pe\n",
			bmp_type_str, offset, bmp_bytes,
			num_sge, bmp_info->sgl_addr, ERR_PTR(err));
	dma_sync_single_for_cpu(pdsc_dev, bmp_info->sgl_addr, size, dma_dir);

	dma_unmap_sgtable(pdsc_dev, &sg_table, dma_dir, 0);
out_free_sg_table:
	sg_free_table(&sg_table);
out_free_pages:
	kfree(pages);

	return err;
}

static int pds_vfio_dirty_write_ack(struct pds_vfio_pci_device *pds_vfio,
				    u32 offset, u32 len)
{
	return pds_vfio_dirty_seq_ack(pds_vfio, &pds_vfio->dirty.host_ack,
				      offset, len, WRITE_ACK);
}

static int pds_vfio_dirty_read_seq(struct pds_vfio_pci_device *pds_vfio,
				   u32 offset, u32 len)
{
	return pds_vfio_dirty_seq_ack(pds_vfio, &pds_vfio->dirty.host_seq,
				      offset, len, READ_SEQ);
}

static int pds_vfio_dirty_process_bitmaps(struct pds_vfio_pci_device *pds_vfio,
					  struct iova_bitmap *dirty_bitmap,
					  u32 bmp_offset, u32 len_bytes)
{
	u64 page_size = pds_vfio->dirty.region_page_size;
	u64 region_start = pds_vfio->dirty.region_start;
	u32 bmp_offset_bit;
	__le64 *seq, *ack;
	int dword_count;

	dword_count = len_bytes / sizeof(u64);
	seq = (__le64 *)((u64)pds_vfio->dirty.host_seq.bmp + bmp_offset);
	ack = (__le64 *)((u64)pds_vfio->dirty.host_ack.bmp + bmp_offset);
	bmp_offset_bit = bmp_offset * 8;

	for (int i = 0; i < dword_count; i++) {
		u64 xor = le64_to_cpu(seq[i]) ^ le64_to_cpu(ack[i]);

		/* prepare for next write_ack call */
		ack[i] = seq[i];

		for (u8 bit_i = 0; bit_i < BITS_PER_TYPE(u64); ++bit_i) {
			if (xor & BIT(bit_i)) {
				u64 abs_bit_i = bmp_offset_bit +
						i * BITS_PER_TYPE(u64) + bit_i;
				u64 addr = abs_bit_i * page_size + region_start;

				iova_bitmap_set(dirty_bitmap, addr, page_size);
			}
		}
	}

	return 0;
}

static int pds_vfio_dirty_sync(struct pds_vfio_pci_device *pds_vfio,
			       struct iova_bitmap *dirty_bitmap,
			       unsigned long iova, unsigned long length)
{
	struct device *dev = &pds_vfio->vfio_coredev.pdev->dev;
	struct pds_vfio_dirty *dirty = &pds_vfio->dirty;
	u64 bmp_offset, bmp_bytes;
	u64 bitmap_size, pages;
	int err;

	dev_dbg(dev, "vf%u: Get dirty page bitmap\n", pds_vfio->vf_id);

	if (!pds_vfio_dirty_is_enabled(pds_vfio)) {
		dev_err(dev, "vf%u: Sync failed, dirty tracking is disabled\n",
			pds_vfio->vf_id);
		return -EINVAL;
	}

	pages = DIV_ROUND_UP(length, pds_vfio->dirty.region_page_size);
	bitmap_size =
		round_up(pages, sizeof(u64) * BITS_PER_BYTE) / BITS_PER_BYTE;

	dev_dbg(dev,
		"vf%u: iova 0x%lx length %lu page_size %llu pages %llu bitmap_size %llu\n",
		pds_vfio->vf_id, iova, length, pds_vfio->dirty.region_page_size,
		pages, bitmap_size);

	if (!length || ((dirty->region_start + iova + length) >
			(dirty->region_start + dirty->region_size))) {
		dev_err(dev, "Invalid iova 0x%lx and/or length 0x%lx to sync\n",
			iova, length);
		return -EINVAL;
	}

	/* bitmap is modified in 64 bit chunks */
	bmp_bytes = ALIGN(DIV_ROUND_UP(length / dirty->region_page_size,
				       sizeof(u64)),
			  sizeof(u64));
	if (bmp_bytes != bitmap_size) {
		dev_err(dev,
			"Calculated bitmap bytes %llu not equal to bitmap size %llu\n",
			bmp_bytes, bitmap_size);
		return -EINVAL;
	}

	bmp_offset = DIV_ROUND_UP(iova / dirty->region_page_size, sizeof(u64));

	dev_dbg(dev,
		"Syncing dirty bitmap, iova 0x%lx length 0x%lx, bmp_offset %llu bmp_bytes %llu\n",
		iova, length, bmp_offset, bmp_bytes);

	err = pds_vfio_dirty_read_seq(pds_vfio, bmp_offset, bmp_bytes);
	if (err)
		return err;

	err = pds_vfio_dirty_process_bitmaps(pds_vfio, dirty_bitmap, bmp_offset,
					     bmp_bytes);
	if (err)
		return err;

	err = pds_vfio_dirty_write_ack(pds_vfio, bmp_offset, bmp_bytes);
	if (err)
		return err;

	return 0;
}

int pds_vfio_dma_logging_report(struct vfio_device *vdev, unsigned long iova,
				unsigned long length, struct iova_bitmap *dirty)
{
	struct pds_vfio_pci_device *pds_vfio =
		container_of(vdev, struct pds_vfio_pci_device,
			     vfio_coredev.vdev);
	int err;

	mutex_lock(&pds_vfio->state_mutex);
	err = pds_vfio_dirty_sync(pds_vfio, dirty, iova, length);
	pds_vfio_state_mutex_unlock(pds_vfio);

	return err;
}

int pds_vfio_dma_logging_start(struct vfio_device *vdev,
			       struct rb_root_cached *ranges, u32 nnodes,
			       u64 *page_size)
{
	struct pds_vfio_pci_device *pds_vfio =
		container_of(vdev, struct pds_vfio_pci_device,
			     vfio_coredev.vdev);
	int err;

	mutex_lock(&pds_vfio->state_mutex);
	pds_vfio_send_host_vf_lm_status_cmd(pds_vfio, PDS_LM_STA_IN_PROGRESS);
	err = pds_vfio_dirty_enable(pds_vfio, ranges, nnodes, page_size);
	pds_vfio_state_mutex_unlock(pds_vfio);

	return err;
}

int pds_vfio_dma_logging_stop(struct vfio_device *vdev)
{
	struct pds_vfio_pci_device *pds_vfio =
		container_of(vdev, struct pds_vfio_pci_device,
			     vfio_coredev.vdev);

	mutex_lock(&pds_vfio->state_mutex);
	pds_vfio_dirty_disable(pds_vfio, true);
	pds_vfio_state_mutex_unlock(pds_vfio);

	return 0;
}
