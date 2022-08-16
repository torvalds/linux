/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *
 * Contributors:
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#ifndef __T7XX_PORT_PROXY_H__
#define __T7XX_PORT_PROXY_H__

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/types.h>

#include "t7xx_hif_cldma.h"
#include "t7xx_modem_ops.h"
#include "t7xx_port.h"

#define MTK_QUEUES		16
#define RX_QUEUE_MAXLEN		32
#define CTRL_QUEUE_MAXLEN	16

enum port_cfg_id {
	PORT_CFG_ID_INVALID,
	PORT_CFG_ID_NORMAL,
	PORT_CFG_ID_EARLY,
};

struct port_proxy {
	int			port_count;
	struct list_head	rx_ch_ports[PORT_CH_ID_MASK + 1];
	struct list_head	queue_ports[CLDMA_NUM][MTK_QUEUES];
	struct device		*dev;
	enum port_cfg_id	cfg_id;
	struct t7xx_port	*ports;
};

struct ccci_header {
	__le32 packet_header;
	__le32 packet_len;
	__le32 status;
	__le32 ex_msg;
};

/* Coupled with HW - indicates if there is data following the CCCI header or not */
#define CCCI_HEADER_NO_DATA	0xffffffff

#define CCCI_H_AST_BIT		BIT(31)
#define CCCI_H_SEQ_FLD		GENMASK(30, 16)
#define CCCI_H_CHN_FLD		GENMASK(15, 0)

struct ctrl_msg_header {
	__le32	ctrl_msg_id;
	__le32	ex_msg;
	__le32	data_length;
};

/* Control identification numbers for AP<->MD messages  */
#define CTL_ID_HS1_MSG		0x0
#define CTL_ID_HS2_MSG		0x1
#define CTL_ID_HS3_MSG		0x2
#define CTL_ID_MD_EX		0x4
#define CTL_ID_DRV_VER_ERROR	0x5
#define CTL_ID_MD_EX_ACK	0x6
#define CTL_ID_MD_EX_PASS	0x8
#define CTL_ID_PORT_ENUM	0x9

/* Modem exception check identification code - "EXCP" */
#define MD_EX_CHK_ID		0x45584350
/* Modem exception check acknowledge identification code - "EREC" */
#define MD_EX_CHK_ACK_ID	0x45524543

#define PORT_INFO_RSRVD		GENMASK(31, 16)
#define PORT_INFO_ENFLG		BIT(15)
#define PORT_INFO_CH_ID		GENMASK(14, 0)

#define PORT_ENUM_VER		0
#define PORT_ENUM_HEAD_PATTERN	0x5a5a5a5a
#define PORT_ENUM_TAIL_PATTERN	0xa5a5a5a5
#define PORT_ENUM_VER_MISMATCH	0x00657272

/* Port operations mapping */
extern struct port_ops wwan_sub_port_ops;
extern struct port_ops ctl_port_ops;

void t7xx_port_proxy_reset(struct port_proxy *port_prox);
void t7xx_port_proxy_uninit(struct port_proxy *port_prox);
int t7xx_port_proxy_init(struct t7xx_modem *md);
void t7xx_port_proxy_md_status_notify(struct port_proxy *port_prox, unsigned int state);
int t7xx_port_enum_msg_handler(struct t7xx_modem *md, void *msg);
int t7xx_port_proxy_chl_enable_disable(struct port_proxy *port_prox, unsigned int ch_id,
				       bool en_flag);
struct t7xx_port *t7xx_port_proxy_get_port_by_name(struct port_proxy *port_prox, char *port_name);
void t7xx_port_proxy_set_cfg(struct t7xx_modem *md, enum port_cfg_id cfg_id);

#endif /* __T7XX_PORT_PROXY_H__ */
