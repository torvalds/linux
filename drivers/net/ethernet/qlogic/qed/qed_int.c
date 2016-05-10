/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_int.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"

struct qed_pi_info {
	qed_int_comp_cb_t	comp_cb;
	void			*cookie;
};

struct qed_sb_sp_info {
	struct qed_sb_info	sb_info;

	/* per protocol index data */
	struct qed_pi_info	pi_info_arr[PIS_PER_SB];
};

enum qed_attention_type {
	QED_ATTN_TYPE_ATTN,
	QED_ATTN_TYPE_PARITY,
};

#define SB_ATTN_ALIGNED_SIZE(p_hwfn) \
	ALIGNED_TYPE_SIZE(struct atten_status_block, p_hwfn)

struct aeu_invert_reg_bit {
	char bit_name[30];

#define ATTENTION_PARITY                (1 << 0)

#define ATTENTION_LENGTH_MASK           (0x00000ff0)
#define ATTENTION_LENGTH_SHIFT          (4)
#define ATTENTION_LENGTH(flags)         (((flags) & ATTENTION_LENGTH_MASK) >> \
					 ATTENTION_LENGTH_SHIFT)
#define ATTENTION_SINGLE                (1 << ATTENTION_LENGTH_SHIFT)
#define ATTENTION_PAR                   (ATTENTION_SINGLE | ATTENTION_PARITY)
#define ATTENTION_PAR_INT               ((2 << ATTENTION_LENGTH_SHIFT) | \
					 ATTENTION_PARITY)

/* Multiple bits start with this offset */
#define ATTENTION_OFFSET_MASK           (0x000ff000)
#define ATTENTION_OFFSET_SHIFT          (12)
	unsigned int flags;

	/* Callback to call if attention will be triggered */
	int (*cb)(struct qed_hwfn *p_hwfn);

	enum block_id block_index;
};

struct aeu_invert_reg {
	struct aeu_invert_reg_bit bits[32];
};

#define MAX_ATTN_GRPS           (8)
#define NUM_ATTN_REGS           (9)

/* HW Attention register */
struct attn_hw_reg {
	u16 reg_idx;             /* Index of this register in its block */
	u16 num_of_bits;         /* number of valid attention bits */
	u32 sts_addr;            /* Address of the STS register */
	u32 sts_clr_addr;        /* Address of the STS_CLR register */
	u32 sts_wr_addr;         /* Address of the STS_WR register */
	u32 mask_addr;           /* Address of the MASK register */
};

/* HW block attention registers */
struct attn_hw_regs {
	u16 num_of_int_regs;            /* Number of interrupt regs */
	u16 num_of_prty_regs;           /* Number of parity regs */
	struct attn_hw_reg **int_regs;  /* interrupt regs */
	struct attn_hw_reg **prty_regs; /* parity regs */
};

/* HW block attention registers */
struct attn_hw_block {
	const char *name;                 /* Block name */
	struct attn_hw_regs chip_regs[1];
};

static struct attn_hw_reg grc_int0_bb_b0 = {
	0, 4, 0x50180, 0x5018c, 0x50188, 0x50184};

static struct attn_hw_reg *grc_int_bb_b0_regs[1] = {
	&grc_int0_bb_b0};

static struct attn_hw_reg grc_prty1_bb_b0 = {
	0, 2, 0x50200, 0x5020c, 0x50208, 0x50204};

static struct attn_hw_reg *grc_prty_bb_b0_regs[1] = {
	&grc_prty1_bb_b0};

static struct attn_hw_reg miscs_int0_bb_b0 = {
	0, 3, 0x9180, 0x918c, 0x9188, 0x9184};

static struct attn_hw_reg miscs_int1_bb_b0 = {
	1, 11, 0x9190, 0x919c, 0x9198, 0x9194};

static struct attn_hw_reg *miscs_int_bb_b0_regs[2] = {
	&miscs_int0_bb_b0, &miscs_int1_bb_b0};

static struct attn_hw_reg miscs_prty0_bb_b0 = {
	0, 1, 0x91a0, 0x91ac, 0x91a8, 0x91a4};

static struct attn_hw_reg *miscs_prty_bb_b0_regs[1] = {
	&miscs_prty0_bb_b0};

static struct attn_hw_reg misc_int0_bb_b0 = {
	0, 1, 0x8180, 0x818c, 0x8188, 0x8184};

static struct attn_hw_reg *misc_int_bb_b0_regs[1] = {
	&misc_int0_bb_b0};

static struct attn_hw_reg pglue_b_int0_bb_b0 = {
	0, 23, 0x2a8180, 0x2a818c, 0x2a8188, 0x2a8184};

static struct attn_hw_reg *pglue_b_int_bb_b0_regs[1] = {
	&pglue_b_int0_bb_b0};

static struct attn_hw_reg pglue_b_prty0_bb_b0 = {
	0, 1, 0x2a8190, 0x2a819c, 0x2a8198, 0x2a8194};

static struct attn_hw_reg pglue_b_prty1_bb_b0 = {
	1, 22, 0x2a8200, 0x2a820c, 0x2a8208, 0x2a8204};

static struct attn_hw_reg *pglue_b_prty_bb_b0_regs[2] = {
	&pglue_b_prty0_bb_b0, &pglue_b_prty1_bb_b0};

static struct attn_hw_reg cnig_int0_bb_b0 = {
	0, 6, 0x2182e8, 0x2182f4, 0x2182f0, 0x2182ec};

static struct attn_hw_reg *cnig_int_bb_b0_regs[1] = {
	&cnig_int0_bb_b0};

static struct attn_hw_reg cnig_prty0_bb_b0 = {
	0, 2, 0x218348, 0x218354, 0x218350, 0x21834c};

static struct attn_hw_reg *cnig_prty_bb_b0_regs[1] = {
	&cnig_prty0_bb_b0};

static struct attn_hw_reg cpmu_int0_bb_b0 = {
	0, 1, 0x303e0, 0x303ec, 0x303e8, 0x303e4};

static struct attn_hw_reg *cpmu_int_bb_b0_regs[1] = {
	&cpmu_int0_bb_b0};

static struct attn_hw_reg ncsi_int0_bb_b0 = {
	0, 1, 0x404cc, 0x404d8, 0x404d4, 0x404d0};

static struct attn_hw_reg *ncsi_int_bb_b0_regs[1] = {
	&ncsi_int0_bb_b0};

static struct attn_hw_reg ncsi_prty1_bb_b0 = {
	0, 1, 0x40000, 0x4000c, 0x40008, 0x40004};

static struct attn_hw_reg *ncsi_prty_bb_b0_regs[1] = {
	&ncsi_prty1_bb_b0};

static struct attn_hw_reg opte_prty1_bb_b0 = {
	0, 11, 0x53000, 0x5300c, 0x53008, 0x53004};

static struct attn_hw_reg opte_prty0_bb_b0 = {
	1, 1, 0x53208, 0x53214, 0x53210, 0x5320c};

static struct attn_hw_reg *opte_prty_bb_b0_regs[2] = {
	&opte_prty1_bb_b0, &opte_prty0_bb_b0};

static struct attn_hw_reg bmb_int0_bb_b0 = {
	0, 16, 0x5400c0, 0x5400cc, 0x5400c8, 0x5400c4};

static struct attn_hw_reg bmb_int1_bb_b0 = {
	1, 28, 0x5400d8, 0x5400e4, 0x5400e0, 0x5400dc};

static struct attn_hw_reg bmb_int2_bb_b0 = {
	2, 26, 0x5400f0, 0x5400fc, 0x5400f8, 0x5400f4};

static struct attn_hw_reg bmb_int3_bb_b0 = {
	3, 31, 0x540108, 0x540114, 0x540110, 0x54010c};

static struct attn_hw_reg bmb_int4_bb_b0 = {
	4, 27, 0x540120, 0x54012c, 0x540128, 0x540124};

static struct attn_hw_reg bmb_int5_bb_b0 = {
	5, 29, 0x540138, 0x540144, 0x540140, 0x54013c};

static struct attn_hw_reg bmb_int6_bb_b0 = {
	6, 30, 0x540150, 0x54015c, 0x540158, 0x540154};

static struct attn_hw_reg bmb_int7_bb_b0 = {
	7, 32, 0x540168, 0x540174, 0x540170, 0x54016c};

static struct attn_hw_reg bmb_int8_bb_b0 = {
	8, 32, 0x540184, 0x540190, 0x54018c, 0x540188};

static struct attn_hw_reg bmb_int9_bb_b0 = {
	9, 32, 0x54019c, 0x5401a8, 0x5401a4, 0x5401a0};

static struct attn_hw_reg bmb_int10_bb_b0 = {
	10, 3, 0x5401b4, 0x5401c0, 0x5401bc, 0x5401b8};

static struct attn_hw_reg bmb_int11_bb_b0 = {
	11, 4, 0x5401cc, 0x5401d8, 0x5401d4, 0x5401d0};

static struct attn_hw_reg *bmb_int_bb_b0_regs[12] = {
	&bmb_int0_bb_b0, &bmb_int1_bb_b0, &bmb_int2_bb_b0, &bmb_int3_bb_b0,
	&bmb_int4_bb_b0, &bmb_int5_bb_b0, &bmb_int6_bb_b0, &bmb_int7_bb_b0,
	&bmb_int8_bb_b0, &bmb_int9_bb_b0, &bmb_int10_bb_b0, &bmb_int11_bb_b0};

static struct attn_hw_reg bmb_prty0_bb_b0 = {
	0, 5, 0x5401dc, 0x5401e8, 0x5401e4, 0x5401e0};

static struct attn_hw_reg bmb_prty1_bb_b0 = {
	1, 31, 0x540400, 0x54040c, 0x540408, 0x540404};

static struct attn_hw_reg bmb_prty2_bb_b0 = {
	2, 15, 0x540410, 0x54041c, 0x540418, 0x540414};

static struct attn_hw_reg *bmb_prty_bb_b0_regs[3] = {
	&bmb_prty0_bb_b0, &bmb_prty1_bb_b0, &bmb_prty2_bb_b0};

static struct attn_hw_reg pcie_prty1_bb_b0 = {
	0, 17, 0x54000, 0x5400c, 0x54008, 0x54004};

static struct attn_hw_reg *pcie_prty_bb_b0_regs[1] = {
	&pcie_prty1_bb_b0};

static struct attn_hw_reg mcp2_prty0_bb_b0 = {
	0, 1, 0x52040, 0x5204c, 0x52048, 0x52044};

static struct attn_hw_reg mcp2_prty1_bb_b0 = {
	1, 12, 0x52204, 0x52210, 0x5220c, 0x52208};

static struct attn_hw_reg *mcp2_prty_bb_b0_regs[2] = {
	&mcp2_prty0_bb_b0, &mcp2_prty1_bb_b0};

static struct attn_hw_reg pswhst_int0_bb_b0 = {
	0, 18, 0x2a0180, 0x2a018c, 0x2a0188, 0x2a0184};

static struct attn_hw_reg *pswhst_int_bb_b0_regs[1] = {
	&pswhst_int0_bb_b0};

static struct attn_hw_reg pswhst_prty0_bb_b0 = {
	0, 1, 0x2a0190, 0x2a019c, 0x2a0198, 0x2a0194};

static struct attn_hw_reg pswhst_prty1_bb_b0 = {
	1, 17, 0x2a0200, 0x2a020c, 0x2a0208, 0x2a0204};

static struct attn_hw_reg *pswhst_prty_bb_b0_regs[2] = {
	&pswhst_prty0_bb_b0, &pswhst_prty1_bb_b0};

static struct attn_hw_reg pswhst2_int0_bb_b0 = {
	0, 5, 0x29e180, 0x29e18c, 0x29e188, 0x29e184};

static struct attn_hw_reg *pswhst2_int_bb_b0_regs[1] = {
	&pswhst2_int0_bb_b0};

static struct attn_hw_reg pswhst2_prty0_bb_b0 = {
	0, 1, 0x29e190, 0x29e19c, 0x29e198, 0x29e194};

static struct attn_hw_reg *pswhst2_prty_bb_b0_regs[1] = {
	&pswhst2_prty0_bb_b0};

static struct attn_hw_reg pswrd_int0_bb_b0 = {
	0, 3, 0x29c180, 0x29c18c, 0x29c188, 0x29c184};

static struct attn_hw_reg *pswrd_int_bb_b0_regs[1] = {
	&pswrd_int0_bb_b0};

static struct attn_hw_reg pswrd_prty0_bb_b0 = {
	0, 1, 0x29c190, 0x29c19c, 0x29c198, 0x29c194};

static struct attn_hw_reg *pswrd_prty_bb_b0_regs[1] = {
	&pswrd_prty0_bb_b0};

static struct attn_hw_reg pswrd2_int0_bb_b0 = {
	0, 5, 0x29d180, 0x29d18c, 0x29d188, 0x29d184};

static struct attn_hw_reg *pswrd2_int_bb_b0_regs[1] = {
	&pswrd2_int0_bb_b0};

static struct attn_hw_reg pswrd2_prty0_bb_b0 = {
	0, 1, 0x29d190, 0x29d19c, 0x29d198, 0x29d194};

static struct attn_hw_reg pswrd2_prty1_bb_b0 = {
	1, 31, 0x29d200, 0x29d20c, 0x29d208, 0x29d204};

static struct attn_hw_reg pswrd2_prty2_bb_b0 = {
	2, 3, 0x29d210, 0x29d21c, 0x29d218, 0x29d214};

static struct attn_hw_reg *pswrd2_prty_bb_b0_regs[3] = {
	&pswrd2_prty0_bb_b0, &pswrd2_prty1_bb_b0, &pswrd2_prty2_bb_b0};

static struct attn_hw_reg pswwr_int0_bb_b0 = {
	0, 16, 0x29a180, 0x29a18c, 0x29a188, 0x29a184};

static struct attn_hw_reg *pswwr_int_bb_b0_regs[1] = {
	&pswwr_int0_bb_b0};

static struct attn_hw_reg pswwr_prty0_bb_b0 = {
	0, 1, 0x29a190, 0x29a19c, 0x29a198, 0x29a194};

static struct attn_hw_reg *pswwr_prty_bb_b0_regs[1] = {
	&pswwr_prty0_bb_b0};

static struct attn_hw_reg pswwr2_int0_bb_b0 = {
	0, 19, 0x29b180, 0x29b18c, 0x29b188, 0x29b184};

static struct attn_hw_reg *pswwr2_int_bb_b0_regs[1] = {
	&pswwr2_int0_bb_b0};

static struct attn_hw_reg pswwr2_prty0_bb_b0 = {
	0, 1, 0x29b190, 0x29b19c, 0x29b198, 0x29b194};

static struct attn_hw_reg pswwr2_prty1_bb_b0 = {
	1, 31, 0x29b200, 0x29b20c, 0x29b208, 0x29b204};

static struct attn_hw_reg pswwr2_prty2_bb_b0 = {
	2, 31, 0x29b210, 0x29b21c, 0x29b218, 0x29b214};

static struct attn_hw_reg pswwr2_prty3_bb_b0 = {
	3, 31, 0x29b220, 0x29b22c, 0x29b228, 0x29b224};

static struct attn_hw_reg pswwr2_prty4_bb_b0 = {
	4, 20, 0x29b230, 0x29b23c, 0x29b238, 0x29b234};

static struct attn_hw_reg *pswwr2_prty_bb_b0_regs[5] = {
	&pswwr2_prty0_bb_b0, &pswwr2_prty1_bb_b0, &pswwr2_prty2_bb_b0,
	&pswwr2_prty3_bb_b0, &pswwr2_prty4_bb_b0};

static struct attn_hw_reg pswrq_int0_bb_b0 = {
	0, 21, 0x280180, 0x28018c, 0x280188, 0x280184};

static struct attn_hw_reg *pswrq_int_bb_b0_regs[1] = {
	&pswrq_int0_bb_b0};

static struct attn_hw_reg pswrq_prty0_bb_b0 = {
	0, 1, 0x280190, 0x28019c, 0x280198, 0x280194};

static struct attn_hw_reg *pswrq_prty_bb_b0_regs[1] = {
	&pswrq_prty0_bb_b0};

static struct attn_hw_reg pswrq2_int0_bb_b0 = {
	0, 15, 0x240180, 0x24018c, 0x240188, 0x240184};

static struct attn_hw_reg *pswrq2_int_bb_b0_regs[1] = {
	&pswrq2_int0_bb_b0};

static struct attn_hw_reg pswrq2_prty1_bb_b0 = {
	0, 9, 0x240200, 0x24020c, 0x240208, 0x240204};

static struct attn_hw_reg *pswrq2_prty_bb_b0_regs[1] = {
	&pswrq2_prty1_bb_b0};

static struct attn_hw_reg pglcs_int0_bb_b0 = {
	0, 1, 0x1d00, 0x1d0c, 0x1d08, 0x1d04};

static struct attn_hw_reg *pglcs_int_bb_b0_regs[1] = {
	&pglcs_int0_bb_b0};

static struct attn_hw_reg dmae_int0_bb_b0 = {
	0, 2, 0xc180, 0xc18c, 0xc188, 0xc184};

static struct attn_hw_reg *dmae_int_bb_b0_regs[1] = {
	&dmae_int0_bb_b0};

static struct attn_hw_reg dmae_prty1_bb_b0 = {
	0, 3, 0xc200, 0xc20c, 0xc208, 0xc204};

static struct attn_hw_reg *dmae_prty_bb_b0_regs[1] = {
	&dmae_prty1_bb_b0};

static struct attn_hw_reg ptu_int0_bb_b0 = {
	0, 8, 0x560180, 0x56018c, 0x560188, 0x560184};

static struct attn_hw_reg *ptu_int_bb_b0_regs[1] = {
	&ptu_int0_bb_b0};

static struct attn_hw_reg ptu_prty1_bb_b0 = {
	0, 18, 0x560200, 0x56020c, 0x560208, 0x560204};

static struct attn_hw_reg *ptu_prty_bb_b0_regs[1] = {
	&ptu_prty1_bb_b0};

static struct attn_hw_reg tcm_int0_bb_b0 = {
	0, 8, 0x1180180, 0x118018c, 0x1180188, 0x1180184};

static struct attn_hw_reg tcm_int1_bb_b0 = {
	1, 32, 0x1180190, 0x118019c, 0x1180198, 0x1180194};

static struct attn_hw_reg tcm_int2_bb_b0 = {
	2, 1, 0x11801a0, 0x11801ac, 0x11801a8, 0x11801a4};

static struct attn_hw_reg *tcm_int_bb_b0_regs[3] = {
	&tcm_int0_bb_b0, &tcm_int1_bb_b0, &tcm_int2_bb_b0};

static struct attn_hw_reg tcm_prty1_bb_b0 = {
	0, 31, 0x1180200, 0x118020c, 0x1180208, 0x1180204};

static struct attn_hw_reg tcm_prty2_bb_b0 = {
	1, 2, 0x1180210, 0x118021c, 0x1180218, 0x1180214};

static struct attn_hw_reg *tcm_prty_bb_b0_regs[2] = {
	&tcm_prty1_bb_b0, &tcm_prty2_bb_b0};

static struct attn_hw_reg mcm_int0_bb_b0 = {
	0, 14, 0x1200180, 0x120018c, 0x1200188, 0x1200184};

static struct attn_hw_reg mcm_int1_bb_b0 = {
	1, 26, 0x1200190, 0x120019c, 0x1200198, 0x1200194};

static struct attn_hw_reg mcm_int2_bb_b0 = {
	2, 1, 0x12001a0, 0x12001ac, 0x12001a8, 0x12001a4};

static struct attn_hw_reg *mcm_int_bb_b0_regs[3] = {
	&mcm_int0_bb_b0, &mcm_int1_bb_b0, &mcm_int2_bb_b0};

static struct attn_hw_reg mcm_prty1_bb_b0 = {
	0, 31, 0x1200200, 0x120020c, 0x1200208, 0x1200204};

static struct attn_hw_reg mcm_prty2_bb_b0 = {
	1, 4, 0x1200210, 0x120021c, 0x1200218, 0x1200214};

static struct attn_hw_reg *mcm_prty_bb_b0_regs[2] = {
	&mcm_prty1_bb_b0, &mcm_prty2_bb_b0};

static struct attn_hw_reg ucm_int0_bb_b0 = {
	0, 17, 0x1280180, 0x128018c, 0x1280188, 0x1280184};

static struct attn_hw_reg ucm_int1_bb_b0 = {
	1, 29, 0x1280190, 0x128019c, 0x1280198, 0x1280194};

static struct attn_hw_reg ucm_int2_bb_b0 = {
	2, 1, 0x12801a0, 0x12801ac, 0x12801a8, 0x12801a4};

static struct attn_hw_reg *ucm_int_bb_b0_regs[3] = {
	&ucm_int0_bb_b0, &ucm_int1_bb_b0, &ucm_int2_bb_b0};

static struct attn_hw_reg ucm_prty1_bb_b0 = {
	0, 31, 0x1280200, 0x128020c, 0x1280208, 0x1280204};

static struct attn_hw_reg ucm_prty2_bb_b0 = {
	1, 7, 0x1280210, 0x128021c, 0x1280218, 0x1280214};

static struct attn_hw_reg *ucm_prty_bb_b0_regs[2] = {
	&ucm_prty1_bb_b0, &ucm_prty2_bb_b0};

static struct attn_hw_reg xcm_int0_bb_b0 = {
	0, 16, 0x1000180, 0x100018c, 0x1000188, 0x1000184};

static struct attn_hw_reg xcm_int1_bb_b0 = {
	1, 25, 0x1000190, 0x100019c, 0x1000198, 0x1000194};

static struct attn_hw_reg xcm_int2_bb_b0 = {
	2, 8, 0x10001a0, 0x10001ac, 0x10001a8, 0x10001a4};

static struct attn_hw_reg *xcm_int_bb_b0_regs[3] = {
	&xcm_int0_bb_b0, &xcm_int1_bb_b0, &xcm_int2_bb_b0};

static struct attn_hw_reg xcm_prty1_bb_b0 = {
	0, 31, 0x1000200, 0x100020c, 0x1000208, 0x1000204};

static struct attn_hw_reg xcm_prty2_bb_b0 = {
	1, 11, 0x1000210, 0x100021c, 0x1000218, 0x1000214};

static struct attn_hw_reg *xcm_prty_bb_b0_regs[2] = {
	&xcm_prty1_bb_b0, &xcm_prty2_bb_b0};

static struct attn_hw_reg ycm_int0_bb_b0 = {
	0, 13, 0x1080180, 0x108018c, 0x1080188, 0x1080184};

static struct attn_hw_reg ycm_int1_bb_b0 = {
	1, 23, 0x1080190, 0x108019c, 0x1080198, 0x1080194};

static struct attn_hw_reg ycm_int2_bb_b0 = {
	2, 1, 0x10801a0, 0x10801ac, 0x10801a8, 0x10801a4};

static struct attn_hw_reg *ycm_int_bb_b0_regs[3] = {
	&ycm_int0_bb_b0, &ycm_int1_bb_b0, &ycm_int2_bb_b0};

static struct attn_hw_reg ycm_prty1_bb_b0 = {
	0, 31, 0x1080200, 0x108020c, 0x1080208, 0x1080204};

static struct attn_hw_reg ycm_prty2_bb_b0 = {
	1, 3, 0x1080210, 0x108021c, 0x1080218, 0x1080214};

static struct attn_hw_reg *ycm_prty_bb_b0_regs[2] = {
	&ycm_prty1_bb_b0, &ycm_prty2_bb_b0};

static struct attn_hw_reg pcm_int0_bb_b0 = {
	0, 5, 0x1100180, 0x110018c, 0x1100188, 0x1100184};

static struct attn_hw_reg pcm_int1_bb_b0 = {
	1, 14, 0x1100190, 0x110019c, 0x1100198, 0x1100194};

static struct attn_hw_reg pcm_int2_bb_b0 = {
	2, 1, 0x11001a0, 0x11001ac, 0x11001a8, 0x11001a4};

static struct attn_hw_reg *pcm_int_bb_b0_regs[3] = {
	&pcm_int0_bb_b0, &pcm_int1_bb_b0, &pcm_int2_bb_b0};

static struct attn_hw_reg pcm_prty1_bb_b0 = {
	0, 11, 0x1100200, 0x110020c, 0x1100208, 0x1100204};

static struct attn_hw_reg *pcm_prty_bb_b0_regs[1] = {
	&pcm_prty1_bb_b0};

static struct attn_hw_reg qm_int0_bb_b0 = {
	0, 22, 0x2f0180, 0x2f018c, 0x2f0188, 0x2f0184};

static struct attn_hw_reg *qm_int_bb_b0_regs[1] = {
	&qm_int0_bb_b0};

static struct attn_hw_reg qm_prty0_bb_b0 = {
	0, 11, 0x2f0190, 0x2f019c, 0x2f0198, 0x2f0194};

static struct attn_hw_reg qm_prty1_bb_b0 = {
	1, 31, 0x2f0200, 0x2f020c, 0x2f0208, 0x2f0204};

static struct attn_hw_reg qm_prty2_bb_b0 = {
	2, 31, 0x2f0210, 0x2f021c, 0x2f0218, 0x2f0214};

static struct attn_hw_reg qm_prty3_bb_b0 = {
	3, 11, 0x2f0220, 0x2f022c, 0x2f0228, 0x2f0224};

static struct attn_hw_reg *qm_prty_bb_b0_regs[4] = {
	&qm_prty0_bb_b0, &qm_prty1_bb_b0, &qm_prty2_bb_b0, &qm_prty3_bb_b0};

static struct attn_hw_reg tm_int0_bb_b0 = {
	0, 32, 0x2c0180, 0x2c018c, 0x2c0188, 0x2c0184};

static struct attn_hw_reg tm_int1_bb_b0 = {
	1, 11, 0x2c0190, 0x2c019c, 0x2c0198, 0x2c0194};

static struct attn_hw_reg *tm_int_bb_b0_regs[2] = {
	&tm_int0_bb_b0, &tm_int1_bb_b0};

static struct attn_hw_reg tm_prty1_bb_b0 = {
	0, 17, 0x2c0200, 0x2c020c, 0x2c0208, 0x2c0204};

static struct attn_hw_reg *tm_prty_bb_b0_regs[1] = {
	&tm_prty1_bb_b0};

static struct attn_hw_reg dorq_int0_bb_b0 = {
	0, 9, 0x100180, 0x10018c, 0x100188, 0x100184};

static struct attn_hw_reg *dorq_int_bb_b0_regs[1] = {
	&dorq_int0_bb_b0};

static struct attn_hw_reg dorq_prty0_bb_b0 = {
	0, 1, 0x100190, 0x10019c, 0x100198, 0x100194};

static struct attn_hw_reg dorq_prty1_bb_b0 = {
	1, 6, 0x100200, 0x10020c, 0x100208, 0x100204};

static struct attn_hw_reg *dorq_prty_bb_b0_regs[2] = {
	&dorq_prty0_bb_b0, &dorq_prty1_bb_b0};

static struct attn_hw_reg brb_int0_bb_b0 = {
	0, 32, 0x3400c0, 0x3400cc, 0x3400c8, 0x3400c4};

static struct attn_hw_reg brb_int1_bb_b0 = {
	1, 30, 0x3400d8, 0x3400e4, 0x3400e0, 0x3400dc};

static struct attn_hw_reg brb_int2_bb_b0 = {
	2, 28, 0x3400f0, 0x3400fc, 0x3400f8, 0x3400f4};

static struct attn_hw_reg brb_int3_bb_b0 = {
	3, 31, 0x340108, 0x340114, 0x340110, 0x34010c};

static struct attn_hw_reg brb_int4_bb_b0 = {
	4, 27, 0x340120, 0x34012c, 0x340128, 0x340124};

static struct attn_hw_reg brb_int5_bb_b0 = {
	5, 1, 0x340138, 0x340144, 0x340140, 0x34013c};

static struct attn_hw_reg brb_int6_bb_b0 = {
	6, 8, 0x340150, 0x34015c, 0x340158, 0x340154};

static struct attn_hw_reg brb_int7_bb_b0 = {
	7, 32, 0x340168, 0x340174, 0x340170, 0x34016c};

static struct attn_hw_reg brb_int8_bb_b0 = {
	8, 17, 0x340184, 0x340190, 0x34018c, 0x340188};

static struct attn_hw_reg brb_int9_bb_b0 = {
	9, 1, 0x34019c, 0x3401a8, 0x3401a4, 0x3401a0};

static struct attn_hw_reg brb_int10_bb_b0 = {
	10, 14, 0x3401b4, 0x3401c0, 0x3401bc, 0x3401b8};

static struct attn_hw_reg brb_int11_bb_b0 = {
	11, 8, 0x3401cc, 0x3401d8, 0x3401d4, 0x3401d0};

static struct attn_hw_reg *brb_int_bb_b0_regs[12] = {
	&brb_int0_bb_b0, &brb_int1_bb_b0, &brb_int2_bb_b0, &brb_int3_bb_b0,
	&brb_int4_bb_b0, &brb_int5_bb_b0, &brb_int6_bb_b0, &brb_int7_bb_b0,
	&brb_int8_bb_b0, &brb_int9_bb_b0, &brb_int10_bb_b0, &brb_int11_bb_b0};

static struct attn_hw_reg brb_prty0_bb_b0 = {
	0, 5, 0x3401dc, 0x3401e8, 0x3401e4, 0x3401e0};

static struct attn_hw_reg brb_prty1_bb_b0 = {
	1, 31, 0x340400, 0x34040c, 0x340408, 0x340404};

static struct attn_hw_reg brb_prty2_bb_b0 = {
	2, 14, 0x340410, 0x34041c, 0x340418, 0x340414};

static struct attn_hw_reg *brb_prty_bb_b0_regs[3] = {
	&brb_prty0_bb_b0, &brb_prty1_bb_b0, &brb_prty2_bb_b0};

static struct attn_hw_reg src_int0_bb_b0 = {
	0, 1, 0x2381d8, 0x2381dc, 0x2381e0, 0x2381e4};

static struct attn_hw_reg *src_int_bb_b0_regs[1] = {
	&src_int0_bb_b0};

static struct attn_hw_reg prs_int0_bb_b0 = {
	0, 2, 0x1f0040, 0x1f004c, 0x1f0048, 0x1f0044};

static struct attn_hw_reg *prs_int_bb_b0_regs[1] = {
	&prs_int0_bb_b0};

static struct attn_hw_reg prs_prty0_bb_b0 = {
	0, 2, 0x1f0050, 0x1f005c, 0x1f0058, 0x1f0054};

static struct attn_hw_reg prs_prty1_bb_b0 = {
	1, 31, 0x1f0204, 0x1f0210, 0x1f020c, 0x1f0208};

static struct attn_hw_reg prs_prty2_bb_b0 = {
	2, 5, 0x1f0214, 0x1f0220, 0x1f021c, 0x1f0218};

static struct attn_hw_reg *prs_prty_bb_b0_regs[3] = {
	&prs_prty0_bb_b0, &prs_prty1_bb_b0, &prs_prty2_bb_b0};

static struct attn_hw_reg tsdm_int0_bb_b0 = {
	0, 26, 0xfb0040, 0xfb004c, 0xfb0048, 0xfb0044};

static struct attn_hw_reg *tsdm_int_bb_b0_regs[1] = {
	&tsdm_int0_bb_b0};

static struct attn_hw_reg tsdm_prty1_bb_b0 = {
	0, 10, 0xfb0200, 0xfb020c, 0xfb0208, 0xfb0204};

static struct attn_hw_reg *tsdm_prty_bb_b0_regs[1] = {
	&tsdm_prty1_bb_b0};

static struct attn_hw_reg msdm_int0_bb_b0 = {
	0, 26, 0xfc0040, 0xfc004c, 0xfc0048, 0xfc0044};

static struct attn_hw_reg *msdm_int_bb_b0_regs[1] = {
	&msdm_int0_bb_b0};

static struct attn_hw_reg msdm_prty1_bb_b0 = {
	0, 11, 0xfc0200, 0xfc020c, 0xfc0208, 0xfc0204};

static struct attn_hw_reg *msdm_prty_bb_b0_regs[1] = {
	&msdm_prty1_bb_b0};

static struct attn_hw_reg usdm_int0_bb_b0 = {
	0, 26, 0xfd0040, 0xfd004c, 0xfd0048, 0xfd0044};

static struct attn_hw_reg *usdm_int_bb_b0_regs[1] = {
	&usdm_int0_bb_b0};

static struct attn_hw_reg usdm_prty1_bb_b0 = {
	0, 10, 0xfd0200, 0xfd020c, 0xfd0208, 0xfd0204};

static struct attn_hw_reg *usdm_prty_bb_b0_regs[1] = {
	&usdm_prty1_bb_b0};

static struct attn_hw_reg xsdm_int0_bb_b0 = {
	0, 26, 0xf80040, 0xf8004c, 0xf80048, 0xf80044};

static struct attn_hw_reg *xsdm_int_bb_b0_regs[1] = {
	&xsdm_int0_bb_b0};

static struct attn_hw_reg xsdm_prty1_bb_b0 = {
	0, 10, 0xf80200, 0xf8020c, 0xf80208, 0xf80204};

static struct attn_hw_reg *xsdm_prty_bb_b0_regs[1] = {
	&xsdm_prty1_bb_b0};

static struct attn_hw_reg ysdm_int0_bb_b0 = {
	0, 26, 0xf90040, 0xf9004c, 0xf90048, 0xf90044};

static struct attn_hw_reg *ysdm_int_bb_b0_regs[1] = {
	&ysdm_int0_bb_b0};

static struct attn_hw_reg ysdm_prty1_bb_b0 = {
	0, 9, 0xf90200, 0xf9020c, 0xf90208, 0xf90204};

static struct attn_hw_reg *ysdm_prty_bb_b0_regs[1] = {
	&ysdm_prty1_bb_b0};

static struct attn_hw_reg psdm_int0_bb_b0 = {
	0, 26, 0xfa0040, 0xfa004c, 0xfa0048, 0xfa0044};

static struct attn_hw_reg *psdm_int_bb_b0_regs[1] = {
	&psdm_int0_bb_b0};

static struct attn_hw_reg psdm_prty1_bb_b0 = {
	0, 9, 0xfa0200, 0xfa020c, 0xfa0208, 0xfa0204};

static struct attn_hw_reg *psdm_prty_bb_b0_regs[1] = {
	&psdm_prty1_bb_b0};

static struct attn_hw_reg tsem_int0_bb_b0 = {
	0, 32, 0x1700040, 0x170004c, 0x1700048, 0x1700044};

static struct attn_hw_reg tsem_int1_bb_b0 = {
	1, 13, 0x1700050, 0x170005c, 0x1700058, 0x1700054};

static struct attn_hw_reg tsem_fast_memory_int0_bb_b0 = {
	2, 1, 0x1740040, 0x174004c, 0x1740048, 0x1740044};

static struct attn_hw_reg *tsem_int_bb_b0_regs[3] = {
	&tsem_int0_bb_b0, &tsem_int1_bb_b0, &tsem_fast_memory_int0_bb_b0};

static struct attn_hw_reg tsem_prty0_bb_b0 = {
	0, 3, 0x17000c8, 0x17000d4, 0x17000d0, 0x17000cc};

static struct attn_hw_reg tsem_prty1_bb_b0 = {
	1, 6, 0x1700200, 0x170020c, 0x1700208, 0x1700204};

static struct attn_hw_reg tsem_fast_memory_vfc_config_prty1_bb_b0 = {
	2, 6, 0x174a200, 0x174a20c, 0x174a208, 0x174a204};

static struct attn_hw_reg *tsem_prty_bb_b0_regs[3] = {
	&tsem_prty0_bb_b0, &tsem_prty1_bb_b0,
	&tsem_fast_memory_vfc_config_prty1_bb_b0};

static struct attn_hw_reg msem_int0_bb_b0 = {
	0, 32, 0x1800040, 0x180004c, 0x1800048, 0x1800044};

static struct attn_hw_reg msem_int1_bb_b0 = {
	1, 13, 0x1800050, 0x180005c, 0x1800058, 0x1800054};

static struct attn_hw_reg msem_fast_memory_int0_bb_b0 = {
	2, 1, 0x1840040, 0x184004c, 0x1840048, 0x1840044};

static struct attn_hw_reg *msem_int_bb_b0_regs[3] = {
	&msem_int0_bb_b0, &msem_int1_bb_b0, &msem_fast_memory_int0_bb_b0};

static struct attn_hw_reg msem_prty0_bb_b0 = {
	0, 3, 0x18000c8, 0x18000d4, 0x18000d0, 0x18000cc};

static struct attn_hw_reg msem_prty1_bb_b0 = {
	1, 6, 0x1800200, 0x180020c, 0x1800208, 0x1800204};

static struct attn_hw_reg *msem_prty_bb_b0_regs[2] = {
	&msem_prty0_bb_b0, &msem_prty1_bb_b0};

static struct attn_hw_reg usem_int0_bb_b0 = {
	0, 32, 0x1900040, 0x190004c, 0x1900048, 0x1900044};

static struct attn_hw_reg usem_int1_bb_b0 = {
	1, 13, 0x1900050, 0x190005c, 0x1900058, 0x1900054};

static struct attn_hw_reg usem_fast_memory_int0_bb_b0 = {
	2, 1, 0x1940040, 0x194004c, 0x1940048, 0x1940044};

static struct attn_hw_reg *usem_int_bb_b0_regs[3] = {
	&usem_int0_bb_b0, &usem_int1_bb_b0, &usem_fast_memory_int0_bb_b0};

static struct attn_hw_reg usem_prty0_bb_b0 = {
	0, 3, 0x19000c8, 0x19000d4, 0x19000d0, 0x19000cc};

static struct attn_hw_reg usem_prty1_bb_b0 = {
	1, 6, 0x1900200, 0x190020c, 0x1900208, 0x1900204};

static struct attn_hw_reg *usem_prty_bb_b0_regs[2] = {
	&usem_prty0_bb_b0, &usem_prty1_bb_b0};

static struct attn_hw_reg xsem_int0_bb_b0 = {
	0, 32, 0x1400040, 0x140004c, 0x1400048, 0x1400044};

static struct attn_hw_reg xsem_int1_bb_b0 = {
	1, 13, 0x1400050, 0x140005c, 0x1400058, 0x1400054};

static struct attn_hw_reg xsem_fast_memory_int0_bb_b0 = {
	2, 1, 0x1440040, 0x144004c, 0x1440048, 0x1440044};

static struct attn_hw_reg *xsem_int_bb_b0_regs[3] = {
	&xsem_int0_bb_b0, &xsem_int1_bb_b0, &xsem_fast_memory_int0_bb_b0};

static struct attn_hw_reg xsem_prty0_bb_b0 = {
	0, 3, 0x14000c8, 0x14000d4, 0x14000d0, 0x14000cc};

static struct attn_hw_reg xsem_prty1_bb_b0 = {
	1, 7, 0x1400200, 0x140020c, 0x1400208, 0x1400204};

static struct attn_hw_reg *xsem_prty_bb_b0_regs[2] = {
	&xsem_prty0_bb_b0, &xsem_prty1_bb_b0};

static struct attn_hw_reg ysem_int0_bb_b0 = {
	0, 32, 0x1500040, 0x150004c, 0x1500048, 0x1500044};

static struct attn_hw_reg ysem_int1_bb_b0 = {
	1, 13, 0x1500050, 0x150005c, 0x1500058, 0x1500054};

static struct attn_hw_reg ysem_fast_memory_int0_bb_b0 = {
	2, 1, 0x1540040, 0x154004c, 0x1540048, 0x1540044};

static struct attn_hw_reg *ysem_int_bb_b0_regs[3] = {
	&ysem_int0_bb_b0, &ysem_int1_bb_b0, &ysem_fast_memory_int0_bb_b0};

static struct attn_hw_reg ysem_prty0_bb_b0 = {
	0, 3, 0x15000c8, 0x15000d4, 0x15000d0, 0x15000cc};

static struct attn_hw_reg ysem_prty1_bb_b0 = {
	1, 7, 0x1500200, 0x150020c, 0x1500208, 0x1500204};

static struct attn_hw_reg *ysem_prty_bb_b0_regs[2] = {
	&ysem_prty0_bb_b0, &ysem_prty1_bb_b0};

static struct attn_hw_reg psem_int0_bb_b0 = {
	0, 32, 0x1600040, 0x160004c, 0x1600048, 0x1600044};

static struct attn_hw_reg psem_int1_bb_b0 = {
	1, 13, 0x1600050, 0x160005c, 0x1600058, 0x1600054};

static struct attn_hw_reg psem_fast_memory_int0_bb_b0 = {
	2, 1, 0x1640040, 0x164004c, 0x1640048, 0x1640044};

static struct attn_hw_reg *psem_int_bb_b0_regs[3] = {
	&psem_int0_bb_b0, &psem_int1_bb_b0, &psem_fast_memory_int0_bb_b0};

static struct attn_hw_reg psem_prty0_bb_b0 = {
	0, 3, 0x16000c8, 0x16000d4, 0x16000d0, 0x16000cc};

static struct attn_hw_reg psem_prty1_bb_b0 = {
	1, 6, 0x1600200, 0x160020c, 0x1600208, 0x1600204};

static struct attn_hw_reg psem_fast_memory_vfc_config_prty1_bb_b0 = {
	2, 6, 0x164a200, 0x164a20c, 0x164a208, 0x164a204};

static struct attn_hw_reg *psem_prty_bb_b0_regs[3] = {
	&psem_prty0_bb_b0, &psem_prty1_bb_b0,
	&psem_fast_memory_vfc_config_prty1_bb_b0};

static struct attn_hw_reg rss_int0_bb_b0 = {
	0, 12, 0x238980, 0x23898c, 0x238988, 0x238984};

static struct attn_hw_reg *rss_int_bb_b0_regs[1] = {
	&rss_int0_bb_b0};

static struct attn_hw_reg rss_prty1_bb_b0 = {
	0, 4, 0x238a00, 0x238a0c, 0x238a08, 0x238a04};

static struct attn_hw_reg *rss_prty_bb_b0_regs[1] = {
	&rss_prty1_bb_b0};

static struct attn_hw_reg tmld_int0_bb_b0 = {
	0, 6, 0x4d0180, 0x4d018c, 0x4d0188, 0x4d0184};

static struct attn_hw_reg *tmld_int_bb_b0_regs[1] = {
	&tmld_int0_bb_b0};

static struct attn_hw_reg tmld_prty1_bb_b0 = {
	0, 8, 0x4d0200, 0x4d020c, 0x4d0208, 0x4d0204};

static struct attn_hw_reg *tmld_prty_bb_b0_regs[1] = {
	&tmld_prty1_bb_b0};

static struct attn_hw_reg muld_int0_bb_b0 = {
	0, 6, 0x4e0180, 0x4e018c, 0x4e0188, 0x4e0184};

static struct attn_hw_reg *muld_int_bb_b0_regs[1] = {
	&muld_int0_bb_b0};

static struct attn_hw_reg muld_prty1_bb_b0 = {
	0, 10, 0x4e0200, 0x4e020c, 0x4e0208, 0x4e0204};

static struct attn_hw_reg *muld_prty_bb_b0_regs[1] = {
	&muld_prty1_bb_b0};

static struct attn_hw_reg yuld_int0_bb_b0 = {
	0, 6, 0x4c8180, 0x4c818c, 0x4c8188, 0x4c8184};

static struct attn_hw_reg *yuld_int_bb_b0_regs[1] = {
	&yuld_int0_bb_b0};

static struct attn_hw_reg yuld_prty1_bb_b0 = {
	0, 6, 0x4c8200, 0x4c820c, 0x4c8208, 0x4c8204};

static struct attn_hw_reg *yuld_prty_bb_b0_regs[1] = {
	&yuld_prty1_bb_b0};

static struct attn_hw_reg xyld_int0_bb_b0 = {
	0, 6, 0x4c0180, 0x4c018c, 0x4c0188, 0x4c0184};

static struct attn_hw_reg *xyld_int_bb_b0_regs[1] = {
	&xyld_int0_bb_b0};

static struct attn_hw_reg xyld_prty1_bb_b0 = {
	0, 9, 0x4c0200, 0x4c020c, 0x4c0208, 0x4c0204};

static struct attn_hw_reg *xyld_prty_bb_b0_regs[1] = {
	&xyld_prty1_bb_b0};

static struct attn_hw_reg prm_int0_bb_b0 = {
	0, 11, 0x230040, 0x23004c, 0x230048, 0x230044};

static struct attn_hw_reg *prm_int_bb_b0_regs[1] = {
	&prm_int0_bb_b0};

static struct attn_hw_reg prm_prty0_bb_b0 = {
	0, 1, 0x230050, 0x23005c, 0x230058, 0x230054};

static struct attn_hw_reg prm_prty1_bb_b0 = {
	1, 24, 0x230200, 0x23020c, 0x230208, 0x230204};

static struct attn_hw_reg *prm_prty_bb_b0_regs[2] = {
	&prm_prty0_bb_b0, &prm_prty1_bb_b0};

static struct attn_hw_reg pbf_pb1_int0_bb_b0 = {
	0, 9, 0xda0040, 0xda004c, 0xda0048, 0xda0044};

static struct attn_hw_reg *pbf_pb1_int_bb_b0_regs[1] = {
	&pbf_pb1_int0_bb_b0};

static struct attn_hw_reg pbf_pb1_prty0_bb_b0 = {
	0, 1, 0xda0050, 0xda005c, 0xda0058, 0xda0054};

static struct attn_hw_reg *pbf_pb1_prty_bb_b0_regs[1] = {
	&pbf_pb1_prty0_bb_b0};

static struct attn_hw_reg pbf_pb2_int0_bb_b0 = {
	0, 9, 0xda4040, 0xda404c, 0xda4048, 0xda4044};

static struct attn_hw_reg *pbf_pb2_int_bb_b0_regs[1] = {
	&pbf_pb2_int0_bb_b0};

static struct attn_hw_reg pbf_pb2_prty0_bb_b0 = {
	0, 1, 0xda4050, 0xda405c, 0xda4058, 0xda4054};

static struct attn_hw_reg *pbf_pb2_prty_bb_b0_regs[1] = {
	&pbf_pb2_prty0_bb_b0};

static struct attn_hw_reg rpb_int0_bb_b0 = {
	0, 9, 0x23c040, 0x23c04c, 0x23c048, 0x23c044};

static struct attn_hw_reg *rpb_int_bb_b0_regs[1] = {
	&rpb_int0_bb_b0};

static struct attn_hw_reg rpb_prty0_bb_b0 = {
	0, 1, 0x23c050, 0x23c05c, 0x23c058, 0x23c054};

static struct attn_hw_reg *rpb_prty_bb_b0_regs[1] = {
	&rpb_prty0_bb_b0};

static struct attn_hw_reg btb_int0_bb_b0 = {
	0, 16, 0xdb00c0, 0xdb00cc, 0xdb00c8, 0xdb00c4};

static struct attn_hw_reg btb_int1_bb_b0 = {
	1, 16, 0xdb00d8, 0xdb00e4, 0xdb00e0, 0xdb00dc};

static struct attn_hw_reg btb_int2_bb_b0 = {
	2, 4, 0xdb00f0, 0xdb00fc, 0xdb00f8, 0xdb00f4};

static struct attn_hw_reg btb_int3_bb_b0 = {
	3, 32, 0xdb0108, 0xdb0114, 0xdb0110, 0xdb010c};

static struct attn_hw_reg btb_int4_bb_b0 = {
	4, 23, 0xdb0120, 0xdb012c, 0xdb0128, 0xdb0124};

static struct attn_hw_reg btb_int5_bb_b0 = {
	5, 32, 0xdb0138, 0xdb0144, 0xdb0140, 0xdb013c};

static struct attn_hw_reg btb_int6_bb_b0 = {
	6, 1, 0xdb0150, 0xdb015c, 0xdb0158, 0xdb0154};

static struct attn_hw_reg btb_int8_bb_b0 = {
	7, 1, 0xdb0184, 0xdb0190, 0xdb018c, 0xdb0188};

static struct attn_hw_reg btb_int9_bb_b0 = {
	8, 1, 0xdb019c, 0xdb01a8, 0xdb01a4, 0xdb01a0};

static struct attn_hw_reg btb_int10_bb_b0 = {
	9, 1, 0xdb01b4, 0xdb01c0, 0xdb01bc, 0xdb01b8};

static struct attn_hw_reg btb_int11_bb_b0 = {
	10, 2, 0xdb01cc, 0xdb01d8, 0xdb01d4, 0xdb01d0};

static struct attn_hw_reg *btb_int_bb_b0_regs[11] = {
	&btb_int0_bb_b0, &btb_int1_bb_b0, &btb_int2_bb_b0, &btb_int3_bb_b0,
	&btb_int4_bb_b0, &btb_int5_bb_b0, &btb_int6_bb_b0, &btb_int8_bb_b0,
	&btb_int9_bb_b0, &btb_int10_bb_b0, &btb_int11_bb_b0};

static struct attn_hw_reg btb_prty0_bb_b0 = {
	0, 5, 0xdb01dc, 0xdb01e8, 0xdb01e4, 0xdb01e0};

static struct attn_hw_reg btb_prty1_bb_b0 = {
	1, 23, 0xdb0400, 0xdb040c, 0xdb0408, 0xdb0404};

static struct attn_hw_reg *btb_prty_bb_b0_regs[2] = {
	&btb_prty0_bb_b0, &btb_prty1_bb_b0};

static struct attn_hw_reg pbf_int0_bb_b0 = {
	0, 1, 0xd80180, 0xd8018c, 0xd80188, 0xd80184};

static struct attn_hw_reg *pbf_int_bb_b0_regs[1] = {
	&pbf_int0_bb_b0};

static struct attn_hw_reg pbf_prty0_bb_b0 = {
	0, 1, 0xd80190, 0xd8019c, 0xd80198, 0xd80194};

static struct attn_hw_reg pbf_prty1_bb_b0 = {
	1, 31, 0xd80200, 0xd8020c, 0xd80208, 0xd80204};

static struct attn_hw_reg pbf_prty2_bb_b0 = {
	2, 27, 0xd80210, 0xd8021c, 0xd80218, 0xd80214};

static struct attn_hw_reg *pbf_prty_bb_b0_regs[3] = {
	&pbf_prty0_bb_b0, &pbf_prty1_bb_b0, &pbf_prty2_bb_b0};

static struct attn_hw_reg rdif_int0_bb_b0 = {
	0, 8, 0x300180, 0x30018c, 0x300188, 0x300184};

static struct attn_hw_reg *rdif_int_bb_b0_regs[1] = {
	&rdif_int0_bb_b0};

static struct attn_hw_reg rdif_prty0_bb_b0 = {
	0, 1, 0x300190, 0x30019c, 0x300198, 0x300194};

static struct attn_hw_reg *rdif_prty_bb_b0_regs[1] = {
	&rdif_prty0_bb_b0};

static struct attn_hw_reg tdif_int0_bb_b0 = {
	0, 8, 0x310180, 0x31018c, 0x310188, 0x310184};

static struct attn_hw_reg *tdif_int_bb_b0_regs[1] = {
	&tdif_int0_bb_b0};

static struct attn_hw_reg tdif_prty0_bb_b0 = {
	0, 1, 0x310190, 0x31019c, 0x310198, 0x310194};

static struct attn_hw_reg tdif_prty1_bb_b0 = {
	1, 11, 0x310200, 0x31020c, 0x310208, 0x310204};

static struct attn_hw_reg *tdif_prty_bb_b0_regs[2] = {
	&tdif_prty0_bb_b0, &tdif_prty1_bb_b0};

static struct attn_hw_reg cdu_int0_bb_b0 = {
	0, 8, 0x5801c0, 0x5801c4, 0x5801c8, 0x5801cc};

static struct attn_hw_reg *cdu_int_bb_b0_regs[1] = {
	&cdu_int0_bb_b0};

static struct attn_hw_reg cdu_prty1_bb_b0 = {
	0, 5, 0x580200, 0x58020c, 0x580208, 0x580204};

static struct attn_hw_reg *cdu_prty_bb_b0_regs[1] = {
	&cdu_prty1_bb_b0};

static struct attn_hw_reg ccfc_int0_bb_b0 = {
	0, 2, 0x2e0180, 0x2e018c, 0x2e0188, 0x2e0184};

static struct attn_hw_reg *ccfc_int_bb_b0_regs[1] = {
	&ccfc_int0_bb_b0};

static struct attn_hw_reg ccfc_prty1_bb_b0 = {
	0, 2, 0x2e0200, 0x2e020c, 0x2e0208, 0x2e0204};

static struct attn_hw_reg ccfc_prty0_bb_b0 = {
	1, 6, 0x2e05e4, 0x2e05f0, 0x2e05ec, 0x2e05e8};

static struct attn_hw_reg *ccfc_prty_bb_b0_regs[2] = {
	&ccfc_prty1_bb_b0, &ccfc_prty0_bb_b0};

static struct attn_hw_reg tcfc_int0_bb_b0 = {
	0, 2, 0x2d0180, 0x2d018c, 0x2d0188, 0x2d0184};

static struct attn_hw_reg *tcfc_int_bb_b0_regs[1] = {
	&tcfc_int0_bb_b0};

static struct attn_hw_reg tcfc_prty1_bb_b0 = {
	0, 2, 0x2d0200, 0x2d020c, 0x2d0208, 0x2d0204};

static struct attn_hw_reg tcfc_prty0_bb_b0 = {
	1, 6, 0x2d05e4, 0x2d05f0, 0x2d05ec, 0x2d05e8};

static struct attn_hw_reg *tcfc_prty_bb_b0_regs[2] = {
	&tcfc_prty1_bb_b0, &tcfc_prty0_bb_b0};

static struct attn_hw_reg igu_int0_bb_b0 = {
	0, 11, 0x180180, 0x18018c, 0x180188, 0x180184};

static struct attn_hw_reg *igu_int_bb_b0_regs[1] = {
	&igu_int0_bb_b0};

static struct attn_hw_reg igu_prty0_bb_b0 = {
	0, 1, 0x180190, 0x18019c, 0x180198, 0x180194};

static struct attn_hw_reg igu_prty1_bb_b0 = {
	1, 31, 0x180200, 0x18020c, 0x180208, 0x180204};

static struct attn_hw_reg igu_prty2_bb_b0 = {
	2, 1, 0x180210, 0x18021c, 0x180218, 0x180214};

static struct attn_hw_reg *igu_prty_bb_b0_regs[3] = {
	&igu_prty0_bb_b0, &igu_prty1_bb_b0, &igu_prty2_bb_b0};

static struct attn_hw_reg cau_int0_bb_b0 = {
	0, 11, 0x1c00d4, 0x1c00d8, 0x1c00dc, 0x1c00e0};

static struct attn_hw_reg *cau_int_bb_b0_regs[1] = {
	&cau_int0_bb_b0};

static struct attn_hw_reg cau_prty1_bb_b0 = {
	0, 13, 0x1c0200, 0x1c020c, 0x1c0208, 0x1c0204};

static struct attn_hw_reg *cau_prty_bb_b0_regs[1] = {
	&cau_prty1_bb_b0};

static struct attn_hw_reg dbg_int0_bb_b0 = {
	0, 1, 0x10180, 0x1018c, 0x10188, 0x10184};

static struct attn_hw_reg *dbg_int_bb_b0_regs[1] = {
	&dbg_int0_bb_b0};

static struct attn_hw_reg dbg_prty1_bb_b0 = {
	0, 1, 0x10200, 0x1020c, 0x10208, 0x10204};

static struct attn_hw_reg *dbg_prty_bb_b0_regs[1] = {
	&dbg_prty1_bb_b0};

static struct attn_hw_reg nig_int0_bb_b0 = {
	0, 12, 0x500040, 0x50004c, 0x500048, 0x500044};

static struct attn_hw_reg nig_int1_bb_b0 = {
	1, 32, 0x500050, 0x50005c, 0x500058, 0x500054};

static struct attn_hw_reg nig_int2_bb_b0 = {
	2, 20, 0x500060, 0x50006c, 0x500068, 0x500064};

static struct attn_hw_reg nig_int3_bb_b0 = {
	3, 18, 0x500070, 0x50007c, 0x500078, 0x500074};

static struct attn_hw_reg nig_int4_bb_b0 = {
	4, 20, 0x500080, 0x50008c, 0x500088, 0x500084};

static struct attn_hw_reg nig_int5_bb_b0 = {
	5, 18, 0x500090, 0x50009c, 0x500098, 0x500094};

static struct attn_hw_reg *nig_int_bb_b0_regs[6] = {
	&nig_int0_bb_b0, &nig_int1_bb_b0, &nig_int2_bb_b0, &nig_int3_bb_b0,
	&nig_int4_bb_b0, &nig_int5_bb_b0};

static struct attn_hw_reg nig_prty0_bb_b0 = {
	0, 1, 0x5000a0, 0x5000ac, 0x5000a8, 0x5000a4};

static struct attn_hw_reg nig_prty1_bb_b0 = {
	1, 31, 0x500200, 0x50020c, 0x500208, 0x500204};

static struct attn_hw_reg nig_prty2_bb_b0 = {
	2, 31, 0x500210, 0x50021c, 0x500218, 0x500214};

static struct attn_hw_reg nig_prty3_bb_b0 = {
	3, 31, 0x500220, 0x50022c, 0x500228, 0x500224};

static struct attn_hw_reg nig_prty4_bb_b0 = {
	4, 17, 0x500230, 0x50023c, 0x500238, 0x500234};

static struct attn_hw_reg *nig_prty_bb_b0_regs[5] = {
	&nig_prty0_bb_b0, &nig_prty1_bb_b0, &nig_prty2_bb_b0,
	&nig_prty3_bb_b0, &nig_prty4_bb_b0};

static struct attn_hw_reg ipc_int0_bb_b0 = {
	0, 13, 0x2050c, 0x20518, 0x20514, 0x20510};

static struct attn_hw_reg *ipc_int_bb_b0_regs[1] = {
	&ipc_int0_bb_b0};

static struct attn_hw_reg ipc_prty0_bb_b0 = {
	0, 1, 0x2051c, 0x20528, 0x20524, 0x20520};

static struct attn_hw_reg *ipc_prty_bb_b0_regs[1] = {
	&ipc_prty0_bb_b0};

static struct attn_hw_block attn_blocks[] = {
	{"grc", {{1, 1, grc_int_bb_b0_regs, grc_prty_bb_b0_regs} } },
	{"miscs", {{2, 1, miscs_int_bb_b0_regs, miscs_prty_bb_b0_regs} } },
	{"misc", {{1, 0, misc_int_bb_b0_regs, NULL} } },
	{"dbu", {{0, 0, NULL, NULL} } },
	{"pglue_b", {{1, 2, pglue_b_int_bb_b0_regs,
		      pglue_b_prty_bb_b0_regs} } },
	{"cnig", {{1, 1, cnig_int_bb_b0_regs, cnig_prty_bb_b0_regs} } },
	{"cpmu", {{1, 0, cpmu_int_bb_b0_regs, NULL} } },
	{"ncsi", {{1, 1, ncsi_int_bb_b0_regs, ncsi_prty_bb_b0_regs} } },
	{"opte", {{0, 2, NULL, opte_prty_bb_b0_regs} } },
	{"bmb", {{12, 3, bmb_int_bb_b0_regs, bmb_prty_bb_b0_regs} } },
	{"pcie", {{0, 1, NULL, pcie_prty_bb_b0_regs} } },
	{"mcp", {{0, 0, NULL, NULL} } },
	{"mcp2", {{0, 2, NULL, mcp2_prty_bb_b0_regs} } },
	{"pswhst", {{1, 2, pswhst_int_bb_b0_regs, pswhst_prty_bb_b0_regs} } },
	{"pswhst2", {{1, 1, pswhst2_int_bb_b0_regs,
		      pswhst2_prty_bb_b0_regs} } },
	{"pswrd", {{1, 1, pswrd_int_bb_b0_regs, pswrd_prty_bb_b0_regs} } },
	{"pswrd2", {{1, 3, pswrd2_int_bb_b0_regs, pswrd2_prty_bb_b0_regs} } },
	{"pswwr", {{1, 1, pswwr_int_bb_b0_regs, pswwr_prty_bb_b0_regs} } },
	{"pswwr2", {{1, 5, pswwr2_int_bb_b0_regs, pswwr2_prty_bb_b0_regs} } },
	{"pswrq", {{1, 1, pswrq_int_bb_b0_regs, pswrq_prty_bb_b0_regs} } },
	{"pswrq2", {{1, 1, pswrq2_int_bb_b0_regs, pswrq2_prty_bb_b0_regs} } },
	{"pglcs", {{1, 0, pglcs_int_bb_b0_regs, NULL} } },
	{"dmae", {{1, 1, dmae_int_bb_b0_regs, dmae_prty_bb_b0_regs} } },
	{"ptu", {{1, 1, ptu_int_bb_b0_regs, ptu_prty_bb_b0_regs} } },
	{"tcm", {{3, 2, tcm_int_bb_b0_regs, tcm_prty_bb_b0_regs} } },
	{"mcm", {{3, 2, mcm_int_bb_b0_regs, mcm_prty_bb_b0_regs} } },
	{"ucm", {{3, 2, ucm_int_bb_b0_regs, ucm_prty_bb_b0_regs} } },
	{"xcm", {{3, 2, xcm_int_bb_b0_regs, xcm_prty_bb_b0_regs} } },
	{"ycm", {{3, 2, ycm_int_bb_b0_regs, ycm_prty_bb_b0_regs} } },
	{"pcm", {{3, 1, pcm_int_bb_b0_regs, pcm_prty_bb_b0_regs} } },
	{"qm", {{1, 4, qm_int_bb_b0_regs, qm_prty_bb_b0_regs} } },
	{"tm", {{2, 1, tm_int_bb_b0_regs, tm_prty_bb_b0_regs} } },
	{"dorq", {{1, 2, dorq_int_bb_b0_regs, dorq_prty_bb_b0_regs} } },
	{"brb", {{12, 3, brb_int_bb_b0_regs, brb_prty_bb_b0_regs} } },
	{"src", {{1, 0, src_int_bb_b0_regs, NULL} } },
	{"prs", {{1, 3, prs_int_bb_b0_regs, prs_prty_bb_b0_regs} } },
	{"tsdm", {{1, 1, tsdm_int_bb_b0_regs, tsdm_prty_bb_b0_regs} } },
	{"msdm", {{1, 1, msdm_int_bb_b0_regs, msdm_prty_bb_b0_regs} } },
	{"usdm", {{1, 1, usdm_int_bb_b0_regs, usdm_prty_bb_b0_regs} } },
	{"xsdm", {{1, 1, xsdm_int_bb_b0_regs, xsdm_prty_bb_b0_regs} } },
	{"ysdm", {{1, 1, ysdm_int_bb_b0_regs, ysdm_prty_bb_b0_regs} } },
	{"psdm", {{1, 1, psdm_int_bb_b0_regs, psdm_prty_bb_b0_regs} } },
	{"tsem", {{3, 3, tsem_int_bb_b0_regs, tsem_prty_bb_b0_regs} } },
	{"msem", {{3, 2, msem_int_bb_b0_regs, msem_prty_bb_b0_regs} } },
	{"usem", {{3, 2, usem_int_bb_b0_regs, usem_prty_bb_b0_regs} } },
	{"xsem", {{3, 2, xsem_int_bb_b0_regs, xsem_prty_bb_b0_regs} } },
	{"ysem", {{3, 2, ysem_int_bb_b0_regs, ysem_prty_bb_b0_regs} } },
	{"psem", {{3, 3, psem_int_bb_b0_regs, psem_prty_bb_b0_regs} } },
	{"rss", {{1, 1, rss_int_bb_b0_regs, rss_prty_bb_b0_regs} } },
	{"tmld", {{1, 1, tmld_int_bb_b0_regs, tmld_prty_bb_b0_regs} } },
	{"muld", {{1, 1, muld_int_bb_b0_regs, muld_prty_bb_b0_regs} } },
	{"yuld", {{1, 1, yuld_int_bb_b0_regs, yuld_prty_bb_b0_regs} } },
	{"xyld", {{1, 1, xyld_int_bb_b0_regs, xyld_prty_bb_b0_regs} } },
	{"prm", {{1, 2, prm_int_bb_b0_regs, prm_prty_bb_b0_regs} } },
	{"pbf_pb1", {{1, 1, pbf_pb1_int_bb_b0_regs,
		      pbf_pb1_prty_bb_b0_regs} } },
	{"pbf_pb2", {{1, 1, pbf_pb2_int_bb_b0_regs,
		      pbf_pb2_prty_bb_b0_regs} } },
	{"rpb", { {1, 1, rpb_int_bb_b0_regs, rpb_prty_bb_b0_regs} } },
	{"btb", { {11, 2, btb_int_bb_b0_regs, btb_prty_bb_b0_regs} } },
	{"pbf", { {1, 3, pbf_int_bb_b0_regs, pbf_prty_bb_b0_regs} } },
	{"rdif", { {1, 1, rdif_int_bb_b0_regs, rdif_prty_bb_b0_regs} } },
	{"tdif", { {1, 2, tdif_int_bb_b0_regs, tdif_prty_bb_b0_regs} } },
	{"cdu", { {1, 1, cdu_int_bb_b0_regs, cdu_prty_bb_b0_regs} } },
	{"ccfc", { {1, 2, ccfc_int_bb_b0_regs, ccfc_prty_bb_b0_regs} } },
	{"tcfc", { {1, 2, tcfc_int_bb_b0_regs, tcfc_prty_bb_b0_regs} } },
	{"igu", { {1, 3, igu_int_bb_b0_regs, igu_prty_bb_b0_regs} } },
	{"cau", { {1, 1, cau_int_bb_b0_regs, cau_prty_bb_b0_regs} } },
	{"umac", { {0, 0, NULL, NULL} } },
	{"xmac", { {0, 0, NULL, NULL} } },
	{"dbg", { {1, 1, dbg_int_bb_b0_regs, dbg_prty_bb_b0_regs} } },
	{"nig", { {6, 5, nig_int_bb_b0_regs, nig_prty_bb_b0_regs} } },
	{"wol", { {0, 0, NULL, NULL} } },
	{"bmbn", { {0, 0, NULL, NULL} } },
	{"ipc", { {1, 1, ipc_int_bb_b0_regs, ipc_prty_bb_b0_regs} } },
	{"nwm", { {0, 0, NULL, NULL} } },
	{"nws", { {0, 0, NULL, NULL} } },
	{"ms", { {0, 0, NULL, NULL} } },
	{"phy_pcie", { {0, 0, NULL, NULL} } },
	{"misc_aeu", { {0, 0, NULL, NULL} } },
	{"bar0_map", { {0, 0, NULL, NULL} } },};

/* Specific HW attention callbacks */
static int qed_mcp_attn_cb(struct qed_hwfn *p_hwfn)
{
	u32 tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, MCP_REG_CPU_STATE);

	/* This might occur on certain instances; Log it once then mask it */
	DP_INFO(p_hwfn->cdev, "MCP_REG_CPU_STATE: %08x - Masking...\n",
		tmp);
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt, MCP_REG_CPU_EVENT_MASK,
	       0xffffffff);

	return 0;
}

#define QED_PSWHST_ATTENTION_INCORRECT_ACCESS		(0x1)
#define ATTENTION_INCORRECT_ACCESS_WR_MASK		(0x1)
#define ATTENTION_INCORRECT_ACCESS_WR_SHIFT		(0)
#define ATTENTION_INCORRECT_ACCESS_CLIENT_MASK		(0xf)
#define ATTENTION_INCORRECT_ACCESS_CLIENT_SHIFT		(1)
#define ATTENTION_INCORRECT_ACCESS_VF_VALID_MASK	(0x1)
#define ATTENTION_INCORRECT_ACCESS_VF_VALID_SHIFT	(5)
#define ATTENTION_INCORRECT_ACCESS_VF_ID_MASK		(0xff)
#define ATTENTION_INCORRECT_ACCESS_VF_ID_SHIFT		(6)
#define ATTENTION_INCORRECT_ACCESS_PF_ID_MASK		(0xf)
#define ATTENTION_INCORRECT_ACCESS_PF_ID_SHIFT		(14)
#define ATTENTION_INCORRECT_ACCESS_BYTE_EN_MASK		(0xff)
#define ATTENTION_INCORRECT_ACCESS_BYTE_EN_SHIFT	(18)
static int qed_pswhst_attn_cb(struct qed_hwfn *p_hwfn)
{
	u32 tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			 PSWHST_REG_INCORRECT_ACCESS_VALID);

	if (tmp & QED_PSWHST_ATTENTION_INCORRECT_ACCESS) {
		u32 addr, data, length;

		addr = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			      PSWHST_REG_INCORRECT_ACCESS_ADDRESS);
		data = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			      PSWHST_REG_INCORRECT_ACCESS_DATA);
		length = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				PSWHST_REG_INCORRECT_ACCESS_LENGTH);

		DP_INFO(p_hwfn->cdev,
			"Incorrect access to %08x of length %08x - PF [%02x] VF [%04x] [valid %02x] client [%02x] write [%02x] Byte-Enable [%04x] [%08x]\n",
			addr, length,
			(u8) GET_FIELD(data, ATTENTION_INCORRECT_ACCESS_PF_ID),
			(u8) GET_FIELD(data, ATTENTION_INCORRECT_ACCESS_VF_ID),
			(u8) GET_FIELD(data,
				       ATTENTION_INCORRECT_ACCESS_VF_VALID),
			(u8) GET_FIELD(data,
				       ATTENTION_INCORRECT_ACCESS_CLIENT),
			(u8) GET_FIELD(data, ATTENTION_INCORRECT_ACCESS_WR),
			(u8) GET_FIELD(data,
				       ATTENTION_INCORRECT_ACCESS_BYTE_EN),
			data);
	}

	return 0;
}

#define QED_GRC_ATTENTION_VALID_BIT	(1 << 0)
#define QED_GRC_ATTENTION_ADDRESS_MASK	(0x7fffff)
#define QED_GRC_ATTENTION_ADDRESS_SHIFT	(0)
#define QED_GRC_ATTENTION_RDWR_BIT	(1 << 23)
#define QED_GRC_ATTENTION_MASTER_MASK	(0xf)
#define QED_GRC_ATTENTION_MASTER_SHIFT	(24)
#define QED_GRC_ATTENTION_PF_MASK	(0xf)
#define QED_GRC_ATTENTION_PF_SHIFT	(0)
#define QED_GRC_ATTENTION_VF_MASK	(0xff)
#define QED_GRC_ATTENTION_VF_SHIFT	(4)
#define QED_GRC_ATTENTION_PRIV_MASK	(0x3)
#define QED_GRC_ATTENTION_PRIV_SHIFT	(14)
#define QED_GRC_ATTENTION_PRIV_VF	(0)
static const char *attn_master_to_str(u8 master)
{
	switch (master) {
	case 1: return "PXP";
	case 2: return "MCP";
	case 3: return "MSDM";
	case 4: return "PSDM";
	case 5: return "YSDM";
	case 6: return "USDM";
	case 7: return "TSDM";
	case 8: return "XSDM";
	case 9: return "DBU";
	case 10: return "DMAE";
	default:
		return "Unkown";
	}
}

static int qed_grc_attn_cb(struct qed_hwfn *p_hwfn)
{
	u32 tmp, tmp2;

	/* We've already cleared the timeout interrupt register, so we learn
	 * of interrupts via the validity register
	 */
	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     GRC_REG_TIMEOUT_ATTN_ACCESS_VALID);
	if (!(tmp & QED_GRC_ATTENTION_VALID_BIT))
		goto out;

	/* Read the GRC timeout information */
	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     GRC_REG_TIMEOUT_ATTN_ACCESS_DATA_0);
	tmp2 = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		      GRC_REG_TIMEOUT_ATTN_ACCESS_DATA_1);

	DP_INFO(p_hwfn->cdev,
		"GRC timeout [%08x:%08x] - %s Address [%08x] [Master %s] [PF: %02x %s %02x]\n",
		tmp2, tmp,
		(tmp & QED_GRC_ATTENTION_RDWR_BIT) ? "Write to" : "Read from",
		GET_FIELD(tmp, QED_GRC_ATTENTION_ADDRESS) << 2,
		attn_master_to_str(GET_FIELD(tmp, QED_GRC_ATTENTION_MASTER)),
		GET_FIELD(tmp2, QED_GRC_ATTENTION_PF),
		(GET_FIELD(tmp2, QED_GRC_ATTENTION_PRIV) ==
		 QED_GRC_ATTENTION_PRIV_VF) ? "VF" : "(Ireelevant)",
		GET_FIELD(tmp2, QED_GRC_ATTENTION_VF));

out:
	/* Regardles of anything else, clean the validity bit */
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt,
	       GRC_REG_TIMEOUT_ATTN_ACCESS_VALID, 0);
	return 0;
}

#define PGLUE_ATTENTION_VALID			(1 << 29)
#define PGLUE_ATTENTION_RD_VALID		(1 << 26)
#define PGLUE_ATTENTION_DETAILS_PFID_MASK	(0xf)
#define PGLUE_ATTENTION_DETAILS_PFID_SHIFT	(20)
#define PGLUE_ATTENTION_DETAILS_VF_VALID_MASK	(0x1)
#define PGLUE_ATTENTION_DETAILS_VF_VALID_SHIFT	(19)
#define PGLUE_ATTENTION_DETAILS_VFID_MASK	(0xff)
#define PGLUE_ATTENTION_DETAILS_VFID_SHIFT	(24)
#define PGLUE_ATTENTION_DETAILS2_WAS_ERR_MASK	(0x1)
#define PGLUE_ATTENTION_DETAILS2_WAS_ERR_SHIFT	(21)
#define PGLUE_ATTENTION_DETAILS2_BME_MASK	(0x1)
#define PGLUE_ATTENTION_DETAILS2_BME_SHIFT	(22)
#define PGLUE_ATTENTION_DETAILS2_FID_EN_MASK	(0x1)
#define PGLUE_ATTENTION_DETAILS2_FID_EN_SHIFT	(23)
#define PGLUE_ATTENTION_ICPL_VALID		(1 << 23)
#define PGLUE_ATTENTION_ZLR_VALID		(1 << 25)
#define PGLUE_ATTENTION_ILT_VALID		(1 << 23)
static int qed_pglub_rbc_attn_cb(struct qed_hwfn *p_hwfn)
{
	u32 tmp;

	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     PGLUE_B_REG_TX_ERR_WR_DETAILS2);
	if (tmp & PGLUE_ATTENTION_VALID) {
		u32 addr_lo, addr_hi, details;

		addr_lo = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_TX_ERR_WR_ADD_31_0);
		addr_hi = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_TX_ERR_WR_ADD_63_32);
		details = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_TX_ERR_WR_DETAILS);

		DP_INFO(p_hwfn,
			"Illegal write by chip to [%08x:%08x] blocked.\n"
			"Details: %08x [PFID %02x, VFID %02x, VF_VALID %02x]\n"
			"Details2 %08x [Was_error %02x BME deassert %02x FID_enable deassert %02x]\n",
			addr_hi, addr_lo, details,
			(u8)GET_FIELD(details, PGLUE_ATTENTION_DETAILS_PFID),
			(u8)GET_FIELD(details, PGLUE_ATTENTION_DETAILS_VFID),
			GET_FIELD(details,
				  PGLUE_ATTENTION_DETAILS_VF_VALID) ? 1 : 0,
			tmp,
			GET_FIELD(tmp,
				  PGLUE_ATTENTION_DETAILS2_WAS_ERR) ? 1 : 0,
			GET_FIELD(tmp,
				  PGLUE_ATTENTION_DETAILS2_BME) ? 1 : 0,
			GET_FIELD(tmp,
				  PGLUE_ATTENTION_DETAILS2_FID_EN) ? 1 : 0);
	}

	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     PGLUE_B_REG_TX_ERR_RD_DETAILS2);
	if (tmp & PGLUE_ATTENTION_RD_VALID) {
		u32 addr_lo, addr_hi, details;

		addr_lo = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_TX_ERR_RD_ADD_31_0);
		addr_hi = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_TX_ERR_RD_ADD_63_32);
		details = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_TX_ERR_RD_DETAILS);

		DP_INFO(p_hwfn,
			"Illegal read by chip from [%08x:%08x] blocked.\n"
			" Details: %08x [PFID %02x, VFID %02x, VF_VALID %02x]\n"
			" Details2 %08x [Was_error %02x BME deassert %02x FID_enable deassert %02x]\n",
			addr_hi, addr_lo, details,
			(u8)GET_FIELD(details, PGLUE_ATTENTION_DETAILS_PFID),
			(u8)GET_FIELD(details, PGLUE_ATTENTION_DETAILS_VFID),
			GET_FIELD(details,
				  PGLUE_ATTENTION_DETAILS_VF_VALID) ? 1 : 0,
			tmp,
			GET_FIELD(tmp, PGLUE_ATTENTION_DETAILS2_WAS_ERR) ? 1
									 : 0,
			GET_FIELD(tmp, PGLUE_ATTENTION_DETAILS2_BME) ? 1 : 0,
			GET_FIELD(tmp, PGLUE_ATTENTION_DETAILS2_FID_EN) ? 1
									: 0);
	}

	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     PGLUE_B_REG_TX_ERR_WR_DETAILS_ICPL);
	if (tmp & PGLUE_ATTENTION_ICPL_VALID)
		DP_INFO(p_hwfn, "ICPL eror - %08x\n", tmp);

	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     PGLUE_B_REG_MASTER_ZLR_ERR_DETAILS);
	if (tmp & PGLUE_ATTENTION_ZLR_VALID) {
		u32 addr_hi, addr_lo;

		addr_lo = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_MASTER_ZLR_ERR_ADD_31_0);
		addr_hi = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_MASTER_ZLR_ERR_ADD_63_32);

		DP_INFO(p_hwfn, "ZLR eror - %08x [Address %08x:%08x]\n",
			tmp, addr_hi, addr_lo);
	}

	tmp = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		     PGLUE_B_REG_VF_ILT_ERR_DETAILS2);
	if (tmp & PGLUE_ATTENTION_ILT_VALID) {
		u32 addr_hi, addr_lo, details;

		addr_lo = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_VF_ILT_ERR_ADD_31_0);
		addr_hi = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_VF_ILT_ERR_ADD_63_32);
		details = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				 PGLUE_B_REG_VF_ILT_ERR_DETAILS);

		DP_INFO(p_hwfn,
			"ILT error - Details %08x Details2 %08x [Address %08x:%08x]\n",
			details, tmp, addr_hi, addr_lo);
	}

	/* Clear the indications */
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt,
	       PGLUE_B_REG_LATCHED_ERRORS_CLR, (1 << 2));

	return 0;
}

#define QED_DORQ_ATTENTION_REASON_MASK	(0xfffff)
#define QED_DORQ_ATTENTION_OPAQUE_MASK (0xffff)
#define QED_DORQ_ATTENTION_SIZE_MASK	(0x7f)
#define QED_DORQ_ATTENTION_SIZE_SHIFT	(16)
static int qed_dorq_attn_cb(struct qed_hwfn *p_hwfn)
{
	u32 reason;

	reason = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, DORQ_REG_DB_DROP_REASON) &
			QED_DORQ_ATTENTION_REASON_MASK;
	if (reason) {
		u32 details = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				     DORQ_REG_DB_DROP_DETAILS);

		DP_INFO(p_hwfn->cdev,
			"DORQ db_drop: adress 0x%08x Opaque FID 0x%04x Size [bytes] 0x%08x Reason: 0x%08x\n",
			qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			       DORQ_REG_DB_DROP_DETAILS_ADDRESS),
			(u16)(details & QED_DORQ_ATTENTION_OPAQUE_MASK),
			GET_FIELD(details, QED_DORQ_ATTENTION_SIZE) * 4,
			reason);
	}

	return -EINVAL;
}

/* Notice aeu_invert_reg must be defined in the same order of bits as HW;  */
static struct aeu_invert_reg aeu_descs[NUM_ATTN_REGS] = {
	{
		{       /* After Invert 1 */
			{"GPIO0 function%d",
			 (32 << ATTENTION_LENGTH_SHIFT), NULL, MAX_BLOCK_ID},
		}
	},

	{
		{       /* After Invert 2 */
			{"PGLUE config_space", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"PGLUE misc_flr", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"PGLUE B RBC", ATTENTION_PAR_INT,
			 qed_pglub_rbc_attn_cb, BLOCK_PGLUE_B},
			{"PGLUE misc_mctp", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"Flash event", ATTENTION_SINGLE, NULL, MAX_BLOCK_ID},
			{"SMB event", ATTENTION_SINGLE,	NULL, MAX_BLOCK_ID},
			{"Main Power", ATTENTION_SINGLE, NULL, MAX_BLOCK_ID},
			{"SW timers #%d", (8 << ATTENTION_LENGTH_SHIFT) |
					  (1 << ATTENTION_OFFSET_SHIFT),
			 NULL, MAX_BLOCK_ID},
			{"PCIE glue/PXP VPD %d",
			 (16 << ATTENTION_LENGTH_SHIFT), NULL, BLOCK_PGLCS},
		}
	},

	{
		{       /* After Invert 3 */
			{"General Attention %d",
			 (32 << ATTENTION_LENGTH_SHIFT), NULL, MAX_BLOCK_ID},
		}
	},

	{
		{       /* After Invert 4 */
			{"General Attention 32", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"General Attention %d",
			 (2 << ATTENTION_LENGTH_SHIFT) |
			 (33 << ATTENTION_OFFSET_SHIFT), NULL, MAX_BLOCK_ID},
			{"General Attention 35", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"CNIG port %d", (4 << ATTENTION_LENGTH_SHIFT),
			 NULL, BLOCK_CNIG},
			{"MCP CPU", ATTENTION_SINGLE,
			 qed_mcp_attn_cb, MAX_BLOCK_ID},
			{"MCP Watchdog timer", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"MCP M2P", ATTENTION_SINGLE, NULL, MAX_BLOCK_ID},
			{"AVS stop status ready", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"MSTAT", ATTENTION_PAR_INT, NULL, MAX_BLOCK_ID},
			{"MSTAT per-path", ATTENTION_PAR_INT,
			 NULL, MAX_BLOCK_ID},
			{"Reserved %d", (6 << ATTENTION_LENGTH_SHIFT),
			 NULL, MAX_BLOCK_ID},
			{"NIG", ATTENTION_PAR_INT, NULL, BLOCK_NIG},
			{"BMB/OPTE/MCP", ATTENTION_PAR_INT, NULL, BLOCK_BMB},
			{"BTB",	ATTENTION_PAR_INT, NULL, BLOCK_BTB},
			{"BRB",	ATTENTION_PAR_INT, NULL, BLOCK_BRB},
			{"PRS",	ATTENTION_PAR_INT, NULL, BLOCK_PRS},
		}
	},

	{
		{       /* After Invert 5 */
			{"SRC", ATTENTION_PAR_INT, NULL, BLOCK_SRC},
			{"PB Client1", ATTENTION_PAR_INT, NULL, BLOCK_PBF_PB1},
			{"PB Client2", ATTENTION_PAR_INT, NULL, BLOCK_PBF_PB2},
			{"RPB", ATTENTION_PAR_INT, NULL, BLOCK_RPB},
			{"PBF", ATTENTION_PAR_INT, NULL, BLOCK_PBF},
			{"QM", ATTENTION_PAR_INT, NULL, BLOCK_QM},
			{"TM", ATTENTION_PAR_INT, NULL, BLOCK_TM},
			{"MCM",  ATTENTION_PAR_INT, NULL, BLOCK_MCM},
			{"MSDM", ATTENTION_PAR_INT, NULL, BLOCK_MSDM},
			{"MSEM", ATTENTION_PAR_INT, NULL, BLOCK_MSEM},
			{"PCM", ATTENTION_PAR_INT, NULL, BLOCK_PCM},
			{"PSDM", ATTENTION_PAR_INT, NULL, BLOCK_PSDM},
			{"PSEM", ATTENTION_PAR_INT, NULL, BLOCK_PSEM},
			{"TCM", ATTENTION_PAR_INT, NULL, BLOCK_TCM},
			{"TSDM", ATTENTION_PAR_INT, NULL, BLOCK_TSDM},
			{"TSEM", ATTENTION_PAR_INT, NULL, BLOCK_TSEM},
		}
	},

	{
		{       /* After Invert 6 */
			{"UCM", ATTENTION_PAR_INT, NULL, BLOCK_UCM},
			{"USDM", ATTENTION_PAR_INT, NULL, BLOCK_USDM},
			{"USEM", ATTENTION_PAR_INT, NULL, BLOCK_USEM},
			{"XCM",	ATTENTION_PAR_INT, NULL, BLOCK_XCM},
			{"XSDM", ATTENTION_PAR_INT, NULL, BLOCK_XSDM},
			{"XSEM", ATTENTION_PAR_INT, NULL, BLOCK_XSEM},
			{"YCM",	ATTENTION_PAR_INT, NULL, BLOCK_YCM},
			{"YSDM", ATTENTION_PAR_INT, NULL, BLOCK_YSDM},
			{"YSEM", ATTENTION_PAR_INT, NULL, BLOCK_YSEM},
			{"XYLD", ATTENTION_PAR_INT, NULL, BLOCK_XYLD},
			{"TMLD", ATTENTION_PAR_INT, NULL, BLOCK_TMLD},
			{"MYLD", ATTENTION_PAR_INT, NULL, BLOCK_MULD},
			{"YULD", ATTENTION_PAR_INT, NULL, BLOCK_YULD},
			{"DORQ", ATTENTION_PAR_INT,
			 qed_dorq_attn_cb, BLOCK_DORQ},
			{"DBG", ATTENTION_PAR_INT, NULL, BLOCK_DBG},
			{"IPC",	ATTENTION_PAR_INT, NULL, BLOCK_IPC},
		}
	},

	{
		{       /* After Invert 7 */
			{"CCFC", ATTENTION_PAR_INT, NULL, BLOCK_CCFC},
			{"CDU", ATTENTION_PAR_INT, NULL, BLOCK_CDU},
			{"DMAE", ATTENTION_PAR_INT, NULL, BLOCK_DMAE},
			{"IGU", ATTENTION_PAR_INT, NULL, BLOCK_IGU},
			{"ATC", ATTENTION_PAR_INT, NULL, MAX_BLOCK_ID},
			{"CAU", ATTENTION_PAR_INT, NULL, BLOCK_CAU},
			{"PTU", ATTENTION_PAR_INT, NULL, BLOCK_PTU},
			{"PRM", ATTENTION_PAR_INT, NULL, BLOCK_PRM},
			{"TCFC", ATTENTION_PAR_INT, NULL, BLOCK_TCFC},
			{"RDIF", ATTENTION_PAR_INT, NULL, BLOCK_RDIF},
			{"TDIF", ATTENTION_PAR_INT, NULL, BLOCK_TDIF},
			{"RSS", ATTENTION_PAR_INT, NULL, BLOCK_RSS},
			{"MISC", ATTENTION_PAR_INT, NULL, BLOCK_MISC},
			{"MISCS", ATTENTION_PAR_INT, NULL, BLOCK_MISCS},
			{"PCIE", ATTENTION_PAR, NULL, BLOCK_PCIE},
			{"Vaux PCI core", ATTENTION_SINGLE, NULL, BLOCK_PGLCS},
			{"PSWRQ", ATTENTION_PAR_INT, NULL, BLOCK_PSWRQ},
		}
	},

	{
		{       /* After Invert 8 */
			{"PSWRQ (pci_clk)", ATTENTION_PAR_INT,
			 NULL, BLOCK_PSWRQ2},
			{"PSWWR", ATTENTION_PAR_INT, NULL, BLOCK_PSWWR},
			{"PSWWR (pci_clk)", ATTENTION_PAR_INT,
			 NULL, BLOCK_PSWWR2},
			{"PSWRD", ATTENTION_PAR_INT, NULL, BLOCK_PSWRD},
			{"PSWRD (pci_clk)", ATTENTION_PAR_INT,
			 NULL, BLOCK_PSWRD2},
			{"PSWHST", ATTENTION_PAR_INT,
			 qed_pswhst_attn_cb, BLOCK_PSWHST},
			{"PSWHST (pci_clk)", ATTENTION_PAR_INT,
			 NULL, BLOCK_PSWHST2},
			{"GRC",	ATTENTION_PAR_INT,
			 qed_grc_attn_cb, BLOCK_GRC},
			{"CPMU", ATTENTION_PAR_INT, NULL, BLOCK_CPMU},
			{"NCSI", ATTENTION_PAR_INT, NULL, BLOCK_NCSI},
			{"MSEM PRAM", ATTENTION_PAR, NULL, MAX_BLOCK_ID},
			{"PSEM PRAM", ATTENTION_PAR, NULL, MAX_BLOCK_ID},
			{"TSEM PRAM", ATTENTION_PAR, NULL, MAX_BLOCK_ID},
			{"USEM PRAM", ATTENTION_PAR, NULL, MAX_BLOCK_ID},
			{"XSEM PRAM", ATTENTION_PAR, NULL, MAX_BLOCK_ID},
			{"YSEM PRAM", ATTENTION_PAR, NULL, MAX_BLOCK_ID},
			{"pxp_misc_mps", ATTENTION_PAR, NULL, BLOCK_PGLCS},
			{"PCIE glue/PXP Exp. ROM", ATTENTION_SINGLE,
			 NULL, BLOCK_PGLCS},
			{"PERST_B assertion", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"PERST_B deassertion", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"Reserved %d", (2 << ATTENTION_LENGTH_SHIFT),
			 NULL, MAX_BLOCK_ID},
		}
	},

	{
		{       /* After Invert 9 */
			{"MCP Latched memory", ATTENTION_PAR,
			 NULL, MAX_BLOCK_ID},
			{"MCP Latched scratchpad cache", ATTENTION_SINGLE,
			 NULL, MAX_BLOCK_ID},
			{"MCP Latched ump_tx", ATTENTION_PAR,
			 NULL, MAX_BLOCK_ID},
			{"MCP Latched scratchpad", ATTENTION_PAR,
			 NULL, MAX_BLOCK_ID},
			{"Reserved %d", (28 << ATTENTION_LENGTH_SHIFT),
			 NULL, MAX_BLOCK_ID},
		}
	},
};

#define ATTN_STATE_BITS         (0xfff)
#define ATTN_BITS_MASKABLE      (0x3ff)
struct qed_sb_attn_info {
	/* Virtual & Physical address of the SB */
	struct atten_status_block       *sb_attn;
	dma_addr_t			sb_phys;

	/* Last seen running index */
	u16				index;

	/* A mask of the AEU bits resulting in a parity error */
	u32				parity_mask[NUM_ATTN_REGS];

	/* A pointer to the attention description structure */
	struct aeu_invert_reg		*p_aeu_desc;

	/* Previously asserted attentions, which are still unasserted */
	u16				known_attn;

	/* Cleanup address for the link's general hw attention */
	u32				mfw_attn_addr;
};

static inline u16 qed_attn_update_idx(struct qed_hwfn *p_hwfn,
				      struct qed_sb_attn_info   *p_sb_desc)
{
	u16     rc = 0;
	u16     index;

	/* Make certain HW write took affect */
	mmiowb();

	index = le16_to_cpu(p_sb_desc->sb_attn->sb_index);
	if (p_sb_desc->index != index) {
		p_sb_desc->index	= index;
		rc		      = QED_SB_ATT_IDX;
	}

	/* Make certain we got a consistent view with HW */
	mmiowb();

	return rc;
}

/**
 *  @brief qed_int_assertion - handles asserted attention bits
 *
 *  @param p_hwfn
 *  @param asserted_bits newly asserted bits
 *  @return int
 */
static int qed_int_assertion(struct qed_hwfn *p_hwfn,
			     u16 asserted_bits)
{
	struct qed_sb_attn_info *sb_attn_sw = p_hwfn->p_sb_attn;
	u32 igu_mask;

	/* Mask the source of the attention in the IGU */
	igu_mask = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			  IGU_REG_ATTENTION_ENABLE);
	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR, "IGU mask: 0x%08x --> 0x%08x\n",
		   igu_mask, igu_mask & ~(asserted_bits & ATTN_BITS_MASKABLE));
	igu_mask &= ~(asserted_bits & ATTN_BITS_MASKABLE);
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt, IGU_REG_ATTENTION_ENABLE, igu_mask);

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
		   "inner known ATTN state: 0x%04x --> 0x%04x\n",
		   sb_attn_sw->known_attn,
		   sb_attn_sw->known_attn | asserted_bits);
	sb_attn_sw->known_attn |= asserted_bits;

	/* Handle MCP events */
	if (asserted_bits & 0x100) {
		qed_mcp_handle_events(p_hwfn, p_hwfn->p_dpc_ptt);
		/* Clean the MCP attention */
		qed_wr(p_hwfn, p_hwfn->p_dpc_ptt,
		       sb_attn_sw->mfw_attn_addr, 0);
	}

	DIRECT_REG_WR((u8 __iomem *)p_hwfn->regview +
		      GTT_BAR0_MAP_REG_IGU_CMD +
		      ((IGU_CMD_ATTN_BIT_SET_UPPER -
			IGU_CMD_INT_ACK_BASE) << 3),
		      (u32)asserted_bits);

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR, "set cmd IGU: 0x%04x\n",
		   asserted_bits);

	return 0;
}

static void qed_int_deassertion_print_bit(struct qed_hwfn *p_hwfn,
					  struct attn_hw_reg *p_reg_desc,
					  struct attn_hw_block *p_block,
					  enum qed_attention_type type,
					  u32 val, u32 mask)
{
	int j;

	for (j = 0; j < p_reg_desc->num_of_bits; j++) {
		if (!(val & (1 << j)))
			continue;

		DP_NOTICE(p_hwfn,
			  "%s (%s): reg %d [0x%08x], bit %d [%s]\n",
			  p_block->name,
			  type == QED_ATTN_TYPE_ATTN ? "Interrupt" :
						       "Parity",
			  p_reg_desc->reg_idx, p_reg_desc->sts_addr,
			  j, (mask & (1 << j)) ? " [MASKED]" : "");
	}
}

/**
 * @brief qed_int_deassertion_aeu_bit - handles the effects of a single
 * cause of the attention
 *
 * @param p_hwfn
 * @param p_aeu - descriptor of an AEU bit which caused the attention
 * @param aeu_en_reg - register offset of the AEU enable reg. which configured
 *  this bit to this group.
 * @param bit_index - index of this bit in the aeu_en_reg
 *
 * @return int
 */
static int
qed_int_deassertion_aeu_bit(struct qed_hwfn *p_hwfn,
			    struct aeu_invert_reg_bit *p_aeu,
			    u32 aeu_en_reg,
			    u32 bitmask)
{
	int rc = -EINVAL;
	u32 val;

	DP_INFO(p_hwfn, "Deasserted attention `%s'[%08x]\n",
		p_aeu->bit_name, bitmask);

	/* Call callback before clearing the interrupt status */
	if (p_aeu->cb) {
		DP_INFO(p_hwfn, "`%s (attention)': Calling Callback function\n",
			p_aeu->bit_name);
		rc = p_aeu->cb(p_hwfn);
	}

	/* Handle HW block interrupt registers */
	if (p_aeu->block_index != MAX_BLOCK_ID) {
		struct attn_hw_block *p_block;
		u32 mask;
		int i;

		p_block = &attn_blocks[p_aeu->block_index];

		/* Handle each interrupt register */
		for (i = 0; i < p_block->chip_regs[0].num_of_int_regs; i++) {
			struct attn_hw_reg *p_reg_desc;
			u32 sts_addr;

			p_reg_desc = p_block->chip_regs[0].int_regs[i];

			/* In case of fatal attention, don't clear the status
			 * so it would appear in following idle check.
			 */
			if (rc == 0)
				sts_addr = p_reg_desc->sts_clr_addr;
			else
				sts_addr = p_reg_desc->sts_addr;

			val = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, sts_addr);
			mask = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				      p_reg_desc->mask_addr);
			qed_int_deassertion_print_bit(p_hwfn, p_reg_desc,
						      p_block,
						      QED_ATTN_TYPE_ATTN,
						      val, mask);
		}
	}

	/* If the attention is benign, no need to prevent it */
	if (!rc)
		goto out;

	/* Prevent this Attention from being asserted in the future */
	val = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en_reg);
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en_reg, (val & ~bitmask));
	DP_INFO(p_hwfn, "`%s' - Disabled future attentions\n",
		p_aeu->bit_name);

out:
	return rc;
}

static void qed_int_parity_print(struct qed_hwfn *p_hwfn,
				 struct aeu_invert_reg_bit *p_aeu,
				 struct attn_hw_block *p_block,
				 u8 bit_index)
{
	int i;

	for (i = 0; i < p_block->chip_regs[0].num_of_prty_regs; i++) {
		struct attn_hw_reg *p_reg_desc;
		u32 val, mask;

		p_reg_desc = p_block->chip_regs[0].prty_regs[i];

		val = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			     p_reg_desc->sts_clr_addr);
		mask = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			      p_reg_desc->mask_addr);
		qed_int_deassertion_print_bit(p_hwfn, p_reg_desc,
					      p_block,
					      QED_ATTN_TYPE_PARITY,
					      val, mask);
	}
}

/**
 * @brief qed_int_deassertion_parity - handle a single parity AEU source
 *
 * @param p_hwfn
 * @param p_aeu - descriptor of an AEU bit which caused the parity
 * @param bit_index
 */
static void qed_int_deassertion_parity(struct qed_hwfn *p_hwfn,
				       struct aeu_invert_reg_bit *p_aeu,
				       u8 bit_index)
{
	u32 block_id = p_aeu->block_index;

	DP_INFO(p_hwfn->cdev, "%s[%d] parity attention is set\n",
		p_aeu->bit_name, bit_index);

	if (block_id != MAX_BLOCK_ID) {
		qed_int_parity_print(p_hwfn, p_aeu, &attn_blocks[block_id],
				     bit_index);

		/* In BB, there's a single parity bit for several blocks */
		if (block_id == BLOCK_BTB) {
			qed_int_parity_print(p_hwfn, p_aeu,
					     &attn_blocks[BLOCK_OPTE],
					     bit_index);
			qed_int_parity_print(p_hwfn, p_aeu,
					     &attn_blocks[BLOCK_MCP],
					     bit_index);
		}
	}
}

/**
 * @brief - handles deassertion of previously asserted attentions.
 *
 * @param p_hwfn
 * @param deasserted_bits - newly deasserted bits
 * @return int
 *
 */
static int qed_int_deassertion(struct qed_hwfn  *p_hwfn,
			       u16 deasserted_bits)
{
	struct qed_sb_attn_info *sb_attn_sw = p_hwfn->p_sb_attn;
	u32 aeu_inv_arr[NUM_ATTN_REGS], aeu_mask;
	u8 i, j, k, bit_idx;
	int rc = 0;

	/* Read the attention registers in the AEU */
	for (i = 0; i < NUM_ATTN_REGS; i++) {
		aeu_inv_arr[i] = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
					MISC_REG_AEU_AFTER_INVERT_1_IGU +
					i * 0x4);
		DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
			   "Deasserted bits [%d]: %08x\n",
			   i, aeu_inv_arr[i]);
	}

	/* Find parity attentions first */
	for (i = 0; i < NUM_ATTN_REGS; i++) {
		struct aeu_invert_reg *p_aeu = &sb_attn_sw->p_aeu_desc[i];
		u32 en = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				MISC_REG_AEU_ENABLE1_IGU_OUT_0 +
				i * sizeof(u32));
		u32 parities;

		/* Skip register in which no parity bit is currently set */
		parities = sb_attn_sw->parity_mask[i] & aeu_inv_arr[i] & en;
		if (!parities)
			continue;

		for (j = 0, bit_idx = 0; bit_idx < 32; j++) {
			struct aeu_invert_reg_bit *p_bit = &p_aeu->bits[j];

			if ((p_bit->flags & ATTENTION_PARITY) &&
			    !!(parities & (1 << bit_idx)))
				qed_int_deassertion_parity(p_hwfn, p_bit,
							   bit_idx);

			bit_idx += ATTENTION_LENGTH(p_bit->flags);
		}
	}

	/* Find non-parity cause for attention and act */
	for (k = 0; k < MAX_ATTN_GRPS; k++) {
		struct aeu_invert_reg_bit *p_aeu;

		/* Handle only groups whose attention is currently deasserted */
		if (!(deasserted_bits & (1 << k)))
			continue;

		for (i = 0; i < NUM_ATTN_REGS; i++) {
			u32 aeu_en = MISC_REG_AEU_ENABLE1_IGU_OUT_0 +
				     i * sizeof(u32) +
				     k * sizeof(u32) * NUM_ATTN_REGS;
			u32 en, bits;

			en = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en);
			bits = aeu_inv_arr[i] & en;

			/* Skip if no bit from this group is currently set */
			if (!bits)
				continue;

			/* Find all set bits from current register which belong
			 * to current group, making them responsible for the
			 * previous assertion.
			 */
			for (j = 0, bit_idx = 0; bit_idx < 32; j++) {
				u8 bit, bit_len;
				u32 bitmask;

				p_aeu = &sb_attn_sw->p_aeu_desc[i].bits[j];

				/* No need to handle parity-only bits */
				if (p_aeu->flags == ATTENTION_PAR)
					continue;

				bit = bit_idx;
				bit_len = ATTENTION_LENGTH(p_aeu->flags);
				if (p_aeu->flags & ATTENTION_PAR_INT) {
					/* Skip Parity */
					bit++;
					bit_len--;
				}

				bitmask = bits & (((1 << bit_len) - 1) << bit);
				if (bitmask) {
					/* Handle source of the attention */
					qed_int_deassertion_aeu_bit(p_hwfn,
								    p_aeu,
								    aeu_en,
								    bitmask);
				}

				bit_idx += ATTENTION_LENGTH(p_aeu->flags);
			}
		}
	}

	/* Clear IGU indication for the deasserted bits */
	DIRECT_REG_WR((u8 __iomem *)p_hwfn->regview +
				    GTT_BAR0_MAP_REG_IGU_CMD +
				    ((IGU_CMD_ATTN_BIT_CLR_UPPER -
				      IGU_CMD_INT_ACK_BASE) << 3),
				    ~((u32)deasserted_bits));

	/* Unmask deasserted attentions in IGU */
	aeu_mask = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			  IGU_REG_ATTENTION_ENABLE);
	aeu_mask |= (deasserted_bits & ATTN_BITS_MASKABLE);
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt, IGU_REG_ATTENTION_ENABLE, aeu_mask);

	/* Clear deassertion from inner state */
	sb_attn_sw->known_attn &= ~deasserted_bits;

	return rc;
}

static int qed_int_attentions(struct qed_hwfn *p_hwfn)
{
	struct qed_sb_attn_info *p_sb_attn_sw = p_hwfn->p_sb_attn;
	struct atten_status_block *p_sb_attn = p_sb_attn_sw->sb_attn;
	u32 attn_bits = 0, attn_acks = 0;
	u16 asserted_bits, deasserted_bits;
	__le16 index;
	int rc = 0;

	/* Read current attention bits/acks - safeguard against attentions
	 * by guaranting work on a synchronized timeframe
	 */
	do {
		index = p_sb_attn->sb_index;
		attn_bits = le32_to_cpu(p_sb_attn->atten_bits);
		attn_acks = le32_to_cpu(p_sb_attn->atten_ack);
	} while (index != p_sb_attn->sb_index);
	p_sb_attn->sb_index = index;

	/* Attention / Deassertion are meaningful (and in correct state)
	 * only when they differ and consistent with known state - deassertion
	 * when previous attention & current ack, and assertion when current
	 * attention with no previous attention
	 */
	asserted_bits = (attn_bits & ~attn_acks & ATTN_STATE_BITS) &
		~p_sb_attn_sw->known_attn;
	deasserted_bits = (~attn_bits & attn_acks & ATTN_STATE_BITS) &
		p_sb_attn_sw->known_attn;

	if ((asserted_bits & ~0x100) || (deasserted_bits & ~0x100)) {
		DP_INFO(p_hwfn,
			"Attention: Index: 0x%04x, Bits: 0x%08x, Acks: 0x%08x, asserted: 0x%04x, De-asserted 0x%04x [Prev. known: 0x%04x]\n",
			index, attn_bits, attn_acks, asserted_bits,
			deasserted_bits, p_sb_attn_sw->known_attn);
	} else if (asserted_bits == 0x100) {
		DP_INFO(p_hwfn,
			"MFW indication via attention\n");
	} else {
		DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
			   "MFW indication [deassertion]\n");
	}

	if (asserted_bits) {
		rc = qed_int_assertion(p_hwfn, asserted_bits);
		if (rc)
			return rc;
	}

	if (deasserted_bits) {
		rc = qed_int_deassertion(p_hwfn, deasserted_bits);
		if (rc)
			return rc;
	}

	return rc;
}

static void qed_sb_ack_attn(struct qed_hwfn *p_hwfn,
			    void __iomem *igu_addr,
			    u32 ack_cons)
{
	struct igu_prod_cons_update igu_ack = { 0 };

	igu_ack.sb_id_and_flags =
		((ack_cons << IGU_PROD_CONS_UPDATE_SB_INDEX_SHIFT) |
		 (1 << IGU_PROD_CONS_UPDATE_UPDATE_FLAG_SHIFT) |
		 (IGU_INT_NOP << IGU_PROD_CONS_UPDATE_ENABLE_INT_SHIFT) |
		 (IGU_SEG_ACCESS_ATTN <<
		  IGU_PROD_CONS_UPDATE_SEGMENT_ACCESS_SHIFT));

	DIRECT_REG_WR(igu_addr, igu_ack.sb_id_and_flags);

	/* Both segments (interrupts & acks) are written to same place address;
	 * Need to guarantee all commands will be received (in-order) by HW.
	 */
	mmiowb();
	barrier();
}

void qed_int_sp_dpc(unsigned long hwfn_cookie)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)hwfn_cookie;
	struct qed_pi_info *pi_info = NULL;
	struct qed_sb_attn_info *sb_attn;
	struct qed_sb_info *sb_info;
	int arr_size;
	u16 rc = 0;

	if (!p_hwfn->p_sp_sb) {
		DP_ERR(p_hwfn->cdev, "DPC called - no p_sp_sb\n");
		return;
	}

	sb_info = &p_hwfn->p_sp_sb->sb_info;
	arr_size = ARRAY_SIZE(p_hwfn->p_sp_sb->pi_info_arr);
	if (!sb_info) {
		DP_ERR(p_hwfn->cdev,
		       "Status block is NULL - cannot ack interrupts\n");
		return;
	}

	if (!p_hwfn->p_sb_attn) {
		DP_ERR(p_hwfn->cdev, "DPC called - no p_sb_attn");
		return;
	}
	sb_attn = p_hwfn->p_sb_attn;

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR, "DPC Called! (hwfn %p %d)\n",
		   p_hwfn, p_hwfn->my_id);

	/* Disable ack for def status block. Required both for msix +
	 * inta in non-mask mode, in inta does no harm.
	 */
	qed_sb_ack(sb_info, IGU_INT_DISABLE, 0);

	/* Gather Interrupts/Attentions information */
	if (!sb_info->sb_virt) {
		DP_ERR(
			p_hwfn->cdev,
			"Interrupt Status block is NULL - cannot check for new interrupts!\n");
	} else {
		u32 tmp_index = sb_info->sb_ack;

		rc = qed_sb_update_sb_idx(sb_info);
		DP_VERBOSE(p_hwfn->cdev, NETIF_MSG_INTR,
			   "Interrupt indices: 0x%08x --> 0x%08x\n",
			   tmp_index, sb_info->sb_ack);
	}

	if (!sb_attn || !sb_attn->sb_attn) {
		DP_ERR(
			p_hwfn->cdev,
			"Attentions Status block is NULL - cannot check for new attentions!\n");
	} else {
		u16 tmp_index = sb_attn->index;

		rc |= qed_attn_update_idx(p_hwfn, sb_attn);
		DP_VERBOSE(p_hwfn->cdev, NETIF_MSG_INTR,
			   "Attention indices: 0x%08x --> 0x%08x\n",
			   tmp_index, sb_attn->index);
	}

	/* Check if we expect interrupts at this time. if not just ack them */
	if (!(rc & QED_SB_EVENT_MASK)) {
		qed_sb_ack(sb_info, IGU_INT_ENABLE, 1);
		return;
	}

	/* Check the validity of the DPC ptt. If not ack interrupts and fail */
	if (!p_hwfn->p_dpc_ptt) {
		DP_NOTICE(p_hwfn->cdev, "Failed to allocate PTT\n");
		qed_sb_ack(sb_info, IGU_INT_ENABLE, 1);
		return;
	}

	if (rc & QED_SB_ATT_IDX)
		qed_int_attentions(p_hwfn);

	if (rc & QED_SB_IDX) {
		int pi;

		/* Look for a free index */
		for (pi = 0; pi < arr_size; pi++) {
			pi_info = &p_hwfn->p_sp_sb->pi_info_arr[pi];
			if (pi_info->comp_cb)
				pi_info->comp_cb(p_hwfn, pi_info->cookie);
		}
	}

	if (sb_attn && (rc & QED_SB_ATT_IDX))
		/* This should be done before the interrupts are enabled,
		 * since otherwise a new attention will be generated.
		 */
		qed_sb_ack_attn(p_hwfn, sb_info->igu_addr, sb_attn->index);

	qed_sb_ack(sb_info, IGU_INT_ENABLE, 1);
}

static void qed_int_sb_attn_free(struct qed_hwfn *p_hwfn)
{
	struct qed_sb_attn_info *p_sb = p_hwfn->p_sb_attn;

	if (!p_sb)
		return;

	if (p_sb->sb_attn)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  SB_ATTN_ALIGNED_SIZE(p_hwfn),
				  p_sb->sb_attn,
				  p_sb->sb_phys);
	kfree(p_sb);
}

static void qed_int_sb_attn_setup(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt)
{
	struct qed_sb_attn_info *sb_info = p_hwfn->p_sb_attn;

	memset(sb_info->sb_attn, 0, sizeof(*sb_info->sb_attn));

	sb_info->index = 0;
	sb_info->known_attn = 0;

	/* Configure Attention Status Block in IGU */
	qed_wr(p_hwfn, p_ptt, IGU_REG_ATTN_MSG_ADDR_L,
	       lower_32_bits(p_hwfn->p_sb_attn->sb_phys));
	qed_wr(p_hwfn, p_ptt, IGU_REG_ATTN_MSG_ADDR_H,
	       upper_32_bits(p_hwfn->p_sb_attn->sb_phys));
}

static void qed_int_sb_attn_init(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 void *sb_virt_addr,
				 dma_addr_t sb_phy_addr)
{
	struct qed_sb_attn_info *sb_info = p_hwfn->p_sb_attn;
	int i, j, k;

	sb_info->sb_attn = sb_virt_addr;
	sb_info->sb_phys = sb_phy_addr;

	/* Set the pointer to the AEU descriptors */
	sb_info->p_aeu_desc = aeu_descs;

	/* Calculate Parity Masks */
	memset(sb_info->parity_mask, 0, sizeof(u32) * NUM_ATTN_REGS);
	for (i = 0; i < NUM_ATTN_REGS; i++) {
		/* j is array index, k is bit index */
		for (j = 0, k = 0; k < 32; j++) {
			unsigned int flags = aeu_descs[i].bits[j].flags;

			if (flags & ATTENTION_PARITY)
				sb_info->parity_mask[i] |= 1 << k;

			k += ATTENTION_LENGTH(flags);
		}
		DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
			   "Attn Mask [Reg %d]: 0x%08x\n",
			   i, sb_info->parity_mask[i]);
	}

	/* Set the address of cleanup for the mcp attention */
	sb_info->mfw_attn_addr = (p_hwfn->rel_pf_id << 3) +
				 MISC_REG_AEU_GENERAL_ATTN_0;

	qed_int_sb_attn_setup(p_hwfn, p_ptt);
}

static int qed_int_sb_attn_alloc(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	struct qed_sb_attn_info *p_sb;
	void *p_virt;
	dma_addr_t p_phys = 0;

	/* SB struct */
	p_sb = kmalloc(sizeof(*p_sb), GFP_KERNEL);
	if (!p_sb) {
		DP_NOTICE(cdev, "Failed to allocate `struct qed_sb_attn_info'\n");
		return -ENOMEM;
	}

	/* SB ring  */
	p_virt = dma_alloc_coherent(&cdev->pdev->dev,
				    SB_ATTN_ALIGNED_SIZE(p_hwfn),
				    &p_phys, GFP_KERNEL);

	if (!p_virt) {
		DP_NOTICE(cdev, "Failed to allocate status block (attentions)\n");
		kfree(p_sb);
		return -ENOMEM;
	}

	/* Attention setup */
	p_hwfn->p_sb_attn = p_sb;
	qed_int_sb_attn_init(p_hwfn, p_ptt, p_virt, p_phys);

	return 0;
}

/* coalescing timeout = timeset << (timer_res + 1) */
#define QED_CAU_DEF_RX_USECS 24
#define QED_CAU_DEF_TX_USECS 48

void qed_init_cau_sb_entry(struct qed_hwfn *p_hwfn,
			   struct cau_sb_entry *p_sb_entry,
			   u8 pf_id,
			   u16 vf_number,
			   u8 vf_valid)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	u32 cau_state;

	memset(p_sb_entry, 0, sizeof(*p_sb_entry));

	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_PF_NUMBER, pf_id);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_VF_NUMBER, vf_number);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_VF_VALID, vf_valid);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_SB_TIMESET0, 0x7F);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_SB_TIMESET1, 0x7F);

	/* setting the time resultion to a fixed value ( = 1) */
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_TIMER_RES0,
		  QED_CAU_DEF_RX_TIMER_RES);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_TIMER_RES1,
		  QED_CAU_DEF_TX_TIMER_RES);

	cau_state = CAU_HC_DISABLE_STATE;

	if (cdev->int_coalescing_mode == QED_COAL_MODE_ENABLE) {
		cau_state = CAU_HC_ENABLE_STATE;
		if (!cdev->rx_coalesce_usecs)
			cdev->rx_coalesce_usecs = QED_CAU_DEF_RX_USECS;
		if (!cdev->tx_coalesce_usecs)
			cdev->tx_coalesce_usecs = QED_CAU_DEF_TX_USECS;
	}

	SET_FIELD(p_sb_entry->data, CAU_SB_ENTRY_STATE0, cau_state);
	SET_FIELD(p_sb_entry->data, CAU_SB_ENTRY_STATE1, cau_state);
}

void qed_int_cau_conf_sb(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 dma_addr_t sb_phys,
			 u16 igu_sb_id,
			 u16 vf_number,
			 u8 vf_valid)
{
	struct cau_sb_entry sb_entry;

	qed_init_cau_sb_entry(p_hwfn, &sb_entry, p_hwfn->rel_pf_id,
			      vf_number, vf_valid);

	if (p_hwfn->hw_init_done) {
		/* Wide-bus, initialize via DMAE */
		u64 phys_addr = (u64)sb_phys;

		qed_dmae_host2grc(p_hwfn, p_ptt, (u64)(uintptr_t)&phys_addr,
				  CAU_REG_SB_ADDR_MEMORY +
				  igu_sb_id * sizeof(u64), 2, 0);
		qed_dmae_host2grc(p_hwfn, p_ptt, (u64)(uintptr_t)&sb_entry,
				  CAU_REG_SB_VAR_MEMORY +
				  igu_sb_id * sizeof(u64), 2, 0);
	} else {
		/* Initialize Status Block Address */
		STORE_RT_REG_AGG(p_hwfn,
				 CAU_REG_SB_ADDR_MEMORY_RT_OFFSET +
				 igu_sb_id * 2,
				 sb_phys);

		STORE_RT_REG_AGG(p_hwfn,
				 CAU_REG_SB_VAR_MEMORY_RT_OFFSET +
				 igu_sb_id * 2,
				 sb_entry);
	}

	/* Configure pi coalescing if set */
	if (p_hwfn->cdev->int_coalescing_mode == QED_COAL_MODE_ENABLE) {
		u8 timeset = p_hwfn->cdev->rx_coalesce_usecs >>
			     (QED_CAU_DEF_RX_TIMER_RES + 1);
		u8 num_tc = 1, i;

		qed_int_cau_conf_pi(p_hwfn, p_ptt, igu_sb_id, RX_PI,
				    QED_COAL_RX_STATE_MACHINE,
				    timeset);

		timeset = p_hwfn->cdev->tx_coalesce_usecs >>
			  (QED_CAU_DEF_TX_TIMER_RES + 1);

		for (i = 0; i < num_tc; i++) {
			qed_int_cau_conf_pi(p_hwfn, p_ptt,
					    igu_sb_id, TX_PI(i),
					    QED_COAL_TX_STATE_MACHINE,
					    timeset);
		}
	}
}

void qed_int_cau_conf_pi(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 u16 igu_sb_id,
			 u32 pi_index,
			 enum qed_coalescing_fsm coalescing_fsm,
			 u8 timeset)
{
	struct cau_pi_entry pi_entry;
	u32 sb_offset;
	u32 pi_offset;

	sb_offset = igu_sb_id * PIS_PER_SB;
	memset(&pi_entry, 0, sizeof(struct cau_pi_entry));

	SET_FIELD(pi_entry.prod, CAU_PI_ENTRY_PI_TIMESET, timeset);
	if (coalescing_fsm == QED_COAL_RX_STATE_MACHINE)
		SET_FIELD(pi_entry.prod, CAU_PI_ENTRY_FSM_SEL, 0);
	else
		SET_FIELD(pi_entry.prod, CAU_PI_ENTRY_FSM_SEL, 1);

	pi_offset = sb_offset + pi_index;
	if (p_hwfn->hw_init_done) {
		qed_wr(p_hwfn, p_ptt,
		       CAU_REG_PI_MEMORY + pi_offset * sizeof(u32),
		       *((u32 *)&(pi_entry)));
	} else {
		STORE_RT_REG(p_hwfn,
			     CAU_REG_PI_MEMORY_RT_OFFSET + pi_offset,
			     *((u32 *)&(pi_entry)));
	}
}

void qed_int_sb_setup(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      struct qed_sb_info *sb_info)
{
	/* zero status block and ack counter */
	sb_info->sb_ack = 0;
	memset(sb_info->sb_virt, 0, sizeof(*sb_info->sb_virt));

	qed_int_cau_conf_sb(p_hwfn, p_ptt, sb_info->sb_phys,
			    sb_info->igu_sb_id, 0, 0);
}

/**
 * @brief qed_get_igu_sb_id - given a sw sb_id return the
 *        igu_sb_id
 *
 * @param p_hwfn
 * @param sb_id
 *
 * @return u16
 */
static u16 qed_get_igu_sb_id(struct qed_hwfn *p_hwfn,
			     u16 sb_id)
{
	u16 igu_sb_id;

	/* Assuming continuous set of IGU SBs dedicated for given PF */
	if (sb_id == QED_SP_SB_ID)
		igu_sb_id = p_hwfn->hw_info.p_igu_info->igu_dsb_id;
	else
		igu_sb_id = sb_id + p_hwfn->hw_info.p_igu_info->igu_base_sb;

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR, "SB [%s] index is 0x%04x\n",
		   (sb_id == QED_SP_SB_ID) ? "DSB" : "non-DSB", igu_sb_id);

	return igu_sb_id;
}

int qed_int_sb_init(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    struct qed_sb_info *sb_info,
		    void *sb_virt_addr,
		    dma_addr_t sb_phy_addr,
		    u16 sb_id)
{
	sb_info->sb_virt = sb_virt_addr;
	sb_info->sb_phys = sb_phy_addr;

	sb_info->igu_sb_id = qed_get_igu_sb_id(p_hwfn, sb_id);

	if (sb_id != QED_SP_SB_ID) {
		p_hwfn->sbs_info[sb_id] = sb_info;
		p_hwfn->num_sbs++;
	}

	sb_info->cdev = p_hwfn->cdev;

	/* The igu address will hold the absolute address that needs to be
	 * written to for a specific status block
	 */
	sb_info->igu_addr = (u8 __iomem *)p_hwfn->regview +
					  GTT_BAR0_MAP_REG_IGU_CMD +
					  (sb_info->igu_sb_id << 3);

	sb_info->flags |= QED_SB_INFO_INIT;

	qed_int_sb_setup(p_hwfn, p_ptt, sb_info);

	return 0;
}

int qed_int_sb_release(struct qed_hwfn *p_hwfn,
		       struct qed_sb_info *sb_info,
		       u16 sb_id)
{
	if (sb_id == QED_SP_SB_ID) {
		DP_ERR(p_hwfn, "Do Not free sp sb using this function");
		return -EINVAL;
	}

	/* zero status block and ack counter */
	sb_info->sb_ack = 0;
	memset(sb_info->sb_virt, 0, sizeof(*sb_info->sb_virt));

	if (p_hwfn->sbs_info[sb_id] != NULL) {
		p_hwfn->sbs_info[sb_id] = NULL;
		p_hwfn->num_sbs--;
	}

	return 0;
}

static void qed_int_sp_sb_free(struct qed_hwfn *p_hwfn)
{
	struct qed_sb_sp_info *p_sb = p_hwfn->p_sp_sb;

	if (!p_sb)
		return;

	if (p_sb->sb_info.sb_virt)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  SB_ALIGNED_SIZE(p_hwfn),
				  p_sb->sb_info.sb_virt,
				  p_sb->sb_info.sb_phys);
	kfree(p_sb);
}

static int qed_int_sp_sb_alloc(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt)
{
	struct qed_sb_sp_info *p_sb;
	dma_addr_t p_phys = 0;
	void *p_virt;

	/* SB struct */
	p_sb = kmalloc(sizeof(*p_sb), GFP_KERNEL);
	if (!p_sb) {
		DP_NOTICE(p_hwfn, "Failed to allocate `struct qed_sb_info'\n");
		return -ENOMEM;
	}

	/* SB ring  */
	p_virt = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				    SB_ALIGNED_SIZE(p_hwfn),
				    &p_phys, GFP_KERNEL);
	if (!p_virt) {
		DP_NOTICE(p_hwfn, "Failed to allocate status block\n");
		kfree(p_sb);
		return -ENOMEM;
	}

	/* Status Block setup */
	p_hwfn->p_sp_sb = p_sb;
	qed_int_sb_init(p_hwfn, p_ptt, &p_sb->sb_info, p_virt,
			p_phys, QED_SP_SB_ID);

	memset(p_sb->pi_info_arr, 0, sizeof(p_sb->pi_info_arr));

	return 0;
}

int qed_int_register_cb(struct qed_hwfn *p_hwfn,
			qed_int_comp_cb_t comp_cb,
			void *cookie,
			u8 *sb_idx,
			__le16 **p_fw_cons)
{
	struct qed_sb_sp_info *p_sp_sb = p_hwfn->p_sp_sb;
	int rc = -ENOMEM;
	u8 pi;

	/* Look for a free index */
	for (pi = 0; pi < ARRAY_SIZE(p_sp_sb->pi_info_arr); pi++) {
		if (p_sp_sb->pi_info_arr[pi].comp_cb)
			continue;

		p_sp_sb->pi_info_arr[pi].comp_cb = comp_cb;
		p_sp_sb->pi_info_arr[pi].cookie = cookie;
		*sb_idx = pi;
		*p_fw_cons = &p_sp_sb->sb_info.sb_virt->pi_array[pi];
		rc = 0;
		break;
	}

	return rc;
}

int qed_int_unregister_cb(struct qed_hwfn *p_hwfn, u8 pi)
{
	struct qed_sb_sp_info *p_sp_sb = p_hwfn->p_sp_sb;

	if (p_sp_sb->pi_info_arr[pi].comp_cb == NULL)
		return -ENOMEM;

	p_sp_sb->pi_info_arr[pi].comp_cb = NULL;
	p_sp_sb->pi_info_arr[pi].cookie = NULL;

	return 0;
}

u16 qed_int_get_sp_sb_id(struct qed_hwfn *p_hwfn)
{
	return p_hwfn->p_sp_sb->sb_info.igu_sb_id;
}

void qed_int_igu_enable_int(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    enum qed_int_mode int_mode)
{
	u32 igu_pf_conf = IGU_PF_CONF_FUNC_EN | IGU_PF_CONF_ATTN_BIT_EN;

	p_hwfn->cdev->int_mode = int_mode;
	switch (p_hwfn->cdev->int_mode) {
	case QED_INT_MODE_INTA:
		igu_pf_conf |= IGU_PF_CONF_INT_LINE_EN;
		igu_pf_conf |= IGU_PF_CONF_SINGLE_ISR_EN;
		break;

	case QED_INT_MODE_MSI:
		igu_pf_conf |= IGU_PF_CONF_MSI_MSIX_EN;
		igu_pf_conf |= IGU_PF_CONF_SINGLE_ISR_EN;
		break;

	case QED_INT_MODE_MSIX:
		igu_pf_conf |= IGU_PF_CONF_MSI_MSIX_EN;
		break;
	case QED_INT_MODE_POLL:
		break;
	}

	qed_wr(p_hwfn, p_ptt, IGU_REG_PF_CONFIGURATION, igu_pf_conf);
}

int qed_int_igu_enable(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
		       enum qed_int_mode int_mode)
{
	int rc = 0;

	/* Configure AEU signal change to produce attentions */
	qed_wr(p_hwfn, p_ptt, IGU_REG_ATTENTION_ENABLE, 0);
	qed_wr(p_hwfn, p_ptt, IGU_REG_LEADING_EDGE_LATCH, 0xfff);
	qed_wr(p_hwfn, p_ptt, IGU_REG_TRAILING_EDGE_LATCH, 0xfff);
	qed_wr(p_hwfn, p_ptt, IGU_REG_ATTENTION_ENABLE, 0xfff);

	/* Flush the writes to IGU */
	mmiowb();

	/* Unmask AEU signals toward IGU */
	qed_wr(p_hwfn, p_ptt, MISC_REG_AEU_MASK_ATTN_IGU, 0xff);
	if ((int_mode != QED_INT_MODE_INTA) || IS_LEAD_HWFN(p_hwfn)) {
		rc = qed_slowpath_irq_req(p_hwfn);
		if (rc != 0) {
			DP_NOTICE(p_hwfn, "Slowpath IRQ request failed\n");
			return -EINVAL;
		}
		p_hwfn->b_int_requested = true;
	}
	/* Enable interrupt Generation */
	qed_int_igu_enable_int(p_hwfn, p_ptt, int_mode);
	p_hwfn->b_int_enabled = 1;

	return rc;
}

void qed_int_igu_disable_int(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt)
{
	p_hwfn->b_int_enabled = 0;

	qed_wr(p_hwfn, p_ptt, IGU_REG_PF_CONFIGURATION, 0);
}

#define IGU_CLEANUP_SLEEP_LENGTH                (1000)
void qed_int_igu_cleanup_sb(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    u32 sb_id,
			    bool cleanup_set,
			    u16 opaque_fid
			    )
{
	u32 pxp_addr = IGU_CMD_INT_ACK_BASE + sb_id;
	u32 sleep_cnt = IGU_CLEANUP_SLEEP_LENGTH;
	u32 data = 0;
	u32 cmd_ctrl = 0;
	u32 val = 0;
	u32 sb_bit = 0;
	u32 sb_bit_addr = 0;

	/* Set the data field */
	SET_FIELD(data, IGU_CLEANUP_CLEANUP_SET, cleanup_set ? 1 : 0);
	SET_FIELD(data, IGU_CLEANUP_CLEANUP_TYPE, 0);
	SET_FIELD(data, IGU_CLEANUP_COMMAND_TYPE, IGU_COMMAND_TYPE_SET);

	/* Set the control register */
	SET_FIELD(cmd_ctrl, IGU_CTRL_REG_PXP_ADDR, pxp_addr);
	SET_FIELD(cmd_ctrl, IGU_CTRL_REG_FID, opaque_fid);
	SET_FIELD(cmd_ctrl, IGU_CTRL_REG_TYPE, IGU_CTRL_CMD_TYPE_WR);

	qed_wr(p_hwfn, p_ptt, IGU_REG_COMMAND_REG_32LSB_DATA, data);

	barrier();

	qed_wr(p_hwfn, p_ptt, IGU_REG_COMMAND_REG_CTRL, cmd_ctrl);

	/* Flush the write to IGU */
	mmiowb();

	/* calculate where to read the status bit from */
	sb_bit = 1 << (sb_id % 32);
	sb_bit_addr = sb_id / 32 * sizeof(u32);

	sb_bit_addr += IGU_REG_CLEANUP_STATUS_0;

	/* Now wait for the command to complete */
	do {
		val = qed_rd(p_hwfn, p_ptt, sb_bit_addr);

		if ((val & sb_bit) == (cleanup_set ? sb_bit : 0))
			break;

		usleep_range(5000, 10000);
	} while (--sleep_cnt);

	if (!sleep_cnt)
		DP_NOTICE(p_hwfn,
			  "Timeout waiting for clear status 0x%08x [for sb %d]\n",
			  val, sb_id);
}

void qed_int_igu_init_pure_rt_single(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     u32 sb_id,
				     u16 opaque,
				     bool b_set)
{
	int pi;

	/* Set */
	if (b_set)
		qed_int_igu_cleanup_sb(p_hwfn, p_ptt, sb_id, 1, opaque);

	/* Clear */
	qed_int_igu_cleanup_sb(p_hwfn, p_ptt, sb_id, 0, opaque);

	/* Clear the CAU for the SB */
	for (pi = 0; pi < 12; pi++)
		qed_wr(p_hwfn, p_ptt,
		       CAU_REG_PI_MEMORY + (sb_id * 12 + pi) * 4, 0);
}

void qed_int_igu_init_pure_rt(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      bool b_set,
			      bool b_slowpath)
{
	u32 igu_base_sb = p_hwfn->hw_info.p_igu_info->igu_base_sb;
	u32 igu_sb_cnt = p_hwfn->hw_info.p_igu_info->igu_sb_cnt;
	u32 sb_id = 0;
	u32 val = 0;

	val = qed_rd(p_hwfn, p_ptt, IGU_REG_BLOCK_CONFIGURATION);
	val |= IGU_REG_BLOCK_CONFIGURATION_VF_CLEANUP_EN;
	val &= ~IGU_REG_BLOCK_CONFIGURATION_PXP_TPH_INTERFACE_EN;
	qed_wr(p_hwfn, p_ptt, IGU_REG_BLOCK_CONFIGURATION, val);

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
		   "IGU cleaning SBs [%d,...,%d]\n",
		   igu_base_sb, igu_base_sb + igu_sb_cnt - 1);

	for (sb_id = igu_base_sb; sb_id < igu_base_sb + igu_sb_cnt; sb_id++)
		qed_int_igu_init_pure_rt_single(p_hwfn, p_ptt, sb_id,
						p_hwfn->hw_info.opaque_fid,
						b_set);

	if (b_slowpath) {
		sb_id = p_hwfn->hw_info.p_igu_info->igu_dsb_id;
		DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
			   "IGU cleaning slowpath SB [%d]\n", sb_id);
		qed_int_igu_init_pure_rt_single(p_hwfn, p_ptt, sb_id,
						p_hwfn->hw_info.opaque_fid,
						b_set);
	}
}

static u32 qed_int_igu_read_cam_block(struct qed_hwfn	*p_hwfn,
				      struct qed_ptt	*p_ptt,
				      u16		sb_id)
{
	u32 val = qed_rd(p_hwfn, p_ptt,
			 IGU_REG_MAPPING_MEMORY +
			 sizeof(u32) * sb_id);
	struct qed_igu_block *p_block;

	p_block = &p_hwfn->hw_info.p_igu_info->igu_map.igu_blocks[sb_id];

	/* stop scanning when hit first invalid PF entry */
	if (!GET_FIELD(val, IGU_MAPPING_LINE_VALID) &&
	    GET_FIELD(val, IGU_MAPPING_LINE_PF_VALID))
		goto out;

	/* Fill the block information */
	p_block->status		= QED_IGU_STATUS_VALID;
	p_block->function_id	= GET_FIELD(val,
					    IGU_MAPPING_LINE_FUNCTION_NUMBER);
	p_block->is_pf		= GET_FIELD(val, IGU_MAPPING_LINE_PF_VALID);
	p_block->vector_number	= GET_FIELD(val,
					    IGU_MAPPING_LINE_VECTOR_NUMBER);

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
		   "IGU_BLOCK: [SB 0x%04x, Value in CAM 0x%08x] func_id = %d is_pf = %d vector_num = 0x%x\n",
		   sb_id, val, p_block->function_id,
		   p_block->is_pf, p_block->vector_number);

out:
	return val;
}

int qed_int_igu_read_cam(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt)
{
	struct qed_igu_info *p_igu_info;
	struct qed_igu_block *blk;
	u32 val;
	u16 sb_id;
	u16 prev_sb_id = 0xFF;

	p_hwfn->hw_info.p_igu_info = kzalloc(sizeof(*p_igu_info), GFP_KERNEL);

	if (!p_hwfn->hw_info.p_igu_info)
		return -ENOMEM;

	p_igu_info = p_hwfn->hw_info.p_igu_info;

	/* Initialize base sb / sb cnt for PFs */
	p_igu_info->igu_base_sb		= 0xffff;
	p_igu_info->igu_sb_cnt		= 0;
	p_igu_info->igu_dsb_id		= 0xffff;
	p_igu_info->igu_base_sb_iov	= 0xffff;

	for (sb_id = 0; sb_id < QED_MAPPING_MEMORY_SIZE(p_hwfn->cdev);
	     sb_id++) {
		blk = &p_igu_info->igu_map.igu_blocks[sb_id];

		val	= qed_int_igu_read_cam_block(p_hwfn, p_ptt, sb_id);

		/* stop scanning when hit first invalid PF entry */
		if (!GET_FIELD(val, IGU_MAPPING_LINE_VALID) &&
		    GET_FIELD(val, IGU_MAPPING_LINE_PF_VALID))
			break;

		if (blk->is_pf) {
			if (blk->function_id == p_hwfn->rel_pf_id) {
				blk->status |= QED_IGU_STATUS_PF;

				if (blk->vector_number == 0) {
					if (p_igu_info->igu_dsb_id == 0xffff)
						p_igu_info->igu_dsb_id = sb_id;
				} else {
					if (p_igu_info->igu_base_sb ==
					    0xffff) {
						p_igu_info->igu_base_sb = sb_id;
					} else if (prev_sb_id != sb_id - 1) {
						DP_NOTICE(p_hwfn->cdev,
							  "consecutive igu vectors for HWFN %x broken",
							  p_hwfn->rel_pf_id);
						break;
					}
					prev_sb_id = sb_id;
					/* we don't count the default */
					(p_igu_info->igu_sb_cnt)++;
				}
			}
		}
	}

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
		   "IGU igu_base_sb=0x%x igu_sb_cnt=%d igu_dsb_id=0x%x\n",
		   p_igu_info->igu_base_sb,
		   p_igu_info->igu_sb_cnt,
		   p_igu_info->igu_dsb_id);

	if (p_igu_info->igu_base_sb == 0xffff ||
	    p_igu_info->igu_dsb_id == 0xffff ||
	    p_igu_info->igu_sb_cnt == 0) {
		DP_NOTICE(p_hwfn,
			  "IGU CAM returned invalid values igu_base_sb=0x%x igu_sb_cnt=%d igu_dsb_id=0x%x\n",
			   p_igu_info->igu_base_sb,
			   p_igu_info->igu_sb_cnt,
			   p_igu_info->igu_dsb_id);
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief Initialize igu runtime registers
 *
 * @param p_hwfn
 */
void qed_int_igu_init_rt(struct qed_hwfn *p_hwfn)
{
	u32 igu_pf_conf = 0;

	igu_pf_conf |= IGU_PF_CONF_FUNC_EN;

	STORE_RT_REG(p_hwfn, IGU_REG_PF_CONFIGURATION_RT_OFFSET, igu_pf_conf);
}

u64 qed_int_igu_read_sisr_reg(struct qed_hwfn *p_hwfn)
{
	u64 intr_status = 0;
	u32 intr_status_lo = 0;
	u32 intr_status_hi = 0;
	u32 lsb_igu_cmd_addr = IGU_REG_SISR_MDPC_WMASK_LSB_UPPER -
			       IGU_CMD_INT_ACK_BASE;
	u32 msb_igu_cmd_addr = IGU_REG_SISR_MDPC_WMASK_MSB_UPPER -
			       IGU_CMD_INT_ACK_BASE;

	intr_status_lo = REG_RD(p_hwfn,
				GTT_BAR0_MAP_REG_IGU_CMD +
				lsb_igu_cmd_addr * 8);
	intr_status_hi = REG_RD(p_hwfn,
				GTT_BAR0_MAP_REG_IGU_CMD +
				msb_igu_cmd_addr * 8);
	intr_status = ((u64)intr_status_hi << 32) + (u64)intr_status_lo;

	return intr_status;
}

static void qed_int_sp_dpc_setup(struct qed_hwfn *p_hwfn)
{
	tasklet_init(p_hwfn->sp_dpc,
		     qed_int_sp_dpc, (unsigned long)p_hwfn);
	p_hwfn->b_sp_dpc_enabled = true;
}

static int qed_int_sp_dpc_alloc(struct qed_hwfn *p_hwfn)
{
	p_hwfn->sp_dpc = kmalloc(sizeof(*p_hwfn->sp_dpc), GFP_KERNEL);
	if (!p_hwfn->sp_dpc)
		return -ENOMEM;

	return 0;
}

static void qed_int_sp_dpc_free(struct qed_hwfn *p_hwfn)
{
	kfree(p_hwfn->sp_dpc);
}

int qed_int_alloc(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt)
{
	int rc = 0;

	rc = qed_int_sp_dpc_alloc(p_hwfn);
	if (rc) {
		DP_ERR(p_hwfn->cdev, "Failed to allocate sp dpc mem\n");
		return rc;
	}
	rc = qed_int_sp_sb_alloc(p_hwfn, p_ptt);
	if (rc) {
		DP_ERR(p_hwfn->cdev, "Failed to allocate sp sb mem\n");
		return rc;
	}
	rc = qed_int_sb_attn_alloc(p_hwfn, p_ptt);
	if (rc) {
		DP_ERR(p_hwfn->cdev, "Failed to allocate sb attn mem\n");
		return rc;
	}
	return rc;
}

void qed_int_free(struct qed_hwfn *p_hwfn)
{
	qed_int_sp_sb_free(p_hwfn);
	qed_int_sb_attn_free(p_hwfn);
	qed_int_sp_dpc_free(p_hwfn);
}

void qed_int_setup(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt)
{
	qed_int_sb_setup(p_hwfn, p_ptt, &p_hwfn->p_sp_sb->sb_info);
	qed_int_sb_attn_setup(p_hwfn, p_ptt);
	qed_int_sp_dpc_setup(p_hwfn);
}

void qed_int_get_num_sbs(struct qed_hwfn	*p_hwfn,
			 struct qed_sb_cnt_info *p_sb_cnt_info)
{
	struct qed_igu_info *info = p_hwfn->hw_info.p_igu_info;

	if (!info || !p_sb_cnt_info)
		return;

	p_sb_cnt_info->sb_cnt		= info->igu_sb_cnt;
	p_sb_cnt_info->sb_iov_cnt	= info->igu_sb_cnt_iov;
	p_sb_cnt_info->sb_free_blk	= info->free_blks;
}

void qed_int_disable_post_isr_release(struct qed_dev *cdev)
{
	int i;

	for_each_hwfn(cdev, i)
		cdev->hwfns[i].b_int_requested = false;
}
