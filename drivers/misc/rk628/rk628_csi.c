// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chisp.com>
 */

#include "rk628.h"
#include "rk628_combtxphy.h"
#include "rk628_config.h"
#include "rk628_csi.h"

#define CSITX_ERR_RETRY_TIMES		3

#define MIPI_DATARATE_MBPS_LOW		750
#define MIPI_DATARATE_MBPS_HIGH		1250

#define USE_4_LANES			4
#define YUV422_8BIT			0x1e
/* Test Code: 0x44 (HS RX Control of Lane 0) */
#define HSFREQRANGE(x)			UPDATE(x, 6, 1)

struct rk628_csi {
	struct rk628_display_mode mode;
	bool txphy_pwron;
	u64 lane_mbps;
};

static inline void testif_testclk_assert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTCLK, PHY_TESTCLK);
	udelay(1);
}

static inline void testif_testclk_deassert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTCLK, 0);
	udelay(1);
}

static inline void testif_testclr_assert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTCLR, PHY_TESTCLR);
	udelay(1);
}

static inline void testif_testclr_deassert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTCLR, 0);
	udelay(1);
}

static inline void testif_testen_assert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTEN, PHY_TESTEN);
	udelay(1);
}

static inline void testif_testen_deassert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTEN, 0);
	udelay(1);
}

static inline void testif_set_data(struct rk628 *rk628, u8 data)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTDIN_MASK, PHY_TESTDIN(data));
	udelay(1);
}

static inline u8 testif_get_data(struct rk628 *rk628)
{
	u32 data = 0;

	rk628_i2c_read(rk628, GRF_DPHY0_STATUS, &data);

	return data >> PHY_TESTDOUT_SHIFT;
}

static void testif_test_code_write(struct rk628 *rk628, u8 test_code)
{
	testif_testclk_assert(rk628);
	testif_set_data(rk628, test_code);
	testif_testen_assert(rk628);
	testif_testclk_deassert(rk628);
	testif_testen_deassert(rk628);
}

static void testif_test_data_write(struct rk628 *rk628, u8 test_data)
{
	testif_testclk_deassert(rk628);
	testif_set_data(rk628, test_data);
	testif_testclk_assert(rk628);
}

static u8 testif_write(struct rk628 *rk628, u8 test_code, u8 test_data)
{
	u8 monitor_data;

	testif_test_code_write(rk628, test_code);
	testif_test_data_write(rk628, test_data);
	monitor_data = testif_get_data(rk628);

	dev_info(rk628->dev, "test_code=0x%02x, ", test_code);
	dev_info(rk628->dev, "test_data=0x%02x, ", test_data);
	dev_info(rk628->dev, "monitor_data=0x%02x\n", monitor_data);

	return monitor_data;
}
static void rk628_csi_get_detected_timings(struct rk628 *rk628)
{
	struct rk628_display_mode *src, *dst;
	struct rk628_csi *csi = rk628->csi;

	if (!csi)
		return;

	src = rk628_display_get_src_mode(rk628);
	dst = rk628_display_get_dst_mode(rk628);

	rk628_set_output_bus_format(rk628, BUS_FMT_YUV422);
	rk628_mode_copy(dst, src);
	rk628_mode_copy(&csi->mode, dst);
}


static inline u8 testif_read(struct rk628 *rk628, u8 test_code)
{
	u8 test_data;

	testif_test_code_write(rk628, test_code);
	test_data = testif_get_data(rk628);
	testif_test_data_write(rk628, test_data);

	return test_data;
}

static inline void mipi_dphy_enableclk_assert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, CSITX_DPHY_CTRL, DPHY_ENABLECLK,
			DPHY_ENABLECLK);
	udelay(1);
}

static inline void mipi_dphy_enableclk_deassert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628,  CSITX_DPHY_CTRL, DPHY_ENABLECLK, 0);
	udelay(1);
}

static inline void mipi_dphy_shutdownz_assert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628,  GRF_MIPI_TX0_CON, CSI_PHYSHUTDOWNZ, 0);
	udelay(1);
}

static inline void mipi_dphy_shutdownz_deassert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628,  GRF_MIPI_TX0_CON, CSI_PHYSHUTDOWNZ,
			CSI_PHYSHUTDOWNZ);
	udelay(1);
}

static inline void mipi_dphy_rstz_assert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628,  GRF_MIPI_TX0_CON, CSI_PHYRSTZ, 0);
	udelay(1);
}

static inline void mipi_dphy_rstz_deassert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628,  GRF_MIPI_TX0_CON, CSI_PHYRSTZ,
			CSI_PHYRSTZ);
	udelay(1);
}

static void mipi_dphy_init_hsfreqrange(struct rk628 *rk628)
{
	const struct {
		unsigned long max_lane_mbps;
		u8 hsfreqrange;
	} hsfreqrange_table[] = {
		{  90, 0x00}, { 100, 0x10}, { 110, 0x20}, { 130, 0x01},
		{ 140, 0x11}, { 150, 0x21}, { 170, 0x02}, { 180, 0x12},
		{ 200, 0x22}, { 220, 0x03}, { 240, 0x13}, { 250, 0x23},
		{ 270, 0x04}, { 300, 0x14}, { 330, 0x05}, { 360, 0x15},
		{ 400, 0x25}, { 450, 0x06}, { 500, 0x16}, { 550, 0x07},
		{ 600, 0x17}, { 650, 0x08}, { 700, 0x18}, { 750, 0x09},
		{ 800, 0x19}, { 850, 0x29}, { 900, 0x39}, { 950, 0x0a},
		{1000, 0x1a}, {1050, 0x2a}, {1100, 0x3a}, {1150, 0x0b},
		{1200, 0x1b}, {1250, 0x2b}, {1300, 0x3b}, {1350, 0x0c},
		{1400, 0x1c}, {1450, 0x2c}, {1500, 0x3c}
	};
	u8 hsfreqrange;
	unsigned int index;
	struct rk628_csi *csi = rk628->csi;

	if (!csi)
		return;

	for (index = 0; index < ARRAY_SIZE(hsfreqrange_table); index++)
		if (csi->lane_mbps <= hsfreqrange_table[index].max_lane_mbps)
			break;

	if (index == ARRAY_SIZE(hsfreqrange_table))
		--index;

	hsfreqrange = hsfreqrange_table[index].hsfreqrange;
	testif_write(rk628, 0x44, HSFREQRANGE(hsfreqrange));
}

static int mipi_dphy_reset(struct rk628 *rk628)
{
	u32 val;
	int ret;

	mipi_dphy_enableclk_deassert(rk628);
	mipi_dphy_shutdownz_assert(rk628);
	mipi_dphy_rstz_assert(rk628);
	testif_testclr_assert(rk628);

	/* Set all REQUEST inputs to zero */
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
		     FORCETXSTOPMODE_MASK | FORCERXMODE_MASK,
		     FORCETXSTOPMODE(0) | FORCERXMODE(0));
	udelay(1);
	testif_testclr_deassert(rk628);
	mipi_dphy_enableclk_assert(rk628);
	mipi_dphy_shutdownz_deassert(rk628);
	mipi_dphy_rstz_deassert(rk628);
	usleep_range(1500, 2000);

	ret = rk628_i2c_read(rk628, CSITX_CSITX_STATUS1, &val);
	if (ret < 0) {
		dev_info(rk628->dev, "lane module is not in stop state\n");
		return ret;
	}

	return 0;
}

static int mipi_dphy_power_on(struct rk628 *rk628)
{
	unsigned int val;
	u32 bus_width, mask;
	struct rk628_csi *csi = rk628->csi;

	if (!csi)
		return -1;

	if ((csi->mode.hdisplay == 3840) &&
	    (csi->mode.vdisplay == 2160)) {
		csi->lane_mbps = MIPI_DATARATE_MBPS_HIGH;
	} else {
		csi->lane_mbps = MIPI_DATARATE_MBPS_LOW;
	}

	bus_width =  csi->lane_mbps << 8;
	bus_width |= COMBTXPHY_MODULEA_EN;
	dev_info(rk628->dev, "%s mipi bitrate:%llu mbps\n", __func__, csi->lane_mbps);
	rk628_combtxphy_set_bus_width(rk628, bus_width);
	rk628_combtxphy_set_mode(rk628, PHY_MODE_VIDEO_MIPI);

	mipi_dphy_init_hsfreqrange(rk628);
	usleep_range(1500, 2000);
	rk628_combtxphy_power_on(rk628);

	usleep_range(1500, 2000);
	mask = DPHY_PLL_LOCK;
	rk628_i2c_read(rk628, CSITX_CSITX_STATUS1, &val);
	if ((val & mask) != mask) {
		dev_info(rk628->dev, "PHY is not locked\n");
		return -1;
	}

	udelay(10);

	return 0;
}

static void mipi_dphy_power_off(struct rk628 *rk628)
{
	rk628_combtxphy_power_off(rk628);
}

static void rk62_csi_reset(struct rk628 *rk628)
{
	rk628_i2c_write(rk628, CSITX_SYS_CTRL0_IMD, 0x1);
	usleep_range(1000, 1100);
	rk628_i2c_write(rk628, CSITX_SYS_CTRL0_IMD, 0x0);
}

static void rk628_csi_set_csi(struct rk628 *rk628)
{
	u8 lanes = USE_4_LANES;
	u8 lane_num;
	u8 dphy_lane_en;
	u32 wc_usrdef;
	struct rk628_csi *csi;

	if (!rk628->csi) {
		csi = devm_kzalloc(rk628->dev, sizeof(*csi), GFP_KERNEL);
		if (!csi)
			return;
		rk628->csi = csi;
	} else {
		csi = rk628->csi;
	}
	lane_num = lanes - 1;
	dphy_lane_en = (1 << (lanes + 1)) - 1;
	wc_usrdef = csi->mode.hdisplay * 2;

	rk62_csi_reset(rk628);

	if (csi->txphy_pwron) {
		dev_info(rk628->dev, "%s: txphy already power on, power off\n",
			__func__);
		mipi_dphy_power_off(rk628);
		csi->txphy_pwron = false;
	}

	mipi_dphy_power_on(rk628);
	csi->txphy_pwron = true;
	dev_info(rk628->dev, "%s: txphy power on!\n", __func__);
	usleep_range(1000, 1500);

	rk628_i2c_update_bits(rk628, CSITX_CSITX_EN,
			VOP_UV_SWAP_MASK |
			VOP_YUV422_EN_MASK |
			VOP_P2_EN_MASK |
			LANE_NUM_MASK |
			DPHY_EN_MASK |
			CSITX_EN_MASK,
			VOP_UV_SWAP(1) |
			VOP_YUV422_EN(1) |
			VOP_P2_EN(1) |
			LANE_NUM(lane_num) |
			DPHY_EN(0) |
			CSITX_EN(0));
	rk628_i2c_update_bits(rk628, CSITX_SYS_CTRL1,
			BYPASS_SELECT_MASK,
			BYPASS_SELECT(1));
	rk628_i2c_write(rk628, CSITX_CONFIG_DONE, CONFIG_DONE_IMD);
	rk628_i2c_write(rk628, CSITX_SYS_CTRL2, VOP_WHOLE_FRM_EN | VSYNC_ENABLE);
	rk628_i2c_update_bits(rk628, CSITX_SYS_CTRL3_IMD,
			CONT_MODE_CLK_CLR_MASK |
			CONT_MODE_CLK_SET_MASK |
			NON_CONTINOUS_MODE_MASK,
			CONT_MODE_CLK_CLR(0) |
			CONT_MODE_CLK_SET(0) |
			NON_CONTINOUS_MODE(1));

	rk628_i2c_write(rk628, CSITX_VOP_PATH_CTRL,
			VOP_WC_USERDEFINE(wc_usrdef) |
			VOP_DT_USERDEFINE(YUV422_8BIT) |
			VOP_PIXEL_FORMAT(0) |
			VOP_WC_USERDEFINE_EN(1) |
			VOP_DT_USERDEFINE_EN(1) |
			VOP_PATH_EN(1));
	rk628_i2c_update_bits(rk628, CSITX_DPHY_CTRL,
				CSI_DPHY_EN_MASK,
				CSI_DPHY_EN(dphy_lane_en));
	rk628_i2c_write(rk628, CSITX_CONFIG_DONE, CONFIG_DONE_IMD);
	dev_info(rk628->dev, "%s csi cofig done\n", __func__);
}

static void enable_csitx(struct rk628 *rk628)
{
	u32 i, ret, val;

	for (i = 0; i < CSITX_ERR_RETRY_TIMES; i++) {
		rk628_csi_set_csi(rk628);
		rk628_i2c_update_bits(rk628, CSITX_CSITX_EN,
					DPHY_EN_MASK |
					CSITX_EN_MASK,
					DPHY_EN(1) |
					CSITX_EN(1));
		rk628_i2c_write(rk628, CSITX_CONFIG_DONE, CONFIG_DONE_IMD);
		msleep(40);
		rk628_i2c_write(rk628, CSITX_ERR_INTR_CLR_IMD, 0xffffffff);
		rk628_i2c_update_bits(rk628, CSITX_SYS_CTRL1,
				BYPASS_SELECT_MASK, BYPASS_SELECT(0));
		rk628_i2c_write(rk628, CSITX_CONFIG_DONE, CONFIG_DONE_IMD);
		msleep(40);
		ret = rk628_i2c_read(rk628, CSITX_ERR_INTR_RAW_STATUS_IMD, &val);
		if (!ret && !val)
			break;

		dev_info(rk628->dev, "%s csitx err, retry:%d, err status:%#x, ret:%d\n",
			 __func__, i, val, ret);
	}

}

static void enable_stream(struct rk628 *rk628, bool en)
{
	dev_info(rk628->dev, "%s: %sable\n", __func__, en ? "en" : "dis");
	if (en) {
		enable_csitx(rk628);
	} else {
		rk628_i2c_update_bits(rk628, CSITX_CSITX_EN,
					DPHY_EN_MASK |
					CSITX_EN_MASK,
					DPHY_EN(0) |
					CSITX_EN(0));
		rk628_i2c_write(rk628, CSITX_CONFIG_DONE, CONFIG_DONE_IMD);
	}
}

void rk628_csi_init(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_SYSTEM_CON0,
			      SW_OUTPUT_MODE_MASK, SW_OUTPUT_MODE(OUTPUT_MODE_CSI));
	rk628_csi_get_detected_timings(rk628);
	mipi_dphy_reset(rk628);
}

void rk628_csi_enable(struct rk628 *rk628)
{
	rk628_csi_get_detected_timings(rk628);
	return enable_stream(rk628, true);
}

void rk628_csi_disable(struct rk628 *rk628)
{
	return enable_stream(rk628, false);
}
