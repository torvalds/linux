/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#ifndef __OMAP_DRM_DSS_H
#define __OMAP_DRM_DSS_H

#include <linux/list.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <video/videomode.h>
#include <linux/platform_data/omapdss.h>
#include <uapi/drm/drm_mode.h>
#include <drm/drm_crtc.h>

#define DISPC_IRQ_FRAMEDONE		(1 << 0)
#define DISPC_IRQ_VSYNC			(1 << 1)
#define DISPC_IRQ_EVSYNC_EVEN		(1 << 2)
#define DISPC_IRQ_EVSYNC_ODD		(1 << 3)
#define DISPC_IRQ_ACBIAS_COUNT_STAT	(1 << 4)
#define DISPC_IRQ_PROG_LINE_NUM		(1 << 5)
#define DISPC_IRQ_GFX_FIFO_UNDERFLOW	(1 << 6)
#define DISPC_IRQ_GFX_END_WIN		(1 << 7)
#define DISPC_IRQ_PAL_GAMMA_MASK	(1 << 8)
#define DISPC_IRQ_OCP_ERR		(1 << 9)
#define DISPC_IRQ_VID1_FIFO_UNDERFLOW	(1 << 10)
#define DISPC_IRQ_VID1_END_WIN		(1 << 11)
#define DISPC_IRQ_VID2_FIFO_UNDERFLOW	(1 << 12)
#define DISPC_IRQ_VID2_END_WIN		(1 << 13)
#define DISPC_IRQ_SYNC_LOST		(1 << 14)
#define DISPC_IRQ_SYNC_LOST_DIGIT	(1 << 15)
#define DISPC_IRQ_WAKEUP		(1 << 16)
#define DISPC_IRQ_SYNC_LOST2		(1 << 17)
#define DISPC_IRQ_VSYNC2		(1 << 18)
#define DISPC_IRQ_VID3_END_WIN		(1 << 19)
#define DISPC_IRQ_VID3_FIFO_UNDERFLOW	(1 << 20)
#define DISPC_IRQ_ACBIAS_COUNT_STAT2	(1 << 21)
#define DISPC_IRQ_FRAMEDONE2		(1 << 22)
#define DISPC_IRQ_FRAMEDONEWB		(1 << 23)
#define DISPC_IRQ_FRAMEDONETV		(1 << 24)
#define DISPC_IRQ_WBBUFFEROVERFLOW	(1 << 25)
#define DISPC_IRQ_WBUNCOMPLETEERROR	(1 << 26)
#define DISPC_IRQ_SYNC_LOST3		(1 << 27)
#define DISPC_IRQ_VSYNC3		(1 << 28)
#define DISPC_IRQ_ACBIAS_COUNT_STAT3	(1 << 29)
#define DISPC_IRQ_FRAMEDONE3		(1 << 30)

struct dss_device;
struct omap_drm_private;
struct omap_dss_device;
struct dispc_device;
struct dss_device;
struct dss_lcd_mgr_config;
struct snd_aes_iec958;
struct snd_cea_861_aud_if;
struct hdmi_avi_infoframe;
struct drm_connector;

enum omap_display_type {
	OMAP_DISPLAY_TYPE_NONE		= 0,
	OMAP_DISPLAY_TYPE_DPI		= 1 << 0,
	OMAP_DISPLAY_TYPE_DBI		= 1 << 1,
	OMAP_DISPLAY_TYPE_SDI		= 1 << 2,
	OMAP_DISPLAY_TYPE_DSI		= 1 << 3,
	OMAP_DISPLAY_TYPE_VENC		= 1 << 4,
	OMAP_DISPLAY_TYPE_HDMI		= 1 << 5,
	OMAP_DISPLAY_TYPE_DVI		= 1 << 6,
};

enum omap_plane_id {
	OMAP_DSS_GFX	= 0,
	OMAP_DSS_VIDEO1	= 1,
	OMAP_DSS_VIDEO2	= 2,
	OMAP_DSS_VIDEO3	= 3,
	OMAP_DSS_WB	= 4,
};

enum omap_channel {
	OMAP_DSS_CHANNEL_LCD	= 0,
	OMAP_DSS_CHANNEL_DIGIT	= 1,
	OMAP_DSS_CHANNEL_LCD2	= 2,
	OMAP_DSS_CHANNEL_LCD3	= 3,
	OMAP_DSS_CHANNEL_WB	= 4,
};

enum omap_color_mode {
	_UNUSED_,
};

enum omap_dss_load_mode {
	OMAP_DSS_LOAD_CLUT_AND_FRAME	= 0,
	OMAP_DSS_LOAD_CLUT_ONLY		= 1,
	OMAP_DSS_LOAD_FRAME_ONLY	= 2,
	OMAP_DSS_LOAD_CLUT_ONCE_FRAME	= 3,
};

enum omap_dss_trans_key_type {
	OMAP_DSS_COLOR_KEY_GFX_DST = 0,
	OMAP_DSS_COLOR_KEY_VID_SRC = 1,
};

enum omap_dss_signal_level {
	OMAPDSS_SIG_ACTIVE_LOW,
	OMAPDSS_SIG_ACTIVE_HIGH,
};

enum omap_dss_signal_edge {
	OMAPDSS_DRIVE_SIG_FALLING_EDGE,
	OMAPDSS_DRIVE_SIG_RISING_EDGE,
};

enum omap_dss_venc_type {
	OMAP_DSS_VENC_TYPE_COMPOSITE,
	OMAP_DSS_VENC_TYPE_SVIDEO,
};

enum omap_dss_dsi_pixel_format {
	OMAP_DSS_DSI_FMT_RGB888,
	OMAP_DSS_DSI_FMT_RGB666,
	OMAP_DSS_DSI_FMT_RGB666_PACKED,
	OMAP_DSS_DSI_FMT_RGB565,
};

enum omap_dss_dsi_mode {
	OMAP_DSS_DSI_CMD_MODE = 0,
	OMAP_DSS_DSI_VIDEO_MODE,
};

enum omap_display_caps {
	OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE	= 1 << 0,
	OMAP_DSS_DISPLAY_CAP_TEAR_ELIM		= 1 << 1,
};

enum omap_dss_display_state {
	OMAP_DSS_DISPLAY_DISABLED = 0,
	OMAP_DSS_DISPLAY_ACTIVE,
};

enum omap_dss_rotation_type {
	OMAP_DSS_ROT_NONE	= 0,
	OMAP_DSS_ROT_TILER	= 1 << 0,
};

enum omap_overlay_caps {
	OMAP_DSS_OVL_CAP_SCALE = 1 << 0,
	OMAP_DSS_OVL_CAP_GLOBAL_ALPHA = 1 << 1,
	OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA = 1 << 2,
	OMAP_DSS_OVL_CAP_ZORDER = 1 << 3,
	OMAP_DSS_OVL_CAP_POS = 1 << 4,
	OMAP_DSS_OVL_CAP_REPLICATION = 1 << 5,
};

enum omap_dss_output_id {
	OMAP_DSS_OUTPUT_DPI	= 1 << 0,
	OMAP_DSS_OUTPUT_DBI	= 1 << 1,
	OMAP_DSS_OUTPUT_SDI	= 1 << 2,
	OMAP_DSS_OUTPUT_DSI1	= 1 << 3,
	OMAP_DSS_OUTPUT_DSI2	= 1 << 4,
	OMAP_DSS_OUTPUT_VENC	= 1 << 5,
	OMAP_DSS_OUTPUT_HDMI	= 1 << 6,
};

/* DSI */

enum omap_dss_dsi_trans_mode {
	/* Sync Pulses: both sync start and end packets sent */
	OMAP_DSS_DSI_PULSE_MODE,
	/* Sync Events: only sync start packets sent */
	OMAP_DSS_DSI_EVENT_MODE,
	/* Burst: only sync start packets sent, pixels are time compressed */
	OMAP_DSS_DSI_BURST_MODE,
};

struct omap_dss_dsi_videomode_timings {
	unsigned long hsclk;

	unsigned int ndl;
	unsigned int bitspp;

	/* pixels */
	u16 hact;
	/* lines */
	u16 vact;

	/* DSI video mode blanking data */
	/* Unit: byte clock cycles */
	u16 hss;
	u16 hsa;
	u16 hse;
	u16 hfp;
	u16 hbp;
	/* Unit: line clocks */
	u16 vsa;
	u16 vfp;
	u16 vbp;

	/* DSI blanking modes */
	int blanking_mode;
	int hsa_blanking_mode;
	int hbp_blanking_mode;
	int hfp_blanking_mode;

	enum omap_dss_dsi_trans_mode trans_mode;

	bool ddr_clk_always_on;
	int window_sync;
};

struct omap_dss_dsi_config {
	enum omap_dss_dsi_mode mode;
	enum omap_dss_dsi_pixel_format pixel_format;
	const struct videomode *vm;

	unsigned long hs_clk_min, hs_clk_max;
	unsigned long lp_clk_min, lp_clk_max;

	bool ddr_clk_always_on;
	enum omap_dss_dsi_trans_mode trans_mode;
};

struct omap_dss_cpr_coefs {
	s16 rr, rg, rb;
	s16 gr, gg, gb;
	s16 br, bg, bb;
};

struct omap_overlay_info {
	dma_addr_t paddr;
	dma_addr_t p_uv_addr;  /* for NV12 format */
	u16 screen_width;
	u16 width;
	u16 height;
	u32 fourcc;
	u8 rotation;
	enum omap_dss_rotation_type rotation_type;

	u16 pos_x;
	u16 pos_y;
	u16 out_width;	/* if 0, out_width == width */
	u16 out_height;	/* if 0, out_height == height */
	u8 global_alpha;
	u8 pre_mult_alpha;
	u8 zorder;
};

struct omap_overlay_manager_info {
	u32 default_color;

	enum omap_dss_trans_key_type trans_key_type;
	u32 trans_key;
	bool trans_enabled;

	bool partial_alpha_enabled;

	bool cpr_enable;
	struct omap_dss_cpr_coefs cpr_coefs;
};

/* 22 pins means 1 clk lane and 10 data lanes */
#define OMAP_DSS_MAX_DSI_PINS 22

struct omap_dsi_pin_config {
	int num_pins;
	/*
	 * pin numbers in the following order:
	 * clk+, clk-
	 * data1+, data1-
	 * data2+, data2-
	 * ...
	 */
	int pins[OMAP_DSS_MAX_DSI_PINS];
};

struct omap_dss_writeback_info {
	u32 paddr;
	u32 p_uv_addr;
	u16 buf_width;
	u16 width;
	u16 height;
	u32 fourcc;
	u8 rotation;
	enum omap_dss_rotation_type rotation_type;
	u8 pre_mult_alpha;
};

struct omapdss_dsi_ops {
	void (*disable)(struct omap_dss_device *dssdev, bool disconnect_lanes,
			bool enter_ulps);

	/* bus configuration */
	int (*set_config)(struct omap_dss_device *dssdev,
			const struct omap_dss_dsi_config *cfg);
	int (*configure_pins)(struct omap_dss_device *dssdev,
			const struct omap_dsi_pin_config *pin_cfg);

	void (*enable_hs)(struct omap_dss_device *dssdev, int channel,
			bool enable);
	int (*enable_te)(struct omap_dss_device *dssdev, bool enable);

	int (*update)(struct omap_dss_device *dssdev, int channel,
			void (*callback)(int, void *), void *data);

	void (*bus_lock)(struct omap_dss_device *dssdev);
	void (*bus_unlock)(struct omap_dss_device *dssdev);

	int (*enable_video_output)(struct omap_dss_device *dssdev, int channel);
	void (*disable_video_output)(struct omap_dss_device *dssdev,
			int channel);

	int (*request_vc)(struct omap_dss_device *dssdev, int *channel);
	int (*set_vc_id)(struct omap_dss_device *dssdev, int channel,
			int vc_id);
	void (*release_vc)(struct omap_dss_device *dssdev, int channel);

	/* data transfer */
	int (*dcs_write)(struct omap_dss_device *dssdev, int channel,
			u8 *data, int len);
	int (*dcs_write_nosync)(struct omap_dss_device *dssdev, int channel,
			u8 *data, int len);
	int (*dcs_read)(struct omap_dss_device *dssdev, int channel, u8 dcs_cmd,
			u8 *data, int len);

	int (*gen_write)(struct omap_dss_device *dssdev, int channel,
			u8 *data, int len);
	int (*gen_write_nosync)(struct omap_dss_device *dssdev, int channel,
			u8 *data, int len);
	int (*gen_read)(struct omap_dss_device *dssdev, int channel,
			u8 *reqdata, int reqlen,
			u8 *data, int len);

	int (*bta_sync)(struct omap_dss_device *dssdev, int channel);

	int (*set_max_rx_packet_size)(struct omap_dss_device *dssdev,
			int channel, u16 plen);
};

struct omap_dss_device_ops {
	int (*connect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);
	void (*disconnect)(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst);

	void (*enable)(struct omap_dss_device *dssdev);
	void (*disable)(struct omap_dss_device *dssdev);

	int (*check_timings)(struct omap_dss_device *dssdev,
			     struct drm_display_mode *mode);

	int (*get_modes)(struct omap_dss_device *dssdev,
			 struct drm_connector *connector);

	const struct omapdss_dsi_ops dsi;
};

/**
 * enum omap_dss_device_ops_flag - Indicates which device ops are supported
 * @OMAP_DSS_DEVICE_OP_MODES: The device supports reading modes
 */
enum omap_dss_device_ops_flag {
	OMAP_DSS_DEVICE_OP_MODES = BIT(3),
};

struct omap_dss_device {
	struct device *dev;

	struct module *owner;

	struct dss_device *dss;
	struct omap_dss_device *next;
	struct drm_bridge *bridge;
	struct drm_bridge *next_bridge;
	struct drm_panel *panel;

	struct list_head list;

	/*
	 * DSS type that this device generates (for DSS internal devices) or
	 * requires (for external encoders, connectors and panels). Must be a
	 * non-zero (different than OMAP_DISPLAY_TYPE_NONE) value.
	 */
	enum omap_display_type type;

	/*
	 * True if the device is a display (panel or connector) at the end of
	 * the pipeline, false otherwise.
	 */
	bool display;

	const char *name;

	const struct omap_dss_driver *driver;
	const struct omap_dss_device_ops *ops;
	unsigned long ops_flags;
	u32 bus_flags;

	enum omap_display_caps caps;

	enum omap_dss_display_state state;

	/* OMAP DSS output specific fields */

	/* DISPC channel for this output */
	enum omap_channel dispc_channel;

	/* output instance */
	enum omap_dss_output_id id;

	/* port number in DT */
	unsigned int of_port;
};

struct omap_dss_driver {
	int (*update)(struct omap_dss_device *dssdev,
			       u16 x, u16 y, u16 w, u16 h);
	int (*sync)(struct omap_dss_device *dssdev);

	int (*enable_te)(struct omap_dss_device *dssdev, bool enable);
	int (*get_te)(struct omap_dss_device *dssdev);

	int (*memory_read)(struct omap_dss_device *dssdev,
			void *buf, size_t size,
			u16 x, u16 y, u16 w, u16 h);
};

struct dss_device *omapdss_get_dss(void);
void omapdss_set_dss(struct dss_device *dss);
static inline bool omapdss_is_initialized(void)
{
	return !!omapdss_get_dss();
}

void omapdss_display_init(struct omap_dss_device *dssdev);
int omapdss_display_get_modes(struct drm_connector *connector,
			      const struct videomode *vm);

void omapdss_device_register(struct omap_dss_device *dssdev);
void omapdss_device_unregister(struct omap_dss_device *dssdev);
struct omap_dss_device *omapdss_device_get(struct omap_dss_device *dssdev);
void omapdss_device_put(struct omap_dss_device *dssdev);
struct omap_dss_device *omapdss_find_device_by_node(struct device_node *node);
int omapdss_device_connect(struct dss_device *dss,
			   struct omap_dss_device *src,
			   struct omap_dss_device *dst);
void omapdss_device_disconnect(struct omap_dss_device *src,
			       struct omap_dss_device *dst);
void omapdss_device_enable(struct omap_dss_device *dssdev);
void omapdss_device_disable(struct omap_dss_device *dssdev);

int omap_dss_get_num_overlay_managers(void);

int omap_dss_get_num_overlays(void);

#define for_each_dss_output(d) \
	while ((d = omapdss_device_next_output(d)) != NULL)
struct omap_dss_device *omapdss_device_next_output(struct omap_dss_device *from);
int omapdss_device_init_output(struct omap_dss_device *out,
			       struct drm_bridge *local_bridge);
void omapdss_device_cleanup_output(struct omap_dss_device *out);

typedef void (*omap_dispc_isr_t) (void *arg, u32 mask);
int omap_dispc_register_isr(omap_dispc_isr_t isr, void *arg, u32 mask);
int omap_dispc_unregister_isr(omap_dispc_isr_t isr, void *arg, u32 mask);

int omapdss_compat_init(void);
void omapdss_compat_uninit(void);

static inline bool omapdss_device_is_enabled(struct omap_dss_device *dssdev)
{
	return dssdev->state == OMAP_DSS_DISPLAY_ACTIVE;
}

enum dss_writeback_channel {
	DSS_WB_LCD1_MGR =	0,
	DSS_WB_LCD2_MGR =	1,
	DSS_WB_TV_MGR =		2,
	DSS_WB_OVL0 =		3,
	DSS_WB_OVL1 =		4,
	DSS_WB_OVL2 =		5,
	DSS_WB_OVL3 =		6,
	DSS_WB_LCD3_MGR =	7,
};

struct dss_mgr_ops {
	void (*start_update)(struct omap_drm_private *priv,
			     enum omap_channel channel);
	int (*enable)(struct omap_drm_private *priv,
		      enum omap_channel channel);
	void (*disable)(struct omap_drm_private *priv,
			enum omap_channel channel);
	void (*set_timings)(struct omap_drm_private *priv,
			    enum omap_channel channel,
			    const struct videomode *vm);
	void (*set_lcd_config)(struct omap_drm_private *priv,
			       enum omap_channel channel,
			       const struct dss_lcd_mgr_config *config);
	int (*register_framedone_handler)(struct omap_drm_private *priv,
			enum omap_channel channel,
			void (*handler)(void *), void *data);
	void (*unregister_framedone_handler)(struct omap_drm_private *priv,
			enum omap_channel channel,
			void (*handler)(void *), void *data);
};

int dss_install_mgr_ops(struct dss_device *dss,
			const struct dss_mgr_ops *mgr_ops,
			struct omap_drm_private *priv);
void dss_uninstall_mgr_ops(struct dss_device *dss);

void dss_mgr_set_timings(struct omap_dss_device *dssdev,
		const struct videomode *vm);
void dss_mgr_set_lcd_config(struct omap_dss_device *dssdev,
		const struct dss_lcd_mgr_config *config);
int dss_mgr_enable(struct omap_dss_device *dssdev);
void dss_mgr_disable(struct omap_dss_device *dssdev);
void dss_mgr_start_update(struct omap_dss_device *dssdev);
int dss_mgr_register_framedone_handler(struct omap_dss_device *dssdev,
		void (*handler)(void *), void *data);
void dss_mgr_unregister_framedone_handler(struct omap_dss_device *dssdev,
		void (*handler)(void *), void *data);

/* dispc ops */

struct dispc_ops {
	u32 (*read_irqstatus)(struct dispc_device *dispc);
	void (*clear_irqstatus)(struct dispc_device *dispc, u32 mask);
	void (*write_irqenable)(struct dispc_device *dispc, u32 mask);

	int (*request_irq)(struct dispc_device *dispc, irq_handler_t handler,
			   void *dev_id);
	void (*free_irq)(struct dispc_device *dispc, void *dev_id);

	int (*runtime_get)(struct dispc_device *dispc);
	void (*runtime_put)(struct dispc_device *dispc);

	int (*get_num_ovls)(struct dispc_device *dispc);
	int (*get_num_mgrs)(struct dispc_device *dispc);

	u32 (*get_memory_bandwidth_limit)(struct dispc_device *dispc);

	void (*mgr_enable)(struct dispc_device *dispc,
			   enum omap_channel channel, bool enable);
	bool (*mgr_is_enabled)(struct dispc_device *dispc,
			       enum omap_channel channel);
	u32 (*mgr_get_vsync_irq)(struct dispc_device *dispc,
				 enum omap_channel channel);
	u32 (*mgr_get_framedone_irq)(struct dispc_device *dispc,
				     enum omap_channel channel);
	u32 (*mgr_get_sync_lost_irq)(struct dispc_device *dispc,
				     enum omap_channel channel);
	bool (*mgr_go_busy)(struct dispc_device *dispc,
			    enum omap_channel channel);
	void (*mgr_go)(struct dispc_device *dispc, enum omap_channel channel);
	void (*mgr_set_lcd_config)(struct dispc_device *dispc,
				   enum omap_channel channel,
				   const struct dss_lcd_mgr_config *config);
	int (*mgr_check_timings)(struct dispc_device *dispc,
				 enum omap_channel channel,
				 const struct videomode *vm);
	void (*mgr_set_timings)(struct dispc_device *dispc,
				enum omap_channel channel,
				const struct videomode *vm);
	void (*mgr_setup)(struct dispc_device *dispc, enum omap_channel channel,
			  const struct omap_overlay_manager_info *info);
	u32 (*mgr_gamma_size)(struct dispc_device *dispc,
			      enum omap_channel channel);
	void (*mgr_set_gamma)(struct dispc_device *dispc,
			      enum omap_channel channel,
			      const struct drm_color_lut *lut,
			      unsigned int length);

	int (*ovl_enable)(struct dispc_device *dispc, enum omap_plane_id plane,
			  bool enable);
	int (*ovl_setup)(struct dispc_device *dispc, enum omap_plane_id plane,
			 const struct omap_overlay_info *oi,
			 const struct videomode *vm, bool mem_to_mem,
			 enum omap_channel channel);

	const u32 *(*ovl_get_color_modes)(struct dispc_device *dispc,
					  enum omap_plane_id plane);

	u32 (*wb_get_framedone_irq)(struct dispc_device *dispc);
	int (*wb_setup)(struct dispc_device *dispc,
		const struct omap_dss_writeback_info *wi,
		bool mem_to_mem, const struct videomode *vm,
		enum dss_writeback_channel channel_in);
	bool (*has_writeback)(struct dispc_device *dispc);
	bool (*wb_go_busy)(struct dispc_device *dispc);
	void (*wb_go)(struct dispc_device *dispc);
};

struct dispc_device *dispc_get_dispc(struct dss_device *dss);
const struct dispc_ops *dispc_get_ops(struct dss_device *dss);

bool omapdss_stack_is_ready(void);
void omapdss_gather_components(struct device *dev);

#endif /* __OMAP_DRM_DSS_H */
