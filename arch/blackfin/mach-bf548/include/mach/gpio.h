/*
 * File:         include/asm-blackfin/mach-bf548/gpio.h
 * Based on:
 * Author:	 Michael Hennerich (hennerich@blackfin.uclinux.org)
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */



#define GPIO_PA0	0
#define GPIO_PA1	1
#define GPIO_PA2	2
#define GPIO_PA3	3
#define GPIO_PA4	4
#define GPIO_PA5	5
#define GPIO_PA6	6
#define GPIO_PA7	7
#define GPIO_PA8	8
#define GPIO_PA9	9
#define GPIO_PA10	10
#define GPIO_PA11	11
#define GPIO_PA12	12
#define GPIO_PA13	13
#define GPIO_PA14	14
#define GPIO_PA15	15
#define GPIO_PB0	16
#define GPIO_PB1	17
#define GPIO_PB2	18
#define GPIO_PB3	19
#define GPIO_PB4	20
#define GPIO_PB5	21
#define GPIO_PB6	22
#define GPIO_PB7	23
#define GPIO_PB8	24
#define GPIO_PB9	25
#define GPIO_PB10	26
#define GPIO_PB11	27
#define GPIO_PB12	28
#define GPIO_PB13	29
#define GPIO_PB14	30
#define GPIO_PB15	31	/* N/A */
#define GPIO_PC0	32
#define GPIO_PC1	33
#define GPIO_PC2	34
#define GPIO_PC3	35
#define GPIO_PC4	36
#define GPIO_PC5	37
#define GPIO_PC6	38
#define GPIO_PC7	39
#define GPIO_PC8	40
#define GPIO_PC9	41
#define GPIO_PC10	42
#define GPIO_PC11	43
#define GPIO_PC12	44
#define GPIO_PC13	45
#define GPIO_PC14	46	/* N/A */
#define GPIO_PC15	47	/* N/A */
#define GPIO_PD0	48
#define GPIO_PD1	49
#define GPIO_PD2	50
#define GPIO_PD3	51
#define GPIO_PD4	52
#define GPIO_PD5	53
#define GPIO_PD6	54
#define GPIO_PD7	55
#define GPIO_PD8	56
#define GPIO_PD9	57
#define GPIO_PD10	58
#define GPIO_PD11	59
#define GPIO_PD12	60
#define GPIO_PD13	61
#define GPIO_PD14	62
#define GPIO_PD15	63
#define GPIO_PE0	64
#define GPIO_PE1	65
#define GPIO_PE2	66
#define GPIO_PE3	67
#define GPIO_PE4	68
#define GPIO_PE5	69
#define GPIO_PE6	70
#define GPIO_PE7	71
#define GPIO_PE8	72
#define GPIO_PE9	73
#define GPIO_PE10	74
#define GPIO_PE11	75
#define GPIO_PE12	76
#define GPIO_PE13	77
#define GPIO_PE14	78
#define GPIO_PE15	79
#define GPIO_PF0	80
#define GPIO_PF1	81
#define GPIO_PF2	82
#define GPIO_PF3	83
#define GPIO_PF4	84
#define GPIO_PF5	85
#define GPIO_PF6	86
#define GPIO_PF7	87
#define GPIO_PF8	88
#define GPIO_PF9	89
#define GPIO_PF10	90
#define GPIO_PF11	91
#define GPIO_PF12	92
#define GPIO_PF13	93
#define GPIO_PF14	94
#define GPIO_PF15	95
#define GPIO_PG0	96
#define GPIO_PG1	97
#define GPIO_PG2	98
#define GPIO_PG3	99
#define GPIO_PG4	100
#define GPIO_PG5	101
#define GPIO_PG6	102
#define GPIO_PG7	103
#define GPIO_PG8	104
#define GPIO_PG9	105
#define GPIO_PG10	106
#define GPIO_PG11	107
#define GPIO_PG12	108
#define GPIO_PG13	109
#define GPIO_PG14	110
#define GPIO_PG15	111
#define GPIO_PH0	112
#define GPIO_PH1	113
#define GPIO_PH2	114
#define GPIO_PH3	115
#define GPIO_PH4	116
#define GPIO_PH5	117
#define GPIO_PH6	118
#define GPIO_PH7	119
#define GPIO_PH8	120
#define GPIO_PH9	121
#define GPIO_PH10	122
#define GPIO_PH11	123
#define GPIO_PH12	124
#define GPIO_PH13	125
#define GPIO_PH14	126	/* N/A */
#define GPIO_PH15	127	/* N/A */
#define GPIO_PI0	128
#define GPIO_PI1	129
#define GPIO_PI2	130
#define GPIO_PI3	131
#define GPIO_PI4	132
#define GPIO_PI5	133
#define GPIO_PI6	134
#define GPIO_PI7	135
#define GPIO_PI8	136
#define GPIO_PI9	137
#define GPIO_PI10	138
#define GPIO_PI11	139
#define GPIO_PI12	140
#define GPIO_PI13	141
#define GPIO_PI14	142
#define GPIO_PI15	143
#define GPIO_PJ0	144
#define GPIO_PJ1	145
#define GPIO_PJ2	146
#define GPIO_PJ3	147
#define GPIO_PJ4	148
#define GPIO_PJ5	149
#define GPIO_PJ6	150
#define GPIO_PJ7	151
#define GPIO_PJ8	152
#define GPIO_PJ9	153
#define GPIO_PJ10	154
#define GPIO_PJ11	155
#define GPIO_PJ12	156
#define GPIO_PJ13	157
#define GPIO_PJ14	158	/* N/A */
#define GPIO_PJ15	159	/* N/A */

#define MAX_BLACKFIN_GPIOS 160

struct gpio_port_t {
	unsigned short port_fer;
	unsigned short dummy1;
	unsigned short data;
	unsigned short dummy2;
	unsigned short data_set;
	unsigned short dummy3;
	unsigned short data_clear;
	unsigned short dummy4;
	unsigned short dir_set;
	unsigned short dummy5;
	unsigned short dir_clear;
	unsigned short dummy6;
	unsigned short inen;
	unsigned short dummy7;
	unsigned int port_mux;
};

struct gpio_port_s {
	unsigned short fer;
	unsigned short data;
	unsigned short dir;
	unsigned short inen;
	unsigned int mux;
};
