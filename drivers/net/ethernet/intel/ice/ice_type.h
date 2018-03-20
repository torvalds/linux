/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_TYPE_H_
#define _ICE_TYPE_H_

#include "ice_status.h"
#include "ice_hw_autogen.h"
#include "ice_osdep.h"
#include "ice_controlq.h"

/* debug masks - set these bits in hw->debug_mask to control output */
#define ICE_DBG_INIT		BIT_ULL(1)
#define ICE_DBG_NVM		BIT_ULL(7)
#define ICE_DBG_RES		BIT_ULL(17)
#define ICE_DBG_AQ_MSG		BIT_ULL(24)
#define ICE_DBG_AQ_CMD		BIT_ULL(27)

enum ice_aq_res_ids {
	ICE_NVM_RES_ID = 1,
	ICE_SPD_RES_ID,
	ICE_GLOBAL_CFG_LOCK_RES_ID,
	ICE_CHANGE_LOCK_RES_ID
};

enum ice_aq_res_access_type {
	ICE_RES_READ = 1,
	ICE_RES_WRITE
};

/* Various MAC types */
enum ice_mac_type {
	ICE_MAC_UNKNOWN = 0,
	ICE_MAC_GENERIC,
};

/* Various RESET request, These are not tied with HW reset types */
enum ice_reset_req {
	ICE_RESET_PFR	= 0,
	ICE_RESET_CORER	= 1,
	ICE_RESET_GLOBR	= 2,
};

/* Bus parameters */
struct ice_bus_info {
	u16 device;
	u8 func;
};

/* NVM Information */
struct ice_nvm_info {
	u32 eetrack;              /* NVM data version */
	u32 oem_ver;              /* OEM version info */
	u16 sr_words;             /* Shadow RAM size in words */
	u16 ver;                  /* NVM package version */
	bool blank_nvm_mode;      /* is NVM empty (no FW present) */
};

/* Port hardware description */
struct ice_hw {
	u8 __iomem *hw_addr;
	void *back;
	u64 debug_mask;		/* bitmap for debug mask */
	enum ice_mac_type mac_type;

	/* pci info */
	u16 device_id;
	u16 vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_vendor_id;
	u8 revision_id;

	u8 pf_id;		/* device profile info */

	struct ice_bus_info bus;
	struct ice_nvm_info nvm;

	/* Control Queue info */
	struct ice_ctl_q_info adminq;

	u8 api_branch;		/* API branch version */
	u8 api_maj_ver;		/* API major version */
	u8 api_min_ver;		/* API minor version */
	u8 api_patch;		/* API patch version */
	u8 fw_branch;		/* firmware branch version */
	u8 fw_maj_ver;		/* firmware major version */
	u8 fw_min_ver;		/* firmware minor version */
	u8 fw_patch;		/* firmware patch version */
	u32 fw_build;		/* firmware build number */
};

/* Checksum and Shadow RAM pointers */
#define ICE_SR_NVM_DEV_STARTER_VER	0x18
#define ICE_SR_NVM_EETRACK_LO		0x2D
#define ICE_SR_NVM_EETRACK_HI		0x2E
#define ICE_SR_SECTOR_SIZE_IN_WORDS	0x800
#define ICE_SR_WORDS_IN_1KB		512

#endif /* _ICE_TYPE_H_ */
