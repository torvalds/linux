/*
 *  drivers/s390/char/tape_std.c
 *    standard tape device functions for ibm tapes.
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001,2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *		 Michael Holzheu <holzheu@de.ibm.com>
 *		 Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Stefan Bader <shbader@de.ibm.com>
 */

#define KMSG_COMPONENT "tape"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/timer.h>

#include <asm/types.h>
#include <asm/idals.h>
#include <asm/ebcdic.h>
#include <asm/tape390.h>

#define TAPE_DBF_AREA	tape_core_dbf

#include "tape.h"
#include "tape_std.h"

/*
 * tape_std_assign
 */
static void
tape_std_assign_timeout(unsigned long data)
{
	struct tape_request *	request;
	struct tape_device *	device;
	int rc;

	request = (struct tape_request *) data;
	device = request->device;
	BUG_ON(!device);

	DBF_EVENT(3, "%08x: Assignment timeout. Device busy.\n",
			device->cdev_id);
	rc = tape_cancel_io(device, request);
	if(rc)
		DBF_EVENT(3, "(%s): Assign timeout: Cancel failed with rc = %i\n",
			dev_name(&device->cdev->dev), rc);
}

int
tape_std_assign(struct tape_device *device)
{
	int                  rc;
	struct timer_list    timeout;
	struct tape_request *request;

	request = tape_alloc_request(2, 11);
	if (IS_ERR(request))
		return PTR_ERR(request);

	request->op = TO_ASSIGN;
	tape_ccw_cc(request->cpaddr, ASSIGN, 11, request->cpdata);
	tape_ccw_end(request->cpaddr + 1, NOP, 0, NULL);

	/*
	 * The assign command sometimes blocks if the device is assigned
	 * to another host (actually this shouldn't happen but it does).
	 * So we set up a timeout for this call.
	 */
	init_timer_on_stack(&timeout);
	timeout.function = tape_std_assign_timeout;
	timeout.data     = (unsigned long) request;
	timeout.expires  = jiffies + 2 * HZ;
	add_timer(&timeout);

	rc = tape_do_io_interruptible(device, request);

	del_timer(&timeout);

	if (rc != 0) {
		DBF_EVENT(3, "%08x: assign failed - device might be busy\n",
			device->cdev_id);
	} else {
		DBF_EVENT(3, "%08x: Tape assigned\n", device->cdev_id);
	}
	tape_free_request(request);
	return rc;
}

/*
 * tape_std_unassign
 */
int
tape_std_unassign (struct tape_device *device)
{
	int                  rc;
	struct tape_request *request;

	if (device->tape_state == TS_NOT_OPER) {
		DBF_EVENT(3, "(%08x): Can't unassign device\n",
			device->cdev_id);
		return -EIO;
	}

	request = tape_alloc_request(2, 11);
	if (IS_ERR(request))
		return PTR_ERR(request);

	request->op = TO_UNASSIGN;
	tape_ccw_cc(request->cpaddr, UNASSIGN, 11, request->cpdata);
	tape_ccw_end(request->cpaddr + 1, NOP, 0, NULL);

	if ((rc = tape_do_io(device, request)) != 0) {
		DBF_EVENT(3, "%08x: Unassign failed\n", device->cdev_id);
	} else {
		DBF_EVENT(3, "%08x: Tape unassigned\n", device->cdev_id);
	}
	tape_free_request(request);
	return rc;
}

/*
 * TAPE390_DISPLAY: Show a string on the tape display.
 */
int
tape_std_display(struct tape_device *device, struct display_struct *disp)
{
	struct tape_request *request;
	int rc;

	request = tape_alloc_request(2, 17);
	if (IS_ERR(request)) {
		DBF_EVENT(3, "TAPE: load display failed\n");
		return PTR_ERR(request);
	}
	request->op = TO_DIS;

	*(unsigned char *) request->cpdata = disp->cntrl;
	DBF_EVENT(5, "TAPE: display cntrl=%04x\n", disp->cntrl);
	memcpy(((unsigned char *) request->cpdata) + 1, disp->message1, 8);
	memcpy(((unsigned char *) request->cpdata) + 9, disp->message2, 8);
	ASCEBC(((unsigned char*) request->cpdata) + 1, 16);

	tape_ccw_cc(request->cpaddr, LOAD_DISPLAY, 17, request->cpdata);
	tape_ccw_end(request->cpaddr + 1, NOP, 0, NULL);

	rc = tape_do_io_interruptible(device, request);
	tape_free_request(request);
	return rc;
}

/*
 * Read block id.
 */
int
tape_std_read_block_id(struct tape_device *device, __u64 *id)
{
	struct tape_request *request;
	int rc;

	request = tape_alloc_request(3, 8);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_RBI;
	/* setup ccws */
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	tape_ccw_cc(request->cpaddr + 1, READ_BLOCK_ID, 8, request->cpdata);
	tape_ccw_end(request->cpaddr + 2, NOP, 0, NULL);
	/* execute it */
	rc = tape_do_io(device, request);
	if (rc == 0)
		/* Get result from read buffer. */
		*id = *(__u64 *) request->cpdata;
	tape_free_request(request);
	return rc;
}

int
tape_std_terminate_write(struct tape_device *device)
{
	int rc;

	if(device->required_tapemarks == 0)
		return 0;

	DBF_LH(5, "tape%d: terminate write %dxEOF\n", device->first_minor,
		device->required_tapemarks);

	rc = tape_mtop(device, MTWEOF, device->required_tapemarks);
	if (rc)
		return rc;

	device->required_tapemarks = 0;
	return tape_mtop(device, MTBSR, 1);
}

/*
 * MTLOAD: Loads the tape.
 * The default implementation just wait until the tape medium state changes
 * to MS_LOADED.
 */
int
tape_std_mtload(struct tape_device *device, int count)
{
	return wait_event_interruptible(device->state_change_wq,
		(device->medium_state == MS_LOADED));
}

/*
 * MTSETBLK: Set block size.
 */
int
tape_std_mtsetblk(struct tape_device *device, int count)
{
	struct idal_buffer *new;

	DBF_LH(6, "tape_std_mtsetblk(%d)\n", count);
	if (count <= 0) {
		/*
		 * Just set block_size to 0. tapechar_read/tapechar_write
		 * will realloc the idal buffer if a bigger one than the
		 * current is needed.
		 */
		device->char_data.block_size = 0;
		return 0;
	}
	if (device->char_data.idal_buf != NULL &&
	    device->char_data.idal_buf->size == count)
		/* We already have a idal buffer of that size. */
		return 0;

	if (count > MAX_BLOCKSIZE) {
		DBF_EVENT(3, "Invalid block size (%d > %d) given.\n",
			count, MAX_BLOCKSIZE);
		return -EINVAL;
	}

	/* Allocate a new idal buffer. */
	new = idal_buffer_alloc(count, 0);
	if (IS_ERR(new))
		return -ENOMEM;
	if (device->char_data.idal_buf != NULL)
		idal_buffer_free(device->char_data.idal_buf);
	device->char_data.idal_buf = new;
	device->char_data.block_size = count;

	DBF_LH(6, "new blocksize is %d\n", device->char_data.block_size);

	return 0;
}

/*
 * MTRESET: Set block size to 0.
 */
int
tape_std_mtreset(struct tape_device *device, int count)
{
	DBF_EVENT(6, "TCHAR:devreset:\n");
	device->char_data.block_size = 0;
	return 0;
}

/*
 * MTFSF: Forward space over 'count' file marks. The tape is positioned
 * at the EOT (End of Tape) side of the file mark.
 */
int
tape_std_mtfsf(struct tape_device *device, int mt_count)
{
	struct tape_request *request;
	struct ccw1 *ccw;

	request = tape_alloc_request(mt_count + 2, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_FSF;
	/* setup ccws */
	ccw = tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1,
			  device->modeset_byte);
	ccw = tape_ccw_repeat(ccw, FORSPACEFILE, mt_count);
	ccw = tape_ccw_end(ccw, NOP, 0, NULL);

	/* execute it */
	return tape_do_io_free(device, request);
}

/*
 * MTFSR: Forward space over 'count' tape blocks (blocksize is set
 * via MTSETBLK.
 */
int
tape_std_mtfsr(struct tape_device *device, int mt_count)
{
	struct tape_request *request;
	struct ccw1 *ccw;
	int rc;

	request = tape_alloc_request(mt_count + 2, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_FSB;
	/* setup ccws */
	ccw = tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1,
			  device->modeset_byte);
	ccw = tape_ccw_repeat(ccw, FORSPACEBLOCK, mt_count);
	ccw = tape_ccw_end(ccw, NOP, 0, NULL);

	/* execute it */
	rc = tape_do_io(device, request);
	if (rc == 0 && request->rescnt > 0) {
		DBF_LH(3, "FSR over tapemark\n");
		rc = 1;
	}
	tape_free_request(request);

	return rc;
}

/*
 * MTBSR: Backward space over 'count' tape blocks.
 * (blocksize is set via MTSETBLK.
 */
int
tape_std_mtbsr(struct tape_device *device, int mt_count)
{
	struct tape_request *request;
	struct ccw1 *ccw;
	int rc;

	request = tape_alloc_request(mt_count + 2, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_BSB;
	/* setup ccws */
	ccw = tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1,
			  device->modeset_byte);
	ccw = tape_ccw_repeat(ccw, BACKSPACEBLOCK, mt_count);
	ccw = tape_ccw_end(ccw, NOP, 0, NULL);

	/* execute it */
	rc = tape_do_io(device, request);
	if (rc == 0 && request->rescnt > 0) {
		DBF_LH(3, "BSR over tapemark\n");
		rc = 1;
	}
	tape_free_request(request);

	return rc;
}

/*
 * MTWEOF: Write 'count' file marks at the current position.
 */
int
tape_std_mtweof(struct tape_device *device, int mt_count)
{
	struct tape_request *request;
	struct ccw1 *ccw;

	request = tape_alloc_request(mt_count + 2, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_WTM;
	/* setup ccws */
	ccw = tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1,
			  device->modeset_byte);
	ccw = tape_ccw_repeat(ccw, WRITETAPEMARK, mt_count);
	ccw = tape_ccw_end(ccw, NOP, 0, NULL);

	/* execute it */
	return tape_do_io_free(device, request);
}

/*
 * MTBSFM: Backward space over 'count' file marks.
 * The tape is positioned at the BOT (Begin Of Tape) side of the
 * last skipped file mark.
 */
int
tape_std_mtbsfm(struct tape_device *device, int mt_count)
{
	struct tape_request *request;
	struct ccw1 *ccw;

	request = tape_alloc_request(mt_count + 2, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_BSF;
	/* setup ccws */
	ccw = tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1,
			  device->modeset_byte);
	ccw = tape_ccw_repeat(ccw, BACKSPACEFILE, mt_count);
	ccw = tape_ccw_end(ccw, NOP, 0, NULL);

	/* execute it */
	return tape_do_io_free(device, request);
}

/*
 * MTBSF: Backward space over 'count' file marks. The tape is positioned at
 * the EOT (End of Tape) side of the last skipped file mark.
 */
int
tape_std_mtbsf(struct tape_device *device, int mt_count)
{
	struct tape_request *request;
	struct ccw1 *ccw;
	int rc;

	request = tape_alloc_request(mt_count + 2, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_BSF;
	/* setup ccws */
	ccw = tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1,
			  device->modeset_byte);
	ccw = tape_ccw_repeat(ccw, BACKSPACEFILE, mt_count);
	ccw = tape_ccw_end(ccw, NOP, 0, NULL);
	/* execute it */
	rc = tape_do_io_free(device, request);
	if (rc == 0) {
		rc = tape_mtop(device, MTFSR, 1);
		if (rc > 0)
			rc = 0;
	}
	return rc;
}

/*
 * MTFSFM: Forward space over 'count' file marks.
 * The tape is positioned at the BOT (Begin Of Tape) side
 * of the last skipped file mark.
 */
int
tape_std_mtfsfm(struct tape_device *device, int mt_count)
{
	struct tape_request *request;
	struct ccw1 *ccw;
	int rc;

	request = tape_alloc_request(mt_count + 2, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_FSF;
	/* setup ccws */
	ccw = tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1,
			  device->modeset_byte);
	ccw = tape_ccw_repeat(ccw, FORSPACEFILE, mt_count);
	ccw = tape_ccw_end(ccw, NOP, 0, NULL);
	/* execute it */
	rc = tape_do_io_free(device, request);
	if (rc == 0) {
		rc = tape_mtop(device, MTBSR, 1);
		if (rc > 0)
			rc = 0;
	}

	return rc;
}

/*
 * MTREW: Rewind the tape.
 */
int
tape_std_mtrew(struct tape_device *device, int mt_count)
{
	struct tape_request *request;

	request = tape_alloc_request(3, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_REW;
	/* setup ccws */
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1,
		    device->modeset_byte);
	tape_ccw_cc(request->cpaddr + 1, REWIND, 0, NULL);
	tape_ccw_end(request->cpaddr + 2, NOP, 0, NULL);

	/* execute it */
	return tape_do_io_free(device, request);
}

/*
 * MTOFFL: Rewind the tape and put the drive off-line.
 * Implement 'rewind unload'
 */
int
tape_std_mtoffl(struct tape_device *device, int mt_count)
{
	struct tape_request *request;

	request = tape_alloc_request(3, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_RUN;
	/* setup ccws */
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	tape_ccw_cc(request->cpaddr + 1, REWIND_UNLOAD, 0, NULL);
	tape_ccw_end(request->cpaddr + 2, NOP, 0, NULL);

	/* execute it */
	return tape_do_io_free(device, request);
}

/*
 * MTNOP: 'No operation'.
 */
int
tape_std_mtnop(struct tape_device *device, int mt_count)
{
	struct tape_request *request;

	request = tape_alloc_request(2, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_NOP;
	/* setup ccws */
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	tape_ccw_end(request->cpaddr + 1, NOP, 0, NULL);
	/* execute it */
	return tape_do_io_free(device, request);
}

/*
 * MTEOM: positions at the end of the portion of the tape already used
 * for recordind data. MTEOM positions after the last file mark, ready for
 * appending another file.
 */
int
tape_std_mteom(struct tape_device *device, int mt_count)
{
	int rc;

	/*
	 * Seek from the beginning of tape (rewind).
	 */
	if ((rc = tape_mtop(device, MTREW, 1)) < 0)
		return rc;

	/*
	 * The logical end of volume is given by two sewuential tapemarks.
	 * Look for this by skipping to the next file (over one tapemark)
	 * and then test for another one (fsr returns 1 if a tapemark was
	 * encountered).
	 */
	do {
		if ((rc = tape_mtop(device, MTFSF, 1)) < 0)
			return rc;
		if ((rc = tape_mtop(device, MTFSR, 1)) < 0)
			return rc;
	} while (rc == 0);

	return tape_mtop(device, MTBSR, 1);
}

/*
 * MTRETEN: Retension the tape, i.e. forward space to end of tape and rewind.
 */
int
tape_std_mtreten(struct tape_device *device, int mt_count)
{
	struct tape_request *request;
	int rc;

	request = tape_alloc_request(4, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_FSF;
	/* setup ccws */
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	tape_ccw_cc(request->cpaddr + 1,FORSPACEFILE, 0, NULL);
	tape_ccw_cc(request->cpaddr + 2, NOP, 0, NULL);
	tape_ccw_end(request->cpaddr + 3, CCW_CMD_TIC, 0, request->cpaddr);
	/* execute it, MTRETEN rc gets ignored */
	rc = tape_do_io_interruptible(device, request);
	tape_free_request(request);
	return tape_mtop(device, MTREW, 1);
}

/*
 * MTERASE: erases the tape.
 */
int
tape_std_mterase(struct tape_device *device, int mt_count)
{
	struct tape_request *request;

	request = tape_alloc_request(6, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_DSE;
	/* setup ccws */
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	tape_ccw_cc(request->cpaddr + 1, REWIND, 0, NULL);
	tape_ccw_cc(request->cpaddr + 2, ERASE_GAP, 0, NULL);
	tape_ccw_cc(request->cpaddr + 3, DATA_SEC_ERASE, 0, NULL);
	tape_ccw_cc(request->cpaddr + 4, REWIND, 0, NULL);
	tape_ccw_end(request->cpaddr + 5, NOP, 0, NULL);

	/* execute it */
	return tape_do_io_free(device, request);
}

/*
 * MTUNLOAD: Rewind the tape and unload it.
 */
int
tape_std_mtunload(struct tape_device *device, int mt_count)
{
	return tape_mtop(device, MTOFFL, mt_count);
}

/*
 * MTCOMPRESSION: used to enable compression.
 * Sets the IDRC on/off.
 */
int
tape_std_mtcompression(struct tape_device *device, int mt_count)
{
	struct tape_request *request;

	if (mt_count < 0 || mt_count > 1) {
		DBF_EXCEPTION(6, "xcom parm\n");
		return -EINVAL;
	}
	request = tape_alloc_request(2, 0);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_NOP;
	/* setup ccws */
	if (mt_count == 0)
		*device->modeset_byte &= ~0x08;
	else
		*device->modeset_byte |= 0x08;
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	tape_ccw_end(request->cpaddr + 1, NOP, 0, NULL);
	/* execute it */
	return tape_do_io_free(device, request);
}

/*
 * Read Block
 */
struct tape_request *
tape_std_read_block(struct tape_device *device, size_t count)
{
	struct tape_request *request;

	/*
	 * We have to alloc 4 ccws in order to be able to transform request
	 * into a read backward request in error case.
	 */
	request = tape_alloc_request(4, 0);
	if (IS_ERR(request)) {
		DBF_EXCEPTION(6, "xrbl fail");
		return request;
	}
	request->op = TO_RFO;
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	tape_ccw_end_idal(request->cpaddr + 1, READ_FORWARD,
			  device->char_data.idal_buf);
	DBF_EVENT(6, "xrbl ccwg\n");
	return request;
}

/*
 * Read Block backward transformation function.
 */
void
tape_std_read_backward(struct tape_device *device, struct tape_request *request)
{
	/*
	 * We have allocated 4 ccws in tape_std_read, so we can now
	 * transform the request to a read backward, followed by a
	 * forward space block.
	 */
	request->op = TO_RBA;
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	tape_ccw_cc_idal(request->cpaddr + 1, READ_BACKWARD,
			 device->char_data.idal_buf);
	tape_ccw_cc(request->cpaddr + 2, FORSPACEBLOCK, 0, NULL);
	tape_ccw_end(request->cpaddr + 3, NOP, 0, NULL);
	DBF_EVENT(6, "xrop ccwg");}

/*
 * Write Block
 */
struct tape_request *
tape_std_write_block(struct tape_device *device, size_t count)
{
	struct tape_request *request;

	request = tape_alloc_request(2, 0);
	if (IS_ERR(request)) {
		DBF_EXCEPTION(6, "xwbl fail\n");
		return request;
	}
	request->op = TO_WRI;
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	tape_ccw_end_idal(request->cpaddr + 1, WRITE_CMD,
			  device->char_data.idal_buf);
	DBF_EVENT(6, "xwbl ccwg\n");
	return request;
}

/*
 * This routine is called by frontend after an ENOSP on write
 */
void
tape_std_process_eov(struct tape_device *device)
{
	/*
	 * End of volume: We have to backspace the last written record, then
	 * we TRY to write a tapemark and then backspace over the written TM
	 */
	if (tape_mtop(device, MTBSR, 1) == 0 &&
	    tape_mtop(device, MTWEOF, 1) == 0) {
		tape_mtop(device, MTBSR, 1);
	}
}

EXPORT_SYMBOL(tape_std_assign);
EXPORT_SYMBOL(tape_std_unassign);
EXPORT_SYMBOL(tape_std_display);
EXPORT_SYMBOL(tape_std_read_block_id);
EXPORT_SYMBOL(tape_std_mtload);
EXPORT_SYMBOL(tape_std_mtsetblk);
EXPORT_SYMBOL(tape_std_mtreset);
EXPORT_SYMBOL(tape_std_mtfsf);
EXPORT_SYMBOL(tape_std_mtfsr);
EXPORT_SYMBOL(tape_std_mtbsr);
EXPORT_SYMBOL(tape_std_mtweof);
EXPORT_SYMBOL(tape_std_mtbsfm);
EXPORT_SYMBOL(tape_std_mtbsf);
EXPORT_SYMBOL(tape_std_mtfsfm);
EXPORT_SYMBOL(tape_std_mtrew);
EXPORT_SYMBOL(tape_std_mtoffl);
EXPORT_SYMBOL(tape_std_mtnop);
EXPORT_SYMBOL(tape_std_mteom);
EXPORT_SYMBOL(tape_std_mtreten);
EXPORT_SYMBOL(tape_std_mterase);
EXPORT_SYMBOL(tape_std_mtunload);
EXPORT_SYMBOL(tape_std_mtcompression);
EXPORT_SYMBOL(tape_std_read_block);
EXPORT_SYMBOL(tape_std_read_backward);
EXPORT_SYMBOL(tape_std_write_block);
EXPORT_SYMBOL(tape_std_process_eov);
