/*
 * Copyright (c) 2013 NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

 /*
  * Function naming determines intended use:
  *
  *     <x>_r(void) : Returns the offset for register <x>.
  *
  *     <x>_w(void) : Returns the word offset for word (4 byte) element <x>.
  *
  *     <x>_<y>_s(void) : Returns size of field <y> of register <x> in bits.
  *
  *     <x>_<y>_f(u32 v) : Returns a value based on 'v' which has been shifted
  *         and masked to place it at field <y> of register <x>.  This value
  *         can be |'d with others to produce a full register value for
  *         register <x>.
  *
  *     <x>_<y>_m(void) : Returns a mask for field <y> of register <x>.  This
  *         value can be ~'d and then &'d to clear the value of field <y> for
  *         register <x>.
  *
  *     <x>_<y>_<z>_f(void) : Returns the constant value <z> after being shifted
  *         to place it at field <y> of register <x>.  This value can be |'d
  *         with others to produce a full register value for <x>.
  *
  *     <x>_<y>_v(u32 r) : Returns the value of field <y> from a full register
  *         <x> value 'r' after being shifted to place its LSB at bit 0.
  *         This value is suitable for direct comparison with other unshifted
  *         values appropriate for use in field <y> of register <x>.
  *
  *     <x>_<y>_<z>_v(void) : Returns the constant value for <z> defined for
  *         field <y> of register <x>.  This value is suitable for direct
  *         comparison with unshifted values appropriate for use in field <y>
  *         of register <x>.
  */

#ifndef HOST1X_HW_HOST1X04_SYNC_H
#define HOST1X_HW_HOST1X04_SYNC_H

#define REGISTER_STRIDE	4

static inline u32 host1x_sync_syncpt_r(unsigned int id)
{
	return 0xf80 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_SYNCPT(id) \
	host1x_sync_syncpt_r(id)
static inline u32 host1x_sync_syncpt_thresh_cpu0_int_status_r(unsigned int id)
{
	return 0xe80 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(id) \
	host1x_sync_syncpt_thresh_cpu0_int_status_r(id)
static inline u32 host1x_sync_syncpt_thresh_int_disable_r(unsigned int id)
{
	return 0xf00 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE(id) \
	host1x_sync_syncpt_thresh_int_disable_r(id)
static inline u32 host1x_sync_syncpt_thresh_int_enable_cpu0_r(unsigned int id)
{
	return 0xf20 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_SYNCPT_THRESH_INT_ENABLE_CPU0(id) \
	host1x_sync_syncpt_thresh_int_enable_cpu0_r(id)
static inline u32 host1x_sync_cf_setup_r(unsigned int channel)
{
	return 0xc00 + channel * REGISTER_STRIDE;
}
#define HOST1X_SYNC_CF_SETUP(channel) \
	host1x_sync_cf_setup_r(channel)
static inline u32 host1x_sync_cf_setup_base_v(u32 r)
{
	return (r >> 0) & 0x3ff;
}
#define HOST1X_SYNC_CF_SETUP_BASE_V(r) \
	host1x_sync_cf_setup_base_v(r)
static inline u32 host1x_sync_cf_setup_limit_v(u32 r)
{
	return (r >> 16) & 0x3ff;
}
#define HOST1X_SYNC_CF_SETUP_LIMIT_V(r) \
	host1x_sync_cf_setup_limit_v(r)
static inline u32 host1x_sync_cmdproc_stop_r(void)
{
	return 0xac;
}
#define HOST1X_SYNC_CMDPROC_STOP \
	host1x_sync_cmdproc_stop_r()
static inline u32 host1x_sync_ch_teardown_r(void)
{
	return 0xb0;
}
#define HOST1X_SYNC_CH_TEARDOWN \
	host1x_sync_ch_teardown_r()
static inline u32 host1x_sync_usec_clk_r(void)
{
	return 0x1a4;
}
#define HOST1X_SYNC_USEC_CLK \
	host1x_sync_usec_clk_r()
static inline u32 host1x_sync_ctxsw_timeout_cfg_r(void)
{
	return 0x1a8;
}
#define HOST1X_SYNC_CTXSW_TIMEOUT_CFG \
	host1x_sync_ctxsw_timeout_cfg_r()
static inline u32 host1x_sync_ip_busy_timeout_r(void)
{
	return 0x1bc;
}
#define HOST1X_SYNC_IP_BUSY_TIMEOUT \
	host1x_sync_ip_busy_timeout_r()
static inline u32 host1x_sync_mlock_owner_r(unsigned int id)
{
	return 0x340 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_MLOCK_OWNER(id) \
	host1x_sync_mlock_owner_r(id)
static inline u32 host1x_sync_mlock_owner_chid_f(u32 v)
{
	return (v & 0xf) << 8;
}
#define HOST1X_SYNC_MLOCK_OWNER_CHID_F(v) \
	host1x_sync_mlock_owner_chid_f(v)
static inline u32 host1x_sync_mlock_owner_cpu_owns_v(u32 r)
{
	return (r >> 1) & 0x1;
}
#define HOST1X_SYNC_MLOCK_OWNER_CPU_OWNS_V(r) \
	host1x_sync_mlock_owner_cpu_owns_v(r)
static inline u32 host1x_sync_mlock_owner_ch_owns_v(u32 r)
{
	return (r >> 0) & 0x1;
}
#define HOST1X_SYNC_MLOCK_OWNER_CH_OWNS_V(r) \
	host1x_sync_mlock_owner_ch_owns_v(r)
static inline u32 host1x_sync_syncpt_int_thresh_r(unsigned int id)
{
	return 0x1380 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_SYNCPT_INT_THRESH(id) \
	host1x_sync_syncpt_int_thresh_r(id)
static inline u32 host1x_sync_syncpt_base_r(unsigned int id)
{
	return 0x600 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_SYNCPT_BASE(id) \
	host1x_sync_syncpt_base_r(id)
static inline u32 host1x_sync_syncpt_cpu_incr_r(unsigned int id)
{
	return 0xf60 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_SYNCPT_CPU_INCR(id) \
	host1x_sync_syncpt_cpu_incr_r(id)
static inline u32 host1x_sync_cbread_r(unsigned int channel)
{
	return 0xc80 + channel * REGISTER_STRIDE;
}
#define HOST1X_SYNC_CBREAD(channel) \
	host1x_sync_cbread_r(channel)
static inline u32 host1x_sync_cfpeek_ctrl_r(void)
{
	return 0x74c;
}
#define HOST1X_SYNC_CFPEEK_CTRL \
	host1x_sync_cfpeek_ctrl_r()
static inline u32 host1x_sync_cfpeek_ctrl_addr_f(u32 v)
{
	return (v & 0x3ff) << 0;
}
#define HOST1X_SYNC_CFPEEK_CTRL_ADDR_F(v) \
	host1x_sync_cfpeek_ctrl_addr_f(v)
static inline u32 host1x_sync_cfpeek_ctrl_channr_f(u32 v)
{
	return (v & 0xf) << 16;
}
#define HOST1X_SYNC_CFPEEK_CTRL_CHANNR_F(v) \
	host1x_sync_cfpeek_ctrl_channr_f(v)
static inline u32 host1x_sync_cfpeek_ctrl_ena_f(u32 v)
{
	return (v & 0x1) << 31;
}
#define HOST1X_SYNC_CFPEEK_CTRL_ENA_F(v) \
	host1x_sync_cfpeek_ctrl_ena_f(v)
static inline u32 host1x_sync_cfpeek_read_r(void)
{
	return 0x750;
}
#define HOST1X_SYNC_CFPEEK_READ \
	host1x_sync_cfpeek_read_r()
static inline u32 host1x_sync_cfpeek_ptrs_r(void)
{
	return 0x754;
}
#define HOST1X_SYNC_CFPEEK_PTRS \
	host1x_sync_cfpeek_ptrs_r()
static inline u32 host1x_sync_cfpeek_ptrs_cf_rd_ptr_v(u32 r)
{
	return (r >> 0) & 0x3ff;
}
#define HOST1X_SYNC_CFPEEK_PTRS_CF_RD_PTR_V(r) \
	host1x_sync_cfpeek_ptrs_cf_rd_ptr_v(r)
static inline u32 host1x_sync_cfpeek_ptrs_cf_wr_ptr_v(u32 r)
{
	return (r >> 16) & 0x3ff;
}
#define HOST1X_SYNC_CFPEEK_PTRS_CF_WR_PTR_V(r) \
	host1x_sync_cfpeek_ptrs_cf_wr_ptr_v(r)
static inline u32 host1x_sync_cbstat_r(unsigned int channel)
{
	return 0xcc0 + channel * REGISTER_STRIDE;
}
#define HOST1X_SYNC_CBSTAT(channel) \
	host1x_sync_cbstat_r(channel)
static inline u32 host1x_sync_cbstat_cboffset_v(u32 r)
{
	return (r >> 0) & 0xffff;
}
#define HOST1X_SYNC_CBSTAT_CBOFFSET_V(r) \
	host1x_sync_cbstat_cboffset_v(r)
static inline u32 host1x_sync_cbstat_cbclass_v(u32 r)
{
	return (r >> 16) & 0x3ff;
}
#define HOST1X_SYNC_CBSTAT_CBCLASS_V(r) \
	host1x_sync_cbstat_cbclass_v(r)

#endif
