/*
 * $Id: ctctty.h,v 1.4 2003/09/18 08:01:10 mschwide Exp $
 *
 * CTC / ESCON network driver, tty interface.
 *
 * Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Fritz Elfert (elfert@de.ibm.com, felfert@millenux.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _CTCTTY_H_
#define _CTCTTY_H_

#include <linux/skbuff.h>
#include <linux/netdevice.h>

extern int  ctc_tty_register_netdev(struct net_device *);
extern void ctc_tty_unregister_netdev(struct net_device *);
extern void ctc_tty_netif_rx(struct sk_buff *);
extern int  ctc_tty_init(void);
extern void ctc_tty_cleanup(void);
extern void ctc_tty_setcarrier(struct net_device *, int);

#endif
