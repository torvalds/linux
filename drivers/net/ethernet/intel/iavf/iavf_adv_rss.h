/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021, Intel Corporation. */

#ifndef _IAVF_ADV_RSS_H_
#define _IAVF_ADV_RSS_H_

struct iavf_adapter;

/* State of advanced RSS configuration */
enum iavf_adv_rss_state_t {
	IAVF_ADV_RSS_ADD_REQUEST,	/* User requests to add RSS */
	IAVF_ADV_RSS_ADD_PENDING,	/* RSS pending add by the PF */
	IAVF_ADV_RSS_DEL_REQUEST,	/* Driver requests to delete RSS */
	IAVF_ADV_RSS_DEL_PENDING,	/* RSS pending delete by the PF */
	IAVF_ADV_RSS_ACTIVE,		/* RSS configuration is active */
};

/* bookkeeping of advanced RSS configuration */
struct iavf_adv_rss {
	enum iavf_adv_rss_state_t state;
	struct list_head list;

	struct virtchnl_rss_cfg cfg_msg;
};
#endif /* _IAVF_ADV_RSS_H_ */
