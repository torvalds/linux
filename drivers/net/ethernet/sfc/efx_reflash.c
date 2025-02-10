// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for AMD network controllers and boards
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/crc32.h>
#include <net/devlink.h>
#include "efx_reflash.h"
#include "net_driver.h"
#include "fw_formats.h"
#include "mcdi_pcol.h"
#include "mcdi.h"

/* Try to parse a Reflash header at the specified offset */
static bool efx_reflash_parse_reflash_header(const struct firmware *fw,
					     size_t header_offset, u32 *type,
					     u32 *subtype, const u8 **data,
					     size_t *data_size)
{
	size_t header_end, trailer_offset, trailer_end;
	u32 magic, version, payload_size, header_len;
	const u8 *header, *trailer;
	u32 expected_crc, crc;

	if (check_add_overflow(header_offset, EFX_REFLASH_HEADER_LENGTH_OFST +
					      EFX_REFLASH_HEADER_LENGTH_LEN,
			       &header_end))
		return false;
	if (fw->size < header_end)
		return false;

	header = fw->data + header_offset;
	magic = get_unaligned_le32(header + EFX_REFLASH_HEADER_MAGIC_OFST);
	if (magic != EFX_REFLASH_HEADER_MAGIC_VALUE)
		return false;

	version = get_unaligned_le32(header + EFX_REFLASH_HEADER_VERSION_OFST);
	if (version != EFX_REFLASH_HEADER_VERSION_VALUE)
		return false;

	payload_size = get_unaligned_le32(header + EFX_REFLASH_HEADER_PAYLOAD_SIZE_OFST);
	header_len = get_unaligned_le32(header + EFX_REFLASH_HEADER_LENGTH_OFST);
	if (check_add_overflow(header_offset, header_len, &trailer_offset) ||
	    check_add_overflow(trailer_offset, payload_size, &trailer_offset) ||
	    check_add_overflow(trailer_offset, EFX_REFLASH_TRAILER_LEN,
			       &trailer_end))
		return false;
	if (fw->size < trailer_end)
		return false;

	trailer = fw->data + trailer_offset;
	expected_crc = get_unaligned_le32(trailer + EFX_REFLASH_TRAILER_CRC_OFST);
	/* Addition could overflow u32, but not size_t since we already
	 * checked trailer_offset didn't overflow.  So cast to size_t first.
	 */
	crc = crc32_le(0, header, (size_t)header_len + payload_size);
	if (crc != expected_crc)
		return false;

	*type = get_unaligned_le32(header + EFX_REFLASH_HEADER_FIRMWARE_TYPE_OFST);
	*subtype = get_unaligned_le32(header + EFX_REFLASH_HEADER_FIRMWARE_SUBTYPE_OFST);
	if (*type == EFX_REFLASH_FIRMWARE_TYPE_BUNDLE) {
		/* All the bundle data is written verbatim to NVRAM */
		*data = fw->data;
		*data_size = fw->size;
	} else {
		/* Other payload types strip the reflash header and trailer
		 * from the data written to NVRAM
		 */
		*data = header + header_len;
		*data_size = payload_size;
	}

	return true;
}

/* Map from FIRMWARE_TYPE to NVRAM_PARTITION_TYPE */
static int efx_reflash_partition_type(u32 type, u32 subtype,
				      u32 *partition_type,
				      u32 *partition_subtype)
{
	int rc = 0;

	switch (type) {
	case EFX_REFLASH_FIRMWARE_TYPE_BOOTROM:
		*partition_type = NVRAM_PARTITION_TYPE_EXPANSION_ROM;
		*partition_subtype = subtype;
		break;
	case EFX_REFLASH_FIRMWARE_TYPE_BUNDLE:
		*partition_type = NVRAM_PARTITION_TYPE_BUNDLE;
		*partition_subtype = subtype;
		break;
	default:
		/* Not supported */
		rc = -EINVAL;
	}

	return rc;
}

/* Try to parse a SmartNIC image header at the specified offset */
static bool efx_reflash_parse_snic_header(const struct firmware *fw,
					  size_t header_offset,
					  u32 *partition_type,
					  u32 *partition_subtype,
					  const u8 **data, size_t *data_size)
{
	u32 magic, version, payload_size, header_len, expected_crc, crc;
	size_t header_end, payload_end;
	const u8 *header;

	if (check_add_overflow(header_offset, EFX_SNICIMAGE_HEADER_MINLEN,
			       &header_end) ||
	    fw->size < header_end)
		return false;

	header = fw->data + header_offset;
	magic = get_unaligned_le32(header + EFX_SNICIMAGE_HEADER_MAGIC_OFST);
	if (magic != EFX_SNICIMAGE_HEADER_MAGIC_VALUE)
		return false;

	version = get_unaligned_le32(header + EFX_SNICIMAGE_HEADER_VERSION_OFST);
	if (version != EFX_SNICIMAGE_HEADER_VERSION_VALUE)
		return false;

	header_len = get_unaligned_le32(header + EFX_SNICIMAGE_HEADER_LENGTH_OFST);
	if (check_add_overflow(header_offset, header_len, &header_end))
		return false;
	payload_size = get_unaligned_le32(header + EFX_SNICIMAGE_HEADER_PAYLOAD_SIZE_OFST);
	if (check_add_overflow(header_end, payload_size, &payload_end) ||
	    fw->size < payload_end)
		return false;

	expected_crc = get_unaligned_le32(header + EFX_SNICIMAGE_HEADER_CRC_OFST);

	/* Calculate CRC omitting the expected CRC field itself */
	crc = crc32_le(~0, header, EFX_SNICIMAGE_HEADER_CRC_OFST);
	crc = ~crc32_le(crc,
			header + EFX_SNICIMAGE_HEADER_CRC_OFST +
			EFX_SNICIMAGE_HEADER_CRC_LEN,
			header_len + payload_size - EFX_SNICIMAGE_HEADER_CRC_OFST -
			EFX_SNICIMAGE_HEADER_CRC_LEN);
	if (crc != expected_crc)
		return false;

	*partition_type =
		get_unaligned_le32(header + EFX_SNICIMAGE_HEADER_PARTITION_TYPE_OFST);
	*partition_subtype =
		get_unaligned_le32(header + EFX_SNICIMAGE_HEADER_PARTITION_SUBTYPE_OFST);
	*data = fw->data;
	*data_size = fw->size;
	return true;
}

/* Try to parse a SmartNIC bundle header at the specified offset */
static bool efx_reflash_parse_snic_bundle_header(const struct firmware *fw,
						 size_t header_offset,
						 u32 *partition_type,
						 u32 *partition_subtype,
						 const u8 **data,
						 size_t *data_size)
{
	u32 magic, version, bundle_type, header_len, expected_crc, crc;
	size_t header_end;
	const u8 *header;

	if (check_add_overflow(header_offset, EFX_SNICBUNDLE_HEADER_LEN,
			       &header_end))
		return false;
	if (fw->size < header_end)
		return false;

	header = fw->data + header_offset;
	magic = get_unaligned_le32(header + EFX_SNICBUNDLE_HEADER_MAGIC_OFST);
	if (magic != EFX_SNICBUNDLE_HEADER_MAGIC_VALUE)
		return false;

	version = get_unaligned_le32(header + EFX_SNICBUNDLE_HEADER_VERSION_OFST);
	if (version != EFX_SNICBUNDLE_HEADER_VERSION_VALUE)
		return false;

	bundle_type = get_unaligned_le32(header + EFX_SNICBUNDLE_HEADER_BUNDLE_TYPE_OFST);
	if (bundle_type != NVRAM_PARTITION_TYPE_BUNDLE)
		return false;

	header_len = get_unaligned_le32(header + EFX_SNICBUNDLE_HEADER_LENGTH_OFST);
	if (header_len != EFX_SNICBUNDLE_HEADER_LEN)
		return false;

	expected_crc = get_unaligned_le32(header + EFX_SNICBUNDLE_HEADER_CRC_OFST);
	crc = ~crc32_le(~0, header, EFX_SNICBUNDLE_HEADER_CRC_OFST);
	if (crc != expected_crc)
		return false;

	*partition_type = NVRAM_PARTITION_TYPE_BUNDLE;
	*partition_subtype = get_unaligned_le32(header + EFX_SNICBUNDLE_HEADER_BUNDLE_SUBTYPE_OFST);
	*data = fw->data;
	*data_size = fw->size;
	return true;
}

/* Try to find a valid firmware payload in the firmware data.
 * When we recognise a valid header, we parse it for the partition type
 * (so we know where to ask the MC to write it to) and the location of
 * the data blob to write.
 */
static int efx_reflash_parse_firmware_data(const struct firmware *fw,
					   u32 *partition_type,
					   u32 *partition_subtype,
					   const u8 **data, size_t *data_size)
{
	size_t header_offset;
	u32 type, subtype;

	/* Some packaging formats (such as CMS/PKCS#7 signed images)
	 * prepend a header for which finding the size is a non-trivial
	 * task, so step through the firmware data until we find a valid
	 * header.
	 *
	 * The checks are intended to reject firmware data that is clearly not
	 * in the expected format.  They do not need to be exhaustive as the
	 * running firmware will perform its own comprehensive validity and
	 * compatibility checks during the update procedure.
	 *
	 * Firmware packages may contain multiple reflash images, e.g. a
	 * bundle containing one or more other images.  Only check the
	 * outermost container by stopping after the first candidate image
	 * found even it is for an unsupported partition type.
	 */
	for (header_offset = 0; header_offset < fw->size; header_offset++) {
		if (efx_reflash_parse_snic_bundle_header(fw, header_offset,
							 partition_type,
							 partition_subtype,
							 data, data_size))
			return 0;

		if (efx_reflash_parse_snic_header(fw, header_offset,
						  partition_type,
						  partition_subtype, data,
						  data_size))
			return 0;

		if (efx_reflash_parse_reflash_header(fw, header_offset, &type,
						     &subtype, data, data_size))
			return efx_reflash_partition_type(type, subtype,
							  partition_type,
							  partition_subtype);
	}

	return -EINVAL;
}

int efx_reflash_flash_firmware(struct efx_nic *efx, const struct firmware *fw,
			       struct netlink_ext_ack *extack)
{
	struct devlink *devlink = efx->devlink;
	u32 type, data_subtype;
	size_t data_size;
	const u8 *data;
	int rc;

	if (!efx_has_cap(efx, BUNDLE_UPDATE)) {
		NL_SET_ERR_MSG_MOD(extack, "NVRAM bundle updates are not supported by the firmware");
		return -EOPNOTSUPP;
	}

	devlink_flash_update_status_notify(devlink, "Checking update", NULL, 0, 0);

	rc = efx_reflash_parse_firmware_data(fw, &type, &data_subtype, &data,
					     &data_size);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Firmware image validation check failed");
		goto out;
	}

	rc = -EOPNOTSUPP;

out:
	devlink_flash_update_status_notify(devlink, rc ? "Update failed" :
							 "Update complete",
					   NULL, 0, 0);
	return rc;
}
