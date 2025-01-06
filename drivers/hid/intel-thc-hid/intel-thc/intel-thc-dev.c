/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#include <linux/bitfield.h>
#include <linux/regmap.h>

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
			     int_quiesce ? "true" : "false");
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

MODULE_AUTHOR("Xinpeng Sun <xinpeng.sun@intel.com>");
MODULE_AUTHOR("Even Xu <even.xu@intel.com>");

MODULE_DESCRIPTION("Intel(R) Intel THC Hardware Driver");
MODULE_LICENSE("GPL");
