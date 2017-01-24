/*
 * link_encoder.h
 *
 *  Created on: Oct 6, 2015
 *      Author: yonsun
 */

#ifndef LINK_ENCODER_H_
#define LINK_ENCODER_H_

#include "grph_object_defs.h"
#include "signal_types.h"
#include "dc_types.h"

struct dc_context;
struct encoder_set_dp_phy_pattern_param;
struct link_mst_stream_allocation_table;
struct dc_link_settings;
struct link_training_settings;
struct core_stream;
struct pipe_ctx;

struct encoder_init_data {
	enum channel_id channel;
	struct graphics_object_id connector;
	enum hpd_source_id hpd_source;
	/* TODO: in DAL2, here was pointer to EventManagerInterface */
	struct graphics_object_id encoder;
	struct dc_context *ctx;
	enum transmitter transmitter;
};

struct encoder_feature_support {
	union {
		struct {
			uint32_t IS_HBR2_CAPABLE:1;
			uint32_t IS_HBR3_CAPABLE:1;
			uint32_t IS_TPS3_CAPABLE:1;
			uint32_t IS_TPS4_CAPABLE:1;
			uint32_t IS_YCBCR_CAPABLE:1;
		} bits;
		uint32_t raw;
	} flags;

	enum dc_color_depth max_hdmi_deep_color;
	unsigned int max_hdmi_pixel_clock;
	bool ycbcr420_supported;
};

enum physical_phy_id {
	PHYLD_0,
	PHYLD_1,
	PHYLD_2,
	PHYLD_3,
	PHYLD_4,
	PHYLD_5,
	PHYLD_6,
	PHYLD_7,
	PHYLD_8,
	PHYLD_9,
	PHYLD_COUNT,
	PHYLD_UNKNOWN = (-1L)
};

enum phy_type {
	PHY_TYPE_UNKNOWN  = 1,
	PHY_TYPE_PCIE_PHY = 2,
	PHY_TYPE_UNIPHY = 3,
};

union dmcu_psr_level {
	struct {
		unsigned int SKIP_CRC:1;
		unsigned int SKIP_DP_VID_STREAM_DISABLE:1;
		unsigned int SKIP_PHY_POWER_DOWN:1;
		unsigned int SKIP_AUX_ACK_CHECK:1;
		unsigned int SKIP_CRTC_DISABLE:1;
		unsigned int SKIP_AUX_RFB_CAPTURE_CHECK:1;
		unsigned int SKIP_SMU_NOTIFICATION:1;
		unsigned int SKIP_AUTO_STATE_ADVANCE:1;
		unsigned int DISABLE_PSR_ENTRY_ABORT:1;
		unsigned int RESERVED:23;
	} bits;
	unsigned int u32all;
};

union dpcd_psr_configuration {
	struct {
		unsigned char ENABLE                    : 1;
		unsigned char TRANSMITTER_ACTIVE_IN_PSR : 1;
		unsigned char CRC_VERIFICATION          : 1;
		unsigned char FRAME_CAPTURE_INDICATION  : 1;
		/* For eDP 1.4, PSR v2*/
		unsigned char LINE_CAPTURE_INDICATION   : 1;
		/* For eDP 1.4, PSR v2*/
		unsigned char IRQ_HPD_WITH_CRC_ERROR    : 1;
		unsigned char RESERVED                  : 2;
	} bits;
	unsigned char raw;
};

union psr_error_status {
	struct {
		unsigned char LINK_CRC_ERROR        :1;
		unsigned char RFB_STORAGE_ERROR     :1;
		unsigned char RESERVED              :6;
	} bits;
	unsigned char raw;
};

union psr_sink_psr_status {
	struct {
	unsigned char SINK_SELF_REFRESH_STATUS  :3;
	unsigned char RESERVED                  :5;
	} bits;
	unsigned char raw;
};

struct psr_dmcu_context {
	/* ddc line */
	enum channel_id channel;
	/* Transmitter id */
	enum transmitter transmitterId;
	/* Engine Id is used for Dig Be source select */
	enum engine_id engineId;
	/* Controller Id used for Dig Fe source select */
	enum controller_id controllerId;
	/* Pcie or Uniphy */
	enum phy_type phyType;
	/* Physical PHY Id used by SMU interpretation */
	enum physical_phy_id smuPhyId;
	/* Vertical total pixels from crtc timing.
	 * This is used for static screen detection.
	 * ie. If we want to detect half a frame,
	 * we use this to determine the hyst lines.
	 */
	unsigned int crtcTimingVerticalTotal;
	/* PSR supported from panel capabilities and
	 * current display configuration
	 */
	bool psrSupportedDisplayConfig;
	/* Whether fast link training is supported by the panel */
	bool psrExitLinkTrainingRequired;
	/* If RFB setup time is greater than the total VBLANK time,
	 * it is not possible for the sink to capture the video frame
	 * in the same frame the SDP is sent. In this case,
	 * the frame capture indication bit should be set and an extra
	 * static frame should be transmitted to the sink.
	 */
	bool psrFrameCaptureIndicationReq;
	/* Set the last possible line SDP may be transmitted without violating
	 * the RFB setup time or entering the active video frame.
	 */
	unsigned int sdpTransmitLineNumDeadline;
	/* The VSync rate in Hz used to calculate the
	 * step size for smooth brightness feature
	 */
	unsigned int vsyncRateHz;
	unsigned int skipPsrWaitForPllLock;
	unsigned int numberOfControllers;
	/* Unused, for future use. To indicate that first changed frame from
	 * state3 shouldn't result in psr_inactive, but rather to perform
	 * an automatic single frame rfb_update.
	 */
	bool rfb_update_auto_en;
	/* Number of frame before entering static screen */
	unsigned int timehyst_frames;
	/* Partial frames before entering static screen */
	unsigned int hyst_lines;
	/* # of repeated AUX transaction attempts to make before
	 * indicating failure to the driver
	 */
	unsigned int aux_repeats;
	/* Controls hw blocks to power down during PSR active state */
	union dmcu_psr_level psr_level;
	/* Controls additional delay after remote frame capture before
	 * continuing powerd own
	 */
	unsigned int frame_delay;
};


struct link_encoder {
	const struct link_encoder_funcs *funcs;
	int32_t aux_channel_offset;
	struct dc_context *ctx;
	struct graphics_object_id id;
	struct graphics_object_id connector;
	uint32_t output_signals;
	enum engine_id preferred_engine;
	struct encoder_feature_support features;
	enum transmitter transmitter;
	enum hpd_source_id hpd_source;
};

struct link_encoder_funcs {
	bool (*validate_output_with_stream)(
		struct link_encoder *enc, struct pipe_ctx *pipe_ctx);
	void (*hw_init)(struct link_encoder *enc);
	void (*setup)(struct link_encoder *enc,
		enum signal_type signal);
	void (*enable_tmds_output)(struct link_encoder *enc,
		enum clock_source_id clock_source,
		enum dc_color_depth color_depth,
		bool hdmi,
		bool dual_link,
		uint32_t pixel_clock);
	void (*enable_dp_output)(struct link_encoder *enc,
		const struct dc_link_settings *link_settings,
		enum clock_source_id clock_source);
	void (*enable_dp_mst_output)(struct link_encoder *enc,
		const struct dc_link_settings *link_settings,
		enum clock_source_id clock_source);
	void (*disable_output)(struct link_encoder *link_enc,
		enum signal_type signal);
	void (*dp_set_lane_settings)(struct link_encoder *enc,
		const struct link_training_settings *link_settings);
	void (*dp_set_phy_pattern)(struct link_encoder *enc,
		const struct encoder_set_dp_phy_pattern_param *para);
	void (*update_mst_stream_allocation_table)(
		struct link_encoder *enc,
		const struct link_mst_stream_allocation_table *table);
	void (*set_dmcu_psr_enable)(struct link_encoder *enc, bool enable);
	void (*setup_dmcu_psr)(struct link_encoder *enc,
			struct psr_dmcu_context *psr_context);
	void (*backlight_control) (struct link_encoder *enc,
		bool enable);
	void (*power_control) (struct link_encoder *enc,
		bool power_up);
	void (*connect_dig_be_to_fe)(struct link_encoder *enc,
		enum engine_id engine,
		bool connect);
	void (*enable_hpd)(struct link_encoder *enc);
	void (*disable_hpd)(struct link_encoder *enc);
	void (*destroy)(struct link_encoder **enc);
};

#endif /* LINK_ENCODER_H_ */
