// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2018 Etnaviv Project
 */

#include <linux/kernel.h>

#include "etnaviv_gem.h"
#include "etnaviv_gpu.h"

#include "cmdstream.xml.h"

#define EXTRACT(val, field) (((val) & field##__MASK) >> field##__SHIFT)

struct etna_validation_state {
	struct etnaviv_gpu *gpu;
	const struct drm_etnaviv_gem_submit_reloc *relocs;
	unsigned int num_relocs;
	u32 *start;
};

static const struct {
	u16 offset;
	u16 size;
} etnaviv_sensitive_states[] __initconst = {
#define ST(start, num) { (start) >> 2, (num) }
	/* 2D */
	ST(0x1200, 1),
	ST(0x1228, 1),
	ST(0x1238, 1),
	ST(0x1284, 1),
	ST(0x128c, 1),
	ST(0x1304, 1),
	ST(0x1310, 1),
	ST(0x1318, 1),
	ST(0x12800, 4),
	ST(0x128a0, 4),
	ST(0x128c0, 4),
	ST(0x12970, 4),
	ST(0x12a00, 8),
	ST(0x12b40, 8),
	ST(0x12b80, 8),
	ST(0x12ce0, 8),
	/* 3D */
	ST(0x0644, 1),
	ST(0x064c, 1),
	ST(0x0680, 8),
	ST(0x086c, 1),
	ST(0x1028, 1),
	ST(0x1410, 1),
	ST(0x1430, 1),
	ST(0x1458, 1),
	ST(0x1460, 8),
	ST(0x1480, 8),
	ST(0x1500, 8),
	ST(0x1520, 8),
	ST(0x1608, 1),
	ST(0x1610, 1),
	ST(0x1658, 1),
	ST(0x165c, 1),
	ST(0x1664, 1),
	ST(0x1668, 1),
	ST(0x16a4, 1),
	ST(0x16c0, 8),
	ST(0x16e0, 8),
	ST(0x1740, 8),
	ST(0x17c0, 8),
	ST(0x17e0, 8),
	ST(0x2400, 14 * 16),
	ST(0x3824, 1),
	ST(0x10800, 32 * 16),
	ST(0x14600, 16),
	ST(0x14800, 8 * 8),
#undef ST
};

#define ETNAVIV_STATES_SIZE (VIV_FE_LOAD_STATE_HEADER_OFFSET__MASK + 1u)
static DECLARE_BITMAP(etnaviv_states, ETNAVIV_STATES_SIZE);

void __init etnaviv_validate_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(etnaviv_sensitive_states); i++)
		bitmap_set(etnaviv_states, etnaviv_sensitive_states[i].offset,
			   etnaviv_sensitive_states[i].size);
}

static void etnaviv_warn_if_non_sensitive(struct etna_validation_state *state,
	unsigned int buf_offset, unsigned int state_addr)
{
	if (state->num_relocs && state->relocs->submit_offset < buf_offset) {
		dev_warn_once(state->gpu->dev,
			      "%s: relocation for non-sensitive state 0x%x at offset %u\n",
			      __func__, state_addr,
			      state->relocs->submit_offset);
		while (state->num_relocs &&
		       state->relocs->submit_offset < buf_offset) {
			state->relocs++;
			state->num_relocs--;
		}
	}
}

static bool etnaviv_validate_load_state(struct etna_validation_state *state,
	u32 *ptr, unsigned int state_offset, unsigned int num)
{
	unsigned int size = min(ETNAVIV_STATES_SIZE, state_offset + num);
	unsigned int st_offset = state_offset, buf_offset;

	for_each_set_bit_from(st_offset, etnaviv_states, size) {
		buf_offset = (ptr - state->start +
			      st_offset - state_offset) * 4;

		etnaviv_warn_if_non_sensitive(state, buf_offset, st_offset * 4);
		if (state->num_relocs &&
		    state->relocs->submit_offset == buf_offset) {
			state->relocs++;
			state->num_relocs--;
			continue;
		}

		dev_warn_ratelimited(state->gpu->dev,
				     "%s: load state touches restricted state 0x%x at offset %u\n",
				     __func__, st_offset * 4, buf_offset);
		return false;
	}

	if (state->num_relocs) {
		buf_offset = (ptr - state->start + num) * 4;
		etnaviv_warn_if_non_sensitive(state, buf_offset, st_offset * 4 +
					      state->relocs->submit_offset -
					      buf_offset);
	}

	return true;
}

static uint8_t cmd_length[32] = {
	[FE_OPCODE_DRAW_PRIMITIVES] = 4,
	[FE_OPCODE_DRAW_INDEXED_PRIMITIVES] = 6,
	[FE_OPCODE_DRAW_INSTANCED] = 4,
	[FE_OPCODE_NOP] = 2,
	[FE_OPCODE_STALL] = 2,
};

bool etnaviv_cmd_validate_one(struct etnaviv_gpu *gpu, u32 *stream,
			      unsigned int size,
			      struct drm_etnaviv_gem_submit_reloc *relocs,
			      unsigned int reloc_size)
{
	struct etna_validation_state state;
	u32 *buf = stream;
	u32 *end = buf + size;

	state.gpu = gpu;
	state.relocs = relocs;
	state.num_relocs = reloc_size;
	state.start = stream;

	while (buf < end) {
		u32 cmd = *buf;
		unsigned int len, n, off;
		unsigned int op = cmd >> 27;

		switch (op) {
		case FE_OPCODE_LOAD_STATE:
			n = EXTRACT(cmd, VIV_FE_LOAD_STATE_HEADER_COUNT);
			len = ALIGN(1 + n, 2);
			if (buf + len > end)
				break;

			off = EXTRACT(cmd, VIV_FE_LOAD_STATE_HEADER_OFFSET);
			if (!etnaviv_validate_load_state(&state, buf + 1,
							 off, n))
				return false;
			break;

		case FE_OPCODE_DRAW_2D:
			n = EXTRACT(cmd, VIV_FE_DRAW_2D_HEADER_COUNT);
			if (n == 0)
				n = 256;
			len = 2 + n * 2;
			break;

		default:
			len = cmd_length[op];
			if (len == 0) {
				dev_err(gpu->dev, "%s: op %u not permitted at offset %tu\n",
					__func__, op, buf - state.start);
				return false;
			}
			break;
		}

		buf += len;
	}

	if (buf > end) {
		dev_err(gpu->dev, "%s: commands overflow end of buffer: %tu > %u\n",
			__func__, buf - state.start, size);
		return false;
	}

	return true;
}
