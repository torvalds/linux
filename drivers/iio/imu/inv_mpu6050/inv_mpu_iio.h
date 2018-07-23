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
#include <linux/i2c-mux.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/regmap.h>
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
 *  @accel_lpf:		Configures accelerometer low pass filter.
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
 *  @int_status:	Interrupt status register.
 *  @pwr_mgmt_1:	Controls chip's power state and clock source.
 *  @pwr_mgmt_2:	Controls power state of individual sensors.
 *  @int_pin_cfg;	Controls interrupt pin configuration.
 *  @accl_offset:	Controls the accelerometer calibration offset.
 *  @gyro_offset:	Controls the gyroscope calibration offset.
 */
struct inv_mpu6050_reg_map {
	u8 sample_rate_div;
	u8 lpf;
	u8 accel_lpf;
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
	u8 int_status;
	u8 pwr_mgmt_1;
	u8 pwr_mgmt_2;
	u8 int_pin_cfg;
	u8 accl_offset;
	u8 gyro_offset;
};

/*device enum */
enum inv_devices {
	INV_MPU6050,
	INV_MPU6500,
	INV_MPU6000,
	INV_MPU9150,
	INV_MPU9250,
	INV_MPU9255,
	INV_ICM20608,
	INV_NUM_PARTS
};

/**
 *  struct inv_mpu6050_chip_config - Cached chip configuration data.
 *  @fsr:		Full scale range.
 *  @lpf:		Digital low pass filter frequency.
 *  @accl_fs:		accel full scale range.
 *  @accl_fifo_enable:	enable accel data output
 *  @gyro_fifo_enable:	enable gyro data output
 *  @fifo_rate:		FIFO update rate.
 */
struct inv_mpu6050_chip_config {
	unsigned int fsr:2;
	unsigned int lpf:3;
	unsigned int accl_fs:2;
	unsigned int accl_fifo_enable:1;
	unsigned int gyro_fifo_enable:1;
	u16 fifo_rate;
	u8 user_ctrl;
};

/**
 *  struct inv_mpu6050_hw - Other important hardware information.
 *  @whoami:	Self identification byte from WHO_AM_I register
 *  @name:      name of the chip.
 *  @reg:   register map of the chip.
 *  @config:    configuration of the chip.
 */
struct inv_mpu6050_hw {
	u8 whoami;
	u8 *name;
	const struct inv_mpu6050_reg_map *reg;
	const struct inv_mpu6050_chip_config *config;
};

/*
 *  struct inv_mpu6050_state - Driver state variables.
 *  @TIMESTAMP_FIFO_SIZE: fifo size for timestamp.
 *  @lock:              Chip access lock.
 *  @trig:              IIO trigger.
 *  @chip_config:	Cached attribute information.
 *  @reg:		Map of important registers.
 *  @hw:		Other hardware-specific information.
 *  @chip_type:		chip type.
 *  @time_stamp_lock:	spin lock to time stamp.
 *  @plat_data:		platform data (deprecated in favor of @orientation).
 *  @orientation:	sensor chip orientation relative to main hardware.
 *  @timestamps:        kfifo queue to store time stamp.
 *  @map		regmap pointer.
 *  @irq		interrupt number.
 *  @irq_mask		the int_pin_cfg mask to configure interrupt type.
 */
struct inv_mpu6050_state {
#define TIMESTAMP_FIFO_SIZE 16
	struct mutex lock;
	struct iio_trigger  *trig;
	struct inv_mpu6050_chip_config chip_config;
	const struct inv_mpu6050_reg_map *reg;
	const struct inv_mpu6050_hw *hw;
	enum   inv_devices chip_type;
	spinlock_t time_stamp_lock;
	struct i2c_mux_core *muxc;
	struct i2c_client *mux_client;
	unsigned int powerup_count;
	struct inv_mpu6050_platform_data plat_data;
	struct iio_mount_matrix orientation;
	DECLARE_KFIFO(timestamps, long long, TIMESTAMP_FIFO_SIZE);
	struct regmap *map;
	int irq;
	u8 irq_mask;
	unsigned skip_samples;
};

/*register and associated bit definition*/
#define INV_MPU6050_REG_ACCEL_OFFSET        0x06
#define INV_MPU6050_REG_GYRO_OFFSET         0x13

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

#define INV_MPU6050_REG_INT_STATUS          0x3A
#define INV_MPU6050_BIT_RAW_DATA_RDY_INT    0x01

#define INV_MPU6050_REG_USER_CTRL           0x6A
#define INV_MPU6050_BIT_FIFO_RST            0x04
#define INV_MPU6050_BIT_DMP_RST             0x08
#define INV_MPU6050_BIT_I2C_MST_EN          0x20
#define INV_MPU6050_BIT_FIFO_EN             0x40
#define INV_MPU6050_BIT_DMP_EN              0x80
#define INV_MPU6050_BIT_I2C_IF_DIS          0x10

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

/* mpu6500 registers */
#define INV_MPU6500_REG_ACCEL_CONFIG_2      0x1D
#define INV_MPU6500_REG_ACCEL_OFFSET        0x77

/* delay time in milliseconds */
#define INV_MPU6050_POWER_UP_TIME            100
#define INV_MPU6050_TEMP_UP_TIME             100
#define INV_MPU6050_SENSOR_UP_TIME           30

/* delay time in microseconds */
#define INV_MPU6050_REG_UP_TIME_MIN          5000
#define INV_MPU6050_REG_UP_TIME_MAX          10000

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
#define INV_MPU6050_ACTIVE_HIGH		0x00
#define INV_MPU6050_ACTIVE_LOW		0x80
/* enable level triggering */
#define INV_MPU6050_LATCH_INT_EN	0x20
#define INV_MPU6050_BIT_BYPASS_EN	0x2


/* init parameters */
#define INV_MPU6050_INIT_FIFO_RATE           50
#define INV_MPU6050_TIME_STAMP_TOR           5
#define INV_MPU6050_MAX_FIFO_RATE            1000
#define INV_MPU6050_MIN_FIFO_RATE            4
#define INV_MPU6050_ONE_K_HZ                 1000

#define INV_MPU6050_REG_WHOAMI			117

#define INV_MPU6000_WHOAMI_VALUE		0x68
#define INV_MPU6050_WHOAMI_VALUE		0x68
#define INV_MPU6500_WHOAMI_VALUE		0x70
#define INV_MPU9150_WHOAMI_VALUE		0x68
#define INV_MPU9250_WHOAMI_VALUE		0x71
#define INV_MPU9255_WHOAMI_VALUE		0x73
#define INV_ICM20608_WHOAMI_VALUE		0xAF

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
int inv_mpu6050_probe_trigger(struct iio_dev *indio_dev, int irq_type);
int inv_reset_fifo(struct iio_dev *indio_dev);
int inv_mpu6050_switch_engine(struct inv_mpu6050_state *st, bool en, u32 mask);
int inv_mpu6050_write_reg(struct inv_mpu6050_state *st, int reg, u8 val);
int inv_mpu6050_set_power_itg(struct inv_mpu6050_state *st, bool power_on);
int inv_mpu_acpi_create_mux_client(struct i2c_client *client);
void inv_mpu_acpi_delete_mux_client(struct i2c_client *client);
int inv_mpu_core_probe(struct regmap *regmap, int irq, const char *name,
		int (*inv_mpu_bus_setup)(struct iio_dev *), int chip_type);
extern const struct dev_pm_ops inv_mpu_pmops;
