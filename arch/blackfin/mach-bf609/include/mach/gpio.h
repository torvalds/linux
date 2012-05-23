/*
 * Copyright 2007-2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#ifndef _MACH_GPIO_H_
#define _MACH_GPIO_H_

#define MAX_BLACKFIN_GPIOS 112

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
#define GPIO_PB15	31
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
#define GPIO_PC14	46
#define GPIO_PC15	47
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


#define BFIN_GPIO_PINT 1


#ifndef __ASSEMBLY__

struct gpio_port_t {
	unsigned long port_fer;
	unsigned long port_fer_set;
	unsigned long port_fer_clear;
	unsigned long data;
	unsigned long data_set;
	unsigned long data_clear;
	unsigned long dir;
	unsigned long dir_set;
	unsigned long dir_clear;
	unsigned long inen;
	unsigned long inen_set;
	unsigned long inen_clear;
	unsigned long port_mux;
	unsigned long toggle;
	unsigned long polar;
	unsigned long polar_set;
	unsigned long polar_clear;
	unsigned long lock;
	unsigned long spare;
	unsigned long revid;
};

struct gpio_port_s {
	unsigned short fer;
	unsigned short data;
	unsigned short dir;
	unsigned short inen;
	unsigned int mux;
};

#endif

#include <mach-common/ports-a.h>
#include <mach-common/ports-b.h>
#include <mach-common/ports-c.h>
#include <mach-common/ports-d.h>
#include <mach-common/ports-e.h>
#include <mach-common/ports-f.h>
#include <mach-common/ports-g.h>

#endif /* _MACH_GPIO_H_ */
