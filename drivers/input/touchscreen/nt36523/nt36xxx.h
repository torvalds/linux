/*
 * Copyright (C) 2010 - 2018 Novatek, Inc.
 * Copyright (C) 2021 XiaoMi, Inc.
 *
 * $Revision: 69262 $
 * $Date: 2020-09-23 15:07:14 +0800 (週三, 23 九月 2020) $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#ifndef 	_LINUX_NVT_TOUCH_H
#define		_LINUX_NVT_TOUCH_H

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/of.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>

#include "nt36xxx_mem_map.h"

#define NVT_DEBUG 1

//---GPIO number---
#define NVTTOUCH_RST_PIN 980
#define NVTTOUCH_INT_PIN 943

#define PINCTRL_STATE_ACTIVE		"pmx_ts_active"
#define PINCTRL_STATE_SUSPEND		"pmx_ts_suspend"

//---INT trigger mode---
//#define IRQ_TYPE_EDGE_RISING 1
//#define IRQ_TYPE_EDGE_FALLING 2
#define INT_TRIGGER_TYPE IRQ_TYPE_EDGE_RISING


//---SPI driver info.---
#define NVT_SPI_NAME "NVT-ts-spi"

#if NVT_DEBUG
#define NVT_LOG(fmt, args...)    pr_err("[%s] %s %d: " fmt, NVT_SPI_NAME, __func__, __LINE__, ##args)
#else
#define NVT_LOG(fmt, args...)    pr_info("[%s] %s %d: " fmt, NVT_SPI_NAME, __func__, __LINE__, ##args)
#endif
#define NVT_ERR(fmt, args...)    pr_err("[%s] %s %d: " fmt, NVT_SPI_NAME, __func__, __LINE__, ##args)

//---Input device info.---
#define NVT_TS_NAME "NVTCapacitiveTouchScreen"
#define NVT_PEN_NAME "NVTCapacitivePen"

//---Touch info.---
#define TOUCH_DEFAULT_MAX_WIDTH 1600
#define TOUCH_DEFAULT_MAX_HEIGHT 2560
#define TOUCH_MAX_FINGER_NUM 10
#define TOUCH_KEY_NUM 0
#if TOUCH_KEY_NUM > 0
extern const uint16_t touch_key_array[TOUCH_KEY_NUM];
#endif
#define TOUCH_FORCE_NUM 1000
//---for Pen---
#define PEN_PRESSURE_MAX (4095)
#define PEN_DISTANCE_MAX (1)
#define PEN_TILT_MIN (-60)
#define PEN_TILT_MAX (60)
//---for pen resolution---
#define PANEL_DEFAULT_WIDTH_MM 148  // 148mm
#define PANEL_DEFAULT_HEIGHT_MM 237 // 237mm
/* Enable only when module have tp reset pin and connected to host */
#define NVT_TOUCH_SUPPORT_HW_RST 0

//---Customerized func.---
#define NVT_TOUCH_MP 0
#define NVT_TOUCH_MP_SETTING_CRITERIA_FROM_CSV 0
#define MT_PROTOCOL_B 1
#define FUNCPAGE_PALM 4
#define PACKET_PALM_ON 3
#define PACKET_PALM_OFF 4

#define BOOT_UPDATE_FIRMWARE 1
#define DEFAULT_BOOT_UPDATE_FIRMWARE_NAME "novatek/nt36523.bin"
#define DEFAULT_MP_UPDATE_FIRMWARE_NAME   "novatek_ts_mp.bin"

//---ESD Protect.---
#define NVT_TOUCH_ESD_PROTECT 1
#define NVT_TOUCH_ESD_CHECK_PERIOD 1500	/* ms */
#define NVT_TOUCH_WDT_RECOVERY 1

enum nvt_ic_state {
	NVT_IC_SUSPEND_IN,
	NVT_IC_SUSPEND_OUT,
	NVT_IC_RESUME_IN,
	NVT_IC_RESUME_OUT,
	NVT_IC_INIT,
};

struct nvt_config_info {
	u8 tp_vendor;
	u8 tp_color;
	u8 display_maker;
	u8 glass_vendor;
	const char *nvt_fw_name;
	const char *nvt_mp_name;
	const char *nvt_limit_name;
};

struct nvt_ts_data {
	struct spi_device *client;
	struct input_dev *input_dev;
	struct delayed_work nvt_fwu_work;
	struct work_struct switch_mode_work;
	struct work_struct pen_charge_state_change_work;
	bool pen_is_charge;
	struct notifier_block pen_charge_state_notifier;
	uint16_t addr;
	int8_t phys[32];
#if defined(CONFIG_FB)
#ifdef CONFIG_DRM
	struct notifier_block drm_notif;
#else
	struct notifier_block fb_notif;
#endif
#endif
	uint32_t config_array_size;
	struct nvt_config_info *config_array;
	const char *fw_name;
	bool lkdown_readed;
	uint8_t fw_ver;
	uint8_t x_num;
	uint8_t y_num;
	int ic_state;
	uint16_t abs_x_max;
	uint16_t abs_y_max;
	uint8_t max_touch_num;
	uint8_t max_button_num;
	uint32_t int_trigger_type;
	int32_t irq_gpio;
	uint32_t irq_flags;
	int32_t reset_gpio;
	uint32_t reset_flags;
	struct mutex lock;
	const struct nvt_ts_mem_map *mmap;
	uint8_t carrier_system;
	uint8_t hw_crc;
	uint16_t nvt_pid;
	uint8_t *rbuf;
	uint8_t *xbuf;
	struct mutex xbuf_lock;
	bool irq_enabled;
	uint8_t cascade;
	bool pen_support;
	bool wgp_stylus;
	uint8_t x_gang_num;
	uint8_t y_gang_num;
	struct input_dev *pen_input_dev;
	bool pen_input_dev_enable;
	int8_t pen_phys[32];
	struct workqueue_struct *event_wq;
	struct work_struct suspend_work;
	struct work_struct resume_work;
	int result_type;
	int panel_index;
	uint32_t spi_max_freq;
	int db_wakeup;
	uint8_t debug_flag;
	bool fw_debug;
	bool dev_pm_suspend;
	struct completion dev_pm_suspend_completion;
	bool palm_sensor_switch;
	int gesture_command_delayed;
	struct pinctrl *ts_pinctrl;
	struct pinctrl_state *pinctrl_state_active;
	struct pinctrl_state *pinctrl_state_suspend;
};

typedef enum {
	RESET_STATE_INIT = 0xA0,// IC reset
	RESET_STATE_REK,		// ReK baseline
	RESET_STATE_REK_FINISH,	// baseline is ready
	RESET_STATE_NORMAL_RUN,	// normal run
	RESET_STATE_MAX  = 0xAF
} RST_COMPLETE_STATE;

typedef enum {
    EVENT_MAP_HOST_CMD                      = 0x50,
    EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE   = 0x51,
    EVENT_MAP_RESET_COMPLETE                = 0x60,
    EVENT_MAP_FWINFO                        = 0x78,
    EVENT_MAP_PROJECTID                     = 0x9A,
} SPI_EVENT_MAP;

//---SPI READ/WRITE---
#define SPI_WRITE_MASK(a)	(a | 0x80)
#define SPI_READ_MASK(a)	(a & 0x7F)

#define DUMMY_BYTES (1)
#define NVT_TRANSFER_LEN	(63*1024)
#define NVT_READ_LEN		(2*1024)

typedef enum {
	NVTWRITE = 0,
	NVTREAD  = 1
} NVT_SPI_RW;

//---extern structures---
extern struct nvt_ts_data *ts;

//---extern functions---
int32_t CTP_SPI_READ(struct spi_device *client, uint8_t *buf, uint16_t len);
int32_t CTP_SPI_WRITE(struct spi_device *client, uint8_t *buf, uint16_t len);
void nvt_bootloader_reset(void);
void nvt_eng_reset(void);
void nvt_sw_reset(void);
void nvt_sw_reset_idle(void);
void nvt_boot_ready(void);
void nvt_bld_crc_enable(void);
void nvt_fw_crc_enable(void);
void nvt_tx_auto_copy_mode(void);
void nvt_set_dbgfw_status(bool enable);
void nvt_match_fw(void);
int32_t nvt_update_firmware(const char *firmware_name);
int32_t nvt_check_fw_reset_state(RST_COMPLETE_STATE check_reset_state);
int32_t nvt_get_fw_info(void);
int32_t nvt_clear_fw_status(void);
int32_t nvt_check_fw_status(void);
int32_t nvt_check_spi_dma_tx_info(void);
int32_t nvt_set_page(uint32_t addr);
int32_t nvt_write_addr(uint32_t addr, uint8_t data);
int32_t nvt_read_pid(void);
bool nvt_get_dbgfw_status(void);
int32_t nvt_set_pocket_palm_switch(uint8_t pocket_palm_switch);
void Boot_Update_Firmware(struct work_struct *work);
int32_t disable_pen_input_device(bool disable);
#if NVT_TOUCH_ESD_PROTECT
extern void nvt_esd_check_enable(uint8_t enable);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

#endif /* _LINUX_NVT_TOUCH_H */
