/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

/*
 * $Id: lpfc_logmsg.h 1.32 2005/01/25 17:52:01EST sf_support Exp  $
 */

#define LOG_ELS                       0x1	/* ELS events */
#define LOG_DISCOVERY                 0x2	/* Link discovery events */
#define LOG_MBOX                      0x4	/* Mailbox events */
#define LOG_INIT                      0x8	/* Initialization events */
#define LOG_LINK_EVENT                0x10	/* Link events */
#define LOG_IP                        0x20	/* IP traffic history */
#define LOG_FCP                       0x40	/* FCP traffic history */
#define LOG_NODE                      0x80	/* Node table events */
#define LOG_MISC                      0x400	/* Miscellaneous events */
#define LOG_SLI                       0x800	/* SLI events */
#define LOG_CHK_COND                  0x1000	/* FCP Check condition flag */
#define LOG_LIBDFC                    0x2000	/* Libdfc events */
#define LOG_ALL_MSG                   0xffff	/* LOG all messages */

#define lpfc_printf_log(phba, level, mask, fmt, arg...) \
	{ if (((mask) &(phba)->cfg_log_verbose) || (level[1] <= '3')) \
		dev_printk(level, &((phba)->pcidev)->dev, fmt, ##arg); }
