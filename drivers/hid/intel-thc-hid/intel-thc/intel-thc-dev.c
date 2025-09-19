/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#include <linux/bitfield.h>
#include <linux/math.h>
#include <linux/regmap.h>
#include <linux/string_choices.h>

#include "intel-thc-dev.h"
#include "intel-thc-hw.h"

static int thc_regmap_read(void *context, unsigned int reg,
			   unsigned int *val)
{
	struct thc_device *thc_ctx = context;
	void __iomem *base = thc_ctx->mmio_addr;

	*val = ioread32(base + reg);
	return 0;
}

static int thc_regmap_write(void *context, unsigned int reg,
			    unsigned int val)
{
	struct thc_device *thc_ctx = context;
	void __iomem *base = thc_ctx->mmio_addr;

	iowrite32(val, base + reg);
	return 0;
}

static const struct regmap_range thc_rw_ranges[] = {
	regmap_reg_range(0x10, 0x14),
	regmap_reg_range(0x1000, 0x1320),
};

static const struct regmap_access_table thc_rw_table = {
	.yes_ranges = thc_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(thc_rw_ranges),
};

static const struct regmap_config thc_regmap_cfg = {
	.name = "thc_regmap_common",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x1320,
	.reg_read = thc_regmap_read,
	.reg_write = thc_regmap_write,
	.cache_type = REGCACHE_NONE,
	.fast_io = true,
	.rd_table = &thc_rw_table,
	.wr_table = &thc_rw_table,
	.volatile_table	= &thc_rw_table,
};

/**
 * thc_clear_state - Clear THC hardware state
 *
 * @dev: The pointer of THC device structure
 */
static void thc_clear_state(const struct thc_device *dev)
{
	u32 val;

	/* Clear interrupt cause register */
	val = THC_M_PRT_ERR_CAUSE_INVLD_DEV_ENTRY |
	      THC_M_PRT_ERR_CAUSE_FRAME_BABBLE_ERR |
	      THC_M_PRT_ERR_CAUSE_BUF_OVRRUN_ERR |
	      THC_M_PRT_ERR_CAUSE_PRD_ENTRY_ERR;
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_ERR_CAUSE_OFFSET, val, val);

	/* Clear interrupt error state */
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_CNTRL_1_OFFSET,
			  THC_M_PRT_READ_DMA_CNTRL_IE_STALL,
			  THC_M_PRT_READ_DMA_CNTRL_IE_STALL);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_CNTRL_2_OFFSET,
			  THC_M_PRT_READ_DMA_CNTRL_IE_STALL,
			  THC_M_PRT_READ_DMA_CNTRL_IE_STALL);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			  THC_M_PRT_INT_STATUS_TXN_ERR_INT_STS,
			  THC_M_PRT_INT_STATUS_TXN_ERR_INT_STS);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			  THC_M_PRT_INT_STATUS_FATAL_ERR_INT_STS,
			  THC_M_PRT_INT_STATUS_FATAL_ERR_INT_STS);

	val = THC_M_PRT_INT_EN_TXN_ERR_INT_EN |
	      THC_M_PRT_INT_EN_FATAL_ERR_INT_EN |
	      THC_M_PRT_INT_EN_BUF_OVRRUN_ERR_INT_EN;
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_INT_EN_OFFSET, val, val);

	val = THC_M_PRT_SW_SEQ_STS_THC_SS_ERR |
	      THC_M_PRT_SW_SEQ_STS_TSSDONE;
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_SW_SEQ_STS_OFFSET, val, val);

	/* Clear RxDMA state */
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_CNTRL_1_OFFSET,
			  THC_M_PRT_READ_DMA_CNTRL_IE_EOF, 0);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_CNTRL_2_OFFSET,
			  THC_M_PRT_READ_DMA_CNTRL_IE_EOF, 0);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_INT_STS_1_OFFSET,
			  THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS,
			  THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_INT_STS_2_OFFSET,
			  THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS,
			  THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_INT_STS_1_OFFSET,
			  THC_M_PRT_READ_DMA_INT_STS_NONDMA_INT_STS,
			  THC_M_PRT_READ_DMA_INT_STS_NONDMA_INT_STS);

	/* Clear TxDMA state */
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_WRITE_DMA_CNTRL_OFFSET,
			  THC_M_PRT_WRITE_DMA_CNTRL_THC_WRDMA_IE_IOC_DMACPL,
			  THC_M_PRT_WRITE_DMA_CNTRL_THC_WRDMA_IE_IOC_DMACPL);

	val = THC_M_PRT_WRITE_INT_STS_THC_WRDMA_ERROR_STS |
	      THC_M_PRT_WRITE_INT_STS_THC_WRDMA_IOC_STS |
	      THC_M_PRT_WRITE_INT_STS_THC_WRDMA_CMPL_STATUS;
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_WRITE_INT_STS_OFFSET, val, val);

	/* Reset all DMAs count */
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_DB_CNT_1_OFFSET,
			  THC_M_PRT_DB_CNT_1_THC_M_PRT_DB_CNT_RST,
			  THC_M_PRT_DB_CNT_1_THC_M_PRT_DB_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_DEVINT_CNT_OFFSET,
			  THC_M_PRT_DEVINT_CNT_THC_M_PRT_DEVINT_CNT_RST,
			  THC_M_PRT_DEVINT_CNT_THC_M_PRT_DEVINT_CNT_RST);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_CNTRL_1_OFFSET,
			  THC_M_PRT_READ_DMA_CNTRL_TPCPR,
			  THC_M_PRT_READ_DMA_CNTRL_TPCPR);

	/* Reset THC hardware sequence state */
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_FRAME_DROP_CNT_1_OFFSET,
			  THC_M_PRT_FRAME_DROP_CNT_1_RFDC,
			  THC_M_PRT_FRAME_DROP_CNT_1_RFDC);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_FRAME_DROP_CNT_2_OFFSET,
			  THC_M_PRT_FRAME_DROP_CNT_2_RFDC,
			  THC_M_PRT_FRAME_DROP_CNT_2_RFDC);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_FRM_CNT_1_OFFSET,
			  THC_M_PRT_FRM_CNT_1_THC_M_PRT_FRM_CNT_RST,
			  THC_M_PRT_FRM_CNT_1_THC_M_PRT_FRM_CNT_RST);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_FRM_CNT_2_OFFSET,
			  THC_M_PRT_FRM_CNT_2_THC_M_PRT_FRM_CNT_RST,
			  THC_M_PRT_FRM_CNT_2_THC_M_PRT_FRM_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_RXDMA_PKT_CNT_1_OFFSET,
			  THC_M_PRT_RXDMA_PKT_CNT_1_THC_M_PRT_RXDMA_PKT_CNT_RST,
			  THC_M_PRT_RXDMA_PKT_CNT_1_THC_M_PRT_RXDMA_PKT_CNT_RST);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_RXDMA_PKT_CNT_2_OFFSET,
			  THC_M_PRT_RXDMA_PKT_CNT_2_THC_M_PRT_RXDMA_PKT_CNT_RST,
			  THC_M_PRT_RXDMA_PKT_CNT_2_THC_M_PRT_RXDMA_PKT_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_SWINT_CNT_1_OFFSET,
			  THC_M_PRT_SWINT_CNT_1_THC_M_PRT_SWINT_CNT_RST,
			  THC_M_PRT_SWINT_CNT_1_THC_M_PRT_SWINT_CNT_RST);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_SWINT_CNT_1_OFFSET,
			  THC_M_PRT_SWINT_CNT_1_THC_M_PRT_SWINT_CNT_RST,
			  THC_M_PRT_SWINT_CNT_1_THC_M_PRT_SWINT_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_TX_FRM_CNT_OFFSET,
			  THC_M_PRT_TX_FRM_CNT_THC_M_PRT_TX_FRM_CNT_RST,
			  THC_M_PRT_TX_FRM_CNT_THC_M_PRT_TX_FRM_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_TXDMA_PKT_CNT_OFFSET,
			  THC_M_PRT_TXDMA_PKT_CNT_THC_M_PRT_TXDMA_PKT_CNT_RST,
			  THC_M_PRT_TXDMA_PKT_CNT_THC_M_PRT_TXDMA_PKT_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_UFRM_CNT_1_OFFSET,
			  THC_M_PRT_UFRM_CNT_1_THC_M_PRT_UFRM_CNT_RST,
			  THC_M_PRT_UFRM_CNT_1_THC_M_PRT_UFRM_CNT_RST);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_UFRM_CNT_2_OFFSET,
			  THC_M_PRT_UFRM_CNT_2_THC_M_PRT_UFRM_CNT_RST,
			  THC_M_PRT_UFRM_CNT_2_THC_M_PRT_UFRM_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_PRD_EMPTY_CNT_1_OFFSET,
			  THC_M_PRT_PRD_EMPTY_CNT_1_RPTEC,
			  THC_M_PRT_PRD_EMPTY_CNT_1_RPTEC);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_PRD_EMPTY_CNT_2_OFFSET,
			  THC_M_PRT_PRD_EMPTY_CNT_2_RPTEC,
			  THC_M_PRT_PRD_EMPTY_CNT_2_RPTEC);
}

/**
 * thc_dev_init - Allocate and initialize the THC device structure
 *
 * @device: The pointer of device structure
 * @mem_addr: The pointer of MMIO memory address
 *
 * Return: The thc_device pointer on success, NULL on failed.
 */
struct thc_device *thc_dev_init(struct device *device, void __iomem *mem_addr)
{
	struct thc_device *thc_dev;
	int ret;

	thc_dev = devm_kzalloc(device, sizeof(*thc_dev), GFP_KERNEL);
	if (!thc_dev)
		return ERR_PTR(-ENOMEM);

	thc_dev->dev = device;
	thc_dev->mmio_addr = mem_addr;
	thc_dev->thc_regmap = devm_regmap_init(device, NULL, thc_dev, &thc_regmap_cfg);
	if (IS_ERR(thc_dev->thc_regmap)) {
		ret = PTR_ERR(thc_dev->thc_regmap);
		dev_err_once(device, "Failed to init thc_regmap: %d\n", ret);
		return ERR_PTR(ret);
	}

	thc_clear_state(thc_dev);

	mutex_init(&thc_dev->thc_bus_lock);
	init_waitqueue_head(&thc_dev->write_complete_wait);
	init_waitqueue_head(&thc_dev->swdma_complete_wait);

	thc_dev->dma_ctx = thc_dma_init(thc_dev);
	if (!thc_dev->dma_ctx) {
		dev_err_once(device, "DMA context init failed\n");
		return ERR_PTR(-ENOMEM);
	}

	return thc_dev;
}
EXPORT_SYMBOL_NS_GPL(thc_dev_init, "INTEL_THC");

static int prepare_pio(const struct thc_device *dev, const u8 pio_op,
		       const u32 address, const u32 size)
{
	u32 sts, ctrl, addr, mask;

	regmap_read(dev->thc_regmap, THC_M_PRT_SW_SEQ_STS_OFFSET, &sts);

	/* Check if THC previous PIO still in progress */
	if (sts & THC_M_PRT_SW_SEQ_STS_THC_SS_CIP) {
		dev_err_once(dev->dev, "THC PIO is still busy!\n");
		return -EBUSY;
	}

	/* Clear error bit and complete bit in state register */
	sts |= THC_M_PRT_SW_SEQ_STS_THC_SS_ERR |
	       THC_M_PRT_SW_SEQ_STS_TSSDONE;
	regmap_write(dev->thc_regmap, THC_M_PRT_SW_SEQ_STS_OFFSET, sts);

	/* Set PIO data size, opcode and interrupt capability */
	ctrl = FIELD_PREP(THC_M_PRT_SW_SEQ_CNTRL_THC_SS_BC, size) |
	       FIELD_PREP(THC_M_PRT_SW_SEQ_CNTRL_THC_SS_CMD, pio_op);
	if (dev->pio_int_supported)
		ctrl |= THC_M_PRT_SW_SEQ_CNTRL_THC_SS_CD_IE;

	mask = THC_M_PRT_SW_SEQ_CNTRL_THC_SS_BC |
	       THC_M_PRT_SW_SEQ_CNTRL_THC_SS_CMD |
	       THC_M_PRT_SW_SEQ_CNTRL_THC_SS_CD_IE;
	regmap_write_bits(dev->thc_regmap,
			  THC_M_PRT_SW_SEQ_CNTRL_OFFSET, mask, ctrl);

	/* Set PIO target address */
	addr = FIELD_PREP(THC_M_PRT_SW_SEQ_DATA0_ADDR_THC_SW_SEQ_DATA0_ADDR, address);
	mask = THC_M_PRT_SW_SEQ_DATA0_ADDR_THC_SW_SEQ_DATA0_ADDR;
	regmap_write_bits(dev->thc_regmap,
			  THC_M_PRT_SW_SEQ_DATA0_ADDR_OFFSET, mask, addr);
	return 0;
}

static void pio_start(const struct thc_device *dev,
		      u32 size_in_bytes, const u32 *buffer)
{
	if (size_in_bytes && buffer)
		regmap_bulk_write(dev->thc_regmap, THC_M_PRT_SW_SEQ_DATA1_OFFSET,
				  buffer, size_in_bytes / sizeof(u32));

	/* Enable Start bit */
	regmap_write_bits(dev->thc_regmap,
			  THC_M_PRT_SW_SEQ_CNTRL_OFFSET,
			  THC_M_PRT_SW_SEQ_CNTRL_TSSGO,
			  THC_M_PRT_SW_SEQ_CNTRL_TSSGO);
}

static int pio_complete(const struct thc_device *dev,
			u32 *buffer, u32 *size)
{
	u32 sts, ctrl;

	regmap_read(dev->thc_regmap, THC_M_PRT_SW_SEQ_STS_OFFSET, &sts);
	if (sts & THC_M_PRT_SW_SEQ_STS_THC_SS_ERR) {
		dev_err_once(dev->dev, "PIO operation error\n");
		return -EBUSY;
	}

	if (buffer && size) {
		regmap_read(dev->thc_regmap, THC_M_PRT_SW_SEQ_CNTRL_OFFSET, &ctrl);
		*size = FIELD_GET(THC_M_PRT_SW_SEQ_CNTRL_THC_SS_BC, ctrl);

		regmap_bulk_read(dev->thc_regmap, THC_M_PRT_SW_SEQ_DATA1_OFFSET,
				 buffer, *size / sizeof(u32));
	}

	sts |= THC_M_PRT_SW_SEQ_STS_THC_SS_ERR | THC_M_PRT_SW_SEQ_STS_TSSDONE;
	regmap_write(dev->thc_regmap, THC_M_PRT_SW_SEQ_STS_OFFSET, sts);
	return 0;
}

static int pio_wait(const struct thc_device *dev)
{
	u32 sts = 0;
	int ret;

	ret = regmap_read_poll_timeout(dev->thc_regmap, THC_M_PRT_SW_SEQ_STS_OFFSET, sts,
				       !(sts & THC_M_PRT_SW_SEQ_STS_THC_SS_CIP ||
				       !(sts & THC_M_PRT_SW_SEQ_STS_TSSDONE)),
				       THC_REGMAP_POLLING_INTERVAL_US, THC_PIO_DONE_TIMEOUT_US);
	if (ret)
		dev_err_once(dev->dev, "Timeout while polling PIO operation done\n");

	return ret;
}

/**
 * thc_tic_pio_read - Read data from touch device by PIO
 *
 * @dev: The pointer of THC private device context
 * @address: Slave address for the PIO operation
 * @size: Expected read data size
 * @actual_size: The pointer of the actual data size read from touch device
 * @buffer: The pointer of data buffer to store the data read from touch device
 *
 * Return: 0 on success, other error codes on failed.
 */
int thc_tic_pio_read(struct thc_device *dev, const u32 address,
		     const u32 size, u32 *actual_size, u32 *buffer)
{
	u8 opcode;
	int ret;

	if (size <= 0 || !actual_size || !buffer) {
		dev_err(dev->dev, "Invalid input parameters, size %u, actual_size %p, buffer %p\n",
			size, actual_size, buffer);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&dev->thc_bus_lock))
		return -EINTR;

	opcode = (dev->port_type == THC_PORT_TYPE_SPI) ?
		 THC_PIO_OP_SPI_TIC_READ : THC_PIO_OP_I2C_TIC_READ;

	ret = prepare_pio(dev, opcode, address, size);
	if (ret < 0)
		goto end;

	pio_start(dev, 0, NULL);

	ret = pio_wait(dev);
	if (ret < 0)
		goto end;

	ret = pio_complete(dev, buffer, actual_size);

end:
	mutex_unlock(&dev->thc_bus_lock);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(thc_tic_pio_read, "INTEL_THC");

/**
 * thc_tic_pio_write - Write data to touch device by PIO
 *
 * @dev: The pointer of THC private device context
 * @address: Slave address for the PIO operation
 * @size: PIO write data size
 * @buffer: The pointer of the write data buffer
 *
 * Return: 0 on success, other error codes on failed.
 */
int thc_tic_pio_write(struct thc_device *dev, const u32 address,
		      const u32 size, const u32 *buffer)
{
	u8 opcode;
	int ret;

	if (size <= 0 || !buffer) {
		dev_err(dev->dev, "Invalid input parameters, size %u, buffer %p\n",
			size, buffer);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&dev->thc_bus_lock))
		return -EINTR;

	opcode = (dev->port_type == THC_PORT_TYPE_SPI) ?
		 THC_PIO_OP_SPI_TIC_WRITE : THC_PIO_OP_I2C_TIC_WRITE;

	ret = prepare_pio(dev, opcode, address, size);
	if (ret < 0)
		goto end;

	pio_start(dev, size, buffer);

	ret = pio_wait(dev);
	if (ret < 0)
		goto end;

	ret = pio_complete(dev, NULL, NULL);

end:
	mutex_unlock(&dev->thc_bus_lock);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(thc_tic_pio_write, "INTEL_THC");

/**
 * thc_tic_pio_write_and_read - Write data followed by read data by PIO
 *
 * @dev: The pointer of THC private device context
 * @address: Slave address for the PIO operation
 * @write_size: PIO write data size
 * @write_buffer: The pointer of the write data buffer
 * @read_size: Expected PIO read data size
 * @actual_size: The pointer of the actual read data size
 * @read_buffer: The pointer of PIO read data buffer
 *
 * Return: 0 on success, other error codes on failed.
 */
int thc_tic_pio_write_and_read(struct thc_device *dev, const u32 address,
			       const u32 write_size, const u32 *write_buffer,
			       const u32 read_size, u32 *actual_size, u32 *read_buffer)
{
	u32 i2c_ctrl, mask;
	int ret;

	if (dev->port_type == THC_PORT_TYPE_SPI) {
		dev_err(dev->dev, "SPI port type doesn't support pio write and read!");
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&dev->thc_bus_lock))
		return -EINTR;

	/* Config i2c PIO write and read sequence */
	i2c_ctrl = FIELD_PREP(THC_M_PRT_SW_SEQ_I2C_WR_CNTRL_THC_PIO_I2C_WBC, write_size);
	mask = THC_M_PRT_SW_SEQ_I2C_WR_CNTRL_THC_PIO_I2C_WBC;

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_SW_SEQ_I2C_WR_CNTRL_OFFSET,
			  mask, i2c_ctrl);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_SW_SEQ_I2C_WR_CNTRL_OFFSET,
			  THC_M_PRT_SW_SEQ_I2C_WR_CNTRL_THC_I2C_RW_PIO_EN,
			  THC_M_PRT_SW_SEQ_I2C_WR_CNTRL_THC_I2C_RW_PIO_EN);

	ret = prepare_pio(dev, THC_PIO_OP_I2C_TIC_WRITE_AND_READ, address, read_size);
	if (ret < 0)
		goto end;

	pio_start(dev, write_size, write_buffer);

	ret = pio_wait(dev);
	if (ret < 0)
		goto end;

	ret = pio_complete(dev, read_buffer, actual_size);

end:
	mutex_unlock(&dev->thc_bus_lock);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(thc_tic_pio_write_and_read, "INTEL_THC");

/**
 * thc_interrupt_config - Configure THC interrupts
 *
 * @dev: The pointer of THC private device context
 */
void thc_interrupt_config(struct thc_device *dev)
{
	u32 mbits, mask, r_dma_ctrl_1;

	/* Clear Error reporting interrupt status bits */
	mbits = THC_M_PRT_INT_STATUS_TXN_ERR_INT_STS |
		THC_M_PRT_INT_STATUS_FATAL_ERR_INT_STS;
	regmap_write_bits(dev->thc_regmap,
			  THC_M_PRT_INT_STATUS_OFFSET,
			  mbits, mbits);

	/* Enable Error Reporting Interrupts */
	mbits = THC_M_PRT_INT_EN_TXN_ERR_INT_EN |
		THC_M_PRT_INT_EN_FATAL_ERR_INT_EN |
		THC_M_PRT_INT_EN_BUF_OVRRUN_ERR_INT_EN;
	regmap_write_bits(dev->thc_regmap,
			  THC_M_PRT_INT_EN_OFFSET,
			  mbits, mbits);

	/* Clear PIO Interrupt status bits */
	mbits = THC_M_PRT_SW_SEQ_STS_THC_SS_ERR |
		THC_M_PRT_SW_SEQ_STS_TSSDONE;
	regmap_write_bits(dev->thc_regmap,
			  THC_M_PRT_SW_SEQ_STS_OFFSET,
			  mbits, mbits);

	/* Read Interrupts */
	regmap_read(dev->thc_regmap,
		    THC_M_PRT_READ_DMA_CNTRL_1_OFFSET,
		    &r_dma_ctrl_1);
	/* Disable RxDMA1 */
	r_dma_ctrl_1 &= ~THC_M_PRT_READ_DMA_CNTRL_IE_EOF;
	regmap_write(dev->thc_regmap,
		     THC_M_PRT_READ_DMA_CNTRL_1_OFFSET,
		     r_dma_ctrl_1);

	/* Ack EOF Interrupt RxDMA1 */
	mbits = THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS;
	/* Ack NonDMA Interrupt */
	mbits |= THC_M_PRT_READ_DMA_INT_STS_NONDMA_INT_STS;
	regmap_write_bits(dev->thc_regmap,
			  THC_M_PRT_READ_DMA_INT_STS_1_OFFSET,
			  mbits, mbits);

	/* Ack EOF Interrupt RxDMA2 */
	regmap_write_bits(dev->thc_regmap,
			  THC_M_PRT_READ_DMA_INT_STS_2_OFFSET,
			  THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS,
			  THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS);

	/* Write Interrupts */
	/* Disable TxDMA */
	regmap_write_bits(dev->thc_regmap,
			  THC_M_PRT_WRITE_DMA_CNTRL_OFFSET,
			  THC_M_PRT_WRITE_DMA_CNTRL_THC_WRDMA_IE_IOC_DMACPL,
			  0);

	/* Clear TxDMA interrupt status bits */
	mbits = THC_M_PRT_WRITE_INT_STS_THC_WRDMA_ERROR_STS;
	mbits |=  THC_M_PRT_WRITE_INT_STS_THC_WRDMA_IOC_STS;
	regmap_write_bits(dev->thc_regmap,
			  THC_M_PRT_WRITE_INT_STS_OFFSET,
			  mbits, mbits);

	/* Enable Non-DMA device inband interrupt */
	r_dma_ctrl_1 |= THC_M_PRT_READ_DMA_CNTRL_IE_NDDI;
	regmap_write(dev->thc_regmap,
		     THC_M_PRT_READ_DMA_CNTRL_1_OFFSET,
		     r_dma_ctrl_1);

	if (dev->port_type == THC_PORT_TYPE_SPI) {
		/* Edge triggered interrupt */
		regmap_write_bits(dev->thc_regmap, THC_M_PRT_TSEQ_CNTRL_1_OFFSET,
				  THC_M_PRT_TSEQ_CNTRL_1_INT_EDG_DET_EN,
				  THC_M_PRT_TSEQ_CNTRL_1_INT_EDG_DET_EN);
	} else {
		/* Level triggered interrupt */
		regmap_write_bits(dev->thc_regmap, THC_M_PRT_TSEQ_CNTRL_1_OFFSET,
				  THC_M_PRT_TSEQ_CNTRL_1_INT_EDG_DET_EN, 0);

		mbits = THC_M_PRT_INT_EN_THC_I2C_IC_MST_ON_HOLD_INT_EN |
			THC_M_PRT_INT_EN_THC_I2C_IC_SCL_STUCK_AT_LOW_DET_INT_EN |
			THC_M_PRT_INT_EN_THC_I2C_IC_TX_ABRT_INT_EN |
			THC_M_PRT_INT_EN_THC_I2C_IC_TX_OVER_INT_EN |
			THC_M_PRT_INT_EN_THC_I2C_IC_RX_FULL_INT_EN |
			THC_M_PRT_INT_EN_THC_I2C_IC_RX_OVER_INT_EN |
			THC_M_PRT_INT_EN_THC_I2C_IC_RX_UNDER_INT_EN;
		regmap_write_bits(dev->thc_regmap, THC_M_PRT_INT_EN_OFFSET,
				  mbits, mbits);
	}

	thc_set_pio_interrupt_support(dev, false);

	/* HIDSPI specific settings */
	if (dev->port_type == THC_PORT_TYPE_SPI) {
		mbits = FIELD_PREP(THC_M_PRT_DEVINT_CFG_1_THC_M_PRT_INTTYP_OFFSET,
				   THC_BIT_OFFSET_INTERRUPT_TYPE) |
			FIELD_PREP(THC_M_PRT_DEVINT_CFG_1_THC_M_PRT_INTTYP_LEN,
				   THC_BIT_LENGTH_INTERRUPT_TYPE) |
			FIELD_PREP(THC_M_PRT_DEVINT_CFG_1_THC_M_PRT_EOF_OFFSET,
				   THC_BIT_OFFSET_LAST_FRAGMENT_FLAG) |
			FIELD_PREP(THC_M_PRT_DEVINT_CFG_1_THC_M_PRT_INTTYP_DATA_VAL,
				   THC_BITMASK_INVALID_TYPE_DATA);
		mask = THC_M_PRT_DEVINT_CFG_1_THC_M_PRT_INTTYP_OFFSET |
		       THC_M_PRT_DEVINT_CFG_1_THC_M_PRT_INTTYP_LEN |
		       THC_M_PRT_DEVINT_CFG_1_THC_M_PRT_EOF_OFFSET |
		       THC_M_PRT_DEVINT_CFG_1_THC_M_PRT_INTTYP_DATA_VAL;
		regmap_write_bits(dev->thc_regmap, THC_M_PRT_DEVINT_CFG_1_OFFSET,
				  mask, mbits);

		mbits = FIELD_PREP(THC_M_PRT_DEVINT_CFG_2_THC_M_PRT_UFSIZE_OFFSET,
				   THC_BIT_OFFSET_MICROFRAME_SIZE) |
			FIELD_PREP(THC_M_PRT_DEVINT_CFG_2_THC_M_PRT_UFSIZE_LEN,
				   THC_BIT_LENGTH_MICROFRAME_SIZE) |
			FIELD_PREP(THC_M_PRT_DEVINT_CFG_2_THC_M_PRT_UFSIZE_UNIT,
				   THC_UNIT_MICROFRAME_SIZE) |
			THC_M_PRT_DEVINT_CFG_2_THC_M_PRT_FTYPE_IGNORE |
			THC_M_PRT_DEVINT_CFG_2_THC_M_PRT_FTYPE_VAL;
		mask = THC_M_PRT_DEVINT_CFG_2_THC_M_PRT_UFSIZE_OFFSET |
		       THC_M_PRT_DEVINT_CFG_2_THC_M_PRT_UFSIZE_LEN |
		       THC_M_PRT_DEVINT_CFG_2_THC_M_PRT_UFSIZE_UNIT |
		       THC_M_PRT_DEVINT_CFG_2_THC_M_PRT_FTYPE_IGNORE |
		       THC_M_PRT_DEVINT_CFG_2_THC_M_PRT_FTYPE_VAL;
		regmap_write_bits(dev->thc_regmap, THC_M_PRT_DEVINT_CFG_2_OFFSET,
				  mask, mbits);
	}
}
EXPORT_SYMBOL_NS_GPL(thc_interrupt_config, "INTEL_THC");

/**
 * thc_int_trigger_type_select - Select THC interrupt trigger type
 *
 * @dev: the pointer of THC private device context
 * @edge_trigger: determine the interrupt is edge triggered or level triggered
 */
void thc_int_trigger_type_select(struct thc_device *dev, bool edge_trigger)
{
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_TSEQ_CNTRL_1_OFFSET,
			  THC_M_PRT_TSEQ_CNTRL_1_INT_EDG_DET_EN,
			  edge_trigger ? THC_M_PRT_TSEQ_CNTRL_1_INT_EDG_DET_EN : 0);
}
EXPORT_SYMBOL_NS_GPL(thc_int_trigger_type_select, "INTEL_THC");

/**
 * thc_interrupt_enable - Enable or disable THC interrupt
 *
 * @dev: the pointer of THC private device context
 * @int_enable: the flag to control THC interrupt enable or disable
 */
void thc_interrupt_enable(struct thc_device *dev, bool int_enable)
{
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_INT_EN_OFFSET,
			  THC_M_PRT_INT_EN_GBL_INT_EN,
			  int_enable ? THC_M_PRT_INT_EN_GBL_INT_EN : 0);
}
EXPORT_SYMBOL_NS_GPL(thc_interrupt_enable, "INTEL_THC");

/**
 * thc_interrupt_quiesce - Quiesce or unquiesce external touch device interrupt
 *
 * @dev: the pointer of THC private device context
 * @int_quiesce: the flag to determine quiesce or unquiesce device interrupt
 *
 * Return: 0 on success, other error codes on failed
 */
int thc_interrupt_quiesce(const struct thc_device *dev, bool int_quiesce)
{
	u32 ctrl;
	int ret;

	regmap_read(dev->thc_regmap, THC_M_PRT_CONTROL_OFFSET, &ctrl);
	if (!(ctrl & THC_M_PRT_CONTROL_THC_DEVINT_QUIESCE_EN) && !int_quiesce) {
		dev_warn(dev->dev, "THC interrupt already unquiesce\n");
		return 0;
	}

	if ((ctrl & THC_M_PRT_CONTROL_THC_DEVINT_QUIESCE_EN) && int_quiesce) {
		dev_warn(dev->dev, "THC interrupt already quiesce\n");
		return 0;
	}

	/* Quiesce device interrupt - Set quiesce bit and waiting for THC HW to ACK */
	if (int_quiesce)
		regmap_write_bits(dev->thc_regmap, THC_M_PRT_CONTROL_OFFSET,
				  THC_M_PRT_CONTROL_THC_DEVINT_QUIESCE_EN,
				  THC_M_PRT_CONTROL_THC_DEVINT_QUIESCE_EN);

	ret = regmap_read_poll_timeout(dev->thc_regmap, THC_M_PRT_CONTROL_OFFSET, ctrl,
				       ctrl & THC_M_PRT_CONTROL_THC_DEVINT_QUIESCE_HW_STS,
				       THC_REGMAP_POLLING_INTERVAL_US, THC_QUIESCE_EN_TIMEOUT_US);
	if (ret) {
		dev_err_once(dev->dev,
			     "Timeout while waiting THC idle, target quiesce state = %s\n",
			     str_true_false(int_quiesce));
		return ret;
	}

	/* Unquiesce device interrupt - Clear the quiesce bit */
	if (!int_quiesce)
		regmap_write_bits(dev->thc_regmap, THC_M_PRT_CONTROL_OFFSET,
				  THC_M_PRT_CONTROL_THC_DEVINT_QUIESCE_EN, 0);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(thc_interrupt_quiesce, "INTEL_THC");

/**
 * thc_set_pio_interrupt_support - Determine PIO interrupt is supported or not
 *
 * @dev: The pointer of THC private device context
 * @supported: The flag to determine enabling PIO interrupt or not
 */
void thc_set_pio_interrupt_support(struct thc_device *dev, bool supported)
{
	dev->pio_int_supported = supported;
}
EXPORT_SYMBOL_NS_GPL(thc_set_pio_interrupt_support, "INTEL_THC");

/**
 * thc_ltr_config - Configure THC Latency Tolerance Reporting(LTR) settings
 *
 * @dev: The pointer of THC private device context
 * @active_ltr_us: active LTR value, unit is us
 * @lp_ltr_us: low power LTR value, unit is us
 */
void thc_ltr_config(struct thc_device *dev, u32 active_ltr_us, u32 lp_ltr_us)
{
	u32 active_ltr_scale, lp_ltr_scale, ltr_ctrl, ltr_mask, orig, tmp;

	if (active_ltr_us >= THC_LTR_MIN_VAL_SCALE_3 &&
	    active_ltr_us < THC_LTR_MAX_VAL_SCALE_3) {
		active_ltr_scale = THC_LTR_SCALE_3;
		active_ltr_us = active_ltr_us >> 5;
	} else if (active_ltr_us >= THC_LTR_MIN_VAL_SCALE_4 &&
		   active_ltr_us < THC_LTR_MAX_VAL_SCALE_4) {
		active_ltr_scale = THC_LTR_SCALE_4;
		active_ltr_us = active_ltr_us >> 10;
	} else if (active_ltr_us >= THC_LTR_MIN_VAL_SCALE_5 &&
		   active_ltr_us < THC_LTR_MAX_VAL_SCALE_5) {
		active_ltr_scale = THC_LTR_SCALE_5;
		active_ltr_us = active_ltr_us >> 15;
	} else {
		active_ltr_scale = THC_LTR_SCALE_2;
	}

	if (lp_ltr_us >= THC_LTR_MIN_VAL_SCALE_3 &&
	    lp_ltr_us < THC_LTR_MAX_VAL_SCALE_3) {
		lp_ltr_scale = THC_LTR_SCALE_3;
		lp_ltr_us = lp_ltr_us >> 5;
	} else if (lp_ltr_us >= THC_LTR_MIN_VAL_SCALE_4 &&
		   lp_ltr_us < THC_LTR_MAX_VAL_SCALE_4) {
		lp_ltr_scale = THC_LTR_SCALE_4;
		lp_ltr_us = lp_ltr_us >> 10;
	} else if (lp_ltr_us >= THC_LTR_MIN_VAL_SCALE_5 &&
		   lp_ltr_us < THC_LTR_MAX_VAL_SCALE_5) {
		lp_ltr_scale = THC_LTR_SCALE_5;
		lp_ltr_us = lp_ltr_us >> 15;
	} else {
		lp_ltr_scale = THC_LTR_SCALE_2;
	}

	regmap_read(dev->thc_regmap, THC_M_CMN_LTR_CTRL_OFFSET, &orig);
	ltr_ctrl = FIELD_PREP(THC_M_CMN_LTR_CTRL_ACT_LTR_VAL, active_ltr_us) |
		   FIELD_PREP(THC_M_CMN_LTR_CTRL_ACT_LTR_SCALE, active_ltr_scale) |
		   THC_M_CMN_LTR_CTRL_ACTIVE_LTR_REQ |
		   THC_M_CMN_LTR_CTRL_ACTIVE_LTR_EN |
		   FIELD_PREP(THC_M_CMN_LTR_CTRL_LP_LTR_VAL, lp_ltr_us) |
		   FIELD_PREP(THC_M_CMN_LTR_CTRL_LP_LTR_SCALE, lp_ltr_scale) |
		   THC_M_CMN_LTR_CTRL_LP_LTR_REQ;

	ltr_mask = THC_M_CMN_LTR_CTRL_ACT_LTR_VAL |
		   THC_M_CMN_LTR_CTRL_ACT_LTR_SCALE |
		   THC_M_CMN_LTR_CTRL_ACTIVE_LTR_REQ |
		   THC_M_CMN_LTR_CTRL_ACTIVE_LTR_EN |
		   THC_M_CMN_LTR_CTRL_LP_LTR_VAL |
		   THC_M_CMN_LTR_CTRL_LP_LTR_SCALE |
		   THC_M_CMN_LTR_CTRL_LP_LTR_REQ |
		   THC_M_CMN_LTR_CTRL_LP_LTR_EN;

	tmp = orig & ~ltr_mask;
	tmp |= ltr_ctrl & ltr_mask;

	regmap_write(dev->thc_regmap, THC_M_CMN_LTR_CTRL_OFFSET, tmp);
}
EXPORT_SYMBOL_NS_GPL(thc_ltr_config, "INTEL_THC");

/**
 * thc_change_ltr_mode - Change THC LTR mode
 *
 * @dev: The pointer of THC private device context
 * @ltr_mode: LTR mode(active or low power)
 */
void thc_change_ltr_mode(struct thc_device *dev, u32 ltr_mode)
{
	if (ltr_mode == THC_LTR_MODE_ACTIVE) {
		regmap_write_bits(dev->thc_regmap, THC_M_CMN_LTR_CTRL_OFFSET,
				  THC_M_CMN_LTR_CTRL_LP_LTR_EN, 0);
		regmap_write_bits(dev->thc_regmap, THC_M_CMN_LTR_CTRL_OFFSET,
				  THC_M_CMN_LTR_CTRL_ACTIVE_LTR_EN,
				  THC_M_CMN_LTR_CTRL_ACTIVE_LTR_EN);
		return;
	}

	regmap_write_bits(dev->thc_regmap, THC_M_CMN_LTR_CTRL_OFFSET,
			  THC_M_CMN_LTR_CTRL_ACTIVE_LTR_EN, 0);
	regmap_write_bits(dev->thc_regmap, THC_M_CMN_LTR_CTRL_OFFSET,
			  THC_M_CMN_LTR_CTRL_LP_LTR_EN,
			  THC_M_CMN_LTR_CTRL_LP_LTR_EN);
}
EXPORT_SYMBOL_NS_GPL(thc_change_ltr_mode, "INTEL_THC");

/**
 * thc_ltr_unconfig - Unconfigure THC Latency Tolerance Reporting(LTR) settings
 *
 * @dev: The pointer of THC private device context
 */
void thc_ltr_unconfig(struct thc_device *dev)
{
	u32 ltr_ctrl, bits_clear;

	regmap_read(dev->thc_regmap, THC_M_CMN_LTR_CTRL_OFFSET, &ltr_ctrl);
	bits_clear = THC_M_CMN_LTR_CTRL_LP_LTR_EN |
			THC_M_CMN_LTR_CTRL_ACTIVE_LTR_EN |
			THC_M_CMN_LTR_CTRL_LP_LTR_REQ |
			THC_M_CMN_LTR_CTRL_ACTIVE_LTR_REQ;

	ltr_ctrl &= ~bits_clear;

	regmap_write(dev->thc_regmap, THC_M_CMN_LTR_CTRL_OFFSET, ltr_ctrl);
}
EXPORT_SYMBOL_NS_GPL(thc_ltr_unconfig, "INTEL_THC");

/**
 * thc_int_cause_read - Read interrupt cause register value
 *
 * @dev: The pointer of THC private device context
 *
 * Return: The interrupt cause register value
 */
u32 thc_int_cause_read(struct thc_device *dev)
{
	u32 int_cause;

	regmap_read(dev->thc_regmap,
		    THC_M_PRT_DEV_INT_CAUSE_REG_VAL_OFFSET, &int_cause);

	return int_cause;
}
EXPORT_SYMBOL_NS_GPL(thc_int_cause_read, "INTEL_THC");

static void thc_print_txn_error_cause(const struct thc_device *dev)
{
	bool known_error = false;
	u32 cause = 0;

	regmap_read(dev->thc_regmap, THC_M_PRT_ERR_CAUSE_OFFSET, &cause);

	if (cause & THC_M_PRT_ERR_CAUSE_PRD_ENTRY_ERR) {
		dev_err(dev->dev, "TXN Error: Invalid PRD Entry\n");
		known_error = true;
	}
	if (cause & THC_M_PRT_ERR_CAUSE_BUF_OVRRUN_ERR) {
		dev_err(dev->dev, "TXN Error: THC Buffer Overrun\n");
		known_error = true;
	}
	if (cause & THC_M_PRT_ERR_CAUSE_FRAME_BABBLE_ERR) {
		dev_err(dev->dev, "TXN Error: Frame Babble\n");
		known_error = true;
	}
	if (cause & THC_M_PRT_ERR_CAUSE_INVLD_DEV_ENTRY) {
		dev_err(dev->dev, "TXN Error: Invalid Device Register Setting\n");
		known_error = true;
	}

	/* Clear interrupt status bits */
	regmap_write(dev->thc_regmap, THC_M_PRT_ERR_CAUSE_OFFSET, cause);

	if (!known_error)
		dev_err(dev->dev, "TXN Error does not match any known value: 0x%X\n",
			cause);
}

/**
 * thc_interrupt_handler - Handle THC interrupts
 *
 * THC interrupts include several types: external touch device (TIC) non-DMA
 * interrupts, PIO completion interrupts, DMA interrtups, I2C subIP raw
 * interrupts and error interrupts.
 *
 * This is a help function for interrupt processing, it detects interrupt
 * type, clear the interrupt status bit and return the interrupt type to caller
 * for future processing.
 *
 * @dev: The pointer of THC private device context
 *
 * Return: The combined flag for interrupt type
 */
int thc_interrupt_handler(struct thc_device *dev)
{
	u32 read_sts_1, read_sts_2, read_sts_sw, write_sts;
	u32 int_sts, err_cause, seq_cntrl, seq_sts;
	int interrupt_type = 0;

	regmap_read(dev->thc_regmap,
		    THC_M_PRT_READ_DMA_INT_STS_1_OFFSET, &read_sts_1);

	if (read_sts_1 & THC_M_PRT_READ_DMA_INT_STS_NONDMA_INT_STS) {
		dev_dbg(dev->dev, "THC non-DMA device interrupt\n");

		regmap_write(dev->thc_regmap, THC_M_PRT_READ_DMA_INT_STS_1_OFFSET,
			     NONDMA_INT_STS_BIT);

		interrupt_type |= BIT(THC_NONDMA_INT);

		return interrupt_type;
	}

	regmap_read(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET, &int_sts);

	if (int_sts & THC_M_PRT_INT_STATUS_TXN_ERR_INT_STS) {
		dev_err(dev->dev, "THC transaction error, int_sts: 0x%08X\n", int_sts);
		thc_print_txn_error_cause(dev);

		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     TXN_ERR_INT_STS_BIT);

		interrupt_type |= BIT(THC_TXN_ERR_INT);

		return interrupt_type;
	}

	regmap_read(dev->thc_regmap, THC_M_PRT_ERR_CAUSE_OFFSET, &err_cause);
	regmap_read(dev->thc_regmap,
		    THC_M_PRT_READ_DMA_INT_STS_2_OFFSET, &read_sts_2);

	if (err_cause & THC_M_PRT_ERR_CAUSE_BUF_OVRRUN_ERR ||
	    read_sts_1 & THC_M_PRT_READ_DMA_INT_STS_STALL_STS ||
	    read_sts_2 & THC_M_PRT_READ_DMA_INT_STS_STALL_STS) {
		dev_err(dev->dev, "Buffer overrun or RxDMA engine stalled!\n");
		thc_print_txn_error_cause(dev);

		regmap_write(dev->thc_regmap, THC_M_PRT_READ_DMA_INT_STS_2_OFFSET,
			     THC_M_PRT_READ_DMA_INT_STS_STALL_STS);
		regmap_write(dev->thc_regmap, THC_M_PRT_READ_DMA_INT_STS_1_OFFSET,
			     THC_M_PRT_READ_DMA_INT_STS_STALL_STS);
		regmap_write(dev->thc_regmap, THC_M_PRT_ERR_CAUSE_OFFSET,
			     THC_M_PRT_ERR_CAUSE_BUF_OVRRUN_ERR);

		interrupt_type |= BIT(THC_TXN_ERR_INT);

		return interrupt_type;
	}

	if (int_sts & THC_M_PRT_INT_STATUS_FATAL_ERR_INT_STS) {
		dev_err_once(dev->dev, "THC FATAL error, int_sts: 0x%08X\n", int_sts);

		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     TXN_FATAL_INT_STS_BIT);

		interrupt_type |= BIT(THC_FATAL_ERR_INT);

		return interrupt_type;
	}

	regmap_read(dev->thc_regmap,
		    THC_M_PRT_SW_SEQ_CNTRL_OFFSET, &seq_cntrl);
	regmap_read(dev->thc_regmap,
		    THC_M_PRT_SW_SEQ_STS_OFFSET, &seq_sts);

	if (seq_cntrl & THC_M_PRT_SW_SEQ_CNTRL_THC_SS_CD_IE &&
	    seq_sts & THC_M_PRT_SW_SEQ_STS_TSSDONE) {
		dev_dbg(dev->dev, "THC_SS_CD_IE and TSSDONE are set\n");
		interrupt_type |= BIT(THC_PIO_DONE_INT);
	}

	if (read_sts_1 & THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS) {
		dev_dbg(dev->dev, "Got RxDMA1 Read Interrupt\n");

		regmap_write(dev->thc_regmap,
			     THC_M_PRT_READ_DMA_INT_STS_1_OFFSET, read_sts_1);

		interrupt_type |= BIT(THC_RXDMA1_INT);
	}

	if (read_sts_2 & THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS) {
		dev_dbg(dev->dev, "Got RxDMA2 Read Interrupt\n");

		regmap_write(dev->thc_regmap,
			     THC_M_PRT_READ_DMA_INT_STS_2_OFFSET, read_sts_2);

		interrupt_type |= BIT(THC_RXDMA2_INT);
	}

	regmap_read(dev->thc_regmap,
		    THC_M_PRT_READ_DMA_INT_STS_SW_OFFSET, &read_sts_sw);

	if (read_sts_sw & THC_M_PRT_READ_DMA_INT_STS_DMACPL_STS) {
		dev_dbg(dev->dev, "Got SwDMA Read Interrupt\n");

		regmap_write(dev->thc_regmap,
			     THC_M_PRT_READ_DMA_INT_STS_SW_OFFSET, read_sts_sw);

		dev->swdma_done = true;
		wake_up_interruptible(&dev->swdma_complete_wait);

		interrupt_type |= BIT(THC_SWDMA_INT);
	}

	regmap_read(dev->thc_regmap,
		    THC_M_PRT_WRITE_INT_STS_OFFSET, &write_sts);

	if (write_sts & THC_M_PRT_WRITE_INT_STS_THC_WRDMA_CMPL_STATUS) {
		dev_dbg(dev->dev, "Got TxDMA Write complete Interrupt\n");

		regmap_write(dev->thc_regmap,
			     THC_M_PRT_WRITE_INT_STS_OFFSET, write_sts);

		dev->write_done = true;
		wake_up_interruptible(&dev->write_complete_wait);

		interrupt_type |= BIT(THC_TXDMA_INT);
	}

	if (int_sts & THC_M_PRT_INT_STATUS_DEV_RAW_INT_STS) {
		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     THC_M_PRT_INT_STATUS_DEV_RAW_INT_STS);
		interrupt_type |= BIT(THC_I2CSUBIP_INT);
	}
	if (int_sts & THC_M_PRT_INT_STATUS_THC_I2C_IC_RX_UNDER_INT_STS) {
		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     THC_M_PRT_INT_STATUS_THC_I2C_IC_RX_UNDER_INT_STS);
		interrupt_type |= BIT(THC_I2CSUBIP_INT);
	}
	if (int_sts & THC_M_PRT_INT_STATUS_THC_I2C_IC_RX_OVER_INT_STS) {
		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     THC_M_PRT_INT_STATUS_THC_I2C_IC_RX_OVER_INT_STS);
		interrupt_type |= BIT(THC_I2CSUBIP_INT);
	}
	if (int_sts & THC_M_PRT_INT_STATUS_THC_I2C_IC_RX_FULL_INT_STS) {
		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     THC_M_PRT_INT_STATUS_THC_I2C_IC_RX_FULL_INT_STS);
		interrupt_type |= BIT(THC_I2CSUBIP_INT);
	}
	if (int_sts & THC_M_PRT_INT_STATUS_THC_I2C_IC_TX_OVER_INT_STS) {
		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     THC_M_PRT_INT_STATUS_THC_I2C_IC_TX_OVER_INT_STS);
		interrupt_type |= BIT(THC_I2CSUBIP_INT);
	}
	if (int_sts & THC_M_PRT_INT_STATUS_THC_I2C_IC_TX_EMPTY_INT_STS) {
		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     THC_M_PRT_INT_STATUS_THC_I2C_IC_TX_EMPTY_INT_STS);
		interrupt_type |= BIT(THC_I2CSUBIP_INT);
	}
	if (int_sts & THC_M_PRT_INT_STATUS_THC_I2C_IC_TX_ABRT_INT_STS) {
		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     THC_M_PRT_INT_STATUS_THC_I2C_IC_TX_ABRT_INT_STS);
		interrupt_type |= BIT(THC_I2CSUBIP_INT);
	}
	if (int_sts & THC_M_PRT_INT_STATUS_THC_I2C_IC_ACTIVITY_INT_STS) {
		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     THC_M_PRT_INT_STATUS_THC_I2C_IC_ACTIVITY_INT_STS);
		interrupt_type |= BIT(THC_I2CSUBIP_INT);
	}
	if (int_sts & THC_M_PRT_INT_STATUS_THC_I2C_IC_SCL_STUCK_AT_LOW_INT_STS) {
		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     THC_M_PRT_INT_STATUS_THC_I2C_IC_SCL_STUCK_AT_LOW_INT_STS);
		interrupt_type |= BIT(THC_I2CSUBIP_INT);
	}
	if (int_sts & THC_M_PRT_INT_STATUS_THC_I2C_IC_STOP_DET_INT_STS) {
		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     THC_M_PRT_INT_STATUS_THC_I2C_IC_STOP_DET_INT_STS);
		interrupt_type |= BIT(THC_I2CSUBIP_INT);
	}
	if (int_sts & THC_M_PRT_INT_STATUS_THC_I2C_IC_START_DET_INT_STS) {
		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     THC_M_PRT_INT_STATUS_THC_I2C_IC_START_DET_INT_STS);
		interrupt_type |= BIT(THC_I2CSUBIP_INT);
	}
	if (int_sts & THC_M_PRT_INT_STATUS_THC_I2C_IC_MST_ON_HOLD_INT_STS) {
		regmap_write(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			     THC_M_PRT_INT_STATUS_THC_I2C_IC_MST_ON_HOLD_INT_STS);
		interrupt_type |= BIT(THC_I2CSUBIP_INT);
	}

	if (!interrupt_type)
		interrupt_type |= BIT(THC_UNKNOWN_INT);

	return interrupt_type;
}
EXPORT_SYMBOL_NS_GPL(thc_interrupt_handler, "INTEL_THC");

/**
 * thc_port_select - Set THC port type
 *
 * @dev: The pointer of THC private device context
 * @port_type: THC port type to use for current device
 *
 * Return: 0 on success, other error codes on failed.
 */
int thc_port_select(struct thc_device *dev, enum thc_port_type port_type)
{
	u32 ctrl, mask;

	if (port_type == THC_PORT_TYPE_SPI) {
		dev_dbg(dev->dev, "Set THC port type to SPI\n");
		dev->port_type = THC_PORT_TYPE_SPI;

		/* Enable delay of CS assertion and set to default value */
		ctrl = THC_M_PRT_SPI_DUTYC_CFG_SPI_CSA_CK_DELAY_EN |
		       FIELD_PREP(THC_M_PRT_SPI_DUTYC_CFG_SPI_CSA_CK_DELAY_VAL,
				  THC_CSA_CK_DELAY_VAL_DEFAULT);
		mask = THC_M_PRT_SPI_DUTYC_CFG_SPI_CSA_CK_DELAY_EN |
		       THC_M_PRT_SPI_DUTYC_CFG_SPI_CSA_CK_DELAY_VAL;
		regmap_write_bits(dev->thc_regmap, THC_M_PRT_SPI_DUTYC_CFG_OFFSET,
				  mask, ctrl);
	} else if (port_type == THC_PORT_TYPE_I2C) {
		dev_dbg(dev->dev, "Set THC port type to I2C\n");
		dev->port_type = THC_PORT_TYPE_I2C;

		/* Set THC transition arbitration policy to frame boundary for I2C */
		ctrl = FIELD_PREP(THC_M_PRT_CONTROL_THC_ARB_POLICY,
				  THC_ARB_POLICY_FRAME_BOUNDARY);
		mask = THC_M_PRT_CONTROL_THC_ARB_POLICY;

		regmap_write_bits(dev->thc_regmap, THC_M_PRT_CONTROL_OFFSET, mask, ctrl);
	} else {
		dev_err(dev->dev, "unsupported THC port type: %d\n", port_type);
		return -EINVAL;
	}

	ctrl = FIELD_PREP(THC_M_PRT_CONTROL_PORT_TYPE, port_type);
	mask = THC_M_PRT_CONTROL_PORT_TYPE;

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_CONTROL_OFFSET, mask, ctrl);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(thc_port_select, "INTEL_THC");

#define THC_SPI_FREQUENCY_7M	7812500
#define THC_SPI_FREQUENCY_15M	15625000
#define THC_SPI_FREQUENCY_17M	17857100
#define THC_SPI_FREQUENCY_20M	20833000
#define THC_SPI_FREQUENCY_25M	25000000
#define THC_SPI_FREQUENCY_31M	31250000
#define THC_SPI_FREQUENCY_41M	41666700

#define THC_SPI_LOW_FREQUENCY	THC_SPI_FREQUENCY_17M

static u8 thc_get_spi_freq_div_val(struct thc_device *dev, u32 spi_freq_val)
{
	static const int frequency[] = {
		THC_SPI_FREQUENCY_7M,
		THC_SPI_FREQUENCY_15M,
		THC_SPI_FREQUENCY_17M,
		THC_SPI_FREQUENCY_20M,
		THC_SPI_FREQUENCY_25M,
		THC_SPI_FREQUENCY_31M,
		THC_SPI_FREQUENCY_41M,
	};
	static const u8 frequency_div[] = {
		THC_SPI_FRQ_DIV_2,
		THC_SPI_FRQ_DIV_1,
		THC_SPI_FRQ_DIV_7,
		THC_SPI_FRQ_DIV_6,
		THC_SPI_FRQ_DIV_5,
		THC_SPI_FRQ_DIV_4,
		THC_SPI_FRQ_DIV_3,
	};
	int size = ARRAY_SIZE(frequency);
	u32 closest_freq;
	u8 freq_div;
	int i;

	for (i = size - 1; i >= 0; i--)
		if ((int)spi_freq_val - frequency[i] >= 0)
			break;

	if (i < 0) {
		dev_err_once(dev->dev, "Not supported SPI frequency %d\n", spi_freq_val);
		return THC_SPI_FRQ_RESERVED;
	}

	closest_freq = frequency[i];
	freq_div = frequency_div[i];

	dev_dbg(dev->dev,
		"Setting SPI frequency: spi_freq_val = %u, Closest freq = %u\n",
		spi_freq_val, closest_freq);

	return freq_div;
}

/**
 * thc_spi_read_config - Configure SPI bus read attributes
 *
 * @dev: The pointer of THC private device context
 * @spi_freq_val: SPI read frequecy value
 * @io_mode: SPI read IO mode
 * @opcode: Read opcode
 * @spi_rd_mps: SPI read max packet size
 *
 * Return: 0 on success, other error codes on failed.
 */
int thc_spi_read_config(struct thc_device *dev, u32 spi_freq_val,
			u32 io_mode, u32 opcode, u32 spi_rd_mps)
{
	bool is_low_freq = false;
	u32 cfg, mask;
	u8 freq_div;

	freq_div = thc_get_spi_freq_div_val(dev, spi_freq_val);
	if (freq_div == THC_SPI_FRQ_RESERVED)
		return -EINVAL;

	if (spi_freq_val < THC_SPI_LOW_FREQUENCY)
		is_low_freq = true;

	cfg = FIELD_PREP(THC_M_PRT_SPI_CFG_SPI_TCRF, freq_div) |
	      FIELD_PREP(THC_M_PRT_SPI_CFG_SPI_TRMODE, io_mode) |
	      (is_low_freq ? THC_M_PRT_SPI_CFG_SPI_LOW_FREQ_EN : 0) |
	      FIELD_PREP(THC_M_PRT_SPI_CFG_SPI_RD_MPS, spi_rd_mps);
	mask = THC_M_PRT_SPI_CFG_SPI_TCRF |
	       THC_M_PRT_SPI_CFG_SPI_TRMODE |
	       THC_M_PRT_SPI_CFG_SPI_LOW_FREQ_EN |
	       THC_M_PRT_SPI_CFG_SPI_RD_MPS;

	regmap_write_bits(dev->thc_regmap,
			  THC_M_PRT_SPI_CFG_OFFSET, mask, cfg);

	if (io_mode == THC_QUAD_IO)
		opcode = FIELD_PREP(THC_M_PRT_SPI_ICRRD_OPCODE_SPI_QIO, opcode);
	else if (io_mode == THC_DUAL_IO)
		opcode = FIELD_PREP(THC_M_PRT_SPI_ICRRD_OPCODE_SPI_DIO, opcode);
	else
		opcode = FIELD_PREP(THC_M_PRT_SPI_ICRRD_OPCODE_SPI_SIO, opcode);

	regmap_write(dev->thc_regmap, THC_M_PRT_SPI_ICRRD_OPCODE_OFFSET, opcode);
	regmap_write(dev->thc_regmap, THC_M_PRT_SPI_DMARD_OPCODE_OFFSET, opcode);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(thc_spi_read_config, "INTEL_THC");

/**
 * thc_spi_write_config - Configure SPI bus write attributes
 *
 * @dev: The pointer of THC private device context
 * @spi_freq_val: SPI write frequecy value
 * @io_mode: SPI write IO mode
 * @opcode: Write opcode
 * @spi_wr_mps: SPI write max packet size
 * @perf_limit: Performance limitation in unit of 10us
 *
 * Return: 0 on success, other error codes on failed.
 */
int thc_spi_write_config(struct thc_device *dev, u32 spi_freq_val,
			 u32 io_mode, u32 opcode, u32 spi_wr_mps,
			 u32 perf_limit)
{
	bool is_low_freq = false;
	u32 cfg, mask;
	u8 freq_div;

	freq_div = thc_get_spi_freq_div_val(dev, spi_freq_val);
	if (freq_div == THC_SPI_FRQ_RESERVED)
		return -EINVAL;

	if (spi_freq_val < THC_SPI_LOW_FREQUENCY)
		is_low_freq = true;

	cfg = FIELD_PREP(THC_M_PRT_SPI_CFG_SPI_TCWF, freq_div) |
	      FIELD_PREP(THC_M_PRT_SPI_CFG_SPI_TWMODE, io_mode) |
	      (is_low_freq ? THC_M_PRT_SPI_CFG_SPI_LOW_FREQ_EN : 0) |
	      FIELD_PREP(THC_M_PRT_SPI_CFG_SPI_WR_MPS, spi_wr_mps);
	mask = THC_M_PRT_SPI_CFG_SPI_TCWF |
	       THC_M_PRT_SPI_CFG_SPI_TWMODE |
	       THC_M_PRT_SPI_CFG_SPI_LOW_FREQ_EN |
	       THC_M_PRT_SPI_CFG_SPI_WR_MPS;

	regmap_write_bits(dev->thc_regmap,
			  THC_M_PRT_SPI_CFG_OFFSET, mask, cfg);

	if (io_mode == THC_QUAD_IO)
		opcode = FIELD_PREP(THC_M_PRT_SPI_ICRRD_OPCODE_SPI_QIO, opcode);
	else if (io_mode == THC_DUAL_IO)
		opcode = FIELD_PREP(THC_M_PRT_SPI_ICRRD_OPCODE_SPI_DIO, opcode);
	else
		opcode = FIELD_PREP(THC_M_PRT_SPI_ICRRD_OPCODE_SPI_SIO, opcode);

	regmap_write(dev->thc_regmap, THC_M_PRT_SPI_WR_OPCODE_OFFSET, opcode);

	dev->perf_limit = perf_limit;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(thc_spi_write_config, "INTEL_THC");

/**
 * thc_spi_input_output_address_config - Configure SPI input and output addresses
 *
 * @dev: the pointer of THC private device context
 * @input_hdr_addr: input report header address
 * @input_bdy_addr: input report body address
 * @output_addr: output report address
 */
void thc_spi_input_output_address_config(struct thc_device *dev, u32 input_hdr_addr,
					 u32 input_bdy_addr, u32 output_addr)
{
	regmap_write(dev->thc_regmap,
		     THC_M_PRT_DEV_INT_CAUSE_ADDR_OFFSET, input_hdr_addr);
	regmap_write(dev->thc_regmap,
		     THC_M_PRT_RD_BULK_ADDR_1_OFFSET, input_bdy_addr);
	regmap_write(dev->thc_regmap,
		     THC_M_PRT_RD_BULK_ADDR_2_OFFSET, input_bdy_addr);
	regmap_write(dev->thc_regmap,
		     THC_M_PRT_WR_BULK_ADDR_OFFSET, output_addr);
}
EXPORT_SYMBOL_NS_GPL(thc_spi_input_output_address_config, "INTEL_THC");

static int thc_i2c_subip_pio_read(struct thc_device *dev, const u32 address,
				  u32 *size, u32 *buffer)
{
	int ret;

	if (!size || *size == 0 || !buffer) {
		dev_err(dev->dev, "Invalid input parameters, size %p, buffer %p\n",
			size, buffer);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&dev->thc_bus_lock))
		return -EINTR;

	ret = prepare_pio(dev, THC_PIO_OP_I2C_SUBSYSTEM_READ, address, *size);
	if (ret < 0)
		goto end;

	pio_start(dev, 0, NULL);

	ret = pio_wait(dev);
	if (ret < 0)
		goto end;

	ret = pio_complete(dev, buffer, size);
	if (ret < 0)
		goto end;

end:
	mutex_unlock(&dev->thc_bus_lock);

	if (ret)
		dev_err_once(dev->dev, "Read THC I2C SubIP register failed %d, offset %u\n",
			     ret, address);

	return ret;
}

static int thc_i2c_subip_pio_write(struct thc_device *dev, const u32 address,
				   const u32 size, const u32 *buffer)
{
	int ret;

	if (size == 0 || !buffer) {
		dev_err(dev->dev, "Invalid input parameters, size %u, buffer %p\n",
			size, buffer);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&dev->thc_bus_lock))
		return -EINTR;

	ret = prepare_pio(dev, THC_PIO_OP_I2C_SUBSYSTEM_WRITE, address, size);
	if (ret < 0)
		goto end;

	pio_start(dev, size, buffer);

	ret = pio_wait(dev);
	if (ret < 0)
		goto end;

	ret = pio_complete(dev, NULL, NULL);
	if (ret < 0)
		goto end;

end:
	mutex_unlock(&dev->thc_bus_lock);

	if (ret)
		dev_err_once(dev->dev, "Write THC I2C SubIP register failed %d, offset %u\n",
			     ret, address);

	return ret;
}

#define I2C_SUBIP_CON_DEFAULT		0x663
#define I2C_SUBIP_INT_MASK_DEFAULT	0x7FFF
#define I2C_SUBIP_RX_TL_DEFAULT		62
#define I2C_SUBIP_TX_TL_DEFAULT		0
#define I2C_SUBIP_DMA_TDLR_DEFAULT	7
#define I2C_SUBIP_DMA_RDLR_DEFAULT	7

static int thc_i2c_subip_set_speed(struct thc_device *dev, const u32 speed,
				   const u32 hcnt, const u32 lcnt)
{
	u32 hcnt_offset, lcnt_offset;
	u32 val;
	int ret;

	switch (speed) {
	case THC_I2C_STANDARD:
		hcnt_offset = THC_I2C_IC_SS_SCL_HCNT_OFFSET;
		lcnt_offset = THC_I2C_IC_SS_SCL_LCNT_OFFSET;
		break;

	case THC_I2C_FAST_AND_PLUS:
		hcnt_offset = THC_I2C_IC_FS_SCL_HCNT_OFFSET;
		lcnt_offset = THC_I2C_IC_FS_SCL_LCNT_OFFSET;
		break;

	case THC_I2C_HIGH_SPEED:
		hcnt_offset = THC_I2C_IC_HS_SCL_HCNT_OFFSET;
		lcnt_offset = THC_I2C_IC_HS_SCL_LCNT_OFFSET;
		break;

	default:
		dev_err_once(dev->dev, "Unsupported i2c speed %d\n", speed);
		ret = -EINVAL;
		return ret;
	}

	ret = thc_i2c_subip_pio_write(dev, hcnt_offset, sizeof(u32), &hcnt);
	if (ret < 0)
		return ret;

	ret = thc_i2c_subip_pio_write(dev, lcnt_offset, sizeof(u32), &lcnt);
	if (ret < 0)
		return ret;

	val = I2C_SUBIP_CON_DEFAULT & ~THC_I2C_IC_CON_SPEED;
	val |= FIELD_PREP(THC_I2C_IC_CON_SPEED, speed);
	ret = thc_i2c_subip_pio_write(dev, THC_I2C_IC_CON_OFFSET, sizeof(u32), &val);
	if (ret < 0)
		return ret;

	return 0;
}

static u32 i2c_subip_regs[] = {
	THC_I2C_IC_CON_OFFSET,
	THC_I2C_IC_TAR_OFFSET,
	THC_I2C_IC_INTR_MASK_OFFSET,
	THC_I2C_IC_RX_TL_OFFSET,
	THC_I2C_IC_TX_TL_OFFSET,
	THC_I2C_IC_DMA_CR_OFFSET,
	THC_I2C_IC_DMA_TDLR_OFFSET,
	THC_I2C_IC_DMA_RDLR_OFFSET,
	THC_I2C_IC_SS_SCL_HCNT_OFFSET,
	THC_I2C_IC_SS_SCL_LCNT_OFFSET,
	THC_I2C_IC_FS_SCL_HCNT_OFFSET,
	THC_I2C_IC_FS_SCL_LCNT_OFFSET,
	THC_I2C_IC_HS_SCL_HCNT_OFFSET,
	THC_I2C_IC_HS_SCL_LCNT_OFFSET,
	THC_I2C_IC_ENABLE_OFFSET,
};

/**
 * thc_i2c_subip_init - Initialize and configure THC I2C subsystem
 *
 * @dev: The pointer of THC private device context
 * @target_address: Slave address of touch device (TIC)
 * @speed: I2C bus frequency speed mode
 * @hcnt: I2C clock SCL high count
 * @lcnt: I2C clock SCL low count
 *
 * Return: 0 on success, other error codes on failed.
 */
int thc_i2c_subip_init(struct thc_device *dev, const u32 target_address,
		       const u32 speed, const u32 hcnt, const u32 lcnt)
{
	u32 read_size = sizeof(u32);
	u32 val;
	int ret;

	ret = thc_i2c_subip_pio_read(dev, THC_I2C_IC_ENABLE_OFFSET, &read_size, &val);
	if (ret < 0)
		return ret;

	val &= ~THC_I2C_IC_ENABLE_ENABLE;
	ret = thc_i2c_subip_pio_write(dev, THC_I2C_IC_ENABLE_OFFSET, sizeof(u32), &val);
	if (ret < 0)
		return ret;

	ret = thc_i2c_subip_pio_read(dev, THC_I2C_IC_TAR_OFFSET, &read_size, &val);
	if (ret < 0)
		return ret;

	val &= ~THC_I2C_IC_TAR_IC_TAR;
	val |= FIELD_PREP(THC_I2C_IC_TAR_IC_TAR, target_address);
	ret = thc_i2c_subip_pio_write(dev, THC_I2C_IC_TAR_OFFSET, sizeof(u32), &val);
	if (ret < 0)
		return ret;

	ret = thc_i2c_subip_set_speed(dev, speed, hcnt, lcnt);
	if (ret < 0)
		return ret;

	val = I2C_SUBIP_INT_MASK_DEFAULT;
	ret = thc_i2c_subip_pio_write(dev, THC_I2C_IC_INTR_MASK_OFFSET, sizeof(u32), &val);
	if (ret < 0)
		return ret;

	val = I2C_SUBIP_RX_TL_DEFAULT;
	ret = thc_i2c_subip_pio_write(dev, THC_I2C_IC_RX_TL_OFFSET, sizeof(u32), &val);
	if (ret < 0)
		return ret;

	val = I2C_SUBIP_TX_TL_DEFAULT;
	ret = thc_i2c_subip_pio_write(dev, THC_I2C_IC_TX_TL_OFFSET, sizeof(u32), &val);
	if (ret < 0)
		return ret;

	val = THC_I2C_IC_DMA_CR_RDMAE | THC_I2C_IC_DMA_CR_TDMAE;
	ret = thc_i2c_subip_pio_write(dev, THC_I2C_IC_DMA_CR_OFFSET, sizeof(u32), &val);
	if (ret < 0)
		return ret;

	val = I2C_SUBIP_DMA_TDLR_DEFAULT;
	ret = thc_i2c_subip_pio_write(dev, THC_I2C_IC_DMA_TDLR_OFFSET, sizeof(u32), &val);
	if (ret < 0)
		return ret;

	val = I2C_SUBIP_DMA_RDLR_DEFAULT;
	ret = thc_i2c_subip_pio_write(dev, THC_I2C_IC_DMA_RDLR_OFFSET, sizeof(u32), &val);
	if (ret < 0)
		return ret;

	ret = thc_i2c_subip_pio_read(dev, THC_I2C_IC_ENABLE_OFFSET, &read_size, &val);
	if (ret < 0)
		return ret;

	val |= THC_I2C_IC_ENABLE_ENABLE;
	ret = thc_i2c_subip_pio_write(dev, THC_I2C_IC_ENABLE_OFFSET, sizeof(u32), &val);
	if (ret < 0)
		return ret;

	dev->i2c_subip_regs = devm_kzalloc(dev->dev, sizeof(i2c_subip_regs), GFP_KERNEL);
	if (!dev->i2c_subip_regs)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(thc_i2c_subip_init, "INTEL_THC");

/**
 * thc_i2c_subip_regs_save - Save THC I2C sub-subsystem register values to THC device context
 *
 * @dev: The pointer of THC private device context
 *
 * Return: 0 on success, other error codes on failed.
 */
int thc_i2c_subip_regs_save(struct thc_device *dev)
{
	int ret;
	u32 read_size = sizeof(u32);

	for (int i = 0; i < ARRAY_SIZE(i2c_subip_regs); i++) {
		ret = thc_i2c_subip_pio_read(dev, i2c_subip_regs[i],
					     &read_size, &dev->i2c_subip_regs[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(thc_i2c_subip_regs_save, "INTEL_THC");

/**
 * thc_i2c_subip_regs_restore - Restore THC I2C subsystem registers from THC device context
 *
 * @dev: The pointer of THC private device context
 *
 * Return: 0 on success, other error codes on failed.
 */
int thc_i2c_subip_regs_restore(struct thc_device *dev)
{
	int ret;
	u32 write_size = sizeof(u32);

	for (int i = 0; i < ARRAY_SIZE(i2c_subip_regs); i++) {
		ret = thc_i2c_subip_pio_write(dev, i2c_subip_regs[i],
					      write_size, &dev->i2c_subip_regs[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(thc_i2c_subip_regs_restore, "INTEL_THC");

/**
 * thc_i2c_set_rx_max_size - Set I2C Rx transfer max input size
 * @dev: The pointer of THC private device context
 * @max_rx_size: Max input report packet size for input report
 *
 * Set @max_rx_size for I2C RxDMA max input size control feature.
 *
 * Return: 0 on success, other error codes on failure.
 */
int thc_i2c_set_rx_max_size(struct thc_device *dev, u32 max_rx_size)
{
	u32 val;
	int ret;

	if (!dev)
		return -EINVAL;

	if (!max_rx_size)
		return -EOPNOTSUPP;

	ret = regmap_read(dev->thc_regmap, THC_M_PRT_SW_SEQ_STS_OFFSET, &val);
	if (ret)
		return ret;

	val |= FIELD_PREP(THC_M_PRT_SPI_ICRRD_OPCODE_I2C_MAX_SIZE, max_rx_size);

	ret = regmap_write(dev->thc_regmap, THC_M_PRT_SPI_ICRRD_OPCODE_OFFSET, val);
	if (ret)
		return ret;

	dev->i2c_max_rx_size = max_rx_size;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(thc_i2c_set_rx_max_size, "INTEL_THC");

/**
 * thc_i2c_rx_max_size_enable - Enable I2C Rx max input size control
 * @dev: The pointer of THC private device context
 * @enable: Enable max input size control or not
 *
 * Enable or disable I2C RxDMA max input size control feature.
 * Max input size control only can be enabled after max input size
 * was set by thc_i2c_set_rx_max_size().
 *
 * Return: 0 on success, other error codes on failure.
 */
int thc_i2c_rx_max_size_enable(struct thc_device *dev, bool enable)
{
	u32 mask = THC_M_PRT_SPI_ICRRD_OPCODE_I2C_MAX_SIZE_EN;
	u32 val = enable ? mask : 0;
	int ret;

	if (!dev)
		return -EINVAL;

	if (!dev->i2c_max_rx_size)
		return -EOPNOTSUPP;

	ret = regmap_write_bits(dev->thc_regmap, THC_M_PRT_SPI_ICRRD_OPCODE_OFFSET, mask, val);
	if (ret)
		return ret;

	dev->i2c_max_rx_size_en = enable;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(thc_i2c_rx_max_size_enable, "INTEL_THC");

/**
 * thc_i2c_set_rx_int_delay - Set I2C Rx input interrupt delay value
 * @dev: The pointer of THC private device context
 * @delay_us: Interrupt delay value, unit is us
 *
 * Set @delay_us for I2C RxDMA input interrupt delay feature.
 *
 * Return: 0 on success, other error codes on failure.
 */
int thc_i2c_set_rx_int_delay(struct thc_device *dev, u32 delay_us)
{
	u32 val;
	int ret;

	if (!dev)
		return -EINVAL;

	if (!delay_us)
		return -EOPNOTSUPP;

	ret = regmap_read(dev->thc_regmap, THC_M_PRT_SW_SEQ_STS_OFFSET, &val);
	if (ret)
		return ret;

	/* THC hardware counts at 10us unit */
	val |= FIELD_PREP(THC_M_PRT_SPI_ICRRD_OPCODE_I2C_INTERVAL, DIV_ROUND_UP(delay_us, 10));

	ret = regmap_write(dev->thc_regmap, THC_M_PRT_SPI_ICRRD_OPCODE_OFFSET, val);
	if (ret)
		return ret;

	dev->i2c_int_delay_us = delay_us;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(thc_i2c_set_rx_int_delay, "INTEL_THC");

/**
 * thc_i2c_rx_int_delay_enable - Enable I2C Rx interrupt delay
 * @dev: The pointer of THC private device context
 * @enable: Enable interrupt delay or not
 *
 * Enable or disable I2C RxDMA input interrupt delay feature.
 * Input interrupt delay can only be enabled after interrupt delay value
 * was set by thc_i2c_set_rx_int_delay().
 *
 * Return: 0 on success, other error codes on failure.
 */
int thc_i2c_rx_int_delay_enable(struct thc_device *dev, bool enable)
{
	u32 mask = THC_M_PRT_SPI_ICRRD_OPCODE_I2C_INTERVAL_EN;
	u32 val = enable ? mask : 0;
	int ret;

	if (!dev)
		return -EINVAL;

	if (!dev->i2c_int_delay_us)
		return -EOPNOTSUPP;

	ret = regmap_write_bits(dev->thc_regmap, THC_M_PRT_SPI_ICRRD_OPCODE_OFFSET, mask, val);
	if (ret)
		return ret;

	dev->i2c_int_delay_en = enable;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(thc_i2c_rx_int_delay_enable, "INTEL_THC");

MODULE_AUTHOR("Xinpeng Sun <xinpeng.sun@intel.com>");
MODULE_AUTHOR("Even Xu <even.xu@intel.com>");

MODULE_DESCRIPTION("Intel(R) Intel THC Hardware Driver");
MODULE_LICENSE("GPL");
