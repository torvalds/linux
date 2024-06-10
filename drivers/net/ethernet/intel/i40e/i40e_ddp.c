// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#include <linux/firmware.h>
#include "i40e.h"

#define I40_DDP_FLASH_REGION		100
#define I40E_PROFILE_INFO_SIZE		48
#define I40E_MAX_PROFILE_NUM		16
#define I40E_PROFILE_LIST_SIZE		\
	(I40E_PROFILE_INFO_SIZE * I40E_MAX_PROFILE_NUM + 4)
#define I40E_DDP_PROFILE_PATH		"intel/i40e/ddp/"
#define I40E_DDP_PROFILE_NAME_MAX	64

struct i40e_ddp_profile_list {
	u32 p_count;
	struct i40e_profile_info p_info[];
};

struct i40e_ddp_old_profile_list {
	struct list_head list;
	size_t old_ddp_size;
	u8 old_ddp_buf[];
};

/**
 * i40e_ddp_profiles_eq - checks if DDP profiles are the equivalent
 * @a: new profile info
 * @b: old profile info
 *
 * checks if DDP profiles are the equivalent.
 * Returns true if profiles are the same.
 **/
static bool i40e_ddp_profiles_eq(struct i40e_profile_info *a,
				 struct i40e_profile_info *b)
{
	return a->track_id == b->track_id &&
		!memcmp(&a->version, &b->version, sizeof(a->version)) &&
		!memcmp(&a->name, &b->name, I40E_DDP_NAME_SIZE);
}

/**
 * i40e_ddp_does_profile_exist - checks if DDP profile loaded already
 * @hw: HW data structure
 * @pinfo: DDP profile information structure
 *
 * checks if DDP profile loaded already.
 * Returns >0 if the profile exists.
 * Returns  0 if the profile is absent.
 * Returns <0 if error.
 **/
static int i40e_ddp_does_profile_exist(struct i40e_hw *hw,
				       struct i40e_profile_info *pinfo)
{
	struct i40e_ddp_profile_list *profile_list;
	u8 buff[I40E_PROFILE_LIST_SIZE];
	int status;
	int i;

	status = i40e_aq_get_ddp_list(hw, buff, I40E_PROFILE_LIST_SIZE, 0,
				      NULL);
	if (status)
		return -1;

	profile_list = (struct i40e_ddp_profile_list *)buff;
	for (i = 0; i < profile_list->p_count; i++) {
		if (i40e_ddp_profiles_eq(pinfo, &profile_list->p_info[i]))
			return 1;
	}
	return 0;
}

/**
 * i40e_ddp_profiles_overlap - checks if DDP profiles overlap.
 * @new: new profile info
 * @old: old profile info
 *
 * checks if DDP profiles overlap.
 * Returns true if profiles are overlap.
 **/
static bool i40e_ddp_profiles_overlap(struct i40e_profile_info *new,
				      struct i40e_profile_info *old)
{
	unsigned int group_id_old = FIELD_GET(0x00FF0000, old->track_id);
	unsigned int group_id_new = FIELD_GET(0x00FF0000, new->track_id);

	/* 0x00 group must be only the first */
	if (group_id_new == 0)
		return true;
	/* 0xFF group is compatible with anything else */
	if (group_id_new == 0xFF || group_id_old == 0xFF)
		return false;
	/* otherwise only profiles from the same group are compatible*/
	return group_id_old != group_id_new;
}

/**
 * i40e_ddp_does_profile_overlap - checks if DDP overlaps with existing one.
 * @hw: HW data structure
 * @pinfo: DDP profile information structure
 *
 * checks if DDP profile overlaps with existing one.
 * Returns >0 if the profile overlaps.
 * Returns  0 if the profile is ok.
 * Returns <0 if error.
 **/
static int i40e_ddp_does_profile_overlap(struct i40e_hw *hw,
					 struct i40e_profile_info *pinfo)
{
	struct i40e_ddp_profile_list *profile_list;
	u8 buff[I40E_PROFILE_LIST_SIZE];
	int status;
	int i;

	status = i40e_aq_get_ddp_list(hw, buff, I40E_PROFILE_LIST_SIZE, 0,
				      NULL);
	if (status)
		return -EIO;

	profile_list = (struct i40e_ddp_profile_list *)buff;
	for (i = 0; i < profile_list->p_count; i++) {
		if (i40e_ddp_profiles_overlap(pinfo,
					      &profile_list->p_info[i]))
			return 1;
	}
	return 0;
}

/**
 * i40e_add_pinfo
 * @hw: pointer to the hardware structure
 * @profile: pointer to the profile segment of the package
 * @profile_info_sec: buffer for information section
 * @track_id: package tracking id
 *
 * Register a profile to the list of loaded profiles.
 */
static int
i40e_add_pinfo(struct i40e_hw *hw, struct i40e_profile_segment *profile,
	       u8 *profile_info_sec, u32 track_id)
{
	struct i40e_profile_section_header *sec;
	struct i40e_profile_info *pinfo;
	u32 offset = 0, info = 0;
	int status;

	sec = (struct i40e_profile_section_header *)profile_info_sec;
	sec->tbl_size = 1;
	sec->data_end = sizeof(struct i40e_profile_section_header) +
			sizeof(struct i40e_profile_info);
	sec->section.type = SECTION_TYPE_INFO;
	sec->section.offset = sizeof(struct i40e_profile_section_header);
	sec->section.size = sizeof(struct i40e_profile_info);
	pinfo = (struct i40e_profile_info *)(profile_info_sec +
					     sec->section.offset);
	pinfo->track_id = track_id;
	pinfo->version = profile->version;
	pinfo->op = I40E_DDP_ADD_TRACKID;

	/* Clear reserved field */
	memset(pinfo->reserved, 0, sizeof(pinfo->reserved));
	memcpy(pinfo->name, profile->name, I40E_DDP_NAME_SIZE);

	status = i40e_aq_write_ddp(hw, (void *)sec, sec->data_end,
				   track_id, &offset, &info, NULL);
	return status;
}

/**
 * i40e_del_pinfo - delete DDP profile info from NIC
 * @hw: HW data structure
 * @profile: DDP profile segment to be deleted
 * @profile_info_sec: DDP profile section header
 * @track_id: track ID of the profile for deletion
 *
 * Removes DDP profile from the NIC.
 **/
static int
i40e_del_pinfo(struct i40e_hw *hw, struct i40e_profile_segment *profile,
	       u8 *profile_info_sec, u32 track_id)
{
	struct i40e_profile_section_header *sec;
	struct i40e_profile_info *pinfo;
	u32 offset = 0, info = 0;
	int status;

	sec = (struct i40e_profile_section_header *)profile_info_sec;
	sec->tbl_size = 1;
	sec->data_end = sizeof(struct i40e_profile_section_header) +
			sizeof(struct i40e_profile_info);
	sec->section.type = SECTION_TYPE_INFO;
	sec->section.offset = sizeof(struct i40e_profile_section_header);
	sec->section.size = sizeof(struct i40e_profile_info);
	pinfo = (struct i40e_profile_info *)(profile_info_sec +
					     sec->section.offset);
	pinfo->track_id = track_id;
	pinfo->version = profile->version;
	pinfo->op = I40E_DDP_REMOVE_TRACKID;

	/* Clear reserved field */
	memset(pinfo->reserved, 0, sizeof(pinfo->reserved));
	memcpy(pinfo->name, profile->name, I40E_DDP_NAME_SIZE);

	status = i40e_aq_write_ddp(hw, (void *)sec, sec->data_end,
				   track_id, &offset, &info, NULL);
	return status;
}

/**
 * i40e_ddp_is_pkg_hdr_valid - performs basic pkg header integrity checks
 * @netdev: net device structure (for logging purposes)
 * @pkg_hdr: pointer to package header
 * @size_huge: size of the whole DDP profile package in size_t
 *
 * Checks correctness of pkg header: Version, size too big/small, and
 * all segment offsets alignment and boundaries. This function lets
 * reject non DDP profile file to be loaded by administrator mistake.
 **/
static bool i40e_ddp_is_pkg_hdr_valid(struct net_device *netdev,
				      struct i40e_package_header *pkg_hdr,
				      size_t size_huge)
{
	u32 size = 0xFFFFFFFFU & size_huge;
	u32 pkg_hdr_size;
	u32 segment;

	if (!pkg_hdr)
		return false;

	if (pkg_hdr->version.major > 0) {
		struct i40e_ddp_version ver = pkg_hdr->version;

		netdev_err(netdev, "Unsupported DDP profile version %u.%u.%u.%u",
			   ver.major, ver.minor, ver.update, ver.draft);
		return false;
	}
	if (size_huge > size) {
		netdev_err(netdev, "Invalid DDP profile - size is bigger than 4G");
		return false;
	}
	if (size < (sizeof(struct i40e_package_header) + sizeof(u32) +
		sizeof(struct i40e_metadata_segment) + sizeof(u32) * 2)) {
		netdev_err(netdev, "Invalid DDP profile - size is too small.");
		return false;
	}

	pkg_hdr_size = sizeof(u32) * (pkg_hdr->segment_count + 2U);
	if (size < pkg_hdr_size) {
		netdev_err(netdev, "Invalid DDP profile - too many segments");
		return false;
	}
	for (segment = 0; segment < pkg_hdr->segment_count; ++segment) {
		u32 offset = pkg_hdr->segment_offset[segment];

		if (0xFU & offset) {
			netdev_err(netdev,
				   "Invalid DDP profile %u segment alignment",
				   segment);
			return false;
		}
		if (pkg_hdr_size > offset || offset >= size) {
			netdev_err(netdev,
				   "Invalid DDP profile %u segment offset",
				   segment);
			return false;
		}
	}

	return true;
}

/**
 * i40e_ddp_load - performs DDP loading
 * @netdev: net device structure
 * @data: buffer containing recipe file
 * @size: size of the buffer
 * @is_add: true when loading profile, false when rolling back the previous one
 *
 * Checks correctness and loads DDP profile to the NIC. The function is
 * also used for rolling back previously loaded profile.
 **/
static int i40e_ddp_load(struct net_device *netdev, const u8 *data, size_t size,
			 bool is_add)
{
	u8 profile_info_sec[sizeof(struct i40e_profile_section_header) +
			    sizeof(struct i40e_profile_info)];
	struct i40e_metadata_segment *metadata_hdr;
	struct i40e_profile_segment *profile_hdr;
	struct i40e_profile_info pinfo;
	struct i40e_package_header *pkg_hdr;
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u32 track_id;
	int istatus;
	int status;

	pkg_hdr = (struct i40e_package_header *)data;
	if (!i40e_ddp_is_pkg_hdr_valid(netdev, pkg_hdr, size))
		return -EINVAL;

	if (size < (sizeof(struct i40e_package_header) + sizeof(u32) +
		    sizeof(struct i40e_metadata_segment) + sizeof(u32) * 2)) {
		netdev_err(netdev, "Invalid DDP recipe size.");
		return -EINVAL;
	}

	/* Find beginning of segment data in buffer */
	metadata_hdr = (struct i40e_metadata_segment *)
		i40e_find_segment_in_package(SEGMENT_TYPE_METADATA, pkg_hdr);
	if (!metadata_hdr) {
		netdev_err(netdev, "Failed to find metadata segment in DDP recipe.");
		return -EINVAL;
	}

	track_id = metadata_hdr->track_id;
	profile_hdr = (struct i40e_profile_segment *)
		i40e_find_segment_in_package(SEGMENT_TYPE_I40E, pkg_hdr);
	if (!profile_hdr) {
		netdev_err(netdev, "Failed to find profile segment in DDP recipe.");
		return -EINVAL;
	}

	pinfo.track_id = track_id;
	pinfo.version = profile_hdr->version;
	if (is_add)
		pinfo.op = I40E_DDP_ADD_TRACKID;
	else
		pinfo.op = I40E_DDP_REMOVE_TRACKID;

	memcpy(pinfo.name, profile_hdr->name, I40E_DDP_NAME_SIZE);

	/* Check if profile data already exists*/
	istatus = i40e_ddp_does_profile_exist(&pf->hw, &pinfo);
	if (istatus < 0) {
		netdev_err(netdev, "Failed to fetch loaded profiles.");
		return istatus;
	}
	if (is_add) {
		if (istatus > 0) {
			netdev_err(netdev, "DDP profile already loaded.");
			return -EINVAL;
		}
		istatus = i40e_ddp_does_profile_overlap(&pf->hw, &pinfo);
		if (istatus < 0) {
			netdev_err(netdev, "Failed to fetch loaded profiles.");
			return istatus;
		}
		if (istatus > 0) {
			netdev_err(netdev, "DDP profile overlaps with existing one.");
			return -EINVAL;
		}
	} else {
		if (istatus == 0) {
			netdev_err(netdev,
				   "DDP profile for deletion does not exist.");
			return -EINVAL;
		}
	}

	/* Load profile data */
	if (is_add) {
		status = i40e_write_profile(&pf->hw, profile_hdr, track_id);
		if (status) {
			if (status == -ENODEV) {
				netdev_err(netdev,
					   "Profile is not supported by the device.");
				return -EPERM;
			}
			netdev_err(netdev, "Failed to write DDP profile.");
			return -EIO;
		}
	} else {
		status = i40e_rollback_profile(&pf->hw, profile_hdr, track_id);
		if (status) {
			netdev_err(netdev, "Failed to remove DDP profile.");
			return -EIO;
		}
	}

	/* Add/remove profile to/from profile list in FW */
	if (is_add) {
		status = i40e_add_pinfo(&pf->hw, profile_hdr, profile_info_sec,
					track_id);
		if (status) {
			netdev_err(netdev, "Failed to add DDP profile info.");
			return -EIO;
		}
	} else {
		status = i40e_del_pinfo(&pf->hw, profile_hdr, profile_info_sec,
					track_id);
		if (status) {
			netdev_err(netdev, "Failed to restore DDP profile info.");
			return -EIO;
		}
	}

	return 0;
}

/**
 * i40e_ddp_restore - restore previously loaded profile and remove from list
 * @pf: PF data struct
 *
 * Restores previously loaded profile stored on the list in driver memory.
 * After rolling back removes entry from the list.
 **/
static int i40e_ddp_restore(struct i40e_pf *pf)
{
	struct i40e_vsi *vsi = i40e_pf_get_main_vsi(pf);
	struct net_device *netdev = vsi->netdev;
	struct i40e_ddp_old_profile_list *entry;
	int status = 0;

	if (!list_empty(&pf->ddp_old_prof)) {
		entry = list_first_entry(&pf->ddp_old_prof,
					 struct i40e_ddp_old_profile_list,
					 list);
		status = i40e_ddp_load(netdev, entry->old_ddp_buf,
				       entry->old_ddp_size, false);
		list_del(&entry->list);
		kfree(entry);
	}
	return status;
}

/**
 * i40e_ddp_flash - callback function for ethtool flash feature
 * @netdev: net device structure
 * @flash: kernel flash structure
 *
 * Ethtool callback function used for loading and unloading DDP profiles.
 **/
int i40e_ddp_flash(struct net_device *netdev, struct ethtool_flash *flash)
{
	const struct firmware *ddp_config;
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	int status = 0;

	/* Check for valid region first */
	if (flash->region != I40_DDP_FLASH_REGION) {
		netdev_err(netdev, "Requested firmware region is not recognized by this driver.");
		return -EINVAL;
	}
	if (pf->hw.bus.func != 0) {
		netdev_err(netdev, "Any DDP operation is allowed only on Phy0 NIC interface");
		return -EINVAL;
	}

	/* If the user supplied "-" instead of file name rollback previously
	 * stored profile.
	 */
	if (strncmp(flash->data, "-", 2) != 0) {
		struct i40e_ddp_old_profile_list *list_entry;
		char profile_name[sizeof(I40E_DDP_PROFILE_PATH)
				  + I40E_DDP_PROFILE_NAME_MAX];

		scnprintf(profile_name, sizeof(profile_name), "%s%s",
			  I40E_DDP_PROFILE_PATH, flash->data);

		/* Load DDP recipe. */
		status = request_firmware(&ddp_config, profile_name,
					  &netdev->dev);
		if (status) {
			netdev_err(netdev, "DDP recipe file request failed.");
			return status;
		}

		status = i40e_ddp_load(netdev, ddp_config->data,
				       ddp_config->size, true);

		if (!status) {
			list_entry =
			  kzalloc(sizeof(struct i40e_ddp_old_profile_list) +
				  ddp_config->size, GFP_KERNEL);
			if (!list_entry) {
				netdev_info(netdev, "Failed to allocate memory for previous DDP profile data.");
				netdev_info(netdev, "New profile loaded but roll-back will be impossible.");
			} else {
				memcpy(list_entry->old_ddp_buf,
				       ddp_config->data, ddp_config->size);
				list_entry->old_ddp_size = ddp_config->size;
				list_add(&list_entry->list, &pf->ddp_old_prof);
			}
		}

		release_firmware(ddp_config);
	} else {
		if (!list_empty(&pf->ddp_old_prof)) {
			status = i40e_ddp_restore(pf);
		} else {
			netdev_warn(netdev, "There is no DDP profile to restore.");
			status = -ENOENT;
		}
	}
	return status;
}
