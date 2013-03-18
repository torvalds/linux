/*
 * SPI bus driver for the Topcliff PCH used by Intel SoCs
 *
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

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/spi/spidev.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include <linux/dmaengine.h>
#include <linux/pch_dma.h>

/* Register offsets */
#define PCH_SPCR		0x00	/* SPI control register */
#define PCH_SPBRR		0x04	/* SPI baud rate register */
#define PCH_SPSR		0x08	/* SPI status register */
#define PCH_SPDWR		0x0C	/* SPI write data register */
#define PCH_SPDRR		0x10	/* SPI read data register */
#define PCH_SSNXCR		0x18	/* SSN Expand Control Register */
#define PCH_SRST		0x1C	/* SPI reset register */
#define PCH_ADDRESS_SIZE	0x20

#define PCH_SPSR_TFD		0x000007C0
#define PCH_SPSR_RFD		0x0000F800

#define PCH_READABLE(x)		(((x) & PCH_SPSR_RFD)>>11)
#define PCH_WRITABLE(x)		(((x) & PCH_SPSR_TFD)>>6)

#define PCH_RX_THOLD		7
#define PCH_RX_THOLD_MAX	15

#define PCH_TX_THOLD		2

#define PCH_MAX_BAUDRATE	5000000
#define PCH_MAX_FIFO_DEPTH	16

#define STATUS_RUNNING		1
#define STATUS_EXITING		2
#define PCH_SLEEP_TIME		10

#define SSN_LOW			0x02U
#define SSN_HIGH		0x03U
#define SSN_NO_CONTROL		0x00U
#define PCH_MAX_CS		0xFF
#define PCI_DEVICE_ID_GE_SPI	0x8816

#define SPCR_SPE_BIT		(1 << 0)
#define SPCR_MSTR_BIT		(1 << 1)
#define SPCR_LSBF_BIT		(1 << 4)
#define SPCR_CPHA_BIT		(1 << 5)
#define SPCR_CPOL_BIT		(1 << 6)
#define SPCR_TFIE_BIT		(1 << 8)
#define SPCR_RFIE_BIT		(1 << 9)
#define SPCR_FIE_BIT		(1 << 10)
#define SPCR_ORIE_BIT		(1 << 11)
#define SPCR_MDFIE_BIT		(1 << 12)
#define SPCR_FICLR_BIT		(1 << 24)
#define SPSR_TFI_BIT		(1 << 0)
#define SPSR_RFI_BIT		(1 << 1)
#define SPSR_FI_BIT		(1 << 2)
#define SPSR_ORF_BIT		(1 << 3)
#define SPBRR_SIZE_BIT		(1 << 10)

#define PCH_ALL			(SPCR_TFIE_BIT|SPCR_RFIE_BIT|SPCR_FIE_BIT|\
				SPCR_ORIE_BIT|SPCR_MDFIE_BIT)

#define SPCR_RFIC_FIELD		20
#define SPCR_TFIC_FIELD		16

#define MASK_SPBRR_SPBR_BITS	((1 << 10) - 1)
#define MASK_RFIC_SPCR_BITS	(0xf << SPCR_RFIC_FIELD)
#define MASK_TFIC_SPCR_BITS	(0xf << SPCR_TFIC_FIELD)

#define PCH_CLOCK_HZ		50000000
#define PCH_MAX_SPBR		1023

/* Definition for ML7213/ML7223/ML7831 by LAPIS Semiconductor */
#define PCI_VENDOR_ID_ROHM		0x10DB
#define PCI_DEVICE_ID_ML7213_SPI	0x802c
#define PCI_DEVICE_ID_ML7223_SPI	0x800F
#define PCI_DEVICE_ID_ML7831_SPI	0x8816

/*
 * Set the number of SPI instance max
 * Intel EG20T PCH :		1ch
 * LAPIS Semiconductor ML7213 IOH :	2ch
 * LAPIS Semiconductor ML7223 IOH :	1ch
 * LAPIS Semiconductor ML7831 IOH :	1ch
*/
#define PCH_SPI_MAX_DEV			2

#define PCH_BUF_SIZE		4096
#define PCH_DMA_TRANS_SIZE	12

static int use_dma = 1;

struct pch_spi_dma_ctrl {
	struct dma_async_tx_descriptor	*desc_tx;
	struct dma_async_tx_descriptor	*desc_rx;
	struct pch_dma_slave		param_tx;
	struct pch_dma_slave		param_rx;
	struct dma_chan		*chan_tx;
	struct dma_chan		*chan_rx;
	struct scatterlist		*sg_tx_p;
	struct scatterlist		*sg_rx_p;
	struct scatterlist		sg_tx;
	struct scatterlist		sg_rx;
	int				nent;
	void				*tx_buf_virt;
	void				*rx_buf_virt;
	dma_addr_t			tx_buf_dma;
	dma_addr_t			rx_buf_dma;
};
/**
 * struct pch_spi_data - Holds the SPI channel specific details
 * @io_remap_addr:		The remapped PCI base address
 * @master:			Pointer to the SPI master structure
 * @work:			Reference to work queue handler
 * @wk:				Workqueue for carrying out execution of the
 *				requests
 * @wait:			Wait queue for waking up upon receiving an
 *				interrupt.
 * @transfer_complete:		Status of SPI Transfer
 * @bcurrent_msg_processing:	Status flag for message processing
 * @lock:			Lock for protecting this structure
 * @queue:			SPI Message queue
 * @status:			Status of the SPI driver
 * @bpw_len:			Length of data to be transferred in bits per
 *				word
 * @transfer_active:		Flag showing active transfer
 * @tx_index:			Transmit data count; for bookkeeping during
 *				transfer
 * @rx_index:			Receive data count; for bookkeeping during
 *				transfer
 * @tx_buff:			Buffer for data to be transmitted
 * @rx_index:			Buffer for Received data
 * @n_curnt_chip:		The chip number that this SPI driver currently
 *				operates on
 * @current_chip:		Reference to the current chip that this SPI
 *				driver currently operates on
 * @current_msg:		The current message that this SPI driver is
 *				handling
 * @cur_trans:			The current transfer that this SPI driver is
 *				handling
 * @board_dat:			Reference to the SPI device data structure
 * @plat_dev:			platform_device structure
 * @ch:				SPI channel number
 * @irq_reg_sts:		Status of IRQ registration
 */
struct pch_spi_data {
	void __iomem *io_remap_addr;
	unsigned long io_base_addr;
	struct spi_master *master;
	struct work_struct work;
	struct workqueue_struct *wk;
	wait_queue_head_t wait;
	u8 transfer_complete;
	u8 bcurrent_msg_processing;
	spinlock_t lock;
	struct list_head queue;
	u8 status;
	u32 bpw_len;
	u8 transfer_active;
	u32 tx_index;
	u32 rx_index;
	u16 *pkt_tx_buff;
	u16 *pkt_rx_buff;
	u8 n_curnt_chip;
	struct spi_device *current_chip;
	struct spi_message *current_msg;
	struct spi_transfer *cur_trans;
	struct pch_spi_board_data *board_dat;
	struct platform_device	*plat_dev;
	int ch;
	struct pch_spi_dma_ctrl dma;
	int use_dma;
	u8 irq_reg_sts;
	int save_total_len;
};

/**
 * struct pch_spi_board_data - Holds the SPI device specific details
 * @pdev:		Pointer to the PCI device
 * @suspend_sts:	Status of suspend
 * @num:		The number of SPI device instance
 */
struct pch_spi_board_data {
	struct pci_dev *pdev;
	u8 suspend_sts;
	int num;
};

struct pch_pd_dev_save {
	int num;
	struct platform_device *pd_save[PCH_SPI_MAX_DEV];
	struct pch_spi_board_data *board_dat;
};

static DEFINE_PCI_DEVICE_TABLE(pch_spi_pcidev_id) = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_GE_SPI),    1, },
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7213_SPI), 2, },
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7223_SPI), 1, },
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7831_SPI), 1, },
	{ }
};

/**
 * pch_spi_writereg() - Performs  register writes
 * @master:	Pointer to struct spi_master.
 * @idx:	Register offset.
 * @val:	Value to be written to register.
 */
static inline void pch_spi_writereg(struct spi_master *master, int idx, u32 val)
{
	struct pch_spi_data *data = spi_master_get_devdata(master);
	iowrite32(val, (data->io_remap_addr + idx));
}

/**
 * pch_spi_readreg() - Performs register reads
 * @master:	Pointer to struct spi_master.
 * @idx:	Register offset.
 */
static inline u32 pch_spi_readreg(struct spi_master *master, int idx)
{
	struct pch_spi_data *data = spi_master_get_devdata(master);
	return ioread32(data->io_remap_addr + idx);
}

static inline void pch_spi_setclr_reg(struct spi_master *master, int idx,
				      u32 set, u32 clr)
{
	u32 tmp = pch_spi_readreg(master, idx);
	tmp = (tmp & ~clr) | set;
	pch_spi_writereg(master, idx, tmp);
}

static void pch_spi_set_master_mode(struct spi_master *master)
{
	pch_spi_setclr_reg(master, PCH_SPCR, SPCR_MSTR_BIT, 0);
}

/**
 * pch_spi_clear_fifo() - Clears the Transmit and Receive FIFOs
 * @master:	Pointer to struct spi_master.
 */
static void pch_spi_clear_fifo(struct spi_master *master)
{
	pch_spi_setclr_reg(master, PCH_SPCR, SPCR_FICLR_BIT, 0);
	pch_spi_setclr_reg(master, PCH_SPCR, 0, SPCR_FICLR_BIT);
}

static void pch_spi_handler_sub(struct pch_spi_data *data, u32 reg_spsr_val,
				void __iomem *io_remap_addr)
{
	u32 n_read, tx_index, rx_index, bpw_len;
	u16 *pkt_rx_buffer, *pkt_tx_buff;
	int read_cnt;
	u32 reg_spcr_val;
	void __iomem *spsr;
	void __iomem *spdrr;
	void __iomem *spdwr;

	spsr = io_remap_addr + PCH_SPSR;
	iowrite32(reg_spsr_val, spsr);

	if (data->transfer_active) {
		rx_index = data->rx_index;
		tx_index = data->tx_index;
		bpw_len = data->bpw_len;
		pkt_rx_buffer = data->pkt_rx_buff;
		pkt_tx_buff = data->pkt_tx_buff;

		spdrr = io_remap_addr + PCH_SPDRR;
		spdwr = io_remap_addr + PCH_SPDWR;

		n_read = PCH_READABLE(reg_spsr_val);

		for (read_cnt = 0; (read_cnt < n_read); read_cnt++) {
			pkt_rx_buffer[rx_index++] = ioread32(spdrr);
			if (tx_index < bpw_len)
				iowrite32(pkt_tx_buff[tx_index++], spdwr);
		}

		/* disable RFI if not needed */
		if ((bpw_len - rx_index) <= PCH_MAX_FIFO_DEPTH) {
			reg_spcr_val = ioread32(io_remap_addr + PCH_SPCR);
			reg_spcr_val &= ~SPCR_RFIE_BIT; /* disable RFI */

			/* reset rx threshold */
			reg_spcr_val &= ~MASK_RFIC_SPCR_BITS;
			reg_spcr_val |= (PCH_RX_THOLD_MAX << SPCR_RFIC_FIELD);

			iowrite32(reg_spcr_val, (io_remap_addr + PCH_SPCR));
		}

		/* update counts */
		data->tx_index = tx_index;
		data->rx_index = rx_index;

		/* if transfer complete interrupt */
		if (reg_spsr_val & SPSR_FI_BIT) {
			if ((tx_index == bpw_len) && (rx_index == tx_index)) {
				/* disable interrupts */
				pch_spi_setclr_reg(data->master, PCH_SPCR, 0,
						   PCH_ALL);

				/* transfer is completed;
				   inform pch_spi_process_messages */
				data->transfer_complete = true;
				data->transfer_active = false;
				wake_up(&data->wait);
			} else {
				dev_err(&data->master->dev,
					"%s : Transfer is not completed",
					__func__);
			}
		}
	}
}

/**
 * pch_spi_handler() - Interrupt handler
 * @irq:	The interrupt number.
 * @dev_id:	Pointer to struct pch_spi_board_data.
 */
static irqreturn_t pch_spi_handler(int irq, void *dev_id)
{
	u32 reg_spsr_val;
	void __iomem *spsr;
	void __iomem *io_remap_addr;
	irqreturn_t ret = IRQ_NONE;
	struct pch_spi_data *data = dev_id;
	struct pch_spi_board_data *board_dat = data->board_dat;

	if (board_dat->suspend_sts) {
		dev_dbg(&board_dat->pdev->dev,
			"%s returning due to suspend\n", __func__);
		return IRQ_NONE;
	}

	io_remap_addr = data->io_remap_addr;
	spsr = io_remap_addr + PCH_SPSR;

	reg_spsr_val = ioread32(spsr);

	if (reg_spsr_val & SPSR_ORF_BIT) {
		dev_err(&board_dat->pdev->dev, "%s Over run error\n", __func__);
		if (data->current_msg->complete != 0) {
			data->transfer_complete = true;
			data->current_msg->status = -EIO;
			data->current_msg->complete(data->current_msg->context);
			data->bcurrent_msg_processing = false;
			data->current_msg = NULL;
			data->cur_trans = NULL;
		}
	}

	if (data->use_dma)
		return IRQ_NONE;

	/* Check if the interrupt is for SPI device */
	if (reg_spsr_val & (SPSR_FI_BIT | SPSR_RFI_BIT)) {
		pch_spi_handler_sub(data, reg_spsr_val, io_remap_addr);
		ret = IRQ_HANDLED;
	}

	dev_dbg(&board_dat->pdev->dev, "%s EXIT return value=%d\n",
		__func__, ret);

	return ret;
}

/**
 * pch_spi_set_baud_rate() - Sets SPBR field in SPBRR
 * @master:	Pointer to struct spi_master.
 * @speed_hz:	Baud rate.
 */
static void pch_spi_set_baud_rate(struct spi_master *master, u32 speed_hz)
{
	u32 n_spbr = PCH_CLOCK_HZ / (speed_hz * 2);

	/* if baud rate is less than we can support limit it */
	if (n_spbr > PCH_MAX_SPBR)
		n_spbr = PCH_MAX_SPBR;

	pch_spi_setclr_reg(master, PCH_SPBRR, n_spbr, MASK_SPBRR_SPBR_BITS);
}

/**
 * pch_spi_set_bits_per_word() - Sets SIZE field in SPBRR
 * @master:		Pointer to struct spi_master.
 * @bits_per_word:	Bits per word for SPI transfer.
 */
static void pch_spi_set_bits_per_word(struct spi_master *master,
				      u8 bits_per_word)
{
	if (bits_per_word == 8)
		pch_spi_setclr_reg(master, PCH_SPBRR, 0, SPBRR_SIZE_BIT);
	else
		pch_spi_setclr_reg(master, PCH_SPBRR, SPBRR_SIZE_BIT, 0);
}

/**
 * pch_spi_setup_transfer() - Configures the PCH SPI hardware for transfer
 * @spi:	Pointer to struct spi_device.
 */
static void pch_spi_setup_transfer(struct spi_device *spi)
{
	u32 flags = 0;

	dev_dbg(&spi->dev, "%s SPBRR content =%x setting baud rate=%d\n",
		__func__, pch_spi_readreg(spi->master, PCH_SPBRR),
		spi->max_speed_hz);
	pch_spi_set_baud_rate(spi->master, spi->max_speed_hz);

	/* set bits per word */
	pch_spi_set_bits_per_word(spi->master, spi->bits_per_word);

	if (!(spi->mode & SPI_LSB_FIRST))
		flags |= SPCR_LSBF_BIT;
	if (spi->mode & SPI_CPOL)
		flags |= SPCR_CPOL_BIT;
	if (spi->mode & SPI_CPHA)
		flags |= SPCR_CPHA_BIT;
	pch_spi_setclr_reg(spi->master, PCH_SPCR, flags,
			   (SPCR_LSBF_BIT | SPCR_CPOL_BIT | SPCR_CPHA_BIT));

	/* Clear the FIFO by toggling  FICLR to 1 and back to 0 */
	pch_spi_clear_fifo(spi->master);
}

/**
 * pch_spi_reset() - Clears SPI registers
 * @master:	Pointer to struct spi_master.
 */
static void pch_spi_reset(struct spi_master *master)
{
	/* write 1 to reset SPI */
	pch_spi_writereg(master, PCH_SRST, 0x1);

	/* clear reset */
	pch_spi_writereg(master, PCH_SRST, 0x0);
}

static int pch_spi_setup(struct spi_device *pspi)
{
	/* check bits per word */
	if (pspi->bits_per_word == 0) {
		pspi->bits_per_word = 8;
		dev_dbg(&pspi->dev, "%s 8 bits per word\n", __func__);
	}

	if ((pspi->bits_per_word != 8) && (pspi->bits_per_word != 16)) {
		dev_err(&pspi->dev, "%s Invalid bits per word\n", __func__);
		return -EINVAL;
	}

	/* Check baud rate setting */
	/* if baud rate of chip is greater than
	   max we can support,return error */
	if ((pspi->max_speed_hz) > PCH_MAX_BAUDRATE)
		pspi->max_speed_hz = PCH_MAX_BAUDRATE;

	dev_dbg(&pspi->dev, "%s MODE = %x\n", __func__,
		(pspi->mode) & (SPI_CPOL | SPI_CPHA));

	return 0;
}

static int pch_spi_transfer(struct spi_device *pspi, struct spi_message *pmsg)
{

	struct spi_transfer *transfer;
	struct pch_spi_data *data = spi_master_get_devdata(pspi->master);
	int retval;
	unsigned long flags;

	/* validate spi message and baud rate */
	if (unlikely(list_empty(&pmsg->transfers) == 1)) {
		dev_err(&pspi->dev, "%s list empty\n", __func__);
		retval = -EINVAL;
		goto err_out;
	}

	if (unlikely(pspi->max_speed_hz == 0)) {
		dev_err(&pspi->dev, "%s pch_spi_transfer maxspeed=%d\n",
			__func__, pspi->max_speed_hz);
		retval = -EINVAL;
		goto err_out;
	}

	dev_dbg(&pspi->dev, "%s Transfer List not empty. "
		"Transfer Speed is set.\n", __func__);

	spin_lock_irqsave(&data->lock, flags);
	/* validate Tx/Rx buffers and Transfer length */
	list_for_each_entry(transfer, &pmsg->transfers, transfer_list) {
		if (!transfer->tx_buf && !transfer->rx_buf) {
			dev_err(&pspi->dev,
				"%s Tx and Rx buffer NULL\n", __func__);
			retval = -EINVAL;
			goto err_return_spinlock;
		}

		if (!transfer->len) {
			dev_err(&pspi->dev, "%s Transfer length invalid\n",
				__func__);
			retval = -EINVAL;
			goto err_return_spinlock;
		}

		dev_dbg(&pspi->dev, "%s Tx/Rx buffer valid. Transfer length"
			" valid\n", __func__);

		/* if baud rate has been specified validate the same */
		if (transfer->speed_hz > PCH_MAX_BAUDRATE)
			transfer->speed_hz = PCH_MAX_BAUDRATE;

		/* if bits per word has been specified validate the same */
		if (transfer->bits_per_word) {
			if ((transfer->bits_per_word != 8)
			    && (transfer->bits_per_word != 16)) {
				retval = -EINVAL;
				dev_err(&pspi->dev,
					"%s Invalid bits per word\n", __func__);
				goto err_return_spinlock;
			}
		}
	}
	spin_unlock_irqrestore(&data->lock, flags);

	/* We won't process any messages if we have been asked to terminate */
	if (data->status == STATUS_EXITING) {
		dev_err(&pspi->dev, "%s status = STATUS_EXITING.\n", __func__);
		retval = -ESHUTDOWN;
		goto err_out;
	}

	/* If suspended ,return -EINVAL */
	if (data->board_dat->suspend_sts) {
		dev_err(&pspi->dev, "%s suspend; returning EINVAL\n", __func__);
		retval = -EINVAL;
		goto err_out;
	}

	/* set status of message */
	pmsg->actual_length = 0;
	dev_dbg(&pspi->dev, "%s - pmsg->status =%d\n", __func__, pmsg->status);

	pmsg->status = -EINPROGRESS;
	spin_lock_irqsave(&data->lock, flags);
	/* add message to queue */
	list_add_tail(&pmsg->queue, &data->queue);
	spin_unlock_irqrestore(&data->lock, flags);

	dev_dbg(&pspi->dev, "%s - Invoked list_add_tail\n", __func__);

	/* schedule work queue to run */
	queue_work(data->wk, &data->work);
	dev_dbg(&pspi->dev, "%s - Invoked queue work\n", __func__);

	retval = 0;

err_out:
	dev_dbg(&pspi->dev, "%s RETURN=%d\n", __func__, retval);
	return retval;
err_return_spinlock:
	dev_dbg(&pspi->dev, "%s RETURN=%d\n", __func__, retval);
	spin_unlock_irqrestore(&data->lock, flags);
	return retval;
}

static inline void pch_spi_select_chip(struct pch_spi_data *data,
				       struct spi_device *pspi)
{
	if (data->current_chip != NULL) {
		if (pspi->chip_select != data->n_curnt_chip) {
			dev_dbg(&pspi->dev, "%s : different slave\n", __func__);
			data->current_chip = NULL;
		}
	}

	data->current_chip = pspi;

	data->n_curnt_chip = data->current_chip->chip_select;

	dev_dbg(&pspi->dev, "%s :Invoking pch_spi_setup_transfer\n", __func__);
	pch_spi_setup_transfer(pspi);
}

static void pch_spi_set_tx(struct pch_spi_data *data, int *bpw)
{
	int size;
	u32 n_writes;
	int j;
	struct spi_message *pmsg;
	const u8 *tx_buf;
	const u16 *tx_sbuf;

	/* set baud rate if needed */
	if (data->cur_trans->speed_hz) {
		dev_dbg(&data->master->dev, "%s:setting baud rate\n", __func__);
		pch_spi_set_baud_rate(data->master, data->cur_trans->speed_hz);
	}

	/* set bits per word if needed */
	if (data->cur_trans->bits_per_word &&
	    (data->current_msg->spi->bits_per_word != data->cur_trans->bits_per_word)) {
		dev_dbg(&data->master->dev, "%s:set bits per word\n", __func__);
		pch_spi_set_bits_per_word(data->master,
					  data->cur_trans->bits_per_word);
		*bpw = data->cur_trans->bits_per_word;
	} else {
		*bpw = data->current_msg->spi->bits_per_word;
	}

	/* reset Tx/Rx index */
	data->tx_index = 0;
	data->rx_index = 0;

	data->bpw_len = data->cur_trans->len / (*bpw / 8);

	/* find alloc size */
	size = data->cur_trans->len * sizeof(*data->pkt_tx_buff);

	/* allocate memory for pkt_tx_buff & pkt_rx_buffer */
	data->pkt_tx_buff = kzalloc(size, GFP_KERNEL);
	if (data->pkt_tx_buff != NULL) {
		data->pkt_rx_buff = kzalloc(size, GFP_KERNEL);
		if (!data->pkt_rx_buff)
			kfree(data->pkt_tx_buff);
	}

	if (!data->pkt_rx_buff) {
		/* flush queue and set status of all transfers to -ENOMEM */
		dev_err(&data->master->dev, "%s :kzalloc failed\n", __func__);
		list_for_each_entry(pmsg, data->queue.next, queue) {
			pmsg->status = -ENOMEM;

			if (pmsg->complete != 0)
				pmsg->complete(pmsg->context);

			/* delete from queue */
			list_del_init(&pmsg->queue);
		}
		return;
	}

	/* copy Tx Data */
	if (data->cur_trans->tx_buf != NULL) {
		if (*bpw == 8) {
			tx_buf = data->cur_trans->tx_buf;
			for (j = 0; j < data->bpw_len; j++)
				data->pkt_tx_buff[j] = *tx_buf++;
		} else {
			tx_sbuf = data->cur_trans->tx_buf;
			for (j = 0; j < data->bpw_len; j++)
				data->pkt_tx_buff[j] = *tx_sbuf++;
		}
	}

	/* if len greater than PCH_MAX_FIFO_DEPTH, write 16,else len bytes */
	n_writes = data->bpw_len;
	if (n_writes > PCH_MAX_FIFO_DEPTH)
		n_writes = PCH_MAX_FIFO_DEPTH;

	dev_dbg(&data->master->dev, "\n%s:Pulling down SSN low - writing "
		"0x2 to SSNXCR\n", __func__);
	pch_spi_writereg(data->master, PCH_SSNXCR, SSN_LOW);

	for (j = 0; j < n_writes; j++)
		pch_spi_writereg(data->master, PCH_SPDWR, data->pkt_tx_buff[j]);

	/* update tx_index */
	data->tx_index = j;

	/* reset transfer complete flag */
	data->transfer_complete = false;
	data->transfer_active = true;
}

static void pch_spi_nomore_transfer(struct pch_spi_data *data)
{
	struct spi_message *pmsg;
	dev_dbg(&data->master->dev, "%s called\n", __func__);
	/* Invoke complete callback
	 * [To the spi core..indicating end of transfer] */
	data->current_msg->status = 0;

	if (data->current_msg->complete != 0) {
		dev_dbg(&data->master->dev,
			"%s:Invoking callback of SPI core\n", __func__);
		data->current_msg->complete(data->current_msg->context);
	}

	/* update status in global variable */
	data->bcurrent_msg_processing = false;

	dev_dbg(&data->master->dev,
		"%s:data->bcurrent_msg_processing = false\n", __func__);

	data->current_msg = NULL;
	data->cur_trans = NULL;

	/* check if we have items in list and not suspending
	 * return 1 if list empty */
	if ((list_empty(&data->queue) == 0) &&
	    (!data->board_dat->suspend_sts) &&
	    (data->status != STATUS_EXITING)) {
		/* We have some more work to do (either there is more tranint
		 * bpw;sfer requests in the current message or there are
		 *more messages)
		 */
		dev_dbg(&data->master->dev, "%s:Invoke queue_work\n", __func__);
		queue_work(data->wk, &data->work);
	} else if (data->board_dat->suspend_sts ||
		   data->status == STATUS_EXITING) {
		dev_dbg(&data->master->dev,
			"%s suspend/remove initiated, flushing queue\n",
			__func__);
		list_for_each_entry(pmsg, data->queue.next, queue) {
			pmsg->status = -EIO;

			if (pmsg->complete)
				pmsg->complete(pmsg->context);

			/* delete from queue */
			list_del_init(&pmsg->queue);
		}
	}
}

static void pch_spi_set_ir(struct pch_spi_data *data)
{
	/* enable interrupts, set threshold, enable SPI */
	if ((data->bpw_len) > PCH_MAX_FIFO_DEPTH)
		/* set receive threshold to PCH_RX_THOLD */
		pch_spi_setclr_reg(data->master, PCH_SPCR,
				   PCH_RX_THOLD << SPCR_RFIC_FIELD |
				   SPCR_FIE_BIT | SPCR_RFIE_BIT |
				   SPCR_ORIE_BIT | SPCR_SPE_BIT,
				   MASK_RFIC_SPCR_BITS | PCH_ALL);
	else
		/* set receive threshold to maximum */
		pch_spi_setclr_reg(data->master, PCH_SPCR,
				   PCH_RX_THOLD_MAX << SPCR_RFIC_FIELD |
				   SPCR_FIE_BIT | SPCR_ORIE_BIT |
				   SPCR_SPE_BIT,
				   MASK_RFIC_SPCR_BITS | PCH_ALL);

	/* Wait until the transfer completes; go to sleep after
				 initiating the transfer. */
	dev_dbg(&data->master->dev,
		"%s:waiting for transfer to get over\n", __func__);

	wait_event_interruptible(data->wait, data->transfer_complete);

	/* clear all interrupts */
	pch_spi_writereg(data->master, PCH_SPSR,
			 pch_spi_readreg(data->master, PCH_SPSR));
	/* Disable interrupts and SPI transfer */
	pch_spi_setclr_reg(data->master, PCH_SPCR, 0, PCH_ALL | SPCR_SPE_BIT);
	/* clear FIFO */
	pch_spi_clear_fifo(data->master);
}

static void pch_spi_copy_rx_data(struct pch_spi_data *data, int bpw)
{
	int j;
	u8 *rx_buf;
	u16 *rx_sbuf;

	/* copy Rx Data */
	if (!data->cur_trans->rx_buf)
		return;

	if (bpw == 8) {
		rx_buf = data->cur_trans->rx_buf;
		for (j = 0; j < data->bpw_len; j++)
			*rx_buf++ = data->pkt_rx_buff[j] & 0xFF;
	} else {
		rx_sbuf = data->cur_trans->rx_buf;
		for (j = 0; j < data->bpw_len; j++)
			*rx_sbuf++ = data->pkt_rx_buff[j];
	}
}

static void pch_spi_copy_rx_data_for_dma(struct pch_spi_data *data, int bpw)
{
	int j;
	u8 *rx_buf;
	u16 *rx_sbuf;
	const u8 *rx_dma_buf;
	const u16 *rx_dma_sbuf;

	/* copy Rx Data */
	if (!data->cur_trans->rx_buf)
		return;

	if (bpw == 8) {
		rx_buf = data->cur_trans->rx_buf;
		rx_dma_buf = data->dma.rx_buf_virt;
		for (j = 0; j < data->bpw_len; j++)
			*rx_buf++ = *rx_dma_buf++ & 0xFF;
		data->cur_trans->rx_buf = rx_buf;
	} else {
		rx_sbuf = data->cur_trans->rx_buf;
		rx_dma_sbuf = data->dma.rx_buf_virt;
		for (j = 0; j < data->bpw_len; j++)
			*rx_sbuf++ = *rx_dma_sbuf++;
		data->cur_trans->rx_buf = rx_sbuf;
	}
}

static int pch_spi_start_transfer(struct pch_spi_data *data)
{
	struct pch_spi_dma_ctrl *dma;
	unsigned long flags;
	int rtn;

	dma = &data->dma;

	spin_lock_irqsave(&data->lock, flags);

	/* disable interrupts, SPI set enable */
	pch_spi_setclr_reg(data->master, PCH_SPCR, SPCR_SPE_BIT, PCH_ALL);

	spin_unlock_irqrestore(&data->lock, flags);

	/* Wait until the transfer completes; go to sleep after
				 initiating the transfer. */
	dev_dbg(&data->master->dev,
		"%s:waiting for transfer to get over\n", __func__);
	rtn = wait_event_interruptible_timeout(data->wait,
					       data->transfer_complete,
					       msecs_to_jiffies(2 * HZ));
	if (!rtn)
		dev_err(&data->master->dev,
			"%s wait-event timeout\n", __func__);

	dma_sync_sg_for_cpu(&data->master->dev, dma->sg_rx_p, dma->nent,
			    DMA_FROM_DEVICE);

	dma_sync_sg_for_cpu(&data->master->dev, dma->sg_tx_p, dma->nent,
			    DMA_FROM_DEVICE);
	memset(data->dma.tx_buf_virt, 0, PAGE_SIZE);

	async_tx_ack(dma->desc_rx);
	async_tx_ack(dma->desc_tx);
	kfree(dma->sg_tx_p);
	kfree(dma->sg_rx_p);

	spin_lock_irqsave(&data->lock, flags);

	/* clear fifo threshold, disable interrupts, disable SPI transfer */
	pch_spi_setclr_reg(data->master, PCH_SPCR, 0,
			   MASK_RFIC_SPCR_BITS | MASK_TFIC_SPCR_BITS | PCH_ALL |
			   SPCR_SPE_BIT);
	/* clear all interrupts */
	pch_spi_writereg(data->master, PCH_SPSR,
			 pch_spi_readreg(data->master, PCH_SPSR));
	/* clear FIFO */
	pch_spi_clear_fifo(data->master);

	spin_unlock_irqrestore(&data->lock, flags);

	return rtn;
}

static void pch_dma_rx_complete(void *arg)
{
	struct pch_spi_data *data = arg;

	/* transfer is completed;inform pch_spi_process_messages_dma */
	data->transfer_complete = true;
	wake_up_interruptible(&data->wait);
}

static bool pch_spi_filter(struct dma_chan *chan, void *slave)
{
	struct pch_dma_slave *param = slave;

	if ((chan->chan_id == param->chan_id) &&
	    (param->dma_dev == chan->device->dev)) {
		chan->private = param;
		return true;
	} else {
		return false;
	}
}

static void pch_spi_request_dma(struct pch_spi_data *data, int bpw)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;
	struct pci_dev *dma_dev;
	struct pch_dma_slave *param;
	struct pch_spi_dma_ctrl *dma;
	unsigned int width;

	if (bpw == 8)
		width = PCH_DMA_WIDTH_1_BYTE;
	else
		width = PCH_DMA_WIDTH_2_BYTES;

	dma = &data->dma;
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Get DMA's dev information */
	dma_dev = pci_get_bus_and_slot(data->board_dat->pdev->bus->number,
				       PCI_DEVFN(12, 0));

	/* Set Tx DMA */
	param = &dma->param_tx;
	param->dma_dev = &dma_dev->dev;
	param->chan_id = data->master->bus_num * 2; /* Tx = 0, 2 */
	param->tx_reg = data->io_base_addr + PCH_SPDWR;
	param->width = width;
	chan = dma_request_channel(mask, pch_spi_filter, param);
	if (!chan) {
		dev_err(&data->master->dev,
			"ERROR: dma_request_channel FAILS(Tx)\n");
		data->use_dma = 0;
		return;
	}
	dma->chan_tx = chan;

	/* Set Rx DMA */
	param = &dma->param_rx;
	param->dma_dev = &dma_dev->dev;
	param->chan_id = data->master->bus_num * 2 + 1; /* Rx = Tx + 1 */
	param->rx_reg = data->io_base_addr + PCH_SPDRR;
	param->width = width;
	chan = dma_request_channel(mask, pch_spi_filter, param);
	if (!chan) {
		dev_err(&data->master->dev,
			"ERROR: dma_request_channel FAILS(Rx)\n");
		dma_release_channel(dma->chan_tx);
		dma->chan_tx = NULL;
		data->use_dma = 0;
		return;
	}
	dma->chan_rx = chan;
}

static void pch_spi_release_dma(struct pch_spi_data *data)
{
	struct pch_spi_dma_ctrl *dma;

	dma = &data->dma;
	if (dma->chan_tx) {
		dma_release_channel(dma->chan_tx);
		dma->chan_tx = NULL;
	}
	if (dma->chan_rx) {
		dma_release_channel(dma->chan_rx);
		dma->chan_rx = NULL;
	}
	return;
}

static void pch_spi_handle_dma(struct pch_spi_data *data, int *bpw)
{
	const u8 *tx_buf;
	const u16 *tx_sbuf;
	u8 *tx_dma_buf;
	u16 *tx_dma_sbuf;
	struct scatterlist *sg;
	struct dma_async_tx_descriptor *desc_tx;
	struct dma_async_tx_descriptor *desc_rx;
	int num;
	int i;
	int size;
	int rem;
	int head;
	unsigned long flags;
	struct pch_spi_dma_ctrl *dma;

	dma = &data->dma;

	/* set baud rate if needed */
	if (data->cur_trans->speed_hz) {
		dev_dbg(&data->master->dev, "%s:setting baud rate\n", __func__);
		spin_lock_irqsave(&data->lock, flags);
		pch_spi_set_baud_rate(data->master, data->cur_trans->speed_hz);
		spin_unlock_irqrestore(&data->lock, flags);
	}

	/* set bits per word if needed */
	if (data->cur_trans->bits_per_word &&
	    (data->current_msg->spi->bits_per_word !=
	     data->cur_trans->bits_per_word)) {
		dev_dbg(&data->master->dev, "%s:set bits per word\n", __func__);
		spin_lock_irqsave(&data->lock, flags);
		pch_spi_set_bits_per_word(data->master,
					  data->cur_trans->bits_per_word);
		spin_unlock_irqrestore(&data->lock, flags);
		*bpw = data->cur_trans->bits_per_word;
	} else {
		*bpw = data->current_msg->spi->bits_per_word;
	}
	data->bpw_len = data->cur_trans->len / (*bpw / 8);

	if (data->bpw_len > PCH_BUF_SIZE) {
		data->bpw_len = PCH_BUF_SIZE;
		data->cur_trans->len -= PCH_BUF_SIZE;
	}

	/* copy Tx Data */
	if (data->cur_trans->tx_buf != NULL) {
		if (*bpw == 8) {
			tx_buf = data->cur_trans->tx_buf;
			tx_dma_buf = dma->tx_buf_virt;
			for (i = 0; i < data->bpw_len; i++)
				*tx_dma_buf++ = *tx_buf++;
		} else {
			tx_sbuf = data->cur_trans->tx_buf;
			tx_dma_sbuf = dma->tx_buf_virt;
			for (i = 0; i < data->bpw_len; i++)
				*tx_dma_sbuf++ = *tx_sbuf++;
		}
	}

	/* Calculate Rx parameter for DMA transmitting */
	if (data->bpw_len > PCH_DMA_TRANS_SIZE) {
		if (data->bpw_len % PCH_DMA_TRANS_SIZE) {
			num = data->bpw_len / PCH_DMA_TRANS_SIZE + 1;
			rem = data->bpw_len % PCH_DMA_TRANS_SIZE;
		} else {
			num = data->bpw_len / PCH_DMA_TRANS_SIZE;
			rem = PCH_DMA_TRANS_SIZE;
		}
		size = PCH_DMA_TRANS_SIZE;
	} else {
		num = 1;
		size = data->bpw_len;
		rem = data->bpw_len;
	}
	dev_dbg(&data->master->dev, "%s num=%d size=%d rem=%d\n",
		__func__, num, size, rem);
	spin_lock_irqsave(&data->lock, flags);

	/* set receive fifo threshold and transmit fifo threshold */
	pch_spi_setclr_reg(data->master, PCH_SPCR,
			   ((size - 1) << SPCR_RFIC_FIELD) |
			   (PCH_TX_THOLD << SPCR_TFIC_FIELD),
			   MASK_RFIC_SPCR_BITS | MASK_TFIC_SPCR_BITS);

	spin_unlock_irqrestore(&data->lock, flags);

	/* RX */
	dma->sg_rx_p = kzalloc(sizeof(struct scatterlist)*num, GFP_ATOMIC);
	sg_init_table(dma->sg_rx_p, num); /* Initialize SG table */
	/* offset, length setting */
	sg = dma->sg_rx_p;
	for (i = 0; i < num; i++, sg++) {
		if (i == (num - 2)) {
			sg->offset = size * i;
			sg->offset = sg->offset * (*bpw / 8);
			sg_set_page(sg, virt_to_page(dma->rx_buf_virt), rem,
				    sg->offset);
			sg_dma_len(sg) = rem;
		} else if (i == (num - 1)) {
			sg->offset = size * (i - 1) + rem;
			sg->offset = sg->offset * (*bpw / 8);
			sg_set_page(sg, virt_to_page(dma->rx_buf_virt), size,
				    sg->offset);
			sg_dma_len(sg) = size;
		} else {
			sg->offset = size * i;
			sg->offset = sg->offset * (*bpw / 8);
			sg_set_page(sg, virt_to_page(dma->rx_buf_virt), size,
				    sg->offset);
			sg_dma_len(sg) = size;
		}
		sg_dma_address(sg) = dma->rx_buf_dma + sg->offset;
	}
	sg = dma->sg_rx_p;
	desc_rx = dmaengine_prep_slave_sg(dma->chan_rx, sg,
					num, DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc_rx) {
		dev_err(&data->master->dev, "%s:device_prep_slave_sg Failed\n",
			__func__);
		return;
	}
	dma_sync_sg_for_device(&data->master->dev, sg, num, DMA_FROM_DEVICE);
	desc_rx->callback = pch_dma_rx_complete;
	desc_rx->callback_param = data;
	dma->nent = num;
	dma->desc_rx = desc_rx;

	/* Calculate Tx parameter for DMA transmitting */
	if (data->bpw_len > PCH_MAX_FIFO_DEPTH) {
		head = PCH_MAX_FIFO_DEPTH - PCH_DMA_TRANS_SIZE;
		if (data->bpw_len % PCH_DMA_TRANS_SIZE > 4) {
			num = data->bpw_len / PCH_DMA_TRANS_SIZE + 1;
			rem = data->bpw_len % PCH_DMA_TRANS_SIZE - head;
		} else {
			num = data->bpw_len / PCH_DMA_TRANS_SIZE;
			rem = data->bpw_len % PCH_DMA_TRANS_SIZE +
			      PCH_DMA_TRANS_SIZE - head;
		}
		size = PCH_DMA_TRANS_SIZE;
	} else {
		num = 1;
		size = data->bpw_len;
		rem = data->bpw_len;
		head = 0;
	}

	dma->sg_tx_p = kzalloc(sizeof(struct scatterlist)*num, GFP_ATOMIC);
	sg_init_table(dma->sg_tx_p, num); /* Initialize SG table */
	/* offset, length setting */
	sg = dma->sg_tx_p;
	for (i = 0; i < num; i++, sg++) {
		if (i == 0) {
			sg->offset = 0;
			sg_set_page(sg, virt_to_page(dma->tx_buf_virt), size + head,
				    sg->offset);
			sg_dma_len(sg) = size + head;
		} else if (i == (num - 1)) {
			sg->offset = head + size * i;
			sg->offset = sg->offset * (*bpw / 8);
			sg_set_page(sg, virt_to_page(dma->tx_buf_virt), rem,
				    sg->offset);
			sg_dma_len(sg) = rem;
		} else {
			sg->offset = head + size * i;
			sg->offset = sg->offset * (*bpw / 8);
			sg_set_page(sg, virt_to_page(dma->tx_buf_virt), size,
				    sg->offset);
			sg_dma_len(sg) = size;
		}
		sg_dma_address(sg) = dma->tx_buf_dma + sg->offset;
	}
	sg = dma->sg_tx_p;
	desc_tx = dmaengine_prep_slave_sg(dma->chan_tx,
					sg, num, DMA_MEM_TO_DEV,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc_tx) {
		dev_err(&data->master->dev, "%s:device_prep_slave_sg Failed\n",
			__func__);
		return;
	}
	dma_sync_sg_for_device(&data->master->dev, sg, num, DMA_TO_DEVICE);
	desc_tx->callback = NULL;
	desc_tx->callback_param = data;
	dma->nent = num;
	dma->desc_tx = desc_tx;

	dev_dbg(&data->master->dev, "\n%s:Pulling down SSN low - writing "
		"0x2 to SSNXCR\n", __func__);

	spin_lock_irqsave(&data->lock, flags);
	pch_spi_writereg(data->master, PCH_SSNXCR, SSN_LOW);
	desc_rx->tx_submit(desc_rx);
	desc_tx->tx_submit(desc_tx);
	spin_unlock_irqrestore(&data->lock, flags);

	/* reset transfer complete flag */
	data->transfer_complete = false;
}

static void pch_spi_process_messages(struct work_struct *pwork)
{
	struct spi_message *pmsg;
	struct pch_spi_data *data;
	int bpw;

	data = container_of(pwork, struct pch_spi_data, work);
	dev_dbg(&data->master->dev, "%s data initialized\n", __func__);

	spin_lock(&data->lock);
	/* check if suspend has been initiated;if yes flush queue */
	if (data->board_dat->suspend_sts || (data->status == STATUS_EXITING)) {
		dev_dbg(&data->master->dev, "%s suspend/remove initiated,"
			"flushing queue\n", __func__);
		list_for_each_entry(pmsg, data->queue.next, queue) {
			pmsg->status = -EIO;

			if (pmsg->complete != 0) {
				spin_unlock(&data->lock);
				pmsg->complete(pmsg->context);
				spin_lock(&data->lock);
			}

			/* delete from queue */
			list_del_init(&pmsg->queue);
		}

		spin_unlock(&data->lock);
		return;
	}

	data->bcurrent_msg_processing = true;
	dev_dbg(&data->master->dev,
		"%s Set data->bcurrent_msg_processing= true\n", __func__);

	/* Get the message from the queue and delete it from there. */
	data->current_msg = list_entry(data->queue.next, struct spi_message,
					queue);

	list_del_init(&data->current_msg->queue);

	data->current_msg->status = 0;

	pch_spi_select_chip(data, data->current_msg->spi);

	spin_unlock(&data->lock);

	if (data->use_dma)
		pch_spi_request_dma(data,
				    data->current_msg->spi->bits_per_word);
	pch_spi_writereg(data->master, PCH_SSNXCR, SSN_NO_CONTROL);
	do {
		int cnt;
		/* If we are already processing a message get the next
		transfer structure from the message otherwise retrieve
		the 1st transfer request from the message. */
		spin_lock(&data->lock);
		if (data->cur_trans == NULL) {
			data->cur_trans =
				list_entry(data->current_msg->transfers.next,
					   struct spi_transfer, transfer_list);
			dev_dbg(&data->master->dev, "%s "
				":Getting 1st transfer message\n", __func__);
		} else {
			data->cur_trans =
				list_entry(data->cur_trans->transfer_list.next,
					   struct spi_transfer, transfer_list);
			dev_dbg(&data->master->dev, "%s "
				":Getting next transfer message\n", __func__);
		}
		spin_unlock(&data->lock);

		if (!data->cur_trans->len)
			goto out;
		cnt = (data->cur_trans->len - 1) / PCH_BUF_SIZE + 1;
		data->save_total_len = data->cur_trans->len;
		if (data->use_dma) {
			int i;
			char *save_rx_buf = data->cur_trans->rx_buf;
			for (i = 0; i < cnt; i ++) {
				pch_spi_handle_dma(data, &bpw);
				if (!pch_spi_start_transfer(data)) {
					data->transfer_complete = true;
					data->current_msg->status = -EIO;
					data->current_msg->complete
						   (data->current_msg->context);
					data->bcurrent_msg_processing = false;
					data->current_msg = NULL;
					data->cur_trans = NULL;
					goto out;
				}
				pch_spi_copy_rx_data_for_dma(data, bpw);
			}
			data->cur_trans->rx_buf = save_rx_buf;
		} else {
			pch_spi_set_tx(data, &bpw);
			pch_spi_set_ir(data);
			pch_spi_copy_rx_data(data, bpw);
			kfree(data->pkt_rx_buff);
			data->pkt_rx_buff = NULL;
			kfree(data->pkt_tx_buff);
			data->pkt_tx_buff = NULL;
		}
		/* increment message count */
		data->cur_trans->len = data->save_total_len;
		data->current_msg->actual_length += data->cur_trans->len;

		dev_dbg(&data->master->dev,
			"%s:data->current_msg->actual_length=%d\n",
			__func__, data->current_msg->actual_length);

		/* check for delay */
		if (data->cur_trans->delay_usecs) {
			dev_dbg(&data->master->dev, "%s:"
				"delay in usec=%d\n", __func__,
				data->cur_trans->delay_usecs);
			udelay(data->cur_trans->delay_usecs);
		}

		spin_lock(&data->lock);

		/* No more transfer in this message. */
		if ((data->cur_trans->transfer_list.next) ==
		    &(data->current_msg->transfers)) {
			pch_spi_nomore_transfer(data);
		}

		spin_unlock(&data->lock);

	} while (data->cur_trans != NULL);

out:
	pch_spi_writereg(data->master, PCH_SSNXCR, SSN_HIGH);
	if (data->use_dma)
		pch_spi_release_dma(data);
}

static void pch_spi_free_resources(struct pch_spi_board_data *board_dat,
				   struct pch_spi_data *data)
{
	dev_dbg(&board_dat->pdev->dev, "%s ENTRY\n", __func__);

	/* free workqueue */
	if (data->wk != NULL) {
		destroy_workqueue(data->wk);
		data->wk = NULL;
		dev_dbg(&board_dat->pdev->dev,
			"%s destroy_workqueue invoked successfully\n",
			__func__);
	}
}

static int pch_spi_get_resources(struct pch_spi_board_data *board_dat,
				 struct pch_spi_data *data)
{
	int retval = 0;

	dev_dbg(&board_dat->pdev->dev, "%s ENTRY\n", __func__);

	/* create workqueue */
	data->wk = create_singlethread_workqueue(KBUILD_MODNAME);
	if (!data->wk) {
		dev_err(&board_dat->pdev->dev,
			"%s create_singlet hread_workqueue failed\n", __func__);
		retval = -EBUSY;
		goto err_return;
	}

	/* reset PCH SPI h/w */
	pch_spi_reset(data->master);
	dev_dbg(&board_dat->pdev->dev,
		"%s pch_spi_reset invoked successfully\n", __func__);

	dev_dbg(&board_dat->pdev->dev, "%s data->irq_reg_sts=true\n", __func__);

err_return:
	if (retval != 0) {
		dev_err(&board_dat->pdev->dev,
			"%s FAIL:invoking pch_spi_free_resources\n", __func__);
		pch_spi_free_resources(board_dat, data);
	}

	dev_dbg(&board_dat->pdev->dev, "%s Return=%d\n", __func__, retval);

	return retval;
}

static void pch_free_dma_buf(struct pch_spi_board_data *board_dat,
			     struct pch_spi_data *data)
{
	struct pch_spi_dma_ctrl *dma;

	dma = &data->dma;
	if (dma->tx_buf_dma)
		dma_free_coherent(&board_dat->pdev->dev, PCH_BUF_SIZE,
				  dma->tx_buf_virt, dma->tx_buf_dma);
	if (dma->rx_buf_dma)
		dma_free_coherent(&board_dat->pdev->dev, PCH_BUF_SIZE,
				  dma->rx_buf_virt, dma->rx_buf_dma);
	return;
}

static void pch_alloc_dma_buf(struct pch_spi_board_data *board_dat,
			      struct pch_spi_data *data)
{
	struct pch_spi_dma_ctrl *dma;

	dma = &data->dma;
	/* Get Consistent memory for Tx DMA */
	dma->tx_buf_virt = dma_alloc_coherent(&board_dat->pdev->dev,
				PCH_BUF_SIZE, &dma->tx_buf_dma, GFP_KERNEL);
	/* Get Consistent memory for Rx DMA */
	dma->rx_buf_virt = dma_alloc_coherent(&board_dat->pdev->dev,
				PCH_BUF_SIZE, &dma->rx_buf_dma, GFP_KERNEL);
}

static int pch_spi_pd_probe(struct platform_device *plat_dev)
{
	int ret;
	struct spi_master *master;
	struct pch_spi_board_data *board_dat = dev_get_platdata(&plat_dev->dev);
	struct pch_spi_data *data;

	dev_dbg(&plat_dev->dev, "%s:debug\n", __func__);

	master = spi_alloc_master(&board_dat->pdev->dev,
				  sizeof(struct pch_spi_data));
	if (!master) {
		dev_err(&plat_dev->dev, "spi_alloc_master[%d] failed.\n",
			plat_dev->id);
		return -ENOMEM;
	}

	data = spi_master_get_devdata(master);
	data->master = master;

	platform_set_drvdata(plat_dev, data);

	/* baseaddress + address offset) */
	data->io_base_addr = pci_resource_start(board_dat->pdev, 1) +
					 PCH_ADDRESS_SIZE * plat_dev->id;
	data->io_remap_addr = pci_iomap(board_dat->pdev, 1, 0) +
					 PCH_ADDRESS_SIZE * plat_dev->id;
	if (!data->io_remap_addr) {
		dev_err(&plat_dev->dev, "%s pci_iomap failed\n", __func__);
		ret = -ENOMEM;
		goto err_pci_iomap;
	}

	dev_dbg(&plat_dev->dev, "[ch%d] remap_addr=%p\n",
		plat_dev->id, data->io_remap_addr);

	/* initialize members of SPI master */
	master->num_chipselect = PCH_MAX_CS;
	master->setup = pch_spi_setup;
	master->transfer = pch_spi_transfer;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;

	data->board_dat = board_dat;
	data->plat_dev = plat_dev;
	data->n_curnt_chip = 255;
	data->status = STATUS_RUNNING;
	data->ch = plat_dev->id;
	data->use_dma = use_dma;

	INIT_LIST_HEAD(&data->queue);
	spin_lock_init(&data->lock);
	INIT_WORK(&data->work, pch_spi_process_messages);
	init_waitqueue_head(&data->wait);

	ret = pch_spi_get_resources(board_dat, data);
	if (ret) {
		dev_err(&plat_dev->dev, "%s fail(retval=%d)\n", __func__, ret);
		goto err_spi_get_resources;
	}

	ret = request_irq(board_dat->pdev->irq, pch_spi_handler,
			  IRQF_SHARED, KBUILD_MODNAME, data);
	if (ret) {
		dev_err(&plat_dev->dev,
			"%s request_irq failed\n", __func__);
		goto err_request_irq;
	}
	data->irq_reg_sts = true;

	pch_spi_set_master_mode(master);

	ret = spi_register_master(master);
	if (ret != 0) {
		dev_err(&plat_dev->dev,
			"%s spi_register_master FAILED\n", __func__);
		goto err_spi_register_master;
	}

	if (use_dma) {
		dev_info(&plat_dev->dev, "Use DMA for data transfers\n");
		pch_alloc_dma_buf(board_dat, data);
	}

	return 0;

err_spi_register_master:
	free_irq(board_dat->pdev->irq, board_dat);
err_request_irq:
	pch_spi_free_resources(board_dat, data);
err_spi_get_resources:
	pci_iounmap(board_dat->pdev, data->io_remap_addr);
err_pci_iomap:
	spi_master_put(master);

	return ret;
}

static int pch_spi_pd_remove(struct platform_device *plat_dev)
{
	struct pch_spi_board_data *board_dat = dev_get_platdata(&plat_dev->dev);
	struct pch_spi_data *data = platform_get_drvdata(plat_dev);
	int count;
	unsigned long flags;

	dev_dbg(&plat_dev->dev, "%s:[ch%d] irq=%d\n",
		__func__, plat_dev->id, board_dat->pdev->irq);

	if (use_dma)
		pch_free_dma_buf(board_dat, data);

	/* check for any pending messages; no action is taken if the queue
	 * is still full; but at least we tried.  Unload anyway */
	count = 500;
	spin_lock_irqsave(&data->lock, flags);
	data->status = STATUS_EXITING;
	while ((list_empty(&data->queue) == 0) && --count) {
		dev_dbg(&board_dat->pdev->dev, "%s :queue not empty\n",
			__func__);
		spin_unlock_irqrestore(&data->lock, flags);
		msleep(PCH_SLEEP_TIME);
		spin_lock_irqsave(&data->lock, flags);
	}
	spin_unlock_irqrestore(&data->lock, flags);

	pch_spi_free_resources(board_dat, data);
	/* disable interrupts & free IRQ */
	if (data->irq_reg_sts) {
		/* disable interrupts */
		pch_spi_setclr_reg(data->master, PCH_SPCR, 0, PCH_ALL);
		data->irq_reg_sts = false;
		free_irq(board_dat->pdev->irq, data);
	}

	pci_iounmap(board_dat->pdev, data->io_remap_addr);
	spi_unregister_master(data->master);

	return 0;
}
#ifdef CONFIG_PM
static int pch_spi_pd_suspend(struct platform_device *pd_dev,
			      pm_message_t state)
{
	u8 count;
	struct pch_spi_board_data *board_dat = dev_get_platdata(&pd_dev->dev);
	struct pch_spi_data *data = platform_get_drvdata(pd_dev);

	dev_dbg(&pd_dev->dev, "%s ENTRY\n", __func__);

	if (!board_dat) {
		dev_err(&pd_dev->dev,
			"%s pci_get_drvdata returned NULL\n", __func__);
		return -EFAULT;
	}

	/* check if the current message is processed:
	   Only after thats done the transfer will be suspended */
	count = 255;
	while ((--count) > 0) {
		if (!(data->bcurrent_msg_processing))
			break;
		msleep(PCH_SLEEP_TIME);
	}

	/* Free IRQ */
	if (data->irq_reg_sts) {
		/* disable all interrupts */
		pch_spi_setclr_reg(data->master, PCH_SPCR, 0, PCH_ALL);
		pch_spi_reset(data->master);
		free_irq(board_dat->pdev->irq, data);

		data->irq_reg_sts = false;
		dev_dbg(&pd_dev->dev,
			"%s free_irq invoked successfully.\n", __func__);
	}

	return 0;
}

static int pch_spi_pd_resume(struct platform_device *pd_dev)
{
	struct pch_spi_board_data *board_dat = dev_get_platdata(&pd_dev->dev);
	struct pch_spi_data *data = platform_get_drvdata(pd_dev);
	int retval;

	if (!board_dat) {
		dev_err(&pd_dev->dev,
			"%s pci_get_drvdata returned NULL\n", __func__);
		return -EFAULT;
	}

	if (!data->irq_reg_sts) {
		/* register IRQ */
		retval = request_irq(board_dat->pdev->irq, pch_spi_handler,
				     IRQF_SHARED, KBUILD_MODNAME, data);
		if (retval < 0) {
			dev_err(&pd_dev->dev,
				"%s request_irq failed\n", __func__);
			return retval;
		}

		/* reset PCH SPI h/w */
		pch_spi_reset(data->master);
		pch_spi_set_master_mode(data->master);
		data->irq_reg_sts = true;
	}
	return 0;
}
#else
#define pch_spi_pd_suspend NULL
#define pch_spi_pd_resume NULL
#endif

static struct platform_driver pch_spi_pd_driver = {
	.driver = {
		.name = "pch-spi",
		.owner = THIS_MODULE,
	},
	.probe = pch_spi_pd_probe,
	.remove = pch_spi_pd_remove,
	.suspend = pch_spi_pd_suspend,
	.resume = pch_spi_pd_resume
};

static int pch_spi_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	struct pch_spi_board_data *board_dat;
	struct platform_device *pd_dev = NULL;
	int retval;
	int i;
	struct pch_pd_dev_save *pd_dev_save;

	pd_dev_save = kzalloc(sizeof(struct pch_pd_dev_save), GFP_KERNEL);
	if (!pd_dev_save) {
		dev_err(&pdev->dev, "%s Can't allocate pd_dev_sav\n", __func__);
		return -ENOMEM;
	}

	board_dat = kzalloc(sizeof(struct pch_spi_board_data), GFP_KERNEL);
	if (!board_dat) {
		dev_err(&pdev->dev, "%s Can't allocate board_dat\n", __func__);
		retval = -ENOMEM;
		goto err_no_mem;
	}

	retval = pci_request_regions(pdev, KBUILD_MODNAME);
	if (retval) {
		dev_err(&pdev->dev, "%s request_region failed\n", __func__);
		goto pci_request_regions;
	}

	board_dat->pdev = pdev;
	board_dat->num = id->driver_data;
	pd_dev_save->num = id->driver_data;
	pd_dev_save->board_dat = board_dat;

	retval = pci_enable_device(pdev);
	if (retval) {
		dev_err(&pdev->dev, "%s pci_enable_device failed\n", __func__);
		goto pci_enable_device;
	}

	for (i = 0; i < board_dat->num; i++) {
		pd_dev = platform_device_alloc("pch-spi", i);
		if (!pd_dev) {
			dev_err(&pdev->dev, "platform_device_alloc failed\n");
			goto err_platform_device;
		}
		pd_dev_save->pd_save[i] = pd_dev;
		pd_dev->dev.parent = &pdev->dev;

		retval = platform_device_add_data(pd_dev, board_dat,
						  sizeof(*board_dat));
		if (retval) {
			dev_err(&pdev->dev,
				"platform_device_add_data failed\n");
			platform_device_put(pd_dev);
			goto err_platform_device;
		}

		retval = platform_device_add(pd_dev);
		if (retval) {
			dev_err(&pdev->dev, "platform_device_add failed\n");
			platform_device_put(pd_dev);
			goto err_platform_device;
		}
	}

	pci_set_drvdata(pdev, pd_dev_save);

	return 0;

err_platform_device:
	pci_disable_device(pdev);
pci_enable_device:
	pci_release_regions(pdev);
pci_request_regions:
	kfree(board_dat);
err_no_mem:
	kfree(pd_dev_save);

	return retval;
}

static void pch_spi_remove(struct pci_dev *pdev)
{
	int i;
	struct pch_pd_dev_save *pd_dev_save = pci_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s ENTRY:pdev=%p\n", __func__, pdev);

	for (i = 0; i < pd_dev_save->num; i++)
		platform_device_unregister(pd_dev_save->pd_save[i]);

	pci_disable_device(pdev);
	pci_release_regions(pdev);
	kfree(pd_dev_save->board_dat);
	kfree(pd_dev_save);
}

#ifdef CONFIG_PM
static int pch_spi_suspend(struct pci_dev *pdev, pm_message_t state)
{
	int retval;
	struct pch_pd_dev_save *pd_dev_save = pci_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s ENTRY\n", __func__);

	pd_dev_save->board_dat->suspend_sts = true;

	/* save config space */
	retval = pci_save_state(pdev);
	if (retval == 0) {
		pci_enable_wake(pdev, PCI_D3hot, 0);
		pci_disable_device(pdev);
		pci_set_power_state(pdev, PCI_D3hot);
	} else {
		dev_err(&pdev->dev, "%s pci_save_state failed\n", __func__);
	}

	return retval;
}

static int pch_spi_resume(struct pci_dev *pdev)
{
	int retval;
	struct pch_pd_dev_save *pd_dev_save = pci_get_drvdata(pdev);
	dev_dbg(&pdev->dev, "%s ENTRY\n", __func__);

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	retval = pci_enable_device(pdev);
	if (retval < 0) {
		dev_err(&pdev->dev,
			"%s pci_enable_device failed\n", __func__);
	} else {
		pci_enable_wake(pdev, PCI_D3hot, 0);

		/* set suspend status to false */
		pd_dev_save->board_dat->suspend_sts = false;
	}

	return retval;
}
#else
#define pch_spi_suspend NULL
#define pch_spi_resume NULL

#endif

static struct pci_driver pch_spi_pcidev_driver = {
	.name = "pch_spi",
	.id_table = pch_spi_pcidev_id,
	.probe = pch_spi_probe,
	.remove = pch_spi_remove,
	.suspend = pch_spi_suspend,
	.resume = pch_spi_resume,
};

static int __init pch_spi_init(void)
{
	int ret;
	ret = platform_driver_register(&pch_spi_pd_driver);
	if (ret)
		return ret;

	ret = pci_register_driver(&pch_spi_pcidev_driver);
	if (ret)
		return ret;

	return 0;
}
module_init(pch_spi_init);

static void __exit pch_spi_exit(void)
{
	pci_unregister_driver(&pch_spi_pcidev_driver);
	platform_driver_unregister(&pch_spi_pd_driver);
}
module_exit(pch_spi_exit);

module_param(use_dma, int, 0644);
MODULE_PARM_DESC(use_dma,
		 "to use DMA for data transfers pass 1 else 0; default 1");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel EG20T PCH/LAPIS Semiconductor ML7xxx IOH SPI Driver");
