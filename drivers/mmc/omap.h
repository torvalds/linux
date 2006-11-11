#ifndef	DRIVERS_MEDIA_MMC_OMAP_H
#define	DRIVERS_MEDIA_MMC_OMAP_H

#define	OMAP_MMC_REG_CMD	0x00
#define	OMAP_MMC_REG_ARGL	0x04
#define	OMAP_MMC_REG_ARGH	0x08
#define	OMAP_MMC_REG_CON	0x0c
#define	OMAP_MMC_REG_STAT	0x10
#define	OMAP_MMC_REG_IE		0x14
#define	OMAP_MMC_REG_CTO	0x18
#define	OMAP_MMC_REG_DTO	0x1c
#define	OMAP_MMC_REG_DATA	0x20
#define	OMAP_MMC_REG_BLEN	0x24
#define	OMAP_MMC_REG_NBLK	0x28
#define	OMAP_MMC_REG_BUF	0x2c
#define OMAP_MMC_REG_SDIO	0x34
#define	OMAP_MMC_REG_REV	0x3c
#define	OMAP_MMC_REG_RSP0	0x40
#define	OMAP_MMC_REG_RSP1	0x44
#define	OMAP_MMC_REG_RSP2	0x48
#define	OMAP_MMC_REG_RSP3	0x4c
#define	OMAP_MMC_REG_RSP4	0x50
#define	OMAP_MMC_REG_RSP5	0x54
#define	OMAP_MMC_REG_RSP6	0x58
#define	OMAP_MMC_REG_RSP7	0x5c
#define	OMAP_MMC_REG_IOSR	0x60
#define	OMAP_MMC_REG_SYSC	0x64
#define	OMAP_MMC_REG_SYSS	0x68

#define	OMAP_MMC_STAT_CARD_ERR		(1 << 14)
#define	OMAP_MMC_STAT_CARD_IRQ		(1 << 13)
#define	OMAP_MMC_STAT_OCR_BUSY		(1 << 12)
#define	OMAP_MMC_STAT_A_EMPTY		(1 << 11)
#define	OMAP_MMC_STAT_A_FULL		(1 << 10)
#define	OMAP_MMC_STAT_CMD_CRC		(1 <<  8)
#define	OMAP_MMC_STAT_CMD_TOUT		(1 <<  7)
#define	OMAP_MMC_STAT_DATA_CRC		(1 <<  6)
#define	OMAP_MMC_STAT_DATA_TOUT		(1 <<  5)
#define	OMAP_MMC_STAT_END_BUSY		(1 <<  4)
#define	OMAP_MMC_STAT_END_OF_DATA	(1 <<  3)
#define	OMAP_MMC_STAT_CARD_BUSY		(1 <<  2)
#define	OMAP_MMC_STAT_END_OF_CMD	(1 <<  0)

#define OMAP_MMC_READ(host, reg)	__raw_readw((host)->virt_base + OMAP_MMC_REG_##reg)
#define OMAP_MMC_WRITE(host, reg, val)	__raw_writew((val), (host)->virt_base + OMAP_MMC_REG_##reg)

/*
 * Command types
 */
#define OMAP_MMC_CMDTYPE_BC	0
#define OMAP_MMC_CMDTYPE_BCR	1
#define OMAP_MMC_CMDTYPE_AC	2
#define OMAP_MMC_CMDTYPE_ADTC	3

#endif
