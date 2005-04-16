/*
 *	Commands to the keyboard processor
 */

#define LK_PARAM		0x80	/* start/end parameter list */

#define LK_CMD_RESUME		0x8b	/* resume transmission to the host */
#define LK_CMD_INHIBIT		0x89	/* stop transmission to the host */
#define LK_CMD_LEDS_ON		0x13	/* light LEDs */
					/* 1st param: led bitmask */
#define LK_CMD_LEDS_OFF		0x11	/* turn off LEDs */
					/* 1st param: led bitmask */
#define LK_CMD_DIS_KEYCLK	0x99	/* disable the keyclick */
#define LK_CMD_ENB_KEYCLK	0x1b	/* enable the keyclick */
					/* 1st param: volume */
#define LK_CMD_DIS_CTLCLK	0xb9	/* disable the Ctrl keyclick */
#define LK_CMD_ENB_CTLCLK	0xbb	/* enable the Ctrl keyclick */
#define LK_CMD_SOUND_CLK	0x9f	/* emit a keyclick */
#define LK_CMD_DIS_BELL		0xa1	/* disable the bell */
#define LK_CMD_ENB_BELL		0x23	/* enable the bell */
					/* 1st param: volume */
#define LK_CMD_BELL		0xa7	/* emit a bell */
#define LK_CMD_TMP_NORPT	0xd1	/* disable typematic */
					/* for the currently pressed key */
#define LK_CMD_ENB_RPT		0xe3	/* enable typematic */
					/* for RPT_DOWN groups */
#define LK_CMD_DIS_RPT		0xe1	/* disable typematic */
					/* for RPT_DOWN groups */
#define LK_CMD_RPT_TO_DOWN	0xd9	/* set RPT_DOWN groups to DOWN */
#define LK_CMD_REQ_ID		0xab	/* request the keyboard ID */
#define LK_CMD_POWER_UP		0xfd	/* init power-up sequence */
#define LK_CMD_TEST_MODE	0xcb	/* enter the factory test mode */
#define LK_CMD_TEST_EXIT	0x80	/* exit the factory test mode */
#define LK_CMD_SET_DEFAULTS	0xd3	/* set power-up defaults */

#define LK_CMD_MODE(m,div)	(LK_PARAM|(((div)&0xf)<<3)|(((m)&0x3)<<1))
					/* select the repeat mode */
					/* for the selected key group */
#define LK_CMD_MODE_AR(m,div)	((((div)&0xf)<<3)|(((m)&0x3)<<1))
					/* select the repeat mode */
					/* and the repeat register */
					/* for the selected key group */
					/* 1st param: register number */
#define LK_CMD_RPT_RATE(r)	(0x78|(((r)&0x3)<<1))
					/* set the delay and repeat rate */
					/* for the selected repeat register */
					/* 1st param: initial delay */
					/* 2nd param: repeat rate */

/* there are 4 leds, represent them in the low 4 bits of a byte */
#define LK_PARAM_LED_MASK(ledbmap)	(LK_PARAM|((ledbmap)&0xf))
#define LK_LED_WAIT		0x1	/* Wait LED */
#define LK_LED_COMP		0x2	/* Compose LED */
#define LK_LED_LOCK		0x4	/* Lock LED */
#define LK_LED_HOLD		0x8	/* Hold Screen LED */

/* max volume is 0, lowest is 0x7 */
#define LK_PARAM_VOLUME(v)		(LK_PARAM|((v)&0x7))

/* mode set command details, div is a key group number */
#define LK_MODE_DOWN		0x0	/* make only */
#define LK_MODE_RPT_DOWN	0x1	/* make and typematic */
#define LK_MODE_DOWN_UP		0x3	/* make and release */

/* there are 4 repeat registers */
#define LK_PARAM_AR(r)		(LK_PARAM|((v)&0x3))

/*
 * Mappings between key groups and keycodes are as follows:
 *
 *  1: 0xbf - 0xff -- alphanumeric,
 *  2: 0x91 - 0xa5 -- numeric keypad,
 *  3: 0xbc        -- Backspace,
 *  4: 0xbd - 0xbe -- Tab, Return,
 *  5: 0xb0 - 0xb2 -- Lock, Compose Character,
 *  6: 0xad - 0xaf -- Ctrl, Shift,
 *  7: 0xa6 - 0xa8 -- Left Arrow, Right Arrow,
 *  8: 0xa9 - 0xac -- Up Arrow, Down Arrow, Right Shift,
 *  9: 0x88 - 0x90 -- editor keypad,
 * 10: 0x56 - 0x62 -- F1 - F5,
 * 11: 0x63 - 0x6e -- F6 - F10,
 * 12: 0x6f - 0x7a -- F11 - F14,
 * 13: 0x7b - 0x7d -- Help, Do,
 * 14: 0x7e - 0x87 -- F17 - F20.
 *
 * Notes:
 * 1. Codes in the 0x00 - 0x40 range are reserved.
 * 2. The assignment of the 0x41 - 0x55 range is undiscovered, probably 10.
 */

/* delay is 5 - 630 ms; 0x00 and 0x7f are reserved */
#define LK_PARAM_DELAY(t)	((t)&0x7f)

/* rate is 12 - 127 Hz; 0x00 - 0x0b and 0x7d (power-up!) are reserved */
#define LK_PARAM_RATE(r)	(LK_PARAM|((r)&0x7f))

#define LK_SHIFT 1<<0
#define LK_CTRL 1<<1
#define LK_LOCK 1<<2
#define LK_COMP 1<<3

#define LK_KEY_SHIFT		0xae
#define LK_KEY_CTRL		0xaf
#define LK_KEY_LOCK		0xb0
#define LK_KEY_COMP		0xb1

#define LK_KEY_RELEASE		0xb3	/* all keys released */
#define LK_KEY_REPEAT		0xb4	/* repeat the last key */

/* status responses */
#define LK_STAT_RESUME_ERR	0xb5	/* keystrokes lost while inhibited */
#define LK_STAT_ERROR		0xb6	/* an invalid command received */
#define LK_STAT_INHIBIT_ACK	0xb7	/* transmission inhibited */
#define LK_STAT_TEST_ACK	0xb8	/* the factory test mode entered */
#define LK_STAT_MODE_KEYDOWN	0xb9	/* a key is down on a change */
					/* to the DOWN_UP mode; */
					/* the keycode follows */
#define LK_STAT_MODE_ACK	0xba	/* the mode command succeeded */

#define LK_STAT_PWRUP_ID	0x01	/* the power-up response start mark */
#define LK_STAT_PWRUP_OK	0x00	/* the power-up self test OK */
#define LK_STAT_PWRUP_KDOWN	0x3d	/* a key was down during the test */
#define LK_STAT_PWRUP_ERROR	0x3e	/* keyboard self test failure */

extern unsigned char scancodeRemap[256];
