
#include "drmP.h"
#include "drm.h"

#include "nouveau_drv.h"
#include "nouveau_drm.h"
#include "nouveau_dma.h"

#include <linux/dma-buf.h>

static struct sg_table *nouveau_gem_map_dma_buf(struct dma_buf_attachment *attachment,
					  enum dma_data_direction dir)
{
	struct nouveau_bo *nvbo = attachment->dmabuf->priv;
	struct drm_device *dev = nvbo->gem->dev;
	int npages = nvbo->bo.num_pages;
	struct sg_table *sg;
	int nents;

	mutex_lock(&dev->struct_mutex);
	sg = drm_prime_pages_to_sg(nvbo->bo.ttm->pages, npages);
	nents = dma_map_sg(attachment->dev, sg->sgl, sg->nents, dir);
	mutex_unlock(&dev->struct_mutex);
	return sg;
}

static void nouveau_gem_unmap_dma_buf(struct dma_buf_attachment *attachment,
				      struct sg_table *sg, enum dma_data_direction dir)
{
	dma_unmap_sg(attachment->dev, sg->sgl, sg->nents, dir);
	sg_free_table(sg);
	kfree(sg);
}

static void nouveau_gem_dmabuf_release(struct dma_buf *dma_buf)
{
	struct nouveau_bo *nvbo = dma_buf->priv;

	if (nvbo->gem->export_dma_buf == dma_buf) {
		nvbo->gem->export_dma_buf = NULL;
		drm_gem_object_unreference_unlocked(nvbo->gem);
	}
}

static void *nouveau_gem_kmap_atomic(struct dma_buf *dma_buf, unsigned long page_num)
{
	return NULL;
}

static void nouveau_gem_kunmap_atomic(struct dma_buf *dma_buf, unsigned long page_num, void *addr)
{

}
static void *nouveau_gem_kmap(struct dma_buf *dma_buf, unsigned long page_num)
{
	return NULL;
}

static void nouveau_gem_kunmap(struct dma_buf *dma_buf, unsigned long page_num, void *addr)
{

}

static const struct dma_buf_ops nouveau_dmabuf_ops =  {
	.map_dma_buf = nouveau_gem_map_dma_buf,
	.unmap_dma_buf = nouveau_gem_unmap_dma_buf,
	.release = nouveau_gem_dmabuf_release,
	.kmap = nouveau_gem_kmap,
	.kmap_atomic = nouveau_gem_kmap_atomic,
	.kunmap = nouveau_gem_kunmap,
	.kunmap_atomic = nouveau_gem_kunmap_atomic,
};

static int
nouveau_prime_new(struct drm_device *dev,
		  size_t size,
		  struct sg_table *sg,
		  struct nouveau_bo **pnvbo)
{
	struct nouveau_bo *nvbo;
	u32 flags = 0;
	int ret;

	flags = TTM_PL_FLAG_TT;

	ret = nouveau_bo_new(dev, size, 0, flags, 0, 0,
			     sg, pnvbo);
	if (ret)
		return ret;
	nvbo = *pnvbo;

	/* we restrict allowed domains on nv50+ to only the types
	 * that were requested at creation time.  not possibly on
	 * earlier chips without busting the ABI.
	 */
	nvbo->valid_domains = NOUVEAU_GEM_DOMAIN_GART;
	nvbo->gem = drm_gem_object_alloc(dev, nvbo->bo.mem.size);
	if (!nvbo->gem) {
		nouveau_bo_ref(NULL, pnvbo);
		return -ENOMEM;
	}

	nvbo->gem->driver_private = nvbo;
	return 0;
}

struct dma_buf *nouveau_gem_prime_export(struct drm_device *dev,
				struct drm_gem_object *obj, int flags)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(obj);
	int ret = 0;

	/* pin buffer into GTT */
	ret = nouveau_bo_pin(nvbo, TTM_PL_FLAG_TT);
	if (ret)
		return ERR_PTR(-EINVAL);

	return dma_buf_export(nvbo, &nouveau_dmabuf_ops, obj->size, flags);
}

struct drm_gem_object *nouveau_gem_prime_import(struct drm_device *dev,
				struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sg;
	struct nouveau_bo *nvbo;
	int ret;

	if (dma_buf->ops == &nouveau_dmabuf_ops) {
		nvbo = dma_buf->priv;
		if (nvbo->gem) {
			if (nvbo->gem->dev == dev) {
				drm_gem_object_reference(nvbo->gem);
				return nvbo->gem;
			}
		}
	}
	/* need to attach */
	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach))
		return ERR_PTR(PTR_ERR(attach));

	sg = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sg)) {
		ret = PTR_ERR(sg);
		goto fail_detach;
	}

	ret = nouveau_prime_new(dev, dma_buf->size, sg, &nvbo);
	if (ret)
		goto fail_unmap;

	nvbo->gem->import_attach = attach;

	return nvbo->gem;

fail_unmap:
	dma_buf_unmap_attachment(attach, sg, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
	return ERR_PTR(ret);
}

