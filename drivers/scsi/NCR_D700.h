/* SPDX-License-Identifier: GPL-2.0 */
/* -*- mode: c; c-basic-offset: 8 -*- */

/* NCR Dual 700 MCA SCSI Driver
 *
 * Copyright (C) 2001 by James.Bottomley@HansenPartnership.com
 */

#ifndef _NCR_D700_H
#define _NCR_D700_H

/* Don't turn on debugging messages */
#undef NCR_D700_DEBUG

/* The MCA identifier */
#define NCR_D700_MCA_ID		0x0092

/* Defines for the Board registers */
#define	BOARD_RESET		0x80	/* board level reset */
#define ADD_PARENB		0x04	/* Address Parity Enabled */
#define DAT_PARENB		0x01	/* Data Parity Enabled */
#define SFBK_ENB		0x10	/* SFDBK Interrupt Enabled */
#define LED0GREEN		0x20	/* Led 0 (red 0; green 1) */
#define LED1GREEN		0x40	/* Led 1 (red 0; green 1) */
#define LED0RED			0xDF	/* Led 0 (red 0; green 1) */
#define LED1RED			0xBF	/* Led 1 (red 0; green 1) */

#define NCR_D700_CLOCK_MHZ	50

#endif
