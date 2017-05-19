#ifndef __NVKM_DISP_DP_H__
#define __NVKM_DISP_DP_H__
#define nvkm_dp(p) container_of((p), struct nvkm_dp, outp)
#include "outp.h"

#include <core/notify.h>
#include <subdev/bios.h>
#include <subdev/bios/dp.h>

struct nvkm_dp {
	union {
		struct nvkm_outp base;
		struct nvkm_outp outp;
	};

	struct nvbios_dpout info;
	u8 version;

	struct nvkm_i2c_aux *aux;

	struct nvkm_notify hpd;
	bool present;
	u8 dpcd[16];

	struct mutex mutex;
	struct {
		atomic_t done;
		bool mst;
	} lt;
};

int nvkm_dp_new(struct nvkm_disp *, int index, struct dcb_output *,
		struct nvkm_outp **);

/* DPCD Receiver Capabilities */
#define DPCD_RC00_DPCD_REV                                              0x00000
#define DPCD_RC01_MAX_LINK_RATE                                         0x00001
#define DPCD_RC02                                                       0x00002
#define DPCD_RC02_ENHANCED_FRAME_CAP                                       0x80
#define DPCD_RC02_TPS3_SUPPORTED                                           0x40
#define DPCD_RC02_MAX_LANE_COUNT                                           0x1f
#define DPCD_RC03                                                       0x00003
#define DPCD_RC03_MAX_DOWNSPREAD                                           0x01
#define DPCD_RC0E_AUX_RD_INTERVAL                                       0x0000e

/* DPCD Link Configuration */
#define DPCD_LC00_LINK_BW_SET                                           0x00100
#define DPCD_LC01                                                       0x00101
#define DPCD_LC01_ENHANCED_FRAME_EN                                        0x80
#define DPCD_LC01_LANE_COUNT_SET                                           0x1f
#define DPCD_LC02                                                       0x00102
#define DPCD_LC02_TRAINING_PATTERN_SET                                     0x03
#define DPCD_LC03(l)                                            ((l) +  0x00103)
#define DPCD_LC03_MAX_PRE_EMPHASIS_REACHED                                 0x20
#define DPCD_LC03_PRE_EMPHASIS_SET                                         0x18
#define DPCD_LC03_MAX_SWING_REACHED                                        0x04
#define DPCD_LC03_VOLTAGE_SWING_SET                                        0x03
#define DPCD_LC0F                                                       0x0010f
#define DPCD_LC0F_LANE1_MAX_POST_CURSOR2_REACHED                           0x40
#define DPCD_LC0F_LANE1_POST_CURSOR2_SET                                   0x30
#define DPCD_LC0F_LANE0_MAX_POST_CURSOR2_REACHED                           0x04
#define DPCD_LC0F_LANE0_POST_CURSOR2_SET                                   0x03
#define DPCD_LC10                                                       0x00110
#define DPCD_LC10_LANE3_MAX_POST_CURSOR2_REACHED                           0x40
#define DPCD_LC10_LANE3_POST_CURSOR2_SET                                   0x30
#define DPCD_LC10_LANE2_MAX_POST_CURSOR2_REACHED                           0x04
#define DPCD_LC10_LANE2_POST_CURSOR2_SET                                   0x03

/* DPCD Link/Sink Status */
#define DPCD_LS02                                                       0x00202
#define DPCD_LS02_LANE1_SYMBOL_LOCKED                                      0x40
#define DPCD_LS02_LANE1_CHANNEL_EQ_DONE                                    0x20
#define DPCD_LS02_LANE1_CR_DONE                                            0x10
#define DPCD_LS02_LANE0_SYMBOL_LOCKED                                      0x04
#define DPCD_LS02_LANE0_CHANNEL_EQ_DONE                                    0x02
#define DPCD_LS02_LANE0_CR_DONE                                            0x01
#define DPCD_LS03                                                       0x00203
#define DPCD_LS03_LANE3_SYMBOL_LOCKED                                      0x40
#define DPCD_LS03_LANE3_CHANNEL_EQ_DONE                                    0x20
#define DPCD_LS03_LANE3_CR_DONE                                            0x10
#define DPCD_LS03_LANE2_SYMBOL_LOCKED                                      0x04
#define DPCD_LS03_LANE2_CHANNEL_EQ_DONE                                    0x02
#define DPCD_LS03_LANE2_CR_DONE                                            0x01
#define DPCD_LS04                                                       0x00204
#define DPCD_LS04_LINK_STATUS_UPDATED                                      0x80
#define DPCD_LS04_DOWNSTREAM_PORT_STATUS_CHANGED                           0x40
#define DPCD_LS04_INTERLANE_ALIGN_DONE                                     0x01
#define DPCD_LS06                                                       0x00206
#define DPCD_LS06_LANE1_PRE_EMPHASIS                                       0xc0
#define DPCD_LS06_LANE1_VOLTAGE_SWING                                      0x30
#define DPCD_LS06_LANE0_PRE_EMPHASIS                                       0x0c
#define DPCD_LS06_LANE0_VOLTAGE_SWING                                      0x03
#define DPCD_LS07                                                       0x00207
#define DPCD_LS07_LANE3_PRE_EMPHASIS                                       0xc0
#define DPCD_LS07_LANE3_VOLTAGE_SWING                                      0x30
#define DPCD_LS07_LANE2_PRE_EMPHASIS                                       0x0c
#define DPCD_LS07_LANE2_VOLTAGE_SWING                                      0x03
#define DPCD_LS0C                                                       0x0020c
#define DPCD_LS0C_LANE3_POST_CURSOR2                                       0xc0
#define DPCD_LS0C_LANE2_POST_CURSOR2                                       0x30
#define DPCD_LS0C_LANE1_POST_CURSOR2                                       0x0c
#define DPCD_LS0C_LANE0_POST_CURSOR2                                       0x03

/* DPCD Sink Control */
#define DPCD_SC00                                                       0x00600
#define DPCD_SC00_SET_POWER                                                0x03
#define DPCD_SC00_SET_POWER_D0                                             0x01
#define DPCD_SC00_SET_POWER_D3                                             0x03
#endif
