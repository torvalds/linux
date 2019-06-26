// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2013 Broadcom Corporation
 */
#ifndef BRCMFMAC_BCDC_H
#define BRCMFMAC_BCDC_H

#ifdef CONFIG_BRCMFMAC_PROTO_BCDC
int brcmf_proto_bcdc_attach(struct brcmf_pub *drvr);
void brcmf_proto_bcdc_detach_pre_delif(struct brcmf_pub *drvr);
void brcmf_proto_bcdc_detach_post_delif(struct brcmf_pub *drvr);
void brcmf_proto_bcdc_txflowblock(struct device *dev, bool state);
void brcmf_proto_bcdc_txcomplete(struct device *dev, struct sk_buff *txp,
				 bool success);
struct brcmf_fws_info *drvr_to_fws(struct brcmf_pub *drvr);
#else
static inline int brcmf_proto_bcdc_attach(struct brcmf_pub *drvr) { return 0; }
static void brcmf_proto_bcdc_detach_pre_delif(struct brcmf_pub *drvr) {};
static inline void brcmf_proto_bcdc_detach_post_delif(struct brcmf_pub *drvr) {}
#endif

#endif /* BRCMFMAC_BCDC_H */
