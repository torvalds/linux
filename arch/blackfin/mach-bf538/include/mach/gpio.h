/*
 * File: arch/blackfin/mach-bf538/include/mach/gpio.h
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2008 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */


#ifndef _MACH_GPIO_H_
#define _MACH_GPIO_H_

	/* FIXME:
	 * For now only support PORTF GPIOs.
	 * PORT C,D and E are for peripheral usage only
	 */
#define MAX_BLACKFIN_GPIOS 16

#define	GPIO_PF0	0	/* PF */
#define	GPIO_PF1	1
#define	GPIO_PF2	2
#define	GPIO_PF3	3
#define	GPIO_PF4	4
#define	GPIO_PF5	5
#define	GPIO_PF6	6
#define	GPIO_PF7	7
#define	GPIO_PF8	8
#define	GPIO_PF9	9
#define	GPIO_PF10	10
#define	GPIO_PF11	11
#define	GPIO_PF12	12
#define	GPIO_PF13	13
#define	GPIO_PF14	14
#define	GPIO_PF15	15
#define	GPIO_PC0	16	/* PC */
#define	GPIO_PC1	17
#define	GPIO_PC4	20
#define	GPIO_PC5	21
#define	GPIO_PC6	22
#define	GPIO_PC7	23
#define	GPIO_PC8	24
#define	GPIO_PC9	25
#define	GPIO_PD0	32	/* PD */
#define	GPIO_PD1	33
#define	GPIO_PD2	34
#define	GPIO_PD3	35
#define	GPIO_PD4	36
#define	GPIO_PD5	37
#define	GPIO_PD6	38
#define	GPIO_PD7	39
#define	GPIO_PD8	40
#define	GPIO_PD9	41
#define	GPIO_PD10      	42
#define	GPIO_PD11      	43
#define	GPIO_PD12      	44
#define	GPIO_PD13      	45
#define	GPIO_PE0	48	/* PE */
#define	GPIO_PE1	49
#define	GPIO_PE2	50
#define	GPIO_PE3	51
#define	GPIO_PE4	52
#define	GPIO_PE5	53
#define	GPIO_PE6	54
#define	GPIO_PE7	55
#define	GPIO_PE8	56
#define	GPIO_PE9	57
#define	GPIO_PE10      	58
#define	GPIO_PE11      	59
#define	GPIO_PE12      	60
#define	GPIO_PE13      	61
#define	GPIO_PE14      	62
#define	GPIO_PE15      	63

#define PORT_F GPIO_PF0
#define PORT_C GPIO_PC0
#define PORT_D GPIO_PD0
#define PORT_E GPIO_PE0

#endif /* _MACH_GPIO_H_ */
