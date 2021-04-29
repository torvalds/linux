/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */
#ifndef _IPA_VERSION_H_
#define _IPA_VERSION_H_

/**
 * enum ipa_version
 * @IPA_VERSION_3_0:	IPA version 3.0/GSI version 1.0
 * @IPA_VERSION_3_1:	IPA version 3.1/GSI version 1.1
 * @IPA_VERSION_3_5:	IPA version 3.5/GSI version 1.2
 * @IPA_VERSION_3_5_1:	IPA version 3.5.1/GSI version 1.3
 * @IPA_VERSION_4_0:	IPA version 4.0/GSI version 2.0
 * @IPA_VERSION_4_1:	IPA version 4.1/GSI version 2.0
 * @IPA_VERSION_4_2:	IPA version 4.2/GSI version 2.2
 * @IPA_VERSION_4_5:	IPA version 4.5/GSI version 2.5
 * @IPA_VERSION_4_7:	IPA version 4.7/GSI version 2.7
 * @IPA_VERSION_4_9:	IPA version 4.9/GSI version 2.9
 * @IPA_VERSION_4_11:	IPA version 4.11/GSI version 2.11 (2.1.1)
 *
 * Defines the version of IPA (and GSI) hardware present on the platform.
 */
enum ipa_version {
	IPA_VERSION_3_0,
	IPA_VERSION_3_1,
	IPA_VERSION_3_5,
	IPA_VERSION_3_5_1,
	IPA_VERSION_4_0,
	IPA_VERSION_4_1,
	IPA_VERSION_4_2,
	IPA_VERSION_4_5,
	IPA_VERSION_4_7,
	IPA_VERSION_4_9,
	IPA_VERSION_4_11,
};

#endif /* _IPA_VERSION_H_ */
