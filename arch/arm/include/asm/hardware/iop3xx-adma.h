/*
 * Copyright © 2006, Intel Corporation.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifndef _ADMA_H
#define _ADMA_H
#include <linux/types.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <asm/hardware/iop_adma.h>

/* Memory copy units */
#define DMA_CCR(chan)		(chan->mmr_base + 0x0)
#define DMA_CSR(chan)		(chan->mmr_base + 0x4)
#define DMA_DAR(chan)		(chan->mmr_base + 0xc)
#define DMA_NDAR(chan)		(chan->mmr_base + 0x10)
#define DMA_PADR(chan)		(chan->mmr_base + 0x14)
#define DMA_PUADR(chan)	(chan->mmr_base + 0x18)
#define DMA_LADR(chan)		(chan->mmr_base + 0x1c)
#define DMA_BCR(chan)		(chan->mmr_base + 0x20)
#define DMA_DCR(chan)		(chan->mmr_base + 0x24)

/* Application accelerator unit  */
#define AAU_ACR(chan)		(chan->mmr_base + 0x0)
#define AAU_ASR(chan)		(chan->mmr_base + 0x4)
#define AAU_ADAR(chan)		(chan->mmr_base + 0x8)
#define AAU_ANDAR(chan)	(chan->mmr_base + 0xc)
#define AAU_SAR(src, chan)	(chan->mmr_base + (0x10 + ((src) << 2)))
#define AAU_DAR(chan)		(chan->mmr_base + 0x20)
#define AAU_ABCR(chan)		(chan->mmr_base + 0x24)
#define AAU_ADCR(chan)		(chan->mmr_base + 0x28)
#define AAU_SAR_EDCR(src_edc)	(chan->mmr_base + (0x02c + ((src_edc-4) << 2)))
#define AAU_EDCR0_IDX	8
#define AAU_EDCR1_IDX	17
#define AAU_EDCR2_IDX	26

#define DMA0_ID 0
#define DMA1_ID 1
#define AAU_ID 2

struct iop3xx_aau_desc_ctrl {
	unsigned int int_en:1;
	unsigned int blk1_cmd_ctrl:3;
	unsigned int blk2_cmd_ctrl:3;
	unsigned int blk3_cmd_ctrl:3;
	unsigned int blk4_cmd_ctrl:3;
	unsigned int blk5_cmd_ctrl:3;
	unsigned int blk6_cmd_ctrl:3;
	unsigned int blk7_cmd_ctrl:3;
	unsigned int blk8_cmd_ctrl:3;
	unsigned int blk_ctrl:2;
	unsigned int dual_xor_en:1;
	unsigned int tx_complete:1;
	unsigned int zero_result_err:1;
	unsigned int zero_result_en:1;
	unsigned int dest_write_en:1;
};

struct iop3xx_aau_e_desc_ctrl {
	unsigned int reserved:1;
	unsigned int blk1_cmd_ctrl:3;
	unsigned int blk2_cmd_ctrl:3;
	unsigned int blk3_cmd_ctrl:3;
	unsigned int blk4_cmd_ctrl:3;
	unsigned int blk5_cmd_ctrl:3;
	unsigned int blk6_cmd_ctrl:3;
	unsigned int blk7_cmd_ctrl:3;
	unsigned int blk8_cmd_ctrl:3;
	unsigned int reserved2:7;
};

struct iop3xx_dma_desc_ctrl {
	unsigned int pci_transaction:4;
	unsigned int int_en:1;
	unsigned int dac_cycle_en:1;
	unsigned int mem_to_mem_en:1;
	unsigned int crc_data_tx_en:1;
	unsigned int crc_gen_en:1;
	unsigned int crc_seed_dis:1;
	unsigned int reserved:21;
	unsigned int crc_tx_complete:1;
};

struct iop3xx_desc_dma {
	u32 next_desc;
	union {
		u32 pci_src_addr;
		u32 pci_dest_addr;
		u32 src_addr;
	};
	union {
		u32 upper_pci_src_addr;
		u32 upper_pci_dest_addr;
	};
	union {
		u32 local_pci_src_addr;
		u32 local_pci_dest_addr;
		u32 dest_addr;
	};
	u32 byte_count;
	union {
		u32 desc_ctrl;
		struct iop3xx_dma_desc_ctrl desc_ctrl_field;
	};
	u32 crc_addr;
};

struct iop3xx_desc_aau {
	u32 next_desc;
	u32 src[4];
	u32 dest_addr;
	u32 byte_count;
	union {
		u32 desc_ctrl;
		struct iop3xx_aau_desc_ctrl desc_ctrl_field;
	};
	union {
		u32 src_addr;
		u32 e_desc_ctrl;
		struct iop3xx_aau_e_desc_ctrl e_desc_ctrl_field;
	} src_edc[31];
};

struct iop3xx_aau_gfmr {
	unsigned int gfmr1:8;
	unsigned int gfmr2:8;
	unsigned int gfmr3:8;
	unsigned int gfmr4:8;
};

struct iop3xx_desc_pq_xor {
	u32 next_desc;
	u32 src[3];
	union {
		u32 data_mult1;
		struct iop3xx_aau_gfmr data_mult1_field;
	};
	u32 dest_addr;
	u32 byte_count;
	union {
		u32 desc_ctrl;
		struct iop3xx_aau_desc_ctrl desc_ctrl_field;
	};
	union {
		u32 src_addr;
		u32 e_desc_ctrl;
		struct iop3xx_aau_e_desc_ctrl e_desc_ctrl_field;
		u32 data_multiplier;
		struct iop3xx_aau_gfmr data_mult_field;
		u32 reserved;
	} src_edc_gfmr[19];
};

struct iop3xx_desc_dual_xor {
	u32 next_desc;
	u32 src0_addr;
	u32 src1_addr;
	u32 h_src_addr;
	u32 d_src_addr;
	u32 h_dest_addr;
	u32 byte_count;
	union {
		u32 desc_ctrl;
		struct iop3xx_aau_desc_ctrl desc_ctrl_field;
	};
	u32 d_dest_addr;
};

union iop3xx_desc {
	struct iop3xx_desc_aau *aau;
	struct iop3xx_desc_dma *dma;
	struct iop3xx_desc_pq_xor *pq_xor;
	struct iop3xx_desc_dual_xor *dual_xor;
	void *ptr;
};

static inline int iop_adma_get_max_xor(void)
{
	return 32;
}

static inline u32 iop_chan_get_current_descriptor(struct iop_adma_chan *chan)
{
	int id = chan->device->id;

	switch (id) {
	case DMA0_ID:
	case DMA1_ID:
		return __raw_readl(DMA_DAR(chan));
	case AAU_ID:
		return __raw_readl(AAU_ADAR(chan));
	default:
		BUG();
	}
	return 0;
}

static inline void iop_chan_set_next_descriptor(struct iop_adma_chan *chan,
						u32 next_desc_addr)
{
	int id = chan->device->id;

	switch (id) {
	case DMA0_ID:
	case DMA1_ID:
		__raw_writel(next_desc_addr, DMA_NDAR(chan));
		break;
	case AAU_ID:
		__raw_writel(next_desc_addr, AAU_ANDAR(chan));
		break;
	}

}

#define IOP_ADMA_STATUS_BUSY (1 << 10)
#define IOP_ADMA_ZERO_SUM_MAX_BYTE_COUNT (1024)
#define IOP_ADMA_XOR_MAX_BYTE_COUNT (16 * 1024 * 1024)
#define IOP_ADMA_MAX_BYTE_COUNT (16 * 1024 * 1024)

static inline int iop_chan_is_busy(struct iop_adma_chan *chan)
{
	u32 status = __raw_readl(DMA_CSR(chan));
	return (status & IOP_ADMA_STATUS_BUSY) ? 1 : 0;
}

static inline int iop_desc_is_aligned(struct iop_adma_desc_slot *desc,
					int num_slots)
{
	/* num_slots will only ever be 1, 2, 4, or 8 */
	return (desc->idx & (num_slots - 1)) ? 0 : 1;
}

/* to do: support large (i.e. > hw max) buffer sizes */
static inline int iop_chan_memcpy_slot_count(size_t len, int *slots_per_op)
{
	*slots_per_op = 1;
	return 1;
}

/* to do: support large (i.e. > hw max) buffer sizes */
static inline int iop_chan_memset_slot_count(size_t len, int *slots_per_op)
{
	*slots_per_op = 1;
	return 1;
}

static inline int iop3xx_aau_xor_slot_count(size_t len, int src_cnt,
					int *slots_per_op)
{
	static const char slot_count_table[] = {
						1, 1, 1, 1, /* 01 - 04 */
						2, 2, 2, 2, /* 05 - 08 */
						4, 4, 4, 4, /* 09 - 12 */
						4, 4, 4, 4, /* 13 - 16 */
						8, 8, 8, 8, /* 17 - 20 */
						8, 8, 8, 8, /* 21 - 24 */
						8, 8, 8, 8, /* 25 - 28 */
						8, 8, 8, 8, /* 29 - 32 */
					      };
	*slots_per_op = slot_count_table[src_cnt - 1];
	return *slots_per_op;
}

static inline int
iop_chan_interrupt_slot_count(int *slots_per_op, struct iop_adma_chan *chan)
{
	switch (chan->device->id) {
	case DMA0_ID:
	case DMA1_ID:
		return iop_chan_memcpy_slot_count(0, slots_per_op);
	case AAU_ID:
		return iop3xx_aau_xor_slot_count(0, 2, slots_per_op);
	default:
		BUG();
	}
	return 0;
}

static inline int iop_chan_xor_slot_count(size_t len, int src_cnt,
						int *slots_per_op)
{
	int slot_cnt = iop3xx_aau_xor_slot_count(len, src_cnt, slots_per_op);

	if (len <= IOP_ADMA_XOR_MAX_BYTE_COUNT)
		return slot_cnt;

	len -= IOP_ADMA_XOR_MAX_BYTE_COUNT;
	while (len > IOP_ADMA_XOR_MAX_BYTE_COUNT) {
		len -= IOP_ADMA_XOR_MAX_BYTE_COUNT;
		slot_cnt += *slots_per_op;
	}

	if (len)
		slot_cnt += *slots_per_op;

	return slot_cnt;
}

/* zero sum on iop3xx is limited to 1k at a time so it requires multiple
 * descriptors
 */
static inline int iop_chan_zero_sum_slot_count(size_t len, int src_cnt,
						int *slots_per_op)
{
	int slot_cnt = iop3xx_aau_xor_slot_count(len, src_cnt, slots_per_op);

	if (len <= IOP_ADMA_ZERO_SUM_MAX_BYTE_COUNT)
		return slot_cnt;

	len -= IOP_ADMA_ZERO_SUM_MAX_BYTE_COUNT;
	while (len > IOP_ADMA_ZERO_SUM_MAX_BYTE_COUNT) {
		len -= IOP_ADMA_ZERO_SUM_MAX_BYTE_COUNT;
		slot_cnt += *slots_per_op;
	}

	if (len)
		slot_cnt += *slots_per_op;

	return slot_cnt;
}

static inline u32 iop_desc_get_dest_addr(struct iop_adma_desc_slot *desc,
					struct iop_adma_chan *chan)
{
	union iop3xx_desc hw_desc = { .ptr = desc->hw_desc, };

	switch (chan->device->id) {
	case DMA0_ID:
	case DMA1_ID:
		return hw_desc.dma->dest_addr;
	case AAU_ID:
		return hw_desc.aau->dest_addr;
	default:
		BUG();
	}
	return 0;
}

static inline u32 iop_desc_get_byte_count(struct iop_adma_desc_slot *desc,
					struct iop_adma_chan *chan)
{
	union iop3xx_desc hw_desc = { .ptr = desc->hw_desc, };

	switch (chan->device->id) {
	case DMA0_ID:
	case DMA1_ID:
		return hw_desc.dma->byte_count;
	case AAU_ID:
		return hw_desc.aau->byte_count;
	default:
		BUG();
	}
	return 0;
}

/* translate the src_idx to a descriptor word index */
static inline int __desc_idx(int src_idx)
{
	static const int desc_idx_table[] = { 0, 0, 0, 0,
					      0, 1, 2, 3,
					      5, 6, 7, 8,
					      9, 10, 11, 12,
					      14, 15, 16, 17,
					      18, 19, 20, 21,
					      23, 24, 25, 26,
					      27, 28, 29, 30,
					    };

	return desc_idx_table[src_idx];
}

static inline u32 iop_desc_get_src_addr(struct iop_adma_desc_slot *desc,
					struct iop_adma_chan *chan,
					int src_idx)
{
	union iop3xx_desc hw_desc = { .ptr = desc->hw_desc, };

	switch (chan->device->id) {
	case DMA0_ID:
	case DMA1_ID:
		return hw_desc.dma->src_addr;
	case AAU_ID:
		break;
	default:
		BUG();
	}

	if (src_idx < 4)
		return hw_desc.aau->src[src_idx];
	else
		return hw_desc.aau->src_edc[__desc_idx(src_idx)].src_addr;
}

static inline void iop3xx_aau_desc_set_src_addr(struct iop3xx_desc_aau *hw_desc,
					int src_idx, dma_addr_t addr)
{
	if (src_idx < 4)
		hw_desc->src[src_idx] = addr;
	else
		hw_desc->src_edc[__desc_idx(src_idx)].src_addr = addr;
}

static inline void
iop_desc_init_memcpy(struct iop_adma_desc_slot *desc, unsigned long flags)
{
	struct iop3xx_desc_dma *hw_desc = desc->hw_desc;
	union {
		u32 value;
		struct iop3xx_dma_desc_ctrl field;
	} u_desc_ctrl;

	u_desc_ctrl.value = 0;
	u_desc_ctrl.field.mem_to_mem_en = 1;
	u_desc_ctrl.field.pci_transaction = 0xe; /* memory read block */
	u_desc_ctrl.field.int_en = flags & DMA_PREP_INTERRUPT;
	hw_desc->desc_ctrl = u_desc_ctrl.value;
	hw_desc->upper_pci_src_addr = 0;
	hw_desc->crc_addr = 0;
}

static inline void
iop_desc_init_memset(struct iop_adma_desc_slot *desc, unsigned long flags)
{
	struct iop3xx_desc_aau *hw_desc = desc->hw_desc;
	union {
		u32 value;
		struct iop3xx_aau_desc_ctrl field;
	} u_desc_ctrl;

	u_desc_ctrl.value = 0;
	u_desc_ctrl.field.blk1_cmd_ctrl = 0x2; /* memory block fill */
	u_desc_ctrl.field.dest_write_en = 1;
	u_desc_ctrl.field.int_en = flags & DMA_PREP_INTERRUPT;
	hw_desc->desc_ctrl = u_desc_ctrl.value;
}

static inline u32
iop3xx_desc_init_xor(struct iop3xx_desc_aau *hw_desc, int src_cnt,
		     unsigned long flags)
{
	int i, shift;
	u32 edcr;
	union {
		u32 value;
		struct iop3xx_aau_desc_ctrl field;
	} u_desc_ctrl;

	u_desc_ctrl.value = 0;
	switch (src_cnt) {
	case 25 ... 32:
		u_desc_ctrl.field.blk_ctrl = 0x3; /* use EDCR[2:0] */
		edcr = 0;
		shift = 1;
		for (i = 24; i < src_cnt; i++) {
			edcr |= (1 << shift);
			shift += 3;
		}
		hw_desc->src_edc[AAU_EDCR2_IDX].e_desc_ctrl = edcr;
		src_cnt = 24;
		/* fall through */
	case 17 ... 24:
		if (!u_desc_ctrl.field.blk_ctrl) {
			hw_desc->src_edc[AAU_EDCR2_IDX].e_desc_ctrl = 0;
			u_desc_ctrl.field.blk_ctrl = 0x3; /* use EDCR[2:0] */
		}
		edcr = 0;
		shift = 1;
		for (i = 16; i < src_cnt; i++) {
			edcr |= (1 << shift);
			shift += 3;
		}
		hw_desc->src_edc[AAU_EDCR1_IDX].e_desc_ctrl = edcr;
		src_cnt = 16;
		/* fall through */
	case 9 ... 16:
		if (!u_desc_ctrl.field.blk_ctrl)
			u_desc_ctrl.field.blk_ctrl = 0x2; /* use EDCR0 */
		edcr = 0;
		shift = 1;
		for (i = 8; i < src_cnt; i++) {
			edcr |= (1 << shift);
			shift += 3;
		}
		hw_desc->src_edc[AAU_EDCR0_IDX].e_desc_ctrl = edcr;
		src_cnt = 8;
		/* fall through */
	case 2 ... 8:
		shift = 1;
		for (i = 0; i < src_cnt; i++) {
			u_desc_ctrl.value |= (1 << shift);
			shift += 3;
		}

		if (!u_desc_ctrl.field.blk_ctrl && src_cnt > 4)
			u_desc_ctrl.field.blk_ctrl = 0x1; /* use mini-desc */
	}

	u_desc_ctrl.field.dest_write_en = 1;
	u_desc_ctrl.field.blk1_cmd_ctrl = 0x7; /* direct fill */
	u_desc_ctrl.field.int_en = flags & DMA_PREP_INTERRUPT;
	hw_desc->desc_ctrl = u_desc_ctrl.value;

	return u_desc_ctrl.value;
}

static inline void
iop_desc_init_xor(struct iop_adma_desc_slot *desc, int src_cnt,
		  unsigned long flags)
{
	iop3xx_desc_init_xor(desc->hw_desc, src_cnt, flags);
}

/* return the number of operations */
static inline int
iop_desc_init_zero_sum(struct iop_adma_desc_slot *desc, int src_cnt,
		       unsigned long flags)
{
	int slot_cnt = desc->slot_cnt, slots_per_op = desc->slots_per_op;
	struct iop3xx_desc_aau *hw_desc, *prev_hw_desc, *iter;
	union {
		u32 value;
		struct iop3xx_aau_desc_ctrl field;
	} u_desc_ctrl;
	int i, j;

	hw_desc = desc->hw_desc;

	for (i = 0, j = 0; (slot_cnt -= slots_per_op) >= 0;
		i += slots_per_op, j++) {
		iter = iop_hw_desc_slot_idx(hw_desc, i);
		u_desc_ctrl.value = iop3xx_desc_init_xor(iter, src_cnt, flags);
		u_desc_ctrl.field.dest_write_en = 0;
		u_desc_ctrl.field.zero_result_en = 1;
		u_desc_ctrl.field.int_en = flags & DMA_PREP_INTERRUPT;
		iter->desc_ctrl = u_desc_ctrl.value;

		/* for the subsequent descriptors preserve the store queue
		 * and chain them together
		 */
		if (i) {
			prev_hw_desc =
				iop_hw_desc_slot_idx(hw_desc, i - slots_per_op);
			prev_hw_desc->next_desc =
				(u32) (desc->async_tx.phys + (i << 5));
		}
	}

	return j;
}

static inline void
iop_desc_init_null_xor(struct iop_adma_desc_slot *desc, int src_cnt,
		       unsigned long flags)
{
	struct iop3xx_desc_aau *hw_desc = desc->hw_desc;
	union {
		u32 value;
		struct iop3xx_aau_desc_ctrl field;
	} u_desc_ctrl;

	u_desc_ctrl.value = 0;
	switch (src_cnt) {
	case 25 ... 32:
		u_desc_ctrl.field.blk_ctrl = 0x3; /* use EDCR[2:0] */
		hw_desc->src_edc[AAU_EDCR2_IDX].e_desc_ctrl = 0;
		/* fall through */
	case 17 ... 24:
		if (!u_desc_ctrl.field.blk_ctrl) {
			hw_desc->src_edc[AAU_EDCR2_IDX].e_desc_ctrl = 0;
			u_desc_ctrl.field.blk_ctrl = 0x3; /* use EDCR[2:0] */
		}
		hw_desc->src_edc[AAU_EDCR1_IDX].e_desc_ctrl = 0;
		/* fall through */
	case 9 ... 16:
		if (!u_desc_ctrl.field.blk_ctrl)
			u_desc_ctrl.field.blk_ctrl = 0x2; /* use EDCR0 */
		hw_desc->src_edc[AAU_EDCR0_IDX].e_desc_ctrl = 0;
		/* fall through */
	case 1 ... 8:
		if (!u_desc_ctrl.field.blk_ctrl && src_cnt > 4)
			u_desc_ctrl.field.blk_ctrl = 0x1; /* use mini-desc */
	}

	u_desc_ctrl.field.dest_write_en = 0;
	u_desc_ctrl.field.int_en = flags & DMA_PREP_INTERRUPT;
	hw_desc->desc_ctrl = u_desc_ctrl.value;
}

static inline void iop_desc_set_byte_count(struct iop_adma_desc_slot *desc,
					struct iop_adma_chan *chan,
					u32 byte_count)
{
	union iop3xx_desc hw_desc = { .ptr = desc->hw_desc, };

	switch (chan->device->id) {
	case DMA0_ID:
	case DMA1_ID:
		hw_desc.dma->byte_count = byte_count;
		break;
	case AAU_ID:
		hw_desc.aau->byte_count = byte_count;
		break;
	default:
		BUG();
	}
}

static inline void
iop_desc_init_interrupt(struct iop_adma_desc_slot *desc,
			struct iop_adma_chan *chan)
{
	union iop3xx_desc hw_desc = { .ptr = desc->hw_desc, };

	switch (chan->device->id) {
	case DMA0_ID:
	case DMA1_ID:
		iop_desc_init_memcpy(desc, 1);
		hw_desc.dma->byte_count = 0;
		hw_desc.dma->dest_addr = 0;
		hw_desc.dma->src_addr = 0;
		break;
	case AAU_ID:
		iop_desc_init_null_xor(desc, 2, 1);
		hw_desc.aau->byte_count = 0;
		hw_desc.aau->dest_addr = 0;
		hw_desc.aau->src[0] = 0;
		hw_desc.aau->src[1] = 0;
		break;
	default:
		BUG();
	}
}

static inline void
iop_desc_set_zero_sum_byte_count(struct iop_adma_desc_slot *desc, u32 len)
{
	int slots_per_op = desc->slots_per_op;
	struct iop3xx_desc_aau *hw_desc = desc->hw_desc, *iter;
	int i = 0;

	if (len <= IOP_ADMA_ZERO_SUM_MAX_BYTE_COUNT) {
		hw_desc->byte_count = len;
	} else {
		do {
			iter = iop_hw_desc_slot_idx(hw_desc, i);
			iter->byte_count = IOP_ADMA_ZERO_SUM_MAX_BYTE_COUNT;
			len -= IOP_ADMA_ZERO_SUM_MAX_BYTE_COUNT;
			i += slots_per_op;
		} while (len > IOP_ADMA_ZERO_SUM_MAX_BYTE_COUNT);

		if (len) {
			iter = iop_hw_desc_slot_idx(hw_desc, i);
			iter->byte_count = len;
		}
	}
}

static inline void iop_desc_set_dest_addr(struct iop_adma_desc_slot *desc,
					struct iop_adma_chan *chan,
					dma_addr_t addr)
{
	union iop3xx_desc hw_desc = { .ptr = desc->hw_desc, };

	switch (chan->device->id) {
	case DMA0_ID:
	case DMA1_ID:
		hw_desc.dma->dest_addr = addr;
		break;
	case AAU_ID:
		hw_desc.aau->dest_addr = addr;
		break;
	default:
		BUG();
	}
}

static inline void iop_desc_set_memcpy_src_addr(struct iop_adma_desc_slot *desc,
					dma_addr_t addr)
{
	struct iop3xx_desc_dma *hw_desc = desc->hw_desc;
	hw_desc->src_addr = addr;
}

static inline void
iop_desc_set_zero_sum_src_addr(struct iop_adma_desc_slot *desc, int src_idx,
				dma_addr_t addr)
{

	struct iop3xx_desc_aau *hw_desc = desc->hw_desc, *iter;
	int slot_cnt = desc->slot_cnt, slots_per_op = desc->slots_per_op;
	int i;

	for (i = 0; (slot_cnt -= slots_per_op) >= 0;
		i += slots_per_op, addr += IOP_ADMA_ZERO_SUM_MAX_BYTE_COUNT) {
		iter = iop_hw_desc_slot_idx(hw_desc, i);
		iop3xx_aau_desc_set_src_addr(iter, src_idx, addr);
	}
}

static inline void iop_desc_set_xor_src_addr(struct iop_adma_desc_slot *desc,
					int src_idx, dma_addr_t addr)
{

	struct iop3xx_desc_aau *hw_desc = desc->hw_desc, *iter;
	int slot_cnt = desc->slot_cnt, slots_per_op = desc->slots_per_op;
	int i;

	for (i = 0; (slot_cnt -= slots_per_op) >= 0;
		i += slots_per_op, addr += IOP_ADMA_XOR_MAX_BYTE_COUNT) {
		iter = iop_hw_desc_slot_idx(hw_desc, i);
		iop3xx_aau_desc_set_src_addr(iter, src_idx, addr);
	}
}

static inline void iop_desc_set_next_desc(struct iop_adma_desc_slot *desc,
					u32 next_desc_addr)
{
	/* hw_desc->next_desc is the same location for all channels */
	union iop3xx_desc hw_desc = { .ptr = desc->hw_desc, };
	BUG_ON(hw_desc.dma->next_desc);
	hw_desc.dma->next_desc = next_desc_addr;
}

static inline u32 iop_desc_get_next_desc(struct iop_adma_desc_slot *desc)
{
	/* hw_desc->next_desc is the same location for all channels */
	union iop3xx_desc hw_desc = { .ptr = desc->hw_desc, };
	return hw_desc.dma->next_desc;
}

static inline void iop_desc_clear_next_desc(struct iop_adma_desc_slot *desc)
{
	/* hw_desc->next_desc is the same location for all channels */
	union iop3xx_desc hw_desc = { .ptr = desc->hw_desc, };
	hw_desc.dma->next_desc = 0;
}

static inline void iop_desc_set_block_fill_val(struct iop_adma_desc_slot *desc,
						u32 val)
{
	struct iop3xx_desc_aau *hw_desc = desc->hw_desc;
	hw_desc->src[0] = val;
}

static inline int iop_desc_get_zero_result(struct iop_adma_desc_slot *desc)
{
	struct iop3xx_desc_aau *hw_desc = desc->hw_desc;
	struct iop3xx_aau_desc_ctrl desc_ctrl = hw_desc->desc_ctrl_field;

	BUG_ON(!(desc_ctrl.tx_complete && desc_ctrl.zero_result_en));
	return desc_ctrl.zero_result_err;
}

static inline void iop_chan_append(struct iop_adma_chan *chan)
{
	u32 dma_chan_ctrl;

	dma_chan_ctrl = __raw_readl(DMA_CCR(chan));
	dma_chan_ctrl |= 0x2;
	__raw_writel(dma_chan_ctrl, DMA_CCR(chan));
}

static inline u32 iop_chan_get_status(struct iop_adma_chan *chan)
{
	return __raw_readl(DMA_CSR(chan));
}

static inline void iop_chan_disable(struct iop_adma_chan *chan)
{
	u32 dma_chan_ctrl = __raw_readl(DMA_CCR(chan));
	dma_chan_ctrl &= ~1;
	__raw_writel(dma_chan_ctrl, DMA_CCR(chan));
}

static inline void iop_chan_enable(struct iop_adma_chan *chan)
{
	u32 dma_chan_ctrl = __raw_readl(DMA_CCR(chan));

	dma_chan_ctrl |= 1;
	__raw_writel(dma_chan_ctrl, DMA_CCR(chan));
}

static inline void iop_adma_device_clear_eot_status(struct iop_adma_chan *chan)
{
	u32 status = __raw_readl(DMA_CSR(chan));
	status &= (1 << 9);
	__raw_writel(status, DMA_CSR(chan));
}

static inline void iop_adma_device_clear_eoc_status(struct iop_adma_chan *chan)
{
	u32 status = __raw_readl(DMA_CSR(chan));
	status &= (1 << 8);
	__raw_writel(status, DMA_CSR(chan));
}

static inline void iop_adma_device_clear_err_status(struct iop_adma_chan *chan)
{
	u32 status = __raw_readl(DMA_CSR(chan));

	switch (chan->device->id) {
	case DMA0_ID:
	case DMA1_ID:
		status &= (1 << 5) | (1 << 3) | (1 << 2) | (1 << 1);
		break;
	case AAU_ID:
		status &= (1 << 5);
		break;
	default:
		BUG();
	}

	__raw_writel(status, DMA_CSR(chan));
}

static inline int
iop_is_err_int_parity(unsigned long status, struct iop_adma_chan *chan)
{
	return 0;
}

static inline int
iop_is_err_mcu_abort(unsigned long status, struct iop_adma_chan *chan)
{
	return 0;
}

static inline int
iop_is_err_int_tabort(unsigned long status, struct iop_adma_chan *chan)
{
	return 0;
}

static inline int
iop_is_err_int_mabort(unsigned long status, struct iop_adma_chan *chan)
{
	return test_bit(5, &status);
}

static inline int
iop_is_err_pci_tabort(unsigned long status, struct iop_adma_chan *chan)
{
	switch (chan->device->id) {
	case DMA0_ID:
	case DMA1_ID:
		return test_bit(2, &status);
	default:
		return 0;
	}
}

static inline int
iop_is_err_pci_mabort(unsigned long status, struct iop_adma_chan *chan)
{
	switch (chan->device->id) {
	case DMA0_ID:
	case DMA1_ID:
		return test_bit(3, &status);
	default:
		return 0;
	}
}

static inline int
iop_is_err_split_tx(unsigned long status, struct iop_adma_chan *chan)
{
	switch (chan->device->id) {
	case DMA0_ID:
	case DMA1_ID:
		return test_bit(1, &status);
	default:
		return 0;
	}
}
#endif /* _ADMA_H */
