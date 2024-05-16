/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MTK_MDP3_COMP_H__
#define __MTK_MDP3_COMP_H__

#include "mtk-mdp3-cmdq.h"

#define MM_REG_WRITE_MASK(cmd, id, base, ofst, val, mask, ...)	\
	cmdq_pkt_write_mask(&((cmd)->pkt), id,			\
		(base) + (ofst), (val), (mask), ##__VA_ARGS__)

#define MM_REG_WRITE(cmd, id, base, ofst, val, mask, ...)	\
do {								\
	typeof(mask) (m) = (mask);				\
	MM_REG_WRITE_MASK(cmd, id, base, ofst, val,		\
		(((m) & (ofst##_MASK)) == (ofst##_MASK)) ?	\
			(0xffffffff) : (m), ##__VA_ARGS__);	\
} while (0)

#define MM_REG_WAIT(cmd, evt)					\
do {								\
	typeof(cmd) (c) = (cmd);				\
	typeof(evt) (e) = (evt);				\
	cmdq_pkt_wfe(&((c)->pkt), (e), true);			\
} while (0)

#define MM_REG_WAIT_NO_CLEAR(cmd, evt)				\
do {								\
	typeof(cmd) (c) = (cmd);				\
	typeof(evt) (e) = (evt);				\
	cmdq_pkt_wfe(&((c)->pkt), (e), false);			\
} while (0)

#define MM_REG_CLEAR(cmd, evt)					\
do {								\
	typeof(cmd) (c) = (cmd);				\
	typeof(evt) (e) = (evt);				\
	cmdq_pkt_clear_event(&((c)->pkt), (e));			\
} while (0)

#define MM_REG_SET_EVENT(cmd, evt)				\
do {								\
	typeof(cmd) (c) = (cmd);				\
	typeof(evt) (e) = (evt);				\
	cmdq_pkt_set_event(&((c)->pkt), (e));			\
} while (0)

#define MM_REG_POLL_MASK(cmd, id, base, ofst, val, _mask, ...)	\
do {								\
	typeof(_mask) (_m) = (_mask);				\
	cmdq_pkt_poll_mask(&((cmd)->pkt), id,			\
		(base) + (ofst), (val), (_m), ##__VA_ARGS__);	\
} while (0)

#define MM_REG_POLL(cmd, id, base, ofst, val, mask, ...)	\
do {								\
	typeof(mask) (m) = (mask);				\
	MM_REG_POLL_MASK((cmd), id, base, ofst, val,		\
		(((m) & (ofst##_MASK)) == (ofst##_MASK)) ?	\
			(0xffffffff) : (m), ##__VA_ARGS__);	\
} while (0)

enum mtk_mdp_comp_id {
	MDP_COMP_NONE = -1,	/* Invalid engine */

	/* ISP */
	MDP_COMP_WPEI = 0,
	MDP_COMP_WPEO,		/* 1 */
	MDP_COMP_WPEI2,		/* 2 */
	MDP_COMP_WPEO2,		/* 3 */
	MDP_COMP_ISP_IMGI,	/* 4 */
	MDP_COMP_ISP_IMGO,	/* 5 */
	MDP_COMP_ISP_IMG2O,	/* 6 */

	/* IPU */
	MDP_COMP_IPUI,		/* 7 */
	MDP_COMP_IPUO,		/* 8 */

	/* MDP */
	MDP_COMP_CAMIN,		/* 9 */
	MDP_COMP_CAMIN2,	/* 10 */
	MDP_COMP_RDMA0,		/* 11 */
	MDP_COMP_RDMA1,		/* 12 */
	MDP_COMP_RDMA2,		/* 13 */
	MDP_COMP_RDMA3,		/* 14 */
	MDP_COMP_AAL0,		/* 15 */
	MDP_COMP_AAL1,		/* 16 */
	MDP_COMP_AAL2,		/* 17 */
	MDP_COMP_AAL3,		/* 18 */
	MDP_COMP_CCORR0,	/* 19 */
	MDP_COMP_RSZ0,		/* 20 */
	MDP_COMP_RSZ1,		/* 21 */
	MDP_COMP_RSZ2,		/* 22 */
	MDP_COMP_RSZ3,		/* 23 */
	MDP_COMP_TDSHP0,	/* 24 */
	MDP_COMP_TDSHP1,	/* 25 */
	MDP_COMP_TDSHP2,	/* 26 */
	MDP_COMP_TDSHP3,	/* 27 */
	MDP_COMP_COLOR0,	/* 28 */
	MDP_COMP_COLOR1,	/* 29 */
	MDP_COMP_COLOR2,	/* 30 */
	MDP_COMP_COLOR3,	/* 31 */
	MDP_COMP_PATH0_SOUT,	/* 32 */
	MDP_COMP_PATH1_SOUT,	/* 33 */
	MDP_COMP_WROT0,		/* 34 */
	MDP_COMP_WROT1,		/* 35 */
	MDP_COMP_WROT2,		/* 36 */
	MDP_COMP_WROT3,		/* 37 */
	MDP_COMP_WDMA,		/* 38 */
	MDP_COMP_SPLIT,		/* 39 */
	MDP_COMP_SPLIT2,	/* 40 */
	MDP_COMP_STITCH,	/* 41 */
	MDP_COMP_FG0,		/* 42 */
	MDP_COMP_FG1,		/* 43 */
	MDP_COMP_FG2,		/* 44 */
	MDP_COMP_FG3,		/* 45 */
	MDP_COMP_TO_SVPP2MOUT,	/* 46 */
	MDP_COMP_TO_SVPP3MOUT,	/* 47 */
	MDP_COMP_TO_WARP0MOUT,	/* 48 */
	MDP_COMP_TO_WARP1MOUT,	/* 49 */
	MDP_COMP_VPP0_SOUT,	/* 50 */
	MDP_COMP_VPP1_SOUT,	/* 51 */
	MDP_COMP_PQ0_SOUT,	/* 52 */
	MDP_COMP_PQ1_SOUT,	/* 53 */
	MDP_COMP_HDR0,		/* 54 */
	MDP_COMP_HDR1,		/* 55 */
	MDP_COMP_HDR2,		/* 56 */
	MDP_COMP_HDR3,		/* 57 */
	MDP_COMP_OVL0,		/* 58 */
	MDP_COMP_OVL1,		/* 59 */
	MDP_COMP_PAD0,		/* 60 */
	MDP_COMP_PAD1,		/* 61 */
	MDP_COMP_PAD2,		/* 62 */
	MDP_COMP_PAD3,		/* 63 */
	MDP_COMP_TCC0,		/* 64 */
	MDP_COMP_TCC1,		/* 65 */
	MDP_COMP_MERGE2,	/* 66 */
	MDP_COMP_MERGE3,	/* 67 */
	MDP_COMP_VDO0DL0,	/* 68 */
	MDP_COMP_VDO1DL0,	/* 69 */
	MDP_COMP_VDO0DL1,	/* 70 */
	MDP_COMP_VDO1DL1,	/* 71 */

	MDP_MAX_COMP_COUNT	/* ALWAYS keep at the end */
};

enum mdp_comp_type {
	MDP_COMP_TYPE_INVALID = 0,

	MDP_COMP_TYPE_RDMA,
	MDP_COMP_TYPE_RSZ,
	MDP_COMP_TYPE_WROT,
	MDP_COMP_TYPE_WDMA,
	MDP_COMP_TYPE_PATH,

	MDP_COMP_TYPE_TDSHP,
	MDP_COMP_TYPE_COLOR,
	MDP_COMP_TYPE_DRE,
	MDP_COMP_TYPE_CCORR,
	MDP_COMP_TYPE_AAL,
	MDP_COMP_TYPE_TCC,
	MDP_COMP_TYPE_HDR,
	MDP_COMP_TYPE_SPLIT,
	MDP_COMP_TYPE_STITCH,
	MDP_COMP_TYPE_FG,
	MDP_COMP_TYPE_OVL,
	MDP_COMP_TYPE_PAD,
	MDP_COMP_TYPE_MERGE,

	MDP_COMP_TYPE_IMGI,
	MDP_COMP_TYPE_WPEI,
	MDP_COMP_TYPE_EXTO,	/* External path */
	MDP_COMP_TYPE_DL_PATH,	/* Direct-link path */
	MDP_COMP_TYPE_DUMMY,

	MDP_COMP_TYPE_COUNT	/* ALWAYS keep at the end */
};

#define MDP_GCE_NO_EVENT (-1)
enum {
	MDP_GCE_EVENT_SOF = 0,
	MDP_GCE_EVENT_EOF = 1,
	MDP_GCE_EVENT_MAX,
};

struct mdp_comp_match {
	enum mdp_comp_type type;
	u32 alias_id;
	s32 inner_id;
	s32 subsys_id;
};

/* Used to describe the item order in MDP property */
struct mdp_comp_info {
	u32 clk_num;
	u32 clk_ofst;
	u32 dts_reg_ofst;
};

struct mdp_comp_blend {
	enum mtk_mdp_comp_id b_id;
	bool aid_mod;
	bool aid_clk;
};

struct mdp_comp_data {
	struct mdp_comp_match match;
	struct mdp_comp_info info;
	struct mdp_comp_blend blend;
};

struct mdp_comp_ops;

struct mdp_comp {
	struct mdp_dev			*mdp_dev;
	void __iomem			*regs;
	phys_addr_t			reg_base;
	u8				subsys_id;
	u8				clk_num;
	struct clk			**clks;
	struct device			*comp_dev;
	enum mdp_comp_type		type;
	enum mtk_mdp_comp_id		public_id;
	s32				inner_id;
	u32				alias_id;
	s32				gce_event[MDP_GCE_EVENT_MAX];
	const struct mdp_comp_ops	*ops;
};

struct mdp_comp_ctx {
	struct mdp_comp			*comp;
	const struct img_compparam	*param;
	const struct img_input		*input;
	const struct img_output		*outputs[IMG_MAX_HW_OUTPUTS];
};

struct mdp_comp_ops {
	s64 (*get_comp_flag)(const struct mdp_comp_ctx *ctx);
	int (*init_comp)(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd);
	int (*config_frame)(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd,
			    const struct v4l2_rect *compose);
	int (*config_subfrm)(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd, u32 index);
	int (*wait_comp_event)(struct mdp_comp_ctx *ctx,
			       struct mdp_cmdq_cmd *cmd);
	int (*advance_subfrm)(struct mdp_comp_ctx *ctx,
			      struct mdp_cmdq_cmd *cmd, u32 index);
	int (*post_process)(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd);
};

struct mdp_dev;

int mdp_comp_config(struct mdp_dev *mdp);
void mdp_comp_destroy(struct mdp_dev *mdp);
int mdp_comp_clock_on(struct device *dev, struct mdp_comp *comp);
void mdp_comp_clock_off(struct device *dev, struct mdp_comp *comp);
int mdp_comp_clocks_on(struct device *dev, struct mdp_comp *comps, int num);
void mdp_comp_clocks_off(struct device *dev, struct mdp_comp *comps, int num);
int mdp_comp_ctx_config(struct mdp_dev *mdp, struct mdp_comp_ctx *ctx,
			const struct img_compparam *param,
			const struct img_ipi_frameparam *frame);

#endif  /* __MTK_MDP3_COMP_H__ */
