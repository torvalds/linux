/*
 * Coldfire generic GPIO support
 *
 * (C) Copyright 2009, Steven King <sfking@fdwdc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
*/

#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfgpio.h>

struct mcf_gpio_chip mcf_gpio_chips[] = {
	MCFGPS(NQ, 1, 7, MCFEPORT_EPDDR, MCFEPORT_EPDR, MCFEPORT_EPPDR),
	MCFGPS(TA, 8, 4, MCFGPTA_GPTDDR, MCFGPTA_GPTPORT, MCFGPTB_GPTPORT),
	MCFGPS(TB, 16, 4, MCFGPTB_GPTDDR, MCFGPTB_GPTPORT, MCFGPTB_GPTPORT),
	MCFGPS(QA, 24, 4, MCFQADC_DDRQA, MCFQADC_PORTQA, MCFQADC_PORTQA),
	MCFGPS(QB, 32, 4, MCFQADC_DDRQB, MCFQADC_PORTQB, MCFQADC_PORTQB),
	MCFGPF(A, 40, 8),
	MCFGPF(B, 48, 8),
	MCFGPF(C, 56, 8),
	MCFGPF(D, 64, 8),
	MCFGPF(E, 72, 8),
	MCFGPF(F, 80, 8),
	MCFGPF(G, 88, 8),
	MCFGPF(H, 96, 8),
	MCFGPF(J, 104, 8),
	MCFGPF(DD, 112, 8),
	MCFGPF(EH, 120, 8),
	MCFGPF(EL, 128, 8),
	MCFGPF(AS, 136, 6),
	MCFGPF(QS, 144, 7),
	MCFGPF(SD, 152, 6),
	MCFGPF(TC, 160, 4),
	MCFGPF(TD, 168, 4),
	MCFGPF(UA, 176, 4),
};

unsigned int mcf_gpio_chips_size = ARRAY_SIZE(mcf_gpio_chips);
