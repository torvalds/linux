/*
 * Copyright (C) 2017 Netronome Systems, Inc.
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

#include <linux/ethtool.h>
#include <linux/vmalloc.h>

#include "nfp_main.h"
#include "nfpcore/nfp.h"
#include "nfpcore/nfp_nffw.h"

#define NFP_DUMP_SPEC_RTSYM	"_abi_dump_spec"

#define ALIGN8(x)	ALIGN(x, 8)

enum nfp_dumpspec_type {
	NFP_DUMPSPEC_TYPE_PROLOG = 10000,
};

/* The following structs must be carefully aligned so that they can be used to
 * interpret the binary dumpspec and populate the dump data in a deterministic
 * way.
 */

/* generic type plus length */
struct nfp_dump_tl {
	__be32 type;
	__be32 length;	/* chunk length to follow, aligned to 8 bytes */
	char data[0];
};

struct nfp_dump_prolog {
	struct nfp_dump_tl tl;
	__be32 dump_level;
};

/* to track state during debug dump creation TLV traversal */
struct nfp_dump_state {
	u32 requested_level;	/* input param */
	u32 dumped_size;	/* adds up to size of dumped data */
	u32 buf_size;		/* size of buffer pointer to by p */
	void *p;		/* current point in dump buffer */
};

struct nfp_dumpspec *
nfp_net_dump_load_dumpspec(struct nfp_cpp *cpp, struct nfp_rtsym_table *rtbl)
{
	const struct nfp_rtsym *specsym;
	struct nfp_dumpspec *dumpspec;
	int bytes_read;
	u32 cpp_id;

	specsym = nfp_rtsym_lookup(rtbl, NFP_DUMP_SPEC_RTSYM);
	if (!specsym)
		return NULL;

	/* expected size of this buffer is in the order of tens of kilobytes */
	dumpspec = vmalloc(sizeof(*dumpspec) + specsym->size);
	if (!dumpspec)
		return NULL;

	dumpspec->size = specsym->size;

	cpp_id = NFP_CPP_ISLAND_ID(specsym->target, NFP_CPP_ACTION_RW, 0,
				   specsym->domain);

	bytes_read = nfp_cpp_read(cpp, cpp_id, specsym->addr, dumpspec->data,
				  specsym->size);
	if (bytes_read != specsym->size) {
		vfree(dumpspec);
		nfp_warn(cpp, "Debug dump specification read failed.\n");
		return NULL;
	}

	return dumpspec;
}

s64 nfp_net_dump_calculate_size(struct nfp_pf *pf, struct nfp_dumpspec *spec,
				u32 flag)
{
	return ALIGN8(sizeof(struct nfp_dump_prolog));
}

static int nfp_add_tlv(u32 type, u32 total_tlv_sz, struct nfp_dump_state *dump)
{
	struct nfp_dump_tl *tl = dump->p;

	if (total_tlv_sz > dump->buf_size)
		return -ENOSPC;

	if (dump->buf_size - total_tlv_sz < dump->dumped_size)
		return -ENOSPC;

	tl->type = cpu_to_be32(type);
	tl->length = cpu_to_be32(total_tlv_sz - sizeof(*tl));

	dump->dumped_size += total_tlv_sz;
	dump->p += total_tlv_sz;

	return 0;
}

static int nfp_dump_populate_prolog(struct nfp_dump_state *dump)
{
	struct nfp_dump_prolog *prolog = dump->p;
	u32 total_size;
	int err;

	total_size = ALIGN8(sizeof(*prolog));

	err = nfp_add_tlv(NFP_DUMPSPEC_TYPE_PROLOG, total_size, dump);
	if (err)
		return err;

	prolog->dump_level = cpu_to_be32(dump->requested_level);

	return 0;
}

int nfp_net_dump_populate_buffer(struct nfp_pf *pf, struct nfp_dumpspec *spec,
				 struct ethtool_dump *dump_param, void *dest)
{
	struct nfp_dump_state dump;
	int err;

	dump.requested_level = dump_param->flag;
	dump.dumped_size = 0;
	dump.p = dest;
	dump.buf_size = dump_param->len;

	err = nfp_dump_populate_prolog(&dump);
	if (err)
		return err;

	/* Set size of actual dump, to trigger warning if different from
	 * calculated size.
	 */
	dump_param->len = dump.dumped_size;

	return 0;
}
