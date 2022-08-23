/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2011 Cisco Systems, Inc.  All rights reserved. */

#ifndef _ENIC_PP_H_
#define _ENIC_PP_H_

#define ENIC_PP_BY_INDEX(enic, vf, pp, err) \
	do { \
		if (enic_is_valid_pp_vf(enic, vf, err)) \
			pp = (vf == PORT_SELF_VF) ? enic->pp : enic->pp + vf; \
		else \
			pp = NULL; \
	} while (0)

int enic_process_set_pp_request(struct enic *enic, int vf,
	struct enic_port_profile *prev_pp, int *restore_pp);
int enic_process_get_pp_request(struct enic *enic, int vf,
	int request, u16 *response);
int enic_is_valid_pp_vf(struct enic *enic, int vf, int *err);

#endif /* _ENIC_PP_H_ */
