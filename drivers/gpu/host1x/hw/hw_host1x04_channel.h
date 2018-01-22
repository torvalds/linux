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

#ifndef HOST1X_HW_HOST1X04_CHANNEL_H
#define HOST1X_HW_HOST1X04_CHANNEL_H

static inline u32 host1x_channel_fifostat_r(void)
{
	return 0x0;
}
#define HOST1X_CHANNEL_FIFOSTAT \
	host1x_channel_fifostat_r()
static inline u32 host1x_channel_fifostat_cfempty_v(u32 r)
{
	return (r >> 11) & 0x1;
}
#define HOST1X_CHANNEL_FIFOSTAT_CFEMPTY_V(r) \
	host1x_channel_fifostat_cfempty_v(r)
static inline u32 host1x_channel_dmastart_r(void)
{
	return 0x14;
}
#define HOST1X_CHANNEL_DMASTART \
	host1x_channel_dmastart_r()
static inline u32 host1x_channel_dmaput_r(void)
{
	return 0x18;
}
#define HOST1X_CHANNEL_DMAPUT \
	host1x_channel_dmaput_r()
static inline u32 host1x_channel_dmaget_r(void)
{
	return 0x1c;
}
#define HOST1X_CHANNEL_DMAGET \
	host1x_channel_dmaget_r()
static inline u32 host1x_channel_dmaend_r(void)
{
	return 0x20;
}
#define HOST1X_CHANNEL_DMAEND \
	host1x_channel_dmaend_r()
static inline u32 host1x_channel_dmactrl_r(void)
{
	return 0x24;
}
#define HOST1X_CHANNEL_DMACTRL \
	host1x_channel_dmactrl_r()
static inline u32 host1x_channel_dmactrl_dmastop(void)
{
	return 1 << 0;
}
#define HOST1X_CHANNEL_DMACTRL_DMASTOP \
	host1x_channel_dmactrl_dmastop()
static inline u32 host1x_channel_dmactrl_dmastop_v(u32 r)
{
	return (r >> 0) & 0x1;
}
#define HOST1X_CHANNEL_DMACTRL_DMASTOP_V(r) \
	host1x_channel_dmactrl_dmastop_v(r)
static inline u32 host1x_channel_dmactrl_dmagetrst(void)
{
	return 1 << 1;
}
#define HOST1X_CHANNEL_DMACTRL_DMAGETRST \
	host1x_channel_dmactrl_dmagetrst()
static inline u32 host1x_channel_dmactrl_dmainitget(void)
{
	return 1 << 2;
}
#define HOST1X_CHANNEL_DMACTRL_DMAINITGET \
	host1x_channel_dmactrl_dmainitget()
static inline u32 host1x_channel_channelctrl_r(void)
{
	return 0x98;
}
#define HOST1X_CHANNEL_CHANNELCTRL \
	host1x_channel_channelctrl_r()
static inline u32 host1x_channel_channelctrl_kernel_filter_gbuffer_f(u32 v)
{
	return (v & 0x1) << 2;
}
#define HOST1X_CHANNEL_CHANNELCTRL_KERNEL_FILTER_GBUFFER(v) \
	host1x_channel_channelctrl_kernel_filter_gbuffer_f(v)

#endif
