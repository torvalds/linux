/*
 *
 * (C) COPYRIGHT 2017 ARM Limited. All rights reserved.
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



#ifndef _KBASE_IPA_DEBUGFS_H_
#define _KBASE_IPA_DEBUGFS_H_

enum kbase_ipa_model_param_type {
	PARAM_TYPE_S32 = 1,
	PARAM_TYPE_STRING,
};

#ifdef CONFIG_DEBUG_FS

void kbase_ipa_debugfs_init(struct kbase_device *kbdev);
int kbase_ipa_model_param_add(struct kbase_ipa_model *model, const char *name,
			      void *addr, size_t size,
			      enum kbase_ipa_model_param_type type);
void kbase_ipa_model_param_free_all(struct kbase_ipa_model *model);

#else /* CONFIG_DEBUG_FS */

static inline int kbase_ipa_model_param_add(struct kbase_ipa_model *model,
					    const char *name, void *addr,
					    size_t size,
					    enum kbase_ipa_model_param_type type)
{
	return 0;
}

static inline void kbase_ipa_model_param_free_all(struct kbase_ipa_model *model)
{ }

#endif /* CONFIG_DEBUG_FS */

#endif /* _KBASE_IPA_DEBUGFS_H_ */
