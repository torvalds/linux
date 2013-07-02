/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Wei WANG <wei_wang@realsil.com.cn>
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#ifndef __RTSX_PCR_H
#define __RTSX_PCR_H

#include <linux/mfd/rtsx_pci.h>

#define MIN_DIV_N_PCR		80
#define MAX_DIV_N_PCR		208

void rts5209_init_params(struct rtsx_pcr *pcr);
void rts5229_init_params(struct rtsx_pcr *pcr);
void rtl8411_init_params(struct rtsx_pcr *pcr);
void rts5227_init_params(struct rtsx_pcr *pcr);
void rts5249_init_params(struct rtsx_pcr *pcr);

#endif
