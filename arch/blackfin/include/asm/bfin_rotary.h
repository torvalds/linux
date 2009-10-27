/*
 * board initialization should put one of these structures into platform_data
 * and place the bfin-rotary onto platform_bus named "bfin-rotary".
 *
 * Copyright 2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BFIN_ROTARY_H
#define _BFIN_ROTARY_H

/* mode bitmasks */
#define ROT_QUAD_ENC	CNTMODE_QUADENC	/* quadrature/grey code encoder mode */
#define ROT_BIN_ENC	CNTMODE_BINENC	/* binary encoder mode */
#define ROT_UD_CNT	CNTMODE_UDCNT	/* rotary counter mode */
#define ROT_DIR_CNT	CNTMODE_DIRCNT	/* direction counter mode */

#define ROT_DEBE	DEBE		/* Debounce Enable */

#define ROT_CDGINV	CDGINV		/* CDG Pin Polarity Invert */
#define ROT_CUDINV	CUDINV		/* CUD Pin Polarity Invert */
#define ROT_CZMINV	CZMINV		/* CZM Pin Polarity Invert */

struct bfin_rotary_platform_data {
	/* set rotary UP KEY_### or BTN_### in case you prefer
	 * bfin-rotary to send EV_KEY otherwise set 0
	 */
	unsigned int rotary_up_key;
	/* set rotary DOWN KEY_### or BTN_### in case you prefer
	 * bfin-rotary to send EV_KEY otherwise set 0
	 */
	unsigned int rotary_down_key;
	/* set rotary BUTTON KEY_### or BTN_### */
	unsigned int rotary_button_key;
	/* set rotary Relative Axis REL_### in case you prefer
	 * bfin-rotary to send EV_REL otherwise set 0
	 */
	unsigned int rotary_rel_code;
	unsigned short debounce;	/* 0..17 */
	unsigned short mode;
};
#endif
