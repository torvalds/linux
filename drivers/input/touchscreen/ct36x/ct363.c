#define CT363_POINT_NUM		10

struct ct363_finger_data {
	unsigned char	xhi;			// X coordinate Hi
	unsigned char	yhi;			// Y coordinate Hi
	unsigned char	ylo : 4;		// Y coordinate Lo
	unsigned char	xlo : 4;		// X coordinate Lo
	unsigned char   status : 3;             // Action information, 1: Down; 2: Move; 3: Up
	unsigned char   id : 5;                 // ID information, from 1 to CFG_MAX_POINT_NUM
	unsigned char	area;			// Touch area
	unsigned char	pressure;		// Touch Pressure
};


struct ct363_priv{
	int press;
	int release;
	int x, y;
	union{
		struct ct363_finger_data pts[CT363_POINT_NUM];
		char buf[CT363_POINT_NUM * sizeof(struct ct363_finger_data)];
	};
};

static int ct363_init_hw(struct ct36x_data *ts)
{
	int ret = 0;

	ret = gpio_request(ts->rst_io.gpio, "ct363_rst");
	if(ret < 0){
		dev_err(ts->dev, "Failed to request rst gpio\n");
		return ret;
	}

	ret = gpio_request(ts->irq_io.gpio, "ct363_irq");
	if(ret < 0){
		gpio_free(ts->rst_io.gpio);
		dev_err(ts->dev, "Failed to request irq gpio\n");
		return ret;
	}
	gpio_direction_input(ts->irq_io.gpio);
	gpio_pull_updown(ts->irq_io.gpio, 1);

	gpio_direction_output(ts->rst_io.gpio, ts->rst_io.active_low);

	return 0;
}
static void ct363_deinit_hw(struct ct36x_data *ts)
{
	gpio_free(ts->rst_io.gpio);
	gpio_free(ts->irq_io.gpio);
}

static void ct363_reset_hw(struct ct36x_data *ts)
{
	gpio_direction_output(ts->rst_io.gpio, ts->rst_io.active_low);
	msleep(50);
	gpio_set_value(ts->rst_io.gpio, !ts->rst_io.active_low);
	msleep(50);
	gpio_set_value(ts->rst_io.gpio, ts->rst_io.active_low);
	msleep(500);
}

static int ct363_init(struct ct36x_data *ts)
{
	int ret = 0, fwchksum, binchksum, updcnt = 5;
	struct ct363_priv *ct363 = NULL;
	
	ret = ct363_init_hw(ts);
	if(ret < 0)
		return ret;

	 /* Hardware reset */
	ct363_reset_hw(ts);
	// Get binary Checksum
	binchksum = ct36x_chip_get_binchksum();
	ct36x_dbg(ts, "CT363 init: binchksum = %d\n", binchksum);

	ret = ct36x_chip_get_fwchksum(ts);
	if(ret < 0){
		dev_err(ts->dev, "CT36X chip: Failed to get fwchksum\n");
		return ret;
	}
	fwchksum = ret;
	ct36x_dbg(ts, "CT363 init: fwchksum = %d\n", fwchksum);
	while(binchksum != fwchksum && updcnt--) {
		/* Update Firmware */
		ret = ct36x_chip_go_bootloader(ts);
		if(ret < 0){
			dev_err(ts->dev, "CT36X chip: Failed to go bootloader\n");
			return ret;
		}
		 /* Hardware reset */
		ct363_reset_hw(ts);

		ret = ct36x_chip_get_fwchksum(ts);
		if(ret < 0){
			dev_err(ts->dev, "CT36X chip: Failed to get fwchksum\n");
			return ret;
		}
		fwchksum = ret;
		ct36x_dbg(ts, "CT363 update FW: fwchksum = %d\n", fwchksum);
	}
	if(binchksum != fwchksum){
		dev_err(ts->dev, "Fail to update FW\n");
		return -ENODEV;
	}

	/* Hardware reset */
	ct363_reset_hw(ts);
	msleep(5);

	ts->point_num = CT363_POINT_NUM;
	
	ct363 = kzalloc(sizeof(struct ct363_priv), GFP_KERNEL);
	if(!ct363){
		dev_err(ts->dev, "No memory for ct36x");
		return -ENOMEM;
	}
	ts->priv = ct363;

	return 0;
}

static void ct363_deinit(struct ct36x_data *ts)
{
	struct ct363_priv *ct363 = ts->priv;

	ct363_deinit_hw(ts);
	kfree(ct363);
	ts->priv = NULL;

	return;
}

static int ct363_suspend(struct ct36x_data *ts)
{
	int ret = 0;

	ret = ct36x_chip_go_sleep(ts);

	if(ret < 0)
		dev_warn(ts->dev, "CT363 chip: failed to go to sleep\n");
	return ret;
}

static int ct363_resume(struct ct36x_data *ts)
{
	int i;

	/* Hardware reset */
	ct363_reset_hw(ts);
	msleep(3);

	for(i = 0; i < ts->point_num; i++){
		input_mt_slot(ts->input, i);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
	}
	input_sync(ts->input);

	return 0;
}

static void ct363_report(struct ct36x_data *ts)
{
	int i, ret = 0;
	int sync = 0, x, y;
	int len = sizeof(struct ct363_finger_data) * ts->point_num;
	struct ct363_priv *ct363 = ts->priv;

	ret = ct36x_read(ts, ct363->buf, len);
	if(ret < 0){
		dev_warn(ts->dev, "Failed to read finger data\n");
		return;
	}


	ct363->press = 0;
	for(i = 0; i < ts->point_num; i++){
		if((ct363->pts[i].xhi != 0xFF && ct363->pts[i].yhi != 0xFF) &&
			(ct363->pts[i].status == 1 || ct363->pts[i].status == 2)){
			x = (ct363->pts[i].xhi<<4)|(ct363->pts[i].xlo&0xF);
			y = (ct363->pts[i].yhi<<4)|(ct363->pts[i].ylo&0xF);
			ct363->x = ts->orientation[0] * x + ts->orientation[1] * y;
			ct363->y = ts->orientation[2] * x + ts->orientation[3] * y;
			if(ct363->x < 0)
				ct363->x = ts->x_max - ct363->x;
			if(ct363->y < 0)
				ct363->y = ts->y_max - ct363->y;

			input_mt_slot(ts->input, ct363->pts[i].id - 1);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, true);
			input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, 1);
			input_report_abs(ts->input, ABS_MT_POSITION_X, x);
			input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
			input_report_abs(ts->input, ABS_MT_PRESSURE, ct363->pts[i].pressure);
			ct36x_dbg(ts, "CT363 report value: x: %d, y:%d\n", ct363->x, ct363->y);

			sync = 1;
			ct363->press |= 0x01 << (ct363->pts[i].id - 1);
		}
	}
	ct363->release &= ct363->release ^ ct363->press;
	for(i = 0; i < ts->point_num; i++){
		if ( ct363->release & (0x01<<i) ) {
			input_mt_slot(ts->input, i);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
			ct36x_dbg(ts, "CT363 release\n");
			sync = 1;
		}
	}
	ct363->release = ct363->press;

	if(sync)
		input_sync(ts->input);

	return;
}
struct ct36x_ops ct363_ops = {
	.init = ct363_init,
	.deinit = ct363_deinit,
	.suspend = ct363_suspend,
	.resume = ct363_resume,
	.report = ct363_report,
};
