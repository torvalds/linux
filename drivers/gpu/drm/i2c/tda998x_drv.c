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

#include <linux/component.h>
#include <linux/gpio/consumer.h>
#include <linux/hdmi.h>
#include <linux/module.h>
#include <linux/platform_data/tda9950.h>
#include <linux/irq.h>
#include <sound/asoundef.h>
#include <sound/hdmi-codec.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/i2c/tda998x.h>

#include <media/cec-notifier.h>

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)

struct tda998x_audio_port {
	u8 format;		/* AFMT_xxx */
	u8 config;		/* AP value */
};

struct tda998x_priv {
	struct i2c_client *cec;
	struct i2c_client *hdmi;
	struct mutex mutex;
	u16 rev;
	u8 cec_addr;
	u8 current_page;
	bool is_on;
	bool supports_infoframes;
	bool sink_has_audio;
	u8 vip_cntrl_0;
	u8 vip_cntrl_1;
	u8 vip_cntrl_2;
	unsigned long tmds_clock;
	struct tda998x_audio_params audio_params;

	struct platform_device *audio_pdev;
	struct mutex audio_mutex;

	struct mutex edid_mutex;
	wait_queue_head_t wq_edid;
	volatile int wq_edid_wait;

	struct work_struct detect_work;
	struct timer_list edid_delay_timer;
	wait_queue_head_t edid_delay_waitq;
	bool edid_delay_active;

	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct drm_connector connector;

	struct tda998x_audio_port audio_port[2];
	struct tda9950_glue cec_glue;
	struct gpio_desc *calib;
	struct cec_notifier *cec_notify;
};

#define conn_to_tda998x_priv(x) \
	container_of(x, struct tda998x_priv, connector)
#define enc_to_tda998x_priv(x) \
	container_of(x, struct tda998x_priv, encoder)
#define bridge_to_tda998x_priv(x) \
	container_of(x, struct tda998x_priv, bridge)

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
# define FEAT_POWERDOWN_PREFILT   BIT(0)
# define FEAT_POWERDOWN_CSC       BIT(1)
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
#define REG_CEC_CAL_XOSC_CTRL1    0xf2
# define CEC_CAL_XOSC_CTRL1_ENA_CAL	BIT(0)
#define REG_CEC_DES_FREQ2         0xf5
# define CEC_DES_FREQ2_DIS_AUTOCAL BIT(7)
#define REG_CEC_CLK               0xf6
# define CEC_CLK_FRO              0x11
#define REG_CEC_FRO_IM_CLK_CTRL   0xfb                /* read/write */
# define CEC_FRO_IM_CLK_CTRL_GHOST_DIS (1 << 7)
# define CEC_FRO_IM_CLK_CTRL_ENA_OTP   (1 << 6)
# define CEC_FRO_IM_CLK_CTRL_IMCLK_SEL (1 << 1)
# define CEC_FRO_IM_CLK_CTRL_FRO_DIV   (1 << 0)
#define REG_CEC_RXSHPDINTENA	  0xfc		      /* read/write */
#define REG_CEC_RXSHPDINT	  0xfd		      /* read */
# define CEC_RXSHPDINT_RXSENS     BIT(0)
# define CEC_RXSHPDINT_HPD        BIT(1)
#define REG_CEC_RXSHPDLEV         0xfe                /* read */
# define CEC_RXSHPDLEV_RXSENS     (1 << 0)
# define CEC_RXSHPDLEV_HPD        (1 << 1)

#define REG_CEC_ENAMODS           0xff                /* read/write */
# define CEC_ENAMODS_EN_CEC_CLK   (1 << 7)
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
cec_write(struct tda998x_priv *priv, u16 addr, u8 val)
{
	u8 buf[] = {addr, val};
	struct i2c_msg msg = {
		.addr = priv->cec_addr,
		.len = 2,
		.buf = buf,
	};
	int ret;

	ret = i2c_transfer(priv->hdmi->adapter, &msg, 1);
	if (ret < 0)
		dev_err(&priv->hdmi->dev, "Error %d writing to cec:0x%x\n",
			ret, addr);
}

static u8
cec_read(struct tda998x_priv *priv, u8 addr)
{
	u8 val;
	struct i2c_msg msg[2] = {
		{
			.addr = priv->cec_addr,
			.len = 1,
			.buf = &addr,
		}, {
			.addr = priv->cec_addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = &val,
		},
	};
	int ret;

	ret = i2c_transfer(priv->hdmi->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0) {
		dev_err(&priv->hdmi->dev, "Error %d reading from cec:0x%x\n",
			ret, addr);
		val = 0;
	}

	return val;
}

static void cec_enamods(struct tda998x_priv *priv, u8 mods, bool enable)
{
	int val = cec_read(priv, REG_CEC_ENAMODS);

	if (val < 0)
		return;

	if (enable)
		val |= mods;
	else
		val &= ~mods;

	cec_write(priv, REG_CEC_ENAMODS, val);
}

static void tda998x_cec_set_calibration(struct tda998x_priv *priv, bool enable)
{
	if (enable) {
		u8 val;

		cec_write(priv, 0xf3, 0xc0);
		cec_write(priv, 0xf4, 0xd4);

		/* Enable automatic calibration mode */
		val = cec_read(priv, REG_CEC_DES_FREQ2);
		val &= ~CEC_DES_FREQ2_DIS_AUTOCAL;
		cec_write(priv, REG_CEC_DES_FREQ2, val);

		/* Enable free running oscillator */
		cec_write(priv, REG_CEC_CLK, CEC_CLK_FRO);
		cec_enamods(priv, CEC_ENAMODS_DIS_FRO, false);

		cec_write(priv, REG_CEC_CAL_XOSC_CTRL1,
			  CEC_CAL_XOSC_CTRL1_ENA_CAL);
	} else {
		cec_write(priv, REG_CEC_CAL_XOSC_CTRL1, 0);
	}
}

/*
 * Calibration for the internal oscillator: we need to set calibration mode,
 * and then pulse the IRQ line low for a 10ms ± 1% period.
 */
static void tda998x_cec_calibration(struct tda998x_priv *priv)
{
	struct gpio_desc *calib = priv->calib;

	mutex_lock(&priv->edid_mutex);
	if (priv->hdmi->irq > 0)
		disable_irq(priv->hdmi->irq);
	gpiod_direction_output(calib, 1);
	tda998x_cec_set_calibration(priv, true);

	local_irq_disable();
	gpiod_set_value(calib, 0);
	mdelay(10);
	gpiod_set_value(calib, 1);
	local_irq_enable();

	tda998x_cec_set_calibration(priv, false);
	gpiod_direction_input(calib);
	if (priv->hdmi->irq > 0)
		enable_irq(priv->hdmi->irq);
	mutex_unlock(&priv->edid_mutex);
}

static int tda998x_cec_hook_init(void *data)
{
	struct tda998x_priv *priv = data;
	struct gpio_desc *calib;

	calib = gpiod_get(&priv->hdmi->dev, "nxp,calib", GPIOD_ASIS);
	if (IS_ERR(calib)) {
		dev_warn(&priv->hdmi->dev, "failed to get calibration gpio: %ld\n",
			 PTR_ERR(calib));
		return PTR_ERR(calib);
	}

	priv->calib = calib;

	return 0;
}

static void tda998x_cec_hook_exit(void *data)
{
	struct tda998x_priv *priv = data;

	gpiod_put(priv->calib);
	priv->calib = NULL;
}

static int tda998x_cec_hook_open(void *data)
{
	struct tda998x_priv *priv = data;

	cec_enamods(priv, CEC_ENAMODS_EN_CEC_CLK | CEC_ENAMODS_EN_CEC, true);
	tda998x_cec_calibration(priv);

	return 0;
}

static void tda998x_cec_hook_release(void *data)
{
	struct tda998x_priv *priv = data;

	cec_enamods(priv, CEC_ENAMODS_EN_CEC_CLK | CEC_ENAMODS_EN_CEC, false);
}

static int
set_page(struct tda998x_priv *priv, u16 reg)
{
	if (REG2PAGE(reg) != priv->current_page) {
		struct i2c_client *client = priv->hdmi;
		u8 buf[] = {
				REG_CURPAGE, REG2PAGE(reg)
		};
		int ret = i2c_master_send(client, buf, sizeof(buf));
		if (ret < 0) {
			dev_err(&client->dev, "%s %04x err %d\n", __func__,
					reg, ret);
			return ret;
		}

		priv->current_page = REG2PAGE(reg);
	}
	return 0;
}

static int
reg_read_range(struct tda998x_priv *priv, u16 reg, char *buf, int cnt)
{
	struct i2c_client *client = priv->hdmi;
	u8 addr = REG2ADDR(reg);
	int ret;

	mutex_lock(&priv->mutex);
	ret = set_page(priv, reg);
	if (ret < 0)
		goto out;

	ret = i2c_master_send(client, &addr, sizeof(addr));
	if (ret < 0)
		goto fail;

	ret = i2c_master_recv(client, buf, cnt);
	if (ret < 0)
		goto fail;

	goto out;

fail:
	dev_err(&client->dev, "Error %d reading from 0x%x\n", ret, reg);
out:
	mutex_unlock(&priv->mutex);
	return ret;
}

#define MAX_WRITE_RANGE_BUF 32

static void
reg_write_range(struct tda998x_priv *priv, u16 reg, u8 *p, int cnt)
{
	struct i2c_client *client = priv->hdmi;
	/* This is the maximum size of the buffer passed in */
	u8 buf[MAX_WRITE_RANGE_BUF + 1];
	int ret;

	if (cnt > MAX_WRITE_RANGE_BUF) {
		dev_err(&client->dev, "Fixed write buffer too small (%d)\n",
				MAX_WRITE_RANGE_BUF);
		return;
	}

	buf[0] = REG2ADDR(reg);
	memcpy(&buf[1], p, cnt);

	mutex_lock(&priv->mutex);
	ret = set_page(priv, reg);
	if (ret < 0)
		goto out;

	ret = i2c_master_send(client, buf, cnt + 1);
	if (ret < 0)
		dev_err(&client->dev, "Error %d writing to 0x%x\n", ret, reg);
out:
	mutex_unlock(&priv->mutex);
}

static int
reg_read(struct tda998x_priv *priv, u16 reg)
{
	u8 val = 0;
	int ret;

	ret = reg_read_range(priv, reg, &val, sizeof(val));
	if (ret < 0)
		return ret;
	return val;
}

static void
reg_write(struct tda998x_priv *priv, u16 reg, u8 val)
{
	struct i2c_client *client = priv->hdmi;
	u8 buf[] = {REG2ADDR(reg), val};
	int ret;

	mutex_lock(&priv->mutex);
	ret = set_page(priv, reg);
	if (ret < 0)
		goto out;

	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret < 0)
		dev_err(&client->dev, "Error %d writing to 0x%x\n", ret, reg);
out:
	mutex_unlock(&priv->mutex);
}

static void
reg_write16(struct tda998x_priv *priv, u16 reg, u16 val)
{
	struct i2c_client *client = priv->hdmi;
	u8 buf[] = {REG2ADDR(reg), val >> 8, val};
	int ret;

	mutex_lock(&priv->mutex);
	ret = set_page(priv, reg);
	if (ret < 0)
		goto out;

	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret < 0)
		dev_err(&client->dev, "Error %d writing to 0x%x\n", ret, reg);
out:
	mutex_unlock(&priv->mutex);
}

static void
reg_set(struct tda998x_priv *priv, u16 reg, u8 val)
{
	int old_val;

	old_val = reg_read(priv, reg);
	if (old_val >= 0)
		reg_write(priv, reg, old_val | val);
}

static void
reg_clear(struct tda998x_priv *priv, u16 reg, u8 val)
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
 * The TDA998x has a problem when trying to read the EDID close to a
 * HPD assertion: it needs a delay of 100ms to avoid timing out while
 * trying to read EDID data.
 *
 * However, tda998x_connector_get_modes() may be called at any moment
 * after tda998x_connector_detect() indicates that we are connected, so
 * we need to delay probing modes in tda998x_connector_get_modes() after
 * we have seen a HPD inactive->active transition.  This code implements
 * that delay.
 */
static void tda998x_edid_delay_done(struct timer_list *t)
{
	struct tda998x_priv *priv = from_timer(priv, t, edid_delay_timer);

	priv->edid_delay_active = false;
	wake_up(&priv->edid_delay_waitq);
	schedule_work(&priv->detect_work);
}

static void tda998x_edid_delay_start(struct tda998x_priv *priv)
{
	priv->edid_delay_active = true;
	mod_timer(&priv->edid_delay_timer, jiffies + HZ/10);
}

static int tda998x_edid_delay_wait(struct tda998x_priv *priv)
{
	return wait_event_killable(priv->edid_delay_waitq, !priv->edid_delay_active);
}

/*
 * We need to run the KMS hotplug event helper outside of our threaded
 * interrupt routine as this can call back into our get_modes method,
 * which will want to make use of interrupts.
 */
static void tda998x_detect_work(struct work_struct *work)
{
	struct tda998x_priv *priv =
		container_of(work, struct tda998x_priv, detect_work);
	struct drm_device *dev = priv->connector.dev;

	if (dev)
		drm_kms_helper_hotplug_event(dev);
}

/*
 * only 2 interrupts may occur: screen plug/unplug and EDID read
 */
static irqreturn_t tda998x_irq_thread(int irq, void *data)
{
	struct tda998x_priv *priv = data;
	u8 sta, cec, lvl, flag0, flag1, flag2;
	bool handled = false;

	sta = cec_read(priv, REG_CEC_INTSTATUS);
	if (sta & CEC_INTSTATUS_HDMI) {
		cec = cec_read(priv, REG_CEC_RXSHPDINT);
		lvl = cec_read(priv, REG_CEC_RXSHPDLEV);
		flag0 = reg_read(priv, REG_INT_FLAGS_0);
		flag1 = reg_read(priv, REG_INT_FLAGS_1);
		flag2 = reg_read(priv, REG_INT_FLAGS_2);
		DRM_DEBUG_DRIVER(
			"tda irq sta %02x cec %02x lvl %02x f0 %02x f1 %02x f2 %02x\n",
			sta, cec, lvl, flag0, flag1, flag2);

		if (cec & CEC_RXSHPDINT_HPD) {
			if (lvl & CEC_RXSHPDLEV_HPD) {
				tda998x_edid_delay_start(priv);
			} else {
				schedule_work(&priv->detect_work);
				cec_notifier_set_phys_addr(priv->cec_notify,
						   CEC_PHYS_ADDR_INVALID);
			}

			handled = true;
		}

		if ((flag2 & INT_FLAGS_2_EDID_BLK_RD) && priv->wq_edid_wait) {
			priv->wq_edid_wait = 0;
			wake_up(&priv->wq_edid);
			handled = true;
		}
	}

	return IRQ_RETVAL(handled);
}

static void
tda998x_write_if(struct tda998x_priv *priv, u8 bit, u16 addr,
		 union hdmi_infoframe *frame)
{
	u8 buf[MAX_WRITE_RANGE_BUF];
	ssize_t len;

	len = hdmi_infoframe_pack(frame, buf, sizeof(buf));
	if (len < 0) {
		dev_err(&priv->hdmi->dev,
			"hdmi_infoframe_pack() type=0x%02x failed: %zd\n",
			frame->any.type, len);
		return;
	}

	reg_clear(priv, REG_DIP_IF_FLAGS, bit);
	reg_write_range(priv, addr, buf, len);
	reg_set(priv, REG_DIP_IF_FLAGS, bit);
}

static int tda998x_write_aif(struct tda998x_priv *priv,
			     struct hdmi_audio_infoframe *cea)
{
	union hdmi_infoframe frame;

	frame.audio = *cea;

	tda998x_write_if(priv, DIP_IF_FLAGS_IF4, REG_IF4_HB0, &frame);

	return 0;
}

static void
tda998x_write_avi(struct tda998x_priv *priv, const struct drm_display_mode *mode)
{
	union hdmi_infoframe frame;

	drm_hdmi_avi_infoframe_from_display_mode(&frame.avi, mode, false);
	frame.avi.quantization_range = HDMI_QUANTIZATION_RANGE_FULL;

	tda998x_write_if(priv, DIP_IF_FLAGS_IF2, REG_IF2_HB0, &frame);
}

/* Audio support */

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

static int
tda998x_configure_audio(struct tda998x_priv *priv,
			struct tda998x_audio_params *params)
{
	u8 buf[6], clksel_aip, clksel_fs, cts_n, adiv;
	u32 n;

	/* Enable audio ports */
	reg_write(priv, REG_ENA_AP, params->config);

	/* Set audio input source */
	switch (params->format) {
	case AFMT_SPDIF:
		reg_write(priv, REG_ENA_ACLK, 0);
		reg_write(priv, REG_MUX_AP, MUX_AP_SELECT_SPDIF);
		clksel_aip = AIP_CLKSEL_AIP_SPDIF;
		clksel_fs = AIP_CLKSEL_FS_FS64SPDIF;
		cts_n = CTS_N_M(3) | CTS_N_K(3);
		break;

	case AFMT_I2S:
		reg_write(priv, REG_ENA_ACLK, 1);
		reg_write(priv, REG_MUX_AP, MUX_AP_SELECT_I2S);
		clksel_aip = AIP_CLKSEL_AIP_I2S;
		clksel_fs = AIP_CLKSEL_FS_ACLK;
		switch (params->sample_width) {
		case 16:
			cts_n = CTS_N_M(3) | CTS_N_K(1);
			break;
		case 18:
		case 20:
		case 24:
			cts_n = CTS_N_M(3) | CTS_N_K(2);
			break;
		default:
		case 32:
			cts_n = CTS_N_M(3) | CTS_N_K(3);
			break;
		}
		break;

	default:
		dev_err(&priv->hdmi->dev, "Unsupported I2S format\n");
		return -EINVAL;
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
	if (priv->tmds_clock > 100000)
		adiv++;			/* AUDIO_DIV_SERCLK_16 */

	/* S/PDIF asks for a larger divider */
	if (params->format == AFMT_SPDIF)
		adiv++;			/* AUDIO_DIV_SERCLK_16 or _32 */

	reg_write(priv, REG_AUDIO_DIV, adiv);

	/*
	 * This is the approximate value of N, which happens to be
	 * the recommended values for non-coherent clocks.
	 */
	n = 128 * params->sample_rate / 1000;

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

	/* Write the channel status
	 * The REG_CH_STAT_B-registers skip IEC958 AES2 byte, because
	 * there is a separate register for each I2S wire.
	 */
	buf[0] = params->status[0];
	buf[1] = params->status[1];
	buf[2] = params->status[3];
	buf[3] = params->status[4];
	reg_write_range(priv, REG_CH_STAT_B(0), buf, 4);

	tda998x_audio_mute(priv, true);
	msleep(20);
	tda998x_audio_mute(priv, false);

	return tda998x_write_aif(priv, &params->cea);
}

static int tda998x_audio_hw_params(struct device *dev, void *data,
				   struct hdmi_codec_daifmt *daifmt,
				   struct hdmi_codec_params *params)
{
	struct tda998x_priv *priv = dev_get_drvdata(dev);
	int i, ret;
	struct tda998x_audio_params audio = {
		.sample_width = params->sample_width,
		.sample_rate = params->sample_rate,
		.cea = params->cea,
	};

	memcpy(audio.status, params->iec.status,
	       min(sizeof(audio.status), sizeof(params->iec.status)));

	switch (daifmt->fmt) {
	case HDMI_I2S:
		if (daifmt->bit_clk_inv || daifmt->frame_clk_inv ||
		    daifmt->bit_clk_master || daifmt->frame_clk_master) {
			dev_err(dev, "%s: Bad flags %d %d %d %d\n", __func__,
				daifmt->bit_clk_inv, daifmt->frame_clk_inv,
				daifmt->bit_clk_master,
				daifmt->frame_clk_master);
			return -EINVAL;
		}
		for (i = 0; i < ARRAY_SIZE(priv->audio_port); i++)
			if (priv->audio_port[i].format == AFMT_I2S)
				audio.config = priv->audio_port[i].config;
		audio.format = AFMT_I2S;
		break;
	case HDMI_SPDIF:
		for (i = 0; i < ARRAY_SIZE(priv->audio_port); i++)
			if (priv->audio_port[i].format == AFMT_SPDIF)
				audio.config = priv->audio_port[i].config;
		audio.format = AFMT_SPDIF;
		break;
	default:
		dev_err(dev, "%s: Invalid format %d\n", __func__, daifmt->fmt);
		return -EINVAL;
	}

	if (audio.config == 0) {
		dev_err(dev, "%s: No audio configuration found\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&priv->audio_mutex);
	if (priv->supports_infoframes && priv->sink_has_audio)
		ret = tda998x_configure_audio(priv, &audio);
	else
		ret = 0;

	if (ret == 0)
		priv->audio_params = audio;
	mutex_unlock(&priv->audio_mutex);

	return ret;
}

static void tda998x_audio_shutdown(struct device *dev, void *data)
{
	struct tda998x_priv *priv = dev_get_drvdata(dev);

	mutex_lock(&priv->audio_mutex);

	reg_write(priv, REG_ENA_AP, 0);

	priv->audio_params.format = AFMT_UNUSED;

	mutex_unlock(&priv->audio_mutex);
}

int tda998x_audio_digital_mute(struct device *dev, void *data, bool enable)
{
	struct tda998x_priv *priv = dev_get_drvdata(dev);

	mutex_lock(&priv->audio_mutex);

	tda998x_audio_mute(priv, enable);

	mutex_unlock(&priv->audio_mutex);
	return 0;
}

static int tda998x_audio_get_eld(struct device *dev, void *data,
				 uint8_t *buf, size_t len)
{
	struct tda998x_priv *priv = dev_get_drvdata(dev);

	mutex_lock(&priv->audio_mutex);
	memcpy(buf, priv->connector.eld,
	       min(sizeof(priv->connector.eld), len));
	mutex_unlock(&priv->audio_mutex);

	return 0;
}

static const struct hdmi_codec_ops audio_codec_ops = {
	.hw_params = tda998x_audio_hw_params,
	.audio_shutdown = tda998x_audio_shutdown,
	.digital_mute = tda998x_audio_digital_mute,
	.get_eld = tda998x_audio_get_eld,
};

static int tda998x_audio_codec_init(struct tda998x_priv *priv,
				    struct device *dev)
{
	struct hdmi_codec_pdata codec_data = {
		.ops = &audio_codec_ops,
		.max_i2s_channels = 2,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->audio_port); i++) {
		if (priv->audio_port[i].format == AFMT_I2S &&
		    priv->audio_port[i].config != 0)
			codec_data.i2s = 1;
		if (priv->audio_port[i].format == AFMT_SPDIF &&
		    priv->audio_port[i].config != 0)
			codec_data.spdif = 1;
	}

	priv->audio_pdev = platform_device_register_data(
		dev, HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_AUTO,
		&codec_data, sizeof(codec_data));

	return PTR_ERR_OR_ZERO(priv->audio_pdev);
}

/* DRM connector functions */

static enum drm_connector_status
tda998x_connector_detect(struct drm_connector *connector, bool force)
{
	struct tda998x_priv *priv = conn_to_tda998x_priv(connector);
	u8 val = cec_read(priv, REG_CEC_RXSHPDLEV);

	return (val & CEC_RXSHPDLEV_HPD) ? connector_status_connected :
			connector_status_disconnected;
}

static void tda998x_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs tda998x_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = tda998x_connector_detect,
	.destroy = tda998x_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int read_edid_block(void *data, u8 *buf, unsigned int blk, size_t length)
{
	struct tda998x_priv *priv = data;
	u8 offset, segptr;
	int ret, i;

	offset = (blk & 1) ? 128 : 0;
	segptr = blk / 2;

	mutex_lock(&priv->edid_mutex);

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
			ret = i;
			goto failed;
		}
	} else {
		for (i = 100; i > 0; i--) {
			msleep(1);
			ret = reg_read(priv, REG_INT_FLAGS_2);
			if (ret < 0)
				goto failed;
			if (ret & INT_FLAGS_2_EDID_BLK_RD)
				break;
		}
	}

	if (i == 0) {
		dev_err(&priv->hdmi->dev, "read edid timeout\n");
		ret = -ETIMEDOUT;
		goto failed;
	}

	ret = reg_read_range(priv, REG_EDID_DATA_0, buf, length);
	if (ret != length) {
		dev_err(&priv->hdmi->dev, "failed to read edid block %d: %d\n",
			blk, ret);
		goto failed;
	}

	ret = 0;

 failed:
	mutex_unlock(&priv->edid_mutex);
	return ret;
}

static int tda998x_connector_get_modes(struct drm_connector *connector)
{
	struct tda998x_priv *priv = conn_to_tda998x_priv(connector);
	struct edid *edid;
	int n;

	/*
	 * If we get killed while waiting for the HPD timeout, return
	 * no modes found: we are not in a restartable path, so we
	 * can't handle signals gracefully.
	 */
	if (tda998x_edid_delay_wait(priv))
		return 0;

	if (priv->rev == TDA19988)
		reg_clear(priv, REG_TX4, TX4_PD_RAM);

	edid = drm_do_get_edid(connector, read_edid_block, priv);

	if (priv->rev == TDA19988)
		reg_set(priv, REG_TX4, TX4_PD_RAM);

	if (!edid) {
		dev_warn(&priv->hdmi->dev, "failed to read EDID\n");
		return 0;
	}

	drm_connector_update_edid_property(connector, edid);
	cec_notifier_set_phys_addr_from_edid(priv->cec_notify, edid);

	mutex_lock(&priv->audio_mutex);
	n = drm_add_edid_modes(connector, edid);
	priv->sink_has_audio = drm_detect_monitor_audio(edid);
	mutex_unlock(&priv->audio_mutex);

	kfree(edid);

	return n;
}

static struct drm_encoder *
tda998x_connector_best_encoder(struct drm_connector *connector)
{
	struct tda998x_priv *priv = conn_to_tda998x_priv(connector);

	return priv->bridge.encoder;
}

static
const struct drm_connector_helper_funcs tda998x_connector_helper_funcs = {
	.get_modes = tda998x_connector_get_modes,
	.best_encoder = tda998x_connector_best_encoder,
};

static int tda998x_connector_init(struct tda998x_priv *priv,
				  struct drm_device *drm)
{
	struct drm_connector *connector = &priv->connector;
	int ret;

	connector->interlace_allowed = 1;

	if (priv->hdmi->irq)
		connector->polled = DRM_CONNECTOR_POLL_HPD;
	else
		connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			DRM_CONNECTOR_POLL_DISCONNECT;

	drm_connector_helper_add(connector, &tda998x_connector_helper_funcs);
	ret = drm_connector_init(drm, connector, &tda998x_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;

	drm_connector_attach_encoder(&priv->connector,
				     priv->bridge.encoder);

	return 0;
}

/* DRM bridge functions */

static int tda998x_bridge_attach(struct drm_bridge *bridge)
{
	struct tda998x_priv *priv = bridge_to_tda998x_priv(bridge);

	return tda998x_connector_init(priv, bridge->dev);
}

static void tda998x_bridge_detach(struct drm_bridge *bridge)
{
	struct tda998x_priv *priv = bridge_to_tda998x_priv(bridge);

	drm_connector_cleanup(&priv->connector);
}

static enum drm_mode_status tda998x_bridge_mode_valid(struct drm_bridge *bridge,
				     const struct drm_display_mode *mode)
{
	/* TDA19988 dotclock can go up to 165MHz */
	struct tda998x_priv *priv = bridge_to_tda998x_priv(bridge);

	if (mode->clock > ((priv->rev == TDA19988) ? 165000 : 150000))
		return MODE_CLOCK_HIGH;
	if (mode->htotal >= BIT(13))
		return MODE_BAD_HVALUE;
	if (mode->vtotal >= BIT(11))
		return MODE_BAD_VVALUE;
	return MODE_OK;
}

static void tda998x_bridge_enable(struct drm_bridge *bridge)
{
	struct tda998x_priv *priv = bridge_to_tda998x_priv(bridge);

	if (!priv->is_on) {
		/* enable video ports, audio will be enabled later */
		reg_write(priv, REG_ENA_VP_0, 0xff);
		reg_write(priv, REG_ENA_VP_1, 0xff);
		reg_write(priv, REG_ENA_VP_2, 0xff);
		/* set muxing after enabling ports: */
		reg_write(priv, REG_VIP_CNTRL_0, priv->vip_cntrl_0);
		reg_write(priv, REG_VIP_CNTRL_1, priv->vip_cntrl_1);
		reg_write(priv, REG_VIP_CNTRL_2, priv->vip_cntrl_2);

		priv->is_on = true;
	}
}

static void tda998x_bridge_disable(struct drm_bridge *bridge)
{
	struct tda998x_priv *priv = bridge_to_tda998x_priv(bridge);

	if (priv->is_on) {
		/* disable video ports */
		reg_write(priv, REG_ENA_VP_0, 0x00);
		reg_write(priv, REG_ENA_VP_1, 0x00);
		reg_write(priv, REG_ENA_VP_2, 0x00);

		priv->is_on = false;
	}
}

static void tda998x_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *mode,
				    const struct drm_display_mode *adjusted_mode)
{
	struct tda998x_priv *priv = bridge_to_tda998x_priv(bridge);
	unsigned long tmds_clock;
	u16 ref_pix, ref_line, n_pix, n_line;
	u16 hs_pix_s, hs_pix_e;
	u16 vs1_pix_s, vs1_pix_e, vs1_line_s, vs1_line_e;
	u16 vs2_pix_s, vs2_pix_e, vs2_line_s, vs2_line_e;
	u16 vwin1_line_s, vwin1_line_e;
	u16 vwin2_line_s, vwin2_line_e;
	u16 de_pix_s, de_pix_e;
	u8 reg, div, rep;

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

	tmds_clock = mode->clock;

	/*
	 * The divisor is power-of-2. The TDA9983B datasheet gives
	 * this as ranges of Msample/s, which is 10x the TMDS clock:
	 *   0 - 800 to 1500 Msample/s
	 *   1 - 400 to 800 Msample/s
	 *   2 - 200 to 400 Msample/s
	 *   3 - as 2 above
	 */
	for (div = 0; div < 3; div++)
		if (80000 >> div <= tmds_clock)
			break;

	mutex_lock(&priv->audio_mutex);

	/* mute the audio FIFO: */
	reg_set(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_FIFO);

	/* set HDMI HDCP mode off: */
	reg_write(priv, REG_TBG_CNTRL_1, TBG_CNTRL_1_DWIN_DIS);
	reg_clear(priv, REG_TX33, TX33_HDMI);
	reg_write(priv, REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(0));

	/* no pre-filter or interpolator: */
	reg_write(priv, REG_HVF_CNTRL_0, HVF_CNTRL_0_PREFIL(0) |
			HVF_CNTRL_0_INTPOL(0));
	reg_set(priv, REG_FEAT_POWERDOWN, FEAT_POWERDOWN_PREFILT);
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
	reg_set(priv, REG_FEAT_POWERDOWN, FEAT_POWERDOWN_CSC);

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

	priv->tmds_clock = adjusted_mode->clock;

	/* CEA-861B section 6 says that:
	 * CEA version 1 (CEA-861) has no support for infoframes.
	 * CEA version 2 (CEA-861A) supports version 1 AVI infoframes,
	 * and optional basic audio.
	 * CEA version 3 (CEA-861B) supports version 1 and 2 AVI infoframes,
	 * and optional digital audio, with audio infoframes.
	 *
	 * Since we only support generation of version 2 AVI infoframes,
	 * ignore CEA version 2 and below (iow, behave as if we're a
	 * CEA-861 source.)
	 */
	priv->supports_infoframes = priv->connector.display_info.cea_rev >= 3;

	if (priv->supports_infoframes) {
		/* We need to turn HDMI HDCP stuff on to get audio through */
		reg &= ~TBG_CNTRL_1_DWIN_DIS;
		reg_write(priv, REG_TBG_CNTRL_1, reg);
		reg_write(priv, REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(1));
		reg_set(priv, REG_TX33, TX33_HDMI);

		tda998x_write_avi(priv, adjusted_mode);

		if (priv->audio_params.format != AFMT_UNUSED &&
		    priv->sink_has_audio)
			tda998x_configure_audio(priv, &priv->audio_params);
	}

	mutex_unlock(&priv->audio_mutex);
}

static const struct drm_bridge_funcs tda998x_bridge_funcs = {
	.attach = tda998x_bridge_attach,
	.detach = tda998x_bridge_detach,
	.mode_valid = tda998x_bridge_mode_valid,
	.disable = tda998x_bridge_disable,
	.mode_set = tda998x_bridge_mode_set,
	.enable = tda998x_bridge_enable,
};

/* I2C driver functions */

static int tda998x_get_audio_ports(struct tda998x_priv *priv,
				   struct device_node *np)
{
	const u32 *port_data;
	u32 size;
	int i;

	port_data = of_get_property(np, "audio-ports", &size);
	if (!port_data)
		return 0;

	size /= sizeof(u32);
	if (size > 2 * ARRAY_SIZE(priv->audio_port) || size % 2 != 0) {
		dev_err(&priv->hdmi->dev,
			"Bad number of elements in audio-ports dt-property\n");
		return -EINVAL;
	}

	size /= 2;

	for (i = 0; i < size; i++) {
		u8 afmt = be32_to_cpup(&port_data[2*i]);
		u8 ena_ap = be32_to_cpup(&port_data[2*i+1]);

		if (afmt != AFMT_SPDIF && afmt != AFMT_I2S) {
			dev_err(&priv->hdmi->dev,
				"Bad audio format %u\n", afmt);
			return -EINVAL;
		}

		priv->audio_port[i].format = afmt;
		priv->audio_port[i].config = ena_ap;
	}

	if (priv->audio_port[0].format == priv->audio_port[1].format) {
		dev_err(&priv->hdmi->dev,
			"There can only be on I2S port and one SPDIF port\n");
		return -EINVAL;
	}
	return 0;
}

static void tda998x_set_config(struct tda998x_priv *priv,
			       const struct tda998x_encoder_params *p)
{
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

	priv->audio_params = p->audio_params;
}

static void tda998x_destroy(struct device *dev)
{
	struct tda998x_priv *priv = dev_get_drvdata(dev);

	drm_bridge_remove(&priv->bridge);

	/* disable all IRQs and free the IRQ handler */
	cec_write(priv, REG_CEC_RXSHPDINTENA, 0);
	reg_clear(priv, REG_INT_FLAGS_2, INT_FLAGS_2_EDID_BLK_RD);

	if (priv->audio_pdev)
		platform_device_unregister(priv->audio_pdev);

	if (priv->hdmi->irq)
		free_irq(priv->hdmi->irq, priv);

	del_timer_sync(&priv->edid_delay_timer);
	cancel_work_sync(&priv->detect_work);

	i2c_unregister_device(priv->cec);

	if (priv->cec_notify)
		cec_notifier_put(priv->cec_notify);
}

static int tda998x_create(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct device_node *np = client->dev.of_node;
	struct i2c_board_info cec_info;
	struct tda998x_priv *priv;
	u32 video;
	int rev_lo, rev_hi, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);

	mutex_init(&priv->mutex);	/* protect the page access */
	mutex_init(&priv->audio_mutex); /* protect access from audio thread */
	mutex_init(&priv->edid_mutex);
	INIT_LIST_HEAD(&priv->bridge.list);
	init_waitqueue_head(&priv->edid_delay_waitq);
	timer_setup(&priv->edid_delay_timer, tda998x_edid_delay_done, 0);
	INIT_WORK(&priv->detect_work, tda998x_detect_work);

	priv->vip_cntrl_0 = VIP_CNTRL_0_SWAP_A(2) | VIP_CNTRL_0_SWAP_B(3);
	priv->vip_cntrl_1 = VIP_CNTRL_1_SWAP_C(0) | VIP_CNTRL_1_SWAP_D(1);
	priv->vip_cntrl_2 = VIP_CNTRL_2_SWAP_E(4) | VIP_CNTRL_2_SWAP_F(5);

	/* CEC I2C address bound to TDA998x I2C addr by configuration pins */
	priv->cec_addr = 0x34 + (client->addr & 0x03);
	priv->current_page = 0xff;
	priv->hdmi = client;

	/* wake up the device: */
	cec_write(priv, REG_CEC_ENAMODS,
			CEC_ENAMODS_EN_RXSENS | CEC_ENAMODS_EN_HDMI);

	tda998x_reset(priv);

	/* read version: */
	rev_lo = reg_read(priv, REG_VERSION_LSB);
	if (rev_lo < 0) {
		dev_err(dev, "failed to read version: %d\n", rev_lo);
		return rev_lo;
	}

	rev_hi = reg_read(priv, REG_VERSION_MSB);
	if (rev_hi < 0) {
		dev_err(dev, "failed to read version: %d\n", rev_hi);
		return rev_hi;
	}

	priv->rev = rev_lo | rev_hi << 8;

	/* mask off feature bits: */
	priv->rev &= ~0x30; /* not-hdcp and not-scalar bit */

	switch (priv->rev) {
	case TDA9989N2:
		dev_info(dev, "found TDA9989 n2");
		break;
	case TDA19989:
		dev_info(dev, "found TDA19989");
		break;
	case TDA19989N2:
		dev_info(dev, "found TDA19989 n2");
		break;
	case TDA19988:
		dev_info(dev, "found TDA19988");
		break;
	default:
		dev_err(dev, "found unsupported device: %04x\n", priv->rev);
		return -ENXIO;
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

	/* ensure interrupts are disabled */
	cec_write(priv, REG_CEC_RXSHPDINTENA, 0);

	/* clear pending interrupts */
	cec_read(priv, REG_CEC_RXSHPDINT);
	reg_read(priv, REG_INT_FLAGS_0);
	reg_read(priv, REG_INT_FLAGS_1);
	reg_read(priv, REG_INT_FLAGS_2);

	/* initialize the optional IRQ */
	if (client->irq) {
		unsigned long irq_flags;

		/* init read EDID waitqueue and HDP work */
		init_waitqueue_head(&priv->wq_edid);

		irq_flags =
			irqd_get_trigger_type(irq_get_irq_data(client->irq));

		priv->cec_glue.irq_flags = irq_flags;

		irq_flags |= IRQF_SHARED | IRQF_ONESHOT;
		ret = request_threaded_irq(client->irq, NULL,
					   tda998x_irq_thread, irq_flags,
					   "tda998x", priv);
		if (ret) {
			dev_err(dev, "failed to request IRQ#%u: %d\n",
				client->irq, ret);
			goto err_irq;
		}

		/* enable HPD irq */
		cec_write(priv, REG_CEC_RXSHPDINTENA, CEC_RXSHPDLEV_HPD);
	}

	priv->cec_notify = cec_notifier_get(dev);
	if (!priv->cec_notify) {
		ret = -ENOMEM;
		goto fail;
	}

	priv->cec_glue.parent = dev;
	priv->cec_glue.data = priv;
	priv->cec_glue.init = tda998x_cec_hook_init;
	priv->cec_glue.exit = tda998x_cec_hook_exit;
	priv->cec_glue.open = tda998x_cec_hook_open;
	priv->cec_glue.release = tda998x_cec_hook_release;

	/*
	 * Some TDA998x are actually two I2C devices merged onto one piece
	 * of silicon: TDA9989 and TDA19989 combine the HDMI transmitter
	 * with a slightly modified TDA9950 CEC device.  The CEC device
	 * is at the TDA9950 address, with the address pins strapped across
	 * to the TDA998x address pins.  Hence, it always has the same
	 * offset.
	 */
	memset(&cec_info, 0, sizeof(cec_info));
	strlcpy(cec_info.type, "tda9950", sizeof(cec_info.type));
	cec_info.addr = priv->cec_addr;
	cec_info.platform_data = &priv->cec_glue;
	cec_info.irq = client->irq;

	priv->cec = i2c_new_device(client->adapter, &cec_info);
	if (!priv->cec) {
		ret = -ENODEV;
		goto fail;
	}

	/* enable EDID read irq: */
	reg_set(priv, REG_INT_FLAGS_2, INT_FLAGS_2_EDID_BLK_RD);

	if (np) {
		/* get the device tree parameters */
		ret = of_property_read_u32(np, "video-ports", &video);
		if (ret == 0) {
			priv->vip_cntrl_0 = video >> 16;
			priv->vip_cntrl_1 = video >> 8;
			priv->vip_cntrl_2 = video;
		}

		ret = tda998x_get_audio_ports(priv, np);
		if (ret)
			goto fail;

		if (priv->audio_port[0].format != AFMT_UNUSED)
			tda998x_audio_codec_init(priv, &client->dev);
	} else if (dev->platform_data) {
		tda998x_set_config(priv, dev->platform_data);
	}

	priv->bridge.funcs = &tda998x_bridge_funcs;
#ifdef CONFIG_OF
	priv->bridge.of_node = dev->of_node;
#endif

	drm_bridge_add(&priv->bridge);

	return 0;

fail:
	tda998x_destroy(dev);
err_irq:
	return ret;
}

/* DRM encoder functions */

static void tda998x_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs tda998x_encoder_funcs = {
	.destroy = tda998x_encoder_destroy,
};

static int tda998x_encoder_init(struct device *dev, struct drm_device *drm)
{
	struct tda998x_priv *priv = dev_get_drvdata(dev);
	u32 crtcs = 0;
	int ret;

	if (dev->of_node)
		crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);

	/* If no CRTCs were found, fall back to our old behaviour */
	if (crtcs == 0) {
		dev_warn(dev, "Falling back to first CRTC\n");
		crtcs = 1 << 0;
	}

	priv->encoder.possible_crtcs = crtcs;

	ret = drm_encoder_init(drm, &priv->encoder, &tda998x_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		goto err_encoder;

	ret = drm_bridge_attach(&priv->encoder, &priv->bridge, NULL);
	if (ret)
		goto err_bridge;

	return 0;

err_bridge:
	drm_encoder_cleanup(&priv->encoder);
err_encoder:
	return ret;
}

static int tda998x_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;

	return tda998x_encoder_init(dev, drm);
}

static void tda998x_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct tda998x_priv *priv = dev_get_drvdata(dev);

	drm_encoder_cleanup(&priv->encoder);
}

static const struct component_ops tda998x_ops = {
	.bind = tda998x_bind,
	.unbind = tda998x_unbind,
};

static int
tda998x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_warn(&client->dev, "adapter does not support I2C\n");
		return -EIO;
	}

	ret = tda998x_create(&client->dev);
	if (ret)
		return ret;

	ret = component_add(&client->dev, &tda998x_ops);
	if (ret)
		tda998x_destroy(&client->dev);
	return ret;
}

static int tda998x_remove(struct i2c_client *client)
{
	component_del(&client->dev, &tda998x_ops);
	tda998x_destroy(&client->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id tda998x_dt_ids[] = {
	{ .compatible = "nxp,tda998x", },
	{ }
};
MODULE_DEVICE_TABLE(of, tda998x_dt_ids);
#endif

static const struct i2c_device_id tda998x_ids[] = {
	{ "tda998x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tda998x_ids);

static struct i2c_driver tda998x_driver = {
	.probe = tda998x_probe,
	.remove = tda998x_remove,
	.driver = {
		.name = "tda998x",
		.of_match_table = of_match_ptr(tda998x_dt_ids),
	},
	.id_table = tda998x_ids,
};

module_i2c_driver(tda998x_driver);

MODULE_AUTHOR("Rob Clark <robdclark@gmail.com");
MODULE_DESCRIPTION("NXP Semiconductors TDA998X HDMI Encoder");
MODULE_LICENSE("GPL");
