/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_main.h"
#include "octeon_network.h"
#include "cn66xx_regs.h"
#include "cn66xx_device.h"
#include "cn23xx_pf_device.h"
#include "cn23xx_vf_device.h"

/** Default configuration
 *  for CN66XX OCTEON Models.
 */
static struct octeon_config default_cn66xx_conf = {
	.card_type                              = LIO_210SV,
	.card_name                              = LIO_210SV_NAME,

	/** IQ attributes */
	.iq					= {
		.max_iqs			= CN6XXX_CFG_IO_QUEUES,
		.pending_list_size		=
			(CN6XXX_MAX_IQ_DESCRIPTORS * CN6XXX_CFG_IO_QUEUES),
		.instr_type			= OCTEON_64BYTE_INSTR,
		.db_min				= CN6XXX_DB_MIN,
		.db_timeout			= CN6XXX_DB_TIMEOUT,
	}
	,

	/** OQ attributes */
	.oq					= {
		.max_oqs			= CN6XXX_CFG_IO_QUEUES,
		.refill_threshold		= CN6XXX_OQ_REFIL_THRESHOLD,
		.oq_intr_pkt			= CN6XXX_OQ_INTR_PKT,
		.oq_intr_time			= CN6XXX_OQ_INTR_TIME,
		.pkts_per_intr			= CN6XXX_OQ_PKTSPER_INTR,
	}
	,

	.num_nic_ports				= DEFAULT_NUM_NIC_PORTS_66XX,
	.num_def_rx_descs			= CN6XXX_MAX_OQ_DESCRIPTORS,
	.num_def_tx_descs			= CN6XXX_MAX_IQ_DESCRIPTORS,
	.def_rx_buf_size			= CN6XXX_OQ_BUF_SIZE,

	/* For ethernet interface 0:  Port cfg Attributes */
	.nic_if_cfg[0] = {
		/* Max Txqs: Half for each of the two ports :max_iq/2 */
		.max_txqs			= MAX_TXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_txqs */
		.num_txqs			= DEF_TXQS_PER_INTF,

		/* Max Rxqs: Half for each of the two ports :max_oq/2  */
		.max_rxqs			= MAX_RXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_rxqs */
		.num_rxqs			= DEF_RXQS_PER_INTF,

		/* Num of desc for rx rings */
		.num_rx_descs			= CN6XXX_MAX_OQ_DESCRIPTORS,

		/* Num of desc for tx rings */
		.num_tx_descs			= CN6XXX_MAX_IQ_DESCRIPTORS,

		/* SKB size, We need not change buf size even for Jumbo frames.
		 * Octeon can send jumbo frames in 4 consecutive descriptors,
		 */
		.rx_buf_size			= CN6XXX_OQ_BUF_SIZE,

		.base_queue			= BASE_QUEUE_NOT_REQUESTED,

		.gmx_port_id			= 0,
	},

	.nic_if_cfg[1] = {
		/* Max Txqs: Half for each of the two ports :max_iq/2 */
		.max_txqs			= MAX_TXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_txqs */
		.num_txqs			= DEF_TXQS_PER_INTF,

		/* Max Rxqs: Half for each of the two ports :max_oq/2  */
		.max_rxqs			= MAX_RXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_rxqs */
		.num_rxqs			= DEF_RXQS_PER_INTF,

		/* Num of desc for rx rings */
		.num_rx_descs			= CN6XXX_MAX_OQ_DESCRIPTORS,

		/* Num of desc for tx rings */
		.num_tx_descs			= CN6XXX_MAX_IQ_DESCRIPTORS,

		/* SKB size, We need not change buf size even for Jumbo frames.
		 * Octeon can send jumbo frames in 4 consecutive descriptors,
		 */
		.rx_buf_size			= CN6XXX_OQ_BUF_SIZE,

		.base_queue			= BASE_QUEUE_NOT_REQUESTED,

		.gmx_port_id			= 1,
	},

	/** Miscellaneous attributes */
	.misc					= {
		/* Host driver link query interval */
		.oct_link_query_interval	= 100,

		/* Octeon link query interval */
		.host_link_query_interval	= 500,

		.enable_sli_oq_bp		= 0,

		/* Control queue group */
		.ctrlq_grp			= 1,
	}
	,
};

/** Default configuration
 *  for CN68XX OCTEON Model.
 */

static struct octeon_config default_cn68xx_conf = {
	.card_type                              = LIO_410NV,
	.card_name                              = LIO_410NV_NAME,

	/** IQ attributes */
	.iq					= {
		.max_iqs			= CN6XXX_CFG_IO_QUEUES,
		.pending_list_size		=
			(CN6XXX_MAX_IQ_DESCRIPTORS * CN6XXX_CFG_IO_QUEUES),
		.instr_type			= OCTEON_64BYTE_INSTR,
		.db_min				= CN6XXX_DB_MIN,
		.db_timeout			= CN6XXX_DB_TIMEOUT,
	}
	,

	/** OQ attributes */
	.oq					= {
		.max_oqs			= CN6XXX_CFG_IO_QUEUES,
		.refill_threshold		= CN6XXX_OQ_REFIL_THRESHOLD,
		.oq_intr_pkt			= CN6XXX_OQ_INTR_PKT,
		.oq_intr_time			= CN6XXX_OQ_INTR_TIME,
		.pkts_per_intr			= CN6XXX_OQ_PKTSPER_INTR,
	}
	,

	.num_nic_ports				= DEFAULT_NUM_NIC_PORTS_68XX,
	.num_def_rx_descs			= CN6XXX_MAX_OQ_DESCRIPTORS,
	.num_def_tx_descs			= CN6XXX_MAX_IQ_DESCRIPTORS,
	.def_rx_buf_size			= CN6XXX_OQ_BUF_SIZE,

	.nic_if_cfg[0] = {
		/* Max Txqs: Half for each of the two ports :max_iq/2 */
		.max_txqs			= MAX_TXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_txqs */
		.num_txqs			= DEF_TXQS_PER_INTF,

		/* Max Rxqs: Half for each of the two ports :max_oq/2  */
		.max_rxqs			= MAX_RXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_rxqs */
		.num_rxqs			= DEF_RXQS_PER_INTF,

		/* Num of desc for rx rings */
		.num_rx_descs			= CN6XXX_MAX_OQ_DESCRIPTORS,

		/* Num of desc for tx rings */
		.num_tx_descs			= CN6XXX_MAX_IQ_DESCRIPTORS,

		/* SKB size, We need not change buf size even for Jumbo frames.
		 * Octeon can send jumbo frames in 4 consecutive descriptors,
		 */
		.rx_buf_size			= CN6XXX_OQ_BUF_SIZE,

		.base_queue			= BASE_QUEUE_NOT_REQUESTED,

		.gmx_port_id			= 0,
	},

	.nic_if_cfg[1] = {
		/* Max Txqs: Half for each of the two ports :max_iq/2 */
		.max_txqs			= MAX_TXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_txqs */
		.num_txqs			= DEF_TXQS_PER_INTF,

		/* Max Rxqs: Half for each of the two ports :max_oq/2  */
		.max_rxqs			= MAX_RXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_rxqs */
		.num_rxqs			= DEF_RXQS_PER_INTF,

		/* Num of desc for rx rings */
		.num_rx_descs			= CN6XXX_MAX_OQ_DESCRIPTORS,

		/* Num of desc for tx rings */
		.num_tx_descs			= CN6XXX_MAX_IQ_DESCRIPTORS,

		/* SKB size, We need not change buf size even for Jumbo frames.
		 * Octeon can send jumbo frames in 4 consecutive descriptors,
		 */
		.rx_buf_size			= CN6XXX_OQ_BUF_SIZE,

		.base_queue			= BASE_QUEUE_NOT_REQUESTED,

		.gmx_port_id			= 1,
	},

	.nic_if_cfg[2] = {
		/* Max Txqs: Half for each of the two ports :max_iq/2 */
		.max_txqs			= MAX_TXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_txqs */
		.num_txqs			= DEF_TXQS_PER_INTF,

		/* Max Rxqs: Half for each of the two ports :max_oq/2  */
		.max_rxqs			= MAX_RXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_rxqs */
		.num_rxqs			= DEF_RXQS_PER_INTF,

		/* Num of desc for rx rings */
		.num_rx_descs			= CN6XXX_MAX_OQ_DESCRIPTORS,

		/* Num of desc for tx rings */
		.num_tx_descs			= CN6XXX_MAX_IQ_DESCRIPTORS,

		/* SKB size, We need not change buf size even for Jumbo frames.
		 * Octeon can send jumbo frames in 4 consecutive descriptors,
		 */
		.rx_buf_size			= CN6XXX_OQ_BUF_SIZE,

		.base_queue			= BASE_QUEUE_NOT_REQUESTED,

		.gmx_port_id			= 2,
	},

	.nic_if_cfg[3] = {
		/* Max Txqs: Half for each of the two ports :max_iq/2 */
		.max_txqs			= MAX_TXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_txqs */
		.num_txqs			= DEF_TXQS_PER_INTF,

		/* Max Rxqs: Half for each of the two ports :max_oq/2  */
		.max_rxqs			= MAX_RXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_rxqs */
		.num_rxqs			= DEF_RXQS_PER_INTF,

		/* Num of desc for rx rings */
		.num_rx_descs			= CN6XXX_MAX_OQ_DESCRIPTORS,

		/* Num of desc for tx rings */
		.num_tx_descs			= CN6XXX_MAX_IQ_DESCRIPTORS,

		/* SKB size, We need not change buf size even for Jumbo frames.
		 * Octeon can send jumbo frames in 4 consecutive descriptors,
		 */
		.rx_buf_size			= CN6XXX_OQ_BUF_SIZE,

		.base_queue			= BASE_QUEUE_NOT_REQUESTED,

		.gmx_port_id			= 3,
	},

	/** Miscellaneous attributes */
	.misc					= {
		/* Host driver link query interval */
		.oct_link_query_interval	= 100,

		/* Octeon link query interval */
		.host_link_query_interval	= 500,

		.enable_sli_oq_bp		= 0,

		/* Control queue group */
		.ctrlq_grp			= 1,
	}
	,
};

/** Default configuration
 *  for CN68XX OCTEON Model.
 */
static struct octeon_config default_cn68xx_210nv_conf = {
	.card_type                              = LIO_210NV,
	.card_name                              = LIO_210NV_NAME,

	/** IQ attributes */

	.iq					= {
		.max_iqs			= CN6XXX_CFG_IO_QUEUES,
		.pending_list_size		=
			(CN6XXX_MAX_IQ_DESCRIPTORS * CN6XXX_CFG_IO_QUEUES),
		.instr_type			= OCTEON_64BYTE_INSTR,
		.db_min				= CN6XXX_DB_MIN,
		.db_timeout			= CN6XXX_DB_TIMEOUT,
	}
	,

	/** OQ attributes */
	.oq					= {
		.max_oqs			= CN6XXX_CFG_IO_QUEUES,
		.refill_threshold		= CN6XXX_OQ_REFIL_THRESHOLD,
		.oq_intr_pkt			= CN6XXX_OQ_INTR_PKT,
		.oq_intr_time			= CN6XXX_OQ_INTR_TIME,
		.pkts_per_intr			= CN6XXX_OQ_PKTSPER_INTR,
	}
	,

	.num_nic_ports			= DEFAULT_NUM_NIC_PORTS_68XX_210NV,
	.num_def_rx_descs		= CN6XXX_MAX_OQ_DESCRIPTORS,
	.num_def_tx_descs		= CN6XXX_MAX_IQ_DESCRIPTORS,
	.def_rx_buf_size		= CN6XXX_OQ_BUF_SIZE,

	.nic_if_cfg[0] = {
		/* Max Txqs: Half for each of the two ports :max_iq/2 */
		.max_txqs			= MAX_TXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_txqs */
		.num_txqs			= DEF_TXQS_PER_INTF,

		/* Max Rxqs: Half for each of the two ports :max_oq/2  */
		.max_rxqs			= MAX_RXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_rxqs */
		.num_rxqs			= DEF_RXQS_PER_INTF,

		/* Num of desc for rx rings */
		.num_rx_descs			= CN6XXX_MAX_OQ_DESCRIPTORS,

		/* Num of desc for tx rings */
		.num_tx_descs			= CN6XXX_MAX_IQ_DESCRIPTORS,

		/* SKB size, We need not change buf size even for Jumbo frames.
		 * Octeon can send jumbo frames in 4 consecutive descriptors,
		 */
		.rx_buf_size			= CN6XXX_OQ_BUF_SIZE,

		.base_queue			= BASE_QUEUE_NOT_REQUESTED,

		.gmx_port_id			= 0,
	},

	.nic_if_cfg[1] = {
		/* Max Txqs: Half for each of the two ports :max_iq/2 */
		.max_txqs			= MAX_TXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_txqs */
		.num_txqs			= DEF_TXQS_PER_INTF,

		/* Max Rxqs: Half for each of the two ports :max_oq/2  */
		.max_rxqs			= MAX_RXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_rxqs */
		.num_rxqs			= DEF_RXQS_PER_INTF,

		/* Num of desc for rx rings */
		.num_rx_descs			= CN6XXX_MAX_OQ_DESCRIPTORS,

		/* Num of desc for tx rings */
		.num_tx_descs			= CN6XXX_MAX_IQ_DESCRIPTORS,

		/* SKB size, We need not change buf size even for Jumbo frames.
		 * Octeon can send jumbo frames in 4 consecutive descriptors,
		 */
		.rx_buf_size			= CN6XXX_OQ_BUF_SIZE,

		.base_queue			= BASE_QUEUE_NOT_REQUESTED,

		.gmx_port_id			= 1,
	},

	/** Miscellaneous attributes */
	.misc					= {
		/* Host driver link query interval */
		.oct_link_query_interval	= 100,

		/* Octeon link query interval */
		.host_link_query_interval	= 500,

		.enable_sli_oq_bp		= 0,

		/* Control queue group */
		.ctrlq_grp			= 1,
	}
	,
};

static struct octeon_config default_cn23xx_conf = {
	.card_type                              = LIO_23XX,
	.card_name                              = LIO_23XX_NAME,
	/** IQ attributes */
	.iq = {
		.max_iqs		= CN23XX_CFG_IO_QUEUES,
		.pending_list_size	= (CN23XX_DEFAULT_IQ_DESCRIPTORS *
					   CN23XX_CFG_IO_QUEUES),
		.instr_type		= OCTEON_64BYTE_INSTR,
		.db_min			= CN23XX_DB_MIN,
		.db_timeout		= CN23XX_DB_TIMEOUT,
		.iq_intr_pkt		= CN23XX_DEF_IQ_INTR_THRESHOLD,
	},

	/** OQ attributes */
	.oq = {
		.max_oqs		= CN23XX_CFG_IO_QUEUES,
		.pkts_per_intr	= CN23XX_OQ_PKTSPER_INTR,
		.refill_threshold	= CN23XX_OQ_REFIL_THRESHOLD,
		.oq_intr_pkt	= CN23XX_OQ_INTR_PKT,
		.oq_intr_time	= CN23XX_OQ_INTR_TIME,
	},

	.num_nic_ports				= DEFAULT_NUM_NIC_PORTS_23XX,
	.num_def_rx_descs			= CN23XX_DEFAULT_OQ_DESCRIPTORS,
	.num_def_tx_descs			= CN23XX_DEFAULT_IQ_DESCRIPTORS,
	.def_rx_buf_size			= CN23XX_OQ_BUF_SIZE,

	/* For ethernet interface 0:  Port cfg Attributes */
	.nic_if_cfg[0] = {
		/* Max Txqs: Half for each of the two ports :max_iq/2 */
		.max_txqs			= MAX_TXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_txqs */
		.num_txqs			= DEF_TXQS_PER_INTF,

		/* Max Rxqs: Half for each of the two ports :max_oq/2  */
		.max_rxqs			= MAX_RXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_rxqs */
		.num_rxqs			= DEF_RXQS_PER_INTF,

		/* Num of desc for rx rings */
		.num_rx_descs			= CN23XX_DEFAULT_OQ_DESCRIPTORS,

		/* Num of desc for tx rings */
		.num_tx_descs			= CN23XX_DEFAULT_IQ_DESCRIPTORS,

		/* SKB size, We need not change buf size even for Jumbo frames.
		 * Octeon can send jumbo frames in 4 consecutive descriptors,
		 */
		.rx_buf_size			= CN23XX_OQ_BUF_SIZE,

		.base_queue			= BASE_QUEUE_NOT_REQUESTED,

		.gmx_port_id			= 0,
	},

	.nic_if_cfg[1] = {
		/* Max Txqs: Half for each of the two ports :max_iq/2 */
		.max_txqs			= MAX_TXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_txqs */
		.num_txqs			= DEF_TXQS_PER_INTF,

		/* Max Rxqs: Half for each of the two ports :max_oq/2  */
		.max_rxqs			= MAX_RXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_rxqs */
		.num_rxqs			= DEF_RXQS_PER_INTF,

		/* Num of desc for rx rings */
		.num_rx_descs			= CN23XX_DEFAULT_OQ_DESCRIPTORS,

		/* Num of desc for tx rings */
		.num_tx_descs			= CN23XX_DEFAULT_IQ_DESCRIPTORS,

		/* SKB size, We need not change buf size even for Jumbo frames.
		 * Octeon can send jumbo frames in 4 consecutive descriptors,
		 */
		.rx_buf_size			= CN23XX_OQ_BUF_SIZE,

		.base_queue			= BASE_QUEUE_NOT_REQUESTED,

		.gmx_port_id			= 1,
	},

	.misc					= {
		/* Host driver link query interval */
		.oct_link_query_interval	= 100,

		/* Octeon link query interval */
		.host_link_query_interval	= 500,

		.enable_sli_oq_bp		= 0,

		/* Control queue group */
		.ctrlq_grp			= 1,
	}
};

static struct octeon_config_ptr {
	u32 conf_type;
} oct_conf_info[MAX_OCTEON_DEVICES] = {
	{
		OCTEON_CONFIG_TYPE_DEFAULT,
	}, {
		OCTEON_CONFIG_TYPE_DEFAULT,
	}, {
		OCTEON_CONFIG_TYPE_DEFAULT,
	}, {
		OCTEON_CONFIG_TYPE_DEFAULT,
	},
};

static char oct_dev_state_str[OCT_DEV_STATES + 1][32] = {
	"BEGIN", "PCI-ENABLE-DONE", "PCI-MAP-DONE", "DISPATCH-INIT-DONE",
	"IQ-INIT-DONE", "SCBUFF-POOL-INIT-DONE", "RESPLIST-INIT-DONE",
	"DROQ-INIT-DONE", "MBOX-SETUP-DONE", "MSIX-ALLOC-VECTOR-DONE",
	"INTR-SET-DONE", "IO-QUEUES-INIT-DONE", "CONSOLE-INIT-DONE",
	"HOST-READY", "CORE-READY", "RUNNING", "IN-RESET",
	"INVALID"
};

static char oct_dev_app_str[CVM_DRV_APP_COUNT + 1][32] = {
	"BASE", "NIC", "UNKNOWN"};

static struct octeon_device *octeon_device[MAX_OCTEON_DEVICES];
static atomic_t adapter_refcounts[MAX_OCTEON_DEVICES];
static atomic_t adapter_fw_states[MAX_OCTEON_DEVICES];

static u32 octeon_device_count;
/* locks device array (i.e. octeon_device[]) */
static DEFINE_SPINLOCK(octeon_devices_lock);

static struct octeon_core_setup core_setup[MAX_OCTEON_DEVICES];

static void oct_set_config_info(int oct_id, int conf_type)
{
	if (conf_type < 0 || conf_type > (NUM_OCTEON_CONFS - 1))
		conf_type = OCTEON_CONFIG_TYPE_DEFAULT;
	oct_conf_info[oct_id].conf_type = conf_type;
}

void octeon_init_device_list(int conf_type)
{
	int i;

	memset(octeon_device, 0, (sizeof(void *) * MAX_OCTEON_DEVICES));
	for (i = 0; i <  MAX_OCTEON_DEVICES; i++)
		oct_set_config_info(i, conf_type);
}
EXPORT_SYMBOL_GPL(octeon_init_device_list);

static void *__retrieve_octeon_config_info(struct octeon_device *oct,
					   u16 card_type)
{
	u32 oct_id = oct->octeon_id;
	void *ret = NULL;

	switch (oct_conf_info[oct_id].conf_type) {
	case OCTEON_CONFIG_TYPE_DEFAULT:
		if (oct->chip_id == OCTEON_CN66XX) {
			ret = &default_cn66xx_conf;
		} else if ((oct->chip_id == OCTEON_CN68XX) &&
			   (card_type == LIO_210NV)) {
			ret = &default_cn68xx_210nv_conf;
		} else if ((oct->chip_id == OCTEON_CN68XX) &&
			   (card_type == LIO_410NV)) {
			ret = &default_cn68xx_conf;
		} else if (oct->chip_id == OCTEON_CN23XX_PF_VID) {
			ret = &default_cn23xx_conf;
		} else if (oct->chip_id == OCTEON_CN23XX_VF_VID) {
			ret = &default_cn23xx_conf;
		}
		break;
	default:
		break;
	}
	return ret;
}

static int __verify_octeon_config_info(struct octeon_device *oct, void *conf)
{
	switch (oct->chip_id) {
	case OCTEON_CN66XX:
	case OCTEON_CN68XX:
		return lio_validate_cn6xxx_config_info(oct, conf);
	case OCTEON_CN23XX_PF_VID:
	case OCTEON_CN23XX_VF_VID:
		return 0;
	default:
		break;
	}

	return 1;
}

void *oct_get_config_info(struct octeon_device *oct, u16 card_type)
{
	void *conf = NULL;

	conf = __retrieve_octeon_config_info(oct, card_type);
	if (!conf)
		return NULL;

	if (__verify_octeon_config_info(oct, conf)) {
		dev_err(&oct->pci_dev->dev, "Configuration verification failed\n");
		return NULL;
	}

	return conf;
}

char *lio_get_state_string(atomic_t *state_ptr)
{
	s32 istate = (s32)atomic_read(state_ptr);

	if (istate > OCT_DEV_STATES || istate < 0)
		return oct_dev_state_str[OCT_DEV_STATE_INVALID];
	return oct_dev_state_str[istate];
}
EXPORT_SYMBOL_GPL(lio_get_state_string);

static char *get_oct_app_string(u32 app_mode)
{
	if (app_mode <= CVM_DRV_APP_END)
		return oct_dev_app_str[app_mode - CVM_DRV_APP_START];
	return oct_dev_app_str[CVM_DRV_INVALID_APP - CVM_DRV_APP_START];
}

void octeon_free_device_mem(struct octeon_device *oct)
{
	int i;

	for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES(oct); i++) {
		if (oct->io_qmask.oq & BIT_ULL(i))
			vfree(oct->droq[i]);
	}

	for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct); i++) {
		if (oct->io_qmask.iq & BIT_ULL(i))
			vfree(oct->instr_queue[i]);
	}

	i = oct->octeon_id;
	vfree(oct);

	octeon_device[i] = NULL;
	octeon_device_count--;
}
EXPORT_SYMBOL_GPL(octeon_free_device_mem);

static struct octeon_device *octeon_allocate_device_mem(u32 pci_id,
							u32 priv_size)
{
	struct octeon_device *oct;
	u8 *buf = NULL;
	u32 octdevsize = 0, configsize = 0, size;

	switch (pci_id) {
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		configsize = sizeof(struct octeon_cn6xxx);
		break;

	case OCTEON_CN23XX_PF_VID:
		configsize = sizeof(struct octeon_cn23xx_pf);
		break;
	case OCTEON_CN23XX_VF_VID:
		configsize = sizeof(struct octeon_cn23xx_vf);
		break;
	default:
		pr_err("%s: Unknown PCI Device: 0x%x\n",
		       __func__,
		       pci_id);
		return NULL;
	}

	if (configsize & 0x7)
		configsize += (8 - (configsize & 0x7));

	octdevsize = sizeof(struct octeon_device);
	if (octdevsize & 0x7)
		octdevsize += (8 - (octdevsize & 0x7));

	if (priv_size & 0x7)
		priv_size += (8 - (priv_size & 0x7));

	size = octdevsize + priv_size + configsize +
		(sizeof(struct octeon_dispatch) * DISPATCH_LIST_SIZE);

	buf = vzalloc(size);
	if (!buf)
		return NULL;

	oct = (struct octeon_device *)buf;
	oct->priv = (void *)(buf + octdevsize);
	oct->chip = (void *)(buf + octdevsize + priv_size);
	oct->dispatch.dlist = (struct octeon_dispatch *)
		(buf + octdevsize + priv_size + configsize);

	return oct;
}

struct octeon_device *octeon_allocate_device(u32 pci_id,
					     u32 priv_size)
{
	u32 oct_idx = 0;
	struct octeon_device *oct = NULL;

	spin_lock(&octeon_devices_lock);

	for (oct_idx = 0; oct_idx < MAX_OCTEON_DEVICES; oct_idx++)
		if (!octeon_device[oct_idx])
			break;

	if (oct_idx < MAX_OCTEON_DEVICES) {
		oct = octeon_allocate_device_mem(pci_id, priv_size);
		if (oct) {
			octeon_device_count++;
			octeon_device[oct_idx] = oct;
		}
	}

	spin_unlock(&octeon_devices_lock);
	if (!oct)
		return NULL;

	spin_lock_init(&oct->pci_win_lock);
	spin_lock_init(&oct->mem_access_lock);

	oct->octeon_id = oct_idx;
	snprintf(oct->device_name, sizeof(oct->device_name),
		 "LiquidIO%d", (oct->octeon_id));

	return oct;
}
EXPORT_SYMBOL_GPL(octeon_allocate_device);

/** Register a device's bus location at initialization time.
 *  @param octeon_dev - pointer to the octeon device structure.
 *  @param bus        - PCIe bus #
 *  @param dev        - PCIe device #
 *  @param func       - PCIe function #
 *  @param is_pf      - TRUE for PF, FALSE for VF
 *  @return reference count of device's adapter
 */
int octeon_register_device(struct octeon_device *oct,
			   int bus, int dev, int func, int is_pf)
{
	int idx, refcount;

	oct->loc.bus = bus;
	oct->loc.dev = dev;
	oct->loc.func = func;

	oct->adapter_refcount = &adapter_refcounts[oct->octeon_id];
	atomic_set(oct->adapter_refcount, 0);

	/* Like the reference count, the f/w state is shared 'per-adapter' */
	oct->adapter_fw_state = &adapter_fw_states[oct->octeon_id];
	atomic_set(oct->adapter_fw_state, FW_NEEDS_TO_BE_LOADED);

	spin_lock(&octeon_devices_lock);
	for (idx = (int)oct->octeon_id - 1; idx >= 0; idx--) {
		if (!octeon_device[idx]) {
			dev_err(&oct->pci_dev->dev,
				"%s: Internal driver error, missing dev",
				__func__);
			spin_unlock(&octeon_devices_lock);
			atomic_inc(oct->adapter_refcount);
			return 1; /* here, refcount is guaranteed to be 1 */
		}
		/* If another device is at same bus/dev, use its refcounter
		 * (and f/w state variable).
		 */
		if ((octeon_device[idx]->loc.bus == bus) &&
		    (octeon_device[idx]->loc.dev == dev)) {
			oct->adapter_refcount =
				octeon_device[idx]->adapter_refcount;
			oct->adapter_fw_state =
				octeon_device[idx]->adapter_fw_state;
			break;
		}
	}
	spin_unlock(&octeon_devices_lock);

	atomic_inc(oct->adapter_refcount);
	refcount = atomic_read(oct->adapter_refcount);

	dev_dbg(&oct->pci_dev->dev, "%s: %02x:%02x:%d refcount %u", __func__,
		oct->loc.bus, oct->loc.dev, oct->loc.func, refcount);

	return refcount;
}
EXPORT_SYMBOL_GPL(octeon_register_device);

/** Deregister a device at de-initialization time.
 *  @param octeon_dev - pointer to the octeon device structure.
 *  @return reference count of device's adapter
 */
int octeon_deregister_device(struct octeon_device *oct)
{
	int refcount;

	atomic_dec(oct->adapter_refcount);
	refcount = atomic_read(oct->adapter_refcount);

	dev_dbg(&oct->pci_dev->dev, "%s: %04d:%02d:%d refcount %u", __func__,
		oct->loc.bus, oct->loc.dev, oct->loc.func, refcount);

	return refcount;
}
EXPORT_SYMBOL_GPL(octeon_deregister_device);

int
octeon_allocate_ioq_vector(struct octeon_device *oct, u32 num_ioqs)
{
	struct octeon_ioq_vector *ioq_vector;
	int cpu_num;
	int size;
	int i;

	size = sizeof(struct octeon_ioq_vector) * num_ioqs;

	oct->ioq_vector = vzalloc(size);
	if (!oct->ioq_vector)
		return -1;
	for (i = 0; i < num_ioqs; i++) {
		ioq_vector		= &oct->ioq_vector[i];
		ioq_vector->oct_dev	= oct;
		ioq_vector->iq_index	= i;
		ioq_vector->droq_index	= i;
		ioq_vector->mbox	= oct->mbox[i];

		cpu_num = i % num_online_cpus();
		cpumask_set_cpu(cpu_num, &ioq_vector->affinity_mask);

		if (oct->chip_id == OCTEON_CN23XX_PF_VID)
			ioq_vector->ioq_num	= i + oct->sriov_info.pf_srn;
		else
			ioq_vector->ioq_num	= i;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(octeon_allocate_ioq_vector);

void
octeon_free_ioq_vector(struct octeon_device *oct)
{
	vfree(oct->ioq_vector);
}
EXPORT_SYMBOL_GPL(octeon_free_ioq_vector);

/* this function is only for setting up the first queue */
int octeon_setup_instr_queues(struct octeon_device *oct)
{
	u32 num_descs = 0;
	u32 iq_no = 0;
	union oct_txpciq txpciq;
	int numa_node = dev_to_node(&oct->pci_dev->dev);

	if (OCTEON_CN6XXX(oct))
		num_descs =
			CFG_GET_NUM_DEF_TX_DESCS(CHIP_CONF(oct, cn6xxx));
	else if (OCTEON_CN23XX_PF(oct))
		num_descs = CFG_GET_NUM_DEF_TX_DESCS(CHIP_CONF(oct, cn23xx_pf));
	else if (OCTEON_CN23XX_VF(oct))
		num_descs = CFG_GET_NUM_DEF_TX_DESCS(CHIP_CONF(oct, cn23xx_vf));

	oct->num_iqs = 0;

	oct->instr_queue[0] = vzalloc_node(sizeof(*oct->instr_queue[0]),
				numa_node);
	if (!oct->instr_queue[0])
		oct->instr_queue[0] =
			vzalloc(sizeof(struct octeon_instr_queue));
	if (!oct->instr_queue[0])
		return 1;
	memset(oct->instr_queue[0], 0, sizeof(struct octeon_instr_queue));
	oct->instr_queue[0]->q_index = 0;
	oct->instr_queue[0]->app_ctx = (void *)(size_t)0;
	oct->instr_queue[0]->ifidx = 0;
	txpciq.u64 = 0;
	txpciq.s.q_no = iq_no;
	txpciq.s.pkind = oct->pfvf_hsword.pkind;
	txpciq.s.use_qpg = 0;
	txpciq.s.qpg = 0;
	if (octeon_init_instr_queue(oct, txpciq, num_descs)) {
		/* prevent memory leak */
		vfree(oct->instr_queue[0]);
		oct->instr_queue[0] = NULL;
		return 1;
	}

	oct->num_iqs++;
	return 0;
}
EXPORT_SYMBOL_GPL(octeon_setup_instr_queues);

int octeon_setup_output_queues(struct octeon_device *oct)
{
	u32 num_descs = 0;
	u32 desc_size = 0;
	u32 oq_no = 0;
	int numa_node = dev_to_node(&oct->pci_dev->dev);

	if (OCTEON_CN6XXX(oct)) {
		num_descs =
			CFG_GET_NUM_DEF_RX_DESCS(CHIP_CONF(oct, cn6xxx));
		desc_size =
			CFG_GET_DEF_RX_BUF_SIZE(CHIP_CONF(oct, cn6xxx));
	} else if (OCTEON_CN23XX_PF(oct)) {
		num_descs = CFG_GET_NUM_DEF_RX_DESCS(CHIP_CONF(oct, cn23xx_pf));
		desc_size = CFG_GET_DEF_RX_BUF_SIZE(CHIP_CONF(oct, cn23xx_pf));
	} else if (OCTEON_CN23XX_VF(oct)) {
		num_descs = CFG_GET_NUM_DEF_RX_DESCS(CHIP_CONF(oct, cn23xx_vf));
		desc_size = CFG_GET_DEF_RX_BUF_SIZE(CHIP_CONF(oct, cn23xx_vf));
	}
	oct->num_oqs = 0;
	oct->droq[0] = vzalloc_node(sizeof(*oct->droq[0]), numa_node);
	if (!oct->droq[0])
		oct->droq[0] = vzalloc(sizeof(*oct->droq[0]));
	if (!oct->droq[0])
		return 1;

	if (octeon_init_droq(oct, oq_no, num_descs, desc_size, NULL)) {
		vfree(oct->droq[oq_no]);
		oct->droq[oq_no] = NULL;
		return 1;
	}
	oct->num_oqs++;

	return 0;
}
EXPORT_SYMBOL_GPL(octeon_setup_output_queues);

int octeon_set_io_queues_off(struct octeon_device *oct)
{
	int loop = BUSY_READING_REG_VF_LOOP_COUNT;

	if (OCTEON_CN6XXX(oct)) {
		octeon_write_csr(oct, CN6XXX_SLI_PKT_INSTR_ENB, 0);
		octeon_write_csr(oct, CN6XXX_SLI_PKT_OUT_ENB, 0);
	} else if (oct->chip_id == OCTEON_CN23XX_VF_VID) {
		u32 q_no;

		/* IOQs will already be in reset.
		 * If RST bit is set, wait for quiet bit to be set.
		 * Once quiet bit is set, clear the RST bit.
		 */
		for (q_no = 0; q_no < oct->sriov_info.rings_per_vf; q_no++) {
			u64 reg_val = octeon_read_csr64(
				oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no));

			while ((reg_val & CN23XX_PKT_INPUT_CTL_RST) &&
			       !(reg_val &  CN23XX_PKT_INPUT_CTL_QUIET) &&
			       loop) {
				reg_val = octeon_read_csr64(
					oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
				loop--;
			}
			if (!loop) {
				dev_err(&oct->pci_dev->dev,
					"clearing the reset reg failed or setting the quiet reg failed for qno: %u\n",
					q_no);
				return -1;
			}

			reg_val = reg_val & ~CN23XX_PKT_INPUT_CTL_RST;
			octeon_write_csr64(oct,
					   CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
					   reg_val);

			reg_val = octeon_read_csr64(
					oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
			if (reg_val & CN23XX_PKT_INPUT_CTL_RST) {
				dev_err(&oct->pci_dev->dev,
					"unable to reset qno %u\n", q_no);
				return -1;
			}
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(octeon_set_io_queues_off);

void octeon_set_droq_pkt_op(struct octeon_device *oct,
			    u32 q_no,
			    u32 enable)
{
	u32 reg_val = 0;

	/* Disable the i/p and o/p queues for this Octeon. */
	if (OCTEON_CN6XXX(oct)) {
		reg_val = octeon_read_csr(oct, CN6XXX_SLI_PKT_OUT_ENB);

		if (enable)
			reg_val = reg_val | (1 << q_no);
		else
			reg_val = reg_val & (~(1 << q_no));

		octeon_write_csr(oct, CN6XXX_SLI_PKT_OUT_ENB, reg_val);
	}
}

int octeon_init_dispatch_list(struct octeon_device *oct)
{
	u32 i;

	oct->dispatch.count = 0;

	for (i = 0; i < DISPATCH_LIST_SIZE; i++) {
		oct->dispatch.dlist[i].opcode = 0;
		INIT_LIST_HEAD(&oct->dispatch.dlist[i].list);
	}

	for (i = 0; i <= REQTYPE_LAST; i++)
		octeon_register_reqtype_free_fn(oct, i, NULL);

	spin_lock_init(&oct->dispatch.lock);

	return 0;
}
EXPORT_SYMBOL_GPL(octeon_init_dispatch_list);

void octeon_delete_dispatch_list(struct octeon_device *oct)
{
	u32 i;
	struct list_head freelist, *temp, *tmp2;

	INIT_LIST_HEAD(&freelist);

	spin_lock_bh(&oct->dispatch.lock);

	for (i = 0; i < DISPATCH_LIST_SIZE; i++) {
		struct list_head *dispatch;

		dispatch = &oct->dispatch.dlist[i].list;
		while (dispatch->next != dispatch) {
			temp = dispatch->next;
			list_move_tail(temp, &freelist);
		}

		oct->dispatch.dlist[i].opcode = 0;
	}

	oct->dispatch.count = 0;

	spin_unlock_bh(&oct->dispatch.lock);

	list_for_each_safe(temp, tmp2, &freelist) {
		list_del(temp);
		kfree(temp);
	}
}
EXPORT_SYMBOL_GPL(octeon_delete_dispatch_list);

octeon_dispatch_fn_t
octeon_get_dispatch(struct octeon_device *octeon_dev, u16 opcode,
		    u16 subcode)
{
	u32 idx;
	struct list_head *dispatch;
	octeon_dispatch_fn_t fn = NULL;
	u16 combined_opcode = OPCODE_SUBCODE(opcode, subcode);

	idx = combined_opcode & OCTEON_OPCODE_MASK;

	spin_lock_bh(&octeon_dev->dispatch.lock);

	if (octeon_dev->dispatch.count == 0) {
		spin_unlock_bh(&octeon_dev->dispatch.lock);
		return NULL;
	}

	if (!(octeon_dev->dispatch.dlist[idx].opcode)) {
		spin_unlock_bh(&octeon_dev->dispatch.lock);
		return NULL;
	}

	if (octeon_dev->dispatch.dlist[idx].opcode == combined_opcode) {
		fn = octeon_dev->dispatch.dlist[idx].dispatch_fn;
	} else {
		list_for_each(dispatch,
			      &octeon_dev->dispatch.dlist[idx].list) {
			if (((struct octeon_dispatch *)dispatch)->opcode ==
			    combined_opcode) {
				fn = ((struct octeon_dispatch *)
				      dispatch)->dispatch_fn;
				break;
			}
		}
	}

	spin_unlock_bh(&octeon_dev->dispatch.lock);
	return fn;
}

/* octeon_register_dispatch_fn
 * Parameters:
 *   octeon_id - id of the octeon device.
 *   opcode    - opcode for which driver should call the registered function
 *   subcode   - subcode for which driver should call the registered function
 *   fn        - The function to call when a packet with "opcode" arrives in
 *		  octeon output queues.
 *   fn_arg    - The argument to be passed when calling function "fn".
 * Description:
 *   Registers a function and its argument to be called when a packet
 *   arrives in Octeon output queues with "opcode".
 * Returns:
 *   Success: 0
 *   Failure: 1
 * Locks:
 *   No locks are held.
 */
int
octeon_register_dispatch_fn(struct octeon_device *oct,
			    u16 opcode,
			    u16 subcode,
			    octeon_dispatch_fn_t fn, void *fn_arg)
{
	u32 idx;
	octeon_dispatch_fn_t pfn;
	u16 combined_opcode = OPCODE_SUBCODE(opcode, subcode);

	idx = combined_opcode & OCTEON_OPCODE_MASK;

	spin_lock_bh(&oct->dispatch.lock);
	/* Add dispatch function to first level of lookup table */
	if (oct->dispatch.dlist[idx].opcode == 0) {
		oct->dispatch.dlist[idx].opcode = combined_opcode;
		oct->dispatch.dlist[idx].dispatch_fn = fn;
		oct->dispatch.dlist[idx].arg = fn_arg;
		oct->dispatch.count++;
		spin_unlock_bh(&oct->dispatch.lock);
		return 0;
	}

	spin_unlock_bh(&oct->dispatch.lock);

	/* Check if there was a function already registered for this
	 * opcode/subcode.
	 */
	pfn = octeon_get_dispatch(oct, opcode, subcode);
	if (!pfn) {
		struct octeon_dispatch *dispatch;

		dev_dbg(&oct->pci_dev->dev,
			"Adding opcode to dispatch list linked list\n");
		dispatch = kmalloc(sizeof(*dispatch), GFP_KERNEL);
		if (!dispatch)
			return 1;

		dispatch->opcode = combined_opcode;
		dispatch->dispatch_fn = fn;
		dispatch->arg = fn_arg;

		/* Add dispatch function to linked list of fn ptrs
		 * at the hashed index.
		 */
		spin_lock_bh(&oct->dispatch.lock);
		list_add(&dispatch->list, &oct->dispatch.dlist[idx].list);
		oct->dispatch.count++;
		spin_unlock_bh(&oct->dispatch.lock);

	} else {
		if (pfn == fn &&
		    octeon_get_dispatch_arg(oct, opcode, subcode) == fn_arg)
			return 0;

		dev_err(&oct->pci_dev->dev,
			"Found previously registered dispatch fn for opcode/subcode: %x/%x\n",
			opcode, subcode);
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(octeon_register_dispatch_fn);

int octeon_core_drv_init(struct octeon_recv_info *recv_info, void *buf)
{
	u32 i;
	char app_name[16];
	struct octeon_device *oct = (struct octeon_device *)buf;
	struct octeon_recv_pkt *recv_pkt = recv_info->recv_pkt;
	struct octeon_core_setup *cs = NULL;
	u32 num_nic_ports = 0;

	if (OCTEON_CN6XXX(oct))
		num_nic_ports =
			CFG_GET_NUM_NIC_PORTS(CHIP_CONF(oct, cn6xxx));
	else if (OCTEON_CN23XX_PF(oct))
		num_nic_ports =
			CFG_GET_NUM_NIC_PORTS(CHIP_CONF(oct, cn23xx_pf));

	if (atomic_read(&oct->status) >= OCT_DEV_RUNNING) {
		dev_err(&oct->pci_dev->dev, "Received CORE OK when device state is 0x%x\n",
			atomic_read(&oct->status));
		goto core_drv_init_err;
	}

	strscpy(app_name,
		get_oct_app_string(
		(u32)recv_pkt->rh.r_core_drv_init.app_mode),
		sizeof(app_name));
	oct->app_mode = (u32)recv_pkt->rh.r_core_drv_init.app_mode;
	if (recv_pkt->rh.r_core_drv_init.app_mode == CVM_DRV_NIC_APP) {
		oct->fw_info.max_nic_ports =
			(u32)recv_pkt->rh.r_core_drv_init.max_nic_ports;
		oct->fw_info.num_gmx_ports =
			(u32)recv_pkt->rh.r_core_drv_init.num_gmx_ports;
	}

	if (oct->fw_info.max_nic_ports < num_nic_ports) {
		dev_err(&oct->pci_dev->dev,
			"Config has more ports than firmware allows (%d > %d).\n",
			num_nic_ports, oct->fw_info.max_nic_ports);
		goto core_drv_init_err;
	}
	oct->fw_info.app_cap_flags = recv_pkt->rh.r_core_drv_init.app_cap_flags;
	oct->fw_info.app_mode = (u32)recv_pkt->rh.r_core_drv_init.app_mode;
	oct->pfvf_hsword.app_mode = (u32)recv_pkt->rh.r_core_drv_init.app_mode;

	oct->pfvf_hsword.pkind = recv_pkt->rh.r_core_drv_init.pkind;

	for (i = 0; i < oct->num_iqs; i++)
		oct->instr_queue[i]->txpciq.s.pkind = oct->pfvf_hsword.pkind;

	atomic_set(&oct->status, OCT_DEV_CORE_OK);

	cs = &core_setup[oct->octeon_id];

	if (recv_pkt->buffer_size[0] != (sizeof(*cs) + OCT_DROQ_INFO_SIZE)) {
		dev_dbg(&oct->pci_dev->dev, "Core setup bytes expected %u found %d\n",
			(u32)sizeof(*cs),
			recv_pkt->buffer_size[0]);
	}

	memcpy(cs, get_rbd(
	       recv_pkt->buffer_ptr[0]) + OCT_DROQ_INFO_SIZE, sizeof(*cs));

	strscpy(oct->boardinfo.name, cs->boardname,
		    sizeof(oct->boardinfo.name));
	strscpy(oct->boardinfo.serial_number, cs->board_serial_number,
		    sizeof(oct->boardinfo.serial_number));

	octeon_swap_8B_data((u64 *)cs, (sizeof(*cs) >> 3));

	oct->boardinfo.major = cs->board_rev_major;
	oct->boardinfo.minor = cs->board_rev_minor;

	dev_info(&oct->pci_dev->dev,
		 "Running %s (%llu Hz)\n",
		 app_name, CVM_CAST64(cs->corefreq));

core_drv_init_err:
	for (i = 0; i < recv_pkt->buffer_count; i++)
		recv_buffer_free(recv_pkt->buffer_ptr[i]);
	octeon_free_recv_info(recv_info);
	return 0;
}
EXPORT_SYMBOL_GPL(octeon_core_drv_init);

int octeon_get_tx_qsize(struct octeon_device *oct, u32 q_no)

{
	if (oct && (q_no < MAX_OCTEON_INSTR_QUEUES(oct)) &&
	    (oct->io_qmask.iq & BIT_ULL(q_no)))
		return oct->instr_queue[q_no]->max_count;

	return -1;
}
EXPORT_SYMBOL_GPL(octeon_get_tx_qsize);

int octeon_get_rx_qsize(struct octeon_device *oct, u32 q_no)
{
	if (oct && (q_no < MAX_OCTEON_OUTPUT_QUEUES(oct)) &&
	    (oct->io_qmask.oq & BIT_ULL(q_no)))
		return oct->droq[q_no]->max_count;
	return -1;
}
EXPORT_SYMBOL_GPL(octeon_get_rx_qsize);

/* Retruns the host firmware handshake OCTEON specific configuration */
struct octeon_config *octeon_get_conf(struct octeon_device *oct)
{
	struct octeon_config *default_oct_conf = NULL;

	/* check the OCTEON Device model & return the corresponding octeon
	 * configuration
	 */

	if (OCTEON_CN6XXX(oct)) {
		default_oct_conf =
			(struct octeon_config *)(CHIP_CONF(oct, cn6xxx));
	} else if (OCTEON_CN23XX_PF(oct)) {
		default_oct_conf = (struct octeon_config *)
			(CHIP_CONF(oct, cn23xx_pf));
	} else if (OCTEON_CN23XX_VF(oct)) {
		default_oct_conf = (struct octeon_config *)
			(CHIP_CONF(oct, cn23xx_vf));
	}
	return default_oct_conf;
}
EXPORT_SYMBOL_GPL(octeon_get_conf);

/* scratch register address is same in all the OCT-II and CN70XX models */
#define CNXX_SLI_SCRATCH1   0x3C0

/* Get the octeon device pointer.
 *  @param octeon_id  - The id for which the octeon device pointer is required.
 *  @return Success: Octeon device pointer.
 *  @return Failure: NULL.
 */
struct octeon_device *lio_get_device(u32 octeon_id)
{
	if (octeon_id >= MAX_OCTEON_DEVICES)
		return NULL;
	else
		return octeon_device[octeon_id];
}
EXPORT_SYMBOL_GPL(lio_get_device);

u64 lio_pci_readq(struct octeon_device *oct, u64 addr)
{
	u64 val64;
	unsigned long flags;
	u32 addrhi;

	spin_lock_irqsave(&oct->pci_win_lock, flags);

	/* The windowed read happens when the LSB of the addr is written.
	 * So write MSB first
	 */
	addrhi = (addr >> 32);
	if ((oct->chip_id == OCTEON_CN66XX) ||
	    (oct->chip_id == OCTEON_CN68XX) ||
	    (oct->chip_id == OCTEON_CN23XX_PF_VID))
		addrhi |= 0x00060000;
	writel(addrhi, oct->reg_list.pci_win_rd_addr_hi);

	/* Read back to preserve ordering of writes */
	readl(oct->reg_list.pci_win_rd_addr_hi);

	writel(addr & 0xffffffff, oct->reg_list.pci_win_rd_addr_lo);
	readl(oct->reg_list.pci_win_rd_addr_lo);

	val64 = readq(oct->reg_list.pci_win_rd_data);

	spin_unlock_irqrestore(&oct->pci_win_lock, flags);

	return val64;
}
EXPORT_SYMBOL_GPL(lio_pci_readq);

void lio_pci_writeq(struct octeon_device *oct,
		    u64 val,
		    u64 addr)
{
	unsigned long flags;

	spin_lock_irqsave(&oct->pci_win_lock, flags);

	writeq(addr, oct->reg_list.pci_win_wr_addr);

	/* The write happens when the LSB is written. So write MSB first. */
	writel(val >> 32, oct->reg_list.pci_win_wr_data_hi);
	/* Read the MSB to ensure ordering of writes. */
	readl(oct->reg_list.pci_win_wr_data_hi);

	writel(val & 0xffffffff, oct->reg_list.pci_win_wr_data_lo);

	spin_unlock_irqrestore(&oct->pci_win_lock, flags);
}
EXPORT_SYMBOL_GPL(lio_pci_writeq);

int octeon_mem_access_ok(struct octeon_device *oct)
{
	u64 access_okay = 0;
	u64 lmc0_reset_ctl;

	/* Check to make sure a DDR interface is enabled */
	if (OCTEON_CN23XX_PF(oct)) {
		lmc0_reset_ctl = lio_pci_readq(oct, CN23XX_LMC0_RESET_CTL);
		access_okay =
			(lmc0_reset_ctl & CN23XX_LMC0_RESET_CTL_DDR3RST_MASK);
	} else {
		lmc0_reset_ctl = lio_pci_readq(oct, CN6XXX_LMC0_RESET_CTL);
		access_okay =
			(lmc0_reset_ctl & CN6XXX_LMC0_RESET_CTL_DDR3RST_MASK);
	}

	return access_okay ? 0 : 1;
}
EXPORT_SYMBOL_GPL(octeon_mem_access_ok);

int octeon_wait_for_ddr_init(struct octeon_device *oct, u32 *timeout)
{
	int ret = 1;
	u32 ms;

	if (!timeout)
		return ret;

	for (ms = 0; (ret != 0) && ((*timeout == 0) || (ms <= *timeout));
	     ms += HZ / 10) {
		ret = octeon_mem_access_ok(oct);

		/* wait 100 ms */
		if (ret)
			schedule_timeout_uninterruptible(HZ / 10);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(octeon_wait_for_ddr_init);

/* Get the octeon id assigned to the octeon device passed as argument.
 *  This function is exported to other modules.
 *  @param dev - octeon device pointer passed as a void *.
 *  @return octeon device id
 */
int lio_get_device_id(void *dev)
{
	struct octeon_device *octeon_dev = (struct octeon_device *)dev;
	u32 i;

	for (i = 0; i < MAX_OCTEON_DEVICES; i++)
		if (octeon_device[i] == octeon_dev)
			return octeon_dev->octeon_id;
	return -1;
}

void lio_enable_irq(struct octeon_droq *droq, struct octeon_instr_queue *iq)
{
	u64 instr_cnt;
	u32 pkts_pend;
	struct octeon_device *oct = NULL;

	/* the whole thing needs to be atomic, ideally */
	if (droq) {
		pkts_pend = (u32)atomic_read(&droq->pkts_pending);
		writel(droq->pkt_count - pkts_pend, droq->pkts_sent_reg);
		droq->pkt_count = pkts_pend;
		oct = droq->oct_dev;
	}
	if (iq) {
		spin_lock_bh(&iq->lock);
		writel(iq->pkts_processed, iq->inst_cnt_reg);
		iq->pkt_in_done -= iq->pkts_processed;
		iq->pkts_processed = 0;
		/* this write needs to be flushed before we release the lock */
		spin_unlock_bh(&iq->lock);
		oct = iq->oct_dev;
	}
	/*write resend. Writing RESEND in SLI_PKTX_CNTS should be enough
	 *to trigger tx interrupts as well, if they are pending.
	 */
	if (oct && (OCTEON_CN23XX_PF(oct) || OCTEON_CN23XX_VF(oct))) {
		if (droq)
			writeq(CN23XX_INTR_RESEND, droq->pkts_sent_reg);
		/*we race with firmrware here. read and write the IN_DONE_CNTS*/
		else if (iq) {
			instr_cnt =  readq(iq->inst_cnt_reg);
			writeq(((instr_cnt & 0xFFFFFFFF00000000ULL) |
				CN23XX_INTR_RESEND),
			       iq->inst_cnt_reg);
		}
	}
}
EXPORT_SYMBOL_GPL(lio_enable_irq);
