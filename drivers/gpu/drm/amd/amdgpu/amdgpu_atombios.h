/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __AMDGPU_ATOMBIOS_H__
#define __AMDGPU_ATOMBIOS_H__

struct atom_clock_dividers {
	u32 post_div;
	union {
		struct {
#ifdef __BIG_ENDIAN
			u32 reserved : 6;
			u32 whole_fb_div : 12;
			u32 frac_fb_div : 14;
#else
			u32 frac_fb_div : 14;
			u32 whole_fb_div : 12;
			u32 reserved : 6;
#endif
		};
		u32 fb_div;
	};
	u32 ref_div;
	bool enable_post_div;
	bool enable_dithen;
	u32 vco_mode;
	u32 real_clock;
	/* added for CI */
	u32 post_divider;
	u32 flags;
};

struct atom_mpll_param {
	union {
		struct {
#ifdef __BIG_ENDIAN
			u32 reserved : 8;
			u32 clkfrac : 12;
			u32 clkf : 12;
#else
			u32 clkf : 12;
			u32 clkfrac : 12;
			u32 reserved : 8;
#endif
		};
		u32 fb_div;
	};
	u32 post_div;
	u32 bwcntl;
	u32 dll_speed;
	u32 vco_mode;
	u32 yclk_sel;
	u32 qdr;
	u32 half_rate;
};

#define MEM_TYPE_GDDR5  0x50
#define MEM_TYPE_GDDR4  0x40
#define MEM_TYPE_GDDR3  0x30
#define MEM_TYPE_DDR2   0x20
#define MEM_TYPE_GDDR1  0x10
#define MEM_TYPE_DDR3   0xb0
#define MEM_TYPE_MASK   0xf0

struct atom_memory_info {
	u8 mem_vendor;
	u8 mem_type;
};

#define MAX_AC_TIMING_ENTRIES 16

struct atom_memory_clock_range_table
{
	u8 num_entries;
	u8 rsv[3];
	u32 mclk[MAX_AC_TIMING_ENTRIES];
};

#define VBIOS_MC_REGISTER_ARRAY_SIZE 32
#define VBIOS_MAX_AC_TIMING_ENTRIES 20

struct atom_mc_reg_entry {
	u32 mclk_max;
	u32 mc_data[VBIOS_MC_REGISTER_ARRAY_SIZE];
};

struct atom_mc_register_address {
	u16 s1;
	u8 pre_reg_data;
};

struct atom_mc_reg_table {
	u8 last;
	u8 num_entries;
	struct atom_mc_reg_entry mc_reg_table_entry[VBIOS_MAX_AC_TIMING_ENTRIES];
	struct atom_mc_register_address mc_reg_address[VBIOS_MC_REGISTER_ARRAY_SIZE];
};

#define MAX_VOLTAGE_ENTRIES 32

struct atom_voltage_table_entry
{
	u16 value;
	u32 smio_low;
};

struct atom_voltage_table
{
	u32 count;
	u32 mask_low;
	u32 phase_delay;
	struct atom_voltage_table_entry entries[MAX_VOLTAGE_ENTRIES];
};

struct amdgpu_gpio_rec
amdgpu_atombios_lookup_gpio(struct amdgpu_device *adev,
			    u8 id);

struct amdgpu_i2c_bus_rec amdgpu_atombios_lookup_i2c_gpio(struct amdgpu_device *adev,
							  uint8_t id);
void amdgpu_atombios_i2c_init(struct amdgpu_device *adev);

bool amdgpu_atombios_has_dce_engine_info(struct amdgpu_device *adev);

bool amdgpu_atombios_get_connector_info_from_object_table(struct amdgpu_device *adev);

int amdgpu_atombios_get_clock_info(struct amdgpu_device *adev);

int amdgpu_atombios_get_gfx_info(struct amdgpu_device *adev);

bool amdgpu_atombios_get_asic_ss_info(struct amdgpu_device *adev,
				      struct amdgpu_atom_ss *ss,
				      int id, u32 clock);

int amdgpu_atombios_get_clock_dividers(struct amdgpu_device *adev,
				       u8 clock_type,
				       u32 clock,
				       bool strobe_mode,
				       struct atom_clock_dividers *dividers);

int amdgpu_atombios_get_memory_pll_dividers(struct amdgpu_device *adev,
					    u32 clock,
					    bool strobe_mode,
					    struct atom_mpll_param *mpll_param);

void amdgpu_atombios_set_engine_dram_timings(struct amdgpu_device *adev,
					     u32 eng_clock, u32 mem_clock);

int amdgpu_atombios_get_leakage_id_from_vbios(struct amdgpu_device *adev,
					      u16 *leakage_id);

int amdgpu_atombios_get_leakage_vddc_based_on_leakage_params(struct amdgpu_device *adev,
							     u16 *vddc, u16 *vddci,
							     u16 virtual_voltage_id,
							     u16 vbios_voltage_id);

int amdgpu_atombios_get_voltage_evv(struct amdgpu_device *adev,
				    u16 virtual_voltage_id,
				    u16 *voltage);

bool
amdgpu_atombios_is_voltage_gpio(struct amdgpu_device *adev,
				u8 voltage_type, u8 voltage_mode);

int amdgpu_atombios_get_voltage_table(struct amdgpu_device *adev,
				      u8 voltage_type, u8 voltage_mode,
				      struct atom_voltage_table *voltage_table);

int amdgpu_atombios_init_mc_reg_table(struct amdgpu_device *adev,
				      u8 module_index,
				      struct atom_mc_reg_table *reg_table);

bool amdgpu_atombios_has_gpu_virtualization_table(struct amdgpu_device *adev);

void amdgpu_atombios_scratch_regs_lock(struct amdgpu_device *adev, bool lock);
void amdgpu_atombios_scratch_regs_init(struct amdgpu_device *adev);
void amdgpu_atombios_scratch_regs_save(struct amdgpu_device *adev);
void amdgpu_atombios_scratch_regs_restore(struct amdgpu_device *adev);
void amdgpu_atombios_scratch_regs_engine_hung(struct amdgpu_device *adev,
					      bool hung);

void amdgpu_atombios_copy_swap(u8 *dst, u8 *src, u8 num_bytes, bool to_le);
int amdgpu_atombios_get_max_vddc(struct amdgpu_device *adev, u8 voltage_type,
			     u16 voltage_id, u16 *voltage);
int amdgpu_atombios_get_leakage_vddc_based_on_leakage_idx(struct amdgpu_device *adev,
						      u16 *voltage,
						      u16 leakage_idx);
void amdgpu_atombios_get_default_voltages(struct amdgpu_device *adev,
					  u16 *vddc, u16 *vddci, u16 *mvdd);
int amdgpu_atombios_get_clock_dividers(struct amdgpu_device *adev,
				       u8 clock_type,
				       u32 clock,
				       bool strobe_mode,
				       struct atom_clock_dividers *dividers);
int amdgpu_atombios_get_svi2_info(struct amdgpu_device *adev,
			      u8 voltage_type,
			      u8 *svd_gpio_id, u8 *svc_gpio_id);
#endif
