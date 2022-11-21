/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2018 - 2020 Intel Corporation.
 */

#ifndef _HFI1_MSIX_H
#define _HFI1_MSIX_H

#include "hfi.h"

/* MSIx interface */
int msix_initialize(struct hfi1_devdata *dd);
int msix_request_irqs(struct hfi1_devdata *dd);
void msix_clean_up_interrupts(struct hfi1_devdata *dd);
int msix_request_general_irq(struct hfi1_devdata *dd);
int msix_request_rcd_irq(struct hfi1_ctxtdata *rcd);
int msix_request_sdma_irq(struct sdma_engine *sde);
void msix_free_irq(struct hfi1_devdata *dd, u8 msix_intr);

/* Netdev interface */
void msix_netdev_synchronize_irq(struct hfi1_devdata *dd);
int msix_netdev_request_rcd_irq(struct hfi1_ctxtdata *rcd);

#endif
