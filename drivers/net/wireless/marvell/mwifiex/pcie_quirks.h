/* SPDX-License-Identifier: GPL-2.0-only */
/* NXP Wireless LAN device driver: PCIE and platform specific quirks */

#include "pcie.h"

#define QUIRK_FW_RST_D3COLD	BIT(0)

void mwifiex_initialize_quirks(struct pcie_service_card *card);
int mwifiex_pcie_reset_d3cold_quirk(struct pci_dev *pdev);
