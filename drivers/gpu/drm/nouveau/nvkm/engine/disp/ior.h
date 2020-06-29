/* SPDX-License-Identifier: MIT */
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
	bool identity;

	struct nvkm_ior_state {
		struct nvkm_outp *outp;
		unsigned rgdiv;
		unsigned proto_evo:4;
		enum nvkm_ior_proto {
			CRT,
			TV,
			TMDS,
			LVDS,
			DP,
			UNKNOWN
		} proto:3;
		unsigned link:2;
		unsigned head:8;
	} arm, asy;

	/* Armed DP state. */
	struct {
		bool mst;
		bool ef;
		u8 nr;
		u8 bw;
	} dp;

	/* Armed TMDS state. */
	struct {
		bool high_speed;
	} tmds;
};

struct nvkm_ior_func {
	struct {
		int (*get)(struct nvkm_outp *, int *link);
		void (*set)(struct nvkm_outp *, struct nvkm_ior *);
	} route;

	void (*state)(struct nvkm_ior *, struct nvkm_ior_state *);
	void (*power)(struct nvkm_ior *, bool normal, bool pu,
		      bool data, bool vsync, bool hsync);
	int (*sense)(struct nvkm_ior *, u32 loadval);
	void (*clock)(struct nvkm_ior *);
	void (*war_2)(struct nvkm_ior *);
	void (*war_3)(struct nvkm_ior *);

	struct {
		void (*ctrl)(struct nvkm_ior *, int head, bool enable,
			     u8 max_ac_packet, u8 rekey, u8 *avi, u8 avi_size,
			     u8 *vendor, u8 vendor_size);
		void (*scdc)(struct nvkm_ior *, int head, u8 scdc);
	} hdmi;

	struct {
		u8 lanes[4];
		int (*links)(struct nvkm_ior *, struct nvkm_i2c_aux *);
		void (*power)(struct nvkm_ior *, int nr);
		void (*pattern)(struct nvkm_ior *, int pattern);
		void (*drive)(struct nvkm_ior *, int ln, int pc,
			      int dc, int pe, int tx_pu);
		void (*vcpi)(struct nvkm_ior *, int head, u8 slot,
			     u8 slot_nr, u16 pbn, u16 aligned);
		void (*audio)(struct nvkm_ior *, int head, bool enable);
		void (*audio_sym)(struct nvkm_ior *, int head, u16 h, u32 v);
		void (*activesym)(struct nvkm_ior *, int head,
				  u8 TU, u8 VTUa, u8 VTUf, u8 VTUi);
		void (*watermark)(struct nvkm_ior *, int head, u8 watermark);
	} dp;

	struct {
		void (*hpd)(struct nvkm_ior *, int head, bool present);
		void (*eld)(struct nvkm_ior *, int head, u8 *data, u8 size);
		void (*device_entry)(struct nvkm_ior *, int head);
	} hda;
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

void nv50_pior_depth(struct nvkm_ior *, struct nvkm_ior_state *, u32 ctrl);

static inline u32
nv50_sor_link(struct nvkm_ior *ior)
{
	return nv50_ior_base(ior) + ((ior->asy.link == 2) * 0x80);
}

void nv50_sor_state(struct nvkm_ior *, struct nvkm_ior_state *);
void nv50_sor_power(struct nvkm_ior *, bool, bool, bool, bool, bool);
void nv50_sor_clock(struct nvkm_ior *);

void g94_sor_state(struct nvkm_ior *, struct nvkm_ior_state *);
int g94_sor_dp_links(struct nvkm_ior *, struct nvkm_i2c_aux *);
void g94_sor_dp_power(struct nvkm_ior *, int);
void g94_sor_dp_pattern(struct nvkm_ior *, int);
void g94_sor_dp_drive(struct nvkm_ior *, int, int, int, int, int);
void g94_sor_dp_audio_sym(struct nvkm_ior *, int, u16, u32);
void g94_sor_dp_activesym(struct nvkm_ior *, int, u8, u8, u8, u8);
void g94_sor_dp_watermark(struct nvkm_ior *, int, u8);

void gt215_sor_dp_audio(struct nvkm_ior *, int, bool);

void gf119_sor_state(struct nvkm_ior *, struct nvkm_ior_state *);
void gf119_sor_clock(struct nvkm_ior *);
int gf119_sor_dp_links(struct nvkm_ior *, struct nvkm_i2c_aux *);
void gf119_sor_dp_pattern(struct nvkm_ior *, int);
void gf119_sor_dp_drive(struct nvkm_ior *, int, int, int, int, int);
void gf119_sor_dp_vcpi(struct nvkm_ior *, int, u8, u8, u16, u16);
void gf119_sor_dp_audio(struct nvkm_ior *, int, bool);
void gf119_sor_dp_audio_sym(struct nvkm_ior *, int, u16, u32);
void gf119_sor_dp_watermark(struct nvkm_ior *, int, u8);

void gm107_sor_dp_pattern(struct nvkm_ior *, int);

void gm200_sor_route_set(struct nvkm_outp *, struct nvkm_ior *);
int gm200_sor_route_get(struct nvkm_outp *, int *);
void gm200_sor_dp_drive(struct nvkm_ior *, int, int, int, int, int);

void gv100_sor_state(struct nvkm_ior *, struct nvkm_ior_state *);
void gv100_sor_dp_audio(struct nvkm_ior *, int, bool);
void gv100_sor_dp_audio_sym(struct nvkm_ior *, int, u16, u32);
void gv100_sor_dp_watermark(struct nvkm_ior *, int, u8);

void g84_hdmi_ctrl(struct nvkm_ior *, int, bool, u8, u8, u8 *, u8 , u8 *, u8);
void gt215_hdmi_ctrl(struct nvkm_ior *, int, bool, u8, u8, u8 *, u8 , u8 *, u8);
void gf119_hdmi_ctrl(struct nvkm_ior *, int, bool, u8, u8, u8 *, u8 , u8 *, u8);
void gk104_hdmi_ctrl(struct nvkm_ior *, int, bool, u8, u8, u8 *, u8 , u8 *, u8);
void gv100_hdmi_ctrl(struct nvkm_ior *, int, bool, u8, u8, u8 *, u8 , u8 *, u8);

void gm200_hdmi_scdc(struct nvkm_ior *, int, u8);

void gt215_hda_hpd(struct nvkm_ior *, int, bool);
void gt215_hda_eld(struct nvkm_ior *, int, u8 *, u8);

void gf119_hda_hpd(struct nvkm_ior *, int, bool);
void gf119_hda_eld(struct nvkm_ior *, int, u8 *, u8);
void gf119_hda_device_entry(struct nvkm_ior *, int);

void gv100_hda_device_entry(struct nvkm_ior *, int);

#define IOR_MSG(i,l,f,a...) do {                                               \
	struct nvkm_ior *_ior = (i);                                           \
	nvkm_##l(&_ior->disp->engine.subdev, "%s: "f"\n", _ior->name, ##a);    \
} while(0)
#define IOR_WARN(i,f,a...) IOR_MSG((i), warn, f, ##a)
#define IOR_DBG(i,f,a...) IOR_MSG((i), debug, f, ##a)

int nv50_dac_cnt(struct nvkm_disp *, unsigned long *);
int nv50_dac_new(struct nvkm_disp *, int);

int gf119_dac_cnt(struct nvkm_disp *, unsigned long *);
int gf119_dac_new(struct nvkm_disp *, int);

int nv50_pior_cnt(struct nvkm_disp *, unsigned long *);
int nv50_pior_new(struct nvkm_disp *, int);

int nv50_sor_cnt(struct nvkm_disp *, unsigned long *);
int nv50_sor_new(struct nvkm_disp *, int);

int g84_sor_new(struct nvkm_disp *, int);

int g94_sor_cnt(struct nvkm_disp *, unsigned long *);
int g94_sor_new(struct nvkm_disp *, int);

int mcp77_sor_new(struct nvkm_disp *, int);
int gt215_sor_new(struct nvkm_disp *, int);
int mcp89_sor_new(struct nvkm_disp *, int);

int gf119_sor_cnt(struct nvkm_disp *, unsigned long *);
int gf119_sor_new(struct nvkm_disp *, int);

int gk104_sor_new(struct nvkm_disp *, int);
int gm107_sor_new(struct nvkm_disp *, int);
int gm200_sor_new(struct nvkm_disp *, int);
int gp100_sor_new(struct nvkm_disp *, int);

int gv100_sor_cnt(struct nvkm_disp *, unsigned long *);
int gv100_sor_new(struct nvkm_disp *, int);

int tu102_sor_new(struct nvkm_disp *, int);
#endif
