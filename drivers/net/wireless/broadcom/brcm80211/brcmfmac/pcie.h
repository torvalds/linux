// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */
#ifndef BRCMFMAC_PCIE_H
#define BRCMFMAC_PCIE_H


struct brcmf_pciedev {
	struct brcmf_bus *bus;
	struct brcmf_pciedev_info *devinfo;
};


void brcmf_pcie_exit(void);
void brcmf_pcie_register(void);


#endif /* BRCMFMAC_PCIE_H */
