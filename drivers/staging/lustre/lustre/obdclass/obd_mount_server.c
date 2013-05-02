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
 * lustre/obdclass/obd_mount_server.c
 *
 * Server mount routines
 *
 * Author: Nathan Rutman <nathan@clusterfs.com>
 */


#define DEBUG_SUBSYSTEM S_CLASS
#define D_MOUNT (D_SUPER | D_CONFIG /* | D_WARNING */)
#define PRINT_CMD CDEBUG
#define PRINT_MASK (D_SUPER | D_CONFIG)

#include <obd.h>
#include <lvfs.h>
#include <lustre_fsfilt.h>
#include <obd_class.h>
#include <lustre/lustre_user.h>
#include <linux/version.h>
#include <lustre_log.h>
#include <lustre_disk.h>
#include <lustre_param.h>

/*********** mount lookup *********/

DEFINE_MUTEX(lustre_mount_info_lock);
static LIST_HEAD(server_mount_info_list);

static struct lustre_mount_info *server_find_mount(const char *name)
{
	struct list_head *tmp;
	struct lustre_mount_info *lmi;
	ENTRY;

	list_for_each(tmp, &server_mount_info_list) {
		lmi = list_entry(tmp, struct lustre_mount_info,
				     lmi_list_chain);
		if (strcmp(name, lmi->lmi_name) == 0)
			RETURN(lmi);
	}
	RETURN(NULL);
}

/* we must register an obd for a mount before we call the setup routine.
 *_setup will call lustre_get_mount to get the mnt struct
 by obd_name, since we can't pass the pointer to setup. */
static int server_register_mount(const char *name, struct super_block *sb,
				 struct vfsmount *mnt)
{
	struct lustre_mount_info *lmi;
	char *name_cp;
	ENTRY;

	LASSERT(sb);

	OBD_ALLOC(lmi, sizeof(*lmi));
	if (!lmi)
		RETURN(-ENOMEM);
	OBD_ALLOC(name_cp, strlen(name) + 1);
	if (!name_cp) {
		OBD_FREE(lmi, sizeof(*lmi));
		RETURN(-ENOMEM);
	}
	strcpy(name_cp, name);

	mutex_lock(&lustre_mount_info_lock);

	if (server_find_mount(name)) {
		mutex_unlock(&lustre_mount_info_lock);
		OBD_FREE(lmi, sizeof(*lmi));
		OBD_FREE(name_cp, strlen(name) + 1);
		CERROR("Already registered %s\n", name);
		RETURN(-EEXIST);
	}
	lmi->lmi_name = name_cp;
	lmi->lmi_sb = sb;
	lmi->lmi_mnt = mnt;
	list_add(&lmi->lmi_list_chain, &server_mount_info_list);

	mutex_unlock(&lustre_mount_info_lock);

	CDEBUG(D_MOUNT, "reg_mnt %p from %s\n", lmi->lmi_mnt, name);

	RETURN(0);
}

/* when an obd no longer needs a mount */
static int server_deregister_mount(const char *name)
{
	struct lustre_mount_info *lmi;
	ENTRY;

	mutex_lock(&lustre_mount_info_lock);
	lmi = server_find_mount(name);
	if (!lmi) {
		mutex_unlock(&lustre_mount_info_lock);
		CERROR("%s not registered\n", name);
		RETURN(-ENOENT);
	}

	CDEBUG(D_MOUNT, "dereg_mnt %p from %s\n", lmi->lmi_mnt, name);

	OBD_FREE(lmi->lmi_name, strlen(lmi->lmi_name) + 1);
	list_del(&lmi->lmi_list_chain);
	OBD_FREE(lmi, sizeof(*lmi));
	mutex_unlock(&lustre_mount_info_lock);

	RETURN(0);
}

/* obd's look up a registered mount using their obdname. This is just
   for initial obd setup to find the mount struct.  It should not be
   called every time you want to mntget. */
struct lustre_mount_info *server_get_mount(const char *name)
{
	struct lustre_mount_info *lmi;
	struct lustre_sb_info *lsi;
	ENTRY;

	mutex_lock(&lustre_mount_info_lock);
	lmi = server_find_mount(name);
	mutex_unlock(&lustre_mount_info_lock);
	if (!lmi) {
		CERROR("Can't find mount for %s\n", name);
		RETURN(NULL);
	}
	lsi = s2lsi(lmi->lmi_sb);

	atomic_inc(&lsi->lsi_mounts);

	CDEBUG(D_MOUNT, "get_mnt %p from %s, refs=%d\n", lmi->lmi_mnt,
	       name, atomic_read(&lsi->lsi_mounts));

	RETURN(lmi);
}
EXPORT_SYMBOL(server_get_mount);

/*
 * Used by mdt to get mount_info from obdname.
 * There are no blocking when using the mount_info.
 * Do not use server_get_mount for this purpose.
 */
struct lustre_mount_info *server_get_mount_2(const char *name)
{
	struct lustre_mount_info *lmi;
	ENTRY;

	mutex_lock(&lustre_mount_info_lock);
	lmi = server_find_mount(name);
	mutex_unlock(&lustre_mount_info_lock);
	if (!lmi)
		CERROR("Can't find mount for %s\n", name);

	RETURN(lmi);
}
EXPORT_SYMBOL(server_get_mount_2);

/* to be called from obd_cleanup methods */
int server_put_mount(const char *name, struct vfsmount *mnt)
{
	struct lustre_mount_info *lmi;
	struct lustre_sb_info *lsi;
	ENTRY;

	mutex_lock(&lustre_mount_info_lock);
	lmi = server_find_mount(name);
	mutex_unlock(&lustre_mount_info_lock);
	if (!lmi) {
		CERROR("Can't find mount for %s\n", name);
		RETURN(-ENOENT);
	}
	lsi = s2lsi(lmi->lmi_sb);

	CDEBUG(D_MOUNT, "put_mnt %p from %s, refs=%d\n",
	       lmi->lmi_mnt, name, atomic_read(&lsi->lsi_mounts));

	if (lustre_put_lsi(lmi->lmi_sb))
		CDEBUG(D_MOUNT, "Last put of mnt %p from %s\n",
		       lmi->lmi_mnt, name);

	/* this obd should never need the mount again */
	server_deregister_mount(name);

	RETURN(0);
}
EXPORT_SYMBOL(server_put_mount);

/* Corresponding to server_get_mount_2 */
int server_put_mount_2(const char *name, struct vfsmount *mnt)
{
	ENTRY;
	RETURN(0);
}
EXPORT_SYMBOL(server_put_mount_2);

/* Set up a MGS to serve startup logs */
static int server_start_mgs(struct super_block *sb)
{
	struct lustre_sb_info    *lsi = s2lsi(sb);
	struct vfsmount	  *mnt = lsi->lsi_srv_mnt;
	struct lustre_mount_info *lmi;
	int    rc = 0;
	ENTRY;

	/* It is impossible to have more than 1 MGS per node, since
	   MGC wouldn't know which to connect to */
	lmi = server_find_mount(LUSTRE_MGS_OBDNAME);
	if (lmi) {
		lsi = s2lsi(lmi->lmi_sb);
		LCONSOLE_ERROR_MSG(0x15d, "The MGS service was already started"
				   " from server\n");
		RETURN(-EALREADY);
	}

	CDEBUG(D_CONFIG, "Start MGS service %s\n", LUSTRE_MGS_OBDNAME);

	rc = server_register_mount(LUSTRE_MGS_OBDNAME, sb, mnt);

	if (!rc) {
		rc = lustre_start_simple(LUSTRE_MGS_OBDNAME, LUSTRE_MGS_NAME,
					 LUSTRE_MGS_OBDNAME, 0, 0,
					 lsi->lsi_osd_obdname, 0);
		/* Do NOT call server_deregister_mount() here. This leads to
		 * inability cleanup cleanly and free lsi and other stuff when
		 * mgs calls server_put_mount() in error handling case. -umka */
	}

	if (rc)
		LCONSOLE_ERROR_MSG(0x15e, "Failed to start MGS '%s' (%d). "
				   "Is the 'mgs' module loaded?\n",
				   LUSTRE_MGS_OBDNAME, rc);
	RETURN(rc);
}

static int server_stop_mgs(struct super_block *sb)
{
	struct obd_device *obd;
	int rc;
	ENTRY;

	CDEBUG(D_MOUNT, "Stop MGS service %s\n", LUSTRE_MGS_OBDNAME);

	/* There better be only one MGS */
	obd = class_name2obd(LUSTRE_MGS_OBDNAME);
	if (!obd) {
		CDEBUG(D_CONFIG, "mgs %s not running\n", LUSTRE_MGS_OBDNAME);
		RETURN(-EALREADY);
	}

	/* The MGS should always stop when we say so */
	obd->obd_force = 1;
	rc = class_manual_cleanup(obd);
	RETURN(rc);
}

/* Since there's only one mgc per node, we have to change it's fs to get
   access to the right disk. */
static int server_mgc_set_fs(struct obd_device *mgc, struct super_block *sb)
{
	struct lustre_sb_info *lsi = s2lsi(sb);
	int rc;
	ENTRY;

	CDEBUG(D_MOUNT, "Set mgc disk for %s\n", lsi->lsi_lmd->lmd_dev);

	/* cl_mgc_sem in mgc insures we sleep if the mgc_fs is busy */
	rc = obd_set_info_async(NULL, mgc->obd_self_export,
				sizeof(KEY_SET_FS), KEY_SET_FS,
				sizeof(*sb), sb, NULL);
	if (rc != 0)
		CERROR("can't set_fs %d\n", rc);

	RETURN(rc);
}

static int server_mgc_clear_fs(struct obd_device *mgc)
{
	int rc;
	ENTRY;

	CDEBUG(D_MOUNT, "Unassign mgc disk\n");

	rc = obd_set_info_async(NULL, mgc->obd_self_export,
				sizeof(KEY_CLEAR_FS), KEY_CLEAR_FS,
				0, NULL, NULL);
	RETURN(rc);
}

static int is_mdc_device(const char *devname)
{
	char *ptr;

	ptr = strrchr(devname, '-');
	if (ptr != NULL && strcmp(ptr, "-mdc") == 0)
		return 1;

	return 0;
}

static inline int tgt_is_mdt0(const char *tgtname)
{
	__u32 idx;
	int   type;

	type = server_name2index(tgtname, &idx, NULL);
	if (type != LDD_F_SV_TYPE_MDT)
		return 0;

	return idx == 0;
}

static inline int is_mdc_for_mdt0(const char *devname)
{
	char   *ptr;

	if (!is_mdc_device(devname))
		return 0;

	ptr = strrchr(devname, '-');
	if (ptr == NULL)
		return 0;

	*ptr = 0;
	if (tgt_is_mdt0(devname)) {
		*ptr = '-';
		return 1;
	}
	*ptr = '-';
	return 0;
}

/**
 * Convert OST/MDT name(fsname-OSTxxxx) to a lwp name
 * (fsname-MDT0000-lwp-OSTxxxx)
 **/
int tgt_name2lwpname(const char *svname, char *lwpname)
{
	char		*fsname;
	const char	*tgt;
	int		rc;
	ENTRY;

	OBD_ALLOC(fsname, MTI_NAME_MAXLEN);
	if (fsname == NULL)
		RETURN(-ENOMEM);

	rc = server_name2fsname(svname, fsname, &tgt);
	if (rc != 0) {
		CERROR("%s: failed to get fsname from svname. %d\n",
		       svname, rc);
		GOTO(cleanup, rc);
	}

	if (*tgt != '-' && *tgt != ':') {
		CERROR("%s: invalid svname name!\n", svname);
		GOTO(cleanup, rc = -EINVAL);
	}

	tgt++;
	if (strncmp(tgt, "OST", 3) != 0 && strncmp(tgt, "MDT", 3) != 0) {
		CERROR("%s is not an OST or MDT target!\n", svname);
		GOTO(cleanup, rc = -EINVAL);
	}
	sprintf(lwpname, "%s-MDT0000-%s-%s", fsname, LUSTRE_LWP_NAME, tgt);
cleanup:
	if (fsname != NULL)
		OBD_FREE(fsname, MTI_NAME_MAXLEN);
	RETURN(rc);
}
EXPORT_SYMBOL(tgt_name2lwpname);

static LIST_HEAD(lwp_register_list);
DEFINE_MUTEX(lwp_register_list_lock);

int lustre_register_lwp_item(const char *lwpname, struct obd_export **exp,
			     register_lwp_cb cb_func, void *cb_data)
{
	struct obd_device	 *lwp;
	struct lwp_register_item *lri;
	ENTRY;

	LASSERTF(strlen(lwpname) < MTI_NAME_MAXLEN, "lwpname is too long %s\n",
		 lwpname);
	LASSERT(exp != NULL && *exp == NULL);

	OBD_ALLOC_PTR(lri);
	if (lri == NULL)
		RETURN(-ENOMEM);

	mutex_lock(&lwp_register_list_lock);

	lwp = class_name2obd(lwpname);
	if (lwp != NULL && lwp->obd_set_up == 1) {
		struct obd_uuid *uuid;

		OBD_ALLOC_PTR(uuid);
		if (uuid == NULL) {
			mutex_unlock(&lwp_register_list_lock);
			OBD_FREE_PTR(lri);
			RETURN(-ENOMEM);
		}
		memcpy(uuid->uuid, lwpname, strlen(lwpname));
		*exp = cfs_hash_lookup(lwp->obd_uuid_hash, uuid);
		OBD_FREE_PTR(uuid);
	}

	memcpy(lri->lri_name, lwpname, strlen(lwpname));
	lri->lri_exp = exp;
	lri->lri_cb_func = cb_func;
	lri->lri_cb_data = cb_data;
	INIT_LIST_HEAD(&lri->lri_list);
	list_add(&lri->lri_list, &lwp_register_list);

	if (*exp != NULL && cb_func != NULL)
		cb_func(cb_data);

	mutex_unlock(&lwp_register_list_lock);
	RETURN(0);
}
EXPORT_SYMBOL(lustre_register_lwp_item);

void lustre_deregister_lwp_item(struct obd_export **exp)
{
	struct lwp_register_item *lri, *tmp;

	mutex_lock(&lwp_register_list_lock);
	list_for_each_entry_safe(lri, tmp, &lwp_register_list, lri_list) {
		if (exp == lri->lri_exp) {
			if (*exp)
				class_export_put(*exp);
			list_del(&lri->lri_list);
			OBD_FREE_PTR(lri);
			break;
		}
	}
	mutex_unlock(&lwp_register_list_lock);
}
EXPORT_SYMBOL(lustre_deregister_lwp_item);

static void lustre_notify_lwp_list(struct obd_export *exp)
{
	struct lwp_register_item *lri, *tmp;
	LASSERT(exp != NULL);

	mutex_lock(&lwp_register_list_lock);
	list_for_each_entry_safe(lri, tmp, &lwp_register_list, lri_list) {
		if (strcmp(exp->exp_obd->obd_name, lri->lri_name))
			continue;
		if (*lri->lri_exp != NULL)
			continue;
		*lri->lri_exp = class_export_get(exp);
		if (lri->lri_cb_func != NULL)
			lri->lri_cb_func(lri->lri_cb_data);
	}
	mutex_unlock(&lwp_register_list_lock);
}

static int lustre_lwp_connect(struct obd_device *lwp)
{
	struct lu_env		 env;
	struct lu_context	 session_ctx;
	struct obd_export	*exp;
	struct obd_uuid		*uuid = NULL;
	struct obd_connect_data	*data = NULL;
	int			 rc;
	ENTRY;

	/* log has been fully processed, let clients connect */
	rc = lu_env_init(&env, lwp->obd_lu_dev->ld_type->ldt_ctx_tags);
	if (rc != 0)
		RETURN(rc);

	lu_context_init(&session_ctx, LCT_SESSION);
	session_ctx.lc_thread = NULL;
	lu_context_enter(&session_ctx);
	env.le_ses = &session_ctx;

	OBD_ALLOC_PTR(data);
	if (data == NULL)
		GOTO(out, rc = -ENOMEM);

	data->ocd_connect_flags = OBD_CONNECT_VERSION | OBD_CONNECT_INDEX;
	data->ocd_version = LUSTRE_VERSION_CODE;
	data->ocd_connect_flags |= OBD_CONNECT_MDS_MDS | OBD_CONNECT_FID |
		OBD_CONNECT_AT | OBD_CONNECT_LRU_RESIZE |
		OBD_CONNECT_FULL20 | OBD_CONNECT_LVB_TYPE |
		OBD_CONNECT_LIGHTWEIGHT;
	OBD_ALLOC_PTR(uuid);
	if (uuid == NULL)
		GOTO(out, rc = -ENOMEM);

	if (strlen(lwp->obd_name) > sizeof(uuid->uuid)) {
		CERROR("%s: Too long lwp name %s, max_size is %d\n",
		       lwp->obd_name, lwp->obd_name, (int)sizeof(uuid->uuid));
		GOTO(out, rc = -EINVAL);
	}

	/* Use lwp name as the uuid, so we find the export by lwp name later */
	memcpy(uuid->uuid, lwp->obd_name, strlen(lwp->obd_name));
	rc = obd_connect(&env, &exp, lwp, uuid, data, NULL);
	if (rc != 0)
		CERROR("%s: connect failed: rc = %d\n", lwp->obd_name, rc);
	else
		lustre_notify_lwp_list(exp);

out:
	if (data != NULL)
		OBD_FREE_PTR(data);
	if (uuid != NULL)
		OBD_FREE_PTR(uuid);

	lu_env_fini(&env);
	lu_context_exit(&session_ctx);
	lu_context_fini(&session_ctx);

	RETURN(rc);
}

/**
 * lwp is used by slaves (Non-MDT0 targets) to manage the connection
 * to MDT0.
 **/
static int lustre_lwp_setup(struct lustre_cfg *lcfg, struct lustre_sb_info *lsi)
{
	struct obd_connect_data *data = NULL;
	struct obd_device	*obd;
	char			*lwpname = NULL;
	char			*lwpuuid = NULL;
	int			 rc;
	ENTRY;

	rc = class_add_uuid(lustre_cfg_string(lcfg, 1),
			    lcfg->lcfg_nid);
	if (rc) {
		CERROR("%s: Can't add uuid: rc =%d\n", lsi->lsi_svname, rc);
		GOTO(out, rc);
	}

	OBD_ALLOC(lwpname, MTI_NAME_MAXLEN);
	if (lwpname == NULL)
		GOTO(out, rc = -ENOMEM);

	rc = tgt_name2lwpname(lsi->lsi_svname, lwpname);
	if (rc != 0) {
		CERROR("%s: failed to generate lwp name. %d\n",
		       lsi->lsi_svname, rc);
		GOTO(out, rc);
	}

	OBD_ALLOC(lwpuuid, MTI_NAME_MAXLEN);
	if (lwpuuid == NULL)
		GOTO(out, rc = -ENOMEM);

	sprintf(lwpuuid, "%s_UUID", lwpname);
	rc = lustre_start_simple(lwpname, LUSTRE_LWP_NAME,
				 lwpuuid, lustre_cfg_string(lcfg, 1),
				 0, 0, 0);
	if (rc) {
		CERROR("%s: setup up failed: rc %d\n", lwpname, rc);
		GOTO(out, rc);
	}

	obd = class_name2obd(lwpname);
	LASSERT(obd != NULL);

	rc = lustre_lwp_connect(obd);
	if (rc != 0)
		CERROR("%s: connect failed: rc = %d\n", lwpname, rc);
out:
	if (data != NULL)
		OBD_FREE_PTR(data);
	if (lwpname != NULL)
		OBD_FREE(lwpname, MTI_NAME_MAXLEN);
	if (lwpuuid != NULL)
		OBD_FREE(lwpuuid, MTI_NAME_MAXLEN);

	RETURN(rc);
}

/* the caller is responsible for memory free */
static struct obd_device *lustre_find_lwp(struct lustre_sb_info *lsi,
					  char **lwpname, char **logname)
{
	struct obd_device	*lwp;
	int			 rc = 0;
	ENTRY;

	LASSERT(lwpname != NULL);
	LASSERT(IS_OST(lsi) || IS_MDT(lsi));

	OBD_ALLOC(*lwpname, MTI_NAME_MAXLEN);
	if (*lwpname == NULL)
		RETURN(ERR_PTR(-ENOMEM));

	if (logname != NULL) {
		OBD_ALLOC(*logname, MTI_NAME_MAXLEN);
		if (*logname == NULL)
			GOTO(out, rc = -ENOMEM);
		rc = server_name2fsname(lsi->lsi_svname, *lwpname, NULL);
		if (rc != 0) {
			CERROR("%s: failed to get fsname from svname. %d\n",
			       lsi->lsi_svname, rc);
			GOTO(out, rc = -EINVAL);
		}
		sprintf(*logname, "%s-client", *lwpname);
	}

	rc = tgt_name2lwpname(lsi->lsi_svname, *lwpname);
	if (rc != 0) {
		CERROR("%s: failed to generate lwp name. %d\n",
		       lsi->lsi_svname, rc);
		GOTO(out, rc = -EINVAL);
	}

	lwp = class_name2obd(*lwpname);

out:
	if (rc != 0) {
		if (*lwpname != NULL) {
			OBD_FREE(*lwpname, MTI_NAME_MAXLEN);
			*lwpname = NULL;
		}
		if (logname != NULL && *logname != NULL) {
			OBD_FREE(*logname, MTI_NAME_MAXLEN);
			*logname = NULL;
		}
		lwp = ERR_PTR(rc);
	}

	RETURN(lwp != NULL ? lwp : ERR_PTR(-ENOENT));
}

static int lustre_lwp_add_conn(struct lustre_cfg *cfg,
			       struct lustre_sb_info *lsi)
{
	struct lustre_cfg_bufs *bufs = NULL;
	struct lustre_cfg      *lcfg = NULL;
	char		       *lwpname = NULL;
	struct obd_device      *lwp;
	int			rc;
	ENTRY;

	lwp = lustre_find_lwp(lsi, &lwpname, NULL);
	if (IS_ERR(lwp)) {
		CERROR("%s: can't find lwp device.\n", lsi->lsi_svname);
		GOTO(out, rc = PTR_ERR(lwp));
	}
	LASSERT(lwpname != NULL);

	OBD_ALLOC_PTR(bufs);
	if (bufs == NULL)
		GOTO(out, rc = -ENOMEM);

	lustre_cfg_bufs_reset(bufs, lwpname);
	lustre_cfg_bufs_set_string(bufs, 1,
				   lustre_cfg_string(cfg, 1));

	lcfg = lustre_cfg_new(LCFG_ADD_CONN, bufs);

	rc = class_add_conn(lwp, lcfg);
	if (rc)
		CERROR("%s: can't add conn: rc = %d\n", lwpname, rc);

out:
	if (bufs != NULL)
		OBD_FREE_PTR(bufs);
	if (lcfg != NULL)
		lustre_cfg_free(lcfg);
	if (lwpname != NULL)
		OBD_FREE(lwpname, MTI_NAME_MAXLEN);
	RETURN(rc);
}

/**
 * Retrieve MDT nids from the client log, then start the lwp device.
 * there are only two scenarios which would include mdt nid.
 * 1.
 * marker   5 (flags=0x01, v2.1.54.0) lustre-MDT0000  'add mdc' xxx-
 * add_uuid  nid=192.168.122.162@tcp(0x20000c0a87aa2)  0:  1:192.168.122.162@tcp
 * attach    0:lustre-MDT0000-mdc  1:mdc  2:lustre-clilmv_UUID
 * setup     0:lustre-MDT0000-mdc  1:lustre-MDT0000_UUID  2:192.168.122.162@tcp
 * add_uuid  nid=192.168.172.1@tcp(0x20000c0a8ac01)  0:  1:192.168.172.1@tcp
 * add_conn  0:lustre-MDT0000-mdc  1:192.168.172.1@tcp
 * modify_mdc_tgts add 0:lustre-clilmv  1:lustre-MDT0000_UUID xxxx
 * marker   5 (flags=0x02, v2.1.54.0) lustre-MDT0000  'add mdc' xxxx-
 * 2.
 * marker   7 (flags=0x01, v2.1.54.0) lustre-MDT0000  'add failnid' xxxx-
 * add_uuid  nid=192.168.122.2@tcp(0x20000c0a87a02)  0:  1:192.168.122.2@tcp
 * add_conn  0:lustre-MDT0000-mdc  1:192.168.122.2@tcp
 * marker   7 (flags=0x02, v2.1.54.0) lustre-MDT0000  'add failnid' xxxx-
 **/
static int client_lwp_config_process(const struct lu_env *env,
				     struct llog_handle *handle,
				     struct llog_rec_hdr *rec, void *data)
{
	struct config_llog_instance *clli = data;
	int			     cfg_len = rec->lrh_len;
	char			    *cfg_buf = (char *) (rec + 1);
	struct lustre_cfg	    *lcfg = NULL;
	struct lustre_sb_info	    *lsi;
	int			     rc = 0, swab = 0;
	ENTRY;

	if (rec->lrh_type != OBD_CFG_REC) {
		CERROR("Unknown llog record type %#x encountered\n",
		       rec->lrh_type);
		RETURN(-EINVAL);
	}

	LASSERT(clli->cfg_sb != NULL);
	lsi = s2lsi(clli->cfg_sb);

	lcfg = (struct lustre_cfg *)cfg_buf;
	if (lcfg->lcfg_version == __swab32(LUSTRE_CFG_VERSION)) {
		lustre_swab_lustre_cfg(lcfg);
		swab = 1;
	}

	rc = lustre_cfg_sanity_check(cfg_buf, cfg_len);
	if (rc)
		GOTO(out, rc);

	switch (lcfg->lcfg_command) {
	case LCFG_MARKER: {
		struct cfg_marker *marker = lustre_cfg_buf(lcfg, 1);

		lustre_swab_cfg_marker(marker, swab,
				       LUSTRE_CFG_BUFLEN(lcfg, 1));
		if (marker->cm_flags & CM_SKIP ||
		    marker->cm_flags & CM_EXCLUDE)
			GOTO(out, rc = 0);

		if (!tgt_is_mdt0(marker->cm_tgtname))
			GOTO(out, rc = 0);

		if (!strncmp(marker->cm_comment, "add mdc", 7) ||
		    !strncmp(marker->cm_comment, "add failnid", 11)) {
			if (marker->cm_flags & CM_START) {
				clli->cfg_flags = CFG_F_MARKER;
				/* This hack is to differentiate the
				 * ADD_UUID is come from "add mdc" record
				 * or from "add failnid" record. */
				if (!strncmp(marker->cm_comment,
					     "add failnid", 11))
					clli->cfg_flags |= CFG_F_SKIP;
			} else if (marker->cm_flags & CM_END) {
				clli->cfg_flags = 0;
			}
		}
		break;
	}
	case LCFG_ADD_UUID: {
		if (clli->cfg_flags == CFG_F_MARKER) {
			rc = lustre_lwp_setup(lcfg, lsi);
			/* XXX: process only the first nid as
			 * we don't need another instance of lwp */
			clli->cfg_flags |= CFG_F_SKIP;
		} else if (clli->cfg_flags == (CFG_F_MARKER | CFG_F_SKIP)) {
			rc = class_add_uuid(lustre_cfg_string(lcfg, 1),
					    lcfg->lcfg_nid);
			if (rc)
				CERROR("%s: Fail to add uuid, rc:%d\n",
				       lsi->lsi_svname, rc);
		}
		break;
	}
	case LCFG_ADD_CONN: {
		if (is_mdc_for_mdt0(lustre_cfg_string(lcfg, 0)))
			rc = lustre_lwp_add_conn(lcfg, lsi);
		break;
	}
	default:
		break;
	}
out:
	RETURN(rc);
}

static int lustre_disconnect_lwp(struct super_block *sb)
{
	struct lustre_sb_info		*lsi = s2lsi(sb);
	struct obd_device		*lwp;
	char				*lwpname = NULL;
	char				*logname = NULL;
	struct lustre_cfg		*lcfg = NULL;
	struct lustre_cfg_bufs		*bufs = NULL;
	struct config_llog_instance	*cfg = NULL;
	int				 rc;
	ENTRY;

	lwp = lustre_find_lwp(lsi, &lwpname, &logname);
	if (IS_ERR(lwp) && PTR_ERR(lwp) != -ENOENT)
		GOTO(out, rc = PTR_ERR(lwp));

	LASSERT(lwpname != NULL);
	LASSERT(logname != NULL);

	OBD_ALLOC_PTR(cfg);
	if (cfg == NULL)
		GOTO(out, rc = -ENOMEM);

	/* end log first */
	cfg->cfg_instance = sb;
	rc = lustre_end_log(sb, logname, cfg);
	if (rc != 0) {
		CERROR("%s: Can't end config log %s.\n", lwpname, logname);
		GOTO(out, rc);
	}

	if (PTR_ERR(lwp) == -ENOENT) {
		CDEBUG(D_CONFIG, "%s: lwp device wasn't started.\n",
		       lsi->lsi_svname);
		GOTO(out, rc = 0);
	}

	OBD_ALLOC_PTR(bufs);
	if (bufs == NULL)
		GOTO(out, rc = -ENOMEM);

	lustre_cfg_bufs_reset(bufs, lwp->obd_name);
	lustre_cfg_bufs_set_string(bufs, 1, NULL);
	lcfg = lustre_cfg_new(LCFG_CLEANUP, bufs);
	if (!lcfg)
		GOTO(out, rc = -ENOMEM);

	/* Disconnect import first. NULL is passed for the '@env', since
	 * it will not be used. */
	rc = lwp->obd_lu_dev->ld_ops->ldo_process_config(NULL, lwp->obd_lu_dev,
							 lcfg);
out:
	if (lcfg)
		lustre_cfg_free(lcfg);
	if (bufs)
		OBD_FREE_PTR(bufs);
	if (cfg)
		OBD_FREE_PTR(cfg);
	if (lwpname)
		OBD_FREE(lwpname, MTI_NAME_MAXLEN);
	if (logname)
		OBD_FREE(logname, MTI_NAME_MAXLEN);
	RETURN(rc);
}

/**
 * Stop the lwp for an OST/MDT target.
 **/
static int lustre_stop_lwp(struct super_block *sb)
{
	struct lustre_sb_info	*lsi = s2lsi(sb);
	struct obd_device	*lwp = NULL;
	char			*lwpname = NULL;
	int			 rc = 0;
	ENTRY;

	lwp = lustre_find_lwp(lsi, &lwpname, NULL);
	if (IS_ERR(lwp)) {
		CDEBUG(PTR_ERR(lwp) == -ENOENT ? D_CONFIG : D_ERROR,
		       "%s: lwp wasn't started.\n", lsi->lsi_svname);
		GOTO(out, rc = 0);
	}

	lwp->obd_force = 1;
	rc = class_manual_cleanup(lwp);

out:
	if (lwpname != NULL)
		OBD_FREE(lwpname, MTI_NAME_MAXLEN);
	RETURN(rc);
}

/**
 * Start the lwp(fsname-MDT0000-lwp-OSTxxxx) for an OST or MDT target,
 * which would be used to establish connection from OST to MDT0.
 **/
static int lustre_start_lwp(struct super_block *sb)
{
	struct lustre_sb_info	    *lsi = s2lsi(sb);
	struct config_llog_instance *cfg = NULL;
	struct obd_device	    *lwp;
	char			    *lwpname = NULL;
	char			    *logname = NULL;
	int			     rc;
	ENTRY;

	lwp = lustre_find_lwp(lsi, &lwpname, &logname);

	/* the lwp device already stared */
	if (lwp && !IS_ERR(lwp))
		GOTO(out, rc = 0);

	if (PTR_ERR(lwp) != -ENOENT)
		GOTO(out, rc = PTR_ERR(lwp));

	LASSERT(lwpname != NULL);
	LASSERT(logname != NULL);

	OBD_ALLOC_PTR(cfg);
	if (cfg == NULL)
		GOTO(out, rc = -ENOMEM);

	cfg->cfg_callback = client_lwp_config_process;
	cfg->cfg_instance = sb;

	rc = lustre_process_log(sb, logname, cfg);
out:
	if (lwpname != NULL)
		OBD_FREE(lwpname, MTI_NAME_MAXLEN);
	if (logname != NULL)
		OBD_FREE(logname, MTI_NAME_MAXLEN);
	if (cfg != NULL)
		OBD_FREE_PTR(cfg);
	RETURN(rc);
}

DEFINE_MUTEX(server_start_lock);

/* Stop MDS/OSS if nobody is using them */
static int server_stop_servers(int lsiflags)
{
	struct obd_device *obd = NULL;
	struct obd_type *type = NULL;
	int rc = 0;
	ENTRY;

	mutex_lock(&server_start_lock);

	/* Either an MDT or an OST or neither  */
	/* if this was an MDT, and there are no more MDT's, clean up the MDS */
	if (lsiflags & LDD_F_SV_TYPE_MDT) {
		obd = class_name2obd(LUSTRE_MDS_OBDNAME);
		if (obd != NULL)
			type = class_search_type(LUSTRE_MDT_NAME);
	}

	/* if this was an OST, and there are no more OST's, clean up the OSS */
	if (lsiflags & LDD_F_SV_TYPE_OST) {
		obd = class_name2obd(LUSTRE_OSS_OBDNAME);
		if (obd != NULL)
			type = class_search_type(LUSTRE_OST_NAME);
	}

	if (obd != NULL && (type == NULL || type->typ_refcnt == 0)) {
		int err;

		obd->obd_force = 1;
		/* obd_fail doesn't mean much on a server obd */
		err = class_manual_cleanup(obd);
		if (rc != 0)
			rc = err;
	}

	mutex_unlock(&server_start_lock);

	RETURN(rc);
}

int server_mti_print(const char *title, struct mgs_target_info *mti)
{
	PRINT_CMD(PRINT_MASK, "mti %s\n", title);
	PRINT_CMD(PRINT_MASK, "server: %s\n", mti->mti_svname);
	PRINT_CMD(PRINT_MASK, "fs:     %s\n", mti->mti_fsname);
	PRINT_CMD(PRINT_MASK, "uuid:   %s\n", mti->mti_uuid);
	PRINT_CMD(PRINT_MASK, "ver: %d  flags: %#x\n",
		  mti->mti_config_ver, mti->mti_flags);
	return 0;
}
EXPORT_SYMBOL(server_mti_print);

/* Generate data for registration */
static int server_lsi2mti(struct lustre_sb_info *lsi,
			  struct mgs_target_info *mti)
{
	lnet_process_id_t id;
	int rc, i = 0;
	int cplen = 0;
	ENTRY;

	if (!IS_SERVER(lsi))
		RETURN(-EINVAL);

	if (strlcpy(mti->mti_svname, lsi->lsi_svname, sizeof(mti->mti_svname))
	    >= sizeof(mti->mti_svname))
		RETURN(-E2BIG);

	mti->mti_nid_count = 0;
	while (LNetGetId(i++, &id) != -ENOENT) {
		if (LNET_NETTYP(LNET_NIDNET(id.nid)) == LOLND)
			continue;

		/* server use --servicenode param, only allow specified
		 * nids be registered */
		if ((lsi->lsi_lmd->lmd_flags & LMD_FLG_NO_PRIMNODE) != 0 &&
		    class_match_nid(lsi->lsi_lmd->lmd_params,
				    PARAM_FAILNODE, id.nid) < 1)
			continue;

		/* match specified network */
		if (!class_match_net(lsi->lsi_lmd->lmd_params,
				     PARAM_NETWORK, LNET_NIDNET(id.nid)))
			continue;

		mti->mti_nids[mti->mti_nid_count] = id.nid;
		mti->mti_nid_count++;
		if (mti->mti_nid_count >= MTI_NIDS_MAX) {
			CWARN("Only using first %d nids for %s\n",
			      mti->mti_nid_count, mti->mti_svname);
			break;
		}
	}

	mti->mti_lustre_ver = LUSTRE_VERSION_CODE;
	mti->mti_config_ver = 0;

	rc = server_name2fsname(lsi->lsi_svname, mti->mti_fsname, NULL);
	if (rc != 0)
		return rc;

	rc = server_name2index(lsi->lsi_svname, &mti->mti_stripe_index, NULL);
	if (rc < 0)
		return rc;
	/* Orion requires index to be set */
	LASSERT(!(rc & LDD_F_NEED_INDEX));
	/* keep only LDD flags */
	mti->mti_flags = lsi->lsi_flags & LDD_F_MASK;
	if (mti->mti_flags & (LDD_F_WRITECONF | LDD_F_VIRGIN))
		mti->mti_flags |= LDD_F_UPDATE;
	cplen = strlcpy(mti->mti_params, lsi->lsi_lmd->lmd_params,
			sizeof(mti->mti_params));
	if (cplen >= sizeof(mti->mti_params))
		return -E2BIG;
	return 0;
}

/* Register an old or new target with the MGS. If needed MGS will construct
   startup logs and assign index */
static int server_register_target(struct lustre_sb_info *lsi)
{
	struct obd_device *mgc = lsi->lsi_mgc;
	struct mgs_target_info *mti = NULL;
	bool writeconf;
	int rc;
	ENTRY;

	LASSERT(mgc);

	if (!IS_SERVER(lsi))
		RETURN(-EINVAL);

	OBD_ALLOC_PTR(mti);
	if (!mti)
		RETURN(-ENOMEM);

	rc = server_lsi2mti(lsi, mti);
	if (rc)
		GOTO(out, rc);

	CDEBUG(D_MOUNT, "Registration %s, fs=%s, %s, index=%04x, flags=%#x\n",
	       mti->mti_svname, mti->mti_fsname,
	       libcfs_nid2str(mti->mti_nids[0]), mti->mti_stripe_index,
	       mti->mti_flags);

	/* if write_conf is true, the registration must succeed */
	writeconf = !!(lsi->lsi_flags & (LDD_F_NEED_INDEX | LDD_F_UPDATE));
	mti->mti_flags |= LDD_F_OPC_REG;

	/* Register the target */
	/* FIXME use mgc_process_config instead */
	rc = obd_set_info_async(NULL, mgc->u.cli.cl_mgc_mgsexp,
				sizeof(KEY_REGISTER_TARGET),
				KEY_REGISTER_TARGET,
				sizeof(*mti), mti, NULL);
	if (rc) {
		if (mti->mti_flags & LDD_F_ERROR) {
			LCONSOLE_ERROR_MSG(0x160,
				"%s: the MGS refuses to allow this server "
				"to start: rc = %d. Please see messages on "
				"the MGS.\n", lsi->lsi_svname, rc);
		} else if (writeconf) {
			LCONSOLE_ERROR_MSG(0x15f,
				"%s: cannot register this server with the MGS: "
				"rc = %d. Is the MGS running?\n",
				lsi->lsi_svname, rc);
		} else {
			CERROR("%s: error registering with the MGS: rc = %d "
			       "(not fatal)\n", lsi->lsi_svname, rc);
			/* reset the error code for non-fatal error. */
			rc = 0;
		}
		GOTO(out, rc);
	}

out:
	if (mti)
		OBD_FREE_PTR(mti);
	RETURN(rc);
}

/**
 * Notify the MGS that this target is ready.
 * Used by IR - if the MGS receives this message, it will notify clients.
 */
static int server_notify_target(struct super_block *sb, struct obd_device *obd)
{
	struct lustre_sb_info *lsi = s2lsi(sb);
	struct obd_device *mgc = lsi->lsi_mgc;
	struct mgs_target_info *mti = NULL;
	int rc;
	ENTRY;

	LASSERT(mgc);

	if (!(IS_SERVER(lsi)))
		RETURN(-EINVAL);

	OBD_ALLOC_PTR(mti);
	if (!mti)
		RETURN(-ENOMEM);
	rc = server_lsi2mti(lsi, mti);
	if (rc)
		GOTO(out, rc);

	mti->mti_instance = obd->u.obt.obt_instance;
	mti->mti_flags |= LDD_F_OPC_READY;

	/* FIXME use mgc_process_config instead */
	rc = obd_set_info_async(NULL, mgc->u.cli.cl_mgc_mgsexp,
				sizeof(KEY_REGISTER_TARGET),
				KEY_REGISTER_TARGET,
				sizeof(*mti), mti, NULL);

	/* Imperative recovery: if the mgs informs us to use IR? */
	if (!rc && !(mti->mti_flags & LDD_F_ERROR) &&
	    (mti->mti_flags & LDD_F_IR_CAPABLE))
		lsi->lsi_flags |= LDD_F_IR_CAPABLE;

out:
	if (mti)
		OBD_FREE_PTR(mti);
	RETURN(rc);

}

/** Start server targets: MDTs and OSTs
 */
static int server_start_targets(struct super_block *sb, struct vfsmount *mnt)
{
	struct obd_device *obd;
	struct lustre_sb_info *lsi = s2lsi(sb);
	struct config_llog_instance cfg;
	struct lu_env env;
	struct lu_device *dev;
	int rc;
	ENTRY;

	CDEBUG(D_MOUNT, "starting target %s\n", lsi->lsi_svname);

	if (IS_MDT(lsi)) {
		/* make sure the MDS is started */
		mutex_lock(&server_start_lock);
		obd = class_name2obd(LUSTRE_MDS_OBDNAME);
		if (!obd) {
			rc = lustre_start_simple(LUSTRE_MDS_OBDNAME,
						 LUSTRE_MDS_NAME,
						 LUSTRE_MDS_OBDNAME"_uuid",
						 0, 0, 0, 0);
			if (rc) {
				mutex_unlock(&server_start_lock);
				CERROR("failed to start MDS: %d\n", rc);
				RETURN(rc);
			}
		}
		mutex_unlock(&server_start_lock);
	}

	/* If we're an OST, make sure the global OSS is running */
	if (IS_OST(lsi)) {
		/* make sure OSS is started */
		mutex_lock(&server_start_lock);
		obd = class_name2obd(LUSTRE_OSS_OBDNAME);
		if (!obd) {
			rc = lustre_start_simple(LUSTRE_OSS_OBDNAME,
						 LUSTRE_OSS_NAME,
						 LUSTRE_OSS_OBDNAME"_uuid",
						 0, 0, 0, 0);
			if (rc) {
				mutex_unlock(&server_start_lock);
				CERROR("failed to start OSS: %d\n", rc);
				RETURN(rc);
			}
		}
		mutex_unlock(&server_start_lock);
	}

	/* Set the mgc fs to our server disk.  This allows the MGC to
	 * read and write configs locally, in case it can't talk to the MGS. */
	if (lsi->lsi_srv_mnt) {
		rc = server_mgc_set_fs(lsi->lsi_mgc, sb);
		if (rc)
			GOTO(out_stop_service, rc);
	}

	/* Register with MGS */
	rc = server_register_target(lsi);
	if (rc)
		GOTO(out_mgc, rc);

	/* Let the target look up the mount using the target's name
	   (we can't pass the sb or mnt through class_process_config.) */
	rc = server_register_mount(lsi->lsi_svname, sb, mnt);
	if (rc)
		GOTO(out_mgc, rc);

	/* Start targets using the llog named for the target */
	memset(&cfg, 0, sizeof(cfg));
	cfg.cfg_callback = class_config_llog_handler;
	rc = lustre_process_log(sb, lsi->lsi_svname, &cfg);
	if (rc) {
		CERROR("failed to start server %s: %d\n",
		       lsi->lsi_svname, rc);
		/* Do NOT call server_deregister_mount() here. This makes it
		 * impossible to find mount later in cleanup time and leaves
		 * @lsi and othder stuff leaked. -umka */
		GOTO(out_mgc, rc);
	}

	obd = class_name2obd(lsi->lsi_svname);
	if (!obd) {
		CERROR("no server named %s was started\n", lsi->lsi_svname);
		GOTO(out_mgc, rc = -ENXIO);
	}

	if (IS_OST(lsi) || IS_MDT(lsi)) {
		rc = lustre_start_lwp(sb);
		if (rc) {
			CERROR("%s: failed to start LWP: %d\n",
			       lsi->lsi_svname, rc);
			GOTO(out_mgc, rc);
		}
	}

	server_notify_target(sb, obd);

	/* calculate recovery timeout, do it after lustre_process_log */
	server_calc_timeout(lsi, obd);

	/* log has been fully processed */
	obd_notify(obd, NULL, OBD_NOTIFY_CONFIG, (void *)CONFIG_LOG);

	/* log has been fully processed, let clients connect */
	dev = obd->obd_lu_dev;
	if (dev && dev->ld_ops->ldo_prepare) {
		rc = lu_env_init(&env, dev->ld_type->ldt_ctx_tags);
		if (rc == 0) {
			struct lu_context  session_ctx;

			lu_context_init(&session_ctx, LCT_SESSION);
			session_ctx.lc_thread = NULL;
			lu_context_enter(&session_ctx);
			env.le_ses = &session_ctx;

			rc = dev->ld_ops->ldo_prepare(&env, NULL, dev);

			lu_env_fini(&env);
			lu_context_exit(&session_ctx);
			lu_context_fini(&session_ctx);
		}
	}

	/* abort recovery only on the complete stack:
	 * many devices can be involved */
	if ((lsi->lsi_lmd->lmd_flags & LMD_FLG_ABORT_RECOV) &&
	    (OBP(obd, iocontrol))) {
		obd_iocontrol(OBD_IOC_ABORT_RECOVERY, obd->obd_self_export, 0,
			      NULL, NULL);
	}

out_mgc:
	/* Release the mgc fs for others to use */
	if (lsi->lsi_srv_mnt)
		server_mgc_clear_fs(lsi->lsi_mgc);

out_stop_service:
	if (rc != 0)
		server_stop_servers(lsi->lsi_flags);

	RETURN(rc);
}

static int lsi_prepare(struct lustre_sb_info *lsi)
{
	__u32 index;
	int rc;
	ENTRY;

	LASSERT(lsi);
	LASSERT(lsi->lsi_lmd);

	/* The server name is given as a mount line option */
	if (lsi->lsi_lmd->lmd_profile == NULL) {
		LCONSOLE_ERROR("Can't determine server name\n");
		RETURN(-EINVAL);
	}

	if (strlen(lsi->lsi_lmd->lmd_profile) >= sizeof(lsi->lsi_svname))
		RETURN(-ENAMETOOLONG);

	strcpy(lsi->lsi_svname, lsi->lsi_lmd->lmd_profile);

	/* Determine osd type */
	if (lsi->lsi_lmd->lmd_osd_type != NULL) {
		if (strlen(lsi->lsi_lmd->lmd_osd_type) >=
		    sizeof(lsi->lsi_osd_type))
			RETURN(-ENAMETOOLONG);

		strcpy(lsi->lsi_osd_type, lsi->lsi_lmd->lmd_osd_type);
	} else {
		strcpy(lsi->lsi_osd_type, LUSTRE_OSD_LDISKFS_NAME);
	}

	/* XXX: a temp. solution for components using fsfilt
	 *      to be removed in one of the subsequent patches */
	if (!strcmp(lsi->lsi_lmd->lmd_osd_type, "osd-ldiskfs"))
		strcpy(lsi->lsi_fstype, "ldiskfs");
	else
		strcpy(lsi->lsi_fstype, lsi->lsi_lmd->lmd_osd_type);

	/* Determine server type */
	rc = server_name2index(lsi->lsi_svname, &index, NULL);
	if (rc < 0) {
		if (lsi->lsi_lmd->lmd_flags & LMD_FLG_MGS) {
			/* Assume we're a bare MGS */
			rc = 0;
			lsi->lsi_lmd->lmd_flags |= LMD_FLG_NOSVC;
		} else {
			LCONSOLE_ERROR("Can't determine server type of '%s'\n",
				       lsi->lsi_svname);
			RETURN(rc);
		}
	}
	lsi->lsi_flags |= rc;

	/* Add mount line flags that used to be in ldd:
	 * writeconf, mgs, anything else?
	 */
	lsi->lsi_flags |= (lsi->lsi_lmd->lmd_flags & LMD_FLG_WRITECONF) ?
		LDD_F_WRITECONF : 0;
	lsi->lsi_flags |= (lsi->lsi_lmd->lmd_flags & LMD_FLG_VIRGIN) ?
		LDD_F_VIRGIN : 0;
	lsi->lsi_flags |= (lsi->lsi_lmd->lmd_flags & LMD_FLG_UPDATE) ?
		LDD_F_UPDATE : 0;
	lsi->lsi_flags |= (lsi->lsi_lmd->lmd_flags & LMD_FLG_MGS) ?
		LDD_F_SV_TYPE_MGS : 0;
	lsi->lsi_flags |= (lsi->lsi_lmd->lmd_flags & LMD_FLG_NO_PRIMNODE) ?
		LDD_F_NO_PRIMNODE : 0;

	RETURN(0);
}

/*************** server mount ******************/

/** Start the shutdown of servers at umount.
 */
static void server_put_super(struct super_block *sb)
{
	struct lustre_sb_info *lsi = s2lsi(sb);
	struct obd_device     *obd;
	char *tmpname, *extraname = NULL;
	int tmpname_sz;
	int lsiflags = lsi->lsi_flags;
	ENTRY;

	LASSERT(IS_SERVER(lsi));

	tmpname_sz = strlen(lsi->lsi_svname) + 1;
	OBD_ALLOC(tmpname, tmpname_sz);
	memcpy(tmpname, lsi->lsi_svname, tmpname_sz);
	CDEBUG(D_MOUNT, "server put_super %s\n", tmpname);
	if (IS_MDT(lsi) && (lsi->lsi_lmd->lmd_flags & LMD_FLG_NOSVC))
		snprintf(tmpname, tmpname_sz, "MGS");

	/* disconnect the lwp first to drain off the inflight request */
	if (IS_OST(lsi) || IS_MDT(lsi)) {
		int	rc;

		rc = lustre_disconnect_lwp(sb);
		if (rc && rc != ETIMEDOUT)
			CERROR("%s: failed to disconnect lwp. (rc=%d)\n",
			       tmpname, rc);
	}

	/* Stop the target */
	if (!(lsi->lsi_lmd->lmd_flags & LMD_FLG_NOSVC) &&
	    (IS_MDT(lsi) || IS_OST(lsi))) {
		struct lustre_profile *lprof = NULL;

		/* tell the mgc to drop the config log */
		lustre_end_log(sb, lsi->lsi_svname, NULL);

		/* COMPAT_146 - profile may get deleted in mgc_cleanup.
		   If there are any setup/cleanup errors, save the lov
		   name for safety cleanup later. */
		lprof = class_get_profile(lsi->lsi_svname);
		if (lprof && lprof->lp_dt) {
			OBD_ALLOC(extraname, strlen(lprof->lp_dt) + 1);
			strcpy(extraname, lprof->lp_dt);
		}

		obd = class_name2obd(lsi->lsi_svname);
		if (obd) {
			CDEBUG(D_MOUNT, "stopping %s\n", obd->obd_name);
			if (lsiflags & LSI_UMOUNT_FAILOVER)
				obd->obd_fail = 1;
			/* We can't seem to give an error return code
			 * to .put_super, so we better make sure we clean up! */
			obd->obd_force = 1;
			class_manual_cleanup(obd);
		} else {
			CERROR("no obd %s\n", lsi->lsi_svname);
			server_deregister_mount(lsi->lsi_svname);
		}
	}

	/* If they wanted the mgs to stop separately from the mdt, they
	   should have put it on a different device. */
	if (IS_MGS(lsi)) {
		/* if MDS start with --nomgs, don't stop MGS then */
		if (!(lsi->lsi_lmd->lmd_flags & LMD_FLG_NOMGS))
			server_stop_mgs(sb);
	}

	if (IS_OST(lsi) || IS_MDT(lsi)) {
		if (lustre_stop_lwp(sb) < 0)
			CERROR("%s: failed to stop lwp!\n", tmpname);
	}

	/* Clean the mgc and sb */
	lustre_common_put_super(sb);

	/* wait till all in-progress cleanups are done
	 * specifically we're interested in ofd cleanup
	 * as it pins OSS */
	obd_zombie_barrier();

	/* Stop the servers (MDS, OSS) if no longer needed.  We must wait
	   until the target is really gone so that our type refcount check
	   is right. */
	server_stop_servers(lsiflags);

	/* In case of startup or cleanup err, stop related obds */
	if (extraname) {
		obd = class_name2obd(extraname);
		if (obd) {
			CWARN("Cleaning orphaned obd %s\n", extraname);
			obd->obd_force = 1;
			class_manual_cleanup(obd);
		}
		OBD_FREE(extraname, strlen(extraname) + 1);
	}

	LCONSOLE_WARN("server umount %s complete\n", tmpname);
	OBD_FREE(tmpname, tmpname_sz);
	EXIT;
}

/** Called only for 'umount -f'
 */
static void server_umount_begin(struct super_block *sb)
{
	struct lustre_sb_info *lsi = s2lsi(sb);
	ENTRY;

	CDEBUG(D_MOUNT, "umount -f\n");
	/* umount = failover
	   umount -f = force
	   no third way to do non-force, non-failover */
	lsi->lsi_flags &= ~LSI_UMOUNT_FAILOVER;
	EXIT;
}

static int server_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct lustre_sb_info *lsi = s2lsi(sb);
	struct obd_statfs statfs;
	int rc;
	ENTRY;

	if (lsi->lsi_dt_dev) {
		rc = dt_statfs(NULL, lsi->lsi_dt_dev, &statfs);
		if (rc == 0) {
			statfs_unpack(buf, &statfs);
			buf->f_type = sb->s_magic;
			RETURN(0);
		}
	}

	/* just return 0 */
	buf->f_type = sb->s_magic;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = 1;
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_files = 1;
	buf->f_ffree = 0;
	buf->f_namelen = NAME_MAX;
	RETURN(0);
}

/** The operations we support directly on the superblock:
 * mount, umount, and df.
 */
static struct super_operations server_ops = {
	.put_super	= server_put_super,
	.umount_begin	= server_umount_begin, /* umount -f */
	.statfs		= server_statfs,
};

#define log2(n) ffz(~(n))
#define LUSTRE_SUPER_MAGIC 0x0BD00BD1

static int server_fill_super_common(struct super_block *sb)
{
	struct inode *root = 0;
	ENTRY;

	CDEBUG(D_MOUNT, "Server sb, dev=%d\n", (int)sb->s_dev);

	sb->s_blocksize = 4096;
	sb->s_blocksize_bits = log2(sb->s_blocksize);
	sb->s_magic = LUSTRE_SUPER_MAGIC;
	sb->s_maxbytes = 0; /* we don't allow file IO on server mountpoints */
	sb->s_flags |= MS_RDONLY;
	sb->s_op = &server_ops;

	root = new_inode(sb);
	if (!root) {
		CERROR("Can't make root inode\n");
		RETURN(-EIO);
	}

	/* returns -EIO for every operation */
	/* make_bad_inode(root); -- badness - can't umount */
	/* apparently we need to be a directory for the mount to finish */
	root->i_mode = S_IFDIR;

	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		CERROR("%s: can't make root dentry\n", sb->s_id);
		RETURN(-EIO);
	}

	RETURN(0);
}

static int osd_start(struct lustre_sb_info *lsi, unsigned long mflags)
{
	struct lustre_mount_data *lmd = lsi->lsi_lmd;
	struct obd_device	 *obd;
	struct dt_device_param    p;
	char			  flagstr[16];
	int			  rc;
	ENTRY;

	CDEBUG(D_MOUNT,
	       "Attempting to start %s, type=%s, lsifl=%x, mountfl=%lx\n",
	       lsi->lsi_svname, lsi->lsi_osd_type, lsi->lsi_flags, mflags);

	sprintf(lsi->lsi_osd_obdname, "%s-osd", lsi->lsi_svname);
	strcpy(lsi->lsi_osd_uuid, lsi->lsi_osd_obdname);
	strcat(lsi->lsi_osd_uuid, "_UUID");
	sprintf(flagstr, "%lu:%lu", mflags, (unsigned long) lmd->lmd_flags);

	obd = class_name2obd(lsi->lsi_osd_obdname);
	if (obd == NULL) {
		rc = lustre_start_simple(lsi->lsi_osd_obdname,
					 lsi->lsi_osd_type,
					 lsi->lsi_osd_uuid, lmd->lmd_dev,
					 flagstr, lsi->lsi_lmd->lmd_opts,
					 lsi->lsi_svname);
		if (rc)
			GOTO(out, rc);
		obd = class_name2obd(lsi->lsi_osd_obdname);
		LASSERT(obd);
	}

	rc = obd_connect(NULL, &lsi->lsi_osd_exp,
			 obd, &obd->obd_uuid, NULL, NULL);
	if (rc) {
		obd->obd_force = 1;
		class_manual_cleanup(obd);
		lsi->lsi_dt_dev = NULL;
	}

	/* XXX: to keep support old components relying on lsi_srv_mnt
	 *	we get this info from OSD just started */
	LASSERT(obd->obd_lu_dev);
	lsi->lsi_dt_dev = lu2dt_dev(obd->obd_lu_dev);
	LASSERT(lsi->lsi_dt_dev);

	dt_conf_get(NULL, lsi->lsi_dt_dev, &p);

	lsi->lsi_srv_mnt = p.ddp_mnt;

out:
	RETURN(rc);
}

/** Fill in the superblock info for a Lustre server.
 * Mount the device with the correct options.
 * Read the on-disk config file.
 * Start the services.
 */
int server_fill_super(struct super_block *sb)
{
	struct lustre_sb_info *lsi = s2lsi(sb);
	int rc;
	ENTRY;

	rc = lsi_prepare(lsi);
	if (rc)
		RETURN(rc);

	/* Start low level OSD */
	rc = osd_start(lsi, sb->s_flags);
	if (rc) {
		CERROR("Unable to start osd on %s: %d\n",
		       lsi->lsi_lmd->lmd_dev, rc);
		lustre_put_lsi(sb);
		RETURN(rc);
	}

	CDEBUG(D_MOUNT, "Found service %s on device %s\n",
	       lsi->lsi_svname, lsi->lsi_lmd->lmd_dev);

	if (class_name2obd(lsi->lsi_svname)) {
		LCONSOLE_ERROR_MSG(0x161, "The target named %s is already "
				   "running. Double-mount may have compromised"
				   " the disk journal.\n",
				   lsi->lsi_svname);
		lustre_put_lsi(sb);
		RETURN(-EALREADY);
	}

	/* Start MGS before MGC */
	if (IS_MGS(lsi) && !(lsi->lsi_lmd->lmd_flags & LMD_FLG_NOMGS)) {
		rc = server_start_mgs(sb);
		if (rc)
			GOTO(out_mnt, rc);
	}

	/* Start MGC before servers */
	rc = lustre_start_mgc(sb);
	if (rc)
		GOTO(out_mnt, rc);

	/* Set up all obd devices for service */
	if (!(lsi->lsi_lmd->lmd_flags & LMD_FLG_NOSVC) &&
	    (IS_OST(lsi) || IS_MDT(lsi))) {
		rc = server_start_targets(sb, lsi->lsi_srv_mnt);
		if (rc < 0) {
			CERROR("Unable to start targets: %d\n", rc);
			GOTO(out_mnt, rc);
		}
		/* FIXME overmount client here, or can we just start a
		 * client log and client_fill_super on this sb?  We
		 * need to make sure server_put_super gets called too
		 * - ll_put_super calls lustre_common_put_super; check
		 * there for LSI_SERVER flag, call s_p_s if so.
		 *
		 * Probably should start client from new thread so we
		 * can return.  Client will not finish until all
		 * servers are connected.  Note - MGS-only server does
		 * NOT get a client, since there is no lustre fs
		 * associated - the MGS is for all lustre fs's */
	}

	rc = server_fill_super_common(sb);
	if (rc)
		GOTO(out_mnt, rc);

	RETURN(0);
out_mnt:
	/* We jump here in case of failure while starting targets or MGS.
	 * In this case we can't just put @mnt and have to do real cleanup
	 * with stoping targets, etc. */
	server_put_super(sb);
	return rc;
}

/*
 * Calculate timeout value for a target.
 */
void server_calc_timeout(struct lustre_sb_info *lsi, struct obd_device *obd)
{
	struct lustre_mount_data *lmd;
	int soft = 0;
	int hard = 0;
	int factor = 0;
	bool has_ir = !!(lsi->lsi_flags & LDD_F_IR_CAPABLE);
	int min = OBD_RECOVERY_TIME_MIN;

	LASSERT(IS_SERVER(lsi));

	lmd = lsi->lsi_lmd;
	if (lmd) {
		soft   = lmd->lmd_recovery_time_soft;
		hard   = lmd->lmd_recovery_time_hard;
		has_ir = has_ir && !(lmd->lmd_flags & LMD_FLG_NOIR);
		obd->obd_no_ir = !has_ir;
	}

	if (soft == 0)
		soft = OBD_RECOVERY_TIME_SOFT;
	if (hard == 0)
		hard = OBD_RECOVERY_TIME_HARD;

	/* target may have ir_factor configured. */
	factor = OBD_IR_FACTOR_DEFAULT;
	if (obd->obd_recovery_ir_factor)
		factor = obd->obd_recovery_ir_factor;

	if (has_ir) {
		int new_soft = soft;
		int new_hard = hard;

		/* adjust timeout value by imperative recovery */

		new_soft = (soft * factor) / OBD_IR_FACTOR_MAX;
		new_hard = (hard * factor) / OBD_IR_FACTOR_MAX;

		/* make sure the timeout is not too short */
		new_soft = max(min, new_soft);
		new_hard = max(new_soft, new_hard);

		LCONSOLE_INFO("%s: Imperative Recovery enabled, recovery "
			      "window shrunk from %d-%d down to %d-%d\n",
			      obd->obd_name, soft, hard, new_soft, new_hard);

		soft = new_soft;
		hard = new_hard;
	}

	/* we're done */
	obd->obd_recovery_timeout   = max(obd->obd_recovery_timeout, soft);
	obd->obd_recovery_time_hard = hard;
	obd->obd_recovery_ir_factor = factor;
}
EXPORT_SYMBOL(server_calc_timeout);
