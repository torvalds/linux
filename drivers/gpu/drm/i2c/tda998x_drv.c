/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */



#include <linux/module.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder_slave.h>
#include <drm/drm_edid.h>


#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)

struct tda998x_priv {
	struct i2c_client *cec;
	uint16_t rev;
	uint8_t current_page;
	int dpms;
};

#define to_tda998x_priv(x)  ((struct tda998x_priv *)to_encoder_slave(x)->slave_priv)

/* The TDA9988 series of devices use a paged register scheme.. to simplify
 * things we encode the page # in upper bits of the register #.  To read/
 * write a given register, we need to make sure CURPAGE register is set
 * appropriately.  Which implies reads/writes are not atomic.  Fun!
 */

#define REG(page, addr) (((page) << 8) | (addr))
#define REG2ADDR(reg)   ((reg) & 0xff)
#define REG2PAGE(reg)   (((reg) >> 8) & 0xff)

#define REG_CURPAGE               0xff                /* write */


/* Page 00h: General Control */
#define REG_VERSION_LSB           REG(0x00, 0x00)     /* read */
#define REG_MAIN_CNTRL0           REG(0x00, 0x01)     /* read/write */
# define MAIN_CNTRL0_SR           (1 << 0)
# define MAIN_CNTRL0_DECS         (1 << 1)
# define MAIN_CNTRL0_DEHS         (1 << 2)
# define MAIN_CNTRL0_CECS         (1 << 3)
# define MAIN_CNTRL0_CEHS         (1 << 4)
# define MAIN_CNTRL0_SCALER       (1 << 7)
#define REG_VERSION_MSB           REG(0x00, 0x02)     /* read */
#define REG_SOFTRESET             REG(0x00, 0x0a)     /* write */
# define SOFTRESET_AUDIO          (1 << 0)
# define SOFTRESET_I2C_MASTER     (1 << 1)
#define REG_DDC_DISABLE           REG(0x00, 0x0b)     /* read/write */
#define REG_CCLK_ON               REG(0x00, 0x0c)     /* read/write */
#define REG_I2C_MASTER            REG(0x00, 0x0d)     /* read/write */
# define I2C_MASTER_DIS_MM        (1 << 0)
# define I2C_MASTER_DIS_FILT      (1 << 1)
# define I2C_MASTER_APP_STRT_LAT  (1 << 2)
#define REG_INT_FLAGS_0           REG(0x00, 0x0f)     /* read/write */
#define REG_INT_FLAGS_1           REG(0x00, 0x10)     /* read/write */
#define REG_INT_FLAGS_2           REG(0x00, 0x11)     /* read/write */
# define INT_FLAGS_2_EDID_BLK_RD  (1 << 1)
#define REG_ENA_VP_0              REG(0x00, 0x18)     /* read/write */
#define REG_ENA_VP_1              REG(0x00, 0x19)     /* read/write */
#define REG_ENA_VP_2              REG(0x00, 0x1a)     /* read/write */
#define REG_ENA_AP                REG(0x00, 0x1e)     /* read/write */
#define REG_VIP_CNTRL_0           REG(0x00, 0x20)     /* write */
# define VIP_CNTRL_0_MIRR_A       (1 << 7)
# define VIP_CNTRL_0_SWAP_A(x)    (((x) & 7) << 4)
# define VIP_CNTRL_0_MIRR_B       (1 << 3)
# define VIP_CNTRL_0_SWAP_B(x)    (((x) & 7) << 0)
#define REG_VIP_CNTRL_1           REG(0x00, 0x21)     /* write */
# define VIP_CNTRL_1_MIRR_C       (1 << 7)
# define VIP_CNTRL_1_SWAP_C(x)    (((x) & 7) << 4)
# define VIP_CNTRL_1_MIRR_D       (1 << 3)
# define VIP_CNTRL_1_SWAP_D(x)    (((x) & 7) << 0)
#define REG_VIP_CNTRL_2           REG(0x00, 0x22)     /* write */
# define VIP_CNTRL_2_MIRR_E       (1 << 7)
# define VIP_CNTRL_2_SWAP_E(x)    (((x) & 7) << 4)
# define VIP_CNTRL_2_MIRR_F       (1 << 3)
# define VIP_CNTRL_2_SWAP_F(x)    (((x) & 7) << 0)
#define REG_VIP_CNTRL_3           REG(0x00, 0x23)     /* write */
# define VIP_CNTRL_3_X_TGL        (1 << 0)
# define VIP_CNTRL_3_H_TGL        (1 << 1)
# define VIP_CNTRL_3_V_TGL        (1 << 2)
# define VIP_CNTRL_3_EMB          (1 << 3)
# define VIP_CNTRL_3_SYNC_DE      (1 << 4)
# define VIP_CNTRL_3_SYNC_HS      (1 << 5)
# define VIP_CNTRL_3_DE_INT       (1 << 6)
# define VIP_CNTRL_3_EDGE         (1 << 7)
#define REG_VIP_CNTRL_4           REG(0x00, 0x24)     /* write */
# define VIP_CNTRL_4_BLC(x)       (((x) & 3) << 0)
# define VIP_CNTRL_4_BLANKIT(x)   (((x) & 3) << 2)
# define VIP_CNTRL_4_CCIR656      (1 << 4)
# define VIP_CNTRL_4_656_ALT      (1 << 5)
# define VIP_CNTRL_4_TST_656      (1 << 6)
# define VIP_CNTRL_4_TST_PAT      (1 << 7)
#define REG_VIP_CNTRL_5           REG(0x00, 0x25)     /* write */
# define VIP_CNTRL_5_CKCASE       (1 << 0)
# define VIP_CNTRL_5_SP_CNT(x)    (((x) & 3) << 1)
#define REG_MAT_CONTRL            REG(0x00, 0x80)     /* write */
# define MAT_CONTRL_MAT_SC(x)     (((x) & 3) << 0)
# define MAT_CONTRL_MAT_BP        (1 << 2)
#define REG_VIDFORMAT             REG(0x00, 0xa0)     /* write */
#define REG_REFPIX_MSB            REG(0x00, 0xa1)     /* write */
#define REG_REFPIX_LSB            REG(0x00, 0xa2)     /* write */
#define REG_REFLINE_MSB           REG(0x00, 0xa3)     /* write */
#define REG_REFLINE_LSB           REG(0x00, 0xa4)     /* write */
#define REG_NPIX_MSB              REG(0x00, 0xa5)     /* write */
#define REG_NPIX_LSB              REG(0x00, 0xa6)     /* write */
#define REG_NLINE_MSB             REG(0x00, 0xa7)     /* write */
#define REG_NLINE_LSB             REG(0x00, 0xa8)     /* write */
#define REG_VS_LINE_STRT_1_MSB    REG(0x00, 0xa9)     /* write */
#define REG_VS_LINE_STRT_1_LSB    REG(0x00, 0xaa)     /* write */
#define REG_VS_PIX_STRT_1_MSB     REG(0x00, 0xab)     /* write */
#define REG_VS_PIX_STRT_1_LSB     REG(0x00, 0xac)     /* write */
#define REG_VS_LINE_END_1_MSB     REG(0x00, 0xad)     /* write */
#define REG_VS_LINE_END_1_LSB     REG(0x00, 0xae)     /* write */
#define REG_VS_PIX_END_1_MSB      REG(0x00, 0xaf)     /* write */
#define REG_VS_PIX_END_1_LSB      REG(0x00, 0xb0)     /* write */
#define REG_VS_PIX_STRT_2_MSB     REG(0x00, 0xb3)     /* write */
#define REG_VS_PIX_STRT_2_LSB     REG(0x00, 0xb4)     /* write */
#define REG_VS_PIX_END_2_MSB      REG(0x00, 0xb7)     /* write */
#define REG_VS_PIX_END_2_LSB      REG(0x00, 0xb8)     /* write */
#define REG_HS_PIX_START_MSB      REG(0x00, 0xb9)     /* write */
#define REG_HS_PIX_START_LSB      REG(0x00, 0xba)     /* write */
#define REG_HS_PIX_STOP_MSB       REG(0x00, 0xbb)     /* write */
#define REG_HS_PIX_STOP_LSB       REG(0x00, 0xbc)     /* write */
#define REG_VWIN_START_1_MSB      REG(0x00, 0xbd)     /* write */
#define REG_VWIN_START_1_LSB      REG(0x00, 0xbe)     /* write */
#define REG_VWIN_END_1_MSB        REG(0x00, 0xbf)     /* write */
#define REG_VWIN_END_1_LSB        REG(0x00, 0xc0)     /* write */
#define REG_DE_START_MSB          REG(0x00, 0xc5)     /* write */
#define REG_DE_START_LSB          REG(0x00, 0xc6)     /* write */
#define REG_DE_STOP_MSB           REG(0x00, 0xc7)     /* write */
#define REG_DE_STOP_LSB           REG(0x00, 0xc8)     /* write */
#define REG_TBG_CNTRL_0           REG(0x00, 0xca)     /* write */
# define TBG_CNTRL_0_FRAME_DIS    (1 << 5)
# define TBG_CNTRL_0_SYNC_MTHD    (1 << 6)
# define TBG_CNTRL_0_SYNC_ONCE    (1 << 7)
#define REG_TBG_CNTRL_1           REG(0x00, 0xcb)     /* write */
# define TBG_CNTRL_1_VH_TGL_0     (1 << 0)
# define TBG_CNTRL_1_VH_TGL_1     (1 << 1)
# define TBG_CNTRL_1_VH_TGL_2     (1 << 2)
# define TBG_CNTRL_1_VHX_EXT_DE   (1 << 3)
# define TBG_CNTRL_1_VHX_EXT_HS   (1 << 4)
# define TBG_CNTRL_1_VHX_EXT_VS   (1 << 5)
# define TBG_CNTRL_1_DWIN_DIS     (1 << 6)
#define REG_ENABLE_SPACE          REG(0x00, 0xd6)     /* write */
#define REG_HVF_CNTRL_0           REG(0x00, 0xe4)     /* write */
# define HVF_CNTRL_0_SM           (1 << 7)
# define HVF_CNTRL_0_RWB          (1 << 6)
# define HVF_CNTRL_0_PREFIL(x)    (((x) & 3) << 2)
# define HVF_CNTRL_0_INTPOL(x)    (((x) & 3) << 0)
#define REG_HVF_CNTRL_1           REG(0x00, 0xe5)     /* write */
# define HVF_CNTRL_1_FOR          (1 << 0)
# define HVF_CNTRL_1_YUVBLK       (1 << 1)
# define HVF_CNTRL_1_VQR(x)       (((x) & 3) << 2)
# define HVF_CNTRL_1_PAD(x)       (((x) & 3) << 4)
# define HVF_CNTRL_1_SEMI_PLANAR  (1 << 6)
#define REG_RPT_CNTRL             REG(0x00, 0xf0)     /* write */


/* Page 02h: PLL settings */
#define REG_PLL_SERIAL_1          REG(0x02, 0x00)     /* read/write */
# define PLL_SERIAL_1_SRL_FDN     (1 << 0)
# define PLL_SERIAL_1_SRL_IZ(x)   (((x) & 3) << 1)
# define PLL_SERIAL_1_SRL_MAN_IZ  (1 << 6)
#define REG_PLL_SERIAL_2          REG(0x02, 0x01)     /* read/write */
# define PLL_SERIAL_2_SRL_NOSC(x) (((x) & 3) << 0)
# define PLL_SERIAL_2_SRL_PR(x)   (((x) & 0xf) << 4)
#define REG_PLL_SERIAL_3          REG(0x02, 0x02)     /* read/write */
# define PLL_SERIAL_3_SRL_CCIR    (1 << 0)
# define PLL_SERIAL_3_SRL_DE      (1 << 2)
# define PLL_SERIAL_3_SRL_PXIN_SEL (1 << 4)
#define REG_SERIALIZER            REG(0x02, 0x03)     /* read/write */
#define REG_BUFFER_OUT            REG(0x02, 0x04)     /* read/write */
#define REG_PLL_SCG1              REG(0x02, 0x05)     /* read/write */
#define REG_PLL_SCG2              REG(0x02, 0x06)     /* read/write */
#define REG_PLL_SCGN1             REG(0x02, 0x07)     /* read/write */
#define REG_PLL_SCGN2             REG(0x02, 0x08)     /* read/write */
#define REG_PLL_SCGR1             REG(0x02, 0x09)     /* read/write */
#define REG_PLL_SCGR2             REG(0x02, 0x0a)     /* read/write */
#define REG_AUDIO_DIV             REG(0x02, 0x0e)     /* read/write */
#define REG_SEL_CLK               REG(0x02, 0x11)     /* read/write */
# define SEL_CLK_SEL_CLK1         (1 << 0)
# define SEL_CLK_SEL_VRF_CLK(x)   (((x) & 3) << 1)
# define SEL_CLK_ENA_SC_CLK       (1 << 3)
#define REG_ANA_GENERAL           REG(0x02, 0x12)     /* read/write */


/* Page 09h: EDID Control */
#define REG_EDID_DATA_0           REG(0x09, 0x00)     /* read */
/* next 127 successive registers are the EDID block */
#define REG_EDID_CTRL             REG(0x09, 0xfa)     /* read/write */
#define REG_DDC_ADDR              REG(0x09, 0xfb)     /* read/write */
#define REG_DDC_OFFS              REG(0x09, 0xfc)     /* read/write */
#define REG_DDC_SEGM_ADDR         REG(0x09, 0xfd)     /* read/write */
#define REG_DDC_SEGM              REG(0x09, 0xfe)     /* read/write */


/* Page 10h: information frames and packets */


/* Page 11h: audio settings and content info packets */
#define REG_AIP_CNTRL_0           REG(0x11, 0x00)     /* read/write */
# define AIP_CNTRL_0_RST_FIFO     (1 << 0)
# define AIP_CNTRL_0_SWAP         (1 << 1)
# define AIP_CNTRL_0_LAYOUT       (1 << 2)
# define AIP_CNTRL_0_ACR_MAN      (1 << 5)
# define AIP_CNTRL_0_RST_CTS      (1 << 6)
#define REG_ENC_CNTRL             REG(0x11, 0x0d)     /* read/write */
# define ENC_CNTRL_RST_ENC        (1 << 0)
# define ENC_CNTRL_RST_SEL        (1 << 1)
# define ENC_CNTRL_CTL_CODE(x)    (((x) & 3) << 2)


/* Page 12h: HDCP and OTP */
#define REG_TX3                   REG(0x12, 0x9a)     /* read/write */
#define REG_TX33                  REG(0x12, 0xb8)     /* read/write */
# define TX33_HDMI                (1 << 1)


/* Page 13h: Gamut related metadata packets */



/* CEC registers: (not paged)
 */
#define REG_CEC_FRO_IM_CLK_CTRL   0xfb                /* read/write */
# define CEC_FRO_IM_CLK_CTRL_GHOST_DIS (1 << 7)
# define CEC_FRO_IM_CLK_CTRL_ENA_OTP   (1 << 6)
# define CEC_FRO_IM_CLK_CTRL_IMCLK_SEL (1 << 1)
# define CEC_FRO_IM_CLK_CTRL_FRO_DIV   (1 << 0)
#define REG_CEC_RXSHPDLEV         0xfe                /* read */
# define CEC_RXSHPDLEV_RXSENS     (1 << 0)
# define CEC_RXSHPDLEV_HPD        (1 << 1)

#define REG_CEC_ENAMODS           0xff                /* read/write */
# define CEC_ENAMODS_DIS_FRO      (1 << 6)
# define CEC_ENAMODS_DIS_CCLK     (1 << 5)
# define CEC_ENAMODS_EN_RXSENS    (1 << 2)
# define CEC_ENAMODS_EN_HDMI      (1 << 1)
# define CEC_ENAMODS_EN_CEC       (1 << 0)


/* Device versions: */
#define TDA9989N2                 0x0101
#define TDA19989                  0x0201
#define TDA19989N2                0x0202
#define TDA19988                  0x0301

static void
cec_write(struct drm_encoder *encoder, uint16_t addr, uint8_t val)
{
	struct i2c_client *client = to_tda998x_priv(encoder)->cec;
	uint8_t buf[] = {addr, val};
	int ret;

	ret = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (ret < 0)
		dev_err(&client->dev, "Error %d writing to cec:0x%x\n", ret, addr);
}

static uint8_t
cec_read(struct drm_encoder *encoder, uint8_t addr)
{
	struct i2c_client *client = to_tda998x_priv(encoder)->cec;
	uint8_t val;
	int ret;

	ret = i2c_master_send(client, &addr, sizeof(addr));
	if (ret < 0)
		goto fail;

	ret = i2c_master_recv(client, &val, sizeof(val));
	if (ret < 0)
		goto fail;

	return val;

fail:
	dev_err(&client->dev, "Error %d reading from cec:0x%x\n", ret, addr);
	return 0;
}

static void
set_page(struct drm_encoder *encoder, uint16_t reg)
{
	struct tda998x_priv *priv = to_tda998x_priv(encoder);

	if (REG2PAGE(reg) != priv->current_page) {
		struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
		uint8_t buf[] = {
				REG_CURPAGE, REG2PAGE(reg)
		};
		int ret = i2c_master_send(client, buf, sizeof(buf));
		if (ret < 0)
			dev_err(&client->dev, "Error %d writing to REG_CURPAGE\n", ret);

		priv->current_page = REG2PAGE(reg);
	}
}

static int
reg_read_range(struct drm_encoder *encoder, uint16_t reg, char *buf, int cnt)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	uint8_t addr = REG2ADDR(reg);
	int ret;

	set_page(encoder, reg);

	ret = i2c_master_send(client, &addr, sizeof(addr));
	if (ret < 0)
		goto fail;

	ret = i2c_master_recv(client, buf, cnt);
	if (ret < 0)
		goto fail;

	return ret;

fail:
	dev_err(&client->dev, "Error %d reading from 0x%x\n", ret, reg);
	return ret;
}

static uint8_t
reg_read(struct drm_encoder *encoder, uint16_t reg)
{
	uint8_t val = 0;
	reg_read_range(encoder, reg, &val, sizeof(val));
	return val;
}

static void
reg_write(struct drm_encoder *encoder, uint16_t reg, uint8_t val)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	uint8_t buf[] = {REG2ADDR(reg), val};
	int ret;

	set_page(encoder, reg);

	ret = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (ret < 0)
		dev_err(&client->dev, "Error %d writing to 0x%x\n", ret, reg);
}

static void
reg_write16(struct drm_encoder *encoder, uint16_t reg, uint16_t val)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	uint8_t buf[] = {REG2ADDR(reg), val >> 8, val};
	int ret;

	set_page(encoder, reg);

	ret = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (ret < 0)
		dev_err(&client->dev, "Error %d writing to 0x%x\n", ret, reg);
}

static void
reg_set(struct drm_encoder *encoder, uint16_t reg, uint8_t val)
{
	reg_write(encoder, reg, reg_read(encoder, reg) | val);
}

static void
reg_clear(struct drm_encoder *encoder, uint16_t reg, uint8_t val)
{
	reg_write(encoder, reg, reg_read(encoder, reg) & ~val);
}

static void
tda998x_reset(struct drm_encoder *encoder)
{
	/* reset audio and i2c master: */
	reg_set(encoder, REG_SOFTRESET, SOFTRESET_AUDIO | SOFTRESET_I2C_MASTER);
	msleep(50);
	reg_clear(encoder, REG_SOFTRESET, SOFTRESET_AUDIO | SOFTRESET_I2C_MASTER);
	msleep(50);

	/* reset transmitter: */
	reg_set(encoder, REG_MAIN_CNTRL0, MAIN_CNTRL0_SR);
	reg_clear(encoder, REG_MAIN_CNTRL0, MAIN_CNTRL0_SR);

	/* PLL registers common configuration */
	reg_write(encoder, REG_PLL_SERIAL_1, 0x00);
	reg_write(encoder, REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(1));
	reg_write(encoder, REG_PLL_SERIAL_3, 0x00);
	reg_write(encoder, REG_SERIALIZER,   0x00);
	reg_write(encoder, REG_BUFFER_OUT,   0x00);
	reg_write(encoder, REG_PLL_SCG1,     0x00);
	reg_write(encoder, REG_AUDIO_DIV,    0x03);
	reg_write(encoder, REG_SEL_CLK,      SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);
	reg_write(encoder, REG_PLL_SCGN1,    0xfa);
	reg_write(encoder, REG_PLL_SCGN2,    0x00);
	reg_write(encoder, REG_PLL_SCGR1,    0x5b);
	reg_write(encoder, REG_PLL_SCGR2,    0x00);
	reg_write(encoder, REG_PLL_SCG2,     0x10);
}

/* DRM encoder functions */

static void
tda998x_encoder_set_config(struct drm_encoder *encoder, void *params)
{
}

static void
tda998x_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct tda998x_priv *priv = to_tda998x_priv(encoder);

	/* we only care about on or off: */
	if (mode != DRM_MODE_DPMS_ON)
		mode = DRM_MODE_DPMS_OFF;

	if (mode == priv->dpms)
		return;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		/* enable audio and video ports */
		reg_write(encoder, REG_ENA_AP, 0xff);
		reg_write(encoder, REG_ENA_VP_0, 0xff);
		reg_write(encoder, REG_ENA_VP_1, 0xff);
		reg_write(encoder, REG_ENA_VP_2, 0xff);
		/* set muxing after enabling ports: */
		reg_write(encoder, REG_VIP_CNTRL_0,
				VIP_CNTRL_0_SWAP_A(2) | VIP_CNTRL_0_SWAP_B(3));
		reg_write(encoder, REG_VIP_CNTRL_1,
				VIP_CNTRL_1_SWAP_C(0) | VIP_CNTRL_1_SWAP_D(1));
		reg_write(encoder, REG_VIP_CNTRL_2,
				VIP_CNTRL_2_SWAP_E(4) | VIP_CNTRL_2_SWAP_F(5));
		break;
	case DRM_MODE_DPMS_OFF:
		/* disable audio and video ports */
		reg_write(encoder, REG_ENA_AP, 0x00);
		reg_write(encoder, REG_ENA_VP_0, 0x00);
		reg_write(encoder, REG_ENA_VP_1, 0x00);
		reg_write(encoder, REG_ENA_VP_2, 0x00);
		break;
	}

	priv->dpms = mode;
}

static void
tda998x_encoder_save(struct drm_encoder *encoder)
{
	DBG("");
}

static void
tda998x_encoder_restore(struct drm_encoder *encoder)
{
	DBG("");
}

static bool
tda998x_encoder_mode_fixup(struct drm_encoder *encoder,
			  const struct drm_display_mode *mode,
			  struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int
tda998x_encoder_mode_valid(struct drm_encoder *encoder,
			  struct drm_display_mode *mode)
{
	return MODE_OK;
}

static void
tda998x_encoder_mode_set(struct drm_encoder *encoder,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct tda998x_priv *priv = to_tda998x_priv(encoder);
	uint16_t hs_start, hs_end, line_start, line_end;
	uint16_t vwin_start, vwin_end, de_start, de_end;
	uint16_t ref_pix, ref_line, pix_start2;
	uint8_t reg, div, rep;

	hs_start   = mode->hsync_start - mode->hdisplay;
	hs_end     = mode->hsync_end - mode->hdisplay;
	line_start = 1;
	line_end   = 1 + mode->vsync_end - mode->vsync_start;
	vwin_start = mode->vtotal - mode->vsync_start;
	vwin_end   = vwin_start + mode->vdisplay;
	de_start   = mode->htotal - mode->hdisplay;
	de_end     = mode->htotal;

	pix_start2 = 0;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		pix_start2 = (mode->htotal / 2) + hs_start;

	/* TODO how is this value calculated?  It is 2 for all common
	 * formats in the tables in out of tree nxp driver (assuming
	 * I've properly deciphered their byzantine table system)
	 */
	ref_line = 2;

	/* this might changes for other color formats from the CRTC: */
	ref_pix = 3 + hs_start;

	div = 148500 / mode->clock;

	DBG("clock=%d, div=%u", mode->clock, div);
	DBG("hs_start=%u, hs_end=%u, line_start=%u, line_end=%u",
			hs_start, hs_end, line_start, line_end);
	DBG("vwin_start=%u, vwin_end=%u, de_start=%u, de_end=%u",
			vwin_start, vwin_end, de_start, de_end);
	DBG("ref_line=%u, ref_pix=%u, pix_start2=%u",
			ref_line, ref_pix, pix_start2);

	/* mute the audio FIFO: */
	reg_set(encoder, REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_FIFO);

	/* set HDMI HDCP mode off: */
	reg_set(encoder, REG_TBG_CNTRL_1, TBG_CNTRL_1_DWIN_DIS);
	reg_clear(encoder, REG_TX33, TX33_HDMI);

	reg_write(encoder, REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(0));
	/* no pre-filter or interpolator: */
	reg_write(encoder, REG_HVF_CNTRL_0, HVF_CNTRL_0_PREFIL(0) |
			HVF_CNTRL_0_INTPOL(0));
	reg_write(encoder, REG_VIP_CNTRL_5, VIP_CNTRL_5_SP_CNT(0));
	reg_write(encoder, REG_VIP_CNTRL_4, VIP_CNTRL_4_BLANKIT(0) |
			VIP_CNTRL_4_BLC(0));
	reg_clear(encoder, REG_PLL_SERIAL_3, PLL_SERIAL_3_SRL_CCIR);

	reg_clear(encoder, REG_PLL_SERIAL_1, PLL_SERIAL_1_SRL_MAN_IZ);
	reg_clear(encoder, REG_PLL_SERIAL_3, PLL_SERIAL_3_SRL_DE);
	reg_write(encoder, REG_SERIALIZER, 0);
	reg_write(encoder, REG_HVF_CNTRL_1, HVF_CNTRL_1_VQR(0));

	/* TODO enable pixel repeat for pixel rates less than 25Msamp/s */
	rep = 0;
	reg_write(encoder, REG_RPT_CNTRL, 0);
	reg_write(encoder, REG_SEL_CLK, SEL_CLK_SEL_VRF_CLK(0) |
			SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);

	reg_write(encoder, REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(div) |
			PLL_SERIAL_2_SRL_PR(rep));

	reg_write16(encoder, REG_VS_PIX_STRT_2_MSB, pix_start2);
	reg_write16(encoder, REG_VS_PIX_END_2_MSB, pix_start2);

	/* set color matrix bypass flag: */
	reg_set(encoder, REG_MAT_CONTRL, MAT_CONTRL_MAT_BP);

	/* set BIAS tmds value: */
	reg_write(encoder, REG_ANA_GENERAL, 0x09);

	reg_clear(encoder, REG_TBG_CNTRL_0, TBG_CNTRL_0_SYNC_MTHD);

	reg_write(encoder, REG_VIP_CNTRL_3, 0);
	reg_set(encoder, REG_VIP_CNTRL_3, VIP_CNTRL_3_SYNC_HS);
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		reg_set(encoder, REG_VIP_CNTRL_3, VIP_CNTRL_3_V_TGL);

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		reg_set(encoder, REG_VIP_CNTRL_3, VIP_CNTRL_3_H_TGL);

	reg_write(encoder, REG_VIDFORMAT, 0x00);
	reg_write16(encoder, REG_NPIX_MSB, mode->hdisplay - 1);
	reg_write16(encoder, REG_NLINE_MSB, mode->vdisplay - 1);
	reg_write16(encoder, REG_VS_LINE_STRT_1_MSB, line_start);
	reg_write16(encoder, REG_VS_LINE_END_1_MSB, line_end);
	reg_write16(encoder, REG_VS_PIX_STRT_1_MSB, hs_start);
	reg_write16(encoder, REG_VS_PIX_END_1_MSB, hs_start);
	reg_write16(encoder, REG_HS_PIX_START_MSB, hs_start);
	reg_write16(encoder, REG_HS_PIX_STOP_MSB, hs_end);
	reg_write16(encoder, REG_VWIN_START_1_MSB, vwin_start);
	reg_write16(encoder, REG_VWIN_END_1_MSB, vwin_end);
	reg_write16(encoder, REG_DE_START_MSB, de_start);
	reg_write16(encoder, REG_DE_STOP_MSB, de_end);

	if (priv->rev == TDA19988) {
		/* let incoming pixels fill the active space (if any) */
		reg_write(encoder, REG_ENABLE_SPACE, 0x01);
	}

	reg_write16(encoder, REG_REFPIX_MSB, ref_pix);
	reg_write16(encoder, REG_REFLINE_MSB, ref_line);

	reg = TBG_CNTRL_1_VHX_EXT_DE |
			TBG_CNTRL_1_VHX_EXT_HS |
			TBG_CNTRL_1_VHX_EXT_VS |
			TBG_CNTRL_1_DWIN_DIS | /* HDCP off */
			TBG_CNTRL_1_VH_TGL_2;
	if (mode->flags & (DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC))
		reg |= TBG_CNTRL_1_VH_TGL_0;
	reg_set(encoder, REG_TBG_CNTRL_1, reg);

	/* must be last register set: */
	reg_clear(encoder, REG_TBG_CNTRL_0, TBG_CNTRL_0_SYNC_ONCE);
}

static enum drm_connector_status
tda998x_encoder_detect(struct drm_encoder *encoder,
		      struct drm_connector *connector)
{
	uint8_t val = cec_read(encoder, REG_CEC_RXSHPDLEV);
	return (val & CEC_RXSHPDLEV_HPD) ? connector_status_connected :
			connector_status_disconnected;
}

static int
read_edid_block(struct drm_encoder *encoder, uint8_t *buf, int blk)
{
	uint8_t offset, segptr;
	int ret, i;

	/* enable EDID read irq: */
	reg_set(encoder, REG_INT_FLAGS_2, INT_FLAGS_2_EDID_BLK_RD);

	offset = (blk & 1) ? 128 : 0;
	segptr = blk / 2;

	reg_write(encoder, REG_DDC_ADDR, 0xa0);
	reg_write(encoder, REG_DDC_OFFS, offset);
	reg_write(encoder, REG_DDC_SEGM_ADDR, 0x60);
	reg_write(encoder, REG_DDC_SEGM, segptr);

	/* enable reading EDID: */
	reg_write(encoder, REG_EDID_CTRL, 0x1);

	/* flag must be cleared by sw: */
	reg_write(encoder, REG_EDID_CTRL, 0x0);

	/* wait for block read to complete: */
	for (i = 100; i > 0; i--) {
		uint8_t val = reg_read(encoder, REG_INT_FLAGS_2);
		if (val & INT_FLAGS_2_EDID_BLK_RD)
			break;
		msleep(1);
	}

	if (i == 0)
		return -ETIMEDOUT;

	ret = reg_read_range(encoder, REG_EDID_DATA_0, buf, EDID_LENGTH);
	if (ret != EDID_LENGTH) {
		dev_err(encoder->dev->dev, "failed to read edid block %d: %d",
				blk, ret);
		return ret;
	}

	reg_clear(encoder, REG_INT_FLAGS_2, INT_FLAGS_2_EDID_BLK_RD);

	return 0;
}

static uint8_t *
do_get_edid(struct drm_encoder *encoder)
{
	int j = 0, valid_extensions = 0;
	uint8_t *block, *new;
	bool print_bad_edid = drm_debug & DRM_UT_KMS;

	if ((block = kmalloc(EDID_LENGTH, GFP_KERNEL)) == NULL)
		return NULL;

	/* base block fetch */
	if (read_edid_block(encoder, block, 0))
		goto fail;

	if (!drm_edid_block_valid(block, 0, print_bad_edid))
		goto fail;

	/* if there's no extensions, we're done */
	if (block[0x7e] == 0)
		return block;

	new = krealloc(block, (block[0x7e] + 1) * EDID_LENGTH, GFP_KERNEL);
	if (!new)
		goto fail;
	block = new;

	for (j = 1; j <= block[0x7e]; j++) {
		uint8_t *ext_block = block + (valid_extensions + 1) * EDID_LENGTH;
		if (read_edid_block(encoder, ext_block, j))
			goto fail;

		if (!drm_edid_block_valid(ext_block, j, print_bad_edid))
			goto fail;

		valid_extensions++;
	}

	if (valid_extensions != block[0x7e]) {
		block[EDID_LENGTH-1] += block[0x7e] - valid_extensions;
		block[0x7e] = valid_extensions;
		new = krealloc(block, (valid_extensions + 1) * EDID_LENGTH, GFP_KERNEL);
		if (!new)
			goto fail;
		block = new;
	}

	return block;

fail:
	dev_warn(encoder->dev->dev, "failed to read EDID\n");
	kfree(block);
	return NULL;
}

static int
tda998x_encoder_get_modes(struct drm_encoder *encoder,
			 struct drm_connector *connector)
{
	struct edid *edid = (struct edid *)do_get_edid(encoder);
	int n = 0;

	if (edid) {
		drm_mode_connector_update_edid_property(connector, edid);
		n = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	return n;
}

static int
tda998x_encoder_create_resources(struct drm_encoder *encoder,
				struct drm_connector *connector)
{
	DBG("");
	return 0;
}

static int
tda998x_encoder_set_property(struct drm_encoder *encoder,
			    struct drm_connector *connector,
			    struct drm_property *property,
			    uint64_t val)
{
	DBG("");
	return 0;
}

static void
tda998x_encoder_destroy(struct drm_encoder *encoder)
{
	struct tda998x_priv *priv = to_tda998x_priv(encoder);
	drm_i2c_encoder_destroy(encoder);
	kfree(priv);
}

static struct drm_encoder_slave_funcs tda998x_encoder_funcs = {
	.set_config = tda998x_encoder_set_config,
	.destroy = tda998x_encoder_destroy,
	.dpms = tda998x_encoder_dpms,
	.save = tda998x_encoder_save,
	.restore = tda998x_encoder_restore,
	.mode_fixup = tda998x_encoder_mode_fixup,
	.mode_valid = tda998x_encoder_mode_valid,
	.mode_set = tda998x_encoder_mode_set,
	.detect = tda998x_encoder_detect,
	.get_modes = tda998x_encoder_get_modes,
	.create_resources = tda998x_encoder_create_resources,
	.set_property = tda998x_encoder_set_property,
};

/* I2C driver functions */

static int
tda998x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	return 0;
}

static int
tda998x_remove(struct i2c_client *client)
{
	return 0;
}

static int
tda998x_encoder_init(struct i2c_client *client,
		    struct drm_device *dev,
		    struct drm_encoder_slave *encoder_slave)
{
	struct drm_encoder *encoder = &encoder_slave->base;
	struct tda998x_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->current_page = 0;
	priv->cec = i2c_new_dummy(client->adapter, 0x34);
	priv->dpms = DRM_MODE_DPMS_OFF;

	encoder_slave->slave_priv = priv;
	encoder_slave->slave_funcs = &tda998x_encoder_funcs;

	/* wake up the device: */
	cec_write(encoder, REG_CEC_ENAMODS,
			CEC_ENAMODS_EN_RXSENS | CEC_ENAMODS_EN_HDMI);

	tda998x_reset(encoder);

	/* read version: */
	priv->rev = reg_read(encoder, REG_VERSION_LSB) |
			reg_read(encoder, REG_VERSION_MSB) << 8;

	/* mask off feature bits: */
	priv->rev &= ~0x30; /* not-hdcp and not-scalar bit */

	switch (priv->rev) {
	case TDA9989N2:  dev_info(dev->dev, "found TDA9989 n2");  break;
	case TDA19989:   dev_info(dev->dev, "found TDA19989");    break;
	case TDA19989N2: dev_info(dev->dev, "found TDA19989 n2"); break;
	case TDA19988:   dev_info(dev->dev, "found TDA19988");    break;
	default:
		DBG("found unsupported device: %04x", priv->rev);
		goto fail;
	}

	/* after reset, enable DDC: */
	reg_write(encoder, REG_DDC_DISABLE, 0x00);

	/* set clock on DDC channel: */
	reg_write(encoder, REG_TX3, 39);

	/* if necessary, disable multi-master: */
	if (priv->rev == TDA19989)
		reg_set(encoder, REG_I2C_MASTER, I2C_MASTER_DIS_MM);

	cec_write(encoder, REG_CEC_FRO_IM_CLK_CTRL,
			CEC_FRO_IM_CLK_CTRL_GHOST_DIS | CEC_FRO_IM_CLK_CTRL_IMCLK_SEL);

	return 0;

fail:
	/* if encoder_init fails, the encoder slave is never registered,
	 * so cleanup here:
	 */
	if (priv->cec)
		i2c_unregister_device(priv->cec);
	kfree(priv);
	encoder_slave->slave_priv = NULL;
	encoder_slave->slave_funcs = NULL;
	return -ENXIO;
}

static struct i2c_device_id tda998x_ids[] = {
	{ "tda998x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tda998x_ids);

static struct drm_i2c_encoder_driver tda998x_driver = {
	.i2c_driver = {
		.probe = tda998x_probe,
		.remove = tda998x_remove,
		.driver = {
			.name = "tda998x",
		},
		.id_table = tda998x_ids,
	},
	.encoder_init = tda998x_encoder_init,
};

/* Module initialization */

static int __init
tda998x_init(void)
{
	DBG("");
	return drm_i2c_encoder_register(THIS_MODULE, &tda998x_driver);
}

static void __exit
tda998x_exit(void)
{
	DBG("");
	drm_i2c_encoder_unregister(&tda998x_driver);
}

MODULE_AUTHOR("Rob Clark <robdclark@gmail.com");
MODULE_DESCRIPTION("NXP Semiconductors TDA998X HDMI Encoder");
MODULE_LICENSE("GPL");

module_init(tda998x_init);
module_exit(tda998x_exit);
