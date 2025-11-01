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

struct ili9882t;

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
	int (*init)(struct ili9882t *boe);
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

/* ILI9882-specific commands, add new commands as you decode them */
#define ILI9882T_DCS_SWITCH_PAGE	0xFF

#define ili9882t_switch_page(ctx, page) \
	mipi_dsi_dcs_write_seq_multi(ctx, ILI9882T_DCS_SWITCH_PAGE, \
				     0x98, 0x82, (page))

static int starry_ili9882t_init(struct ili9882t *ili)
{
	struct mipi_dsi_multi_context ctx = { .dsi = ili->dsi };

	usleep_range(5000, 5100);

	ili9882t_switch_page(&ctx, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x00, 0x42);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x01, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x02, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x03, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x04, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x05, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x06, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x07, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x08, 0x80);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x09, 0x81);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0a, 0x71);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0b, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0e, 0x1a);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x24, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x25, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x26, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x27, 0x00);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2c, 0xd4);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0x40);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x11);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe6, 0x32);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd1, 0x30);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd6, 0x55);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe3, 0x93);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe4, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe5, 0x80);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x31, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x32, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x33, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x34, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x35, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x36, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x37, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x38, 0x28);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x39, 0x29);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3a, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3b, 0x13);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3c, 0x15);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3d, 0x17);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3e, 0x09);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3f, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x40, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x41, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x42, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x43, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x44, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x45, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x46, 0x02);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x47, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x48, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x49, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4a, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4b, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4c, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4e, 0x28);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4f, 0x29);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x50, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x51, 0x12);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x52, 0x14);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x53, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x54, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x55, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x56, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x57, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x58, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x59, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5a, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5b, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5c, 0x02);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x61, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x62, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x63, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x64, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x65, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x66, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x67, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x68, 0x28);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x69, 0x29);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6a, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6b, 0x14);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6c, 0x12);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6d, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6e, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6f, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x70, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x71, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x72, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x73, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x74, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x75, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x76, 0x02);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x77, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x78, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x79, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7a, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7b, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7c, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7e, 0x28);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7f, 0x29);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x80, 0x17);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x81, 0x15);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x82, 0x13);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x83, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x84, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x85, 0x09);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x86, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x87, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x88, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x89, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x8a, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x8b, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x8c, 0x07);

	ili9882t_switch_page(&ctx, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x29, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2a, 0x3b);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x06, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x07, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x08, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x09, 0x44);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3c, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x39, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3a, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3b, 0x44);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x53, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5e, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x84, 0x00);

	ili9882t_switch_page(&ctx, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x20, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x21, 0x3c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x22, 0xfa);

	ili9882t_switch_page(&ctx, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe2, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe5, 0x91);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe6, 0x3c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe7, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe8, 0xfa);

	ili9882t_switch_page(&ctx, 0x12);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x87, 0x2c);

	ili9882t_switch_page(&ctx, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x73, 0xe5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x7f, 0x6b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6d, 0xa4);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x79, 0x54);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x69, 0x97);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6a, 0x97);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa5, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x61, 0xda);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa7, 0xf1);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x62, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1d, 0x90);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x86, 0x87);

	ili9882t_switch_page(&ctx, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x80);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc1, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca, 0x58);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcb, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xce, 0x58);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcf, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x67, 0x60);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x10, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x92, 0x22);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd3, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd6, 0x55);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xdc, 0x38);

	ili9882t_switch_page(&ctx, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe0, 0x00, 0x10, 0x2a, 0x4d, 0x61, 0x56, 0x6a, 0x6e,
				     0x79, 0x76, 0x8f, 0x95, 0x98, 0xae, 0xaa, 0xb2, 0xbb, 0xce,
				     0xc6, 0xbd, 0xd5, 0xe2, 0xe8);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe1, 0x00, 0x10, 0x2a, 0x4d, 0x61, 0x56, 0x6a, 0x6e,
				     0x79, 0x76, 0x8f, 0x95, 0x98, 0xae, 0xaa, 0xb2, 0xbb, 0xce,
				     0xc6, 0xbd, 0xd5, 0xe2, 0xe8);

	ili9882t_switch_page(&ctx, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x81);

	ili9882t_switch_page(&ctx, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x00, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x01, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x02, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x03, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x04, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x05, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x07, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x08, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x09, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0a, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0b, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0c, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0d, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0e, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x0f, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x10, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x11, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x12, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x13, 0x09);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x14, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x15, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x16, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x17, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x18, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x19, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1a, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1b, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1c, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1d, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1e, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x1f, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x20, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x21, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x22, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x23, 0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x24, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x25, 0x12);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x26, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x27, 0x13);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x28, 0x07);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x29, 0x14);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2a, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2b, 0x15);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2c, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2d, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2e, 0x09);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2f, 0x17);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x31, 0x18);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x32, 0x09);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x33, 0x19);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x34, 0x09);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x35, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x36, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x37, 0x1b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x38, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x39, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3a, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3b, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3c, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3d, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3e, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3f, 0x1f);

	ili9882t_switch_page(&ctx, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x01);

	ili9882t_switch_page(&ctx, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x02, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x20, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x25, 0x16);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x26, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x27, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x29, 0x71);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2a, 0x46);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2b, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2d, 0xc7);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x31, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x32, 0xdf);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x33, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x34, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x35, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x36, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x38, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x80, 0x3e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x81, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x12);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc2, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc3, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc4, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc6, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc7, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc8, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc9, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x00, 0x81);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x08, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x09, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x07, 0x21);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x04, 0x10);

	ili9882t_switch_page(&ctx, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x64, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6d, 0x00);

	ili9882t_switch_page(&ctx, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa6, 0x44);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa7, 0xb6);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa8, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xa9, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xaa, 0x51);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xab, 0x51);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xac, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x92);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbe, 0xa1);

	ili9882t_switch_page(&ctx, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x86, 0x87);

	ili9882t_switch_page(&ctx, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x92, 0x22);

	ili9882t_switch_page(&ctx, 0x00);
	mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);

	mipi_dsi_msleep(&ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&ctx);

	mipi_dsi_msleep(&ctx, 20);

	return ctx.accum_err;
};

static inline struct ili9882t *to_ili9882t(struct drm_panel *panel)
{
	return container_of(panel, struct ili9882t, base);
}

static int ili9882t_disable(struct drm_panel *panel)
{
	struct ili9882t *ili = to_ili9882t(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = ili->dsi };

	ili9882t_switch_page(&ctx, 0x00);

	ili->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);

	mipi_dsi_msleep(&ctx, 150);

	return ctx.accum_err;
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
	ret = mipi_dsi_dcs_nop(ili->dsi);
	if (ret < 0) {
		dev_err(&ili->dsi->dev, "Failed to send NOP: %d\n", ret);
		goto poweroff;
	}
	usleep_range(1000, 2000);

	gpiod_set_value(ili->enable_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value(ili->enable_gpio, 0);
	msleep(50);
	gpiod_set_value(ili->enable_gpio, 1);
	usleep_range(6000, 10000);

	ret = ili->desc->init(ili);
	if (ret < 0)
		goto poweroff;

	return 0;

poweroff:
	gpiod_set_value(ili->enable_gpio, 0);
	regulator_disable(ili->avee);
poweroffavdd:
	regulator_disable(ili->avdd);
poweroff1v8:
	usleep_range(5000, 7000);
	regulator_disable(ili->pp1800);

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
	.init = starry_ili9882t_init,
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
		dev_err(dev, "cannot get enable-gpios %ld\n",
			PTR_ERR(ili->enable_gpio));
		return PTR_ERR(ili->enable_gpio);
	}

	gpiod_set_value(ili->enable_gpio, 0);

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

	ili = devm_drm_panel_alloc(&dsi->dev, __typeof(*ili), base,
				   &ili9882t_funcs, DRM_MODE_CONNECTOR_DSI);

	if (IS_ERR(ili))
		return PTR_ERR(ili);

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
