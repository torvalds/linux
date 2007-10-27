/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2005 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#define LOG_ELS                       0x1	/* ELS events */
#define LOG_DISCOVERY                 0x2	/* Link discovery events */
#define LOG_MBOX                      0x4	/* Mailbox events */
#define LOG_INIT                      0x8	/* Initialization events */
#define LOG_LINK_EVENT                0x10	/* Link events */
#define LOG_IP                        0x20	/* IP traffic history */
#define LOG_FCP                       0x40	/* FCP traffic history */
#define LOG_NODE                      0x80	/* Node table events */
#define LOG_TEMP                      0x100	/* Temperature sensor events */
#define LOG_MISC                      0x400	/* Miscellaneous events */
#define LOG_SLI                       0x800	/* SLI events */
#define LOG_FCP_ERROR                 0x1000	/* log errors, not underruns */
#define LOG_LIBDFC                    0x2000	/* Libdfc events */
#define LOG_VPORT                     0x4000	/* NPIV events */
#define LOG_ALL_MSG                   0xffff	/* LOG all messages */

#define lpfc_printf_vlog(vport, level, mask, fmt, arg...) \
	{ if (((mask) &(vport)->cfg_log_verbose) || (level[1] <= '3')) \
		dev_printk(level, &((vport)->phba->pcidev)->dev, "%d:(%d):" \
			   fmt, (vport)->phba->brd_no, vport->vpi, ##arg); }

#define lpfc_printf_log(phba, level, mask, fmt, arg...) \
	{ if (((mask) &(phba)->pport->cfg_log_verbose) || (level[1] <= '3')) \
		dev_printk(level, &((phba)->pcidev)->dev, "%d:" \
			   fmt, phba->brd_no, ##arg); }
