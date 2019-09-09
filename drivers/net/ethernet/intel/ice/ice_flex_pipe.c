// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Intel Corporation. */

#include "ice_common.h"
#include "ice_flex_pipe.h"

/**
 * ice_find_buf_table
 * @ice_seg: pointer to the ice segment
 *
 * Returns the address of the buffer table within the ice segment.
 */
static struct ice_buf_table *ice_find_buf_table(struct ice_seg *ice_seg)
{
	struct ice_nvm_table *nvms;

	nvms = (struct ice_nvm_table *)
		(ice_seg->device_table +
		 le32_to_cpu(ice_seg->device_table_count));

	return (__force struct ice_buf_table *)
		(nvms->vers + le32_to_cpu(nvms->table_count));
}

/**
 * ice_acquire_global_cfg_lock
 * @hw: pointer to the HW structure
 * @access: access type (read or write)
 *
 * This function will request ownership of the global config lock for reading
 * or writing of the package. When attempting to obtain write access, the
 * caller must check for the following two return values:
 *
 * ICE_SUCCESS        - Means the caller has acquired the global config lock
 *                      and can perform writing of the package.
 * ICE_ERR_AQ_NO_WORK - Indicates another driver has already written the
 *                      package or has found that no update was necessary; in
 *                      this case, the caller can just skip performing any
 *                      update of the package.
 */
static enum ice_status
ice_acquire_global_cfg_lock(struct ice_hw *hw,
			    enum ice_aq_res_access_type access)
{
	enum ice_status status;

	status = ice_acquire_res(hw, ICE_GLOBAL_CFG_LOCK_RES_ID, access,
				 ICE_GLOBAL_CFG_LOCK_TIMEOUT);

	if (!status)
		mutex_lock(&ice_global_cfg_lock_sw);
	else if (status == ICE_ERR_AQ_NO_WORK)
		ice_debug(hw, ICE_DBG_PKG,
			  "Global config lock: No work to do\n");

	return status;
}

/**
 * ice_release_global_cfg_lock
 * @hw: pointer to the HW structure
 *
 * This function will release the global config lock.
 */
static void ice_release_global_cfg_lock(struct ice_hw *hw)
{
	mutex_unlock(&ice_global_cfg_lock_sw);
	ice_release_res(hw, ICE_GLOBAL_CFG_LOCK_RES_ID);
}

/**
 * ice_aq_download_pkg
 * @hw: pointer to the hardware structure
 * @pkg_buf: the package buffer to transfer
 * @buf_size: the size of the package buffer
 * @last_buf: last buffer indicator
 * @error_offset: returns error offset
 * @error_info: returns error information
 * @cd: pointer to command details structure or NULL
 *
 * Download Package (0x0C40)
 */
static enum ice_status
ice_aq_download_pkg(struct ice_hw *hw, struct ice_buf_hdr *pkg_buf,
		    u16 buf_size, bool last_buf, u32 *error_offset,
		    u32 *error_info, struct ice_sq_cd *cd)
{
	struct ice_aqc_download_pkg *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	if (error_offset)
		*error_offset = 0;
	if (error_info)
		*error_info = 0;

	cmd = &desc.params.download_pkg;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_download_pkg);
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	if (last_buf)
		cmd->flags |= ICE_AQC_DOWNLOAD_PKG_LAST_BUF;

	status = ice_aq_send_cmd(hw, &desc, pkg_buf, buf_size, cd);
	if (status == ICE_ERR_AQ_ERROR) {
		/* Read error from buffer only when the FW returned an error */
		struct ice_aqc_download_pkg_resp *resp;

		resp = (struct ice_aqc_download_pkg_resp *)pkg_buf;
		if (error_offset)
			*error_offset = le32_to_cpu(resp->error_offset);
		if (error_info)
			*error_info = le32_to_cpu(resp->error_info);
	}

	return status;
}

/**
 * ice_find_seg_in_pkg
 * @hw: pointer to the hardware structure
 * @seg_type: the segment type to search for (i.e., SEGMENT_TYPE_CPK)
 * @pkg_hdr: pointer to the package header to be searched
 *
 * This function searches a package file for a particular segment type. On
 * success it returns a pointer to the segment header, otherwise it will
 * return NULL.
 */
static struct ice_generic_seg_hdr *
ice_find_seg_in_pkg(struct ice_hw *hw, u32 seg_type,
		    struct ice_pkg_hdr *pkg_hdr)
{
	u32 i;

	ice_debug(hw, ICE_DBG_PKG, "Package format version: %d.%d.%d.%d\n",
		  pkg_hdr->format_ver.major, pkg_hdr->format_ver.minor,
		  pkg_hdr->format_ver.update, pkg_hdr->format_ver.draft);

	/* Search all package segments for the requested segment type */
	for (i = 0; i < le32_to_cpu(pkg_hdr->seg_count); i++) {
		struct ice_generic_seg_hdr *seg;

		seg = (struct ice_generic_seg_hdr *)
			((u8 *)pkg_hdr + le32_to_cpu(pkg_hdr->seg_offset[i]));

		if (le32_to_cpu(seg->seg_type) == seg_type)
			return seg;
	}

	return NULL;
}

/**
 * ice_dwnld_cfg_bufs
 * @hw: pointer to the hardware structure
 * @bufs: pointer to an array of buffers
 * @count: the number of buffers in the array
 *
 * Obtains global config lock and downloads the package configuration buffers
 * to the firmware. Metadata buffers are skipped, and the first metadata buffer
 * found indicates that the rest of the buffers are all metadata buffers.
 */
static enum ice_status
ice_dwnld_cfg_bufs(struct ice_hw *hw, struct ice_buf *bufs, u32 count)
{
	enum ice_status status;
	struct ice_buf_hdr *bh;
	u32 offset, info, i;

	if (!bufs || !count)
		return ICE_ERR_PARAM;

	/* If the first buffer's first section has its metadata bit set
	 * then there are no buffers to be downloaded, and the operation is
	 * considered a success.
	 */
	bh = (struct ice_buf_hdr *)bufs;
	if (le32_to_cpu(bh->section_entry[0].type) & ICE_METADATA_BUF)
		return 0;

	/* reset pkg_dwnld_status in case this function is called in the
	 * reset/rebuild flow
	 */
	hw->pkg_dwnld_status = ICE_AQ_RC_OK;

	status = ice_acquire_global_cfg_lock(hw, ICE_RES_WRITE);
	if (status) {
		if (status == ICE_ERR_AQ_NO_WORK)
			hw->pkg_dwnld_status = ICE_AQ_RC_EEXIST;
		else
			hw->pkg_dwnld_status = hw->adminq.sq_last_status;
		return status;
	}

	for (i = 0; i < count; i++) {
		bool last = ((i + 1) == count);

		if (!last) {
			/* check next buffer for metadata flag */
			bh = (struct ice_buf_hdr *)(bufs + i + 1);

			/* A set metadata flag in the next buffer will signal
			 * that the current buffer will be the last buffer
			 * downloaded
			 */
			if (le16_to_cpu(bh->section_count))
				if (le32_to_cpu(bh->section_entry[0].type) &
				    ICE_METADATA_BUF)
					last = true;
		}

		bh = (struct ice_buf_hdr *)(bufs + i);

		status = ice_aq_download_pkg(hw, bh, ICE_PKG_BUF_SIZE, last,
					     &offset, &info, NULL);

		/* Save AQ status from download package */
		hw->pkg_dwnld_status = hw->adminq.sq_last_status;
		if (status) {
			ice_debug(hw, ICE_DBG_PKG,
				  "Pkg download failed: err %d off %d inf %d\n",
				  status, offset, info);

			break;
		}

		if (last)
			break;
	}

	ice_release_global_cfg_lock(hw);

	return status;
}

/**
 * ice_aq_get_pkg_info_list
 * @hw: pointer to the hardware structure
 * @pkg_info: the buffer which will receive the information list
 * @buf_size: the size of the pkg_info information buffer
 * @cd: pointer to command details structure or NULL
 *
 * Get Package Info List (0x0C43)
 */
static enum ice_status
ice_aq_get_pkg_info_list(struct ice_hw *hw,
			 struct ice_aqc_get_pkg_info_resp *pkg_info,
			 u16 buf_size, struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_pkg_info_list);

	return ice_aq_send_cmd(hw, &desc, pkg_info, buf_size, cd);
}

/**
 * ice_download_pkg
 * @hw: pointer to the hardware structure
 * @ice_seg: pointer to the segment of the package to be downloaded
 *
 * Handles the download of a complete package.
 */
static enum ice_status
ice_download_pkg(struct ice_hw *hw, struct ice_seg *ice_seg)
{
	struct ice_buf_table *ice_buf_tbl;

	ice_debug(hw, ICE_DBG_PKG, "Segment version: %d.%d.%d.%d\n",
		  ice_seg->hdr.seg_ver.major, ice_seg->hdr.seg_ver.minor,
		  ice_seg->hdr.seg_ver.update, ice_seg->hdr.seg_ver.draft);

	ice_debug(hw, ICE_DBG_PKG, "Seg: type 0x%X, size %d, name %s\n",
		  le32_to_cpu(ice_seg->hdr.seg_type),
		  le32_to_cpu(ice_seg->hdr.seg_size), ice_seg->hdr.seg_name);

	ice_buf_tbl = ice_find_buf_table(ice_seg);

	ice_debug(hw, ICE_DBG_PKG, "Seg buf count: %d\n",
		  le32_to_cpu(ice_buf_tbl->buf_count));

	return ice_dwnld_cfg_bufs(hw, ice_buf_tbl->buf_array,
				  le32_to_cpu(ice_buf_tbl->buf_count));
}

/**
 * ice_init_pkg_info
 * @hw: pointer to the hardware structure
 * @pkg_hdr: pointer to the driver's package hdr
 *
 * Saves off the package details into the HW structure.
 */
static enum ice_status
ice_init_pkg_info(struct ice_hw *hw, struct ice_pkg_hdr *pkg_hdr)
{
	struct ice_global_metadata_seg *meta_seg;
	struct ice_generic_seg_hdr *seg_hdr;

	if (!pkg_hdr)
		return ICE_ERR_PARAM;

	meta_seg = (struct ice_global_metadata_seg *)
		   ice_find_seg_in_pkg(hw, SEGMENT_TYPE_METADATA, pkg_hdr);
	if (meta_seg) {
		hw->pkg_ver = meta_seg->pkg_ver;
		memcpy(hw->pkg_name, meta_seg->pkg_name, sizeof(hw->pkg_name));

		ice_debug(hw, ICE_DBG_PKG, "Pkg: %d.%d.%d.%d, %s\n",
			  meta_seg->pkg_ver.major, meta_seg->pkg_ver.minor,
			  meta_seg->pkg_ver.update, meta_seg->pkg_ver.draft,
			  meta_seg->pkg_name);
	} else {
		ice_debug(hw, ICE_DBG_INIT,
			  "Did not find metadata segment in driver package\n");
		return ICE_ERR_CFG;
	}

	seg_hdr = ice_find_seg_in_pkg(hw, SEGMENT_TYPE_ICE, pkg_hdr);
	if (seg_hdr) {
		hw->ice_pkg_ver = seg_hdr->seg_ver;
		memcpy(hw->ice_pkg_name, seg_hdr->seg_name,
		       sizeof(hw->ice_pkg_name));

		ice_debug(hw, ICE_DBG_PKG, "Ice Pkg: %d.%d.%d.%d, %s\n",
			  seg_hdr->seg_ver.major, seg_hdr->seg_ver.minor,
			  seg_hdr->seg_ver.update, seg_hdr->seg_ver.draft,
			  seg_hdr->seg_name);
	} else {
		ice_debug(hw, ICE_DBG_INIT,
			  "Did not find ice segment in driver package\n");
		return ICE_ERR_CFG;
	}

	return 0;
}

/**
 * ice_get_pkg_info
 * @hw: pointer to the hardware structure
 *
 * Store details of the package currently loaded in HW into the HW structure.
 */
static enum ice_status ice_get_pkg_info(struct ice_hw *hw)
{
	struct ice_aqc_get_pkg_info_resp *pkg_info;
	enum ice_status status;
	u16 size;
	u32 i;

	size = sizeof(*pkg_info) + (sizeof(pkg_info->pkg_info[0]) *
				    (ICE_PKG_CNT - 1));
	pkg_info = kzalloc(size, GFP_KERNEL);
	if (!pkg_info)
		return ICE_ERR_NO_MEMORY;

	status = ice_aq_get_pkg_info_list(hw, pkg_info, size, NULL);
	if (status)
		goto init_pkg_free_alloc;

	for (i = 0; i < le32_to_cpu(pkg_info->count); i++) {
#define ICE_PKG_FLAG_COUNT	4
		char flags[ICE_PKG_FLAG_COUNT + 1] = { 0 };
		u8 place = 0;

		if (pkg_info->pkg_info[i].is_active) {
			flags[place++] = 'A';
			hw->active_pkg_ver = pkg_info->pkg_info[i].ver;
			memcpy(hw->active_pkg_name,
			       pkg_info->pkg_info[i].name,
			       sizeof(hw->active_pkg_name));
			hw->active_pkg_in_nvm = pkg_info->pkg_info[i].is_in_nvm;
		}
		if (pkg_info->pkg_info[i].is_active_at_boot)
			flags[place++] = 'B';
		if (pkg_info->pkg_info[i].is_modified)
			flags[place++] = 'M';
		if (pkg_info->pkg_info[i].is_in_nvm)
			flags[place++] = 'N';

		ice_debug(hw, ICE_DBG_PKG, "Pkg[%d]: %d.%d.%d.%d,%s,%s\n",
			  i, pkg_info->pkg_info[i].ver.major,
			  pkg_info->pkg_info[i].ver.minor,
			  pkg_info->pkg_info[i].ver.update,
			  pkg_info->pkg_info[i].ver.draft,
			  pkg_info->pkg_info[i].name, flags);
	}

init_pkg_free_alloc:
	kfree(pkg_info);

	return status;
}

/**
 * ice_verify_pkg - verify package
 * @pkg: pointer to the package buffer
 * @len: size of the package buffer
 *
 * Verifies various attributes of the package file, including length, format
 * version, and the requirement of at least one segment.
 */
static enum ice_status ice_verify_pkg(struct ice_pkg_hdr *pkg, u32 len)
{
	u32 seg_count;
	u32 i;

	if (len < sizeof(*pkg))
		return ICE_ERR_BUF_TOO_SHORT;

	if (pkg->format_ver.major != ICE_PKG_FMT_VER_MAJ ||
	    pkg->format_ver.minor != ICE_PKG_FMT_VER_MNR ||
	    pkg->format_ver.update != ICE_PKG_FMT_VER_UPD ||
	    pkg->format_ver.draft != ICE_PKG_FMT_VER_DFT)
		return ICE_ERR_CFG;

	/* pkg must have at least one segment */
	seg_count = le32_to_cpu(pkg->seg_count);
	if (seg_count < 1)
		return ICE_ERR_CFG;

	/* make sure segment array fits in package length */
	if (len < sizeof(*pkg) + ((seg_count - 1) * sizeof(pkg->seg_offset)))
		return ICE_ERR_BUF_TOO_SHORT;

	/* all segments must fit within length */
	for (i = 0; i < seg_count; i++) {
		u32 off = le32_to_cpu(pkg->seg_offset[i]);
		struct ice_generic_seg_hdr *seg;

		/* segment header must fit */
		if (len < off + sizeof(*seg))
			return ICE_ERR_BUF_TOO_SHORT;

		seg = (struct ice_generic_seg_hdr *)((u8 *)pkg + off);

		/* segment body must fit */
		if (len < off + le32_to_cpu(seg->seg_size))
			return ICE_ERR_BUF_TOO_SHORT;
	}

	return 0;
}

/**
 * ice_free_seg - free package segment pointer
 * @hw: pointer to the hardware structure
 *
 * Frees the package segment pointer in the proper manner, depending on if the
 * segment was allocated or just the passed in pointer was stored.
 */
void ice_free_seg(struct ice_hw *hw)
{
	if (hw->pkg_copy) {
		devm_kfree(ice_hw_to_dev(hw), hw->pkg_copy);
		hw->pkg_copy = NULL;
		hw->pkg_size = 0;
	}
	hw->seg = NULL;
}

/**
 * ice_chk_pkg_version - check package version for compatibility with driver
 * @pkg_ver: pointer to a version structure to check
 *
 * Check to make sure that the package about to be downloaded is compatible with
 * the driver. To be compatible, the major and minor components of the package
 * version must match our ICE_PKG_SUPP_VER_MAJ and ICE_PKG_SUPP_VER_MNR
 * definitions.
 */
static enum ice_status ice_chk_pkg_version(struct ice_pkg_ver *pkg_ver)
{
	if (pkg_ver->major != ICE_PKG_SUPP_VER_MAJ ||
	    pkg_ver->minor != ICE_PKG_SUPP_VER_MNR)
		return ICE_ERR_NOT_SUPPORTED;

	return 0;
}

/**
 * ice_init_pkg - initialize/download package
 * @hw: pointer to the hardware structure
 * @buf: pointer to the package buffer
 * @len: size of the package buffer
 *
 * This function initializes a package. The package contains HW tables
 * required to do packet processing. First, the function extracts package
 * information such as version. Then it finds the ice configuration segment
 * within the package; this function then saves a copy of the segment pointer
 * within the supplied package buffer. Next, the function will cache any hints
 * from the package, followed by downloading the package itself. Note, that if
 * a previous PF driver has already downloaded the package successfully, then
 * the current driver will not have to download the package again.
 *
 * The local package contents will be used to query default behavior and to
 * update specific sections of the HW's version of the package (e.g. to update
 * the parse graph to understand new protocols).
 *
 * This function stores a pointer to the package buffer memory, and it is
 * expected that the supplied buffer will not be freed immediately. If the
 * package buffer needs to be freed, such as when read from a file, use
 * ice_copy_and_init_pkg() instead of directly calling ice_init_pkg() in this
 * case.
 */
enum ice_status ice_init_pkg(struct ice_hw *hw, u8 *buf, u32 len)
{
	struct ice_pkg_hdr *pkg;
	enum ice_status status;
	struct ice_seg *seg;

	if (!buf || !len)
		return ICE_ERR_PARAM;

	pkg = (struct ice_pkg_hdr *)buf;
	status = ice_verify_pkg(pkg, len);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "failed to verify pkg (err: %d)\n",
			  status);
		return status;
	}

	/* initialize package info */
	status = ice_init_pkg_info(hw, pkg);
	if (status)
		return status;

	/* before downloading the package, check package version for
	 * compatibility with driver
	 */
	status = ice_chk_pkg_version(&hw->pkg_ver);
	if (status)
		return status;

	/* find segment in given package */
	seg = (struct ice_seg *)ice_find_seg_in_pkg(hw, SEGMENT_TYPE_ICE, pkg);
	if (!seg) {
		ice_debug(hw, ICE_DBG_INIT, "no ice segment in package.\n");
		return ICE_ERR_CFG;
	}

	/* download package */
	status = ice_download_pkg(hw, seg);
	if (status == ICE_ERR_AQ_NO_WORK) {
		ice_debug(hw, ICE_DBG_INIT,
			  "package previously loaded - no work.\n");
		status = 0;
	}

	/* Get information on the package currently loaded in HW, then make sure
	 * the driver is compatible with this version.
	 */
	if (!status) {
		status = ice_get_pkg_info(hw);
		if (!status)
			status = ice_chk_pkg_version(&hw->active_pkg_ver);
	}

	if (status)
		ice_debug(hw, ICE_DBG_INIT, "package load failed, %d\n",
			  status);

	return status;
}

/**
 * ice_copy_and_init_pkg - initialize/download a copy of the package
 * @hw: pointer to the hardware structure
 * @buf: pointer to the package buffer
 * @len: size of the package buffer
 *
 * This function copies the package buffer, and then calls ice_init_pkg() to
 * initialize the copied package contents.
 *
 * The copying is necessary if the package buffer supplied is constant, or if
 * the memory may disappear shortly after calling this function.
 *
 * If the package buffer resides in the data segment and can be modified, the
 * caller is free to use ice_init_pkg() instead of ice_copy_and_init_pkg().
 *
 * However, if the package buffer needs to be copied first, such as when being
 * read from a file, the caller should use ice_copy_and_init_pkg().
 *
 * This function will first copy the package buffer, before calling
 * ice_init_pkg(). The caller is free to immediately destroy the original
 * package buffer, as the new copy will be managed by this function and
 * related routines.
 */
enum ice_status ice_copy_and_init_pkg(struct ice_hw *hw, const u8 *buf, u32 len)
{
	enum ice_status status;
	u8 *buf_copy;

	if (!buf || !len)
		return ICE_ERR_PARAM;

	buf_copy = devm_kmemdup(ice_hw_to_dev(hw), buf, len, GFP_KERNEL);

	status = ice_init_pkg(hw, buf_copy, len);
	if (status) {
		/* Free the copy, since we failed to initialize the package */
		devm_kfree(ice_hw_to_dev(hw), buf_copy);
	} else {
		/* Track the copied pkg so we can free it later */
		hw->pkg_copy = buf_copy;
		hw->pkg_size = len;
	}

	return status;
}
