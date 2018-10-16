/*
 * Copyright (C) 2016-2018 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __NFP_BPF_H__
#define __NFP_BPF_H__ 1

#include <linux/bitfield.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/rhashtable.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "../nfp_asm.h"
#include "fw.h"

#define cmsg_warn(bpf, msg...)	nn_dp_warn(&(bpf)->app->ctrl->dp, msg)

/* For relocation logic use up-most byte of branch instruction as scratch
 * area.  Remember to clear this before sending instructions to HW!
 */
#define OP_RELO_TYPE	0xff00000000000000ULL

enum nfp_relo_type {
	RELO_NONE = 0,
	/* standard internal jumps */
	RELO_BR_REL,
	/* internal jumps to parts of the outro */
	RELO_BR_GO_OUT,
	RELO_BR_GO_ABORT,
	/* external jumps to fixed addresses */
	RELO_BR_NEXT_PKT,
	RELO_BR_HELPER,
	/* immediate relocation against load address */
	RELO_IMMED_REL,
};

/* To make absolute relocated branches (branches other than RELO_BR_REL)
 * distinguishable in user space dumps from normal jumps, add a large offset
 * to them.
 */
#define BR_OFF_RELO		15000

enum static_regs {
	STATIC_REG_IMMA		= 20, /* Bank AB */
	STATIC_REG_IMM		= 21, /* Bank AB */
	STATIC_REG_STACK	= 22, /* Bank A */
	STATIC_REG_PKT_LEN	= 22, /* Bank B */
};

enum pkt_vec {
	PKT_VEC_PKT_LEN		= 0,
	PKT_VEC_PKT_PTR		= 2,
	PKT_VEC_QSEL_SET	= 4,
	PKT_VEC_QSEL_VAL	= 6,
};

#define PKT_VEL_QSEL_SET_BIT	4

#define pv_len(np)	reg_lm(1, PKT_VEC_PKT_LEN)
#define pv_ctm_ptr(np)	reg_lm(1, PKT_VEC_PKT_PTR)
#define pv_qsel_set(np)	reg_lm(1, PKT_VEC_QSEL_SET)
#define pv_qsel_val(np)	reg_lm(1, PKT_VEC_QSEL_VAL)

#define stack_reg(np)	reg_a(STATIC_REG_STACK)
#define stack_imm(np)	imm_b(np)
#define plen_reg(np)	reg_b(STATIC_REG_PKT_LEN)
#define pptr_reg(np)	pv_ctm_ptr(np)
#define imm_a(np)	reg_a(STATIC_REG_IMM)
#define imm_b(np)	reg_b(STATIC_REG_IMM)
#define imma_a(np)	reg_a(STATIC_REG_IMMA)
#define imma_b(np)	reg_b(STATIC_REG_IMMA)
#define imm_both(np)	reg_both(STATIC_REG_IMM)

#define NFP_BPF_ABI_FLAGS	reg_imm(0)
#define   NFP_BPF_ABI_FLAG_MARK	1

/**
 * struct nfp_app_bpf - bpf app priv structure
 * @app:		backpointer to the app
 *
 * @bpf_dev:		BPF offload device handle
 *
 * @tag_allocator:	bitmap of control message tags in use
 * @tag_alloc_next:	next tag bit to allocate
 * @tag_alloc_last:	next tag bit to be freed
 *
 * @cmsg_replies:	received cmsg replies waiting to be consumed
 * @cmsg_wq:		work queue for waiting for cmsg replies
 *
 * @map_list:		list of offloaded maps
 * @maps_in_use:	number of currently offloaded maps
 * @map_elems_in_use:	number of elements allocated to offloaded maps
 *
 * @maps_neutral:	hash table of offload-neutral maps (on pointer)
 *
 * @adjust_head:	adjust head capability
 * @adjust_head.flags:		extra flags for adjust head
 * @adjust_head.off_min:	minimal packet offset within buffer required
 * @adjust_head.off_max:	maximum packet offset within buffer required
 * @adjust_head.guaranteed_sub:	negative adjustment guaranteed possible
 * @adjust_head.guaranteed_add:	positive adjustment guaranteed possible
 *
 * @maps:		map capability
 * @maps.types:			supported map types
 * @maps.max_maps:		max number of maps supported
 * @maps.max_elems:		max number of entries in each map
 * @maps.max_key_sz:		max size of map key
 * @maps.max_val_sz:		max size of map value
 * @maps.max_elem_sz:		max size of map entry (key + value)
 *
 * @helpers:		helper addressess for various calls
 * @helpers.map_lookup:		map lookup helper address
 * @helpers.map_update:		map update helper address
 * @helpers.map_delete:		map delete helper address
 * @helpers.perf_event_output:	output perf event to a ring buffer
 *
 * @pseudo_random:	FW initialized the pseudo-random machinery (CSRs)
 * @queue_select:	BPF can set the RX queue ID in packet vector
 * @adjust_tail:	BPF can simply trunc packet size for adjust tail
 */
struct nfp_app_bpf {
	struct nfp_app *app;

	struct bpf_offload_dev *bpf_dev;

	DECLARE_BITMAP(tag_allocator, U16_MAX + 1);
	u16 tag_alloc_next;
	u16 tag_alloc_last;

	struct sk_buff_head cmsg_replies;
	struct wait_queue_head cmsg_wq;

	struct list_head map_list;
	unsigned int maps_in_use;
	unsigned int map_elems_in_use;

	struct rhashtable maps_neutral;

	struct nfp_bpf_cap_adjust_head {
		u32 flags;
		int off_min;
		int off_max;
		int guaranteed_sub;
		int guaranteed_add;
	} adjust_head;

	struct {
		u32 types;
		u32 max_maps;
		u32 max_elems;
		u32 max_key_sz;
		u32 max_val_sz;
		u32 max_elem_sz;
	} maps;

	struct {
		u32 map_lookup;
		u32 map_update;
		u32 map_delete;
		u32 perf_event_output;
	} helpers;

	bool pseudo_random;
	bool queue_select;
	bool adjust_tail;
};

enum nfp_bpf_map_use {
	NFP_MAP_UNUSED = 0,
	NFP_MAP_USE_READ,
	NFP_MAP_USE_WRITE,
	NFP_MAP_USE_ATOMIC_CNT,
};

struct nfp_bpf_map_word {
	unsigned char type		:4;
	unsigned char non_zero_update	:1;
};

/**
 * struct nfp_bpf_map - private per-map data attached to BPF maps for offload
 * @offmap:	pointer to the offloaded BPF map
 * @bpf:	back pointer to bpf app private structure
 * @tid:	table id identifying map on datapath
 * @l:		link on the nfp_app_bpf->map_list list
 * @use_map:	map of how the value is used (in 4B chunks)
 */
struct nfp_bpf_map {
	struct bpf_offloaded_map *offmap;
	struct nfp_app_bpf *bpf;
	u32 tid;
	struct list_head l;
	struct nfp_bpf_map_word use_map[];
};

struct nfp_bpf_neutral_map {
	struct rhash_head l;
	struct bpf_map *ptr;
	u32 map_id;
	u32 count;
};

extern const struct rhashtable_params nfp_bpf_maps_neutral_params;

struct nfp_prog;
struct nfp_insn_meta;
typedef int (*instr_cb_t)(struct nfp_prog *, struct nfp_insn_meta *);

#define nfp_prog_first_meta(nfp_prog)					\
	list_first_entry(&(nfp_prog)->insns, struct nfp_insn_meta, l)
#define nfp_prog_last_meta(nfp_prog)					\
	list_last_entry(&(nfp_prog)->insns, struct nfp_insn_meta, l)
#define nfp_meta_next(meta)	list_next_entry(meta, l)
#define nfp_meta_prev(meta)	list_prev_entry(meta, l)

/**
 * struct nfp_bpf_reg_state - register state for calls
 * @reg: BPF register state from latest path
 * @var_off: for stack arg - changes stack offset on different paths
 */
struct nfp_bpf_reg_state {
	struct bpf_reg_state reg;
	bool var_off;
};

#define FLAG_INSN_IS_JUMP_DST	BIT(0)

/**
 * struct nfp_insn_meta - BPF instruction wrapper
 * @insn: BPF instruction
 * @ptr: pointer type for memory operations
 * @ldst_gather_len: memcpy length gathered from load/store sequence
 * @paired_st: the paired store insn at the head of the sequence
 * @ptr_not_const: pointer is not always constant
 * @pkt_cache: packet data cache information
 * @pkt_cache.range_start: start offset for associated packet data cache
 * @pkt_cache.range_end: end offset for associated packet data cache
 * @pkt_cache.do_init: this read needs to initialize packet data cache
 * @xadd_over_16bit: 16bit immediate is not guaranteed
 * @xadd_maybe_16bit: 16bit immediate is possible
 * @jmp_dst: destination info for jump instructions
 * @jump_neg_op: jump instruction has inverted immediate, use ADD instead of SUB
 * @func_id: function id for call instructions
 * @arg1: arg1 for call instructions
 * @arg2: arg2 for call instructions
 * @umin_src: copy of core verifier umin_value for src opearnd.
 * @umax_src: copy of core verifier umax_value for src operand.
 * @umin_dst: copy of core verifier umin_value for dst opearnd.
 * @umax_dst: copy of core verifier umax_value for dst operand.
 * @off: index of first generated machine instruction (in nfp_prog.prog)
 * @n: eBPF instruction number
 * @flags: eBPF instruction extra optimization flags
 * @skip: skip this instruction (optimized out)
 * @double_cb: callback for second part of the instruction
 * @l: link on nfp_prog->insns list
 */
struct nfp_insn_meta {
	struct bpf_insn insn;
	union {
		/* pointer ops (ld/st/xadd) */
		struct {
			struct bpf_reg_state ptr;
			struct bpf_insn *paired_st;
			s16 ldst_gather_len;
			bool ptr_not_const;
			struct {
				s16 range_start;
				s16 range_end;
				bool do_init;
			} pkt_cache;
			bool xadd_over_16bit;
			bool xadd_maybe_16bit;
		};
		/* jump */
		struct {
			struct nfp_insn_meta *jmp_dst;
			bool jump_neg_op;
		};
		/* function calls */
		struct {
			u32 func_id;
			struct bpf_reg_state arg1;
			struct nfp_bpf_reg_state arg2;
		};
		/* We are interested in range info for operands of ALU
		 * operations. For example, shift amount, multiplicand and
		 * multiplier etc.
		 */
		struct {
			u64 umin_src;
			u64 umax_src;
			u64 umin_dst;
			u64 umax_dst;
		};
	};
	unsigned int off;
	unsigned short n;
	unsigned short flags;
	bool skip;
	instr_cb_t double_cb;

	struct list_head l;
};

#define BPF_SIZE_MASK	0x18

static inline u8 mbpf_class(const struct nfp_insn_meta *meta)
{
	return BPF_CLASS(meta->insn.code);
}

static inline u8 mbpf_src(const struct nfp_insn_meta *meta)
{
	return BPF_SRC(meta->insn.code);
}

static inline u8 mbpf_op(const struct nfp_insn_meta *meta)
{
	return BPF_OP(meta->insn.code);
}

static inline u8 mbpf_mode(const struct nfp_insn_meta *meta)
{
	return BPF_MODE(meta->insn.code);
}

static inline bool is_mbpf_alu(const struct nfp_insn_meta *meta)
{
	return mbpf_class(meta) == BPF_ALU64 || mbpf_class(meta) == BPF_ALU;
}

static inline bool is_mbpf_load(const struct nfp_insn_meta *meta)
{
	return (meta->insn.code & ~BPF_SIZE_MASK) == (BPF_LDX | BPF_MEM);
}

static inline bool is_mbpf_store(const struct nfp_insn_meta *meta)
{
	return (meta->insn.code & ~BPF_SIZE_MASK) == (BPF_STX | BPF_MEM);
}

static inline bool is_mbpf_load_pkt(const struct nfp_insn_meta *meta)
{
	return is_mbpf_load(meta) && meta->ptr.type == PTR_TO_PACKET;
}

static inline bool is_mbpf_store_pkt(const struct nfp_insn_meta *meta)
{
	return is_mbpf_store(meta) && meta->ptr.type == PTR_TO_PACKET;
}

static inline bool is_mbpf_classic_load(const struct nfp_insn_meta *meta)
{
	u8 code = meta->insn.code;

	return BPF_CLASS(code) == BPF_LD &&
	       (BPF_MODE(code) == BPF_ABS || BPF_MODE(code) == BPF_IND);
}

static inline bool is_mbpf_classic_store(const struct nfp_insn_meta *meta)
{
	u8 code = meta->insn.code;

	return BPF_CLASS(code) == BPF_ST && BPF_MODE(code) == BPF_MEM;
}

static inline bool is_mbpf_classic_store_pkt(const struct nfp_insn_meta *meta)
{
	return is_mbpf_classic_store(meta) && meta->ptr.type == PTR_TO_PACKET;
}

static inline bool is_mbpf_xadd(const struct nfp_insn_meta *meta)
{
	return (meta->insn.code & ~BPF_SIZE_MASK) == (BPF_STX | BPF_XADD);
}

static inline bool is_mbpf_mul(const struct nfp_insn_meta *meta)
{
	return is_mbpf_alu(meta) && mbpf_op(meta) == BPF_MUL;
}

static inline bool is_mbpf_div(const struct nfp_insn_meta *meta)
{
	return is_mbpf_alu(meta) && mbpf_op(meta) == BPF_DIV;
}

/**
 * struct nfp_prog - nfp BPF program
 * @bpf: backpointer to the bpf app priv structure
 * @prog: machine code
 * @prog_len: number of valid instructions in @prog array
 * @__prog_alloc_len: alloc size of @prog array
 * @verifier_meta: temporary storage for verifier's insn meta
 * @type: BPF program type
 * @last_bpf_off: address of the last instruction translated from BPF
 * @tgt_out: jump target for normal exit
 * @tgt_abort: jump target for abort (e.g. access outside of packet buffer)
 * @n_translated: number of successfully translated instructions (for errors)
 * @error: error code if something went wrong
 * @stack_depth: max stack depth from the verifier
 * @adjust_head_location: if program has single adjust head call - the insn no.
 * @map_records_cnt: the number of map pointers recorded for this prog
 * @map_records: the map record pointers from bpf->maps_neutral
 * @insns: list of BPF instruction wrappers (struct nfp_insn_meta)
 */
struct nfp_prog {
	struct nfp_app_bpf *bpf;

	u64 *prog;
	unsigned int prog_len;
	unsigned int __prog_alloc_len;

	struct nfp_insn_meta *verifier_meta;

	enum bpf_prog_type type;

	unsigned int last_bpf_off;
	unsigned int tgt_out;
	unsigned int tgt_abort;

	unsigned int n_translated;
	int error;

	unsigned int stack_depth;
	unsigned int adjust_head_location;

	unsigned int map_records_cnt;
	struct nfp_bpf_neutral_map **map_records;

	struct list_head insns;
};

/**
 * struct nfp_bpf_vnic - per-vNIC BPF priv structure
 * @tc_prog:	currently loaded cls_bpf program
 * @start_off:	address of the first instruction in the memory
 * @tgt_done:	jump target to get the next packet
 */
struct nfp_bpf_vnic {
	struct bpf_prog *tc_prog;
	unsigned int start_off;
	unsigned int tgt_done;
};

void nfp_bpf_jit_prepare(struct nfp_prog *nfp_prog, unsigned int cnt);
int nfp_bpf_jit(struct nfp_prog *prog);
bool nfp_bpf_supported_opcode(u8 code);

extern const struct bpf_prog_offload_ops nfp_bpf_analyzer_ops;

struct netdev_bpf;
struct nfp_app;
struct nfp_net;

int nfp_ndo_bpf(struct nfp_app *app, struct nfp_net *nn,
		struct netdev_bpf *bpf);
int nfp_net_bpf_offload(struct nfp_net *nn, struct bpf_prog *prog,
			bool old_prog, struct netlink_ext_ack *extack);

struct nfp_insn_meta *
nfp_bpf_goto_meta(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
		  unsigned int insn_idx, unsigned int n_insns);

void *nfp_bpf_relo_for_vnic(struct nfp_prog *nfp_prog, struct nfp_bpf_vnic *bv);

long long int
nfp_bpf_ctrl_alloc_map(struct nfp_app_bpf *bpf, struct bpf_map *map);
void
nfp_bpf_ctrl_free_map(struct nfp_app_bpf *bpf, struct nfp_bpf_map *nfp_map);
int nfp_bpf_ctrl_getfirst_entry(struct bpf_offloaded_map *offmap,
				void *next_key);
int nfp_bpf_ctrl_update_entry(struct bpf_offloaded_map *offmap,
			      void *key, void *value, u64 flags);
int nfp_bpf_ctrl_del_entry(struct bpf_offloaded_map *offmap, void *key);
int nfp_bpf_ctrl_lookup_entry(struct bpf_offloaded_map *offmap,
			      void *key, void *value);
int nfp_bpf_ctrl_getnext_entry(struct bpf_offloaded_map *offmap,
			       void *key, void *next_key);

int nfp_bpf_event_output(struct nfp_app_bpf *bpf, const void *data,
			 unsigned int len);

void nfp_bpf_ctrl_msg_rx(struct nfp_app *app, struct sk_buff *skb);
void
nfp_bpf_ctrl_msg_rx_raw(struct nfp_app *app, const void *data,
			unsigned int len);
#endif
