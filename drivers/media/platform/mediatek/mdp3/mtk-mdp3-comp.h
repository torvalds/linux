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
	MDP_COMP_AAL0,		/* 12 */
	MDP_COMP_CCORR0,	/* 13 */
	MDP_COMP_RSZ0,		/* 14 */
	MDP_COMP_RSZ1,		/* 15 */
	MDP_COMP_TDSHP0,	/* 16 */
	MDP_COMP_COLOR0,	/* 17 */
	MDP_COMP_PATH0_SOUT,	/* 18 */
	MDP_COMP_PATH1_SOUT,	/* 19 */
	MDP_COMP_WROT0,		/* 20 */
	MDP_COMP_WDMA,		/* 21 */

	/* Dummy Engine */
	MDP_COMP_RDMA1,		/* 22 */
	MDP_COMP_RSZ2,		/* 23 */
	MDP_COMP_TDSHP1,	/* 24 */
	MDP_COMP_WROT1,		/* 25 */

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
	MDP_COMP_TYPE_HDR,

	MDP_COMP_TYPE_IMGI,
	MDP_COMP_TYPE_WPEI,
	MDP_COMP_TYPE_EXTO,	/* External path */
	MDP_COMP_TYPE_DL_PATH,	/* Direct-link path */

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
};

/* Used to describe the item order in MDP property */
struct mdp_comp_info {
	u32 clk_num;
	u32 clk_ofst;
	u32 dts_reg_ofst;
};

struct mdp_comp_data {
	struct mdp_comp_match match;
	struct mdp_comp_info info;
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
