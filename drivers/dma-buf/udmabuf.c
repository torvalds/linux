// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/memfd.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/udmabuf.h>
#include <linux/hugetlb.h>

static int list_limit = 1024;
module_param(list_limit, int, 0644);
MODULE_PARM_DESC(list_limit, "udmabuf_create_list->count limit. Default is 1024.");

static int size_limit_mb = 64;
module_param(size_limit_mb, int, 0644);
MODULE_PARM_DESC(size_limit_mb, "Max size of a dmabuf, in megabytes. Default is 64.");

struct udmabuf {
	pgoff_t pagecount;
	struct page **pages;
	struct sg_table *sg;
	struct miscdevice *device;
};

static vm_fault_t udmabuf_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct udmabuf *ubuf = vma->vm_private_data;
	pgoff_t pgoff = vmf->pgoff;

	if (pgoff >= ubuf->pagecount)
		return VM_FAULT_SIGBUS;
	vmf->page = ubuf->pages[pgoff];
	get_page(vmf->page);
	return 0;
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
	return 0;
}

static struct sg_table *get_sg_table(struct device *dev, struct dma_buf *buf,
				     enum dma_data_direction direction)
{
	struct udmabuf *ubuf = buf->priv;
	struct sg_table *sg;
	int ret;

	sg = kzalloc(sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return ERR_PTR(-ENOMEM);
	ret = sg_alloc_table_from_pages(sg, ubuf->pages, ubuf->pagecount,
					0, ubuf->pagecount << PAGE_SHIFT,
					GFP_KERNEL);
	if (ret < 0)
		goto err;
	ret = dma_map_sgtable(dev, sg, direction, 0);
	if (ret < 0)
		goto err;
	return sg;

err:
	sg_free_table(sg);
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

static void release_udmabuf(struct dma_buf *buf)
{
	struct udmabuf *ubuf = buf->priv;
	struct device *dev = ubuf->device->this_device;
	pgoff_t pg;

	if (ubuf->sg)
		put_sg_table(dev, ubuf->sg, DMA_BIDIRECTIONAL);

	for (pg = 0; pg < ubuf->pagecount; pg++)
		put_page(ubuf->pages[pg]);
	kfree(ubuf->pages);
	kfree(ubuf);
}

static int begin_cpu_udmabuf(struct dma_buf *buf,
			     enum dma_data_direction direction)
{
	struct udmabuf *ubuf = buf->priv;
	struct device *dev = ubuf->device->this_device;

	if (!ubuf->sg) {
		ubuf->sg = get_sg_table(dev, buf, direction);
		if (IS_ERR(ubuf->sg))
			return PTR_ERR(ubuf->sg);
	} else {
		dma_sync_sg_for_cpu(dev, ubuf->sg->sgl, ubuf->sg->nents,
				    direction);
	}

	return 0;
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
	.begin_cpu_access  = begin_cpu_udmabuf,
	.end_cpu_access    = end_cpu_udmabuf,
};

#define SEALS_WANTED (F_SEAL_SHRINK)
#define SEALS_DENIED (F_SEAL_WRITE)

static long udmabuf_create(struct miscdevice *device,
			   struct udmabuf_create_list *head,
			   struct udmabuf_create_item *list)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct file *memfd = NULL;
	struct address_space *mapping = NULL;
	struct udmabuf *ubuf;
	struct dma_buf *buf;
	pgoff_t pgoff, pgcnt, pgidx, pgbuf = 0, pglimit;
	struct page *page, *hpage = NULL;
	pgoff_t subpgoff, maxsubpgs;
	struct hstate *hpstate;
	int seals, ret = -EINVAL;
	u32 i, flags;

	ubuf = kzalloc(sizeof(*ubuf), GFP_KERNEL);
	if (!ubuf)
		return -ENOMEM;

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

	ubuf->pages = kmalloc_array(ubuf->pagecount, sizeof(*ubuf->pages),
				    GFP_KERNEL);
	if (!ubuf->pages) {
		ret = -ENOMEM;
		goto err;
	}

	pgbuf = 0;
	for (i = 0; i < head->count; i++) {
		ret = -EBADFD;
		memfd = fget(list[i].memfd);
		if (!memfd)
			goto err;
		mapping = file_inode(memfd)->i_mapping;
		if (!shmem_mapping(mapping) && !is_file_hugepages(memfd))
			goto err;
		seals = memfd_fcntl(memfd, F_GET_SEALS, 0);
		if (seals == -EINVAL)
			goto err;
		ret = -EINVAL;
		if ((seals & SEALS_WANTED) != SEALS_WANTED ||
		    (seals & SEALS_DENIED) != 0)
			goto err;
		pgoff = list[i].offset >> PAGE_SHIFT;
		pgcnt = list[i].size   >> PAGE_SHIFT;
		if (is_file_hugepages(memfd)) {
			hpstate = hstate_file(memfd);
			pgoff = list[i].offset >> huge_page_shift(hpstate);
			subpgoff = (list[i].offset &
				    ~huge_page_mask(hpstate)) >> PAGE_SHIFT;
			maxsubpgs = huge_page_size(hpstate) >> PAGE_SHIFT;
		}
		for (pgidx = 0; pgidx < pgcnt; pgidx++) {
			if (is_file_hugepages(memfd)) {
				if (!hpage) {
					hpage = find_get_page_flags(mapping, pgoff,
								    FGP_ACCESSED);
					if (!hpage) {
						ret = -EINVAL;
						goto err;
					}
				}
				page = hpage + subpgoff;
				get_page(page);
				subpgoff++;
				if (subpgoff == maxsubpgs) {
					put_page(hpage);
					hpage = NULL;
					subpgoff = 0;
					pgoff++;
				}
			} else {
				page = shmem_read_mapping_page(mapping,
							       pgoff + pgidx);
				if (IS_ERR(page)) {
					ret = PTR_ERR(page);
					goto err;
				}
			}
			ubuf->pages[pgbuf++] = page;
		}
		fput(memfd);
		memfd = NULL;
		if (hpage) {
			put_page(hpage);
			hpage = NULL;
		}
	}

	exp_info.ops  = &udmabuf_ops;
	exp_info.size = ubuf->pagecount << PAGE_SHIFT;
	exp_info.priv = ubuf;
	exp_info.flags = O_RDWR;

	ubuf->device = device;
	buf = dma_buf_export(&exp_info);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		goto err;
	}

	flags = 0;
	if (head->flags & UDMABUF_FLAGS_CLOEXEC)
		flags |= O_CLOEXEC;
	return dma_buf_fd(buf, flags);

err:
	while (pgbuf > 0)
		put_page(ubuf->pages[--pgbuf]);
	if (memfd)
		fput(memfd);
	kfree(ubuf->pages);
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
	return misc_register(&udmabuf_misc);
}

static void __exit udmabuf_dev_exit(void)
{
	misc_deregister(&udmabuf_misc);
}

module_init(udmabuf_dev_init)
module_exit(udmabuf_dev_exit)

MODULE_AUTHOR("Gerd Hoffmann <kraxel@redhat.com>");
MODULE_LICENSE("GPL v2");
