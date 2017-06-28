/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
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

/*
 * nfp_target.c
 * CPP Access Width Decoder
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Francois H. Theron <francois.theron@netronome.com>
 */

#include <linux/bitops.h>

#include "nfp_cpp.h"

#include "nfp6000/nfp6000.h"

#define P32 1
#define P64 2

/* This structure ONLY includes items that can be done with a read or write of
 * 32-bit or 64-bit words. All others are not listed.
 */

#define AT(_action, _token, _pull, _push)				\
	case NFP_CPP_ID(0, (_action), (_token)):			\
		return PUSHPULL((_pull), (_push))

static int target_rw(u32 cpp_id, int pp, int start, int len)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(0, 0,  0, pp);
	AT(1, 0, pp,  0);
	AT(NFP_CPP_ACTION_RW, 0, pp, pp);
	default:
		return -EINVAL;
	}
}

static int nfp6000_nbi_dma(u32 cpp_id)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(0, 0,   0, P64);	/* ReadNbiDma */
	AT(1, 0,   P64, 0);	/* WriteNbiDma */
	AT(NFP_CPP_ACTION_RW, 0, P64, P64);
	default:
		return -EINVAL;
	}
}

static int nfp6000_nbi_stats(u32 cpp_id)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(0, 0,   0, P32);	/* ReadNbiStats */
	AT(1, 0,   P32, 0);	/* WriteNbiStats */
	AT(NFP_CPP_ACTION_RW, 0, P32, P32);
	default:
		return -EINVAL;
	}
}

static int nfp6000_nbi_tm(u32 cpp_id)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(0, 0,   0, P64);	/* ReadNbiTM */
	AT(1, 0,   P64, 0);	/* WriteNbiTM */
	AT(NFP_CPP_ACTION_RW, 0, P64, P64);
	default:
		return -EINVAL;
	}
}

static int nfp6000_nbi_ppc(u32 cpp_id)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(0, 0,   0, P64);	/* ReadNbiPreclassifier */
	AT(1, 0,   P64, 0);	/* WriteNbiPreclassifier */
	AT(NFP_CPP_ACTION_RW, 0, P64, P64);
	default:
		return -EINVAL;
	}
}

static int nfp6000_nbi(u32 cpp_id, u64 address)
{
	u64 rel_addr = address & 0x3fFFFF;

	if (rel_addr < (1 << 20))
		return nfp6000_nbi_dma(cpp_id);
	if (rel_addr < (2 << 20))
		return nfp6000_nbi_stats(cpp_id);
	if (rel_addr < (3 << 20))
		return nfp6000_nbi_tm(cpp_id);
	return nfp6000_nbi_ppc(cpp_id);
}

/* This structure ONLY includes items that can be done with a read or write of
 * 32-bit or 64-bit words. All others are not listed.
 */
static int nfp6000_mu_common(u32 cpp_id)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(NFP_CPP_ACTION_RW, 0, P64, P64);	/* read_be/write_be */
	AT(NFP_CPP_ACTION_RW, 1, P64, P64);	/* read_le/write_le */
	AT(NFP_CPP_ACTION_RW, 2, P64, P64);	/* read_swap_be/write_swap_be */
	AT(NFP_CPP_ACTION_RW, 3, P64, P64);	/* read_swap_le/write_swap_le */
	AT(0, 0,   0, P64);	/* read_be */
	AT(0, 1,   0, P64);	/* read_le */
	AT(0, 2,   0, P64);	/* read_swap_be */
	AT(0, 3,   0, P64);	/* read_swap_le */
	AT(1, 0, P64,   0);	/* write_be */
	AT(1, 1, P64,   0);	/* write_le */
	AT(1, 2, P64,   0);	/* write_swap_be */
	AT(1, 3, P64,   0);	/* write_swap_le */
	AT(3, 0,   0, P32);	/* atomic_read */
	AT(3, 2, P32,   0);	/* mask_compare_write */
	AT(4, 0, P32,   0);	/* atomic_write */
	AT(4, 2,   0,   0);	/* atomic_write_imm */
	AT(4, 3,   0, P32);	/* swap_imm */
	AT(5, 0, P32,   0);	/* set */
	AT(5, 3,   0, P32);	/* test_set_imm */
	AT(6, 0, P32,   0);	/* clr */
	AT(6, 3,   0, P32);	/* test_clr_imm */
	AT(7, 0, P32,   0);	/* add */
	AT(7, 3,   0, P32);	/* test_add_imm */
	AT(8, 0, P32,   0);	/* addsat */
	AT(8, 3,   0, P32);	/* test_subsat_imm */
	AT(9, 0, P32,   0);	/* sub */
	AT(9, 3,   0, P32);	/* test_sub_imm */
	AT(10, 0, P32,   0);	/* subsat */
	AT(10, 3,   0, P32);	/* test_subsat_imm */
	AT(13, 0,   0, P32);	/* microq128_get */
	AT(13, 1,   0, P32);	/* microq128_pop */
	AT(13, 2, P32,   0);	/* microq128_put */
	AT(15, 0, P32,   0);	/* xor */
	AT(15, 3,   0, P32);	/* test_xor_imm */
	AT(28, 0,   0, P32);	/* read32_be */
	AT(28, 1,   0, P32);	/* read32_le */
	AT(28, 2,   0, P32);	/* read32_swap_be */
	AT(28, 3,   0, P32);	/* read32_swap_le */
	AT(31, 0, P32,   0);	/* write32_be */
	AT(31, 1, P32,   0);	/* write32_le */
	AT(31, 2, P32,   0);	/* write32_swap_be */
	AT(31, 3, P32,   0);	/* write32_swap_le */
	default:
		return -EINVAL;
	}
}

static int nfp6000_mu_ctm(u32 cpp_id)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(16, 1,   0, P32);	/* packet_read_packet_status */
	AT(17, 1,   0, P32);	/* packet_credit_get */
	AT(17, 3,   0, P64);	/* packet_add_thread */
	AT(18, 2,   0, P64);	/* packet_free_and_return_pointer */
	AT(18, 3,   0, P64);	/* packet_return_pointer */
	AT(21, 0,   0, P64);	/* pe_dma_to_memory_indirect */
	AT(21, 1,   0, P64);	/* pe_dma_to_memory_indirect_swap */
	AT(21, 2,   0, P64);	/* pe_dma_to_memory_indirect_free */
	AT(21, 3,   0, P64);	/* pe_dma_to_memory_indirect_free_swap */
	default:
		return nfp6000_mu_common(cpp_id);
	}
}

static int nfp6000_mu_emu(u32 cpp_id)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(18, 0,   0, P32);	/* read_queue */
	AT(18, 1,   0, P32);	/* read_queue_ring */
	AT(18, 2, P32,   0);	/* write_queue */
	AT(18, 3, P32,   0);	/* write_queue_ring */
	AT(20, 2, P32,   0);	/* journal */
	AT(21, 0,   0, P32);	/* get */
	AT(21, 1,   0, P32);	/* get_eop */
	AT(21, 2,   0, P32);	/* get_freely */
	AT(22, 0,   0, P32);	/* pop */
	AT(22, 1,   0, P32);	/* pop_eop */
	AT(22, 2,   0, P32);	/* pop_freely */
	default:
		return nfp6000_mu_common(cpp_id);
	}
}

static int nfp6000_mu_imu(u32 cpp_id)
{
	return nfp6000_mu_common(cpp_id);
}

static int nfp6000_mu(u32 cpp_id, u64 address)
{
	int pp;

	if (address < 0x2000000000ULL)
		pp = nfp6000_mu_ctm(cpp_id);
	else if (address < 0x8000000000ULL)
		pp = nfp6000_mu_emu(cpp_id);
	else if (address < 0x9800000000ULL)
		pp = nfp6000_mu_ctm(cpp_id);
	else if (address < 0x9C00000000ULL)
		pp = nfp6000_mu_emu(cpp_id);
	else if (address < 0xA000000000ULL)
		pp = nfp6000_mu_imu(cpp_id);
	else
		pp = nfp6000_mu_ctm(cpp_id);

	return pp;
}

static int nfp6000_ila(u32 cpp_id)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(0, 1,   0, P32);	/* read_check_error */
	AT(2, 0,   0, P32);	/* read_int */
	AT(3, 0, P32,   0);	/* write_int */
	default:
		return target_rw(cpp_id, P32, 48, 4);
	}
}

static int nfp6000_pci(u32 cpp_id)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(2, 0,   0, P32);
	AT(3, 0, P32,   0);
	default:
		return target_rw(cpp_id, P32, 4, 4);
	}
}

static int nfp6000_crypto(u32 cpp_id)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(2, 0, P64,   0);
	default:
		return target_rw(cpp_id, P64, 12, 4);
	}
}

static int nfp6000_cap_xpb(u32 cpp_id)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(0, 1,   0, P32); /* RingGet */
	AT(0, 2, P32,   0); /* Interthread Signal */
	AT(1, 1, P32,   0); /* RingPut */
	AT(1, 2, P32,   0); /* CTNNWr */
	AT(2, 0,   0, P32); /* ReflectRd, signal none */
	AT(2, 1,   0, P32); /* ReflectRd, signal self */
	AT(2, 2,   0, P32); /* ReflectRd, signal remote */
	AT(2, 3,   0, P32); /* ReflectRd, signal both */
	AT(3, 0, P32,   0); /* ReflectWr, signal none */
	AT(3, 1, P32,   0); /* ReflectWr, signal self */
	AT(3, 2, P32,   0); /* ReflectWr, signal remote */
	AT(3, 3, P32,   0); /* ReflectWr, signal both */
	AT(NFP_CPP_ACTION_RW, 1, P32, P32);
	default:
		return target_rw(cpp_id, P32, 1, 63);
	}
}

static int nfp6000_cls(u32 cpp_id)
{
	switch (cpp_id & NFP_CPP_ID(0, ~0, ~0)) {
	AT(0, 3, P32,  0); /* xor */
	AT(2, 0, P32,  0); /* set */
	AT(2, 1, P32,  0); /* clr */
	AT(4, 0, P32,  0); /* add */
	AT(4, 1, P32,  0); /* add64 */
	AT(6, 0, P32,  0); /* sub */
	AT(6, 1, P32,  0); /* sub64 */
	AT(6, 2, P32,  0); /* subsat */
	AT(8, 2, P32,  0); /* hash_mask */
	AT(8, 3, P32,  0); /* hash_clear */
	AT(9, 0,  0, P32); /* ring_get */
	AT(9, 1,  0, P32); /* ring_pop */
	AT(9, 2,  0, P32); /* ring_get_freely */
	AT(9, 3,  0, P32); /* ring_pop_freely */
	AT(10, 0, P32,  0); /* ring_put */
	AT(10, 2, P32,  0); /* ring_journal */
	AT(14, 0,  P32, 0); /* reflect_write_sig_local */
	AT(15, 1,  0, P32); /* reflect_read_sig_local */
	AT(17, 2, P32,  0); /* statisic */
	AT(24, 0,  0, P32); /* ring_read */
	AT(24, 1, P32,  0); /* ring_write */
	AT(25, 0,  0, P32); /* ring_workq_add_thread */
	AT(25, 1, P32,  0); /* ring_workq_add_work */
	default:
		return target_rw(cpp_id, P32, 0, 64);
	}
}

int nfp_target_pushpull(u32 cpp_id, u64 address)
{
	switch (NFP_CPP_ID_TARGET_of(cpp_id)) {
	case NFP_CPP_TARGET_NBI:
		return nfp6000_nbi(cpp_id, address);
	case NFP_CPP_TARGET_QDR:
		return target_rw(cpp_id, P32, 24, 4);
	case NFP_CPP_TARGET_ILA:
		return nfp6000_ila(cpp_id);
	case NFP_CPP_TARGET_MU:
		return nfp6000_mu(cpp_id, address);
	case NFP_CPP_TARGET_PCIE:
		return nfp6000_pci(cpp_id);
	case NFP_CPP_TARGET_ARM:
		if (address < 0x10000)
			return target_rw(cpp_id, P64, 1, 1);
		else
			return target_rw(cpp_id, P32, 1, 1);
	case NFP_CPP_TARGET_CRYPTO:
		return nfp6000_crypto(cpp_id);
	case NFP_CPP_TARGET_CT_XPB:
		return nfp6000_cap_xpb(cpp_id);
	case NFP_CPP_TARGET_CLS:
		return nfp6000_cls(cpp_id);
	case 0:
		return target_rw(cpp_id, P32, 4, 4);
	default:
		return -EINVAL;
	}
}

#undef AT
#undef P32
#undef P64

/* All magic NFP-6xxx IMB 'mode' numbers here are from:
 * Databook (1 August 2013)
 * - System Overview and Connectivity
 * -- Internal Connectivity
 * --- Distributed Switch Fabric - Command Push/Pull (DSF-CPP) Bus
 * ---- CPP addressing
 * ----- Table 3.6. CPP Address Translation Mode Commands
 */

#define _NIC_NFP6000_MU_LOCALITY_DIRECT     2

static int nfp_decode_basic(u64 addr, int *dest_island, int cpp_tgt,
			    int mode, bool addr40, int isld1, int isld0)
{
	int iid_lsb, idx_lsb;

	/* This function doesn't handle MU or CTXBP */
	if (cpp_tgt == NFP_CPP_TARGET_MU || cpp_tgt == NFP_CPP_TARGET_CT_XPB)
		return -EINVAL;

	switch (mode) {
	case 0:
		/* For VQDR, in this mode for 32-bit addressing
		 * it would be islands 0, 16, 32 and 48 depending on channel
		 * and upper address bits.
		 * Since those are not all valid islands, most decode
		 * cases would result in bad island IDs, but we do them
		 * anyway since this is decoding an address that is already
		 * assumed to be used as-is to get to sram.
		 */
		iid_lsb = addr40 ? 34 : 26;
		*dest_island = (addr >> iid_lsb) & 0x3F;
		return 0;
	case 1:
		/* For VQDR 32-bit, this would decode as:
		 * Channel 0: island#0
		 * Channel 1: island#0
		 * Channel 2: island#1
		 * Channel 3: island#1
		 * That would be valid as long as both islands
		 * have VQDR. Let's allow this.
		 */
		idx_lsb = addr40 ? 39 : 31;
		if (addr & BIT_ULL(idx_lsb))
			*dest_island = isld1;
		else
			*dest_island = isld0;

		return 0;
	case 2:
		/* For VQDR 32-bit:
		 * Channel 0: (island#0 | 0)
		 * Channel 1: (island#0 | 1)
		 * Channel 2: (island#1 | 0)
		 * Channel 3: (island#1 | 1)
		 *
		 * Make sure we compare against isldN values
		 * by clearing the LSB.
		 * This is what the silicon does.
		 */
		isld0 &= ~1;
		isld1 &= ~1;

		idx_lsb = addr40 ? 39 : 31;
		iid_lsb = idx_lsb - 1;

		if (addr & BIT_ULL(idx_lsb))
			*dest_island = isld1 | (int)((addr >> iid_lsb) & 1);
		else
			*dest_island = isld0 | (int)((addr >> iid_lsb) & 1);

		return 0;
	case 3:
		/* In this mode the data address starts to affect the island ID
		 * so rather not allow it. In some really specific case
		 * one could use this to send the upper half of the
		 * VQDR channel to another MU, but this is getting very
		 * specific.
		 * However, as above for mode 0, this is the decoder
		 * and the caller should validate the resulting IID.
		 * This blindly does what the silicon would do.
		 */
		isld0 &= ~3;
		isld1 &= ~3;

		idx_lsb = addr40 ? 39 : 31;
		iid_lsb = idx_lsb - 2;

		if (addr & BIT_ULL(idx_lsb))
			*dest_island = isld1 | (int)((addr >> iid_lsb) & 3);
		else
			*dest_island = isld0 | (int)((addr >> iid_lsb) & 3);

		return 0;
	default:
		return -EINVAL;
	}
}

static int nfp_encode_basic_qdr(u64 addr, int dest_island, int cpp_tgt,
				int mode, bool addr40, int isld1, int isld0)
{
	int v, ret;

	/* Full Island ID and channel bits overlap? */
	ret = nfp_decode_basic(addr, &v, cpp_tgt, mode, addr40, isld1, isld0);
	if (ret)
		return ret;

	/* The current address won't go where expected? */
	if (dest_island != -1 && dest_island != v)
		return -EINVAL;

	/* If dest_island was -1, we don't care where it goes. */
	return 0;
}

/* Try each option, take first one that fits.
 * Not sure if we would want to do some smarter
 * searching and prefer 0 or non-0 island IDs.
 */
static int nfp_encode_basic_search(u64 *addr, int dest_island, int *isld,
				   int iid_lsb, int idx_lsb, int v_max)
{
	int i, v;

	for (i = 0; i < 2; i++)
		for (v = 0; v < v_max; v++) {
			if (dest_island != (isld[i] | v))
				continue;

			*addr &= ~GENMASK_ULL(idx_lsb, iid_lsb);
			*addr |= ((u64)i << idx_lsb);
			*addr |= ((u64)v << iid_lsb);
			return 0;
		}

	return -ENODEV;
}

/* For VQDR, we may not modify the Channel bits, which might overlap
 *  with the Index bit. When it does, we need to ensure that isld0 == isld1.
 */
static int nfp_encode_basic(u64 *addr, int dest_island, int cpp_tgt,
			    int mode, bool addr40, int isld1, int isld0)
{
	int iid_lsb, idx_lsb;
	int isld[2];
	u64 v64;

	isld[0] = isld0;
	isld[1] = isld1;

	/* This function doesn't handle MU or CTXBP */
	if (cpp_tgt == NFP_CPP_TARGET_MU || cpp_tgt == NFP_CPP_TARGET_CT_XPB)
		return -EINVAL;

	switch (mode) {
	case 0:
		if (cpp_tgt == NFP_CPP_TARGET_QDR && !addr40)
			/* In this specific mode we'd rather not modify
			 * the address but we can verify if the existing
			 * contents will point to a valid island.
			 */
			return nfp_encode_basic_qdr(*addr, cpp_tgt, dest_island,
						    mode, addr40, isld1, isld0);

		iid_lsb = addr40 ? 34 : 26;
		/* <39:34> or <31:26> */
		v64 = GENMASK_ULL(iid_lsb + 5, iid_lsb);
		*addr &= ~v64;
		*addr |= ((u64)dest_island << iid_lsb) & v64;
		return 0;
	case 1:
		if (cpp_tgt == NFP_CPP_TARGET_QDR && !addr40)
			return nfp_encode_basic_qdr(*addr, cpp_tgt, dest_island,
						    mode, addr40, isld1, isld0);

		idx_lsb = addr40 ? 39 : 31;
		if (dest_island == isld0) {
			/* Only need to clear the Index bit */
			*addr &= ~BIT_ULL(idx_lsb);
			return 0;
		}

		if (dest_island == isld1) {
			/* Only need to set the Index bit */
			*addr |= BIT_ULL(idx_lsb);
			return 0;
		}

		return -ENODEV;
	case 2:
		/* iid<0> = addr<30> = channel<0>
		 * channel<1> = addr<31> = Index
		 */
		if (cpp_tgt == NFP_CPP_TARGET_QDR && !addr40)
			/* Special case where we allow channel bits to
			 * be set before hand and with them select an island.
			 * So we need to confirm that it's at least plausible.
			 */
			return nfp_encode_basic_qdr(*addr, cpp_tgt, dest_island,
						    mode, addr40, isld1, isld0);

		/* Make sure we compare against isldN values
		 * by clearing the LSB.
		 * This is what the silicon does.
		 */
		isld[0] &= ~1;
		isld[1] &= ~1;

		idx_lsb = addr40 ? 39 : 31;
		iid_lsb = idx_lsb - 1;

		return nfp_encode_basic_search(addr, dest_island, isld,
					       iid_lsb, idx_lsb, 2);
	case 3:
		if (cpp_tgt == NFP_CPP_TARGET_QDR && !addr40)
			/* iid<0> = addr<29> = data
			 * iid<1> = addr<30> = channel<0>
			 * channel<1> = addr<31> = Index
			 */
			return nfp_encode_basic_qdr(*addr, cpp_tgt, dest_island,
						    mode, addr40, isld1, isld0);

		isld[0] &= ~3;
		isld[1] &= ~3;

		idx_lsb = addr40 ? 39 : 31;
		iid_lsb = idx_lsb - 2;

		return nfp_encode_basic_search(addr, dest_island, isld,
					       iid_lsb, idx_lsb, 4);
	default:
		return -EINVAL;
	}
}

static int nfp_encode_mu(u64 *addr, int dest_island, int mode,
			 bool addr40, int isld1, int isld0)
{
	int iid_lsb, idx_lsb, locality_lsb;
	int isld[2];
	u64 v64;
	int da;

	isld[0] = isld0;
	isld[1] = isld1;
	locality_lsb = nfp_cppat_mu_locality_lsb(mode, addr40);

	if (((*addr >> locality_lsb) & 3) == _NIC_NFP6000_MU_LOCALITY_DIRECT)
		da = 1;
	else
		da = 0;

	switch (mode) {
	case 0:
		iid_lsb = addr40 ? 32 : 24;
		v64 = GENMASK_ULL(iid_lsb + 5, iid_lsb);
		*addr &= ~v64;
		*addr |= (((u64)dest_island) << iid_lsb) & v64;
		return 0;
	case 1:
		if (da) {
			iid_lsb = addr40 ? 32 : 24;
			v64 = GENMASK_ULL(iid_lsb + 5, iid_lsb);
			*addr &= ~v64;
			*addr |= (((u64)dest_island) << iid_lsb) & v64;
			return 0;
		}

		idx_lsb = addr40 ? 37 : 29;
		if (dest_island == isld0) {
			*addr &= ~BIT_ULL(idx_lsb);
			return 0;
		}

		if (dest_island == isld1) {
			*addr |= BIT_ULL(idx_lsb);
			return 0;
		}

		return -ENODEV;
	case 2:
		if (da) {
			iid_lsb = addr40 ? 32 : 24;
			v64 = GENMASK_ULL(iid_lsb + 5, iid_lsb);
			*addr &= ~v64;
			*addr |= (((u64)dest_island) << iid_lsb) & v64;
			return 0;
		}

		/* Make sure we compare against isldN values
		 * by clearing the LSB.
		 * This is what the silicon does.
		 */
		isld[0] &= ~1;
		isld[1] &= ~1;

		idx_lsb = addr40 ? 37 : 29;
		iid_lsb = idx_lsb - 1;

		return nfp_encode_basic_search(addr, dest_island, isld,
					       iid_lsb, idx_lsb, 2);
	case 3:
		/* Only the EMU will use 40 bit addressing. Silently
		 * set the direct locality bit for everyone else.
		 * The SDK toolchain uses dest_island <= 0 to test
		 * for atypical address encodings to support access
		 * to local-island CTM with a 32-but address (high-locality
		 * is effewctively ignored and just used for
		 * routing to island #0).
		 */
		if (dest_island > 0 && (dest_island < 24 || dest_island > 26)) {
			*addr |= ((u64)_NIC_NFP6000_MU_LOCALITY_DIRECT)
							<< locality_lsb;
			da = 1;
		}

		if (da) {
			iid_lsb = addr40 ? 32 : 24;
			v64 = GENMASK_ULL(iid_lsb + 5, iid_lsb);
			*addr &= ~v64;
			*addr |= (((u64)dest_island) << iid_lsb) & v64;
			return 0;
		}

		isld[0] &= ~3;
		isld[1] &= ~3;

		idx_lsb = addr40 ? 37 : 29;
		iid_lsb = idx_lsb - 2;

		return nfp_encode_basic_search(addr, dest_island, isld,
					       iid_lsb, idx_lsb, 4);
	default:
		return -EINVAL;
	}
}

static int nfp_cppat_addr_encode(u64 *addr, int dest_island, int cpp_tgt,
				 int mode, bool addr40, int isld1, int isld0)
{
	switch (cpp_tgt) {
	case NFP_CPP_TARGET_NBI:
	case NFP_CPP_TARGET_QDR:
	case NFP_CPP_TARGET_ILA:
	case NFP_CPP_TARGET_PCIE:
	case NFP_CPP_TARGET_ARM:
	case NFP_CPP_TARGET_CRYPTO:
	case NFP_CPP_TARGET_CLS:
		return nfp_encode_basic(addr, dest_island, cpp_tgt, mode,
					addr40, isld1, isld0);

	case NFP_CPP_TARGET_MU:
		return nfp_encode_mu(addr, dest_island, mode,
				     addr40, isld1, isld0);

	case NFP_CPP_TARGET_CT_XPB:
		if (mode != 1 || addr40)
			return -EINVAL;
		*addr &= ~GENMASK_ULL(29, 24);
		*addr |= ((u64)dest_island << 24) & GENMASK_ULL(29, 24);
		return 0;
	default:
		return -EINVAL;
	}
}

int nfp_target_cpp(u32 cpp_island_id, u64 cpp_island_address,
		   u32 *cpp_target_id, u64 *cpp_target_address,
		   const u32 *imb_table)
{
	const int island = NFP_CPP_ID_ISLAND_of(cpp_island_id);
	const int target = NFP_CPP_ID_TARGET_of(cpp_island_id);
	u32 imb;
	int err;

	if (target < 0 || target >= 16)
		return -EINVAL;

	if (island == 0) {
		/* Already translated */
		*cpp_target_id = cpp_island_id;
		*cpp_target_address = cpp_island_address;
		return 0;
	}

	/* CPP + Island only allowed on systems with IMB tables */
	if (!imb_table)
		return -EINVAL;

	imb = imb_table[target];

	*cpp_target_address = cpp_island_address;
	err = nfp_cppat_addr_encode(cpp_target_address, island, target,
				    ((imb >> 13) & 7), ((imb >> 12) & 1),
				    ((imb >> 6)  & 0x3f), ((imb >> 0)  & 0x3f));
	if (err)
		return err;

	*cpp_target_id = NFP_CPP_ID(target,
				    NFP_CPP_ID_ACTION_of(cpp_island_id),
				    NFP_CPP_ID_TOKEN_of(cpp_island_id));

	return 0;
}
