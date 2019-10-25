// SPDX-License-Identifier: GPL-2.0
// SPI interface for ChromeOS Embedded Controller
//
// Copyright (C) 2012 Google, Inc

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <uapi/linux/sched/types.h>

/* The header byte, which follows the preamble */
#define EC_MSG_HEADER			0xec

/*
 * Number of EC preamble bytes we read at a time. Since it takes
 * about 400-500us for the EC to respond there is not a lot of
 * point in tuning this. If the EC could respond faster then
 * we could increase this so that might expect the preamble and
 * message to occur in a single transaction. However, the maximum
 * SPI transfer size is 256 bytes, so at 5MHz we need a response
 * time of perhaps <320us (200 bytes / 1600 bits).
 */
#define EC_MSG_PREAMBLE_COUNT		32

/*
 * Allow for a long time for the EC to respond.  We support i2c
 * tunneling and support fairly long messages for the tunnel (249
 * bytes long at the moment).  If we're talking to a 100 kHz device
 * on the other end and need to transfer ~256 bytes, then we need:
 *  10 us/bit * ~10 bits/byte * ~256 bytes = ~25ms
 *
 * We'll wait 8 times that to handle clock stretching and other
 * paranoia.  Note that some battery gas gauge ICs claim to have a
 * clock stretch of 144ms in rare situations.  That's incentive for
 * not directly passing i2c through, but it's too late for that for
 * existing hardware.
 *
 * It's pretty unlikely that we'll really see a 249 byte tunnel in
 * anything other than testing.  If this was more common we might
 * consider having slow commands like this require a GET_STATUS
 * wait loop.  The 'flash write' command would be another candidate
 * for this, clocking in at 2-3ms.
 */
#define EC_MSG_DEADLINE_MS		200

/*
  * Time between raising the SPI chip select (for the end of a
  * transaction) and dropping it again (for the next transaction).
  * If we go too fast, the EC will miss the transaction. We know that we
  * need at least 70 us with the 16 MHz STM32 EC, so go with 200 us to be
  * safe.
  */
#define EC_SPI_RECOVERY_TIME_NS	(200 * 1000)

/**
 * struct cros_ec_spi - information about a SPI-connected EC
 *
 * @spi: SPI device we are connected to
 * @last_transfer_ns: time that we last finished a transfer.
 * @start_of_msg_delay: used to set the delay_usecs on the spi_transfer that
 *      is sent when we want to turn on CS at the start of a transaction.
 * @end_of_msg_delay: used to set the delay_usecs on the spi_transfer that
 *      is sent when we want to turn off CS at the end of a transaction.
 * @high_pri_worker: Used to schedule high priority work.
 */
struct cros_ec_spi {
	struct spi_device *spi;
	s64 last_transfer_ns;
	unsigned int start_of_msg_delay;
	unsigned int end_of_msg_delay;
	struct kthread_worker *high_pri_worker;
};

typedef int (*cros_ec_xfer_fn_t) (struct cros_ec_device *ec_dev,
				  struct cros_ec_command *ec_msg);

/**
 * struct cros_ec_xfer_work_params - params for our high priority workers
 *
 * @work: The work_struct needed to queue work
 * @fn: The function to use to transfer
 * @ec_dev: ChromeOS EC device
 * @ec_msg: Message to transfer
 * @ret: The return value of the function
 */

struct cros_ec_xfer_work_params {
	struct kthread_work work;
	cros_ec_xfer_fn_t fn;
	struct cros_ec_device *ec_dev;
	struct cros_ec_command *ec_msg;
	int ret;
};

static void debug_packet(struct device *dev, const char *name, u8 *ptr,
			 int len)
{
#ifdef DEBUG
	int i;

	dev_dbg(dev, "%s: ", name);
	for (i = 0; i < len; i++)
		pr_cont(" %02x", ptr[i]);

	pr_cont("\n");
#endif
}

static int terminate_request(struct cros_ec_device *ec_dev)
{
	struct cros_ec_spi *ec_spi = ec_dev->priv;
	struct spi_message msg;
	struct spi_transfer trans;
	int ret;

	/*
	 * Turn off CS, possibly adding a delay to ensure the rising edge
	 * doesn't come too soon after the end of the data.
	 */
	spi_message_init(&msg);
	memset(&trans, 0, sizeof(trans));
	trans.delay_usecs = ec_spi->end_of_msg_delay;
	spi_message_add_tail(&trans, &msg);

	ret = spi_sync_locked(ec_spi->spi, &msg);

	/* Reset end-of-response timer */
	ec_spi->last_transfer_ns = ktime_get_ns();
	if (ret < 0) {
		dev_err(ec_dev->dev,
			"cs-deassert spi transfer failed: %d\n",
			ret);
	}

	return ret;
}

/**
 * receive_n_bytes - receive n bytes from the EC.
 *
 * Assumes buf is a pointer into the ec_dev->din buffer
 */
static int receive_n_bytes(struct cros_ec_device *ec_dev, u8 *buf, int n)
{
	struct cros_ec_spi *ec_spi = ec_dev->priv;
	struct spi_transfer trans;
	struct spi_message msg;
	int ret;

	BUG_ON(buf - ec_dev->din + n > ec_dev->din_size);

	memset(&trans, 0, sizeof(trans));
	trans.cs_change = 1;
	trans.rx_buf = buf;
	trans.len = n;

	spi_message_init(&msg);
	spi_message_add_tail(&trans, &msg);
	ret = spi_sync_locked(ec_spi->spi, &msg);
	if (ret < 0)
		dev_err(ec_dev->dev, "spi transfer failed: %d\n", ret);

	return ret;
}

/**
 * cros_ec_spi_receive_packet - Receive a packet from the EC.
 *
 * This function has two phases: reading the preamble bytes (since if we read
 * data from the EC before it is ready to send, we just get preamble) and
 * reading the actual message.
 *
 * The received data is placed into ec_dev->din.
 *
 * @ec_dev: ChromeOS EC device
 * @need_len: Number of message bytes we need to read
 */
static int cros_ec_spi_receive_packet(struct cros_ec_device *ec_dev,
				      int need_len)
{
	struct ec_host_response *response;
	u8 *ptr, *end;
	int ret;
	unsigned long deadline;
	int todo;

	BUG_ON(ec_dev->din_size < EC_MSG_PREAMBLE_COUNT);

	/* Receive data until we see the header byte */
	deadline = jiffies + msecs_to_jiffies(EC_MSG_DEADLINE_MS);
	while (true) {
		unsigned long start_jiffies = jiffies;

		ret = receive_n_bytes(ec_dev,
				      ec_dev->din,
				      EC_MSG_PREAMBLE_COUNT);
		if (ret < 0)
			return ret;

		ptr = ec_dev->din;
		for (end = ptr + EC_MSG_PREAMBLE_COUNT; ptr != end; ptr++) {
			if (*ptr == EC_SPI_FRAME_START) {
				dev_dbg(ec_dev->dev, "msg found at %zd\n",
					ptr - ec_dev->din);
				break;
			}
		}
		if (ptr != end)
			break;

		/*
		 * Use the time at the start of the loop as a timeout.  This
		 * gives us one last shot at getting the transfer and is useful
		 * in case we got context switched out for a while.
		 */
		if (time_after(start_jiffies, deadline)) {
			dev_warn(ec_dev->dev, "EC failed to respond in time\n");
			return -ETIMEDOUT;
		}
	}

	/*
	 * ptr now points to the header byte. Copy any valid data to the
	 * start of our buffer
	 */
	todo = end - ++ptr;
	BUG_ON(todo < 0 || todo > ec_dev->din_size);
	todo = min(todo, need_len);
	memmove(ec_dev->din, ptr, todo);
	ptr = ec_dev->din + todo;
	dev_dbg(ec_dev->dev, "need %d, got %d bytes from preamble\n",
		need_len, todo);
	need_len -= todo;

	/* If the entire response struct wasn't read, get the rest of it. */
	if (todo < sizeof(*response)) {
		ret = receive_n_bytes(ec_dev, ptr, sizeof(*response) - todo);
		if (ret < 0)
			return -EBADMSG;
		ptr += (sizeof(*response) - todo);
		todo = sizeof(*response);
	}

	response = (struct ec_host_response *)ec_dev->din;

	/* Abort if data_len is too large. */
	if (response->data_len > ec_dev->din_size)
		return -EMSGSIZE;

	/* Receive data until we have it all */
	while (need_len > 0) {
		/*
		 * We can't support transfers larger than the SPI FIFO size
		 * unless we have DMA. We don't have DMA on the ISP SPI ports
		 * for Exynos. We need a way of asking SPI driver for
		 * maximum-supported transfer size.
		 */
		todo = min(need_len, 256);
		dev_dbg(ec_dev->dev, "loop, todo=%d, need_len=%d, ptr=%zd\n",
			todo, need_len, ptr - ec_dev->din);

		ret = receive_n_bytes(ec_dev, ptr, todo);
		if (ret < 0)
			return ret;

		ptr += todo;
		need_len -= todo;
	}

	dev_dbg(ec_dev->dev, "loop done, ptr=%zd\n", ptr - ec_dev->din);

	return 0;
}

/**
 * cros_ec_spi_receive_response - Receive a response from the EC.
 *
 * This function has two phases: reading the preamble bytes (since if we read
 * data from the EC before it is ready to send, we just get preamble) and
 * reading the actual message.
 *
 * The received data is placed into ec_dev->din.
 *
 * @ec_dev: ChromeOS EC device
 * @need_len: Number of message bytes we need to read
 */
static int cros_ec_spi_receive_response(struct cros_ec_device *ec_dev,
					int need_len)
{
	u8 *ptr, *end;
	int ret;
	unsigned long deadline;
	int todo;

	BUG_ON(ec_dev->din_size < EC_MSG_PREAMBLE_COUNT);

	/* Receive data until we see the header byte */
	deadline = jiffies + msecs_to_jiffies(EC_MSG_DEADLINE_MS);
	while (true) {
		unsigned long start_jiffies = jiffies;

		ret = receive_n_bytes(ec_dev,
				      ec_dev->din,
				      EC_MSG_PREAMBLE_COUNT);
		if (ret < 0)
			return ret;

		ptr = ec_dev->din;
		for (end = ptr + EC_MSG_PREAMBLE_COUNT; ptr != end; ptr++) {
			if (*ptr == EC_SPI_FRAME_START) {
				dev_dbg(ec_dev->dev, "msg found at %zd\n",
					ptr - ec_dev->din);
				break;
			}
		}
		if (ptr != end)
			break;

		/*
		 * Use the time at the start of the loop as a timeout.  This
		 * gives us one last shot at getting the transfer and is useful
		 * in case we got context switched out for a while.
		 */
		if (time_after(start_jiffies, deadline)) {
			dev_warn(ec_dev->dev, "EC failed to respond in time\n");
			return -ETIMEDOUT;
		}
	}

	/*
	 * ptr now points to the header byte. Copy any valid data to the
	 * start of our buffer
	 */
	todo = end - ++ptr;
	BUG_ON(todo < 0 || todo > ec_dev->din_size);
	todo = min(todo, need_len);
	memmove(ec_dev->din, ptr, todo);
	ptr = ec_dev->din + todo;
	dev_dbg(ec_dev->dev, "need %d, got %d bytes from preamble\n",
		 need_len, todo);
	need_len -= todo;

	/* Receive data until we have it all */
	while (need_len > 0) {
		/*
		 * We can't support transfers larger than the SPI FIFO size
		 * unless we have DMA. We don't have DMA on the ISP SPI ports
		 * for Exynos. We need a way of asking SPI driver for
		 * maximum-supported transfer size.
		 */
		todo = min(need_len, 256);
		dev_dbg(ec_dev->dev, "loop, todo=%d, need_len=%d, ptr=%zd\n",
			todo, need_len, ptr - ec_dev->din);

		ret = receive_n_bytes(ec_dev, ptr, todo);
		if (ret < 0)
			return ret;

		debug_packet(ec_dev->dev, "interim", ptr, todo);
		ptr += todo;
		need_len -= todo;
	}

	dev_dbg(ec_dev->dev, "loop done, ptr=%zd\n", ptr - ec_dev->din);

	return 0;
}

/**
 * do_cros_ec_pkt_xfer_spi - Transfer a packet over SPI and receive the reply
 *
 * @ec_dev: ChromeOS EC device
 * @ec_msg: Message to transfer
 */
static int do_cros_ec_pkt_xfer_spi(struct cros_ec_device *ec_dev,
				   struct cros_ec_command *ec_msg)
{
	struct ec_host_response *response;
	struct cros_ec_spi *ec_spi = ec_dev->priv;
	struct spi_transfer trans, trans_delay;
	struct spi_message msg;
	int i, len;
	u8 *ptr;
	u8 *rx_buf;
	u8 sum;
	u8 rx_byte;
	int ret = 0, final_ret;
	unsigned long delay;

	len = cros_ec_prepare_tx(ec_dev, ec_msg);
	dev_dbg(ec_dev->dev, "prepared, len=%d\n", len);

	/* If it's too soon to do another transaction, wait */
	delay = ktime_get_ns() - ec_spi->last_transfer_ns;
	if (delay < EC_SPI_RECOVERY_TIME_NS)
		ndelay(EC_SPI_RECOVERY_TIME_NS - delay);

	rx_buf = kzalloc(len, GFP_KERNEL);
	if (!rx_buf)
		return -ENOMEM;

	spi_bus_lock(ec_spi->spi->master);

	/*
	 * Leave a gap between CS assertion and clocking of data to allow the
	 * EC time to wakeup.
	 */
	spi_message_init(&msg);
	if (ec_spi->start_of_msg_delay) {
		memset(&trans_delay, 0, sizeof(trans_delay));
		trans_delay.delay_usecs = ec_spi->start_of_msg_delay;
		spi_message_add_tail(&trans_delay, &msg);
	}

	/* Transmit phase - send our message */
	memset(&trans, 0, sizeof(trans));
	trans.tx_buf = ec_dev->dout;
	trans.rx_buf = rx_buf;
	trans.len = len;
	trans.cs_change = 1;
	spi_message_add_tail(&trans, &msg);
	ret = spi_sync_locked(ec_spi->spi, &msg);

	/* Get the response */
	if (!ret) {
		/* Verify that EC can process command */
		for (i = 0; i < len; i++) {
			rx_byte = rx_buf[i];
			/*
			 * Seeing the PAST_END, RX_BAD_DATA, or NOT_READY
			 * markers are all signs that the EC didn't fully
			 * receive our command. e.g., if the EC is flashing
			 * itself, it can't respond to any commands and instead
			 * clocks out EC_SPI_PAST_END from its SPI hardware
			 * buffer. Similar occurrences can happen if the AP is
			 * too slow to clock out data after asserting CS -- the
			 * EC will abort and fill its buffer with
			 * EC_SPI_RX_BAD_DATA.
			 *
			 * In all cases, these errors should be safe to retry.
			 * Report -EAGAIN and let the caller decide what to do
			 * about that.
			 */
			if (rx_byte == EC_SPI_PAST_END  ||
			    rx_byte == EC_SPI_RX_BAD_DATA ||
			    rx_byte == EC_SPI_NOT_READY) {
				ret = -EAGAIN;
				break;
			}
		}
	}

	if (!ret)
		ret = cros_ec_spi_receive_packet(ec_dev,
				ec_msg->insize + sizeof(*response));
	else if (ret != -EAGAIN)
		dev_err(ec_dev->dev, "spi transfer failed: %d\n", ret);

	final_ret = terminate_request(ec_dev);

	spi_bus_unlock(ec_spi->spi->master);

	if (!ret)
		ret = final_ret;
	if (ret < 0)
		goto exit;

	ptr = ec_dev->din;

	/* check response error code */
	response = (struct ec_host_response *)ptr;
	ec_msg->result = response->result;

	ret = cros_ec_check_result(ec_dev, ec_msg);
	if (ret)
		goto exit;

	len = response->data_len;
	sum = 0;
	if (len > ec_msg->insize) {
		dev_err(ec_dev->dev, "packet too long (%d bytes, expected %d)",
			len, ec_msg->insize);
		ret = -EMSGSIZE;
		goto exit;
	}

	for (i = 0; i < sizeof(*response); i++)
		sum += ptr[i];

	/* copy response packet payload and compute checksum */
	memcpy(ec_msg->data, ptr + sizeof(*response), len);
	for (i = 0; i < len; i++)
		sum += ec_msg->data[i];

	if (sum) {
		dev_err(ec_dev->dev,
			"bad packet checksum, calculated %x\n",
			sum);
		ret = -EBADMSG;
		goto exit;
	}

	ret = len;
exit:
	kfree(rx_buf);
	if (ec_msg->command == EC_CMD_REBOOT_EC)
		msleep(EC_REBOOT_DELAY_MS);

	return ret;
}

/**
 * do_cros_ec_cmd_xfer_spi - Transfer a message over SPI and receive the reply
 *
 * @ec_dev: ChromeOS EC device
 * @ec_msg: Message to transfer
 */
static int do_cros_ec_cmd_xfer_spi(struct cros_ec_device *ec_dev,
				   struct cros_ec_command *ec_msg)
{
	struct cros_ec_spi *ec_spi = ec_dev->priv;
	struct spi_transfer trans;
	struct spi_message msg;
	int i, len;
	u8 *ptr;
	u8 *rx_buf;
	u8 rx_byte;
	int sum;
	int ret = 0, final_ret;
	unsigned long delay;

	len = cros_ec_prepare_tx(ec_dev, ec_msg);
	dev_dbg(ec_dev->dev, "prepared, len=%d\n", len);

	/* If it's too soon to do another transaction, wait */
	delay = ktime_get_ns() - ec_spi->last_transfer_ns;
	if (delay < EC_SPI_RECOVERY_TIME_NS)
		ndelay(EC_SPI_RECOVERY_TIME_NS - delay);

	rx_buf = kzalloc(len, GFP_KERNEL);
	if (!rx_buf)
		return -ENOMEM;

	spi_bus_lock(ec_spi->spi->master);

	/* Transmit phase - send our message */
	debug_packet(ec_dev->dev, "out", ec_dev->dout, len);
	memset(&trans, 0, sizeof(trans));
	trans.tx_buf = ec_dev->dout;
	trans.rx_buf = rx_buf;
	trans.len = len;
	trans.cs_change = 1;
	spi_message_init(&msg);
	spi_message_add_tail(&trans, &msg);
	ret = spi_sync_locked(ec_spi->spi, &msg);

	/* Get the response */
	if (!ret) {
		/* Verify that EC can process command */
		for (i = 0; i < len; i++) {
			rx_byte = rx_buf[i];
			/* See comments in cros_ec_pkt_xfer_spi() */
			if (rx_byte == EC_SPI_PAST_END  ||
			    rx_byte == EC_SPI_RX_BAD_DATA ||
			    rx_byte == EC_SPI_NOT_READY) {
				ret = -EAGAIN;
				break;
			}
		}
	}

	if (!ret)
		ret = cros_ec_spi_receive_response(ec_dev,
				ec_msg->insize + EC_MSG_TX_PROTO_BYTES);
	else if (ret != -EAGAIN)
		dev_err(ec_dev->dev, "spi transfer failed: %d\n", ret);

	final_ret = terminate_request(ec_dev);

	spi_bus_unlock(ec_spi->spi->master);

	if (!ret)
		ret = final_ret;
	if (ret < 0)
		goto exit;

	ptr = ec_dev->din;

	/* check response error code */
	ec_msg->result = ptr[0];
	ret = cros_ec_check_result(ec_dev, ec_msg);
	if (ret)
		goto exit;

	len = ptr[1];
	sum = ptr[0] + ptr[1];
	if (len > ec_msg->insize) {
		dev_err(ec_dev->dev, "packet too long (%d bytes, expected %d)",
			len, ec_msg->insize);
		ret = -ENOSPC;
		goto exit;
	}

	/* copy response packet payload and compute checksum */
	for (i = 0; i < len; i++) {
		sum += ptr[i + 2];
		if (ec_msg->insize)
			ec_msg->data[i] = ptr[i + 2];
	}
	sum &= 0xff;

	debug_packet(ec_dev->dev, "in", ptr, len + 3);

	if (sum != ptr[len + 2]) {
		dev_err(ec_dev->dev,
			"bad packet checksum, expected %02x, got %02x\n",
			sum, ptr[len + 2]);
		ret = -EBADMSG;
		goto exit;
	}

	ret = len;
exit:
	kfree(rx_buf);
	if (ec_msg->command == EC_CMD_REBOOT_EC)
		msleep(EC_REBOOT_DELAY_MS);

	return ret;
}

static void cros_ec_xfer_high_pri_work(struct kthread_work *work)
{
	struct cros_ec_xfer_work_params *params;

	params = container_of(work, struct cros_ec_xfer_work_params, work);
	params->ret = params->fn(params->ec_dev, params->ec_msg);
}

static int cros_ec_xfer_high_pri(struct cros_ec_device *ec_dev,
				 struct cros_ec_command *ec_msg,
				 cros_ec_xfer_fn_t fn)
{
	struct cros_ec_spi *ec_spi = ec_dev->priv;
	struct cros_ec_xfer_work_params params = {
		.work = KTHREAD_WORK_INIT(params.work,
					  cros_ec_xfer_high_pri_work),
		.ec_dev = ec_dev,
		.ec_msg = ec_msg,
		.fn = fn,
	};

	/*
	 * This looks a bit ridiculous.  Why do the work on a
	 * different thread if we're just going to block waiting for
	 * the thread to finish?  The key here is that the thread is
	 * running at high priority but the calling context might not
	 * be.  We need to be at high priority to avoid getting
	 * context switched out for too long and the EC giving up on
	 * the transfer.
	 */
	kthread_queue_work(ec_spi->high_pri_worker, &params.work);
	kthread_flush_work(&params.work);

	return params.ret;
}

static int cros_ec_pkt_xfer_spi(struct cros_ec_device *ec_dev,
				struct cros_ec_command *ec_msg)
{
	return cros_ec_xfer_high_pri(ec_dev, ec_msg, do_cros_ec_pkt_xfer_spi);
}

static int cros_ec_cmd_xfer_spi(struct cros_ec_device *ec_dev,
				struct cros_ec_command *ec_msg)
{
	return cros_ec_xfer_high_pri(ec_dev, ec_msg, do_cros_ec_cmd_xfer_spi);
}

static void cros_ec_spi_dt_probe(struct cros_ec_spi *ec_spi, struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 val;
	int ret;

	ret = of_property_read_u32(np, "google,cros-ec-spi-pre-delay", &val);
	if (!ret)
		ec_spi->start_of_msg_delay = val;

	ret = of_property_read_u32(np, "google,cros-ec-spi-msg-delay", &val);
	if (!ret)
		ec_spi->end_of_msg_delay = val;
}

static void cros_ec_spi_high_pri_release(void *worker)
{
	kthread_destroy_worker(worker);
}

static int cros_ec_spi_devm_high_pri_alloc(struct device *dev,
					   struct cros_ec_spi *ec_spi)
{
	struct sched_param sched_priority = {
		.sched_priority = MAX_RT_PRIO / 2,
	};
	int err;

	ec_spi->high_pri_worker =
		kthread_create_worker(0, "cros_ec_spi_high_pri");

	if (IS_ERR(ec_spi->high_pri_worker)) {
		err = PTR_ERR(ec_spi->high_pri_worker);
		dev_err(dev, "Can't create cros_ec high pri worker: %d\n", err);
		return err;
	}

	err = devm_add_action_or_reset(dev, cros_ec_spi_high_pri_release,
				       ec_spi->high_pri_worker);
	if (err)
		return err;

	err = sched_setscheduler_nocheck(ec_spi->high_pri_worker->task,
					 SCHED_FIFO, &sched_priority);
	if (err)
		dev_err(dev, "Can't set cros_ec high pri priority: %d\n", err);
	return err;
}

static int cros_ec_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct cros_ec_device *ec_dev;
	struct cros_ec_spi *ec_spi;
	int err;

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	spi->rt = true;
	err = spi_setup(spi);
	if (err < 0)
		return err;

	ec_spi = devm_kzalloc(dev, sizeof(*ec_spi), GFP_KERNEL);
	if (ec_spi == NULL)
		return -ENOMEM;
	ec_spi->spi = spi;
	ec_dev = devm_kzalloc(dev, sizeof(*ec_dev), GFP_KERNEL);
	if (!ec_dev)
		return -ENOMEM;

	/* Check for any DT properties */
	cros_ec_spi_dt_probe(ec_spi, dev);

	spi_set_drvdata(spi, ec_dev);
	ec_dev->dev = dev;
	ec_dev->priv = ec_spi;
	ec_dev->irq = spi->irq;
	ec_dev->cmd_xfer = cros_ec_cmd_xfer_spi;
	ec_dev->pkt_xfer = cros_ec_pkt_xfer_spi;
	ec_dev->phys_name = dev_name(&ec_spi->spi->dev);
	ec_dev->din_size = EC_MSG_PREAMBLE_COUNT +
			   sizeof(struct ec_host_response) +
			   sizeof(struct ec_response_get_protocol_info);
	ec_dev->dout_size = sizeof(struct ec_host_request);

	ec_spi->last_transfer_ns = ktime_get_ns();

	err = cros_ec_spi_devm_high_pri_alloc(dev, ec_spi);
	if (err)
		return err;

	err = cros_ec_register(ec_dev);
	if (err) {
		dev_err(dev, "cannot register EC\n");
		return err;
	}

	device_init_wakeup(&spi->dev, true);

	return 0;
}

static int cros_ec_spi_remove(struct spi_device *spi)
{
	struct cros_ec_device *ec_dev = spi_get_drvdata(spi);

	return cros_ec_unregister(ec_dev);
}

#ifdef CONFIG_PM_SLEEP
static int cros_ec_spi_suspend(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);

	return cros_ec_suspend(ec_dev);
}

static int cros_ec_spi_resume(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);

	return cros_ec_resume(ec_dev);
}
#endif

static SIMPLE_DEV_PM_OPS(cros_ec_spi_pm_ops, cros_ec_spi_suspend,
			 cros_ec_spi_resume);

static const struct of_device_id cros_ec_spi_of_match[] = {
	{ .compatible = "google,cros-ec-spi", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, cros_ec_spi_of_match);

static const struct spi_device_id cros_ec_spi_id[] = {
	{ "cros-ec-spi", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, cros_ec_spi_id);

static struct spi_driver cros_ec_driver_spi = {
	.driver	= {
		.name	= "cros-ec-spi",
		.of_match_table = cros_ec_spi_of_match,
		.pm	= &cros_ec_spi_pm_ops,
	},
	.probe		= cros_ec_spi_probe,
	.remove		= cros_ec_spi_remove,
	.id_table	= cros_ec_spi_id,
};

module_spi_driver(cros_ec_driver_spi);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SPI interface for ChromeOS Embedded Controller");
