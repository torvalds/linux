/*****************************************************************************
 *                                                                           *
 * File: tp.c                                                                *
 * $Revision: 1.6 $                                                          *
 * $Date: 2005/03/23 07:15:59 $                                              *
 * Description:                                                              *
 *  Core ASIC Management.                                                    *
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

#include "common.h"
#include "regs.h"
#include "tp.h"

struct petp {
	adapter_t *adapter;
};

/* Pause deadlock avoidance parameters */
#define DROP_MSEC 16
#define DROP_PKTS_CNT  1


static void tp_init(adapter_t *ap, const struct tp_params *p,
		    unsigned int tp_clk)
{
	if (t1_is_asic(ap)) {
		u32 val;

		val = F_TP_IN_CSPI_CPL | F_TP_IN_CSPI_CHECK_IP_CSUM |
		      F_TP_IN_CSPI_CHECK_TCP_CSUM | F_TP_IN_ESPI_ETHERNET;
		if (!p->pm_size)
			val |= F_OFFLOAD_DISABLE;
		else
			val |= F_TP_IN_ESPI_CHECK_IP_CSUM |
				F_TP_IN_ESPI_CHECK_TCP_CSUM;
		t1_write_reg_4(ap, A_TP_IN_CONFIG, val);
		t1_write_reg_4(ap, A_TP_OUT_CONFIG, F_TP_OUT_CSPI_CPL |
			       F_TP_OUT_ESPI_ETHERNET |
			       F_TP_OUT_ESPI_GENERATE_IP_CSUM |
			       F_TP_OUT_ESPI_GENERATE_TCP_CSUM);
		t1_write_reg_4(ap, A_TP_GLOBAL_CONFIG, V_IP_TTL(64) |
			       F_PATH_MTU /* IP DF bit */ |
			       V_5TUPLE_LOOKUP(p->use_5tuple_mode) |
			       V_SYN_COOKIE_PARAMETER(29));

		/*
		 * Enable pause frame deadlock prevention.
		 */
		if (is_T2(ap)) {
			u32 drop_ticks = DROP_MSEC * (tp_clk / 1000);

			t1_write_reg_4(ap, A_TP_TX_DROP_CONFIG,
				       F_ENABLE_TX_DROP | F_ENABLE_TX_ERROR |
				       V_DROP_TICKS_CNT(drop_ticks) |
				       V_NUM_PKTS_DROPPED(DROP_PKTS_CNT));
		}

	}
}

void t1_tp_destroy(struct petp *tp)
{
	kfree(tp);
}

struct petp * __devinit t1_tp_create(adapter_t *adapter, struct tp_params *p)
{
	struct petp *tp = kmalloc(sizeof(*tp), GFP_KERNEL);
	if (!tp)
		return NULL;
	memset(tp, 0, sizeof(*tp));
	tp->adapter = adapter;

	return tp;
}

void t1_tp_intr_enable(struct petp *tp)
{
	u32 tp_intr = t1_read_reg_4(tp->adapter, A_PL_ENABLE);

	{
		/* We don't use any TP interrupts */
		t1_write_reg_4(tp->adapter, A_TP_INT_ENABLE, 0);
		t1_write_reg_4(tp->adapter, A_PL_ENABLE,
			       tp_intr | F_PL_INTR_TP);
	}
}

void t1_tp_intr_disable(struct petp *tp)
{
	u32 tp_intr = t1_read_reg_4(tp->adapter, A_PL_ENABLE);

	{
		t1_write_reg_4(tp->adapter, A_TP_INT_ENABLE, 0);
		t1_write_reg_4(tp->adapter, A_PL_ENABLE,
			       tp_intr & ~F_PL_INTR_TP);
	}
}

void t1_tp_intr_clear(struct petp *tp)
{
	t1_write_reg_4(tp->adapter, A_TP_INT_CAUSE, 0xffffffff);
	t1_write_reg_4(tp->adapter, A_PL_CAUSE, F_PL_INTR_TP);
}

int t1_tp_intr_handler(struct petp *tp)
{
	u32 cause;


	cause = t1_read_reg_4(tp->adapter, A_TP_INT_CAUSE);
	t1_write_reg_4(tp->adapter, A_TP_INT_CAUSE, cause);
	return 0;
}

static void set_csum_offload(struct petp *tp, u32 csum_bit, int enable)
{
	u32 val = t1_read_reg_4(tp->adapter, A_TP_GLOBAL_CONFIG);

	if (enable)
		val |= csum_bit;
	else
		val &= ~csum_bit;
	t1_write_reg_4(tp->adapter, A_TP_GLOBAL_CONFIG, val);
}

void t1_tp_set_ip_checksum_offload(struct petp *tp, int enable)
{
	set_csum_offload(tp, F_IP_CSUM, enable);
}

void t1_tp_set_udp_checksum_offload(struct petp *tp, int enable)
{
	set_csum_offload(tp, F_UDP_CSUM, enable);
}

void t1_tp_set_tcp_checksum_offload(struct petp *tp, int enable)
{
	set_csum_offload(tp, F_TCP_CSUM, enable);
}

/*
 * Initialize TP state.  tp_params contains initial settings for some TP
 * parameters, particularly the one-time PM and CM settings.
 */
int t1_tp_reset(struct petp *tp, struct tp_params *p, unsigned int tp_clk)
{
	int busy = 0;
	adapter_t *adapter = tp->adapter;

	tp_init(adapter, p, tp_clk);
	if (!busy)
		t1_write_reg_4(adapter, A_TP_RESET, F_TP_RESET);
	else
		CH_ERR("%s: TP initialization timed out\n",
		       adapter->name);
	return busy;
}
