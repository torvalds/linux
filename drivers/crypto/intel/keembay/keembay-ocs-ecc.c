// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay OCS ECC Crypto Driver.
 *
 * Copyright (C) 2019-2021 Intel Corporation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <crypto/ecc_curve.h>
#include <crypto/ecdh.h>
#include <crypto/engine.h>
#include <crypto/internal/ecc.h>
#include <crypto/internal/kpp.h>
#include <crypto/kpp.h>
#include <crypto/rng.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/fips.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/string.h>

#define DRV_NAME			"keembay-ocs-ecc"

#define KMB_OCS_ECC_PRIORITY		350

#define HW_OFFS_OCS_ECC_COMMAND		0x00000000
#define HW_OFFS_OCS_ECC_STATUS		0x00000004
#define HW_OFFS_OCS_ECC_DATA_IN		0x00000080
#define HW_OFFS_OCS_ECC_CX_DATA_OUT	0x00000100
#define HW_OFFS_OCS_ECC_CY_DATA_OUT	0x00000180
#define HW_OFFS_OCS_ECC_ISR		0x00000400
#define HW_OFFS_OCS_ECC_IER		0x00000404

#define HW_OCS_ECC_ISR_INT_STATUS_DONE	BIT(0)
#define HW_OCS_ECC_COMMAND_INS_BP	BIT(0)

#define HW_OCS_ECC_COMMAND_START_VAL	BIT(0)

#define OCS_ECC_OP_SIZE_384		BIT(8)
#define OCS_ECC_OP_SIZE_256		0

/* ECC Instruction : for ECC_COMMAND */
#define OCS_ECC_INST_WRITE_AX		(0x1 << HW_OCS_ECC_COMMAND_INS_BP)
#define OCS_ECC_INST_WRITE_AY		(0x2 << HW_OCS_ECC_COMMAND_INS_BP)
#define OCS_ECC_INST_WRITE_BX_D		(0x3 << HW_OCS_ECC_COMMAND_INS_BP)
#define OCS_ECC_INST_WRITE_BY_L		(0x4 << HW_OCS_ECC_COMMAND_INS_BP)
#define OCS_ECC_INST_WRITE_P		(0x5 << HW_OCS_ECC_COMMAND_INS_BP)
#define OCS_ECC_INST_WRITE_A		(0x6 << HW_OCS_ECC_COMMAND_INS_BP)
#define OCS_ECC_INST_CALC_D_IDX_A	(0x8 << HW_OCS_ECC_COMMAND_INS_BP)
#define OCS_ECC_INST_CALC_A_POW_B_MODP	(0xB << HW_OCS_ECC_COMMAND_INS_BP)
#define OCS_ECC_INST_CALC_A_MUL_B_MODP	(0xC  << HW_OCS_ECC_COMMAND_INS_BP)
#define OCS_ECC_INST_CALC_A_ADD_B_MODP	(0xD << HW_OCS_ECC_COMMAND_INS_BP)

#define ECC_ENABLE_INTR			1

#define POLL_USEC			100
#define TIMEOUT_USEC			10000

#define KMB_ECC_VLI_MAX_DIGITS		ECC_CURVE_NIST_P384_DIGITS
#define KMB_ECC_VLI_MAX_BYTES		(KMB_ECC_VLI_MAX_DIGITS \
					 << ECC_DIGITS_TO_BYTES_SHIFT)

#define POW_CUBE			3

/**
 * struct ocs_ecc_dev - ECC device context
 * @list: List of device contexts
 * @dev: OCS ECC device
 * @base_reg: IO base address of OCS ECC
 * @engine: Crypto engine for the device
 * @irq_done: IRQ done completion.
 * @irq: IRQ number
 */
struct ocs_ecc_dev {
	struct list_head list;
	struct device *dev;
	void __iomem *base_reg;
	struct crypto_engine *engine;
	struct completion irq_done;
	int irq;
};

/**
 * struct ocs_ecc_ctx - Transformation context.
 * @ecc_dev:	 The ECC driver associated with this context.
 * @curve:	 The elliptic curve used by this transformation.
 * @private_key: The private key.
 */
struct ocs_ecc_ctx {
	struct ocs_ecc_dev *ecc_dev;
	const struct ecc_curve *curve;
	u64 private_key[KMB_ECC_VLI_MAX_DIGITS];
};

/* Driver data. */
struct ocs_ecc_drv {
	struct list_head dev_list;
	spinlock_t lock;	/* Protects dev_list. */
};

/* Global variable holding the list of OCS ECC devices (only one expected). */
static struct ocs_ecc_drv ocs_ecc = {
	.dev_list = LIST_HEAD_INIT(ocs_ecc.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(ocs_ecc.lock),
};

/* Get OCS ECC tfm context from kpp_request. */
static inline struct ocs_ecc_ctx *kmb_ocs_ecc_tctx(struct kpp_request *req)
{
	return kpp_tfm_ctx(crypto_kpp_reqtfm(req));
}

/* Converts number of digits to number of bytes. */
static inline unsigned int digits_to_bytes(unsigned int n)
{
	return n << ECC_DIGITS_TO_BYTES_SHIFT;
}

/*
 * Wait for ECC idle i.e when an operation (other than write operations)
 * is done.
 */
static inline int ocs_ecc_wait_idle(struct ocs_ecc_dev *dev)
{
	u32 value;

	return readl_poll_timeout((dev->base_reg + HW_OFFS_OCS_ECC_STATUS),
				  value,
				  !(value & HW_OCS_ECC_ISR_INT_STATUS_DONE),
				  POLL_USEC, TIMEOUT_USEC);
}

static void ocs_ecc_cmd_start(struct ocs_ecc_dev *ecc_dev, u32 op_size)
{
	iowrite32(op_size | HW_OCS_ECC_COMMAND_START_VAL,
		  ecc_dev->base_reg + HW_OFFS_OCS_ECC_COMMAND);
}

/* Direct write of u32 buffer to ECC engine with associated instruction. */
static void ocs_ecc_write_cmd_and_data(struct ocs_ecc_dev *dev,
				       u32 op_size,
				       u32 inst,
				       const void *data_in,
				       size_t data_size)
{
	iowrite32(op_size | inst, dev->base_reg + HW_OFFS_OCS_ECC_COMMAND);

	/* MMIO Write src uint32 to dst. */
	memcpy_toio(dev->base_reg + HW_OFFS_OCS_ECC_DATA_IN, data_in,
		    data_size);
}

/* Start OCS ECC operation and wait for its completion. */
static int ocs_ecc_trigger_op(struct ocs_ecc_dev *ecc_dev, u32 op_size,
			      u32 inst)
{
	reinit_completion(&ecc_dev->irq_done);

	iowrite32(ECC_ENABLE_INTR, ecc_dev->base_reg + HW_OFFS_OCS_ECC_IER);
	iowrite32(op_size | inst, ecc_dev->base_reg + HW_OFFS_OCS_ECC_COMMAND);

	return wait_for_completion_interruptible(&ecc_dev->irq_done);
}

/**
 * ocs_ecc_read_cx_out() - Read the CX data output buffer.
 * @dev:	The OCS ECC device to read from.
 * @cx_out:	The buffer where to store the CX value. Must be at least
 *		@byte_count byte long.
 * @byte_count:	The amount of data to read.
 */
static inline void ocs_ecc_read_cx_out(struct ocs_ecc_dev *dev, void *cx_out,
				       size_t byte_count)
{
	memcpy_fromio(cx_out, dev->base_reg + HW_OFFS_OCS_ECC_CX_DATA_OUT,
		      byte_count);
}

/**
 * ocs_ecc_read_cy_out() - Read the CX data output buffer.
 * @dev:	The OCS ECC device to read from.
 * @cy_out:	The buffer where to store the CY value. Must be at least
 *		@byte_count byte long.
 * @byte_count:	The amount of data to read.
 */
static inline void ocs_ecc_read_cy_out(struct ocs_ecc_dev *dev, void *cy_out,
				       size_t byte_count)
{
	memcpy_fromio(cy_out, dev->base_reg + HW_OFFS_OCS_ECC_CY_DATA_OUT,
		      byte_count);
}

static struct ocs_ecc_dev *kmb_ocs_ecc_find_dev(struct ocs_ecc_ctx *tctx)
{
	if (tctx->ecc_dev)
		return tctx->ecc_dev;

	spin_lock(&ocs_ecc.lock);

	/* Only a single OCS device available. */
	tctx->ecc_dev = list_first_entry(&ocs_ecc.dev_list, struct ocs_ecc_dev,
					 list);

	spin_unlock(&ocs_ecc.lock);

	return tctx->ecc_dev;
}

/* Do point multiplication using OCS ECC HW. */
static int kmb_ecc_point_mult(struct ocs_ecc_dev *ecc_dev,
			      struct ecc_point *result,
			      const struct ecc_point *point,
			      u64 *scalar,
			      const struct ecc_curve *curve)
{
	u8 sca[KMB_ECC_VLI_MAX_BYTES]; /* Use the maximum data size. */
	u32 op_size = (curve->g.ndigits > ECC_CURVE_NIST_P256_DIGITS) ?
		      OCS_ECC_OP_SIZE_384 : OCS_ECC_OP_SIZE_256;
	size_t nbytes = digits_to_bytes(curve->g.ndigits);
	int rc = 0;

	/* Generate random nbytes for Simple and Differential SCA protection. */
	rc = crypto_get_default_rng();
	if (rc)
		return rc;

	rc = crypto_rng_get_bytes(crypto_default_rng, sca, nbytes);
	crypto_put_default_rng();
	if (rc)
		return rc;

	/* Wait engine to be idle before starting new operation. */
	rc = ocs_ecc_wait_idle(ecc_dev);
	if (rc)
		return rc;

	/* Send ecc_start pulse as well as indicating operation size. */
	ocs_ecc_cmd_start(ecc_dev, op_size);

	/* Write ax param; Base point (Gx). */
	ocs_ecc_write_cmd_and_data(ecc_dev, op_size, OCS_ECC_INST_WRITE_AX,
				   point->x, nbytes);

	/* Write ay param; Base point (Gy). */
	ocs_ecc_write_cmd_and_data(ecc_dev, op_size, OCS_ECC_INST_WRITE_AY,
				   point->y, nbytes);

	/*
	 * Write the private key into DATA_IN reg.
	 *
	 * Since DATA_IN register is used to write different values during the
	 * computation private Key value is overwritten with
	 * side-channel-resistance value.
	 */
	ocs_ecc_write_cmd_and_data(ecc_dev, op_size, OCS_ECC_INST_WRITE_BX_D,
				   scalar, nbytes);

	/* Write operand by/l. */
	ocs_ecc_write_cmd_and_data(ecc_dev, op_size, OCS_ECC_INST_WRITE_BY_L,
				   sca, nbytes);
	memzero_explicit(sca, sizeof(sca));

	/* Write p = curve prime(GF modulus). */
	ocs_ecc_write_cmd_and_data(ecc_dev, op_size, OCS_ECC_INST_WRITE_P,
				   curve->p, nbytes);

	/* Write a = curve coefficient. */
	ocs_ecc_write_cmd_and_data(ecc_dev, op_size, OCS_ECC_INST_WRITE_A,
				   curve->a, nbytes);

	/* Make hardware perform the multiplication. */
	rc = ocs_ecc_trigger_op(ecc_dev, op_size, OCS_ECC_INST_CALC_D_IDX_A);
	if (rc)
		return rc;

	/* Read result. */
	ocs_ecc_read_cx_out(ecc_dev, result->x, nbytes);
	ocs_ecc_read_cy_out(ecc_dev, result->y, nbytes);

	return 0;
}

/**
 * kmb_ecc_do_scalar_op() - Perform Scalar operation using OCS ECC HW.
 * @ecc_dev:	The OCS ECC device to use.
 * @scalar_out:	Where to store the output scalar.
 * @scalar_a:	Input scalar operand 'a'.
 * @scalar_b:	Input scalar operand 'b'
 * @curve:	The curve on which the operation is performed.
 * @ndigits:	The size of the operands (in digits).
 * @inst:	The operation to perform (as an OCS ECC instruction).
 *
 * Return:	0 on success, negative error code otherwise.
 */
static int kmb_ecc_do_scalar_op(struct ocs_ecc_dev *ecc_dev, u64 *scalar_out,
				const u64 *scalar_a, const u64 *scalar_b,
				const struct ecc_curve *curve,
				unsigned int ndigits, const u32 inst)
{
	u32 op_size = (ndigits > ECC_CURVE_NIST_P256_DIGITS) ?
		      OCS_ECC_OP_SIZE_384 : OCS_ECC_OP_SIZE_256;
	size_t nbytes = digits_to_bytes(ndigits);
	int rc;

	/* Wait engine to be idle before starting new operation. */
	rc = ocs_ecc_wait_idle(ecc_dev);
	if (rc)
		return rc;

	/* Send ecc_start pulse as well as indicating operation size. */
	ocs_ecc_cmd_start(ecc_dev, op_size);

	/* Write ax param (Base point (Gx).*/
	ocs_ecc_write_cmd_and_data(ecc_dev, op_size, OCS_ECC_INST_WRITE_AX,
				   scalar_a, nbytes);

	/* Write ay param Base point (Gy).*/
	ocs_ecc_write_cmd_and_data(ecc_dev, op_size, OCS_ECC_INST_WRITE_AY,
				   scalar_b, nbytes);

	/* Write p = curve prime(GF modulus).*/
	ocs_ecc_write_cmd_and_data(ecc_dev, op_size, OCS_ECC_INST_WRITE_P,
				   curve->p, nbytes);

	/* Give instruction A.B or A+B to ECC engine. */
	rc = ocs_ecc_trigger_op(ecc_dev, op_size, inst);
	if (rc)
		return rc;

	ocs_ecc_read_cx_out(ecc_dev, scalar_out, nbytes);

	if (vli_is_zero(scalar_out, ndigits))
		return -EINVAL;

	return 0;
}

/* SP800-56A section 5.6.2.3.4 partial verification: ephemeral keys only */
static int kmb_ocs_ecc_is_pubkey_valid_partial(struct ocs_ecc_dev *ecc_dev,
					       const struct ecc_curve *curve,
					       struct ecc_point *pk)
{
	u64 xxx[KMB_ECC_VLI_MAX_DIGITS] = { 0 };
	u64 yy[KMB_ECC_VLI_MAX_DIGITS] = { 0 };
	u64 w[KMB_ECC_VLI_MAX_DIGITS] = { 0 };
	int rc;

	if (WARN_ON(pk->ndigits != curve->g.ndigits))
		return -EINVAL;

	/* Check 1: Verify key is not the zero point. */
	if (ecc_point_is_zero(pk))
		return -EINVAL;

	/* Check 2: Verify key is in the range [0, p-1]. */
	if (vli_cmp(curve->p, pk->x, pk->ndigits) != 1)
		return -EINVAL;

	if (vli_cmp(curve->p, pk->y, pk->ndigits) != 1)
		return -EINVAL;

	/* Check 3: Verify that y^2 == (x^3 + a·x + b) mod p */

	 /* y^2 */
	/* Compute y^2 -> store in yy */
	rc = kmb_ecc_do_scalar_op(ecc_dev, yy, pk->y, pk->y, curve, pk->ndigits,
				  OCS_ECC_INST_CALC_A_MUL_B_MODP);
	if (rc)
		goto exit;

	/* x^3 */
	/* Assigning w = 3, used for calculating x^3. */
	w[0] = POW_CUBE;
	/* Load the next stage.*/
	rc = kmb_ecc_do_scalar_op(ecc_dev, xxx, pk->x, w, curve, pk->ndigits,
				  OCS_ECC_INST_CALC_A_POW_B_MODP);
	if (rc)
		goto exit;

	/* Do a*x -> store in w. */
	rc = kmb_ecc_do_scalar_op(ecc_dev, w, curve->a, pk->x, curve,
				  pk->ndigits,
				  OCS_ECC_INST_CALC_A_MUL_B_MODP);
	if (rc)
		goto exit;

	/* Do ax + b == w + b; store in w. */
	rc = kmb_ecc_do_scalar_op(ecc_dev, w, w, curve->b, curve,
				  pk->ndigits,
				  OCS_ECC_INST_CALC_A_ADD_B_MODP);
	if (rc)
		goto exit;

	/* x^3 + ax + b == x^3 + w -> store in w. */
	rc = kmb_ecc_do_scalar_op(ecc_dev, w, xxx, w, curve, pk->ndigits,
				  OCS_ECC_INST_CALC_A_ADD_B_MODP);
	if (rc)
		goto exit;

	/* Compare y^2 == x^3 + a·x + b. */
	rc = vli_cmp(yy, w, pk->ndigits);
	if (rc)
		rc = -EINVAL;

exit:
	memzero_explicit(xxx, sizeof(xxx));
	memzero_explicit(yy, sizeof(yy));
	memzero_explicit(w, sizeof(w));

	return rc;
}

/* SP800-56A section 5.6.2.3.3 full verification */
static int kmb_ocs_ecc_is_pubkey_valid_full(struct ocs_ecc_dev *ecc_dev,
					    const struct ecc_curve *curve,
					    struct ecc_point *pk)
{
	struct ecc_point *nQ;
	int rc;

	/* Checks 1 through 3 */
	rc = kmb_ocs_ecc_is_pubkey_valid_partial(ecc_dev, curve, pk);
	if (rc)
		return rc;

	/* Check 4: Verify that nQ is the zero point. */
	nQ = ecc_alloc_point(pk->ndigits);
	if (!nQ)
		return -ENOMEM;

	rc = kmb_ecc_point_mult(ecc_dev, nQ, pk, curve->n, curve);
	if (rc)
		goto exit;

	if (!ecc_point_is_zero(nQ))
		rc = -EINVAL;

exit:
	ecc_free_point(nQ);

	return rc;
}

static int kmb_ecc_is_key_valid(const struct ecc_curve *curve,
				const u64 *private_key, size_t private_key_len)
{
	size_t ndigits = curve->g.ndigits;
	u64 one[KMB_ECC_VLI_MAX_DIGITS] = {1};
	u64 res[KMB_ECC_VLI_MAX_DIGITS];

	if (private_key_len != digits_to_bytes(ndigits))
		return -EINVAL;

	if (!private_key)
		return -EINVAL;

	/* Make sure the private key is in the range [2, n-3]. */
	if (vli_cmp(one, private_key, ndigits) != -1)
		return -EINVAL;

	vli_sub(res, curve->n, one, ndigits);
	vli_sub(res, res, one, ndigits);
	if (vli_cmp(res, private_key, ndigits) != 1)
		return -EINVAL;

	return 0;
}

/*
 * ECC private keys are generated using the method of extra random bits,
 * equivalent to that described in FIPS 186-4, Appendix B.4.1.
 *
 * d = (c mod(n–1)) + 1    where c is a string of random bits, 64 bits longer
 *                         than requested
 * 0 <= c mod(n-1) <= n-2  and implies that
 * 1 <= d <= n-1
 *
 * This method generates a private key uniformly distributed in the range
 * [1, n-1].
 */
static int kmb_ecc_gen_privkey(const struct ecc_curve *curve, u64 *privkey)
{
	size_t nbytes = digits_to_bytes(curve->g.ndigits);
	u64 priv[KMB_ECC_VLI_MAX_DIGITS];
	size_t nbits;
	int rc;

	nbits = vli_num_bits(curve->n, curve->g.ndigits);

	/* Check that N is included in Table 1 of FIPS 186-4, section 6.1.1 */
	if (nbits < 160 || curve->g.ndigits > ARRAY_SIZE(priv))
		return -EINVAL;

	/*
	 * FIPS 186-4 recommends that the private key should be obtained from a
	 * RBG with a security strength equal to or greater than the security
	 * strength associated with N.
	 *
	 * The maximum security strength identified by NIST SP800-57pt1r4 for
	 * ECC is 256 (N >= 512).
	 *
	 * This condition is met by the default RNG because it selects a favored
	 * DRBG with a security strength of 256.
	 */
	if (crypto_get_default_rng())
		return -EFAULT;

	rc = crypto_rng_get_bytes(crypto_default_rng, (u8 *)priv, nbytes);
	crypto_put_default_rng();
	if (rc)
		goto cleanup;

	rc = kmb_ecc_is_key_valid(curve, priv, nbytes);
	if (rc)
		goto cleanup;

	ecc_swap_digits(priv, privkey, curve->g.ndigits);

cleanup:
	memzero_explicit(&priv, sizeof(priv));

	return rc;
}

static int kmb_ocs_ecdh_set_secret(struct crypto_kpp *tfm, const void *buf,
				   unsigned int len)
{
	struct ocs_ecc_ctx *tctx = kpp_tfm_ctx(tfm);
	struct ecdh params;
	int rc = 0;

	rc = crypto_ecdh_decode_key(buf, len, &params);
	if (rc)
		goto cleanup;

	/* Ensure key size is not bigger then expected. */
	if (params.key_size > digits_to_bytes(tctx->curve->g.ndigits)) {
		rc = -EINVAL;
		goto cleanup;
	}

	/* Auto-generate private key is not provided. */
	if (!params.key || !params.key_size) {
		rc = kmb_ecc_gen_privkey(tctx->curve, tctx->private_key);
		goto cleanup;
	}

	rc = kmb_ecc_is_key_valid(tctx->curve, (const u64 *)params.key,
				  params.key_size);
	if (rc)
		goto cleanup;

	ecc_swap_digits((const u64 *)params.key, tctx->private_key,
			tctx->curve->g.ndigits);
cleanup:
	memzero_explicit(&params, sizeof(params));

	if (rc)
		tctx->curve = NULL;

	return rc;
}

/* Compute shared secret. */
static int kmb_ecc_do_shared_secret(struct ocs_ecc_ctx *tctx,
				    struct kpp_request *req)
{
	struct ocs_ecc_dev *ecc_dev = tctx->ecc_dev;
	const struct ecc_curve *curve = tctx->curve;
	u64 shared_secret[KMB_ECC_VLI_MAX_DIGITS];
	u64 pubk_buf[KMB_ECC_VLI_MAX_DIGITS * 2];
	size_t copied, nbytes, pubk_len;
	struct ecc_point *pk, *result;
	int rc;

	nbytes = digits_to_bytes(curve->g.ndigits);

	/* Public key is a point, thus it has two coordinates */
	pubk_len = 2 * nbytes;

	/* Copy public key from SG list to pubk_buf. */
	copied = sg_copy_to_buffer(req->src,
				   sg_nents_for_len(req->src, pubk_len),
				   pubk_buf, pubk_len);
	if (copied != pubk_len)
		return -EINVAL;

	/* Allocate and initialize public key point. */
	pk = ecc_alloc_point(curve->g.ndigits);
	if (!pk)
		return -ENOMEM;

	ecc_swap_digits(pubk_buf, pk->x, curve->g.ndigits);
	ecc_swap_digits(&pubk_buf[curve->g.ndigits], pk->y, curve->g.ndigits);

	/*
	 * Check the public key for following
	 * Check 1: Verify key is not the zero point.
	 * Check 2: Verify key is in the range [1, p-1].
	 * Check 3: Verify that y^2 == (x^3 + a·x + b) mod p
	 */
	rc = kmb_ocs_ecc_is_pubkey_valid_partial(ecc_dev, curve, pk);
	if (rc)
		goto exit_free_pk;

	/* Allocate point for storing computed shared secret. */
	result = ecc_alloc_point(pk->ndigits);
	if (!result) {
		rc = -ENOMEM;
		goto exit_free_pk;
	}

	/* Calculate the shared secret.*/
	rc = kmb_ecc_point_mult(ecc_dev, result, pk, tctx->private_key, curve);
	if (rc)
		goto exit_free_result;

	if (ecc_point_is_zero(result)) {
		rc = -EFAULT;
		goto exit_free_result;
	}

	/* Copy shared secret from point to buffer. */
	ecc_swap_digits(result->x, shared_secret, result->ndigits);

	/* Request might ask for less bytes than what we have. */
	nbytes = min_t(size_t, nbytes, req->dst_len);

	copied = sg_copy_from_buffer(req->dst,
				     sg_nents_for_len(req->dst, nbytes),
				     shared_secret, nbytes);

	if (copied != nbytes)
		rc = -EINVAL;

	memzero_explicit(shared_secret, sizeof(shared_secret));

exit_free_result:
	ecc_free_point(result);

exit_free_pk:
	ecc_free_point(pk);

	return rc;
}

/* Compute public key. */
static int kmb_ecc_do_public_key(struct ocs_ecc_ctx *tctx,
				 struct kpp_request *req)
{
	const struct ecc_curve *curve = tctx->curve;
	u64 pubk_buf[KMB_ECC_VLI_MAX_DIGITS * 2];
	struct ecc_point *pk;
	size_t pubk_len;
	size_t copied;
	int rc;

	/* Public key is a point, so it has double the digits. */
	pubk_len = 2 * digits_to_bytes(curve->g.ndigits);

	pk = ecc_alloc_point(curve->g.ndigits);
	if (!pk)
		return -ENOMEM;

	/* Public Key(pk) = priv * G. */
	rc = kmb_ecc_point_mult(tctx->ecc_dev, pk, &curve->g, tctx->private_key,
				curve);
	if (rc)
		goto exit;

	/* SP800-56A rev 3 5.6.2.1.3 key check */
	if (kmb_ocs_ecc_is_pubkey_valid_full(tctx->ecc_dev, curve, pk)) {
		rc = -EAGAIN;
		goto exit;
	}

	/* Copy public key from point to buffer. */
	ecc_swap_digits(pk->x, pubk_buf, pk->ndigits);
	ecc_swap_digits(pk->y, &pubk_buf[pk->ndigits], pk->ndigits);

	/* Copy public key to req->dst. */
	copied = sg_copy_from_buffer(req->dst,
				     sg_nents_for_len(req->dst, pubk_len),
				     pubk_buf, pubk_len);

	if (copied != pubk_len)
		rc = -EINVAL;

exit:
	ecc_free_point(pk);

	return rc;
}

static int kmb_ocs_ecc_do_one_request(struct crypto_engine *engine,
				      void *areq)
{
	struct kpp_request *req = container_of(areq, struct kpp_request, base);
	struct ocs_ecc_ctx *tctx = kmb_ocs_ecc_tctx(req);
	struct ocs_ecc_dev *ecc_dev = tctx->ecc_dev;
	int rc;

	if (req->src)
		rc = kmb_ecc_do_shared_secret(tctx, req);
	else
		rc = kmb_ecc_do_public_key(tctx, req);

	crypto_finalize_kpp_request(ecc_dev->engine, req, rc);

	return 0;
}

static int kmb_ocs_ecdh_generate_public_key(struct kpp_request *req)
{
	struct ocs_ecc_ctx *tctx = kmb_ocs_ecc_tctx(req);
	const struct ecc_curve *curve = tctx->curve;

	/* Ensure kmb_ocs_ecdh_set_secret() has been successfully called. */
	if (!tctx->curve)
		return -EINVAL;

	/* Ensure dst is present. */
	if (!req->dst)
		return -EINVAL;

	/* Check the request dst is big enough to hold the public key. */
	if (req->dst_len < (2 * digits_to_bytes(curve->g.ndigits)))
		return -EINVAL;

	/* 'src' is not supposed to be present when generate pubk is called. */
	if (req->src)
		return -EINVAL;

	return crypto_transfer_kpp_request_to_engine(tctx->ecc_dev->engine,
						     req);
}

static int kmb_ocs_ecdh_compute_shared_secret(struct kpp_request *req)
{
	struct ocs_ecc_ctx *tctx = kmb_ocs_ecc_tctx(req);
	const struct ecc_curve *curve = tctx->curve;

	/* Ensure kmb_ocs_ecdh_set_secret() has been successfully called. */
	if (!tctx->curve)
		return -EINVAL;

	/* Ensure dst is present. */
	if (!req->dst)
		return -EINVAL;

	/* Ensure src is present. */
	if (!req->src)
		return -EINVAL;

	/*
	 * req->src is expected to the (other-side) public key, so its length
	 * must be 2 * coordinate size (in bytes).
	 */
	if (req->src_len != 2 * digits_to_bytes(curve->g.ndigits))
		return -EINVAL;

	return crypto_transfer_kpp_request_to_engine(tctx->ecc_dev->engine,
						     req);
}

static int kmb_ecc_tctx_init(struct ocs_ecc_ctx *tctx, unsigned int curve_id)
{
	memset(tctx, 0, sizeof(*tctx));

	tctx->ecc_dev = kmb_ocs_ecc_find_dev(tctx);

	if (IS_ERR(tctx->ecc_dev)) {
		pr_err("Failed to find the device : %ld\n",
		       PTR_ERR(tctx->ecc_dev));
		return PTR_ERR(tctx->ecc_dev);
	}

	tctx->curve = ecc_get_curve(curve_id);
	if (!tctx->curve)
		return -EOPNOTSUPP;

	return 0;
}

static int kmb_ocs_ecdh_nist_p256_init_tfm(struct crypto_kpp *tfm)
{
	struct ocs_ecc_ctx *tctx = kpp_tfm_ctx(tfm);

	return kmb_ecc_tctx_init(tctx, ECC_CURVE_NIST_P256);
}

static int kmb_ocs_ecdh_nist_p384_init_tfm(struct crypto_kpp *tfm)
{
	struct ocs_ecc_ctx *tctx = kpp_tfm_ctx(tfm);

	return kmb_ecc_tctx_init(tctx, ECC_CURVE_NIST_P384);
}

static void kmb_ocs_ecdh_exit_tfm(struct crypto_kpp *tfm)
{
	struct ocs_ecc_ctx *tctx = kpp_tfm_ctx(tfm);

	memzero_explicit(tctx->private_key, sizeof(*tctx->private_key));
}

static unsigned int kmb_ocs_ecdh_max_size(struct crypto_kpp *tfm)
{
	struct ocs_ecc_ctx *tctx = kpp_tfm_ctx(tfm);

	/* Public key is made of two coordinates, so double the digits. */
	return digits_to_bytes(tctx->curve->g.ndigits) * 2;
}

static struct kpp_engine_alg ocs_ecdh_p256 = {
	.base.set_secret = kmb_ocs_ecdh_set_secret,
	.base.generate_public_key = kmb_ocs_ecdh_generate_public_key,
	.base.compute_shared_secret = kmb_ocs_ecdh_compute_shared_secret,
	.base.init = kmb_ocs_ecdh_nist_p256_init_tfm,
	.base.exit = kmb_ocs_ecdh_exit_tfm,
	.base.max_size = kmb_ocs_ecdh_max_size,
	.base.base = {
		.cra_name = "ecdh-nist-p256",
		.cra_driver_name = "ecdh-nist-p256-keembay-ocs",
		.cra_priority = KMB_OCS_ECC_PRIORITY,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct ocs_ecc_ctx),
	},
	.op.do_one_request = kmb_ocs_ecc_do_one_request,
};

static struct kpp_engine_alg ocs_ecdh_p384 = {
	.base.set_secret = kmb_ocs_ecdh_set_secret,
	.base.generate_public_key = kmb_ocs_ecdh_generate_public_key,
	.base.compute_shared_secret = kmb_ocs_ecdh_compute_shared_secret,
	.base.init = kmb_ocs_ecdh_nist_p384_init_tfm,
	.base.exit = kmb_ocs_ecdh_exit_tfm,
	.base.max_size = kmb_ocs_ecdh_max_size,
	.base.base = {
		.cra_name = "ecdh-nist-p384",
		.cra_driver_name = "ecdh-nist-p384-keembay-ocs",
		.cra_priority = KMB_OCS_ECC_PRIORITY,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct ocs_ecc_ctx),
	},
	.op.do_one_request = kmb_ocs_ecc_do_one_request,
};

static irqreturn_t ocs_ecc_irq_handler(int irq, void *dev_id)
{
	struct ocs_ecc_dev *ecc_dev = dev_id;
	u32 status;

	/*
	 * Read the status register and write it back to clear the
	 * DONE_INT_STATUS bit.
	 */
	status = ioread32(ecc_dev->base_reg + HW_OFFS_OCS_ECC_ISR);
	iowrite32(status, ecc_dev->base_reg + HW_OFFS_OCS_ECC_ISR);

	if (!(status & HW_OCS_ECC_ISR_INT_STATUS_DONE))
		return IRQ_NONE;

	complete(&ecc_dev->irq_done);

	return IRQ_HANDLED;
}

static int kmb_ocs_ecc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ocs_ecc_dev *ecc_dev;
	int rc;

	ecc_dev = devm_kzalloc(dev, sizeof(*ecc_dev), GFP_KERNEL);
	if (!ecc_dev)
		return -ENOMEM;

	ecc_dev->dev = dev;

	platform_set_drvdata(pdev, ecc_dev);

	INIT_LIST_HEAD(&ecc_dev->list);
	init_completion(&ecc_dev->irq_done);

	/* Get base register address. */
	ecc_dev->base_reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ecc_dev->base_reg)) {
		dev_err(dev, "Failed to get base address\n");
		rc = PTR_ERR(ecc_dev->base_reg);
		goto list_del;
	}

	/* Get and request IRQ */
	ecc_dev->irq = platform_get_irq(pdev, 0);
	if (ecc_dev->irq < 0) {
		rc = ecc_dev->irq;
		goto list_del;
	}

	rc = devm_request_threaded_irq(dev, ecc_dev->irq, ocs_ecc_irq_handler,
				       NULL, 0, "keembay-ocs-ecc", ecc_dev);
	if (rc < 0) {
		dev_err(dev, "Could not request IRQ\n");
		goto list_del;
	}

	/* Add device to the list of OCS ECC devices. */
	spin_lock(&ocs_ecc.lock);
	list_add_tail(&ecc_dev->list, &ocs_ecc.dev_list);
	spin_unlock(&ocs_ecc.lock);

	/* Initialize crypto engine. */
	ecc_dev->engine = crypto_engine_alloc_init(dev, 1);
	if (!ecc_dev->engine) {
		dev_err(dev, "Could not allocate crypto engine\n");
		rc = -ENOMEM;
		goto list_del;
	}

	rc = crypto_engine_start(ecc_dev->engine);
	if (rc) {
		dev_err(dev, "Could not start crypto engine\n");
		goto cleanup;
	}

	/* Register the KPP algo. */
	rc = crypto_engine_register_kpp(&ocs_ecdh_p256);
	if (rc) {
		dev_err(dev,
			"Could not register OCS algorithms with Crypto API\n");
		goto cleanup;
	}

	rc = crypto_engine_register_kpp(&ocs_ecdh_p384);
	if (rc) {
		dev_err(dev,
			"Could not register OCS algorithms with Crypto API\n");
		goto ocs_ecdh_p384_error;
	}

	return 0;

ocs_ecdh_p384_error:
	crypto_engine_unregister_kpp(&ocs_ecdh_p256);

cleanup:
	crypto_engine_exit(ecc_dev->engine);

list_del:
	spin_lock(&ocs_ecc.lock);
	list_del(&ecc_dev->list);
	spin_unlock(&ocs_ecc.lock);

	return rc;
}

static void kmb_ocs_ecc_remove(struct platform_device *pdev)
{
	struct ocs_ecc_dev *ecc_dev;

	ecc_dev = platform_get_drvdata(pdev);

	crypto_engine_unregister_kpp(&ocs_ecdh_p384);
	crypto_engine_unregister_kpp(&ocs_ecdh_p256);

	spin_lock(&ocs_ecc.lock);
	list_del(&ecc_dev->list);
	spin_unlock(&ocs_ecc.lock);

	crypto_engine_exit(ecc_dev->engine);
}

/* Device tree driver match. */
static const struct of_device_id kmb_ocs_ecc_of_match[] = {
	{
		.compatible = "intel,keembay-ocs-ecc",
	},
	{}
};

/* The OCS driver is a platform device. */
static struct platform_driver kmb_ocs_ecc_driver = {
	.probe = kmb_ocs_ecc_probe,
	.remove = kmb_ocs_ecc_remove,
	.driver = {
			.name = DRV_NAME,
			.of_match_table = kmb_ocs_ecc_of_match,
		},
};
module_platform_driver(kmb_ocs_ecc_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel Keem Bay OCS ECC Driver");
MODULE_ALIAS_CRYPTO("ecdh-nist-p256");
MODULE_ALIAS_CRYPTO("ecdh-nist-p384");
MODULE_ALIAS_CRYPTO("ecdh-nist-p256-keembay-ocs");
MODULE_ALIAS_CRYPTO("ecdh-nist-p384-keembay-ocs");
