#ifndef __GB_I2S_H__
#define __GB_I2S_H__

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

#define GB_I2S_MGMT_TYPE_PROTOCOL_VERSION		0x01
#define GB_I2S_MGMT_TYPE_GET_SUPPORTED_CONFIGURATIONS	0x02
#define GB_I2S_MGMT_TYPE_SET_CONFIGURATION		0x03
#define GB_I2S_MGMT_TYPE_SET_SAMPLES_PER_MESSAGE	0x04
#define GB_I2S_MGMT_TYPE_GET_PROCESSING_DELAY		0x05
#define GB_I2S_MGMT_TYPE_SET_START_DELAY		0x06
#define GB_I2S_MGMT_TYPE_ACTIVATE_CPORT			0x07
#define GB_I2S_MGMT_TYPE_DEACTIVATE_CPORT		0x08
#define GB_I2S_MGMT_TYPE_REPORT_EVENT			0x09

#define GB_I2S_MGMT_BYTE_ORDER_NA			BIT(0)
#define GB_I2S_MGMT_BYTE_ORDER_BE			BIT(1)
#define GB_I2S_MGMT_BYTE_ORDER_LE			BIT(2)

#define GB_I2S_MGMT_SPATIAL_LOCATION_FL			BIT(0)
#define GB_I2S_MGMT_SPATIAL_LOCATION_FR			BIT(1)
#define GB_I2S_MGMT_SPATIAL_LOCATION_FC			BIT(2)
#define GB_I2S_MGMT_SPATIAL_LOCATION_LFE		BIT(3)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BL			BIT(4)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BR			BIT(5)
#define GB_I2S_MGMT_SPATIAL_LOCATION_FLC		BIT(6)
#define GB_I2S_MGMT_SPATIAL_LOCATION_FRC		BIT(7)
#define GB_I2S_MGMT_SPATIAL_LOCATION_C			BIT(8) /* BC in USB */
#define GB_I2S_MGMT_SPATIAL_LOCATION_SL			BIT(9)
#define GB_I2S_MGMT_SPATIAL_LOCATION_SR			BIT(10)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TC			BIT(11)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFL		BIT(12)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFC		BIT(13)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFR		BIT(14)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TBL		BIT(15)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TBC		BIT(16)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TBR		BIT(17)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFLC		BIT(18)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFRC		BIT(19)
#define GB_I2S_MGMT_SPATIAL_LOCATION_LLFE		BIT(20)
#define GB_I2S_MGMT_SPATIAL_LOCATION_RLFE		BIT(21)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TSL		BIT(22)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TSR		BIT(23)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BC			BIT(24)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BLC		BIT(25)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BRC		BIT(26)
#define GB_I2S_MGMT_SPATIAL_LOCATION_RD			BIT(31)

#define GB_I2S_MGMT_PROTOCOL_PCM			BIT(0)
#define GB_I2S_MGMT_PROTOCOL_I2S			BIT(1)
#define GB_I2S_MGMT_PROTOCOL_LR_STEREO			BIT(2)

#define GB_I2S_MGMT_ROLE_MASTER				BIT(0)
#define GB_I2S_MGMT_ROLE_SLAVE				BIT(1)

#define GB_I2S_MGMT_POLARITY_NORMAL			BIT(0)
#define GB_I2S_MGMT_POLARITY_REVERSED			BIT(1)

#define GB_I2S_MGMT_EDGE_RISING				BIT(0)
#define GB_I2S_MGMT_EDGE_FALLING			BIT(1)

#define GB_I2S_MGMT_EVENT_UNSPECIFIED			0x1
#define GB_I2S_MGMT_EVENT_HALT				0x2
#define GB_I2S_MGMT_EVENT_INTERNAL_ERROR		0x3
#define GB_I2S_MGMT_EVENT_PROTOCOL_ERROR		0x4
#define GB_I2S_MGMT_EVENT_FAILURE			0x5
#define GB_I2S_MGMT_EVENT_OUT_OF_SEQUENCE		0x6
#define GB_I2S_MGMT_EVENT_UNDERRUN			0x7
#define GB_I2S_MGMT_EVENT_OVERRUN			0x8
#define GB_I2S_MGMT_EVENT_CLOCKING			0x9
#define GB_I2S_MGMT_EVENT_DATA_LEN			0xa

struct gb_i2s_mgmt_configuration {
	__le32	sample_frequency;
	__u8	num_channels;
	__u8	bytes_per_channel;
	__u8	byte_order;
	__u8	pad;
	__le32	spatial_locations;
	__le32	ll_protocol;
	__u8	ll_bclk_role;
	__u8	ll_wclk_role;
	__u8	ll_wclk_polarity;
	__u8	ll_wclk_change_edge;
	__u8	ll_wclk_tx_edge;
	__u8	ll_wclk_rx_edge;
	__u8	ll_data_offset;
	__u8	ll_pad;
};

/* get supported configurations request has no payload */
struct gb_i2s_mgmt_get_supported_configurations_response {
	__u8					config_count;
	__u8					pad[3];
	struct gb_i2s_mgmt_configuration	config[0];
};

struct gb_i2s_mgmt_set_configuration_request {
	struct gb_i2s_mgmt_configuration	config;
};
/* set configuration response has no payload */

struct gb_i2s_mgmt_set_samples_per_message_request {
	__le16	samples_per_message;
};
/* set samples per message response has no payload */

/* get processing request delay has no payload */
struct gb_i2s_mgmt_get_processing_delay_response {
	__le32	microseconds;
};

struct gb_i2s_mgmt_set_start_delay_request {
	__le32	microseconds;
};
/* set start delay response has no payload */

struct gb_i2s_mgmt_activate_cport_request {
	__le16	cport;
};
/* activate cport response has no payload */

struct gb_i2s_mgmt_deactivate_cport_request {
	__le16	cport;
};
/* deactivate cport response has no payload */

struct gb_i2s_mgmt_report_event_request {
	__u8	event;
};
/* report event response has no payload */

#define GB_I2S_DATA_TYPE_PROTOCOL_VERSION		0x01
#define GB_I2S_DATA_TYPE_SEND_DATA			0x02

struct gb_i2s_send_data_request {
	__le32	sample_number;
	__le32	size;
	__u8	data[0];
};
/* send data has no response at all */

#endif /* __GB_I2S_H__ */
