// SPDX-License-Identifier: GPL-2.0
/*
 *  Mellanox BlueField I2C bus driver
 *
 *  Copyright (C) 2020 Mellanox Technologies, Ltd.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/string.h>

/* Defines what functionality is present. */
#define MLXBF_I2C_FUNC_SMBUS_BLOCK \
	(I2C_FUNC_SMBUS_BLOCK_DATA | I2C_FUNC_SMBUS_BLOCK_PROC_CALL)

#define MLXBF_I2C_FUNC_SMBUS_DEFAULT \
	(I2C_FUNC_SMBUS_BYTE      | I2C_FUNC_SMBUS_BYTE_DATA | \
	 I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_I2C_BLOCK | \
	 I2C_FUNC_SMBUS_PROC_CALL)

#define MLXBF_I2C_FUNC_ALL \
	(MLXBF_I2C_FUNC_SMBUS_DEFAULT | MLXBF_I2C_FUNC_SMBUS_BLOCK | \
	 I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SLAVE)

#define MLXBF_I2C_SMBUS_MAX        3

/* Shared resources info in BlueField platforms. */

#define MLXBF_I2C_COALESCE_TYU_ADDR    0x02801300
#define MLXBF_I2C_COALESCE_TYU_SIZE    0x010

#define MLXBF_I2C_GPIO_TYU_ADDR        0x02802000
#define MLXBF_I2C_GPIO_TYU_SIZE        0x100

#define MLXBF_I2C_COREPLL_TYU_ADDR     0x02800358
#define MLXBF_I2C_COREPLL_TYU_SIZE     0x008

#define MLXBF_I2C_COREPLL_YU_ADDR      0x02800c30
#define MLXBF_I2C_COREPLL_YU_SIZE      0x00c

#define MLXBF_I2C_SHARED_RES_MAX       3

/*
 * Note that the following SMBus, CAUSE, GPIO and PLL register addresses
 * refer to their respective offsets relative to the corresponding
 * memory-mapped region whose addresses are specified in either the DT or
 * the ACPI tables or above.
 */

/*
 * SMBus Master core clock frequency. Timing configurations are
 * strongly dependent on the core clock frequency of the SMBus
 * Master. Default value is set to 400MHz.
 */
#define MLXBF_I2C_TYU_PLL_OUT_FREQ  (400 * 1000 * 1000)
/* Reference clock for Bluefield - 156 MHz. */
#define MLXBF_I2C_PLL_IN_FREQ       (156 * 1000 * 1000)

/* Constant used to determine the PLL frequency. */
#define MLNXBF_I2C_COREPLL_CONST    16384

/* PLL registers. */
#define MLXBF_I2C_CORE_PLL_REG0         0x0
#define MLXBF_I2C_CORE_PLL_REG1         0x4
#define MLXBF_I2C_CORE_PLL_REG2         0x8

/* OR cause register. */
#define MLXBF_I2C_CAUSE_OR_EVTEN0    0x14
#define MLXBF_I2C_CAUSE_OR_CLEAR     0x18

/* Arbiter Cause Register. */
#define MLXBF_I2C_CAUSE_ARBITER      0x1c

/*
 * Cause Status flags. Note that those bits might be considered
 * as interrupt enabled bits.
 */

/* Transaction ended with STOP. */
#define MLXBF_I2C_CAUSE_TRANSACTION_ENDED  BIT(0)
/* Master arbitration lost. */
#define MLXBF_I2C_CAUSE_M_ARBITRATION_LOST BIT(1)
/* Unexpected start detected. */
#define MLXBF_I2C_CAUSE_UNEXPECTED_START   BIT(2)
/* Unexpected stop detected. */
#define MLXBF_I2C_CAUSE_UNEXPECTED_STOP    BIT(3)
/* Wait for transfer continuation. */
#define MLXBF_I2C_CAUSE_WAIT_FOR_FW_DATA   BIT(4)
/* Failed to generate STOP. */
#define MLXBF_I2C_CAUSE_PUT_STOP_FAILED    BIT(5)
/* Failed to generate START. */
#define MLXBF_I2C_CAUSE_PUT_START_FAILED   BIT(6)
/* Clock toggle completed. */
#define MLXBF_I2C_CAUSE_CLK_TOGGLE_DONE    BIT(7)
/* Transfer timeout occurred. */
#define MLXBF_I2C_CAUSE_M_FW_TIMEOUT       BIT(8)
/* Master busy bit reset. */
#define MLXBF_I2C_CAUSE_M_GW_BUSY_FALL     BIT(9)

#define MLXBF_I2C_CAUSE_MASTER_ARBITER_BITS_MASK     GENMASK(9, 0)

#define MLXBF_I2C_CAUSE_MASTER_STATUS_ERROR \
	(MLXBF_I2C_CAUSE_M_ARBITRATION_LOST | \
	 MLXBF_I2C_CAUSE_UNEXPECTED_START | \
	 MLXBF_I2C_CAUSE_UNEXPECTED_STOP | \
	 MLXBF_I2C_CAUSE_PUT_STOP_FAILED | \
	 MLXBF_I2C_CAUSE_PUT_START_FAILED | \
	 MLXBF_I2C_CAUSE_CLK_TOGGLE_DONE | \
	 MLXBF_I2C_CAUSE_M_FW_TIMEOUT)

/*
 * Slave cause status flags. Note that those bits might be considered
 * as interrupt enabled bits.
 */

/* Write transaction received successfully. */
#define MLXBF_I2C_CAUSE_WRITE_SUCCESS         BIT(0)
/* Read transaction received, waiting for response. */
#define MLXBF_I2C_CAUSE_READ_WAIT_FW_RESPONSE BIT(13)
/* Slave busy bit reset. */
#define MLXBF_I2C_CAUSE_S_GW_BUSY_FALL        BIT(18)

#define MLXBF_I2C_CAUSE_SLAVE_ARBITER_BITS_MASK     GENMASK(20, 0)

/* Cause coalesce registers. */
#define MLXBF_I2C_CAUSE_COALESCE_0        0x00
#define MLXBF_I2C_CAUSE_COALESCE_1        0x04
#define MLXBF_I2C_CAUSE_COALESCE_2        0x08

#define MLXBF_I2C_CAUSE_TYU_SLAVE_BIT   MLXBF_I2C_SMBUS_MAX
#define MLXBF_I2C_CAUSE_YU_SLAVE_BIT    1

/* Functional enable register. */
#define MLXBF_I2C_GPIO_0_FUNC_EN_0    0x28
/* Force OE enable register. */
#define MLXBF_I2C_GPIO_0_FORCE_OE_EN  0x30
/*
 * Note that Smbus GWs are on GPIOs 30:25. Two pins are used to control
 * SDA/SCL lines:
 *
 *  SMBUS GW0 -> bits[26:25]
 *  SMBUS GW1 -> bits[28:27]
 *  SMBUS GW2 -> bits[30:29]
 */
#define MLXBF_I2C_GPIO_SMBUS_GW_PINS(num) (25 + ((num) << 1))

/* Note that gw_id can be 0,1 or 2. */
#define MLXBF_I2C_GPIO_SMBUS_GW_MASK(num) \
	(0xffffffff & (~(0x3 << MLXBF_I2C_GPIO_SMBUS_GW_PINS(num))))

#define MLXBF_I2C_GPIO_SMBUS_GW_RESET_PINS(num, val) \
	((val) & MLXBF_I2C_GPIO_SMBUS_GW_MASK(num))

#define MLXBF_I2C_GPIO_SMBUS_GW_ASSERT_PINS(num, val) \
	((val) | (0x3 << MLXBF_I2C_GPIO_SMBUS_GW_PINS(num)))

/* SMBus timing parameters. */
#define MLXBF_I2C_SMBUS_TIMER_SCL_LOW_SCL_HIGH    0x00
#define MLXBF_I2C_SMBUS_TIMER_FALL_RISE_SPIKE     0x04
#define MLXBF_I2C_SMBUS_TIMER_THOLD               0x08
#define MLXBF_I2C_SMBUS_TIMER_TSETUP_START_STOP   0x0c
#define MLXBF_I2C_SMBUS_TIMER_TSETUP_DATA         0x10
#define MLXBF_I2C_SMBUS_THIGH_MAX_TBUF            0x14
#define MLXBF_I2C_SMBUS_SCL_LOW_TIMEOUT           0x18

/*
 * Defines SMBus operating frequency and core clock frequency.
 * According to ADB files, default values are compliant to 100KHz SMBus
 * @ 400MHz core clock. The driver should be able to calculate core
 * frequency based on PLL parameters.
 */
#define MLXBF_I2C_COREPLL_FREQ          MLXBF_I2C_TYU_PLL_OUT_FREQ

/* Core PLL TYU configuration. */
#define MLXBF_I2C_COREPLL_CORE_F_TYU_MASK   GENMASK(12, 0)
#define MLXBF_I2C_COREPLL_CORE_OD_TYU_MASK  GENMASK(3, 0)
#define MLXBF_I2C_COREPLL_CORE_R_TYU_MASK   GENMASK(5, 0)

#define MLXBF_I2C_COREPLL_CORE_F_TYU_SHIFT  3
#define MLXBF_I2C_COREPLL_CORE_OD_TYU_SHIFT 16
#define MLXBF_I2C_COREPLL_CORE_R_TYU_SHIFT  20

/* Core PLL YU configuration. */
#define MLXBF_I2C_COREPLL_CORE_F_YU_MASK    GENMASK(25, 0)
#define MLXBF_I2C_COREPLL_CORE_OD_YU_MASK   GENMASK(3, 0)
#define MLXBF_I2C_COREPLL_CORE_R_YU_MASK    GENMASK(5, 0)

#define MLXBF_I2C_COREPLL_CORE_F_YU_SHIFT   0
#define MLXBF_I2C_COREPLL_CORE_OD_YU_SHIFT  1
#define MLXBF_I2C_COREPLL_CORE_R_YU_SHIFT   26

/* Core PLL frequency. */
static u64 mlxbf_i2c_corepll_frequency;

/* SMBus Master GW. */
#define MLXBF_I2C_SMBUS_MASTER_GW     0x200
/* Number of bytes received and sent. */
#define MLXBF_I2C_SMBUS_RS_BYTES      0x300
/* Packet error check (PEC) value. */
#define MLXBF_I2C_SMBUS_MASTER_PEC    0x304
/* Status bits (ACK/NACK/FW Timeout). */
#define MLXBF_I2C_SMBUS_MASTER_STATUS 0x308
/* SMbus Master Finite State Machine. */
#define MLXBF_I2C_SMBUS_MASTER_FSM    0x310

/*
 * When enabled, the master will issue a stop condition in case of
 * timeout while waiting for FW response.
 */
#define MLXBF_I2C_SMBUS_EN_FW_TIMEOUT 0x31c

/* SMBus master GW control bits offset in MLXBF_I2C_SMBUS_MASTER_GW[31:3]. */
#define MLXBF_I2C_MASTER_LOCK_BIT         BIT(31) /* Lock bit. */
#define MLXBF_I2C_MASTER_BUSY_BIT         BIT(30) /* Busy bit. */
#define MLXBF_I2C_MASTER_START_BIT        BIT(29) /* Control start. */
#define MLXBF_I2C_MASTER_CTL_WRITE_BIT    BIT(28) /* Control write phase. */
#define MLXBF_I2C_MASTER_CTL_READ_BIT     BIT(19) /* Control read phase. */
#define MLXBF_I2C_MASTER_STOP_BIT         BIT(3)  /* Control stop. */

#define MLXBF_I2C_MASTER_ENABLE \
	(MLXBF_I2C_MASTER_LOCK_BIT | MLXBF_I2C_MASTER_BUSY_BIT | \
	 MLXBF_I2C_MASTER_START_BIT | MLXBF_I2C_MASTER_STOP_BIT)

#define MLXBF_I2C_MASTER_ENABLE_WRITE \
	(MLXBF_I2C_MASTER_ENABLE | MLXBF_I2C_MASTER_CTL_WRITE_BIT)

#define MLXBF_I2C_MASTER_ENABLE_READ \
	(MLXBF_I2C_MASTER_ENABLE | MLXBF_I2C_MASTER_CTL_READ_BIT)

#define MLXBF_I2C_MASTER_SLV_ADDR_SHIFT   12 /* Slave address shift. */
#define MLXBF_I2C_MASTER_WRITE_SHIFT      21 /* Control write bytes shift. */
#define MLXBF_I2C_MASTER_SEND_PEC_SHIFT   20 /* Send PEC byte shift. */
#define MLXBF_I2C_MASTER_PARSE_EXP_SHIFT  11 /* Parse expected bytes shift. */
#define MLXBF_I2C_MASTER_READ_SHIFT       4  /* Control read bytes shift. */

/* SMBus master GW Data descriptor. */
#define MLXBF_I2C_MASTER_DATA_DESC_ADDR   0x280
#define MLXBF_I2C_MASTER_DATA_DESC_SIZE   0x80 /* Size in bytes. */

/* Maximum bytes to read/write per SMBus transaction. */
#define MLXBF_I2C_MASTER_DATA_R_LENGTH  MLXBF_I2C_MASTER_DATA_DESC_SIZE
#define MLXBF_I2C_MASTER_DATA_W_LENGTH (MLXBF_I2C_MASTER_DATA_DESC_SIZE - 1)

/* All bytes were transmitted. */
#define MLXBF_I2C_SMBUS_STATUS_BYTE_CNT_DONE      BIT(0)
/* NACK received. */
#define MLXBF_I2C_SMBUS_STATUS_NACK_RCV           BIT(1)
/* Slave's byte count >128 bytes. */
#define MLXBF_I2C_SMBUS_STATUS_READ_ERR           BIT(2)
/* Timeout occurred. */
#define MLXBF_I2C_SMBUS_STATUS_FW_TIMEOUT         BIT(3)

#define MLXBF_I2C_SMBUS_MASTER_STATUS_MASK        GENMASK(3, 0)

#define MLXBF_I2C_SMBUS_MASTER_STATUS_ERROR \
	(MLXBF_I2C_SMBUS_STATUS_NACK_RCV | \
	 MLXBF_I2C_SMBUS_STATUS_READ_ERR | \
	 MLXBF_I2C_SMBUS_STATUS_FW_TIMEOUT)

#define MLXBF_I2C_SMBUS_MASTER_FSM_STOP_MASK      BIT(31)
#define MLXBF_I2C_SMBUS_MASTER_FSM_PS_STATE_MASK  BIT(15)

/* SMBus slave GW. */
#define MLXBF_I2C_SMBUS_SLAVE_GW              0x400
/* Number of bytes received and sent from/to master. */
#define MLXBF_I2C_SMBUS_SLAVE_RS_MASTER_BYTES 0x500
/* Packet error check (PEC) value. */
#define MLXBF_I2C_SMBUS_SLAVE_PEC             0x504
/* SMBus slave Finite State Machine (FSM). */
#define MLXBF_I2C_SMBUS_SLAVE_FSM             0x510
/*
 * Should be set when all raised causes handled, and cleared by HW on
 * every new cause.
 */
#define MLXBF_I2C_SMBUS_SLAVE_READY           0x52c

/* SMBus slave GW control bits offset in MLXBF_I2C_SMBUS_SLAVE_GW[31:19]. */
#define MLXBF_I2C_SLAVE_BUSY_BIT         BIT(30) /* Busy bit. */
#define MLXBF_I2C_SLAVE_WRITE_BIT        BIT(29) /* Control write enable. */

#define MLXBF_I2C_SLAVE_ENABLE \
	(MLXBF_I2C_SLAVE_BUSY_BIT | MLXBF_I2C_SLAVE_WRITE_BIT)

#define MLXBF_I2C_SLAVE_WRITE_BYTES_SHIFT 22 /* Number of bytes to write. */
#define MLXBF_I2C_SLAVE_SEND_PEC_SHIFT    21 /* Send PEC byte shift. */

/* SMBus slave GW Data descriptor. */
#define MLXBF_I2C_SLAVE_DATA_DESC_ADDR   0x480
#define MLXBF_I2C_SLAVE_DATA_DESC_SIZE   0x80 /* Size in bytes. */

/* SMbus slave configuration registers. */
#define MLXBF_I2C_SMBUS_SLAVE_ADDR_CFG        0x514
#define MLXBF_I2C_SMBUS_SLAVE_ADDR_CNT        16
#define MLXBF_I2C_SMBUS_SLAVE_ADDR_EN_BIT     7
#define MLXBF_I2C_SMBUS_SLAVE_ADDR_MASK       GENMASK(6, 0)

#define MLXBF_I2C_SLAVE_ADDR_ENABLED(addr) \
	((addr) & (1 << MLXBF_I2C_SMBUS_SLAVE_ADDR_EN_BIT))

/*
 * Timeout is given in microsends. Note also that timeout handling is not
 * exact.
 */
#define MLXBF_I2C_SMBUS_TIMEOUT   (300 * 1000) /* 300ms */

/* Encapsulates timing parameters. */
struct mlxbf_i2c_timings {
	u16 scl_high;		/* Clock high period. */
	u16 scl_low;		/* Clock low period. */
	u8 sda_rise;		/* Data rise time. */
	u8 sda_fall;		/* Data fall time. */
	u8 scl_rise;		/* Clock rise time. */
	u8 scl_fall;		/* Clock fall time. */
	u16 hold_start;		/* Hold time after (REPEATED) START. */
	u16 hold_data;		/* Data hold time. */
	u16 setup_start;	/* REPEATED START condition setup time. */
	u16 setup_stop;		/* STOP condition setup time. */
	u16 setup_data;		/* Data setup time. */
	u16 pad;		/* Padding. */
	u16 buf;		/* Bus free time between STOP and START. */
	u16 thigh_max;		/* Thigh max. */
	u32 timeout;		/* Detect clock low timeout. */
};

enum {
	MLXBF_I2C_F_READ = BIT(0),
	MLXBF_I2C_F_WRITE = BIT(1),
	MLXBF_I2C_F_NORESTART = BIT(3),
	MLXBF_I2C_F_SMBUS_OPERATION = BIT(4),
	MLXBF_I2C_F_SMBUS_BLOCK = BIT(5),
	MLXBF_I2C_F_SMBUS_PEC = BIT(6),
	MLXBF_I2C_F_SMBUS_PROCESS_CALL = BIT(7),
};

struct mlxbf_i2c_smbus_operation {
	u32 flags;
	u32 length; /* Buffer length in bytes. */
	u8 *buffer;
};

#define MLXBF_I2C_SMBUS_OP_CNT_1	1
#define MLXBF_I2C_SMBUS_OP_CNT_2	2
#define MLXBF_I2C_SMBUS_OP_CNT_3	3
#define MLXBF_I2C_SMBUS_MAX_OP_CNT	MLXBF_I2C_SMBUS_OP_CNT_3

struct mlxbf_i2c_smbus_request {
	u8 slave;
	u8 operation_cnt;
	struct mlxbf_i2c_smbus_operation operation[MLXBF_I2C_SMBUS_MAX_OP_CNT];
};

struct mlxbf_i2c_resource {
	void __iomem *io;
	struct resource *params;
	struct mutex *lock; /* Mutex to protect mlxbf_i2c_resource. */
	u8 type;
};

/* List of chip resources that are being accessed by the driver. */
enum {
	MLXBF_I2C_SMBUS_RES,
	MLXBF_I2C_MST_CAUSE_RES,
	MLXBF_I2C_SLV_CAUSE_RES,
	MLXBF_I2C_COALESCE_RES,
	MLXBF_I2C_COREPLL_RES,
	MLXBF_I2C_GPIO_RES,
	MLXBF_I2C_END_RES,
};

/* Helper macro to define an I2C resource parameters. */
#define MLXBF_I2C_RES_PARAMS(addr, size, str) \
	{ \
		.start = (addr), \
		.end = (addr) + (size) - 1, \
		.name = (str) \
	}

static struct resource mlxbf_i2c_coalesce_tyu_params =
		MLXBF_I2C_RES_PARAMS(MLXBF_I2C_COALESCE_TYU_ADDR,
				     MLXBF_I2C_COALESCE_TYU_SIZE,
				     "COALESCE_MEM");
static struct resource mlxbf_i2c_corepll_tyu_params =
		MLXBF_I2C_RES_PARAMS(MLXBF_I2C_COREPLL_TYU_ADDR,
				     MLXBF_I2C_COREPLL_TYU_SIZE,
				     "COREPLL_MEM");
static struct resource mlxbf_i2c_corepll_yu_params =
		MLXBF_I2C_RES_PARAMS(MLXBF_I2C_COREPLL_YU_ADDR,
				     MLXBF_I2C_COREPLL_YU_SIZE,
				     "COREPLL_MEM");
static struct resource mlxbf_i2c_gpio_tyu_params =
		MLXBF_I2C_RES_PARAMS(MLXBF_I2C_GPIO_TYU_ADDR,
				     MLXBF_I2C_GPIO_TYU_SIZE,
				     "GPIO_MEM");

static struct mutex mlxbf_i2c_coalesce_lock;
static struct mutex mlxbf_i2c_corepll_lock;
static struct mutex mlxbf_i2c_gpio_lock;

/* Mellanox BlueField chip type. */
enum mlxbf_i2c_chip_type {
	MLXBF_I2C_CHIP_TYPE_1, /* Mellanox BlueField-1 chip. */
	MLXBF_I2C_CHIP_TYPE_2, /* Mallanox BlueField-2 chip. */
};

struct mlxbf_i2c_chip_info {
	enum mlxbf_i2c_chip_type type;
	/* Chip shared resources that are being used by the I2C controller. */
	struct mlxbf_i2c_resource *shared_res[MLXBF_I2C_SHARED_RES_MAX];

	/* Callback to calculate the core PLL frequency. */
	u64 (*calculate_freq)(struct mlxbf_i2c_resource *corepll_res);
};

struct mlxbf_i2c_priv {
	const struct mlxbf_i2c_chip_info *chip;
	struct i2c_adapter adap;
	struct mlxbf_i2c_resource *smbus;
	struct mlxbf_i2c_resource *mst_cause;
	struct mlxbf_i2c_resource *slv_cause;
	struct mlxbf_i2c_resource *coalesce;
	u64 frequency; /* Core frequency in Hz. */
	int bus; /* Physical bus identifier. */
	int irq;
	struct i2c_client *slave;
};

static struct mlxbf_i2c_resource mlxbf_i2c_coalesce_res[] = {
	[MLXBF_I2C_CHIP_TYPE_1] = {
		.params = &mlxbf_i2c_coalesce_tyu_params,
		.lock = &mlxbf_i2c_coalesce_lock,
		.type = MLXBF_I2C_COALESCE_RES
	},
	{}
};

static struct mlxbf_i2c_resource mlxbf_i2c_corepll_res[] = {
	[MLXBF_I2C_CHIP_TYPE_1] = {
		.params = &mlxbf_i2c_corepll_tyu_params,
		.lock = &mlxbf_i2c_corepll_lock,
		.type = MLXBF_I2C_COREPLL_RES
	},
	[MLXBF_I2C_CHIP_TYPE_2] = {
		.params = &mlxbf_i2c_corepll_yu_params,
		.lock = &mlxbf_i2c_corepll_lock,
		.type = MLXBF_I2C_COREPLL_RES,
	}
};

static struct mlxbf_i2c_resource mlxbf_i2c_gpio_res[] = {
	[MLXBF_I2C_CHIP_TYPE_1] = {
		.params = &mlxbf_i2c_gpio_tyu_params,
		.lock = &mlxbf_i2c_gpio_lock,
		.type = MLXBF_I2C_GPIO_RES
	},
	{}
};

static u8 mlxbf_i2c_bus_count;

static struct mutex mlxbf_i2c_bus_lock;

/* Polling frequency in microseconds. */
#define MLXBF_I2C_POLL_FREQ_IN_USEC        200

#define MLXBF_I2C_SHIFT_0   0
#define MLXBF_I2C_SHIFT_8   8
#define MLXBF_I2C_SHIFT_16  16
#define MLXBF_I2C_SHIFT_24  24

#define MLXBF_I2C_MASK_8    GENMASK(7, 0)
#define MLXBF_I2C_MASK_16   GENMASK(15, 0)

#define MLXBF_I2C_FREQUENCY_1GHZ  1000000000

/*
 * Function to poll a set of bits at a specific address; it checks whether
 * the bits are equal to zero when eq_zero is set to 'true', and not equal
 * to zero when eq_zero is set to 'false'.
 * Note that the timeout is given in microseconds.
 */
static u32 mlxbf_smbus_poll(void __iomem *io, u32 addr, u32 mask,
			    bool eq_zero, u32  timeout)
{
	u32 bits;

	timeout = (timeout / MLXBF_I2C_POLL_FREQ_IN_USEC) + 1;

	do {
		bits = readl(io + addr) & mask;
		if (eq_zero ? bits == 0 : bits != 0)
			return eq_zero ? 1 : bits;
		udelay(MLXBF_I2C_POLL_FREQ_IN_USEC);
	} while (timeout-- != 0);

	return 0;
}

/*
 * SW must make sure that the SMBus Master GW is idle before starting
 * a transaction. Accordingly, this function polls the Master FSM stop
 * bit; it returns false when the bit is asserted, true if not.
 */
static bool mlxbf_smbus_master_wait_for_idle(struct mlxbf_i2c_priv *priv)
{
	u32 mask = MLXBF_I2C_SMBUS_MASTER_FSM_STOP_MASK;
	u32 addr = MLXBF_I2C_SMBUS_MASTER_FSM;
	u32 timeout = MLXBF_I2C_SMBUS_TIMEOUT;

	if (mlxbf_smbus_poll(priv->smbus->io, addr, mask, true, timeout))
		return true;

	return false;
}

static bool mlxbf_i2c_smbus_transaction_success(u32 master_status,
						u32 cause_status)
{
	/*
	 * When transaction ended with STOP, all bytes were transmitted,
	 * and no NACK received, then the transaction ended successfully.
	 * On the other hand, when the GW is configured with the stop bit
	 * de-asserted then the SMBus expects the following GW configuration
	 * for transfer continuation.
	 */
	if ((cause_status & MLXBF_I2C_CAUSE_WAIT_FOR_FW_DATA) ||
	    ((cause_status & MLXBF_I2C_CAUSE_TRANSACTION_ENDED) &&
	     (master_status & MLXBF_I2C_SMBUS_STATUS_BYTE_CNT_DONE) &&
	     !(master_status & MLXBF_I2C_SMBUS_STATUS_NACK_RCV)))
		return true;

	return false;
}

/*
 * Poll SMBus master status and return transaction status,
 * i.e. whether succeeded or failed. I2C and SMBus fault codes
 * are returned as negative numbers from most calls, with zero
 * or some positive number indicating a non-fault return.
 */
static int mlxbf_i2c_smbus_check_status(struct mlxbf_i2c_priv *priv)
{
	u32 master_status_bits;
	u32 cause_status_bits;

	/*
	 * GW busy bit is raised by the driver and cleared by the HW
	 * when the transaction is completed. The busy bit is a good
	 * indicator of transaction status. So poll the busy bit, and
	 * then read the cause and master status bits to determine if
	 * errors occurred during the transaction.
	 */
	mlxbf_smbus_poll(priv->smbus->io, MLXBF_I2C_SMBUS_MASTER_GW,
			 MLXBF_I2C_MASTER_BUSY_BIT, true,
			 MLXBF_I2C_SMBUS_TIMEOUT);

	/* Read cause status bits. */
	cause_status_bits = readl(priv->mst_cause->io +
					MLXBF_I2C_CAUSE_ARBITER);
	cause_status_bits &= MLXBF_I2C_CAUSE_MASTER_ARBITER_BITS_MASK;

	/*
	 * Parse both Cause and Master GW bits, then return transaction status.
	 */

	master_status_bits = readl(priv->smbus->io +
					MLXBF_I2C_SMBUS_MASTER_STATUS);
	master_status_bits &= MLXBF_I2C_SMBUS_MASTER_STATUS_MASK;

	if (mlxbf_i2c_smbus_transaction_success(master_status_bits,
						cause_status_bits))
		return 0;

	/*
	 * In case of timeout on GW busy, the ISR will clear busy bit but
	 * transaction ended bits cause will not be set so the transaction
	 * fails. Then, we must check Master GW status bits.
	 */
	if ((master_status_bits & MLXBF_I2C_SMBUS_MASTER_STATUS_ERROR) &&
	    (cause_status_bits & (MLXBF_I2C_CAUSE_TRANSACTION_ENDED |
				  MLXBF_I2C_CAUSE_M_GW_BUSY_FALL)))
		return -EIO;

	if (cause_status_bits & MLXBF_I2C_CAUSE_MASTER_STATUS_ERROR)
		return -EAGAIN;

	return -ETIMEDOUT;
}

static void mlxbf_i2c_smbus_write_data(struct mlxbf_i2c_priv *priv,
				       const u8 *data, u8 length, u32 addr)
{
	u8 offset, aligned_length;
	u32 data32;

	aligned_length = round_up(length, 4);

	/*
	 * Copy data bytes from 4-byte aligned source buffer.
	 * Data copied to the Master GW Data Descriptor MUST be shifted
	 * left so the data starts at the MSB of the descriptor registers
	 * as required by the underlying hardware. Enable byte swapping
	 * when writing data bytes to the 32 * 32-bit HW Data registers
	 * a.k.a Master GW Data Descriptor.
	 */
	for (offset = 0; offset < aligned_length; offset += sizeof(u32)) {
		data32 = *((u32 *)(data + offset));
		iowrite32be(data32, priv->smbus->io + addr + offset);
	}
}

static void mlxbf_i2c_smbus_read_data(struct mlxbf_i2c_priv *priv,
				      u8 *data, u8 length, u32 addr)
{
	u32 data32, mask;
	u8 byte, offset;

	mask = sizeof(u32) - 1;

	/*
	 * Data bytes in the Master GW Data Descriptor are shifted left
	 * so the data starts at the MSB of the descriptor registers as
	 * set by the underlying hardware. Enable byte swapping while
	 * reading data bytes from the 32 * 32-bit HW Data registers
	 * a.k.a Master GW Data Descriptor.
	 */

	for (offset = 0; offset < (length & ~mask); offset += sizeof(u32)) {
		data32 = ioread32be(priv->smbus->io + addr + offset);
		*((u32 *)(data + offset)) = data32;
	}

	if (!(length & mask))
		return;

	data32 = ioread32be(priv->smbus->io + addr + offset);

	for (byte = 0; byte < (length & mask); byte++) {
		data[offset + byte] = data32 & GENMASK(7, 0);
		data32 = ror32(data32, MLXBF_I2C_SHIFT_8);
	}
}

static int mlxbf_i2c_smbus_enable(struct mlxbf_i2c_priv *priv, u8 slave,
				  u8 len, u8 block_en, u8 pec_en, bool read)
{
	u32 command;

	/* Set Master GW control word. */
	if (read) {
		command = MLXBF_I2C_MASTER_ENABLE_READ;
		command |= rol32(len, MLXBF_I2C_MASTER_READ_SHIFT);
	} else {
		command = MLXBF_I2C_MASTER_ENABLE_WRITE;
		command |= rol32(len, MLXBF_I2C_MASTER_WRITE_SHIFT);
	}
	command |= rol32(slave, MLXBF_I2C_MASTER_SLV_ADDR_SHIFT);
	command |= rol32(block_en, MLXBF_I2C_MASTER_PARSE_EXP_SHIFT);
	command |= rol32(pec_en, MLXBF_I2C_MASTER_SEND_PEC_SHIFT);

	/* Clear status bits. */
	writel(0x0, priv->smbus->io + MLXBF_I2C_SMBUS_MASTER_STATUS);
	/* Set the cause data. */
	writel(~0x0, priv->mst_cause->io + MLXBF_I2C_CAUSE_OR_CLEAR);
	/* Zero PEC byte. */
	writel(0x0, priv->smbus->io + MLXBF_I2C_SMBUS_MASTER_PEC);
	/* Zero byte count. */
	writel(0x0, priv->smbus->io + MLXBF_I2C_SMBUS_RS_BYTES);

	/* GW activation. */
	writel(command, priv->smbus->io + MLXBF_I2C_SMBUS_MASTER_GW);

	/*
	 * Poll master status and check status bits. An ACK is sent when
	 * completing writing data to the bus (Master 'byte_count_done' bit
	 * is set to 1).
	 */
	return mlxbf_i2c_smbus_check_status(priv);
}

static int
mlxbf_i2c_smbus_start_transaction(struct mlxbf_i2c_priv *priv,
				  struct mlxbf_i2c_smbus_request *request)
{
	u8 data_desc[MLXBF_I2C_MASTER_DATA_DESC_SIZE] = { 0 };
	u8 op_idx, data_idx, data_len, write_len, read_len;
	struct mlxbf_i2c_smbus_operation *operation;
	u8 read_en, write_en, block_en, pec_en;
	u8 slave, flags, addr;
	u8 *read_buf;
	int ret = 0;

	if (request->operation_cnt > MLXBF_I2C_SMBUS_MAX_OP_CNT)
		return -EINVAL;

	read_buf = NULL;
	data_idx = 0;
	read_en = 0;
	write_en = 0;
	write_len = 0;
	read_len = 0;
	block_en = 0;
	pec_en = 0;
	slave = request->slave & GENMASK(6, 0);
	addr = slave << 1;

	/* First of all, check whether the HW is idle. */
	if (WARN_ON(!mlxbf_smbus_master_wait_for_idle(priv)))
		return -EBUSY;

	/* Set first byte. */
	data_desc[data_idx++] = addr;

	for (op_idx = 0; op_idx < request->operation_cnt; op_idx++) {
		operation = &request->operation[op_idx];
		flags = operation->flags;

		/*
		 * Note that read and write operations might be handled by a
		 * single command. If the MLXBF_I2C_F_SMBUS_OPERATION is set
		 * then write command byte and set the optional SMBus specific
		 * bits such as block_en and pec_en. These bits MUST be
		 * submitted by the first operation only.
		 */
		if (op_idx == 0 && flags & MLXBF_I2C_F_SMBUS_OPERATION) {
			block_en = flags & MLXBF_I2C_F_SMBUS_BLOCK;
			pec_en = flags & MLXBF_I2C_F_SMBUS_PEC;
		}

		if (flags & MLXBF_I2C_F_WRITE) {
			write_en = 1;
			write_len += operation->length;
			memcpy(data_desc + data_idx,
			       operation->buffer, operation->length);
			data_idx += operation->length;
		}
		/*
		 * We assume that read operations are performed only once per
		 * SMBus transaction. *TBD* protect this statement so it won't
		 * be executed twice? or return an error if we try to read more
		 * than once?
		 */
		if (flags & MLXBF_I2C_F_READ) {
			read_en = 1;
			/* Subtract 1 as required by HW. */
			read_len = operation->length - 1;
			read_buf = operation->buffer;
		}
	}

	/* Set Master GW data descriptor. */
	data_len = write_len + 1; /* Add one byte of the slave address. */
	/*
	 * Note that data_len cannot be 0. Indeed, the slave address byte
	 * must be written to the data registers.
	 */
	mlxbf_i2c_smbus_write_data(priv, (const u8 *)data_desc, data_len,
				   MLXBF_I2C_MASTER_DATA_DESC_ADDR);

	if (write_en) {
		ret = mlxbf_i2c_smbus_enable(priv, slave, write_len, block_en,
					 pec_en, 0);
		if (ret)
			return ret;
	}

	if (read_en) {
		/* Write slave address to Master GW data descriptor. */
		mlxbf_i2c_smbus_write_data(priv, (const u8 *)&addr, 1,
					   MLXBF_I2C_MASTER_DATA_DESC_ADDR);
		ret = mlxbf_i2c_smbus_enable(priv, slave, read_len, block_en,
					 pec_en, 1);
		if (!ret) {
			/* Get Master GW data descriptor. */
			mlxbf_i2c_smbus_read_data(priv, data_desc, read_len + 1,
					     MLXBF_I2C_MASTER_DATA_DESC_ADDR);

			/* Get data from Master GW data descriptor. */
			memcpy(read_buf, data_desc, read_len + 1);
		}

		/*
		 * After a read operation the SMBus FSM ps (present state)
		 * needs to be 'manually' reset. This should be removed in
		 * next tag integration.
		 */
		writel(MLXBF_I2C_SMBUS_MASTER_FSM_PS_STATE_MASK,
			priv->smbus->io + MLXBF_I2C_SMBUS_MASTER_FSM);
	}

	return ret;
}

/* I2C SMBus protocols. */

static void
mlxbf_i2c_smbus_quick_command(struct mlxbf_i2c_smbus_request *request,
			      u8 read)
{
	request->operation_cnt = MLXBF_I2C_SMBUS_OP_CNT_1;

	request->operation[0].length = 0;
	request->operation[0].flags = MLXBF_I2C_F_WRITE;
	request->operation[0].flags |= read ? MLXBF_I2C_F_READ : 0;
}

static void mlxbf_i2c_smbus_byte_func(struct mlxbf_i2c_smbus_request *request,
				      u8 *data, bool read, bool pec_check)
{
	request->operation_cnt = MLXBF_I2C_SMBUS_OP_CNT_1;

	request->operation[0].length = 1;
	request->operation[0].length += pec_check;

	request->operation[0].flags = MLXBF_I2C_F_SMBUS_OPERATION;
	request->operation[0].flags |= read ?
				MLXBF_I2C_F_READ : MLXBF_I2C_F_WRITE;
	request->operation[0].flags |= pec_check ? MLXBF_I2C_F_SMBUS_PEC : 0;

	request->operation[0].buffer = data;
}

static void
mlxbf_i2c_smbus_data_byte_func(struct mlxbf_i2c_smbus_request *request,
			       u8 *command, u8 *data, bool read, bool pec_check)
{
	request->operation_cnt = MLXBF_I2C_SMBUS_OP_CNT_2;

	request->operation[0].length = 1;
	request->operation[0].flags =
			MLXBF_I2C_F_SMBUS_OPERATION | MLXBF_I2C_F_WRITE;
	request->operation[0].flags |= pec_check ? MLXBF_I2C_F_SMBUS_PEC : 0;
	request->operation[0].buffer = command;

	request->operation[1].length = 1;
	request->operation[1].length += pec_check;
	request->operation[1].flags = read ?
				MLXBF_I2C_F_READ : MLXBF_I2C_F_WRITE;
	request->operation[1].buffer = data;
}

static void
mlxbf_i2c_smbus_data_word_func(struct mlxbf_i2c_smbus_request *request,
			       u8 *command, u8 *data, bool read, bool pec_check)
{
	request->operation_cnt = MLXBF_I2C_SMBUS_OP_CNT_2;

	request->operation[0].length = 1;
	request->operation[0].flags =
			MLXBF_I2C_F_SMBUS_OPERATION | MLXBF_I2C_F_WRITE;
	request->operation[0].flags |= pec_check ? MLXBF_I2C_F_SMBUS_PEC : 0;
	request->operation[0].buffer = command;

	request->operation[1].length = 2;
	request->operation[1].length += pec_check;
	request->operation[1].flags = read ?
				MLXBF_I2C_F_READ : MLXBF_I2C_F_WRITE;
	request->operation[1].buffer = data;
}

static void
mlxbf_i2c_smbus_i2c_block_func(struct mlxbf_i2c_smbus_request *request,
			       u8 *command, u8 *data, u8 *data_len, bool read,
			       bool pec_check)
{
	request->operation_cnt = MLXBF_I2C_SMBUS_OP_CNT_2;

	request->operation[0].length = 1;
	request->operation[0].flags =
			MLXBF_I2C_F_SMBUS_OPERATION | MLXBF_I2C_F_WRITE;
	request->operation[0].flags |= pec_check ? MLXBF_I2C_F_SMBUS_PEC : 0;
	request->operation[0].buffer = command;

	/*
	 * As specified in the standard, the max number of bytes to read/write
	 * per block operation is 32 bytes. In Golan code, the controller can
	 * read up to 128 bytes and write up to 127 bytes.
	 */
	request->operation[1].length =
	    (*data_len + pec_check > I2C_SMBUS_BLOCK_MAX) ?
	    I2C_SMBUS_BLOCK_MAX : *data_len + pec_check;
	request->operation[1].flags = read ?
				MLXBF_I2C_F_READ : MLXBF_I2C_F_WRITE;
	/*
	 * Skip the first data byte, which corresponds to the number of bytes
	 * to read/write.
	 */
	request->operation[1].buffer = data + 1;

	*data_len = request->operation[1].length;

	/* Set the number of byte to read. This will be used by userspace. */
	if (read)
		data[0] = *data_len;
}

static void mlxbf_i2c_smbus_block_func(struct mlxbf_i2c_smbus_request *request,
				       u8 *command, u8 *data, u8 *data_len,
				       bool read, bool pec_check)
{
	request->operation_cnt = MLXBF_I2C_SMBUS_OP_CNT_2;

	request->operation[0].length = 1;
	request->operation[0].flags =
			MLXBF_I2C_F_SMBUS_OPERATION | MLXBF_I2C_F_WRITE;
	request->operation[0].flags |= MLXBF_I2C_F_SMBUS_BLOCK;
	request->operation[0].flags |= pec_check ? MLXBF_I2C_F_SMBUS_PEC : 0;
	request->operation[0].buffer = command;

	request->operation[1].length =
	    (*data_len + pec_check > I2C_SMBUS_BLOCK_MAX) ?
	    I2C_SMBUS_BLOCK_MAX : *data_len + pec_check;
	request->operation[1].flags = read ?
				MLXBF_I2C_F_READ : MLXBF_I2C_F_WRITE;
	request->operation[1].buffer = data + 1;

	*data_len = request->operation[1].length;

	/* Set the number of bytes to read. This will be used by userspace. */
	if (read)
		data[0] = *data_len;
}

static void
mlxbf_i2c_smbus_process_call_func(struct mlxbf_i2c_smbus_request *request,
				  u8 *command, u8 *data, bool pec_check)
{
	request->operation_cnt = MLXBF_I2C_SMBUS_OP_CNT_3;

	request->operation[0].length = 1;
	request->operation[0].flags =
			MLXBF_I2C_F_SMBUS_OPERATION | MLXBF_I2C_F_WRITE;
	request->operation[0].flags |= MLXBF_I2C_F_SMBUS_BLOCK;
	request->operation[0].flags |= pec_check ? MLXBF_I2C_F_SMBUS_PEC : 0;
	request->operation[0].buffer = command;

	request->operation[1].length = 2;
	request->operation[1].flags = MLXBF_I2C_F_WRITE;
	request->operation[1].buffer = data;

	request->operation[2].length = 3;
	request->operation[2].flags = MLXBF_I2C_F_READ;
	request->operation[2].buffer = data;
}

static void
mlxbf_i2c_smbus_blk_process_call_func(struct mlxbf_i2c_smbus_request *request,
				      u8 *command, u8 *data, u8 *data_len,
				      bool pec_check)
{
	u32 length;

	request->operation_cnt = MLXBF_I2C_SMBUS_OP_CNT_3;

	request->operation[0].length = 1;
	request->operation[0].flags =
			MLXBF_I2C_F_SMBUS_OPERATION | MLXBF_I2C_F_WRITE;
	request->operation[0].flags |= MLXBF_I2C_F_SMBUS_BLOCK;
	request->operation[0].flags |= (pec_check) ? MLXBF_I2C_F_SMBUS_PEC : 0;
	request->operation[0].buffer = command;

	length = (*data_len + pec_check > I2C_SMBUS_BLOCK_MAX) ?
	    I2C_SMBUS_BLOCK_MAX : *data_len + pec_check;

	request->operation[1].length = length - pec_check;
	request->operation[1].flags = MLXBF_I2C_F_WRITE;
	request->operation[1].buffer = data;

	request->operation[2].length = length;
	request->operation[2].flags = MLXBF_I2C_F_READ;
	request->operation[2].buffer = data;

	*data_len = length; /* including PEC byte. */
}

/* Initialization functions. */

static bool mlxbf_i2c_has_chip_type(struct mlxbf_i2c_priv *priv, u8 type)
{
	return priv->chip->type == type;
}

static struct mlxbf_i2c_resource *
mlxbf_i2c_get_shared_resource(struct mlxbf_i2c_priv *priv, u8 type)
{
	const struct mlxbf_i2c_chip_info *chip = priv->chip;
	struct mlxbf_i2c_resource *res;
	u8 res_idx = 0;

	for (res_idx = 0; res_idx < MLXBF_I2C_SHARED_RES_MAX; res_idx++) {
		res = chip->shared_res[res_idx];
		if (res && res->type == type)
			return res;
	}

	return NULL;
}

static int mlxbf_i2c_init_resource(struct platform_device *pdev,
				   struct mlxbf_i2c_resource **res,
				   u8 type)
{
	struct mlxbf_i2c_resource *tmp_res;
	struct device *dev = &pdev->dev;

	if (!res || *res || type >= MLXBF_I2C_END_RES)
		return -EINVAL;

	tmp_res = devm_kzalloc(dev, sizeof(struct mlxbf_i2c_resource),
			       GFP_KERNEL);
	if (!tmp_res)
		return -ENOMEM;

	tmp_res->params = platform_get_resource(pdev, IORESOURCE_MEM, type);
	if (!tmp_res->params) {
		devm_kfree(dev, tmp_res);
		return -EIO;
	}

	tmp_res->io = devm_ioremap_resource(dev, tmp_res->params);
	if (IS_ERR(tmp_res->io)) {
		devm_kfree(dev, tmp_res);
		return PTR_ERR(tmp_res->io);
	}

	tmp_res->type = type;

	*res = tmp_res;

	return 0;
}

static u32 mlxbf_i2c_get_ticks(struct mlxbf_i2c_priv *priv, u64 nanoseconds,
			       bool minimum)
{
	u64 frequency;
	u32 ticks;

	/*
	 * Compute ticks as follow:
	 *
	 *           Ticks
	 * Time = --------- x 10^9    =>    Ticks = Time x Frequency x 10^-9
	 *         Frequency
	 */
	frequency = priv->frequency;
	ticks = (nanoseconds * frequency) / MLXBF_I2C_FREQUENCY_1GHZ;
	/*
	 * The number of ticks is rounded down and if minimum is equal to 1
	 * then add one tick.
	 */
	if (minimum)
		ticks++;

	return ticks;
}

static u32 mlxbf_i2c_set_timer(struct mlxbf_i2c_priv *priv, u64 nsec, bool opt,
			       u32 mask, u8 shift)
{
	u32 val = (mlxbf_i2c_get_ticks(priv, nsec, opt) & mask) << shift;

	return val;
}

static void mlxbf_i2c_set_timings(struct mlxbf_i2c_priv *priv,
				  const struct mlxbf_i2c_timings *timings)
{
	u32 timer;

	timer = mlxbf_i2c_set_timer(priv, timings->scl_high,
				    false, MLXBF_I2C_MASK_16,
				    MLXBF_I2C_SHIFT_0);
	timer |= mlxbf_i2c_set_timer(priv, timings->scl_low,
				     false, MLXBF_I2C_MASK_16,
				     MLXBF_I2C_SHIFT_16);
	writel(timer, priv->smbus->io +
		MLXBF_I2C_SMBUS_TIMER_SCL_LOW_SCL_HIGH);

	timer = mlxbf_i2c_set_timer(priv, timings->sda_rise, false,
				    MLXBF_I2C_MASK_8, MLXBF_I2C_SHIFT_0);
	timer |= mlxbf_i2c_set_timer(priv, timings->sda_fall, false,
				     MLXBF_I2C_MASK_8, MLXBF_I2C_SHIFT_8);
	timer |= mlxbf_i2c_set_timer(priv, timings->scl_rise, false,
				     MLXBF_I2C_MASK_8, MLXBF_I2C_SHIFT_16);
	timer |= mlxbf_i2c_set_timer(priv, timings->scl_fall, false,
				     MLXBF_I2C_MASK_8, MLXBF_I2C_SHIFT_24);
	writel(timer, priv->smbus->io +
		MLXBF_I2C_SMBUS_TIMER_FALL_RISE_SPIKE);

	timer = mlxbf_i2c_set_timer(priv, timings->hold_start, true,
				    MLXBF_I2C_MASK_16, MLXBF_I2C_SHIFT_0);
	timer |= mlxbf_i2c_set_timer(priv, timings->hold_data, true,
				     MLXBF_I2C_MASK_16, MLXBF_I2C_SHIFT_16);
	writel(timer, priv->smbus->io + MLXBF_I2C_SMBUS_TIMER_THOLD);

	timer = mlxbf_i2c_set_timer(priv, timings->setup_start, true,
				    MLXBF_I2C_MASK_16, MLXBF_I2C_SHIFT_0);
	timer |= mlxbf_i2c_set_timer(priv, timings->setup_stop, true,
				     MLXBF_I2C_MASK_16, MLXBF_I2C_SHIFT_16);
	writel(timer, priv->smbus->io +
		MLXBF_I2C_SMBUS_TIMER_TSETUP_START_STOP);

	timer = mlxbf_i2c_set_timer(priv, timings->setup_data, true,
				    MLXBF_I2C_MASK_16, MLXBF_I2C_SHIFT_0);
	writel(timer, priv->smbus->io + MLXBF_I2C_SMBUS_TIMER_TSETUP_DATA);

	timer = mlxbf_i2c_set_timer(priv, timings->buf, false,
				    MLXBF_I2C_MASK_16, MLXBF_I2C_SHIFT_0);
	timer |= mlxbf_i2c_set_timer(priv, timings->thigh_max, false,
				     MLXBF_I2C_MASK_16, MLXBF_I2C_SHIFT_16);
	writel(timer, priv->smbus->io + MLXBF_I2C_SMBUS_THIGH_MAX_TBUF);

	timer = timings->timeout;
	writel(timer, priv->smbus->io + MLXBF_I2C_SMBUS_SCL_LOW_TIMEOUT);
}

enum mlxbf_i2c_timings_config {
	MLXBF_I2C_TIMING_CONFIG_100KHZ,
	MLXBF_I2C_TIMING_CONFIG_400KHZ,
	MLXBF_I2C_TIMING_CONFIG_1000KHZ,
};

/*
 * Note that the mlxbf_i2c_timings->timeout value is not related to the
 * bus frequency, it is impacted by the time it takes the driver to
 * complete data transmission before transaction abort.
 */
static const struct mlxbf_i2c_timings mlxbf_i2c_timings[] = {
	[MLXBF_I2C_TIMING_CONFIG_100KHZ] = {
		.scl_high = 4810,
		.scl_low = 5000,
		.hold_start = 4000,
		.setup_start = 4800,
		.setup_stop = 4000,
		.setup_data = 250,
		.sda_rise = 50,
		.sda_fall = 50,
		.scl_rise = 50,
		.scl_fall = 50,
		.hold_data = 300,
		.buf = 20000,
		.thigh_max = 5000,
		.timeout = 106500
	},
	[MLXBF_I2C_TIMING_CONFIG_400KHZ] = {
		.scl_high = 1011,
		.scl_low = 1300,
		.hold_start = 600,
		.setup_start = 700,
		.setup_stop = 600,
		.setup_data = 100,
		.sda_rise = 50,
		.sda_fall = 50,
		.scl_rise = 50,
		.scl_fall = 50,
		.hold_data = 300,
		.buf = 20000,
		.thigh_max = 5000,
		.timeout = 106500
	},
	[MLXBF_I2C_TIMING_CONFIG_1000KHZ] = {
		.scl_high = 600,
		.scl_low = 1300,
		.hold_start = 600,
		.setup_start = 600,
		.setup_stop = 600,
		.setup_data = 100,
		.sda_rise = 50,
		.sda_fall = 50,
		.scl_rise = 50,
		.scl_fall = 50,
		.hold_data = 300,
		.buf = 20000,
		.thigh_max = 5000,
		.timeout = 106500
	}
};

static int mlxbf_i2c_init_timings(struct platform_device *pdev,
				  struct mlxbf_i2c_priv *priv)
{
	enum mlxbf_i2c_timings_config config_idx;
	struct device *dev = &pdev->dev;
	u32 config_khz;

	int ret;

	ret = device_property_read_u32(dev, "clock-frequency", &config_khz);
	if (ret < 0)
		config_khz = I2C_MAX_STANDARD_MODE_FREQ;

	switch (config_khz) {
	default:
		/* Default settings is 100 KHz. */
		pr_warn("Illegal value %d: defaulting to 100 KHz\n",
			config_khz);
		fallthrough;
	case I2C_MAX_STANDARD_MODE_FREQ:
		config_idx = MLXBF_I2C_TIMING_CONFIG_100KHZ;
		break;

	case I2C_MAX_FAST_MODE_FREQ:
		config_idx = MLXBF_I2C_TIMING_CONFIG_400KHZ;
		break;

	case I2C_MAX_FAST_MODE_PLUS_FREQ:
		config_idx = MLXBF_I2C_TIMING_CONFIG_1000KHZ;
		break;
	}

	mlxbf_i2c_set_timings(priv, &mlxbf_i2c_timings[config_idx]);

	return 0;
}

static int mlxbf_i2c_get_gpio(struct platform_device *pdev,
			      struct mlxbf_i2c_priv *priv)
{
	struct mlxbf_i2c_resource *gpio_res;
	struct device *dev = &pdev->dev;
	struct resource	*params;
	resource_size_t size;

	gpio_res = mlxbf_i2c_get_shared_resource(priv, MLXBF_I2C_GPIO_RES);
	if (!gpio_res)
		return -EPERM;

	/*
	 * The GPIO region in TYU space is shared among I2C busses.
	 * This function MUST be serialized to avoid racing when
	 * claiming the memory region and/or setting up the GPIO.
	 */
	lockdep_assert_held(gpio_res->lock);

	/* Check whether the memory map exist. */
	if (gpio_res->io)
		return 0;

	params = gpio_res->params;
	size = resource_size(params);

	if (!devm_request_mem_region(dev, params->start, size, params->name))
		return -EFAULT;

	gpio_res->io = devm_ioremap(dev, params->start, size);
	if (!gpio_res->io) {
		devm_release_mem_region(dev, params->start, size);
		return -ENOMEM;
	}

	return 0;
}

static int mlxbf_i2c_release_gpio(struct platform_device *pdev,
				  struct mlxbf_i2c_priv *priv)
{
	struct mlxbf_i2c_resource *gpio_res;
	struct device *dev = &pdev->dev;
	struct resource	*params;

	gpio_res = mlxbf_i2c_get_shared_resource(priv, MLXBF_I2C_GPIO_RES);
	if (!gpio_res)
		return 0;

	mutex_lock(gpio_res->lock);

	if (gpio_res->io) {
		/* Release the GPIO resource. */
		params = gpio_res->params;
		devm_iounmap(dev, gpio_res->io);
		devm_release_mem_region(dev, params->start,
					resource_size(params));
	}

	mutex_unlock(gpio_res->lock);

	return 0;
}

static int mlxbf_i2c_get_corepll(struct platform_device *pdev,
				 struct mlxbf_i2c_priv *priv)
{
	struct mlxbf_i2c_resource *corepll_res;
	struct device *dev = &pdev->dev;
	struct resource *params;
	resource_size_t size;

	corepll_res = mlxbf_i2c_get_shared_resource(priv,
						    MLXBF_I2C_COREPLL_RES);
	if (!corepll_res)
		return -EPERM;

	/*
	 * The COREPLL region in TYU space is shared among I2C busses.
	 * This function MUST be serialized to avoid racing when
	 * claiming the memory region.
	 */
	lockdep_assert_held(corepll_res->lock);

	/* Check whether the memory map exist. */
	if (corepll_res->io)
		return 0;

	params = corepll_res->params;
	size = resource_size(params);

	if (!devm_request_mem_region(dev, params->start, size, params->name))
		return -EFAULT;

	corepll_res->io = devm_ioremap(dev, params->start, size);
	if (!corepll_res->io) {
		devm_release_mem_region(dev, params->start, size);
		return -ENOMEM;
	}

	return 0;
}

static int mlxbf_i2c_release_corepll(struct platform_device *pdev,
				     struct mlxbf_i2c_priv *priv)
{
	struct mlxbf_i2c_resource *corepll_res;
	struct device *dev = &pdev->dev;
	struct resource *params;

	corepll_res = mlxbf_i2c_get_shared_resource(priv,
						    MLXBF_I2C_COREPLL_RES);

	mutex_lock(corepll_res->lock);

	if (corepll_res->io) {
		/* Release the CorePLL resource. */
		params = corepll_res->params;
		devm_iounmap(dev, corepll_res->io);
		devm_release_mem_region(dev, params->start,
					resource_size(params));
	}

	mutex_unlock(corepll_res->lock);

	return 0;
}

static int mlxbf_i2c_init_master(struct platform_device *pdev,
				 struct mlxbf_i2c_priv *priv)
{
	struct mlxbf_i2c_resource *gpio_res;
	struct device *dev = &pdev->dev;
	u32 config_reg;
	int ret;

	/* This configuration is only needed for BlueField 1. */
	if (!mlxbf_i2c_has_chip_type(priv, MLXBF_I2C_CHIP_TYPE_1))
		return 0;

	gpio_res = mlxbf_i2c_get_shared_resource(priv, MLXBF_I2C_GPIO_RES);
	if (!gpio_res)
		return -EPERM;

	/*
	 * The GPIO region in TYU space is shared among I2C busses.
	 * This function MUST be serialized to avoid racing when
	 * claiming the memory region and/or setting up the GPIO.
	 */

	mutex_lock(gpio_res->lock);

	ret = mlxbf_i2c_get_gpio(pdev, priv);
	if (ret < 0) {
		dev_err(dev, "Failed to get gpio resource");
		mutex_unlock(gpio_res->lock);
		return ret;
	}

	/*
	 * TYU - Configuration for GPIO pins. Those pins must be asserted in
	 * MLXBF_I2C_GPIO_0_FUNC_EN_0, i.e. GPIO 0 is controlled by HW, and must
	 * be reset in MLXBF_I2C_GPIO_0_FORCE_OE_EN, i.e. GPIO_OE will be driven
	 * instead of HW_OE.
	 * For now, we do not reset the GPIO state when the driver is removed.
	 * First, it is not necessary to disable the bus since we are using
	 * the same busses. Then, some busses might be shared among Linux and
	 * platform firmware; disabling the bus might compromise the system
	 * functionality.
	 */
	config_reg = readl(gpio_res->io + MLXBF_I2C_GPIO_0_FUNC_EN_0);
	config_reg = MLXBF_I2C_GPIO_SMBUS_GW_ASSERT_PINS(priv->bus,
							 config_reg);
	writel(config_reg, gpio_res->io + MLXBF_I2C_GPIO_0_FUNC_EN_0);

	config_reg = readl(gpio_res->io + MLXBF_I2C_GPIO_0_FORCE_OE_EN);
	config_reg = MLXBF_I2C_GPIO_SMBUS_GW_RESET_PINS(priv->bus,
							config_reg);
	writel(config_reg, gpio_res->io + MLXBF_I2C_GPIO_0_FORCE_OE_EN);

	mutex_unlock(gpio_res->lock);

	return 0;
}

static u64 mlxbf_calculate_freq_from_tyu(struct mlxbf_i2c_resource *corepll_res)
{
	u64 core_frequency, pad_frequency;
	u8 core_od, core_r;
	u32 corepll_val;
	u16 core_f;

	pad_frequency = MLXBF_I2C_PLL_IN_FREQ;

	corepll_val = readl(corepll_res->io + MLXBF_I2C_CORE_PLL_REG1);

	/* Get Core PLL configuration bits. */
	core_f = rol32(corepll_val, MLXBF_I2C_COREPLL_CORE_F_TYU_SHIFT) &
			MLXBF_I2C_COREPLL_CORE_F_TYU_MASK;
	core_od = rol32(corepll_val, MLXBF_I2C_COREPLL_CORE_OD_TYU_SHIFT) &
			MLXBF_I2C_COREPLL_CORE_OD_TYU_MASK;
	core_r = rol32(corepll_val, MLXBF_I2C_COREPLL_CORE_R_TYU_SHIFT) &
			MLXBF_I2C_COREPLL_CORE_R_TYU_MASK;

	/*
	 * Compute PLL output frequency as follow:
	 *
	 *                                       CORE_F + 1
	 * PLL_OUT_FREQ = PLL_IN_FREQ * ----------------------------
	 *                              (CORE_R + 1) * (CORE_OD + 1)
	 *
	 * Where PLL_OUT_FREQ and PLL_IN_FREQ refer to CoreFrequency
	 * and PadFrequency, respectively.
	 */
	core_frequency = pad_frequency * (++core_f);
	core_frequency /= (++core_r) * (++core_od);

	return core_frequency;
}

static u64 mlxbf_calculate_freq_from_yu(struct mlxbf_i2c_resource *corepll_res)
{
	u32 corepll_reg1_val, corepll_reg2_val;
	u64 corepll_frequency, pad_frequency;
	u8 core_od, core_r;
	u32 core_f;

	pad_frequency = MLXBF_I2C_PLL_IN_FREQ;

	corepll_reg1_val = readl(corepll_res->io + MLXBF_I2C_CORE_PLL_REG1);
	corepll_reg2_val = readl(corepll_res->io + MLXBF_I2C_CORE_PLL_REG2);

	/* Get Core PLL configuration bits */
	core_f = rol32(corepll_reg1_val, MLXBF_I2C_COREPLL_CORE_F_YU_SHIFT) &
			MLXBF_I2C_COREPLL_CORE_F_YU_MASK;
	core_r = rol32(corepll_reg1_val, MLXBF_I2C_COREPLL_CORE_R_YU_SHIFT) &
			MLXBF_I2C_COREPLL_CORE_R_YU_MASK;
	core_od = rol32(corepll_reg2_val,  MLXBF_I2C_COREPLL_CORE_OD_YU_SHIFT) &
			MLXBF_I2C_COREPLL_CORE_OD_YU_MASK;

	/*
	 * Compute PLL output frequency as follow:
	 *
	 *                                     CORE_F / 16384
	 * PLL_OUT_FREQ = PLL_IN_FREQ * ----------------------------
	 *                              (CORE_R + 1) * (CORE_OD + 1)
	 *
	 * Where PLL_OUT_FREQ and PLL_IN_FREQ refer to CoreFrequency
	 * and PadFrequency, respectively.
	 */
	corepll_frequency = (pad_frequency * core_f) / MLNXBF_I2C_COREPLL_CONST;
	corepll_frequency /= (++core_r) * (++core_od);

	return corepll_frequency;
}

static int mlxbf_i2c_calculate_corepll_freq(struct platform_device *pdev,
					    struct mlxbf_i2c_priv *priv)
{
	const struct mlxbf_i2c_chip_info *chip = priv->chip;
	struct mlxbf_i2c_resource *corepll_res;
	struct device *dev = &pdev->dev;
	u64 *freq = &priv->frequency;
	int ret;

	corepll_res = mlxbf_i2c_get_shared_resource(priv,
						    MLXBF_I2C_COREPLL_RES);
	if (!corepll_res)
		return -EPERM;

	/*
	 * First, check whether the TYU core Clock frequency is set.
	 * The TYU core frequency is the same for all I2C busses; when
	 * the first device gets probed the frequency is determined and
	 * stored into a globally visible variable. So, first of all,
	 * check whether the frequency is already set. Here, we assume
	 * that the frequency is expected to be greater than 0.
	 */
	mutex_lock(corepll_res->lock);
	if (!mlxbf_i2c_corepll_frequency) {
		if (!chip->calculate_freq) {
			mutex_unlock(corepll_res->lock);
			return -EPERM;
		}

		ret = mlxbf_i2c_get_corepll(pdev, priv);
		if (ret < 0) {
			dev_err(dev, "Failed to get corePLL resource");
			mutex_unlock(corepll_res->lock);
			return ret;
		}

		mlxbf_i2c_corepll_frequency = chip->calculate_freq(corepll_res);
	}
	mutex_unlock(corepll_res->lock);

	*freq = mlxbf_i2c_corepll_frequency;

	return 0;
}

static int mlxbf_slave_enable(struct mlxbf_i2c_priv *priv, u8 addr)
{
	u32 slave_reg, slave_reg_tmp, slave_reg_avail, slave_addr_mask;
	u8 reg, reg_cnt, byte, addr_tmp, reg_avail, byte_avail;
	bool avail, disabled;

	disabled = false;
	avail = false;

	if (!priv)
		return -EPERM;

	reg_cnt = MLXBF_I2C_SMBUS_SLAVE_ADDR_CNT >> 2;
	slave_addr_mask = MLXBF_I2C_SMBUS_SLAVE_ADDR_MASK;

	/*
	 * Read the slave registers. There are 4 * 32-bit slave registers.
	 * Each slave register can hold up to 4 * 8-bit slave configuration
	 * (7-bit address, 1 status bit (1 if enabled, 0 if not)).
	 */
	for (reg = 0; reg < reg_cnt; reg++) {
		slave_reg = readl(priv->smbus->io +
				MLXBF_I2C_SMBUS_SLAVE_ADDR_CFG + reg * 0x4);
		/*
		 * Each register holds 4 slave addresses. So, we have to keep
		 * the byte order consistent with the value read in order to
		 * update the register correctly, if needed.
		 */
		slave_reg_tmp = slave_reg;
		for (byte = 0; byte < 4; byte++) {
			addr_tmp = slave_reg_tmp & GENMASK(7, 0);

			/*
			 * Mark the first available slave address slot, i.e. its
			 * enabled bit should be unset. This slot might be used
			 * later on to register our slave.
			 */
			if (!avail && !MLXBF_I2C_SLAVE_ADDR_ENABLED(addr_tmp)) {
				avail = true;
				reg_avail = reg;
				byte_avail = byte;
				slave_reg_avail = slave_reg;
			}

			/*
			 * Parse slave address bytes and check whether the
			 * slave address already exists and it's enabled,
			 * i.e. most significant bit is set.
			 */
			if ((addr_tmp & slave_addr_mask) == addr) {
				if (MLXBF_I2C_SLAVE_ADDR_ENABLED(addr_tmp))
					return 0;
				disabled = true;
				break;
			}

			/* Parse next byte. */
			slave_reg_tmp >>= 8;
		}

		/* Exit the loop if the slave address is found. */
		if (disabled)
			break;
	}

	if (!avail && !disabled)
		return -EINVAL; /* No room for a new slave address. */

	if (avail && !disabled) {
		reg = reg_avail;
		byte = byte_avail;
		/* Set the slave address. */
		slave_reg_avail &= ~(slave_addr_mask << (byte * 8));
		slave_reg_avail |= addr << (byte * 8);
		slave_reg = slave_reg_avail;
	}

	/* Enable the slave address and update the register. */
	slave_reg |= (1 << MLXBF_I2C_SMBUS_SLAVE_ADDR_EN_BIT) << (byte * 8);
	writel(slave_reg, priv->smbus->io + MLXBF_I2C_SMBUS_SLAVE_ADDR_CFG +
		reg * 0x4);

	return 0;
}

static int mlxbf_slave_disable(struct mlxbf_i2c_priv *priv)
{
	u32 slave_reg, slave_reg_tmp, slave_addr_mask;
	u8 addr, addr_tmp, reg, reg_cnt, slave_byte;
	struct i2c_client *client = priv->slave;
	bool exist;

	exist = false;

	addr = client->addr;
	reg_cnt = MLXBF_I2C_SMBUS_SLAVE_ADDR_CNT >> 2;
	slave_addr_mask = MLXBF_I2C_SMBUS_SLAVE_ADDR_MASK;

	/*
	 * Read the slave registers. There are 4 * 32-bit slave registers.
	 * Each slave register can hold up to 4 * 8-bit slave configuration
	 * (7-bit address, 1 status bit (1 if enabled, 0 if not)).
	 */
	for (reg = 0; reg < reg_cnt; reg++) {
		slave_reg = readl(priv->smbus->io +
				MLXBF_I2C_SMBUS_SLAVE_ADDR_CFG + reg * 0x4);

		/* Check whether the address slots are empty. */
		if (slave_reg == 0)
			continue;

		/*
		 * Each register holds 4 slave addresses. So, we have to keep
		 * the byte order consistent with the value read in order to
		 * update the register correctly, if needed.
		 */
		slave_reg_tmp = slave_reg;
		slave_byte = 0;
		while (slave_reg_tmp != 0) {
			addr_tmp = slave_reg_tmp & slave_addr_mask;
			/*
			 * Parse slave address bytes and check whether the
			 * slave address already exists.
			 */
			if (addr_tmp == addr) {
				exist = true;
				break;
			}

			/* Parse next byte. */
			slave_reg_tmp >>= 8;
			slave_byte += 1;
		}

		/* Exit the loop if the slave address is found. */
		if (exist)
			break;
	}

	if (!exist)
		return 0; /* Slave is not registered, nothing to do. */

	/* Cleanup the slave address slot. */
	slave_reg &= ~(GENMASK(7, 0) << (slave_byte * 8));
	writel(slave_reg, priv->smbus->io + MLXBF_I2C_SMBUS_SLAVE_ADDR_CFG +
		reg * 0x4);

	return 0;
}

static int mlxbf_i2c_init_coalesce(struct platform_device *pdev,
				   struct mlxbf_i2c_priv *priv)
{
	struct mlxbf_i2c_resource *coalesce_res;
	struct resource *params;
	resource_size_t size;
	int ret = 0;

	/*
	 * Unlike BlueField-1 platform, the coalesce registers is a dedicated
	 * resource in the next generations of BlueField.
	 */
	if (mlxbf_i2c_has_chip_type(priv, MLXBF_I2C_CHIP_TYPE_1)) {
		coalesce_res = mlxbf_i2c_get_shared_resource(priv,
						MLXBF_I2C_COALESCE_RES);
		if (!coalesce_res)
			return -EPERM;

		/*
		 * The Cause Coalesce group in TYU space is shared among
		 * I2C busses. This function MUST be serialized to avoid
		 * racing when claiming the memory region.
		 */
		lockdep_assert_held(mlxbf_i2c_gpio_res->lock);

		/* Check whether the memory map exist. */
		if (coalesce_res->io) {
			priv->coalesce = coalesce_res;
			return 0;
		}

		params = coalesce_res->params;
		size = resource_size(params);

		if (!request_mem_region(params->start, size, params->name))
			return -EFAULT;

		coalesce_res->io = ioremap(params->start, size);
		if (!coalesce_res->io) {
			release_mem_region(params->start, size);
			return -ENOMEM;
		}

		priv->coalesce = coalesce_res;

	} else {
		ret = mlxbf_i2c_init_resource(pdev, &priv->coalesce,
					      MLXBF_I2C_COALESCE_RES);
	}

	return ret;
}

static int mlxbf_i2c_release_coalesce(struct platform_device *pdev,
				      struct mlxbf_i2c_priv *priv)
{
	struct mlxbf_i2c_resource *coalesce_res;
	struct device *dev = &pdev->dev;
	struct resource *params;
	resource_size_t size;

	coalesce_res = priv->coalesce;

	if (coalesce_res->io) {
		params = coalesce_res->params;
		size = resource_size(params);
		if (mlxbf_i2c_has_chip_type(priv, MLXBF_I2C_CHIP_TYPE_1)) {
			mutex_lock(coalesce_res->lock);
			iounmap(coalesce_res->io);
			release_mem_region(params->start, size);
			mutex_unlock(coalesce_res->lock);
		} else {
			devm_release_mem_region(dev, params->start, size);
		}
	}

	return 0;
}

static int mlxbf_i2c_init_slave(struct platform_device *pdev,
				struct mlxbf_i2c_priv *priv)
{
	struct device *dev = &pdev->dev;
	u32 int_reg;
	int ret;

	/* Reset FSM. */
	writel(0, priv->smbus->io + MLXBF_I2C_SMBUS_SLAVE_FSM);

	/*
	 * Enable slave cause interrupt bits. Drive
	 * MLXBF_I2C_CAUSE_READ_WAIT_FW_RESPONSE and
	 * MLXBF_I2C_CAUSE_WRITE_SUCCESS, these are enabled when an external
	 * masters issue a Read and Write, respectively. But, clear all
	 * interrupts first.
	 */
	writel(~0, priv->slv_cause->io + MLXBF_I2C_CAUSE_OR_CLEAR);
	int_reg = MLXBF_I2C_CAUSE_READ_WAIT_FW_RESPONSE;
	int_reg |= MLXBF_I2C_CAUSE_WRITE_SUCCESS;
	writel(int_reg, priv->slv_cause->io + MLXBF_I2C_CAUSE_OR_EVTEN0);

	/* Finally, set the 'ready' bit to start handling transactions. */
	writel(0x1, priv->smbus->io + MLXBF_I2C_SMBUS_SLAVE_READY);

	/* Initialize the cause coalesce resource. */
	ret = mlxbf_i2c_init_coalesce(pdev, priv);
	if (ret < 0) {
		dev_err(dev, "failed to initialize cause coalesce\n");
		return ret;
	}

	return 0;
}

static bool mlxbf_i2c_has_coalesce(struct mlxbf_i2c_priv *priv, bool *read,
				   bool *write)
{
	const struct mlxbf_i2c_chip_info *chip = priv->chip;
	u32 coalesce0_reg, cause_reg;
	u8 slave_shift, is_set;

	*write = false;
	*read = false;

	slave_shift = chip->type != MLXBF_I2C_CHIP_TYPE_1 ?
				MLXBF_I2C_CAUSE_YU_SLAVE_BIT :
				priv->bus + MLXBF_I2C_CAUSE_TYU_SLAVE_BIT;

	coalesce0_reg = readl(priv->coalesce->io + MLXBF_I2C_CAUSE_COALESCE_0);
	is_set = coalesce0_reg & (1 << slave_shift);

	if (!is_set)
		return false;

	/* Check the source of the interrupt, i.e. whether a Read or Write. */
	cause_reg = readl(priv->slv_cause->io + MLXBF_I2C_CAUSE_ARBITER);
	if (cause_reg & MLXBF_I2C_CAUSE_READ_WAIT_FW_RESPONSE)
		*read = true;
	else if (cause_reg & MLXBF_I2C_CAUSE_WRITE_SUCCESS)
		*write = true;

	/* Clear cause bits. */
	writel(~0x0, priv->slv_cause->io + MLXBF_I2C_CAUSE_OR_CLEAR);

	return true;
}

static bool mlxbf_smbus_slave_wait_for_idle(struct mlxbf_i2c_priv *priv,
					    u32 timeout)
{
	u32 mask = MLXBF_I2C_CAUSE_S_GW_BUSY_FALL;
	u32 addr = MLXBF_I2C_CAUSE_ARBITER;

	if (mlxbf_smbus_poll(priv->slv_cause->io, addr, mask, false, timeout))
		return true;

	return false;
}

/* Send byte to 'external' smbus master. */
static int mlxbf_smbus_irq_send(struct mlxbf_i2c_priv *priv, u8 recv_bytes)
{
	u8 data_desc[MLXBF_I2C_SLAVE_DATA_DESC_SIZE] = { 0 };
	u8 write_size, pec_en, addr, byte, value, byte_cnt, desc_size;
	struct i2c_client *slave = priv->slave;
	u32 control32, data32;
	int ret;

	if (!slave)
		return -EINVAL;

	addr = 0;
	byte = 0;
	desc_size = MLXBF_I2C_SLAVE_DATA_DESC_SIZE;

	/*
	 * Read bytes received from the external master. These bytes should
	 * be located in the first data descriptor register of the slave GW.
	 * These bytes are the slave address byte and the internal register
	 * address, if supplied.
	 */
	if (recv_bytes > 0) {
		data32 = ioread32be(priv->smbus->io +
					MLXBF_I2C_SLAVE_DATA_DESC_ADDR);

		/* Parse the received bytes. */
		switch (recv_bytes) {
		case 2:
			byte = (data32 >> 8) & GENMASK(7, 0);
			fallthrough;
		case 1:
			addr = (data32 & GENMASK(7, 0)) >> 1;
		}

		/* Check whether it's our slave address. */
		if (slave->addr != addr)
			return -EINVAL;
	}

	/*
	 * I2C read transactions may start by a WRITE followed by a READ.
	 * Indeed, most slave devices would expect the internal address
	 * following the slave address byte. So, write that byte first,
	 * and then, send the requested data bytes to the master.
	 */
	if (recv_bytes > 1) {
		i2c_slave_event(slave, I2C_SLAVE_WRITE_REQUESTED, &value);
		value = byte;
		ret = i2c_slave_event(slave, I2C_SLAVE_WRITE_RECEIVED,
				      &value);
		i2c_slave_event(slave, I2C_SLAVE_STOP, &value);

		if (ret < 0)
			return ret;
	}

	/*
	 * Now, send data to the master; currently, the driver supports
	 * READ_BYTE, READ_WORD and BLOCK READ protocols. Note that the
	 * hardware can send up to 128 bytes per transfer. That is the
	 * size of its data registers.
	 */
	i2c_slave_event(slave, I2C_SLAVE_READ_REQUESTED, &value);

	for (byte_cnt = 0; byte_cnt < desc_size; byte_cnt++) {
		data_desc[byte_cnt] = value;
		i2c_slave_event(slave, I2C_SLAVE_READ_PROCESSED, &value);
	}

	/* Send a stop condition to the backend. */
	i2c_slave_event(slave, I2C_SLAVE_STOP, &value);

	/* Handle the actual transfer. */

	/* Set the number of bytes to write to master. */
	write_size = (byte_cnt - 1) & 0x7f;

	/* Write data to Slave GW data descriptor. */
	mlxbf_i2c_smbus_write_data(priv, data_desc, byte_cnt,
				   MLXBF_I2C_SLAVE_DATA_DESC_ADDR);

	pec_en = 0; /* Disable PEC since it is not supported. */

	/* Prepare control word. */
	control32 = MLXBF_I2C_SLAVE_ENABLE;
	control32 |= rol32(write_size, MLXBF_I2C_SLAVE_WRITE_BYTES_SHIFT);
	control32 |= rol32(pec_en, MLXBF_I2C_SLAVE_SEND_PEC_SHIFT);

	writel(control32, priv->smbus->io + MLXBF_I2C_SMBUS_SLAVE_GW);

	/*
	 * Wait until the transfer is completed; the driver will wait
	 * until the GW is idle, a cause will rise on fall of GW busy.
	 */
	mlxbf_smbus_slave_wait_for_idle(priv, MLXBF_I2C_SMBUS_TIMEOUT);

	/* Release the Slave GW. */
	writel(0x0, priv->smbus->io + MLXBF_I2C_SMBUS_SLAVE_RS_MASTER_BYTES);
	writel(0x0, priv->smbus->io + MLXBF_I2C_SMBUS_SLAVE_PEC);
	writel(0x1, priv->smbus->io + MLXBF_I2C_SMBUS_SLAVE_READY);

	return 0;
}

/* Receive bytes from 'external' smbus master. */
static int mlxbf_smbus_irq_recv(struct mlxbf_i2c_priv *priv, u8 recv_bytes)
{
	u8 data_desc[MLXBF_I2C_SLAVE_DATA_DESC_SIZE] = { 0 };
	struct i2c_client *slave = priv->slave;
	u8 value, byte, addr;
	int ret = 0;

	if (!slave)
		return -EINVAL;

	/* Read data from Slave GW data descriptor. */
	mlxbf_i2c_smbus_read_data(priv, data_desc, recv_bytes,
				  MLXBF_I2C_SLAVE_DATA_DESC_ADDR);

	/* Check whether its our slave address. */
	addr = data_desc[0] >> 1;
	if (slave->addr != addr)
		return -EINVAL;

	/*
	 * Notify the slave backend; another I2C master wants to write data
	 * to us. This event is sent once the slave address and the write bit
	 * is detected.
	 */
	i2c_slave_event(slave, I2C_SLAVE_WRITE_REQUESTED, &value);

	/* Send the received data to the slave backend. */
	for (byte = 1; byte < recv_bytes; byte++) {
		value = data_desc[byte];
		ret = i2c_slave_event(slave, I2C_SLAVE_WRITE_RECEIVED,
				      &value);
		if (ret < 0)
			break;
	}

	/* Send a stop condition to the backend. */
	i2c_slave_event(slave, I2C_SLAVE_STOP, &value);

	/* Release the Slave GW. */
	writel(0x0, priv->smbus->io + MLXBF_I2C_SMBUS_SLAVE_RS_MASTER_BYTES);
	writel(0x0, priv->smbus->io + MLXBF_I2C_SMBUS_SLAVE_PEC);
	writel(0x1, priv->smbus->io + MLXBF_I2C_SMBUS_SLAVE_READY);

	return ret;
}

static irqreturn_t mlxbf_smbus_irq(int irq, void *ptr)
{
	struct mlxbf_i2c_priv *priv = ptr;
	bool read, write, irq_is_set;
	u32 rw_bytes_reg;
	u8 recv_bytes;

	/*
	 * Read TYU interrupt register and determine the source of the
	 * interrupt. Based on the source of the interrupt one of the
	 * following actions are performed:
	 *  - Receive data and send response to master.
	 *  - Send data and release slave GW.
	 *
	 * Handle read/write transaction only. CRmaster and Iarp requests
	 * are ignored for now.
	 */
	irq_is_set = mlxbf_i2c_has_coalesce(priv, &read, &write);
	if (!irq_is_set || (!read && !write)) {
		/* Nothing to do here, interrupt was not from this device. */
		return IRQ_NONE;
	}

	/*
	 * The MLXBF_I2C_SMBUS_SLAVE_RS_MASTER_BYTES includes the number of
	 * bytes from/to master. These are defined by 8-bits each. If the lower
	 * 8 bits are set, then the master expect to read N bytes from the
	 * slave, if the higher 8 bits are sent then the slave expect N bytes
	 * from the master.
	 */
	rw_bytes_reg = readl(priv->smbus->io +
				MLXBF_I2C_SMBUS_SLAVE_RS_MASTER_BYTES);
	recv_bytes = (rw_bytes_reg >> 8) & GENMASK(7, 0);

	/*
	 * For now, the slave supports 128 bytes transfer. Discard remaining
	 * data bytes if the master wrote more than
	 * MLXBF_I2C_SLAVE_DATA_DESC_SIZE, i.e, the actual size of the slave
	 * data descriptor.
	 *
	 * Note that we will never expect to transfer more than 128 bytes; as
	 * specified in the SMBus standard, block transactions cannot exceed
	 * 32 bytes.
	 */
	recv_bytes = recv_bytes > MLXBF_I2C_SLAVE_DATA_DESC_SIZE ?
		MLXBF_I2C_SLAVE_DATA_DESC_SIZE : recv_bytes;

	if (read)
		mlxbf_smbus_irq_send(priv, recv_bytes);
	else
		mlxbf_smbus_irq_recv(priv, recv_bytes);

	return IRQ_HANDLED;
}

/* Return negative errno on error. */
static s32 mlxbf_i2c_smbus_xfer(struct i2c_adapter *adap, u16 addr,
				unsigned short flags, char read_write,
				u8 command, int size,
				union i2c_smbus_data *data)
{
	struct mlxbf_i2c_smbus_request request = { 0 };
	struct mlxbf_i2c_priv *priv;
	bool read, pec;
	u8 byte_cnt;

	request.slave = addr;

	read = (read_write == I2C_SMBUS_READ);
	pec = flags & I2C_FUNC_SMBUS_PEC;

	switch (size) {
	case I2C_SMBUS_QUICK:
		mlxbf_i2c_smbus_quick_command(&request, read);
		dev_dbg(&adap->dev, "smbus quick, slave 0x%02x\n", addr);
		break;

	case I2C_SMBUS_BYTE:
		mlxbf_i2c_smbus_byte_func(&request,
					  read ? &data->byte : &command, read,
					  pec);
		dev_dbg(&adap->dev, "smbus %s byte, slave 0x%02x.\n",
			read ? "read" : "write", addr);
		break;

	case I2C_SMBUS_BYTE_DATA:
		mlxbf_i2c_smbus_data_byte_func(&request, &command, &data->byte,
					       read, pec);
		dev_dbg(&adap->dev, "smbus %s byte data at 0x%02x, slave 0x%02x.\n",
			read ? "read" : "write", command, addr);
		break;

	case I2C_SMBUS_WORD_DATA:
		mlxbf_i2c_smbus_data_word_func(&request, &command,
					       (u8 *)&data->word, read, pec);
		dev_dbg(&adap->dev, "smbus %s word data at 0x%02x, slave 0x%02x.\n",
			read ? "read" : "write", command, addr);
		break;

	case I2C_SMBUS_I2C_BLOCK_DATA:
		byte_cnt = data->block[0];
		mlxbf_i2c_smbus_i2c_block_func(&request, &command, data->block,
					       &byte_cnt, read, pec);
		dev_dbg(&adap->dev, "i2c %s block data, %d bytes at 0x%02x, slave 0x%02x.\n",
			read ? "read" : "write", byte_cnt, command, addr);
		break;

	case I2C_SMBUS_BLOCK_DATA:
		byte_cnt = read ? I2C_SMBUS_BLOCK_MAX : data->block[0];
		mlxbf_i2c_smbus_block_func(&request, &command, data->block,
					   &byte_cnt, read, pec);
		dev_dbg(&adap->dev, "smbus %s block data, %d bytes at 0x%02x, slave 0x%02x.\n",
			read ? "read" : "write", byte_cnt, command, addr);
		break;

	case I2C_FUNC_SMBUS_PROC_CALL:
		mlxbf_i2c_smbus_process_call_func(&request, &command,
						  (u8 *)&data->word, pec);
		dev_dbg(&adap->dev, "process call, wr/rd at 0x%02x, slave 0x%02x.\n",
			command, addr);
		break;

	case I2C_FUNC_SMBUS_BLOCK_PROC_CALL:
		byte_cnt = data->block[0];
		mlxbf_i2c_smbus_blk_process_call_func(&request, &command,
						      data->block, &byte_cnt,
						      pec);
		dev_dbg(&adap->dev, "block process call, wr/rd %d bytes, slave 0x%02x.\n",
			byte_cnt, addr);
		break;

	default:
		dev_dbg(&adap->dev, "Unsupported I2C/SMBus command %d\n",
			size);
		return -EOPNOTSUPP;
	}

	priv = i2c_get_adapdata(adap);

	return mlxbf_i2c_smbus_start_transaction(priv, &request);
}

static int mlxbf_i2c_reg_slave(struct i2c_client *slave)
{
	struct mlxbf_i2c_priv *priv = i2c_get_adapdata(slave->adapter);
	int ret;

	if (priv->slave)
		return -EBUSY;

	/*
	 * Do not support ten bit chip address and do not use Packet Error
	 * Checking (PEC).
	 */
	if (slave->flags & (I2C_CLIENT_TEN | I2C_CLIENT_PEC))
		return -EAFNOSUPPORT;

	ret = mlxbf_slave_enable(priv, slave->addr);
	if (ret < 0)
		return ret;

	priv->slave = slave;

	return 0;
}

static int mlxbf_i2c_unreg_slave(struct i2c_client *slave)
{
	struct mlxbf_i2c_priv *priv = i2c_get_adapdata(slave->adapter);
	int ret;

	WARN_ON(!priv->slave);

	/* Unregister slave, i.e. disable the slave address in hardware. */
	ret = mlxbf_slave_disable(priv);
	if (ret < 0)
		return ret;

	priv->slave = NULL;

	return 0;
}

static u32 mlxbf_i2c_functionality(struct i2c_adapter *adap)
{
	return MLXBF_I2C_FUNC_ALL;
}

static struct mlxbf_i2c_chip_info mlxbf_i2c_chip[] = {
	[MLXBF_I2C_CHIP_TYPE_1] = {
		.type = MLXBF_I2C_CHIP_TYPE_1,
		.shared_res = {
			[0] = &mlxbf_i2c_coalesce_res[MLXBF_I2C_CHIP_TYPE_1],
			[1] = &mlxbf_i2c_corepll_res[MLXBF_I2C_CHIP_TYPE_1],
			[2] = &mlxbf_i2c_gpio_res[MLXBF_I2C_CHIP_TYPE_1]
		},
		.calculate_freq = mlxbf_calculate_freq_from_tyu
	},
	[MLXBF_I2C_CHIP_TYPE_2] = {
		.type = MLXBF_I2C_CHIP_TYPE_2,
		.shared_res = {
			[0] = &mlxbf_i2c_corepll_res[MLXBF_I2C_CHIP_TYPE_2]
		},
		.calculate_freq = mlxbf_calculate_freq_from_yu
	}
};

static const struct i2c_algorithm mlxbf_i2c_algo = {
	.smbus_xfer = mlxbf_i2c_smbus_xfer,
	.functionality = mlxbf_i2c_functionality,
	.reg_slave = mlxbf_i2c_reg_slave,
	.unreg_slave = mlxbf_i2c_unreg_slave,
};

static struct i2c_adapter_quirks mlxbf_i2c_quirks = {
	.max_read_len = MLXBF_I2C_MASTER_DATA_R_LENGTH,
	.max_write_len = MLXBF_I2C_MASTER_DATA_W_LENGTH,
};

static const struct of_device_id mlxbf_i2c_dt_ids[] = {
	{
		.compatible = "mellanox,i2c-mlxbf1",
		.data = &mlxbf_i2c_chip[MLXBF_I2C_CHIP_TYPE_1]
	},
	{
		.compatible = "mellanox,i2c-mlxbf2",
		.data = &mlxbf_i2c_chip[MLXBF_I2C_CHIP_TYPE_2]
	},
	{},
};

MODULE_DEVICE_TABLE(of, mlxbf_i2c_dt_ids);

#ifdef CONFIG_ACPI
static const struct acpi_device_id mlxbf_i2c_acpi_ids[] = {
	{ "MLNXBF03", (kernel_ulong_t)&mlxbf_i2c_chip[MLXBF_I2C_CHIP_TYPE_1] },
	{ "MLNXBF23", (kernel_ulong_t)&mlxbf_i2c_chip[MLXBF_I2C_CHIP_TYPE_2] },
	{},
};

MODULE_DEVICE_TABLE(acpi, mlxbf_i2c_acpi_ids);

static int mlxbf_i2c_acpi_probe(struct device *dev, struct mlxbf_i2c_priv *priv)
{
	const struct acpi_device_id *aid;
	struct acpi_device *adev;
	unsigned long bus_id = 0;
	const char *uid;
	int ret;

	if (acpi_disabled)
		return -ENOENT;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENXIO;

	aid = acpi_match_device(mlxbf_i2c_acpi_ids, dev);
	if (!aid)
		return -ENODEV;

	priv->chip = (struct mlxbf_i2c_chip_info *)aid->driver_data;

	uid = acpi_device_uid(adev);
	if (!uid || !(*uid)) {
		dev_err(dev, "Cannot retrieve UID\n");
		return -ENODEV;
	}

	ret = kstrtoul(uid, 0, &bus_id);
	if (!ret)
		priv->bus = bus_id;

	return ret;
}
#else
static int mlxbf_i2c_acpi_probe(struct device *dev, struct mlxbf_i2c_priv *priv)
{
	return -ENOENT;
}
#endif /* CONFIG_ACPI */

static int mlxbf_i2c_of_probe(struct device *dev, struct mlxbf_i2c_priv *priv)
{
	const struct of_device_id *oid;
	int bus_id = -1;

	if (IS_ENABLED(CONFIG_OF) && dev->of_node) {
		oid = of_match_node(mlxbf_i2c_dt_ids, dev->of_node);
		if (!oid)
			return -ENODEV;

		priv->chip = oid->data;

		bus_id = of_alias_get_id(dev->of_node, "i2c");
		if (bus_id >= 0)
			priv->bus = bus_id;
	}

	if (bus_id < 0) {
		dev_err(dev, "Cannot get bus id");
		return bus_id;
	}

	return 0;
}

static int mlxbf_i2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mlxbf_i2c_priv *priv;
	struct i2c_adapter *adap;
	int irq, ret;

	priv = devm_kzalloc(dev, sizeof(struct mlxbf_i2c_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = mlxbf_i2c_acpi_probe(dev, priv);
	if (ret < 0 && ret != -ENOENT && ret != -ENXIO)
		ret = mlxbf_i2c_of_probe(dev, priv);

	if (ret < 0)
		return ret;

	ret = mlxbf_i2c_init_resource(pdev, &priv->smbus,
				      MLXBF_I2C_SMBUS_RES);
	if (ret < 0) {
		dev_err(dev, "Cannot fetch smbus resource info");
		return ret;
	}

	ret = mlxbf_i2c_init_resource(pdev, &priv->mst_cause,
				      MLXBF_I2C_MST_CAUSE_RES);
	if (ret < 0) {
		dev_err(dev, "Cannot fetch cause master resource info");
		return ret;
	}

	ret = mlxbf_i2c_init_resource(pdev, &priv->slv_cause,
				      MLXBF_I2C_SLV_CAUSE_RES);
	if (ret < 0) {
		dev_err(dev, "Cannot fetch cause slave resource info");
		return ret;
	}

	adap = &priv->adap;
	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_HWMON;
	adap->algo = &mlxbf_i2c_algo;
	adap->quirks = &mlxbf_i2c_quirks;
	adap->dev.parent = dev;
	adap->dev.of_node = dev->of_node;
	adap->nr = priv->bus;

	snprintf(adap->name, sizeof(adap->name), "i2c%d", adap->nr);
	i2c_set_adapdata(adap, priv);

	/* Read Core PLL frequency. */
	ret = mlxbf_i2c_calculate_corepll_freq(pdev, priv);
	if (ret < 0) {
		dev_err(dev, "cannot get core clock frequency\n");
		/* Set to default value. */
		priv->frequency = MLXBF_I2C_COREPLL_FREQ;
	}

	/*
	 * Initialize master.
	 * Note that a physical bus might be shared among Linux and firmware
	 * (e.g., ATF). Thus, the bus should be initialized and ready and
	 * bus initialization would be unnecessary. This requires additional
	 * knowledge about physical busses. But, since an extra initialization
	 * does not really hurt, then keep the code as is.
	 */
	ret = mlxbf_i2c_init_master(pdev, priv);
	if (ret < 0) {
		dev_err(dev, "failed to initialize smbus master %d",
			priv->bus);
		return ret;
	}

	mlxbf_i2c_init_timings(pdev, priv);

	mlxbf_i2c_init_slave(pdev, priv);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	ret = devm_request_irq(dev, irq, mlxbf_smbus_irq,
			       IRQF_ONESHOT | IRQF_SHARED | IRQF_PROBE_SHARED,
			       dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "Cannot get irq %d\n", irq);
		return ret;
	}

	priv->irq = irq;

	platform_set_drvdata(pdev, priv);

	ret = i2c_add_numbered_adapter(adap);
	if (ret < 0)
		return ret;

	mutex_lock(&mlxbf_i2c_bus_lock);
	mlxbf_i2c_bus_count++;
	mutex_unlock(&mlxbf_i2c_bus_lock);

	return 0;
}

static int mlxbf_i2c_remove(struct platform_device *pdev)
{
	struct mlxbf_i2c_priv *priv = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct resource *params;

	params = priv->smbus->params;
	devm_release_mem_region(dev, params->start, resource_size(params));

	params = priv->mst_cause->params;
	devm_release_mem_region(dev, params->start, resource_size(params));

	params = priv->slv_cause->params;
	devm_release_mem_region(dev, params->start, resource_size(params));

	/*
	 * Release shared resources. This should be done when releasing
	 * the I2C controller.
	 */
	mutex_lock(&mlxbf_i2c_bus_lock);
	if (--mlxbf_i2c_bus_count == 0) {
		mlxbf_i2c_release_coalesce(pdev, priv);
		mlxbf_i2c_release_corepll(pdev, priv);
		mlxbf_i2c_release_gpio(pdev, priv);
	}
	mutex_unlock(&mlxbf_i2c_bus_lock);

	devm_free_irq(dev, priv->irq, priv);

	i2c_del_adapter(&priv->adap);

	return 0;
}

static struct platform_driver mlxbf_i2c_driver = {
	.probe = mlxbf_i2c_probe,
	.remove = mlxbf_i2c_remove,
	.driver = {
		.name = "i2c-mlxbf",
		.of_match_table = mlxbf_i2c_dt_ids,
#ifdef CONFIG_ACPI
		.acpi_match_table = ACPI_PTR(mlxbf_i2c_acpi_ids),
#endif /* CONFIG_ACPI  */
	},
};

static int __init mlxbf_i2c_init(void)
{
	mutex_init(&mlxbf_i2c_coalesce_lock);
	mutex_init(&mlxbf_i2c_corepll_lock);
	mutex_init(&mlxbf_i2c_gpio_lock);

	mutex_init(&mlxbf_i2c_bus_lock);

	return platform_driver_register(&mlxbf_i2c_driver);
}
module_init(mlxbf_i2c_init);

static void __exit mlxbf_i2c_exit(void)
{
	platform_driver_unregister(&mlxbf_i2c_driver);

	mutex_destroy(&mlxbf_i2c_bus_lock);

	mutex_destroy(&mlxbf_i2c_gpio_lock);
	mutex_destroy(&mlxbf_i2c_corepll_lock);
	mutex_destroy(&mlxbf_i2c_coalesce_lock);
}
module_exit(mlxbf_i2c_exit);

MODULE_DESCRIPTION("Mellanox BlueField I2C bus driver");
MODULE_AUTHOR("Khalil Blaiech <kblaiech@nvidia.com>");
MODULE_LICENSE("GPL v2");
