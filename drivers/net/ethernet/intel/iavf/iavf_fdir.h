/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021, Intel Corporation. */

#ifndef _IAVF_FDIR_H_
#define _IAVF_FDIR_H_

struct iavf_adapter;

/* State of Flow Director filter */
enum iavf_fdir_fltr_state_t {
	IAVF_FDIR_FLTR_ADD_REQUEST,	/* User requests to add filter */
	IAVF_FDIR_FLTR_ADD_PENDING,	/* Filter pending add by the PF */
	IAVF_FDIR_FLTR_DEL_REQUEST,	/* User requests to delete filter */
	IAVF_FDIR_FLTR_DEL_PENDING,	/* Filter pending delete by the PF */
	IAVF_FDIR_FLTR_ACTIVE,		/* Filter is active */
};

/* bookkeeping of Flow Director filters */
struct iavf_fdir_fltr {
	enum iavf_fdir_fltr_state_t state;
	struct list_head list;

	u32 flow_id;

	struct virtchnl_fdir_add vc_add_msg;
};

int iavf_fill_fdir_add_msg(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr);
void iavf_print_fdir_fltr(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr);
bool iavf_fdir_is_dup_fltr(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr);
void iavf_fdir_list_add_fltr(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr);
struct iavf_fdir_fltr *iavf_find_fdir_fltr_by_loc(struct iavf_adapter *adapter, u32 loc);
#endif /* _IAVF_FDIR_H_ */
