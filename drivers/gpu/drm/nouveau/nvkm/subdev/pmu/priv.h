/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_PMU_PRIV_H__
#define __NVKM_PMU_PRIV_H__
#define nvkm_pmu(p) container_of((p), struct nvkm_pmu, subdev)
#include <subdev/pmu.h>
#include <subdev/pmu/fuc/os.h>

int nvkm_pmu_ctor(const struct nvkm_pmu_func *, struct nvkm_device *,
		  int index, struct nvkm_pmu *);
int nvkm_pmu_new_(const struct nvkm_pmu_func *, struct nvkm_device *,
		  int index, struct nvkm_pmu **);

struct nvkm_pmu_func {
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
	void (*pgob)(struct nvkm_pmu *, bool);
};

int gt215_pmu_init(struct nvkm_pmu *);
void gt215_pmu_fini(struct nvkm_pmu *);
void gt215_pmu_intr(struct nvkm_pmu *);
void gt215_pmu_recv(struct nvkm_pmu *);
int gt215_pmu_send(struct nvkm_pmu *, u32[2], u32, u32, u32, u32);

bool gf100_pmu_enabled(struct nvkm_pmu *);
void gf100_pmu_reset(struct nvkm_pmu *);

void gk110_pmu_pgob(struct nvkm_pmu *, bool);
#endif
