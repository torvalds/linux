// SPDX-License-Identifier: GPL-2.0
/**
 * Rockchip rk1608 driver
 *
 * Copyright (C) 2017-2018 Rockchip Electronics Co., Ltd.
 *
 */
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>
#include <linux/rk-preisp.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include "rk1608.h"

#define ENABLE_DMA_BUFFER 1
#define SPI_BUFSIZ  max(32, SMP_CACHE_BYTES)

/**
 * Rk1608 is used as the Pre-ISP to link on Soc, which mainly has two
 * functions. One is to download the firmware of RK1608, and the other
 * is to match the extra sensor such as camera and enable sensor by
 * calling sensor's s_power.
 *	|-----------------------|
 *	|     Sensor Camera     |
 *	|-----------------------|
 *	|-----------||----------|
 *	|-----------||----------|
 *	|-----------\/----------|
 *	|     Pre-ISP RK1608    |
 *	|-----------------------|
 *	|-----------||----------|
 *	|-----------||----------|
 *	|-----------\/----------|
 *	|      Rockchip Soc     |
 *	|-----------------------|
 * Data Transfer As shown above. In RK1608, the data received from the
 * extra sensor,and it is passed to the Soc through ISP.
 */

static inline struct rk1608_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rk1608_state, sd);
}

/**
 * rk1608_operation_query - RK1608 last operation state query
 *
 * @spi: device from which data will be read
 * @state: last operation state [out]
 * Context: can sleep
 *
 * It returns zero on success, else a negative error code.
 */
static int rk1608_operation_query(struct spi_device *spi, s32 *state)
{
	s32 query_cmd = RK1608_CMD_QUERY;
	struct spi_transfer query_cmd_packet = {
		.tx_buf = &query_cmd,
		.len    = sizeof(query_cmd),
	};
	struct spi_transfer state_packet = {
		.rx_buf = state,
		.len    = sizeof(*state),
	};
	struct spi_message  m;

	spi_message_init(&m);
	spi_message_add_tail(&query_cmd_packet, &m);
	spi_message_add_tail(&state_packet, &m);
	spi_sync(spi, &m);

	return ((*state & RK1608_STATE_ID_MASK) == RK1608_STATE_ID) ? 0 : -1;
}

static int rk1608_write(struct spi_device *spi,
			s32 addr, const s32 *data, size_t data_len)
{
	u8 *local_buf = NULL;
	int ret = 0;
	s32 write_cmd = RK1608_CMD_WRITE;

	struct spi_transfer write_cmd_packet = {
		.tx_buf = &write_cmd,
		.len    = sizeof(write_cmd),
	};
	struct spi_transfer addr_packet = {
		.tx_buf = &addr,
		.len    = sizeof(addr),
	};
	struct spi_transfer data_packet = {
		.tx_buf = data,
		.len    = data_len,
	};
	struct spi_message m;
	u32 trans_len;

#if ENABLE_DMA_BUFFER
	trans_len = data_len + sizeof(write_cmd) + sizeof(addr);
	if (trans_len > (size_t)SPI_BUFSIZ) {
		local_buf = kmalloc(max_t(size_t, SPI_BUFSIZ, data_len),
				    GFP_KERNEL | GFP_DMA);
		if (!local_buf)
			return -ENOMEM;
		memcpy(local_buf, data, data_len);
		data_packet.tx_buf = local_buf;
	}
#endif

	spi_message_init(&m);
	spi_message_add_tail(&write_cmd_packet, &m);
	spi_message_add_tail(&addr_packet, &m);
	spi_message_add_tail(&data_packet, &m);
	ret = spi_sync(spi, &m);

	kfree(local_buf);

	return ret;
}

/**
 * rk1608_safe_write - RK1608 synchronous write with state check
 *
 * @spi: spi device
 * @addr: resource address
 * @data: data buffer
 * @data_len: data buffer size, in bytes
 * Context: can sleep
 *
 * It returns zero on success, else a negative error code.
 */
static int _rk1608_safe_write(struct spi_device *spi, s32 addr,
			      const s32 *data, size_t data_len)
{
	int ret = 0;
	s32 state = 0;
	s32 try = 0;

	do {
		ret = rk1608_write(spi, addr, data, data_len);
		if (ret == 0)
			ret = rk1608_operation_query(spi, &state);

		if (ret != 0)
			return ret;
		else if ((state & RK1608_STATE_MASK) == 0)
			break;

		if (try++ == RK1608_OP_TRY_MAX)
			break;
		udelay(RK1608_OP_TRY_DELAY);
	} while (1);

	return (state & RK1608_STATE_MASK);
}

static int rk1608_safe_write(struct spi_device *spi,
			     s32 addr, const s32 *data, size_t data_len)
{
	int ret = 0;
	size_t max_op_size = (size_t)RK1608_MAX_OP_BYTES;

	while (data_len > 0) {
		size_t slen = ALIGN(MIN(data_len, max_op_size), 4);

		ret = _rk1608_safe_write(spi, addr, data, slen);
		if (ret == -ENOMEM) {
			max_op_size = slen / 2;
			continue;
		}

		if (ret)
			break;
		data_len = data_len - slen;
		data = (s32 *)((s8 *)data + slen);
		addr += slen;
	}

	return ret;
}

static void rk1608_hw_init(struct spi_device *spi)
{
	s32 write_data = SPI0_PLL_SEL_APLL;

	/* modify rk1608 spi slave clk to 300M */
	rk1608_safe_write(spi, CRUPMU_CLKSEL14_CON, &write_data, 4);

	/* modify rk1608 spi io driver strength to 8mA */
	write_data = BIT7_6_SEL_8MA;
	rk1608_safe_write(spi, PMUGRF_GPIO1A_E, &write_data, 4);
	write_data = BIT1_0_SEL_8MA;
	rk1608_safe_write(spi, PMUGRF_GPIO1B_E, &write_data, 4);
}

/**
 * rk1608_read - RK1608 synchronous read
 *
 * @spi: spi device
 * @addr: resource address
 * @data: data buffer [out]
 * @data_len: data buffer size, in bytes
 * Context: can sleep
 *
 * It returns zero on success, else a negative error code.
 */
static int rk1608_read(struct spi_device *spi,
		       s32 addr, s32 *data, size_t data_len)
{
	u8 *local_buf = NULL;
	int ret;
	s32 real_len = MIN(data_len, RK1608_MAX_OP_BYTES);
	s32 read_cmd = RK1608_CMD_READ | (real_len << 14 &
					   RK1608_STATE_ID_MASK);
	s32 read_begin_cmd = RK1608_CMD_READ_BEGIN;
	s32 dummy = 0;
	struct spi_transfer read_cmd_packet = {
		.tx_buf = &read_cmd,
		.len    = sizeof(read_cmd),
	};
	struct spi_transfer addr_packet = {
		.tx_buf = &addr,
		.len    = sizeof(addr),
	};
	struct spi_transfer read_dummy_packet = {
		.tx_buf = &dummy,
		.len    = sizeof(dummy),
	};
	struct spi_transfer read_begin_cmd_packet = {
		.tx_buf = &read_begin_cmd,
		.len    = sizeof(read_begin_cmd),
	};
	struct spi_transfer data_packet = {
		.rx_buf = data,
		.len    = data_len,
	};
	struct spi_message m;
	u32 trans_len;

#if ENABLE_DMA_BUFFER
	trans_len = data_len + sizeof(read_cmd) + sizeof(addr) +
		    sizeof(dummy) + sizeof(read_begin_cmd);
	if (trans_len > (size_t)SPI_BUFSIZ) {
		local_buf = kmalloc(max_t(size_t, SPI_BUFSIZ, data_len),
				    GFP_KERNEL | GFP_DMA);
		if (!local_buf)
			return -ENOMEM;
		data_packet.rx_buf = local_buf;
	}
#endif

	spi_message_init(&m);
	spi_message_add_tail(&read_cmd_packet, &m);
	spi_message_add_tail(&addr_packet, &m);
	spi_message_add_tail(&read_dummy_packet, &m);
	spi_message_add_tail(&read_begin_cmd_packet, &m);
	spi_message_add_tail(&data_packet, &m);
	ret = spi_sync(spi, &m);

	if (local_buf) {
		memcpy(data, local_buf, data_len);
		kfree(local_buf);
	}

	return ret;
}

/**
 * rk1608_safe_read - RK1608 synchronous read with state check
 *
 * @spi: spi device
 * @addr: resource address
 * @data: data buffer [out]
 * @data_len: data buffer size, in bytes
 * Context: can sleep
 *
 * It returns zero on success, else a negative error code.
 */
static int rk1608_safe_read(struct spi_device *spi,
			    s32 addr, s32 *data, size_t data_len)
{
	s32 state = 0;
	s32 retry = 0;

	do {
		rk1608_read(spi, addr, data, data_len);
		if (rk1608_operation_query(spi, &state) != 0)
			return -EPERM;
		if ((state & RK1608_STATE_MASK) == 0)
			break;
		udelay(RK1608_OP_TRY_DELAY);
	} while (retry++ != RK1608_OP_TRY_MAX);

	return -(state & RK1608_STATE_MASK);
}

static int rk1608_read_wait(struct spi_device *spi,
			    const struct rk1608_section *sec)
{
	s32 value = 0;
	int retry = 0;
	int ret = 0;

	do {
		ret = rk1608_safe_read(spi, sec->wait_addr, &value, 4);
		if (!ret && value == sec->wait_value)
			break;

		if (retry++ == sec->timeout) {
			ret = -EPERM;
			dev_err(&spi->dev, "Read 0x%x is %x != %x timeout\n",
				sec->wait_addr, value, sec->wait_value);
			break;
		}
		msleep(sec->wait_time);
	} while (1);

	return ret;
}

static int rk1608_boot_request(struct spi_device *spi,
			       const struct rk1608_section *sec)
{
	struct rk1608_boot_req boot_req;
	int retry = 0;
	int ret = 0;

	/* Send boot request to rk1608 for ddr init */
	boot_req.flag = sec->flag;
	boot_req.load_addr = sec->load_addr;
	boot_req.boot_len = sec->size;
	boot_req.status = 1;
	boot_req.cmd = 2;

	ret = rk1608_safe_write(spi, BOOT_REQUEST_ADDR,
				(s32 *)&boot_req, sizeof(boot_req));
	if (ret)
		return ret;

	if (sec->flag & BOOT_FLAG_READ_WAIT) {
		/* Waitting for rk1608 init ddr done */
		do {
			ret = rk1608_safe_read(spi, BOOT_REQUEST_ADDR,
					       (s32 *)&boot_req,
					       sizeof(boot_req));

			if (!ret && boot_req.status == 0)
				break;

			if (retry++ == sec->timeout) {
				ret = -EPERM;
				dev_err(&spi->dev, "Boot request timeout\n");
				break;
			}
			msleep(sec->wait_time);
		} while (1);
	}

	return ret;
}

static int rk1608_download_section(struct spi_device *spi, const u8 *data,
				   const struct rk1608_section *sec)
{
	int ret = 0;

	dev_info(&spi->dev, "offset:%x,size:%x,addr:%x,wait_time:%x",
		 sec->offset, sec->size, sec->load_addr, sec->wait_time);

	dev_info(&spi->dev, "timeout:%x,crc:%x,flag:%x,type:%x",
		 sec->timeout, sec->crc_16, sec->flag, sec->type);

	if (sec->size > 0) {
		ret = rk1608_safe_write(spi, sec->load_addr,
					(s32 *)(data + sec->offset),
					sec->size);
		if (ret) {
			dev_err(&spi->dev, "RK1608 safe write err =%d\n", ret);
			return ret;
		}
	}

	if (sec->flag & BOOT_FLAG_BOOT_REQUEST)
		ret = rk1608_boot_request(spi, sec);
	else if (sec->flag & BOOT_FLAG_READ_WAIT)
		ret = rk1608_read_wait(spi, sec);

	return ret;
}

/**
 * rk1608_download_fw: - rk1608 firmware download through spi
 *
 * @spi: spi device
 * @fw_name: name of firmware file, NULL for default firmware name
 * Context: can sleep
 *
 * It returns zero on success, else a negative error code.
 */
static int rk1608_download_fw(struct spi_device *spi, const char *fw_name)
{
	const struct rk1608_header *head;
	const struct firmware *fw;
	u32 i = 0;
	int ret = 0;

	if (!fw_name)
		fw_name = RK1608_FW_NAME;

	dev_info(&spi->dev, "Before request firmware");
	ret = request_firmware(&fw, fw_name, &spi->dev);
	if (ret) {
		dev_err(&spi->dev, "Request firmware %s failed!", fw_name);
		return ret;
	}

	head = (const struct rk1608_header *)fw->data;

	dev_info(&spi->dev, "Request firmware %s (version:%s) success!",
		 fw_name, head->version);

	for (i = 0; i < head->section_count; i++) {
		ret = rk1608_download_section(spi, fw->data,
					      &head->sections[i]);
		if (ret)
			break;
	}

	release_firmware(fw);
	return ret;
}

static int rk1608_lsb_w32(struct spi_device *spi, s32 addr, s32 data)
{
	s32 write_cmd = RK1608_CMD_WRITE;
	struct spi_transfer write_cmd_packet = {
		.tx_buf = &write_cmd,
		.len    = sizeof(write_cmd),
	};
	struct spi_transfer addr_packet = {
		.tx_buf = &addr,
		.len    = sizeof(addr),
	};
	struct spi_transfer data_packet = {
		.tx_buf = &data,
		.len    = sizeof(data),
	};
	struct spi_message  m;

	write_cmd = MSB2LSB32(write_cmd);
	addr = MSB2LSB32(addr);
	data = MSB2LSB32(data);
	spi_message_init(&m);
	spi_message_add_tail(&write_cmd_packet, &m);
	spi_message_add_tail(&addr_packet, &m);
	spi_message_add_tail(&data_packet, &m);

	return spi_sync(spi, &m);
}

static int rk1608_msg_init_sensor(struct rk1608_state *pdata,
				  struct msg_init *msg, int id)
{
	struct i2c_client *client;

	msg->msg_head.size = sizeof(struct msg_init);
	msg->msg_head.type = id_msg_init_sensor_t;
	msg->msg_head.id.camera_id = id;
	msg->msg_head.mux.sync = 1;
	msg->in_mipi_phy = pdata->dphy[id]->in_mipi;
	msg->out_mipi_phy = pdata->dphy[id]->out_mipi;
	msg->mipi_lane = pdata->dphy[id]->mipi_lane;
	msg->bayer = 0;
	memcpy(msg->sensor_name, pdata->sensor[id]->name,
	       sizeof(msg->sensor_name));

	client = v4l2_get_subdevdata(pdata->sensor[id]);
	msg->i2c_slave_addr = (client->addr) << 1;
	msg->i2c_bus = 1;

	return rk1608_send_msg_to_dsp(pdata, &msg->msg_head);
}

static int rk1608_msg_set_input_size(struct rk1608_state *pdata,
				     struct msg_in_size *msg, int id)
{
	msg->msg_head.size = sizeof(struct msg_in_size) / sizeof(int);
	msg->msg_head.type = id_msg_set_input_size_t;
	msg->msg_head.id.camera_id = id;
	msg->msg_head.mux.sync = 1;

	msg->channel[0].data_id = 0x6b;
	msg->channel[0].decode_format = 0x2b;
	msg->channel[0].flag = 1;
	msg->channel[0].width = pdata->dphy[id]->mf.width;
	msg->channel[0].height = pdata->dphy[id]->mf.height * 2;

	msg->channel[1].data_id = 0x2b;
	msg->channel[1].decode_format = 0x2b;
	msg->channel[1].flag = 2;
	msg->channel[1].width = pdata->dphy[id]->mf.width;
	msg->channel[1].height = pdata->dphy[id]->mf.height;

	return rk1608_send_msg_to_dsp(pdata, &msg->msg_head);
}

static int rk1608_msg_set_output_size(struct rk1608_state *pdata,
				      struct msg_set_output_size *msg, int id)
{
	msg->head.msg_head.size = sizeof(struct msg_set_output_size) / sizeof(int);
	msg->head.msg_head.type = id_msg_set_output_size_t;
	msg->head.msg_head.id.camera_id = id;
	msg->head.msg_head.mux.sync = 1;
	msg->head.width = pdata->dphy[id]->mf.width;
	msg->head.height = pdata->dphy[id]->mf.height;
	msg->head.mipi_clk = pdata->dphy[id]->link_freqs;
	msg->head.line_length_pclk = pdata->dphy[id]->htotal;
	msg->head.frame_length_lines = pdata->dphy[id]->vtotal;
	msg->head.mipi_lane = pdata->dphy[id]->mipi_lane;

	msg->channel[0].data_id = 0x2b;
	msg->channel[0].decode_format = 0x2b;
	msg->channel[0].flag = 1;
	msg->channel[0].width = pdata->dphy[id]->mf.width;
	msg->channel[0].height = pdata->dphy[id]->mf.height;

	msg->channel[1].data_id = 0x30;
	msg->channel[1].decode_format = 0x2b;
	msg->channel[1].flag = 0;
	msg->channel[1].width = pdata->dphy[id]->mf.width;
	msg->channel[1].height = 2;

	return rk1608_send_msg_to_dsp(pdata, &msg->head.msg_head);
}

static int rk1608_msg_set_stream_in_on(struct rk1608_state *pdata,
				       struct msg *msg, int id)
{
	msg->size = sizeof(struct msg);
	msg->type = id_msg_set_stream_in_on_t;
	msg->id.camera_id = id;
	msg->mux.sync = 1;

	return rk1608_send_msg_to_dsp(pdata, msg);
}

static int rk1608_msg_set_stream_in_off(struct rk1608_state *pdata,
					struct msg *msg, int id)
{
	msg->size = sizeof(struct msg);
	msg->type = id_msg_set_stream_in_off_t;
	msg->id.camera_id = id;
	msg->mux.sync = 1;

	return rk1608_send_msg_to_dsp(pdata, msg);
}

static int rk1608_msg_set_stream_out_on(struct rk1608_state *pdata,
					struct msg *msg, int id)
{
	msg->size = sizeof(struct msg);
	msg->type = id_msg_set_stream_out_on_t;
	msg->id.camera_id = id;
	msg->mux.sync = 1;

	return rk1608_send_msg_to_dsp(pdata, msg);
}

static int rk1608_msg_set_stream_out_off(struct rk1608_state *pdata,
					 struct msg *msg, int id)
{
	msg->size = sizeof(struct msg);
	msg->type = id_msg_set_stream_out_off_t;
	msg->id.camera_id = id;
	msg->mux.sync = 1;

	return rk1608_send_msg_to_dsp(pdata, msg);
}

static int rk1608_set_log_level(struct rk1608_state *pdata,
				struct msg *msg, int level)
{
	msg->size = sizeof(struct msg);
	msg->type = id_msg_set_log_level_t;
	msg->mux.log_level = level;

	return rk1608_send_msg_to_dsp(pdata, msg);
}

static int rk1608_send_meta_hdrae(struct rk1608_state *pdata,
				  struct preisp_hdrae_exp_s *hdrae_exp)
{
	int ret = 0;
	unsigned long flags;
	struct msg_set_sensor_info_s *msg;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->msg_head.size = sizeof(*msg) / 4;
	msg->msg_head.type = id_msg_set_sensor_info_t;
	msg->msg_head.id.camera_id = 0;
	msg->set_exp_cnt = pdata->set_exp_cnt++;

	spin_lock_irqsave(&pdata->hdrae_lock, flags);
	msg->r_gain = pdata->hdrae_para.r_gain;
	msg->b_gain = pdata->hdrae_para.b_gain;
	msg->gr_gain = pdata->hdrae_para.gr_gain;
	msg->gb_gain = pdata->hdrae_para.gb_gain;
	memcpy(msg->lsc_table, pdata->hdrae_para.lsc_table,
	       sizeof(msg->lsc_table));
	spin_unlock_irqrestore(&pdata->hdrae_lock, flags);

	/* dsp hdrae */
	msg->dsp_hdrae.bayer_mode = BAYER_MODE_BGGR;
	msg->dsp_hdrae.grid_mode = AE_MEASURE_GRID_15X15;
	memset(&msg->dsp_hdrae.weight[0], 3, ISP_DSP_HDRAE_MAXGRIDITEMS);
	msg->dsp_hdrae.hist_mode = AE_HISTSTATICMODE_Y;
	msg->dsp_hdrae.ycoeff.rcoef = 1;
	msg->dsp_hdrae.ycoeff.gcoef = 1;
	msg->dsp_hdrae.ycoeff.bcoef = 1;
	msg->dsp_hdrae.ycoeff.offset = 0;
	msg->dsp_hdrae.imgbits = 0;
	msg->dsp_hdrae.width = 1920;
	msg->dsp_hdrae.height = 1080;
	msg->dsp_hdrae.frames = 2;

	msg->reg_exp_time[0] = hdrae_exp->long_exp_reg;
	msg->reg_exp_gain[0] = hdrae_exp->long_gain_reg;
	msg->reg_exp_time[1] = hdrae_exp->middle_exp_reg;
	msg->reg_exp_gain[1] = hdrae_exp->middle_gain_reg;
	msg->reg_exp_time[2] = hdrae_exp->short_exp_reg;
	msg->reg_exp_gain[2] = hdrae_exp->short_gain_reg;

	msg->exp_time[0] = hdrae_exp->long_exp_val;
	msg->exp_gain[0] = hdrae_exp->long_gain_val;
	msg->exp_time[1] = hdrae_exp->middle_exp_val;
	msg->exp_gain[1] = hdrae_exp->middle_gain_val;
	msg->exp_time[2] = hdrae_exp->short_exp_val;
	msg->exp_gain[2] = hdrae_exp->short_gain_val;

	ret = rk1608_send_msg_to_dsp(pdata, &msg->msg_head);
	kfree(msg);

	return ret;
}

static int rk1608_init_sensor(struct rk1608_state *pdata, int id)
{
	struct msg *msg = NULL;
	struct msg_init *msg_init = NULL;
	struct msg_in_size *msg_in_size = NULL;
	struct msg_set_output_size *msg_out_size = NULL;
	int ret = 0;

	if (!pdata->sensor[id]) {
		dev_err(pdata->dev, "Did not find a sensor[%d]!\n", id);
		return -EINVAL;
	}

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto err;
	}
	msg_init = kzalloc(sizeof(*msg_init), GFP_KERNEL);
	if (!msg_init) {
		ret = -ENOMEM;
		goto err;
	}
	msg_in_size = kzalloc(sizeof(*msg_in_size), GFP_KERNEL);
	if (!msg_in_size) {
		ret = -ENOMEM;
		goto err;
	}
	msg_out_size = kzalloc(sizeof(*msg_out_size), GFP_KERNEL);
	if (!msg_out_size) {
		ret = -ENOMEM;
		goto err;
	}

	ret = rk1608_msg_init_sensor(pdata, msg_init, id);
	ret |= rk1608_msg_set_input_size(pdata, msg_in_size, id);
	ret |= rk1608_msg_set_output_size(pdata, msg_out_size, id);
	ret |= rk1608_msg_set_stream_in_on(pdata, msg, id);
	ret |= rk1608_msg_set_stream_out_on(pdata, msg, id);

err:
	kfree(msg_init);
	kfree(msg_in_size);
	kfree(msg_out_size);
	kfree(msg);

	return ret;
}

static int rk1608_deinit(struct rk1608_state *pdata, int id)
{
	struct msg *msg;
	int ret = 0;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	ret = rk1608_msg_set_stream_out_off(pdata, msg, id);
	ret |= rk1608_msg_set_stream_in_off(pdata, msg, id);
	kfree(msg);

	return ret;
}

static void rk1608_cs_set_value(struct rk1608_state *pdata, int value)
{
	s8 null_cmd = 0;

	struct spi_transfer null_cmd_packet = {
		.tx_buf = &null_cmd,
		.len    = sizeof(null_cmd),
		.cs_change = !value,
	};
	struct spi_message  m;

	spi_message_init(&m);
	spi_message_add_tail(&null_cmd_packet, &m);
	spi_sync(pdata->spi, &m);
}

static void rk1608_set_spi_speed(struct rk1608_state *pdata, u32 hz)
{
	pdata->spi->max_speed_hz = hz;
}

static int rk1608_power_on(struct rk1608_state *pdata)
{
	struct spi_device *spi = pdata->spi;
	int ret = 0;

	/* Request rk1608 enter slave mode */
	rk1608_cs_set_value(pdata, 0);
	if (pdata->wakeup_gpio)
		gpiod_direction_output(pdata->wakeup_gpio, 1);

	usleep_range(3000, 3500);
	if (pdata->reset_gpio) {
		gpiod_direction_output(pdata->reset_gpio, 1);
		gpiod_direction_output(pdata->reset_gpio, 0);
		gpiod_direction_output(pdata->reset_gpio, 1);
	}

	/* After Reset pull-up, CSn should keep low for 2ms+ */
	usleep_range(3000, 3500);
	rk1608_cs_set_value(pdata, 1);
	rk1608_lsb_w32(spi, SPI_ENR, 0);
	rk1608_lsb_w32(spi, SPI_CTRL0,
		       OPM_SLAVE_MODE | RSD_SEL_2CYC | DFS_SEL_16BIT);
	rk1608_hw_init(pdata->spi);
	rk1608_set_spi_speed(pdata, pdata->max_speed_hz);

	/* Download system firmware */
	ret = rk1608_download_fw(pdata->spi, pdata->firm_name);
	if (ret)
		dev_err(pdata->dev, "Download firmware failed!");
	else
		dev_info(pdata->dev, "Download firmware success!");

	if (pdata->irq > 0)
		enable_irq(pdata->irq);

	return 0;
}

static int rk1608_power_off(struct rk1608_state *pdata)
{
	/* Request rk1608 enter slave mode */
	if (pdata->irq > 0)
		disable_irq(pdata->irq);
	if (pdata->wakeup_gpio)
		gpiod_direction_output(pdata->wakeup_gpio, 0);
	if (pdata->reset_gpio)
		gpiod_direction_output(pdata->reset_gpio, 0);
	rk1608_cs_set_value(pdata, 0);

	return 0;
}

static int rk1608_sensor_power(struct v4l2_subdev *sd, int on)
{
	int ret = 0;
	struct msg *msg = NULL;
	struct rk1608_state *pdata = to_state(sd);

	mutex_lock(&pdata->lock);
	if (on) {
		v4l2_subdev_call(pdata->sensor[sd->grp_id], core, s_power, on);
		if (!pdata->power_count)
			rk1608_power_on(pdata);

		msg = kzalloc(sizeof(*msg), GFP_KERNEL);
		if (!msg) {
			mutex_unlock(&pdata->lock);
			return -ENOMEM;
		}

		ret = rk1608_set_log_level(pdata, msg, 2);
		if (!msg)
			kfree(msg);
	} else if (!on && pdata->power_count == 1) {
		v4l2_subdev_call(pdata->sensor[sd->grp_id], core, s_power, on);
		rk1608_power_off(pdata);
	}

	/* Update the power count. */
	pdata->power_count += on ? 1 : -1;
	WARN_ON(pdata->power_count < 0);
	mutex_unlock(&pdata->lock);

	return ret;
}

static int rk1608_stream_on(struct rk1608_state *pdata)
{
	int id = 0, cnt = 0, ret = 0;

	mutex_lock(&pdata->lock);
	id = pdata->sd.grp_id;
	pdata->sensor_cnt = 0;
	pdata->set_exp_cnt = 1;
	ret = rk1608_init_sensor(pdata, id);
	if (ret) {
		dev_err(pdata->dev, "Init rk1608[%d] is failed!",
			pdata->sd.grp_id);
		mutex_unlock(&pdata->lock);
		return ret;
	}

	/* Waiting for the sensor to be ready */
	while (pdata->sensor_cnt < pdata->sensor_nums[id]) {
		/* TIMEOUT 10s break */
		if (cnt++ > SENSOR_TIMEOUT) {
			dev_err(pdata->dev,
				"Sensor%d is ready to timeout!",
				pdata->sensor_cnt);
			break;
		}
		usleep_range(10000, 11000);
	}

	if (pdata->sensor_nums[id]) {
		if (pdata->sensor_cnt == pdata->sensor_nums[id])
			dev_info(pdata->dev, "Sensor(num %d) is ready!",
				 pdata->sensor_cnt);
	} else {
		dev_warn(pdata->dev, "No sensor is found!");
	}
	mutex_unlock(&pdata->lock);

	pdata->hdrae_para.r_gain = 0x0100;
	pdata->hdrae_para.b_gain = 0x0100;
	pdata->hdrae_para.gr_gain = 0x0100;
	pdata->hdrae_para.gb_gain = 0x0100;
	for (cnt = 0; cnt < PREISP_LSCTBL_SIZE; cnt++)
		pdata->hdrae_para.lsc_table[cnt] = 0x0400;
	memset(&pdata->hdrae_exp, 0, sizeof(pdata->hdrae_exp));
	return 0;
}

static int rk1608_stream_off(struct rk1608_state *pdata)
{
	mutex_lock(&pdata->sensor_lock);
	pdata->sensor_cnt = 0;
	mutex_unlock(&pdata->sensor_lock);
	rk1608_deinit(pdata, pdata->sd.grp_id);

	return 0;
}

static int rk1608_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret;
	struct rk1608_state *pdata = to_state(sd);

	if (enable)
		ret = rk1608_stream_on(pdata);
	else
		ret = rk1608_stream_off(pdata);

	v4l2_subdev_call(pdata->sensor[sd->grp_id], video, s_stream, enable);

	return ret;
}

static int rk1608_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct rk1608_state *pdata = to_state(sd);

	v4l2_subdev_call(pdata->sensor[sd->grp_id],
			 video,
			 g_frame_interval,
			 fi);

	return 0;
}

static long rk1608_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct rk1608_state *pdata = to_state(sd);
	struct preisp_hdrae_para_s *hdrae_para;
	struct preisp_hdrae_exp_s *hdrae_exp;

	switch (cmd) {
	case PREISP_CMD_SAVE_HDRAE_PARAM:
		hdrae_para = arg;
		spin_lock(&pdata->hdrae_lock);
		pdata->hdrae_para = *hdrae_para;
		spin_unlock(&pdata->hdrae_lock);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		hdrae_exp = arg;
		if (pdata->hdrae_exp.long_exp_reg == hdrae_exp->long_exp_reg &&
		    pdata->hdrae_exp.long_gain_reg == hdrae_exp->long_gain_reg &&
		    pdata->hdrae_exp.short_exp_reg == hdrae_exp->short_exp_reg &&
		    pdata->hdrae_exp.short_gain_reg == hdrae_exp->short_gain_reg)
			break;

		pdata->hdrae_exp = *hdrae_exp;

		/* hdr exposure start */
		if (pdata->aesync_gpio)
			gpiod_direction_output(pdata->aesync_gpio, 1);

		v4l2_subdev_call(pdata->sensor[sd->grp_id], core, ioctl,
				 cmd, hdrae_exp);

		if (pdata->aesync_gpio)
			gpiod_direction_output(pdata->aesync_gpio, 0);

		rk1608_send_meta_hdrae(pdata, hdrae_exp);
		break;
	default:
		return -ENOTTY;
	}

	return 0;
}

static int rk1608_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_ctrl *remote_ctrl;
	struct rk1608_state *pdata =
		container_of(ctrl->handler,
			     struct rk1608_state, ctrl_handler);
	int id = pdata->sd.grp_id;

	remote_ctrl = v4l2_ctrl_find(pdata->sensor[id]->ctrl_handler,
				     ctrl->id);
	if (remote_ctrl) {
		ctrl->val = v4l2_ctrl_g_ctrl(remote_ctrl);
		__v4l2_ctrl_modify_range(ctrl,
					 remote_ctrl->minimum,
					 remote_ctrl->maximum,
					 remote_ctrl->step,
					 remote_ctrl->default_value);
	}

	return 0;
}

static int rk1608_set_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;

	struct v4l2_ctrl *remote_ctrl;
	struct rk1608_state *pdata =
		container_of(ctrl->handler,
			     struct rk1608_state, ctrl_handler);
	int id = pdata->sd.grp_id;

	remote_ctrl = v4l2_ctrl_find(pdata->sensor[id]->ctrl_handler,
				     ctrl->id);
	if (remote_ctrl)
		ret = v4l2_ctrl_s_ctrl(remote_ctrl, ctrl->val);

	return ret;
}

static const struct v4l2_ctrl_ops rk1608_ctrl_ops = {
	.g_volatile_ctrl = rk1608_g_volatile_ctrl,
	.s_ctrl = rk1608_set_ctrl,
};

static int rk1608_initialize_controls(struct rk1608_state *rk1608)
{
	int ret;
	struct v4l2_ctrl_handler *handler;
	unsigned long flags = V4L2_CTRL_FLAG_VOLATILE |
			      V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;

	handler = &rk1608->ctrl_handler;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;

	rk1608->hblank = v4l2_ctrl_new_std(handler,
					   &rk1608_ctrl_ops,
					   V4L2_CID_HBLANK,
					   0, 0x7FFFFFFF, 1, 0);
	if (rk1608->hblank)
		rk1608->hblank->flags |= flags;

	rk1608->vblank = v4l2_ctrl_new_std(handler,
					   &rk1608_ctrl_ops,
					   V4L2_CID_VBLANK,
					   0, 0x7FFFFFFF, 1, 0);
	if (rk1608->vblank)
		rk1608->vblank->flags |= flags;

	rk1608->exposure = v4l2_ctrl_new_std(handler,
					     &rk1608_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     0, 0x7FFFFFFF, 1, 0);
	if (rk1608->exposure)
		rk1608->exposure->flags |= flags;

	rk1608->gain = v4l2_ctrl_new_std(handler,
					 &rk1608_ctrl_ops,
					 V4L2_CID_ANALOGUE_GAIN,
					 0, 0x7FFFFFFF, 1, 0);
	if (rk1608->gain)
		rk1608->gain->flags |= flags;

	if (handler->error) {
		ret = handler->error;
		dev_err(rk1608->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	rk1608->sd.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static const struct v4l2_subdev_video_ops rk1608_subdev_video_ops = {
	.s_stream = rk1608_s_stream,
	.g_frame_interval = rk1608_g_frame_interval,
};

static const struct v4l2_subdev_core_ops rk1608_core_ops = {
	.s_power = rk1608_sensor_power,
	.ioctl	 = rk1608_ioctl,
};

static const struct v4l2_subdev_ops rk1608_subdev_ops = {
	.core	= &rk1608_core_ops,
	.video	= &rk1608_subdev_video_ops,
};

/**
 * rk1608_msq_read_head - read rk1608 msg queue head
 *
 * @spi: spi device
 * @addr: msg queue head addr
 * @m: msg queue pointer
 *
 * It returns zero on success, else a negative error code.
 */
static int rk1608_msq_read_head(struct spi_device *spi,
				u32 addr, struct rk1608_msg_queue *q)
{
	int err = 0;
	s32 reg;

	err = rk1608_safe_read(spi, RK1608_PMU_SYS_REG0, &reg, 4);

	if (err || ((reg & RK1608_MSG_QUEUE_OK_MASK) !=
		     RK1608_MSG_QUEUE_OK_TAG))
		return -EINVAL;

	err = rk1608_safe_read(spi, addr, (s32 *)q, sizeof(*q));

	return err;
}

/**
 * rk1608_msq_recv_msg - receive a msg from RK1608 -> AP msg queue
 *
 * @q: msg queue
 * @m: a msg pointer buf [out]
 *
 * need call rk1608_msq_recv_msg_free to free msg after msg use done
 *
 * It returns zero on success, else a negative error code.
 */
static int rk1608_msq_recv_msg(struct spi_device *spi, struct msg **m)
{
	struct rk1608_msg_queue queue;
	struct rk1608_msg_queue *q = &queue;
	u32 size = 0, msg_size = 0;
	u32 recv_addr = 0;
	u32 next_recv_addr = 0;
	int err = 0;

	*m = NULL;
	err = rk1608_msq_read_head(spi, RK1608_S_MSG_QUEUE_ADDR, q);
	if (err)
		return err;

	if (q->cur_send == q->cur_recv)
		return -EINVAL;
	/* Skip to head when size is 0 */
	err = rk1608_safe_read(spi, (s32)q->cur_recv, (s32 *)&size, 4);
	if (err)
		return err;
	if (size == 0) {
		err = rk1608_safe_read(spi, (s32)q->buf_head, (s32 *)&size, 4);
		if (err)
			return err;

		msg_size = size * sizeof(u32);
		recv_addr = q->buf_head;
		next_recv_addr = q->buf_head + msg_size;
	} else {
		msg_size = size * sizeof(u32);
		recv_addr = q->cur_recv;
		next_recv_addr = q->cur_recv + msg_size;
		if (next_recv_addr == q->buf_tail)
			next_recv_addr = q->buf_head;
	}

	if (msg_size > (q->buf_tail - q->buf_head))
		return -EPERM;

	*m = kmalloc(msg_size, GFP_KERNEL);
	err = rk1608_safe_read(spi, recv_addr, (s32 *)*m, msg_size);
	if (err == 0) {
		err = rk1608_safe_write(spi, RK1608_S_MSG_QUEUE_ADDR +
				       (u8 *)&q->cur_recv - (u8 *)q,
				       &next_recv_addr, 4);
	}
	if (err)
		kfree(*m);

	return err;
}

/**
 * rk1608_msq_tail_free_size - get msg queue tail unused buf size
 *
 * @q: msg queue
 *
 * It returns size of msg queue tail unused buf size, unit byte
 */
static u32 rk1608_msq_tail_free_size(const struct rk1608_msg_queue *q)
{
	if (q->cur_send >= q->cur_recv)
		return (q->buf_tail - q->cur_send);

	return q->cur_recv - q->cur_send;
}

/**
 * rk1608_interrupt_request - RK1608 request a dsp interrupt
 *
 * @spi: spi device
 * @interrupt_num: interrupt identification
 * Context: can sleep
 *
 * It returns zero on success, else a negative error code.
 */
static int rk1608_interrupt_request(struct spi_device *spi,
				    s32 interrupt_num)
{
	s32 write_reg1_cmd = APB_CMD_WRITE_REG1;
	struct spi_transfer write_reg1_cmd_packet = {
		.tx_buf = &write_reg1_cmd,
		.len    = sizeof(write_reg1_cmd),
	};
	struct spi_transfer reg1_packet = {
		.tx_buf = &interrupt_num,
		.len    = sizeof(interrupt_num),
	};
	struct spi_message  m;

	spi_message_init(&m);
	spi_message_add_tail(&write_reg1_cmd_packet, &m);
	spi_message_add_tail(&reg1_packet, &m);

	return spi_sync(spi, &m);
}

/**
 * dsp_msq_head_free_size - get msg queue head unused buf size
 *
 * @q: msg queue
 *
 * It returns size of msg queue head unused buf size, unit byte
 */
static u32 rk1608_msq_head_free_size(const struct rk1608_msg_queue *q)
{
	if (q->cur_send >= q->cur_recv)
		return (q->cur_recv - q->buf_head);

	return 0;
}

/*
 * rk1608_msq_send_msg - send a msg to Soc -> DSP msg queue
 *
 * @spi: spi device
 * @m: a msg to send
 *
 * It returns zero on success, else a negative error code.
 */
static int rk1608_msq_send_msg(struct spi_device *spi, struct msg *m)
{
	int err = 0;
	s32 tmp = 0;
	struct rk1608_msg_queue queue;
	struct rk1608_msg_queue *q = &queue;
	u32 msg_size = m->size * sizeof(u32);

	err = rk1608_msq_read_head(spi, RK1608_R_MSG_QUEUE_ADDR, q);
	if (err)
		return err;

	if (rk1608_msq_tail_free_size(q) > msg_size) {
		u32 next_send;

		err = rk1608_safe_write(spi, q->cur_send, (s32 *)m, msg_size);
		next_send = q->cur_send + msg_size;
		if (next_send == q->buf_tail)
			next_send = q->buf_head;
		q->cur_send = next_send;
	} else if (rk1608_msq_head_free_size(q) > msg_size) {
		/* Set size to 0 for skip to head mark */
		err = rk1608_safe_write(spi, q->cur_send, &tmp, 4);
		if (err)
			return err;
		err = rk1608_safe_write(spi, q->buf_head, (s32 *)m, msg_size);
		q->cur_send = q->buf_head + msg_size;
	} else {
		return -EPERM;
	}

	if (err)
		return err;

	err = rk1608_safe_write(spi, RK1608_R_MSG_QUEUE_ADDR +
				(u8 *)&q->cur_send - (u8 *)q, &q->cur_send, 4);
	rk1608_interrupt_request(spi, RK1608_IRQ_TYPE_MSG);

	return err;
}

static int rk1608_send_msg_to_dsp(struct rk1608_state *pdata, struct msg *m)
{
	int ret, msg_num = 0, timeout = 0;

	/* For msg sync */
	if (pdata->msg_num >= 8) {
		dev_err(pdata->dev, "MSG sync queue full\n!");
		ret = -EINVAL;
	} else if (m->mux.sync == 1) {
		mutex_lock(&pdata->send_msg_lock);
		msg_num = pdata->msg_num;
		pdata->msg_done[pdata->msg_num++] = 0;
		mutex_unlock(&pdata->send_msg_lock);
	}

	mutex_lock(&pdata->send_msg_lock);
	ret = rk1608_msq_send_msg(pdata->spi, m);
	mutex_unlock(&pdata->send_msg_lock);

	/* For msg sync */
	if (m->mux.sync == 1) {
		timeout = wait_event_timeout(pdata->msg_wait,
					     pdata->msg_done[msg_num],
					     MSG_SYNC_TIMEOUT);
		if (unlikely(timeout <= 0)) {
			dev_info(pdata->dev,
				 "MSG wait timeout %d msg_num:%d\n",
				 timeout, pdata->msg_num);
			mutex_lock(&pdata->send_msg_lock);
			pdata->msg_done[msg_num] = 0;
			mutex_unlock(&pdata->send_msg_lock);
		}
	}

	return ret;
}

static void rk1608_print_rk1608_log(struct rk1608_state *pdata,
				    struct msg *log)
{
	char *str = (char *)(log);

	str[log->size * sizeof(s32) - 1] = 0;
	str += sizeof(struct msg);
	dev_info(pdata->dev, "RK1608(%d): %s", log->id.core_id, str);
}

static void rk1608_dispatch_received_msg(struct rk1608_state *pdata,
					 struct msg *msg)
{
	if (msg->type == id_msg_set_stream_out_on_ret_t) {
		mutex_lock(&pdata->sensor_lock);
		pdata->sensor_cnt++;
		mutex_unlock(&pdata->sensor_lock);
	}

	if (msg->type == id_msg_rk1608_log_t)
		rk1608_print_rk1608_log(pdata, msg);
}

static irqreturn_t rk1608_threaded_isr(int irq, void *ctx)
{
	struct rk1608_state *pdata = ctx;
	struct msg *msg;

	while (!rk1608_msq_recv_msg(pdata->spi, &msg) && NULL != msg) {
		rk1608_dispatch_received_msg(pdata, msg);
		/* For kernel msg sync */
		if (msg->type >= id_msg_init_sensor_ret_t &&
		    msg->type <= id_msg_set_stream_out_off_ret_t) {
			dev_info(pdata->dev, "RK1608 kernel sync\n");
			mutex_lock(&pdata->send_msg_lock);
			pdata->msg_num--;
			pdata->msg_done[pdata->msg_num] = 1;
			mutex_unlock(&pdata->send_msg_lock);
			wake_up(&pdata->msg_wait);
		}
		kfree(msg);
	}

	return IRQ_HANDLED;
}

static int rk1608_parse_dt_property(struct rk1608_state *pdata)
{
	int i, ret = 0;
	struct device *dev = pdata->dev;
	struct device_node *node = dev->of_node;

	if (!node)
		return -ENODEV;

	of_property_read_u32(node, "spi-max-frequency",
			     &pdata->max_speed_hz);
	if (ret) {
		dev_err(dev, "can not get spi-max-frequency!");
		return -ENOENT;
	}

	ret = of_property_read_u32(node, "spi-min-frequency",
				   &pdata->min_speed_hz);
	if (ret) {
		dev_warn(dev, "can not get spi-min-frequency!");
		pdata->min_speed_hz = pdata->max_speed_hz / 2;
	}

	ret = of_property_read_string(node, "firmware-names",
				      &pdata->firm_name);
	if (ret) {
		dev_warn(dev, "can not get firmware-names!");
		pdata->firm_name = NULL;
	}

	pdata->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						    GPIOD_OUT_LOW);
	if (IS_ERR(pdata->reset_gpio)) {
		dev_err(dev, "can not find reset-gpio\n");
		return PTR_ERR(pdata->reset_gpio);
	}

	pdata->irq = -1;
	pdata->irq_gpio = devm_gpiod_get(dev, "irq", GPIOD_IN);
	if (IS_ERR(pdata->irq_gpio)) {
		dev_err(dev, "can not find irq-gpio\n");
		return -ENOENT;
	}

	pdata->wakeup_gpio = devm_gpiod_get_optional(dev, "wakeup",
						     GPIOD_OUT_LOW);
	if (IS_ERR(pdata->wakeup_gpio)) {
		dev_err(dev, "can not find wakeup_gpio\n");
		return PTR_ERR(pdata->wakeup_gpio);
	}

	pdata->aesync_gpio = devm_gpiod_get_optional(dev, "aesync",
						     GPIOD_OUT_LOW);
	if (IS_ERR(pdata->aesync_gpio)) {
		dev_err(dev, "can not find aesync_gpio\n");
		return PTR_ERR(pdata->aesync_gpio);
	}

	pdata->msg_num = 0;
	init_waitqueue_head(&pdata->msg_wait);
	for (i = 0; i < 8; i++)
		pdata->msg_done[i] = 0;

	return ret;
}

static int rk1608_get_remote_node_dev(struct rk1608_state *pdev)
{
	struct i2c_client *sensor_pdev = NULL;
	struct platform_device *dphydev = NULL;
	struct device *dev = pdev->dev;
	struct device_node *parent = dev->of_node;
	struct device_node *remote = NULL;
	int ret = 0, dphys = 0, sensor_nums = 0;
	int i;

	for (i = 0; i < 2; i++) {
		remote = of_graph_get_remote_node(parent, 0, i);
		if (!remote)
			continue;

		dphydev = of_find_device_by_node(remote);
		of_node_put(remote);
		if (!dphydev) {
			dev_err(dev, "Failed to get dhpy device(%s)\n",
				of_node_full_name(remote));
			continue;
		} else {
			pdev->dphy[dphys] = platform_get_drvdata(dphydev);
			if (pdev->dphy[dphys]) {
				dphydev = NULL;
				pdev->dphy[dphys]->rk1608_sd = &pdev->sd;
				pdev->sensor_nums[dphys] =
					pdev->dphy[dphys]->cam_nums;
				dphys++;
			} else {
				dev_err(dev, "Failed to get dhpy drvdata\n");
			}
		}
	}

	for (i = 0; i < 4; i++) {
		remote = of_graph_get_remote_node(parent, 1, i);
		if (!remote)
			continue;

		sensor_pdev = of_find_i2c_device_by_node(remote);
		of_node_put(remote);
		if (!sensor_pdev) {
			dev_err(dev, "Failed to get sensor device(%s)\n",
				of_node_full_name(remote));
			continue;
		} else {
			pdev->sensor[sensor_nums] =
				i2c_get_clientdata(sensor_pdev);
			if (pdev->sensor[sensor_nums]) {
				sensor_pdev = NULL;
				sensor_nums++;
			} else {
				dev_err(dev, "Failed to get sensor drvdata\n");
			}
		}
	}

	if (dphys && sensor_nums)
		dev_info(dev, "Get %d dphys, %d sensors!\n",
			 dphys, sensor_nums);
	else
		ret = -EINVAL;

	return ret;
}

static int rk1608_probe(struct spi_device *spi)
{
	struct rk1608_state *rk1608;
	struct v4l2_subdev *sd;
	int ret = 0;

	rk1608 = devm_kzalloc(&spi->dev, sizeof(*rk1608), GFP_KERNEL);
	if (!rk1608)
		return -ENOMEM;
	rk1608->dev = &spi->dev;
	rk1608->spi = spi;
	spi_set_drvdata(spi, rk1608);

	ret = rk1608_parse_dt_property(rk1608);
	if (ret) {
		dev_err(rk1608->dev, "RK1608 parse dt property err %x\n", ret);
		return ret;
	}

	ret = rk1608_get_remote_node_dev(rk1608);
	if (ret) {
		dev_err(rk1608->dev, "Get remote node dev err %x\n", ret);
		return ret;
	}

	rk1608->sensor_cnt = 0;
	mutex_init(&rk1608->sensor_lock);
	mutex_init(&rk1608->send_msg_lock);
	mutex_init(&rk1608->lock);
	spin_lock_init(&rk1608->hdrae_lock);
	sd = &rk1608->sd;

	rk1608_initialize_controls(rk1608);
	v4l2_spi_subdev_init(sd, spi, &rk1608_subdev_ops);

	if (!IS_ERR(rk1608->irq_gpio)) {
		rk1608->irq = gpiod_to_irq(rk1608->irq_gpio);
		ret = devm_request_threaded_irq(
						rk1608->dev,
						rk1608->irq,
						NULL,
						rk1608_threaded_isr,
						IRQF_TRIGGER_RISING | IRQF_ONESHOT,
						"msg-irq",
						rk1608);
		if (ret) {
			dev_err(rk1608->dev,
				"cannot request thread irq: %d\n", ret);
			v4l2_ctrl_handler_free(&rk1608->ctrl_handler);
			return ret;
		}
		disable_irq(rk1608->irq);
	}

	return 0;
}

static int rk1608_remove(struct spi_device *spi)
{
	struct rk1608_state *rk1608 = spi_get_drvdata(spi);

	v4l2_ctrl_handler_free(&rk1608->ctrl_handler);
	mutex_destroy(&rk1608->lock);
	mutex_destroy(&rk1608->send_msg_lock);
	mutex_destroy(&rk1608->sensor_lock);

	return 0;
}

static const struct spi_device_id rk1608_id[] = {
	{ "rk1608", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, rk1608_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id rk1608_of_match[] = {
	{ .compatible = "rockchip,rk1608" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rk1608_of_match);
#endif

static struct spi_driver rk1608_driver = {
	.driver = {
		.of_match_table = of_match_ptr(rk1608_of_match),
		.name	= "rk1608",
	},
	.probe		= rk1608_probe,
	.remove		= rk1608_remove,
	.id_table	= rk1608_id,
};

static int __init preisp_mod_init(void)
{
	return spi_register_driver(&rk1608_driver);
}

static void __exit preisp_mod_exit(void)
{
	spi_unregister_driver(&rk1608_driver);
}

late_initcall(preisp_mod_init);
module_exit(preisp_mod_exit);

MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("A DSP driver for rk1608 chip");
MODULE_LICENSE("GPL v2");
