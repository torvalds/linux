/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2012 Intel Corporation. All rights reserved.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  Supports the SMBus Message Transport (SMT) in the Intel Atom Processor
 *  S12xx Product Family.
 *
 *  Features supported by this driver:
 *  Hardware PEC                     yes
 *  Block buffer                     yes
 *  Block process call transaction   no
 *  Slave mode                       no
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/interrupt.h>

#include <asm-generic/io-64-nonatomic-lo-hi.h>

/* PCI Address Constants */
#define SMBBAR		0

/* PCI DIDs for the Intel SMBus Message Transport (SMT) Devices */
#define PCI_DEVICE_ID_INTEL_S1200_SMT0	0x0c59
#define PCI_DEVICE_ID_INTEL_S1200_SMT1	0x0c5a

#define ISMT_DESC_ENTRIES	32	/* number of descriptor entries */
#define ISMT_MAX_RETRIES	3	/* number of SMBus retries to attempt */

/* Hardware Descriptor Constants - Control Field */
#define ISMT_DESC_CWRL	0x01	/* Command/Write Length */
#define ISMT_DESC_BLK	0X04	/* Perform Block Transaction */
#define ISMT_DESC_FAIR	0x08	/* Set fairness flag upon successful arbit. */
#define ISMT_DESC_PEC	0x10	/* Packet Error Code */
#define ISMT_DESC_I2C	0x20	/* I2C Enable */
#define ISMT_DESC_INT	0x40	/* Interrupt */
#define ISMT_DESC_SOE	0x80	/* Stop On Error */

/* Hardware Descriptor Constants - Status Field */
#define ISMT_DESC_SCS	0x01	/* Success */
#define ISMT_DESC_DLTO	0x04	/* Data Low Time Out */
#define ISMT_DESC_NAK	0x08	/* NAK Received */
#define ISMT_DESC_CRC	0x10	/* CRC Error */
#define ISMT_DESC_CLTO	0x20	/* Clock Low Time Out */
#define ISMT_DESC_COL	0x40	/* Collisions */
#define ISMT_DESC_LPR	0x80	/* Large Packet Received */

/* Macros */
#define ISMT_DESC_ADDR_RW(addr, rw) (((addr) << 1) | (rw))

/* iSMT General Register address offsets (SMBBAR + <addr>) */
#define ISMT_GR_GCTRL		0x000	/* General Control */
#define ISMT_GR_SMTICL		0x008	/* SMT Interrupt Cause Location */
#define ISMT_GR_ERRINTMSK	0x010	/* Error Interrupt Mask */
#define ISMT_GR_ERRAERMSK	0x014	/* Error AER Mask */
#define ISMT_GR_ERRSTS		0x018	/* Error Status */
#define ISMT_GR_ERRINFO		0x01c	/* Error Information */

/* iSMT Master Registers */
#define ISMT_MSTR_MDBA		0x100	/* Master Descriptor Base Address */
#define ISMT_MSTR_MCTRL		0x108	/* Master Control */
#define ISMT_MSTR_MSTS		0x10c	/* Master Status */
#define ISMT_MSTR_MDS		0x110	/* Master Descriptor Size */
#define ISMT_MSTR_RPOLICY	0x114	/* Retry Policy */

/* iSMT Miscellaneous Registers */
#define ISMT_SPGT	0x300	/* SMBus PHY Global Timing */

/* General Control Register (GCTRL) bit definitions */
#define ISMT_GCTRL_TRST	0x04	/* Target Reset */
#define ISMT_GCTRL_KILL	0x08	/* Kill */
#define ISMT_GCTRL_SRST	0x40	/* Soft Reset */

/* Master Control Register (MCTRL) bit definitions */
#define ISMT_MCTRL_SS	0x01		/* Start/Stop */
#define ISMT_MCTRL_MEIE	0x10		/* Master Error Interrupt Enable */
#define ISMT_MCTRL_FMHP	0x00ff0000	/* Firmware Master Head Ptr (FMHP) */

/* Master Status Register (MSTS) bit definitions */
#define ISMT_MSTS_HMTP	0xff0000	/* HW Master Tail Pointer (HMTP) */
#define ISMT_MSTS_MIS	0x20		/* Master Interrupt Status (MIS) */
#define ISMT_MSTS_MEIS	0x10		/* Master Error Int Status (MEIS) */
#define ISMT_MSTS_IP	0x01		/* In Progress */

/* Master Descriptor Size (MDS) bit definitions */
#define ISMT_MDS_MASK	0xff	/* Master Descriptor Size mask (MDS) */

/* SMBus PHY Global Timing Register (SPGT) bit definitions */
#define ISMT_SPGT_SPD_MASK	0xc0000000	/* SMBus Speed mask */
#define ISMT_SPGT_SPD_80K	0x00		/* 80 kHz */
#define ISMT_SPGT_SPD_100K	(0x1 << 30)	/* 100 kHz */
#define ISMT_SPGT_SPD_400K	(0x2 << 30)	/* 400 kHz */
#define ISMT_SPGT_SPD_1M	(0x3 << 30)	/* 1 MHz */


/* MSI Control Register (MSICTL) bit definitions */
#define ISMT_MSICTL_MSIE	0x01	/* MSI Enable */

/* iSMT Hardware Descriptor */
struct ismt_desc {
	u8 tgtaddr_rw;	/* target address & r/w bit */
	u8 wr_len_cmd;	/* write length in bytes or a command */
	u8 rd_len;	/* read length */
	u8 control;	/* control bits */
	u8 status;	/* status bits */
	u8 retry;	/* collision retry and retry count */
	u8 rxbytes;	/* received bytes */
	u8 txbytes;	/* transmitted bytes */
	u32 dptr_low;	/* lower 32 bit of the data pointer */
	u32 dptr_high;	/* upper 32 bit of the data pointer */
} __packed;

struct ismt_priv {
	struct i2c_adapter adapter;
	void *smba;				/* PCI BAR */
	struct pci_dev *pci_dev;
	struct ismt_desc *hw;			/* descriptor virt base addr */
	dma_addr_t io_rng_dma;			/* descriptor HW base addr */
	u8 head;				/* ring buffer head pointer */
	struct completion cmp;			/* interrupt completion */
	u8 dma_buffer[I2C_SMBUS_BLOCK_MAX + 1];	/* temp R/W data buffer */
	bool using_msi;				/* type of interrupt flag */
};

/**
 * ismt_ids - PCI device IDs supported by this driver
 */
static const DEFINE_PCI_DEVICE_TABLE(ismt_ids) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_S1200_SMT0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_S1200_SMT1) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, ismt_ids);

/* Bus speed control bits for slow debuggers - refer to the docs for usage */
static unsigned int bus_speed;
module_param(bus_speed, uint, S_IRUGO);
MODULE_PARM_DESC(bus_speed, "Bus Speed in kHz (0 = BIOS default)");

/**
 * __ismt_desc_dump() - dump the contents of a specific descriptor
 */
static void __ismt_desc_dump(struct device *dev, const struct ismt_desc *desc)
{

	dev_dbg(dev, "Descriptor struct:  %p\n", desc);
	dev_dbg(dev, "\ttgtaddr_rw=0x%02X\n", desc->tgtaddr_rw);
	dev_dbg(dev, "\twr_len_cmd=0x%02X\n", desc->wr_len_cmd);
	dev_dbg(dev, "\trd_len=    0x%02X\n", desc->rd_len);
	dev_dbg(dev, "\tcontrol=   0x%02X\n", desc->control);
	dev_dbg(dev, "\tstatus=    0x%02X\n", desc->status);
	dev_dbg(dev, "\tretry=     0x%02X\n", desc->retry);
	dev_dbg(dev, "\trxbytes=   0x%02X\n", desc->rxbytes);
	dev_dbg(dev, "\ttxbytes=   0x%02X\n", desc->txbytes);
	dev_dbg(dev, "\tdptr_low=  0x%08X\n", desc->dptr_low);
	dev_dbg(dev, "\tdptr_high= 0x%08X\n", desc->dptr_high);
}
/**
 * ismt_desc_dump() - dump the contents of a descriptor for debug purposes
 * @priv: iSMT private data
 */
static void ismt_desc_dump(struct ismt_priv *priv)
{
	struct device *dev = &priv->pci_dev->dev;
	struct ismt_desc *desc = &priv->hw[priv->head];

	dev_dbg(dev, "Dump of the descriptor struct:  0x%X\n", priv->head);
	__ismt_desc_dump(dev, desc);
}

/**
 * ismt_gen_reg_dump() - dump the iSMT General Registers
 * @priv: iSMT private data
 */
static void ismt_gen_reg_dump(struct ismt_priv *priv)
{
	struct device *dev = &priv->pci_dev->dev;

	dev_dbg(dev, "Dump of the iSMT General Registers\n");
	dev_dbg(dev, "  GCTRL.... : (0x%p)=0x%X\n",
		priv->smba + ISMT_GR_GCTRL,
		readl(priv->smba + ISMT_GR_GCTRL));
	dev_dbg(dev, "  SMTICL... : (0x%p)=0x%016llX\n",
		priv->smba + ISMT_GR_SMTICL,
		(long long unsigned int)readq(priv->smba + ISMT_GR_SMTICL));
	dev_dbg(dev, "  ERRINTMSK : (0x%p)=0x%X\n",
		priv->smba + ISMT_GR_ERRINTMSK,
		readl(priv->smba + ISMT_GR_ERRINTMSK));
	dev_dbg(dev, "  ERRAERMSK : (0x%p)=0x%X\n",
		priv->smba + ISMT_GR_ERRAERMSK,
		readl(priv->smba + ISMT_GR_ERRAERMSK));
	dev_dbg(dev, "  ERRSTS... : (0x%p)=0x%X\n",
		priv->smba + ISMT_GR_ERRSTS,
		readl(priv->smba + ISMT_GR_ERRSTS));
	dev_dbg(dev, "  ERRINFO.. : (0x%p)=0x%X\n",
		priv->smba + ISMT_GR_ERRINFO,
		readl(priv->smba + ISMT_GR_ERRINFO));
}

/**
 * ismt_mstr_reg_dump() - dump the iSMT Master Registers
 * @priv: iSMT private data
 */
static void ismt_mstr_reg_dump(struct ismt_priv *priv)
{
	struct device *dev = &priv->pci_dev->dev;

	dev_dbg(dev, "Dump of the iSMT Master Registers\n");
	dev_dbg(dev, "  MDBA..... : (0x%p)=0x%016llX\n",
		priv->smba + ISMT_MSTR_MDBA,
		(long long unsigned int)readq(priv->smba + ISMT_MSTR_MDBA));
	dev_dbg(dev, "  MCTRL.... : (0x%p)=0x%X\n",
		priv->smba + ISMT_MSTR_MCTRL,
		readl(priv->smba + ISMT_MSTR_MCTRL));
	dev_dbg(dev, "  MSTS..... : (0x%p)=0x%X\n",
		priv->smba + ISMT_MSTR_MSTS,
		readl(priv->smba + ISMT_MSTR_MSTS));
	dev_dbg(dev, "  MDS...... : (0x%p)=0x%X\n",
		priv->smba + ISMT_MSTR_MDS,
		readl(priv->smba + ISMT_MSTR_MDS));
	dev_dbg(dev, "  RPOLICY.. : (0x%p)=0x%X\n",
		priv->smba + ISMT_MSTR_RPOLICY,
		readl(priv->smba + ISMT_MSTR_RPOLICY));
	dev_dbg(dev, "  SPGT..... : (0x%p)=0x%X\n",
		priv->smba + ISMT_SPGT,
		readl(priv->smba + ISMT_SPGT));
}

/**
 * ismt_submit_desc() - add a descriptor to the ring
 * @priv: iSMT private data
 */
static void ismt_submit_desc(struct ismt_priv *priv)
{
	uint fmhp;
	uint val;

	ismt_desc_dump(priv);
	ismt_gen_reg_dump(priv);
	ismt_mstr_reg_dump(priv);

	/* Set the FMHP (Firmware Master Head Pointer)*/
	fmhp = ((priv->head + 1) % ISMT_DESC_ENTRIES) << 16;
	val = readl(priv->smba + ISMT_MSTR_MCTRL);
	writel((val & ~ISMT_MCTRL_FMHP) | fmhp,
	       priv->smba + ISMT_MSTR_MCTRL);

	/* Set the start bit */
	val = readl(priv->smba + ISMT_MSTR_MCTRL);
	writel(val | ISMT_MCTRL_SS,
	       priv->smba + ISMT_MSTR_MCTRL);
}

/**
 * ismt_process_desc() - handle the completion of the descriptor
 * @desc: the iSMT hardware descriptor
 * @data: data buffer from the upper layer
 * @priv: ismt_priv struct holding our dma buffer
 * @size: SMBus transaction type
 * @read_write: flag to indicate if this is a read or write
 */
static int ismt_process_desc(const struct ismt_desc *desc,
			     union i2c_smbus_data *data,
			     struct ismt_priv *priv, int size,
			     char read_write)
{
	u8 *dma_buffer = priv->dma_buffer;

	dev_dbg(&priv->pci_dev->dev, "Processing completed descriptor\n");
	__ismt_desc_dump(&priv->pci_dev->dev, desc);

	if (desc->status & ISMT_DESC_SCS) {
		if (read_write == I2C_SMBUS_WRITE &&
		    size != I2C_SMBUS_PROC_CALL)
			return 0;

		switch (size) {
		case I2C_SMBUS_BYTE:
		case I2C_SMBUS_BYTE_DATA:
			data->byte = dma_buffer[0];
			break;
		case I2C_SMBUS_WORD_DATA:
		case I2C_SMBUS_PROC_CALL:
			data->word = dma_buffer[0] | (dma_buffer[1] << 8);
			break;
		case I2C_SMBUS_BLOCK_DATA:
			memcpy(&data->block[1], dma_buffer, desc->rxbytes);
			data->block[0] = desc->rxbytes;
			break;
		}
		return 0;
	}

	if (likely(desc->status & ISMT_DESC_NAK))
		return -ENXIO;

	if (desc->status & ISMT_DESC_CRC)
		return -EBADMSG;

	if (desc->status & ISMT_DESC_COL)
		return -EAGAIN;

	if (desc->status & ISMT_DESC_LPR)
		return -EPROTO;

	if (desc->status & (ISMT_DESC_DLTO | ISMT_DESC_CLTO))
		return -ETIMEDOUT;

	return -EIO;
}

/**
 * ismt_access() - process an SMBus command
 * @adap: the i2c host adapter
 * @addr: address of the i2c/SMBus target
 * @flags: command options
 * @read_write: read from or write to device
 * @command: the i2c/SMBus command to issue
 * @size: SMBus transaction type
 * @data: read/write data buffer
 */
static int ismt_access(struct i2c_adapter *adap, u16 addr,
		       unsigned short flags, char read_write, u8 command,
		       int size, union i2c_smbus_data *data)
{
	int ret;
	dma_addr_t dma_addr = 0; /* address of the data buffer */
	u8 dma_size = 0;
	enum dma_data_direction dma_direction = 0;
	struct ismt_desc *desc;
	struct ismt_priv *priv = i2c_get_adapdata(adap);
	struct device *dev = &priv->pci_dev->dev;

	desc = &priv->hw[priv->head];

	/* Initialize the descriptor */
	memset(desc, 0, sizeof(struct ismt_desc));
	desc->tgtaddr_rw = ISMT_DESC_ADDR_RW(addr, read_write);

	/* Initialize common control bits */
	if (likely(priv->using_msi))
		desc->control = ISMT_DESC_INT | ISMT_DESC_FAIR;
	else
		desc->control = ISMT_DESC_FAIR;

	if ((flags & I2C_CLIENT_PEC) && (size != I2C_SMBUS_QUICK)
	    && (size != I2C_SMBUS_I2C_BLOCK_DATA))
		desc->control |= ISMT_DESC_PEC;

	switch (size) {
	case I2C_SMBUS_QUICK:
		dev_dbg(dev, "I2C_SMBUS_QUICK\n");
		break;

	case I2C_SMBUS_BYTE:
		if (read_write == I2C_SMBUS_WRITE) {
			/*
			 * Send Byte
			 * The command field contains the write data
			 */
			dev_dbg(dev, "I2C_SMBUS_BYTE:  WRITE\n");
			desc->control |= ISMT_DESC_CWRL;
			desc->wr_len_cmd = command;
		} else {
			/* Receive Byte */
			dev_dbg(dev, "I2C_SMBUS_BYTE:  READ\n");
			dma_size = 1;
			dma_direction = DMA_FROM_DEVICE;
			desc->rd_len = 1;
		}
		break;

	case I2C_SMBUS_BYTE_DATA:
		if (read_write == I2C_SMBUS_WRITE) {
			/*
			 * Write Byte
			 * Command plus 1 data byte
			 */
			dev_dbg(dev, "I2C_SMBUS_BYTE_DATA:  WRITE\n");
			desc->wr_len_cmd = 2;
			dma_size = 2;
			dma_direction = DMA_TO_DEVICE;
			priv->dma_buffer[0] = command;
			priv->dma_buffer[1] = data->byte;
		} else {
			/* Read Byte */
			dev_dbg(dev, "I2C_SMBUS_BYTE_DATA:  READ\n");
			desc->control |= ISMT_DESC_CWRL;
			desc->wr_len_cmd = command;
			desc->rd_len = 1;
			dma_size = 1;
			dma_direction = DMA_FROM_DEVICE;
		}
		break;

	case I2C_SMBUS_WORD_DATA:
		if (read_write == I2C_SMBUS_WRITE) {
			/* Write Word */
			dev_dbg(dev, "I2C_SMBUS_WORD_DATA:  WRITE\n");
			desc->wr_len_cmd = 3;
			dma_size = 3;
			dma_direction = DMA_TO_DEVICE;
			priv->dma_buffer[0] = command;
			priv->dma_buffer[1] = data->word & 0xff;
			priv->dma_buffer[2] = data->word >> 8;
		} else {
			/* Read Word */
			dev_dbg(dev, "I2C_SMBUS_WORD_DATA:  READ\n");
			desc->wr_len_cmd = command;
			desc->control |= ISMT_DESC_CWRL;
			desc->rd_len = 2;
			dma_size = 2;
			dma_direction = DMA_FROM_DEVICE;
		}
		break;

	case I2C_SMBUS_PROC_CALL:
		dev_dbg(dev, "I2C_SMBUS_PROC_CALL\n");
		desc->wr_len_cmd = 3;
		desc->rd_len = 2;
		dma_size = 3;
		dma_direction = DMA_BIDIRECTIONAL;
		priv->dma_buffer[0] = command;
		priv->dma_buffer[1] = data->word & 0xff;
		priv->dma_buffer[2] = data->word >> 8;
		break;

	case I2C_SMBUS_BLOCK_DATA:
		if (read_write == I2C_SMBUS_WRITE) {
			/* Block Write */
			dev_dbg(dev, "I2C_SMBUS_BLOCK_DATA:  WRITE\n");
			dma_size = data->block[0] + 1;
			dma_direction = DMA_TO_DEVICE;
			desc->wr_len_cmd = dma_size;
			desc->control |= ISMT_DESC_BLK;
			priv->dma_buffer[0] = command;
			memcpy(&priv->dma_buffer[1], &data->block[1], dma_size);
		} else {
			/* Block Read */
			dev_dbg(dev, "I2C_SMBUS_BLOCK_DATA:  READ\n");
			dma_size = I2C_SMBUS_BLOCK_MAX;
			dma_direction = DMA_FROM_DEVICE;
			desc->rd_len = dma_size;
			desc->wr_len_cmd = command;
			desc->control |= (ISMT_DESC_BLK | ISMT_DESC_CWRL);
		}
		break;

	default:
		dev_err(dev, "Unsupported transaction %d\n",
			size);
		return -EOPNOTSUPP;
	}

	/* map the data buffer */
	if (dma_size != 0) {
		dev_dbg(dev, " dev=%p\n", dev);
		dev_dbg(dev, " data=%p\n", data);
		dev_dbg(dev, " dma_buffer=%p\n", priv->dma_buffer);
		dev_dbg(dev, " dma_size=%d\n", dma_size);
		dev_dbg(dev, " dma_direction=%d\n", dma_direction);

		dma_addr = dma_map_single(dev,
				      priv->dma_buffer,
				      dma_size,
				      dma_direction);

		if (dma_mapping_error(dev, dma_addr)) {
			dev_err(dev, "Error in mapping dma buffer %p\n",
				priv->dma_buffer);
			return -EIO;
		}

		dev_dbg(dev, " dma_addr = 0x%016llX\n",
			(unsigned long long)dma_addr);

		desc->dptr_low = lower_32_bits(dma_addr);
		desc->dptr_high = upper_32_bits(dma_addr);
	}

	INIT_COMPLETION(priv->cmp);

	/* Add the descriptor */
	ismt_submit_desc(priv);

	/* Now we wait for interrupt completion, 1s */
	ret = wait_for_completion_timeout(&priv->cmp, HZ*1);

	/* unmap the data buffer */
	if (dma_size != 0)
		dma_unmap_single(&adap->dev, dma_addr, dma_size, dma_direction);

	if (unlikely(!ret)) {
		dev_err(dev, "completion wait timed out\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	/* do any post processing of the descriptor here */
	ret = ismt_process_desc(desc, data, priv, size, read_write);

out:
	/* Update the ring pointer */
	priv->head++;
	priv->head %= ISMT_DESC_ENTRIES;

	return ret;
}

/**
 * ismt_func() - report which i2c commands are supported by this adapter
 * @adap: the i2c host adapter
 */
static u32 ismt_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_QUICK		|
	       I2C_FUNC_SMBUS_BYTE		|
	       I2C_FUNC_SMBUS_BYTE_DATA		|
	       I2C_FUNC_SMBUS_WORD_DATA		|
	       I2C_FUNC_SMBUS_PROC_CALL		|
	       I2C_FUNC_SMBUS_BLOCK_DATA	|
	       I2C_FUNC_SMBUS_PEC;
}

/**
 * smbus_algorithm - the adapter algorithm and supported functionality
 * @smbus_xfer: the adapter algorithm
 * @functionality: functionality supported by the adapter
 */
static const struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer	= ismt_access,
	.functionality	= ismt_func,
};

/**
 * ismt_handle_isr() - interrupt handler bottom half
 * @priv: iSMT private data
 */
static irqreturn_t ismt_handle_isr(struct ismt_priv *priv)
{
	complete(&priv->cmp);

	return IRQ_HANDLED;
}


/**
 * ismt_do_interrupt() - IRQ interrupt handler
 * @vec: interrupt vector
 * @data: iSMT private data
 */
static irqreturn_t ismt_do_interrupt(int vec, void *data)
{
	u32 val;
	struct ismt_priv *priv = data;

	/*
	 * check to see it's our interrupt, return IRQ_NONE if not ours
	 * since we are sharing interrupt
	 */
	val = readl(priv->smba + ISMT_MSTR_MSTS);

	if (!(val & (ISMT_MSTS_MIS | ISMT_MSTS_MEIS)))
		return IRQ_NONE;
	else
		writel(val | ISMT_MSTS_MIS | ISMT_MSTS_MEIS,
		       priv->smba + ISMT_MSTR_MSTS);

	return ismt_handle_isr(priv);
}

/**
 * ismt_do_msi_interrupt() - MSI interrupt handler
 * @vec: interrupt vector
 * @data: iSMT private data
 */
static irqreturn_t ismt_do_msi_interrupt(int vec, void *data)
{
	return ismt_handle_isr(data);
}

/**
 * ismt_hw_init() - initialize the iSMT hardware
 * @priv: iSMT private data
 */
static void ismt_hw_init(struct ismt_priv *priv)
{
	u32 val;
	struct device *dev = &priv->pci_dev->dev;

	/* initialize the Master Descriptor Base Address (MDBA) */
	writeq(priv->io_rng_dma, priv->smba + ISMT_MSTR_MDBA);

	/* initialize the Master Control Register (MCTRL) */
	writel(ISMT_MCTRL_MEIE, priv->smba + ISMT_MSTR_MCTRL);

	/* initialize the Master Status Register (MSTS) */
	writel(0, priv->smba + ISMT_MSTR_MSTS);

	/* initialize the Master Descriptor Size (MDS) */
	val = readl(priv->smba + ISMT_MSTR_MDS);
	writel((val & ~ISMT_MDS_MASK) | (ISMT_DESC_ENTRIES - 1),
		priv->smba + ISMT_MSTR_MDS);

	/*
	 * Set the SMBus speed (could use this for slow HW debuggers)
	 */

	val = readl(priv->smba + ISMT_SPGT);

	switch (bus_speed) {
	case 0:
		break;

	case 80:
		dev_dbg(dev, "Setting SMBus clock to 80 kHz\n");
		writel(((val & ~ISMT_SPGT_SPD_MASK) | ISMT_SPGT_SPD_80K),
			priv->smba + ISMT_SPGT);
		break;

	case 100:
		dev_dbg(dev, "Setting SMBus clock to 100 kHz\n");
		writel(((val & ~ISMT_SPGT_SPD_MASK) | ISMT_SPGT_SPD_100K),
			priv->smba + ISMT_SPGT);
		break;

	case 400:
		dev_dbg(dev, "Setting SMBus clock to 400 kHz\n");
		writel(((val & ~ISMT_SPGT_SPD_MASK) | ISMT_SPGT_SPD_400K),
			priv->smba + ISMT_SPGT);
		break;

	case 1000:
		dev_dbg(dev, "Setting SMBus clock to 1000 kHz\n");
		writel(((val & ~ISMT_SPGT_SPD_MASK) | ISMT_SPGT_SPD_1M),
			priv->smba + ISMT_SPGT);
		break;

	default:
		dev_warn(dev, "Invalid SMBus clock speed, only 0, 80, 100, 400, and 1000 are valid\n");
		break;
	}

	val = readl(priv->smba + ISMT_SPGT);

	switch (val & ISMT_SPGT_SPD_MASK) {
	case ISMT_SPGT_SPD_80K:
		bus_speed = 80;
		break;
	case ISMT_SPGT_SPD_100K:
		bus_speed = 100;
		break;
	case ISMT_SPGT_SPD_400K:
		bus_speed = 400;
		break;
	case ISMT_SPGT_SPD_1M:
		bus_speed = 1000;
		break;
	}
	dev_dbg(dev, "SMBus clock is running at %d kHz\n", bus_speed);
}

/**
 * ismt_dev_init() - initialize the iSMT data structures
 * @priv: iSMT private data
 */
static int ismt_dev_init(struct ismt_priv *priv)
{
	/* allocate memory for the descriptor */
	priv->hw = dmam_alloc_coherent(&priv->pci_dev->dev,
				       (ISMT_DESC_ENTRIES
					       * sizeof(struct ismt_desc)),
				       &priv->io_rng_dma,
				       GFP_KERNEL);
	if (!priv->hw)
		return -ENOMEM;

	memset(priv->hw, 0, (ISMT_DESC_ENTRIES * sizeof(struct ismt_desc)));

	priv->head = 0;
	init_completion(&priv->cmp);

	return 0;
}

/**
 * ismt_int_init() - initialize interrupts
 * @priv: iSMT private data
 */
static int ismt_int_init(struct ismt_priv *priv)
{
	int err;

	/* Try using MSI interrupts */
	err = pci_enable_msi(priv->pci_dev);
	if (err) {
		dev_warn(&priv->pci_dev->dev,
			 "Unable to use MSI interrupts, falling back to legacy\n");
		goto intx;
	}

	err = devm_request_irq(&priv->pci_dev->dev,
			       priv->pci_dev->irq,
			       ismt_do_msi_interrupt,
			       0,
			       "ismt-msi",
			       priv);
	if (err) {
		pci_disable_msi(priv->pci_dev);
		goto intx;
	}

	priv->using_msi = true;
	goto done;

	/* Try using legacy interrupts */
intx:
	err = devm_request_irq(&priv->pci_dev->dev,
			       priv->pci_dev->irq,
			       ismt_do_interrupt,
			       IRQF_SHARED,
			       "ismt-intx",
			       priv);
	if (err) {
		dev_err(&priv->pci_dev->dev, "no usable interrupts\n");
		return -ENODEV;
	}

	priv->using_msi = false;

done:
	return 0;
}

static struct pci_driver ismt_driver;

/**
 * ismt_probe() - probe for iSMT devices
 * @pdev: PCI-Express device
 * @id: PCI-Express device ID
 */
static int
ismt_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int err;
	struct ismt_priv *priv;
	unsigned long start, len;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	pci_set_drvdata(pdev, priv);
	i2c_set_adapdata(&priv->adapter, priv);
	priv->adapter.owner = THIS_MODULE;

	priv->adapter.class = I2C_CLASS_HWMON;

	priv->adapter.algo = &smbus_algorithm;

	/* set up the sysfs linkage to our parent device */
	priv->adapter.dev.parent = &pdev->dev;

	/* number of retries on lost arbitration */
	priv->adapter.retries = ISMT_MAX_RETRIES;

	priv->pci_dev = pdev;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable SMBus PCI device (%d)\n",
			err);
		return err;
	}

	/* enable bus mastering */
	pci_set_master(pdev);

	/* Determine the address of the SMBus area */
	start = pci_resource_start(pdev, SMBBAR);
	len = pci_resource_len(pdev, SMBBAR);
	if (!start || !len) {
		dev_err(&pdev->dev,
			"SMBus base address uninitialized, upgrade BIOS\n");
		return -ENODEV;
	}

	snprintf(priv->adapter.name, sizeof(priv->adapter.name),
		 "SMBus iSMT adapter at %lx", start);

	dev_dbg(&priv->pci_dev->dev, " start=0x%lX\n", start);
	dev_dbg(&priv->pci_dev->dev, " len=0x%lX\n", len);

	err = acpi_check_resource_conflict(&pdev->resource[SMBBAR]);
	if (err) {
		dev_err(&pdev->dev, "ACPI resource conflict!\n");
		return err;
	}

	err = pci_request_region(pdev, SMBBAR, ismt_driver.name);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to request SMBus region 0x%lx-0x%lx\n",
			start, start + len);
		return err;
	}

	priv->smba = pcim_iomap(pdev, SMBBAR, len);
	if (!priv->smba) {
		dev_err(&pdev->dev, "Unable to ioremap SMBus BAR\n");
		err = -ENODEV;
		goto fail;
	}

	if ((pci_set_dma_mask(pdev, DMA_BIT_MASK(64)) != 0) ||
	    (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64)) != 0)) {
		if ((pci_set_dma_mask(pdev, DMA_BIT_MASK(32)) != 0) ||
		    (pci_set_consistent_dma_mask(pdev,
						 DMA_BIT_MASK(32)) != 0)) {
			dev_err(&pdev->dev, "pci_set_dma_mask fail %p\n",
				pdev);
			goto fail;
		}
	}

	err = ismt_dev_init(priv);
	if (err)
		goto fail;

	ismt_hw_init(priv);

	err = ismt_int_init(priv);
	if (err)
		goto fail;

	err = i2c_add_adapter(&priv->adapter);
	if (err) {
		dev_err(&pdev->dev, "Failed to add SMBus iSMT adapter\n");
		err = -ENODEV;
		goto fail;
	}
	return 0;

fail:
	pci_release_region(pdev, SMBBAR);
	return err;
}

/**
 * ismt_remove() - release driver resources
 * @pdev: PCI-Express device
 */
static void ismt_remove(struct pci_dev *pdev)
{
	struct ismt_priv *priv = pci_get_drvdata(pdev);

	i2c_del_adapter(&priv->adapter);
	pci_release_region(pdev, SMBBAR);
}

/**
 * ismt_suspend() - place the device in suspend
 * @pdev: PCI-Express device
 * @mesg: PM message
 */
#ifdef CONFIG_PM
static int ismt_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	pci_save_state(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, mesg));
	return 0;
}

/**
 * ismt_resume() - PCI resume code
 * @pdev: PCI-Express device
 */
static int ismt_resume(struct pci_dev *pdev)
{
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	return pci_enable_device(pdev);
}

#else

#define ismt_suspend NULL
#define ismt_resume NULL

#endif

static struct pci_driver ismt_driver = {
	.name = "ismt_smbus",
	.id_table = ismt_ids,
	.probe = ismt_probe,
	.remove = ismt_remove,
	.suspend = ismt_suspend,
	.resume = ismt_resume,
};

module_pci_driver(ismt_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Bill E. Brown <bill.e.brown@intel.com>");
MODULE_DESCRIPTION("Intel SMBus Message Transport (iSMT) driver");
