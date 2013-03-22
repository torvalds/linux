/*
 * Copyright (c) 2012-2013, NVIDIA Corporation.
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

#ifndef __hw_host1x01_sync_h__
#define __hw_host1x01_sync_h__

#define REGISTER_STRIDE	4

static inline u32 host1x_sync_syncpt_r(unsigned int id)
{
	return 0x400 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_SYNCPT(id) \
	host1x_sync_syncpt_r(id)
static inline u32 host1x_sync_syncpt_thresh_cpu0_int_status_r(unsigned int id)
{
	return 0x40 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(id) \
	host1x_sync_syncpt_thresh_cpu0_int_status_r(id)
static inline u32 host1x_sync_syncpt_thresh_int_disable_r(unsigned int id)
{
	return 0x60 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE(id) \
	host1x_sync_syncpt_thresh_int_disable_r(id)
static inline u32 host1x_sync_syncpt_thresh_int_enable_cpu0_r(unsigned int id)
{
	return 0x68 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_SYNCPT_THRESH_INT_ENABLE_CPU0(id) \
	host1x_sync_syncpt_thresh_int_enable_cpu0_r(id)
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
static inline u32 host1x_sync_syncpt_int_thresh_r(unsigned int id)
{
	return 0x500 + id * REGISTER_STRIDE;
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
	return 0x700 + id * REGISTER_STRIDE;
}
#define HOST1X_SYNC_SYNCPT_CPU_INCR(id) \
	host1x_sync_syncpt_cpu_incr_r(id)
#endif /* __hw_host1x01_sync_h__ */
