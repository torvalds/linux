/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  dcdbas.h: Definitions for Dell Systems Management Base driver
 *
 *  Copyright (C) 1995-2005 Dell Inc.
 */

#ifndef _DCDBAS_H_
#define _DCDBAS_H_

#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#define MAX_SMI_DATA_BUF_SIZE			(256 * 1024)

#define HC_ACTION_NONE				(0)
#define HC_ACTION_HOST_CONTROL_POWEROFF		BIT(1)
#define HC_ACTION_HOST_CONTROL_POWERCYCLE	BIT(2)

#define HC_SMITYPE_NONE				(0)
#define HC_SMITYPE_TYPE1			(1)
#define HC_SMITYPE_TYPE2			(2)
#define HC_SMITYPE_TYPE3			(3)

#define ESM_APM_CMD				(0x0A0)
#define ESM_APM_POWER_CYCLE			(0x10)
#define ESM_STATUS_CMD_UNSUCCESSFUL		(-1)

#define CMOS_BASE_PORT				(0x070)
#define CMOS_PAGE1_INDEX_PORT			(0)
#define CMOS_PAGE1_DATA_PORT			(1)
#define CMOS_PAGE2_INDEX_PORT_PIIX4		(2)
#define CMOS_PAGE2_DATA_PORT_PIIX4		(3)
#define PE1400_APM_CONTROL_PORT			(0x0B0)
#define PCAT_APM_CONTROL_PORT			(0x0B2)
#define PCAT_APM_STATUS_PORT			(0x0B3)
#define PE1300_CMOS_CMD_STRUCT_PTR		(0x38)
#define PE1400_CMOS_CMD_STRUCT_PTR		(0x70)

#define MAX_SYSMGMT_SHORTCMD_PARMBUF_LEN	(14)
#define MAX_SYSMGMT_LONGCMD_SGENTRY_NUM		(16)

#define TIMEOUT_USEC_SHORT_SEMA_BLOCKING	(10000)
#define EXPIRED_TIMER				(0)

#define SMI_CMD_MAGIC				(0x534D4931)
#define SMM_EPS_SIG				"$SCB"

#define DCDBAS_DEV_ATTR_RW(_name) \
	DEVICE_ATTR(_name,0600,_name##_show,_name##_store);

#define DCDBAS_DEV_ATTR_RO(_name) \
	DEVICE_ATTR(_name,0400,_name##_show,NULL);

#define DCDBAS_DEV_ATTR_WO(_name) \
	DEVICE_ATTR(_name,0200,NULL,_name##_store);

struct smi_cmd {
	__u32 magic;
	__u32 ebx;
	__u32 ecx;
	__u16 command_address;
	__u8 command_code;
	__u8 reserved;
	__u8 command_buffer[1];
} __attribute__ ((packed));

struct apm_cmd {
	__u8 command;
	__s8 status;
	__u16 reserved;
	union {
		struct {
			__u8 parm[MAX_SYSMGMT_SHORTCMD_PARMBUF_LEN];
		} __attribute__ ((packed)) shortreq;

		struct {
			__u16 num_sg_entries;
			struct {
				__u32 size;
				__u64 addr;
			} __attribute__ ((packed))
			    sglist[MAX_SYSMGMT_LONGCMD_SGENTRY_NUM];
		} __attribute__ ((packed)) longreq;
	} __attribute__ ((packed)) parameters;
} __attribute__ ((packed));

int dcdbas_smi_request(struct smi_cmd *smi_cmd);

struct smm_eps_table {
	char smm_comm_buff_anchor[4];
	u8 length;
	u8 checksum;
	u8 version;
	u64 smm_comm_buff_addr;
	u64 num_of_4k_pages;
} __packed;

struct smi_buffer {
	u8 *virt;
	unsigned long size;
	dma_addr_t dma;
};

int dcdbas_smi_alloc(struct smi_buffer *smi_buffer, unsigned long size);
void dcdbas_smi_free(struct smi_buffer *smi_buffer);

#endif /* _DCDBAS_H_ */

