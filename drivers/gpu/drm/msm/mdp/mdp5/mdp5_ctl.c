/*
 * Copyright (c) 2014-2015 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mdp5_kms.h"
#include "mdp5_ctl.h"

/*
 * CTL - MDP Control Pool Manager
 *
 * Controls are shared between all CRTCs.
 *
 * They are intended to be used for data path configuration.
 * The top level register programming describes the complete data path for
 * a specific data path ID - REG_MDP5_CTL_*(<id>, ...)
 *
 * Hardware capabilities determine the number of concurrent data paths
 *
 * In certain use cases (high-resolution dual pipe), one single CTL can be
 * shared across multiple CRTCs.
 *
 * Because the number of CTLs can be less than the number of CRTCs,
 * CTLs are dynamically allocated from a pool of CTLs, only once a CRTC is
 * requested by the client (in mdp5_crtc_mode_set()).
 */

struct op_mode {
	struct mdp5_interface intf;

	bool encoder_enabled;
	uint32_t start_mask;
};

struct mdp5_ctl {
	struct mdp5_ctl_manager *ctlm;

	u32 id;
	int lm;

	/* whether this CTL has been allocated or not: */
	bool busy;

	/* Operation Mode Configuration for the Pipeline */
	struct op_mode pipeline;

	/* REG_MDP5_CTL_*(<id>) registers access info + lock: */
	spinlock_t hw_lock;
	u32 reg_offset;

	/* when do CTL registers need to be flushed? (mask of trigger bits) */
	u32 pending_ctl_trigger;

	bool cursor_on;

	struct drm_crtc *crtc;
};

struct mdp5_ctl_manager {
	struct drm_device *dev;

	/* number of CTL / Layer Mixers in this hw config: */
	u32 nlm;
	u32 nctl;

	/* to filter out non-present bits in the current hardware config */
	u32 flush_hw_mask;

	/* pool of CTLs + lock to protect resource allocation (ctls[i].busy) */
	spinlock_t pool_lock;
	struct mdp5_ctl ctls[MAX_CTL];
};

static inline
struct mdp5_kms *get_kms(struct mdp5_ctl_manager *ctl_mgr)
{
	struct msm_drm_private *priv = ctl_mgr->dev->dev_private;

	return to_mdp5_kms(to_mdp_kms(priv->kms));
}

static inline
void ctl_write(struct mdp5_ctl *ctl, u32 reg, u32 data)
{
	struct mdp5_kms *mdp5_kms = get_kms(ctl->ctlm);

	(void)ctl->reg_offset; /* TODO use this instead of mdp5_write */
	mdp5_write(mdp5_kms, reg, data);
}

static inline
u32 ctl_read(struct mdp5_ctl *ctl, u32 reg)
{
	struct mdp5_kms *mdp5_kms = get_kms(ctl->ctlm);

	(void)ctl->reg_offset; /* TODO use this instead of mdp5_write */
	return mdp5_read(mdp5_kms, reg);
}

static void set_display_intf(struct mdp5_kms *mdp5_kms,
		struct mdp5_interface *intf)
{
	unsigned long flags;
	u32 intf_sel;

	spin_lock_irqsave(&mdp5_kms->resource_lock, flags);
	intf_sel = mdp5_read(mdp5_kms, REG_MDP5_MDP_DISP_INTF_SEL(0));

	switch (intf->num) {
	case 0:
		intf_sel &= ~MDP5_MDP_DISP_INTF_SEL_INTF0__MASK;
		intf_sel |= MDP5_MDP_DISP_INTF_SEL_INTF0(intf->type);
		break;
	case 1:
		intf_sel &= ~MDP5_MDP_DISP_INTF_SEL_INTF1__MASK;
		intf_sel |= MDP5_MDP_DISP_INTF_SEL_INTF1(intf->type);
		break;
	case 2:
		intf_sel &= ~MDP5_MDP_DISP_INTF_SEL_INTF2__MASK;
		intf_sel |= MDP5_MDP_DISP_INTF_SEL_INTF2(intf->type);
		break;
	case 3:
		intf_sel &= ~MDP5_MDP_DISP_INTF_SEL_INTF3__MASK;
		intf_sel |= MDP5_MDP_DISP_INTF_SEL_INTF3(intf->type);
		break;
	default:
		BUG();
		break;
	}

	mdp5_write(mdp5_kms, REG_MDP5_MDP_DISP_INTF_SEL(0), intf_sel);
	spin_unlock_irqrestore(&mdp5_kms->resource_lock, flags);
}

static void set_ctl_op(struct mdp5_ctl *ctl, struct mdp5_interface *intf)
{
	unsigned long flags;
	u32 ctl_op = 0;

	if (!mdp5_cfg_intf_is_virtual(intf->type))
		ctl_op |= MDP5_CTL_OP_INTF_NUM(INTF0 + intf->num);

	switch (intf->type) {
	case INTF_DSI:
		if (intf->mode == MDP5_INTF_DSI_MODE_COMMAND)
			ctl_op |= MDP5_CTL_OP_CMD_MODE;
		break;

	case INTF_WB:
		if (intf->mode == MDP5_INTF_WB_MODE_LINE)
			ctl_op |= MDP5_CTL_OP_MODE(MODE_WB_2_LINE);
		break;

	default:
		break;
	}

	spin_lock_irqsave(&ctl->hw_lock, flags);
	ctl_write(ctl, REG_MDP5_CTL_OP(ctl->id), ctl_op);
	spin_unlock_irqrestore(&ctl->hw_lock, flags);
}

int mdp5_ctl_set_intf(struct mdp5_ctl *ctl, struct mdp5_interface *intf)
{
	struct mdp5_ctl_manager *ctl_mgr = ctl->ctlm;
	struct mdp5_kms *mdp5_kms = get_kms(ctl_mgr);

	memcpy(&ctl->pipeline.intf, intf, sizeof(*intf));

	ctl->pipeline.start_mask = mdp_ctl_flush_mask_lm(ctl->lm) |
				   mdp_ctl_flush_mask_encoder(intf);

	/* Virtual interfaces need not set a display intf (e.g.: Writeback) */
	if (!mdp5_cfg_intf_is_virtual(intf->type))
		set_display_intf(mdp5_kms, intf);

	set_ctl_op(ctl, intf);

	return 0;
}

static bool start_signal_needed(struct mdp5_ctl *ctl)
{
	struct op_mode *pipeline = &ctl->pipeline;

	if (!pipeline->encoder_enabled || pipeline->start_mask != 0)
		return false;

	switch (pipeline->intf.type) {
	case INTF_WB:
		return true;
	case INTF_DSI:
		return pipeline->intf.mode == MDP5_INTF_DSI_MODE_COMMAND;
	default:
		return false;
	}
}

/*
 * send_start_signal() - Overlay Processor Start Signal
 *
 * For a given control operation (display pipeline), a START signal needs to be
 * executed in order to kick off operation and activate all layers.
 * e.g.: DSI command mode, Writeback
 */
static void send_start_signal(struct mdp5_ctl *ctl)
{
	unsigned long flags;

	spin_lock_irqsave(&ctl->hw_lock, flags);
	ctl_write(ctl, REG_MDP5_CTL_START(ctl->id), 1);
	spin_unlock_irqrestore(&ctl->hw_lock, flags);
}

static void refill_start_mask(struct mdp5_ctl *ctl)
{
	struct op_mode *pipeline = &ctl->pipeline;
	struct mdp5_interface *intf = &ctl->pipeline.intf;

	pipeline->start_mask = mdp_ctl_flush_mask_lm(ctl->lm);

	/*
	 * Writeback encoder needs to program & flush
	 * address registers for each page flip..
	 */
	if (intf->type == INTF_WB)
		pipeline->start_mask |= mdp_ctl_flush_mask_encoder(intf);
}

/**
 * mdp5_ctl_set_encoder_state() - set the encoder state
 *
 * @enable: true, when encoder is ready for data streaming; false, otherwise.
 *
 * Note:
 * This encoder state is needed to trigger START signal (data path kickoff).
 */
int mdp5_ctl_set_encoder_state(struct mdp5_ctl *ctl, bool enabled)
{
	if (WARN_ON(!ctl))
		return -EINVAL;

	ctl->pipeline.encoder_enabled = enabled;
	DBG("intf_%d: %s", ctl->pipeline.intf.num, enabled ? "on" : "off");

	if (start_signal_needed(ctl)) {
		send_start_signal(ctl);
		refill_start_mask(ctl);
	}

	return 0;
}

/*
 * Note:
 * CTL registers need to be flushed after calling this function
 * (call mdp5_ctl_commit() with mdp_ctl_flush_mask_ctl() mask)
 */
int mdp5_ctl_set_cursor(struct mdp5_ctl *ctl, int cursor_id, bool enable)
{
	struct mdp5_ctl_manager *ctl_mgr = ctl->ctlm;
	unsigned long flags;
	u32 blend_cfg;
	int lm = ctl->lm;

	if (unlikely(WARN_ON(lm < 0))) {
		dev_err(ctl_mgr->dev->dev, "CTL %d cannot find LM: %d",
				ctl->id, lm);
		return -EINVAL;
	}

	spin_lock_irqsave(&ctl->hw_lock, flags);

	blend_cfg = ctl_read(ctl, REG_MDP5_CTL_LAYER_REG(ctl->id, lm));

	if (enable)
		blend_cfg |=  MDP5_CTL_LAYER_REG_CURSOR_OUT;
	else
		blend_cfg &= ~MDP5_CTL_LAYER_REG_CURSOR_OUT;

	ctl_write(ctl, REG_MDP5_CTL_LAYER_REG(ctl->id, lm), blend_cfg);

	spin_unlock_irqrestore(&ctl->hw_lock, flags);

	ctl->pending_ctl_trigger = mdp_ctl_flush_mask_cursor(cursor_id);
	ctl->cursor_on = enable;

	return 0;
}

int mdp5_ctl_blend(struct mdp5_ctl *ctl, u32 lm, u32 blend_cfg)
{
	unsigned long flags;

	if (ctl->cursor_on)
		blend_cfg |=  MDP5_CTL_LAYER_REG_CURSOR_OUT;
	else
		blend_cfg &= ~MDP5_CTL_LAYER_REG_CURSOR_OUT;

	spin_lock_irqsave(&ctl->hw_lock, flags);
	ctl_write(ctl, REG_MDP5_CTL_LAYER_REG(ctl->id, lm), blend_cfg);
	spin_unlock_irqrestore(&ctl->hw_lock, flags);

	ctl->pending_ctl_trigger = mdp_ctl_flush_mask_lm(lm);

	return 0;
}

u32 mdp_ctl_flush_mask_encoder(struct mdp5_interface *intf)
{
	if (intf->type == INTF_WB)
		return MDP5_CTL_FLUSH_WB;

	switch (intf->num) {
	case 0: return MDP5_CTL_FLUSH_TIMING_0;
	case 1: return MDP5_CTL_FLUSH_TIMING_1;
	case 2: return MDP5_CTL_FLUSH_TIMING_2;
	case 3: return MDP5_CTL_FLUSH_TIMING_3;
	default: return 0;
	}
}

u32 mdp_ctl_flush_mask_cursor(int cursor_id)
{
	switch (cursor_id) {
	case 0: return MDP5_CTL_FLUSH_CURSOR_0;
	case 1: return MDP5_CTL_FLUSH_CURSOR_1;
	default: return 0;
	}
}

u32 mdp_ctl_flush_mask_pipe(enum mdp5_pipe pipe)
{
	switch (pipe) {
	case SSPP_VIG0: return MDP5_CTL_FLUSH_VIG0;
	case SSPP_VIG1: return MDP5_CTL_FLUSH_VIG1;
	case SSPP_VIG2: return MDP5_CTL_FLUSH_VIG2;
	case SSPP_RGB0: return MDP5_CTL_FLUSH_RGB0;
	case SSPP_RGB1: return MDP5_CTL_FLUSH_RGB1;
	case SSPP_RGB2: return MDP5_CTL_FLUSH_RGB2;
	case SSPP_DMA0: return MDP5_CTL_FLUSH_DMA0;
	case SSPP_DMA1: return MDP5_CTL_FLUSH_DMA1;
	case SSPP_VIG3: return MDP5_CTL_FLUSH_VIG3;
	case SSPP_RGB3: return MDP5_CTL_FLUSH_RGB3;
	default:        return 0;
	}
}

u32 mdp_ctl_flush_mask_lm(int lm)
{
	switch (lm) {
	case 0:  return MDP5_CTL_FLUSH_LM0;
	case 1:  return MDP5_CTL_FLUSH_LM1;
	case 2:  return MDP5_CTL_FLUSH_LM2;
	case 5:  return MDP5_CTL_FLUSH_LM5;
	default: return 0;
	}
}

static u32 fix_sw_flush(struct mdp5_ctl *ctl, u32 flush_mask)
{
	struct mdp5_ctl_manager *ctl_mgr = ctl->ctlm;
	u32 sw_mask = 0;
#define BIT_NEEDS_SW_FIX(bit) \
	(!(ctl_mgr->flush_hw_mask & bit) && (flush_mask & bit))

	/* for some targets, cursor bit is the same as LM bit */
	if (BIT_NEEDS_SW_FIX(MDP5_CTL_FLUSH_CURSOR_0))
		sw_mask |= mdp_ctl_flush_mask_lm(ctl->lm);

	return sw_mask;
}

/**
 * mdp5_ctl_commit() - Register Flush
 *
 * The flush register is used to indicate several registers are all
 * programmed, and are safe to update to the back copy of the double
 * buffered registers.
 *
 * Some registers FLUSH bits are shared when the hardware does not have
 * dedicated bits for them; handling these is the job of fix_sw_flush().
 *
 * CTL registers need to be flushed in some circumstances; if that is the
 * case, some trigger bits will be present in both flush mask and
 * ctl->pending_ctl_trigger.
 */
int mdp5_ctl_commit(struct mdp5_ctl *ctl, u32 flush_mask)
{
	struct mdp5_ctl_manager *ctl_mgr = ctl->ctlm;
	struct op_mode *pipeline = &ctl->pipeline;
	unsigned long flags;

	pipeline->start_mask &= ~flush_mask;

	VERB("flush_mask=%x, start_mask=%x, trigger=%x", flush_mask,
			pipeline->start_mask, ctl->pending_ctl_trigger);

	if (ctl->pending_ctl_trigger & flush_mask) {
		flush_mask |= MDP5_CTL_FLUSH_CTL;
		ctl->pending_ctl_trigger = 0;
	}

	flush_mask |= fix_sw_flush(ctl, flush_mask);

	flush_mask &= ctl_mgr->flush_hw_mask;

	if (flush_mask) {
		spin_lock_irqsave(&ctl->hw_lock, flags);
		ctl_write(ctl, REG_MDP5_CTL_FLUSH(ctl->id), flush_mask);
		spin_unlock_irqrestore(&ctl->hw_lock, flags);
	}

	if (start_signal_needed(ctl)) {
		send_start_signal(ctl);
		refill_start_mask(ctl);
	}

	return 0;
}

void mdp5_ctl_release(struct mdp5_ctl *ctl)
{
	struct mdp5_ctl_manager *ctl_mgr = ctl->ctlm;
	unsigned long flags;

	if (unlikely(WARN_ON(ctl->id >= MAX_CTL) || !ctl->busy)) {
		dev_err(ctl_mgr->dev->dev, "CTL %d in bad state (%d)",
				ctl->id, ctl->busy);
		return;
	}

	spin_lock_irqsave(&ctl_mgr->pool_lock, flags);
	ctl->busy = false;
	spin_unlock_irqrestore(&ctl_mgr->pool_lock, flags);

	DBG("CTL %d released", ctl->id);
}

int mdp5_ctl_get_ctl_id(struct mdp5_ctl *ctl)
{
	return WARN_ON(!ctl) ? -EINVAL : ctl->id;
}

/*
 * mdp5_ctl_request() - CTL dynamic allocation
 *
 * Note: Current implementation considers that we can only have one CRTC per CTL
 *
 * @return first free CTL
 */
struct mdp5_ctl *mdp5_ctlm_request(struct mdp5_ctl_manager *ctl_mgr,
		struct drm_crtc *crtc)
{
	struct mdp5_ctl *ctl = NULL;
	unsigned long flags;
	int c;

	spin_lock_irqsave(&ctl_mgr->pool_lock, flags);

	for (c = 0; c < ctl_mgr->nctl; c++)
		if (!ctl_mgr->ctls[c].busy)
			break;

	if (unlikely(c >= ctl_mgr->nctl)) {
		dev_err(ctl_mgr->dev->dev, "No more CTL available!");
		goto unlock;
	}

	ctl = &ctl_mgr->ctls[c];

	ctl->lm = mdp5_crtc_get_lm(crtc);
	ctl->crtc = crtc;
	ctl->busy = true;
	ctl->pending_ctl_trigger = 0;
	DBG("CTL %d allocated", ctl->id);

unlock:
	spin_unlock_irqrestore(&ctl_mgr->pool_lock, flags);
	return ctl;
}

void mdp5_ctlm_hw_reset(struct mdp5_ctl_manager *ctl_mgr)
{
	unsigned long flags;
	int c;

	for (c = 0; c < ctl_mgr->nctl; c++) {
		struct mdp5_ctl *ctl = &ctl_mgr->ctls[c];

		spin_lock_irqsave(&ctl->hw_lock, flags);
		ctl_write(ctl, REG_MDP5_CTL_OP(ctl->id), 0);
		spin_unlock_irqrestore(&ctl->hw_lock, flags);
	}
}

void mdp5_ctlm_destroy(struct mdp5_ctl_manager *ctl_mgr)
{
	kfree(ctl_mgr);
}

struct mdp5_ctl_manager *mdp5_ctlm_init(struct drm_device *dev,
		void __iomem *mmio_base, const struct mdp5_cfg_hw *hw_cfg)
{
	struct mdp5_ctl_manager *ctl_mgr;
	const struct mdp5_ctl_block *ctl_cfg = &hw_cfg->ctl;
	unsigned long flags;
	int c, ret;

	ctl_mgr = kzalloc(sizeof(*ctl_mgr), GFP_KERNEL);
	if (!ctl_mgr) {
		dev_err(dev->dev, "failed to allocate CTL manager\n");
		ret = -ENOMEM;
		goto fail;
	}

	if (unlikely(WARN_ON(ctl_cfg->count > MAX_CTL))) {
		dev_err(dev->dev, "Increase static pool size to at least %d\n",
				ctl_cfg->count);
		ret = -ENOSPC;
		goto fail;
	}

	/* initialize the CTL manager: */
	ctl_mgr->dev = dev;
	ctl_mgr->nlm = hw_cfg->lm.count;
	ctl_mgr->nctl = ctl_cfg->count;
	ctl_mgr->flush_hw_mask = ctl_cfg->flush_hw_mask;
	spin_lock_init(&ctl_mgr->pool_lock);

	/* initialize each CTL of the pool: */
	spin_lock_irqsave(&ctl_mgr->pool_lock, flags);
	for (c = 0; c < ctl_mgr->nctl; c++) {
		struct mdp5_ctl *ctl = &ctl_mgr->ctls[c];

		if (WARN_ON(!ctl_cfg->base[c])) {
			dev_err(dev->dev, "CTL_%d: base is null!\n", c);
			ret = -EINVAL;
			goto fail;
		}
		ctl->ctlm = ctl_mgr;
		ctl->id = c;
		ctl->reg_offset = ctl_cfg->base[c];
		ctl->busy = false;
		spin_lock_init(&ctl->hw_lock);
	}
	spin_unlock_irqrestore(&ctl_mgr->pool_lock, flags);
	DBG("Pool of %d CTLs created.", ctl_mgr->nctl);

	return ctl_mgr;

fail:
	if (ctl_mgr)
		mdp5_ctlm_destroy(ctl_mgr);

	return ERR_PTR(ret);
}
