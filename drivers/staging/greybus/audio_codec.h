// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus audio driver
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 */

#ifndef __LINUX_GBAUDIO_CODEC_H
#define __LINUX_GBAUDIO_CODEC_H

#include <sound/soc.h>
#include <sound/jack.h>

#include "greybus.h"
#include "greybus_protocols.h"

#define NAME_SIZE	32
#define MAX_DAIS	2	/* APB1, APB2 */

enum {
	APB1_PCM = 0,
	APB2_PCM,
	NUM_CODEC_DAIS,
};

/* device_type should be same as defined in audio.h (Android media layer) */
enum {
	GBAUDIO_DEVICE_NONE                     = 0x0,
	/* reserved bits */
	GBAUDIO_DEVICE_BIT_IN                   = 0x80000000,
	GBAUDIO_DEVICE_BIT_DEFAULT              = 0x40000000,
	/* output devices */
	GBAUDIO_DEVICE_OUT_SPEAKER              = 0x2,
	GBAUDIO_DEVICE_OUT_WIRED_HEADSET        = 0x4,
	GBAUDIO_DEVICE_OUT_WIRED_HEADPHONE      = 0x8,
	/* input devices */
	GBAUDIO_DEVICE_IN_BUILTIN_MIC           = GBAUDIO_DEVICE_BIT_IN | 0x4,
	GBAUDIO_DEVICE_IN_WIRED_HEADSET         = GBAUDIO_DEVICE_BIT_IN | 0x10,
};

#define GBCODEC_JACK_MASK		0x0000FFFF
#define GBCODEC_JACK_BUTTON_MASK	0xFFFF0000

enum gbaudio_codec_state {
	GBAUDIO_CODEC_SHUTDOWN = 0,
	GBAUDIO_CODEC_STARTUP,
	GBAUDIO_CODEC_HWPARAMS,
	GBAUDIO_CODEC_PREPARE,
	GBAUDIO_CODEC_START,
	GBAUDIO_CODEC_STOP,
};

struct gbaudio_stream_params {
	int state;
	u8 sig_bits, channels;
	uint32_t format, rate;
};

struct gbaudio_codec_dai {
	int id;
	/* runtime params for playback/capture streams */
	struct gbaudio_stream_params params[2];
	struct list_head list;
};

struct gbaudio_codec_info {
	struct device *dev;
	struct snd_soc_codec *codec;
	struct list_head module_list;
	/* to maintain runtime stream params for each DAI */
	struct list_head dai_list;
	struct mutex lock;
};

struct gbaudio_widget {
	__u8 id;
	const char *name;
	struct list_head list;
};

struct gbaudio_control {
	__u8 id;
	char *name;
	char *wname;
	const char * const *texts;
	int items;
	struct list_head list;
};

struct gbaudio_data_connection {
	int id;
	__le16 data_cport;
	struct gb_connection *connection;
	struct list_head list;
	/* maintain runtime state for playback/capture stream */
	int state[2];
};

/* stream direction */
#define GB_PLAYBACK	BIT(0)
#define GB_CAPTURE	BIT(1)

enum gbaudio_module_state {
	GBAUDIO_MODULE_OFF = 0,
	GBAUDIO_MODULE_ON,
};

struct gbaudio_module_info {
	/* module info */
	struct device *dev;
	int dev_id;	/* check if it should be bundle_id/hd_cport_id */
	int vid;
	int pid;
	int type;
	int set_uevent;
	char vstr[NAME_SIZE];
	char pstr[NAME_SIZE];
	struct list_head list;
	/* need to share this info to above user space */
	int manager_id;
	char name[NAME_SIZE];
	unsigned int ip_devices;
	unsigned int op_devices;

	/* jack related */
	char jack_name[NAME_SIZE];
	char button_name[NAME_SIZE];
	int jack_type;
	int jack_mask;
	int button_mask;
	int button_status;
	struct snd_soc_jack headset_jack;
	struct snd_soc_jack button_jack;

	/* connection info */
	struct gb_connection *mgmt_connection;
	size_t num_data_connections;
	struct list_head data_list;

	/* topology related */
	int num_dais;
	int num_controls;
	int num_dapm_widgets;
	int num_dapm_routes;
	unsigned long dai_offset;
	unsigned long widget_offset;
	unsigned long control_offset;
	unsigned long route_offset;
	struct snd_kcontrol_new *controls;
	struct snd_soc_dapm_widget *dapm_widgets;
	struct snd_soc_dapm_route *dapm_routes;
	struct snd_soc_dai_driver *dais;

	struct list_head widget_list;
	struct list_head ctl_list;
	struct list_head widget_ctl_list;

	struct gb_audio_topology *topology;
};

int gbaudio_tplg_parse_data(struct gbaudio_module_info *module,
			       struct gb_audio_topology *tplg_data);
void gbaudio_tplg_release(struct gbaudio_module_info *module);

int gbaudio_module_update(struct gbaudio_codec_info *codec,
			  struct snd_soc_dapm_widget *w,
			  struct gbaudio_module_info *module,
			  int enable);
int gbaudio_register_module(struct gbaudio_module_info *module);
void gbaudio_unregister_module(struct gbaudio_module_info *module);

/* protocol related */
extern int gb_audio_gb_get_topology(struct gb_connection *connection,
				    struct gb_audio_topology **topology);
extern int gb_audio_gb_get_control(struct gb_connection *connection,
				   u8 control_id, u8 index,
				   struct gb_audio_ctl_elem_value *value);
extern int gb_audio_gb_set_control(struct gb_connection *connection,
				   u8 control_id, u8 index,
				   struct gb_audio_ctl_elem_value *value);
extern int gb_audio_gb_enable_widget(struct gb_connection *connection,
				     u8 widget_id);
extern int gb_audio_gb_disable_widget(struct gb_connection *connection,
				      u8 widget_id);
extern int gb_audio_gb_get_pcm(struct gb_connection *connection,
			       u16 data_cport, uint32_t *format,
			       uint32_t *rate, u8 *channels,
			       u8 *sig_bits);
extern int gb_audio_gb_set_pcm(struct gb_connection *connection,
			       u16 data_cport, uint32_t format,
			       uint32_t rate, u8 channels,
			       u8 sig_bits);
extern int gb_audio_gb_set_tx_data_size(struct gb_connection *connection,
					u16 data_cport, u16 size);
extern int gb_audio_gb_activate_tx(struct gb_connection *connection,
				   u16 data_cport);
extern int gb_audio_gb_deactivate_tx(struct gb_connection *connection,
				     u16 data_cport);
extern int gb_audio_gb_set_rx_data_size(struct gb_connection *connection,
					u16 data_cport, u16 size);
extern int gb_audio_gb_activate_rx(struct gb_connection *connection,
				   u16 data_cport);
extern int gb_audio_gb_deactivate_rx(struct gb_connection *connection,
				     u16 data_cport);
extern int gb_audio_apbridgea_set_config(struct gb_connection *connection,
					 __u16 i2s_port, __u32 format,
					 __u32 rate, __u32 mclk_freq);
extern int gb_audio_apbridgea_register_cport(struct gb_connection *connection,
					     __u16 i2s_port, __u16 cportid,
					     __u8 direction);
extern int gb_audio_apbridgea_unregister_cport(struct gb_connection *connection,
					       __u16 i2s_port, __u16 cportid,
					       __u8 direction);
extern int gb_audio_apbridgea_set_tx_data_size(struct gb_connection *connection,
					       __u16 i2s_port, __u16 size);
extern int gb_audio_apbridgea_prepare_tx(struct gb_connection *connection,
					 __u16 i2s_port);
extern int gb_audio_apbridgea_start_tx(struct gb_connection *connection,
				       __u16 i2s_port, __u64 timestamp);
extern int gb_audio_apbridgea_stop_tx(struct gb_connection *connection,
				      __u16 i2s_port);
extern int gb_audio_apbridgea_shutdown_tx(struct gb_connection *connection,
					  __u16 i2s_port);
extern int gb_audio_apbridgea_set_rx_data_size(struct gb_connection *connection,
					       __u16 i2s_port, __u16 size);
extern int gb_audio_apbridgea_prepare_rx(struct gb_connection *connection,
					 __u16 i2s_port);
extern int gb_audio_apbridgea_start_rx(struct gb_connection *connection,
				       __u16 i2s_port);
extern int gb_audio_apbridgea_stop_rx(struct gb_connection *connection,
				      __u16 i2s_port);
extern int gb_audio_apbridgea_shutdown_rx(struct gb_connection *connection,
					  __u16 i2s_port);

#endif /* __LINUX_GBAUDIO_CODEC_H */
