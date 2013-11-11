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

#ifdef DEBUG
	dev_err(dev, "%s %d: err 0x%x\n", __func__, __LINE__, err);
#endif

	if (err) {
		char tmp[CAAM_ERROR_STR_MAX];

		dev_err(dev, "%08x: %s\n", err, caam_jr_strstatus(tmp, err));
	}

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
int gen_split_key(struct device *jrdev, u8 *key_out, int split_key_len,
		  int split_key_pad_len, const u8 *key_in, u32 keylen,
		  u32 alg_op)
{
	u32 *desc;
	struct split_key_result result;
	dma_addr_t dma_addr_in, dma_addr_out;
	int ret = 0;

	desc = kmalloc(CAAM_CMD_SZ * 6 + CAAM_PTR_SZ * 2, GFP_KERNEL | GFP_DMA);
	if (!desc) {
		dev_err(jrdev, "unable to allocate key input memory\n");
		return -ENOMEM;
	}

	init_job_desc(desc, 0);

	dma_addr_in = dma_map_single(jrdev, (void *)key_in, keylen,
				     DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, dma_addr_in)) {
		dev_err(jrdev, "unable to map key input memory\n");
		kfree(desc);
		return -ENOMEM;
	}
	append_key(desc, dma_addr_in, keylen, CLASS_2 | KEY_DEST_CLASS_REG);

	/* Sets MDHA up into an HMAC-INIT */
	append_operation(desc, alg_op | OP_ALG_DECRYPT | OP_ALG_AS_INIT);

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
	dma_addr_out = dma_map_single(jrdev, key_out, split_key_pad_len,
				      DMA_FROM_DEVICE);
	if (dma_mapping_error(jrdev, dma_addr_out)) {
		dev_err(jrdev, "unable to map key output memory\n");
		kfree(desc);
		return -ENOMEM;
	}
	append_fifo_store(desc, dma_addr_out, split_key_len,
			  LDST_CLASS_2_CCB | FIFOST_TYPE_SPLIT_KEK);

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ctx.key@"xstr(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, key_in, keylen, 1);
	print_hex_dump(KERN_ERR, "jobdesc@"xstr(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc), 1);
#endif

	result.err = 0;
	init_completion(&result.completion);

	ret = caam_jr_enqueue(jrdev, desc, split_key_done, &result);
	if (!ret) {
		/* in progress */
		wait_for_completion_interruptible(&result.completion);
		ret = result.err;
#ifdef DEBUG
		print_hex_dump(KERN_ERR, "ctx.key@"xstr(__LINE__)": ",
			       DUMP_PREFIX_ADDRESS, 16, 4, key_out,
			       split_key_pad_len, 1);
#endif
	}

	dma_unmap_single(jrdev, dma_addr_out, split_key_pad_len,
			 DMA_FROM_DEVICE);
	dma_unmap_single(jrdev, dma_addr_in, keylen, DMA_TO_DEVICE);

	kfree(desc);

	return ret;
}
EXPORT_SYMBOL(gen_split_key);
