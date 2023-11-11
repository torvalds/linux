/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ATMEL_SHA_REGS_H__
#define __ATMEL_SHA_REGS_H__

#define SHA_REG_DIGEST(x)		(0x80 + ((x) * 0x04))
#define SHA_REG_DIN(x)			(0x40 + ((x) * 0x04))

#define SHA_CR				0x00
#define SHA_CR_START			(1 << 0)
#define SHA_CR_FIRST			(1 << 4)
#define SHA_CR_SWRST			(1 << 8)
#define SHA_CR_WUIHV			(1 << 12)
#define SHA_CR_WUIEHV			(1 << 13)

#define SHA_MR				0x04
#define SHA_MR_MODE_MASK		(0x3 << 0)
#define SHA_MR_MODE_MANUAL		0x0
#define SHA_MR_MODE_AUTO		0x1
#define SHA_MR_MODE_PDC			0x2
#define SHA_MR_MODE_IDATAR0		0x2
#define SHA_MR_PROCDLY			(1 << 4)
#define SHA_MR_UIHV			(1 << 5)
#define SHA_MR_UIEHV			(1 << 6)
#define SHA_MR_ALGO_MASK		GENMASK(10, 8)
#define SHA_MR_ALGO_SHA1		(0 << 8)
#define SHA_MR_ALGO_SHA256		(1 << 8)
#define SHA_MR_ALGO_SHA384		(2 << 8)
#define SHA_MR_ALGO_SHA512		(3 << 8)
#define SHA_MR_ALGO_SHA224		(4 << 8)
#define SHA_MR_HMAC			(1 << 11)
#define	SHA_MR_DUALBUFF			(1 << 16)

#define SHA_FLAGS_ALGO_MASK	SHA_MR_ALGO_MASK
#define SHA_FLAGS_SHA1		SHA_MR_ALGO_SHA1
#define SHA_FLAGS_SHA256	SHA_MR_ALGO_SHA256
#define SHA_FLAGS_SHA384	SHA_MR_ALGO_SHA384
#define SHA_FLAGS_SHA512	SHA_MR_ALGO_SHA512
#define SHA_FLAGS_SHA224	SHA_MR_ALGO_SHA224
#define SHA_FLAGS_HMAC		SHA_MR_HMAC
#define SHA_FLAGS_HMAC_SHA1	(SHA_FLAGS_HMAC | SHA_FLAGS_SHA1)
#define SHA_FLAGS_HMAC_SHA256	(SHA_FLAGS_HMAC | SHA_FLAGS_SHA256)
#define SHA_FLAGS_HMAC_SHA384	(SHA_FLAGS_HMAC | SHA_FLAGS_SHA384)
#define SHA_FLAGS_HMAC_SHA512	(SHA_FLAGS_HMAC | SHA_FLAGS_SHA512)
#define SHA_FLAGS_HMAC_SHA224	(SHA_FLAGS_HMAC | SHA_FLAGS_SHA224)
#define SHA_FLAGS_MODE_MASK	(SHA_FLAGS_HMAC | SHA_FLAGS_ALGO_MASK)

#define SHA_IER				0x10
#define SHA_IDR				0x14
#define SHA_IMR				0x18
#define SHA_ISR				0x1C
#define SHA_INT_DATARDY			(1 << 0)
#define SHA_INT_ENDTX			(1 << 1)
#define SHA_INT_TXBUFE			(1 << 2)
#define SHA_INT_URAD			(1 << 8)
#define SHA_ISR_URAT_MASK		(0x7 << 12)
#define SHA_ISR_URAT_IDR		(0x0 << 12)
#define SHA_ISR_URAT_ODR		(0x1 << 12)
#define SHA_ISR_URAT_MR			(0x2 << 12)
#define SHA_ISR_URAT_WO			(0x5 << 12)

#define SHA_MSR				0x20
#define SHA_BCR				0x30

#define	SHA_HW_VERSION		0xFC

#define SHA_TPR				0x108
#define SHA_TCR				0x10C
#define SHA_TNPR			0x118
#define SHA_TNCR			0x11C
#define SHA_PTCR			0x120
#define SHA_PTCR_TXTEN		(1 << 8)
#define SHA_PTCR_TXTDIS		(1 << 9)
#define SHA_PTSR			0x124
#define SHA_PTSR_TXTEN		(1 << 8)

#endif /* __ATMEL_SHA_REGS_H__ */
