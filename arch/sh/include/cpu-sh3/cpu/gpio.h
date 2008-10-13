/*
 *  include/asm-sh/cpu-sh3/gpio.h
 *
 *  Copyright (C) 2007  Markus Brunner, Mark Jonas
 *
 *  Addresses for the Pin Function Controller
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef _CPU_SH3_GPIO_H
#define _CPU_SH3_GPIO_H

#if defined(CONFIG_CPU_SUBTYPE_SH7720) || \
    defined(CONFIG_CPU_SUBTYPE_SH7721)

/* Control registers */
#define PORT_PACR	0xA4050100UL
#define PORT_PBCR	0xA4050102UL
#define PORT_PCCR	0xA4050104UL
#define PORT_PDCR	0xA4050106UL
#define PORT_PECR	0xA4050108UL
#define PORT_PFCR	0xA405010AUL
#define PORT_PGCR	0xA405010CUL
#define PORT_PHCR	0xA405010EUL
#define PORT_PJCR	0xA4050110UL
#define PORT_PKCR	0xA4050112UL
#define PORT_PLCR	0xA4050114UL
#define PORT_PMCR	0xA4050116UL
#define PORT_PPCR	0xA4050118UL
#define PORT_PRCR	0xA405011AUL
#define PORT_PSCR	0xA405011CUL
#define PORT_PTCR	0xA405011EUL
#define PORT_PUCR	0xA4050120UL
#define PORT_PVCR	0xA4050122UL

/* Data registers */
#define PORT_PADR	0xA4050140UL
/* Address of PORT_PBDR is wrong in the datasheet, see errata 2005-09-21 */
#define PORT_PBDR	0xA4050142UL
#define PORT_PCDR	0xA4050144UL
#define PORT_PDDR	0xA4050146UL
#define PORT_PEDR	0xA4050148UL
#define PORT_PFDR	0xA405014AUL
#define PORT_PGDR	0xA405014CUL
#define PORT_PHDR	0xA405014EUL
#define PORT_PJDR	0xA4050150UL
#define PORT_PKDR	0xA4050152UL
#define PORT_PLDR	0xA4050154UL
#define PORT_PMDR	0xA4050156UL
#define PORT_PPDR	0xA4050158UL
#define PORT_PRDR	0xA405015AUL
#define PORT_PSDR	0xA405015CUL
#define PORT_PTDR	0xA405015EUL
#define PORT_PUDR	0xA4050160UL
#define PORT_PVDR	0xA4050162UL

/* Pin Select Registers */
#define PORT_PSELA	0xA4050124UL
#define PORT_PSELB	0xA4050126UL
#define PORT_PSELC	0xA4050128UL
#define PORT_PSELD	0xA405012AUL

#endif

#endif
