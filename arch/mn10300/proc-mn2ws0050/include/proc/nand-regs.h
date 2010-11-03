/* NAND flash interface register definitions
 *
 * Copyright (C) 2008-2009 Panasonic Corporation
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef	_PROC_NAND_REGS_H_
#define	_PROC_NAND_REGS_H_

/* command register */
#define FCOMMAND_0		__SYSREG(0xd8f00000, u8) /* fcommand[24:31] */
#define FCOMMAND_1		__SYSREG(0xd8f00001, u8) /* fcommand[16:23] */
#define FCOMMAND_2		__SYSREG(0xd8f00002, u8) /* fcommand[8:15] */
#define FCOMMAND_3		__SYSREG(0xd8f00003, u8) /* fcommand[0:7] */

/* for dma 16 byte trans, use FCOMMAND2 register */
#define FCOMMAND2_0		__SYSREG(0xd8f00110, u8) /* fcommand2[24:31] */
#define FCOMMAND2_1		__SYSREG(0xd8f00111, u8) /* fcommand2[16:23] */
#define FCOMMAND2_2		__SYSREG(0xd8f00112, u8) /* fcommand2[8:15] */
#define FCOMMAND2_3		__SYSREG(0xd8f00113, u8) /* fcommand2[0:7] */

#define FCOMMAND_FIEN		0x80		/* nand flash I/F enable */
#define FCOMMAND_BW_8BIT	0x00		/* 8bit bus width */
#define FCOMMAND_BW_16BIT	0x40		/* 16bit bus width */
#define FCOMMAND_BLOCKSZ_SMALL	0x00		/* small block */
#define FCOMMAND_BLOCKSZ_LARGE	0x20		/* large block */
#define FCOMMAND_DMASTART	0x10		/* dma start */
#define FCOMMAND_RYBY		0x08		/* ready/busy flag */
#define FCOMMAND_RYBYINTMSK	0x04		/* mask ready/busy interrupt */
#define FCOMMAND_XFWP		0x02		/* write protect enable */
#define FCOMMAND_XFCE		0x01		/* flash device disable */
#define FCOMMAND_SEQKILL	0x10		/* stop seq-read */
#define FCOMMAND_ANUM		0x07		/* address cycle */
#define FCOMMAND_ANUM_NONE	0x00		/* address cycle none */
#define FCOMMAND_ANUM_1CYC	0x01		/* address cycle 1cycle */
#define FCOMMAND_ANUM_2CYC	0x02		/* address cycle 2cycle */
#define FCOMMAND_ANUM_3CYC	0x03		/* address cycle 3cycle */
#define FCOMMAND_ANUM_4CYC	0x04		/* address cycle 4cycle */
#define FCOMMAND_ANUM_5CYC	0x05		/* address cycle 5cycle */
#define FCOMMAND_FCMD_READ0	0x00		/* read1 command */
#define FCOMMAND_FCMD_SEQIN	0x80		/* page program 1st command */
#define FCOMMAND_FCMD_PAGEPROG	0x10		/* page program 2nd command */
#define FCOMMAND_FCMD_RESET	0xff		/* reset command */
#define FCOMMAND_FCMD_ERASE1	0x60		/* erase 1st command */
#define FCOMMAND_FCMD_ERASE2	0xd0		/* erase 2nd command */
#define FCOMMAND_FCMD_STATUS	0x70		/* read status command */
#define FCOMMAND_FCMD_READID	0x90		/* read id command */
#define FCOMMAND_FCMD_READOOB	0x50		/* read3 command */
/* address register */
#define FADD			__SYSREG(0xd8f00004, u32)
/* address register 2 */
#define FADD2			__SYSREG(0xd8f00008, u32)
/* error judgement register */
#define FJUDGE			__SYSREG(0xd8f0000c, u32)
#define FJUDGE_NOERR		0x0		/* no error */
#define FJUDGE_1BITERR		0x1		/* 1bit error in data area */
#define FJUDGE_PARITYERR	0x2		/* parity error */
#define FJUDGE_UNCORRECTABLE	0x3		/* uncorrectable error */
#define FJUDGE_ERRJDG_MSK	0x3		/* mask of judgement result */
/* 1st ECC store register */
#define FECC11			__SYSREG(0xd8f00010, u32)
/* 2nd ECC store register */
#define FECC12			__SYSREG(0xd8f00014, u32)
/* 3rd ECC store register */
#define FECC21			__SYSREG(0xd8f00018, u32)
/* 4th ECC store register */
#define FECC22			__SYSREG(0xd8f0001c, u32)
/* 5th ECC store register */
#define FECC31			__SYSREG(0xd8f00020, u32)
/* 6th ECC store register */
#define FECC32			__SYSREG(0xd8f00024, u32)
/* 7th ECC store register */
#define FECC41			__SYSREG(0xd8f00028, u32)
/* 8th ECC store register */
#define FECC42			__SYSREG(0xd8f0002c, u32)
/* data register */
#define FDATA			__SYSREG(0xd8f00030, u32)
/* access pulse register */
#define FPWS			__SYSREG(0xd8f00100, u32)
#define FPWS_PWS1W_2CLK		0x00000000 /* write pulse width 1clock */
#define FPWS_PWS1W_3CLK		0x01000000 /* write pulse width 2clock */
#define FPWS_PWS1W_4CLK		0x02000000 /* write pulse width 4clock */
#define FPWS_PWS1W_5CLK		0x03000000 /* write pulse width 5clock */
#define FPWS_PWS1W_6CLK		0x04000000 /* write pulse width 6clock */
#define FPWS_PWS1W_7CLK		0x05000000 /* write pulse width 7clock */
#define FPWS_PWS1W_8CLK		0x06000000 /* write pulse width 8clock */
#define FPWS_PWS1R_3CLK		0x00010000 /* read pulse width 3clock */
#define FPWS_PWS1R_4CLK		0x00020000 /* read pulse width 4clock */
#define FPWS_PWS1R_5CLK		0x00030000 /* read pulse width 5clock */
#define FPWS_PWS1R_6CLK		0x00040000 /* read pulse width 6clock */
#define FPWS_PWS1R_7CLK		0x00050000 /* read pulse width 7clock */
#define FPWS_PWS1R_8CLK		0x00060000 /* read pulse width 8clock */
#define FPWS_PWS2W_2CLK		0x00000100 /* write pulse interval 2clock */
#define FPWS_PWS2W_3CLK		0x00000200 /* write pulse interval 3clock */
#define FPWS_PWS2W_4CLK		0x00000300 /* write pulse interval 4clock */
#define FPWS_PWS2W_5CLK		0x00000400 /* write pulse interval 5clock */
#define FPWS_PWS2W_6CLK		0x00000500 /* write pulse interval 6clock */
#define FPWS_PWS2R_2CLK		0x00000001 /* read pulse interval 2clock */
#define FPWS_PWS2R_3CLK		0x00000002 /* read pulse interval 3clock */
#define FPWS_PWS2R_4CLK		0x00000003 /* read pulse interval 4clock */
#define FPWS_PWS2R_5CLK		0x00000004 /* read pulse interval 5clock */
#define FPWS_PWS2R_6CLK		0x00000005 /* read pulse interval 6clock */
/* command register 2 */
#define FCOMMAND2		__SYSREG(0xd8f00110, u32)
/* transfer frequency register */
#define FNUM			__SYSREG(0xd8f00114, u32)
#define FSDATA_ADDR		0xd8f00400
/* active data register */
#define FSDATA			__SYSREG(FSDATA_ADDR, u32)

#endif /* _PROC_NAND_REGS_H_ */
