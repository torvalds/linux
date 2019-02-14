/* SPDX-License-Identifier: GPL-2.0 */

struct rk1608_chinf {
	u32 width;
	u32 height;
	u32 data_id;
	u32 decode_format;
	u32 flag;
};

struct rk1608_dphy {
	struct v4l2_subdev sd;
	struct v4l2_subdev *rk1608_sd;
	struct platform_device *pdev;
	struct device *dev;
	struct media_pad pad;
	struct v4l2_mbus_framefmt mf;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl_handler ctrl_handler;
	u32 cam_nums;
	u32 data_type;
	u32 in_mipi;
	u32 out_mipi;
	u32 mipi_lane;

	u32 htotal;
	u32 vtotal;
	s64 link_freqs;
	u32 module_index;
	const char *module_facing;
	const char *module_name;
	const char *len_name;

	u32 i2c_bus;
	u32 i2c_addr;
	const char *sensor_name;

	struct rk1608_chinf in_ch[4];
	struct rk1608_chinf out_ch[4];
};
