/*
 * ***************************************************************************
 *  FILE:     sme_userspace.h
 *
 *  PURPOSE:    SME related definitions.
 *
 *  Copyright (C) 2007-2008 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ***************************************************************************
 */
#ifndef __LINUX_SME_USERSPACE_H__
#define __LINUX_SME_USERSPACE_H__ 1

#include <linux/kernel.h>

int uf_sme_init(unifi_priv_t *priv);
void uf_sme_deinit(unifi_priv_t *priv);
int uf_sme_queue_message(unifi_priv_t *priv, u8 *buffer, int length);


#include "csr_wifi_router_lib.h"
#include "csr_wifi_router_sef.h"
#include "csr_wifi_router_ctrl_lib.h"
#include "csr_wifi_router_ctrl_sef.h"
#include "csr_wifi_sme_task.h"
#ifdef CSR_SUPPORT_WEXT_AP
#include "csr_wifi_nme_ap_lib.h"
#endif
#include "csr_wifi_sme_lib.h"

void CsrWifiRouterTransportInit(unifi_priv_t *priv);
void CsrWifiRouterTransportRecv(unifi_priv_t *priv, u8* buffer, CsrSize bufferLength);
void CsrWifiRouterTransportDeInit(unifi_priv_t *priv);

#endif /* __LINUX_SME_USERSPACE_H__ */
