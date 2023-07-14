// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021-2022, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"

/**
 * ice_gnss_do_write - Write data to internal GNSS receiver
 * @pf: board private structure
 * @buf: command buffer
 * @size: command buffer size
 *
 * Write UBX command data to the GNSS receiver
 *
 * Return:
 * * number of bytes written - success
 * * negative - error code
 */
static int
ice_gnss_do_write(struct ice_pf *pf, const unsigned char *buf, unsigned int size)
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

	return err;
}

/**
 * ice_gnss_read - Read data from internal GNSS module
 * @work: GNSS read work structure
 *
 * Read the data from internal GNSS receiver, write it to gnss_dev.
 */
static void ice_gnss_read(struct kthread_work *work)
{
	struct gnss_serial *gnss = container_of(work, struct gnss_serial,
						read_work.work);
	unsigned long delay = ICE_GNSS_POLL_DATA_DELAY_TIME;
	unsigned int i, bytes_read, data_len, count;
	struct ice_aqc_link_topo_addr link_topo;
	struct ice_pf *pf;
	struct ice_hw *hw;
	__be16 data_len_b;
	char *buf = NULL;
	u8 i2c_params;
	int err = 0;

	pf = gnss->back;
	if (!pf || !test_bit(ICE_FLAG_GNSS, pf->flags))
		return;

	hw = &pf->hw;

	memset(&link_topo, 0, sizeof(struct ice_aqc_link_topo_addr));
	link_topo.topo_params.index = ICE_E810T_GNSS_I2C_BUS;
	link_topo.topo_params.node_type_ctx |=
		FIELD_PREP(ICE_AQC_LINK_TOPO_NODE_CTX_M,
			   ICE_AQC_LINK_TOPO_NODE_CTX_OVERRIDE);

	i2c_params = ICE_GNSS_UBX_DATA_LEN_WIDTH |
		     ICE_AQC_I2C_USE_REPEATED_START;

	err = ice_aq_read_i2c(hw, link_topo, ICE_GNSS_UBX_I2C_BUS_ADDR,
			      cpu_to_le16(ICE_GNSS_UBX_DATA_LEN_H),
			      i2c_params, (u8 *)&data_len_b, NULL);
	if (err)
		goto requeue;

	data_len = be16_to_cpu(data_len_b);
	if (data_len == 0 || data_len == U16_MAX)
		goto requeue;

	/* The u-blox has data_len bytes for us to read */

	data_len = min_t(typeof(data_len), data_len, PAGE_SIZE);

	buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto requeue;
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
			goto free_buf;
	}

	count = gnss_insert_raw(pf->gnss_dev, buf, i);
	if (count != i)
		dev_warn(ice_pf_to_dev(pf),
			 "gnss_insert_raw ret=%d size=%d\n",
			 count, i);
	delay = ICE_GNSS_TIMER_DELAY_TIME;
free_buf:
	free_page((unsigned long)buf);
requeue:
	kthread_queue_delayed_work(gnss->kworker, &gnss->read_work, delay);
	if (err)
		dev_dbg(ice_pf_to_dev(pf), "GNSS failed to read err=%d\n", err);
}

/**
 * ice_gnss_struct_init - Initialize GNSS receiver
 * @pf: Board private structure
 *
 * Initialize GNSS structures and workers.
 *
 * Return:
 * * pointer to initialized gnss_serial struct - success
 * * NULL - error
 */
static struct gnss_serial *ice_gnss_struct_init(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct kthread_worker *kworker;
	struct gnss_serial *gnss;

	gnss = kzalloc(sizeof(*gnss), GFP_KERNEL);
	if (!gnss)
		return NULL;

	gnss->back = pf;
	pf->gnss_serial = gnss;

	kthread_init_delayed_work(&gnss->read_work, ice_gnss_read);
	kworker = kthread_create_worker(0, "ice-gnss-%s", dev_name(dev));
	if (IS_ERR(kworker)) {
		kfree(gnss);
		return NULL;
	}

	gnss->kworker = kworker;

	return gnss;
}

/**
 * ice_gnss_open - Open GNSS device
 * @gdev: pointer to the gnss device struct
 *
 * Open GNSS device and start filling the read buffer for consumer.
 *
 * Return:
 * * 0 - success
 * * negative - error code
 */
static int ice_gnss_open(struct gnss_device *gdev)
{
	struct ice_pf *pf = gnss_get_drvdata(gdev);
	struct gnss_serial *gnss;

	if (!pf)
		return -EFAULT;

	if (!test_bit(ICE_FLAG_GNSS, pf->flags))
		return -EFAULT;

	gnss = pf->gnss_serial;
	if (!gnss)
		return -ENODEV;

	kthread_queue_delayed_work(gnss->kworker, &gnss->read_work, 0);

	return 0;
}

/**
 * ice_gnss_close - Close GNSS device
 * @gdev: pointer to the gnss device struct
 *
 * Close GNSS device, cancel worker, stop filling the read buffer.
 */
static void ice_gnss_close(struct gnss_device *gdev)
{
	struct ice_pf *pf = gnss_get_drvdata(gdev);
	struct gnss_serial *gnss;

	if (!pf)
		return;

	gnss = pf->gnss_serial;
	if (!gnss)
		return;

	kthread_cancel_delayed_work_sync(&gnss->read_work);
}

/**
 * ice_gnss_write - Write to GNSS device
 * @gdev: pointer to the gnss device struct
 * @buf: pointer to the user data
 * @count: size of the buffer to be sent to the GNSS device
 *
 * Return:
 * * number of written bytes - success
 * * negative - error code
 */
static int
ice_gnss_write(struct gnss_device *gdev, const unsigned char *buf,
	       size_t count)
{
	struct ice_pf *pf = gnss_get_drvdata(gdev);
	struct gnss_serial *gnss;

	/* We cannot write a single byte using our I2C implementation. */
	if (count <= 1 || count > ICE_GNSS_TTY_WRITE_BUF)
		return -EINVAL;

	if (!pf)
		return -EFAULT;

	if (!test_bit(ICE_FLAG_GNSS, pf->flags))
		return -EFAULT;

	gnss = pf->gnss_serial;
	if (!gnss)
		return -ENODEV;

	return ice_gnss_do_write(pf, buf, count);
}

static const struct gnss_operations ice_gnss_ops = {
	.open = ice_gnss_open,
	.close = ice_gnss_close,
	.write_raw = ice_gnss_write,
};

/**
 * ice_gnss_register - Register GNSS receiver
 * @pf: Board private structure
 *
 * Allocate and register GNSS receiver in the Linux GNSS subsystem.
 *
 * Return:
 * * 0 - success
 * * negative - error code
 */
static int ice_gnss_register(struct ice_pf *pf)
{
	struct gnss_device *gdev;
	int ret;

	gdev = gnss_allocate_device(ice_pf_to_dev(pf));
	if (!gdev) {
		dev_err(ice_pf_to_dev(pf),
			"gnss_allocate_device returns NULL\n");
		return -ENOMEM;
	}

	gdev->ops = &ice_gnss_ops;
	gdev->type = GNSS_TYPE_UBX;
	gnss_set_drvdata(gdev, pf);
	ret = gnss_register_device(gdev);
	if (ret) {
		dev_err(ice_pf_to_dev(pf), "gnss_register_device err=%d\n",
			ret);
		gnss_put_device(gdev);
	} else {
		pf->gnss_dev = gdev;
	}

	return ret;
}

/**
 * ice_gnss_deregister - Deregister GNSS receiver
 * @pf: Board private structure
 *
 * Deregister GNSS receiver from the Linux GNSS subsystem,
 * release its resources.
 */
static void ice_gnss_deregister(struct ice_pf *pf)
{
	if (pf->gnss_dev) {
		gnss_deregister_device(pf->gnss_dev);
		gnss_put_device(pf->gnss_dev);
		pf->gnss_dev = NULL;
	}
}

/**
 * ice_gnss_init - Initialize GNSS support
 * @pf: Board private structure
 */
void ice_gnss_init(struct ice_pf *pf)
{
	int ret;

	pf->gnss_serial = ice_gnss_struct_init(pf);
	if (!pf->gnss_serial)
		return;

	ret = ice_gnss_register(pf);
	if (!ret) {
		set_bit(ICE_FLAG_GNSS, pf->flags);
		dev_info(ice_pf_to_dev(pf), "GNSS init successful\n");
	} else {
		ice_gnss_exit(pf);
		dev_err(ice_pf_to_dev(pf), "GNSS init failure\n");
	}
}

/**
 * ice_gnss_exit - Disable GNSS TTY support
 * @pf: Board private structure
 */
void ice_gnss_exit(struct ice_pf *pf)
{
	ice_gnss_deregister(pf);
	clear_bit(ICE_FLAG_GNSS, pf->flags);

	if (pf->gnss_serial) {
		struct gnss_serial *gnss = pf->gnss_serial;

		kthread_cancel_delayed_work_sync(&gnss->read_work);
		kthread_destroy_worker(gnss->kworker);
		gnss->kworker = NULL;

		kfree(gnss);
		pf->gnss_serial = NULL;
	}
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
