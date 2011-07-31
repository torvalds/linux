/*
 * Copyright (C) 2008-2009 QUALCOMM Incorporated.
 */

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <media/msm_camera.h>
#include <mach/gpio.h>
#include <mach/camera.h>
#include <asm/mach-types.h>
#include "mt9t013.h"

/*=============================================================
	SENSOR REGISTER DEFINES
==============================================================*/
#define MT9T013_REG_MODEL_ID 		 0x0000
#define MT9T013_MODEL_ID     		 0x2600
#define REG_GROUPED_PARAMETER_HOLD   0x0104
#define GROUPED_PARAMETER_HOLD       0x0100
#define GROUPED_PARAMETER_UPDATE     0x0000
#define REG_COARSE_INT_TIME          0x3012
#define REG_VT_PIX_CLK_DIV           0x0300
#define REG_VT_SYS_CLK_DIV           0x0302
#define REG_PRE_PLL_CLK_DIV          0x0304
#define REG_PLL_MULTIPLIER           0x0306
#define REG_OP_PIX_CLK_DIV           0x0308
#define REG_OP_SYS_CLK_DIV           0x030A
#define REG_SCALE_M                  0x0404
#define REG_FRAME_LENGTH_LINES       0x300A
#define REG_LINE_LENGTH_PCK          0x300C
#define REG_X_ADDR_START             0x3004
#define REG_Y_ADDR_START             0x3002
#define REG_X_ADDR_END               0x3008
#define REG_Y_ADDR_END               0x3006
#define REG_X_OUTPUT_SIZE            0x034C
#define REG_Y_OUTPUT_SIZE            0x034E
#define REG_FINE_INT_TIME            0x3014
#define REG_ROW_SPEED                0x3016
#define MT9T013_REG_RESET_REGISTER   0x301A
#define MT9T013_RESET_REGISTER_PWON  0x10CC
#define MT9T013_RESET_REGISTER_PWOFF 0x1008 /* 0x10C8 stop streaming*/
#define REG_READ_MODE                0x3040
#define REG_GLOBAL_GAIN              0x305E
#define REG_TEST_PATTERN_MODE        0x3070


enum mt9t013_test_mode {
	TEST_OFF,
	TEST_1,
	TEST_2,
	TEST_3
};

enum mt9t013_resolution {
	QTR_SIZE,
	FULL_SIZE,
	INVALID_SIZE
};

enum mt9t013_reg_update {
	REG_INIT, /* registers that need to be updated during initialization */
	UPDATE_PERIODIC, /* registers that needs periodic I2C writes */
	UPDATE_ALL, /* all registers will be updated */
	UPDATE_INVALID
};

enum mt9t013_setting {
	RES_PREVIEW,
	RES_CAPTURE
};

/* actuator's Slave Address */
#define MT9T013_AF_I2C_ADDR   0x18

/*
* AF Total steps parameters
*/
#define MT9T013_TOTAL_STEPS_NEAR_TO_FAR    30

/*
 * Time in milisecs for waiting for the sensor to reset.
 */
#define MT9T013_RESET_DELAY_MSECS   66

/* for 30 fps preview */
#define MT9T013_DEFAULT_CLOCK_RATE  24000000
#define MT9T013_DEFAULT_MAX_FPS     26


/* FIXME: Changes from here */
struct mt9t013_work {
	struct work_struct work;
};

static struct  mt9t013_work *mt9t013_sensorw;
static struct  i2c_client *mt9t013_client;

struct mt9t013_ctrl {
	const struct msm_camera_sensor_info *sensordata;

	int sensormode;
	uint32_t fps_divider; 		/* init to 1 * 0x00000400 */
	uint32_t pict_fps_divider; 	/* init to 1 * 0x00000400 */

	uint16_t curr_lens_pos;
	uint16_t init_curr_lens_pos;
	uint16_t my_reg_gain;
	uint32_t my_reg_line_count;

	enum mt9t013_resolution prev_res;
	enum mt9t013_resolution pict_res;
	enum mt9t013_resolution curr_res;
	enum mt9t013_test_mode  set_test;

	unsigned short imgaddr;
};


static struct mt9t013_ctrl *mt9t013_ctrl;
static DECLARE_WAIT_QUEUE_HEAD(mt9t013_wait_queue);
DECLARE_MUTEX(mt9t013_sem);

extern struct mt9t013_reg mt9t013_regs; /* from mt9t013_reg.c */

static int mt9t013_i2c_rxdata(unsigned short saddr,
	unsigned char *rxdata, int length)
{
	struct i2c_msg msgs[] = {
	{
		.addr   = saddr,
		.flags = 0,
		.len   = 2,
		.buf   = rxdata,
	},
	{
		.addr  = saddr,
		.flags = I2C_M_RD,
		.len   = length,
		.buf   = rxdata,
	},
	};

	if (i2c_transfer(mt9t013_client->adapter, msgs, 2) < 0) {
		pr_err("mt9t013_i2c_rxdata failed!\n");
		return -EIO;
	}

	return 0;
}

static int32_t mt9t013_i2c_read_w(unsigned short saddr,
	unsigned short raddr, unsigned short *rdata)
{
	int32_t rc = 0;
	unsigned char buf[4];

	if (!rdata)
		return -EIO;

	memset(buf, 0, sizeof(buf));

	buf[0] = (raddr & 0xFF00)>>8;
	buf[1] = (raddr & 0x00FF);

	rc = mt9t013_i2c_rxdata(saddr, buf, 2);
	if (rc < 0)
		return rc;

	*rdata = buf[0] << 8 | buf[1];

	if (rc < 0)
		pr_err("mt9t013_i2c_read failed!\n");

	return rc;
}

static int32_t mt9t013_i2c_txdata(unsigned short saddr,
	unsigned char *txdata, int length)
{
	struct i2c_msg msg[] = {
	{
		.addr = saddr,
		.flags = 0,
		.len = length,
		.buf = txdata,
	},
	};

	if (i2c_transfer(mt9t013_client->adapter, msg, 1) < 0) {
		pr_err("mt9t013_i2c_txdata failed\n");
		return -EIO;
	}

	return 0;
}

static int32_t mt9t013_i2c_write_b(unsigned short saddr,
	unsigned short waddr, unsigned short wdata)
{
	int32_t rc = -EIO;
	unsigned char buf[2];

	memset(buf, 0, sizeof(buf));
	buf[0] = waddr;
	buf[1] = wdata;
	rc = mt9t013_i2c_txdata(saddr, buf, 2);

	if (rc < 0)
		pr_err("i2c_write failed, addr = 0x%x, val = 0x%x!\n",
		waddr, wdata);

	return rc;
}

static int32_t mt9t013_i2c_write_w(unsigned short saddr,
	unsigned short waddr, unsigned short wdata)
{
	int32_t rc = -EIO;
	unsigned char buf[4];

	memset(buf, 0, sizeof(buf));
	buf[0] = (waddr & 0xFF00)>>8;
	buf[1] = (waddr & 0x00FF);
	buf[2] = (wdata & 0xFF00)>>8;
	buf[3] = (wdata & 0x00FF);

	rc = mt9t013_i2c_txdata(saddr, buf, 4);

	if (rc < 0)
		pr_err("i2c_write_w failed, addr = 0x%x, val = 0x%x!\n",
		waddr, wdata);

	return rc;
}

static int32_t mt9t013_i2c_write_w_table(
	struct mt9t013_i2c_reg_conf *reg_conf_tbl, int num_of_items_in_table)
{
	int i;
	int32_t rc = -EIO;

	for (i = 0; i < num_of_items_in_table; i++) {
		rc = mt9t013_i2c_write_w(mt9t013_client->addr,
			reg_conf_tbl->waddr, reg_conf_tbl->wdata);
		if (rc < 0)
			break;
		reg_conf_tbl++;
	}

	return rc;
}

static int32_t mt9t013_test(enum mt9t013_test_mode mo)
{
	int32_t rc = 0;

	rc = mt9t013_i2c_write_w(mt9t013_client->addr,
			REG_GROUPED_PARAMETER_HOLD,
			GROUPED_PARAMETER_HOLD);
	if (rc < 0)
		return rc;

	if (mo == TEST_OFF)
		return 0;
	else {
		rc = mt9t013_i2c_write_w_table(mt9t013_regs.ttbl,
				mt9t013_regs.ttbl_size);
		if (rc < 0)
			return rc;
		rc = mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_TEST_PATTERN_MODE, (uint16_t)mo);
		if (rc < 0)
			return rc;
	}

	rc = mt9t013_i2c_write_w(mt9t013_client->addr,
			REG_GROUPED_PARAMETER_HOLD,
			GROUPED_PARAMETER_UPDATE);
	if (rc < 0)
		return rc;

	return rc;
}

static int32_t mt9t013_set_lc(void)
{
	int32_t rc;

	rc = mt9t013_i2c_write_w_table(mt9t013_regs.lctbl, mt9t013_regs.lctbl_size);
	if (rc < 0)
		return rc;

	return rc;
}

static int32_t mt9t013_set_default_focus(uint8_t af_step)
{
	int32_t rc = 0;
	uint8_t code_val_msb, code_val_lsb;
	code_val_msb = 0x01;
	code_val_lsb = af_step;

	/* Write the digital code for current to the actuator */
	rc = mt9t013_i2c_write_b(MT9T013_AF_I2C_ADDR>>1,
			code_val_msb, code_val_lsb);

	mt9t013_ctrl->curr_lens_pos = 0;
	mt9t013_ctrl->init_curr_lens_pos = 0;
	return rc;
}

static void mt9t013_get_pict_fps(uint16_t fps, uint16_t *pfps)
{
	/* input fps is preview fps in Q8 format */
	uint32_t divider;   /*Q10 */
	uint32_t pclk_mult; /*Q10 */

	if (mt9t013_ctrl->prev_res == QTR_SIZE) {
		divider =
			(uint32_t)(
		((mt9t013_regs.reg_pat[RES_PREVIEW].frame_length_lines *
		mt9t013_regs.reg_pat[RES_PREVIEW].line_length_pck) *
		0x00000400) /
		(mt9t013_regs.reg_pat[RES_CAPTURE].frame_length_lines *
		mt9t013_regs.reg_pat[RES_CAPTURE].line_length_pck));

		pclk_mult =
		(uint32_t) ((mt9t013_regs.reg_pat[RES_CAPTURE].pll_multiplier *
		0x00000400) /
		(mt9t013_regs.reg_pat[RES_PREVIEW].pll_multiplier));

	} else {
		/* full size resolution used for preview. */
		divider   = 0x00000400;  /*1.0 */
		pclk_mult = 0x00000400;  /*1.0 */
	}

	/* Verify PCLK settings and frame sizes. */
	*pfps =
		(uint16_t) (fps * divider * pclk_mult /
		0x00000400 / 0x00000400);
}

static uint16_t mt9t013_get_prev_lines_pf(void)
{
	if (mt9t013_ctrl->prev_res == QTR_SIZE)
		return mt9t013_regs.reg_pat[RES_PREVIEW].frame_length_lines;
	else
		return mt9t013_regs.reg_pat[RES_CAPTURE].frame_length_lines;
}

static uint16_t mt9t013_get_prev_pixels_pl(void)
{
	if (mt9t013_ctrl->prev_res == QTR_SIZE)
		return mt9t013_regs.reg_pat[RES_PREVIEW].line_length_pck;
	else
		return mt9t013_regs.reg_pat[RES_CAPTURE].line_length_pck;
}

static uint16_t mt9t013_get_pict_lines_pf(void)
{
	return mt9t013_regs.reg_pat[RES_CAPTURE].frame_length_lines;
}

static uint16_t mt9t013_get_pict_pixels_pl(void)
{
	return mt9t013_regs.reg_pat[RES_CAPTURE].line_length_pck;
}

static uint32_t mt9t013_get_pict_max_exp_lc(void)
{
	uint16_t snapshot_lines_per_frame;

	if (mt9t013_ctrl->pict_res == QTR_SIZE) {
		snapshot_lines_per_frame =
		mt9t013_regs.reg_pat[RES_PREVIEW].frame_length_lines - 1;
	} else  {
		snapshot_lines_per_frame =
		mt9t013_regs.reg_pat[RES_CAPTURE].frame_length_lines - 1;
	}

	return snapshot_lines_per_frame * 24;
}

static int32_t mt9t013_set_fps(struct fps_cfg *fps)
{
	/* input is new fps in Q8 format */
	int32_t rc = 0;

	mt9t013_ctrl->fps_divider = fps->fps_div;
	mt9t013_ctrl->pict_fps_divider = fps->pict_fps_div;

	rc = mt9t013_i2c_write_w(mt9t013_client->addr,
			REG_GROUPED_PARAMETER_HOLD,
			GROUPED_PARAMETER_HOLD);
	if (rc < 0)
		return -EBUSY;

	CDBG("mt9t013_set_fps: fps_div is %d, frame_rate is %d\n",
			fps->fps_div,
			(uint16_t) (mt9t013_regs.reg_pat[RES_PREVIEW].
						frame_length_lines *
					fps->fps_div/0x00000400));

	CDBG("mt9t013_set_fps: fps_mult is %d, frame_rate is %d\n",
			fps->f_mult,
			(uint16_t)(mt9t013_regs.reg_pat[RES_PREVIEW].
					line_length_pck *
					fps->f_mult / 0x00000400));

	rc = mt9t013_i2c_write_w(mt9t013_client->addr,
			REG_LINE_LENGTH_PCK,
			(uint16_t) (
			mt9t013_regs.reg_pat[RES_PREVIEW].line_length_pck *
			fps->f_mult / 0x00000400));
	if (rc < 0)
		return rc;

	rc = mt9t013_i2c_write_w(mt9t013_client->addr,
			REG_GROUPED_PARAMETER_HOLD,
			GROUPED_PARAMETER_UPDATE);
	if (rc < 0)
		return rc;

	return rc;
}

static int32_t mt9t013_write_exp_gain(uint16_t gain, uint32_t line)
{
	const uint16_t max_legal_gain = 0x01FF;
	uint32_t line_length_ratio = 0x00000400;
	enum mt9t013_setting setting;
	int32_t rc = 0;

	if (mt9t013_ctrl->sensormode == SENSOR_PREVIEW_MODE) {
		mt9t013_ctrl->my_reg_gain = gain;
		mt9t013_ctrl->my_reg_line_count = (uint16_t) line;
	}

	if (gain > max_legal_gain)
		gain = max_legal_gain;

	/* Verify no overflow */
	if (mt9t013_ctrl->sensormode != SENSOR_SNAPSHOT_MODE) {
		line = (uint32_t) (line * mt9t013_ctrl->fps_divider /
			0x00000400);

		setting = RES_PREVIEW;

	} else {
		line = (uint32_t) (line * mt9t013_ctrl->pict_fps_divider /
			0x00000400);

		setting = RES_CAPTURE;
	}

	/*Set digital gain to 1 */
	gain |= 0x0200;

	if ((mt9t013_regs.reg_pat[setting].frame_length_lines - 1) < line) {

		line_length_ratio =
		(uint32_t) (line * 0x00000400) /
		(mt9t013_regs.reg_pat[setting].frame_length_lines - 1);
	} else
		line_length_ratio = 0x00000400;

	/* There used to be PARAMETER_HOLD register write before and
	 * after REG_GLOBAL_GAIN & REG_COARSE_INIT_TIME. This causes
	 * aec oscillation. Hence removed. */

	rc = mt9t013_i2c_write_w(mt9t013_client->addr, REG_GLOBAL_GAIN, gain);
	if (rc < 0)
		return rc;

	rc = mt9t013_i2c_write_w(mt9t013_client->addr,
			REG_COARSE_INT_TIME,
			(uint16_t)((uint32_t) line * 0x00000400 /
			line_length_ratio));
	if (rc < 0)
		return rc;

	return rc;
}

static int32_t mt9t013_set_pict_exp_gain(uint16_t gain, uint32_t line)
{
	int32_t rc = 0;

	rc = mt9t013_write_exp_gain(gain, line);
	if (rc < 0)
		return rc;

	rc = mt9t013_i2c_write_w(mt9t013_client->addr,
			MT9T013_REG_RESET_REGISTER,
			0x10CC | 0x0002);

	mdelay(5);

	/* camera_timed_wait(snapshot_wait*exposure_ratio); */
	return rc;
}

static int32_t mt9t013_setting(enum mt9t013_reg_update rupdate,
	enum mt9t013_setting rt)
{
	int32_t rc = 0;

	switch (rupdate) {
	case UPDATE_PERIODIC: {

	if (rt == RES_PREVIEW || rt == RES_CAPTURE) {
#if 0
		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				MT9T013_REG_RESET_REGISTER,
				MT9T013_RESET_REGISTER_PWOFF);
		if (rc < 0)
			return rc;
#endif

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_VT_PIX_CLK_DIV,
				mt9t013_regs.reg_pat[rt].vt_pix_clk_div);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_VT_SYS_CLK_DIV,
				mt9t013_regs.reg_pat[rt].vt_sys_clk_div);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_PRE_PLL_CLK_DIV,
				mt9t013_regs.reg_pat[rt].pre_pll_clk_div);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_PLL_MULTIPLIER,
				mt9t013_regs.reg_pat[rt].pll_multiplier);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_OP_PIX_CLK_DIV,
				mt9t013_regs.reg_pat[rt].op_pix_clk_div);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_OP_SYS_CLK_DIV,
				mt9t013_regs.reg_pat[rt].op_sys_clk_div);
		if (rc < 0)
			return rc;

		mdelay(5);

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_GROUPED_PARAMETER_HOLD,
				GROUPED_PARAMETER_HOLD);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_ROW_SPEED,
				mt9t013_regs.reg_pat[rt].row_speed);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_X_ADDR_START,
				mt9t013_regs.reg_pat[rt].x_addr_start);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_X_ADDR_END,
				mt9t013_regs.reg_pat[rt].x_addr_end);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_Y_ADDR_START,
				mt9t013_regs.reg_pat[rt].y_addr_start);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_Y_ADDR_END,
				mt9t013_regs.reg_pat[rt].y_addr_end);
		if (rc < 0)
			return rc;

		if (machine_is_sapphire()) {
			if (rt == 0)
				rc = mt9t013_i2c_write_w(mt9t013_client->addr,
					REG_READ_MODE,
					0x046F);
			else
				rc = mt9t013_i2c_write_w(mt9t013_client->addr,
					REG_READ_MODE,
					0x0027);
		} else
			rc = mt9t013_i2c_write_w(mt9t013_client->addr,
					REG_READ_MODE,
					mt9t013_regs.reg_pat[rt].read_mode);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_SCALE_M,
				mt9t013_regs.reg_pat[rt].scale_m);
		if (rc < 0)
			return rc;


		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_X_OUTPUT_SIZE,
				mt9t013_regs.reg_pat[rt].x_output_size);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_Y_OUTPUT_SIZE,
				mt9t013_regs.reg_pat[rt].y_output_size);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_LINE_LENGTH_PCK,
				mt9t013_regs.reg_pat[rt].line_length_pck);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
			REG_FRAME_LENGTH_LINES,
			(mt9t013_regs.reg_pat[rt].frame_length_lines *
			mt9t013_ctrl->fps_divider / 0x00000400));
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
			REG_COARSE_INT_TIME,
			mt9t013_regs.reg_pat[rt].coarse_int_time);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
			REG_FINE_INT_TIME,
			mt9t013_regs.reg_pat[rt].fine_int_time);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
			REG_GROUPED_PARAMETER_HOLD,
			GROUPED_PARAMETER_UPDATE);
		if (rc < 0)
			return rc;

		rc = mt9t013_test(mt9t013_ctrl->set_test);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
			MT9T013_REG_RESET_REGISTER,
			MT9T013_RESET_REGISTER_PWON);
		if (rc < 0)
			return rc;

		mdelay(5);

		return rc;
	}
	}
		break;

	/*CAMSENSOR_REG_UPDATE_PERIODIC */
	case REG_INIT: {
	if (rt == RES_PREVIEW || rt == RES_CAPTURE) {

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				MT9T013_REG_RESET_REGISTER,
				MT9T013_RESET_REGISTER_PWOFF);
		if (rc < 0)
			/* MODE_SELECT, stop streaming */
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_VT_PIX_CLK_DIV,
				mt9t013_regs.reg_pat[rt].vt_pix_clk_div);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_VT_SYS_CLK_DIV,
				mt9t013_regs.reg_pat[rt].vt_sys_clk_div);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_PRE_PLL_CLK_DIV,
				mt9t013_regs.reg_pat[rt].pre_pll_clk_div);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_PLL_MULTIPLIER,
				mt9t013_regs.reg_pat[rt].pll_multiplier);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_OP_PIX_CLK_DIV,
				mt9t013_regs.reg_pat[rt].op_pix_clk_div);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_OP_SYS_CLK_DIV,
				mt9t013_regs.reg_pat[rt].op_sys_clk_div);
		if (rc < 0)
			return rc;

		mdelay(5);

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_GROUPED_PARAMETER_HOLD,
				GROUPED_PARAMETER_HOLD);
		if (rc < 0)
			return rc;

		/* additional power saving mode ok around 38.2MHz */
		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				0x3084, 0x2409);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				0x3092, 0x0A49);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				0x3094, 0x4949);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				0x3096, 0x4949);
		if (rc < 0)
			return rc;

		/* Set preview or snapshot mode */
		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_ROW_SPEED,
				mt9t013_regs.reg_pat[rt].row_speed);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_X_ADDR_START,
				mt9t013_regs.reg_pat[rt].x_addr_start);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_X_ADDR_END,
				mt9t013_regs.reg_pat[rt].x_addr_end);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_Y_ADDR_START,
				mt9t013_regs.reg_pat[rt].y_addr_start);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_Y_ADDR_END,
				mt9t013_regs.reg_pat[rt].y_addr_end);
		if (rc < 0)
			return rc;

		if (machine_is_sapphire()) {
			if (rt == 0)
				rc = mt9t013_i2c_write_w(mt9t013_client->addr,
					REG_READ_MODE,
					0x046F);
			else
				rc = mt9t013_i2c_write_w(mt9t013_client->addr,
					REG_READ_MODE,
					0x0027);
		} else
			rc = mt9t013_i2c_write_w(mt9t013_client->addr,
					REG_READ_MODE,
					mt9t013_regs.reg_pat[rt].read_mode);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_SCALE_M,
				mt9t013_regs.reg_pat[rt].scale_m);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_X_OUTPUT_SIZE,
				mt9t013_regs.reg_pat[rt].x_output_size);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_Y_OUTPUT_SIZE,
				mt9t013_regs.reg_pat[rt].y_output_size);
		if (rc < 0)
			return 0;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_LINE_LENGTH_PCK,
				mt9t013_regs.reg_pat[rt].line_length_pck);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_FRAME_LENGTH_LINES,
				mt9t013_regs.reg_pat[rt].frame_length_lines);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_COARSE_INT_TIME,
				mt9t013_regs.reg_pat[rt].coarse_int_time);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_FINE_INT_TIME,
				mt9t013_regs.reg_pat[rt].fine_int_time);
		if (rc < 0)
			return rc;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_GROUPED_PARAMETER_HOLD,
				GROUPED_PARAMETER_UPDATE);
			if (rc < 0)
				return rc;

		/* load lens shading */
		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_GROUPED_PARAMETER_HOLD,
				GROUPED_PARAMETER_HOLD);
		if (rc < 0)
			return rc;

		/* most likely needs to be written only once. */
		rc = mt9t013_set_lc();
		if (rc < 0)
			return -EBUSY;

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_GROUPED_PARAMETER_HOLD,
				GROUPED_PARAMETER_UPDATE);
		if (rc < 0)
			return rc;

		rc = mt9t013_test(mt9t013_ctrl->set_test);
		if (rc < 0)
			return rc;

		mdelay(5);

		rc =
			mt9t013_i2c_write_w(mt9t013_client->addr,
				MT9T013_REG_RESET_REGISTER,
				MT9T013_RESET_REGISTER_PWON);
		if (rc < 0)
			/* MODE_SELECT, stop streaming */
			return rc;

		CDBG("!!! mt9t013 !!! PowerOn is done!\n");
		mdelay(5);
		return rc;
		}
	} /* case CAMSENSOR_REG_INIT: */
	break;

	/*CAMSENSOR_REG_INIT */
	default:
		rc = -EINVAL;
		break;
	} /* switch (rupdate) */

	return rc;
}

static int32_t mt9t013_video_config(int mode, int res)
{
	int32_t rc;

	switch (res) {
	case QTR_SIZE:
		rc = mt9t013_setting(UPDATE_PERIODIC, RES_PREVIEW);
		if (rc < 0)
			return rc;
		CDBG("sensor configuration done!\n");
		break;

	case FULL_SIZE:
		rc = mt9t013_setting(UPDATE_PERIODIC, RES_CAPTURE);
		if (rc < 0)
			return rc;
		break;

	default:
		return -EINVAL;
	} /* switch */

	mt9t013_ctrl->prev_res = res;
	mt9t013_ctrl->curr_res = res;
	mt9t013_ctrl->sensormode = mode;

	return mt9t013_write_exp_gain(mt9t013_ctrl->my_reg_gain,
			mt9t013_ctrl->my_reg_line_count);
}

static int32_t mt9t013_snapshot_config(int mode)
{
	int32_t rc = 0;

	rc = mt9t013_setting(UPDATE_PERIODIC, RES_CAPTURE);
	if (rc < 0)
		return rc;

	mt9t013_ctrl->curr_res = mt9t013_ctrl->pict_res;
	mt9t013_ctrl->sensormode = mode;
	return rc;
}

static int32_t mt9t013_raw_snapshot_config(int mode)
{
	int32_t rc = 0;

	rc = mt9t013_setting(UPDATE_PERIODIC, RES_CAPTURE);
	if (rc < 0)
		return rc;

	mt9t013_ctrl->curr_res = mt9t013_ctrl->pict_res;
	mt9t013_ctrl->sensormode = mode;
	return rc;
}

static int32_t mt9t013_power_down(void)
{
	int32_t rc = 0;

	rc = mt9t013_i2c_write_w(mt9t013_client->addr,
			MT9T013_REG_RESET_REGISTER,
			MT9T013_RESET_REGISTER_PWOFF);
	if (rc >= 0)
		mdelay(5);
	return rc;
}

static int32_t mt9t013_move_focus(int direction, int32_t num_steps)
{
	int16_t step_direction;
	int16_t actual_step;
	int16_t next_position;
	int16_t break_steps[4];
	uint8_t code_val_msb, code_val_lsb;
	int16_t i;

	if (num_steps > MT9T013_TOTAL_STEPS_NEAR_TO_FAR)
		num_steps = MT9T013_TOTAL_STEPS_NEAR_TO_FAR;
	else if (num_steps == 0)
		return -EINVAL;

	if (direction == MOVE_NEAR)
		step_direction = 4;
	else if (direction == MOVE_FAR)
		step_direction = -4;
	else
		return -EINVAL;

	if (mt9t013_ctrl->curr_lens_pos < mt9t013_ctrl->init_curr_lens_pos)
		mt9t013_ctrl->curr_lens_pos = mt9t013_ctrl->init_curr_lens_pos;

	actual_step =
		(int16_t) (step_direction *
		(int16_t) num_steps);

	for (i = 0; i < 4; i++)
		break_steps[i] =
			actual_step / 4 * (i + 1) - actual_step / 4 * i;

	for (i = 0; i < 4; i++) {
		next_position =
		(int16_t)
		(mt9t013_ctrl->curr_lens_pos + break_steps[i]);

		if (next_position > 255)
			next_position = 255;
		else if (next_position < 0)
			next_position = 0;

		code_val_msb =
		((next_position >> 4) << 2) |
		((next_position << 4) >> 6);

		code_val_lsb =
		((next_position & 0x03) << 6);

		/* Writing the digital code for current to the actuator */
		if (mt9t013_i2c_write_b(MT9T013_AF_I2C_ADDR>>1,
				code_val_msb, code_val_lsb) < 0)
			return -EBUSY;

		/* Storing the current lens Position */
		mt9t013_ctrl->curr_lens_pos = next_position;

		if (i < 3)
			mdelay(1);
	} /* for */

	return 0;
}

static int mt9t013_sensor_init_done(const struct msm_camera_sensor_info *data)
{
	gpio_direction_output(data->sensor_reset, 0);
	gpio_free(data->sensor_reset);
	return 0;
}

static int mt9t013_probe_init_sensor(const struct msm_camera_sensor_info *data)
{
	int rc;
	uint16_t chipid;

	rc = gpio_request(data->sensor_reset, "mt9t013");
	if (!rc)
		gpio_direction_output(data->sensor_reset, 1);
	else
		goto init_probe_done;

	mdelay(20);

	/* RESET the sensor image part via I2C command */
	rc = mt9t013_i2c_write_w(mt9t013_client->addr,
		MT9T013_REG_RESET_REGISTER, 0x1009);
	if (rc < 0)
		goto init_probe_fail;

	/* 3. Read sensor Model ID: */
	rc = mt9t013_i2c_read_w(mt9t013_client->addr,
		MT9T013_REG_MODEL_ID, &chipid);

	if (rc < 0)
		goto init_probe_fail;

	CDBG("mt9t013 model_id = 0x%x\n", chipid);

	/* 4. Compare sensor ID to MT9T012VC ID: */
	if (chipid != MT9T013_MODEL_ID) {
		rc = -ENODEV;
		goto init_probe_fail;
	}

	rc = mt9t013_i2c_write_w(mt9t013_client->addr,
		0x3064, 0x0805);
	if (rc < 0)
		goto init_probe_fail;

	mdelay(MT9T013_RESET_DELAY_MSECS);

	goto init_probe_done;

	/* sensor: output enable */
#if 0
	rc = mt9t013_i2c_write_w(mt9t013_client->addr,
		MT9T013_REG_RESET_REGISTER,
		MT9T013_RESET_REGISTER_PWON);

	/* if this fails, the sensor is not the MT9T013 */
	rc = mt9t013_set_default_focus(0);
#endif

init_probe_fail:
	gpio_direction_output(data->sensor_reset, 0);
	gpio_free(data->sensor_reset);
init_probe_done:
	return rc;
}

static int32_t mt9t013_poweron_af(void)
{
	int32_t rc = 0;

	/* enable AF actuator */
	CDBG("enable AF actuator, gpio = %d\n",
			mt9t013_ctrl->sensordata->vcm_pwd);
	rc = gpio_request(mt9t013_ctrl->sensordata->vcm_pwd, "mt9t013");
	if (!rc) {
		gpio_direction_output(mt9t013_ctrl->sensordata->vcm_pwd, 0);
		mdelay(20);
		rc = mt9t013_set_default_focus(0);
	} else
		pr_err("%s, gpio_request failed (%d)!\n", __func__, rc);
	return rc;
}

static void mt9t013_poweroff_af(void)
{
	gpio_direction_output(mt9t013_ctrl->sensordata->vcm_pwd, 1);
	gpio_free(mt9t013_ctrl->sensordata->vcm_pwd);
}

int mt9t013_sensor_open_init(const struct msm_camera_sensor_info *data)
{
	int32_t  rc;

	mt9t013_ctrl = kzalloc(sizeof(struct mt9t013_ctrl), GFP_KERNEL);
	if (!mt9t013_ctrl) {
		pr_err("mt9t013_init failed!\n");
		rc = -ENOMEM;
		goto init_done;
	}

	mt9t013_ctrl->fps_divider = 1 * 0x00000400;
	mt9t013_ctrl->pict_fps_divider = 1 * 0x00000400;
	mt9t013_ctrl->set_test = TEST_OFF;
	mt9t013_ctrl->prev_res = QTR_SIZE;
	mt9t013_ctrl->pict_res = FULL_SIZE;

	if (data)
		mt9t013_ctrl->sensordata = data;

	/* enable mclk first */
	msm_camio_clk_rate_set(MT9T013_DEFAULT_CLOCK_RATE);
	mdelay(20);

	msm_camio_camif_pad_reg_reset();
	mdelay(20);

	rc = mt9t013_probe_init_sensor(data);
	if (rc < 0)
		goto init_fail;

	if (mt9t013_ctrl->prev_res == QTR_SIZE)
		rc = mt9t013_setting(REG_INIT, RES_PREVIEW);
	else
		rc = mt9t013_setting(REG_INIT, RES_CAPTURE);

	if (rc >= 0)
		rc = mt9t013_poweron_af();

	if (rc < 0)
		goto init_fail;
	else
		goto init_done;

init_fail:
	kfree(mt9t013_ctrl);
init_done:
	return rc;
}

static int mt9t013_init_client(struct i2c_client *client)
{
	/* Initialize the MSM_CAMI2C Chip */
	init_waitqueue_head(&mt9t013_wait_queue);
	return 0;
}


static int32_t mt9t013_set_sensor_mode(int mode, int res)
{
	int32_t rc = 0;
	rc = mt9t013_i2c_write_w(mt9t013_client->addr,
			REG_GROUPED_PARAMETER_HOLD,
			GROUPED_PARAMETER_HOLD);
	if (rc < 0)
		return rc;

	switch (mode) {
	case SENSOR_PREVIEW_MODE:
		rc = mt9t013_video_config(mode, res);
		break;

	case SENSOR_SNAPSHOT_MODE:
		rc = mt9t013_snapshot_config(mode);
		break;

	case SENSOR_RAW_SNAPSHOT_MODE:
		rc = mt9t013_raw_snapshot_config(mode);
		break;

	default:
		return -EINVAL;
	}

	/* FIXME: what should we do if rc < 0? */
	if (rc >= 0)
		return mt9t013_i2c_write_w(mt9t013_client->addr,
				REG_GROUPED_PARAMETER_HOLD,
				GROUPED_PARAMETER_UPDATE);
	return rc;
}

int mt9t013_sensor_config(void __user *argp)
{
	struct sensor_cfg_data cdata;
	long   rc = 0;

	if (copy_from_user(&cdata, (void *)argp,
			sizeof(struct sensor_cfg_data)))
		return -EFAULT;

	down(&mt9t013_sem);

	CDBG("mt9t013_sensor_config: cfgtype = %d\n", cdata.cfgtype);
	switch (cdata.cfgtype) {
	case CFG_GET_PICT_FPS:
		mt9t013_get_pict_fps(cdata.cfg.gfps.prevfps,
				&(cdata.cfg.gfps.pictfps));
		if (copy_to_user((void *)argp,
				&cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_GET_PREV_L_PF:
		cdata.cfg.prevl_pf = mt9t013_get_prev_lines_pf();
		if (copy_to_user((void *)argp,
				&cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_GET_PREV_P_PL:
		cdata.cfg.prevp_pl = mt9t013_get_prev_pixels_pl();
		if (copy_to_user((void *)argp,
				&cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_GET_PICT_L_PF:
		cdata.cfg.pictl_pf = mt9t013_get_pict_lines_pf();
		if (copy_to_user((void *)argp,
				&cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_GET_PICT_P_PL:
		cdata.cfg.pictp_pl =
			mt9t013_get_pict_pixels_pl();

		if (copy_to_user((void *)argp,
				&cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_GET_PICT_MAX_EXP_LC:
		cdata.cfg.pict_max_exp_lc =
			mt9t013_get_pict_max_exp_lc();

		if (copy_to_user((void *)argp,
				&cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_SET_FPS:
	case CFG_SET_PICT_FPS:
		rc = mt9t013_set_fps(&(cdata.cfg.fps));
		break;

	case CFG_SET_EXP_GAIN:
		rc = mt9t013_write_exp_gain(cdata.cfg.exp_gain.gain,
				cdata.cfg.exp_gain.line);
		break;

	case CFG_SET_PICT_EXP_GAIN:
		rc = mt9t013_set_pict_exp_gain(cdata.cfg.exp_gain.gain,
				cdata.cfg.exp_gain.line);
		break;

	case CFG_SET_MODE:
		rc = mt9t013_set_sensor_mode(cdata.mode, cdata.rs);
		break;

	case CFG_PWR_DOWN:
		rc = mt9t013_power_down();
		break;

	case CFG_MOVE_FOCUS:
		rc = mt9t013_move_focus(cdata.cfg.focus.dir,
				cdata.cfg.focus.steps);
		break;

	case CFG_SET_DEFAULT_FOCUS:
		rc = mt9t013_set_default_focus(cdata.cfg.focus.steps);
		break;

	case CFG_GET_AF_MAX_STEPS:
		cdata.max_steps = MT9T013_TOTAL_STEPS_NEAR_TO_FAR;
		if (copy_to_user((void *)argp,
				&cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_SET_EFFECT:
	default:
		rc = -EINVAL;
		break;
	}

	up(&mt9t013_sem);
	return rc;
}

int mt9t013_sensor_release(void)
{
	int rc = -EBADF;

	down(&mt9t013_sem);

	mt9t013_poweroff_af();
	mt9t013_power_down();

	gpio_direction_output(mt9t013_ctrl->sensordata->sensor_reset,
			0);
	gpio_free(mt9t013_ctrl->sensordata->sensor_reset);

	kfree(mt9t013_ctrl);

	up(&mt9t013_sem);
	CDBG("mt9t013_release completed!\n");
	return rc;
}

static int mt9t013_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int rc = 0;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		rc = -ENOTSUPP;
		goto probe_failure;
	}

	mt9t013_sensorw =
		kzalloc(sizeof(struct mt9t013_work), GFP_KERNEL);

	if (!mt9t013_sensorw) {
		rc = -ENOMEM;
		goto probe_failure;
	}

	i2c_set_clientdata(client, mt9t013_sensorw);
	mt9t013_init_client(client);
	mt9t013_client = client;
	mt9t013_client->addr = mt9t013_client->addr >> 1;
	mdelay(50);

	CDBG("i2c probe ok\n");
	return 0;

probe_failure:
	kfree(mt9t013_sensorw);
	mt9t013_sensorw = NULL;
	pr_err("i2c probe failure %d\n", rc);
	return rc;
}

static const struct i2c_device_id mt9t013_i2c_id[] = {
	{ "mt9t013", 0},
	{ }
};

static struct i2c_driver mt9t013_i2c_driver = {
	.id_table = mt9t013_i2c_id,
	.probe  = mt9t013_i2c_probe,
	.remove = __exit_p(mt9t013_i2c_remove),
	.driver = {
		.name = "mt9t013",
	},
};

static int mt9t013_sensor_probe(
		const struct msm_camera_sensor_info *info,
		struct msm_sensor_ctrl *s)
{
	/* We expect this driver to match with the i2c device registered
	 * in the board file immediately. */
	int rc = i2c_add_driver(&mt9t013_i2c_driver);
	if (rc < 0 || mt9t013_client == NULL) {
		rc = -ENOTSUPP;
		goto probe_done;
	}

	/* enable mclk first */
	msm_camio_clk_rate_set(MT9T013_DEFAULT_CLOCK_RATE);
	mdelay(20);

	rc = mt9t013_probe_init_sensor(info);
	if (rc < 0) {
		i2c_del_driver(&mt9t013_i2c_driver);
		goto probe_done;
	}

	s->s_init = mt9t013_sensor_open_init;
	s->s_release = mt9t013_sensor_release;
	s->s_config  = mt9t013_sensor_config;
	mt9t013_sensor_init_done(info);

probe_done:
	return rc;
}

static int __mt9t013_probe(struct platform_device *pdev)
{
	return msm_camera_drv_start(pdev, mt9t013_sensor_probe);
}

static struct platform_driver msm_camera_driver = {
	.probe = __mt9t013_probe,
	.driver = {
		.name = "msm_camera_mt9t013",
		.owner = THIS_MODULE,
	},
};

static int __init mt9t013_init(void)
{
	return platform_driver_register(&msm_camera_driver);
}

module_init(mt9t013_init);
