/* SPDX-License-Identifier: GPL-2.0 */

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
	u32 in_mipi;
	u32 out_mipi;
	u32 mipi_lane;
	u32 data_type;
	u32 htotal;
	u32 vtotal;
	s64 link_freqs;
};
