/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_OUTP_H__
#define __NVIF_OUTP_H__
#include <nvif/object.h>
#include <nvif/if0012.h>
#include <drm/display/drm_dp.h>
struct nvif_disp;

struct nvif_outp {
	struct nvif_object object;
	u32 id;

	struct {
		enum {
			NVIF_OUTP_DAC,
			NVIF_OUTP_SOR,
			NVIF_OUTP_PIOR,
		} type;

		enum {
			NVIF_OUTP_RGB_CRT,
			NVIF_OUTP_TMDS,
			NVIF_OUTP_LVDS,
			NVIF_OUTP_DP,
		} proto;

		u8 heads;
#define NVIF_OUTP_DDC_INVALID 0xff
		u8 ddc;
		u8 conn;

		union {
			struct {
				u32 freq_max;
			} rgb_crt;
			struct {
				bool dual;
			} tmds;
			struct {
				bool acpi_edid;
			} lvds;
			struct {
				u8   aux;
				bool mst;
				bool increased_wm;
				u8   link_nr;
				u32  link_bw;
			} dp;
		};
	} info;

	struct {
		int id;
		int link;
	} or;
};

int nvif_outp_ctor(struct nvif_disp *, const char *name, int id, struct nvif_outp *);
void nvif_outp_dtor(struct nvif_outp *);

enum nvif_outp_detect_status {
	NOT_PRESENT,
	PRESENT,
	UNKNOWN,
};

enum nvif_outp_detect_status nvif_outp_detect(struct nvif_outp *);
int nvif_outp_edid_get(struct nvif_outp *, u8 **pedid);

int nvif_outp_load_detect(struct nvif_outp *, u32 loadval);
int nvif_outp_acquire_dac(struct nvif_outp *);
int nvif_outp_acquire_sor(struct nvif_outp *, bool hda);
int nvif_outp_acquire_pior(struct nvif_outp *);
int nvif_outp_inherit_rgb_crt(struct nvif_outp *outp, u8 *proto_out);
int nvif_outp_inherit_lvds(struct nvif_outp *outp, u8 *proto_out);
int nvif_outp_inherit_tmds(struct nvif_outp *outp, u8 *proto_out);
int nvif_outp_inherit_dp(struct nvif_outp *outp, u8 *proto_out);

void nvif_outp_release(struct nvif_outp *);

static inline bool
nvif_outp_acquired(struct nvif_outp *outp)
{
	return outp->or.id >= 0;
}

int nvif_outp_bl_get(struct nvif_outp *);
int nvif_outp_bl_set(struct nvif_outp *, int level);

int nvif_outp_lvds(struct nvif_outp *, bool dual, bool bpc8);

int nvif_outp_hdmi(struct nvif_outp *, int head, bool enable, u8 max_ac_packet, u8 rekey, u32 khz,
		   bool scdc, bool scdc_scrambling, bool scdc_low_rates);

int nvif_outp_infoframe(struct nvif_outp *, u8 type, struct nvif_outp_infoframe_v0 *, u32 size);
int nvif_outp_hda_eld(struct nvif_outp *, int head, void *data, u32 size);

int nvif_outp_dp_aux_pwr(struct nvif_outp *, bool enable);
int nvif_outp_dp_aux_xfer(struct nvif_outp *, u8 type, u8 *size, u32 addr, u8 *data);

struct nvif_outp_dp_rate {
	int dpcd; /* -1 for non-indexed rates */
	u32 rate;
};

int nvif_outp_dp_rates(struct nvif_outp *, struct nvif_outp_dp_rate *rate, int rate_nr);
int nvif_outp_dp_train(struct nvif_outp *, u8 dpcd[DP_RECEIVER_CAP_SIZE],
		       u8 lttprs, u8 link_nr, u32 link_bw, bool mst, bool post_lt_adj,
		       bool retrain);
int nvif_outp_dp_drive(struct nvif_outp *, u8 link_nr, u8 pe[4], u8 vs[4]);
int nvif_outp_dp_sst(struct nvif_outp *, int head, u32 watermark, u32 hblanksym, u32 vblanksym);
int nvif_outp_dp_mst_id_get(struct nvif_outp *, u32 *id);
int nvif_outp_dp_mst_id_put(struct nvif_outp *, u32 id);
int nvif_outp_dp_mst_vcpi(struct nvif_outp *, int head,
			  u8 start_slot, u8 num_slots, u16 pbn, u16 aligned_pbn);
#endif
