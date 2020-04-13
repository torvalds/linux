/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */
#ifndef _IPA_UC_H_
#define _IPA_UC_H_

struct ipa;

/**
 * ipa_uc_setup() - set up the IPA microcontroller subsystem
 * @ipa:	IPA pointer
 */
void ipa_uc_setup(struct ipa *ipa);

/**
 * ipa_uc_teardown() - inverse of ipa_uc_setup()
 * @ipa:	IPA pointer
 */
void ipa_uc_teardown(struct ipa *ipa);

/**
 * ipa_uc_panic_notifier()
 * @ipa:	IPA pointer
 *
 * Notifier function called when the system crashes, to inform the
 * microcontroller of the event.
 */
void ipa_uc_panic_notifier(struct ipa *ipa);

#endif /* _IPA_UC_H_ */
