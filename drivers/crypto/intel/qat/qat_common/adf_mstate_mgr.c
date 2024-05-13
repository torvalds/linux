// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024 Intel Corporation */

#include <linux/slab.h>
#include <linux/types.h>
#include "adf_mstate_mgr.h"

#define ADF_MSTATE_MAGIC	0xADF5CAEA
#define ADF_MSTATE_VERSION	0x1

struct adf_mstate_sect_h {
	u8 id[ADF_MSTATE_ID_LEN];
	u32 size;
	u32 sub_sects;
	u8 state[];
};

u32 adf_mstate_state_size(struct adf_mstate_mgr *mgr)
{
	return mgr->state - mgr->buf;
}

static inline u32 adf_mstate_avail_room(struct adf_mstate_mgr *mgr)
{
	return mgr->buf + mgr->size - mgr->state;
}

void adf_mstate_mgr_init(struct adf_mstate_mgr *mgr, u8 *buf, u32 size)
{
	mgr->buf = buf;
	mgr->state = buf;
	mgr->size = size;
	mgr->n_sects = 0;
};

struct adf_mstate_mgr *adf_mstate_mgr_new(u8 *buf, u32 size)
{
	struct adf_mstate_mgr *mgr;

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return NULL;

	adf_mstate_mgr_init(mgr, buf, size);

	return mgr;
}

void adf_mstate_mgr_destroy(struct adf_mstate_mgr *mgr)
{
	kfree(mgr);
}

void adf_mstate_mgr_init_from_parent(struct adf_mstate_mgr *mgr,
				     struct adf_mstate_mgr *p_mgr)
{
	adf_mstate_mgr_init(mgr, p_mgr->state,
			    p_mgr->size - adf_mstate_state_size(p_mgr));
}

void adf_mstate_mgr_init_from_psect(struct adf_mstate_mgr *mgr,
				    struct adf_mstate_sect_h *p_sect)
{
	adf_mstate_mgr_init(mgr, p_sect->state, p_sect->size);
	mgr->n_sects = p_sect->sub_sects;
}

static void adf_mstate_preamble_init(struct adf_mstate_preh *preamble)
{
	preamble->magic = ADF_MSTATE_MAGIC;
	preamble->version = ADF_MSTATE_VERSION;
	preamble->preh_len = sizeof(*preamble);
	preamble->size = 0;
	preamble->n_sects = 0;
}

/* default preambles checker */
static int adf_mstate_preamble_def_checker(struct adf_mstate_preh *preamble,
					   void *opaque)
{
	struct adf_mstate_mgr *mgr = opaque;

	if (preamble->magic != ADF_MSTATE_MAGIC ||
	    preamble->version > ADF_MSTATE_VERSION ||
	    preamble->preh_len > mgr->size) {
		pr_debug("QAT: LM - Invalid state (magic=%#x, version=%#x, hlen=%u), state_size=%u\n",
			 preamble->magic, preamble->version, preamble->preh_len,
			 mgr->size);
		return -EINVAL;
	}

	return 0;
}

struct adf_mstate_preh *adf_mstate_preamble_add(struct adf_mstate_mgr *mgr)
{
	struct adf_mstate_preh *pre = (struct adf_mstate_preh *)mgr->buf;

	if (adf_mstate_avail_room(mgr) < sizeof(*pre)) {
		pr_err("QAT: LM - Not enough space for preamble\n");
		return NULL;
	}

	adf_mstate_preamble_init(pre);
	mgr->state += pre->preh_len;

	return pre;
}

int adf_mstate_preamble_update(struct adf_mstate_mgr *mgr)
{
	struct adf_mstate_preh *preamble = (struct adf_mstate_preh *)mgr->buf;

	preamble->size = adf_mstate_state_size(mgr) - preamble->preh_len;
	preamble->n_sects = mgr->n_sects;

	return 0;
}

static void adf_mstate_dump_sect(struct adf_mstate_sect_h *sect,
				 const char *prefix)
{
	pr_debug("QAT: LM - %s QAT state section %s\n", prefix, sect->id);
	print_hex_dump_debug("h-", DUMP_PREFIX_OFFSET, 16, 2, sect,
			     sizeof(*sect), true);
	print_hex_dump_debug("s-", DUMP_PREFIX_OFFSET, 16, 2, sect->state,
			     sect->size, true);
}

static inline void __adf_mstate_sect_update(struct adf_mstate_mgr *mgr,
					    struct adf_mstate_sect_h *sect,
					    u32 size,
					    u32 n_subsects)
{
	sect->size += size;
	sect->sub_sects += n_subsects;
	mgr->n_sects++;
	mgr->state += sect->size;

	adf_mstate_dump_sect(sect, "Add");
}

void adf_mstate_sect_update(struct adf_mstate_mgr *p_mgr,
			    struct adf_mstate_mgr *curr_mgr,
			    struct adf_mstate_sect_h *sect)
{
	__adf_mstate_sect_update(p_mgr, sect, adf_mstate_state_size(curr_mgr),
				 curr_mgr->n_sects);
}

static struct adf_mstate_sect_h *adf_mstate_sect_add_header(struct adf_mstate_mgr *mgr,
							    const char *id)
{
	struct adf_mstate_sect_h *sect = (struct adf_mstate_sect_h *)(mgr->state);

	if (adf_mstate_avail_room(mgr) < sizeof(*sect)) {
		pr_debug("QAT: LM - Not enough space for header of QAT state sect %s\n", id);
		return NULL;
	}

	strscpy(sect->id, id, sizeof(sect->id));
	sect->size = 0;
	sect->sub_sects = 0;
	mgr->state += sizeof(*sect);

	return sect;
}

struct adf_mstate_sect_h *adf_mstate_sect_add_vreg(struct adf_mstate_mgr *mgr,
						   const char *id,
						   struct adf_mstate_vreginfo *info)
{
	struct adf_mstate_sect_h *sect;

	sect = adf_mstate_sect_add_header(mgr, id);
	if (!sect)
		return NULL;

	if (adf_mstate_avail_room(mgr) < info->size) {
		pr_debug("QAT: LM - Not enough space for QAT state sect %s, requires %u\n",
			 id, info->size);
		return NULL;
	}

	memcpy(sect->state, info->addr, info->size);
	__adf_mstate_sect_update(mgr, sect, info->size, 0);

	return sect;
}

struct adf_mstate_sect_h *adf_mstate_sect_add(struct adf_mstate_mgr *mgr,
					      const char *id,
					      adf_mstate_populate populate,
					      void *opaque)
{
	struct adf_mstate_mgr sub_sects_mgr;
	struct adf_mstate_sect_h *sect;
	int avail_room, size;

	sect = adf_mstate_sect_add_header(mgr, id);
	if (!sect)
		return NULL;

	if (!populate)
		return sect;

	avail_room = adf_mstate_avail_room(mgr);
	adf_mstate_mgr_init_from_parent(&sub_sects_mgr, mgr);

	size = (*populate)(&sub_sects_mgr, sect->state, avail_room, opaque);
	if (size < 0)
		return NULL;

	size += adf_mstate_state_size(&sub_sects_mgr);
	if (avail_room < size) {
		pr_debug("QAT: LM - Not enough space for QAT state sect %s, requires %u\n",
			 id, size);
		return NULL;
	}
	__adf_mstate_sect_update(mgr, sect, size, sub_sects_mgr.n_sects);

	return sect;
}

static int adf_mstate_sect_validate(struct adf_mstate_mgr *mgr)
{
	struct adf_mstate_sect_h *start = (struct adf_mstate_sect_h *)mgr->state;
	struct adf_mstate_sect_h *sect = start;
	u64 end;
	int i;

	end = (uintptr_t)mgr->buf + mgr->size;
	for (i = 0; i < mgr->n_sects; i++) {
		uintptr_t s_start = (uintptr_t)sect->state;
		uintptr_t s_end = s_start + sect->size;

		if (s_end < s_start || s_end > end) {
			pr_debug("QAT: LM - Corrupted state section (index=%u, size=%u) in state_mgr (size=%u, secs=%u)\n",
				 i, sect->size, mgr->size, mgr->n_sects);
			return -EINVAL;
		}
		sect = (struct adf_mstate_sect_h *)s_end;
	}

	pr_debug("QAT: LM - Scanned section (last child=%s, size=%lu) in state_mgr (size=%u, secs=%u)\n",
		 start->id, sizeof(struct adf_mstate_sect_h) * (ulong)(sect - start),
		 mgr->size, mgr->n_sects);

	return 0;
}

u32 adf_mstate_state_size_from_remote(struct adf_mstate_mgr *mgr)
{
	struct adf_mstate_preh *preh = (struct adf_mstate_preh *)mgr->buf;

	return preh->preh_len + preh->size;
}

int adf_mstate_mgr_init_from_remote(struct adf_mstate_mgr *mgr, u8 *buf, u32 size,
				    adf_mstate_preamble_checker pre_checker,
				    void *opaque)
{
	struct adf_mstate_preh *pre;
	int ret;

	adf_mstate_mgr_init(mgr, buf, size);
	pre = (struct adf_mstate_preh *)(mgr->buf);

	pr_debug("QAT: LM - Dump state preambles\n");
	print_hex_dump_debug("", DUMP_PREFIX_OFFSET, 16, 2, pre, pre->preh_len, 0);

	if (pre_checker)
		ret = (*pre_checker)(pre, opaque);
	else
		ret = adf_mstate_preamble_def_checker(pre, mgr);
	if (ret)
		return ret;

	mgr->state = mgr->buf + pre->preh_len;
	mgr->n_sects = pre->n_sects;

	return adf_mstate_sect_validate(mgr);
}

struct adf_mstate_sect_h *adf_mstate_sect_lookup(struct adf_mstate_mgr *mgr,
						 const char *id,
						 adf_mstate_action action,
						 void *opaque)
{
	struct adf_mstate_sect_h *sect = (struct adf_mstate_sect_h *)mgr->state;
	struct adf_mstate_mgr sub_sects_mgr;
	int i, ret;

	for (i = 0; i < mgr->n_sects; i++) {
		if (!strncmp(sect->id, id, sizeof(sect->id)))
			goto found;

		sect = (struct adf_mstate_sect_h *)(sect->state + sect->size);
	}

	return NULL;

found:
	adf_mstate_dump_sect(sect, "Found");

	adf_mstate_mgr_init_from_psect(&sub_sects_mgr, sect);
	if (sect->sub_sects && adf_mstate_sect_validate(&sub_sects_mgr))
		return NULL;

	if (!action)
		return sect;

	ret = (*action)(&sub_sects_mgr, sect->state, sect->size, opaque);
	if (ret)
		return NULL;

	return sect;
}
