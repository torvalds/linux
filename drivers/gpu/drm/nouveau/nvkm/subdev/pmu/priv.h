/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_PMU_PRIV_H__
#define __NVKM_PMU_PRIV_H__
#define nvkm_pmu(p) container_of((p), struct nvkm_pmu, subdev)
#include <subdev/pmu.h>
#include <subdev/pmu/fuc/os.h>
enum nvkm_acr_lsf_id;
struct nvkm_acr_lsfw;

struct nvkm_pmu_func {
	const struct nvkm_falcon_func *flcn;

	struct {
		u32 *data;
		u32  size;
	} code;

	struct {
		u32 *data;
		u32  size;
	} data;

	bool (*enabled)(struct nvkm_pmu *);
	void (*reset)(struct nvkm_pmu *);
	int (*init)(struct nvkm_pmu *);
	void (*fini)(struct nvkm_pmu *);
	void (*intr)(struct nvkm_pmu *);
	int (*send)(struct nvkm_pmu *, u32 reply[2], u32 process,
		    u32 message, u32 data0, u32 data1);
	void (*recv)(struct nvkm_pmu *);
	int (*initmsg)(struct nvkm_pmu *);
	void (*pgob)(struct nvkm_pmu *, bool);
};

extern const struct nvkm_falcon_func gt215_pmu_flcn;
int gt215_pmu_init(struct nvkm_pmu *);
void gt215_pmu_fini(struct nvkm_pmu *);
void gt215_pmu_intr(struct nvkm_pmu *);
void gt215_pmu_recv(struct nvkm_pmu *);
int gt215_pmu_send(struct nvkm_pmu *, u32[2], u32, u32, u32, u32);

bool gf100_pmu_enabled(struct nvkm_pmu *);
void gf100_pmu_reset(struct nvkm_pmu *);
void gp102_pmu_reset(struct nvkm_pmu *pmu);

void gk110_pmu_pgob(struct nvkm_pmu *, bool);

extern const struct nvkm_falcon_func gm200_pmu_flcn;

void gm20b_pmu_acr_bld_patch(struct nvkm_acr *, u32, s64);
void gm20b_pmu_acr_bld_write(struct nvkm_acr *, u32, struct nvkm_acr_lsfw *);
int gm20b_pmu_acr_boot(struct nvkm_falcon *);
int gm20b_pmu_acr_bootstrap_falcon(struct nvkm_falcon *, enum nvkm_acr_lsf_id);
void gm20b_pmu_recv(struct nvkm_pmu *);
int gm20b_pmu_initmsg(struct nvkm_pmu *);

struct nvkm_pmu_fwif {
	int version;
	int (*load)(struct nvkm_pmu *, int ver, const struct nvkm_pmu_fwif *);
	const struct nvkm_pmu_func *func;
	const struct nvkm_acr_lsf_func *acr;
};

int gf100_pmu_nofw(struct nvkm_pmu *, int, const struct nvkm_pmu_fwif *);
int gm200_pmu_nofw(struct nvkm_pmu *, int, const struct nvkm_pmu_fwif *);
int gm20b_pmu_load(struct nvkm_pmu *, int, const struct nvkm_pmu_fwif *);

int nvkm_pmu_ctor(const struct nvkm_pmu_fwif *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  struct nvkm_pmu *);
int nvkm_pmu_new_(const struct nvkm_pmu_fwif *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  struct nvkm_pmu **);
#endif
