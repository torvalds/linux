/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Author: Damien Lespiau <damien.lespiau@intel.com>
 *
 */

#include <linux/seq_file.h>
#include <linux/circ_buf.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include "intel_drv.h"

struct pipe_crc_info {
	const char *name;
	struct drm_i915_private *dev_priv;
	enum pipe pipe;
};

/* As the drm_debugfs_init() routines are called before dev->dev_private is
 * allocated we need to hook into the minor for release.
 */
static int drm_add_fake_info_node(struct drm_minor *minor,
				  struct dentry *ent, const void *key)
{
	struct drm_info_node *node;

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (node == NULL) {
		debugfs_remove(ent);
		return -ENOMEM;
	}

	node->minor = minor;
	node->dent = ent;
	node->info_ent = (void *) key;

	mutex_lock(&minor->debugfs_lock);
	list_add(&node->list, &minor->debugfs_list);
	mutex_unlock(&minor->debugfs_lock);

	return 0;
}

static int i915_pipe_crc_open(struct inode *inode, struct file *filep)
{
	struct pipe_crc_info *info = inode->i_private;
	struct drm_i915_private *dev_priv = info->dev_priv;
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[info->pipe];

	if (info->pipe >= INTEL_INFO(dev_priv)->num_pipes)
		return -ENODEV;

	spin_lock_irq(&pipe_crc->lock);

	if (pipe_crc->opened) {
		spin_unlock_irq(&pipe_crc->lock);
		return -EBUSY; /* already open */
	}

	pipe_crc->opened = true;
	filep->private_data = inode->i_private;

	spin_unlock_irq(&pipe_crc->lock);

	return 0;
}

static int i915_pipe_crc_release(struct inode *inode, struct file *filep)
{
	struct pipe_crc_info *info = inode->i_private;
	struct drm_i915_private *dev_priv = info->dev_priv;
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[info->pipe];

	spin_lock_irq(&pipe_crc->lock);
	pipe_crc->opened = false;
	spin_unlock_irq(&pipe_crc->lock);

	return 0;
}

/* (6 fields, 8 chars each, space separated (5) + '\n') */
#define PIPE_CRC_LINE_LEN	(6 * 8 + 5 + 1)
/* account for \'0' */
#define PIPE_CRC_BUFFER_LEN	(PIPE_CRC_LINE_LEN + 1)

static int pipe_crc_data_count(struct intel_pipe_crc *pipe_crc)
{
	assert_spin_locked(&pipe_crc->lock);
	return CIRC_CNT(pipe_crc->head, pipe_crc->tail,
			INTEL_PIPE_CRC_ENTRIES_NR);
}

static ssize_t
i915_pipe_crc_read(struct file *filep, char __user *user_buf, size_t count,
		   loff_t *pos)
{
	struct pipe_crc_info *info = filep->private_data;
	struct drm_i915_private *dev_priv = info->dev_priv;
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[info->pipe];
	char buf[PIPE_CRC_BUFFER_LEN];
	int n_entries;
	ssize_t bytes_read;

	/*
	 * Don't allow user space to provide buffers not big enough to hold
	 * a line of data.
	 */
	if (count < PIPE_CRC_LINE_LEN)
		return -EINVAL;

	if (pipe_crc->source == INTEL_PIPE_CRC_SOURCE_NONE)
		return 0;

	/* nothing to read */
	spin_lock_irq(&pipe_crc->lock);
	while (pipe_crc_data_count(pipe_crc) == 0) {
		int ret;

		if (filep->f_flags & O_NONBLOCK) {
			spin_unlock_irq(&pipe_crc->lock);
			return -EAGAIN;
		}

		ret = wait_event_interruptible_lock_irq(pipe_crc->wq,
				pipe_crc_data_count(pipe_crc), pipe_crc->lock);
		if (ret) {
			spin_unlock_irq(&pipe_crc->lock);
			return ret;
		}
	}

	/* We now have one or more entries to read */
	n_entries = count / PIPE_CRC_LINE_LEN;

	bytes_read = 0;
	while (n_entries > 0) {
		struct intel_pipe_crc_entry *entry =
			&pipe_crc->entries[pipe_crc->tail];

		if (CIRC_CNT(pipe_crc->head, pipe_crc->tail,
			     INTEL_PIPE_CRC_ENTRIES_NR) < 1)
			break;

		BUILD_BUG_ON_NOT_POWER_OF_2(INTEL_PIPE_CRC_ENTRIES_NR);
		pipe_crc->tail = (pipe_crc->tail + 1) &
				 (INTEL_PIPE_CRC_ENTRIES_NR - 1);

		bytes_read += snprintf(buf, PIPE_CRC_BUFFER_LEN,
				       "%8u %8x %8x %8x %8x %8x\n",
				       entry->frame, entry->crc[0],
				       entry->crc[1], entry->crc[2],
				       entry->crc[3], entry->crc[4]);

		spin_unlock_irq(&pipe_crc->lock);

		if (copy_to_user(user_buf, buf, PIPE_CRC_LINE_LEN))
			return -EFAULT;

		user_buf += PIPE_CRC_LINE_LEN;
		n_entries--;

		spin_lock_irq(&pipe_crc->lock);
	}

	spin_unlock_irq(&pipe_crc->lock);

	return bytes_read;
}

static const struct file_operations i915_pipe_crc_fops = {
	.owner = THIS_MODULE,
	.open = i915_pipe_crc_open,
	.read = i915_pipe_crc_read,
	.release = i915_pipe_crc_release,
};

static struct pipe_crc_info i915_pipe_crc_data[I915_MAX_PIPES] = {
	{
		.name = "i915_pipe_A_crc",
		.pipe = PIPE_A,
	},
	{
		.name = "i915_pipe_B_crc",
		.pipe = PIPE_B,
	},
	{
		.name = "i915_pipe_C_crc",
		.pipe = PIPE_C,
	},
};

static int i915_pipe_crc_create(struct dentry *root, struct drm_minor *minor,
				enum pipe pipe)
{
	struct drm_i915_private *dev_priv = to_i915(minor->dev);
	struct dentry *ent;
	struct pipe_crc_info *info = &i915_pipe_crc_data[pipe];

	info->dev_priv = dev_priv;
	ent = debugfs_create_file(info->name, S_IRUGO, root, info,
				  &i915_pipe_crc_fops);
	if (!ent)
		return -ENOMEM;

	return drm_add_fake_info_node(minor, ent, info);
}

static const char * const pipe_crc_sources[] = {
	"none",
	"plane1",
	"plane2",
	"pf",
	"pipe",
	"TV",
	"DP-B",
	"DP-C",
	"DP-D",
	"auto",
};

static const char *pipe_crc_source_name(enum intel_pipe_crc_source source)
{
	BUILD_BUG_ON(ARRAY_SIZE(pipe_crc_sources) != INTEL_PIPE_CRC_SOURCE_MAX);
	return pipe_crc_sources[source];
}

static int display_crc_ctl_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	int i;

	for (i = 0; i < I915_MAX_PIPES; i++)
		seq_printf(m, "%c %s\n", pipe_name(i),
			   pipe_crc_source_name(dev_priv->pipe_crc[i].source));

	return 0;
}

static int display_crc_ctl_open(struct inode *inode, struct file *file)
{
	return single_open(file, display_crc_ctl_show, inode->i_private);
}

static int i8xx_pipe_crc_ctl_reg(enum intel_pipe_crc_source *source,
				 uint32_t *val)
{
	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		*source = INTEL_PIPE_CRC_SOURCE_PIPE;

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_INCLUDE_BORDER_I8XX;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int i9xx_pipe_crc_auto_source(struct drm_i915_private *dev_priv,
				     enum pipe pipe,
				     enum intel_pipe_crc_source *source)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_encoder *encoder;
	struct intel_crtc *crtc;
	struct intel_digital_port *dig_port;
	int ret = 0;

	*source = INTEL_PIPE_CRC_SOURCE_PIPE;

	drm_modeset_lock_all(dev);
	for_each_intel_encoder(dev, encoder) {
		if (!encoder->base.crtc)
			continue;

		crtc = to_intel_crtc(encoder->base.crtc);

		if (crtc->pipe != pipe)
			continue;

		switch (encoder->type) {
		case INTEL_OUTPUT_TVOUT:
			*source = INTEL_PIPE_CRC_SOURCE_TV;
			break;
		case INTEL_OUTPUT_DP:
		case INTEL_OUTPUT_EDP:
			dig_port = enc_to_dig_port(&encoder->base);
			switch (dig_port->port) {
			case PORT_B:
				*source = INTEL_PIPE_CRC_SOURCE_DP_B;
				break;
			case PORT_C:
				*source = INTEL_PIPE_CRC_SOURCE_DP_C;
				break;
			case PORT_D:
				*source = INTEL_PIPE_CRC_SOURCE_DP_D;
				break;
			default:
				WARN(1, "nonexisting DP port %c\n",
				     port_name(dig_port->port));
				break;
			}
			break;
		default:
			break;
		}
	}
	drm_modeset_unlock_all(dev);

	return ret;
}

static int vlv_pipe_crc_ctl_reg(struct drm_i915_private *dev_priv,
				enum pipe pipe,
				enum intel_pipe_crc_source *source,
				uint32_t *val)
{
	bool need_stable_symbols = false;

	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO) {
		int ret = i9xx_pipe_crc_auto_source(dev_priv, pipe, source);
		if (ret)
			return ret;
	}

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PIPE_VLV;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_B:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_B_VLV;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_C:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_C_VLV;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_D:
		if (!IS_CHERRYVIEW(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_D_VLV;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * When the pipe CRC tap point is after the transcoders we need
	 * to tweak symbol-level features to produce a deterministic series of
	 * symbols for a given frame. We need to reset those features only once
	 * a frame (instead of every nth symbol):
	 *   - DC-balance: used to ensure a better clock recovery from the data
	 *     link (SDVO)
	 *   - DisplayPort scrambling: used for EMI reduction
	 */
	if (need_stable_symbols) {
		uint32_t tmp = I915_READ(PORT_DFT2_G4X);

		tmp |= DC_BALANCE_RESET_VLV;
		switch (pipe) {
		case PIPE_A:
			tmp |= PIPE_A_SCRAMBLE_RESET;
			break;
		case PIPE_B:
			tmp |= PIPE_B_SCRAMBLE_RESET;
			break;
		case PIPE_C:
			tmp |= PIPE_C_SCRAMBLE_RESET;
			break;
		default:
			return -EINVAL;
		}
		I915_WRITE(PORT_DFT2_G4X, tmp);
	}

	return 0;
}

static int i9xx_pipe_crc_ctl_reg(struct drm_i915_private *dev_priv,
				 enum pipe pipe,
				 enum intel_pipe_crc_source *source,
				 uint32_t *val)
{
	bool need_stable_symbols = false;

	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO) {
		int ret = i9xx_pipe_crc_auto_source(dev_priv, pipe, source);
		if (ret)
			return ret;
	}

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PIPE_I9XX;
		break;
	case INTEL_PIPE_CRC_SOURCE_TV:
		if (!SUPPORTS_TV(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_TV_PRE;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_B:
		if (!IS_G4X(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_B_G4X;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_C:
		if (!IS_G4X(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_C_G4X;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_D:
		if (!IS_G4X(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_D_G4X;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * When the pipe CRC tap point is after the transcoders we need
	 * to tweak symbol-level features to produce a deterministic series of
	 * symbols for a given frame. We need to reset those features only once
	 * a frame (instead of every nth symbol):
	 *   - DC-balance: used to ensure a better clock recovery from the data
	 *     link (SDVO)
	 *   - DisplayPort scrambling: used for EMI reduction
	 */
	if (need_stable_symbols) {
		uint32_t tmp = I915_READ(PORT_DFT2_G4X);

		WARN_ON(!IS_G4X(dev_priv));

		I915_WRITE(PORT_DFT_I9XX,
			   I915_READ(PORT_DFT_I9XX) | DC_BALANCE_RESET);

		if (pipe == PIPE_A)
			tmp |= PIPE_A_SCRAMBLE_RESET;
		else
			tmp |= PIPE_B_SCRAMBLE_RESET;

		I915_WRITE(PORT_DFT2_G4X, tmp);
	}

	return 0;
}

static void vlv_undo_pipe_scramble_reset(struct drm_i915_private *dev_priv,
					 enum pipe pipe)
{
	uint32_t tmp = I915_READ(PORT_DFT2_G4X);

	switch (pipe) {
	case PIPE_A:
		tmp &= ~PIPE_A_SCRAMBLE_RESET;
		break;
	case PIPE_B:
		tmp &= ~PIPE_B_SCRAMBLE_RESET;
		break;
	case PIPE_C:
		tmp &= ~PIPE_C_SCRAMBLE_RESET;
		break;
	default:
		return;
	}
	if (!(tmp & PIPE_SCRAMBLE_RESET_MASK))
		tmp &= ~DC_BALANCE_RESET_VLV;
	I915_WRITE(PORT_DFT2_G4X, tmp);

}

static void g4x_undo_pipe_scramble_reset(struct drm_i915_private *dev_priv,
					 enum pipe pipe)
{
	uint32_t tmp = I915_READ(PORT_DFT2_G4X);

	if (pipe == PIPE_A)
		tmp &= ~PIPE_A_SCRAMBLE_RESET;
	else
		tmp &= ~PIPE_B_SCRAMBLE_RESET;
	I915_WRITE(PORT_DFT2_G4X, tmp);

	if (!(tmp & PIPE_SCRAMBLE_RESET_MASK)) {
		I915_WRITE(PORT_DFT_I9XX,
			   I915_READ(PORT_DFT_I9XX) & ~DC_BALANCE_RESET);
	}
}

static int ilk_pipe_crc_ctl_reg(enum intel_pipe_crc_source *source,
				uint32_t *val)
{
	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		*source = INTEL_PIPE_CRC_SOURCE_PIPE;

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PLANE1:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PRIMARY_ILK;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE2:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_SPRITE_ILK;
		break;
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PIPE_ILK;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void hsw_trans_edp_pipe_A_crc_wa(struct drm_i915_private *dev_priv,
					bool enable)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv, PIPE_A);
	struct intel_crtc_state *pipe_config;
	struct drm_atomic_state *state;
	int ret = 0;

	drm_modeset_lock_all(dev);
	state = drm_atomic_state_alloc(dev);
	if (!state) {
		ret = -ENOMEM;
		goto unlock;
	}

	state->acquire_ctx = drm_modeset_legacy_acquire_ctx(&crtc->base);
	pipe_config = intel_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(pipe_config)) {
		ret = PTR_ERR(pipe_config);
		goto put_state;
	}

	pipe_config->pch_pfit.force_thru = enable;
	if (pipe_config->cpu_transcoder == TRANSCODER_EDP &&
	    pipe_config->pch_pfit.enabled != enable)
		pipe_config->base.connectors_changed = true;

	ret = drm_atomic_commit(state);

put_state:
	drm_atomic_state_put(state);
unlock:
	WARN(ret, "Toggling workaround to %i returns %i\n", enable, ret);
	drm_modeset_unlock_all(dev);
}

static int ivb_pipe_crc_ctl_reg(struct drm_i915_private *dev_priv,
				enum pipe pipe,
				enum intel_pipe_crc_source *source,
				uint32_t *val)
{
	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		*source = INTEL_PIPE_CRC_SOURCE_PF;

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PLANE1:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PRIMARY_IVB;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE2:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_SPRITE_IVB;
		break;
	case INTEL_PIPE_CRC_SOURCE_PF:
		if (IS_HASWELL(dev_priv) && pipe == PIPE_A)
			hsw_trans_edp_pipe_A_crc_wa(dev_priv, true);

		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PF_IVB;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int get_new_crc_ctl_reg(struct drm_i915_private *dev_priv,
			       enum pipe pipe,
			       enum intel_pipe_crc_source *source, u32 *val)
{
	if (IS_GEN2(dev_priv))
		return i8xx_pipe_crc_ctl_reg(source, val);
	else if (INTEL_GEN(dev_priv) < 5)
		return i9xx_pipe_crc_ctl_reg(dev_priv, pipe, source, val);
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		return vlv_pipe_crc_ctl_reg(dev_priv, pipe, source, val);
	else if (IS_GEN5(dev_priv) || IS_GEN6(dev_priv))
		return ilk_pipe_crc_ctl_reg(source, val);
	else
		return ivb_pipe_crc_ctl_reg(dev_priv, pipe, source, val);
}

static int pipe_crc_set_source(struct drm_i915_private *dev_priv,
			       enum pipe pipe,
			       enum intel_pipe_crc_source source)
{
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[pipe];
	struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv, pipe);
	enum intel_display_power_domain power_domain;
	u32 val = 0; /* shut up gcc */
	int ret;

	if (pipe_crc->source == source)
		return 0;

	/* forbid changing the source without going back to 'none' */
	if (pipe_crc->source && source)
		return -EINVAL;

	power_domain = POWER_DOMAIN_PIPE(pipe);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain)) {
		DRM_DEBUG_KMS("Trying to capture CRC while pipe is off\n");
		return -EIO;
	}

	ret = get_new_crc_ctl_reg(dev_priv, pipe, &source, &val);
	if (ret != 0)
		goto out;

	/* none -> real source transition */
	if (source) {
		struct intel_pipe_crc_entry *entries;

		DRM_DEBUG_DRIVER("collecting CRCs for pipe %c, %s\n",
				 pipe_name(pipe), pipe_crc_source_name(source));

		entries = kcalloc(INTEL_PIPE_CRC_ENTRIES_NR,
				  sizeof(pipe_crc->entries[0]),
				  GFP_KERNEL);
		if (!entries) {
			ret = -ENOMEM;
			goto out;
		}

		/*
		 * When IPS gets enabled, the pipe CRC changes. Since IPS gets
		 * enabled and disabled dynamically based on package C states,
		 * user space can't make reliable use of the CRCs, so let's just
		 * completely disable it.
		 */
		hsw_disable_ips(crtc);

		spin_lock_irq(&pipe_crc->lock);
		kfree(pipe_crc->entries);
		pipe_crc->entries = entries;
		pipe_crc->head = 0;
		pipe_crc->tail = 0;
		spin_unlock_irq(&pipe_crc->lock);
	}

	pipe_crc->source = source;

	I915_WRITE(PIPE_CRC_CTL(pipe), val);
	POSTING_READ(PIPE_CRC_CTL(pipe));

	/* real source -> none transition */
	if (!source) {
		struct intel_pipe_crc_entry *entries;
		struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv,
								  pipe);

		DRM_DEBUG_DRIVER("stopping CRCs for pipe %c\n",
				 pipe_name(pipe));

		drm_modeset_lock(&crtc->base.mutex, NULL);
		if (crtc->base.state->active)
			intel_wait_for_vblank(dev_priv, pipe);
		drm_modeset_unlock(&crtc->base.mutex);

		spin_lock_irq(&pipe_crc->lock);
		entries = pipe_crc->entries;
		pipe_crc->entries = NULL;
		pipe_crc->head = 0;
		pipe_crc->tail = 0;
		spin_unlock_irq(&pipe_crc->lock);

		kfree(entries);

		if (IS_G4X(dev_priv))
			g4x_undo_pipe_scramble_reset(dev_priv, pipe);
		else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
			vlv_undo_pipe_scramble_reset(dev_priv, pipe);
		else if (IS_HASWELL(dev_priv) && pipe == PIPE_A)
			hsw_trans_edp_pipe_A_crc_wa(dev_priv, false);

		hsw_enable_ips(crtc);
	}

	ret = 0;

out:
	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

/*
 * Parse pipe CRC command strings:
 *   command: wsp* object wsp+ name wsp+ source wsp*
 *   object: 'pipe'
 *   name: (A | B | C)
 *   source: (none | plane1 | plane2 | pf)
 *   wsp: (#0x20 | #0x9 | #0xA)+
 *
 * eg.:
 *  "pipe A plane1"  ->  Start CRC computations on plane1 of pipe A
 *  "pipe A none"    ->  Stop CRC
 */
static int display_crc_ctl_tokenize(char *buf, char *words[], int max_words)
{
	int n_words = 0;

	while (*buf) {
		char *end;

		/* skip leading white space */
		buf = skip_spaces(buf);
		if (!*buf)
			break;	/* end of buffer */

		/* find end of word */
		for (end = buf; *end && !isspace(*end); end++)
			;

		if (n_words == max_words) {
			DRM_DEBUG_DRIVER("too many words, allowed <= %d\n",
					 max_words);
			return -EINVAL;	/* ran out of words[] before bytes */
		}

		if (*end)
			*end++ = '\0';
		words[n_words++] = buf;
		buf = end;
	}

	return n_words;
}

enum intel_pipe_crc_object {
	PIPE_CRC_OBJECT_PIPE,
};

static const char * const pipe_crc_objects[] = {
	"pipe",
};

static int
display_crc_ctl_parse_object(const char *buf, enum intel_pipe_crc_object *o)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pipe_crc_objects); i++)
		if (!strcmp(buf, pipe_crc_objects[i])) {
			*o = i;
			return 0;
		}

	return -EINVAL;
}

static int display_crc_ctl_parse_pipe(const char *buf, enum pipe *pipe)
{
	const char name = buf[0];

	if (name < 'A' || name >= pipe_name(I915_MAX_PIPES))
		return -EINVAL;

	*pipe = name - 'A';

	return 0;
}

static int
display_crc_ctl_parse_source(const char *buf, enum intel_pipe_crc_source *s)
{
	int i;

	if (!buf) {
		*s = INTEL_PIPE_CRC_SOURCE_NONE;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(pipe_crc_sources); i++)
		if (!strcmp(buf, pipe_crc_sources[i])) {
			*s = i;
			return 0;
		}

	return -EINVAL;
}

static int display_crc_ctl_parse(struct drm_i915_private *dev_priv,
				 char *buf, size_t len)
{
#define N_WORDS 3
	int n_words;
	char *words[N_WORDS];
	enum pipe pipe;
	enum intel_pipe_crc_object object;
	enum intel_pipe_crc_source source;

	n_words = display_crc_ctl_tokenize(buf, words, N_WORDS);
	if (n_words != N_WORDS) {
		DRM_DEBUG_DRIVER("tokenize failed, a command is %d words\n",
				 N_WORDS);
		return -EINVAL;
	}

	if (display_crc_ctl_parse_object(words[0], &object) < 0) {
		DRM_DEBUG_DRIVER("unknown object %s\n", words[0]);
		return -EINVAL;
	}

	if (display_crc_ctl_parse_pipe(words[1], &pipe) < 0) {
		DRM_DEBUG_DRIVER("unknown pipe %s\n", words[1]);
		return -EINVAL;
	}

	if (display_crc_ctl_parse_source(words[2], &source) < 0) {
		DRM_DEBUG_DRIVER("unknown source %s\n", words[2]);
		return -EINVAL;
	}

	return pipe_crc_set_source(dev_priv, pipe, source);
}

static ssize_t display_crc_ctl_write(struct file *file, const char __user *ubuf,
				     size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	char *tmpbuf;
	int ret;

	if (len == 0)
		return 0;

	if (len > PAGE_SIZE - 1) {
		DRM_DEBUG_DRIVER("expected <%lu bytes into pipe crc control\n",
				 PAGE_SIZE);
		return -E2BIG;
	}

	tmpbuf = kmalloc(len + 1, GFP_KERNEL);
	if (!tmpbuf)
		return -ENOMEM;

	if (copy_from_user(tmpbuf, ubuf, len)) {
		ret = -EFAULT;
		goto out;
	}
	tmpbuf[len] = '\0';

	ret = display_crc_ctl_parse(dev_priv, tmpbuf, len);

out:
	kfree(tmpbuf);
	if (ret < 0)
		return ret;

	*offp += len;
	return len;
}

const struct file_operations i915_display_crc_ctl_fops = {
	.owner = THIS_MODULE,
	.open = display_crc_ctl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = display_crc_ctl_write
};

void intel_display_crc_init(struct drm_i915_private *dev_priv)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[pipe];

		pipe_crc->opened = false;
		spin_lock_init(&pipe_crc->lock);
		init_waitqueue_head(&pipe_crc->wq);
	}
}

int intel_pipe_crc_create(struct drm_minor *minor)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(i915_pipe_crc_data); i++) {
		ret = i915_pipe_crc_create(minor->debugfs_root, minor, i);
		if (ret)
			return ret;
	}

	return 0;
}

void intel_pipe_crc_cleanup(struct drm_minor *minor)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(i915_pipe_crc_data); i++) {
		struct drm_info_list *info_list =
			(struct drm_info_list *)&i915_pipe_crc_data[i];

		drm_debugfs_remove_files(info_list, 1, minor);
	}
}

int intel_crtc_set_crc_source(struct drm_crtc *crtc, const char *source_name,
			      size_t *values_cnt)
{
	struct drm_i915_private *dev_priv = crtc->dev->dev_private;
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[crtc->index];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum intel_display_power_domain power_domain;
	enum intel_pipe_crc_source source;
	u32 val = 0; /* shut up gcc */
	int ret = 0;

	if (display_crc_ctl_parse_source(source_name, &source) < 0) {
		DRM_DEBUG_DRIVER("unknown source %s\n", source_name);
		return -EINVAL;
	}

	power_domain = POWER_DOMAIN_PIPE(crtc->index);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain)) {
		DRM_DEBUG_KMS("Trying to capture CRC while pipe is off\n");
		return -EIO;
	}

	ret = get_new_crc_ctl_reg(dev_priv, crtc->index, &source, &val);
	if (ret != 0)
		goto out;

	if (source) {
		/*
		 * When IPS gets enabled, the pipe CRC changes. Since IPS gets
		 * enabled and disabled dynamically based on package C states,
		 * user space can't make reliable use of the CRCs, so let's just
		 * completely disable it.
		 */
		hsw_disable_ips(intel_crtc);
	}

	I915_WRITE(PIPE_CRC_CTL(crtc->index), val);
	POSTING_READ(PIPE_CRC_CTL(crtc->index));

	if (!source) {
		if (IS_G4X(dev_priv))
			g4x_undo_pipe_scramble_reset(dev_priv, crtc->index);
		else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
			vlv_undo_pipe_scramble_reset(dev_priv, crtc->index);
		else if (IS_HASWELL(dev_priv) && crtc->index == PIPE_A)
			hsw_trans_edp_pipe_A_crc_wa(dev_priv, false);

		hsw_enable_ips(intel_crtc);
	}

	pipe_crc->skipped = 0;
	*values_cnt = 5;

out:
	intel_display_power_put(dev_priv, power_domain);

	return ret;
}
