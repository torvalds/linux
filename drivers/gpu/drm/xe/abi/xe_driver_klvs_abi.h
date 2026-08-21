/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _ABI_XE_DRIVER_KLVS_ABI_H
#define _ABI_XE_DRIVER_KLVS_ABI_H

#include "abi/guc_klvs_abi.h"

/**
 * DOC: Xe Driver KLVs
 *
 * The Xe driver uses the following keys from the `GuC Reserved KLVs`_ range:
 *
 * _`MIGRATION_KLV_DEVICE_DEVID_KEY` :
 *      PCI device ID of the migrated VF.
 * _`MIGRATION_KLV_DEVICE_REVID_KEY` :
 *      PCI device revision ID of the migrated VF.
 */

#define MIGRATION_KLV_DEVICE_DEVID_KEY		0xf001u
#define MIGRATION_KLV_DEVICE_DEVID_LEN		1u
#define MIGRATION_KLV_DEVICE_REVID_KEY		0xf002u
#define MIGRATION_KLV_DEVICE_REVID_LEN		1u

#endif
