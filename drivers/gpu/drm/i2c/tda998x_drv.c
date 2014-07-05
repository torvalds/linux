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



#include <linux/hdmi.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <sound/asoundef.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder_slave.h>
#include <drm/drm_edid.h>
#include <drm/i2c/tda998x.h>

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)

struct tda998x_priv {
	struct i2c_client *cec;
	struct i2c_client *hdmi;
	uint16_t rev;
	uint8_t current_page;
	int dpms;
	bool is_hdmi_sink;
	u8 vip_cntrl_0;
	u8 vip_cntrl_1;
	u8 vip_cntrl_2;
	struct tda998x_encoder_params params;

	wait_queue_head_t wq_edid;
	volatile int wq_edid_wait;
	struct drm_encoder *encoder;
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
#define REG_FEAT_POWERDOWN        REG(0x00, 0x0e)     /* read/write */
# define FEAT_POWERDOWN_SPDIF     (1 << 3)
#define REG_INT_FLAGS_0           REG(0x00, 0x0f)     /* read/write */
#define REG_INT_FLAGS_1           REG(0x00, 0x10)     /* read/write */
#define REG_INT_FLAGS_2           REG(0x00, 0x11)     /* read/write */
# define INT_FLAGS_2_EDID_BLK_RD  (1 << 1)
#define REG_ENA_ACLK              REG(0x00, 0x16)     /* read/write */
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
#define REG_MUX_AP                REG(0x00, 0x26)     /* read/write */
# define MUX_AP_SELECT_I2S	  0x64
# define MUX_AP_SELECT_SPDIF	  0x40
#define REG_MUX_VP_VIP_OUT        REG(0x00, 0x27)     /* read/write */
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
#define REG_VS_LINE_STRT_2_MSB    REG(0x00, 0xb1)     /* write */
#define REG_VS_LINE_STRT_2_LSB    REG(0x00, 0xb2)     /* write */
#define REG_VS_PIX_STRT_2_MSB     REG(0x00, 0xb3)     /* write */
#define REG_VS_PIX_STRT_2_LSB     REG(0x00, 0xb4)     /* write */
#define REG_VS_LINE_END_2_MSB     REG(0x00, 0xb5)     /* write */
#define REG_VS_LINE_END_2_LSB     REG(0x00, 0xb6)     /* write */
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
#define REG_VWIN_START_2_MSB      REG(0x00, 0xc1)     /* write */
#define REG_VWIN_START_2_LSB      REG(0x00, 0xc2)     /* write */
#define REG_VWIN_END_2_MSB        REG(0x00, 0xc3)     /* write */
#define REG_VWIN_END_2_LSB        REG(0x00, 0xc4)     /* write */
#define REG_DE_START_MSB          REG(0x00, 0xc5)     /* write */
#define REG_DE_START_LSB          REG(0x00, 0xc6)     /* write */
#define REG_DE_STOP_MSB           REG(0x00, 0xc7)     /* write */
#define REG_DE_STOP_LSB           REG(0x00, 0xc8)     /* write */
#define REG_TBG_CNTRL_0           REG(0x00, 0xca)     /* write */
# define TBG_CNTRL_0_TOP_TGL      (1 << 0)
# define TBG_CNTRL_0_TOP_SEL      (1 << 1)
# define TBG_CNTRL_0_DE_EXT       (1 << 2)
# define TBG_CNTRL_0_TOP_EXT      (1 << 3)
# define TBG_CNTRL_0_FRAME_DIS    (1 << 5)
# define TBG_CNTRL_0_SYNC_MTHD    (1 << 6)
# define TBG_CNTRL_0_SYNC_ONCE    (1 << 7)
#define REG_TBG_CNTRL_1           REG(0x00, 0xcb)     /* write */
# define TBG_CNTRL_1_H_TGL        (1 << 0)
# define TBG_CNTRL_1_V_TGL        (1 << 1)
# define TBG_CNTRL_1_TGL_EN       (1 << 2)
# define TBG_CNTRL_1_X_EXT        (1 << 3)
# define TBG_CNTRL_1_H_EXT        (1 << 4)
# define TBG_CNTRL_1_V_EXT        (1 << 5)
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
#define REG_I2S_FORMAT            REG(0x00, 0xfc)     /* read/write */
# define I2S_FORMAT(x)            (((x) & 3) << 0)
#define REG_AIP_CLKSEL            REG(0x00, 0xfd)     /* write */
# define AIP_CLKSEL_AIP_SPDIF	  (0 << 3)
# define AIP_CLKSEL_AIP_I2S	  (1 << 3)
# define AIP_CLKSEL_FS_ACLK	  (0 << 0)
# define AIP_CLKSEL_FS_MCLK	  (1 << 0)
# define AIP_CLKSEL_FS_FS64SPDIF  (2 << 0)

/* Page 02h: PLL settings */
#define REG_PLL_SERIAL_1          REG(0x02, 0x00)     /* read/write */
# define PLL_SERIAL_1_SRL_FDN     (1 << 0)
# define PLL_SERIAL_1_SRL_IZ(x)   (((x) & 3) << 1)
# define PLL_SERIAL_1_SRL_MAN_IZ  (1 << 6)
#define REG_PLL_SERIAL_2          REG(0x02, 0x01)     /* read/write */
# define PLL_SERIAL_2_SRL_NOSC(x) ((x) << 0)
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
# define AUDIO_DIV_SERCLK_1       0
# define AUDIO_DIV_SERCLK_2       1
# define AUDIO_DIV_SERCLK_4       2
# define AUDIO_DIV_SERCLK_8       3
# define AUDIO_DIV_SERCLK_16      4
# define AUDIO_DIV_SERCLK_32      5
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
#define REG_IF1_HB0               REG(0x10, 0x20)     /* read/write */
#define REG_IF2_HB0               REG(0x10, 0x40)     /* read/write */
#define REG_IF3_HB0               REG(0x10, 0x60)     /* read/write */
#define REG_IF4_HB0               REG(0x10, 0x80)     /* read/write */
#define REG_IF5_HB0               REG(0x10, 0xa0)     /* read/write */


/* Page 11h: audio settings and content info packets */
#define REG_AIP_CNTRL_0           REG(0x11, 0x00)     /* read/write */
# define AIP_CNTRL_0_RST_FIFO     (1 << 0)
# define AIP_CNTRL_0_SWAP         (1 << 1)
# define AIP_CNTRL_0_LAYOUT       (1 << 2)
# define AIP_CNTRL_0_ACR_MAN      (1 << 5)
# define AIP_CNTRL_0_RST_CTS      (1 << 6)
#define REG_CA_I2S                REG(0x11, 0x01)     /* read/write */
# define CA_I2S_CA_I2S(x)         (((x) & 31) << 0)
# define CA_I2S_HBR_CHSTAT        (1 << 6)
#define REG_LATENCY_RD            REG(0x11, 0x04)     /* read/write */
#define REG_ACR_CTS_0             REG(0x11, 0x05)     /* read/write */
#define REG_ACR_CTS_1             REG(0x11, 0x06)     /* read/write */
#define REG_ACR_CTS_2             REG(0x11, 0x07)     /* read/write */
#define REG_ACR_N_0               REG(0x11, 0x08)     /* read/write */
#define REG_ACR_N_1               REG(0x11, 0x09)     /* read/write */
#define REG_ACR_N_2               REG(0x11, 0x0a)     /* read/write */
#define REG_CTS_N                 REG(0x11, 0x0c)     /* read/write */
# define CTS_N_K(x)               (((x) & 7) << 0)
# define CTS_N_M(x)               (((x) & 3) << 4)
#define REG_ENC_CNTRL             REG(0x11, 0x0d)     /* read/write */
# define ENC_CNTRL_RST_ENC        (1 << 0)
# define ENC_CNTRL_RST_SEL        (1 << 1)
# define ENC_CNTRL_CTL_CODE(x)    (((x) & 3) << 2)
#define REG_DIP_FLAGS             REG(0x11, 0x0e)     /* read/write */
# define DIP_FLAGS_ACR            (1 << 0)
# define DIP_FLAGS_GC             (1 << 1)
#define REG_DIP_IF_FLAGS          REG(0x11, 0x0f)     /* read/write */
# define DIP_IF_FLAGS_IF1         (1 << 1)
# define DIP_IF_FLAGS_IF2         (1 << 2)
# define DIP_IF_FLAGS_IF3         (1 << 3)
# define DIP_IF_FLAGS_IF4         (1 << 4)
# define DIP_IF_FLAGS_IF5         (1 << 5)
#define REG_CH_STAT_B(x)          REG(0x11, 0x14 + (x)) /* read/write */


/* Page 12h: HDCP and OTP */
#define REG_TX3                   REG(0x12, 0x9a)     /* read/write */
#define REG_TX4                   REG(0x12, 0x9b)     /* read/write */
# define TX4_PD_RAM               (1 << 1)
#define REG_TX33                  REG(0x12, 0xb8)     /* read/write */
# define TX33_HDMI                (1 << 1)


/* Page 13h: Gamut related metadata packets */



/* CEC registers: (not paged)
 */
#define REG_CEC_INTSTATUS	  0xee		      /* read */
# define CEC_INTSTATUS_CEC	  (1 << 0)
# define CEC_INTSTATUS_HDMI	  (1 << 1)
#define REG_CEC_FRO_IM_CLK_CTRL   0xfb                /* read/write */
# define CEC_FRO_IM_CLK_CTRL_GHOST_DIS (1 << 7)
# define CEC_FRO_IM_CLK_CTRL_ENA_OTP   (1 << 6)
# define CEC_FRO_IM_CLK_CTRL_IMCLK_SEL (1 << 1)
# define CEC_FRO_IM_CLK_CTRL_FRO_DIV   (1 << 0)
#define REG_CEC_RXSHPDINTENA	  0xfc		      /* read/write */
#define REG_CEC_RXSHPDINT	  0xfd		      /* read */
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
cec_write(struct tda998x_priv *priv, uint16_t addr, uint8_t val)
{
	struct i2c_client *client = priv->cec;
	uint8_t buf[] = {addr, val};
	int ret;

	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret < 0)
		dev_err(&client->dev, "Error %d writing to cec:0x%x\n", ret, addr);
}

static uint8_t
cec_read(struct tda998x_priv *priv, uint8_t addr)
{
	struct i2c_client *client = priv->cec;
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

static int
set_page(struct tda998x_priv *priv, uint16_t reg)
{
	if (REG2PAGE(reg) != priv->current_page) {
		struct i2c_client *client = priv->hdmi;
		uint8_t buf[] = {
				REG_CURPAGE, REG2PAGE(reg)
		};
		int ret = i2c_master_send(client, buf, sizeof(buf));
		if (ret < 0) {
			dev_err(&client->dev, "setpage %04x err %d\n",
					reg, ret);
			return ret;
		}

		priv->current_page = REG2PAGE(reg);
	}
	return 0;
}

static int
reg_read_range(struct tda998x_priv *priv, uint16_t reg, char *buf, int cnt)
{
	struct i2c_client *client = priv->hdmi;
	uint8_t addr = REG2ADDR(reg);
	int ret;

	ret = set_page(priv, reg);
	if (ret < 0)
		return ret;

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

static void
reg_write_range(struct tda998x_priv *priv, uint16_t reg, uint8_t *p, int cnt)
{
	struct i2c_client *client = priv->hdmi;
	uint8_t buf[cnt+1];
	int ret;

	buf[0] = REG2ADDR(reg);
	memcpy(&buf[1], p, cnt);

	ret = set_page(priv, reg);
	if (ret < 0)
		return;

	ret = i2c_master_send(client, buf, cnt + 1);
	if (ret < 0)
		dev_err(&client->dev, "Error %d writing to 0x%x\n", ret, reg);
}

static int
reg_read(struct tda998x_priv *priv, uint16_t reg)
{
	uint8_t val = 0;
	int ret;

	ret = reg_read_range(priv, reg, &val, sizeof(val));
	if (ret < 0)
		return ret;
	return val;
}

static void
reg_write(struct tda998x_priv *priv, uint16_t reg, uint8_t val)
{
	struct i2c_client *client = priv->hdmi;
	uint8_t buf[] = {REG2ADDR(reg), val};
	int ret;

	ret = set_page(priv, reg);
	if (ret < 0)
		return;

	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret < 0)
		dev_err(&client->dev, "Error %d writing to 0x%x\n", ret, reg);
}

static void
reg_write16(struct tda998x_priv *priv, uint16_t reg, uint16_t val)
{
	struct i2c_client *client = priv->hdmi;
	uint8_t buf[] = {REG2ADDR(reg), val >> 8, val};
	int ret;

	ret = set_page(priv, reg);
	if (ret < 0)
		return;

	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret < 0)
		dev_err(&client->dev, "Error %d writing to 0x%x\n", ret, reg);
}

static void
reg_set(struct tda998x_priv *priv, uint16_t reg, uint8_t val)
{
	int old_val;

	old_val = reg_read(priv, reg);
	if (old_val >= 0)
		reg_write(priv, reg, old_val | val);
}

static void
reg_clear(struct tda998x_priv *priv, uint16_t reg, uint8_t val)
{
	int old_val;

	old_val = reg_read(priv, reg);
	if (old_val >= 0)
		reg_write(priv, reg, old_val & ~val);
}

static void
tda998x_reset(struct tda998x_priv *priv)
{
	/* reset audio and i2c master: */
	reg_write(priv, REG_SOFTRESET, SOFTRESET_AUDIO | SOFTRESET_I2C_MASTER);
	msleep(50);
	reg_write(priv, REG_SOFTRESET, 0);
	msleep(50);

	/* reset transmitter: */
	reg_set(priv, REG_MAIN_CNTRL0, MAIN_CNTRL0_SR);
	reg_clear(priv, REG_MAIN_CNTRL0, MAIN_CNTRL0_SR);

	/* PLL registers common configuration */
	reg_write(priv, REG_PLL_SERIAL_1, 0x00);
	reg_write(priv, REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(1));
	reg_write(priv, REG_PLL_SERIAL_3, 0x00);
	reg_write(priv, REG_SERIALIZER,   0x00);
	reg_write(priv, REG_BUFFER_OUT,   0x00);
	reg_write(priv, REG_PLL_SCG1,     0x00);
	reg_write(priv, REG_AUDIO_DIV,    AUDIO_DIV_SERCLK_8);
	reg_write(priv, REG_SEL_CLK,      SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);
	reg_write(priv, REG_PLL_SCGN1,    0xfa);
	reg_write(priv, REG_PLL_SCGN2,    0x00);
	reg_write(priv, REG_PLL_SCGR1,    0x5b);
	reg_write(priv, REG_PLL_SCGR2,    0x00);
	reg_write(priv, REG_PLL_SCG2,     0x10);

	/* Write the default value MUX register */
	reg_write(priv, REG_MUX_VP_VIP_OUT, 0x24);
}

/*
 * only 2 interrupts may occur: screen plug/unplug and EDID read
 */
static irqreturn_t tda998x_irq_thread(int irq, void *data)
{
	struct tda998x_priv *priv = data;
	u8 sta, cec, lvl, flag0, flag1, flag2;

	if (!priv)
		return IRQ_HANDLED;
	sta = cec_read(priv, REG_CEC_INTSTATUS);
	cec = cec_read(priv, REG_CEC_RXSHPDINT);
	lvl = cec_read(priv, REG_CEC_RXSHPDLEV);
	flag0 = reg_read(priv, REG_INT_FLAGS_0);
	flag1 = reg_read(priv, REG_INT_FLAGS_1);
	flag2 = reg_read(priv, REG_INT_FLAGS_2);
	DRM_DEBUG_DRIVER(
		"tda irq sta %02x cec %02x lvl %02x f0 %02x f1 %02x f2 %02x\n",
		sta, cec, lvl, flag0, flag1, flag2);
	if ((flag2 & INT_FLAGS_2_EDID_BLK_RD) && priv->wq_edid_wait) {
		priv->wq_edid_wait = 0;
		wake_up(&priv->wq_edid);
	} else if (cec != 0) {			/* HPD change */
		if (priv->encoder && priv->encoder->dev)
			drm_helper_hpd_irq_event(priv->encoder->dev);
	}
	return IRQ_HANDLED;
}

static uint8_t tda998x_cksum(uint8_t *buf, size_t bytes)
{
	int sum = 0;

	while (bytes--)
		sum -= *buf++;
	return sum;
}

#define HB(x) (x)
#define PB(x) (HB(2) + 1 + (x))

static void
tda998x_write_if(struct tda998x_priv *priv, uint8_t bit, uint16_t addr,
		 uint8_t *buf, size_t size)
{
	buf[PB(0)] = tda998x_cksum(buf, size);

	reg_clear(priv, REG_DIP_IF_FLAGS, bit);
	reg_write_range(priv, addr, buf, size);
	reg_set(priv, REG_DIP_IF_FLAGS, bit);
}

static void
tda998x_write_aif(struct tda998x_priv *priv, struct tda998x_encoder_params *p)
{
	u8 buf[PB(HDMI_AUDIO_INFOFRAME_SIZE) + 1];

	memset(buf, 0, sizeof(buf));
	buf[HB(0)] = HDMI_INFOFRAME_TYPE_AUDIO;
	buf[HB(1)] = 0x01;
	buf[HB(2)] = HDMI_AUDIO_INFOFRAME_SIZE;
	buf[PB(1)] = p->audio_frame[1] & 0x07; /* CC */
	buf[PB(2)] = p->audio_frame[2] & 0x1c; /* SF */
	buf[PB(4)] = p->audio_frame[4];
	buf[PB(5)] = p->audio_frame[5] & 0xf8; /* DM_INH + LSV */

	tda998x_write_if(priv, DIP_IF_FLAGS_IF4, REG_IF4_HB0, buf,
			 sizeof(buf));
}

static void
tda998x_write_avi(struct tda998x_priv *priv, struct drm_display_mode *mode)
{
	u8 buf[PB(HDMI_AVI_INFOFRAME_SIZE) + 1];

	memset(buf, 0, sizeof(buf));
	buf[HB(0)] = HDMI_INFOFRAME_TYPE_AVI;
	buf[HB(1)] = 0x02;
	buf[HB(2)] = HDMI_AVI_INFOFRAME_SIZE;
	buf[PB(1)] = HDMI_SCAN_MODE_UNDERSCAN;
	buf[PB(2)] = HDMI_ACTIVE_ASPECT_PICTURE;
	buf[PB(3)] = HDMI_QUANTIZATION_RANGE_FULL << 2;
	buf[PB(4)] = drm_match_cea_mode(mode);

	tda998x_write_if(priv, DIP_IF_FLAGS_IF2, REG_IF2_HB0, buf,
			 sizeof(buf));
}

static void tda998x_audio_mute(struct tda998x_priv *priv, bool on)
{
	if (on) {
		reg_set(priv, REG_SOFTRESET, SOFTRESET_AUDIO);
		reg_clear(priv, REG_SOFTRESET, SOFTRESET_AUDIO);
		reg_set(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_FIFO);
	} else {
		reg_clear(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_FIFO);
	}
}

static void
tda998x_configure_audio(struct tda998x_priv *priv,
		struct drm_display_mode *mode, struct tda998x_encoder_params *p)
{
	uint8_t buf[6], clksel_aip, clksel_fs, cts_n, adiv;
	uint32_t n;

	/* Enable audio ports */
	reg_write(priv, REG_ENA_AP, p->audio_cfg);
	reg_write(priv, REG_ENA_ACLK, p->audio_clk_cfg);

	/* Set audio input source */
	switch (p->audio_format) {
	case AFMT_SPDIF:
		reg_write(priv, REG_MUX_AP, MUX_AP_SELECT_SPDIF);
		clksel_aip = AIP_CLKSEL_AIP_SPDIF;
		clksel_fs = AIP_CLKSEL_FS_FS64SPDIF;
		cts_n = CTS_N_M(3) | CTS_N_K(3);
		break;

	case AFMT_I2S:
		reg_write(priv, REG_MUX_AP, MUX_AP_SELECT_I2S);
		clksel_aip = AIP_CLKSEL_AIP_I2S;
		clksel_fs = AIP_CLKSEL_FS_ACLK;
		cts_n = CTS_N_M(3) | CTS_N_K(3);
		break;

	default:
		BUG();
		return;
	}

	reg_write(priv, REG_AIP_CLKSEL, clksel_aip);
	reg_clear(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_LAYOUT |
					AIP_CNTRL_0_ACR_MAN);	/* auto CTS */
	reg_write(priv, REG_CTS_N, cts_n);

	/*
	 * Audio input somehow depends on HDMI line rate which is
	 * related to pixclk. Testing showed that modes with pixclk
	 * >100MHz need a larger divider while <40MHz need the default.
	 * There is no detailed info in the datasheet, so we just
	 * assume 100MHz requires larger divider.
	 */
	adiv = AUDIO_DIV_SERCLK_8;
	if (mode->clock > 100000)
		adiv++;			/* AUDIO_DIV_SERCLK_16 */

	/* S/PDIF asks for a larger divider */
	if (p->audio_format == AFMT_SPDIF)
		adiv++;			/* AUDIO_DIV_SERCLK_16 or _32 */

	reg_write(priv, REG_AUDIO_DIV, adiv);

	/*
	 * This is the approximate value of N, which happens to be
	 * the recommended values for non-coherent clocks.
	 */
	n = 128 * p->audio_sample_rate / 1000;

	/* Write the CTS and N values */
	buf[0] = 0x44;
	buf[1] = 0x42;
	buf[2] = 0x01;
	buf[3] = n;
	buf[4] = n >> 8;
	buf[5] = n >> 16;
	reg_write_range(priv, REG_ACR_CTS_0, buf, 6);

	/* Set CTS clock reference */
	reg_write(priv, REG_AIP_CLKSEL, clksel_aip | clksel_fs);

	/* Reset CTS generator */
	reg_set(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_CTS);
	reg_clear(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_CTS);

	/* Write the channel status */
	buf[0] = IEC958_AES0_CON_NOT_COPYRIGHT;
	buf[1] = 0x00;
	buf[2] = IEC958_AES3_CON_FS_NOTID;
	buf[3] = IEC958_AES4_CON_ORIGFS_NOTID |
			IEC958_AES4_CON_MAX_WORDLEN_24;
	reg_write_range(priv, REG_CH_STAT_B(0), buf, 4);

	tda998x_audio_mute(priv, true);
	msleep(20);
	tda998x_audio_mute(priv, false);

	/* Write the audio information packet */
	tda998x_write_aif(priv, p);
}

/* DRM encoder functions */

static void
tda998x_encoder_set_config(struct drm_encoder *encoder, void *params)
{
	struct tda998x_priv *priv = to_tda998x_priv(encoder);
	struct tda998x_encoder_params *p = params;

	priv->vip_cntrl_0 = VIP_CNTRL_0_SWAP_A(p->swap_a) |
			    (p->mirr_a ? VIP_CNTRL_0_MIRR_A : 0) |
			    VIP_CNTRL_0_SWAP_B(p->swap_b) |
			    (p->mirr_b ? VIP_CNTRL_0_MIRR_B : 0);
	priv->vip_cntrl_1 = VIP_CNTRL_1_SWAP_C(p->swap_c) |
			    (p->mirr_c ? VIP_CNTRL_1_MIRR_C : 0) |
			    VIP_CNTRL_1_SWAP_D(p->swap_d) |
			    (p->mirr_d ? VIP_CNTRL_1_MIRR_D : 0);
	priv->vip_cntrl_2 = VIP_CNTRL_2_SWAP_E(p->swap_e) |
			    (p->mirr_e ? VIP_CNTRL_2_MIRR_E : 0) |
			    VIP_CNTRL_2_SWAP_F(p->swap_f) |
			    (p->mirr_f ? VIP_CNTRL_2_MIRR_F : 0);

	priv->params = *p;
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
		/* enable video ports, audio will be enabled later */
		reg_write(priv, REG_ENA_VP_0, 0xff);
		reg_write(priv, REG_ENA_VP_1, 0xff);
		reg_write(priv, REG_ENA_VP_2, 0xff);
		/* set muxing after enabling ports: */
		reg_write(priv, REG_VIP_CNTRL_0, priv->vip_cntrl_0);
		reg_write(priv, REG_VIP_CNTRL_1, priv->vip_cntrl_1);
		reg_write(priv, REG_VIP_CNTRL_2, priv->vip_cntrl_2);
		break;
	case DRM_MODE_DPMS_OFF:
		/* disable video ports */
		reg_write(priv, REG_ENA_VP_0, 0x00);
		reg_write(priv, REG_ENA_VP_1, 0x00);
		reg_write(priv, REG_ENA_VP_2, 0x00);
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
	if (mode->clock > 150000)
		return MODE_CLOCK_HIGH;
	if (mode->htotal >= BIT(13))
		return MODE_BAD_HVALUE;
	if (mode->vtotal >= BIT(11))
		return MODE_BAD_VVALUE;
	return MODE_OK;
}

static void
tda998x_encoder_mode_set(struct drm_encoder *encoder,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct tda998x_priv *priv = to_tda998x_priv(encoder);
	uint16_t ref_pix, ref_line, n_pix, n_line;
	uint16_t hs_pix_s, hs_pix_e;
	uint16_t vs1_pix_s, vs1_pix_e, vs1_line_s, vs1_line_e;
	uint16_t vs2_pix_s, vs2_pix_e, vs2_line_s, vs2_line_e;
	uint16_t vwin1_line_s, vwin1_line_e;
	uint16_t vwin2_line_s, vwin2_line_e;
	uint16_t de_pix_s, de_pix_e;
	uint8_t reg, div, rep;

	/*
	 * Internally TDA998x is using ITU-R BT.656 style sync but
	 * we get VESA style sync. TDA998x is using a reference pixel
	 * relative to ITU to sync to the input frame and for output
	 * sync generation. Currently, we are using reference detection
	 * from HS/VS, i.e. REFPIX/REFLINE denote frame start sync point
	 * which is position of rising VS with coincident rising HS.
	 *
	 * Now there is some issues to take care of:
	 * - HDMI data islands require sync-before-active
	 * - TDA998x register values must be > 0 to be enabled
	 * - REFLINE needs an additional offset of +1
	 * - REFPIX needs an addtional offset of +1 for UYUV and +3 for RGB
	 *
	 * So we add +1 to all horizontal and vertical register values,
	 * plus an additional +3 for REFPIX as we are using RGB input only.
	 */
	n_pix        = mode->htotal;
	n_line       = mode->vtotal;

	hs_pix_e     = mode->hsync_end - mode->hdisplay;
	hs_pix_s     = mode->hsync_start - mode->hdisplay;
	de_pix_e     = mode->htotal;
	de_pix_s     = mode->htotal - mode->hdisplay;
	ref_pix      = 3 + hs_pix_s;

	/*
	 * Attached LCD controllers may generate broken sync. Allow
	 * those to adjust the position of the rising VS edge by adding
	 * HSKEW to ref_pix.
	 */
	if (adjusted_mode->flags & DRM_MODE_FLAG_HSKEW)
		ref_pix += adjusted_mode->hskew;

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) == 0) {
		ref_line     = 1 + mode->vsync_start - mode->vdisplay;
		vwin1_line_s = mode->vtotal - mode->vdisplay - 1;
		vwin1_line_e = vwin1_line_s + mode->vdisplay;
		vs1_pix_s    = vs1_pix_e = hs_pix_s;
		vs1_line_s   = mode->vsync_start - mode->vdisplay;
		vs1_line_e   = vs1_line_s +
			       mode->vsync_end - mode->vsync_start;
		vwin2_line_s = vwin2_line_e = 0;
		vs2_pix_s    = vs2_pix_e  = 0;
		vs2_line_s   = vs2_line_e = 0;
	} else {
		ref_line     = 1 + (mode->vsync_start - mode->vdisplay)/2;
		vwin1_line_s = (mode->vtotal - mode->vdisplay)/2;
		vwin1_line_e = vwin1_line_s + mode->vdisplay/2;
		vs1_pix_s    = vs1_pix_e = hs_pix_s;
		vs1_line_s   = (mode->vsync_start - mode->vdisplay)/2;
		vs1_line_e   = vs1_line_s +
			       (mode->vsync_end - mode->vsync_start)/2;
		vwin2_line_s = vwin1_line_s + mode->vtotal/2;
		vwin2_line_e = vwin2_line_s + mode->vdisplay/2;
		vs2_pix_s    = vs2_pix_e = hs_pix_s + mode->htotal/2;
		vs2_line_s   = vs1_line_s + mode->vtotal/2 ;
		vs2_line_e   = vs2_line_s +
			       (mode->vsync_end - mode->vsync_start)/2;
	}

	div = 148500 / mode->clock;
	if (div != 0) {
		div--;
		if (div > 3)
			div = 3;
	}

	/* mute the audio FIFO: */
	reg_set(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_FIFO);

	/* set HDMI HDCP mode off: */
	reg_write(priv, REG_TBG_CNTRL_1, TBG_CNTRL_1_DWIN_DIS);
	reg_clear(priv, REG_TX33, TX33_HDMI);
	reg_write(priv, REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(0));

	/* no pre-filter or interpolator: */
	reg_write(priv, REG_HVF_CNTRL_0, HVF_CNTRL_0_PREFIL(0) |
			HVF_CNTRL_0_INTPOL(0));
	reg_write(priv, REG_VIP_CNTRL_5, VIP_CNTRL_5_SP_CNT(0));
	reg_write(priv, REG_VIP_CNTRL_4, VIP_CNTRL_4_BLANKIT(0) |
			VIP_CNTRL_4_BLC(0));

	reg_clear(priv, REG_PLL_SERIAL_1, PLL_SERIAL_1_SRL_MAN_IZ);
	reg_clear(priv, REG_PLL_SERIAL_3, PLL_SERIAL_3_SRL_CCIR |
					  PLL_SERIAL_3_SRL_DE);
	reg_write(priv, REG_SERIALIZER, 0);
	reg_write(priv, REG_HVF_CNTRL_1, HVF_CNTRL_1_VQR(0));

	/* TODO enable pixel repeat for pixel rates less than 25Msamp/s */
	rep = 0;
	reg_write(priv, REG_RPT_CNTRL, 0);
	reg_write(priv, REG_SEL_CLK, SEL_CLK_SEL_VRF_CLK(0) |
			SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);

	reg_write(priv, REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(div) |
			PLL_SERIAL_2_SRL_PR(rep));

	/* set color matrix bypass flag: */
	reg_write(priv, REG_MAT_CONTRL, MAT_CONTRL_MAT_BP |
				MAT_CONTRL_MAT_SC(1));

	/* set BIAS tmds value: */
	reg_write(priv, REG_ANA_GENERAL, 0x09);

	/*
	 * Sync on rising HSYNC/VSYNC
	 */
	reg = VIP_CNTRL_3_SYNC_HS;

	/*
	 * TDA19988 requires high-active sync at input stage,
	 * so invert low-active sync provided by master encoder here
	 */
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		reg |= VIP_CNTRL_3_H_TGL;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		reg |= VIP_CNTRL_3_V_TGL;
	reg_write(priv, REG_VIP_CNTRL_3, reg);

	reg_write(priv, REG_VIDFORMAT, 0x00);
	reg_write16(priv, REG_REFPIX_MSB, ref_pix);
	reg_write16(priv, REG_REFLINE_MSB, ref_line);
	reg_write16(priv, REG_NPIX_MSB, n_pix);
	reg_write16(priv, REG_NLINE_MSB, n_line);
	reg_write16(priv, REG_VS_LINE_STRT_1_MSB, vs1_line_s);
	reg_write16(priv, REG_VS_PIX_STRT_1_MSB, vs1_pix_s);
	reg_write16(priv, REG_VS_LINE_END_1_MSB, vs1_line_e);
	reg_write16(priv, REG_VS_PIX_END_1_MSB, vs1_pix_e);
	reg_write16(priv, REG_VS_LINE_STRT_2_MSB, vs2_line_s);
	reg_write16(priv, REG_VS_PIX_STRT_2_MSB, vs2_pix_s);
	reg_write16(priv, REG_VS_LINE_END_2_MSB, vs2_line_e);
	reg_write16(priv, REG_VS_PIX_END_2_MSB, vs2_pix_e);
	reg_write16(priv, REG_HS_PIX_START_MSB, hs_pix_s);
	reg_write16(priv, REG_HS_PIX_STOP_MSB, hs_pix_e);
	reg_write16(priv, REG_VWIN_START_1_MSB, vwin1_line_s);
	reg_write16(priv, REG_VWIN_END_1_MSB, vwin1_line_e);
	reg_write16(priv, REG_VWIN_START_2_MSB, vwin2_line_s);
	reg_write16(priv, REG_VWIN_END_2_MSB, vwin2_line_e);
	reg_write16(priv, REG_DE_START_MSB, de_pix_s);
	reg_write16(priv, REG_DE_STOP_MSB, de_pix_e);

	if (priv->rev == TDA19988) {
		/* let incoming pixels fill the active space (if any) */
		reg_write(priv, REG_ENABLE_SPACE, 0x00);
	}

	/*
	 * Always generate sync polarity relative to input sync and
	 * revert input stage toggled sync at output stage
	 */
	reg = TBG_CNTRL_1_DWIN_DIS | TBG_CNTRL_1_TGL_EN;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		reg |= TBG_CNTRL_1_H_TGL;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		reg |= TBG_CNTRL_1_V_TGL;
	reg_write(priv, REG_TBG_CNTRL_1, reg);

	/* must be last register set: */
	reg_write(priv, REG_TBG_CNTRL_0, 0);

	/* Only setup the info frames if the sink is HDMI */
	if (priv->is_hdmi_sink) {
		/* We need to turn HDMI HDCP stuff on to get audio through */
		reg &= ~TBG_CNTRL_1_DWIN_DIS;
		reg_write(priv, REG_TBG_CNTRL_1, reg);
		reg_write(priv, REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(1));
		reg_set(priv, REG_TX33, TX33_HDMI);

		tda998x_write_avi(priv, adjusted_mode);

		if (priv->params.audio_cfg)
			tda998x_configure_audio(priv, adjusted_mode,
						&priv->params);
	}
}

static enum drm_connector_status
tda998x_encoder_detect(struct drm_encoder *encoder,
		      struct drm_connector *connector)
{
	struct tda998x_priv *priv = to_tda998x_priv(encoder);
	uint8_t val = cec_read(priv, REG_CEC_RXSHPDLEV);

	return (val & CEC_RXSHPDLEV_HPD) ? connector_status_connected :
			connector_status_disconnected;
}

static int
read_edid_block(struct drm_encoder *encoder, uint8_t *buf, int blk)
{
	struct tda998x_priv *priv = to_tda998x_priv(encoder);
	uint8_t offset, segptr;
	int ret, i;

	offset = (blk & 1) ? 128 : 0;
	segptr = blk / 2;

	reg_write(priv, REG_DDC_ADDR, 0xa0);
	reg_write(priv, REG_DDC_OFFS, offset);
	reg_write(priv, REG_DDC_SEGM_ADDR, 0x60);
	reg_write(priv, REG_DDC_SEGM, segptr);

	/* enable reading EDID: */
	priv->wq_edid_wait = 1;
	reg_write(priv, REG_EDID_CTRL, 0x1);

	/* flag must be cleared by sw: */
	reg_write(priv, REG_EDID_CTRL, 0x0);

	/* wait for block read to complete: */
	if (priv->hdmi->irq) {
		i = wait_event_timeout(priv->wq_edid,
					!priv->wq_edid_wait,
					msecs_to_jiffies(100));
		if (i < 0) {
			dev_err(&priv->hdmi->dev, "read edid wait err %d\n", i);
			return i;
		}
	} else {
		for (i = 100; i > 0; i--) {
			msleep(1);
			ret = reg_read(priv, REG_INT_FLAGS_2);
			if (ret < 0)
				return ret;
			if (ret & INT_FLAGS_2_EDID_BLK_RD)
				break;
		}
	}

	if (i == 0) {
		dev_err(&priv->hdmi->dev, "read edid timeout\n");
		return -ETIMEDOUT;
	}

	ret = reg_read_range(priv, REG_EDID_DATA_0, buf, EDID_LENGTH);
	if (ret != EDID_LENGTH) {
		dev_err(&priv->hdmi->dev, "failed to read edid block %d: %d\n",
			blk, ret);
		return ret;
	}

	return 0;
}

static uint8_t *
do_get_edid(struct drm_encoder *encoder)
{
	struct tda998x_priv *priv = to_tda998x_priv(encoder);
	int j, valid_extensions = 0;
	uint8_t *block, *new;
	bool print_bad_edid = drm_debug & DRM_UT_KMS;

	if ((block = kmalloc(EDID_LENGTH, GFP_KERNEL)) == NULL)
		return NULL;

	if (priv->rev == TDA19988)
		reg_clear(priv, REG_TX4, TX4_PD_RAM);

	/* base block fetch */
	if (read_edid_block(encoder, block, 0))
		goto fail;

	if (!drm_edid_block_valid(block, 0, print_bad_edid))
		goto fail;

	/* if there's no extensions, we're done */
	if (block[0x7e] == 0)
		goto done;

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

done:
	if (priv->rev == TDA19988)
		reg_set(priv, REG_TX4, TX4_PD_RAM);

	return block;

fail:
	if (priv->rev == TDA19988)
		reg_set(priv, REG_TX4, TX4_PD_RAM);
	dev_warn(&priv->hdmi->dev, "failed to read EDID\n");
	kfree(block);
	return NULL;
}

static int
tda998x_encoder_get_modes(struct drm_encoder *encoder,
			 struct drm_connector *connector)
{
	struct tda998x_priv *priv = to_tda998x_priv(encoder);
	struct edid *edid = (struct edid *)do_get_edid(encoder);
	int n = 0;

	if (edid) {
		drm_mode_connector_update_edid_property(connector, edid);
		n = drm_add_edid_modes(connector, edid);
		priv->is_hdmi_sink = drm_detect_hdmi_monitor(edid);
		kfree(edid);
	}

	return n;
}

static int
tda998x_encoder_create_resources(struct drm_encoder *encoder,
				struct drm_connector *connector)
{
	struct tda998x_priv *priv = to_tda998x_priv(encoder);

	if (priv->hdmi->irq)
		connector->polled = DRM_CONNECTOR_POLL_HPD;
	else
		connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			DRM_CONNECTOR_POLL_DISCONNECT;
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

	/* disable all IRQs and free the IRQ handler */
	cec_write(priv, REG_CEC_RXSHPDINTENA, 0);
	reg_clear(priv, REG_INT_FLAGS_2, INT_FLAGS_2_EDID_BLK_RD);
	if (priv->hdmi->irq)
		free_irq(priv->hdmi->irq, priv);

	if (priv->cec)
		i2c_unregister_device(priv->cec);
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
	struct tda998x_priv *priv;
	struct device_node *np = client->dev.of_node;
	u32 video;
	int rev_lo, rev_hi, ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->vip_cntrl_0 = VIP_CNTRL_0_SWAP_A(2) | VIP_CNTRL_0_SWAP_B(3);
	priv->vip_cntrl_1 = VIP_CNTRL_1_SWAP_C(0) | VIP_CNTRL_1_SWAP_D(1);
	priv->vip_cntrl_2 = VIP_CNTRL_2_SWAP_E(4) | VIP_CNTRL_2_SWAP_F(5);

	priv->current_page = 0xff;
	priv->hdmi = client;
	priv->cec = i2c_new_dummy(client->adapter, 0x34);
	if (!priv->cec) {
		kfree(priv);
		return -ENODEV;
	}

	priv->encoder = &encoder_slave->base;
	priv->dpms = DRM_MODE_DPMS_OFF;

	encoder_slave->slave_priv = priv;
	encoder_slave->slave_funcs = &tda998x_encoder_funcs;

	/* wake up the device: */
	cec_write(priv, REG_CEC_ENAMODS,
			CEC_ENAMODS_EN_RXSENS | CEC_ENAMODS_EN_HDMI);

	tda998x_reset(priv);

	/* read version: */
	rev_lo = reg_read(priv, REG_VERSION_LSB);
	rev_hi = reg_read(priv, REG_VERSION_MSB);
	if (rev_lo < 0 || rev_hi < 0) {
		ret = rev_lo < 0 ? rev_lo : rev_hi;
		goto fail;
	}

	priv->rev = rev_lo | rev_hi << 8;

	/* mask off feature bits: */
	priv->rev &= ~0x30; /* not-hdcp and not-scalar bit */

	switch (priv->rev) {
	case TDA9989N2:
		dev_info(&client->dev, "found TDA9989 n2");
		break;
	case TDA19989:
		dev_info(&client->dev, "found TDA19989");
		break;
	case TDA19989N2:
		dev_info(&client->dev, "found TDA19989 n2");
		break;
	case TDA19988:
		dev_info(&client->dev, "found TDA19988");
		break;
	default:
		dev_err(&client->dev, "found unsupported device: %04x\n",
			priv->rev);
		goto fail;
	}

	/* after reset, enable DDC: */
	reg_write(priv, REG_DDC_DISABLE, 0x00);

	/* set clock on DDC channel: */
	reg_write(priv, REG_TX3, 39);

	/* if necessary, disable multi-master: */
	if (priv->rev == TDA19989)
		reg_set(priv, REG_I2C_MASTER, I2C_MASTER_DIS_MM);

	cec_write(priv, REG_CEC_FRO_IM_CLK_CTRL,
			CEC_FRO_IM_CLK_CTRL_GHOST_DIS | CEC_FRO_IM_CLK_CTRL_IMCLK_SEL);

	/* initialize the optional IRQ */
	if (client->irq) {
		int irqf_trigger;

		/* init read EDID waitqueue */
		init_waitqueue_head(&priv->wq_edid);

		/* clear pending interrupts */
		reg_read(priv, REG_INT_FLAGS_0);
		reg_read(priv, REG_INT_FLAGS_1);
		reg_read(priv, REG_INT_FLAGS_2);

		irqf_trigger =
			irqd_get_trigger_type(irq_get_irq_data(client->irq));
		ret = request_threaded_irq(client->irq, NULL,
					   tda998x_irq_thread,
					   irqf_trigger | IRQF_ONESHOT,
					   "tda998x", priv);
		if (ret) {
			dev_err(&client->dev,
				"failed to request IRQ#%u: %d\n",
				client->irq, ret);
			goto fail;
		}

		/* enable HPD irq */
		cec_write(priv, REG_CEC_RXSHPDINTENA, CEC_RXSHPDLEV_HPD);
	}

	/* enable EDID read irq: */
	reg_set(priv, REG_INT_FLAGS_2, INT_FLAGS_2_EDID_BLK_RD);

	if (!np)
		return 0;		/* non-DT */

	/* get the optional video properties */
	ret = of_property_read_u32(np, "video-ports", &video);
	if (ret == 0) {
		priv->vip_cntrl_0 = video >> 16;
		priv->vip_cntrl_1 = video >> 8;
		priv->vip_cntrl_2 = video;
	}

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

#ifdef CONFIG_OF
static const struct of_device_id tda998x_dt_ids[] = {
	{ .compatible = "nxp,tda998x", },
	{ }
};
MODULE_DEVICE_TABLE(of, tda998x_dt_ids);
#endif

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
			.of_match_table = of_match_ptr(tda998x_dt_ids),
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
