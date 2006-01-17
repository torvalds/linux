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

#ifndef IRQ_RETVAL
#define IRQ_RETVAL(x)
typedef void irqreturn_t;
#endif

typedef irqreturn_t (*intr_handler_t)(int, void *, struct pt_regs *);

struct sge_intr_counts {
	unsigned int respQ_empty;      /* # times respQ empty */
	unsigned int respQ_overflow;   /* # respQ overflow (fatal) */
	unsigned int freelistQ_empty;  /* # times freelist empty */
	unsigned int pkt_too_big;      /* packet too large (fatal) */
	unsigned int pkt_mismatch;
	unsigned int cmdQ_full[3];     /* not HW IRQ, host cmdQ[] full */
	unsigned int cmdQ_restarted[3];/* # of times cmdQ X was restarted */
	unsigned int ethernet_pkts;    /* # of Ethernet packets received */
	unsigned int offload_pkts;     /* # of offload packets received */
	unsigned int offload_bundles;  /* # of offload pkt bundles delivered */
	unsigned int pure_rsps;        /* # of non-payload responses */
	unsigned int unhandled_irqs;   /* # of unhandled interrupts */
	unsigned int tx_ipfrags;
	unsigned int tx_reg_pkts;
	unsigned int tx_lso_pkts;
	unsigned int tx_do_cksum;
};

struct sge_port_stats {
	unsigned long rx_cso_good;     /* # of successful RX csum offloads */
	unsigned long tx_cso;          /* # of TX checksum offloads */
	unsigned long vlan_xtract;     /* # of VLAN tag extractions */
	unsigned long vlan_insert;     /* # of VLAN tag extractions */
	unsigned long tso;             /* # of TSO requests */
	unsigned long rx_drops;        /* # of packets dropped due to no mem */
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
intr_handler_t t1_select_intr_handler(adapter_t *adapter);
int t1_start_xmit(struct sk_buff *skb, struct net_device *dev);
void t1_set_vlan_accel(struct adapter *adapter, int on_off);
void t1_sge_start(struct sge *);
void t1_sge_stop(struct sge *);
int t1_sge_intr_error_handler(struct sge *);
void t1_sge_intr_enable(struct sge *);
void t1_sge_intr_disable(struct sge *);
void t1_sge_intr_clear(struct sge *);
const struct sge_intr_counts *t1_sge_get_intr_counts(struct sge *sge);
const struct sge_port_stats *t1_sge_get_port_stats(struct sge *sge, int port);

#endif /* _CXGB_SGE_H_ */
