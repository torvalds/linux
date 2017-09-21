/*********************************************************************
 *                
 * Filename:      irlan_provider.h
 * Version:       0.1
 * Description:   IrDA LAN access layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Sun May  9 12:26:11 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#ifndef IRLAN_SERVER_H
#define IRLAN_SERVER_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include <net/irda/irlan_common.h>

void irlan_provider_ctrl_disconnect_indication(void *instance, void *sap, 
					       LM_REASON reason, 
					       struct sk_buff *skb);


void irlan_provider_connect_response(struct irlan_cb *, struct tsap_cb *);

int irlan_parse_open_data_cmd(struct irlan_cb *self, struct sk_buff *skb);
int irlan_provider_parse_command(struct irlan_cb *self, int cmd,
				 struct sk_buff *skb);

void irlan_provider_send_reply(struct irlan_cb *self, int command, 
			       int ret_code);
int irlan_provider_open_ctrl_tsap(struct irlan_cb *self);

#endif


