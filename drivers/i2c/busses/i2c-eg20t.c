/*
 * Copyright (C) 2011 LAPIS Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/slab.h>

#define PCH_EVENT_SET	0	/* I2C Interrupt Event Set Status */
#define PCH_EVENT_NONE	1	/* I2C Interrupt Event Clear Status */
#define PCH_MAX_CLK		100000	/* Maximum Clock speed in MHz */
#define PCH_BUFFER_MODE_ENABLE	0x0002	/* flag for Buffer mode enable */
#define PCH_EEPROM_SW_RST_MODE_ENABLE	0x0008	/* EEPROM SW RST enable flag */

#define PCH_I2CSADR	0x00	/* I2C slave address register */
#define PCH_I2CCTL	0x04	/* I2C control register */
#define PCH_I2CSR	0x08	/* I2C status register */
#define PCH_I2CDR	0x0C	/* I2C data register */
#define PCH_I2CMON	0x10	/* I2C bus monitor register */
#define PCH_I2CBC	0x14	/* I2C bus transfer rate setup counter */
#define PCH_I2CMOD	0x18	/* I2C mode register */
#define PCH_I2CBUFSLV	0x1C	/* I2C buffer mode slave address register */
#define PCH_I2CBUFSUB	0x20	/* I2C buffer mode subaddress register */
#define PCH_I2CBUFFOR	0x24	/* I2C buffer mode format register */
#define PCH_I2CBUFCTL	0x28	/* I2C buffer mode control register */
#define PCH_I2CBUFMSK	0x2C	/* I2C buffer mode interrupt mask register */
#define PCH_I2CBUFSTA	0x30	/* I2C buffer mode status register */
#define PCH_I2CBUFLEV	0x34	/* I2C buffer mode level register */
#define PCH_I2CESRFOR	0x38	/* EEPROM software reset mode format register */
#define PCH_I2CESRCTL	0x3C	/* EEPROM software reset mode ctrl register */
#define PCH_I2CESRMSK	0x40	/* EEPROM software reset mode */
#define PCH_I2CESRSTA	0x44	/* EEPROM software reset mode status register */
#define PCH_I2CTMR	0x48	/* I2C timer register */
#define PCH_I2CSRST	0xFC	/* I2C reset register */
#define PCH_I2CNF	0xF8	/* I2C noise filter register */

#define BUS_IDLE_TIMEOUT	20
#define PCH_I2CCTL_I2CMEN	0x0080
#define TEN_BIT_ADDR_DEFAULT	0xF000
#define TEN_BIT_ADDR_MASK	0xF0
#define PCH_START		0x0020
#define PCH_RESTART		0x0004
#define PCH_ESR_START		0x0001
#define PCH_BUFF_START		0x1
#define PCH_REPSTART		0x0004
#define PCH_ACK			0x0008
#define PCH_GETACK		0x0001
#define CLR_REG			0x0
#define I2C_RD			0x1
#define I2CMCF_BIT		0x0080
#define I2CMIF_BIT		0x0002
#define I2CMAL_BIT		0x0010
#define I2CBMFI_BIT		0x0001
#define I2CBMAL_BIT		0x0002
#define I2CBMNA_BIT		0x0004
#define I2CBMTO_BIT		0x0008
#define I2CBMIS_BIT		0x0010
#define I2CESRFI_BIT		0X0001
#define I2CESRTO_BIT		0x0002
#define I2CESRFIIE_BIT		0x1
#define I2CESRTOIE_BIT		0x2
#define I2CBMDZ_BIT		0x0040
#define I2CBMAG_BIT		0x0020
#define I2CMBB_BIT		0x0020
#define BUFFER_MODE_MASK	(I2CBMFI_BIT | I2CBMAL_BIT | I2CBMNA_BIT | \
				I2CBMTO_BIT | I2CBMIS_BIT)
#define I2C_ADDR_MSK		0xFF
#define I2C_MSB_2B_MSK		0x300
#define FAST_MODE_CLK		400
#define FAST_MODE_EN		0x0001
#define SUB_ADDR_LEN_MAX	4
#define BUF_LEN_MAX		32
#define PCH_BUFFER_MODE		0x1
#define EEPROM_SW_RST_MODE	0x0002
#define NORMAL_INTR_ENBL	0x0300
#define EEPROM_RST_INTR_ENBL	(I2CESRFIIE_BIT | I2CESRTOIE_BIT)
#define EEPROM_RST_INTR_DISBL	0x0
#define BUFFER_MODE_INTR_ENBL	0x001F
#define BUFFER_MODE_INTR_DISBL	0x0
#define NORMAL_MODE		0x0
#define BUFFER_MODE		0x1
#define EEPROM_SR_MODE		0x2
#define I2C_TX_MODE		0x0010
#define PCH_BUF_TX		0xFFF7
#define PCH_BUF_RD		0x0008
#define I2C_ERROR_MASK	(I2CESRTO_EVENT | I2CBMIS_EVENT | I2CBMTO_EVENT | \
			I2CBMNA_EVENT | I2CBMAL_EVENT | I2CMAL_EVENT)
#define I2CMAL_EVENT		0x0001
#define I2CMCF_EVENT		0x0002
#define I2CBMFI_EVENT		0x0004
#define I2CBMAL_EVENT		0x0008
#define I2CBMNA_EVENT		0x0010
#define I2CBMTO_EVENT		0x0020
#define I2CBMIS_EVENT		0x0040
#define I2CESRFI_EVENT		0x0080
#define I2CESRTO_EVENT		0x0100
#define PCI_DEVICE_ID_PCH_I2C	0x8817

#define pch_dbg(adap, fmt, arg...)  \
	dev_dbg(adap->pch_adapter.dev.parent, "%s :" fmt, __func__, ##arg)

#define pch_err(adap, fmt, arg...)  \
	dev_err(adap->pch_adapter.dev.parent, "%s :" fmt, __func__, ##arg)

#define pch_pci_err(pdev, fmt, arg...)  \
	dev_err(&pdev->dev, "%s :" fmt, __func__, ##arg)

#define pch_pci_dbg(pdev, fmt, arg...)  \
	dev_dbg(&pdev->dev, "%s :" fmt, __func__, ##arg)

/*
Set the number of I2C instance max
Intel EG20T PCH :		1ch
LAPIS Semiconductor ML7213 IOH :	2ch
LAPIS Semiconductor ML7831 IOH :	1ch
*/
#define PCH_I2C_MAX_DEV			2

/**
 * struct i2c_algo_pch_data - for I2C driver functionalities
 * @pch_adapter:		stores the reference to i2c_adapter structure
 * @p_adapter_info:		stores the reference to adapter_info structure
 * @pch_base_address:		specifies the remapped base address
 * @pch_buff_mode_en:		specifies if buffer mode is enabled
 * @pch_event_flag:		specifies occurrence of interrupt events
 * @pch_i2c_xfer_in_progress:	specifies whether the transfer is completed
 */
struct i2c_algo_pch_data {
	struct i2c_adapter pch_adapter;
	struct adapter_info *p_adapter_info;
	void __iomem *pch_base_address;
	int pch_buff_mode_en;
	u32 pch_event_flag;
	bool pch_i2c_xfer_in_progress;
};

/**
 * struct adapter_info - This structure holds the adapter information for the
			 PCH i2c controller
 * @pch_data:		stores a list of i2c_algo_pch_data
 * @pch_i2c_suspended:	specifies whether the system is suspended or not
 *			perhaps with more lines and words.
 * @ch_num:		specifies the number of i2c instance
 *
 * pch_data has as many elements as maximum I2C channels
 */
struct adapter_info {
	struct i2c_algo_pch_data pch_data[PCH_I2C_MAX_DEV];
	bool pch_i2c_suspended;
	int ch_num;
};


static int pch_i2c_speed = 100; /* I2C bus speed in Kbps */
static int pch_clk = 50000;	/* specifies I2C clock speed in KHz */
static wait_queue_head_t pch_event;
static DEFINE_MUTEX(pch_mutex);

/* Definition for ML7213 by LAPIS Semiconductor */
#define PCI_VENDOR_ID_ROHM		0x10DB
#define PCI_DEVICE_ID_ML7213_I2C	0x802D
#define PCI_DEVICE_ID_ML7223_I2C	0x8010
#define PCI_DEVICE_ID_ML7831_I2C	0x8817

static DEFINE_PCI_DEVICE_TABLE(pch_pcidev_id) = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_PCH_I2C),   1, },
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7213_I2C), 2, },
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7223_I2C), 1, },
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7831_I2C), 1, },
	{0,}
};

static irqreturn_t pch_i2c_handler(int irq, void *pData);

static inline void pch_setbit(void __iomem *addr, u32 offset, u32 bitmask)
{
	u32 val;
	val = ioread32(addr + offset);
	val |= bitmask;
	iowrite32(val, addr + offset);
}

static inline void pch_clrbit(void __iomem *addr, u32 offset, u32 bitmask)
{
	u32 val;
	val = ioread32(addr + offset);
	val &= (~bitmask);
	iowrite32(val, addr + offset);
}

/**
 * pch_i2c_init() - hardware initialization of I2C module
 * @adap:	Pointer to struct i2c_algo_pch_data.
 */
static void pch_i2c_init(struct i2c_algo_pch_data *adap)
{
	void __iomem *p = adap->pch_base_address;
	u32 pch_i2cbc;
	u32 pch_i2ctmr;
	u32 reg_value;

	/* reset I2C controller */
	iowrite32(0x01, p + PCH_I2CSRST);
	msleep(20);
	iowrite32(0x0, p + PCH_I2CSRST);

	/* Initialize I2C registers */
	iowrite32(0x21, p + PCH_I2CNF);

	pch_setbit(adap->pch_base_address, PCH_I2CCTL, PCH_I2CCTL_I2CMEN);

	if (pch_i2c_speed != 400)
		pch_i2c_speed = 100;

	reg_value = PCH_I2CCTL_I2CMEN;
	if (pch_i2c_speed == FAST_MODE_CLK) {
		reg_value |= FAST_MODE_EN;
		pch_dbg(adap, "Fast mode enabled\n");
	}

	if (pch_clk > PCH_MAX_CLK)
		pch_clk = 62500;

	pch_i2cbc = (pch_clk + (pch_i2c_speed * 4)) / (pch_i2c_speed * 8);
	/* Set transfer speed in I2CBC */
	iowrite32(pch_i2cbc, p + PCH_I2CBC);

	pch_i2ctmr = (pch_clk) / 8;
	iowrite32(pch_i2ctmr, p + PCH_I2CTMR);

	reg_value |= NORMAL_INTR_ENBL;	/* Enable interrupts in normal mode */
	iowrite32(reg_value, p + PCH_I2CCTL);

	pch_dbg(adap,
		"I2CCTL=%x pch_i2cbc=%x pch_i2ctmr=%x Enable interrupts\n",
		ioread32(p + PCH_I2CCTL), pch_i2cbc, pch_i2ctmr);

	init_waitqueue_head(&pch_event);
}

static inline bool ktime_lt(const ktime_t cmp1, const ktime_t cmp2)
{
	return cmp1.tv64 < cmp2.tv64;
}

/**
 * pch_i2c_wait_for_bus_idle() - check the status of bus.
 * @adap:	Pointer to struct i2c_algo_pch_data.
 * @timeout:	waiting time counter (ms).
 */
static s32 pch_i2c_wait_for_bus_idle(struct i2c_algo_pch_data *adap,
				     s32 timeout)
{
	void __iomem *p = adap->pch_base_address;
	int schedule = 0;
	unsigned long end = jiffies + msecs_to_jiffies(timeout);

	while (ioread32(p + PCH_I2CSR) & I2CMBB_BIT) {
		if (time_after(jiffies, end)) {
			pch_dbg(adap, "I2CSR = %x\n", ioread32(p + PCH_I2CSR));
			pch_err(adap, "%s: Timeout Error.return%d\n",
					__func__, -ETIME);
			pch_i2c_init(adap);

			return -ETIME;
		}

		if (!schedule)
			/* Retry after some usecs */
			udelay(5);
		else
			/* Wait a bit more without consuming CPU */
			usleep_range(20, 1000);

		schedule = 1;
	}

	return 0;
}

/**
 * pch_i2c_start() - Generate I2C start condition in normal mode.
 * @adap:	Pointer to struct i2c_algo_pch_data.
 *
 * Generate I2C start condition in normal mode by setting I2CCTL.I2CMSTA to 1.
 */
static void pch_i2c_start(struct i2c_algo_pch_data *adap)
{
	void __iomem *p = adap->pch_base_address;
	pch_dbg(adap, "I2CCTL = %x\n", ioread32(p + PCH_I2CCTL));
	pch_setbit(adap->pch_base_address, PCH_I2CCTL, PCH_START);
}

/**
 * pch_i2c_getack() - to confirm ACK/NACK
 * @adap:	Pointer to struct i2c_algo_pch_data.
 */
static s32 pch_i2c_getack(struct i2c_algo_pch_data *adap)
{
	u32 reg_val;
	void __iomem *p = adap->pch_base_address;
	reg_val = ioread32(p + PCH_I2CSR) & PCH_GETACK;

	if (reg_val != 0) {
		pch_err(adap, "return%d\n", -EPROTO);
		return -EPROTO;
	}

	return 0;
}

/**
 * pch_i2c_stop() - generate stop condition in normal mode.
 * @adap:	Pointer to struct i2c_algo_pch_data.
 */
static void pch_i2c_stop(struct i2c_algo_pch_data *adap)
{
	void __iomem *p = adap->pch_base_address;
	pch_dbg(adap, "I2CCTL = %x\n", ioread32(p + PCH_I2CCTL));
	/* clear the start bit */
	pch_clrbit(adap->pch_base_address, PCH_I2CCTL, PCH_START);
}

static int pch_i2c_wait_for_check_xfer(struct i2c_algo_pch_data *adap)
{
	long ret;

	ret = wait_event_timeout(pch_event,
			(adap->pch_event_flag != 0), msecs_to_jiffies(1000));
	if (!ret) {
		pch_err(adap, "%s:wait-event timeout\n", __func__);
		adap->pch_event_flag = 0;
		pch_i2c_stop(adap);
		pch_i2c_init(adap);
		return -ETIMEDOUT;
	}

	if (adap->pch_event_flag & I2C_ERROR_MASK) {
		pch_err(adap, "Lost Arbitration\n");
		adap->pch_event_flag = 0;
		pch_clrbit(adap->pch_base_address, PCH_I2CSR, I2CMAL_BIT);
		pch_clrbit(adap->pch_base_address, PCH_I2CSR, I2CMIF_BIT);
		pch_i2c_init(adap);
		return -EAGAIN;
	}

	adap->pch_event_flag = 0;

	if (pch_i2c_getack(adap)) {
		pch_dbg(adap, "Receive NACK for slave address"
			"setting\n");
		return -EIO;
	}

	return 0;
}

/**
 * pch_i2c_repstart() - generate repeated start condition in normal mode
 * @adap:	Pointer to struct i2c_algo_pch_data.
 */
static void pch_i2c_repstart(struct i2c_algo_pch_data *adap)
{
	void __iomem *p = adap->pch_base_address;
	pch_dbg(adap, "I2CCTL = %x\n", ioread32(p + PCH_I2CCTL));
	pch_setbit(adap->pch_base_address, PCH_I2CCTL, PCH_REPSTART);
}

/**
 * pch_i2c_writebytes() - write data to I2C bus in normal mode
 * @i2c_adap:	Pointer to the struct i2c_adapter.
 * @last:	specifies whether last message or not.
 *		In the case of compound mode it will be 1 for last message,
 *		otherwise 0.
 * @first:	specifies whether first message or not.
 *		1 for first message otherwise 0.
 */
static s32 pch_i2c_writebytes(struct i2c_adapter *i2c_adap,
			      struct i2c_msg *msgs, u32 last, u32 first)
{
	struct i2c_algo_pch_data *adap = i2c_adap->algo_data;
	u8 *buf;
	u32 length;
	u32 addr;
	u32 addr_2_msb;
	u32 addr_8_lsb;
	s32 wrcount;
	s32 rtn;
	void __iomem *p = adap->pch_base_address;

	length = msgs->len;
	buf = msgs->buf;
	addr = msgs->addr;

	/* enable master tx */
	pch_setbit(adap->pch_base_address, PCH_I2CCTL, I2C_TX_MODE);

	pch_dbg(adap, "I2CCTL = %x msgs->len = %d\n", ioread32(p + PCH_I2CCTL),
		length);

	if (first) {
		if (pch_i2c_wait_for_bus_idle(adap, BUS_IDLE_TIMEOUT) == -ETIME)
			return -ETIME;
	}

	if (msgs->flags & I2C_M_TEN) {
		addr_2_msb = ((addr & I2C_MSB_2B_MSK) >> 7) & 0x06;
		iowrite32(addr_2_msb | TEN_BIT_ADDR_MASK, p + PCH_I2CDR);
		if (first)
			pch_i2c_start(adap);

		rtn = pch_i2c_wait_for_check_xfer(adap);
		if (rtn)
			return rtn;

		addr_8_lsb = (addr & I2C_ADDR_MSK);
		iowrite32(addr_8_lsb, p + PCH_I2CDR);
	} else {
		/* set 7 bit slave address and R/W bit as 0 */
		iowrite32(addr << 1, p + PCH_I2CDR);
		if (first)
			pch_i2c_start(adap);
	}

	rtn = pch_i2c_wait_for_check_xfer(adap);
	if (rtn)
		return rtn;

	for (wrcount = 0; wrcount < length; ++wrcount) {
		/* write buffer value to I2C data register */
		iowrite32(buf[wrcount], p + PCH_I2CDR);
		pch_dbg(adap, "writing %x to Data register\n", buf[wrcount]);

		rtn = pch_i2c_wait_for_check_xfer(adap);
		if (rtn)
			return rtn;

		pch_clrbit(adap->pch_base_address, PCH_I2CSR, I2CMCF_BIT);
		pch_clrbit(adap->pch_base_address, PCH_I2CSR, I2CMIF_BIT);
	}

	/* check if this is the last message */
	if (last)
		pch_i2c_stop(adap);
	else
		pch_i2c_repstart(adap);

	pch_dbg(adap, "return=%d\n", wrcount);

	return wrcount;
}

/**
 * pch_i2c_sendack() - send ACK
 * @adap:	Pointer to struct i2c_algo_pch_data.
 */
static void pch_i2c_sendack(struct i2c_algo_pch_data *adap)
{
	void __iomem *p = adap->pch_base_address;
	pch_dbg(adap, "I2CCTL = %x\n", ioread32(p + PCH_I2CCTL));
	pch_clrbit(adap->pch_base_address, PCH_I2CCTL, PCH_ACK);
}

/**
 * pch_i2c_sendnack() - send NACK
 * @adap:	Pointer to struct i2c_algo_pch_data.
 */
static void pch_i2c_sendnack(struct i2c_algo_pch_data *adap)
{
	void __iomem *p = adap->pch_base_address;
	pch_dbg(adap, "I2CCTL = %x\n", ioread32(p + PCH_I2CCTL));
	pch_setbit(adap->pch_base_address, PCH_I2CCTL, PCH_ACK);
}

/**
 * pch_i2c_restart() - Generate I2C restart condition in normal mode.
 * @adap:	Pointer to struct i2c_algo_pch_data.
 *
 * Generate I2C restart condition in normal mode by setting I2CCTL.I2CRSTA.
 */
static void pch_i2c_restart(struct i2c_algo_pch_data *adap)
{
	void __iomem *p = adap->pch_base_address;
	pch_dbg(adap, "I2CCTL = %x\n", ioread32(p + PCH_I2CCTL));
	pch_setbit(adap->pch_base_address, PCH_I2CCTL, PCH_RESTART);
}

/**
 * pch_i2c_readbytes() - read data  from I2C bus in normal mode.
 * @i2c_adap:	Pointer to the struct i2c_adapter.
 * @msgs:	Pointer to i2c_msg structure.
 * @last:	specifies whether last message or not.
 * @first:	specifies whether first message or not.
 */
static s32 pch_i2c_readbytes(struct i2c_adapter *i2c_adap, struct i2c_msg *msgs,
			     u32 last, u32 first)
{
	struct i2c_algo_pch_data *adap = i2c_adap->algo_data;

	u8 *buf;
	u32 count;
	u32 length;
	u32 addr;
	u32 addr_2_msb;
	u32 addr_8_lsb;
	void __iomem *p = adap->pch_base_address;
	s32 rtn;

	length = msgs->len;
	buf = msgs->buf;
	addr = msgs->addr;

	/* enable master reception */
	pch_clrbit(adap->pch_base_address, PCH_I2CCTL, I2C_TX_MODE);

	if (first) {
		if (pch_i2c_wait_for_bus_idle(adap, BUS_IDLE_TIMEOUT) == -ETIME)
			return -ETIME;
	}

	if (msgs->flags & I2C_M_TEN) {
		addr_2_msb = ((addr & I2C_MSB_2B_MSK) >> 7);
		iowrite32(addr_2_msb | TEN_BIT_ADDR_MASK, p + PCH_I2CDR);
		if (first)
			pch_i2c_start(adap);

		rtn = pch_i2c_wait_for_check_xfer(adap);
		if (rtn)
			return rtn;

		addr_8_lsb = (addr & I2C_ADDR_MSK);
		iowrite32(addr_8_lsb, p + PCH_I2CDR);

		pch_i2c_restart(adap);

		rtn = pch_i2c_wait_for_check_xfer(adap);
		if (rtn)
			return rtn;

		addr_2_msb |= I2C_RD;
		iowrite32(addr_2_msb | TEN_BIT_ADDR_MASK, p + PCH_I2CDR);
	} else {
		/* 7 address bits + R/W bit */
		addr = (((addr) << 1) | (I2C_RD));
		iowrite32(addr, p + PCH_I2CDR);
	}

	/* check if it is the first message */
	if (first)
		pch_i2c_start(adap);

	rtn = pch_i2c_wait_for_check_xfer(adap);
	if (rtn)
		return rtn;

	if (length == 0) {
		pch_i2c_stop(adap);
		ioread32(p + PCH_I2CDR); /* Dummy read needs */

		count = length;
	} else {
		int read_index;
		int loop;
		pch_i2c_sendack(adap);

		/* Dummy read */
		for (loop = 1, read_index = 0; loop < length; loop++) {
			buf[read_index] = ioread32(p + PCH_I2CDR);

			if (loop != 1)
				read_index++;

			rtn = pch_i2c_wait_for_check_xfer(adap);
			if (rtn)
				return rtn;
		}	/* end for */

		pch_i2c_sendnack(adap);

		buf[read_index] = ioread32(p + PCH_I2CDR); /* Read final - 1 */

		if (length != 1)
			read_index++;

		rtn = pch_i2c_wait_for_check_xfer(adap);
		if (rtn)
			return rtn;

		if (last)
			pch_i2c_stop(adap);
		else
			pch_i2c_repstart(adap);

		buf[read_index++] = ioread32(p + PCH_I2CDR); /* Read Final */
		count = read_index;
	}

	return count;
}

/**
 * pch_i2c_cb() - Interrupt handler Call back function
 * @adap:	Pointer to struct i2c_algo_pch_data.
 */
static void pch_i2c_cb(struct i2c_algo_pch_data *adap)
{
	u32 sts;
	void __iomem *p = adap->pch_base_address;

	sts = ioread32(p + PCH_I2CSR);
	sts &= (I2CMAL_BIT | I2CMCF_BIT | I2CMIF_BIT);
	if (sts & I2CMAL_BIT)
		adap->pch_event_flag |= I2CMAL_EVENT;

	if (sts & I2CMCF_BIT)
		adap->pch_event_flag |= I2CMCF_EVENT;

	/* clear the applicable bits */
	pch_clrbit(adap->pch_base_address, PCH_I2CSR, sts);

	pch_dbg(adap, "PCH_I2CSR = %x\n", ioread32(p + PCH_I2CSR));

	wake_up(&pch_event);
}

/**
 * pch_i2c_handler() - interrupt handler for the PCH I2C controller
 * @irq:	irq number.
 * @pData:	cookie passed back to the handler function.
 */
static irqreturn_t pch_i2c_handler(int irq, void *pData)
{
	u32 reg_val;
	int flag;
	int i;
	struct adapter_info *adap_info = pData;
	void __iomem *p;
	u32 mode;

	for (i = 0, flag = 0; i < adap_info->ch_num; i++) {
		p = adap_info->pch_data[i].pch_base_address;
		mode = ioread32(p + PCH_I2CMOD);
		mode &= BUFFER_MODE | EEPROM_SR_MODE;
		if (mode != NORMAL_MODE) {
			pch_err(adap_info->pch_data,
				"I2C-%d mode(%d) is not supported\n", mode, i);
			continue;
		}
		reg_val = ioread32(p + PCH_I2CSR);
		if (reg_val & (I2CMAL_BIT | I2CMCF_BIT | I2CMIF_BIT)) {
			pch_i2c_cb(&adap_info->pch_data[i]);
			flag = 1;
		}
	}

	return flag ? IRQ_HANDLED : IRQ_NONE;
}

/**
 * pch_i2c_xfer() - Reading adnd writing data through I2C bus
 * @i2c_adap:	Pointer to the struct i2c_adapter.
 * @msgs:	Pointer to i2c_msg structure.
 * @num:	number of messages.
 */
static s32 pch_i2c_xfer(struct i2c_adapter *i2c_adap,
			struct i2c_msg *msgs, s32 num)
{
	struct i2c_msg *pmsg;
	u32 i = 0;
	u32 status;
	s32 ret;

	struct i2c_algo_pch_data *adap = i2c_adap->algo_data;

	ret = mutex_lock_interruptible(&pch_mutex);
	if (ret)
		return -ERESTARTSYS;

	if (adap->p_adapter_info->pch_i2c_suspended) {
		mutex_unlock(&pch_mutex);
		return -EBUSY;
	}

	pch_dbg(adap, "adap->p_adapter_info->pch_i2c_suspended is %d\n",
		adap->p_adapter_info->pch_i2c_suspended);
	/* transfer not completed */
	adap->pch_i2c_xfer_in_progress = true;

	for (i = 0; i < num && ret >= 0; i++) {
		pmsg = &msgs[i];
		pmsg->flags |= adap->pch_buff_mode_en;
		status = pmsg->flags;
		pch_dbg(adap,
			"After invoking I2C_MODE_SEL :flag= 0x%x\n", status);

		if ((status & (I2C_M_RD)) != false) {
			ret = pch_i2c_readbytes(i2c_adap, pmsg, (i + 1 == num),
						(i == 0));
		} else {
			ret = pch_i2c_writebytes(i2c_adap, pmsg, (i + 1 == num),
						 (i == 0));
		}
	}

	adap->pch_i2c_xfer_in_progress = false;	/* transfer completed */

	mutex_unlock(&pch_mutex);

	return (ret < 0) ? ret : num;
}

/**
 * pch_i2c_func() - return the functionality of the I2C driver
 * @adap:	Pointer to struct i2c_algo_pch_data.
 */
static u32 pch_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_10BIT_ADDR;
}

static struct i2c_algorithm pch_algorithm = {
	.master_xfer = pch_i2c_xfer,
	.functionality = pch_i2c_func
};

/**
 * pch_i2c_disbl_int() - Disable PCH I2C interrupts
 * @adap:	Pointer to struct i2c_algo_pch_data.
 */
static void pch_i2c_disbl_int(struct i2c_algo_pch_data *adap)
{
	void __iomem *p = adap->pch_base_address;

	pch_clrbit(adap->pch_base_address, PCH_I2CCTL, NORMAL_INTR_ENBL);

	iowrite32(EEPROM_RST_INTR_DISBL, p + PCH_I2CESRMSK);

	iowrite32(BUFFER_MODE_INTR_DISBL, p + PCH_I2CBUFMSK);
}

static int __devinit pch_i2c_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	void __iomem *base_addr;
	int ret;
	int i, j;
	struct adapter_info *adap_info;
	struct i2c_adapter *pch_adap;

	pch_pci_dbg(pdev, "Entered.\n");

	adap_info = kzalloc((sizeof(struct adapter_info)), GFP_KERNEL);
	if (adap_info == NULL) {
		pch_pci_err(pdev, "Memory allocation FAILED\n");
		return -ENOMEM;
	}

	ret = pci_enable_device(pdev);
	if (ret) {
		pch_pci_err(pdev, "pci_enable_device FAILED\n");
		goto err_pci_enable;
	}

	ret = pci_request_regions(pdev, KBUILD_MODNAME);
	if (ret) {
		pch_pci_err(pdev, "pci_request_regions FAILED\n");
		goto err_pci_req;
	}

	base_addr = pci_iomap(pdev, 1, 0);

	if (base_addr == NULL) {
		pch_pci_err(pdev, "pci_iomap FAILED\n");
		ret = -ENOMEM;
		goto err_pci_iomap;
	}

	/* Set the number of I2C channel instance */
	adap_info->ch_num = id->driver_data;

	ret = request_irq(pdev->irq, pch_i2c_handler, IRQF_SHARED,
		  KBUILD_MODNAME, adap_info);
	if (ret) {
		pch_pci_err(pdev, "request_irq FAILED\n");
		goto err_request_irq;
	}

	for (i = 0; i < adap_info->ch_num; i++) {
		pch_adap = &adap_info->pch_data[i].pch_adapter;
		adap_info->pch_i2c_suspended = false;

		adap_info->pch_data[i].p_adapter_info = adap_info;

		pch_adap->owner = THIS_MODULE;
		pch_adap->class = I2C_CLASS_HWMON;
		strcpy(pch_adap->name, KBUILD_MODNAME);
		pch_adap->algo = &pch_algorithm;
		pch_adap->algo_data = &adap_info->pch_data[i];

		/* base_addr + offset; */
		adap_info->pch_data[i].pch_base_address = base_addr + 0x100 * i;

		pch_adap->dev.parent = &pdev->dev;

		pch_i2c_init(&adap_info->pch_data[i]);

		pch_adap->nr = i;
		ret = i2c_add_numbered_adapter(pch_adap);
		if (ret) {
			pch_pci_err(pdev, "i2c_add_adapter[ch:%d] FAILED\n", i);
			goto err_add_adapter;
		}
	}

	pci_set_drvdata(pdev, adap_info);
	pch_pci_dbg(pdev, "returns %d.\n", ret);
	return 0;

err_add_adapter:
	for (j = 0; j < i; j++)
		i2c_del_adapter(&adap_info->pch_data[j].pch_adapter);
	free_irq(pdev->irq, adap_info);
err_request_irq:
	pci_iounmap(pdev, base_addr);
err_pci_iomap:
	pci_release_regions(pdev);
err_pci_req:
	pci_disable_device(pdev);
err_pci_enable:
	kfree(adap_info);
	return ret;
}

static void __devexit pch_i2c_remove(struct pci_dev *pdev)
{
	int i;
	struct adapter_info *adap_info = pci_get_drvdata(pdev);

	free_irq(pdev->irq, adap_info);

	for (i = 0; i < adap_info->ch_num; i++) {
		pch_i2c_disbl_int(&adap_info->pch_data[i]);
		i2c_del_adapter(&adap_info->pch_data[i].pch_adapter);
	}

	if (adap_info->pch_data[0].pch_base_address)
		pci_iounmap(pdev, adap_info->pch_data[0].pch_base_address);

	for (i = 0; i < adap_info->ch_num; i++)
		adap_info->pch_data[i].pch_base_address = NULL;

	pci_set_drvdata(pdev, NULL);

	pci_release_regions(pdev);

	pci_disable_device(pdev);
	kfree(adap_info);
}

#ifdef CONFIG_PM
static int pch_i2c_suspend(struct pci_dev *pdev, pm_message_t state)
{
	int ret;
	int i;
	struct adapter_info *adap_info = pci_get_drvdata(pdev);
	void __iomem *p = adap_info->pch_data[0].pch_base_address;

	adap_info->pch_i2c_suspended = true;

	for (i = 0; i < adap_info->ch_num; i++) {
		while ((adap_info->pch_data[i].pch_i2c_xfer_in_progress)) {
			/* Wait until all channel transfers are completed */
			msleep(20);
		}
	}

	/* Disable the i2c interrupts */
	for (i = 0; i < adap_info->ch_num; i++)
		pch_i2c_disbl_int(&adap_info->pch_data[i]);

	pch_pci_dbg(pdev, "I2CSR = %x I2CBUFSTA = %x I2CESRSTA = %x "
		"invoked function pch_i2c_disbl_int successfully\n",
		ioread32(p + PCH_I2CSR), ioread32(p + PCH_I2CBUFSTA),
		ioread32(p + PCH_I2CESRSTA));

	ret = pci_save_state(pdev);

	if (ret) {
		pch_pci_err(pdev, "pci_save_state\n");
		return ret;
	}

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int pch_i2c_resume(struct pci_dev *pdev)
{
	int i;
	struct adapter_info *adap_info = pci_get_drvdata(pdev);

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	if (pci_enable_device(pdev) < 0) {
		pch_pci_err(pdev, "pch_i2c_resume:pci_enable_device FAILED\n");
		return -EIO;
	}

	pci_enable_wake(pdev, PCI_D3hot, 0);

	for (i = 0; i < adap_info->ch_num; i++)
		pch_i2c_init(&adap_info->pch_data[i]);

	adap_info->pch_i2c_suspended = false;

	return 0;
}
#else
#define pch_i2c_suspend NULL
#define pch_i2c_resume NULL
#endif

static struct pci_driver pch_pcidriver = {
	.name = KBUILD_MODNAME,
	.id_table = pch_pcidev_id,
	.probe = pch_i2c_probe,
	.remove = __devexit_p(pch_i2c_remove),
	.suspend = pch_i2c_suspend,
	.resume = pch_i2c_resume
};

static int __init pch_pci_init(void)
{
	return pci_register_driver(&pch_pcidriver);
}
module_init(pch_pci_init);

static void __exit pch_pci_exit(void)
{
	pci_unregister_driver(&pch_pcidriver);
}
module_exit(pch_pci_exit);

MODULE_DESCRIPTION("Intel EG20T PCH/LAPIS Semico ML7213/ML7223/ML7831 IOH I2C");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tomoya MORINAGA. <tomoya.rohm@gmail.com>");
module_param(pch_i2c_speed, int, (S_IRUSR | S_IWUSR));
module_param(pch_clk, int, (S_IRUSR | S_IWUSR));
