/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_V2_REG_H__
#define __RK_CRYPTO_V2_REG_H__

#define _SBF(s, v)			((v) << (s))
#define _BIT(b)				_SBF(b, 1)

#define CRYPTO_WRITE_MASK_SHIFT		(16)
#define CRYPTO_WRITE_MASK_ALL		((0xffffu << CRYPTO_WRITE_MASK_SHIFT))

#define WRITE_MASK			(16)

/* Crypto control registers*/
#define CRYPTO_CLK_CTL			0x0000
#define CRYPTO_AUTO_CLKGATE_EN		_BIT(0)

#define CRYPTO_RST_CTL			0x0004
#define CRYPTO_SW_PKA_RESET		_BIT(2)
#define CRYPTO_SW_RNG_RESET		_BIT(1)
#define CRYPTO_SW_CC_RESET		_BIT(0)

/* Crypto DMA control registers*/
#define CRYPTO_DMA_INT_EN		0x0008
#define CRYPTO_ZERO_ERR_INT_EN		_BIT(6)
#define CRYPTO_LIST_ERR_INT_EN		_BIT(5)
#define CRYPTO_SRC_ERR_INT_EN		_BIT(4)
#define CRYPTO_DST_ERR_INT_EN		_BIT(3)
#define CRYPTO_SRC_ITEM_INT_EN		_BIT(2)
#define CRYPTO_DST_ITEM_DONE_INT_EN	_BIT(1)
#define CRYPTO_LIST_DONE_INT_EN		_BIT(0)

#define CRYPTO_DMA_INT_ST		0x000C
#define CRYPTO_ZERO_LEN_INT_ST		_BIT(6)
#define CRYPTO_LIST_ERR_INT_ST		_BIT(5)
#define CRYPTO_SRC_ERR_INT_ST		_BIT(4)
#define CRYPTO_DST_ERR_INT_ST		_BIT(3)
#define CRYPTO_SRC_ITEM_DONE_INT_ST	_BIT(2)
#define CRYPTO_DST_ITEM_DONE_INT_ST	_BIT(1)
#define CRYPTO_LIST_DONE_INT_ST		_BIT(0)

#define CRYPTO_DMA_CTL			0x0010
#define CRYPTO_DMA_RESTART		_BIT(1)
#define CRYPTO_DMA_START		_BIT(0)

/* DMA LIST Start Address Register */
#define CRYPTO_DMA_LLI_ADDR		0x0014

#define CRYPTO_DMA_ST			0x0018
#define CRYPTO_DMA_BUSY			_BIT(0)

#define CRYPTO_DMA_STATE		0x001C
#define CRYPTO_LLI_IDLE_STATE		_SBF(4, 0x00)
#define CRYPTO_LLI_FETCH_STATE		_SBF(4, 0x01)
#define CRYPTO_LLI_WORK_STATE		_SBF(4, 0x02)
#define CRYPTO_SRC_IDLE_STATE		_SBF(2, 0x00)
#define CRYPTO_SRC_LOAD_STATE		_SBF(2, 0x01)
#define CRYPTO_SRC_WORK_STATE		_SBF(2, 0x02)
#define CRYPTO_DST_IDLE_STATE		_SBF(0, 0x00)
#define CRYPTO_DST_LOAD_STATE		_SBF(0, 0x01)
#define CRYPTO_DST_WORK_STATE		_SBF(0, 0x02)

/* DMA LLI Read Address Register */
#define CRYPTO_DMA_LLI_RADDR		0x0020

/* DMA Source Data Read Address Register */
#define CRYPTO_DMA_SRC_RADDR		0x0024

/* DMA Destination Data Read Address Register */
#define CRYPTO_DMA_DST_RADDR		0x0028

#define CRYPTO_DMA_ITEM_ID		0x002C

#define CRYPTO_FIFO_CTL			0x0040
#define CRYPTO_DOUT_BYTESWAP		_BIT(1)
#define CRYPTO_DOIN_BYTESWAP		_BIT(0)

/* Block Cipher Control Register */
#define CRYPTO_BC_CTL			0x0044
#define CRYPTO_BC_AES			_SBF(8, 0x00)
#define CRYPTO_BC_DES			_SBF(8, 0x02)
#define CRYPTO_BC_TDES			_SBF(8, 0x03)
#define CRYPTO_BC_ECB			_SBF(4, 0x00)
#define CRYPTO_BC_CBC			_SBF(4, 0x01)
#define CRYPTO_BC_CTS			_SBF(4, 0x02)
#define CRYPTO_BC_CTR			_SBF(4, 0x03)
#define CRYPTO_BC_CFB			_SBF(4, 0x04)
#define CRYPTO_BC_OFB			_SBF(4, 0x05)
#define CRYPTO_BC_XTS			_SBF(4, 0x06)
#define CRYPTO_BC_CCM			_SBF(4, 0x07)
#define CRYPTO_BC_GCM			_SBF(4, 0x08)
#define CRYPTO_BC_CMAC			_SBF(4, 0x09)
#define CRYPTO_BC_CBC_MAC		_SBF(4, 0x0A)
#define CRYPTO_BC_128_bit_key		_SBF(2, 0x00)
#define CRYPTO_BC_192_bit_key		_SBF(2, 0x01)
#define CRYPTO_BC_256_bit_key		_SBF(2, 0x02)
#define CRYPTO_BC_DECRYPT		_BIT(1)
#define CRYPTO_BC_ENABLE		_BIT(0)

/* Hash Control Register */
#define CRYPTO_HASH_CTL			0x0048
#define CRYPTO_SHA1			_SBF(4, 0x00)
#define CRYPTO_MD5			_SBF(4, 0x01)
#define CRYPTO_SHA256			_SBF(4, 0x02)
#define CRYPTO_SHA224			_SBF(4, 0x03)
#define CRYPTO_SHA512			_SBF(4, 0x08)
#define CRYPTO_SHA384			_SBF(4, 0x09)
#define CRYPTO_SHA512_224		_SBF(4, 0x0A)
#define CRYPTO_SHA512_256		_SBF(4, 0x0B)
#define CRYPTO_HMAC_ENABLE		_BIT(3)
#define CRYPTO_HW_PAD_ENABLE		_BIT(2)
#define CRYPTO_HASH_SRC_SEL		_BIT(1)
#define CRYPTO_HASH_ENABLE		_BIT(0)

/* Cipher Status Register */
#define CRYPTO_CIPHER_ST		0x004C
#define CRYPTO_OTP_KEY_VALID		_BIT(2)
#define CRYPTO_HASH_BUSY		_BIT(1)
#define CRYPTO_BLOCK_CIPHER_BUSY	_BIT(0)

#define CRYPTO_CIPHER_STATE		0x0050
#define CRYPTO_HASH_IDLE_STATE		_SBF(10, 0x01)
#define CRYPTO_HASH_IPAD_STATE		_SBF(10, 0x02)
#define CRYPTO_HASH_TEXT_STATE		_SBF(10, 0x04)
#define CRYPTO_HASH_OPAD_STATE		_SBF(10, 0x08)
#define CRYPTO_HASH_OPAD_EXT_STATE	_SBF(10, 0x10)
#define CRYPTO_GCM_IDLE_STATE		_SBF(8, 0x00)
#define CRYPTO_GCM_PRE_STATE		_SBF(8, 0x01)
#define CRYPTO_GCM_NA_STATE		_SBF(8, 0x02)
#define CRYPTO_GCM_PC_STATE		_SBF(8, 0x03)
#define CRYPTO_CCM_IDLE_STATE		_SBF(6, 0x00)
#define CRYPTO_CCM_PRE_STATE		_SBF(6, 0x01)
#define CRYPTO_CCM_NA_STATE		_SBF(6, 0x02)
#define CRYPTO_CCM_PC_STATE		_SBF(6, 0x03)
#define CRYPTO_PARALLEL_IDLE_STATE	_SBF(4, 0x00)
#define CRYPTO_PARALLEL_PRE_STATE	_SBF(4, 0x01)
#define CRYPTO_PARALLEL_BULK_STATE	_SBF(4, 0x02)
#define CRYPTO_MAC_IDLE_STATE		_SBF(2, 0x00)
#define CRYPTO_MAC_PRE_STATE		_SBF(2, 0x01)
#define CRYPTO_MAC_BULK_STATE		_SBF(2, 0x02)
#define CRYPTO_SERIAL_IDLE_STATE	_SBF(0, 0x00)
#define CRYPTO_SERIAL_PRE_STATE		_SBF(0, 0x01)
#define CRYPTO_SERIAL_BULK_STATE	_SBF(0, 0x02)

#define CRYPTO_CH0_IV_0         0x0100
#define CRYPTO_CH0_IV_1         0x0104
#define CRYPTO_CH0_IV_2         0x0108
#define CRYPTO_CH0_IV_3         0x010c
#define CRYPTO_CH1_IV_0         0x0110
#define CRYPTO_CH1_IV_1         0x0114
#define CRYPTO_CH1_IV_2         0x0118
#define CRYPTO_CH1_IV_3         0x011c
#define CRYPTO_CH2_IV_0         0x0120
#define CRYPTO_CH2_IV_1         0x0124
#define CRYPTO_CH2_IV_2         0x0128
#define CRYPTO_CH2_IV_3         0x012c
#define CRYPTO_CH3_IV_0         0x0130
#define CRYPTO_CH3_IV_1         0x0134
#define CRYPTO_CH3_IV_2         0x0138
#define CRYPTO_CH3_IV_3         0x013c
#define CRYPTO_CH4_IV_0         0x0140
#define CRYPTO_CH4_IV_1         0x0144
#define CRYPTO_CH4_IV_2         0x0148
#define CRYPTO_CH4_IV_3         0x014c
#define CRYPTO_CH5_IV_0         0x0150
#define CRYPTO_CH5_IV_1         0x0154
#define CRYPTO_CH5_IV_2         0x0158
#define CRYPTO_CH5_IV_3         0x015c
#define CRYPTO_CH6_IV_0         0x0160
#define CRYPTO_CH6_IV_1         0x0164
#define CRYPTO_CH6_IV_2         0x0168
#define CRYPTO_CH6_IV_3         0x016c
#define CRYPTO_CH7_IV_0         0x0170
#define CRYPTO_CH7_IV_1         0x0174
#define CRYPTO_CH7_IV_2         0x0178
#define CRYPTO_CH7_IV_3         0x017c

#define CRYPTO_CH0_KEY_0        0x0180
#define CRYPTO_CH0_KEY_1        0x0184
#define CRYPTO_CH0_KEY_2        0x0188
#define CRYPTO_CH0_KEY_3        0x018c
#define CRYPTO_CH1_KEY_0        0x0190
#define CRYPTO_CH1_KEY_1        0x0194
#define CRYPTO_CH1_KEY_2        0x0198
#define CRYPTO_CH1_KEY_3        0x019c
#define CRYPTO_CH2_KEY_0        0x01a0
#define CRYPTO_CH2_KEY_1        0x01a4
#define CRYPTO_CH2_KEY_2        0x01a8
#define CRYPTO_CH2_KEY_3        0x01ac
#define CRYPTO_CH3_KEY_0        0x01b0
#define CRYPTO_CH3_KEY_1        0x01b4
#define CRYPTO_CH3_KEY_2        0x01b8
#define CRYPTO_CH3_KEY_3        0x01bc
#define CRYPTO_CH4_KEY_0        0x01c0
#define CRYPTO_CH4_KEY_1        0x01c4
#define CRYPTO_CH4_KEY_2        0x01c8
#define CRYPTO_CH4_KEY_3        0x01cc
#define CRYPTO_CH5_KEY_0        0x01d0
#define CRYPTO_CH5_KEY_1        0x01d4
#define CRYPTO_CH5_KEY_2        0x01d8
#define CRYPTO_CH5_KEY_3        0x01dc
#define CRYPTO_CH6_KEY_0        0x01e0
#define CRYPTO_CH6_KEY_1        0x01e4
#define CRYPTO_CH6_KEY_2        0x01e8
#define CRYPTO_CH6_KEY_3        0x01ec
#define CRYPTO_CH7_KEY_0        0x01f0
#define CRYPTO_CH7_KEY_1        0x01f4
#define CRYPTO_CH7_KEY_2        0x01f8
#define CRYPTO_CH7_KEY_3        0x01fc
#define CRYPTO_KEY_CHANNEL_NUM	8

#define CRYPTO_CH0_IV_LEN_0     0x0300
#define CRYPTO_CH1_IV_LEN_0     0x0304
#define CRYPTO_CH2_IV_LEN_0     0x0308
#define CRYPTO_CH3_IV_LEN_0     0x030c
#define CRYPTO_CH4_IV_LEN_0     0x0310
#define CRYPTO_CH5_IV_LEN_0     0x0314
#define CRYPTO_CH6_IV_LEN_0     0x0318
#define CRYPTO_CH7_IV_LEN_0     0x031c

#define CRYPTO_READ(dev, offset)		  \
		readl_relaxed(((dev)->reg + (offset)))
#define CRYPTO_WRITE(dev, offset, val)	  \
		writel_relaxed((val), ((dev)->reg + (offset)))

#define CRYPTO_CLK_NUM	(4)

enum endian_mode {
	BIG_ENDIAN = 0,
	LITTLE_ENDIAN
};

#endif

