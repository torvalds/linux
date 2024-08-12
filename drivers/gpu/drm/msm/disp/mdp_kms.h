/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MDP_KMS_H__
#define __MDP_KMS_H__

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include "mdp_format.h"
#include "msm_drv.h"
#include "msm_kms.h"
#include "mdp_common.xml.h"

struct mdp_kms;

struct mdp_kms_funcs {
	struct msm_kms_funcs base;
	void (*set_irqmask)(struct mdp_kms *mdp_kms, uint32_t irqmask,
		uint32_t old_irqmask);
};

struct mdp_kms {
	struct msm_kms base;

	const struct mdp_kms_funcs *funcs;

	/* irq handling: */
	bool in_irq;
	struct list_head irq_list;    /* list of mdp4_irq */
	uint32_t vblank_mask;         /* irq bits set for userspace vblank */
	uint32_t cur_irq_mask;        /* current irq mask */
};
#define to_mdp_kms(x) container_of(x, struct mdp_kms, base)

static inline int mdp_kms_init(struct mdp_kms *mdp_kms,
		const struct mdp_kms_funcs *funcs)
{
	mdp_kms->funcs = funcs;
	INIT_LIST_HEAD(&mdp_kms->irq_list);
	return msm_kms_init(&mdp_kms->base, &funcs->base);
}

static inline void mdp_kms_destroy(struct mdp_kms *mdp_kms)
{
	msm_kms_destroy(&mdp_kms->base);
}

/*
 * irq helpers:
 */

/* For transiently registering for different MDP irqs that various parts
 * of the KMS code need during setup/configuration.  These are not
 * necessarily the same as what drm_vblank_get/put() are requesting, and
 * the hysteresis in drm_vblank_put() is not necessarily desirable for
 * internal housekeeping related irq usage.
 */
struct mdp_irq {
	struct list_head node;
	uint32_t irqmask;
	bool registered;
	void (*irq)(struct mdp_irq *irq, uint32_t irqstatus);
};

void mdp_dispatch_irqs(struct mdp_kms *mdp_kms, uint32_t status);
void mdp_update_vblank_mask(struct mdp_kms *mdp_kms, uint32_t mask, bool enable);
void mdp_irq_wait(struct mdp_kms *mdp_kms, uint32_t irqmask);
void mdp_irq_register(struct mdp_kms *mdp_kms, struct mdp_irq *irq);
void mdp_irq_unregister(struct mdp_kms *mdp_kms, struct mdp_irq *irq);
void mdp_irq_update(struct mdp_kms *mdp_kms);

/*
 * pixel format helpers:
 */

/* MDP capabilities */
#define MDP_CAP_SMP		BIT(0)	/* Shared Memory Pool                 */
#define MDP_CAP_DSC		BIT(1)	/* VESA Display Stream Compression    */
#define MDP_CAP_CDM		BIT(2)	/* Chroma Down Module (HDMI 2.0 YUV)  */
#define MDP_CAP_SRC_SPLIT	BIT(3)	/* Source Split of SSPPs */

/* MDP pipe capabilities */
#define MDP_PIPE_CAP_HFLIP			BIT(0)
#define MDP_PIPE_CAP_VFLIP			BIT(1)
#define MDP_PIPE_CAP_SCALE			BIT(2)
#define MDP_PIPE_CAP_CSC			BIT(3)
#define MDP_PIPE_CAP_DECIMATION			BIT(4)
#define MDP_PIPE_CAP_SW_PIX_EXT			BIT(5)
#define MDP_PIPE_CAP_CURSOR			BIT(6)

/* MDP layer mixer caps */
#define MDP_LM_CAP_DISPLAY			BIT(0)
#define MDP_LM_CAP_WB				BIT(1)
#define MDP_LM_CAP_PAIR				BIT(2)

static inline bool pipe_supports_yuv(uint32_t pipe_caps)
{
	return (pipe_caps & MDP_PIPE_CAP_SCALE) &&
		(pipe_caps & MDP_PIPE_CAP_CSC);
}

enum csc_type {
	CSC_RGB2RGB = 0,
	CSC_YUV2RGB,
	CSC_RGB2YUV,
	CSC_YUV2YUV,
	CSC_MAX
};

struct csc_cfg {
	enum csc_type type;
	uint32_t matrix[9];
	uint32_t pre_bias[3];
	uint32_t post_bias[3];
	uint32_t pre_clamp[6];
	uint32_t post_clamp[6];
};

struct csc_cfg *mdp_get_default_csc_cfg(enum csc_type);

#endif /* __MDP_KMS_H__ */
