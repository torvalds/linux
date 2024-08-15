// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * X-Gene SLIMpro I2C Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Author: Feng Kan <fkan@apm.com>
 * Author: Hieu Le <hnle@apm.com>
 *
 * This driver provides support for X-Gene SLIMpro I2C device access
 * using the APM X-Gene SLIMpro mailbox driver.
 */
#include <acpi/pcc.h>
#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define MAILBOX_OP_TIMEOUT		1000	/* Operation time out in ms */
#define MAILBOX_I2C_INDEX		0
#define SLIMPRO_IIC_BUS			1	/* Use I2C bus 1 only */

#define SMBUS_CMD_LEN			1
#define BYTE_DATA			1
#define WORD_DATA			2
#define BLOCK_DATA			3

#define SLIMPRO_IIC_I2C_PROTOCOL	0
#define SLIMPRO_IIC_SMB_PROTOCOL	1

#define SLIMPRO_IIC_READ		0
#define SLIMPRO_IIC_WRITE		1

#define IIC_SMB_WITHOUT_DATA_LEN	0
#define IIC_SMB_WITH_DATA_LEN		1

#define SLIMPRO_DEBUG_MSG		0
#define SLIMPRO_MSG_TYPE_SHIFT		28
#define SLIMPRO_DBG_SUBTYPE_I2C1READ	4
#define SLIMPRO_DBGMSG_TYPE_SHIFT	24
#define SLIMPRO_DBGMSG_TYPE_MASK	0x0F000000U
#define SLIMPRO_IIC_DEV_SHIFT		23
#define SLIMPRO_IIC_DEV_MASK		0x00800000U
#define SLIMPRO_IIC_DEVID_SHIFT		13
#define SLIMPRO_IIC_DEVID_MASK		0x007FE000U
#define SLIMPRO_IIC_RW_SHIFT		12
#define SLIMPRO_IIC_RW_MASK		0x00001000U
#define SLIMPRO_IIC_PROTO_SHIFT		11
#define SLIMPRO_IIC_PROTO_MASK		0x00000800U
#define SLIMPRO_IIC_ADDRLEN_SHIFT	8
#define SLIMPRO_IIC_ADDRLEN_MASK	0x00000700U
#define SLIMPRO_IIC_DATALEN_SHIFT	0
#define SLIMPRO_IIC_DATALEN_MASK	0x000000FFU

/*
 * SLIMpro I2C message encode
 *
 * dev		- Controller number (0-based)
 * chip		- I2C chip address
 * op		- SLIMPRO_IIC_READ or SLIMPRO_IIC_WRITE
 * proto	- SLIMPRO_IIC_SMB_PROTOCOL or SLIMPRO_IIC_I2C_PROTOCOL
 * addrlen	- Length of the address field
 * datalen	- Length of the data field
 */
#define SLIMPRO_IIC_ENCODE_MSG(dev, chip, op, proto, addrlen, datalen) \
	((SLIMPRO_DEBUG_MSG << SLIMPRO_MSG_TYPE_SHIFT) | \
	((SLIMPRO_DBG_SUBTYPE_I2C1READ << SLIMPRO_DBGMSG_TYPE_SHIFT) & \
	SLIMPRO_DBGMSG_TYPE_MASK) | \
	((dev << SLIMPRO_IIC_DEV_SHIFT) & SLIMPRO_IIC_DEV_MASK) | \
	((chip << SLIMPRO_IIC_DEVID_SHIFT) & SLIMPRO_IIC_DEVID_MASK) | \
	((op << SLIMPRO_IIC_RW_SHIFT) & SLIMPRO_IIC_RW_MASK) | \
	((proto << SLIMPRO_IIC_PROTO_SHIFT) & SLIMPRO_IIC_PROTO_MASK) | \
	((addrlen << SLIMPRO_IIC_ADDRLEN_SHIFT) & SLIMPRO_IIC_ADDRLEN_MASK) | \
	((datalen << SLIMPRO_IIC_DATALEN_SHIFT) & SLIMPRO_IIC_DATALEN_MASK))

#define SLIMPRO_MSG_TYPE(v)             (((v) & 0xF0000000) >> 28)

/*
 * Encode for upper address for block data
 */
#define SLIMPRO_IIC_ENCODE_FLAG_BUFADDR			0x80000000
#define SLIMPRO_IIC_ENCODE_FLAG_WITH_DATA_LEN(a)	((u32) (((a) << 30) \
								& 0x40000000))
#define SLIMPRO_IIC_ENCODE_UPPER_BUFADDR(a)		((u32) (((a) >> 12) \
								& 0x3FF00000))
#define SLIMPRO_IIC_ENCODE_ADDR(a)			((a) & 0x000FFFFF)

#define SLIMPRO_IIC_MSG_DWORD_COUNT			3

/* PCC related defines */
#define PCC_SIGNATURE			0x50424300
#define PCC_STS_CMD_COMPLETE		BIT(0)
#define PCC_STS_SCI_DOORBELL		BIT(1)
#define PCC_STS_ERR			BIT(2)
#define PCC_STS_PLAT_NOTIFY		BIT(3)
#define PCC_CMD_GENERATE_DB_INT		BIT(15)

struct slimpro_i2c_dev {
	struct i2c_adapter adapter;
	struct device *dev;
	struct mbox_chan *mbox_chan;
	struct pcc_mbox_chan *pcc_chan;
	struct mbox_client mbox_client;
	int mbox_idx;
	struct completion rd_complete;
	u8 dma_buffer[I2C_SMBUS_BLOCK_MAX + 1]; /* dma_buffer[0] is used for length */
	u32 *resp_msg;
	phys_addr_t comm_base_addr;
	void *pcc_comm_addr;
};

#define to_slimpro_i2c_dev(cl)	\
		container_of(cl, struct slimpro_i2c_dev, mbox_client)

enum slimpro_i2c_version {
	XGENE_SLIMPRO_I2C_V1 = 0,
	XGENE_SLIMPRO_I2C_V2 = 1,
};

/*
 * This function tests and clears a bitmask then returns its old value
 */
static u16 xgene_word_tst_and_clr(u16 *addr, u16 mask)
{
	u16 ret, val;

	val = le16_to_cpu(READ_ONCE(*addr));
	ret = val & mask;
	val &= ~mask;
	WRITE_ONCE(*addr, cpu_to_le16(val));

	return ret;
}

static void slimpro_i2c_rx_cb(struct mbox_client *cl, void *mssg)
{
	struct slimpro_i2c_dev *ctx = to_slimpro_i2c_dev(cl);

	/*
	 * Response message format:
	 * mssg[0] is the return code of the operation
	 * mssg[1] is the first data word
	 * mssg[2] is NOT used
	 */
	if (ctx->resp_msg)
		*ctx->resp_msg = ((u32 *)mssg)[1];

	if (ctx->mbox_client.tx_block)
		complete(&ctx->rd_complete);
}

static void slimpro_i2c_pcc_rx_cb(struct mbox_client *cl, void *msg)
{
	struct slimpro_i2c_dev *ctx = to_slimpro_i2c_dev(cl);
	struct acpi_pcct_shared_memory *generic_comm_base = ctx->pcc_comm_addr;

	/* Check if platform sends interrupt */
	if (!xgene_word_tst_and_clr(&generic_comm_base->status,
				    PCC_STS_SCI_DOORBELL))
		return;

	if (xgene_word_tst_and_clr(&generic_comm_base->status,
				   PCC_STS_CMD_COMPLETE)) {
		msg = generic_comm_base + 1;

		/* Response message msg[1] contains the return value. */
		if (ctx->resp_msg)
			*ctx->resp_msg = ((u32 *)msg)[1];

		complete(&ctx->rd_complete);
	}
}

static void slimpro_i2c_pcc_tx_prepare(struct slimpro_i2c_dev *ctx, u32 *msg)
{
	struct acpi_pcct_shared_memory *generic_comm_base = ctx->pcc_comm_addr;
	u32 *ptr = (void *)(generic_comm_base + 1);
	u16 status;
	int i;

	WRITE_ONCE(generic_comm_base->signature,
		   cpu_to_le32(PCC_SIGNATURE | ctx->mbox_idx));

	WRITE_ONCE(generic_comm_base->command,
		   cpu_to_le16(SLIMPRO_MSG_TYPE(msg[0]) | PCC_CMD_GENERATE_DB_INT));

	status = le16_to_cpu(READ_ONCE(generic_comm_base->status));
	status &= ~PCC_STS_CMD_COMPLETE;
	WRITE_ONCE(generic_comm_base->status, cpu_to_le16(status));

	/* Copy the message to the PCC comm space */
	for (i = 0; i < SLIMPRO_IIC_MSG_DWORD_COUNT; i++)
		WRITE_ONCE(ptr[i], cpu_to_le32(msg[i]));
}

static int start_i2c_msg_xfer(struct slimpro_i2c_dev *ctx)
{
	if (ctx->mbox_client.tx_block || !acpi_disabled) {
		if (!wait_for_completion_timeout(&ctx->rd_complete,
						 msecs_to_jiffies(MAILBOX_OP_TIMEOUT)))
			return -ETIMEDOUT;
	}

	/* Check of invalid data or no device */
	if (*ctx->resp_msg == 0xffffffff)
		return -ENODEV;

	return 0;
}

static int slimpro_i2c_send_msg(struct slimpro_i2c_dev *ctx,
				u32 *msg,
				u32 *data)
{
	int rc;

	ctx->resp_msg = data;

	if (!acpi_disabled) {
		reinit_completion(&ctx->rd_complete);
		slimpro_i2c_pcc_tx_prepare(ctx, msg);
	}

	rc = mbox_send_message(ctx->mbox_chan, msg);
	if (rc < 0)
		goto err;

	rc = start_i2c_msg_xfer(ctx);

err:
	if (!acpi_disabled)
		mbox_chan_txdone(ctx->mbox_chan, 0);

	ctx->resp_msg = NULL;

	return rc;
}

static int slimpro_i2c_rd(struct slimpro_i2c_dev *ctx, u32 chip,
			  u32 addr, u32 addrlen, u32 protocol,
			  u32 readlen, u32 *data)
{
	u32 msg[3];

	msg[0] = SLIMPRO_IIC_ENCODE_MSG(SLIMPRO_IIC_BUS, chip,
					SLIMPRO_IIC_READ, protocol, addrlen, readlen);
	msg[1] = SLIMPRO_IIC_ENCODE_ADDR(addr);
	msg[2] = 0;

	return slimpro_i2c_send_msg(ctx, msg, data);
}

static int slimpro_i2c_wr(struct slimpro_i2c_dev *ctx, u32 chip,
			  u32 addr, u32 addrlen, u32 protocol, u32 writelen,
			  u32 data)
{
	u32 msg[3];

	msg[0] = SLIMPRO_IIC_ENCODE_MSG(SLIMPRO_IIC_BUS, chip,
					SLIMPRO_IIC_WRITE, protocol, addrlen, writelen);
	msg[1] = SLIMPRO_IIC_ENCODE_ADDR(addr);
	msg[2] = data;

	return slimpro_i2c_send_msg(ctx, msg, msg);
}

static int slimpro_i2c_blkrd(struct slimpro_i2c_dev *ctx, u32 chip, u32 addr,
			     u32 addrlen, u32 protocol, u32 readlen,
			     u32 with_data_len, void *data)
{
	dma_addr_t paddr;
	u32 msg[3];
	int rc;

	paddr = dma_map_single(ctx->dev, ctx->dma_buffer, readlen, DMA_FROM_DEVICE);
	if (dma_mapping_error(ctx->dev, paddr)) {
		dev_err(&ctx->adapter.dev, "Error in mapping dma buffer %p\n",
			ctx->dma_buffer);
		return -ENOMEM;
	}

	msg[0] = SLIMPRO_IIC_ENCODE_MSG(SLIMPRO_IIC_BUS, chip, SLIMPRO_IIC_READ,
					protocol, addrlen, readlen);
	msg[1] = SLIMPRO_IIC_ENCODE_FLAG_BUFADDR |
		 SLIMPRO_IIC_ENCODE_FLAG_WITH_DATA_LEN(with_data_len) |
		 SLIMPRO_IIC_ENCODE_UPPER_BUFADDR(paddr) |
		 SLIMPRO_IIC_ENCODE_ADDR(addr);
	msg[2] = (u32)paddr;

	rc = slimpro_i2c_send_msg(ctx, msg, msg);

	/* Copy to destination */
	memcpy(data, ctx->dma_buffer, readlen);

	dma_unmap_single(ctx->dev, paddr, readlen, DMA_FROM_DEVICE);
	return rc;
}

static int slimpro_i2c_blkwr(struct slimpro_i2c_dev *ctx, u32 chip,
			     u32 addr, u32 addrlen, u32 protocol, u32 writelen,
			     void *data)
{
	dma_addr_t paddr;
	u32 msg[3];
	int rc;

	if (writelen > I2C_SMBUS_BLOCK_MAX)
		return -EINVAL;

	memcpy(ctx->dma_buffer, data, writelen);
	paddr = dma_map_single(ctx->dev, ctx->dma_buffer, writelen,
			       DMA_TO_DEVICE);
	if (dma_mapping_error(ctx->dev, paddr)) {
		dev_err(&ctx->adapter.dev, "Error in mapping dma buffer %p\n",
			ctx->dma_buffer);
		return -ENOMEM;
	}

	msg[0] = SLIMPRO_IIC_ENCODE_MSG(SLIMPRO_IIC_BUS, chip, SLIMPRO_IIC_WRITE,
					protocol, addrlen, writelen);
	msg[1] = SLIMPRO_IIC_ENCODE_FLAG_BUFADDR |
		 SLIMPRO_IIC_ENCODE_UPPER_BUFADDR(paddr) |
		 SLIMPRO_IIC_ENCODE_ADDR(addr);
	msg[2] = (u32)paddr;

	if (ctx->mbox_client.tx_block)
		reinit_completion(&ctx->rd_complete);

	rc = slimpro_i2c_send_msg(ctx, msg, msg);

	dma_unmap_single(ctx->dev, paddr, writelen, DMA_TO_DEVICE);
	return rc;
}

static int xgene_slimpro_i2c_xfer(struct i2c_adapter *adap, u16 addr,
				  unsigned short flags, char read_write,
				  u8 command, int size,
				  union i2c_smbus_data *data)
{
	struct slimpro_i2c_dev *ctx = i2c_get_adapdata(adap);
	int ret = -EOPNOTSUPP;
	u32 val;

	switch (size) {
	case I2C_SMBUS_BYTE:
		if (read_write == I2C_SMBUS_READ) {
			ret = slimpro_i2c_rd(ctx, addr, 0, 0,
					     SLIMPRO_IIC_SMB_PROTOCOL,
					     BYTE_DATA, &val);
			data->byte = val;
		} else {
			ret = slimpro_i2c_wr(ctx, addr, command, SMBUS_CMD_LEN,
					     SLIMPRO_IIC_SMB_PROTOCOL,
					     0, 0);
		}
		break;
	case I2C_SMBUS_BYTE_DATA:
		if (read_write == I2C_SMBUS_READ) {
			ret = slimpro_i2c_rd(ctx, addr, command, SMBUS_CMD_LEN,
					     SLIMPRO_IIC_SMB_PROTOCOL,
					     BYTE_DATA, &val);
			data->byte = val;
		} else {
			val = data->byte;
			ret = slimpro_i2c_wr(ctx, addr, command, SMBUS_CMD_LEN,
					     SLIMPRO_IIC_SMB_PROTOCOL,
					     BYTE_DATA, val);
		}
		break;
	case I2C_SMBUS_WORD_DATA:
		if (read_write == I2C_SMBUS_READ) {
			ret = slimpro_i2c_rd(ctx, addr, command, SMBUS_CMD_LEN,
					     SLIMPRO_IIC_SMB_PROTOCOL,
					     WORD_DATA, &val);
			data->word = val;
		} else {
			val = data->word;
			ret = slimpro_i2c_wr(ctx, addr, command, SMBUS_CMD_LEN,
					     SLIMPRO_IIC_SMB_PROTOCOL,
					     WORD_DATA, val);
		}
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			ret = slimpro_i2c_blkrd(ctx, addr, command,
						SMBUS_CMD_LEN,
						SLIMPRO_IIC_SMB_PROTOCOL,
						I2C_SMBUS_BLOCK_MAX + 1,
						IIC_SMB_WITH_DATA_LEN,
						&data->block[0]);

		} else {
			ret = slimpro_i2c_blkwr(ctx, addr, command,
						SMBUS_CMD_LEN,
						SLIMPRO_IIC_SMB_PROTOCOL,
						data->block[0] + 1,
						&data->block[0]);
		}
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			ret = slimpro_i2c_blkrd(ctx, addr,
						command,
						SMBUS_CMD_LEN,
						SLIMPRO_IIC_I2C_PROTOCOL,
						I2C_SMBUS_BLOCK_MAX,
						IIC_SMB_WITHOUT_DATA_LEN,
						&data->block[1]);
		} else {
			ret = slimpro_i2c_blkwr(ctx, addr, command,
						SMBUS_CMD_LEN,
						SLIMPRO_IIC_I2C_PROTOCOL,
						data->block[0],
						&data->block[1]);
		}
		break;
	default:
		break;
	}
	return ret;
}

/*
* Return list of supported functionality.
*/
static u32 xgene_slimpro_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_BLOCK_DATA |
		I2C_FUNC_SMBUS_I2C_BLOCK;
}

static const struct i2c_algorithm xgene_slimpro_i2c_algorithm = {
	.smbus_xfer = xgene_slimpro_i2c_xfer,
	.functionality = xgene_slimpro_i2c_func,
};

static int xgene_slimpro_i2c_probe(struct platform_device *pdev)
{
	struct slimpro_i2c_dev *ctx;
	struct i2c_adapter *adapter;
	struct mbox_client *cl;
	int rc;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = &pdev->dev;
	platform_set_drvdata(pdev, ctx);
	cl = &ctx->mbox_client;

	/* Request mailbox channel */
	cl->dev = &pdev->dev;
	init_completion(&ctx->rd_complete);
	cl->tx_tout = MAILBOX_OP_TIMEOUT;
	cl->knows_txdone = false;
	if (acpi_disabled) {
		cl->tx_block = true;
		cl->rx_callback = slimpro_i2c_rx_cb;
		ctx->mbox_chan = mbox_request_channel(cl, MAILBOX_I2C_INDEX);
		if (IS_ERR(ctx->mbox_chan)) {
			dev_err(&pdev->dev, "i2c mailbox channel request failed\n");
			return PTR_ERR(ctx->mbox_chan);
		}
	} else {
		struct pcc_mbox_chan *pcc_chan;
		const struct acpi_device_id *acpi_id;
		int version = XGENE_SLIMPRO_I2C_V1;

		acpi_id = acpi_match_device(pdev->dev.driver->acpi_match_table,
					    &pdev->dev);
		if (!acpi_id)
			return -EINVAL;

		version = (int)acpi_id->driver_data;

		if (device_property_read_u32(&pdev->dev, "pcc-channel",
					     &ctx->mbox_idx))
			ctx->mbox_idx = MAILBOX_I2C_INDEX;

		cl->tx_block = false;
		cl->rx_callback = slimpro_i2c_pcc_rx_cb;
		pcc_chan = pcc_mbox_request_channel(cl, ctx->mbox_idx);
		if (IS_ERR(pcc_chan)) {
			dev_err(&pdev->dev, "PCC mailbox channel request failed\n");
			return PTR_ERR(pcc_chan);
		}

		ctx->pcc_chan = pcc_chan;
		ctx->mbox_chan = pcc_chan->mchan;

		if (!ctx->mbox_chan->mbox->txdone_irq) {
			dev_err(&pdev->dev, "PCC IRQ not supported\n");
			rc = -ENOENT;
			goto mbox_err;
		}

		/*
		 * This is the shared communication region
		 * for the OS and Platform to communicate over.
		 */
		ctx->comm_base_addr = pcc_chan->shmem_base_addr;
		if (ctx->comm_base_addr) {
			if (version == XGENE_SLIMPRO_I2C_V2)
				ctx->pcc_comm_addr = memremap(
							ctx->comm_base_addr,
							pcc_chan->shmem_size,
							MEMREMAP_WT);
			else
				ctx->pcc_comm_addr = memremap(
							ctx->comm_base_addr,
							pcc_chan->shmem_size,
							MEMREMAP_WB);
		} else {
			dev_err(&pdev->dev, "Failed to get PCC comm region\n");
			rc = -ENOENT;
			goto mbox_err;
		}

		if (!ctx->pcc_comm_addr) {
			dev_err(&pdev->dev,
				"Failed to ioremap PCC comm region\n");
			rc = -ENOMEM;
			goto mbox_err;
		}
	}
	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rc)
		dev_warn(&pdev->dev, "Unable to set dma mask\n");

	/* Setup I2C adapter */
	adapter = &ctx->adapter;
	snprintf(adapter->name, sizeof(adapter->name), "MAILBOX I2C");
	adapter->algo = &xgene_slimpro_i2c_algorithm;
	adapter->class = I2C_CLASS_HWMON;
	adapter->dev.parent = &pdev->dev;
	adapter->dev.of_node = pdev->dev.of_node;
	ACPI_COMPANION_SET(&adapter->dev, ACPI_COMPANION(&pdev->dev));
	i2c_set_adapdata(adapter, ctx);
	rc = i2c_add_adapter(adapter);
	if (rc)
		goto mbox_err;

	dev_info(&pdev->dev, "Mailbox I2C Adapter registered\n");
	return 0;

mbox_err:
	if (acpi_disabled)
		mbox_free_channel(ctx->mbox_chan);
	else
		pcc_mbox_free_channel(ctx->pcc_chan);

	return rc;
}

static int xgene_slimpro_i2c_remove(struct platform_device *pdev)
{
	struct slimpro_i2c_dev *ctx = platform_get_drvdata(pdev);

	i2c_del_adapter(&ctx->adapter);

	if (acpi_disabled)
		mbox_free_channel(ctx->mbox_chan);
	else
		pcc_mbox_free_channel(ctx->pcc_chan);

	return 0;
}

static const struct of_device_id xgene_slimpro_i2c_dt_ids[] = {
	{.compatible = "apm,xgene-slimpro-i2c" },
	{},
};
MODULE_DEVICE_TABLE(of, xgene_slimpro_i2c_dt_ids);

#ifdef CONFIG_ACPI
static const struct acpi_device_id xgene_slimpro_i2c_acpi_ids[] = {
	{"APMC0D40", XGENE_SLIMPRO_I2C_V1},
	{"APMC0D8B", XGENE_SLIMPRO_I2C_V2},
	{}
};
MODULE_DEVICE_TABLE(acpi, xgene_slimpro_i2c_acpi_ids);
#endif

static struct platform_driver xgene_slimpro_i2c_driver = {
	.probe	= xgene_slimpro_i2c_probe,
	.remove	= xgene_slimpro_i2c_remove,
	.driver	= {
		.name	= "xgene-slimpro-i2c",
		.of_match_table = of_match_ptr(xgene_slimpro_i2c_dt_ids),
		.acpi_match_table = ACPI_PTR(xgene_slimpro_i2c_acpi_ids)
	},
};

module_platform_driver(xgene_slimpro_i2c_driver);

MODULE_DESCRIPTION("APM X-Gene SLIMpro I2C driver");
MODULE_AUTHOR("Feng Kan <fkan@apm.com>");
MODULE_AUTHOR("Hieu Le <hnle@apm.com>");
MODULE_LICENSE("GPL");
