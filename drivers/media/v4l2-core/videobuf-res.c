/*******************************************************************
 *
 *  Copyright C 2012 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2012/9/6   16:46
 *
 *******************************************************************/


#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <media/videobuf-res.h>
#include <linux/io.h>

struct videobuf_res_memory {
	u32 magic;
	void *vaddr;
	resource_size_t phy_addr;
	unsigned long size;
};

static int debug;
module_param(debug, int, 0644);

#define MAGIC_RE_MEM 0x123039dc
#define MAGIC_CHECK(is, should)						    \
	if (unlikely((is) != (should)))	{				    \
		printk(KERN_ERR "magic mismatch: %x expected %x\n", (is), (should)); \
		BUG();							    \
	}

#define dprintk(level, fmt, arg...)					\
	if (debug >= level)						\
		printk(KERN_DEBUG "vbuf-resource: " fmt , ## arg)

static void* res_alloc(struct videobuf_queue *q,size_t boff,unsigned long size, resource_size_t* phy_addr)
{
	void __iomem *ret = NULL; 
	struct videobuf_res_privdata *res = NULL;
	long res_size = 0;

	BUG_ON(!size);

	BUG_ON(!q->priv_data);

	res  = (struct videobuf_res_privdata *)q->priv_data;
	MAGIC_CHECK(res->magic, MAGIC_RE_MEM);

	res_size = res->end-res->start+1;
	if(boff+size<=res_size){
		*phy_addr = res->start+boff;
		//ret = ioremap_wc(*phy_addr,size);
		//if(!ret)
		//	*phy_addr = 0;
	}else{
		printk(KERN_ERR "videobuf_res alloc buff is too small: %lx expected %lx\n", res_size, boff+size);
	}
	return (void*)ret;
}

static void res_free(struct videobuf_res_memory *mem)
{
	if(mem->vaddr)
		iounmap((void __iomem *)mem->vaddr);
	mem->vaddr = NULL;
	mem->size = 0;
	mem->phy_addr = 0;
	return;
}

static void
videobuf_vm_open(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;

	dprintk(2,"vm_open %p [count=%u,vma=%08lx-%08lx]\n",
		map, map->count, vma->vm_start, vma->vm_end);

	map->count++;
}

static void videobuf_vm_close(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;
	struct videobuf_queue *q = map->q;
	int i;

	dprintk(2,"vm_close %p [count=%u,vma=%08lx-%08lx]\n",
		map, map->count, vma->vm_start, vma->vm_end);

	map->count--;
	if (0 == map->count) {
		struct videobuf_res_memory *mem;

		dprintk(1,"munmap %p q=%p\n", map, q);
		videobuf_queue_lock(q);

		/* We need first to cancel streams, before unmapping */
		if (q->streaming)
			videobuf_queue_cancel(q);

		for (i = 0; i < VIDEO_MAX_FRAME; i++) {
			if (NULL == q->bufs[i])
				continue;

			if (q->bufs[i]->map != map)
				continue;

			mem = q->bufs[i]->priv;
			if (mem) {
				/* This callback is called only if kernel has
				   allocated memory and this memory is mmapped.
				   In this case, memory should be freed,
				   in order to do memory unmap.
				 */

				MAGIC_CHECK(mem->magic, MAGIC_RE_MEM);

				dprintk(1,"buf[%d] freeing %p\n",
					i, mem->vaddr);

				res_free(mem);
			}

			q->bufs[i]->map   = NULL;
			q->bufs[i]->baddr = 0;
		}

		kfree(map);

		videobuf_queue_unlock(q);
	}
}

static const struct vm_operations_struct videobuf_vm_ops = {
	.open     = videobuf_vm_open,
	.close    = videobuf_vm_close,
};

static struct videobuf_buffer *__videobuf_alloc_vb(size_t size)
{
	struct videobuf_res_memory *mem;
	struct videobuf_buffer *vb;

	vb = kzalloc(size + sizeof(*mem), GFP_KERNEL);
	if (vb) {
		mem = vb->priv = ((char *)vb) + size;
		mem->magic = MAGIC_RE_MEM;

		dprintk(1, "%s: allocated at %p(%ld+%ld) & %p(%ld)\n",
			__func__, vb, (long)sizeof(*vb), (long)size - sizeof(*vb),
			mem, (long)sizeof(*mem));
	}

	return vb;
}

static void *__videobuf_to_vaddr(struct videobuf_buffer *buf)
{
	struct videobuf_res_memory *mem = buf->priv;

	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_RE_MEM);

	return mem->vaddr;
}

static int __videobuf_iolock(struct videobuf_queue *q,
			     struct videobuf_buffer *vb,
			     struct v4l2_framebuffer *fbuf)
{
	struct videobuf_res_memory *mem = vb->priv;

	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_RE_MEM);

	switch (vb->memory) {
	case V4L2_MEMORY_MMAP:
		dprintk(1,"%s memory method MMAP\n", __func__);

		/* All handling should be done by __videobuf_mmap_mapper() */
		if (!mem->phy_addr) {
			printk(KERN_ERR "%s memory is not alloced/mmapped.\n",__func__);
			return -EINVAL;
		}
		break;
	case V4L2_MEMORY_USERPTR:
		dprintk(1,"%s memory method USERPTR\n", __func__);
		return -EINVAL;
	case V4L2_MEMORY_OVERLAY:
	default:
		dprintk(1,"%s memory method OVERLAY/unknown\n",
			__func__);
		return -EINVAL;
	}

	return 0;
}

static int __videobuf_mmap_mapper(struct videobuf_queue *q,
				  struct videobuf_buffer *buf,
				  struct vm_area_struct *vma)
{
	struct videobuf_res_memory *mem;
	struct videobuf_mapping *map;
	int retval;
	unsigned long size;

	dprintk(2,"%s\n", __func__);

	/* create mapping + update buffer list */
	map = kzalloc(sizeof(struct videobuf_mapping), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	buf->map = map;
	map->q = q;

	buf->baddr = vma->vm_start;

	mem = buf->priv;
	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_RE_MEM);

	mem->size = PAGE_ALIGN(buf->bsize);
	mem->vaddr = res_alloc(q,buf->boff,mem->size, &mem->phy_addr);

	//if ((!mem->vaddr)||(!mem->phy_addr)){
	if (!mem->phy_addr){
		printk(KERN_ERR  "res_alloc size %ld failed\n",
			mem->size);
		goto error;
	}
	dprintk(1,"res_alloc data is at addr 0x%x (size %ld)\n",
		mem->phy_addr, mem->size);

	/* Try to remap memory */

	size = vma->vm_end - vma->vm_start;
	size = (size < mem->size) ? size : mem->size;

	//vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	//vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	retval = remap_pfn_range(vma, vma->vm_start,
				 mem->phy_addr >> PAGE_SHIFT,
				 size, vma->vm_page_prot);
	if (retval) {
		printk(KERN_ERR "mmap: remap failed with error %d. ", retval);
		res_free(mem);
		goto error;
	}

	vma->vm_ops          = &videobuf_vm_ops;
	vma->vm_flags       |= (VM_DONTEXPAND| VM_IO | VM_RESERVED);
	vma->vm_private_data = map;

	dprintk(1,"mmap %p: q=%p %08lx-%08lx (%lx) pgoff %08lx buf %d, vm flag 0x%lx\n",
		map, q, vma->vm_start, vma->vm_end,
		(long int)buf->bsize,
		vma->vm_pgoff, buf->i,vma->vm_flags);

	videobuf_vm_open(vma);

	return 0;

error:
	kfree(map);
	return -ENOMEM;
}

static struct videobuf_qtype_ops qops = {
	.magic        = MAGIC_QTYPE_OPS,

	.alloc_vb     = __videobuf_alloc_vb,
	.iolock       = __videobuf_iolock,
	.mmap_mapper  = __videobuf_mmap_mapper,
	.vaddr        = __videobuf_to_vaddr,
};

void videobuf_queue_res_init(struct videobuf_queue *q,
				    const struct videobuf_queue_ops *ops,
				    struct device *dev,
				    spinlock_t *irqlock,
				    enum v4l2_buf_type type,
				    enum v4l2_field field,
				    unsigned int msize,
				    void *priv,
				    struct mutex *ext_lock)
{
	struct videobuf_res_privdata* res = (struct videobuf_res_privdata*)priv;
	
	BUG_ON(!res);
	MAGIC_CHECK(res->magic, MAGIC_RE_MEM);

	if(res->start>=res->end){
		printk(KERN_ERR "videobuf_queue_res_init: resource is invalid.\n");
		return;
	}	
	videobuf_queue_core_init(q, ops, dev, irqlock, type, field, msize,
	 	priv, &qops, ext_lock);
	return;
}
EXPORT_SYMBOL_GPL(videobuf_queue_res_init);

resource_size_t videobuf_to_res(struct videobuf_buffer *buf)
{
	struct videobuf_res_memory *mem = buf->priv;

	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_RE_MEM);

	return mem->phy_addr;
}
EXPORT_SYMBOL_GPL(videobuf_to_res);

void videobuf_res_free(struct videobuf_queue *q,
			      struct videobuf_buffer *buf)
{
	struct videobuf_res_memory *mem = buf->priv;

	/* mmapped memory can't be freed here, otherwise mmapped region
	   would be released, while still needed. In this case, the memory
	   release should happen inside videobuf_vm_close().
	   So, it should free memory only if the memory were allocated for
	   read() operation.
	 */
	if (buf->memory != V4L2_MEMORY_USERPTR)
		return;

	if (!mem)
		return;

	MAGIC_CHECK(mem->magic, MAGIC_RE_MEM);

	/* handle user space pointer case */
	if (buf->baddr) {
		return;
	}

	/* read() method */
	res_free(mem);
	return;
}
EXPORT_SYMBOL_GPL(videobuf_res_free);

MODULE_DESCRIPTION("helper module to manage video4linux resource buffers");
MODULE_AUTHOR("Amlogic");
MODULE_LICENSE("GPL");
