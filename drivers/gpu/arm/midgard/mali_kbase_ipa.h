/*
 *
 * (C) COPYRIGHT 2011-2015 ARM Limited. All rights reserved.
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



struct kbase_ipa_context;

/**
 * kbase_ipa_init - initialize the kbase ipa core
 * @kbdev:      kbase device
 *
 * Return:      pointer to the IPA context or NULL on failure
 */
struct kbase_ipa_context *kbase_ipa_init(struct kbase_device *kbdev);

/**
 * kbase_ipa_term - terminate the kbase ipa core
 * @ctx:        pointer to the IPA context
 */
void kbase_ipa_term(struct kbase_ipa_context *ctx);
