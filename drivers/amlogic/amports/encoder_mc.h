/*
 * AMLOGIC AVC encoder driver.
 *
 * Author:  <simon.zheng@amlogic.com>
 *
 */

#ifndef ENCODER_MC_H
#define ENCODER_MC_H
#include "amports_config.h"

extern const u32 mix_dump_mc[];
extern const u32 half_encoder_mc[];
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
extern const u32 mix_sw_mc[];
extern const u32 mix_sw_mc_hdec_dblk[];
extern const u32 mix_dump_mc_dblk[];
extern const u32 mix_sw_mc_vdec2_dblk[];
extern const u32 vdec2_encoder_mc[];
extern const u32 mix_sw_mc_hdec_m2_dblk[];
extern const u32 mix_dump_mc_m2_dblk[];
#endif
#endif /* ENCODER_MC_H */
