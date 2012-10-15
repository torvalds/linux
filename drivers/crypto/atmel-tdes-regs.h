#ifndef __ATMEL_TDES_REGS_H__
#define __ATMEL_TDES_REGS_H__

#define TDES_CR			0x00
#define TDES_CR_START			(1 << 0)
#define TDES_CR_SWRST			(1 << 8)
#define TDES_CR_LOADSEED		(1 << 16)

#define	TDES_MR			0x04
#define TDES_MR_CYPHER_DEC		(0 << 0)
#define TDES_MR_CYPHER_ENC		(1 << 0)
#define TDES_MR_TDESMOD_MASK	(0x3 << 1)
#define TDES_MR_TDESMOD_DES		(0x0 << 1)
#define TDES_MR_TDESMOD_TDES	(0x1 << 1)
#define TDES_MR_TDESMOD_XTEA	(0x2 << 1)
#define TDES_MR_KEYMOD_3KEY		(0 << 4)
#define TDES_MR_KEYMOD_2KEY		(1 << 4)
#define TDES_MR_SMOD_MASK		(0x3 << 8)
#define TDES_MR_SMOD_MANUAL		(0x0 << 8)
#define TDES_MR_SMOD_AUTO		(0x1 << 8)
#define TDES_MR_SMOD_PDC		(0x2 << 8)
#define TDES_MR_OPMOD_MASK		(0x3 << 12)
#define TDES_MR_OPMOD_ECB		(0x0 << 12)
#define TDES_MR_OPMOD_CBC		(0x1 << 12)
#define TDES_MR_OPMOD_OFB		(0x2 << 12)
#define TDES_MR_OPMOD_CFB		(0x3 << 12)
#define TDES_MR_LOD				(0x1 << 15)
#define TDES_MR_CFBS_MASK		(0x3 << 16)
#define TDES_MR_CFBS_64b		(0x0 << 16)
#define TDES_MR_CFBS_32b		(0x1 << 16)
#define TDES_MR_CFBS_16b		(0x2 << 16)
#define TDES_MR_CFBS_8b			(0x3 << 16)
#define TDES_MR_CKEY_MASK		(0xF << 20)
#define TDES_MR_CKEY_OFFSET		20
#define TDES_MR_CTYPE_MASK		(0x3F << 24)
#define TDES_MR_CTYPE_OFFSET	24

#define	TDES_IER		0x10
#define	TDES_IDR		0x14
#define	TDES_IMR		0x18
#define	TDES_ISR		0x1C
#define TDES_INT_DATARDY		(1 << 0)
#define TDES_INT_ENDRX			(1 << 1)
#define TDES_INT_ENDTX			(1 << 2)
#define TDES_INT_RXBUFF			(1 << 3)
#define TDES_INT_TXBUFE			(1 << 4)
#define TDES_INT_URAD			(1 << 8)
#define TDES_ISR_URAT_MASK		(0x3 << 12)
#define TDES_ISR_URAT_IDR		(0x0 << 12)
#define TDES_ISR_URAT_ODR		(0x1 << 12)
#define TDES_ISR_URAT_MR		(0x2 << 12)
#define TDES_ISR_URAT_WO		(0x3 << 12)


#define	TDES_KEY1W1R	0x20
#define	TDES_KEY1W2R	0x24
#define	TDES_KEY2W1R	0x28
#define	TDES_KEY2W2R	0x2C
#define	TDES_KEY3W1R	0x30
#define	TDES_KEY3W2R	0x34
#define	TDES_IDATA1R	0x40
#define	TDES_IDATA2R	0x44
#define	TDES_ODATA1R	0x50
#define	TDES_ODATA2R	0x54
#define	TDES_IV1R		0x60
#define	TDES_IV2R		0x64

#define	TDES_XTEARNDR	0x70
#define	TDES_XTEARNDR_XTEA_RNDS_MASK	(0x3F << 0)
#define	TDES_XTEARNDR_XTEA_RNDS_OFFSET	0

#define TDES_RPR		0x100
#define TDES_RCR		0x104
#define TDES_TPR		0x108
#define TDES_TCR		0x10C
#define TDES_RNPR		0x118
#define TDES_RNCR		0x11C
#define TDES_TNPR		0x118
#define TDES_TNCR		0x11C
#define TDES_PTCR		0x120
#define TDES_PTCR_RXTEN			(1 << 0)
#define TDES_PTCR_RXTDIS		(1 << 1)
#define TDES_PTCR_TXTEN			(1 << 8)
#define TDES_PTCR_TXTDIS		(1 << 9)
#define TDES_PTSR		0x124
#define TDES_PTSR_RXTEN			(1 << 0)
#define TDES_PTSR_TXTEN			(1 << 8)

#endif /* __ATMEL_TDES_REGS_H__ */
