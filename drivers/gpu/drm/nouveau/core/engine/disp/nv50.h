#ifndef __NV50_DISP_H__
#define __NV50_DISP_H__

#include <core/parent.h>
#include <core/namedb.h>
#include <core/ramht.h>

#include <engine/dmaobj.h>
#include <engine/disp.h>

struct dcb_output;

struct nv50_disp_priv {
	struct nouveau_disp base;
	struct nouveau_oclass *sclass;
	struct {
		int nr;
	} head;
	struct {
		int nr;
		int (*power)(struct nv50_disp_priv *, int dac, u32 data);
		int (*sense)(struct nv50_disp_priv *, int dac, u32 load);
	} dac;
	struct {
		int nr;
		int (*power)(struct nv50_disp_priv *, int sor, u32 data);
		int (*hda_eld)(struct nv50_disp_priv *, int sor, u8 *, u32);
		int (*hdmi)(struct nv50_disp_priv *, int head, int sor, u32);
		int (*dp_train_init)(struct nv50_disp_priv *, int sor, int link,
				     int head, u16 type, u16 mask, u32 data,
				     struct dcb_output *);
		int (*dp_train_fini)(struct nv50_disp_priv *, int sor, int link,
				     int head, u16 type, u16 mask, u32 data,
				     struct dcb_output *);
		int (*dp_train)(struct nv50_disp_priv *, int sor, int link,
				u16 type, u16 mask, u32 data,
				struct dcb_output *);
		int (*dp_lnkctl)(struct nv50_disp_priv *, int sor, int link,
				 int head, u16 type, u16 mask, u32 data,
				 struct dcb_output *);
		int (*dp_drvctl)(struct nv50_disp_priv *, int sor, int link,
				 int lane, u16 type, u16 mask, u32 data,
				 struct dcb_output *);
		u32 lvdsconf;
	} sor;
};

#define DAC_MTHD(n) (n), (n) + 0x03

int nv50_dac_mthd(struct nouveau_object *, u32, void *, u32);
int nv50_dac_power(struct nv50_disp_priv *, int, u32);
int nv50_dac_sense(struct nv50_disp_priv *, int, u32);

#define SOR_MTHD(n) (n), (n) + 0x3f

int nva3_hda_eld(struct nv50_disp_priv *, int, u8 *, u32);
int nvd0_hda_eld(struct nv50_disp_priv *, int, u8 *, u32);

int nv84_hdmi_ctrl(struct nv50_disp_priv *, int, int, u32);
int nva3_hdmi_ctrl(struct nv50_disp_priv *, int, int, u32);
int nvd0_hdmi_ctrl(struct nv50_disp_priv *, int, int, u32);

int nv50_sor_mthd(struct nouveau_object *, u32, void *, u32);
int nv50_sor_power(struct nv50_disp_priv *, int, u32);

int nv94_sor_dp_train_init(struct nv50_disp_priv *, int, int, int, u16, u16,
		           u32, struct dcb_output *);
int nv94_sor_dp_train_fini(struct nv50_disp_priv *, int, int, int, u16, u16,
		           u32, struct dcb_output *);
int nv94_sor_dp_train(struct nv50_disp_priv *, int, int, u16, u16, u32,
		      struct dcb_output *);
int nv94_sor_dp_lnkctl(struct nv50_disp_priv *, int, int, int, u16, u16, u32,
		       struct dcb_output *);
int nv94_sor_dp_drvctl(struct nv50_disp_priv *, int, int, int, u16, u16, u32,
		       struct dcb_output *);

int nvd0_sor_dp_train(struct nv50_disp_priv *, int, int, u16, u16, u32,
		      struct dcb_output *);
int nvd0_sor_dp_lnkctl(struct nv50_disp_priv *, int, int, int, u16, u16, u32,
		       struct dcb_output *);
int nvd0_sor_dp_drvctl(struct nv50_disp_priv *, int, int, int, u16, u16, u32,
		       struct dcb_output *);

struct nv50_disp_base {
	struct nouveau_parent base;
	struct nouveau_ramht *ramht;
	u32 chan;
};

struct nv50_disp_chan {
	struct nouveau_namedb base;
	int chid;
};

int  nv50_disp_chan_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, int, int, void **);
void nv50_disp_chan_destroy(struct nv50_disp_chan *);
u32  nv50_disp_chan_rd32(struct nouveau_object *, u64);
void nv50_disp_chan_wr32(struct nouveau_object *, u64, u32);

#define nv50_disp_chan_init(a)                                                 \
	nouveau_namedb_init(&(a)->base)
#define nv50_disp_chan_fini(a,b)                                               \
	nouveau_namedb_fini(&(a)->base, (b))

int  nv50_disp_dmac_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, u32, int, int, void **);
void nv50_disp_dmac_dtor(struct nouveau_object *);

struct nv50_disp_dmac {
	struct nv50_disp_chan base;
	struct nouveau_dmaobj *pushdma;
	u32 push;
};

struct nv50_disp_pioc {
	struct nv50_disp_chan base;
};

extern struct nouveau_ofuncs nv50_disp_mast_ofuncs;
extern struct nouveau_ofuncs nv50_disp_sync_ofuncs;
extern struct nouveau_ofuncs nv50_disp_ovly_ofuncs;
extern struct nouveau_ofuncs nv50_disp_oimm_ofuncs;
extern struct nouveau_ofuncs nv50_disp_curs_ofuncs;
extern struct nouveau_ofuncs nv50_disp_base_ofuncs;
extern struct nouveau_oclass nv50_disp_cclass;
void nv50_disp_intr(struct nouveau_subdev *);

extern struct nouveau_omthds nv84_disp_base_omthds[];

extern struct nouveau_omthds nva3_disp_base_omthds[];

extern struct nouveau_ofuncs nvd0_disp_mast_ofuncs;
extern struct nouveau_ofuncs nvd0_disp_sync_ofuncs;
extern struct nouveau_ofuncs nvd0_disp_ovly_ofuncs;
extern struct nouveau_ofuncs nvd0_disp_oimm_ofuncs;
extern struct nouveau_ofuncs nvd0_disp_curs_ofuncs;
extern struct nouveau_ofuncs nvd0_disp_base_ofuncs;
extern struct nouveau_oclass nvd0_disp_cclass;
void nvd0_disp_intr(struct nouveau_subdev *);

#endif
