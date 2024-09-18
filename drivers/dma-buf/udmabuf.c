// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/memfd.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/shmem_fs.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>
#include <linux/udmabuf.h>
#include <linux/vmalloc.h>
#include <linux/iosys-map.h>

static int list_limit = 1024;
module_param(list_limit, int, 0644);
MODULE_PARM_DESC(list_limit, "udmabuf_create_list->count limit. Default is 1024.");

static int size_limit_mb = 64;
module_param(size_limit_mb, int, 0644);
MODULE_PARM_DESC(size_limit_mb, "Max size of a dmabuf, in megabytes. Default is 64.");

struct udmabuf {
	pgoff_t pagecount;
	struct folio **folios;
	struct sg_table *sg;
	struct miscdevice *device;
	pgoff_t *offsets;
	struct list_head unpin_list;
};

struct udmabuf_folio {
	struct folio *folio;
	struct list_head list;
};

static vm_fault_t udmabuf_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct udmabuf *ubuf = vma->vm_private_data;
	pgoff_t pgoff = vmf->pgoff;
	unsigned long pfn;

	if (pgoff >= ubuf->pagecount)
		return VM_FAULT_SIGBUS;

	pfn = folio_pfn(ubuf->folios[pgoff]);
	pfn += ubuf->offsets[pgoff] >> PAGE_SHIFT;

	return vmf_insert_pfn(vma, vmf->address, pfn);
}

static const struct vm_operations_struct udmabuf_vm_ops = {
	.fault = udmabuf_vm_fault,
};

static int mmap_udmabuf(struct dma_buf *buf, struct vm_area_struct *vma)
{
	struct udmabuf *ubuf = buf->priv;

	if ((vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) == 0)
		return -EINVAL;

	vma->vm_ops = &udmabuf_vm_ops;
	vma->vm_private_data = ubuf;
	vm_flags_set(vma, VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
	return 0;
}

static int vmap_udmabuf(struct dma_buf *buf, struct iosys_map *map)
{
	struct udmabuf *ubuf = buf->priv;
	struct page **pages;
	void *vaddr;
	pgoff_t pg;

	dma_resv_assert_held(buf->resv);

	pages = kvmalloc_array(ubuf->pagecount, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (pg = 0; pg < ubuf->pagecount; pg++)
		pages[pg] = &ubuf->folios[pg]->page;

	vaddr = vm_map_ram(pages, ubuf->pagecount, -1);
	kvfree(pages);
	if (!vaddr)
		return -EINVAL;

	iosys_map_set_vaddr(map, vaddr);
	return 0;
}

static void vunmap_udmabuf(struct dma_buf *buf, struct iosys_map *map)
{
	struct udmabuf *ubuf = buf->priv;

	dma_resv_assert_held(buf->resv);

	vm_unmap_ram(map->vaddr, ubuf->pagecount);
}

static struct sg_table *get_sg_table(struct device *dev, struct dma_buf *buf,
				     enum dma_data_direction direction)
{
	struct udmabuf *ubuf = buf->priv;
	struct sg_table *sg;
	struct scatterlist *sgl;
	unsigned int i = 0;
	int ret;

	sg = kzalloc(sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(sg, ubuf->pagecount, GFP_KERNEL);
	if (ret < 0)
		goto err_alloc;

	for_each_sg(sg->sgl, sgl, ubuf->pagecount, i)
		sg_set_folio(sgl, ubuf->folios[i], PAGE_SIZE,
			     ubuf->offsets[i]);

	ret = dma_map_sgtable(dev, sg, direction, 0);
	if (ret < 0)
		goto err_map;
	return sg;

err_map:
	sg_free_table(sg);
err_alloc:
	kfree(sg);
	return ERR_PTR(ret);
}

static void put_sg_table(struct device *dev, struct sg_table *sg,
			 enum dma_data_direction direction)
{
	dma_unmap_sgtable(dev, sg, direction, 0);
	sg_free_table(sg);
	kfree(sg);
}

static struct sg_table *map_udmabuf(struct dma_buf_attachment *at,
				    enum dma_data_direction direction)
{
	return get_sg_table(at->dev, at->dmabuf, direction);
}

static void unmap_udmabuf(struct dma_buf_attachment *at,
			  struct sg_table *sg,
			  enum dma_data_direction direction)
{
	return put_sg_table(at->dev, sg, direction);
}

static void unpin_all_folios(struct list_head *unpin_list)
{
	struct udmabuf_folio *ubuf_folio;

	while (!list_empty(unpin_list)) {
		ubuf_folio = list_first_entry(unpin_list,
					      struct udmabuf_folio, list);
		unpin_folio(ubuf_folio->folio);

		list_del(&ubuf_folio->list);
		kfree(ubuf_folio);
	}
}

static int add_to_unpin_list(struct list_head *unpin_list,
			     struct folio *folio)
{
	struct udmabuf_folio *ubuf_folio;

	ubuf_folio = kzalloc(sizeof(*ubuf_folio), GFP_KERNEL);
	if (!ubuf_folio)
		return -ENOMEM;

	ubuf_folio->folio = folio;
	list_add_tail(&ubuf_folio->list, unpin_list);
	return 0;
}

static void release_udmabuf(struct dma_buf *buf)
{
	struct udmabuf *ubuf = buf->priv;
	struct device *dev = ubuf->device->this_device;

	if (ubuf->sg)
		put_sg_table(dev, ubuf->sg, DMA_BIDIRECTIONAL);

	unpin_all_folios(&ubuf->unpin_list);
	kvfree(ubuf->offsets);
	kvfree(ubuf->folios);
	kfree(ubuf);
}

static int begin_cpu_udmabuf(struct dma_buf *buf,
			     enum dma_data_direction direction)
{
	struct udmabuf *ubuf = buf->priv;
	struct device *dev = ubuf->device->this_device;
	int ret = 0;

	if (!ubuf->sg) {
		ubuf->sg = get_sg_table(dev, buf, direction);
		if (IS_ERR(ubuf->sg)) {
			ret = PTR_ERR(ubuf->sg);
			ubuf->sg = NULL;
		}
	} else {
		dma_sync_sg_for_cpu(dev, ubuf->sg->sgl, ubuf->sg->nents,
				    direction);
	}

	return ret;
}

static int end_cpu_udmabuf(struct dma_buf *buf,
			   enum dma_data_direction direction)
{
	struct udmabuf *ubuf = buf->priv;
	struct device *dev = ubuf->device->this_device;

	if (!ubuf->sg)
		return -EINVAL;

	dma_sync_sg_for_device(dev, ubuf->sg->sgl, ubuf->sg->nents, direction);
	return 0;
}

static const struct dma_buf_ops udmabuf_ops = {
	.cache_sgt_mapping = true,
	.map_dma_buf	   = map_udmabuf,
	.unmap_dma_buf	   = unmap_udmabuf,
	.release	   = release_udmabuf,
	.mmap		   = mmap_udmabuf,
	.vmap		   = vmap_udmabuf,
	.vunmap		   = vunmap_udmabuf,
	.begin_cpu_access  = begin_cpu_udmabuf,
	.end_cpu_access    = end_cpu_udmabuf,
};

#define SEALS_WANTED (F_SEAL_SHRINK)
#define SEALS_DENIED (F_SEAL_WRITE)

static int check_memfd_seals(struct file *memfd)
{
	int seals;

	if (!memfd)
		return -EBADFD;

	if (!shmem_file(memfd) && !is_file_hugepages(memfd))
		return -EBADFD;

	seals = memfd_fcntl(memfd, F_GET_SEALS, 0);
	if (seals == -EINVAL)
		return -EBADFD;

	if ((seals & SEALS_WANTED) != SEALS_WANTED ||
	    (seals & SEALS_DENIED) != 0)
		return -EINVAL;

	return 0;
}

static int export_udmabuf(struct udmabuf *ubuf,
			  struct miscdevice *device,
			  u32 flags)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *buf;

	ubuf->device = device;
	exp_info.ops  = &udmabuf_ops;
	exp_info.size = ubuf->pagecount << PAGE_SHIFT;
	exp_info.priv = ubuf;
	exp_info.flags = O_RDWR;

	buf = dma_buf_export(&exp_info);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	return dma_buf_fd(buf, flags);
}

static long udmabuf_create(struct miscdevice *device,
			   struct udmabuf_create_list *head,
			   struct udmabuf_create_item *list)
{
	pgoff_t pgoff, pgcnt, pglimit, pgbuf = 0;
	long nr_folios, ret = -EINVAL;
	struct file *memfd = NULL;
	struct folio **folios;
	struct udmabuf *ubuf;
	u32 i, j, k, flags;
	loff_t end;

	ubuf = kzalloc(sizeof(*ubuf), GFP_KERNEL);
	if (!ubuf)
		return -ENOMEM;

	INIT_LIST_HEAD(&ubuf->unpin_list);
	pglimit = (size_limit_mb * 1024 * 1024) >> PAGE_SHIFT;
	for (i = 0; i < head->count; i++) {
		if (!IS_ALIGNED(list[i].offset, PAGE_SIZE))
			goto err;
		if (!IS_ALIGNED(list[i].size, PAGE_SIZE))
			goto err;
		ubuf->pagecount += list[i].size >> PAGE_SHIFT;
		if (ubuf->pagecount > pglimit)
			goto err;
	}

	if (!ubuf->pagecount)
		goto err;

	ubuf->folios = kvmalloc_array(ubuf->pagecount, sizeof(*ubuf->folios),
				      GFP_KERNEL);
	if (!ubuf->folios) {
		ret = -ENOMEM;
		goto err;
	}
	ubuf->offsets = kvcalloc(ubuf->pagecount, sizeof(*ubuf->offsets),
				 GFP_KERNEL);
	if (!ubuf->offsets) {
		ret = -ENOMEM;
		goto err;
	}

	pgbuf = 0;
	for (i = 0; i < head->count; i++) {
		memfd = fget(list[i].memfd);
		ret = check_memfd_seals(memfd);
		if (ret < 0)
			goto err;

		pgcnt = list[i].size >> PAGE_SHIFT;
		folios = kvmalloc_array(pgcnt, sizeof(*folios), GFP_KERNEL);
		if (!folios) {
			ret = -ENOMEM;
			goto err;
		}

		end = list[i].offset + (pgcnt << PAGE_SHIFT) - 1;
		ret = memfd_pin_folios(memfd, list[i].offset, end,
				       folios, pgcnt, &pgoff);
		if (ret <= 0) {
			kvfree(folios);
			if (!ret)
				ret = -EINVAL;
			goto err;
		}

		nr_folios = ret;
		pgoff >>= PAGE_SHIFT;
		for (j = 0, k = 0; j < pgcnt; j++) {
			ubuf->folios[pgbuf] = folios[k];
			ubuf->offsets[pgbuf] = pgoff << PAGE_SHIFT;

			if (j == 0 || ubuf->folios[pgbuf-1] != folios[k]) {
				ret = add_to_unpin_list(&ubuf->unpin_list,
							folios[k]);
				if (ret < 0) {
					kfree(folios);
					goto err;
				}
			}

			pgbuf++;
			if (++pgoff == folio_nr_pages(folios[k])) {
				pgoff = 0;
				if (++k == nr_folios)
					break;
			}
		}

		kvfree(folios);
		fput(memfd);
		memfd = NULL;
	}

	flags = head->flags & UDMABUF_FLAGS_CLOEXEC ? O_CLOEXEC : 0;
	ret = export_udmabuf(ubuf, device, flags);
	if (ret < 0)
		goto err;

	return ret;

err:
	if (memfd)
		fput(memfd);
	unpin_all_folios(&ubuf->unpin_list);
	kvfree(ubuf->offsets);
	kvfree(ubuf->folios);
	kfree(ubuf);
	return ret;
}

static long udmabuf_ioctl_create(struct file *filp, unsigned long arg)
{
	struct udmabuf_create create;
	struct udmabuf_create_list head;
	struct udmabuf_create_item list;

	if (copy_from_user(&create, (void __user *)arg,
			   sizeof(create)))
		return -EFAULT;

	head.flags  = create.flags;
	head.count  = 1;
	list.memfd  = create.memfd;
	list.offset = create.offset;
	list.size   = create.size;

	return udmabuf_create(filp->private_data, &head, &list);
}

static long udmabuf_ioctl_create_list(struct file *filp, unsigned long arg)
{
	struct udmabuf_create_list head;
	struct udmabuf_create_item *list;
	int ret = -EINVAL;
	u32 lsize;

	if (copy_from_user(&head, (void __user *)arg, sizeof(head)))
		return -EFAULT;
	if (head.count > list_limit)
		return -EINVAL;
	lsize = sizeof(struct udmabuf_create_item) * head.count;
	list = memdup_user((void __user *)(arg + sizeof(head)), lsize);
	if (IS_ERR(list))
		return PTR_ERR(list);

	ret = udmabuf_create(filp->private_data, &head, list);
	kfree(list);
	return ret;
}

static long udmabuf_ioctl(struct file *filp, unsigned int ioctl,
			  unsigned long arg)
{
	long ret;

	switch (ioctl) {
	case UDMABUF_CREATE:
		ret = udmabuf_ioctl_create(filp, arg);
		break;
	case UDMABUF_CREATE_LIST:
		ret = udmabuf_ioctl_create_list(filp, arg);
		break;
	default:
		ret = -ENOTTY;
		break;
	}
	return ret;
}

static const struct file_operations udmabuf_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl = udmabuf_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = udmabuf_ioctl,
#endif
};

static struct miscdevice udmabuf_misc = {
	.minor          = MISC_DYNAMIC_MINOR,
	.name           = "udmabuf",
	.fops           = &udmabuf_fops,
};

static int __init udmabuf_dev_init(void)
{
	int ret;

	ret = misc_register(&udmabuf_misc);
	if (ret < 0) {
		pr_err("Could not initialize udmabuf device\n");
		return ret;
	}

	ret = dma_coerce_mask_and_coherent(udmabuf_misc.this_device,
					   DMA_BIT_MASK(64));
	if (ret < 0) {
		pr_err("Could not setup DMA mask for udmabuf device\n");
		misc_deregister(&udmabuf_misc);
		return ret;
	}

	return 0;
}

static void __exit udmabuf_dev_exit(void)
{
	misc_deregister(&udmabuf_misc);
}

module_init(udmabuf_dev_init)
module_exit(udmabuf_dev_exit)

MODULE_AUTHOR("Gerd Hoffmann <kraxel@redhat.com>");
