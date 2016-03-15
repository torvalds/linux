/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/lustre_param.h
 *
 * User-settable parameter keys
 *
 * Author: Nathan Rutman <nathan@clusterfs.com>
 */

#ifndef _LUSTRE_PARAM_H
#define _LUSTRE_PARAM_H

/** \defgroup param param
 *
 * @{
 */

/* For interoperability */
struct cfg_interop_param {
	char *old_param;
	char *new_param;
};

/* obd_config.c */
int class_find_param(char *buf, char *key, char **valp);
int class_parse_nid(char *buf, lnet_nid_t *nid, char **endh);
int class_parse_nid_quiet(char *buf, lnet_nid_t *nid, char **endh);

/****************** User-settable parameter keys *********************/
/* e.g.
	tunefs.lustre --param="failover.node=192.168.0.13@tcp0" /dev/sda
	lctl conf_param testfs-OST0000 failover.node=3@elan,192.168.0.3@tcp0
		    ... testfs-MDT0000.lov.stripesize=4M
		    ... testfs-OST0000.ost.client_cache_seconds=15
		    ... testfs.sys.timeout=<secs>
		    ... testfs.llite.max_read_ahead_mb=16
*/

/* System global or special params not handled in obd's proc
 * See mgs_write_log_sys()
 */
#define PARAM_TIMEOUT	      "timeout="	  /* global */
#define PARAM_LDLM_TIMEOUT	 "ldlm_timeout="     /* global */
#define PARAM_AT_MIN	       "at_min="	   /* global */
#define PARAM_AT_MAX	       "at_max="	   /* global */
#define PARAM_AT_EXTRA	     "at_extra="	 /* global */
#define PARAM_AT_EARLY_MARGIN      "at_early_margin="  /* global */
#define PARAM_AT_HISTORY	   "at_history="       /* global */
#define PARAM_JOBID_VAR		   "jobid_var="	       /* global */
#define PARAM_MGSNODE	      "mgsnode="	  /* only at mounttime */
#define PARAM_FAILNODE	     "failover.node="    /* add failover nid */
#define PARAM_FAILMODE	     "failover.mode="    /* initial mount only */
#define PARAM_ACTIVE	       "active="	   /* activate/deactivate */
#define PARAM_NETWORK	      "network="	  /* bind on nid */
#define PARAM_ID_UPCALL		"identity_upcall="  /* identity upcall */

/* Prefixes for parameters handled by obd's proc methods (XXX_process_config) */
#define PARAM_OST		  "ost."
#define PARAM_OSC		  "osc."
#define PARAM_MDT		  "mdt."
#define PARAM_MDD		  "mdd."
#define PARAM_MDC		  "mdc."
#define PARAM_LLITE		"llite."
#define PARAM_LOV		  "lov."
#define PARAM_LOD		"lod."
#define PARAM_OSP		"osp."
#define PARAM_SYS		  "sys."	      /* global */
#define PARAM_SRPC		 "srpc."
#define PARAM_SRPC_FLVR	    "srpc.flavor."
#define PARAM_SRPC_UDESC	   "srpc.udesc.cli2mdt"
#define PARAM_SEC		  "security."
#define PARAM_QUOTA		"quota."	    /* global */

/** @} param */

#endif /* _LUSTRE_PARAM_H */
