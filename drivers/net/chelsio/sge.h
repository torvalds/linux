/*****************************************************************************
 *                                                                           *
 * File: sge.h                                                               *
 * $Revision: 1.11 $                                                          *
 * $Date: 2005/06/21 22:10:55 $                                              *
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

#ifndef _CXGB_SGE_H_
#define _CXGB_SGE_H_

#include <linux/types.h>
#include <linux/interrupt.h>
#include <asm/byteorder.h>

struct sge_intr_counts {
	unsigned int rx_drops;        /* # of packets dropped due to no mem */
	unsigned int pure_rsps;        /* # of non-payload responses */
	unsigned int unhandled_irqs;   /* # of unhandled interrupts */
	unsigned int respQ_empty;      /* # times respQ empty */
	unsigned int respQ_overflow;   /* # respQ overflow (fatal) */
	unsigned int freelistQ_empty;  /* # times freelist empty */
	unsigned int pkt_too_big;      /* packet too large (fatal) */
	unsigned int pkt_mismatch;
	unsigned int cmdQ_full[3];     /* not HW IRQ, host cmdQ[] full */
	unsigned int cmdQ_restarted[3];/* # of times cmdQ X was restarted */
};

struct sge_port_stats {
	u64 rx_packets;      /* # of Ethernet packets received */
	u64 rx_cso_good;     /* # of successful RX csum offloads */
	u64 tx_packets;      /* # of TX packets */
	u64 tx_cso;          /* # of TX checksum offloads */
	u64 tx_tso;          /* # of TSO requests */
	u64 vlan_xtract;     /* # of VLAN tag extractions */
	u64 vlan_insert;     /* # of VLAN tag insertions */
};

struct sk_buff;
struct net_device;
struct adapter;
struct sge_params;
struct sge;

struct sge *t1_sge_create(struct adapter *, struct sge_params *);
int t1_sge_configure(struct sge *, struct sge_params *);
int t1_sge_set_coalesce_params(struct sge *, struct sge_params *);
void t1_sge_destroy(struct sge *);
irq_handler_t t1_select_intr_handler(adapter_t *adapter);
int t1_start_xmit(struct sk_buff *skb, struct net_device *dev);
void t1_set_vlan_accel(struct adapter *adapter, int on_off);
void t1_sge_start(struct sge *);
void t1_sge_stop(struct sge *);
int t1_sge_intr_error_handler(struct sge *);
void t1_sge_intr_enable(struct sge *);
void t1_sge_intr_disable(struct sge *);
void t1_sge_intr_clear(struct sge *);
const struct sge_intr_counts *t1_sge_get_intr_counts(const struct sge *sge);
void t1_sge_get_port_stats(const struct sge *sge, int port, struct sge_port_stats *);
void t1_sched_set_max_avail_bytes(struct sge *, unsigned int);
void t1_sched_set_drain_bits_per_us(struct sge *, unsigned int, unsigned int);
unsigned int t1_sched_update_parms(struct sge *, unsigned int, unsigned int,
			   unsigned int);

#endif /* _CXGB_SGE_H_ */
