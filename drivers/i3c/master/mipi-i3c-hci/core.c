// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 *
 * Core driver code with main interface to the I3C subsystem.
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "hci.h"
#include "ext_caps.h"
#include "cmd.h"
#include "dat.h"


/*
 * Host Controller Capabilities and Operation Registers
 */

#define HCI_VERSION			0x00	/* HCI Version (in BCD) */

#define HC_CONTROL			0x04
#define HC_CONTROL_BUS_ENABLE		BIT(31)
#define HC_CONTROL_RESUME		BIT(30)
#define HC_CONTROL_ABORT		BIT(29)
#define HC_CONTROL_HALT_ON_CMD_TIMEOUT	BIT(12)
#define HC_CONTROL_HOT_JOIN_CTRL	BIT(8)	/* Hot-Join ACK/NACK Control */
#define HC_CONTROL_I2C_TARGET_PRESENT	BIT(7)
#define HC_CONTROL_PIO_MODE		BIT(6)	/* DMA/PIO Mode Selector */
#define HC_CONTROL_DATA_BIG_ENDIAN	BIT(4)
#define HC_CONTROL_IBA_INCLUDE		BIT(0)	/* Include I3C Broadcast Address */

#define MASTER_DEVICE_ADDR		0x08	/* Master Device Address */
#define MASTER_DYNAMIC_ADDR_VALID	BIT(31)	/* Dynamic Address is Valid */
#define MASTER_DYNAMIC_ADDR(v)		FIELD_PREP(GENMASK(22, 16), v)

#define HC_CAPABILITIES			0x0c
#define HC_CAP_SG_DC_EN			BIT(30)
#define HC_CAP_SG_IBI_EN		BIT(29)
#define HC_CAP_SG_CR_EN			BIT(28)
#define HC_CAP_MAX_DATA_LENGTH		GENMASK(24, 22)
#define HC_CAP_CMD_SIZE			GENMASK(21, 20)
#define HC_CAP_DIRECT_COMMANDS_EN	BIT(18)
#define HC_CAP_MULTI_LANE_EN		BIT(15)
#define HC_CAP_CMD_CCC_DEFBYTE		BIT(10)
#define HC_CAP_HDR_BT_EN		BIT(8)
#define HC_CAP_HDR_TS_EN		BIT(7)
#define HC_CAP_HDR_DDR_EN		BIT(6)
#define HC_CAP_NON_CURRENT_MASTER_CAP	BIT(5)	/* master handoff capable */
#define HC_CAP_DATA_BYTE_CFG_EN		BIT(4)	/* endian selection possible */
#define HC_CAP_AUTO_COMMAND		BIT(3)
#define HC_CAP_COMBO_COMMAND		BIT(2)

#define RESET_CONTROL			0x10
#define BUS_RESET			BIT(31)
#define BUS_RESET_TYPE			GENMASK(30, 29)
#define IBI_QUEUE_RST			BIT(5)
#define RX_FIFO_RST			BIT(4)
#define TX_FIFO_RST			BIT(3)
#define RESP_QUEUE_RST			BIT(2)
#define CMD_QUEUE_RST			BIT(1)
#define SOFT_RST			BIT(0)	/* Core Reset */

#define PRESENT_STATE			0x14
#define STATE_CURRENT_MASTER		BIT(2)

#define INTR_STATUS			0x20
#define INTR_STATUS_ENABLE		0x24
#define INTR_SIGNAL_ENABLE		0x28
#define INTR_FORCE			0x2c
#define INTR_HC_CMD_SEQ_UFLOW_STAT	BIT(12)	/* Cmd Sequence Underflow */
#define INTR_HC_RESET_CANCEL		BIT(11)	/* HC Cancelled Reset */
#define INTR_HC_INTERNAL_ERR		BIT(10)	/* HC Internal Error */
#define INTR_HC_PIO			BIT(8)	/* cascaded PIO interrupt */
#define INTR_HC_RINGS			GENMASK(7, 0)

#define DAT_SECTION			0x30	/* Device Address Table */
#define DAT_ENTRY_SIZE			GENMASK(31, 28)
#define DAT_TABLE_SIZE			GENMASK(18, 12)
#define DAT_TABLE_OFFSET		GENMASK(11, 0)

#define DCT_SECTION			0x34	/* Device Characteristics Table */
#define DCT_ENTRY_SIZE			GENMASK(31, 28)
#define DCT_TABLE_INDEX			GENMASK(23, 19)
#define DCT_TABLE_SIZE			GENMASK(18, 12)
#define DCT_TABLE_OFFSET		GENMASK(11, 0)

#define RING_HEADERS_SECTION		0x38
#define RING_HEADERS_OFFSET		GENMASK(15, 0)

#define PIO_SECTION			0x3c
#define PIO_REGS_OFFSET			GENMASK(15, 0)	/* PIO Offset */

#define EXT_CAPS_SECTION		0x40
#define EXT_CAPS_OFFSET			GENMASK(15, 0)

#define IBI_NOTIFY_CTRL			0x58	/* IBI Notify Control */
#define IBI_NOTIFY_SIR_REJECTED		BIT(3)	/* Rejected Target Interrupt Request */
#define IBI_NOTIFY_MR_REJECTED		BIT(1)	/* Rejected Master Request Control */
#define IBI_NOTIFY_HJ_REJECTED		BIT(0)	/* Rejected Hot-Join Control */

#define DEV_CTX_BASE_LO			0x60
#define DEV_CTX_BASE_HI			0x64


static inline struct i3c_hci *to_i3c_hci(struct i3c_master_controller *m)
{
	return container_of(m, struct i3c_hci, master);
}

static int i3c_hci_bus_init(struct i3c_master_controller *m)
{
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_device_info info;
	int ret;

	DBG("");

	if (hci->cmd == &mipi_i3c_hci_cmd_v1) {
		ret = mipi_i3c_hci_dat_v1.init(hci);
		if (ret)
			return ret;
	}

	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		return ret;
	reg_write(MASTER_DEVICE_ADDR,
		  MASTER_DYNAMIC_ADDR(ret) | MASTER_DYNAMIC_ADDR_VALID);
	memset(&info, 0, sizeof(info));
	info.dyn_addr = ret;
	ret = i3c_master_set_info(m, &info);
	if (ret)
		return ret;

	ret = hci->io->init(hci);
	if (ret)
		return ret;

	/* Set RESP_BUF_THLD to 0(n) to get 1(n+1) response */
	if (hci->quirks & HCI_QUIRK_RESP_BUF_THLD)
		amd_set_resp_buf_thld(hci);

	reg_set(HC_CONTROL, HC_CONTROL_BUS_ENABLE);
	DBG("HC_CONTROL = %#x", reg_read(HC_CONTROL));

	return 0;
}

static void i3c_hci_bus_cleanup(struct i3c_master_controller *m)
{
	struct i3c_hci *hci = to_i3c_hci(m);
	struct platform_device *pdev = to_platform_device(m->dev.parent);

	DBG("");

	reg_clear(HC_CONTROL, HC_CONTROL_BUS_ENABLE);
	synchronize_irq(platform_get_irq(pdev, 0));
	hci->io->cleanup(hci);
	if (hci->cmd == &mipi_i3c_hci_cmd_v1)
		mipi_i3c_hci_dat_v1.cleanup(hci);
}

void mipi_i3c_hci_resume(struct i3c_hci *hci)
{
	reg_set(HC_CONTROL, HC_CONTROL_RESUME);
}

/* located here rather than pio.c because needed bits are in core reg space */
void mipi_i3c_hci_pio_reset(struct i3c_hci *hci)
{
	reg_write(RESET_CONTROL, RX_FIFO_RST | TX_FIFO_RST | RESP_QUEUE_RST);
}

/* located here rather than dct.c because needed bits are in core reg space */
void mipi_i3c_hci_dct_index_reset(struct i3c_hci *hci)
{
	reg_write(DCT_SECTION, FIELD_PREP(DCT_TABLE_INDEX, 0));
}

static int i3c_hci_send_ccc_cmd(struct i3c_master_controller *m,
				struct i3c_ccc_cmd *ccc)
{
	struct i3c_hci *hci = to_i3c_hci(m);
	struct hci_xfer *xfer;
	bool raw = !!(hci->quirks & HCI_QUIRK_RAW_CCC);
	bool prefixed = raw && !!(ccc->id & I3C_CCC_DIRECT);
	unsigned int nxfers = ccc->ndests + prefixed;
	DECLARE_COMPLETION_ONSTACK(done);
	int i, last, ret = 0;

	DBG("cmd=%#x rnw=%d ndests=%d data[0].len=%d",
	    ccc->id, ccc->rnw, ccc->ndests, ccc->dests[0].payload.len);

	xfer = hci_alloc_xfer(nxfers);
	if (!xfer)
		return -ENOMEM;

	if (prefixed) {
		xfer->data = NULL;
		xfer->data_len = 0;
		xfer->rnw = false;
		hci->cmd->prep_ccc(hci, xfer, I3C_BROADCAST_ADDR,
				   ccc->id, true);
		xfer++;
	}

	for (i = 0; i < nxfers - prefixed; i++) {
		xfer[i].data = ccc->dests[i].payload.data;
		xfer[i].data_len = ccc->dests[i].payload.len;
		xfer[i].rnw = ccc->rnw;
		ret = hci->cmd->prep_ccc(hci, &xfer[i], ccc->dests[i].addr,
					 ccc->id, raw);
		if (ret)
			goto out;
		xfer[i].cmd_desc[0] |= CMD_0_ROC;
	}
	last = i - 1;
	xfer[last].cmd_desc[0] |= CMD_0_TOC;
	xfer[last].completion = &done;

	if (prefixed)
		xfer--;

	ret = hci->io->queue_xfer(hci, xfer, nxfers);
	if (ret)
		goto out;
	if (!wait_for_completion_timeout(&done, HZ) &&
	    hci->io->dequeue_xfer(hci, xfer, nxfers)) {
		ret = -ETIME;
		goto out;
	}
	for (i = prefixed; i < nxfers; i++) {
		if (ccc->rnw)
			ccc->dests[i - prefixed].payload.len =
				RESP_DATA_LENGTH(xfer[i].response);
		switch (RESP_STATUS(xfer[i].response)) {
		case RESP_SUCCESS:
			continue;
		case RESP_ERR_ADDR_HEADER:
		case RESP_ERR_NACK:
			ccc->err = I3C_ERROR_M2;
			fallthrough;
		default:
			ret = -EIO;
			goto out;
		}
	}

	if (ccc->rnw)
		DBG("got: %*ph",
		    ccc->dests[0].payload.len, ccc->dests[0].payload.data);

out:
	hci_free_xfer(xfer, nxfers);
	return ret;
}

static int i3c_hci_daa(struct i3c_master_controller *m)
{
	struct i3c_hci *hci = to_i3c_hci(m);

	DBG("");

	return hci->cmd->perform_daa(hci);
}

static int i3c_hci_alloc_safe_xfer_buf(struct i3c_hci *hci,
				       struct hci_xfer *xfer)
{
	if (hci->io != &mipi_i3c_hci_dma ||
	    xfer->data == NULL || !is_vmalloc_addr(xfer->data))
		return 0;

	if (xfer->rnw)
		xfer->bounce_buf = kzalloc(xfer->data_len, GFP_KERNEL);
	else
		xfer->bounce_buf = kmemdup(xfer->data,
					   xfer->data_len, GFP_KERNEL);

	return xfer->bounce_buf == NULL ? -ENOMEM : 0;
}

static void i3c_hci_free_safe_xfer_buf(struct i3c_hci *hci,
				       struct hci_xfer *xfer)
{
	if (hci->io != &mipi_i3c_hci_dma || xfer->bounce_buf == NULL)
		return;

	if (xfer->rnw)
		memcpy(xfer->data, xfer->bounce_buf, xfer->data_len);

	kfree(xfer->bounce_buf);
}

static int i3c_hci_priv_xfers(struct i3c_dev_desc *dev,
			      struct i3c_priv_xfer *i3c_xfers,
			      int nxfers)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct hci_xfer *xfer;
	DECLARE_COMPLETION_ONSTACK(done);
	unsigned int size_limit;
	int i, last, ret = 0;

	DBG("nxfers = %d", nxfers);

	xfer = hci_alloc_xfer(nxfers);
	if (!xfer)
		return -ENOMEM;

	size_limit = 1U << (16 + FIELD_GET(HC_CAP_MAX_DATA_LENGTH, hci->caps));

	for (i = 0; i < nxfers; i++) {
		xfer[i].data_len = i3c_xfers[i].len;
		ret = -EFBIG;
		if (xfer[i].data_len >= size_limit)
			goto out;
		xfer[i].rnw = i3c_xfers[i].rnw;
		if (i3c_xfers[i].rnw) {
			xfer[i].data = i3c_xfers[i].data.in;
		} else {
			/* silence the const qualifier warning with a cast */
			xfer[i].data = (void *) i3c_xfers[i].data.out;
		}
		hci->cmd->prep_i3c_xfer(hci, dev, &xfer[i]);
		xfer[i].cmd_desc[0] |= CMD_0_ROC;
		ret = i3c_hci_alloc_safe_xfer_buf(hci, &xfer[i]);
		if (ret)
			goto out;
	}
	last = i - 1;
	xfer[last].cmd_desc[0] |= CMD_0_TOC;
	xfer[last].completion = &done;

	ret = hci->io->queue_xfer(hci, xfer, nxfers);
	if (ret)
		goto out;
	if (!wait_for_completion_timeout(&done, HZ) &&
	    hci->io->dequeue_xfer(hci, xfer, nxfers)) {
		ret = -ETIME;
		goto out;
	}
	for (i = 0; i < nxfers; i++) {
		if (i3c_xfers[i].rnw)
			i3c_xfers[i].len = RESP_DATA_LENGTH(xfer[i].response);
		if (RESP_STATUS(xfer[i].response) != RESP_SUCCESS) {
			ret = -EIO;
			goto out;
		}
	}

out:
	for (i = 0; i < nxfers; i++)
		i3c_hci_free_safe_xfer_buf(hci, &xfer[i]);

	hci_free_xfer(xfer, nxfers);
	return ret;
}

static int i3c_hci_i2c_xfers(struct i2c_dev_desc *dev,
			     const struct i2c_msg *i2c_xfers, int nxfers)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct hci_xfer *xfer;
	DECLARE_COMPLETION_ONSTACK(done);
	int i, last, ret = 0;

	DBG("nxfers = %d", nxfers);

	xfer = hci_alloc_xfer(nxfers);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < nxfers; i++) {
		xfer[i].data = i2c_xfers[i].buf;
		xfer[i].data_len = i2c_xfers[i].len;
		xfer[i].rnw = i2c_xfers[i].flags & I2C_M_RD;
		hci->cmd->prep_i2c_xfer(hci, dev, &xfer[i]);
		xfer[i].cmd_desc[0] |= CMD_0_ROC;
		ret = i3c_hci_alloc_safe_xfer_buf(hci, &xfer[i]);
		if (ret)
			goto out;
	}
	last = i - 1;
	xfer[last].cmd_desc[0] |= CMD_0_TOC;
	xfer[last].completion = &done;

	ret = hci->io->queue_xfer(hci, xfer, nxfers);
	if (ret)
		goto out;
	if (!wait_for_completion_timeout(&done, HZ) &&
	    hci->io->dequeue_xfer(hci, xfer, nxfers)) {
		ret = -ETIME;
		goto out;
	}
	for (i = 0; i < nxfers; i++) {
		if (RESP_STATUS(xfer[i].response) != RESP_SUCCESS) {
			ret = -EIO;
			goto out;
		}
	}

out:
	for (i = 0; i < nxfers; i++)
		i3c_hci_free_safe_xfer_buf(hci, &xfer[i]);

	hci_free_xfer(xfer, nxfers);
	return ret;
}

static int i3c_hci_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data;
	int ret;

	DBG("");

	dev_data = kzalloc(sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data)
		return -ENOMEM;
	if (hci->cmd == &mipi_i3c_hci_cmd_v1) {
		ret = mipi_i3c_hci_dat_v1.alloc_entry(hci);
		if (ret < 0) {
			kfree(dev_data);
			return ret;
		}
		mipi_i3c_hci_dat_v1.set_dynamic_addr(hci, ret, dev->info.dyn_addr);
		dev_data->dat_idx = ret;
	}
	i3c_dev_set_master_data(dev, dev_data);
	return 0;
}

static int i3c_hci_reattach_i3c_dev(struct i3c_dev_desc *dev, u8 old_dyn_addr)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);

	DBG("");

	if (hci->cmd == &mipi_i3c_hci_cmd_v1)
		mipi_i3c_hci_dat_v1.set_dynamic_addr(hci, dev_data->dat_idx,
					     dev->info.dyn_addr);
	return 0;
}

static void i3c_hci_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);

	DBG("");

	i3c_dev_set_master_data(dev, NULL);
	if (hci->cmd == &mipi_i3c_hci_cmd_v1)
		mipi_i3c_hci_dat_v1.free_entry(hci, dev_data->dat_idx);
	kfree(dev_data);
}

static int i3c_hci_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data;
	int ret;

	DBG("");

	if (hci->cmd != &mipi_i3c_hci_cmd_v1)
		return 0;
	dev_data = kzalloc(sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data)
		return -ENOMEM;
	ret = mipi_i3c_hci_dat_v1.alloc_entry(hci);
	if (ret < 0) {
		kfree(dev_data);
		return ret;
	}
	mipi_i3c_hci_dat_v1.set_static_addr(hci, ret, dev->addr);
	mipi_i3c_hci_dat_v1.set_flags(hci, ret, DAT_0_I2C_DEVICE, 0);
	dev_data->dat_idx = ret;
	i2c_dev_set_master_data(dev, dev_data);
	return 0;
}

static void i3c_hci_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data = i2c_dev_get_master_data(dev);

	DBG("");

	if (dev_data) {
		i2c_dev_set_master_data(dev, NULL);
		if (hci->cmd == &mipi_i3c_hci_cmd_v1)
			mipi_i3c_hci_dat_v1.free_entry(hci, dev_data->dat_idx);
		kfree(dev_data);
	}
}

static int i3c_hci_request_ibi(struct i3c_dev_desc *dev,
			       const struct i3c_ibi_setup *req)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);
	unsigned int dat_idx = dev_data->dat_idx;

	if (req->max_payload_len != 0)
		mipi_i3c_hci_dat_v1.set_flags(hci, dat_idx, DAT_0_IBI_PAYLOAD, 0);
	else
		mipi_i3c_hci_dat_v1.clear_flags(hci, dat_idx, DAT_0_IBI_PAYLOAD, 0);
	return hci->io->request_ibi(hci, dev, req);
}

static void i3c_hci_free_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);

	hci->io->free_ibi(hci, dev);
}

static int i3c_hci_enable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);

	mipi_i3c_hci_dat_v1.clear_flags(hci, dev_data->dat_idx, DAT_0_SIR_REJECT, 0);
	return i3c_master_enec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);
}

static int i3c_hci_disable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);

	mipi_i3c_hci_dat_v1.set_flags(hci, dev_data->dat_idx, DAT_0_SIR_REJECT, 0);
	return i3c_master_disec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);
}

static void i3c_hci_recycle_ibi_slot(struct i3c_dev_desc *dev,
				     struct i3c_ibi_slot *slot)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);

	hci->io->recycle_ibi_slot(hci, dev, slot);
}

static const struct i3c_master_controller_ops i3c_hci_ops = {
	.bus_init		= i3c_hci_bus_init,
	.bus_cleanup		= i3c_hci_bus_cleanup,
	.do_daa			= i3c_hci_daa,
	.send_ccc_cmd		= i3c_hci_send_ccc_cmd,
	.priv_xfers		= i3c_hci_priv_xfers,
	.i2c_xfers		= i3c_hci_i2c_xfers,
	.attach_i3c_dev		= i3c_hci_attach_i3c_dev,
	.reattach_i3c_dev	= i3c_hci_reattach_i3c_dev,
	.detach_i3c_dev		= i3c_hci_detach_i3c_dev,
	.attach_i2c_dev		= i3c_hci_attach_i2c_dev,
	.detach_i2c_dev		= i3c_hci_detach_i2c_dev,
	.request_ibi		= i3c_hci_request_ibi,
	.free_ibi		= i3c_hci_free_ibi,
	.enable_ibi		= i3c_hci_enable_ibi,
	.disable_ibi		= i3c_hci_disable_ibi,
	.recycle_ibi_slot	= i3c_hci_recycle_ibi_slot,
};

static irqreturn_t i3c_hci_irq_handler(int irq, void *dev_id)
{
	struct i3c_hci *hci = dev_id;
	irqreturn_t result = IRQ_NONE;
	u32 val;

	val = reg_read(INTR_STATUS);
	DBG("INTR_STATUS = %#x", val);

	if (val) {
		reg_write(INTR_STATUS, val);
	} else {
		/* v1.0 does not have PIO cascaded notification bits */
		val |= INTR_HC_PIO;
	}

	if (val & INTR_HC_RESET_CANCEL) {
		DBG("cancelled reset");
		val &= ~INTR_HC_RESET_CANCEL;
	}
	if (val & INTR_HC_INTERNAL_ERR) {
		dev_err(&hci->master.dev, "Host Controller Internal Error\n");
		val &= ~INTR_HC_INTERNAL_ERR;
	}
	if (val & INTR_HC_PIO) {
		hci->io->irq_handler(hci, 0);
		val &= ~INTR_HC_PIO;
	}
	if (val & INTR_HC_RINGS) {
		hci->io->irq_handler(hci, val & INTR_HC_RINGS);
		val &= ~INTR_HC_RINGS;
	}
	if (val)
		dev_err(&hci->master.dev, "unexpected INTR_STATUS %#x\n", val);
	else
		result = IRQ_HANDLED;

	return result;
}

static int i3c_hci_init(struct i3c_hci *hci)
{
	bool size_in_dwords, mode_selector;
	u32 regval, offset;
	int ret;

	/* Validate HCI hardware version */
	regval = reg_read(HCI_VERSION);
	hci->version_major = (regval >> 8) & 0xf;
	hci->version_minor = (regval >> 4) & 0xf;
	hci->revision = regval & 0xf;
	dev_notice(&hci->master.dev, "MIPI I3C HCI v%u.%u r%02u\n",
		   hci->version_major, hci->version_minor, hci->revision);
	/* known versions */
	switch (regval & ~0xf) {
	case 0x100:	/* version 1.0 */
	case 0x110:	/* version 1.1 */
	case 0x200:	/* version 2.0 */
		break;
	default:
		dev_err(&hci->master.dev, "unsupported HCI version\n");
		return -EPROTONOSUPPORT;
	}

	hci->caps = reg_read(HC_CAPABILITIES);
	DBG("caps = %#x", hci->caps);

	size_in_dwords = hci->version_major < 1 ||
			 (hci->version_major == 1 && hci->version_minor < 1);

	regval = reg_read(DAT_SECTION);
	offset = FIELD_GET(DAT_TABLE_OFFSET, regval);
	hci->DAT_regs = offset ? hci->base_regs + offset : NULL;
	hci->DAT_entries = FIELD_GET(DAT_TABLE_SIZE, regval);
	hci->DAT_entry_size = FIELD_GET(DAT_ENTRY_SIZE, regval) ? 0 : 8;
	if (size_in_dwords)
		hci->DAT_entries = 4 * hci->DAT_entries / hci->DAT_entry_size;
	dev_info(&hci->master.dev, "DAT: %u %u-bytes entries at offset %#x\n",
		 hci->DAT_entries, hci->DAT_entry_size, offset);

	regval = reg_read(DCT_SECTION);
	offset = FIELD_GET(DCT_TABLE_OFFSET, regval);
	hci->DCT_regs = offset ? hci->base_regs + offset : NULL;
	hci->DCT_entries = FIELD_GET(DCT_TABLE_SIZE, regval);
	hci->DCT_entry_size = FIELD_GET(DCT_ENTRY_SIZE, regval) ? 0 : 16;
	if (size_in_dwords)
		hci->DCT_entries = 4 * hci->DCT_entries / hci->DCT_entry_size;
	dev_info(&hci->master.dev, "DCT: %u %u-bytes entries at offset %#x\n",
		 hci->DCT_entries, hci->DCT_entry_size, offset);

	regval = reg_read(RING_HEADERS_SECTION);
	offset = FIELD_GET(RING_HEADERS_OFFSET, regval);
	hci->RHS_regs = offset ? hci->base_regs + offset : NULL;
	dev_info(&hci->master.dev, "Ring Headers at offset %#x\n", offset);

	regval = reg_read(PIO_SECTION);
	offset = FIELD_GET(PIO_REGS_OFFSET, regval);
	hci->PIO_regs = offset ? hci->base_regs + offset : NULL;
	dev_info(&hci->master.dev, "PIO section at offset %#x\n", offset);

	regval = reg_read(EXT_CAPS_SECTION);
	offset = FIELD_GET(EXT_CAPS_OFFSET, regval);
	hci->EXTCAPS_regs = offset ? hci->base_regs + offset : NULL;
	dev_info(&hci->master.dev, "Extended Caps at offset %#x\n", offset);

	ret = i3c_hci_parse_ext_caps(hci);
	if (ret)
		return ret;

	/*
	 * Now let's reset the hardware.
	 * SOFT_RST must be clear before we write to it.
	 * Then we must wait until it clears again.
	 */
	ret = readx_poll_timeout(reg_read, RESET_CONTROL, regval,
				 !(regval & SOFT_RST), 1, 10000);
	if (ret)
		return -ENXIO;
	reg_write(RESET_CONTROL, SOFT_RST);
	ret = readx_poll_timeout(reg_read, RESET_CONTROL, regval,
				 !(regval & SOFT_RST), 1, 10000);
	if (ret)
		return -ENXIO;

	/* Disable all interrupts and allow all signal updates */
	reg_write(INTR_SIGNAL_ENABLE, 0x0);
	reg_write(INTR_STATUS_ENABLE, 0xffffffff);

	/* Make sure our data ordering fits the host's */
	regval = reg_read(HC_CONTROL);
	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)) {
		if (!(regval & HC_CONTROL_DATA_BIG_ENDIAN)) {
			regval |= HC_CONTROL_DATA_BIG_ENDIAN;
			reg_write(HC_CONTROL, regval);
			regval = reg_read(HC_CONTROL);
			if (!(regval & HC_CONTROL_DATA_BIG_ENDIAN)) {
				dev_err(&hci->master.dev, "cannot set BE mode\n");
				return -EOPNOTSUPP;
			}
		}
	} else {
		if (regval & HC_CONTROL_DATA_BIG_ENDIAN) {
			regval &= ~HC_CONTROL_DATA_BIG_ENDIAN;
			reg_write(HC_CONTROL, regval);
			regval = reg_read(HC_CONTROL);
			if (regval & HC_CONTROL_DATA_BIG_ENDIAN) {
				dev_err(&hci->master.dev, "cannot clear BE mode\n");
				return -EOPNOTSUPP;
			}
		}
	}

	/* Select our command descriptor model */
	switch (FIELD_GET(HC_CAP_CMD_SIZE, hci->caps)) {
	case 0:
		hci->cmd = &mipi_i3c_hci_cmd_v1;
		break;
	case 1:
		hci->cmd = &mipi_i3c_hci_cmd_v2;
		break;
	default:
		dev_err(&hci->master.dev, "wrong CMD_SIZE capability value\n");
		return -EINVAL;
	}

	mode_selector = hci->version_major > 1 ||
				(hci->version_major == 1 && hci->version_minor > 0);

	/* Quirk for HCI_QUIRK_PIO_MODE on AMD platforms */
	if (hci->quirks & HCI_QUIRK_PIO_MODE)
		hci->RHS_regs = NULL;

	/* Try activating DMA operations first */
	if (hci->RHS_regs) {
		reg_clear(HC_CONTROL, HC_CONTROL_PIO_MODE);
		if (mode_selector && (reg_read(HC_CONTROL) & HC_CONTROL_PIO_MODE)) {
			dev_err(&hci->master.dev, "PIO mode is stuck\n");
			ret = -EIO;
		} else {
			hci->io = &mipi_i3c_hci_dma;
			dev_info(&hci->master.dev, "Using DMA\n");
		}
	}

	/* If no DMA, try PIO */
	if (!hci->io && hci->PIO_regs) {
		reg_set(HC_CONTROL, HC_CONTROL_PIO_MODE);
		if (mode_selector && !(reg_read(HC_CONTROL) & HC_CONTROL_PIO_MODE)) {
			dev_err(&hci->master.dev, "DMA mode is stuck\n");
			ret = -EIO;
		} else {
			hci->io = &mipi_i3c_hci_pio;
			dev_info(&hci->master.dev, "Using PIO\n");
		}
	}

	if (!hci->io) {
		dev_err(&hci->master.dev, "neither DMA nor PIO can be used\n");
		if (!ret)
			ret = -EINVAL;
		return ret;
	}

	/* Configure OD and PP timings for AMD platforms */
	if (hci->quirks & HCI_QUIRK_OD_PP_TIMING)
		amd_set_od_pp_timing(hci);

	return 0;
}

static int i3c_hci_probe(struct platform_device *pdev)
{
	struct i3c_hci *hci;
	int irq, ret;

	hci = devm_kzalloc(&pdev->dev, sizeof(*hci), GFP_KERNEL);
	if (!hci)
		return -ENOMEM;
	hci->base_regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hci->base_regs))
		return PTR_ERR(hci->base_regs);

	platform_set_drvdata(pdev, hci);
	/* temporary for dev_printk's, to be replaced in i3c_master_register */
	hci->master.dev.init_name = dev_name(&pdev->dev);

	hci->quirks = (unsigned long)device_get_match_data(&pdev->dev);

	ret = i3c_hci_init(hci);
	if (ret)
		return ret;

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq, i3c_hci_irq_handler,
			       0, NULL, hci);
	if (ret)
		return ret;

	ret = i3c_master_register(&hci->master, &pdev->dev,
				  &i3c_hci_ops, false);
	if (ret)
		return ret;

	return 0;
}

static void i3c_hci_remove(struct platform_device *pdev)
{
	struct i3c_hci *hci = platform_get_drvdata(pdev);

	i3c_master_unregister(&hci->master);
}

static const __maybe_unused struct of_device_id i3c_hci_of_match[] = {
	{ .compatible = "mipi-i3c-hci", },
	{},
};
MODULE_DEVICE_TABLE(of, i3c_hci_of_match);

static const struct acpi_device_id i3c_hci_acpi_match[] = {
	{ "AMDI5017", HCI_QUIRK_PIO_MODE | HCI_QUIRK_OD_PP_TIMING | HCI_QUIRK_RESP_BUF_THLD },
	{}
};
MODULE_DEVICE_TABLE(acpi, i3c_hci_acpi_match);

static struct platform_driver i3c_hci_driver = {
	.probe = i3c_hci_probe,
	.remove_new = i3c_hci_remove,
	.driver = {
		.name = "mipi-i3c-hci",
		.of_match_table = of_match_ptr(i3c_hci_of_match),
		.acpi_match_table = i3c_hci_acpi_match,
	},
};
module_platform_driver(i3c_hci_driver);
MODULE_ALIAS("platform:mipi-i3c-hci");

MODULE_AUTHOR("Nicolas Pitre <npitre@baylibre.com>");
MODULE_DESCRIPTION("MIPI I3C HCI driver");
MODULE_LICENSE("Dual BSD/GPL");
