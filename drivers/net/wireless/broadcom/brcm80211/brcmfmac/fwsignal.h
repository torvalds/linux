// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2012 Broadcom Corporation
 */

#ifndef FWSIGNAL_H_
#define FWSIGNAL_H_

struct brcmf_fws_info *brcmf_fws_attach(struct brcmf_pub *drvr);
void brcmf_fws_detach(struct brcmf_fws_info *fws);
void brcmf_fws_debugfs_create(struct brcmf_pub *drvr);
bool brcmf_fws_queue_skbs(struct brcmf_fws_info *fws);
bool brcmf_fws_fc_active(struct brcmf_fws_info *fws);
void brcmf_fws_hdrpull(struct brcmf_if *ifp, s16 siglen, struct sk_buff *skb);
int brcmf_fws_process_skb(struct brcmf_if *ifp, struct sk_buff *skb);

void brcmf_fws_reset_interface(struct brcmf_if *ifp);
void brcmf_fws_add_interface(struct brcmf_if *ifp);
void brcmf_fws_del_interface(struct brcmf_if *ifp);
void brcmf_fws_bustxfail(struct brcmf_fws_info *fws, struct sk_buff *skb);
void brcmf_fws_bus_blocked(struct brcmf_pub *drvr, bool flow_blocked);
void brcmf_fws_rxreorder(struct brcmf_if *ifp, struct sk_buff *skb);

#endif /* FWSIGNAL_H_ */
