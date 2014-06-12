#ifndef __NOUVEAU_PWR_H__
#define __NOUVEAU_PWR_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_pwr {
	struct nouveau_subdev base;

	struct {
		u32 base;
		u32 size;
	} send;

	struct {
		u32 base;
		u32 size;

		struct work_struct work;
		wait_queue_head_t wait;
		u32 process;
		u32 message;
		u32 data[2];
	} recv;

	int  (*message)(struct nouveau_pwr *, u32[2], u32, u32, u32, u32);
	void (*pgob)(struct nouveau_pwr *, bool);
};

static inline struct nouveau_pwr *
nouveau_pwr(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_PWR];
}

extern struct nouveau_oclass *nva3_pwr_oclass;
extern struct nouveau_oclass *nvc0_pwr_oclass;
extern struct nouveau_oclass *nvd0_pwr_oclass;
extern struct nouveau_oclass *gk104_pwr_oclass;
extern struct nouveau_oclass *nv108_pwr_oclass;

/* interface to MEMX process running on PPWR */
struct nouveau_memx;
int  nouveau_memx_init(struct nouveau_pwr *, struct nouveau_memx **);
int  nouveau_memx_fini(struct nouveau_memx **, bool exec);
void nouveau_memx_wr32(struct nouveau_memx *, u32 addr, u32 data);
void nouveau_memx_wait(struct nouveau_memx *,
		       u32 addr, u32 mask, u32 data, u32 nsec);
void nouveau_memx_nsec(struct nouveau_memx *, u32 nsec);

#endif
