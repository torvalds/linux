/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_S390_UAPI_RAW3270_H
#define __ASM_S390_UAPI_RAW3270_H

/* Local Channel Commands */
#define TC_WRITE	0x01		/* Write */
#define TC_RDBUF	0x02		/* Read Buffer */
#define TC_EWRITE	0x05		/* Erase write */
#define TC_READMOD	0x06		/* Read modified */
#define TC_EWRITEA	0x0d		/* Erase write alternate */
#define TC_WRITESF	0x11		/* Write structured field */

/* Buffer Control Orders */
#define TO_GE		0x08		/* Graphics Escape */
#define TO_SF		0x1d		/* Start field */
#define TO_SBA		0x11		/* Set buffer address */
#define TO_IC		0x13		/* Insert cursor */
#define TO_PT		0x05		/* Program tab */
#define TO_RA		0x3c		/* Repeat to address */
#define TO_SFE		0x29		/* Start field extended */
#define TO_EUA		0x12		/* Erase unprotected to address */
#define TO_MF		0x2c		/* Modify field */
#define TO_SA		0x28		/* Set attribute */

/* Field Attribute Bytes */
#define TF_INPUT	0x40		/* Visible input */
#define TF_INPUTN	0x4c		/* Invisible input */
#define TF_INMDT	0xc1		/* Visible, Set-MDT */
#define TF_LOG		0x60

/* Character Attribute Bytes */
#define TAT_RESET	0x00
#define TAT_FIELD	0xc0
#define TAT_EXTHI	0x41
#define TAT_FGCOLOR	0x42
#define TAT_CHARS	0x43
#define TAT_BGCOLOR	0x45
#define TAT_TRANS	0x46

/* Extended-Highlighting Bytes */
#define TAX_RESET	0x00
#define TAX_BLINK	0xf1
#define TAX_REVER	0xf2
#define TAX_UNDER	0xf4

/* Reset value */
#define TAR_RESET	0x00

/* Color values */
#define TAC_RESET	0x00
#define TAC_BLUE	0xf1
#define TAC_RED		0xf2
#define TAC_PINK	0xf3
#define TAC_GREEN	0xf4
#define TAC_TURQ	0xf5
#define TAC_YELLOW	0xf6
#define TAC_WHITE	0xf7
#define TAC_DEFAULT	0x00

/* Write Control Characters */
#define TW_NONE		0x40		/* No particular action */
#define TW_KR		0xc2		/* Keyboard restore */
#define TW_PLUSALARM	0x04		/* Add this bit for alarm */

#define RAW3270_FIRSTMINOR	1	/* First minor number */
#define RAW3270_MAXDEVS		255	/* Max number of 3270 devices */

#define AID_CLEAR		0x6d
#define AID_ENTER		0x7d
#define AID_PF3			0xf3
#define AID_PF7			0xf7
#define AID_PF8			0xf8
#define AID_READ_PARTITION	0x88

#endif /* __ASM_S390_UAPI_RAW3270_H */
