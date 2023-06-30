// SPDX-License-Identifier: GPL-2.0
/*
 * Based on m25p80.c, by Mike Lavender (mike@steroidmicros.com), with
 * influence from lart.c (Abraham Van Der Merwe) and mtd_dataflash.c
 *
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-nor.h>
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
 * around for safety for now
 */
#define DEFAULT_READY_WAIT_JIFFIES		(40UL * HZ)

/*
 * For full-chip erase, calibrated to a 2MB flash (M25P16); should be scaled up
 * for larger flash
 */
#define CHIP_ERASE_2MB_READY_WAIT_JIFFIES	(40UL * HZ)

#define SPI_NOR_MAX_ADDR_NBYTES	4

#define SPI_NOR_SRST_SLEEP_MIN 200
#define SPI_NOR_SRST_SLEEP_MAX 400

/**
 * spi_nor_get_cmd_ext() - Get the command opcode extension based on the
 *			   extension type.
 * @nor:		pointer to a 'struct spi_nor'
 * @op:			pointer to the 'struct spi_mem_op' whose properties
 *			need to be initialized.
 *
 * Right now, only "repeat" and "invert" are supported.
 *
 * Return: The opcode extension.
 */
static u8 spi_nor_get_cmd_ext(const struct spi_nor *nor,
			      const struct spi_mem_op *op)
{
	switch (nor->cmd_ext_type) {
	case SPI_NOR_EXT_INVERT:
		return ~op->cmd.opcode;

	case SPI_NOR_EXT_REPEAT:
		return op->cmd.opcode;

	default:
		dev_err(nor->dev, "Unknown command extension type\n");
		return 0;
	}
}

/**
 * spi_nor_spimem_setup_op() - Set up common properties of a spi-mem op.
 * @nor:		pointer to a 'struct spi_nor'
 * @op:			pointer to the 'struct spi_mem_op' whose properties
 *			need to be initialized.
 * @proto:		the protocol from which the properties need to be set.
 */
void spi_nor_spimem_setup_op(const struct spi_nor *nor,
			     struct spi_mem_op *op,
			     const enum spi_nor_protocol proto)
{
	u8 ext;

	op->cmd.buswidth = spi_nor_get_protocol_inst_nbits(proto);

	if (op->addr.nbytes)
		op->addr.buswidth = spi_nor_get_protocol_addr_nbits(proto);

	if (op->dummy.nbytes)
		op->dummy.buswidth = spi_nor_get_protocol_addr_nbits(proto);

	if (op->data.nbytes)
		op->data.buswidth = spi_nor_get_protocol_data_nbits(proto);

	if (spi_nor_protocol_is_dtr(proto)) {
		/*
		 * SPIMEM supports mixed DTR modes, but right now we can only
		 * have all phases either DTR or STR. IOW, SPIMEM can have
		 * something like 4S-4D-4D, but SPI NOR can't. So, set all 4
		 * phases to either DTR or STR.
		 */
		op->cmd.dtr = true;
		op->addr.dtr = true;
		op->dummy.dtr = true;
		op->data.dtr = true;

		/* 2 bytes per clock cycle in DTR mode. */
		op->dummy.nbytes *= 2;

		ext = spi_nor_get_cmd_ext(nor, op);
		op->cmd.opcode = (op->cmd.opcode << 8) | ext;
		op->cmd.nbytes = 2;
	}
}

/**
 * spi_nor_spimem_bounce() - check if a bounce buffer is needed for the data
 *                           transfer
 * @nor:        pointer to 'struct spi_nor'
 * @op:         pointer to 'struct spi_mem_op' template for transfer
 *
 * If we have to use the bounce buffer, the data field in @op will be updated.
 *
 * Return: true if the bounce buffer is needed, false if not
 */
static bool spi_nor_spimem_bounce(struct spi_nor *nor, struct spi_mem_op *op)
{
	/* op->data.buf.in occupies the same memory as op->data.buf.out */
	if (object_is_on_stack(op->data.buf.in) ||
	    !virt_addr_valid(op->data.buf.in)) {
		if (op->data.nbytes > nor->bouncebuf_size)
			op->data.nbytes = nor->bouncebuf_size;
		op->data.buf.in = nor->bouncebuf;
		return true;
	}

	return false;
}

/**
 * spi_nor_spimem_exec_op() - execute a memory operation
 * @nor:        pointer to 'struct spi_nor'
 * @op:         pointer to 'struct spi_mem_op' template for transfer
 *
 * Return: 0 on success, -error otherwise.
 */
static int spi_nor_spimem_exec_op(struct spi_nor *nor, struct spi_mem_op *op)
{
	int error;

	error = spi_mem_adjust_op_size(nor->spimem, op);
	if (error)
		return error;

	return spi_mem_exec_op(nor->spimem, op);
}

int spi_nor_controller_ops_read_reg(struct spi_nor *nor, u8 opcode,
				    u8 *buf, size_t len)
{
	if (spi_nor_protocol_is_dtr(nor->reg_proto))
		return -EOPNOTSUPP;

	return nor->controller_ops->read_reg(nor, opcode, buf, len);
}

int spi_nor_controller_ops_write_reg(struct spi_nor *nor, u8 opcode,
				     const u8 *buf, size_t len)
{
	if (spi_nor_protocol_is_dtr(nor->reg_proto))
		return -EOPNOTSUPP;

	return nor->controller_ops->write_reg(nor, opcode, buf, len);
}

static int spi_nor_controller_ops_erase(struct spi_nor *nor, loff_t offs)
{
	if (spi_nor_protocol_is_dtr(nor->reg_proto))
		return -EOPNOTSUPP;

	return nor->controller_ops->erase(nor, offs);
}

/**
 * spi_nor_spimem_read_data() - read data from flash's memory region via
 *                              spi-mem
 * @nor:        pointer to 'struct spi_nor'
 * @from:       offset to read from
 * @len:        number of bytes to read
 * @buf:        pointer to dst buffer
 *
 * Return: number of bytes read successfully, -errno otherwise
 */
static ssize_t spi_nor_spimem_read_data(struct spi_nor *nor, loff_t from,
					size_t len, u8 *buf)
{
	struct spi_mem_op op =
		SPI_MEM_OP(SPI_MEM_OP_CMD(nor->read_opcode, 0),
			   SPI_MEM_OP_ADDR(nor->addr_nbytes, from, 0),
			   SPI_MEM_OP_DUMMY(nor->read_dummy, 0),
			   SPI_MEM_OP_DATA_IN(len, buf, 0));
	bool usebouncebuf;
	ssize_t nbytes;
	int error;

	spi_nor_spimem_setup_op(nor, &op, nor->read_proto);

	/* convert the dummy cycles to the number of bytes */
	op.dummy.nbytes = (nor->read_dummy * op.dummy.buswidth) / 8;
	if (spi_nor_protocol_is_dtr(nor->read_proto))
		op.dummy.nbytes *= 2;

	usebouncebuf = spi_nor_spimem_bounce(nor, &op);

	if (nor->dirmap.rdesc) {
		nbytes = spi_mem_dirmap_read(nor->dirmap.rdesc, op.addr.val,
					     op.data.nbytes, op.data.buf.in);
	} else {
		error = spi_nor_spimem_exec_op(nor, &op);
		if (error)
			return error;
		nbytes = op.data.nbytes;
	}

	if (usebouncebuf && nbytes > 0)
		memcpy(buf, op.data.buf.in, nbytes);

	return nbytes;
}

/**
 * spi_nor_read_data() - read data from flash memory
 * @nor:        pointer to 'struct spi_nor'
 * @from:       offset to read from
 * @len:        number of bytes to read
 * @buf:        pointer to dst buffer
 *
 * Return: number of bytes read successfully, -errno otherwise
 */
ssize_t spi_nor_read_data(struct spi_nor *nor, loff_t from, size_t len, u8 *buf)
{
	if (nor->spimem)
		return spi_nor_spimem_read_data(nor, from, len, buf);

	return nor->controller_ops->read(nor, from, len, buf);
}

/**
 * spi_nor_spimem_write_data() - write data to flash memory via
 *                               spi-mem
 * @nor:        pointer to 'struct spi_nor'
 * @to:         offset to write to
 * @len:        number of bytes to write
 * @buf:        pointer to src buffer
 *
 * Return: number of bytes written successfully, -errno otherwise
 */
static ssize_t spi_nor_spimem_write_data(struct spi_nor *nor, loff_t to,
					 size_t len, const u8 *buf)
{
	struct spi_mem_op op =
		SPI_MEM_OP(SPI_MEM_OP_CMD(nor->program_opcode, 0),
			   SPI_MEM_OP_ADDR(nor->addr_nbytes, to, 0),
			   SPI_MEM_OP_NO_DUMMY,
			   SPI_MEM_OP_DATA_OUT(len, buf, 0));
	ssize_t nbytes;
	int error;

	if (nor->program_opcode == SPINOR_OP_AAI_WP && nor->sst_write_second)
		op.addr.nbytes = 0;

	spi_nor_spimem_setup_op(nor, &op, nor->write_proto);

	if (spi_nor_spimem_bounce(nor, &op))
		memcpy(nor->bouncebuf, buf, op.data.nbytes);

	if (nor->dirmap.wdesc) {
		nbytes = spi_mem_dirmap_write(nor->dirmap.wdesc, op.addr.val,
					      op.data.nbytes, op.data.buf.out);
	} else {
		error = spi_nor_spimem_exec_op(nor, &op);
		if (error)
			return error;
		nbytes = op.data.nbytes;
	}

	return nbytes;
}

/**
 * spi_nor_write_data() - write data to flash memory
 * @nor:        pointer to 'struct spi_nor'
 * @to:         offset to write to
 * @len:        number of bytes to write
 * @buf:        pointer to src buffer
 *
 * Return: number of bytes written successfully, -errno otherwise
 */
ssize_t spi_nor_write_data(struct spi_nor *nor, loff_t to, size_t len,
			   const u8 *buf)
{
	if (nor->spimem)
		return spi_nor_spimem_write_data(nor, to, len, buf);

	return nor->controller_ops->write(nor, to, len, buf);
}

/**
 * spi_nor_read_any_reg() - read any register from flash memory, nonvolatile or
 * volatile.
 * @nor:        pointer to 'struct spi_nor'.
 * @op:		SPI memory operation. op->data.buf must be DMA-able.
 * @proto:	SPI protocol to use for the register operation.
 *
 * Return: zero on success, -errno otherwise
 */
int spi_nor_read_any_reg(struct spi_nor *nor, struct spi_mem_op *op,
			 enum spi_nor_protocol proto)
{
	if (!nor->spimem)
		return -EOPNOTSUPP;

	spi_nor_spimem_setup_op(nor, op, proto);
	return spi_nor_spimem_exec_op(nor, op);
}

/**
 * spi_nor_write_any_volatile_reg() - write any volatile register to flash
 * memory.
 * @nor:        pointer to 'struct spi_nor'
 * @op:		SPI memory operation. op->data.buf must be DMA-able.
 * @proto:	SPI protocol to use for the register operation.
 *
 * Writing volatile registers are instant according to some manufacturers
 * (Cypress, Micron) and do not need any status polling.
 *
 * Return: zero on success, -errno otherwise
 */
int spi_nor_write_any_volatile_reg(struct spi_nor *nor, struct spi_mem_op *op,
				   enum spi_nor_protocol proto)
{
	int ret;

	if (!nor->spimem)
		return -EOPNOTSUPP;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;
	spi_nor_spimem_setup_op(nor, op, proto);
	return spi_nor_spimem_exec_op(nor, op);
}

/**
 * spi_nor_write_enable() - Set write enable latch with Write Enable command.
 * @nor:	pointer to 'struct spi_nor'.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_write_enable(struct spi_nor *nor)
{
	int ret;

	if (nor->spimem) {
		struct spi_mem_op op = SPI_NOR_WREN_OP;

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_write_reg(nor, SPINOR_OP_WREN,
						       NULL, 0);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d on Write Enable\n", ret);

	return ret;
}

/**
 * spi_nor_write_disable() - Send Write Disable instruction to the chip.
 * @nor:	pointer to 'struct spi_nor'.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_write_disable(struct spi_nor *nor)
{
	int ret;

	if (nor->spimem) {
		struct spi_mem_op op = SPI_NOR_WRDI_OP;

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_write_reg(nor, SPINOR_OP_WRDI,
						       NULL, 0);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d on Write Disable\n", ret);

	return ret;
}

/**
 * spi_nor_read_id() - Read the JEDEC ID.
 * @nor:	pointer to 'struct spi_nor'.
 * @naddr:	number of address bytes to send. Can be zero if the operation
 *		does not need to send an address.
 * @ndummy:	number of dummy bytes to send after an opcode or address. Can
 *		be zero if the operation does not require dummy bytes.
 * @id:		pointer to a DMA-able buffer where the value of the JEDEC ID
 *		will be written.
 * @proto:	the SPI protocol for register operation.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_read_id(struct spi_nor *nor, u8 naddr, u8 ndummy, u8 *id,
		    enum spi_nor_protocol proto)
{
	int ret;

	if (nor->spimem) {
		struct spi_mem_op op =
			SPI_NOR_READID_OP(naddr, ndummy, id, SPI_NOR_MAX_ID_LEN);

		spi_nor_spimem_setup_op(nor, &op, proto);
		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = nor->controller_ops->read_reg(nor, SPINOR_OP_RDID, id,
						    SPI_NOR_MAX_ID_LEN);
	}
	return ret;
}

/**
 * spi_nor_read_sr() - Read the Status Register.
 * @nor:	pointer to 'struct spi_nor'.
 * @sr:		pointer to a DMA-able buffer where the value of the
 *              Status Register will be written. Should be at least 2 bytes.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_read_sr(struct spi_nor *nor, u8 *sr)
{
	int ret;

	if (nor->spimem) {
		struct spi_mem_op op = SPI_NOR_RDSR_OP(sr);

		if (nor->reg_proto == SNOR_PROTO_8_8_8_DTR) {
			op.addr.nbytes = nor->params->rdsr_addr_nbytes;
			op.dummy.nbytes = nor->params->rdsr_dummy;
			/*
			 * We don't want to read only one byte in DTR mode. So,
			 * read 2 and then discard the second byte.
			 */
			op.data.nbytes = 2;
		}

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_read_reg(nor, SPINOR_OP_RDSR, sr,
						      1);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d reading SR\n", ret);

	return ret;
}

/**
 * spi_nor_read_cr() - Read the Configuration Register using the
 * SPINOR_OP_RDCR (35h) command.
 * @nor:	pointer to 'struct spi_nor'
 * @cr:		pointer to a DMA-able buffer where the value of the
 *              Configuration Register will be written.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_read_cr(struct spi_nor *nor, u8 *cr)
{
	int ret;

	if (nor->spimem) {
		struct spi_mem_op op = SPI_NOR_RDCR_OP(cr);

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_read_reg(nor, SPINOR_OP_RDCR, cr,
						      1);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d reading CR\n", ret);

	return ret;
}

/**
 * spi_nor_set_4byte_addr_mode_en4b_ex4b() - Enter/Exit 4-byte address mode
 *			using SPINOR_OP_EN4B/SPINOR_OP_EX4B. Typically used by
 *			Winbond and Macronix.
 * @nor:	pointer to 'struct spi_nor'.
 * @enable:	true to enter the 4-byte address mode, false to exit the 4-byte
 *		address mode.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_set_4byte_addr_mode_en4b_ex4b(struct spi_nor *nor, bool enable)
{
	int ret;

	if (nor->spimem) {
		struct spi_mem_op op = SPI_NOR_EN4B_EX4B_OP(enable);

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_write_reg(nor,
						       enable ? SPINOR_OP_EN4B :
								SPINOR_OP_EX4B,
						       NULL, 0);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d setting 4-byte mode\n", ret);

	return ret;
}

/**
 * spi_nor_set_4byte_addr_mode_wren_en4b_ex4b() - Set 4-byte address mode using
 * SPINOR_OP_WREN followed by SPINOR_OP_EN4B or SPINOR_OP_EX4B. Typically used
 * by ST and Micron flashes.
 * @nor:	pointer to 'struct spi_nor'.
 * @enable:	true to enter the 4-byte address mode, false to exit the 4-byte
 *		address mode.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_set_4byte_addr_mode_wren_en4b_ex4b(struct spi_nor *nor, bool enable)
{
	int ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	ret = spi_nor_set_4byte_addr_mode_en4b_ex4b(nor, enable);
	if (ret)
		return ret;

	return spi_nor_write_disable(nor);
}

/**
 * spi_nor_set_4byte_addr_mode_brwr() - Set 4-byte address mode using
 *			SPINOR_OP_BRWR. Typically used by Spansion flashes.
 * @nor:	pointer to 'struct spi_nor'.
 * @enable:	true to enter the 4-byte address mode, false to exit the 4-byte
 *		address mode.
 *
 * 8-bit volatile bank register used to define A[30:A24] bits. MSB (bit[7]) is
 * used to enable/disable 4-byte address mode. When MSB is set to ‘1’, 4-byte
 * address mode is active and A[30:24] bits are don’t care. Write instruction is
 * SPINOR_OP_BRWR(17h) with 1 byte of data.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_set_4byte_addr_mode_brwr(struct spi_nor *nor, bool enable)
{
	int ret;

	nor->bouncebuf[0] = enable << 7;

	if (nor->spimem) {
		struct spi_mem_op op = SPI_NOR_BRWR_OP(nor->bouncebuf);

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_write_reg(nor, SPINOR_OP_BRWR,
						       nor->bouncebuf, 1);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d setting 4-byte mode\n", ret);

	return ret;
}

/**
 * spi_nor_sr_ready() - Query the Status Register to see if the flash is ready
 * for new commands.
 * @nor:	pointer to 'struct spi_nor'.
 *
 * Return: 1 if ready, 0 if not ready, -errno on errors.
 */
int spi_nor_sr_ready(struct spi_nor *nor)
{
	int ret;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	return !(nor->bouncebuf[0] & SR_WIP);
}

/**
 * spi_nor_use_parallel_locking() - Checks if RWW locking scheme shall be used
 * @nor:	pointer to 'struct spi_nor'.
 *
 * Return: true if parallel locking is enabled, false otherwise.
 */
static bool spi_nor_use_parallel_locking(struct spi_nor *nor)
{
	return nor->flags & SNOR_F_RWW;
}

/* Locking helpers for status read operations */
static int spi_nor_rww_start_rdst(struct spi_nor *nor)
{
	struct spi_nor_rww *rww = &nor->rww;
	int ret = -EAGAIN;

	mutex_lock(&nor->lock);

	if (rww->ongoing_io || rww->ongoing_rd)
		goto busy;

	rww->ongoing_io = true;
	rww->ongoing_rd = true;
	ret = 0;

busy:
	mutex_unlock(&nor->lock);
	return ret;
}

static void spi_nor_rww_end_rdst(struct spi_nor *nor)
{
	struct spi_nor_rww *rww = &nor->rww;

	mutex_lock(&nor->lock);

	rww->ongoing_io = false;
	rww->ongoing_rd = false;

	mutex_unlock(&nor->lock);
}

static int spi_nor_lock_rdst(struct spi_nor *nor)
{
	if (spi_nor_use_parallel_locking(nor))
		return spi_nor_rww_start_rdst(nor);

	return 0;
}

static void spi_nor_unlock_rdst(struct spi_nor *nor)
{
	if (spi_nor_use_parallel_locking(nor)) {
		spi_nor_rww_end_rdst(nor);
		wake_up(&nor->rww.wait);
	}
}

/**
 * spi_nor_ready() - Query the flash to see if it is ready for new commands.
 * @nor:	pointer to 'struct spi_nor'.
 *
 * Return: 1 if ready, 0 if not ready, -errno on errors.
 */
static int spi_nor_ready(struct spi_nor *nor)
{
	int ret;

	ret = spi_nor_lock_rdst(nor);
	if (ret)
		return 0;

	/* Flashes might override the standard routine. */
	if (nor->params->ready)
		ret = nor->params->ready(nor);
	else
		ret = spi_nor_sr_ready(nor);

	spi_nor_unlock_rdst(nor);

	return ret;
}

/**
 * spi_nor_wait_till_ready_with_timeout() - Service routine to read the
 * Status Register until ready, or timeout occurs.
 * @nor:		pointer to "struct spi_nor".
 * @timeout_jiffies:	jiffies to wait until timeout.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_wait_till_ready_with_timeout(struct spi_nor *nor,
						unsigned long timeout_jiffies)
{
	unsigned long deadline;
	int timeout = 0, ret;

	deadline = jiffies + timeout_jiffies;

	while (!timeout) {
		if (time_after_eq(jiffies, deadline))
			timeout = 1;

		ret = spi_nor_ready(nor);
		if (ret < 0)
			return ret;
		if (ret)
			return 0;

		cond_resched();
	}

	dev_dbg(nor->dev, "flash operation timed out\n");

	return -ETIMEDOUT;
}

/**
 * spi_nor_wait_till_ready() - Wait for a predefined amount of time for the
 * flash to be ready, or timeout occurs.
 * @nor:	pointer to "struct spi_nor".
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_wait_till_ready(struct spi_nor *nor)
{
	return spi_nor_wait_till_ready_with_timeout(nor,
						    DEFAULT_READY_WAIT_JIFFIES);
}

/**
 * spi_nor_global_block_unlock() - Unlock Global Block Protection.
 * @nor:	pointer to 'struct spi_nor'.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_global_block_unlock(struct spi_nor *nor)
{
	int ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	if (nor->spimem) {
		struct spi_mem_op op = SPI_NOR_GBULK_OP;

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_write_reg(nor, SPINOR_OP_GBULK,
						       NULL, 0);
	}

	if (ret) {
		dev_dbg(nor->dev, "error %d on Global Block Unlock\n", ret);
		return ret;
	}

	return spi_nor_wait_till_ready(nor);
}

/**
 * spi_nor_write_sr() - Write the Status Register.
 * @nor:	pointer to 'struct spi_nor'.
 * @sr:		pointer to DMA-able buffer to write to the Status Register.
 * @len:	number of bytes to write to the Status Register.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_write_sr(struct spi_nor *nor, const u8 *sr, size_t len)
{
	int ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	if (nor->spimem) {
		struct spi_mem_op op = SPI_NOR_WRSR_OP(sr, len);

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_write_reg(nor, SPINOR_OP_WRSR, sr,
						       len);
	}

	if (ret) {
		dev_dbg(nor->dev, "error %d writing SR\n", ret);
		return ret;
	}

	return spi_nor_wait_till_ready(nor);
}

/**
 * spi_nor_write_sr1_and_check() - Write one byte to the Status Register 1 and
 * ensure that the byte written match the received value.
 * @nor:	pointer to a 'struct spi_nor'.
 * @sr1:	byte value to be written to the Status Register.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_write_sr1_and_check(struct spi_nor *nor, u8 sr1)
{
	int ret;

	nor->bouncebuf[0] = sr1;

	ret = spi_nor_write_sr(nor, nor->bouncebuf, 1);
	if (ret)
		return ret;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	if (nor->bouncebuf[0] != sr1) {
		dev_dbg(nor->dev, "SR1: read back test failed\n");
		return -EIO;
	}

	return 0;
}

/**
 * spi_nor_write_16bit_sr_and_check() - Write the Status Register 1 and the
 * Status Register 2 in one shot. Ensure that the byte written in the Status
 * Register 1 match the received value, and that the 16-bit Write did not
 * affect what was already in the Status Register 2.
 * @nor:	pointer to a 'struct spi_nor'.
 * @sr1:	byte value to be written to the Status Register 1.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_write_16bit_sr_and_check(struct spi_nor *nor, u8 sr1)
{
	int ret;
	u8 *sr_cr = nor->bouncebuf;
	u8 cr_written;

	/* Make sure we don't overwrite the contents of Status Register 2. */
	if (!(nor->flags & SNOR_F_NO_READ_CR)) {
		ret = spi_nor_read_cr(nor, &sr_cr[1]);
		if (ret)
			return ret;
	} else if (nor->params->quad_enable) {
		/*
		 * If the Status Register 2 Read command (35h) is not
		 * supported, we should at least be sure we don't
		 * change the value of the SR2 Quad Enable bit.
		 *
		 * We can safely assume that when the Quad Enable method is
		 * set, the value of the QE bit is one, as a consequence of the
		 * nor->params->quad_enable() call.
		 *
		 * We can safely assume that the Quad Enable bit is present in
		 * the Status Register 2 at BIT(1). According to the JESD216
		 * revB standard, BFPT DWORDS[15], bits 22:20, the 16-bit
		 * Write Status (01h) command is available just for the cases
		 * in which the QE bit is described in SR2 at BIT(1).
		 */
		sr_cr[1] = SR2_QUAD_EN_BIT1;
	} else {
		sr_cr[1] = 0;
	}

	sr_cr[0] = sr1;

	ret = spi_nor_write_sr(nor, sr_cr, 2);
	if (ret)
		return ret;

	ret = spi_nor_read_sr(nor, sr_cr);
	if (ret)
		return ret;

	if (sr1 != sr_cr[0]) {
		dev_dbg(nor->dev, "SR: Read back test failed\n");
		return -EIO;
	}

	if (nor->flags & SNOR_F_NO_READ_CR)
		return 0;

	cr_written = sr_cr[1];

	ret = spi_nor_read_cr(nor, &sr_cr[1]);
	if (ret)
		return ret;

	if (cr_written != sr_cr[1]) {
		dev_dbg(nor->dev, "CR: read back test failed\n");
		return -EIO;
	}

	return 0;
}

/**
 * spi_nor_write_16bit_cr_and_check() - Write the Status Register 1 and the
 * Configuration Register in one shot. Ensure that the byte written in the
 * Configuration Register match the received value, and that the 16-bit Write
 * did not affect what was already in the Status Register 1.
 * @nor:	pointer to a 'struct spi_nor'.
 * @cr:		byte value to be written to the Configuration Register.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_write_16bit_cr_and_check(struct spi_nor *nor, u8 cr)
{
	int ret;
	u8 *sr_cr = nor->bouncebuf;
	u8 sr_written;

	/* Keep the current value of the Status Register 1. */
	ret = spi_nor_read_sr(nor, sr_cr);
	if (ret)
		return ret;

	sr_cr[1] = cr;

	ret = spi_nor_write_sr(nor, sr_cr, 2);
	if (ret)
		return ret;

	sr_written = sr_cr[0];

	ret = spi_nor_read_sr(nor, sr_cr);
	if (ret)
		return ret;

	if (sr_written != sr_cr[0]) {
		dev_dbg(nor->dev, "SR: Read back test failed\n");
		return -EIO;
	}

	if (nor->flags & SNOR_F_NO_READ_CR)
		return 0;

	ret = spi_nor_read_cr(nor, &sr_cr[1]);
	if (ret)
		return ret;

	if (cr != sr_cr[1]) {
		dev_dbg(nor->dev, "CR: read back test failed\n");
		return -EIO;
	}

	return 0;
}

/**
 * spi_nor_write_sr_and_check() - Write the Status Register 1 and ensure that
 * the byte written match the received value without affecting other bits in the
 * Status Register 1 and 2.
 * @nor:	pointer to a 'struct spi_nor'.
 * @sr1:	byte value to be written to the Status Register.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_write_sr_and_check(struct spi_nor *nor, u8 sr1)
{
	if (nor->flags & SNOR_F_HAS_16BIT_SR)
		return spi_nor_write_16bit_sr_and_check(nor, sr1);

	return spi_nor_write_sr1_and_check(nor, sr1);
}

/**
 * spi_nor_write_sr2() - Write the Status Register 2 using the
 * SPINOR_OP_WRSR2 (3eh) command.
 * @nor:	pointer to 'struct spi_nor'.
 * @sr2:	pointer to DMA-able buffer to write to the Status Register 2.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_write_sr2(struct spi_nor *nor, const u8 *sr2)
{
	int ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	if (nor->spimem) {
		struct spi_mem_op op = SPI_NOR_WRSR2_OP(sr2);

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_write_reg(nor, SPINOR_OP_WRSR2,
						       sr2, 1);
	}

	if (ret) {
		dev_dbg(nor->dev, "error %d writing SR2\n", ret);
		return ret;
	}

	return spi_nor_wait_till_ready(nor);
}

/**
 * spi_nor_read_sr2() - Read the Status Register 2 using the
 * SPINOR_OP_RDSR2 (3fh) command.
 * @nor:	pointer to 'struct spi_nor'.
 * @sr2:	pointer to DMA-able buffer where the value of the
 *		Status Register 2 will be written.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_read_sr2(struct spi_nor *nor, u8 *sr2)
{
	int ret;

	if (nor->spimem) {
		struct spi_mem_op op = SPI_NOR_RDSR2_OP(sr2);

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_read_reg(nor, SPINOR_OP_RDSR2, sr2,
						      1);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d reading SR2\n", ret);

	return ret;
}

/**
 * spi_nor_erase_chip() - Erase the entire flash memory.
 * @nor:	pointer to 'struct spi_nor'.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_erase_chip(struct spi_nor *nor)
{
	int ret;

	dev_dbg(nor->dev, " %lldKiB\n", (long long)(nor->mtd.size >> 10));

	if (nor->spimem) {
		struct spi_mem_op op = SPI_NOR_CHIP_ERASE_OP;

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = spi_nor_controller_ops_write_reg(nor,
						       SPINOR_OP_CHIP_ERASE,
						       NULL, 0);
	}

	if (ret)
		dev_dbg(nor->dev, "error %d erasing chip\n", ret);

	return ret;
}

static u8 spi_nor_convert_opcode(u8 opcode, const u8 table[][2], size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		if (table[i][0] == opcode)
			return table[i][1];

	/* No conversion found, keep input op code. */
	return opcode;
}

u8 spi_nor_convert_3to4_read(u8 opcode)
{
	static const u8 spi_nor_3to4_read[][2] = {
		{ SPINOR_OP_READ,	SPINOR_OP_READ_4B },
		{ SPINOR_OP_READ_FAST,	SPINOR_OP_READ_FAST_4B },
		{ SPINOR_OP_READ_1_1_2,	SPINOR_OP_READ_1_1_2_4B },
		{ SPINOR_OP_READ_1_2_2,	SPINOR_OP_READ_1_2_2_4B },
		{ SPINOR_OP_READ_1_1_4,	SPINOR_OP_READ_1_1_4_4B },
		{ SPINOR_OP_READ_1_4_4,	SPINOR_OP_READ_1_4_4_4B },
		{ SPINOR_OP_READ_1_1_8,	SPINOR_OP_READ_1_1_8_4B },
		{ SPINOR_OP_READ_1_8_8,	SPINOR_OP_READ_1_8_8_4B },

		{ SPINOR_OP_READ_1_1_1_DTR,	SPINOR_OP_READ_1_1_1_DTR_4B },
		{ SPINOR_OP_READ_1_2_2_DTR,	SPINOR_OP_READ_1_2_2_DTR_4B },
		{ SPINOR_OP_READ_1_4_4_DTR,	SPINOR_OP_READ_1_4_4_DTR_4B },
	};

	return spi_nor_convert_opcode(opcode, spi_nor_3to4_read,
				      ARRAY_SIZE(spi_nor_3to4_read));
}

static u8 spi_nor_convert_3to4_program(u8 opcode)
{
	static const u8 spi_nor_3to4_program[][2] = {
		{ SPINOR_OP_PP,		SPINOR_OP_PP_4B },
		{ SPINOR_OP_PP_1_1_4,	SPINOR_OP_PP_1_1_4_4B },
		{ SPINOR_OP_PP_1_4_4,	SPINOR_OP_PP_1_4_4_4B },
		{ SPINOR_OP_PP_1_1_8,	SPINOR_OP_PP_1_1_8_4B },
		{ SPINOR_OP_PP_1_8_8,	SPINOR_OP_PP_1_8_8_4B },
	};

	return spi_nor_convert_opcode(opcode, spi_nor_3to4_program,
				      ARRAY_SIZE(spi_nor_3to4_program));
}

static u8 spi_nor_convert_3to4_erase(u8 opcode)
{
	static const u8 spi_nor_3to4_erase[][2] = {
		{ SPINOR_OP_BE_4K,	SPINOR_OP_BE_4K_4B },
		{ SPINOR_OP_BE_32K,	SPINOR_OP_BE_32K_4B },
		{ SPINOR_OP_SE,		SPINOR_OP_SE_4B },
	};

	return spi_nor_convert_opcode(opcode, spi_nor_3to4_erase,
				      ARRAY_SIZE(spi_nor_3to4_erase));
}

static bool spi_nor_has_uniform_erase(const struct spi_nor *nor)
{
	return !!nor->params->erase_map.uniform_erase_type;
}

static void spi_nor_set_4byte_opcodes(struct spi_nor *nor)
{
	nor->read_opcode = spi_nor_convert_3to4_read(nor->read_opcode);
	nor->program_opcode = spi_nor_convert_3to4_program(nor->program_opcode);
	nor->erase_opcode = spi_nor_convert_3to4_erase(nor->erase_opcode);

	if (!spi_nor_has_uniform_erase(nor)) {
		struct spi_nor_erase_map *map = &nor->params->erase_map;
		struct spi_nor_erase_type *erase;
		int i;

		for (i = 0; i < SNOR_ERASE_TYPE_MAX; i++) {
			erase = &map->erase_type[i];
			erase->opcode =
				spi_nor_convert_3to4_erase(erase->opcode);
		}
	}
}

static int spi_nor_prep(struct spi_nor *nor)
{
	int ret = 0;

	if (nor->controller_ops && nor->controller_ops->prepare)
		ret = nor->controller_ops->prepare(nor);

	return ret;
}

static void spi_nor_unprep(struct spi_nor *nor)
{
	if (nor->controller_ops && nor->controller_ops->unprepare)
		nor->controller_ops->unprepare(nor);
}

static void spi_nor_offset_to_banks(u64 bank_size, loff_t start, size_t len,
				    u8 *first, u8 *last)
{
	/* This is currently safe, the number of banks being very small */
	*first = DIV_ROUND_DOWN_ULL(start, bank_size);
	*last = DIV_ROUND_DOWN_ULL(start + len - 1, bank_size);
}

/* Generic helpers for internal locking and serialization */
static bool spi_nor_rww_start_io(struct spi_nor *nor)
{
	struct spi_nor_rww *rww = &nor->rww;
	bool start = false;

	mutex_lock(&nor->lock);

	if (rww->ongoing_io)
		goto busy;

	rww->ongoing_io = true;
	start = true;

busy:
	mutex_unlock(&nor->lock);
	return start;
}

static void spi_nor_rww_end_io(struct spi_nor *nor)
{
	mutex_lock(&nor->lock);
	nor->rww.ongoing_io = false;
	mutex_unlock(&nor->lock);
}

static int spi_nor_lock_device(struct spi_nor *nor)
{
	if (!spi_nor_use_parallel_locking(nor))
		return 0;

	return wait_event_killable(nor->rww.wait, spi_nor_rww_start_io(nor));
}

static void spi_nor_unlock_device(struct spi_nor *nor)
{
	if (spi_nor_use_parallel_locking(nor)) {
		spi_nor_rww_end_io(nor);
		wake_up(&nor->rww.wait);
	}
}

/* Generic helpers for internal locking and serialization */
static bool spi_nor_rww_start_exclusive(struct spi_nor *nor)
{
	struct spi_nor_rww *rww = &nor->rww;
	bool start = false;

	mutex_lock(&nor->lock);

	if (rww->ongoing_io || rww->ongoing_rd || rww->ongoing_pe)
		goto busy;

	rww->ongoing_io = true;
	rww->ongoing_rd = true;
	rww->ongoing_pe = true;
	start = true;

busy:
	mutex_unlock(&nor->lock);
	return start;
}

static void spi_nor_rww_end_exclusive(struct spi_nor *nor)
{
	struct spi_nor_rww *rww = &nor->rww;

	mutex_lock(&nor->lock);
	rww->ongoing_io = false;
	rww->ongoing_rd = false;
	rww->ongoing_pe = false;
	mutex_unlock(&nor->lock);
}

int spi_nor_prep_and_lock(struct spi_nor *nor)
{
	int ret;

	ret = spi_nor_prep(nor);
	if (ret)
		return ret;

	if (!spi_nor_use_parallel_locking(nor))
		mutex_lock(&nor->lock);
	else
		ret = wait_event_killable(nor->rww.wait,
					  spi_nor_rww_start_exclusive(nor));

	return ret;
}

void spi_nor_unlock_and_unprep(struct spi_nor *nor)
{
	if (!spi_nor_use_parallel_locking(nor)) {
		mutex_unlock(&nor->lock);
	} else {
		spi_nor_rww_end_exclusive(nor);
		wake_up(&nor->rww.wait);
	}

	spi_nor_unprep(nor);
}

/* Internal locking helpers for program and erase operations */
static bool spi_nor_rww_start_pe(struct spi_nor *nor, loff_t start, size_t len)
{
	struct spi_nor_rww *rww = &nor->rww;
	unsigned int used_banks = 0;
	bool started = false;
	u8 first, last;
	int bank;

	mutex_lock(&nor->lock);

	if (rww->ongoing_io || rww->ongoing_rd || rww->ongoing_pe)
		goto busy;

	spi_nor_offset_to_banks(nor->params->bank_size, start, len, &first, &last);
	for (bank = first; bank <= last; bank++) {
		if (rww->used_banks & BIT(bank))
			goto busy;

		used_banks |= BIT(bank);
	}

	rww->used_banks |= used_banks;
	rww->ongoing_pe = true;
	started = true;

busy:
	mutex_unlock(&nor->lock);
	return started;
}

static void spi_nor_rww_end_pe(struct spi_nor *nor, loff_t start, size_t len)
{
	struct spi_nor_rww *rww = &nor->rww;
	u8 first, last;
	int bank;

	mutex_lock(&nor->lock);

	spi_nor_offset_to_banks(nor->params->bank_size, start, len, &first, &last);
	for (bank = first; bank <= last; bank++)
		rww->used_banks &= ~BIT(bank);

	rww->ongoing_pe = false;

	mutex_unlock(&nor->lock);
}

static int spi_nor_prep_and_lock_pe(struct spi_nor *nor, loff_t start, size_t len)
{
	int ret;

	ret = spi_nor_prep(nor);
	if (ret)
		return ret;

	if (!spi_nor_use_parallel_locking(nor))
		mutex_lock(&nor->lock);
	else
		ret = wait_event_killable(nor->rww.wait,
					  spi_nor_rww_start_pe(nor, start, len));

	return ret;
}

static void spi_nor_unlock_and_unprep_pe(struct spi_nor *nor, loff_t start, size_t len)
{
	if (!spi_nor_use_parallel_locking(nor)) {
		mutex_unlock(&nor->lock);
	} else {
		spi_nor_rww_end_pe(nor, start, len);
		wake_up(&nor->rww.wait);
	}

	spi_nor_unprep(nor);
}

/* Internal locking helpers for read operations */
static bool spi_nor_rww_start_rd(struct spi_nor *nor, loff_t start, size_t len)
{
	struct spi_nor_rww *rww = &nor->rww;
	unsigned int used_banks = 0;
	bool started = false;
	u8 first, last;
	int bank;

	mutex_lock(&nor->lock);

	if (rww->ongoing_io || rww->ongoing_rd)
		goto busy;

	spi_nor_offset_to_banks(nor->params->bank_size, start, len, &first, &last);
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
	mutex_unlock(&nor->lock);
	return started;
}

static void spi_nor_rww_end_rd(struct spi_nor *nor, loff_t start, size_t len)
{
	struct spi_nor_rww *rww = &nor->rww;
	u8 first, last;
	int bank;

	mutex_lock(&nor->lock);

	spi_nor_offset_to_banks(nor->params->bank_size, start, len, &first, &last);
	for (bank = first; bank <= last; bank++)
		nor->rww.used_banks &= ~BIT(bank);

	rww->ongoing_io = false;
	rww->ongoing_rd = false;

	mutex_unlock(&nor->lock);
}

static int spi_nor_prep_and_lock_rd(struct spi_nor *nor, loff_t start, size_t len)
{
	int ret;

	ret = spi_nor_prep(nor);
	if (ret)
		return ret;

	if (!spi_nor_use_parallel_locking(nor))
		mutex_lock(&nor->lock);
	else
		ret = wait_event_killable(nor->rww.wait,
					  spi_nor_rww_start_rd(nor, start, len));

	return ret;
}

static void spi_nor_unlock_and_unprep_rd(struct spi_nor *nor, loff_t start, size_t len)
{
	if (!spi_nor_use_parallel_locking(nor)) {
		mutex_unlock(&nor->lock);
	} else {
		spi_nor_rww_end_rd(nor, start, len);
		wake_up(&nor->rww.wait);
	}

	spi_nor_unprep(nor);
}

static u32 spi_nor_convert_addr(struct spi_nor *nor, loff_t addr)
{
	if (!nor->params->convert_addr)
		return addr;

	return nor->params->convert_addr(nor, addr);
}

/*
 * Initiate the erasure of a single sector
 */
int spi_nor_erase_sector(struct spi_nor *nor, u32 addr)
{
	int i;

	addr = spi_nor_convert_addr(nor, addr);

	if (nor->spimem) {
		struct spi_mem_op op =
			SPI_NOR_SECTOR_ERASE_OP(nor->erase_opcode,
						nor->addr_nbytes, addr);

		spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

		return spi_mem_exec_op(nor->spimem, &op);
	} else if (nor->controller_ops->erase) {
		return spi_nor_controller_ops_erase(nor, addr);
	}

	/*
	 * Default implementation, if driver doesn't have a specialized HW
	 * control
	 */
	for (i = nor->addr_nbytes - 1; i >= 0; i--) {
		nor->bouncebuf[i] = addr & 0xff;
		addr >>= 8;
	}

	return spi_nor_controller_ops_write_reg(nor, nor->erase_opcode,
						nor->bouncebuf, nor->addr_nbytes);
}

/**
 * spi_nor_div_by_erase_size() - calculate remainder and update new dividend
 * @erase:	pointer to a structure that describes a SPI NOR erase type
 * @dividend:	dividend value
 * @remainder:	pointer to u32 remainder (will be updated)
 *
 * Return: the result of the division
 */
static u64 spi_nor_div_by_erase_size(const struct spi_nor_erase_type *erase,
				     u64 dividend, u32 *remainder)
{
	/* JEDEC JESD216B Standard imposes erase sizes to be power of 2. */
	*remainder = (u32)dividend & erase->size_mask;
	return dividend >> erase->size_shift;
}

/**
 * spi_nor_find_best_erase_type() - find the best erase type for the given
 *				    offset in the serial flash memory and the
 *				    number of bytes to erase. The region in
 *				    which the address fits is expected to be
 *				    provided.
 * @map:	the erase map of the SPI NOR
 * @region:	pointer to a structure that describes a SPI NOR erase region
 * @addr:	offset in the serial flash memory
 * @len:	number of bytes to erase
 *
 * Return: a pointer to the best fitted erase type, NULL otherwise.
 */
static const struct spi_nor_erase_type *
spi_nor_find_best_erase_type(const struct spi_nor_erase_map *map,
			     const struct spi_nor_erase_region *region,
			     u64 addr, u32 len)
{
	const struct spi_nor_erase_type *erase;
	u32 rem;
	int i;
	u8 erase_mask = region->offset & SNOR_ERASE_TYPE_MASK;

	/*
	 * Erase types are ordered by size, with the smallest erase type at
	 * index 0.
	 */
	for (i = SNOR_ERASE_TYPE_MAX - 1; i >= 0; i--) {
		/* Does the erase region support the tested erase type? */
		if (!(erase_mask & BIT(i)))
			continue;

		erase = &map->erase_type[i];
		if (!erase->size)
			continue;

		/* Alignment is not mandatory for overlaid regions */
		if (region->offset & SNOR_OVERLAID_REGION &&
		    region->size <= len)
			return erase;

		/* Don't erase more than what the user has asked for. */
		if (erase->size > len)
			continue;

		spi_nor_div_by_erase_size(erase, addr, &rem);
		if (!rem)
			return erase;
	}

	return NULL;
}

static u64 spi_nor_region_is_last(const struct spi_nor_erase_region *region)
{
	return region->offset & SNOR_LAST_REGION;
}

static u64 spi_nor_region_end(const struct spi_nor_erase_region *region)
{
	return (region->offset & ~SNOR_ERASE_FLAGS_MASK) + region->size;
}

/**
 * spi_nor_region_next() - get the next spi nor region
 * @region:	pointer to a structure that describes a SPI NOR erase region
 *
 * Return: the next spi nor region or NULL if last region.
 */
struct spi_nor_erase_region *
spi_nor_region_next(struct spi_nor_erase_region *region)
{
	if (spi_nor_region_is_last(region))
		return NULL;
	region++;
	return region;
}

/**
 * spi_nor_find_erase_region() - find the region of the serial flash memory in
 *				 which the offset fits
 * @map:	the erase map of the SPI NOR
 * @addr:	offset in the serial flash memory
 *
 * Return: a pointer to the spi_nor_erase_region struct, ERR_PTR(-errno)
 *	   otherwise.
 */
static struct spi_nor_erase_region *
spi_nor_find_erase_region(const struct spi_nor_erase_map *map, u64 addr)
{
	struct spi_nor_erase_region *region = map->regions;
	u64 region_start = region->offset & ~SNOR_ERASE_FLAGS_MASK;
	u64 region_end = region_start + region->size;

	while (addr < region_start || addr >= region_end) {
		region = spi_nor_region_next(region);
		if (!region)
			return ERR_PTR(-EINVAL);

		region_start = region->offset & ~SNOR_ERASE_FLAGS_MASK;
		region_end = region_start + region->size;
	}

	return region;
}

/**
 * spi_nor_init_erase_cmd() - initialize an erase command
 * @region:	pointer to a structure that describes a SPI NOR erase region
 * @erase:	pointer to a structure that describes a SPI NOR erase type
 *
 * Return: the pointer to the allocated erase command, ERR_PTR(-errno)
 *	   otherwise.
 */
static struct spi_nor_erase_command *
spi_nor_init_erase_cmd(const struct spi_nor_erase_region *region,
		       const struct spi_nor_erase_type *erase)
{
	struct spi_nor_erase_command *cmd;

	cmd = kmalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&cmd->list);
	cmd->opcode = erase->opcode;
	cmd->count = 1;

	if (region->offset & SNOR_OVERLAID_REGION)
		cmd->size = region->size;
	else
		cmd->size = erase->size;

	return cmd;
}

/**
 * spi_nor_destroy_erase_cmd_list() - destroy erase command list
 * @erase_list:	list of erase commands
 */
static void spi_nor_destroy_erase_cmd_list(struct list_head *erase_list)
{
	struct spi_nor_erase_command *cmd, *next;

	list_for_each_entry_safe(cmd, next, erase_list, list) {
		list_del(&cmd->list);
		kfree(cmd);
	}
}

/**
 * spi_nor_init_erase_cmd_list() - initialize erase command list
 * @nor:	pointer to a 'struct spi_nor'
 * @erase_list:	list of erase commands to be executed once we validate that the
 *		erase can be performed
 * @addr:	offset in the serial flash memory
 * @len:	number of bytes to erase
 *
 * Builds the list of best fitted erase commands and verifies if the erase can
 * be performed.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_init_erase_cmd_list(struct spi_nor *nor,
				       struct list_head *erase_list,
				       u64 addr, u32 len)
{
	const struct spi_nor_erase_map *map = &nor->params->erase_map;
	const struct spi_nor_erase_type *erase, *prev_erase = NULL;
	struct spi_nor_erase_region *region;
	struct spi_nor_erase_command *cmd = NULL;
	u64 region_end;
	int ret = -EINVAL;

	region = spi_nor_find_erase_region(map, addr);
	if (IS_ERR(region))
		return PTR_ERR(region);

	region_end = spi_nor_region_end(region);

	while (len) {
		erase = spi_nor_find_best_erase_type(map, region, addr, len);
		if (!erase)
			goto destroy_erase_cmd_list;

		if (prev_erase != erase ||
		    erase->size != cmd->size ||
		    region->offset & SNOR_OVERLAID_REGION) {
			cmd = spi_nor_init_erase_cmd(region, erase);
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
			region = spi_nor_region_next(region);
			if (!region)
				goto destroy_erase_cmd_list;
			region_end = spi_nor_region_end(region);
		}

		prev_erase = erase;
	}

	return 0;

destroy_erase_cmd_list:
	spi_nor_destroy_erase_cmd_list(erase_list);
	return ret;
}

/**
 * spi_nor_erase_multi_sectors() - perform a non-uniform erase
 * @nor:	pointer to a 'struct spi_nor'
 * @addr:	offset in the serial flash memory
 * @len:	number of bytes to erase
 *
 * Build a list of best fitted erase commands and execute it once we validate
 * that the erase can be performed.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_erase_multi_sectors(struct spi_nor *nor, u64 addr, u32 len)
{
	LIST_HEAD(erase_list);
	struct spi_nor_erase_command *cmd, *next;
	int ret;

	ret = spi_nor_init_erase_cmd_list(nor, &erase_list, addr, len);
	if (ret)
		return ret;

	list_for_each_entry_safe(cmd, next, &erase_list, list) {
		nor->erase_opcode = cmd->opcode;
		while (cmd->count) {
			dev_vdbg(nor->dev, "erase_cmd->size = 0x%08x, erase_cmd->opcode = 0x%02x, erase_cmd->count = %u\n",
				 cmd->size, cmd->opcode, cmd->count);

			ret = spi_nor_lock_device(nor);
			if (ret)
				goto destroy_erase_cmd_list;

			ret = spi_nor_write_enable(nor);
			if (ret) {
				spi_nor_unlock_device(nor);
				goto destroy_erase_cmd_list;
			}

			ret = spi_nor_erase_sector(nor, addr);
			spi_nor_unlock_device(nor);
			if (ret)
				goto destroy_erase_cmd_list;

			ret = spi_nor_wait_till_ready(nor);
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
	spi_nor_destroy_erase_cmd_list(&erase_list);
	return ret;
}

/*
 * Erase an address range on the nor chip.  The address range may extend
 * one or more erase sectors. Return an error if there is a problem erasing.
 */
static int spi_nor_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	u32 addr, len;
	uint32_t rem;
	int ret;

	dev_dbg(nor->dev, "at 0x%llx, len %lld\n", (long long)instr->addr,
			(long long)instr->len);

	if (spi_nor_has_uniform_erase(nor)) {
		div_u64_rem(instr->len, mtd->erasesize, &rem);
		if (rem)
			return -EINVAL;
	}

	addr = instr->addr;
	len = instr->len;

	ret = spi_nor_prep_and_lock_pe(nor, instr->addr, instr->len);
	if (ret)
		return ret;

	/* whole-chip erase? */
	if (len == mtd->size && !(nor->flags & SNOR_F_NO_OP_CHIP_ERASE)) {
		unsigned long timeout;

		ret = spi_nor_lock_device(nor);
		if (ret)
			goto erase_err;

		ret = spi_nor_write_enable(nor);
		if (ret) {
			spi_nor_unlock_device(nor);
			goto erase_err;
		}

		ret = spi_nor_erase_chip(nor);
		spi_nor_unlock_device(nor);
		if (ret)
			goto erase_err;

		/*
		 * Scale the timeout linearly with the size of the flash, with
		 * a minimum calibrated to an old 2MB flash. We could try to
		 * pull these from CFI/SFDP, but these values should be good
		 * enough for now.
		 */
		timeout = max(CHIP_ERASE_2MB_READY_WAIT_JIFFIES,
			      CHIP_ERASE_2MB_READY_WAIT_JIFFIES *
			      (unsigned long)(mtd->size / SZ_2M));
		ret = spi_nor_wait_till_ready_with_timeout(nor, timeout);
		if (ret)
			goto erase_err;

	/* REVISIT in some cases we could speed up erasing large regions
	 * by using SPINOR_OP_SE instead of SPINOR_OP_BE_4K.  We may have set up
	 * to use "small sector erase", but that's not always optimal.
	 */

	/* "sector"-at-a-time erase */
	} else if (spi_nor_has_uniform_erase(nor)) {
		while (len) {
			ret = spi_nor_lock_device(nor);
			if (ret)
				goto erase_err;

			ret = spi_nor_write_enable(nor);
			if (ret) {
				spi_nor_unlock_device(nor);
				goto erase_err;
			}

			ret = spi_nor_erase_sector(nor, addr);
			spi_nor_unlock_device(nor);
			if (ret)
				goto erase_err;

			ret = spi_nor_wait_till_ready(nor);
			if (ret)
				goto erase_err;

			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}

	/* erase multiple sectors */
	} else {
		ret = spi_nor_erase_multi_sectors(nor, addr, len);
		if (ret)
			goto erase_err;
	}

	ret = spi_nor_write_disable(nor);

erase_err:
	spi_nor_unlock_and_unprep_pe(nor, instr->addr, instr->len);

	return ret;
}

/**
 * spi_nor_sr1_bit6_quad_enable() - Set the Quad Enable BIT(6) in the Status
 * Register 1.
 * @nor:	pointer to a 'struct spi_nor'
 *
 * Bit 6 of the Status Register 1 is the QE bit for Macronix like QSPI memories.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_sr1_bit6_quad_enable(struct spi_nor *nor)
{
	int ret;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	if (nor->bouncebuf[0] & SR1_QUAD_EN_BIT6)
		return 0;

	nor->bouncebuf[0] |= SR1_QUAD_EN_BIT6;

	return spi_nor_write_sr1_and_check(nor, nor->bouncebuf[0]);
}

/**
 * spi_nor_sr2_bit1_quad_enable() - set the Quad Enable BIT(1) in the Status
 * Register 2.
 * @nor:       pointer to a 'struct spi_nor'.
 *
 * Bit 1 of the Status Register 2 is the QE bit for Spansion like QSPI memories.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_sr2_bit1_quad_enable(struct spi_nor *nor)
{
	int ret;

	if (nor->flags & SNOR_F_NO_READ_CR)
		return spi_nor_write_16bit_cr_and_check(nor, SR2_QUAD_EN_BIT1);

	ret = spi_nor_read_cr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	if (nor->bouncebuf[0] & SR2_QUAD_EN_BIT1)
		return 0;

	nor->bouncebuf[0] |= SR2_QUAD_EN_BIT1;

	return spi_nor_write_16bit_cr_and_check(nor, nor->bouncebuf[0]);
}

/**
 * spi_nor_sr2_bit7_quad_enable() - set QE bit in Status Register 2.
 * @nor:	pointer to a 'struct spi_nor'
 *
 * Set the Quad Enable (QE) bit in the Status Register 2.
 *
 * This is one of the procedures to set the QE bit described in the SFDP
 * (JESD216 rev B) specification but no manufacturer using this procedure has
 * been identified yet, hence the name of the function.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_sr2_bit7_quad_enable(struct spi_nor *nor)
{
	u8 *sr2 = nor->bouncebuf;
	int ret;
	u8 sr2_written;

	/* Check current Quad Enable bit value. */
	ret = spi_nor_read_sr2(nor, sr2);
	if (ret)
		return ret;
	if (*sr2 & SR2_QUAD_EN_BIT7)
		return 0;

	/* Update the Quad Enable bit. */
	*sr2 |= SR2_QUAD_EN_BIT7;

	ret = spi_nor_write_sr2(nor, sr2);
	if (ret)
		return ret;

	sr2_written = *sr2;

	/* Read back and check it. */
	ret = spi_nor_read_sr2(nor, sr2);
	if (ret)
		return ret;

	if (*sr2 != sr2_written) {
		dev_dbg(nor->dev, "SR2: Read back test failed\n");
		return -EIO;
	}

	return 0;
}

static const struct spi_nor_manufacturer *manufacturers[] = {
	&spi_nor_atmel,
	&spi_nor_catalyst,
	&spi_nor_eon,
	&spi_nor_esmt,
	&spi_nor_everspin,
	&spi_nor_fujitsu,
	&spi_nor_gigadevice,
	&spi_nor_intel,
	&spi_nor_issi,
	&spi_nor_macronix,
	&spi_nor_micron,
	&spi_nor_st,
	&spi_nor_spansion,
	&spi_nor_sst,
	&spi_nor_winbond,
	&spi_nor_xilinx,
	&spi_nor_xmc,
};

static const struct flash_info spi_nor_generic_flash = {
	.name = "spi-nor-generic",
	.n_banks = 1,
	/*
	 * JESD216 rev A doesn't specify the page size, therefore we need a
	 * sane default.
	 */
	.page_size = 256,
	.parse_sfdp = true,
};

static const struct flash_info *spi_nor_match_id(struct spi_nor *nor,
						 const u8 *id)
{
	const struct flash_info *part;
	unsigned int i, j;

	for (i = 0; i < ARRAY_SIZE(manufacturers); i++) {
		for (j = 0; j < manufacturers[i]->nparts; j++) {
			part = &manufacturers[i]->parts[j];
			if (part->id_len &&
			    !memcmp(part->id, id, part->id_len)) {
				nor->manufacturer = manufacturers[i];
				return part;
			}
		}
	}

	return NULL;
}

static const struct flash_info *spi_nor_detect(struct spi_nor *nor)
{
	const struct flash_info *info;
	u8 *id = nor->bouncebuf;
	int ret;

	ret = spi_nor_read_id(nor, 0, 0, id, nor->reg_proto);
	if (ret) {
		dev_dbg(nor->dev, "error %d reading JEDEC ID\n", ret);
		return ERR_PTR(ret);
	}

	/* Cache the complete flash ID. */
	nor->id = devm_kmemdup(nor->dev, id, SPI_NOR_MAX_ID_LEN, GFP_KERNEL);
	if (!nor->id)
		return ERR_PTR(-ENOMEM);

	info = spi_nor_match_id(nor, id);

	/* Fallback to a generic flash described only by its SFDP data. */
	if (!info) {
		ret = spi_nor_check_sfdp_signature(nor);
		if (!ret)
			info = &spi_nor_generic_flash;
	}

	if (!info) {
		dev_err(nor->dev, "unrecognized JEDEC id bytes: %*ph\n",
			SPI_NOR_MAX_ID_LEN, id);
		return ERR_PTR(-ENODEV);
	}
	return info;
}

static int spi_nor_read(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	loff_t from_lock = from;
	size_t len_lock = len;
	ssize_t ret;

	dev_dbg(nor->dev, "from 0x%08x, len %zd\n", (u32)from, len);

	ret = spi_nor_prep_and_lock_rd(nor, from_lock, len_lock);
	if (ret)
		return ret;

	while (len) {
		loff_t addr = from;

		addr = spi_nor_convert_addr(nor, addr);

		ret = spi_nor_read_data(nor, addr, len, buf);
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
	spi_nor_unlock_and_unprep_rd(nor, from_lock, len_lock);

	return ret;
}

/*
 * Write an address range to the nor chip.  Data must be written in
 * FLASH_PAGESIZE chunks.  The address range may be any size provided
 * it is within the physical boundaries.
 */
static int spi_nor_write(struct mtd_info *mtd, loff_t to, size_t len,
	size_t *retlen, const u_char *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	size_t page_offset, page_remain, i;
	ssize_t ret;
	u32 page_size = nor->params->page_size;

	dev_dbg(nor->dev, "to 0x%08x, len %zd\n", (u32)to, len);

	ret = spi_nor_prep_and_lock_pe(nor, to, len);
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
			uint64_t aux = addr;

			page_offset = do_div(aux, page_size);
		}
		/* the size of data remaining on the first page */
		page_remain = min_t(size_t, page_size - page_offset, len - i);

		addr = spi_nor_convert_addr(nor, addr);

		ret = spi_nor_lock_device(nor);
		if (ret)
			goto write_err;

		ret = spi_nor_write_enable(nor);
		if (ret) {
			spi_nor_unlock_device(nor);
			goto write_err;
		}

		ret = spi_nor_write_data(nor, addr, page_remain, buf + i);
		spi_nor_unlock_device(nor);
		if (ret < 0)
			goto write_err;
		written = ret;

		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			goto write_err;
		*retlen += written;
		i += written;
	}

write_err:
	spi_nor_unlock_and_unprep_pe(nor, to, len);

	return ret;
}

static int spi_nor_check(struct spi_nor *nor)
{
	if (!nor->dev ||
	    (!nor->spimem && !nor->controller_ops) ||
	    (!nor->spimem && nor->controller_ops &&
	    (!nor->controller_ops->read ||
	     !nor->controller_ops->write ||
	     !nor->controller_ops->read_reg ||
	     !nor->controller_ops->write_reg))) {
		pr_err("spi-nor: please fill all the necessary fields!\n");
		return -EINVAL;
	}

	if (nor->spimem && nor->controller_ops) {
		dev_err(nor->dev, "nor->spimem and nor->controller_ops are mutually exclusive, please set just one of them.\n");
		return -EINVAL;
	}

	return 0;
}

void
spi_nor_set_read_settings(struct spi_nor_read_command *read,
			  u8 num_mode_clocks,
			  u8 num_wait_states,
			  u8 opcode,
			  enum spi_nor_protocol proto)
{
	read->num_mode_clocks = num_mode_clocks;
	read->num_wait_states = num_wait_states;
	read->opcode = opcode;
	read->proto = proto;
}

void spi_nor_set_pp_settings(struct spi_nor_pp_command *pp, u8 opcode,
			     enum spi_nor_protocol proto)
{
	pp->opcode = opcode;
	pp->proto = proto;
}

static int spi_nor_hwcaps2cmd(u32 hwcaps, const int table[][2], size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		if (table[i][0] == (int)hwcaps)
			return table[i][1];

	return -EINVAL;
}

int spi_nor_hwcaps_read2cmd(u32 hwcaps)
{
	static const int hwcaps_read2cmd[][2] = {
		{ SNOR_HWCAPS_READ,		SNOR_CMD_READ },
		{ SNOR_HWCAPS_READ_FAST,	SNOR_CMD_READ_FAST },
		{ SNOR_HWCAPS_READ_1_1_1_DTR,	SNOR_CMD_READ_1_1_1_DTR },
		{ SNOR_HWCAPS_READ_1_1_2,	SNOR_CMD_READ_1_1_2 },
		{ SNOR_HWCAPS_READ_1_2_2,	SNOR_CMD_READ_1_2_2 },
		{ SNOR_HWCAPS_READ_2_2_2,	SNOR_CMD_READ_2_2_2 },
		{ SNOR_HWCAPS_READ_1_2_2_DTR,	SNOR_CMD_READ_1_2_2_DTR },
		{ SNOR_HWCAPS_READ_1_1_4,	SNOR_CMD_READ_1_1_4 },
		{ SNOR_HWCAPS_READ_1_4_4,	SNOR_CMD_READ_1_4_4 },
		{ SNOR_HWCAPS_READ_4_4_4,	SNOR_CMD_READ_4_4_4 },
		{ SNOR_HWCAPS_READ_1_4_4_DTR,	SNOR_CMD_READ_1_4_4_DTR },
		{ SNOR_HWCAPS_READ_1_1_8,	SNOR_CMD_READ_1_1_8 },
		{ SNOR_HWCAPS_READ_1_8_8,	SNOR_CMD_READ_1_8_8 },
		{ SNOR_HWCAPS_READ_8_8_8,	SNOR_CMD_READ_8_8_8 },
		{ SNOR_HWCAPS_READ_1_8_8_DTR,	SNOR_CMD_READ_1_8_8_DTR },
		{ SNOR_HWCAPS_READ_8_8_8_DTR,	SNOR_CMD_READ_8_8_8_DTR },
	};

	return spi_nor_hwcaps2cmd(hwcaps, hwcaps_read2cmd,
				  ARRAY_SIZE(hwcaps_read2cmd));
}

int spi_nor_hwcaps_pp2cmd(u32 hwcaps)
{
	static const int hwcaps_pp2cmd[][2] = {
		{ SNOR_HWCAPS_PP,		SNOR_CMD_PP },
		{ SNOR_HWCAPS_PP_1_1_4,		SNOR_CMD_PP_1_1_4 },
		{ SNOR_HWCAPS_PP_1_4_4,		SNOR_CMD_PP_1_4_4 },
		{ SNOR_HWCAPS_PP_4_4_4,		SNOR_CMD_PP_4_4_4 },
		{ SNOR_HWCAPS_PP_1_1_8,		SNOR_CMD_PP_1_1_8 },
		{ SNOR_HWCAPS_PP_1_8_8,		SNOR_CMD_PP_1_8_8 },
		{ SNOR_HWCAPS_PP_8_8_8,		SNOR_CMD_PP_8_8_8 },
		{ SNOR_HWCAPS_PP_8_8_8_DTR,	SNOR_CMD_PP_8_8_8_DTR },
	};

	return spi_nor_hwcaps2cmd(hwcaps, hwcaps_pp2cmd,
				  ARRAY_SIZE(hwcaps_pp2cmd));
}

/**
 * spi_nor_spimem_check_op - check if the operation is supported
 *                           by controller
 *@nor:        pointer to a 'struct spi_nor'
 *@op:         pointer to op template to be checked
 *
 * Returns 0 if operation is supported, -EOPNOTSUPP otherwise.
 */
static int spi_nor_spimem_check_op(struct spi_nor *nor,
				   struct spi_mem_op *op)
{
	/*
	 * First test with 4 address bytes. The opcode itself might
	 * be a 3B addressing opcode but we don't care, because
	 * SPI controller implementation should not check the opcode,
	 * but just the sequence.
	 */
	op->addr.nbytes = 4;
	if (!spi_mem_supports_op(nor->spimem, op)) {
		if (nor->params->size > SZ_16M)
			return -EOPNOTSUPP;

		/* If flash size <= 16MB, 3 address bytes are sufficient */
		op->addr.nbytes = 3;
		if (!spi_mem_supports_op(nor->spimem, op))
			return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * spi_nor_spimem_check_readop - check if the read op is supported
 *                               by controller
 *@nor:         pointer to a 'struct spi_nor'
 *@read:        pointer to op template to be checked
 *
 * Returns 0 if operation is supported, -EOPNOTSUPP otherwise.
 */
static int spi_nor_spimem_check_readop(struct spi_nor *nor,
				       const struct spi_nor_read_command *read)
{
	struct spi_mem_op op = SPI_NOR_READ_OP(read->opcode);

	spi_nor_spimem_setup_op(nor, &op, read->proto);

	/* convert the dummy cycles to the number of bytes */
	op.dummy.nbytes = (read->num_mode_clocks + read->num_wait_states) *
			  op.dummy.buswidth / 8;
	if (spi_nor_protocol_is_dtr(nor->read_proto))
		op.dummy.nbytes *= 2;

	return spi_nor_spimem_check_op(nor, &op);
}

/**
 * spi_nor_spimem_check_pp - check if the page program op is supported
 *                           by controller
 *@nor:         pointer to a 'struct spi_nor'
 *@pp:          pointer to op template to be checked
 *
 * Returns 0 if operation is supported, -EOPNOTSUPP otherwise.
 */
static int spi_nor_spimem_check_pp(struct spi_nor *nor,
				   const struct spi_nor_pp_command *pp)
{
	struct spi_mem_op op = SPI_NOR_PP_OP(pp->opcode);

	spi_nor_spimem_setup_op(nor, &op, pp->proto);

	return spi_nor_spimem_check_op(nor, &op);
}

/**
 * spi_nor_spimem_adjust_hwcaps - Find optimal Read/Write protocol
 *                                based on SPI controller capabilities
 * @nor:        pointer to a 'struct spi_nor'
 * @hwcaps:     pointer to resulting capabilities after adjusting
 *              according to controller and flash's capability
 */
static void
spi_nor_spimem_adjust_hwcaps(struct spi_nor *nor, u32 *hwcaps)
{
	struct spi_nor_flash_parameter *params = nor->params;
	unsigned int cap;

	/* X-X-X modes are not supported yet, mask them all. */
	*hwcaps &= ~SNOR_HWCAPS_X_X_X;

	/*
	 * If the reset line is broken, we do not want to enter a stateful
	 * mode.
	 */
	if (nor->flags & SNOR_F_BROKEN_RESET)
		*hwcaps &= ~(SNOR_HWCAPS_X_X_X | SNOR_HWCAPS_X_X_X_DTR);

	for (cap = 0; cap < sizeof(*hwcaps) * BITS_PER_BYTE; cap++) {
		int rdidx, ppidx;

		if (!(*hwcaps & BIT(cap)))
			continue;

		rdidx = spi_nor_hwcaps_read2cmd(BIT(cap));
		if (rdidx >= 0 &&
		    spi_nor_spimem_check_readop(nor, &params->reads[rdidx]))
			*hwcaps &= ~BIT(cap);

		ppidx = spi_nor_hwcaps_pp2cmd(BIT(cap));
		if (ppidx < 0)
			continue;

		if (spi_nor_spimem_check_pp(nor,
					    &params->page_programs[ppidx]))
			*hwcaps &= ~BIT(cap);
	}
}

/**
 * spi_nor_set_erase_type() - set a SPI NOR erase type
 * @erase:	pointer to a structure that describes a SPI NOR erase type
 * @size:	the size of the sector/block erased by the erase type
 * @opcode:	the SPI command op code to erase the sector/block
 */
void spi_nor_set_erase_type(struct spi_nor_erase_type *erase, u32 size,
			    u8 opcode)
{
	erase->size = size;
	erase->opcode = opcode;
	/* JEDEC JESD216B Standard imposes erase sizes to be power of 2. */
	erase->size_shift = ffs(erase->size) - 1;
	erase->size_mask = (1 << erase->size_shift) - 1;
}

/**
 * spi_nor_mask_erase_type() - mask out a SPI NOR erase type
 * @erase:	pointer to a structure that describes a SPI NOR erase type
 */
void spi_nor_mask_erase_type(struct spi_nor_erase_type *erase)
{
	erase->size = 0;
}

/**
 * spi_nor_init_uniform_erase_map() - Initialize uniform erase map
 * @map:		the erase map of the SPI NOR
 * @erase_mask:		bitmask encoding erase types that can erase the entire
 *			flash memory
 * @flash_size:		the spi nor flash memory size
 */
void spi_nor_init_uniform_erase_map(struct spi_nor_erase_map *map,
				    u8 erase_mask, u64 flash_size)
{
	/* Offset 0 with erase_mask and SNOR_LAST_REGION bit set */
	map->uniform_region.offset = (erase_mask & SNOR_ERASE_TYPE_MASK) |
				     SNOR_LAST_REGION;
	map->uniform_region.size = flash_size;
	map->regions = &map->uniform_region;
	map->uniform_erase_type = erase_mask;
}

int spi_nor_post_bfpt_fixups(struct spi_nor *nor,
			     const struct sfdp_parameter_header *bfpt_header,
			     const struct sfdp_bfpt *bfpt)
{
	int ret;

	if (nor->manufacturer && nor->manufacturer->fixups &&
	    nor->manufacturer->fixups->post_bfpt) {
		ret = nor->manufacturer->fixups->post_bfpt(nor, bfpt_header,
							   bfpt);
		if (ret)
			return ret;
	}

	if (nor->info->fixups && nor->info->fixups->post_bfpt)
		return nor->info->fixups->post_bfpt(nor, bfpt_header, bfpt);

	return 0;
}

static int spi_nor_select_read(struct spi_nor *nor,
			       u32 shared_hwcaps)
{
	int cmd, best_match = fls(shared_hwcaps & SNOR_HWCAPS_READ_MASK) - 1;
	const struct spi_nor_read_command *read;

	if (best_match < 0)
		return -EINVAL;

	cmd = spi_nor_hwcaps_read2cmd(BIT(best_match));
	if (cmd < 0)
		return -EINVAL;

	read = &nor->params->reads[cmd];
	nor->read_opcode = read->opcode;
	nor->read_proto = read->proto;

	/*
	 * In the SPI NOR framework, we don't need to make the difference
	 * between mode clock cycles and wait state clock cycles.
	 * Indeed, the value of the mode clock cycles is used by a QSPI
	 * flash memory to know whether it should enter or leave its 0-4-4
	 * (Continuous Read / XIP) mode.
	 * eXecution In Place is out of the scope of the mtd sub-system.
	 * Hence we choose to merge both mode and wait state clock cycles
	 * into the so called dummy clock cycles.
	 */
	nor->read_dummy = read->num_mode_clocks + read->num_wait_states;
	return 0;
}

static int spi_nor_select_pp(struct spi_nor *nor,
			     u32 shared_hwcaps)
{
	int cmd, best_match = fls(shared_hwcaps & SNOR_HWCAPS_PP_MASK) - 1;
	const struct spi_nor_pp_command *pp;

	if (best_match < 0)
		return -EINVAL;

	cmd = spi_nor_hwcaps_pp2cmd(BIT(best_match));
	if (cmd < 0)
		return -EINVAL;

	pp = &nor->params->page_programs[cmd];
	nor->program_opcode = pp->opcode;
	nor->write_proto = pp->proto;
	return 0;
}

/**
 * spi_nor_select_uniform_erase() - select optimum uniform erase type
 * @map:		the erase map of the SPI NOR
 * @wanted_size:	the erase type size to search for. Contains the value of
 *			info->sector_size, the "small sector" size in case
 *			CONFIG_MTD_SPI_NOR_USE_4K_SECTORS is defined or 0 if
 *			there is no information about the sector size. The
 *			latter is the case if the flash parameters are parsed
 *			solely by SFDP, then the largest supported erase type
 *			is selected.
 *
 * Once the optimum uniform sector erase command is found, disable all the
 * other.
 *
 * Return: pointer to erase type on success, NULL otherwise.
 */
static const struct spi_nor_erase_type *
spi_nor_select_uniform_erase(struct spi_nor_erase_map *map,
			     const u32 wanted_size)
{
	const struct spi_nor_erase_type *tested_erase, *erase = NULL;
	int i;
	u8 uniform_erase_type = map->uniform_erase_type;

	for (i = SNOR_ERASE_TYPE_MAX - 1; i >= 0; i--) {
		if (!(uniform_erase_type & BIT(i)))
			continue;

		tested_erase = &map->erase_type[i];

		/* Skip masked erase types. */
		if (!tested_erase->size)
			continue;

		/*
		 * If the current erase size is the one, stop here:
		 * we have found the right uniform Sector Erase command.
		 */
		if (tested_erase->size == wanted_size) {
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
	map->uniform_erase_type &= ~SNOR_ERASE_TYPE_MASK;
	map->uniform_erase_type |= BIT(erase - map->erase_type);
	return erase;
}

static int spi_nor_select_erase(struct spi_nor *nor)
{
	struct spi_nor_erase_map *map = &nor->params->erase_map;
	const struct spi_nor_erase_type *erase = NULL;
	struct mtd_info *mtd = &nor->mtd;
	u32 wanted_size = nor->info->sector_size;
	int i;

	/*
	 * The previous implementation handling Sector Erase commands assumed
	 * that the SPI flash memory has an uniform layout then used only one
	 * of the supported erase sizes for all Sector Erase commands.
	 * So to be backward compatible, the new implementation also tries to
	 * manage the SPI flash memory as uniform with a single erase sector
	 * size, when possible.
	 */
#ifdef CONFIG_MTD_SPI_NOR_USE_4K_SECTORS
	/* prefer "small sector" erase if possible */
	wanted_size = 4096u;
#endif

	if (spi_nor_has_uniform_erase(nor)) {
		erase = spi_nor_select_uniform_erase(map, wanted_size);
		if (!erase)
			return -EINVAL;
		nor->erase_opcode = erase->opcode;
		mtd->erasesize = erase->size;
		return 0;
	}

	/*
	 * For non-uniform SPI flash memory, set mtd->erasesize to the
	 * maximum erase sector size. No need to set nor->erase_opcode.
	 */
	for (i = SNOR_ERASE_TYPE_MAX - 1; i >= 0; i--) {
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

static int spi_nor_default_setup(struct spi_nor *nor,
				 const struct spi_nor_hwcaps *hwcaps)
{
	struct spi_nor_flash_parameter *params = nor->params;
	u32 ignored_mask, shared_mask;
	int err;

	/*
	 * Keep only the hardware capabilities supported by both the SPI
	 * controller and the SPI flash memory.
	 */
	shared_mask = hwcaps->mask & params->hwcaps.mask;

	if (nor->spimem) {
		/*
		 * When called from spi_nor_probe(), all caps are set and we
		 * need to discard some of them based on what the SPI
		 * controller actually supports (using spi_mem_supports_op()).
		 */
		spi_nor_spimem_adjust_hwcaps(nor, &shared_mask);
	} else {
		/*
		 * SPI n-n-n protocols are not supported when the SPI
		 * controller directly implements the spi_nor interface.
		 * Yet another reason to switch to spi-mem.
		 */
		ignored_mask = SNOR_HWCAPS_X_X_X | SNOR_HWCAPS_X_X_X_DTR;
		if (shared_mask & ignored_mask) {
			dev_dbg(nor->dev,
				"SPI n-n-n protocols are not supported.\n");
			shared_mask &= ~ignored_mask;
		}
	}

	/* Select the (Fast) Read command. */
	err = spi_nor_select_read(nor, shared_mask);
	if (err) {
		dev_dbg(nor->dev,
			"can't select read settings supported by both the SPI controller and memory.\n");
		return err;
	}

	/* Select the Page Program command. */
	err = spi_nor_select_pp(nor, shared_mask);
	if (err) {
		dev_dbg(nor->dev,
			"can't select write settings supported by both the SPI controller and memory.\n");
		return err;
	}

	/* Select the Sector Erase command. */
	err = spi_nor_select_erase(nor);
	if (err) {
		dev_dbg(nor->dev,
			"can't select erase settings supported by both the SPI controller and memory.\n");
		return err;
	}

	return 0;
}

static int spi_nor_set_addr_nbytes(struct spi_nor *nor)
{
	if (nor->params->addr_nbytes) {
		nor->addr_nbytes = nor->params->addr_nbytes;
	} else if (nor->read_proto == SNOR_PROTO_8_8_8_DTR) {
		/*
		 * In 8D-8D-8D mode, one byte takes half a cycle to transfer. So
		 * in this protocol an odd addr_nbytes cannot be used because
		 * then the address phase would only span a cycle and a half.
		 * Half a cycle would be left over. We would then have to start
		 * the dummy phase in the middle of a cycle and so too the data
		 * phase, and we will end the transaction with half a cycle left
		 * over.
		 *
		 * Force all 8D-8D-8D flashes to use an addr_nbytes of 4 to
		 * avoid this situation.
		 */
		nor->addr_nbytes = 4;
	} else if (nor->info->addr_nbytes) {
		nor->addr_nbytes = nor->info->addr_nbytes;
	} else {
		nor->addr_nbytes = 3;
	}

	if (nor->addr_nbytes == 3 && nor->params->size > 0x1000000) {
		/* enable 4-byte addressing if the device exceeds 16MiB */
		nor->addr_nbytes = 4;
	}

	if (nor->addr_nbytes > SPI_NOR_MAX_ADDR_NBYTES) {
		dev_dbg(nor->dev, "The number of address bytes is too large: %u\n",
			nor->addr_nbytes);
		return -EINVAL;
	}

	/* Set 4byte opcodes when possible. */
	if (nor->addr_nbytes == 4 && nor->flags & SNOR_F_4B_OPCODES &&
	    !(nor->flags & SNOR_F_HAS_4BAIT))
		spi_nor_set_4byte_opcodes(nor);

	return 0;
}

static int spi_nor_setup(struct spi_nor *nor,
			 const struct spi_nor_hwcaps *hwcaps)
{
	int ret;

	if (nor->params->setup)
		ret = nor->params->setup(nor, hwcaps);
	else
		ret = spi_nor_default_setup(nor, hwcaps);
	if (ret)
		return ret;

	return spi_nor_set_addr_nbytes(nor);
}

/**
 * spi_nor_manufacturer_init_params() - Initialize the flash's parameters and
 * settings based on MFR register and ->default_init() hook.
 * @nor:	pointer to a 'struct spi_nor'.
 */
static void spi_nor_manufacturer_init_params(struct spi_nor *nor)
{
	if (nor->manufacturer && nor->manufacturer->fixups &&
	    nor->manufacturer->fixups->default_init)
		nor->manufacturer->fixups->default_init(nor);

	if (nor->info->fixups && nor->info->fixups->default_init)
		nor->info->fixups->default_init(nor);
}

/**
 * spi_nor_no_sfdp_init_params() - Initialize the flash's parameters and
 * settings based on nor->info->sfdp_flags. This method should be called only by
 * flashes that do not define SFDP tables. If the flash supports SFDP but the
 * information is wrong and the settings from this function can not be retrieved
 * by parsing SFDP, one should instead use the fixup hooks and update the wrong
 * bits.
 * @nor:	pointer to a 'struct spi_nor'.
 */
static void spi_nor_no_sfdp_init_params(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;
	struct spi_nor_erase_map *map = &params->erase_map;
	const u8 no_sfdp_flags = nor->info->no_sfdp_flags;
	u8 i, erase_mask;

	if (no_sfdp_flags & SPI_NOR_DUAL_READ) {
		params->hwcaps.mask |= SNOR_HWCAPS_READ_1_1_2;
		spi_nor_set_read_settings(&params->reads[SNOR_CMD_READ_1_1_2],
					  0, 8, SPINOR_OP_READ_1_1_2,
					  SNOR_PROTO_1_1_2);
	}

	if (no_sfdp_flags & SPI_NOR_QUAD_READ) {
		params->hwcaps.mask |= SNOR_HWCAPS_READ_1_1_4;
		spi_nor_set_read_settings(&params->reads[SNOR_CMD_READ_1_1_4],
					  0, 8, SPINOR_OP_READ_1_1_4,
					  SNOR_PROTO_1_1_4);
	}

	if (no_sfdp_flags & SPI_NOR_OCTAL_READ) {
		params->hwcaps.mask |= SNOR_HWCAPS_READ_1_1_8;
		spi_nor_set_read_settings(&params->reads[SNOR_CMD_READ_1_1_8],
					  0, 8, SPINOR_OP_READ_1_1_8,
					  SNOR_PROTO_1_1_8);
	}

	if (no_sfdp_flags & SPI_NOR_OCTAL_DTR_READ) {
		params->hwcaps.mask |= SNOR_HWCAPS_READ_8_8_8_DTR;
		spi_nor_set_read_settings(&params->reads[SNOR_CMD_READ_8_8_8_DTR],
					  0, 20, SPINOR_OP_READ_FAST,
					  SNOR_PROTO_8_8_8_DTR);
	}

	if (no_sfdp_flags & SPI_NOR_OCTAL_DTR_PP) {
		params->hwcaps.mask |= SNOR_HWCAPS_PP_8_8_8_DTR;
		/*
		 * Since xSPI Page Program opcode is backward compatible with
		 * Legacy SPI, use Legacy SPI opcode there as well.
		 */
		spi_nor_set_pp_settings(&params->page_programs[SNOR_CMD_PP_8_8_8_DTR],
					SPINOR_OP_PP, SNOR_PROTO_8_8_8_DTR);
	}

	/*
	 * Sector Erase settings. Sort Erase Types in ascending order, with the
	 * smallest erase size starting at BIT(0).
	 */
	erase_mask = 0;
	i = 0;
	if (no_sfdp_flags & SECT_4K) {
		erase_mask |= BIT(i);
		spi_nor_set_erase_type(&map->erase_type[i], 4096u,
				       SPINOR_OP_BE_4K);
		i++;
	}
	erase_mask |= BIT(i);
	spi_nor_set_erase_type(&map->erase_type[i], nor->info->sector_size,
			       SPINOR_OP_SE);
	spi_nor_init_uniform_erase_map(map, erase_mask, params->size);
}

/**
 * spi_nor_init_flags() - Initialize NOR flags for settings that are not defined
 * in the JESD216 SFDP standard, thus can not be retrieved when parsing SFDP.
 * @nor:	pointer to a 'struct spi_nor'
 */
static void spi_nor_init_flags(struct spi_nor *nor)
{
	struct device_node *np = spi_nor_get_flash_node(nor);
	const u16 flags = nor->info->flags;

	if (of_property_read_bool(np, "broken-flash-reset"))
		nor->flags |= SNOR_F_BROKEN_RESET;

	if (of_property_read_bool(np, "no-wp"))
		nor->flags |= SNOR_F_NO_WP;

	if (flags & SPI_NOR_SWP_IS_VOLATILE)
		nor->flags |= SNOR_F_SWP_IS_VOLATILE;

	if (flags & SPI_NOR_HAS_LOCK)
		nor->flags |= SNOR_F_HAS_LOCK;

	if (flags & SPI_NOR_HAS_TB) {
		nor->flags |= SNOR_F_HAS_SR_TB;
		if (flags & SPI_NOR_TB_SR_BIT6)
			nor->flags |= SNOR_F_HAS_SR_TB_BIT6;
	}

	if (flags & SPI_NOR_4BIT_BP) {
		nor->flags |= SNOR_F_HAS_4BIT_BP;
		if (flags & SPI_NOR_BP3_SR_BIT6)
			nor->flags |= SNOR_F_HAS_SR_BP3_BIT6;
	}

	if (flags & NO_CHIP_ERASE)
		nor->flags |= SNOR_F_NO_OP_CHIP_ERASE;

	if (flags & SPI_NOR_RWW && nor->info->n_banks > 1 &&
	    !nor->controller_ops)
		nor->flags |= SNOR_F_RWW;
}

/**
 * spi_nor_init_fixup_flags() - Initialize NOR flags for settings that can not
 * be discovered by SFDP for this particular flash because the SFDP table that
 * indicates this support is not defined in the flash. In case the table for
 * this support is defined but has wrong values, one should instead use a
 * post_sfdp() hook to set the SNOR_F equivalent flag.
 * @nor:       pointer to a 'struct spi_nor'
 */
static void spi_nor_init_fixup_flags(struct spi_nor *nor)
{
	const u8 fixup_flags = nor->info->fixup_flags;

	if (fixup_flags & SPI_NOR_4B_OPCODES)
		nor->flags |= SNOR_F_4B_OPCODES;

	if (fixup_flags & SPI_NOR_IO_MODE_EN_VOLATILE)
		nor->flags |= SNOR_F_IO_MODE_EN_VOLATILE;
}

/**
 * spi_nor_late_init_params() - Late initialization of default flash parameters.
 * @nor:	pointer to a 'struct spi_nor'
 *
 * Used to initialize flash parameters that are not declared in the JESD216
 * SFDP standard, or where SFDP tables are not defined at all.
 * Will replace the spi_nor_manufacturer_init_params() method.
 */
static void spi_nor_late_init_params(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;

	if (nor->manufacturer && nor->manufacturer->fixups &&
	    nor->manufacturer->fixups->late_init)
		nor->manufacturer->fixups->late_init(nor);

	if (nor->info->fixups && nor->info->fixups->late_init)
		nor->info->fixups->late_init(nor);

	/* Default method kept for backward compatibility. */
	if (!params->set_4byte_addr_mode)
		params->set_4byte_addr_mode = spi_nor_set_4byte_addr_mode_brwr;

	spi_nor_init_flags(nor);
	spi_nor_init_fixup_flags(nor);

	/*
	 * NOR protection support. When locking_ops are not provided, we pick
	 * the default ones.
	 */
	if (nor->flags & SNOR_F_HAS_LOCK && !nor->params->locking_ops)
		spi_nor_init_default_locking_ops(nor);

	if (nor->info->n_banks > 1)
		params->bank_size = div64_u64(params->size, nor->info->n_banks);
}

/**
 * spi_nor_sfdp_init_params_deprecated() - Deprecated way of initializing flash
 * parameters and settings based on JESD216 SFDP standard.
 * @nor:	pointer to a 'struct spi_nor'.
 *
 * The method has a roll-back mechanism: in case the SFDP parsing fails, the
 * legacy flash parameters and settings will be restored.
 */
static void spi_nor_sfdp_init_params_deprecated(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter sfdp_params;

	memcpy(&sfdp_params, nor->params, sizeof(sfdp_params));

	if (spi_nor_parse_sfdp(nor)) {
		memcpy(nor->params, &sfdp_params, sizeof(*nor->params));
		nor->flags &= ~SNOR_F_4B_OPCODES;
	}
}

/**
 * spi_nor_init_params_deprecated() - Deprecated way of initializing flash
 * parameters and settings.
 * @nor:	pointer to a 'struct spi_nor'.
 *
 * The method assumes that flash doesn't support SFDP so it initializes flash
 * parameters in spi_nor_no_sfdp_init_params() which later on can be overwritten
 * when parsing SFDP, if supported.
 */
static void spi_nor_init_params_deprecated(struct spi_nor *nor)
{
	spi_nor_no_sfdp_init_params(nor);

	spi_nor_manufacturer_init_params(nor);

	if (nor->info->no_sfdp_flags & (SPI_NOR_DUAL_READ |
					SPI_NOR_QUAD_READ |
					SPI_NOR_OCTAL_READ |
					SPI_NOR_OCTAL_DTR_READ))
		spi_nor_sfdp_init_params_deprecated(nor);
}

/**
 * spi_nor_init_default_params() - Default initialization of flash parameters
 * and settings. Done for all flashes, regardless is they define SFDP tables
 * or not.
 * @nor:	pointer to a 'struct spi_nor'.
 */
static void spi_nor_init_default_params(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;
	const struct flash_info *info = nor->info;
	struct device_node *np = spi_nor_get_flash_node(nor);

	params->quad_enable = spi_nor_sr2_bit1_quad_enable;
	params->otp.org = &info->otp_org;

	/* Default to 16-bit Write Status (01h) Command */
	nor->flags |= SNOR_F_HAS_16BIT_SR;

	/* Set SPI NOR sizes. */
	params->writesize = 1;
	params->size = (u64)info->sector_size * info->n_sectors;
	params->bank_size = params->size;
	params->page_size = info->page_size;

	if (!(info->flags & SPI_NOR_NO_FR)) {
		/* Default to Fast Read for DT and non-DT platform devices. */
		params->hwcaps.mask |= SNOR_HWCAPS_READ_FAST;

		/* Mask out Fast Read if not requested at DT instantiation. */
		if (np && !of_property_read_bool(np, "m25p,fast-read"))
			params->hwcaps.mask &= ~SNOR_HWCAPS_READ_FAST;
	}

	/* (Fast) Read settings. */
	params->hwcaps.mask |= SNOR_HWCAPS_READ;
	spi_nor_set_read_settings(&params->reads[SNOR_CMD_READ],
				  0, 0, SPINOR_OP_READ,
				  SNOR_PROTO_1_1_1);

	if (params->hwcaps.mask & SNOR_HWCAPS_READ_FAST)
		spi_nor_set_read_settings(&params->reads[SNOR_CMD_READ_FAST],
					  0, 8, SPINOR_OP_READ_FAST,
					  SNOR_PROTO_1_1_1);
	/* Page Program settings. */
	params->hwcaps.mask |= SNOR_HWCAPS_PP;
	spi_nor_set_pp_settings(&params->page_programs[SNOR_CMD_PP],
				SPINOR_OP_PP, SNOR_PROTO_1_1_1);

	if (info->flags & SPI_NOR_QUAD_PP) {
		params->hwcaps.mask |= SNOR_HWCAPS_PP_1_1_4;
		spi_nor_set_pp_settings(&params->page_programs[SNOR_CMD_PP_1_1_4],
					SPINOR_OP_PP_1_1_4, SNOR_PROTO_1_1_4);
	}
}

/**
 * spi_nor_init_params() - Initialize the flash's parameters and settings.
 * @nor:	pointer to a 'struct spi_nor'.
 *
 * The flash parameters and settings are initialized based on a sequence of
 * calls that are ordered by priority:
 *
 * 1/ Default flash parameters initialization. The initializations are done
 *    based on nor->info data:
 *		spi_nor_info_init_params()
 *
 * which can be overwritten by:
 * 2/ Manufacturer flash parameters initialization. The initializations are
 *    done based on MFR register, or when the decisions can not be done solely
 *    based on MFR, by using specific flash_info tweeks, ->default_init():
 *		spi_nor_manufacturer_init_params()
 *
 * which can be overwritten by:
 * 3/ SFDP flash parameters initialization. JESD216 SFDP is a standard and
 *    should be more accurate that the above.
 *		spi_nor_parse_sfdp() or spi_nor_no_sfdp_init_params()
 *
 *    Please note that there is a ->post_bfpt() fixup hook that can overwrite
 *    the flash parameters and settings immediately after parsing the Basic
 *    Flash Parameter Table.
 *    spi_nor_post_sfdp_fixups() is called after the SFDP tables are parsed.
 *    It is used to tweak various flash parameters when information provided
 *    by the SFDP tables are wrong.
 *
 * which can be overwritten by:
 * 4/ Late flash parameters initialization, used to initialize flash
 * parameters that are not declared in the JESD216 SFDP standard, or where SFDP
 * tables are not defined at all.
 *		spi_nor_late_init_params()
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_init_params(struct spi_nor *nor)
{
	int ret;

	nor->params = devm_kzalloc(nor->dev, sizeof(*nor->params), GFP_KERNEL);
	if (!nor->params)
		return -ENOMEM;

	spi_nor_init_default_params(nor);

	if (nor->info->parse_sfdp) {
		ret = spi_nor_parse_sfdp(nor);
		if (ret) {
			dev_err(nor->dev, "BFPT parsing failed. Please consider using SPI_NOR_SKIP_SFDP when declaring the flash\n");
			return ret;
		}
	} else if (nor->info->no_sfdp_flags & SPI_NOR_SKIP_SFDP) {
		spi_nor_no_sfdp_init_params(nor);
	} else {
		spi_nor_init_params_deprecated(nor);
	}

	spi_nor_late_init_params(nor);

	return 0;
}

/** spi_nor_octal_dtr_enable() - enable Octal DTR I/O if needed
 * @nor:                 pointer to a 'struct spi_nor'
 * @enable:              whether to enable or disable Octal DTR
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_octal_dtr_enable(struct spi_nor *nor, bool enable)
{
	int ret;

	if (!nor->params->octal_dtr_enable)
		return 0;

	if (!(nor->read_proto == SNOR_PROTO_8_8_8_DTR &&
	      nor->write_proto == SNOR_PROTO_8_8_8_DTR))
		return 0;

	if (!(nor->flags & SNOR_F_IO_MODE_EN_VOLATILE))
		return 0;

	ret = nor->params->octal_dtr_enable(nor, enable);
	if (ret)
		return ret;

	if (enable)
		nor->reg_proto = SNOR_PROTO_8_8_8_DTR;
	else
		nor->reg_proto = SNOR_PROTO_1_1_1;

	return 0;
}

/**
 * spi_nor_quad_enable() - enable Quad I/O if needed.
 * @nor:                pointer to a 'struct spi_nor'
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_quad_enable(struct spi_nor *nor)
{
	if (!nor->params->quad_enable)
		return 0;

	if (!(spi_nor_get_protocol_width(nor->read_proto) == 4 ||
	      spi_nor_get_protocol_width(nor->write_proto) == 4))
		return 0;

	return nor->params->quad_enable(nor);
}

/**
 * spi_nor_set_4byte_addr_mode() - Set address mode.
 * @nor:                pointer to a 'struct spi_nor'.
 * @enable:             enable/disable 4 byte address mode.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_set_4byte_addr_mode(struct spi_nor *nor, bool enable)
{
	struct spi_nor_flash_parameter *params = nor->params;
	int ret;

	ret = params->set_4byte_addr_mode(nor, enable);
	if (ret && ret != -ENOTSUPP)
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

static int spi_nor_init(struct spi_nor *nor)
{
	int err;

	err = spi_nor_octal_dtr_enable(nor, true);
	if (err) {
		dev_dbg(nor->dev, "octal mode not supported\n");
		return err;
	}

	err = spi_nor_quad_enable(nor);
	if (err) {
		dev_dbg(nor->dev, "quad mode not supported\n");
		return err;
	}

	/*
	 * Some SPI NOR flashes are write protected by default after a power-on
	 * reset cycle, in order to avoid inadvertent writes during power-up.
	 * Backward compatibility imposes to unlock the entire flash memory
	 * array at power-up by default. Depending on the kernel configuration
	 * (1) do nothing, (2) always unlock the entire flash array or (3)
	 * unlock the entire flash array only when the software write
	 * protection bits are volatile. The latter is indicated by
	 * SNOR_F_SWP_IS_VOLATILE.
	 */
	if (IS_ENABLED(CONFIG_MTD_SPI_NOR_SWP_DISABLE) ||
	    (IS_ENABLED(CONFIG_MTD_SPI_NOR_SWP_DISABLE_ON_VOLATILE) &&
	     nor->flags & SNOR_F_SWP_IS_VOLATILE))
		spi_nor_try_unlock_all(nor);

	if (nor->addr_nbytes == 4 &&
	    nor->read_proto != SNOR_PROTO_8_8_8_DTR &&
	    !(nor->flags & SNOR_F_4B_OPCODES)) {
		/*
		 * If the RESET# pin isn't hooked up properly, or the system
		 * otherwise doesn't perform a reset command in the boot
		 * sequence, it's impossible to 100% protect against unexpected
		 * reboots (e.g., crashes). Warn the user (or hopefully, system
		 * designer) that this is bad.
		 */
		WARN_ONCE(nor->flags & SNOR_F_BROKEN_RESET,
			  "enabling reset hack; may not recover from unexpected reboots\n");
		err = spi_nor_set_4byte_addr_mode(nor, true);
		if (err)
			return err;
	}

	return 0;
}

/**
 * spi_nor_soft_reset() - Perform a software reset
 * @nor:	pointer to 'struct spi_nor'
 *
 * Performs a "Soft Reset and Enter Default Protocol Mode" sequence which resets
 * the device to its power-on-reset state. This is useful when the software has
 * made some changes to device (volatile) registers and needs to reset it before
 * shutting down, for example.
 *
 * Not every flash supports this sequence. The same set of opcodes might be used
 * for some other operation on a flash that does not support this. Support for
 * this sequence can be discovered via SFDP in the BFPT table.
 *
 * Return: 0 on success, -errno otherwise.
 */
static void spi_nor_soft_reset(struct spi_nor *nor)
{
	struct spi_mem_op op;
	int ret;

	op = (struct spi_mem_op)SPINOR_SRSTEN_OP;

	spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret) {
		dev_warn(nor->dev, "Software reset failed: %d\n", ret);
		return;
	}

	op = (struct spi_mem_op)SPINOR_SRST_OP;

	spi_nor_spimem_setup_op(nor, &op, nor->reg_proto);

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret) {
		dev_warn(nor->dev, "Software reset failed: %d\n", ret);
		return;
	}

	/*
	 * Software Reset is not instant, and the delay varies from flash to
	 * flash. Looking at a few flashes, most range somewhere below 100
	 * microseconds. So, sleep for a range of 200-400 us.
	 */
	usleep_range(SPI_NOR_SRST_SLEEP_MIN, SPI_NOR_SRST_SLEEP_MAX);
}

/* mtd suspend handler */
static int spi_nor_suspend(struct mtd_info *mtd)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	/* Disable octal DTR mode if we enabled it. */
	ret = spi_nor_octal_dtr_enable(nor, false);
	if (ret)
		dev_err(nor->dev, "suspend() failed\n");

	return ret;
}

/* mtd resume handler */
static void spi_nor_resume(struct mtd_info *mtd)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	struct device *dev = nor->dev;
	int ret;

	/* re-initialize the nor chip */
	ret = spi_nor_init(nor);
	if (ret)
		dev_err(dev, "resume() failed\n");
}

static int spi_nor_get_device(struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master(mtd);
	struct spi_nor *nor = mtd_to_spi_nor(master);
	struct device *dev;

	if (nor->spimem)
		dev = nor->spimem->spi->controller->dev.parent;
	else
		dev = nor->dev;

	if (!try_module_get(dev->driver->owner))
		return -ENODEV;

	return 0;
}

static void spi_nor_put_device(struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master(mtd);
	struct spi_nor *nor = mtd_to_spi_nor(master);
	struct device *dev;

	if (nor->spimem)
		dev = nor->spimem->spi->controller->dev.parent;
	else
		dev = nor->dev;

	module_put(dev->driver->owner);
}

static void spi_nor_restore(struct spi_nor *nor)
{
	int ret;

	/* restore the addressing mode */
	if (nor->addr_nbytes == 4 && !(nor->flags & SNOR_F_4B_OPCODES) &&
	    nor->flags & SNOR_F_BROKEN_RESET) {
		ret = spi_nor_set_4byte_addr_mode(nor, false);
		if (ret)
			/*
			 * Do not stop the execution in the hope that the flash
			 * will default to the 3-byte address mode after the
			 * software reset.
			 */
			dev_err(nor->dev, "Failed to exit 4-byte address mode, err = %d\n", ret);
	}

	if (nor->flags & SNOR_F_SOFT_RESET)
		spi_nor_soft_reset(nor);
}

static const struct flash_info *spi_nor_match_name(struct spi_nor *nor,
						   const char *name)
{
	unsigned int i, j;

	for (i = 0; i < ARRAY_SIZE(manufacturers); i++) {
		for (j = 0; j < manufacturers[i]->nparts; j++) {
			if (!strcmp(name, manufacturers[i]->parts[j].name)) {
				nor->manufacturer = manufacturers[i];
				return &manufacturers[i]->parts[j];
			}
		}
	}

	return NULL;
}

static const struct flash_info *spi_nor_get_flash_info(struct spi_nor *nor,
						       const char *name)
{
	const struct flash_info *info = NULL;

	if (name)
		info = spi_nor_match_name(nor, name);
	/* Try to auto-detect if chip name wasn't specified or not found */
	if (!info)
		return spi_nor_detect(nor);

	/*
	 * If caller has specified name of flash model that can normally be
	 * detected using JEDEC, let's verify it.
	 */
	if (name && info->id_len) {
		const struct flash_info *jinfo;

		jinfo = spi_nor_detect(nor);
		if (IS_ERR(jinfo)) {
			return jinfo;
		} else if (jinfo != info) {
			/*
			 * JEDEC knows better, so overwrite platform ID. We
			 * can't trust partitions any longer, but we'll let
			 * mtd apply them anyway, since some partitions may be
			 * marked read-only, and we don't want to loose that
			 * information, even if it's not 100% accurate.
			 */
			dev_warn(nor->dev, "found %s, expected %s\n",
				 jinfo->name, info->name);
			info = jinfo;
		}
	}

	return info;
}

static void spi_nor_set_mtd_info(struct spi_nor *nor)
{
	struct mtd_info *mtd = &nor->mtd;
	struct device *dev = nor->dev;

	spi_nor_set_mtd_locking_ops(nor);
	spi_nor_set_mtd_otp_ops(nor);

	mtd->dev.parent = dev;
	if (!mtd->name)
		mtd->name = dev_name(dev);
	mtd->type = MTD_NORFLASH;
	mtd->flags = MTD_CAP_NORFLASH;
	/* Unset BIT_WRITEABLE to enable JFFS2 write buffer for ECC'd NOR */
	if (nor->flags & SNOR_F_ECC)
		mtd->flags &= ~MTD_BIT_WRITEABLE;
	if (nor->info->flags & SPI_NOR_NO_ERASE)
		mtd->flags |= MTD_NO_ERASE;
	else
		mtd->_erase = spi_nor_erase;
	mtd->writesize = nor->params->writesize;
	mtd->writebufsize = nor->params->page_size;
	mtd->size = nor->params->size;
	mtd->_read = spi_nor_read;
	/* Might be already set by some SST flashes. */
	if (!mtd->_write)
		mtd->_write = spi_nor_write;
	mtd->_suspend = spi_nor_suspend;
	mtd->_resume = spi_nor_resume;
	mtd->_get_device = spi_nor_get_device;
	mtd->_put_device = spi_nor_put_device;
}

static int spi_nor_hw_reset(struct spi_nor *nor)
{
	struct gpio_desc *reset;

	reset = devm_gpiod_get_optional(nor->dev, "reset", GPIOD_OUT_LOW);
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

int spi_nor_scan(struct spi_nor *nor, const char *name,
		 const struct spi_nor_hwcaps *hwcaps)
{
	const struct flash_info *info;
	struct device *dev = nor->dev;
	struct mtd_info *mtd = &nor->mtd;
	int ret;
	int i;

	ret = spi_nor_check(nor);
	if (ret)
		return ret;

	/* Reset SPI protocol for all commands. */
	nor->reg_proto = SNOR_PROTO_1_1_1;
	nor->read_proto = SNOR_PROTO_1_1_1;
	nor->write_proto = SNOR_PROTO_1_1_1;

	/*
	 * We need the bounce buffer early to read/write registers when going
	 * through the spi-mem layer (buffers have to be DMA-able).
	 * For spi-mem drivers, we'll reallocate a new buffer if
	 * nor->params->page_size turns out to be greater than PAGE_SIZE (which
	 * shouldn't happen before long since NOR pages are usually less
	 * than 1KB) after spi_nor_scan() returns.
	 */
	nor->bouncebuf_size = PAGE_SIZE;
	nor->bouncebuf = devm_kmalloc(dev, nor->bouncebuf_size,
				      GFP_KERNEL);
	if (!nor->bouncebuf)
		return -ENOMEM;

	ret = spi_nor_hw_reset(nor);
	if (ret)
		return ret;

	info = spi_nor_get_flash_info(nor, name);
	if (IS_ERR(info))
		return PTR_ERR(info);

	nor->info = info;

	mutex_init(&nor->lock);

	/* Init flash parameters based on flash_info struct and SFDP */
	ret = spi_nor_init_params(nor);
	if (ret)
		return ret;

	if (spi_nor_use_parallel_locking(nor))
		init_waitqueue_head(&nor->rww.wait);

	/*
	 * Configure the SPI memory:
	 * - select op codes for (Fast) Read, Page Program and Sector Erase.
	 * - set the number of dummy cycles (mode cycles + wait states).
	 * - set the SPI protocols for register and memory accesses.
	 * - set the number of address bytes.
	 */
	ret = spi_nor_setup(nor, hwcaps);
	if (ret)
		return ret;

	/* Send all the required SPI flash commands to initialize device */
	ret = spi_nor_init(nor);
	if (ret)
		return ret;

	/* No mtd_info fields should be used up to this point. */
	spi_nor_set_mtd_info(nor);

	dev_info(dev, "%s (%lld Kbytes)\n", info->name,
			(long long)mtd->size >> 10);

	dev_dbg(dev,
		"mtd .name = %s, .size = 0x%llx (%lldMiB), "
		".erasesize = 0x%.8x (%uKiB) .numeraseregions = %d\n",
		mtd->name, (long long)mtd->size, (long long)(mtd->size >> 20),
		mtd->erasesize, mtd->erasesize / 1024, mtd->numeraseregions);

	if (mtd->numeraseregions)
		for (i = 0; i < mtd->numeraseregions; i++)
			dev_dbg(dev,
				"mtd.eraseregions[%d] = { .offset = 0x%llx, "
				".erasesize = 0x%.8x (%uKiB), "
				".numblocks = %d }\n",
				i, (long long)mtd->eraseregions[i].offset,
				mtd->eraseregions[i].erasesize,
				mtd->eraseregions[i].erasesize / 1024,
				mtd->eraseregions[i].numblocks);
	return 0;
}
EXPORT_SYMBOL_GPL(spi_nor_scan);

static int spi_nor_create_read_dirmap(struct spi_nor *nor)
{
	struct spi_mem_dirmap_info info = {
		.op_tmpl = SPI_MEM_OP(SPI_MEM_OP_CMD(nor->read_opcode, 0),
				      SPI_MEM_OP_ADDR(nor->addr_nbytes, 0, 0),
				      SPI_MEM_OP_DUMMY(nor->read_dummy, 0),
				      SPI_MEM_OP_DATA_IN(0, NULL, 0)),
		.offset = 0,
		.length = nor->params->size,
	};
	struct spi_mem_op *op = &info.op_tmpl;

	spi_nor_spimem_setup_op(nor, op, nor->read_proto);

	/* convert the dummy cycles to the number of bytes */
	op->dummy.nbytes = (nor->read_dummy * op->dummy.buswidth) / 8;
	if (spi_nor_protocol_is_dtr(nor->read_proto))
		op->dummy.nbytes *= 2;

	/*
	 * Since spi_nor_spimem_setup_op() only sets buswidth when the number
	 * of data bytes is non-zero, the data buswidth won't be set here. So,
	 * do it explicitly.
	 */
	op->data.buswidth = spi_nor_get_protocol_data_nbits(nor->read_proto);

	nor->dirmap.rdesc = devm_spi_mem_dirmap_create(nor->dev, nor->spimem,
						       &info);
	return PTR_ERR_OR_ZERO(nor->dirmap.rdesc);
}

static int spi_nor_create_write_dirmap(struct spi_nor *nor)
{
	struct spi_mem_dirmap_info info = {
		.op_tmpl = SPI_MEM_OP(SPI_MEM_OP_CMD(nor->program_opcode, 0),
				      SPI_MEM_OP_ADDR(nor->addr_nbytes, 0, 0),
				      SPI_MEM_OP_NO_DUMMY,
				      SPI_MEM_OP_DATA_OUT(0, NULL, 0)),
		.offset = 0,
		.length = nor->params->size,
	};
	struct spi_mem_op *op = &info.op_tmpl;

	if (nor->program_opcode == SPINOR_OP_AAI_WP && nor->sst_write_second)
		op->addr.nbytes = 0;

	spi_nor_spimem_setup_op(nor, op, nor->write_proto);

	/*
	 * Since spi_nor_spimem_setup_op() only sets buswidth when the number
	 * of data bytes is non-zero, the data buswidth won't be set here. So,
	 * do it explicitly.
	 */
	op->data.buswidth = spi_nor_get_protocol_data_nbits(nor->write_proto);

	nor->dirmap.wdesc = devm_spi_mem_dirmap_create(nor->dev, nor->spimem,
						       &info);
	return PTR_ERR_OR_ZERO(nor->dirmap.wdesc);
}

static int spi_nor_probe(struct spi_mem *spimem)
{
	struct spi_device *spi = spimem->spi;
	struct flash_platform_data *data = dev_get_platdata(&spi->dev);
	struct spi_nor *nor;
	/*
	 * Enable all caps by default. The core will mask them after
	 * checking what's really supported using spi_mem_supports_op().
	 */
	const struct spi_nor_hwcaps hwcaps = { .mask = SNOR_HWCAPS_ALL };
	char *flash_name;
	int ret;

	nor = devm_kzalloc(&spi->dev, sizeof(*nor), GFP_KERNEL);
	if (!nor)
		return -ENOMEM;

	nor->spimem = spimem;
	nor->dev = &spi->dev;
	spi_nor_set_flash_node(nor, spi->dev.of_node);

	spi_mem_set_drvdata(spimem, nor);

	if (data && data->name)
		nor->mtd.name = data->name;

	if (!nor->mtd.name)
		nor->mtd.name = spi_mem_get_name(spimem);

	/*
	 * For some (historical?) reason many platforms provide two different
	 * names in flash_platform_data: "name" and "type". Quite often name is
	 * set to "m25p80" and then "type" provides a real chip name.
	 * If that's the case, respect "type" and ignore a "name".
	 */
	if (data && data->type)
		flash_name = data->type;
	else if (!strcmp(spi->modalias, "spi-nor"))
		flash_name = NULL; /* auto-detect */
	else
		flash_name = spi->modalias;

	ret = spi_nor_scan(nor, flash_name, &hwcaps);
	if (ret)
		return ret;

	spi_nor_debugfs_register(nor);

	/*
	 * None of the existing parts have > 512B pages, but let's play safe
	 * and add this logic so that if anyone ever adds support for such
	 * a NOR we don't end up with buffer overflows.
	 */
	if (nor->params->page_size > PAGE_SIZE) {
		nor->bouncebuf_size = nor->params->page_size;
		devm_kfree(nor->dev, nor->bouncebuf);
		nor->bouncebuf = devm_kmalloc(nor->dev,
					      nor->bouncebuf_size,
					      GFP_KERNEL);
		if (!nor->bouncebuf)
			return -ENOMEM;
	}

	ret = spi_nor_create_read_dirmap(nor);
	if (ret)
		return ret;

	ret = spi_nor_create_write_dirmap(nor);
	if (ret)
		return ret;

	return mtd_device_register(&nor->mtd, data ? data->parts : NULL,
				   data ? data->nr_parts : 0);
}

static int spi_nor_remove(struct spi_mem *spimem)
{
	struct spi_nor *nor = spi_mem_get_drvdata(spimem);

	spi_nor_restore(nor);

	/* Clean up MTD stuff. */
	return mtd_device_unregister(&nor->mtd);
}

static void spi_nor_shutdown(struct spi_mem *spimem)
{
	struct spi_nor *nor = spi_mem_get_drvdata(spimem);

	spi_nor_restore(nor);
}

/*
 * Do NOT add to this array without reading the following:
 *
 * Historically, many flash devices are bound to this driver by their name. But
 * since most of these flash are compatible to some extent, and their
 * differences can often be differentiated by the JEDEC read-ID command, we
 * encourage new users to add support to the spi-nor library, and simply bind
 * against a generic string here (e.g., "jedec,spi-nor").
 *
 * Many flash names are kept here in this list to keep them available
 * as module aliases for existing platforms.
 */
static const struct spi_device_id spi_nor_dev_ids[] = {
	/*
	 * Allow non-DT platform devices to bind to the "spi-nor" modalias, and
	 * hack around the fact that the SPI core does not provide uevent
	 * matching for .of_match_table
	 */
	{"spi-nor"},

	/*
	 * Entries not used in DTs that should be safe to drop after replacing
	 * them with "spi-nor" in platform data.
	 */
	{"s25sl064a"},	{"w25x16"},	{"m25p10"},	{"m25px64"},

	/*
	 * Entries that were used in DTs without "jedec,spi-nor" fallback and
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
	{"m25p05-nonjedec"},	{"m25p10-nonjedec"},	{"m25p20-nonjedec"},
	{"m25p40-nonjedec"},	{"m25p80-nonjedec"},	{"m25p16-nonjedec"},
	{"m25p32-nonjedec"},	{"m25p64-nonjedec"},	{"m25p128-nonjedec"},

	/* Everspin MRAMs (non-JEDEC) */
	{ "mr25h128" }, /* 128 Kib, 40 MHz */
	{ "mr25h256" }, /* 256 Kib, 40 MHz */
	{ "mr25h10" },  /*   1 Mib, 40 MHz */
	{ "mr25h40" },  /*   4 Mib, 40 MHz */

	{ },
};
MODULE_DEVICE_TABLE(spi, spi_nor_dev_ids);

static const struct of_device_id spi_nor_of_table[] = {
	/*
	 * Generic compatibility for SPI NOR that can be identified by the
	 * JEDEC READ ID opcode (0x9F). Use this, if possible.
	 */
	{ .compatible = "jedec,spi-nor" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, spi_nor_of_table);

/*
 * REVISIT: many of these chips have deep power-down modes, which
 * should clearly be entered on suspend() to minimize power use.
 * And also when they're otherwise idle...
 */
static struct spi_mem_driver spi_nor_driver = {
	.spidrv = {
		.driver = {
			.name = "spi-nor",
			.of_match_table = spi_nor_of_table,
			.dev_groups = spi_nor_sysfs_groups,
		},
		.id_table = spi_nor_dev_ids,
	},
	.probe = spi_nor_probe,
	.remove = spi_nor_remove,
	.shutdown = spi_nor_shutdown,
};

static int __init spi_nor_module_init(void)
{
	return spi_mem_driver_register(&spi_nor_driver);
}
module_init(spi_nor_module_init);

static void __exit spi_nor_module_exit(void)
{
	spi_mem_driver_unregister(&spi_nor_driver);
	spi_nor_debugfs_shutdown();
}
module_exit(spi_nor_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Huang Shijie <shijie8@gmail.com>");
MODULE_AUTHOR("Mike Lavender");
MODULE_DESCRIPTION("framework for SPI NOR");
