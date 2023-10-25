/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics lps22df driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2021 STMicroelectronics Inc.
 */

#ifndef __ST_LPS22DF_H
#define __ST_LPS22DF_H

#include <linux/module.h>
#include <linux/types.h>
#include <linux/iio/iio.h>
#include <linux/property.h>
#include <linux/iio/trigger.h>

#include "../common/stm_iio_types.h"

#define ST_LPS22DF_DEV_NAME			"lps22df"
#define ST_LPS28DFW_DEV_NAME			"lps28dfw"

#define ST_LPS22DF_MAX_FIFO_LENGTH		127

#define ST_LPS22DF_INTERRUPT_CFG_ADDR		0x0b
#define ST_LPS22DF_LIR_MASK			BIT(2)

#define ST_LPS22DF_WHO_AM_I_ADDR		0x0f
#define ST_LPS22DF_WHO_AM_I_VAL			0xb4

#define ST_LPS22DF_CTRL_REG1_ADDR		0x10
#define ST_LPS22DF_AVG_MASK			GENMASK(2, 0)
#define ST_LPS22DF_ODR_MASK			GENMASK(6, 3)

#define ST_LPS22DF_CTRL_REG2_ADDR		0x11
#define ST_LPS22DF_SWRESET_MASK			BIT(2)
#define ST_LPS22DF_BDU_MASK			BIT(3)
#define ST_LPS22DF_EN_LPFP_MASK			BIT(4)
#define ST_LPS22DF_FS_MODE_MASK			BIT(6)
#define ST_LPS22DF_BOOT_MASK			BIT(7)

#define ST_LPS22DF_CTRL3_ADDR			0x12
#define ST_LPS22DF_IF_ADD_INC_MASK		BIT(0)
#define ST_LPS22DF_PP_OD_MASK			BIT(1)
#define ST_LPS22DF_INT_H_L_MASK			BIT(3)

#define ST_LPS22DF_CTRL4_ADDR			0x13
#define ST_LPS22DF_INT_F_WTM_MASK		BIT(1)

#define ST_LPS22DF_FIFO_CTRL_ADDR		0x14
#define ST_LPS22DF_FIFO_MODE_MASK		GENMASK(1, 0)

#define ST_LPS22DF_FIFO_WTM_ADDR		0x15
#define ST_LPS22DF_FIFO_THS_MASK		GENMASK(6, 0)

#define ST_LPS22DF_FIFO_STATUS1_ADDR		0x25
#define ST_LPS22DF_FIFO_SRC_DIFF_MASK		GENMASK(7, 0)

#define ST_LPS22DF_FIFO_STATUS2_ADDR		0x26
#define ST_LPS22DF_FIFO_WTM_IA_MASK		BIT(7)

#define ST_LPS22DF_PRESS_OUT_XL_ADDR		0x28

#define ST_LPS22DF_TEMP_OUT_L_ADDR		0x2b

#define ST_LPS22DF_FIFO_DATA_OUT_PRESS_XL_ADDR	0x78

#define ST_LPS22DF_PRESS_1260_FS_AVL_GAIN	(1000000000UL / 4096UL)
#define ST_LPS22DF_PRESS_4060_FS_AVL_GAIN	(1000000000UL / 2048UL)
#define ST_LPS22DF_TEMP_FS_AVL_GAIN		100

#define ST_LPS22DF_ODR_LIST_NUM			9

enum st_lps22df_sensor_type {
	ST_LPS22DF_PRESS = 0,
	ST_LPS22DF_TEMP,
	ST_LPS22DF_SENSORS_NUMB,
};

enum st_lps22df_fifo_mode {
	ST_LPS22DF_BYPASS = 0x0,
	ST_LPS22DF_STREAM = 0x2,
};

#define ST_LPS22DF_PRESS_SAMPLE_LEN		3
#define ST_LPS22DF_TEMP_SAMPLE_LEN		2

#define ST_LPS22DF_TX_MAX_LENGTH		64
#define ST_LPS22DF_RX_MAX_LENGTH		((ST_LPS22DF_MAX_FIFO_LENGTH + 1) * \
						 ST_LPS22DF_PRESS_SAMPLE_LEN)

struct st_lps22df_transfer_buffer {
	u8 rx_buf[ST_LPS22DF_RX_MAX_LENGTH];
	u8 tx_buf[ST_LPS22DF_TX_MAX_LENGTH] ____cacheline_aligned;
};

struct st_lps22df_transfer_function {
	int (*write)(struct device *dev, u8 addr, int len, u8 *data);
	int (*read)(struct device *dev, u8 addr, int len, u8 *data);
};

enum st_lps22df_hw_id {
	ST_LPS22DF_ID,
	ST_LPS28DFW_ID,
	ST_LPS22DF_MAX_ID,
};

struct st_lps22df_fs {
	u32 gain;
	u8 val;
};

struct st_lps22df_fs_table_t {
	u8 addr;
	u8 mask;
	u8 fs_len;
	struct st_lps22df_fs fs_avl[2];
};

struct st_lps22df_settings {
	struct {
		enum st_lps22df_hw_id hw_id;
		const char *name;
	} id[ST_LPS22DF_MAX_ID];
	struct st_lps22df_fs_table_t fs_table;
	bool st_multi_scale;
};

struct st_lps22df_hw {
	struct device *dev;
	int irq;

	struct mutex fifo_lock;
	struct mutex lock;
	u8 watermark;

	struct iio_dev *iio_devs[ST_LPS22DF_SENSORS_NUMB];
	u8 enable_mask;
	u8 odr;

	s64 last_fifo_ts;
	s64 delta_ts;
	s64 ts_irq;
	s64 ts;
	const struct st_lps22df_settings *settings;

	const struct st_lps22df_transfer_function *tf;
	struct st_lps22df_transfer_buffer tb;
};

struct st_lps22df_sensor {
	struct st_lps22df_hw *hw;
	enum st_lps22df_sensor_type type;
	char name[32];

	u32 gain;
	u8 odr;
};

int st_lps22df_common_probe(struct device *dev, int irq, int hw_id,
			    const struct st_lps22df_transfer_function *tf_ops);
int st_lps22df_write_with_mask(struct st_lps22df_hw *hw, u8 addr, u8 mask,
			       u8 data);
int st_lps22df_allocate_buffers(struct st_lps22df_hw *hw);
int st_lps22df_set_enable(struct st_lps22df_sensor *sensor, bool enable);
ssize_t st_lps22df_sysfs_set_hwfifo_watermark(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count);
ssize_t st_lps22df_sysfs_flush_fifo(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size);

#endif /* __ST_LPS22DF_H */
