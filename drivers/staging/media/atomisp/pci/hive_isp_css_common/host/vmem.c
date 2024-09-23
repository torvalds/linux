// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2016, Intel Corporation.
 */

#include "isp.h"
#include "vmem.h"
#include "vmem_local.h"

#if !defined(HRT_MEMORY_ACCESS)
#include "ia_css_device_access.h"
#endif
#include "assert_support.h"

typedef unsigned long long hive_uedge;
typedef hive_uedge *hive_wide;

/* Copied from SDK: sim_semantics.c */

/* subword bits move like this:         MSB[____xxxx____]LSB -> MSB[00000000xxxx]LSB */
static inline hive_uedge
subword(hive_uedge w, unsigned int start, unsigned int end)
{
	return (w & (((1ULL << (end - 1)) - 1) << 1 | 1)) >> start;
}

/* inverse subword bits move like this: MSB[xxxx____xxxx]LSB -> MSB[xxxx0000xxxx]LSB */
static inline hive_uedge
inv_subword(hive_uedge w, unsigned int start, unsigned int end)
{
	return w & (~(((1ULL << (end - 1)) - 1) << 1 | 1) | ((1ULL << start) - 1));
}

#define uedge_bits (8 * sizeof(hive_uedge))
#define move_lower_bits(target, target_bit, src, src_bit) move_subword(target, target_bit, src, 0, src_bit)
#define move_upper_bits(target, target_bit, src, src_bit) move_subword(target, target_bit, src, src_bit, uedge_bits)
#define move_word(target, target_bit, src) move_subword(target, target_bit, src, 0, uedge_bits)

static void
move_subword(
    hive_uedge *target,
    unsigned int target_bit,
    hive_uedge src,
    unsigned int src_start,
    unsigned int src_end)
{
	unsigned int start_elem = target_bit / uedge_bits;
	unsigned int start_bit  = target_bit % uedge_bits;
	unsigned int subword_width = src_end - src_start;

	hive_uedge src_subword = subword(src, src_start, src_end);

	if (subword_width + start_bit > uedge_bits) { /* overlap */
		hive_uedge old_val1;
		hive_uedge old_val0 = inv_subword(target[start_elem], start_bit, uedge_bits);

		target[start_elem] = old_val0 | (src_subword << start_bit);
		old_val1 = inv_subword(target[start_elem + 1], 0,
				       subword_width + start_bit - uedge_bits);
		target[start_elem + 1] = old_val1 | (src_subword >> (uedge_bits - start_bit));
	} else {
		hive_uedge old_val = inv_subword(target[start_elem], start_bit,
						 start_bit + subword_width);

		target[start_elem] = old_val | (src_subword << start_bit);
	}
}

static void
hive_sim_wide_unpack(
    hive_wide vector,
    hive_wide elem,
    hive_uint elem_bits,
    hive_uint index)
{
	/* pointers into wide_type: */
	unsigned int start_elem = (elem_bits * index) / uedge_bits;
	unsigned int start_bit  = (elem_bits * index) % uedge_bits;
	unsigned int end_elem   = (elem_bits * (index + 1) - 1) / uedge_bits;
	unsigned int end_bit    = ((elem_bits * (index + 1) - 1) % uedge_bits) + 1;

	if (elem_bits == uedge_bits) {
		/* easy case for speedup: */
		elem[0] = vector[index];
	} else if (start_elem == end_elem) {
		/* only one (<=64 bits) element needs to be (partly) copied: */
		move_subword(elem, 0, vector[start_elem], start_bit, end_bit);
	} else {
		/* general case: handles edge spanning cases (includes >64bit elements) */
		unsigned int bits_written = 0;
		unsigned int i;

		move_upper_bits(elem, bits_written, vector[start_elem], start_bit);
		bits_written += (64 - start_bit);
		for (i = start_elem + 1; i < end_elem; i++) {
			move_word(elem, bits_written, vector[i]);
			bits_written += uedge_bits;
		}
		move_lower_bits(elem, bits_written, vector[end_elem], end_bit);
	}
}

static void
hive_sim_wide_pack(
    hive_wide vector,
    hive_wide elem,
    hive_uint elem_bits,
    hive_uint index)
{
	/* pointers into wide_type: */
	unsigned int start_elem = (elem_bits * index) / uedge_bits;

	/* easy case for speedup: */
	if (elem_bits == uedge_bits) {
		vector[start_elem] = elem[0];
	} else if (elem_bits > uedge_bits) {
		unsigned int bits_to_write = elem_bits;
		unsigned int start_bit = elem_bits * index;
		unsigned int i = 0;

		for (; bits_to_write > uedge_bits;
		     bits_to_write -= uedge_bits, i++, start_bit += uedge_bits) {
			move_word(vector, start_bit, elem[i]);
		}
		move_lower_bits(vector, start_bit, elem[i], bits_to_write);
	} else {
		/* only one element needs to be (partly) copied: */
		move_lower_bits(vector, elem_bits * index, elem[0], elem_bits);
	}
}

static void load_vector(
    const isp_ID_t		ID,
    t_vmem_elem		*to,
    const t_vmem_elem	*from)
{
	unsigned int i;
	hive_uedge *data;
	unsigned int size = sizeof(short) * ISP_NWAY;

	VMEM_ARRAY(v, 2 * ISP_NWAY); /* Need 2 vectors to work around vmem hss bug */
	assert(ISP_BAMEM_BASE[ID] != (hrt_address) - 1);
#if !defined(HRT_MEMORY_ACCESS)
	ia_css_device_load(ISP_BAMEM_BASE[ID] + (unsigned long)from, &v[0][0], size);
#else
	hrt_master_port_load(ISP_BAMEM_BASE[ID] + (unsigned long)from, &v[0][0], size);
#endif
	data = (hive_uedge *)v;
	for (i = 0; i < ISP_NWAY; i++) {
		hive_uedge elem = 0;

		hive_sim_wide_unpack(data, &elem, ISP_VEC_ELEMBITS, i);
		to[i] = elem;
	}
	udelay(1); /* Spend at least 1 cycles per vector */
}

static void store_vector(
    const isp_ID_t		ID,
    t_vmem_elem		*to,
    const t_vmem_elem	*from)
{
	unsigned int i;
	unsigned int size = sizeof(short) * ISP_NWAY;

	VMEM_ARRAY(v, 2 * ISP_NWAY); /* Need 2 vectors to work around vmem hss bug */
	//load_vector (&v[1][0], &to[ISP_NWAY]); /* Fetch the next vector, since it will be overwritten. */
	hive_uedge *data = (hive_uedge *)v;

	for (i = 0; i < ISP_NWAY; i++) {
		hive_sim_wide_pack(data, (hive_wide)&from[i], ISP_VEC_ELEMBITS, i);
	}
	assert(ISP_BAMEM_BASE[ID] != (hrt_address) - 1);
#if !defined(HRT_MEMORY_ACCESS)
	ia_css_device_store(ISP_BAMEM_BASE[ID] + (unsigned long)to, &v, size);
#else
	//hrt_mem_store (ISP, VMEM, (unsigned)to, &v, siz); /* This will overwrite the next vector as well */
	hrt_master_port_store(ISP_BAMEM_BASE[ID] + (unsigned long)to, &v, size);
#endif
	udelay(1); /* Spend at least 1 cycles per vector */
}

void isp_vmem_load(
    const isp_ID_t		ID,
    const t_vmem_elem	*from,
    t_vmem_elem		*to,
    unsigned int elems) /* In t_vmem_elem */
{
	unsigned int c;
	const t_vmem_elem *vp = from;

	assert(ID < N_ISP_ID);
	assert((unsigned long)from % ISP_VEC_ALIGN == 0);
	assert(elems % ISP_NWAY == 0);
	for (c = 0; c < elems; c += ISP_NWAY) {
		load_vector(ID, &to[c], vp);
		vp = (t_vmem_elem *)((char *)vp + ISP_VEC_ALIGN);
	}
}

void isp_vmem_store(
    const isp_ID_t		ID,
    t_vmem_elem		*to,
    const t_vmem_elem	*from,
    unsigned int elems) /* In t_vmem_elem */
{
	unsigned int c;
	t_vmem_elem *vp = to;

	assert(ID < N_ISP_ID);
	assert((unsigned long)to % ISP_VEC_ALIGN == 0);
	assert(elems % ISP_NWAY == 0);
	for (c = 0; c < elems; c += ISP_NWAY) {
		store_vector(ID, vp, &from[c]);
		vp = (t_vmem_elem *)((char *)vp + ISP_VEC_ALIGN);
	}
}

void isp_vmem_2d_load(
    const isp_ID_t		ID,
    const t_vmem_elem	*from,
    t_vmem_elem		*to,
    unsigned int height,
    unsigned int width,
    unsigned int stride_to,  /* In t_vmem_elem */

    unsigned stride_from /* In t_vmem_elem */)
{
	unsigned int h;

	assert(ID < N_ISP_ID);
	assert((unsigned long)from % ISP_VEC_ALIGN == 0);
	assert(width % ISP_NWAY == 0);
	assert(stride_from % ISP_NWAY == 0);
	for (h = 0; h < height; h++) {
		unsigned int c;
		const t_vmem_elem *vp = from;

		for (c = 0; c < width; c += ISP_NWAY) {
			load_vector(ID, &to[stride_to * h + c], vp);
			vp = (t_vmem_elem *)((char *)vp + ISP_VEC_ALIGN);
		}
		from = (const t_vmem_elem *)((const char *)from + stride_from / ISP_NWAY *
					     ISP_VEC_ALIGN);
	}
}

void isp_vmem_2d_store(
    const isp_ID_t		ID,
    t_vmem_elem		*to,
    const t_vmem_elem	*from,
    unsigned int height,
    unsigned int width,
    unsigned int stride_to,  /* In t_vmem_elem */

    unsigned stride_from /* In t_vmem_elem */)
{
	unsigned int h;

	assert(ID < N_ISP_ID);
	assert((unsigned long)to % ISP_VEC_ALIGN == 0);
	assert(width % ISP_NWAY == 0);
	assert(stride_to % ISP_NWAY == 0);
	for (h = 0; h < height; h++) {
		unsigned int c;
		t_vmem_elem *vp = to;

		for (c = 0; c < width; c += ISP_NWAY) {
			store_vector(ID, vp, &from[stride_from * h + c]);
			vp = (t_vmem_elem *)((char *)vp + ISP_VEC_ALIGN);
		}
		to = (t_vmem_elem *)((char *)to + stride_to / ISP_NWAY * ISP_VEC_ALIGN);
	}
}
