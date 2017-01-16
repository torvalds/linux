#ifndef __NVKM_GR_PRIV_H__
#define __NVKM_GR_PRIV_H__
#define nvkm_gr(p) container_of((p), struct nvkm_gr, engine)
#include <engine/gr.h>
#include <core/enum.h>
struct nvkm_fb_tile;
struct nvkm_fifo_chan;

int nvkm_gr_ctor(const struct nvkm_gr_func *, struct nvkm_device *,
		 int index, bool enable, struct nvkm_gr *);

bool nv04_gr_idle(struct nvkm_gr *);

struct nvkm_gr_func {
	void *(*dtor)(struct nvkm_gr *);
	int (*oneinit)(struct nvkm_gr *);
	int (*init)(struct nvkm_gr *);
	int (*fini)(struct nvkm_gr *, bool);
	void (*intr)(struct nvkm_gr *);
	void (*tile)(struct nvkm_gr *, int region, struct nvkm_fb_tile *);
	int (*tlb_flush)(struct nvkm_gr *);
	int (*chan_new)(struct nvkm_gr *, struct nvkm_fifo_chan *,
			const struct nvkm_oclass *, struct nvkm_object **);
	int (*object_get)(struct nvkm_gr *, int, struct nvkm_sclass *);
	/* Returns chipset-specific counts of units packed into an u64.
	 */
	u64 (*units)(struct nvkm_gr *);
	bool (*chsw_load)(struct nvkm_gr *);
	struct nvkm_sclass sclass[];
};

extern const struct nvkm_bitfield nv04_gr_nsource[];
extern const struct nvkm_object_func nv04_gr_object;

extern const struct nvkm_bitfield nv10_gr_intr_name[];
extern const struct nvkm_bitfield nv10_gr_nstatus[];

extern const struct nvkm_enum nv50_data_error_names[];
#endif
