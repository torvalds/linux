/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2006, Intel Corporation.
 */
#ifndef _ADMA_H
#define _ADMA_H
#include <linux/types.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <asm/hardware/iop_adma.h>

#define ADMA_ACCR(chan)	(chan->mmr_base + 0x0)
#define ADMA_ACSR(chan)	(chan->mmr_base + 0x4)
#define ADMA_ADAR(chan)	(chan->mmr_base + 0x8)
#define ADMA_IIPCR(chan)	(chan->mmr_base + 0x18)
#define ADMA_IIPAR(chan)	(chan->mmr_base + 0x1c)
#define ADMA_IIPUAR(chan)	(chan->mmr_base + 0x20)
#define ADMA_ANDAR(chan)	(chan->mmr_base + 0x24)
#define ADMA_ADCR(chan)	(chan->mmr_base + 0x28)
#define ADMA_CARMD(chan)	(chan->mmr_base + 0x2c)
#define ADMA_ABCR(chan)	(chan->mmr_base + 0x30)
#define ADMA_DLADR(chan)	(chan->mmr_base + 0x34)
#define ADMA_DUADR(chan)	(chan->mmr_base + 0x38)
#define ADMA_SLAR(src, chan)	(chan->mmr_base + (0x3c + (src << 3)))
#define ADMA_SUAR(src, chan)	(chan->mmr_base + (0x40 + (src << 3)))

struct iop13xx_adma_src {
	u32 src_addr;
	union {
		u32 upper_src_addr;
		struct {
			unsigned int pq_upper_src_addr:24;
			unsigned int pq_dmlt:8;
		};
	};
};

struct iop13xx_adma_desc_ctrl {
	unsigned int int_en:1;
	unsigned int xfer_dir:2;
	unsigned int src_select:4;
	unsigned int zero_result:1;
	unsigned int block_fill_en:1;
	unsigned int crc_gen_en:1;
	unsigned int crc_xfer_dis:1;
	unsigned int crc_seed_fetch_dis:1;
	unsigned int status_write_back_en:1;
	unsigned int endian_swap_en:1;
	unsigned int reserved0:2;
	unsigned int pq_update_xfer_en:1;
	unsigned int dual_xor_en:1;
	unsigned int pq_xfer_en:1;
	unsigned int p_xfer_dis:1;
	unsigned int reserved1:10;
	unsigned int relax_order_en:1;
	unsigned int no_snoop_en:1;
};

struct iop13xx_adma_byte_count {
	unsigned int byte_count:24;
	unsigned int host_if:3;
	unsigned int reserved:2;
	unsigned int zero_result_err_q:1;
	unsigned int zero_result_err:1;
	unsigned int tx_complete:1;
};

struct iop13xx_adma_desc_hw {
	u32 next_desc;
	union {
		u32 desc_ctrl;
		struct iop13xx_adma_desc_ctrl desc_ctrl_field;
	};
	union {
		u32 crc_addr;
		u32 block_fill_data;
		u32 q_dest_addr;
	};
	union {
		u32 byte_count;
		struct iop13xx_adma_byte_count byte_count_field;
	};
	union {
		u32 dest_addr;
		u32 p_dest_addr;
	};
	union {
		u32 upper_dest_addr;
		u32 pq_upper_dest_addr;
	};
	struct iop13xx_adma_src src[1];
};

struct iop13xx_adma_desc_dual_xor {
	u32 next_desc;
	u32 desc_ctrl;
	u32 reserved;
	u32 byte_count;
	u32 h_dest_addr;
	u32 h_upper_dest_addr;
	u32 src0_addr;
	u32 upper_src0_addr;
	u32 src1_addr;
	u32 upper_src1_addr;
	u32 h_src_addr;
	u32 h_upper_src_addr;
	u32 d_src_addr;
	u32 d_upper_src_addr;
	u32 d_dest_addr;
	u32 d_upper_dest_addr;
};

struct iop13xx_adma_desc_pq_update {
	u32 next_desc;
	u32 desc_ctrl;
	u32 reserved;
	u32 byte_count;
	u32 p_dest_addr;
	u32 p_upper_dest_addr;
	u32 src0_addr;
	u32 upper_src0_addr;
	u32 src1_addr;
	u32 upper_src1_addr;
	u32 p_src_addr;
	u32 p_upper_src_addr;
	u32 q_src_addr;
	struct {
		unsigned int q_upper_src_addr:24;
		unsigned int q_dmlt:8;
	};
	u32 q_dest_addr;
	u32 q_upper_dest_addr;
};

static inline int iop_adma_get_max_xor(void)
{
	return 16;
}

#define iop_adma_get_max_pq iop_adma_get_max_xor

static inline u32 iop_chan_get_current_descriptor(struct iop_adma_chan *chan)
{
	return __raw_readl(ADMA_ADAR(chan));
}

static inline void iop_chan_set_next_descriptor(struct iop_adma_chan *chan,
						u32 next_desc_addr)
{
	__raw_writel(next_desc_addr, ADMA_ANDAR(chan));
}

#define ADMA_STATUS_BUSY (1 << 13)

static inline char iop_chan_is_busy(struct iop_adma_chan *chan)
{
	if (__raw_readl(ADMA_ACSR(chan)) &
		ADMA_STATUS_BUSY)
		return 1;
	else
		return 0;
}

static inline int
iop_chan_get_desc_align(struct iop_adma_chan *chan, int num_slots)
{
	return 1;
}
#define iop_desc_is_aligned(x, y) 1

static inline int
iop_chan_memcpy_slot_count(size_t len, int *slots_per_op)
{
	*slots_per_op = 1;
	return 1;
}

#define iop_chan_interrupt_slot_count(s, c) iop_chan_memcpy_slot_count(0, s)

static inline int
iop_chan_memset_slot_count(size_t len, int *slots_per_op)
{
	*slots_per_op = 1;
	return 1;
}

static inline int
iop_chan_xor_slot_count(size_t len, int src_cnt, int *slots_per_op)
{
	static const char slot_count_table[] = { 1, 2, 2, 2,
						 2, 3, 3, 3,
						 3, 4, 4, 4,
						 4, 5, 5, 5,
						};
	*slots_per_op = slot_count_table[src_cnt - 1];
	return *slots_per_op;
}

#define ADMA_MAX_BYTE_COUNT	(16 * 1024 * 1024)
#define IOP_ADMA_MAX_BYTE_COUNT ADMA_MAX_BYTE_COUNT
#define IOP_ADMA_ZERO_SUM_MAX_BYTE_COUNT ADMA_MAX_BYTE_COUNT
#define IOP_ADMA_XOR_MAX_BYTE_COUNT ADMA_MAX_BYTE_COUNT
#define IOP_ADMA_PQ_MAX_BYTE_COUNT ADMA_MAX_BYTE_COUNT
#define iop_chan_zero_sum_slot_count(l, s, o) iop_chan_xor_slot_count(l, s, o)
#define iop_chan_pq_slot_count iop_chan_xor_slot_count
#define iop_chan_pq_zero_sum_slot_count iop_chan_xor_slot_count

static inline u32 iop_desc_get_byte_count(struct iop_adma_desc_slot *desc,
					struct iop_adma_chan *chan)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	return hw_desc->byte_count_field.byte_count;
}

static inline u32 iop_desc_get_src_addr(struct iop_adma_desc_slot *desc,
					struct iop_adma_chan *chan,
					int src_idx)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	return hw_desc->src[src_idx].src_addr;
}

static inline u32 iop_desc_get_src_count(struct iop_adma_desc_slot *desc,
					struct iop_adma_chan *chan)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	return hw_desc->desc_ctrl_field.src_select + 1;
}

static inline void
iop_desc_init_memcpy(struct iop_adma_desc_slot *desc, unsigned long flags)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	union {
		u32 value;
		struct iop13xx_adma_desc_ctrl field;
	} u_desc_ctrl;

	u_desc_ctrl.value = 0;
	u_desc_ctrl.field.xfer_dir = 3; /* local to internal bus */
	u_desc_ctrl.field.int_en = flags & DMA_PREP_INTERRUPT;
	hw_desc->desc_ctrl = u_desc_ctrl.value;
	hw_desc->crc_addr = 0;
}

static inline void
iop_desc_init_memset(struct iop_adma_desc_slot *desc, unsigned long flags)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	union {
		u32 value;
		struct iop13xx_adma_desc_ctrl field;
	} u_desc_ctrl;

	u_desc_ctrl.value = 0;
	u_desc_ctrl.field.xfer_dir = 3; /* local to internal bus */
	u_desc_ctrl.field.block_fill_en = 1;
	u_desc_ctrl.field.int_en = flags & DMA_PREP_INTERRUPT;
	hw_desc->desc_ctrl = u_desc_ctrl.value;
	hw_desc->crc_addr = 0;
}

/* to do: support buffers larger than ADMA_MAX_BYTE_COUNT */
static inline void
iop_desc_init_xor(struct iop_adma_desc_slot *desc, int src_cnt,
		  unsigned long flags)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	union {
		u32 value;
		struct iop13xx_adma_desc_ctrl field;
	} u_desc_ctrl;

	u_desc_ctrl.value = 0;
	u_desc_ctrl.field.src_select = src_cnt - 1;
	u_desc_ctrl.field.xfer_dir = 3; /* local to internal bus */
	u_desc_ctrl.field.int_en = flags & DMA_PREP_INTERRUPT;
	hw_desc->desc_ctrl = u_desc_ctrl.value;
	hw_desc->crc_addr = 0;

}
#define iop_desc_init_null_xor(d, s, i) iop_desc_init_xor(d, s, i)

/* to do: support buffers larger than ADMA_MAX_BYTE_COUNT */
static inline int
iop_desc_init_zero_sum(struct iop_adma_desc_slot *desc, int src_cnt,
		       unsigned long flags)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	union {
		u32 value;
		struct iop13xx_adma_desc_ctrl field;
	} u_desc_ctrl;

	u_desc_ctrl.value = 0;
	u_desc_ctrl.field.src_select = src_cnt - 1;
	u_desc_ctrl.field.xfer_dir = 3; /* local to internal bus */
	u_desc_ctrl.field.zero_result = 1;
	u_desc_ctrl.field.status_write_back_en = 1;
	u_desc_ctrl.field.int_en = flags & DMA_PREP_INTERRUPT;
	hw_desc->desc_ctrl = u_desc_ctrl.value;
	hw_desc->crc_addr = 0;

	return 1;
}

static inline void
iop_desc_init_pq(struct iop_adma_desc_slot *desc, int src_cnt,
		  unsigned long flags)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	union {
		u32 value;
		struct iop13xx_adma_desc_ctrl field;
	} u_desc_ctrl;

	u_desc_ctrl.value = 0;
	u_desc_ctrl.field.src_select = src_cnt - 1;
	u_desc_ctrl.field.xfer_dir = 3; /* local to internal bus */
	u_desc_ctrl.field.pq_xfer_en = 1;
	u_desc_ctrl.field.p_xfer_dis = !!(flags & DMA_PREP_PQ_DISABLE_P);
	u_desc_ctrl.field.int_en = flags & DMA_PREP_INTERRUPT;
	hw_desc->desc_ctrl = u_desc_ctrl.value;
}

static inline void
iop_desc_init_pq_zero_sum(struct iop_adma_desc_slot *desc, int src_cnt,
			  unsigned long flags)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	union {
		u32 value;
		struct iop13xx_adma_desc_ctrl field;
	} u_desc_ctrl;

	u_desc_ctrl.value = 0;
	u_desc_ctrl.field.src_select = src_cnt - 1;
	u_desc_ctrl.field.xfer_dir = 3; /* local to internal bus */
	u_desc_ctrl.field.zero_result = 1;
	u_desc_ctrl.field.status_write_back_en = 1;
	u_desc_ctrl.field.pq_xfer_en = 1;
	u_desc_ctrl.field.p_xfer_dis = !!(flags & DMA_PREP_PQ_DISABLE_P);
	u_desc_ctrl.field.int_en = flags & DMA_PREP_INTERRUPT;
	hw_desc->desc_ctrl = u_desc_ctrl.value;
}

static inline void iop_desc_set_byte_count(struct iop_adma_desc_slot *desc,
					struct iop_adma_chan *chan,
					u32 byte_count)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	hw_desc->byte_count = byte_count;
}

static inline void
iop_desc_set_zero_sum_byte_count(struct iop_adma_desc_slot *desc, u32 len)
{
	int slots_per_op = desc->slots_per_op;
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc, *iter;
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

#define iop_desc_set_pq_zero_sum_byte_count iop_desc_set_zero_sum_byte_count

static inline void iop_desc_set_dest_addr(struct iop_adma_desc_slot *desc,
					struct iop_adma_chan *chan,
					dma_addr_t addr)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	hw_desc->dest_addr = addr;
	hw_desc->upper_dest_addr = 0;
}

static inline void
iop_desc_set_pq_addr(struct iop_adma_desc_slot *desc, dma_addr_t *addr)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;

	hw_desc->dest_addr = addr[0];
	hw_desc->q_dest_addr = addr[1];
	hw_desc->upper_dest_addr = 0;
}

static inline void iop_desc_set_memcpy_src_addr(struct iop_adma_desc_slot *desc,
					dma_addr_t addr)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	hw_desc->src[0].src_addr = addr;
	hw_desc->src[0].upper_src_addr = 0;
}

static inline void iop_desc_set_xor_src_addr(struct iop_adma_desc_slot *desc,
					int src_idx, dma_addr_t addr)
{
	int slot_cnt = desc->slot_cnt, slots_per_op = desc->slots_per_op;
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc, *iter;
	int i = 0;

	do {
		iter = iop_hw_desc_slot_idx(hw_desc, i);
		iter->src[src_idx].src_addr = addr;
		iter->src[src_idx].upper_src_addr = 0;
		slot_cnt -= slots_per_op;
		if (slot_cnt) {
			i += slots_per_op;
			addr += IOP_ADMA_XOR_MAX_BYTE_COUNT;
		}
	} while (slot_cnt);
}

static inline void
iop_desc_set_pq_src_addr(struct iop_adma_desc_slot *desc, int src_idx,
			 dma_addr_t addr, unsigned char coef)
{
	int slot_cnt = desc->slot_cnt, slots_per_op = desc->slots_per_op;
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc, *iter;
	struct iop13xx_adma_src *src;
	int i = 0;

	do {
		iter = iop_hw_desc_slot_idx(hw_desc, i);
		src = &iter->src[src_idx];
		src->src_addr = addr;
		src->pq_upper_src_addr = 0;
		src->pq_dmlt = coef;
		slot_cnt -= slots_per_op;
		if (slot_cnt) {
			i += slots_per_op;
			addr += IOP_ADMA_PQ_MAX_BYTE_COUNT;
		}
	} while (slot_cnt);
}

static inline void
iop_desc_init_interrupt(struct iop_adma_desc_slot *desc,
	struct iop_adma_chan *chan)
{
	iop_desc_init_memcpy(desc, 1);
	iop_desc_set_byte_count(desc, chan, 0);
	iop_desc_set_dest_addr(desc, chan, 0);
	iop_desc_set_memcpy_src_addr(desc, 0);
}

#define iop_desc_set_zero_sum_src_addr iop_desc_set_xor_src_addr
#define iop_desc_set_pq_zero_sum_src_addr iop_desc_set_pq_src_addr

static inline void
iop_desc_set_pq_zero_sum_addr(struct iop_adma_desc_slot *desc, int pq_idx,
			      dma_addr_t *src)
{
	iop_desc_set_xor_src_addr(desc, pq_idx, src[pq_idx]);
	iop_desc_set_xor_src_addr(desc, pq_idx+1, src[pq_idx+1]);
}

static inline void iop_desc_set_next_desc(struct iop_adma_desc_slot *desc,
					u32 next_desc_addr)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;

	iop_paranoia(hw_desc->next_desc);
	hw_desc->next_desc = next_desc_addr;
}

static inline u32 iop_desc_get_next_desc(struct iop_adma_desc_slot *desc)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	return hw_desc->next_desc;
}

static inline void iop_desc_clear_next_desc(struct iop_adma_desc_slot *desc)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	hw_desc->next_desc = 0;
}

static inline void iop_desc_set_block_fill_val(struct iop_adma_desc_slot *desc,
						u32 val)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	hw_desc->block_fill_data = val;
}

static inline enum sum_check_flags
iop_desc_get_zero_result(struct iop_adma_desc_slot *desc)
{
	struct iop13xx_adma_desc_hw *hw_desc = desc->hw_desc;
	struct iop13xx_adma_desc_ctrl desc_ctrl = hw_desc->desc_ctrl_field;
	struct iop13xx_adma_byte_count byte_count = hw_desc->byte_count_field;
	enum sum_check_flags flags;

	BUG_ON(!(byte_count.tx_complete && desc_ctrl.zero_result));

	flags = byte_count.zero_result_err_q << SUM_CHECK_Q;
	flags |= byte_count.zero_result_err << SUM_CHECK_P;

	return flags;
}

static inline void iop_chan_append(struct iop_adma_chan *chan)
{
	u32 adma_accr;

	adma_accr = __raw_readl(ADMA_ACCR(chan));
	adma_accr |= 0x2;
	__raw_writel(adma_accr, ADMA_ACCR(chan));
}

static inline u32 iop_chan_get_status(struct iop_adma_chan *chan)
{
	return __raw_readl(ADMA_ACSR(chan));
}

static inline void iop_chan_disable(struct iop_adma_chan *chan)
{
	u32 adma_chan_ctrl = __raw_readl(ADMA_ACCR(chan));
	adma_chan_ctrl &= ~0x1;
	__raw_writel(adma_chan_ctrl, ADMA_ACCR(chan));
}

static inline void iop_chan_enable(struct iop_adma_chan *chan)
{
	u32 adma_chan_ctrl;

	adma_chan_ctrl = __raw_readl(ADMA_ACCR(chan));
	adma_chan_ctrl |= 0x1;
	__raw_writel(adma_chan_ctrl, ADMA_ACCR(chan));
}

static inline void iop_adma_device_clear_eot_status(struct iop_adma_chan *chan)
{
	u32 status = __raw_readl(ADMA_ACSR(chan));
	status &= (1 << 12);
	__raw_writel(status, ADMA_ACSR(chan));
}

static inline void iop_adma_device_clear_eoc_status(struct iop_adma_chan *chan)
{
	u32 status = __raw_readl(ADMA_ACSR(chan));
	status &= (1 << 11);
	__raw_writel(status, ADMA_ACSR(chan));
}

static inline void iop_adma_device_clear_err_status(struct iop_adma_chan *chan)
{
	u32 status = __raw_readl(ADMA_ACSR(chan));
	status &= (1 << 9) | (1 << 5) | (1 << 4) | (1 << 3);
	__raw_writel(status, ADMA_ACSR(chan));
}

static inline int
iop_is_err_int_parity(unsigned long status, struct iop_adma_chan *chan)
{
	return test_bit(9, &status);
}

static inline int
iop_is_err_mcu_abort(unsigned long status, struct iop_adma_chan *chan)
{
	return test_bit(5, &status);
}

static inline int
iop_is_err_int_tabort(unsigned long status, struct iop_adma_chan *chan)
{
	return test_bit(4, &status);
}

static inline int
iop_is_err_int_mabort(unsigned long status, struct iop_adma_chan *chan)
{
	return test_bit(3, &status);
}

static inline int
iop_is_err_pci_tabort(unsigned long status, struct iop_adma_chan *chan)
{
	return 0;
}

static inline int
iop_is_err_pci_mabort(unsigned long status, struct iop_adma_chan *chan)
{
	return 0;
}

static inline int
iop_is_err_split_tx(unsigned long status, struct iop_adma_chan *chan)
{
	return 0;
}

#endif /* _ADMA_H */
