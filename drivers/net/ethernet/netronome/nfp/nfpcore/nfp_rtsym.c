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
 * nfp_rtsym.c
 * Interface for accessing run-time symbol table
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Espen Skoglund <espen.skoglund@netronome.com>
 *          Francois H. Theron <francois.theron@netronome.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io-64-nonatomic-hi-lo.h>

#include "nfp.h"
#include "nfp_cpp.h"
#include "nfp_nffw.h"
#include "nfp6000/nfp6000.h"

/* These need to match the linker */
#define SYM_TGT_LMEM		0
#define SYM_TGT_EMU_CACHE	0x17

struct nfp_rtsym_entry {
	u8	type;
	u8	target;
	u8	island;
	u8	addr_hi;
	__le32	addr_lo;
	__le16	name;
	u8	menum;
	u8	size_hi;
	__le32	size_lo;
};

struct nfp_rtsym_table {
	struct nfp_cpp *cpp;
	int num;
	char *strtab;
	struct nfp_rtsym symtab[];
};

static int nfp_meid(u8 island_id, u8 menum)
{
	return (island_id & 0x3F) == island_id && menum < 12 ?
		(island_id << 4) | (menum + 4) : -1;
}

static void
nfp_rtsym_sw_entry_init(struct nfp_rtsym_table *cache, u32 strtab_size,
			struct nfp_rtsym *sw, struct nfp_rtsym_entry *fw)
{
	sw->type = fw->type;
	sw->name = cache->strtab + le16_to_cpu(fw->name) % strtab_size;
	sw->addr = ((u64)fw->addr_hi << 32) | le32_to_cpu(fw->addr_lo);
	sw->size = ((u64)fw->size_hi << 32) | le32_to_cpu(fw->size_lo);

	switch (fw->target) {
	case SYM_TGT_LMEM:
		sw->target = NFP_RTSYM_TARGET_LMEM;
		break;
	case SYM_TGT_EMU_CACHE:
		sw->target = NFP_RTSYM_TARGET_EMU_CACHE;
		break;
	default:
		sw->target = fw->target;
		break;
	}

	if (fw->menum != 0xff)
		sw->domain = nfp_meid(fw->island, fw->menum);
	else if (fw->island != 0xff)
		sw->domain = fw->island;
	else
		sw->domain = -1;
}

struct nfp_rtsym_table *nfp_rtsym_table_read(struct nfp_cpp *cpp)
{
	struct nfp_rtsym_table *rtbl;
	const struct nfp_mip *mip;

	mip = nfp_mip_open(cpp);
	rtbl = __nfp_rtsym_table_read(cpp, mip);
	nfp_mip_close(mip);

	return rtbl;
}

struct nfp_rtsym_table *
__nfp_rtsym_table_read(struct nfp_cpp *cpp, const struct nfp_mip *mip)
{
	const u32 dram = NFP_CPP_ID(NFP_CPP_TARGET_MU, NFP_CPP_ACTION_RW, 0) |
		NFP_ISL_EMEM0;
	u32 strtab_addr, symtab_addr, strtab_size, symtab_size;
	struct nfp_rtsym_entry *rtsymtab;
	struct nfp_rtsym_table *cache;
	int err, n, size;

	if (!mip)
		return NULL;

	nfp_mip_strtab(mip, &strtab_addr, &strtab_size);
	nfp_mip_symtab(mip, &symtab_addr, &symtab_size);

	if (!symtab_size || !strtab_size || symtab_size % sizeof(*rtsymtab))
		return NULL;

	/* Align to 64 bits */
	symtab_size = round_up(symtab_size, 8);
	strtab_size = round_up(strtab_size, 8);

	rtsymtab = kmalloc(symtab_size, GFP_KERNEL);
	if (!rtsymtab)
		return NULL;

	size = sizeof(*cache);
	size += symtab_size / sizeof(*rtsymtab) * sizeof(struct nfp_rtsym);
	size +=	strtab_size + 1;
	cache = kmalloc(size, GFP_KERNEL);
	if (!cache)
		goto exit_free_rtsym_raw;

	cache->cpp = cpp;
	cache->num = symtab_size / sizeof(*rtsymtab);
	cache->strtab = (void *)&cache->symtab[cache->num];

	err = nfp_cpp_read(cpp, dram, symtab_addr, rtsymtab, symtab_size);
	if (err != symtab_size)
		goto exit_free_cache;

	err = nfp_cpp_read(cpp, dram, strtab_addr, cache->strtab, strtab_size);
	if (err != strtab_size)
		goto exit_free_cache;
	cache->strtab[strtab_size] = '\0';

	for (n = 0; n < cache->num; n++)
		nfp_rtsym_sw_entry_init(cache, strtab_size,
					&cache->symtab[n], &rtsymtab[n]);

	kfree(rtsymtab);

	return cache;

exit_free_cache:
	kfree(cache);
exit_free_rtsym_raw:
	kfree(rtsymtab);
	return NULL;
}

/**
 * nfp_rtsym_count() - Get the number of RTSYM descriptors
 * @rtbl:	NFP RTsym table
 *
 * Return: Number of RTSYM descriptors
 */
int nfp_rtsym_count(struct nfp_rtsym_table *rtbl)
{
	if (!rtbl)
		return -EINVAL;
	return rtbl->num;
}

/**
 * nfp_rtsym_get() - Get the Nth RTSYM descriptor
 * @rtbl:	NFP RTsym table
 * @idx:	Index (0-based) of the RTSYM descriptor
 *
 * Return: const pointer to a struct nfp_rtsym descriptor, or NULL
 */
const struct nfp_rtsym *nfp_rtsym_get(struct nfp_rtsym_table *rtbl, int idx)
{
	if (!rtbl)
		return NULL;
	if (idx >= rtbl->num)
		return NULL;

	return &rtbl->symtab[idx];
}

/**
 * nfp_rtsym_lookup() - Return the RTSYM descriptor for a symbol name
 * @rtbl:	NFP RTsym table
 * @name:	Symbol name
 *
 * Return: const pointer to a struct nfp_rtsym descriptor, or NULL
 */
const struct nfp_rtsym *
nfp_rtsym_lookup(struct nfp_rtsym_table *rtbl, const char *name)
{
	int n;

	if (!rtbl)
		return NULL;

	for (n = 0; n < rtbl->num; n++)
		if (strcmp(name, rtbl->symtab[n].name) == 0)
			return &rtbl->symtab[n];

	return NULL;
}

/**
 * nfp_rtsym_read_le() - Read a simple unsigned scalar value from symbol
 * @rtbl:	NFP RTsym table
 * @name:	Symbol name
 * @error:	Poniter to error code (optional)
 *
 * Lookup a symbol, map, read it and return it's value. Value of the symbol
 * will be interpreted as a simple little-endian unsigned value. Symbol can
 * be 4 or 8 bytes in size.
 *
 * Return: value read, on error sets the error and returns ~0ULL.
 */
u64 nfp_rtsym_read_le(struct nfp_rtsym_table *rtbl, const char *name,
		      int *error)
{
	const struct nfp_rtsym *sym;
	u32 val32, id;
	u64 val;
	int err;

	sym = nfp_rtsym_lookup(rtbl, name);
	if (!sym) {
		err = -ENOENT;
		goto exit;
	}

	id = NFP_CPP_ISLAND_ID(sym->target, NFP_CPP_ACTION_RW, 0, sym->domain);

	switch (sym->size) {
	case 4:
		err = nfp_cpp_readl(rtbl->cpp, id, sym->addr, &val32);
		val = val32;
		break;
	case 8:
		err = nfp_cpp_readq(rtbl->cpp, id, sym->addr, &val);
		break;
	default:
		nfp_err(rtbl->cpp,
			"rtsym '%s' unsupported or non-scalar size: %lld\n",
			name, sym->size);
		err = -EINVAL;
		break;
	}

	if (err == sym->size)
		err = 0;
	else if (err >= 0)
		err = -EIO;
exit:
	if (error)
		*error = err;

	if (err)
		return ~0ULL;
	return val;
}
