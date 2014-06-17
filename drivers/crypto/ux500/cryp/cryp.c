/**
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Shujuan Chen <shujuan.chen@stericsson.com> for ST-Ericsson.
 * Author: Jonas Linde <jonas.linde@stericsson.com> for ST-Ericsson.
 * Author: Niklas Hernaeus <niklas.hernaeus@stericsson.com> for ST-Ericsson.
 * Author: Joakim Bech <joakim.xx.bech@stericsson.com> for ST-Ericsson.
 * Author: Berne Hebark <berne.herbark@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "cryp_p.h"
#include "cryp.h"

/**
 * cryp_wait_until_done - wait until the device logic is not busy
 */
void cryp_wait_until_done(struct cryp_device_data *device_data)
{
	while (cryp_is_logic_busy(device_data))
		cpu_relax();
}

/**
 * cryp_check - This routine checks Peripheral and PCell Id
 * @device_data: Pointer to the device data struct for base address.
 */
int cryp_check(struct cryp_device_data *device_data)
{
	int peripheralid2 = 0;

	if (NULL == device_data)
		return -EINVAL;

	peripheralid2 = readl_relaxed(&device_data->base->periphId2);

	if (peripheralid2 != CRYP_PERIPHERAL_ID2_DB8500)
		return -EPERM;

	/* Check Peripheral and Pcell Id Register for CRYP */
	if ((CRYP_PERIPHERAL_ID0 ==
		readl_relaxed(&device_data->base->periphId0))
	    && (CRYP_PERIPHERAL_ID1 ==
		    readl_relaxed(&device_data->base->periphId1))
	    && (CRYP_PERIPHERAL_ID3 ==
		    readl_relaxed(&device_data->base->periphId3))
	    && (CRYP_PCELL_ID0 ==
		    readl_relaxed(&device_data->base->pcellId0))
	    && (CRYP_PCELL_ID1 ==
		    readl_relaxed(&device_data->base->pcellId1))
	    && (CRYP_PCELL_ID2 ==
		    readl_relaxed(&device_data->base->pcellId2))
	    && (CRYP_PCELL_ID3 ==
		    readl_relaxed(&device_data->base->pcellId3))) {
		return 0;
	}

	return -EPERM;
}

/**
 * cryp_activity - This routine enables/disable the cryptography function.
 * @device_data: Pointer to the device data struct for base address.
 * @cryp_crypen: Enable/Disable functionality
 */
void cryp_activity(struct cryp_device_data *device_data,
		   enum cryp_crypen cryp_crypen)
{
	CRYP_PUT_BITS(&device_data->base->cr,
		      cryp_crypen,
		      CRYP_CR_CRYPEN_POS,
		      CRYP_CR_CRYPEN_MASK);
}

/**
 * cryp_flush_inoutfifo - Resets both the input and the output FIFOs
 * @device_data: Pointer to the device data struct for base address.
 */
void cryp_flush_inoutfifo(struct cryp_device_data *device_data)
{
	/*
	 * We always need to disble the hardware before trying to flush the
	 * FIFO. This is something that isn't written in the design
	 * specification, but we have been informed by the hardware designers
	 * that this must be done.
	 */
	cryp_activity(device_data, CRYP_CRYPEN_DISABLE);
	cryp_wait_until_done(device_data);

	CRYP_SET_BITS(&device_data->base->cr, CRYP_CR_FFLUSH_MASK);
	/*
	 * CRYP_SR_INFIFO_READY_MASK is the expected value on the status
	 * register when starting a new calculation, which means Input FIFO is
	 * not full and input FIFO is empty.
	 */
	while (readl_relaxed(&device_data->base->sr) !=
	       CRYP_SR_INFIFO_READY_MASK)
		cpu_relax();
}

/**
 * cryp_set_configuration - This routine set the cr CRYP IP
 * @device_data: Pointer to the device data struct for base address.
 * @cryp_config: Pointer to the configuration parameter
 * @control_register: The control register to be written later on.
 */
int cryp_set_configuration(struct cryp_device_data *device_data,
			   struct cryp_config *cryp_config,
			   u32 *control_register)
{
	u32 cr_for_kse;

	if (NULL == device_data || NULL == cryp_config)
		return -EINVAL;

	*control_register |= (cryp_config->keysize << CRYP_CR_KEYSIZE_POS);

	/* Prepare key for decryption in AES_ECB and AES_CBC mode. */
	if ((CRYP_ALGORITHM_DECRYPT == cryp_config->algodir) &&
	    ((CRYP_ALGO_AES_ECB == cryp_config->algomode) ||
	     (CRYP_ALGO_AES_CBC == cryp_config->algomode))) {
		cr_for_kse = *control_register;
		/*
		 * This seems a bit odd, but it is indeed needed to set this to
		 * encrypt even though it is a decryption that we are doing. It
		 * also mentioned in the design spec that you need to do this.
		 * After the keyprepartion for decrypting is done you should set
		 * algodir back to decryption, which is done outside this if
		 * statement.
		 *
		 * According to design specification we should set mode ECB
		 * during key preparation even though we might be running CBC
		 * when enter this function.
		 *
		 * Writing to KSE_ENABLED will drop CRYPEN when key preparation
		 * is done. Therefore we need to set CRYPEN again outside this
		 * if statement when running decryption.
		 */
		cr_for_kse |= ((CRYP_ALGORITHM_ENCRYPT << CRYP_CR_ALGODIR_POS) |
			       (CRYP_ALGO_AES_ECB << CRYP_CR_ALGOMODE_POS) |
			       (CRYP_CRYPEN_ENABLE << CRYP_CR_CRYPEN_POS) |
			       (KSE_ENABLED << CRYP_CR_KSE_POS));

		writel_relaxed(cr_for_kse, &device_data->base->cr);
		cryp_wait_until_done(device_data);
	}

	*control_register |=
		((cryp_config->algomode << CRYP_CR_ALGOMODE_POS) |
		 (cryp_config->algodir << CRYP_CR_ALGODIR_POS));

	return 0;
}

/**
 * cryp_configure_protection - set the protection bits in the CRYP logic.
 * @device_data: Pointer to the device data struct for base address.
 * @p_protect_config:	Pointer to the protection mode and
 *			secure mode configuration
 */
int cryp_configure_protection(struct cryp_device_data *device_data,
			      struct cryp_protection_config *p_protect_config)
{
	if (NULL == p_protect_config)
		return -EINVAL;

	CRYP_WRITE_BIT(&device_data->base->cr,
		       (u32) p_protect_config->secure_access,
		       CRYP_CR_SECURE_MASK);
	CRYP_PUT_BITS(&device_data->base->cr,
		      p_protect_config->privilege_access,
		      CRYP_CR_PRLG_POS,
		      CRYP_CR_PRLG_MASK);

	return 0;
}

/**
 * cryp_is_logic_busy - returns the busy status of the CRYP logic
 * @device_data: Pointer to the device data struct for base address.
 */
int cryp_is_logic_busy(struct cryp_device_data *device_data)
{
	return CRYP_TEST_BITS(&device_data->base->sr,
			      CRYP_SR_BUSY_MASK);
}

/**
 * cryp_configure_for_dma - configures the CRYP IP for DMA operation
 * @device_data: Pointer to the device data struct for base address.
 * @dma_req: Specifies the DMA request type value.
 */
void cryp_configure_for_dma(struct cryp_device_data *device_data,
			    enum cryp_dma_req_type dma_req)
{
	CRYP_SET_BITS(&device_data->base->dmacr,
		      (u32) dma_req);
}

/**
 * cryp_configure_key_values - configures the key values for CRYP operations
 * @device_data: Pointer to the device data struct for base address.
 * @key_reg_index: Key value index register
 * @key_value: The key value struct
 */
int cryp_configure_key_values(struct cryp_device_data *device_data,
			      enum cryp_key_reg_index key_reg_index,
			      struct cryp_key_value key_value)
{
	while (cryp_is_logic_busy(device_data))
		cpu_relax();

	switch (key_reg_index) {
	case CRYP_KEY_REG_1:
		writel_relaxed(key_value.key_value_left,
				&device_data->base->key_1_l);
		writel_relaxed(key_value.key_value_right,
				&device_data->base->key_1_r);
		break;
	case CRYP_KEY_REG_2:
		writel_relaxed(key_value.key_value_left,
				&device_data->base->key_2_l);
		writel_relaxed(key_value.key_value_right,
				&device_data->base->key_2_r);
		break;
	case CRYP_KEY_REG_3:
		writel_relaxed(key_value.key_value_left,
				&device_data->base->key_3_l);
		writel_relaxed(key_value.key_value_right,
				&device_data->base->key_3_r);
		break;
	case CRYP_KEY_REG_4:
		writel_relaxed(key_value.key_value_left,
				&device_data->base->key_4_l);
		writel_relaxed(key_value.key_value_right,
				&device_data->base->key_4_r);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * cryp_configure_init_vector - configures the initialization vector register
 * @device_data: Pointer to the device data struct for base address.
 * @init_vector_index: Specifies the index of the init vector.
 * @init_vector_value: Specifies the value for the init vector.
 */
int cryp_configure_init_vector(struct cryp_device_data *device_data,
			       enum cryp_init_vector_index
			       init_vector_index,
			       struct cryp_init_vector_value
			       init_vector_value)
{
	while (cryp_is_logic_busy(device_data))
		cpu_relax();

	switch (init_vector_index) {
	case CRYP_INIT_VECTOR_INDEX_0:
		writel_relaxed(init_vector_value.init_value_left,
		       &device_data->base->init_vect_0_l);
		writel_relaxed(init_vector_value.init_value_right,
		       &device_data->base->init_vect_0_r);
		break;
	case CRYP_INIT_VECTOR_INDEX_1:
		writel_relaxed(init_vector_value.init_value_left,
		       &device_data->base->init_vect_1_l);
		writel_relaxed(init_vector_value.init_value_right,
		       &device_data->base->init_vect_1_r);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * cryp_save_device_context -	Store hardware registers and
 *				other device context parameter
 * @device_data: Pointer to the device data struct for base address.
 * @ctx: Crypto device context
 */
void cryp_save_device_context(struct cryp_device_data *device_data,
			      struct cryp_device_context *ctx,
			      int cryp_mode)
{
	enum cryp_algo_mode algomode;
	struct cryp_register __iomem *src_reg = device_data->base;
	struct cryp_config *config =
		(struct cryp_config *)device_data->current_ctx;

	/*
	 * Always start by disable the hardware and wait for it to finish the
	 * ongoing calculations before trying to reprogram it.
	 */
	cryp_activity(device_data, CRYP_CRYPEN_DISABLE);
	cryp_wait_until_done(device_data);

	if (cryp_mode == CRYP_MODE_DMA)
		cryp_configure_for_dma(device_data, CRYP_DMA_DISABLE_BOTH);

	if (CRYP_TEST_BITS(&src_reg->sr, CRYP_SR_IFEM_MASK) == 0)
		ctx->din = readl_relaxed(&src_reg->din);

	ctx->cr = readl_relaxed(&src_reg->cr) & CRYP_CR_CONTEXT_SAVE_MASK;

	switch (config->keysize) {
	case CRYP_KEY_SIZE_256:
		ctx->key_4_l = readl_relaxed(&src_reg->key_4_l);
		ctx->key_4_r = readl_relaxed(&src_reg->key_4_r);

	case CRYP_KEY_SIZE_192:
		ctx->key_3_l = readl_relaxed(&src_reg->key_3_l);
		ctx->key_3_r = readl_relaxed(&src_reg->key_3_r);

	case CRYP_KEY_SIZE_128:
		ctx->key_2_l = readl_relaxed(&src_reg->key_2_l);
		ctx->key_2_r = readl_relaxed(&src_reg->key_2_r);

	default:
		ctx->key_1_l = readl_relaxed(&src_reg->key_1_l);
		ctx->key_1_r = readl_relaxed(&src_reg->key_1_r);
	}

	/* Save IV for CBC mode for both AES and DES. */
	algomode = ((ctx->cr & CRYP_CR_ALGOMODE_MASK) >> CRYP_CR_ALGOMODE_POS);
	if (algomode == CRYP_ALGO_TDES_CBC ||
	    algomode == CRYP_ALGO_DES_CBC ||
	    algomode == CRYP_ALGO_AES_CBC) {
		ctx->init_vect_0_l = readl_relaxed(&src_reg->init_vect_0_l);
		ctx->init_vect_0_r = readl_relaxed(&src_reg->init_vect_0_r);
		ctx->init_vect_1_l = readl_relaxed(&src_reg->init_vect_1_l);
		ctx->init_vect_1_r = readl_relaxed(&src_reg->init_vect_1_r);
	}
}

/**
 * cryp_restore_device_context -	Restore hardware registers and
 *					other device context parameter
 * @device_data: Pointer to the device data struct for base address.
 * @ctx: Crypto device context
 */
void cryp_restore_device_context(struct cryp_device_data *device_data,
				 struct cryp_device_context *ctx)
{
	struct cryp_register __iomem *reg = device_data->base;
	struct cryp_config *config =
		(struct cryp_config *)device_data->current_ctx;

	/*
	 * Fall through for all items in switch statement. DES is captured in
	 * the default.
	 */
	switch (config->keysize) {
	case CRYP_KEY_SIZE_256:
		writel_relaxed(ctx->key_4_l, &reg->key_4_l);
		writel_relaxed(ctx->key_4_r, &reg->key_4_r);

	case CRYP_KEY_SIZE_192:
		writel_relaxed(ctx->key_3_l, &reg->key_3_l);
		writel_relaxed(ctx->key_3_r, &reg->key_3_r);

	case CRYP_KEY_SIZE_128:
		writel_relaxed(ctx->key_2_l, &reg->key_2_l);
		writel_relaxed(ctx->key_2_r, &reg->key_2_r);

	default:
		writel_relaxed(ctx->key_1_l, &reg->key_1_l);
		writel_relaxed(ctx->key_1_r, &reg->key_1_r);
	}

	/* Restore IV for CBC mode for AES and DES. */
	if (config->algomode == CRYP_ALGO_TDES_CBC ||
	    config->algomode == CRYP_ALGO_DES_CBC ||
	    config->algomode == CRYP_ALGO_AES_CBC) {
		writel_relaxed(ctx->init_vect_0_l, &reg->init_vect_0_l);
		writel_relaxed(ctx->init_vect_0_r, &reg->init_vect_0_r);
		writel_relaxed(ctx->init_vect_1_l, &reg->init_vect_1_l);
		writel_relaxed(ctx->init_vect_1_r, &reg->init_vect_1_r);
	}
}
