/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#ifndef _DIRTY_H_
#define _DIRTY_H_

struct pds_vfio_bmp_info {
	unsigned long *bmp;
	u32 bmp_bytes;
	struct pds_lm_sg_elem *sgl;
	dma_addr_t sgl_addr;
	u16 num_sge;
};

struct pds_vfio_dirty {
	struct pds_vfio_bmp_info host_seq;
	struct pds_vfio_bmp_info host_ack;
	u64 region_size;
	u64 region_start;
	u64 region_page_size;
	bool is_enabled;
};

struct pds_vfio_pci_device;

bool pds_vfio_dirty_is_enabled(struct pds_vfio_pci_device *pds_vfio);
void pds_vfio_dirty_set_enabled(struct pds_vfio_pci_device *pds_vfio);
void pds_vfio_dirty_set_disabled(struct pds_vfio_pci_device *pds_vfio);
void pds_vfio_dirty_disable(struct pds_vfio_pci_device *pds_vfio,
			    bool send_cmd);

int pds_vfio_dma_logging_report(struct vfio_device *vdev, unsigned long iova,
				unsigned long length,
				struct iova_bitmap *dirty);
int pds_vfio_dma_logging_start(struct vfio_device *vdev,
			       struct rb_root_cached *ranges, u32 nnodes,
			       u64 *page_size);
int pds_vfio_dma_logging_stop(struct vfio_device *vdev);
#endif /* _DIRTY_H_ */
