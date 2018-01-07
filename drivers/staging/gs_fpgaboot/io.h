/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define GPDIR	0
#define GPCFG	4	/* open drain or not */
#define GPDAT	8

/*
 * gpio port and pin definitions
 * NOTE: port number starts from 0
 */
#define	XL_INITN_PORT	1
#define	XL_INITN_PIN	14
#define	XL_RDWRN_PORT	1
#define	XL_RDWRN_PIN	13
#define	XL_CCLK_PORT	1
#define	XL_CCLK_PIN	10
#define	XL_PROGN_PORT	1
#define	XL_PROGN_PIN	25
#define	XL_CSIN_PORT	1
#define	XL_CSIN_PIN	26
#define	XL_DONE_PORT	1
#define	XL_DONE_PIN	27

/*
 * gpio mapping
 *
	XL_config_D0 – gpio1_31
	Xl_config_d1 – gpio1_30
	Xl_config_d2 – gpio1_29
	Xl_config_d3 – gpio1_28
	Xl_config_d4 – gpio1_27
	Xl_config_d5 – gpio1_26
	Xl_config_d6 – gpio1_25
	Xl_config_d7 – gpio1_24
	Xl_config_d8 – gpio1_23
	Xl_config_d9 – gpio1_22
	Xl_config_d10 – gpio1_21
	Xl_config_d11 – gpio1_20
	Xl_config_d12 – gpio1_19
	Xl_config_d13 – gpio1_18
	Xl_config_d14 – gpio1_16
	Xl_config_d15 – gpio1_14
*
*/

/*
 * program bus width in bytes
 */
enum wbus {
	bus_1byte	= 1,
	bus_2byte	= 2,
};

#define MAX_WAIT_DONE	10000

struct gpiobus {
	int	ngpio;
	void __iomem *r[4];
};

int xl_supported_prog_bus_width(enum wbus bus_bytes);

void xl_program_b(int32_t i);
void xl_rdwr_b(int32_t i);
void xl_csi_b(int32_t i);

int xl_get_init_b(void);
int xl_get_done_b(void);

void xl_shift_cclk(int count);
void xl_shift_bytes_out(enum wbus bus_byte, unsigned char *pdata);

int xl_init_io(void);
