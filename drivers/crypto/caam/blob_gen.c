// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Pengutronix, Steffen Trumtrar <kernel@pengutronix.de>
 * Copyright (C) 2021 Pengutronix, Ahmad Fatoum <kernel@pengutronix.de>
 * Copyright 2024 NXP
 */

#define pr_fmt(fmt) "caam blob_gen: " fmt

#include <linux/bitfield.h>
#include <linux/device.h>
#include <soc/fsl/caam-blob.h>

#include "compat.h"
#include "desc_constr.h"
#include "desc.h"
#include "error.h"
#include "intern.h"
#include "jr.h"
#include "regs.h"

#define CAAM_BLOB_DESC_BYTES_MAX					\
	/* Command to initialize & stating length of descriptor */	\
	(CAAM_CMD_SZ +							\
	/* Command to append the key-modifier + key-modifier data */	\
	 CAAM_CMD_SZ + CAAM_BLOB_KEYMOD_LENGTH +			\
	/* Command to include input key + pointer to the input key */	\
	 CAAM_CMD_SZ + CAAM_PTR_SZ_MAX +				\
	/* Command to include output key + pointer to the output key */	\
	 CAAM_CMD_SZ + CAAM_PTR_SZ_MAX +				\
	/* Command describing the operation to perform */		\
	 CAAM_CMD_SZ)

struct caam_blob_priv {
	struct device jrdev;
};

struct caam_blob_job_result {
	int err;
	struct completion completion;
};

static void caam_blob_job_done(struct device *dev, u32 *desc, u32 err, void *context)
{
	struct caam_blob_job_result *res = context;
	int ecode = 0;

	dev_dbg(dev, "%s %d: err 0x%x\n", __func__, __LINE__, err);

	if (err)
		ecode = caam_jr_strstatus(dev, err);

	res->err = ecode;

	/*
	 * Upon completion, desc points to a buffer containing a CAAM job
	 * descriptor which encapsulates data into an externally-storable
	 * blob.
	 */
	complete(&res->completion);
}

int caam_process_blob(struct caam_blob_priv *priv,
		      struct caam_blob_info *info, bool encap)
{
	const struct caam_drv_private *ctrlpriv;
	struct caam_blob_job_result testres;
	struct device *jrdev = &priv->jrdev;
	dma_addr_t dma_in, dma_out;
	int op = OP_PCLID_BLOB;
	size_t output_len;
	u32 *desc;
	u32 moo;
	int ret;

	if (info->key_mod_len > CAAM_BLOB_KEYMOD_LENGTH)
		return -EINVAL;

	if (encap) {
		op |= OP_TYPE_ENCAP_PROTOCOL;
		output_len = info->input_len + CAAM_BLOB_OVERHEAD;
	} else {
		op |= OP_TYPE_DECAP_PROTOCOL;
		output_len = info->input_len - CAAM_BLOB_OVERHEAD;
	}

	desc = kzalloc(CAAM_BLOB_DESC_BYTES_MAX, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	dma_in = dma_map_single(jrdev, info->input, info->input_len,
				DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, dma_in)) {
		dev_err(jrdev, "unable to map input DMA buffer\n");
		ret = -ENOMEM;
		goto out_free;
	}

	dma_out = dma_map_single(jrdev, info->output, output_len,
				 DMA_FROM_DEVICE);
	if (dma_mapping_error(jrdev, dma_out)) {
		dev_err(jrdev, "unable to map output DMA buffer\n");
		ret = -ENOMEM;
		goto out_unmap_in;
	}

	ctrlpriv = dev_get_drvdata(jrdev->parent);
	moo = FIELD_GET(CSTA_MOO, rd_reg32(&ctrlpriv->jr[0]->perfmon.status));
	if (moo != CSTA_MOO_SECURE && moo != CSTA_MOO_TRUSTED)
		dev_warn(jrdev,
			 "using insecure test key, enable HAB to use unique device key!\n");

	/*
	 * A data blob is encrypted using a blob key (BK); a random number.
	 * The BK is used as an AES-CCM key. The initial block (B0) and the
	 * initial counter (Ctr0) are generated automatically and stored in
	 * Class 1 Context DWords 0+1+2+3. The random BK is stored in the
	 * Class 1 Key Register. Operation Mode is set to AES-CCM.
	 */

	init_job_desc(desc, 0);
	append_key_as_imm(desc, info->key_mod, info->key_mod_len,
			  info->key_mod_len, CLASS_2 | KEY_DEST_CLASS_REG);
	append_seq_in_ptr_intlen(desc, dma_in, info->input_len, 0);
	append_seq_out_ptr_intlen(desc, dma_out, output_len, 0);
	append_operation(desc, op);

	print_hex_dump_debug("data@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 1, info->input,
			     info->input_len, false);
	print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 1, desc,
			     desc_bytes(desc), false);

	testres.err = 0;
	init_completion(&testres.completion);

	ret = caam_jr_enqueue(jrdev, desc, caam_blob_job_done, &testres);
	if (ret == -EINPROGRESS) {
		wait_for_completion(&testres.completion);
		ret = testres.err;
		print_hex_dump_debug("output@"__stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 1, info->output,
				     output_len, false);
	}

	if (ret == 0)
		info->output_len = output_len;

	dma_unmap_single(jrdev, dma_out, output_len, DMA_FROM_DEVICE);
out_unmap_in:
	dma_unmap_single(jrdev, dma_in, info->input_len, DMA_TO_DEVICE);
out_free:
	kfree(desc);

	return ret;
}
EXPORT_SYMBOL(caam_process_blob);

struct caam_blob_priv *caam_blob_gen_init(void)
{
	struct caam_drv_private *ctrlpriv;
	struct device *jrdev;

	/*
	 * caam_blob_gen_init() may expectedly fail with -ENODEV, e.g. when
	 * CAAM driver didn't probe or when SoC lacks BLOB support. An
	 * error would be harsh in this case, so we stick to info level.
	 */

	jrdev = caam_jr_alloc();
	if (IS_ERR(jrdev)) {
		pr_info("job ring requested, but none currently available\n");
		return ERR_PTR(-ENODEV);
	}

	ctrlpriv = dev_get_drvdata(jrdev->parent);
	if (!ctrlpriv->blob_present) {
		dev_info(jrdev, "no hardware blob generation support\n");
		caam_jr_free(jrdev);
		return ERR_PTR(-ENODEV);
	}

	return container_of(jrdev, struct caam_blob_priv, jrdev);
}
EXPORT_SYMBOL(caam_blob_gen_init);

void caam_blob_gen_exit(struct caam_blob_priv *priv)
{
	caam_jr_free(&priv->jrdev);
}
EXPORT_SYMBOL(caam_blob_gen_exit);
