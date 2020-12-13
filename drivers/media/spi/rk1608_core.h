/* SPDX-License-Identifier: GPL-2.0 */
/**
 * Rockchip rk1608 driver
 *
 * Copyright (C) 2017-2018 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef __RK1608_CORE_H__
#define __RK1608_CORE_H__

#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/miscdevice.h>
#include <linux/version.h>
#include "rk1608_dphy.h"

#define RK1608_VERSION			KERNEL_VERSION(0, 0x01, 0x04)

#define UPDATE_REF_DATA_FROM_EEPROM (1)

#define RK1608_OP_TRY_MAX		3
#define RK1608_OP_TRY_DELAY		10
#define RK1608_CMD_WRITE		0x00000011
#define RK1608_CMD_WRITE_REG0		0X00010011
#define RK1608_CMD_WRITE_REG1		0X00020011
#define RK1608_CMD_READ			0x00000077
#define RK1608_CMD_READ_BEGIN		0x000000aa
#define RK1608_CMD_QUERY		0x000000ff
#define RK1608_CMD_QUERY_REG2		0x000001ff
#define RK1608_STATE_ID_MASK		0xffff0000
#define RK1608_STATE_ID			0X16080000
#define RK1608_STATE_MASK		0x0000ffff
#define APB_CMD_WRITE_REG1		0X00020011
#define RK1608_R_MSG_QUEUE_ADDR		0x60050000

#define RK1608_IRQ_TYPE_MSG		0x12345678
#define BOOT_REQUEST_ADDR		0x18000010
#define RK1608_HEAD_ADDR		0x60000000
#define RK1608_FW_NAME			"rk1608.rkl"
#define RK1608_S_MSG_QUEUE_ADDR		0x60050010
#define RK1608_PMU_SYS_REG0		0x120000f0
#define RK1608_MSG_QUEUE_OK_MASK	0xffff0001
#define RK1608_MSG_QUEUE_OK_TAG		0x16080001
#define RK1608_MAX_OP_BYTES		60000
#define MSG_SYNC_TIMEOUT		3000

#define BOOT_FLAG_CRC			(0x01 << 0)
#define BOOT_FLAG_EXE			(0x01 << 1)
#define BOOT_FLAG_LOAD_PMEM		(0x01 << 2)
#define BOOT_FLAG_ACK			(0x01 << 3)
#define BOOT_FLAG_READ_WAIT		(0x01 << 4)
#define BOOT_FLAG_BOOT_REQUEST		(0x01 << 5)

#define DEBUG_DUMP_ALL_SEND_RECV_MSG	0
#define RK1608_MCLK_RATE		(24 * 1000 * 1000ul)
#define SENSOR_TIMEOUT			1000

#define OPM_SLAVE_MODE			0x100000
#define RSD_SEL_2CYC			0x008000
#define DFS_SEL_16BIT			0x000002
#define SPI_CTRL0			0x11060000
#define SPI_ENR				0x11060008
#define CRUPMU_CLKSEL14_CON		0x12008098
#define PMUGRF_GPIO1A_E			0x12030040
#define PMUGRF_GPIO1B_E			0x12030044
#define BIT7_6_SEL_8MA			0xf000a000
#define BIT1_0_SEL_8MA			0x000f000a
#define SPI0_PLL_SEL_APLL		0xff004000
#define INVALID_ID			-1
#define RK1608_MAX_SEC_NUM		10

#define ISP_DSP_HDRAE_MAXGRIDITEMS	225

#define MIRROR_BIT_MASK BIT(0)
#define FLIP_BIT_MASK BIT(1)

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MSB2LSB32
#define MSB2LSB32(x)	((((u32)(x) & 0x80808080) >> 7) | \
			(((u32)(x) & 0x40404040) >> 5) | \
			(((u32)(x) & 0x20202020) >> 3) | \
			(((u32)(x) & 0x10101010) >> 1) | \
			(((u32)(x) & 0x08080808) << 1) | \
			(((u32)(x) & 0x04040404) << 3) | \
			(((u32)(x) & 0x02020202) << 5) | \
			(((u32)(x) & 0x01010101) << 7))
#endif

struct rk1608_client_list {
	struct mutex mutex; /* protect clients */
	struct list_head list;
};

struct rk1608_state {
	struct v4l2_subdev sd;
	struct rk1608_dphy *dphy[2];
	struct mutex lock; /* protect resource */
	struct mutex sensor_lock; /* protect sensor */
	struct mutex send_msg_lock; /* protect msg */
	struct mutex spi2apb_lock; /* protect spi2apb write/read */
	spinlock_t hdrae_lock; /* protect hdrae parameter */
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwren_gpio;
	struct gpio_desc *irq_gpio;
	int irq;
	struct gpio_desc *wakeup_gpio;
	struct gpio_desc *aesync_gpio;
	struct v4l2_subdev *sensor[4];
	struct device *dev;
	struct spi_device *spi;
	struct clk *mclk;
	struct miscdevice misc;
	struct rk1608_client_list clients;
	int log_level;
	int power_count;
	int msg_num;
	u32 link_nums;
	u32 sensor_cnt;
	u32 sensor_nums[2];
	u32 msg_done[8];
	wait_queue_head_t msg_wait;
	struct media_pad pad;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *h_flip;
	struct v4l2_ctrl *v_flip;
	struct v4l2_ctrl_handler ctrl_handler;
	u32 max_speed_hz;
	u32 min_speed_hz;
	struct preisp_hdrae_para_s hdrae_para;
	struct preisp_hdrae_exp_s hdrae_exp;
	u32 set_exp_cnt;
	const char *firm_name;
	u8 flip;
};

struct rk1608_section {
	union {
		u32 offset;
		u32 wait_value;
	};
	u32 size;
	union {
		u32 load_addr;
		u32 wait_addr;
	};
	u16 wait_time;
	u16 timeout;
	u16 crc_16;
	u8 flag;
	u8 type;
};

struct rk1608_header {
	char version[32];
	u32 header_size;
	u32 section_count;
	struct rk1608_section sections[RK1608_MAX_SEC_NUM];
};

struct rk1608_boot_req {
	u32 flag;
	u32 load_addr;
	u32 boot_len;
	u8 status;
	u8 dummy[2];
	u8 cmd;
};

struct rk1608_msg_queue {
	u32 buf_head; /* msg buffer head */
	u32 buf_tail; /* msg buffer tail */
	u32 cur_send; /* current msg send position */
	u32 cur_recv; /* current msg receive position */
};

enum _log_level {
	LOG_ERROR,
	LOG_WARN,
	LOG_INFO,
	LOG_DEBUG,
};

struct msg {
	u32 size; /* unit 4 bytes */
	u16 type;
	union {
		u8 camera_id;
		u8 core_id;
	} id;
	union {
		u8 sync;
		u8 log_level;
		s8 err;
	} mux;
};

struct msg_init {
	struct msg msg_head;
	u32 i2c_bus;
	u32 i2c_clk;
	s8 in_mipi_phy;
	s8 out_mipi_phy;
	s8 mipi_lane;
	s8 bayer;
	u8 sensor_name[32];
	u8 i2c_slave_addr;
};

struct preisp_vc_cfg {
	s8 data_id;
	s8 decode_format;
	s8 flag;
	s8 unused;

	u16 width;
	u16 height;
};

struct msg_in_size {
	struct msg msg_head;
	struct preisp_vc_cfg channel[4];
};

struct msg_out_size_head {
	struct msg msg_head;
	u16 width;
	u16 height;
	u32 mipi_clk;
	u16 line_length_pclk;
	u16 frame_length_lines;
	u16 mipi_lane;
	union {
		u16 flip;
		u16 reserved;
	};
};

struct msg_set_output_size {
	struct msg_out_size_head head;
	struct preisp_vc_cfg channel[4];
};

struct msg_init_dsp_time {
	struct msg msg_head;
	u16 t_ms;
	s32 tv_sec;
	s32 tv_usec;
};

enum ISP_AE_Bayer_Mode_e {
	BAYER_MODE_MIN = 0,
	BAYER_MODE_BGGR = 1,
	BAYER_MODE_GRBG = 2,
	BAYER_MODE_GBRG = 3,
	BAYER_MODE_RGGB = 4,
	BAYER_MODE_MAX = 5
};

struct ISP_AE_YCOEFF_s {
	int rcoef;
	int gcoef;
	int bcoef;
	int offset;
};

enum ISP_AE_Hist_Mode_e {
	AE_HISTSTATICMODE_INVALID = 0,
	AE_HISTSTATICMODE_Y,
	AE_HISTSTATICMODE_R,
	AE_HISTSTATICMODE_G,
	AE_HISTSTATICMODE_B,
	AE_HISTSTATICMODE_MAX
};

enum ISP_AE_Grid_Mode_e {
	AE_MEASURE_GRID_INVALID = 0,
	AE_MEASURE_GRID_1X1,
	AE_MEASURE_GRID_5X5,
	AE_MEASURE_GRID_9X9,
	AE_MEASURE_GRID_15X15,
	AE_MEASURE_GRID_MAX
};

struct ISP_DSP_hdrae_cfg_s {
	enum ISP_AE_Bayer_Mode_e bayer_mode;
	enum ISP_AE_Grid_Mode_e grid_mode;
	u8 weight[ISP_DSP_HDRAE_MAXGRIDITEMS];
	enum ISP_AE_Hist_Mode_e hist_mode;
	struct ISP_AE_YCOEFF_s ycoeff;
	u8 imgbits;
	u16 width;
	u16 height;
	u16 frames;
};

struct msg_set_sensor_info_s {
	struct msg msg_head;
	u32 set_exp_cnt;
	u16 r_gain;
	u16 b_gain;
	u16 gr_gain;
	u16 gb_gain;
	u32 exp_time[3];
	u32 exp_gain[3];
	u32 reg_exp_time[3];
	u32 reg_exp_gain[3];
	s32 lsc_table[17 * 17];
	struct ISP_DSP_hdrae_cfg_s dsp_hdrae;
};

struct msg_calib_temp {
	u32 size; // unit 4 bytes
	u16 type; // msg identification
	s8  camera_id;
	s8  sync;

	u32 temp;
	u32 calib_version;

#if UPDATE_REF_DATA_FROM_EEPROM
	u32 calib_exist;
	u32 calib_sn_size;
	u32 calib_sn_offset;
	u32 calib_sn_code;
#endif
};

#define MSG_RESPONSE_ID_OFFSET 0x2ff

enum {
	/* AP -> RK1608
	 * 1 msg of sensor
	 */
	id_msg_init_sensor_t =		0x0001,
	id_msg_set_input_size_t,
	id_msg_set_output_size_t,
	id_msg_set_stream_in_on_t,
	id_msg_set_stream_in_off_t,
	id_msg_set_stream_out_on_t,
	id_msg_set_stream_out_off_t,

	/* AP -> RK1608
	 * 2 msg of take picture
	 */
	id_msg_take_picture_t =		0x0021,
	id_msg_take_picture_done_t,

	/* AP -> RK1608
	 * 3 msg of realtime parameter
	 */
	id_msg_rt_args_t =		0x0031,
	id_msg_set_sensor_info_t,

	/* AP -> RK1608
	 * 4 msg of power manager
	 */
	id_msg_set_sys_mode_bypass_t =	0x0200,
	id_msg_set_sys_mode_standby_t,
	id_msg_set_sys_mode_idle_enable_t,
	id_msg_set_sys_mode_idle_disable_t,
	id_msg_set_sys_mode_slave_dsp_on_t,
	id_msg_set_sys_mode_slave_dsp_off_t,

	/* AP -> RK1608
	 * 5 msg of debug config
	 */
	id_msg_set_log_level_t =	0x0250,

	/* RK1608 -> AP
	 * 6 response of sensor msg
	 */
	id_msg_init_sensor_ret_t =	0x0300,
	id_msg_set_input_size_ret_t,
	id_msg_set_output_size_ret_t,
	id_msg_set_stream_in_on_ret_t,
	id_msg_set_stream_in_off_ret_t,
	id_msg_set_stream_out_on_ret_t,
	id_msg_set_stream_out_off_ret_t,

	/* RK1608 -> AP
	 * 7 response of take picture msg
	 */
	id_msg_take_picture_ret_t =	0x0320,
	id_msg_take_picture_done_ret_t,

	/* RK1608 -> AP
	 * 8 response of realtime parameter msg
	 */
	id_msg_rt_args_ret_t =		0x0330,

	/* rk1608 -> AP */
	id_msg_do_i2c_t =		0x0390,
	/* AP -> rk1608 */
	id_msg_do_i2c_ret_t,

	/* RK1608 -> AP
	 * 9 msg of print log
	 */
	id_msg_rk1608_log_t =		0x0400,

	/* dsi2csi dump */
	id_msg_dsi2sci_rgb_dump_t =	0x6000,
	id_msg_dsi2sci_nv12_dump_t =	0x6001,

	/* RK1608 -> AP
	 * 10  msg of xfile
	 */
	/* id_msg_xfile_import_t =		0x8000 + 0x0600,
	 * id_msg_xfile_export_t,
	 * id_msg_xfile_mkdir_t
	 */

	/* for dsp time. */
	id_msg_frame_time_t = 0x1000,
	id_msg_sys_time_set_t,

	//calib temperature and version
	id_msg_temperature_t = 0x1002,
	id_msg_temperature_req_t = 0x1302,
	id_msg_calib_temperature_t = 0x1004,
	id_msg_calib_temperature_req_t = 0x1303,

	id_msg_calibration_write_req_t = 0x1050,
	id_msg_calibration_write_done_t,
	id_msg_calibration_read_req_t = 0x1052,
	id_msg_calibration_read_done_t,
	id_msg_calibration_write_req_mode2_t = 0x1054,
	id_msg_calibration_read_req_mode2_t,

	id_msg_calibration_write_req_ret_t = 0x1050 + MSG_RESPONSE_ID_OFFSET,
	id_msg_calibration_write_done_ret_t = 0x1051 + MSG_RESPONSE_ID_OFFSET,
	id_msg_calibration_read_req_ret_t = 0x1052 + MSG_RESPONSE_ID_OFFSET,
	id_msg_calibration_read_done_ret_t = 0x1053 + MSG_RESPONSE_ID_OFFSET,


	/* 1808 for disp control */
	id_msg_disp_set_frame_output_t = 0x1070,
	id_msg_disp_set_frame_format_t,
	id_msg_disp_set_frame_type_t,
	id_msg_disp_set_pro_time_t,
	id_msg_disp_set_pro_current_t,
	id_msg_disp_set_denoise_t,
	id_msg_disp_set_led_on_off_t,

	/* 0xf000 ~ 0xfdff id reversed. */
	id_msg_xfile_import_t = 0xfe00,
	id_msg_xfile_export_t,
	id_msg_xfile_mkdir_t

};


#define PREISP_CALIB_ITEM_NUM       24
#define PREISP_CALIB_MAGIC      "#SLM_CALIB_DATA#"

struct calib_item {
	unsigned char name[48];
	unsigned int  offset;
	unsigned int  size;
	unsigned int  temp;
	unsigned int  crc32;
};

struct calib_head {
	unsigned char magic[16];
	unsigned int  version;
	unsigned int  head_size;
	unsigned int  image_size;
	unsigned int  items_number;
	unsigned char reserved0[32];
	unsigned int  hash_len;
	unsigned char hash[32];
	unsigned char reserved1[28];
	unsigned int  sign_tag;
	unsigned int  sign_len;
	unsigned char rsa_hash[256];
	unsigned char reserved2[120];
	struct calib_item item[PREISP_CALIB_ITEM_NUM];
};

#define XFILE_MAX_PATH 256
struct msg_xfile {
	u32 size;
	u16 type;
	s8  camera_id;
	union {
		s8 sync;
		s8 ret;
	};
	u32 addr;
	u32 data_size;
	u32 cb;
	u32 args;
	char path[XFILE_MAX_PATH];
};

int rk1608_send_msg_to_dsp(struct rk1608_state *pdata, struct msg *m);
/**
 * rk1608_write - RK1608 synchronous write
 *
 * @spi: spi device
 * @addr: resource address
 * @data: data buffer
 * @data_len: data buffer size, in bytes
 * Context: can sleep
 *
 * It returns zero on success, else a negative error code.
 */
int rk1608_write(struct spi_device *spi, s32 addr,
		 const s32 *data, size_t data_len);

/**
 * rk1608_safe_write - RK1608 synchronous write with state check
 *
 * @spi: spi device
 * @addr: resource address
 * @data: data buffer
 * @data_len: data buffer size, in bytes
 * Context: can sleep
 *
 * It returns zero on success, else operation state code.
 */
int rk1608_safe_write(struct rk1608_state *rk1608, struct spi_device *spi,
		      s32 addr, const s32 *data, size_t data_len);

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
int rk1608_read(struct spi_device *spi, s32 addr,
		s32 *data, size_t data_len);

/**
 * rk1608_safe_read - RK1608 synchronous read with state check
 *
 * @spi: spi device
 * @addr: resource address
 * @data: data buffer [out]
 * @data_len: data buffer size, in bytes
 * Context: can sleep
 *
 * It returns zero on success, else operation state code.
 */
int rk1608_safe_read(struct rk1608_state *rk1608, struct spi_device *spi,
		     s32 addr, s32 *data, size_t data_len);

/**
 * rk1608_operation_query - RK1608 last operation state query
 *
 * @spi: spi device
 * @state: last operation state [out]
 * Context: can sleep
 *
 * It returns zero on success, else a negative error code.
 */
int rk1608_operation_query(struct spi_device *spi, s32 *state);

/**
 * rk1608_state_query - RK1608 system state query
 *
 * @spi: spi device
 * @state: system state [out]
 * Context: can sleep
 *
 * It returns zero on success, else a negative error code.
 */
int rk1608_state_query(struct spi_device *spi, int32_t *state);

/**
 * rk1608_interrupt_request - RK1608 request a rk1608 interrupt
 *
 * @spi: spi device
 * @interrupt_num: interrupt identification
 * Context: can sleep
 *
 * It returns zero on success, else a negative error code.
 */
int rk1608_interrupt_request(struct spi_device *spi, s32 interrupt_num);

/**
 * rk1608_download_fw: - rk1608 firmware download through spi
 *
 * @spi: spi device
 * @fw_name: name of firmware file, NULL for default firmware name
 * Context: can sleep
 *
 * It returns zero on success, else a negative error code.
 **/
int rk1608_download_fw(struct rk1608_state *rk1608, struct spi_device *spi,
			const char *fw_name);

/**
 * rk1608_msq_recv_msg - receive a msg from RK1608 -> AP msg queue
 *
 * @spi: spi device
 * @m: a msg pointer buf [out]
 *
 * need call rk1608_msq_free_received_msg to free msg after msg use done
 *
 * It returns zero on success, else a negative error code.
 */
int rk1608_msq_recv_msg(struct rk1608_state *rk1608, struct spi_device *spi,
			struct msg **m);

/*
 * rk1608_msq_send_msg - send a msg from AP -> RK1608 msg queue
 *
 * @spi: spi device
 * @m: a msg to send
 *
 * It returns zero on success, else a negative error code.
 */
int rk1608_msq_send_msg(struct rk1608_state *rk1608, struct spi_device *spi,
			struct msg *m);

int rk1608_set_power(struct rk1608_state *pdata, int on);

void rk1608_set_spi_speed(struct rk1608_state *pdata, u32 hz);

int rk1608_set_log_level(struct rk1608_state *pdata, int level);

#endif
