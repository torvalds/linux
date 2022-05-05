// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NPCM7xx I2C Controller driver
 *
 * Copyright (C) 2020 Nuvoton Technologies tali.perry@nuvoton.com
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

enum i2c_mode {
	I2C_MASTER,
	I2C_SLAVE,
};

/*
 * External I2C Interface driver xfer indication values, which indicate status
 * of the bus.
 */
enum i2c_state_ind {
	I2C_NO_STATUS_IND = 0,
	I2C_SLAVE_RCV_IND,
	I2C_SLAVE_XMIT_IND,
	I2C_SLAVE_XMIT_MISSING_DATA_IND,
	I2C_SLAVE_RESTART_IND,
	I2C_SLAVE_DONE_IND,
	I2C_MASTER_DONE_IND,
	I2C_NACK_IND,
	I2C_BUS_ERR_IND,
	I2C_WAKE_UP_IND,
	I2C_BLOCK_BYTES_ERR_IND,
	I2C_SLAVE_RCV_MISSING_DATA_IND,
};

/*
 * Operation type values (used to define the operation currently running)
 * module is interrupt driven, on each interrupt the current operation is
 * checked to see if the module is currently reading or writing.
 */
enum i2c_oper {
	I2C_NO_OPER = 0,
	I2C_WRITE_OPER,
	I2C_READ_OPER,
};

/* I2C Bank (module had 2 banks of registers) */
enum i2c_bank {
	I2C_BANK_0 = 0,
	I2C_BANK_1,
};

/* Internal I2C states values (for the I2C module state machine). */
enum i2c_state {
	I2C_DISABLE = 0,
	I2C_IDLE,
	I2C_MASTER_START,
	I2C_SLAVE_MATCH,
	I2C_OPER_STARTED,
	I2C_STOP_PENDING,
};

#if IS_ENABLED(CONFIG_I2C_SLAVE)
/* Module supports setting multiple own slave addresses */
enum i2c_addr {
	I2C_SLAVE_ADDR1 = 0,
	I2C_SLAVE_ADDR2,
	I2C_SLAVE_ADDR3,
	I2C_SLAVE_ADDR4,
	I2C_SLAVE_ADDR5,
	I2C_SLAVE_ADDR6,
	I2C_SLAVE_ADDR7,
	I2C_SLAVE_ADDR8,
	I2C_SLAVE_ADDR9,
	I2C_SLAVE_ADDR10,
	I2C_GC_ADDR,
	I2C_ARP_ADDR,
};
#endif

/* init register and default value required to enable module */
#define NPCM_I2CSEGCTL			0xE4
#define NPCM_I2CSEGCTL_INIT_VAL		0x0333F000

/* Common regs */
#define NPCM_I2CSDA			0x00
#define NPCM_I2CST			0x02
#define NPCM_I2CCST			0x04
#define NPCM_I2CCTL1			0x06
#define NPCM_I2CADDR1			0x08
#define NPCM_I2CCTL2			0x0A
#define NPCM_I2CADDR2			0x0C
#define NPCM_I2CCTL3			0x0E
#define NPCM_I2CCST2			0x18
#define NPCM_I2CCST3			0x19
#define I2C_VER				0x1F

/*BANK0 regs*/
#define NPCM_I2CADDR3			0x10
#define NPCM_I2CADDR7			0x11
#define NPCM_I2CADDR4			0x12
#define NPCM_I2CADDR8			0x13
#define NPCM_I2CADDR5			0x14
#define NPCM_I2CADDR9			0x15
#define NPCM_I2CADDR6			0x16
#define NPCM_I2CADDR10			0x17

#if IS_ENABLED(CONFIG_I2C_SLAVE)
/*
 * npcm_i2caddr array:
 * The module supports having multiple own slave addresses.
 * Since the addr regs are sprinkled all over the address space,
 * use this array to get the address or each register.
 */
#define I2C_NUM_OWN_ADDR 10
static const int npcm_i2caddr[I2C_NUM_OWN_ADDR] = {
	NPCM_I2CADDR1, NPCM_I2CADDR2, NPCM_I2CADDR3, NPCM_I2CADDR4,
	NPCM_I2CADDR5, NPCM_I2CADDR6, NPCM_I2CADDR7, NPCM_I2CADDR8,
	NPCM_I2CADDR9, NPCM_I2CADDR10,
};
#endif

#define NPCM_I2CCTL4			0x1A
#define NPCM_I2CCTL5			0x1B
#define NPCM_I2CSCLLT			0x1C /* SCL Low Time */
#define NPCM_I2CFIF_CTL			0x1D /* FIFO Control */
#define NPCM_I2CSCLHT			0x1E /* SCL High Time */

/* BANK 1 regs */
#define NPCM_I2CFIF_CTS			0x10 /* Both FIFOs Control and Status */
#define NPCM_I2CTXF_CTL			0x12 /* Tx-FIFO Control */
#define NPCM_I2CT_OUT			0x14 /* Bus T.O. */
#define NPCM_I2CPEC			0x16 /* PEC Data */
#define NPCM_I2CTXF_STS			0x1A /* Tx-FIFO Status */
#define NPCM_I2CRXF_STS			0x1C /* Rx-FIFO Status */
#define NPCM_I2CRXF_CTL			0x1E /* Rx-FIFO Control */

/* NPCM_I2CST reg fields */
#define NPCM_I2CST_XMIT			BIT(0)
#define NPCM_I2CST_MASTER		BIT(1)
#define NPCM_I2CST_NMATCH		BIT(2)
#define NPCM_I2CST_STASTR		BIT(3)
#define NPCM_I2CST_NEGACK		BIT(4)
#define NPCM_I2CST_BER			BIT(5)
#define NPCM_I2CST_SDAST		BIT(6)
#define NPCM_I2CST_SLVSTP		BIT(7)

/* NPCM_I2CCST reg fields */
#define NPCM_I2CCST_BUSY		BIT(0)
#define NPCM_I2CCST_BB			BIT(1)
#define NPCM_I2CCST_MATCH		BIT(2)
#define NPCM_I2CCST_GCMATCH		BIT(3)
#define NPCM_I2CCST_TSDA		BIT(4)
#define NPCM_I2CCST_TGSCL		BIT(5)
#define NPCM_I2CCST_MATCHAF		BIT(6)
#define NPCM_I2CCST_ARPMATCH		BIT(7)

/* NPCM_I2CCTL1 reg fields */
#define NPCM_I2CCTL1_START		BIT(0)
#define NPCM_I2CCTL1_STOP		BIT(1)
#define NPCM_I2CCTL1_INTEN		BIT(2)
#define NPCM_I2CCTL1_EOBINTE		BIT(3)
#define NPCM_I2CCTL1_ACK		BIT(4)
#define NPCM_I2CCTL1_GCMEN		BIT(5)
#define NPCM_I2CCTL1_NMINTE		BIT(6)
#define NPCM_I2CCTL1_STASTRE		BIT(7)

/* RW1S fields (inside a RW reg): */
#define NPCM_I2CCTL1_RWS   \
	(NPCM_I2CCTL1_START | NPCM_I2CCTL1_STOP | NPCM_I2CCTL1_ACK)

/* npcm_i2caddr reg fields */
#define NPCM_I2CADDR_A			GENMASK(6, 0)
#define NPCM_I2CADDR_SAEN		BIT(7)

/* NPCM_I2CCTL2 reg fields */
#define I2CCTL2_ENABLE			BIT(0)
#define I2CCTL2_SCLFRQ6_0		GENMASK(7, 1)

/* NPCM_I2CCTL3 reg fields */
#define I2CCTL3_SCLFRQ8_7		GENMASK(1, 0)
#define I2CCTL3_ARPMEN			BIT(2)
#define I2CCTL3_IDL_START		BIT(3)
#define I2CCTL3_400K_MODE		BIT(4)
#define I2CCTL3_BNK_SEL			BIT(5)
#define I2CCTL3_SDA_LVL			BIT(6)
#define I2CCTL3_SCL_LVL			BIT(7)

/* NPCM_I2CCST2 reg fields */
#define NPCM_I2CCST2_MATCHA1F		BIT(0)
#define NPCM_I2CCST2_MATCHA2F		BIT(1)
#define NPCM_I2CCST2_MATCHA3F		BIT(2)
#define NPCM_I2CCST2_MATCHA4F		BIT(3)
#define NPCM_I2CCST2_MATCHA5F		BIT(4)
#define NPCM_I2CCST2_MATCHA6F		BIT(5)
#define NPCM_I2CCST2_MATCHA7F		BIT(5)
#define NPCM_I2CCST2_INTSTS		BIT(7)

/* NPCM_I2CCST3 reg fields */
#define NPCM_I2CCST3_MATCHA8F		BIT(0)
#define NPCM_I2CCST3_MATCHA9F		BIT(1)
#define NPCM_I2CCST3_MATCHA10F		BIT(2)
#define NPCM_I2CCST3_EO_BUSY		BIT(7)

/* NPCM_I2CCTL4 reg fields */
#define I2CCTL4_HLDT			GENMASK(5, 0)
#define I2CCTL4_LVL_WE			BIT(7)

/* NPCM_I2CCTL5 reg fields */
#define I2CCTL5_DBNCT			GENMASK(3, 0)

/* NPCM_I2CFIF_CTS reg fields */
#define NPCM_I2CFIF_CTS_RXF_TXE		BIT(1)
#define NPCM_I2CFIF_CTS_RFTE_IE		BIT(3)
#define NPCM_I2CFIF_CTS_CLR_FIFO	BIT(6)
#define NPCM_I2CFIF_CTS_SLVRSTR		BIT(7)

/* NPCM_I2CTXF_CTL reg fields */
#define NPCM_I2CTXF_CTL_TX_THR		GENMASK(4, 0)
#define NPCM_I2CTXF_CTL_THR_TXIE	BIT(6)

/* NPCM_I2CT_OUT reg fields */
#define NPCM_I2CT_OUT_TO_CKDIV		GENMASK(5, 0)
#define NPCM_I2CT_OUT_T_OUTIE		BIT(6)
#define NPCM_I2CT_OUT_T_OUTST		BIT(7)

/* NPCM_I2CTXF_STS reg fields */
#define NPCM_I2CTXF_STS_TX_BYTES	GENMASK(4, 0)
#define NPCM_I2CTXF_STS_TX_THST		BIT(6)

/* NPCM_I2CRXF_STS reg fields */
#define NPCM_I2CRXF_STS_RX_BYTES	GENMASK(4, 0)
#define NPCM_I2CRXF_STS_RX_THST		BIT(6)

/* NPCM_I2CFIF_CTL reg fields */
#define NPCM_I2CFIF_CTL_FIFO_EN		BIT(4)

/* NPCM_I2CRXF_CTL reg fields */
#define NPCM_I2CRXF_CTL_RX_THR		GENMASK(4, 0)
#define NPCM_I2CRXF_CTL_LAST_PEC	BIT(5)
#define NPCM_I2CRXF_CTL_THR_RXIE	BIT(6)

#define I2C_HW_FIFO_SIZE		16

/* I2C_VER reg fields */
#define I2C_VER_VERSION			GENMASK(6, 0)
#define I2C_VER_FIFO_EN			BIT(7)

/* stall/stuck timeout in us */
#define DEFAULT_STALL_COUNT		25

/* SCLFRQ field position */
#define SCLFRQ_0_TO_6			GENMASK(6, 0)
#define SCLFRQ_7_TO_8			GENMASK(8, 7)

/* supported clk settings. values in Hz. */
#define I2C_FREQ_MIN_HZ			10000
#define I2C_FREQ_MAX_HZ			I2C_MAX_FAST_MODE_PLUS_FREQ

/* Status of one I2C module */
struct npcm_i2c {
	struct i2c_adapter adap;
	struct device *dev;
	unsigned char __iomem *reg;
	spinlock_t lock;   /* IRQ synchronization */
	struct completion cmd_complete;
	int cmd_err;
	struct i2c_msg *msgs;
	int msgs_num;
	int num;
	u32 apb_clk;
	struct i2c_bus_recovery_info rinfo;
	enum i2c_state state;
	enum i2c_oper operation;
	enum i2c_mode master_or_slave;
	enum i2c_state_ind stop_ind;
	u8 dest_addr;
	u8 *rd_buf;
	u16 rd_size;
	u16 rd_ind;
	u8 *wr_buf;
	u16 wr_size;
	u16 wr_ind;
	bool fifo_use;
	u16 PEC_mask; /* PEC bit mask per slave address */
	bool PEC_use;
	bool read_block_use;
	unsigned long int_time_stamp;
	unsigned long bus_freq; /* in Hz */
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	u8 own_slave_addr;
	struct i2c_client *slave;
	int slv_rd_size;
	int slv_rd_ind;
	int slv_wr_size;
	int slv_wr_ind;
	u8 slv_rd_buf[I2C_HW_FIFO_SIZE];
	u8 slv_wr_buf[I2C_HW_FIFO_SIZE];
#endif
	struct dentry *debugfs; /* debugfs device directory */
	u64 ber_cnt;
	u64 rec_succ_cnt;
	u64 rec_fail_cnt;
	u64 nack_cnt;
	u64 timeout_cnt;
};

static inline void npcm_i2c_select_bank(struct npcm_i2c *bus,
					enum i2c_bank bank)
{
	u8 i2cctl3 = ioread8(bus->reg + NPCM_I2CCTL3);

	if (bank == I2C_BANK_0)
		i2cctl3 = i2cctl3 & ~I2CCTL3_BNK_SEL;
	else
		i2cctl3 = i2cctl3 | I2CCTL3_BNK_SEL;
	iowrite8(i2cctl3, bus->reg + NPCM_I2CCTL3);
}

static void npcm_i2c_init_params(struct npcm_i2c *bus)
{
	bus->stop_ind = I2C_NO_STATUS_IND;
	bus->rd_size = 0;
	bus->wr_size = 0;
	bus->rd_ind = 0;
	bus->wr_ind = 0;
	bus->read_block_use = false;
	bus->int_time_stamp = 0;
	bus->PEC_use = false;
	bus->PEC_mask = 0;
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	if (bus->slave)
		bus->master_or_slave = I2C_SLAVE;
#endif
}

static inline void npcm_i2c_wr_byte(struct npcm_i2c *bus, u8 data)
{
	iowrite8(data, bus->reg + NPCM_I2CSDA);
}

static inline u8 npcm_i2c_rd_byte(struct npcm_i2c *bus)
{
	return ioread8(bus->reg + NPCM_I2CSDA);
}

static int npcm_i2c_get_SCL(struct i2c_adapter *_adap)
{
	struct npcm_i2c *bus = container_of(_adap, struct npcm_i2c, adap);

	return !!(I2CCTL3_SCL_LVL & ioread32(bus->reg + NPCM_I2CCTL3));
}

static int npcm_i2c_get_SDA(struct i2c_adapter *_adap)
{
	struct npcm_i2c *bus = container_of(_adap, struct npcm_i2c, adap);

	return !!(I2CCTL3_SDA_LVL & ioread32(bus->reg + NPCM_I2CCTL3));
}

static inline u16 npcm_i2c_get_index(struct npcm_i2c *bus)
{
	if (bus->operation == I2C_READ_OPER)
		return bus->rd_ind;
	if (bus->operation == I2C_WRITE_OPER)
		return bus->wr_ind;
	return 0;
}

/* quick protocol (just address) */
static inline bool npcm_i2c_is_quick(struct npcm_i2c *bus)
{
	return bus->wr_size == 0 && bus->rd_size == 0;
}

static void npcm_i2c_disable(struct npcm_i2c *bus)
{
	u8 i2cctl2;

#if IS_ENABLED(CONFIG_I2C_SLAVE)
	int i;

	/* select bank 0 for I2C addresses */
	npcm_i2c_select_bank(bus, I2C_BANK_0);

	/* Slave addresses removal */
	for (i = I2C_SLAVE_ADDR1; i < I2C_NUM_OWN_ADDR; i++)
		iowrite8(0, bus->reg + npcm_i2caddr[i]);

	npcm_i2c_select_bank(bus, I2C_BANK_1);
#endif
	/* Disable module */
	i2cctl2 = ioread8(bus->reg + NPCM_I2CCTL2);
	i2cctl2 = i2cctl2 & ~I2CCTL2_ENABLE;
	iowrite8(i2cctl2, bus->reg + NPCM_I2CCTL2);

	bus->state = I2C_DISABLE;
}

static void npcm_i2c_enable(struct npcm_i2c *bus)
{
	u8 i2cctl2 = ioread8(bus->reg + NPCM_I2CCTL2);

	i2cctl2 = i2cctl2 | I2CCTL2_ENABLE;
	iowrite8(i2cctl2, bus->reg + NPCM_I2CCTL2);
	bus->state = I2C_IDLE;
}

/* enable\disable end of busy (EOB) interrupts */
static inline void npcm_i2c_eob_int(struct npcm_i2c *bus, bool enable)
{
	u8 val;

	/* Clear EO_BUSY pending bit: */
	val = ioread8(bus->reg + NPCM_I2CCST3);
	val = val | NPCM_I2CCST3_EO_BUSY;
	iowrite8(val, bus->reg + NPCM_I2CCST3);

	val = ioread8(bus->reg + NPCM_I2CCTL1);
	val &= ~NPCM_I2CCTL1_RWS;
	if (enable)
		val |= NPCM_I2CCTL1_EOBINTE;
	else
		val &= ~NPCM_I2CCTL1_EOBINTE;
	iowrite8(val, bus->reg + NPCM_I2CCTL1);
}

static inline bool npcm_i2c_tx_fifo_empty(struct npcm_i2c *bus)
{
	u8 tx_fifo_sts;

	tx_fifo_sts = ioread8(bus->reg + NPCM_I2CTXF_STS);
	/* check if TX FIFO is not empty */
	if ((tx_fifo_sts & NPCM_I2CTXF_STS_TX_BYTES) == 0)
		return false;

	/* check if TX FIFO status bit is set: */
	return !!FIELD_GET(NPCM_I2CTXF_STS_TX_THST, tx_fifo_sts);
}

static inline bool npcm_i2c_rx_fifo_full(struct npcm_i2c *bus)
{
	u8 rx_fifo_sts;

	rx_fifo_sts = ioread8(bus->reg + NPCM_I2CRXF_STS);
	/* check if RX FIFO is not empty: */
	if ((rx_fifo_sts & NPCM_I2CRXF_STS_RX_BYTES) == 0)
		return false;

	/* check if rx fifo full status is set: */
	return !!FIELD_GET(NPCM_I2CRXF_STS_RX_THST, rx_fifo_sts);
}

static inline void npcm_i2c_clear_fifo_int(struct npcm_i2c *bus)
{
	u8 val;

	val = ioread8(bus->reg + NPCM_I2CFIF_CTS);
	val = (val & NPCM_I2CFIF_CTS_SLVRSTR) | NPCM_I2CFIF_CTS_RXF_TXE;
	iowrite8(val, bus->reg + NPCM_I2CFIF_CTS);
}

static inline void npcm_i2c_clear_tx_fifo(struct npcm_i2c *bus)
{
	u8 val;

	val = ioread8(bus->reg + NPCM_I2CTXF_STS);
	val = val | NPCM_I2CTXF_STS_TX_THST;
	iowrite8(val, bus->reg + NPCM_I2CTXF_STS);
}

static inline void npcm_i2c_clear_rx_fifo(struct npcm_i2c *bus)
{
	u8 val;

	val = ioread8(bus->reg + NPCM_I2CRXF_STS);
	val = val | NPCM_I2CRXF_STS_RX_THST;
	iowrite8(val, bus->reg + NPCM_I2CRXF_STS);
}

static void npcm_i2c_int_enable(struct npcm_i2c *bus, bool enable)
{
	u8 val;

	val = ioread8(bus->reg + NPCM_I2CCTL1);
	val &= ~NPCM_I2CCTL1_RWS;
	if (enable)
		val |= NPCM_I2CCTL1_INTEN;
	else
		val &= ~NPCM_I2CCTL1_INTEN;
	iowrite8(val, bus->reg + NPCM_I2CCTL1);
}

static inline void npcm_i2c_master_start(struct npcm_i2c *bus)
{
	u8 val;

	val = ioread8(bus->reg + NPCM_I2CCTL1);
	val &= ~(NPCM_I2CCTL1_STOP | NPCM_I2CCTL1_ACK);
	val |= NPCM_I2CCTL1_START;
	iowrite8(val, bus->reg + NPCM_I2CCTL1);
}

static inline void npcm_i2c_master_stop(struct npcm_i2c *bus)
{
	u8 val;

	/*
	 * override HW issue: I2C may fail to supply stop condition in Master
	 * Write operation.
	 * Need to delay at least 5 us from the last int, before issueing a stop
	 */
	udelay(10); /* function called from interrupt, can't sleep */
	val = ioread8(bus->reg + NPCM_I2CCTL1);
	val &= ~(NPCM_I2CCTL1_START | NPCM_I2CCTL1_ACK);
	val |= NPCM_I2CCTL1_STOP;
	iowrite8(val, bus->reg + NPCM_I2CCTL1);

	if (!bus->fifo_use)
		return;

	npcm_i2c_select_bank(bus, I2C_BANK_1);

	if (bus->operation == I2C_READ_OPER)
		npcm_i2c_clear_rx_fifo(bus);
	else
		npcm_i2c_clear_tx_fifo(bus);
	npcm_i2c_clear_fifo_int(bus);
	iowrite8(0, bus->reg + NPCM_I2CTXF_CTL);
}

static inline void npcm_i2c_stall_after_start(struct npcm_i2c *bus, bool stall)
{
	u8 val;

	val = ioread8(bus->reg + NPCM_I2CCTL1);
	val &= ~NPCM_I2CCTL1_RWS;
	if (stall)
		val |= NPCM_I2CCTL1_STASTRE;
	else
		val &= ~NPCM_I2CCTL1_STASTRE;
	iowrite8(val, bus->reg + NPCM_I2CCTL1);
}

static inline void npcm_i2c_nack(struct npcm_i2c *bus)
{
	u8 val;

	val = ioread8(bus->reg + NPCM_I2CCTL1);
	val &= ~(NPCM_I2CCTL1_STOP | NPCM_I2CCTL1_START);
	val |= NPCM_I2CCTL1_ACK;
	iowrite8(val, bus->reg + NPCM_I2CCTL1);
}

#if IS_ENABLED(CONFIG_I2C_SLAVE)
static void npcm_i2c_slave_int_enable(struct npcm_i2c *bus, bool enable)
{
	u8 i2cctl1;

	/* enable interrupt on slave match: */
	i2cctl1 = ioread8(bus->reg + NPCM_I2CCTL1);
	i2cctl1 &= ~NPCM_I2CCTL1_RWS;
	if (enable)
		i2cctl1 |= NPCM_I2CCTL1_NMINTE;
	else
		i2cctl1 &= ~NPCM_I2CCTL1_NMINTE;
	iowrite8(i2cctl1, bus->reg + NPCM_I2CCTL1);
}

static int npcm_i2c_slave_enable(struct npcm_i2c *bus, enum i2c_addr addr_type,
				 u8 addr, bool enable)
{
	u8 i2cctl1;
	u8 i2cctl3;
	u8 sa_reg;

	sa_reg = (addr & 0x7F) | FIELD_PREP(NPCM_I2CADDR_SAEN, enable);
	if (addr_type == I2C_GC_ADDR) {
		i2cctl1 = ioread8(bus->reg + NPCM_I2CCTL1);
		if (enable)
			i2cctl1 |= NPCM_I2CCTL1_GCMEN;
		else
			i2cctl1 &= ~NPCM_I2CCTL1_GCMEN;
		iowrite8(i2cctl1, bus->reg + NPCM_I2CCTL1);
		return 0;
	}
	if (addr_type == I2C_ARP_ADDR) {
		i2cctl3 = ioread8(bus->reg + NPCM_I2CCTL3);
		if (enable)
			i2cctl3 |= I2CCTL3_ARPMEN;
		else
			i2cctl3 &= ~I2CCTL3_ARPMEN;
		iowrite8(i2cctl3, bus->reg + NPCM_I2CCTL3);
		return 0;
	}
	if (addr_type >= I2C_ARP_ADDR)
		return -EFAULT;
	/* select bank 0 for address 3 to 10 */
	if (addr_type > I2C_SLAVE_ADDR2)
		npcm_i2c_select_bank(bus, I2C_BANK_0);
	/* Set and enable the address */
	iowrite8(sa_reg, bus->reg + npcm_i2caddr[addr_type]);
	npcm_i2c_slave_int_enable(bus, enable);
	if (addr_type > I2C_SLAVE_ADDR2)
		npcm_i2c_select_bank(bus, I2C_BANK_1);
	return 0;
}
#endif

static void npcm_i2c_reset(struct npcm_i2c *bus)
{
	/*
	 * Save I2CCTL1 relevant bits. It is being cleared when the module
	 *  is disabled.
	 */
	u8 i2cctl1;
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	u8 addr;
#endif

	i2cctl1 = ioread8(bus->reg + NPCM_I2CCTL1);

	npcm_i2c_disable(bus);
	npcm_i2c_enable(bus);

	/* Restore NPCM_I2CCTL1 Status */
	i2cctl1 &= ~NPCM_I2CCTL1_RWS;
	iowrite8(i2cctl1, bus->reg + NPCM_I2CCTL1);

	/* Clear BB (BUS BUSY) bit */
	iowrite8(NPCM_I2CCST_BB, bus->reg + NPCM_I2CCST);
	iowrite8(0xFF, bus->reg + NPCM_I2CST);

	/* Clear EOB bit */
	iowrite8(NPCM_I2CCST3_EO_BUSY, bus->reg + NPCM_I2CCST3);

	/* Clear all fifo bits: */
	iowrite8(NPCM_I2CFIF_CTS_CLR_FIFO, bus->reg + NPCM_I2CFIF_CTS);

#if IS_ENABLED(CONFIG_I2C_SLAVE)
	if (bus->slave) {
		addr = bus->slave->addr;
		npcm_i2c_slave_enable(bus, I2C_SLAVE_ADDR1, addr, true);
	}
#endif

	bus->state = I2C_IDLE;
}

static inline bool npcm_i2c_is_master(struct npcm_i2c *bus)
{
	return !!FIELD_GET(NPCM_I2CST_MASTER, ioread8(bus->reg + NPCM_I2CST));
}

static void npcm_i2c_callback(struct npcm_i2c *bus,
			      enum i2c_state_ind op_status, u16 info)
{
	struct i2c_msg *msgs;
	int msgs_num;

	msgs = bus->msgs;
	msgs_num = bus->msgs_num;
	/*
	 * check that transaction was not timed-out, and msgs still
	 * holds a valid value.
	 */
	if (!msgs)
		return;

	if (completion_done(&bus->cmd_complete))
		return;

	switch (op_status) {
	case I2C_MASTER_DONE_IND:
		bus->cmd_err = bus->msgs_num;
		fallthrough;
	case I2C_BLOCK_BYTES_ERR_IND:
		/* Master tx finished and all transmit bytes were sent */
		if (bus->msgs) {
			if (msgs[0].flags & I2C_M_RD)
				msgs[0].len = info;
			else if (msgs_num == 2 &&
				 msgs[1].flags & I2C_M_RD)
				msgs[1].len = info;
		}
		if (completion_done(&bus->cmd_complete) == false)
			complete(&bus->cmd_complete);
	break;

	case I2C_NACK_IND:
		/* MASTER transmit got a NACK before tx all bytes */
		bus->cmd_err = -ENXIO;
		if (bus->master_or_slave == I2C_MASTER)
			complete(&bus->cmd_complete);

		break;
	case I2C_BUS_ERR_IND:
		/* Bus error */
		bus->cmd_err = -EAGAIN;
		if (bus->master_or_slave == I2C_MASTER)
			complete(&bus->cmd_complete);

		break;
	case I2C_WAKE_UP_IND:
		/* I2C wake up */
		break;
	default:
		break;
	}

	bus->operation = I2C_NO_OPER;
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	if (bus->slave)
		bus->master_or_slave = I2C_SLAVE;
#endif
}

static u8 npcm_i2c_fifo_usage(struct npcm_i2c *bus)
{
	if (bus->operation == I2C_WRITE_OPER)
		return FIELD_GET(NPCM_I2CTXF_STS_TX_BYTES,
				 ioread8(bus->reg + NPCM_I2CTXF_STS));
	if (bus->operation == I2C_READ_OPER)
		return FIELD_GET(NPCM_I2CRXF_STS_RX_BYTES,
				 ioread8(bus->reg + NPCM_I2CRXF_STS));
	return 0;
}

static void npcm_i2c_write_to_fifo_master(struct npcm_i2c *bus, u16 max_bytes)
{
	u8 size_free_fifo;

	/*
	 * Fill the FIFO, while the FIFO is not full and there are more bytes
	 * to write
	 */
	size_free_fifo = I2C_HW_FIFO_SIZE - npcm_i2c_fifo_usage(bus);
	while (max_bytes-- && size_free_fifo) {
		if (bus->wr_ind < bus->wr_size)
			npcm_i2c_wr_byte(bus, bus->wr_buf[bus->wr_ind++]);
		else
			npcm_i2c_wr_byte(bus, 0xFF);
		size_free_fifo = I2C_HW_FIFO_SIZE - npcm_i2c_fifo_usage(bus);
	}
}

/*
 * npcm_i2c_set_fifo:
 * configure the FIFO before using it. If nread is -1 RX FIFO will not be
 * configured. same for nwrite
 */
static void npcm_i2c_set_fifo(struct npcm_i2c *bus, int nread, int nwrite)
{
	u8 rxf_ctl = 0;

	if (!bus->fifo_use)
		return;
	npcm_i2c_select_bank(bus, I2C_BANK_1);
	npcm_i2c_clear_tx_fifo(bus);
	npcm_i2c_clear_rx_fifo(bus);

	/* configure RX FIFO */
	if (nread > 0) {
		rxf_ctl = min_t(int, nread, I2C_HW_FIFO_SIZE);

		/* set LAST bit. if LAST is set next FIFO packet is nacked */
		if (nread <= I2C_HW_FIFO_SIZE)
			rxf_ctl |= NPCM_I2CRXF_CTL_LAST_PEC;

		/*
		 * if we are about to read the first byte in blk rd mode,
		 * don't NACK it. If slave returns zero size HW can't NACK
		 * it immediately, it will read extra byte and then NACK.
		 */
		if (bus->rd_ind == 0 && bus->read_block_use) {
			/* set fifo to read one byte, no last: */
			rxf_ctl = 1;
		}

		/* set fifo size: */
		iowrite8(rxf_ctl, bus->reg + NPCM_I2CRXF_CTL);
	}

	/* configure TX FIFO */
	if (nwrite > 0) {
		if (nwrite > I2C_HW_FIFO_SIZE)
			/* data to send is more then FIFO size. */
			iowrite8(I2C_HW_FIFO_SIZE, bus->reg + NPCM_I2CTXF_CTL);
		else
			iowrite8(nwrite, bus->reg + NPCM_I2CTXF_CTL);

		npcm_i2c_clear_tx_fifo(bus);
	}
}

static void npcm_i2c_read_fifo(struct npcm_i2c *bus, u8 bytes_in_fifo)
{
	u8 data;

	while (bytes_in_fifo--) {
		data = npcm_i2c_rd_byte(bus);
		if (bus->rd_ind < bus->rd_size)
			bus->rd_buf[bus->rd_ind++] = data;
	}
}

static inline void npcm_i2c_clear_master_status(struct npcm_i2c *bus)
{
	u8 val;

	/* Clear NEGACK, STASTR and BER bits */
	val = NPCM_I2CST_BER | NPCM_I2CST_NEGACK | NPCM_I2CST_STASTR;
	iowrite8(val, bus->reg + NPCM_I2CST);
}

static void npcm_i2c_master_abort(struct npcm_i2c *bus)
{
	/* Only current master is allowed to issue a stop condition */
	if (!npcm_i2c_is_master(bus))
		return;

	npcm_i2c_eob_int(bus, true);
	npcm_i2c_master_stop(bus);
	npcm_i2c_clear_master_status(bus);
}

#if IS_ENABLED(CONFIG_I2C_SLAVE)
static u8 npcm_i2c_get_slave_addr(struct npcm_i2c *bus, enum i2c_addr addr_type)
{
	u8 slave_add;

	/* select bank 0 for address 3 to 10 */
	if (addr_type > I2C_SLAVE_ADDR2)
		npcm_i2c_select_bank(bus, I2C_BANK_0);

	slave_add = ioread8(bus->reg + npcm_i2caddr[(int)addr_type]);

	if (addr_type > I2C_SLAVE_ADDR2)
		npcm_i2c_select_bank(bus, I2C_BANK_1);

	return slave_add;
}

static int npcm_i2c_remove_slave_addr(struct npcm_i2c *bus, u8 slave_add)
{
	int i;

	/* Set the enable bit */
	slave_add |= 0x80;
	npcm_i2c_select_bank(bus, I2C_BANK_0);
	for (i = I2C_SLAVE_ADDR1; i < I2C_NUM_OWN_ADDR; i++) {
		if (ioread8(bus->reg + npcm_i2caddr[i]) == slave_add)
			iowrite8(0, bus->reg + npcm_i2caddr[i]);
	}
	npcm_i2c_select_bank(bus, I2C_BANK_1);
	return 0;
}

static void npcm_i2c_write_fifo_slave(struct npcm_i2c *bus, u16 max_bytes)
{
	/*
	 * Fill the FIFO, while the FIFO is not full and there are more bytes
	 * to write
	 */
	npcm_i2c_clear_fifo_int(bus);
	npcm_i2c_clear_tx_fifo(bus);
	iowrite8(0, bus->reg + NPCM_I2CTXF_CTL);
	while (max_bytes-- && I2C_HW_FIFO_SIZE != npcm_i2c_fifo_usage(bus)) {
		if (bus->slv_wr_size <= 0)
			break;
		bus->slv_wr_ind = bus->slv_wr_ind % I2C_HW_FIFO_SIZE;
		npcm_i2c_wr_byte(bus, bus->slv_wr_buf[bus->slv_wr_ind]);
		bus->slv_wr_ind++;
		bus->slv_wr_ind = bus->slv_wr_ind % I2C_HW_FIFO_SIZE;
		bus->slv_wr_size--;
	}
}

static void npcm_i2c_read_fifo_slave(struct npcm_i2c *bus, u8 bytes_in_fifo)
{
	u8 data;

	if (!bus->slave)
		return;

	while (bytes_in_fifo--) {
		data = npcm_i2c_rd_byte(bus);

		bus->slv_rd_ind = bus->slv_rd_ind % I2C_HW_FIFO_SIZE;
		bus->slv_rd_buf[bus->slv_rd_ind] = data;
		bus->slv_rd_ind++;

		/* 1st byte is length in block protocol: */
		if (bus->slv_rd_ind == 1 && bus->read_block_use)
			bus->slv_rd_size = data + bus->PEC_use + 1;
	}
}

static int npcm_i2c_slave_get_wr_buf(struct npcm_i2c *bus)
{
	int i;
	u8 value;
	int ind;
	int ret = bus->slv_wr_ind;

	/* fill a cyclic buffer */
	for (i = 0; i < I2C_HW_FIFO_SIZE; i++) {
		if (bus->slv_wr_size >= I2C_HW_FIFO_SIZE)
			break;
		i2c_slave_event(bus->slave, I2C_SLAVE_READ_REQUESTED, &value);
		ind = (bus->slv_wr_ind + bus->slv_wr_size) % I2C_HW_FIFO_SIZE;
		bus->slv_wr_buf[ind] = value;
		bus->slv_wr_size++;
		i2c_slave_event(bus->slave, I2C_SLAVE_READ_PROCESSED, &value);
	}
	return I2C_HW_FIFO_SIZE - ret;
}

static void npcm_i2c_slave_send_rd_buf(struct npcm_i2c *bus)
{
	int i;

	for (i = 0; i < bus->slv_rd_ind; i++)
		i2c_slave_event(bus->slave, I2C_SLAVE_WRITE_RECEIVED,
				&bus->slv_rd_buf[i]);
	/*
	 * once we send bytes up, need to reset the counter of the wr buf
	 * got data from master (new offset in device), ignore wr fifo:
	 */
	if (bus->slv_rd_ind) {
		bus->slv_wr_size = 0;
		bus->slv_wr_ind = 0;
	}

	bus->slv_rd_ind = 0;
	bus->slv_rd_size = bus->adap.quirks->max_read_len;

	npcm_i2c_clear_fifo_int(bus);
	npcm_i2c_clear_rx_fifo(bus);
}

static void npcm_i2c_slave_receive(struct npcm_i2c *bus, u16 nread,
				   u8 *read_data)
{
	bus->state = I2C_OPER_STARTED;
	bus->operation = I2C_READ_OPER;
	bus->slv_rd_size = nread;
	bus->slv_rd_ind = 0;

	iowrite8(0, bus->reg + NPCM_I2CTXF_CTL);
	iowrite8(I2C_HW_FIFO_SIZE, bus->reg + NPCM_I2CRXF_CTL);
	npcm_i2c_clear_tx_fifo(bus);
	npcm_i2c_clear_rx_fifo(bus);
}

static void npcm_i2c_slave_xmit(struct npcm_i2c *bus, u16 nwrite,
				u8 *write_data)
{
	if (nwrite == 0)
		return;

	bus->state = I2C_OPER_STARTED;
	bus->operation = I2C_WRITE_OPER;

	/* get the next buffer */
	npcm_i2c_slave_get_wr_buf(bus);
	npcm_i2c_write_fifo_slave(bus, nwrite);
}

/*
 * npcm_i2c_slave_wr_buf_sync:
 * currently slave IF only supports single byte operations.
 * in order to utilize the npcm HW FIFO, the driver will ask for 16 bytes
 * at a time, pack them in buffer, and then transmit them all together
 * to the FIFO and onward to the bus.
 * NACK on read will be once reached to bus->adap->quirks->max_read_len.
 * sending a NACK wherever the backend requests for it is not supported.
 * the next two functions allow reading to local buffer before writing it all
 * to the HW FIFO.
 */
static void npcm_i2c_slave_wr_buf_sync(struct npcm_i2c *bus)
{
	int left_in_fifo;

	left_in_fifo = FIELD_GET(NPCM_I2CTXF_STS_TX_BYTES,
				 ioread8(bus->reg + NPCM_I2CTXF_STS));

	/* fifo already full: */
	if (left_in_fifo >= I2C_HW_FIFO_SIZE ||
	    bus->slv_wr_size >= I2C_HW_FIFO_SIZE)
		return;

	/* update the wr fifo index back to the untransmitted bytes: */
	bus->slv_wr_ind = bus->slv_wr_ind - left_in_fifo;
	bus->slv_wr_size = bus->slv_wr_size + left_in_fifo;

	if (bus->slv_wr_ind < 0)
		bus->slv_wr_ind += I2C_HW_FIFO_SIZE;
}

static void npcm_i2c_slave_rd_wr(struct npcm_i2c *bus)
{
	if (NPCM_I2CST_XMIT & ioread8(bus->reg + NPCM_I2CST)) {
		/*
		 * Slave got an address match with direction bit 1 so it should
		 * transmit data. Write till the master will NACK
		 */
		bus->operation = I2C_WRITE_OPER;
		npcm_i2c_slave_xmit(bus, bus->adap.quirks->max_write_len,
				    bus->slv_wr_buf);
	} else {
		/*
		 * Slave got an address match with direction bit 0 so it should
		 * receive data.
		 * this module does not support saying no to bytes.
		 * it will always ACK.
		 */
		bus->operation = I2C_READ_OPER;
		npcm_i2c_read_fifo_slave(bus, npcm_i2c_fifo_usage(bus));
		bus->stop_ind = I2C_SLAVE_RCV_IND;
		npcm_i2c_slave_send_rd_buf(bus);
		npcm_i2c_slave_receive(bus, bus->adap.quirks->max_read_len,
				       bus->slv_rd_buf);
	}
}

static irqreturn_t npcm_i2c_int_slave_handler(struct npcm_i2c *bus)
{
	u8 val;
	irqreturn_t ret = IRQ_NONE;
	u8 i2cst = ioread8(bus->reg + NPCM_I2CST);

	/* Slave: A NACK has occurred */
	if (NPCM_I2CST_NEGACK & i2cst) {
		bus->stop_ind = I2C_NACK_IND;
		npcm_i2c_slave_wr_buf_sync(bus);
		if (bus->fifo_use)
			/* clear the FIFO */
			iowrite8(NPCM_I2CFIF_CTS_CLR_FIFO,
				 bus->reg + NPCM_I2CFIF_CTS);

		/* In slave write, NACK is OK, otherwise it is a problem */
		bus->stop_ind = I2C_NO_STATUS_IND;
		bus->operation = I2C_NO_OPER;
		bus->own_slave_addr = 0xFF;

		/*
		 * Slave has to wait for STOP to decide this is the end
		 * of the transaction. tx is not yet considered as done
		 */
		iowrite8(NPCM_I2CST_NEGACK, bus->reg + NPCM_I2CST);

		ret = IRQ_HANDLED;
	}

	/* Slave mode: a Bus Error (BER) has been identified */
	if (NPCM_I2CST_BER & i2cst) {
		/*
		 * Check whether bus arbitration or Start or Stop during data
		 * xfer bus arbitration problem should not result in recovery
		 */
		bus->stop_ind = I2C_BUS_ERR_IND;

		/* wait for bus busy before clear fifo */
		iowrite8(NPCM_I2CFIF_CTS_CLR_FIFO, bus->reg + NPCM_I2CFIF_CTS);

		bus->state = I2C_IDLE;

		/*
		 * in BER case we might get 2 interrupts: one for slave one for
		 * master ( for a channel which is master\slave switching)
		 */
		if (completion_done(&bus->cmd_complete) == false) {
			bus->cmd_err = -EIO;
			complete(&bus->cmd_complete);
		}
		bus->own_slave_addr = 0xFF;
		iowrite8(NPCM_I2CST_BER, bus->reg + NPCM_I2CST);
		ret = IRQ_HANDLED;
	}

	/* A Slave Stop Condition has been identified */
	if (NPCM_I2CST_SLVSTP & i2cst) {
		u8 bytes_in_fifo = npcm_i2c_fifo_usage(bus);

		bus->stop_ind = I2C_SLAVE_DONE_IND;

		if (bus->operation == I2C_READ_OPER)
			npcm_i2c_read_fifo_slave(bus, bytes_in_fifo);

		/* if the buffer is empty nothing will be sent */
		npcm_i2c_slave_send_rd_buf(bus);

		/* Slave done transmitting or receiving */
		bus->stop_ind = I2C_NO_STATUS_IND;

		/*
		 * Note, just because we got here, it doesn't mean we through
		 * away the wr buffer.
		 * we keep it until the next received offset.
		 */
		bus->operation = I2C_NO_OPER;
		bus->own_slave_addr = 0xFF;
		i2c_slave_event(bus->slave, I2C_SLAVE_STOP, 0);
		iowrite8(NPCM_I2CST_SLVSTP, bus->reg + NPCM_I2CST);
		if (bus->fifo_use) {
			npcm_i2c_clear_fifo_int(bus);
			npcm_i2c_clear_rx_fifo(bus);
			npcm_i2c_clear_tx_fifo(bus);

			iowrite8(NPCM_I2CFIF_CTS_CLR_FIFO,
				 bus->reg + NPCM_I2CFIF_CTS);
		}
		bus->state = I2C_IDLE;
		ret = IRQ_HANDLED;
	}

	/* restart condition occurred and Rx-FIFO was not empty */
	if (bus->fifo_use && FIELD_GET(NPCM_I2CFIF_CTS_SLVRSTR,
				       ioread8(bus->reg + NPCM_I2CFIF_CTS))) {
		bus->stop_ind = I2C_SLAVE_RESTART_IND;
		bus->master_or_slave = I2C_SLAVE;
		if (bus->operation == I2C_READ_OPER)
			npcm_i2c_read_fifo_slave(bus, npcm_i2c_fifo_usage(bus));
		bus->operation = I2C_WRITE_OPER;
		iowrite8(0, bus->reg + NPCM_I2CRXF_CTL);
		val = NPCM_I2CFIF_CTS_CLR_FIFO | NPCM_I2CFIF_CTS_SLVRSTR |
		      NPCM_I2CFIF_CTS_RXF_TXE;
		iowrite8(val, bus->reg + NPCM_I2CFIF_CTS);
		npcm_i2c_slave_rd_wr(bus);
		ret = IRQ_HANDLED;
	}

	/* A Slave Address Match has been identified */
	if (NPCM_I2CST_NMATCH & i2cst) {
		u8 info = 0;

		/* Address match automatically implies slave mode */
		bus->master_or_slave = I2C_SLAVE;
		npcm_i2c_clear_fifo_int(bus);
		npcm_i2c_clear_rx_fifo(bus);
		npcm_i2c_clear_tx_fifo(bus);
		iowrite8(0, bus->reg + NPCM_I2CTXF_CTL);
		iowrite8(I2C_HW_FIFO_SIZE, bus->reg + NPCM_I2CRXF_CTL);
		if (NPCM_I2CST_XMIT & i2cst) {
			bus->operation = I2C_WRITE_OPER;
		} else {
			i2c_slave_event(bus->slave, I2C_SLAVE_WRITE_REQUESTED,
					&info);
			bus->operation = I2C_READ_OPER;
		}
		if (bus->own_slave_addr == 0xFF) {
			/* Check which type of address match */
			val = ioread8(bus->reg + NPCM_I2CCST);
			if (NPCM_I2CCST_MATCH & val) {
				u16 addr;
				enum i2c_addr eaddr;
				u8 i2ccst2;
				u8 i2ccst3;

				i2ccst3 = ioread8(bus->reg + NPCM_I2CCST3);
				i2ccst2 = ioread8(bus->reg + NPCM_I2CCST2);

				/*
				 * the i2c module can response to 10 own SA.
				 * check which one was addressed by the master.
				 * respond to the first one.
				 */
				addr = ((i2ccst3 & 0x07) << 7) |
					(i2ccst2 & 0x7F);
				info = ffs(addr);
				eaddr = (enum i2c_addr)info;
				addr = npcm_i2c_get_slave_addr(bus, eaddr);
				addr &= 0x7F;
				bus->own_slave_addr = addr;
				if (bus->PEC_mask & BIT(info))
					bus->PEC_use = true;
				else
					bus->PEC_use = false;
			} else {
				if (NPCM_I2CCST_GCMATCH & val)
					bus->own_slave_addr = 0;
				if (NPCM_I2CCST_ARPMATCH & val)
					bus->own_slave_addr = 0x61;
			}
		} else {
			/*
			 *  Slave match can happen in two options:
			 *  1. Start, SA, read (slave read without further ado)
			 *  2. Start, SA, read, data, restart, SA, read,  ...
			 *     (slave read in fragmented mode)
			 *  3. Start, SA, write, data, restart, SA, read, ..
			 *     (regular write-read mode)
			 */
			if ((bus->state == I2C_OPER_STARTED &&
			     bus->operation == I2C_READ_OPER &&
			     bus->stop_ind == I2C_SLAVE_XMIT_IND) ||
			     bus->stop_ind == I2C_SLAVE_RCV_IND) {
				/* slave tx after slave rx w/o STOP */
				bus->stop_ind = I2C_SLAVE_RESTART_IND;
			}
		}

		if (NPCM_I2CST_XMIT & i2cst)
			bus->stop_ind = I2C_SLAVE_XMIT_IND;
		else
			bus->stop_ind = I2C_SLAVE_RCV_IND;
		bus->state = I2C_SLAVE_MATCH;
		npcm_i2c_slave_rd_wr(bus);
		iowrite8(NPCM_I2CST_NMATCH, bus->reg + NPCM_I2CST);
		ret = IRQ_HANDLED;
	}

	/* Slave SDA status is set - tx or rx */
	if ((NPCM_I2CST_SDAST & i2cst) ||
	    (bus->fifo_use &&
	    (npcm_i2c_tx_fifo_empty(bus) || npcm_i2c_rx_fifo_full(bus)))) {
		npcm_i2c_slave_rd_wr(bus);
		iowrite8(NPCM_I2CST_SDAST, bus->reg + NPCM_I2CST);
		ret = IRQ_HANDLED;
	} /* SDAST */

	return ret;
}

static int npcm_i2c_reg_slave(struct i2c_client *client)
{
	unsigned long lock_flags;
	struct npcm_i2c *bus = i2c_get_adapdata(client->adapter);

	bus->slave = client;

	if (!bus->slave)
		return -EINVAL;

	if (client->flags & I2C_CLIENT_TEN)
		return -EAFNOSUPPORT;

	spin_lock_irqsave(&bus->lock, lock_flags);

	npcm_i2c_init_params(bus);
	bus->slv_rd_size = 0;
	bus->slv_wr_size = 0;
	bus->slv_rd_ind = 0;
	bus->slv_wr_ind = 0;
	if (client->flags & I2C_CLIENT_PEC)
		bus->PEC_use = true;

	dev_info(bus->dev, "i2c%d register slave SA=0x%x, PEC=%d\n", bus->num,
		 client->addr, bus->PEC_use);

	npcm_i2c_slave_enable(bus, I2C_SLAVE_ADDR1, client->addr, true);
	npcm_i2c_clear_fifo_int(bus);
	npcm_i2c_clear_rx_fifo(bus);
	npcm_i2c_clear_tx_fifo(bus);
	npcm_i2c_slave_int_enable(bus, true);

	spin_unlock_irqrestore(&bus->lock, lock_flags);
	return 0;
}

static int npcm_i2c_unreg_slave(struct i2c_client *client)
{
	struct npcm_i2c *bus = client->adapter->algo_data;
	unsigned long lock_flags;

	spin_lock_irqsave(&bus->lock, lock_flags);
	if (!bus->slave) {
		spin_unlock_irqrestore(&bus->lock, lock_flags);
		return -EINVAL;
	}
	npcm_i2c_slave_int_enable(bus, false);
	npcm_i2c_remove_slave_addr(bus, client->addr);
	bus->slave = NULL;
	spin_unlock_irqrestore(&bus->lock, lock_flags);
	return 0;
}
#endif /* CONFIG_I2C_SLAVE */

static void npcm_i2c_master_fifo_read(struct npcm_i2c *bus)
{
	int rcount;
	int fifo_bytes;
	enum i2c_state_ind ind = I2C_MASTER_DONE_IND;

	fifo_bytes = npcm_i2c_fifo_usage(bus);
	rcount = bus->rd_size - bus->rd_ind;

	/*
	 * In order not to change the RX_TRH during transaction (we found that
	 * this might be problematic if it takes too much time to read the FIFO)
	 * we read the data in the following way. If the number of bytes to
	 * read == FIFO Size + C (where C < FIFO Size)then first read C bytes
	 * and in the next int we read rest of the data.
	 */
	if (rcount < (2 * I2C_HW_FIFO_SIZE) && rcount > I2C_HW_FIFO_SIZE)
		fifo_bytes = rcount - I2C_HW_FIFO_SIZE;

	if (rcount <= fifo_bytes) {
		/* last bytes are about to be read - end of tx */
		bus->state = I2C_STOP_PENDING;
		bus->stop_ind = ind;
		npcm_i2c_eob_int(bus, true);
		/* Stop should be set before reading last byte. */
		npcm_i2c_master_stop(bus);
		npcm_i2c_read_fifo(bus, fifo_bytes);
	} else {
		npcm_i2c_read_fifo(bus, fifo_bytes);
		rcount = bus->rd_size - bus->rd_ind;
		npcm_i2c_set_fifo(bus, rcount, -1);
	}
}

static void npcm_i2c_irq_master_handler_write(struct npcm_i2c *bus)
{
	u16 wcount;

	if (bus->fifo_use)
		npcm_i2c_clear_tx_fifo(bus); /* clear the TX fifo status bit */

	/* Master write operation - last byte handling */
	if (bus->wr_ind == bus->wr_size) {
		if (bus->fifo_use && npcm_i2c_fifo_usage(bus) > 0)
			/*
			 * No more bytes to send (to add to the FIFO),
			 * however the FIFO is not empty yet. It is
			 * still in the middle of tx. Currently there's nothing
			 * to do except for waiting to the end of the tx
			 * We will get an int when the FIFO will get empty.
			 */
			return;

		if (bus->rd_size == 0) {
			/* all bytes have been written, in wr only operation */
			npcm_i2c_eob_int(bus, true);
			bus->state = I2C_STOP_PENDING;
			bus->stop_ind = I2C_MASTER_DONE_IND;
			npcm_i2c_master_stop(bus);
			/* Clear SDA Status bit (by writing dummy byte) */
			npcm_i2c_wr_byte(bus, 0xFF);

		} else {
			/* last write-byte written on previous int - restart */
			npcm_i2c_set_fifo(bus, bus->rd_size, -1);
			/* Generate repeated start upon next write to SDA */
			npcm_i2c_master_start(bus);

			/*
			 * Receiving one byte only - stall after successful
			 * completion of send address byte. If we NACK here, and
			 * slave doesn't ACK the address, we might
			 * unintentionally NACK the next multi-byte read.
			 */
			if (bus->rd_size == 1)
				npcm_i2c_stall_after_start(bus, true);

			/* Next int will occur on read */
			bus->operation = I2C_READ_OPER;
			/* send the slave address in read direction */
			npcm_i2c_wr_byte(bus, bus->dest_addr | 0x1);
		}
	} else {
		/* write next byte not last byte and not slave address */
		if (!bus->fifo_use || bus->wr_size == 1) {
			npcm_i2c_wr_byte(bus, bus->wr_buf[bus->wr_ind++]);
		} else {
			wcount = bus->wr_size - bus->wr_ind;
			npcm_i2c_set_fifo(bus, -1, wcount);
			if (wcount)
				npcm_i2c_write_to_fifo_master(bus, wcount);
		}
	}
}

static void npcm_i2c_irq_master_handler_read(struct npcm_i2c *bus)
{
	u16 block_extra_bytes_size;
	u8 data;

	/* added bytes to the packet: */
	block_extra_bytes_size = bus->read_block_use + bus->PEC_use;

	/*
	 * Perform master read, distinguishing between last byte and the rest of
	 * the bytes. The last byte should be read when the clock is stopped
	 */
	if (bus->rd_ind == 0) { /* first byte handling: */
		if (bus->read_block_use) {
			/* first byte in block protocol is the size: */
			data = npcm_i2c_rd_byte(bus);
			data = clamp_val(data, 1, I2C_SMBUS_BLOCK_MAX);
			bus->rd_size = data + block_extra_bytes_size;
			bus->rd_buf[bus->rd_ind++] = data;

			/* clear RX FIFO interrupt status: */
			if (bus->fifo_use) {
				data = ioread8(bus->reg + NPCM_I2CFIF_CTS);
				data = data | NPCM_I2CFIF_CTS_RXF_TXE;
				iowrite8(data, bus->reg + NPCM_I2CFIF_CTS);
			}

			npcm_i2c_set_fifo(bus, bus->rd_size - 1, -1);
			npcm_i2c_stall_after_start(bus, false);
		} else {
			npcm_i2c_clear_tx_fifo(bus);
			npcm_i2c_master_fifo_read(bus);
		}
	} else {
		if (bus->rd_size == block_extra_bytes_size &&
		    bus->read_block_use) {
			bus->state = I2C_STOP_PENDING;
			bus->stop_ind = I2C_BLOCK_BYTES_ERR_IND;
			bus->cmd_err = -EIO;
			npcm_i2c_eob_int(bus, true);
			npcm_i2c_master_stop(bus);
			npcm_i2c_read_fifo(bus, npcm_i2c_fifo_usage(bus));
		} else {
			npcm_i2c_master_fifo_read(bus);
		}
	}
}

static void npcm_i2c_irq_handle_nmatch(struct npcm_i2c *bus)
{
	iowrite8(NPCM_I2CST_NMATCH, bus->reg + NPCM_I2CST);
	npcm_i2c_nack(bus);
	bus->stop_ind = I2C_BUS_ERR_IND;
	npcm_i2c_callback(bus, bus->stop_ind, npcm_i2c_get_index(bus));
}

/* A NACK has occurred */
static void npcm_i2c_irq_handle_nack(struct npcm_i2c *bus)
{
	u8 val;

	if (bus->nack_cnt < ULLONG_MAX)
		bus->nack_cnt++;

	if (bus->fifo_use) {
		/*
		 * if there are still untransmitted bytes in TX FIFO
		 * reduce them from wr_ind
		 */
		if (bus->operation == I2C_WRITE_OPER)
			bus->wr_ind -= npcm_i2c_fifo_usage(bus);

		/* clear the FIFO */
		iowrite8(NPCM_I2CFIF_CTS_CLR_FIFO, bus->reg + NPCM_I2CFIF_CTS);
	}

	/* In master write operation, got unexpected NACK */
	bus->stop_ind = I2C_NACK_IND;
	/* Only current master is allowed to issue Stop Condition */
	if (npcm_i2c_is_master(bus)) {
		/* stopping in the middle */
		npcm_i2c_eob_int(bus, false);
		npcm_i2c_master_stop(bus);

		/*
		 * The bus is released from stall only after the SW clears
		 * NEGACK bit. Then a Stop condition is sent.
		 */
		npcm_i2c_clear_master_status(bus);
		readx_poll_timeout_atomic(ioread8, bus->reg + NPCM_I2CCST, val,
					  !(val & NPCM_I2CCST_BUSY), 10, 200);
	}
	bus->state = I2C_IDLE;

	/*
	 * In Master mode, NACK should be cleared only after STOP.
	 * In such case, the bus is released from stall only after the
	 * software clears NACK bit. Then a Stop condition is sent.
	 */
	npcm_i2c_callback(bus, bus->stop_ind, bus->wr_ind);
}

	/* Master mode: a Bus Error has been identified */
static void npcm_i2c_irq_handle_ber(struct npcm_i2c *bus)
{
	if (bus->ber_cnt < ULLONG_MAX)
		bus->ber_cnt++;
	bus->stop_ind = I2C_BUS_ERR_IND;
	if (npcm_i2c_is_master(bus)) {
		npcm_i2c_master_abort(bus);
	} else {
		npcm_i2c_clear_master_status(bus);

		/* Clear BB (BUS BUSY) bit */
		iowrite8(NPCM_I2CCST_BB, bus->reg + NPCM_I2CCST);

		bus->cmd_err = -EAGAIN;
		npcm_i2c_callback(bus, bus->stop_ind, npcm_i2c_get_index(bus));
	}
	bus->state = I2C_IDLE;
}

	/* EOB: a master End Of Busy (meaning STOP completed) */
static void npcm_i2c_irq_handle_eob(struct npcm_i2c *bus)
{
	npcm_i2c_eob_int(bus, false);
	bus->state = I2C_IDLE;
	npcm_i2c_callback(bus, bus->stop_ind, bus->rd_ind);
}

/* Address sent and requested stall occurred (Master mode) */
static void npcm_i2c_irq_handle_stall_after_start(struct npcm_i2c *bus)
{
	if (npcm_i2c_is_quick(bus)) {
		bus->state = I2C_STOP_PENDING;
		bus->stop_ind = I2C_MASTER_DONE_IND;
		npcm_i2c_eob_int(bus, true);
		npcm_i2c_master_stop(bus);
	} else if ((bus->rd_size == 1) && !bus->read_block_use) {
		/*
		 * Receiving one byte only - set NACK after ensuring
		 * slave ACKed the address byte.
		 */
		npcm_i2c_nack(bus);
	}

	/* Reset stall-after-address-byte */
	npcm_i2c_stall_after_start(bus, false);

	/* Clear stall only after setting STOP */
	iowrite8(NPCM_I2CST_STASTR, bus->reg + NPCM_I2CST);
}

/* SDA status is set - TX or RX, master */
static void npcm_i2c_irq_handle_sda(struct npcm_i2c *bus, u8 i2cst)
{
	u8 fif_cts;

	if (!npcm_i2c_is_master(bus))
		return;

	if (bus->state == I2C_IDLE) {
		bus->stop_ind = I2C_WAKE_UP_IND;

		if (npcm_i2c_is_quick(bus) || bus->read_block_use)
			/*
			 * Need to stall after successful
			 * completion of sending address byte
			 */
			npcm_i2c_stall_after_start(bus, true);
		else
			npcm_i2c_stall_after_start(bus, false);

		/*
		 * Receiving one byte only - stall after successful completion
		 * of sending address byte If we NACK here, and slave doesn't
		 * ACK the address, we might unintentionally NACK the next
		 * multi-byte read
		 */
		if (bus->wr_size == 0 && bus->rd_size == 1)
			npcm_i2c_stall_after_start(bus, true);

		/* Initiate I2C master tx */

		/* select bank 1 for FIFO regs */
		npcm_i2c_select_bank(bus, I2C_BANK_1);

		fif_cts = ioread8(bus->reg + NPCM_I2CFIF_CTS);
		fif_cts = fif_cts & ~NPCM_I2CFIF_CTS_SLVRSTR;

		/* clear FIFO and relevant status bits. */
		fif_cts = fif_cts | NPCM_I2CFIF_CTS_CLR_FIFO;
		iowrite8(fif_cts, bus->reg + NPCM_I2CFIF_CTS);

		/* re-enable */
		fif_cts = fif_cts | NPCM_I2CFIF_CTS_RXF_TXE;
		iowrite8(fif_cts, bus->reg + NPCM_I2CFIF_CTS);

		/*
		 * Configure the FIFO threshold:
		 * according to the needed # of bytes to read.
		 * Note: due to HW limitation can't config the rx fifo before it
		 * got and ACK on the restart. LAST bit will not be reset unless
		 * RX completed. It will stay set on the next tx.
		 */
		if (bus->wr_size)
			npcm_i2c_set_fifo(bus, -1, bus->wr_size);
		else
			npcm_i2c_set_fifo(bus, bus->rd_size, -1);

		bus->state = I2C_OPER_STARTED;

		if (npcm_i2c_is_quick(bus) || bus->wr_size)
			npcm_i2c_wr_byte(bus, bus->dest_addr);
		else
			npcm_i2c_wr_byte(bus, bus->dest_addr | BIT(0));
	/* SDA interrupt, after start\restart */
	} else {
		if (NPCM_I2CST_XMIT & i2cst) {
			bus->operation = I2C_WRITE_OPER;
			npcm_i2c_irq_master_handler_write(bus);
		} else {
			bus->operation = I2C_READ_OPER;
			npcm_i2c_irq_master_handler_read(bus);
		}
	}
}

static int npcm_i2c_int_master_handler(struct npcm_i2c *bus)
{
	u8 i2cst;
	int ret = -EIO;

	i2cst = ioread8(bus->reg + NPCM_I2CST);

	if (FIELD_GET(NPCM_I2CST_NMATCH, i2cst)) {
		npcm_i2c_irq_handle_nmatch(bus);
		return 0;
	}
	/* A NACK has occurred */
	if (FIELD_GET(NPCM_I2CST_NEGACK, i2cst)) {
		npcm_i2c_irq_handle_nack(bus);
		return 0;
	}

	/* Master mode: a Bus Error has been identified */
	if (FIELD_GET(NPCM_I2CST_BER, i2cst)) {
		npcm_i2c_irq_handle_ber(bus);
		return 0;
	}

	/* EOB: a master End Of Busy (meaning STOP completed) */
	if ((FIELD_GET(NPCM_I2CCTL1_EOBINTE,
		       ioread8(bus->reg + NPCM_I2CCTL1)) == 1) &&
	    (FIELD_GET(NPCM_I2CCST3_EO_BUSY,
		       ioread8(bus->reg + NPCM_I2CCST3)))) {
		npcm_i2c_irq_handle_eob(bus);
		return 0;
	}

	/* Address sent and requested stall occurred (Master mode) */
	if (FIELD_GET(NPCM_I2CST_STASTR, i2cst)) {
		npcm_i2c_irq_handle_stall_after_start(bus);
		ret = 0;
	}

	/* SDA status is set - TX or RX, master */
	if (FIELD_GET(NPCM_I2CST_SDAST, i2cst) ||
	    (bus->fifo_use &&
	    (npcm_i2c_tx_fifo_empty(bus) || npcm_i2c_rx_fifo_full(bus)))) {
		npcm_i2c_irq_handle_sda(bus, i2cst);
		ret = 0;
	}

	return ret;
}

/* recovery using TGCLK functionality of the module */
static int npcm_i2c_recovery_tgclk(struct i2c_adapter *_adap)
{
	u8               val;
	u8               fif_cts;
	bool             done = false;
	int              status = -ENOTRECOVERABLE;
	struct npcm_i2c *bus = container_of(_adap, struct npcm_i2c, adap);
	/* Allow 3 bytes (27 toggles) to be read from the slave: */
	int              iter = 27;

	if ((npcm_i2c_get_SDA(_adap) == 1) && (npcm_i2c_get_SCL(_adap) == 1)) {
		dev_dbg(bus->dev, "bus%d recovery skipped, bus not stuck",
			bus->num);
		npcm_i2c_reset(bus);
		return status;
	}

	npcm_i2c_int_enable(bus, false);
	npcm_i2c_disable(bus);
	npcm_i2c_enable(bus);
	iowrite8(NPCM_I2CCST_BB, bus->reg + NPCM_I2CCST);
	npcm_i2c_clear_tx_fifo(bus);
	npcm_i2c_clear_rx_fifo(bus);
	iowrite8(0, bus->reg + NPCM_I2CRXF_CTL);
	iowrite8(0, bus->reg + NPCM_I2CTXF_CTL);
	npcm_i2c_stall_after_start(bus, false);

	/* select bank 1 for FIFO regs */
	npcm_i2c_select_bank(bus, I2C_BANK_1);

	/* clear FIFO and relevant status bits. */
	fif_cts = ioread8(bus->reg + NPCM_I2CFIF_CTS);
	fif_cts &= ~NPCM_I2CFIF_CTS_SLVRSTR;
	fif_cts |= NPCM_I2CFIF_CTS_CLR_FIFO;
	iowrite8(fif_cts, bus->reg + NPCM_I2CFIF_CTS);
	npcm_i2c_set_fifo(bus, -1, 0);

	/* Repeat the following sequence until SDA is released */
	do {
		/* Issue a single SCL toggle */
		iowrite8(NPCM_I2CCST_TGSCL, bus->reg + NPCM_I2CCST);
		usleep_range(20, 30);
		/* If SDA line is inactive (high), stop */
		if (npcm_i2c_get_SDA(_adap)) {
			done = true;
			status = 0;
		}
	} while (!done && iter--);

	/* If SDA line is released: send start-addr-stop, to re-sync. */
	if (npcm_i2c_get_SDA(_adap)) {
		/* Send an address byte in write direction: */
		npcm_i2c_wr_byte(bus, bus->dest_addr);
		npcm_i2c_master_start(bus);
		/* Wait until START condition is sent */
		status = readx_poll_timeout(npcm_i2c_get_SCL, _adap, val, !val,
					    20, 200);
		/* If START condition was sent */
		if (npcm_i2c_is_master(bus) > 0) {
			usleep_range(20, 30);
			npcm_i2c_master_stop(bus);
			usleep_range(200, 500);
		}
	}
	npcm_i2c_reset(bus);
	npcm_i2c_int_enable(bus, true);

	if ((npcm_i2c_get_SDA(_adap) == 1) && (npcm_i2c_get_SCL(_adap) == 1))
		status = 0;
	else
		status = -ENOTRECOVERABLE;
	if (status) {
		if (bus->rec_fail_cnt < ULLONG_MAX)
			bus->rec_fail_cnt++;
	} else {
		if (bus->rec_succ_cnt < ULLONG_MAX)
			bus->rec_succ_cnt++;
	}
	return status;
}

/* recovery using bit banging functionality of the module */
static void npcm_i2c_recovery_init(struct i2c_adapter *_adap)
{
	struct npcm_i2c *bus = container_of(_adap, struct npcm_i2c, adap);
	struct i2c_bus_recovery_info *rinfo = &bus->rinfo;

	rinfo->recover_bus = npcm_i2c_recovery_tgclk;

	/*
	 * npcm i2c HW allows direct reading of SCL and SDA.
	 * However, it does not support setting SCL and SDA directly.
	 * The recovery function can toggle SCL when SDA is low (but not set)
	 * Getter functions used internally, and can be used externally.
	 */
	rinfo->get_scl = npcm_i2c_get_SCL;
	rinfo->get_sda = npcm_i2c_get_SDA;
	_adap->bus_recovery_info = rinfo;
}

/* SCLFRQ min/max field values */
#define SCLFRQ_MIN  10
#define SCLFRQ_MAX  511
#define clk_coef(freq, mul)	DIV_ROUND_UP((freq) * (mul), 1000000)

/*
 * npcm_i2c_init_clk: init HW timing parameters.
 * NPCM7XX i2c module timing parameters are dependent on module core clk (APB)
 * and bus frequency.
 * 100kHz bus requires tSCL = 4 * SCLFRQ * tCLK. LT and HT are symmetric.
 * 400kHz bus requires asymmetric HT and LT. A different equation is recommended
 * by the HW designer, given core clock range (equations in comments below).
 *
 */
static int npcm_i2c_init_clk(struct npcm_i2c *bus, u32 bus_freq_hz)
{
	u32  k1 = 0;
	u32  k2 = 0;
	u8   dbnct = 0;
	u32  sclfrq = 0;
	u8   hldt = 7;
	u8   fast_mode = 0;
	u32  src_clk_khz;
	u32  bus_freq_khz;

	src_clk_khz = bus->apb_clk / 1000;
	bus_freq_khz = bus_freq_hz / 1000;
	bus->bus_freq = bus_freq_hz;

	/* 100KHz and below: */
	if (bus_freq_hz <= I2C_MAX_STANDARD_MODE_FREQ) {
		sclfrq = src_clk_khz / (bus_freq_khz * 4);

		if (sclfrq < SCLFRQ_MIN || sclfrq > SCLFRQ_MAX)
			return -EDOM;

		if (src_clk_khz >= 40000)
			hldt = 17;
		else if (src_clk_khz >= 12500)
			hldt = 15;
		else
			hldt = 7;
	}

	/* 400KHz: */
	else if (bus_freq_hz <= I2C_MAX_FAST_MODE_FREQ) {
		sclfrq = 0;
		fast_mode = I2CCTL3_400K_MODE;

		if (src_clk_khz < 7500)
			/* 400KHZ cannot be supported for core clock < 7.5MHz */
			return -EDOM;

		else if (src_clk_khz >= 50000) {
			k1 = 80;
			k2 = 48;
			hldt = 12;
			dbnct = 7;
		}

		/* Master or Slave with frequency > 25MHz */
		else if (src_clk_khz > 25000) {
			hldt = clk_coef(src_clk_khz, 300) + 7;
			k1 = clk_coef(src_clk_khz, 1600);
			k2 = clk_coef(src_clk_khz, 900);
		}
	}

	/* 1MHz: */
	else if (bus_freq_hz <= I2C_MAX_FAST_MODE_PLUS_FREQ) {
		sclfrq = 0;
		fast_mode = I2CCTL3_400K_MODE;

		/* 1MHZ cannot be supported for core clock < 24 MHz */
		if (src_clk_khz < 24000)
			return -EDOM;

		k1 = clk_coef(src_clk_khz, 620);
		k2 = clk_coef(src_clk_khz, 380);

		/* Core clk > 40 MHz */
		if (src_clk_khz > 40000) {
			/*
			 * Set HLDT:
			 * SDA hold time:  (HLDT-7) * T(CLK) >= 120
			 * HLDT = 120/T(CLK) + 7 = 120 * FREQ(CLK) + 7
			 */
			hldt = clk_coef(src_clk_khz, 120) + 7;
		} else {
			hldt = 7;
			dbnct = 2;
		}
	}

	/* Frequency larger than 1 MHz is not supported */
	else
		return -EINVAL;

	if (bus_freq_hz >= I2C_MAX_FAST_MODE_FREQ) {
		k1 = round_up(k1, 2);
		k2 = round_up(k2 + 1, 2);
		if (k1 < SCLFRQ_MIN || k1 > SCLFRQ_MAX ||
		    k2 < SCLFRQ_MIN || k2 > SCLFRQ_MAX)
			return -EDOM;
	}

	/* write sclfrq value. bits [6:0] are in I2CCTL2 reg */
	iowrite8(FIELD_PREP(I2CCTL2_SCLFRQ6_0, sclfrq & 0x7F),
		 bus->reg + NPCM_I2CCTL2);

	/* bits [8:7] are in I2CCTL3 reg */
	iowrite8(fast_mode | FIELD_PREP(I2CCTL3_SCLFRQ8_7, (sclfrq >> 7) & 0x3),
		 bus->reg + NPCM_I2CCTL3);

	/* Select Bank 0 to access NPCM_I2CCTL4/NPCM_I2CCTL5 */
	npcm_i2c_select_bank(bus, I2C_BANK_0);

	if (bus_freq_hz >= I2C_MAX_FAST_MODE_FREQ) {
		/*
		 * Set SCL Low/High Time:
		 * k1 = 2 * SCLLT7-0 -> Low Time  = k1 / 2
		 * k2 = 2 * SCLLT7-0 -> High Time = k2 / 2
		 */
		iowrite8(k1 / 2, bus->reg + NPCM_I2CSCLLT);
		iowrite8(k2 / 2, bus->reg + NPCM_I2CSCLHT);

		iowrite8(dbnct, bus->reg + NPCM_I2CCTL5);
	}

	iowrite8(hldt, bus->reg + NPCM_I2CCTL4);

	/* Return to Bank 1, and stay there by default: */
	npcm_i2c_select_bank(bus, I2C_BANK_1);

	return 0;
}

static int npcm_i2c_init_module(struct npcm_i2c *bus, enum i2c_mode mode,
				u32 bus_freq_hz)
{
	u8 val;
	int ret;

	/* Check whether module already enabled or frequency is out of bounds */
	if ((bus->state != I2C_DISABLE && bus->state != I2C_IDLE) ||
	    bus_freq_hz < I2C_FREQ_MIN_HZ || bus_freq_hz > I2C_FREQ_MAX_HZ)
		return -EINVAL;

	npcm_i2c_disable(bus);

	/* Configure FIFO mode : */
	if (FIELD_GET(I2C_VER_FIFO_EN, ioread8(bus->reg + I2C_VER))) {
		bus->fifo_use = true;
		npcm_i2c_select_bank(bus, I2C_BANK_0);
		val = ioread8(bus->reg + NPCM_I2CFIF_CTL);
		val |= NPCM_I2CFIF_CTL_FIFO_EN;
		iowrite8(val, bus->reg + NPCM_I2CFIF_CTL);
		npcm_i2c_select_bank(bus, I2C_BANK_1);
	} else {
		bus->fifo_use = false;
	}

	/* Configure I2C module clock frequency */
	ret = npcm_i2c_init_clk(bus, bus_freq_hz);
	if (ret) {
		dev_err(bus->dev, "npcm_i2c_init_clk failed\n");
		return ret;
	}

	/* Enable module (before configuring CTL1) */
	npcm_i2c_enable(bus);
	bus->state = I2C_IDLE;
	val = ioread8(bus->reg + NPCM_I2CCTL1);
	val = (val | NPCM_I2CCTL1_NMINTE) & ~NPCM_I2CCTL1_RWS;
	iowrite8(val, bus->reg + NPCM_I2CCTL1);

	npcm_i2c_int_enable(bus, true);

	npcm_i2c_reset(bus);

	return 0;
}

static int __npcm_i2c_init(struct npcm_i2c *bus, struct platform_device *pdev)
{
	u32 clk_freq_hz;
	int ret;

	/* Initialize the internal data structures */
	bus->state = I2C_DISABLE;
	bus->master_or_slave = I2C_SLAVE;
	bus->int_time_stamp = 0;
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	bus->slave = NULL;
#endif

	ret = device_property_read_u32(&pdev->dev, "clock-frequency",
				       &clk_freq_hz);
	if (ret) {
		dev_info(&pdev->dev, "Could not read clock-frequency property");
		clk_freq_hz = I2C_MAX_STANDARD_MODE_FREQ;
	}

	ret = npcm_i2c_init_module(bus, I2C_MASTER, clk_freq_hz);
	if (ret) {
		dev_err(&pdev->dev, "npcm_i2c_init_module failed\n");
		return ret;
	}

	return 0;
}

static irqreturn_t npcm_i2c_bus_irq(int irq, void *dev_id)
{
	struct npcm_i2c *bus = dev_id;

	if (npcm_i2c_is_master(bus))
		bus->master_or_slave = I2C_MASTER;

	if (bus->master_or_slave == I2C_MASTER) {
		bus->int_time_stamp = jiffies;
		if (!npcm_i2c_int_master_handler(bus))
			return IRQ_HANDLED;
	}
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	if (bus->slave) {
		bus->master_or_slave = I2C_SLAVE;
		return npcm_i2c_int_slave_handler(bus);
	}
#endif
	return IRQ_NONE;
}

static bool npcm_i2c_master_start_xmit(struct npcm_i2c *bus,
				       u8 slave_addr, u16 nwrite, u16 nread,
				       u8 *write_data, u8 *read_data,
				       bool use_PEC, bool use_read_block)
{
	if (bus->state != I2C_IDLE) {
		bus->cmd_err = -EBUSY;
		return false;
	}
	bus->dest_addr = slave_addr << 1;
	bus->wr_buf = write_data;
	bus->wr_size = nwrite;
	bus->wr_ind = 0;
	bus->rd_buf = read_data;
	bus->rd_size = nread;
	bus->rd_ind = 0;
	bus->PEC_use = 0;

	/* for tx PEC is appended to buffer from i2c IF. PEC flag is ignored */
	if (nread)
		bus->PEC_use = use_PEC;

	bus->read_block_use = use_read_block;
	if (nread && !nwrite)
		bus->operation = I2C_READ_OPER;
	else
		bus->operation = I2C_WRITE_OPER;
	if (bus->fifo_use) {
		u8 i2cfif_cts;

		npcm_i2c_select_bank(bus, I2C_BANK_1);
		/* clear FIFO and relevant status bits. */
		i2cfif_cts = ioread8(bus->reg + NPCM_I2CFIF_CTS);
		i2cfif_cts &= ~NPCM_I2CFIF_CTS_SLVRSTR;
		i2cfif_cts |= NPCM_I2CFIF_CTS_CLR_FIFO;
		iowrite8(i2cfif_cts, bus->reg + NPCM_I2CFIF_CTS);
	}

	bus->state = I2C_IDLE;
	npcm_i2c_stall_after_start(bus, true);
	npcm_i2c_master_start(bus);
	return true;
}

static int npcm_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
				int num)
{
	struct npcm_i2c *bus = container_of(adap, struct npcm_i2c, adap);
	struct i2c_msg *msg0, *msg1;
	unsigned long time_left, flags;
	u16 nwrite, nread;
	u8 *write_data, *read_data;
	u8 slave_addr;
	int timeout;
	int ret = 0;
	bool read_block = false;
	bool read_PEC = false;
	u8 bus_busy;
	unsigned long timeout_usec;

	if (bus->state == I2C_DISABLE) {
		dev_err(bus->dev, "I2C%d module is disabled", bus->num);
		return -EINVAL;
	}

	msg0 = &msgs[0];
	slave_addr = msg0->addr;
	if (msg0->flags & I2C_M_RD) { /* read */
		nwrite = 0;
		write_data = NULL;
		read_data = msg0->buf;
		if (msg0->flags & I2C_M_RECV_LEN) {
			nread = 1;
			read_block = true;
			if (msg0->flags & I2C_CLIENT_PEC)
				read_PEC = true;
		} else {
			nread = msg0->len;
		}
	} else { /* write */
		nwrite = msg0->len;
		write_data = msg0->buf;
		nread = 0;
		read_data = NULL;
		if (num == 2) {
			msg1 = &msgs[1];
			read_data = msg1->buf;
			if (msg1->flags & I2C_M_RECV_LEN) {
				nread = 1;
				read_block = true;
				if (msg1->flags & I2C_CLIENT_PEC)
					read_PEC = true;
			} else {
				nread = msg1->len;
				read_block = false;
			}
		}
	}

	/*
	 * Adaptive TimeOut: estimated time in usec + 100% margin:
	 * 2: double the timeout for clock stretching case
	 * 9: bits per transaction (including the ack/nack)
	 */
	timeout_usec = (2 * 9 * USEC_PER_SEC / bus->bus_freq) * (2 + nread + nwrite);
	timeout = max(msecs_to_jiffies(35), usecs_to_jiffies(timeout_usec));
	if (nwrite >= 32 * 1024 || nread >= 32 * 1024) {
		dev_err(bus->dev, "i2c%d buffer too big\n", bus->num);
		return -EINVAL;
	}

	time_left = jiffies + msecs_to_jiffies(DEFAULT_STALL_COUNT) + 1;
	do {
		/*
		 * we must clear slave address immediately when the bus is not
		 * busy, so we spinlock it, but we don't keep the lock for the
		 * entire while since it is too long.
		 */
		spin_lock_irqsave(&bus->lock, flags);
		bus_busy = ioread8(bus->reg + NPCM_I2CCST) & NPCM_I2CCST_BB;
#if IS_ENABLED(CONFIG_I2C_SLAVE)
		if (!bus_busy && bus->slave)
			iowrite8((bus->slave->addr & 0x7F),
				 bus->reg + NPCM_I2CADDR1);
#endif
		spin_unlock_irqrestore(&bus->lock, flags);

	} while (time_is_after_jiffies(time_left) && bus_busy);

	if (bus_busy) {
		iowrite8(NPCM_I2CCST_BB, bus->reg + NPCM_I2CCST);
		npcm_i2c_reset(bus);
		i2c_recover_bus(adap);
		return -EAGAIN;
	}

	npcm_i2c_init_params(bus);
	bus->dest_addr = slave_addr;
	bus->msgs = msgs;
	bus->msgs_num = num;
	bus->cmd_err = 0;
	bus->read_block_use = read_block;

	reinit_completion(&bus->cmd_complete);
	if (!npcm_i2c_master_start_xmit(bus, slave_addr, nwrite, nread,
					write_data, read_data, read_PEC,
					read_block))
		ret = -EBUSY;

	if (ret != -EBUSY) {
		time_left = wait_for_completion_timeout(&bus->cmd_complete,
							timeout);

		if (time_left == 0) {
			if (bus->timeout_cnt < ULLONG_MAX)
				bus->timeout_cnt++;
			if (bus->master_or_slave == I2C_MASTER) {
				i2c_recover_bus(adap);
				bus->cmd_err = -EIO;
				bus->state = I2C_IDLE;
			}
		}
	}
	ret = bus->cmd_err;

	/* if there was BER, check if need to recover the bus: */
	if (bus->cmd_err == -EAGAIN)
		ret = i2c_recover_bus(adap);

	/*
	 * After any type of error, check if LAST bit is still set,
	 * due to a HW issue.
	 * It cannot be cleared without resetting the module.
	 */
	if (bus->cmd_err &&
	    (NPCM_I2CRXF_CTL_LAST_PEC & ioread8(bus->reg + NPCM_I2CRXF_CTL)))
		npcm_i2c_reset(bus);

#if IS_ENABLED(CONFIG_I2C_SLAVE)
	/* reenable slave if it was enabled */
	if (bus->slave)
		iowrite8((bus->slave->addr & 0x7F) | NPCM_I2CADDR_SAEN,
			 bus->reg + NPCM_I2CADDR1);
#endif
	return bus->cmd_err;
}

static u32 npcm_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C |
	       I2C_FUNC_SMBUS_EMUL |
	       I2C_FUNC_SMBUS_BLOCK_DATA |
	       I2C_FUNC_SMBUS_PEC |
	       I2C_FUNC_SLAVE;
}

static const struct i2c_adapter_quirks npcm_i2c_quirks = {
	.max_read_len = 32768,
	.max_write_len = 32768,
	.flags = I2C_AQ_COMB_WRITE_THEN_READ,
};

static const struct i2c_algorithm npcm_i2c_algo = {
	.master_xfer = npcm_i2c_master_xfer,
	.functionality = npcm_i2c_functionality,
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	.reg_slave	= npcm_i2c_reg_slave,
	.unreg_slave	= npcm_i2c_unreg_slave,
#endif
};

/* i2c debugfs directory: used to keep health monitor of i2c devices */
static struct dentry *npcm_i2c_debugfs_dir;

static void npcm_i2c_init_debugfs(struct platform_device *pdev,
				  struct npcm_i2c *bus)
{
	struct dentry *d;

	if (!npcm_i2c_debugfs_dir)
		return;
	d = debugfs_create_dir(dev_name(&pdev->dev), npcm_i2c_debugfs_dir);
	if (IS_ERR_OR_NULL(d))
		return;
	debugfs_create_u64("ber_cnt", 0444, d, &bus->ber_cnt);
	debugfs_create_u64("nack_cnt", 0444, d, &bus->nack_cnt);
	debugfs_create_u64("rec_succ_cnt", 0444, d, &bus->rec_succ_cnt);
	debugfs_create_u64("rec_fail_cnt", 0444, d, &bus->rec_fail_cnt);
	debugfs_create_u64("timeout_cnt", 0444, d, &bus->timeout_cnt);

	bus->debugfs = d;
}

static int npcm_i2c_probe_bus(struct platform_device *pdev)
{
	struct npcm_i2c *bus;
	struct i2c_adapter *adap;
	struct clk *i2c_clk;
	static struct regmap *gcr_regmap;
	static struct regmap *clk_regmap;
	int irq;
	int ret;

	bus = devm_kzalloc(&pdev->dev, sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return -ENOMEM;

	bus->dev = &pdev->dev;

	bus->num = of_alias_get_id(pdev->dev.of_node, "i2c");
	/* core clk must be acquired to calculate module timing settings */
	i2c_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2c_clk))
		return PTR_ERR(i2c_clk);
	bus->apb_clk = clk_get_rate(i2c_clk);

	gcr_regmap = syscon_regmap_lookup_by_compatible("nuvoton,npcm750-gcr");
	if (IS_ERR(gcr_regmap))
		return PTR_ERR(gcr_regmap);
	regmap_write(gcr_regmap, NPCM_I2CSEGCTL, NPCM_I2CSEGCTL_INIT_VAL);

	clk_regmap = syscon_regmap_lookup_by_compatible("nuvoton,npcm750-clk");
	if (IS_ERR(clk_regmap))
		return PTR_ERR(clk_regmap);

	bus->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(bus->reg))
		return PTR_ERR(bus->reg);

	spin_lock_init(&bus->lock);
	init_completion(&bus->cmd_complete);

	adap = &bus->adap;
	adap->owner = THIS_MODULE;
	adap->retries = 3;
	adap->timeout = HZ;
	adap->algo = &npcm_i2c_algo;
	adap->quirks = &npcm_i2c_quirks;
	adap->algo_data = bus;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;
	adap->nr = pdev->id;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(bus->dev, irq, npcm_i2c_bus_irq, 0,
			       dev_name(bus->dev), bus);
	if (ret)
		return ret;

	ret = __npcm_i2c_init(bus, pdev);
	if (ret)
		return ret;

	npcm_i2c_recovery_init(adap);

	i2c_set_adapdata(adap, bus);

	snprintf(bus->adap.name, sizeof(bus->adap.name), "npcm_i2c_%d",
		 bus->num);
	ret = i2c_add_numbered_adapter(&bus->adap);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, bus);
	npcm_i2c_init_debugfs(pdev, bus);
	return 0;
}

static int npcm_i2c_remove_bus(struct platform_device *pdev)
{
	unsigned long lock_flags;
	struct npcm_i2c *bus = platform_get_drvdata(pdev);

	debugfs_remove_recursive(bus->debugfs);
	spin_lock_irqsave(&bus->lock, lock_flags);
	npcm_i2c_disable(bus);
	spin_unlock_irqrestore(&bus->lock, lock_flags);
	i2c_del_adapter(&bus->adap);
	return 0;
}

static const struct of_device_id npcm_i2c_bus_of_table[] = {
	{ .compatible = "nuvoton,npcm750-i2c", },
	{}
};
MODULE_DEVICE_TABLE(of, npcm_i2c_bus_of_table);

static struct platform_driver npcm_i2c_bus_driver = {
	.probe = npcm_i2c_probe_bus,
	.remove = npcm_i2c_remove_bus,
	.driver = {
		.name = "nuvoton-i2c",
		.of_match_table = npcm_i2c_bus_of_table,
	}
};

static int __init npcm_i2c_init(void)
{
	npcm_i2c_debugfs_dir = debugfs_create_dir("npcm_i2c", NULL);
	platform_driver_register(&npcm_i2c_bus_driver);
	return 0;
}
module_init(npcm_i2c_init);

static void __exit npcm_i2c_exit(void)
{
	platform_driver_unregister(&npcm_i2c_bus_driver);
	debugfs_remove_recursive(npcm_i2c_debugfs_dir);
}
module_exit(npcm_i2c_exit);

MODULE_AUTHOR("Avi Fishman <avi.fishman@gmail.com>");
MODULE_AUTHOR("Tali Perry <tali.perry@nuvoton.com>");
MODULE_AUTHOR("Tyrone Ting <kfting@nuvoton.com>");
MODULE_DESCRIPTION("Nuvoton I2C Bus Driver");
MODULE_LICENSE("GPL v2");
