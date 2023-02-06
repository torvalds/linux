/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OP-TEE STM32MP BSEC PTA interface, used by STM32 ROMEM driver
 *
 * Copyright (C) 2022, STMicroelectronics - All Rights Reserved
 */

#if IS_ENABLED(CONFIG_NVMEM_STM32_BSEC_OPTEE_TA)
/**
 * stm32_bsec_optee_ta_open() - initialize the STM32 BSEC TA
 * @ctx: the OP-TEE context on success
 *
 * Return:
 *	On success, 0. On failure, -errno.
 */
int stm32_bsec_optee_ta_open(struct tee_context **ctx);

/**
 * stm32_bsec_optee_ta_close() - release the STM32 BSEC TA
 * @ctx: the OP-TEE context
 *
 * This function used to clean the OP-TEE resources initialized in
 * stm32_bsec_optee_ta_open(); it can be used as callback to
 * devm_add_action_or_reset()
 */
void stm32_bsec_optee_ta_close(void *ctx);

/**
 * stm32_bsec_optee_ta_read() - nvmem read access using TA client driver
 * @ctx: the OP-TEE context provided by stm32_bsec_optee_ta_open
 * @offset: nvmem offset
 * @buf: buffer to fill with nvem values
 * @bytes: number of bytes to read
 *
 * Return:
 *	On success, 0. On failure, -errno.
 */
int stm32_bsec_optee_ta_read(struct tee_context *ctx, unsigned int offset,
			     void *buf, size_t bytes);

/**
 * stm32_bsec_optee_ta_write() - nvmem write access using TA client driver
 * @ctx: the OP-TEE context provided by stm32_bsec_optee_ta_open
 * @lower: number of lower OTP, not protected by ECC
 * @offset: nvmem offset
 * @buf: buffer with nvem values
 * @bytes: number of bytes to write
 *
 * Return:
 *	On success, 0. On failure, -errno.
 */
int stm32_bsec_optee_ta_write(struct tee_context *ctx, unsigned int lower,
			      unsigned int offset, void *buf, size_t bytes);

#else

static inline int stm32_bsec_optee_ta_open(struct tee_context **ctx)
{
	return -EOPNOTSUPP;
}

static inline void stm32_bsec_optee_ta_close(void *ctx)
{
}

static inline int stm32_bsec_optee_ta_read(struct tee_context *ctx,
					   unsigned int offset, void *buf,
					   size_t bytes)
{
	return -EOPNOTSUPP;
}

static inline int stm32_bsec_optee_ta_write(struct tee_context *ctx,
					    unsigned int lower,
					    unsigned int offset, void *buf,
					    size_t bytes)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_NVMEM_STM32_BSEC_OPTEE_TA */
