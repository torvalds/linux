/*
 * Panasonic MN86471A DP->HDMI bridge driver (via PS4 Aeolia ICC interface)
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/ps4.h>

#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "drm_atomic_helper.h"
#include "drm_edid.h"
#include "drmP.h"

#include "radeon_mode.h"
#include "ObjectID.h"

#define CMD_READ	1, 1
#define CMD_WRITE	2, 2
#define CMD_MASK	2, 3
#define CMD_DELAY	3, 1
#define CMD_WAIT_SET	3, 2
#define CMD_WAIT_CLEAR	3, 3

#define TSYSCTRL 0x7005
# define TSYSCTRL_HDMI BIT(7)

#define TSRST 0x7006
# define TSRST_AVCSRST BIT(0)
# define TSRST_ENCSRST BIT(1)
# define TSRST_FIFOSRST BIT(2)
# define TSRST_CCSRST BIT(3)
# define TSRST_HDCPSRST BIT(4)
# define TSRST_AUDSRST BIT(6)
# define TSRST_VIFSRST BIT(7)

#define TMONREG 0x7008
# define TMONREG_HPD BIT(3)

#define TDPCMODE 0x7009


#define UPDCTRL 0x7011
# define UPDCTRL_ALLUPD BIT(7)
# define UPDCTRL_AVIIUPD BIT(6)
# define UPDCTRL_AUDIUPD BIT(5)
# define UPDCTRL_CLKUPD BIT(4)
# define UPDCTRL_HVSIUPD BIT(3)
# define UPDCTRL_VIFUPD BIT(2)
# define UPDCTRL_AUDUPD BIT(1)
# define UPDCTRL_CSCUPD BIT(0)


#define VINCNT 0x7040
# define VINCNT_VIF_FILEN BIT(6)

#define VMUTECNT 0x705f
# define VMUTECNT_CCVMUTE BIT(7)
# define VMUTECNT_DUMON BIT(6)
# define VMUTECNT_LINEWIDTH_80 (0<<4)
# define VMUTECNT_LINEWIDTH_90 (1<<4)
# define VMUTECNT_LINEWIDTH_180 (2<<4)
# define VMUTECNT_LINEWIDTH_360 (3<<4)
# define VMUTECNT_VMUTE_MUTE_ASYNC 1
# define VMUTECNT_VMUTE_MUTE_NORMAL 2
# define VMUTECNT_VMUTE_MUTE_RAMPA 4
# define VMUTECNT_VMUTE_MUTE_RAMPB 8
# define VMUTECNT_VMUTE_MUTE_COLORBAR_RGB 10
# define VMUTECNT_VMUTE_MUTE_TOGGLE 12
# define VMUTECNT_VMUTE_MUTE_COLORBAR_YCBCR 14

#define CSCMOD 0x70c0
#define C420SET 0x70c2
#define OUTWSET 0x70c3

#define PKTENA 0x7202

#define INFENA 0x7203
# define INFENA_AVIEN BIT(6)

#define AKESTA 0x7a84
# define AKESTA_BUSY BIT(0)

#define AKESRST 0x7a88

#define HDCPEN 0x7a8b
# define HDCPEN_NONE 0x00
# define HDCPEN_ENC_EN 0x03
# define HDCPEN_ENC_DIS 0x05


struct i2c_cmd_hdr {
	u8 major;
	u8 length;
	u8 minor;
	u8 count;
} __packed;

struct i2c_cmdqueue {
	struct {
		u8 code;
		u16 length;
		u8 count;
		u8 cmdbuf[0x7ec];
	} __packed req;
	struct {
		u8 res1, res2;
		u8 unk1, unk2;
		u8 count;
		u8 databuf[0x7eb];
	} __packed reply;

	u8 *p;
	struct i2c_cmd_hdr *cmd;
};

struct mn86471a_bridge {
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_bridge bridge;
	struct i2c_cmdqueue cq;
	struct mutex mutex;

	int mode;
};

/* this should really be taken care of by the connector, but that is currently
 * contained/owned by radeon_connector so just use a global for now */
static struct mn86471a_bridge g_bridge = {
	.mutex = __MUTEX_INITIALIZER(g_bridge.mutex)
};

static void cq_init(struct i2c_cmdqueue *q, u8 code)
{
	q->req.code = code;
	q->req.count = 0;
	q->p = q->req.cmdbuf;
	q->cmd = NULL;
}

static void cq_cmd(struct i2c_cmdqueue *q, u8 major, u8 minor)
{
	if (!q->cmd || q->cmd->major != major || q->cmd->minor != minor) {
		if (q->cmd)
			q->cmd->length = q->p - (u8 *)q->cmd;
		q->cmd = (struct i2c_cmd_hdr *)q->p;
		q->cmd->major = major;
		q->cmd->minor = minor;
		q->cmd->length = 0;
		q->cmd->count = 1;
		q->req.count += 1;
		q->p += sizeof(*q->cmd);
	} else {
		q->cmd->count += 1;
	}
}

static int cq_exec(struct i2c_cmdqueue *q)
{
	int res;

	if (!q->cmd)
		return 0;

	q->cmd->length = q->p - (u8 *)q->cmd;
	q->req.length = q->p - (u8 *)&q->req;
	
	res = apcie_icc_cmd(0x10, 0, &q->req, q->req.length,
		      &q->reply, sizeof(q->reply));
	
	if (res < 5) {
		DRM_ERROR("icc i2c commandqueue failed: %d\n", res);
		return -EIO;
	}
	if (q->reply.res1 != 0 || q->reply.res2) {
		DRM_ERROR("icc i2c commandqueue failed: %d, %d\n",
			  q->reply.res1, q->reply.res2);
		return -EIO;
	}

	return res;
}

static void cq_read(struct i2c_cmdqueue *q, u16 addr, u8 count)
{
	cq_cmd(q, CMD_READ);
	*q->p++ = count;
	*q->p++ = addr >> 8;
	*q->p++ = addr & 0xff;
	*q->p++ = 0;
}

static void cq_writereg(struct i2c_cmdqueue *q, u16 addr, u8 data)
{
	cq_cmd(q, CMD_WRITE);
	*q->p++ = 1;
	*q->p++ = addr >> 8;
	*q->p++ = addr & 0xff;
	*q->p++ = data;
}

#if 0
static void cq_write(struct i2c_cmdqueue *q, u16 addr, u8 *data, u8 count)
{
	cq_cmd(q, CMD_WRITE);
	*q->p++ = count;
	*q->p++ = addr >> 8;
	*q->p++ = addr & 0xff;
	while (count--)
		*q->p++ = *data++;
}
#endif

static void cq_mask(struct i2c_cmdqueue *q, u16 addr, u8 value, u8 mask)
{
	cq_cmd(q, CMD_MASK);
	*q->p++ = 1;
	*q->p++ = addr >> 8;
	*q->p++ = addr & 0xff;
	*q->p++ = value;
	*q->p++ = mask;
}

#if 0
static void cq_delay(struct i2c_cmdqueue *q, u16 time)
{
	cq_cmd(q, CMD_DELAY);
	*q->p++ = 0;
	*q->p++ = time & 0xff;
	*q->p++ = time>>8;
	*q->p++ = 0;
}
#endif

static void cq_wait_set(struct i2c_cmdqueue *q, u16 addr, u8 mask)
{
	cq_cmd(q, CMD_WAIT_SET);
	*q->p++ = 0;
	*q->p++ = addr >> 8;
	*q->p++ = addr & 0xff;
	*q->p++ = mask;
}

static void cq_wait_clear(struct i2c_cmdqueue *q, u16 addr, u8 mask)
{
	cq_cmd(q, CMD_WAIT_CLEAR);
	*q->p++ = 0;
	*q->p++ = addr >> 8;
	*q->p++ = addr & 0xff;
	*q->p++ = mask;
}

static inline struct mn86471a_bridge *
		bridge_to_mn86471a(struct drm_bridge *bridge)
{
	return container_of(bridge, struct mn86471a_bridge, bridge);
}

static void mn86471a_mode_set(struct drm_bridge *bridge,
			      struct drm_display_mode *mode,
			      struct drm_display_mode *adjusted_mode)
{
	struct mn86471a_bridge *mn_bridge = bridge_to_mn86471a(bridge);
	
	/* This gets called before pre_enable/enable, so we just stash
	 * the vic ID for later */
	mn_bridge->mode = drm_match_cea_mode(adjusted_mode);
	DRM_DEBUG_KMS("vic mode: %d\n", mn_bridge->mode);
	if (!mn_bridge->mode) {
		DRM_ERROR("attempted to set non-CEA mode\n");
	}
}

static void mn86471a_pre_enable(struct drm_bridge *bridge)
{
	struct mn86471a_bridge *mn_bridge = bridge_to_mn86471a(bridge);
	DRM_DEBUG_KMS("mn86471a_pre_enable\n");

	mutex_lock(&mn_bridge->mutex);
	cq_init(&mn_bridge->cq, 4);

#if 0
	/* No idea. DP stuff probably. This borks for some reason. Meh. */
	cq_writereg(&mn_bridge->cq, 0x7657,0xff);
	cq_writereg(&mn_bridge->cq, 0x76a5,0x80);
	cq_writereg(&mn_bridge->cq, 0x76a6,0x04);
	cq_writereg(&mn_bridge->cq, 0x7601,0x0a);
	cq_writereg(&mn_bridge->cq, 0x7602,0x84);
	cq_writereg(&mn_bridge->cq, 0x7603,0x00);
	cq_writereg(&mn_bridge->cq, 0x76a8,0x09);
	cq_writereg(&mn_bridge->cq, 0x76ae,0xd1);
	cq_writereg(&mn_bridge->cq, 0x76af,0x50);
	cq_writereg(&mn_bridge->cq, 0x76b0,0x70);
	cq_writereg(&mn_bridge->cq, 0x76b1,0xb0);
	cq_writereg(&mn_bridge->cq, 0x76b2,0xf0);
	cq_writereg(&mn_bridge->cq, 0x76db,0x00);
	cq_writereg(&mn_bridge->cq, 0x76dc,0x64);
	cq_writereg(&mn_bridge->cq, 0x76dd,0x22);
	cq_writereg(&mn_bridge->cq, 0x76e4,0x00);
	cq_writereg(&mn_bridge->cq, 0x76e6,0x1e); /* 0 for (DP?) scramble off */
	cq_writereg(&mn_bridge->cq, 0x7670,0xff);
	cq_writereg(&mn_bridge->cq, 0x7671,0xff);
	cq_writereg(&mn_bridge->cq, 0x7672,0xff);
	cq_writereg(&mn_bridge->cq, 0x7673,0xff);
	cq_writereg(&mn_bridge->cq, 0x7668,0xff);
	cq_writereg(&mn_bridge->cq, 0x7669,0xff);
	cq_writereg(&mn_bridge->cq, 0x766a,0xff);
	cq_writereg(&mn_bridge->cq, 0x766b,0xff);
	cq_writereg(&mn_bridge->cq, 0x7655,0x04);
	cq_writereg(&mn_bridge->cq, 0x7007,0xff);
	cq_writereg(&mn_bridge->cq, 0x7098,0xff);
	cq_writereg(&mn_bridge->cq, 0x7099,0x00);
	cq_writereg(&mn_bridge->cq, 0x709a,0x0f);
	cq_writereg(&mn_bridge->cq, 0x709b,0x00);
	cq_writereg(&mn_bridge->cq, 0x709c,0x50);
	cq_writereg(&mn_bridge->cq, 0x709d,0x00);
	cq_writereg(&mn_bridge->cq, 0x709e,0x00);
	cq_writereg(&mn_bridge->cq, 0x709f,0xd0);
	cq_writereg(&mn_bridge->cq, 0x7a9c,0x2e);
	cq_writereg(&mn_bridge->cq, 0x7021,0x04);
	cq_writereg(&mn_bridge->cq, 0x7028,0x00);
	cq_writereg(&mn_bridge->cq, 0x7030,0xa3);
	cq_writereg(&mn_bridge->cq, 0x7016,0x04);
#endif

	/* Disable InfoFrames */
	cq_writereg(&mn_bridge->cq, INFENA, 0x00);
	/* Reset HDCP */
	cq_writereg(&mn_bridge->cq, TSRST, TSRST_ENCSRST | TSRST_HDCPSRST);
	/* Disable HDCP flag */
	cq_writereg(&mn_bridge->cq, TSRST, HDCPEN_ENC_DIS);
	/* HDCP AKE reset */
	cq_writereg(&mn_bridge->cq, AKESRST, 0xff);
	/* Wait AKE busy */
	cq_wait_clear(&mn_bridge->cq, AKESTA, AKESTA_BUSY);
	
	if (cq_exec(&mn_bridge->cq) < 0) {
		DRM_ERROR("failed to run pre-enable sequence");
	}
	mutex_unlock(&mn_bridge->mutex);
}

static void mn86471a_enable(struct drm_bridge *bridge)
{
	struct mn86471a_bridge *mn_bridge = bridge_to_mn86471a(bridge);
	u8 dp[3];

	if (!mn_bridge->mode) {
		DRM_ERROR("mode not available\n");
		return;
	}

	DRM_DEBUG_KMS("mn86471a_enable (mode: %d)\n", mn_bridge->mode);
	
	/* Here come the dragons */

	mutex_lock(&mn_bridge->mutex);
	cq_init(&mn_bridge->cq, 4);
	/* Read DisplayPort status (?) */
	cq_read(&mn_bridge->cq, 0x76e1, 3);
	if (cq_exec(&mn_bridge->cq) < 11) {
		mutex_unlock(&mn_bridge->mutex);
		DRM_ERROR("could not read DP status");
		return;
	}
	memcpy(dp, &mn_bridge->cq.reply.databuf[3], 3);

	cq_init(&mn_bridge->cq, 4);

	/* Wait for DP lane status */
	cq_wait_set(&mn_bridge->cq, 0x761e, 0x77);
	cq_wait_set(&mn_bridge->cq, 0x761f, 0x77);
	/* Wait for ?? */
	cq_wait_set(&mn_bridge->cq, 0x7669, 0x01);

	cq_writereg(&mn_bridge->cq, 0x76d9, (dp[0] & 0x1f) | (dp[0] << 5));
	cq_writereg(&mn_bridge->cq, 0x76da, (dp[1] & 0x7c) | ((dp[0] >> 3) & 3) | ((dp[1] << 5) & 0x80));
	cq_writereg(&mn_bridge->cq, 0x76db, 0x80 | ((dp[1] >> 3) & 0xf));
	cq_writereg(&mn_bridge->cq, 0x76e4, 0x01);
	cq_writereg(&mn_bridge->cq, TSYSCTRL, TSYSCTRL_HDMI);
	cq_writereg(&mn_bridge->cq, VINCNT, VINCNT_VIF_FILEN);
	cq_writereg(&mn_bridge->cq, 0x7071, 0);
	cq_writereg(&mn_bridge->cq, 0x7062, mn_bridge->mode);
	cq_writereg(&mn_bridge->cq, 0x765a, 0);
	cq_writereg(&mn_bridge->cq, 0x7062, mn_bridge->mode | 0x80);
	cq_writereg(&mn_bridge->cq, 0x7215, 0x28); /* aspect */
	cq_writereg(&mn_bridge->cq, 0x7217, mn_bridge->mode);
	cq_writereg(&mn_bridge->cq, 0x7218, 0);
	cq_writereg(&mn_bridge->cq, CSCMOD, 0xdc);
	cq_writereg(&mn_bridge->cq, C420SET, 0xaa);
	cq_writereg(&mn_bridge->cq, TDPCMODE, 0x4a);
	cq_writereg(&mn_bridge->cq, OUTWSET, 0x00);
	cq_writereg(&mn_bridge->cq, 0x70c4, 0x08);
	cq_writereg(&mn_bridge->cq, 0x70c5, 0x08);
	cq_writereg(&mn_bridge->cq, 0x7096, 0xff);
	cq_writereg(&mn_bridge->cq, 0x7027, 0x00);
	cq_writereg(&mn_bridge->cq, 0x7020, 0x20);
	cq_writereg(&mn_bridge->cq, 0x700b, 0x01);
	cq_writereg(&mn_bridge->cq, PKTENA, 0x20);
	cq_writereg(&mn_bridge->cq, 0x7096, 0xff);
	cq_writereg(&mn_bridge->cq, INFENA, INFENA_AVIEN);
	cq_writereg(&mn_bridge->cq, UPDCTRL, UPDCTRL_ALLUPD | UPDCTRL_AVIIUPD |
					     UPDCTRL_CLKUPD | UPDCTRL_VIFUPD |
					     UPDCTRL_CSCUPD);
	cq_wait_set(&mn_bridge->cq, 0x7096, 0x80);

	cq_mask(&mn_bridge->cq, 0x7216, 0x00, 0x80);
	cq_writereg(&mn_bridge->cq, 0x7218, 0x00);

	cq_writereg(&mn_bridge->cq, 0x7096, 0xff);
	cq_writereg(&mn_bridge->cq, VMUTECNT, VMUTECNT_LINEWIDTH_90 | VMUTECNT_VMUTE_MUTE_NORMAL);
	cq_writereg(&mn_bridge->cq, 0x7016, 0x04);
	cq_writereg(&mn_bridge->cq, 0x7a88, 0xff);
	cq_writereg(&mn_bridge->cq, 0x7a83, 0x88);
	cq_writereg(&mn_bridge->cq, 0x7204, 0x40);
	
	cq_wait_set(&mn_bridge->cq, 0x7096, 0x80);
	
	cq_writereg(&mn_bridge->cq, 0x7006, 0x02);
	cq_writereg(&mn_bridge->cq, 0x7020, 0x21);
	cq_writereg(&mn_bridge->cq, 0x7a8b, 0x00);
	cq_writereg(&mn_bridge->cq, 0x7020, 0x21);

	cq_writereg(&mn_bridge->cq, VMUTECNT, VMUTECNT_LINEWIDTH_90);
	if (cq_exec(&mn_bridge->cq) < 0) {
		DRM_ERROR("Failed to configure bridge mode\n");
	}

	mutex_unlock(&mn_bridge->mutex);
}

static void mn86471a_disable(struct drm_bridge *bridge)
{
	struct mn86471a_bridge *mn_bridge = bridge_to_mn86471a(bridge);
	DRM_DEBUG_KMS("mn86471a_disable\n");

	mutex_lock(&mn_bridge->mutex);
	cq_init(&mn_bridge->cq, 4);
	cq_writereg(&mn_bridge->cq, VMUTECNT, VMUTECNT_LINEWIDTH_90 | VMUTECNT_VMUTE_MUTE_NORMAL);
	cq_writereg(&mn_bridge->cq, INFENA, 0x00);
	if (cq_exec(&mn_bridge->cq) < 0) {
		DRM_ERROR("Failed to disable bridge\n");
	}
	mutex_unlock(&mn_bridge->mutex);
}

static void mn86471a_post_disable(struct drm_bridge *bridge)
{
	/* struct mn86471a_bridge *mn_bridge = bridge_to_mn86471a(bridge); */
	DRM_DEBUG_KMS("mn86471a_post_disable\n");
}

/* Hardcoded modes, since we don't really know how to do custom modes yet.
 * Other CEA modes *should* work (and are allowed if externally added) */

/* 1 - 640x480@60Hz */
static const struct drm_display_mode mode_480p = {
	DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 25175, 640, 656,
		 752, 800, 0, 480, 490, 492, 525, 0,
		 DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	.vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3
};
/* 4 - 1280x720@60Hz */
static const struct drm_display_mode mode_720p = {
	DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
		 1430, 1650, 0, 720, 725, 730, 750, 0,
		 DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9
};
/* 16 - 1920x1080@60Hz */
static const struct drm_display_mode mode_1080p = {
	DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		 2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		 DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9
};

int mn86471a_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *newmode;
	DRM_DEBUG_KMS("mn86471a_get_modes\n");

	newmode = drm_mode_duplicate(dev, &mode_1080p);
	drm_mode_probed_add(connector, newmode);
	//newmode = drm_mode_duplicate(dev, &mode_720p);
	//drm_mode_probed_add(connector, newmode);
	//newmode = drm_mode_duplicate(dev, &mode_480p);
	//drm_mode_probed_add(connector, newmode);

	drm_mode_connector_update_edid_property(connector, NULL);

	return 0;
}

enum drm_connector_status mn86471a_detect(struct drm_connector *connector,
		bool force)
{
	struct mn86471a_bridge *mn_bridge = &g_bridge;
	u8 reg;

	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct radeon_connector_atom_dig *radeon_dig_connector = radeon_connector->con_priv;

	radeon_dig_connector->dp_sink_type = CONNECTOR_OBJECT_ID_DISPLAYPORT;
	radeon_dp_getdpcd(radeon_connector);

	mutex_lock(&mn_bridge->mutex);
	cq_init(&mn_bridge->cq, 4);
	cq_read(&mn_bridge->cq, TMONREG, 1);
	if (cq_exec(&mn_bridge->cq) < 9) {
		mutex_unlock(&mn_bridge->mutex);
		DRM_ERROR("could not read TMONREG");
		return connector_status_disconnected;
	}
	reg = mn_bridge->cq.reply.databuf[3];
	mutex_unlock(&mn_bridge->mutex);
	
	DRM_DEBUG_KMS("TMONREG=0x%02x\n", reg);

	if (reg & TMONREG_HPD)
		return connector_status_connected;
	else
		return connector_status_disconnected;
}

int mn86471a_mode_valid(struct drm_connector *connector,
				  struct drm_display_mode *mode)
{
	int vic = drm_match_cea_mode(mode);

	/* Allow anything that we can match up to a VIC (CEA modes) */
	if (!vic || vic != 16) {
		return MODE_BAD;
	}

	return MODE_OK;
}

static int mn86471a_bridge_attach(struct drm_bridge *bridge)
{
	/* struct mn86471a_bridge *mn_bridge = bridge_to_mn86471a(bridge); */

	return 0;
}

static struct drm_bridge_funcs mn86471a_bridge_funcs = {
	.pre_enable = mn86471a_pre_enable,
	.enable = mn86471a_enable,
	.disable = mn86471a_disable,
	.post_disable = mn86471a_post_disable,
	.attach = mn86471a_bridge_attach,
	.mode_set = mn86471a_mode_set
};

int mn86471a_bridge_register(struct drm_connector *connector,
			     struct drm_encoder *encoder)
{
	int ret;
	struct mn86471a_bridge *mn_bridge = &g_bridge;
	struct drm_device *dev = connector->dev;

	mn_bridge->encoder = encoder;
	mn_bridge->connector = connector;
	mn_bridge->bridge.funcs = &mn86471a_bridge_funcs;
	ret = drm_bridge_attach(dev, &mn_bridge->bridge);
	if (ret) {
		DRM_ERROR("Failed to initialize bridge with drm\n");
		return -EINVAL;
	}

	encoder->bridge = &mn_bridge->bridge;

	return 0;
}


