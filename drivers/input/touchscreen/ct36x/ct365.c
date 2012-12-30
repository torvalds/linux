#define CT365_POINT_NUM		10

struct ct365_finger_data {
	unsigned char   status : 4;             // Action information, 1: Down; 2: Move; 3: Up
	unsigned char   id : 4;                 // ID information, from 1 to CFG_MAX_POINT_NUM
	unsigned char	xhi;			// X coordinate Hi
	unsigned char	yhi;			// Y coordinate Hi
	unsigned char	ylo : 4;		// Y coordinate Lo
	unsigned char	xlo : 4;		// X coordinate Lo
	unsigned char	area;			// Touch area
	unsigned char	pressure;		// Touch Pressure
};


struct ct365_priv{
	int press;
	int release;
	int x, y;
	union{
		struct ct365_finger_data pts[CT365_POINT_NUM];
		char buf[CT365_POINT_NUM * sizeof(struct ct365_finger_data)];
	};
};

static int ct365_init_hw(struct ct36x_data *ts)
{
	int ret = 0;

	ret = gpio_request(ts->rst_io.gpio, "ct365_rst");
	if(ret < 0){
		dev_err(ts->dev, "Failed to request rst gpio\n");
		return ret;
	}

	ret = gpio_request(ts->irq_io.gpio, "ct365_irq");
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
static void ct365_deinit_hw(struct ct36x_data *ts)
{
	gpio_free(ts->rst_io.gpio);
	gpio_free(ts->irq_io.gpio);
}

static void ct365_reset_hw(struct ct36x_data *ts)
{
	gpio_direction_output(ts->rst_io.gpio, ts->rst_io.active_low);
	msleep(50);
	gpio_set_value(ts->rst_io.gpio, !ts->rst_io.active_low);
	msleep(50);
	gpio_set_value(ts->rst_io.gpio, ts->rst_io.active_low);
	msleep(500);
}

static int ct365_init(struct ct36x_data *ts)
{
	int ret = 0, fwchksum, binchksum, updcnt = 5;
	struct ct365_priv *ct365 = NULL;
	
	ret = ct365_init_hw(ts);
	if(ret < 0)
		return ret;

	 /* Hardware reset */
	ct365_reset_hw(ts);
	// Get binary Checksum
	binchksum = ct36x_chip_get_binchksum();
	ct36x_dbg(ts, "CT365 init: binchksum = %d\n", binchksum);

	ret = ct36x_chip_get_fwchksum(ts);
	if(ret < 0){
		dev_err(ts->dev, "CT36X chip: Failed to get fwchksum\n");
		return ret;
	}
	fwchksum = ret;
	ct36x_dbg(ts, "CT365 init: fwchksum = %d\n", fwchksum);

	while(binchksum != fwchksum && updcnt--) {
		/* Update Firmware */
		ret = ct36x_chip_go_bootloader(ts);
		if(ret < 0){
			dev_err(ts->dev, "CT36X chip: Failed to go bootloader\n");
			return ret;
		}
		 /* Hardware reset */
		ct365_reset_hw(ts);

		ret = ct36x_chip_get_fwchksum(ts);
		if(ret < 0){
			dev_err(ts->dev, "CT36X chip: Failed to get fwchksum\n");
			return ret;
		}
		fwchksum = ret;
		ct36x_dbg(ts, "CT365 update FW: fwchksum = %d\n", fwchksum);
	}
	if(binchksum != fwchksum){
		dev_err(ts->dev, "Fail to update FW\n");
		return -ENODEV;
	}

	/* Hardware reset */
	ct365_reset_hw(ts);

	ret = ct36x_chip_set_idle(ts);
	if(ret < 0){
		dev_err(ts->dev, "CT365 init: Failed to set idle\n");
		return ret;
	}

	msleep(5);

	ts->point_num = CT365_POINT_NUM;
	
	ct365 = kzalloc(sizeof(struct ct365_priv), GFP_KERNEL);
	if(!ct365){
		dev_err(ts->dev, "No memory for ct36x");
		return -ENOMEM;
	}

	return 0;
}

static void ct365_deinit(struct ct36x_data *ts)
{
	struct ct365_priv *ct365 = ts->priv;

	ct365_deinit_hw(ts);
	kfree(ct365);
	ts->priv = NULL;

	return;
}

static int ct365_suspend(struct ct36x_data *ts)
{
	int ret = 0;
	char buf[3];

	buf[0] = 0xFF;
	buf[1] = 0x8F;
	buf[2] = 0xFF;
	ret = ct36x_write(ts, buf, 3);
	if(ret < 0)
		dev_warn(ts->dev, "CT365 suspend: i2c write(0xff, 0x8f, 0xff) error\n");
	msleep(3);

	buf[0] = 0x00;
	buf[1] = 0xAF;
	ret = ct36x_write(ts, buf, 2);
	if(ret < 0)
		dev_warn(ts->dev, "CT365 suspend: i2c write(0x00, 0xaf) error\n");
	msleep(3);

	return ret;
}

static int ct365_resume(struct ct36x_data *ts)
{
	int i, ret = 0;
	char buf[2];

	/* Hardware reset */
	ct365_reset_hw(ts);

	buf[0] = 0x00;
	buf[1] = 0x5A;
	ret = ct36x_update_write(ts, 0x7F, buf, 2);
	if(ret < 0)
		dev_warn(ts->dev, "CT365 resume: i2c update write(0x00, 0xaf) error\n");
	msleep(3);

	for(i = 0; i < ts->point_num; i++){
		input_mt_slot(ts->input, i);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
	}
	input_sync(ts->input);

	return 0;
}

static void ct365_report(struct ct36x_data *ts)
{
	int i, ret = 0;
	int sync = 0, x, y;
	int len = sizeof(struct ct365_finger_data) * ts->point_num;
	struct ct365_priv *ct365 = ts->priv;

	ret = ct36x_read(ts, ct365->buf, len);
	if(ret < 0){
		dev_warn(ts->dev, "Failed to read finger data\n");
		return;
	}

	ct36x_dbg(ts, "Read fingers data:\n");

	ct365->press = 0;
	for(i = 0; i < ts->point_num; i++){
		ct36x_dbg(ts, "pst[%d]: id: %d, status: %d, area: %d, pressure: %d\n"
				"xhi: %d, xlo:%d, yhi: %d, ylo: %d\n", 
				i, ct365->pts[i].id, ct365->pts[i].status, ct365->pts[i].area, ct365->pts[i].pressure,
				ct365->pts[i].xhi, ct365->pts[i].ylo, ct365->pts[i].yhi, ct365->pts[i].ylo);

		if((ct365->pts[i].xhi != 0xFF && ct365->pts[i].yhi != 0xFF) &&
			(ct365->pts[i].status == 1 || ct365->pts[i].status == 2)){
			x = (ct365->pts[i].xhi<<4)|(ct365->pts[i].xlo&0xF);
			y = (ct365->pts[i].yhi<<4)|(ct365->pts[i].ylo&0xF);
			ct365->x = ts->orientation[0] * x + ts->orientation[1] * y;
			ct365->y = ts->orientation[2] * x + ts->orientation[3] * y;
			if(ct365->x < 0)
				ct365->x = ts->x_max - ct365->x;
			if(ct365->y < 0)
				ct365->y = ts->y_max - ct365->y;

			input_mt_slot(ts->input, ct365->pts[i].id - 1);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, true);
			input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, 1);
			input_report_abs(ts->input, ABS_MT_POSITION_X, x);
			input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
			input_report_abs(ts->input, ABS_MT_PRESSURE, ct365->pts[i].pressure);

			sync = 1;
			ct365->press |= 0x01 << (ct365->pts[i].id - 1);
		}
	}
	ct365->release &= ct365->release ^ ct365->press;
	for(i = 0; i < ts->point_num; i++){
		if ( ct365->release & (0x01<<i) ) {
			input_mt_slot(ts->input, i);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
			sync = 1;
		}
	}
	ct365->release = ct365->press;

	if(sync)
		input_sync(ts->input);

	return;
}
struct ct36x_ops ct365_ops = {
	.init = ct365_init,
	.deinit = ct365_deinit,
	.suspend = ct365_suspend,
	.resume = ct365_resume,
	.report = ct365_report,
};
