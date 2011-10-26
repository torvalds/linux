/*
 * Freescale UUT driver
 *
 * Copyright 2008-2014 Freescale Semiconductor, Inc.
 * Copyright 2008-2009 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

static u64 get_be64(u8 *buf)
{
	return ((u64)get_unaligned_be32(buf) << 32) |
		get_unaligned_be32(buf + 4);
}

static int utp_init(struct fsg_dev *fsg)
{
	init_waitqueue_head(&utp_context.wq);
	init_waitqueue_head(&utp_context.list_full_wq);

	INIT_LIST_HEAD(&utp_context.read);
	INIT_LIST_HEAD(&utp_context.write);
	mutex_init(&utp_context.lock);

	/* the max message is 64KB */
	utp_context.buffer = vmalloc(0x10000);
	if (!utp_context.buffer)
		return -EIO;
	utp_context.utp_version = 0x1ull;
	fsg->utp = &utp_context;
	return misc_register(&utp_dev);
}

static void utp_exit(struct fsg_dev *fsg)
{
	vfree(utp_context.buffer);
	misc_deregister(&utp_dev);
}

static struct utp_user_data *utp_user_data_alloc(size_t size)
{
	struct utp_user_data *uud;

	uud = vmalloc(size + sizeof(*uud));
	if (!uud)
		return uud;
	memset(uud, 0, size + sizeof(*uud));
	uud->data.size = size + sizeof(uud->data);
	INIT_LIST_HEAD(&uud->link);
	return uud;
}

static void utp_user_data_free(struct utp_user_data *uud)
{
	mutex_lock(&utp_context.lock);
	list_del(&uud->link);
	mutex_unlock(&utp_context.lock);
	vfree(uud);
}

/* Get the number of element for list */
static u32 count_list(struct list_head *l)
{
	u32 count = 0;
	struct list_head *tmp;

	mutex_lock(&utp_context.lock);
	list_for_each(tmp, l) {
		count++;
	}
	mutex_unlock(&utp_context.lock);

	return count;
}
/* The routine will not go on if utp_context.queue is empty */
#define WAIT_ACTIVITY(queue) \
 wait_event_interruptible(utp_context.wq, !list_empty(&utp_context.queue))

/* Called by userspace program (uuc) */
static ssize_t utp_file_read(struct file *file,
			     char __user *buf,
			     size_t size,
			     loff_t *off)
{
	struct utp_user_data *uud;
	size_t size_to_put;
	int free = 0;

	WAIT_ACTIVITY(read);

	mutex_lock(&utp_context.lock);
	uud = list_first_entry(&utp_context.read, struct utp_user_data, link);
	mutex_unlock(&utp_context.lock);
	size_to_put = uud->data.size;

	if (size >= size_to_put)
		free = !0;
	if (copy_to_user(buf, &uud->data, size_to_put)) {
		printk(KERN_INFO "[ %s ] copy error\n", __func__);
		return -EACCES;
	}
	if (free)
		utp_user_data_free(uud);
	else {
		pr_info("sizeof = %d, size = %d\n",
			sizeof(uud->data),
			uud->data.size);

		pr_err("Will not free utp_user_data, because buffer size = %d,"
			"need to put %d\n", size, size_to_put);
	}

	/*
	 * The user program has already finished data process,
	 * go on getting data from the host
	 */
	wake_up(&utp_context.list_full_wq);

	return size_to_put;
}

static ssize_t utp_file_write(struct file *file, const char __user *buf,
				size_t size, loff_t *off)
{
	struct utp_user_data *uud;

	if (size < sizeof(uud->data))
		return -EINVAL;
	uud = utp_user_data_alloc(size);
	if (uud == NULL)
		return -ENOMEM;
	if (copy_from_user(&uud->data, buf, size)) {
		printk(KERN_INFO "[ %s ] copy error!\n", __func__);
		vfree(uud);
		return -EACCES;
	}
	mutex_lock(&utp_context.lock);
	list_add_tail(&uud->link, &utp_context.write);
	/* Go on EXEC routine process */
	wake_up(&utp_context.wq);
	mutex_unlock(&utp_context.lock);
	return size;
}

/*
 * uuc should change to use soc bus infrastructure to soc information
 * /sys/devices/soc0/soc_id
 * this function can be removed.
 */
static long
utp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int cpu_id = 0;

	switch (cmd) {
	case UTP_GET_CPU_ID:
		return put_user(cpu_id, (int __user *)arg);
	default:
		return -ENOIOCTLCMD;
	}
}

/* Will be called when the host wants to get the sense data */
static int utp_get_sense(struct fsg_dev *fsg)
{
	if (UTP_CTX(fsg)->processed == 0)
		return -1;

	UTP_CTX(fsg)->processed = 0;
	return 0;
}

static int utp_do_read(struct fsg_dev *fsg, void *data, size_t size)
{
	struct fsg_buffhd	*bh;
	int			rc;
	u32			amount_left;
	unsigned int		amount;

	/* Get the starting Logical Block Address and check that it's
	 * not too big */

	amount_left = size;
	if (unlikely(amount_left == 0))
		return -EIO;		/* No default reply*/

	pr_debug("%s: sending %d\n", __func__, size);
	for (;;) {
		/* Figure out how much we need to read:
		 * Try to read the remaining amount.
		 * But don't read more than the buffer size.
		 * And don't try to read past the end of the file.
		 * Finally, if we're not at a page boundary, don't read past
		 *	the next page.
		 * If this means reading 0 then we were asked to read past
		 *	the end of file. */
		amount = min((unsigned int) amount_left, FSG_BUFLEN);

		/* Wait for the next buffer to become available */
		bh = fsg->common->next_buffhd_to_fill;
		while (bh->state != BUF_STATE_EMPTY) {
			rc = sleep_thread(fsg->common, true);
			if (rc)
				return rc;
		}

		/* If we were asked to read past the end of file,
		 * end with an empty buffer. */
		if (amount == 0) {
			bh->inreq->length = 0;
			bh->state = BUF_STATE_FULL;
			break;
		}

		/* Perform the read */
		pr_info("Copied to %p, %d bytes started from %d\n",
				bh->buf, amount, size - amount_left);
		/* from upt buffer to file_storeage buffer */
		memcpy(bh->buf, data + size - amount_left, amount);
		amount_left  -= amount;
		fsg->common->residue -= amount;

		bh->inreq->length = amount;
		bh->state = BUF_STATE_FULL;

		/* Send this buffer and go read some more */
		bh->inreq->zero = 0;

		/* USB Physical transfer: Data from device to host */
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
				&bh->inreq_busy, &bh->state);

		fsg->common->next_buffhd_to_fill = bh->next;

		if (amount_left <= 0)
			break;
	}

	return size - amount_left;
}

static int utp_do_write(struct fsg_dev *fsg, void *data, size_t size)
{
	struct fsg_buffhd	*bh;
	int			get_some_more;
	u32			amount_left_to_req, amount_left_to_write;
	unsigned int		amount;
	int			rc;
	loff_t			offset;

	/* Carry out the file writes */
	get_some_more = 1;
	amount_left_to_req = amount_left_to_write = size;

	if (unlikely(amount_left_to_write == 0))
		return -EIO;

	offset = 0;
	while (amount_left_to_write > 0) {

		/* Queue a request for more data from the host */
		bh = fsg->common->next_buffhd_to_fill;
		if (bh->state == BUF_STATE_EMPTY && get_some_more) {

			/* Figure out how much we want to get:
			 * Try to get the remaining amount.
			 * But don't get more than the buffer size.
			 * And don't try to go past the end of the file.
			 * If we're not at a page boundary,
			 *	don't go past the next page.
			 * If this means getting 0, then we were asked
			 *	to write past the end of file.
			 * Finally, round down to a block boundary. */
			amount = min(amount_left_to_req, FSG_BUFLEN);

			if (amount == 0) {
				get_some_more = 0;
				/* cry now */
				continue;
			}

			/* Get the next buffer */
			amount_left_to_req -= amount;
			if (amount_left_to_req == 0)
				get_some_more = 0;

			/* amount is always divisible by 512, hence by
			 * the bulk-out maxpacket size */
			bh->outreq->length = bh->bulk_out_intended_length =
					amount;
			bh->outreq->short_not_ok = 1;
			start_transfer(fsg, fsg->bulk_out, bh->outreq,
					&bh->outreq_busy, &bh->state);
			fsg->common->next_buffhd_to_fill = bh->next;
			continue;
		}

		/* Write the received data to the backing file */
		bh = fsg->common->next_buffhd_to_drain;
		if (bh->state == BUF_STATE_EMPTY && !get_some_more)
			break;			/* We stopped early */
		if (bh->state == BUF_STATE_FULL) {
			smp_rmb();
			fsg->common->next_buffhd_to_drain = bh->next;
			bh->state = BUF_STATE_EMPTY;

			/* Did something go wrong with the transfer? */
			if (bh->outreq->status != 0)
				/* cry again, COMMUNICATION_FAILURE */
				break;

			amount = bh->outreq->actual;

			/* Perform the write */
			memcpy(data + offset, bh->buf, amount);

			offset += amount;
			if (signal_pending(current))
				return -EINTR;		/* Interrupted!*/
			amount_left_to_write -= amount;
			fsg->common->residue -= amount;

			/* Did the host decide to stop early? */
			if (bh->outreq->actual != bh->outreq->length) {
				fsg->common->short_packet_received = 1;
				break;
			}
			continue;
		}

		/* Wait for something to happen */
		rc = sleep_thread(fsg->common, true);
		if (rc)
			return rc;
	}

	return -EIO;
}

static inline void utp_set_sense(struct fsg_dev *fsg, u16 code, u64 reply)
{
	UTP_CTX(fsg)->processed = true;
	UTP_CTX(fsg)->sdinfo = reply & 0xFFFFFFFF;
	UTP_CTX(fsg)->sdinfo_h = (reply >> 32) & 0xFFFFFFFF;
	UTP_CTX(fsg)->sd = (UTP_SENSE_KEY << 16) | code;
}

static void utp_poll(struct fsg_dev *fsg)
{
	struct utp_context *ctx = UTP_CTX(fsg);
	struct utp_user_data *uud = NULL;

	mutex_lock(&ctx->lock);
	if (!list_empty(&ctx->write))
		uud = list_first_entry(&ctx->write, struct utp_user_data, link);
	mutex_unlock(&ctx->lock);

	if (uud) {
		if (uud->data.flags & UTP_FLAG_STATUS) {
			printk(KERN_WARNING "%s: exit with status %d\n",
					__func__, uud->data.status);
			UTP_SS_EXIT(fsg, uud->data.status);
		} else if (uud->data.flags & UTP_FLAG_REPORT_BUSY) {
			UTP_SS_BUSY(fsg, --ctx->counter);
		} else {
			printk("%s: pass returned.\n", __func__);
			UTP_SS_PASS(fsg);
		}
		utp_user_data_free(uud);
	} else {
		if (utp_context.cur_state & UTP_FLAG_DATA) {
			if (count_list(&ctx->read) < 7) {
				pr_debug("%s: pass returned in POLL stage. \n", __func__);
				UTP_SS_PASS(fsg);
				utp_context.cur_state = 0;
				return;
			}
		}
		UTP_SS_BUSY(fsg, --ctx->counter);
	}
}

static int utp_exec(struct fsg_dev *fsg,
		    char *command,
		    int cmdsize,
		    unsigned long long payload)
{
	struct utp_user_data *uud = NULL, *uud2r;
	struct utp_context *ctx = UTP_CTX(fsg);

	ctx->counter = 0xFFFF;
	uud2r = utp_user_data_alloc(cmdsize + 1);
	if (!uud2r)
		return -ENOMEM;
	uud2r->data.flags = UTP_FLAG_COMMAND;
	uud2r->data.payload = payload;
	strncpy(uud2r->data.command, command, cmdsize);

	mutex_lock(&ctx->lock);
	list_add_tail(&uud2r->link, &ctx->read);
	mutex_unlock(&ctx->lock);
	/* wake up the read routine */
	wake_up(&ctx->wq);

	if (command[0] == '!')	/* there will be no response */
		return 0;

	/*
	 * the user program (uuc) will return utp_message
	 * and add list to write list
	 */
	WAIT_ACTIVITY(write);

	mutex_lock(&ctx->lock);
	if (!list_empty(&ctx->write)) {
		uud = list_first_entry(&ctx->write, struct utp_user_data, link);
#ifdef DEBUG
		pr_info("UUD:\n\tFlags = %02X\n", uud->data.flags);
		if (uud->data.flags & UTP_FLAG_DATA) {
			pr_info("\tbufsize = %d\n", uud->data.bufsize);
			print_hex_dump(KERN_DEBUG, "\t", DUMP_PREFIX_NONE,
				16, 2, uud->data.data, uud->data.bufsize, true);
		}
		if (uud->data.flags & UTP_FLAG_REPORT_BUSY)
			pr_info("\tBUSY\n");
#endif
	}
	mutex_unlock(&ctx->lock);

	if (uud->data.flags & UTP_FLAG_DATA) {
		memcpy(ctx->buffer, uud->data.data, uud->data.bufsize);
		UTP_SS_SIZE(fsg, uud->data.bufsize);
	} else if (uud->data.flags & UTP_FLAG_REPORT_BUSY) {
		UTP_SS_BUSY(fsg, ctx->counter);
	} else if (uud->data.flags & UTP_FLAG_STATUS) {
		printk(KERN_WARNING "%s: exit with status %d\n", __func__,
				uud->data.status);
		UTP_SS_EXIT(fsg, uud->data.status);
	} else {
		pr_debug("%s: pass returned in EXEC stage. \n", __func__);
		UTP_SS_PASS(fsg);
	}
	utp_user_data_free(uud);
	return 0;
}

static int utp_send_status(struct fsg_dev *fsg)
{
	struct fsg_buffhd	*bh;
	u8			status = US_BULK_STAT_OK;
	struct bulk_cs_wrap	*csw;
	int			rc;

	/* Wait for the next buffer to become available */
	bh = fsg->common->next_buffhd_to_fill;
	while (bh->state != BUF_STATE_EMPTY) {
		rc = sleep_thread(fsg->common, true);
		if (rc)
			return rc;
	}

	if (fsg->common->phase_error) {
		DBG(fsg, "sending phase-error status\n");
		status = US_BULK_STAT_PHASE;

	} else if ((UTP_CTX(fsg)->sd & 0xFFFF) != UTP_REPLY_PASS) {
		status = US_BULK_STAT_FAIL;
	}

	csw = bh->buf;

	/* Store and send the Bulk-only CSW */
	csw->Signature = __constant_cpu_to_le32(US_BULK_CS_SIGN);
	csw->Tag = fsg->common->tag;
	csw->Residue = cpu_to_le32(fsg->common->residue);
	csw->Status = status;

	bh->inreq->length = US_BULK_CS_WRAP_LEN;
	bh->inreq->zero = 0;
	start_transfer(fsg, fsg->bulk_in, bh->inreq,
			&bh->inreq_busy, &bh->state);
	fsg->common->next_buffhd_to_fill = bh->next;
	return 0;
}

static int utp_handle_message(struct fsg_dev *fsg,
			      char *cdb_data,
			      int default_reply)
{
	struct utp_msg *m = (struct utp_msg *)cdb_data;
	void *data = NULL;
	int r;
	struct utp_user_data *uud2r;
	unsigned long long param;
	unsigned long tag;

	if (m->f0 != 0xF0)
		return default_reply;

	tag = get_unaligned_be32((void *)&m->utp_msg_tag);
	param = get_be64((void *)&m->param);
	pr_debug("Type 0x%x, tag 0x%08lx, param %llx\n",
			m->utp_msg_type, tag, param);

	switch ((enum utp_msg_type)m->utp_msg_type) {

	case UTP_POLL:
		if (get_be64((void *)&m->param) == 1) {
			pr_debug("%s: version request\n", __func__);
			UTP_SS_EXIT(fsg, UTP_CTX(fsg)->utp_version);
			break;
		}
		utp_poll(fsg);
		break;
	case UTP_EXEC:
		pr_debug("%s: EXEC\n", __func__);
		data = vmalloc(fsg->common->data_size);
		memset(data, 0, fsg->common->data_size);
		/* copy data from usb buffer to utp buffer */
		utp_do_write(fsg, data, fsg->common->data_size);
		utp_exec(fsg, data, fsg->common->data_size, param);
		vfree(data);
		break;
	case UTP_GET: /* data from device to host */
		pr_debug("%s: GET, %d bytes\n", __func__,
					fsg->common->data_size);
		r = utp_do_read(fsg, UTP_CTX(fsg)->buffer,
					fsg->common->data_size);
		UTP_SS_PASS(fsg);
		break;
	case UTP_PUT:
		utp_context.cur_state =  UTP_FLAG_DATA;
		pr_debug("%s: PUT, Received %d bytes\n", __func__, fsg->common->data_size);/* data from host to device */
		uud2r = utp_user_data_alloc(fsg->common->data_size);
		if (!uud2r)
			return -ENOMEM;
		uud2r->data.bufsize = fsg->common->data_size;
		uud2r->data.flags = UTP_FLAG_DATA;
		utp_do_write(fsg, uud2r->data.data, fsg->common->data_size);
		/* don't know what will be written */
		mutex_lock(&UTP_CTX(fsg)->lock);
		list_add_tail(&uud2r->link, &UTP_CTX(fsg)->read);
		mutex_unlock(&UTP_CTX(fsg)->lock);
		wake_up(&UTP_CTX(fsg)->wq);
		/*
		 * Return PASS or FAIL according to uuc's status
		 * Please open it if need to check uuc's status
		 * and use another version uuc
		 */
#if 0
		struct utp_user_data *uud = NULL;
		struct utp_context *ctx;
		WAIT_ACTIVITY(write);
		ctx = UTP_CTX(fsg);
		mutex_lock(&ctx->lock);

		if (!list_empty(&ctx->write))
			uud = list_first_entry(&ctx->write,
					struct utp_user_data, link);

		mutex_unlock(&ctx->lock);
		if (uud) {
			if (uud->data.flags & UTP_FLAG_STATUS) {
				printk(KERN_WARNING "%s: exit with status %d\n",
					 __func__, uud->data.status);
				UTP_SS_EXIT(fsg, uud->data.status);
			} else {
				pr_debug("%s: pass\n", __func__);
				UTP_SS_PASS(fsg);
			}
			utp_user_data_free(uud);
		} else{
			UTP_SS_PASS(fsg);
		}
#endif
		if (count_list(&UTP_CTX(fsg)->read) < 7) {
			utp_context.cur_state = 0;
			UTP_SS_PASS(fsg);
		} else
			UTP_SS_BUSY(fsg, UTP_CTX(fsg)->counter);

		break;
	}

	utp_send_status(fsg);
	return -1;
}
