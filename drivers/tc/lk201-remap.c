/*
 * Keyboard mappings for DEC LK201/401/501 keyboards
 *
 * 17.05.99 Michael Engel (engel@unix-ag.org)
 *
 * DEC US keyboards generate keycodes in the range 0x55 - 0xfb
 *
 * This conflicts with Linux scancode conventions which define
 * 0x00-0x7f as "normal" and 0x80-0xff as "shifted" scancodes, so we
 * have to remap the keycodes to 0x00-0x7f with the scancodeRemap
 * array. The generated scancode is simply the number of the key counted
 * from the left upper to the right lower corner of the keyboard ...
 *
 * These scancodes are then being remapped (I hope ;-)) with the
 * lk501*map[] arrays which define scancode -> Linux code mapping
 *
 * Oh man is this horrible ;-)
 *
 * Scancodes with dual labels exist for keyboards as follows:
 *
 * code:  left label          / right label
 *
 * 0x73:  LKx01, LK421        / LK443, LK444
 * 0x74:  LKx01, LK421        / LK443, LK444
 * 0x7c:  LKx01, LK421        / LK443, LK444
 * 0x8a:  LKx01, LK421        / LK443, LK444
 * 0x8b:  LKx01, LK421        / LK443, LK444
 * 0x8c:  LKx01, LK421        / LK443, LK444
 * 0x8d:  LKx01, LK421        / LK443, LK444
 * 0x8e:  LKx01, LK421        / LK443, LK444
 * 0x8f:  LKx01, LK421        / LK443, LK444
 * 0x9c:  LKx01, LK421        / LK443, LK444
 * 0xa1:  LKx01, LK421        / LK443, LK444
 * 0xa2:  LKx01, LK421        / LK443, LK444
 * 0xa3:  LKx01, LK421        / LK443, LK444
 * 0xa4:  LKx01, LK421        / LK443, LK444
 * 0xad:         LK421        / LK443, LK444
 * 0xc9:  LKx01, LK421, LK443 /        LK444
 * 0xf7:  LKx01,        LK443 /        LK444
 */

unsigned char scancodeRemap[256] = {
/* ----- 								*/
/*  0 */ 0,		0,		0,		0,
/* ----- 								*/
/*  4 */ 0,		0,		0,		0,
/* ----- 								*/
/*  8 */ 0,		0,		0,		0,
/* ----- 								*/
/*  c */ 0,		0,		0,		0,
/* ----- 								*/
/* 10 */ 0,		0,		0,		0,
/* ----- 								*/
/* 14 */ 0,		0,		0,		0,
/* ----- 								*/
/* 18 */ 0,		0,		0,		0,
/* ----- 								*/
/* 1c */ 0,		0,		0,		0,
/* ----- 								*/
/* 20 */ 0,		0,		0,		0,
/* ----- 								*/
/* 24 */ 0,		0,		0,		0,
/* ----- 								*/
/* 28 */ 0,		0,		0,		0,
/* ----- 								*/
/* 2c */ 0,		0,		0,		0,
/* ----- 								*/
/* 30 */ 0,		0,		0,		0,
/* ----- 								*/
/* 34 */ 0,		0,		0,		0,
/* ----- 								*/
/* 38 */ 0,		0,		0,		0,
/* ----- 								*/
/* 3c */ 0,		0,		0,		0,
/* ----- 								*/
/* 40 */ 0,		0,		0,		0,
/* ----- 								*/
/* 44 */ 0,		0,		0,		0,
/* ----- 								*/
/* 48 */ 0,		0,		0,		0,
/* ----- 								*/
/* 4c */ 0,		0,		0,		0,
/* ----- 								*/
/* 50 */ 0,		0,		0,		0,
/* ----- 	 	ESC		F1		F2 		*/
/* 54 */ 0,		0,		0x01,  		0x02,
/* ----- F3		F4		F5				*/
/* 58 */ 0x03,  	0x04,		0x05,		0,
/* ----- 								*/
/* 5c */ 0,		0,		0,		0,
/* ----- 								*/
/* 60 */ 0,		0,		0,		0,
/* ----- F6		F7		F8		F9		*/
/* 64 */ 0x06,		0x07,		0x08,		0x09,
/* ----- F10								*/
/* 68 */ 0x0a,		0,		0,		0,
/* ----- 								*/
/* 6c */ 0,		0,		0,		0,
/* ----- 		F11   		F12		F13/PRNT SCRN	*/
/* 70 */ 0,		0x0b,  		0x0c,		0x0d,
/* ----- F14/SCRL LCK							*/
/* 74 */ 0x0e,		0,		0,		0,
/* ----- 								*/
/* 78 */ 0,		0,		0,		0,
/* ----- HELP/PAUSE	DO						*/
/* 7c */ 0x0f,		0x10,		0,		0,
/* ----- F17		F18		F19		F20		*/
/* 80 */ 0x11,		0x12,		0x13,		0x14,
/* ----- 								*/
/* 84 */ 0,		0,		0,		0,
/* ----- 				FIND/INSERT	INSERT/HOME	*/
/* 88 */ 0,		0,		0x23,		0x24,
/* ----- REMOVE/PG UP	SELECT/DELETE	PREVIOUS/END	NEXT/PG DN	*/
/* 8c */ 0x25,		0x38,		0x39,		0x3a,
/* ----- 				KP 0				*/
/* 90 */ 0,		0,		0x6b,		0,
/* ----- KP .		KP ENTER	KP 1		KP 2		*/
/* 94 */ 0x6c,		0x65,		0x62,		0x63,
/* ----- KP 3		KP 4		KP 5		KP 6		*/
/* 98 */ 0x64,		0x4e,		0x4f,		0x50,
/* ----- KP ,/KP +	KP 7		KP 8		KP 9		*/
/* 9c */ 0x51,		0x3b,		0x3c,		0x3d,
/* ----- KP -		KP F1/NUM LCK	KP F2/KP /	KP F3/KP *	*/
/* a0 */ 0x3e,		0x26,		0x27,		0x28,
/* ----- KP F4/KP -					LEFT		*/
/* a4 */ 0x29,		0,		0,		0x5f,
/* ----- RIGHT		DOWN		UP		SHIFT Rt	*/
/* a8 */ 0x61,		0x60, 		0x4d,		0x5e,
/* ----- ALT		COMP Rt/CTRL Rt	SHIFT		CONTROL		*/
/* ac */ 0,		0,		0x52,		0x3f,
/* ----- CAPS		COMPOSE		ALT Rt				*/
/* b0 */ 0x40,		0x67,		0,		0,
/* ----- 								*/
/* b4 */ 0,		0,		0,		0,
/* ----- 								*/
/* b8 */ 0,		0,		0,		0,
/* ----- BKSP		RET		TAB		`		*/
/* bc */ 0x22,		0x37,		0x2a,		0x15,
/* ----- 1		q		a		z		*/
/* c0 */ 0x16,		0x2b,		0x41,		0x54,
/* ----- 		2		w		s		*/
/* c4 */ 0,		0x17,		0x2c,		0x42,
/* ----- x		</\\				3		*/
/* c8 */ 0x55,		0x53,		0,		0x18,
/* ----- e		d		c				*/
/* cc */ 0x2d,		0x43,		0x56,		0,
/* ----- 4		r		f		v		*/
/* d0 */ 0x19,		0x2e,		0x44,		0x57,
/* ----- SPACE				5		t		*/
/* d4 */ 0x68,		0,		0x1a,		0x2f,
/* ----- g		b				6		*/
/* d8 */ 0x45,		0x58,		0,		0x1b,
/* ----- y		h		n				*/
/* dc */ 0x30,		0x46,		0x59,		0,
/* ----- 7		u		j		m		*/
/* e0 */ 0x1c,		0x31,		0x47,		0x5a,
/* ----- 		8		i		k		*/
/* e4 */ 0,		0x1d,		0x32,		0x48,
/* ----- ,				9		o		*/
/* e8 */ 0x5b,		0,		0x1e,		0x33,
/* ----- l		.				0		*/
/* ec */ 0x49,		0x5c,		0,		0x1f,
/* ----- p				;		/		*/
/* f0 */ 0x34,		0,		0x4a,		0x5d,
/* ----- 		=		]		\\/\'		*/
/* f4 */ 0,		0x21,		0x36,		0x4c,
/* ----- 		-		[		\'		*/
/* f8 */ 0,		0x20,		0x35,		0x4b,
/* ----- 								*/
/* fc */ 0,		0,		0,		0,
};

