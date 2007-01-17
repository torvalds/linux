/*****************************************************************************
 *                                                                           *
 * File: espi.h                                                              *
 * $Revision: 1.7 $                                                          *
 * $Date: 2005/06/21 18:29:47 $                                              *
 * Description:                                                              *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License, version 2, as       *
 * published by the Free Software Foundation.                                *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.                 *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED    *
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF      *
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.                     *
 *                                                                           *
 * http://www.chelsio.com                                                    *
 *                                                                           *
 * Copyright (c) 2003 - 2005 Chelsio Communications, Inc.                    *
 * All rights reserved.                                                      *
 *                                                                           *
 * Maintainers: maintainers@chelsio.com                                      *
 *                                                                           *
 * Authors: Dimitrios Michailidis   <dm@chelsio.com>                         *
 *          Tina Yang               <tainay@chelsio.com>                     *
 *          Felix Marti             <felix@chelsio.com>                      *
 *          Scott Bardone           <sbardone@chelsio.com>                   *
 *          Kurt Ottaway            <kottaway@chelsio.com>                   *
 *          Frank DiMambro          <frank@chelsio.com>                      *
 *                                                                           *
 * History:                                                                  *
 *                                                                           *
 ****************************************************************************/

#ifndef _CXGB_ESPI_H_
#define _CXGB_ESPI_H_

#include "common.h"

struct espi_intr_counts {
	unsigned int DIP4_err;
	unsigned int rx_drops;
	unsigned int tx_drops;
	unsigned int rx_ovflw;
	unsigned int parity_err;
	unsigned int DIP2_parity_err;
};

struct peespi;

struct peespi *t1_espi_create(adapter_t *adapter);
void t1_espi_destroy(struct peespi *espi);
int t1_espi_init(struct peespi *espi, int mac_type, int nports);

void t1_espi_intr_enable(struct peespi *);
void t1_espi_intr_clear(struct peespi *);
void t1_espi_intr_disable(struct peespi *);
int t1_espi_intr_handler(struct peespi *);
const struct espi_intr_counts *t1_espi_get_intr_counts(struct peespi *espi);

void t1_espi_set_misc_ctrl(adapter_t *adapter, u32 val);
u32 t1_espi_get_mon(adapter_t *adapter, u32 addr, u8 wait);
int t1_espi_get_mon_t204(adapter_t *, u32 *, u8);

#endif /* _CXGB_ESPI_H_ */
