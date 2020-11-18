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
	void (*wpr_patch)(struct nvkm_acr *, s64 adjust);
	void (*wpr_check)(struct nvkm_acr *, u64 *start, u64 *limit);
	int (*init)(struct nvkm_acr *);
	void (*fini)(struct nvkm_acr *);
	u64 bootstrap_falcons;
};

extern const struct nvkm_acr_func gm200_acr;
int gm200_acr_wpr_parse(struct nvkm_acr *);
u32 gm200_acr_wpr_layout(struct nvkm_acr *);
int gm200_acr_wpr_build(struct nvkm_acr *, struct nvkm_acr_lsf *);
void gm200_acr_wpr_patch(struct nvkm_acr *, s64);
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
void gp102_acr_wpr_patch(struct nvkm_acr *, s64);

struct nvkm_acr_hsfw {
	const struct nvkm_acr_hsf_func *func;
	const char *name;
	struct list_head head;

	u32 imem_size;
	u32 imem_tag;
	u32 *imem;

	u8 *image;
	u32 image_size;
	u32 non_sec_addr;
	u32 non_sec_size;
	u32 sec_addr;
	u32 sec_size;
	u32 data_addr;
	u32 data_size;

	struct {
		struct {
			void *data;
			u32 size;
		} prod, dbg;
		u32 patch_loc;
	} sig;
};

struct nvkm_acr_hsf_fwif {
	int version;
	int (*load)(struct nvkm_acr *, const char *bl, const char *fw,
		    const char *name, int version,
		    const struct nvkm_acr_hsf_fwif *);
	const struct nvkm_acr_hsf_func *func;
};

int nvkm_acr_hsfw_load(struct nvkm_acr *, const char *, const char *,
		       const char *, int, const struct nvkm_acr_hsf_fwif *);
void nvkm_acr_hsfw_del_all(struct nvkm_acr *);

struct nvkm_acr_hsf {
	const struct nvkm_acr_hsf_func *func;
	const char *name;
	struct list_head head;

	u32 imem_size;
	u32 imem_tag;
	u32 *imem;

	u32 non_sec_addr;
	u32 non_sec_size;
	u32 sec_addr;
	u32 sec_size;
	u32 data_addr;
	u32 data_size;

	struct nvkm_memory *ucode;
	struct nvkm_vma *vma;
	struct nvkm_falcon *falcon;
};

struct nvkm_acr_hsf_func {
	int (*load)(struct nvkm_acr *, struct nvkm_acr_hsfw *);
	int (*boot)(struct nvkm_acr *, struct nvkm_acr_hsf *);
	void (*bld)(struct nvkm_acr *, struct nvkm_acr_hsf *);
};

int gm200_acr_hsfw_load(struct nvkm_acr *, struct nvkm_acr_hsfw *,
			struct nvkm_falcon *);
int gm200_acr_hsfw_boot(struct nvkm_acr *, struct nvkm_acr_hsf *,
			u32 clear_intr, u32 mbox0_ok);

int gm200_acr_load_boot(struct nvkm_acr *, struct nvkm_acr_hsf *);

extern const struct nvkm_acr_hsf_func gm200_acr_unload_0;
int gm200_acr_unload_load(struct nvkm_acr *, struct nvkm_acr_hsfw *);
int gm200_acr_unload_boot(struct nvkm_acr *, struct nvkm_acr_hsf *);
void gm200_acr_hsfw_bld(struct nvkm_acr *, struct nvkm_acr_hsf *);

extern const struct nvkm_acr_hsf_func gm20b_acr_load_0;

int gp102_acr_load_load(struct nvkm_acr *, struct nvkm_acr_hsfw *);

extern const struct nvkm_acr_hsf_func gp108_acr_unload_0;
void gp108_acr_hsfw_bld(struct nvkm_acr *, struct nvkm_acr_hsf *);

int nvkm_acr_new_(const struct nvkm_acr_fwif *, struct nvkm_device *, int,
		  struct nvkm_acr **);
int nvkm_acr_hsf_boot(struct nvkm_acr *, const char *name);

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
