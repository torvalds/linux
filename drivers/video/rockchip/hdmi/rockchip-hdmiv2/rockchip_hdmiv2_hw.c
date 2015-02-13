#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include "rockchip_hdmiv2.h"
#include "rockchip_hdmiv2_hw.h"

static const struct phy_mpll_config_tab PHY_MPLL_TABLE[] = {
	/*tmdsclk = (pixclk / ref_cntrl ) * (fbdiv2 * fbdiv1) / nctrl / tmdsmhl
	  opmode: 0:HDMI1.4	1:HDMI2.0
	*/
/*	|pixclock|	tmdsclock|pixrepet|colordepth|prepdiv|tmdsmhl|opmode|
		fbdiv2|fbdiv1|ref_cntrl|nctrl|propctrl|intctrl|gmpctrl| */
	{27000000,	27000000,	0,	8,	0,	0,	0,
		2,	3,	0,	3,	3,	0,	0},
	{27000000,	33750000,	0,	10,	1,	0,	0,
		5,	1,	0,	3,	3,	0,	0},
	{27000000,	40500000,	0,	12,	2,	0,	0,
		3,	3,	0,	3,	3,	0,	0},
	{27000000,	54000000,	0,	16,	3,	0,	0,
		2,	3,	0,	2,	5,	0,	1},
/*	{74250000,	74250000,	0,	8,	0,	0,	0,
	1,	3,	0,	2,	5,	0,	1}, */
	{74250000,      74250000,	0,      8,      0,      0,      0,
		4,      3,      3,      2,      7,      0,      3},
	{74250000,	92812500,	0,	10,	1,	0,	0,
		5,	0,	1,	1,	7,	0,	2},
	{74250000,	111375000,	0,	12,	2,	0,	0,
		1,	2,	0,	1,	7,	0,	2},
	{74250000,	148500000,	0,	16,	3,	0,	0,
		1,	3,	0,	1,	7,	0,	2},
	{148500000,	74250000,	0,	8,	0,	0,	0,
		1,	1,	1,	1,	0,	0,	3},
	{148500000,	148500000,	0,	8,	0,	0,	0,
		1,	1,	0,	1,	0,	0,	3},
	{148500000,	185625000,	0,	10,	1,	0,	0,
		5,	0,	3,	0,	7,	0,	3},
	{148500000,	222750000,	0,	12,	2,	0,	0,
		1,	2,	1,	0,	7,	0,	3},
	{148500000,	297000000,	0,	16,	3,	0,	0,
		1,	1,	0,	0,	7,	0,	3},
	{297000000,	148500000,	0,	8,	0,	0,	0,
		1,	0,	1,	0,	0,	0,	3},
	{297000000,	297000000,	0,	8,	0,	0,	0,
		1,	0,	0,	0,	0,	0,	3},
	{297000000,	371250000,	0,	10,	1,	3,	1,
		5,	0,	3,	0,	7,	0,	3},
	{297000000,	445500000,	0,	12,	2,	3,	1,
		1,	2,	2,	0,	7,	0,	3},
	{297000000,	594000000,	0,	16,	1,	3,	1,
		1,	3,	1,	0,	0,	0,	3},
/*	{594000000,	297000000,	0,	8,	0,	0,	0,
		1,	3,	3,	1,	0,	0,	3},*/
	{594000000,	297000000,	0,	8,	0,	0,	0,
		1,	0,	1,	0,	0,	0,	3},
	{594000000,	594000000,	0,	8,	0,	3,	1,
		1,	3,	3,	0,	0,	0,	3},
};
/* ddc i2c master reset */
static void rockchip_hdmiv2_i2cm_reset(struct hdmi_dev *hdmi_dev)
{
	hdmi_msk_reg(hdmi_dev, I2CM_SOFTRSTZ,
		     m_I2CM_SOFTRST, v_I2CM_SOFTRST(0));
	usleep_range(90, 100);
}

/*set read/write offset,set read/write mode*/
static void rockchip_hdmiv2_i2cm_write_request(struct hdmi_dev *hdmi_dev,
					       u8 offset, u8 data)
{
	hdmi_writel(hdmi_dev, I2CM_ADDRESS, offset);
	hdmi_writel(hdmi_dev, I2CM_DATAO, data);
	hdmi_msk_reg(hdmi_dev, I2CM_OPERATION, m_I2CM_WR, v_I2CM_WR(1));
}

static void rockchip_hdmiv2_i2cm_read_request(struct hdmi_dev *hdmi_dev,
					      u8 offset)
{
	hdmi_writel(hdmi_dev, I2CM_ADDRESS, offset);
	hdmi_msk_reg(hdmi_dev, I2CM_OPERATION, m_I2CM_RD, v_I2CM_RD(1));
}

static void rockchip_hdmiv2_i2cm_write_data(struct hdmi_dev *hdmi_dev,
					    u8 data, u8 offset)
{
	u8 interrupt;
	int trytime = 2;
	int i = 20;

	while (trytime-- > 0) {
		rockchip_hdmiv2_i2cm_write_request(hdmi_dev, offset, data);
		while (i--) {
			usleep_range(900, 1000);
			interrupt = hdmi_readl(hdmi_dev, IH_I2CM_STAT0);
			if (interrupt)
				hdmi_writel(hdmi_dev,
					    IH_I2CM_STAT0, interrupt);

			if (interrupt & (m_SCDC_READREQ |
					 m_I2CM_DONE | m_I2CM_ERROR))
				break;
		}

		if (interrupt & m_I2CM_DONE) {
			dev_dbg(hdmi_dev->hdmi->dev,
				"[%s] write offset %02x data %02x success\n",
				__func__, offset, data);
			trytime = 0;
		} else if ((interrupt & m_I2CM_ERROR) || (i == -1)) {
			dev_err(hdmi_dev->hdmi->dev,
				"[%s] write data error\n", __func__);
			rockchip_hdmiv2_i2cm_reset(hdmi_dev);
		}
	}
}

static int rockchip_hdmiv2_i2cm_read_data(struct hdmi_dev *hdmi_dev, u8 offset)
{
	u8 interrupt, val;
	int trytime = 2;
	int i = 20;

	while (trytime-- > 0) {
		rockchip_hdmiv2_i2cm_read_request(hdmi_dev, offset);
		while (i--) {
			usleep_range(900, 1000);
			interrupt = hdmi_readl(hdmi_dev, IH_I2CM_STAT0);
			if (interrupt)
				hdmi_writel(hdmi_dev, IH_I2CM_STAT0, interrupt);

			if (interrupt & (m_SCDC_READREQ |
				m_I2CM_DONE | m_I2CM_ERROR))
				break;
		}

		if (interrupt & m_I2CM_DONE) {
			val = hdmi_readl(hdmi_dev, I2CM_DATAI);
			trytime = 0;
		} else if ((interrupt & m_I2CM_ERROR) || (i == -1)) {
			pr_err("[%s] read data error\n", __func__);
			rockchip_hdmiv2_i2cm_reset(hdmi_dev);
		}
	}
	return val;
}

static void rockchip_hdmiv2_i2cm_mask_int(struct hdmi_dev *hdmi_dev, int mask)
{
	if (0 == mask) {
		hdmi_msk_reg(hdmi_dev, I2CM_INT,
			     m_I2CM_DONE_MASK, v_I2CM_DONE_MASK(0));
		hdmi_msk_reg(hdmi_dev, I2CM_CTLINT,
			     m_I2CM_NACK_MASK | m_I2CM_ARB_MASK,
			     v_I2CM_NACK_MASK(0) | v_I2CM_ARB_MASK(0));
	} else {
		hdmi_msk_reg(hdmi_dev, I2CM_INT,
			     m_I2CM_DONE_MASK, v_I2CM_DONE_MASK(1));
		hdmi_msk_reg(hdmi_dev, I2CM_CTLINT,
			     m_I2CM_NACK_MASK | m_I2CM_ARB_MASK,
			     v_I2CM_NACK_MASK(1) | v_I2CM_ARB_MASK(1));
	}
}

#define I2C_DIV_FACTOR 100000
static u16 i2c_count(u16 sfrclock, u16 sclmintime)
{
	unsigned long tmp_scl_period = 0;

	if (((sfrclock * sclmintime) % I2C_DIV_FACTOR) != 0)
		tmp_scl_period = (unsigned long)((sfrclock * sclmintime) +
				(I2C_DIV_FACTOR - ((sfrclock * sclmintime) %
				I2C_DIV_FACTOR))) / I2C_DIV_FACTOR;
	else
		tmp_scl_period = (unsigned long)(sfrclock * sclmintime) /
				I2C_DIV_FACTOR;

	return (u16)(tmp_scl_period);
}

#define EDID_I2C_MIN_SS_SCL_HIGH_TIME	50000
#define EDID_I2C_MIN_SS_SCL_LOW_TIME	50000

static void rockchip_hdmiv2_i2cm_clk_init(struct hdmi_dev *hdmi_dev)
{
	/* Set DDC I2C CLK which devided from DDC_CLK. */
	hdmi_writel(hdmi_dev, I2CM_SS_SCL_HCNT_0_ADDR,
		    i2c_count(24000, EDID_I2C_MIN_SS_SCL_HIGH_TIME));
	hdmi_writel(hdmi_dev, I2CM_SS_SCL_LCNT_0_ADDR,
		    i2c_count(24000, EDID_I2C_MIN_SS_SCL_LOW_TIME));
	hdmi_msk_reg(hdmi_dev, I2CM_DIV, m_I2CM_FAST_STD_MODE,
		     v_I2CM_FAST_STD_MODE(STANDARD_MODE));
}

static int rockchip_hdmiv2_scdc_get_sink_version(struct hdmi_dev *hdmi_dev)
{
	return rockchip_hdmiv2_i2cm_read_data(hdmi_dev, SCDC_SINK_VER);
}

static void rockchip_hdmiv2_scdc_set_source_version(struct hdmi_dev *hdmi_dev,
						    u8 version)
{
	rockchip_hdmiv2_i2cm_write_data(hdmi_dev, version, SCDC_SOURCE_VER);
}


static void rockchip_hdmiv2_scdc_read_request(struct hdmi_dev *hdmi_dev,
					      int enable)
{
	hdmi_msk_reg(hdmi_dev, I2CM_SCDC_READ_UPDATE,
		     m_I2CM_READ_REQ_EN, v_I2CM_READ_REQ_EN(enable));
	rockchip_hdmiv2_i2cm_write_data(hdmi_dev, enable, SCDC_CONFIG_0);
}

#ifdef HDMI_20_SCDC
static void rockchip_hdmiv2_scdc_update_read(struct hdmi_dev *hdmi_dev)
{
	hdmi_msk_reg(hdmi_dev, I2CM_SCDC_READ_UPDATE,
		     m_I2CM_READ_UPDATE, v_I2CM_READ_UPDATE(1));
}


static int rockchip_hdmiv2_scdc_get_scambling_status(struct hdmi_dev *hdmi_dev)
{
	int val;

	val = rockchip_hdmiv2_i2cm_read_data(hdmi_dev, SCDC_SCRAMBLER_STAT);
	return val;
}

static void rockchip_hdmiv2_scdc_enable_polling(struct hdmi_dev *hdmi_dev,
						int enable)
{
	rockchip_hdmiv2_scdc_read_request(hdmi_dev, enable);
	hdmi_msk_reg(hdmi_dev, I2CM_SCDC_READ_UPDATE,
		     m_I2CM_UPRD_VSYNC_EN, v_I2CM_UPRD_VSYNC_EN(enable));
}

static int rockchip_hdmiv2_scdc_get_status_reg0(struct hdmi_dev *hdmi_dev)
{
	rockchip_hdmiv2_scdc_read_request(hdmi_dev, 1);
	rockchip_hdmiv2_scdc_update_read(hdmi_dev);
	return hdmi_readl(hdmi_dev, I2CM_SCDC_UPDATE0);
}

static int rockchip_hdmiv2_scdc_get_status_reg1(struct hdmi_dev *hdmi_dev)
{
	rockchip_hdmiv2_scdc_read_request(hdmi_dev, 1);
	rockchip_hdmiv2_scdc_update_read(hdmi_dev);
	return hdmi_readl(hdmi_dev, I2CM_SCDC_UPDATE1);
}
#endif

static void rockchip_hdmiv2_scdc_init(struct hdmi_dev *hdmi_dev)
{
	rockchip_hdmiv2_i2cm_reset(hdmi_dev);
	rockchip_hdmiv2_i2cm_mask_int(hdmi_dev, 1);
	rockchip_hdmiv2_i2cm_clk_init(hdmi_dev);
	/* set scdc i2c addr */
	hdmi_writel(hdmi_dev, I2CM_SLAVE, DDC_I2C_SCDC_ADDR);
	rockchip_hdmiv2_i2cm_mask_int(hdmi_dev, 0);/*enable interrupt*/
}


static int rockchip_hdmiv2_scrambling_enable(struct hdmi_dev *hdmi_dev,
					     int enable)
{
	HDMIDBG("%s enable %d\n", __func__, enable);
	if (1 == enable) {
		/* Write on Rx the bit Scrambling_Enable, register 0x20 */
		rockchip_hdmiv2_i2cm_write_data(hdmi_dev, 1, SCDC_TMDS_CONFIG);
		/* TMDS software reset request */
		hdmi_msk_reg(hdmi_dev, MC_SWRSTZREQ,
			     m_TMDS_SWRST, v_TMDS_SWRST(0));
		/* Enable/Disable Scrambling */
		hdmi_msk_reg(hdmi_dev, FC_SCRAMBLER_CTRL,
			     m_FC_SCRAMBLE_EN, v_FC_SCRAMBLE_EN(1));
	} else {
		/* Enable/Disable Scrambling */
		hdmi_msk_reg(hdmi_dev, FC_SCRAMBLER_CTRL,
			     m_FC_SCRAMBLE_EN, v_FC_SCRAMBLE_EN(0));
		/* TMDS software reset request */
		hdmi_msk_reg(hdmi_dev, MC_SWRSTZREQ,
			     m_TMDS_SWRST, v_TMDS_SWRST(0));
		/* Write on Rx the bit Scrambling_Enable, register 0x20 */
		rockchip_hdmiv2_i2cm_write_data(hdmi_dev, 0, SCDC_TMDS_CONFIG);
	}
	return 0;
}



static const struct phy_mpll_config_tab *get_phy_mpll_tab(
		unsigned int pixclock, unsigned int tmdsclk,
		char pixrepet, char colordepth)
{
	int i;

	if (pixclock == 0)
		return NULL;
	HDMIDBG("%s pixClock %u pixRepet %d colorDepth %d\n",
		__func__, pixclock, pixrepet, colordepth);
	for (i = 0; i < ARRAY_SIZE(PHY_MPLL_TABLE); i++) {
		if ((PHY_MPLL_TABLE[i].pix_clock == pixclock) &&
		    (PHY_MPLL_TABLE[i].tmdsclock == tmdsclk) &&
		    (PHY_MPLL_TABLE[i].pix_repet == pixrepet) &&
		    (PHY_MPLL_TABLE[i].color_depth == colordepth))
			return &PHY_MPLL_TABLE[i];
	}
	return NULL;
}

static void rockchip_hdmiv2_powerdown(struct hdmi_dev *hdmi_dev)
{
	hdmi_msk_reg(hdmi_dev, PHY_CONF0,
		     m_PDDQ_SIG | m_TXPWRON_SIG | m_ENHPD_RXSENSE_SIG,
		     v_PDDQ_SIG(1) | v_TXPWRON_SIG(0) |
		     v_ENHPD_RXSENSE_SIG(1));
	hdmi_writel(hdmi_dev, MC_CLKDIS, 0x7f);
}

static int rockchip_hdmiv2_write_phy(struct hdmi_dev *hdmi_dev,
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
			usleep_range(900, 1000);
			op_status = hdmi_readl(hdmi_dev, IH_I2CMPHY_STAT0);
			if (op_status)
				hdmi_writel(hdmi_dev,
					    IH_I2CMPHY_STAT0,
					    op_status);

			if (op_status & (m_I2CMPHY_DONE | m_I2CMPHY_ERR))
				break;
		}

		if (op_status & m_I2CMPHY_DONE)
			return 0;
		else
			dev_err(hdmi_dev->hdmi->dev,
				"[%s] operation error,trytime=%d\n",
				__func__, trytime);
		msleep(100);
	}

	return -1;
}

static int __maybe_unused rockchip_hdmiv2_read_phy(struct hdmi_dev *hdmi_dev,
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
			usleep_range(900, 1000);
			op_status = hdmi_readl(hdmi_dev, IH_I2CMPHY_STAT0);
			if (op_status)
				hdmi_writel(hdmi_dev, IH_I2CMPHY_STAT0,
					    op_status);

			if (op_status & (m_I2CMPHY_DONE | m_I2CMPHY_ERR))
				break;
		}

		if (op_status & m_I2CMPHY_DONE) {
			val = hdmi_readl(hdmi_dev, PHY_I2CM_DATAI_1);
			val = (val & 0xff) << 8;
			val += (hdmi_readl(hdmi_dev, PHY_I2CM_DATAI_0) & 0xff);
			pr_debug("phy_reg0x%02x: 0x%04x",
				 reg_addr, val);
			return val;
		} else {
			pr_err("[%s] operation error,trytime=%d\n",
			       __func__, trytime);
		}
		msleep(100);
	}

	return -1;
}

void rockchip_hdmiv2_dump_phy_regs(struct hdmi_dev *hdmi_dev)
{
	int i;

	for (i = 0; i < 0x28; i++)
		pr_info("phy reg %02x val %04x\n",
			i, rockchip_hdmiv2_read_phy(hdmi_dev, i));
}

static int rockchip_hdmiv2_config_phy(struct hdmi_dev *hdmi_dev)
{
	int stat = 0, i = 0;
	const struct phy_mpll_config_tab *phy_mpll = NULL;

	hdmi_msk_reg(hdmi_dev, PHY_I2CM_DIV,
		     m_PHY_I2CM_FAST_STD, v_PHY_I2CM_FAST_STD(0));
	/* power off PHY */
	/* hdmi_writel(hdmi_dev, PHY_CONF0, 0x1e); */
	hdmi_msk_reg(hdmi_dev, PHY_CONF0,
		     m_PDDQ_SIG | m_TXPWRON_SIG | m_SVSRET_SIG,
		     v_PDDQ_SIG(1) | v_TXPWRON_SIG(0) | v_SVSRET_SIG(1));

	if (hdmi_dev->tmdsclk_ratio_change &&
	    hdmi_dev->hdmi->edid.scdc_present == 1) {
		mutex_lock(&hdmi_dev->ddc_lock);
		rockchip_hdmiv2_scdc_init(hdmi_dev);
		stat = rockchip_hdmiv2_i2cm_read_data(hdmi_dev,
						      SCDC_TMDS_CONFIG);
		if (hdmi_dev->tmdsclk > 340000000)
			stat |= 2;
		else
			stat &= 0x1;
		rockchip_hdmiv2_i2cm_write_data(hdmi_dev,
						stat, SCDC_TMDS_CONFIG);
		mutex_unlock(&hdmi_dev->ddc_lock);
	}
	/* reset PHY */
	hdmi_writel(hdmi_dev, MC_PHYRSTZ, v_PHY_RSTZ(1));
	usleep_range(1000, 2000);
	hdmi_writel(hdmi_dev, MC_PHYRSTZ, v_PHY_RSTZ(0));

	/* Set slave address as PHY GEN2 address */
	hdmi_writel(hdmi_dev, PHY_I2CM_SLAVE, PHY_GEN2_ADDR);

	/* config the required PHY I2C register */
	phy_mpll = get_phy_mpll_tab(hdmi_dev->pixelclk,
				    hdmi_dev->tmdsclk,
				    hdmi_dev->pixelrepeat - 1,
				    hdmi_dev->colordepth);
	if (phy_mpll) {
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_OPMODE_PLLCFG,
					  v_PREP_DIV(phy_mpll->prep_div) |
					  v_TMDS_CNTRL(
					  phy_mpll->tmdsmhl_cntrl) |
					  v_OPMODE(phy_mpll->opmode) |
					  v_FBDIV2_CNTRL(
					  phy_mpll->fbdiv2_cntrl) |
					  v_FBDIV1_CNTRL(
					  phy_mpll->fbdiv1_cntrl) |
					  v_REF_CNTRL(phy_mpll->ref_cntrl) |
					  v_MPLL_N_CNTRL(phy_mpll->n_cntrl));
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_PLLCURRCTRL,
					  v_MPLL_PROP_CNTRL(
					  phy_mpll->prop_cntrl) |
					  v_MPLL_INT_CNTRL(
					  phy_mpll->int_cntrl));
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_PLLGMPCTRL,
					  v_MPLL_GMP_CNTRL(
					  phy_mpll->gmp_cntrl));
	}
	if (hdmi_dev->tmdsclk <= 74250000) {
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_CLKSYMCTRL,
					  v_OVERRIDE(1) | v_SLOPEBOOST(0) |
					  v_TX_SYMON(1) | v_TX_TRAON(0) |
					  v_TX_TRBON(0) | v_CLK_SYMON(1));
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_TERM_RESIS,
					  v_TX_TERM(R100_OHMS));
	} else if (hdmi_dev->tmdsclk <= 148500000) {
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_CLKSYMCTRL,
					  v_OVERRIDE(1) | v_SLOPEBOOST(1) |
					  v_TX_SYMON(1) | v_TX_TRAON(0) |
					  v_TX_TRBON(0) | v_CLK_SYMON(1));
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_TERM_RESIS,
					  v_TX_TERM(R100_OHMS));
	} else if (hdmi_dev->tmdsclk <= 340000000) {
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_CLKSYMCTRL,
					  v_OVERRIDE(1) | v_SLOPEBOOST(1) |
					  v_TX_SYMON(1) | v_TX_TRAON(0) |
					  v_TX_TRBON(0) | v_CLK_SYMON(1));
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_TERM_RESIS,
					  v_TX_TERM(R100_OHMS));
	} else if (hdmi_dev->tmdsclk > 340000000) {
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_CLKSYMCTRL,
					  v_OVERRIDE(1) | v_SLOPEBOOST(0) |
					  v_TX_SYMON(1) | v_TX_TRAON(0) |
					  v_TX_TRBON(0) | v_CLK_SYMON(1));
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_TERM_RESIS,
					  v_TX_TERM(R100_OHMS));
	}

	if (hdmi_dev->tmdsclk < 297000000)
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_VLEVCTRL,
					  v_SUP_TXLVL(18) | v_SUP_CLKLVL(17));
	else
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_VLEVCTRL,
					  v_SUP_TXLVL(14) | v_SUP_CLKLVL(13));

	rockchip_hdmiv2_write_phy(hdmi_dev, 0x05, 0x8000);
	if (hdmi_dev->tmdsclk_ratio_change)
		msleep(100);
	/* power on PHY */
	hdmi_writel(hdmi_dev, PHY_CONF0, 0x2e);
	/*
	hdmi_msk_reg(hdmi_dev, PHY_CONF0,
		     m_PDDQ_SIG | m_TXPWRON_SIG | m_ENHPD_RXSENSE_SIG,
		     v_PDDQ_SIG(0) | v_TXPWRON_SIG(1) |
		     v_ENHPD_RXSENSE_SIG(1));
	*/
	/* check if the PHY PLL is locked */
	#define PHY_TIMEOUT	10000
	while (i++ < PHY_TIMEOUT) {
		if ((i % 10) == 0) {
			stat = hdmi_readl(hdmi_dev, PHY_STAT0);
			if (stat & m_PHY_LOCK)
				break;
			usleep_range(1000, 2000);
		}
	}
	if ((stat & m_PHY_LOCK) == 0) {
		stat = hdmi_readl(hdmi_dev, MC_LOCKONCLOCK);
		dev_err(hdmi_dev->hdmi->dev,
			"PHY PLL not locked: PCLK_ON=%d,TMDSCLK_ON=%d\n",
			(stat & m_PCLK_ON) >> 6, (stat & m_TMDSCLK_ON) >> 5);
		return -1;
	}

	return 0;
}

static int rockchip_hdmiv2_video_framecomposer(struct hdmi *hdmi_drv,
					       struct hdmi_video *vpara)
{
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;
	int value, vsync_pol, hsync_pol, de_pol;
	struct hdmi_video_timing *timing = NULL;
	struct fb_videomode *mode = NULL;
	u32 sink_version, tmdsclk;

	vsync_pol = hdmi_drv->lcdc->cur_screen->pin_vsync;
	hsync_pol = hdmi_drv->lcdc->cur_screen->pin_hsync;
	de_pol = (hdmi_drv->lcdc->cur_screen->pin_den == 0) ? 1 : 0;

	hdmi_msk_reg(hdmi_dev, A_VIDPOLCFG,
		     m_DATAEN_POL | m_VSYNC_POL | m_HSYNC_POL,
		     v_DATAEN_POL(de_pol) |
		     v_VSYNC_POL(vsync_pol) |
		     v_HSYNC_POL(hsync_pol));

	timing = (struct hdmi_video_timing *)hdmi_vic2timing(vpara->vic);
	if (timing == NULL) {
		dev_err(hdmi_drv->dev,
			"[%s] not found vic %d\n", __func__, vpara->vic);
		return -ENOENT;
	}
	mode = &(timing->mode);
	if (vpara->color_input == HDMI_COLOR_YCBCR420)
		tmdsclk = mode->pixclock / 2;
	else
		tmdsclk = mode->pixclock;
	switch (vpara->color_output_depth) {
	case 10:
		tmdsclk += tmdsclk / 4;
		break;
	case 12:
		tmdsclk += tmdsclk / 2;
		break;
	case 16:
		tmdsclk += tmdsclk;
		break;
	case 8:
	default:
		break;
	}

	if (tmdsclk > 594000000) {
		vpara->color_output_depth = 8;
		tmdsclk = mode->pixclock;
	}
	pr_info("pixel clk is %u tmds clk is %u\n", mode->pixclock, tmdsclk);
	if ((tmdsclk > 340000000 && hdmi_dev->tmdsclk < 340000000) ||
	    (tmdsclk < 340000000 && hdmi_dev->tmdsclk > 340000000))
		hdmi_dev->tmdsclk_ratio_change = true;
	else
		hdmi_dev->tmdsclk_ratio_change = false;

	hdmi_dev->tmdsclk = tmdsclk;
	hdmi_dev->pixelclk = mode->pixclock;
	hdmi_dev->pixelrepeat = timing->pixelrepeat;
	hdmi_dev->colordepth = vpara->color_output_depth;

	/* Video Register has already been set in uboot,
	   so we no need to set again */

	if (hdmi_drv->uboot)
		return -1;

	/* Start/stop HDCP keepout window generation */
	hdmi_msk_reg(hdmi_dev, FC_INVIDCONF,
		     m_FC_HDCP_KEEPOUT, v_FC_HDCP_KEEPOUT(1));
	if (hdmi_drv->edid.scdc_present == 1) {
		if (tmdsclk > 340000000) {/* used for HDMI 2.0 TX */
			mutex_lock(&hdmi_dev->ddc_lock);
			rockchip_hdmiv2_scdc_init(hdmi_dev);
			sink_version =
			rockchip_hdmiv2_scdc_get_sink_version(hdmi_dev);
			pr_info("sink scdc version is %d\n", sink_version);
			sink_version = hdmi_drv->edid.hf_vsdb_version;
			rockchip_hdmiv2_scdc_set_source_version(hdmi_dev,
								sink_version);
			if (hdmi_drv->edid.rr_capable == 1)
				rockchip_hdmiv2_scdc_read_request(hdmi_dev, 1);
			rockchip_hdmiv2_scrambling_enable(hdmi_dev, 1);
			mutex_unlock(&hdmi_dev->ddc_lock);
		} else {
			mutex_lock(&hdmi_dev->ddc_lock);
			rockchip_hdmiv2_scdc_init(hdmi_dev);
			rockchip_hdmiv2_scrambling_enable(hdmi_dev, 0);
			mutex_unlock(&hdmi_dev->ddc_lock);
		}
	}

	hdmi_msk_reg(hdmi_dev, FC_INVIDCONF,
		     m_FC_VSYNC_POL | m_FC_HSYNC_POL | m_FC_DE_POL |
		     m_FC_HDMI_DVI | m_FC_INTERLACE_MODE,
		     v_FC_VSYNC_POL(vsync_pol) | v_FC_HSYNC_POL(hsync_pol) |
		     v_FC_DE_POL(de_pol) | v_FC_HDMI_DVI(vpara->sink_hdmi) |
		     v_FC_INTERLACE_MODE(mode->vmode));
	if (mode->vmode == FB_VMODE_INTERLACED)
		hdmi_msk_reg(hdmi_dev, FC_INVIDCONF,
			     m_FC_VBLANK, v_FC_VBLANK(1));
	else
		hdmi_msk_reg(hdmi_dev, FC_INVIDCONF,
			     m_FC_VBLANK, v_FC_VBLANK(0));

	value = mode->xres;
	if (vpara->color_input == HDMI_COLOR_YCBCR420)
		value = value / 2;
	hdmi_writel(hdmi_dev, FC_INHACTIV1, v_FC_HACTIVE1(value >> 8));
	hdmi_writel(hdmi_dev, FC_INHACTIV0, (value & 0xff));

	value = mode->yres;
	hdmi_writel(hdmi_dev, FC_INVACTIV1, v_FC_VACTIVE1(value >> 8));
	hdmi_writel(hdmi_dev, FC_INVACTIV0, (value & 0xff));

	value = mode->hsync_len + mode->left_margin + mode->right_margin;
	if (vpara->color_input == HDMI_COLOR_YCBCR420)
		value = value / 2;
	hdmi_writel(hdmi_dev, FC_INHBLANK1, v_FC_HBLANK1(value >> 8));
	hdmi_writel(hdmi_dev, FC_INHBLANK0, (value & 0xff));

	value = mode->vsync_len + mode->upper_margin + mode->lower_margin;
	hdmi_writel(hdmi_dev, FC_INVBLANK, (value & 0xff));

	value = mode->right_margin;
	if (vpara->color_input == HDMI_COLOR_YCBCR420)
		value = value / 2;
	hdmi_writel(hdmi_dev, FC_HSYNCINDELAY1, v_FC_HSYNCINDEAY1(value >> 8));
	hdmi_writel(hdmi_dev, FC_HSYNCINDELAY0, (value & 0xff));

	value = mode->lower_margin;
	hdmi_writel(hdmi_dev, FC_VSYNCINDELAY, (value & 0xff));

	value = mode->hsync_len;
	if (vpara->color_input == HDMI_COLOR_YCBCR420)
		value = value / 2;
	hdmi_writel(hdmi_dev, FC_HSYNCINWIDTH1, v_FC_HSYNCWIDTH1(value >> 8));
	hdmi_writel(hdmi_dev, FC_HSYNCINWIDTH0, (value & 0xff));

	value = mode->vsync_len;
	hdmi_writel(hdmi_dev, FC_VSYNCINWIDTH, (value & 0xff));

	/*Set the control period minimum duration
	 (min. of 12 pixel clock cycles, refer to HDMI 1.4b specification)*/
	hdmi_writel(hdmi_dev, FC_CTRLDUR, 12);
	hdmi_writel(hdmi_dev, FC_EXCTRLDUR, 32);

	hdmi_writel(hdmi_dev, FC_EXCTRLSPAC,
		    (hdmi_dev->tmdsclk/1000) * 50 / (256 * 512));

#if 0
	/* spacing < 256^2 * config / tmdsClock, spacing <= 50ms
	 * worst case: tmdsClock == 25MHz => config <= 19
	 */
	hdmi_writel(hdmi_dev, FC_EXCTRLSPAC, 1);

	/*Set PreambleFilter*/
	for (i = 0; i < 3; i++) {
		value = (i + 1) * 11;
		if (i == 0)		/*channel 0*/
			hdmi_writel(hdmi_dev, FC_CH0PREAM, value);
		else if (i == 1)	/*channel 1*/
			hdmi_writel(hdmi_dev, FC_CH1PREAM, value & 0x3f);
		else if (i == 2)	/*channel 2*/
			hdmi_writel(hdmi_dev, FC_CH2PREAM, value & 0x3f);
	}
#endif

	hdmi_writel(hdmi_dev, FC_PRCONF, v_FC_PR_FACTOR(timing->pixelrepeat));

	return 0;
}

static int rockchip_hdmiv2_video_packetizer(struct hdmi_dev *hdmi_dev,
					    struct hdmi_video *vpara)
{
	unsigned char color_depth = 0;
	unsigned char output_select = 0;
	unsigned char remap_size = 0;

	if (vpara->color_output == HDMI_COLOR_YCBCR422) {
		switch (vpara->color_output_depth) {
		case 8:
			remap_size = YCC422_16BIT;
			break;
		case 10:
			remap_size = YCC422_20BIT;
			break;
		case 12:
			remap_size = YCC422_24BIT;
			break;
		default:
			remap_size = YCC422_16BIT;
			break;
		}

		output_select = OUT_FROM_YCC422_REMAP;
		/*Config remap size for the different color Depth*/
		hdmi_msk_reg(hdmi_dev, VP_REMAP,
			     m_YCC422_SIZE, v_YCC422_SIZE(remap_size));
	} else {
		switch (vpara->color_output_depth) {
		case 10:
			color_depth = COLOR_DEPTH_30BIT;
			output_select = OUT_FROM_PIXEL_PACKING;
			break;
		case 12:
			color_depth = COLOR_DEPTH_36BIT;
			output_select = OUT_FROM_PIXEL_PACKING;
			break;
		case 16:
			color_depth = COLOR_DEPTH_48BIT;
			output_select = OUT_FROM_PIXEL_PACKING;
			break;
		case 8:
		default:
			color_depth = COLOR_DEPTH_24BIT_DEFAULT;
			output_select = OUT_FROM_8BIT_BYPASS;
			break;
		}

		/*Config Color Depth*/
		hdmi_msk_reg(hdmi_dev, VP_PR_CD,
			     m_COLOR_DEPTH, v_COLOR_DEPTH(color_depth));
	}

	/*Config pixel repettion*/
	hdmi_msk_reg(hdmi_dev, VP_PR_CD, m_DESIRED_PR_FACTOR,
		     v_DESIRED_PR_FACTOR(hdmi_dev->pixelrepeat - 1));
	if (hdmi_dev->pixelrepeat > 1)
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_PIXEL_REPET_EN | m_BYPASS_SEL,
			     v_PIXEL_REPET_EN(1) | v_BYPASS_SEL(0));
	else
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_PIXEL_REPET_EN | m_BYPASS_SEL,
			     v_PIXEL_REPET_EN(0) | v_BYPASS_SEL(1));

	/*config output select*/
	if (output_select == OUT_FROM_PIXEL_PACKING) { /* pixel packing */
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_BYPASS_EN | m_PIXEL_PACK_EN |
			     m_YCC422_EN | m_OUTPUT_SEL,
			     v_BYPASS_EN(0) | v_PIXEL_PACK_EN(1) |
			     v_YCC422_EN(0) | v_OUTPUT_SEL(output_select));
	} else if (output_select == OUT_FROM_YCC422_REMAP) { /* YCC422 */
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_BYPASS_EN | m_PIXEL_PACK_EN |
			     m_YCC422_EN | m_OUTPUT_SEL,
			     v_BYPASS_EN(0) | v_PIXEL_PACK_EN(0) |
			     v_YCC422_EN(1) | v_OUTPUT_SEL(output_select));
	} else if (output_select == OUT_FROM_8BIT_BYPASS ||
		   output_select == 3) { /* bypass */
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_BYPASS_EN | m_PIXEL_PACK_EN |
			     m_YCC422_EN | m_OUTPUT_SEL,
			     v_BYPASS_EN(1) | v_PIXEL_PACK_EN(0) |
			     v_YCC422_EN(0) | v_OUTPUT_SEL(output_select));
	}

#if defined(HDMI_VIDEO_STUFFING)
	/* YCC422 and pixel packing stuffing*/
	hdmi_msk_reg(hdmi_dev, VP_STUFF, m_PR_STUFFING, v_PR_STUFFING(1));
	hdmi_msk_reg(hdmi_dev, VP_STUFF,
		     m_YCC422_STUFFING | m_PP_STUFFING,
		     v_YCC422_STUFFING(1) | v_PP_STUFFING(1));
#endif
	return 0;
}

static int rockchip_hdmiv2_video_sampler(struct hdmi_dev *hdmi_dev,
					 struct hdmi_video *vpara)
{
	int map_code = 0;

	if (vpara->color_input == HDMI_COLOR_YCBCR422) {
		/* YCC422 mapping is discontinued - only map 1 is supported */
		switch (vpara->color_output_depth) {
		case 8:
			map_code = VIDEO_YCBCR422_8BIT;
			break;
		case 10:
			map_code = VIDEO_YCBCR422_10BIT;
			break;
		case 12:
			map_code = VIDEO_YCBCR422_12BIT;
			break;
		default:
			map_code = VIDEO_YCBCR422_8BIT;
			break;
		}
	} else if (vpara->color_input == HDMI_COLOR_YCBCR420 ||
		   vpara->color_input == HDMI_COLOR_YCBCR444) {
		switch (vpara->color_output_depth) {
		case 10:
			map_code = VIDEO_YCBCR444_10BIT;
			break;
		case 12:
			map_code = VIDEO_YCBCR444_12BIT;
			break;
		case 16:
			map_code = VIDEO_YCBCR444_16BIT;
			break;
		case 8:
		default:
			map_code = VIDEO_YCBCR444_8BIT;
			break;
		}
	} else {
		switch (vpara->color_output_depth) {
		case 10:
			map_code = VIDEO_RGB444_10BIT;
			break;
		case 12:
			map_code = VIDEO_RGB444_12BIT;
			break;
		case 16:
			map_code = VIDEO_RGB444_16BIT;
			break;
		case 8:
		default:
			map_code = VIDEO_RGB444_8BIT;
			break;
		}
		map_code += (vpara->color_input == HDMI_COLOR_YCBCR444) ?
			    8 : 0;
	}

	/* Set Data enable signal from external
	   and set video sample input mapping */
	hdmi_msk_reg(hdmi_dev, TX_INVID0,
		     m_INTERNAL_DE_GEN | m_VIDEO_MAPPING,
		     v_INTERNAL_DE_GEN(0) | v_VIDEO_MAPPING(map_code));

#if defined(HDMI_VIDEO_STUFFING)
	hdmi_writel(hdmi_dev, TX_GYDATA0, 0x00);
	hdmi_writel(hdmi_dev, TX_GYDATA1, 0x00);
	hdmi_msk_reg(hdmi_dev, TX_INSTUFFING,
		     m_GYDATA_STUFF, v_GYDATA_STUFF(1));
	hdmi_writel(hdmi_dev, TX_RCRDATA0, 0x00);
	hdmi_writel(hdmi_dev, TX_RCRDATA1, 0x00);
	hdmi_msk_reg(hdmi_dev, TX_INSTUFFING,
		     m_RCRDATA_STUFF, v_RCRDATA_STUFF(1));
	hdmi_writel(hdmi_dev, TX_BCBDATA0, 0x00);
	hdmi_writel(hdmi_dev, TX_BCBDATA1, 0x00);
	hdmi_msk_reg(hdmi_dev, TX_INSTUFFING,
		     m_BCBDATA_STUFF, v_BCBDATA_STUFF(1));
#endif
	return 0;
}

static const char coeff_csc[][24] = {
		/*   G		R	    B		Bias
		     A1    |	A2     |    A3     |	A4    |
		     B1    |    B2     |    B3     |    B4    |
		     C1    |    C2     |    C3     |    C4    | */
	{	/* CSC_RGB_0_255_TO_RGB_16_235_8BIT */
		0x36, 0xf7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,		/*G*/
		0x00, 0x00, 0x36, 0xf7, 0x00, 0x00, 0x00, 0x40,		/*R*/
		0x00, 0x00, 0x00, 0x00, 0x36, 0xf7, 0x00, 0x40,		/*B*/
	},
	{	/* CSC_RGB_0_255_TO_RGB_16_235_10BIT */
		0x36, 0xf7, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,		/*G*/
		0x00, 0x00, 0x36, 0xf7, 0x00, 0x00, 0x01, 0x00,		/*R*/
		0x00, 0x00, 0x00, 0x00, 0x36, 0xf7, 0x01, 0x00,		/*B*/
	},
	{	/* CSC_RGB_0_255_TO_ITU601_16_235_8BIT */
		0x20, 0x40, 0x10, 0x80, 0x06, 0x40, 0x00, 0x40,		/*Y*/
		0xe8, 0x80, 0x1c, 0x00, 0xfb, 0x80, 0x02, 0x00,		/*Cr*/
		0xed, 0x80, 0xf6, 0x80, 0x1c, 0x00, 0x02, 0x00,		/*Cb*/
	},
	{	/* CSC_RGB_0_255_TO_ITU601_16_235_10BIT */
		0x20, 0x40, 0x10, 0x80, 0x06, 0x40, 0x01, 0x00,		/*Y*/
		0xe8, 0x80, 0x1c, 0x00, 0xfb, 0x80, 0x08, 0x00,		/*Cr*/
		0xed, 0x80, 0xf6, 0x80, 0x1c, 0x00, 0x08, 0x00,		/*Cb*/
	},
	{	/* CSC_RGB_0_255_TO_ITU709_16_235_8BIT */
		0x27, 0x40, 0x0b, 0xc0, 0x04, 0x00, 0x00, 0x40,		/*Y*/
		0xe6, 0x80, 0x1c, 0x00, 0xfd, 0x80, 0x02, 0x00,		/*Cr*/
		0xea, 0x40, 0xf9, 0x80, 0x1c, 0x00, 0x02, 0x00,		/*Cb*/
	},
	{	/* CSC_RGB_0_255_TO_ITU709_16_235_10BIT */
		0x27, 0x40, 0x0b, 0xc0, 0x04, 0x00, 0x01, 0x00,		/*Y*/
		0xe6, 0x80, 0x1c, 0x00, 0xfd, 0x80, 0x08, 0x00,		/*Cr*/
		0xea, 0x40, 0xf9, 0x80, 0x1c, 0x00, 0x08, 0x00,		/*Cb*/
	},
		/* Y		Cr	    Cb		Bias */
	{	/* CSC_ITU601_16_235_TO_RGB_0_255_8BIT */
		0x20, 0x00, 0x69, 0x26, 0x74, 0xfd, 0x01, 0x0e,		/*G*/
		0x20, 0x00, 0x2c, 0xdd, 0x00, 0x00, 0x7e, 0x9a,		/*R*/
		0x20, 0x00, 0x00, 0x00, 0x38, 0xb4, 0x7e, 0x3b,		/*B*/
	},
	{	/* CSC_ITU709_16_235_TO_RGB_0_255_8BIT */
		0x20, 0x00, 0x71, 0x06, 0x7a, 0x02, 0x00, 0xa7,		/*G*/
		0x20, 0x00, 0x32, 0x64, 0x00, 0x00, 0x7e, 0x6d,		/*R*/
		0x20, 0x00, 0x00, 0x00, 0x3b, 0x61, 0x7e, 0x25,		/*B*/
	},
};

static int rockchip_hdmiv2_video_csc(struct hdmi_dev *hdmi_dev,
				     struct hdmi_video *vpara)
{
	int i, mode, interpolation, decimation, csc_scale;
	const char *coeff = NULL;
	unsigned char color_depth = 0;

	if (vpara->color_input == vpara->color_output) {
		hdmi_msk_reg(hdmi_dev, MC_FLOWCTRL,
			     m_FEED_THROUGH_OFF, v_FEED_THROUGH_OFF(0));
		return 0;
	}

	if (vpara->color_input == HDMI_COLOR_YCBCR422 &&
	    vpara->color_output != HDMI_COLOR_YCBCR422 &&
	    vpara->color_output != HDMI_COLOR_YCBCR420) {
		interpolation = 1;
		hdmi_msk_reg(hdmi_dev, CSC_CFG,
			     m_CSC_INTPMODE, v_CSC_INTPMODE(interpolation));
	}

	if ((vpara->color_input == HDMI_COLOR_RGB_0_255 ||
	     vpara->color_input == HDMI_COLOR_YCBCR444) &&
	     vpara->color_output == HDMI_COLOR_YCBCR422) {
		decimation = 1;
		hdmi_msk_reg(hdmi_dev, CSC_CFG,
			     m_CSC_DECIMODE, v_CSC_DECIMODE(decimation));
	}

	switch (vpara->vic) {
	case HDMI_720X480I_60HZ_4_3:
	case HDMI_720X576I_50HZ_4_3:
	case HDMI_720X480P_60HZ_4_3:
	case HDMI_720X576P_50HZ_4_3:
	case HDMI_720X480I_60HZ_16_9:
	case HDMI_720X576I_50HZ_16_9:
	case HDMI_720X480P_60HZ_16_9:
	case HDMI_720X576P_50HZ_16_9:
		if (vpara->color_input == HDMI_COLOR_RGB_0_255 &&
		    vpara->color_output >= HDMI_COLOR_YCBCR444) {
			mode = CSC_RGB_0_255_TO_ITU601_16_235_8BIT;
			csc_scale = 0;
		} else if (vpara->color_input >= HDMI_COLOR_YCBCR444 &&
			   vpara->color_output == HDMI_COLOR_RGB_0_255) {
			mode = CSC_ITU601_16_235_TO_RGB_0_255_8BIT;
			csc_scale = 1;
		}
		break;
	default:
		if (vpara->color_input == HDMI_COLOR_RGB_0_255 &&
		    vpara->color_output >= HDMI_COLOR_YCBCR444) {
			mode = CSC_RGB_0_255_TO_ITU709_16_235_8BIT;
			csc_scale = 0;
		} else if (vpara->color_input >= HDMI_COLOR_YCBCR444 &&
			   vpara->color_output == HDMI_COLOR_RGB_0_255) {
			mode = CSC_ITU709_16_235_TO_RGB_0_255_8BIT;
			csc_scale = 1;
		}
		break;
	}

	if ((vpara->color_input == HDMI_COLOR_RGB_0_255) &&
	    (vpara->color_output == HDMI_COLOR_RGB_16_235)) {
		mode = CSC_RGB_0_255_TO_RGB_16_235_8BIT;
		csc_scale = 0;
	}

	switch (vpara->color_output_depth) {
	case 10:
		color_depth = COLOR_DEPTH_30BIT;
		mode += 1;
		break;
	case 12:
		color_depth = COLOR_DEPTH_36BIT;
		mode += 2;
		break;
	case 16:
		color_depth = COLOR_DEPTH_48BIT;
		mode += 3;
		break;
	case 8:
	default:
		color_depth = COLOR_DEPTH_24BIT;
		break;
	}

	coeff = coeff_csc[mode];
	for (i = 0; i < 24; i++)
		hdmi_writel(hdmi_dev, CSC_COEF_A1_MSB + i, coeff[i]);

	hdmi_msk_reg(hdmi_dev, CSC_SCALE,
		     m_CSC_SCALE, v_CSC_SCALE(csc_scale));
	/*config CSC_COLOR_DEPTH*/
	hdmi_msk_reg(hdmi_dev, CSC_SCALE,
		     m_CSC_COLOR_DEPTH, v_CSC_COLOR_DEPTH(color_depth));

	/* enable CSC */
	hdmi_msk_reg(hdmi_dev, MC_FLOWCTRL,
		     m_FEED_THROUGH_OFF, v_FEED_THROUGH_OFF(1));

	return 0;
}


static int hdmi_dev_detect_hotplug(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;
	u32 value = hdmi_readl(hdmi_dev, PHY_STAT0);

	HDMIDBG("[%s] reg%x value %02x\n", __func__, PHY_STAT0, value);

	if (value & m_PHY_HPD)
		return HDMI_HPD_ACTIVED;
	else
		return HDMI_HPD_REMOVED;
}

static int hdmi_dev_read_edid(struct hdmi *hdmi, int block, unsigned char *buff)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;
	int i = 0, n = 0, index = 0, ret = -1, trytime = 5;
	int offset = (block % 2) * 0x80;
	int interrupt = 0;

	HDMIDBG("[%s] block %d\n", __func__, block);

	rockchip_hdmiv2_i2cm_reset(hdmi_dev);

	/* Set DDC I2C CLK which devided from DDC_CLK to 100KHz. */
	rockchip_hdmiv2_i2cm_clk_init(hdmi_dev);

	/* Enable I2C interrupt for reading edid */
	rockchip_hdmiv2_i2cm_mask_int(hdmi_dev, 0);

	hdmi_writel(hdmi_dev, I2CM_SLAVE, DDC_I2C_EDID_ADDR);
	hdmi_writel(hdmi_dev, I2CM_SEGADDR, DDC_I2C_SEG_ADDR);
	hdmi_writel(hdmi_dev, I2CM_SEGPTR, block / 2);
	for (n = 0; n < HDMI_EDID_BLOCK_SIZE / 8; n++) {
		for (trytime = 0; trytime < 5; trytime++) {
			hdmi_writel(hdmi_dev, I2CM_ADDRESS, offset + 8 * n);
			/* enable extend sequential read operation */
			if (block == 0)
				hdmi_msk_reg(hdmi_dev, I2CM_OPERATION,
					     m_I2CM_RD8, v_I2CM_RD8(1));
			else
				hdmi_msk_reg(hdmi_dev, I2CM_OPERATION,
					     m_I2CM_RD8_EXT,
					     v_I2CM_RD8_EXT(1));

			i = 20;
			while (i--) {
				usleep_range(900, 1000);
				interrupt = hdmi_readl(hdmi_dev,
						       IH_I2CM_STAT0);
				if (interrupt)
					hdmi_writel(hdmi_dev,
						    IH_I2CM_STAT0, interrupt);

				if (interrupt &
				    (m_SCDC_READREQ | m_I2CM_DONE |
				     m_I2CM_ERROR))
					break;
			}

			if (interrupt & m_I2CM_DONE) {
				for (index = 0; index < 8; index++)
					buff[8 * n + index] =
						hdmi_readl(hdmi_dev,
							   I2CM_READ_BUFF0 +
							   index);

				if (n == HDMI_EDID_BLOCK_SIZE / 8 - 1) {
					ret = 0;
					goto exit;
				}
				break;
			} else if ((interrupt & m_I2CM_ERROR) || (i == -1)) {
				dev_err(hdmi->dev,
					"[%s] edid read %d error\n",
					__func__, offset + 8 * n);
			}
		}
		if (trytime == 5) {
			dev_err(hdmi->dev,
				"[%s] edid read error\n", __func__);
			break;
		}
	}

exit:
	/* Disable I2C interrupt */
	rockchip_hdmiv2_i2cm_mask_int(hdmi_dev, 1);

	#ifdef DEBUG
	if (!ret) {
		for (index = 0; index < 128; index++) {
			printk("0x%02x ,", buff[index]);
			if ((index + 1) % 16 == 0)
				printk("\n");
		}
	}
	#endif
	return ret;
}

static void hdmi_dev_config_avi(struct hdmi_dev *hdmi_dev,
				struct hdmi_video *vpara)
{
	unsigned char colorimetry, ext_colorimetry, aspect_ratio, y1y0;
	unsigned char rgb_quan_range = AVI_QUANTIZATION_RANGE_DEFAULT;

	/* Set AVI infoFrame Data byte1 */
	if (vpara->color_output == HDMI_COLOR_YCBCR444)
		y1y0 = AVI_COLOR_MODE_YCBCR444;
	else if (vpara->color_output == HDMI_COLOR_YCBCR422)
		y1y0 = AVI_COLOR_MODE_YCBCR422;
	else if (vpara->color_output == HDMI_COLOR_YCBCR420)
		y1y0 = AVI_COLOR_MODE_YCBCR420;
	else
		y1y0 = AVI_COLOR_MODE_RGB;

	hdmi_msk_reg(hdmi_dev, FC_AVICONF0,
		     m_FC_ACTIV_FORMAT | m_FC_RGC_YCC,
		     v_FC_RGC_YCC(y1y0) | v_FC_ACTIV_FORMAT(1));

	/* Set AVI infoFrame Data byte2 */
	switch (vpara->vic) {
	case HDMI_720X480I_60HZ_4_3:
	case HDMI_720X576I_50HZ_4_3:
	case HDMI_720X480P_60HZ_4_3:
	case HDMI_720X576P_50HZ_4_3:
		aspect_ratio = AVI_CODED_FRAME_ASPECT_4_3;
		colorimetry = AVI_COLORIMETRY_SMPTE_170M;
		break;
	case HDMI_720X480I_60HZ_16_9:
	case HDMI_720X576I_50HZ_16_9:
	case HDMI_720X480P_60HZ_16_9:
	case HDMI_720X576P_50HZ_16_9:
		aspect_ratio = AVI_CODED_FRAME_ASPECT_16_9;
		colorimetry = AVI_COLORIMETRY_SMPTE_170M;
		break;
	default:
		aspect_ratio = AVI_CODED_FRAME_ASPECT_16_9;
		colorimetry = AVI_COLORIMETRY_ITU709;
	}

	if (vpara->color_output_depth > 8) {
		colorimetry = AVI_COLORIMETRY_EXTENDED;
		ext_colorimetry = 6;
	} else if (vpara->color_output == HDMI_COLOR_RGB_16_235 ||
		 vpara->color_output == HDMI_COLOR_RGB_0_255) {
		colorimetry = AVI_COLORIMETRY_NO_DATA;
		ext_colorimetry = 0;
	}

	hdmi_writel(hdmi_dev, FC_AVICONF1,
		    v_FC_COLORIMETRY(colorimetry) |
		    v_FC_PIC_ASPEC_RATIO(aspect_ratio) |
		    v_FC_ACT_ASPEC_RATIO(ACTIVE_ASPECT_RATE_DEFAULT));

	/* Set AVI infoFrame Data byte3 */
	hdmi_msk_reg(hdmi_dev, FC_AVICONF2,
		     m_FC_EXT_COLORIMETRY | m_FC_QUAN_RANGE,
		     v_FC_EXT_COLORIMETRY(ext_colorimetry) |
		     v_FC_QUAN_RANGE(rgb_quan_range));

	/* Set AVI infoFrame Data byte4 */
	if ((vpara->vic > 92 && vpara->vic < 96) || (vpara->vic == 98))
		hdmi_writel(hdmi_dev, FC_AVIVID, 0);
	else
		hdmi_writel(hdmi_dev, FC_AVIVID, vpara->vic & 0xff);
	/* Set AVI infoFrame Data byte5 */
	hdmi_msk_reg(hdmi_dev, FC_AVICONF3, m_FC_YQ | m_FC_CN,
		     v_FC_YQ(YQ_LIMITED_RANGE) | v_FC_CN(CN_GRAPHICS));
}

static int hdmi_dev_config_vsi(struct hdmi *hdmi,
			       unsigned char vic_3d, unsigned char format)
{
	int i = 0, id = 0x000c03;
	unsigned char data[3] = {0};

	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG("[%s] vic %d format %d.\n", __func__, vic_3d, format);

	hdmi_msk_reg(hdmi_dev, FC_DATAUTO0, m_VSD_AUTO, v_VSD_AUTO(0));
	hdmi_writel(hdmi_dev, FC_VSDIEEEID2, id & 0xff);
	hdmi_writel(hdmi_dev, FC_VSDIEEEID1, (id >> 8) & 0xff);
	hdmi_writel(hdmi_dev, FC_VSDIEEEID0, (id >> 16) & 0xff);

	data[0] = format << 5;	/* PB4 --HDMI_Video_Format */
	switch (format) {
	case HDMI_VIDEO_FORMAT_4KX2K:
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
	hdmi_writel(hdmi_dev, FC_VSDSIZE, 0x6);
/*	if (auto_send) { */
	hdmi_writel(hdmi_dev, FC_DATAUTO1, 0);
	hdmi_writel(hdmi_dev, FC_DATAUTO2, 0x11);
	hdmi_msk_reg(hdmi_dev, FC_DATAUTO0, m_VSD_AUTO, v_VSD_AUTO(1));
/*	}
	else {
		hdmi_msk_reg(hdmi_dev, FC_DATMAN, m_VSD_MAN, v_VSD_MAN(1));
	}
*/
	return 0;
}

static int hdmi_dev_config_video(struct hdmi *hdmi, struct hdmi_video *vpara)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG("%s vic %d 3dformat %d color mode %d color depth %d\n",
		__func__, vpara->vic, vpara->format_3d,
		vpara->color_output, vpara->color_output_depth);

	if (hdmi_dev->soctype == HDMI_SOC_RK3288)
		vpara->color_input = HDMI_COLOR_RGB_0_255;

	if (!hdmi->uboot) {
		/* befor configure video, we power off phy */
		hdmi_msk_reg(hdmi_dev, PHY_CONF0,
			     m_PDDQ_SIG | m_TXPWRON_SIG,
			     v_PDDQ_SIG(1) | v_TXPWRON_SIG(0));

		/* force output blue */
		if (vpara->color_output == HDMI_COLOR_RGB_0_255) {
			hdmi_writel(hdmi_dev, FC_DBGTMDS2, 0x00);	/*R*/
			hdmi_writel(hdmi_dev, FC_DBGTMDS1, 0x00);	/*G*/
			hdmi_writel(hdmi_dev, FC_DBGTMDS0, 0x00);	/*B*/
		} else if (vpara->color_output == HDMI_COLOR_RGB_16_235) {
			hdmi_writel(hdmi_dev, FC_DBGTMDS2, 0x10);	/*R*/
			hdmi_writel(hdmi_dev, FC_DBGTMDS1, 0x10);	/*G*/
			hdmi_writel(hdmi_dev, FC_DBGTMDS0, 0x10);	/*B*/
		} else {
			hdmi_writel(hdmi_dev, FC_DBGTMDS2, 0x80);	/*R*/
			hdmi_writel(hdmi_dev, FC_DBGTMDS1, 0x10);	/*G*/
			hdmi_writel(hdmi_dev, FC_DBGTMDS0, 0x80);	/*B*/
		}
		hdmi_msk_reg(hdmi_dev, FC_DBGFORCE,
			     m_FC_FORCEVIDEO, v_FC_FORCEVIDEO(1));
	}

	hdmi_writel(hdmi_dev, MC_CLKDIS, m_HDCPCLK_DISABLE);
	if (rockchip_hdmiv2_video_framecomposer(hdmi, vpara) < 0)
		return -1;

	if (rockchip_hdmiv2_video_packetizer(hdmi_dev, vpara) < 0)
		return -1;
	/* Color space convert */
	if (rockchip_hdmiv2_video_csc(hdmi_dev, vpara) < 0)
		return -1;
	if (rockchip_hdmiv2_video_sampler(hdmi_dev, vpara) < 0)
		return -1;

	if (vpara->sink_hdmi == OUTPUT_HDMI) {
		hdmi_dev_config_avi(hdmi_dev, vpara);
		if (vpara->format_3d != HDMI_3D_NONE) {
			hdmi_dev_config_vsi(hdmi,
					    vpara->format_3d,
					    HDMI_VIDEO_FORMAT_3D);
		} else if ((vpara->vic > 92 && vpara->vic < 96) ||
			 (vpara->vic == 98)) {
			vpara->vic = (vpara->vic == 98) ?
				     4 : (96 - vpara->vic);
			hdmi_dev_config_vsi(hdmi,
					    vpara->vic,
					    HDMI_VIDEO_FORMAT_4KX2K);
		} else {
			hdmi_dev_config_vsi(hdmi,
					    vpara->vic,
					    HDMI_VIDEO_FORMAT_NORMAL);
		}
		dev_info(hdmi->dev, "[%s] sucess output HDMI.\n", __func__);
	} else {
		dev_info(hdmi->dev, "[%s] sucess output DVI.\n", __func__);
	}

	rockchip_hdmiv2_config_phy(hdmi_dev);
	return 0;
}

static void hdmi_dev_config_aai(struct hdmi_dev *hdmi_dev,
				struct hdmi_audio *audio)
{
	/*Refer to CEA861-E Audio infoFrame*/
	/*Set both Audio Channel Count and Audio Coding
	  Type Refer to Stream Head for HDMI*/
	hdmi_msk_reg(hdmi_dev, FC_AUDICONF0,
		     m_FC_CHN_CNT | m_FC_CODING_TYEP,
		     v_FC_CHN_CNT(audio->channel-1) | v_FC_CODING_TYEP(0));

	/*Set both Audio Sample Size and Sample Frequency
	  Refer to Stream Head for HDMI*/
	hdmi_msk_reg(hdmi_dev, FC_AUDICONF1,
		     m_FC_SAMPLE_SIZE | m_FC_SAMPLE_FREQ,
		     v_FC_SAMPLE_SIZE(0) | v_FC_SAMPLE_FREQ(0));

	/*Set Channel Allocation*/
	hdmi_writel(hdmi_dev, FC_AUDICONF2, 0x00);

	/*Set LFEPBL¡¢DOWN-MIX INH and LSV*/
	hdmi_writel(hdmi_dev, FC_AUDICONF3, 0x00);
}

static int hdmi_dev_config_audio(struct hdmi *hdmi, struct hdmi_audio *audio)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;
	int word_length = 0, channel = 0, mclk_fs;
	unsigned int N = 0, CTS = 0;
	int rate = 0;

	HDMIDBG("%s\n", __func__);

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
		mclk_fs = FS_64;
		rate = AUDIO_32K;
		if (hdmi_dev->tmdsclk >= 594000000)
			N = N_32K_HIGHCLK;
		else if (hdmi_dev->tmdsclk >= 297000000)
			N = N_32K_MIDCLK;
		else
			N = N_32K_LOWCLK;
		/*div a num to avoid the value is exceed 2^32(int)*/
		CTS = CALC_CTS(N, hdmi_dev->tmdsclk/1000, 32);
		break;
	case HDMI_AUDIO_FS_44100:
		mclk_fs = FS_64;
		rate = AUDIO_441K;
		if (hdmi_dev->tmdsclk >= 594000000)
			N = N_441K_HIGHCLK;
		else if (hdmi_dev->tmdsclk >= 297000000)
			N = N_441K_MIDCLK;
		else
			N = N_441K_LOWCLK;

		CTS = CALC_CTS(N, hdmi_dev->tmdsclk/100, 441);
		break;
	case HDMI_AUDIO_FS_48000:
		mclk_fs = FS_64;
		rate = AUDIO_48K;
		if (hdmi_dev->tmdsclk >= 594000000)	/*FS_153.6*/
			N = N_48K_HIGHCLK;
		else if (hdmi_dev->tmdsclk >= 297000000)
			N = N_48K_MIDCLK;
		else
			N = N_48K_LOWCLK;

		CTS = CALC_CTS(N, hdmi_dev->tmdsclk/1000, 48);
		break;
	case HDMI_AUDIO_FS_88200:
		mclk_fs = FS_64;
		rate = AUDIO_882K;
		if (hdmi_dev->tmdsclk >= 594000000)
			N = N_882K_HIGHCLK;
		else if (hdmi_dev->tmdsclk >= 297000000)
			N = N_882K_MIDCLK;
		else
			N = N_882K_LOWCLK;

		CTS = CALC_CTS(N, hdmi_dev->tmdsclk/100, 882);
		break;
	case HDMI_AUDIO_FS_96000:
		mclk_fs = FS_64;
		rate = AUDIO_96K;
		if (hdmi_dev->tmdsclk >= 594000000)	/*FS_153.6*/
			N = N_96K_HIGHCLK;
		else if (hdmi_dev->tmdsclk >= 297000000)
			N = N_96K_MIDCLK;
		else
			N = N_96K_LOWCLK;

		CTS = CALC_CTS(N, hdmi_dev->tmdsclk/1000, 96);
		break;
	case HDMI_AUDIO_FS_176400:
		mclk_fs = FS_64;
		rate = AUDIO_1764K;
		if (hdmi_dev->tmdsclk >= 594000000)
			N = N_1764K_HIGHCLK;
		else if (hdmi_dev->tmdsclk >= 297000000)
			N = N_1764K_MIDCLK;
		else
			N = N_1764K_LOWCLK;

		CTS = CALC_CTS(N, hdmi_dev->tmdsclk/100, 1764);
		break;
	case HDMI_AUDIO_FS_192000:
		mclk_fs = FS_64;
		rate = AUDIO_192K;
		if (hdmi_dev->tmdsclk >= 594000000)	/*FS_153.6*/
			N = N_192K_HIGHCLK;
		else if (hdmi_dev->tmdsclk >= 297000000)
			N = N_192K_MIDCLK;
		else
			N = N_192K_LOWCLK;

		CTS = CALC_CTS(N, hdmi_dev->tmdsclk/1000, 192);
		break;
	default:
		dev_err(hdmi_dev->hdmi->dev,
			"[%s] not support such sample rate %d\n",
			__func__, audio->rate);
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

	HDMIDBG("rate = %d, tmdsclk = %u, N = %d, CTS = %d\n",
		audio->rate, hdmi_dev->tmdsclk, N, CTS);
	/* more than 2 channels => layout 1 else layout 0 */
	hdmi_msk_reg(hdmi_dev, FC_AUDSCONF,
		     m_AUD_PACK_LAYOUT,
		     v_AUD_PACK_LAYOUT((audio->channel > 2) ? 1 : 0));

	if (hdmi_dev->audiosrc == HDMI_AUDIO_SRC_SPDIF) {
		mclk_fs = FS_128;
		hdmi_msk_reg(hdmi_dev, AUD_CONF0,
			     m_I2S_SEL, v_I2S_SEL(AUDIO_SPDIF_GPA));
		hdmi_msk_reg(hdmi_dev, AUD_SPDIF1,
			     m_SET_NLPCM | m_SPDIF_WIDTH,
			     v_SET_NLPCM(PCM_LINEAR) |
			     v_SPDIF_WIDTH(word_length));
		/*Mask fifo empty and full int and reset fifo*/
		hdmi_msk_reg(hdmi_dev, AUD_SPDIFINT,
			     m_FIFO_EMPTY_MASK | m_FIFO_FULL_MASK,
			     v_FIFO_EMPTY_MASK(1) | v_FIFO_FULL_MASK(1));
		hdmi_msk_reg(hdmi_dev, AUD_SPDIF0,
			     m_SW_SAUD_FIFO_RST, v_SW_SAUD_FIFO_RST(1));
	} else {
		/*Mask fifo empty and full int and reset fifo*/
		hdmi_msk_reg(hdmi_dev, AUD_INT,
			     m_FIFO_EMPTY_MASK | m_FIFO_FULL_MASK,
			     v_FIFO_EMPTY_MASK(1) | v_FIFO_FULL_MASK(1));
		hdmi_msk_reg(hdmi_dev, AUD_CONF0,
			     m_SW_AUD_FIFO_RST, v_SW_AUD_FIFO_RST(1));
		hdmi_writel(hdmi_dev, MC_SWRSTZREQ, 0xF7);
		usleep_range(90, 100);
		hdmi_msk_reg(hdmi_dev, AUD_CONF0,
			     m_I2S_SEL | m_I2S_IN_EN,
			     v_I2S_SEL(AUDIO_I2S) | v_I2S_IN_EN(channel));
		hdmi_writel(hdmi_dev, AUD_CONF1,
			    v_I2S_MODE(I2S_STANDARD_MODE) |
			    v_I2S_WIDTH(word_length));
	}

	hdmi_msk_reg(hdmi_dev, AUD_INPUTCLKFS,
		     m_LFS_FACTOR, v_LFS_FACTOR(mclk_fs));

	/*Set N value*/
	hdmi_msk_reg(hdmi_dev, AUD_N3, m_NCTS_ATOMIC_WR, v_NCTS_ATOMIC_WR(1));
	/*Set CTS by manual*/
	hdmi_msk_reg(hdmi_dev, AUD_CTS3,
		     m_N_SHIFT | m_CTS_MANUAL | m_AUD_CTS3,
		     v_N_SHIFT(N_SHIFT_1) |
		     v_CTS_MANUAL(1) |
		     v_AUD_CTS3(CTS >> 16));
	hdmi_writel(hdmi_dev, AUD_CTS2, (CTS >> 8) & 0xff);
	hdmi_writel(hdmi_dev, AUD_CTS1, CTS & 0xff);

	hdmi_msk_reg(hdmi_dev, AUD_N3, m_AUD_N3, v_AUD_N3(N >> 16));
	hdmi_writel(hdmi_dev, AUD_N2, (N >> 8) & 0xff);
	hdmi_writel(hdmi_dev, AUD_N1, N & 0xff);

	/* set channel status register */
	hdmi_msk_reg(hdmi_dev, FC_AUDSCHNLS7,
		     m_AUDIO_SAMPLE_RATE, v_AUDIO_SAMPLE_RATE(rate));
	/* hdmi_writel(hdmi_dev, FC_AUDSCHNLS2, 0x1); */
	/* hdmi_writel(hdmi_dev, FC_AUDSCHNLS8, ((~rate) << 4) | 0x2); */

	hdmi_dev_config_aai(hdmi_dev, audio);

	return 0;
}

static int hdmi_dev_control_output(struct hdmi *hdmi, int enable)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG("[%s] %d\n", __func__, enable);

	if (enable == HDMI_AV_UNMUTE) {
		hdmi_writel(hdmi_dev, FC_DBGFORCE, 0x00);
		hdmi_msk_reg(hdmi_dev, FC_GCP,
			     m_FC_SET_AVMUTE | m_FC_CLR_AVMUTE,
			     v_FC_SET_AVMUTE(0) | v_FC_CLR_AVMUTE(1));
	} else {
		if (enable & HDMI_VIDEO_MUTE) {
			hdmi_msk_reg(hdmi_dev, FC_DBGFORCE,
				     m_FC_FORCEVIDEO, v_FC_FORCEVIDEO(1));
			hdmi_msk_reg(hdmi_dev, FC_GCP,
				     m_FC_SET_AVMUTE | m_FC_CLR_AVMUTE,
				     v_FC_SET_AVMUTE(1) | v_FC_CLR_AVMUTE(0));
		}
/*		if (enable & HDMI_AUDIO_MUTE) {
			hdmi_msk_reg(hdmi_dev, FC_AUDSCONF,
				     m_AUD_PACK_SAMPFIT,
				     v_AUD_PACK_SAMPFIT(0x0F));
		}
*/		if (enable == (HDMI_VIDEO_MUTE | HDMI_AUDIO_MUTE)) {
			msleep(100);
			rockchip_hdmiv2_powerdown(hdmi_dev);
			hdmi_dev->tmdsclk = 0;
/*
			hdmi_msk_reg(hdmi_dev, PHY_CONF0,
				     m_PDDQ_SIG | m_TXPWRON_SIG,
				     v_PDDQ_SIG(1) | v_TXPWRON_SIG(0));
			hdmi_writel(hdmi_dev, MC_CLKDIS, 0x7f);
*/		}
	}
	return 0;
}

static int hdmi_dev_insert(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG("%s\n", __func__);
	hdmi_writel(hdmi_dev, MC_CLKDIS, m_HDCPCLK_DISABLE);
	return HDMI_ERROR_SUCESS;
}

static int hdmi_dev_remove(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG("%s\n", __func__);
	rockchip_hdmiv2_powerdown(hdmi_dev);
	hdmi_dev->tmdsclk = 0;
	return HDMI_ERROR_SUCESS;
}

static int hdmi_dev_enable(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG("%s\n", __func__);
	if (!hdmi_dev->enable) {
		hdmi_writel(hdmi_dev, IH_MUTE, 0x00);
		hdmi_dev->enable = 1;
	}
	hdmi_submit_work(hdmi, HDMI_HPD_CHANGE, 10, NULL);
	return 0;
}

static int hdmi_dev_disable(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG("%s\n", __func__);
	if (hdmi_dev->enable) {
		hdmi_dev->enable = 0;
		hdmi_writel(hdmi_dev, IH_MUTE, 0x1);
	}
	return 0;
}

void rockchip_hdmiv2_dev_init_ops(struct hdmi_ops *ops)
{
	if (ops) {
		ops->enable	= hdmi_dev_enable;
		ops->disable	= hdmi_dev_disable;
		ops->getstatus	= hdmi_dev_detect_hotplug;
		ops->insert	= hdmi_dev_insert;
		ops->remove	= hdmi_dev_remove;
		ops->getedid	= hdmi_dev_read_edid;
		ops->setvideo	= hdmi_dev_config_video;
		ops->setaudio	= hdmi_dev_config_audio;
		ops->setmute	= hdmi_dev_control_output;
		ops->setvsi	= hdmi_dev_config_vsi;
	}
}

void rockchip_hdmiv2_dev_initial(struct hdmi_dev *hdmi_dev)
{
	struct hdmi *hdmi = hdmi_dev->hdmi;

	if (!hdmi->uboot) {
		/* reset hdmi */
		if (hdmi_dev->soctype == HDMI_SOC_RK3288) {
			writel_relaxed((1 << 9) | (1 << 25),
				       RK_CRU_VIRT + 0x01d4);
			udelay(1);
			writel_relaxed((0 << 9) | (1 << 25),
				       RK_CRU_VIRT + 0x01d4);
		} else if (hdmi_dev->soctype == HDMI_SOC_RK3368) {
			pr_info("reset hdmi\n");
			regmap_write(hdmi_dev->grf_base, 0x031c,
				     (1 << 9) | (1 << 25));
			udelay(5);
			regmap_write(hdmi_dev->grf_base, 0x031c,
				     (0 << 9) | (1 << 25));
		}
		rockchip_hdmiv2_powerdown(hdmi_dev);
	}
	/*mute unnecessary interrrupt, only enable hpd*/
	hdmi_writel(hdmi_dev, IH_MUTE_FC_STAT0, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_FC_STAT1, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_FC_STAT2, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_AS_STAT0, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_PHY_STAT0, 0xfe);
	hdmi_writel(hdmi_dev, IH_MUTE_I2CM_STAT0, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_CEC_STAT0, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_VP_STAT0, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_I2CMPHY_STAT0, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_AHBDMAAUD_STAT0, 0xff);

	/* disable hdcp interrup */
	hdmi_writel(hdmi_dev, A_APIINTMSK, 0xff);
	hdmi_writel(hdmi_dev, PHY_MASK, 0xf1);

	if (hdmi->property->feature & SUPPORT_CEC)
		rockchip_hdmiv2_cec_init(hdmi);
	if (hdmi->property->feature & SUPPORT_HDCP)
		rockchip_hdmiv2_hdcp_init(hdmi);
}

irqreturn_t rockchip_hdmiv2_dev_irq(int irq, void *priv)
{
	struct hdmi_dev *hdmi_dev = priv;
	struct hdmi *hdmi = hdmi_dev->hdmi;
	char phy_pol = hdmi_readl(hdmi_dev, PHY_POL0);
	char phy_status = hdmi_readl(hdmi_dev, PHY_STAT0);
	char phy_int0 = hdmi_readl(hdmi_dev, PHY_INI0);
	/*read interrupt*/
	char fc_stat0 = hdmi_readl(hdmi_dev, IH_FC_STAT0);
	char fc_stat1 = hdmi_readl(hdmi_dev, IH_FC_STAT1);
	char fc_stat2 = hdmi_readl(hdmi_dev, IH_FC_STAT2);
	char aud_int = hdmi_readl(hdmi_dev, IH_AS_SATA0);
	char phy_int = hdmi_readl(hdmi_dev, IH_PHY_STAT0);
	char vp_stat0 = hdmi_readl(hdmi_dev, IH_VP_STAT0);
	char cec_int = hdmi_readl(hdmi_dev, IH_CEC_STAT0);
	char hdcp_int = hdmi_readl(hdmi_dev, A_APIINTSTAT);
	char hdcp2_int = hdmi_readl(hdmi_dev, HDCP2REG_STAT);

	/*clear interrupt*/
	hdmi_writel(hdmi_dev, IH_FC_STAT0, fc_stat0);
	hdmi_writel(hdmi_dev, IH_FC_STAT1, fc_stat1);
	hdmi_writel(hdmi_dev, IH_FC_STAT2, fc_stat2);
	hdmi_writel(hdmi_dev, IH_VP_STAT0, vp_stat0);

	if (phy_int0 || phy_int) {
		phy_pol = (phy_int0 & (~phy_status)) | ((~phy_int0) & phy_pol);
		hdmi_writel(hdmi_dev, PHY_POL0, phy_pol);
		hdmi_writel(hdmi_dev, IH_PHY_STAT0, phy_int);
		if ((phy_int & m_HPD) || ((phy_int & 0x3c) == 0x3c))
			hdmi_submit_work(hdmi, HDMI_HPD_CHANGE, 20, NULL);
	}

	/* Audio error */
	if (aud_int) {
		hdmi_writel(hdmi_dev, IH_AS_SATA0, aud_int);
		hdmi_msk_reg(hdmi_dev, AUD_CONF0,
			     m_SW_AUD_FIFO_RST, v_SW_AUD_FIFO_RST(1));
		hdmi_writel(hdmi_dev, MC_SWRSTZREQ, 0xF7);
	}
	/* CEC */
	if (cec_int) {
		hdmi_writel(hdmi_dev, IH_CEC_STAT0, cec_int);
		rockchip_hdmiv2_cec_isr(hdmi_dev, cec_int);
	}
	/* HDCP */
	if (hdcp_int) {
		hdmi_writel(hdmi_dev, A_APIINTCLR, hdcp_int);
		pr_info("hdcp_int is 0x%02x\n", hdcp_int);
	}

	/* HDCP2 */
	if (hdcp2_int) {
		hdmi_writel(hdmi_dev, HDCP2REG_STAT, hdcp2_int);
		pr_info("hdcp2_int is 0x%02x\n", hdcp2_int);
	}
	return IRQ_HANDLED;
}
