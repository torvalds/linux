/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 */

/* probe_roms - scan for oem parameters */

#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <linux/efi.h>
#include <asm/probe_roms.h>

#include "isci.h"
#include "task.h"
#include "probe_roms.h"

struct efi_variable {
	efi_char16_t  VariableName[1024/sizeof(efi_char16_t)];
	efi_guid_t    VendorGuid;
	unsigned long DataSize;
	__u8          Data[1024];
	efi_status_t  Status;
	__u32         Attributes;
} __attribute__((packed));

struct isci_orom *isci_request_oprom(struct pci_dev *pdev)
{
	void __iomem *oprom = pci_map_biosrom(pdev);
	struct isci_orom *rom = NULL;
	size_t len, i;
	int j;
	char oem_sig[4];
	struct isci_oem_hdr oem_hdr;
	u8 *tmp, sum;

	if (!oprom)
		return NULL;

	len = pci_biosrom_size(pdev);
	rom = devm_kzalloc(&pdev->dev, sizeof(*rom), GFP_KERNEL);
	if (!rom) {
		dev_warn(&pdev->dev,
			 "Unable to allocate memory for orom\n");
		return NULL;
	}

	for (i = 0; i < len && rom; i += ISCI_OEM_SIG_SIZE) {
		memcpy_fromio(oem_sig, oprom + i, ISCI_OEM_SIG_SIZE);

		/* we think we found the OEM table */
		if (memcmp(oem_sig, ISCI_OEM_SIG, ISCI_OEM_SIG_SIZE) == 0) {
			size_t copy_len;

			memcpy_fromio(&oem_hdr, oprom + i, sizeof(oem_hdr));

			copy_len = min(oem_hdr.len - sizeof(oem_hdr),
				       sizeof(*rom));

			memcpy_fromio(rom,
				      oprom + i + sizeof(oem_hdr),
				      copy_len);

			/* calculate checksum */
			tmp = (u8 *)&oem_hdr;
			for (j = 0, sum = 0; j < sizeof(oem_hdr); j++, tmp++)
				sum += *tmp;

			tmp = (u8 *)rom;
			for (j = 0; j < sizeof(*rom); j++, tmp++)
				sum += *tmp;

			if (sum != 0) {
				dev_warn(&pdev->dev,
					 "OEM table checksum failed\n");
				continue;
			}

			/* keep going if that's not the oem param table */
			if (memcmp(rom->hdr.signature,
				   ISCI_ROM_SIG,
				   ISCI_ROM_SIG_SIZE) != 0)
				continue;

			dev_info(&pdev->dev,
				 "OEM parameter table found in OROM\n");
			break;
		}
	}

	if (i >= len) {
		dev_err(&pdev->dev, "oprom parse error\n");
		devm_kfree(&pdev->dev, rom);
		rom = NULL;
	}
	pci_unmap_biosrom(oprom);

	return rom;
}

/**
 * isci_parse_oem_parameters() - This method will take OEM parameters
 *    from the module init parameters and copy them to oem_params. This will
 *    only copy values that are not set to the module parameter default values
 * @oem_parameters: This parameter specifies the controller default OEM
 *    parameters. It is expected that this has been initialized to the default
 *    parameters for the controller
 *
 *
 */
enum sci_status isci_parse_oem_parameters(union scic_oem_parameters *oem_params,
					  struct isci_orom *orom, int scu_index)
{
	/* check for valid inputs */
	if (scu_index < 0 || scu_index > SCI_MAX_CONTROLLERS ||
	    scu_index > orom->hdr.num_elements || !oem_params)
		return -EINVAL;

	oem_params->sds1 = orom->ctrl[scu_index];
	return 0;
}

struct isci_orom *isci_request_firmware(struct pci_dev *pdev, const struct firmware *fw)
{
	struct isci_orom *orom = NULL, *data;

	if (request_firmware(&fw, ISCI_FW_NAME, &pdev->dev) != 0)
		return NULL;

	if (fw->size < sizeof(*orom))
		goto out;

	data = (struct isci_orom *)fw->data;

	if (strncmp(ISCI_ROM_SIG, data->hdr.signature,
		    strlen(ISCI_ROM_SIG)) != 0)
		goto out;

	orom = devm_kzalloc(&pdev->dev, fw->size, GFP_KERNEL);
	if (!orom)
		goto out;

	memcpy(orom, fw->data, fw->size);

 out:
	release_firmware(fw);

	return orom;
}

static struct efi *get_efi(void)
{
	#ifdef CONFIG_EFI
	return &efi;
	#else
	return NULL;
	#endif
}

struct isci_orom *isci_get_efi_var(struct pci_dev *pdev)
{
	struct efi_variable *evar;
	efi_status_t status;
	struct isci_orom *rom = NULL;
	struct isci_oem_hdr *oem_hdr;
	u8 *tmp, sum;
	int j;
	size_t copy_len;

	evar = devm_kzalloc(&pdev->dev,
			    sizeof(struct efi_variable),
			    GFP_KERNEL);
	if (!evar) {
		dev_warn(&pdev->dev,
			 "Unable to allocate memory for EFI var\n");
		return NULL;
	}

	rom = devm_kzalloc(&pdev->dev, sizeof(*rom), GFP_KERNEL);
	if (!rom) {
		dev_warn(&pdev->dev,
			 "Unable to allocate memory for orom\n");
		return NULL;
	}

	for (j = 0; j < strlen(ISCI_EFI_VAR_NAME) + 1; j++)
		evar->VariableName[j] = ISCI_EFI_VAR_NAME[j];

	evar->DataSize = 1024;
	evar->VendorGuid = ISCI_EFI_VENDOR_GUID;
	evar->Attributes = ISCI_EFI_ATTRIBUTES;

	if (get_efi())
		status = get_efi()->get_variable(evar->VariableName,
						 &evar->VendorGuid,
						 &evar->Attributes,
						 &evar->DataSize,
						 evar->Data);
	else
		status = EFI_NOT_FOUND;

	if (status != EFI_SUCCESS) {
		dev_warn(&pdev->dev,
			 "Unable to obtain EFI variable for OEM parms\n");
		return NULL;
	}

	oem_hdr = (struct isci_oem_hdr *)evar->Data;

	if (memcmp(oem_hdr->sig, ISCI_OEM_SIG, ISCI_OEM_SIG_SIZE) != 0) {
		dev_warn(&pdev->dev,
			 "Invalid OEM header signature\n");
		return NULL;
	}

	/* calculate checksum */
	tmp = (u8 *)oem_hdr;
	for (j = 0, sum = 0; j < sizeof(oem_hdr); j++, tmp++)
		sum += *tmp;

	tmp = (u8 *)rom;
	for (j = 0; j < sizeof(*rom); j++, tmp++)
		sum += *tmp;

	if (sum != 0) {
		dev_warn(&pdev->dev,
			 "OEM table checksum failed\n");
		return NULL;
	}

	copy_len = min_t(u16, evar->DataSize,
			 min_t(u16, oem_hdr->len - sizeof(*oem_hdr), sizeof(*rom)));

	memcpy(rom, (char *)evar->Data + sizeof(*oem_hdr), copy_len);

	if (memcmp(rom->hdr.signature,
		   ISCI_ROM_SIG,
		   ISCI_ROM_SIG_SIZE) != 0) {
		dev_warn(&pdev->dev,
			 "Invalid OEM table signature\n");
		return NULL;
	}

	return rom;
}
