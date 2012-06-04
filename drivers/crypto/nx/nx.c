/**
 * Routines supporting the Power 7+ Nest Accelerators driver
 *
 * Copyright (C) 2011-2012 International Business Machines Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 only.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Author: Kent Yoder <yoder1@us.ibm.com>
 */

#include <crypto/internal/hash.h>
#include <crypto/hash.h>
#include <crypto/aes.h>
#include <crypto/sha.h>
#include <crypto/algapi.h>
#include <crypto/scatterwalk.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/device.h>
#include <linux/of.h>
#include <asm/pSeries_reconfig.h>
#include <asm/abs_addr.h>
#include <asm/hvcall.h>
#include <asm/vio.h>

#include "nx_csbcpb.h"
#include "nx.h"


/**
 * nx_hcall_sync - make an H_COP_OP hcall for the passed in op structure
 *
 * @nx_ctx: the crypto context handle
 * @op: PFO operation struct to pass in
 * @may_sleep: flag indicating the request can sleep
 *
 * Make the hcall, retrying while the hardware is busy. If we cannot yield
 * the thread, limit the number of retries to 10 here.
 */
int nx_hcall_sync(struct nx_crypto_ctx *nx_ctx,
		  struct vio_pfo_op    *op,
		  u32                   may_sleep)
{
	int rc, retries = 10;
	struct vio_dev *viodev = nx_driver.viodev;

	atomic_inc(&(nx_ctx->stats->sync_ops));

	do {
		rc = vio_h_cop_sync(viodev, op);
	} while ((rc == -EBUSY && !may_sleep && retries--) ||
	         (rc == -EBUSY && may_sleep && cond_resched()));

	if (rc) {
		dev_dbg(&viodev->dev, "vio_h_cop_sync failed: rc: %d "
			"hcall rc: %ld\n", rc, op->hcall_err);
		atomic_inc(&(nx_ctx->stats->errors));
		atomic_set(&(nx_ctx->stats->last_error), op->hcall_err);
		atomic_set(&(nx_ctx->stats->last_error_pid), current->pid);
	}

	return rc;
}

/**
 * nx_build_sg_list - build an NX scatter list describing a single  buffer
 *
 * @sg_head: pointer to the first scatter list element to build
 * @start_addr: pointer to the linear buffer
 * @len: length of the data at @start_addr
 * @sgmax: the largest number of scatter list elements we're allowed to create
 *
 * This function will start writing nx_sg elements at @sg_head and keep
 * writing them until all of the data from @start_addr is described or
 * until sgmax elements have been written. Scatter list elements will be
 * created such that none of the elements describes a buffer that crosses a 4K
 * boundary.
 */
struct nx_sg *nx_build_sg_list(struct nx_sg *sg_head,
			       u8           *start_addr,
			       unsigned int  len,
			       u32           sgmax)
{
	unsigned int sg_len = 0;
	struct nx_sg *sg;
	u64 sg_addr = (u64)start_addr;
	u64 end_addr;

	/* determine the start and end for this address range - slightly
	 * different if this is in VMALLOC_REGION */
	if (is_vmalloc_addr(start_addr))
		sg_addr = phys_to_abs(page_to_phys(vmalloc_to_page(start_addr)))
			  + offset_in_page(sg_addr);
	else
		sg_addr = virt_to_abs(sg_addr);

	end_addr = sg_addr + len;

	/* each iteration will write one struct nx_sg element and add the
	 * length of data described by that element to sg_len. Once @len bytes
	 * have been described (or @sgmax elements have been written), the
	 * loop ends. min_t is used to ensure @end_addr falls on the same page
	 * as sg_addr, if not, we need to create another nx_sg element for the
	 * data on the next page */
	for (sg = sg_head; sg_len < len; sg++) {
		sg->addr = sg_addr;
		sg_addr = min_t(u64, NX_PAGE_NUM(sg_addr + NX_PAGE_SIZE), end_addr);
		sg->len = sg_addr - sg->addr;
		sg_len += sg->len;

		if ((sg - sg_head) == sgmax) {
			pr_err("nx: scatter/gather list overflow, pid: %d\n",
			       current->pid);
			return NULL;
		}
	}

	/* return the moved sg_head pointer */
	return sg;
}

/**
 * nx_walk_and_build - walk a linux scatterlist and build an nx scatterlist
 *
 * @nx_dst: pointer to the first nx_sg element to write
 * @sglen: max number of nx_sg entries we're allowed to write
 * @sg_src: pointer to the source linux scatterlist to walk
 * @start: number of bytes to fast-forward past at the beginning of @sg_src
 * @src_len: number of bytes to walk in @sg_src
 */
struct nx_sg *nx_walk_and_build(struct nx_sg       *nx_dst,
				unsigned int        sglen,
				struct scatterlist *sg_src,
				unsigned int        start,
				unsigned int        src_len)
{
	struct scatter_walk walk;
	struct nx_sg *nx_sg = nx_dst;
	unsigned int n, offset = 0, len = src_len;
	char *dst;

	/* we need to fast forward through @start bytes first */
	for (;;) {
		scatterwalk_start(&walk, sg_src);

		if (start < offset + sg_src->length)
			break;

		offset += sg_src->length;
		sg_src = scatterwalk_sg_next(sg_src);
	}

	/* start - offset is the number of bytes to advance in the scatterlist
	 * element we're currently looking at */
	scatterwalk_advance(&walk, start - offset);

	while (len && nx_sg) {
		n = scatterwalk_clamp(&walk, len);
		if (!n) {
			scatterwalk_start(&walk, sg_next(walk.sg));
			n = scatterwalk_clamp(&walk, len);
		}
		dst = scatterwalk_map(&walk);

		nx_sg = nx_build_sg_list(nx_sg, dst, n, sglen);
		len -= n;

		scatterwalk_unmap(dst);
		scatterwalk_advance(&walk, n);
		scatterwalk_done(&walk, SCATTERWALK_FROM_SG, len);
	}

	/* return the moved destination pointer */
	return nx_sg;
}

/**
 * nx_build_sg_lists - walk the input scatterlists and build arrays of NX
 *                     scatterlists based on them.
 *
 * @nx_ctx: NX crypto context for the lists we're building
 * @desc: the block cipher descriptor for the operation
 * @dst: destination scatterlist
 * @src: source scatterlist
 * @nbytes: length of data described in the scatterlists
 * @iv: destination for the iv data, if the algorithm requires it
 *
 * This is common code shared by all the AES algorithms. It uses the block
 * cipher walk routines to traverse input and output scatterlists, building
 * corresponding NX scatterlists
 */
int nx_build_sg_lists(struct nx_crypto_ctx  *nx_ctx,
		      struct blkcipher_desc *desc,
		      struct scatterlist    *dst,
		      struct scatterlist    *src,
		      unsigned int           nbytes,
		      u8                    *iv)
{
	struct nx_sg *nx_insg = nx_ctx->in_sg;
	struct nx_sg *nx_outsg = nx_ctx->out_sg;
	struct blkcipher_walk walk;
	int rc;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	rc = blkcipher_walk_virt_block(desc, &walk, AES_BLOCK_SIZE);
	if (rc)
		goto out;

	if (iv)
		memcpy(iv, walk.iv, AES_BLOCK_SIZE);

	while (walk.nbytes) {
		nx_insg = nx_build_sg_list(nx_insg, walk.src.virt.addr,
					   walk.nbytes, nx_ctx->ap->sglen);
		nx_outsg = nx_build_sg_list(nx_outsg, walk.dst.virt.addr,
					    walk.nbytes, nx_ctx->ap->sglen);

		rc = blkcipher_walk_done(desc, &walk, 0);
		if (rc)
			break;
	}

	if (walk.nbytes) {
		nx_insg = nx_build_sg_list(nx_insg, walk.src.virt.addr,
					   walk.nbytes, nx_ctx->ap->sglen);
		nx_outsg = nx_build_sg_list(nx_outsg, walk.dst.virt.addr,
					    walk.nbytes, nx_ctx->ap->sglen);

		rc = 0;
	}

	/* these lengths should be negative, which will indicate to phyp that
	 * the input and output parameters are scatterlists, not linear
	 * buffers */
	nx_ctx->op.inlen = (nx_ctx->in_sg - nx_insg) * sizeof(struct nx_sg);
	nx_ctx->op.outlen = (nx_ctx->out_sg - nx_outsg) * sizeof(struct nx_sg);
out:
	return rc;
}

/**
 * nx_ctx_init - initialize an nx_ctx's vio_pfo_op struct
 *
 * @nx_ctx: the nx context to initialize
 * @function: the function code for the op
 */
void nx_ctx_init(struct nx_crypto_ctx *nx_ctx, unsigned int function)
{
	memset(nx_ctx->kmem, 0, nx_ctx->kmem_len);
	nx_ctx->csbcpb->csb.valid |= NX_CSB_VALID_BIT;

	nx_ctx->op.flags = function;
	nx_ctx->op.csbcpb = virt_to_abs(nx_ctx->csbcpb);
	nx_ctx->op.in = virt_to_abs(nx_ctx->in_sg);
	nx_ctx->op.out = virt_to_abs(nx_ctx->out_sg);

	if (nx_ctx->csbcpb_aead) {
		nx_ctx->csbcpb_aead->csb.valid |= NX_CSB_VALID_BIT;

		nx_ctx->op_aead.flags = function;
		nx_ctx->op_aead.csbcpb = virt_to_abs(nx_ctx->csbcpb_aead);
		nx_ctx->op_aead.in = virt_to_abs(nx_ctx->in_sg);
		nx_ctx->op_aead.out = virt_to_abs(nx_ctx->out_sg);
	}
}

static void nx_of_update_status(struct device   *dev,
			       struct property *p,
			       struct nx_of    *props)
{
	if (!strncmp(p->value, "okay", p->length)) {
		props->status = NX_WAITING;
		props->flags |= NX_OF_FLAG_STATUS_SET;
	} else {
		dev_info(dev, "%s: status '%s' is not 'okay'\n", __func__,
			 (char *)p->value);
	}
}

static void nx_of_update_sglen(struct device   *dev,
			       struct property *p,
			       struct nx_of    *props)
{
	if (p->length != sizeof(props->max_sg_len)) {
		dev_err(dev, "%s: unexpected format for "
			"ibm,max-sg-len property\n", __func__);
		dev_dbg(dev, "%s: ibm,max-sg-len is %d bytes "
			"long, expected %zd bytes\n", __func__,
			p->length, sizeof(props->max_sg_len));
		return;
	}

	props->max_sg_len = *(u32 *)p->value;
	props->flags |= NX_OF_FLAG_MAXSGLEN_SET;
}

static void nx_of_update_msc(struct device   *dev,
			     struct property *p,
			     struct nx_of    *props)
{
	struct msc_triplet *trip;
	struct max_sync_cop *msc;
	unsigned int bytes_so_far, i, lenp;

	msc = (struct max_sync_cop *)p->value;
	lenp = p->length;

	/* You can't tell if the data read in for this property is sane by its
	 * size alone. This is because there are sizes embedded in the data
	 * structure. The best we can do is check lengths as we parse and bail
	 * as soon as a length error is detected. */
	bytes_so_far = 0;

	while ((bytes_so_far + sizeof(struct max_sync_cop)) <= lenp) {
		bytes_so_far += sizeof(struct max_sync_cop);

		trip = msc->trip;

		for (i = 0;
		     ((bytes_so_far + sizeof(struct msc_triplet)) <= lenp) &&
		     i < msc->triplets;
		     i++) {
			if (msc->fc > NX_MAX_FC || msc->mode > NX_MAX_MODE) {
				dev_err(dev, "unknown function code/mode "
					"combo: %d/%d (ignored)\n", msc->fc,
					msc->mode);
				goto next_loop;
			}

			switch (trip->keybitlen) {
			case 128:
			case 160:
				props->ap[msc->fc][msc->mode][0].databytelen =
					trip->databytelen;
				props->ap[msc->fc][msc->mode][0].sglen =
					trip->sglen;
				break;
			case 192:
				props->ap[msc->fc][msc->mode][1].databytelen =
					trip->databytelen;
				props->ap[msc->fc][msc->mode][1].sglen =
					trip->sglen;
				break;
			case 256:
				if (msc->fc == NX_FC_AES) {
					props->ap[msc->fc][msc->mode][2].
						databytelen = trip->databytelen;
					props->ap[msc->fc][msc->mode][2].sglen =
						trip->sglen;
				} else if (msc->fc == NX_FC_AES_HMAC ||
					   msc->fc == NX_FC_SHA) {
					props->ap[msc->fc][msc->mode][1].
						databytelen = trip->databytelen;
					props->ap[msc->fc][msc->mode][1].sglen =
						trip->sglen;
				} else {
					dev_warn(dev, "unknown function "
						"code/key bit len combo"
						": (%u/256)\n", msc->fc);
				}
				break;
			case 512:
				props->ap[msc->fc][msc->mode][2].databytelen =
					trip->databytelen;
				props->ap[msc->fc][msc->mode][2].sglen =
					trip->sglen;
				break;
			default:
				dev_warn(dev, "unknown function code/key bit "
					 "len combo: (%u/%u)\n", msc->fc,
					 trip->keybitlen);
				break;
			}
next_loop:
			bytes_so_far += sizeof(struct msc_triplet);
			trip++;
		}

		msc = (struct max_sync_cop *)trip;
	}

	props->flags |= NX_OF_FLAG_MAXSYNCCOP_SET;
}

/**
 * nx_of_init - read openFirmware values from the device tree
 *
 * @dev: device handle
 * @props: pointer to struct to hold the properties values
 *
 * Called once at driver probe time, this function will read out the
 * openFirmware properties we use at runtime. If all the OF properties are
 * acceptable, when we exit this function props->flags will indicate that
 * we're ready to register our crypto algorithms.
 */
static void nx_of_init(struct device *dev, struct nx_of *props)
{
	struct device_node *base_node = dev->of_node;
	struct property *p;

	p = of_find_property(base_node, "status", NULL);
	if (!p)
		dev_info(dev, "%s: property 'status' not found\n", __func__);
	else
		nx_of_update_status(dev, p, props);

	p = of_find_property(base_node, "ibm,max-sg-len", NULL);
	if (!p)
		dev_info(dev, "%s: property 'ibm,max-sg-len' not found\n",
			 __func__);
	else
		nx_of_update_sglen(dev, p, props);

	p = of_find_property(base_node, "ibm,max-sync-cop", NULL);
	if (!p)
		dev_info(dev, "%s: property 'ibm,max-sync-cop' not found\n",
			 __func__);
	else
		nx_of_update_msc(dev, p, props);
}

/**
 * nx_register_algs - register algorithms with the crypto API
 *
 * Called from nx_probe()
 *
 * If all OF properties are in an acceptable state, the driver flags will
 * indicate that we're ready and we'll create our debugfs files and register
 * out crypto algorithms.
 */
static int nx_register_algs(void)
{
	int rc = -1;

	if (nx_driver.of.flags != NX_OF_FLAG_MASK_READY)
		goto out;

	memset(&nx_driver.stats, 0, sizeof(struct nx_stats));

	rc = NX_DEBUGFS_INIT(&nx_driver);
	if (rc)
		goto out;

	rc = crypto_register_alg(&nx_ecb_aes_alg);
	if (rc)
		goto out;

	rc = crypto_register_alg(&nx_cbc_aes_alg);
	if (rc)
		goto out_unreg_ecb;

	rc = crypto_register_alg(&nx_ctr_aes_alg);
	if (rc)
		goto out_unreg_cbc;

	rc = crypto_register_alg(&nx_ctr3686_aes_alg);
	if (rc)
		goto out_unreg_ctr;

	rc = crypto_register_alg(&nx_gcm_aes_alg);
	if (rc)
		goto out_unreg_ctr3686;

	rc = crypto_register_alg(&nx_gcm4106_aes_alg);
	if (rc)
		goto out_unreg_gcm;

	rc = crypto_register_alg(&nx_ccm_aes_alg);
	if (rc)
		goto out_unreg_gcm4106;

	rc = crypto_register_alg(&nx_ccm4309_aes_alg);
	if (rc)
		goto out_unreg_ccm;

	rc = crypto_register_shash(&nx_shash_sha256_alg);
	if (rc)
		goto out_unreg_ccm4309;

	rc = crypto_register_shash(&nx_shash_sha512_alg);
	if (rc)
		goto out_unreg_s256;

	rc = crypto_register_shash(&nx_shash_aes_xcbc_alg);
	if (rc)
		goto out_unreg_s512;

	nx_driver.of.status = NX_OKAY;

	goto out;

out_unreg_s512:
	crypto_unregister_shash(&nx_shash_sha512_alg);
out_unreg_s256:
	crypto_unregister_shash(&nx_shash_sha256_alg);
out_unreg_ccm4309:
	crypto_unregister_alg(&nx_ccm4309_aes_alg);
out_unreg_ccm:
	crypto_unregister_alg(&nx_ccm_aes_alg);
out_unreg_gcm4106:
	crypto_unregister_alg(&nx_gcm4106_aes_alg);
out_unreg_gcm:
	crypto_unregister_alg(&nx_gcm_aes_alg);
out_unreg_ctr3686:
	crypto_unregister_alg(&nx_ctr3686_aes_alg);
out_unreg_ctr:
	crypto_unregister_alg(&nx_ctr_aes_alg);
out_unreg_cbc:
	crypto_unregister_alg(&nx_cbc_aes_alg);
out_unreg_ecb:
	crypto_unregister_alg(&nx_ecb_aes_alg);
out:
	return rc;
}

/**
 * nx_crypto_ctx_init - create and initialize a crypto api context
 *
 * @nx_ctx: the crypto api context
 * @fc: function code for the context
 * @mode: the function code specific mode for this context
 */
static int nx_crypto_ctx_init(struct nx_crypto_ctx *nx_ctx, u32 fc, u32 mode)
{
	if (nx_driver.of.status != NX_OKAY) {
		pr_err("Attempt to initialize NX crypto context while device "
		       "is not available!\n");
		return -ENODEV;
	}

	/* we need an extra page for csbcpb_aead for these modes */
	if (mode == NX_MODE_AES_GCM || mode == NX_MODE_AES_CCM)
		nx_ctx->kmem_len = (4 * NX_PAGE_SIZE) +
				   sizeof(struct nx_csbcpb);
	else
		nx_ctx->kmem_len = (3 * NX_PAGE_SIZE) +
				   sizeof(struct nx_csbcpb);

	nx_ctx->kmem = kmalloc(nx_ctx->kmem_len, GFP_KERNEL);
	if (!nx_ctx->kmem)
		return -ENOMEM;

	/* the csbcpb and scatterlists must be 4K aligned pages */
	nx_ctx->csbcpb = (struct nx_csbcpb *)(round_up((u64)nx_ctx->kmem,
						       (u64)NX_PAGE_SIZE));
	nx_ctx->in_sg = (struct nx_sg *)((u8 *)nx_ctx->csbcpb + NX_PAGE_SIZE);
	nx_ctx->out_sg = (struct nx_sg *)((u8 *)nx_ctx->in_sg + NX_PAGE_SIZE);

	if (mode == NX_MODE_AES_GCM || mode == NX_MODE_AES_CCM)
		nx_ctx->csbcpb_aead =
			(struct nx_csbcpb *)((u8 *)nx_ctx->out_sg +
					     NX_PAGE_SIZE);

	/* give each context a pointer to global stats and their OF
	 * properties */
	nx_ctx->stats = &nx_driver.stats;
	memcpy(nx_ctx->props, nx_driver.of.ap[fc][mode],
	       sizeof(struct alg_props) * 3);

	return 0;
}

/* entry points from the crypto tfm initializers */
int nx_crypto_ctx_aes_ccm_init(struct crypto_tfm *tfm)
{
	return nx_crypto_ctx_init(crypto_tfm_ctx(tfm), NX_FC_AES,
				  NX_MODE_AES_CCM);
}

int nx_crypto_ctx_aes_gcm_init(struct crypto_tfm *tfm)
{
	return nx_crypto_ctx_init(crypto_tfm_ctx(tfm), NX_FC_AES,
				  NX_MODE_AES_GCM);
}

int nx_crypto_ctx_aes_ctr_init(struct crypto_tfm *tfm)
{
	return nx_crypto_ctx_init(crypto_tfm_ctx(tfm), NX_FC_AES,
				  NX_MODE_AES_CTR);
}

int nx_crypto_ctx_aes_cbc_init(struct crypto_tfm *tfm)
{
	return nx_crypto_ctx_init(crypto_tfm_ctx(tfm), NX_FC_AES,
				  NX_MODE_AES_CBC);
}

int nx_crypto_ctx_aes_ecb_init(struct crypto_tfm *tfm)
{
	return nx_crypto_ctx_init(crypto_tfm_ctx(tfm), NX_FC_AES,
				  NX_MODE_AES_ECB);
}

int nx_crypto_ctx_sha_init(struct crypto_tfm *tfm)
{
	return nx_crypto_ctx_init(crypto_tfm_ctx(tfm), NX_FC_SHA, NX_MODE_SHA);
}

int nx_crypto_ctx_aes_xcbc_init(struct crypto_tfm *tfm)
{
	return nx_crypto_ctx_init(crypto_tfm_ctx(tfm), NX_FC_AES,
				  NX_MODE_AES_XCBC_MAC);
}

/**
 * nx_crypto_ctx_exit - destroy a crypto api context
 *
 * @tfm: the crypto transform pointer for the context
 *
 * As crypto API contexts are destroyed, this exit hook is called to free the
 * memory associated with it.
 */
void nx_crypto_ctx_exit(struct crypto_tfm *tfm)
{
	struct nx_crypto_ctx *nx_ctx = crypto_tfm_ctx(tfm);

	kzfree(nx_ctx->kmem);
	nx_ctx->csbcpb = NULL;
	nx_ctx->csbcpb_aead = NULL;
	nx_ctx->in_sg = NULL;
	nx_ctx->out_sg = NULL;
}

static int __devinit nx_probe(struct vio_dev *viodev,
			      const struct vio_device_id *id)
{
	dev_dbg(&viodev->dev, "driver probed: %s resource id: 0x%x\n",
		viodev->name, viodev->resource_id);

	if (nx_driver.viodev) {
		dev_err(&viodev->dev, "%s: Attempt to register more than one "
			"instance of the hardware\n", __func__);
		return -EINVAL;
	}

	nx_driver.viodev = viodev;

	nx_of_init(&viodev->dev, &nx_driver.of);

	return nx_register_algs();
}

static int __devexit nx_remove(struct vio_dev *viodev)
{
	dev_dbg(&viodev->dev, "entering nx_remove for UA 0x%x\n",
		viodev->unit_address);

	if (nx_driver.of.status == NX_OKAY) {
		NX_DEBUGFS_FINI(&nx_driver);

		crypto_unregister_alg(&nx_ccm_aes_alg);
		crypto_unregister_alg(&nx_ccm4309_aes_alg);
		crypto_unregister_alg(&nx_gcm_aes_alg);
		crypto_unregister_alg(&nx_gcm4106_aes_alg);
		crypto_unregister_alg(&nx_ctr_aes_alg);
		crypto_unregister_alg(&nx_ctr3686_aes_alg);
		crypto_unregister_alg(&nx_cbc_aes_alg);
		crypto_unregister_alg(&nx_ecb_aes_alg);
		crypto_unregister_shash(&nx_shash_sha256_alg);
		crypto_unregister_shash(&nx_shash_sha512_alg);
		crypto_unregister_shash(&nx_shash_aes_xcbc_alg);
	}

	return 0;
}


/* module wide initialization/cleanup */
static int __init nx_init(void)
{
	return vio_register_driver(&nx_driver.viodriver);
}

static void __exit nx_fini(void)
{
	vio_unregister_driver(&nx_driver.viodriver);
}

static struct vio_device_id nx_crypto_driver_ids[] __devinitdata = {
	{ "ibm,sym-encryption-v1", "ibm,sym-encryption" },
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, nx_crypto_driver_ids);

/* driver state structure */
struct nx_crypto_driver nx_driver = {
	.viodriver = {
		.id_table = nx_crypto_driver_ids,
		.probe = nx_probe,
		.remove = nx_remove,
		.name  = NX_NAME,
	},
};

module_init(nx_init);
module_exit(nx_fini);

MODULE_AUTHOR("Kent Yoder <yoder1@us.ibm.com>");
MODULE_DESCRIPTION(NX_STRING);
MODULE_LICENSE("GPL");
MODULE_VERSION(NX_VERSION);
