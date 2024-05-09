/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DISP_OUTP_H__
#define __NVKM_DISP_OUTP_H__
#include "priv.h"

#include <drm/display/drm_dp.h>
#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/dp.h>

struct nvkm_outp {
	const struct nvkm_outp_func *func;
	struct nvkm_disp *disp;
	int index;
	struct dcb_output info;

	struct nvkm_i2c_bus *i2c;

	struct list_head head;
	struct nvkm_conn *conn;
	bool identity;

	/* Assembly state. */
#define NVKM_OUTP_PRIV 1
#define NVKM_OUTP_USER 2
	u8 acquired:2;
	struct nvkm_ior *ior;

	union {
		struct {
			bool dual;
			bool bpc8;
		} lvds;

		struct {
			struct nvbios_dpout info;
			u8 version;
			bool mst;
			bool increased_wm;

			struct nvkm_i2c_aux *aux;

			bool enabled;
			bool aux_pwr;
			bool aux_pwr_pu;
			u8 lttpr[6];
			u8 lttprs;
			u8 dpcd[DP_RECEIVER_CAP_SIZE];

			struct {
				int dpcd; /* -1, or index into SUPPORTED_LINK_RATES table */
				u32 rate;
			} rate[8];
			int rates;

			struct mutex mutex;
			struct {
				u8 nr;
				u8 bw;
				bool mst;
				bool post_adj;
			} lt;
		} dp;
	};

	struct nvkm_object object;
	struct {
		struct nvkm_head *head;
	} asy;
};

int nvkm_outp_new_(const struct nvkm_outp_func *, struct nvkm_disp *, int index,
		   struct dcb_output *, struct nvkm_outp **);
int nvkm_outp_new(struct nvkm_disp *, int index, struct dcb_output *, struct nvkm_outp **);
void nvkm_outp_del(struct nvkm_outp **);
void nvkm_outp_init(struct nvkm_outp *);
void nvkm_outp_fini(struct nvkm_outp *);

int nvkm_outp_detect(struct nvkm_outp *);

struct nvkm_ior *nvkm_outp_inherit(struct nvkm_outp *);
int nvkm_outp_acquire(struct nvkm_outp *, bool hda);
int nvkm_outp_acquire_or(struct nvkm_outp *, u8 user, bool hda);
int nvkm_outp_acquire_ior(struct nvkm_outp *, u8 user, struct nvkm_ior *);
void nvkm_outp_release(struct nvkm_outp *);
void nvkm_outp_release_or(struct nvkm_outp *, u8 user);

int nvkm_outp_bl_get(struct nvkm_outp *);
int nvkm_outp_bl_set(struct nvkm_outp *, int level);

struct nvkm_outp_func {
	void *(*dtor)(struct nvkm_outp *);
	void (*init)(struct nvkm_outp *);
	void (*fini)(struct nvkm_outp *);

	int (*detect)(struct nvkm_outp *);
	int (*edid_get)(struct nvkm_outp *, u8 *data, u16 *size);

	struct nvkm_ior *(*inherit)(struct nvkm_outp *);
	int (*acquire)(struct nvkm_outp *, bool hda);
	void (*release)(struct nvkm_outp *);

	struct {
		int (*get)(struct nvkm_outp *);
		int (*set)(struct nvkm_outp *, int level);
	} bl;

	struct {
		int (*aux_pwr)(struct nvkm_outp *, bool pu);
		int (*aux_xfer)(struct nvkm_outp *, u8 type, u32 addr, u8 *data, u8 *size);
		int (*rates)(struct nvkm_outp *);
		int (*train)(struct nvkm_outp *, bool retrain);
		int (*drive)(struct nvkm_outp *, u8 lanes, u8 pe[4], u8 vs[4]);
		int (*mst_id_get)(struct nvkm_outp *, u32 *id);
		int (*mst_id_put)(struct nvkm_outp *, u32 id);
	} dp;
};

#define OUTP_MSG(o,l,f,a...) do {                                              \
	struct nvkm_outp *_outp = (o);                                         \
	nvkm_##l(&_outp->disp->engine.subdev, "outp %02x:%04x:%04x: "f"\n",    \
		 _outp->index, _outp->info.hasht, _outp->info.hashm, ##a);     \
} while(0)
#define OUTP_ERR(o,f,a...) OUTP_MSG((o), error, f, ##a)
#define OUTP_DBG(o,f,a...) OUTP_MSG((o), debug, f, ##a)
#define OUTP_TRACE(o,f,a...) OUTP_MSG((o), trace, f, ##a)
#endif
