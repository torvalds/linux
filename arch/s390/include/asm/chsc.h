/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2020
 *
 * Author(s): Alexandra Winter <wintera@linux.ibm.com>
 *
 * Interface for Channel Subsystem Call
 */
#ifndef _ASM_S390_CHSC_H
#define _ASM_S390_CHSC_H

#include <uapi/asm/chsc.h>

/* struct from linux/notifier.h */
struct notifier_block;

/**
 * Operation codes for CHSC PNSO:
 *    PNSO_OC_NET_BRIDGE_INFO - only addresses that are visible to a bridgeport
 *    PNSO_OC_NET_ADDR_INFO   - all addresses
 */
#define PNSO_OC_NET_BRIDGE_INFO		0
#define PNSO_OC_NET_ADDR_INFO		3
/**
 * struct chsc_pnso_naid_l2 - network address information descriptor
 * @nit:  Network interface token
 * @addr_lnid: network address and logical network id (VLAN ID)
 */
struct chsc_pnso_naid_l2 {
	u64 nit;
	struct { u8 mac[6]; u16 lnid; } addr_lnid;
} __packed;

struct chsc_pnso_resume_token {
	u64 t1;
	u64 t2;
} __packed;

struct chsc_pnso_naihdr {
	struct chsc_pnso_resume_token resume_token;
	u32:32;
	u32 instance;
	u32:24;
	u8 naids;
	u32 reserved[3];
} __packed;

struct chsc_pnso_area {
	struct chsc_header request;
	u8:2;
	u8 m:1;
	u8:5;
	u8:2;
	u8 ssid:2;
	u8 fmt:4;
	u16 sch;
	u8:8;
	u8 cssid;
	u16:16;
	u8 oc;
	u32:24;
	struct chsc_pnso_resume_token resume_token;
	u32 n:1;
	u32:31;
	u32 reserved[3];
	struct chsc_header response;
	u32:32;
	struct chsc_pnso_naihdr naihdr;
	struct chsc_pnso_naid_l2 entries[];
} __packed __aligned(PAGE_SIZE);

/*
 * notifier interface - registered notifiers gets called on
 * the following events:
 * - ap config changed (CHSC_NOTIFY_AP_CFG)
 */
enum chsc_notify_type {
	CHSC_NOTIFY_AP_CFG = 3,
};

int chsc_notifier_register(struct notifier_block *nb);
int chsc_notifier_unregister(struct notifier_block *nb);

#endif /* _ASM_S390_CHSC_H */
