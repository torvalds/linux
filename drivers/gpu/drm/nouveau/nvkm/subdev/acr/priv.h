#ifndef __NVKM_ACR_PRIV_H__
#define __NVKM_ACR_PRIV_H__
#include <subdev/acr.h>
struct lsb_header_tail;

struct nvkm_acr_fwif {
	int version;
	int (*load)(struct nvkm_acr *, int version,
		    const struct nvkm_acr_fwif *);
	const struct nvkm_acr_func *func;
};

int gm200_acr_nofw(struct nvkm_acr *, int, const struct nvkm_acr_fwif *);
int gm20b_acr_load(struct nvkm_acr *, int, const struct nvkm_acr_fwif *);
int gp102_acr_load(struct nvkm_acr *, int, const struct nvkm_acr_fwif *);

struct nvkm_acr_lsf;
struct nvkm_acr_func {
	const struct nvkm_acr_hsf_fwif *load;
	const struct nvkm_acr_hsf_fwif *ahesasc;
	const struct nvkm_acr_hsf_fwif *asb;
	const struct nvkm_acr_hsf_fwif *unload;
	int (*wpr_parse)(struct nvkm_acr *);
	u32 (*wpr_layout)(struct nvkm_acr *);
	int (*wpr_alloc)(struct nvkm_acr *, u32 wpr_size);
	int (*wpr_build)(struct nvkm_acr *, struct nvkm_acr_lsf *rtos);
	int (*wpr_patch)(struct nvkm_acr *, s64 adjust);
	void (*wpr_check)(struct nvkm_acr *, u64 *start, u64 *limit);
	int (*init)(struct nvkm_acr *);
	void (*fini)(struct nvkm_acr *);
	u64 bootstrap_falcons;
};

extern const struct nvkm_acr_func gm200_acr;
int gm200_acr_wpr_parse(struct nvkm_acr *);
u32 gm200_acr_wpr_layout(struct nvkm_acr *);
int gm200_acr_wpr_build(struct nvkm_acr *, struct nvkm_acr_lsf *);
int gm200_acr_wpr_patch(struct nvkm_acr *, s64);
void gm200_acr_wpr_check(struct nvkm_acr *, u64 *, u64 *);
void gm200_acr_wpr_build_lsb_tail(struct nvkm_acr_lsfw *,
				  struct lsb_header_tail *);
int gm200_acr_init(struct nvkm_acr *);

int gm20b_acr_wpr_alloc(struct nvkm_acr *, u32 wpr_size);

int gp102_acr_wpr_parse(struct nvkm_acr *);
u32 gp102_acr_wpr_layout(struct nvkm_acr *);
int gp102_acr_wpr_alloc(struct nvkm_acr *, u32 wpr_size);
int gp102_acr_wpr_build(struct nvkm_acr *, struct nvkm_acr_lsf *);
int gp102_acr_wpr_build_lsb(struct nvkm_acr *, struct nvkm_acr_lsfw *);
int gp102_acr_wpr_patch(struct nvkm_acr *, s64);

int tu102_acr_init(struct nvkm_acr *);

void ga100_acr_wpr_check(struct nvkm_acr *, u64 *, u64 *);

struct nvkm_acr_hsfw {
	struct nvkm_falcon_fw fw;

	enum nvkm_acr_hsf_id {
		NVKM_ACR_HSF_PMU,
		NVKM_ACR_HSF_SEC2,
		NVKM_ACR_HSF_GSP,
	} falcon_id;
	u32 boot_mbox0;
	u32 intr_clear;

	struct list_head head;
};

int nvkm_acr_hsfw_boot(struct nvkm_acr *, const char *name);

struct nvkm_acr_hsf_fwif {
	int version;
	int (*load)(struct nvkm_acr *, const char *bl, const char *fw,
		    const char *name, int version,
		    const struct nvkm_acr_hsf_fwif *);
	const struct nvkm_falcon_fw_func *func;

	enum nvkm_acr_hsf_id falcon_id;
	u32 boot_mbox0;
	u32 intr_clear;
};


int gm200_acr_hsfw_ctor(struct nvkm_acr *, const char *, const char *, const char *, int,
			const struct nvkm_acr_hsf_fwif *);
int gm200_acr_hsfw_load_bld(struct nvkm_falcon_fw *);
extern const struct nvkm_falcon_fw_func gm200_acr_unload_0;

extern const struct nvkm_falcon_fw_func gm20b_acr_load_0;

int gp102_acr_load_setup(struct nvkm_falcon_fw *);

extern const struct nvkm_falcon_fw_func gp108_acr_load_0;

extern const struct nvkm_falcon_fw_func gp108_acr_hsfw_0;
int gp108_acr_hsfw_load_bld(struct nvkm_falcon_fw *);

int ga100_acr_hsfw_ctor(struct nvkm_acr *, const char *, const char *, const char *, int,
			const struct nvkm_acr_hsf_fwif *);

int nvkm_acr_new_(const struct nvkm_acr_fwif *, struct nvkm_device *, enum nvkm_subdev_type,
		  int inst, struct nvkm_acr **);

struct nvkm_acr_lsf {
	const struct nvkm_acr_lsf_func *func;
	struct nvkm_falcon *falcon;
	enum nvkm_acr_lsf_id id;
	struct list_head head;
};

struct nvkm_acr_lsfw *nvkm_acr_lsfw_add(const struct nvkm_acr_lsf_func *,
					struct nvkm_acr *, struct nvkm_falcon *,
					enum nvkm_acr_lsf_id);
void nvkm_acr_lsfw_del(struct nvkm_acr_lsfw *);
void nvkm_acr_lsfw_del_all(struct nvkm_acr *);
#endif
