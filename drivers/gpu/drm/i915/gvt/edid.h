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

#ifndef _GVT_EDID_H_
#define _GVT_EDID_H_

#define EDID_SIZE		128
#define EDID_ADDR		0x50 /* Linux hvm EDID addr */

#define GVT_AUX_NATIVE_WRITE			0x8
#define GVT_AUX_NATIVE_READ			0x9
#define GVT_AUX_I2C_WRITE			0x0
#define GVT_AUX_I2C_READ			0x1
#define GVT_AUX_I2C_STATUS			0x2
#define GVT_AUX_I2C_MOT				0x4
#define GVT_AUX_I2C_REPLY_ACK			(0x0 << 6)

struct intel_vgpu_edid_data {
	bool data_valid;
	unsigned char edid_block[EDID_SIZE];
};

enum gmbus_cycle_type {
	GMBUS_NOCYCLE	= 0x0,
	NIDX_NS_W	= 0x1,
	IDX_NS_W	= 0x3,
	GMBUS_STOP	= 0x4,
	NIDX_STOP	= 0x5,
	IDX_STOP	= 0x7
};

/*
 * States of GMBUS
 *
 * GMBUS0-3 could be related to the EDID virtualization. Another two GMBUS
 * registers, GMBUS4 (interrupt mask) and GMBUS5 (2 byte indes register), are
 * not considered here. Below describes the usage of GMBUS registers that are
 * cared by the EDID virtualization
 *
 * GMBUS0:
 *      R/W
 *      port selection. value of bit0 - bit2 corresponds to the GPIO registers.
 *
 * GMBUS1:
 *      R/W Protect
 *      Command and Status.
 *      bit0 is the direction bit: 1 is read; 0 is write.
 *      bit1 - bit7 is slave 7-bit address.
 *      bit16 - bit24 total byte count (ignore?)
 *
 * GMBUS2:
 *      Most of bits are read only except bit 15 (IN_USE)
 *      Status register
 *      bit0 - bit8 current byte count
 *      bit 11: hardware ready;
 *
 * GMBUS3:
 *      Read/Write
 *      Data for transfer
 */

/* From hw specs, Other phases like START, ADDRESS, INDEX
 * are invisible to GMBUS MMIO interface. So no definitions
 * in below enum types
 */
enum gvt_gmbus_phase {
	GMBUS_IDLE_PHASE = 0,
	GMBUS_DATA_PHASE,
	GMBUS_WAIT_PHASE,
	//GMBUS_STOP_PHASE,
	GMBUS_MAX_PHASE
};

struct intel_vgpu_i2c_gmbus {
	unsigned int total_byte_count; /* from GMBUS1 */
	enum gmbus_cycle_type cycle_type;
	enum gvt_gmbus_phase phase;
};

struct intel_vgpu_i2c_aux_ch {
	bool i2c_over_aux_ch;
	bool aux_ch_mot;
};

enum i2c_state {
	I2C_NOT_SPECIFIED = 0,
	I2C_GMBUS = 1,
	I2C_AUX_CH = 2
};

/* I2C sequences cannot interleave.
 * GMBUS and AUX_CH sequences cannot interleave.
 */
struct intel_vgpu_i2c_edid {
	enum i2c_state state;

	unsigned int port;
	bool slave_selected;
	bool edid_available;
	unsigned int current_edid_read;

	struct intel_vgpu_i2c_gmbus gmbus;
	struct intel_vgpu_i2c_aux_ch aux_ch;
};

void intel_vgpu_init_i2c_edid(struct intel_vgpu *vgpu);

int intel_gvt_i2c_handle_gmbus_read(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes);

int intel_gvt_i2c_handle_gmbus_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes);

void intel_gvt_i2c_handle_aux_ch_write(struct intel_vgpu *vgpu,
		int port_idx,
		unsigned int offset,
		void *p_data);

#endif /*_GVT_EDID_H_*/
