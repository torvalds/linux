/*
 *  ms_block.c - Sony MemoryStick (legacy) storage support

 *  Copyright (C) 2013 Maxim Levitsky <maximlevitsky@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Minor portions of the driver were copied from mspro_block.c which is
 * Copyright (C) 2007 Alex Dubov <oakad@yahoo.com>
 *
 */
#define DRIVER_NAME "ms_block"
#define pr_fmt(fmt) DRIVER_NAME ": " fmt

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/memstick.h>
#include <linux/idr.h>
#include <linux/hdreg.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/bitmap.h>
#include <linux/scatterlist.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include "ms_block.h"

static int debug;
static int cache_flush_timeout = 1000;
static bool verify_writes;

/*
 * Copies section of 'sg_from' starting from offset 'offset' and with length
 * 'len' To another scatterlist of to_nents enties
 */
static size_t msb_sg_copy(struct scatterlist *sg_from,
	struct scatterlist *sg_to, int to_nents, size_t offset, size_t len)
{
	size_t copied = 0;

	while (offset > 0) {
		if (offset >= sg_from->length) {
			if (sg_is_last(sg_from))
				return 0;

			offset -= sg_from->length;
			sg_from = sg_next(sg_from);
			continue;
		}

		copied = min(len, sg_from->length - offset);
		sg_set_page(sg_to, sg_page(sg_from),
			copied, sg_from->offset + offset);

		len -= copied;
		offset = 0;

		if (sg_is_last(sg_from) || !len)
			goto out;

		sg_to = sg_next(sg_to);
		to_nents--;
		sg_from = sg_next(sg_from);
	}

	while (len > sg_from->length && to_nents--) {
		len -= sg_from->length;
		copied += sg_from->length;

		sg_set_page(sg_to, sg_page(sg_from),
				sg_from->length, sg_from->offset);

		if (sg_is_last(sg_from) || !len)
			goto out;

		sg_from = sg_next(sg_from);
		sg_to = sg_next(sg_to);
	}

	if (len && to_nents) {
		sg_set_page(sg_to, sg_page(sg_from), len, sg_from->offset);
		copied += len;
	}
out:
	sg_mark_end(sg_to);
	return copied;
}

/*
 * Compares section of 'sg' starting from offset 'offset' and with length 'len'
 * to linear buffer of length 'len' at address 'buffer'
 * Returns 0 if equal and  -1 otherwice
 */
static int msb_sg_compare_to_buffer(struct scatterlist *sg,
					size_t offset, u8 *buffer, size_t len)
{
	int retval = 0, cmplen;
	struct sg_mapping_iter miter;

	sg_miter_start(&miter, sg, sg_nents(sg),
					SG_MITER_ATOMIC | SG_MITER_FROM_SG);

	while (sg_miter_next(&miter) && len > 0) {
		if (offset >= miter.length) {
			offset -= miter.length;
			continue;
		}

		cmplen = min(miter.length - offset, len);
		retval = memcmp(miter.addr + offset, buffer, cmplen) ? -1 : 0;
		if (retval)
			break;

		buffer += cmplen;
		len -= cmplen;
		offset = 0;
	}

	if (!retval && len)
		retval = -1;

	sg_miter_stop(&miter);
	return retval;
}


/* Get zone at which block with logical address 'lba' lives
 * Flash is broken into zones.
 * Each zone consists of 512 eraseblocks, out of which in first
 * zone 494 are used and 496 are for all following zones.
 * Therefore zone #0 hosts blocks 0-493, zone #1 blocks 494-988, etc...
*/
static int msb_get_zone_from_lba(int lba)
{
	if (lba < 494)
		return 0;
	return ((lba - 494) / 496) + 1;
}

/* Get zone of physical block. Trivial */
static int msb_get_zone_from_pba(int pba)
{
	return pba / MS_BLOCKS_IN_ZONE;
}

/* Debug test to validate free block counts */
static int msb_validate_used_block_bitmap(struct msb_data *msb)
{
	int total_free_blocks = 0;
	int i;

	if (!debug)
		return 0;

	for (i = 0; i < msb->zone_count; i++)
		total_free_blocks += msb->free_block_count[i];

	if (msb->block_count - bitmap_weight(msb->used_blocks_bitmap,
					msb->block_count) == total_free_blocks)
		return 0;

	pr_err("BUG: free block counts don't match the bitmap");
	msb->read_only = true;
	return -EINVAL;
}

/* Mark physical block as used */
static void msb_mark_block_used(struct msb_data *msb, int pba)
{
	int zone = msb_get_zone_from_pba(pba);

	if (test_bit(pba, msb->used_blocks_bitmap)) {
		pr_err(
		"BUG: attempt to mark already used pba %d as used", pba);
		msb->read_only = true;
		return;
	}

	if (msb_validate_used_block_bitmap(msb))
		return;

	/* No races because all IO is single threaded */
	__set_bit(pba, msb->used_blocks_bitmap);
	msb->free_block_count[zone]--;
}

/* Mark physical block as free */
static void msb_mark_block_unused(struct msb_data *msb, int pba)
{
	int zone = msb_get_zone_from_pba(pba);

	if (!test_bit(pba, msb->used_blocks_bitmap)) {
		pr_err("BUG: attempt to mark already unused pba %d as unused" , pba);
		msb->read_only = true;
		return;
	}

	if (msb_validate_used_block_bitmap(msb))
		return;

	/* No races because all IO is single threaded */
	__clear_bit(pba, msb->used_blocks_bitmap);
	msb->free_block_count[zone]++;
}

/* Invalidate current register window */
static void msb_invalidate_reg_window(struct msb_data *msb)
{
	msb->reg_addr.w_offset = offsetof(struct ms_register, id);
	msb->reg_addr.w_length = sizeof(struct ms_id_register);
	msb->reg_addr.r_offset = offsetof(struct ms_register, id);
	msb->reg_addr.r_length = sizeof(struct ms_id_register);
	msb->addr_valid = false;
}

/* Start a state machine */
static int msb_run_state_machine(struct msb_data *msb, int   (*state_func)
		(struct memstick_dev *card, struct memstick_request **req))
{
	struct memstick_dev *card = msb->card;

	WARN_ON(msb->state != -1);
	msb->int_polling = false;
	msb->state = 0;
	msb->exit_error = 0;

	memset(&card->current_mrq, 0, sizeof(card->current_mrq));

	card->next_request = state_func;
	memstick_new_req(card->host);
	wait_for_completion(&card->mrq_complete);

	WARN_ON(msb->state != -1);
	return msb->exit_error;
}

/* State machines call that to exit */
static int msb_exit_state_machine(struct msb_data *msb, int error)
{
	WARN_ON(msb->state == -1);

	msb->state = -1;
	msb->exit_error = error;
	msb->card->next_request = h_msb_default_bad;

	/* Invalidate reg window on errors */
	if (error)
		msb_invalidate_reg_window(msb);

	complete(&msb->card->mrq_complete);
	return -ENXIO;
}

/* read INT register */
static int msb_read_int_reg(struct msb_data *msb, long timeout)
{
	struct memstick_request *mrq = &msb->card->current_mrq;

	WARN_ON(msb->state == -1);

	if (!msb->int_polling) {
		msb->int_timeout = jiffies +
			msecs_to_jiffies(timeout == -1 ? 500 : timeout);
		msb->int_polling = true;
	} else if (time_after(jiffies, msb->int_timeout)) {
		mrq->data[0] = MEMSTICK_INT_CMDNAK;
		return 0;
	}

	if ((msb->caps & MEMSTICK_CAP_AUTO_GET_INT) &&
				mrq->need_card_int && !mrq->error) {
		mrq->data[0] = mrq->int_reg;
		mrq->need_card_int = false;
		return 0;
	} else {
		memstick_init_req(mrq, MS_TPC_GET_INT, NULL, 1);
		return 1;
	}
}

/* Read a register */
static int msb_read_regs(struct msb_data *msb, int offset, int len)
{
	struct memstick_request *req = &msb->card->current_mrq;

	if (msb->reg_addr.r_offset != offset ||
	    msb->reg_addr.r_length != len || !msb->addr_valid) {

		msb->reg_addr.r_offset = offset;
		msb->reg_addr.r_length = len;
		msb->addr_valid = true;

		memstick_init_req(req, MS_TPC_SET_RW_REG_ADRS,
			&msb->reg_addr, sizeof(msb->reg_addr));
		return 0;
	}

	memstick_init_req(req, MS_TPC_READ_REG, NULL, len);
	return 1;
}

/* Write a card register */
static int msb_write_regs(struct msb_data *msb, int offset, int len, void *buf)
{
	struct memstick_request *req = &msb->card->current_mrq;

	if (msb->reg_addr.w_offset != offset ||
		msb->reg_addr.w_length != len  || !msb->addr_valid) {

		msb->reg_addr.w_offset = offset;
		msb->reg_addr.w_length = len;
		msb->addr_valid = true;

		memstick_init_req(req, MS_TPC_SET_RW_REG_ADRS,
			&msb->reg_addr, sizeof(msb->reg_addr));
		return 0;
	}

	memstick_init_req(req, MS_TPC_WRITE_REG, buf, len);
	return 1;
}

/* Handler for absence of IO */
static int h_msb_default_bad(struct memstick_dev *card,
						struct memstick_request **mrq)
{
	return -ENXIO;
}

/*
 * This function is a handler for reads of one page from device.
 * Writes output to msb->current_sg, takes sector address from msb->reg.param
 * Can also be used to read extra data only. Set params accordintly.
 */
static int h_msb_read_page(struct memstick_dev *card,
					struct memstick_request **out_mrq)
{
	struct msb_data *msb = memstick_get_drvdata(card);
	struct memstick_request *mrq = *out_mrq = &card->current_mrq;
	struct scatterlist sg[2];
	u8 command, intreg;

	if (mrq->error) {
		dbg("read_page, unknown error");
		return msb_exit_state_machine(msb, mrq->error);
	}
again:
	switch (msb->state) {
	case MSB_RP_SEND_BLOCK_ADDRESS:
		/* msb_write_regs sometimes "fails" because it needs to update
			the reg window, and thus it returns request for that.
			Then we stay in this state and retry */
		if (!msb_write_regs(msb,
			offsetof(struct ms_register, param),
			sizeof(struct ms_param_register),
			(unsigned char *)&msb->regs.param))
			return 0;

		msb->state = MSB_RP_SEND_READ_COMMAND;
		return 0;

	case MSB_RP_SEND_READ_COMMAND:
		command = MS_CMD_BLOCK_READ;
		memstick_init_req(mrq, MS_TPC_SET_CMD, &command, 1);
		msb->state = MSB_RP_SEND_INT_REQ;
		return 0;

	case MSB_RP_SEND_INT_REQ:
		msb->state = MSB_RP_RECEIVE_INT_REQ_RESULT;
		/* If dont actually need to send the int read request (only in
			serial mode), then just fall through */
		if (msb_read_int_reg(msb, -1))
			return 0;
		/* fallthrough */

	case MSB_RP_RECEIVE_INT_REQ_RESULT:
		intreg = mrq->data[0];
		msb->regs.status.interrupt = intreg;

		if (intreg & MEMSTICK_INT_CMDNAK)
			return msb_exit_state_machine(msb, -EIO);

		if (!(intreg & MEMSTICK_INT_CED)) {
			msb->state = MSB_RP_SEND_INT_REQ;
			goto again;
		}

		msb->int_polling = false;
		msb->state = (intreg & MEMSTICK_INT_ERR) ?
			MSB_RP_SEND_READ_STATUS_REG : MSB_RP_SEND_OOB_READ;
		goto again;

	case MSB_RP_SEND_READ_STATUS_REG:
		 /* read the status register to understand source of the INT_ERR */
		if (!msb_read_regs(msb,
			offsetof(struct ms_register, status),
			sizeof(struct ms_status_register)))
			return 0;

		msb->state = MSB_RP_RECEIVE_STATUS_REG;
		return 0;

	case MSB_RP_RECEIVE_STATUS_REG:
		msb->regs.status = *(struct ms_status_register *)mrq->data;
		msb->state = MSB_RP_SEND_OOB_READ;
		/* fallthrough */

	case MSB_RP_SEND_OOB_READ:
		if (!msb_read_regs(msb,
			offsetof(struct ms_register, extra_data),
			sizeof(struct ms_extra_data_register)))
			return 0;

		msb->state = MSB_RP_RECEIVE_OOB_READ;
		return 0;

	case MSB_RP_RECEIVE_OOB_READ:
		msb->regs.extra_data =
			*(struct ms_extra_data_register *) mrq->data;
		msb->state = MSB_RP_SEND_READ_DATA;
		/* fallthrough */

	case MSB_RP_SEND_READ_DATA:
		/* Skip that state if we only read the oob */
		if (msb->regs.param.cp == MEMSTICK_CP_EXTRA) {
			msb->state = MSB_RP_RECEIVE_READ_DATA;
			goto again;
		}

		sg_init_table(sg, ARRAY_SIZE(sg));
		msb_sg_copy(msb->current_sg, sg, ARRAY_SIZE(sg),
			msb->current_sg_offset,
			msb->page_size);

		memstick_init_req_sg(mrq, MS_TPC_READ_LONG_DATA, sg);
		msb->state = MSB_RP_RECEIVE_READ_DATA;
		return 0;

	case MSB_RP_RECEIVE_READ_DATA:
		if (!(msb->regs.status.interrupt & MEMSTICK_INT_ERR)) {
			msb->current_sg_offset += msb->page_size;
			return msb_exit_state_machine(msb, 0);
		}

		if (msb->regs.status.status1 & MEMSTICK_UNCORR_ERROR) {
			dbg("read_page: uncorrectable error");
			return msb_exit_state_machine(msb, -EBADMSG);
		}

		if (msb->regs.status.status1 & MEMSTICK_CORR_ERROR) {
			dbg("read_page: correctable error");
			msb->current_sg_offset += msb->page_size;
			return msb_exit_state_machine(msb, -EUCLEAN);
		} else {
			dbg("read_page: INT error, but no status error bits");
			return msb_exit_state_machine(msb, -EIO);
		}
	}

	BUG();
}

/*
 * Handler of writes of exactly one block.
 * Takes address from msb->regs.param.
 * Writes same extra data to blocks, also taken
 * from msb->regs.extra
 * Returns -EBADMSG if write fails due to uncorrectable error, or -EIO if
 * device refuses to take the command or something else
 */
static int h_msb_write_block(struct memstick_dev *card,
					struct memstick_request **out_mrq)
{
	struct msb_data *msb = memstick_get_drvdata(card);
	struct memstick_request *mrq = *out_mrq = &card->current_mrq;
	struct scatterlist sg[2];
	u8 intreg, command;

	if (mrq->error)
		return msb_exit_state_machine(msb, mrq->error);

again:
	switch (msb->state) {

	/* HACK: Jmicon handling of TPCs between 8 and
	 *	sizeof(memstick_request.data) is broken due to hardware
	 *	bug in PIO mode that is used for these TPCs
	 *	Therefore split the write
	 */

	case MSB_WB_SEND_WRITE_PARAMS:
		if (!msb_write_regs(msb,
			offsetof(struct ms_register, param),
			sizeof(struct ms_param_register),
			&msb->regs.param))
			return 0;

		msb->state = MSB_WB_SEND_WRITE_OOB;
		return 0;

	case MSB_WB_SEND_WRITE_OOB:
		if (!msb_write_regs(msb,
			offsetof(struct ms_register, extra_data),
			sizeof(struct ms_extra_data_register),
			&msb->regs.extra_data))
			return 0;
		msb->state = MSB_WB_SEND_WRITE_COMMAND;
		return 0;


	case MSB_WB_SEND_WRITE_COMMAND:
		command = MS_CMD_BLOCK_WRITE;
		memstick_init_req(mrq, MS_TPC_SET_CMD, &command, 1);
		msb->state = MSB_WB_SEND_INT_REQ;
		return 0;

	case MSB_WB_SEND_INT_REQ:
		msb->state = MSB_WB_RECEIVE_INT_REQ;
		if (msb_read_int_reg(msb, -1))
			return 0;
		/* fallthrough */

	case MSB_WB_RECEIVE_INT_REQ:
		intreg = mrq->data[0];
		msb->regs.status.interrupt = intreg;

		/* errors mean out of here, and fast... */
		if (intreg & (MEMSTICK_INT_CMDNAK))
			return msb_exit_state_machine(msb, -EIO);

		if (intreg & MEMSTICK_INT_ERR)
			return msb_exit_state_machine(msb, -EBADMSG);


		/* for last page we need to poll CED */
		if (msb->current_page == msb->pages_in_block) {
			if (intreg & MEMSTICK_INT_CED)
				return msb_exit_state_machine(msb, 0);
			msb->state = MSB_WB_SEND_INT_REQ;
			goto again;

		}

		/* for non-last page we need BREQ before writing next chunk */
		if (!(intreg & MEMSTICK_INT_BREQ)) {
			msb->state = MSB_WB_SEND_INT_REQ;
			goto again;
		}

		msb->int_polling = false;
		msb->state = MSB_WB_SEND_WRITE_DATA;
		/* fallthrough */

	case MSB_WB_SEND_WRITE_DATA:
		sg_init_table(sg, ARRAY_SIZE(sg));

		if (msb_sg_copy(msb->current_sg, sg, ARRAY_SIZE(sg),
			msb->current_sg_offset,
			msb->page_size) < msb->page_size)
			return msb_exit_state_machine(msb, -EIO);

		memstick_init_req_sg(mrq, MS_TPC_WRITE_LONG_DATA, sg);
		mrq->need_card_int = 1;
		msb->state = MSB_WB_RECEIVE_WRITE_CONFIRMATION;
		return 0;

	case MSB_WB_RECEIVE_WRITE_CONFIRMATION:
		msb->current_page++;
		msb->current_sg_offset += msb->page_size;
		msb->state = MSB_WB_SEND_INT_REQ;
		goto again;
	default:
		BUG();
	}

	return 0;
}

/*
 * This function is used to send simple IO requests to device that consist
 * of register write + command
 */
static int h_msb_send_command(struct memstick_dev *card,
					struct memstick_request **out_mrq)
{
	struct msb_data *msb = memstick_get_drvdata(card);
	struct memstick_request *mrq = *out_mrq = &card->current_mrq;
	u8 intreg;

	if (mrq->error) {
		dbg("send_command: unknown error");
		return msb_exit_state_machine(msb, mrq->error);
	}
again:
	switch (msb->state) {

	/* HACK: see h_msb_write_block */
	case MSB_SC_SEND_WRITE_PARAMS: /* write param register*/
		if (!msb_write_regs(msb,
			offsetof(struct ms_register, param),
			sizeof(struct ms_param_register),
			&msb->regs.param))
			return 0;
		msb->state = MSB_SC_SEND_WRITE_OOB;
		return 0;

	case MSB_SC_SEND_WRITE_OOB:
		if (!msb->command_need_oob) {
			msb->state = MSB_SC_SEND_COMMAND;
			goto again;
		}

		if (!msb_write_regs(msb,
			offsetof(struct ms_register, extra_data),
			sizeof(struct ms_extra_data_register),
			&msb->regs.extra_data))
			return 0;

		msb->state = MSB_SC_SEND_COMMAND;
		return 0;

	case MSB_SC_SEND_COMMAND:
		memstick_init_req(mrq, MS_TPC_SET_CMD, &msb->command_value, 1);
		msb->state = MSB_SC_SEND_INT_REQ;
		return 0;

	case MSB_SC_SEND_INT_REQ:
		msb->state = MSB_SC_RECEIVE_INT_REQ;
		if (msb_read_int_reg(msb, -1))
			return 0;
		/* fallthrough */

	case MSB_SC_RECEIVE_INT_REQ:
		intreg = mrq->data[0];

		if (intreg & MEMSTICK_INT_CMDNAK)
			return msb_exit_state_machine(msb, -EIO);
		if (intreg & MEMSTICK_INT_ERR)
			return msb_exit_state_machine(msb, -EBADMSG);

		if (!(intreg & MEMSTICK_INT_CED)) {
			msb->state = MSB_SC_SEND_INT_REQ;
			goto again;
		}

		return msb_exit_state_machine(msb, 0);
	}

	BUG();
}

/* Small handler for card reset */
static int h_msb_reset(struct memstick_dev *card,
					struct memstick_request **out_mrq)
{
	u8 command = MS_CMD_RESET;
	struct msb_data *msb = memstick_get_drvdata(card);
	struct memstick_request *mrq = *out_mrq = &card->current_mrq;

	if (mrq->error)
		return msb_exit_state_machine(msb, mrq->error);

	switch (msb->state) {
	case MSB_RS_SEND:
		memstick_init_req(mrq, MS_TPC_SET_CMD, &command, 1);
		mrq->need_card_int = 0;
		msb->state = MSB_RS_CONFIRM;
		return 0;
	case MSB_RS_CONFIRM:
		return msb_exit_state_machine(msb, 0);
	}
	BUG();
}

/* This handler is used to do serial->parallel switch */
static int h_msb_parallel_switch(struct memstick_dev *card,
					struct memstick_request **out_mrq)
{
	struct msb_data *msb = memstick_get_drvdata(card);
	struct memstick_request *mrq = *out_mrq = &card->current_mrq;
	struct memstick_host *host = card->host;

	if (mrq->error) {
		dbg("parallel_switch: error");
		msb->regs.param.system &= ~MEMSTICK_SYS_PAM;
		return msb_exit_state_machine(msb, mrq->error);
	}

	switch (msb->state) {
	case MSB_PS_SEND_SWITCH_COMMAND:
		/* Set the parallel interface on memstick side */
		msb->regs.param.system |= MEMSTICK_SYS_PAM;

		if (!msb_write_regs(msb,
			offsetof(struct ms_register, param),
			1,
			(unsigned char *)&msb->regs.param))
			return 0;

		msb->state = MSB_PS_SWICH_HOST;
		return 0;

	case MSB_PS_SWICH_HOST:
		 /* Set parallel interface on our side + send a dummy request
			to see if card responds */
		host->set_param(host, MEMSTICK_INTERFACE, MEMSTICK_PAR4);
		memstick_init_req(mrq, MS_TPC_GET_INT, NULL, 1);
		msb->state = MSB_PS_CONFIRM;
		return 0;

	case MSB_PS_CONFIRM:
		return msb_exit_state_machine(msb, 0);
	}

	BUG();
}

static int msb_switch_to_parallel(struct msb_data *msb);

/* Reset the card, to guard against hw errors beeing treated as bad blocks */
static int msb_reset(struct msb_data *msb, bool full)
{

	bool was_parallel = msb->regs.param.system & MEMSTICK_SYS_PAM;
	struct memstick_dev *card = msb->card;
	struct memstick_host *host = card->host;
	int error;

	/* Reset the card */
	msb->regs.param.system = MEMSTICK_SYS_BAMD;

	if (full) {
		error =  host->set_param(host,
					MEMSTICK_POWER, MEMSTICK_POWER_OFF);
		if (error)
			goto out_error;

		msb_invalidate_reg_window(msb);

		error = host->set_param(host,
					MEMSTICK_POWER, MEMSTICK_POWER_ON);
		if (error)
			goto out_error;

		error = host->set_param(host,
					MEMSTICK_INTERFACE, MEMSTICK_SERIAL);
		if (error) {
out_error:
			dbg("Failed to reset the host controller");
			msb->read_only = true;
			return -EFAULT;
		}
	}

	error = msb_run_state_machine(msb, h_msb_reset);
	if (error) {
		dbg("Failed to reset the card");
		msb->read_only = true;
		return -ENODEV;
	}

	/* Set parallel mode */
	if (was_parallel)
		msb_switch_to_parallel(msb);
	return 0;
}

/* Attempts to switch interface to parallel mode */
static int msb_switch_to_parallel(struct msb_data *msb)
{
	int error;

	error = msb_run_state_machine(msb, h_msb_parallel_switch);
	if (error) {
		pr_err("Switch to parallel failed");
		msb->regs.param.system &= ~MEMSTICK_SYS_PAM;
		msb_reset(msb, true);
		return -EFAULT;
	}

	msb->caps |= MEMSTICK_CAP_AUTO_GET_INT;
	return 0;
}

/* Changes overwrite flag on a page */
static int msb_set_overwrite_flag(struct msb_data *msb,
						u16 pba, u8 page, u8 flag)
{
	if (msb->read_only)
		return -EROFS;

	msb->regs.param.block_address = cpu_to_be16(pba);
	msb->regs.param.page_address = page;
	msb->regs.param.cp = MEMSTICK_CP_OVERWRITE;
	msb->regs.extra_data.overwrite_flag = flag;
	msb->command_value = MS_CMD_BLOCK_WRITE;
	msb->command_need_oob = true;

	dbg_verbose("changing overwrite flag to %02x for sector %d, page %d",
							flag, pba, page);
	return msb_run_state_machine(msb, h_msb_send_command);
}

static int msb_mark_bad(struct msb_data *msb, int pba)
{
	pr_notice("marking pba %d as bad", pba);
	msb_reset(msb, true);
	return msb_set_overwrite_flag(
			msb, pba, 0, 0xFF & ~MEMSTICK_OVERWRITE_BKST);
}

static int msb_mark_page_bad(struct msb_data *msb, int pba, int page)
{
	dbg("marking page %d of pba %d as bad", page, pba);
	msb_reset(msb, true);
	return msb_set_overwrite_flag(msb,
		pba, page, ~MEMSTICK_OVERWRITE_PGST0);
}

/* Erases one physical block */
static int msb_erase_block(struct msb_data *msb, u16 pba)
{
	int error, try;
	if (msb->read_only)
		return -EROFS;

	dbg_verbose("erasing pba %d", pba);

	for (try = 1; try < 3; try++) {
		msb->regs.param.block_address = cpu_to_be16(pba);
		msb->regs.param.page_address = 0;
		msb->regs.param.cp = MEMSTICK_CP_BLOCK;
		msb->command_value = MS_CMD_BLOCK_ERASE;
		msb->command_need_oob = false;


		error = msb_run_state_machine(msb, h_msb_send_command);
		if (!error || msb_reset(msb, true))
			break;
	}

	if (error) {
		pr_err("erase failed, marking pba %d as bad", pba);
		msb_mark_bad(msb, pba);
	}

	dbg_verbose("erase success, marking pba %d as unused", pba);
	msb_mark_block_unused(msb, pba);
	__set_bit(pba, msb->erased_blocks_bitmap);
	return error;
}

/* Reads one page from device */
static int msb_read_page(struct msb_data *msb,
	u16 pba, u8 page, struct ms_extra_data_register *extra,
					struct scatterlist *sg,  int offset)
{
	int try, error;

	if (pba == MS_BLOCK_INVALID) {
		unsigned long flags;
		struct sg_mapping_iter miter;
		size_t len = msb->page_size;

		dbg_verbose("read unmapped sector. returning 0xFF");

		local_irq_save(flags);
		sg_miter_start(&miter, sg, sg_nents(sg),
				SG_MITER_ATOMIC | SG_MITER_TO_SG);

		while (sg_miter_next(&miter) && len > 0) {

			int chunklen;

			if (offset && offset >= miter.length) {
				offset -= miter.length;
				continue;
			}

			chunklen = min(miter.length - offset, len);
			memset(miter.addr + offset, 0xFF, chunklen);
			len -= chunklen;
			offset = 0;
		}

		sg_miter_stop(&miter);
		local_irq_restore(flags);

		if (offset)
			return -EFAULT;

		if (extra)
			memset(extra, 0xFF, sizeof(*extra));
		return 0;
	}

	if (pba >= msb->block_count) {
		pr_err("BUG: attempt to read beyond the end of the card at pba %d", pba);
		return -EINVAL;
	}

	for (try = 1; try < 3; try++) {
		msb->regs.param.block_address = cpu_to_be16(pba);
		msb->regs.param.page_address = page;
		msb->regs.param.cp = MEMSTICK_CP_PAGE;

		msb->current_sg = sg;
		msb->current_sg_offset = offset;
		error = msb_run_state_machine(msb, h_msb_read_page);


		if (error == -EUCLEAN) {
			pr_notice("correctable error on pba %d, page %d",
				pba, page);
			error = 0;
		}

		if (!error && extra)
			*extra = msb->regs.extra_data;

		if (!error || msb_reset(msb, true))
			break;

	}

	/* Mark bad pages */
	if (error == -EBADMSG) {
		pr_err("uncorrectable error on read of pba %d, page %d",
			pba, page);

		if (msb->regs.extra_data.overwrite_flag &
					MEMSTICK_OVERWRITE_PGST0)
			msb_mark_page_bad(msb, pba, page);
		return -EBADMSG;
	}

	if (error)
		pr_err("read of pba %d, page %d failed with error %d",
			pba, page, error);
	return error;
}

/* Reads oob of page only */
static int msb_read_oob(struct msb_data *msb, u16 pba, u16 page,
	struct ms_extra_data_register *extra)
{
	int error;

	BUG_ON(!extra);
	msb->regs.param.block_address = cpu_to_be16(pba);
	msb->regs.param.page_address = page;
	msb->regs.param.cp = MEMSTICK_CP_EXTRA;

	if (pba > msb->block_count) {
		pr_err("BUG: attempt to read beyond the end of card at pba %d", pba);
		return -EINVAL;
	}

	error = msb_run_state_machine(msb, h_msb_read_page);
	*extra = msb->regs.extra_data;

	if (error == -EUCLEAN) {
		pr_notice("correctable error on pba %d, page %d",
			pba, page);
		return 0;
	}

	return error;
}

/* Reads a block and compares it with data contained in scatterlist orig_sg */
static int msb_verify_block(struct msb_data *msb, u16 pba,
				struct scatterlist *orig_sg,  int offset)
{
	struct scatterlist sg;
	int page = 0, error;

	sg_init_one(&sg, msb->block_buffer, msb->block_size);

	while (page < msb->pages_in_block) {

		error = msb_read_page(msb, pba, page,
				NULL, &sg, page * msb->page_size);
		if (error)
			return error;
		page++;
	}

	if (msb_sg_compare_to_buffer(orig_sg, offset,
				msb->block_buffer, msb->block_size))
		return -EIO;
	return 0;
}

/* Writes exectly one block + oob */
static int msb_write_block(struct msb_data *msb,
			u16 pba, u32 lba, struct scatterlist *sg, int offset)
{
	int error, current_try = 1;
	BUG_ON(sg->length < msb->page_size);

	if (msb->read_only)
		return -EROFS;

	if (pba == MS_BLOCK_INVALID) {
		pr_err(
			"BUG: write: attempt to write MS_BLOCK_INVALID block");
		return -EINVAL;
	}

	if (pba >= msb->block_count || lba >= msb->logical_block_count) {
		pr_err(
		"BUG: write: attempt to write beyond the end of device");
		return -EINVAL;
	}

	if (msb_get_zone_from_lba(lba) != msb_get_zone_from_pba(pba)) {
		pr_err("BUG: write: lba zone mismatch");
		return -EINVAL;
	}

	if (pba == msb->boot_block_locations[0] ||
		pba == msb->boot_block_locations[1]) {
		pr_err("BUG: write: attempt to write to boot blocks!");
		return -EINVAL;
	}

	while (1) {

		if (msb->read_only)
			return -EROFS;

		msb->regs.param.cp = MEMSTICK_CP_BLOCK;
		msb->regs.param.page_address = 0;
		msb->regs.param.block_address = cpu_to_be16(pba);

		msb->regs.extra_data.management_flag = 0xFF;
		msb->regs.extra_data.overwrite_flag = 0xF8;
		msb->regs.extra_data.logical_address = cpu_to_be16(lba);

		msb->current_sg = sg;
		msb->current_sg_offset = offset;
		msb->current_page = 0;

		error = msb_run_state_machine(msb, h_msb_write_block);

		/* Sector we just wrote to is assumed erased since its pba
			was erased. If it wasn't erased, write will succeed
			and will just clear the bits that were set in the block
			thus test that what we have written,
			matches what we expect.
			We do trust the blocks that we erased */
		if (!error && (verify_writes ||
				!test_bit(pba, msb->erased_blocks_bitmap)))
			error = msb_verify_block(msb, pba, sg, offset);

		if (!error)
			break;

		if (current_try > 1 || msb_reset(msb, true))
			break;

		pr_err("write failed, trying to erase the pba %d", pba);
		error = msb_erase_block(msb, pba);
		if (error)
			break;

		current_try++;
	}
	return error;
}

/* Finds a free block for write replacement */
static u16 msb_get_free_block(struct msb_data *msb, int zone)
{
	u16 pos;
	int pba = zone * MS_BLOCKS_IN_ZONE;
	int i;

	get_random_bytes(&pos, sizeof(pos));

	if (!msb->free_block_count[zone]) {
		pr_err("NO free blocks in the zone %d, to use for a write, (media is WORN out) switching to RO mode", zone);
		msb->read_only = true;
		return MS_BLOCK_INVALID;
	}

	pos %= msb->free_block_count[zone];

	dbg_verbose("have %d choices for a free block, selected randomally: %d",
		msb->free_block_count[zone], pos);

	pba = find_next_zero_bit(msb->used_blocks_bitmap,
							msb->block_count, pba);
	for (i = 0; i < pos; ++i)
		pba = find_next_zero_bit(msb->used_blocks_bitmap,
						msb->block_count, pba + 1);

	dbg_verbose("result of the free blocks scan: pba %d", pba);

	if (pba == msb->block_count || (msb_get_zone_from_pba(pba)) != zone) {
		pr_err("BUG: cant get a free block");
		msb->read_only = true;
		return MS_BLOCK_INVALID;
	}

	msb_mark_block_used(msb, pba);
	return pba;
}

static int msb_update_block(struct msb_data *msb, u16 lba,
	struct scatterlist *sg, int offset)
{
	u16 pba, new_pba;
	int error, try;

	pba = msb->lba_to_pba_table[lba];
	dbg_verbose("start of a block update at lba  %d, pba %d", lba, pba);

	if (pba != MS_BLOCK_INVALID) {
		dbg_verbose("setting the update flag on the block");
		msb_set_overwrite_flag(msb, pba, 0,
				0xFF & ~MEMSTICK_OVERWRITE_UDST);
	}

	for (try = 0; try < 3; try++) {
		new_pba = msb_get_free_block(msb,
			msb_get_zone_from_lba(lba));

		if (new_pba == MS_BLOCK_INVALID) {
			error = -EIO;
			goto out;
		}

		dbg_verbose("block update: writing updated block to the pba %d",
								new_pba);
		error = msb_write_block(msb, new_pba, lba, sg, offset);
		if (error == -EBADMSG) {
			msb_mark_bad(msb, new_pba);
			continue;
		}

		if (error)
			goto out;

		dbg_verbose("block update: erasing the old block");
		msb_erase_block(msb, pba);
		msb->lba_to_pba_table[lba] = new_pba;
		return 0;
	}
out:
	if (error) {
		pr_err("block update error after %d tries,  switching to r/o mode", try);
		msb->read_only = true;
	}
	return error;
}

/* Converts endiannes in the boot block for easy use */
static void msb_fix_boot_page_endianness(struct ms_boot_page *p)
{
	p->header.block_id = be16_to_cpu(p->header.block_id);
	p->header.format_reserved = be16_to_cpu(p->header.format_reserved);
	p->entry.disabled_block.start_addr
		= be32_to_cpu(p->entry.disabled_block.start_addr);
	p->entry.disabled_block.data_size
		= be32_to_cpu(p->entry.disabled_block.data_size);
	p->entry.cis_idi.start_addr
		= be32_to_cpu(p->entry.cis_idi.start_addr);
	p->entry.cis_idi.data_size
		= be32_to_cpu(p->entry.cis_idi.data_size);
	p->attr.block_size = be16_to_cpu(p->attr.block_size);
	p->attr.number_of_blocks = be16_to_cpu(p->attr.number_of_blocks);
	p->attr.number_of_effective_blocks
		= be16_to_cpu(p->attr.number_of_effective_blocks);
	p->attr.page_size = be16_to_cpu(p->attr.page_size);
	p->attr.memory_manufacturer_code
		= be16_to_cpu(p->attr.memory_manufacturer_code);
	p->attr.memory_device_code = be16_to_cpu(p->attr.memory_device_code);
	p->attr.implemented_capacity
		= be16_to_cpu(p->attr.implemented_capacity);
	p->attr.controller_number = be16_to_cpu(p->attr.controller_number);
	p->attr.controller_function = be16_to_cpu(p->attr.controller_function);
}

static int msb_read_boot_blocks(struct msb_data *msb)
{
	int pba = 0;
	struct scatterlist sg;
	struct ms_extra_data_register extra;
	struct ms_boot_page *page;

	msb->boot_block_locations[0] = MS_BLOCK_INVALID;
	msb->boot_block_locations[1] = MS_BLOCK_INVALID;
	msb->boot_block_count = 0;

	dbg_verbose("Start of a scan for the boot blocks");

	if (!msb->boot_page) {
		page = kmalloc(sizeof(struct ms_boot_page)*2, GFP_KERNEL);
		if (!page)
			return -ENOMEM;

		msb->boot_page = page;
	} else
		page = msb->boot_page;

	msb->block_count = MS_BLOCK_MAX_BOOT_ADDR;

	for (pba = 0; pba < MS_BLOCK_MAX_BOOT_ADDR; pba++) {

		sg_init_one(&sg, page, sizeof(*page));
		if (msb_read_page(msb, pba, 0, &extra, &sg, 0)) {
			dbg("boot scan: can't read pba %d", pba);
			continue;
		}

		if (extra.management_flag & MEMSTICK_MANAGEMENT_SYSFLG) {
			dbg("management flag doesn't indicate boot block %d",
									pba);
			continue;
		}

		if (be16_to_cpu(page->header.block_id) != MS_BLOCK_BOOT_ID) {
			dbg("the pba at %d doesn' contain boot block ID", pba);
			continue;
		}

		msb_fix_boot_page_endianness(page);
		msb->boot_block_locations[msb->boot_block_count] = pba;

		page++;
		msb->boot_block_count++;

		if (msb->boot_block_count == 2)
			break;
	}

	if (!msb->boot_block_count) {
		pr_err("media doesn't contain master page, aborting");
		return -EIO;
	}

	dbg_verbose("End of scan for boot blocks");
	return 0;
}

static int msb_read_bad_block_table(struct msb_data *msb, int block_nr)
{
	struct ms_boot_page *boot_block;
	struct scatterlist sg;
	u16 *buffer = NULL;
	int offset = 0;
	int i, error = 0;
	int data_size, data_offset, page, page_offset, size_to_read;
	u16 pba;

	BUG_ON(block_nr > 1);
	boot_block = &msb->boot_page[block_nr];
	pba = msb->boot_block_locations[block_nr];

	if (msb->boot_block_locations[block_nr] == MS_BLOCK_INVALID)
		return -EINVAL;

	data_size = boot_block->entry.disabled_block.data_size;
	data_offset = sizeof(struct ms_boot_page) +
			boot_block->entry.disabled_block.start_addr;
	if (!data_size)
		return 0;

	page = data_offset / msb->page_size;
	page_offset = data_offset % msb->page_size;
	size_to_read =
		DIV_ROUND_UP(data_size + page_offset, msb->page_size) *
			msb->page_size;

	dbg("reading bad block of boot block at pba %d, offset %d len %d",
		pba, data_offset, data_size);

	buffer = kzalloc(size_to_read, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	/* Read the buffer */
	sg_init_one(&sg, buffer, size_to_read);

	while (offset < size_to_read) {
		error = msb_read_page(msb, pba, page, NULL, &sg, offset);
		if (error)
			goto out;

		page++;
		offset += msb->page_size;

		if (page == msb->pages_in_block) {
			pr_err(
			"bad block table extends beyond the boot block");
			break;
		}
	}

	/* Process the bad block table */
	for (i = page_offset; i < data_size / sizeof(u16); i++) {

		u16 bad_block = be16_to_cpu(buffer[i]);

		if (bad_block >= msb->block_count) {
			dbg("bad block table contains invalid block %d",
								bad_block);
			continue;
		}

		if (test_bit(bad_block, msb->used_blocks_bitmap))  {
			dbg("duplicate bad block %d in the table",
				bad_block);
			continue;
		}

		dbg("block %d is marked as factory bad", bad_block);
		msb_mark_block_used(msb, bad_block);
	}
out:
	kfree(buffer);
	return error;
}

static int msb_ftl_initialize(struct msb_data *msb)
{
	int i;

	if (msb->ftl_initialized)
		return 0;

	msb->zone_count = msb->block_count / MS_BLOCKS_IN_ZONE;
	msb->logical_block_count = msb->zone_count * 496 - 2;

	msb->used_blocks_bitmap = kzalloc(msb->block_count / 8, GFP_KERNEL);
	msb->erased_blocks_bitmap = kzalloc(msb->block_count / 8, GFP_KERNEL);
	msb->lba_to_pba_table =
		kmalloc(msb->logical_block_count * sizeof(u16), GFP_KERNEL);

	if (!msb->used_blocks_bitmap || !msb->lba_to_pba_table ||
						!msb->erased_blocks_bitmap) {
		kfree(msb->used_blocks_bitmap);
		kfree(msb->lba_to_pba_table);
		kfree(msb->erased_blocks_bitmap);
		return -ENOMEM;
	}

	for (i = 0; i < msb->zone_count; i++)
		msb->free_block_count[i] = MS_BLOCKS_IN_ZONE;

	memset(msb->lba_to_pba_table, MS_BLOCK_INVALID,
			msb->logical_block_count * sizeof(u16));

	dbg("initial FTL tables created. Zone count = %d, Logical block count = %d",
		msb->zone_count, msb->logical_block_count);

	msb->ftl_initialized = true;
	return 0;
}

static int msb_ftl_scan(struct msb_data *msb)
{
	u16 pba, lba, other_block;
	u8 overwrite_flag, management_flag, other_overwrite_flag;
	int error;
	struct ms_extra_data_register extra;
	u8 *overwrite_flags = kzalloc(msb->block_count, GFP_KERNEL);

	if (!overwrite_flags)
		return -ENOMEM;

	dbg("Start of media scanning");
	for (pba = 0; pba < msb->block_count; pba++) {

		if (pba == msb->boot_block_locations[0] ||
			pba == msb->boot_block_locations[1]) {
			dbg_verbose("pba %05d -> [boot block]", pba);
			msb_mark_block_used(msb, pba);
			continue;
		}

		if (test_bit(pba, msb->used_blocks_bitmap)) {
			dbg_verbose("pba %05d -> [factory bad]", pba);
			continue;
		}

		memset(&extra, 0, sizeof(extra));
		error = msb_read_oob(msb, pba, 0, &extra);

		/* can't trust the page if we can't read the oob */
		if (error == -EBADMSG) {
			pr_notice(
			"oob of pba %d damaged, will try to erase it", pba);
			msb_mark_block_used(msb, pba);
			msb_erase_block(msb, pba);
			continue;
		} else if (error) {
			pr_err("unknown error %d on read of oob of pba %d - aborting",
				error, pba);

			kfree(overwrite_flags);
			return error;
		}

		lba = be16_to_cpu(extra.logical_address);
		management_flag = extra.management_flag;
		overwrite_flag = extra.overwrite_flag;
		overwrite_flags[pba] = overwrite_flag;

		/* Skip bad blocks */
		if (!(overwrite_flag & MEMSTICK_OVERWRITE_BKST)) {
			dbg("pba %05d -> [BAD]", pba);
			msb_mark_block_used(msb, pba);
			continue;
		}

		/* Skip system/drm blocks */
		if ((management_flag & MEMSTICK_MANAGEMENT_FLAG_NORMAL) !=
			MEMSTICK_MANAGEMENT_FLAG_NORMAL) {
			dbg("pba %05d -> [reserved management flag %02x]",
							pba, management_flag);
			msb_mark_block_used(msb, pba);
			continue;
		}

		/* Erase temporary tables */
		if (!(management_flag & MEMSTICK_MANAGEMENT_ATFLG)) {
			dbg("pba %05d -> [temp table] - will erase", pba);

			msb_mark_block_used(msb, pba);
			msb_erase_block(msb, pba);
			continue;
		}

		if (lba == MS_BLOCK_INVALID) {
			dbg_verbose("pba %05d -> [free]", pba);
			continue;
		}

		msb_mark_block_used(msb, pba);

		/* Block has LBA not according to zoning*/
		if (msb_get_zone_from_lba(lba) != msb_get_zone_from_pba(pba)) {
			pr_notice("pba %05d -> [bad lba %05d] - will erase",
								pba, lba);
			msb_erase_block(msb, pba);
			continue;
		}

		/* No collisions - great */
		if (msb->lba_to_pba_table[lba] == MS_BLOCK_INVALID) {
			dbg_verbose("pba %05d -> [lba %05d]", pba, lba);
			msb->lba_to_pba_table[lba] = pba;
			continue;
		}

		other_block = msb->lba_to_pba_table[lba];
		other_overwrite_flag = overwrite_flags[other_block];

		pr_notice("Collision between pba %d and pba %d",
			pba, other_block);

		if (!(overwrite_flag & MEMSTICK_OVERWRITE_UDST)) {
			pr_notice("pba %d is marked as stable, use it", pba);
			msb_erase_block(msb, other_block);
			msb->lba_to_pba_table[lba] = pba;
			continue;
		}

		if (!(other_overwrite_flag & MEMSTICK_OVERWRITE_UDST)) {
			pr_notice("pba %d is marked as stable, use it",
								other_block);
			msb_erase_block(msb, pba);
			continue;
		}

		pr_notice("collision between blocks %d and %d, without stable flag set on both, erasing pba %d",
				pba, other_block, other_block);

		msb_erase_block(msb, other_block);
		msb->lba_to_pba_table[lba] = pba;
	}

	dbg("End of media scanning");
	kfree(overwrite_flags);
	return 0;
}

static void msb_cache_flush_timer(unsigned long data)
{
	struct msb_data *msb = (struct msb_data *)data;
	msb->need_flush_cache = true;
	queue_work(msb->io_queue, &msb->io_work);
}


static void msb_cache_discard(struct msb_data *msb)
{
	if (msb->cache_block_lba == MS_BLOCK_INVALID)
		return;

	del_timer_sync(&msb->cache_flush_timer);

	dbg_verbose("Discarding the write cache");
	msb->cache_block_lba = MS_BLOCK_INVALID;
	bitmap_zero(&msb->valid_cache_bitmap, msb->pages_in_block);
}

static int msb_cache_init(struct msb_data *msb)
{
	setup_timer(&msb->cache_flush_timer, msb_cache_flush_timer,
		(unsigned long)msb);

	if (!msb->cache)
		msb->cache = kzalloc(msb->block_size, GFP_KERNEL);
	if (!msb->cache)
		return -ENOMEM;

	msb_cache_discard(msb);
	return 0;
}

static int msb_cache_flush(struct msb_data *msb)
{
	struct scatterlist sg;
	struct ms_extra_data_register extra;
	int page, offset, error;
	u16 pba, lba;

	if (msb->read_only)
		return -EROFS;

	if (msb->cache_block_lba == MS_BLOCK_INVALID)
		return 0;

	lba = msb->cache_block_lba;
	pba = msb->lba_to_pba_table[lba];

	dbg_verbose("Flushing the write cache of pba %d (LBA %d)",
						pba, msb->cache_block_lba);

	sg_init_one(&sg, msb->cache , msb->block_size);

	/* Read all missing pages in cache */
	for (page = 0; page < msb->pages_in_block; page++) {

		if (test_bit(page, &msb->valid_cache_bitmap))
			continue;

		offset = page * msb->page_size;

		dbg_verbose("reading non-present sector %d of cache block %d",
			page, lba);
		error = msb_read_page(msb, pba, page, &extra, &sg, offset);

		/* Bad pages are copied with 00 page status */
		if (error == -EBADMSG) {
			pr_err("read error on sector %d, contents probably damaged", page);
			continue;
		}

		if (error)
			return error;

		if ((extra.overwrite_flag & MEMSTICK_OV_PG_NORMAL) !=
							MEMSTICK_OV_PG_NORMAL) {
			dbg("page %d is marked as bad", page);
			continue;
		}

		set_bit(page, &msb->valid_cache_bitmap);
	}

	/* Write the cache now */
	error = msb_update_block(msb, msb->cache_block_lba, &sg, 0);
	pba = msb->lba_to_pba_table[msb->cache_block_lba];

	/* Mark invalid pages */
	if (!error) {
		for (page = 0; page < msb->pages_in_block; page++) {

			if (test_bit(page, &msb->valid_cache_bitmap))
				continue;

			dbg("marking page %d as containing damaged data",
				page);
			msb_set_overwrite_flag(msb,
				pba , page, 0xFF & ~MEMSTICK_OV_PG_NORMAL);
		}
	}

	msb_cache_discard(msb);
	return error;
}

static int msb_cache_write(struct msb_data *msb, int lba,
	int page, bool add_to_cache_only, struct scatterlist *sg, int offset)
{
	int error;
	struct scatterlist sg_tmp[10];

	if (msb->read_only)
		return -EROFS;

	if (msb->cache_block_lba == MS_BLOCK_INVALID ||
						lba != msb->cache_block_lba)
		if (add_to_cache_only)
			return 0;

	/* If we need to write different block */
	if (msb->cache_block_lba != MS_BLOCK_INVALID &&
						lba != msb->cache_block_lba) {
		dbg_verbose("first flush the cache");
		error = msb_cache_flush(msb);
		if (error)
			return error;
	}

	if (msb->cache_block_lba  == MS_BLOCK_INVALID) {
		msb->cache_block_lba  = lba;
		mod_timer(&msb->cache_flush_timer,
			jiffies + msecs_to_jiffies(cache_flush_timeout));
	}

	dbg_verbose("Write of LBA %d page %d to cache ", lba, page);

	sg_init_table(sg_tmp, ARRAY_SIZE(sg_tmp));
	msb_sg_copy(sg, sg_tmp, ARRAY_SIZE(sg_tmp), offset, msb->page_size);

	sg_copy_to_buffer(sg_tmp, sg_nents(sg_tmp),
		msb->cache + page * msb->page_size, msb->page_size);

	set_bit(page, &msb->valid_cache_bitmap);
	return 0;
}

static int msb_cache_read(struct msb_data *msb, int lba,
				int page, struct scatterlist *sg, int offset)
{
	int pba = msb->lba_to_pba_table[lba];
	struct scatterlist sg_tmp[10];
	int error = 0;

	if (lba == msb->cache_block_lba &&
			test_bit(page, &msb->valid_cache_bitmap)) {

		dbg_verbose("Read of LBA %d (pba %d) sector %d from cache",
							lba, pba, page);

		sg_init_table(sg_tmp, ARRAY_SIZE(sg_tmp));
		msb_sg_copy(sg, sg_tmp, ARRAY_SIZE(sg_tmp),
			offset, msb->page_size);
		sg_copy_from_buffer(sg_tmp, sg_nents(sg_tmp),
			msb->cache + msb->page_size * page,
							msb->page_size);
	} else {
		dbg_verbose("Read of LBA %d (pba %d) sector %d from device",
							lba, pba, page);

		error = msb_read_page(msb, pba, page, NULL, sg, offset);
		if (error)
			return error;

		msb_cache_write(msb, lba, page, true, sg, offset);
	}
	return error;
}

/* Emulated geometry table
 * This table content isn't that importaint,
 * One could put here different values, providing that they still
 * cover whole disk.
 * 64 MB entry is what windows reports for my 64M memstick */

static const struct chs_entry chs_table[] = {
/*        size sectors cylynders  heads */
	{ 4,    16,    247,       2  },
	{ 8,    16,    495,       2  },
	{ 16,   16,    495,       4  },
	{ 32,   16,    991,       4  },
	{ 64,   16,    991,       8  },
	{128,   16,    991,       16 },
	{ 0 }
};

/* Load information about the card */
static int msb_init_card(struct memstick_dev *card)
{
	struct msb_data *msb = memstick_get_drvdata(card);
	struct memstick_host *host = card->host;
	struct ms_boot_page *boot_block;
	int error = 0, i, raw_size_in_megs;

	msb->caps = 0;

	if (card->id.class >= MEMSTICK_CLASS_ROM &&
				card->id.class <= MEMSTICK_CLASS_ROM)
		msb->read_only = true;

	msb->state = -1;
	error = msb_reset(msb, false);
	if (error)
		return error;

	/* Due to a bug in Jmicron driver written by Alex Dubov,
	 its serial mode barely works,
	 so we switch to parallel mode right away */
	if (host->caps & MEMSTICK_CAP_PAR4)
		msb_switch_to_parallel(msb);

	msb->page_size = sizeof(struct ms_boot_page);

	/* Read the boot page */
	error = msb_read_boot_blocks(msb);
	if (error)
		return -EIO;

	boot_block = &msb->boot_page[0];

	/* Save intersting attributes from boot page */
	msb->block_count = boot_block->attr.number_of_blocks;
	msb->page_size = boot_block->attr.page_size;

	msb->pages_in_block = boot_block->attr.block_size * 2;
	msb->block_size = msb->page_size * msb->pages_in_block;

	if (msb->page_size > PAGE_SIZE) {
		/* this isn't supported by linux at all, anyway*/
		dbg("device page %d size isn't supported", msb->page_size);
		return -EINVAL;
	}

	msb->block_buffer = kzalloc(msb->block_size, GFP_KERNEL);
	if (!msb->block_buffer)
		return -ENOMEM;

	raw_size_in_megs = (msb->block_size * msb->block_count) >> 20;

	for (i = 0; chs_table[i].size; i++) {

		if (chs_table[i].size != raw_size_in_megs)
			continue;

		msb->geometry.cylinders = chs_table[i].cyl;
		msb->geometry.heads = chs_table[i].head;
		msb->geometry.sectors = chs_table[i].sec;
		break;
	}

	if (boot_block->attr.transfer_supporting == 1)
		msb->caps |= MEMSTICK_CAP_PAR4;

	if (boot_block->attr.device_type & 0x03)
		msb->read_only = true;

	dbg("Total block count = %d", msb->block_count);
	dbg("Each block consists of %d pages", msb->pages_in_block);
	dbg("Page size = %d bytes", msb->page_size);
	dbg("Parallel mode supported: %d", !!(msb->caps & MEMSTICK_CAP_PAR4));
	dbg("Read only: %d", msb->read_only);

#if 0
	/* Now we can switch the interface */
	if (host->caps & msb->caps & MEMSTICK_CAP_PAR4)
		msb_switch_to_parallel(msb);
#endif

	error = msb_cache_init(msb);
	if (error)
		return error;

	error = msb_ftl_initialize(msb);
	if (error)
		return error;


	/* Read the bad block table */
	error = msb_read_bad_block_table(msb, 0);

	if (error && error != -ENOMEM) {
		dbg("failed to read bad block table from primary boot block, trying from backup");
		error = msb_read_bad_block_table(msb, 1);
	}

	if (error)
		return error;

	/* *drum roll* Scan the media */
	error = msb_ftl_scan(msb);
	if (error) {
		pr_err("Scan of media failed");
		return error;
	}

	return 0;

}

static int msb_do_write_request(struct msb_data *msb, int lba,
	int page, struct scatterlist *sg, size_t len, int *sucessfuly_written)
{
	int error = 0;
	off_t offset = 0;
	*sucessfuly_written = 0;

	while (offset < len) {
		if (page == 0 && len - offset >= msb->block_size) {

			if (msb->cache_block_lba == lba)
				msb_cache_discard(msb);

			dbg_verbose("Writing whole lba %d", lba);
			error = msb_update_block(msb, lba, sg, offset);
			if (error)
				return error;

			offset += msb->block_size;
			*sucessfuly_written += msb->block_size;
			lba++;
			continue;
		}

		error = msb_cache_write(msb, lba, page, false, sg, offset);
		if (error)
			return error;

		offset += msb->page_size;
		*sucessfuly_written += msb->page_size;

		page++;
		if (page == msb->pages_in_block) {
			page = 0;
			lba++;
		}
	}
	return 0;
}

static int msb_do_read_request(struct msb_data *msb, int lba,
		int page, struct scatterlist *sg, int len, int *sucessfuly_read)
{
	int error = 0;
	int offset = 0;
	*sucessfuly_read = 0;

	while (offset < len) {

		error = msb_cache_read(msb, lba, page, sg, offset);
		if (error)
			return error;

		offset += msb->page_size;
		*sucessfuly_read += msb->page_size;

		page++;
		if (page == msb->pages_in_block) {
			page = 0;
			lba++;
		}
	}
	return 0;
}

static void msb_io_work(struct work_struct *work)
{
	struct msb_data *msb = container_of(work, struct msb_data, io_work);
	int page, error, len;
	sector_t lba;
	unsigned long flags;
	struct scatterlist *sg = msb->prealloc_sg;

	dbg_verbose("IO: work started");

	while (1) {
		spin_lock_irqsave(&msb->q_lock, flags);

		if (msb->need_flush_cache) {
			msb->need_flush_cache = false;
			spin_unlock_irqrestore(&msb->q_lock, flags);
			msb_cache_flush(msb);
			continue;
		}

		if (!msb->req) {
			msb->req = blk_fetch_request(msb->queue);
			if (!msb->req) {
				dbg_verbose("IO: no more requests exiting");
				spin_unlock_irqrestore(&msb->q_lock, flags);
				return;
			}
		}

		spin_unlock_irqrestore(&msb->q_lock, flags);

		/* If card was removed meanwhile */
		if (!msb->req)
			return;

		/* process the request */
		dbg_verbose("IO: processing new request");
		blk_rq_map_sg(msb->queue, msb->req, sg);

		lba = blk_rq_pos(msb->req);

		sector_div(lba, msb->page_size / 512);
		page = sector_div(lba, msb->pages_in_block);

		if (rq_data_dir(msb->req) == READ)
			error = msb_do_read_request(msb, lba, page, sg,
				blk_rq_bytes(msb->req), &len);
		else
			error = msb_do_write_request(msb, lba, page, sg,
				blk_rq_bytes(msb->req), &len);

		spin_lock_irqsave(&msb->q_lock, flags);

		if (len)
			if (!__blk_end_request(msb->req, 0, len))
				msb->req = NULL;

		if (error && msb->req) {
			dbg_verbose("IO: ending one sector of the request with error");
			if (!__blk_end_request(msb->req, error, msb->page_size))
				msb->req = NULL;
		}

		if (msb->req)
			dbg_verbose("IO: request still pending");

		spin_unlock_irqrestore(&msb->q_lock, flags);
	}
}

static DEFINE_IDR(msb_disk_idr); /*set of used disk numbers */
static DEFINE_MUTEX(msb_disk_lock); /* protects against races in open/release */

static int msb_bd_open(struct block_device *bdev, fmode_t mode)
{
	struct gendisk *disk = bdev->bd_disk;
	struct msb_data *msb = disk->private_data;

	dbg_verbose("block device open");

	mutex_lock(&msb_disk_lock);

	if (msb && msb->card)
		msb->usage_count++;

	mutex_unlock(&msb_disk_lock);
	return 0;
}

static void msb_data_clear(struct msb_data *msb)
{
	kfree(msb->boot_page);
	kfree(msb->used_blocks_bitmap);
	kfree(msb->lba_to_pba_table);
	kfree(msb->cache);
	msb->card = NULL;
}

static int msb_disk_release(struct gendisk *disk)
{
	struct msb_data *msb = disk->private_data;

	dbg_verbose("block device release");
	mutex_lock(&msb_disk_lock);

	if (msb) {
		if (msb->usage_count)
			msb->usage_count--;

		if (!msb->usage_count) {
			disk->private_data = NULL;
			idr_remove(&msb_disk_idr, msb->disk_id);
			put_disk(disk);
			kfree(msb);
		}
	}
	mutex_unlock(&msb_disk_lock);
	return 0;
}

static void msb_bd_release(struct gendisk *disk, fmode_t mode)
{
	msb_disk_release(disk);
}

static int msb_bd_getgeo(struct block_device *bdev,
				 struct hd_geometry *geo)
{
	struct msb_data *msb = bdev->bd_disk->private_data;
	*geo = msb->geometry;
	return 0;
}

static int msb_prepare_req(struct request_queue *q, struct request *req)
{
	if (req->cmd_type != REQ_TYPE_FS &&
				req->cmd_type != REQ_TYPE_BLOCK_PC) {
		blk_dump_rq_flags(req, "MS unsupported request");
		return BLKPREP_KILL;
	}
	req->cmd_flags |= REQ_DONTPREP;
	return BLKPREP_OK;
}

static void msb_submit_req(struct request_queue *q)
{
	struct memstick_dev *card = q->queuedata;
	struct msb_data *msb = memstick_get_drvdata(card);
	struct request *req = NULL;

	dbg_verbose("Submit request");

	if (msb->card_dead) {
		dbg("Refusing requests on removed card");

		WARN_ON(!msb->io_queue_stopped);

		while ((req = blk_fetch_request(q)) != NULL)
			__blk_end_request_all(req, -ENODEV);
		return;
	}

	if (msb->req)
		return;

	if (!msb->io_queue_stopped)
		queue_work(msb->io_queue, &msb->io_work);
}

static int msb_check_card(struct memstick_dev *card)
{
	struct msb_data *msb = memstick_get_drvdata(card);
	return (msb->card_dead == 0);
}

static void msb_stop(struct memstick_dev *card)
{
	struct msb_data *msb = memstick_get_drvdata(card);
	unsigned long flags;

	dbg("Stopping all msblock IO");

	spin_lock_irqsave(&msb->q_lock, flags);
	blk_stop_queue(msb->queue);
	msb->io_queue_stopped = true;
	spin_unlock_irqrestore(&msb->q_lock, flags);

	del_timer_sync(&msb->cache_flush_timer);
	flush_workqueue(msb->io_queue);

	if (msb->req) {
		spin_lock_irqsave(&msb->q_lock, flags);
		blk_requeue_request(msb->queue, msb->req);
		msb->req = NULL;
		spin_unlock_irqrestore(&msb->q_lock, flags);
	}

}

static void msb_start(struct memstick_dev *card)
{
	struct msb_data *msb = memstick_get_drvdata(card);
	unsigned long flags;

	dbg("Resuming IO from msblock");

	msb_invalidate_reg_window(msb);

	spin_lock_irqsave(&msb->q_lock, flags);
	if (!msb->io_queue_stopped || msb->card_dead) {
		spin_unlock_irqrestore(&msb->q_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&msb->q_lock, flags);

	/* Kick cache flush anyway, its harmless */
	msb->need_flush_cache = true;
	msb->io_queue_stopped = false;

	spin_lock_irqsave(&msb->q_lock, flags);
	blk_start_queue(msb->queue);
	spin_unlock_irqrestore(&msb->q_lock, flags);

	queue_work(msb->io_queue, &msb->io_work);

}

static const struct block_device_operations msb_bdops = {
	.open    = msb_bd_open,
	.release = msb_bd_release,
	.getgeo  = msb_bd_getgeo,
	.owner   = THIS_MODULE
};

/* Registers the block device */
static int msb_init_disk(struct memstick_dev *card)
{
	struct msb_data *msb = memstick_get_drvdata(card);
	struct memstick_host *host = card->host;
	int rc;
	u64 limit = BLK_BOUNCE_HIGH;
	unsigned long capacity;

	if (host->dev.dma_mask && *(host->dev.dma_mask))
		limit = *(host->dev.dma_mask);

	mutex_lock(&msb_disk_lock);
	msb->disk_id = idr_alloc(&msb_disk_idr, card, 0, 256, GFP_KERNEL);
	mutex_unlock(&msb_disk_lock);

	if (msb->disk_id  < 0)
		return msb->disk_id;

	msb->disk = alloc_disk(0);
	if (!msb->disk) {
		rc = -ENOMEM;
		goto out_release_id;
	}

	msb->queue = blk_init_queue(msb_submit_req, &msb->q_lock);
	if (!msb->queue) {
		rc = -ENOMEM;
		goto out_put_disk;
	}

	msb->queue->queuedata = card;
	blk_queue_prep_rq(msb->queue, msb_prepare_req);

	blk_queue_bounce_limit(msb->queue, limit);
	blk_queue_max_hw_sectors(msb->queue, MS_BLOCK_MAX_PAGES);
	blk_queue_max_segments(msb->queue, MS_BLOCK_MAX_SEGS);
	blk_queue_max_segment_size(msb->queue,
				   MS_BLOCK_MAX_PAGES * msb->page_size);
	blk_queue_logical_block_size(msb->queue, msb->page_size);

	sprintf(msb->disk->disk_name, "msblk%d", msb->disk_id);
	msb->disk->fops = &msb_bdops;
	msb->disk->private_data = msb;
	msb->disk->queue = msb->queue;
	msb->disk->driverfs_dev = &card->dev;
	msb->disk->flags |= GENHD_FL_EXT_DEVT;

	capacity = msb->pages_in_block * msb->logical_block_count;
	capacity *= (msb->page_size / 512);
	set_capacity(msb->disk, capacity);
	dbg("Set total disk size to %lu sectors", capacity);

	msb->usage_count = 1;
	msb->io_queue = alloc_ordered_workqueue("ms_block", WQ_MEM_RECLAIM);
	INIT_WORK(&msb->io_work, msb_io_work);
	sg_init_table(msb->prealloc_sg, MS_BLOCK_MAX_SEGS+1);

	if (msb->read_only)
		set_disk_ro(msb->disk, 1);

	msb_start(card);
	add_disk(msb->disk);
	dbg("Disk added");
	return 0;

out_put_disk:
	put_disk(msb->disk);
out_release_id:
	mutex_lock(&msb_disk_lock);
	idr_remove(&msb_disk_idr, msb->disk_id);
	mutex_unlock(&msb_disk_lock);
	return rc;
}

static int msb_probe(struct memstick_dev *card)
{
	struct msb_data *msb;
	int rc = 0;

	msb = kzalloc(sizeof(struct msb_data), GFP_KERNEL);
	if (!msb)
		return -ENOMEM;
	memstick_set_drvdata(card, msb);
	msb->card = card;
	spin_lock_init(&msb->q_lock);

	rc = msb_init_card(card);
	if (rc)
		goto out_free;

	rc = msb_init_disk(card);
	if (!rc) {
		card->check = msb_check_card;
		card->stop = msb_stop;
		card->start = msb_start;
		return 0;
	}
out_free:
	memstick_set_drvdata(card, NULL);
	msb_data_clear(msb);
	kfree(msb);
	return rc;
}

static void msb_remove(struct memstick_dev *card)
{
	struct msb_data *msb = memstick_get_drvdata(card);
	unsigned long flags;

	if (!msb->io_queue_stopped)
		msb_stop(card);

	dbg("Removing the disk device");

	/* Take care of unhandled + new requests from now on */
	spin_lock_irqsave(&msb->q_lock, flags);
	msb->card_dead = true;
	blk_start_queue(msb->queue);
	spin_unlock_irqrestore(&msb->q_lock, flags);

	/* Remove the disk */
	del_gendisk(msb->disk);
	blk_cleanup_queue(msb->queue);
	msb->queue = NULL;

	mutex_lock(&msb_disk_lock);
	msb_data_clear(msb);
	mutex_unlock(&msb_disk_lock);

	msb_disk_release(msb->disk);
	memstick_set_drvdata(card, NULL);
}

#ifdef CONFIG_PM

static int msb_suspend(struct memstick_dev *card, pm_message_t state)
{
	msb_stop(card);
	return 0;
}

static int msb_resume(struct memstick_dev *card)
{
	struct msb_data *msb = memstick_get_drvdata(card);
	struct msb_data *new_msb = NULL;
	bool card_dead = true;

#ifndef CONFIG_MEMSTICK_UNSAFE_RESUME
	msb->card_dead = true;
	return 0;
#endif
	mutex_lock(&card->host->lock);

	new_msb = kzalloc(sizeof(struct msb_data), GFP_KERNEL);
	if (!new_msb)
		goto out;

	new_msb->card = card;
	memstick_set_drvdata(card, new_msb);
	spin_lock_init(&new_msb->q_lock);
	sg_init_table(msb->prealloc_sg, MS_BLOCK_MAX_SEGS+1);

	if (msb_init_card(card))
		goto out;

	if (msb->block_size != new_msb->block_size)
		goto out;

	if (memcmp(msb->boot_page, new_msb->boot_page,
					sizeof(struct ms_boot_page)))
		goto out;

	if (msb->logical_block_count != new_msb->logical_block_count ||
		memcmp(msb->lba_to_pba_table, new_msb->lba_to_pba_table,
						msb->logical_block_count))
		goto out;

	if (msb->block_count != new_msb->block_count ||
		memcmp(msb->used_blocks_bitmap, new_msb->used_blocks_bitmap,
							msb->block_count / 8))
		goto out;

	card_dead = false;
out:
	if (card_dead)
		dbg("Card was removed/replaced during suspend");

	msb->card_dead = card_dead;
	memstick_set_drvdata(card, msb);

	if (new_msb) {
		msb_data_clear(new_msb);
		kfree(new_msb);
	}

	msb_start(card);
	mutex_unlock(&card->host->lock);
	return 0;
}
#else

#define msb_suspend NULL
#define msb_resume NULL

#endif /* CONFIG_PM */

static struct memstick_device_id msb_id_tbl[] = {
	{MEMSTICK_MATCH_ALL, MEMSTICK_TYPE_LEGACY, MEMSTICK_CATEGORY_STORAGE,
	 MEMSTICK_CLASS_FLASH},

	{MEMSTICK_MATCH_ALL, MEMSTICK_TYPE_LEGACY, MEMSTICK_CATEGORY_STORAGE,
	 MEMSTICK_CLASS_ROM},

	{MEMSTICK_MATCH_ALL, MEMSTICK_TYPE_LEGACY, MEMSTICK_CATEGORY_STORAGE,
	 MEMSTICK_CLASS_RO},

	{MEMSTICK_MATCH_ALL, MEMSTICK_TYPE_LEGACY, MEMSTICK_CATEGORY_STORAGE,
	 MEMSTICK_CLASS_WP},

	{MEMSTICK_MATCH_ALL, MEMSTICK_TYPE_DUO, MEMSTICK_CATEGORY_STORAGE_DUO,
	 MEMSTICK_CLASS_DUO},
	{}
};
MODULE_DEVICE_TABLE(memstick, msb_id_tbl);


static struct memstick_driver msb_driver = {
	.driver = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE
	},
	.id_table = msb_id_tbl,
	.probe    = msb_probe,
	.remove   = msb_remove,
	.suspend  = msb_suspend,
	.resume   = msb_resume
};

static int major;

static int __init msb_init(void)
{
	int rc = register_blkdev(0, DRIVER_NAME);

	if (rc < 0) {
		pr_err("failed to register major (error %d)\n", rc);
		return rc;
	}

	major = rc;
	rc = memstick_register_driver(&msb_driver);
	if (rc) {
		unregister_blkdev(major, DRIVER_NAME);
		pr_err("failed to register memstick driver (error %d)\n", rc);
	}

	return rc;
}

static void __exit msb_exit(void)
{
	memstick_unregister_driver(&msb_driver);
	unregister_blkdev(major, DRIVER_NAME);
	idr_destroy(&msb_disk_idr);
}

module_init(msb_init);
module_exit(msb_exit);

module_param(cache_flush_timeout, int, S_IRUGO);
MODULE_PARM_DESC(cache_flush_timeout,
				"Cache flush timeout in msec (1000 default)");
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug level (0-2)");

module_param(verify_writes, bool, S_IRUGO);
MODULE_PARM_DESC(verify_writes, "Read back and check all data that is written");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Maxim Levitsky");
MODULE_DESCRIPTION("Sony MemoryStick block device driver");
