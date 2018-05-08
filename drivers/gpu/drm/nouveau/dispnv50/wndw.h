#ifndef __NV50_KMS_WNDW_H__
#define __NV50_KMS_WNDW_H__
#define nv50_wndw(p) container_of((p), struct nv50_wndw, plane)
#include "disp.h"
#include "atom.h"

#include <nvif/notify.h>

struct nv50_wndw_ctxdma {
	struct list_head head;
	struct nvif_object object;
};

struct nv50_wndw {
	const struct nv50_wndw_func *func;
	const struct nv50_wimm_func *immd;
	int id;
	struct nv50_disp_interlock interlock;

	struct {
		struct nvif_object *parent;
		struct list_head list;
	} ctxdma;

	struct drm_plane plane;

	struct nv50_dmac wndw;
	struct nv50_dmac wimm;

	struct nvif_notify notify;
	u16 ntfy;
	u16 sema;
	u32 data;
};

int nv50_wndw_new_(const struct nv50_wndw_func *, struct drm_device *,
		   enum drm_plane_type, const char *name, int index,
		   const u32 *format, enum nv50_disp_interlock_type,
		   u32 interlock_data, u32 heads, struct nv50_wndw **);
void nv50_wndw_init(struct nv50_wndw *);
void nv50_wndw_fini(struct nv50_wndw *);
void nv50_wndw_flush_set(struct nv50_wndw *, u32 *interlock,
			 struct nv50_wndw_atom *);
void nv50_wndw_flush_clr(struct nv50_wndw *, u32 *interlock, bool flush,
			 struct nv50_wndw_atom *);
void nv50_wndw_ntfy_enable(struct nv50_wndw *, struct nv50_wndw_atom *);
int nv50_wndw_wait_armed(struct nv50_wndw *, struct nv50_wndw_atom *);

struct nv50_wndw_func {
	int (*acquire)(struct nv50_wndw *, struct nv50_wndw_atom *asyw,
		       struct nv50_head_atom *asyh);
	void (*release)(struct nv50_wndw *, struct nv50_wndw_atom *asyw,
			struct nv50_head_atom *asyh);
	void (*prepare)(struct nv50_wndw *, struct nv50_head_atom *asyh,
			struct nv50_wndw_atom *asyw);

	void (*sema_set)(struct nv50_wndw *, struct nv50_wndw_atom *);
	void (*sema_clr)(struct nv50_wndw *);
	void (*ntfy_reset)(struct nouveau_bo *, u32 offset);
	void (*ntfy_set)(struct nv50_wndw *, struct nv50_wndw_atom *);
	void (*ntfy_clr)(struct nv50_wndw *);
	int (*ntfy_wait_begun)(struct nouveau_bo *, u32 offset,
			       struct nvif_device *);
	void (*image_set)(struct nv50_wndw *, struct nv50_wndw_atom *);
	void (*image_clr)(struct nv50_wndw *);
	void (*lut)(struct nv50_wndw *, struct nv50_wndw_atom *);

	void (*update)(struct nv50_wndw *, u32 *interlock);
};

extern const struct drm_plane_funcs nv50_wndw;

struct nv50_wimm_func {
	void (*point)(struct nv50_wndw *, struct nv50_wndw_atom *);

	void (*update)(struct nv50_wndw *, u32 *interlock);
};
#endif
