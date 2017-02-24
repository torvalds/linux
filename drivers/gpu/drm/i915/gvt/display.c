/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Ke Yu
 *    Zhiyuan Lv <zhiyuan.lv@intel.com>
 *
 * Contributors:
 *    Terrence Xu <terrence.xu@intel.com>
 *    Changbin Du <changbin.du@intel.com>
 *    Bing Niu <bing.niu@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#include "i915_drv.h"
#include "gvt.h"

static int get_edp_pipe(struct intel_vgpu *vgpu)
{
	u32 data = vgpu_vreg(vgpu, _TRANS_DDI_FUNC_CTL_EDP);
	int pipe = -1;

	switch (data & TRANS_DDI_EDP_INPUT_MASK) {
	case TRANS_DDI_EDP_INPUT_A_ON:
	case TRANS_DDI_EDP_INPUT_A_ONOFF:
		pipe = PIPE_A;
		break;
	case TRANS_DDI_EDP_INPUT_B_ONOFF:
		pipe = PIPE_B;
		break;
	case TRANS_DDI_EDP_INPUT_C_ONOFF:
		pipe = PIPE_C;
		break;
	}
	return pipe;
}

static int edp_pipe_is_enabled(struct intel_vgpu *vgpu)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;

	if (!(vgpu_vreg(vgpu, PIPECONF(_PIPE_EDP)) & PIPECONF_ENABLE))
		return 0;

	if (!(vgpu_vreg(vgpu, _TRANS_DDI_FUNC_CTL_EDP) & TRANS_DDI_FUNC_ENABLE))
		return 0;
	return 1;
}

static int pipe_is_enabled(struct intel_vgpu *vgpu, int pipe)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;

	if (WARN_ON(pipe < PIPE_A || pipe >= I915_MAX_PIPES))
		return -EINVAL;

	if (vgpu_vreg(vgpu, PIPECONF(pipe)) & PIPECONF_ENABLE)
		return 1;

	if (edp_pipe_is_enabled(vgpu) &&
			get_edp_pipe(vgpu) == pipe)
		return 1;
	return 0;
}

static unsigned char virtual_dp_monitor_edid[GVT_EDID_NUM][EDID_SIZE] = {
	{
/* EDID with 1024x768 as its resolution */
		/*Header*/
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
		/* Vendor & Product Identification */
		0x22, 0xf0, 0x54, 0x29, 0x00, 0x00, 0x00, 0x00, 0x04, 0x17,
		/* Version & Revision */
		0x01, 0x04,
		/* Basic Display Parameters & Features */
		0xa5, 0x34, 0x20, 0x78, 0x23,
		/* Color Characteristics */
		0xfc, 0x81, 0xa4, 0x55, 0x4d, 0x9d, 0x25, 0x12, 0x50, 0x54,
		/* Established Timings: maximum resolution is 1024x768 */
		0x21, 0x08, 0x00,
		/* Standard Timings. All invalid */
		0x00, 0xc0, 0x00, 0xc0, 0x00, 0x40, 0x00, 0x80, 0x00, 0x00,
		0x00, 0x40, 0x00, 0x00, 0x00, 0x01,
		/* 18 Byte Data Blocks 1: invalid */
		0x00, 0x00, 0x80, 0xa0, 0x70, 0xb0,
		0x23, 0x40, 0x30, 0x20, 0x36, 0x00, 0x06, 0x44, 0x21, 0x00, 0x00, 0x1a,
		/* 18 Byte Data Blocks 2: invalid */
		0x00, 0x00, 0x00, 0xfd, 0x00, 0x18, 0x3c, 0x18, 0x50, 0x11, 0x00, 0x0a,
		0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		/* 18 Byte Data Blocks 3: invalid */
		0x00, 0x00, 0x00, 0xfc, 0x00, 0x48,
		0x50, 0x20, 0x5a, 0x52, 0x32, 0x34, 0x34, 0x30, 0x77, 0x0a, 0x20, 0x20,
		/* 18 Byte Data Blocks 4: invalid */
		0x00, 0x00, 0x00, 0xff, 0x00, 0x43, 0x4e, 0x34, 0x33, 0x30, 0x34, 0x30,
		0x44, 0x58, 0x51, 0x0a, 0x20, 0x20,
		/* Extension Block Count */
		0x00,
		/* Checksum */
		0xef,
	},
	{
/* EDID with 1920x1200 as its resolution */
		/*Header*/
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
		/* Vendor & Product Identification */
		0x22, 0xf0, 0x54, 0x29, 0x00, 0x00, 0x00, 0x00, 0x04, 0x17,
		/* Version & Revision */
		0x01, 0x04,
		/* Basic Display Parameters & Features */
		0xa5, 0x34, 0x20, 0x78, 0x23,
		/* Color Characteristics */
		0xfc, 0x81, 0xa4, 0x55, 0x4d, 0x9d, 0x25, 0x12, 0x50, 0x54,
		/* Established Timings: maximum resolution is 1024x768 */
		0x21, 0x08, 0x00,
		/*
		 * Standard Timings.
		 * below new resolutions can be supported:
		 * 1920x1080, 1280x720, 1280x960, 1280x1024,
		 * 1440x900, 1600x1200, 1680x1050
		 */
		0xd1, 0xc0, 0x81, 0xc0, 0x81, 0x40, 0x81, 0x80, 0x95, 0x00,
		0xa9, 0x40, 0xb3, 0x00, 0x01, 0x01,
		/* 18 Byte Data Blocks 1: max resolution is 1920x1200 */
		0x28, 0x3c, 0x80, 0xa0, 0x70, 0xb0,
		0x23, 0x40, 0x30, 0x20, 0x36, 0x00, 0x06, 0x44, 0x21, 0x00, 0x00, 0x1a,
		/* 18 Byte Data Blocks 2: invalid */
		0x00, 0x00, 0x00, 0xfd, 0x00, 0x18, 0x3c, 0x18, 0x50, 0x11, 0x00, 0x0a,
		0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		/* 18 Byte Data Blocks 3: invalid */
		0x00, 0x00, 0x00, 0xfc, 0x00, 0x48,
		0x50, 0x20, 0x5a, 0x52, 0x32, 0x34, 0x34, 0x30, 0x77, 0x0a, 0x20, 0x20,
		/* 18 Byte Data Blocks 4: invalid */
		0x00, 0x00, 0x00, 0xff, 0x00, 0x43, 0x4e, 0x34, 0x33, 0x30, 0x34, 0x30,
		0x44, 0x58, 0x51, 0x0a, 0x20, 0x20,
		/* Extension Block Count */
		0x00,
		/* Checksum */
		0x45,
	},
};

#define DPCD_HEADER_SIZE        0xb

static u8 dpcd_fix_data[DPCD_HEADER_SIZE] = {
	0x11, 0x0a, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void emulate_monitor_status_change(struct intel_vgpu *vgpu)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	vgpu_vreg(vgpu, SDEISR) &= ~(SDE_PORTB_HOTPLUG_CPT |
			SDE_PORTC_HOTPLUG_CPT |
			SDE_PORTD_HOTPLUG_CPT);

	if (IS_SKYLAKE(dev_priv))
		vgpu_vreg(vgpu, SDEISR) &= ~(SDE_PORTA_HOTPLUG_SPT |
				SDE_PORTE_HOTPLUG_SPT);

	if (intel_vgpu_has_monitor_on_port(vgpu, PORT_B))
		vgpu_vreg(vgpu, SDEISR) |= SDE_PORTB_HOTPLUG_CPT;

	if (intel_vgpu_has_monitor_on_port(vgpu, PORT_C))
		vgpu_vreg(vgpu, SDEISR) |= SDE_PORTC_HOTPLUG_CPT;

	if (intel_vgpu_has_monitor_on_port(vgpu, PORT_D))
		vgpu_vreg(vgpu, SDEISR) |= SDE_PORTD_HOTPLUG_CPT;

	if (IS_SKYLAKE(dev_priv) &&
			intel_vgpu_has_monitor_on_port(vgpu, PORT_E)) {
		vgpu_vreg(vgpu, SDEISR) |= SDE_PORTE_HOTPLUG_SPT;
	}

	if (intel_vgpu_has_monitor_on_port(vgpu, PORT_A)) {
		if (IS_BROADWELL(dev_priv))
			vgpu_vreg(vgpu, GEN8_DE_PORT_ISR) |=
				GEN8_PORT_DP_A_HOTPLUG;
		else
			vgpu_vreg(vgpu, SDEISR) |= SDE_PORTA_HOTPLUG_SPT;
	}
}

static void clean_virtual_dp_monitor(struct intel_vgpu *vgpu, int port_num)
{
	struct intel_vgpu_port *port = intel_vgpu_port(vgpu, port_num);

	kfree(port->edid);
	port->edid = NULL;

	kfree(port->dpcd);
	port->dpcd = NULL;
}

static int setup_virtual_dp_monitor(struct intel_vgpu *vgpu, int port_num,
		int type)
{
	struct intel_vgpu_port *port = intel_vgpu_port(vgpu, port_num);

	port->edid = kzalloc(sizeof(*(port->edid)), GFP_KERNEL);
	if (!port->edid)
		return -ENOMEM;

	port->dpcd = kzalloc(sizeof(*(port->dpcd)), GFP_KERNEL);
	if (!port->dpcd) {
		kfree(port->edid);
		return -ENOMEM;
	}

	memcpy(port->edid->edid_block, virtual_dp_monitor_edid[GVT_EDID_1920_1200],
			EDID_SIZE);
	port->edid->data_valid = true;

	memcpy(port->dpcd->data, dpcd_fix_data, DPCD_HEADER_SIZE);
	port->dpcd->data_valid = true;
	port->dpcd->data[DPCD_SINK_COUNT] = 0x1;
	port->type = type;

	emulate_monitor_status_change(vgpu);
	return 0;
}

/**
 * intel_gvt_check_vblank_emulation - check if vblank emulation timer should
 * be turned on/off when a virtual pipe is enabled/disabled.
 * @gvt: a GVT device
 *
 * This function is used to turn on/off vblank timer according to currently
 * enabled/disabled virtual pipes.
 *
 */
void intel_gvt_check_vblank_emulation(struct intel_gvt *gvt)
{
	struct intel_gvt_irq *irq = &gvt->irq;
	struct intel_vgpu *vgpu;
	bool have_enabled_pipe = false;
	int pipe, id;

	if (WARN_ON(!mutex_is_locked(&gvt->lock)))
		return;

	hrtimer_cancel(&irq->vblank_timer.timer);

	for_each_active_vgpu(gvt, vgpu, id) {
		for (pipe = 0; pipe < I915_MAX_PIPES; pipe++) {
			have_enabled_pipe =
				pipe_is_enabled(vgpu, pipe);
			if (have_enabled_pipe)
				break;
		}
	}

	if (have_enabled_pipe)
		hrtimer_start(&irq->vblank_timer.timer,
			ktime_add_ns(ktime_get(), irq->vblank_timer.period),
			HRTIMER_MODE_ABS);
}

static void emulate_vblank_on_pipe(struct intel_vgpu *vgpu, int pipe)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	struct intel_vgpu_irq *irq = &vgpu->irq;
	int vblank_event[] = {
		[PIPE_A] = PIPE_A_VBLANK,
		[PIPE_B] = PIPE_B_VBLANK,
		[PIPE_C] = PIPE_C_VBLANK,
	};
	int event;

	if (pipe < PIPE_A || pipe > PIPE_C)
		return;

	for_each_set_bit(event, irq->flip_done_event[pipe],
			INTEL_GVT_EVENT_MAX) {
		clear_bit(event, irq->flip_done_event[pipe]);
		if (!pipe_is_enabled(vgpu, pipe))
			continue;

		vgpu_vreg(vgpu, PIPE_FLIPCOUNT_G4X(pipe))++;
		intel_vgpu_trigger_virtual_event(vgpu, event);
	}

	if (pipe_is_enabled(vgpu, pipe)) {
		vgpu_vreg(vgpu, PIPE_FRMCOUNT_G4X(pipe))++;
		intel_vgpu_trigger_virtual_event(vgpu, vblank_event[pipe]);
	}
}

static void emulate_vblank(struct intel_vgpu *vgpu)
{
	int pipe;

	for_each_pipe(vgpu->gvt->dev_priv, pipe)
		emulate_vblank_on_pipe(vgpu, pipe);
}

/**
 * intel_gvt_emulate_vblank - trigger vblank events for vGPUs on GVT device
 * @gvt: a GVT device
 *
 * This function is used to trigger vblank interrupts for vGPUs on GVT device
 *
 */
void intel_gvt_emulate_vblank(struct intel_gvt *gvt)
{
	struct intel_vgpu *vgpu;
	int id;

	if (WARN_ON(!mutex_is_locked(&gvt->lock)))
		return;

	for_each_active_vgpu(gvt, vgpu, id)
		emulate_vblank(vgpu);
}

/**
 * intel_vgpu_clean_display - clean vGPU virtual display emulation
 * @vgpu: a vGPU
 *
 * This function is used to clean vGPU virtual display emulation stuffs
 *
 */
void intel_vgpu_clean_display(struct intel_vgpu *vgpu)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;

	if (IS_SKYLAKE(dev_priv))
		clean_virtual_dp_monitor(vgpu, PORT_D);
	else
		clean_virtual_dp_monitor(vgpu, PORT_B);
}

/**
 * intel_vgpu_init_display- initialize vGPU virtual display emulation
 * @vgpu: a vGPU
 *
 * This function is used to initialize vGPU virtual display emulation stuffs
 *
 * Returns:
 * Zero on success, negative error code if failed.
 *
 */
int intel_vgpu_init_display(struct intel_vgpu *vgpu)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;

	intel_vgpu_init_i2c_edid(vgpu);

	if (IS_SKYLAKE(dev_priv))
		return setup_virtual_dp_monitor(vgpu, PORT_D, GVT_DP_D);
	else
		return setup_virtual_dp_monitor(vgpu, PORT_B, GVT_DP_B);
}

/**
 * intel_vgpu_reset_display- reset vGPU virtual display emulation
 * @vgpu: a vGPU
 *
 * This function is used to reset vGPU virtual display emulation stuffs
 *
 */
void intel_vgpu_reset_display(struct intel_vgpu *vgpu)
{
	emulate_monitor_status_change(vgpu);
}
