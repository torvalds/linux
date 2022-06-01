/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_ACR_H__
#define __NVKM_ACR_H__
#define nvkm_acr(p) container_of((p), struct nvkm_acr, subdev)
#include <core/subdev.h>
#include <core/falcon.h>

enum nvkm_acr_lsf_id {
	NVKM_ACR_LSF_PMU = 0,
	NVKM_ACR_LSF_GSPLITE = 1,
	NVKM_ACR_LSF_FECS = 2,
	NVKM_ACR_LSF_GPCCS = 3,
	NVKM_ACR_LSF_NVDEC = 4,
	NVKM_ACR_LSF_SEC2 = 7,
	NVKM_ACR_LSF_MINION = 10,
	NVKM_ACR_LSF_NUM
};

static inline const char *
nvkm_acr_lsf_id(enum nvkm_acr_lsf_id id)
{
	switch (id) {
	case NVKM_ACR_LSF_PMU    : return "pmu";
	case NVKM_ACR_LSF_GSPLITE: return "gsplite";
	case NVKM_ACR_LSF_FECS   : return "fecs";
	case NVKM_ACR_LSF_GPCCS  : return "gpccs";
	case NVKM_ACR_LSF_NVDEC  : return "nvdec";
	case NVKM_ACR_LSF_SEC2   : return "sec2";
	case NVKM_ACR_LSF_MINION : return "minion";
	default:
		return "unknown";
	}
}

struct nvkm_acr {
	const struct nvkm_acr_func *func;
	struct nvkm_subdev subdev;

	struct list_head hsfw;
	struct list_head lsfw, lsf;

	u64 managed_falcons;

	struct nvkm_memory *wpr;
	u64 wpr_start;
	u64 wpr_end;
	u64 shadow_start;

	struct nvkm_memory *inst;
	struct nvkm_vmm *vmm;

	bool done;
	struct nvkm_acr_lsf *rtos;

	const struct firmware *wpr_fw;
	bool wpr_comp;
	u64 wpr_prev;
};

bool nvkm_acr_managed_falcon(struct nvkm_device *, enum nvkm_acr_lsf_id);
int nvkm_acr_bootstrap_falcons(struct nvkm_device *, unsigned long mask);

int gm200_acr_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_acr **);
int gm20b_acr_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_acr **);
int gp102_acr_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_acr **);
int gp108_acr_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_acr **);
int gp10b_acr_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_acr **);
int gv100_acr_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_acr **);
int tu102_acr_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_acr **);
int ga102_acr_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_acr **);

struct nvkm_acr_lsfw {
	const struct nvkm_acr_lsf_func *func;
	struct nvkm_falcon *falcon;
	enum nvkm_acr_lsf_id id;

	struct list_head head;

	struct nvkm_blob img;

	const struct firmware *sig;

	bool secure_bootloader;
	u32 bootloader_size;
	u32 bootloader_imem_offset;

	u32 app_size;
	u32 app_start_offset;
	u32 app_imem_entry;
	u32 app_resident_code_offset;
	u32 app_resident_code_size;
	u32 app_resident_data_offset;
	u32 app_resident_data_size;
	u32 app_imem_offset;
	u32 app_dmem_offset;

	u32 ucode_size;
	u32 data_size;

	u32 fuse_ver;
	u32 engine_id;
	u32 ucode_id;
	u32 sig_size;
	u32 sig_nr;
	u8 *sigs;

	struct {
		u32 lsb;
		u32 img;
		u32 bld;
	} offset;
	u32 bl_data_size;
};

struct nvkm_acr_lsf_func {
/* The (currently) map directly to LSB header flags. */
#define NVKM_ACR_LSF_LOAD_CODE_AT_0                                  0x00000001
#define NVKM_ACR_LSF_DMACTL_REQ_CTX                                  0x00000004
#define NVKM_ACR_LSF_FORCE_PRIV_LOAD                                 0x00000008
	u32 flags;
	u32 bl_entry;
	u32 bld_size;
	void (*bld_write)(struct nvkm_acr *, u32 bld, struct nvkm_acr_lsfw *);
	void (*bld_patch)(struct nvkm_acr *, u32 bld, s64 adjust);
	u64 bootstrap_falcons;
	int (*bootstrap_falcon)(struct nvkm_falcon *, enum nvkm_acr_lsf_id);
	int (*bootstrap_multiple_falcons)(struct nvkm_falcon *, u32 mask);
};

int
nvkm_acr_lsfw_load_sig_image_desc(struct nvkm_subdev *, struct nvkm_falcon *,
				  enum nvkm_acr_lsf_id, const char *path,
				  int ver, const struct nvkm_acr_lsf_func *);
int
nvkm_acr_lsfw_load_sig_image_desc_v1(struct nvkm_subdev *, struct nvkm_falcon *,
				     enum nvkm_acr_lsf_id, const char *path,
				     int ver, const struct nvkm_acr_lsf_func *);

int
nvkm_acr_lsfw_load_sig_image_desc_v2(struct nvkm_subdev *, struct nvkm_falcon *,
				     enum nvkm_acr_lsf_id, const char *path,
				     int ver, const struct nvkm_acr_lsf_func *);

int
nvkm_acr_lsfw_load_bl_inst_data_sig(struct nvkm_subdev *, struct nvkm_falcon *,
				    enum nvkm_acr_lsf_id, const char *path,
				    int ver, const struct nvkm_acr_lsf_func *);

int
nvkm_acr_lsfw_load_bl_sig_net(struct nvkm_subdev *, struct nvkm_falcon *,
				    enum nvkm_acr_lsf_id, const char *path,
				    int ver, const struct nvkm_acr_lsf_func *,
				    const void *, u32, const void *, u32);
#endif
