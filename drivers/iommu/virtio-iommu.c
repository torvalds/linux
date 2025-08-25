// SPDX-License-Identifier: GPL-2.0
/*
 * Virtio driver for the paravirtualized IOMMU
 *
 * Copyright (C) 2019 Arm Limited
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/dma-map-ops.h>
#include <linux/freezer.h>
#include <linux/interval_tree.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/wait.h>

#include <uapi/linux/virtio_iommu.h>

#include "dma-iommu.h"

#define MSI_IOVA_BASE			0x8000000
#define MSI_IOVA_LENGTH			0x100000

#define VIOMMU_REQUEST_VQ		0
#define VIOMMU_EVENT_VQ			1
#define VIOMMU_NR_VQS			2

struct viommu_dev {
	struct iommu_device		iommu;
	struct device			*dev;
	struct virtio_device		*vdev;

	struct ida			domain_ids;

	struct virtqueue		*vqs[VIOMMU_NR_VQS];
	spinlock_t			request_lock;
	struct list_head		requests;
	void				*evts;

	/* Device configuration */
	struct iommu_domain_geometry	geometry;
	u64				pgsize_bitmap;
	u32				first_domain;
	u32				last_domain;
	u32				identity_domain_id;
	/* Supported MAP flags */
	u32				map_flags;
	u32				probe_size;
};

struct viommu_mapping {
	phys_addr_t			paddr;
	struct interval_tree_node	iova;
	u32				flags;
};

struct viommu_domain {
	struct iommu_domain		domain;
	struct viommu_dev		*viommu;
	unsigned int			id;
	u32				map_flags;

	spinlock_t			mappings_lock;
	struct rb_root_cached		mappings;

	unsigned long			nr_endpoints;
};

struct viommu_endpoint {
	struct device			*dev;
	struct viommu_dev		*viommu;
	struct viommu_domain		*vdomain;
	struct list_head		resv_regions;
};

struct viommu_request {
	struct list_head		list;
	void				*writeback;
	unsigned int			write_offset;
	unsigned int			len;
	char				buf[] __counted_by(len);
};

#define VIOMMU_FAULT_RESV_MASK		0xffffff00

struct viommu_event {
	union {
		u32			head;
		struct virtio_iommu_fault fault;
	};
};

static struct viommu_domain viommu_identity_domain;

#define to_viommu_domain(domain)	\
	container_of(domain, struct viommu_domain, domain)

static int viommu_get_req_errno(void *buf, size_t len)
{
	struct virtio_iommu_req_tail *tail = buf + len - sizeof(*tail);

	switch (tail->status) {
	case VIRTIO_IOMMU_S_OK:
		return 0;
	case VIRTIO_IOMMU_S_UNSUPP:
		return -ENOSYS;
	case VIRTIO_IOMMU_S_INVAL:
		return -EINVAL;
	case VIRTIO_IOMMU_S_RANGE:
		return -ERANGE;
	case VIRTIO_IOMMU_S_NOENT:
		return -ENOENT;
	case VIRTIO_IOMMU_S_FAULT:
		return -EFAULT;
	case VIRTIO_IOMMU_S_NOMEM:
		return -ENOMEM;
	case VIRTIO_IOMMU_S_IOERR:
	case VIRTIO_IOMMU_S_DEVERR:
	default:
		return -EIO;
	}
}

static void viommu_set_req_status(void *buf, size_t len, int status)
{
	struct virtio_iommu_req_tail *tail = buf + len - sizeof(*tail);

	tail->status = status;
}

static off_t viommu_get_write_desc_offset(struct viommu_dev *viommu,
					  struct virtio_iommu_req_head *req,
					  size_t len)
{
	size_t tail_size = sizeof(struct virtio_iommu_req_tail);

	if (req->type == VIRTIO_IOMMU_T_PROBE)
		return len - viommu->probe_size - tail_size;

	return len - tail_size;
}

/*
 * __viommu_sync_req - Complete all in-flight requests
 *
 * Wait for all added requests to complete. When this function returns, all
 * requests that were in-flight at the time of the call have completed.
 */
static int __viommu_sync_req(struct viommu_dev *viommu)
{
	unsigned int len;
	size_t write_len;
	struct viommu_request *req;
	struct virtqueue *vq = viommu->vqs[VIOMMU_REQUEST_VQ];

	assert_spin_locked(&viommu->request_lock);

	virtqueue_kick(vq);

	while (!list_empty(&viommu->requests)) {
		len = 0;
		req = virtqueue_get_buf(vq, &len);
		if (!req)
			continue;

		if (!len)
			viommu_set_req_status(req->buf, req->len,
					      VIRTIO_IOMMU_S_IOERR);

		write_len = req->len - req->write_offset;
		if (req->writeback && len == write_len)
			memcpy(req->writeback, req->buf + req->write_offset,
			       write_len);

		list_del(&req->list);
		kfree(req);
	}

	return 0;
}

static int viommu_sync_req(struct viommu_dev *viommu)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&viommu->request_lock, flags);
	ret = __viommu_sync_req(viommu);
	if (ret)
		dev_dbg(viommu->dev, "could not sync requests (%d)\n", ret);
	spin_unlock_irqrestore(&viommu->request_lock, flags);

	return ret;
}

/*
 * __viommu_add_request - Add one request to the queue
 * @buf: pointer to the request buffer
 * @len: length of the request buffer
 * @writeback: copy data back to the buffer when the request completes.
 *
 * Add a request to the queue. Only synchronize the queue if it's already full.
 * Otherwise don't kick the queue nor wait for requests to complete.
 *
 * When @writeback is true, data written by the device, including the request
 * status, is copied into @buf after the request completes. This is unsafe if
 * the caller allocates @buf on stack and drops the lock between add_req() and
 * sync_req().
 *
 * Return 0 if the request was successfully added to the queue.
 */
static int __viommu_add_req(struct viommu_dev *viommu, void *buf, size_t len,
			    bool writeback)
{
	int ret;
	off_t write_offset;
	struct viommu_request *req;
	struct scatterlist top_sg, bottom_sg;
	struct scatterlist *sg[2] = { &top_sg, &bottom_sg };
	struct virtqueue *vq = viommu->vqs[VIOMMU_REQUEST_VQ];

	assert_spin_locked(&viommu->request_lock);

	write_offset = viommu_get_write_desc_offset(viommu, buf, len);
	if (write_offset <= 0)
		return -EINVAL;

	req = kzalloc(struct_size(req, buf, len), GFP_ATOMIC);
	if (!req)
		return -ENOMEM;

	req->len = len;
	if (writeback) {
		req->writeback = buf + write_offset;
		req->write_offset = write_offset;
	}
	memcpy(&req->buf, buf, write_offset);

	sg_init_one(&top_sg, req->buf, write_offset);
	sg_init_one(&bottom_sg, req->buf + write_offset, len - write_offset);

	ret = virtqueue_add_sgs(vq, sg, 1, 1, req, GFP_ATOMIC);
	if (ret == -ENOSPC) {
		/* If the queue is full, sync and retry */
		if (!__viommu_sync_req(viommu))
			ret = virtqueue_add_sgs(vq, sg, 1, 1, req, GFP_ATOMIC);
	}
	if (ret)
		goto err_free;

	list_add_tail(&req->list, &viommu->requests);
	return 0;

err_free:
	kfree(req);
	return ret;
}

static int viommu_add_req(struct viommu_dev *viommu, void *buf, size_t len)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&viommu->request_lock, flags);
	ret = __viommu_add_req(viommu, buf, len, false);
	if (ret)
		dev_dbg(viommu->dev, "could not add request: %d\n", ret);
	spin_unlock_irqrestore(&viommu->request_lock, flags);

	return ret;
}

/*
 * Send a request and wait for it to complete. Return the request status (as an
 * errno)
 */
static int viommu_send_req_sync(struct viommu_dev *viommu, void *buf,
				size_t len)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&viommu->request_lock, flags);

	ret = __viommu_add_req(viommu, buf, len, true);
	if (ret) {
		dev_dbg(viommu->dev, "could not add request (%d)\n", ret);
		goto out_unlock;
	}

	ret = __viommu_sync_req(viommu);
	if (ret) {
		dev_dbg(viommu->dev, "could not sync requests (%d)\n", ret);
		/* Fall-through (get the actual request status) */
	}

	ret = viommu_get_req_errno(buf, len);
out_unlock:
	spin_unlock_irqrestore(&viommu->request_lock, flags);
	return ret;
}

static int viommu_send_attach_req(struct viommu_dev *viommu, struct device *dev,
				  struct virtio_iommu_req_attach *req)
{
	int ret;
	unsigned int i;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	for (i = 0; i < fwspec->num_ids; i++) {
		req->endpoint = cpu_to_le32(fwspec->ids[i]);
		ret = viommu_send_req_sync(viommu, req, sizeof(*req));
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * viommu_add_mapping - add a mapping to the internal tree
 *
 * On success, return the new mapping. Otherwise return NULL.
 */
static int viommu_add_mapping(struct viommu_domain *vdomain, u64 iova, u64 end,
			      phys_addr_t paddr, u32 flags)
{
	unsigned long irqflags;
	struct viommu_mapping *mapping;

	mapping = kzalloc(sizeof(*mapping), GFP_ATOMIC);
	if (!mapping)
		return -ENOMEM;

	mapping->paddr		= paddr;
	mapping->iova.start	= iova;
	mapping->iova.last	= end;
	mapping->flags		= flags;

	spin_lock_irqsave(&vdomain->mappings_lock, irqflags);
	interval_tree_insert(&mapping->iova, &vdomain->mappings);
	spin_unlock_irqrestore(&vdomain->mappings_lock, irqflags);

	return 0;
}

/*
 * viommu_del_mappings - remove mappings from the internal tree
 *
 * @vdomain: the domain
 * @iova: start of the range
 * @end: end of the range
 *
 * On success, returns the number of unmapped bytes
 */
static size_t viommu_del_mappings(struct viommu_domain *vdomain,
				  u64 iova, u64 end)
{
	size_t unmapped = 0;
	unsigned long flags;
	struct viommu_mapping *mapping = NULL;
	struct interval_tree_node *node, *next;

	spin_lock_irqsave(&vdomain->mappings_lock, flags);
	next = interval_tree_iter_first(&vdomain->mappings, iova, end);
	while (next) {
		node = next;
		mapping = container_of(node, struct viommu_mapping, iova);
		next = interval_tree_iter_next(node, iova, end);

		/* Trying to split a mapping? */
		if (mapping->iova.start < iova)
			break;

		/*
		 * Virtio-iommu doesn't allow UNMAP to split a mapping created
		 * with a single MAP request, so remove the full mapping.
		 */
		unmapped += mapping->iova.last - mapping->iova.start + 1;

		interval_tree_remove(node, &vdomain->mappings);
		kfree(mapping);
	}
	spin_unlock_irqrestore(&vdomain->mappings_lock, flags);

	return unmapped;
}

/*
 * Fill the domain with identity mappings, skipping the device's reserved
 * regions.
 */
static int viommu_domain_map_identity(struct viommu_endpoint *vdev,
				      struct viommu_domain *vdomain)
{
	int ret;
	struct iommu_resv_region *resv;
	u64 iova = vdomain->domain.geometry.aperture_start;
	u64 limit = vdomain->domain.geometry.aperture_end;
	u32 flags = VIRTIO_IOMMU_MAP_F_READ | VIRTIO_IOMMU_MAP_F_WRITE;
	unsigned long granule = 1UL << __ffs(vdomain->domain.pgsize_bitmap);

	iova = ALIGN(iova, granule);
	limit = ALIGN_DOWN(limit + 1, granule) - 1;

	list_for_each_entry(resv, &vdev->resv_regions, list) {
		u64 resv_start = ALIGN_DOWN(resv->start, granule);
		u64 resv_end = ALIGN(resv->start + resv->length, granule) - 1;

		if (resv_end < iova || resv_start > limit)
			/* No overlap */
			continue;

		if (resv_start > iova) {
			ret = viommu_add_mapping(vdomain, iova, resv_start - 1,
						 (phys_addr_t)iova, flags);
			if (ret)
				goto err_unmap;
		}

		if (resv_end >= limit)
			return 0;

		iova = resv_end + 1;
	}

	ret = viommu_add_mapping(vdomain, iova, limit, (phys_addr_t)iova,
				 flags);
	if (ret)
		goto err_unmap;
	return 0;

err_unmap:
	viommu_del_mappings(vdomain, 0, iova);
	return ret;
}

/*
 * viommu_replay_mappings - re-send MAP requests
 *
 * When reattaching a domain that was previously detached from all endpoints,
 * mappings were deleted from the device. Re-create the mappings available in
 * the internal tree.
 */
static int viommu_replay_mappings(struct viommu_domain *vdomain)
{
	int ret = 0;
	unsigned long flags;
	struct viommu_mapping *mapping;
	struct interval_tree_node *node;
	struct virtio_iommu_req_map map;

	spin_lock_irqsave(&vdomain->mappings_lock, flags);
	node = interval_tree_iter_first(&vdomain->mappings, 0, -1UL);
	while (node) {
		mapping = container_of(node, struct viommu_mapping, iova);
		map = (struct virtio_iommu_req_map) {
			.head.type	= VIRTIO_IOMMU_T_MAP,
			.domain		= cpu_to_le32(vdomain->id),
			.virt_start	= cpu_to_le64(mapping->iova.start),
			.virt_end	= cpu_to_le64(mapping->iova.last),
			.phys_start	= cpu_to_le64(mapping->paddr),
			.flags		= cpu_to_le32(mapping->flags),
		};

		ret = viommu_send_req_sync(vdomain->viommu, &map, sizeof(map));
		if (ret)
			break;

		node = interval_tree_iter_next(node, 0, -1UL);
	}
	spin_unlock_irqrestore(&vdomain->mappings_lock, flags);

	return ret;
}

static int viommu_add_resv_mem(struct viommu_endpoint *vdev,
			       struct virtio_iommu_probe_resv_mem *mem,
			       size_t len)
{
	size_t size;
	u64 start64, end64;
	phys_addr_t start, end;
	struct iommu_resv_region *region = NULL, *next;
	unsigned long prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_MMIO;

	start = start64 = le64_to_cpu(mem->start);
	end = end64 = le64_to_cpu(mem->end);
	size = end64 - start64 + 1;

	/* Catch any overflow, including the unlikely end64 - start64 + 1 = 0 */
	if (start != start64 || end != end64 || size < end64 - start64)
		return -EOVERFLOW;

	if (len < sizeof(*mem))
		return -EINVAL;

	switch (mem->subtype) {
	default:
		dev_warn(vdev->dev, "unknown resv mem subtype 0x%x\n",
			 mem->subtype);
		fallthrough;
	case VIRTIO_IOMMU_RESV_MEM_T_RESERVED:
		region = iommu_alloc_resv_region(start, size, 0,
						 IOMMU_RESV_RESERVED,
						 GFP_KERNEL);
		break;
	case VIRTIO_IOMMU_RESV_MEM_T_MSI:
		region = iommu_alloc_resv_region(start, size, prot,
						 IOMMU_RESV_MSI,
						 GFP_KERNEL);
		break;
	}
	if (!region)
		return -ENOMEM;

	/* Keep the list sorted */
	list_for_each_entry(next, &vdev->resv_regions, list) {
		if (next->start > region->start)
			break;
	}
	list_add_tail(&region->list, &next->list);
	return 0;
}

static int viommu_probe_endpoint(struct viommu_dev *viommu, struct device *dev)
{
	int ret;
	u16 type, len;
	size_t cur = 0;
	size_t probe_len;
	struct virtio_iommu_req_probe *probe;
	struct virtio_iommu_probe_property *prop;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct viommu_endpoint *vdev = dev_iommu_priv_get(dev);

	if (!fwspec->num_ids)
		return -EINVAL;

	probe_len = sizeof(*probe) + viommu->probe_size +
		    sizeof(struct virtio_iommu_req_tail);
	probe = kzalloc(probe_len, GFP_KERNEL);
	if (!probe)
		return -ENOMEM;

	probe->head.type = VIRTIO_IOMMU_T_PROBE;
	/*
	 * For now, assume that properties of an endpoint that outputs multiple
	 * IDs are consistent. Only probe the first one.
	 */
	probe->endpoint = cpu_to_le32(fwspec->ids[0]);

	ret = viommu_send_req_sync(viommu, probe, probe_len);
	if (ret)
		goto out_free;

	prop = (void *)probe->properties;
	type = le16_to_cpu(prop->type) & VIRTIO_IOMMU_PROBE_T_MASK;

	while (type != VIRTIO_IOMMU_PROBE_T_NONE &&
	       cur < viommu->probe_size) {
		len = le16_to_cpu(prop->length) + sizeof(*prop);

		switch (type) {
		case VIRTIO_IOMMU_PROBE_T_RESV_MEM:
			ret = viommu_add_resv_mem(vdev, (void *)prop, len);
			break;
		default:
			dev_err(dev, "unknown viommu prop 0x%x\n", type);
		}

		if (ret)
			dev_err(dev, "failed to parse viommu prop 0x%x\n", type);

		cur += len;
		if (cur >= viommu->probe_size)
			break;

		prop = (void *)probe->properties + cur;
		type = le16_to_cpu(prop->type) & VIRTIO_IOMMU_PROBE_T_MASK;
	}

out_free:
	kfree(probe);
	return ret;
}

static int viommu_fault_handler(struct viommu_dev *viommu,
				struct virtio_iommu_fault *fault)
{
	char *reason_str;

	u8 reason	= fault->reason;
	u32 flags	= le32_to_cpu(fault->flags);
	u32 endpoint	= le32_to_cpu(fault->endpoint);
	u64 address	= le64_to_cpu(fault->address);

	switch (reason) {
	case VIRTIO_IOMMU_FAULT_R_DOMAIN:
		reason_str = "domain";
		break;
	case VIRTIO_IOMMU_FAULT_R_MAPPING:
		reason_str = "page";
		break;
	case VIRTIO_IOMMU_FAULT_R_UNKNOWN:
	default:
		reason_str = "unknown";
		break;
	}

	/* TODO: find EP by ID and report_iommu_fault */
	if (flags & VIRTIO_IOMMU_FAULT_F_ADDRESS)
		dev_err_ratelimited(viommu->dev, "%s fault from EP %u at %#llx [%s%s%s]\n",
				    reason_str, endpoint, address,
				    flags & VIRTIO_IOMMU_FAULT_F_READ ? "R" : "",
				    flags & VIRTIO_IOMMU_FAULT_F_WRITE ? "W" : "",
				    flags & VIRTIO_IOMMU_FAULT_F_EXEC ? "X" : "");
	else
		dev_err_ratelimited(viommu->dev, "%s fault from EP %u\n",
				    reason_str, endpoint);
	return 0;
}

static void viommu_event_handler(struct virtqueue *vq)
{
	int ret;
	unsigned int len;
	struct scatterlist sg[1];
	struct viommu_event *evt;
	struct viommu_dev *viommu = vq->vdev->priv;

	while ((evt = virtqueue_get_buf(vq, &len)) != NULL) {
		if (len > sizeof(*evt)) {
			dev_err(viommu->dev,
				"invalid event buffer (len %u != %zu)\n",
				len, sizeof(*evt));
		} else if (!(evt->head & VIOMMU_FAULT_RESV_MASK)) {
			viommu_fault_handler(viommu, &evt->fault);
		}

		sg_init_one(sg, evt, sizeof(*evt));
		ret = virtqueue_add_inbuf(vq, sg, 1, evt, GFP_ATOMIC);
		if (ret)
			dev_err(viommu->dev, "could not add event buffer\n");
	}

	virtqueue_kick(vq);
}

/* IOMMU API */

static struct iommu_domain *viommu_domain_alloc_paging(struct device *dev)
{
	struct viommu_endpoint *vdev = dev_iommu_priv_get(dev);
	struct viommu_dev *viommu = vdev->viommu;
	unsigned long viommu_page_size;
	struct viommu_domain *vdomain;
	int ret;

	viommu_page_size = 1UL << __ffs(viommu->pgsize_bitmap);
	if (viommu_page_size > PAGE_SIZE) {
		dev_err(vdev->dev,
			"granule 0x%lx larger than system page size 0x%lx\n",
			viommu_page_size, PAGE_SIZE);
		return ERR_PTR(-ENODEV);
	}

	vdomain = kzalloc(sizeof(*vdomain), GFP_KERNEL);
	if (!vdomain)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&vdomain->mappings_lock);
	vdomain->mappings = RB_ROOT_CACHED;

	ret = ida_alloc_range(&viommu->domain_ids, viommu->first_domain,
			      viommu->last_domain, GFP_KERNEL);
	if (ret < 0) {
		kfree(vdomain);
		return ERR_PTR(ret);
	}

	vdomain->id = (unsigned int)ret;

	vdomain->domain.pgsize_bitmap = viommu->pgsize_bitmap;
	vdomain->domain.geometry = viommu->geometry;

	vdomain->map_flags = viommu->map_flags;
	vdomain->viommu = viommu;

	return &vdomain->domain;
}

static void viommu_domain_free(struct iommu_domain *domain)
{
	struct viommu_domain *vdomain = to_viommu_domain(domain);

	/* Free all remaining mappings */
	viommu_del_mappings(vdomain, 0, ULLONG_MAX);

	if (vdomain->viommu)
		ida_free(&vdomain->viommu->domain_ids, vdomain->id);

	kfree(vdomain);
}

static struct iommu_domain *viommu_domain_alloc_identity(struct device *dev)
{
	struct viommu_endpoint *vdev = dev_iommu_priv_get(dev);
	struct iommu_domain *domain;
	int ret;

	if (virtio_has_feature(vdev->viommu->vdev,
			       VIRTIO_IOMMU_F_BYPASS_CONFIG))
		return &viommu_identity_domain.domain;

	domain = viommu_domain_alloc_paging(dev);
	if (IS_ERR(domain))
		return domain;

	ret = viommu_domain_map_identity(vdev, to_viommu_domain(domain));
	if (ret) {
		viommu_domain_free(domain);
		return ERR_PTR(ret);
	}
	return domain;
}

static int viommu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	int ret = 0;
	struct virtio_iommu_req_attach req;
	struct viommu_endpoint *vdev = dev_iommu_priv_get(dev);
	struct viommu_domain *vdomain = to_viommu_domain(domain);

	if (vdomain->viommu != vdev->viommu)
		return -EINVAL;

	/*
	 * In the virtio-iommu device, when attaching the endpoint to a new
	 * domain, it is detached from the old one and, if as a result the
	 * old domain isn't attached to any endpoint, all mappings are removed
	 * from the old domain and it is freed.
	 *
	 * In the driver the old domain still exists, and its mappings will be
	 * recreated if it gets reattached to an endpoint. Otherwise it will be
	 * freed explicitly.
	 *
	 * vdev->vdomain is protected by group->mutex
	 */
	if (vdev->vdomain)
		vdev->vdomain->nr_endpoints--;

	req = (struct virtio_iommu_req_attach) {
		.head.type	= VIRTIO_IOMMU_T_ATTACH,
		.domain		= cpu_to_le32(vdomain->id),
	};

	ret = viommu_send_attach_req(vdomain->viommu, dev, &req);
	if (ret)
		return ret;

	if (!vdomain->nr_endpoints) {
		/*
		 * This endpoint is the first to be attached to the domain.
		 * Replay existing mappings (e.g. SW MSI).
		 */
		ret = viommu_replay_mappings(vdomain);
		if (ret)
			return ret;
	}

	vdomain->nr_endpoints++;
	vdev->vdomain = vdomain;

	return 0;
}

static int viommu_attach_identity_domain(struct iommu_domain *domain,
					 struct device *dev)
{
	int ret = 0;
	struct virtio_iommu_req_attach req;
	struct viommu_endpoint *vdev = dev_iommu_priv_get(dev);
	struct viommu_domain *vdomain = to_viommu_domain(domain);

	req = (struct virtio_iommu_req_attach) {
		.head.type	= VIRTIO_IOMMU_T_ATTACH,
		.domain		= cpu_to_le32(vdev->viommu->identity_domain_id),
		.flags          = cpu_to_le32(VIRTIO_IOMMU_ATTACH_F_BYPASS),
	};

	ret = viommu_send_attach_req(vdev->viommu, dev, &req);
	if (ret)
		return ret;

	if (vdev->vdomain)
		vdev->vdomain->nr_endpoints--;
	vdomain->nr_endpoints++;
	vdev->vdomain = vdomain;
	return 0;
}

static struct viommu_domain viommu_identity_domain = {
	.domain = {
		.type = IOMMU_DOMAIN_IDENTITY,
		.ops = &(const struct iommu_domain_ops) {
			.attach_dev = viommu_attach_identity_domain,
		},
	},
};

static void viommu_detach_dev(struct viommu_endpoint *vdev)
{
	int i;
	struct virtio_iommu_req_detach req;
	struct viommu_domain *vdomain = vdev->vdomain;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(vdev->dev);

	if (!vdomain)
		return;

	req = (struct virtio_iommu_req_detach) {
		.head.type	= VIRTIO_IOMMU_T_DETACH,
		.domain		= cpu_to_le32(vdomain->id),
	};

	for (i = 0; i < fwspec->num_ids; i++) {
		req.endpoint = cpu_to_le32(fwspec->ids[i]);
		WARN_ON(viommu_send_req_sync(vdev->viommu, &req, sizeof(req)));
	}
	vdomain->nr_endpoints--;
	vdev->vdomain = NULL;
}

static int viommu_map_pages(struct iommu_domain *domain, unsigned long iova,
			    phys_addr_t paddr, size_t pgsize, size_t pgcount,
			    int prot, gfp_t gfp, size_t *mapped)
{
	int ret;
	u32 flags;
	size_t size = pgsize * pgcount;
	u64 end = iova + size - 1;
	struct virtio_iommu_req_map map;
	struct viommu_domain *vdomain = to_viommu_domain(domain);

	flags = (prot & IOMMU_READ ? VIRTIO_IOMMU_MAP_F_READ : 0) |
		(prot & IOMMU_WRITE ? VIRTIO_IOMMU_MAP_F_WRITE : 0) |
		(prot & IOMMU_MMIO ? VIRTIO_IOMMU_MAP_F_MMIO : 0);

	if (flags & ~vdomain->map_flags)
		return -EINVAL;

	ret = viommu_add_mapping(vdomain, iova, end, paddr, flags);
	if (ret)
		return ret;

	if (vdomain->nr_endpoints) {
		map = (struct virtio_iommu_req_map) {
			.head.type	= VIRTIO_IOMMU_T_MAP,
			.domain		= cpu_to_le32(vdomain->id),
			.virt_start	= cpu_to_le64(iova),
			.phys_start	= cpu_to_le64(paddr),
			.virt_end	= cpu_to_le64(end),
			.flags		= cpu_to_le32(flags),
		};

		ret = viommu_add_req(vdomain->viommu, &map, sizeof(map));
		if (ret) {
			viommu_del_mappings(vdomain, iova, end);
			return ret;
		}
	}
	if (mapped)
		*mapped = size;

	return 0;
}

static size_t viommu_unmap_pages(struct iommu_domain *domain, unsigned long iova,
				 size_t pgsize, size_t pgcount,
				 struct iommu_iotlb_gather *gather)
{
	int ret = 0;
	size_t unmapped;
	struct virtio_iommu_req_unmap unmap;
	struct viommu_domain *vdomain = to_viommu_domain(domain);
	size_t size = pgsize * pgcount;

	unmapped = viommu_del_mappings(vdomain, iova, iova + size - 1);
	if (unmapped < size)
		return 0;

	/* Device already removed all mappings after detach. */
	if (!vdomain->nr_endpoints)
		return unmapped;

	unmap = (struct virtio_iommu_req_unmap) {
		.head.type	= VIRTIO_IOMMU_T_UNMAP,
		.domain		= cpu_to_le32(vdomain->id),
		.virt_start	= cpu_to_le64(iova),
		.virt_end	= cpu_to_le64(iova + unmapped - 1),
	};

	ret = viommu_add_req(vdomain->viommu, &unmap, sizeof(unmap));
	return ret ? 0 : unmapped;
}

static phys_addr_t viommu_iova_to_phys(struct iommu_domain *domain,
				       dma_addr_t iova)
{
	u64 paddr = 0;
	unsigned long flags;
	struct viommu_mapping *mapping;
	struct interval_tree_node *node;
	struct viommu_domain *vdomain = to_viommu_domain(domain);

	spin_lock_irqsave(&vdomain->mappings_lock, flags);
	node = interval_tree_iter_first(&vdomain->mappings, iova, iova);
	if (node) {
		mapping = container_of(node, struct viommu_mapping, iova);
		paddr = mapping->paddr + (iova - mapping->iova.start);
	}
	spin_unlock_irqrestore(&vdomain->mappings_lock, flags);

	return paddr;
}

static void viommu_iotlb_sync(struct iommu_domain *domain,
			      struct iommu_iotlb_gather *gather)
{
	struct viommu_domain *vdomain = to_viommu_domain(domain);

	viommu_sync_req(vdomain->viommu);
}

static int viommu_iotlb_sync_map(struct iommu_domain *domain,
				 unsigned long iova, size_t size)
{
	struct viommu_domain *vdomain = to_viommu_domain(domain);

	/*
	 * May be called before the viommu is initialized including
	 * while creating direct mapping
	 */
	if (!vdomain->nr_endpoints)
		return 0;
	return viommu_sync_req(vdomain->viommu);
}

static void viommu_flush_iotlb_all(struct iommu_domain *domain)
{
	struct viommu_domain *vdomain = to_viommu_domain(domain);

	/*
	 * May be called before the viommu is initialized including
	 * while creating direct mapping
	 */
	if (!vdomain->nr_endpoints)
		return;
	viommu_sync_req(vdomain->viommu);
}

static void viommu_get_resv_regions(struct device *dev, struct list_head *head)
{
	struct iommu_resv_region *entry, *new_entry, *msi = NULL;
	struct viommu_endpoint *vdev = dev_iommu_priv_get(dev);
	int prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_MMIO;

	list_for_each_entry(entry, &vdev->resv_regions, list) {
		if (entry->type == IOMMU_RESV_MSI)
			msi = entry;

		new_entry = kmemdup(entry, sizeof(*entry), GFP_KERNEL);
		if (!new_entry)
			return;
		list_add_tail(&new_entry->list, head);
	}

	/*
	 * If the device didn't register any bypass MSI window, add a
	 * software-mapped region.
	 */
	if (!msi) {
		msi = iommu_alloc_resv_region(MSI_IOVA_BASE, MSI_IOVA_LENGTH,
					      prot, IOMMU_RESV_SW_MSI,
					      GFP_KERNEL);
		if (!msi)
			return;

		list_add_tail(&msi->list, head);
	}

	iommu_dma_get_resv_regions(dev, head);
}

static const struct bus_type *virtio_bus_type;

static int viommu_match_node(struct device *dev, const void *data)
{
	return device_match_fwnode(dev->parent, data);
}

static struct viommu_dev *viommu_get_by_fwnode(struct fwnode_handle *fwnode)
{
	struct device *dev = bus_find_device(virtio_bus_type, NULL, fwnode,
					     viommu_match_node);

	put_device(dev);

	return dev ? dev_to_virtio(dev)->priv : NULL;
}

static struct iommu_device *viommu_probe_device(struct device *dev)
{
	int ret;
	struct viommu_endpoint *vdev;
	struct viommu_dev *viommu = NULL;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	viommu = viommu_get_by_fwnode(fwspec->iommu_fwnode);
	if (!viommu)
		return ERR_PTR(-ENODEV);

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return ERR_PTR(-ENOMEM);

	vdev->dev = dev;
	vdev->viommu = viommu;
	INIT_LIST_HEAD(&vdev->resv_regions);
	dev_iommu_priv_set(dev, vdev);

	if (viommu->probe_size) {
		/* Get additional information for this endpoint */
		ret = viommu_probe_endpoint(viommu, dev);
		if (ret)
			goto err_free_dev;
	}

	return &viommu->iommu;

err_free_dev:
	iommu_put_resv_regions(dev, &vdev->resv_regions);
	kfree(vdev);

	return ERR_PTR(ret);
}

static void viommu_release_device(struct device *dev)
{
	struct viommu_endpoint *vdev = dev_iommu_priv_get(dev);

	viommu_detach_dev(vdev);
	iommu_put_resv_regions(dev, &vdev->resv_regions);
	kfree(vdev);
}

static struct iommu_group *viommu_device_group(struct device *dev)
{
	if (dev_is_pci(dev))
		return pci_device_group(dev);
	else
		return generic_device_group(dev);
}

static int viommu_of_xlate(struct device *dev,
			   const struct of_phandle_args *args)
{
	return iommu_fwspec_add_ids(dev, args->args, 1);
}

static bool viommu_capable(struct device *dev, enum iommu_cap cap)
{
	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		return true;
	case IOMMU_CAP_DEFERRED_FLUSH:
		return true;
	default:
		return false;
	}
}

static const struct iommu_ops viommu_ops = {
	.capable		= viommu_capable,
	.domain_alloc_identity	= viommu_domain_alloc_identity,
	.domain_alloc_paging	= viommu_domain_alloc_paging,
	.probe_device		= viommu_probe_device,
	.release_device		= viommu_release_device,
	.device_group		= viommu_device_group,
	.get_resv_regions	= viommu_get_resv_regions,
	.of_xlate		= viommu_of_xlate,
	.owner			= THIS_MODULE,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev		= viommu_attach_dev,
		.map_pages		= viommu_map_pages,
		.unmap_pages		= viommu_unmap_pages,
		.iova_to_phys		= viommu_iova_to_phys,
		.flush_iotlb_all	= viommu_flush_iotlb_all,
		.iotlb_sync		= viommu_iotlb_sync,
		.iotlb_sync_map		= viommu_iotlb_sync_map,
		.free			= viommu_domain_free,
	}
};

static int viommu_init_vqs(struct viommu_dev *viommu)
{
	struct virtio_device *vdev = dev_to_virtio(viommu->dev);
	struct virtqueue_info vqs_info[] = {
		{ "request" },
		{ "event", viommu_event_handler },
	};

	return virtio_find_vqs(vdev, VIOMMU_NR_VQS, viommu->vqs,
			       vqs_info, NULL);
}

static int viommu_fill_evtq(struct viommu_dev *viommu)
{
	int i, ret;
	struct scatterlist sg[1];
	struct viommu_event *evts;
	struct virtqueue *vq = viommu->vqs[VIOMMU_EVENT_VQ];
	size_t nr_evts = vq->num_free;

	viommu->evts = evts = devm_kmalloc_array(viommu->dev, nr_evts,
						 sizeof(*evts), GFP_KERNEL);
	if (!evts)
		return -ENOMEM;

	for (i = 0; i < nr_evts; i++) {
		sg_init_one(sg, &evts[i], sizeof(*evts));
		ret = virtqueue_add_inbuf(vq, sg, 1, &evts[i], GFP_KERNEL);
		if (ret)
			return ret;
	}

	return 0;
}

static int viommu_probe(struct virtio_device *vdev)
{
	struct device *parent_dev = vdev->dev.parent;
	struct viommu_dev *viommu = NULL;
	struct device *dev = &vdev->dev;
	u64 input_start = 0;
	u64 input_end = -1UL;
	int ret;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1) ||
	    !virtio_has_feature(vdev, VIRTIO_IOMMU_F_MAP_UNMAP))
		return -ENODEV;

	viommu = devm_kzalloc(dev, sizeof(*viommu), GFP_KERNEL);
	if (!viommu)
		return -ENOMEM;

	/* Borrow this for easy lookups later */
	virtio_bus_type = dev->bus;

	spin_lock_init(&viommu->request_lock);
	ida_init(&viommu->domain_ids);
	viommu->dev = dev;
	viommu->vdev = vdev;
	INIT_LIST_HEAD(&viommu->requests);

	ret = viommu_init_vqs(viommu);
	if (ret)
		return ret;

	virtio_cread_le(vdev, struct virtio_iommu_config, page_size_mask,
			&viommu->pgsize_bitmap);

	if (!viommu->pgsize_bitmap) {
		ret = -EINVAL;
		goto err_free_vqs;
	}

	viommu->map_flags = VIRTIO_IOMMU_MAP_F_READ | VIRTIO_IOMMU_MAP_F_WRITE;
	viommu->last_domain = ~0U;

	/* Optional features */
	virtio_cread_le_feature(vdev, VIRTIO_IOMMU_F_INPUT_RANGE,
				struct virtio_iommu_config, input_range.start,
				&input_start);

	virtio_cread_le_feature(vdev, VIRTIO_IOMMU_F_INPUT_RANGE,
				struct virtio_iommu_config, input_range.end,
				&input_end);

	virtio_cread_le_feature(vdev, VIRTIO_IOMMU_F_DOMAIN_RANGE,
				struct virtio_iommu_config, domain_range.start,
				&viommu->first_domain);

	virtio_cread_le_feature(vdev, VIRTIO_IOMMU_F_DOMAIN_RANGE,
				struct virtio_iommu_config, domain_range.end,
				&viommu->last_domain);

	virtio_cread_le_feature(vdev, VIRTIO_IOMMU_F_PROBE,
				struct virtio_iommu_config, probe_size,
				&viommu->probe_size);

	viommu->geometry = (struct iommu_domain_geometry) {
		.aperture_start	= input_start,
		.aperture_end	= input_end,
		.force_aperture	= true,
	};

	if (virtio_has_feature(vdev, VIRTIO_IOMMU_F_MMIO))
		viommu->map_flags |= VIRTIO_IOMMU_MAP_F_MMIO;

	/* Reserve an ID to use as the bypass domain */
	if (virtio_has_feature(viommu->vdev, VIRTIO_IOMMU_F_BYPASS_CONFIG)) {
		viommu->identity_domain_id = viommu->first_domain;
		viommu->first_domain++;
	}

	virtio_device_ready(vdev);

	/* Populate the event queue with buffers */
	ret = viommu_fill_evtq(viommu);
	if (ret)
		goto err_free_vqs;

	ret = iommu_device_sysfs_add(&viommu->iommu, dev, NULL, "%s",
				     virtio_bus_name(vdev));
	if (ret)
		goto err_free_vqs;

	vdev->priv = viommu;

	iommu_device_register(&viommu->iommu, &viommu_ops, parent_dev);

	dev_info(dev, "input address: %u bits\n",
		 order_base_2(viommu->geometry.aperture_end));
	dev_info(dev, "page mask: %#llx\n", viommu->pgsize_bitmap);

	return 0;

err_free_vqs:
	vdev->config->del_vqs(vdev);

	return ret;
}

static void viommu_remove(struct virtio_device *vdev)
{
	struct viommu_dev *viommu = vdev->priv;

	iommu_device_sysfs_remove(&viommu->iommu);
	iommu_device_unregister(&viommu->iommu);

	/* Stop all virtqueues */
	virtio_reset_device(vdev);
	vdev->config->del_vqs(vdev);

	dev_info(&vdev->dev, "device removed\n");
}

static void viommu_config_changed(struct virtio_device *vdev)
{
	dev_warn(&vdev->dev, "config changed\n");
}

static unsigned int features[] = {
	VIRTIO_IOMMU_F_MAP_UNMAP,
	VIRTIO_IOMMU_F_INPUT_RANGE,
	VIRTIO_IOMMU_F_DOMAIN_RANGE,
	VIRTIO_IOMMU_F_PROBE,
	VIRTIO_IOMMU_F_MMIO,
	VIRTIO_IOMMU_F_BYPASS_CONFIG,
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_IOMMU, VIRTIO_DEV_ANY_ID },
	{ 0 },
};
MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_iommu_drv = {
	.driver.name		= KBUILD_MODNAME,
	.id_table		= id_table,
	.feature_table		= features,
	.feature_table_size	= ARRAY_SIZE(features),
	.probe			= viommu_probe,
	.remove			= viommu_remove,
	.config_changed		= viommu_config_changed,
};

module_virtio_driver(virtio_iommu_drv);

MODULE_DESCRIPTION("Virtio IOMMU driver");
MODULE_AUTHOR("Jean-Philippe Brucker <jean-philippe.brucker@arm.com>");
MODULE_LICENSE("GPL v2");
