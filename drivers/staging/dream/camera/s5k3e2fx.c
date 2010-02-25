/*
 * Copyright (C) 2008-2009 QUALCOMM Incorporated.
 */

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <media/msm_camera.h>
#include <mach/gpio.h>
#include <mach/camera.h>
#include "s5k3e2fx.h"

#define S5K3E2FX_REG_MODEL_ID   0x0000
#define S5K3E2FX_MODEL_ID   		0x3E2F

/* PLL Registers */
#define REG_PRE_PLL_CLK_DIV       		0x0305
#define REG_PLL_MULTIPLIER_MSB    		0x0306
#define REG_PLL_MULTIPLIER_LSB    		0x0307
#define REG_VT_PIX_CLK_DIV        		0x0301
#define REG_VT_SYS_CLK_DIV        		0x0303
#define REG_OP_PIX_CLK_DIV        		0x0309
#define REG_OP_SYS_CLK_DIV        		0x030B

/* Data Format Registers */
#define REG_CCP_DATA_FORMAT_MSB   		0x0112
#define REG_CCP_DATA_FORMAT_LSB   		0x0113

/* Output Size */
#define REG_X_OUTPUT_SIZE_MSB     		0x034C
#define REG_X_OUTPUT_SIZE_LSB     		0x034D
#define REG_Y_OUTPUT_SIZE_MSB     		0x034E
#define REG_Y_OUTPUT_SIZE_LSB     		0x034F

/* Binning */
#define REG_X_EVEN_INC            		0x0381
#define REG_X_ODD_INC             		0x0383
#define REG_Y_EVEN_INC            		0x0385
#define REG_Y_ODD_INC             		0x0387
/*Reserved register */
#define REG_BINNING_ENABLE        		0x3014

/* Frame Fotmat */
#define REG_FRAME_LENGTH_LINES_MSB		0x0340
#define REG_FRAME_LENGTH_LINES_LSB		0x0341
#define REG_LINE_LENGTH_PCK_MSB   		0x0342
#define REG_LINE_LENGTH_PCK_LSB   		0x0343

/* MSR setting */
/* Reserved registers */
#define REG_SHADE_CLK_ENABLE      		0x30AC
#define REG_SEL_CCP               		0x30C4
#define REG_VPIX                  		0x3024
#define REG_CLAMP_ON              		0x3015
#define REG_OFFSET                		0x307E

/* CDS timing settings */
/* Reserved registers */
#define REG_LD_START              		0x3000
#define REG_LD_END                		0x3001
#define REG_SL_START              		0x3002
#define REG_SL_END                		0x3003
#define REG_RX_START              		0x3004
#define REG_S1_START              		0x3005
#define REG_S1_END                		0x3006
#define REG_S1S_START             		0x3007
#define REG_S1S_END               		0x3008
#define REG_S3_START              		0x3009
#define REG_S3_END                		0x300A
#define REG_CMP_EN_START          		0x300B
#define REG_CLP_SL_START          		0x300C
#define REG_CLP_SL_END            		0x300D
#define REG_OFF_START             		0x300E
#define REG_RMP_EN_START          		0x300F
#define REG_TX_START              		0x3010
#define REG_TX_END                		0x3011
#define REG_STX_WIDTH             		0x3012
#define REG_TYPE1_AF_ENABLE       		0x3130
#define DRIVER_ENABLED            		0x0001
#define AUTO_START_ENABLED        		0x0010
#define REG_NEW_POSITION          		0x3131
#define REG_3152_RESERVED         		0x3152
#define REG_315A_RESERVED         		0x315A
#define REG_ANALOGUE_GAIN_CODE_GLOBAL_MSB 0x0204
#define REG_ANALOGUE_GAIN_CODE_GLOBAL_LSB 0x0205
#define REG_FINE_INTEGRATION_TIME     		0x0200
#define REG_COARSE_INTEGRATION_TIME   		0x0202
#define REG_COARSE_INTEGRATION_TIME_LSB   0x0203

/* Mode select register */
#define S5K3E2FX_REG_MODE_SELECT  		0x0100
#define S5K3E2FX_MODE_SELECT_STREAM 		0x01   /* start streaming */
#define S5K3E2FX_MODE_SELECT_SW_STANDBY 0x00   /* software standby */
#define S5K3E2FX_REG_SOFTWARE_RESET   0x0103
#define S5K3E2FX_SOFTWARE_RESET     		0x01
#define REG_TEST_PATTERN_MODE     		0x0601

struct reg_struct {
	uint8_t pre_pll_clk_div;               /* 0x0305 */
	uint8_t pll_multiplier_msb;            /* 0x0306 */
	uint8_t pll_multiplier_lsb;            /* 0x0307 */
	uint8_t vt_pix_clk_div;                /* 0x0301 */
	uint8_t vt_sys_clk_div;                /* 0x0303 */
	uint8_t op_pix_clk_div;                /* 0x0309 */
	uint8_t op_sys_clk_div;                /* 0x030B */
	uint8_t ccp_data_format_msb;           /* 0x0112 */
	uint8_t ccp_data_format_lsb;           /* 0x0113 */
	uint8_t x_output_size_msb;             /* 0x034C */
	uint8_t x_output_size_lsb;             /* 0x034D */
	uint8_t y_output_size_msb;             /* 0x034E */
	uint8_t y_output_size_lsb;             /* 0x034F */
	uint8_t x_even_inc;                    /* 0x0381 */
	uint8_t x_odd_inc;                     /* 0x0383 */
	uint8_t y_even_inc;                    /* 0x0385 */
	uint8_t y_odd_inc;                     /* 0x0387 */
	uint8_t binning_enable;                /* 0x3014 */
	uint8_t frame_length_lines_msb;        /* 0x0340 */
	uint8_t frame_length_lines_lsb;        /* 0x0341 */
	uint8_t line_length_pck_msb;           /* 0x0342 */
	uint8_t line_length_pck_lsb;           /* 0x0343 */
	uint8_t shade_clk_enable ;             /* 0x30AC */
	uint8_t sel_ccp;                       /* 0x30C4 */
	uint8_t vpix;                          /* 0x3024 */
	uint8_t clamp_on;                      /* 0x3015 */
	uint8_t offset;                        /* 0x307E */
	uint8_t ld_start;                      /* 0x3000 */
	uint8_t ld_end;                        /* 0x3001 */
	uint8_t sl_start;                      /* 0x3002 */
	uint8_t sl_end;                        /* 0x3003 */
	uint8_t rx_start;                      /* 0x3004 */
	uint8_t s1_start;                      /* 0x3005 */
	uint8_t s1_end;                        /* 0x3006 */
	uint8_t s1s_start;                     /* 0x3007 */
	uint8_t s1s_end;                       /* 0x3008 */
	uint8_t s3_start;                      /* 0x3009 */
	uint8_t s3_end;                        /* 0x300A */
	uint8_t cmp_en_start;                  /* 0x300B */
	uint8_t clp_sl_start;                  /* 0x300C */
	uint8_t clp_sl_end;                    /* 0x300D */
	uint8_t off_start;                     /* 0x300E */
	uint8_t rmp_en_start;                  /* 0x300F */
	uint8_t tx_start;                      /* 0x3010 */
	uint8_t tx_end;                        /* 0x3011 */
	uint8_t stx_width;                     /* 0x3012 */
	uint8_t reg_3152_reserved;             /* 0x3152 */
	uint8_t reg_315A_reserved;             /* 0x315A */
	uint8_t analogue_gain_code_global_msb; /* 0x0204 */
	uint8_t analogue_gain_code_global_lsb; /* 0x0205 */
	uint8_t fine_integration_time;         /* 0x0200 */
	uint8_t coarse_integration_time;       /* 0x0202 */
	uint32_t size_h;
	uint32_t blk_l;
	uint32_t size_w;
	uint32_t blk_p;
};

struct reg_struct s5k3e2fx_reg_pat[2] = {
	{ /* Preview */
		0x06,  /* pre_pll_clk_div       REG=0x0305 */
		0x00,  /* pll_multiplier_msb    REG=0x0306 */
		0x88,  /* pll_multiplier_lsb    REG=0x0307 */
		0x0a,  /* vt_pix_clk_div        REG=0x0301 */
		0x01,  /* vt_sys_clk_div        REG=0x0303 */
		0x0a,  /* op_pix_clk_div        REG=0x0309 */
		0x01,  /* op_sys_clk_div        REG=0x030B */
		0x0a,  /* ccp_data_format_msb   REG=0x0112 */
		0x0a,  /* ccp_data_format_lsb   REG=0x0113 */
		0x05,  /* x_output_size_msb     REG=0x034C */
		0x10,  /* x_output_size_lsb     REG=0x034D */
		0x03,  /* y_output_size_msb     REG=0x034E */
		0xcc,  /* y_output_size_lsb     REG=0x034F */

	/* enable binning for preview */
		0x01,  /* x_even_inc             REG=0x0381 */
		0x01,  /* x_odd_inc              REG=0x0383 */
		0x01,  /* y_even_inc             REG=0x0385 */
		0x03,  /* y_odd_inc              REG=0x0387 */
		0x06,  /* binning_enable         REG=0x3014 */

		0x03,  /* frame_length_lines_msb        REG=0x0340 */
		0xde,  /* frame_length_lines_lsb        REG=0x0341 */
		0x0a,  /* line_length_pck_msb           REG=0x0342 */
		0xac,  /* line_length_pck_lsb           REG=0x0343 */
		0x81,  /* shade_clk_enable              REG=0x30AC */
		0x01,  /* sel_ccp                       REG=0x30C4 */
		0x04,  /* vpix                          REG=0x3024 */
		0x00,  /* clamp_on                      REG=0x3015 */
		0x02,  /* offset                        REG=0x307E */
		0x03,  /* ld_start                      REG=0x3000 */
		0x9c,  /* ld_end                        REG=0x3001 */
		0x02,  /* sl_start                      REG=0x3002 */
		0x9e,  /* sl_end                        REG=0x3003 */
		0x05,  /* rx_start                      REG=0x3004 */
		0x0f,  /* s1_start                      REG=0x3005 */
		0x24,  /* s1_end                        REG=0x3006 */
		0x7c,  /* s1s_start                     REG=0x3007 */
		0x9a,  /* s1s_end                       REG=0x3008 */
		0x10,  /* s3_start                      REG=0x3009 */
		0x14,  /* s3_end                        REG=0x300A */
		0x10,  /* cmp_en_start                  REG=0x300B */
		0x04,  /* clp_sl_start                  REG=0x300C */
		0x26,  /* clp_sl_end                    REG=0x300D */
		0x02,  /* off_start                     REG=0x300E */
		0x0e,  /* rmp_en_start                  REG=0x300F */
		0x30,  /* tx_start                      REG=0x3010 */
		0x4e,  /* tx_end                        REG=0x3011 */
		0x1E,  /* stx_width                     REG=0x3012 */
		0x08,  /* reg_3152_reserved             REG=0x3152 */
		0x10,  /* reg_315A_reserved             REG=0x315A */
		0x00,  /* analogue_gain_code_global_msb REG=0x0204 */
		0x80,  /* analogue_gain_code_global_lsb REG=0x0205 */
		0x02,  /* fine_integration_time         REG=0x0200 */
		0x03,  /* coarse_integration_time       REG=0x0202 */
		972,
		18,
		1296,
		1436
	},
	{ /* Snapshot */
		0x06,  /* pre_pll_clk_div               REG=0x0305 */
		0x00,  /* pll_multiplier_msb            REG=0x0306 */
		0x88,  /* pll_multiplier_lsb            REG=0x0307 */
		0x0a,  /* vt_pix_clk_div                REG=0x0301 */
		0x01,  /* vt_sys_clk_div                REG=0x0303 */
		0x0a,  /* op_pix_clk_div                REG=0x0309 */
		0x01,  /* op_sys_clk_div                REG=0x030B */
		0x0a,  /* ccp_data_format_msb           REG=0x0112 */
		0x0a,  /* ccp_data_format_lsb           REG=0x0113 */
		0x0a,  /* x_output_size_msb             REG=0x034C */
		0x30,  /* x_output_size_lsb             REG=0x034D */
		0x07,  /* y_output_size_msb             REG=0x034E */
		0xa8,  /* y_output_size_lsb             REG=0x034F */

	/* disable binning for snapshot */
		0x01,  /* x_even_inc                    REG=0x0381 */
		0x01,  /* x_odd_inc                     REG=0x0383 */
		0x01,  /* y_even_inc                    REG=0x0385 */
		0x01,  /* y_odd_inc                     REG=0x0387 */
		0x00,  /* binning_enable                REG=0x3014 */

		0x07,  /* frame_length_lines_msb        REG=0x0340 */
		0xb6,  /* frame_length_lines_lsb        REG=0x0341 */
		0x0a,  /* line_length_pck_msb           REG=0x0342 */
		0xac,  /* line_length_pck_lsb           REG=0x0343 */
		0x81,  /* shade_clk_enable              REG=0x30AC */
		0x01,  /* sel_ccp                       REG=0x30C4 */
		0x04,  /* vpix                          REG=0x3024 */
		0x00,  /* clamp_on                      REG=0x3015 */
		0x02,  /* offset                        REG=0x307E */
		0x03,  /* ld_start                      REG=0x3000 */
		0x9c,  /* ld_end                        REG=0x3001 */
		0x02,  /* sl_start                      REG=0x3002 */
		0x9e,  /* sl_end                        REG=0x3003 */
		0x05,  /* rx_start                      REG=0x3004 */
		0x0f,  /* s1_start                      REG=0x3005 */
		0x24,  /* s1_end                        REG=0x3006 */
		0x7c,  /* s1s_start                     REG=0x3007 */
		0x9a,  /* s1s_end                       REG=0x3008 */
		0x10,  /* s3_start                      REG=0x3009 */
		0x14,  /* s3_end                        REG=0x300A */
		0x10,  /* cmp_en_start                  REG=0x300B */
		0x04,  /* clp_sl_start                  REG=0x300C */
		0x26,  /* clp_sl_end                    REG=0x300D */
		0x02,  /* off_start                     REG=0x300E */
		0x0e,  /* rmp_en_start                  REG=0x300F */
		0x30,  /* tx_start                      REG=0x3010 */
		0x4e,  /* tx_end                        REG=0x3011 */
		0x1E,  /* stx_width                     REG=0x3012 */
		0x08,  /* reg_3152_reserved             REG=0x3152 */
		0x10,  /* reg_315A_reserved             REG=0x315A */
		0x00,  /* analogue_gain_code_global_msb REG=0x0204 */
		0x80,  /* analogue_gain_code_global_lsb REG=0x0205 */
		0x02,  /* fine_integration_time         REG=0x0200 */
		0x03,  /* coarse_integration_time       REG=0x0202 */
		1960,
		14,
		2608,
		124
	}
};

struct s5k3e2fx_work {
	struct work_struct work;
};
static struct s5k3e2fx_work *s5k3e2fx_sensorw;
static struct i2c_client *s5k3e2fx_client;

struct s5k3e2fx_ctrl {
	const struct msm_camera_sensor_info *sensordata;

	int sensormode;
	uint32_t fps_divider; /* init to 1 * 0x00000400 */
	uint32_t pict_fps_divider; /* init to 1 * 0x00000400 */

	uint16_t curr_lens_pos;
	uint16_t init_curr_lens_pos;
	uint16_t my_reg_gain;
	uint32_t my_reg_line_count;

	enum msm_s_resolution prev_res;
	enum msm_s_resolution pict_res;
	enum msm_s_resolution curr_res;
	enum msm_s_test_mode  set_test;
};

struct s5k3e2fx_i2c_reg_conf {
	unsigned short waddr;
	unsigned char  bdata;
};

static struct s5k3e2fx_ctrl *s5k3e2fx_ctrl;
static DECLARE_WAIT_QUEUE_HEAD(s5k3e2fx_wait_queue);
DECLARE_MUTEX(s5k3e2fx_sem);

static int s5k3e2fx_i2c_rxdata(unsigned short saddr, unsigned char *rxdata,
	int length)
{
	struct i2c_msg msgs[] = {
		{
			.addr   = saddr,
			.flags = 0,
			.len   = 2,
			.buf   = rxdata,
		},
		{
			.addr   = saddr,
			.flags = I2C_M_RD,
			.len   = length,
			.buf   = rxdata,
		},
	};

	if (i2c_transfer(s5k3e2fx_client->adapter, msgs, 2) < 0) {
		CDBG("s5k3e2fx_i2c_rxdata failed!\n");
		return -EIO;
	}

	return 0;
}

static int32_t s5k3e2fx_i2c_txdata(unsigned short saddr,
	unsigned char *txdata, int length)
{
	struct i2c_msg msg[] = {
		{
		.addr  = saddr,
		.flags = 0,
		.len = length,
		.buf = txdata,
		},
	};

	if (i2c_transfer(s5k3e2fx_client->adapter, msg, 1) < 0) {
		CDBG("s5k3e2fx_i2c_txdata failed\n");
		return -EIO;
	}

	return 0;
}

static int32_t s5k3e2fx_i2c_write_b(unsigned short saddr, unsigned short waddr,
	unsigned char bdata)
{
	int32_t rc = -EIO;
	unsigned char buf[4];

	memset(buf, 0, sizeof(buf));
	buf[0] = (waddr & 0xFF00)>>8;
	buf[1] = (waddr & 0x00FF);
	buf[2] = bdata;

	rc = s5k3e2fx_i2c_txdata(saddr, buf, 3);

	if (rc < 0)
		CDBG("i2c_write_w failed, addr = 0x%x, val = 0x%x!\n",
			waddr, bdata);

	return rc;
}

static int32_t s5k3e2fx_i2c_write_table(
	struct s5k3e2fx_i2c_reg_conf *reg_cfg_tbl, int num)
{
	int i;
	int32_t rc = -EIO;
	for (i = 0; i < num; i++) {
		if (rc < 0)
			break;
		reg_cfg_tbl++;
	}

	return rc;
}

static int32_t s5k3e2fx_i2c_read_w(unsigned short saddr, unsigned short raddr,
	unsigned short *rdata)
{
	int32_t rc = 0;
	unsigned char buf[4];

	if (!rdata)
		return -EIO;

	memset(buf, 0, sizeof(buf));

	buf[0] = (raddr & 0xFF00)>>8;
	buf[1] = (raddr & 0x00FF);

	rc = s5k3e2fx_i2c_rxdata(saddr, buf, 2);
	if (rc < 0)
		return rc;

	*rdata = buf[0] << 8 | buf[1];

	if (rc < 0)
		CDBG("s5k3e2fx_i2c_read failed!\n");

	return rc;
}

static int s5k3e2fx_probe_init_done(const struct msm_camera_sensor_info *data)
{
	gpio_direction_output(data->sensor_reset, 0);
	gpio_free(data->sensor_reset);
	return 0;
}

static int s5k3e2fx_probe_init_sensor(const struct msm_camera_sensor_info *data)
{
	int32_t  rc;
	uint16_t chipid = 0;

	rc = gpio_request(data->sensor_reset, "s5k3e2fx");
	if (!rc)
		gpio_direction_output(data->sensor_reset, 1);
	else
		goto init_probe_done;

	mdelay(20);

	CDBG("s5k3e2fx_sensor_init(): reseting sensor.\n");

	rc = s5k3e2fx_i2c_read_w(s5k3e2fx_client->addr,
		S5K3E2FX_REG_MODEL_ID, &chipid);
	if (rc < 0)
		goto init_probe_fail;

	if (chipid != S5K3E2FX_MODEL_ID) {
		CDBG("S5K3E2FX wrong model_id = 0x%x\n", chipid);
		rc = -ENODEV;
		goto init_probe_fail;
	}

	goto init_probe_done;

init_probe_fail:
	s5k3e2fx_probe_init_done(data);
init_probe_done:
	return rc;
}

static int s5k3e2fx_init_client(struct i2c_client *client)
{
	/* Initialize the MSM_CAMI2C Chip */
	init_waitqueue_head(&s5k3e2fx_wait_queue);
	return 0;
}

static const struct i2c_device_id s5k3e2fx_i2c_id[] = {
	{ "s5k3e2fx", 0},
	{ }
};

static int s5k3e2fx_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int rc = 0;
	CDBG("s5k3e2fx_probe called!\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		CDBG("i2c_check_functionality failed\n");
		goto probe_failure;
	}

	s5k3e2fx_sensorw = kzalloc(sizeof(struct s5k3e2fx_work), GFP_KERNEL);
	if (!s5k3e2fx_sensorw) {
		CDBG("kzalloc failed.\n");
		rc = -ENOMEM;
		goto probe_failure;
	}

	i2c_set_clientdata(client, s5k3e2fx_sensorw);
	s5k3e2fx_init_client(client);
	s5k3e2fx_client = client;

	mdelay(50);

	CDBG("s5k3e2fx_probe successed! rc = %d\n", rc);
	return 0;

probe_failure:
	CDBG("s5k3e2fx_probe failed! rc = %d\n", rc);
	return rc;
}

static struct i2c_driver s5k3e2fx_i2c_driver = {
	.id_table = s5k3e2fx_i2c_id,
	.probe  = s5k3e2fx_i2c_probe,
	.remove = __exit_p(s5k3e2fx_i2c_remove),
	.driver = {
		.name = "s5k3e2fx",
	},
};

static int32_t s5k3e2fx_test(enum msm_s_test_mode mo)
{
	int32_t rc = 0;

	if (mo == S_TEST_OFF)
		rc = 0;
	else
		rc = s5k3e2fx_i2c_write_b(s5k3e2fx_client->addr,
			REG_TEST_PATTERN_MODE, (uint16_t)mo);

	return rc;
}

static int32_t s5k3e2fx_setting(enum msm_s_reg_update rupdate,
	enum msm_s_setting rt)
{
	int32_t rc = 0;
	uint16_t num_lperf;

	switch (rupdate) {
	case S_UPDATE_PERIODIC:
	if (rt == S_RES_PREVIEW || rt == S_RES_CAPTURE) {

		struct s5k3e2fx_i2c_reg_conf tbl_1[] = {
		{REG_CCP_DATA_FORMAT_MSB, s5k3e2fx_reg_pat[rt].ccp_data_format_msb},
		{REG_CCP_DATA_FORMAT_LSB, s5k3e2fx_reg_pat[rt].ccp_data_format_lsb},
		{REG_X_OUTPUT_SIZE_MSB, s5k3e2fx_reg_pat[rt].x_output_size_msb},
		{REG_X_OUTPUT_SIZE_LSB, s5k3e2fx_reg_pat[rt].x_output_size_lsb},
		{REG_Y_OUTPUT_SIZE_MSB, s5k3e2fx_reg_pat[rt].y_output_size_msb},
		{REG_Y_OUTPUT_SIZE_LSB, s5k3e2fx_reg_pat[rt].y_output_size_lsb},
		{REG_X_EVEN_INC, s5k3e2fx_reg_pat[rt].x_even_inc},
		{REG_X_ODD_INC,  s5k3e2fx_reg_pat[rt].x_odd_inc},
		{REG_Y_EVEN_INC, s5k3e2fx_reg_pat[rt].y_even_inc},
		{REG_Y_ODD_INC,  s5k3e2fx_reg_pat[rt].y_odd_inc},
		{REG_BINNING_ENABLE, s5k3e2fx_reg_pat[rt].binning_enable},
		};

		struct s5k3e2fx_i2c_reg_conf tbl_2[] = {
			{REG_FRAME_LENGTH_LINES_MSB, 0},
			{REG_FRAME_LENGTH_LINES_LSB, 0},
			{REG_LINE_LENGTH_PCK_MSB, s5k3e2fx_reg_pat[rt].line_length_pck_msb},
			{REG_LINE_LENGTH_PCK_LSB, s5k3e2fx_reg_pat[rt].line_length_pck_lsb},
			{REG_SHADE_CLK_ENABLE, s5k3e2fx_reg_pat[rt].shade_clk_enable},
			{REG_SEL_CCP, s5k3e2fx_reg_pat[rt].sel_ccp},
			{REG_VPIX, s5k3e2fx_reg_pat[rt].vpix},
			{REG_CLAMP_ON, s5k3e2fx_reg_pat[rt].clamp_on},
			{REG_OFFSET, s5k3e2fx_reg_pat[rt].offset},
			{REG_LD_START, s5k3e2fx_reg_pat[rt].ld_start},
			{REG_LD_END, s5k3e2fx_reg_pat[rt].ld_end},
			{REG_SL_START, s5k3e2fx_reg_pat[rt].sl_start},
			{REG_SL_END, s5k3e2fx_reg_pat[rt].sl_end},
			{REG_RX_START, s5k3e2fx_reg_pat[rt].rx_start},
			{REG_S1_START, s5k3e2fx_reg_pat[rt].s1_start},
			{REG_S1_END, s5k3e2fx_reg_pat[rt].s1_end},
			{REG_S1S_START, s5k3e2fx_reg_pat[rt].s1s_start},
			{REG_S1S_END, s5k3e2fx_reg_pat[rt].s1s_end},
			{REG_S3_START, s5k3e2fx_reg_pat[rt].s3_start},
			{REG_S3_END, s5k3e2fx_reg_pat[rt].s3_end},
			{REG_CMP_EN_START, s5k3e2fx_reg_pat[rt].cmp_en_start},
			{REG_CLP_SL_START, s5k3e2fx_reg_pat[rt].clp_sl_start},
			{REG_CLP_SL_END, s5k3e2fx_reg_pat[rt].clp_sl_end},
			{REG_OFF_START, s5k3e2fx_reg_pat[rt].off_start},
			{REG_RMP_EN_START, s5k3e2fx_reg_pat[rt].rmp_en_start},
			{REG_TX_START, s5k3e2fx_reg_pat[rt].tx_start},
			{REG_TX_END, s5k3e2fx_reg_pat[rt].tx_end},
			{REG_STX_WIDTH, s5k3e2fx_reg_pat[rt].stx_width},
			{REG_3152_RESERVED, s5k3e2fx_reg_pat[rt].reg_3152_reserved},
			{REG_315A_RESERVED, s5k3e2fx_reg_pat[rt].reg_315A_reserved},
			{REG_ANALOGUE_GAIN_CODE_GLOBAL_MSB, s5k3e2fx_reg_pat[rt].analogue_gain_code_global_msb},
			{REG_ANALOGUE_GAIN_CODE_GLOBAL_LSB, s5k3e2fx_reg_pat[rt].analogue_gain_code_global_lsb},
			{REG_FINE_INTEGRATION_TIME, s5k3e2fx_reg_pat[rt].fine_integration_time},
			{REG_COARSE_INTEGRATION_TIME, s5k3e2fx_reg_pat[rt].coarse_integration_time},
			{S5K3E2FX_REG_MODE_SELECT, S5K3E2FX_MODE_SELECT_STREAM},
		};

		rc = s5k3e2fx_i2c_write_table(&tbl_1[0],
			ARRAY_SIZE(tbl_1));
		if (rc < 0)
			return rc;

		num_lperf =
			(uint16_t)((s5k3e2fx_reg_pat[rt].frame_length_lines_msb << 8) & 0xFF00) +
				s5k3e2fx_reg_pat[rt].frame_length_lines_lsb;

		num_lperf = num_lperf * s5k3e2fx_ctrl->fps_divider / 0x0400;

		tbl_2[0] = (struct s5k3e2fx_i2c_reg_conf) {REG_FRAME_LENGTH_LINES_MSB, (num_lperf & 0xFF00) >> 8};
		tbl_2[1] = (struct s5k3e2fx_i2c_reg_conf) {REG_FRAME_LENGTH_LINES_LSB, (num_lperf & 0x00FF)};

		rc = s5k3e2fx_i2c_write_table(&tbl_2[0],
			ARRAY_SIZE(tbl_2));
		if (rc < 0)
			return rc;

		mdelay(5);

		rc = s5k3e2fx_test(s5k3e2fx_ctrl->set_test);
		if (rc < 0)
			return rc;
	}
	break; /* UPDATE_PERIODIC */

	case S_REG_INIT:
	if (rt == S_RES_PREVIEW || rt == S_RES_CAPTURE) {

		struct s5k3e2fx_i2c_reg_conf tbl_3[] = {
			{S5K3E2FX_REG_SOFTWARE_RESET, S5K3E2FX_SOFTWARE_RESET},
			{S5K3E2FX_REG_MODE_SELECT, S5K3E2FX_MODE_SELECT_SW_STANDBY},
			/* PLL setting */
			{REG_PRE_PLL_CLK_DIV, s5k3e2fx_reg_pat[rt].pre_pll_clk_div},
			{REG_PLL_MULTIPLIER_MSB, s5k3e2fx_reg_pat[rt].pll_multiplier_msb},
			{REG_PLL_MULTIPLIER_LSB, s5k3e2fx_reg_pat[rt].pll_multiplier_lsb},
			{REG_VT_PIX_CLK_DIV, s5k3e2fx_reg_pat[rt].vt_pix_clk_div},
			{REG_VT_SYS_CLK_DIV, s5k3e2fx_reg_pat[rt].vt_sys_clk_div},
			{REG_OP_PIX_CLK_DIV, s5k3e2fx_reg_pat[rt].op_pix_clk_div},
			{REG_OP_SYS_CLK_DIV, s5k3e2fx_reg_pat[rt].op_sys_clk_div},
			/*Data Format */
			{REG_CCP_DATA_FORMAT_MSB, s5k3e2fx_reg_pat[rt].ccp_data_format_msb},
			{REG_CCP_DATA_FORMAT_LSB, s5k3e2fx_reg_pat[rt].ccp_data_format_lsb},
			/*Output Size */
			{REG_X_OUTPUT_SIZE_MSB, s5k3e2fx_reg_pat[rt].x_output_size_msb},
			{REG_X_OUTPUT_SIZE_LSB, s5k3e2fx_reg_pat[rt].x_output_size_lsb},
			{REG_Y_OUTPUT_SIZE_MSB, s5k3e2fx_reg_pat[rt].y_output_size_msb},
			{REG_Y_OUTPUT_SIZE_LSB, s5k3e2fx_reg_pat[rt].y_output_size_lsb},
			/* Binning */
			{REG_X_EVEN_INC, s5k3e2fx_reg_pat[rt].x_even_inc},
			{REG_X_ODD_INC, s5k3e2fx_reg_pat[rt].x_odd_inc },
			{REG_Y_EVEN_INC, s5k3e2fx_reg_pat[rt].y_even_inc},
			{REG_Y_ODD_INC, s5k3e2fx_reg_pat[rt].y_odd_inc},
			{REG_BINNING_ENABLE, s5k3e2fx_reg_pat[rt].binning_enable},
			/* Frame format */
			{REG_FRAME_LENGTH_LINES_MSB, s5k3e2fx_reg_pat[rt].frame_length_lines_msb},
			{REG_FRAME_LENGTH_LINES_LSB, s5k3e2fx_reg_pat[rt].frame_length_lines_lsb},
			{REG_LINE_LENGTH_PCK_MSB, s5k3e2fx_reg_pat[rt].line_length_pck_msb},
			{REG_LINE_LENGTH_PCK_LSB, s5k3e2fx_reg_pat[rt].line_length_pck_lsb},
			/* MSR setting */
			{REG_SHADE_CLK_ENABLE, s5k3e2fx_reg_pat[rt].shade_clk_enable},
			{REG_SEL_CCP, s5k3e2fx_reg_pat[rt].sel_ccp},
			{REG_VPIX, s5k3e2fx_reg_pat[rt].vpix},
			{REG_CLAMP_ON, s5k3e2fx_reg_pat[rt].clamp_on},
			{REG_OFFSET, s5k3e2fx_reg_pat[rt].offset},
			/* CDS timing setting */
			{REG_LD_START, s5k3e2fx_reg_pat[rt].ld_start},
			{REG_LD_END, s5k3e2fx_reg_pat[rt].ld_end},
			{REG_SL_START, s5k3e2fx_reg_pat[rt].sl_start},
			{REG_SL_END, s5k3e2fx_reg_pat[rt].sl_end},
			{REG_RX_START, s5k3e2fx_reg_pat[rt].rx_start},
			{REG_S1_START, s5k3e2fx_reg_pat[rt].s1_start},
			{REG_S1_END, s5k3e2fx_reg_pat[rt].s1_end},
			{REG_S1S_START, s5k3e2fx_reg_pat[rt].s1s_start},
			{REG_S1S_END, s5k3e2fx_reg_pat[rt].s1s_end},
			{REG_S3_START, s5k3e2fx_reg_pat[rt].s3_start},
			{REG_S3_END, s5k3e2fx_reg_pat[rt].s3_end},
			{REG_CMP_EN_START, s5k3e2fx_reg_pat[rt].cmp_en_start},
			{REG_CLP_SL_START, s5k3e2fx_reg_pat[rt].clp_sl_start},
			{REG_CLP_SL_END, s5k3e2fx_reg_pat[rt].clp_sl_end},
			{REG_OFF_START, s5k3e2fx_reg_pat[rt].off_start},
			{REG_RMP_EN_START, s5k3e2fx_reg_pat[rt].rmp_en_start},
			{REG_TX_START, s5k3e2fx_reg_pat[rt].tx_start},
			{REG_TX_END, s5k3e2fx_reg_pat[rt].tx_end},
			{REG_STX_WIDTH, s5k3e2fx_reg_pat[rt].stx_width},
			{REG_3152_RESERVED, s5k3e2fx_reg_pat[rt].reg_3152_reserved},
			{REG_315A_RESERVED, s5k3e2fx_reg_pat[rt].reg_315A_reserved},
			{REG_ANALOGUE_GAIN_CODE_GLOBAL_MSB, s5k3e2fx_reg_pat[rt].analogue_gain_code_global_msb},
			{REG_ANALOGUE_GAIN_CODE_GLOBAL_LSB, s5k3e2fx_reg_pat[rt].analogue_gain_code_global_lsb},
			{REG_FINE_INTEGRATION_TIME, s5k3e2fx_reg_pat[rt].fine_integration_time},
			{REG_COARSE_INTEGRATION_TIME, s5k3e2fx_reg_pat[rt].coarse_integration_time},
			{S5K3E2FX_REG_MODE_SELECT, S5K3E2FX_MODE_SELECT_STREAM},
		};

		/* reset fps_divider */
		s5k3e2fx_ctrl->fps_divider = 1 * 0x0400;
		rc = s5k3e2fx_i2c_write_table(&tbl_3[0],
			ARRAY_SIZE(tbl_3));
		if (rc < 0)
			return rc;
	}
	break; /* case REG_INIT: */

	default:
		rc = -EINVAL;
		break;
	} /* switch (rupdate) */

	return rc;
}

static int s5k3e2fx_sensor_open_init(const struct msm_camera_sensor_info *data)
{
	int32_t  rc;

	s5k3e2fx_ctrl = kzalloc(sizeof(struct s5k3e2fx_ctrl), GFP_KERNEL);
	if (!s5k3e2fx_ctrl) {
		CDBG("s5k3e2fx_init failed!\n");
		rc = -ENOMEM;
		goto init_done;
	}

	s5k3e2fx_ctrl->fps_divider = 1 * 0x00000400;
	s5k3e2fx_ctrl->pict_fps_divider = 1 * 0x00000400;
	s5k3e2fx_ctrl->set_test = S_TEST_OFF;
	s5k3e2fx_ctrl->prev_res = S_QTR_SIZE;
	s5k3e2fx_ctrl->pict_res = S_FULL_SIZE;

	if (data)
		s5k3e2fx_ctrl->sensordata = data;

	/* enable mclk first */
	msm_camio_clk_rate_set(24000000);
	mdelay(20);

	msm_camio_camif_pad_reg_reset();
	mdelay(20);

	rc = s5k3e2fx_probe_init_sensor(data);
	if (rc < 0)
		goto init_fail1;

	if (s5k3e2fx_ctrl->prev_res == S_QTR_SIZE)
		rc = s5k3e2fx_setting(S_REG_INIT, S_RES_PREVIEW);
	else
		rc = s5k3e2fx_setting(S_REG_INIT, S_RES_CAPTURE);

	if (rc < 0) {
		CDBG("s5k3e2fx_setting failed. rc = %d\n", rc);
		goto init_fail1;
	}

	/* initialize AF */
	rc = s5k3e2fx_i2c_write_b(s5k3e2fx_client->addr, 0x3146, 0x3A);
	if (rc < 0)
		goto init_fail1;

	rc = s5k3e2fx_i2c_write_b(s5k3e2fx_client->addr, 0x3130, 0x03);
	if (rc < 0)
		goto init_fail1;

	goto init_done;

init_fail1:
	s5k3e2fx_probe_init_done(data);
	kfree(s5k3e2fx_ctrl);
init_done:
	return rc;
}

static int32_t s5k3e2fx_power_down(void)
{
	int32_t rc = 0;
	return rc;
}

static int s5k3e2fx_sensor_release(void)
{
	int rc = -EBADF;

	down(&s5k3e2fx_sem);

	s5k3e2fx_power_down();

	gpio_direction_output(s5k3e2fx_ctrl->sensordata->sensor_reset,
		0);
	gpio_free(s5k3e2fx_ctrl->sensordata->sensor_reset);

	kfree(s5k3e2fx_ctrl);
	s5k3e2fx_ctrl = NULL;

	CDBG("s5k3e2fx_release completed\n");

	up(&s5k3e2fx_sem);
	return rc;
}

static void s5k3e2fx_get_pict_fps(uint16_t fps, uint16_t *pfps)
{
	/* input fps is preview fps in Q8 format */
	uint32_t divider;   /* Q10 */

	divider = (uint32_t)
		((s5k3e2fx_reg_pat[S_RES_PREVIEW].size_h +
			s5k3e2fx_reg_pat[S_RES_PREVIEW].blk_l) *
		 (s5k3e2fx_reg_pat[S_RES_PREVIEW].size_w +
			s5k3e2fx_reg_pat[S_RES_PREVIEW].blk_p)) * 0x00000400 /
		((s5k3e2fx_reg_pat[S_RES_CAPTURE].size_h +
			s5k3e2fx_reg_pat[S_RES_CAPTURE].blk_l) *
		 (s5k3e2fx_reg_pat[S_RES_CAPTURE].size_w +
			s5k3e2fx_reg_pat[S_RES_CAPTURE].blk_p));

	/* Verify PCLK settings and frame sizes. */
	*pfps = (uint16_t)(fps * divider / 0x00000400);
}

static uint16_t s5k3e2fx_get_prev_lines_pf(void)
{
	return (s5k3e2fx_reg_pat[S_RES_PREVIEW].size_h +
		s5k3e2fx_reg_pat[S_RES_PREVIEW].blk_l);
}

static uint16_t s5k3e2fx_get_prev_pixels_pl(void)
{
	return (s5k3e2fx_reg_pat[S_RES_PREVIEW].size_w +
		s5k3e2fx_reg_pat[S_RES_PREVIEW].blk_p);
}

static uint16_t s5k3e2fx_get_pict_lines_pf(void)
{
	return (s5k3e2fx_reg_pat[S_RES_CAPTURE].size_h +
		s5k3e2fx_reg_pat[S_RES_CAPTURE].blk_l);
}

static uint16_t s5k3e2fx_get_pict_pixels_pl(void)
{
	return (s5k3e2fx_reg_pat[S_RES_CAPTURE].size_w +
		s5k3e2fx_reg_pat[S_RES_CAPTURE].blk_p);
}

static uint32_t s5k3e2fx_get_pict_max_exp_lc(void)
{
	uint32_t snapshot_lines_per_frame;

	if (s5k3e2fx_ctrl->pict_res == S_QTR_SIZE)
		snapshot_lines_per_frame =
		s5k3e2fx_reg_pat[S_RES_PREVIEW].size_h +
		s5k3e2fx_reg_pat[S_RES_PREVIEW].blk_l;
	else
		snapshot_lines_per_frame = 3961 * 3;

	return snapshot_lines_per_frame;
}

static int32_t s5k3e2fx_set_fps(struct fps_cfg *fps)
{
	/* input is new fps in Q10 format */
	int32_t rc = 0;

	s5k3e2fx_ctrl->fps_divider = fps->fps_div;

	rc = s5k3e2fx_i2c_write_b(s5k3e2fx_client->addr,
		REG_FRAME_LENGTH_LINES_MSB,
		(((s5k3e2fx_reg_pat[S_RES_PREVIEW].size_h +
			s5k3e2fx_reg_pat[S_RES_PREVIEW].blk_l) *
			s5k3e2fx_ctrl->fps_divider / 0x400) & 0xFF00) >> 8);
	if (rc < 0)
		goto set_fps_done;

	rc = s5k3e2fx_i2c_write_b(s5k3e2fx_client->addr,
		REG_FRAME_LENGTH_LINES_LSB,
		(((s5k3e2fx_reg_pat[S_RES_PREVIEW].size_h +
			s5k3e2fx_reg_pat[S_RES_PREVIEW].blk_l) *
			s5k3e2fx_ctrl->fps_divider / 0x400) & 0xFF00));

set_fps_done:
	return rc;
}

static int32_t s5k3e2fx_write_exp_gain(uint16_t gain, uint32_t line)
{
	int32_t rc = 0;

	uint16_t max_legal_gain = 0x0200;
	uint32_t ll_ratio; /* Q10 */
	uint16_t ll_pck, fl_lines;
	uint16_t offset = 4;
	uint8_t  gain_msb, gain_lsb;
	uint8_t  intg_t_msb, intg_t_lsb;
	uint8_t  ll_pck_msb, ll_pck_lsb, tmp;

	struct s5k3e2fx_i2c_reg_conf tbl[2];

	CDBG("Line:%d s5k3e2fx_write_exp_gain \n", __LINE__);

	if (s5k3e2fx_ctrl->sensormode == SENSOR_PREVIEW_MODE) {

		s5k3e2fx_ctrl->my_reg_gain = gain;
		s5k3e2fx_ctrl->my_reg_line_count = (uint16_t)line;

		fl_lines = s5k3e2fx_reg_pat[S_RES_PREVIEW].size_h +
			s5k3e2fx_reg_pat[S_RES_CAPTURE].blk_l;

		ll_pck = s5k3e2fx_reg_pat[S_RES_PREVIEW].size_w +
			s5k3e2fx_reg_pat[S_RES_CAPTURE].blk_p;

	} else {

		fl_lines = s5k3e2fx_reg_pat[S_RES_CAPTURE].size_h +
			s5k3e2fx_reg_pat[S_RES_CAPTURE].blk_l;

		ll_pck = s5k3e2fx_reg_pat[S_RES_CAPTURE].size_w +
			s5k3e2fx_reg_pat[S_RES_CAPTURE].blk_p;
	}

	if (gain > max_legal_gain)
		gain = max_legal_gain;

	/* in Q10 */
	line = (line * s5k3e2fx_ctrl->fps_divider);

	if (fl_lines < (line / 0x400))
		ll_ratio = (line / (fl_lines - offset));
	else
		ll_ratio = 0x400;

	/* update gain registers */
	gain_msb = (gain & 0xFF00) >> 8;
	gain_lsb = gain & 0x00FF;
	tbl[0].waddr = REG_ANALOGUE_GAIN_CODE_GLOBAL_MSB;
	tbl[0].bdata = gain_msb;
	tbl[1].waddr = REG_ANALOGUE_GAIN_CODE_GLOBAL_LSB;
	tbl[1].bdata = gain_lsb;
	rc = s5k3e2fx_i2c_write_table(&tbl[0], ARRAY_SIZE(tbl));
	if (rc < 0)
		goto write_gain_done;

	ll_pck = ll_pck * ll_ratio;
	ll_pck_msb = ((ll_pck / 0x400) & 0xFF00) >> 8;
	ll_pck_lsb = (ll_pck / 0x400) & 0x00FF;
	tbl[0].waddr = REG_LINE_LENGTH_PCK_MSB;
	tbl[0].bdata = s5k3e2fx_reg_pat[S_RES_PREVIEW].line_length_pck_msb;
	tbl[1].waddr = REG_LINE_LENGTH_PCK_LSB;
	tbl[1].bdata = s5k3e2fx_reg_pat[S_RES_PREVIEW].line_length_pck_lsb;
	rc = s5k3e2fx_i2c_write_table(&tbl[0], ARRAY_SIZE(tbl));
	if (rc < 0)
		goto write_gain_done;

	tmp = (ll_pck * 0x400) / ll_ratio;
	intg_t_msb = (tmp & 0xFF00) >> 8;
	intg_t_lsb = (tmp & 0x00FF);
	tbl[0].waddr = REG_COARSE_INTEGRATION_TIME;
	tbl[0].bdata = intg_t_msb;
	tbl[1].waddr = REG_COARSE_INTEGRATION_TIME_LSB;
	tbl[1].bdata = intg_t_lsb;
	rc = s5k3e2fx_i2c_write_table(&tbl[0], ARRAY_SIZE(tbl));

write_gain_done:
	return rc;
}

static int32_t s5k3e2fx_set_pict_exp_gain(uint16_t gain, uint32_t line)
{
	int32_t rc = 0;

	CDBG("Line:%d s5k3e2fx_set_pict_exp_gain \n", __LINE__);

	rc =
		s5k3e2fx_write_exp_gain(gain, line);

	return rc;
}

static int32_t s5k3e2fx_video_config(int mode, int res)
{
	int32_t rc;

	switch (res) {
	case S_QTR_SIZE:
		rc = s5k3e2fx_setting(S_UPDATE_PERIODIC, S_RES_PREVIEW);
		if (rc < 0)
			return rc;

		CDBG("s5k3e2fx sensor configuration done!\n");
		break;

	case S_FULL_SIZE:
		rc = s5k3e2fx_setting(S_UPDATE_PERIODIC, S_RES_CAPTURE);
		if (rc < 0)
			return rc;

		break;

	default:
		return 0;
	} /* switch */

	s5k3e2fx_ctrl->prev_res = res;
	s5k3e2fx_ctrl->curr_res = res;
	s5k3e2fx_ctrl->sensormode = mode;

	rc =
		s5k3e2fx_write_exp_gain(s5k3e2fx_ctrl->my_reg_gain,
			s5k3e2fx_ctrl->my_reg_line_count);

	return rc;
}

static int32_t s5k3e2fx_snapshot_config(int mode)
{
	int32_t rc = 0;

	rc = s5k3e2fx_setting(S_UPDATE_PERIODIC, S_RES_CAPTURE);
	if (rc < 0)
		return rc;

	s5k3e2fx_ctrl->curr_res = s5k3e2fx_ctrl->pict_res;
	s5k3e2fx_ctrl->sensormode = mode;

	return rc;
}

static int32_t s5k3e2fx_raw_snapshot_config(int mode)
{
	int32_t rc = 0;

	rc = s5k3e2fx_setting(S_UPDATE_PERIODIC, S_RES_CAPTURE);
	if (rc < 0)
		return rc;

	s5k3e2fx_ctrl->curr_res = s5k3e2fx_ctrl->pict_res;
	s5k3e2fx_ctrl->sensormode = mode;

	return rc;
}

static int32_t s5k3e2fx_set_sensor_mode(int mode, int res)
{
	int32_t rc = 0;

	switch (mode) {
	case SENSOR_PREVIEW_MODE:
		rc = s5k3e2fx_video_config(mode, res);
		break;

	case SENSOR_SNAPSHOT_MODE:
		rc = s5k3e2fx_snapshot_config(mode);
		break;

	case SENSOR_RAW_SNAPSHOT_MODE:
		rc = s5k3e2fx_raw_snapshot_config(mode);
		break;

	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int32_t s5k3e2fx_set_default_focus(void)
{
	int32_t rc = 0;

	rc = s5k3e2fx_i2c_write_b(s5k3e2fx_client->addr,
			0x3131, 0);
	if (rc < 0)
		return rc;

	rc = s5k3e2fx_i2c_write_b(s5k3e2fx_client->addr,
			0x3132, 0);
	if (rc < 0)
		return rc;

	s5k3e2fx_ctrl->curr_lens_pos = 0;

	return rc;
}

static int32_t s5k3e2fx_move_focus(int direction, int32_t num_steps)
{
	int32_t rc = 0;
	int32_t i;
	int16_t step_direction;
	int16_t actual_step;
	int16_t next_pos, pos_offset;
	int16_t init_code = 50;
	uint8_t next_pos_msb, next_pos_lsb;
	int16_t s_move[5];
	uint32_t gain; /* Q10 format */

	if (direction == MOVE_NEAR)
		step_direction = 20;
	else if (direction == MOVE_FAR)
		step_direction = -20;
	else {
		CDBG("s5k3e2fx_move_focus failed at line %d ...\n", __LINE__);
		return -EINVAL;
	}

	actual_step = step_direction * (int16_t)num_steps;
	pos_offset = init_code + s5k3e2fx_ctrl->curr_lens_pos;
	gain = actual_step * 0x400 / 5;

	for (i = 0; i <= 4; i++) {
		if (actual_step >= 0)
			s_move[i] = ((((i+1)*gain+0x200) - (i*gain+0x200))/0x400);
		else
			s_move[i] = ((((i+1)*gain-0x200) - (i*gain-0x200))/0x400);
	}

	/* Ring Damping Code */
	for (i = 0; i <= 4; i++) {
		next_pos = (int16_t)(pos_offset + s_move[i]);

		if (next_pos > (738 + init_code))
			next_pos = 738 + init_code;
		else if (next_pos < 0)
			next_pos = 0;

		CDBG("next_position in damping mode = %d\n", next_pos);
		/* Writing the Values to the actuator */
		if (next_pos == init_code)
			next_pos = 0x00;

		next_pos_msb = next_pos >> 8;
		next_pos_lsb = next_pos & 0x00FF;

		rc = s5k3e2fx_i2c_write_b(s5k3e2fx_client->addr, 0x3131, next_pos_msb);
		if (rc < 0)
			break;

		rc = s5k3e2fx_i2c_write_b(s5k3e2fx_client->addr, 0x3132, next_pos_lsb);
		if (rc < 0)
			break;

		pos_offset = next_pos;
		s5k3e2fx_ctrl->curr_lens_pos = pos_offset - init_code;
		if (i < 4)
			mdelay(3);
	}

	return rc;
}

static int s5k3e2fx_sensor_config(void __user *argp)
{
	struct sensor_cfg_data cdata;
	long   rc = 0;

	if (copy_from_user(&cdata,
			(void *)argp,
			sizeof(struct sensor_cfg_data)))
		return -EFAULT;

	down(&s5k3e2fx_sem);

	CDBG("%s: cfgtype = %d\n", __func__, cdata.cfgtype);
	switch (cdata.cfgtype) {
	case CFG_GET_PICT_FPS:
		s5k3e2fx_get_pict_fps(cdata.cfg.gfps.prevfps,
			&(cdata.cfg.gfps.pictfps));

		if (copy_to_user((void *)argp, &cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_GET_PREV_L_PF:
		cdata.cfg.prevl_pf = s5k3e2fx_get_prev_lines_pf();

		if (copy_to_user((void *)argp,
				&cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_GET_PREV_P_PL:
		cdata.cfg.prevp_pl = s5k3e2fx_get_prev_pixels_pl();

		if (copy_to_user((void *)argp,
				&cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_GET_PICT_L_PF:
		cdata.cfg.pictl_pf = s5k3e2fx_get_pict_lines_pf();

		if (copy_to_user((void *)argp,
				&cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_GET_PICT_P_PL:
		cdata.cfg.pictp_pl = s5k3e2fx_get_pict_pixels_pl();

		if (copy_to_user((void *)argp,
				&cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_GET_PICT_MAX_EXP_LC:
		cdata.cfg.pict_max_exp_lc =
			s5k3e2fx_get_pict_max_exp_lc();

		if (copy_to_user((void *)argp,
				&cdata,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_SET_FPS:
	case CFG_SET_PICT_FPS:
		rc = s5k3e2fx_set_fps(&(cdata.cfg.fps));
		break;

	case CFG_SET_EXP_GAIN:
		rc =
			s5k3e2fx_write_exp_gain(cdata.cfg.exp_gain.gain,
				cdata.cfg.exp_gain.line);
		break;

	case CFG_SET_PICT_EXP_GAIN:
		CDBG("Line:%d CFG_SET_PICT_EXP_GAIN \n", __LINE__);
		rc =
			s5k3e2fx_set_pict_exp_gain(
				cdata.cfg.exp_gain.gain,
				cdata.cfg.exp_gain.line);
		break;

	case CFG_SET_MODE:
		rc =
			s5k3e2fx_set_sensor_mode(
			cdata.mode, cdata.rs);
		break;

	case CFG_PWR_DOWN:
		rc = s5k3e2fx_power_down();
		break;

	case CFG_MOVE_FOCUS:
		rc =
			s5k3e2fx_move_focus(
			cdata.cfg.focus.dir,
			cdata.cfg.focus.steps);
		break;

	case CFG_SET_DEFAULT_FOCUS:
		rc =
			s5k3e2fx_set_default_focus();
		break;

	case CFG_GET_AF_MAX_STEPS:
	case CFG_SET_EFFECT:
	case CFG_SET_LENS_SHADING:
	default:
		rc = -EINVAL;
		break;
	}

	up(&s5k3e2fx_sem);
	return rc;
}

static int s5k3e2fx_sensor_probe(const struct msm_camera_sensor_info *info,
		struct msm_sensor_ctrl *s)
{
	int rc = 0;

	rc = i2c_add_driver(&s5k3e2fx_i2c_driver);
	if (rc < 0 || s5k3e2fx_client == NULL) {
		rc = -ENOTSUPP;
		goto probe_fail;
	}

	msm_camio_clk_rate_set(24000000);
	mdelay(20);

	rc = s5k3e2fx_probe_init_sensor(info);
	if (rc < 0)
		goto probe_fail;

	s->s_init = s5k3e2fx_sensor_open_init;
	s->s_release = s5k3e2fx_sensor_release;
	s->s_config  = s5k3e2fx_sensor_config;
	s5k3e2fx_probe_init_done(info);

	return rc;

probe_fail:
	CDBG("SENSOR PROBE FAILS!\n");
	return rc;
}

static int __s5k3e2fx_probe(struct platform_device *pdev)
{
	return msm_camera_drv_start(pdev, s5k3e2fx_sensor_probe);
}

static struct platform_driver msm_camera_driver = {
	.probe = __s5k3e2fx_probe,
	.driver = {
		.name = "msm_camera_s5k3e2fx",
		.owner = THIS_MODULE,
	},
};

static int __init s5k3e2fx_init(void)
{
	return platform_driver_register(&msm_camera_driver);
}

module_init(s5k3e2fx_init);

