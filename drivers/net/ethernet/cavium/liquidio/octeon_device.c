/**********************************************************************
* Author: Cavium, Inc.
*
* Contact: support@cavium.com
*          Please include "LiquidIO" in the subject.
*
* Copyright (c) 2003-2015 Cavium, Inc.
*
* This file is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, Version 2, as
* published by the Free Software Foundation.
*
* This file is distributed in the hope that it will be useful, but
* AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
* NONINFRINGEMENT.  See the GNU General Public License for more
* details.
*
* This file may also be available under a different license from Cavium.
* Contact Cavium, Inc. for more information
**********************************************************************/
#include <linux/version.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/crc32.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include "octeon_config.h"
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_nic.h"
#include "octeon_main.h"
#include "octeon_network.h"
#include "cn66xx_regs.h"
#include "cn66xx_device.h"
#include "cn68xx_regs.h"
#include "cn68xx_device.h"
#include "liquidio_image.h"
#include "octeon_mem_ops.h"

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
		.info_ptr			= OCTEON_OQ_INFOPTR_MODE,
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
		.info_ptr			= OCTEON_OQ_INFOPTR_MODE,
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
		.info_ptr			= OCTEON_OQ_INFOPTR_MODE,
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

enum {
	OCTEON_CONFIG_TYPE_DEFAULT = 0,
	NUM_OCTEON_CONFS,
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
	"BEGIN",	"PCI-MAP-DONE",	      "DISPATCH-INIT-DONE",
	"IQ-INIT-DONE", "SCBUFF-POOL-INIT-DONE", "RESPLIST-INIT-DONE",
	"DROQ-INIT-DONE", "IO-QUEUES-INIT-DONE", "CONSOLE-INIT-DONE",
	"HOST-READY",	"CORE-READY",	      "RUNNING",	   "IN-RESET",
	"INVALID"
};

static char oct_dev_app_str[CVM_DRV_APP_COUNT + 1][32] = {
	"BASE", "NIC", "UNKNOWN"};

static struct octeon_device *octeon_device[MAX_OCTEON_DEVICES];
static u32 octeon_device_count;

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

static void *__retrieve_octeon_config_info(struct octeon_device *oct,
					   u16 card_type)
{
	u32 oct_id = oct->octeon_id;
	void *ret = NULL;

	switch (oct_conf_info[oct_id].conf_type) {
	case OCTEON_CONFIG_TYPE_DEFAULT:
		if (oct->chip_id == OCTEON_CN66XX) {
			ret = (void *)&default_cn66xx_conf;
		} else if ((oct->chip_id == OCTEON_CN68XX) &&
			   (card_type == LIO_210NV)) {
			ret =  (void *)&default_cn68xx_210nv_conf;
		} else if ((oct->chip_id == OCTEON_CN68XX) &&
			   (card_type == LIO_410NV)) {
			ret =  (void *)&default_cn68xx_conf;
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

static char *get_oct_app_string(u32 app_mode)
{
	if (app_mode <= CVM_DRV_APP_END)
		return oct_dev_app_str[app_mode - CVM_DRV_APP_START];
	return oct_dev_app_str[CVM_DRV_INVALID_APP - CVM_DRV_APP_START];
}

int octeon_download_firmware(struct octeon_device *oct, const u8 *data,
			     size_t size)
{
	int ret = 0;
	u8 *p;
	u8 *buffer;
	u32 crc32_result;
	u64 load_addr;
	u32 image_len;
	struct octeon_firmware_file_header *h;
	u32 i;

	if (size < sizeof(struct octeon_firmware_file_header)) {
		dev_err(&oct->pci_dev->dev, "Firmware file too small (%d < %d).\n",
			(u32)size,
			(u32)sizeof(struct octeon_firmware_file_header));
		return -EINVAL;
	}

	h = (struct octeon_firmware_file_header *)data;

	if (be32_to_cpu(h->magic) != LIO_NIC_MAGIC) {
		dev_err(&oct->pci_dev->dev, "Unrecognized firmware file.\n");
		return -EINVAL;
	}

	crc32_result =
		crc32(~0, data,
		      sizeof(struct octeon_firmware_file_header) -
		      sizeof(u32)) ^ ~0U;
	if (crc32_result != be32_to_cpu(h->crc32)) {
		dev_err(&oct->pci_dev->dev, "Firmware CRC mismatch (0x%08x != 0x%08x).\n",
			crc32_result, be32_to_cpu(h->crc32));
		return -EINVAL;
	}

	if (memcmp(LIQUIDIO_VERSION, h->version, strlen(LIQUIDIO_VERSION))) {
		dev_err(&oct->pci_dev->dev, "Unmatched firmware version. Expected %s, got %s.\n",
			LIQUIDIO_VERSION, h->version);
		return -EINVAL;
	}

	if (be32_to_cpu(h->num_images) > LIO_MAX_IMAGES) {
		dev_err(&oct->pci_dev->dev, "Too many images in firmware file (%d).\n",
			be32_to_cpu(h->num_images));
		return -EINVAL;
	}

	dev_info(&oct->pci_dev->dev, "Firmware version: %s\n", h->version);
	snprintf(oct->fw_info.liquidio_firmware_version, 32, "LIQUIDIO: %s",
		 h->version);

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	memcpy(buffer, data, size);

	p = buffer + sizeof(struct octeon_firmware_file_header);

	/* load all images */
	for (i = 0; i < be32_to_cpu(h->num_images); i++) {
		load_addr = be64_to_cpu(h->desc[i].addr);
		image_len = be32_to_cpu(h->desc[i].len);

		/* validate the image */
		crc32_result = crc32(~0, p, image_len) ^ ~0U;
		if (crc32_result != be32_to_cpu(h->desc[i].crc32)) {
			dev_err(&oct->pci_dev->dev,
				"Firmware CRC mismatch in image %d (0x%08x != 0x%08x).\n",
				i, crc32_result,
				be32_to_cpu(h->desc[i].crc32));
			ret = -EINVAL;
			goto done_downloading;
		}

		/* download the image */
		octeon_pci_write_core_mem(oct, load_addr, p, image_len);

		p += image_len;
		dev_dbg(&oct->pci_dev->dev,
			"Downloaded image %d (%d bytes) to address 0x%016llx\n",
			i, image_len, load_addr);
	}

	/* Invoke the bootcmd */
	ret = octeon_console_send_cmd(oct, h->bootcmd, 50);

done_downloading:
	kfree(buffer);

	return ret;
}

void octeon_free_device_mem(struct octeon_device *oct)
{
	u32 i;

	for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES; i++) {
		/* could check  mask as well */
		vfree(oct->droq[i]);
	}

	for (i = 0; i < MAX_OCTEON_INSTR_QUEUES; i++) {
		/* could check mask as well */
		vfree(oct->instr_queue[i]);
	}

	i = oct->octeon_id;
	vfree(oct);

	octeon_device[i] = NULL;
	octeon_device_count--;
}

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

	buf = vmalloc(size);
	if (!buf)
		return NULL;

	memset(buf, 0, size);

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

	for (oct_idx = 0; oct_idx < MAX_OCTEON_DEVICES; oct_idx++)
		if (!octeon_device[oct_idx])
			break;

	if (oct_idx == MAX_OCTEON_DEVICES)
		return NULL;

	oct = octeon_allocate_device_mem(pci_id, priv_size);
	if (!oct)
		return NULL;

	spin_lock_init(&oct->pci_win_lock);
	spin_lock_init(&oct->mem_access_lock);

	octeon_device_count++;
	octeon_device[oct_idx] = oct;

	oct->octeon_id = oct_idx;
	snprintf((oct->device_name), sizeof(oct->device_name),
		 "LiquidIO%d", (oct->octeon_id));

	return oct;
}

int octeon_setup_instr_queues(struct octeon_device *oct)
{
	u32 i, num_iqs = 0;
	u32 num_descs = 0;

	/* this causes queue 0 to be default queue */
	if (OCTEON_CN6XXX(oct)) {
		num_iqs = 1;
		num_descs =
			CFG_GET_NUM_DEF_TX_DESCS(CHIP_FIELD(oct, cn6xxx, conf));
	}

	oct->num_iqs = 0;

	for (i = 0; i < num_iqs; i++) {
		oct->instr_queue[i] =
			vmalloc(sizeof(struct octeon_instr_queue));
		if (!oct->instr_queue[i])
			return 1;

		memset(oct->instr_queue[i], 0,
		       sizeof(struct octeon_instr_queue));

		oct->instr_queue[i]->app_ctx = (void *)(size_t)i;
		if (octeon_init_instr_queue(oct, i, num_descs))
			return 1;

		oct->num_iqs++;
	}

	return 0;
}

int octeon_setup_output_queues(struct octeon_device *oct)
{
	u32 i, num_oqs = 0;
	u32 num_descs = 0;
	u32 desc_size = 0;

	/* this causes queue 0 to be default queue */
	if (OCTEON_CN6XXX(oct)) {
		/* CFG_GET_OQ_MAX_BASE_Q(CHIP_FIELD(oct, cn6xxx, conf)); */
		num_oqs = 1;
		num_descs =
			CFG_GET_NUM_DEF_RX_DESCS(CHIP_FIELD(oct, cn6xxx, conf));
		desc_size =
			CFG_GET_DEF_RX_BUF_SIZE(CHIP_FIELD(oct, cn6xxx, conf));
	}

	oct->num_oqs = 0;

	for (i = 0; i < num_oqs; i++) {
		oct->droq[i] = vmalloc(sizeof(*oct->droq[i]));
		if (!oct->droq[i])
			return 1;

		memset(oct->droq[i], 0, sizeof(struct octeon_droq));

		if (octeon_init_droq(oct, i, num_descs, desc_size, NULL))
			return 1;

		oct->num_oqs++;
	}

	return 0;
}

void octeon_set_io_queues_off(struct octeon_device *oct)
{
	/* Disable the i/p and o/p queues for this Octeon. */

	octeon_write_csr(oct, CN6XXX_SLI_PKT_INSTR_ENB, 0);
	octeon_write_csr(oct, CN6XXX_SLI_PKT_OUT_ENB, 0);
}

void octeon_set_droq_pkt_op(struct octeon_device *oct,
			    u32 q_no,
			    u32 enable)
{
	u32 reg_val = 0;

	/* Disable the i/p and o/p queues for this Octeon. */
	reg_val = octeon_read_csr(oct, CN6XXX_SLI_PKT_OUT_ENB);

	if (enable)
		reg_val = reg_val | (1 << q_no);
	else
		reg_val = reg_val & (~(1 << q_no));

	octeon_write_csr(oct, CN6XXX_SLI_PKT_OUT_ENB, reg_val);
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
			list_del(temp);
			list_add_tail(temp, &freelist);
		}

		oct->dispatch.dlist[i].opcode = 0;
	}

	oct->dispatch.count = 0;

	spin_unlock_bh(&oct->dispatch.lock);

	list_for_each_safe(temp, tmp2, &freelist) {
		list_del(temp);
		vfree(temp);
	}
}

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
		dispatch = (struct octeon_dispatch *)
			   vmalloc(sizeof(struct octeon_dispatch));
		if (!dispatch) {
			dev_err(&oct->pci_dev->dev,
				"No memory to add dispatch function\n");
			return 1;
		}
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
		dev_err(&oct->pci_dev->dev,
			"Found previously registered dispatch fn for opcode/subcode: %x/%x\n",
			opcode, subcode);
		return 1;
	}

	return 0;
}

/* octeon_unregister_dispatch_fn
 * Parameters:
 *   oct       - octeon device
 *   opcode    - driver should unregister the function for this opcode
 *   subcode   - driver should unregister the function for this subcode
 * Description:
 *   Unregister the function set for this opcode+subcode.
 * Returns:
 *   Success: 0
 *   Failure: 1
 * Locks:
 *   No locks are held.
 */
int
octeon_unregister_dispatch_fn(struct octeon_device *oct, u16 opcode,
			      u16 subcode)
{
	int retval = 0;
	u32 idx;
	struct list_head *dispatch, *dfree = NULL, *tmp2;
	u16 combined_opcode = OPCODE_SUBCODE(opcode, subcode);

	idx = combined_opcode & OCTEON_OPCODE_MASK;

	spin_lock_bh(&oct->dispatch.lock);

	if (oct->dispatch.count == 0) {
		spin_unlock_bh(&oct->dispatch.lock);
		dev_err(&oct->pci_dev->dev,
			"No dispatch functions registered for this device\n");
		return 1;
	}

	if (oct->dispatch.dlist[idx].opcode == combined_opcode) {
		dispatch = &oct->dispatch.dlist[idx].list;
		if (dispatch->next != dispatch) {
			dispatch = dispatch->next;
			oct->dispatch.dlist[idx].opcode =
				((struct octeon_dispatch *)dispatch)->opcode;
			oct->dispatch.dlist[idx].dispatch_fn =
				((struct octeon_dispatch *)
				 dispatch)->dispatch_fn;
			oct->dispatch.dlist[idx].arg =
				((struct octeon_dispatch *)dispatch)->arg;
			list_del(dispatch);
			dfree = dispatch;
		} else {
			oct->dispatch.dlist[idx].opcode = 0;
			oct->dispatch.dlist[idx].dispatch_fn = NULL;
			oct->dispatch.dlist[idx].arg = NULL;
		}
	} else {
		retval = 1;
		list_for_each_safe(dispatch, tmp2,
				   &(oct->dispatch.dlist[idx].
				     list)) {
			if (((struct octeon_dispatch *)dispatch)->opcode ==
			    combined_opcode) {
				list_del(dispatch);
				dfree = dispatch;
				retval = 0;
			}
		}
	}

	if (!retval)
		oct->dispatch.count--;

	spin_unlock_bh(&oct->dispatch.lock);
	vfree(dfree);
	return retval;
}

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
			CFG_GET_NUM_NIC_PORTS(CHIP_FIELD(oct, cn6xxx, conf));

	if (atomic_read(&oct->status) >= OCT_DEV_RUNNING) {
		dev_err(&oct->pci_dev->dev, "Received CORE OK when device state is 0x%x\n",
			atomic_read(&oct->status));
		goto core_drv_init_err;
	}

	strncpy(app_name,
		get_oct_app_string(
		(u32)recv_pkt->rh.r_core_drv_init.app_mode),
		sizeof(app_name) - 1);
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

	atomic_set(&oct->status, OCT_DEV_CORE_OK);

	cs = &core_setup[oct->octeon_id];

	if (recv_pkt->buffer_size[0] != sizeof(*cs)) {
		dev_dbg(&oct->pci_dev->dev, "Core setup bytes expected %u found %d\n",
			(u32)sizeof(*cs),
			recv_pkt->buffer_size[0]);
	}

	memcpy(cs, get_rbd(recv_pkt->buffer_ptr[0]), sizeof(*cs));
	strncpy(oct->boardinfo.name, cs->boardname, OCT_BOARD_NAME);
	strncpy(oct->boardinfo.serial_number, cs->board_serial_number,
		OCT_SERIAL_LEN);

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

int octeon_get_tx_qsize(struct octeon_device *oct, u32 q_no)

{
	if (oct && (q_no < MAX_OCTEON_INSTR_QUEUES) &&
	    (oct->io_qmask.iq & (1UL << q_no)))
		return oct->instr_queue[q_no]->max_count;

	return -1;
}

int octeon_get_rx_qsize(struct octeon_device *oct, u32 q_no)
{
	if (oct && (q_no < MAX_OCTEON_OUTPUT_QUEUES) &&
	    (oct->io_qmask.oq & (1UL << q_no)))
		return oct->droq[q_no]->max_count;
	return -1;
}

/* Retruns the host firmware handshake OCTEON specific configuration */
struct octeon_config *octeon_get_conf(struct octeon_device *oct)
{
	struct octeon_config *default_oct_conf = NULL;

	/* check the OCTEON Device model & return the corresponding octeon
	 * configuration
	 */

	if (OCTEON_CN6XXX(oct)) {
		default_oct_conf =
			(struct octeon_config *)(CHIP_FIELD(oct, cn6xxx, conf));
	}

	return default_oct_conf;
}

/* scratch register address is same in all the OCT-II and CN70XX models */
#define CNXX_SLI_SCRATCH1   0x3C0

/** Get the octeon device pointer.
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

u64 lio_pci_readq(struct octeon_device *oct, u64 addr)
{
	u64 val64;
	unsigned long flags;
	u32 val32, addrhi;

	spin_lock_irqsave(&oct->pci_win_lock, flags);

	/* The windowed read happens when the LSB of the addr is written.
	 * So write MSB first
	 */
	addrhi = (addr >> 32);
	if ((oct->chip_id == OCTEON_CN66XX) || (oct->chip_id == OCTEON_CN68XX))
		addrhi |= 0x00060000;
	writel(addrhi, oct->reg_list.pci_win_rd_addr_hi);

	/* Read back to preserve ordering of writes */
	val32 = readl(oct->reg_list.pci_win_rd_addr_hi);

	writel(addr & 0xffffffff, oct->reg_list.pci_win_rd_addr_lo);
	val32 = readl(oct->reg_list.pci_win_rd_addr_lo);

	val64 = readq(oct->reg_list.pci_win_rd_data);

	spin_unlock_irqrestore(&oct->pci_win_lock, flags);

	return val64;
}

void lio_pci_writeq(struct octeon_device *oct,
		    u64 val,
		    u64 addr)
{
	u32 val32;
	unsigned long flags;

	spin_lock_irqsave(&oct->pci_win_lock, flags);

	writeq(addr, oct->reg_list.pci_win_wr_addr);

	/* The write happens when the LSB is written. So write MSB first. */
	writel(val >> 32, oct->reg_list.pci_win_wr_data_hi);
	/* Read the MSB to ensure ordering of writes. */
	val32 = readl(oct->reg_list.pci_win_wr_data_hi);

	writel(val & 0xffffffff, oct->reg_list.pci_win_wr_data_lo);

	spin_unlock_irqrestore(&oct->pci_win_lock, flags);
}

int octeon_mem_access_ok(struct octeon_device *oct)
{
	u64 access_okay = 0;

	/* Check to make sure a DDR interface is enabled */
	u64 lmc0_reset_ctl = lio_pci_readq(oct, CN6XXX_LMC0_RESET_CTL);

	access_okay = (lmc0_reset_ctl & CN6XXX_LMC0_RESET_CTL_DDR3RST_MASK);

	return access_okay ? 0 : 1;
}

int octeon_wait_for_ddr_init(struct octeon_device *oct, u32 *timeout)
{
	int ret = 1;
	u32 ms;

	if (!timeout)
		return ret;

	while (*timeout == 0)
		schedule_timeout_uninterruptible(HZ / 10);

	for (ms = 0; (ret != 0) && ((*timeout == 0) || (ms <= *timeout));
	     ms += HZ / 10) {
		ret = octeon_mem_access_ok(oct);

		/* wait 100 ms */
		if (ret)
			schedule_timeout_uninterruptible(HZ / 10);
	}

	return ret;
}

/** Get the octeon id assigned to the octeon device passed as argument.
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
