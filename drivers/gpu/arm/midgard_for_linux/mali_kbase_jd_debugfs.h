/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_kbase_jd_debugfs.h
 * Header file for job dispatcher-related entries in debugfs
 */

#ifndef _KBASE_JD_DEBUGFS_H
#define _KBASE_JD_DEBUGFS_H

#include <linux/debugfs.h>

#include <mali_kbase.h>

#define MALI_JD_DEBUGFS_VERSION 1

/**
 * kbasep_jd_debugfs_ctx_init() - Add debugfs entries for JD system
 *
 * @kctx Pointer to kbase_context
 */
void kbasep_jd_debugfs_ctx_init(struct kbase_context *kctx);

#endif  /*_KBASE_JD_DEBUGFS_H*/
