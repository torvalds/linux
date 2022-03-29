/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RVE_REG_H__
#define __RVE_REG_H__

#include "rve_drv.h"

/* sys reg */
#define RVE_SWREG0_IVE_VERSION            0x000
#define RVE_SWREG1_IVE_IRQ                0x004
#define RVE_SWREG2_IRQ_CTRL               0x008
#define RVE_SWREG3_IVE_IDLE_PRC_STA       0x00c
#define RVE_SWREG4_IVE_FORCE_IDLE_WBASE   0x010
#define RVE_SWREG5_IVE_IDLE_CTRL          0x014
#define RVE_SWREG6_IVE_WORK_STA           0x018
#define RVE_SWREG7_IVE_SWAP               0x01c

/* llp reg */
#define RVE_SWLTB0_START_BASE             0x100
#define RVE_SWLTB1_CTRL                   0x104
#define RVE_SWLTB2_CFG_DONE               0x108
#define RVE_SWLTB3_ENABLE                 0x10c
#define RVE_SWLTB4_PAUSE_CTRL             0x110
#define RVE_SWLTB5_DECODED_NUM            0x114
#define RVE_SWLTB6_SKIP_NUM               0x118
#define RVE_SWLTB7_TOTAL_NUM              0x11c
#define RVE_SWLTB8_LAST_FRAME_BASE        0x120
#define RVE_SWLTB9_LAST_IDX               0x124

/* op reg */
#define RVE_SWCFG0_EN                     0x200
#define RVE_SWCFG4_OPERATOR               0x210
#define RVE_SWCFG5_CTRL                   0x214
#define RVE_SWCFG6_TIMEOUT_THRESH         0x218
#define RVE_SWCFG7_DDR_CTRL               0x21c
#define RVE_SWCFG9_PIC_INFO               0x224
#define RVE_SWCFG10_HOR_STRIDE0           0x228
#define RVE_SWCFG11_HOR_STRIDE1           0x22c
#define RVE_SWCFG12_SRC0_BASE             0x230
#define RVE_SWCFG13_SRC1_BASE             0x234
#define RVE_SWCFG14_SRC2_BASE             0x238
#define RVE_SWCFG15_SRC3_BASE             0x23c
#define RVE_SWCFG16_DST0_BASE             0x240
#define RVE_SWCFG17_DST1_BASE             0x244
#define RVE_SWCFG18_DST2_BASE             0x248
#define RVE_SWCFG20_OP_CTRL0              0x250
#define RVE_SWCFG21_OP_CTRL1              0x254
#define RVE_SWCFG22_OP_CTRL2              0x258
#define RVE_SWCFG23_OP_CTRL3              0x25c
#define RVE_SWCFG24_OP_CTRL4              0x260
#define RVE_SWCFG25_OP_CTRL5              0x264
#define RVE_SWCFG26_OP_CTRL6              0x268
#define RVE_SWCFG27_OP_CTRL7              0x26c
#define RVE_SWCFG28_OP_CTRL8              0x270
#define RVE_SWCFG29_OP_CTRL9              0x274

/* monitor reg */
#define RVE_SWCFG32_MONITOR_CTRL0         0x280
#define RVE_SWCFG33_MONITOR_CTRL1         0x284
#define RVE_SWCFG34_MONITOR_INFO0         0x288
#define RVE_SWCFG35_MONITOR_INFO1         0x28c
#define RVE_SWCFG36_MONITOR_INFO2         0x290
#define RVE_SWCFG37_MONITOR_INFO3         0x294
#define RVE_SWCFG38_MONITOR_INFO4         0x298
#define RVE_SWCFG39_MONITOR_INFO5         0x29c

/* mmu reg */

/* common reg */
#define RVE_SYS_REG                       0x000
#define RVE_LTB_REG                       0x100
#define RVE_CFG_REG                       0x200
#define RVE_MMU_REG                       0x300

/* mode value */
#define RVE_LLP_MODE                      0x8000
#define RVE_LLP_DONE                      0x11
#define RVE_CLEAR_UP_REG6_WROK_STA        0xff0000

void rve_soft_reset(struct rve_scheduler_t *scheduler);
int rve_set_reg(struct rve_job *job, struct rve_scheduler_t *scheduler);
int rve_init_reg(struct rve_job *job);
int rve_get_version(struct rve_scheduler_t *scheduler);

void rve_dump_read_back_reg(struct rve_scheduler_t *scheduler);
void rve_get_monitor_info(struct rve_job *job);

#endif

