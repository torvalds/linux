/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Platform Security Processor (PSP) interface driver
 *
 * Copyright (C) 2017-2019 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 */

#ifndef __PSP_DEV_H__
#define __PSP_DEV_H__

#include <linux/device.h>
#include <linux/list.h>
#include <linux/bits.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/psp.h>
#include <linux/psp-platform-access.h>

#include "sp-dev.h"

#define MAX_PSP_NAME_LEN		16

extern struct psp_device *psp_master;

typedef void (*psp_irq_handler_t)(int, void *, unsigned int);

union psp_cap_register {
	unsigned int raw;
	struct {
		unsigned int sev			:1,
			     tee			:1,
			     dbc_thru_ext		:1,
			     sfs			:1,
			     rsvd1			:3,
			     security_reporting		:1,
			     fused_part			:1,
			     rsvd2			:1,
			     debug_lock_on		:1,
			     rsvd3			:2,
			     tsme_status		:1,
			     rsvd4			:1,
			     anti_rollback_status	:1,
			     rpmc_production_enabled	:1,
			     rpmc_spirom_available	:1,
			     hsp_tpm_available		:1,
			     rom_armor_enforced		:1,
			     rsvd5			:12;
	};
};

struct psp_device {
	struct list_head entry;

	struct psp_vdata *vdata;
	char name[MAX_PSP_NAME_LEN];

	struct device *dev;
	struct sp_device *sp;

	void __iomem *io_regs;
	struct mutex mailbox_mutex;

	psp_irq_handler_t sev_irq_handler;
	void *sev_irq_data;

	void *sev_data;
	void *tee_data;
	void *platform_access_data;
	void *dbc_data;
	void *sfs_data;

	union psp_cap_register capability;
};

void psp_set_sev_irq_handler(struct psp_device *psp, psp_irq_handler_t handler,
			     void *data);
void psp_clear_sev_irq_handler(struct psp_device *psp);

struct psp_device *psp_get_master_device(void);

/**
 * enum psp_cmd - PSP mailbox commands
 * @PSP_CMD_TEE_RING_INIT:	Initialize TEE ring buffer
 * @PSP_CMD_TEE_RING_DESTROY:	Destroy TEE ring buffer
 * @PSP_CMD_TEE_EXTENDED_CMD:	Extended command
 * @PSP_CMD_MAX:		Maximum command id
 */
enum psp_cmd {
	PSP_CMD_TEE_RING_INIT		= 1,
	PSP_CMD_TEE_RING_DESTROY	= 2,
	PSP_CMD_TEE_EXTENDED_CMD	= 14,
	PSP_CMD_MAX			= 15,
};

int psp_mailbox_command(struct psp_device *psp, enum psp_cmd cmd, void *cmdbuff,
			unsigned int timeout_msecs, unsigned int *cmdresp);

/**
 * struct psp_ext_req_buffer_hdr - Structure of the extended command header
 * @payload_size: total payload size
 * @sub_cmd_id: extended command ID
 * @status: status of command execution (out)
 */
struct psp_ext_req_buffer_hdr {
	u32 payload_size;
	u32 sub_cmd_id;
	u32 status;
} __packed;

struct psp_ext_request {
	struct psp_ext_req_buffer_hdr header;
	void *buf;
} __packed;

/**
 * enum psp_sub_cmd - PSP mailbox sub commands
 * @PSP_SUB_CMD_DBC_GET_NONCE:		Get nonce from DBC
 * @PSP_SUB_CMD_DBC_SET_UID:		Set UID for DBC
 * @PSP_SUB_CMD_DBC_GET_PARAMETER:	Get parameter from DBC
 * @PSP_SUB_CMD_DBC_SET_PARAMETER:	Set parameter for DBC
 * @PSP_SUB_CMD_SFS_GET_FW_VERS:	Get firmware versions for ASP and other MP
 * @PSP_SUB_CMD_SFS_UPDATE:		Command to load, verify and execute SFS package
 */
enum psp_sub_cmd {
	PSP_SUB_CMD_DBC_GET_NONCE	= PSP_DYNAMIC_BOOST_GET_NONCE,
	PSP_SUB_CMD_DBC_SET_UID		= PSP_DYNAMIC_BOOST_SET_UID,
	PSP_SUB_CMD_DBC_GET_PARAMETER	= PSP_DYNAMIC_BOOST_GET_PARAMETER,
	PSP_SUB_CMD_DBC_SET_PARAMETER	= PSP_DYNAMIC_BOOST_SET_PARAMETER,
	PSP_SUB_CMD_SFS_GET_FW_VERS	= PSP_SFS_GET_FW_VERSIONS,
	PSP_SUB_CMD_SFS_UPDATE		= PSP_SFS_UPDATE,
};

int psp_extended_mailbox_cmd(struct psp_device *psp, unsigned int timeout_msecs,
			     struct psp_ext_request *req);
#endif /* __PSP_DEV_H */
