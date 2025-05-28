/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <subdev/gsp.h>
#ifndef __NVKM_RM_H__
#define __NVKM_RM_H__
#include "handles.h"
struct nvkm_outp;
struct r535_gr;

struct nvkm_rm_impl {
	const struct nvkm_rm_wpr *wpr;
	const struct nvkm_rm_api *api;
};

struct nvkm_rm {
	struct nvkm_device *device;
	const struct nvkm_rm_gpu *gpu;
	const struct nvkm_rm_wpr *wpr;
	const struct nvkm_rm_api *api;
};

struct nvkm_rm_wpr {
	u32 os_carveout_size;
	u32 base_size;
	u64 heap_size_min;
	u32 heap_size_non_wpr;
	u32 rsvd_size_pmu;
	bool offset_set_by_acr;
};

struct nvkm_rm_api {
	const struct nvkm_rm_api_gsp {
		void (*set_rmargs)(struct nvkm_gsp *, bool resume);
		int (*set_system_info)(struct nvkm_gsp *);
		int (*get_static_info)(struct nvkm_gsp *);
		bool (*xlat_mc_engine_idx)(u32 mc_engine_idx, enum nvkm_subdev_type *, int *inst);
		void (*drop_send_user_shared_data)(struct nvkm_gsp *);
		void (*drop_post_nocat_record)(struct nvkm_gsp *);
		u32 (*sr_data_size)(struct nvkm_gsp *);
	} *gsp;

	const struct nvkm_rm_api_rpc {
		void *(*get)(struct nvkm_gsp *, u32 fn, u32 argc);
		void *(*push)(struct nvkm_gsp *gsp, void *argv,
			      enum nvkm_gsp_rpc_reply_policy policy, u32 repc);
		void (*done)(struct nvkm_gsp *gsp, void *repv);
	} *rpc;

	const struct nvkm_rm_api_ctrl {
		void *(*get)(struct nvkm_gsp_object *, u32 cmd, u32 params_size);
		int (*push)(struct nvkm_gsp_object *, void **params, u32 repc);
		void (*done)(struct nvkm_gsp_object *, void *params);
	} *ctrl;

	const struct nvkm_rm_api_alloc {
		void *(*get)(struct nvkm_gsp_object *, u32 oclass, u32 params_size);
		void *(*push)(struct nvkm_gsp_object *, void *params);
		void (*done)(struct nvkm_gsp_object *, void *params);

		int (*free)(struct nvkm_gsp_object *);
	} *alloc;

	const struct nvkm_rm_api_client {
		int (*ctor)(struct nvkm_gsp_client *, u32 handle);
	} *client;

	const struct nvkm_rm_api_device {
		int (*ctor)(struct nvkm_gsp_client *, struct nvkm_gsp_device *);
		void (*dtor)(struct nvkm_gsp_device *);

		struct {
			int (*ctor)(struct nvkm_gsp_device *, u32 handle, u32 id,
				    nvkm_gsp_event_func, struct nvkm_gsp_event *);
			void (*dtor)(struct nvkm_gsp_event *);
		} event;
	} *device;

	const struct nvkm_rm_api_fbsr {
		int (*suspend)(struct nvkm_gsp *);
		void (*resume)(struct nvkm_gsp *);
	} *fbsr;

	const struct nvkm_rm_api_disp {
		int (*get_static_info)(struct nvkm_disp *);
		int (*get_supported)(struct nvkm_disp *, unsigned long *display_mask);
		int (*get_connect_state)(struct nvkm_disp *, unsigned display_id);
		int (*get_active)(struct nvkm_disp *, unsigned head, u32 *display_id);

		int (*bl_ctrl)(struct nvkm_disp *, unsigned display_id, bool set, int *val);

		struct {
			int (*get_caps)(struct nvkm_disp *, int *link_bw, bool *mst, bool *wm);
			int (*set_indexed_link_rates)(struct nvkm_outp *);
		} dp;

		struct {
			int (*set_pushbuf)(struct nvkm_disp *, s32 oclass, int inst,
					   struct nvkm_memory *);
			int (*dmac_alloc)(struct nvkm_disp *, u32 oclass, int inst, u32 put_offset,
					  struct nvkm_gsp_object *);
		} chan;
	} *disp;

	const struct nvkm_rm_api_fifo {
		int (*xlat_rm_engine_type)(u32 rm_engine_type,
					   enum nvkm_subdev_type *, int *nv2080_type);
		int (*ectx_size)(struct nvkm_fifo *);
		unsigned rsvd_chids;
		int (*rc_triggered)(void *priv, u32 fn, void *repv, u32 repc);
		struct {
			int (*alloc)(struct nvkm_gsp_device *, u32 handle,
				     u32 nv2080_engine_type, u8 runq, bool priv, int chid,
				     u64 inst_addr, u64 userd_addr, u64 mthdbuf_addr,
				     struct nvkm_vmm *, u64 gpfifo_offset, u32 gpfifo_length,
				     struct nvkm_gsp_object *);
		} chan;
	} *fifo;

	const struct nvkm_rm_api_engine {
		int (*alloc)(struct nvkm_gsp_object *chan, u32 handle, u32 class, int inst,
			     struct nvkm_gsp_object *);
	} *ce, *nvdec, *nvenc, *nvjpg, *ofa;

	const struct nvkm_rm_api_gr {
		int (*get_ctxbufs_info)(struct r535_gr *);
		struct {
			int (*init)(struct r535_gr *);
			void (*fini)(struct r535_gr *);
		} scrubber;
	} *gr;
};

extern const struct nvkm_rm_impl r535_rm_tu102;
extern const struct nvkm_rm_impl r535_rm_ga102;
extern const struct nvkm_rm_api_gsp r535_gsp;
typedef struct DOD_METHOD_DATA DOD_METHOD_DATA;
typedef struct JT_METHOD_DATA JT_METHOD_DATA;
typedef struct CAPS_METHOD_DATA CAPS_METHOD_DATA;
void r535_gsp_acpi_dod(acpi_handle, DOD_METHOD_DATA *);
void r535_gsp_acpi_jt(acpi_handle, JT_METHOD_DATA *);
void r535_gsp_acpi_caps(acpi_handle, CAPS_METHOD_DATA *);
struct NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_PARAMS;
void r535_gsp_get_static_info_fb(struct nvkm_gsp *,
				 const struct NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_PARAMS *);
extern const struct nvkm_rm_api_rpc r535_rpc;
extern const struct nvkm_rm_api_ctrl r535_ctrl;
extern const struct nvkm_rm_api_alloc r535_alloc;
extern const struct nvkm_rm_api_client r535_client;
void r535_gsp_client_dtor(struct nvkm_gsp_client *);
extern const struct nvkm_rm_api_device r535_device;
int r535_mmu_vaspace_new(struct nvkm_vmm *, u32 handle, bool external);
void r535_mmu_vaspace_del(struct nvkm_vmm *);
extern const struct nvkm_rm_api_fbsr r535_fbsr;
void r535_fbsr_resume(struct nvkm_gsp *);
int r535_fbsr_memlist(struct nvkm_gsp_device *, u32 handle, enum nvkm_memory_target,
		      u64 phys, u64 size, struct sg_table *, struct nvkm_gsp_object *);
extern const struct nvkm_rm_api_disp r535_disp;
extern const struct nvkm_rm_api_fifo r535_fifo;
void r535_fifo_rc_chid(struct nvkm_fifo *, int chid);
extern const struct nvkm_rm_api_engine r535_ce;
extern const struct nvkm_rm_api_gr r535_gr;
void *r535_gr_dtor(struct nvkm_gr *);
int r535_gr_oneinit(struct nvkm_gr *);
u64 r535_gr_units(struct nvkm_gr *);
int r535_gr_chan_new(struct nvkm_gr *, struct nvkm_chan *, const struct nvkm_oclass *,
		     struct nvkm_object **);
int r535_gr_promote_ctx(struct r535_gr *, bool golden, struct nvkm_vmm *,
			struct nvkm_memory **pctxbuf_mem, struct nvkm_vma **pctxbuf_vma,
			struct nvkm_gsp_object *chan);
extern const struct nvkm_rm_api_engine r535_nvdec;
extern const struct nvkm_rm_api_engine r535_nvenc;
extern const struct nvkm_rm_api_engine r535_nvjpg;
extern const struct nvkm_rm_api_engine r535_ofa;

extern const struct nvkm_rm_impl r570_rm_tu102;
extern const struct nvkm_rm_impl r570_rm_ga102;
extern const struct nvkm_rm_impl r570_rm_gh100;
extern const struct nvkm_rm_impl r570_rm_gb10x;
extern const struct nvkm_rm_impl r570_rm_gb20x;
extern const struct nvkm_rm_api_gsp r570_gsp;
extern const struct nvkm_rm_api_client r570_client;
extern const struct nvkm_rm_api_fbsr r570_fbsr;
extern const struct nvkm_rm_api_disp r570_disp;
extern const struct nvkm_rm_api_fifo r570_fifo;
extern const struct nvkm_rm_api_gr r570_gr;
int r570_gr_gpc_mask(struct nvkm_gsp *, u32 *mask);
int r570_gr_tpc_mask(struct nvkm_gsp *, int gpc, u32 *mask);
extern const struct nvkm_rm_api_engine r570_ofa;
#endif
