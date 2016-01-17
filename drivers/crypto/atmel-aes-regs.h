#ifndef __ATMEL_AES_REGS_H__
#define __ATMEL_AES_REGS_H__

#define AES_CR			0x00
#define AES_CR_START		(1 << 0)
#define AES_CR_SWRST		(1 << 8)
#define AES_CR_LOADSEED		(1 << 16)

#define	AES_MR			0x04
#define AES_MR_CYPHER_DEC		(0 << 0)
#define AES_MR_CYPHER_ENC		(1 << 0)
#define AES_MR_GTAGEN			(1 << 1)
#define	AES_MR_DUALBUFF			(1 << 3)
#define AES_MR_PROCDLY_MASK		(0xF << 4)
#define AES_MR_PROCDLY_OFFSET	4
#define AES_MR_SMOD_MASK		(0x3 << 8)
#define AES_MR_SMOD_MANUAL		(0x0 << 8)
#define AES_MR_SMOD_AUTO		(0x1 << 8)
#define AES_MR_SMOD_IDATAR0		(0x2 << 8)
#define	AES_MR_KEYSIZE_MASK		(0x3 << 10)
#define	AES_MR_KEYSIZE_128		(0x0 << 10)
#define	AES_MR_KEYSIZE_192		(0x1 << 10)
#define	AES_MR_KEYSIZE_256		(0x2 << 10)
#define AES_MR_OPMOD_MASK		(0x7 << 12)
#define AES_MR_OPMOD_ECB		(0x0 << 12)
#define AES_MR_OPMOD_CBC		(0x1 << 12)
#define AES_MR_OPMOD_OFB		(0x2 << 12)
#define AES_MR_OPMOD_CFB		(0x3 << 12)
#define AES_MR_OPMOD_CTR		(0x4 << 12)
#define AES_MR_OPMOD_GCM		(0x5 << 12)
#define AES_MR_LOD				(0x1 << 15)
#define AES_MR_CFBS_MASK		(0x7 << 16)
#define AES_MR_CFBS_128b		(0x0 << 16)
#define AES_MR_CFBS_64b			(0x1 << 16)
#define AES_MR_CFBS_32b			(0x2 << 16)
#define AES_MR_CFBS_16b			(0x3 << 16)
#define AES_MR_CFBS_8b			(0x4 << 16)
#define AES_MR_CKEY_MASK		(0xF << 20)
#define AES_MR_CKEY_OFFSET		20
#define AES_MR_CMTYP_MASK		(0x1F << 24)
#define AES_MR_CMTYP_OFFSET		24

#define	AES_IER		0x10
#define	AES_IDR		0x14
#define	AES_IMR		0x18
#define	AES_ISR		0x1C
#define AES_INT_DATARDY		(1 << 0)
#define AES_INT_URAD		(1 << 8)
#define AES_INT_TAGRDY		(1 << 16)
#define AES_ISR_URAT_MASK	(0xF << 12)
#define AES_ISR_URAT_IDR_WR_PROC	(0x0 << 12)
#define AES_ISR_URAT_ODR_RD_PROC	(0x1 << 12)
#define AES_ISR_URAT_MR_WR_PROC		(0x2 << 12)
#define AES_ISR_URAT_ODR_RD_SUBK	(0x3 << 12)
#define AES_ISR_URAT_MR_WR_SUBK		(0x4 << 12)
#define AES_ISR_URAT_WOR_RD			(0x5 << 12)

#define AES_KEYWR(x)	(0x20 + ((x) * 0x04))
#define AES_IDATAR(x)	(0x40 + ((x) * 0x04))
#define AES_ODATAR(x)	(0x50 + ((x) * 0x04))
#define AES_IVR(x)		(0x60 + ((x) * 0x04))

#define AES_AADLENR	0x70
#define AES_CLENR	0x74
#define AES_GHASHR(x)	(0x78 + ((x) * 0x04))
#define AES_TAGR(x)	(0x88 + ((x) * 0x04))
#define AES_CTRR	0x98
#define AES_GCMHR(x)	(0x9c + ((x) * 0x04))

#define AES_HW_VERSION	0xFC

#endif /* __ATMEL_AES_REGS_H__ */
