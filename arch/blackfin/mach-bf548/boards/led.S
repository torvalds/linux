/****************************************************
 * LED1 ---- PG6        LED2 ---- PG7               *
 * LED3 ---- PG8        LED4 ---- PG9               *
 * LED5 ---- PG10       LED6 ---- PG11              *
 ****************************************************/

#include <linux/linkage.h>
#include <asm/blackfin.h>

/* All functions in this file save the registers they uses.
   So there is no need to save any registers before calling them.  */

	.text;

/* Initialize LEDs.  */

ENTRY(_led_init)
	LINK 0;
	[--SP] = P0;
	[--SP] = R0;
	[--SP] = R1;
	[--SP] = R2;
	R1 = (PG6|PG7|PG8|PG9|PG10|PG11)(Z);
	R2 = ~R1;

	P0.H = hi(PORTG_FER);
	P0.L = lo(PORTG_FER);
	R0 = W[P0](Z);
	SSYNC;
	R0 = R0 & R2;
	W[P0] = R0.L;
	SSYNC;

	P0.H = hi(PORTG_DIR_SET);
	P0.L = lo(PORTG_DIR_SET);
	W[P0] = R1.L;
	SSYNC;

	P0.H = hi(PORTG_INEN);
	P0.L = lo(PORTG_INEN);
	R0 = W[P0](Z);
	SSYNC;
	R0 = R0 & R2;
	W[P0] = R0.L;
	SSYNC;

	R2 = [SP++];
	R1 = [SP++];
	R0 = [SP++];
	P0 = [SP++];
	RTS;
	.size	_led_init, .-_led_init

/* Set one LED on. Leave other LEDs unchanged.
   It expects the LED number passed through R0.  */

ENTRY(_led_on)
	LINK 0;
	[--SP] = P0;
	[--SP] = R1;
	CALL _led_init;
	R1 = 1;
	R0 += 5;
	R1 <<= R0;
	P0.H = hi(PORTG_SET);
	P0.L = lo(PORTG_SET);
	W[P0] = R1.L;
	SSYNC;
	R1 = [SP++];
	P0 = [SP++];
	UNLINK;
	RTS;
	.size	_led_on, .-_led_on

/* Set one LED off. Leave other LEDs unchanged.  */

ENTRY(_led_off)
	LINK 0;
	[--SP] = P0;
	[--SP] = R1;
	CALL _led_init;
	R1 = 1;
	R0 += 5;
	R1 <<= R0;
	P0.H = hi(PORTG_CLEAR);
	P0.L = lo(PORTG_CLEAR);
	W[P0] = R1.L;
	SSYNC;
	R1 = [SP++];
	P0 = [SP++];
	UNLINK;
	RTS;
	.size	_led_off, .-_led_off

/* Toggle one LED. Leave other LEDs unchanged.  */

ENTRY(_led_toggle)
	LINK 0;
	[--SP] = P0;
	[--SP] = R1;
	CALL _led_init;
	R1 = 1;
	R0 += 5;
	R1 <<= R0;
	P0.H = hi(PORTG);
	P0.L = lo(PORTG);
	R0 = W[P0](Z);
	SSYNC;
	R0 = R0 ^ R1;
	W[P0] = R0.L;
	SSYNC;
	R1 = [SP++];
	P0 = [SP++];
	UNLINK;
	RTS;
	.size	_led_toggle, .-_led_toggle

/* Display the number using LEDs in binary format.  */

ENTRY(_led_disp_num)
	LINK 0;
	[--SP] = P0;
	[--SP] = R1;
	[--SP] = R2;
	CALL _led_init;
	R1 = 0x3f(X);
	R0 = R0 & R1;
	R2 = 6(X);
	R0 <<= R2;
	R1 <<= R2;
	P0.H = hi(PORTG);
	P0.L = lo(PORTG);
	R2 = W[P0](Z);
	SSYNC;
	R1 = ~R1;
	R2 = R2 & R1;
	R2 = R2 | R0;
	W[P0] = R2.L;
	SSYNC;
	R2 = [SP++];
	R1 = [SP++];
	P0 = [SP++];
	UNLINK;
	RTS;
	.size	_led_disp_num, .-_led_disp_num

/* Toggle the number using LEDs in binary format.  */

ENTRY(_led_toggle_num)
	LINK 0;
	[--SP] = P0;
	[--SP] = R1;
	[--SP] = R2;
	CALL _led_init;
	R1 = 0x3f(X);
	R0 = R0 & R1;
	R1 = 6(X);
	R0 <<= R1;
	P0.H = hi(PORTG);
	P0.L = lo(PORTG);
	R1 = W[P0](Z);
	SSYNC;
	R1 = R1 ^ R0;
	W[P0] = R1.L;
	SSYNC;
	R2 = [SP++];
	R1 = [SP++];
	P0 = [SP++];
	UNLINK;
	RTS;
	.size	_led_toggle_num, .-_led_toggle_num

