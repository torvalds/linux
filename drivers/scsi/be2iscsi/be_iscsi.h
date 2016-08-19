/**
 * Copyright (C) 2005 - 2015 Avago Technologies
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Written by: Jayamohan Kallickal (jayamohan.kallickal@avagotech.com)
 *
 * Contact Information:
 * linux-drivers@avagotech.com
 *
 * Avago Technologies
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#ifndef _BE_ISCSI_
#define _BE_ISCSI_

#include "be_main.h"
#include "be_mgmt.h"

void beiscsi_iface_create_default(struct beiscsi_hba *phba);

void beiscsi_iface_destroy_default(struct beiscsi_hba *phba);

int beiscsi_iface_get_param(struct iscsi_iface *iface,
			     enum iscsi_param_type param_type,
			     int param, char *buf);

int beiscsi_iface_set_param(struct Scsi_Host *shost,
			     void *data, uint32_t count);

umode_t beiscsi_attr_is_visible(int param_type, int param);

void beiscsi_offload_connection(struct beiscsi_conn *beiscsi_conn,
				struct beiscsi_offload_params *params);

void beiscsi_offload_iscsi(struct beiscsi_hba *phba, struct iscsi_conn *conn,
			   struct beiscsi_conn *beiscsi_conn,
			   unsigned int fw_handle);

struct iscsi_cls_session *beiscsi_session_create(struct iscsi_endpoint *ep,
						 uint16_t cmds_max,
						 uint16_t qdepth,
						 uint32_t initial_cmdsn);

void beiscsi_session_destroy(struct iscsi_cls_session *cls_session);

struct iscsi_cls_conn *beiscsi_conn_create(struct iscsi_cls_session
					   *cls_session, uint32_t cid);

int beiscsi_conn_bind(struct iscsi_cls_session *cls_session,
		      struct iscsi_cls_conn *cls_conn,
		      uint64_t transport_fd, int is_leading);

int beiscsi_ep_get_param(struct iscsi_endpoint *ep, enum iscsi_param param,
			 char *buf);

int beiscsi_get_host_param(struct Scsi_Host *shost,
			   enum iscsi_host_param param, char *buf);

int beiscsi_get_macaddr(char *buf, struct beiscsi_hba *phba);

int beiscsi_set_param(struct iscsi_cls_conn *cls_conn,
		      enum iscsi_param param, char *buf, int buflen);

int beiscsi_conn_start(struct iscsi_cls_conn *cls_conn);

struct iscsi_endpoint *beiscsi_ep_connect(struct Scsi_Host *shost,
					  struct sockaddr *dst_addr,
					  int non_blocking);

int beiscsi_ep_poll(struct iscsi_endpoint *ep, int timeout_ms);

void beiscsi_ep_disconnect(struct iscsi_endpoint *ep);

void beiscsi_conn_get_stats(struct iscsi_cls_conn *cls_conn,
			    struct iscsi_stats *stats);

#endif
