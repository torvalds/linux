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
#include "sci_controller_constants.h"
#include "scic_remote_device.h"
#include "sci_environment.h"
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

	if (!oprom)
		return NULL;

	len = pci_biosrom_size(pdev);
	rom = devm_kzalloc(&pdev->dev, sizeof(*rom), GFP_KERNEL);

	for (i = 0; i < len && rom; i += ISCI_ROM_SIG_SIZE) {
		memcpy_fromio(rom->hdr.signature, oprom + i, ISCI_ROM_SIG_SIZE);
		if (memcmp(rom->hdr.signature, ISCI_ROM_SIG,
			   ISCI_ROM_SIG_SIZE) == 0) {
			size_t copy_len = min(len - i, sizeof(*rom));

			memcpy_fromio(rom, oprom + i, copy_len);
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
	int i;

	/* check for valid inputs */
	if (!(scu_index >= 0
	      && scu_index < SCI_MAX_CONTROLLERS
	      && oem_params != NULL))
		return -EINVAL;

	for (i = 0; i < SCI_MAX_PHYS; i++) {
		oem_params->sds1.phys[i].sas_address.low =
			orom->ctrl[scu_index].phys[i].sas_address.low;
		oem_params->sds1.phys[i].sas_address.high =
			orom->ctrl[scu_index].phys[i].sas_address.high;
	}

	for (i = 0; i < SCI_MAX_PORTS; i++)
		oem_params->sds1.ports[i].phy_mask =
			orom->ctrl[scu_index].ports[i].phy_mask;

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
	struct isci_orom *orom = NULL;

	evar = devm_kzalloc(&pdev->dev,
			    sizeof(struct efi_variable),
			    GFP_KERNEL);
	if (!evar) {
		dev_warn(&pdev->dev,
			 "Unable to allocate memory for EFI var\n");
		return NULL;
	}

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

	if (status == EFI_SUCCESS)
		orom = (struct isci_orom *)evar->Data;
	else
		dev_warn(&pdev->dev,
			 "Unable to obtain EFI variable for OEM parms\n");

	if (orom && memcmp(orom->hdr.signature, ISCI_ROM_SIG,
			   strlen(ISCI_ROM_SIG)) != 0)
		dev_warn(&pdev->dev,
			 "Verifying OROM signature failed\n");

	if (!orom)
		devm_kfree(&pdev->dev, evar);

	return orom;
}
