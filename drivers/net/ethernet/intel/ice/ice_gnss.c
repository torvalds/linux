// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021-2022, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include <linux/tty_driver.h>

/**
 * ice_gnss_do_write - Write data to internal GNSS
 * @pf: board private structure
 * @buf: command buffer
 * @size: command buffer size
 *
 * Write UBX command data to the GNSS receiver
 */
static unsigned int
ice_gnss_do_write(struct ice_pf *pf, unsigned char *buf, unsigned int size)
{
	struct ice_aqc_link_topo_addr link_topo;
	struct ice_hw *hw = &pf->hw;
	unsigned int offset = 0;
	int err = 0;

	memset(&link_topo, 0, sizeof(struct ice_aqc_link_topo_addr));
	link_topo.topo_params.index = ICE_E810T_GNSS_I2C_BUS;
	link_topo.topo_params.node_type_ctx |=
		FIELD_PREP(ICE_AQC_LINK_TOPO_NODE_CTX_M,
			   ICE_AQC_LINK_TOPO_NODE_CTX_OVERRIDE);

	/* It's not possible to write a single byte to u-blox.
	 * Write all bytes in a loop until there are 6 or less bytes left. If
	 * there are exactly 6 bytes left, the last write would be only a byte.
	 * In this case, do 4+2 bytes writes instead of 5+1. Otherwise, do the
	 * last 2 to 5 bytes write.
	 */
	while (size - offset > ICE_GNSS_UBX_WRITE_BYTES + 1) {
		err = ice_aq_write_i2c(hw, link_topo, ICE_GNSS_UBX_I2C_BUS_ADDR,
				       cpu_to_le16(buf[offset]),
				       ICE_MAX_I2C_WRITE_BYTES,
				       &buf[offset + 1], NULL);
		if (err)
			goto err_out;

		offset += ICE_GNSS_UBX_WRITE_BYTES;
	}

	/* Single byte would be written. Write 4 bytes instead of 5. */
	if (size - offset == ICE_GNSS_UBX_WRITE_BYTES + 1) {
		err = ice_aq_write_i2c(hw, link_topo, ICE_GNSS_UBX_I2C_BUS_ADDR,
				       cpu_to_le16(buf[offset]),
				       ICE_MAX_I2C_WRITE_BYTES - 1,
				       &buf[offset + 1], NULL);
		if (err)
			goto err_out;

		offset += ICE_GNSS_UBX_WRITE_BYTES - 1;
	}

	/* Do the last write, 2 to 5 bytes. */
	err = ice_aq_write_i2c(hw, link_topo, ICE_GNSS_UBX_I2C_BUS_ADDR,
			       cpu_to_le16(buf[offset]), size - offset - 1,
			       &buf[offset + 1], NULL);
	if (err)
		goto err_out;

	return size;

err_out:
	dev_err(ice_pf_to_dev(pf), "GNSS failed to write, offset=%u, size=%u, err=%d\n",
		offset, size, err);

	return offset;
}

/**
 * ice_gnss_write_pending - Write all pending data to internal GNSS
 * @work: GNSS write work structure
 */
static void ice_gnss_write_pending(struct kthread_work *work)
{
	struct gnss_serial *gnss = container_of(work, struct gnss_serial,
						write_work);
	struct ice_pf *pf = gnss->back;

	if (!list_empty(&gnss->queue)) {
		struct gnss_write_buf *write_buf = NULL;
		unsigned int bytes;

		write_buf = list_first_entry(&gnss->queue,
					     struct gnss_write_buf, queue);

		bytes = ice_gnss_do_write(pf, write_buf->buf, write_buf->size);
		dev_dbg(ice_pf_to_dev(pf), "%u bytes written to GNSS\n", bytes);

		list_del(&write_buf->queue);
		kfree(write_buf->buf);
		kfree(write_buf);
	}
}

/**
 * ice_gnss_read - Read data from internal GNSS module
 * @work: GNSS read work structure
 *
 * Read the data from internal GNSS receiver, number of bytes read will be
 * returned in *read_data parameter.
 */
static void ice_gnss_read(struct kthread_work *work)
{
	struct gnss_serial *gnss = container_of(work, struct gnss_serial,
						read_work.work);
	struct ice_aqc_link_topo_addr link_topo;
	unsigned int i, bytes_read, data_len;
	struct tty_port *port;
	struct ice_pf *pf;
	struct ice_hw *hw;
	__be16 data_len_b;
	char *buf = NULL;
	u8 i2c_params;
	int err = 0;

	pf = gnss->back;
	if (!pf || !gnss->tty || !gnss->tty->port) {
		err = -EFAULT;
		goto exit;
	}

	hw = &pf->hw;
	port = gnss->tty->port;

	buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto exit;
	}

	memset(&link_topo, 0, sizeof(struct ice_aqc_link_topo_addr));
	link_topo.topo_params.index = ICE_E810T_GNSS_I2C_BUS;
	link_topo.topo_params.node_type_ctx |=
		FIELD_PREP(ICE_AQC_LINK_TOPO_NODE_CTX_M,
			   ICE_AQC_LINK_TOPO_NODE_CTX_OVERRIDE);

	i2c_params = ICE_GNSS_UBX_DATA_LEN_WIDTH |
		     ICE_AQC_I2C_USE_REPEATED_START;

	/* Read data length in a loop, when it's not 0 the data is ready */
	for (i = 0; i < ICE_MAX_UBX_READ_TRIES; i++) {
		err = ice_aq_read_i2c(hw, link_topo, ICE_GNSS_UBX_I2C_BUS_ADDR,
				      cpu_to_le16(ICE_GNSS_UBX_DATA_LEN_H),
				      i2c_params, (u8 *)&data_len_b, NULL);
		if (err)
			goto exit_buf;

		data_len = be16_to_cpu(data_len_b);
		if (data_len != 0 && data_len != U16_MAX)
			break;

		mdelay(10);
	}

	data_len = min_t(typeof(data_len), data_len, PAGE_SIZE);
	data_len = tty_buffer_request_room(port, data_len);
	if (!data_len) {
		err = -ENOMEM;
		goto exit_buf;
	}

	/* Read received data */
	for (i = 0; i < data_len; i += bytes_read) {
		unsigned int bytes_left = data_len - i;

		bytes_read = min_t(typeof(bytes_left), bytes_left,
				   ICE_MAX_I2C_DATA_SIZE);

		err = ice_aq_read_i2c(hw, link_topo, ICE_GNSS_UBX_I2C_BUS_ADDR,
				      cpu_to_le16(ICE_GNSS_UBX_EMPTY_DATA),
				      bytes_read, &buf[i], NULL);
		if (err)
			goto exit_buf;
	}

	/* Send the data to the tty layer for users to read. This doesn't
	 * actually push the data through unless tty->low_latency is set.
	 */
	tty_insert_flip_string(port, buf, i);
	tty_flip_buffer_push(port);

exit_buf:
	free_page((unsigned long)buf);
	kthread_queue_delayed_work(gnss->kworker, &gnss->read_work,
				   ICE_GNSS_TIMER_DELAY_TIME);
exit:
	if (err)
		dev_dbg(ice_pf_to_dev(pf), "GNSS failed to read err=%d\n", err);
}

/**
 * ice_gnss_struct_init - Initialize GNSS structure for the TTY
 * @pf: Board private structure
 * @index: TTY device index
 */
static struct gnss_serial *ice_gnss_struct_init(struct ice_pf *pf, int index)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct kthread_worker *kworker;
	struct gnss_serial *gnss;

	gnss = kzalloc(sizeof(*gnss), GFP_KERNEL);
	if (!gnss)
		return NULL;

	mutex_init(&gnss->gnss_mutex);
	gnss->open_count = 0;
	gnss->back = pf;
	pf->gnss_serial[index] = gnss;

	kthread_init_delayed_work(&gnss->read_work, ice_gnss_read);
	INIT_LIST_HEAD(&gnss->queue);
	kthread_init_work(&gnss->write_work, ice_gnss_write_pending);
	/* Allocate a kworker for handling work required for the GNSS TTY
	 * writes.
	 */
	kworker = kthread_create_worker(0, "ice-gnss-%s", dev_name(dev));
	if (IS_ERR(kworker)) {
		kfree(gnss);
		return NULL;
	}

	gnss->kworker = kworker;

	return gnss;
}

/**
 * ice_gnss_tty_open - Initialize GNSS structures on TTY device open
 * @tty: pointer to the tty_struct
 * @filp: pointer to the file
 *
 * This routine is mandatory. If this routine is not filled in, the attempted
 * open will fail with ENODEV.
 */
static int ice_gnss_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct gnss_serial *gnss;
	struct ice_pf *pf;

	pf = (struct ice_pf *)tty->driver->driver_state;
	if (!pf)
		return -EFAULT;

	/* Clear the pointer in case something fails */
	tty->driver_data = NULL;

	/* Get the serial object associated with this tty pointer */
	gnss = pf->gnss_serial[tty->index];
	if (!gnss) {
		/* Initialize GNSS struct on the first device open */
		gnss = ice_gnss_struct_init(pf, tty->index);
		if (!gnss)
			return -ENOMEM;
	}

	mutex_lock(&gnss->gnss_mutex);

	/* Save our structure within the tty structure */
	tty->driver_data = gnss;
	gnss->tty = tty;
	gnss->open_count++;
	kthread_queue_delayed_work(gnss->kworker, &gnss->read_work, 0);

	mutex_unlock(&gnss->gnss_mutex);

	return 0;
}

/**
 * ice_gnss_tty_close - Cleanup GNSS structures on tty device close
 * @tty: pointer to the tty_struct
 * @filp: pointer to the file
 */
static void ice_gnss_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct gnss_serial *gnss = tty->driver_data;
	struct ice_pf *pf;

	if (!gnss)
		return;

	pf = (struct ice_pf *)tty->driver->driver_state;
	if (!pf)
		return;

	mutex_lock(&gnss->gnss_mutex);

	if (!gnss->open_count) {
		/* Port was never opened */
		dev_err(ice_pf_to_dev(pf), "GNSS port not opened\n");
		goto exit;
	}

	gnss->open_count--;
	if (gnss->open_count <= 0) {
		/* Port is in shutdown state */
		kthread_cancel_delayed_work_sync(&gnss->read_work);
	}
exit:
	mutex_unlock(&gnss->gnss_mutex);
}

/**
 * ice_gnss_tty_write - Write GNSS data
 * @tty: pointer to the tty_struct
 * @buf: pointer to the user data
 * @count: the number of characters queued to be sent to the HW
 *
 * The write function call is called by the user when there is data to be sent
 * to the hardware. First the tty core receives the call, and then it passes the
 * data on to the tty driver's write function. The tty core also tells the tty
 * driver the size of the data being sent.
 * If any errors happen during the write call, a negative error value should be
 * returned instead of the number of characters queued to be written.
 */
static int
ice_gnss_tty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	struct gnss_write_buf *write_buf;
	struct gnss_serial *gnss;
	unsigned char *cmd_buf;
	struct ice_pf *pf;
	int err = count;

	/* We cannot write a single byte using our I2C implementation. */
	if (count <= 1 || count > ICE_GNSS_TTY_WRITE_BUF)
		return -EINVAL;

	gnss = tty->driver_data;
	if (!gnss)
		return -EFAULT;

	pf = (struct ice_pf *)tty->driver->driver_state;
	if (!pf)
		return -EFAULT;

	/* Only allow to write on TTY 0 */
	if (gnss != pf->gnss_serial[0])
		return -EIO;

	mutex_lock(&gnss->gnss_mutex);

	if (!gnss->open_count) {
		err = -EINVAL;
		goto exit;
	}

	cmd_buf = kcalloc(count, sizeof(*buf), GFP_KERNEL);
	if (!cmd_buf) {
		err = -ENOMEM;
		goto exit;
	}

	memcpy(cmd_buf, buf, count);

	/* Send the data out to a hardware port */
	write_buf = kzalloc(sizeof(*write_buf), GFP_KERNEL);
	if (!write_buf) {
		kfree(cmd_buf);
		err = -ENOMEM;
		goto exit;
	}

	write_buf->buf = cmd_buf;
	write_buf->size = count;
	INIT_LIST_HEAD(&write_buf->queue);
	list_add_tail(&write_buf->queue, &gnss->queue);
	kthread_queue_work(gnss->kworker, &gnss->write_work);
exit:
	mutex_unlock(&gnss->gnss_mutex);
	return err;
}

/**
 * ice_gnss_tty_write_room - Returns the numbers of characters to be written.
 * @tty: pointer to the tty_struct
 *
 * This routine returns the numbers of characters the tty driver will accept
 * for queuing to be written or 0 if either the TTY is not open or user
 * tries to write to the TTY other than the first.
 */
static unsigned int ice_gnss_tty_write_room(struct tty_struct *tty)
{
	struct gnss_serial *gnss = tty->driver_data;

	/* Only allow to write on TTY 0 */
	if (!gnss || gnss != gnss->back->gnss_serial[0])
		return 0;

	mutex_lock(&gnss->gnss_mutex);

	if (!gnss->open_count) {
		mutex_unlock(&gnss->gnss_mutex);
		return 0;
	}

	mutex_unlock(&gnss->gnss_mutex);
	return ICE_GNSS_TTY_WRITE_BUF;
}

static const struct tty_operations tty_gps_ops = {
	.open =		ice_gnss_tty_open,
	.close =	ice_gnss_tty_close,
	.write =	ice_gnss_tty_write,
	.write_room =	ice_gnss_tty_write_room,
};

/**
 * ice_gnss_create_tty_driver - Create a TTY driver for GNSS
 * @pf: Board private structure
 */
static struct tty_driver *ice_gnss_create_tty_driver(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	const int ICE_TTYDRV_NAME_MAX = 14;
	struct tty_driver *tty_driver;
	char *ttydrv_name;
	unsigned int i;
	int err;

	tty_driver = tty_alloc_driver(ICE_GNSS_TTY_MINOR_DEVICES,
				      TTY_DRIVER_REAL_RAW);
	if (IS_ERR(tty_driver)) {
		dev_err(dev, "Failed to allocate memory for GNSS TTY\n");
		return NULL;
	}

	ttydrv_name = kzalloc(ICE_TTYDRV_NAME_MAX, GFP_KERNEL);
	if (!ttydrv_name) {
		tty_driver_kref_put(tty_driver);
		return NULL;
	}

	snprintf(ttydrv_name, ICE_TTYDRV_NAME_MAX, "ttyGNSS_%02x%02x_",
		 (u8)pf->pdev->bus->number, (u8)PCI_SLOT(pf->pdev->devfn));

	/* Initialize the tty driver*/
	tty_driver->owner = THIS_MODULE;
	tty_driver->driver_name = dev_driver_string(dev);
	tty_driver->name = (const char *)ttydrv_name;
	tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	tty_driver->subtype = SERIAL_TYPE_NORMAL;
	tty_driver->init_termios = tty_std_termios;
	tty_driver->init_termios.c_iflag &= ~INLCR;
	tty_driver->init_termios.c_iflag |= IGNCR;
	tty_driver->init_termios.c_oflag &= ~OPOST;
	tty_driver->init_termios.c_lflag &= ~ICANON;
	tty_driver->init_termios.c_cflag &= ~(CSIZE | CBAUD | CBAUDEX);
	/* baud rate 9600 */
	tty_termios_encode_baud_rate(&tty_driver->init_termios, 9600, 9600);
	tty_driver->driver_state = pf;
	tty_set_operations(tty_driver, &tty_gps_ops);

	for (i = 0; i < ICE_GNSS_TTY_MINOR_DEVICES; i++) {
		pf->gnss_tty_port[i] = kzalloc(sizeof(*pf->gnss_tty_port[i]),
					       GFP_KERNEL);
		if (!pf->gnss_tty_port[i])
			goto err_out;

		pf->gnss_serial[i] = NULL;

		tty_port_init(pf->gnss_tty_port[i]);
		tty_port_link_device(pf->gnss_tty_port[i], tty_driver, i);
	}

	err = tty_register_driver(tty_driver);
	if (err) {
		dev_err(dev, "Failed to register TTY driver err=%d\n", err);
		goto err_out;
	}

	for (i = 0; i < ICE_GNSS_TTY_MINOR_DEVICES; i++)
		dev_info(dev, "%s%d registered\n", ttydrv_name, i);

	return tty_driver;

err_out:
	while (i--) {
		tty_port_destroy(pf->gnss_tty_port[i]);
		kfree(pf->gnss_tty_port[i]);
	}
	kfree(ttydrv_name);
	tty_driver_kref_put(pf->ice_gnss_tty_driver);

	return NULL;
}

/**
 * ice_gnss_init - Initialize GNSS TTY support
 * @pf: Board private structure
 */
void ice_gnss_init(struct ice_pf *pf)
{
	struct tty_driver *tty_driver;

	tty_driver = ice_gnss_create_tty_driver(pf);
	if (!tty_driver)
		return;

	pf->ice_gnss_tty_driver = tty_driver;

	set_bit(ICE_FLAG_GNSS, pf->flags);
	dev_info(ice_pf_to_dev(pf), "GNSS TTY init successful\n");
}

/**
 * ice_gnss_exit - Disable GNSS TTY support
 * @pf: Board private structure
 */
void ice_gnss_exit(struct ice_pf *pf)
{
	unsigned int i;

	if (!test_bit(ICE_FLAG_GNSS, pf->flags) || !pf->ice_gnss_tty_driver)
		return;

	for (i = 0; i < ICE_GNSS_TTY_MINOR_DEVICES; i++) {
		if (pf->gnss_tty_port[i]) {
			tty_port_destroy(pf->gnss_tty_port[i]);
			kfree(pf->gnss_tty_port[i]);
		}

		if (pf->gnss_serial[i]) {
			struct gnss_serial *gnss = pf->gnss_serial[i];

			kthread_cancel_work_sync(&gnss->write_work);
			kthread_cancel_delayed_work_sync(&gnss->read_work);
			kfree(gnss);
			pf->gnss_serial[i] = NULL;
		}
	}

	tty_unregister_driver(pf->ice_gnss_tty_driver);
	kfree(pf->ice_gnss_tty_driver->name);
	tty_driver_kref_put(pf->ice_gnss_tty_driver);
	pf->ice_gnss_tty_driver = NULL;
}

/**
 * ice_gnss_is_gps_present - Check if GPS HW is present
 * @hw: pointer to HW struct
 */
bool ice_gnss_is_gps_present(struct ice_hw *hw)
{
	if (!hw->func_caps.ts_func_info.src_tmr_owned)
		return false;

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
	if (ice_is_e810t(hw)) {
		int err;
		u8 data;

		err = ice_read_pca9575_reg_e810t(hw, ICE_PCA9575_P0_IN, &data);
		if (err || !!(data & ICE_E810T_P0_GNSS_PRSNT_N))
			return false;
	} else {
		return false;
	}
#else
	if (!ice_is_e810t(hw))
		return false;
#endif /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */

	return true;
}
