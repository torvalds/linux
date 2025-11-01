/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_GSP_PRIV_H__
#define __NVKM_GSP_PRIV_H__
#include <subdev/gsp.h>
#include <rm/gpu.h>
enum nvkm_acr_lsf_id;

int nvkm_gsp_fwsec_frts(struct nvkm_gsp *);
int nvkm_gsp_fwsec_sb(struct nvkm_gsp *);

struct nvkm_gsp_fwif {
	int version;
	int (*load)(struct nvkm_gsp *, int ver, const struct nvkm_gsp_fwif *);
	const struct nvkm_gsp_func *func;
	const struct nvkm_rm_impl *rm;
	const char *ver;
};

int nvkm_gsp_load_fw(struct nvkm_gsp *, const char *name, const char *ver,
		     const struct firmware **);
void nvkm_gsp_dtor_fws(struct nvkm_gsp *);

int gv100_gsp_nofw(struct nvkm_gsp *, int, const struct nvkm_gsp_fwif *);

int tu102_gsp_load(struct nvkm_gsp *, int, const struct nvkm_gsp_fwif *);
int tu102_gsp_load_rm(struct nvkm_gsp *, const struct nvkm_gsp_fwif *);

int gh100_gsp_load(struct nvkm_gsp *, int, const struct nvkm_gsp_fwif *);

#define NVKM_GSP_FIRMWARE_BOOTER(chip,vers)                      \
MODULE_FIRMWARE("nvidia/"#chip"/gsp/booter_load-"#vers".bin");   \
MODULE_FIRMWARE("nvidia/"#chip"/gsp/booter_unload-"#vers".bin"); \
MODULE_FIRMWARE("nvidia/"#chip"/gsp/bootloader-"#vers".bin");    \
MODULE_FIRMWARE("nvidia/"#chip"/gsp/gsp-"#vers".bin")

#define NVKM_GSP_FIRMWARE_FMC(chip,vers)                      \
MODULE_FIRMWARE("nvidia/"#chip"/gsp/fmc-"#vers".bin");        \
MODULE_FIRMWARE("nvidia/"#chip"/gsp/bootloader-"#vers".bin"); \
MODULE_FIRMWARE("nvidia/"#chip"/gsp/gsp-"#vers".bin")

struct nvkm_gsp_func {
	const struct nvkm_falcon_func *flcn;
	const struct nvkm_falcon_fw_func *fwsec;

	char *sig_section;

	struct {
		int (*ctor)(struct nvkm_gsp *, const char *name, const struct firmware *,
			    struct nvkm_falcon *, struct nvkm_falcon_fw *);
	} booter;

	void (*dtor)(struct nvkm_gsp *);
	int (*oneinit)(struct nvkm_gsp *);
	int (*init)(struct nvkm_gsp *);
	int (*fini)(struct nvkm_gsp *, bool suspend);
	int (*reset)(struct nvkm_gsp *);

	struct {
		const struct nvkm_rm_gpu *gpu;
	} rm;
};

extern const struct nvkm_falcon_func tu102_gsp_flcn;
extern const struct nvkm_falcon_fw_func tu102_gsp_fwsec;
int tu102_gsp_booter_ctor(struct nvkm_gsp *, const char *, const struct firmware *,
			  struct nvkm_falcon *, struct nvkm_falcon_fw *);
int tu102_gsp_oneinit(struct nvkm_gsp *);
int tu102_gsp_init(struct nvkm_gsp *);
int tu102_gsp_fini(struct nvkm_gsp *, bool suspend);
int tu102_gsp_reset(struct nvkm_gsp *);
u64 tu102_gsp_wpr_heap_size(struct nvkm_gsp *);

extern const struct nvkm_falcon_func ga102_gsp_flcn;
extern const struct nvkm_falcon_fw_func ga102_gsp_fwsec;
int ga102_gsp_booter_ctor(struct nvkm_gsp *, const char *, const struct firmware *,
			  struct nvkm_falcon *, struct nvkm_falcon_fw *);
int ga102_gsp_reset(struct nvkm_gsp *);

int gh100_gsp_oneinit(struct nvkm_gsp *);
int gh100_gsp_init(struct nvkm_gsp *);
int gh100_gsp_fini(struct nvkm_gsp *, bool suspend);

void r535_gsp_dtor(struct nvkm_gsp *);
int r535_gsp_oneinit(struct nvkm_gsp *);
int r535_gsp_init(struct nvkm_gsp *);
int r535_gsp_fini(struct nvkm_gsp *, bool suspend);

int nvkm_gsp_new_(const struct nvkm_gsp_fwif *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  struct nvkm_gsp **);

extern const struct nvkm_gsp_func gv100_gsp;
#endif
