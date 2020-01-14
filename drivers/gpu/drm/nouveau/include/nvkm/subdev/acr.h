/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_ACR_H__
#define __NVKM_ACR_H__
#define nvkm_acr(p) container_of((p), struct nvkm_acr, subdev)
#include <core/subdev.h>
struct nvkm_falcon;

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

	struct list_head lsfw, lsf;
};

int nvkm_acr_bootstrap_falcons(struct nvkm_device *, unsigned long mask);

int gm200_acr_new(struct nvkm_device *, int, struct nvkm_acr **);
int gm20b_acr_new(struct nvkm_device *, int, struct nvkm_acr **);
int gp102_acr_new(struct nvkm_device *, int, struct nvkm_acr **);
int gp108_acr_new(struct nvkm_device *, int, struct nvkm_acr **);
int gp10b_acr_new(struct nvkm_device *, int, struct nvkm_acr **);

struct nvkm_acr_lsfw {
	const struct nvkm_acr_lsf_func *func;
	struct nvkm_falcon *falcon;
	enum nvkm_acr_lsf_id id;

	struct list_head head;

	struct nvkm_blob img;

	const struct firmware *sig;

	u32 bootloader_size;
	u32 bootloader_imem_offset;

	u32 app_size;
	u32 app_start_offset;
	u32 app_imem_entry;
	u32 app_resident_code_offset;
	u32 app_resident_code_size;
	u32 app_resident_data_offset;
	u32 app_resident_data_size;

	u32 ucode_size;
	u32 data_size;
};

struct nvkm_acr_lsf_func {
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
nvkm_acr_lsfw_load_bl_inst_data_sig(struct nvkm_subdev *, struct nvkm_falcon *,
				    enum nvkm_acr_lsf_id, const char *path,
				    int ver, const struct nvkm_acr_lsf_func *);
#endif
