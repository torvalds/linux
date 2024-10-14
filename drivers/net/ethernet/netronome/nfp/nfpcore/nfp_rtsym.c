// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

/*
 * nfp_rtsym.c
 * Interface for accessing run-time symbol table
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Espen Skoglund <espen.skoglund@netronome.com>
 *          Francois H. Theron <francois.theron@netronome.com>
 */

#include <linux/unaligned.h>
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

u64 nfp_rtsym_size(const struct nfp_rtsym *sym)
{
	switch (sym->type) {
	case NFP_RTSYM_TYPE_NONE:
		pr_err("rtsym '%s': type NONE\n", sym->name);
		return 0;
	default:
		pr_warn("rtsym '%s': unknown type: %d\n", sym->name, sym->type);
		fallthrough;
	case NFP_RTSYM_TYPE_OBJECT:
	case NFP_RTSYM_TYPE_FUNCTION:
		return sym->size;
	case NFP_RTSYM_TYPE_ABS:
		return sizeof(u64);
	}
}

static int
nfp_rtsym_to_dest(struct nfp_cpp *cpp, const struct nfp_rtsym *sym,
		  u8 action, u8 token, u64 off, u32 *cpp_id, u64 *addr)
{
	if (sym->type != NFP_RTSYM_TYPE_OBJECT) {
		nfp_err(cpp, "rtsym '%s': direct access to non-object rtsym\n",
			sym->name);
		return -EINVAL;
	}

	*addr = sym->addr + off;

	if (sym->target == NFP_RTSYM_TARGET_EMU_CACHE) {
		int locality_off = nfp_cpp_mu_locality_lsb(cpp);

		*addr &= ~(NFP_MU_ADDR_ACCESS_TYPE_MASK << locality_off);
		*addr |= NFP_MU_ADDR_ACCESS_TYPE_DIRECT << locality_off;

		*cpp_id = NFP_CPP_ISLAND_ID(NFP_CPP_TARGET_MU, action, token,
					    sym->domain);
	} else if (sym->target < 0) {
		nfp_err(cpp, "rtsym '%s': unhandled target encoding: %d\n",
			sym->name, sym->target);
		return -EINVAL;
	} else {
		*cpp_id = NFP_CPP_ISLAND_ID(sym->target, action, token,
					    sym->domain);
	}

	return 0;
}

int __nfp_rtsym_read(struct nfp_cpp *cpp, const struct nfp_rtsym *sym,
		     u8 action, u8 token, u64 off, void *buf, size_t len)
{
	u64 sym_size = nfp_rtsym_size(sym);
	u32 cpp_id;
	u64 addr;
	int err;

	if (off > sym_size) {
		nfp_err(cpp, "rtsym '%s': read out of bounds: off: %lld + len: %zd > size: %lld\n",
			sym->name, off, len, sym_size);
		return -ENXIO;
	}
	len = min_t(size_t, len, sym_size - off);

	if (sym->type == NFP_RTSYM_TYPE_ABS) {
		u8 tmp[8];

		put_unaligned_le64(sym->addr, tmp);
		memcpy(buf, &tmp[off], len);

		return len;
	}

	err = nfp_rtsym_to_dest(cpp, sym, action, token, off, &cpp_id, &addr);
	if (err)
		return err;

	return nfp_cpp_read(cpp, cpp_id, addr, buf, len);
}

int nfp_rtsym_read(struct nfp_cpp *cpp, const struct nfp_rtsym *sym, u64 off,
		   void *buf, size_t len)
{
	return __nfp_rtsym_read(cpp, sym, NFP_CPP_ACTION_RW, 0, off, buf, len);
}

int __nfp_rtsym_readl(struct nfp_cpp *cpp, const struct nfp_rtsym *sym,
		      u8 action, u8 token, u64 off, u32 *value)
{
	u32 cpp_id;
	u64 addr;
	int err;

	if (off + 4 > nfp_rtsym_size(sym)) {
		nfp_err(cpp, "rtsym '%s': readl out of bounds: off: %lld + 4 > size: %lld\n",
			sym->name, off, nfp_rtsym_size(sym));
		return -ENXIO;
	}

	err = nfp_rtsym_to_dest(cpp, sym, action, token, off, &cpp_id, &addr);
	if (err)
		return err;

	return nfp_cpp_readl(cpp, cpp_id, addr, value);
}

int nfp_rtsym_readl(struct nfp_cpp *cpp, const struct nfp_rtsym *sym, u64 off,
		    u32 *value)
{
	return __nfp_rtsym_readl(cpp, sym, NFP_CPP_ACTION_RW, 0, off, value);
}

int __nfp_rtsym_readq(struct nfp_cpp *cpp, const struct nfp_rtsym *sym,
		      u8 action, u8 token, u64 off, u64 *value)
{
	u32 cpp_id;
	u64 addr;
	int err;

	if (off + 8 > nfp_rtsym_size(sym)) {
		nfp_err(cpp, "rtsym '%s': readq out of bounds: off: %lld + 8 > size: %lld\n",
			sym->name, off, nfp_rtsym_size(sym));
		return -ENXIO;
	}

	if (sym->type == NFP_RTSYM_TYPE_ABS) {
		*value = sym->addr;
		return 0;
	}

	err = nfp_rtsym_to_dest(cpp, sym, action, token, off, &cpp_id, &addr);
	if (err)
		return err;

	return nfp_cpp_readq(cpp, cpp_id, addr, value);
}

int nfp_rtsym_readq(struct nfp_cpp *cpp, const struct nfp_rtsym *sym, u64 off,
		    u64 *value)
{
	return __nfp_rtsym_readq(cpp, sym, NFP_CPP_ACTION_RW, 0, off, value);
}

int __nfp_rtsym_write(struct nfp_cpp *cpp, const struct nfp_rtsym *sym,
		      u8 action, u8 token, u64 off, void *buf, size_t len)
{
	u64 sym_size = nfp_rtsym_size(sym);
	u32 cpp_id;
	u64 addr;
	int err;

	if (off > sym_size) {
		nfp_err(cpp, "rtsym '%s': write out of bounds: off: %lld + len: %zd > size: %lld\n",
			sym->name, off, len, sym_size);
		return -ENXIO;
	}
	len = min_t(size_t, len, sym_size - off);

	err = nfp_rtsym_to_dest(cpp, sym, action, token, off, &cpp_id, &addr);
	if (err)
		return err;

	return nfp_cpp_write(cpp, cpp_id, addr, buf, len);
}

int nfp_rtsym_write(struct nfp_cpp *cpp, const struct nfp_rtsym *sym, u64 off,
		    void *buf, size_t len)
{
	return __nfp_rtsym_write(cpp, sym, NFP_CPP_ACTION_RW, 0, off, buf, len);
}

int __nfp_rtsym_writel(struct nfp_cpp *cpp, const struct nfp_rtsym *sym,
		       u8 action, u8 token, u64 off, u32 value)
{
	u32 cpp_id;
	u64 addr;
	int err;

	if (off + 4 > nfp_rtsym_size(sym)) {
		nfp_err(cpp, "rtsym '%s': writel out of bounds: off: %lld + 4 > size: %lld\n",
			sym->name, off, nfp_rtsym_size(sym));
		return -ENXIO;
	}

	err = nfp_rtsym_to_dest(cpp, sym, action, token, off, &cpp_id, &addr);
	if (err)
		return err;

	return nfp_cpp_writel(cpp, cpp_id, addr, value);
}

int nfp_rtsym_writel(struct nfp_cpp *cpp, const struct nfp_rtsym *sym, u64 off,
		     u32 value)
{
	return __nfp_rtsym_writel(cpp, sym, NFP_CPP_ACTION_RW, 0, off, value);
}

int __nfp_rtsym_writeq(struct nfp_cpp *cpp, const struct nfp_rtsym *sym,
		       u8 action, u8 token, u64 off, u64 value)
{
	u32 cpp_id;
	u64 addr;
	int err;

	if (off + 8 > nfp_rtsym_size(sym)) {
		nfp_err(cpp, "rtsym '%s': writeq out of bounds: off: %lld + 8 > size: %lld\n",
			sym->name, off, nfp_rtsym_size(sym));
		return -ENXIO;
	}

	err = nfp_rtsym_to_dest(cpp, sym, action, token, off, &cpp_id, &addr);
	if (err)
		return err;

	return nfp_cpp_writeq(cpp, cpp_id, addr, value);
}

int nfp_rtsym_writeq(struct nfp_cpp *cpp, const struct nfp_rtsym *sym, u64 off,
		     u64 value)
{
	return __nfp_rtsym_writeq(cpp, sym, NFP_CPP_ACTION_RW, 0, off, value);
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
	u32 val32;
	u64 val;
	int err;

	sym = nfp_rtsym_lookup(rtbl, name);
	if (!sym) {
		err = -ENOENT;
		goto exit;
	}

	switch (nfp_rtsym_size(sym)) {
	case 4:
		err = nfp_rtsym_readl(rtbl->cpp, sym, 0, &val32);
		val = val32;
		break;
	case 8:
		err = nfp_rtsym_readq(rtbl->cpp, sym, 0, &val);
		break;
	default:
		nfp_err(rtbl->cpp,
			"rtsym '%s': unsupported or non-scalar size: %lld\n",
			name, nfp_rtsym_size(sym));
		err = -EINVAL;
		break;
	}

exit:
	if (error)
		*error = err;

	if (err)
		return ~0ULL;
	return val;
}

/**
 * nfp_rtsym_write_le() - Write an unsigned scalar value to a symbol
 * @rtbl:	NFP RTsym table
 * @name:	Symbol name
 * @value:	Value to write
 *
 * Lookup a symbol and write a value to it. Symbol can be 4 or 8 bytes in size.
 * If 4 bytes then the lower 32-bits of 'value' are used. Value will be
 * written as simple little-endian unsigned value.
 *
 * Return: 0 on success or error code.
 */
int nfp_rtsym_write_le(struct nfp_rtsym_table *rtbl, const char *name,
		       u64 value)
{
	const struct nfp_rtsym *sym;
	int err;

	sym = nfp_rtsym_lookup(rtbl, name);
	if (!sym)
		return -ENOENT;

	switch (nfp_rtsym_size(sym)) {
	case 4:
		err = nfp_rtsym_writel(rtbl->cpp, sym, 0, value);
		break;
	case 8:
		err = nfp_rtsym_writeq(rtbl->cpp, sym, 0, value);
		break;
	default:
		nfp_err(rtbl->cpp,
			"rtsym '%s': unsupported or non-scalar size: %lld\n",
			name, nfp_rtsym_size(sym));
		err = -EINVAL;
		break;
	}

	return err;
}

u8 __iomem *
nfp_rtsym_map(struct nfp_rtsym_table *rtbl, const char *name, const char *id,
	      unsigned int min_size, struct nfp_cpp_area **area)
{
	const struct nfp_rtsym *sym;
	u8 __iomem *mem;
	u32 cpp_id;
	u64 addr;
	int err;

	sym = nfp_rtsym_lookup(rtbl, name);
	if (!sym)
		return (u8 __iomem *)ERR_PTR(-ENOENT);

	err = nfp_rtsym_to_dest(rtbl->cpp, sym, NFP_CPP_ACTION_RW, 0, 0,
				&cpp_id, &addr);
	if (err) {
		nfp_err(rtbl->cpp, "rtsym '%s': mapping failed\n", name);
		return (u8 __iomem *)ERR_PTR(err);
	}

	if (sym->size < min_size) {
		nfp_err(rtbl->cpp, "rtsym '%s': too small\n", name);
		return (u8 __iomem *)ERR_PTR(-EINVAL);
	}

	mem = nfp_cpp_map_area(rtbl->cpp, id, cpp_id, addr, sym->size, area);
	if (IS_ERR(mem)) {
		nfp_err(rtbl->cpp, "rtysm '%s': failed to map: %ld\n",
			name, PTR_ERR(mem));
		return mem;
	}

	return mem;
}
