/*****************************************************************************
 *                                                                           *
 * File: ch_ethtool.h                                                        *
 * $Revision: 1.5 $                                                          *
 * $Date: 2005/03/23 07:15:58 $                                              *
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

#ifndef __CHETHTOOL_LINUX_H__
#define __CHETHTOOL_LINUX_H__

/* TCB size in 32-bit words */
#define TCB_WORDS (TCB_SIZE / 4)

enum {
	ETHTOOL_SETREG,
	ETHTOOL_GETREG,
	ETHTOOL_SETTPI,
	ETHTOOL_GETTPI,
	ETHTOOL_DEVUP,
	ETHTOOL_GETMTUTAB,
	ETHTOOL_SETMTUTAB,
	ETHTOOL_GETMTU,
	ETHTOOL_SET_PM,
	ETHTOOL_GET_PM,
	ETHTOOL_GET_TCAM,
	ETHTOOL_SET_TCAM,
	ETHTOOL_GET_TCB,
	ETHTOOL_READ_TCAM_WORD,
};

struct ethtool_reg {
	uint32_t cmd;
	uint32_t addr;
	uint32_t val;
};

struct ethtool_mtus {
	uint32_t cmd;
	uint16_t mtus[NMTUS];
};

struct ethtool_pm {
	uint32_t cmd;
	uint32_t tx_pg_sz;
	uint32_t tx_num_pg;
	uint32_t rx_pg_sz;
	uint32_t rx_num_pg;
	uint32_t pm_total;
};

struct ethtool_tcam {
	uint32_t cmd;
	uint32_t tcam_size;
	uint32_t nservers;
	uint32_t nroutes;
};

struct ethtool_tcb {
	uint32_t cmd;
	uint32_t tcb_index;
	uint32_t tcb_data[TCB_WORDS];
};

struct ethtool_tcam_word {
	uint32_t cmd;
	uint32_t addr;
	uint32_t buf[3];
};

#define SIOCCHETHTOOL SIOCDEVPRIVATE
#endif
