/*
* Copyright (C) 2012 Invensense, Inc.
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/
#include <linux/i2c.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/platform_data/invensense_mpu6050.h>

/**
 *  struct inv_mpu6050_reg_map - Notable registers.
 *  @sample_rate_div:	Divider applied to gyro output rate.
 *  @lpf:		Configures internal low pass filter.
 *  @user_ctrl:		Enables/resets the FIFO.
 *  @fifo_en:		Determines which data will appear in FIFO.
 *  @gyro_config:	gyro config register.
 *  @accl_config:	accel config register
 *  @fifo_count_h:	Upper byte of FIFO count.
 *  @fifo_r_w:		FIFO register.
 *  @raw_gyro:		Address of first gyro register.
 *  @raw_accl:		Address of first accel register.
 *  @temperature:	temperature register
 *  @int_enable:	Interrupt enable register.
 *  @pwr_mgmt_1:	Controls chip's power state and clock source.
 *  @pwr_mgmt_2:	Controls power state of individual sensors.
 */
struct inv_mpu6050_reg_map {
	u8 sample_rate_div;
	u8 lpf;
	u8 user_ctrl;
	u8 fifo_en;
	u8 gyro_config;
	u8 accl_config;
	u8 fifo_count_h;
	u8 fifo_r_w;
	u8 raw_gyro;
	u8 raw_accl;
	u8 temperature;
	u8 int_enable;
	u8 pwr_mgmt_1;
	u8 pwr_mgmt_2;
	u8 int_pin_cfg;
};

/*device enum */
enum inv_devices {
	INV_MPU6050,
	INV_MPU6500,
	INV_NUM_PARTS
};

/**
 *  struct inv_mpu6050_chip_config - Cached chip configuration data.
 *  @fsr:		Full scale range.
 *  @lpf:		Digital low pass filter frequency.
 *  @accl_fs:		accel full scale range.
 *  @enable:		master enable state.
 *  @accl_fifo_enable:	enable accel data output
 *  @gyro_fifo_enable:	enable gyro data output
 *  @fifo_rate:		FIFO update rate.
 */
struct inv_mpu6050_chip_config {
	unsigned int fsr:2;
	unsigned int lpf:3;
	unsigned int accl_fs:2;
	unsigned int enable:1;
	unsigned int accl_fifo_enable:1;
	unsigned int gyro_fifo_enable:1;
	u16 fifo_rate;
};

/**
 *  struct inv_mpu6050_hw - Other important hardware information.
 *  @num_reg:	Number of registers on device.
 *  @name:      name of the chip.
 *  @reg:   register map of the chip.
 *  @config:    configuration of the chip.
 */
struct inv_mpu6050_hw {
	u8 num_reg;
	u8 *name;
	const struct inv_mpu6050_reg_map *reg;
	const struct inv_mpu6050_chip_config *config;
};

/*
 *  struct inv_mpu6050_state - Driver state variables.
 *  @TIMESTAMP_FIFO_SIZE: fifo size for timestamp.
 *  @trig:              IIO trigger.
 *  @chip_config:	Cached attribute information.
 *  @reg:		Map of important registers.
 *  @hw:		Other hardware-specific information.
 *  @chip_type:		chip type.
 *  @time_stamp_lock:	spin lock to time stamp.
 *  @client:		i2c client handle.
 *  @plat_data:		platform data.
 *  @timestamps:        kfifo queue to store time stamp.
 */
struct inv_mpu6050_state {
#define TIMESTAMP_FIFO_SIZE 16
	struct iio_trigger  *trig;
	struct inv_mpu6050_chip_config chip_config;
	const struct inv_mpu6050_reg_map *reg;
	const struct inv_mpu6050_hw *hw;
	enum   inv_devices chip_type;
	spinlock_t time_stamp_lock;
	struct i2c_client *client;
	struct i2c_adapter *mux_adapter;
	unsigned int powerup_count;
	struct inv_mpu6050_platform_data plat_data;
	DECLARE_KFIFO(timestamps, long long, TIMESTAMP_FIFO_SIZE);
};

/*register and associated bit definition*/
#define INV_MPU6050_REG_SAMPLE_RATE_DIV     0x19
#define INV_MPU6050_REG_CONFIG              0x1A
#define INV_MPU6050_REG_GYRO_CONFIG         0x1B
#define INV_MPU6050_REG_ACCEL_CONFIG        0x1C

#define INV_MPU6050_REG_FIFO_EN             0x23
#define INV_MPU6050_BIT_ACCEL_OUT           0x08
#define INV_MPU6050_BITS_GYRO_OUT           0x70

#define INV_MPU6050_REG_INT_ENABLE          0x38
#define INV_MPU6050_BIT_DATA_RDY_EN         0x01
#define INV_MPU6050_BIT_DMP_INT_EN          0x02

#define INV_MPU6050_REG_RAW_ACCEL           0x3B
#define INV_MPU6050_REG_TEMPERATURE         0x41
#define INV_MPU6050_REG_RAW_GYRO            0x43

#define INV_MPU6050_REG_USER_CTRL           0x6A
#define INV_MPU6050_BIT_FIFO_RST            0x04
#define INV_MPU6050_BIT_DMP_RST             0x08
#define INV_MPU6050_BIT_I2C_MST_EN          0x20
#define INV_MPU6050_BIT_FIFO_EN             0x40
#define INV_MPU6050_BIT_DMP_EN              0x80

#define INV_MPU6050_REG_PWR_MGMT_1          0x6B
#define INV_MPU6050_BIT_H_RESET             0x80
#define INV_MPU6050_BIT_SLEEP               0x40
#define INV_MPU6050_BIT_CLK_MASK            0x7

#define INV_MPU6050_REG_PWR_MGMT_2          0x6C
#define INV_MPU6050_BIT_PWR_ACCL_STBY       0x38
#define INV_MPU6050_BIT_PWR_GYRO_STBY       0x07

#define INV_MPU6050_REG_FIFO_COUNT_H        0x72
#define INV_MPU6050_REG_FIFO_R_W            0x74

#define INV_MPU6050_BYTES_PER_3AXIS_SENSOR   6
#define INV_MPU6050_FIFO_COUNT_BYTE          2
#define INV_MPU6050_FIFO_THRESHOLD           500
#define INV_MPU6050_POWER_UP_TIME            100
#define INV_MPU6050_TEMP_UP_TIME             100
#define INV_MPU6050_SENSOR_UP_TIME           30
#define INV_MPU6050_REG_UP_TIME              5

#define INV_MPU6050_TEMP_OFFSET	             12421
#define INV_MPU6050_TEMP_SCALE               2941
#define INV_MPU6050_MAX_GYRO_FS_PARAM        3
#define INV_MPU6050_MAX_ACCL_FS_PARAM        3
#define INV_MPU6050_THREE_AXIS               3
#define INV_MPU6050_GYRO_CONFIG_FSR_SHIFT    3
#define INV_MPU6050_ACCL_CONFIG_FSR_SHIFT    3

/* 6 + 6 round up and plus 8 */
#define INV_MPU6050_OUTPUT_DATA_SIZE         24

#define INV_MPU6050_REG_INT_PIN_CFG	0x37
#define INV_MPU6050_BIT_BYPASS_EN	0x2

/* init parameters */
#define INV_MPU6050_INIT_FIFO_RATE           50
#define INV_MPU6050_TIME_STAMP_TOR           5
#define INV_MPU6050_MAX_FIFO_RATE            1000
#define INV_MPU6050_MIN_FIFO_RATE            4
#define INV_MPU6050_ONE_K_HZ                 1000

/* scan element definition */
enum inv_mpu6050_scan {
	INV_MPU6050_SCAN_ACCL_X,
	INV_MPU6050_SCAN_ACCL_Y,
	INV_MPU6050_SCAN_ACCL_Z,
	INV_MPU6050_SCAN_GYRO_X,
	INV_MPU6050_SCAN_GYRO_Y,
	INV_MPU6050_SCAN_GYRO_Z,
	INV_MPU6050_SCAN_TIMESTAMP,
};

enum inv_mpu6050_filter_e {
	INV_MPU6050_FILTER_256HZ_NOLPF2 = 0,
	INV_MPU6050_FILTER_188HZ,
	INV_MPU6050_FILTER_98HZ,
	INV_MPU6050_FILTER_42HZ,
	INV_MPU6050_FILTER_20HZ,
	INV_MPU6050_FILTER_10HZ,
	INV_MPU6050_FILTER_5HZ,
	INV_MPU6050_FILTER_2100HZ_NOLPF,
	NUM_MPU6050_FILTER
};

/* IIO attribute address */
enum INV_MPU6050_IIO_ATTR_ADDR {
	ATTR_GYRO_MATRIX,
	ATTR_ACCL_MATRIX,
};

enum inv_mpu6050_accl_fs_e {
	INV_MPU6050_FS_02G = 0,
	INV_MPU6050_FS_04G,
	INV_MPU6050_FS_08G,
	INV_MPU6050_FS_16G,
	NUM_ACCL_FSR
};

enum inv_mpu6050_fsr_e {
	INV_MPU6050_FSR_250DPS = 0,
	INV_MPU6050_FSR_500DPS,
	INV_MPU6050_FSR_1000DPS,
	INV_MPU6050_FSR_2000DPS,
	NUM_MPU6050_FSR
};

enum inv_mpu6050_clock_sel_e {
	INV_CLK_INTERNAL = 0,
	INV_CLK_PLL,
	NUM_CLK
};

irqreturn_t inv_mpu6050_irq_handler(int irq, void *p);
irqreturn_t inv_mpu6050_read_fifo(int irq, void *p);
int inv_mpu6050_probe_trigger(struct iio_dev *indio_dev);
void inv_mpu6050_remove_trigger(struct inv_mpu6050_state *st);
int inv_reset_fifo(struct iio_dev *indio_dev);
int inv_mpu6050_switch_engine(struct inv_mpu6050_state *st, bool en, u32 mask);
int inv_mpu6050_write_reg(struct inv_mpu6050_state *st, int reg, u8 val);
int inv_mpu6050_set_power_itg(struct inv_mpu6050_state *st, bool power_on);
