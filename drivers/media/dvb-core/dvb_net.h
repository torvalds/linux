/*
 * dvb_net.h
 *
 * Copyright (C) 2001 Ralph Metzler for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DVB_NET_H_
#define _DVB_NET_H_

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "dvbdev.h"

#define DVB_NET_DEVICES_MAX 10

#ifdef CONFIG_DVB_NET

struct dvb_net {
	struct dvb_device *dvbdev;
	struct net_device *device[DVB_NET_DEVICES_MAX];
	int state[DVB_NET_DEVICES_MAX];
	unsigned int exit:1;
	struct dmx_demux *demux;
	struct mutex ioctl_mutex;
};

void dvb_net_release(struct dvb_net *);
int  dvb_net_init(struct dvb_adapter *, struct dvb_net *, struct dmx_demux *);

#else

struct dvb_net {
	struct dvb_device *dvbdev;
};

static inline void dvb_net_release(struct dvb_net *dvbnet)
{
}

static inline int dvb_net_init(struct dvb_adapter *adap,
			       struct dvb_net *dvbnet, struct dmx_demux *dmx)
{
	return 0;
}

#endif /* ifdef CONFIG_DVB_NET */

#endif
