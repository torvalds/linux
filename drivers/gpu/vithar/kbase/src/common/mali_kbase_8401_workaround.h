/*
 *
 * (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_kbase_8401_workaround.h
 * Functions related to working around BASE_HW_ISSUE_8401
 */

#ifndef _KBASE_8401_WORKAROUND_H_
#define _KBASE_8401_WORKAROUND_H_

mali_error kbasep_8401_workaround_init(kbase_device *kbdev);
void kbasep_8401_workaround_term(kbase_device *kbdev);
void kbasep_8401_submit_dummy_job(kbase_device *kbdev, int js);
mali_bool kbasep_8401_is_workaround_job(kbase_jd_atom *katom);

#endif /* _KBASE_8401_WORKAROUND_H_ */

