/**
 * Copyright (C) 2005 - 2010 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Written by: Jayamohan Kallickal (jayamohank@serverengines.com)
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 *
 */

#ifndef _BE_ISCSI_
#define _BE_ISCSI_

#include "be_main.h"
#include "be_mgmt.h"

#define BE2_IPV4  0x1
#define BE2_IPV6  0x10

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

int beiscsi_conn_get_param(struct iscsi_cls_conn *cls_conn,
			   enum iscsi_param param, char *buf);

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
