// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2018, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/sort.h>
#include <linux/of_platform.h>
#include <linux/rpmsg.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <uapi/misc/fastrpc.h>

#define ADSP_DOMAIN_ID (0)
#define MDSP_DOMAIN_ID (1)
#define SDSP_DOMAIN_ID (2)
#define CDSP_DOMAIN_ID (3)
#define FASTRPC_DEV_MAX		4 /* adsp, mdsp, slpi, cdsp*/
#define FASTRPC_MAX_SESSIONS	9 /*8 compute, 1 cpz*/
#define FASTRPC_ALIGN		128
#define FASTRPC_MAX_FDLIST	16
#define FASTRPC_MAX_CRCLIST	64
#define FASTRPC_PHYS(p)	((p) & 0xffffffff)
#define FASTRPC_CTX_MAX (256)
#define FASTRPC_INIT_HANDLE	1
#define FASTRPC_CTXID_MASK (0xFF0)
#define INIT_FILELEN_MAX (2 * 1024 * 1024)
#define FASTRPC_DEVICE_NAME	"fastrpc"
#define ADSP_MMAP_ADD_PAGES 0x1000

/* Retrives number of input buffers from the scalars parameter */
#define REMOTE_SCALARS_INBUFS(sc)	(((sc) >> 16) & 0x0ff)

/* Retrives number of output buffers from the scalars parameter */
#define REMOTE_SCALARS_OUTBUFS(sc)	(((sc) >> 8) & 0x0ff)

/* Retrives number of input handles from the scalars parameter */
#define REMOTE_SCALARS_INHANDLES(sc)	(((sc) >> 4) & 0x0f)

/* Retrives number of output handles from the scalars parameter */
#define REMOTE_SCALARS_OUTHANDLES(sc)	((sc) & 0x0f)

#define REMOTE_SCALARS_LENGTH(sc)	(REMOTE_SCALARS_INBUFS(sc) +   \
					 REMOTE_SCALARS_OUTBUFS(sc) +  \
					 REMOTE_SCALARS_INHANDLES(sc)+ \
					 REMOTE_SCALARS_OUTHANDLES(sc))
#define FASTRPC_BUILD_SCALARS(attr, method, in, out, oin, oout)  \
				(((attr & 0x07) << 29) |		\
				((method & 0x1f) << 24) |	\
				((in & 0xff) << 16) |		\
				((out & 0xff) <<  8) |		\
				((oin & 0x0f) <<  4) |		\
				(oout & 0x0f))

#define FASTRPC_SCALARS(method, in, out) \
		FASTRPC_BUILD_SCALARS(0, method, in, out, 0, 0)

#define FASTRPC_CREATE_PROCESS_NARGS	6
/* Remote Method id table */
#define FASTRPC_RMID_INIT_ATTACH	0
#define FASTRPC_RMID_INIT_RELEASE	1
#define FASTRPC_RMID_INIT_MMAP		4
#define FASTRPC_RMID_INIT_MUNMAP	5
#define FASTRPC_RMID_INIT_CREATE	6
#define FASTRPC_RMID_INIT_CREATE_ATTR	7
#define FASTRPC_RMID_INIT_CREATE_STATIC	8

/* Protection Domain(PD) ids */
#define AUDIO_PD	(0) /* also GUEST_OS PD? */
#define USER_PD		(1)
#define SENSORS_PD	(2)

#define miscdev_to_cctx(d) container_of(d, struct fastrpc_channel_ctx, miscdev)

static const char *domains[FASTRPC_DEV_MAX] = { "adsp", "mdsp",
						"sdsp", "cdsp"};
struct fastrpc_phy_page {
	u64 addr;		/* physical address */
	u64 size;		/* size of contiguous region */
};

struct fastrpc_invoke_buf {
	u32 num;		/* number of contiguous regions */
	u32 pgidx;		/* index to start of contiguous region */
};

struct fastrpc_remote_arg {
	u64 pv;
	u64 len;
};

struct fastrpc_mmap_rsp_msg {
	u64 vaddr;
};

struct fastrpc_mmap_req_msg {
	s32 pgid;
	u32 flags;
	u64 vaddr;
	s32 num;
};

struct fastrpc_munmap_req_msg {
	s32 pgid;
	u64 vaddr;
	u64 size;
};

struct fastrpc_msg {
	int pid;		/* process group id */
	int tid;		/* thread id */
	u64 ctx;		/* invoke caller context */
	u32 handle;	/* handle to invoke */
	u32 sc;		/* scalars structure describing the data */
	u64 addr;		/* physical address */
	u64 size;		/* size of contiguous region */
};

struct fastrpc_invoke_rsp {
	u64 ctx;		/* invoke caller context */
	int retval;		/* invoke return value */
};

struct fastrpc_buf_overlap {
	u64 start;
	u64 end;
	int raix;
	u64 mstart;
	u64 mend;
	u64 offset;
};

struct fastrpc_buf {
	struct fastrpc_user *fl;
	struct dma_buf *dmabuf;
	struct device *dev;
	void *virt;
	u64 phys;
	u64 size;
	/* Lock for dma buf attachments */
	struct mutex lock;
	struct list_head attachments;
	/* mmap support */
	struct list_head node; /* list of user requested mmaps */
	uintptr_t raddr;
};

struct fastrpc_dma_buf_attachment {
	struct device *dev;
	struct sg_table sgt;
	struct list_head node;
};

struct fastrpc_map {
	struct list_head node;
	struct fastrpc_user *fl;
	int fd;
	struct dma_buf *buf;
	struct sg_table *table;
	struct dma_buf_attachment *attach;
	u64 phys;
	u64 size;
	void *va;
	u64 len;
	struct kref refcount;
};

struct fastrpc_invoke_ctx {
	int nscalars;
	int nbufs;
	int retval;
	int pid;
	int tgid;
	u32 sc;
	u32 *crc;
	u64 ctxid;
	u64 msg_sz;
	struct kref refcount;
	struct list_head node; /* list of ctxs */
	struct completion work;
	struct work_struct put_work;
	struct fastrpc_msg msg;
	struct fastrpc_user *fl;
	struct fastrpc_remote_arg *rpra;
	struct fastrpc_map **maps;
	struct fastrpc_buf *buf;
	struct fastrpc_invoke_args *args;
	struct fastrpc_buf_overlap *olaps;
	struct fastrpc_channel_ctx *cctx;
};

struct fastrpc_session_ctx {
	struct device *dev;
	int sid;
	bool used;
	bool valid;
};

struct fastrpc_channel_ctx {
	int domain_id;
	int sesscount;
	struct rpmsg_device *rpdev;
	struct fastrpc_session_ctx session[FASTRPC_MAX_SESSIONS];
	spinlock_t lock;
	struct idr ctx_idr;
	struct list_head users;
	struct miscdevice miscdev;
	struct kref refcount;
};

struct fastrpc_user {
	struct list_head user;
	struct list_head maps;
	struct list_head pending;
	struct list_head mmaps;

	struct fastrpc_channel_ctx *cctx;
	struct fastrpc_session_ctx *sctx;
	struct fastrpc_buf *init_mem;

	int tgid;
	int pd;
	/* Lock for lists */
	spinlock_t lock;
	/* lock for allocations */
	struct mutex mutex;
};

static void fastrpc_free_map(struct kref *ref)
{
	struct fastrpc_map *map;

	map = container_of(ref, struct fastrpc_map, refcount);

	if (map->table) {
		dma_buf_unmap_attachment(map->attach, map->table,
					 DMA_BIDIRECTIONAL);
		dma_buf_detach(map->buf, map->attach);
		dma_buf_put(map->buf);
	}

	if (map->fl) {
		spin_lock(&map->fl->lock);
		list_del(&map->node);
		spin_unlock(&map->fl->lock);
		map->fl = NULL;
	}

	kfree(map);
}

static void fastrpc_map_put(struct fastrpc_map *map)
{
	if (map)
		kref_put(&map->refcount, fastrpc_free_map);
}

static int fastrpc_map_get(struct fastrpc_map *map)
{
	if (!map)
		return -ENOENT;

	return kref_get_unless_zero(&map->refcount) ? 0 : -ENOENT;
}

static int fastrpc_map_find(struct fastrpc_user *fl, int fd,
			    struct fastrpc_map **ppmap)
{
	struct fastrpc_map *map = NULL;

	mutex_lock(&fl->mutex);
	list_for_each_entry(map, &fl->maps, node) {
		if (map->fd == fd) {
			fastrpc_map_get(map);
			*ppmap = map;
			mutex_unlock(&fl->mutex);
			return 0;
		}
	}
	mutex_unlock(&fl->mutex);

	return -ENOENT;
}

static void fastrpc_buf_free(struct fastrpc_buf *buf)
{
	dma_free_coherent(buf->dev, buf->size, buf->virt,
			  FASTRPC_PHYS(buf->phys));
	kfree(buf);
}

static int fastrpc_buf_alloc(struct fastrpc_user *fl, struct device *dev,
			     u64 size, struct fastrpc_buf **obuf)
{
	struct fastrpc_buf *buf;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	INIT_LIST_HEAD(&buf->attachments);
	INIT_LIST_HEAD(&buf->node);
	mutex_init(&buf->lock);

	buf->fl = fl;
	buf->virt = NULL;
	buf->phys = 0;
	buf->size = size;
	buf->dev = dev;
	buf->raddr = 0;

	buf->virt = dma_alloc_coherent(dev, buf->size, (dma_addr_t *)&buf->phys,
				       GFP_KERNEL);
	if (!buf->virt) {
		mutex_destroy(&buf->lock);
		kfree(buf);
		return -ENOMEM;
	}

	if (fl->sctx && fl->sctx->sid)
		buf->phys += ((u64)fl->sctx->sid << 32);

	*obuf = buf;

	return 0;
}

static void fastrpc_channel_ctx_free(struct kref *ref)
{
	struct fastrpc_channel_ctx *cctx;

	cctx = container_of(ref, struct fastrpc_channel_ctx, refcount);

	kfree(cctx);
}

static void fastrpc_channel_ctx_get(struct fastrpc_channel_ctx *cctx)
{
	kref_get(&cctx->refcount);
}

static void fastrpc_channel_ctx_put(struct fastrpc_channel_ctx *cctx)
{
	kref_put(&cctx->refcount, fastrpc_channel_ctx_free);
}

static void fastrpc_context_free(struct kref *ref)
{
	struct fastrpc_invoke_ctx *ctx;
	struct fastrpc_channel_ctx *cctx;
	unsigned long flags;
	int i;

	ctx = container_of(ref, struct fastrpc_invoke_ctx, refcount);
	cctx = ctx->cctx;

	for (i = 0; i < ctx->nscalars; i++)
		fastrpc_map_put(ctx->maps[i]);

	if (ctx->buf)
		fastrpc_buf_free(ctx->buf);

	spin_lock_irqsave(&cctx->lock, flags);
	idr_remove(&cctx->ctx_idr, ctx->ctxid >> 4);
	spin_unlock_irqrestore(&cctx->lock, flags);

	kfree(ctx->maps);
	kfree(ctx->olaps);
	kfree(ctx);

	fastrpc_channel_ctx_put(cctx);
}

static void fastrpc_context_get(struct fastrpc_invoke_ctx *ctx)
{
	kref_get(&ctx->refcount);
}

static void fastrpc_context_put(struct fastrpc_invoke_ctx *ctx)
{
	kref_put(&ctx->refcount, fastrpc_context_free);
}

static void fastrpc_context_put_wq(struct work_struct *work)
{
	struct fastrpc_invoke_ctx *ctx =
			container_of(work, struct fastrpc_invoke_ctx, put_work);

	fastrpc_context_put(ctx);
}

#define CMP(aa, bb) ((aa) == (bb) ? 0 : (aa) < (bb) ? -1 : 1)
static int olaps_cmp(const void *a, const void *b)
{
	struct fastrpc_buf_overlap *pa = (struct fastrpc_buf_overlap *)a;
	struct fastrpc_buf_overlap *pb = (struct fastrpc_buf_overlap *)b;
	/* sort with lowest starting buffer first */
	int st = CMP(pa->start, pb->start);
	/* sort with highest ending buffer first */
	int ed = CMP(pb->end, pa->end);

	return st == 0 ? ed : st;
}

static void fastrpc_get_buff_overlaps(struct fastrpc_invoke_ctx *ctx)
{
	u64 max_end = 0;
	int i;

	for (i = 0; i < ctx->nbufs; ++i) {
		ctx->olaps[i].start = ctx->args[i].ptr;
		ctx->olaps[i].end = ctx->olaps[i].start + ctx->args[i].length;
		ctx->olaps[i].raix = i;
	}

	sort(ctx->olaps, ctx->nbufs, sizeof(*ctx->olaps), olaps_cmp, NULL);

	for (i = 0; i < ctx->nbufs; ++i) {
		/* Falling inside previous range */
		if (ctx->olaps[i].start < max_end) {
			ctx->olaps[i].mstart = max_end;
			ctx->olaps[i].mend = ctx->olaps[i].end;
			ctx->olaps[i].offset = max_end - ctx->olaps[i].start;

			if (ctx->olaps[i].end > max_end) {
				max_end = ctx->olaps[i].end;
			} else {
				ctx->olaps[i].mend = 0;
				ctx->olaps[i].mstart = 0;
			}

		} else  {
			ctx->olaps[i].mend = ctx->olaps[i].end;
			ctx->olaps[i].mstart = ctx->olaps[i].start;
			ctx->olaps[i].offset = 0;
			max_end = ctx->olaps[i].end;
		}
	}
}

static struct fastrpc_invoke_ctx *fastrpc_context_alloc(
			struct fastrpc_user *user, u32 kernel, u32 sc,
			struct fastrpc_invoke_args *args)
{
	struct fastrpc_channel_ctx *cctx = user->cctx;
	struct fastrpc_invoke_ctx *ctx = NULL;
	unsigned long flags;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&ctx->node);
	ctx->fl = user;
	ctx->nscalars = REMOTE_SCALARS_LENGTH(sc);
	ctx->nbufs = REMOTE_SCALARS_INBUFS(sc) +
		     REMOTE_SCALARS_OUTBUFS(sc);

	if (ctx->nscalars) {
		ctx->maps = kcalloc(ctx->nscalars,
				    sizeof(*ctx->maps), GFP_KERNEL);
		if (!ctx->maps) {
			kfree(ctx);
			return ERR_PTR(-ENOMEM);
		}
		ctx->olaps = kcalloc(ctx->nscalars,
				    sizeof(*ctx->olaps), GFP_KERNEL);
		if (!ctx->olaps) {
			kfree(ctx->maps);
			kfree(ctx);
			return ERR_PTR(-ENOMEM);
		}
		ctx->args = args;
		fastrpc_get_buff_overlaps(ctx);
	}

	/* Released in fastrpc_context_put() */
	fastrpc_channel_ctx_get(cctx);

	ctx->sc = sc;
	ctx->retval = -1;
	ctx->pid = current->pid;
	ctx->tgid = user->tgid;
	ctx->cctx = cctx;
	init_completion(&ctx->work);
	INIT_WORK(&ctx->put_work, fastrpc_context_put_wq);

	spin_lock(&user->lock);
	list_add_tail(&ctx->node, &user->pending);
	spin_unlock(&user->lock);

	spin_lock_irqsave(&cctx->lock, flags);
	ret = idr_alloc_cyclic(&cctx->ctx_idr, ctx, 1,
			       FASTRPC_CTX_MAX, GFP_ATOMIC);
	if (ret < 0) {
		spin_unlock_irqrestore(&cctx->lock, flags);
		goto err_idr;
	}
	ctx->ctxid = ret << 4;
	spin_unlock_irqrestore(&cctx->lock, flags);

	kref_init(&ctx->refcount);

	return ctx;
err_idr:
	spin_lock(&user->lock);
	list_del(&ctx->node);
	spin_unlock(&user->lock);
	fastrpc_channel_ctx_put(cctx);
	kfree(ctx->maps);
	kfree(ctx->olaps);
	kfree(ctx);

	return ERR_PTR(ret);
}

static struct sg_table *
fastrpc_map_dma_buf(struct dma_buf_attachment *attachment,
		    enum dma_data_direction dir)
{
	struct fastrpc_dma_buf_attachment *a = attachment->priv;
	struct sg_table *table;
	int ret;

	table = &a->sgt;

	ret = dma_map_sgtable(attachment->dev, table, dir, 0);
	if (ret)
		table = ERR_PTR(ret);
	return table;
}

static void fastrpc_unmap_dma_buf(struct dma_buf_attachment *attach,
				  struct sg_table *table,
				  enum dma_data_direction dir)
{
	dma_unmap_sgtable(attach->dev, table, dir, 0);
}

static void fastrpc_release(struct dma_buf *dmabuf)
{
	struct fastrpc_buf *buffer = dmabuf->priv;

	fastrpc_buf_free(buffer);
}

static int fastrpc_dma_buf_attach(struct dma_buf *dmabuf,
				  struct dma_buf_attachment *attachment)
{
	struct fastrpc_dma_buf_attachment *a;
	struct fastrpc_buf *buffer = dmabuf->priv;
	int ret;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	ret = dma_get_sgtable(buffer->dev, &a->sgt, buffer->virt,
			      FASTRPC_PHYS(buffer->phys), buffer->size);
	if (ret < 0) {
		dev_err(buffer->dev, "failed to get scatterlist from DMA API\n");
		kfree(a);
		return -EINVAL;
	}

	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->node);
	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->node, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

static void fastrpc_dma_buf_detatch(struct dma_buf *dmabuf,
				    struct dma_buf_attachment *attachment)
{
	struct fastrpc_dma_buf_attachment *a = attachment->priv;
	struct fastrpc_buf *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->node);
	mutex_unlock(&buffer->lock);
	sg_free_table(&a->sgt);
	kfree(a);
}

static void *fastrpc_vmap(struct dma_buf *dmabuf)
{
	struct fastrpc_buf *buf = dmabuf->priv;

	return buf->virt;
}

static int fastrpc_mmap(struct dma_buf *dmabuf,
			struct vm_area_struct *vma)
{
	struct fastrpc_buf *buf = dmabuf->priv;
	size_t size = vma->vm_end - vma->vm_start;

	return dma_mmap_coherent(buf->dev, vma, buf->virt,
				 FASTRPC_PHYS(buf->phys), size);
}

static const struct dma_buf_ops fastrpc_dma_buf_ops = {
	.attach = fastrpc_dma_buf_attach,
	.detach = fastrpc_dma_buf_detatch,
	.map_dma_buf = fastrpc_map_dma_buf,
	.unmap_dma_buf = fastrpc_unmap_dma_buf,
	.mmap = fastrpc_mmap,
	.vmap = fastrpc_vmap,
	.release = fastrpc_release,
};

static int fastrpc_map_create(struct fastrpc_user *fl, int fd,
			      u64 len, struct fastrpc_map **ppmap)
{
	struct fastrpc_session_ctx *sess = fl->sctx;
	struct fastrpc_map *map = NULL;
	int err = 0;

	if (!fastrpc_map_find(fl, fd, ppmap))
		return 0;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	INIT_LIST_HEAD(&map->node);
	map->fl = fl;
	map->fd = fd;
	map->buf = dma_buf_get(fd);
	if (IS_ERR(map->buf)) {
		err = PTR_ERR(map->buf);
		goto get_err;
	}

	map->attach = dma_buf_attach(map->buf, sess->dev);
	if (IS_ERR(map->attach)) {
		dev_err(sess->dev, "Failed to attach dmabuf\n");
		err = PTR_ERR(map->attach);
		goto attach_err;
	}

	map->table = dma_buf_map_attachment(map->attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(map->table)) {
		err = PTR_ERR(map->table);
		goto map_err;
	}

	map->phys = sg_dma_address(map->table->sgl);
	map->phys += ((u64)fl->sctx->sid << 32);
	map->size = len;
	map->va = sg_virt(map->table->sgl);
	map->len = len;
	kref_init(&map->refcount);

	spin_lock(&fl->lock);
	list_add_tail(&map->node, &fl->maps);
	spin_unlock(&fl->lock);
	*ppmap = map;

	return 0;

map_err:
	dma_buf_detach(map->buf, map->attach);
attach_err:
	dma_buf_put(map->buf);
get_err:
	kfree(map);

	return err;
}

/*
 * Fastrpc payload buffer with metadata looks like:
 *
 * >>>>>>  START of METADATA <<<<<<<<<
 * +---------------------------------+
 * |           Arguments             |
 * | type:(struct fastrpc_remote_arg)|
 * |             (0 - N)             |
 * +---------------------------------+
 * |         Invoke Buffer list      |
 * | type:(struct fastrpc_invoke_buf)|
 * |           (0 - N)               |
 * +---------------------------------+
 * |         Page info list          |
 * | type:(struct fastrpc_phy_page)  |
 * |             (0 - N)             |
 * +---------------------------------+
 * |         Optional info           |
 * |(can be specific to SoC/Firmware)|
 * +---------------------------------+
 * >>>>>>>>  END of METADATA <<<<<<<<<
 * +---------------------------------+
 * |         Inline ARGS             |
 * |            (0-N)                |
 * +---------------------------------+
 */

static int fastrpc_get_meta_size(struct fastrpc_invoke_ctx *ctx)
{
	int size = 0;

	size = (sizeof(struct fastrpc_remote_arg) +
		sizeof(struct fastrpc_invoke_buf) +
		sizeof(struct fastrpc_phy_page)) * ctx->nscalars +
		sizeof(u64) * FASTRPC_MAX_FDLIST +
		sizeof(u32) * FASTRPC_MAX_CRCLIST;

	return size;
}

static u64 fastrpc_get_payload_size(struct fastrpc_invoke_ctx *ctx, int metalen)
{
	u64 size = 0;
	int oix;

	size = ALIGN(metalen, FASTRPC_ALIGN);
	for (oix = 0; oix < ctx->nbufs; oix++) {
		int i = ctx->olaps[oix].raix;

		if (ctx->args[i].fd == 0 || ctx->args[i].fd == -1) {

			if (ctx->olaps[oix].offset == 0)
				size = ALIGN(size, FASTRPC_ALIGN);

			size += (ctx->olaps[oix].mend - ctx->olaps[oix].mstart);
		}
	}

	return size;
}

static int fastrpc_create_maps(struct fastrpc_invoke_ctx *ctx)
{
	struct device *dev = ctx->fl->sctx->dev;
	int i, err;

	for (i = 0; i < ctx->nscalars; ++i) {
		/* Make sure reserved field is set to 0 */
		if (ctx->args[i].reserved)
			return -EINVAL;

		if (ctx->args[i].fd == 0 || ctx->args[i].fd == -1 ||
		    ctx->args[i].length == 0)
			continue;

		err = fastrpc_map_create(ctx->fl, ctx->args[i].fd,
					 ctx->args[i].length, &ctx->maps[i]);
		if (err) {
			dev_err(dev, "Error Creating map %d\n", err);
			return -EINVAL;
		}

	}
	return 0;
}

static int fastrpc_get_args(u32 kernel, struct fastrpc_invoke_ctx *ctx)
{
	struct device *dev = ctx->fl->sctx->dev;
	struct fastrpc_remote_arg *rpra;
	struct fastrpc_invoke_buf *list;
	struct fastrpc_phy_page *pages;
	int inbufs, i, oix, err = 0;
	u64 len, rlen, pkt_size;
	u64 pg_start, pg_end;
	uintptr_t args;
	int metalen;

	inbufs = REMOTE_SCALARS_INBUFS(ctx->sc);
	metalen = fastrpc_get_meta_size(ctx);
	pkt_size = fastrpc_get_payload_size(ctx, metalen);

	err = fastrpc_create_maps(ctx);
	if (err)
		return err;

	ctx->msg_sz = pkt_size;

	err = fastrpc_buf_alloc(ctx->fl, dev, pkt_size, &ctx->buf);
	if (err)
		return err;

	rpra = ctx->buf->virt;
	list = ctx->buf->virt + ctx->nscalars * sizeof(*rpra);
	pages = ctx->buf->virt + ctx->nscalars * (sizeof(*list) +
		sizeof(*rpra));
	args = (uintptr_t)ctx->buf->virt + metalen;
	rlen = pkt_size - metalen;
	ctx->rpra = rpra;

	for (oix = 0; oix < ctx->nbufs; ++oix) {
		int mlen;

		i = ctx->olaps[oix].raix;
		len = ctx->args[i].length;

		rpra[i].pv = 0;
		rpra[i].len = len;
		list[i].num = len ? 1 : 0;
		list[i].pgidx = i;

		if (!len)
			continue;

		if (ctx->maps[i]) {
			struct vm_area_struct *vma = NULL;

			rpra[i].pv = (u64) ctx->args[i].ptr;
			pages[i].addr = ctx->maps[i]->phys;

			mmap_read_lock(current->mm);
			vma = find_vma(current->mm, ctx->args[i].ptr);
			if (vma)
				pages[i].addr += ctx->args[i].ptr -
						 vma->vm_start;
			mmap_read_unlock(current->mm);

			pg_start = (ctx->args[i].ptr & PAGE_MASK) >> PAGE_SHIFT;
			pg_end = ((ctx->args[i].ptr + len - 1) & PAGE_MASK) >>
				  PAGE_SHIFT;
			pages[i].size = (pg_end - pg_start + 1) * PAGE_SIZE;

		} else {

			if (ctx->olaps[oix].offset == 0) {
				rlen -= ALIGN(args, FASTRPC_ALIGN) - args;
				args = ALIGN(args, FASTRPC_ALIGN);
			}

			mlen = ctx->olaps[oix].mend - ctx->olaps[oix].mstart;

			if (rlen < mlen)
				goto bail;

			rpra[i].pv = args - ctx->olaps[oix].offset;
			pages[i].addr = ctx->buf->phys -
					ctx->olaps[oix].offset +
					(pkt_size - rlen);
			pages[i].addr = pages[i].addr &	PAGE_MASK;

			pg_start = (args & PAGE_MASK) >> PAGE_SHIFT;
			pg_end = ((args + len - 1) & PAGE_MASK) >> PAGE_SHIFT;
			pages[i].size = (pg_end - pg_start + 1) * PAGE_SIZE;
			args = args + mlen;
			rlen -= mlen;
		}

		if (i < inbufs && !ctx->maps[i]) {
			void *dst = (void *)(uintptr_t)rpra[i].pv;
			void *src = (void *)(uintptr_t)ctx->args[i].ptr;

			if (!kernel) {
				if (copy_from_user(dst, (void __user *)src,
						   len)) {
					err = -EFAULT;
					goto bail;
				}
			} else {
				memcpy(dst, src, len);
			}
		}
	}

	for (i = ctx->nbufs; i < ctx->nscalars; ++i) {
		rpra[i].pv = (u64) ctx->args[i].ptr;
		rpra[i].len = ctx->args[i].length;
		list[i].num = ctx->args[i].length ? 1 : 0;
		list[i].pgidx = i;
		pages[i].addr = ctx->maps[i]->phys;
		pages[i].size = ctx->maps[i]->size;
	}

bail:
	if (err)
		dev_err(dev, "Error: get invoke args failed:%d\n", err);

	return err;
}

static int fastrpc_put_args(struct fastrpc_invoke_ctx *ctx,
			    u32 kernel)
{
	struct fastrpc_remote_arg *rpra = ctx->rpra;
	int i, inbufs;

	inbufs = REMOTE_SCALARS_INBUFS(ctx->sc);

	for (i = inbufs; i < ctx->nbufs; ++i) {
		void *src = (void *)(uintptr_t)rpra[i].pv;
		void *dst = (void *)(uintptr_t)ctx->args[i].ptr;
		u64 len = rpra[i].len;

		if (!kernel) {
			if (copy_to_user((void __user *)dst, src, len))
				return -EFAULT;
		} else {
			memcpy(dst, src, len);
		}
	}

	return 0;
}

static int fastrpc_invoke_send(struct fastrpc_session_ctx *sctx,
			       struct fastrpc_invoke_ctx *ctx,
			       u32 kernel, uint32_t handle)
{
	struct fastrpc_channel_ctx *cctx;
	struct fastrpc_user *fl = ctx->fl;
	struct fastrpc_msg *msg = &ctx->msg;
	int ret;

	cctx = fl->cctx;
	msg->pid = fl->tgid;
	msg->tid = current->pid;

	if (kernel)
		msg->pid = 0;

	msg->ctx = ctx->ctxid | fl->pd;
	msg->handle = handle;
	msg->sc = ctx->sc;
	msg->addr = ctx->buf ? ctx->buf->phys : 0;
	msg->size = roundup(ctx->msg_sz, PAGE_SIZE);
	fastrpc_context_get(ctx);

	ret = rpmsg_send(cctx->rpdev->ept, (void *)msg, sizeof(*msg));

	if (ret)
		fastrpc_context_put(ctx);

	return ret;

}

static int fastrpc_internal_invoke(struct fastrpc_user *fl,  u32 kernel,
				   u32 handle, u32 sc,
				   struct fastrpc_invoke_args *args)
{
	struct fastrpc_invoke_ctx *ctx = NULL;
	int err = 0;

	if (!fl->sctx)
		return -EINVAL;

	if (!fl->cctx->rpdev)
		return -EPIPE;

	if (handle == FASTRPC_INIT_HANDLE && !kernel) {
		dev_warn_ratelimited(fl->sctx->dev, "user app trying to send a kernel RPC message (%d)\n",  handle);
		return -EPERM;
	}

	ctx = fastrpc_context_alloc(fl, kernel, sc, args);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	if (ctx->nscalars) {
		err = fastrpc_get_args(kernel, ctx);
		if (err)
			goto bail;
	}

	/* make sure that all CPU memory writes are seen by DSP */
	dma_wmb();
	/* Send invoke buffer to remote dsp */
	err = fastrpc_invoke_send(fl->sctx, ctx, kernel, handle);
	if (err)
		goto bail;

	if (kernel) {
		if (!wait_for_completion_timeout(&ctx->work, 10 * HZ))
			err = -ETIMEDOUT;
	} else {
		err = wait_for_completion_interruptible(&ctx->work);
	}

	if (err)
		goto bail;

	/* Check the response from remote dsp */
	err = ctx->retval;
	if (err)
		goto bail;

	if (ctx->nscalars) {
		/* make sure that all memory writes by DSP are seen by CPU */
		dma_rmb();
		/* populate all the output buffers with results */
		err = fastrpc_put_args(ctx, kernel);
		if (err)
			goto bail;
	}

bail:
	if (err != -ERESTARTSYS && err != -ETIMEDOUT) {
		/* We are done with this compute context */
		spin_lock(&fl->lock);
		list_del(&ctx->node);
		spin_unlock(&fl->lock);
		fastrpc_context_put(ctx);
	}
	if (err)
		dev_dbg(fl->sctx->dev, "Error: Invoke Failed %d\n", err);

	return err;
}

static int fastrpc_init_create_process(struct fastrpc_user *fl,
					char __user *argp)
{
	struct fastrpc_init_create init;
	struct fastrpc_invoke_args *args;
	struct fastrpc_phy_page pages[1];
	struct fastrpc_map *map = NULL;
	struct fastrpc_buf *imem = NULL;
	int memlen;
	int err;
	struct {
		int pgid;
		u32 namelen;
		u32 filelen;
		u32 pageslen;
		u32 attrs;
		u32 siglen;
	} inbuf;
	u32 sc;

	args = kcalloc(FASTRPC_CREATE_PROCESS_NARGS, sizeof(*args), GFP_KERNEL);
	if (!args)
		return -ENOMEM;

	if (copy_from_user(&init, argp, sizeof(init))) {
		err = -EFAULT;
		goto err;
	}

	if (init.filelen > INIT_FILELEN_MAX) {
		err = -EINVAL;
		goto err;
	}

	inbuf.pgid = fl->tgid;
	inbuf.namelen = strlen(current->comm) + 1;
	inbuf.filelen = init.filelen;
	inbuf.pageslen = 1;
	inbuf.attrs = init.attrs;
	inbuf.siglen = init.siglen;
	fl->pd = USER_PD;

	if (init.filelen && init.filefd) {
		err = fastrpc_map_create(fl, init.filefd, init.filelen, &map);
		if (err)
			goto err;
	}

	memlen = ALIGN(max(INIT_FILELEN_MAX, (int)init.filelen * 4),
		       1024 * 1024);
	err = fastrpc_buf_alloc(fl, fl->sctx->dev, memlen,
				&imem);
	if (err)
		goto err_alloc;

	fl->init_mem = imem;
	args[0].ptr = (u64)(uintptr_t)&inbuf;
	args[0].length = sizeof(inbuf);
	args[0].fd = -1;

	args[1].ptr = (u64)(uintptr_t)current->comm;
	args[1].length = inbuf.namelen;
	args[1].fd = -1;

	args[2].ptr = (u64) init.file;
	args[2].length = inbuf.filelen;
	args[2].fd = init.filefd;

	pages[0].addr = imem->phys;
	pages[0].size = imem->size;

	args[3].ptr = (u64)(uintptr_t) pages;
	args[3].length = 1 * sizeof(*pages);
	args[3].fd = -1;

	args[4].ptr = (u64)(uintptr_t)&inbuf.attrs;
	args[4].length = sizeof(inbuf.attrs);
	args[4].fd = -1;

	args[5].ptr = (u64)(uintptr_t) &inbuf.siglen;
	args[5].length = sizeof(inbuf.siglen);
	args[5].fd = -1;

	sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_CREATE, 4, 0);
	if (init.attrs)
		sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_CREATE_ATTR, 6, 0);

	err = fastrpc_internal_invoke(fl, true, FASTRPC_INIT_HANDLE,
				      sc, args);
	if (err)
		goto err_invoke;

	kfree(args);

	return 0;

err_invoke:
	fl->init_mem = NULL;
	fastrpc_buf_free(imem);
err_alloc:
	fastrpc_map_put(map);
err:
	kfree(args);

	return err;
}

static struct fastrpc_session_ctx *fastrpc_session_alloc(
					struct fastrpc_channel_ctx *cctx)
{
	struct fastrpc_session_ctx *session = NULL;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&cctx->lock, flags);
	for (i = 0; i < cctx->sesscount; i++) {
		if (!cctx->session[i].used && cctx->session[i].valid) {
			cctx->session[i].used = true;
			session = &cctx->session[i];
			break;
		}
	}
	spin_unlock_irqrestore(&cctx->lock, flags);

	return session;
}

static void fastrpc_session_free(struct fastrpc_channel_ctx *cctx,
				 struct fastrpc_session_ctx *session)
{
	unsigned long flags;

	spin_lock_irqsave(&cctx->lock, flags);
	session->used = false;
	spin_unlock_irqrestore(&cctx->lock, flags);
}

static int fastrpc_release_current_dsp_process(struct fastrpc_user *fl)
{
	struct fastrpc_invoke_args args[1];
	int tgid = 0;
	u32 sc;

	tgid = fl->tgid;
	args[0].ptr = (u64)(uintptr_t) &tgid;
	args[0].length = sizeof(tgid);
	args[0].fd = -1;
	args[0].reserved = 0;
	sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_RELEASE, 1, 0);

	return fastrpc_internal_invoke(fl, true, FASTRPC_INIT_HANDLE,
				       sc, &args[0]);
}

static int fastrpc_device_release(struct inode *inode, struct file *file)
{
	struct fastrpc_user *fl = (struct fastrpc_user *)file->private_data;
	struct fastrpc_channel_ctx *cctx = fl->cctx;
	struct fastrpc_invoke_ctx *ctx, *n;
	struct fastrpc_map *map, *m;
	struct fastrpc_buf *buf, *b;
	unsigned long flags;

	fastrpc_release_current_dsp_process(fl);

	spin_lock_irqsave(&cctx->lock, flags);
	list_del(&fl->user);
	spin_unlock_irqrestore(&cctx->lock, flags);

	if (fl->init_mem)
		fastrpc_buf_free(fl->init_mem);

	list_for_each_entry_safe(ctx, n, &fl->pending, node) {
		list_del(&ctx->node);
		fastrpc_context_put(ctx);
	}

	list_for_each_entry_safe(map, m, &fl->maps, node)
		fastrpc_map_put(map);

	list_for_each_entry_safe(buf, b, &fl->mmaps, node) {
		list_del(&buf->node);
		fastrpc_buf_free(buf);
	}

	fastrpc_session_free(cctx, fl->sctx);
	fastrpc_channel_ctx_put(cctx);

	mutex_destroy(&fl->mutex);
	kfree(fl);
	file->private_data = NULL;

	return 0;
}

static int fastrpc_device_open(struct inode *inode, struct file *filp)
{
	struct fastrpc_channel_ctx *cctx = miscdev_to_cctx(filp->private_data);
	struct fastrpc_user *fl = NULL;
	unsigned long flags;

	fl = kzalloc(sizeof(*fl), GFP_KERNEL);
	if (!fl)
		return -ENOMEM;

	/* Released in fastrpc_device_release() */
	fastrpc_channel_ctx_get(cctx);

	filp->private_data = fl;
	spin_lock_init(&fl->lock);
	mutex_init(&fl->mutex);
	INIT_LIST_HEAD(&fl->pending);
	INIT_LIST_HEAD(&fl->maps);
	INIT_LIST_HEAD(&fl->mmaps);
	INIT_LIST_HEAD(&fl->user);
	fl->tgid = current->tgid;
	fl->cctx = cctx;

	fl->sctx = fastrpc_session_alloc(cctx);
	if (!fl->sctx) {
		dev_err(&cctx->rpdev->dev, "No session available\n");
		mutex_destroy(&fl->mutex);
		kfree(fl);

		return -EBUSY;
	}

	spin_lock_irqsave(&cctx->lock, flags);
	list_add_tail(&fl->user, &cctx->users);
	spin_unlock_irqrestore(&cctx->lock, flags);

	return 0;
}

static int fastrpc_dmabuf_alloc(struct fastrpc_user *fl, char __user *argp)
{
	struct fastrpc_alloc_dma_buf bp;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct fastrpc_buf *buf = NULL;
	int err;

	if (copy_from_user(&bp, argp, sizeof(bp)))
		return -EFAULT;

	err = fastrpc_buf_alloc(fl, fl->sctx->dev, bp.size, &buf);
	if (err)
		return err;
	exp_info.ops = &fastrpc_dma_buf_ops;
	exp_info.size = bp.size;
	exp_info.flags = O_RDWR;
	exp_info.priv = buf;
	buf->dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(buf->dmabuf)) {
		err = PTR_ERR(buf->dmabuf);
		fastrpc_buf_free(buf);
		return err;
	}

	bp.fd = dma_buf_fd(buf->dmabuf, O_ACCMODE);
	if (bp.fd < 0) {
		dma_buf_put(buf->dmabuf);
		return -EINVAL;
	}

	if (copy_to_user(argp, &bp, sizeof(bp))) {
		/*
		 * The usercopy failed, but we can't do much about it, as
		 * dma_buf_fd() already called fd_install() and made the
		 * file descriptor accessible for the current process. It
		 * might already be closed and dmabuf no longer valid when
		 * we reach this point. Therefore "leak" the fd and rely on
		 * the process exit path to do any required cleanup.
		 */
		return -EFAULT;
	}

	return 0;
}

static int fastrpc_init_attach(struct fastrpc_user *fl, int pd)
{
	struct fastrpc_invoke_args args[1];
	int tgid = fl->tgid;
	u32 sc;

	args[0].ptr = (u64)(uintptr_t) &tgid;
	args[0].length = sizeof(tgid);
	args[0].fd = -1;
	args[0].reserved = 0;
	sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_ATTACH, 1, 0);
	fl->pd = pd;

	return fastrpc_internal_invoke(fl, true, FASTRPC_INIT_HANDLE,
				       sc, &args[0]);
}

static int fastrpc_invoke(struct fastrpc_user *fl, char __user *argp)
{
	struct fastrpc_invoke_args *args = NULL;
	struct fastrpc_invoke inv;
	u32 nscalars;
	int err;

	if (copy_from_user(&inv, argp, sizeof(inv)))
		return -EFAULT;

	/* nscalars is truncated here to max supported value */
	nscalars = REMOTE_SCALARS_LENGTH(inv.sc);
	if (nscalars) {
		args = kcalloc(nscalars, sizeof(*args), GFP_KERNEL);
		if (!args)
			return -ENOMEM;

		if (copy_from_user(args, (void __user *)(uintptr_t)inv.args,
				   nscalars * sizeof(*args))) {
			kfree(args);
			return -EFAULT;
		}
	}

	err = fastrpc_internal_invoke(fl, false, inv.handle, inv.sc, args);
	kfree(args);

	return err;
}

static int fastrpc_req_munmap_impl(struct fastrpc_user *fl,
				   struct fastrpc_req_munmap *req)
{
	struct fastrpc_invoke_args args[1] = { [0] = { 0 } };
	struct fastrpc_buf *buf = NULL, *iter, *b;
	struct fastrpc_munmap_req_msg req_msg;
	struct device *dev = fl->sctx->dev;
	int err;
	u32 sc;

	spin_lock(&fl->lock);
	list_for_each_entry_safe(iter, b, &fl->mmaps, node) {
		if ((iter->raddr == req->vaddrout) && (iter->size == req->size)) {
			buf = iter;
			break;
		}
	}
	spin_unlock(&fl->lock);

	if (!buf) {
		dev_err(dev, "mmap not in list\n");
		return -EINVAL;
	}

	req_msg.pgid = fl->tgid;
	req_msg.size = buf->size;
	req_msg.vaddr = buf->raddr;

	args[0].ptr = (u64) (uintptr_t) &req_msg;
	args[0].length = sizeof(req_msg);

	sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_MUNMAP, 1, 0);
	err = fastrpc_internal_invoke(fl, true, FASTRPC_INIT_HANDLE, sc,
				      &args[0]);
	if (!err) {
		dev_dbg(dev, "unmmap\tpt 0x%09lx OK\n", buf->raddr);
		spin_lock(&fl->lock);
		list_del(&buf->node);
		spin_unlock(&fl->lock);
		fastrpc_buf_free(buf);
	} else {
		dev_err(dev, "unmmap\tpt 0x%09lx ERROR\n", buf->raddr);
	}

	return err;
}

static int fastrpc_req_munmap(struct fastrpc_user *fl, char __user *argp)
{
	struct fastrpc_req_munmap req;

	if (copy_from_user(&req, argp, sizeof(req)))
		return -EFAULT;

	return fastrpc_req_munmap_impl(fl, &req);
}

static int fastrpc_req_mmap(struct fastrpc_user *fl, char __user *argp)
{
	struct fastrpc_invoke_args args[3] = { [0 ... 2] = { 0 } };
	struct fastrpc_buf *buf = NULL;
	struct fastrpc_mmap_req_msg req_msg;
	struct fastrpc_mmap_rsp_msg rsp_msg;
	struct fastrpc_req_munmap req_unmap;
	struct fastrpc_phy_page pages;
	struct fastrpc_req_mmap req;
	struct device *dev = fl->sctx->dev;
	int err;
	u32 sc;

	if (copy_from_user(&req, argp, sizeof(req)))
		return -EFAULT;

	if (req.flags != ADSP_MMAP_ADD_PAGES) {
		dev_err(dev, "flag not supported 0x%x\n", req.flags);
		return -EINVAL;
	}

	if (req.vaddrin) {
		dev_err(dev, "adding user allocated pages is not supported\n");
		return -EINVAL;
	}

	err = fastrpc_buf_alloc(fl, fl->sctx->dev, req.size, &buf);
	if (err) {
		dev_err(dev, "failed to allocate buffer\n");
		return err;
	}

	req_msg.pgid = fl->tgid;
	req_msg.flags = req.flags;
	req_msg.vaddr = req.vaddrin;
	req_msg.num = sizeof(pages);

	args[0].ptr = (u64) (uintptr_t) &req_msg;
	args[0].length = sizeof(req_msg);

	pages.addr = buf->phys;
	pages.size = buf->size;

	args[1].ptr = (u64) (uintptr_t) &pages;
	args[1].length = sizeof(pages);

	args[2].ptr = (u64) (uintptr_t) &rsp_msg;
	args[2].length = sizeof(rsp_msg);

	sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_MMAP, 2, 1);
	err = fastrpc_internal_invoke(fl, true, FASTRPC_INIT_HANDLE, sc,
				      &args[0]);
	if (err) {
		dev_err(dev, "mmap error (len 0x%08llx)\n", buf->size);
		goto err_invoke;
	}

	/* update the buffer to be able to deallocate the memory on the DSP */
	buf->raddr = (uintptr_t) rsp_msg.vaddr;

	/* let the client know the address to use */
	req.vaddrout = rsp_msg.vaddr;

	spin_lock(&fl->lock);
	list_add_tail(&buf->node, &fl->mmaps);
	spin_unlock(&fl->lock);

	if (copy_to_user((void __user *)argp, &req, sizeof(req))) {
		/* unmap the memory and release the buffer */
		req_unmap.vaddrout = buf->raddr;
		req_unmap.size = buf->size;
		fastrpc_req_munmap_impl(fl, &req_unmap);
		return -EFAULT;
	}

	dev_dbg(dev, "mmap\t\tpt 0x%09lx OK [len 0x%08llx]\n",
		buf->raddr, buf->size);

	return 0;

err_invoke:
	fastrpc_buf_free(buf);

	return err;
}

static long fastrpc_device_ioctl(struct file *file, unsigned int cmd,
				 unsigned long arg)
{
	struct fastrpc_user *fl = (struct fastrpc_user *)file->private_data;
	char __user *argp = (char __user *)arg;
	int err;

	switch (cmd) {
	case FASTRPC_IOCTL_INVOKE:
		err = fastrpc_invoke(fl, argp);
		break;
	case FASTRPC_IOCTL_INIT_ATTACH:
		err = fastrpc_init_attach(fl, AUDIO_PD);
		break;
	case FASTRPC_IOCTL_INIT_ATTACH_SNS:
		err = fastrpc_init_attach(fl, SENSORS_PD);
		break;
	case FASTRPC_IOCTL_INIT_CREATE:
		err = fastrpc_init_create_process(fl, argp);
		break;
	case FASTRPC_IOCTL_ALLOC_DMA_BUFF:
		err = fastrpc_dmabuf_alloc(fl, argp);
		break;
	case FASTRPC_IOCTL_MMAP:
		err = fastrpc_req_mmap(fl, argp);
		break;
	case FASTRPC_IOCTL_MUNMAP:
		err = fastrpc_req_munmap(fl, argp);
		break;
	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

static const struct file_operations fastrpc_fops = {
	.open = fastrpc_device_open,
	.release = fastrpc_device_release,
	.unlocked_ioctl = fastrpc_device_ioctl,
	.compat_ioctl = fastrpc_device_ioctl,
};

static int fastrpc_cb_probe(struct platform_device *pdev)
{
	struct fastrpc_channel_ctx *cctx;
	struct fastrpc_session_ctx *sess;
	struct device *dev = &pdev->dev;
	int i, sessions = 0;
	unsigned long flags;
	int rc;

	cctx = dev_get_drvdata(dev->parent);
	if (!cctx)
		return -EINVAL;

	of_property_read_u32(dev->of_node, "qcom,nsessions", &sessions);

	spin_lock_irqsave(&cctx->lock, flags);
	if (cctx->sesscount >= FASTRPC_MAX_SESSIONS) {
		dev_err(&pdev->dev, "too many sessions\n");
		spin_unlock_irqrestore(&cctx->lock, flags);
		return -ENOSPC;
	}
	sess = &cctx->session[cctx->sesscount++];
	sess->used = false;
	sess->valid = true;
	sess->dev = dev;
	dev_set_drvdata(dev, sess);

	if (of_property_read_u32(dev->of_node, "reg", &sess->sid))
		dev_info(dev, "FastRPC Session ID not specified in DT\n");

	if (sessions > 0) {
		struct fastrpc_session_ctx *dup_sess;

		for (i = 1; i < sessions; i++) {
			if (cctx->sesscount >= FASTRPC_MAX_SESSIONS)
				break;
			dup_sess = &cctx->session[cctx->sesscount++];
			memcpy(dup_sess, sess, sizeof(*dup_sess));
		}
	}
	spin_unlock_irqrestore(&cctx->lock, flags);
	rc = dma_set_mask(dev, DMA_BIT_MASK(32));
	if (rc) {
		dev_err(dev, "32-bit DMA enable failed\n");
		return rc;
	}

	return 0;
}

static int fastrpc_cb_remove(struct platform_device *pdev)
{
	struct fastrpc_channel_ctx *cctx = dev_get_drvdata(pdev->dev.parent);
	struct fastrpc_session_ctx *sess = dev_get_drvdata(&pdev->dev);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&cctx->lock, flags);
	for (i = 1; i < FASTRPC_MAX_SESSIONS; i++) {
		if (cctx->session[i].sid == sess->sid) {
			cctx->session[i].valid = false;
			cctx->sesscount--;
		}
	}
	spin_unlock_irqrestore(&cctx->lock, flags);

	return 0;
}

static const struct of_device_id fastrpc_match_table[] = {
	{ .compatible = "qcom,fastrpc-compute-cb", },
	{}
};

static struct platform_driver fastrpc_cb_driver = {
	.probe = fastrpc_cb_probe,
	.remove = fastrpc_cb_remove,
	.driver = {
		.name = "qcom,fastrpc-cb",
		.of_match_table = fastrpc_match_table,
		.suppress_bind_attrs = true,
	},
};

static int fastrpc_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct device *rdev = &rpdev->dev;
	struct fastrpc_channel_ctx *data;
	int i, err, domain_id = -1;
	const char *domain;

	err = of_property_read_string(rdev->of_node, "label", &domain);
	if (err) {
		dev_info(rdev, "FastRPC Domain not specified in DT\n");
		return err;
	}

	for (i = 0; i <= CDSP_DOMAIN_ID; i++) {
		if (!strcmp(domains[i], domain)) {
			domain_id = i;
			break;
		}
	}

	if (domain_id < 0) {
		dev_info(rdev, "FastRPC Invalid Domain ID %d\n", domain_id);
		return -EINVAL;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = devm_kasprintf(rdev, GFP_KERNEL, "fastrpc-%s",
					    domains[domain_id]);
	data->miscdev.fops = &fastrpc_fops;
	err = misc_register(&data->miscdev);
	if (err) {
		kfree(data);
		return err;
	}

	kref_init(&data->refcount);

	dev_set_drvdata(&rpdev->dev, data);
	dma_set_mask_and_coherent(rdev, DMA_BIT_MASK(32));
	INIT_LIST_HEAD(&data->users);
	spin_lock_init(&data->lock);
	idr_init(&data->ctx_idr);
	data->domain_id = domain_id;
	data->rpdev = rpdev;

	return of_platform_populate(rdev->of_node, NULL, NULL, rdev);
}

static void fastrpc_notify_users(struct fastrpc_user *user)
{
	struct fastrpc_invoke_ctx *ctx;

	spin_lock(&user->lock);
	list_for_each_entry(ctx, &user->pending, node) {
		ctx->retval = -EPIPE;
		complete(&ctx->work);
	}
	spin_unlock(&user->lock);
}

static void fastrpc_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct fastrpc_channel_ctx *cctx = dev_get_drvdata(&rpdev->dev);
	struct fastrpc_user *user;
	unsigned long flags;

	/* No invocations past this point */
	spin_lock_irqsave(&cctx->lock, flags);
	cctx->rpdev = NULL;
	list_for_each_entry(user, &cctx->users, user)
		fastrpc_notify_users(user);
	spin_unlock_irqrestore(&cctx->lock, flags);

	misc_deregister(&cctx->miscdev);
	of_platform_depopulate(&rpdev->dev);

	fastrpc_channel_ctx_put(cctx);
}

static int fastrpc_rpmsg_callback(struct rpmsg_device *rpdev, void *data,
				  int len, void *priv, u32 addr)
{
	struct fastrpc_channel_ctx *cctx = dev_get_drvdata(&rpdev->dev);
	struct fastrpc_invoke_rsp *rsp = data;
	struct fastrpc_invoke_ctx *ctx;
	unsigned long flags;
	unsigned long ctxid;

	if (len < sizeof(*rsp))
		return -EINVAL;

	ctxid = ((rsp->ctx & FASTRPC_CTXID_MASK) >> 4);

	spin_lock_irqsave(&cctx->lock, flags);
	ctx = idr_find(&cctx->ctx_idr, ctxid);
	spin_unlock_irqrestore(&cctx->lock, flags);

	if (!ctx) {
		dev_err(&rpdev->dev, "No context ID matches response\n");
		return -ENOENT;
	}

	ctx->retval = rsp->retval;
	complete(&ctx->work);

	/*
	 * The DMA buffer associated with the context cannot be freed in
	 * interrupt context so schedule it through a worker thread to
	 * avoid a kernel BUG.
	 */
	schedule_work(&ctx->put_work);

	return 0;
}

static const struct of_device_id fastrpc_rpmsg_of_match[] = {
	{ .compatible = "qcom,fastrpc" },
	{ },
};
MODULE_DEVICE_TABLE(of, fastrpc_rpmsg_of_match);

static struct rpmsg_driver fastrpc_driver = {
	.probe = fastrpc_rpmsg_probe,
	.remove = fastrpc_rpmsg_remove,
	.callback = fastrpc_rpmsg_callback,
	.drv = {
		.name = "qcom,fastrpc",
		.of_match_table = fastrpc_rpmsg_of_match,
	},
};

static int fastrpc_init(void)
{
	int ret;

	ret = platform_driver_register(&fastrpc_cb_driver);
	if (ret < 0) {
		pr_err("fastrpc: failed to register cb driver\n");
		return ret;
	}

	ret = register_rpmsg_driver(&fastrpc_driver);
	if (ret < 0) {
		pr_err("fastrpc: failed to register rpmsg driver\n");
		platform_driver_unregister(&fastrpc_cb_driver);
		return ret;
	}

	return 0;
}
module_init(fastrpc_init);

static void fastrpc_exit(void)
{
	platform_driver_unregister(&fastrpc_cb_driver);
	unregister_rpmsg_driver(&fastrpc_driver);
}
module_exit(fastrpc_exit);

MODULE_LICENSE("GPL v2");
