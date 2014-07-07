#include <linux/delay.h>
#include <linux/interrupt.h>
#include "rk3288_hdmi_hw.h"

static const struct phy_mpll_config_tab PHY_MPLL_TABLE[] = {	/* opmode: 0:HDMI1.4     1:HDMI2.0 */
/*      pixclock    pixrepet        colordepth	    prepdiv  tmdsmhl opmode  fbdiv2  fbdiv1  ref_cntrl nctrl propctrl intctrl gmpctrl */
	{27000000,	0,	HDMI_COLOR_DEPTH_8BIT,	0,	0,	0,	2,	3,	0,	3,	7,	0,	3},
	{27000000,	0,	HDMI_COLOR_DEPTH_10BIT,	1,	0,	0,	5,	1,	0,	3,	3,	0,	0},
	{27000000,	0,	HDMI_COLOR_DEPTH_12BIT,	2,	0,	0,	3,	3,	0,	3,	3,	0,	0},
	{27000000,	0,	HDMI_COLOR_DEPTH_16BIT,	3,	0,	0,	2,	3,	0,	2,	5,	0,	1},
	{74250000,	0,	HDMI_COLOR_DEPTH_8BIT,	0,	0,	0,	4,	3,	3,	2,	7,	0,	3},
	{74250000,	0,	HDMI_COLOR_DEPTH_10BIT, 1,	0,	0,	5,	3,	3,	2,	7,	0,	2},
	{74250000,	0,	HDMI_COLOR_DEPTH_12BIT,	2,	0,	0,	1,	2,	0,	1,	7,	0,	2},
	{74250000,	0,	HDMI_COLOR_DEPTH_16BIT, 3,	0,	0,	1,	3,	0,	1,	7,	0,	2},
	{148500000,	0,	HDMI_COLOR_DEPTH_8BIT,	0,	0,	0,	1,	1,	0,	1,	0,	0,	3},
	{148500000,	0,	HDMI_COLOR_DEPTH_10BIT,	1,	0,	0,	5,	1,	3,	1,	7,	0,	3},
	{148500000,	0,	HDMI_COLOR_DEPTH_12BIT,	2,	0,	0,	1,	2,	1,	0,	7,	0,	3},
	{148500000,	0,	HDMI_COLOR_DEPTH_16BIT,	3,	0,	0,	1,	1,	0,	0,	7,	0,	3},
	{297000000,	0,	HDMI_COLOR_DEPTH_8BIT,	0,	0,	0,	1,	0,	0,	0,	0,	0,	3},
	{297000000,	0,	HDMI_COLOR_DEPTH_10BIT,	1,	3,	1,	5,	0,	3,	0,	7,	0,	3},
	{297000000,	0,	HDMI_COLOR_DEPTH_12BIT,	2,	3,	1,	1,	2,	2,	0,	7,	0,	3},
	{297000000,	0,	HDMI_COLOR_DEPTH_16BIT,	3,	3,	1,	1,	1,	0,	0,	5,	0,	3},
	{594000000,	0,	HDMI_COLOR_DEPTH_8BIT,	0,	3,	1,	1,	0,	0,	0,	3,	0,	3},
};

const struct phy_mpll_config_tab *get_phy_mpll_tab(int pixClock, char pixRepet,
						   char colorDepth)
{
	int i;

	if (pixClock == 0)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(PHY_MPLL_TABLE); i++) {
		if ((PHY_MPLL_TABLE[i].pix_clock == pixClock)
		    && (PHY_MPLL_TABLE[i].pix_repet == pixRepet)
		    && (PHY_MPLL_TABLE[i].color_depth == colorDepth))
			return &PHY_MPLL_TABLE[i];
	}
	return NULL;
}

static void rk3288_hdmi_av_mute(struct hdmi *hdmi_drv, int enable)
{
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	hdmi_msk_reg(hdmi_dev, FC_GCP, m_FC_SET_AVMUTE,
		     v_FC_SET_AVMUTE(enable));
#if 0
	/* audio mute priority: AVMUTE, sample flat, validity */
	/* AVMUTE also mutes video */
	value = enable ? 0xF : 0;
	hdmi_msk_reg(hdmi_dev, FC_AUDSCONF, m_AUD_PACK_SAMPFIT,
		     v_AUD_PACK_SAMPFIT(value));
#endif
}

static void rk3288_hdmi_set_pwr_mode(struct hdmi *hdmi_drv, int mode)
{
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);
	if (hdmi_drv->pwr_mode == mode)
		return;

	hdmi_dbg(hdmi_drv->dev, "%s change pwr_mode %d --> %d\n",
		   __func__, hdmi_drv->pwr_mode, mode);

	switch (mode) {
	case NORMAL:
		hdmi_writel(hdmi_dev, MC_CLKDIS, 0x00);
		break;
	case LOWER_PWR:
		hdmi_writel(hdmi_dev, MC_CLKDIS, 0xff);
		hdmi_msk_reg(hdmi_dev, PHY_CONF0,
			     m_TMDS_EN | m_TXPWRON_SIG | m_ENHPD_RXSENSE_SIG,
			     v_TMDS_EN(0) | v_TXPWRON_SIG(0) |
			     v_ENHPD_RXSENSE_SIG(1));
		break;
	default:
		hdmi_dbg(hdmi_drv->dev, "unkown hdmi pwr mode %d\n", mode);
	}
	hdmi_drv->pwr_mode = mode;
}

/* i2c master reset */
void rk3288_hdmi_i2cm_reset(struct rk3288_hdmi_device *hdmi_dev)
{
	hdmi_msk_reg(hdmi_dev, I2CM_SOFTRSTZ, m_I2CM_SOFTRST,
		     v_I2CM_SOFTRST(0));
	udelay(100);
}

void rk3288_hdmi_reset(struct hdmi *hdmi_drv)
{
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	hdmi_writel(hdmi_dev, MC_SWRSTZREQ, 0x00);
	udelay(100);
	hdmi_writel(hdmi_dev, MC_SWRSTZREQ, 0xff);
	hdmi_writel(hdmi_dev, MC_SWRSTZREQ_2, 0x00);
	udelay(100);
	hdmi_writel(hdmi_dev, MC_SWRSTZREQ_2, 0x01);

	rk3288_hdmi_i2cm_reset(hdmi_dev);
#if 1
	/* reset PHY */
	hdmi_writel(hdmi_dev, MC_PHYRSTZ, v_PHY_RSTZ(1));
	udelay(100);
	hdmi_writel(hdmi_dev, MC_PHYRSTZ, v_PHY_RSTZ(0));
#endif

	rk3288_hdmi_set_pwr_mode(hdmi_drv, LOWER_PWR);
}

int rk3288_hdmi_detect_hotplug(struct hdmi *hdmi_drv)
{
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);
	u32 value = hdmi_readl(hdmi_dev, PHY_STAT0);

	hdmi_dbg(hdmi_drv->dev, "[%s] reg%x value %02x\n", __func__,
		 PHY_STAT0, value);

	if ((value & m_PHY_HPD) || ((value & 0xf0) == 0xf0))
		return HDMI_HPD_ACTIVED;
	else
		return HDMI_HPD_REMOVED;
}

int rk3288_hdmi_read_edid(struct hdmi *hdmi_drv, int block, unsigned char *buff)
{
	int i = 0, n = 0, index = 0, ret = -1, trytime = 2;
	int offset = (block % 2) * 0x80;
	int interrupt = 0;
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	hdmi_dbg(hdmi_drv->dev, "[%s] block %d\n", __func__, block);

	/* Set DDC I2C CLK which devided from DDC_CLK to 100KHz */
	hdmi_writel(hdmi_dev, I2CM_SS_SCL_HCNT_0_ADDR, 0x7a);
	hdmi_writel(hdmi_dev, I2CM_SS_SCL_LCNT_0_ADDR, 0x8d);
	hdmi_msk_reg(hdmi_dev, I2CM_DIV, m_I2CM_FAST_STD_MODE,
		v_I2CM_FAST_STD_MODE(STANDARD_MODE));	/* Set Standard Mode */

	/* Enable I2C interrupt for reading edid */
	hdmi_writel(hdmi_dev, IH_MUTE_I2CM_STAT0,
		    v_SCDC_READREQ_MUTE(0) | v_I2CM_DONE_MUTE(0) |
		    v_I2CM_ERR_MUTE(0));
	hdmi_msk_reg(hdmi_dev, I2CM_INT, m_I2CM_DONE_MASK, v_I2CM_DONE_MASK(0));
	hdmi_msk_reg(hdmi_dev, I2CM_CTLINT, m_I2CM_NACK_MASK | m_I2CM_ARB_MASK,
		     v_I2CM_NACK_MASK(0) | v_I2CM_ARB_MASK(0));

	hdmi_writel(hdmi_dev, I2CM_SLAVE, DDC_I2C_EDID_ADDR);
	hdmi_writel(hdmi_dev, I2CM_SEGADDR, DDC_I2C_SEG_ADDR);
	hdmi_writel(hdmi_dev, I2CM_SEGPTR, block / 2);
	while (trytime--) {
		for (n = 0; n < HDMI_EDID_BLOCK_SIZE / 8; n++) {
			hdmi_writel(hdmi_dev, I2CM_ADDRESS, offset + 8 * n);
			/* enable extend sequential read operation */
			if (block == 0)
				hdmi_msk_reg(hdmi_dev, I2CM_OPERATION,
					     m_I2CM_RD8, v_I2CM_RD8(1));
			else
				hdmi_msk_reg(hdmi_dev, I2CM_OPERATION,
					     m_I2CM_RD8_EXT, v_I2CM_RD8_EXT(1));

			i = 20;
			while (i--) {
				msleep(1);
				interrupt = hdmi_readl(hdmi_dev, IH_I2CM_STAT0);
				if (interrupt)
					hdmi_writel(hdmi_dev, IH_I2CM_STAT0,
						    interrupt);

				if (interrupt &
				    (m_SCDC_READREQ | m_I2CM_DONE |
				     m_I2CM_ERROR))
					break;
				msleep(4);
			}

			if (interrupt & m_I2CM_DONE) {
				for (index = 0; index < 8; index++) {
					buff[8 * n + index] =
					    hdmi_readl(hdmi_dev,
						       I2CM_READ_BUFF0 + index);
				}

				if (n == HDMI_EDID_BLOCK_SIZE / 8 - 1) {
					ret = 0;
					hdmi_dbg(hdmi_drv->dev,
						 "[%s] edid read sucess\n",
						 __func__);

#ifdef HDMI_DEBUG
					for (i = 0; i < 128; i++) {
						printk("%02x ,", buff[i]);
						if ((i + 1) % 16 == 0)
							printk("\n");
					}
#endif
					goto exit;
				}
				continue;
			} else if ((interrupt & m_I2CM_ERROR) || (i == -1)) {
				hdmi_err(hdmi_drv->dev,
					 "[%s] edid read error\n",
					 __func__);
				rk3288_hdmi_i2cm_reset(hdmi_dev);
				break;
			}
		}

		hdmi_err(hdmi_drv->dev, "[%s] edid try times %d\n",
			 __func__, trytime);
		msleep(100);
	}

exit:
	/* Disable I2C interrupt */
	hdmi_msk_reg(hdmi_dev, IH_MUTE_I2CM_STAT0,
		     m_I2CM_DONE_MUTE | m_I2CM_ERR_MUTE,
		     v_I2CM_DONE_MUTE(1) | v_I2CM_ERR_MUTE(1));
	hdmi_msk_reg(hdmi_dev, I2CM_INT, m_I2CM_DONE_MASK, v_I2CM_DONE_MASK(1));
	hdmi_msk_reg(hdmi_dev, I2CM_CTLINT, m_I2CM_NACK_MASK | m_I2CM_ARB_MASK,
		     v_I2CM_NACK_MASK(1) | v_I2CM_ARB_MASK(1));
	return ret;
}

static int rk3288_hdmi_video_forceOutput(struct hdmi *hdmi_drv, char enable)
{
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	hdmi_msk_reg(hdmi_dev, FC_DBGFORCE, m_FC_FORCEAUDIO,
		     v_FC_FORCEAUDIO(0));

	if (enable) {		/* Force output Blue */
		hdmi_writel(hdmi_dev, FC_DBGTMDS2, 0x00); /*R*/
		hdmi_writel(hdmi_dev, FC_DBGTMDS1, 0x00); /*G*/
		hdmi_writel(hdmi_dev, FC_DBGTMDS0, 0xff); /*B*/
		hdmi_msk_reg(hdmi_dev, FC_DBGFORCE, m_FC_FORCEVIDEO,
				 v_FC_FORCEVIDEO(1));
	} else {
		hdmi_msk_reg(hdmi_dev, FC_DBGFORCE, m_FC_FORCEVIDEO,
			     v_FC_FORCEVIDEO(0));
		hdmi_writel(hdmi_dev, FC_DBGTMDS2, 0x00); /*R*/
		hdmi_writel(hdmi_dev, FC_DBGTMDS1, 0x00); /*G*/
		hdmi_writel(hdmi_dev, FC_DBGTMDS0, 0x00); /*B*/
	}

	return 0;
}

/* TODO Daisen wait to add support 3D */
static int rk3288_hdmi_video_frameComposer(struct hdmi *hdmi_drv,
					   struct hdmi_video_para *vpara)
{
	int h_act = 0, v_act = 0;
	int h_syncdelay = 0, v_syncdelay = 0;
	int h_sync = 0, v_sync = 0;
	int h_blank = 0, v_blank = 0;
	int vsync_pol = hdmi_drv->lcdc->cur_screen->pin_vsync;
	int hsync_pol = hdmi_drv->lcdc->cur_screen->pin_hsync;
	int de_pol = (hdmi_drv->lcdc->cur_screen->pin_den == 0) ? 1 : 0;
	struct fb_videomode *mode = NULL;
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	mode = (struct fb_videomode *)hdmi_vic_to_videomode(vpara->vic);
	if (mode == NULL) {
		hdmi_err(hdmi_drv->dev, "[%s] not found vic %d\n", __func__,
			 vpara->vic);
		return -ENOENT;
	}

	hdmi_drv->pixclock = mode->pixclock;
	switch (vpara->color_depth) {
	case HDMI_COLOR_DEPTH_8BIT:
		hdmi_drv->tmdsclk = mode->pixclock;
		break;
	case HDMI_COLOR_DEPTH_10BIT:
		hdmi_drv->tmdsclk = mode->pixclock * 10 / 8;
		break;
	case HDMI_COLOR_DEPTH_12BIT:
		hdmi_drv->tmdsclk = mode->pixclock * 12 / 8;
		break;
	case HDMI_COLOR_DEPTH_16BIT:
		hdmi_drv->tmdsclk = mode->pixclock * 2;
		break;
	default:
		hdmi_drv->tmdsclk = mode->pixclock;
		break;
	}

	/* cfg to bypass hdcp data encrypt */
	hdmi_msk_reg(hdmi_dev, A_HDCPCFG0, m_ENCRYPT_BYPASS | m_HDMI_DVI,
		     v_ENCRYPT_BYPASS(1) | v_HDMI_DVI(vpara->output_mode));
	hdmi_msk_reg(hdmi_dev, FC_INVIDCONF,
		     m_FC_VSYNC_POL | m_FC_HSYNC_POL | m_FC_DE_POL |
		     m_FC_HDMI_DVI | m_FC_INTERLACE_MODE,
		     v_FC_VSYNC_POL(vsync_pol) | v_FC_HSYNC_POL(hsync_pol) |
		     v_FC_DE_POL(de_pol) | v_FC_HDMI_DVI(vpara->
							 output_mode) |
		     v_FC_INTERLACE_MODE(mode->vmode));
	hdmi_msk_reg(hdmi_dev, FC_INVIDCONF, m_FC_VBLANK,
		     v_FC_VBLANK(mode->vmode));

	h_act = mode->xres;
	hdmi_writel(hdmi_dev, FC_INHACTIV1, v_FC_HACTIVE1(h_act >> 8));
	hdmi_writel(hdmi_dev, FC_INHACTIV0, (h_act & 0xff));

	v_act = mode->yres;
	hdmi_writel(hdmi_dev, FC_INVACTIV1, v_FC_VACTIVE1(v_act >> 8));
	hdmi_writel(hdmi_dev, FC_INVACTIV0, (v_act & 0xff));

	h_blank = mode->hsync_len + mode->left_margin + mode->right_margin;
	hdmi_writel(hdmi_dev, FC_INHBLANK1, v_FC_HBLANK1(h_blank >> 8));
	hdmi_writel(hdmi_dev, FC_INHBLANK0, (h_blank & 0xff));

	v_blank = mode->vsync_len + mode->upper_margin + mode->lower_margin;
	hdmi_writel(hdmi_dev, FC_INVBLANK, (v_blank & 0xff));

	h_syncdelay = mode->right_margin;
	hdmi_writel(hdmi_dev, FC_HSYNCINDELAY1,
		    v_FC_HSYNCINDEAY1(h_syncdelay >> 8));
	hdmi_writel(hdmi_dev, FC_HSYNCINDELAY0, (h_syncdelay & 0xff));

	v_syncdelay = mode->lower_margin;
	hdmi_writel(hdmi_dev, FC_VSYNCINDELAY, (v_syncdelay & 0xff));

	h_sync = mode->hsync_len;
	hdmi_writel(hdmi_dev, FC_HSYNCINWIDTH1, v_FC_HSYNCWIDTH1(h_sync >> 8));
	hdmi_writel(hdmi_dev, FC_HSYNCINWIDTH0, (h_sync & 0xff));

	v_sync = mode->vsync_len;
	hdmi_writel(hdmi_dev, FC_VSYNCINWIDTH, (v_sync & 0xff));

	/* Set the control period minimum duration
	* (min. of 12 pixel clock cycles, refer to HDMI 1.4b specification)
	*/
	hdmi_writel(hdmi_dev, FC_CTRLDUR, 12);
	hdmi_writel(hdmi_dev, FC_EXCTRLDUR, 32);
#if 0
	/* used for HDMI 2.0 TX TODO Daisen wait to modify HDCP KEEPOUT */
	if (hdmi_drv->tmdsclk > 340000000) {
		hdmi_msk_reg(hdmi_dev, FC_INVIDCONF, m_FC_HDCP_KEEPOUT,
			     v_FC_HDCP_KEEPOUT(1));
		hdmi_msk_reg(hdmi_dev, FC_SCRAMBLER_CTRL, m_FC_SCRAMBLE_EN,
			     v_FC_SCRAMBLE_EN(1));
	}

	/* spacing < 256^2 * config / tmdsClock, spacing <= 50ms
	 * worst case: tmdsClock == 25MHz => config <= 19
	*/
	hdmi_writel(hdmi_dev, FC_EXCTRLSPAC, 1);

	/* Set PreambleFilter */
	for (i = 0; i < 3; i++) {
		value = (i + 1) * 11;
		if (i == 0)		/* channel 0 */
			hdmi_writel(hdmi_dev, FC_CH0PREAM, value);
		else if (i == 1)	/* channel 1 */
			hdmi_writel(hdmi_dev, FC_CH1PREAM, value & 0x3f);
		else if (i == 2)	/* channel 2 */
			hdmi_writel(hdmi_dev, FC_CH2PREAM, value & 0x3f);
	}
#endif
	/* Set PixelRepetition:No pixel repetition */
	hdmi_writel(hdmi_dev, FC_PRCONF,
		    v_FC_PR_FACTOR(vpara->pixel_repet + 1));

	return 0;
}

static int rk3288_hdmi_video_packetizer(struct hdmi *hdmi_drv,
					struct hdmi_video_para *vpara)
{
	unsigned char color_depth = 0;
	unsigned char output_select = 0;
	unsigned char remap_size = 0;
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	if (vpara->output_color == VIDEO_OUTPUT_RGB444
	    || vpara->output_color == VIDEO_OUTPUT_YCBCR444
	    || vpara->output_color == VIDEO_OUTPUT_YCBCR420) {

		switch (vpara->color_depth) {
		case HDMI_COLOR_DEPTH_8BIT:
			color_depth = COLOR_DEPTH_24BIT;
			output_select = OUT_FROM_8BIT_BYPASS;
			break;
		case HDMI_COLOR_DEPTH_10BIT:
			color_depth = COLOR_DEPTH_30BIT;
			output_select = OUT_FROM_PIXEL_PACKING;
			break;
		case HDMI_COLOR_DEPTH_12BIT:
			color_depth = COLOR_DEPTH_36BIT;
			output_select = OUT_FROM_PIXEL_PACKING;
			break;
		case HDMI_COLOR_DEPTH_16BIT:
			color_depth = COLOR_DEPTH_48BIT;
			output_select = OUT_FROM_PIXEL_PACKING;
			break;
		default:
			color_depth = COLOR_DEPTH_24BIT;
			output_select = OUT_FROM_8BIT_BYPASS;
			break;
		}

		/* Config Color Depth */
		hdmi_msk_reg(hdmi_dev, VP_PR_CD, m_COLOR_DEPTH,
			     v_COLOR_DEPTH(color_depth));
	} else if (vpara->output_color == VIDEO_OUTPUT_YCBCR422) {

		switch (vpara->color_depth) {
		case HDMI_COLOR_DEPTH_8BIT:
			remap_size = YCC422_16BIT;
			break;
		case HDMI_COLOR_DEPTH_10BIT:
			remap_size = YCC422_20BIT;
			break;
		case HDMI_COLOR_DEPTH_12BIT:
			remap_size = YCC422_24BIT;
			break;
		default:
			remap_size = YCC422_16BIT;
			break;
		}

		output_select = OUT_FROM_YCC422_REMAP;
		/* Config remap size for the different color Depth */
		hdmi_msk_reg(hdmi_dev, VP_REMAP, m_YCC422_SIZE,
			     v_YCC422_SIZE(remap_size));
	} else {
		hdmi_err(hdmi_drv->dev, "invalid output color type: %d",
			 vpara->output_color);
		return -1;
	}

	/* Config pixel repettion */
	hdmi_msk_reg(hdmi_dev, VP_PR_CD, m_DESIRED_PR_FACTOR,
		     v_DESIRED_PR_FACTOR(vpara->pixel_repet));
	if (vpara->pixel_repet > 0)
		hdmi_msk_reg(hdmi_dev, VP_CONF, m_PIXEL_REPET_EN | m_BYPASS_SEL,
			     v_PIXEL_REPET_EN(1) | v_BYPASS_SEL(0));
	else
		hdmi_msk_reg(hdmi_dev, VP_CONF, m_PIXEL_REPET_EN | m_BYPASS_SEL,
			     v_PIXEL_REPET_EN(0) | v_BYPASS_SEL(1));

	/* config output select */
	if (output_select == OUT_FROM_PIXEL_PACKING) {	/* pixel packing */
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_BYPASS_EN | m_PIXEL_PACK_EN | m_YCC422_EN |
			     m_OUTPUT_SEL,
			     v_BYPASS_EN(0) | v_PIXEL_PACK_EN(1) |
			     v_YCC422_EN(0) | v_OUTPUT_SEL(output_select));
	} else if (output_select == OUT_FROM_YCC422_REMAP) {	/* YCC422 */
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_BYPASS_EN | m_PIXEL_PACK_EN | m_YCC422_EN |
			     m_OUTPUT_SEL,
			     v_BYPASS_EN(0) | v_PIXEL_PACK_EN(0) |
			     v_YCC422_EN(1) | v_OUTPUT_SEL(output_select));
	} else if (output_select == OUT_FROM_8BIT_BYPASS || output_select == 3) {
		/* bypass */
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_BYPASS_EN | m_PIXEL_PACK_EN | m_YCC422_EN |
			     m_OUTPUT_SEL,
			     v_BYPASS_EN(1) | v_PIXEL_PACK_EN(0) |
			     v_YCC422_EN(0) | v_OUTPUT_SEL(output_select));
	}
#if defined(HDMI_VIDEO_STUFFING)
	/* YCC422 and pixel packing stuffing */
	hdmi_msk_reg(hdmi_dev, VP_STUFF, m_PR_STUFFING, v_PR_STUFFING(1));
	hdmi_msk_reg(hdmi_dev, VP_STUFF, m_YCC422_STUFFING | m_PP_STUFFING,
		     v_YCC422_STUFFING(1) | v_PP_STUFFING(1));
#endif
	return 0;
}

int rk3288_hdmi_video_sampler(struct hdmi *hdmi_drv,
			      struct hdmi_video_para *vpara)
{
	int map_code = 0;
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	if (vpara->input_color == VIDEO_INPUT_COLOR_RGB
	    || vpara->input_color == VIDEO_INPUT_COLOR_YCBCR444
	    || vpara->input_color == VIDEO_INPUT_COLOR_YCBCR420) {

		switch (vpara->color_depth) {
		case HDMI_COLOR_DEPTH_8BIT:
			map_code = VIDEO_RGB444_8BIT;
			break;
		case HDMI_COLOR_DEPTH_10BIT:
			map_code = VIDEO_RGB444_10BIT;
			break;
		case HDMI_COLOR_DEPTH_12BIT:
			map_code = VIDEO_RGB444_12BIT;
			break;
		case HDMI_COLOR_DEPTH_16BIT:
			map_code = VIDEO_RGB444_16BIT;
			break;
		default:
			map_code = VIDEO_RGB444_8BIT;
			break;
		}
		map_code +=
		    (vpara->input_color == VIDEO_INPUT_COLOR_YCBCR444) ? 8 : 0;
	} else if (vpara->input_color == VIDEO_INPUT_COLOR_YCBCR422) {
		/* YCC422 mapping is discontinued - only map 1 is supported */
		switch (vpara->color_depth) {
		case HDMI_COLOR_DEPTH_8BIT:
			map_code = VIDEO_YCBCR422_8BIT;
			break;
		case HDMI_COLOR_DEPTH_10BIT:
			map_code = VIDEO_YCBCR422_10BIT;
			break;
		case HDMI_COLOR_DEPTH_12BIT:
			map_code = VIDEO_YCBCR422_12BIT;
			break;
		default:
			map_code = VIDEO_YCBCR422_8BIT;
			break;
		}
	} else {
		hdmi_err(hdmi_drv->dev, "invalid input color type: %d",
			 vpara->input_color);
		return -1;
	}

	/* Set Data enable signal from external
	* and set video sample input mapping
	*/
	hdmi_msk_reg(hdmi_dev, TX_INVID0, m_INTERNAL_DE_GEN | m_VIDEO_MAPPING,
		     v_INTERNAL_DE_GEN(0) | v_VIDEO_MAPPING(map_code));

#if defined(HDMI_VIDEO_STUFFING)
	hdmi_writel(hdmi_dev, TX_GYDATA0, 0x00);
	hdmi_writel(hdmi_dev, TX_GYDATA1, 0x00);
	hdmi_msk_reg(hdmi_dev, TX_INSTUFFING, m_GYDATA_STUFF,
		     v_GYDATA_STUFF(1));
	hdmi_writel(hdmi_dev, TX_RCRDATA0, 0x00);
	hdmi_writel(hdmi_dev, TX_RCRDATA1, 0x00);
	hdmi_msk_reg(hdmi_dev, TX_INSTUFFING, m_RCRDATA_STUFF,
		     v_RCRDATA_STUFF(1));
	hdmi_writel(hdmi_dev, TX_BCBDATA0, 0x00);
	hdmi_writel(hdmi_dev, TX_BCBDATA1, 0x00);
	hdmi_msk_reg(hdmi_dev, TX_INSTUFFING, m_BCBDATA_STUFF,
		     v_BCBDATA_STUFF(1));
#endif
	return 0;
}

#ifdef HDMI_DEBUG
static int rk3288_hdmi_read_phy(struct rk3288_hdmi_device *hdmi_dev,
				int reg_addr)
{
	int trytime = 2, i = 0, op_status = 0;
	int val = 0;

	while (trytime--) {
		hdmi_writel(hdmi_dev, PHY_I2CM_ADDRESS, reg_addr);
		hdmi_writel(hdmi_dev, PHY_I2CM_DATAI_1, 0x00);
		hdmi_writel(hdmi_dev, PHY_I2CM_DATAI_0, 0x00);
		hdmi_writel(hdmi_dev, PHY_I2CM_OPERATION, m_PHY_I2CM_READ);

		i = 20;
		while (i--) {
			msleep(1);
			op_status = hdmi_readl(hdmi_dev, IH_I2CMPHY_STAT0);
			if (op_status)
				hdmi_writel(hdmi_dev, IH_I2CMPHY_STAT0,
					    op_status);

			if (op_status & (m_I2CMPHY_DONE | m_I2CMPHY_ERR)) {
				break;
			}
			msleep(4);
		}

		if (op_status & m_I2CMPHY_DONE) {
			val = hdmi_readl(hdmi_dev, PHY_I2CM_DATAI_1);
			val = (val & 0xff) << 8;
			val += (hdmi_readl(hdmi_dev, PHY_I2CM_DATAI_0) & 0xff);
			hdmi_dbg(hdmi_dev->dev, "phy_reg0x%02x: 0x%04x",
				 reg_addr, val);
			return val;
		} else {
			hdmi_err(hdmi_dev->dev,
				 "[%s] operation error,trytime=%d\n",
				 __func__, trytime);
		}
		msleep(100);
	}

	return -1;
}
#endif

static int rk3288_hdmi_write_phy(struct rk3288_hdmi_device *hdmi_dev,
				 int reg_addr, int val)
{
	int trytime = 2, i = 0, op_status = 0;

	while (trytime--) {
		hdmi_writel(hdmi_dev, PHY_I2CM_ADDRESS, reg_addr);
		hdmi_writel(hdmi_dev, PHY_I2CM_DATAO_1, (val >> 8) & 0xff);
		hdmi_writel(hdmi_dev, PHY_I2CM_DATAO_0, val & 0xff);
		hdmi_writel(hdmi_dev, PHY_I2CM_OPERATION, m_PHY_I2CM_WRITE);

		i = 20;
		while (i--) {
			msleep(1);
			op_status = hdmi_readl(hdmi_dev, IH_I2CMPHY_STAT0);
			if (op_status)
				hdmi_writel(hdmi_dev, IH_I2CMPHY_STAT0,
					    op_status);

			if (op_status & (m_I2CMPHY_DONE | m_I2CMPHY_ERR)) {
				break;
			}
			msleep(4);
		}

		if (op_status & m_I2CMPHY_DONE) {
			return 0;
		} else {
			hdmi_err(hdmi_dev->dev,
				 "[%s] operation error,trytime=%d\n",
				 __func__, trytime);
		}
		msleep(100);
	}

	return -1;
}

int rk3288_hdmi_config_phy(struct hdmi *hdmi_drv, unsigned char pixel_repet,
			   unsigned char color_depth)
{
	int stat = 0, i = 0;
	const struct phy_mpll_config_tab *phy_mpll = NULL;
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	hdmi_msk_reg(hdmi_dev, PHY_I2CM_DIV, m_PHY_I2CM_FAST_STD,
		     v_PHY_I2CM_FAST_STD(0));

	/* power on PHY */
	hdmi_writel(hdmi_dev, PHY_CONF0, 0x3a);
	/*
	hdmi_msk_reg(hdmi_dev, PHY_CONF0, m_PDDQ_SIG | m_TXPWRON_SIG,
		     v_PDDQ_SIG(1) | v_TXPWRON_SIG(0));
	*/

	/* reset PHY */
	hdmi_writel(hdmi_dev, MC_PHYRSTZ, v_PHY_RSTZ(1));
	msleep(5);
	hdmi_writel(hdmi_dev, MC_PHYRSTZ, v_PHY_RSTZ(0));

	/* Set slave address as PHY GEN2 address */
	hdmi_writel(hdmi_dev, PHY_I2CM_SLAVE, PHY_GEN2_ADDR);

	/* config the required PHY I2C register */
	phy_mpll =
	    get_phy_mpll_tab(hdmi_drv->pixclock, pixel_repet, color_depth);
	if (phy_mpll) {
		rk3288_hdmi_write_phy(hdmi_dev, PHYTX_OPMODE_PLLCFG,
				      v_PREP_DIV(phy_mpll->prep_div) |
				      v_TMDS_CNTRL(phy_mpll->tmdsmhl_cntrl) |
				      v_OPMODE(phy_mpll->opmode) |
				      v_FBDIV2_CNTRL(phy_mpll->fbdiv2_cntrl) |
				      v_FBDIV1_CNTRL(phy_mpll->fbdiv1_cntrl) |
				      v_REF_CNTRL(phy_mpll->ref_cntrl) |
				      v_MPLL_N_CNTRL(phy_mpll->n_cntrl));
		rk3288_hdmi_write_phy(hdmi_dev, PHYTX_PLLCURRCTRL,
				      v_MPLL_PROP_CNTRL(phy_mpll->prop_cntrl) |
				      v_MPLL_INT_CNTRL(phy_mpll->int_cntrl));
		rk3288_hdmi_write_phy(hdmi_dev, PHYTX_PLLGMPCTRL,
				      v_MPLL_GMP_CNTRL(phy_mpll->gmp_cntrl));
	}
	if (hdmi_drv->pixclock <= 74250000) {
		rk3288_hdmi_write_phy(hdmi_dev, PHYTX_CLKSYMCTRL,
				      v_OVERRIDE(1) | v_SLOPEBOOST(0)
				      | v_TX_SYMON(1) | v_TX_TRAON(0) |
				      v_TX_TRBON(0) | v_CLK_SYMON(1));
		rk3288_hdmi_write_phy(hdmi_dev, PHYTX_TERM_RESIS,
				      v_TX_TERM(R100_Ohms));
	} else if (hdmi_drv->pixclock == 148500000) {
		rk3288_hdmi_write_phy(hdmi_dev, PHYTX_CLKSYMCTRL,
				      v_OVERRIDE(1) | v_SLOPEBOOST(3)
				      | v_TX_SYMON(1) | v_TX_TRAON(0) |
				      v_TX_TRBON(0) | v_CLK_SYMON(1));
		rk3288_hdmi_write_phy(hdmi_dev, PHYTX_TERM_RESIS,
				      v_TX_TERM(R100_Ohms));
	} else if (hdmi_drv->pixclock == 297000000) {
		rk3288_hdmi_write_phy(hdmi_dev, PHYTX_CLKSYMCTRL,
				      v_OVERRIDE(1) | v_SLOPEBOOST(2)
				      | v_TX_SYMON(1) | v_TX_TRAON(0) |
				      v_TX_TRBON(0) | v_CLK_SYMON(1));
		rk3288_hdmi_write_phy(hdmi_dev, PHYTX_TERM_RESIS,
				      v_TX_TERM(R100_Ohms));
	} else if (hdmi_drv->pixclock > 297000000) {
		/* TODO Daisen wait to add and modify */
		rk3288_hdmi_write_phy(hdmi_dev, PHYTX_TERM_RESIS,
				      v_TX_TERM(R13333_Ohms));
	}

	if (hdmi_drv->pixclock < 297000000)
		rk3288_hdmi_write_phy(hdmi_dev, PHYTX_VLEVCTRL,
				      v_SUP_TXLVL(20) | v_SUP_CLKLVL(19));
	else
		rk3288_hdmi_write_phy(hdmi_dev, PHYTX_VLEVCTRL,
				      v_SUP_TXLVL(17) | v_SUP_CLKLVL(16));

	/* power on PHY */
	hdmi_writel(hdmi_dev, PHY_CONF0, 0x6e);
	/*
	hdmi_msk_reg(hdmi_dev, PHY_CONF0,
		     m_PDDQ_SIG | m_TXPWRON_SIG | m_ENHPD_RXSENSE_SIG,
		     v_PDDQ_SIG(0) | v_TXPWRON_SIG(1) | v_ENHPD_RXSENSE_SIG(1));
	*/

	/* check if the PHY PLL is locked */
#define PHY_TIMEOUT	10000
	while (i++ < PHY_TIMEOUT) {
		if ((i % 100) == 0) {
			stat = hdmi_readl(hdmi_dev, PHY_STAT0);
			if (stat & m_PHY_LOCK) {
				break;
			}
		}
	}
	if ((stat & m_PHY_LOCK) == 0) {
		stat = hdmi_readl(hdmi_dev, MC_LOCKONCLOCK);
		hdmi_err(hdmi_dev->dev,
			 "PHY PLL not locked: PCLK_ON=%d,TMDSCLK_ON=%d\n",
			 (stat & m_PCLK_ON) >> 6, (stat & m_TMDSCLK_ON) >> 5);
		return -1;
	}

	return 0;
}

int rk3288_hdmi_config_vsi(struct hdmi *hdmi_drv, unsigned char vic_3d,
			   unsigned char format, int auto_send)
{
	int i = 0;
	unsigned char data[3] = { 0 };
	int id = 0x000c03;
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	hdmi_dbg(hdmi_drv->dev, "[%s] vic %d format %d.\n", __func__,
		 vic_3d, format);
	hdmi_msk_reg(hdmi_dev, FC_DATAUTO0, m_VSD_AUTO, v_VSD_AUTO(0));
	hdmi_writel(hdmi_dev, FC_VSDIEEEID0, id & 0xff);
	hdmi_writel(hdmi_dev, FC_VSDIEEEID1, (id >> 8) & 0xff);
	hdmi_writel(hdmi_dev, FC_VSDIEEEID2, (id >> 16) & 0xff);

	data[0] = format << 5;		/* PB4 --HDMI_Video_Format */
	switch (format) {
	case HDMI_VIDEO_FORMAT_4Kx2K:
		data[1] = vic_3d;	/* PB5--HDMI_VIC */
		data[2] = 0;
		break;
	case HDMI_VIDEO_FORMAT_3D:
		data[1] = vic_3d << 4;	/* PB5--3D_Structure field */
		data[2] = 0;		/* PB6--3D_Ext_Data field */
		break;
	default:
		data[1] = 0;
		data[2] = 0;
		break;
	}

	for (i = 0; i < 3; i++)
		hdmi_writel(hdmi_dev, FC_VSDPAYLOAD0 + i, data[i]);

	if (auto_send) {
		hdmi_msk_reg(hdmi_dev, FC_DATAUTO0, m_VSD_AUTO,
			     v_VSD_AUTO(auto_send));
	} else {
		hdmi_msk_reg(hdmi_dev, FC_DATMAN, m_VSD_MAN, v_VSD_MAN(1));
	}

	return 0;
}

static void rk3288_hdmi_config_avi(struct hdmi *hdmi_drv, unsigned char vic,
				   struct hdmi_video_para *vpara)
{
	unsigned char colorimetry, ext_colorimetry, aspect_ratio, y1y0;
	unsigned char rgb_quan_range = AVI_QUANTIZATION_RANGE_DEFAULT;
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	/* Set AVI infoFrame Data byte1 */
	if (vpara->output_color == VIDEO_OUTPUT_YCBCR444)
		y1y0 = AVI_COLOR_MODE_YCBCR444;
	else if (vpara->output_color == VIDEO_OUTPUT_YCBCR422)
		y1y0 = AVI_COLOR_MODE_YCBCR422;
	else if (vpara->output_color == VIDEO_OUTPUT_YCBCR420)
		y1y0 = AVI_COLOR_MODE_YCBCR420;
	else
		y1y0 = AVI_COLOR_MODE_RGB;

	hdmi_msk_reg(hdmi_dev, FC_AVICONF0, m_FC_ACTIV_FORMAT | m_FC_RGC_YCC,
		     v_FC_RGC_YCC(y1y0) | v_FC_ACTIV_FORMAT(1));

	/* Set AVI infoFrame Data byte2 */
	switch (vic) {
	case HDMI_720x480i_60Hz_4_3:
	case HDMI_720x576i_50Hz_4_3:
	case HDMI_720x480p_60Hz_4_3:
	case HDMI_720x576p_50Hz_4_3:
		aspect_ratio = AVI_CODED_FRAME_ASPECT_4_3;
		colorimetry = AVI_COLORIMETRY_SMPTE_170M;
		break;
	case HDMI_720x480i_60Hz_16_9:
	case HDMI_720x576i_50Hz_16_9:
	case HDMI_720x480p_60Hz_16_9:
	case HDMI_720x576p_50Hz_16_9:
		aspect_ratio = AVI_CODED_FRAME_ASPECT_16_9;
		colorimetry = AVI_COLORIMETRY_SMPTE_170M;
		break;
	default:
		aspect_ratio = AVI_CODED_FRAME_ASPECT_16_9;
		colorimetry = AVI_COLORIMETRY_ITU709;
	}

	if (vpara->color_depth > HDMI_COLOR_DEPTH_8BIT) {
		colorimetry = AVI_COLORIMETRY_EXTENDED;
		ext_colorimetry = 6;
	} else if (vpara->output_color == VIDEO_OUTPUT_RGB444) {
		colorimetry = AVI_COLORIMETRY_NO_DATA;
		ext_colorimetry = 0;
	}

	hdmi_writel(hdmi_dev, FC_AVICONF1,
		    v_FC_COLORIMETRY(colorimetry) |
		    v_FC_PIC_ASPEC_RATIO(aspect_ratio) |
		    v_FC_ACT_ASPEC_RATIO
		    (ACTIVE_ASPECT_RATE_SAME_AS_CODED_FRAME));

	/* Set AVI infoFrame Data byte3 */
	hdmi_msk_reg(hdmi_dev, FC_AVICONF2,
		     m_FC_EXT_COLORIMETRY | m_FC_QUAN_RANGE,
		     v_FC_EXT_COLORIMETRY(ext_colorimetry) |
		     v_FC_QUAN_RANGE(rgb_quan_range));

	/* Set AVI infoFrame Data byte4 */
	hdmi_writel(hdmi_dev, FC_AVIVID, (vic & 0xff));

	/* Set AVI infoFrame Data byte5 */
	hdmi_msk_reg(hdmi_dev, FC_AVICONF3, m_FC_YQ | m_FC_CN,
		     v_FC_YQ(YQ_LIMITED_RANGE) | v_FC_CN(CN_GRAPHICS));

}

static const char coeff_csc[][24] = {
	/*   G          R           B           Bias	*/
	/*   A1    |    A2     |    A3     |    A4    |	*/
	/*   B1    |    B2     |    B3     |    B4    |	*/
	/*   C1    |    C2     |    C3     |    C4    |	*/
	{	/* CSC_RGB_0_255_TO_RGB_16_235_8BIT */
	 0x1b, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,	/* G */
	 0x00, 0x00, 0x1b, 0x80, 0x00, 0x00, 0x00, 0x20,	/* R */
	 0x00, 0x00, 0x00, 0x00, 0x1b, 0x80, 0x00, 0x20,	/* B */
	},
	{	/* CSC_RGB_0_255_TO_RGB_16_235_10BIT */
	 0x1b, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,	/* G */
	 0x00, 0x00, 0x1b, 0x80, 0x00, 0x00, 0x00, 0x80,	/* R */
	 0x00, 0x00, 0x00, 0x00, 0x1b, 0x80, 0x00, 0x80,	/* B */
	},
#if 0
	{	/* CSC_RGB_0_255_TO_ITU601_16_235_8BIT */
	 0x25, 0x91, 0x13, 0x23, 0x07, 0x4c, 0x00, 0x00,	/* Y */
	 0xe5, 0x34, 0x20, 0x00, 0xfa, 0xcc, 0x02, 0x00,	/* Cr */
	 0xea, 0xcd, 0xf5, 0x33, 0x20, 0x00, 0x02, 0x00,	/* Cb */
	},
	{	/* CSC_RGB_0_255_TO_ITU601_16_235_10BIT */
	 0x25, 0x91, 0x13, 0x23, 0x07, 0x4c, 0x00, 0x00,	/* Y */
	 0xe5, 0x34, 0x20, 0x00, 0xfa, 0xcc, 0x08, 0x00,	/* Cr */
	 0xea, 0xcd, 0xf5, 0x33, 0x20, 0x00, 0x08, 0x00,	/* Cb */
	},
	{	/* CSC_RGB_0_255_TO_ITU709_16_235_8BIT */
	 0x2d, 0xc6, 0x0d, 0x9b, 0x04, 0x9f, 0x00, 0x00,	/* Y */
	 0xe2, 0xef, 0x20, 0x00, 0xfd, 0x11, 0x02, 0x00,	/* Cr */
	 0xe7, 0x55, 0xf8, 0xab, 0x20, 0x00, 0x02, 0x00,	/* Cb */
	},
	{	/* CSC_RGB_0_255_TO_ITU709_16_235_10BIT */
	 0x2d, 0xc6, 0x0d, 0x9b, 0x04, 0x9f, 0x00, 0x00,	/* Y */
	 0xe2, 0xef, 0x20, 0x00, 0xfd, 0x11, 0x08, 0x00,	/* Cr */
	 0xe7, 0x55, 0xf8, 0xab, 0x20, 0x00, 0x08, 0x00,	/* Cb */
	},
#else
	{	/* CSC_RGB_0_255_TO_ITU601_16_235_8BIT */
	 0x20, 0x40, 0x10, 0x80, 0x06, 0x40, 0x00, 0x40,	/* Y */
	 0xe8, 0x80, 0x1c, 0x00, 0xfb, 0x80, 0x02, 0x00,	/* Cr */
	 0xed, 0x80, 0xf6, 0x80, 0x1c, 0x00, 0x02, 0x00,	/* Cb */
	},
	{	/* CSC_RGB_0_255_TO_ITU601_16_235_10BIT */
	 0x20, 0x40, 0x10, 0x80, 0x06, 0x40, 0x01, 0x00,	/* Y */
	 0xe8, 0x80, 0x1c, 0x00, 0xfb, 0x80, 0x08, 0x00,	/* Cr */
	 0xed, 0x80, 0xf6, 0x80, 0x1c, 0x00, 0x08, 0x00,	/* Cb */
	},
	{	/* CSC_RGB_0_255_TO_ITU709_16_235_8BIT */
	 0x27, 0x40, 0x0b, 0xc0, 0x04, 0x00, 0x00, 0x40,	/* Y */
	 0xe6, 0x80, 0x1c, 0x00, 0xfd, 0x80, 0x02, 0x00,	/* Cr */
	 0xea, 0x40, 0xf9, 0x80, 0x1c, 0x00, 0x02, 0x00,	/* Cb */
	},
	{	/* CSC_RGB_0_255_TO_ITU709_16_235_10BIT */
	 0x27, 0x40, 0x0b, 0xc0, 0x04, 0x00, 0x01, 0x00,	/* Y */
	 0xe6, 0x80, 0x1c, 0x00, 0xfd, 0x80, 0x08, 0x00,	/* Cr */
	 0xea, 0x40, 0xf9, 0x80, 0x1c, 0x00, 0x08, 0x00,	/* Cb */
	},
#endif
	/* Y             Cr          Cb          Bias */
	{	/* CSC_ITU601_16_235_TO_RGB_0_255_8BIT */
	 0x20, 0x00, 0x69, 0x26, 0x74, 0xfd, 0x01, 0x0e,	/* G */
	 0x20, 0x00, 0x2c, 0xdd, 0x00, 0x00, 0x7e, 0x9a,	/* R */
	 0x20, 0x00, 0x00, 0x00, 0x38, 0xb4, 0x7e, 0x3b,	/* B */
	},
	{	/* CSC_ITU709_16_235_TO_RGB_0_255_8BIT */
	 0x20, 0x00, 0x71, 0x06, 0x7a, 0x02, 0x00, 0xa7,	/* G */
	 0x20, 0x00, 0x32, 0x64, 0x00, 0x00, 0x7e, 0x6d,	/* R */
	 0x20, 0x00, 0x00, 0x00, 0x3b, 0x61, 0x7e, 0x25,	/* B */
	},
};

static int rk3288_hdmi_video_csc(struct hdmi *hdmi_drv,
				 struct hdmi_video_para *vpara)
{
	int i, mode, interpolation, decimation, csc_scale;
	const char *coeff = NULL;
	unsigned char color_depth = 0;
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	if ((vpara->input_color == vpara->output_color)
	    && (vpara->color_limit_range == 0)) {
		hdmi_msk_reg(hdmi_dev, MC_FLOWCTRL, m_FEED_THROUGH_OFF,
			     v_FEED_THROUGH_OFF(0));
		return 0;
	}

	if (vpara->input_color == VIDEO_INPUT_COLOR_YCBCR422 &&
	    (vpara->output_color == VIDEO_OUTPUT_RGB444
	     || vpara->output_color == VIDEO_OUTPUT_YCBCR444)) {
		interpolation = 1;
		hdmi_msk_reg(hdmi_dev, CSC_CFG, m_CSC_INTPMODE,
			     v_CSC_INTPMODE(interpolation));
	}

	if ((vpara->input_color == VIDEO_INPUT_COLOR_RGB
	     || vpara->input_color == VIDEO_INPUT_COLOR_YCBCR444)
	    && vpara->output_color == VIDEO_OUTPUT_YCBCR422) {
		decimation = 1;
		hdmi_msk_reg(hdmi_dev, CSC_CFG, m_CSC_DECIMODE,
			     v_CSC_DECIMODE(decimation));
	}

	switch (vpara->vic) {
	case HDMI_720x480i_60Hz_4_3:
	case HDMI_720x576i_50Hz_4_3:
	case HDMI_720x480p_60Hz_4_3:
	case HDMI_720x576p_50Hz_4_3:
	case HDMI_720x480i_60Hz_16_9:
	case HDMI_720x576i_50Hz_16_9:
	case HDMI_720x480p_60Hz_16_9:
	case HDMI_720x576p_50Hz_16_9:
		if (vpara->input_color == VIDEO_INPUT_COLOR_RGB
		    && vpara->output_color >= VIDEO_OUTPUT_YCBCR444) {
			mode = CSC_RGB_0_255_TO_ITU601_16_235_8BIT;
			csc_scale = 0;
		} else if (vpara->input_color >= VIDEO_OUTPUT_YCBCR444
			   && vpara->output_color == VIDEO_OUTPUT_RGB444) {
			mode = CSC_ITU601_16_235_TO_RGB_0_255_8BIT;
			csc_scale = 1;
		}
		break;
	default:
		if (vpara->input_color == VIDEO_INPUT_COLOR_RGB
		    && vpara->output_color >= VIDEO_OUTPUT_YCBCR444) {
			mode = CSC_RGB_0_255_TO_ITU709_16_235_8BIT;
			csc_scale = 0;
		} else if (vpara->input_color >= VIDEO_OUTPUT_YCBCR444
			   && vpara->output_color == VIDEO_OUTPUT_RGB444) {
			mode = CSC_ITU709_16_235_TO_RGB_0_255_8BIT;
			csc_scale = 1;
		}
		break;
	}

	if ((vpara->input_color == VIDEO_INPUT_COLOR_RGB)
	    && (vpara->output_color == VIDEO_OUTPUT_RGB444)
	    && (vpara->color_limit_range == 1)) {
		mode = CSC_RGB_0_255_TO_RGB_16_235_8BIT;
		csc_scale = 1;
	}

	switch (vpara->color_depth) {
	case HDMI_COLOR_DEPTH_8BIT:
		color_depth = COLOR_DEPTH_24BIT;
		break;
	case HDMI_COLOR_DEPTH_10BIT:
		color_depth = COLOR_DEPTH_30BIT;
		mode += 1;
		break;
	case HDMI_COLOR_DEPTH_12BIT:
		color_depth = COLOR_DEPTH_36BIT;
		mode += 2;
		break;
	case HDMI_COLOR_DEPTH_16BIT:
		color_depth = COLOR_DEPTH_48BIT;
		mode += 3;
		break;
	default:
		color_depth = COLOR_DEPTH_24BIT;
		break;
	}

	coeff = coeff_csc[mode];
	for (i = 0; i < 24; i++) {
		hdmi_writel(hdmi_dev, CSC_COEF_A1_MSB + i, coeff[i]);
	}
	hdmi_msk_reg(hdmi_dev, CSC_SCALE, m_CSC_SCALE, v_CSC_SCALE(csc_scale));
	/* config CSC_COLOR_DEPTH */
	hdmi_msk_reg(hdmi_dev, CSC_SCALE, m_CSC_COLOR_DEPTH,
		     v_CSC_COLOR_DEPTH(color_depth));

	/* enable CSC */
	hdmi_msk_reg(hdmi_dev, MC_FLOWCTRL, m_FEED_THROUGH_OFF,
		     v_FEED_THROUGH_OFF(1));
	return 0;
}

int rk3288_hdmi_config_video(struct hdmi *hdmi_drv,
			     struct hdmi_video_para *vpara)
{
	if (rk3288_hdmi_video_forceOutput(hdmi_drv, 1) < 0)
		return -1;
	if (rk3288_hdmi_video_frameComposer(hdmi_drv, vpara) < 0)
		return -1;
	if (rk3288_hdmi_video_packetizer(hdmi_drv, vpara) < 0)
		return -1;
	if (rk3288_hdmi_video_csc(hdmi_drv, vpara) < 0)
		return -1;
	if (rk3288_hdmi_video_sampler(hdmi_drv, vpara) < 0)
		return -1;

	if (vpara->output_mode == OUTPUT_HDMI) {
		rk3288_hdmi_config_avi(hdmi_drv, vpara->vic, vpara);
		hdmi_dbg(hdmi_drv->dev, "[%s] sucess output HDMI.\n",
			 __func__);

		if (vpara->format_3d != 0)
			rk3288_hdmi_config_vsi(hdmi_drv, vpara->format_3d,
					       HDMI_VIDEO_FORMAT_3D, 1);
#ifndef HDMI_VERSION_2
		else if ((vpara->vic > 92 && vpara->vic < 96)
			 || (vpara->vic == 98)) {
			vpara->vic = (vpara->vic == 98) ? 4 : (96 - vpara->vic);
			rk3288_hdmi_config_vsi(hdmi_drv, vpara->vic,
					       HDMI_VIDEO_FORMAT_4Kx2K, 1);
		}
#endif
		else
			rk3288_hdmi_config_vsi(hdmi_drv, vpara->vic,
					       HDMI_VIDEO_FORMAT_NORMAL, 1);
	} else {
		hdmi_dbg(hdmi_drv->dev, "[%s] sucess output DVI.\n",
			 __func__);
	}

	rk3288_hdmi_set_pwr_mode(hdmi_drv, NORMAL);
	rk3288_hdmi_config_phy(hdmi_drv, vpara->pixel_repet,
			       vpara->color_depth);
	return 0;
}

static void rk3288_hdmi_config_aai(struct hdmi *hdmi_drv,
				   struct hdmi_audio *audio)
{
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	/* Refer to CEA861-E Audio infoFrame */
	/* Set both Audio Channel Count and
	* Audio Coding Type Refer to Stream Head for HDMI
	*/
	hdmi_msk_reg(hdmi_dev, FC_AUDICONF0, m_FC_CHN_CNT | m_FC_CODING_TYEP,
		     v_FC_CHN_CNT(0) | v_FC_CODING_TYEP(0));

	/* Set both Audio Sample Size and
	* Sample Frequency Refer to Stream Head for HDMI
	*/
	hdmi_msk_reg(hdmi_dev, FC_AUDICONF1,
		     m_FC_SAMPLE_SIZE | m_FC_SAMPLE_FREQ,
		     v_FC_SAMPLE_SIZE(0) | v_FC_SAMPLE_FREQ(0));

	/* Set Channel Allocation */
	hdmi_writel(hdmi_dev, FC_AUDICONF2, 0x00);

	/* Set LFEPBL¡¢DOWN-MIX INH and LSV */
	hdmi_writel(hdmi_dev, FC_AUDICONF3, 0x00);
}

int rk3288_hdmi_config_audio(struct hdmi *hdmi_drv, struct hdmi_audio *audio)
{
	int word_length = 0, channel = 0, mclk_fs;
	unsigned int N = 0, CTS = 0;
	unsigned char layout_value = 0;
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	if (audio->channel < 3)
		channel = I2S_CHANNEL_1_2;
	else if (audio->channel < 5)
		channel = I2S_CHANNEL_3_4;
	else if (audio->channel < 7)
		channel = I2S_CHANNEL_5_6;
	else
		channel = I2S_CHANNEL_7_8;

	switch (audio->rate) {
	case HDMI_AUDIO_FS_32000:
		mclk_fs = FS_256;
		if (hdmi_drv->pixclock >= 594000000)
			N = N_32K_HIGHCLK;
		else if (hdmi_drv->pixclock == 297000000)
			N = N_32K_MIDCLK;
		else
			N = N_32K_LOWCLK;

		/* div a num to avoid the value is exceed 2^32(int) */
		CTS = CALC_CTS(N, hdmi_drv->tmdsclk / 1000, 32);
		break;
	case HDMI_AUDIO_FS_44100:
		mclk_fs = FS_256;
		if (hdmi_drv->pixclock >= 594000000)
			N = N_441K_HIGHCLK;
		else if (hdmi_drv->pixclock == 297000000)
			N = N_441K_MIDCLK;
		else
			N = N_441K_LOWCLK;

		CTS = CALC_CTS(N, hdmi_drv->tmdsclk / 100, 441);
		break;
	case HDMI_AUDIO_FS_48000:
		mclk_fs = FS_256;
		if (hdmi_drv->pixclock >= 594000000)	/* FS_153.6 */
			N = N_48K_HIGHCLK;
		else if (hdmi_drv->pixclock == 297000000)
			N = N_48K_MIDCLK;
		else
			N = N_48K_LOWCLK;

		CTS = CALC_CTS(N, hdmi_drv->tmdsclk / 1000, 48);
		break;
	case HDMI_AUDIO_FS_88200:
		mclk_fs = FS_256;
		if (hdmi_drv->pixclock >= 594000000)
			N = N_882K_HIGHCLK;
		else if (hdmi_drv->pixclock == 297000000)
			N = N_882K_MIDCLK;
		else
			N = N_882K_LOWCLK;

		CTS = CALC_CTS(N, hdmi_drv->tmdsclk / 100, 882);
		break;
	case HDMI_AUDIO_FS_96000:
		mclk_fs = FS_256;
		if (hdmi_drv->pixclock >= 594000000)	/* FS_153.6 */
			N = N_96K_HIGHCLK;
		else if (hdmi_drv->pixclock == 297000000)
			N = N_96K_MIDCLK;
		else
			N = N_96K_LOWCLK;

		CTS = CALC_CTS(N, hdmi_drv->tmdsclk / 1000, 96);
		break;
	case HDMI_AUDIO_FS_176400:
		mclk_fs = FS_256;
		if (hdmi_drv->pixclock >= 594000000)
			N = N_1764K_HIGHCLK;
		else if (hdmi_drv->pixclock == 297000000)
			N = N_1764K_MIDCLK;
		else
			N = N_1764K_LOWCLK;

		CTS = CALC_CTS(N, hdmi_drv->tmdsclk / 100, 1764);
		break;
	case HDMI_AUDIO_FS_192000:
		mclk_fs = FS_256;
		if (hdmi_drv->pixclock >= 594000000)	/* FS_153.6 */
			N = N_192K_HIGHCLK;
		else if (hdmi_drv->pixclock == 297000000)
			N = N_192K_MIDCLK;
		else
			N = N_192K_LOWCLK;

		CTS = CALC_CTS(N, hdmi_drv->tmdsclk / 1000, 192);
		break;
	default:
		hdmi_err(hdmi_drv->dev,
			 "[%s] not support such sample rate %d\n", __func__,
			 audio->rate);
		return -ENOENT;
	}

	switch (audio->word_length) {
	case HDMI_AUDIO_WORD_LENGTH_16bit:
		word_length = I2S_16BIT_SAMPLE;
		break;
	case HDMI_AUDIO_WORD_LENGTH_20bit:
		word_length = I2S_20BIT_SAMPLE;
		break;
	case HDMI_AUDIO_WORD_LENGTH_24bit:
		word_length = I2S_24BIT_SAMPLE;
		break;
	default:
		word_length = I2S_16BIT_SAMPLE;
	}

	hdmi_dbg(hdmi_drv->dev, "rate = %d, tmdsclk = %d, N = %d, CTS = %d\n",
		 audio->rate, hdmi_drv->tmdsclk, N, CTS);
	/* more than 2 channels => layout 1 else layout 0
	* TODO Daisen wait to modify
	*/
	layout_value = (audio->channel > 2) ? 1 : 0;
	hdmi_msk_reg(hdmi_dev, FC_AUDSCONF, m_AUD_PACK_LAYOUT,
		     v_AUD_PACK_LAYOUT(layout_value));

	if (hdmi_drv->audio.type == INPUT_SPDIF) {
		hdmi_msk_reg(hdmi_dev, AUD_CONF0, m_I2S_SEL,
			     v_I2S_SEL(AUDIO_SPDIF_GPA));
		hdmi_msk_reg(hdmi_dev, AUD_SPDIF1, m_SET_NLPCM | m_SPDIF_WIDTH,
			     v_SET_NLPCM(PCM_LINEAR) |
			     v_SPDIF_WIDTH(word_length));
		/* Mask fifo empty and full int and reset fifo */
		hdmi_msk_reg(hdmi_dev, AUD_SPDIFINT,
			     m_FIFO_EMPTY_MASK | m_FIFO_FULL_MASK,
			     v_FIFO_EMPTY_MASK(1) | v_FIFO_FULL_MASK(1));
		hdmi_msk_reg(hdmi_dev, AUD_SPDIF0, m_SW_SAUD_FIFO_RST,
			     v_SW_SAUD_FIFO_RST(1));
	} else {
		hdmi_msk_reg(hdmi_dev, AUD_CONF0, m_I2S_SEL | m_I2S_IN_EN,
			     v_I2S_SEL(AUDIO_I2S) | v_I2S_IN_EN(channel));
		hdmi_writel(hdmi_dev, AUD_CONF1,
			    v_I2S_MODE(I2S_STANDARD_MODE) |
			    v_I2S_WIDTH(word_length));
		/* Mask fifo empty and full int and reset fifo */
		hdmi_msk_reg(hdmi_dev, AUD_INT,
			     m_FIFO_EMPTY_MASK | m_FIFO_FULL_MASK,
			     v_FIFO_EMPTY_MASK(1) | v_FIFO_FULL_MASK(1));
		hdmi_msk_reg(hdmi_dev, AUD_CONF0, m_SW_AUD_FIFO_RST,
			     v_SW_AUD_FIFO_RST(1));
	}

	hdmi_msk_reg(hdmi_dev, AUD_INPUTCLKFS, m_LFS_FACTOR,
		     v_LFS_FACTOR(mclk_fs));

	/* Set N value */
	hdmi_msk_reg(hdmi_dev, AUD_N3, m_AUD_N3, v_AUD_N3(N >> 16));
	hdmi_writel(hdmi_dev, AUD_N2, (N >> 8) & 0xff);
	hdmi_writel(hdmi_dev, AUD_N1, N & 0xff);
	/* Set CTS by manual */
	hdmi_msk_reg(hdmi_dev, AUD_CTS3, m_N_SHIFT | m_CTS_MANUAL | m_AUD_CTS3,
		     v_N_SHIFT(N_SHIFT_1) | v_CTS_MANUAL(1) | v_AUD_CTS3(CTS >>
									 16));
	hdmi_writel(hdmi_dev, AUD_CTS2, (CTS >> 8) & 0xff);
	hdmi_writel(hdmi_dev, AUD_CTS1, CTS & 0xff);

	hdmi_msk_reg(hdmi_dev, MC_CLKDIS, m_AUDCLK_DISABLE,
		     v_AUDCLK_DISABLE(0));
	rk3288_hdmi_config_aai(hdmi_drv, audio);

	return 0;
}

void rk3288_hdmi_control_output(struct hdmi *hdmi_drv, int enable)
{
	hdmi_dbg(hdmi_drv->dev, "[%s] %d\n", __func__, enable);
	if (enable == 0) {
		rk3288_hdmi_av_mute(hdmi_drv, 1);
		rk3288_hdmi_set_pwr_mode(hdmi_drv, LOWER_PWR);
	} else {
		if (hdmi_drv->pwr_mode == LOWER_PWR)
			rk3288_hdmi_set_pwr_mode(hdmi_drv, NORMAL);

		/* disable blue screen transmission
		* after turning on all necessary blocks
		*/
		rk3288_hdmi_video_forceOutput(hdmi_drv, 0);
		rk3288_hdmi_av_mute(hdmi_drv, 0);
	}
}

int rk3288_hdmi_insert(struct hdmi *hdmi_drv)
{
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	/* report HPD state to HDCP (after configuration) */
	hdmi_msk_reg(hdmi_dev, A_HDCPCFG0, m_RX_DETECT, v_RX_DETECT(1));

	return 0;
}

int rk3288_hdmi_removed(struct hdmi *hdmi_drv)
{
	rk3288_hdmi_control_output(hdmi_drv, 0);
	dev_printk(KERN_INFO, hdmi_drv->dev, "Removed.\n");
	return 0;
}

int rk3288_hdmi_initial(struct hdmi *hdmi_drv)
{
	int rc = HDMI_ERROR_SUCESS;

	hdmi_drv->pwr_mode = NORMAL;
	hdmi_drv->insert = rk3288_hdmi_insert;
	hdmi_drv->remove = rk3288_hdmi_removed;
	hdmi_drv->control_output = rk3288_hdmi_control_output;
	hdmi_drv->config_video = rk3288_hdmi_config_video;
	hdmi_drv->config_audio = rk3288_hdmi_config_audio;
	hdmi_drv->detect_hotplug = rk3288_hdmi_detect_hotplug;
	hdmi_drv->read_edid = rk3288_hdmi_read_edid;

	rk3288_hdmi_reset(hdmi_drv);

	if (hdmi_drv->hdcp_power_on_cb)
		rc = hdmi_drv->hdcp_power_on_cb();

	return rc;
}

irqreturn_t hdmi_irq(int irq, void *priv)
{
	struct hdmi *hdmi_drv = (struct hdmi *)priv;
	struct rk3288_hdmi_device *hdmi_dev =
	    container_of(hdmi_drv, struct rk3288_hdmi_device, driver);
	int phy_int = 0, i2cm_int = 0, phy_i2cm_int = 0, cec_int = 0;
	int aud_dma_int = 0;

	/* read interrupt */
	phy_int = hdmi_readl(hdmi_dev, IH_PHY_STAT0);
	i2cm_int = hdmi_readl(hdmi_dev, IH_I2CM_STAT0);
	phy_i2cm_int = hdmi_readl(hdmi_dev, IH_I2CMPHY_STAT0);
	cec_int = hdmi_readl(hdmi_dev, IH_CEC_STAT0);
	aud_dma_int = hdmi_readl(hdmi_dev, IH_AHBDMAAUD_STAT0);
	/*
	hdcp_int = hdmi_readl(hdmi_dev, A_APIINTSTAT);
	*/

	/* clear interrupt */
	hdmi_writel(hdmi_dev, IH_PHY_STAT0, phy_int);
	hdmi_writel(hdmi_dev, IH_I2CM_STAT0, i2cm_int);
	hdmi_writel(hdmi_dev, IH_I2CMPHY_STAT0, phy_i2cm_int);
	hdmi_writel(hdmi_dev, IH_CEC_STAT0, cec_int);
	hdmi_writel(hdmi_dev, IH_AHBDMAAUD_STAT0, aud_dma_int);
	/*
	hdmi_writel(hdmi_dev, A_APIINTCLR, hdcp_int);
	*/

	/* HPD or RX_SENSE */
	if ((phy_int & m_HPD) || ((phy_int & 0x3c) == 0x3c)) {
		if (hdmi_drv->state == HDMI_SLEEP)
			hdmi_drv->state = WAIT_HOTPLUG;
		queue_delayed_work(hdmi_drv->workqueue, &hdmi_drv->delay_work,
				   msecs_to_jiffies(5));
	}
	/* I2CM write or read result */
	if (i2cm_int & (m_SCDC_READREQ | m_I2CM_DONE | m_I2CM_ERROR)) {
		/* spin_lock(&hdmi_drv->irq_lock); */
		hdmi_dev->i2cm_int = i2cm_int;
		/* spin_unlock(&hdmi_drv->irq_lock); */
	}
	/* PHY I2CM write or read result */
	if (phy_i2cm_int & (m_I2CMPHY_DONE | m_I2CMPHY_ERR)) {
		/* mutex_lock(&hdmi_dev->int_mutex); */
		hdmi_dev->phy_i2cm_int = phy_i2cm_int;
		/* mutex_unlock(&hdmi_dev->int_mutex); */
	}
	/* CEC */
	if (cec_int) {
	}
	/* HDCP */
	if (hdmi_drv->hdcp_irq_cb)
		hdmi_drv->hdcp_irq_cb(i2cm_int);

	return IRQ_HANDLED;
}
