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

/* Limit the number of status updates during the erase or write phases */
#define EFX_DEVLINK_STATUS_UPDATE_COUNT		50

/* Expected timeout for the efx_mcdi_nvram_update_finish_polled() */
#define EFX_DEVLINK_UPDATE_FINISH_TIMEOUT	900

/* Ideal erase chunk size.  This is a balance between minimising the number of
 * MCDI requests to erase an entire partition whilst avoiding tripping the MCDI
 * RPC timeout.
 */
#define EFX_NVRAM_ERASE_IDEAL_CHUNK_SIZE	(64 * 1024)

static int efx_reflash_erase_partition(struct efx_nic *efx,
				       struct netlink_ext_ack *extack,
				       struct devlink *devlink, u32 type,
				       size_t partition_size,
				       size_t align)
{
	size_t chunk, offset, next_update;
	int rc;

	/* Partitions that cannot be erased or do not require erase before
	 * write are advertised with a erase alignment/sector size of zero.
	 */
	if (align == 0)
		/* Nothing to do */
		return 0;

	if (partition_size % align)
		return -EINVAL;

	/* Erase the entire NVRAM partition a chunk at a time to avoid
	 * potentially tripping the MCDI RPC timeout.
	 */
	if (align >= EFX_NVRAM_ERASE_IDEAL_CHUNK_SIZE)
		chunk = align;
	else
		chunk = rounddown(EFX_NVRAM_ERASE_IDEAL_CHUNK_SIZE, align);

	for (offset = 0, next_update = 0; offset < partition_size; offset += chunk) {
		if (offset >= next_update) {
			devlink_flash_update_status_notify(devlink, "Erasing",
							   NULL, offset,
							   partition_size);
			next_update += partition_size / EFX_DEVLINK_STATUS_UPDATE_COUNT;
		}

		chunk = min_t(size_t, partition_size - offset, chunk);
		rc = efx_mcdi_nvram_erase(efx, type, offset, chunk);
		if (rc) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Erase failed for NVRAM partition %#x at %#zx-%#zx",
					       type, offset, offset + chunk - 1);
			return rc;
		}
	}

	devlink_flash_update_status_notify(devlink, "Erasing", NULL,
					   partition_size, partition_size);

	return 0;
}

static int efx_reflash_write_partition(struct efx_nic *efx,
				       struct netlink_ext_ack *extack,
				       struct devlink *devlink, u32 type,
				       const u8 *data, size_t data_size,
				       size_t align)
{
	size_t write_max, chunk, offset, next_update;
	int rc;

	if (align == 0)
		return -EINVAL;

	/* Write the NVRAM partition in chunks that are the largest multiple
	 * of the partition's required write alignment that will fit into the
	 * MCDI NVRAM_WRITE RPC payload.
	 */
	if (efx->type->mcdi_max_ver < 2)
		write_max = MC_CMD_NVRAM_WRITE_IN_WRITE_BUFFER_LEN *
			    MC_CMD_NVRAM_WRITE_IN_WRITE_BUFFER_MAXNUM;
	else
		write_max = MC_CMD_NVRAM_WRITE_IN_WRITE_BUFFER_LEN *
			    MC_CMD_NVRAM_WRITE_IN_WRITE_BUFFER_MAXNUM_MCDI2;
	chunk = rounddown(write_max, align);

	for (offset = 0, next_update = 0; offset + chunk <= data_size; offset += chunk) {
		if (offset >= next_update) {
			devlink_flash_update_status_notify(devlink, "Writing",
							   NULL, offset,
							   data_size);
			next_update += data_size / EFX_DEVLINK_STATUS_UPDATE_COUNT;
		}

		rc = efx_mcdi_nvram_write(efx, type, offset, data + offset, chunk);
		if (rc) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Write failed for NVRAM partition %#x at %#zx-%#zx",
					       type, offset, offset + chunk - 1);
			return rc;
		}
	}

	/* Round up left over data to satisfy write alignment */
	if (offset < data_size) {
		size_t remaining = data_size - offset;
		u8 *buf;

		if (offset >= next_update)
			devlink_flash_update_status_notify(devlink, "Writing",
							   NULL, offset,
							   data_size);

		chunk = roundup(remaining, align);
		buf = kmalloc(chunk, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		memcpy(buf, data + offset, remaining);
		memset(buf + remaining, 0xFF, chunk - remaining);
		rc = efx_mcdi_nvram_write(efx, type, offset, buf, chunk);
		kfree(buf);
		if (rc) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Write failed for NVRAM partition %#x at %#zx-%#zx",
					       type, offset, offset + chunk - 1);
			return rc;
		}
	}

	devlink_flash_update_status_notify(devlink, "Writing", NULL, data_size,
					   data_size);

	return 0;
}

int efx_reflash_flash_firmware(struct efx_nic *efx, const struct firmware *fw,
			       struct netlink_ext_ack *extack)
{
	size_t data_size, size, erase_align, write_align;
	struct devlink *devlink = efx->devlink;
	u32 type, data_subtype, subtype;
	const u8 *data;
	bool protected;
	int rc;

	if (!efx_has_cap(efx, BUNDLE_UPDATE)) {
		NL_SET_ERR_MSG_MOD(extack, "NVRAM bundle updates are not supported by the firmware");
		return -EOPNOTSUPP;
	}

	mutex_lock(&efx->reflash_mutex);

	devlink_flash_update_status_notify(devlink, "Checking update", NULL, 0, 0);

	if (efx->type->flash_auto_partition) {
		/* NIC wants entire FW file including headers;
		 * FW will validate 'subtype' if there is one
		 */
		type = NVRAM_PARTITION_TYPE_AUTO;
		data = fw->data;
		data_size = fw->size;
	} else {
		rc = efx_reflash_parse_firmware_data(fw, &type, &data_subtype, &data,
						     &data_size);
		if (rc) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Firmware image validation check failed");
			goto out_unlock;
		}

		rc = efx_mcdi_nvram_metadata(efx, type, &subtype, NULL, NULL, 0);
		if (rc) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Metadata query for NVRAM partition %#x failed",
					       type);
			goto out_unlock;
		}

		if (subtype != data_subtype) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Firmware image is not appropriate for this adapter");
			rc = -EINVAL;
			goto out_unlock;
		}
	}

	rc = efx_mcdi_nvram_info(efx, type, &size, &erase_align, &write_align,
				 &protected);
	if (rc) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Info query for NVRAM partition %#x failed",
				       type);
		goto out_unlock;
	}

	if (protected) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "NVRAM partition %#x is protected",
				       type);
		rc = -EPERM;
		goto out_unlock;
	}

	if (write_align == 0) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "NVRAM partition %#x is not writable",
				       type);
		rc = -EACCES;
		goto out_unlock;
	}

	if (erase_align != 0 && size % erase_align) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "NVRAM partition %#x has a bad partition table entry, can't erase it",
				       type);
		rc = -EACCES;
		goto out_unlock;
	}

	if (data_size > size) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Firmware image is too big for NVRAM partition %#x",
				       type);
		rc = -EFBIG;
		goto out_unlock;
	}

	devlink_flash_update_status_notify(devlink, "Starting update", NULL, 0, 0);

	rc = efx_mcdi_nvram_update_start(efx, type);
	if (rc) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Update start request for NVRAM partition %#x failed",
				       type);
		goto out_unlock;
	}

	rc = efx_reflash_erase_partition(efx, extack, devlink, type, size,
					 erase_align);
	if (rc)
		goto out_update_finish;

	rc = efx_reflash_write_partition(efx, extack, devlink, type, data,
					 data_size, write_align);
	if (rc)
		goto out_update_finish;

	devlink_flash_update_timeout_notify(devlink, "Finishing update", NULL,
					    EFX_DEVLINK_UPDATE_FINISH_TIMEOUT);

out_update_finish:
	if (rc)
		/* Don't obscure the return code from an earlier failure */
		efx_mcdi_nvram_update_finish(efx, type, EFX_UPDATE_FINISH_ABORT);
	else
		rc = efx_mcdi_nvram_update_finish_polled(efx, type);
out_unlock:
	mutex_unlock(&efx->reflash_mutex);
	devlink_flash_update_status_notify(devlink, rc ? "Update failed" :
							 "Update complete",
					   NULL, 0, 0);
	return rc;
}
