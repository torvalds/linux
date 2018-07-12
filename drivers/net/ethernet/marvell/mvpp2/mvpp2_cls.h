/*
 * RSS and Classifier definitions for Marvell PPv2 Network Controller
 *
 * Copyright (C) 2014 Marvell
 *
 * Marcin Wojtas <mw@semihalf.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _MVPP2_CLS_H_
#define _MVPP2_CLS_H_

#include "mvpp2.h"
#include "mvpp2_prs.h"

/* Classifier constants */
#define MVPP2_CLS_FLOWS_TBL_SIZE	512
#define MVPP2_CLS_FLOWS_TBL_DATA_WORDS	3
#define MVPP2_CLS_LKP_TBL_SIZE		64
#define MVPP2_CLS_RX_QUEUES		256

/* Classifier flow constants */
enum mvpp2_cls_engine {
	MVPP22_CLS_ENGINE_C2 = 1,
	MVPP22_CLS_ENGINE_C3A,
	MVPP22_CLS_ENGINE_C3B,
	MVPP22_CLS_ENGINE_C4,
	MVPP22_CLS_ENGINE_C3HA = 6,
	MVPP22_CLS_ENGINE_C3HB = 7,
};

enum mvpp2_cls_flow_seq {
	MVPP2_CLS_FLOW_SEQ_NORMAL = 0,
	MVPP2_CLS_FLOW_SEQ_FIRST1,
	MVPP2_CLS_FLOW_SEQ_FIRST2,
	MVPP2_CLS_FLOW_SEQ_LAST,
	MVPP2_CLS_FLOW_SEQ_MIDDLE
};

/* Classifier C2 engine constants */
#define MVPP22_CLS_C2_TCAM_EN(data)		((data) << 16)

enum mvpp22_cls_c2_action {
	MVPP22_C2_NO_UPD = 0,
	MVPP22_C2_NO_UPD_LOCK,
	MVPP22_C2_UPD,
	MVPP22_C2_UPD_LOCK,
};

enum mvpp22_cls_c2_fwd_action {
	MVPP22_C2_FWD_NO_UPD = 0,
	MVPP22_C2_FWD_NO_UPD_LOCK,
	MVPP22_C2_FWD_SW,
	MVPP22_C2_FWD_SW_LOCK,
	MVPP22_C2_FWD_HW,
	MVPP22_C2_FWD_HW_LOCK,
	MVPP22_C2_FWD_HW_LOW_LAT,
	MVPP22_C2_FWD_HW_LOW_LAT_LOCK,
};

#define MVPP2_CLS_C2_TCAM_WORDS			5
#define MVPP2_CLS_C2_ATTR_WORDS			5

struct mvpp2_cls_c2_entry {
	u32 index;
	u32 tcam[MVPP2_CLS_C2_TCAM_WORDS];
	u32 act;
	u32 attr[MVPP2_CLS_C2_ATTR_WORDS];
};

/* Classifier C2 engine entries */
#define MVPP22_CLS_C2_RSS_ENTRY(port)	(port)
#define MVPP22_CLS_C2_N_ENTRIES		MVPP2_MAX_PORTS

#define MVPP22_RSS_FLOW_C2_OFFS		0

struct mvpp2_cls_flow_entry {
	u32 index;
	u32 data[MVPP2_CLS_FLOWS_TBL_DATA_WORDS];
};

struct mvpp2_cls_lookup_entry {
	u32 lkpid;
	u32 way;
	u32 data;
};

void mvpp22_rss_fill_table(struct mvpp2_port *port, u32 table);

void mvpp22_rss_port_init(struct mvpp2_port *port);

void mvpp2_cls_init(struct mvpp2 *priv);

void mvpp2_cls_port_config(struct mvpp2_port *port);

void mvpp2_cls_oversize_rxq_set(struct mvpp2_port *port);

#endif
