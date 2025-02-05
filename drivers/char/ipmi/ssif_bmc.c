// SPDX-License-Identifier: GPL-2.0-only
/*
 * The driver for BMC side of SSIF interface
 *
 * Copyright (c) 2022, Ampere Computing LLC
 *
 */

#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/ipmi_ssif_bmc.h>

#define DEVICE_NAME                             "ipmi-ssif-host"

#define GET_8BIT_ADDR(addr_7bit)                (((addr_7bit) << 1) & 0xff)

/* A standard SMBus Transaction is limited to 32 data bytes */
#define MAX_PAYLOAD_PER_TRANSACTION             32
/* Transaction includes the address, the command, the length and the PEC byte */
#define MAX_TRANSACTION                         (MAX_PAYLOAD_PER_TRANSACTION + 4)

#define MAX_IPMI_DATA_PER_START_TRANSACTION     30
#define MAX_IPMI_DATA_PER_MIDDLE_TRANSACTION    31

#define SSIF_IPMI_SINGLEPART_WRITE              0x2
#define SSIF_IPMI_SINGLEPART_READ               0x3
#define SSIF_IPMI_MULTIPART_WRITE_START         0x6
#define SSIF_IPMI_MULTIPART_WRITE_MIDDLE        0x7
#define SSIF_IPMI_MULTIPART_WRITE_END           0x8
#define SSIF_IPMI_MULTIPART_READ_START          0x3
#define SSIF_IPMI_MULTIPART_READ_MIDDLE         0x9

/*
 * IPMI 2.0 Spec, section 12.7 SSIF Timing,
 * Request-to-Response Time is T6max(250ms) - T1max(20ms) - 3ms = 227ms
 * Recover ssif_bmc from busy state if it takes up to 500ms
 */
#define RESPONSE_TIMEOUT                        500 /* ms */

struct ssif_part_buffer {
	u8 address;
	u8 smbus_cmd;
	u8 length;
	u8 payload[MAX_PAYLOAD_PER_TRANSACTION];
	u8 pec;
	u8 index;
};

/*
 * SSIF internal states:
 *   SSIF_READY         0x00 : Ready state
 *   SSIF_START         0x01 : Start smbus transaction
 *   SSIF_SMBUS_CMD     0x02 : Received SMBus command
 *   SSIF_REQ_RECVING   0x03 : Receiving request
 *   SSIF_RES_SENDING   0x04 : Sending response
 *   SSIF_ABORTING      0x05 : Aborting state
 */
enum ssif_state {
	SSIF_READY,
	SSIF_START,
	SSIF_SMBUS_CMD,
	SSIF_REQ_RECVING,
	SSIF_RES_SENDING,
	SSIF_ABORTING,
	SSIF_STATE_MAX
};

struct ssif_bmc_ctx {
	struct i2c_client       *client;
	struct miscdevice       miscdev;
	int                     msg_idx;
	bool                    pec_support;
	/* ssif bmc spinlock */
	spinlock_t              lock;
	wait_queue_head_t       wait_queue;
	u8                      running;
	enum ssif_state         state;
	/* Timeout waiting for response */
	struct timer_list       response_timer;
	bool                    response_timer_inited;
	/* Flag to identify a Multi-part Read Transaction */
	bool                    is_singlepart_read;
	u8                      nbytes_processed;
	u8                      remain_len;
	u8                      recv_len;
	/* Block Number of a Multi-part Read Transaction */
	u8                      block_num;
	bool                    request_available;
	bool                    response_in_progress;
	bool                    busy;
	bool                    aborting;
	/* Buffer for SSIF Transaction part*/
	struct ssif_part_buffer part_buf;
	struct ipmi_ssif_msg    response;
	struct ipmi_ssif_msg    request;
};

static inline struct ssif_bmc_ctx *to_ssif_bmc(struct file *file)
{
	return container_of(file->private_data, struct ssif_bmc_ctx, miscdev);
}

static const char *state_to_string(enum ssif_state state)
{
	switch (state) {
	case SSIF_READY:
		return "SSIF_READY";
	case SSIF_START:
		return "SSIF_START";
	case SSIF_SMBUS_CMD:
		return "SSIF_SMBUS_CMD";
	case SSIF_REQ_RECVING:
		return "SSIF_REQ_RECVING";
	case SSIF_RES_SENDING:
		return "SSIF_RES_SENDING";
	case SSIF_ABORTING:
		return "SSIF_ABORTING";
	default:
		return "SSIF_STATE_UNKNOWN";
	}
}

/* Handle SSIF message that will be sent to user */
static ssize_t ssif_bmc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct ssif_bmc_ctx *ssif_bmc = to_ssif_bmc(file);
	struct ipmi_ssif_msg msg;
	unsigned long flags;
	ssize_t ret;

	spin_lock_irqsave(&ssif_bmc->lock, flags);
	while (!ssif_bmc->request_available) {
		spin_unlock_irqrestore(&ssif_bmc->lock, flags);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(ssif_bmc->wait_queue,
					       ssif_bmc->request_available);
		if (ret)
			return ret;
		spin_lock_irqsave(&ssif_bmc->lock, flags);
	}

	if (count < min_t(ssize_t,
			  sizeof_field(struct ipmi_ssif_msg, len) + ssif_bmc->request.len,
			  sizeof(struct ipmi_ssif_msg))) {
		spin_unlock_irqrestore(&ssif_bmc->lock, flags);
		ret = -EINVAL;
	} else {
		count = min_t(ssize_t,
			      sizeof_field(struct ipmi_ssif_msg, len) + ssif_bmc->request.len,
			      sizeof(struct ipmi_ssif_msg));
		memcpy(&msg, &ssif_bmc->request, count);
		ssif_bmc->request_available = false;
		spin_unlock_irqrestore(&ssif_bmc->lock, flags);

		ret = copy_to_user(buf, &msg, count);
	}

	return (ret < 0) ? ret : count;
}

/* Handle SSIF message that is written by user */
static ssize_t ssif_bmc_write(struct file *file, const char __user *buf, size_t count,
			      loff_t *ppos)
{
	struct ssif_bmc_ctx *ssif_bmc = to_ssif_bmc(file);
	struct ipmi_ssif_msg msg;
	unsigned long flags;
	ssize_t ret;

	if (count < sizeof(msg.len) ||
	    count > sizeof(struct ipmi_ssif_msg))
		return -EINVAL;

	if (copy_from_user(&msg, buf, count))
		return -EFAULT;

	if (!msg.len || msg.len > IPMI_SSIF_PAYLOAD_MAX ||
	    count < sizeof_field(struct ipmi_ssif_msg, len) + msg.len)
		return -EINVAL;

	spin_lock_irqsave(&ssif_bmc->lock, flags);
	while (ssif_bmc->response_in_progress) {
		spin_unlock_irqrestore(&ssif_bmc->lock, flags);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(ssif_bmc->wait_queue,
					       !ssif_bmc->response_in_progress);
		if (ret)
			return ret;
		spin_lock_irqsave(&ssif_bmc->lock, flags);
	}

	/*
	 * The write must complete before the response timeout fired, otherwise
	 * the response is aborted and wait for next request
	 * Return -EINVAL if the response is aborted
	 */
	ret = (ssif_bmc->response_timer_inited) ? 0 : -EINVAL;
	if (ret)
		goto exit;

	del_timer(&ssif_bmc->response_timer);
	ssif_bmc->response_timer_inited = false;

	memcpy(&ssif_bmc->response, &msg, count);
	ssif_bmc->is_singlepart_read = (msg.len <= MAX_PAYLOAD_PER_TRANSACTION);

	ssif_bmc->response_in_progress = true;

	/* ssif_bmc not busy */
	ssif_bmc->busy = false;

	/* Clean old request buffer */
	memset(&ssif_bmc->request, 0, sizeof(struct ipmi_ssif_msg));
exit:
	spin_unlock_irqrestore(&ssif_bmc->lock, flags);

	return (ret < 0) ? ret : count;
}

static int ssif_bmc_open(struct inode *inode, struct file *file)
{
	struct ssif_bmc_ctx *ssif_bmc = to_ssif_bmc(file);
	int ret = 0;

	spin_lock_irq(&ssif_bmc->lock);
	if (!ssif_bmc->running)
		ssif_bmc->running = 1;
	else
		ret = -EBUSY;
	spin_unlock_irq(&ssif_bmc->lock);

	return ret;
}

static __poll_t ssif_bmc_poll(struct file *file, poll_table *wait)
{
	struct ssif_bmc_ctx *ssif_bmc = to_ssif_bmc(file);
	__poll_t mask = 0;

	poll_wait(file, &ssif_bmc->wait_queue, wait);

	spin_lock_irq(&ssif_bmc->lock);
	/* The request is available, userspace application can get the request */
	if (ssif_bmc->request_available)
		mask |= EPOLLIN;

	spin_unlock_irq(&ssif_bmc->lock);

	return mask;
}

static int ssif_bmc_release(struct inode *inode, struct file *file)
{
	struct ssif_bmc_ctx *ssif_bmc = to_ssif_bmc(file);

	spin_lock_irq(&ssif_bmc->lock);
	ssif_bmc->running = 0;
	spin_unlock_irq(&ssif_bmc->lock);

	return 0;
}

/*
 * System calls to device interface for user apps
 */
static const struct file_operations ssif_bmc_fops = {
	.owner		= THIS_MODULE,
	.open		= ssif_bmc_open,
	.read		= ssif_bmc_read,
	.write		= ssif_bmc_write,
	.release	= ssif_bmc_release,
	.poll		= ssif_bmc_poll,
};

/* Called with ssif_bmc->lock held. */
static void complete_response(struct ssif_bmc_ctx *ssif_bmc)
{
	/* Invalidate response in buffer to denote it having been sent. */
	ssif_bmc->response.len = 0;
	ssif_bmc->response_in_progress = false;
	ssif_bmc->nbytes_processed = 0;
	ssif_bmc->remain_len = 0;
	ssif_bmc->busy = false;
	wake_up_all(&ssif_bmc->wait_queue);
}

static void response_timeout(struct timer_list *t)
{
	struct ssif_bmc_ctx *ssif_bmc = from_timer(ssif_bmc, t, response_timer);
	unsigned long flags;

	spin_lock_irqsave(&ssif_bmc->lock, flags);

	/* Do nothing if the response is in progress */
	if (!ssif_bmc->response_in_progress) {
		/* Recover ssif_bmc from busy */
		ssif_bmc->busy = false;
		ssif_bmc->response_timer_inited = false;
		/* Set aborting flag */
		ssif_bmc->aborting = true;
	}

	spin_unlock_irqrestore(&ssif_bmc->lock, flags);
}

/* Called with ssif_bmc->lock held. */
static void handle_request(struct ssif_bmc_ctx *ssif_bmc)
{
	/* set ssif_bmc to busy waiting for response */
	ssif_bmc->busy = true;
	/* Request message is available to process */
	ssif_bmc->request_available = true;
	/* Clean old response buffer */
	memset(&ssif_bmc->response, 0, sizeof(struct ipmi_ssif_msg));
	/* This is the new READ request.*/
	wake_up_all(&ssif_bmc->wait_queue);

	/* Armed timer to recover slave from busy state in case of no response */
	if (!ssif_bmc->response_timer_inited) {
		timer_setup(&ssif_bmc->response_timer, response_timeout, 0);
		ssif_bmc->response_timer_inited = true;
	}
	mod_timer(&ssif_bmc->response_timer, jiffies + msecs_to_jiffies(RESPONSE_TIMEOUT));
}

static void calculate_response_part_pec(struct ssif_part_buffer *part)
{
	u8 addr = part->address;

	/* PEC - Start Read Address */
	part->pec = i2c_smbus_pec(0, &addr, 1);
	/* PEC - SSIF Command */
	part->pec = i2c_smbus_pec(part->pec, &part->smbus_cmd, 1);
	/* PEC - Restart Write Address */
	addr = addr | 0x01;
	part->pec = i2c_smbus_pec(part->pec, &addr, 1);
	part->pec = i2c_smbus_pec(part->pec, &part->length, 1);
	if (part->length)
		part->pec = i2c_smbus_pec(part->pec, part->payload, part->length);
}

static void set_singlepart_response_buffer(struct ssif_bmc_ctx *ssif_bmc)
{
	struct ssif_part_buffer *part = &ssif_bmc->part_buf;

	part->address = GET_8BIT_ADDR(ssif_bmc->client->addr);
	part->length = (u8)ssif_bmc->response.len;

	/* Clear the rest to 0 */
	memset(part->payload + part->length, 0, MAX_PAYLOAD_PER_TRANSACTION - part->length);
	memcpy(&part->payload[0], &ssif_bmc->response.payload[0], part->length);
}

static void set_multipart_response_buffer(struct ssif_bmc_ctx *ssif_bmc)
{
	struct ssif_part_buffer *part = &ssif_bmc->part_buf;
	u8 part_len = 0;

	part->address = GET_8BIT_ADDR(ssif_bmc->client->addr);
	switch (part->smbus_cmd) {
	case SSIF_IPMI_MULTIPART_READ_START:
		/*
		 * Read Start length is 32 bytes.
		 * Read Start transfer first 30 bytes of IPMI response
		 * and 2 special code 0x00, 0x01.
		 */
		ssif_bmc->nbytes_processed = 0;
		ssif_bmc->block_num = 0;
		part->length = MAX_PAYLOAD_PER_TRANSACTION;
		part_len = MAX_IPMI_DATA_PER_START_TRANSACTION;
		ssif_bmc->remain_len = ssif_bmc->response.len - part_len;

		part->payload[0] = 0x00; /* Start Flag */
		part->payload[1] = 0x01; /* Start Flag */

		memcpy(&part->payload[2], &ssif_bmc->response.payload[0], part_len);
		break;

	case SSIF_IPMI_MULTIPART_READ_MIDDLE:
		/*
		 * IPMI READ Middle or READ End messages can carry up to 31 bytes
		 * IPMI data plus block number byte.
		 */
		if (ssif_bmc->remain_len <= MAX_IPMI_DATA_PER_MIDDLE_TRANSACTION) {
			/*
			 * This is READ End message
			 *  Return length is the remaining response data length
			 *  plus block number
			 *  Block number 0xFF is to indicate this is last message
			 *
			 */
			/* Clean the buffer */
			memset(&part->payload[0], 0, MAX_PAYLOAD_PER_TRANSACTION);
			part->length = ssif_bmc->remain_len + 1;
			part_len = ssif_bmc->remain_len;
			ssif_bmc->block_num = 0xFF;
			part->payload[0] = ssif_bmc->block_num;
		} else {
			/*
			 * This is READ Middle message
			 *  Response length is the maximum SMBUS transfer length
			 *  Block number byte is incremented
			 * Return length is maximum SMBUS transfer length
			 */
			part->length = MAX_PAYLOAD_PER_TRANSACTION;
			part_len = MAX_IPMI_DATA_PER_MIDDLE_TRANSACTION;
			part->payload[0] = ssif_bmc->block_num;
			ssif_bmc->block_num++;
		}

		ssif_bmc->remain_len -= part_len;
		memcpy(&part->payload[1], ssif_bmc->response.payload + ssif_bmc->nbytes_processed,
		       part_len);
		break;

	default:
		/* Do not expect to go to this case */
		dev_err(&ssif_bmc->client->dev, "%s: Unexpected SMBus command 0x%x\n",
			__func__, part->smbus_cmd);
		break;
	}

	ssif_bmc->nbytes_processed += part_len;
}

static bool supported_read_cmd(u8 cmd)
{
	if (cmd == SSIF_IPMI_SINGLEPART_READ ||
	    cmd == SSIF_IPMI_MULTIPART_READ_START ||
	    cmd == SSIF_IPMI_MULTIPART_READ_MIDDLE)
		return true;

	return false;
}

static bool supported_write_cmd(u8 cmd)
{
	if (cmd == SSIF_IPMI_SINGLEPART_WRITE ||
	    cmd == SSIF_IPMI_MULTIPART_WRITE_START ||
	    cmd == SSIF_IPMI_MULTIPART_WRITE_MIDDLE ||
	    cmd == SSIF_IPMI_MULTIPART_WRITE_END)
		return true;

	return false;
}

/* Process the IPMI response that will be read by master */
static void handle_read_processed(struct ssif_bmc_ctx *ssif_bmc, u8 *val)
{
	struct ssif_part_buffer *part = &ssif_bmc->part_buf;

	/* msg_idx start from 0 */
	if (part->index < part->length)
		*val = part->payload[part->index];
	else if (part->index == part->length && ssif_bmc->pec_support)
		*val = part->pec;
	else
		*val = 0;

	part->index++;
}

static void handle_write_received(struct ssif_bmc_ctx *ssif_bmc, u8 *val)
{
	/*
	 * The msg_idx must be 1 when first enter SSIF_REQ_RECVING state
	 * And it would never exceeded 36 bytes included the 32 bytes max payload +
	 * the address + the command + the len and the PEC.
	 */
	if (ssif_bmc->msg_idx < 1  || ssif_bmc->msg_idx > MAX_TRANSACTION)
		return;

	if (ssif_bmc->msg_idx == 1) {
		ssif_bmc->part_buf.length = *val;
		ssif_bmc->part_buf.index = 0;
	} else {
		ssif_bmc->part_buf.payload[ssif_bmc->part_buf.index++] = *val;
	}

	ssif_bmc->msg_idx++;
}

static bool validate_request_part(struct ssif_bmc_ctx *ssif_bmc)
{
	struct ssif_part_buffer *part = &ssif_bmc->part_buf;
	bool ret = true;
	u8 cpec;
	u8 addr;

	if (part->index == part->length) {
		/* PEC is not included */
		ssif_bmc->pec_support = false;
		ret = true;
		goto exit;
	}

	if (part->index != part->length + 1) {
		ret = false;
		goto exit;
	}

	/* PEC is included */
	ssif_bmc->pec_support = true;
	part->pec = part->payload[part->length];
	addr = GET_8BIT_ADDR(ssif_bmc->client->addr);
	cpec = i2c_smbus_pec(0, &addr, 1);
	cpec = i2c_smbus_pec(cpec, &part->smbus_cmd, 1);
	cpec = i2c_smbus_pec(cpec, &part->length, 1);
	/*
	 * As SMBus specification does not allow the length
	 * (byte count) in the Write-Block protocol to be zero.
	 * Therefore, it is illegal to have the last Middle
	 * transaction in the sequence carry 32-byte and have
	 * a length of ‘0’ in the End transaction.
	 * But some users may try to use this way and we should
	 * prevent ssif_bmc driver broken in this case.
	 */
	if (part->length)
		cpec = i2c_smbus_pec(cpec, part->payload, part->length);

	if (cpec != part->pec)
		ret = false;

exit:
	return ret;
}

static void process_request_part(struct ssif_bmc_ctx *ssif_bmc)
{
	struct ssif_part_buffer *part = &ssif_bmc->part_buf;
	unsigned int len;

	switch (part->smbus_cmd) {
	case SSIF_IPMI_SINGLEPART_WRITE:
		/* save the whole part to request*/
		ssif_bmc->request.len = part->length;
		memcpy(ssif_bmc->request.payload, part->payload, part->length);

		break;
	case SSIF_IPMI_MULTIPART_WRITE_START:
		ssif_bmc->request.len = 0;

		fallthrough;
	case SSIF_IPMI_MULTIPART_WRITE_MIDDLE:
	case SSIF_IPMI_MULTIPART_WRITE_END:
		len = ssif_bmc->request.len + part->length;
		/* Do the bound check here, not allow the request len exceed 254 bytes */
		if (len > IPMI_SSIF_PAYLOAD_MAX) {
			dev_warn(&ssif_bmc->client->dev,
				 "Warn: Request exceeded 254 bytes, aborting");
			/* Request too long, aborting */
			ssif_bmc->aborting =  true;
		} else {
			memcpy(ssif_bmc->request.payload + ssif_bmc->request.len,
			       part->payload, part->length);
			ssif_bmc->request.len += part->length;
		}
		break;
	default:
		/* Do not expect to go to this case */
		dev_err(&ssif_bmc->client->dev, "%s: Unexpected SMBus command 0x%x\n",
			__func__, part->smbus_cmd);
		break;
	}
}

static void process_smbus_cmd(struct ssif_bmc_ctx *ssif_bmc, u8 *val)
{
	/* SMBUS command can vary (single or multi-part) */
	ssif_bmc->part_buf.smbus_cmd = *val;
	ssif_bmc->msg_idx = 1;
	memset(&ssif_bmc->part_buf.payload[0], 0, MAX_PAYLOAD_PER_TRANSACTION);

	if (*val == SSIF_IPMI_SINGLEPART_WRITE || *val == SSIF_IPMI_MULTIPART_WRITE_START) {
		/*
		 * The response maybe not come in-time, causing host SSIF driver
		 * to timeout and resend a new request. In such case check for
		 * pending response and clear it
		 */
		if (ssif_bmc->response_in_progress)
			complete_response(ssif_bmc);

		/* This is new request, flip aborting flag if set */
		if (ssif_bmc->aborting)
			ssif_bmc->aborting = false;
	}
}

static void on_read_requested_event(struct ssif_bmc_ctx *ssif_bmc, u8 *val)
{
	if (ssif_bmc->state == SSIF_READY ||
	    ssif_bmc->state == SSIF_START ||
	    ssif_bmc->state == SSIF_REQ_RECVING ||
	    ssif_bmc->state == SSIF_RES_SENDING) {
		dev_warn(&ssif_bmc->client->dev,
			 "Warn: %s unexpected READ REQUESTED in state=%s\n",
			 __func__, state_to_string(ssif_bmc->state));
		ssif_bmc->state = SSIF_ABORTING;
		*val = 0;
		return;

	} else if (ssif_bmc->state == SSIF_SMBUS_CMD) {
		if (!supported_read_cmd(ssif_bmc->part_buf.smbus_cmd)) {
			dev_warn(&ssif_bmc->client->dev, "Warn: Unknown SMBus read command=0x%x",
				 ssif_bmc->part_buf.smbus_cmd);
			ssif_bmc->aborting = true;
		}

		if (ssif_bmc->aborting)
			ssif_bmc->state = SSIF_ABORTING;
		else
			ssif_bmc->state = SSIF_RES_SENDING;
	}

	ssif_bmc->msg_idx = 0;

	/* Send 0 if there is nothing to send */
	if (!ssif_bmc->response_in_progress || ssif_bmc->state == SSIF_ABORTING) {
		*val = 0;
		return;
	}

	if (ssif_bmc->is_singlepart_read)
		set_singlepart_response_buffer(ssif_bmc);
	else
		set_multipart_response_buffer(ssif_bmc);

	calculate_response_part_pec(&ssif_bmc->part_buf);
	ssif_bmc->part_buf.index = 0;
	*val = ssif_bmc->part_buf.length;
}

static void on_read_processed_event(struct ssif_bmc_ctx *ssif_bmc, u8 *val)
{
	if (ssif_bmc->state == SSIF_READY ||
	    ssif_bmc->state == SSIF_START ||
	    ssif_bmc->state == SSIF_REQ_RECVING ||
	    ssif_bmc->state == SSIF_SMBUS_CMD) {
		dev_warn(&ssif_bmc->client->dev,
			 "Warn: %s unexpected READ PROCESSED in state=%s\n",
			 __func__, state_to_string(ssif_bmc->state));
		ssif_bmc->state = SSIF_ABORTING;
		*val = 0;
		return;
	}

	/* Send 0 if there is nothing to send */
	if (!ssif_bmc->response_in_progress || ssif_bmc->state == SSIF_ABORTING) {
		*val = 0;
		return;
	}

	handle_read_processed(ssif_bmc, val);
}

static void on_write_requested_event(struct ssif_bmc_ctx *ssif_bmc, u8 *val)
{
	if (ssif_bmc->state == SSIF_READY || ssif_bmc->state == SSIF_SMBUS_CMD) {
		ssif_bmc->state = SSIF_START;

	} else if (ssif_bmc->state == SSIF_START ||
		   ssif_bmc->state == SSIF_REQ_RECVING ||
		   ssif_bmc->state == SSIF_RES_SENDING) {
		dev_warn(&ssif_bmc->client->dev,
			 "Warn: %s unexpected WRITE REQUEST in state=%s\n",
			 __func__, state_to_string(ssif_bmc->state));
		ssif_bmc->state = SSIF_ABORTING;
		return;
	}

	ssif_bmc->msg_idx = 0;
	ssif_bmc->part_buf.address = *val;
}

static void on_write_received_event(struct ssif_bmc_ctx *ssif_bmc, u8 *val)
{
	if (ssif_bmc->state == SSIF_READY ||
	    ssif_bmc->state == SSIF_RES_SENDING) {
		dev_warn(&ssif_bmc->client->dev,
			 "Warn: %s unexpected WRITE RECEIVED in state=%s\n",
			 __func__, state_to_string(ssif_bmc->state));
		ssif_bmc->state = SSIF_ABORTING;

	} else if (ssif_bmc->state == SSIF_START) {
		ssif_bmc->state = SSIF_SMBUS_CMD;

	} else if (ssif_bmc->state == SSIF_SMBUS_CMD) {
		if (!supported_write_cmd(ssif_bmc->part_buf.smbus_cmd)) {
			dev_warn(&ssif_bmc->client->dev, "Warn: Unknown SMBus write command=0x%x",
				 ssif_bmc->part_buf.smbus_cmd);
			ssif_bmc->aborting = true;
		}

		if (ssif_bmc->aborting)
			ssif_bmc->state = SSIF_ABORTING;
		else
			ssif_bmc->state = SSIF_REQ_RECVING;
	}

	/* This is response sending state */
	if (ssif_bmc->state == SSIF_REQ_RECVING)
		handle_write_received(ssif_bmc, val);
	else if (ssif_bmc->state == SSIF_SMBUS_CMD)
		process_smbus_cmd(ssif_bmc, val);
}

static void on_stop_event(struct ssif_bmc_ctx *ssif_bmc, u8 *val)
{
	if (ssif_bmc->state == SSIF_READY ||
	    ssif_bmc->state == SSIF_START ||
	    ssif_bmc->state == SSIF_SMBUS_CMD ||
	    ssif_bmc->state == SSIF_ABORTING) {
		dev_warn(&ssif_bmc->client->dev,
			 "Warn: %s unexpected SLAVE STOP in state=%s\n",
			 __func__, state_to_string(ssif_bmc->state));
		ssif_bmc->state = SSIF_READY;

	} else if (ssif_bmc->state == SSIF_REQ_RECVING) {
		if (validate_request_part(ssif_bmc)) {
			process_request_part(ssif_bmc);
			if (ssif_bmc->part_buf.smbus_cmd == SSIF_IPMI_SINGLEPART_WRITE ||
			    ssif_bmc->part_buf.smbus_cmd == SSIF_IPMI_MULTIPART_WRITE_END)
				handle_request(ssif_bmc);
			ssif_bmc->state = SSIF_READY;
		} else {
			/*
			 * A BMC that receives an invalid request drop the data for the write
			 * transaction and any further transactions (read or write) until
			 * the next valid read or write Start transaction is received
			 */
			dev_err(&ssif_bmc->client->dev, "Error: invalid pec\n");
			ssif_bmc->aborting = true;
		}
	} else if (ssif_bmc->state == SSIF_RES_SENDING) {
		if (ssif_bmc->is_singlepart_read || ssif_bmc->block_num == 0xFF) {
			memset(&ssif_bmc->part_buf, 0, sizeof(struct ssif_part_buffer));
			/* Invalidate response buffer to denote it is sent */
			complete_response(ssif_bmc);
		}
		ssif_bmc->state = SSIF_READY;
	}

	/* Reset message index */
	ssif_bmc->msg_idx = 0;
}

/*
 * Callback function to handle I2C slave events
 */
static int ssif_bmc_cb(struct i2c_client *client, enum i2c_slave_event event, u8 *val)
{
	unsigned long flags;
	struct ssif_bmc_ctx *ssif_bmc = i2c_get_clientdata(client);
	int ret = 0;

	spin_lock_irqsave(&ssif_bmc->lock, flags);

	switch (event) {
	case I2C_SLAVE_READ_REQUESTED:
		on_read_requested_event(ssif_bmc, val);
		break;

	case I2C_SLAVE_WRITE_REQUESTED:
		on_write_requested_event(ssif_bmc, val);
		break;

	case I2C_SLAVE_READ_PROCESSED:
		on_read_processed_event(ssif_bmc, val);
		break;

	case I2C_SLAVE_WRITE_RECEIVED:
		on_write_received_event(ssif_bmc, val);
		break;

	case I2C_SLAVE_STOP:
		on_stop_event(ssif_bmc, val);
		break;

	default:
		dev_warn(&ssif_bmc->client->dev, "Warn: Unknown i2c slave event\n");
		break;
	}

	if (!ssif_bmc->aborting && ssif_bmc->busy)
		ret = -EBUSY;

	spin_unlock_irqrestore(&ssif_bmc->lock, flags);

	return ret;
}

static int ssif_bmc_probe(struct i2c_client *client)
{
	struct ssif_bmc_ctx *ssif_bmc;
	int ret;

	ssif_bmc = devm_kzalloc(&client->dev, sizeof(*ssif_bmc), GFP_KERNEL);
	if (!ssif_bmc)
		return -ENOMEM;

	spin_lock_init(&ssif_bmc->lock);

	init_waitqueue_head(&ssif_bmc->wait_queue);
	ssif_bmc->request_available = false;
	ssif_bmc->response_in_progress = false;
	ssif_bmc->busy = false;
	ssif_bmc->response_timer_inited = false;

	/* Register misc device interface */
	ssif_bmc->miscdev.minor = MISC_DYNAMIC_MINOR;
	ssif_bmc->miscdev.name = DEVICE_NAME;
	ssif_bmc->miscdev.fops = &ssif_bmc_fops;
	ssif_bmc->miscdev.parent = &client->dev;
	ret = misc_register(&ssif_bmc->miscdev);
	if (ret)
		return ret;

	ssif_bmc->client = client;
	ssif_bmc->client->flags |= I2C_CLIENT_SLAVE;

	/* Register I2C slave */
	i2c_set_clientdata(client, ssif_bmc);
	ret = i2c_slave_register(client, ssif_bmc_cb);
	if (ret)
		misc_deregister(&ssif_bmc->miscdev);

	return ret;
}

static void ssif_bmc_remove(struct i2c_client *client)
{
	struct ssif_bmc_ctx *ssif_bmc = i2c_get_clientdata(client);

	i2c_slave_unregister(client);
	misc_deregister(&ssif_bmc->miscdev);
}

static const struct of_device_id ssif_bmc_match[] = {
	{ .compatible = "ssif-bmc" },
	{ },
};
MODULE_DEVICE_TABLE(of, ssif_bmc_match);

static const struct i2c_device_id ssif_bmc_id[] = {
	{ DEVICE_NAME },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ssif_bmc_id);

static struct i2c_driver ssif_bmc_driver = {
	.driver         = {
		.name           = DEVICE_NAME,
		.of_match_table = ssif_bmc_match,
	},
	.probe          = ssif_bmc_probe,
	.remove         = ssif_bmc_remove,
	.id_table       = ssif_bmc_id,
};

module_i2c_driver(ssif_bmc_driver);

MODULE_AUTHOR("Quan Nguyen <quan@os.amperecomputing.com>");
MODULE_AUTHOR("Chuong Tran <chuong@os.amperecomputing.com>");
MODULE_DESCRIPTION("Linux device driver of the BMC IPMI SSIF interface.");
MODULE_LICENSE("GPL");
