// SPDX-License-Identifier: GPL-2.0-or-later
//
// Axiado SPI controller driver (Host mode only)
//
// Copyright (C) 2022-2025 Axiado Corporation (or its affiliates).
//

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/sizes.h>

#include "spi-axiado.h"

/**
 * ax_spi_read - Register Read - 32 bit per word
 * @xspi:	Pointer to the ax_spi structure
 * @offset:	Register offset address
 *
 * @return:	Returns the value of that register
 */
static inline u32 ax_spi_read(struct ax_spi *xspi, u32 offset)
{
	return readl_relaxed(xspi->regs + offset);
}

/**
 * ax_spi_write - Register write - 32 bit per word
 * @xspi:	Pointer to the ax_spi structure
 * @offset:	Register offset address
 * @val:	Value to write into that register
 */
static inline void ax_spi_write(struct ax_spi *xspi, u32 offset, u32 val)
{
	writel_relaxed(val, xspi->regs + offset);
}

/**
 * ax_spi_write_b - Register Read - 8 bit per word
 * @xspi:	Pointer to the ax_spi structure
 * @offset:	Register offset address
 * @val:	Value to write into that register
 */
static inline void ax_spi_write_b(struct ax_spi *xspi, u32 offset, u8 val)
{
	writeb_relaxed(val, xspi->regs + offset);
}

/**
 * ax_spi_init_hw - Initialize the hardware and configure the SPI controller
 * @xspi:	Pointer to the ax_spi structure
 *
 * * On reset the SPI controller is configured to be in host mode.
 * In host mode baud rate divisor is set to 4, threshold value for TX FIFO
 * not full interrupt is set to 1 and size of the word to be transferred as 8 bit.
 *
 * This function initializes the SPI controller to disable and clear all the
 * interrupts, enable manual target select and manual start, deselect all the
 * chip select lines, and enable the SPI controller.
 */
static void ax_spi_init_hw(struct ax_spi *xspi)
{
	u32 reg_value;

	/* Clear CR1 */
	ax_spi_write(xspi, AX_SPI_CR1, AX_SPI_CR1_CLR);

	/* CR1 - CPO CHP MSS SCE SCR */
	reg_value = ax_spi_read(xspi, AX_SPI_CR1);
	reg_value |= AX_SPI_CR1_SCR | AX_SPI_CR1_SCE;

	ax_spi_write(xspi, AX_SPI_CR1, reg_value);

	/* CR2 - MTE SRD SWD SSO */
	reg_value = ax_spi_read(xspi, AX_SPI_CR2);
	reg_value |= AX_SPI_CR2_SWD | AX_SPI_CR2_SRD;

	ax_spi_write(xspi, AX_SPI_CR2, reg_value);

	/* CR3 - Reserverd bits S3W SDL */
	ax_spi_write(xspi, AX_SPI_CR3, AX_SPI_CR3_SDL);

	/* SCDR - Reserved bits SCS SCD */
	ax_spi_write(xspi, AX_SPI_SCDR, (AX_SPI_SCDR_SCS | AX_SPI_SCD_DEFAULT));

	/* IMR */
	ax_spi_write(xspi, AX_SPI_IMR, AX_SPI_IMR_CLR);

	/* ISR - Clear all the interrupt */
	ax_spi_write(xspi, AX_SPI_ISR, AX_SPI_ISR_CLR);
}

/**
 * ax_spi_chipselect - Select or deselect the chip select line
 * @spi:	Pointer to the spi_device structure
 * @is_high:	Select(0) or deselect (1) the chip select line
 */
static void ax_spi_chipselect(struct spi_device *spi, bool is_high)
{
	struct ax_spi *xspi = spi_controller_get_devdata(spi->controller);
	u32 ctrl_reg;

	ctrl_reg = ax_spi_read(xspi, AX_SPI_CR2);
	/* Reset the chip select */
	ctrl_reg &= ~AX_SPI_DEFAULT_TS_MASK;
	ctrl_reg |= spi_get_chipselect(spi, 0);

	ax_spi_write(xspi, AX_SPI_CR2, ctrl_reg);
}

/**
 * ax_spi_config_clock_mode - Sets clock polarity and phase
 * @spi:	Pointer to the spi_device structure
 *
 * Sets the requested clock polarity and phase.
 */
static void ax_spi_config_clock_mode(struct spi_device *spi)
{
	struct ax_spi *xspi = spi_controller_get_devdata(spi->controller);
	u32 ctrl_reg, new_ctrl_reg;

	new_ctrl_reg = ax_spi_read(xspi, AX_SPI_CR1);
	ctrl_reg = new_ctrl_reg;

	/* Set the SPI clock phase and clock polarity */
	new_ctrl_reg &= ~(AX_SPI_CR1_CPHA | AX_SPI_CR1_CPOL);
	if (spi->mode & SPI_CPHA)
		new_ctrl_reg |= AX_SPI_CR1_CPHA;
	if (spi->mode & SPI_CPOL)
		new_ctrl_reg |= AX_SPI_CR1_CPOL;

	if (new_ctrl_reg != ctrl_reg)
		ax_spi_write(xspi, AX_SPI_CR1, new_ctrl_reg);
	ax_spi_write(xspi, AX_SPI_CR1, 0x03);
}

/**
 * ax_spi_config_clock_freq - Sets clock frequency
 * @spi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provides
 *		information about next transfer setup parameters
 *
 * Sets the requested clock frequency.
 * Note: If the requested frequency is not an exact match with what can be
 * obtained using the prescalar value the driver sets the clock frequency which
 * is lower than the requested frequency (maximum lower) for the transfer. If
 * the requested frequency is higher or lower than that is supported by the SPI
 * controller the driver will set the highest or lowest frequency supported by
 * controller.
 */
static void ax_spi_config_clock_freq(struct spi_device *spi,
				     struct spi_transfer *transfer)
{
	struct ax_spi *xspi = spi_controller_get_devdata(spi->controller);

	ax_spi_write(xspi, AX_SPI_SCDR, (AX_SPI_SCDR_SCS | AX_SPI_SCD_DEFAULT));
}

/**
 * ax_spi_setup_transfer - Configure SPI controller for specified transfer
 * @spi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provides
 *		information about next transfer setup parameters
 *
 * Sets the operational mode of SPI controller for the next SPI transfer and
 * sets the requested clock frequency.
 *
 */
static void ax_spi_setup_transfer(struct spi_device *spi,
				 struct spi_transfer *transfer)
{
	struct ax_spi *xspi = spi_controller_get_devdata(spi->controller);

	ax_spi_config_clock_freq(spi, transfer);

	dev_dbg(&spi->dev, "%s, mode %d, %u bits/w, %u clock speed\n",
		__func__, spi->mode, spi->bits_per_word,
		xspi->speed_hz);
}

/**
 * ax_spi_fill_tx_fifo - Fills the TX FIFO with as many bytes as possible
 * @xspi:	Pointer to the ax_spi structure
 */
static void ax_spi_fill_tx_fifo(struct ax_spi *xspi)
{
	unsigned long trans_cnt = 0;

	while ((trans_cnt < xspi->tx_fifo_depth) &&
	       (xspi->tx_bytes > 0)) {
		/* When xspi in busy condition, bytes may send failed,
		 * then spi control did't work thoroughly, add one byte delay
		 */
		if (ax_spi_read(xspi, AX_SPI_IVR) & AX_SPI_IVR_TFOV)
			usleep_range(10, 10);
		if (xspi->tx_buf)
			ax_spi_write_b(xspi, AX_SPI_TXFIFO, *xspi->tx_buf++);
		else
			ax_spi_write_b(xspi, AX_SPI_TXFIFO, 0);

		xspi->tx_bytes--;
		trans_cnt++;
	}
}

/**
 * ax_spi_get_rx_byte - Gets a byte from the RX FIFO buffer
 * @xspi: Controller private data (struct ax_spi *)
 *
 * This function handles the logic of extracting bytes from the 32-bit RX FIFO.
 * It reads a new 32-bit word from AX_SPI_RXFIFO only when the current buffered
 * word has been fully processed (all 4 bytes extracted). It then extracts
 * bytes one by one, assuming the controller is little-endian.
 *
 * Returns: The next 8-bit byte read from the RX FIFO stream.
 */
static u8 ax_spi_get_rx_byte_for_irq(struct ax_spi *xspi)
{
	u8 byte_val;

	/* If all bytes from the current 32-bit word have been extracted,
	 * read a new word from the hardware RX FIFO.
	 */
	if (xspi->bytes_left_in_current_rx_word_for_irq == 0) {
		xspi->current_rx_fifo_word_for_irq = ax_spi_read(xspi, AX_SPI_RXFIFO);
		xspi->bytes_left_in_current_rx_word_for_irq = 4; // A new 32-bit word has 4 bytes
	}

	/* Extract the least significant byte from the current 32-bit word */
	byte_val = (u8)(xspi->current_rx_fifo_word_for_irq & 0xFF);

	/* Shift the word right by 8 bits to prepare the next byte for extraction */
	xspi->current_rx_fifo_word_for_irq >>= 8;
	xspi->bytes_left_in_current_rx_word_for_irq--;

	return byte_val;
}

/**
 * Helper function to process received bytes and check for transfer completion.
 * This avoids code duplication and centralizes the completion logic.
 * Returns true if the transfer was finalized.
 */
static bool ax_spi_process_rx_and_finalize(struct spi_controller *ctlr)
{
	struct ax_spi *xspi = spi_controller_get_devdata(ctlr);

	/* Process any remaining bytes in the RX FIFO */
	u32 avail_bytes = ax_spi_read(xspi, AX_SPI_RX_FBCAR);

	/* This loop handles bytes that are already staged from a previous word read */
	while (xspi->bytes_left_in_current_rx_word_for_irq &&
	       (xspi->rx_copy_remaining || xspi->rx_discard)) {
		u8 b = ax_spi_get_rx_byte_for_irq(xspi);

		if (xspi->rx_discard) {
			xspi->rx_discard--;
		} else {
			*xspi->rx_buf++ = b;
			xspi->rx_copy_remaining--;
		}
	}

	/* This loop processes new words directly from the FIFO */
	while (avail_bytes >= 4 && (xspi->rx_copy_remaining || xspi->rx_discard)) {
		/* This function should handle reading from the FIFO */
		u8 b = ax_spi_get_rx_byte_for_irq(xspi);

		if (xspi->rx_discard) {
			xspi->rx_discard--;
		} else {
			*xspi->rx_buf++ = b;
			xspi->rx_copy_remaining--;
		}
		/* ax_spi_get_rx_byte_for_irq fetches a new word when needed
		 * and updates internal state.
		 */
		if (xspi->bytes_left_in_current_rx_word_for_irq == 3)
			avail_bytes -= 4;
	}

	/* Completion Check: The transfer is truly complete if all expected
	 * RX bytes have been copied or discarded.
	 */
	if (xspi->rx_copy_remaining == 0 && xspi->rx_discard == 0) {
		/* Defensive drain: If for some reason there are leftover bytes
		 * in the HW FIFO after we've logically finished,
		 * read and discard them to prevent them from corrupting the next transfer.
		 * This should be a bounded operation.
		 */
		int safety_words = AX_SPI_RX_FIFO_DRAIN_LIMIT; // Limit to avoid getting stuck

		while (ax_spi_read(xspi, AX_SPI_RX_FBCAR) > 0 && safety_words-- > 0)
			ax_spi_read(xspi, AX_SPI_RXFIFO);

		/* Disable all interrupts for this transfer and finalize. */
		ax_spi_write(xspi, AX_SPI_IMR, 0x00);
		spi_finalize_current_transfer(ctlr);
		return true;
	}

	return false;
}

/**
 * ax_spi_irq - Interrupt service routine of the SPI controller
 * @irq:	IRQ number
 * @dev_id:	Pointer to the xspi structure
 *
 * This function handles RX FIFO almost full and Host Transfer Completed interrupts only.
 * On RX FIFO amlost full interrupt this function reads the received data from RX FIFO and
 * fills the TX FIFO if there is any data remaining to be transferred.
 * On Host Transfer Completed interrupt this function indicates that transfer is completed,
 * the SPI subsystem will clear MTC bit.
 *
 * Return:	IRQ_HANDLED when handled; IRQ_NONE otherwise.
 */
static irqreturn_t ax_spi_irq(int irq, void *dev_id)
{
	struct spi_controller *ctlr = dev_id;
	struct ax_spi *xspi = spi_controller_get_devdata(ctlr);
	u32 intr_status;

	intr_status = ax_spi_read(xspi, AX_SPI_IVR);
	if (!intr_status)
		return IRQ_NONE;

	/* Handle "Message Transfer Complete" interrupt.
	 * This means all bytes have been shifted out of the TX FIFO.
	 * It's time to harvest the final incoming bytes from the RX FIFO.
	 */
	if (intr_status & AX_SPI_IVR_MTCV) {
		/* Clear the MTC interrupt flag immediately. */
		ax_spi_write(xspi, AX_SPI_ISR, AX_SPI_ISR_MTC);

		/* For a TX-only transfer, rx_buf would be NULL.
		 * In the spi-core, rx_copy_remaining would be 0.
		 * So we can finalize immediately.
		 */
		if (!xspi->rx_buf) {
			ax_spi_write(xspi, AX_SPI_IMR, 0x00);
			spi_finalize_current_transfer(ctlr);
			return IRQ_HANDLED;
		}
		/* For a full-duplex transfer, process any remaining RX data.
		 * The helper function will handle finalization if everything is received.
		 */
		ax_spi_process_rx_and_finalize(ctlr);
		return IRQ_HANDLED;
	}

	/* Handle "RX FIFO Full / Threshold Met" interrupt.
	 * This means we need to make space in the RX FIFO by reading from it.
	 */
	if (intr_status & AX_SPI_IVR_RFFV) {
		if (ax_spi_process_rx_and_finalize(ctlr)) {
			/* Transfer was finalized inside the helper, we are done. */
		} else {
			/* RX is not yet complete. If there are still TX bytes to send
			 * (for very long transfers), we can fill the TX FIFO again.
			 */
			if (xspi->tx_bytes)
				ax_spi_fill_tx_fifo(xspi);
		}
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int ax_prepare_message(struct spi_controller *ctlr,
			      struct spi_message *msg)
{
	ax_spi_config_clock_mode(msg->spi);
	return 0;
}

/**
 * ax_transfer_one - Initiates the SPI transfer
 * @ctlr:	Pointer to spi_controller structure
 * @spi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provides
 *		information about next transfer parameters
 *
 * This function fills the TX FIFO, starts the SPI transfer and
 * returns a positive transfer count so that core will wait for completion.
 *
 * Return:	Number of bytes transferred in the last transfer
 */
static int ax_transfer_one(struct spi_controller *ctlr,
			   struct spi_device *spi,
			   struct spi_transfer *transfer)
{
	struct ax_spi *xspi = spi_controller_get_devdata(ctlr);
	int drain_limit;

	/* Pre-transfer cleanup:Flush the RX FIFO to discard any stale data.
	 * This is the crucial part. Before every new transfer, we must ensure
	 * the HW is in a clean state to avoid processing stale data
	 * from a previous, possibly failed or interrupted, transfer.
	 */
	drain_limit = AX_SPI_RX_FIFO_DRAIN_LIMIT; // Sane limit to prevent infinite loop on HW error
	while (ax_spi_read(xspi, AX_SPI_RX_FBCAR) > 0 && drain_limit-- > 0)
		ax_spi_read(xspi, AX_SPI_RXFIFO); // Read and discard

	if (drain_limit <= 0)
		dev_warn(&ctlr->dev, "RX FIFO drain timeout before transfer\n");

	/* Clear any stale interrupt flags from a previous transfer.
	 * This prevents an immediate, false interrupt trigger.
	 */
	ax_spi_write(xspi, AX_SPI_ISR, AX_SPI_ISR_CLR);

	xspi->tx_buf = transfer->tx_buf;
	xspi->rx_buf = transfer->rx_buf;
	xspi->tx_bytes = transfer->len;
	xspi->rx_bytes = transfer->len;

	/* Reset RX 32-bit to byte buffer for each new transfer */
	if (transfer->tx_buf && !transfer->rx_buf) {
		/* TX mode: discard all received data */
		xspi->rx_discard = transfer->len;
		xspi->rx_copy_remaining = 0;
	} else if ((!transfer->tx_buf && transfer->rx_buf) ||
		   (transfer->tx_buf && transfer->rx_buf)) {
		/* RX mode: generate clock by filling TX FIFO with dummy bytes
		 * Full-duplex mode: generate clock by filling TX FIFO
		 */
		xspi->rx_discard = 0;
		xspi->rx_copy_remaining = transfer->len;
	} else {
		/* No TX and RX */
		xspi->rx_discard = 0;
		xspi->rx_copy_remaining = transfer->len;
	}

	ax_spi_setup_transfer(spi, transfer);
	ax_spi_fill_tx_fifo(xspi);
	ax_spi_write(xspi, AX_SPI_CR2, (AX_SPI_CR2_HTE | AX_SPI_CR2_SRD | AX_SPI_CR2_SWD));

	ax_spi_write(xspi, AX_SPI_IMR, (AX_SPI_IMR_MTCM | AX_SPI_IMR_RFFM));
	return transfer->len;
}

/**
 * ax_prepare_transfer_hardware - Prepares hardware for transfer.
 * @ctlr:	Pointer to the spi_controller structure which provides
 *		information about the controller.
 *
 * This function enables SPI host controller.
 *
 * Return:	0 always
 */
static int ax_prepare_transfer_hardware(struct spi_controller *ctlr)
{
	struct ax_spi *xspi = spi_controller_get_devdata(ctlr);

	u32 reg_value;

	reg_value = ax_spi_read(xspi, AX_SPI_CR1);
	reg_value |= AX_SPI_CR1_SCE;

	ax_spi_write(xspi, AX_SPI_CR1, reg_value);

	return 0;
}

/**
 * ax_unprepare_transfer_hardware - Relaxes hardware after transfer
 * @ctlr:	Pointer to the spi_controller structure which provides
 *		information about the controller.
 *
 * This function disables the SPI host controller when no target selected.
 *
 * Return:	0 always
 */
static int ax_unprepare_transfer_hardware(struct spi_controller *ctlr)
{
	struct ax_spi *xspi = spi_controller_get_devdata(ctlr);

	u32 reg_value;

	/* Disable the SPI if target is deselected */
	reg_value = ax_spi_read(xspi, AX_SPI_CR1);
	reg_value &= ~AX_SPI_CR1_SCE;

	ax_spi_write(xspi, AX_SPI_CR1, reg_value);

	return 0;
}

/**
 * ax_spi_detect_fifo_depth - Detect the FIFO depth of the hardware
 * @xspi:	Pointer to the ax_spi structure
 *
 * The depth of the TX FIFO is a synthesis configuration parameter of the SPI
 * IP. The FIFO threshold register is sized so that its maximum value can be the
 * FIFO size - 1. This is used to detect the size of the FIFO.
 */
static void ax_spi_detect_fifo_depth(struct ax_spi *xspi)
{
	/* The MSBs will get truncated giving us the size of the FIFO */
	ax_spi_write(xspi, AX_SPI_TX_FAETR, ALMOST_EMPTY_TRESHOLD);
	xspi->tx_fifo_depth = FIFO_DEPTH;

	/* Set the threshold limit */
	ax_spi_write(xspi, AX_SPI_TX_FAETR, ALMOST_EMPTY_TRESHOLD);
	ax_spi_write(xspi, AX_SPI_RX_FAFTR, ALMOST_FULL_TRESHOLD);
}

/* --- Internal Helper Function for 32-bit RX FIFO Read --- */
/**
 * ax_spi_get_rx_byte - Gets a byte from the RX FIFO buffer
 * @xspi: Controller private data (struct ax_spi *)
 *
 * This function handles the logic of extracting bytes from the 32-bit RX FIFO.
 * It reads a new 32-bit word from AX_SPI_RXFIFO only when the current buffered
 * word has been fully processed (all 4 bytes extracted). It then extracts
 * bytes one by one, assuming the controller is little-endian.
 *
 * Returns: The next 8-bit byte read from the RX FIFO stream.
 */
static u8 ax_spi_get_rx_byte(struct ax_spi *xspi)
{
	u8 byte_val;

	/* If all bytes from the current 32-bit word have been extracted,
	 * read a new word from the hardware RX FIFO.
	 */
	if (xspi->bytes_left_in_current_rx_word == 0) {
		xspi->current_rx_fifo_word = ax_spi_read(xspi, AX_SPI_RXFIFO);
		xspi->bytes_left_in_current_rx_word = 4; // A new 32-bit word has 4 bytes
	}

	/* Extract the least significant byte from the current 32-bit word */
	byte_val = (u8)(xspi->current_rx_fifo_word & 0xFF);

	/* Shift the word right by 8 bits to prepare the next byte for extraction */
	xspi->current_rx_fifo_word >>= 8;
	xspi->bytes_left_in_current_rx_word--;

	return byte_val;
}

static int ax_spi_mem_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct spi_device *spi = mem->spi;
	struct ax_spi *xspi = spi_controller_get_devdata(spi->controller);
	u32 reg_val;
	int ret = 0;
	u8 cmd_buf[AX_SPI_COMMAND_BUFFER_SIZE];
	int cmd_len = 0;
	int i = 0, timeout = AX_SPI_TRX_FIFO_TIMEOUT;
	int bytes_to_discard_from_rx;
	u8 *rx_buf_ptr = (u8 *)op->data.buf.in;
	u8 *tx_buf_ptr = (u8 *)op->data.buf.out;
	u32 rx_count_reg = 0;

	dev_dbg(&spi->dev,
		"%s: cmd:%02x mode:%d.%d.%d.%d addr:%llx len:%d\n",
		__func__, op->cmd.opcode, op->cmd.buswidth, op->addr.buswidth,
		op->dummy.buswidth, op->data.buswidth, op->addr.val,
		op->data.nbytes);

	/* Validate operation parameters: Only 1-bit bus width supported */
	if (op->cmd.buswidth != 1 ||
	    (op->addr.nbytes && op->addr.buswidth != 0 &&
	    op->addr.buswidth != 1) ||
	    (op->dummy.nbytes && op->dummy.buswidth != 0 &&
	    op->dummy.buswidth != 1) ||
	    (op->data.nbytes && op->data.buswidth != 1)) {
		dev_err(&spi->dev, "Unsupported bus width, only 1-bit bus width supported\n");
		return -EOPNOTSUPP;
	}

	/* Initialize controller hardware */
	ax_spi_init_hw(xspi);

	/* Assert chip select (pull low) */
	ax_spi_chipselect(spi, false);

	/* Build command phase: Copy opcode to cmd_buf */
	if (op->cmd.nbytes == 2) {
		cmd_buf[cmd_len++] = (op->cmd.opcode >> 8) & 0xFF;
		cmd_buf[cmd_len++] = op->cmd.opcode & 0xFF;
	} else {
		cmd_buf[cmd_len++] = op->cmd.opcode;
	}

	/* Put address bytes to cmd_buf */
	if (op->addr.nbytes) {
		for (i = op->addr.nbytes - 1; i >= 0; i--) {
			cmd_buf[cmd_len] = (op->addr.val >> (i * 8)) & 0xFF;
			cmd_len++;
		}
	}

	/* Configure controller for desired operation mode (write/read) */
	reg_val = ax_spi_read(xspi, AX_SPI_CR2);
	reg_val |= AX_SPI_CR2_SWD | AX_SPI_CR2_SRI | AX_SPI_CR2_SRD;
	ax_spi_write(xspi, AX_SPI_CR2, reg_val);

	/* Write command and address bytes to TX_FIFO */
	for (i = 0; i < cmd_len; i++)
		ax_spi_write_b(xspi, AX_SPI_TXFIFO, cmd_buf[i]);

	/* Add dummy bytes (for clock generation) or actual data bytes to TX_FIFO */
	if (op->data.dir == SPI_MEM_DATA_IN) {
		for (i = 0; i < op->dummy.nbytes; i++)
			ax_spi_write_b(xspi, AX_SPI_TXFIFO, 0x00);
		for (i = 0; i < op->data.nbytes; i++)
			ax_spi_write_b(xspi, AX_SPI_TXFIFO, 0x00);
	} else {
		for (i = 0; i < op->data.nbytes; i++)
			ax_spi_write_b(xspi, AX_SPI_TXFIFO, tx_buf_ptr[i]);
	}

	/* Start the SPI transmission */
	reg_val = ax_spi_read(xspi, AX_SPI_CR2);
	reg_val |= AX_SPI_CR2_HTE;
	ax_spi_write(xspi, AX_SPI_CR2, reg_val);

	/* Wait for TX FIFO to become empty */
	while (timeout-- > 0) {
		u32 tx_count_reg = ax_spi_read(xspi, AX_SPI_TX_FBCAR);

		if (tx_count_reg == 0) {
			udelay(1);
			break;
		}
		udelay(1);
	}

	/* Handle Data Reception (for read operations) */
	if (op->data.dir == SPI_MEM_DATA_IN) {
		/* Reset the internal RX byte buffer for this new operation.
		 * This ensures ax_spi_get_rx_byte starts fresh for each exec_op call.
		 */
		xspi->bytes_left_in_current_rx_word = 0;
		xspi->current_rx_fifo_word = 0;

		timeout = AX_SPI_TRX_FIFO_TIMEOUT;
		while (timeout-- > 0) {
			rx_count_reg = ax_spi_read(xspi, AX_SPI_RX_FBCAR);
			if (rx_count_reg >= op->data.nbytes)
				break;
			udelay(1); /* Small delay to prevent aggressive busy-waiting */
		}

		if (timeout < 0) {
			ret = -ETIMEDOUT;
			goto out_unlock;
		}

		/* Calculate how many bytes we need to discard from the RX FIFO.
		 * Since we set SRI, we only need to discard the address bytes and
		 * dummy bytes from the RX FIFO.
		 */
		bytes_to_discard_from_rx = op->addr.nbytes + op->dummy.nbytes;
		for (i = 0; i < bytes_to_discard_from_rx; i++)
			ax_spi_get_rx_byte(xspi);

		/* Read actual data bytes into op->data.buf.in */
		for (i = 0; i < op->data.nbytes; i++) {
			*rx_buf_ptr = ax_spi_get_rx_byte(xspi);
			rx_buf_ptr++;
		}
	} else if (op->data.dir == SPI_MEM_DATA_OUT) {
		timeout = AX_SPI_TRX_FIFO_TIMEOUT;
		while (timeout-- > 0) {
			u32 tx_fifo_level = ax_spi_read(xspi, AX_SPI_TX_FBCAR);

			if (tx_fifo_level == 0)
				break;
			udelay(1);
		}
		if (timeout < 0) {
			ret = -ETIMEDOUT;
			goto out_unlock;
		}
	}

out_unlock:
	/* Deassert chip select (pull high) */
	ax_spi_chipselect(spi, true);

	return ret;
}

static int ax_spi_mem_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	struct spi_device *spi = mem->spi;
	struct ax_spi *xspi = spi_controller_get_devdata(spi->controller);
	size_t max_transfer_payload_bytes;
	size_t fifo_total_bytes;
	size_t protocol_overhead_bytes;

	fifo_total_bytes = xspi->tx_fifo_depth;
	/* Calculate protocol overhead bytes according to the real operation each time. */
	protocol_overhead_bytes = op->cmd.nbytes + op->addr.nbytes + op->dummy.nbytes;

	/* Calculate the maximum data payload that can fit into the FIFO. */
	if (fifo_total_bytes <= protocol_overhead_bytes) {
		max_transfer_payload_bytes = 0;
		dev_warn_once(&spi->dev, "SPI FIFO (%zu bytes) is too small for protocol overhead (%zu bytes)! Max data size forced to 0.\n",
			 fifo_total_bytes, protocol_overhead_bytes);
	} else {
		max_transfer_payload_bytes = fifo_total_bytes - protocol_overhead_bytes;
	}

	/* Limit op->data.nbytes based on the calculated max payload and SZ_64K.
	 * This is the value that spi-mem will then use to split requests.
	 */
	if (op->data.nbytes > max_transfer_payload_bytes) {
		op->data.nbytes = max_transfer_payload_bytes;
		dev_dbg(&spi->dev, "%s %d: op->data.nbytes adjusted to %u due to FIFO overhead\n",
			__func__, __LINE__, op->data.nbytes);
	}

	/* Also apply the overall max transfer size */
	if (op->data.nbytes > SZ_64K) {
		op->data.nbytes = SZ_64K;
		dev_dbg(&spi->dev, "%s %d: op->data.nbytes adjusted to %u due to SZ_64K limit\n",
			__func__, __LINE__, op->data.nbytes);
	}

	return 0;
}

static const struct spi_controller_mem_ops ax_spi_mem_ops = {
	.exec_op = ax_spi_mem_exec_op,
	.adjust_op_size = ax_spi_mem_adjust_op_size,
};

/**
 * ax_spi_probe - Probe method for the SPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function initializes the driver data structures and the hardware.
 *
 * Return:	0 on success and error value on error
 */
static int ax_spi_probe(struct platform_device *pdev)
{
	int ret = 0, irq;
	struct spi_controller *ctlr;
	struct ax_spi *xspi;
	u32 num_cs;

	ctlr = devm_spi_alloc_host(&pdev->dev, sizeof(*xspi));
	if (!ctlr)
		return -ENOMEM;

	xspi = spi_controller_get_devdata(ctlr);
	ctlr->dev.of_node = pdev->dev.of_node;
	platform_set_drvdata(pdev, ctlr);

	xspi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xspi->regs)) {
		ret = PTR_ERR(xspi->regs);
		goto remove_ctlr;
	}

	xspi->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(xspi->pclk)) {
		dev_err(&pdev->dev, "pclk clock not found.\n");
		ret = PTR_ERR(xspi->pclk);
		goto remove_ctlr;
	}

	xspi->ref_clk = devm_clk_get(&pdev->dev, "ref");
	if (IS_ERR(xspi->ref_clk)) {
		dev_err(&pdev->dev, "ref clock not found.\n");
		ret = PTR_ERR(xspi->ref_clk);
		goto remove_ctlr;
	}

	ret = clk_prepare_enable(xspi->pclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable APB clock.\n");
		goto remove_ctlr;
	}

	ret = clk_prepare_enable(xspi->ref_clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto clk_dis_apb;
	}

	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, SPI_AUTOSUSPEND_TIMEOUT);
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = of_property_read_u32(pdev->dev.of_node, "num-cs", &num_cs);
	if (ret < 0)
		ctlr->num_chipselect = AX_SPI_DEFAULT_NUM_CS;
	else
		ctlr->num_chipselect = num_cs;

	ax_spi_detect_fifo_depth(xspi);

	xspi->current_rx_fifo_word = 0;
	xspi->bytes_left_in_current_rx_word = 0;

	/* Initialize IRQ-related variables */
	xspi->bytes_left_in_current_rx_word_for_irq = 0;
	xspi->current_rx_fifo_word_for_irq = 0;

	/* SPI controller initializations */
	ax_spi_init_hw(xspi);

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		ret = -ENXIO;
		goto clk_dis_all;
	}

	ret = devm_request_irq(&pdev->dev, irq, ax_spi_irq,
			       0, pdev->name, ctlr);
	if (ret != 0) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "request_irq failed\n");
		goto clk_dis_all;
	}

	ctlr->use_gpio_descriptors = true;
	ctlr->prepare_transfer_hardware = ax_prepare_transfer_hardware;
	ctlr->prepare_message = ax_prepare_message;
	ctlr->transfer_one = ax_transfer_one;
	ctlr->unprepare_transfer_hardware = ax_unprepare_transfer_hardware;
	ctlr->set_cs = ax_spi_chipselect;
	ctlr->auto_runtime_pm = true;
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;

	xspi->clk_rate = clk_get_rate(xspi->ref_clk);
	/* Set to default valid value */
	ctlr->max_speed_hz = xspi->clk_rate / 2;
	xspi->speed_hz = ctlr->max_speed_hz;

	ctlr->bits_per_word_mask = SPI_BPW_MASK(8);

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	ctlr->mem_ops = &ax_spi_mem_ops;

	ret = spi_register_controller(ctlr);
	if (ret) {
		dev_err(&pdev->dev, "spi_register_controller failed\n");
		goto clk_dis_all;
	}

	return ret;

clk_dis_all:
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(xspi->ref_clk);
clk_dis_apb:
	clk_disable_unprepare(xspi->pclk);
remove_ctlr:
	spi_controller_put(ctlr);
	return ret;
}

/**
 * ax_spi_remove - Remove method for the SPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees all resources allocated to
 * the device.
 */
static void ax_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr = platform_get_drvdata(pdev);
	struct ax_spi *xspi = spi_controller_get_devdata(ctlr);

	spi_unregister_controller(ctlr);

	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	clk_disable_unprepare(xspi->ref_clk);
	clk_disable_unprepare(xspi->pclk);
}

/**
 * ax_spi_suspend - Suspend method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function disables the SPI controller and
 * changes the driver state to "suspend"
 *
 * Return:	0 on success and error value on error
 */
static int __maybe_unused ax_spi_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);

	return spi_controller_suspend(ctlr);
}

/**
 * ax_spi_resume - Resume method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function changes the driver state to "ready"
 *
 * Return:	0 on success and error value on error
 */
static int __maybe_unused ax_spi_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct ax_spi *xspi = spi_controller_get_devdata(ctlr);

	ax_spi_init_hw(xspi);
	return spi_controller_resume(ctlr);
}

/**
 * ax_spi_runtime_resume - Runtime resume method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function enables the clocks
 *
 * Return:	0 on success and error value on error
 */
static int __maybe_unused ax_spi_runtime_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct ax_spi *xspi = spi_controller_get_devdata(ctlr);
	int ret;

	ret = clk_prepare_enable(xspi->pclk);
	if (ret) {
		dev_err(dev, "Cannot enable APB clock.\n");
		return ret;
	}

	ret = clk_prepare_enable(xspi->ref_clk);
	if (ret) {
		dev_err(dev, "Cannot enable device clock.\n");
		clk_disable_unprepare(xspi->pclk);
		return ret;
	}
	return 0;
}

/**
 * ax_spi_runtime_suspend - Runtime suspend method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function disables the clocks
 *
 * Return:	Always 0
 */
static int __maybe_unused ax_spi_runtime_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct ax_spi *xspi = spi_controller_get_devdata(ctlr);

	clk_disable_unprepare(xspi->ref_clk);
	clk_disable_unprepare(xspi->pclk);

	return 0;
}

static const struct dev_pm_ops ax_spi_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(ax_spi_runtime_suspend,
			   ax_spi_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(ax_spi_suspend, ax_spi_resume)
};

static const struct of_device_id ax_spi_of_match[] = {
	{ .compatible = "axiado,ax3000-spi" },
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, ax_spi_of_match);

/* ax_spi_driver - This structure defines the SPI subsystem platform driver */
static struct platform_driver ax_spi_driver = {
	.probe	= ax_spi_probe,
	.remove	= ax_spi_remove,
	.driver = {
		.name = AX_SPI_NAME,
		.of_match_table = ax_spi_of_match,
		.pm = &ax_spi_dev_pm_ops,
	},
};

module_platform_driver(ax_spi_driver);

MODULE_AUTHOR("Axiado Corporation");
MODULE_DESCRIPTION("Axiado SPI Host driver");
MODULE_LICENSE("GPL");
