// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
 */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */
#include <linux/firmware.h>
#include "bnad.h"
#include "bfi.h"
#include "cna.h"

const struct firmware *bfi_fw;
static u32 *bfi_image_ct_cna, *bfi_image_ct2_cna;
static u32 bfi_image_ct_cna_size, bfi_image_ct2_cna_size;

static u32 *
cna_read_firmware(struct pci_dev *pdev, u32 **bfi_image,
			u32 *bfi_image_size, char *fw_name)
{
	const struct firmware *fw;
	u32 n;

	if (request_firmware(&fw, fw_name, &pdev->dev)) {
		dev_alert(&pdev->dev, "can't load firmware %s\n", fw_name);
		goto error;
	}

	*bfi_image = (u32 *)fw->data;
	*bfi_image_size = fw->size/sizeof(u32);
	bfi_fw = fw;

	/* Convert loaded firmware to host order as it is stored in file
	 * as sequence of LE32 integers.
	 */
	for (n = 0; n < *bfi_image_size; n++)
		le32_to_cpus(*bfi_image + n);

	return *bfi_image;
error:
	return NULL;
}

u32 *
cna_get_firmware_buf(struct pci_dev *pdev)
{
	if (pdev->device == BFA_PCI_DEVICE_ID_CT2) {
		if (bfi_image_ct2_cna_size == 0)
			cna_read_firmware(pdev, &bfi_image_ct2_cna,
				&bfi_image_ct2_cna_size, CNA_FW_FILE_CT2);
		return bfi_image_ct2_cna;
	} else if (bfa_asic_id_ct(pdev->device)) {
		if (bfi_image_ct_cna_size == 0)
			cna_read_firmware(pdev, &bfi_image_ct_cna,
				&bfi_image_ct_cna_size, CNA_FW_FILE_CT);
		return bfi_image_ct_cna;
	}

	return NULL;
}

u32 *
bfa_cb_image_get_chunk(enum bfi_asic_gen asic_gen, u32 off)
{
	switch (asic_gen) {
	case BFI_ASIC_GEN_CT:
		return (bfi_image_ct_cna + off);
	case BFI_ASIC_GEN_CT2:
		return (bfi_image_ct2_cna + off);
	default:
		return NULL;
	}
}

u32
bfa_cb_image_get_size(enum bfi_asic_gen asic_gen)
{
	switch (asic_gen) {
	case BFI_ASIC_GEN_CT:
		return bfi_image_ct_cna_size;
	case BFI_ASIC_GEN_CT2:
		return bfi_image_ct2_cna_size;
	default:
		return 0;
	}
}
