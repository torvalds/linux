/*
 * Greybus audio driver
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __LINUX_GBAUDIO_CODEC_H
#define __LINUX_GBAUDIO_CODEC_H

#include <sound/soc.h>

#include "greybus.h"
#include "greybus_protocols.h"

#define NAME_SIZE	32
#define MAX_DAIS	2	/* APB1, APB2 */

enum {
	APB1_PCM = 0,
	APB2_PCM,
	NUM_CODEC_DAIS,
};

enum gbcodec_reg_index {
	GBCODEC_CTL_REG,
	GBCODEC_MUTE_REG,
	GBCODEC_PB_LVOL_REG,
	GBCODEC_PB_RVOL_REG,
	GBCODEC_CAP_LVOL_REG,
	GBCODEC_CAP_RVOL_REG,
	GBCODEC_APB1_MUX_REG,
	GBCODEC_APB2_MUX_REG,
	GBCODEC_REG_COUNT
};

/* bit 0-SPK, 1-HP, 2-DAC,
 * 4-MIC, 5-HSMIC, 6-MIC2
 */
#define GBCODEC_CTL_REG_DEFAULT		0x00

/* bit 0,1 - APB1-PB-L/R
 * bit 2,3 - APB2-PB-L/R
 * bit 4,5 - APB1-Cap-L/R
 * bit 6,7 - APB2-Cap-L/R
 */
#define	GBCODEC_MUTE_REG_DEFAULT	0x00

/* 0-127 steps */
#define	GBCODEC_PB_VOL_REG_DEFAULT	0x00
#define	GBCODEC_CAP_VOL_REG_DEFAULT	0x00

/* bit 0,1,2 - PB stereo, left, right
 * bit 8,9,10 - Cap stereo, left, right
 */
#define GBCODEC_APB1_MUX_REG_DEFAULT	0x00
#define GBCODEC_APB2_MUX_REG_DEFAULT	0x00

static const u8 gbcodec_reg_defaults[GBCODEC_REG_COUNT] = {
	GBCODEC_CTL_REG_DEFAULT,
	GBCODEC_MUTE_REG_DEFAULT,
	GBCODEC_PB_VOL_REG_DEFAULT,
	GBCODEC_PB_VOL_REG_DEFAULT,
	GBCODEC_CAP_VOL_REG_DEFAULT,
	GBCODEC_CAP_VOL_REG_DEFAULT,
	GBCODEC_APB1_MUX_REG_DEFAULT,
	GBCODEC_APB2_MUX_REG_DEFAULT,
};

struct gbaudio_widget {
	__u8 id;
	const char *name;
	struct list_head list;
};

struct gbaudio_control {
	__u8 id;
	char *name;
	const char * const *texts;
	struct list_head list;
};

struct gbaudio_dai {
	__le16 data_cport;
	char name[NAME_SIZE];
	/* DAI users */
	atomic_t users;
	struct gb_connection *connection;
	struct list_head list;
};

struct gbaudio_codec_info {
	/* module info */
	int dev_id;	/* check if it should be bundle_id/hd_cport_id */
	int vid;
	int pid;
	int slot;
	int type;
	int dai_added;
	int codec_registered;
	int set_uevent;
	char vstr[NAME_SIZE];
	char pstr[NAME_SIZE];
	struct list_head list;
	struct gb_audio_topology *topology;
	/* need to share this info to above user space */
	int manager_id;
	char name[NAME_SIZE];

	/*
	 * there can be a rece condition between gb_audio_disconnect()
	 * and dai->trigger from above ASoC layer.
	 * To avoid any deadlock over codec_info->lock, atomic variable
	 * is used.
	 */
	atomic_t is_connected;
	struct mutex lock;

	/* soc related data */
	struct snd_soc_codec *codec;
	struct device *dev;
	u8 reg[GBCODEC_REG_COUNT];

	/* dai_link related */
	char card_name[NAME_SIZE];
	char *dailink_name[MAX_DAIS];
	int num_dai_links;

	struct gb_connection *mgmt_connection;
	size_t num_data_connections;
	/* topology related */
	int num_dais;
	int num_kcontrols;
	int num_dapm_widgets;
	int num_dapm_routes;
	unsigned long dai_offset;
	unsigned long widget_offset;
	unsigned long control_offset;
	unsigned long route_offset;
	struct snd_kcontrol_new *kctls;
	struct snd_soc_dapm_widget *widgets;
	struct snd_soc_dapm_route *routes;
	struct snd_soc_dai_driver *dais;

	/* lists */
	struct list_head dai_list;
	struct list_head widget_list;
	struct list_head codec_ctl_list;
	struct list_head widget_ctl_list;
};

struct gbaudio_dai *gbaudio_find_dai(struct gbaudio_codec_info *gbcodec,
				     int data_cport, const char *name);
int gbaudio_tplg_parse_data(struct gbaudio_codec_info *gbcodec,
			       struct gb_audio_topology *tplg_data);
void gbaudio_tplg_release(struct gbaudio_codec_info *gbcodec);

/* protocol related */
extern int gb_audio_gb_get_topology(struct gb_connection *connection,
				    struct gb_audio_topology **topology);
extern int gb_audio_gb_get_control(struct gb_connection *connection,
				   uint8_t control_id, uint8_t index,
				   struct gb_audio_ctl_elem_value *value);
extern int gb_audio_gb_set_control(struct gb_connection *connection,
				   uint8_t control_id, uint8_t index,
				   struct gb_audio_ctl_elem_value *value);
extern int gb_audio_gb_enable_widget(struct gb_connection *connection,
				     uint8_t widget_id);
extern int gb_audio_gb_disable_widget(struct gb_connection *connection,
				      uint8_t widget_id);
extern int gb_audio_gb_get_pcm(struct gb_connection *connection,
			       uint16_t data_cport, uint32_t *format,
			       uint32_t *rate, uint8_t *channels,
			       uint8_t *sig_bits);
extern int gb_audio_gb_set_pcm(struct gb_connection *connection,
			       uint16_t data_cport, uint32_t format,
			       uint32_t rate, uint8_t channels,
			       uint8_t sig_bits);
extern int gb_audio_gb_set_tx_data_size(struct gb_connection *connection,
					uint16_t data_cport, uint16_t size);
extern int gb_audio_gb_get_tx_delay(struct gb_connection *connection,
				    uint16_t data_cport, uint32_t *delay);
extern int gb_audio_gb_activate_tx(struct gb_connection *connection,
				   uint16_t data_cport);
extern int gb_audio_gb_deactivate_tx(struct gb_connection *connection,
				     uint16_t data_cport);
extern int gb_audio_gb_set_rx_data_size(struct gb_connection *connection,
					uint16_t data_cport, uint16_t size);
extern int gb_audio_gb_get_rx_delay(struct gb_connection *connection,
				    uint16_t data_cport, uint32_t *delay);
extern int gb_audio_gb_activate_rx(struct gb_connection *connection,
				   uint16_t data_cport);
extern int gb_audio_gb_deactivate_rx(struct gb_connection *connection,
				     uint16_t data_cport);
extern int gb_audio_apbridgea_set_config(struct gb_connection *connection,
					 __u16 i2s_port, __u32 format,
					 __u32 rate, __u32 mclk_freq);
extern int gb_audio_apbridgea_register_cport(struct gb_connection *connection,
					     __u16 i2s_port, __u16 cportid);
extern int gb_audio_apbridgea_unregister_cport(struct gb_connection *connection,
					       __u16 i2s_port, __u16 cportid);
extern int gb_audio_apbridgea_set_tx_data_size(struct gb_connection *connection,
					       __u16 i2s_port, __u16 size);
extern int gb_audio_apbridgea_get_tx_delay(struct gb_connection *connection,
					   __u16 i2s_port, __u32 *delay);
extern int gb_audio_apbridgea_start_tx(struct gb_connection *connection,
				       __u16 i2s_port, __u64 timestamp);
extern int gb_audio_apbridgea_stop_tx(struct gb_connection *connection,
				      __u16 i2s_port);
extern int gb_audio_apbridgea_set_rx_data_size(struct gb_connection *connection,
					       __u16 i2s_port, __u16 size);
extern int gb_audio_apbridgea_get_rx_delay(struct gb_connection *connection,
					   __u16 i2s_port, __u32 *delay);
extern int gb_audio_apbridgea_start_rx(struct gb_connection *connection,
				       __u16 i2s_port);
extern int gb_audio_apbridgea_stop_rx(struct gb_connection *connection,
				      __u16 i2s_port);

#endif /* __LINUX_GBAUDIO_CODEC_H */
