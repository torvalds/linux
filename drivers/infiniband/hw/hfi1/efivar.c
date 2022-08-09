// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 */

#include <linux/string.h>
#include <linux/string_helpers.h>

#include "efivar.h"

/* GUID for HFI1 variables in EFI */
#define HFI1_EFIVAR_GUID EFI_GUID(0xc50a953e, 0xa8b2, 0x42a6, \
		0xbf, 0x89, 0xd3, 0x33, 0xa6, 0xe9, 0xe6, 0xd4)
/* largest EFI data size we expect */
#define EFI_DATA_SIZE 4096

/*
 * Read the named EFI variable.  Return the size of the actual data in *size
 * and a kmalloc'ed buffer in *return_data.  The caller must free the
 * data.  It is guaranteed that *return_data will be NULL and *size = 0
 * if this routine fails.
 *
 * Return 0 on success, -errno on failure.
 */
static int read_efi_var(const char *name, unsigned long *size,
			void **return_data)
{
	efi_status_t status;
	efi_char16_t *uni_name;
	efi_guid_t guid;
	unsigned long temp_size;
	void *temp_buffer;
	void *data;
	int i;
	int ret;

	/* set failure return values */
	*size = 0;
	*return_data = NULL;

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE))
		return -EOPNOTSUPP;

	uni_name = kcalloc(strlen(name) + 1, sizeof(efi_char16_t), GFP_KERNEL);
	temp_buffer = kzalloc(EFI_DATA_SIZE, GFP_KERNEL);

	if (!uni_name || !temp_buffer) {
		ret = -ENOMEM;
		goto fail;
	}

	/* input: the size of the buffer */
	temp_size = EFI_DATA_SIZE;

	/* convert ASCII to unicode - it is a 1:1 mapping */
	for (i = 0; name[i]; i++)
		uni_name[i] = name[i];

	/* need a variable for our GUID */
	guid = HFI1_EFIVAR_GUID;

	/* call into EFI runtime services */
	status = efi.get_variable(
			uni_name,
			&guid,
			NULL,
			&temp_size,
			temp_buffer);

	/*
	 * It would be nice to call efi_status_to_err() here, but that
	 * is in the EFIVAR_FS code and may not be compiled in.
	 * However, even that is insufficient since it does not cover
	 * EFI_BUFFER_TOO_SMALL which could be an important return.
	 * For now, just split out succces or not found.
	 */
	ret = status == EFI_SUCCESS   ? 0 :
	      status == EFI_NOT_FOUND ? -ENOENT :
					-EINVAL;
	if (ret)
		goto fail;

	/*
	 * We have successfully read the EFI variable into our
	 * temporary buffer.  Now allocate a correctly sized
	 * buffer.
	 */
	data = kmemdup(temp_buffer, temp_size, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto fail;
	}

	*size = temp_size;
	*return_data = data;

fail:
	kfree(uni_name);
	kfree(temp_buffer);

	return ret;
}

/*
 * Read an HFI1 EFI variable of the form:
 *	<PCIe address>-<kind>
 * Return an kalloc'ed array and size of the data.
 *
 * Returns 0 on success, -errno on failure.
 */
int read_hfi1_efi_var(struct hfi1_devdata *dd, const char *kind,
		      unsigned long *size, void **return_data)
{
	char prefix_name[64];
	char name[64];
	int result;

	/* create a common prefix */
	snprintf(prefix_name, sizeof(prefix_name), "%04x:%02x:%02x.%x",
		 pci_domain_nr(dd->pcidev->bus),
		 dd->pcidev->bus->number,
		 PCI_SLOT(dd->pcidev->devfn),
		 PCI_FUNC(dd->pcidev->devfn));
	snprintf(name, sizeof(name), "%s-%s", prefix_name, kind);
	result = read_efi_var(name, size, return_data);

	/*
	 * If reading the lowercase EFI variable fail, read the uppercase
	 * variable.
	 */
	if (result) {
		string_upper(prefix_name, prefix_name);
		snprintf(name, sizeof(name), "%s-%s", prefix_name, kind);
		result = read_efi_var(name, size, return_data);
	}

	return result;
}
