/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_OUTP_H__
#define __NVIF_OUTP_H__
#include <nvif/object.h>
#include <nvif/if0012.h>
struct nvif_disp;

struct nvif_outp {
	struct nvif_object object;

	struct {
		int id;
		int link;
	} or;
};

int nvif_outp_ctor(struct nvif_disp *, const char *name, int id, struct nvif_outp *);
void nvif_outp_dtor(struct nvif_outp *);
int nvif_outp_load_detect(struct nvif_outp *, u32 loadval);
int nvif_outp_acquire_rgb_crt(struct nvif_outp *);
int nvif_outp_acquire_tmds(struct nvif_outp *, int head,
			   bool hdmi, u8 max_ac_packet, u8 rekey, u8 scdc, bool hda);
int nvif_outp_acquire_lvds(struct nvif_outp *, bool dual, bool bpc8);
int nvif_outp_acquire_dp(struct nvif_outp *, u8 dpcd[16],
			 int link_nr, int link_bw, bool hda, bool mst);
void nvif_outp_release(struct nvif_outp *);
int nvif_outp_infoframe(struct nvif_outp *, u8 type, struct nvif_outp_infoframe_v0 *, u32 size);
int nvif_outp_hda_eld(struct nvif_outp *, int head, void *data, u32 size);
int nvif_outp_dp_aux_pwr(struct nvif_outp *, bool enable);
int nvif_outp_dp_retrain(struct nvif_outp *);
int nvif_outp_dp_mst_vcpi(struct nvif_outp *, int head,
			  u8 start_slot, u8 num_slots, u16 pbn, u16 aligned_pbn);
#endif
