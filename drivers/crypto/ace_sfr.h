/*
 * Header file for Advanced Crypto Engine - SFR definitions
 *
 * Copyright (c) 2011  Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __ACE_SFR_H__
#define __ACE_SFR_H__

#ifdef __cplusplus
extern "C" {
#endif


/*****************************************************************
	SFR Addresses
*****************************************************************/
#if defined(CONFIG_ARCH_S5PV210)
#define ACE_SFR_BASE		(0xEA000000)
#elif defined(CONFIG_ARCH_EXYNOS4) || defined(CONFIG_ARCH_EXYNOS5)
#define ACE_SFR_BASE		(0x10830000)
#else
#error No ARCH is defined.
#endif

#if defined(CONFIG_ARCH_S5PV210)
#define ACE_FC_OFFSET		(0x0)
#define ACE_AES_OFFSET		(0x4000)
#define ACE_TDES_OFFSET		(0x5000)
#define ACE_HASH_OFFSET		(0x6000)
#define ACE_PKA_OFFSET		(0x7000)
#elif defined(CONFIG_ARCH_EXYNOS4) || defined(CONFIG_ARCH_EXYNOS5)
#define ACE_FC_OFFSET		(0x0)
#define ACE_AES_OFFSET		(0x200)
#define ACE_TDES_OFFSET		(0x300)
#define ACE_HASH_OFFSET		(0x400)
#define ACE_PKA_OFFSET		(0x700)
#endif

/* Feed control registers */
#define ACE_FC_INTSTAT		(ACE_FC_OFFSET + 0x00)
#define ACE_FC_INTENSET		(ACE_FC_OFFSET + 0x04)
#define ACE_FC_INTENCLR		(ACE_FC_OFFSET + 0x08)
#define ACE_FC_INTPEND		(ACE_FC_OFFSET + 0x0C)
#define ACE_FC_FIFOSTAT		(ACE_FC_OFFSET + 0x10)
#define ACE_FC_FIFOCTRL		(ACE_FC_OFFSET + 0x14)
#define ACE_FC_GLOBAL		(ACE_FC_OFFSET + 0x18)
#define ACE_FC_BRDMAS		(ACE_FC_OFFSET + 0x20)
#define ACE_FC_BRDMAL		(ACE_FC_OFFSET + 0x24)
#define ACE_FC_BRDMAC		(ACE_FC_OFFSET + 0x28)
#define ACE_FC_BTDMAS		(ACE_FC_OFFSET + 0x30)
#define ACE_FC_BTDMAL		(ACE_FC_OFFSET + 0x34)
#define ACE_FC_BTDMAC		(ACE_FC_OFFSET + 0x38)
#define ACE_FC_HRDMAS		(ACE_FC_OFFSET + 0x40)
#define ACE_FC_HRDMAL		(ACE_FC_OFFSET + 0x44)
#define ACE_FC_HRDMAC		(ACE_FC_OFFSET + 0x48)
#define ACE_FC_PKDMAS		(ACE_FC_OFFSET + 0x50)
#define ACE_FC_PKDMAL		(ACE_FC_OFFSET + 0x54)
#define ACE_FC_PKDMAC		(ACE_FC_OFFSET + 0x58)
#define ACE_FC_PKDMAO		(ACE_FC_OFFSET + 0x5C)

/* AES control registers */
#define ACE_AES_CONTROL		(ACE_AES_OFFSET + 0x00)
#define ACE_AES_STATUS		(ACE_AES_OFFSET + 0x04)

#define ACE_AES_IN1		(ACE_AES_OFFSET + 0x10)
#define ACE_AES_IN2		(ACE_AES_OFFSET + 0x14)
#define ACE_AES_IN3		(ACE_AES_OFFSET + 0x18)
#define ACE_AES_IN4		(ACE_AES_OFFSET + 0x1C)

#define ACE_AES_OUT1		(ACE_AES_OFFSET + 0x20)
#define ACE_AES_OUT2		(ACE_AES_OFFSET + 0x24)
#define ACE_AES_OUT3		(ACE_AES_OFFSET + 0x28)
#define ACE_AES_OUT4		(ACE_AES_OFFSET + 0x2C)

#define ACE_AES_IV1		(ACE_AES_OFFSET + 0x30)
#define ACE_AES_IV2		(ACE_AES_OFFSET + 0x34)
#define ACE_AES_IV3		(ACE_AES_OFFSET + 0x38)
#define ACE_AES_IV4		(ACE_AES_OFFSET + 0x3C)

#define ACE_AES_CNT1		(ACE_AES_OFFSET + 0x40)
#define ACE_AES_CNT2		(ACE_AES_OFFSET + 0x44)
#define ACE_AES_CNT3		(ACE_AES_OFFSET + 0x48)
#define ACE_AES_CNT4		(ACE_AES_OFFSET + 0x4C)

#define ACE_AES_KEY1		(ACE_AES_OFFSET + 0x80)
#define ACE_AES_KEY2		(ACE_AES_OFFSET + 0x84)
#define ACE_AES_KEY3		(ACE_AES_OFFSET + 0x88)
#define ACE_AES_KEY4		(ACE_AES_OFFSET + 0x8C)
#define ACE_AES_KEY5		(ACE_AES_OFFSET + 0x90)
#define ACE_AES_KEY6		(ACE_AES_OFFSET + 0x94)
#define ACE_AES_KEY7		(ACE_AES_OFFSET + 0x98)
#define ACE_AES_KEY8		(ACE_AES_OFFSET + 0x9C)

/* TDES control registers */
#define ACE_TDES_CONTROL	(ACE_TDES_OFFSET + 0x00)
#define ACE_TDES_STATUS		(ACE_TDES_OFFSET + 0x04)

#define ACE_TDES_KEY11		(ACE_TDES_OFFSET + 0x10)
#define ACE_TDES_KEY12		(ACE_TDES_OFFSET + 0x14)
#define ACE_TDES_KEY21		(ACE_TDES_OFFSET + 0x18)
#define ACE_TDES_KEY22		(ACE_TDES_OFFSET + 0x1C)
#define ACE_TDES_KEY31		(ACE_TDES_OFFSET + 0x20)
#define ACE_TDES_KEY32		(ACE_TDES_OFFSET + 0x24)

#define ACE_TDES_IV1		(ACE_TDES_OFFSET + 0x28)
#define ACE_TDES_IV2		(ACE_TDES_OFFSET + 0x2C)

#define ACE_TDES_IN1		(ACE_TDES_OFFSET + 0x30)
#define ACE_TDES_IN2		(ACE_TDES_OFFSET + 0x34)

#define ACE_TDES_OUT1		(ACE_TDES_OFFSET + 0x38)
#define ACE_TDES_OUT2		(ACE_TDES_OFFSET + 0x3C)

/* HASH control registers */
#if defined(CONFIG_ARCH_S5PV210)
#define ACE_HASH_CONTROL	(ACE_HASH_OFFSET + 0x00)
#define ACE_HASH_CONTROL2	(ACE_HASH_OFFSET + 0x04)
#define ACE_HASH_FIFO_MODE	(ACE_HASH_OFFSET + 0x08)
#define ACE_HASH_BYTESWAP	(ACE_HASH_OFFSET + 0x0C)
#define ACE_HASH_STATUS		(ACE_HASH_OFFSET + 0x10)
#define ACE_HASH_MSGSIZE_LOW	(ACE_HASH_OFFSET + 0x14)
#define ACE_HASH_MSGSIZE_HIGH	(ACE_HASH_OFFSET + 0x18)

#define ACE_HASH_IN1		(ACE_HASH_OFFSET + 0x20)
#define ACE_HASH_IN2		(ACE_HASH_OFFSET + 0x24)
#define ACE_HASH_IN3		(ACE_HASH_OFFSET + 0x28)
#define ACE_HASH_IN4		(ACE_HASH_OFFSET + 0x2C)
#define ACE_HASH_IN5		(ACE_HASH_OFFSET + 0x30)
#define ACE_HASH_IN6		(ACE_HASH_OFFSET + 0x34)
#define ACE_HASH_IN7		(ACE_HASH_OFFSET + 0x38)
#define ACE_HASH_IN8		(ACE_HASH_OFFSET + 0x3C)

#define ACE_HASH_SEED1		(ACE_HASH_OFFSET + 0x40)
#define ACE_HASH_SEED2		(ACE_HASH_OFFSET + 0x44)
#define ACE_HASH_SEED3		(ACE_HASH_OFFSET + 0x48)
#define ACE_HASH_SEED4		(ACE_HASH_OFFSET + 0x4C)
#define ACE_HASH_SEED5		(ACE_HASH_OFFSET + 0x50)

#define ACE_HASH_RESULT1	(ACE_HASH_OFFSET + 0x60)
#define ACE_HASH_RESULT2	(ACE_HASH_OFFSET + 0x64)
#define ACE_HASH_RESULT3	(ACE_HASH_OFFSET + 0x68)
#define ACE_HASH_RESULT4	(ACE_HASH_OFFSET + 0x6C)
#define ACE_HASH_RESULT5	(ACE_HASH_OFFSET + 0x70)

#define ACE_HASH_PRNG1		(ACE_HASH_OFFSET + 0x80)
#define ACE_HASH_PRNG2		(ACE_HASH_OFFSET + 0x84)
#define ACE_HASH_PRNG3		(ACE_HASH_OFFSET + 0x88)
#define ACE_HASH_PRNG4		(ACE_HASH_OFFSET + 0x8C)
#define ACE_HASH_PRNG5		(ACE_HASH_OFFSET + 0x90)

#define ACE_HASH_IV1		(ACE_HASH_OFFSET + 0xA0)
#define ACE_HASH_IV2		(ACE_HASH_OFFSET + 0xA4)
#define ACE_HASH_IV3		(ACE_HASH_OFFSET + 0xA8)
#define ACE_HASH_IV4		(ACE_HASH_OFFSET + 0xAC)
#define ACE_HASH_IV5		(ACE_HASH_OFFSET + 0xB0)

#define ACE_HASH_PRELEN_HIGH	(ACE_HASH_OFFSET + 0xC0)
#define ACE_HASH_PRELEN_LOW	(ACE_HASH_OFFSET + 0xC4)
#elif defined(CONFIG_ARCH_EXYNOS4) || defined(CONFIG_ARCH_EXYNOS5)
#define ACE_HASH_CONTROL	(ACE_HASH_OFFSET + 0x00)
#define ACE_HASH_CONTROL2	(ACE_HASH_OFFSET + 0x04)
#define ACE_HASH_FIFO_MODE	(ACE_HASH_OFFSET + 0x08)
#define ACE_HASH_BYTESWAP	(ACE_HASH_OFFSET + 0x0C)
#define ACE_HASH_STATUS		(ACE_HASH_OFFSET + 0x10)
#define ACE_HASH_MSGSIZE_LOW	(ACE_HASH_OFFSET + 0x20)
#define ACE_HASH_MSGSIZE_HIGH	(ACE_HASH_OFFSET + 0x24)
#define ACE_HASH_PRELEN_LOW	(ACE_HASH_OFFSET + 0x28)
#define ACE_HASH_PRELEN_HIGH	(ACE_HASH_OFFSET + 0x2C)

#define ACE_HASH_IN1		(ACE_HASH_OFFSET + 0x30)
#define ACE_HASH_IN2		(ACE_HASH_OFFSET + 0x34)
#define ACE_HASH_IN3		(ACE_HASH_OFFSET + 0x38)
#define ACE_HASH_IN4		(ACE_HASH_OFFSET + 0x3C)
#define ACE_HASH_IN5		(ACE_HASH_OFFSET + 0x40)
#define ACE_HASH_IN6		(ACE_HASH_OFFSET + 0x44)
#define ACE_HASH_IN7		(ACE_HASH_OFFSET + 0x48)
#define ACE_HASH_IN8		(ACE_HASH_OFFSET + 0x4C)
#define ACE_HASH_IN9		(ACE_HASH_OFFSET + 0x50)
#define ACE_HASH_IN10		(ACE_HASH_OFFSET + 0x54)
#define ACE_HASH_IN11		(ACE_HASH_OFFSET + 0x58)
#define ACE_HASH_IN12		(ACE_HASH_OFFSET + 0x5C)
#define ACE_HASH_IN13		(ACE_HASH_OFFSET + 0x60)
#define ACE_HASH_IN14		(ACE_HASH_OFFSET + 0x64)
#define ACE_HASH_IN15		(ACE_HASH_OFFSET + 0x68)
#define ACE_HASH_IN16		(ACE_HASH_OFFSET + 0x6C)

#define ACE_HASH_HMAC_KEY_IN1	(ACE_HASH_OFFSET + 0x70)
#define ACE_HASH_HMAC_KEY_IN2	(ACE_HASH_OFFSET + 0x74)
#define ACE_HASH_HMAC_KEY_IN3	(ACE_HASH_OFFSET + 0x78)
#define ACE_HASH_HMAC_KEY_IN4	(ACE_HASH_OFFSET + 0x7C)
#define ACE_HASH_HMAC_KEY_IN5	(ACE_HASH_OFFSET + 0x80)
#define ACE_HASH_HMAC_KEY_IN6	(ACE_HASH_OFFSET + 0x84)
#define ACE_HASH_HMAC_KEY_IN7	(ACE_HASH_OFFSET + 0x88)
#define ACE_HASH_HMAC_KEY_IN8	(ACE_HASH_OFFSET + 0x8C)
#define ACE_HASH_HMAC_KEY_IN9	(ACE_HASH_OFFSET + 0x90)
#define ACE_HASH_HMAC_KEY_IN10	(ACE_HASH_OFFSET + 0x94)
#define ACE_HASH_HMAC_KEY_IN11	(ACE_HASH_OFFSET + 0x98)
#define ACE_HASH_HMAC_KEY_IN12	(ACE_HASH_OFFSET + 0x9C)
#define ACE_HASH_HMAC_KEY_IN13	(ACE_HASH_OFFSET + 0xA0)
#define ACE_HASH_HMAC_KEY_IN14	(ACE_HASH_OFFSET + 0xA4)
#define ACE_HASH_HMAC_KEY_IN15	(ACE_HASH_OFFSET + 0xA8)
#define ACE_HASH_HMAC_KEY_IN16	(ACE_HASH_OFFSET + 0xAC)

#define ACE_HASH_IV1		(ACE_HASH_OFFSET + 0xB0)
#define ACE_HASH_IV2		(ACE_HASH_OFFSET + 0xB4)
#define ACE_HASH_IV3		(ACE_HASH_OFFSET + 0xB8)
#define ACE_HASH_IV4		(ACE_HASH_OFFSET + 0xBC)
#define ACE_HASH_IV5		(ACE_HASH_OFFSET + 0xC0)
#define ACE_HASH_IV6		(ACE_HASH_OFFSET + 0xC4)
#define ACE_HASH_IV7		(ACE_HASH_OFFSET + 0xC8)
#define ACE_HASH_IV8		(ACE_HASH_OFFSET + 0xCC)

#define ACE_HASH_RESULT1	(ACE_HASH_OFFSET + 0x100)
#define ACE_HASH_RESULT2	(ACE_HASH_OFFSET + 0x104)
#define ACE_HASH_RESULT3	(ACE_HASH_OFFSET + 0x108)
#define ACE_HASH_RESULT4	(ACE_HASH_OFFSET + 0x10C)
#define ACE_HASH_RESULT5	(ACE_HASH_OFFSET + 0x110)
#define ACE_HASH_RESULT6	(ACE_HASH_OFFSET + 0x114)
#define ACE_HASH_RESULT7	(ACE_HASH_OFFSET + 0x118)
#define ACE_HASH_RESULT8	(ACE_HASH_OFFSET + 0x11C)

#define ACE_HASH_SEED1		(ACE_HASH_OFFSET + 0x140)
#define ACE_HASH_SEED2		(ACE_HASH_OFFSET + 0x144)
#define ACE_HASH_SEED3		(ACE_HASH_OFFSET + 0x148)
#define ACE_HASH_SEED4		(ACE_HASH_OFFSET + 0x14C)
#define ACE_HASH_SEED5		(ACE_HASH_OFFSET + 0x150)

#define ACE_HASH_PRNG1		(ACE_HASH_OFFSET + 0x160)
#define ACE_HASH_PRNG2		(ACE_HASH_OFFSET + 0x164)
#define ACE_HASH_PRNG3		(ACE_HASH_OFFSET + 0x168)
#define ACE_HASH_PRNG4		(ACE_HASH_OFFSET + 0x16C)
#define ACE_HASH_PRNG5		(ACE_HASH_OFFSET + 0x170)
#endif

/* PKA control registers */
#define ACE_PKA_SFR0		(ACE_PKA_OFFSET + 0x00)
#define ACE_PKA_SFR1		(ACE_PKA_OFFSET + 0x04)
#define ACE_PKA_SFR2		(ACE_PKA_OFFSET + 0x08)
#define ACE_PKA_SFR3		(ACE_PKA_OFFSET + 0x0C)
#define ACE_PKA_SFR4		(ACE_PKA_OFFSET + 0x10)


/*****************************************************************
	OFFSET
*****************************************************************/

/* ACE_FC_INT */
#define ACE_FC_PKDMA			(1 << 0)
#define ACE_FC_HRDMA			(1 << 1)
#define ACE_FC_BTDMA			(1 << 2)
#define ACE_FC_BRDMA			(1 << 3)
#define ACE_FC_PRNG_ERROR		(1 << 4)
#define ACE_FC_MSG_DONE			(1 << 5)
#define ACE_FC_PRNG_DONE		(1 << 6)
#define ACE_FC_PARTIAL_DONE		(1 << 7)

/* ACE_FC_FIFOSTAT */
#define ACE_FC_PKFIFO_EMPTY		(1 << 0)
#define ACE_FC_PKFIFO_FULL		(1 << 1)
#define ACE_FC_HRFIFO_EMPTY		(1 << 2)
#define ACE_FC_HRFIFO_FULL		(1 << 3)
#define ACE_FC_BTFIFO_EMPTY		(1 << 4)
#define ACE_FC_BTFIFO_FULL		(1 << 5)
#define ACE_FC_BRFIFO_EMPTY		(1 << 6)
#define ACE_FC_BRFIFO_FULL		(1 << 7)

/* ACE_FC_FIFOCTRL */
#define ACE_FC_SELHASH_MASK		(3 << 0)
#define ACE_FC_SELHASH_EXOUT		(0 << 0)	/*independent source*/
#define ACE_FC_SELHASH_BCIN		(1 << 0)	/*block cipher input*/
#define ACE_FC_SELHASH_BCOUT		(2 << 0)	/*block cipher output*/
#define ACE_FC_SELBC_MASK		(1 << 2)
#define ACE_FC_SELBC_AES		(0 << 2)	/* AES */
#define ACE_FC_SELBC_DES		(1 << 2)	/* DES */

/* ACE_FC_GLOBAL */
#define ACE_FC_SSS_RESET		(1 << 0)
#define ACE_FC_DMA_RESET		(1 << 1)
#define ACE_FC_AES_RESET		(1 << 2)
#define ACE_FC_DES_RESET		(1 << 3)
#define ACE_FC_HASH_RESET		(1 << 4)
#define ACE_FC_AXI_ENDIAN_MASK		(3 << 6)
#define ACE_FC_AXI_ENDIAN_LE		(0 << 6)
#define ACE_FC_AXI_ENDIAN_BIBE		(1 << 6)
#define ACE_FC_AXI_ENDIAN_WIBE		(2 << 6)

/* Feed control - BRDMA control */
#define ACE_FC_BRDMACFLUSH_OFF		(0 << 0)
#define ACE_FC_BRDMACFLUSH_ON		(1 << 0)
#define ACE_FC_BRDMACSWAP_ON		(1 << 1)
#define ACE_FC_BRDMACARPROT_MASK	(0x7 << 2)
#define ACE_FC_BRDMACARPROT_OFS		(2)
#define ACE_FC_BRDMACARCACHE_MASK	(0xF << 5)
#define ACE_FC_BRDMACARCACHE_OFS	(5)

/* Feed control - BTDMA control */
#define ACE_FC_BTDMACFLUSH_OFF		(0 << 0)
#define ACE_FC_BTDMACFLUSH_ON		(1 << 0)
#define ACE_FC_BTDMACSWAP_ON		(1 << 1)
#define ACE_FC_BTDMACAWPROT_MASK	(0x7 << 2)
#define ACE_FC_BTDMACAWPROT_OFS		(2)
#define ACE_FC_BTDMACAWCACHE_MASK	(0xF << 5)
#define ACE_FC_BTDMACAWCACHE_OFS	(5)

/* Feed control - HRDMA control */
#define ACE_FC_HRDMACFLUSH_OFF		(0 << 0)
#define ACE_FC_HRDMACFLUSH_ON		(1 << 0)
#define ACE_FC_HRDMACSWAP_ON		(1 << 1)
#define ACE_FC_HRDMACARPROT_MASK	(0x7 << 2)
#define ACE_FC_HRDMACARPROT_OFS		(2)
#define ACE_FC_HRDMACARCACHE_MASK	(0xF << 5)
#define ACE_FC_HRDMACARCACHE_OFS	(5)

/* Feed control - PKDMA control */
#define ACE_FC_PKDMACBYTESWAP_ON	(1 << 3)
#define ACE_FC_PKDMACDESEND_ON		(1 << 2)
#define ACE_FC_PKDMACTRANSMIT_ON	(1 << 1)
#define ACE_FC_PKDMACFLUSH_ON		(1 << 0)

/* Feed control - PKDMA offset */
#define ACE_FC_SRAMOFFSET_MASK		(0xFFF)

/* AES control */
#define ACE_AES_MODE_MASK		(1 << 0)
#define ACE_AES_MODE_ENC		(0 << 0)
#define ACE_AES_MODE_DEC		(1 << 0)
#define ACE_AES_OPERMODE_MASK		(3 << 1)
#define ACE_AES_OPERMODE_ECB		(0 << 1)
#define ACE_AES_OPERMODE_CBC		(1 << 1)
#define ACE_AES_OPERMODE_CTR		(2 << 1)
#define ACE_AES_FIFO_MASK		(1 << 3)
#define ACE_AES_FIFO_OFF		(0 << 3)	/* CPU mode */
#define ACE_AES_FIFO_ON			(1 << 3)	/* FIFO mode */
#define ACE_AES_KEYSIZE_MASK		(3 << 4)
#define ACE_AES_KEYSIZE_128		(0 << 4)
#define ACE_AES_KEYSIZE_192		(1 << 4)
#define ACE_AES_KEYSIZE_256		(2 << 4)
#define ACE_AES_KEYCNGMODE_MASK		(1 << 6)
#define ACE_AES_KEYCNGMODE_OFF		(0 << 6)
#define ACE_AES_KEYCNGMODE_ON		(1 << 6)
#define ACE_AES_SWAP_MASK		(0x1F << 7)
#define ACE_AES_SWAPKEY_OFF		(0 << 7)
#define ACE_AES_SWAPKEY_ON		(1 << 7)
#define ACE_AES_SWAPCNT_OFF		(0 << 8)
#define ACE_AES_SWAPCNT_ON		(1 << 8)
#define ACE_AES_SWAPIV_OFF		(0 << 9)
#define ACE_AES_SWAPIV_ON		(1 << 9)
#define ACE_AES_SWAPDO_OFF		(0 << 10)
#define ACE_AES_SWAPDO_ON		(1 << 10)
#define ACE_AES_SWAPDI_OFF		(0 << 11)
#define ACE_AES_SWAPDI_ON		(1 << 11)
#define ACE_AES_COUNTERSIZE_MASK	(3 << 12)
#define ACE_AES_COUNTERSIZE_128		(0 << 12)
#define ACE_AES_COUNTERSIZE_64		(1 << 12)
#define ACE_AES_COUNTERSIZE_32		(2 << 12)
#define ACE_AES_COUNTERSIZE_16		(3 << 12)

/* AES status */
#define ACE_AES_OUTRDY_MASK		(1 << 0)
#define ACE_AES_OUTRDY_OFF		(0 << 0)
#define ACE_AES_OUTRDY_ON		(1 << 0)
#define ACE_AES_INRDY_MASK		(1 << 1)
#define ACE_AES_INRDY_OFF		(0 << 1)
#define ACE_AES_INRDY_ON		(1 << 1)
#define ACE_AES_BUSY_MASK		(1 << 2)
#define ACE_AES_BUSY_OFF		(0 << 2)
#define ACE_AES_BUSY_ON			(1 << 2)

/* TDES control */
#define ACE_TDES_MODE_MASK		(1 << 0)
#define ACE_TDES_MODE_ENC		(0 << 0)
#define ACE_TDES_MODE_DEC		(1 << 0)
#define ACE_TDES_OPERMODE_MASK		(1 << 1)
#define ACE_TDES_OPERMODE_ECB		(0 << 1)
#define ACE_TDES_OPERMODE_CBC		(1 << 1)
#define ACE_TDES_SEL_MASK		(3 << 3)
#define ACE_TDES_SEL_DES		(0 << 3)
#define ACE_TDES_SEL_TDESEDE		(1 << 3)	/* TDES EDE mode */
#define ACE_TDES_SEL_TDESEEE		(3 << 3)	/* TDES EEE mode */
#define ACE_TDES_FIFO_MASK		(1 << 5)
#define ACE_TDES_FIFO_OFF		(0 << 5)	/* CPU mode */
#define ACE_TDES_FIFO_ON		(1 << 5)	/* FIFO mode */
#define ACE_TDES_SWAP_MASK		(0xF << 6)
#define ACE_TDES_SWAPKEY_OFF		(0 << 6)
#define ACE_TDES_SWAPKEY_ON		(1 << 6)
#define ACE_TDES_SWAPIV_OFF		(0 << 7)
#define ACE_TDES_SWAPIV_ON		(1 << 7)
#define ACE_TDES_SWAPDO_OFF		(0 << 8)
#define ACE_TDES_SWAPDO_ON		(1 << 8)
#define ACE_TDES_SWAPDI_OFF		(0 << 9)
#define ACE_TDES_SWAPDI_ON		(1 << 9)

/* TDES status */
#define ACE_TDES_OUTRDY_MASK		(1 << 0)
#define ACE_TDES_OUTRDY_OFF		(0 << 0)
#define ACE_TDES_OUTRDY_ON		(1 << 0)
#define ACE_TDES_INRDY_MASK		(1 << 1)
#define ACE_TDES_INRDY_OFF		(0 << 1)
#define ACE_TDES_INRDY_ON		(1 << 1)
#define ACE_TDES_BUSY_MASK		(1 << 2)
#define ACE_TDES_BUSY_OFF		(0 << 2)
#define ACE_TDES_BUSY_ON		(1 << 2)

/* Hash control */
#define ACE_HASH_ENGSEL_MASK		(0xF << 0)
#define ACE_HASH_ENGSEL_SHA1HASH	(0x0 << 0)
#define ACE_HASH_ENGSEL_SHA1HMAC	(0x1 << 0)
#define ACE_HASH_ENGSEL_SHA1HMACIN	(0x1 << 0)
#define ACE_HASH_ENGSEL_SHA1HMACOUT	(0x9 << 0)
#define ACE_HASH_ENGSEL_MD5HASH		(0x2 << 0)
#define ACE_HASH_ENGSEL_MD5HMAC		(0x3 << 0)
#define ACE_HASH_ENGSEL_MD5HMACIN	(0x3 << 0)
#define ACE_HASH_ENGSEL_MD5HMACOUT	(0xB << 0)
#define ACE_HASH_ENGSEL_SHA256HASH	(0x4 << 0)
#define ACE_HASH_ENGSEL_SHA256HMAC	(0x5 << 0)
#if defined(CONFIG_ARCH_S5PV210)
#define ACE_HASH_ENGSEL_PRNG		(0x4 << 0)
#elif defined(CONFIG_ARCH_EXYNOS4) || defined(CONFIG_ARCH_EXYNOS5)
#define ACE_HASH_ENGSEL_PRNG		(0x8 << 0)
#endif
#define ACE_HASH_STARTBIT_ON		(1 << 4)
#define ACE_HASH_USERIV_EN		(1 << 5)

/* Hash control 2 */
#if defined(CONFIG_ARCH_S5PV210)
#define ACE_HASH_PAUSE_ON		(1 << 3)
#elif defined(CONFIG_ARCH_EXYNOS4) || defined(CONFIG_ARCH_EXYNOS5)
#define ACE_HASH_PAUSE_ON		(1 << 0)
#endif

/* Hash control - FIFO mode */
#define ACE_HASH_FIFO_MASK		(1 << 0)
#define ACE_HASH_FIFO_OFF		(0 << 0)
#define ACE_HASH_FIFO_ON		(1 << 0)

/* Hash control - byte swap */
#if defined(CONFIG_ARCH_S5PV210)
#define ACE_HASH_SWAP_MASK		(0x7 << 1)
#elif defined(CONFIG_ARCH_EXYNOS4) || defined(CONFIG_ARCH_EXYNOS5)
#define ACE_HASH_SWAP_MASK		(0xF << 0)
#endif
#define ACE_HASH_SWAPKEY_OFF		(0 << 0)
#define	ACE_HASH_SWAPKEY_ON		(1 << 0)
#define ACE_HASH_SWAPIV_OFF		(0 << 1)
#define	ACE_HASH_SWAPIV_ON		(1 << 1)
#define ACE_HASH_SWAPDO_OFF		(0 << 2)
#define ACE_HASH_SWAPDO_ON		(1 << 2)
#define ACE_HASH_SWAPDI_OFF		(0 << 3)
#define ACE_HASH_SWAPDI_ON		(1 << 3)

/* Hash status */
#define ACE_HASH_BUFRDY_MASK		(1 << 0)
#define ACE_HASH_BUFRDY_OFF		(0 << 0)
#define ACE_HASH_BUFRDY_ON		(1 << 0)
#define ACE_HASH_SEEDSETTING_MASK	(1 << 1)
#define ACE_HASH_SEEDSETTING_OFF	(0 << 1)
#define ACE_HASH_SEEDSETTING_ON		(1 << 1)
#define ACE_HASH_PRNGBUSY_MASK		(1 << 2)
#define ACE_HASH_PRNGBUSY_OFF		(0 << 2)
#define ACE_HASH_PRNGBUSY_ON		(1 << 2)
#define ACE_HASH_PARTIALDONE_MASK	(1 << 4)
#define ACE_HASH_PARTIALDONE_OFF	(0 << 4)
#define ACE_HASH_PARTIALDONE_ON		(1 << 4)
#define ACE_HASH_PRNGDONE_MASK		(1 << 5)
#define ACE_HASH_PRNGDONE_OFF		(0 << 5)
#define ACE_HASH_PRNGDONE_ON		(1 << 5)
#define ACE_HASH_MSGDONE_MASK		(1 << 6)
#define ACE_HASH_MSGDONE_OFF		(0 << 6)
#define ACE_HASH_MSGDONE_ON		(1 << 6)
#define ACE_HASH_PRNGERROR_MASK		(1 << 7)
#define ACE_HASH_PRNGERROR_OFF		(0 << 7)
#define ACE_HASH_PRNGERROR_ON		(1 << 7)

/* To Do: SFRs for PKA */

#ifdef __cplusplus
}
#endif

#endif

