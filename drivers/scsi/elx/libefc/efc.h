/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#ifndef __EFC_H__
#define __EFC_H__

#include "../include/efc_common.h"
#include "efclib.h"
#include "efc_sm.h"
#include "efc_cmds.h"
#include "efc_domain.h"
#include "efc_nport.h"
#include "efc_node.h"
#include "efc_fabric.h"
#include "efc_device.h"
#include "efc_els.h"

#define EFC_MAX_REMOTE_NODES			2048
#define NODE_SPARAMS_SIZE			256

enum efc_scsi_del_initiator_reason {
	EFC_SCSI_INITIATOR_DELETED,
	EFC_SCSI_INITIATOR_MISSING,
};

enum efc_scsi_del_target_reason {
	EFC_SCSI_TARGET_DELETED,
	EFC_SCSI_TARGET_MISSING,
};

#define EFC_FC_ELS_DEFAULT_RETRIES		3

#define domain_sm_trace(domain) \
	efc_log_debug(domain->efc, "[domain:%s] %-20s %-20s\n", \
		      domain->display_name, __func__, efc_sm_event_name(evt)) \

#define domain_trace(domain, fmt, ...) \
	efc_log_debug(domain->efc, \
		      "[%s]" fmt, domain->display_name, ##__VA_ARGS__) \

#define node_sm_trace() \
	efc_log_debug(node->efc, "[%s] %-20s %-20s\n", \
		      node->display_name, __func__, efc_sm_event_name(evt)) \

#define nport_sm_trace(nport) \
	efc_log_debug(nport->efc, \
		"[%s] %-20s\n", nport->display_name, efc_sm_event_name(evt)) \

#endif /* __EFC_H__ */
