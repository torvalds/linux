/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _PANEL_SAMSUNG_DSI_H
#define _PANEL_SAMSUNG_DSI_H

#include <drm/drm_mipi_dsi.h>

static inline void samsung_dsi_test_key_on_lvl1(struct mipi_dsi_multi_context *ctx)
{
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9f, 0xa5, 0xa5);
}

static inline void samsung_dsi_test_key_off_lvl1(struct mipi_dsi_multi_context *ctx)
{
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9f, 0x5a, 0x5a);
}

static inline void samsung_dsi_test_key_on_lvl2(struct mipi_dsi_multi_context *ctx)
{
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0x5a, 0x5a);
}

static inline void samsung_dsi_test_key_off_lvl2(struct mipi_dsi_multi_context *ctx)
{
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0xa5, 0xa5);
}

static inline void samsung_dsi_test_key_on_lvl3(struct mipi_dsi_multi_context *ctx)
{
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfc, 0x5a, 0x5a);
}

static inline void samsung_dsi_test_key_off_lvl3(struct mipi_dsi_multi_context *ctx)
{
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfc, 0xa5, 0xa5);
}

#endif /* _PANEL_SAMSUNG_DSI_H */
