/*
 * SPI bus driver for the Topcliff PCH used by Intel SoCs
 *
 * Copyright (C) 2010 OKI SEMICONDUCTOR Co., LTD.
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

/* Register offsets */
#define PCH_SPCR		0x00	/* SPI control register */
#define PCH_SPBRR		0x04	/* SPI baud rate register */
#define PCH_SPSR		0x08	/* SPI status register */
#define PCH_SPDWR		0x0C	/* SPI write data register */
#define PCH_SPDRR		0x10	/* SPI read data register */
#define PCH_SSNXCR		0x18	/* SSN Expand Control Register */
#define PCH_SRST		0x1C	/* SPI reset register */

#define PCH_SPSR_TFD		0x000007C0
#define PCH_SPSR_RFD		0x0000F800

#define PCH_READABLE(x)		(((x) & PCH_SPSR_RFD)>>11)
#define PCH_WRITABLE(x)		(((x) & PCH_SPSR_TFD)>>6)

#define PCH_RX_THOLD		7
#define PCH_RX_THOLD_MAX	15

#define PCH_MAX_BAUDRATE	5000000
#define PCH_MAX_FIFO_DEPTH	16

#define STATUS_RUNNING		1
#define STATUS_EXITING		2
#define PCH_SLEEP_TIME		10

#define PCH_ADDRESS_SIZE	0x20

#define SSN_LOW			0x02U
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
#define SPBRR_SIZE_BIT		(1 << 10)

#define PCH_ALL			(SPCR_TFIE_BIT|SPCR_RFIE_BIT|SPCR_FIE_BIT|SPCR_ORIE_BIT|SPCR_MDFIE_BIT)

#define SPCR_RFIC_FIELD		20
#define SPCR_TFIC_FIELD		16

#define SPSR_INT_BITS		0x1F
#define MASK_SPBRR_SPBR_BITS	(~((1 << 10) - 1))
#define MASK_RFIC_SPCR_BITS	(~(0xf << 20))
#define MASK_TFIC_SPCR_BITS	(~(0xf000f << 12))

#define PCH_CLOCK_HZ		50000000
#define PCH_MAX_SPBR		1023


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
 */
struct pch_spi_data {
	void __iomem *io_remap_addr;
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
};

/**
 * struct pch_spi_board_data - Holds the SPI device specific details
 * @pdev:		Pointer to the PCI device
 * @irq_reg_sts:	Status of IRQ registration
 * @pci_req_sts:	Status of pci_request_regions
 * @suspend_sts:	Status of suspend
 * @data:		Pointer to SPI channel data structure
 */
struct pch_spi_board_data {
	struct pci_dev *pdev;
	u8 irq_reg_sts;
	u8 pci_req_sts;
	u8 suspend_sts;
	struct pch_spi_data *data;
};

static struct pci_device_id pch_spi_pcidev_id[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_GE_SPI)},
	{0,}
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
			reg_spcr_val &= MASK_RFIC_SPCR_BITS;
			reg_spcr_val |= (PCH_RX_THOLD_MAX << SPCR_RFIC_FIELD);
			iowrite32(((reg_spcr_val) &= (~(SPCR_RFIE_BIT))),
				 (io_remap_addr + PCH_SPCR));
		}

		/* update counts */
		data->tx_index = tx_index;
		data->rx_index = rx_index;

	}

	/* if transfer complete interrupt */
	if (reg_spsr_val & SPSR_FI_BIT) {
		/* disable FI & RFI interrupts */
		pch_spi_setclr_reg(data->master, PCH_SPCR, 0,
				   SPCR_FIE_BIT | SPCR_TFIE_BIT);

		/* transfer is completed;inform pch_spi_process_messages */
		data->transfer_complete = true;
		wake_up(&data->wait);
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
	struct pch_spi_data *data;
	void __iomem *spsr;
	void __iomem *io_remap_addr;
	irqreturn_t ret = IRQ_NONE;
	struct pch_spi_board_data *board_dat = dev_id;

	if (board_dat->suspend_sts) {
		dev_dbg(&board_dat->pdev->dev,
			"%s returning due to suspend\n", __func__);
		return IRQ_NONE;
	}

	data = board_dat->data;
	io_remap_addr = data->io_remap_addr;
	spsr = io_remap_addr + PCH_SPSR;

	reg_spsr_val = ioread32(spsr);

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

	pch_spi_setclr_reg(master, PCH_SPBRR, n_spbr, ~MASK_SPBRR_SPBR_BITS);
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
		dev_err(&pspi->dev, "%s pch_spi_tranfer maxspeed=%d\n",
			__func__, pspi->max_speed_hz);
		retval = -EINVAL;
		goto err_out;
	}

	dev_dbg(&pspi->dev, "%s Transfer List not empty. "
		"Transfer Speed is set.\n", __func__);

	/* validate Tx/Rx buffers and Transfer length */
	list_for_each_entry(transfer, &pmsg->transfers, transfer_list) {
		if (!transfer->tx_buf && !transfer->rx_buf) {
			dev_err(&pspi->dev,
				"%s Tx and Rx buffer NULL\n", __func__);
			retval = -EINVAL;
			goto err_out;
		}

		if (!transfer->len) {
			dev_err(&pspi->dev, "%s Transfer length invalid\n",
				__func__);
			retval = -EINVAL;
			goto err_out;
		}

		dev_dbg(&pspi->dev, "%s Tx/Rx buffer valid. Transfer length"
			" valid\n", __func__);

		/* if baud rate hs been specified validate the same */
		if (transfer->speed_hz > PCH_MAX_BAUDRATE)
			transfer->speed_hz = PCH_MAX_BAUDRATE;

		/* if bits per word has been specified validate the same */
		if (transfer->bits_per_word) {
			if ((transfer->bits_per_word != 8)
			    && (transfer->bits_per_word != 16)) {
				retval = -EINVAL;
				dev_err(&pspi->dev,
					"%s Invalid bits per word\n", __func__);
				goto err_out;
			}
		}
	}

	spin_lock_irqsave(&data->lock, flags);

	/* We won't process any messages if we have been asked to terminate */
	if (data->status == STATUS_EXITING) {
		dev_err(&pspi->dev, "%s status = STATUS_EXITING.\n", __func__);
		retval = -ESHUTDOWN;
		goto err_return_spinlock;
	}

	/* If suspended ,return -EINVAL */
	if (data->board_dat->suspend_sts) {
		dev_err(&pspi->dev, "%s suspend; returning EINVAL\n", __func__);
		retval = -EINVAL;
		goto err_return_spinlock;
	}

	/* set status of message */
	pmsg->actual_length = 0;
	dev_dbg(&pspi->dev, "%s - pmsg->status =%d\n", __func__, pmsg->status);

	pmsg->status = -EINPROGRESS;

	/* add message to queue */
	list_add_tail(&pmsg->queue, &data->queue);
	dev_dbg(&pspi->dev, "%s - Invoked list_add_tail\n", __func__);

	/* schedule work queue to run */
	queue_work(data->wk, &data->work);
	dev_dbg(&pspi->dev, "%s - Invoked queue work\n", __func__);

	retval = 0;

err_return_spinlock:
	spin_unlock_irqrestore(&data->lock, flags);
err_out:
	dev_dbg(&pspi->dev, "%s RETURN=%d\n", __func__, retval);
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

static void pch_spi_set_tx(struct pch_spi_data *data, int *bpw,
			   struct spi_message **ppmsg)
{
	int size;
	u32 n_writes;
	int j;
	struct spi_message *pmsg;
	const u8 *tx_buf;
	const u16 *tx_sbuf;

	pmsg = *ppmsg;

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


static void pch_spi_nomore_transfer(struct pch_spi_data *data,
						struct spi_message *pmsg)
{
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
	/* enable interrupts */
	if ((data->bpw_len) > PCH_MAX_FIFO_DEPTH) {
		/* set receive threhold to PCH_RX_THOLD */
		pch_spi_setclr_reg(data->master, PCH_SPCR,
				   PCH_RX_THOLD << SPCR_TFIC_FIELD,
				   ~MASK_TFIC_SPCR_BITS);
		/* enable FI and RFI interrupts */
		pch_spi_setclr_reg(data->master, PCH_SPCR,
				   SPCR_RFIE_BIT | SPCR_TFIE_BIT, 0);
	} else {
		/* set receive threhold to maximum */
		pch_spi_setclr_reg(data->master, PCH_SPCR,
				   PCH_RX_THOLD_MAX << SPCR_TFIC_FIELD,
				   ~MASK_TFIC_SPCR_BITS);
		/* enable FI interrupt */
		pch_spi_setclr_reg(data->master, PCH_SPCR, SPCR_FIE_BIT, 0);
	}

	dev_dbg(&data->master->dev,
		"%s:invoking pch_spi_set_enable to enable SPI\n", __func__);

	/* SPI set enable */
	pch_spi_setclr_reg(data->current_chip->master, PCH_SPCR, SPCR_SPE_BIT, 0);

	/* Wait until the transfer completes; go to sleep after
				 initiating the transfer. */
	dev_dbg(&data->master->dev,
		"%s:waiting for transfer to get over\n", __func__);

	wait_event_interruptible(data->wait, data->transfer_complete);

	pch_spi_writereg(data->master, PCH_SSNXCR, SSN_NO_CONTROL);
	dev_dbg(&data->master->dev,
		"%s:no more control over SSN-writing 0 to SSNXCR.", __func__);

	data->transfer_active = false;
	dev_dbg(&data->master->dev,
		"%s set data->transfer_active = false\n", __func__);

	/* clear all interrupts */
	pch_spi_writereg(data->master, PCH_SPSR,
			 pch_spi_readreg(data->master, PCH_SPSR));
	/* disable interrupts */
	pch_spi_setclr_reg(data->master, PCH_SPCR, 0, PCH_ALL);
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
		dev_dbg(&data->master->dev,
			"%s suspend/remove initiated,flushing queue\n",
			__func__);

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

	do {
		/* If we are already processing a message get the next
		transfer structure from the message otherwise retrieve
		the 1st transfer request from the message. */
		spin_lock(&data->lock);

		if (data->cur_trans == NULL) {
			data->cur_trans =
			    list_entry(data->current_msg->transfers.
				       next, struct spi_transfer,
				       transfer_list);
			dev_dbg(&data->master->dev,
				"%s :Getting 1st transfer message\n", __func__);
		} else {
			data->cur_trans =
			    list_entry(data->cur_trans->transfer_list.next,
				       struct spi_transfer,
				       transfer_list);
			dev_dbg(&data->master->dev,
				"%s :Getting next transfer message\n",
				__func__);
		}

		spin_unlock(&data->lock);

		pch_spi_set_tx(data, &bpw, &pmsg);

		/* Control interrupt*/
		pch_spi_set_ir(data);

		/* Disable SPI transfer */
		pch_spi_setclr_reg(data->current_chip->master, PCH_SPCR, 0,
				   SPCR_SPE_BIT);

		/* clear FIFO */
		pch_spi_clear_fifo(data->master);

		/* copy Rx Data */
		pch_spi_copy_rx_data(data, bpw);

		/* free memory */
		kfree(data->pkt_rx_buff);
		data->pkt_rx_buff = NULL;

		kfree(data->pkt_tx_buff);
		data->pkt_tx_buff = NULL;

		/* increment message count */
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
			pch_spi_nomore_transfer(data, pmsg);
		}

		spin_unlock(&data->lock);

	} while (data->cur_trans != NULL);
}

static void pch_spi_free_resources(struct pch_spi_board_data *board_dat)
{
	dev_dbg(&board_dat->pdev->dev, "%s ENTRY\n", __func__);

	/* free workqueue */
	if (board_dat->data->wk != NULL) {
		destroy_workqueue(board_dat->data->wk);
		board_dat->data->wk = NULL;
		dev_dbg(&board_dat->pdev->dev,
			"%s destroy_workqueue invoked successfully\n",
			__func__);
	}

	/* disable interrupts & free IRQ */
	if (board_dat->irq_reg_sts) {
		/* disable interrupts */
		pch_spi_setclr_reg(board_dat->data->master, PCH_SPCR, 0,
				   PCH_ALL);

		/* free IRQ */
		free_irq(board_dat->pdev->irq, board_dat);

		dev_dbg(&board_dat->pdev->dev,
			"%s free_irq invoked successfully\n", __func__);

		board_dat->irq_reg_sts = false;
	}

	/* unmap PCI base address */
	if (board_dat->data->io_remap_addr != 0) {
		pci_iounmap(board_dat->pdev, board_dat->data->io_remap_addr);

		board_dat->data->io_remap_addr = 0;

		dev_dbg(&board_dat->pdev->dev,
			"%s pci_iounmap invoked successfully\n", __func__);
	}

	/* release PCI region */
	if (board_dat->pci_req_sts) {
		pci_release_regions(board_dat->pdev);
		dev_dbg(&board_dat->pdev->dev,
			"%s pci_release_regions invoked successfully\n",
			__func__);
		board_dat->pci_req_sts = false;
	}
}

static int pch_spi_get_resources(struct pch_spi_board_data *board_dat)
{
	void __iomem *io_remap_addr;
	int retval;
	dev_dbg(&board_dat->pdev->dev, "%s ENTRY\n", __func__);

	/* create workqueue */
	board_dat->data->wk = create_singlethread_workqueue(KBUILD_MODNAME);
	if (!board_dat->data->wk) {
		dev_err(&board_dat->pdev->dev,
			"%s create_singlet hread_workqueue failed\n", __func__);
		retval = -EBUSY;
		goto err_return;
	}

	dev_dbg(&board_dat->pdev->dev,
		"%s create_singlethread_workqueue success\n", __func__);

	retval = pci_request_regions(board_dat->pdev, KBUILD_MODNAME);
	if (retval != 0) {
		dev_err(&board_dat->pdev->dev,
			"%s request_region failed\n", __func__);
		goto err_return;
	}

	board_dat->pci_req_sts = true;

	io_remap_addr = pci_iomap(board_dat->pdev, 1, 0);
	if (io_remap_addr == 0) {
		dev_err(&board_dat->pdev->dev,
			"%s pci_iomap failed\n", __func__);
		retval = -ENOMEM;
		goto err_return;
	}

	/* calculate base address for all channels */
	board_dat->data->io_remap_addr = io_remap_addr;

	/* reset PCH SPI h/w */
	pch_spi_reset(board_dat->data->master);
	dev_dbg(&board_dat->pdev->dev,
		"%s pch_spi_reset invoked successfully\n", __func__);

	/* register IRQ */
	retval = request_irq(board_dat->pdev->irq, pch_spi_handler,
			     IRQF_SHARED, KBUILD_MODNAME, board_dat);
	if (retval != 0) {
		dev_err(&board_dat->pdev->dev,
			"%s request_irq failed\n", __func__);
		goto err_return;
	}

	dev_dbg(&board_dat->pdev->dev, "%s request_irq returned=%d\n",
		__func__, retval);

	board_dat->irq_reg_sts = true;
	dev_dbg(&board_dat->pdev->dev, "%s data->irq_reg_sts=true\n", __func__);

err_return:
	if (retval != 0) {
		dev_err(&board_dat->pdev->dev,
			"%s FAIL:invoking pch_spi_free_resources\n", __func__);
		pch_spi_free_resources(board_dat);
	}

	dev_dbg(&board_dat->pdev->dev, "%s Return=%d\n", __func__, retval);

	return retval;
}

static int pch_spi_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{

	struct spi_master *master;

	struct pch_spi_board_data *board_dat;
	int retval;

	dev_dbg(&pdev->dev, "%s ENTRY\n", __func__);

	/* allocate memory for private data */
	board_dat = kzalloc(sizeof(struct pch_spi_board_data), GFP_KERNEL);
	if (board_dat == NULL) {
		dev_err(&pdev->dev,
			" %s memory allocation for private data failed\n",
			__func__);
		retval = -ENOMEM;
		goto err_kmalloc;
	}

	dev_dbg(&pdev->dev,
		"%s memory allocation for private data success\n", __func__);

	/* enable PCI device */
	retval = pci_enable_device(pdev);
	if (retval != 0) {
		dev_err(&pdev->dev, "%s pci_enable_device FAILED\n", __func__);

		goto err_pci_en_device;
	}

	dev_dbg(&pdev->dev, "%s pci_enable_device returned=%d\n",
		__func__, retval);

	board_dat->pdev = pdev;

	/* alllocate memory for SPI master */
	master = spi_alloc_master(&pdev->dev, sizeof(struct pch_spi_data));
	if (master == NULL) {
		retval = -ENOMEM;
		dev_err(&pdev->dev, "%s Fail.\n", __func__);
		goto err_spi_alloc_master;
	}

	dev_dbg(&pdev->dev,
		"%s spi_alloc_master returned non NULL\n", __func__);

	/* initialize members of SPI master */
	master->bus_num = -1;
	master->num_chipselect = PCH_MAX_CS;
	master->setup = pch_spi_setup;
	master->transfer = pch_spi_transfer;
	dev_dbg(&pdev->dev,
		"%s transfer member of SPI master initialized\n", __func__);

	board_dat->data = spi_master_get_devdata(master);

	board_dat->data->master = master;
	board_dat->data->n_curnt_chip = 255;
	board_dat->data->board_dat = board_dat;
	board_dat->data->status = STATUS_RUNNING;

	INIT_LIST_HEAD(&board_dat->data->queue);
	spin_lock_init(&board_dat->data->lock);
	INIT_WORK(&board_dat->data->work, pch_spi_process_messages);
	init_waitqueue_head(&board_dat->data->wait);

	/* allocate resources for PCH SPI */
	retval = pch_spi_get_resources(board_dat);
	if (retval) {
		dev_err(&pdev->dev, "%s fail(retval=%d)\n", __func__, retval);
		goto err_spi_get_resources;
	}

	dev_dbg(&pdev->dev, "%s pch_spi_get_resources returned=%d\n",
		__func__, retval);

	/* save private data in dev */
	pci_set_drvdata(pdev, board_dat);
	dev_dbg(&pdev->dev, "%s invoked pci_set_drvdata\n", __func__);

	/* set master mode */
	pch_spi_set_master_mode(master);
	dev_dbg(&pdev->dev,
		"%s invoked pch_spi_set_master_mode\n", __func__);

	/* Register the controller with the SPI core. */
	retval = spi_register_master(master);
	if (retval != 0) {
		dev_err(&pdev->dev,
			"%s spi_register_master FAILED\n", __func__);
		goto err_spi_reg_master;
	}

	dev_dbg(&pdev->dev, "%s spi_register_master returned=%d\n",
		__func__, retval);


	return 0;

err_spi_reg_master:
	spi_unregister_master(master);
err_spi_get_resources:
err_spi_alloc_master:
	spi_master_put(master);
	pci_disable_device(pdev);
err_pci_en_device:
	kfree(board_dat);
err_kmalloc:
	return retval;
}

static void pch_spi_remove(struct pci_dev *pdev)
{
	struct pch_spi_board_data *board_dat = pci_get_drvdata(pdev);
	int count;

	dev_dbg(&pdev->dev, "%s ENTRY\n", __func__);

	if (!board_dat) {
		dev_err(&pdev->dev,
			"%s pci_get_drvdata returned NULL\n", __func__);
		return;
	}

	/* check for any pending messages; no action is taken if the queue
	 * is still full; but at least we tried.  Unload anyway */
	count = 500;
	spin_lock(&board_dat->data->lock);
	board_dat->data->status = STATUS_EXITING;
	while ((list_empty(&board_dat->data->queue) == 0) && --count) {
		dev_dbg(&board_dat->pdev->dev, "%s :queue not empty\n",
			__func__);
		spin_unlock(&board_dat->data->lock);
		msleep(PCH_SLEEP_TIME);
		spin_lock(&board_dat->data->lock);
	}
	spin_unlock(&board_dat->data->lock);

	/* Free resources allocated for PCH SPI */
	pch_spi_free_resources(board_dat);

	spi_unregister_master(board_dat->data->master);

	/* free memory for private data */
	kfree(board_dat);

	pci_set_drvdata(pdev, NULL);

	/* disable PCI device */
	pci_disable_device(pdev);

	dev_dbg(&pdev->dev, "%s invoked pci_disable_device\n", __func__);
}

#ifdef CONFIG_PM
static int pch_spi_suspend(struct pci_dev *pdev, pm_message_t state)
{
	u8 count;
	int retval;

	struct pch_spi_board_data *board_dat = pci_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s ENTRY\n", __func__);

	if (!board_dat) {
		dev_err(&pdev->dev,
			"%s pci_get_drvdata returned NULL\n", __func__);
		return -EFAULT;
	}

	retval = 0;
	board_dat->suspend_sts = true;

	/* check if the current message is processed:
	   Only after thats done the transfer will be suspended */
	count = 255;
	while ((--count) > 0) {
		if (!(board_dat->data->bcurrent_msg_processing)) {
			dev_dbg(&pdev->dev, "%s board_dat->data->bCurrent_"
				"msg_processing = false\n", __func__);
			break;
		} else {
			dev_dbg(&pdev->dev, "%s board_dat->data->bCurrent_msg_"
				"processing = true\n", __func__);
		}
		msleep(PCH_SLEEP_TIME);
	}

	/* Free IRQ */
	if (board_dat->irq_reg_sts) {
		/* disable all interrupts */
		pch_spi_setclr_reg(board_dat->data->master, PCH_SPCR, 0,
				   PCH_ALL);
		pch_spi_reset(board_dat->data->master);

		free_irq(board_dat->pdev->irq, board_dat);

		board_dat->irq_reg_sts = false;
		dev_dbg(&pdev->dev,
			"%s free_irq invoked successfully.\n", __func__);
	}

	/* save config space */
	retval = pci_save_state(pdev);

	if (retval == 0) {
		dev_dbg(&pdev->dev, "%s pci_save_state returned=%d\n",
			__func__, retval);
		/* disable PM notifications */
		pci_enable_wake(pdev, PCI_D3hot, 0);
		dev_dbg(&pdev->dev,
			"%s pci_enable_wake invoked successfully\n", __func__);
		/* disable PCI device */
		pci_disable_device(pdev);
		dev_dbg(&pdev->dev,
			"%s pci_disable_device invoked successfully\n",
			__func__);
		/* move device to D3hot  state */
		pci_set_power_state(pdev, PCI_D3hot);
		dev_dbg(&pdev->dev,
			"%s pci_set_power_state invoked successfully\n",
			__func__);
	} else {
		dev_err(&pdev->dev, "%s pci_save_state failed\n", __func__);
	}

	dev_dbg(&pdev->dev, "%s return=%d\n", __func__, retval);

	return retval;
}

static int pch_spi_resume(struct pci_dev *pdev)
{
	int retval;

	struct pch_spi_board_data *board = pci_get_drvdata(pdev);
	dev_dbg(&pdev->dev, "%s ENTRY\n", __func__);

	if (!board) {
		dev_err(&pdev->dev,
			"%s pci_get_drvdata returned NULL\n", __func__);
		return -EFAULT;
	}

	/* move device to DO power state */
	pci_set_power_state(pdev, PCI_D0);

	/* restore state */
	pci_restore_state(pdev);

	retval = pci_enable_device(pdev);
	if (retval < 0) {
		dev_err(&pdev->dev,
			"%s pci_enable_device failed\n", __func__);
	} else {
		/* disable PM notifications */
		pci_enable_wake(pdev, PCI_D3hot, 0);

		/* register IRQ handler */
		if (!board->irq_reg_sts) {
			/* register IRQ */
			retval = request_irq(board->pdev->irq, pch_spi_handler,
					     IRQF_SHARED, KBUILD_MODNAME,
					     board);
			if (retval < 0) {
				dev_err(&pdev->dev,
					"%s request_irq failed\n", __func__);
				return retval;
			}
			board->irq_reg_sts = true;

			/* reset PCH SPI h/w */
			pch_spi_reset(board->data->master);
			pch_spi_set_master_mode(board->data->master);

			/* set suspend status to false */
			board->suspend_sts = false;

		}
	}

	dev_dbg(&pdev->dev, "%s returning=%d\n", __func__, retval);

	return retval;
}
#else
#define pch_spi_suspend NULL
#define pch_spi_resume NULL

#endif

static struct pci_driver pch_spi_pcidev = {
	.name = "pch_spi",
	.id_table = pch_spi_pcidev_id,
	.probe = pch_spi_probe,
	.remove = pch_spi_remove,
	.suspend = pch_spi_suspend,
	.resume = pch_spi_resume,
};

static int __init pch_spi_init(void)
{
	return pci_register_driver(&pch_spi_pcidev);
}
module_init(pch_spi_init);

static void __exit pch_spi_exit(void)
{
	pci_unregister_driver(&pch_spi_pcidev);
}
module_exit(pch_spi_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Topcliff PCH SPI PCI Driver");
