/*
 *  drivers/s390/char/sclp_rw.c
 *     driver: reading from and writing to system console on S/390 via SCLP
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/kmod.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <asm/uaccess.h>

#include "sclp.h"
#include "sclp_rw.h"

/*
 * The room for the SCCB (only for writing) is not equal to a pages size
 * (as it is specified as the maximum size in the SCLP documentation)
 * because of the additional data structure described above.
 */
#define MAX_SCCB_ROOM (PAGE_SIZE - sizeof(struct sclp_buffer))

/* Event type structure for write message and write priority message */
static struct sclp_register sclp_rw_event = {
	.send_mask = EVTYP_MSG_MASK | EVTYP_PMSGCMD_MASK
};

/*
 * Setup a sclp write buffer. Gets a page as input (4K) and returns
 * a pointer to a struct sclp_buffer structure that is located at the
 * end of the input page. This reduces the buffer space by a few
 * bytes but simplifies things.
 */
struct sclp_buffer *
sclp_make_buffer(void *page, unsigned short columns, unsigned short htab)
{
	struct sclp_buffer *buffer;
	struct write_sccb *sccb;

	sccb = (struct write_sccb *) page;
	/*
	 * We keep the struct sclp_buffer structure at the end
	 * of the sccb page.
	 */
	buffer = ((struct sclp_buffer *) ((addr_t) sccb + PAGE_SIZE)) - 1;
	buffer->sccb = sccb;
	buffer->retry_count = 0;
	buffer->mto_number = 0;
	buffer->mto_char_sum = 0;
	buffer->current_line = NULL;
	buffer->current_length = 0;
	buffer->columns = columns;
	buffer->htab = htab;

	/* initialize sccb */
	memset(sccb, 0, sizeof(struct write_sccb));
	sccb->header.length = sizeof(struct write_sccb);
	sccb->msg_buf.header.length = sizeof(struct msg_buf);
	sccb->msg_buf.header.type = EVTYP_MSG;
	sccb->msg_buf.mdb.header.length = sizeof(struct mdb);
	sccb->msg_buf.mdb.header.type = 1;
	sccb->msg_buf.mdb.header.tag = 0xD4C4C240;	/* ebcdic "MDB " */
	sccb->msg_buf.mdb.header.revision_code = 1;
	sccb->msg_buf.mdb.go.length = sizeof(struct go);
	sccb->msg_buf.mdb.go.type = 1;

	return buffer;
}

/*
 * Return a pointer to the original page that has been used to create
 * the buffer.
 */
void *
sclp_unmake_buffer(struct sclp_buffer *buffer)
{
	return buffer->sccb;
}

/*
 * Initialize a new Message Text Object (MTO) at the end of the provided buffer
 * with enough room for max_len characters. Return 0 on success.
 */
static int
sclp_initialize_mto(struct sclp_buffer *buffer, int max_len)
{
	struct write_sccb *sccb;
	struct mto *mto;
	int mto_size;

	/* max size of new Message Text Object including message text  */
	mto_size = sizeof(struct mto) + max_len;

	/* check if current buffer sccb can contain the mto */
	sccb = buffer->sccb;
	if ((MAX_SCCB_ROOM - sccb->header.length) < mto_size)
		return -ENOMEM;

	/* find address of new message text object */
	mto = (struct mto *)(((addr_t) sccb) + sccb->header.length);

	/*
	 * fill the new Message-Text Object,
	 * starting behind the former last byte of the SCCB
	 */
	memset(mto, 0, sizeof(struct mto));
	mto->length = sizeof(struct mto);
	mto->type = 4;	/* message text object */
	mto->line_type_flags = LNTPFLGS_ENDTEXT; /* end text */

	/* set pointer to first byte after struct mto. */
	buffer->current_line = (char *) (mto + 1);
	buffer->current_length = 0;

	return 0;
}

/*
 * Finalize MTO initialized by sclp_initialize_mto(), updating the sizes of
 * MTO, enclosing MDB, event buffer and SCCB.
 */
static void
sclp_finalize_mto(struct sclp_buffer *buffer)
{
	struct write_sccb *sccb;
	struct mto *mto;
	int str_len, mto_size;

	str_len = buffer->current_length;
	buffer->current_line = NULL;
	buffer->current_length = 0;

	/* real size of new Message Text Object including message text	*/
	mto_size = sizeof(struct mto) + str_len;

	/* find address of new message text object */
	sccb = buffer->sccb;
	mto = (struct mto *)(((addr_t) sccb) + sccb->header.length);

	/* set size of message text object */
	mto->length = mto_size;

	/*
	 * update values of sizes
	 * (SCCB, Event(Message) Buffer, Message Data Block)
	 */
	sccb->header.length += mto_size;
	sccb->msg_buf.header.length += mto_size;
	sccb->msg_buf.mdb.header.length += mto_size;

	/*
	 * count number of buffered messages (= number of Message Text
	 * Objects) and number of buffered characters
	 * for the SCCB currently used for buffering and at all
	 */
	buffer->mto_number++;
	buffer->mto_char_sum += str_len;
}

/*
 * processing of a message including escape characters,
 * returns number of characters written to the output sccb
 * ("processed" means that is not guaranteed that the character have already
 *  been sent to the SCLP but that it will be done at least next time the SCLP
 *  is not busy)
 */
int
sclp_write(struct sclp_buffer *buffer, const unsigned char *msg, int count)
{
	int spaces, i_msg;
	int rc;

	/*
	 * parse msg for escape sequences (\t,\v ...) and put formated
	 * msg into an mto (created by sclp_initialize_mto).
	 *
	 * We have to do this work ourselfs because there is no support for
	 * these characters on the native machine and only partial support
	 * under VM (Why does VM interpret \n but the native machine doesn't ?)
	 *
	 * Depending on i/o-control setting the message is always written
	 * immediately or we wait for a final new line maybe coming with the
	 * next message. Besides we avoid a buffer overrun by writing its
	 * content.
	 *
	 * RESTRICTIONS:
	 *
	 * \r and \b work within one line because we are not able to modify
	 * previous output that have already been accepted by the SCLP.
	 *
	 * \t combined with following \r is not correctly represented because
	 * \t is expanded to some spaces but \r does not know about a
	 * previous \t and decreases the current position by one column.
	 * This is in order to a slim and quick implementation.
	 */
	for (i_msg = 0; i_msg < count; i_msg++) {
		switch (msg[i_msg]) {
		case '\n':	/* new line, line feed (ASCII)	*/
			/* check if new mto needs to be created */
			if (buffer->current_line == NULL) {
				rc = sclp_initialize_mto(buffer, 0);
				if (rc)
					return i_msg;
			}
			sclp_finalize_mto(buffer);
			break;
		case '\a':	/* bell, one for several times	*/
			/* set SCLP sound alarm bit in General Object */
			buffer->sccb->msg_buf.mdb.go.general_msg_flags |=
				GNRLMSGFLGS_SNDALRM;
			break;
		case '\t':	/* horizontal tabulator	 */
			/* check if new mto needs to be created */
			if (buffer->current_line == NULL) {
				rc = sclp_initialize_mto(buffer,
							 buffer->columns);
				if (rc)
					return i_msg;
			}
			/* "go to (next htab-boundary + 1, same line)" */
			do {
				if (buffer->current_length >= buffer->columns)
					break;
				/* ok, add a blank */
				*buffer->current_line++ = 0x40;
				buffer->current_length++;
			} while (buffer->current_length % buffer->htab);
			break;
		case '\f':	/* form feed  */
		case '\v':	/* vertical tabulator  */
			/* "go to (actual column, actual line + 1)" */
			/* = new line, leading spaces */
			if (buffer->current_line != NULL) {
				spaces = buffer->current_length;
				sclp_finalize_mto(buffer);
				rc = sclp_initialize_mto(buffer,
							 buffer->columns);
				if (rc)
					return i_msg;
				memset(buffer->current_line, 0x40, spaces);
				buffer->current_line += spaces;
				buffer->current_length = spaces;
			} else {
				/* one an empty line this is the same as \n */
				rc = sclp_initialize_mto(buffer,
							 buffer->columns);
				if (rc)
					return i_msg;
				sclp_finalize_mto(buffer);
			}
			break;
		case '\b':	/* backspace  */
			/* "go to (actual column - 1, actual line)" */
			/* decrement counter indicating position, */
			/* do not remove last character */
			if (buffer->current_line != NULL &&
			    buffer->current_length > 0) {
				buffer->current_length--;
				buffer->current_line--;
			}
			break;
		case 0x00:	/* end of string  */
			/* transfer current line to SCCB */
			if (buffer->current_line != NULL)
				sclp_finalize_mto(buffer);
			/* skip the rest of the message including the 0 byte */
			i_msg = count - 1;
			break;
		default:	/* no escape character	*/
			/* do not output unprintable characters */
			if (!isprint(msg[i_msg]))
				break;
			/* check if new mto needs to be created */
			if (buffer->current_line == NULL) {
				rc = sclp_initialize_mto(buffer,
							 buffer->columns);
				if (rc)
					return i_msg;
			}
			*buffer->current_line++ = sclp_ascebc(msg[i_msg]);
			buffer->current_length++;
			break;
		}
		/* check if current mto is full */
		if (buffer->current_line != NULL &&
		    buffer->current_length >= buffer->columns)
			sclp_finalize_mto(buffer);
	}

	/* return number of processed characters */
	return i_msg;
}

/*
 * Return the number of free bytes in the sccb
 */
int
sclp_buffer_space(struct sclp_buffer *buffer)
{
	int count;

	count = MAX_SCCB_ROOM - buffer->sccb->header.length;
	if (buffer->current_line != NULL)
		count -= sizeof(struct mto) + buffer->current_length;
	return count;
}

/*
 * Return number of characters in buffer
 */
int
sclp_chars_in_buffer(struct sclp_buffer *buffer)
{
	int count;

	count = buffer->mto_char_sum;
	if (buffer->current_line != NULL)
		count += buffer->current_length;
	return count;
}

/*
 * sets or provides some values that influence the drivers behaviour
 */
void
sclp_set_columns(struct sclp_buffer *buffer, unsigned short columns)
{
	buffer->columns = columns;
	if (buffer->current_line != NULL &&
	    buffer->current_length > buffer->columns)
		sclp_finalize_mto(buffer);
}

void
sclp_set_htab(struct sclp_buffer *buffer, unsigned short htab)
{
	buffer->htab = htab;
}

/*
 * called by sclp_console_init and/or sclp_tty_init
 */
int
sclp_rw_init(void)
{
	static int init_done = 0;
	int rc;

	if (init_done)
		return 0;

	rc = sclp_register(&sclp_rw_event);
	if (rc == 0)
		init_done = 1;
	return rc;
}

#define SCLP_BUFFER_MAX_RETRY		1

/*
 * second half of Write Event Data-function that has to be done after
 * interruption indicating completion of Service Call.
 */
static void
sclp_writedata_callback(struct sclp_req *request, void *data)
{
	int rc;
	struct sclp_buffer *buffer;
	struct write_sccb *sccb;

	buffer = (struct sclp_buffer *) data;
	sccb = buffer->sccb;

	if (request->status == SCLP_REQ_FAILED) {
		if (buffer->callback != NULL)
			buffer->callback(buffer, -EIO);
		return;
	}
	/* check SCLP response code and choose suitable action	*/
	switch (sccb->header.response_code) {
	case 0x0020 :
		/* Normal completion, buffer processed, message(s) sent */
		rc = 0;
		break;

	case 0x0340: /* Contained SCLP equipment check */
		if (++buffer->retry_count > SCLP_BUFFER_MAX_RETRY) {
			rc = -EIO;
			break;
		}
		/* remove processed buffers and requeue rest */
		if (sclp_remove_processed((struct sccb_header *) sccb) > 0) {
			/* not all buffers were processed */
			sccb->header.response_code = 0x0000;
			buffer->request.status = SCLP_REQ_FILLED;
			rc = sclp_add_request(request);
			if (rc == 0)
				return;
		} else
			rc = 0;
		break;

	case 0x0040: /* SCLP equipment check */
	case 0x05f0: /* Target resource in improper state */
		if (++buffer->retry_count > SCLP_BUFFER_MAX_RETRY) {
			rc = -EIO;
			break;
		}
		/* retry request */
		sccb->header.response_code = 0x0000;
		buffer->request.status = SCLP_REQ_FILLED;
		rc = sclp_add_request(request);
		if (rc == 0)
			return;
		break;
	default:
		if (sccb->header.response_code == 0x71f0)
			rc = -ENOMEM;
		else
			rc = -EINVAL;
		break;
	}
	if (buffer->callback != NULL)
		buffer->callback(buffer, rc);
}

/*
 * Setup the request structure in the struct sclp_buffer to do SCLP Write
 * Event Data and pass the request to the core SCLP loop. Return zero on
 * success, non-zero otherwise.
 */
int
sclp_emit_buffer(struct sclp_buffer *buffer,
		 void (*callback)(struct sclp_buffer *, int))
{
	struct write_sccb *sccb;

	/* add current line if there is one */
	if (buffer->current_line != NULL)
		sclp_finalize_mto(buffer);

	/* Are there messages in the output buffer ? */
	if (buffer->mto_number == 0)
		return -EIO;

	sccb = buffer->sccb;
	if (sclp_rw_event.sclp_receive_mask & EVTYP_MSG_MASK)
		/* Use normal write message */
		sccb->msg_buf.header.type = EVTYP_MSG;
	else if (sclp_rw_event.sclp_receive_mask & EVTYP_PMSGCMD_MASK)
		/* Use write priority message */
		sccb->msg_buf.header.type = EVTYP_PMSGCMD;
	else
		return -ENOSYS;
	buffer->request.command = SCLP_CMDW_WRITE_EVENT_DATA;
	buffer->request.status = SCLP_REQ_FILLED;
	buffer->request.callback = sclp_writedata_callback;
	buffer->request.callback_data = buffer;
	buffer->request.sccb = sccb;
	buffer->callback = callback;
	return sclp_add_request(&buffer->request);
}
