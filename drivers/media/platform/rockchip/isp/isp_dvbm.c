// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Rockchip Electronics Co., Ltd */

#include <linux/of.h>
#include <linux/of_platform.h>
#include <soc/rockchip/rockchip_dvbm.h>

#include "dev.h"
#include "regs.h"

static struct dvbm_port *g_dvbm;

int rkisp_dvbm_get(struct rkisp_device *dev)
{
	struct device_node *np = dev->dev->of_node;
	struct device_node *np_dvbm = of_parse_phandle(np, "dvbm", 0);
	int ret = -EINVAL;

	g_dvbm = NULL;
	if (dev->isp_ver != ISP_V32)
		goto end;

	if (!np_dvbm || !of_device_is_available(np_dvbm)) {
		dev_warn(dev->dev, "failed to get dvbm node\n");
	} else {
		struct platform_device *p_dvbm = of_find_device_by_node(np_dvbm);

		g_dvbm = rk_dvbm_get_port(p_dvbm, DVBM_ISP_PORT);
		of_node_put(np_dvbm);
	}

end:
	return ret;
}

int rkisp_dvbm_init(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_dummy_buffer *buf = &stream->dummy_buf;
	struct dvbm_isp_cfg_t dvbm_cfg;
	u32 width, height, wrap_line;

	if (!g_dvbm)
		return -EINVAL;

	width = stream->out_fmt.plane_fmt[0].bytesperline;
	height = stream->out_fmt.height;
	wrap_line = dev->cap_dev.wrap_line;
	dvbm_cfg.dma_addr = buf->dma_addr;
	dvbm_cfg.ybuf_bot = 0;
	dvbm_cfg.ybuf_top = width * wrap_line;
	dvbm_cfg.ybuf_lstd = width;
	dvbm_cfg.ybuf_fstd = width * height;
	dvbm_cfg.cbuf_bot = dvbm_cfg.ybuf_top;
	dvbm_cfg.cbuf_top = dvbm_cfg.cbuf_bot + (width * wrap_line / 2);
	dvbm_cfg.cbuf_lstd = width;
	dvbm_cfg.cbuf_fstd = dvbm_cfg.ybuf_fstd / 2;

	rk_dvbm_ctrl(g_dvbm, DVBM_ISP_SET_CFG, &dvbm_cfg);
	rk_dvbm_link(g_dvbm);
	return 0;
}

void rkisp_dvbm_deinit(void)
{
	if (g_dvbm)
		rk_dvbm_unlink(g_dvbm);
}

int rkisp_dvbm_event(struct rkisp_device *dev, u32 event)
{
	enum dvbm_cmd cmd;
	u32 seq;

	if (!g_dvbm || dev->isp_ver != ISP_V32 ||
	    !dev->cap_dev.wrap_line)
		return -EINVAL;

	rkisp_dmarx_get_frame(dev, &seq, NULL, NULL, true);

	switch (event) {
	case CIF_ISP_V_START:
		cmd = DVBM_ISP_FRM_START;
		break;
	case CIF_MI_MP_FRAME:
		cmd = DVBM_ISP_FRM_END;
		break;
	default:
		return -EINVAL;
	}

	return rk_dvbm_ctrl(g_dvbm, cmd, &seq);
}
