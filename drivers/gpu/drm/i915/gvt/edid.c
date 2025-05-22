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

#include <drm/display/drm_dp.h>

#include "display/intel_dp_aux_regs.h"
#include "display/intel_gmbus.h"
#include "display/intel_gmbus_regs.h"
#include "gvt.h"
#include "i915_drv.h"
#include "i915_reg.h"

#define GMBUS1_TOTAL_BYTES_SHIFT 16
#define GMBUS1_TOTAL_BYTES_MASK 0x1ff
#define gmbus1_total_byte_count(v) (((v) >> \
	GMBUS1_TOTAL_BYTES_SHIFT) & GMBUS1_TOTAL_BYTES_MASK)
#define gmbus1_target_addr(v) (((v) & 0xff) >> 1)
#define gmbus1_target_index(v) (((v) >> 8) & 0xff)
#define gmbus1_bus_cycle(v) (((v) >> 25) & 0x7)

/* GMBUS0 bits definitions */
#define _GMBUS_PIN_SEL_MASK     (0x7)

static unsigned char edid_get_byte(struct intel_vgpu *vgpu)
{
	struct intel_vgpu_i2c_edid *edid = &vgpu->display.i2c_edid;
	unsigned char chr = 0;

	if (edid->state == I2C_NOT_SPECIFIED || !edid->target_selected) {
		gvt_vgpu_err("Driver tries to read EDID without proper sequence!\n");
		return 0;
	}
	if (edid->current_edid_read >= EDID_SIZE) {
		gvt_vgpu_err("edid_get_byte() exceeds the size of EDID!\n");
		return 0;
	}

	if (!edid->edid_available) {
		gvt_vgpu_err("Reading EDID but EDID is not available!\n");
		return 0;
	}

	if (intel_vgpu_has_monitor_on_port(vgpu, edid->port)) {
		struct intel_vgpu_edid_data *edid_data =
			intel_vgpu_port(vgpu, edid->port)->edid;

		chr = edid_data->edid_block[edid->current_edid_read];
		edid->current_edid_read++;
	} else {
		gvt_vgpu_err("No EDID available during the reading?\n");
	}
	return chr;
}

static inline int cnp_get_port_from_gmbus0(u32 gmbus0)
{
	int port_select = gmbus0 & _GMBUS_PIN_SEL_MASK;
	int port = -EINVAL;

	if (port_select == GMBUS_PIN_1_BXT)
		port = PORT_B;
	else if (port_select == GMBUS_PIN_2_BXT)
		port = PORT_C;
	else if (port_select == GMBUS_PIN_3_BXT)
		port = PORT_D;
	else if (port_select == GMBUS_PIN_4_CNP)
		port = PORT_E;
	return port;
}

static inline int bxt_get_port_from_gmbus0(u32 gmbus0)
{
	int port_select = gmbus0 & _GMBUS_PIN_SEL_MASK;
	int port = -EINVAL;

	if (port_select == GMBUS_PIN_1_BXT)
		port = PORT_B;
	else if (port_select == GMBUS_PIN_2_BXT)
		port = PORT_C;
	else if (port_select == GMBUS_PIN_3_BXT)
		port = PORT_D;
	return port;
}

static inline int get_port_from_gmbus0(u32 gmbus0)
{
	int port_select = gmbus0 & _GMBUS_PIN_SEL_MASK;
	int port = -EINVAL;

	if (port_select == GMBUS_PIN_VGADDC)
		port = PORT_E;
	else if (port_select == GMBUS_PIN_DPC)
		port = PORT_C;
	else if (port_select == GMBUS_PIN_DPB)
		port = PORT_B;
	else if (port_select == GMBUS_PIN_DPD)
		port = PORT_D;
	return port;
}

static void reset_gmbus_controller(struct intel_vgpu *vgpu)
{
	vgpu_vreg_t(vgpu, PCH_GMBUS2) = GMBUS_HW_RDY;
	if (!vgpu->display.i2c_edid.edid_available)
		vgpu_vreg_t(vgpu, PCH_GMBUS2) |= GMBUS_SATOER;
	vgpu->display.i2c_edid.gmbus.phase = GMBUS_IDLE_PHASE;
}

/* GMBUS0 */
static int gmbus0_mmio_write(struct intel_vgpu *vgpu,
			unsigned int offset, void *p_data, unsigned int bytes)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;
	int port, pin_select;

	memcpy(&vgpu_vreg(vgpu, offset), p_data, bytes);

	pin_select = vgpu_vreg(vgpu, offset) & _GMBUS_PIN_SEL_MASK;

	intel_vgpu_init_i2c_edid(vgpu);

	if (pin_select == 0)
		return 0;

	if (IS_BROXTON(i915))
		port = bxt_get_port_from_gmbus0(pin_select);
	else if (IS_COFFEELAKE(i915) || IS_COMETLAKE(i915))
		port = cnp_get_port_from_gmbus0(pin_select);
	else
		port = get_port_from_gmbus0(pin_select);
	if (drm_WARN_ON(&i915->drm, port < 0))
		return 0;

	vgpu->display.i2c_edid.state = I2C_GMBUS;
	vgpu->display.i2c_edid.gmbus.phase = GMBUS_IDLE_PHASE;

	vgpu_vreg_t(vgpu, PCH_GMBUS2) &= ~GMBUS_ACTIVE;
	vgpu_vreg_t(vgpu, PCH_GMBUS2) |= GMBUS_HW_RDY | GMBUS_HW_WAIT_PHASE;

	if (intel_vgpu_has_monitor_on_port(vgpu, port) &&
			!intel_vgpu_port_is_dp(vgpu, port)) {
		vgpu->display.i2c_edid.port = port;
		vgpu->display.i2c_edid.edid_available = true;
		vgpu_vreg_t(vgpu, PCH_GMBUS2) &= ~GMBUS_SATOER;
	} else
		vgpu_vreg_t(vgpu, PCH_GMBUS2) |= GMBUS_SATOER;
	return 0;
}

static int gmbus1_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	struct intel_vgpu_i2c_edid *i2c_edid = &vgpu->display.i2c_edid;
	u32 target_addr;
	u32 wvalue = *(u32 *)p_data;

	if (vgpu_vreg(vgpu, offset) & GMBUS_SW_CLR_INT) {
		if (!(wvalue & GMBUS_SW_CLR_INT)) {
			vgpu_vreg(vgpu, offset) &= ~GMBUS_SW_CLR_INT;
			reset_gmbus_controller(vgpu);
		}
		/*
		 * TODO: "This bit is cleared to zero when an event
		 * causes the HW_RDY bit transition to occur "
		 */
	} else {
		/*
		 * per bspec setting this bit can cause:
		 * 1) INT status bit cleared
		 * 2) HW_RDY bit asserted
		 */
		if (wvalue & GMBUS_SW_CLR_INT) {
			vgpu_vreg_t(vgpu, PCH_GMBUS2) &= ~GMBUS_INT;
			vgpu_vreg_t(vgpu, PCH_GMBUS2) |= GMBUS_HW_RDY;
		}

		/* For virtualization, we suppose that HW is always ready,
		 * so GMBUS_SW_RDY should always be cleared
		 */
		if (wvalue & GMBUS_SW_RDY)
			wvalue &= ~GMBUS_SW_RDY;

		i2c_edid->gmbus.total_byte_count =
			gmbus1_total_byte_count(wvalue);
		target_addr = gmbus1_target_addr(wvalue);

		/* vgpu gmbus only support EDID */
		if (target_addr == EDID_ADDR) {
			i2c_edid->target_selected = true;
		} else if (target_addr != 0) {
			gvt_dbg_dpy(
				"vgpu%d: unsupported gmbus target addr(0x%x)\n"
				"	gmbus operations will be ignored.\n",
					vgpu->id, target_addr);
		}

		if (wvalue & GMBUS_CYCLE_INDEX)
			i2c_edid->current_edid_read =
				gmbus1_target_index(wvalue);

		i2c_edid->gmbus.cycle_type = gmbus1_bus_cycle(wvalue);
		switch (gmbus1_bus_cycle(wvalue)) {
		case GMBUS_NOCYCLE:
			break;
		case GMBUS_STOP:
			/* From spec:
			 * This can only cause a STOP to be generated
			 * if a GMBUS cycle is generated, the GMBUS is
			 * currently in a data/wait/idle phase, or it is in a
			 * WAIT phase
			 */
			if (gmbus1_bus_cycle(vgpu_vreg(vgpu, offset))
				!= GMBUS_NOCYCLE) {
				intel_vgpu_init_i2c_edid(vgpu);
				/* After the 'stop' cycle, hw state would become
				 * 'stop phase' and then 'idle phase' after a
				 * few milliseconds. In emulation, we just set
				 * it as 'idle phase' ('stop phase' is not
				 * visible in gmbus interface)
				 */
				i2c_edid->gmbus.phase = GMBUS_IDLE_PHASE;
				vgpu_vreg_t(vgpu, PCH_GMBUS2) &= ~GMBUS_ACTIVE;
			}
			break;
		case NIDX_NS_W:
		case IDX_NS_W:
		case NIDX_STOP:
		case IDX_STOP:
			/* From hw spec the GMBUS phase
			 * transition like this:
			 * START (-->INDEX) -->DATA
			 */
			i2c_edid->gmbus.phase = GMBUS_DATA_PHASE;
			vgpu_vreg_t(vgpu, PCH_GMBUS2) |= GMBUS_ACTIVE;
			break;
		default:
			gvt_vgpu_err("Unknown/reserved GMBUS cycle detected!\n");
			break;
		}
		/*
		 * From hw spec the WAIT state will be
		 * cleared:
		 * (1) in a new GMBUS cycle
		 * (2) by generating a stop
		 */
		vgpu_vreg(vgpu, offset) = wvalue;
	}
	return 0;
}

static int gmbus3_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
	void *p_data, unsigned int bytes)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;

	drm_WARN_ON(&i915->drm, 1);
	return 0;
}

static int gmbus3_mmio_read(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	int i;
	unsigned char byte_data;
	struct intel_vgpu_i2c_edid *i2c_edid = &vgpu->display.i2c_edid;
	int byte_left = i2c_edid->gmbus.total_byte_count -
				i2c_edid->current_edid_read;
	int byte_count = byte_left;
	u32 reg_data = 0;

	/* Data can only be received if previous settings correct */
	if (vgpu_vreg_t(vgpu, PCH_GMBUS1) & GMBUS_SLAVE_READ) {
		if (byte_left <= 0) {
			memcpy(p_data, &vgpu_vreg(vgpu, offset), bytes);
			return 0;
		}

		if (byte_count > 4)
			byte_count = 4;
		for (i = 0; i < byte_count; i++) {
			byte_data = edid_get_byte(vgpu);
			reg_data |= (byte_data << (i << 3));
		}

		memcpy(&vgpu_vreg(vgpu, offset), &reg_data, byte_count);
		memcpy(p_data, &vgpu_vreg(vgpu, offset), bytes);

		if (byte_left <= 4) {
			switch (i2c_edid->gmbus.cycle_type) {
			case NIDX_STOP:
			case IDX_STOP:
				i2c_edid->gmbus.phase = GMBUS_IDLE_PHASE;
				break;
			case NIDX_NS_W:
			case IDX_NS_W:
			default:
				i2c_edid->gmbus.phase = GMBUS_WAIT_PHASE;
				break;
			}
			intel_vgpu_init_i2c_edid(vgpu);
		}
		/*
		 * Read GMBUS3 during send operation,
		 * return the latest written value
		 */
	} else {
		memcpy(p_data, &vgpu_vreg(vgpu, offset), bytes);
		gvt_vgpu_err("warning: gmbus3 read with nothing returned\n");
	}
	return 0;
}

static int gmbus2_mmio_read(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 value = vgpu_vreg(vgpu, offset);

	if (!(vgpu_vreg(vgpu, offset) & GMBUS_INUSE))
		vgpu_vreg(vgpu, offset) |= GMBUS_INUSE;
	memcpy(p_data, (void *)&value, bytes);
	return 0;
}

static int gmbus2_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 wvalue = *(u32 *)p_data;

	if (wvalue & GMBUS_INUSE)
		vgpu_vreg(vgpu, offset) &= ~GMBUS_INUSE;
	/* All other bits are read-only */
	return 0;
}

/**
 * intel_gvt_i2c_handle_gmbus_read - emulate gmbus register mmio read
 * @vgpu: a vGPU
 * @offset: reg offset
 * @p_data: data return buffer
 * @bytes: access data length
 *
 * This function is used to emulate gmbus register mmio read
 *
 * Returns:
 * Zero on success, negative error code if failed.
 *
 */
int intel_gvt_i2c_handle_gmbus_read(struct intel_vgpu *vgpu,
	unsigned int offset, void *p_data, unsigned int bytes)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;

	if (drm_WARN_ON(&i915->drm, bytes > 8 && (offset & (bytes - 1))))
		return -EINVAL;

	if (offset == i915_mmio_reg_offset(PCH_GMBUS2))
		return gmbus2_mmio_read(vgpu, offset, p_data, bytes);
	else if (offset == i915_mmio_reg_offset(PCH_GMBUS3))
		return gmbus3_mmio_read(vgpu, offset, p_data, bytes);

	memcpy(p_data, &vgpu_vreg(vgpu, offset), bytes);
	return 0;
}

/**
 * intel_gvt_i2c_handle_gmbus_write - emulate gmbus register mmio write
 * @vgpu: a vGPU
 * @offset: reg offset
 * @p_data: data return buffer
 * @bytes: access data length
 *
 * This function is used to emulate gmbus register mmio write
 *
 * Returns:
 * Zero on success, negative error code if failed.
 *
 */
int intel_gvt_i2c_handle_gmbus_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;

	if (drm_WARN_ON(&i915->drm, bytes > 8 && (offset & (bytes - 1))))
		return -EINVAL;

	if (offset == i915_mmio_reg_offset(PCH_GMBUS0))
		return gmbus0_mmio_write(vgpu, offset, p_data, bytes);
	else if (offset == i915_mmio_reg_offset(PCH_GMBUS1))
		return gmbus1_mmio_write(vgpu, offset, p_data, bytes);
	else if (offset == i915_mmio_reg_offset(PCH_GMBUS2))
		return gmbus2_mmio_write(vgpu, offset, p_data, bytes);
	else if (offset == i915_mmio_reg_offset(PCH_GMBUS3))
		return gmbus3_mmio_write(vgpu, offset, p_data, bytes);

	memcpy(&vgpu_vreg(vgpu, offset), p_data, bytes);
	return 0;
}

enum {
	AUX_CH_CTL = 0,
	AUX_CH_DATA1,
	AUX_CH_DATA2,
	AUX_CH_DATA3,
	AUX_CH_DATA4,
	AUX_CH_DATA5
};

static inline int get_aux_ch_reg(unsigned int offset)
{
	int reg;

	switch (offset & 0xff) {
	case 0x10:
		reg = AUX_CH_CTL;
		break;
	case 0x14:
		reg = AUX_CH_DATA1;
		break;
	case 0x18:
		reg = AUX_CH_DATA2;
		break;
	case 0x1c:
		reg = AUX_CH_DATA3;
		break;
	case 0x20:
		reg = AUX_CH_DATA4;
		break;
	case 0x24:
		reg = AUX_CH_DATA5;
		break;
	default:
		reg = -1;
		break;
	}
	return reg;
}

/**
 * intel_gvt_i2c_handle_aux_ch_write - emulate AUX channel register write
 * @vgpu: a vGPU
 * @port_idx: port index
 * @offset: reg offset
 * @p_data: write ptr
 *
 * This function is used to emulate AUX channel register write
 *
 */
void intel_gvt_i2c_handle_aux_ch_write(struct intel_vgpu *vgpu,
				int port_idx,
				unsigned int offset,
				void *p_data)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;
	struct intel_vgpu_i2c_edid *i2c_edid = &vgpu->display.i2c_edid;
	int msg_length, ret_msg_size;
	int msg, addr, ctrl, op;
	u32 value = *(u32 *)p_data;
	int aux_data_for_write = 0;
	int reg = get_aux_ch_reg(offset);

	if (reg != AUX_CH_CTL) {
		vgpu_vreg(vgpu, offset) = value;
		return;
	}

	msg_length = REG_FIELD_GET(DP_AUX_CH_CTL_MESSAGE_SIZE_MASK, value);

	// check the msg in DATA register.
	msg = vgpu_vreg(vgpu, offset + 4);
	addr = (msg >> 8) & 0xffff;
	ctrl = (msg >> 24) & 0xff;
	op = ctrl >> 4;
	if (!(value & DP_AUX_CH_CTL_SEND_BUSY)) {
		/* The ctl write to clear some states */
		return;
	}

	/* Always set the wanted value for vms. */
	ret_msg_size = (((op & 0x1) == DP_AUX_I2C_READ) ? 2 : 1);
	vgpu_vreg(vgpu, offset) =
		DP_AUX_CH_CTL_DONE |
		DP_AUX_CH_CTL_MESSAGE_SIZE(ret_msg_size);

	if (msg_length == 3) {
		if (!(op & DP_AUX_I2C_MOT)) {
			/* stop */
			intel_vgpu_init_i2c_edid(vgpu);
		} else {
			/* start or restart */
			i2c_edid->aux_ch.i2c_over_aux_ch = true;
			i2c_edid->aux_ch.aux_ch_mot = true;
			if (addr == 0) {
				/* reset the address */
				intel_vgpu_init_i2c_edid(vgpu);
			} else if (addr == EDID_ADDR) {
				i2c_edid->state = I2C_AUX_CH;
				i2c_edid->port = port_idx;
				i2c_edid->target_selected = true;
				if (intel_vgpu_has_monitor_on_port(vgpu,
					port_idx) &&
					intel_vgpu_port_is_dp(vgpu, port_idx))
					i2c_edid->edid_available = true;
			}
		}
	} else if ((op & 0x1) == DP_AUX_I2C_WRITE) {
		/* TODO
		 * We only support EDID reading from I2C_over_AUX. And
		 * we do not expect the index mode to be used. Right now
		 * the WRITE operation is ignored. It is good enough to
		 * support the gfx driver to do EDID access.
		 */
	} else {
		if (drm_WARN_ON(&i915->drm, (op & 0x1) != DP_AUX_I2C_READ))
			return;
		if (drm_WARN_ON(&i915->drm, msg_length != 4))
			return;
		if (i2c_edid->edid_available && i2c_edid->target_selected) {
			unsigned char val = edid_get_byte(vgpu);

			aux_data_for_write = (val << 16);
		} else
			aux_data_for_write = (0xff << 16);
	}
	/* write the return value in AUX_CH_DATA reg which includes:
	 * ACK of I2C_WRITE
	 * returned byte if it is READ
	 */
	aux_data_for_write |= DP_AUX_I2C_REPLY_ACK << 24;
	vgpu_vreg(vgpu, offset + 4) = aux_data_for_write;
}

/**
 * intel_vgpu_init_i2c_edid - initialize vGPU i2c edid emulation
 * @vgpu: a vGPU
 *
 * This function is used to initialize vGPU i2c edid emulation stuffs
 *
 */
void intel_vgpu_init_i2c_edid(struct intel_vgpu *vgpu)
{
	struct intel_vgpu_i2c_edid *edid = &vgpu->display.i2c_edid;

	edid->state = I2C_NOT_SPECIFIED;

	edid->port = -1;
	edid->target_selected = false;
	edid->edid_available = false;
	edid->current_edid_read = 0;

	memset(&edid->gmbus, 0, sizeof(struct intel_vgpu_i2c_gmbus));

	edid->aux_ch.i2c_over_aux_ch = false;
	edid->aux_ch.aux_ch_mot = false;
}
