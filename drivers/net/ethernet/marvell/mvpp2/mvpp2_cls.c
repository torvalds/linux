/*
 * RSS and Classifier helpers for Marvell PPv2 Network Controller
 *
 * Copyright (C) 2014 Marvell
 *
 * Marcin Wojtas <mw@semihalf.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include "mvpp2.h"
#include "mvpp2_cls.h"
#include "mvpp2_prs.h"

/* Update classification flow table registers */
static void mvpp2_cls_flow_write(struct mvpp2 *priv,
				 struct mvpp2_cls_flow_entry *fe)
{
	mvpp2_write(priv, MVPP2_CLS_FLOW_INDEX_REG, fe->index);
	mvpp2_write(priv, MVPP2_CLS_FLOW_TBL0_REG,  fe->data[0]);
	mvpp2_write(priv, MVPP2_CLS_FLOW_TBL1_REG,  fe->data[1]);
	mvpp2_write(priv, MVPP2_CLS_FLOW_TBL2_REG,  fe->data[2]);
}

/* Update classification lookup table register */
static void mvpp2_cls_lookup_write(struct mvpp2 *priv,
				   struct mvpp2_cls_lookup_entry *le)
{
	u32 val;

	val = (le->way << MVPP2_CLS_LKP_INDEX_WAY_OFFS) | le->lkpid;
	mvpp2_write(priv, MVPP2_CLS_LKP_INDEX_REG, val);
	mvpp2_write(priv, MVPP2_CLS_LKP_TBL_REG, le->data);
}

static void mvpp2_cls_flow_eng_set(struct mvpp2_cls_flow_entry *fe,
				   int engine)
{
	fe->data[0] &= ~MVPP2_CLS_FLOW_TBL0_ENG(MVPP2_CLS_FLOW_TBL0_ENG_MASK);
	fe->data[0] |= MVPP2_CLS_FLOW_TBL0_ENG(engine);
}

static void mvpp2_cls_flow_port_id_sel(struct mvpp2_cls_flow_entry *fe,
				       bool from_packet)
{
	if (from_packet)
		fe->data[0] |= MVPP2_CLS_FLOW_TBL0_PORT_ID_SEL;
	else
		fe->data[0] &= ~MVPP2_CLS_FLOW_TBL0_PORT_ID_SEL;
}

static void mvpp2_cls_flow_seq_set(struct mvpp2_cls_flow_entry *fe, u32 seq)
{
	fe->data[1] &= ~MVPP2_CLS_FLOW_TBL1_SEQ(MVPP2_CLS_FLOW_TBL1_SEQ_MASK);
	fe->data[1] |= MVPP2_CLS_FLOW_TBL1_SEQ(seq);
}

static void mvpp2_cls_flow_last_set(struct mvpp2_cls_flow_entry *fe,
				    bool is_last)
{
	fe->data[0] &= ~MVPP2_CLS_FLOW_TBL0_LAST;
	fe->data[0] |= !!is_last;
}

static void mvpp2_cls_flow_pri_set(struct mvpp2_cls_flow_entry *fe, int prio)
{
	fe->data[1] &= ~MVPP2_CLS_FLOW_TBL1_PRIO(MVPP2_CLS_FLOW_TBL1_PRIO_MASK);
	fe->data[1] |= MVPP2_CLS_FLOW_TBL1_PRIO(prio);
}

static void mvpp2_cls_flow_port_add(struct mvpp2_cls_flow_entry *fe,
				    u32 port)
{
	fe->data[0] |= MVPP2_CLS_FLOW_TBL0_PORT_ID(port);
}

/* Initialize the Lookup Id table entry for the given flow */
static void mvpp2_cls_flow_lkp_init(struct mvpp2 *priv, int port_id)
{
	struct mvpp2_cls_lookup_entry le;

	le.way = 0;
	le.lkpid = port_id;

	/* The default RxQ for this port is set in the C2 lookup */
	le.data = 0;

	le.data |= MVPP2_CLS_LKP_FLOW_PTR(port_id);
	le.data |= MVPP2_CLS_LKP_TBL_LOOKUP_EN_MASK;

	mvpp2_cls_lookup_write(priv, &le);
}

/* Initialize the flow table entries for the given flow */
static void mvpp2_cls_flow_init(struct mvpp2 *priv, int port_id)
{
	struct mvpp2_cls_flow_entry fe;
	int i;

	/* C2 lookup */
	memset(&fe, 0, sizeof(fe));
	fe.index = port_id;

	mvpp2_cls_flow_eng_set(&fe, MVPP22_CLS_ENGINE_C2);
	mvpp2_cls_flow_port_id_sel(&fe, true);
	mvpp2_cls_flow_last_set(&fe, 1);
	mvpp2_cls_flow_pri_set(&fe, 0);
	mvpp2_cls_flow_seq_set(&fe, MVPP2_CLS_FLOW_SEQ_LAST);

	/* Add all ports */
	for (i = 0; i < MVPP2_MAX_PORTS; i++)
		mvpp2_cls_flow_port_add(&fe, BIT(i));

	mvpp2_cls_flow_write(priv, &fe);
}

static void mvpp2_cls_port_init_flows(struct mvpp2 *priv)
{
	int i;

	for (i = 0; i < MVPP2_MAX_PORTS; i++) {
		mvpp2_cls_flow_lkp_init(priv, i);
		mvpp2_cls_flow_init(priv, i);
	}
}

static void mvpp2_cls_c2_write(struct mvpp2 *priv,
			       struct mvpp2_cls_c2_entry *c2)
{
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_IDX, c2->index);

	/* Write TCAM */
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_DATA0, c2->tcam[0]);
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_DATA1, c2->tcam[1]);
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_DATA2, c2->tcam[2]);
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_DATA3, c2->tcam[3]);
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_DATA4, c2->tcam[4]);

	mvpp2_write(priv, MVPP22_CLS_C2_ACT, c2->act);

	mvpp2_write(priv, MVPP22_CLS_C2_ATTR0, c2->attr[0]);
	mvpp2_write(priv, MVPP22_CLS_C2_ATTR1, c2->attr[1]);
	mvpp2_write(priv, MVPP22_CLS_C2_ATTR2, c2->attr[2]);
	mvpp2_write(priv, MVPP22_CLS_C2_ATTR3, c2->attr[3]);
}

static void mvpp2_port_c2_cls_init(struct mvpp2_port *port)
{
	struct mvpp2_cls_c2_entry c2;
	u8 qh, ql, pmap;

	memset(&c2, 0, sizeof(c2));

	c2.index = MVPP22_CLS_C2_RSS_ENTRY(port->id);

	pmap = BIT(port->id);
	c2.tcam[4] = MVPP22_CLS_C2_PORT_ID(pmap);
	c2.tcam[4] |= MVPP22_CLS_C2_TCAM_EN(MVPP22_CLS_C2_PORT_ID(pmap));

	/* Update RSS status after matching this entry */
	c2.act = MVPP22_CLS_C2_ACT_RSS_EN(MVPP22_C2_UPD_LOCK);

	/* Mark packet as "forwarded to software", needed for RSS */
	c2.act |= MVPP22_CLS_C2_ACT_FWD(MVPP22_C2_FWD_SW_LOCK);

	/* Configure the default rx queue : Update Queue Low and Queue High, but
	 * don't lock, since the rx queue selection might be overridden by RSS
	 */
	c2.act |= MVPP22_CLS_C2_ACT_QHIGH(MVPP22_C2_UPD) |
		   MVPP22_CLS_C2_ACT_QLOW(MVPP22_C2_UPD);

	qh = (port->first_rxq >> 3) & MVPP22_CLS_C2_ATTR0_QHIGH_MASK;
	ql = port->first_rxq & MVPP22_CLS_C2_ATTR0_QLOW_MASK;

	c2.attr[0] = MVPP22_CLS_C2_ATTR0_QHIGH(qh) |
		      MVPP22_CLS_C2_ATTR0_QLOW(ql);

	mvpp2_cls_c2_write(port->priv, &c2);
}

/* Classifier default initialization */
void mvpp2_cls_init(struct mvpp2 *priv)
{
	struct mvpp2_cls_lookup_entry le;
	struct mvpp2_cls_flow_entry fe;
	int index;

	/* Enable classifier */
	mvpp2_write(priv, MVPP2_CLS_MODE_REG, MVPP2_CLS_MODE_ACTIVE_MASK);

	/* Clear classifier flow table */
	memset(&fe.data, 0, sizeof(fe.data));
	for (index = 0; index < MVPP2_CLS_FLOWS_TBL_SIZE; index++) {
		fe.index = index;
		mvpp2_cls_flow_write(priv, &fe);
	}

	/* Clear classifier lookup table */
	le.data = 0;
	for (index = 0; index < MVPP2_CLS_LKP_TBL_SIZE; index++) {
		le.lkpid = index;
		le.way = 0;
		mvpp2_cls_lookup_write(priv, &le);

		le.way = 1;
		mvpp2_cls_lookup_write(priv, &le);
	}

	mvpp2_cls_port_init_flows(priv);
}

void mvpp2_cls_port_config(struct mvpp2_port *port)
{
	struct mvpp2_cls_lookup_entry le;
	u32 val;

	/* Set way for the port */
	val = mvpp2_read(port->priv, MVPP2_CLS_PORT_WAY_REG);
	val &= ~MVPP2_CLS_PORT_WAY_MASK(port->id);
	mvpp2_write(port->priv, MVPP2_CLS_PORT_WAY_REG, val);

	/* Pick the entry to be accessed in lookup ID decoding table
	 * according to the way and lkpid.
	 */
	le.lkpid = port->id;
	le.way = 0;
	le.data = 0;

	/* Set initial CPU queue for receiving packets */
	le.data &= ~MVPP2_CLS_LKP_TBL_RXQ_MASK;
	le.data |= port->first_rxq;

	/* Disable classification engines */
	le.data &= ~MVPP2_CLS_LKP_TBL_LOOKUP_EN_MASK;

	/* Update lookup ID table entry */
	mvpp2_cls_lookup_write(port->priv, &le);

	mvpp2_port_c2_cls_init(port);
}

/* Set CPU queue number for oversize packets */
void mvpp2_cls_oversize_rxq_set(struct mvpp2_port *port)
{
	u32 val;

	mvpp2_write(port->priv, MVPP2_CLS_OVERSIZE_RXQ_LOW_REG(port->id),
		    port->first_rxq & MVPP2_CLS_OVERSIZE_RXQ_LOW_MASK);

	mvpp2_write(port->priv, MVPP2_CLS_SWFWD_P2HQ_REG(port->id),
		    (port->first_rxq >> MVPP2_CLS_OVERSIZE_RXQ_LOW_BITS));

	val = mvpp2_read(port->priv, MVPP2_CLS_SWFWD_PCTRL_REG);
	val |= MVPP2_CLS_SWFWD_PCTRL_MASK(port->id);
	mvpp2_write(port->priv, MVPP2_CLS_SWFWD_PCTRL_REG, val);
}

static inline u32 mvpp22_rxfh_indir(struct mvpp2_port *port, u32 rxq)
{
	int nrxqs, cpu, cpus = num_possible_cpus();

	/* Number of RXQs per CPU */
	nrxqs = port->nrxqs / cpus;

	/* CPU that will handle this rx queue */
	cpu = rxq / nrxqs;

	if (!cpu_online(cpu))
		return port->first_rxq;

	/* Indirection to better distribute the paquets on the CPUs when
	 * configuring the RSS queues.
	 */
	return port->first_rxq + ((rxq * nrxqs + rxq / cpus) % port->nrxqs);
}

void mvpp22_rss_fill_table(struct mvpp2_port *port, u32 table)
{
	struct mvpp2 *priv = port->priv;
	int i;

	for (i = 0; i < MVPP22_RSS_TABLE_ENTRIES; i++) {
		u32 sel = MVPP22_RSS_INDEX_TABLE(table) |
			  MVPP22_RSS_INDEX_TABLE_ENTRY(i);
		mvpp2_write(priv, MVPP22_RSS_INDEX, sel);

		mvpp2_write(priv, MVPP22_RSS_TABLE_ENTRY,
			    mvpp22_rxfh_indir(port, port->indir[i]));
	}
}

void mvpp22_rss_port_init(struct mvpp2_port *port)
{
	struct mvpp2 *priv = port->priv;
	int i;

	/* Set the table width: replace the whole classifier Rx queue number
	 * with the ones configured in RSS table entries.
	 */
	mvpp2_write(priv, MVPP22_RSS_INDEX, MVPP22_RSS_INDEX_TABLE(port->id));
	mvpp2_write(priv, MVPP22_RSS_WIDTH, 8);

	/* The default RxQ is used as a key to select the RSS table to use.
	 * We use one RSS table per port.
	 */
	mvpp2_write(priv, MVPP22_RSS_INDEX,
		    MVPP22_RSS_INDEX_QUEUE(port->first_rxq));
	mvpp2_write(priv, MVPP22_RXQ2RSS_TABLE,
		    MVPP22_RSS_TABLE_POINTER(port->id));

	/* Configure the first table to evenly distribute the packets across
	 * real Rx Queues. The table entries map a hash to a port Rx Queue.
	 */
	for (i = 0; i < MVPP22_RSS_TABLE_ENTRIES; i++)
		port->indir[i] = ethtool_rxfh_indir_default(i, port->nrxqs);

	mvpp22_rss_fill_table(port, port->id);
}
