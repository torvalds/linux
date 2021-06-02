/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2021 Linaro Ltd.
 */
#ifndef _IPA_RESOURCE_H_
#define _IPA_RESOURCE_H_

struct ipa;
struct ipa_resource_data;

/**
 * ipa_resource_config() - Configure resources
 * @ipa:	IPA pointer
 * @data:	IPA resource configuration data
 *
 * There is no need for a matching ipa_resource_deconfig() function.
 *
 * Return:	true if all regions are valid, false otherwise
 */
int ipa_resource_config(struct ipa *ipa, const struct ipa_resource_data *data);

#endif /* _IPA_RESOURCE_H_ */
