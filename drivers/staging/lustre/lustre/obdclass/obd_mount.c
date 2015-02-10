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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/obd_mount.c
 *
 * Client mount routines
 *
 * Author: Nathan Rutman <nathan@clusterfs.com>
 */


#define DEBUG_SUBSYSTEM S_CLASS
#define D_MOUNT (D_SUPER|D_CONFIG/*|D_WARNING */)
#define PRINT_CMD CDEBUG

#include "../include/obd.h"
#include "../include/linux/lustre_compat25.h"
#include "../include/obd_class.h"
#include "../include/lustre/lustre_user.h"
#include "../include/lustre_log.h"
#include "../include/lustre_disk.h"
#include "../include/lustre_param.h"

static int (*client_fill_super)(struct super_block *sb,
				struct vfsmount *mnt);

static void (*kill_super_cb)(struct super_block *sb);

/**************** config llog ********************/

/** Get a config log from the MGS and process it.
 * This func is called for both clients and servers.
 * Continue to process new statements appended to the logs
 * (whenever the config lock is revoked) until lustre_end_log
 * is called.
 * @param sb The superblock is used by the MGC to write to the local copy of
 *   the config log
 * @param logname The name of the llog to replicate from the MGS
 * @param cfg Since the same mgc may be used to follow multiple config logs
 *   (e.g. ost1, ost2, client), the config_llog_instance keeps the state for
 *   this log, and is added to the mgc's list of logs to follow.
 */
int lustre_process_log(struct super_block *sb, char *logname,
		     struct config_llog_instance *cfg)
{
	struct lustre_cfg *lcfg;
	struct lustre_cfg_bufs *bufs;
	struct lustre_sb_info *lsi = s2lsi(sb);
	struct obd_device *mgc = lsi->lsi_mgc;
	int rc;

	LASSERT(mgc);
	LASSERT(cfg);

	OBD_ALLOC_PTR(bufs);
	if (bufs == NULL)
		return -ENOMEM;

	/* mgc_process_config */
	lustre_cfg_bufs_reset(bufs, mgc->obd_name);
	lustre_cfg_bufs_set_string(bufs, 1, logname);
	lustre_cfg_bufs_set(bufs, 2, cfg, sizeof(*cfg));
	lustre_cfg_bufs_set(bufs, 3, &sb, sizeof(sb));
	lcfg = lustre_cfg_new(LCFG_LOG_START, bufs);
	rc = obd_process_config(mgc, sizeof(*lcfg), lcfg);
	lustre_cfg_free(lcfg);

	OBD_FREE_PTR(bufs);

	if (rc == -EINVAL)
		LCONSOLE_ERROR_MSG(0x15b, "%s: The configuration from log '%s' failed from the MGS (%d).  Make sure this client and the MGS are running compatible versions of Lustre.\n",
				   mgc->obd_name, logname, rc);

	if (rc)
		LCONSOLE_ERROR_MSG(0x15c, "%s: The configuration from log '%s' failed (%d). This may be the result of communication errors between this node and the MGS, a bad configuration, or other errors. See the syslog for more information.\n",
				   mgc->obd_name, logname,
				   rc);

	/* class_obd_list(); */
	return rc;
}
EXPORT_SYMBOL(lustre_process_log);

/* Stop watching this config log for updates */
int lustre_end_log(struct super_block *sb, char *logname,
		       struct config_llog_instance *cfg)
{
	struct lustre_cfg *lcfg;
	struct lustre_cfg_bufs bufs;
	struct lustre_sb_info *lsi = s2lsi(sb);
	struct obd_device *mgc = lsi->lsi_mgc;
	int rc;

	if (!mgc)
		return -ENOENT;

	/* mgc_process_config */
	lustre_cfg_bufs_reset(&bufs, mgc->obd_name);
	lustre_cfg_bufs_set_string(&bufs, 1, logname);
	if (cfg)
		lustre_cfg_bufs_set(&bufs, 2, cfg, sizeof(*cfg));
	lcfg = lustre_cfg_new(LCFG_LOG_END, &bufs);
	rc = obd_process_config(mgc, sizeof(*lcfg), lcfg);
	lustre_cfg_free(lcfg);
	return rc;
}
EXPORT_SYMBOL(lustre_end_log);

/**************** obd start *******************/

/** lustre_cfg_bufs are a holdover from 1.4; we can still set these up from
 * lctl (and do for echo cli/srv.
 */
int do_lcfg(char *cfgname, lnet_nid_t nid, int cmd,
	    char *s1, char *s2, char *s3, char *s4)
{
	struct lustre_cfg_bufs bufs;
	struct lustre_cfg    * lcfg = NULL;
	int rc;

	CDEBUG(D_TRACE, "lcfg %s %#x %s %s %s %s\n", cfgname,
	       cmd, s1, s2, s3, s4);

	lustre_cfg_bufs_reset(&bufs, cfgname);
	if (s1)
		lustre_cfg_bufs_set_string(&bufs, 1, s1);
	if (s2)
		lustre_cfg_bufs_set_string(&bufs, 2, s2);
	if (s3)
		lustre_cfg_bufs_set_string(&bufs, 3, s3);
	if (s4)
		lustre_cfg_bufs_set_string(&bufs, 4, s4);

	lcfg = lustre_cfg_new(cmd, &bufs);
	lcfg->lcfg_nid = nid;
	rc = class_process_config(lcfg);
	lustre_cfg_free(lcfg);
	return rc;
}
EXPORT_SYMBOL(do_lcfg);

/** Call class_attach and class_setup.  These methods in turn call
 * obd type-specific methods.
 */
int lustre_start_simple(char *obdname, char *type, char *uuid,
			char *s1, char *s2, char *s3, char *s4)
{
	int rc;
	CDEBUG(D_MOUNT, "Starting obd %s (typ=%s)\n", obdname, type);

	rc = do_lcfg(obdname, 0, LCFG_ATTACH, type, uuid, NULL, NULL);
	if (rc) {
		CERROR("%s attach error %d\n", obdname, rc);
		return rc;
	}
	rc = do_lcfg(obdname, 0, LCFG_SETUP, s1, s2, s3, s4);
	if (rc) {
		CERROR("%s setup error %d\n", obdname, rc);
		do_lcfg(obdname, 0, LCFG_DETACH, NULL, NULL, NULL, NULL);
	}
	return rc;
}

DEFINE_MUTEX(mgc_start_lock);

/** Set up a mgc obd to process startup logs
 *
 * \param sb [in] super block of the mgc obd
 *
 * \retval 0 success, otherwise error code
 */
int lustre_start_mgc(struct super_block *sb)
{
	struct obd_connect_data *data = NULL;
	struct lustre_sb_info *lsi = s2lsi(sb);
	struct obd_device *obd;
	struct obd_export *exp;
	struct obd_uuid *uuid;
	class_uuid_t uuidc;
	lnet_nid_t nid;
	char *mgcname = NULL, *niduuid = NULL, *mgssec = NULL;
	char *ptr;
	int rc = 0, i = 0, j, len;

	LASSERT(lsi->lsi_lmd);

	/* Find the first non-lo MGS nid for our MGC name */
	if (IS_SERVER(lsi)) {
		/* mount -o mgsnode=nid */
		ptr = lsi->lsi_lmd->lmd_mgs;
		if (lsi->lsi_lmd->lmd_mgs &&
		    (class_parse_nid(lsi->lsi_lmd->lmd_mgs, &nid, &ptr) == 0)) {
			i++;
		} else if (IS_MGS(lsi)) {
			lnet_process_id_t id;
			while ((rc = LNetGetId(i++, &id)) != -ENOENT) {
				if (LNET_NETTYP(LNET_NIDNET(id.nid)) == LOLND)
					continue;
				nid = id.nid;
				i++;
				break;
			}
		}
	} else { /* client */
		/* Use nids from mount line: uml1,1@elan:uml2,2@elan:/lustre */
		ptr = lsi->lsi_lmd->lmd_dev;
		if (class_parse_nid(ptr, &nid, &ptr) == 0)
			i++;
	}
	if (i == 0) {
		CERROR("No valid MGS nids found.\n");
		return -EINVAL;
	}

	mutex_lock(&mgc_start_lock);

	len = strlen(LUSTRE_MGC_OBDNAME) + strlen(libcfs_nid2str(nid)) + 1;
	OBD_ALLOC(mgcname, len);
	OBD_ALLOC(niduuid, len + 2);
	if (!mgcname || !niduuid) {
		rc = -ENOMEM;
		goto out_free;
	}
	sprintf(mgcname, "%s%s", LUSTRE_MGC_OBDNAME, libcfs_nid2str(nid));

	mgssec = lsi->lsi_lmd->lmd_mgssec ? lsi->lsi_lmd->lmd_mgssec : "";

	OBD_ALLOC_PTR(data);
	if (data == NULL) {
		rc = -ENOMEM;
		goto out_free;
	}

	obd = class_name2obd(mgcname);
	if (obd && !obd->obd_stopping) {
		int recov_bk;

		rc = obd_set_info_async(NULL, obd->obd_self_export,
					strlen(KEY_MGSSEC), KEY_MGSSEC,
					strlen(mgssec), mgssec, NULL);
		if (rc)
			goto out_free;

		/* Re-using an existing MGC */
		atomic_inc(&obd->u.cli.cl_mgc_refcount);

		/* IR compatibility check, only for clients */
		if (lmd_is_client(lsi->lsi_lmd)) {
			int has_ir;
			int vallen = sizeof(*data);
			__u32 *flags = &lsi->lsi_lmd->lmd_flags;

			rc = obd_get_info(NULL, obd->obd_self_export,
					  strlen(KEY_CONN_DATA), KEY_CONN_DATA,
					  &vallen, data, NULL);
			LASSERT(rc == 0);
			has_ir = OCD_HAS_FLAG(data, IMP_RECOV);
			if (has_ir ^ !(*flags & LMD_FLG_NOIR)) {
				/* LMD_FLG_NOIR is for test purpose only */
				LCONSOLE_WARN(
					"Trying to mount a client with IR setting not compatible with current mgc. Force to use current mgc setting that is IR %s.\n",
					has_ir ? "enabled" : "disabled");
				if (has_ir)
					*flags &= ~LMD_FLG_NOIR;
				else
					*flags |= LMD_FLG_NOIR;
			}
		}

		recov_bk = 0;
		/* If we are restarting the MGS, don't try to keep the MGC's
		   old connection, or registration will fail. */
		if (IS_MGS(lsi)) {
			CDEBUG(D_MOUNT, "New MGS with live MGC\n");
			recov_bk = 1;
		}

		/* Try all connections, but only once (again).
		   We don't want to block another target from starting
		   (using its local copy of the log), but we do want to connect
		   if at all possible. */
		recov_bk++;
		CDEBUG(D_MOUNT, "%s: Set MGC reconnect %d\n", mgcname,
		       recov_bk);
		rc = obd_set_info_async(NULL, obd->obd_self_export,
					sizeof(KEY_INIT_RECOV_BACKUP),
					KEY_INIT_RECOV_BACKUP,
					sizeof(recov_bk), &recov_bk, NULL);
		rc = 0;
		goto out;
	}

	CDEBUG(D_MOUNT, "Start MGC '%s'\n", mgcname);

	/* Add the primary nids for the MGS */
	i = 0;
	sprintf(niduuid, "%s_%x", mgcname, i);
	if (IS_SERVER(lsi)) {
		ptr = lsi->lsi_lmd->lmd_mgs;
		if (IS_MGS(lsi)) {
			/* Use local nids (including LO) */
			lnet_process_id_t id;
			while ((rc = LNetGetId(i++, &id)) != -ENOENT) {
				rc = do_lcfg(mgcname, id.nid,
					     LCFG_ADD_UUID, niduuid,
					     NULL, NULL, NULL);
			}
		} else {
			/* Use mgsnode= nids */
			/* mount -o mgsnode=nid */
			if (lsi->lsi_lmd->lmd_mgs) {
				ptr = lsi->lsi_lmd->lmd_mgs;
			} else if (class_find_param(ptr, PARAM_MGSNODE,
						    &ptr) != 0) {
				CERROR("No MGS nids given.\n");
				rc = -EINVAL;
				goto out_free;
			}
			while (class_parse_nid(ptr, &nid, &ptr) == 0) {
				rc = do_lcfg(mgcname, nid,
					     LCFG_ADD_UUID, niduuid,
					     NULL, NULL, NULL);
				i++;
			}
		}
	} else { /* client */
		/* Use nids from mount line: uml1,1@elan:uml2,2@elan:/lustre */
		ptr = lsi->lsi_lmd->lmd_dev;
		while (class_parse_nid(ptr, &nid, &ptr) == 0) {
			rc = do_lcfg(mgcname, nid,
				     LCFG_ADD_UUID, niduuid, NULL, NULL, NULL);
			i++;
			/* Stop at the first failover nid */
			if (*ptr == ':')
				break;
		}
	}
	if (i == 0) {
		CERROR("No valid MGS nids found.\n");
		rc = -EINVAL;
		goto out_free;
	}
	lsi->lsi_lmd->lmd_mgs_failnodes = 1;

	/* Random uuid for MGC allows easier reconnects */
	OBD_ALLOC_PTR(uuid);
	ll_generate_random_uuid(uuidc);
	class_uuid_unparse(uuidc, uuid);

	/* Start the MGC */
	rc = lustre_start_simple(mgcname, LUSTRE_MGC_NAME,
				 (char *)uuid->uuid, LUSTRE_MGS_OBDNAME,
				 niduuid, NULL, NULL);
	OBD_FREE_PTR(uuid);
	if (rc)
		goto out_free;

	/* Add any failover MGS nids */
	i = 1;
	while (ptr && ((*ptr == ':' ||
	       class_find_param(ptr, PARAM_MGSNODE, &ptr) == 0))) {
		/* New failover node */
		sprintf(niduuid, "%s_%x", mgcname, i);
		j = 0;
		while (class_parse_nid_quiet(ptr, &nid, &ptr) == 0) {
			j++;
			rc = do_lcfg(mgcname, nid,
				     LCFG_ADD_UUID, niduuid, NULL, NULL, NULL);
			if (*ptr == ':')
				break;
		}
		if (j > 0) {
			rc = do_lcfg(mgcname, 0, LCFG_ADD_CONN,
				     niduuid, NULL, NULL, NULL);
			i++;
		} else {
			/* at ":/fsname" */
			break;
		}
	}
	lsi->lsi_lmd->lmd_mgs_failnodes = i;

	obd = class_name2obd(mgcname);
	if (!obd) {
		CERROR("Can't find mgcobd %s\n", mgcname);
		rc = -ENOTCONN;
		goto out_free;
	}

	rc = obd_set_info_async(NULL, obd->obd_self_export,
				strlen(KEY_MGSSEC), KEY_MGSSEC,
				strlen(mgssec), mgssec, NULL);
	if (rc)
		goto out_free;

	/* Keep a refcount of servers/clients who started with "mount",
	   so we know when we can get rid of the mgc. */
	atomic_set(&obd->u.cli.cl_mgc_refcount, 1);

	/* We connect to the MGS at setup, and don't disconnect until cleanup */
	data->ocd_connect_flags = OBD_CONNECT_VERSION | OBD_CONNECT_AT |
				  OBD_CONNECT_FULL20 | OBD_CONNECT_IMP_RECOV |
				  OBD_CONNECT_LVB_TYPE;

#if LUSTRE_VERSION_CODE < OBD_OCD_VERSION(3, 2, 50, 0)
	data->ocd_connect_flags |= OBD_CONNECT_MNE_SWAB;
#else
#warning "LU-1644: Remove old OBD_CONNECT_MNE_SWAB fixup and imp_need_mne_swab"
#endif

	if (lmd_is_client(lsi->lsi_lmd) &&
	    lsi->lsi_lmd->lmd_flags & LMD_FLG_NOIR)
		data->ocd_connect_flags &= ~OBD_CONNECT_IMP_RECOV;
	data->ocd_version = LUSTRE_VERSION_CODE;
	rc = obd_connect(NULL, &exp, obd, &(obd->obd_uuid), data, NULL);
	if (rc) {
		CERROR("connect failed %d\n", rc);
		goto out;
	}

	obd->u.cli.cl_mgc_mgsexp = exp;

out:
	/* Keep the mgc info in the sb. Note that many lsi's can point
	   to the same mgc.*/
	lsi->lsi_mgc = obd;
out_free:
	mutex_unlock(&mgc_start_lock);

	if (data)
		OBD_FREE_PTR(data);
	if (mgcname)
		OBD_FREE(mgcname, len);
	if (niduuid)
		OBD_FREE(niduuid, len + 2);
	return rc;
}

static int lustre_stop_mgc(struct super_block *sb)
{
	struct lustre_sb_info *lsi = s2lsi(sb);
	struct obd_device *obd;
	char *niduuid = NULL, *ptr = NULL;
	int i, rc = 0, len = 0;

	if (!lsi)
		return -ENOENT;
	obd = lsi->lsi_mgc;
	if (!obd)
		return -ENOENT;
	lsi->lsi_mgc = NULL;

	mutex_lock(&mgc_start_lock);
	LASSERT(atomic_read(&obd->u.cli.cl_mgc_refcount) > 0);
	if (!atomic_dec_and_test(&obd->u.cli.cl_mgc_refcount)) {
		/* This is not fatal, every client that stops
		   will call in here. */
		CDEBUG(D_MOUNT, "mgc still has %d references.\n",
		       atomic_read(&obd->u.cli.cl_mgc_refcount));
		rc = -EBUSY;
		goto out;
	}

	/* The MGC has no recoverable data in any case.
	 * force shutdown set in umount_begin */
	obd->obd_no_recov = 1;

	if (obd->u.cli.cl_mgc_mgsexp) {
		/* An error is not fatal, if we are unable to send the
		   disconnect mgs ping evictor cleans up the export */
		rc = obd_disconnect(obd->u.cli.cl_mgc_mgsexp);
		if (rc)
			CDEBUG(D_MOUNT, "disconnect failed %d\n", rc);
	}

	/* Save the obdname for cleaning the nid uuids, which are
	   obdname_XX */
	len = strlen(obd->obd_name) + 6;
	OBD_ALLOC(niduuid, len);
	if (niduuid) {
		strcpy(niduuid, obd->obd_name);
		ptr = niduuid + strlen(niduuid);
	}

	rc = class_manual_cleanup(obd);
	if (rc)
		goto out;

	/* Clean the nid uuids */
	if (!niduuid) {
		rc = -ENOMEM;
		goto out;
	}

	for (i = 0; i < lsi->lsi_lmd->lmd_mgs_failnodes; i++) {
		sprintf(ptr, "_%x", i);
		rc = do_lcfg(LUSTRE_MGC_OBDNAME, 0, LCFG_DEL_UUID,
			     niduuid, NULL, NULL, NULL);
		if (rc)
			CERROR("del MDC UUID %s failed: rc = %d\n",
			       niduuid, rc);
	}
out:
	if (niduuid)
		OBD_FREE(niduuid, len);

	/* class_import_put will get rid of the additional connections */
	mutex_unlock(&mgc_start_lock);
	return rc;
}

/***************** lustre superblock **************/

struct lustre_sb_info *lustre_init_lsi(struct super_block *sb)
{
	struct lustre_sb_info *lsi;

	OBD_ALLOC_PTR(lsi);
	if (!lsi)
		return NULL;
	OBD_ALLOC_PTR(lsi->lsi_lmd);
	if (!lsi->lsi_lmd) {
		OBD_FREE_PTR(lsi);
		return NULL;
	}

	lsi->lsi_lmd->lmd_exclude_count = 0;
	lsi->lsi_lmd->lmd_recovery_time_soft = 0;
	lsi->lsi_lmd->lmd_recovery_time_hard = 0;
	s2lsi_nocast(sb) = lsi;
	/* we take 1 extra ref for our setup */
	atomic_set(&lsi->lsi_mounts, 1);

	/* Default umount style */
	lsi->lsi_flags = LSI_UMOUNT_FAILOVER;

	return lsi;
}

static int lustre_free_lsi(struct super_block *sb)
{
	struct lustre_sb_info *lsi = s2lsi(sb);

	LASSERT(lsi != NULL);
	CDEBUG(D_MOUNT, "Freeing lsi %p\n", lsi);

	/* someone didn't call server_put_mount. */
	LASSERT(atomic_read(&lsi->lsi_mounts) == 0);

	if (lsi->lsi_lmd != NULL) {
		if (lsi->lsi_lmd->lmd_dev != NULL)
			OBD_FREE(lsi->lsi_lmd->lmd_dev,
				 strlen(lsi->lsi_lmd->lmd_dev) + 1);
		if (lsi->lsi_lmd->lmd_profile != NULL)
			OBD_FREE(lsi->lsi_lmd->lmd_profile,
				 strlen(lsi->lsi_lmd->lmd_profile) + 1);
		if (lsi->lsi_lmd->lmd_mgssec != NULL)
			OBD_FREE(lsi->lsi_lmd->lmd_mgssec,
				 strlen(lsi->lsi_lmd->lmd_mgssec) + 1);
		if (lsi->lsi_lmd->lmd_opts != NULL)
			OBD_FREE(lsi->lsi_lmd->lmd_opts,
				 strlen(lsi->lsi_lmd->lmd_opts) + 1);
		if (lsi->lsi_lmd->lmd_exclude_count)
			OBD_FREE(lsi->lsi_lmd->lmd_exclude,
				 sizeof(lsi->lsi_lmd->lmd_exclude[0]) *
				 lsi->lsi_lmd->lmd_exclude_count);
		if (lsi->lsi_lmd->lmd_mgs != NULL)
			OBD_FREE(lsi->lsi_lmd->lmd_mgs,
				 strlen(lsi->lsi_lmd->lmd_mgs) + 1);
		if (lsi->lsi_lmd->lmd_osd_type != NULL)
			OBD_FREE(lsi->lsi_lmd->lmd_osd_type,
				 strlen(lsi->lsi_lmd->lmd_osd_type) + 1);
		if (lsi->lsi_lmd->lmd_params != NULL)
			OBD_FREE(lsi->lsi_lmd->lmd_params, 4096);

		OBD_FREE(lsi->lsi_lmd, sizeof(*lsi->lsi_lmd));
	}

	LASSERT(lsi->lsi_llsbi == NULL);
	OBD_FREE(lsi, sizeof(*lsi));
	s2lsi_nocast(sb) = NULL;

	return 0;
}

/* The lsi has one reference for every server that is using the disk -
   e.g. MDT, MGS, and potentially MGC */
int lustre_put_lsi(struct super_block *sb)
{
	struct lustre_sb_info *lsi = s2lsi(sb);

	LASSERT(lsi != NULL);

	CDEBUG(D_MOUNT, "put %p %d\n", sb, atomic_read(&lsi->lsi_mounts));
	if (atomic_dec_and_test(&lsi->lsi_mounts)) {
		if (IS_SERVER(lsi) && lsi->lsi_osd_exp) {
			lu_device_put(&lsi->lsi_dt_dev->dd_lu_dev);
			lsi->lsi_osd_exp->exp_obd->obd_lvfs_ctxt.dt = NULL;
			lsi->lsi_dt_dev = NULL;
			obd_disconnect(lsi->lsi_osd_exp);
			/* wait till OSD is gone */
			obd_zombie_barrier();
		}
		lustre_free_lsi(sb);
		return 1;
	}
	return 0;
}

/*** SERVER NAME ***
 * <FSNAME><SEPERATOR><TYPE><INDEX>
 * FSNAME is between 1 and 8 characters (inclusive).
 *	Excluded characters are '/' and ':'
 * SEPERATOR is either ':' or '-'
 * TYPE: "OST", "MDT", etc.
 * INDEX: Hex representation of the index
 */

/** Get the fsname ("lustre") from the server name ("lustre-OST003F").
 * @param [in] svname server name including type and index
 * @param [out] fsname Buffer to copy filesystem name prefix into.
 *  Must have at least 'strlen(fsname) + 1' chars.
 * @param [out] endptr if endptr isn't NULL it is set to end of fsname
 * rc < 0  on error
 */
int server_name2fsname(const char *svname, char *fsname, const char **endptr)
{
	const char *dash;

	dash = svname + strnlen(svname, 8); /* max fsname length is 8 */
	for (; dash > svname && *dash != '-' && *dash != ':'; dash--)
		;
	if (dash == svname)
		return -EINVAL;

	if (fsname != NULL) {
		strncpy(fsname, svname, dash - svname);
		fsname[dash - svname] = '\0';
	}

	if (endptr != NULL)
		*endptr = dash;

	return 0;
}
EXPORT_SYMBOL(server_name2fsname);

/**
 * Get service name (svname) from string
 * rc < 0 on error
 * if endptr isn't NULL it is set to end of fsname *
 */
int server_name2svname(const char *label, char *svname, const char **endptr,
		       size_t svsize)
{
	int rc;
	const char *dash;

	/* We use server_name2fsname() just for parsing */
	rc = server_name2fsname(label, NULL, &dash);
	if (rc != 0)
		return rc;

	if (endptr != NULL)
		*endptr = dash;

	if (strlcpy(svname, dash + 1, svsize) >= svsize)
		return -E2BIG;

	return 0;
}
EXPORT_SYMBOL(server_name2svname);


/* Get the index from the obd name.
   rc = server type, or
   rc < 0  on error
   if endptr isn't NULL it is set to end of name */
int server_name2index(const char *svname, __u32 *idx, const char **endptr)
{
	unsigned long index;
	int rc;
	const char *dash;

	/* We use server_name2fsname() just for parsing */
	rc = server_name2fsname(svname, NULL, &dash);
	if (rc != 0)
		return rc;

	dash++;

	if (strncmp(dash, "MDT", 3) == 0)
		rc = LDD_F_SV_TYPE_MDT;
	else if (strncmp(dash, "OST", 3) == 0)
		rc = LDD_F_SV_TYPE_OST;
	else
		return -EINVAL;

	dash += 3;

	if (strncmp(dash, "all", 3) == 0) {
		if (endptr != NULL)
			*endptr = dash + 3;
		return rc | LDD_F_SV_ALL;
	}

	index = simple_strtoul(dash, (char **)endptr, 16);
	if (idx != NULL)
		*idx = index;

	/* Account for -mdc after index that is possible when specifying mdt */
	if (endptr != NULL && strncmp(LUSTRE_MDC_NAME, *endptr + 1,
				      sizeof(LUSTRE_MDC_NAME)-1) == 0)
		*endptr += sizeof(LUSTRE_MDC_NAME);

	return rc;
}
EXPORT_SYMBOL(server_name2index);

/*************** mount common between server and client ***************/

/* Common umount */
int lustre_common_put_super(struct super_block *sb)
{
	int rc;

	CDEBUG(D_MOUNT, "dropping sb %p\n", sb);

	/* Drop a ref to the MGC */
	rc = lustre_stop_mgc(sb);
	if (rc && (rc != -ENOENT)) {
		if (rc != -EBUSY) {
			CERROR("Can't stop MGC: %d\n", rc);
			return rc;
		}
		/* BUSY just means that there's some other obd that
		   needs the mgc.  Let him clean it up. */
		CDEBUG(D_MOUNT, "MGC still in use\n");
	}
	/* Drop a ref to the mounted disk */
	lustre_put_lsi(sb);
	lu_types_stop();
	return rc;
}
EXPORT_SYMBOL(lustre_common_put_super);

static void lmd_print(struct lustre_mount_data *lmd)
{
	int i;

	PRINT_CMD(D_MOUNT, "  mount data:\n");
	if (lmd_is_client(lmd))
		PRINT_CMD(D_MOUNT, "profile: %s\n", lmd->lmd_profile);
	PRINT_CMD(D_MOUNT, "device:  %s\n", lmd->lmd_dev);
	PRINT_CMD(D_MOUNT, "flags:   %x\n", lmd->lmd_flags);

	if (lmd->lmd_opts)
		PRINT_CMD(D_MOUNT, "options: %s\n", lmd->lmd_opts);

	if (lmd->lmd_recovery_time_soft)
		PRINT_CMD(D_MOUNT, "recovery time soft: %d\n",
			  lmd->lmd_recovery_time_soft);

	if (lmd->lmd_recovery_time_hard)
		PRINT_CMD(D_MOUNT, "recovery time hard: %d\n",
			  lmd->lmd_recovery_time_hard);

	for (i = 0; i < lmd->lmd_exclude_count; i++) {
		PRINT_CMD(D_MOUNT, "exclude %d:  OST%04x\n", i,
			  lmd->lmd_exclude[i]);
	}
}

/* Is this server on the exclusion list */
int lustre_check_exclusion(struct super_block *sb, char *svname)
{
	struct lustre_sb_info *lsi = s2lsi(sb);
	struct lustre_mount_data *lmd = lsi->lsi_lmd;
	__u32 index;
	int i, rc;

	rc = server_name2index(svname, &index, NULL);
	if (rc != LDD_F_SV_TYPE_OST)
		/* Only exclude OSTs */
		return 0;

	CDEBUG(D_MOUNT, "Check exclusion %s (%d) in %d of %s\n", svname,
	       index, lmd->lmd_exclude_count, lmd->lmd_dev);

	for (i = 0; i < lmd->lmd_exclude_count; i++) {
		if (index == lmd->lmd_exclude[i]) {
			CWARN("Excluding %s (on exclusion list)\n", svname);
			return 1;
		}
	}
	return 0;
}

/* mount -v  -o exclude=lustre-OST0001:lustre-OST0002 -t lustre ... */
static int lmd_make_exclusion(struct lustre_mount_data *lmd, const char *ptr)
{
	const char *s1 = ptr, *s2;
	__u32 index, *exclude_list;
	int rc = 0, devmax;

	/* The shortest an ost name can be is 8 chars: -OST0000.
	   We don't actually know the fsname at this time, so in fact
	   a user could specify any fsname. */
	devmax = strlen(ptr) / 8 + 1;

	/* temp storage until we figure out how many we have */
	OBD_ALLOC(exclude_list, sizeof(index) * devmax);
	if (!exclude_list)
		return -ENOMEM;

	/* we enter this fn pointing at the '=' */
	while (*s1 && *s1 != ' ' && *s1 != ',') {
		s1++;
		rc = server_name2index(s1, &index, &s2);
		if (rc < 0) {
			CERROR("Can't parse server name '%s': rc = %d\n",
			       s1, rc);
			break;
		}
		if (rc == LDD_F_SV_TYPE_OST)
			exclude_list[lmd->lmd_exclude_count++] = index;
		else
			CDEBUG(D_MOUNT, "ignoring exclude %.*s: type = %#x\n",
			       (uint)(s2-s1), s1, rc);
		s1 = s2;
		/* now we are pointing at ':' (next exclude)
		   or ',' (end of excludes) */
		if (lmd->lmd_exclude_count >= devmax)
			break;
	}
	if (rc >= 0) /* non-err */
		rc = 0;

	if (lmd->lmd_exclude_count) {
		/* permanent, freed in lustre_free_lsi */
		OBD_ALLOC(lmd->lmd_exclude, sizeof(index) *
			  lmd->lmd_exclude_count);
		if (lmd->lmd_exclude) {
			memcpy(lmd->lmd_exclude, exclude_list,
			       sizeof(index) * lmd->lmd_exclude_count);
		} else {
			rc = -ENOMEM;
			lmd->lmd_exclude_count = 0;
		}
	}
	OBD_FREE(exclude_list, sizeof(index) * devmax);
	return rc;
}

static int lmd_parse_mgssec(struct lustre_mount_data *lmd, char *ptr)
{
	char   *tail;
	int     length;

	if (lmd->lmd_mgssec != NULL) {
		OBD_FREE(lmd->lmd_mgssec, strlen(lmd->lmd_mgssec) + 1);
		lmd->lmd_mgssec = NULL;
	}

	tail = strchr(ptr, ',');
	if (tail == NULL)
		length = strlen(ptr);
	else
		length = tail - ptr;

	OBD_ALLOC(lmd->lmd_mgssec, length + 1);
	if (lmd->lmd_mgssec == NULL)
		return -ENOMEM;

	memcpy(lmd->lmd_mgssec, ptr, length);
	lmd->lmd_mgssec[length] = '\0';
	return 0;
}

static int lmd_parse_string(char **handle, char *ptr)
{
	char   *tail;
	int     length;

	if ((handle == NULL) || (ptr == NULL))
		return -EINVAL;

	if (*handle != NULL) {
		OBD_FREE(*handle, strlen(*handle) + 1);
		*handle = NULL;
	}

	tail = strchr(ptr, ',');
	if (tail == NULL)
		length = strlen(ptr);
	else
		length = tail - ptr;

	OBD_ALLOC(*handle, length + 1);
	if (*handle == NULL)
		return -ENOMEM;

	memcpy(*handle, ptr, length);
	(*handle)[length] = '\0';

	return 0;
}

/* Collect multiple values for mgsnid specifiers */
static int lmd_parse_mgs(struct lustre_mount_data *lmd, char **ptr)
{
	lnet_nid_t nid;
	char *tail = *ptr;
	char *mgsnid;
	int   length;
	int   oldlen = 0;

	/* Find end of nidlist */
	while (class_parse_nid_quiet(tail, &nid, &tail) == 0) {}
	length = tail - *ptr;
	if (length == 0) {
		LCONSOLE_ERROR_MSG(0x159, "Can't parse NID '%s'\n", *ptr);
		return -EINVAL;
	}

	if (lmd->lmd_mgs != NULL)
		oldlen = strlen(lmd->lmd_mgs) + 1;

	OBD_ALLOC(mgsnid, oldlen + length + 1);
	if (mgsnid == NULL)
		return -ENOMEM;

	if (lmd->lmd_mgs != NULL) {
		/* Multiple mgsnid= are taken to mean failover locations */
		memcpy(mgsnid, lmd->lmd_mgs, oldlen);
		mgsnid[oldlen - 1] = ':';
		OBD_FREE(lmd->lmd_mgs, oldlen);
	}
	memcpy(mgsnid + oldlen, *ptr, length);
	mgsnid[oldlen + length] = '\0';
	lmd->lmd_mgs = mgsnid;
	*ptr = tail;

	return 0;
}

/** Parse mount line options
 * e.g. mount -v -t lustre -o abort_recov uml1:uml2:/lustre-client /mnt/lustre
 * dev is passed as device=uml1:/lustre by mount.lustre
 */
static int lmd_parse(char *options, struct lustre_mount_data *lmd)
{
	char *s1, *s2, *devname = NULL;
	struct lustre_mount_data *raw = (struct lustre_mount_data *)options;
	int rc = 0;

	LASSERT(lmd);
	if (!options) {
		LCONSOLE_ERROR_MSG(0x162, "Missing mount data: check that /sbin/mount.lustre is installed.\n");
		return -EINVAL;
	}

	/* Options should be a string - try to detect old lmd data */
	if ((raw->lmd_magic & 0xffffff00) == (LMD_MAGIC & 0xffffff00)) {
		LCONSOLE_ERROR_MSG(0x163, "You're using an old version of /sbin/mount.lustre.  Please install version %s\n",
				   LUSTRE_VERSION_STRING);
		return -EINVAL;
	}
	lmd->lmd_magic = LMD_MAGIC;

	OBD_ALLOC(lmd->lmd_params, 4096);
	if (lmd->lmd_params == NULL)
		return -ENOMEM;
	lmd->lmd_params[0] = '\0';

	/* Set default flags here */

	s1 = options;
	while (*s1) {
		int clear = 0;
		int time_min = OBD_RECOVERY_TIME_MIN;

		/* Skip whitespace and extra commas */
		while (*s1 == ' ' || *s1 == ',')
			s1++;

		/* Client options are parsed in ll_options: eg. flock,
		   user_xattr, acl */

		/* Parse non-ldiskfs options here. Rather than modifying
		   ldiskfs, we just zero these out here */
		if (strncmp(s1, "abort_recov", 11) == 0) {
			lmd->lmd_flags |= LMD_FLG_ABORT_RECOV;
			clear++;
		} else if (strncmp(s1, "recovery_time_soft=", 19) == 0) {
			lmd->lmd_recovery_time_soft = max_t(int,
				simple_strtoul(s1 + 19, NULL, 10), time_min);
			clear++;
		} else if (strncmp(s1, "recovery_time_hard=", 19) == 0) {
			lmd->lmd_recovery_time_hard = max_t(int,
				simple_strtoul(s1 + 19, NULL, 10), time_min);
			clear++;
		} else if (strncmp(s1, "noir", 4) == 0) {
			lmd->lmd_flags |= LMD_FLG_NOIR; /* test purpose only. */
			clear++;
		} else if (strncmp(s1, "nosvc", 5) == 0) {
			lmd->lmd_flags |= LMD_FLG_NOSVC;
			clear++;
		} else if (strncmp(s1, "nomgs", 5) == 0) {
			lmd->lmd_flags |= LMD_FLG_NOMGS;
			clear++;
		} else if (strncmp(s1, "noscrub", 7) == 0) {
			lmd->lmd_flags |= LMD_FLG_NOSCRUB;
			clear++;
		} else if (strncmp(s1, PARAM_MGSNODE,
				   sizeof(PARAM_MGSNODE) - 1) == 0) {
			s2 = s1 + sizeof(PARAM_MGSNODE) - 1;
			/* Assume the next mount opt is the first
			   invalid nid we get to. */
			rc = lmd_parse_mgs(lmd, &s2);
			if (rc)
				goto invalid;
			clear++;
		} else if (strncmp(s1, "writeconf", 9) == 0) {
			lmd->lmd_flags |= LMD_FLG_WRITECONF;
			clear++;
		} else if (strncmp(s1, "update", 6) == 0) {
			lmd->lmd_flags |= LMD_FLG_UPDATE;
			clear++;
		} else if (strncmp(s1, "virgin", 6) == 0) {
			lmd->lmd_flags |= LMD_FLG_VIRGIN;
			clear++;
		} else if (strncmp(s1, "noprimnode", 10) == 0) {
			lmd->lmd_flags |= LMD_FLG_NO_PRIMNODE;
			clear++;
		} else if (strncmp(s1, "mgssec=", 7) == 0) {
			rc = lmd_parse_mgssec(lmd, s1 + 7);
			if (rc)
				goto invalid;
			clear++;
		/* ost exclusion list */
		} else if (strncmp(s1, "exclude=", 8) == 0) {
			rc = lmd_make_exclusion(lmd, s1 + 7);
			if (rc)
				goto invalid;
			clear++;
		} else if (strncmp(s1, "mgs", 3) == 0) {
			/* We are an MGS */
			lmd->lmd_flags |= LMD_FLG_MGS;
			clear++;
		} else if (strncmp(s1, "svname=", 7) == 0) {
			rc = lmd_parse_string(&lmd->lmd_profile, s1 + 7);
			if (rc)
				goto invalid;
			clear++;
		} else if (strncmp(s1, "param=", 6) == 0) {
			int length;
			char *tail = strchr(s1 + 6, ',');
			if (tail == NULL)
				length = strlen(s1);
			else
				length = tail - s1;
			length -= 6;
			strncat(lmd->lmd_params, s1 + 6, length);
			strcat(lmd->lmd_params, " ");
			clear++;
		} else if (strncmp(s1, "osd=", 4) == 0) {
			rc = lmd_parse_string(&lmd->lmd_osd_type, s1 + 4);
			if (rc)
				goto invalid;
			clear++;
		}
		/* Linux 2.4 doesn't pass the device, so we stuck it at the
		   end of the options. */
		else if (strncmp(s1, "device=", 7) == 0) {
			devname = s1 + 7;
			/* terminate options right before device.  device
			   must be the last one. */
			*s1 = '\0';
			break;
		}

		/* Find next opt */
		s2 = strchr(s1, ',');
		if (s2 == NULL) {
			if (clear)
				*s1 = '\0';
			break;
		}
		s2++;
		if (clear)
			memmove(s1, s2, strlen(s2) + 1);
		else
			s1 = s2;
	}

	if (!devname) {
		LCONSOLE_ERROR_MSG(0x164, "Can't find the device name (need mount option 'device=...')\n");
		goto invalid;
	}

	s1 = strstr(devname, ":/");
	if (s1) {
		++s1;
		lmd->lmd_flags |= LMD_FLG_CLIENT;
		/* Remove leading /s from fsname */
		while (*++s1 == '/') ;
		/* Freed in lustre_free_lsi */
		OBD_ALLOC(lmd->lmd_profile, strlen(s1) + 8);
		if (!lmd->lmd_profile)
			return -ENOMEM;
		sprintf(lmd->lmd_profile, "%s-client", s1);
	}

	/* Freed in lustre_free_lsi */
	OBD_ALLOC(lmd->lmd_dev, strlen(devname) + 1);
	if (!lmd->lmd_dev)
		return -ENOMEM;
	strcpy(lmd->lmd_dev, devname);

	/* Save mount options */
	s1 = options + strlen(options) - 1;
	while (s1 >= options && (*s1 == ',' || *s1 == ' '))
		*s1-- = 0;
	if (*options != 0) {
		/* Freed in lustre_free_lsi */
		OBD_ALLOC(lmd->lmd_opts, strlen(options) + 1);
		if (!lmd->lmd_opts)
			return -ENOMEM;
		strcpy(lmd->lmd_opts, options);
	}

	lmd_print(lmd);
	lmd->lmd_magic = LMD_MAGIC;

	return rc;

invalid:
	CERROR("Bad mount options %s\n", options);
	return -EINVAL;
}

struct lustre_mount_data2 {
	void *lmd2_data;
	struct vfsmount *lmd2_mnt;
};

/** This is the entry point for the mount call into Lustre.
 * This is called when a server or client is mounted,
 * and this is where we start setting things up.
 * @param data Mount options (e.g. -o flock,abort_recov)
 */
int lustre_fill_super(struct super_block *sb, void *data, int silent)
{
	struct lustre_mount_data *lmd;
	struct lustre_mount_data2 *lmd2 = data;
	struct lustre_sb_info *lsi;
	int rc;

	CDEBUG(D_MOUNT|D_VFSTRACE, "VFS Op: sb %p\n", sb);

	lsi = lustre_init_lsi(sb);
	if (!lsi)
		return -ENOMEM;
	lmd = lsi->lsi_lmd;

	/*
	 * Disable lockdep during mount, because mount locking patterns are
	 * `special'.
	 */
	lockdep_off();

	/*
	 * LU-639: the obd cleanup of last mount may not finish yet, wait here.
	 */
	obd_zombie_barrier();

	/* Figure out the lmd from the mount options */
	if (lmd_parse((char *)(lmd2->lmd2_data), lmd)) {
		lustre_put_lsi(sb);
		rc = -EINVAL;
		goto out;
	}

	if (lmd_is_client(lmd)) {
		CDEBUG(D_MOUNT, "Mounting client %s\n", lmd->lmd_profile);
		if (client_fill_super == NULL)
			request_module("lustre");
		if (client_fill_super == NULL) {
			LCONSOLE_ERROR_MSG(0x165, "Nothing registered for client mount! Is the 'lustre' module loaded?\n");
			lustre_put_lsi(sb);
			rc = -ENODEV;
		} else {
			rc = lustre_start_mgc(sb);
			if (rc) {
				lustre_put_lsi(sb);
				goto out;
			}
			/* Connect and start */
			/* (should always be ll_fill_super) */
			rc = (*client_fill_super)(sb, lmd2->lmd2_mnt);
			/* c_f_s will call lustre_common_put_super on failure */
		}
	} else {
		CERROR("This is client-side-only module, cannot handle server mount.\n");
		rc = -EINVAL;
	}

	/* If error happens in fill_super() call, @lsi will be killed there.
	 * This is why we do not put it here. */
	goto out;
out:
	if (rc) {
		CERROR("Unable to mount %s (%d)\n",
		       s2lsi(sb) ? lmd->lmd_dev : "", rc);
	} else {
		CDEBUG(D_SUPER, "Mount %s complete\n",
		       lmd->lmd_dev);
	}
	lockdep_on();
	return rc;
}


/* We can't call ll_fill_super by name because it lives in a module that
   must be loaded after this one. */
void lustre_register_client_fill_super(int (*cfs)(struct super_block *sb,
						  struct vfsmount *mnt))
{
	client_fill_super = cfs;
}
EXPORT_SYMBOL(lustre_register_client_fill_super);

void lustre_register_kill_super_cb(void (*cfs)(struct super_block *sb))
{
	kill_super_cb = cfs;
}
EXPORT_SYMBOL(lustre_register_kill_super_cb);

/***************** FS registration ******************/
struct dentry *lustre_mount(struct file_system_type *fs_type, int flags,
				const char *devname, void *data)
{
	struct lustre_mount_data2 lmd2 = {
		.lmd2_data = data,
		.lmd2_mnt = NULL
	};

	return mount_nodev(fs_type, flags, &lmd2, lustre_fill_super);
}

void lustre_kill_super(struct super_block *sb)
{
	struct lustre_sb_info *lsi = s2lsi(sb);

	if (kill_super_cb && lsi && !IS_SERVER(lsi))
		(*kill_super_cb)(sb);

	kill_anon_super(sb);
}

/** Register the "lustre" fs type
 */
struct file_system_type lustre_fs_type = {
	.owner	= THIS_MODULE,
	.name	 = "lustre",
	.mount	= lustre_mount,
	.kill_sb      = lustre_kill_super,
	.fs_flags     = FS_BINARY_MOUNTDATA | FS_REQUIRES_DEV |
			FS_HAS_FIEMAP | FS_RENAME_DOES_D_MOVE,
};
MODULE_ALIAS_FS("lustre");

int lustre_register_fs(void)
{
	return register_filesystem(&lustre_fs_type);
}

int lustre_unregister_fs(void)
{
	return unregister_filesystem(&lustre_fs_type);
}
