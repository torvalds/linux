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

	unsigned int capability;
};

void psp_set_sev_irq_handler(struct psp_device *psp, psp_irq_handler_t handler,
			     void *data);
void psp_clear_sev_irq_handler(struct psp_device *psp);

struct psp_device *psp_get_master_device(void);

#define PSP_CAPABILITY_SEV			BIT(0)
#define PSP_CAPABILITY_TEE			BIT(1)
#define PSP_CAPABILITY_DBC_THRU_EXT		BIT(2)
#define PSP_CAPABILITY_PSP_SECURITY_REPORTING	BIT(7)

#define PSP_CAPABILITY_PSP_SECURITY_OFFSET	8
/*
 * The PSP doesn't directly store these bits in the capability register
 * but instead copies them from the results of query command.
 *
 * The offsets from the query command are below, and shifted when used.
 */
#define PSP_SECURITY_FUSED_PART			BIT(0)
#define PSP_SECURITY_DEBUG_LOCK_ON		BIT(2)
#define PSP_SECURITY_TSME_STATUS		BIT(5)
#define PSP_SECURITY_ANTI_ROLLBACK_STATUS	BIT(7)
#define PSP_SECURITY_RPMC_PRODUCTION_ENABLED	BIT(8)
#define PSP_SECURITY_RPMC_SPIROM_AVAILABLE	BIT(9)
#define PSP_SECURITY_HSP_TPM_AVAILABLE		BIT(10)
#define PSP_SECURITY_ROM_ARMOR_ENFORCED		BIT(11)

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
 */
enum psp_sub_cmd {
	PSP_SUB_CMD_DBC_GET_NONCE	= PSP_DYNAMIC_BOOST_GET_NONCE,
	PSP_SUB_CMD_DBC_SET_UID		= PSP_DYNAMIC_BOOST_SET_UID,
	PSP_SUB_CMD_DBC_GET_PARAMETER	= PSP_DYNAMIC_BOOST_GET_PARAMETER,
	PSP_SUB_CMD_DBC_SET_PARAMETER	= PSP_DYNAMIC_BOOST_SET_PARAMETER,
};

int psp_extended_mailbox_cmd(struct psp_device *psp, unsigned int timeout_msecs,
			     struct psp_ext_request *req);
#endif /* __PSP_DEV_H */
