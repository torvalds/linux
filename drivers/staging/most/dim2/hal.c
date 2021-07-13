// SPDX-License-Identifier: GPL-2.0
/*
 * hal.c - DIM2 HAL implementation
 * (MediaLB, Device Interface Macro IP, OS62420)
 *
 * Copyright (C) 2015-2016, Microchip Technology Germany II GmbH & Co. KG
 */

/* Author: Andrey Shvetsov <andrey.shvetsov@k2l.de> */

#include "hal.h"
#include "errors.h"
#include "reg.h"
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/io.h>

/*
 * Size factor for isochronous DBR buffer.
 * Minimal value is 3.
 */
#define ISOC_DBR_FACTOR 3u

/*
 * Number of 32-bit units for DBR map.
 *
 * 1: block size is 512, max allocation is 16K
 * 2: block size is 256, max allocation is 8K
 * 4: block size is 128, max allocation is 4K
 * 8: block size is 64, max allocation is 2K
 *
 * Min allocated space is block size.
 * Max possible allocated space is 32 blocks.
 */
#define DBR_MAP_SIZE 2

/* -------------------------------------------------------------------------- */
/* not configurable area */

#define CDT 0x00
#define ADT 0x40
#define MLB_CAT 0x80
#define AHB_CAT 0x88

#define DBR_SIZE  (16 * 1024) /* specified by IP */
#define DBR_BLOCK_SIZE  (DBR_SIZE / 32 / DBR_MAP_SIZE)

#define ROUND_UP_TO(x, d)  (DIV_ROUND_UP(x, (d)) * (d))

/* -------------------------------------------------------------------------- */
/* generic helper functions and macros */

static inline u32 bit_mask(u8 position)
{
	return (u32)1 << position;
}

static inline bool dim_on_error(u8 error_id, const char *error_message)
{
	dimcb_on_error(error_id, error_message);
	return false;
}

/* -------------------------------------------------------------------------- */
/* types and local variables */

struct async_tx_dbr {
	u8 ch_addr;
	u16 rpc;
	u16 wpc;
	u16 rest_size;
	u16 sz_queue[CDT0_RPC_MASK + 1];
};

struct lld_global_vars_t {
	bool dim_is_initialized;
	bool mcm_is_initialized;
	struct dim2_regs __iomem *dim2; /* DIM2 core base address */
	struct async_tx_dbr atx_dbr;
	u32 fcnt;
	u32 dbr_map[DBR_MAP_SIZE];
};

static struct lld_global_vars_t g = { false };

/* -------------------------------------------------------------------------- */

static int dbr_get_mask_size(u16 size)
{
	int i;

	for (i = 0; i < 6; i++)
		if (size <= (DBR_BLOCK_SIZE << i))
			return 1 << i;
	return 0;
}

/**
 * alloc_dbr() - Allocates DBR memory.
 * @size: Allocating memory size.
 * Returns: Offset in DBR memory by success or DBR_SIZE if out of memory.
 */
static int alloc_dbr(u16 size)
{
	int mask_size;
	int i, block_idx = 0;

	if (size <= 0)
		return DBR_SIZE; /* out of memory */

	mask_size = dbr_get_mask_size(size);
	if (mask_size == 0)
		return DBR_SIZE; /* out of memory */

	for (i = 0; i < DBR_MAP_SIZE; i++) {
		u32 const blocks = DIV_ROUND_UP(size, DBR_BLOCK_SIZE);
		u32 mask = ~((~(u32)0) << blocks);

		do {
			if ((g.dbr_map[i] & mask) == 0) {
				g.dbr_map[i] |= mask;
				return block_idx * DBR_BLOCK_SIZE;
			}
			block_idx += mask_size;
			/* do shift left with 2 steps in case mask_size == 32 */
			mask <<= mask_size - 1;
		} while ((mask <<= 1) != 0);
	}

	return DBR_SIZE; /* out of memory */
}

static void free_dbr(int offs, int size)
{
	int block_idx = offs / DBR_BLOCK_SIZE;
	u32 const blocks = DIV_ROUND_UP(size, DBR_BLOCK_SIZE);
	u32 mask = ~((~(u32)0) << blocks);

	mask <<= block_idx % 32;
	g.dbr_map[block_idx / 32] &= ~mask;
}

/* -------------------------------------------------------------------------- */

static void dim2_transfer_madr(u32 val)
{
	writel(val, &g.dim2->MADR);

	/* wait for transfer completion */
	while ((readl(&g.dim2->MCTL) & 1) != 1)
		continue;

	writel(0, &g.dim2->MCTL);   /* clear transfer complete */
}

static void dim2_clear_dbr(u16 addr, u16 size)
{
	enum { MADR_TB_BIT = 30, MADR_WNR_BIT = 31 };

	u16 const end_addr = addr + size;
	u32 const cmd = bit_mask(MADR_WNR_BIT) | bit_mask(MADR_TB_BIT);

	writel(0, &g.dim2->MCTL);   /* clear transfer complete */
	writel(0, &g.dim2->MDAT0);

	for (; addr < end_addr; addr++)
		dim2_transfer_madr(cmd | addr);
}

static u32 dim2_read_ctr(u32 ctr_addr, u16 mdat_idx)
{
	dim2_transfer_madr(ctr_addr);

	return readl((&g.dim2->MDAT0) + mdat_idx);
}

static void dim2_write_ctr_mask(u32 ctr_addr, const u32 *mask, const u32 *value)
{
	enum { MADR_WNR_BIT = 31 };

	writel(0, &g.dim2->MCTL);   /* clear transfer complete */

	if (mask[0] != 0)
		writel(value[0], &g.dim2->MDAT0);
	if (mask[1] != 0)
		writel(value[1], &g.dim2->MDAT1);
	if (mask[2] != 0)
		writel(value[2], &g.dim2->MDAT2);
	if (mask[3] != 0)
		writel(value[3], &g.dim2->MDAT3);

	writel(mask[0], &g.dim2->MDWE0);
	writel(mask[1], &g.dim2->MDWE1);
	writel(mask[2], &g.dim2->MDWE2);
	writel(mask[3], &g.dim2->MDWE3);

	dim2_transfer_madr(bit_mask(MADR_WNR_BIT) | ctr_addr);
}

static inline void dim2_write_ctr(u32 ctr_addr, const u32 *value)
{
	u32 const mask[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };

	dim2_write_ctr_mask(ctr_addr, mask, value);
}

static inline void dim2_clear_ctr(u32 ctr_addr)
{
	u32 const value[4] = { 0, 0, 0, 0 };

	dim2_write_ctr(ctr_addr, value);
}

static void dim2_configure_cat(u8 cat_base, u8 ch_addr, u8 ch_type,
			       bool read_not_write)
{
	bool isoc_fce = ch_type == CAT_CT_VAL_ISOC;
	bool sync_mfe = ch_type == CAT_CT_VAL_SYNC;
	u16 const cat =
		(read_not_write << CAT_RNW_BIT) |
		(ch_type << CAT_CT_SHIFT) |
		(ch_addr << CAT_CL_SHIFT) |
		(isoc_fce << CAT_FCE_BIT) |
		(sync_mfe << CAT_MFE_BIT) |
		(false << CAT_MT_BIT) |
		(true << CAT_CE_BIT);
	u8 const ctr_addr = cat_base + ch_addr / 8;
	u8 const idx = (ch_addr % 8) / 2;
	u8 const shift = (ch_addr % 2) * 16;
	u32 mask[4] = { 0, 0, 0, 0 };
	u32 value[4] = { 0, 0, 0, 0 };

	mask[idx] = (u32)0xFFFF << shift;
	value[idx] = cat << shift;
	dim2_write_ctr_mask(ctr_addr, mask, value);
}

static void dim2_clear_cat(u8 cat_base, u8 ch_addr)
{
	u8 const ctr_addr = cat_base + ch_addr / 8;
	u8 const idx = (ch_addr % 8) / 2;
	u8 const shift = (ch_addr % 2) * 16;
	u32 mask[4] = { 0, 0, 0, 0 };
	u32 value[4] = { 0, 0, 0, 0 };

	mask[idx] = (u32)0xFFFF << shift;
	dim2_write_ctr_mask(ctr_addr, mask, value);
}

static void dim2_configure_cdt(u8 ch_addr, u16 dbr_address, u16 hw_buffer_size,
			       u16 packet_length)
{
	u32 cdt[4] = { 0, 0, 0, 0 };

	if (packet_length)
		cdt[1] = ((packet_length - 1) << CDT1_BS_ISOC_SHIFT);

	cdt[3] =
		((hw_buffer_size - 1) << CDT3_BD_SHIFT) |
		(dbr_address << CDT3_BA_SHIFT);
	dim2_write_ctr(CDT + ch_addr, cdt);
}

static u16 dim2_rpc(u8 ch_addr)
{
	u32 cdt0 = dim2_read_ctr(CDT + ch_addr, 0);

	return (cdt0 >> CDT0_RPC_SHIFT) & CDT0_RPC_MASK;
}

static void dim2_clear_cdt(u8 ch_addr)
{
	u32 cdt[4] = { 0, 0, 0, 0 };

	dim2_write_ctr(CDT + ch_addr, cdt);
}

static void dim2_configure_adt(u8 ch_addr)
{
	u32 adt[4] = { 0, 0, 0, 0 };

	adt[0] =
		(true << ADT0_CE_BIT) |
		(true << ADT0_LE_BIT) |
		(0 << ADT0_PG_BIT);

	dim2_write_ctr(ADT + ch_addr, adt);
}

static void dim2_clear_adt(u8 ch_addr)
{
	u32 adt[4] = { 0, 0, 0, 0 };

	dim2_write_ctr(ADT + ch_addr, adt);
}

static void dim2_start_ctrl_async(u8 ch_addr, u8 idx, u32 buf_addr,
				  u16 buffer_size)
{
	u8 const shift = idx * 16;

	u32 mask[4] = { 0, 0, 0, 0 };
	u32 adt[4] = { 0, 0, 0, 0 };

	mask[1] =
		bit_mask(ADT1_PS_BIT + shift) |
		bit_mask(ADT1_RDY_BIT + shift) |
		(ADT1_CTRL_ASYNC_BD_MASK << (ADT1_BD_SHIFT + shift));
	adt[1] =
		(true << (ADT1_PS_BIT + shift)) |
		(true << (ADT1_RDY_BIT + shift)) |
		((buffer_size - 1) << (ADT1_BD_SHIFT + shift));

	mask[idx + 2] = 0xFFFFFFFF;
	adt[idx + 2] = buf_addr;

	dim2_write_ctr_mask(ADT + ch_addr, mask, adt);
}

static void dim2_start_isoc_sync(u8 ch_addr, u8 idx, u32 buf_addr,
				 u16 buffer_size)
{
	u8 const shift = idx * 16;

	u32 mask[4] = { 0, 0, 0, 0 };
	u32 adt[4] = { 0, 0, 0, 0 };

	mask[1] =
		bit_mask(ADT1_RDY_BIT + shift) |
		(ADT1_ISOC_SYNC_BD_MASK << (ADT1_BD_SHIFT + shift));
	adt[1] =
		(true << (ADT1_RDY_BIT + shift)) |
		((buffer_size - 1) << (ADT1_BD_SHIFT + shift));

	mask[idx + 2] = 0xFFFFFFFF;
	adt[idx + 2] = buf_addr;

	dim2_write_ctr_mask(ADT + ch_addr, mask, adt);
}

static void dim2_clear_ctram(void)
{
	u32 ctr_addr;

	for (ctr_addr = 0; ctr_addr < 0x90; ctr_addr++)
		dim2_clear_ctr(ctr_addr);
}

static void dim2_configure_channel(
	u8 ch_addr, u8 type, u8 is_tx, u16 dbr_address, u16 hw_buffer_size,
	u16 packet_length)
{
	dim2_configure_cdt(ch_addr, dbr_address, hw_buffer_size, packet_length);
	dim2_configure_cat(MLB_CAT, ch_addr, type, is_tx ? 1 : 0);

	dim2_configure_adt(ch_addr);
	dim2_configure_cat(AHB_CAT, ch_addr, type, is_tx ? 0 : 1);

	/* unmask interrupt for used channel, enable mlb_sys_int[0] interrupt */
	writel(readl(&g.dim2->ACMR0) | bit_mask(ch_addr), &g.dim2->ACMR0);
}

static void dim2_clear_channel(u8 ch_addr)
{
	/* mask interrupt for used channel, disable mlb_sys_int[0] interrupt */
	writel(readl(&g.dim2->ACMR0) & ~bit_mask(ch_addr), &g.dim2->ACMR0);

	dim2_clear_cat(AHB_CAT, ch_addr);
	dim2_clear_adt(ch_addr);

	dim2_clear_cat(MLB_CAT, ch_addr);
	dim2_clear_cdt(ch_addr);

	/* clear channel status bit */
	writel(bit_mask(ch_addr), &g.dim2->ACSR0);
}

/* -------------------------------------------------------------------------- */
/* trace async tx dbr fill state */

static inline u16 norm_pc(u16 pc)
{
	return pc & CDT0_RPC_MASK;
}

static void dbrcnt_init(u8 ch_addr, u16 dbr_size)
{
	g.atx_dbr.rest_size = dbr_size;
	g.atx_dbr.rpc = dim2_rpc(ch_addr);
	g.atx_dbr.wpc = g.atx_dbr.rpc;
}

static void dbrcnt_enq(int buf_sz)
{
	g.atx_dbr.rest_size -= buf_sz;
	g.atx_dbr.sz_queue[norm_pc(g.atx_dbr.wpc)] = buf_sz;
	g.atx_dbr.wpc++;
}

u16 dim_dbr_space(struct dim_channel *ch)
{
	u16 cur_rpc;
	struct async_tx_dbr *dbr = &g.atx_dbr;

	if (ch->addr != dbr->ch_addr)
		return 0xFFFF;

	cur_rpc = dim2_rpc(ch->addr);

	while (norm_pc(dbr->rpc) != cur_rpc) {
		dbr->rest_size += dbr->sz_queue[norm_pc(dbr->rpc)];
		dbr->rpc++;
	}

	if ((u16)(dbr->wpc - dbr->rpc) >= CDT0_RPC_MASK)
		return 0;

	return dbr->rest_size;
}

/* -------------------------------------------------------------------------- */
/* channel state helpers */

static void state_init(struct int_ch_state *state)
{
	state->request_counter = 0;
	state->service_counter = 0;

	state->idx1 = 0;
	state->idx2 = 0;
	state->level = 0;
}

/* -------------------------------------------------------------------------- */
/* macro helper functions */

static inline bool check_channel_address(u32 ch_address)
{
	return ch_address > 0 && (ch_address % 2) == 0 &&
	       (ch_address / 2) <= (u32)CAT_CL_MASK;
}

static inline bool check_packet_length(u32 packet_length)
{
	u16 const max_size = ((u16)CDT3_BD_ISOC_MASK + 1u) / ISOC_DBR_FACTOR;

	if (packet_length <= 0)
		return false; /* too small */

	if (packet_length > max_size)
		return false; /* too big */

	if (packet_length - 1u > (u32)CDT1_BS_ISOC_MASK)
		return false; /* too big */

	return true;
}

static inline bool check_bytes_per_frame(u32 bytes_per_frame)
{
	u16 const bd_factor = g.fcnt + 2;
	u16 const max_size = ((u16)CDT3_BD_MASK + 1u) >> bd_factor;

	if (bytes_per_frame <= 0)
		return false; /* too small */

	if (bytes_per_frame > max_size)
		return false; /* too big */

	return true;
}

u16 dim_norm_ctrl_async_buffer_size(u16 buf_size)
{
	u16 const max_size = (u16)ADT1_CTRL_ASYNC_BD_MASK + 1u;

	if (buf_size > max_size)
		return max_size;

	return buf_size;
}

static inline u16 norm_isoc_buffer_size(u16 buf_size, u16 packet_length)
{
	u16 n;
	u16 const max_size = (u16)ADT1_ISOC_SYNC_BD_MASK + 1u;

	if (buf_size > max_size)
		buf_size = max_size;

	n = buf_size / packet_length;

	if (n < 2u)
		return 0; /* too small buffer for given packet_length */

	return packet_length * n;
}

static inline u16 norm_sync_buffer_size(u16 buf_size, u16 bytes_per_frame)
{
	u16 n;
	u16 const max_size = (u16)ADT1_ISOC_SYNC_BD_MASK + 1u;
	u32 const unit = bytes_per_frame << g.fcnt;

	if (buf_size > max_size)
		buf_size = max_size;

	n = buf_size / unit;

	if (n < 1u)
		return 0; /* too small buffer for given bytes_per_frame */

	return unit * n;
}

static void dim2_cleanup(void)
{
	/* disable MediaLB */
	writel(false << MLBC0_MLBEN_BIT, &g.dim2->MLBC0);

	dim2_clear_ctram();

	/* disable mlb_int interrupt */
	writel(0, &g.dim2->MIEN);

	/* clear status for all dma channels */
	writel(0xFFFFFFFF, &g.dim2->ACSR0);
	writel(0xFFFFFFFF, &g.dim2->ACSR1);

	/* mask interrupts for all channels */
	writel(0, &g.dim2->ACMR0);
	writel(0, &g.dim2->ACMR1);
}

static void dim2_initialize(bool enable_6pin, u8 mlb_clock)
{
	dim2_cleanup();

	/* configure and enable MediaLB */
	writel(enable_6pin << MLBC0_MLBPEN_BIT |
	       mlb_clock << MLBC0_MLBCLK_SHIFT |
	       g.fcnt << MLBC0_FCNT_SHIFT |
	       true << MLBC0_MLBEN_BIT,
	       &g.dim2->MLBC0);

	/* activate all HBI channels */
	writel(0xFFFFFFFF, &g.dim2->HCMR0);
	writel(0xFFFFFFFF, &g.dim2->HCMR1);

	/* enable HBI */
	writel(bit_mask(HCTL_EN_BIT), &g.dim2->HCTL);

	/* configure DMA */
	writel(ACTL_DMA_MODE_VAL_DMA_MODE_1 << ACTL_DMA_MODE_BIT |
	       true << ACTL_SCE_BIT, &g.dim2->ACTL);
}

static bool dim2_is_mlb_locked(void)
{
	u32 const mask0 = bit_mask(MLBC0_MLBLK_BIT);
	u32 const mask1 = bit_mask(MLBC1_CLKMERR_BIT) |
			  bit_mask(MLBC1_LOCKERR_BIT);
	u32 const c1 = readl(&g.dim2->MLBC1);
	u32 const nda_mask = (u32)MLBC1_NDA_MASK << MLBC1_NDA_SHIFT;

	writel(c1 & nda_mask, &g.dim2->MLBC1);
	return (readl(&g.dim2->MLBC1) & mask1) == 0 &&
	       (readl(&g.dim2->MLBC0) & mask0) != 0;
}

/* -------------------------------------------------------------------------- */
/* channel help routines */

static inline bool service_channel(u8 ch_addr, u8 idx)
{
	u8 const shift = idx * 16;
	u32 const adt1 = dim2_read_ctr(ADT + ch_addr, 1);
	u32 mask[4] = { 0, 0, 0, 0 };
	u32 adt_w[4] = { 0, 0, 0, 0 };

	if (((adt1 >> (ADT1_DNE_BIT + shift)) & 1) == 0)
		return false;

	mask[1] =
		bit_mask(ADT1_DNE_BIT + shift) |
		bit_mask(ADT1_ERR_BIT + shift) |
		bit_mask(ADT1_RDY_BIT + shift);
	dim2_write_ctr_mask(ADT + ch_addr, mask, adt_w);

	/* clear channel status bit */
	writel(bit_mask(ch_addr), &g.dim2->ACSR0);

	return true;
}

/* -------------------------------------------------------------------------- */
/* channel init routines */

static void isoc_init(struct dim_channel *ch, u8 ch_addr, u16 packet_length)
{
	state_init(&ch->state);

	ch->addr = ch_addr;

	ch->packet_length = packet_length;
	ch->bytes_per_frame = 0;
	ch->done_sw_buffers_number = 0;
}

static void sync_init(struct dim_channel *ch, u8 ch_addr, u16 bytes_per_frame)
{
	state_init(&ch->state);

	ch->addr = ch_addr;

	ch->packet_length = 0;
	ch->bytes_per_frame = bytes_per_frame;
	ch->done_sw_buffers_number = 0;
}

static void channel_init(struct dim_channel *ch, u8 ch_addr)
{
	state_init(&ch->state);

	ch->addr = ch_addr;

	ch->packet_length = 0;
	ch->bytes_per_frame = 0;
	ch->done_sw_buffers_number = 0;
}

/* returns true if channel interrupt state is cleared */
static bool channel_service_interrupt(struct dim_channel *ch)
{
	struct int_ch_state *const state = &ch->state;

	if (!service_channel(ch->addr, state->idx2))
		return false;

	state->idx2 ^= 1;
	state->request_counter++;
	return true;
}

static bool channel_start(struct dim_channel *ch, u32 buf_addr, u16 buf_size)
{
	struct int_ch_state *const state = &ch->state;

	if (buf_size <= 0)
		return dim_on_error(DIM_ERR_BAD_BUFFER_SIZE, "Bad buffer size");

	if (ch->packet_length == 0 && ch->bytes_per_frame == 0 &&
	    buf_size != dim_norm_ctrl_async_buffer_size(buf_size))
		return dim_on_error(DIM_ERR_BAD_BUFFER_SIZE,
				    "Bad control/async buffer size");

	if (ch->packet_length &&
	    buf_size != norm_isoc_buffer_size(buf_size, ch->packet_length))
		return dim_on_error(DIM_ERR_BAD_BUFFER_SIZE,
				    "Bad isochronous buffer size");

	if (ch->bytes_per_frame &&
	    buf_size != norm_sync_buffer_size(buf_size, ch->bytes_per_frame))
		return dim_on_error(DIM_ERR_BAD_BUFFER_SIZE,
				    "Bad synchronous buffer size");

	if (state->level >= 2u)
		return dim_on_error(DIM_ERR_OVERFLOW, "Channel overflow");

	++state->level;

	if (ch->addr == g.atx_dbr.ch_addr)
		dbrcnt_enq(buf_size);

	if (ch->packet_length || ch->bytes_per_frame)
		dim2_start_isoc_sync(ch->addr, state->idx1, buf_addr, buf_size);
	else
		dim2_start_ctrl_async(ch->addr, state->idx1, buf_addr,
				      buf_size);
	state->idx1 ^= 1;

	return true;
}

static u8 channel_service(struct dim_channel *ch)
{
	struct int_ch_state *const state = &ch->state;

	if (state->service_counter != state->request_counter) {
		state->service_counter++;
		if (state->level == 0)
			return DIM_ERR_UNDERFLOW;

		--state->level;
		ch->done_sw_buffers_number++;
	}

	return DIM_NO_ERROR;
}

static bool channel_detach_buffers(struct dim_channel *ch, u16 buffers_number)
{
	if (buffers_number > ch->done_sw_buffers_number)
		return dim_on_error(DIM_ERR_UNDERFLOW, "Channel underflow");

	ch->done_sw_buffers_number -= buffers_number;
	return true;
}

/* -------------------------------------------------------------------------- */
/* API */

u8 dim_startup(struct dim2_regs __iomem *dim_base_address, u32 mlb_clock,
	       u32 fcnt)
{
	g.dim_is_initialized = false;

	if (!dim_base_address)
		return DIM_INIT_ERR_DIM_ADDR;

	/* MediaLB clock: 0 - 256 fs, 1 - 512 fs, 2 - 1024 fs, 3 - 2048 fs */
	/* MediaLB clock: 4 - 3072 fs, 5 - 4096 fs, 6 - 6144 fs, 7 - 8192 fs */
	if (mlb_clock >= 8)
		return DIM_INIT_ERR_MLB_CLOCK;

	if (fcnt > MLBC0_FCNT_MAX_VAL)
		return DIM_INIT_ERR_MLB_CLOCK;

	g.dim2 = dim_base_address;
	g.fcnt = fcnt;
	g.dbr_map[0] = 0;
	g.dbr_map[1] = 0;

	dim2_initialize(mlb_clock >= 3, mlb_clock);

	g.dim_is_initialized = true;

	return DIM_NO_ERROR;
}

void dim_shutdown(void)
{
	g.dim_is_initialized = false;
	dim2_cleanup();
}

bool dim_get_lock_state(void)
{
	return dim2_is_mlb_locked();
}

static u8 init_ctrl_async(struct dim_channel *ch, u8 type, u8 is_tx,
			  u16 ch_address, u16 hw_buffer_size)
{
	if (!g.dim_is_initialized || !ch)
		return DIM_ERR_DRIVER_NOT_INITIALIZED;

	if (!check_channel_address(ch_address))
		return DIM_INIT_ERR_CHANNEL_ADDRESS;

	if (!ch->dbr_size)
		ch->dbr_size = ROUND_UP_TO(hw_buffer_size, DBR_BLOCK_SIZE);
	ch->dbr_addr = alloc_dbr(ch->dbr_size);
	if (ch->dbr_addr >= DBR_SIZE)
		return DIM_INIT_ERR_OUT_OF_MEMORY;

	channel_init(ch, ch_address / 2);

	dim2_configure_channel(ch->addr, type, is_tx,
			       ch->dbr_addr, ch->dbr_size, 0);

	return DIM_NO_ERROR;
}

void dim_service_mlb_int_irq(void)
{
	writel(0, &g.dim2->MS0);
	writel(0, &g.dim2->MS1);
}

/*
 * Retrieves maximal possible correct buffer size for isochronous data type
 * conform to given packet length and not bigger than given buffer size.
 *
 * Returns non-zero correct buffer size or zero by error.
 */
u16 dim_norm_isoc_buffer_size(u16 buf_size, u16 packet_length)
{
	if (!check_packet_length(packet_length))
		return 0;

	return norm_isoc_buffer_size(buf_size, packet_length);
}

/*
 * Retrieves maximal possible correct buffer size for synchronous data type
 * conform to given bytes per frame and not bigger than given buffer size.
 *
 * Returns non-zero correct buffer size or zero by error.
 */
u16 dim_norm_sync_buffer_size(u16 buf_size, u16 bytes_per_frame)
{
	if (!check_bytes_per_frame(bytes_per_frame))
		return 0;

	return norm_sync_buffer_size(buf_size, bytes_per_frame);
}

u8 dim_init_control(struct dim_channel *ch, u8 is_tx, u16 ch_address,
		    u16 max_buffer_size)
{
	return init_ctrl_async(ch, CAT_CT_VAL_CONTROL, is_tx, ch_address,
			       max_buffer_size);
}

u8 dim_init_async(struct dim_channel *ch, u8 is_tx, u16 ch_address,
		  u16 max_buffer_size)
{
	u8 ret = init_ctrl_async(ch, CAT_CT_VAL_ASYNC, is_tx, ch_address,
				 max_buffer_size);

	if (is_tx && !g.atx_dbr.ch_addr) {
		g.atx_dbr.ch_addr = ch->addr;
		dbrcnt_init(ch->addr, ch->dbr_size);
		writel(bit_mask(20), &g.dim2->MIEN);
	}

	return ret;
}

u8 dim_init_isoc(struct dim_channel *ch, u8 is_tx, u16 ch_address,
		 u16 packet_length)
{
	if (!g.dim_is_initialized || !ch)
		return DIM_ERR_DRIVER_NOT_INITIALIZED;

	if (!check_channel_address(ch_address))
		return DIM_INIT_ERR_CHANNEL_ADDRESS;

	if (!check_packet_length(packet_length))
		return DIM_ERR_BAD_CONFIG;

	if (!ch->dbr_size)
		ch->dbr_size = packet_length * ISOC_DBR_FACTOR;
	ch->dbr_addr = alloc_dbr(ch->dbr_size);
	if (ch->dbr_addr >= DBR_SIZE)
		return DIM_INIT_ERR_OUT_OF_MEMORY;

	isoc_init(ch, ch_address / 2, packet_length);

	dim2_configure_channel(ch->addr, CAT_CT_VAL_ISOC, is_tx, ch->dbr_addr,
			       ch->dbr_size, packet_length);

	return DIM_NO_ERROR;
}

u8 dim_init_sync(struct dim_channel *ch, u8 is_tx, u16 ch_address,
		 u16 bytes_per_frame)
{
	u16 bd_factor = g.fcnt + 2;

	if (!g.dim_is_initialized || !ch)
		return DIM_ERR_DRIVER_NOT_INITIALIZED;

	if (!check_channel_address(ch_address))
		return DIM_INIT_ERR_CHANNEL_ADDRESS;

	if (!check_bytes_per_frame(bytes_per_frame))
		return DIM_ERR_BAD_CONFIG;

	if (!ch->dbr_size)
		ch->dbr_size = bytes_per_frame << bd_factor;
	ch->dbr_addr = alloc_dbr(ch->dbr_size);
	if (ch->dbr_addr >= DBR_SIZE)
		return DIM_INIT_ERR_OUT_OF_MEMORY;

	sync_init(ch, ch_address / 2, bytes_per_frame);

	dim2_clear_dbr(ch->dbr_addr, ch->dbr_size);
	dim2_configure_channel(ch->addr, CAT_CT_VAL_SYNC, is_tx,
			       ch->dbr_addr, ch->dbr_size, 0);

	return DIM_NO_ERROR;
}

u8 dim_destroy_channel(struct dim_channel *ch)
{
	if (!g.dim_is_initialized || !ch)
		return DIM_ERR_DRIVER_NOT_INITIALIZED;

	if (ch->addr == g.atx_dbr.ch_addr) {
		writel(0, &g.dim2->MIEN);
		g.atx_dbr.ch_addr = 0;
	}

	dim2_clear_channel(ch->addr);
	if (ch->dbr_addr < DBR_SIZE)
		free_dbr(ch->dbr_addr, ch->dbr_size);
	ch->dbr_addr = DBR_SIZE;

	return DIM_NO_ERROR;
}

void dim_service_ahb_int_irq(struct dim_channel *const *channels)
{
	bool state_changed;

	if (!g.dim_is_initialized) {
		dim_on_error(DIM_ERR_DRIVER_NOT_INITIALIZED,
			     "DIM is not initialized");
		return;
	}

	if (!channels) {
		dim_on_error(DIM_ERR_DRIVER_NOT_INITIALIZED, "Bad channels");
		return;
	}

	/*
	 * Use while-loop and a flag to make sure the age is changed back at
	 * least once, otherwise the interrupt may never come if CPU generates
	 * interrupt on changing age.
	 * This cycle runs not more than number of channels, because
	 * channel_service_interrupt() routine doesn't start the channel again.
	 */
	do {
		struct dim_channel *const *ch = channels;

		state_changed = false;

		while (*ch) {
			state_changed |= channel_service_interrupt(*ch);
			++ch;
		}
	} while (state_changed);
}

u8 dim_service_channel(struct dim_channel *ch)
{
	if (!g.dim_is_initialized || !ch)
		return DIM_ERR_DRIVER_NOT_INITIALIZED;

	return channel_service(ch);
}

struct dim_ch_state_t *dim_get_channel_state(struct dim_channel *ch,
					     struct dim_ch_state_t *state_ptr)
{
	if (!ch || !state_ptr)
		return NULL;

	state_ptr->ready = ch->state.level < 2;
	state_ptr->done_buffers = ch->done_sw_buffers_number;

	return state_ptr;
}

bool dim_enqueue_buffer(struct dim_channel *ch, u32 buffer_addr,
			u16 buffer_size)
{
	if (!ch)
		return dim_on_error(DIM_ERR_DRIVER_NOT_INITIALIZED,
				    "Bad channel");

	return channel_start(ch, buffer_addr, buffer_size);
}

bool dim_detach_buffers(struct dim_channel *ch, u16 buffers_number)
{
	if (!ch)
		return dim_on_error(DIM_ERR_DRIVER_NOT_INITIALIZED,
				    "Bad channel");

	return channel_detach_buffers(ch, buffers_number);
}
