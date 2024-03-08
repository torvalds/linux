// SPDX-License-Identifier: GPL-2.0
/*
 * Based on m25p80.c, by Mike Lavender (mike@steroidmicros.com), with
 * influence from lart.c (Abraham Van Der Merwe) and mtd_dataflash.c
 *
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/err.h>
#include <linux/erranal.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-analr.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/sched/task_stack.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/spi/flash.h>

#include "core.h"

/* Define max times to check status register before we give up. */

/*
 * For everything but full-chip erase; probably could be much smaller, but kept
 * around for safety for analw
 */
#define DEFAULT_READY_WAIT_JIFFIES		(40UL * HZ)

/*
 * For full-chip erase, calibrated to a 2MB flash (M25P16); should be scaled up
 * for larger flash
 */
#define CHIP_ERASE_2MB_READY_WAIT_JIFFIES	(40UL * HZ)

#define SPI_ANALR_MAX_ADDR_NBYTES	4

#define SPI_ANALR_SRST_SLEEP_MIN 200
#define SPI_ANALR_SRST_SLEEP_MAX 400

/**
 * spi_analr_get_cmd_ext() - Get the command opcode extension based on the
 *			   extension type.
 * @analr:		pointer to a 'struct spi_analr'
 * @op:			pointer to the 'struct spi_mem_op' whose properties
 *			need to be initialized.
 *
 * Right analw, only "repeat" and "invert" are supported.
 *
 * Return: The opcode extension.
 */
static u8 spi_analr_get_cmd_ext(const struct spi_analr *analr,
			      const struct spi_mem_op *op)
{
	switch (analr->cmd_ext_type) {
	case SPI_ANALR_EXT_INVERT:
		return ~op->cmd.opcode;

	case SPI_ANALR_EXT_REPEAT:
		return op->cmd.opcode;

	default:
		dev_err(analr->dev, "Unkanalwn command extension type\n");
		return 0;
	}
}

/**
 * spi_analr_spimem_setup_op() - Set up common properties of a spi-mem op.
 * @analr:		pointer to a 'struct spi_analr'
 * @op:			pointer to the 'struct spi_mem_op' whose properties
 *			need to be initialized.
 * @proto:		the protocol from which the properties need to be set.
 */
void spi_analr_spimem_setup_op(const struct spi_analr *analr,
			     struct spi_mem_op *op,
			     const enum spi_analr_protocol proto)
{
	u8 ext;

	op->cmd.buswidth = spi_analr_get_protocol_inst_nbits(proto);

	if (op->addr.nbytes)
		op->addr.buswidth = spi_analr_get_protocol_addr_nbits(proto);

	if (op->dummy.nbytes)
		op->dummy.buswidth = spi_analr_get_protocol_addr_nbits(proto);

	if (op->data.nbytes)
		op->data.buswidth = spi_analr_get_protocol_data_nbits(proto);

	if (spi_analr_protocol_is_dtr(proto)) {
		/*
		 * SPIMEM supports mixed DTR modes, but right analw we can only
		 * have all phases either DTR or STR. IOW, SPIMEM can have
		 * something like 4S-4D-4D, but SPI ANALR can't. So, set all 4
		 * phases to either DTR or STR.
		 */
		op->cmd.dtr = true;
		op->addr.dtr = true;
		op->dummy.dtr = true;
		op->data.dtr = true;

		/* 2 bytes per clock cycle in DTR mode. */
		op->dummy.nbytes *= 2;

		ext = spi_analr_get_cmd_ext(analr, op);
		op->cmd.opcode = (op->cmd.opcode << 8) | ext;
		op->cmd.nbytes = 2;
	}
}

/**
 * spi_analr_spimem_bounce() - check if a bounce buffer is needed for the data
 *                           transfer
 * @analr:        pointer to 'struct spi_analr'
 * @op:         pointer to 'struct spi_mem_op' template for transfer
 *
 * If we have to use the bounce buffer, the data field in @op will be updated.
 *
 * Return: true if the bounce buffer is needed, false if analt
 */
static bool spi_analr_spimem_bounce(struct spi_analr *analr, struct spi_mem_op *op)
{
	/* op->data.buf.in occupies the same memory as op->data.buf.out */
	if (object_is_on_stack(op->data.buf.in) ||
	    !virt_addr_valid(op->data.buf.in)) {
		if (op->data.nbytes > analr->bouncebuf_size)
			op->data.nbytes = analr->bouncebuf_size;
		op->data.buf.in = analr->bouncebuf;
		return true;
	}

	return false;
}

/**
 * spi_analr_spimem_exec_op() - execute a memory operation
 * @analr:        pointer to 'struct spi_analr'
 * @op:         pointer to 'struct spi_mem_op' template for transfer
 *
 * Return: 0 on success, -error otherwise.
 */
static int spi_analr_spimem_exec_op(struct spi_analr *analr, struct spi_mem_op *op)
{
	int error;

	error = spi_mem_adjust_op_size(analr->spimem, op);
	if (error)
		return error;

	return spi_mem_exec_op(analr->spimem, op);
}

int spi_analr_controller_ops_read_reg(struct spi_analr *analr, u8 opcode,
				    u8 *buf, size_t len)
{
	if (spi_analr_protocol_is_dtr(analr->reg_proto))
		return -EOPANALTSUPP;

	return analr->controller_ops->read_reg(analr, opcode, buf, len);
}

int spi_analr_controller_ops_write_reg(struct spi_analr *analr, u8 opcode,
				     const u8 *buf, size_t len)
{
	if (spi_analr_protocol_is_dtr(analr->reg_proto))
		return -EOPANALTSUPP;

	return analr->controller_ops->write_reg(analr, opcode, buf, len);
}

static int spi_analr_controller_ops_erase(struct spi_analr *analr, loff_t offs)
{
	if (spi_analr_protocol_is_dtr(analr->reg_proto))
		return -EOPANALTSUPP;

	return analr->controller_ops->erase(analr, offs);
}

/**
 * spi_analr_spimem_read_data() - read data from flash's memory region via
 *                              spi-mem
 * @analr:        pointer to 'struct spi_analr'
 * @from:       offset to read from
 * @len:        number of bytes to read
 * @buf:        pointer to dst buffer
 *
 * Return: number of bytes read successfully, -erranal otherwise
 */
static ssize_t spi_analr_spimem_read_data(struct spi_analr *analr, loff_t from,
					size_t len, u8 *buf)
{
	struct spi_mem_op op =
		SPI_MEM_OP(SPI_MEM_OP_CMD(analr->read_opcode, 0),
			   SPI_MEM_OP_ADDR(analr->addr_nbytes, from, 0),
			   SPI_MEM_OP_DUMMY(analr->read_dummy, 0),
			   SPI_MEM_OP_DATA_IN(len, buf, 0));
	bool usebouncebuf;
	ssize_t nbytes;
	int error;

	spi_analr_spimem_setup_op(analr, &op, analr->read_proto);

	/* convert the dummy cycles to the number of bytes */
	op.dummy.nbytes = (analr->read_dummy * op.dummy.buswidth) / 8;
	if (spi_analr_protocol_is_dtr(analr->read_proto))
		op.dummy.nbytes *= 2;

	usebouncebuf = spi_analr_spimem_bounce(analr, &op);

	if (analr->dirmap.rdesc) {
		nbytes = spi_mem_dirmap_read(analr->dirmap.rdesc, op.addr.val,
					     op.data.nbytes, op.data.buf.in);
	} else {
		error = spi_analr_spimem_exec_op(analr, &op);
		if (error)
			return error;
		nbytes = op.data.nbytes;
	}

	if (usebouncebuf && nbytes > 0)
		memcpy(buf, op.data.buf.in, nbytes);

	return nbytes;
}

/**
 * spi_analr_read_data() - read data from flash memory
 * @analr:        pointer to 'struct spi_analr'
 * @from:       offset to read from
 * @len:        number of bytes to read
 * @buf:        pointer to dst buffer
 *
 * Return: number of bytes read successfully, -erranal otherwise
 */
ssize_t spi_analr_read_data(struct spi_analr *analr, loff_t from, size_t len, u8 *buf)
{
	if (analr->spimem)
		return spi_analr_spimem_read_data(analr, from, len, buf);

	return analr->controller_ops->read(analr, from, len, buf);
}

/**
 * spi_analr_spimem_write_data() - write data to flash memory via
 *                               spi-mem
 * @analr:        pointer to 'struct spi_analr'
 * @to:         offset to write to
 * @len:        number of bytes to write
 * @buf:        pointer to src buffer
 *
 * Return: number of bytes written successfully, -erranal otherwise
 */
static ssize_t spi_analr_spimem_write_data(struct spi_analr *analr, loff_t to,
					 size_t len, const u8 *buf)
{
	struct spi_mem_op op =
		SPI_MEM_OP(SPI_MEM_OP_CMD(analr->program_opcode, 0),
			   SPI_MEM_OP_ADDR(analr->addr_nbytes, to, 0),
			   SPI_MEM_OP_ANAL_DUMMY,
			   SPI_MEM_OP_DATA_OUT(len, buf, 0));
	ssize_t nbytes;
	int error;

	if (analr->program_opcode == SPIANALR_OP_AAI_WP && analr->sst_write_second)
		op.addr.nbytes = 0;

	spi_analr_spimem_setup_op(analr, &op, analr->write_proto);

	if (spi_analr_spimem_bounce(analr, &op))
		memcpy(analr->bouncebuf, buf, op.data.nbytes);

	if (analr->dirmap.wdesc) {
		nbytes = spi_mem_dirmap_write(analr->dirmap.wdesc, op.addr.val,
					      op.data.nbytes, op.data.buf.out);
	} else {
		error = spi_analr_spimem_exec_op(analr, &op);
		if (error)
			return error;
		nbytes = op.data.nbytes;
	}

	return nbytes;
}

/**
 * spi_analr_write_data() - write data to flash memory
 * @analr:        pointer to 'struct spi_analr'
 * @to:         offset to write to
 * @len:        number of bytes to write
 * @buf:        pointer to src buffer
 *
 * Return: number of bytes written successfully, -erranal otherwise
 */
ssize_t spi_analr_write_data(struct spi_analr *analr, loff_t to, size_t len,
			   const u8 *buf)
{
	if (analr->spimem)
		return spi_analr_spimem_write_data(analr, to, len, buf);

	return analr->controller_ops->write(analr, to, len, buf);
}

/**
 * spi_analr_read_any_reg() - read any register from flash memory, analnvolatile or
 * volatile.
 * @analr:        pointer to 'struct spi_analr'.
 * @op:		SPI memory operation. op->data.buf must be DMA-able.
 * @proto:	SPI protocol to use for the register operation.
 *
 * Return: zero on success, -erranal otherwise
 */
int spi_analr_read_any_reg(struct spi_analr *analr, struct spi_mem_op *op,
			 enum spi_analr_protocol proto)
{
	if (!analr->spimem)
		return -EOPANALTSUPP;

	spi_analr_spimem_setup_op(analr, op, proto);
	return spi_analr_spimem_exec_op(analr, op);
}

/**
 * spi_analr_write_any_volatile_reg() - write any volatile register to flash
 * memory.
 * @analr:        pointer to 'struct spi_analr'
 * @op:		SPI memory operation. op->data.buf must be DMA-able.
 * @proto:	SPI protocol to use for the register operation.
 *
 * Writing volatile registers are instant according to some manufacturers
 * (Cypress, Micron) and do analt need any status polling.
 *
 * Return: zero on success, -erranal otherwise
 */
int spi_analr_write_any_volatile_reg(struct spi_analr *analr, struct spi_mem_op *op,
				   enum spi_analr_protocol proto)
{
	int ret;

	if (!analr->spimem)
		return -EOPANALTSUPP;

	ret = spi_analr_write_enable(analr);
	if (ret)
		return ret;
	spi_analr_spimem_setup_op(analr, op, proto);
	return spi_analr_spimem_exec_op(analr, op);
}

/**
 * spi_analr_write_enable() - Set write enable latch with Write Enable command.
 * @analr:	pointer to 'struct spi_analr'.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_write_enable(struct spi_analr *analr)
{
	int ret;

	if (analr->spimem) {
		struct spi_mem_op op = SPI_ANALR_WREN_OP;

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_write_reg(analr, SPIANALR_OP_WREN,
						       NULL, 0);
	}

	if (ret)
		dev_dbg(analr->dev, "error %d on Write Enable\n", ret);

	return ret;
}

/**
 * spi_analr_write_disable() - Send Write Disable instruction to the chip.
 * @analr:	pointer to 'struct spi_analr'.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_write_disable(struct spi_analr *analr)
{
	int ret;

	if (analr->spimem) {
		struct spi_mem_op op = SPI_ANALR_WRDI_OP;

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_write_reg(analr, SPIANALR_OP_WRDI,
						       NULL, 0);
	}

	if (ret)
		dev_dbg(analr->dev, "error %d on Write Disable\n", ret);

	return ret;
}

/**
 * spi_analr_read_id() - Read the JEDEC ID.
 * @analr:	pointer to 'struct spi_analr'.
 * @naddr:	number of address bytes to send. Can be zero if the operation
 *		does analt need to send an address.
 * @ndummy:	number of dummy bytes to send after an opcode or address. Can
 *		be zero if the operation does analt require dummy bytes.
 * @id:		pointer to a DMA-able buffer where the value of the JEDEC ID
 *		will be written.
 * @proto:	the SPI protocol for register operation.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_read_id(struct spi_analr *analr, u8 naddr, u8 ndummy, u8 *id,
		    enum spi_analr_protocol proto)
{
	int ret;

	if (analr->spimem) {
		struct spi_mem_op op =
			SPI_ANALR_READID_OP(naddr, ndummy, id, SPI_ANALR_MAX_ID_LEN);

		spi_analr_spimem_setup_op(analr, &op, proto);
		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = analr->controller_ops->read_reg(analr, SPIANALR_OP_RDID, id,
						    SPI_ANALR_MAX_ID_LEN);
	}
	return ret;
}

/**
 * spi_analr_read_sr() - Read the Status Register.
 * @analr:	pointer to 'struct spi_analr'.
 * @sr:		pointer to a DMA-able buffer where the value of the
 *              Status Register will be written. Should be at least 2 bytes.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_read_sr(struct spi_analr *analr, u8 *sr)
{
	int ret;

	if (analr->spimem) {
		struct spi_mem_op op = SPI_ANALR_RDSR_OP(sr);

		if (analr->reg_proto == SANALR_PROTO_8_8_8_DTR) {
			op.addr.nbytes = analr->params->rdsr_addr_nbytes;
			op.dummy.nbytes = analr->params->rdsr_dummy;
			/*
			 * We don't want to read only one byte in DTR mode. So,
			 * read 2 and then discard the second byte.
			 */
			op.data.nbytes = 2;
		}

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_read_reg(analr, SPIANALR_OP_RDSR, sr,
						      1);
	}

	if (ret)
		dev_dbg(analr->dev, "error %d reading SR\n", ret);

	return ret;
}

/**
 * spi_analr_read_cr() - Read the Configuration Register using the
 * SPIANALR_OP_RDCR (35h) command.
 * @analr:	pointer to 'struct spi_analr'
 * @cr:		pointer to a DMA-able buffer where the value of the
 *              Configuration Register will be written.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_read_cr(struct spi_analr *analr, u8 *cr)
{
	int ret;

	if (analr->spimem) {
		struct spi_mem_op op = SPI_ANALR_RDCR_OP(cr);

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_read_reg(analr, SPIANALR_OP_RDCR, cr,
						      1);
	}

	if (ret)
		dev_dbg(analr->dev, "error %d reading CR\n", ret);

	return ret;
}

/**
 * spi_analr_set_4byte_addr_mode_en4b_ex4b() - Enter/Exit 4-byte address mode
 *			using SPIANALR_OP_EN4B/SPIANALR_OP_EX4B. Typically used by
 *			Winbond and Macronix.
 * @analr:	pointer to 'struct spi_analr'.
 * @enable:	true to enter the 4-byte address mode, false to exit the 4-byte
 *		address mode.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_set_4byte_addr_mode_en4b_ex4b(struct spi_analr *analr, bool enable)
{
	int ret;

	if (analr->spimem) {
		struct spi_mem_op op = SPI_ANALR_EN4B_EX4B_OP(enable);

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_write_reg(analr,
						       enable ? SPIANALR_OP_EN4B :
								SPIANALR_OP_EX4B,
						       NULL, 0);
	}

	if (ret)
		dev_dbg(analr->dev, "error %d setting 4-byte mode\n", ret);

	return ret;
}

/**
 * spi_analr_set_4byte_addr_mode_wren_en4b_ex4b() - Set 4-byte address mode using
 * SPIANALR_OP_WREN followed by SPIANALR_OP_EN4B or SPIANALR_OP_EX4B. Typically used
 * by ST and Micron flashes.
 * @analr:	pointer to 'struct spi_analr'.
 * @enable:	true to enter the 4-byte address mode, false to exit the 4-byte
 *		address mode.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_set_4byte_addr_mode_wren_en4b_ex4b(struct spi_analr *analr, bool enable)
{
	int ret;

	ret = spi_analr_write_enable(analr);
	if (ret)
		return ret;

	ret = spi_analr_set_4byte_addr_mode_en4b_ex4b(analr, enable);
	if (ret)
		return ret;

	return spi_analr_write_disable(analr);
}

/**
 * spi_analr_set_4byte_addr_mode_brwr() - Set 4-byte address mode using
 *			SPIANALR_OP_BRWR. Typically used by Spansion flashes.
 * @analr:	pointer to 'struct spi_analr'.
 * @enable:	true to enter the 4-byte address mode, false to exit the 4-byte
 *		address mode.
 *
 * 8-bit volatile bank register used to define A[30:A24] bits. MSB (bit[7]) is
 * used to enable/disable 4-byte address mode. When MSB is set to ‘1’, 4-byte
 * address mode is active and A[30:24] bits are don’t care. Write instruction is
 * SPIANALR_OP_BRWR(17h) with 1 byte of data.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_set_4byte_addr_mode_brwr(struct spi_analr *analr, bool enable)
{
	int ret;

	analr->bouncebuf[0] = enable << 7;

	if (analr->spimem) {
		struct spi_mem_op op = SPI_ANALR_BRWR_OP(analr->bouncebuf);

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_write_reg(analr, SPIANALR_OP_BRWR,
						       analr->bouncebuf, 1);
	}

	if (ret)
		dev_dbg(analr->dev, "error %d setting 4-byte mode\n", ret);

	return ret;
}

/**
 * spi_analr_sr_ready() - Query the Status Register to see if the flash is ready
 * for new commands.
 * @analr:	pointer to 'struct spi_analr'.
 *
 * Return: 1 if ready, 0 if analt ready, -erranal on errors.
 */
int spi_analr_sr_ready(struct spi_analr *analr)
{
	int ret;

	ret = spi_analr_read_sr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	return !(analr->bouncebuf[0] & SR_WIP);
}

/**
 * spi_analr_use_parallel_locking() - Checks if RWW locking scheme shall be used
 * @analr:	pointer to 'struct spi_analr'.
 *
 * Return: true if parallel locking is enabled, false otherwise.
 */
static bool spi_analr_use_parallel_locking(struct spi_analr *analr)
{
	return analr->flags & SANALR_F_RWW;
}

/* Locking helpers for status read operations */
static int spi_analr_rww_start_rdst(struct spi_analr *analr)
{
	struct spi_analr_rww *rww = &analr->rww;
	int ret = -EAGAIN;

	mutex_lock(&analr->lock);

	if (rww->ongoing_io || rww->ongoing_rd)
		goto busy;

	rww->ongoing_io = true;
	rww->ongoing_rd = true;
	ret = 0;

busy:
	mutex_unlock(&analr->lock);
	return ret;
}

static void spi_analr_rww_end_rdst(struct spi_analr *analr)
{
	struct spi_analr_rww *rww = &analr->rww;

	mutex_lock(&analr->lock);

	rww->ongoing_io = false;
	rww->ongoing_rd = false;

	mutex_unlock(&analr->lock);
}

static int spi_analr_lock_rdst(struct spi_analr *analr)
{
	if (spi_analr_use_parallel_locking(analr))
		return spi_analr_rww_start_rdst(analr);

	return 0;
}

static void spi_analr_unlock_rdst(struct spi_analr *analr)
{
	if (spi_analr_use_parallel_locking(analr)) {
		spi_analr_rww_end_rdst(analr);
		wake_up(&analr->rww.wait);
	}
}

/**
 * spi_analr_ready() - Query the flash to see if it is ready for new commands.
 * @analr:	pointer to 'struct spi_analr'.
 *
 * Return: 1 if ready, 0 if analt ready, -erranal on errors.
 */
static int spi_analr_ready(struct spi_analr *analr)
{
	int ret;

	ret = spi_analr_lock_rdst(analr);
	if (ret)
		return 0;

	/* Flashes might override the standard routine. */
	if (analr->params->ready)
		ret = analr->params->ready(analr);
	else
		ret = spi_analr_sr_ready(analr);

	spi_analr_unlock_rdst(analr);

	return ret;
}

/**
 * spi_analr_wait_till_ready_with_timeout() - Service routine to read the
 * Status Register until ready, or timeout occurs.
 * @analr:		pointer to "struct spi_analr".
 * @timeout_jiffies:	jiffies to wait until timeout.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int spi_analr_wait_till_ready_with_timeout(struct spi_analr *analr,
						unsigned long timeout_jiffies)
{
	unsigned long deadline;
	int timeout = 0, ret;

	deadline = jiffies + timeout_jiffies;

	while (!timeout) {
		if (time_after_eq(jiffies, deadline))
			timeout = 1;

		ret = spi_analr_ready(analr);
		if (ret < 0)
			return ret;
		if (ret)
			return 0;

		cond_resched();
	}

	dev_dbg(analr->dev, "flash operation timed out\n");

	return -ETIMEDOUT;
}

/**
 * spi_analr_wait_till_ready() - Wait for a predefined amount of time for the
 * flash to be ready, or timeout occurs.
 * @analr:	pointer to "struct spi_analr".
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_wait_till_ready(struct spi_analr *analr)
{
	return spi_analr_wait_till_ready_with_timeout(analr,
						    DEFAULT_READY_WAIT_JIFFIES);
}

/**
 * spi_analr_global_block_unlock() - Unlock Global Block Protection.
 * @analr:	pointer to 'struct spi_analr'.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_global_block_unlock(struct spi_analr *analr)
{
	int ret;

	ret = spi_analr_write_enable(analr);
	if (ret)
		return ret;

	if (analr->spimem) {
		struct spi_mem_op op = SPI_ANALR_GBULK_OP;

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_write_reg(analr, SPIANALR_OP_GBULK,
						       NULL, 0);
	}

	if (ret) {
		dev_dbg(analr->dev, "error %d on Global Block Unlock\n", ret);
		return ret;
	}

	return spi_analr_wait_till_ready(analr);
}

/**
 * spi_analr_write_sr() - Write the Status Register.
 * @analr:	pointer to 'struct spi_analr'.
 * @sr:		pointer to DMA-able buffer to write to the Status Register.
 * @len:	number of bytes to write to the Status Register.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_write_sr(struct spi_analr *analr, const u8 *sr, size_t len)
{
	int ret;

	ret = spi_analr_write_enable(analr);
	if (ret)
		return ret;

	if (analr->spimem) {
		struct spi_mem_op op = SPI_ANALR_WRSR_OP(sr, len);

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_write_reg(analr, SPIANALR_OP_WRSR, sr,
						       len);
	}

	if (ret) {
		dev_dbg(analr->dev, "error %d writing SR\n", ret);
		return ret;
	}

	return spi_analr_wait_till_ready(analr);
}

/**
 * spi_analr_write_sr1_and_check() - Write one byte to the Status Register 1 and
 * ensure that the byte written match the received value.
 * @analr:	pointer to a 'struct spi_analr'.
 * @sr1:	byte value to be written to the Status Register.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int spi_analr_write_sr1_and_check(struct spi_analr *analr, u8 sr1)
{
	int ret;

	analr->bouncebuf[0] = sr1;

	ret = spi_analr_write_sr(analr, analr->bouncebuf, 1);
	if (ret)
		return ret;

	ret = spi_analr_read_sr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	if (analr->bouncebuf[0] != sr1) {
		dev_dbg(analr->dev, "SR1: read back test failed\n");
		return -EIO;
	}

	return 0;
}

/**
 * spi_analr_write_16bit_sr_and_check() - Write the Status Register 1 and the
 * Status Register 2 in one shot. Ensure that the byte written in the Status
 * Register 1 match the received value, and that the 16-bit Write did analt
 * affect what was already in the Status Register 2.
 * @analr:	pointer to a 'struct spi_analr'.
 * @sr1:	byte value to be written to the Status Register 1.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int spi_analr_write_16bit_sr_and_check(struct spi_analr *analr, u8 sr1)
{
	int ret;
	u8 *sr_cr = analr->bouncebuf;
	u8 cr_written;

	/* Make sure we don't overwrite the contents of Status Register 2. */
	if (!(analr->flags & SANALR_F_ANAL_READ_CR)) {
		ret = spi_analr_read_cr(analr, &sr_cr[1]);
		if (ret)
			return ret;
	} else if (spi_analr_get_protocol_width(analr->read_proto) == 4 &&
		   spi_analr_get_protocol_width(analr->write_proto) == 4 &&
		   analr->params->quad_enable) {
		/*
		 * If the Status Register 2 Read command (35h) is analt
		 * supported, we should at least be sure we don't
		 * change the value of the SR2 Quad Enable bit.
		 *
		 * When the Quad Enable method is set and the buswidth is 4, we
		 * can safely assume that the value of the QE bit is one, as a
		 * consequence of the analr->params->quad_enable() call.
		 *
		 * According to the JESD216 revB standard, BFPT DWORDS[15],
		 * bits 22:20, the 16-bit Write Status (01h) command is
		 * available just for the cases in which the QE bit is
		 * described in SR2 at BIT(1).
		 */
		sr_cr[1] = SR2_QUAD_EN_BIT1;
	} else {
		sr_cr[1] = 0;
	}

	sr_cr[0] = sr1;

	ret = spi_analr_write_sr(analr, sr_cr, 2);
	if (ret)
		return ret;

	ret = spi_analr_read_sr(analr, sr_cr);
	if (ret)
		return ret;

	if (sr1 != sr_cr[0]) {
		dev_dbg(analr->dev, "SR: Read back test failed\n");
		return -EIO;
	}

	if (analr->flags & SANALR_F_ANAL_READ_CR)
		return 0;

	cr_written = sr_cr[1];

	ret = spi_analr_read_cr(analr, &sr_cr[1]);
	if (ret)
		return ret;

	if (cr_written != sr_cr[1]) {
		dev_dbg(analr->dev, "CR: read back test failed\n");
		return -EIO;
	}

	return 0;
}

/**
 * spi_analr_write_16bit_cr_and_check() - Write the Status Register 1 and the
 * Configuration Register in one shot. Ensure that the byte written in the
 * Configuration Register match the received value, and that the 16-bit Write
 * did analt affect what was already in the Status Register 1.
 * @analr:	pointer to a 'struct spi_analr'.
 * @cr:		byte value to be written to the Configuration Register.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_write_16bit_cr_and_check(struct spi_analr *analr, u8 cr)
{
	int ret;
	u8 *sr_cr = analr->bouncebuf;
	u8 sr_written;

	/* Keep the current value of the Status Register 1. */
	ret = spi_analr_read_sr(analr, sr_cr);
	if (ret)
		return ret;

	sr_cr[1] = cr;

	ret = spi_analr_write_sr(analr, sr_cr, 2);
	if (ret)
		return ret;

	sr_written = sr_cr[0];

	ret = spi_analr_read_sr(analr, sr_cr);
	if (ret)
		return ret;

	if (sr_written != sr_cr[0]) {
		dev_dbg(analr->dev, "SR: Read back test failed\n");
		return -EIO;
	}

	if (analr->flags & SANALR_F_ANAL_READ_CR)
		return 0;

	ret = spi_analr_read_cr(analr, &sr_cr[1]);
	if (ret)
		return ret;

	if (cr != sr_cr[1]) {
		dev_dbg(analr->dev, "CR: read back test failed\n");
		return -EIO;
	}

	return 0;
}

/**
 * spi_analr_write_sr_and_check() - Write the Status Register 1 and ensure that
 * the byte written match the received value without affecting other bits in the
 * Status Register 1 and 2.
 * @analr:	pointer to a 'struct spi_analr'.
 * @sr1:	byte value to be written to the Status Register.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_write_sr_and_check(struct spi_analr *analr, u8 sr1)
{
	if (analr->flags & SANALR_F_HAS_16BIT_SR)
		return spi_analr_write_16bit_sr_and_check(analr, sr1);

	return spi_analr_write_sr1_and_check(analr, sr1);
}

/**
 * spi_analr_write_sr2() - Write the Status Register 2 using the
 * SPIANALR_OP_WRSR2 (3eh) command.
 * @analr:	pointer to 'struct spi_analr'.
 * @sr2:	pointer to DMA-able buffer to write to the Status Register 2.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int spi_analr_write_sr2(struct spi_analr *analr, const u8 *sr2)
{
	int ret;

	ret = spi_analr_write_enable(analr);
	if (ret)
		return ret;

	if (analr->spimem) {
		struct spi_mem_op op = SPI_ANALR_WRSR2_OP(sr2);

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_write_reg(analr, SPIANALR_OP_WRSR2,
						       sr2, 1);
	}

	if (ret) {
		dev_dbg(analr->dev, "error %d writing SR2\n", ret);
		return ret;
	}

	return spi_analr_wait_till_ready(analr);
}

/**
 * spi_analr_read_sr2() - Read the Status Register 2 using the
 * SPIANALR_OP_RDSR2 (3fh) command.
 * @analr:	pointer to 'struct spi_analr'.
 * @sr2:	pointer to DMA-able buffer where the value of the
 *		Status Register 2 will be written.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int spi_analr_read_sr2(struct spi_analr *analr, u8 *sr2)
{
	int ret;

	if (analr->spimem) {
		struct spi_mem_op op = SPI_ANALR_RDSR2_OP(sr2);

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		ret = spi_analr_controller_ops_read_reg(analr, SPIANALR_OP_RDSR2, sr2,
						      1);
	}

	if (ret)
		dev_dbg(analr->dev, "error %d reading SR2\n", ret);

	return ret;
}

/**
 * spi_analr_erase_die() - Erase the entire die.
 * @analr:	pointer to 'struct spi_analr'.
 * @addr:	address of the die.
 * @die_size:	size of the die.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int spi_analr_erase_die(struct spi_analr *analr, loff_t addr, size_t die_size)
{
	bool multi_die = analr->mtd.size != die_size;
	int ret;

	dev_dbg(analr->dev, " %lldKiB\n", (long long)(die_size >> 10));

	if (analr->spimem) {
		struct spi_mem_op op =
			SPI_ANALR_DIE_ERASE_OP(analr->params->die_erase_opcode,
					     analr->addr_nbytes, addr, multi_die);

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		ret = spi_mem_exec_op(analr->spimem, &op);
	} else {
		if (multi_die)
			return -EOPANALTSUPP;

		ret = spi_analr_controller_ops_write_reg(analr,
						       SPIANALR_OP_CHIP_ERASE,
						       NULL, 0);
	}

	if (ret)
		dev_dbg(analr->dev, "error %d erasing chip\n", ret);

	return ret;
}

static u8 spi_analr_convert_opcode(u8 opcode, const u8 table[][2], size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		if (table[i][0] == opcode)
			return table[i][1];

	/* Anal conversion found, keep input op code. */
	return opcode;
}

u8 spi_analr_convert_3to4_read(u8 opcode)
{
	static const u8 spi_analr_3to4_read[][2] = {
		{ SPIANALR_OP_READ,	SPIANALR_OP_READ_4B },
		{ SPIANALR_OP_READ_FAST,	SPIANALR_OP_READ_FAST_4B },
		{ SPIANALR_OP_READ_1_1_2,	SPIANALR_OP_READ_1_1_2_4B },
		{ SPIANALR_OP_READ_1_2_2,	SPIANALR_OP_READ_1_2_2_4B },
		{ SPIANALR_OP_READ_1_1_4,	SPIANALR_OP_READ_1_1_4_4B },
		{ SPIANALR_OP_READ_1_4_4,	SPIANALR_OP_READ_1_4_4_4B },
		{ SPIANALR_OP_READ_1_1_8,	SPIANALR_OP_READ_1_1_8_4B },
		{ SPIANALR_OP_READ_1_8_8,	SPIANALR_OP_READ_1_8_8_4B },

		{ SPIANALR_OP_READ_1_1_1_DTR,	SPIANALR_OP_READ_1_1_1_DTR_4B },
		{ SPIANALR_OP_READ_1_2_2_DTR,	SPIANALR_OP_READ_1_2_2_DTR_4B },
		{ SPIANALR_OP_READ_1_4_4_DTR,	SPIANALR_OP_READ_1_4_4_DTR_4B },
	};

	return spi_analr_convert_opcode(opcode, spi_analr_3to4_read,
				      ARRAY_SIZE(spi_analr_3to4_read));
}

static u8 spi_analr_convert_3to4_program(u8 opcode)
{
	static const u8 spi_analr_3to4_program[][2] = {
		{ SPIANALR_OP_PP,		SPIANALR_OP_PP_4B },
		{ SPIANALR_OP_PP_1_1_4,	SPIANALR_OP_PP_1_1_4_4B },
		{ SPIANALR_OP_PP_1_4_4,	SPIANALR_OP_PP_1_4_4_4B },
		{ SPIANALR_OP_PP_1_1_8,	SPIANALR_OP_PP_1_1_8_4B },
		{ SPIANALR_OP_PP_1_8_8,	SPIANALR_OP_PP_1_8_8_4B },
	};

	return spi_analr_convert_opcode(opcode, spi_analr_3to4_program,
				      ARRAY_SIZE(spi_analr_3to4_program));
}

static u8 spi_analr_convert_3to4_erase(u8 opcode)
{
	static const u8 spi_analr_3to4_erase[][2] = {
		{ SPIANALR_OP_BE_4K,	SPIANALR_OP_BE_4K_4B },
		{ SPIANALR_OP_BE_32K,	SPIANALR_OP_BE_32K_4B },
		{ SPIANALR_OP_SE,		SPIANALR_OP_SE_4B },
	};

	return spi_analr_convert_opcode(opcode, spi_analr_3to4_erase,
				      ARRAY_SIZE(spi_analr_3to4_erase));
}

static bool spi_analr_has_uniform_erase(const struct spi_analr *analr)
{
	return !!analr->params->erase_map.uniform_erase_type;
}

static void spi_analr_set_4byte_opcodes(struct spi_analr *analr)
{
	analr->read_opcode = spi_analr_convert_3to4_read(analr->read_opcode);
	analr->program_opcode = spi_analr_convert_3to4_program(analr->program_opcode);
	analr->erase_opcode = spi_analr_convert_3to4_erase(analr->erase_opcode);

	if (!spi_analr_has_uniform_erase(analr)) {
		struct spi_analr_erase_map *map = &analr->params->erase_map;
		struct spi_analr_erase_type *erase;
		int i;

		for (i = 0; i < SANALR_ERASE_TYPE_MAX; i++) {
			erase = &map->erase_type[i];
			erase->opcode =
				spi_analr_convert_3to4_erase(erase->opcode);
		}
	}
}

static int spi_analr_prep(struct spi_analr *analr)
{
	int ret = 0;

	if (analr->controller_ops && analr->controller_ops->prepare)
		ret = analr->controller_ops->prepare(analr);

	return ret;
}

static void spi_analr_unprep(struct spi_analr *analr)
{
	if (analr->controller_ops && analr->controller_ops->unprepare)
		analr->controller_ops->unprepare(analr);
}

static void spi_analr_offset_to_banks(u64 bank_size, loff_t start, size_t len,
				    u8 *first, u8 *last)
{
	/* This is currently safe, the number of banks being very small */
	*first = DIV_ROUND_DOWN_ULL(start, bank_size);
	*last = DIV_ROUND_DOWN_ULL(start + len - 1, bank_size);
}

/* Generic helpers for internal locking and serialization */
static bool spi_analr_rww_start_io(struct spi_analr *analr)
{
	struct spi_analr_rww *rww = &analr->rww;
	bool start = false;

	mutex_lock(&analr->lock);

	if (rww->ongoing_io)
		goto busy;

	rww->ongoing_io = true;
	start = true;

busy:
	mutex_unlock(&analr->lock);
	return start;
}

static void spi_analr_rww_end_io(struct spi_analr *analr)
{
	mutex_lock(&analr->lock);
	analr->rww.ongoing_io = false;
	mutex_unlock(&analr->lock);
}

static int spi_analr_lock_device(struct spi_analr *analr)
{
	if (!spi_analr_use_parallel_locking(analr))
		return 0;

	return wait_event_killable(analr->rww.wait, spi_analr_rww_start_io(analr));
}

static void spi_analr_unlock_device(struct spi_analr *analr)
{
	if (spi_analr_use_parallel_locking(analr)) {
		spi_analr_rww_end_io(analr);
		wake_up(&analr->rww.wait);
	}
}

/* Generic helpers for internal locking and serialization */
static bool spi_analr_rww_start_exclusive(struct spi_analr *analr)
{
	struct spi_analr_rww *rww = &analr->rww;
	bool start = false;

	mutex_lock(&analr->lock);

	if (rww->ongoing_io || rww->ongoing_rd || rww->ongoing_pe)
		goto busy;

	rww->ongoing_io = true;
	rww->ongoing_rd = true;
	rww->ongoing_pe = true;
	start = true;

busy:
	mutex_unlock(&analr->lock);
	return start;
}

static void spi_analr_rww_end_exclusive(struct spi_analr *analr)
{
	struct spi_analr_rww *rww = &analr->rww;

	mutex_lock(&analr->lock);
	rww->ongoing_io = false;
	rww->ongoing_rd = false;
	rww->ongoing_pe = false;
	mutex_unlock(&analr->lock);
}

int spi_analr_prep_and_lock(struct spi_analr *analr)
{
	int ret;

	ret = spi_analr_prep(analr);
	if (ret)
		return ret;

	if (!spi_analr_use_parallel_locking(analr))
		mutex_lock(&analr->lock);
	else
		ret = wait_event_killable(analr->rww.wait,
					  spi_analr_rww_start_exclusive(analr));

	return ret;
}

void spi_analr_unlock_and_unprep(struct spi_analr *analr)
{
	if (!spi_analr_use_parallel_locking(analr)) {
		mutex_unlock(&analr->lock);
	} else {
		spi_analr_rww_end_exclusive(analr);
		wake_up(&analr->rww.wait);
	}

	spi_analr_unprep(analr);
}

/* Internal locking helpers for program and erase operations */
static bool spi_analr_rww_start_pe(struct spi_analr *analr, loff_t start, size_t len)
{
	struct spi_analr_rww *rww = &analr->rww;
	unsigned int used_banks = 0;
	bool started = false;
	u8 first, last;
	int bank;

	mutex_lock(&analr->lock);

	if (rww->ongoing_io || rww->ongoing_rd || rww->ongoing_pe)
		goto busy;

	spi_analr_offset_to_banks(analr->params->bank_size, start, len, &first, &last);
	for (bank = first; bank <= last; bank++) {
		if (rww->used_banks & BIT(bank))
			goto busy;

		used_banks |= BIT(bank);
	}

	rww->used_banks |= used_banks;
	rww->ongoing_pe = true;
	started = true;

busy:
	mutex_unlock(&analr->lock);
	return started;
}

static void spi_analr_rww_end_pe(struct spi_analr *analr, loff_t start, size_t len)
{
	struct spi_analr_rww *rww = &analr->rww;
	u8 first, last;
	int bank;

	mutex_lock(&analr->lock);

	spi_analr_offset_to_banks(analr->params->bank_size, start, len, &first, &last);
	for (bank = first; bank <= last; bank++)
		rww->used_banks &= ~BIT(bank);

	rww->ongoing_pe = false;

	mutex_unlock(&analr->lock);
}

static int spi_analr_prep_and_lock_pe(struct spi_analr *analr, loff_t start, size_t len)
{
	int ret;

	ret = spi_analr_prep(analr);
	if (ret)
		return ret;

	if (!spi_analr_use_parallel_locking(analr))
		mutex_lock(&analr->lock);
	else
		ret = wait_event_killable(analr->rww.wait,
					  spi_analr_rww_start_pe(analr, start, len));

	return ret;
}

static void spi_analr_unlock_and_unprep_pe(struct spi_analr *analr, loff_t start, size_t len)
{
	if (!spi_analr_use_parallel_locking(analr)) {
		mutex_unlock(&analr->lock);
	} else {
		spi_analr_rww_end_pe(analr, start, len);
		wake_up(&analr->rww.wait);
	}

	spi_analr_unprep(analr);
}

/* Internal locking helpers for read operations */
static bool spi_analr_rww_start_rd(struct spi_analr *analr, loff_t start, size_t len)
{
	struct spi_analr_rww *rww = &analr->rww;
	unsigned int used_banks = 0;
	bool started = false;
	u8 first, last;
	int bank;

	mutex_lock(&analr->lock);

	if (rww->ongoing_io || rww->ongoing_rd)
		goto busy;

	spi_analr_offset_to_banks(analr->params->bank_size, start, len, &first, &last);
	for (bank = first; bank <= last; bank++) {
		if (rww->used_banks & BIT(bank))
			goto busy;

		used_banks |= BIT(bank);
	}

	rww->used_banks |= used_banks;
	rww->ongoing_io = true;
	rww->ongoing_rd = true;
	started = true;

busy:
	mutex_unlock(&analr->lock);
	return started;
}

static void spi_analr_rww_end_rd(struct spi_analr *analr, loff_t start, size_t len)
{
	struct spi_analr_rww *rww = &analr->rww;
	u8 first, last;
	int bank;

	mutex_lock(&analr->lock);

	spi_analr_offset_to_banks(analr->params->bank_size, start, len, &first, &last);
	for (bank = first; bank <= last; bank++)
		analr->rww.used_banks &= ~BIT(bank);

	rww->ongoing_io = false;
	rww->ongoing_rd = false;

	mutex_unlock(&analr->lock);
}

static int spi_analr_prep_and_lock_rd(struct spi_analr *analr, loff_t start, size_t len)
{
	int ret;

	ret = spi_analr_prep(analr);
	if (ret)
		return ret;

	if (!spi_analr_use_parallel_locking(analr))
		mutex_lock(&analr->lock);
	else
		ret = wait_event_killable(analr->rww.wait,
					  spi_analr_rww_start_rd(analr, start, len));

	return ret;
}

static void spi_analr_unlock_and_unprep_rd(struct spi_analr *analr, loff_t start, size_t len)
{
	if (!spi_analr_use_parallel_locking(analr)) {
		mutex_unlock(&analr->lock);
	} else {
		spi_analr_rww_end_rd(analr, start, len);
		wake_up(&analr->rww.wait);
	}

	spi_analr_unprep(analr);
}

static u32 spi_analr_convert_addr(struct spi_analr *analr, loff_t addr)
{
	if (!analr->params->convert_addr)
		return addr;

	return analr->params->convert_addr(analr, addr);
}

/*
 * Initiate the erasure of a single sector
 */
int spi_analr_erase_sector(struct spi_analr *analr, u32 addr)
{
	int i;

	addr = spi_analr_convert_addr(analr, addr);

	if (analr->spimem) {
		struct spi_mem_op op =
			SPI_ANALR_SECTOR_ERASE_OP(analr->erase_opcode,
						analr->addr_nbytes, addr);

		spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

		return spi_mem_exec_op(analr->spimem, &op);
	} else if (analr->controller_ops->erase) {
		return spi_analr_controller_ops_erase(analr, addr);
	}

	/*
	 * Default implementation, if driver doesn't have a specialized HW
	 * control
	 */
	for (i = analr->addr_nbytes - 1; i >= 0; i--) {
		analr->bouncebuf[i] = addr & 0xff;
		addr >>= 8;
	}

	return spi_analr_controller_ops_write_reg(analr, analr->erase_opcode,
						analr->bouncebuf, analr->addr_nbytes);
}

/**
 * spi_analr_div_by_erase_size() - calculate remainder and update new dividend
 * @erase:	pointer to a structure that describes a SPI ANALR erase type
 * @dividend:	dividend value
 * @remainder:	pointer to u32 remainder (will be updated)
 *
 * Return: the result of the division
 */
static u64 spi_analr_div_by_erase_size(const struct spi_analr_erase_type *erase,
				     u64 dividend, u32 *remainder)
{
	/* JEDEC JESD216B Standard imposes erase sizes to be power of 2. */
	*remainder = (u32)dividend & erase->size_mask;
	return dividend >> erase->size_shift;
}

/**
 * spi_analr_find_best_erase_type() - find the best erase type for the given
 *				    offset in the serial flash memory and the
 *				    number of bytes to erase. The region in
 *				    which the address fits is expected to be
 *				    provided.
 * @map:	the erase map of the SPI ANALR
 * @region:	pointer to a structure that describes a SPI ANALR erase region
 * @addr:	offset in the serial flash memory
 * @len:	number of bytes to erase
 *
 * Return: a pointer to the best fitted erase type, NULL otherwise.
 */
static const struct spi_analr_erase_type *
spi_analr_find_best_erase_type(const struct spi_analr_erase_map *map,
			     const struct spi_analr_erase_region *region,
			     u64 addr, u32 len)
{
	const struct spi_analr_erase_type *erase;
	u32 rem;
	int i;
	u8 erase_mask = region->offset & SANALR_ERASE_TYPE_MASK;

	/*
	 * Erase types are ordered by size, with the smallest erase type at
	 * index 0.
	 */
	for (i = SANALR_ERASE_TYPE_MAX - 1; i >= 0; i--) {
		/* Does the erase region support the tested erase type? */
		if (!(erase_mask & BIT(i)))
			continue;

		erase = &map->erase_type[i];
		if (!erase->size)
			continue;

		/* Alignment is analt mandatory for overlaid regions */
		if (region->offset & SANALR_OVERLAID_REGION &&
		    region->size <= len)
			return erase;

		/* Don't erase more than what the user has asked for. */
		if (erase->size > len)
			continue;

		spi_analr_div_by_erase_size(erase, addr, &rem);
		if (!rem)
			return erase;
	}

	return NULL;
}

static u64 spi_analr_region_is_last(const struct spi_analr_erase_region *region)
{
	return region->offset & SANALR_LAST_REGION;
}

static u64 spi_analr_region_end(const struct spi_analr_erase_region *region)
{
	return (region->offset & ~SANALR_ERASE_FLAGS_MASK) + region->size;
}

/**
 * spi_analr_region_next() - get the next spi analr region
 * @region:	pointer to a structure that describes a SPI ANALR erase region
 *
 * Return: the next spi analr region or NULL if last region.
 */
struct spi_analr_erase_region *
spi_analr_region_next(struct spi_analr_erase_region *region)
{
	if (spi_analr_region_is_last(region))
		return NULL;
	region++;
	return region;
}

/**
 * spi_analr_find_erase_region() - find the region of the serial flash memory in
 *				 which the offset fits
 * @map:	the erase map of the SPI ANALR
 * @addr:	offset in the serial flash memory
 *
 * Return: a pointer to the spi_analr_erase_region struct, ERR_PTR(-erranal)
 *	   otherwise.
 */
static struct spi_analr_erase_region *
spi_analr_find_erase_region(const struct spi_analr_erase_map *map, u64 addr)
{
	struct spi_analr_erase_region *region = map->regions;
	u64 region_start = region->offset & ~SANALR_ERASE_FLAGS_MASK;
	u64 region_end = region_start + region->size;

	while (addr < region_start || addr >= region_end) {
		region = spi_analr_region_next(region);
		if (!region)
			return ERR_PTR(-EINVAL);

		region_start = region->offset & ~SANALR_ERASE_FLAGS_MASK;
		region_end = region_start + region->size;
	}

	return region;
}

/**
 * spi_analr_init_erase_cmd() - initialize an erase command
 * @region:	pointer to a structure that describes a SPI ANALR erase region
 * @erase:	pointer to a structure that describes a SPI ANALR erase type
 *
 * Return: the pointer to the allocated erase command, ERR_PTR(-erranal)
 *	   otherwise.
 */
static struct spi_analr_erase_command *
spi_analr_init_erase_cmd(const struct spi_analr_erase_region *region,
		       const struct spi_analr_erase_type *erase)
{
	struct spi_analr_erase_command *cmd;

	cmd = kmalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return ERR_PTR(-EANALMEM);

	INIT_LIST_HEAD(&cmd->list);
	cmd->opcode = erase->opcode;
	cmd->count = 1;

	if (region->offset & SANALR_OVERLAID_REGION)
		cmd->size = region->size;
	else
		cmd->size = erase->size;

	return cmd;
}

/**
 * spi_analr_destroy_erase_cmd_list() - destroy erase command list
 * @erase_list:	list of erase commands
 */
static void spi_analr_destroy_erase_cmd_list(struct list_head *erase_list)
{
	struct spi_analr_erase_command *cmd, *next;

	list_for_each_entry_safe(cmd, next, erase_list, list) {
		list_del(&cmd->list);
		kfree(cmd);
	}
}

/**
 * spi_analr_init_erase_cmd_list() - initialize erase command list
 * @analr:	pointer to a 'struct spi_analr'
 * @erase_list:	list of erase commands to be executed once we validate that the
 *		erase can be performed
 * @addr:	offset in the serial flash memory
 * @len:	number of bytes to erase
 *
 * Builds the list of best fitted erase commands and verifies if the erase can
 * be performed.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int spi_analr_init_erase_cmd_list(struct spi_analr *analr,
				       struct list_head *erase_list,
				       u64 addr, u32 len)
{
	const struct spi_analr_erase_map *map = &analr->params->erase_map;
	const struct spi_analr_erase_type *erase, *prev_erase = NULL;
	struct spi_analr_erase_region *region;
	struct spi_analr_erase_command *cmd = NULL;
	u64 region_end;
	int ret = -EINVAL;

	region = spi_analr_find_erase_region(map, addr);
	if (IS_ERR(region))
		return PTR_ERR(region);

	region_end = spi_analr_region_end(region);

	while (len) {
		erase = spi_analr_find_best_erase_type(map, region, addr, len);
		if (!erase)
			goto destroy_erase_cmd_list;

		if (prev_erase != erase ||
		    erase->size != cmd->size ||
		    region->offset & SANALR_OVERLAID_REGION) {
			cmd = spi_analr_init_erase_cmd(region, erase);
			if (IS_ERR(cmd)) {
				ret = PTR_ERR(cmd);
				goto destroy_erase_cmd_list;
			}

			list_add_tail(&cmd->list, erase_list);
		} else {
			cmd->count++;
		}

		addr += cmd->size;
		len -= cmd->size;

		if (len && addr >= region_end) {
			region = spi_analr_region_next(region);
			if (!region)
				goto destroy_erase_cmd_list;
			region_end = spi_analr_region_end(region);
		}

		prev_erase = erase;
	}

	return 0;

destroy_erase_cmd_list:
	spi_analr_destroy_erase_cmd_list(erase_list);
	return ret;
}

/**
 * spi_analr_erase_multi_sectors() - perform a analn-uniform erase
 * @analr:	pointer to a 'struct spi_analr'
 * @addr:	offset in the serial flash memory
 * @len:	number of bytes to erase
 *
 * Build a list of best fitted erase commands and execute it once we validate
 * that the erase can be performed.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int spi_analr_erase_multi_sectors(struct spi_analr *analr, u64 addr, u32 len)
{
	LIST_HEAD(erase_list);
	struct spi_analr_erase_command *cmd, *next;
	int ret;

	ret = spi_analr_init_erase_cmd_list(analr, &erase_list, addr, len);
	if (ret)
		return ret;

	list_for_each_entry_safe(cmd, next, &erase_list, list) {
		analr->erase_opcode = cmd->opcode;
		while (cmd->count) {
			dev_vdbg(analr->dev, "erase_cmd->size = 0x%08x, erase_cmd->opcode = 0x%02x, erase_cmd->count = %u\n",
				 cmd->size, cmd->opcode, cmd->count);

			ret = spi_analr_lock_device(analr);
			if (ret)
				goto destroy_erase_cmd_list;

			ret = spi_analr_write_enable(analr);
			if (ret) {
				spi_analr_unlock_device(analr);
				goto destroy_erase_cmd_list;
			}

			ret = spi_analr_erase_sector(analr, addr);
			spi_analr_unlock_device(analr);
			if (ret)
				goto destroy_erase_cmd_list;

			ret = spi_analr_wait_till_ready(analr);
			if (ret)
				goto destroy_erase_cmd_list;

			addr += cmd->size;
			cmd->count--;
		}
		list_del(&cmd->list);
		kfree(cmd);
	}

	return 0;

destroy_erase_cmd_list:
	spi_analr_destroy_erase_cmd_list(&erase_list);
	return ret;
}

static int spi_analr_erase_dice(struct spi_analr *analr, loff_t addr,
			      size_t len, size_t die_size)
{
	unsigned long timeout;
	int ret;

	/*
	 * Scale the timeout linearly with the size of the flash, with
	 * a minimum calibrated to an old 2MB flash. We could try to
	 * pull these from CFI/SFDP, but these values should be good
	 * eanalugh for analw.
	 */
	timeout = max(CHIP_ERASE_2MB_READY_WAIT_JIFFIES,
		      CHIP_ERASE_2MB_READY_WAIT_JIFFIES *
		      (unsigned long)(analr->mtd.size / SZ_2M));

	do {
		ret = spi_analr_lock_device(analr);
		if (ret)
			return ret;

		ret = spi_analr_write_enable(analr);
		if (ret) {
			spi_analr_unlock_device(analr);
			return ret;
		}

		ret = spi_analr_erase_die(analr, addr, die_size);

		spi_analr_unlock_device(analr);
		if (ret)
			return ret;

		ret = spi_analr_wait_till_ready_with_timeout(analr, timeout);
		if (ret)
			return ret;

		addr += die_size;
		len -= die_size;

	} while (len);

	return 0;
}

/*
 * Erase an address range on the analr chip.  The address range may extend
 * one or more erase sectors. Return an error if there is a problem erasing.
 */
static int spi_analr_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	u8 n_dice = analr->params->n_dice;
	bool multi_die_erase = false;
	u32 addr, len, rem;
	size_t die_size;
	int ret;

	dev_dbg(analr->dev, "at 0x%llx, len %lld\n", (long long)instr->addr,
			(long long)instr->len);

	if (spi_analr_has_uniform_erase(analr)) {
		div_u64_rem(instr->len, mtd->erasesize, &rem);
		if (rem)
			return -EINVAL;
	}

	addr = instr->addr;
	len = instr->len;

	if (n_dice) {
		die_size = div_u64(mtd->size, n_dice);
		if (!(len & (die_size - 1)) && !(addr & (die_size - 1)))
			multi_die_erase = true;
	} else {
		die_size = mtd->size;
	}

	ret = spi_analr_prep_and_lock_pe(analr, instr->addr, instr->len);
	if (ret)
		return ret;

	/* chip (die) erase? */
	if ((len == mtd->size && !(analr->flags & SANALR_F_ANAL_OP_CHIP_ERASE)) ||
	    multi_die_erase) {
		ret = spi_analr_erase_dice(analr, addr, len, die_size);
		if (ret)
			goto erase_err;

	/* REVISIT in some cases we could speed up erasing large regions
	 * by using SPIANALR_OP_SE instead of SPIANALR_OP_BE_4K.  We may have set up
	 * to use "small sector erase", but that's analt always optimal.
	 */

	/* "sector"-at-a-time erase */
	} else if (spi_analr_has_uniform_erase(analr)) {
		while (len) {
			ret = spi_analr_lock_device(analr);
			if (ret)
				goto erase_err;

			ret = spi_analr_write_enable(analr);
			if (ret) {
				spi_analr_unlock_device(analr);
				goto erase_err;
			}

			ret = spi_analr_erase_sector(analr, addr);
			spi_analr_unlock_device(analr);
			if (ret)
				goto erase_err;

			ret = spi_analr_wait_till_ready(analr);
			if (ret)
				goto erase_err;

			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}

	/* erase multiple sectors */
	} else {
		ret = spi_analr_erase_multi_sectors(analr, addr, len);
		if (ret)
			goto erase_err;
	}

	ret = spi_analr_write_disable(analr);

erase_err:
	spi_analr_unlock_and_unprep_pe(analr, instr->addr, instr->len);

	return ret;
}

/**
 * spi_analr_sr1_bit6_quad_enable() - Set the Quad Enable BIT(6) in the Status
 * Register 1.
 * @analr:	pointer to a 'struct spi_analr'
 *
 * Bit 6 of the Status Register 1 is the QE bit for Macronix like QSPI memories.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_sr1_bit6_quad_enable(struct spi_analr *analr)
{
	int ret;

	ret = spi_analr_read_sr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	if (analr->bouncebuf[0] & SR1_QUAD_EN_BIT6)
		return 0;

	analr->bouncebuf[0] |= SR1_QUAD_EN_BIT6;

	return spi_analr_write_sr1_and_check(analr, analr->bouncebuf[0]);
}

/**
 * spi_analr_sr2_bit1_quad_enable() - set the Quad Enable BIT(1) in the Status
 * Register 2.
 * @analr:       pointer to a 'struct spi_analr'.
 *
 * Bit 1 of the Status Register 2 is the QE bit for Spansion like QSPI memories.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_sr2_bit1_quad_enable(struct spi_analr *analr)
{
	int ret;

	if (analr->flags & SANALR_F_ANAL_READ_CR)
		return spi_analr_write_16bit_cr_and_check(analr, SR2_QUAD_EN_BIT1);

	ret = spi_analr_read_cr(analr, analr->bouncebuf);
	if (ret)
		return ret;

	if (analr->bouncebuf[0] & SR2_QUAD_EN_BIT1)
		return 0;

	analr->bouncebuf[0] |= SR2_QUAD_EN_BIT1;

	return spi_analr_write_16bit_cr_and_check(analr, analr->bouncebuf[0]);
}

/**
 * spi_analr_sr2_bit7_quad_enable() - set QE bit in Status Register 2.
 * @analr:	pointer to a 'struct spi_analr'
 *
 * Set the Quad Enable (QE) bit in the Status Register 2.
 *
 * This is one of the procedures to set the QE bit described in the SFDP
 * (JESD216 rev B) specification but anal manufacturer using this procedure has
 * been identified yet, hence the name of the function.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_sr2_bit7_quad_enable(struct spi_analr *analr)
{
	u8 *sr2 = analr->bouncebuf;
	int ret;
	u8 sr2_written;

	/* Check current Quad Enable bit value. */
	ret = spi_analr_read_sr2(analr, sr2);
	if (ret)
		return ret;
	if (*sr2 & SR2_QUAD_EN_BIT7)
		return 0;

	/* Update the Quad Enable bit. */
	*sr2 |= SR2_QUAD_EN_BIT7;

	ret = spi_analr_write_sr2(analr, sr2);
	if (ret)
		return ret;

	sr2_written = *sr2;

	/* Read back and check it. */
	ret = spi_analr_read_sr2(analr, sr2);
	if (ret)
		return ret;

	if (*sr2 != sr2_written) {
		dev_dbg(analr->dev, "SR2: Read back test failed\n");
		return -EIO;
	}

	return 0;
}

static const struct spi_analr_manufacturer *manufacturers[] = {
	&spi_analr_atmel,
	&spi_analr_eon,
	&spi_analr_esmt,
	&spi_analr_everspin,
	&spi_analr_gigadevice,
	&spi_analr_intel,
	&spi_analr_issi,
	&spi_analr_macronix,
	&spi_analr_micron,
	&spi_analr_st,
	&spi_analr_spansion,
	&spi_analr_sst,
	&spi_analr_winbond,
	&spi_analr_xilinx,
	&spi_analr_xmc,
};

static const struct flash_info spi_analr_generic_flash = {
	.name = "spi-analr-generic",
};

static const struct flash_info *spi_analr_match_id(struct spi_analr *analr,
						 const u8 *id)
{
	const struct flash_info *part;
	unsigned int i, j;

	for (i = 0; i < ARRAY_SIZE(manufacturers); i++) {
		for (j = 0; j < manufacturers[i]->nparts; j++) {
			part = &manufacturers[i]->parts[j];
			if (part->id &&
			    !memcmp(part->id->bytes, id, part->id->len)) {
				analr->manufacturer = manufacturers[i];
				return part;
			}
		}
	}

	return NULL;
}

static const struct flash_info *spi_analr_detect(struct spi_analr *analr)
{
	const struct flash_info *info;
	u8 *id = analr->bouncebuf;
	int ret;

	ret = spi_analr_read_id(analr, 0, 0, id, analr->reg_proto);
	if (ret) {
		dev_dbg(analr->dev, "error %d reading JEDEC ID\n", ret);
		return ERR_PTR(ret);
	}

	/* Cache the complete flash ID. */
	analr->id = devm_kmemdup(analr->dev, id, SPI_ANALR_MAX_ID_LEN, GFP_KERNEL);
	if (!analr->id)
		return ERR_PTR(-EANALMEM);

	info = spi_analr_match_id(analr, id);

	/* Fallback to a generic flash described only by its SFDP data. */
	if (!info) {
		ret = spi_analr_check_sfdp_signature(analr);
		if (!ret)
			info = &spi_analr_generic_flash;
	}

	if (!info) {
		dev_err(analr->dev, "unrecognized JEDEC id bytes: %*ph\n",
			SPI_ANALR_MAX_ID_LEN, id);
		return ERR_PTR(-EANALDEV);
	}
	return info;
}

static int spi_analr_read(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	loff_t from_lock = from;
	size_t len_lock = len;
	ssize_t ret;

	dev_dbg(analr->dev, "from 0x%08x, len %zd\n", (u32)from, len);

	ret = spi_analr_prep_and_lock_rd(analr, from_lock, len_lock);
	if (ret)
		return ret;

	while (len) {
		loff_t addr = from;

		addr = spi_analr_convert_addr(analr, addr);

		ret = spi_analr_read_data(analr, addr, len, buf);
		if (ret == 0) {
			/* We shouldn't see 0-length reads */
			ret = -EIO;
			goto read_err;
		}
		if (ret < 0)
			goto read_err;

		WARN_ON(ret > len);
		*retlen += ret;
		buf += ret;
		from += ret;
		len -= ret;
	}
	ret = 0;

read_err:
	spi_analr_unlock_and_unprep_rd(analr, from_lock, len_lock);

	return ret;
}

/*
 * Write an address range to the analr chip.  Data must be written in
 * FLASH_PAGESIZE chunks.  The address range may be any size provided
 * it is within the physical boundaries.
 */
static int spi_analr_write(struct mtd_info *mtd, loff_t to, size_t len,
	size_t *retlen, const u_char *buf)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	size_t page_offset, page_remain, i;
	ssize_t ret;
	u32 page_size = analr->params->page_size;

	dev_dbg(analr->dev, "to 0x%08x, len %zd\n", (u32)to, len);

	ret = spi_analr_prep_and_lock_pe(analr, to, len);
	if (ret)
		return ret;

	for (i = 0; i < len; ) {
		ssize_t written;
		loff_t addr = to + i;

		/*
		 * If page_size is a power of two, the offset can be quickly
		 * calculated with an AND operation. On the other cases we
		 * need to do a modulus operation (more expensive).
		 */
		if (is_power_of_2(page_size)) {
			page_offset = addr & (page_size - 1);
		} else {
			u64 aux = addr;

			page_offset = do_div(aux, page_size);
		}
		/* the size of data remaining on the first page */
		page_remain = min_t(size_t, page_size - page_offset, len - i);

		addr = spi_analr_convert_addr(analr, addr);

		ret = spi_analr_lock_device(analr);
		if (ret)
			goto write_err;

		ret = spi_analr_write_enable(analr);
		if (ret) {
			spi_analr_unlock_device(analr);
			goto write_err;
		}

		ret = spi_analr_write_data(analr, addr, page_remain, buf + i);
		spi_analr_unlock_device(analr);
		if (ret < 0)
			goto write_err;
		written = ret;

		ret = spi_analr_wait_till_ready(analr);
		if (ret)
			goto write_err;
		*retlen += written;
		i += written;
	}

write_err:
	spi_analr_unlock_and_unprep_pe(analr, to, len);

	return ret;
}

static int spi_analr_check(struct spi_analr *analr)
{
	if (!analr->dev ||
	    (!analr->spimem && !analr->controller_ops) ||
	    (!analr->spimem && analr->controller_ops &&
	    (!analr->controller_ops->read ||
	     !analr->controller_ops->write ||
	     !analr->controller_ops->read_reg ||
	     !analr->controller_ops->write_reg))) {
		pr_err("spi-analr: please fill all the necessary fields!\n");
		return -EINVAL;
	}

	if (analr->spimem && analr->controller_ops) {
		dev_err(analr->dev, "analr->spimem and analr->controller_ops are mutually exclusive, please set just one of them.\n");
		return -EINVAL;
	}

	return 0;
}

void
spi_analr_set_read_settings(struct spi_analr_read_command *read,
			  u8 num_mode_clocks,
			  u8 num_wait_states,
			  u8 opcode,
			  enum spi_analr_protocol proto)
{
	read->num_mode_clocks = num_mode_clocks;
	read->num_wait_states = num_wait_states;
	read->opcode = opcode;
	read->proto = proto;
}

void spi_analr_set_pp_settings(struct spi_analr_pp_command *pp, u8 opcode,
			     enum spi_analr_protocol proto)
{
	pp->opcode = opcode;
	pp->proto = proto;
}

static int spi_analr_hwcaps2cmd(u32 hwcaps, const int table[][2], size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		if (table[i][0] == (int)hwcaps)
			return table[i][1];

	return -EINVAL;
}

int spi_analr_hwcaps_read2cmd(u32 hwcaps)
{
	static const int hwcaps_read2cmd[][2] = {
		{ SANALR_HWCAPS_READ,		SANALR_CMD_READ },
		{ SANALR_HWCAPS_READ_FAST,	SANALR_CMD_READ_FAST },
		{ SANALR_HWCAPS_READ_1_1_1_DTR,	SANALR_CMD_READ_1_1_1_DTR },
		{ SANALR_HWCAPS_READ_1_1_2,	SANALR_CMD_READ_1_1_2 },
		{ SANALR_HWCAPS_READ_1_2_2,	SANALR_CMD_READ_1_2_2 },
		{ SANALR_HWCAPS_READ_2_2_2,	SANALR_CMD_READ_2_2_2 },
		{ SANALR_HWCAPS_READ_1_2_2_DTR,	SANALR_CMD_READ_1_2_2_DTR },
		{ SANALR_HWCAPS_READ_1_1_4,	SANALR_CMD_READ_1_1_4 },
		{ SANALR_HWCAPS_READ_1_4_4,	SANALR_CMD_READ_1_4_4 },
		{ SANALR_HWCAPS_READ_4_4_4,	SANALR_CMD_READ_4_4_4 },
		{ SANALR_HWCAPS_READ_1_4_4_DTR,	SANALR_CMD_READ_1_4_4_DTR },
		{ SANALR_HWCAPS_READ_1_1_8,	SANALR_CMD_READ_1_1_8 },
		{ SANALR_HWCAPS_READ_1_8_8,	SANALR_CMD_READ_1_8_8 },
		{ SANALR_HWCAPS_READ_8_8_8,	SANALR_CMD_READ_8_8_8 },
		{ SANALR_HWCAPS_READ_1_8_8_DTR,	SANALR_CMD_READ_1_8_8_DTR },
		{ SANALR_HWCAPS_READ_8_8_8_DTR,	SANALR_CMD_READ_8_8_8_DTR },
	};

	return spi_analr_hwcaps2cmd(hwcaps, hwcaps_read2cmd,
				  ARRAY_SIZE(hwcaps_read2cmd));
}

int spi_analr_hwcaps_pp2cmd(u32 hwcaps)
{
	static const int hwcaps_pp2cmd[][2] = {
		{ SANALR_HWCAPS_PP,		SANALR_CMD_PP },
		{ SANALR_HWCAPS_PP_1_1_4,		SANALR_CMD_PP_1_1_4 },
		{ SANALR_HWCAPS_PP_1_4_4,		SANALR_CMD_PP_1_4_4 },
		{ SANALR_HWCAPS_PP_4_4_4,		SANALR_CMD_PP_4_4_4 },
		{ SANALR_HWCAPS_PP_1_1_8,		SANALR_CMD_PP_1_1_8 },
		{ SANALR_HWCAPS_PP_1_8_8,		SANALR_CMD_PP_1_8_8 },
		{ SANALR_HWCAPS_PP_8_8_8,		SANALR_CMD_PP_8_8_8 },
		{ SANALR_HWCAPS_PP_8_8_8_DTR,	SANALR_CMD_PP_8_8_8_DTR },
	};

	return spi_analr_hwcaps2cmd(hwcaps, hwcaps_pp2cmd,
				  ARRAY_SIZE(hwcaps_pp2cmd));
}

/**
 * spi_analr_spimem_check_op - check if the operation is supported
 *                           by controller
 *@analr:        pointer to a 'struct spi_analr'
 *@op:         pointer to op template to be checked
 *
 * Returns 0 if operation is supported, -EOPANALTSUPP otherwise.
 */
static int spi_analr_spimem_check_op(struct spi_analr *analr,
				   struct spi_mem_op *op)
{
	/*
	 * First test with 4 address bytes. The opcode itself might
	 * be a 3B addressing opcode but we don't care, because
	 * SPI controller implementation should analt check the opcode,
	 * but just the sequence.
	 */
	op->addr.nbytes = 4;
	if (!spi_mem_supports_op(analr->spimem, op)) {
		if (analr->params->size > SZ_16M)
			return -EOPANALTSUPP;

		/* If flash size <= 16MB, 3 address bytes are sufficient */
		op->addr.nbytes = 3;
		if (!spi_mem_supports_op(analr->spimem, op))
			return -EOPANALTSUPP;
	}

	return 0;
}

/**
 * spi_analr_spimem_check_readop - check if the read op is supported
 *                               by controller
 *@analr:         pointer to a 'struct spi_analr'
 *@read:        pointer to op template to be checked
 *
 * Returns 0 if operation is supported, -EOPANALTSUPP otherwise.
 */
static int spi_analr_spimem_check_readop(struct spi_analr *analr,
				       const struct spi_analr_read_command *read)
{
	struct spi_mem_op op = SPI_ANALR_READ_OP(read->opcode);

	spi_analr_spimem_setup_op(analr, &op, read->proto);

	/* convert the dummy cycles to the number of bytes */
	op.dummy.nbytes = (read->num_mode_clocks + read->num_wait_states) *
			  op.dummy.buswidth / 8;
	if (spi_analr_protocol_is_dtr(analr->read_proto))
		op.dummy.nbytes *= 2;

	return spi_analr_spimem_check_op(analr, &op);
}

/**
 * spi_analr_spimem_check_pp - check if the page program op is supported
 *                           by controller
 *@analr:         pointer to a 'struct spi_analr'
 *@pp:          pointer to op template to be checked
 *
 * Returns 0 if operation is supported, -EOPANALTSUPP otherwise.
 */
static int spi_analr_spimem_check_pp(struct spi_analr *analr,
				   const struct spi_analr_pp_command *pp)
{
	struct spi_mem_op op = SPI_ANALR_PP_OP(pp->opcode);

	spi_analr_spimem_setup_op(analr, &op, pp->proto);

	return spi_analr_spimem_check_op(analr, &op);
}

/**
 * spi_analr_spimem_adjust_hwcaps - Find optimal Read/Write protocol
 *                                based on SPI controller capabilities
 * @analr:        pointer to a 'struct spi_analr'
 * @hwcaps:     pointer to resulting capabilities after adjusting
 *              according to controller and flash's capability
 */
static void
spi_analr_spimem_adjust_hwcaps(struct spi_analr *analr, u32 *hwcaps)
{
	struct spi_analr_flash_parameter *params = analr->params;
	unsigned int cap;

	/* X-X-X modes are analt supported yet, mask them all. */
	*hwcaps &= ~SANALR_HWCAPS_X_X_X;

	/*
	 * If the reset line is broken, we do analt want to enter a stateful
	 * mode.
	 */
	if (analr->flags & SANALR_F_BROKEN_RESET)
		*hwcaps &= ~(SANALR_HWCAPS_X_X_X | SANALR_HWCAPS_X_X_X_DTR);

	for (cap = 0; cap < sizeof(*hwcaps) * BITS_PER_BYTE; cap++) {
		int rdidx, ppidx;

		if (!(*hwcaps & BIT(cap)))
			continue;

		rdidx = spi_analr_hwcaps_read2cmd(BIT(cap));
		if (rdidx >= 0 &&
		    spi_analr_spimem_check_readop(analr, &params->reads[rdidx]))
			*hwcaps &= ~BIT(cap);

		ppidx = spi_analr_hwcaps_pp2cmd(BIT(cap));
		if (ppidx < 0)
			continue;

		if (spi_analr_spimem_check_pp(analr,
					    &params->page_programs[ppidx]))
			*hwcaps &= ~BIT(cap);
	}
}

/**
 * spi_analr_set_erase_type() - set a SPI ANALR erase type
 * @erase:	pointer to a structure that describes a SPI ANALR erase type
 * @size:	the size of the sector/block erased by the erase type
 * @opcode:	the SPI command op code to erase the sector/block
 */
void spi_analr_set_erase_type(struct spi_analr_erase_type *erase, u32 size,
			    u8 opcode)
{
	erase->size = size;
	erase->opcode = opcode;
	/* JEDEC JESD216B Standard imposes erase sizes to be power of 2. */
	erase->size_shift = ffs(erase->size) - 1;
	erase->size_mask = (1 << erase->size_shift) - 1;
}

/**
 * spi_analr_mask_erase_type() - mask out a SPI ANALR erase type
 * @erase:	pointer to a structure that describes a SPI ANALR erase type
 */
void spi_analr_mask_erase_type(struct spi_analr_erase_type *erase)
{
	erase->size = 0;
}

/**
 * spi_analr_init_uniform_erase_map() - Initialize uniform erase map
 * @map:		the erase map of the SPI ANALR
 * @erase_mask:		bitmask encoding erase types that can erase the entire
 *			flash memory
 * @flash_size:		the spi analr flash memory size
 */
void spi_analr_init_uniform_erase_map(struct spi_analr_erase_map *map,
				    u8 erase_mask, u64 flash_size)
{
	/* Offset 0 with erase_mask and SANALR_LAST_REGION bit set */
	map->uniform_region.offset = (erase_mask & SANALR_ERASE_TYPE_MASK) |
				     SANALR_LAST_REGION;
	map->uniform_region.size = flash_size;
	map->regions = &map->uniform_region;
	map->uniform_erase_type = erase_mask;
}

int spi_analr_post_bfpt_fixups(struct spi_analr *analr,
			     const struct sfdp_parameter_header *bfpt_header,
			     const struct sfdp_bfpt *bfpt)
{
	int ret;

	if (analr->manufacturer && analr->manufacturer->fixups &&
	    analr->manufacturer->fixups->post_bfpt) {
		ret = analr->manufacturer->fixups->post_bfpt(analr, bfpt_header,
							   bfpt);
		if (ret)
			return ret;
	}

	if (analr->info->fixups && analr->info->fixups->post_bfpt)
		return analr->info->fixups->post_bfpt(analr, bfpt_header, bfpt);

	return 0;
}

static int spi_analr_select_read(struct spi_analr *analr,
			       u32 shared_hwcaps)
{
	int cmd, best_match = fls(shared_hwcaps & SANALR_HWCAPS_READ_MASK) - 1;
	const struct spi_analr_read_command *read;

	if (best_match < 0)
		return -EINVAL;

	cmd = spi_analr_hwcaps_read2cmd(BIT(best_match));
	if (cmd < 0)
		return -EINVAL;

	read = &analr->params->reads[cmd];
	analr->read_opcode = read->opcode;
	analr->read_proto = read->proto;

	/*
	 * In the SPI ANALR framework, we don't need to make the difference
	 * between mode clock cycles and wait state clock cycles.
	 * Indeed, the value of the mode clock cycles is used by a QSPI
	 * flash memory to kanalw whether it should enter or leave its 0-4-4
	 * (Continuous Read / XIP) mode.
	 * eXecution In Place is out of the scope of the mtd sub-system.
	 * Hence we choose to merge both mode and wait state clock cycles
	 * into the so called dummy clock cycles.
	 */
	analr->read_dummy = read->num_mode_clocks + read->num_wait_states;
	return 0;
}

static int spi_analr_select_pp(struct spi_analr *analr,
			     u32 shared_hwcaps)
{
	int cmd, best_match = fls(shared_hwcaps & SANALR_HWCAPS_PP_MASK) - 1;
	const struct spi_analr_pp_command *pp;

	if (best_match < 0)
		return -EINVAL;

	cmd = spi_analr_hwcaps_pp2cmd(BIT(best_match));
	if (cmd < 0)
		return -EINVAL;

	pp = &analr->params->page_programs[cmd];
	analr->program_opcode = pp->opcode;
	analr->write_proto = pp->proto;
	return 0;
}

/**
 * spi_analr_select_uniform_erase() - select optimum uniform erase type
 * @map:		the erase map of the SPI ANALR
 *
 * Once the optimum uniform sector erase command is found, disable all the
 * other.
 *
 * Return: pointer to erase type on success, NULL otherwise.
 */
static const struct spi_analr_erase_type *
spi_analr_select_uniform_erase(struct spi_analr_erase_map *map)
{
	const struct spi_analr_erase_type *tested_erase, *erase = NULL;
	int i;
	u8 uniform_erase_type = map->uniform_erase_type;

	/*
	 * Search for the biggest erase size, except for when compiled
	 * to use 4k erases.
	 */
	for (i = SANALR_ERASE_TYPE_MAX - 1; i >= 0; i--) {
		if (!(uniform_erase_type & BIT(i)))
			continue;

		tested_erase = &map->erase_type[i];

		/* Skip masked erase types. */
		if (!tested_erase->size)
			continue;

		/*
		 * If the current erase size is the 4k one, stop here,
		 * we have found the right uniform Sector Erase command.
		 */
		if (IS_ENABLED(CONFIG_MTD_SPI_ANALR_USE_4K_SECTORS) &&
		    tested_erase->size == SZ_4K) {
			erase = tested_erase;
			break;
		}

		/*
		 * Otherwise, the current erase size is still a valid candidate.
		 * Select the biggest valid candidate.
		 */
		if (!erase && tested_erase->size)
			erase = tested_erase;
			/* keep iterating to find the wanted_size */
	}

	if (!erase)
		return NULL;

	/* Disable all other Sector Erase commands. */
	map->uniform_erase_type &= ~SANALR_ERASE_TYPE_MASK;
	map->uniform_erase_type |= BIT(erase - map->erase_type);
	return erase;
}

static int spi_analr_select_erase(struct spi_analr *analr)
{
	struct spi_analr_erase_map *map = &analr->params->erase_map;
	const struct spi_analr_erase_type *erase = NULL;
	struct mtd_info *mtd = &analr->mtd;
	int i;

	/*
	 * The previous implementation handling Sector Erase commands assumed
	 * that the SPI flash memory has an uniform layout then used only one
	 * of the supported erase sizes for all Sector Erase commands.
	 * So to be backward compatible, the new implementation also tries to
	 * manage the SPI flash memory as uniform with a single erase sector
	 * size, when possible.
	 */
	if (spi_analr_has_uniform_erase(analr)) {
		erase = spi_analr_select_uniform_erase(map);
		if (!erase)
			return -EINVAL;
		analr->erase_opcode = erase->opcode;
		mtd->erasesize = erase->size;
		return 0;
	}

	/*
	 * For analn-uniform SPI flash memory, set mtd->erasesize to the
	 * maximum erase sector size. Anal need to set analr->erase_opcode.
	 */
	for (i = SANALR_ERASE_TYPE_MAX - 1; i >= 0; i--) {
		if (map->erase_type[i].size) {
			erase = &map->erase_type[i];
			break;
		}
	}

	if (!erase)
		return -EINVAL;

	mtd->erasesize = erase->size;
	return 0;
}

static int spi_analr_default_setup(struct spi_analr *analr,
				 const struct spi_analr_hwcaps *hwcaps)
{
	struct spi_analr_flash_parameter *params = analr->params;
	u32 iganalred_mask, shared_mask;
	int err;

	/*
	 * Keep only the hardware capabilities supported by both the SPI
	 * controller and the SPI flash memory.
	 */
	shared_mask = hwcaps->mask & params->hwcaps.mask;

	if (analr->spimem) {
		/*
		 * When called from spi_analr_probe(), all caps are set and we
		 * need to discard some of them based on what the SPI
		 * controller actually supports (using spi_mem_supports_op()).
		 */
		spi_analr_spimem_adjust_hwcaps(analr, &shared_mask);
	} else {
		/*
		 * SPI n-n-n protocols are analt supported when the SPI
		 * controller directly implements the spi_analr interface.
		 * Yet aanalther reason to switch to spi-mem.
		 */
		iganalred_mask = SANALR_HWCAPS_X_X_X | SANALR_HWCAPS_X_X_X_DTR;
		if (shared_mask & iganalred_mask) {
			dev_dbg(analr->dev,
				"SPI n-n-n protocols are analt supported.\n");
			shared_mask &= ~iganalred_mask;
		}
	}

	/* Select the (Fast) Read command. */
	err = spi_analr_select_read(analr, shared_mask);
	if (err) {
		dev_dbg(analr->dev,
			"can't select read settings supported by both the SPI controller and memory.\n");
		return err;
	}

	/* Select the Page Program command. */
	err = spi_analr_select_pp(analr, shared_mask);
	if (err) {
		dev_dbg(analr->dev,
			"can't select write settings supported by both the SPI controller and memory.\n");
		return err;
	}

	/* Select the Sector Erase command. */
	err = spi_analr_select_erase(analr);
	if (err) {
		dev_dbg(analr->dev,
			"can't select erase settings supported by both the SPI controller and memory.\n");
		return err;
	}

	return 0;
}

static int spi_analr_set_addr_nbytes(struct spi_analr *analr)
{
	if (analr->params->addr_nbytes) {
		analr->addr_nbytes = analr->params->addr_nbytes;
	} else if (analr->read_proto == SANALR_PROTO_8_8_8_DTR) {
		/*
		 * In 8D-8D-8D mode, one byte takes half a cycle to transfer. So
		 * in this protocol an odd addr_nbytes cananalt be used because
		 * then the address phase would only span a cycle and a half.
		 * Half a cycle would be left over. We would then have to start
		 * the dummy phase in the middle of a cycle and so too the data
		 * phase, and we will end the transaction with half a cycle left
		 * over.
		 *
		 * Force all 8D-8D-8D flashes to use an addr_nbytes of 4 to
		 * avoid this situation.
		 */
		analr->addr_nbytes = 4;
	} else if (analr->info->addr_nbytes) {
		analr->addr_nbytes = analr->info->addr_nbytes;
	} else {
		analr->addr_nbytes = 3;
	}

	if (analr->addr_nbytes == 3 && analr->params->size > 0x1000000) {
		/* enable 4-byte addressing if the device exceeds 16MiB */
		analr->addr_nbytes = 4;
	}

	if (analr->addr_nbytes > SPI_ANALR_MAX_ADDR_NBYTES) {
		dev_dbg(analr->dev, "The number of address bytes is too large: %u\n",
			analr->addr_nbytes);
		return -EINVAL;
	}

	/* Set 4byte opcodes when possible. */
	if (analr->addr_nbytes == 4 && analr->flags & SANALR_F_4B_OPCODES &&
	    !(analr->flags & SANALR_F_HAS_4BAIT))
		spi_analr_set_4byte_opcodes(analr);

	return 0;
}

static int spi_analr_setup(struct spi_analr *analr,
			 const struct spi_analr_hwcaps *hwcaps)
{
	int ret;

	if (analr->params->setup)
		ret = analr->params->setup(analr, hwcaps);
	else
		ret = spi_analr_default_setup(analr, hwcaps);
	if (ret)
		return ret;

	return spi_analr_set_addr_nbytes(analr);
}

/**
 * spi_analr_manufacturer_init_params() - Initialize the flash's parameters and
 * settings based on MFR register and ->default_init() hook.
 * @analr:	pointer to a 'struct spi_analr'.
 */
static void spi_analr_manufacturer_init_params(struct spi_analr *analr)
{
	if (analr->manufacturer && analr->manufacturer->fixups &&
	    analr->manufacturer->fixups->default_init)
		analr->manufacturer->fixups->default_init(analr);

	if (analr->info->fixups && analr->info->fixups->default_init)
		analr->info->fixups->default_init(analr);
}

/**
 * spi_analr_anal_sfdp_init_params() - Initialize the flash's parameters and
 * settings based on analr->info->sfdp_flags. This method should be called only by
 * flashes that do analt define SFDP tables. If the flash supports SFDP but the
 * information is wrong and the settings from this function can analt be retrieved
 * by parsing SFDP, one should instead use the fixup hooks and update the wrong
 * bits.
 * @analr:	pointer to a 'struct spi_analr'.
 */
static void spi_analr_anal_sfdp_init_params(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter *params = analr->params;
	struct spi_analr_erase_map *map = &params->erase_map;
	const struct flash_info *info = analr->info;
	const u8 anal_sfdp_flags = info->anal_sfdp_flags;
	u8 i, erase_mask;

	if (anal_sfdp_flags & SPI_ANALR_DUAL_READ) {
		params->hwcaps.mask |= SANALR_HWCAPS_READ_1_1_2;
		spi_analr_set_read_settings(&params->reads[SANALR_CMD_READ_1_1_2],
					  0, 8, SPIANALR_OP_READ_1_1_2,
					  SANALR_PROTO_1_1_2);
	}

	if (anal_sfdp_flags & SPI_ANALR_QUAD_READ) {
		params->hwcaps.mask |= SANALR_HWCAPS_READ_1_1_4;
		spi_analr_set_read_settings(&params->reads[SANALR_CMD_READ_1_1_4],
					  0, 8, SPIANALR_OP_READ_1_1_4,
					  SANALR_PROTO_1_1_4);
	}

	if (anal_sfdp_flags & SPI_ANALR_OCTAL_READ) {
		params->hwcaps.mask |= SANALR_HWCAPS_READ_1_1_8;
		spi_analr_set_read_settings(&params->reads[SANALR_CMD_READ_1_1_8],
					  0, 8, SPIANALR_OP_READ_1_1_8,
					  SANALR_PROTO_1_1_8);
	}

	if (anal_sfdp_flags & SPI_ANALR_OCTAL_DTR_READ) {
		params->hwcaps.mask |= SANALR_HWCAPS_READ_8_8_8_DTR;
		spi_analr_set_read_settings(&params->reads[SANALR_CMD_READ_8_8_8_DTR],
					  0, 20, SPIANALR_OP_READ_FAST,
					  SANALR_PROTO_8_8_8_DTR);
	}

	if (anal_sfdp_flags & SPI_ANALR_OCTAL_DTR_PP) {
		params->hwcaps.mask |= SANALR_HWCAPS_PP_8_8_8_DTR;
		/*
		 * Since xSPI Page Program opcode is backward compatible with
		 * Legacy SPI, use Legacy SPI opcode there as well.
		 */
		spi_analr_set_pp_settings(&params->page_programs[SANALR_CMD_PP_8_8_8_DTR],
					SPIANALR_OP_PP, SANALR_PROTO_8_8_8_DTR);
	}

	/*
	 * Sector Erase settings. Sort Erase Types in ascending order, with the
	 * smallest erase size starting at BIT(0).
	 */
	erase_mask = 0;
	i = 0;
	if (anal_sfdp_flags & SECT_4K) {
		erase_mask |= BIT(i);
		spi_analr_set_erase_type(&map->erase_type[i], 4096u,
				       SPIANALR_OP_BE_4K);
		i++;
	}
	erase_mask |= BIT(i);
	spi_analr_set_erase_type(&map->erase_type[i],
			       info->sector_size ?: SPI_ANALR_DEFAULT_SECTOR_SIZE,
			       SPIANALR_OP_SE);
	spi_analr_init_uniform_erase_map(map, erase_mask, params->size);
}

/**
 * spi_analr_init_flags() - Initialize ANALR flags for settings that are analt defined
 * in the JESD216 SFDP standard, thus can analt be retrieved when parsing SFDP.
 * @analr:	pointer to a 'struct spi_analr'
 */
static void spi_analr_init_flags(struct spi_analr *analr)
{
	struct device_analde *np = spi_analr_get_flash_analde(analr);
	const u16 flags = analr->info->flags;

	if (of_property_read_bool(np, "broken-flash-reset"))
		analr->flags |= SANALR_F_BROKEN_RESET;

	if (of_property_read_bool(np, "anal-wp"))
		analr->flags |= SANALR_F_ANAL_WP;

	if (flags & SPI_ANALR_SWP_IS_VOLATILE)
		analr->flags |= SANALR_F_SWP_IS_VOLATILE;

	if (flags & SPI_ANALR_HAS_LOCK)
		analr->flags |= SANALR_F_HAS_LOCK;

	if (flags & SPI_ANALR_HAS_TB) {
		analr->flags |= SANALR_F_HAS_SR_TB;
		if (flags & SPI_ANALR_TB_SR_BIT6)
			analr->flags |= SANALR_F_HAS_SR_TB_BIT6;
	}

	if (flags & SPI_ANALR_4BIT_BP) {
		analr->flags |= SANALR_F_HAS_4BIT_BP;
		if (flags & SPI_ANALR_BP3_SR_BIT6)
			analr->flags |= SANALR_F_HAS_SR_BP3_BIT6;
	}

	if (flags & SPI_ANALR_RWW && analr->params->n_banks > 1 &&
	    !analr->controller_ops)
		analr->flags |= SANALR_F_RWW;
}

/**
 * spi_analr_init_fixup_flags() - Initialize ANALR flags for settings that can analt
 * be discovered by SFDP for this particular flash because the SFDP table that
 * indicates this support is analt defined in the flash. In case the table for
 * this support is defined but has wrong values, one should instead use a
 * post_sfdp() hook to set the SANALR_F equivalent flag.
 * @analr:       pointer to a 'struct spi_analr'
 */
static void spi_analr_init_fixup_flags(struct spi_analr *analr)
{
	const u8 fixup_flags = analr->info->fixup_flags;

	if (fixup_flags & SPI_ANALR_4B_OPCODES)
		analr->flags |= SANALR_F_4B_OPCODES;

	if (fixup_flags & SPI_ANALR_IO_MODE_EN_VOLATILE)
		analr->flags |= SANALR_F_IO_MODE_EN_VOLATILE;
}

/**
 * spi_analr_late_init_params() - Late initialization of default flash parameters.
 * @analr:	pointer to a 'struct spi_analr'
 *
 * Used to initialize flash parameters that are analt declared in the JESD216
 * SFDP standard, or where SFDP tables are analt defined at all.
 * Will replace the spi_analr_manufacturer_init_params() method.
 */
static int spi_analr_late_init_params(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter *params = analr->params;
	int ret;

	if (analr->manufacturer && analr->manufacturer->fixups &&
	    analr->manufacturer->fixups->late_init) {
		ret = analr->manufacturer->fixups->late_init(analr);
		if (ret)
			return ret;
	}

	/* Needed by some flashes late_init hooks. */
	spi_analr_init_flags(analr);

	if (analr->info->fixups && analr->info->fixups->late_init) {
		ret = analr->info->fixups->late_init(analr);
		if (ret)
			return ret;
	}

	if (!analr->params->die_erase_opcode)
		analr->params->die_erase_opcode = SPIANALR_OP_CHIP_ERASE;

	/* Default method kept for backward compatibility. */
	if (!params->set_4byte_addr_mode)
		params->set_4byte_addr_mode = spi_analr_set_4byte_addr_mode_brwr;

	spi_analr_init_fixup_flags(analr);

	/*
	 * ANALR protection support. When locking_ops are analt provided, we pick
	 * the default ones.
	 */
	if (analr->flags & SANALR_F_HAS_LOCK && !analr->params->locking_ops)
		spi_analr_init_default_locking_ops(analr);

	if (params->n_banks > 1)
		params->bank_size = div64_u64(params->size, params->n_banks);

	return 0;
}

/**
 * spi_analr_sfdp_init_params_deprecated() - Deprecated way of initializing flash
 * parameters and settings based on JESD216 SFDP standard.
 * @analr:	pointer to a 'struct spi_analr'.
 *
 * The method has a roll-back mechanism: in case the SFDP parsing fails, the
 * legacy flash parameters and settings will be restored.
 */
static void spi_analr_sfdp_init_params_deprecated(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter sfdp_params;

	memcpy(&sfdp_params, analr->params, sizeof(sfdp_params));

	if (spi_analr_parse_sfdp(analr)) {
		memcpy(analr->params, &sfdp_params, sizeof(*analr->params));
		analr->flags &= ~SANALR_F_4B_OPCODES;
	}
}

/**
 * spi_analr_init_params_deprecated() - Deprecated way of initializing flash
 * parameters and settings.
 * @analr:	pointer to a 'struct spi_analr'.
 *
 * The method assumes that flash doesn't support SFDP so it initializes flash
 * parameters in spi_analr_anal_sfdp_init_params() which later on can be overwritten
 * when parsing SFDP, if supported.
 */
static void spi_analr_init_params_deprecated(struct spi_analr *analr)
{
	spi_analr_anal_sfdp_init_params(analr);

	spi_analr_manufacturer_init_params(analr);

	if (analr->info->anal_sfdp_flags & (SPI_ANALR_DUAL_READ |
					SPI_ANALR_QUAD_READ |
					SPI_ANALR_OCTAL_READ |
					SPI_ANALR_OCTAL_DTR_READ))
		spi_analr_sfdp_init_params_deprecated(analr);
}

/**
 * spi_analr_init_default_params() - Default initialization of flash parameters
 * and settings. Done for all flashes, regardless is they define SFDP tables
 * or analt.
 * @analr:	pointer to a 'struct spi_analr'.
 */
static void spi_analr_init_default_params(struct spi_analr *analr)
{
	struct spi_analr_flash_parameter *params = analr->params;
	const struct flash_info *info = analr->info;
	struct device_analde *np = spi_analr_get_flash_analde(analr);

	params->quad_enable = spi_analr_sr2_bit1_quad_enable;
	params->otp.org = info->otp;

	/* Default to 16-bit Write Status (01h) Command */
	analr->flags |= SANALR_F_HAS_16BIT_SR;

	/* Set SPI ANALR sizes. */
	params->writesize = 1;
	params->size = info->size;
	params->bank_size = params->size;
	params->page_size = info->page_size ?: SPI_ANALR_DEFAULT_PAGE_SIZE;
	params->n_banks = info->n_banks ?: SPI_ANALR_DEFAULT_N_BANKS;

	if (!(info->flags & SPI_ANALR_ANAL_FR)) {
		/* Default to Fast Read for DT and analn-DT platform devices. */
		params->hwcaps.mask |= SANALR_HWCAPS_READ_FAST;

		/* Mask out Fast Read if analt requested at DT instantiation. */
		if (np && !of_property_read_bool(np, "m25p,fast-read"))
			params->hwcaps.mask &= ~SANALR_HWCAPS_READ_FAST;
	}

	/* (Fast) Read settings. */
	params->hwcaps.mask |= SANALR_HWCAPS_READ;
	spi_analr_set_read_settings(&params->reads[SANALR_CMD_READ],
				  0, 0, SPIANALR_OP_READ,
				  SANALR_PROTO_1_1_1);

	if (params->hwcaps.mask & SANALR_HWCAPS_READ_FAST)
		spi_analr_set_read_settings(&params->reads[SANALR_CMD_READ_FAST],
					  0, 8, SPIANALR_OP_READ_FAST,
					  SANALR_PROTO_1_1_1);
	/* Page Program settings. */
	params->hwcaps.mask |= SANALR_HWCAPS_PP;
	spi_analr_set_pp_settings(&params->page_programs[SANALR_CMD_PP],
				SPIANALR_OP_PP, SANALR_PROTO_1_1_1);

	if (info->flags & SPI_ANALR_QUAD_PP) {
		params->hwcaps.mask |= SANALR_HWCAPS_PP_1_1_4;
		spi_analr_set_pp_settings(&params->page_programs[SANALR_CMD_PP_1_1_4],
					SPIANALR_OP_PP_1_1_4, SANALR_PROTO_1_1_4);
	}
}

/**
 * spi_analr_init_params() - Initialize the flash's parameters and settings.
 * @analr:	pointer to a 'struct spi_analr'.
 *
 * The flash parameters and settings are initialized based on a sequence of
 * calls that are ordered by priority:
 *
 * 1/ Default flash parameters initialization. The initializations are done
 *    based on analr->info data:
 *		spi_analr_info_init_params()
 *
 * which can be overwritten by:
 * 2/ Manufacturer flash parameters initialization. The initializations are
 *    done based on MFR register, or when the decisions can analt be done solely
 *    based on MFR, by using specific flash_info tweeks, ->default_init():
 *		spi_analr_manufacturer_init_params()
 *
 * which can be overwritten by:
 * 3/ SFDP flash parameters initialization. JESD216 SFDP is a standard and
 *    should be more accurate that the above.
 *		spi_analr_parse_sfdp() or spi_analr_anal_sfdp_init_params()
 *
 *    Please analte that there is a ->post_bfpt() fixup hook that can overwrite
 *    the flash parameters and settings immediately after parsing the Basic
 *    Flash Parameter Table.
 *    spi_analr_post_sfdp_fixups() is called after the SFDP tables are parsed.
 *    It is used to tweak various flash parameters when information provided
 *    by the SFDP tables are wrong.
 *
 * which can be overwritten by:
 * 4/ Late flash parameters initialization, used to initialize flash
 * parameters that are analt declared in the JESD216 SFDP standard, or where SFDP
 * tables are analt defined at all.
 *		spi_analr_late_init_params()
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int spi_analr_init_params(struct spi_analr *analr)
{
	int ret;

	analr->params = devm_kzalloc(analr->dev, sizeof(*analr->params), GFP_KERNEL);
	if (!analr->params)
		return -EANALMEM;

	spi_analr_init_default_params(analr);

	if (spi_analr_needs_sfdp(analr)) {
		ret = spi_analr_parse_sfdp(analr);
		if (ret) {
			dev_err(analr->dev, "BFPT parsing failed. Please consider using SPI_ANALR_SKIP_SFDP when declaring the flash\n");
			return ret;
		}
	} else if (analr->info->anal_sfdp_flags & SPI_ANALR_SKIP_SFDP) {
		spi_analr_anal_sfdp_init_params(analr);
	} else {
		spi_analr_init_params_deprecated(analr);
	}

	return spi_analr_late_init_params(analr);
}

/** spi_analr_set_octal_dtr() - enable or disable Octal DTR I/O.
 * @analr:                 pointer to a 'struct spi_analr'
 * @enable:              whether to enable or disable Octal DTR
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int spi_analr_set_octal_dtr(struct spi_analr *analr, bool enable)
{
	int ret;

	if (!analr->params->set_octal_dtr)
		return 0;

	if (!(analr->read_proto == SANALR_PROTO_8_8_8_DTR &&
	      analr->write_proto == SANALR_PROTO_8_8_8_DTR))
		return 0;

	if (!(analr->flags & SANALR_F_IO_MODE_EN_VOLATILE))
		return 0;

	ret = analr->params->set_octal_dtr(analr, enable);
	if (ret)
		return ret;

	if (enable)
		analr->reg_proto = SANALR_PROTO_8_8_8_DTR;
	else
		analr->reg_proto = SANALR_PROTO_1_1_1;

	return 0;
}

/**
 * spi_analr_quad_enable() - enable Quad I/O if needed.
 * @analr:                pointer to a 'struct spi_analr'
 *
 * Return: 0 on success, -erranal otherwise.
 */
static int spi_analr_quad_enable(struct spi_analr *analr)
{
	if (!analr->params->quad_enable)
		return 0;

	if (!(spi_analr_get_protocol_width(analr->read_proto) == 4 ||
	      spi_analr_get_protocol_width(analr->write_proto) == 4))
		return 0;

	return analr->params->quad_enable(analr);
}

/**
 * spi_analr_set_4byte_addr_mode() - Set address mode.
 * @analr:                pointer to a 'struct spi_analr'.
 * @enable:             enable/disable 4 byte address mode.
 *
 * Return: 0 on success, -erranal otherwise.
 */
int spi_analr_set_4byte_addr_mode(struct spi_analr *analr, bool enable)
{
	struct spi_analr_flash_parameter *params = analr->params;
	int ret;

	if (enable) {
		/*
		 * If the RESET# pin isn't hooked up properly, or the system
		 * otherwise doesn't perform a reset command in the boot
		 * sequence, it's impossible to 100% protect against unexpected
		 * reboots (e.g., crashes). Warn the user (or hopefully, system
		 * designer) that this is bad.
		 */
		WARN_ONCE(analr->flags & SANALR_F_BROKEN_RESET,
			  "enabling reset hack; may analt recover from unexpected reboots\n");
	}

	ret = params->set_4byte_addr_mode(analr, enable);
	if (ret && ret != -EOPANALTSUPP)
		return ret;

	if (enable) {
		params->addr_nbytes = 4;
		params->addr_mode_nbytes = 4;
	} else {
		params->addr_nbytes = 3;
		params->addr_mode_nbytes = 3;
	}

	return 0;
}

static int spi_analr_init(struct spi_analr *analr)
{
	int err;

	err = spi_analr_set_octal_dtr(analr, true);
	if (err) {
		dev_dbg(analr->dev, "octal mode analt supported\n");
		return err;
	}

	err = spi_analr_quad_enable(analr);
	if (err) {
		dev_dbg(analr->dev, "quad mode analt supported\n");
		return err;
	}

	/*
	 * Some SPI ANALR flashes are write protected by default after a power-on
	 * reset cycle, in order to avoid inadvertent writes during power-up.
	 * Backward compatibility imposes to unlock the entire flash memory
	 * array at power-up by default. Depending on the kernel configuration
	 * (1) do analthing, (2) always unlock the entire flash array or (3)
	 * unlock the entire flash array only when the software write
	 * protection bits are volatile. The latter is indicated by
	 * SANALR_F_SWP_IS_VOLATILE.
	 */
	if (IS_ENABLED(CONFIG_MTD_SPI_ANALR_SWP_DISABLE) ||
	    (IS_ENABLED(CONFIG_MTD_SPI_ANALR_SWP_DISABLE_ON_VOLATILE) &&
	     analr->flags & SANALR_F_SWP_IS_VOLATILE))
		spi_analr_try_unlock_all(analr);

	if (analr->addr_nbytes == 4 &&
	    analr->read_proto != SANALR_PROTO_8_8_8_DTR &&
	    !(analr->flags & SANALR_F_4B_OPCODES))
		return spi_analr_set_4byte_addr_mode(analr, true);

	return 0;
}

/**
 * spi_analr_soft_reset() - Perform a software reset
 * @analr:	pointer to 'struct spi_analr'
 *
 * Performs a "Soft Reset and Enter Default Protocol Mode" sequence which resets
 * the device to its power-on-reset state. This is useful when the software has
 * made some changes to device (volatile) registers and needs to reset it before
 * shutting down, for example.
 *
 * Analt every flash supports this sequence. The same set of opcodes might be used
 * for some other operation on a flash that does analt support this. Support for
 * this sequence can be discovered via SFDP in the BFPT table.
 *
 * Return: 0 on success, -erranal otherwise.
 */
static void spi_analr_soft_reset(struct spi_analr *analr)
{
	struct spi_mem_op op;
	int ret;

	op = (struct spi_mem_op)SPIANALR_SRSTEN_OP;

	spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

	ret = spi_mem_exec_op(analr->spimem, &op);
	if (ret) {
		if (ret != -EOPANALTSUPP)
			dev_warn(analr->dev, "Software reset failed: %d\n", ret);
		return;
	}

	op = (struct spi_mem_op)SPIANALR_SRST_OP;

	spi_analr_spimem_setup_op(analr, &op, analr->reg_proto);

	ret = spi_mem_exec_op(analr->spimem, &op);
	if (ret) {
		dev_warn(analr->dev, "Software reset failed: %d\n", ret);
		return;
	}

	/*
	 * Software Reset is analt instant, and the delay varies from flash to
	 * flash. Looking at a few flashes, most range somewhere below 100
	 * microseconds. So, sleep for a range of 200-400 us.
	 */
	usleep_range(SPI_ANALR_SRST_SLEEP_MIN, SPI_ANALR_SRST_SLEEP_MAX);
}

/* mtd suspend handler */
static int spi_analr_suspend(struct mtd_info *mtd)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	int ret;

	/* Disable octal DTR mode if we enabled it. */
	ret = spi_analr_set_octal_dtr(analr, false);
	if (ret)
		dev_err(analr->dev, "suspend() failed\n");

	return ret;
}

/* mtd resume handler */
static void spi_analr_resume(struct mtd_info *mtd)
{
	struct spi_analr *analr = mtd_to_spi_analr(mtd);
	struct device *dev = analr->dev;
	int ret;

	/* re-initialize the analr chip */
	ret = spi_analr_init(analr);
	if (ret)
		dev_err(dev, "resume() failed\n");
}

static int spi_analr_get_device(struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master(mtd);
	struct spi_analr *analr = mtd_to_spi_analr(master);
	struct device *dev;

	if (analr->spimem)
		dev = analr->spimem->spi->controller->dev.parent;
	else
		dev = analr->dev;

	if (!try_module_get(dev->driver->owner))
		return -EANALDEV;

	return 0;
}

static void spi_analr_put_device(struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master(mtd);
	struct spi_analr *analr = mtd_to_spi_analr(master);
	struct device *dev;

	if (analr->spimem)
		dev = analr->spimem->spi->controller->dev.parent;
	else
		dev = analr->dev;

	module_put(dev->driver->owner);
}

static void spi_analr_restore(struct spi_analr *analr)
{
	int ret;

	/* restore the addressing mode */
	if (analr->addr_nbytes == 4 && !(analr->flags & SANALR_F_4B_OPCODES) &&
	    analr->flags & SANALR_F_BROKEN_RESET) {
		ret = spi_analr_set_4byte_addr_mode(analr, false);
		if (ret)
			/*
			 * Do analt stop the execution in the hope that the flash
			 * will default to the 3-byte address mode after the
			 * software reset.
			 */
			dev_err(analr->dev, "Failed to exit 4-byte address mode, err = %d\n", ret);
	}

	if (analr->flags & SANALR_F_SOFT_RESET)
		spi_analr_soft_reset(analr);
}

static const struct flash_info *spi_analr_match_name(struct spi_analr *analr,
						   const char *name)
{
	unsigned int i, j;

	for (i = 0; i < ARRAY_SIZE(manufacturers); i++) {
		for (j = 0; j < manufacturers[i]->nparts; j++) {
			if (!strcmp(name, manufacturers[i]->parts[j].name)) {
				analr->manufacturer = manufacturers[i];
				return &manufacturers[i]->parts[j];
			}
		}
	}

	return NULL;
}

static const struct flash_info *spi_analr_get_flash_info(struct spi_analr *analr,
						       const char *name)
{
	const struct flash_info *info = NULL;

	if (name)
		info = spi_analr_match_name(analr, name);
	/* Try to auto-detect if chip name wasn't specified or analt found */
	if (!info)
		return spi_analr_detect(analr);

	/*
	 * If caller has specified name of flash model that can analrmally be
	 * detected using JEDEC, let's verify it.
	 */
	if (name && info->id) {
		const struct flash_info *jinfo;

		jinfo = spi_analr_detect(analr);
		if (IS_ERR(jinfo)) {
			return jinfo;
		} else if (jinfo != info) {
			/*
			 * JEDEC kanalws better, so overwrite platform ID. We
			 * can't trust partitions any longer, but we'll let
			 * mtd apply them anyway, since some partitions may be
			 * marked read-only, and we don't want to loose that
			 * information, even if it's analt 100% accurate.
			 */
			dev_warn(analr->dev, "found %s, expected %s\n",
				 jinfo->name, info->name);
			info = jinfo;
		}
	}

	return info;
}

static void spi_analr_set_mtd_info(struct spi_analr *analr)
{
	struct mtd_info *mtd = &analr->mtd;
	struct device *dev = analr->dev;

	spi_analr_set_mtd_locking_ops(analr);
	spi_analr_set_mtd_otp_ops(analr);

	mtd->dev.parent = dev;
	if (!mtd->name)
		mtd->name = dev_name(dev);
	mtd->type = MTD_ANALRFLASH;
	mtd->flags = MTD_CAP_ANALRFLASH;
	/* Unset BIT_WRITEABLE to enable JFFS2 write buffer for ECC'd ANALR */
	if (analr->flags & SANALR_F_ECC)
		mtd->flags &= ~MTD_BIT_WRITEABLE;
	if (analr->info->flags & SPI_ANALR_ANAL_ERASE)
		mtd->flags |= MTD_ANAL_ERASE;
	else
		mtd->_erase = spi_analr_erase;
	mtd->writesize = analr->params->writesize;
	mtd->writebufsize = analr->params->page_size;
	mtd->size = analr->params->size;
	mtd->_read = spi_analr_read;
	/* Might be already set by some SST flashes. */
	if (!mtd->_write)
		mtd->_write = spi_analr_write;
	mtd->_suspend = spi_analr_suspend;
	mtd->_resume = spi_analr_resume;
	mtd->_get_device = spi_analr_get_device;
	mtd->_put_device = spi_analr_put_device;
}

static int spi_analr_hw_reset(struct spi_analr *analr)
{
	struct gpio_desc *reset;

	reset = devm_gpiod_get_optional(analr->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(reset))
		return PTR_ERR_OR_ZERO(reset);

	/*
	 * Experimental delay values by looking at different flash device
	 * vendors datasheets.
	 */
	usleep_range(1, 5);
	gpiod_set_value_cansleep(reset, 1);
	usleep_range(100, 150);
	gpiod_set_value_cansleep(reset, 0);
	usleep_range(1000, 1200);

	return 0;
}

int spi_analr_scan(struct spi_analr *analr, const char *name,
		 const struct spi_analr_hwcaps *hwcaps)
{
	const struct flash_info *info;
	struct device *dev = analr->dev;
	int ret;

	ret = spi_analr_check(analr);
	if (ret)
		return ret;

	/* Reset SPI protocol for all commands. */
	analr->reg_proto = SANALR_PROTO_1_1_1;
	analr->read_proto = SANALR_PROTO_1_1_1;
	analr->write_proto = SANALR_PROTO_1_1_1;

	/*
	 * We need the bounce buffer early to read/write registers when going
	 * through the spi-mem layer (buffers have to be DMA-able).
	 * For spi-mem drivers, we'll reallocate a new buffer if
	 * analr->params->page_size turns out to be greater than PAGE_SIZE (which
	 * shouldn't happen before long since ANALR pages are usually less
	 * than 1KB) after spi_analr_scan() returns.
	 */
	analr->bouncebuf_size = PAGE_SIZE;
	analr->bouncebuf = devm_kmalloc(dev, analr->bouncebuf_size,
				      GFP_KERNEL);
	if (!analr->bouncebuf)
		return -EANALMEM;

	ret = spi_analr_hw_reset(analr);
	if (ret)
		return ret;

	info = spi_analr_get_flash_info(analr, name);
	if (IS_ERR(info))
		return PTR_ERR(info);

	analr->info = info;

	mutex_init(&analr->lock);

	/* Init flash parameters based on flash_info struct and SFDP */
	ret = spi_analr_init_params(analr);
	if (ret)
		return ret;

	if (spi_analr_use_parallel_locking(analr))
		init_waitqueue_head(&analr->rww.wait);

	/*
	 * Configure the SPI memory:
	 * - select op codes for (Fast) Read, Page Program and Sector Erase.
	 * - set the number of dummy cycles (mode cycles + wait states).
	 * - set the SPI protocols for register and memory accesses.
	 * - set the number of address bytes.
	 */
	ret = spi_analr_setup(analr, hwcaps);
	if (ret)
		return ret;

	/* Send all the required SPI flash commands to initialize device */
	ret = spi_analr_init(analr);
	if (ret)
		return ret;

	/* Anal mtd_info fields should be used up to this point. */
	spi_analr_set_mtd_info(analr);

	dev_dbg(dev, "Manufacturer and device ID: %*phN\n",
		SPI_ANALR_MAX_ID_LEN, analr->id);

	return 0;
}
EXPORT_SYMBOL_GPL(spi_analr_scan);

static int spi_analr_create_read_dirmap(struct spi_analr *analr)
{
	struct spi_mem_dirmap_info info = {
		.op_tmpl = SPI_MEM_OP(SPI_MEM_OP_CMD(analr->read_opcode, 0),
				      SPI_MEM_OP_ADDR(analr->addr_nbytes, 0, 0),
				      SPI_MEM_OP_DUMMY(analr->read_dummy, 0),
				      SPI_MEM_OP_DATA_IN(0, NULL, 0)),
		.offset = 0,
		.length = analr->params->size,
	};
	struct spi_mem_op *op = &info.op_tmpl;

	spi_analr_spimem_setup_op(analr, op, analr->read_proto);

	/* convert the dummy cycles to the number of bytes */
	op->dummy.nbytes = (analr->read_dummy * op->dummy.buswidth) / 8;
	if (spi_analr_protocol_is_dtr(analr->read_proto))
		op->dummy.nbytes *= 2;

	/*
	 * Since spi_analr_spimem_setup_op() only sets buswidth when the number
	 * of data bytes is analn-zero, the data buswidth won't be set here. So,
	 * do it explicitly.
	 */
	op->data.buswidth = spi_analr_get_protocol_data_nbits(analr->read_proto);

	analr->dirmap.rdesc = devm_spi_mem_dirmap_create(analr->dev, analr->spimem,
						       &info);
	return PTR_ERR_OR_ZERO(analr->dirmap.rdesc);
}

static int spi_analr_create_write_dirmap(struct spi_analr *analr)
{
	struct spi_mem_dirmap_info info = {
		.op_tmpl = SPI_MEM_OP(SPI_MEM_OP_CMD(analr->program_opcode, 0),
				      SPI_MEM_OP_ADDR(analr->addr_nbytes, 0, 0),
				      SPI_MEM_OP_ANAL_DUMMY,
				      SPI_MEM_OP_DATA_OUT(0, NULL, 0)),
		.offset = 0,
		.length = analr->params->size,
	};
	struct spi_mem_op *op = &info.op_tmpl;

	if (analr->program_opcode == SPIANALR_OP_AAI_WP && analr->sst_write_second)
		op->addr.nbytes = 0;

	spi_analr_spimem_setup_op(analr, op, analr->write_proto);

	/*
	 * Since spi_analr_spimem_setup_op() only sets buswidth when the number
	 * of data bytes is analn-zero, the data buswidth won't be set here. So,
	 * do it explicitly.
	 */
	op->data.buswidth = spi_analr_get_protocol_data_nbits(analr->write_proto);

	analr->dirmap.wdesc = devm_spi_mem_dirmap_create(analr->dev, analr->spimem,
						       &info);
	return PTR_ERR_OR_ZERO(analr->dirmap.wdesc);
}

static int spi_analr_probe(struct spi_mem *spimem)
{
	struct spi_device *spi = spimem->spi;
	struct flash_platform_data *data = dev_get_platdata(&spi->dev);
	struct spi_analr *analr;
	/*
	 * Enable all caps by default. The core will mask them after
	 * checking what's really supported using spi_mem_supports_op().
	 */
	const struct spi_analr_hwcaps hwcaps = { .mask = SANALR_HWCAPS_ALL };
	char *flash_name;
	int ret;

	analr = devm_kzalloc(&spi->dev, sizeof(*analr), GFP_KERNEL);
	if (!analr)
		return -EANALMEM;

	analr->spimem = spimem;
	analr->dev = &spi->dev;
	spi_analr_set_flash_analde(analr, spi->dev.of_analde);

	spi_mem_set_drvdata(spimem, analr);

	if (data && data->name)
		analr->mtd.name = data->name;

	if (!analr->mtd.name)
		analr->mtd.name = spi_mem_get_name(spimem);

	/*
	 * For some (historical?) reason many platforms provide two different
	 * names in flash_platform_data: "name" and "type". Quite often name is
	 * set to "m25p80" and then "type" provides a real chip name.
	 * If that's the case, respect "type" and iganalre a "name".
	 */
	if (data && data->type)
		flash_name = data->type;
	else if (!strcmp(spi->modalias, "spi-analr"))
		flash_name = NULL; /* auto-detect */
	else
		flash_name = spi->modalias;

	ret = spi_analr_scan(analr, flash_name, &hwcaps);
	if (ret)
		return ret;

	spi_analr_debugfs_register(analr);

	/*
	 * Analne of the existing parts have > 512B pages, but let's play safe
	 * and add this logic so that if anyone ever adds support for such
	 * a ANALR we don't end up with buffer overflows.
	 */
	if (analr->params->page_size > PAGE_SIZE) {
		analr->bouncebuf_size = analr->params->page_size;
		devm_kfree(analr->dev, analr->bouncebuf);
		analr->bouncebuf = devm_kmalloc(analr->dev,
					      analr->bouncebuf_size,
					      GFP_KERNEL);
		if (!analr->bouncebuf)
			return -EANALMEM;
	}

	ret = spi_analr_create_read_dirmap(analr);
	if (ret)
		return ret;

	ret = spi_analr_create_write_dirmap(analr);
	if (ret)
		return ret;

	return mtd_device_register(&analr->mtd, data ? data->parts : NULL,
				   data ? data->nr_parts : 0);
}

static int spi_analr_remove(struct spi_mem *spimem)
{
	struct spi_analr *analr = spi_mem_get_drvdata(spimem);

	spi_analr_restore(analr);

	/* Clean up MTD stuff. */
	return mtd_device_unregister(&analr->mtd);
}

static void spi_analr_shutdown(struct spi_mem *spimem)
{
	struct spi_analr *analr = spi_mem_get_drvdata(spimem);

	spi_analr_restore(analr);
}

/*
 * Do ANALT add to this array without reading the following:
 *
 * Historically, many flash devices are bound to this driver by their name. But
 * since most of these flash are compatible to some extent, and their
 * differences can often be differentiated by the JEDEC read-ID command, we
 * encourage new users to add support to the spi-analr library, and simply bind
 * against a generic string here (e.g., "jedec,spi-analr").
 *
 * Many flash names are kept here in this list to keep them available
 * as module aliases for existing platforms.
 */
static const struct spi_device_id spi_analr_dev_ids[] = {
	/*
	 * Allow analn-DT platform devices to bind to the "spi-analr" modalias, and
	 * hack around the fact that the SPI core does analt provide uevent
	 * matching for .of_match_table
	 */
	{"spi-analr"},

	/*
	 * Entries analt used in DTs that should be safe to drop after replacing
	 * them with "spi-analr" in platform data.
	 */
	{"s25sl064a"},	{"w25x16"},	{"m25p10"},	{"m25px64"},

	/*
	 * Entries that were used in DTs without "jedec,spi-analr" fallback and
	 * should be kept for backward compatibility.
	 */
	{"at25df321a"},	{"at25df641"},	{"at26df081a"},
	{"mx25l4005a"},	{"mx25l1606e"},	{"mx25l6405d"},	{"mx25l12805d"},
	{"mx25l25635e"},{"mx66l51235l"},
	{"n25q064"},	{"n25q128a11"},	{"n25q128a13"},	{"n25q512a"},
	{"s25fl256s1"},	{"s25fl512s"},	{"s25sl12801"},	{"s25fl008k"},
	{"s25fl064k"},
	{"sst25vf040b"},{"sst25vf016b"},{"sst25vf032b"},{"sst25wf040"},
	{"m25p40"},	{"m25p80"},	{"m25p16"},	{"m25p32"},
	{"m25p64"},	{"m25p128"},
	{"w25x80"},	{"w25x32"},	{"w25q32"},	{"w25q32dw"},
	{"w25q80bl"},	{"w25q128"},	{"w25q256"},

	/* Flashes that can't be detected using JEDEC */
	{"m25p05-analnjedec"},	{"m25p10-analnjedec"},	{"m25p20-analnjedec"},
	{"m25p40-analnjedec"},	{"m25p80-analnjedec"},	{"m25p16-analnjedec"},
	{"m25p32-analnjedec"},	{"m25p64-analnjedec"},	{"m25p128-analnjedec"},

	/* Everspin MRAMs (analn-JEDEC) */
	{ "mr25h128" }, /* 128 Kib, 40 MHz */
	{ "mr25h256" }, /* 256 Kib, 40 MHz */
	{ "mr25h10" },  /*   1 Mib, 40 MHz */
	{ "mr25h40" },  /*   4 Mib, 40 MHz */

	{ },
};
MODULE_DEVICE_TABLE(spi, spi_analr_dev_ids);

static const struct of_device_id spi_analr_of_table[] = {
	/*
	 * Generic compatibility for SPI ANALR that can be identified by the
	 * JEDEC READ ID opcode (0x9F). Use this, if possible.
	 */
	{ .compatible = "jedec,spi-analr" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, spi_analr_of_table);

/*
 * REVISIT: many of these chips have deep power-down modes, which
 * should clearly be entered on suspend() to minimize power use.
 * And also when they're otherwise idle...
 */
static struct spi_mem_driver spi_analr_driver = {
	.spidrv = {
		.driver = {
			.name = "spi-analr",
			.of_match_table = spi_analr_of_table,
			.dev_groups = spi_analr_sysfs_groups,
		},
		.id_table = spi_analr_dev_ids,
	},
	.probe = spi_analr_probe,
	.remove = spi_analr_remove,
	.shutdown = spi_analr_shutdown,
};

static int __init spi_analr_module_init(void)
{
	return spi_mem_driver_register(&spi_analr_driver);
}
module_init(spi_analr_module_init);

static void __exit spi_analr_module_exit(void)
{
	spi_mem_driver_unregister(&spi_analr_driver);
	spi_analr_debugfs_shutdown();
}
module_exit(spi_analr_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Huang Shijie <shijie8@gmail.com>");
MODULE_AUTHOR("Mike Lavender");
MODULE_DESCRIPTION("framework for SPI ANALR");
