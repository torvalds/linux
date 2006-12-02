/* Linux driver for devices based on the DiBcom DiB0700 USB bridge
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 *  Copyright (C) 2005-6 DiBcom, SA
 */
#include "dib0700.h"

#include "dib3000mc.h"
#include "dib7000m.h"
#include "mt2060.h"

static int force_lna_activation;
module_param(force_lna_activation, int, 0644);
MODULE_PARM_DESC(force_lna_activation, "force the activation of Low-Noise-Amplifyer(s) (LNA), "
		"if applicable for the device (default: 0=automatic/off).");

/* Hauppauge Nova-T 500
 *  has a LNA on GPIO0 which is enabled by setting 1 */
static struct mt2060_config bristol_mt2060_config[2] = {
	{
		.i2c_address = 0x60,
		.clock_out   = 3,
	}, {
		.i2c_address = 0x61,
	}
};

static struct dibx000_agc_config bristol_dib3000p_mt2060_agc_config = {
	.band_caps = BAND_VHF | BAND_UHF,
	.setup     = (1 << 8) | (5 << 5) | (0 << 4) | (0 << 3) | (0 << 2) | (2 << 0),

	.agc1_max = 42598,
	.agc1_min = 17694,
	.agc2_max = 45875,
	.agc2_min = 0,

	.agc1_pt1 = 0,
	.agc1_pt2 = 59,

	.agc1_slope1 = 0,
	.agc1_slope2 = 69,

	.agc2_pt1 = 0,
	.agc2_pt2 = 59,

	.agc2_slope1 = 111,
	.agc2_slope2 = 28,
};

static struct dib3000mc_config bristol_dib3000mc_config[2] = {
	{	.agc          = &bristol_dib3000p_mt2060_agc_config,
		.max_time     = 0x196,
		.ln_adc_level = 0x1cc7,
		.output_mpeg2_in_188_bytes = 1,
	},
	{	.agc          = &bristol_dib3000p_mt2060_agc_config,
		.max_time     = 0x196,
		.ln_adc_level = 0x1cc7,
		.output_mpeg2_in_188_bytes = 1,
	}
};

static int bristol_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dib0700_state *st = adap->dev->priv;
	if (adap->id == 0) {
		dib0700_set_gpio(adap->dev, GPIO6,  GPIO_OUT, 0); msleep(10);
		dib0700_set_gpio(adap->dev, GPIO6,  GPIO_OUT, 1); msleep(10);
		dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0); msleep(10);
		dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1); msleep(10);

		if (force_lna_activation)
			dib0700_set_gpio(adap->dev, GPIO0, GPIO_OUT, 1);
		else
			dib0700_set_gpio(adap->dev, GPIO0, GPIO_OUT, 0);

		if (dib3000mc_i2c_enumeration(&adap->dev->i2c_adap, 2, DEFAULT_DIB3000P_I2C_ADDRESS, bristol_dib3000mc_config) != 0) {
			dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 0); msleep(10);
			return -ENODEV;
		}
	}
	st->mt2060_if1[adap->id] = 1220;
	return (adap->fe = dvb_attach(dib3000mc_attach, &adap->dev->i2c_adap,
		(10 + adap->id) << 1, &bristol_dib3000mc_config[adap->id])) == NULL ? -ENODEV : 0;
}

static int bristol_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dib0700_state *st = adap->dev->priv;
	struct i2c_adapter *tun_i2c = dib3000mc_get_tuner_i2c_master(adap->fe, 1);
	return dvb_attach(mt2060_attach,adap->fe, tun_i2c, &bristol_mt2060_config[adap->id],
		st->mt2060_if1[adap->id]) == NULL ? -ENODEV : 0;
}

/* STK7700P: Hauppauge Nova-T Stick, AVerMedia Volar */
/*
static struct mt2060_config stk7000p_mt2060_config = {
	0x60
};
*/

static int stk7700p_frontend_attach(struct dvb_usb_adapter *adap)
{
	/* unless there is no real power management in DVB - we leave the device on GPIO6 */
	dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 0); msleep(10);
	dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 1); msleep(10);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1); msleep(10);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0); msleep(10);

//	adap->fe = dib7000m_attach(&adap->dev->i2c_adap, &stk7700p_dib7000m_config, 18);
	return 0;
}

static int stk7700p_tuner_attach(struct dvb_usb_adapter *adap)
{
//	tun_i2c = dib7000m_get_tuner_i2c_master(adap->fe, 1);
//	return mt2060_attach(adap->fe, tun_i2c, &stk3000p_mt2060_config, if1);
	return 0;
}

struct usb_device_id dib0700_usb_id_table[] = {
		{ USB_DEVICE(USB_VID_DIBCOM,    USB_PID_DIBCOM_STK7700P) },
		{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_NOVA_T_500) },
		{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_NOVA_T_500_2) },
		{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_NOVA_T_STICK) },
		{ USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_VOLAR) },
		{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, dib0700_usb_id_table);

#define DIB0700_DEFAULT_DEVICE_PROPERTIES \
	.caps              = DVB_USB_IS_AN_I2C_ADAPTER, \
	.usb_ctrl          = DEVICE_SPECIFIC, \
	.firmware          = "dvb-usb-dib0700-01.fw", \
	.download_firmware = dib0700_download_firmware, \
	.no_reconnect      = 1, \
	.size_of_priv      = sizeof(struct dib0700_state), \
	.i2c_algo          = &dib0700_i2c_algo, \
	.identify_state    = dib0700_identify_state

#define DIB0700_DEFAULT_STREAMING_CONFIG(ep) \
	.streaming_ctrl   = dib0700_streaming_ctrl, \
	.stream = { \
		.type = USB_BULK, \
		.count = 4, \
		.endpoint = ep, \
		.u = { \
			.bulk = { \
				.buffersize = 39480, \
			} \
		} \
	}

struct dvb_usb_device_properties dib0700_devices[] = {
	{
		DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 1,
		.adapter = {
			{
				.frontend_attach  = stk7700p_frontend_attach,
				.tuner_attach     = stk7700p_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),
			},
		},

		.num_device_descs = 3,
		.devices = {
			{   "DiBcom STK7700P reference design",
				{ &dib0700_usb_id_table[0], NULL },
				{ NULL },
			},
			{   "Hauppauge Nova-T Stick",
				{ &dib0700_usb_id_table[3], NULL },
				{ NULL },
			},
			{   "AVerMedia AVerTV DVB-T Volar",
				{ &dib0700_usb_id_table[4], NULL },
				{ NULL },
			},
		}
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 2,
		.adapter = {
			{
				.frontend_attach  = bristol_frontend_attach,
				.tuner_attach     = bristol_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),
			}, {
				.frontend_attach  = bristol_frontend_attach,
				.tuner_attach     = bristol_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x03),
			}
		},

		.num_device_descs = 1,
		.devices = {
			{   "Hauppauge Nova-T 500 Dual DVB-T",
				{ &dib0700_usb_id_table[1], &dib0700_usb_id_table[2], NULL },
				{ NULL },
			},
		}
	}
};

int dib0700_device_count = ARRAY_SIZE(dib0700_devices);
