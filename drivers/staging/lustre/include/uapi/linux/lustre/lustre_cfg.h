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
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef _UAPI_LUSTRE_CFG_H_
#define _UAPI_LUSTRE_CFG_H_

#include <linux/errno.h>
#include <linux/kernel.h>
#include <uapi/linux/lustre/lustre_user.h>

/** \defgroup cfg cfg
 *
 * @{
 */

/*
 * 1cf6
 * lcfG
 */
#define LUSTRE_CFG_VERSION 0x1cf60001
#define LUSTRE_CFG_MAX_BUFCOUNT 8

#define LCFG_HDR_SIZE(count) \
	__ALIGN_KERNEL(offsetof(struct lustre_cfg, lcfg_buflens[(count)]), 8)

/** If the LCFG_REQUIRED bit is set in a configuration command,
 * then the client is required to understand this parameter
 * in order to mount the filesystem. If it does not understand
 * a REQUIRED command the client mount will fail.
 */
#define LCFG_REQUIRED	0x0001000

enum lcfg_command_type {
	LCFG_ATTACH		  = 0x00cf001, /**< create a new obd instance */
	LCFG_DETACH		  = 0x00cf002, /**< destroy obd instance */
	LCFG_SETUP		  = 0x00cf003, /**< call type-specific setup */
	LCFG_CLEANUP		  = 0x00cf004, /**< call type-specific cleanup
						 */
	LCFG_ADD_UUID		  = 0x00cf005, /**< add a nid to a niduuid */
	LCFG_DEL_UUID		  = 0x00cf006, /**< remove a nid from
						 *  a niduuid
						 */
	LCFG_MOUNTOPT		  = 0x00cf007, /**< create a profile
						 * (mdc, osc)
						 */
	LCFG_DEL_MOUNTOPT	  = 0x00cf008, /**< destroy a profile */
	LCFG_SET_TIMEOUT	  = 0x00cf009, /**< set obd_timeout */
	LCFG_SET_UPCALL		  = 0x00cf00a, /**< deprecated */
	LCFG_ADD_CONN		  = 0x00cf00b, /**< add a failover niduuid to
						 *  an obd
						 */
	LCFG_DEL_CONN		  = 0x00cf00c, /**< remove a failover niduuid */
	LCFG_LOV_ADD_OBD	  = 0x00cf00d, /**< add an osc to a lov */
	LCFG_LOV_DEL_OBD	  = 0x00cf00e, /**< remove an osc from a lov */
	LCFG_PARAM		  = 0x00cf00f, /**< set a proc parameter */
	LCFG_MARKER		  = 0x00cf010, /**< metadata about next
						 *  cfg rec
						 */
	LCFG_LOG_START		  = 0x00ce011, /**< mgc only, process a
						 *  cfg log
						 */
	LCFG_LOG_END		  = 0x00ce012, /**< stop processing updates */
	LCFG_LOV_ADD_INA	  = 0x00ce013, /**< like LOV_ADD_OBD,
						 *  inactive
						 */
	LCFG_ADD_MDC		  = 0x00cf014, /**< add an mdc to a lmv */
	LCFG_DEL_MDC		  = 0x00cf015, /**< remove an mdc from a lmv */
	LCFG_SPTLRPC_CONF	  = 0x00ce016, /**< security */
	LCFG_POOL_NEW		  = 0x00ce020, /**< create an ost pool name */
	LCFG_POOL_ADD		  = 0x00ce021, /**< add an ost to a pool */
	LCFG_POOL_REM		  = 0x00ce022, /**< remove an ost from a pool */
	LCFG_POOL_DEL		  = 0x00ce023, /**< destroy an ost pool name */
	LCFG_SET_LDLM_TIMEOUT	  = 0x00ce030, /**< set ldlm_timeout */
	LCFG_PRE_CLEANUP	  = 0x00cf031, /**< call type-specific pre
						 * cleanup cleanup
						 */
	LCFG_SET_PARAM		  = 0x00ce032, /**< use set_param syntax to set
						 * a proc parameters
						 */
};

struct lustre_cfg_bufs {
	void  *lcfg_buf[LUSTRE_CFG_MAX_BUFCOUNT];
	__u32 lcfg_buflen[LUSTRE_CFG_MAX_BUFCOUNT];
	__u32 lcfg_bufcount;
};

struct lustre_cfg {
	__u32 lcfg_version;
	__u32 lcfg_command;

	__u32 lcfg_num;
	__u32 lcfg_flags;
	__u64 lcfg_nid;
	__u32 lcfg_nal;		/* not used any more */

	__u32 lcfg_bufcount;
	__u32 lcfg_buflens[0];
};

enum cfg_record_type {
	PORTALS_CFG_TYPE	= 1,
	LUSTRE_CFG_TYPE		= 123,
};

#define LUSTRE_CFG_BUFLEN(lcfg, idx)					\
	((lcfg)->lcfg_bufcount <= (idx) ? 0 : (lcfg)->lcfg_buflens[(idx)])

static inline void lustre_cfg_bufs_set(struct lustre_cfg_bufs *bufs,
				       __u32 index, void *buf, __u32 buflen)
{
	if (index >= LUSTRE_CFG_MAX_BUFCOUNT)
		return;

	if (!bufs)
		return;

	if (bufs->lcfg_bufcount <= index)
		bufs->lcfg_bufcount = index + 1;

	bufs->lcfg_buf[index] = buf;
	bufs->lcfg_buflen[index] = buflen;
}

static inline void lustre_cfg_bufs_set_string(struct lustre_cfg_bufs *bufs,
					      __u32 index, char *str)
{
	lustre_cfg_bufs_set(bufs, index, str, str ? strlen(str) + 1 : 0);
}

static inline void lustre_cfg_bufs_reset(struct lustre_cfg_bufs *bufs,
					 char *name)
{
	memset((bufs), 0, sizeof(*bufs));
	if (name)
		lustre_cfg_bufs_set_string(bufs, 0, name);
}

static inline void *lustre_cfg_buf(struct lustre_cfg *lcfg, __u32 index)
{
	__u32 i;
	size_t offset;
	__u32 bufcount;

	if (!lcfg)
		return NULL;

	bufcount = lcfg->lcfg_bufcount;
	if (index >= bufcount)
		return NULL;

	offset = LCFG_HDR_SIZE(lcfg->lcfg_bufcount);
	for (i = 0; i < index; i++)
		offset += __ALIGN_KERNEL(lcfg->lcfg_buflens[i], 8);
	return (char *)lcfg + offset;
}

static inline void lustre_cfg_bufs_init(struct lustre_cfg_bufs *bufs,
					struct lustre_cfg *lcfg)
{
	__u32 i;

	bufs->lcfg_bufcount = lcfg->lcfg_bufcount;
	for (i = 0; i < bufs->lcfg_bufcount; i++) {
		bufs->lcfg_buflen[i] = lcfg->lcfg_buflens[i];
		bufs->lcfg_buf[i] = lustre_cfg_buf(lcfg, i);
	}
}

static inline __u32 lustre_cfg_len(__u32 bufcount, __u32 *buflens)
{
	__u32 i;
	__u32 len;

	len = LCFG_HDR_SIZE(bufcount);
	for (i = 0; i < bufcount; i++)
		len += __ALIGN_KERNEL(buflens[i], 8);

	return __ALIGN_KERNEL(len, 8);
}

static inline void lustre_cfg_init(struct lustre_cfg *lcfg, int cmd,
				   struct lustre_cfg_bufs *bufs)
{
	char *ptr;
	__u32 i;

	lcfg->lcfg_version = LUSTRE_CFG_VERSION;
	lcfg->lcfg_command = cmd;
	lcfg->lcfg_bufcount = bufs->lcfg_bufcount;

	ptr = (char *)lcfg + LCFG_HDR_SIZE(lcfg->lcfg_bufcount);
	for (i = 0; i < lcfg->lcfg_bufcount; i++) {
		lcfg->lcfg_buflens[i] = bufs->lcfg_buflen[i];
		if (bufs->lcfg_buf[i]) {
			memcpy(ptr, bufs->lcfg_buf[i], bufs->lcfg_buflen[i]);
			ptr += __ALIGN_KERNEL(bufs->lcfg_buflen[i], 8);
		}
	}
}

static inline int lustre_cfg_sanity_check(void *buf, size_t len)
{
	struct lustre_cfg *lcfg = (struct lustre_cfg *)buf;

	if (!lcfg)
		return -EINVAL;

	/* check that the first bits of the struct are valid */
	if (len < LCFG_HDR_SIZE(0))
		return -EINVAL;

	if (lcfg->lcfg_version != LUSTRE_CFG_VERSION)
		return -EINVAL;

	if (lcfg->lcfg_bufcount >= LUSTRE_CFG_MAX_BUFCOUNT)
		return -EINVAL;

	/* check that the buflens are valid */
	if (len < LCFG_HDR_SIZE(lcfg->lcfg_bufcount))
		return -EINVAL;

	/* make sure all the pointers point inside the data */
	if (len < lustre_cfg_len(lcfg->lcfg_bufcount, lcfg->lcfg_buflens))
		return -EINVAL;

	return 0;
}

/** @} cfg */

#endif /* _UAPI_LUSTRE_CFG_H_ */
