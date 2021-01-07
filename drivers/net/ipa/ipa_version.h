/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */
#ifndef _IPA_VERSION_H_
#define _IPA_VERSION_H_

/**
 * enum ipa_version
 *
 * Defines the version of IPA (and GSI) hardware present on the platform.
 * It seems this might be better defined elsewhere, but having it here gets
 * it where it's needed.
 */
enum ipa_version {
	IPA_VERSION_3_5_1,	/* GSI version 1.3.0 */
	IPA_VERSION_4_0,	/* GSI version 2.0 */
	IPA_VERSION_4_1,	/* GSI version 2.1 */
	IPA_VERSION_4_2,	/* GSI version 2.2 */
	IPA_VERSION_4_5,	/* GSI version 2.5 */
};

#endif /* _IPA_VERSION_H_ */
