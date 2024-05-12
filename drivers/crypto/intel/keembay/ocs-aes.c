// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay OCS AES Crypto Driver.
 *
 * Copyright (C) 2018-2020 Intel Corporation
 */

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/swab.h>

#include <asm/byteorder.h>
#include <asm/errno.h>

#include <crypto/aes.h>
#include <crypto/gcm.h>

#include "ocs-aes.h"

#define AES_COMMAND_OFFSET			0x0000
#define AES_KEY_0_OFFSET			0x0004
#define AES_KEY_1_OFFSET			0x0008
#define AES_KEY_2_OFFSET			0x000C
#define AES_KEY_3_OFFSET			0x0010
#define AES_KEY_4_OFFSET			0x0014
#define AES_KEY_5_OFFSET			0x0018
#define AES_KEY_6_OFFSET			0x001C
#define AES_KEY_7_OFFSET			0x0020
#define AES_IV_0_OFFSET				0x0024
#define AES_IV_1_OFFSET				0x0028
#define AES_IV_2_OFFSET				0x002C
#define AES_IV_3_OFFSET				0x0030
#define AES_ACTIVE_OFFSET			0x0034
#define AES_STATUS_OFFSET			0x0038
#define AES_KEY_SIZE_OFFSET			0x0044
#define AES_IER_OFFSET				0x0048
#define AES_ISR_OFFSET				0x005C
#define AES_MULTIPURPOSE1_0_OFFSET		0x0200
#define AES_MULTIPURPOSE1_1_OFFSET		0x0204
#define AES_MULTIPURPOSE1_2_OFFSET		0x0208
#define AES_MULTIPURPOSE1_3_OFFSET		0x020C
#define AES_MULTIPURPOSE2_0_OFFSET		0x0220
#define AES_MULTIPURPOSE2_1_OFFSET		0x0224
#define AES_MULTIPURPOSE2_2_OFFSET		0x0228
#define AES_MULTIPURPOSE2_3_OFFSET		0x022C
#define AES_BYTE_ORDER_CFG_OFFSET		0x02C0
#define AES_TLEN_OFFSET				0x0300
#define AES_T_MAC_0_OFFSET			0x0304
#define AES_T_MAC_1_OFFSET			0x0308
#define AES_T_MAC_2_OFFSET			0x030C
#define AES_T_MAC_3_OFFSET			0x0310
#define AES_PLEN_OFFSET				0x0314
#define AES_A_DMA_SRC_ADDR_OFFSET		0x0400
#define AES_A_DMA_DST_ADDR_OFFSET		0x0404
#define AES_A_DMA_SRC_SIZE_OFFSET		0x0408
#define AES_A_DMA_DST_SIZE_OFFSET		0x040C
#define AES_A_DMA_DMA_MODE_OFFSET		0x0410
#define AES_A_DMA_NEXT_SRC_DESCR_OFFSET		0x0418
#define AES_A_DMA_NEXT_DST_DESCR_OFFSET		0x041C
#define AES_A_DMA_WHILE_ACTIVE_MODE_OFFSET	0x0420
#define AES_A_DMA_LOG_OFFSET			0x0424
#define AES_A_DMA_STATUS_OFFSET			0x0428
#define AES_A_DMA_PERF_CNTR_OFFSET		0x042C
#define AES_A_DMA_MSI_ISR_OFFSET		0x0480
#define AES_A_DMA_MSI_IER_OFFSET		0x0484
#define AES_A_DMA_MSI_MASK_OFFSET		0x0488
#define AES_A_DMA_INBUFFER_WRITE_FIFO_OFFSET	0x0600
#define AES_A_DMA_OUTBUFFER_READ_FIFO_OFFSET	0x0700

/*
 * AES_A_DMA_DMA_MODE register.
 * Default: 0x00000000.
 * bit[31]	ACTIVE
 *		This bit activates the DMA. When the DMA finishes, it resets
 *		this bit to zero.
 * bit[30:26]	Unused by this driver.
 * bit[25]	SRC_LINK_LIST_EN
 *		Source link list enable bit. When the linked list is terminated
 *		this bit is reset by the DMA.
 * bit[24]	DST_LINK_LIST_EN
 *		Destination link list enable bit. When the linked list is
 *		terminated this bit is reset by the DMA.
 * bit[23:0]	Unused by this driver.
 */
#define AES_A_DMA_DMA_MODE_ACTIVE		BIT(31)
#define AES_A_DMA_DMA_MODE_SRC_LINK_LIST_EN	BIT(25)
#define AES_A_DMA_DMA_MODE_DST_LINK_LIST_EN	BIT(24)

/*
 * AES_ACTIVE register
 * default 0x00000000
 * bit[31:10]	Reserved
 * bit[9]	LAST_ADATA
 * bit[8]	LAST_GCX
 * bit[7:2]	Reserved
 * bit[1]	TERMINATION
 * bit[0]	TRIGGER
 */
#define AES_ACTIVE_LAST_ADATA			BIT(9)
#define AES_ACTIVE_LAST_CCM_GCM			BIT(8)
#define AES_ACTIVE_TERMINATION			BIT(1)
#define AES_ACTIVE_TRIGGER			BIT(0)

#define AES_DISABLE_INT				0x00000000
#define AES_DMA_CPD_ERR_INT			BIT(8)
#define AES_DMA_OUTBUF_RD_ERR_INT		BIT(7)
#define AES_DMA_OUTBUF_WR_ERR_INT		BIT(6)
#define AES_DMA_INBUF_RD_ERR_INT		BIT(5)
#define AES_DMA_INBUF_WR_ERR_INT		BIT(4)
#define AES_DMA_BAD_COMP_INT			BIT(3)
#define AES_DMA_SAI_INT				BIT(2)
#define AES_DMA_SRC_DONE_INT			BIT(0)
#define AES_COMPLETE_INT			BIT(1)

#define AES_DMA_MSI_MASK_CLEAR			BIT(0)

#define AES_128_BIT_KEY				0x00000000
#define AES_256_BIT_KEY				BIT(0)

#define AES_DEACTIVATE_PERF_CNTR		0x00000000
#define AES_ACTIVATE_PERF_CNTR			BIT(0)

#define AES_MAX_TAG_SIZE_U32			4

#define OCS_LL_DMA_FLAG_TERMINATE		BIT(31)

/*
 * There is an inconsistency in the documentation. This is documented as a
 * 11-bit value, but it is actually 10-bits.
 */
#define AES_DMA_STATUS_INPUT_BUFFER_OCCUPANCY_MASK	0x3FF

/*
 * During CCM decrypt, the OCS block needs to finish processing the ciphertext
 * before the tag is written. For 128-bit mode this required delay is 28 OCS
 * clock cycles. For 256-bit mode it is 36 OCS clock cycles.
 */
#define CCM_DECRYPT_DELAY_TAG_CLK_COUNT		36UL

/*
 * During CCM decrypt there must be a delay of at least 42 OCS clock cycles
 * between setting the TRIGGER bit in AES_ACTIVE and setting the LAST_CCM_GCM
 * bit in the same register (as stated in the OCS databook)
 */
#define CCM_DECRYPT_DELAY_LAST_GCX_CLK_COUNT	42UL

/* See RFC3610 section 2.2 */
#define L_PRIME_MIN (1)
#define L_PRIME_MAX (7)
/*
 * CCM IV format from RFC 3610 section 2.3
 *
 *   Octet Number   Contents
 *   ------------   ---------
 *   0              Flags
 *   1 ... 15-L     Nonce N
 *   16-L ... 15    Counter i
 *
 * Flags = L' = L - 1
 */
#define L_PRIME_IDX		0
#define COUNTER_START(lprime)	(16 - ((lprime) + 1))
#define COUNTER_LEN(lprime)	((lprime) + 1)

enum aes_counter_mode {
	AES_CTR_M_NO_INC = 0,
	AES_CTR_M_32_INC = 1,
	AES_CTR_M_64_INC = 2,
	AES_CTR_M_128_INC = 3,
};

/**
 * struct ocs_dma_linked_list - OCS DMA linked list entry.
 * @src_addr:   Source address of the data.
 * @src_len:    Length of data to be fetched.
 * @next:	Next dma_list to fetch.
 * @ll_flags:   Flags (Freeze @ terminate) for the DMA engine.
 */
struct ocs_dma_linked_list {
	u32 src_addr;
	u32 src_len;
	u32 next;
	u32 ll_flags;
} __packed;

/*
 * Set endianness of inputs and outputs
 * AES_BYTE_ORDER_CFG
 * default 0x00000000
 * bit [10] - KEY_HI_LO_SWAP
 * bit [9] - KEY_HI_SWAP_DWORDS_IN_OCTWORD
 * bit [8] - KEY_HI_SWAP_BYTES_IN_DWORD
 * bit [7] - KEY_LO_SWAP_DWORDS_IN_OCTWORD
 * bit [6] - KEY_LO_SWAP_BYTES_IN_DWORD
 * bit [5] - IV_SWAP_DWORDS_IN_OCTWORD
 * bit [4] - IV_SWAP_BYTES_IN_DWORD
 * bit [3] - DOUT_SWAP_DWORDS_IN_OCTWORD
 * bit [2] - DOUT_SWAP_BYTES_IN_DWORD
 * bit [1] - DOUT_SWAP_DWORDS_IN_OCTWORD
 * bit [0] - DOUT_SWAP_BYTES_IN_DWORD
 */
static inline void aes_a_set_endianness(const struct ocs_aes_dev *aes_dev)
{
	iowrite32(0x7FF, aes_dev->base_reg + AES_BYTE_ORDER_CFG_OFFSET);
}

/* Trigger AES process start. */
static inline void aes_a_op_trigger(const struct ocs_aes_dev *aes_dev)
{
	iowrite32(AES_ACTIVE_TRIGGER, aes_dev->base_reg + AES_ACTIVE_OFFSET);
}

/* Indicate last bulk of data. */
static inline void aes_a_op_termination(const struct ocs_aes_dev *aes_dev)
{
	iowrite32(AES_ACTIVE_TERMINATION,
		  aes_dev->base_reg + AES_ACTIVE_OFFSET);
}

/*
 * Set LAST_CCM_GCM in AES_ACTIVE register and clear all other bits.
 *
 * Called when DMA is programmed to fetch the last batch of data.
 * - For AES-CCM it is called for the last batch of Payload data and Ciphertext
 *   data.
 * - For AES-GCM, it is called for the last batch of Plaintext data and
 *   Ciphertext data.
 */
static inline void aes_a_set_last_gcx(const struct ocs_aes_dev *aes_dev)
{
	iowrite32(AES_ACTIVE_LAST_CCM_GCM,
		  aes_dev->base_reg + AES_ACTIVE_OFFSET);
}

/* Wait for LAST_CCM_GCM bit to be unset. */
static inline void aes_a_wait_last_gcx(const struct ocs_aes_dev *aes_dev)
{
	u32 aes_active_reg;

	do {
		aes_active_reg = ioread32(aes_dev->base_reg +
					  AES_ACTIVE_OFFSET);
	} while (aes_active_reg & AES_ACTIVE_LAST_CCM_GCM);
}

/* Wait for 10 bits of input occupancy. */
static void aes_a_dma_wait_input_buffer_occupancy(const struct ocs_aes_dev *aes_dev)
{
	u32 reg;

	do {
		reg = ioread32(aes_dev->base_reg + AES_A_DMA_STATUS_OFFSET);
	} while (reg & AES_DMA_STATUS_INPUT_BUFFER_OCCUPANCY_MASK);
}

 /*
  * Set LAST_CCM_GCM and LAST_ADATA bits in AES_ACTIVE register (and clear all
  * other bits).
  *
  * Called when DMA is programmed to fetch the last batch of Associated Data
  * (CCM case) or Additional Authenticated Data (GCM case).
  */
static inline void aes_a_set_last_gcx_and_adata(const struct ocs_aes_dev *aes_dev)
{
	iowrite32(AES_ACTIVE_LAST_ADATA | AES_ACTIVE_LAST_CCM_GCM,
		  aes_dev->base_reg + AES_ACTIVE_OFFSET);
}

/* Set DMA src and dst transfer size to 0 */
static inline void aes_a_dma_set_xfer_size_zero(const struct ocs_aes_dev *aes_dev)
{
	iowrite32(0, aes_dev->base_reg + AES_A_DMA_SRC_SIZE_OFFSET);
	iowrite32(0, aes_dev->base_reg + AES_A_DMA_DST_SIZE_OFFSET);
}

/* Activate DMA for zero-byte transfer case. */
static inline void aes_a_dma_active(const struct ocs_aes_dev *aes_dev)
{
	iowrite32(AES_A_DMA_DMA_MODE_ACTIVE,
		  aes_dev->base_reg + AES_A_DMA_DMA_MODE_OFFSET);
}

/* Activate DMA and enable src linked list */
static inline void aes_a_dma_active_src_ll_en(const struct ocs_aes_dev *aes_dev)
{
	iowrite32(AES_A_DMA_DMA_MODE_ACTIVE |
		  AES_A_DMA_DMA_MODE_SRC_LINK_LIST_EN,
		  aes_dev->base_reg + AES_A_DMA_DMA_MODE_OFFSET);
}

/* Activate DMA and enable dst linked list */
static inline void aes_a_dma_active_dst_ll_en(const struct ocs_aes_dev *aes_dev)
{
	iowrite32(AES_A_DMA_DMA_MODE_ACTIVE |
		  AES_A_DMA_DMA_MODE_DST_LINK_LIST_EN,
		  aes_dev->base_reg + AES_A_DMA_DMA_MODE_OFFSET);
}

/* Activate DMA and enable src and dst linked lists */
static inline void aes_a_dma_active_src_dst_ll_en(const struct ocs_aes_dev *aes_dev)
{
	iowrite32(AES_A_DMA_DMA_MODE_ACTIVE |
		  AES_A_DMA_DMA_MODE_SRC_LINK_LIST_EN |
		  AES_A_DMA_DMA_MODE_DST_LINK_LIST_EN,
		  aes_dev->base_reg + AES_A_DMA_DMA_MODE_OFFSET);
}

/* Reset PERF_CNTR to 0 and activate it */
static inline void aes_a_dma_reset_and_activate_perf_cntr(const struct ocs_aes_dev *aes_dev)
{
	iowrite32(0x00000000, aes_dev->base_reg + AES_A_DMA_PERF_CNTR_OFFSET);
	iowrite32(AES_ACTIVATE_PERF_CNTR,
		  aes_dev->base_reg + AES_A_DMA_WHILE_ACTIVE_MODE_OFFSET);
}

/* Wait until PERF_CNTR is > delay, then deactivate it */
static inline void aes_a_dma_wait_and_deactivate_perf_cntr(const struct ocs_aes_dev *aes_dev,
							   int delay)
{
	while (ioread32(aes_dev->base_reg + AES_A_DMA_PERF_CNTR_OFFSET) < delay)
		;
	iowrite32(AES_DEACTIVATE_PERF_CNTR,
		  aes_dev->base_reg + AES_A_DMA_WHILE_ACTIVE_MODE_OFFSET);
}

/* Disable AES and DMA IRQ. */
static void aes_irq_disable(struct ocs_aes_dev *aes_dev)
{
	u32 isr_val = 0;

	/* Disable interrupts */
	iowrite32(AES_DISABLE_INT,
		  aes_dev->base_reg + AES_A_DMA_MSI_IER_OFFSET);
	iowrite32(AES_DISABLE_INT, aes_dev->base_reg + AES_IER_OFFSET);

	/* Clear any pending interrupt */
	isr_val = ioread32(aes_dev->base_reg + AES_A_DMA_MSI_ISR_OFFSET);
	if (isr_val)
		iowrite32(isr_val,
			  aes_dev->base_reg + AES_A_DMA_MSI_ISR_OFFSET);

	isr_val = ioread32(aes_dev->base_reg + AES_A_DMA_MSI_MASK_OFFSET);
	if (isr_val)
		iowrite32(isr_val,
			  aes_dev->base_reg + AES_A_DMA_MSI_MASK_OFFSET);

	isr_val = ioread32(aes_dev->base_reg + AES_ISR_OFFSET);
	if (isr_val)
		iowrite32(isr_val, aes_dev->base_reg + AES_ISR_OFFSET);
}

/* Enable AES or DMA IRQ.  IRQ is disabled once fired. */
static void aes_irq_enable(struct ocs_aes_dev *aes_dev, u8 irq)
{
	if (irq == AES_COMPLETE_INT) {
		/* Ensure DMA error interrupts are enabled */
		iowrite32(AES_DMA_CPD_ERR_INT |
			  AES_DMA_OUTBUF_RD_ERR_INT |
			  AES_DMA_OUTBUF_WR_ERR_INT |
			  AES_DMA_INBUF_RD_ERR_INT |
			  AES_DMA_INBUF_WR_ERR_INT |
			  AES_DMA_BAD_COMP_INT |
			  AES_DMA_SAI_INT,
			  aes_dev->base_reg + AES_A_DMA_MSI_IER_OFFSET);
		/*
		 * AES_IER
		 * default 0x00000000
		 * bits [31:3] - reserved
		 * bit [2] - EN_SKS_ERR
		 * bit [1] - EN_AES_COMPLETE
		 * bit [0] - reserved
		 */
		iowrite32(AES_COMPLETE_INT, aes_dev->base_reg + AES_IER_OFFSET);
		return;
	}
	if (irq == AES_DMA_SRC_DONE_INT) {
		/* Ensure AES interrupts are disabled */
		iowrite32(AES_DISABLE_INT, aes_dev->base_reg + AES_IER_OFFSET);
		/*
		 * DMA_MSI_IER
		 * default 0x00000000
		 * bits [31:9] - reserved
		 * bit [8] - CPD_ERR_INT_EN
		 * bit [7] - OUTBUF_RD_ERR_INT_EN
		 * bit [6] - OUTBUF_WR_ERR_INT_EN
		 * bit [5] - INBUF_RD_ERR_INT_EN
		 * bit [4] - INBUF_WR_ERR_INT_EN
		 * bit [3] - BAD_COMP_INT_EN
		 * bit [2] - SAI_INT_EN
		 * bit [1] - DST_DONE_INT_EN
		 * bit [0] - SRC_DONE_INT_EN
		 */
		iowrite32(AES_DMA_CPD_ERR_INT |
			  AES_DMA_OUTBUF_RD_ERR_INT |
			  AES_DMA_OUTBUF_WR_ERR_INT |
			  AES_DMA_INBUF_RD_ERR_INT |
			  AES_DMA_INBUF_WR_ERR_INT |
			  AES_DMA_BAD_COMP_INT |
			  AES_DMA_SAI_INT |
			  AES_DMA_SRC_DONE_INT,
			  aes_dev->base_reg + AES_A_DMA_MSI_IER_OFFSET);
	}
}

/* Enable and wait for IRQ (either from OCS AES engine or DMA) */
static int ocs_aes_irq_enable_and_wait(struct ocs_aes_dev *aes_dev, u8 irq)
{
	int rc;

	reinit_completion(&aes_dev->irq_completion);
	aes_irq_enable(aes_dev, irq);
	rc = wait_for_completion_interruptible(&aes_dev->irq_completion);
	if (rc)
		return rc;

	return aes_dev->dma_err_mask ? -EIO : 0;
}

/* Configure DMA to OCS, linked list mode */
static inline void dma_to_ocs_aes_ll(struct ocs_aes_dev *aes_dev,
				     dma_addr_t dma_list)
{
	iowrite32(0, aes_dev->base_reg + AES_A_DMA_SRC_SIZE_OFFSET);
	iowrite32(dma_list,
		  aes_dev->base_reg + AES_A_DMA_NEXT_SRC_DESCR_OFFSET);
}

/* Configure DMA from OCS, linked list mode */
static inline void dma_from_ocs_aes_ll(struct ocs_aes_dev *aes_dev,
				       dma_addr_t dma_list)
{
	iowrite32(0, aes_dev->base_reg + AES_A_DMA_DST_SIZE_OFFSET);
	iowrite32(dma_list,
		  aes_dev->base_reg + AES_A_DMA_NEXT_DST_DESCR_OFFSET);
}

irqreturn_t ocs_aes_irq_handler(int irq, void *dev_id)
{
	struct ocs_aes_dev *aes_dev = dev_id;
	u32 aes_dma_isr;

	/* Read DMA ISR status. */
	aes_dma_isr = ioread32(aes_dev->base_reg + AES_A_DMA_MSI_ISR_OFFSET);

	/* Disable and clear interrupts. */
	aes_irq_disable(aes_dev);

	/* Save DMA error status. */
	aes_dev->dma_err_mask = aes_dma_isr &
				(AES_DMA_CPD_ERR_INT |
				 AES_DMA_OUTBUF_RD_ERR_INT |
				 AES_DMA_OUTBUF_WR_ERR_INT |
				 AES_DMA_INBUF_RD_ERR_INT |
				 AES_DMA_INBUF_WR_ERR_INT |
				 AES_DMA_BAD_COMP_INT |
				 AES_DMA_SAI_INT);

	/* Signal IRQ completion. */
	complete(&aes_dev->irq_completion);

	return IRQ_HANDLED;
}

/**
 * ocs_aes_set_key() - Write key into OCS AES hardware.
 * @aes_dev:	The OCS AES device to write the key to.
 * @key_size:	The size of the key (in bytes).
 * @key:	The key to write.
 * @cipher:	The cipher the key is for.
 *
 * For AES @key_size must be either 16 or 32. For SM4 @key_size must be 16.
 *
 * Return:	0 on success, negative error code otherwise.
 */
int ocs_aes_set_key(struct ocs_aes_dev *aes_dev, u32 key_size, const u8 *key,
		    enum ocs_cipher cipher)
{
	const u32 *key_u32;
	u32 val;
	int i;

	/* OCS AES supports 128-bit and 256-bit keys only. */
	if (cipher == OCS_AES && !(key_size == 32 || key_size == 16)) {
		dev_err(aes_dev->dev,
			"%d-bit keys not supported by AES cipher\n",
			key_size * 8);
		return -EINVAL;
	}
	/* OCS SM4 supports 128-bit keys only. */
	if (cipher == OCS_SM4 && key_size != 16) {
		dev_err(aes_dev->dev,
			"%d-bit keys not supported for SM4 cipher\n",
			key_size * 8);
		return -EINVAL;
	}

	if (!key)
		return -EINVAL;

	key_u32 = (const u32 *)key;

	/* Write key to AES_KEY[0-7] registers */
	for (i = 0; i < (key_size / sizeof(u32)); i++) {
		iowrite32(key_u32[i],
			  aes_dev->base_reg + AES_KEY_0_OFFSET +
			  (i * sizeof(u32)));
	}
	/*
	 * Write key size
	 * bits [31:1] - reserved
	 * bit [0] - AES_KEY_SIZE
	 *           0 - 128 bit key
	 *           1 - 256 bit key
	 */
	val = (key_size == 16) ? AES_128_BIT_KEY : AES_256_BIT_KEY;
	iowrite32(val, aes_dev->base_reg + AES_KEY_SIZE_OFFSET);

	return 0;
}

/* Write AES_COMMAND */
static inline void set_ocs_aes_command(struct ocs_aes_dev *aes_dev,
				       enum ocs_cipher cipher,
				       enum ocs_mode mode,
				       enum ocs_instruction instruction)
{
	u32 val;

	/* AES_COMMAND
	 * default 0x000000CC
	 * bit [14] - CIPHER_SELECT
	 *            0 - AES
	 *            1 - SM4
	 * bits [11:8] - OCS_AES_MODE
	 *               0000 - ECB
	 *               0001 - CBC
	 *               0010 - CTR
	 *               0110 - CCM
	 *               0111 - GCM
	 *               1001 - CTS
	 * bits [7:6] - AES_INSTRUCTION
	 *              00 - ENCRYPT
	 *              01 - DECRYPT
	 *              10 - EXPAND
	 *              11 - BYPASS
	 * bits [3:2] - CTR_M_BITS
	 *              00 - No increment
	 *              01 - Least significant 32 bits are incremented
	 *              10 - Least significant 64 bits are incremented
	 *              11 - Full 128 bits are incremented
	 */
	val = (cipher << 14) | (mode << 8) | (instruction << 6) |
	      (AES_CTR_M_128_INC << 2);
	iowrite32(val, aes_dev->base_reg + AES_COMMAND_OFFSET);
}

static void ocs_aes_init(struct ocs_aes_dev *aes_dev,
			 enum ocs_mode mode,
			 enum ocs_cipher cipher,
			 enum ocs_instruction instruction)
{
	/* Ensure interrupts are disabled and pending interrupts cleared. */
	aes_irq_disable(aes_dev);

	/* Set endianness recommended by data-sheet. */
	aes_a_set_endianness(aes_dev);

	/* Set AES_COMMAND register. */
	set_ocs_aes_command(aes_dev, cipher, mode, instruction);
}

/*
 * Write the byte length of the last AES/SM4 block of Payload data (without
 * zero padding and without the length of the MAC) in register AES_PLEN.
 */
static inline void ocs_aes_write_last_data_blk_len(struct ocs_aes_dev *aes_dev,
						   u32 size)
{
	u32 val;

	if (size == 0) {
		val = 0;
		goto exit;
	}

	val = size % AES_BLOCK_SIZE;
	if (val == 0)
		val = AES_BLOCK_SIZE;

exit:
	iowrite32(val, aes_dev->base_reg + AES_PLEN_OFFSET);
}

/*
 * Validate inputs according to mode.
 * If OK return 0; else return -EINVAL.
 */
static int ocs_aes_validate_inputs(dma_addr_t src_dma_list, u32 src_size,
				   const u8 *iv, u32 iv_size,
				   dma_addr_t aad_dma_list, u32 aad_size,
				   const u8 *tag, u32 tag_size,
				   enum ocs_cipher cipher, enum ocs_mode mode,
				   enum ocs_instruction instruction,
				   dma_addr_t dst_dma_list)
{
	/* Ensure cipher, mode and instruction are valid. */
	if (!(cipher == OCS_AES || cipher == OCS_SM4))
		return -EINVAL;

	if (mode != OCS_MODE_ECB && mode != OCS_MODE_CBC &&
	    mode != OCS_MODE_CTR && mode != OCS_MODE_CCM &&
	    mode != OCS_MODE_GCM && mode != OCS_MODE_CTS)
		return -EINVAL;

	if (instruction != OCS_ENCRYPT && instruction != OCS_DECRYPT &&
	    instruction != OCS_EXPAND  && instruction != OCS_BYPASS)
		return -EINVAL;

	/*
	 * When instruction is OCS_BYPASS, OCS simply copies data from source
	 * to destination using DMA.
	 *
	 * AES mode is irrelevant, but both source and destination DMA
	 * linked-list must be defined.
	 */
	if (instruction == OCS_BYPASS) {
		if (src_dma_list == DMA_MAPPING_ERROR ||
		    dst_dma_list == DMA_MAPPING_ERROR)
			return -EINVAL;

		return 0;
	}

	/*
	 * For performance reasons switch based on mode to limit unnecessary
	 * conditionals for each mode
	 */
	switch (mode) {
	case OCS_MODE_ECB:
		/* Ensure input length is multiple of block size */
		if (src_size % AES_BLOCK_SIZE != 0)
			return -EINVAL;

		/* Ensure source and destination linked lists are created */
		if (src_dma_list == DMA_MAPPING_ERROR ||
		    dst_dma_list == DMA_MAPPING_ERROR)
			return -EINVAL;

		return 0;

	case OCS_MODE_CBC:
		/* Ensure input length is multiple of block size */
		if (src_size % AES_BLOCK_SIZE != 0)
			return -EINVAL;

		/* Ensure source and destination linked lists are created */
		if (src_dma_list == DMA_MAPPING_ERROR ||
		    dst_dma_list == DMA_MAPPING_ERROR)
			return -EINVAL;

		/* Ensure IV is present and block size in length */
		if (!iv || iv_size != AES_BLOCK_SIZE)
			return -EINVAL;

		return 0;

	case OCS_MODE_CTR:
		/* Ensure input length of 1 byte or greater */
		if (src_size == 0)
			return -EINVAL;

		/* Ensure source and destination linked lists are created */
		if (src_dma_list == DMA_MAPPING_ERROR ||
		    dst_dma_list == DMA_MAPPING_ERROR)
			return -EINVAL;

		/* Ensure IV is present and block size in length */
		if (!iv || iv_size != AES_BLOCK_SIZE)
			return -EINVAL;

		return 0;

	case OCS_MODE_CTS:
		/* Ensure input length >= block size */
		if (src_size < AES_BLOCK_SIZE)
			return -EINVAL;

		/* Ensure source and destination linked lists are created */
		if (src_dma_list == DMA_MAPPING_ERROR ||
		    dst_dma_list == DMA_MAPPING_ERROR)
			return -EINVAL;

		/* Ensure IV is present and block size in length */
		if (!iv || iv_size != AES_BLOCK_SIZE)
			return -EINVAL;

		return 0;

	case OCS_MODE_GCM:
		/* Ensure IV is present and GCM_AES_IV_SIZE in length */
		if (!iv || iv_size != GCM_AES_IV_SIZE)
			return -EINVAL;

		/*
		 * If input data present ensure source and destination linked
		 * lists are created
		 */
		if (src_size && (src_dma_list == DMA_MAPPING_ERROR ||
				 dst_dma_list == DMA_MAPPING_ERROR))
			return -EINVAL;

		/* If aad present ensure aad linked list is created */
		if (aad_size && aad_dma_list == DMA_MAPPING_ERROR)
			return -EINVAL;

		/* Ensure tag destination is set */
		if (!tag)
			return -EINVAL;

		/* Just ensure that tag_size doesn't cause overflows. */
		if (tag_size > (AES_MAX_TAG_SIZE_U32 * sizeof(u32)))
			return -EINVAL;

		return 0;

	case OCS_MODE_CCM:
		/* Ensure IV is present and block size in length */
		if (!iv || iv_size != AES_BLOCK_SIZE)
			return -EINVAL;

		/* 2 <= L <= 8, so 1 <= L' <= 7 */
		if (iv[L_PRIME_IDX] < L_PRIME_MIN ||
		    iv[L_PRIME_IDX] > L_PRIME_MAX)
			return -EINVAL;

		/* If aad present ensure aad linked list is created */
		if (aad_size && aad_dma_list == DMA_MAPPING_ERROR)
			return -EINVAL;

		/* Just ensure that tag_size doesn't cause overflows. */
		if (tag_size > (AES_MAX_TAG_SIZE_U32 * sizeof(u32)))
			return -EINVAL;

		if (instruction == OCS_DECRYPT) {
			/*
			 * If input data present ensure source and destination
			 * linked lists are created
			 */
			if (src_size && (src_dma_list == DMA_MAPPING_ERROR ||
					 dst_dma_list == DMA_MAPPING_ERROR))
				return -EINVAL;

			/* Ensure input tag is present */
			if (!tag)
				return -EINVAL;

			return 0;
		}

		/* Instruction == OCS_ENCRYPT */

		/*
		 * Destination linked list always required (for tag even if no
		 * input data)
		 */
		if (dst_dma_list == DMA_MAPPING_ERROR)
			return -EINVAL;

		/* If input data present ensure src linked list is created */
		if (src_size && src_dma_list == DMA_MAPPING_ERROR)
			return -EINVAL;

		return 0;

	default:
		return -EINVAL;
	}
}

/**
 * ocs_aes_op() - Perform AES/SM4 operation.
 * @aes_dev:		The OCS AES device to use.
 * @mode:		The mode to use (ECB, CBC, CTR, or CTS).
 * @cipher:		The cipher to use (AES or SM4).
 * @instruction:	The instruction to perform (encrypt or decrypt).
 * @dst_dma_list:	The OCS DMA list mapping output memory.
 * @src_dma_list:	The OCS DMA list mapping input payload data.
 * @src_size:		The amount of data mapped by @src_dma_list.
 * @iv:			The IV vector.
 * @iv_size:		The size (in bytes) of @iv.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int ocs_aes_op(struct ocs_aes_dev *aes_dev,
	       enum ocs_mode mode,
	       enum ocs_cipher cipher,
	       enum ocs_instruction instruction,
	       dma_addr_t dst_dma_list,
	       dma_addr_t src_dma_list,
	       u32 src_size,
	       u8 *iv,
	       u32 iv_size)
{
	u32 *iv32;
	int rc;

	rc = ocs_aes_validate_inputs(src_dma_list, src_size, iv, iv_size, 0, 0,
				     NULL, 0, cipher, mode, instruction,
				     dst_dma_list);
	if (rc)
		return rc;
	/*
	 * ocs_aes_validate_inputs() is a generic check, now ensure mode is not
	 * GCM or CCM.
	 */
	if (mode == OCS_MODE_GCM || mode == OCS_MODE_CCM)
		return -EINVAL;

	/* Cast IV to u32 array. */
	iv32 = (u32 *)iv;

	ocs_aes_init(aes_dev, mode, cipher, instruction);

	if (mode == OCS_MODE_CTS) {
		/* Write the byte length of the last data block to engine. */
		ocs_aes_write_last_data_blk_len(aes_dev, src_size);
	}

	/* ECB is the only mode that doesn't use IV. */
	if (mode != OCS_MODE_ECB) {
		iowrite32(iv32[0], aes_dev->base_reg + AES_IV_0_OFFSET);
		iowrite32(iv32[1], aes_dev->base_reg + AES_IV_1_OFFSET);
		iowrite32(iv32[2], aes_dev->base_reg + AES_IV_2_OFFSET);
		iowrite32(iv32[3], aes_dev->base_reg + AES_IV_3_OFFSET);
	}

	/* Set AES_ACTIVE.TRIGGER to start the operation. */
	aes_a_op_trigger(aes_dev);

	/* Configure and activate input / output DMA. */
	dma_to_ocs_aes_ll(aes_dev, src_dma_list);
	dma_from_ocs_aes_ll(aes_dev, dst_dma_list);
	aes_a_dma_active_src_dst_ll_en(aes_dev);

	if (mode == OCS_MODE_CTS) {
		/*
		 * For CTS mode, instruct engine to activate ciphertext
		 * stealing if last block of data is incomplete.
		 */
		aes_a_set_last_gcx(aes_dev);
	} else {
		/* For all other modes, just write the 'termination' bit. */
		aes_a_op_termination(aes_dev);
	}

	/* Wait for engine to complete processing. */
	rc = ocs_aes_irq_enable_and_wait(aes_dev, AES_COMPLETE_INT);
	if (rc)
		return rc;

	if (mode == OCS_MODE_CTR) {
		/* Read back IV for streaming mode */
		iv32[0] = ioread32(aes_dev->base_reg + AES_IV_0_OFFSET);
		iv32[1] = ioread32(aes_dev->base_reg + AES_IV_1_OFFSET);
		iv32[2] = ioread32(aes_dev->base_reg + AES_IV_2_OFFSET);
		iv32[3] = ioread32(aes_dev->base_reg + AES_IV_3_OFFSET);
	}

	return 0;
}

/* Compute and write J0 to engine registers. */
static void ocs_aes_gcm_write_j0(const struct ocs_aes_dev *aes_dev,
				 const u8 *iv)
{
	const u32 *j0 = (u32 *)iv;

	/*
	 * IV must be 12 bytes; Other sizes not supported as Linux crypto API
	 * does only expects/allows 12 byte IV for GCM
	 */
	iowrite32(0x00000001, aes_dev->base_reg + AES_IV_0_OFFSET);
	iowrite32(__swab32(j0[2]), aes_dev->base_reg + AES_IV_1_OFFSET);
	iowrite32(__swab32(j0[1]), aes_dev->base_reg + AES_IV_2_OFFSET);
	iowrite32(__swab32(j0[0]), aes_dev->base_reg + AES_IV_3_OFFSET);
}

/* Read GCM tag from engine registers. */
static inline void ocs_aes_gcm_read_tag(struct ocs_aes_dev *aes_dev,
					u8 *tag, u32 tag_size)
{
	u32 tag_u32[AES_MAX_TAG_SIZE_U32];

	/*
	 * The Authentication Tag T is stored in Little Endian order in the
	 * registers with the most significant bytes stored from AES_T_MAC[3]
	 * downward.
	 */
	tag_u32[0] = __swab32(ioread32(aes_dev->base_reg + AES_T_MAC_3_OFFSET));
	tag_u32[1] = __swab32(ioread32(aes_dev->base_reg + AES_T_MAC_2_OFFSET));
	tag_u32[2] = __swab32(ioread32(aes_dev->base_reg + AES_T_MAC_1_OFFSET));
	tag_u32[3] = __swab32(ioread32(aes_dev->base_reg + AES_T_MAC_0_OFFSET));

	memcpy(tag, tag_u32, tag_size);
}

/**
 * ocs_aes_gcm_op() - Perform GCM operation.
 * @aes_dev:		The OCS AES device to use.
 * @cipher:		The Cipher to use (AES or SM4).
 * @instruction:	The instruction to perform (encrypt or decrypt).
 * @dst_dma_list:	The OCS DMA list mapping output memory.
 * @src_dma_list:	The OCS DMA list mapping input payload data.
 * @src_size:		The amount of data mapped by @src_dma_list.
 * @iv:			The input IV vector.
 * @aad_dma_list:	The OCS DMA list mapping input AAD data.
 * @aad_size:		The amount of data mapped by @aad_dma_list.
 * @out_tag:		Where to store computed tag.
 * @tag_size:		The size (in bytes) of @out_tag.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int ocs_aes_gcm_op(struct ocs_aes_dev *aes_dev,
		   enum ocs_cipher cipher,
		   enum ocs_instruction instruction,
		   dma_addr_t dst_dma_list,
		   dma_addr_t src_dma_list,
		   u32 src_size,
		   const u8 *iv,
		   dma_addr_t aad_dma_list,
		   u32 aad_size,
		   u8 *out_tag,
		   u32 tag_size)
{
	u64 bit_len;
	u32 val;
	int rc;

	rc = ocs_aes_validate_inputs(src_dma_list, src_size, iv,
				     GCM_AES_IV_SIZE, aad_dma_list,
				     aad_size, out_tag, tag_size, cipher,
				     OCS_MODE_GCM, instruction,
				     dst_dma_list);
	if (rc)
		return rc;

	ocs_aes_init(aes_dev, OCS_MODE_GCM, cipher, instruction);

	/* Compute and write J0 to OCS HW. */
	ocs_aes_gcm_write_j0(aes_dev, iv);

	/* Write out_tag byte length */
	iowrite32(tag_size, aes_dev->base_reg + AES_TLEN_OFFSET);

	/* Write the byte length of the last plaintext / ciphertext block. */
	ocs_aes_write_last_data_blk_len(aes_dev, src_size);

	/* Write ciphertext bit length */
	bit_len = (u64)src_size * 8;
	val = bit_len & 0xFFFFFFFF;
	iowrite32(val, aes_dev->base_reg + AES_MULTIPURPOSE2_0_OFFSET);
	val = bit_len >> 32;
	iowrite32(val, aes_dev->base_reg + AES_MULTIPURPOSE2_1_OFFSET);

	/* Write aad bit length */
	bit_len = (u64)aad_size * 8;
	val = bit_len & 0xFFFFFFFF;
	iowrite32(val, aes_dev->base_reg + AES_MULTIPURPOSE2_2_OFFSET);
	val = bit_len >> 32;
	iowrite32(val, aes_dev->base_reg + AES_MULTIPURPOSE2_3_OFFSET);

	/* Set AES_ACTIVE.TRIGGER to start the operation. */
	aes_a_op_trigger(aes_dev);

	/* Process AAD. */
	if (aad_size) {
		/* If aad present, configure DMA to feed it to the engine. */
		dma_to_ocs_aes_ll(aes_dev, aad_dma_list);
		aes_a_dma_active_src_ll_en(aes_dev);

		/* Instructs engine to pad last block of aad, if needed. */
		aes_a_set_last_gcx_and_adata(aes_dev);

		/* Wait for DMA transfer to complete. */
		rc = ocs_aes_irq_enable_and_wait(aes_dev, AES_DMA_SRC_DONE_INT);
		if (rc)
			return rc;
	} else {
		aes_a_set_last_gcx_and_adata(aes_dev);
	}

	/* Wait until adata (if present) has been processed. */
	aes_a_wait_last_gcx(aes_dev);
	aes_a_dma_wait_input_buffer_occupancy(aes_dev);

	/* Now process payload. */
	if (src_size) {
		/* Configure and activate DMA for both input and output data. */
		dma_to_ocs_aes_ll(aes_dev, src_dma_list);
		dma_from_ocs_aes_ll(aes_dev, dst_dma_list);
		aes_a_dma_active_src_dst_ll_en(aes_dev);
	} else {
		aes_a_dma_set_xfer_size_zero(aes_dev);
		aes_a_dma_active(aes_dev);
	}

	/* Instruct AES/SMA4 engine payload processing is over. */
	aes_a_set_last_gcx(aes_dev);

	/* Wait for OCS AES engine to complete processing. */
	rc = ocs_aes_irq_enable_and_wait(aes_dev, AES_COMPLETE_INT);
	if (rc)
		return rc;

	ocs_aes_gcm_read_tag(aes_dev, out_tag, tag_size);

	return 0;
}

/* Write encrypted tag to AES/SM4 engine. */
static void ocs_aes_ccm_write_encrypted_tag(struct ocs_aes_dev *aes_dev,
					    const u8 *in_tag, u32 tag_size)
{
	int i;

	/* Ensure DMA input buffer is empty */
	aes_a_dma_wait_input_buffer_occupancy(aes_dev);

	/*
	 * During CCM decrypt, the OCS block needs to finish processing the
	 * ciphertext before the tag is written.  So delay needed after DMA has
	 * completed writing the ciphertext
	 */
	aes_a_dma_reset_and_activate_perf_cntr(aes_dev);
	aes_a_dma_wait_and_deactivate_perf_cntr(aes_dev,
						CCM_DECRYPT_DELAY_TAG_CLK_COUNT);

	/* Write encrypted tag to AES/SM4 engine. */
	for (i = 0; i < tag_size; i++) {
		iowrite8(in_tag[i], aes_dev->base_reg +
				    AES_A_DMA_INBUFFER_WRITE_FIFO_OFFSET);
	}
}

/*
 * Write B0 CCM block to OCS AES HW.
 *
 * Note: B0 format is documented in NIST Special Publication 800-38C
 * https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38c.pdf
 * (see Section A.2.1)
 */
static int ocs_aes_ccm_write_b0(const struct ocs_aes_dev *aes_dev,
				const u8 *iv, u32 adata_size, u32 tag_size,
				u32 cryptlen)
{
	u8 b0[16]; /* CCM B0 block is 16 bytes long. */
	int i, q;

	/* Initialize B0 to 0. */
	memset(b0, 0, sizeof(b0));

	/*
	 * B0[0] is the 'Flags Octet' and has the following structure:
	 *   bit 7: Reserved
	 *   bit 6: Adata flag
	 *   bit 5-3: t value encoded as (t-2)/2
	 *   bit 2-0: q value encoded as q - 1
	 */
	/* If there is AAD data, set the Adata flag. */
	if (adata_size)
		b0[0] |= BIT(6);
	/*
	 * t denotes the octet length of T.
	 * t can only be an element of { 4, 6, 8, 10, 12, 14, 16} and is
	 * encoded as (t - 2) / 2
	 */
	b0[0] |= (((tag_size - 2) / 2) & 0x7)  << 3;
	/*
	 * q is the octet length of Q.
	 * q can only be an element of {2, 3, 4, 5, 6, 7, 8} and is encoded as
	 * q - 1 == iv[0] & 0x7;
	 */
	b0[0] |= iv[0] & 0x7;
	/*
	 * Copy the Nonce N from IV to B0; N is located in iv[1]..iv[15 - q]
	 * and must be copied to b0[1]..b0[15-q].
	 * q == (iv[0] & 0x7) + 1
	 */
	q = (iv[0] & 0x7) + 1;
	for (i = 1; i <= 15 - q; i++)
		b0[i] = iv[i];
	/*
	 * The rest of B0 must contain Q, i.e., the message length.
	 * Q is encoded in q octets, in big-endian order, so to write it, we
	 * start from the end of B0 and we move backward.
	 */
	i = sizeof(b0) - 1;
	while (q) {
		b0[i] = cryptlen & 0xff;
		cryptlen >>= 8;
		i--;
		q--;
	}
	/*
	 * If cryptlen is not zero at this point, it means that its original
	 * value was too big.
	 */
	if (cryptlen)
		return -EOVERFLOW;
	/* Now write B0 to OCS AES input buffer. */
	for (i = 0; i < sizeof(b0); i++)
		iowrite8(b0[i], aes_dev->base_reg +
				AES_A_DMA_INBUFFER_WRITE_FIFO_OFFSET);
	return 0;
}

/*
 * Write adata length to OCS AES HW.
 *
 * Note: adata len encoding is documented in NIST Special Publication 800-38C
 * https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38c.pdf
 * (see Section A.2.2)
 */
static void ocs_aes_ccm_write_adata_len(const struct ocs_aes_dev *aes_dev,
					u64 adata_len)
{
	u8 enc_a[10]; /* Maximum encoded size: 10 octets. */
	int i, len;

	/*
	 * adata_len ('a') is encoded as follows:
	 * If 0 < a < 2^16 - 2^8    ==> 'a' encoded as [a]16, i.e., two octets
	 *				(big endian).
	 * If 2^16 - 2^8 ≤ a < 2^32 ==> 'a' encoded as 0xff || 0xfe || [a]32,
	 *				i.e., six octets (big endian).
	 * If 2^32 ≤ a < 2^64       ==> 'a' encoded as 0xff || 0xff || [a]64,
	 *				i.e., ten octets (big endian).
	 */
	if (adata_len < 65280) {
		len = 2;
		*(__be16 *)enc_a = cpu_to_be16(adata_len);
	} else if (adata_len <= 0xFFFFFFFF) {
		len = 6;
		*(__be16 *)enc_a = cpu_to_be16(0xfffe);
		*(__be32 *)&enc_a[2] = cpu_to_be32(adata_len);
	} else { /* adata_len >= 2^32 */
		len = 10;
		*(__be16 *)enc_a = cpu_to_be16(0xffff);
		*(__be64 *)&enc_a[2] = cpu_to_be64(adata_len);
	}
	for (i = 0; i < len; i++)
		iowrite8(enc_a[i],
			 aes_dev->base_reg +
			 AES_A_DMA_INBUFFER_WRITE_FIFO_OFFSET);
}

static int ocs_aes_ccm_do_adata(struct ocs_aes_dev *aes_dev,
				dma_addr_t adata_dma_list, u32 adata_size)
{
	int rc;

	if (!adata_size) {
		/* Since no aad the LAST_GCX bit can be set now */
		aes_a_set_last_gcx_and_adata(aes_dev);
		goto exit;
	}

	/* Adata case. */

	/*
	 * Form the encoding of the Associated data length and write it
	 * to the AES/SM4 input buffer.
	 */
	ocs_aes_ccm_write_adata_len(aes_dev, adata_size);

	/* Configure the AES/SM4 DMA to fetch the Associated Data */
	dma_to_ocs_aes_ll(aes_dev, adata_dma_list);

	/* Activate DMA to fetch Associated data. */
	aes_a_dma_active_src_ll_en(aes_dev);

	/* Set LAST_GCX and LAST_ADATA in AES ACTIVE register. */
	aes_a_set_last_gcx_and_adata(aes_dev);

	/* Wait for DMA transfer to complete. */
	rc = ocs_aes_irq_enable_and_wait(aes_dev, AES_DMA_SRC_DONE_INT);
	if (rc)
		return rc;

exit:
	/* Wait until adata (if present) has been processed. */
	aes_a_wait_last_gcx(aes_dev);
	aes_a_dma_wait_input_buffer_occupancy(aes_dev);

	return 0;
}

static int ocs_aes_ccm_encrypt_do_payload(struct ocs_aes_dev *aes_dev,
					  dma_addr_t dst_dma_list,
					  dma_addr_t src_dma_list,
					  u32 src_size)
{
	if (src_size) {
		/*
		 * Configure and activate DMA for both input and output
		 * data.
		 */
		dma_to_ocs_aes_ll(aes_dev, src_dma_list);
		dma_from_ocs_aes_ll(aes_dev, dst_dma_list);
		aes_a_dma_active_src_dst_ll_en(aes_dev);
	} else {
		/* Configure and activate DMA for output data only. */
		dma_from_ocs_aes_ll(aes_dev, dst_dma_list);
		aes_a_dma_active_dst_ll_en(aes_dev);
	}

	/*
	 * Set the LAST GCX bit in AES_ACTIVE Register to instruct
	 * AES/SM4 engine to pad the last block of data.
	 */
	aes_a_set_last_gcx(aes_dev);

	/* We are done, wait for IRQ and return. */
	return ocs_aes_irq_enable_and_wait(aes_dev, AES_COMPLETE_INT);
}

static int ocs_aes_ccm_decrypt_do_payload(struct ocs_aes_dev *aes_dev,
					  dma_addr_t dst_dma_list,
					  dma_addr_t src_dma_list,
					  u32 src_size)
{
	if (!src_size) {
		/* Let engine process 0-length input. */
		aes_a_dma_set_xfer_size_zero(aes_dev);
		aes_a_dma_active(aes_dev);
		aes_a_set_last_gcx(aes_dev);

		return 0;
	}

	/*
	 * Configure and activate DMA for both input and output
	 * data.
	 */
	dma_to_ocs_aes_ll(aes_dev, src_dma_list);
	dma_from_ocs_aes_ll(aes_dev, dst_dma_list);
	aes_a_dma_active_src_dst_ll_en(aes_dev);
	/*
	 * Set the LAST GCX bit in AES_ACTIVE Register; this allows the
	 * AES/SM4 engine to differentiate between encrypted data and
	 * encrypted MAC.
	 */
	aes_a_set_last_gcx(aes_dev);
	 /*
	  * Enable DMA DONE interrupt; once DMA transfer is over,
	  * interrupt handler will process the MAC/tag.
	  */
	return ocs_aes_irq_enable_and_wait(aes_dev, AES_DMA_SRC_DONE_INT);
}

/*
 * Compare Tag to Yr.
 *
 * Only used at the end of CCM decrypt. If tag == yr, message authentication
 * has succeeded.
 */
static inline int ccm_compare_tag_to_yr(struct ocs_aes_dev *aes_dev,
					u8 tag_size_bytes)
{
	u32 tag[AES_MAX_TAG_SIZE_U32];
	u32 yr[AES_MAX_TAG_SIZE_U32];
	u8 i;

	/* Read Tag and Yr from AES registers. */
	for (i = 0; i < AES_MAX_TAG_SIZE_U32; i++) {
		tag[i] = ioread32(aes_dev->base_reg +
				  AES_T_MAC_0_OFFSET + (i * sizeof(u32)));
		yr[i] = ioread32(aes_dev->base_reg +
				 AES_MULTIPURPOSE2_0_OFFSET +
				 (i * sizeof(u32)));
	}

	return memcmp(tag, yr, tag_size_bytes) ? -EBADMSG : 0;
}

/**
 * ocs_aes_ccm_op() - Perform CCM operation.
 * @aes_dev:		The OCS AES device to use.
 * @cipher:		The Cipher to use (AES or SM4).
 * @instruction:	The instruction to perform (encrypt or decrypt).
 * @dst_dma_list:	The OCS DMA list mapping output memory.
 * @src_dma_list:	The OCS DMA list mapping input payload data.
 * @src_size:		The amount of data mapped by @src_dma_list.
 * @iv:			The input IV vector.
 * @adata_dma_list:	The OCS DMA list mapping input A-data.
 * @adata_size:		The amount of data mapped by @adata_dma_list.
 * @in_tag:		Input tag.
 * @tag_size:		The size (in bytes) of @in_tag.
 *
 * Note: for encrypt the tag is appended to the ciphertext (in the memory
 *	 mapped by @dst_dma_list).
 *
 * Return: 0 on success, negative error code otherwise.
 */
int ocs_aes_ccm_op(struct ocs_aes_dev *aes_dev,
		   enum ocs_cipher cipher,
		   enum ocs_instruction instruction,
		   dma_addr_t dst_dma_list,
		   dma_addr_t src_dma_list,
		   u32 src_size,
		   u8 *iv,
		   dma_addr_t adata_dma_list,
		   u32 adata_size,
		   u8 *in_tag,
		   u32 tag_size)
{
	u32 *iv_32;
	u8 lprime;
	int rc;

	rc = ocs_aes_validate_inputs(src_dma_list, src_size, iv,
				     AES_BLOCK_SIZE, adata_dma_list, adata_size,
				     in_tag, tag_size, cipher, OCS_MODE_CCM,
				     instruction, dst_dma_list);
	if (rc)
		return rc;

	ocs_aes_init(aes_dev, OCS_MODE_CCM, cipher, instruction);

	/*
	 * Note: rfc 3610 and NIST 800-38C require counter of zero to encrypt
	 * auth tag so ensure this is the case
	 */
	lprime = iv[L_PRIME_IDX];
	memset(&iv[COUNTER_START(lprime)], 0, COUNTER_LEN(lprime));

	/*
	 * Nonce is already converted to ctr0 before being passed into this
	 * function as iv.
	 */
	iv_32 = (u32 *)iv;
	iowrite32(__swab32(iv_32[0]),
		  aes_dev->base_reg + AES_MULTIPURPOSE1_3_OFFSET);
	iowrite32(__swab32(iv_32[1]),
		  aes_dev->base_reg + AES_MULTIPURPOSE1_2_OFFSET);
	iowrite32(__swab32(iv_32[2]),
		  aes_dev->base_reg + AES_MULTIPURPOSE1_1_OFFSET);
	iowrite32(__swab32(iv_32[3]),
		  aes_dev->base_reg + AES_MULTIPURPOSE1_0_OFFSET);

	/* Write MAC/tag length in register AES_TLEN */
	iowrite32(tag_size, aes_dev->base_reg + AES_TLEN_OFFSET);
	/*
	 * Write the byte length of the last AES/SM4 block of Payload data
	 * (without zero padding and without the length of the MAC) in register
	 * AES_PLEN.
	 */
	ocs_aes_write_last_data_blk_len(aes_dev, src_size);

	/* Set AES_ACTIVE.TRIGGER to start the operation. */
	aes_a_op_trigger(aes_dev);

	aes_a_dma_reset_and_activate_perf_cntr(aes_dev);

	/* Form block B0 and write it to the AES/SM4 input buffer. */
	rc = ocs_aes_ccm_write_b0(aes_dev, iv, adata_size, tag_size, src_size);
	if (rc)
		return rc;
	/*
	 * Ensure there has been at least CCM_DECRYPT_DELAY_LAST_GCX_CLK_COUNT
	 * clock cycles since TRIGGER bit was set
	 */
	aes_a_dma_wait_and_deactivate_perf_cntr(aes_dev,
						CCM_DECRYPT_DELAY_LAST_GCX_CLK_COUNT);

	/* Process Adata. */
	ocs_aes_ccm_do_adata(aes_dev, adata_dma_list, adata_size);

	/* For Encrypt case we just process the payload and return. */
	if (instruction == OCS_ENCRYPT) {
		return ocs_aes_ccm_encrypt_do_payload(aes_dev, dst_dma_list,
						      src_dma_list, src_size);
	}
	/* For Decypt we need to process the payload and then the tag. */
	rc = ocs_aes_ccm_decrypt_do_payload(aes_dev, dst_dma_list,
					    src_dma_list, src_size);
	if (rc)
		return rc;

	/* Process MAC/tag directly: feed tag to engine and wait for IRQ. */
	ocs_aes_ccm_write_encrypted_tag(aes_dev, in_tag, tag_size);
	rc = ocs_aes_irq_enable_and_wait(aes_dev, AES_COMPLETE_INT);
	if (rc)
		return rc;

	return ccm_compare_tag_to_yr(aes_dev, tag_size);
}

/**
 * ocs_create_linked_list_from_sg() - Create OCS DMA linked list from SG list.
 * @aes_dev:	  The OCS AES device the list will be created for.
 * @sg:		  The SG list OCS DMA linked list will be created from. When
 *		  passed to this function, @sg must have been already mapped
 *		  with dma_map_sg().
 * @sg_dma_count: The number of DMA-mapped entries in @sg. This must be the
 *		  value returned by dma_map_sg() when @sg was mapped.
 * @dll_desc:	  The OCS DMA dma_list to use to store information about the
 *		  created linked list.
 * @data_size:	  The size of the data (from the SG list) to be mapped into the
 *		  OCS DMA linked list.
 * @data_offset:  The offset (within the SG list) of the data to be mapped.
 *
 * Return:	0 on success, negative error code otherwise.
 */
int ocs_create_linked_list_from_sg(const struct ocs_aes_dev *aes_dev,
				   struct scatterlist *sg,
				   int sg_dma_count,
				   struct ocs_dll_desc *dll_desc,
				   size_t data_size, size_t data_offset)
{
	struct ocs_dma_linked_list *ll = NULL;
	struct scatterlist *sg_tmp;
	unsigned int tmp;
	int dma_nents;
	int i;

	if (!dll_desc || !sg || !aes_dev)
		return -EINVAL;

	/* Default values for when no ddl_desc is created. */
	dll_desc->vaddr = NULL;
	dll_desc->dma_addr = DMA_MAPPING_ERROR;
	dll_desc->size = 0;

	if (data_size == 0)
		return 0;

	/* Loop over sg_list until we reach entry at specified offset. */
	while (data_offset >= sg_dma_len(sg)) {
		data_offset -= sg_dma_len(sg);
		sg_dma_count--;
		sg = sg_next(sg);
		/* If we reach the end of the list, offset was invalid. */
		if (!sg || sg_dma_count == 0)
			return -EINVAL;
	}

	/* Compute number of DMA-mapped SG entries to add into OCS DMA list. */
	dma_nents = 0;
	tmp = 0;
	sg_tmp = sg;
	while (tmp < data_offset + data_size) {
		/* If we reach the end of the list, data_size was invalid. */
		if (!sg_tmp)
			return -EINVAL;
		tmp += sg_dma_len(sg_tmp);
		dma_nents++;
		sg_tmp = sg_next(sg_tmp);
	}
	if (dma_nents > sg_dma_count)
		return -EINVAL;

	/* Allocate the DMA list, one entry for each SG entry. */
	dll_desc->size = sizeof(struct ocs_dma_linked_list) * dma_nents;
	dll_desc->vaddr = dma_alloc_coherent(aes_dev->dev, dll_desc->size,
					     &dll_desc->dma_addr, GFP_KERNEL);
	if (!dll_desc->vaddr)
		return -ENOMEM;

	/* Populate DMA linked list entries. */
	ll = dll_desc->vaddr;
	for (i = 0; i < dma_nents; i++, sg = sg_next(sg)) {
		ll[i].src_addr = sg_dma_address(sg) + data_offset;
		ll[i].src_len = (sg_dma_len(sg) - data_offset) < data_size ?
				(sg_dma_len(sg) - data_offset) : data_size;
		data_offset = 0;
		data_size -= ll[i].src_len;
		/* Current element points to the DMA address of the next one. */
		ll[i].next = dll_desc->dma_addr + (sizeof(*ll) * (i + 1));
		ll[i].ll_flags = 0;
	}
	/* Terminate last element. */
	ll[i - 1].next = 0;
	ll[i - 1].ll_flags = OCS_LL_DMA_FLAG_TERMINATE;

	return 0;
}
