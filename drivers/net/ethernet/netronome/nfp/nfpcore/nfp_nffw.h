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
 * nfp_nffw.h
 * Authors: Jason McMullan <jason.mcmullan@netronome.com>
 *          Francois H. Theron <francois.theron@netronome.com>
 */

#ifndef NFP_NFFW_H
#define NFP_NFFW_H

/* Implemented in nfp_nffw.c */

struct nfp_nffw_info;

struct nfp_nffw_info *nfp_nffw_info_open(struct nfp_cpp *cpp);
void nfp_nffw_info_close(struct nfp_nffw_info *state);
int nfp_nffw_info_mip_first(struct nfp_nffw_info *state, u32 *cpp_id, u64 *off);

/* Implemented in nfp_mip.c */

struct nfp_mip;

const struct nfp_mip *nfp_mip_open(struct nfp_cpp *cpp);
void nfp_mip_close(const struct nfp_mip *mip);

const char *nfp_mip_name(const struct nfp_mip *mip);
void nfp_mip_symtab(const struct nfp_mip *mip, u32 *addr, u32 *size);
void nfp_mip_strtab(const struct nfp_mip *mip, u32 *addr, u32 *size);

/* Implemented in nfp_rtsym.c */

#define NFP_RTSYM_TYPE_NONE		0
#define NFP_RTSYM_TYPE_OBJECT		1
#define NFP_RTSYM_TYPE_FUNCTION		2
#define NFP_RTSYM_TYPE_ABS		3

#define NFP_RTSYM_TARGET_NONE		0
#define NFP_RTSYM_TARGET_LMEM		-1
#define NFP_RTSYM_TARGET_EMU_CACHE	-7

/**
 * struct nfp_rtsym - RTSYM descriptor
 * @name:	Symbol name
 * @addr:	Address in the domain/target's address space
 * @size:	Size (in bytes) of the symbol
 * @type:	NFP_RTSYM_TYPE_* of the symbol
 * @target:	CPP Target identifier, or NFP_RTSYM_TARGET_*
 * @domain:	CPP Target Domain (island)
 */
struct nfp_rtsym {
	const char *name;
	u64 addr;
	u64 size;
	int type;
	int target;
	int domain;
};

struct nfp_rtsym_table;

struct nfp_rtsym_table *nfp_rtsym_table_read(struct nfp_cpp *cpp);
struct nfp_rtsym_table *
__nfp_rtsym_table_read(struct nfp_cpp *cpp, const struct nfp_mip *mip);
int nfp_rtsym_count(struct nfp_rtsym_table *rtbl);
const struct nfp_rtsym *nfp_rtsym_get(struct nfp_rtsym_table *rtbl, int idx);
const struct nfp_rtsym *
nfp_rtsym_lookup(struct nfp_rtsym_table *rtbl, const char *name);
u64 nfp_rtsym_read_le(struct nfp_rtsym_table *rtbl, const char *name,
		      int *error);

#endif /* NFP_NFFW_H */
