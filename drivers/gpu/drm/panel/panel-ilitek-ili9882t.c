// SPDX-License-Identifier: GPL-2.0
/*
 * Panels based on the Ilitek ILI9882T display controller.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

/*
 * Use this descriptor struct to describe different panels using the
 * Ilitek ILI9882T display controller.
 */
struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int bpc;

	/**
	 * @width_mm: width of the panel's active display area
	 * @height_mm: height of the panel's active display area
	 */
	struct {
		unsigned int width_mm;
		unsigned int height_mm;
	} size;

	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	const struct panel_init_cmd *init_cmds;
	unsigned int lanes;
};

struct ili9882t {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	const struct panel_desc *desc;

	enum drm_panel_orientation orientation;
	struct regulator *pp3300;
	struct regulator *pp1800;
	struct regulator *avee;
	struct regulator *avdd;
	struct gpio_desc *enable_gpio;
};

enum dsi_cmd_type {
	INIT_DCS_CMD,
	DELAY_CMD,
};

struct panel_init_cmd {
	enum dsi_cmd_type type;
	size_t len;
	const char *data;
};

#define _INIT_DCS_CMD(...) { \
	.type = INIT_DCS_CMD, \
	.len = sizeof((char[]){__VA_ARGS__}), \
	.data = (char[]){__VA_ARGS__} }

#define _INIT_DELAY_CMD(...) { \
	.type = DELAY_CMD,\
	.len = sizeof((char[]){__VA_ARGS__}), \
	.data = (char[]){__VA_ARGS__} }

/* ILI9882-specific commands, add new commands as you decode them */
#define ILI9882T_DCS_SWITCH_PAGE	0xFF

#define _INIT_SWITCH_PAGE_CMD(page) \
	_INIT_DCS_CMD(ILI9882T_DCS_SWITCH_PAGE, 0x98, 0x82, (page))

static const struct panel_init_cmd starry_ili9882t_init_cmd[] = {
	_INIT_DELAY_CMD(5),
	_INIT_SWITCH_PAGE_CMD(0x01),
	_INIT_DCS_CMD(0x00, 0x42),
	_INIT_DCS_CMD(0x01, 0x11),
	_INIT_DCS_CMD(0x02, 0x00),
	_INIT_DCS_CMD(0x03, 0x00),

	_INIT_DCS_CMD(0x04, 0x01),
	_INIT_DCS_CMD(0x05, 0x11),
	_INIT_DCS_CMD(0x06, 0x00),
	_INIT_DCS_CMD(0x07, 0x00),

	_INIT_DCS_CMD(0x08, 0x80),
	_INIT_DCS_CMD(0x09, 0x81),
	_INIT_DCS_CMD(0x0A, 0x71),
	_INIT_DCS_CMD(0x0B, 0x00),

	_INIT_DCS_CMD(0x0C, 0x00),
	_INIT_DCS_CMD(0x0E, 0x1A),

	_INIT_DCS_CMD(0x24, 0x00),
	_INIT_DCS_CMD(0x25, 0x00),
	_INIT_DCS_CMD(0x26, 0x00),
	_INIT_DCS_CMD(0x27, 0x00),

	_INIT_DCS_CMD(0x2C, 0xD4),
	_INIT_DCS_CMD(0xB9, 0x40),

	_INIT_DCS_CMD(0xB0, 0x11),

	_INIT_DCS_CMD(0xE6, 0x32),
	_INIT_DCS_CMD(0xD1, 0x30),

	_INIT_DCS_CMD(0xD6, 0x55),

	_INIT_DCS_CMD(0xD0, 0x01),
	_INIT_DCS_CMD(0xE3, 0x93),
	_INIT_DCS_CMD(0xE4, 0x00),
	_INIT_DCS_CMD(0xE5, 0x80),

	_INIT_DCS_CMD(0x31, 0x07),
	_INIT_DCS_CMD(0x32, 0x07),
	_INIT_DCS_CMD(0x33, 0x07),
	_INIT_DCS_CMD(0x34, 0x07),
	_INIT_DCS_CMD(0x35, 0x07),
	_INIT_DCS_CMD(0x36, 0x01),
	_INIT_DCS_CMD(0x37, 0x00),
	_INIT_DCS_CMD(0x38, 0x28),
	_INIT_DCS_CMD(0x39, 0x29),
	_INIT_DCS_CMD(0x3A, 0x11),
	_INIT_DCS_CMD(0x3B, 0x13),
	_INIT_DCS_CMD(0x3C, 0x15),
	_INIT_DCS_CMD(0x3D, 0x17),
	_INIT_DCS_CMD(0x3E, 0x09),
	_INIT_DCS_CMD(0x3F, 0x0D),
	_INIT_DCS_CMD(0x40, 0x02),
	_INIT_DCS_CMD(0x41, 0x02),
	_INIT_DCS_CMD(0x42, 0x02),
	_INIT_DCS_CMD(0x43, 0x02),
	_INIT_DCS_CMD(0x44, 0x02),
	_INIT_DCS_CMD(0x45, 0x02),
	_INIT_DCS_CMD(0x46, 0x02),

	_INIT_DCS_CMD(0x47, 0x07),
	_INIT_DCS_CMD(0x48, 0x07),
	_INIT_DCS_CMD(0x49, 0x07),
	_INIT_DCS_CMD(0x4A, 0x07),
	_INIT_DCS_CMD(0x4B, 0x07),
	_INIT_DCS_CMD(0x4C, 0x01),
	_INIT_DCS_CMD(0x4D, 0x00),
	_INIT_DCS_CMD(0x4E, 0x28),
	_INIT_DCS_CMD(0x4F, 0x29),
	_INIT_DCS_CMD(0x50, 0x10),
	_INIT_DCS_CMD(0x51, 0x12),
	_INIT_DCS_CMD(0x52, 0x14),
	_INIT_DCS_CMD(0x53, 0x16),
	_INIT_DCS_CMD(0x54, 0x08),
	_INIT_DCS_CMD(0x55, 0x0C),
	_INIT_DCS_CMD(0x56, 0x02),
	_INIT_DCS_CMD(0x57, 0x02),
	_INIT_DCS_CMD(0x58, 0x02),
	_INIT_DCS_CMD(0x59, 0x02),
	_INIT_DCS_CMD(0x5A, 0x02),
	_INIT_DCS_CMD(0x5B, 0x02),
	_INIT_DCS_CMD(0x5C, 0x02),

	_INIT_DCS_CMD(0x61, 0x07),
	_INIT_DCS_CMD(0x62, 0x07),
	_INIT_DCS_CMD(0x63, 0x07),
	_INIT_DCS_CMD(0x64, 0x07),
	_INIT_DCS_CMD(0x65, 0x07),
	_INIT_DCS_CMD(0x66, 0x01),
	_INIT_DCS_CMD(0x67, 0x00),
	_INIT_DCS_CMD(0x68, 0x28),
	_INIT_DCS_CMD(0x69, 0x29),
	_INIT_DCS_CMD(0x6A, 0x16),
	_INIT_DCS_CMD(0x6B, 0x14),
	_INIT_DCS_CMD(0x6C, 0x12),
	_INIT_DCS_CMD(0x6D, 0x10),
	_INIT_DCS_CMD(0x6E, 0x0C),
	_INIT_DCS_CMD(0x6F, 0x08),
	_INIT_DCS_CMD(0x70, 0x02),
	_INIT_DCS_CMD(0x71, 0x02),
	_INIT_DCS_CMD(0x72, 0x02),
	_INIT_DCS_CMD(0x73, 0x02),
	_INIT_DCS_CMD(0x74, 0x02),
	_INIT_DCS_CMD(0x75, 0x02),
	_INIT_DCS_CMD(0x76, 0x02),

	_INIT_DCS_CMD(0x77, 0x07),
	_INIT_DCS_CMD(0x78, 0x07),
	_INIT_DCS_CMD(0x79, 0x07),
	_INIT_DCS_CMD(0x7A, 0x07),
	_INIT_DCS_CMD(0x7B, 0x07),
	_INIT_DCS_CMD(0x7C, 0x01),
	_INIT_DCS_CMD(0x7D, 0x00),
	_INIT_DCS_CMD(0x7E, 0x28),
	_INIT_DCS_CMD(0x7F, 0x29),
	_INIT_DCS_CMD(0x80, 0x17),
	_INIT_DCS_CMD(0x81, 0x15),
	_INIT_DCS_CMD(0x82, 0x13),
	_INIT_DCS_CMD(0x83, 0x11),
	_INIT_DCS_CMD(0x84, 0x0D),
	_INIT_DCS_CMD(0x85, 0x09),
	_INIT_DCS_CMD(0x86, 0x02),
	_INIT_DCS_CMD(0x87, 0x07),
	_INIT_DCS_CMD(0x88, 0x07),
	_INIT_DCS_CMD(0x89, 0x07),
	_INIT_DCS_CMD(0x8A, 0x07),
	_INIT_DCS_CMD(0x8B, 0x07),
	_INIT_DCS_CMD(0x8C, 0x07),

	_INIT_SWITCH_PAGE_CMD(0x02),
	_INIT_DCS_CMD(0x29, 0x3A),
	_INIT_DCS_CMD(0x2A, 0x3B),

	_INIT_DCS_CMD(0x06, 0x01),
	_INIT_DCS_CMD(0x07, 0x01),
	_INIT_DCS_CMD(0x08, 0x0C),
	_INIT_DCS_CMD(0x09, 0x44),

	_INIT_DCS_CMD(0x3C, 0x0A),
	_INIT_DCS_CMD(0x39, 0x11),
	_INIT_DCS_CMD(0x3D, 0x00),
	_INIT_DCS_CMD(0x3A, 0x0C),
	_INIT_DCS_CMD(0x3B, 0x44),

	_INIT_DCS_CMD(0x53, 0x1F),
	_INIT_DCS_CMD(0x5E, 0x40),
	_INIT_DCS_CMD(0x84, 0x00),

	_INIT_SWITCH_PAGE_CMD(0x03),
	_INIT_DCS_CMD(0x20, 0x01),
	_INIT_DCS_CMD(0x21, 0x3C),
	_INIT_DCS_CMD(0x22, 0xFA),

	_INIT_SWITCH_PAGE_CMD(0x0A),
	_INIT_DCS_CMD(0xE0, 0x01),
	_INIT_DCS_CMD(0xE2, 0x01),
	_INIT_DCS_CMD(0xE5, 0x91),
	_INIT_DCS_CMD(0xE6, 0x3C),
	_INIT_DCS_CMD(0xE7, 0x00),
	_INIT_DCS_CMD(0xE8, 0xFA),

	_INIT_SWITCH_PAGE_CMD(0x12),
	_INIT_DCS_CMD(0x87, 0x2C),

	_INIT_SWITCH_PAGE_CMD(0x05),
	_INIT_DCS_CMD(0x73, 0xE5),
	_INIT_DCS_CMD(0x7F, 0x6B),
	_INIT_DCS_CMD(0x6D, 0xA4),
	_INIT_DCS_CMD(0x79, 0x54),
	_INIT_DCS_CMD(0x69, 0x97),
	_INIT_DCS_CMD(0x6A, 0x97),
	_INIT_DCS_CMD(0xA5, 0x3F),
	_INIT_DCS_CMD(0x61, 0xDA),
	_INIT_DCS_CMD(0xA7, 0xF1),
	_INIT_DCS_CMD(0x5F, 0x01),
	_INIT_DCS_CMD(0x62, 0x3F),
	_INIT_DCS_CMD(0x1D, 0x90),
	_INIT_DCS_CMD(0x86, 0x87),

	_INIT_SWITCH_PAGE_CMD(0x06),
	_INIT_DCS_CMD(0xC0, 0x80),
	_INIT_DCS_CMD(0xC1, 0x07),
	_INIT_DCS_CMD(0xCA, 0x58),
	_INIT_DCS_CMD(0xCB, 0x02),
	_INIT_DCS_CMD(0xCE, 0x58),
	_INIT_DCS_CMD(0xCF, 0x02),
	_INIT_DCS_CMD(0x67, 0x60),
	_INIT_DCS_CMD(0x10, 0x00),
	_INIT_DCS_CMD(0x92, 0x22),
	_INIT_DCS_CMD(0xD3, 0x08),
	_INIT_DCS_CMD(0xD6, 0x55),
	_INIT_DCS_CMD(0xDC, 0x38),

	_INIT_SWITCH_PAGE_CMD(0x08),
	_INIT_DCS_CMD(0xE0, 0x00, 0x10, 0x2A, 0x4D, 0x61, 0x56, 0x6A, 0x6E, 0x79, 0x76, 0x8F, 0x95, 0x98, 0xAE, 0xAA, 0xB2, 0xBB, 0xCE, 0xC6, 0xBD, 0xD5, 0xE2, 0xE8),
	_INIT_DCS_CMD(0xE1, 0x00, 0x10, 0x2A, 0x4D, 0x61, 0x56, 0x6A, 0x6E, 0x79, 0x76, 0x8F, 0x95, 0x98, 0xAE, 0xAA, 0xB2, 0xBB, 0xCE, 0xC6, 0xBD, 0xD5, 0xE2, 0xE8),

	_INIT_SWITCH_PAGE_CMD(0x04),
	_INIT_DCS_CMD(0xBA, 0x81),

	_INIT_SWITCH_PAGE_CMD(0x0C),
	_INIT_DCS_CMD(0x00, 0x02),
	_INIT_DCS_CMD(0x01, 0x00),
	_INIT_DCS_CMD(0x02, 0x03),
	_INIT_DCS_CMD(0x03, 0x01),
	_INIT_DCS_CMD(0x04, 0x03),
	_INIT_DCS_CMD(0x05, 0x02),
	_INIT_DCS_CMD(0x06, 0x04),
	_INIT_DCS_CMD(0x07, 0x03),
	_INIT_DCS_CMD(0x08, 0x03),
	_INIT_DCS_CMD(0x09, 0x04),
	_INIT_DCS_CMD(0x0A, 0x04),
	_INIT_DCS_CMD(0x0B, 0x05),
	_INIT_DCS_CMD(0x0C, 0x04),
	_INIT_DCS_CMD(0x0D, 0x06),
	_INIT_DCS_CMD(0x0E, 0x05),
	_INIT_DCS_CMD(0x0F, 0x07),
	_INIT_DCS_CMD(0x10, 0x04),
	_INIT_DCS_CMD(0x11, 0x08),
	_INIT_DCS_CMD(0x12, 0x05),
	_INIT_DCS_CMD(0x13, 0x09),
	_INIT_DCS_CMD(0x14, 0x05),
	_INIT_DCS_CMD(0x15, 0x0A),
	_INIT_DCS_CMD(0x16, 0x06),
	_INIT_DCS_CMD(0x17, 0x0B),
	_INIT_DCS_CMD(0x18, 0x05),
	_INIT_DCS_CMD(0x19, 0x0C),
	_INIT_DCS_CMD(0x1A, 0x06),
	_INIT_DCS_CMD(0x1B, 0x0D),
	_INIT_DCS_CMD(0x1C, 0x06),
	_INIT_DCS_CMD(0x1D, 0x0E),
	_INIT_DCS_CMD(0x1E, 0x07),
	_INIT_DCS_CMD(0x1F, 0x0F),
	_INIT_DCS_CMD(0x20, 0x06),
	_INIT_DCS_CMD(0x21, 0x10),
	_INIT_DCS_CMD(0x22, 0x07),
	_INIT_DCS_CMD(0x23, 0x11),
	_INIT_DCS_CMD(0x24, 0x07),
	_INIT_DCS_CMD(0x25, 0x12),
	_INIT_DCS_CMD(0x26, 0x08),
	_INIT_DCS_CMD(0x27, 0x13),
	_INIT_DCS_CMD(0x28, 0x07),
	_INIT_DCS_CMD(0x29, 0x14),
	_INIT_DCS_CMD(0x2A, 0x08),
	_INIT_DCS_CMD(0x2B, 0x15),
	_INIT_DCS_CMD(0x2C, 0x08),
	_INIT_DCS_CMD(0x2D, 0x16),
	_INIT_DCS_CMD(0x2E, 0x09),
	_INIT_DCS_CMD(0x2F, 0x17),
	_INIT_DCS_CMD(0x30, 0x08),
	_INIT_DCS_CMD(0x31, 0x18),
	_INIT_DCS_CMD(0x32, 0x09),
	_INIT_DCS_CMD(0x33, 0x19),
	_INIT_DCS_CMD(0x34, 0x09),
	_INIT_DCS_CMD(0x35, 0x1A),
	_INIT_DCS_CMD(0x36, 0x0A),
	_INIT_DCS_CMD(0x37, 0x1B),
	_INIT_DCS_CMD(0x38, 0x0A),
	_INIT_DCS_CMD(0x39, 0x1C),
	_INIT_DCS_CMD(0x3A, 0x0A),
	_INIT_DCS_CMD(0x3B, 0x1D),
	_INIT_DCS_CMD(0x3C, 0x0A),
	_INIT_DCS_CMD(0x3D, 0x1E),
	_INIT_DCS_CMD(0x3E, 0x0A),
	_INIT_DCS_CMD(0x3F, 0x1F),

	_INIT_SWITCH_PAGE_CMD(0x04),
	_INIT_DCS_CMD(0xBA, 0x01),

	_INIT_SWITCH_PAGE_CMD(0x0E),
	_INIT_DCS_CMD(0x02, 0x0C),
	_INIT_DCS_CMD(0x20, 0x10),
	_INIT_DCS_CMD(0x25, 0x16),
	_INIT_DCS_CMD(0x26, 0xE0),
	_INIT_DCS_CMD(0x27, 0x00),
	_INIT_DCS_CMD(0x29, 0x71),
	_INIT_DCS_CMD(0x2A, 0x46),
	_INIT_DCS_CMD(0x2B, 0x1F),
	_INIT_DCS_CMD(0x2D, 0xC7),
	_INIT_DCS_CMD(0x31, 0x02),
	_INIT_DCS_CMD(0x32, 0xDF),
	_INIT_DCS_CMD(0x33, 0x5A),
	_INIT_DCS_CMD(0x34, 0xC0),
	_INIT_DCS_CMD(0x35, 0x5A),
	_INIT_DCS_CMD(0x36, 0xC0),
	_INIT_DCS_CMD(0x38, 0x65),
	_INIT_DCS_CMD(0x80, 0x3E),
	_INIT_DCS_CMD(0x81, 0xA0),
	_INIT_DCS_CMD(0xB0, 0x01),
	_INIT_DCS_CMD(0xB1, 0xCC),
	_INIT_DCS_CMD(0xC0, 0x12),
	_INIT_DCS_CMD(0xC2, 0xCC),
	_INIT_DCS_CMD(0xC3, 0xCC),
	_INIT_DCS_CMD(0xC4, 0xCC),
	_INIT_DCS_CMD(0xC5, 0xCC),
	_INIT_DCS_CMD(0xC6, 0xCC),
	_INIT_DCS_CMD(0xC7, 0xCC),
	_INIT_DCS_CMD(0xC8, 0xCC),
	_INIT_DCS_CMD(0xC9, 0xCC),
	_INIT_DCS_CMD(0x30, 0x00),
	_INIT_DCS_CMD(0x00, 0x81),
	_INIT_DCS_CMD(0x08, 0x02),
	_INIT_DCS_CMD(0x09, 0x00),
	_INIT_DCS_CMD(0x07, 0x21),
	_INIT_DCS_CMD(0x04, 0x10),

	_INIT_SWITCH_PAGE_CMD(0x1E),
	_INIT_DCS_CMD(0x60, 0x00),
	_INIT_DCS_CMD(0x64, 0x00),
	_INIT_DCS_CMD(0x6D, 0x00),

	_INIT_SWITCH_PAGE_CMD(0x0B),
	_INIT_DCS_CMD(0xA6, 0x44),
	_INIT_DCS_CMD(0xA7, 0xB6),
	_INIT_DCS_CMD(0xA8, 0x03),
	_INIT_DCS_CMD(0xA9, 0x03),
	_INIT_DCS_CMD(0xAA, 0x51),
	_INIT_DCS_CMD(0xAB, 0x51),
	_INIT_DCS_CMD(0xAC, 0x04),
	_INIT_DCS_CMD(0xBD, 0x92),
	_INIT_DCS_CMD(0xBE, 0xA1),

	_INIT_SWITCH_PAGE_CMD(0x05),
	_INIT_DCS_CMD(0x86, 0x87),

	_INIT_SWITCH_PAGE_CMD(0x06),
	_INIT_DCS_CMD(0x92, 0x22),

	_INIT_SWITCH_PAGE_CMD(0x00),
	_INIT_DCS_CMD(MIPI_DCS_EXIT_SLEEP_MODE),
	_INIT_DELAY_CMD(120),
	_INIT_DCS_CMD(MIPI_DCS_SET_DISPLAY_ON),
	_INIT_DELAY_CMD(20),
	{},
};

static inline struct ili9882t *to_ili9882t(struct drm_panel *panel)
{
	return container_of(panel, struct ili9882t, base);
}

static int ili9882t_init_dcs_cmd(struct ili9882t *ili)
{
	struct mipi_dsi_device *dsi = ili->dsi;
	struct drm_panel *panel = &ili->base;
	int i, err = 0;

	if (ili->desc->init_cmds) {
		const struct panel_init_cmd *init_cmds = ili->desc->init_cmds;

		for (i = 0; init_cmds[i].len != 0; i++) {
			const struct panel_init_cmd *cmd = &init_cmds[i];

			switch (cmd->type) {
			case DELAY_CMD:
				msleep(cmd->data[0]);
				err = 0;
				break;

			case INIT_DCS_CMD:
				err = mipi_dsi_dcs_write(dsi, cmd->data[0],
							 cmd->len <= 1 ? NULL :
							 &cmd->data[1],
							 cmd->len - 1);
				break;

			default:
				err = -EINVAL;
			}

			if (err < 0) {
				dev_err(panel->dev,
					"failed to write command %u\n", i);
				return err;
			}
		}
	}
	return 0;
}

static int ili9882t_switch_page(struct mipi_dsi_device *dsi, u8 page)
{
	int ret;
	const struct panel_init_cmd cmd = _INIT_SWITCH_PAGE_CMD(page);

	ret = mipi_dsi_dcs_write(dsi, cmd.data[0],
				 cmd.len <= 1 ? NULL :
				 &cmd.data[1],
				 cmd.len - 1);
	if (ret) {
		dev_err(&dsi->dev,
			"error switching panel controller page (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int ili9882t_enter_sleep_mode(struct ili9882t *ili)
{
	struct mipi_dsi_device *dsi = ili->dsi;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		return ret;

	return 0;
}

static int ili9882t_disable(struct drm_panel *panel)
{
	struct ili9882t *ili = to_ili9882t(panel);
	struct mipi_dsi_device *dsi = ili->dsi;
	int ret;

	ili9882t_switch_page(dsi, 0x00);
	ret = ili9882t_enter_sleep_mode(ili);
	if (ret < 0) {
		dev_err(panel->dev, "failed to set panel off: %d\n", ret);
		return ret;
	}

	msleep(150);

	return 0;
}

static int ili9882t_unprepare(struct drm_panel *panel)
{
	struct ili9882t *ili = to_ili9882t(panel);

	gpiod_set_value(ili->enable_gpio, 0);
	usleep_range(1000, 2000);
	regulator_disable(ili->avee);
	regulator_disable(ili->avdd);
	usleep_range(5000, 7000);
	regulator_disable(ili->pp1800);
	regulator_disable(ili->pp3300);

	return 0;
}

static int ili9882t_prepare(struct drm_panel *panel)
{
	struct ili9882t *ili = to_ili9882t(panel);
	int ret;

	gpiod_set_value(ili->enable_gpio, 0);
	usleep_range(1000, 1500);

	ret = regulator_enable(ili->pp3300);
	if (ret < 0)
		return ret;

	ret = regulator_enable(ili->pp1800);
	if (ret < 0)
		return ret;

	usleep_range(3000, 5000);

	ret = regulator_enable(ili->avdd);
	if (ret < 0)
		goto poweroff1v8;
	ret = regulator_enable(ili->avee);
	if (ret < 0)
		goto poweroffavdd;

	usleep_range(10000, 11000);

	// MIPI needs to keep the LP11 state before the lcm_reset pin is pulled high
	mipi_dsi_dcs_nop(ili->dsi);
	usleep_range(1000, 2000);

	gpiod_set_value(ili->enable_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value(ili->enable_gpio, 0);
	msleep(50);
	gpiod_set_value(ili->enable_gpio, 1);
	usleep_range(6000, 10000);

	ret = ili9882t_init_dcs_cmd(ili);
	if (ret < 0) {
		dev_err(panel->dev, "failed to init panel: %d\n", ret);
		goto poweroff;
	}

	return 0;

poweroff:
	regulator_disable(ili->avee);
poweroffavdd:
	regulator_disable(ili->avdd);
poweroff1v8:
	usleep_range(5000, 7000);
	regulator_disable(ili->pp1800);
	gpiod_set_value(ili->enable_gpio, 0);

	return ret;
}

static int ili9882t_enable(struct drm_panel *panel)
{
	msleep(130);
	return 0;
}

static const struct drm_display_mode starry_ili9882t_default_mode = {
	.clock = 165280,
	.hdisplay = 1200,
	.hsync_start = 1200 + 72,
	.hsync_end = 1200 + 72 + 30,
	.htotal = 1200 + 72 + 30 + 72,
	.vdisplay = 1920,
	.vsync_start = 1920 + 68,
	.vsync_end = 1920 + 68 + 2,
	.vtotal = 1920 + 68 + 2 + 10,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct panel_desc starry_ili9882t_desc = {
	.modes = &starry_ili9882t_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 141,
		.height_mm = 226,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM,
	.init_cmds = starry_ili9882t_init_cmd,
};

static int ili9882t_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	struct ili9882t *ili = to_ili9882t(panel);
	const struct drm_display_mode *m = ili->desc->modes;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, m);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
		return -ENOMEM;
	}

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = ili->desc->size.width_mm;
	connector->display_info.height_mm = ili->desc->size.height_mm;
	connector->display_info.bpc = ili->desc->bpc;

	return 1;
}

static enum drm_panel_orientation ili9882t_get_orientation(struct drm_panel *panel)
{
	struct ili9882t *ili = to_ili9882t(panel);

	return ili->orientation;
}

static const struct drm_panel_funcs ili9882t_funcs = {
	.disable = ili9882t_disable,
	.unprepare = ili9882t_unprepare,
	.prepare = ili9882t_prepare,
	.enable = ili9882t_enable,
	.get_modes = ili9882t_get_modes,
	.get_orientation = ili9882t_get_orientation,
};

static int ili9882t_add(struct ili9882t *ili)
{
	struct device *dev = &ili->dsi->dev;
	int err;

	ili->avdd = devm_regulator_get(dev, "avdd");
	if (IS_ERR(ili->avdd))
		return PTR_ERR(ili->avdd);

	ili->avee = devm_regulator_get(dev, "avee");
	if (IS_ERR(ili->avee))
		return PTR_ERR(ili->avee);

	ili->pp3300 = devm_regulator_get(dev, "pp3300");
	if (IS_ERR(ili->pp3300))
		return PTR_ERR(ili->pp3300);

	ili->pp1800 = devm_regulator_get(dev, "pp1800");
	if (IS_ERR(ili->pp1800))
		return PTR_ERR(ili->pp1800);

	ili->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ili->enable_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(ili->enable_gpio));
		return PTR_ERR(ili->enable_gpio);
	}

	gpiod_set_value(ili->enable_gpio, 0);

	drm_panel_init(&ili->base, dev, &ili9882t_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	err = of_drm_get_panel_orientation(dev->of_node, &ili->orientation);
	if (err < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, err);
		return err;
	}

	err = drm_panel_of_backlight(&ili->base);
	if (err)
		return err;

	ili->base.funcs = &ili9882t_funcs;
	ili->base.dev = &ili->dsi->dev;

	drm_panel_add(&ili->base);

	return 0;
}

static int ili9882t_probe(struct mipi_dsi_device *dsi)
{
	struct ili9882t *ili;
	int ret;
	const struct panel_desc *desc;

	ili = devm_kzalloc(&dsi->dev, sizeof(*ili), GFP_KERNEL);
	if (!ili)
		return -ENOMEM;

	desc = of_device_get_match_data(&dsi->dev);
	dsi->lanes = desc->lanes;
	dsi->format = desc->format;
	dsi->mode_flags = desc->mode_flags;
	ili->desc = desc;
	ili->dsi = dsi;
	ret = ili9882t_add(ili);
	if (ret < 0)
		return ret;

	mipi_dsi_set_drvdata(dsi, ili);

	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&ili->base);

	return ret;
}

static void ili9882t_remove(struct mipi_dsi_device *dsi)
{
	struct ili9882t *ili = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	if (ili->base.dev)
		drm_panel_remove(&ili->base);
}

static const struct of_device_id ili9882t_of_match[] = {
	{ .compatible = "starry,ili9882t",
	  .data = &starry_ili9882t_desc
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ili9882t_of_match);

static struct mipi_dsi_driver ili9882t_driver = {
	.driver = {
		.name = "panel-ili9882t",
		.of_match_table = ili9882t_of_match,
	},
	.probe = ili9882t_probe,
	.remove = ili9882t_remove,
};
module_mipi_dsi_driver(ili9882t_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("Ilitek ILI9882T-based panels driver");
MODULE_LICENSE("GPL");
