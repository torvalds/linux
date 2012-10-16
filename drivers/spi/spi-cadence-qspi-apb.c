/*
 * Driver for Cadence QSPI Controller
 *
 * Copyright (C) 2012 Altera Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include "spi-cadence-qspi.h"
#include "spi-cadence-qspi-apb.h"

static unsigned int cadence_qspi_apb_cmd2addr(const unsigned char* addr_buf,
	unsigned int addr_width)
{
	unsigned int addr;

	addr = (addr_buf[0] << 16) | (addr_buf[1] << 8) | addr_buf[2];

	if (addr_width == 4)
		addr = (addr << 8) | addr_buf[3];

	return addr;
}

static void cadence_qspi_apb_read_fifo_data(void *dest, const void *src_ahb_addr,
			 unsigned int bytes)
{
	unsigned int temp;
	int remaining = bytes;
	unsigned int *dest_ptr = (unsigned int *)dest;
	unsigned int *src_ptr = (unsigned int *)src_ahb_addr;

	while (remaining > 0) {
		if (remaining >= CQSPI_FIFO_WIDTH) {
			*dest_ptr = CQSPI_READL(src_ptr);
			remaining -= CQSPI_FIFO_WIDTH;
		} else {
			/* dangling bytes */
			temp = CQSPI_READL(src_ptr);
			memcpy(dest_ptr, &temp, remaining);
			break;
		}
		dest_ptr++;
	}

	return;
}

static void cadence_qspi_apb_write_fifo_data(void *dest_ahb_addr,
					const void *src, unsigned int bytes)
{
	unsigned int temp;
	int remaining = bytes;
	unsigned int *dest_ptr = (unsigned int *)dest_ahb_addr;
	unsigned int *src_ptr = (unsigned int *)src;

	while (remaining > 0) {
		if (remaining >= CQSPI_FIFO_WIDTH) {
			CQSPI_WRITEL(*src_ptr, dest_ptr);
			remaining -= CQSPI_FIFO_WIDTH;
		} else {
			/* dangling bytes */
			memcpy(&temp, src_ptr, remaining);
			CQSPI_WRITEL(temp, dest_ptr);
			break;
		}
		src_ptr++;
	}

	return;
}

/* Return 1 if idle, otherwise return 0 (busy). */
static unsigned int cadence_qspi_wait_idle(void * reg_base) {
	unsigned int count = 0;
	unsigned timeout;

	timeout = cadence_qspi_init_timeout(CQSPI_TIMEOUT_MS);
	while (cadence_qspi_check_timeout(timeout)) {
		if (CQSPI_REG_IS_IDLE(reg_base)) {
			/* Read few times in succession to ensure it does
			not transition low again */
			count++;
			if (count >= CQSPI_POLL_IDLE_RETRY)
				return 1;
		} else {
			count = 0;
		}
	}

	/* Timeout, in busy mode. */
	pr_err("QSPI: QSPI is still busy after %dms timeout.\n",
		CQSPI_TIMEOUT_MS);
	return 0;
}

static void cadence_qspi_apb_readdata_capture(void *reg_base,
			unsigned int bypass, unsigned int delay)
{
	unsigned int reg;

	cadence_qspi_apb_controller_disable(reg_base);
	reg = CQSPI_READL(reg_base + CQSPI_REG_READCAPTURE);

	if (bypass) {
		reg |= (1 << CQSPI_REG_READCAPTURE_BYPASS_LSB);
	} else {
		reg &= ~(1 << CQSPI_REG_READCAPTURE_BYPASS_LSB);
	}

	reg &= ~(CQSPI_REG_READCAPTURE_DELAY_MASK
		<< CQSPI_REG_READCAPTURE_DELAY_LSB);

	reg |= ((delay & CQSPI_REG_READCAPTURE_DELAY_MASK)
		<< CQSPI_REG_READCAPTURE_DELAY_LSB);

	CQSPI_WRITEL(reg, reg_base + CQSPI_REG_READCAPTURE);

	cadence_qspi_apb_controller_enable(reg_base);
	return;
}

static void cadence_qspi_apb_config_baudrate_div(void *reg_base,
		unsigned int ref_clk_hz, unsigned int sclk_hz)
{
	unsigned int reg;
	unsigned int div;

	cadence_qspi_apb_controller_disable(reg_base);
	reg = CQSPI_READL(reg_base + CQSPI_REG_CONFIG);
	reg &= ~(CQSPI_REG_CONFIG_BAUD_MASK << CQSPI_REG_CONFIG_BAUD_LSB);

	div = ref_clk_hz / sclk_hz;

	/* Recalculate the baudrate divisor based on QSPI specification. */
	if (div > 32)
		div = 32;

	/* Check if even number. */
	if (div & 1)
		div = (div / 2);
	else
		div = (div / 2) - 1;

	pr_debug("QSPI: ref_clk %dHz sclk %dHz div 0x%x\n", ref_clk_hz,
		sclk_hz, div);

	div = (div & CQSPI_REG_CONFIG_BAUD_MASK) << CQSPI_REG_CONFIG_BAUD_LSB;
	reg |= div;
	CQSPI_WRITEL(reg, reg_base + CQSPI_REG_CONFIG);

	cadence_qspi_apb_controller_enable(reg_base);
	return;
}

static void cadence_qspi_apb_chipselect(void * reg_base,
	unsigned int chip_select, unsigned int decoder_enable)
{
	unsigned int reg;

	cadence_qspi_apb_controller_disable(reg_base);

	pr_debug("QSPI: chipselect %d decode %d\n", chip_select,
		decoder_enable);

	reg = CQSPI_READL(reg_base + CQSPI_REG_CONFIG);
	/* docoder */
	if (decoder_enable)
		reg |= CQSPI_REG_CONFIG_DECODE_MASK;
	else {
		reg &= ~CQSPI_REG_CONFIG_DECODE_MASK;

		/* Convert CS if without decoder.
		 * CS0 to 4b'1110
		 * CS1 to 4b'1101
		 * CS2 to 4b'1011
		 * CS3 to 4b'0111
		 */
		chip_select = 0xF & ~(1 << chip_select);
	}

	reg &= ~(CQSPI_REG_CONFIG_CHIPSELECT_MASK
			<< CQSPI_REG_CONFIG_CHIPSELECT_LSB);
	reg |= (chip_select & CQSPI_REG_CONFIG_CHIPSELECT_MASK)
			<< CQSPI_REG_CONFIG_CHIPSELECT_LSB;
	CQSPI_WRITEL(reg, reg_base + CQSPI_REG_CONFIG);

	cadence_qspi_apb_controller_enable(reg_base);
	return;
}

static int cadence_qspi_apb_exec_flash_cmd(void *reg_base, unsigned int reg)
{
	unsigned int timeout;

	/* Write the CMDCTRL without start execution. */
	CQSPI_WRITEL(reg, reg_base + CQSPI_REG_CMDCTRL);
	/* Start execute */
	reg |= CQSPI_REG_CMDCTRL_EXECUTE_MASK;
	CQSPI_WRITEL(reg, reg_base + CQSPI_REG_CMDCTRL);

	/* Polling for completion. */
	timeout = cadence_qspi_init_timeout(CQSPI_TIMEOUT_MS);
	while (cadence_qspi_check_timeout(timeout)) {
		reg = CQSPI_READL(reg_base + CQSPI_REG_CMDCTRL) &
			CQSPI_REG_CMDCTRL_INPROGRESS_MASK;
		if (!reg)
			break;
	}

	if (reg != 0) {
		pr_err("QSPI: flash cmd execute time out\n");
		return -EIO;
	}

	/* Polling QSPI idle status. */
	if (!cadence_qspi_wait_idle(reg_base))
		return -EIO;

	return 0;
}

/* For command RDID, RDSR. */
static int cadence_qspi_apb_command_read(void *reg_base,
	unsigned int txlen, const unsigned char *txbuf,
	unsigned rxlen, unsigned char *rxbuf)
{
	unsigned int reg;
	unsigned int read_len;
	int status;

	if (!rxlen || rxlen > CQSPI_STIG_DATA_LEN_MAX || rxbuf == NULL) {
		pr_err("QSPI: Invalid input argument, len %d rxbuf 0x%08x\n",
			rxlen, (unsigned int)rxbuf);
		return -EINVAL;
	}

	reg = txbuf[0] << CQSPI_REG_CMDCTRL_OPCODE_LSB;

	reg |= (0x1 << CQSPI_REG_CMDCTRL_RD_EN_LSB);

	/* 0 means 1 byte. */
	reg |= (((rxlen - 1) & CQSPI_REG_CMDCTRL_RD_BYTES_MASK)
		<< CQSPI_REG_CMDCTRL_RD_BYTES_LSB);
	status = cadence_qspi_apb_exec_flash_cmd(reg_base, reg);
	if (status != 0)
		return status;

	reg = CQSPI_READL(reg_base + CQSPI_REG_CMDREADDATALOWER);

	/* Put the read value into rx_buf */
	read_len = (rxlen > 4) ? 4 : rxlen;
	memcpy(rxbuf, &reg, read_len);
	rxbuf += read_len;

	if (rxlen > 4) {
		reg = CQSPI_READL(reg_base + CQSPI_REG_CMDREADDATAUPPER);

		read_len = rxlen - read_len;
		memcpy(rxbuf, &reg, read_len);
	}

	return 0;
}

/* For commands: WRSR, WREN, WRDI, CHIP_ERASE, BE, etc. */
static int cadence_qspi_apb_command_write(void *reg_base, unsigned txlen,
	const unsigned char *txbuf)
{
	unsigned int reg;
	unsigned int addr_value;
	unsigned int data;

	if (!txlen || txlen > 5 || txbuf == NULL) {
		pr_err("QSPI: Invalid input argument, cmdlen %d txbuf 0x%08x\n",
			txlen, (unsigned int)txbuf);
		return -EINVAL;
	}

	reg = txbuf[0] << CQSPI_REG_CMDCTRL_OPCODE_LSB;
	if (txlen == 2 || txlen == 3) {
		/* Command with data only. */
		reg |= (0x1 << CQSPI_REG_CMDCTRL_WR_EN_LSB);
		reg |= ((txlen - 2) & CQSPI_REG_CMDCTRL_WR_BYTES_MASK)
			<< CQSPI_REG_CMDCTRL_WR_BYTES_LSB;

		memcpy(&data, &txbuf[1], txlen - 1);
		/* Write the data */
		CQSPI_WRITEL(data, reg_base + CQSPI_REG_CMDWRITEDATALOWER);
	}
	else if (txlen == 4 || txlen == 5) {
		/* Command with address */
		reg |= (0x1 << CQSPI_REG_CMDCTRL_ADDR_EN_LSB);
		/* Number of bytes to write. */
		reg |= ((txlen - 2) & CQSPI_REG_CMDCTRL_ADD_BYTES_MASK)
			<< CQSPI_REG_CMDCTRL_ADD_BYTES_LSB;
		/* Get address */
		addr_value = cadence_qspi_apb_cmd2addr(&txbuf[1],
			txlen >=5 ? 4 : 3);

		CQSPI_WRITEL(addr_value, reg_base + CQSPI_REG_CMDADDRESS);
	}

	return cadence_qspi_apb_exec_flash_cmd(reg_base, reg);
}

static int cadence_qspi_apb_indirect_read_setup(void *reg_base,
	unsigned int ahb_phy_addr, unsigned txlen, const unsigned char *txbuf,
	unsigned int addr_bytes)
{
	unsigned int reg;
	unsigned int addr_value;
	unsigned int dummy_clk;
	unsigned int dummy_bytes;

	if ((addr_bytes == 3 && txlen < 4) || (addr_bytes == 4 && txlen < 5)) {
		pr_err("QSPI: Invalid txbuf length, length %d\n", txlen);
		return -EINVAL;
	}

	CQSPI_WRITEL((ahb_phy_addr & CQSPI_INDIRECTTRIGGER_ADDR_MASK),
		reg_base + CQSPI_REG_INDIRECTTRIGGER);

	reg = txbuf[0] << CQSPI_REG_RD_INSTR_OPCODE_LSB;

#ifdef CONFIG_M25PXX_USE_FAST_READ_QUAD_OUTPUT
	reg |= (CQSPI_INST_TYPE_QUAD << CQSPI_REG_RD_INSTR_TYPE_DATA_LSB);
#endif /* CONFIG_M25PXX_USE_FAST_READ_QUAD_OUTPUT */

	/* Get address */
	addr_value = cadence_qspi_apb_cmd2addr(&txbuf[1], addr_bytes);
	CQSPI_WRITEL(addr_value, reg_base + CQSPI_REG_INDIRECTRDSTARTADDR);

	/* The remaining lenght is dummy bytes. */
	dummy_bytes = txlen - addr_bytes - 1;

	/* Setup dummy clock cycles */
	if (dummy_bytes) {

		if (dummy_bytes > CQSPI_DUMMY_BYTES_MAX)
			dummy_bytes = CQSPI_DUMMY_BYTES_MAX;

		reg |= (1 << CQSPI_REG_RD_INSTR_MODE_EN_LSB);
		/* Set all high to ensure chip doesn't enter XIP */
		CQSPI_WRITEL(0xFF, reg_base + CQSPI_REG_MODE_BIT);

		/* Convert to clock cycles. */
		dummy_clk = dummy_bytes * CQSPI_DUMMY_CLKS_PER_BYTE;
		/* Need to minus the mode byte (8 clocks). */
		dummy_clk -= CQSPI_DUMMY_CLKS_PER_BYTE;

		if (dummy_clk)
			reg |= (dummy_clk & CQSPI_REG_RD_INSTR_DUMMY_MASK)
				<< CQSPI_REG_RD_INSTR_DUMMY_LSB;
	}

	CQSPI_WRITEL(reg, reg_base + CQSPI_REG_RD_INSTR);

	/* Set device size */
	reg = CQSPI_READL(reg_base + CQSPI_REG_SIZE);
	reg &= ~CQSPI_REG_SIZE_ADDRESS_MASK;
	reg |= (addr_bytes - 1);
	CQSPI_WRITEL(reg, reg_base + CQSPI_REG_SIZE);
	return 0;
}

static int cadence_qspi_apb_indirect_read_execute(
	struct struct_cqspi *cadence_qspi, unsigned rxlen,
	unsigned char *rxbuf)
{
	unsigned int reg = 0;
	unsigned int timeout;
	unsigned int watermark = CQSPI_REG_SRAM_THRESHOLD_BYTES;
	unsigned int *irq_status = &(cadence_qspi->irq_status);
	struct platform_device *pdev = cadence_qspi->pdev;
	struct cqspi_platform_data *pdata = pdev->dev.platform_data;
	void *reg_base = cadence_qspi->iobase;
	void *ahb_base = cadence_qspi->qspi_ahb_virt;
	int remaining = (int)rxlen;
	int ret = 0;

	if (remaining < watermark)
		watermark = remaining;

	CQSPI_WRITEL(watermark, reg_base + CQSPI_REG_INDIRECTRDWATERMARK);
	CQSPI_WRITEL(remaining, reg_base + CQSPI_REG_INDIRECTRDBYTES);
	CQSPI_WRITEL(pdata->fifo_depth - CQSPI_REG_SRAM_RESV_WORDS,
		reg_base + CQSPI_REG_SRAMPARTITION);

	/* Clear all interrupts. */
	CQSPI_WRITEL(CQSPI_IRQ_STATUS_MASK, reg_base + CQSPI_REG_IRQSTATUS);

	CQSPI_WRITEL(CQSPI_IRQ_MASK_RD, reg_base + CQSPI_REG_IRQMASK);

	CQSPI_WRITEL(CQSPI_REG_INDIRECTRD_START_MASK,
			reg_base + CQSPI_REG_INDIRECTRD);

	while (remaining > 0) {
		ret = wait_event_interruptible_timeout(cadence_qspi->waitqueue,
			*irq_status, CQSPI_TIMEOUT_MS);
		if (!ret) {
			pr_err("QSPI: Indirect read timeout\n");
			ret = -ETIMEDOUT;
			goto failrd;
		}

		if (*irq_status & CQSPI_IRQ_STATUS_ERR) {
			/* Error occurred */
			pr_err("QSPI: Indirect read error IRQ status 0x%08x\n",
				*irq_status);
			ret = -EPERM;
			goto failrd;
		}

		if (*irq_status & (CQSPI_REG_IRQ_IND_RD_OVERFLOW |
			CQSPI_REG_IRQ_IND_COMP | CQSPI_REG_IRQ_WATERMARK)) {

			reg = CQSPI_GET_RD_SRAM_LEVEL(reg_base);
			/* convert to bytes */
			reg *= CQSPI_FIFO_WIDTH;
			reg = reg > remaining ? remaining : reg;
			/* Read data from FIFO. */
			cadence_qspi_apb_read_fifo_data(rxbuf, ahb_base, reg);
			rxbuf += reg;
			remaining -= reg;
		}
	}

	/* Check indirect done status */
	timeout = cadence_qspi_init_timeout(CQSPI_TIMEOUT_MS);
	while (cadence_qspi_check_timeout(timeout)) {
		reg = CQSPI_READL(reg_base + CQSPI_REG_INDIRECTRD);
		if (reg & CQSPI_REG_INDIRECTRD_DONE_MASK)
			break;
	}

	if (!(reg & CQSPI_REG_INDIRECTRD_DONE_MASK)) {
		pr_err("QSPI : Indirect read completion status error with "
			"reg 0x%08x\n", reg);
		ret = -ETIMEDOUT;
		goto failrd;
	}

	/* Disable interrupt */
	CQSPI_WRITEL(0, reg_base + CQSPI_REG_IRQMASK);

	/* Clear indirect completion status */
	CQSPI_WRITEL(CQSPI_REG_INDIRECTRD_DONE_MASK,
		reg_base + CQSPI_REG_INDIRECTRD);
	return 0;

failrd:
	/* Disable interrupt */
	CQSPI_WRITEL(0, reg_base + CQSPI_REG_IRQMASK);

	/* Cancel the indirect read */
	CQSPI_WRITEL(CQSPI_REG_INDIRECTWR_CANCEL_MASK,
			reg_base + CQSPI_REG_INDIRECTRD);
	return ret;
}

static int cadence_qspi_apb_indirect_write_setup(void *reg_base,
	unsigned int ahb_phy_addr, unsigned txlen, const unsigned char *txbuf)
{
	unsigned int reg;
	unsigned int addr_bytes = (txlen >= 5) ? 4: 3;

	if (txlen < 4 || txbuf == NULL) {
		pr_err("QSPI: Invalid input argument, txlen %d txbuf 0x%08x\n",
			txlen, (unsigned int)txbuf);
		return -EINVAL;
	}

	CQSPI_WRITEL( (ahb_phy_addr & CQSPI_INDIRECTTRIGGER_ADDR_MASK),
		reg_base + CQSPI_REG_INDIRECTTRIGGER);

	/* Set opcode. */
	reg = txbuf[0] << CQSPI_REG_WR_INSTR_OPCODE_LSB;
	CQSPI_WRITEL(reg, reg_base + CQSPI_REG_WR_INSTR);

	/* Setup write address. */
	reg = cadence_qspi_apb_cmd2addr(&txbuf[1], addr_bytes);
	CQSPI_WRITEL(reg, reg_base + CQSPI_REG_INDIRECTWRSTARTADDR);

	reg = CQSPI_READL(reg_base + CQSPI_REG_SIZE);
	reg &= ~CQSPI_REG_SIZE_ADDRESS_MASK;
	reg |= (addr_bytes - 1);
	CQSPI_WRITEL(reg, reg_base +  CQSPI_REG_SIZE);
	return 0;
}

static int cadence_qspi_apb_indirect_write_execute(
	struct struct_cqspi *cadence_qspi, unsigned txlen,
	const unsigned char *txbuf)
{
	int ret;
	unsigned int timeout;
	unsigned int reg = 0;
	unsigned int *irq_status = &(cadence_qspi->irq_status);
	void *reg_base = cadence_qspi->iobase;
	void *ahb_base = cadence_qspi->qspi_ahb_virt;
	struct platform_device *pdev = cadence_qspi->pdev;
	struct cqspi_platform_data *pdata = pdev->dev.platform_data;
	struct cqspi_flash_pdata *f_pdata =
			&(pdata->f_pdata[cadence_qspi->current_cs]);
	unsigned int page_size = f_pdata->page_size;
	int remaining = (int)txlen;
	unsigned int write_bytes;

	CQSPI_WRITEL(remaining, reg_base + CQSPI_REG_INDIRECTWRBYTES);

	CQSPI_WRITEL(CQSPI_REG_SRAM_THRESHOLD_BYTES, reg_base +
			CQSPI_REG_INDIRECTWRWATERMARK);

	CQSPI_WRITEL(CQSPI_REG_SRAM_PARTITION_WR,
			reg_base + CQSPI_REG_SRAMPARTITION);

	/* Clear all interrupts. */
	CQSPI_WRITEL(CQSPI_IRQ_STATUS_MASK, reg_base + CQSPI_REG_IRQSTATUS);

	CQSPI_WRITEL(CQSPI_IRQ_MASK_WR, reg_base + CQSPI_REG_IRQMASK);

	CQSPI_WRITEL(CQSPI_REG_INDIRECTWR_START_MASK,
			reg_base + CQSPI_REG_INDIRECTWR);

	/* Write a page or remaining bytes. */
	write_bytes = remaining > page_size ? page_size : remaining;
	/* Fill up the data at the begining */
	cadence_qspi_apb_write_fifo_data(ahb_base, txbuf, write_bytes);
	txbuf += write_bytes;
	remaining -= write_bytes;

	while (remaining > 0) {
		ret = wait_event_interruptible_timeout(cadence_qspi->waitqueue,
			*irq_status, CQSPI_TIMEOUT_MS);
		if (!ret) {
			pr_err("QSPI: Indirect write timeout\n");
			ret = -ETIMEDOUT;
			goto failwr;
		}

		if (*irq_status & CQSPI_IRQ_STATUS_ERR) {
			/* Error occurred */
			pr_err("QSPI : Indirect write error"
				"IRQ status 0x%08x\n", *irq_status);
			ret = -EPERM;
			goto failwr;
		}

		if (*irq_status & (CQSPI_REG_IRQ_UNDERFLOW |
			CQSPI_REG_IRQ_IND_COMP | CQSPI_REG_IRQ_WATERMARK)){
			/* Calculate number of bytes to write. */
			write_bytes = remaining > page_size ?
				page_size : remaining;

			cadence_qspi_apb_write_fifo_data(ahb_base, txbuf,
				write_bytes);
			txbuf  += write_bytes;
			remaining -= write_bytes;
		}
	}

	/* Check indirect done status */
	timeout = cadence_qspi_init_timeout(CQSPI_TIMEOUT_MS);
	while (cadence_qspi_check_timeout(timeout)) {
		reg = CQSPI_READL(reg_base + CQSPI_REG_INDIRECTWR);
		if (reg & CQSPI_REG_INDIRECTWR_DONE_MASK)
			break;
	}

	if (!(reg & CQSPI_REG_INDIRECTWR_DONE_MASK)) {
		pr_err("QSPI: Indirect write completion status error with "
			"reg 0x%08x\n", reg);
		ret = -ETIMEDOUT;
		goto failwr;
	}

	/* Disable interrupt. */
	CQSPI_WRITEL(0, reg_base + CQSPI_REG_IRQMASK);

	/* Clear indirect completion status */
	CQSPI_WRITEL(CQSPI_REG_INDIRECTWR_DONE_MASK,
		reg_base + CQSPI_REG_INDIRECTWR);
	return 0;

failwr:
	/* Disable interrupt. */
	CQSPI_WRITEL(0, reg_base + CQSPI_REG_IRQMASK);

	/* Cancel the indirect write */
	CQSPI_WRITEL(CQSPI_REG_INDIRECTWR_CANCEL_MASK,
		reg_base + CQSPI_REG_INDIRECTWR);
	return ret;
}

void cadence_qspi_apb_controller_enable(void *reg_base)
{
	unsigned int reg;
	reg = CQSPI_READL(reg_base + CQSPI_REG_CONFIG);
	reg |= CQSPI_REG_CONFIG_ENABLE_MASK;
	CQSPI_WRITEL(reg, reg_base + CQSPI_REG_CONFIG);
	return;
}

void cadence_qspi_apb_controller_disable(void *reg_base)
{
	unsigned int reg;
	reg = CQSPI_READL(reg_base + CQSPI_REG_CONFIG);
	reg &= ~CQSPI_REG_CONFIG_ENABLE_MASK;
	CQSPI_WRITEL(reg, reg_base + CQSPI_REG_CONFIG);
	return;
}

unsigned int cadence_qspi_apb_is_controller_ready(void *reg_base)
{
	return cadence_qspi_wait_idle(reg_base);
}

void cadence_qspi_apb_controller_init(struct struct_cqspi *cadence_qspi)
{
	cadence_qspi_apb_controller_disable(cadence_qspi->iobase);

	/* Configure the remap address register, no remap */
	CQSPI_WRITEL(0, cadence_qspi->iobase + CQSPI_REG_REMAP);

	/* Disable all interrupts. */
	CQSPI_WRITEL(0, cadence_qspi->iobase + CQSPI_REG_IRQMASK);

	/* Set read data capture to default. */
	cadence_qspi_apb_readdata_capture(cadence_qspi->iobase, 1, 0);

	cadence_qspi_apb_controller_enable(cadence_qspi->iobase);
	return;
}

void cadence_qspi_apb_delay(struct struct_cqspi *cadence_qspi,
	unsigned int ref_clk, unsigned int sclk_hz)
{
	void __iomem *iobase = cadence_qspi->iobase;
	struct platform_device *pdev = cadence_qspi->pdev;
	struct cqspi_platform_data *pdata = pdev->dev.platform_data;
	struct cqspi_flash_pdata *f_pdata =
			&(pdata->f_pdata[cadence_qspi->current_cs]);
	unsigned int ref_clk_ns;
	unsigned int sclk_ns;
	unsigned int tshsl, tchsh, tslch, tsd2d;
	unsigned int reg;

	cadence_qspi_apb_controller_disable(iobase);

	/* Convert to ns. */
	ref_clk_ns = (1000000000) / pdata->master_ref_clk_hz;

	/* Convert to ns. */
	sclk_ns = (1000000000) / sclk_hz;

	/* Plus 1 to round up 1 clock cycle. */
	tshsl = CQSPI_CAL_DELAY(f_pdata->tshsl_ns, ref_clk_ns, sclk_ns) + 1;
	tchsh = CQSPI_CAL_DELAY(f_pdata->tchsh_ns, ref_clk_ns, sclk_ns) + 1;
	tslch = CQSPI_CAL_DELAY(f_pdata->tslch_ns, ref_clk_ns, sclk_ns) + 1;
	tsd2d = CQSPI_CAL_DELAY(f_pdata->tsd2d_ns, ref_clk_ns, sclk_ns) + 1;

	reg = ((tshsl & CQSPI_REG_DELAY_TSHSL_MASK)
			<< CQSPI_REG_DELAY_TSHSL_LSB);
	reg |= ((tchsh & CQSPI_REG_DELAY_TCHSH_MASK)
			<< CQSPI_REG_DELAY_TCHSH_LSB);
	reg |= ((tslch & CQSPI_REG_DELAY_TSLCH_MASK)
			<< CQSPI_REG_DELAY_TSLCH_LSB);
	reg |= ((tsd2d & CQSPI_REG_DELAY_TSD2D_MASK)
			<< CQSPI_REG_DELAY_TSD2D_LSB);
	CQSPI_WRITEL(reg, iobase + CQSPI_REG_DELAY);

	cadence_qspi_apb_controller_enable(iobase);
	return;
}

void cadence_qspi_switch_cs(struct struct_cqspi *cadence_qspi,
	unsigned int cs)
{
	unsigned int reg;
	struct platform_device *pdev = cadence_qspi->pdev;
	struct cqspi_platform_data *pdata = pdev->dev.platform_data;
	struct cqspi_flash_pdata *f_pdata = &(pdata->f_pdata[cs]);
	void __iomem *iobase = cadence_qspi->iobase;

	cadence_qspi_apb_controller_disable(iobase);

	/* Configure page size and block size. */
	reg = CQSPI_READL(iobase + CQSPI_REG_SIZE);
	/* clear the previous value */
	reg &= ~(CQSPI_REG_SIZE_PAGE_MASK << CQSPI_REG_SIZE_PAGE_LSB);
	reg &= ~(CQSPI_REG_SIZE_BLOCK_MASK << CQSPI_REG_SIZE_BLOCK_LSB);
	reg |= (f_pdata->page_size << CQSPI_REG_SIZE_PAGE_LSB);
	reg |= (f_pdata->block_size << CQSPI_REG_SIZE_BLOCK_LSB);
	CQSPI_WRITEL(reg, iobase + CQSPI_REG_SIZE);

	/* configure the chip select */
	cadence_qspi_apb_chipselect (iobase, cs, pdata->ext_decoder);
	cadence_qspi_apb_controller_enable(iobase);
	return;
}

int cadence_qspi_apb_process_queue(struct struct_cqspi *cadence_qspi,
				  struct spi_device *spi, unsigned int n_trans,
				  struct spi_transfer **spi_xfer)
{
	struct platform_device *pdev = cadence_qspi->pdev;
	struct cqspi_platform_data *pdata = pdev->dev.platform_data;
	struct spi_transfer *cmd_xfer = spi_xfer[0];
	struct spi_transfer *data_xfer = (n_trans >= 2) ? spi_xfer[1] : NULL;
	void __iomem *iobase = cadence_qspi->iobase;
	unsigned int sclk;
	int ret = 0;

	if (!cmd_xfer->len) {
		pr_err("QSPI: SPI transfer length is 0.\n");
		return -EINVAL;
	}

	/* Setup baudrate divisor. */
	sclk = cmd_xfer->speed_hz ? cmd_xfer->speed_hz : spi->max_speed_hz;
	cadence_qspi_apb_config_baudrate_div(iobase,
		pdata->master_ref_clk_hz, sclk);

	/* Switch chip select. */
	if (cadence_qspi->current_cs != spi->chip_select) {
		cadence_qspi->current_cs = spi->chip_select;
		cadence_qspi_switch_cs(cadence_qspi, spi->chip_select);
	}

	/* Configure device delay if we change device clock. */
	cadence_qspi_apb_delay(cadence_qspi, pdata->master_ref_clk_hz, sclk);

	/*
	 * Use STIG command to send if the transfer length is less than
	 * 4 or if only one transfer.
	 */
	 if ((cmd_xfer->len < 4) || (n_trans == 1)) {
		 /* STIG command */
		 if (data_xfer && data_xfer->rx_buf) {
			 /* STIG read */
			 ret = cadence_qspi_apb_command_read(iobase,
				cmd_xfer->len, cmd_xfer->tx_buf,
				data_xfer->len, data_xfer->rx_buf);
		 } else {
			 /* STIG write */
			 ret = cadence_qspi_apb_command_write(iobase,
				 cmd_xfer->len, cmd_xfer->tx_buf);
		 }
	 } else if (cmd_xfer->len >= 4 && (n_trans == 2)){
		 /* Indirect operation */
		 if (data_xfer->rx_buf) {
			 /* Indirect read */
			 ret = cadence_qspi_apb_indirect_read_setup(iobase,
				 pdata->qspi_ahb_phy, cmd_xfer->len,
				 cmd_xfer->tx_buf, spi->addr_width);
			 ret = cadence_qspi_apb_indirect_read_execute(
				cadence_qspi, data_xfer->len,
				data_xfer->rx_buf);
		 } else {
			 /* Indirect write */
			 ret = cadence_qspi_apb_indirect_write_setup(
				 iobase, pdata->qspi_ahb_phy,
				 cmd_xfer->len, cmd_xfer->tx_buf);
			ret = cadence_qspi_apb_indirect_write_execute(
				cadence_qspi, data_xfer->len,
				data_xfer->tx_buf);
		 }
	 } else {
		pr_err("QSPI : Unknown SPI transfer.\n");
		return -EINVAL;
	 }
	return ret;
}

MODULE_LICENSE("Dual BSD/GPL");
