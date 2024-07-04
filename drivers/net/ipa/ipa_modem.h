/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2024 Linaro Ltd.
 */
#ifndef _IPA_MODEM_H_
#define _IPA_MODEM_H_

struct net_device;
struct sk_buff;

struct ipa;

int ipa_modem_start(struct ipa *ipa);
int ipa_modem_stop(struct ipa *ipa);

void ipa_modem_skb_rx(struct net_device *netdev, struct sk_buff *skb);

void ipa_modem_suspend(struct net_device *netdev);
void ipa_modem_resume(struct net_device *netdev);

int ipa_modem_config(struct ipa *ipa);
void ipa_modem_deconfig(struct ipa *ipa);

#endif /* _IPA_MODEM_H_ */
