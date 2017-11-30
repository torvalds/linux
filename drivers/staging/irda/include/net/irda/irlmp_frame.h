/*********************************************************************
 *                
 * Filename:      irlmp_frame.h
 * Version:       0.9
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Aug 19 02:09:59 1997
 * Modified at:   Fri Dec 10 13:21:53 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997, 1999 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
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

#ifndef IRMLP_FRAME_H
#define IRMLP_FRAME_H

#include <linux/skbuff.h>

#include <net/irda/discovery.h>

/* IrLMP frame opcodes */
#define CONNECT_CMD    0x01
#define CONNECT_CNF    0x81
#define DISCONNECT     0x02
#define ACCESSMODE_CMD 0x03
#define ACCESSMODE_CNF 0x83

#define CONTROL_BIT    0x80

void irlmp_send_data_pdu(struct lap_cb *self, __u8 dlsap, __u8 slsap,
				int expedited, struct sk_buff *skb);
void irlmp_send_lcf_pdu(struct lap_cb *self, __u8 dlsap, __u8 slsap, 
			__u8 opcode, struct sk_buff *skb);
void irlmp_link_data_indication(struct lap_cb *, struct sk_buff *, 
				int unreliable);
#ifdef CONFIG_IRDA_ULTRA
void irlmp_link_unitdata_indication(struct lap_cb *, struct sk_buff *);
#endif /* CONFIG_IRDA_ULTRA */

void irlmp_link_connect_indication(struct lap_cb *, __u32 saddr, __u32 daddr,
				   struct qos_info *qos, struct sk_buff *skb);
void irlmp_link_connect_request(__u32 daddr);
void irlmp_link_connect_confirm(struct lap_cb *self, struct qos_info *qos, 
				struct sk_buff *skb);
void irlmp_link_disconnect_indication(struct lap_cb *, struct irlap_cb *, 
				      LAP_REASON reason, struct sk_buff *); 
void irlmp_link_discovery_confirm(struct lap_cb *self, hashbin_t *log);
void irlmp_link_discovery_indication(struct lap_cb *, discovery_t *discovery);

#endif
