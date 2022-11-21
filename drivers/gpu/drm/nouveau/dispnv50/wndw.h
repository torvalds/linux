#ifndef __NV50_KMS_WNDW_H__
#define __NV50_KMS_WNDW_H__
#define nv50_wndw(p) container_of((p), struct nv50_wndw, plane)
#include "disp.h"
#include "atom.h"
#include "lut.h"

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

	struct nv50_lut ilut;

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

	int (*sema_set)(struct nv50_wndw *, struct nv50_wndw_atom *);
	int (*sema_clr)(struct nv50_wndw *);
	void (*ntfy_reset)(struct nouveau_bo *, u32 offset);
	int (*ntfy_set)(struct nv50_wndw *, struct nv50_wndw_atom *);
	int (*ntfy_clr)(struct nv50_wndw *);
	int (*ntfy_wait_begun)(struct nouveau_bo *, u32 offset,
			       struct nvif_device *);
	void (*ilut)(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyh, int size);
	void (*csc)(struct nv50_wndw *, struct nv50_wndw_atom *,
		    const struct drm_color_ctm *);
	int (*csc_set)(struct nv50_wndw *, struct nv50_wndw_atom *);
	int (*csc_clr)(struct nv50_wndw *);
	bool ilut_identity;
	int  ilut_size;
	bool olut_core;
	int (*xlut_set)(struct nv50_wndw *, struct nv50_wndw_atom *);
	int (*xlut_clr)(struct nv50_wndw *);
	int (*image_set)(struct nv50_wndw *, struct nv50_wndw_atom *);
	int (*image_clr)(struct nv50_wndw *);
	int (*scale_set)(struct nv50_wndw *, struct nv50_wndw_atom *);
	int (*blend_set)(struct nv50_wndw *, struct nv50_wndw_atom *);

	int (*update)(struct nv50_wndw *, u32 *interlock);
};

extern const struct drm_plane_funcs nv50_wndw;

void base507c_ntfy_reset(struct nouveau_bo *, u32);
int base507c_ntfy_set(struct nv50_wndw *, struct nv50_wndw_atom *);
int base507c_ntfy_clr(struct nv50_wndw *);
int base507c_ntfy_wait_begun(struct nouveau_bo *, u32, struct nvif_device *);
int base507c_image_clr(struct nv50_wndw *);
int base507c_update(struct nv50_wndw *, u32 *);

void base907c_csc(struct nv50_wndw *, struct nv50_wndw_atom *,
		  const struct drm_color_ctm *);

struct nv50_wimm_func {
	int (*point)(struct nv50_wndw *, struct nv50_wndw_atom *);

	int (*update)(struct nv50_wndw *, u32 *interlock);
};

extern const struct nv50_wimm_func curs507a;
bool curs507a_space(struct nv50_wndw *);

static inline __must_check int
nvif_chan_wait(struct nv50_dmac *dmac, u32 size)
{
	struct nv50_wndw *wndw = container_of(dmac, typeof(*wndw), wimm);
	return curs507a_space(wndw) ? 0 : -ETIMEDOUT;
}

int wndwc37e_new(struct nouveau_drm *, enum drm_plane_type, int, s32,
		 struct nv50_wndw **);
int wndwc37e_new_(const struct nv50_wndw_func *, struct nouveau_drm *,
		  enum drm_plane_type type, int index, s32 oclass, u32 heads,
		  struct nv50_wndw **);
int wndwc37e_acquire(struct nv50_wndw *, struct nv50_wndw_atom *,
		     struct nv50_head_atom *);
void wndwc37e_release(struct nv50_wndw *, struct nv50_wndw_atom *,
		      struct nv50_head_atom *);
int wndwc37e_sema_set(struct nv50_wndw *, struct nv50_wndw_atom *);
int wndwc37e_sema_clr(struct nv50_wndw *);
int wndwc37e_ntfy_set(struct nv50_wndw *, struct nv50_wndw_atom *);
int wndwc37e_ntfy_clr(struct nv50_wndw *);
int wndwc37e_image_clr(struct nv50_wndw *);
int wndwc37e_blend_set(struct nv50_wndw *, struct nv50_wndw_atom *);
int wndwc37e_update(struct nv50_wndw *, u32 *);

int wndwc57e_new(struct nouveau_drm *, enum drm_plane_type, int, s32,
		 struct nv50_wndw **);
void wndwc57e_ilut(struct nv50_wndw *, struct nv50_wndw_atom *, int);
int wndwc57e_ilut_set(struct nv50_wndw *, struct nv50_wndw_atom *);
int wndwc57e_ilut_clr(struct nv50_wndw *);
int wndwc57e_csc_set(struct nv50_wndw *, struct nv50_wndw_atom *);
int wndwc57e_csc_clr(struct nv50_wndw *);

int wndwc67e_new(struct nouveau_drm *, enum drm_plane_type, int, s32,
		 struct nv50_wndw **);

int nv50_wndw_new(struct nouveau_drm *, enum drm_plane_type, int index,
		  struct nv50_wndw **);
#endif
