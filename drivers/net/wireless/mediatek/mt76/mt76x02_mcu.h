/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#ifndef __MT76x02_MCU_H
#define __MT76x02_MCU_H

#include "mt76x02.h"

#define MT_MCU_RESET_CTL		0x070C
#define MT_MCU_INT_LEVEL		0x0718
#define MT_MCU_COM_REG0			0x0730
#define MT_MCU_COM_REG1			0x0734
#define MT_MCU_COM_REG2			0x0738
#define MT_MCU_COM_REG3			0x073C

#define MT_INBAND_PACKET_MAX_LEN	192
#define MT_MCU_MEMMAP_WLAN		0x410000

#define MT_MCU_PCIE_REMAP_BASE4		0x074C

#define MT_MCU_SEMAPHORE_00		0x07B0
#define MT_MCU_SEMAPHORE_01		0x07B4
#define MT_MCU_SEMAPHORE_02		0x07B8
#define MT_MCU_SEMAPHORE_03		0x07BC

#define MT_MCU_ILM_ADDR			0x80000

enum mcu_cmd {
	CMD_FUN_SET_OP = 1,
	CMD_LOAD_CR = 2,
	CMD_INIT_GAIN_OP = 3,
	CMD_DYNC_VGA_OP = 6,
	CMD_TDLS_CH_SW = 7,
	CMD_BURST_WRITE = 8,
	CMD_READ_MODIFY_WRITE = 9,
	CMD_RANDOM_READ = 10,
	CMD_BURST_READ = 11,
	CMD_RANDOM_WRITE = 12,
	CMD_LED_MODE_OP = 16,
	CMD_POWER_SAVING_OP = 20,
	CMD_WOW_CONFIG = 21,
	CMD_WOW_QUERY = 22,
	CMD_WOW_FEATURE = 24,
	CMD_CARRIER_DETECT_OP = 28,
	CMD_RADOR_DETECT_OP = 29,
	CMD_SWITCH_CHANNEL_OP = 30,
	CMD_CALIBRATION_OP = 31,
	CMD_BEACON_OP = 32,
	CMD_ANTENNA_OP = 33,
};

enum mcu_power_mode {
	RADIO_OFF = 0x30,
	RADIO_ON = 0x31,
	RADIO_OFF_AUTO_WAKEUP = 0x32,
	RADIO_OFF_ADVANCE = 0x33,
	RADIO_ON_ADVANCE = 0x34,
};

enum mcu_function {
	Q_SELECT = 1,
	BW_SETTING = 2,
	USB2_SW_DISCONNECT = 2,
	USB3_SW_DISCONNECT = 3,
	LOG_FW_DEBUG_MSG = 4,
	GET_FW_VERSION = 5,
};

struct mt76x02_fw_header {
	__le32 ilm_len;
	__le32 dlm_len;
	__le16 build_ver;
	__le16 fw_ver;
	u8 pad[4];
	char build_time[16];
};

struct mt76x02_patch_header {
	char build_time[16];
	char platform[4];
	char hw_version[4];
	char patch_version[4];
	u8 pad[2];
};

static inline struct sk_buff *
mt76x02_mcu_msg_alloc(const void *data, int len)
{
	return mt76_mcu_msg_alloc(data, 0, len, 0);
}

int mt76x02_mcu_cleanup(struct mt76x02_dev *dev);
int mt76x02_mcu_calibrate(struct mt76x02_dev *dev, int type, u32 param);
int mt76x02_mcu_msg_send(struct mt76_dev *mdev, int cmd, const void *data,
			 int len, bool wait_resp);
int mt76x02_mcu_function_select(struct mt76x02_dev *dev, enum mcu_function func,
				u32 val);
int mt76x02_mcu_set_radio_state(struct mt76x02_dev *dev, bool on);
void mt76x02_set_ethtool_fwver(struct mt76x02_dev *dev,
			       const struct mt76x02_fw_header *h);

#endif /* __MT76x02_MCU_H */
