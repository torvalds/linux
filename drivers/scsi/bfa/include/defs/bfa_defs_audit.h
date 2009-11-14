/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __BFA_DEFS_AUDIT_H__
#define __BFA_DEFS_AUDIT_H__

#include <bfa_os_inc.h>

/**
 * BFA audit events
 */
enum bfa_audit_aen_event {
	BFA_AUDIT_AEN_AUTH_ENABLE 	= 1,
	BFA_AUDIT_AEN_AUTH_DISABLE 	= 2,
};

/**
 * audit event data
 */
struct bfa_audit_aen_data_s {
	wwn_t           pwwn;
};

#endif /* __BFA_DEFS_AUDIT_H__ */
