#ifndef __NV50_KMS_HEAD_H__
#define __NV50_KMS_HEAD_H__
#define nv50_head(c) container_of((c), struct nv50_head, base.base)
#include "disp.h"
#include "atom.h"
#include "lut.h"

#include "nouveau_crtc.h"

struct nv50_head {
	const struct nv50_head_func *func;
	struct nouveau_crtc base;
	struct nv50_lut olut;
};

int nv50_head_create(struct drm_device *, int index);
void nv50_head_flush_set(struct nv50_head *, struct nv50_head_atom *);
void nv50_head_flush_clr(struct nv50_head *, struct nv50_head_atom *, bool y);

struct nv50_head_func {
	void (*view)(struct nv50_head *, struct nv50_head_atom *);
	void (*mode)(struct nv50_head *, struct nv50_head_atom *);
	void (*olut)(struct nv50_head *, struct nv50_head_atom *);
	bool olut_identity;
	void (*olut_set)(struct nv50_head *, struct nv50_head_atom *);
	void (*olut_clr)(struct nv50_head *);
	void (*core_calc)(struct nv50_head *, struct nv50_head_atom *);
	void (*core_set)(struct nv50_head *, struct nv50_head_atom *);
	void (*core_clr)(struct nv50_head *);
	int (*curs_layout)(struct nv50_head *, struct nv50_wndw_atom *,
			   struct nv50_head_atom *);
	int (*curs_format)(struct nv50_head *, struct nv50_wndw_atom *,
			   struct nv50_head_atom *);
	void (*curs_set)(struct nv50_head *, struct nv50_head_atom *);
	void (*curs_clr)(struct nv50_head *);
	void (*base)(struct nv50_head *, struct nv50_head_atom *);
	void (*ovly)(struct nv50_head *, struct nv50_head_atom *);
	void (*dither)(struct nv50_head *, struct nv50_head_atom *);
	void (*procamp)(struct nv50_head *, struct nv50_head_atom *);
	void (*or)(struct nv50_head *, struct nv50_head_atom *);
};

extern const struct nv50_head_func head507d;
void head507d_view(struct nv50_head *, struct nv50_head_atom *);
void head507d_mode(struct nv50_head *, struct nv50_head_atom *);
void head507d_olut(struct nv50_head *, struct nv50_head_atom *);
void head507d_core_calc(struct nv50_head *, struct nv50_head_atom *);
void head507d_core_clr(struct nv50_head *);
int head507d_curs_layout(struct nv50_head *, struct nv50_wndw_atom *,
			 struct nv50_head_atom *);
int head507d_curs_format(struct nv50_head *, struct nv50_wndw_atom *,
			 struct nv50_head_atom *);
void head507d_base(struct nv50_head *, struct nv50_head_atom *);
void head507d_ovly(struct nv50_head *, struct nv50_head_atom *);
void head507d_dither(struct nv50_head *, struct nv50_head_atom *);
void head507d_procamp(struct nv50_head *, struct nv50_head_atom *);

extern const struct nv50_head_func head827d;

extern const struct nv50_head_func head907d;
void head907d_view(struct nv50_head *, struct nv50_head_atom *);
void head907d_mode(struct nv50_head *, struct nv50_head_atom *);
void head907d_olut(struct nv50_head *, struct nv50_head_atom *);
void head907d_olut_set(struct nv50_head *, struct nv50_head_atom *);
void head907d_olut_clr(struct nv50_head *);
void head907d_core_set(struct nv50_head *, struct nv50_head_atom *);
void head907d_core_clr(struct nv50_head *);
void head907d_curs_set(struct nv50_head *, struct nv50_head_atom *);
void head907d_curs_clr(struct nv50_head *);
void head907d_ovly(struct nv50_head *, struct nv50_head_atom *);
void head907d_procamp(struct nv50_head *, struct nv50_head_atom *);
void head907d_or(struct nv50_head *, struct nv50_head_atom *);

extern const struct nv50_head_func head917d;
int head917d_curs_layout(struct nv50_head *, struct nv50_wndw_atom *,
			 struct nv50_head_atom *);

extern const struct nv50_head_func headc37d;
void headc37d_view(struct nv50_head *, struct nv50_head_atom *);
void headc37d_core_set(struct nv50_head *, struct nv50_head_atom *);
void headc37d_core_clr(struct nv50_head *);
int headc37d_curs_format(struct nv50_head *, struct nv50_wndw_atom *,
			 struct nv50_head_atom *);
void headc37d_curs_set(struct nv50_head *, struct nv50_head_atom *);
void headc37d_curs_clr(struct nv50_head *);
void headc37d_dither(struct nv50_head *, struct nv50_head_atom *);

extern const struct nv50_head_func headc57d;
#endif
