/* SPDX-License-Identifier: MIT */
#ifndef __NV50_CRC_H__
#define __NV50_CRC_H__

#include <linux/mutex.h>
#include <drm/drm_crtc.h>
#include <drm/drm_vblank_work.h>

#include <nvif/mem.h>
#include <nvkm/subdev/bios.h>
#include "nouveau_encoder.h"

struct nv50_atom;
struct nv50_disp;
struct nv50_head;

#if IS_ENABLED(CONFIG_DEBUG_FS)
enum nv50_crc_source {
	NV50_CRC_SOURCE_NONE = 0,
	NV50_CRC_SOURCE_AUTO,
	NV50_CRC_SOURCE_RG,
	NV50_CRC_SOURCE_OUTP_ACTIVE,
	NV50_CRC_SOURCE_OUTP_COMPLETE,
	NV50_CRC_SOURCE_OUTP_INACTIVE,
};

/* RG -> SF (DP only)
 *    -> SOR
 *    -> PIOR
 *    -> DAC
 */
enum nv50_crc_source_type {
	NV50_CRC_SOURCE_TYPE_NONE = 0,
	NV50_CRC_SOURCE_TYPE_SOR,
	NV50_CRC_SOURCE_TYPE_PIOR,
	NV50_CRC_SOURCE_TYPE_DAC,
	NV50_CRC_SOURCE_TYPE_RG,
	NV50_CRC_SOURCE_TYPE_SF,
};

struct nv50_crc_notifier_ctx {
	struct nvif_mem mem;
	struct nvif_object ntfy;
};

struct nv50_crc_atom {
	enum nv50_crc_source src;
	/* Only used for gv100+ */
	u8 wndw : 4;
};

struct nv50_crc_func {
	int (*set_src)(struct nv50_head *, int or, enum nv50_crc_source_type,
		       struct nv50_crc_notifier_ctx *, u32 wndw);
	int (*set_ctx)(struct nv50_head *, struct nv50_crc_notifier_ctx *);
	u32 (*get_entry)(struct nv50_head *, struct nv50_crc_notifier_ctx *,
			 enum nv50_crc_source, int idx);
	bool (*ctx_finished)(struct nv50_head *,
			     struct nv50_crc_notifier_ctx *);
	short flip_threshold;
	short num_entries;
	size_t notifier_len;
};

struct nv50_crc {
	spinlock_t lock;
	struct nv50_crc_notifier_ctx ctx[2];
	struct drm_vblank_work flip_work;
	enum nv50_crc_source src;

	u64 frame;
	short entry_idx;
	short flip_threshold;
	u8 ctx_idx : 1;
	bool ctx_changed : 1;
};

void nv50_crc_init(struct drm_device *dev);
int nv50_head_crc_late_register(struct nv50_head *);
void nv50_crc_handle_vblank(struct nv50_head *head);

int nv50_crc_verify_source(struct drm_crtc *, const char *, size_t *);
const char *const *nv50_crc_get_sources(struct drm_crtc *, size_t *);
int nv50_crc_set_source(struct drm_crtc *, const char *);

int nv50_crc_atomic_check_head(struct nv50_head *, struct nv50_head_atom *,
			       struct nv50_head_atom *);
void nv50_crc_atomic_check_outp(struct nv50_atom *atom);
void nv50_crc_atomic_stop_reporting(struct drm_atomic_state *);
void nv50_crc_atomic_init_notifier_contexts(struct drm_atomic_state *);
void nv50_crc_atomic_release_notifier_contexts(struct drm_atomic_state *);
void nv50_crc_atomic_start_reporting(struct drm_atomic_state *);
void nv50_crc_atomic_set(struct nv50_head *, struct nv50_head_atom *);
void nv50_crc_atomic_clr(struct nv50_head *);

extern const struct nv50_crc_func crc907d;
extern const struct nv50_crc_func crcc37d;

#else /* IS_ENABLED(CONFIG_DEBUG_FS) */
struct nv50_crc {};
struct nv50_crc_func {};
struct nv50_crc_atom {};

#define nv50_crc_verify_source NULL
#define nv50_crc_get_sources NULL
#define nv50_crc_set_source NULL

static inline void nv50_crc_init(struct drm_device *dev) {}
static inline int
nv50_head_crc_late_register(struct nv50_head *head) { return 0; }
static inline void nv50_crc_handle_vblank(struct nv50_head *head) {}

static inline int
nv50_crc_atomic_check_head(struct nv50_head *head,
			   struct nv50_head_atom *asyh,
			   struct nv50_head_atom *armh) { return 0; }
static inline void nv50_crc_atomic_check_outp(struct nv50_atom *atom) {}
static inline void
nv50_crc_atomic_stop_reporting(struct drm_atomic_state *state) {}
static inline void
nv50_crc_atomic_init_notifier_contexts(struct drm_atomic_state *state) {}
static inline void
nv50_crc_atomic_release_notifier_contexts(struct drm_atomic_state *state) {}
static inline void
nv50_crc_atomic_start_reporting(struct drm_atomic_state *state) {}
static inline void
nv50_crc_atomic_set(struct nv50_head *head, struct nv50_head_atom *state) {}
static inline void
nv50_crc_atomic_clr(struct nv50_head *head) {}

#endif /* IS_ENABLED(CONFIG_DEBUG_FS) */
#endif /* !__NV50_CRC_H__ */
