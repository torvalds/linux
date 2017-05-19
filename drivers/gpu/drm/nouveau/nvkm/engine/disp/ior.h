#ifndef __NVKM_DISP_IOR_H__
#define __NVKM_DISP_IOR_H__
#include "priv.h"
struct nvkm_i2c_aux;

struct nvkm_ior {
	const struct nvkm_ior_func *func;
	struct nvkm_disp *disp;
	enum nvkm_ior_type {
		DAC,
		SOR,
		PIOR,
	} type;
	int id;
	char name[8];

	struct list_head head;

	struct nvkm_ior_state {
		unsigned rgdiv;
		unsigned proto_evo:4;
		enum nvkm_ior_proto {
			CRT,
			TMDS,
			LVDS,
			DP,
			UNKNOWN
		} proto:3;
		unsigned link:2;
		unsigned head:4;
	} arm, asy;

	/* Armed DP state. */
	struct {
		bool mst;
		bool ef;
		u8 nr;
		u8 bw;
	} dp;
};

struct nvkm_ior_func {
	void (*state)(struct nvkm_ior *, struct nvkm_ior_state *);
	void (*power)(struct nvkm_ior *, bool normal, bool pu,
		      bool data, bool vsync, bool hsync);
	int (*sense)(struct nvkm_ior *, u32 loadval);

	struct {
		void (*ctrl)(struct nvkm_ior *, int head, bool enable,
			     u8 max_ac_packet, u8 rekey, u8 *avi, u8 avi_size,
			     u8 *vendor, u8 vendor_size);
	} hdmi;

	struct {
		u8 lanes[4];
		int (*links)(struct nvkm_ior *, struct nvkm_i2c_aux *);
		void (*power)(struct nvkm_ior *, int nr);
		void (*pattern)(struct nvkm_ior *, int pattern);
		void (*drive)(struct nvkm_ior *, int ln, int pc,
			      int dc, int pe, int tx_pu);
	} dp;
};

int nvkm_ior_new_(const struct nvkm_ior_func *func, struct nvkm_disp *,
		  enum nvkm_ior_type type, int id);
void nvkm_ior_del(struct nvkm_ior **);
struct nvkm_ior *nvkm_ior_find(struct nvkm_disp *, enum nvkm_ior_type, int id);

static inline u32
nv50_ior_base(struct nvkm_ior *ior)
{
	return ior->id * 0x800;
}

void nv50_dac_power(struct nvkm_ior *, bool, bool, bool, bool, bool);
int nv50_dac_sense(struct nvkm_ior *, u32);

static inline u32
nv50_sor_link(struct nvkm_ior *ior)
{
	return nv50_ior_base(ior) + ((ior->asy.link == 2) * 0x80);
}

void nv50_sor_state(struct nvkm_ior *, struct nvkm_ior_state *);
void nv50_sor_power(struct nvkm_ior *, bool, bool, bool, bool, bool);

void g94_sor_state(struct nvkm_ior *, struct nvkm_ior_state *);
int g94_sor_dp_links(struct nvkm_ior *, struct nvkm_i2c_aux *);
void g94_sor_dp_power(struct nvkm_ior *, int);
void g94_sor_dp_pattern(struct nvkm_ior *, int);
void g94_sor_dp_drive(struct nvkm_ior *, int, int, int, int, int);

void gf119_sor_state(struct nvkm_ior *, struct nvkm_ior_state *);
int gf119_sor_dp_links(struct nvkm_ior *, struct nvkm_i2c_aux *);
void gf119_sor_dp_pattern(struct nvkm_ior *, int);
void gf119_sor_dp_drive(struct nvkm_ior *, int, int, int, int, int);

void gm107_sor_dp_pattern(struct nvkm_ior *, int);

void g84_hdmi_ctrl(struct nvkm_ior *, int, bool, u8, u8, u8 *, u8 , u8 *, u8);
void gt215_hdmi_ctrl(struct nvkm_ior *, int, bool, u8, u8, u8 *, u8 , u8 *, u8);
void gf119_hdmi_ctrl(struct nvkm_ior *, int, bool, u8, u8, u8 *, u8 , u8 *, u8);
void gk104_hdmi_ctrl(struct nvkm_ior *, int, bool, u8, u8, u8 *, u8 , u8 *, u8);

#define IOR_MSG(i,l,f,a...) do {                                               \
	struct nvkm_ior *_ior = (i);                                           \
	nvkm_##l(&_ior->disp->engine.subdev, "%s: "f, _ior->name, ##a);        \
} while(0)
#define IOR_WARN(i,f,a...) IOR_MSG((i), warn, f, ##a)
#define IOR_DBG(i,f,a...) IOR_MSG((i), debug, f, ##a)

int nv50_dac_new(struct nvkm_disp *, int);
int gf119_dac_new(struct nvkm_disp *, int);

int nv50_pior_new(struct nvkm_disp *, int);

int nv50_sor_new(struct nvkm_disp *, int);
int g84_sor_new(struct nvkm_disp *, int);
int g94_sor_new(struct nvkm_disp *, int);
int mcp77_sor_new(struct nvkm_disp *, int);
int gt215_sor_new(struct nvkm_disp *, int);
int mcp89_sor_new(struct nvkm_disp *, int);
int gf119_sor_new(struct nvkm_disp *, int);
int gk104_sor_new(struct nvkm_disp *, int);
int gm107_sor_new(struct nvkm_disp *, int);
int gm200_sor_new(struct nvkm_disp *, int);
#endif
