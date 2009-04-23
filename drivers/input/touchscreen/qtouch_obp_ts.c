/*
 * drivers/input/touchscreen/qtouch_obp_ts.c - driver for Quantum touch IC
 *
 * Copyright (C) 2009 Google, Inc.
 * Copyright (C) 2009 Motorola, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Derived from the Motorola OBP touch driver.
 *
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/earlysuspend.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/qtouch_obp_ts.h>

#define IGNORE_CHECKSUM_MISMATCH

struct qtm_object {
	struct qtm_obj_entry		entry;
	uint8_t				report_id_min;
	uint8_t				report_id_max;
};

struct axis_map {
	int	key;
	int	x;
	int	y;
};

struct coordinate_map {
	int x_data;
	int y_data;
	int z_data;
	int w_data;
	int down;
};

#define _BITMAP_LEN			BITS_TO_LONGS(QTM_OBP_MAX_OBJECT_NUM)
struct qtouch_ts_data {
	struct i2c_client		*client;
	struct input_dev		*input_dev;
	struct work_struct		init_work;
	struct work_struct		work;
	struct qtouch_ts_platform_data	*pdata;
	struct coordinate_map		finger_data[10];
	struct early_suspend		early_suspend;

	struct qtm_object		obj_tbl[QTM_OBP_MAX_OBJECT_NUM];
	unsigned long			obj_map[_BITMAP_LEN];

	uint32_t			last_keystate;
	uint16_t			eeprom_checksum;
	uint8_t			    checksum_cnt;
	int					x_delta;
	int					y_delta;

	/* Note: The message buffer is reused for reading different messages.
	 * MUST enforce that there is no concurrent access to msg_buf. */
	uint8_t				*msg_buf;
	int				msg_size;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void qtouch_ts_early_suspend(struct early_suspend *handler);
static void qtouch_ts_late_resume(struct early_suspend *handler);
#endif

static struct workqueue_struct *qtouch_ts_wq;

static uint32_t qtouch_tsdebug;
module_param_named(tsdebug, qtouch_tsdebug, uint, 0664);

static irqreturn_t qtouch_ts_irq_handler(int irq, void *dev_id)
{
	struct qtouch_ts_data *ts = dev_id;

	disable_irq_nosync(ts->client->irq);
	queue_work(qtouch_ts_wq, &ts->work);
	return IRQ_HANDLED;
}

static int qtouch_write(struct qtouch_ts_data *ts, void *buf, int buf_sz)
{
	int retries = 10;
	int ret;

	do {
		ret = i2c_master_send(ts->client, (char *)buf, buf_sz);
	} while ((ret < buf_sz) && (--retries > 0));

	if (ret < 0)
		pr_info("%s: Error while trying to write %d bytes\n", __func__,
			buf_sz);
	else if (ret != buf_sz) {
		pr_info("%s: Write %d bytes, expected %d\n", __func__,
			ret, buf_sz);
		ret = -EIO;
	}
	return ret;
}

static int qtouch_set_addr(struct qtouch_ts_data *ts, uint16_t addr)
{
	int ret;

	/* Note: addr on the wire is LSB first */
	ret = qtouch_write(ts, (char *)&addr, sizeof(uint16_t));
	if (ret < 0)
		pr_info("%s: Can't send obp addr 0x%4x\n", __func__, addr);

	return ret >= 0 ? 0 : ret;
}

static int qtouch_read(struct qtouch_ts_data *ts, void *buf, int buf_sz)
{
	int retries = 10;
	int ret;

	do {
		memset(buf, 0, buf_sz);
		ret = i2c_master_recv(ts->client, (char *)buf, buf_sz);
	} while ((ret < 0) && (--retries > 0));

	if (ret < 0)
		pr_info("%s: Error while trying to read %d bytes\n", __func__,
			buf_sz);
	else if (ret != buf_sz) {
		pr_info("%s: Read %d bytes, expected %d\n", __func__,
			ret, buf_sz);
		ret = -EIO;
	}

	return ret >= 0 ? 0 : ret;
}

static int qtouch_read_addr(struct qtouch_ts_data *ts, uint16_t addr,
			    void *buf, int buf_sz)
{
	int ret;

	ret = qtouch_set_addr(ts, addr);
	if (ret != 0)
		return ret;

	return qtouch_read(ts, buf, buf_sz);
}

static struct qtm_obj_message *qtouch_read_msg(struct qtouch_ts_data *ts)
{
	int ret;

	ret = qtouch_read(ts, ts->msg_buf, ts->msg_size);
	if (!ret)
		return (struct qtm_obj_message *)ts->msg_buf;
	return NULL;
}

static int qtouch_write_addr(struct qtouch_ts_data *ts, uint16_t addr,
			     void *buf, int buf_sz)
{
	int ret;
	uint8_t write_buf[128];

	if (buf_sz + sizeof(uint16_t) > sizeof(write_buf)) {
		pr_err("%s: Buffer too large (%d)\n", __func__, buf_sz);
		return -EINVAL;
	}

	memcpy(write_buf, (void *)&addr, sizeof(addr));
	memcpy((void *)write_buf + sizeof(addr), buf, buf_sz);

	ret = qtouch_write(ts, write_buf, buf_sz + sizeof(addr));

	if (ret < 0) {
		pr_err("%s: Could not write %d bytes.\n", __func__, buf_sz);
		return ret;
	}

	return 0;
}

static uint16_t calc_csum(uint16_t curr_sum, void *_buf, int buf_sz)
{
	uint8_t *buf = _buf;
	uint32_t new_sum;
	int i;

	while (buf_sz-- > 0) {
		new_sum = (((uint32_t) curr_sum) << 8) | *(buf++);
		for (i = 0; i < 8; ++i) {
			if (new_sum & 0x800000)
				new_sum ^= 0x800500;
			new_sum <<= 1;
		}
		curr_sum = ((uint32_t) new_sum >> 8) & 0xffff;
	}

	return curr_sum;
}

static inline struct qtm_object *find_obj(struct qtouch_ts_data *ts, int id)
{
	return &ts->obj_tbl[id];
}

static struct qtm_object *create_obj(struct qtouch_ts_data *ts,
				     struct qtm_obj_entry *entry)
{
	struct qtm_object *obj;

	obj = &ts->obj_tbl[entry->type];
	memcpy(&obj->entry, entry, sizeof(*entry));
	set_bit(entry->type, ts->obj_map);

	return obj;
}

static struct qtm_object *find_object_rid(struct qtouch_ts_data *ts, int rid)
{
	int i;

	for_each_bit(i, ts->obj_map, QTM_OBP_MAX_OBJECT_NUM) {
		struct qtm_object *obj = &ts->obj_tbl[i];

		if ((rid >= obj->report_id_min) && (rid <= obj->report_id_max))
			return obj;
	}

	return NULL;
}

static void qtouch_force_reset(struct qtouch_ts_data *ts, uint8_t sw_reset)
{
	struct qtm_object *obj;
	uint16_t addr;
	uint8_t val;
	int ret;

	if (ts->pdata->hw_reset && !sw_reset) {
		pr_info("%s: Forcing HW reset\n", __func__);
		ts->pdata->hw_reset();
	} else if (sw_reset) {
		pr_info("%s: Forcing SW reset\n", __func__);
		obj = find_obj(ts, QTM_OBJ_GEN_CMD_PROC);
		addr =
		    obj->entry.addr + offsetof(struct qtm_gen_cmd_proc, reset);
		val = 1;
		ret = qtouch_write_addr(ts, addr, &val, 1);
		if (ret)
			pr_err("%s: Unable to send the reset msg\n", __func__);
	}
}

static int qtouch_force_calibration(struct qtouch_ts_data *ts)
{
	struct qtm_object *obj;
	uint16_t addr;
	uint8_t val;
	int ret;

	pr_info("%s: Forcing calibration\n", __func__);

	obj = find_obj(ts, QTM_OBJ_GEN_CMD_PROC);

	addr = obj->entry.addr + offsetof(struct qtm_gen_cmd_proc, calibrate);
	val = 1;
	ret = qtouch_write_addr(ts, addr, &val, 1);
	if (ret)
		pr_err("%s: Unable to send the calibrate message\n", __func__);
	return ret;
}

#undef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
static int qtouch_power_config(struct qtouch_ts_data *ts, int on)
{
	struct qtm_gen_power_cfg pwr_cfg;
	struct qtm_object *obj;

	if (!on) {
		/* go to standby mode */
		pwr_cfg.idle_acq_int = 0;
		pwr_cfg.active_acq_int = 0;
	} else {
		pwr_cfg.idle_acq_int = ts->pdata->power_cfg.idle_acq_int;
		pwr_cfg.active_acq_int = ts->pdata->power_cfg.active_acq_int;
	}

	pwr_cfg.active_idle_to = ts->pdata->power_cfg.active_idle_to;

	obj = find_obj(ts, QTM_OBJ_GEN_PWR_CONF);
	return qtouch_write_addr(ts, obj->entry.addr, &pwr_cfg,
				 min(sizeof(pwr_cfg), obj->entry.size));
}

/* Apply the configuration provided in the platform_data to the hardware */
static int qtouch_hw_init(struct qtouch_ts_data *ts)
{
	struct qtm_object *obj;
	int i;
	int ret;
	uint16_t adj_addr;

	pr_info("%s: Doing hw init\n", __func__);

	/* take the IC out of suspend */
	qtouch_power_config(ts, 1);

	/* configure the acquisition object. */
	obj = find_obj(ts, QTM_OBJ_GEN_ACQUIRE_CONF);
	ret = qtouch_write_addr(ts, obj->entry.addr, &ts->pdata->acquire_cfg,
				min(sizeof(ts->pdata->acquire_cfg),
				    obj->entry.size));
	if (ret != 0) {
		pr_err("%s: Can't write acquisition config\n", __func__);
		return ret;
	}

	/* The multitouch and keyarray objects have very similar memory
	 * layout, but are just different enough where we basically have to
	 * repeat the same code */

	/* configure the multi-touch object. */
	obj = find_obj(ts, QTM_OBJ_TOUCH_MULTI);
	if (obj && obj->entry.num_inst > 0) {
		struct qtm_touch_multi_cfg cfg;
		memcpy(&cfg, &ts->pdata->multi_touch_cfg, sizeof(cfg));
		if (ts->pdata->flags & QTOUCH_USE_MULTITOUCH)
			cfg.ctrl |= (1 << 1) | (1 << 0); /* reporten | enable */
		else
			cfg.ctrl = 0;
		ret = qtouch_write_addr(ts, obj->entry.addr, &cfg,
					min(sizeof(cfg), obj->entry.size));
		if (ret != 0) {
			pr_err("%s: Can't write multi-touch config\n",
			       __func__);
			return ret;
		}
	}

	/* configure the key-array object. */
	obj = find_obj(ts, QTM_OBJ_TOUCH_KEYARRAY);
	if (obj && obj->entry.num_inst > 0) {
		struct qtm_touch_keyarray_cfg cfg;
		for (i = 0; i < obj->entry.num_inst; i++) {
			if (i > (ts->pdata->key_array.num_keys - 1)) {
				pr_info("%s: No entry key instance.\n",
					__func__);
				memset(&cfg, 0, sizeof(cfg));
			} else if (ts->pdata->flags & QTOUCH_USE_KEYARRAY) {
				memcpy(&cfg, &ts->pdata->key_array.cfg[i], sizeof(cfg));
				cfg.ctrl |= (1 << 1) | (1 << 0); /* reporten | enable */
			} else
				memset(&cfg, 0, sizeof(cfg));

			adj_addr = obj->entry.addr +
				((obj->entry.size + 1) * i);
			ret = qtouch_write_addr(ts, adj_addr, &cfg,
				min(sizeof(cfg), obj->entry.size));
			if (ret != 0) {
				pr_err("%s: Can't write keyarray config\n",
					   __func__);
				return ret;
			}
		}
	}

	/* configure the signal filter */
	obj = find_obj(ts, QTM_OBJ_PROCG_SIG_FILTER);
	if (obj && obj->entry.num_inst > 0) {
		ret = qtouch_write_addr(ts, obj->entry.addr,
					&ts->pdata->sig_filter_cfg,
					min(sizeof(ts->pdata->sig_filter_cfg),
					    obj->entry.size));
		if (ret != 0) {
			pr_err("%s: Can't write signal filter config\n",
			       __func__);
			return ret;
		}
	}

	/* configure the linearization table */
	obj = find_obj(ts, QTM_OBJ_PROCI_LINEAR_TBL);
	if (obj && obj->entry.num_inst > 0) {
		ret = qtouch_write_addr(ts, obj->entry.addr,
					&ts->pdata->linear_tbl_cfg,
					min(sizeof(ts->pdata->linear_tbl_cfg),
					    obj->entry.size));
		if (ret != 0) {
			pr_err("%s: Can't write linear table config\n",
			       __func__);
			return ret;
		}
	}
	/* configure the grip suppression table */
	obj = find_obj(ts, QTM_OBJ_PROCI_GRIPFACESUPPRESSION);
	if (obj && obj->entry.num_inst > 0) {
		ret = qtouch_write_addr(ts, obj->entry.addr,
					&ts->pdata->grip_suppression_cfg,
					min(sizeof(ts->pdata->grip_suppression_cfg),
					    obj->entry.size));
		if (ret != 0) {
			pr_err("%s: Can't write the grip suppression config\n",
			       __func__);
			return ret;
		}
	}

	/* configure the noise suppression table */
	obj = find_obj(ts, QTM_OBJ_NOISESUPPRESSION_1);
	if (obj && obj->entry.num_inst > 0) {
		ret = qtouch_write_addr(ts, obj->entry.addr,
					&ts->pdata->noise1_suppression_cfg,
					min(sizeof(ts->pdata->noise1_suppression_cfg),
					    obj->entry.size));
		if (ret != 0) {
			pr_err("%s: Can't write the noise suppression config\n",
			       __func__);
			return ret;
		}
	}

	ret = qtouch_force_calibration(ts);
	if (ret != 0) {
		pr_err("%s: Unable to recalibrate after reset\n", __func__);
		return ret;
	}

	/* Write the settings into nvram, if needed */
	if (ts->pdata->flags & QTOUCH_CFG_BACKUPNV) {
		uint8_t val;
		uint16_t addr;

		obj = find_obj(ts, QTM_OBJ_GEN_CMD_PROC);
		addr = obj->entry.addr + offsetof(struct qtm_gen_cmd_proc,
						  backupnv);
		val = 0x55;
		ret = qtouch_write_addr(ts, addr, &val, 1);
		if (ret != 0) {
			pr_err("%s: Can't backup nvram settings\n", __func__);
			return ret;
		}
		/* Since the IC does not indicate that has completed the
		backup place a hard wait here.  If we communicate with the
		IC during backup the EEPROM may be corrupted */

		msleep(500);
	}

	/* reset the address pointer */
	ret = qtouch_set_addr(ts, ts->obj_tbl[QTM_OBJ_GEN_MSG_PROC].entry.addr);
	if (ret != 0) {
		pr_err("%s: Unable to reset address pointer after reset\n",
		       __func__);
		return ret;
	}

	return 0;
}

/* Handles a message from the command processor object. */
static int do_cmd_proc_msg(struct qtouch_ts_data *ts, struct qtm_object *obj,
			   void *_msg)
{
	struct qtm_cmd_proc_msg *msg = _msg;
	int ret = 0;
	int hw_reset = 0;

	if (msg->status & QTM_CMD_PROC_STATUS_RESET) {
		if (qtouch_tsdebug)
			pr_info("%s:EEPROM checksum is 0x%X cnt %i\n",
				__func__, msg->checksum, ts->checksum_cnt);
		if (msg->checksum != ts->eeprom_checksum) {
			if (ts->checksum_cnt > 2) {
				/* Assume the checksum is what it is, cannot
				disable the touch screen so set the checksum*/
				ts->eeprom_checksum = msg->checksum;
				ts->checksum_cnt = 0;
			} else {
				ret = qtouch_hw_init(ts);
				if (ret != 0)
					pr_err("%s:Cannot init the touch IC\n",
						   __func__);
				hw_reset = 1;
				ts->checksum_cnt++;
			}
		}
		pr_info("%s: Reset done.\n", __func__);
	}

	if (msg->status & QTM_CMD_PROC_STATUS_CAL)
		pr_info("%s: Self-calibration started.\n", __func__);

	if (msg->status & QTM_CMD_PROC_STATUS_OFL)
		pr_err("%s: Acquisition cycle length overflow\n", __func__);

	if (msg->status & QTM_CMD_PROC_STATUS_SIGERR)
		pr_err("%s: Acquisition error\n", __func__);

	if (msg->status & QTM_CMD_PROC_STATUS_CFGERR) {
		ret = qtouch_hw_init(ts);
		if (ret != 0)
			pr_err("%s:Cannot init the touch IC\n",
			       __func__);

		pr_err("%s: Configuration error\n", __func__);
	}
	/* Check the EEPROM checksum.  An ESD event may cause
	the checksum to change during operation so we need to
	reprogram the EEPROM and reset the IC */
	if (ts->pdata->flags & QTOUCH_EEPROM_CHECKSUM) {
		if (msg->checksum != ts->eeprom_checksum) {
			if (qtouch_tsdebug)
				pr_info("%s:EEPROM checksum is 0x%X cnt %i \
						hw_reset %i\n",
					__func__, msg->checksum,
					ts->checksum_cnt, hw_reset);
			if (ts->checksum_cnt > 2) {
				/* Assume the checksum is what it is, cannot
				disable the touch screen so set the checksum*/
				ts->eeprom_checksum = msg->checksum;
				ts->checksum_cnt = 0;
			} else {
				if (!hw_reset) {
					ret = qtouch_hw_init(ts);
					if (ret != 0)
						pr_err("%s:Cannot init the touch IC\n",
						__func__);
					qtouch_force_reset(ts, 0);
					ts->checksum_cnt++;
				}
			}
		}
	}
	return ret;
}

/* Handles a message from a multi-touch object. */
static int do_touch_multi_msg(struct qtouch_ts_data *ts, struct qtm_object *obj,
			      void *_msg)
{
	struct qtm_touch_multi_msg *msg = _msg;
	int i;
	int x;
	int y;
	int pressure;
	int width;
	int finger;
	int down;

	finger = msg->report_id - obj->report_id_min;
	if (finger >= ts->pdata->multi_touch_cfg.num_touch)
		return 0;

	/* x/y are 10bit values, with bottom 2 bits inside the xypos_lsb */
	x = (msg->xpos_msb << 2) | ((msg->xypos_lsb >> 6) & 0x3);
	y = (msg->ypos_msb << 2) | ((msg->xypos_lsb >> 2) & 0x3);
	width = msg->touch_area;
	pressure = msg->touch_amp;

	if (ts->pdata->flags & QTOUCH_SWAP_XY)
		swap(x, y);

	if (qtouch_tsdebug & 2)
		pr_info("%s: stat=%02x, f=%d x=%d y=%d p=%d w=%d\n", __func__,
			msg->status, finger, x, y, pressure, width);

	down = !(msg->status & QTM_TOUCH_MULTI_STATUS_RELEASE);

	/* The chip may report erroneous points way
	beyond what a user could possibly perform so we filter
	these out */
	if (ts->finger_data[finger].down &&
			(abs(ts->finger_data[finger].x_data - x) > ts->x_delta ||
			abs(ts->finger_data[finger].y_data - y) > ts->y_delta)) {
				down = 0;
				if (qtouch_tsdebug & 2)
					pr_info("%s: x0 %i x1 %i y0 %i y1 %i\n",
						__func__,
						ts->finger_data[finger].x_data, x,
						ts->finger_data[finger].y_data, y);
	} else {
		ts->finger_data[finger].x_data = x;
		ts->finger_data[finger].y_data = y;
		ts->finger_data[finger].w_data = width;
	}

	/* The touch IC will not give back a pressure of zero
	   so send a 0 when a liftoff is produced */
	if (!down) {
		ts->finger_data[finger].z_data = 0;
	} else {
		ts->finger_data[finger].z_data = pressure;
		ts->finger_data[finger].down = down;
	}

	for (i = 0; i < ts->pdata->multi_touch_cfg.num_touch; i++) {
		if (ts->finger_data[i].down == 0)
			continue;
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR,
				 ts->finger_data[i].z_data);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR,
				 ts->finger_data[i].w_data);
		input_report_abs(ts->input_dev, ABS_MT_POSITION_X,
				 ts->finger_data[i].x_data);
		input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,
				 ts->finger_data[i].y_data);
		input_mt_sync(ts->input_dev);
	}
	input_sync(ts->input_dev);

	if (!down) {
		memset(&ts->finger_data[finger], 0,
		sizeof(struct coordinate_map));
	}

	return 0;
}

/* Handles a message from a keyarray object. */
static int do_touch_keyarray_msg(struct qtouch_ts_data *ts,
				 struct qtm_object *obj, void *_msg)
{
	struct qtm_touch_keyarray_msg *msg = _msg;
	int i;

	/* nothing changed.. odd. */
	if (ts->last_keystate == msg->keystate)
		return 0;

	for (i = 0; i < ts->pdata->key_array.num_keys; ++i) {
		struct qtouch_key *key = &ts->pdata->key_array.keys[i];
		uint32_t bit = 1 << (key->channel & 0x1f);
		if ((msg->keystate & bit) != (ts->last_keystate & bit))
			input_report_key(ts->input_dev, key->code,
					 msg->keystate & bit);
	}
	input_sync(ts->input_dev);

	if (qtouch_tsdebug & 2)
		pr_info("%s: key state changed 0x%08x -> 0x%08x\n", __func__,
			ts->last_keystate, msg->keystate);

	/* update our internal state */
	ts->last_keystate = msg->keystate;

	return 0;
}

static int qtouch_handle_msg(struct qtouch_ts_data *ts, struct qtm_object *obj,
			     struct qtm_obj_message *msg)
{
	int ret = 0;

	/* These are all the known objects that we know how to handle. */
	switch (obj->entry.type) {
	case QTM_OBJ_GEN_CMD_PROC:
		ret = do_cmd_proc_msg(ts, obj, msg);
		break;

	case QTM_OBJ_TOUCH_MULTI:
		ret = do_touch_multi_msg(ts, obj, msg);
		break;

	case QTM_OBJ_TOUCH_KEYARRAY:
		ret = do_touch_keyarray_msg(ts, obj, msg);
		break;

	default:
		/* probably not fatal? */
		ret = 0;
		pr_info("%s: No handler defined for message from object "
			"type %d, report_id %d\n", __func__, obj->entry.type,
			msg->report_id);
	}

	return ret;
}

static void qtouch_ts_work_func(struct work_struct *work)
{
	struct qtouch_ts_data *ts =
		container_of(work, struct qtouch_ts_data, work);
	struct qtm_obj_message *msg;
	struct qtm_object *obj;
	int ret;

	msg = qtouch_read_msg(ts);
	if (msg == NULL) {
		pr_err("%s: Cannot read message\n", __func__);
		goto done;
	}

	obj = find_object_rid(ts, msg->report_id);
	if (!obj) {
		pr_err("%s: Unknown object for report_id %d\n", __func__,
		       msg->report_id);
		goto done;
	}

	ret = qtouch_handle_msg(ts, obj, msg);
	if (ret != 0) {
		pr_err("%s: Unable to process message for obj %d, "
		       "report_id %d\n", __func__, obj->entry.type,
		       msg->report_id);
		goto done;
	}

done:
	enable_irq(ts->client->irq);
}

static int qtouch_process_info_block(struct qtouch_ts_data *ts)
{
	struct qtm_id_info qtm_info;
	uint16_t our_csum = 0x0;
	uint16_t their_csum;
	uint8_t report_id;
	uint16_t addr;
	int err;
	int i;

	/* query the device and get the info block. */
	err = qtouch_read_addr(ts, QTM_OBP_ID_INFO_ADDR, &qtm_info,
			       sizeof(qtm_info));
	if (err != 0) {
		pr_err("%s: Cannot read info object block\n", __func__);
		goto err_read_info_block;
	}
	our_csum = calc_csum(our_csum, &qtm_info, sizeof(qtm_info));

	/* TODO: Add a version/family/variant check? */
	pr_info("%s: Build version is 0x%x\n", __func__, qtm_info.version);

	if (qtm_info.num_objs == 0) {
		pr_err("%s: Device (0x%x/0x%x/0x%x/0x%x) does not export any "
		       "objects.\n", __func__, qtm_info.family_id,
		       qtm_info.variant_id, qtm_info.version, qtm_info.build);
		err = -ENODEV;
		goto err_no_objects;
	}

	addr = QTM_OBP_ID_INFO_ADDR + sizeof(qtm_info);
	report_id = 1;

	/* read out the object entries table */
	for (i = 0; i < qtm_info.num_objs; ++i) {
		struct qtm_object *obj;
		struct qtm_obj_entry entry;

		err = qtouch_read_addr(ts, addr, &entry, sizeof(entry));
		if (err != 0) {
			pr_err("%s: Can't read object (%d) entry.\n",
			       __func__, i);
			err = -EIO;
			goto err_read_entry;
		}
		our_csum = calc_csum(our_csum, &entry, sizeof(entry));
		addr += sizeof(entry);

		entry.size++;
		entry.num_inst++;

		pr_info("%s: Object %d @ 0x%04x (%d) insts %d rep_ids %d\n",
			__func__, entry.type, entry.addr, entry.size,
			entry.num_inst, entry.num_rids);

		if (entry.type >= QTM_OBP_MAX_OBJECT_NUM) {
			pr_warning("%s: Unknown object type (%d) encountered\n",
				   __func__, entry.type);
			/* Not fatal */
			continue;
		}

		/* save the message_procesor msg_size for easy reference. */
		if (entry.type == QTM_OBJ_GEN_MSG_PROC)
			ts->msg_size = entry.size;

		obj = create_obj(ts, &entry);
		/* set the report_id range that the object is responsible for */
		if ((obj->entry.num_rids * obj->entry.num_inst) != 0) {
			obj->report_id_min = report_id;
			report_id += obj->entry.num_rids * obj->entry.num_inst;
			obj->report_id_max = report_id - 1;
		}
	}

	if (!ts->msg_size) {
		pr_err("%s: Message processing object not found. Bailing.\n",
		       __func__);
		err = -ENODEV;
		goto err_no_msg_proc;
	}

	/* verify that some basic objects are present. These objects are
	 * assumed to be present by the rest of the driver, so fail out now
	 * if the firmware is busted. */
	if (!find_obj(ts, QTM_OBJ_GEN_PWR_CONF) ||
	    !find_obj(ts, QTM_OBJ_GEN_ACQUIRE_CONF) ||
	    !find_obj(ts, QTM_OBJ_GEN_MSG_PROC) ||
	    !find_obj(ts, QTM_OBJ_GEN_CMD_PROC)) {
		pr_err("%s: Required objects are missing\n", __func__);
		err = -ENOENT;
		goto err_missing_objs;
	}

	err = qtouch_read_addr(ts, addr, &their_csum, sizeof(their_csum));
	if (err != 0) {
		pr_err("%s: Unable to read remote checksum\n", __func__);
		err = -ENODEV;
		goto err_no_checksum;
	}

	/* FIXME: The algorithm described in the datasheet doesn't seem to
	 * match what the touch firmware is doing on the other side. We
	 * always get mismatches! */
	if (our_csum != their_csum) {
		pr_warning("%s: Checksum mismatch (0x%04x != 0x%04x)\n",
			   __func__, our_csum, their_csum);
#ifndef IGNORE_CHECKSUM_MISMATCH
		err = -ENODEV;
		goto err_bad_checksum;
#endif
	}

	pr_info("%s: %s found. family 0x%x, variant 0x%x, ver 0x%x, "
		"build 0x%x, matrix %dx%d, %d objects.\n", __func__,
		QTOUCH_TS_NAME, qtm_info.family_id, qtm_info.variant_id,
		qtm_info.version, qtm_info.build, qtm_info.matrix_x_size,
		qtm_info.matrix_y_size, qtm_info.num_objs);

	ts->eeprom_checksum = ts->pdata->nv_checksum;

	return 0;

err_no_checksum:
err_missing_objs:
err_no_msg_proc:
err_read_entry:
err_no_objects:
err_read_info_block:
	return err;
}

static int qtouch_ts_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct qtouch_ts_platform_data *pdata = client->dev.platform_data;
	struct qtouch_ts_data *ts;
	struct qtm_object *obj;
	int err;
	int i;

	if (pdata == NULL) {
		pr_err("%s: platform data required\n", __func__);
		return -ENODEV;
	} else if (!client->irq) {
		pr_err("%s: polling mode currently not supported\n", __func__);
		return -ENODEV;
	} else if (!pdata->hw_reset) {
		pr_err("%s: Must supply a hw reset function\n", __func__);
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s: need I2C_FUNC_I2C\n", __func__);
		return -ENODEV;
	}

	ts = kzalloc(sizeof(struct qtouch_ts_data), GFP_KERNEL);
	if (ts == NULL) {
		err = -ENOMEM;
		goto err_alloc_data_failed;
	}

	INIT_WORK(&ts->work, qtouch_ts_work_func);

	ts->pdata = pdata;
	ts->client = client;
	i2c_set_clientdata(client, ts);
	ts->checksum_cnt = 0;
	ts->x_delta = ts->pdata->x_delta;
	ts->y_delta = ts->pdata->y_delta;

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		pr_err("%s: failed to alloc input device\n", __func__);
		err = -ENOMEM;
		goto err_alloc_input_dev;
	}
	ts->input_dev->name = "qtouch-touchscreen";
	input_set_drvdata(ts->input_dev, ts);

	qtouch_force_reset(ts, 0);

	err = qtouch_process_info_block(ts);
	if (err != 0)
		goto err_process_info_block;

	ts->msg_buf = kmalloc(ts->msg_size, GFP_KERNEL);
	if (ts->msg_buf == NULL) {
		pr_err("%s: Cannot allocate msg_buf\n", __func__);
		err = -ENOMEM;
		goto err_alloc_msg_buf;
	}

	/* Point the address pointer to the message processor.
	 * Must do this before enabling interrupts */
	obj = find_obj(ts, QTM_OBJ_GEN_MSG_PROC);
	err = qtouch_set_addr(ts, obj->entry.addr);
	if (err != 0) {
		pr_err("%s: Can't to set addr to msg processor\n", __func__);
		goto err_rst_addr_msg_proc;
	}

	set_bit(EV_SYN, ts->input_dev->evbit);

	/* register the harwdare assisted virtual keys, if any */
	obj = find_obj(ts, QTM_OBJ_TOUCH_KEYARRAY);
	if (obj && (obj->entry.num_inst > 0) &&
	    (pdata->flags & QTOUCH_USE_KEYARRAY)) {
		for (i = 0; i < pdata->key_array.num_keys; ++i)
			input_set_capability(ts->input_dev, EV_KEY,
					     pdata->key_array.keys[i].code);
	}

	/* register the software virtual keys, if any are provided */
	for (i = 0; i < pdata->vkeys.count; ++i)
		input_set_capability(ts->input_dev, EV_KEY,
				     pdata->vkeys.keys[i].code);

	obj = find_obj(ts, QTM_OBJ_TOUCH_MULTI);
	if (obj && obj->entry.num_inst > 0) {
		set_bit(EV_ABS, ts->input_dev->evbit);
		/* Legacy support for testing only */
		input_set_capability(ts->input_dev, EV_KEY, BTN_TOUCH);
		input_set_capability(ts->input_dev, EV_KEY, BTN_2);
		input_set_abs_params(ts->input_dev, ABS_X,
			pdata->abs_min_x, pdata->abs_max_x,
			pdata->fuzz_x, 0);
		input_set_abs_params(ts->input_dev, ABS_HAT0X,
			pdata->abs_min_x, pdata->abs_max_x,
			pdata->fuzz_x, 0);
		input_set_abs_params(ts->input_dev, ABS_Y,
			pdata->abs_min_y, pdata->abs_max_y,
			pdata->fuzz_y, 0);
		input_set_abs_params(ts->input_dev, ABS_HAT0Y,
			pdata->abs_min_x, pdata->abs_max_x,
			pdata->fuzz_x, 0);
		input_set_abs_params(ts->input_dev, ABS_PRESSURE,
			pdata->abs_min_p, pdata->abs_max_p,
			pdata->fuzz_p, 0);
		input_set_abs_params(ts->input_dev, ABS_TOOL_WIDTH,
			pdata->abs_min_w, pdata->abs_max_w,
			pdata->fuzz_w, 0);

		/* multi touch */
		input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X,
			pdata->abs_min_x, pdata->abs_max_x,
			pdata->fuzz_x, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y,
			pdata->abs_min_y, pdata->abs_max_y,
			pdata->fuzz_y, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR,
			pdata->abs_min_p, pdata->abs_max_p,
			pdata->fuzz_p, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR,
			pdata->abs_min_w, pdata->abs_max_w,
			pdata->fuzz_w, 0);
	}

	memset(&ts->finger_data[0], 0,
		(sizeof(struct coordinate_map) *
		ts->pdata->multi_touch_cfg.num_touch));

	err = input_register_device(ts->input_dev);
	if (err != 0) {
		pr_err("%s: Cannot register input device \"%s\"\n", __func__,
		       ts->input_dev->name);
		goto err_input_register_dev;
	}

	err = request_irq(ts->client->irq, qtouch_ts_irq_handler,
			  IRQ_DISABLED | pdata->irqflags, "qtouch_ts_int", ts);
	if (err != 0) {
		pr_err("%s: request_irq (%d) failed\n", __func__,
		       ts->client->irq);
		goto err_request_irq;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = qtouch_ts_early_suspend;
	ts->early_suspend.resume = qtouch_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	return 0;

err_request_irq:
	input_unregister_device(ts->input_dev);

err_input_register_dev:
err_rst_addr_msg_proc:
	if (ts->msg_buf)
		kfree(ts->msg_buf);

err_alloc_msg_buf:
err_process_info_block:
	input_free_device(ts->input_dev);

err_alloc_input_dev:
	i2c_set_clientdata(client, NULL);
	kfree(ts);

err_alloc_data_failed:
	return err;
}

static int qtouch_ts_remove(struct i2c_client *client)
{
	struct qtouch_ts_data *ts = i2c_get_clientdata(client);

	unregister_early_suspend(&ts->early_suspend);
	free_irq(ts->client->irq, ts);
	input_unregister_device(ts->input_dev);
	input_free_device(ts->input_dev);
	i2c_set_clientdata(client, NULL);
	kfree(ts);
	return 0;
}

static int qtouch_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct qtouch_ts_data *ts = i2c_get_clientdata(client);
	int ret;
	if (qtouch_tsdebug & 4)
		pr_info("%s: Suspending\n", __func__);

	disable_irq_nosync(ts->client->irq);
	ret = cancel_work_sync(&ts->work);
	if (ret) { /* if work was pending disable-count is now 2 */
		pr_info("%s: Pending work item\n", __func__);
		enable_irq(ts->client->irq);
	}

	ret = qtouch_power_config(ts, 0);
	if (ret < 0)
		pr_err("%s: Cannot write power config\n", __func__);

	return 0;
}

static int qtouch_ts_resume(struct i2c_client *client)
{
	struct qtouch_ts_data *ts = i2c_get_clientdata(client);
	int ret;
	int i;

	if (qtouch_tsdebug & 4)
		pr_info("%s: Resuming\n", __func__);

	/* If we were suspended while a touch was happening
	   we need to tell the upper layers so they do not hang
	   waiting on the liftoff that will not come. */
	for (i = 0; i < ts->pdata->multi_touch_cfg.num_touch; i++) {
		if (qtouch_tsdebug & 4)
			pr_info("%s: Finger %i down state %i\n",
				__func__, i, ts->finger_data[i].down);
		if (ts->finger_data[i].down == 0)
			continue;
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_mt_sync(ts->input_dev);
		memset(&ts->finger_data[i], 0,
		sizeof(struct coordinate_map));
	}
	input_sync(ts->input_dev);

	ret = qtouch_power_config(ts, 1);
	if (ret < 0) {
		pr_err("%s: Cannot write power config\n", __func__);
		return -EIO;
	}
	qtouch_force_reset(ts, 0);

	enable_irq(ts->client->irq);
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void qtouch_ts_early_suspend(struct early_suspend *handler)
{
	struct qtouch_ts_data *ts;

	ts = container_of(handler, struct qtouch_ts_data, early_suspend);
	qtouch_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void qtouch_ts_late_resume(struct early_suspend *handler)
{
	struct qtouch_ts_data *ts;

	ts = container_of(handler, struct qtouch_ts_data, early_suspend);
	qtouch_ts_resume(ts->client);
}
#endif

/******** init ********/
static const struct i2c_device_id qtouch_ts_id[] = {
	{ QTOUCH_TS_NAME, 0 },
	{ }
};

static struct i2c_driver qtouch_ts_driver = {
	.probe		= qtouch_ts_probe,
	.remove		= qtouch_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= qtouch_ts_suspend,
	.resume		= qtouch_ts_resume,
#endif
	.id_table	= qtouch_ts_id,
	.driver = {
		.name	= QTOUCH_TS_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __devinit qtouch_ts_init(void)
{
	qtouch_ts_wq = create_singlethread_workqueue("qtouch_obp_ts_wq");
	if (qtouch_ts_wq == NULL) {
		pr_err("%s: No memory for qtouch_ts_wq\n", __func__);
		return -ENOMEM;
	}
	return i2c_add_driver(&qtouch_ts_driver);
}

static void __exit qtouch_ts_exit(void)
{
	i2c_del_driver(&qtouch_ts_driver);
	if (qtouch_ts_wq)
		destroy_workqueue(qtouch_ts_wq);
}

module_init(qtouch_ts_init);
module_exit(qtouch_ts_exit);

MODULE_AUTHOR("Dima Zavin <dima@android.com>");
MODULE_DESCRIPTION("Quantum OBP Touchscreen Driver");
MODULE_LICENSE("GPL");
