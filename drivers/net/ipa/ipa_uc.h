/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */
#ifndef _IPA_UC_H_
#define _IPA_UC_H_

struct ipa;

/**
 * ipa_uc_config() - Configure the IPA microcontroller subsystem
 * @ipa:	IPA pointer
 */
void ipa_uc_config(struct ipa *ipa);

/**
 * ipa_uc_deconfig() - Inverse of ipa_uc_config()
 * @ipa:	IPA pointer
 */
void ipa_uc_deconfig(struct ipa *ipa);

/**
 * ipa_uc_panic_notifier()
 * @ipa:	IPA pointer
 *
 * Notifier function called when the system crashes, to inform the
 * microcontroller of the event.
 */
void ipa_uc_panic_notifier(struct ipa *ipa);

#endif /* _IPA_UC_H_ */
