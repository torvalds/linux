/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  i2c Support for Atmel's AT91 Two-Wire Interface (TWI)
 *
 *  Copyright (C) 2011 Weinmann Medical GmbH
 *  Author: Nikolaus Voss <n.voss@weinmann.de>
 *
 *  Evolved from original work by:
 *  Copyright (C) 2004 Rick Bronson
 *  Converted to 2.6 by Andrew Victor <andrew@sanpeople.com>
 *
 *  Borrowed heavily from original work by:
 *  Copyright (C) 2000 Philip Edelbrock <phil@stimpy.netroedge.com>
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/i2c.h>
#include <linux/platform_data/dma-atmel.h>
#include <linux/platform_device.h>

#define AT91_I2C_TIMEOUT	msecs_to_jiffies(100)	/* transfer timeout */
#define AT91_I2C_DMA_THRESHOLD	8			/* enable DMA if transfer size is bigger than this threshold */
#define AUTOSUSPEND_TIMEOUT		2000
#define AT91_I2C_MAX_ALT_CMD_DATA_SIZE	256

/* AT91 TWI register definitions */
#define	AT91_TWI_CR		0x0000	/* Control Register */
#define	AT91_TWI_START		BIT(0)	/* Send a Start Condition */
#define	AT91_TWI_STOP		BIT(1)	/* Send a Stop Condition */
#define	AT91_TWI_MSEN		BIT(2)	/* Master Transfer Enable */
#define	AT91_TWI_MSDIS		BIT(3)	/* Master Transfer Disable */
#define	AT91_TWI_SVEN		BIT(4)	/* Slave Transfer Enable */
#define	AT91_TWI_SVDIS		BIT(5)	/* Slave Transfer Disable */
#define	AT91_TWI_QUICK		BIT(6)	/* SMBus quick command */
#define	AT91_TWI_SWRST		BIT(7)	/* Software Reset */
#define	AT91_TWI_CLEAR		BIT(15) /* Bus clear command */
#define	AT91_TWI_ACMEN		BIT(16) /* Alternative Command Mode Enable */
#define	AT91_TWI_ACMDIS		BIT(17) /* Alternative Command Mode Disable */
#define	AT91_TWI_THRCLR		BIT(24) /* Transmit Holding Register Clear */
#define	AT91_TWI_RHRCLR		BIT(25) /* Receive Holding Register Clear */
#define	AT91_TWI_LOCKCLR	BIT(26) /* Lock Clear */
#define	AT91_TWI_FIFOEN		BIT(28) /* FIFO Enable */
#define	AT91_TWI_FIFODIS	BIT(29) /* FIFO Disable */

#define	AT91_TWI_MMR		0x0004	/* Master Mode Register */
#define	AT91_TWI_IADRSZ_1	0x0100	/* Internal Device Address Size */
#define	AT91_TWI_MREAD		BIT(12)	/* Master Read Direction */

#define	AT91_TWI_SMR		0x0008	/* Slave Mode Register */
#define	AT91_TWI_SMR_SADR_MAX	0x007f
#define	AT91_TWI_SMR_SADR(x)	(((x) & AT91_TWI_SMR_SADR_MAX) << 16)

#define	AT91_TWI_IADR		0x000c	/* Internal Address Register */

#define	AT91_TWI_CWGR		0x0010	/* Clock Waveform Generator Reg */
#define	AT91_TWI_CWGR_HOLD_MAX	0x1f
#define	AT91_TWI_CWGR_HOLD(x)	(((x) & AT91_TWI_CWGR_HOLD_MAX) << 24)

#define	AT91_TWI_SR		0x0020	/* Status Register */
#define	AT91_TWI_TXCOMP		BIT(0)	/* Transmission Complete */
#define	AT91_TWI_RXRDY		BIT(1)	/* Receive Holding Register Ready */
#define	AT91_TWI_TXRDY		BIT(2)	/* Transmit Holding Register Ready */
#define	AT91_TWI_SVREAD		BIT(3)	/* Slave Read */
#define	AT91_TWI_SVACC		BIT(4)	/* Slave Access */
#define	AT91_TWI_OVRE		BIT(6)	/* Overrun Error */
#define	AT91_TWI_UNRE		BIT(7)	/* Underrun Error */
#define	AT91_TWI_NACK		BIT(8)	/* Not Acknowledged */
#define	AT91_TWI_EOSACC		BIT(11)	/* End Of Slave Access */
#define	AT91_TWI_LOCK		BIT(23) /* TWI Lock due to Frame Errors */
#define	AT91_TWI_SCL		BIT(24) /* TWI SCL status */
#define	AT91_TWI_SDA		BIT(25) /* TWI SDA status */

#define	AT91_TWI_INT_MASK \
	(AT91_TWI_TXCOMP | AT91_TWI_RXRDY | AT91_TWI_TXRDY | AT91_TWI_NACK \
	| AT91_TWI_SVACC | AT91_TWI_EOSACC)

#define	AT91_TWI_IER		0x0024	/* Interrupt Enable Register */
#define	AT91_TWI_IDR		0x0028	/* Interrupt Disable Register */
#define	AT91_TWI_IMR		0x002c	/* Interrupt Mask Register */
#define	AT91_TWI_RHR		0x0030	/* Receive Holding Register */
#define	AT91_TWI_THR		0x0034	/* Transmit Holding Register */

#define	AT91_TWI_ACR		0x0040	/* Alternative Command Register */
#define	AT91_TWI_ACR_DATAL_MASK	GENMASK(15, 0)
#define	AT91_TWI_ACR_DATAL(len)	((len) & AT91_TWI_ACR_DATAL_MASK)
#define	AT91_TWI_ACR_DIR	BIT(8)

#define AT91_TWI_FILTR		0x0044
#define AT91_TWI_FILTR_FILT	BIT(0)
#define AT91_TWI_FILTR_PADFEN	BIT(1)
#define AT91_TWI_FILTR_THRES(v)		((v) << 8)
#define AT91_TWI_FILTR_THRES_MAX	7
#define AT91_TWI_FILTR_THRES_MASK	GENMASK(10, 8)

#define	AT91_TWI_FMR		0x0050	/* FIFO Mode Register */
#define	AT91_TWI_FMR_TXRDYM(mode)	(((mode) & 0x3) << 0)
#define	AT91_TWI_FMR_TXRDYM_MASK	(0x3 << 0)
#define	AT91_TWI_FMR_RXRDYM(mode)	(((mode) & 0x3) << 4)
#define	AT91_TWI_FMR_RXRDYM_MASK	(0x3 << 4)
#define	AT91_TWI_ONE_DATA	0x0
#define	AT91_TWI_TWO_DATA	0x1
#define	AT91_TWI_FOUR_DATA	0x2

#define	AT91_TWI_FLR		0x0054	/* FIFO Level Register */

#define	AT91_TWI_FSR		0x0060	/* FIFO Status Register */
#define	AT91_TWI_FIER		0x0064	/* FIFO Interrupt Enable Register */
#define	AT91_TWI_FIDR		0x0068	/* FIFO Interrupt Disable Register */
#define	AT91_TWI_FIMR		0x006c	/* FIFO Interrupt Mask Register */

#define	AT91_TWI_VER		0x00fc	/* Version Register */

struct at91_twi_pdata {
	unsigned clk_max_div;
	unsigned clk_offset;
	bool has_unre_flag;
	bool has_alt_cmd;
	bool has_hold_field;
	bool has_dig_filtr;
	bool has_adv_dig_filtr;
	bool has_ana_filtr;
	bool has_clear_cmd;
	struct at_dma_slave dma_slave;
};

struct at91_twi_dma {
	struct dma_chan *chan_rx;
	struct dma_chan *chan_tx;
	struct scatterlist sg[2];
	struct dma_async_tx_descriptor *data_desc;
	enum dma_data_direction direction;
	bool buf_mapped;
	bool xfer_in_progress;
};

struct at91_twi_dev {
	struct device *dev;
	void __iomem *base;
	struct completion cmd_complete;
	struct clk *clk;
	u8 *buf;
	size_t buf_len;
	struct i2c_msg *msg;
	int irq;
	unsigned imr;
	unsigned transfer_status;
	struct i2c_adapter adapter;
	unsigned twi_cwgr_reg;
	struct at91_twi_pdata *pdata;
	bool use_dma;
	bool use_alt_cmd;
	bool recv_len_abort;
	u32 fifo_size;
	struct at91_twi_dma dma;
	bool slave_detected;
	struct i2c_bus_recovery_info rinfo;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_pins_default;
	struct pinctrl_state *pinctrl_pins_gpio;
#ifdef CONFIG_I2C_AT91_SLAVE_EXPERIMENTAL
	unsigned smr;
	struct i2c_client *slave;
#endif
	bool enable_dig_filt;
	bool enable_ana_filt;
	u32 filter_width;
};

unsigned at91_twi_read(struct at91_twi_dev *dev, unsigned reg);
void at91_twi_write(struct at91_twi_dev *dev, unsigned reg, unsigned val);
void at91_disable_twi_interrupts(struct at91_twi_dev *dev);
void at91_twi_irq_save(struct at91_twi_dev *dev);
void at91_twi_irq_restore(struct at91_twi_dev *dev);
void at91_init_twi_bus(struct at91_twi_dev *dev);

void at91_init_twi_bus_master(struct at91_twi_dev *dev);
int at91_twi_probe_master(struct platform_device *pdev, u32 phy_addr,
			  struct at91_twi_dev *dev);

#ifdef CONFIG_I2C_AT91_SLAVE_EXPERIMENTAL
void at91_init_twi_bus_slave(struct at91_twi_dev *dev);
int at91_twi_probe_slave(struct platform_device *pdev, u32 phy_addr,
			 struct at91_twi_dev *dev);

#else
static inline void at91_init_twi_bus_slave(struct at91_twi_dev *dev) {}
static inline int at91_twi_probe_slave(struct platform_device *pdev,
				       u32 phy_addr, struct at91_twi_dev *dev)
{
	return -EINVAL;
}

#endif
