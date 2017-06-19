/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _QED_DCBX_H
#define _QED_DCBX_H
#include <linux/types.h>
#include <linux/slab.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"

#define DCBX_CONFIG_MAX_APP_PROTOCOL    4

enum qed_mib_read_type {
	QED_DCBX_OPERATIONAL_MIB,
	QED_DCBX_REMOTE_MIB,
	QED_DCBX_LOCAL_MIB,
	QED_DCBX_REMOTE_LLDP_MIB,
	QED_DCBX_LOCAL_LLDP_MIB
};

struct qed_dcbx_app_data {
	bool enable;		/* DCB enabled */
	bool update;		/* Update indication */
	u8 priority;		/* Priority */
	u8 tc;			/* Traffic Class */
};

#define QED_DCBX_VERSION_DISABLED       0
#define QED_DCBX_VERSION_IEEE           1
#define QED_DCBX_VERSION_CEE            2

struct qed_dcbx_set {
#define QED_DCBX_OVERRIDE_STATE	        BIT(0)
#define QED_DCBX_OVERRIDE_PFC_CFG       BIT(1)
#define QED_DCBX_OVERRIDE_ETS_CFG       BIT(2)
#define QED_DCBX_OVERRIDE_APP_CFG       BIT(3)
#define QED_DCBX_OVERRIDE_DSCP_CFG      BIT(4)
	u32 override_flags;
	bool enabled;
	struct qed_dcbx_admin_params config;
	u32 ver_num;
};

struct qed_dcbx_results {
	bool dcbx_enabled;
	u8 pf_id;
	struct qed_dcbx_app_data arr[DCBX_MAX_PROTOCOL_TYPE];
};

struct qed_dcbx_app_metadata {
	enum dcbx_protocol_type id;
	char *name;
	enum qed_pci_personality personality;
};

struct qed_dcbx_info {
	struct lldp_status_params_s lldp_remote[LLDP_MAX_LLDP_AGENTS];
	struct lldp_config_params_s lldp_local[LLDP_MAX_LLDP_AGENTS];
	struct dcbx_local_params local_admin;
	struct qed_dcbx_results results;
	struct dcbx_mib operational;
	struct dcbx_mib remote;
	struct qed_dcbx_set set;
	struct qed_dcbx_get get;
	u8 dcbx_cap;
};

struct qed_dcbx_mib_meta_data {
	struct lldp_config_params_s *lldp_local;
	struct lldp_status_params_s *lldp_remote;
	struct dcbx_local_params *local_admin;
	struct dcbx_mib *mib;
	size_t size;
	u32 addr;
};

#ifdef CONFIG_DCB
int qed_dcbx_get_config_params(struct qed_hwfn *, struct qed_dcbx_set *);

int qed_dcbx_config_params(struct qed_hwfn *,
			   struct qed_ptt *, struct qed_dcbx_set *, bool);
#endif

/* QED local interface routines */
int
qed_dcbx_mib_update_event(struct qed_hwfn *,
			  struct qed_ptt *, enum qed_mib_read_type);

int qed_dcbx_info_alloc(struct qed_hwfn *p_hwfn);
void qed_dcbx_info_free(struct qed_hwfn *p_hwfn);
void qed_dcbx_set_pf_update_params(struct qed_dcbx_results *p_src,
				   struct pf_update_ramrod_data *p_dest);

#endif
