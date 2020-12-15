// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * CCS static data binary parser library
 *
 * Copyright 2019--2020 Intel Corporation
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "ccs-data-defs.h"

struct bin_container {
	void *base;
	void *now;
	void *end;
	size_t size;
};

static void *bin_alloc(struct bin_container *bin, size_t len)
{
	void *ptr;

	len = ALIGN(len, 8);

	if (bin->end - bin->now < len)
		return NULL;

	ptr = bin->now;
	bin->now += len;

	return ptr;
}

static void bin_reserve(struct bin_container *bin, size_t len)
{
	bin->size += ALIGN(len, 8);
}

static int bin_backing_alloc(struct bin_container *bin)
{
	bin->base = bin->now = kvzalloc(bin->size, GFP_KERNEL);
	if (!bin->base)
		return -ENOMEM;

	bin->end = bin->base + bin->size;

	return 0;
}

#define is_contained(var, endp)				\
	(sizeof(*var) <= (endp) - (void *)(var))
#define has_headroom(ptr, headroom, endp)	\
	((headroom) <= (endp) - (void *)(ptr))
#define is_contained_with_headroom(var, headroom, endp)		\
	(sizeof(*var) + (headroom) <= (endp) - (void *)(var))

static int
ccs_data_parse_length_specifier(const struct __ccs_data_length_specifier *__len,
				size_t *__hlen, size_t *__plen,
				const void *endp)
{
	size_t hlen, plen;

	if (!is_contained(__len, endp))
		return -ENODATA;

	switch (__len->length >> CCS_DATA_LENGTH_SPECIFIER_SIZE_SHIFT) {
	case CCS_DATA_LENGTH_SPECIFIER_1:
		hlen = sizeof(*__len);
		plen = __len->length &
			((1 << CCS_DATA_LENGTH_SPECIFIER_SIZE_SHIFT) - 1);
		break;
	case CCS_DATA_LENGTH_SPECIFIER_2: {
		struct __ccs_data_length_specifier2 *__len2 = (void *)__len;

		if (!is_contained(__len2, endp))
			return -ENODATA;

		hlen = sizeof(*__len2);
		plen = ((size_t)
			(__len2->length[0] &
			 ((1 << CCS_DATA_LENGTH_SPECIFIER_SIZE_SHIFT) - 1))
			<< 8) + __len2->length[1];
		break;
	}
	case CCS_DATA_LENGTH_SPECIFIER_3: {
		struct __ccs_data_length_specifier3 *__len3 = (void *)__len;

		if (!is_contained(__len3, endp))
			return -ENODATA;

		hlen = sizeof(*__len3);
		plen = ((size_t)
			(__len3->length[0] &
			 ((1 << CCS_DATA_LENGTH_SPECIFIER_SIZE_SHIFT) - 1))
			<< 16) + (__len3->length[0] << 8) + __len3->length[1];
		break;
	}
	default:
		return -EINVAL;
	}

	if (!has_headroom(__len, hlen + plen, endp))
		return -ENODATA;

	*__hlen = hlen;
	*__plen = plen;

	return 0;
}

static u8
ccs_data_parse_format_version(const struct __ccs_data_block *block)
{
	return block->id >> CCS_DATA_BLOCK_HEADER_ID_VERSION_SHIFT;
}

static u8 ccs_data_parse_block_id(const struct __ccs_data_block *block,
				       bool is_first)
{
	if (!is_first)
		return block->id;

	return block->id & ((1 << CCS_DATA_BLOCK_HEADER_ID_VERSION_SHIFT) - 1);
}

static int ccs_data_parse_version(struct bin_container *bin,
				  struct ccs_data_container *ccsdata,
				  const void *payload, const void *endp)
{
	const struct __ccs_data_block_version *v = payload;
	struct ccs_data_block_version *vv;

	if (v + 1 != endp)
		return -ENODATA;

	if (!bin->base) {
		bin_reserve(bin, sizeof(*ccsdata->version));
		return 0;
	}

	ccsdata->version = bin_alloc(bin, sizeof(*ccsdata->version));
	if (!ccsdata->version)
		return -ENOMEM;

	vv = ccsdata->version;
	vv->version_major = ((u16)v->static_data_version_major[0] << 8) +
		v->static_data_version_major[1];
	vv->version_minor = ((u16)v->static_data_version_minor[0] << 8) +
		v->static_data_version_minor[1];
	vv->date_year =  ((u16)v->year[0] << 8) + v->year[1];
	vv->date_month = v->month;
	vv->date_day = v->day;

	return 0;
}

static void print_ccs_data_version(struct device *dev,
				   struct ccs_data_block_version *v)
{
	dev_dbg(dev,
		"static data version %4.4x.%4.4x, date %4.4u-%2.2u-%2.2u\n",
		v->version_major, v->version_minor,
		v->date_year, v->date_month, v->date_day);
}

static int ccs_data_block_parse_header(const struct __ccs_data_block *block,
				       bool is_first, unsigned int *__block_id,
				       const void **payload,
				       const struct __ccs_data_block **next_block,
				       const void *endp, struct device *dev,
				       bool verbose)
{
	size_t plen, hlen;
	u8 block_id;
	int rval;

	if (!is_contained(block, endp))
		return -ENODATA;

	rval = ccs_data_parse_length_specifier(&block->length, &hlen, &plen,
					       endp);
	if (rval < 0)
		return rval;

	block_id = ccs_data_parse_block_id(block, is_first);

	if (verbose)
		dev_dbg(dev,
			"Block ID 0x%2.2x, header length %zu, payload length %zu\n",
			block_id, hlen, plen);

	if (!has_headroom(&block->length, hlen + plen, endp))
		return -ENODATA;

	if (__block_id)
		*__block_id = block_id;

	if (payload)
		*payload = (void *)&block->length + hlen;

	if (next_block)
		*next_block = (void *)&block->length + hlen + plen;

	return 0;
}

static int ccs_data_parse_regs(struct bin_container *bin,
			       struct ccs_reg **__regs,
			       size_t *__num_regs, const void *payload,
			       const void *endp, struct device *dev)
{
	struct ccs_reg *regs_base, *regs;
	size_t num_regs = 0;
	u16 addr = 0;

	if (bin->base && __regs) {
		regs = regs_base = bin_alloc(bin, sizeof(*regs) * *__num_regs);
		if (!regs)
			return -ENOMEM;
	}

	while (payload < endp && num_regs < INT_MAX) {
		const struct __ccs_data_block_regs *r = payload;
		size_t len;
		const void *data;

		if (!is_contained(r, endp))
			return -ENODATA;

		switch (r->reg_len >> CCS_DATA_BLOCK_REGS_SEL_SHIFT) {
		case CCS_DATA_BLOCK_REGS_SEL_REGS:
			addr += r->reg_len & CCS_DATA_BLOCK_REGS_ADDR_MASK;
			len = ((r->reg_len & CCS_DATA_BLOCK_REGS_LEN_MASK)
			       >> CCS_DATA_BLOCK_REGS_LEN_SHIFT) + 1;

			if (!is_contained_with_headroom(r, len, endp))
				return -ENODATA;

			data = r + 1;
			break;
		case CCS_DATA_BLOCK_REGS_SEL_REGS2: {
			const struct __ccs_data_block_regs2 *r2 = payload;

			if (!is_contained(r2, endp))
				return -ENODATA;

			addr += ((u16)(r2->reg_len &
				       CCS_DATA_BLOCK_REGS_2_ADDR_MASK) << 8)
				+ r2->addr;
			len = ((r2->reg_len & CCS_DATA_BLOCK_REGS_2_LEN_MASK)
			       >> CCS_DATA_BLOCK_REGS_2_LEN_SHIFT) + 1;

			if (!is_contained_with_headroom(r2, len, endp))
				return -ENODATA;

			data = r2 + 1;
			break;
		}
		case CCS_DATA_BLOCK_REGS_SEL_REGS3: {
			const struct __ccs_data_block_regs3 *r3 = payload;

			if (!is_contained(r3, endp))
				return -ENODATA;

			addr = ((u16)r3->addr[0] << 8) + r3->addr[1];
			len = (r3->reg_len & CCS_DATA_BLOCK_REGS_3_LEN_MASK) + 1;

			if (!is_contained_with_headroom(r3, len, endp))
				return -ENODATA;

			data = r3 + 1;
			break;
		}
		default:
			return -EINVAL;
		}

		num_regs++;

		if (!bin->base) {
			bin_reserve(bin, len);
		} else if (__regs) {
			regs->addr = addr;
			regs->len = len;
			regs->value = bin_alloc(bin, len);
			if (!regs->value)
				return -ENOMEM;

			memcpy(regs->value, data, len);
			regs++;
		}

		addr += len;
		payload = data + len;
	}

	if (!bin->base)
		bin_reserve(bin, sizeof(*regs) * num_regs);

	if (__num_regs)
		*__num_regs = num_regs;

	if (bin->base && __regs)
		*__regs = regs_base;

	return 0;
}

static int ccs_data_parse_reg_rules(struct bin_container *bin,
				    struct ccs_reg **__regs,
				    size_t *__num_regs,
				    const void *payload,
				    const void *endp, struct device *dev)
{
	int rval;

	if (!bin->base)
		return ccs_data_parse_regs(bin, NULL, NULL, payload, endp, dev);

	rval = ccs_data_parse_regs(bin, NULL, __num_regs, payload, endp, dev);
	if (rval)
		return rval;

	return ccs_data_parse_regs(bin, __regs, __num_regs, payload, endp,
				   dev);
}

static void assign_ffd_entry(struct ccs_frame_format_desc *desc,
			     const struct __ccs_data_block_ffd_entry *ent)
{
	desc->pixelcode = ent->pixelcode;
	desc->value = ((u16)ent->value[0] << 8) + ent->value[1];
}

static int ccs_data_parse_ffd(struct bin_container *bin,
			      struct ccs_frame_format_descs **ffd,
			      const void *payload,
			      const void *endp, struct device *dev)
{
	const struct __ccs_data_block_ffd *__ffd = payload;
	const struct __ccs_data_block_ffd_entry *__entry;
	unsigned int i;

	if (!is_contained(__ffd, endp))
		return -ENODATA;

	if ((void *)__ffd + sizeof(*__ffd) +
	    ((u32)__ffd->num_column_descs +
	     (u32)__ffd->num_row_descs) *
	    sizeof(struct __ccs_data_block_ffd_entry) != endp)
		return -ENODATA;

	if (!bin->base) {
		bin_reserve(bin, sizeof(**ffd));
		bin_reserve(bin, __ffd->num_column_descs *
			    sizeof(struct ccs_frame_format_desc));
		bin_reserve(bin, __ffd->num_row_descs *
			    sizeof(struct ccs_frame_format_desc));

		return 0;
	}

	*ffd = bin_alloc(bin, sizeof(**ffd));
	if (!*ffd)
		return -ENOMEM;

	(*ffd)->num_column_descs = __ffd->num_column_descs;
	(*ffd)->num_row_descs = __ffd->num_row_descs;
	__entry = (void *)(__ffd + 1);

	(*ffd)->column_descs = bin_alloc(bin, __ffd->num_column_descs *
					 sizeof(*(*ffd)->column_descs));
	if (!(*ffd)->column_descs)
		return -ENOMEM;

	for (i = 0; i < __ffd->num_column_descs; i++, __entry++)
		assign_ffd_entry(&(*ffd)->column_descs[i], __entry);

	(*ffd)->row_descs = bin_alloc(bin, __ffd->num_row_descs *
				      sizeof(*(*ffd)->row_descs));
	if (!(*ffd)->row_descs)
		return -ENOMEM;

	for (i = 0; i < __ffd->num_row_descs; i++, __entry++)
		assign_ffd_entry(&(*ffd)->row_descs[i], __entry);

	if (__entry != endp)
		return -EPROTO;

	return 0;
}

static int ccs_data_parse_pdaf_readout(struct bin_container *bin,
				       struct ccs_pdaf_readout **pdaf_readout,
				       const void *payload,
				       const void *endp, struct device *dev)
{
	const struct __ccs_data_block_pdaf_readout *__pdaf = payload;

	if (!is_contained(__pdaf, endp))
		return -ENODATA;

	if (!bin->base) {
		bin_reserve(bin, sizeof(**pdaf_readout));
	} else {
		*pdaf_readout = bin_alloc(bin, sizeof(**pdaf_readout));
		if (!*pdaf_readout)
			return -ENOMEM;

		(*pdaf_readout)->pdaf_readout_info_order =
			__pdaf->pdaf_readout_info_order;
	}

	return ccs_data_parse_ffd(bin, !bin->base ? NULL : &(*pdaf_readout)->ffd,
				  __pdaf + 1, endp, dev);
}

static int ccs_data_parse_rules(struct bin_container *bin,
				struct ccs_rule **__rules,
				size_t *__num_rules, const void *payload,
				const void *endp, struct device *dev)
{
	struct ccs_rule *rules_base, *rules = NULL, *next_rule;
	size_t num_rules = 0;
	const void *__next_rule = payload;
	int rval;

	if (bin->base) {
		rules_base = next_rule =
			bin_alloc(bin, sizeof(*rules) * *__num_rules);
		if (!rules_base)
			return -ENOMEM;
	}

	while (__next_rule < endp) {
		size_t rule_hlen, rule_plen, rule_plen2;
		const u8 *__rule_type;
		const void *rule_payload;

		/* Size of a single rule */
		rval = ccs_data_parse_length_specifier(__next_rule, &rule_hlen,
						       &rule_plen, endp);

		if (rval < 0)
			return rval;

		__rule_type = __next_rule + rule_hlen;

		if (!is_contained(__rule_type, endp))
			return -ENODATA;

		rule_payload = __rule_type + 1;
		rule_plen2 = rule_plen - sizeof(*__rule_type);

		switch (*__rule_type) {
		case CCS_DATA_BLOCK_RULE_ID_IF: {
			const struct __ccs_data_block_rule_if *__if_rules =
				rule_payload;
			const size_t __num_if_rules =
				rule_plen2 / sizeof(*__if_rules);
			struct ccs_if_rule *if_rule;

			if (!has_headroom(__if_rules,
					  sizeof(*__if_rules) * __num_if_rules,
					  rule_payload + rule_plen2))
				return -ENODATA;

			/* Also check there is no extra data */
			if (__if_rules + __num_if_rules !=
			    rule_payload + rule_plen2)
				return -EINVAL;

			if (!bin->base) {
				bin_reserve(bin,
					    sizeof(*if_rule) *
					    __num_if_rules);
				num_rules++;
			} else {
				unsigned int i;

				rules = next_rule;
				next_rule++;

				if_rule = bin_alloc(bin,
						    sizeof(*if_rule) *
						    __num_if_rules);
				if (!if_rule)
					return -ENOMEM;

				for (i = 0; i < __num_if_rules; i++) {
					if_rule[i].addr =
						((u16)__if_rules[i].addr[0]
						 << 8) +
						__if_rules[i].addr[1];
					if_rule[i].value = __if_rules[i].value;
					if_rule[i].mask = __if_rules[i].mask;
				}

				rules->if_rules = if_rule;
				rules->num_if_rules = __num_if_rules;
			}
			break;
		}
		case CCS_DATA_BLOCK_RULE_ID_READ_ONLY_REGS:
			rval = ccs_data_parse_reg_rules(bin, &rules->read_only_regs,
							&rules->num_read_only_regs,
							rule_payload,
							rule_payload + rule_plen2,
							dev);
			if (rval)
				return rval;
			break;
		case CCS_DATA_BLOCK_RULE_ID_FFD:
			rval = ccs_data_parse_ffd(bin, &rules->frame_format,
						  rule_payload,
						  rule_payload + rule_plen2,
						  dev);
			if (rval)
				return rval;
			break;
		case CCS_DATA_BLOCK_RULE_ID_MSR:
			rval = ccs_data_parse_reg_rules(bin,
							&rules->manufacturer_regs,
							&rules->num_manufacturer_regs,
							rule_payload,
							rule_payload + rule_plen2,
							dev);
			if (rval)
				return rval;
			break;
		case CCS_DATA_BLOCK_RULE_ID_PDAF_READOUT:
			rval = ccs_data_parse_pdaf_readout(bin,
							   &rules->pdaf_readout,
							   rule_payload,
							   rule_payload + rule_plen2,
							   dev);
			if (rval)
				return rval;
			break;
		default:
			dev_dbg(dev,
				"Don't know how to handle rule type %u!\n",
				*__rule_type);
			return -EINVAL;
		}
		__next_rule = __next_rule + rule_hlen + rule_plen;
	}

	if (!bin->base) {
		bin_reserve(bin, sizeof(*rules) * num_rules);
		*__num_rules = num_rules;
	} else {
		*__rules = rules_base;
	}

	return 0;
}

static int ccs_data_parse_pdaf(struct bin_container *bin, struct ccs_pdaf_pix_loc **pdaf,
			       const void *payload, const void *endp,
			       struct device *dev)
{
	const struct __ccs_data_block_pdaf_pix_loc *__pdaf = payload;
	const struct __ccs_data_block_pdaf_pix_loc_block_desc_group *__bdesc_group;
	const struct __ccs_data_block_pdaf_pix_loc_pixel_desc *__pixel_desc;
	unsigned int i;
	u16 num_block_desc_groups;
	u8 max_block_type_id = 0;
	const u8 *__num_pixel_descs;

	if (!is_contained(__pdaf, endp))
		return -ENODATA;

	if (bin->base) {
		*pdaf = bin_alloc(bin, sizeof(**pdaf));
		if (!*pdaf)
			return -ENOMEM;
	} else {
		bin_reserve(bin, sizeof(**pdaf));
	}

	num_block_desc_groups =
		((u16)__pdaf->num_block_desc_groups[0] << 8) +
		__pdaf->num_block_desc_groups[1];

	if (bin->base) {
		(*pdaf)->main_offset_x =
			((u16)__pdaf->main_offset_x[0] << 8) +
			__pdaf->main_offset_x[1];
		(*pdaf)->main_offset_y =
			((u16)__pdaf->main_offset_y[0] << 8) +
			__pdaf->main_offset_y[1];
		(*pdaf)->global_pdaf_type = __pdaf->global_pdaf_type;
		(*pdaf)->block_width = __pdaf->block_width;
		(*pdaf)->block_height = __pdaf->block_height;
		(*pdaf)->num_block_desc_groups = num_block_desc_groups;
	}

	__bdesc_group = (const void *)(__pdaf + 1);

	if (bin->base) {
		(*pdaf)->block_desc_groups =
			bin_alloc(bin,
				  sizeof(struct ccs_pdaf_pix_loc_block_desc_group) *
				  num_block_desc_groups);
		if (!(*pdaf)->block_desc_groups)
			return -ENOMEM;
	} else {
		bin_reserve(bin, sizeof(struct ccs_pdaf_pix_loc_block_desc_group) *
			    num_block_desc_groups);
	}

	for (i = 0; i < num_block_desc_groups; i++) {
		const struct __ccs_data_block_pdaf_pix_loc_block_desc *__bdesc;
		u16 num_block_descs;
		unsigned int j;

		if (!is_contained(__bdesc_group, endp))
			return -ENODATA;

		num_block_descs =
			((u16)__bdesc_group->num_block_descs[0] << 8) +
			__bdesc_group->num_block_descs[1];

		if (bin->base) {
			(*pdaf)->block_desc_groups[i].repeat_y =
				__bdesc_group->repeat_y;
			(*pdaf)->block_desc_groups[i].num_block_descs =
				num_block_descs;
		}

		__bdesc = (const void *)(__bdesc_group + 1);

		if (bin->base) {
			(*pdaf)->block_desc_groups[i].block_descs =
				bin_alloc(bin,
					  sizeof(struct ccs_pdaf_pix_loc_block_desc) *
					  num_block_descs);
			if (!(*pdaf)->block_desc_groups[i].block_descs)
				return -ENOMEM;
		} else {
			bin_reserve(bin, sizeof(struct ccs_pdaf_pix_loc_block_desc) *
				    num_block_descs);
		}

		for (j = 0; j < num_block_descs; j++, __bdesc++) {
			struct ccs_pdaf_pix_loc_block_desc *bdesc;

			if (!is_contained(__bdesc, endp))
				return -ENODATA;

			if (max_block_type_id <= __bdesc->block_type_id)
				max_block_type_id = __bdesc->block_type_id + 1;

			if (!bin->base)
				continue;

			bdesc = &(*pdaf)->block_desc_groups[i].block_descs[j];

			bdesc->repeat_x = ((u16)__bdesc->repeat_x[0] << 8)
				+ __bdesc->repeat_x[1];

			if (__bdesc->block_type_id >= num_block_descs)
				return -EINVAL;

			bdesc->block_type_id = __bdesc->block_type_id;
		}

		__bdesc_group = (const void *)__bdesc;
	}

	__num_pixel_descs = (const void *)__bdesc_group;

	if (bin->base) {
		(*pdaf)->pixel_desc_groups =
			bin_alloc(bin,
				  sizeof(struct ccs_pdaf_pix_loc_pixel_desc_group) *
				  max_block_type_id);
		if (!(*pdaf)->pixel_desc_groups)
			return -ENOMEM;
		(*pdaf)->num_pixel_desc_grups = max_block_type_id;
	} else {
		bin_reserve(bin, sizeof(struct ccs_pdaf_pix_loc_pixel_desc_group) *
			    max_block_type_id);
	}

	for (i = 0; i < max_block_type_id; i++) {
		struct ccs_pdaf_pix_loc_pixel_desc_group *pdgroup;
		unsigned int j;

		if (!is_contained(__num_pixel_descs, endp))
			return -ENODATA;

		if (bin->base) {
			pdgroup = &(*pdaf)->pixel_desc_groups[i];
			pdgroup->descs =
				bin_alloc(bin,
					  sizeof(struct ccs_pdaf_pix_loc_pixel_desc) *
					  *__num_pixel_descs);
			if (!pdgroup->descs)
				return -ENOMEM;
			pdgroup->num_descs = *__num_pixel_descs;
		} else {
			bin_reserve(bin, sizeof(struct ccs_pdaf_pix_loc_pixel_desc) *
				    *__num_pixel_descs);
		}

		__pixel_desc = (const void *)(__num_pixel_descs + 1);

		for (j = 0; j < *__num_pixel_descs; j++, __pixel_desc++) {
			struct ccs_pdaf_pix_loc_pixel_desc *pdesc;

			if (!is_contained(__pixel_desc, endp))
				return -ENODATA;

			if (!bin->base)
				continue;

			pdesc = &pdgroup->descs[j];
			pdesc->pixel_type = __pixel_desc->pixel_type;
			pdesc->small_offset_x = __pixel_desc->small_offset_x;
			pdesc->small_offset_y = __pixel_desc->small_offset_y;
		}

		__num_pixel_descs = (const void *)(__pixel_desc + 1);
	}

	return 0;
}

static int ccs_data_parse_license(struct bin_container *bin,
				  char **__license,
				  size_t *__license_length,
				  const void *payload, const void *endp)
{
	size_t size = endp - payload;
	char *license;

	if (!bin->base) {
		bin_reserve(bin, size);
		return 0;
	}

	license = bin_alloc(bin, size);
	if (!license)
		return -ENOMEM;

	memcpy(license, payload, size);

	*__license = license;
	*__license_length = size;

	return 0;
}

static int ccs_data_parse_end(bool *end, const void *payload, const void *endp,
			      struct device *dev)
{
	const struct __ccs_data_block_end *__end = payload;

	if (__end + 1 != endp) {
		dev_dbg(dev, "Invalid end block length %u\n",
			(unsigned int)(endp - payload));
		return -ENODATA;
	}

	*end = true;

	return 0;
}

static int __ccs_data_parse(struct bin_container *bin,
			    struct ccs_data_container *ccsdata,
			    const void *data, size_t len, struct device *dev,
			    bool verbose)
{
	const struct __ccs_data_block *block = data;
	const struct __ccs_data_block *endp = data + len;
	unsigned int version;
	bool is_first = true;
	int rval;

	version = ccs_data_parse_format_version(block);
	if (version != CCS_STATIC_DATA_VERSION) {
		dev_dbg(dev, "Don't know how to handle version %u\n", version);
		return -EINVAL;
	}

	if (verbose)
		dev_dbg(dev, "Parsing CCS static data version %u\n", version);

	if (!bin->base)
		*ccsdata = (struct ccs_data_container){ 0 };

	while (block < endp) {
		const struct __ccs_data_block *next_block;
		unsigned int block_id;
		const void *payload;

		rval = ccs_data_block_parse_header(block, is_first, &block_id,
						   &payload, &next_block, endp,
						   dev,
						   bin->base ? false : verbose);

		if (rval < 0)
			return rval;

		switch (block_id) {
		case CCS_DATA_BLOCK_ID_DUMMY:
			break;
		case CCS_DATA_BLOCK_ID_DATA_VERSION:
			rval = ccs_data_parse_version(bin, ccsdata, payload,
						      next_block);
			if (rval < 0)
				return rval;
			break;
		case CCS_DATA_BLOCK_ID_SENSOR_READ_ONLY_REGS:
			rval = ccs_data_parse_regs(
				bin, &ccsdata->sensor_read_only_regs,
				&ccsdata->num_sensor_read_only_regs, payload,
				next_block, dev);
			if (rval < 0)
				return rval;
			break;
		case CCS_DATA_BLOCK_ID_SENSOR_MANUFACTURER_REGS:
			rval = ccs_data_parse_regs(
				bin, &ccsdata->sensor_manufacturer_regs,
				&ccsdata->num_sensor_manufacturer_regs, payload,
				next_block, dev);
			if (rval < 0)
				return rval;
			break;
		case CCS_DATA_BLOCK_ID_MODULE_READ_ONLY_REGS:
			rval = ccs_data_parse_regs(
				bin, &ccsdata->module_read_only_regs,
				&ccsdata->num_module_read_only_regs, payload,
				next_block, dev);
			if (rval < 0)
				return rval;
			break;
		case CCS_DATA_BLOCK_ID_MODULE_MANUFACTURER_REGS:
			rval = ccs_data_parse_regs(
				bin, &ccsdata->module_manufacturer_regs,
				&ccsdata->num_module_manufacturer_regs, payload,
				next_block, dev);
			if (rval < 0)
				return rval;
			break;
		case CCS_DATA_BLOCK_ID_SENSOR_PDAF_PIXEL_LOCATION:
			rval = ccs_data_parse_pdaf(bin, &ccsdata->sensor_pdaf,
						   payload, next_block, dev);
			if (rval < 0)
				return rval;
			break;
		case CCS_DATA_BLOCK_ID_MODULE_PDAF_PIXEL_LOCATION:
			rval = ccs_data_parse_pdaf(bin, &ccsdata->module_pdaf,
						   payload, next_block, dev);
			if (rval < 0)
				return rval;
			break;
		case CCS_DATA_BLOCK_ID_SENSOR_RULE_BASED_BLOCK:
			rval = ccs_data_parse_rules(
				bin, &ccsdata->sensor_rules,
				&ccsdata->num_sensor_rules, payload, next_block,
				dev);
			if (rval < 0)
				return rval;
			break;
		case CCS_DATA_BLOCK_ID_MODULE_RULE_BASED_BLOCK:
			rval = ccs_data_parse_rules(
				bin, &ccsdata->module_rules,
				&ccsdata->num_module_rules, payload, next_block,
				dev);
			if (rval < 0)
				return rval;
			break;
		case CCS_DATA_BLOCK_ID_LICENSE:
			rval = ccs_data_parse_license(bin, &ccsdata->license,
						      &ccsdata->license_length,
						      payload, next_block);
			if (rval < 0)
				return rval;
			break;
		case CCS_DATA_BLOCK_ID_END:
			rval = ccs_data_parse_end(&ccsdata->end, payload,
						  next_block, dev);
			if (rval < 0)
				return rval;
			break;
		default:
			dev_dbg(dev, "WARNING: not handling block ID 0x%2.2x\n",
				block_id);
		}

		block = next_block;
		is_first = false;
	}

	return 0;
}

/**
 * ccs_data_parse - Parse a CCS static data file into a usable in-memory
 *		    data structure
 * @ccsdata:	CCS static data in-memory data structure
 * @data:	CCS static data binary
 * @len:	Length of @data
 * @dev:	Device the data is related to (used for printing debug messages)
 * @verbose:	Whether to be verbose or not
 */
int ccs_data_parse(struct ccs_data_container *ccsdata, const void *data,
		   size_t len, struct device *dev, bool verbose)
{
	struct bin_container bin = { 0 };
	int rval;

	rval = __ccs_data_parse(&bin, ccsdata, data, len, dev, verbose);
	if (rval)
		return rval;

	rval = bin_backing_alloc(&bin);
	if (rval)
		return rval;

	rval = __ccs_data_parse(&bin, ccsdata, data, len, dev, false);
	if (rval)
		goto out_free;

	if (verbose && ccsdata->version)
		print_ccs_data_version(dev, ccsdata->version);

	if (bin.now != bin.end) {
		rval = -EPROTO;
		dev_dbg(dev, "parsing mismatch; base %p; now %p; end %p\n",
			bin.base, bin.now, bin.end);
		goto out_free;
	}

	ccsdata->backing = bin.base;

	return 0;

out_free:
	kvfree(bin.base);

	return rval;
}
