// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 * Intel specific definitions for NVDIMM Firmware Interface Table - NFIT
 */
#ifndef _NFIT_INTEL_H_
#define _NFIT_INTEL_H_

#define ND_INTEL_SMART 1

#define ND_INTEL_SMART_SHUTDOWN_COUNT_VALID     (1 << 5)
#define ND_INTEL_SMART_SHUTDOWN_VALID           (1 << 10)

struct nd_intel_smart {
	u32 status;
	union {
		struct {
			u32 flags;
			u8 reserved0[4];
			u8 health;
			u8 spares;
			u8 life_used;
			u8 alarm_flags;
			u16 media_temperature;
			u16 ctrl_temperature;
			u32 shutdown_count;
			u8 ait_status;
			u16 pmic_temperature;
			u8 reserved1[8];
			u8 shutdown_state;
			u32 vendor_size;
			u8 vendor_data[92];
		} __packed;
		u8 data[128];
	};
} __packed;

extern const struct nvdimm_security_ops *intel_security_ops;

#define ND_INTEL_STATUS_SIZE		4
#define ND_INTEL_PASSPHRASE_SIZE	32

#define ND_INTEL_STATUS_NOT_SUPPORTED	1
#define ND_INTEL_STATUS_RETRY		5
#define ND_INTEL_STATUS_NOT_READY	9
#define ND_INTEL_STATUS_INVALID_STATE	10
#define ND_INTEL_STATUS_INVALID_PASS	11
#define ND_INTEL_STATUS_OVERWRITE_UNSUPPORTED	0x10007
#define ND_INTEL_STATUS_OQUERY_INPROGRESS	0x10007
#define ND_INTEL_STATUS_OQUERY_SEQUENCE_ERR	0x20007

#define ND_INTEL_SEC_STATE_ENABLED	0x02
#define ND_INTEL_SEC_STATE_LOCKED	0x04
#define ND_INTEL_SEC_STATE_FROZEN	0x08
#define ND_INTEL_SEC_STATE_PLIMIT	0x10
#define ND_INTEL_SEC_STATE_UNSUPPORTED	0x20
#define ND_INTEL_SEC_STATE_OVERWRITE	0x40

#define ND_INTEL_SEC_ESTATE_ENABLED	0x01
#define ND_INTEL_SEC_ESTATE_PLIMIT	0x02

struct nd_intel_get_security_state {
	u32 status;
	u8 extended_state;
	u8 reserved[3];
	u8 state;
	u8 reserved1[3];
} __packed;

struct nd_intel_set_passphrase {
	u8 old_pass[ND_INTEL_PASSPHRASE_SIZE];
	u8 new_pass[ND_INTEL_PASSPHRASE_SIZE];
	u32 status;
} __packed;

struct nd_intel_unlock_unit {
	u8 passphrase[ND_INTEL_PASSPHRASE_SIZE];
	u32 status;
} __packed;

struct nd_intel_disable_passphrase {
	u8 passphrase[ND_INTEL_PASSPHRASE_SIZE];
	u32 status;
} __packed;

struct nd_intel_freeze_lock {
	u32 status;
} __packed;

struct nd_intel_secure_erase {
	u8 passphrase[ND_INTEL_PASSPHRASE_SIZE];
	u32 status;
} __packed;

struct nd_intel_overwrite {
	u8 passphrase[ND_INTEL_PASSPHRASE_SIZE];
	u32 status;
} __packed;

struct nd_intel_query_overwrite {
	u32 status;
} __packed;

struct nd_intel_set_master_passphrase {
	u8 old_pass[ND_INTEL_PASSPHRASE_SIZE];
	u8 new_pass[ND_INTEL_PASSPHRASE_SIZE];
	u32 status;
} __packed;

struct nd_intel_master_secure_erase {
	u8 passphrase[ND_INTEL_PASSPHRASE_SIZE];
	u32 status;
} __packed;

#define ND_INTEL_FWA_IDLE 0
#define ND_INTEL_FWA_ARMED 1
#define ND_INTEL_FWA_BUSY 2

#define ND_INTEL_DIMM_FWA_NONE 0
#define ND_INTEL_DIMM_FWA_NOTSTAGED 1
#define ND_INTEL_DIMM_FWA_SUCCESS 2
#define ND_INTEL_DIMM_FWA_NEEDRESET 3
#define ND_INTEL_DIMM_FWA_MEDIAFAILED 4
#define ND_INTEL_DIMM_FWA_ABORT 5
#define ND_INTEL_DIMM_FWA_NOTSUPP 6
#define ND_INTEL_DIMM_FWA_ERROR 7

struct nd_intel_fw_activate_dimminfo {
	u32 status;
	u16 result;
	u8 state;
	u8 reserved[7];
} __packed;

#define ND_INTEL_DIMM_FWA_ARM 1
#define ND_INTEL_DIMM_FWA_DISARM 0

struct nd_intel_fw_activate_arm {
	u8 activate_arm;
	u32 status;
} __packed;

/* Root device command payloads */
#define ND_INTEL_BUS_FWA_CAP_FWQUIESCE (1 << 0)
#define ND_INTEL_BUS_FWA_CAP_OSQUIESCE (1 << 1)
#define ND_INTEL_BUS_FWA_CAP_RESET     (1 << 2)

struct nd_intel_bus_fw_activate_businfo {
	u32 status;
	u16 reserved;
	u8 state;
	u8 capability;
	u64 activate_tmo;
	u64 cpu_quiesce_tmo;
	u64 io_quiesce_tmo;
	u64 max_quiesce_tmo;
} __packed;

#define ND_INTEL_BUS_FWA_STATUS_NOARM  (6 | 1 << 16)
#define ND_INTEL_BUS_FWA_STATUS_BUSY   (6 | 2 << 16)
#define ND_INTEL_BUS_FWA_STATUS_NOFW   (6 | 3 << 16)
#define ND_INTEL_BUS_FWA_STATUS_TMO    (6 | 4 << 16)
#define ND_INTEL_BUS_FWA_STATUS_NOIDLE (6 | 5 << 16)
#define ND_INTEL_BUS_FWA_STATUS_ABORT  (6 | 6 << 16)

#define ND_INTEL_BUS_FWA_IODEV_FORCE_IDLE (0)
#define ND_INTEL_BUS_FWA_IODEV_OS_IDLE (1)
struct nd_intel_bus_fw_activate {
	u8 iodev_state;
	u32 status;
} __packed;

extern const struct nvdimm_fw_ops *intel_fw_ops;
extern const struct nvdimm_bus_fw_ops *intel_bus_fw_ops;
#endif
