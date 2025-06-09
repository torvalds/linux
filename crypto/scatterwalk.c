// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * Cipher operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *               2002 Adam J. Richter <adam@yggdrasil.com>
 *               2004 Jean-Luc Cooke <jlcooke@certainkey.com>
 */

#include <crypto/scatterwalk.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>

void scatterwalk_skip(struct scatter_walk *walk, unsigned int nbytes)
{
	struct scatterlist *sg = walk->sg;

	nbytes += walk->offset - sg->offset;

	while (nbytes > sg->length) {
		nbytes -= sg->length;
		sg = sg_next(sg);
	}
	walk->sg = sg;
	walk->offset = sg->offset + nbytes;
}
EXPORT_SYMBOL_GPL(scatterwalk_skip);

inline void memcpy_from_scatterwalk(void *buf, struct scatter_walk *walk,
				    unsigned int nbytes)
{
	do {
		unsigned int to_copy;

		to_copy = scatterwalk_next(walk, nbytes);
		memcpy(buf, walk->addr, to_copy);
		scatterwalk_done_src(walk, to_copy);
		buf += to_copy;
		nbytes -= to_copy;
	} while (nbytes);
}
EXPORT_SYMBOL_GPL(memcpy_from_scatterwalk);

inline void memcpy_to_scatterwalk(struct scatter_walk *walk, const void *buf,
				  unsigned int nbytes)
{
	do {
		unsigned int to_copy;

		to_copy = scatterwalk_next(walk, nbytes);
		memcpy(walk->addr, buf, to_copy);
		scatterwalk_done_dst(walk, to_copy);
		buf += to_copy;
		nbytes -= to_copy;
	} while (nbytes);
}
EXPORT_SYMBOL_GPL(memcpy_to_scatterwalk);

void memcpy_from_sglist(void *buf, struct scatterlist *sg,
			unsigned int start, unsigned int nbytes)
{
	struct scatter_walk walk;

	if (unlikely(nbytes == 0)) /* in case sg == NULL */
		return;

	scatterwalk_start_at_pos(&walk, sg, start);
	memcpy_from_scatterwalk(buf, &walk, nbytes);
}
EXPORT_SYMBOL_GPL(memcpy_from_sglist);

void memcpy_to_sglist(struct scatterlist *sg, unsigned int start,
		      const void *buf, unsigned int nbytes)
{
	struct scatter_walk walk;

	if (unlikely(nbytes == 0)) /* in case sg == NULL */
		return;

	scatterwalk_start_at_pos(&walk, sg, start);
	memcpy_to_scatterwalk(&walk, buf, nbytes);
}
EXPORT_SYMBOL_GPL(memcpy_to_sglist);

void memcpy_sglist(struct scatterlist *dst, struct scatterlist *src,
		   unsigned int nbytes)
{
	struct scatter_walk swalk;
	struct scatter_walk dwalk;

	if (unlikely(nbytes == 0)) /* in case sg == NULL */
		return;

	scatterwalk_start(&swalk, src);
	scatterwalk_start(&dwalk, dst);

	do {
		unsigned int slen, dlen;
		unsigned int len;

		slen = scatterwalk_next(&swalk, nbytes);
		dlen = scatterwalk_next(&dwalk, nbytes);
		len = min(slen, dlen);
		memcpy(dwalk.addr, swalk.addr, len);
		scatterwalk_done_dst(&dwalk, len);
		scatterwalk_done_src(&swalk, len);
		nbytes -= len;
	} while (nbytes);
}
EXPORT_SYMBOL_GPL(memcpy_sglist);

struct scatterlist *scatterwalk_ffwd(struct scatterlist dst[2],
				     struct scatterlist *src,
				     unsigned int len)
{
	for (;;) {
		if (!len)
			return src;

		if (src->length > len)
			break;

		len -= src->length;
		src = sg_next(src);
	}

	sg_init_table(dst, 2);
	sg_set_page(dst, sg_page(src), src->length - len, src->offset + len);
	scatterwalk_crypto_chain(dst, sg_next(src), 2);

	return dst;
}
EXPORT_SYMBOL_GPL(scatterwalk_ffwd);
