// SPDX-License-Identifier: GPL-2.0
/*
 * CAAM/SEC 4.x functions for handling key-generation jobs
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 *
 */
#include "compat.h"
#include "jr.h"
#include "error.h"
#include "desc_constr.h"
#include "key_gen.h"

void split_key_done(struct device *dev, u32 *desc, u32 err,
			   void *context)
{
	struct split_key_result *res = context;

	dev_dbg(dev, "%s %d: err 0x%x\n", __func__, __LINE__, err);

	if (err)
		caam_jr_strstatus(dev, err);

	res->err = err;

	complete(&res->completion);
}
EXPORT_SYMBOL(split_key_done);
/*
get a split ipad/opad key

Split key generation-----------------------------------------------

[00] 0xb0810008    jobdesc: stidx=1 share=never len=8
[01] 0x04000014        key: class2->keyreg len=20
			@0xffe01000
[03] 0x84410014  operation: cls2-op sha1 hmac init dec
[04] 0x24940000     fifold: class2 msgdata-last2 len=0 imm
[05] 0xa4000001       jump: class2 local all ->1 [06]
[06] 0x64260028    fifostr: class2 mdsplit-jdk len=40
			@0xffe04000
*/
int gen_split_key(struct device *jrdev, u8 *key_out,
		  struct alginfo * const adata, const u8 *key_in, u32 keylen,
		  int max_keylen)
{
	u32 *desc;
	struct split_key_result result;
	dma_addr_t dma_addr;
	int ret = -ENOMEM;

	adata->keylen = split_key_len(adata->algtype & OP_ALG_ALGSEL_MASK);
	adata->keylen_pad = split_key_pad_len(adata->algtype &
					      OP_ALG_ALGSEL_MASK);

	dev_dbg(jrdev, "split keylen %d split keylen padded %d\n",
		adata->keylen, adata->keylen_pad);
	print_hex_dump_debug("ctx.key@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key_in, keylen, 1);

	if (adata->keylen_pad > max_keylen)
		return -EINVAL;

	desc = kmalloc(CAAM_CMD_SZ * 6 + CAAM_PTR_SZ * 2, GFP_KERNEL | GFP_DMA);
	if (!desc) {
		dev_err(jrdev, "unable to allocate key input memory\n");
		return ret;
	}

	memcpy(key_out, key_in, keylen);

	dma_addr = dma_map_single(jrdev, key_out, adata->keylen_pad,
				  DMA_BIDIRECTIONAL);
	if (dma_mapping_error(jrdev, dma_addr)) {
		dev_err(jrdev, "unable to map key memory\n");
		goto out_free;
	}

	init_job_desc(desc, 0);
	append_key(desc, dma_addr, keylen, CLASS_2 | KEY_DEST_CLASS_REG);

	/* Sets MDHA up into an HMAC-INIT */
	append_operation(desc, (adata->algtype & OP_ALG_ALGSEL_MASK) |
			 OP_ALG_AAI_HMAC | OP_TYPE_CLASS2_ALG | OP_ALG_DECRYPT |
			 OP_ALG_AS_INIT);

	/*
	 * do a FIFO_LOAD of zero, this will trigger the internal key expansion
	 * into both pads inside MDHA
	 */
	append_fifo_load_as_imm(desc, NULL, 0, LDST_CLASS_2_CCB |
				FIFOLD_TYPE_MSG | FIFOLD_TYPE_LAST2);

	/*
	 * FIFO_STORE with the explicit split-key content store
	 * (0x26 output type)
	 */
	append_fifo_store(desc, dma_addr, adata->keylen,
			  LDST_CLASS_2_CCB | FIFOST_TYPE_SPLIT_KEK);

	print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	result.err = 0;
	init_completion(&result.completion);

	ret = caam_jr_enqueue(jrdev, desc, split_key_done, &result);
	if (!ret) {
		/* in progress */
		wait_for_completion(&result.completion);
		ret = result.err;

		print_hex_dump_debug("ctx.key@"__stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, key_out,
				     adata->keylen_pad, 1);
	}

	dma_unmap_single(jrdev, dma_addr, adata->keylen_pad, DMA_BIDIRECTIONAL);
out_free:
	kfree(desc);
	return ret;
}
EXPORT_SYMBOL(gen_split_key);
