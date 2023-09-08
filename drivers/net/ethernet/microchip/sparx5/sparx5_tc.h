/* SPDX-License-Identifier: GPL-2.0+ */
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#ifndef __SPARX5_TC_H__
#define __SPARX5_TC_H__

#include <net/flow_offload.h>
#include <net/pkt_cls.h>
#include <linux/netdevice.h>

/* Controls how PORT_MASK is applied */
enum SPX5_PORT_MASK_MODE {
	SPX5_PMM_OR_DSTMASK,
	SPX5_PMM_AND_VLANMASK,
	SPX5_PMM_REPLACE_PGID,
	SPX5_PMM_REPLACE_ALL,
	SPX5_PMM_REDIR_PGID,
	SPX5_PMM_OR_PGID_MASK,
};

/* Controls ES0 forwarding  */
enum SPX5_FORWARDING_SEL {
	SPX5_FWSEL_NO_ACTION,
	SPX5_FWSEL_COPY_TO_LOOPBACK,
	SPX5_FWSEL_REDIRECT_TO_LOOPBACK,
	SPX5_FWSEL_DISCARD,
};

/* Controls tag A (outer tagging) */
enum SPX5_OUTER_TAG_SEL {
	SPX5_OTAG_PORT,
	SPX5_OTAG_TAG_A,
	SPX5_OTAG_FORCED_PORT,
	SPX5_OTAG_UNTAG,
};

/* Selects TPID for ES0 tag A */
enum SPX5_TPID_A_SEL {
	SPX5_TPID_A_8100,
	SPX5_TPID_A_88A8,
	SPX5_TPID_A_CUST1,
	SPX5_TPID_A_CUST2,
	SPX5_TPID_A_CUST3,
	SPX5_TPID_A_CLASSIFIED,
};

/* Selects VID for ES0 tag A */
enum SPX5_VID_A_SEL {
	SPX5_VID_A_CLASSIFIED,
	SPX5_VID_A_VAL,
	SPX5_VID_A_IFH,
	SPX5_VID_A_RESERVED,
};

/* Select PCP source for ES0 tag A */
enum SPX5_PCP_A_SEL {
	SPX5_PCP_A_CLASSIFIED,
	SPX5_PCP_A_VAL,
	SPX5_PCP_A_RESERVED,
	SPX5_PCP_A_POPPED,
	SPX5_PCP_A_MAPPED_0,
	SPX5_PCP_A_MAPPED_1,
	SPX5_PCP_A_MAPPED_2,
	SPX5_PCP_A_MAPPED_3,
};

/* Select DEI source for ES0 tag A */
enum SPX5_DEI_A_SEL {
	SPX5_DEI_A_CLASSIFIED,
	SPX5_DEI_A_VAL,
	SPX5_DEI_A_REW,
	SPX5_DEI_A_POPPED,
	SPX5_DEI_A_MAPPED_0,
	SPX5_DEI_A_MAPPED_1,
	SPX5_DEI_A_MAPPED_2,
	SPX5_DEI_A_MAPPED_3,
};

/* Controls tag B (inner tagging) */
enum SPX5_INNER_TAG_SEL {
	SPX5_ITAG_NO_PUSH,
	SPX5_ITAG_PUSH_B_TAG,
};

/* Selects TPID for ES0 tag B. */
enum SPX5_TPID_B_SEL {
	SPX5_TPID_B_8100,
	SPX5_TPID_B_88A8,
	SPX5_TPID_B_CUST1,
	SPX5_TPID_B_CUST2,
	SPX5_TPID_B_CUST3,
	SPX5_TPID_B_CLASSIFIED,
};

int sparx5_port_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			 void *type_data);

int sparx5_tc_matchall(struct net_device *ndev,
		       struct tc_cls_matchall_offload *tmo,
		       bool ingress);

int sparx5_tc_flower(struct net_device *ndev, struct flow_cls_offload *fco,
		     bool ingress);

#endif	/* __SPARX5_TC_H__ */
