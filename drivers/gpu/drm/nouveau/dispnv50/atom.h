#ifndef __NV50_KMS_ATOM_H__
#define __NV50_KMS_ATOM_H__
#define nv50_atom(p) container_of((p), struct nv50_atom, state)
#include <drm/drm_atomic.h>

struct nv50_atom {
	struct drm_atomic_state state;

	struct list_head outp;
	bool lock_core;
	bool flush_disable;
};

#define nv50_head_atom(p) container_of((p), struct nv50_head_atom, state)

struct nv50_head_atom {
	struct drm_crtc_state state;

	struct {
		u32 mask;
		u32 olut;
	} wndw;

	struct {
		u16 iW;
		u16 iH;
		u16 oW;
		u16 oH;
	} view;

	struct nv50_head_mode {
		bool interlace;
		u32 clock;
		struct {
			u16 active;
			u16 synce;
			u16 blanke;
			u16 blanks;
		} h;
		struct {
			u32 active;
			u16 synce;
			u16 blanke;
			u16 blanks;
			u16 blank2s;
			u16 blank2e;
			u16 blankus;
		} v;
	} mode;

	struct {
		bool visible;
		u32 handle;
		u64 offset:40;
		u8 buffer:1;
		u8 mode:4;
		u16 size:11;
		u8 range:2;
		u8 output_mode:2;
		void (*load)(struct drm_color_lut *, int size, void __iomem *);
	} olut;

	struct {
		bool visible;
		u32 handle;
		u64 offset:40;
		u8  format;
		u8  kind:7;
		u8  layout:1;
		u8  blockh:4;
		u16 blocks:12;
		u32 pitch:20;
		u16 x;
		u16 y;
		u16 w;
		u16 h;
	} core;

	struct {
		bool visible;
		u32 handle;
		u64 offset:40;
		u8  layout:2;
		u8  format:8;
	} curs;

	struct {
		u8  depth;
		u8  cpp;
		u16 x;
		u16 y;
		u16 w;
		u16 h;
	} base;

	struct {
		u8 cpp;
	} ovly;

	struct {
		bool enable:1;
		u8 bits:2;
		u8 mode:4;
	} dither;

	struct {
		struct {
			u16 cos:12;
			u16 sin:12;
		} sat;
	} procamp;

	struct {
		u8 nhsync:1;
		u8 nvsync:1;
		u8 depth:4;
		u8 bpc;
	} or;

	/* Currently only used for MST */
	struct {
		int pbn;
		u8 tu:6;
	} dp;

	union nv50_head_atom_mask {
		struct {
			bool olut:1;
			bool core:1;
			bool curs:1;
			bool view:1;
			bool mode:1;
			bool base:1;
			bool ovly:1;
			bool dither:1;
			bool procamp:1;
			bool or:1;
		};
		u16 mask;
	} set, clr;
};

static inline struct nv50_head_atom *
nv50_head_atom_get(struct drm_atomic_state *state, struct drm_crtc *crtc)
{
	struct drm_crtc_state *statec = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(statec))
		return (void *)statec;
	return nv50_head_atom(statec);
}

#define nv50_wndw_atom(p) container_of((p), struct nv50_wndw_atom, state)

struct nv50_wndw_atom {
	struct drm_plane_state state;

	struct drm_property_blob *ilut;
	bool visible;

	struct {
		u32  handle;
		u16  offset:12;
		bool awaken:1;
	} ntfy;

	struct {
		u32 handle;
		u16 offset:12;
		u32 acquire;
		u32 release;
	} sema;

	struct {
		u32 handle;
		struct {
			u64 offset:40;
			u8  buffer:1;
			u8  enable:2;
			u8  mode:4;
			u16 size:11;
			u8  range:2;
			u8  output_mode:2;
			void (*load)(struct drm_color_lut *, int size,
				     void __iomem *);
		} i;
	} xlut;

	struct {
		u32 matrix[12];
		bool valid;
	} csc;

	struct {
		u8  mode:2;
		u8  interval:4;

		u8  colorspace:2;
		u8  format;
		u8  kind:7;
		u8  layout:1;
		u8  blockh:4;
		u16 blocks[3];
		u32 pitch[3];
		u16 w;
		u16 h;

		u32 handle[6];
		u64 offset[6];
	} image;

	struct {
		u16 sx;
		u16 sy;
		u16 sw;
		u16 sh;
		u16 dw;
		u16 dh;
	} scale;

	struct {
		u16 x;
		u16 y;
	} point;

	struct {
		u8 depth;
		u8 k1;
		u8 src_color:4;
		u8 dst_color:4;
	} blend;

	union nv50_wndw_atom_mask {
		struct {
			bool ntfy:1;
			bool sema:1;
			bool xlut:1;
			bool csc:1;
			bool image:1;
			bool scale:1;
			bool point:1;
			bool blend:1;
		};
		u8 mask;
	} set, clr;
};
#endif
