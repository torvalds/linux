/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *    Author: Frank Mori Hess <fmh6jj@gmail.com>
 *   Copyright: (C) 2006, 2010, 2015 Fluke Corporation
 *	(C) 2017 Frank Mori Hess
 ***************************************************************************/

#include <linux/dmaengine.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/io.h>
#include "nec7210.h"

static const int fifo_reg_offset = 2;

static const int gpib_control_status_pci_resource_index;
static const int gpib_fifo_pci_resource_index = 1;

/* We don't have a real pci vendor/device id, the following will need to be
 * patched to match prototype hardware.
 */
#define BOGUS_PCI_VENDOR_ID_FLUKE 0xffff
#define BOGUS_PCI_DEVICE_ID_FLUKE_BLADERUNNER 0x0

struct fmh_priv {
	struct nec7210_priv nec7210_priv;
	struct resource *gpib_iomem_res;
	struct resource *write_transfer_counter_res;
	struct resource *dma_port_res;
	int irq;
	struct dma_chan *dma_channel;
	u8 *dma_buffer;
	int dma_buffer_size;
	int dma_burst_length;
	void __iomem *fifo_base;
	unsigned supports_fifo_interrupts : 1;
};

static inline int fmh_gpib_half_fifo_size(struct fmh_priv *priv)
{
	return priv->dma_burst_length;
}

// registers beyond the nec7210 register set
enum fmh_gpib_regs {
	EXT_STATUS_1_REG = 0x9,
	STATE1_REG = 0xc,
	ISR0_IMR0_REG = 0xe,
	BUS_STATUS_REG = 0xf
};

/* IMR0 -- Interrupt Mode Register 0 */
enum imr0_bits {
	ATN_INTERRUPT_ENABLE_BIT = 0x4,
	IFC_INTERRUPT_ENABLE_BIT = 0x8
};

/* ISR0 -- Interrupt Status Register 0 */
enum isr0_bits {
	ATN_INTERRUPT_BIT = 0x4,
	IFC_INTERRUPT_BIT = 0x8
};

enum state1_bits {
	SOURCE_HANDSHAKE_SIDS_BITS = 0x0, /* source idle state */
	SOURCE_HANDSHAKE_SGNS_BITS = 0x1, /* source generate state */
	SOURCE_HANDSHAKE_SDYS_BITS = 0x2, /* source delay state */
	SOURCE_HANDSHAKE_STRS_BITS = 0x5, /* source transfer state */
	SOURCE_HANDSHAKE_MASK = 0x7
};

enum fmh_gpib_auxmr_bits {
	AUX_I_REG = 0xe0,
};

enum aux_reg_i_bits {
	LOCAL_PPOLL_MODE_BIT = 0x4
};

enum ext_status_1_bits {
	DATA_IN_STATUS_BIT = 0x01,
	DATA_OUT_STATUS_BIT = 0x02,
	COMMAND_OUT_STATUS_BIT = 0x04,
	RFD_HOLDOFF_STATUS_BIT = 0x08,
	END_STATUS_BIT = 0x10
};

/* dma fifo reg and bits */
enum dma_fifo_regs {
	FIFO_DATA_REG = 0x0,
	FIFO_CONTROL_STATUS_REG = 0x1,
	FIFO_XFER_COUNTER_REG = 0x2,
	FIFO_MAX_BURST_LENGTH_REG = 0x3
};

enum fifo_data_bits {
	FIFO_DATA_EOI_FLAG = 0x100
};

enum fifo_control_bits {
	TX_FIFO_DMA_REQUEST_ENABLE = 0x0001,
	TX_FIFO_CLEAR = 0x0002,
	TX_FIFO_HALF_EMPTY_INTERRUPT_ENABLE = 0x0008,
	RX_FIFO_DMA_REQUEST_ENABLE = 0x0100,
	RX_FIFO_CLEAR = 0x0200,
	RX_FIFO_HALF_FULL_INTERRUPT_ENABLE = 0x0800
};

enum fifo_status_bits {
	TX_FIFO_EMPTY = 0x0001,
	TX_FIFO_FULL = 0x0002,
	TX_FIFO_HALF_EMPTY = 0x0004,
	TX_FIFO_HALF_EMPTY_INTERRUPT_IS_ENABLED = 0x0008,
	TX_FIFO_DMA_REQUEST_IS_ENABLED = 0x0010,
	RX_FIFO_EMPTY = 0x0100,
	RX_FIFO_FULL = 0x0200,
	RX_FIFO_HALF_FULL = 0x0400,
	RX_FIFO_HALF_FULL_INTERRUPT_IS_ENABLED = 0x0800,
	RX_FIFO_DMA_REQUEST_IS_ENABLED = 0x1000
};

static const unsigned int fifo_data_mask = 0x00ff;
static const unsigned int fifo_xfer_counter_mask = 0x0fff;
static const unsigned int fifo_max_burst_length_mask = 0x00ff;

static inline uint8_t gpib_cs_read_byte(struct nec7210_priv *nec_priv,
					unsigned int register_num)
{
	return readb(nec_priv->mmiobase + register_num * nec_priv->offset);
}

static inline void gpib_cs_write_byte(struct nec7210_priv *nec_priv, uint8_t data,
				      unsigned int register_num)
{
	writeb(data, nec_priv->mmiobase + register_num * nec_priv->offset);
}

static inline uint16_t fifos_read(struct fmh_priv *fmh_priv, int register_num)
{
	if (!fmh_priv->fifo_base)
		return 0;
	return readw(fmh_priv->fifo_base + register_num * fifo_reg_offset);
}

static inline void fifos_write(struct fmh_priv *fmh_priv, uint16_t data, int register_num)
{
	if (!fmh_priv->fifo_base)
		return;
	writew(data, fmh_priv->fifo_base + register_num * fifo_reg_offset);
}

enum bus_status_bits {
	BSR_ATN_BIT = 0x01,
	BSR_EOI_BIT = 0x02,
	BSR_SRQ_BIT = 0x04,
	BSR_IFC_BIT = 0x08,
	BSR_REN_BIT = 0x10,
	BSR_DAV_BIT = 0x20,
	BSR_NRFD_BIT = 0x40,
	BSR_NDAC_BIT = 0x80,
};

enum fmh_gpib_aux_cmds {
	/* AUX_RTL2 is an auxiliary command which causes the cb7210 to assert
	 * (and keep asserted) the local rtl message.  This is used in conjunction
	 * with the normal nec7210 AUX_RTL command, which
	 * pulses the rtl message, having the effect of clearing rtl if it was left
	 * asserted by AUX_RTL2.
	 */
	AUX_RTL2 = 0x0d,
	AUX_RFD_HOLDOFF_ASAP = 0x15,
	AUX_REQT = 0x18,
	AUX_REQF = 0x19,
	AUX_LO_SPEED = 0x40,
	AUX_HI_SPEED = 0x41
};
