/*
 * drivers/video/tegra/host/nvhost_hardware.h
 *
 * Tegra Graphics Host Register Offsets
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __NVHOST_HARDWARE_H
#define __NVHOST_HARDWARE_H

#include <linux/types.h>
#include <linux/bitops.h>

/* class ids */
enum {
	NV_HOST1X_CLASS_ID = 0x1,
	NV_VIDEO_ENCODE_MPEG_CLASS_ID = 0x20,
	NV_GRAPHICS_3D_CLASS_ID = 0x60
};


/* channel registers */
#define NV_HOST1X_CHANNELS 8
#define NV_HOST1X_CHANNEL0_BASE 0
#define NV_HOST1X_CHANNEL_MAP_SIZE_BYTES 16384


#define HOST1X_CHANNEL_FIFOSTAT		0x00
#define HOST1X_CHANNEL_INDDATA		0x0c
#define HOST1X_CHANNEL_DMASTART		0x14
#define HOST1X_CHANNEL_DMAPUT		0x18
#define HOST1X_CHANNEL_DMAGET		0x1c
#define HOST1X_CHANNEL_DMAEND		0x20
#define HOST1X_CHANNEL_DMACTRL		0x24

#define HOST1X_SYNC_CF_SETUP(x)		(0x3080 + (4 * (x)))

#define HOST1X_SYNC_SYNCPT_BASE(x)	(0x3600 + (4 * (x)))

#define HOST1X_SYNC_CBREAD(x)		(0x3720 + (4 * (x)))
#define HOST1X_SYNC_CFPEEK_CTRL		0x374c
#define HOST1X_SYNC_CFPEEK_READ		0x3750
#define HOST1X_SYNC_CFPEEK_PTRS		0x3754
#define HOST1X_SYNC_CBSTAT(x)		(0x3758 + (4 * (x)))

static inline unsigned nvhost_channel_fifostat_outfentries(u32 reg)
{
	return (reg >> 24) & 0x1f;
}

static inline u32 nvhost_channel_dmactrl(bool stop, bool get_rst, bool init_get)
{
	u32 v = stop ? 1 : 0;
	if (get_rst)
		v |= 2;
	if (init_get)
		v |= 4;
	return v;
}


/* sync registers */
#define NV_HOST1X_SYNCPT_NB_PTS 32
#define NV_HOST1X_SYNCPT_NB_BASES 8
#define NV_HOST1X_NB_MLOCKS 16
#define HOST1X_CHANNEL_SYNC_REG_BASE 12288

enum {
	HOST1X_SYNC_INTMASK = 0x4,
	HOST1X_SYNC_INTC0MASK = 0x8,
	HOST1X_SYNC_HINTSTATUS = 0x20,
	HOST1X_SYNC_HINTMASK = 0x24,
	HOST1X_SYNC_HINTSTATUS_EXT = 0x28,
	HOST1X_SYNC_HINTMASK_EXT = 0x2c,
	HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS = 0x40,
	HOST1X_SYNC_SYNCPT_THRESH_INT_MASK_0 = 0x50,
	HOST1X_SYNC_SYNCPT_THRESH_INT_MASK_1 = 0x54,
	HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE = 0x60,
	HOST1X_SYNC_SYNCPT_THRESH_INT_ENABLE_CPU0 = 0x68,
	HOST1X_SYNC_USEC_CLK = 0x1a4,
	HOST1X_SYNC_CTXSW_TIMEOUT_CFG = 0x1a8,
	HOST1X_SYNC_IP_BUSY_TIMEOUT = 0x1bc,
	HOST1X_SYNC_IP_READ_TIMEOUT_ADDR = 0x1c0,
	HOST1X_SYNC_IP_WRITE_TIMEOUT_ADDR = 0x1c4,
	HOST1X_SYNC_MLOCK_0 = 0x2c0,
	HOST1X_SYNC_MLOCK_OWNER_0 = 0x340,
	HOST1X_SYNC_SYNCPT_0 = 0x400,
	HOST1X_SYNC_SYNCPT_INT_THRESH_0 = 0x500,
	HOST1X_SYNC_SYNCPT_BASE_0 = 0x600,
	HOST1X_SYNC_SYNCPT_CPU_INCR = 0x700
};

static inline bool nvhost_sync_hintstatus_ext_ip_read_int(u32 reg)
{
	return (reg & BIT(30)) != 0;
}

static inline bool nvhost_sync_hintstatus_ext_ip_write_int(u32 reg)
{
	return (reg & BIT(31)) != 0;
}

static inline bool nvhost_sync_mlock_owner_ch_owns(u32 reg)
{
	return (reg & BIT(0)) != 0;
}

static inline bool nvhost_sync_mlock_owner_cpu_owns(u32 reg)
{
	return (reg & BIT(1)) != 0;
}

static inline unsigned int nvhost_sync_mlock_owner_owner_chid(u32 reg)
{
	return (reg >> 8) & 0xf;
}


/* host class */
enum {
	NV_CLASS_HOST_INCR_SYNCPT = 0x0,
	NV_CLASS_HOST_WAIT_SYNCPT = 0x8,
	NV_CLASS_HOST_WAIT_SYNCPT_BASE = 0x9,
	NV_CLASS_HOST_INCR_SYNCPT_BASE = 0xc,
	NV_CLASS_HOST_INDOFF = 0x2d,
	NV_CLASS_HOST_INDDATA = 0x2e
};

static inline u32 nvhost_class_host_wait_syncpt_base(
	unsigned indx, unsigned base_indx, unsigned offset)
{
	return (indx << 24) | (base_indx << 16) | offset;
}

static inline u32 nvhost_class_host_incr_syncpt_base(
	unsigned base_indx, unsigned offset)
{
	return (base_indx << 24) | offset;
}

enum {
	NV_HOST_MODULE_HOST1X = 0,
	NV_HOST_MODULE_MPE = 1,
	NV_HOST_MODULE_GR3D = 6
};

static inline u32 nvhost_class_host_indoff_reg_write(
	unsigned mod_id, unsigned offset, bool auto_inc)
{
	u32 v = (0xf << 28) | (mod_id << 18) | (offset << 2);
	if (auto_inc)
		v |= BIT(27);
	return v;
}

static inline u32 nvhost_class_host_indoff_reg_read(
	unsigned mod_id, unsigned offset, bool auto_inc)
{
	u32 v = (mod_id << 18) | (offset << 2) | 1;
	if (auto_inc)
		v |= BIT(27);
	return v;
}


/* cdma opcodes */
static inline u32 nvhost_opcode_setclass(
	unsigned class_id, unsigned offset, unsigned mask)
{
	return (0 << 28) | (offset << 16) | (class_id << 6) | mask;
}

static inline u32 nvhost_opcode_incr(unsigned offset, unsigned count)
{
	return (1 << 28) | (offset << 16) | count;
}

static inline u32 nvhost_opcode_nonincr(unsigned offset, unsigned count)
{
	return (2 << 28) | (offset << 16) | count;
}

static inline u32 nvhost_opcode_mask(unsigned offset, unsigned mask)
{
	return (3 << 28) | (offset << 16) | mask;
}

static inline u32 nvhost_opcode_imm(unsigned offset, unsigned value)
{
	return (4 << 28) | (offset << 16) | value;
}

static inline u32 nvhost_opcode_restart(unsigned address)
{
	return (5 << 28) | (address >> 4);
}

static inline u32 nvhost_opcode_gather(unsigned offset, unsigned count)
{
	return (6 << 28) | (offset << 16) | count;
}

static inline u32 nvhost_opcode_gather_nonincr(unsigned offset,	unsigned count)
{
	return (6 << 28) | (offset << 16) | BIT(15) | count;
}

static inline u32 nvhost_opcode_gather_incr(unsigned offset, unsigned count)
{
	return (6 << 28) | (offset << 16) | BIT(15) | BIT(14) | count;
}

#define NVHOST_OPCODE_NOOP nvhost_opcode_nonincr(0, 0)



#endif /* __NVHOST_HARDWARE_H */

