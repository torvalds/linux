/*
 *
 *
 *  Copyright (C) 2007 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*

This source file should encompass ALL per-device type information for the
driver.  To define a new device, add elements to the pvr2_device_table and
pvr2_device_desc structures.

*/

#include "pvrusb2-devattr.h"
#include <linux/usb.h>
#include <linux/module.h>
/* This is needed in order to pull in tuner type ids... */
#include <linux/i2c.h>
#include <media/tuner.h>
#ifdef CONFIG_VIDEO_PVRUSB2_DVB
#include "pvrusb2-hdw-internal.h"
#include "lgdt330x.h"
#include "s5h1409.h"
#include "s5h1411.h"
#include "tda10048.h"
#include "tda18271.h"
#include "tda8290.h"
#include "tuner-simple.h"
#endif


/*------------------------------------------------------------------------*/
/* Hauppauge PVR-USB2 Model 29xxx */

static const struct pvr2_device_client_desc pvr2_cli_29xxx[] = {
	{ .module_id = PVR2_CLIENT_ID_SAA7115 },
	{ .module_id = PVR2_CLIENT_ID_MSP3400 },
	{ .module_id = PVR2_CLIENT_ID_TUNER },
	{ .module_id = PVR2_CLIENT_ID_DEMOD },
};

#define PVR2_FIRMWARE_29xxx "v4l-pvrusb2-29xxx-01.fw"
static const char *pvr2_fw1_names_29xxx[] = {
		PVR2_FIRMWARE_29xxx,
};

static const struct pvr2_device_desc pvr2_device_29xxx = {
		.description = "WinTV PVR USB2 Model 29xxx",
		.shortname = "29xxx",
		.client_table.lst = pvr2_cli_29xxx,
		.client_table.cnt = ARRAY_SIZE(pvr2_cli_29xxx),
		.fx2_firmware.lst = pvr2_fw1_names_29xxx,
		.fx2_firmware.cnt = ARRAY_SIZE(pvr2_fw1_names_29xxx),
		.flag_has_hauppauge_rom = !0,
		.flag_has_analogtuner = !0,
		.flag_has_fmradio = !0,
		.flag_has_composite = !0,
		.flag_has_svideo = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_HAUPPAUGE,
		.led_scheme = PVR2_LED_SCHEME_HAUPPAUGE,
		.ir_scheme = PVR2_IR_SCHEME_29XXX,
};



/*------------------------------------------------------------------------*/
/* Hauppauge PVR-USB2 Model 24xxx */

static const struct pvr2_device_client_desc pvr2_cli_24xxx[] = {
	{ .module_id = PVR2_CLIENT_ID_CX25840 },
	{ .module_id = PVR2_CLIENT_ID_TUNER },
	{ .module_id = PVR2_CLIENT_ID_WM8775 },
	{ .module_id = PVR2_CLIENT_ID_DEMOD },
};

#define PVR2_FIRMWARE_24xxx "v4l-pvrusb2-24xxx-01.fw"
static const char *pvr2_fw1_names_24xxx[] = {
		PVR2_FIRMWARE_24xxx,
};

static const struct pvr2_device_desc pvr2_device_24xxx = {
		.description = "WinTV PVR USB2 Model 24xxx",
		.shortname = "24xxx",
		.client_table.lst = pvr2_cli_24xxx,
		.client_table.cnt = ARRAY_SIZE(pvr2_cli_24xxx),
		.fx2_firmware.lst = pvr2_fw1_names_24xxx,
		.fx2_firmware.cnt = ARRAY_SIZE(pvr2_fw1_names_24xxx),
		.flag_has_cx25840 = !0,
		.flag_has_wm8775 = !0,
		.flag_has_hauppauge_rom = !0,
		.flag_has_analogtuner = !0,
		.flag_has_fmradio = !0,
		.flag_has_composite = !0,
		.flag_has_svideo = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_HAUPPAUGE,
		.led_scheme = PVR2_LED_SCHEME_HAUPPAUGE,
		.ir_scheme = PVR2_IR_SCHEME_24XXX,
};



/*------------------------------------------------------------------------*/
/* GOTVIEW USB2.0 DVD2 */

static const struct pvr2_device_client_desc pvr2_cli_gotview_2[] = {
	{ .module_id = PVR2_CLIENT_ID_CX25840 },
	{ .module_id = PVR2_CLIENT_ID_TUNER },
	{ .module_id = PVR2_CLIENT_ID_DEMOD },
};

static const struct pvr2_device_desc pvr2_device_gotview_2 = {
		.description = "Gotview USB 2.0 DVD 2",
		.shortname = "gv2",
		.client_table.lst = pvr2_cli_gotview_2,
		.client_table.cnt = ARRAY_SIZE(pvr2_cli_gotview_2),
		.flag_has_cx25840 = !0,
		.default_tuner_type = TUNER_PHILIPS_FM1216ME_MK3,
		.flag_has_analogtuner = !0,
		.flag_has_fmradio = !0,
		.flag_has_composite = !0,
		.flag_has_svideo = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_GOTVIEW,
};



/*------------------------------------------------------------------------*/
/* GOTVIEW USB2.0 DVD Deluxe */

/* (same module list as gotview_2) */

static const struct pvr2_device_desc pvr2_device_gotview_2d = {
		.description = "Gotview USB 2.0 DVD Deluxe",
		.shortname = "gv2d",
		.client_table.lst = pvr2_cli_gotview_2,
		.client_table.cnt = ARRAY_SIZE(pvr2_cli_gotview_2),
		.flag_has_cx25840 = !0,
		.default_tuner_type = TUNER_PHILIPS_FM1216ME_MK3,
		.flag_has_analogtuner = !0,
		.flag_has_composite = !0,
		.flag_has_svideo = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_GOTVIEW,
};



/*------------------------------------------------------------------------*/
/* Terratec Grabster AV400 */

static const struct pvr2_device_client_desc pvr2_cli_av400[] = {
	{ .module_id = PVR2_CLIENT_ID_CX25840 },
};

static const struct pvr2_device_desc pvr2_device_av400 = {
		.description = "Terratec Grabster AV400",
		.shortname = "av400",
		.flag_is_experimental = 1,
		.client_table.lst = pvr2_cli_av400,
		.client_table.cnt = ARRAY_SIZE(pvr2_cli_av400),
		.flag_has_cx25840 = !0,
		.flag_has_analogtuner = 0,
		.flag_has_composite = !0,
		.flag_has_svideo = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_AV400,
};



/*------------------------------------------------------------------------*/
/* OnAir Creator */

#ifdef CONFIG_VIDEO_PVRUSB2_DVB
static struct lgdt330x_config pvr2_lgdt3303_config = {
	.demod_address       = 0x0e,
	.demod_chip          = LGDT3303,
	.clock_polarity_flip = 1,
};

static int pvr2_lgdt3303_attach(struct pvr2_dvb_adapter *adap)
{
	adap->fe = dvb_attach(lgdt330x_attach, &pvr2_lgdt3303_config,
			      &adap->channel.hdw->i2c_adap);
	if (adap->fe)
		return 0;

	return -EIO;
}

static int pvr2_lgh06xf_attach(struct pvr2_dvb_adapter *adap)
{
	dvb_attach(simple_tuner_attach, adap->fe,
		   &adap->channel.hdw->i2c_adap, 0x61,
		   TUNER_LG_TDVS_H06XF);

	return 0;
}

static const struct pvr2_dvb_props pvr2_onair_creator_fe_props = {
	.frontend_attach = pvr2_lgdt3303_attach,
	.tuner_attach    = pvr2_lgh06xf_attach,
};
#endif

static const struct pvr2_device_client_desc pvr2_cli_onair_creator[] = {
	{ .module_id = PVR2_CLIENT_ID_SAA7115 },
	{ .module_id = PVR2_CLIENT_ID_CS53L32A },
	{ .module_id = PVR2_CLIENT_ID_TUNER },
};

static const struct pvr2_device_desc pvr2_device_onair_creator = {
		.description = "OnAir Creator Hybrid USB tuner",
		.shortname = "oac",
		.client_table.lst = pvr2_cli_onair_creator,
		.client_table.cnt = ARRAY_SIZE(pvr2_cli_onair_creator),
		.default_tuner_type = TUNER_LG_TDVS_H06XF,
		.flag_has_analogtuner = !0,
		.flag_has_composite = !0,
		.flag_has_svideo = !0,
		.flag_digital_requires_cx23416 = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_ONAIR,
		.digital_control_scheme = PVR2_DIGITAL_SCHEME_ONAIR,
		.default_std_mask = V4L2_STD_NTSC_M,
#ifdef CONFIG_VIDEO_PVRUSB2_DVB
		.dvb_props = &pvr2_onair_creator_fe_props,
#endif
};



/*------------------------------------------------------------------------*/
/* OnAir USB 2.0 */

#ifdef CONFIG_VIDEO_PVRUSB2_DVB
static struct lgdt330x_config pvr2_lgdt3302_config = {
	.demod_address       = 0x0e,
	.demod_chip          = LGDT3302,
};

static int pvr2_lgdt3302_attach(struct pvr2_dvb_adapter *adap)
{
	adap->fe = dvb_attach(lgdt330x_attach, &pvr2_lgdt3302_config,
			      &adap->channel.hdw->i2c_adap);
	if (adap->fe)
		return 0;

	return -EIO;
}

static int pvr2_fcv1236d_attach(struct pvr2_dvb_adapter *adap)
{
	dvb_attach(simple_tuner_attach, adap->fe,
		   &adap->channel.hdw->i2c_adap, 0x61,
		   TUNER_PHILIPS_FCV1236D);

	return 0;
}

static const struct pvr2_dvb_props pvr2_onair_usb2_fe_props = {
	.frontend_attach = pvr2_lgdt3302_attach,
	.tuner_attach    = pvr2_fcv1236d_attach,
};
#endif

static const struct pvr2_device_client_desc pvr2_cli_onair_usb2[] = {
	{ .module_id = PVR2_CLIENT_ID_SAA7115 },
	{ .module_id = PVR2_CLIENT_ID_CS53L32A },
	{ .module_id = PVR2_CLIENT_ID_TUNER },
};

static const struct pvr2_device_desc pvr2_device_onair_usb2 = {
		.description = "OnAir USB2 Hybrid USB tuner",
		.shortname = "oa2",
		.client_table.lst = pvr2_cli_onair_usb2,
		.client_table.cnt = ARRAY_SIZE(pvr2_cli_onair_usb2),
		.default_tuner_type = TUNER_PHILIPS_FCV1236D,
		.flag_has_analogtuner = !0,
		.flag_has_composite = !0,
		.flag_has_svideo = !0,
		.flag_digital_requires_cx23416 = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_ONAIR,
		.digital_control_scheme = PVR2_DIGITAL_SCHEME_ONAIR,
		.default_std_mask = V4L2_STD_NTSC_M,
#ifdef CONFIG_VIDEO_PVRUSB2_DVB
		.dvb_props = &pvr2_onair_usb2_fe_props,
#endif
};



/*------------------------------------------------------------------------*/
/* Hauppauge PVR-USB2 Model 73xxx */

#ifdef CONFIG_VIDEO_PVRUSB2_DVB
static struct tda10048_config hauppauge_tda10048_config = {
	.demod_address  = 0x10 >> 1,
	.output_mode    = TDA10048_PARALLEL_OUTPUT,
	.fwbulkwritelen = TDA10048_BULKWRITE_50,
	.inversion      = TDA10048_INVERSION_ON,
	.dtv6_if_freq_khz = TDA10048_IF_3300,
	.dtv7_if_freq_khz = TDA10048_IF_3800,
	.dtv8_if_freq_khz = TDA10048_IF_4300,
	.clk_freq_khz   = TDA10048_CLK_16000,
	.disable_gate_access = 1,
};

static struct tda829x_config tda829x_no_probe = {
	.probe_tuner = TDA829X_DONT_PROBE,
};

static struct tda18271_std_map hauppauge_tda18271_dvbt_std_map = {
        .dvbt_6   = { .if_freq = 3300, .agc_mode = 3, .std = 4,
                      .if_lvl = 1, .rfagc_top = 0x37, },
        .dvbt_7   = { .if_freq = 3800, .agc_mode = 3, .std = 5,
                      .if_lvl = 1, .rfagc_top = 0x37, },
        .dvbt_8   = { .if_freq = 4300, .agc_mode = 3, .std = 6,
                      .if_lvl = 1, .rfagc_top = 0x37, },
};

static struct tda18271_config hauppauge_tda18271_dvb_config = {
	.std_map = &hauppauge_tda18271_dvbt_std_map,
	.gate    = TDA18271_GATE_ANALOG,
	.output_opt = TDA18271_OUTPUT_LT_OFF,
};

static int pvr2_tda10048_attach(struct pvr2_dvb_adapter *adap)
{
	adap->fe = dvb_attach(tda10048_attach, &hauppauge_tda10048_config,
			      &adap->channel.hdw->i2c_adap);
	if (adap->fe)
		return 0;

	return -EIO;
}

static int pvr2_73xxx_tda18271_8295_attach(struct pvr2_dvb_adapter *adap)
{
	dvb_attach(tda829x_attach, adap->fe,
		   &adap->channel.hdw->i2c_adap, 0x42,
		   &tda829x_no_probe);
	dvb_attach(tda18271_attach, adap->fe, 0x60,
		   &adap->channel.hdw->i2c_adap,
		   &hauppauge_tda18271_dvb_config);

	return 0;
}

static const struct pvr2_dvb_props pvr2_73xxx_dvb_props = {
	.frontend_attach = pvr2_tda10048_attach,
	.tuner_attach    = pvr2_73xxx_tda18271_8295_attach,
};
#endif

static const struct pvr2_device_client_desc pvr2_cli_73xxx[] = {
	{ .module_id = PVR2_CLIENT_ID_CX25840 },
	{ .module_id = PVR2_CLIENT_ID_TUNER,
	  .i2c_address_list = "\x42"},
};

#define PVR2_FIRMWARE_73xxx "v4l-pvrusb2-73xxx-01.fw"
static const char *pvr2_fw1_names_73xxx[] = {
		PVR2_FIRMWARE_73xxx,
};

static const struct pvr2_device_desc pvr2_device_73xxx = {
		.description = "WinTV HVR-1900 Model 73xxx",
		.shortname = "73xxx",
		.client_table.lst = pvr2_cli_73xxx,
		.client_table.cnt = ARRAY_SIZE(pvr2_cli_73xxx),
		.fx2_firmware.lst = pvr2_fw1_names_73xxx,
		.fx2_firmware.cnt = ARRAY_SIZE(pvr2_fw1_names_73xxx),
		.flag_has_cx25840 = !0,
		.flag_has_hauppauge_rom = !0,
		.flag_has_analogtuner = !0,
		.flag_has_composite = !0,
		.flag_has_svideo = !0,
		.flag_fx2_16kb = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_HAUPPAUGE,
		.digital_control_scheme = PVR2_DIGITAL_SCHEME_HAUPPAUGE,
		.led_scheme = PVR2_LED_SCHEME_HAUPPAUGE,
		.ir_scheme = PVR2_IR_SCHEME_ZILOG,
#ifdef CONFIG_VIDEO_PVRUSB2_DVB
		.dvb_props = &pvr2_73xxx_dvb_props,
#endif
};



/*------------------------------------------------------------------------*/
/* Hauppauge PVR-USB2 Model 75xxx */

#ifdef CONFIG_VIDEO_PVRUSB2_DVB
static struct s5h1409_config pvr2_s5h1409_config = {
	.demod_address = 0x32 >> 1,
	.output_mode   = S5H1409_PARALLEL_OUTPUT,
	.gpio          = S5H1409_GPIO_OFF,
	.qam_if        = 4000,
	.inversion     = S5H1409_INVERSION_ON,
	.status_mode   = S5H1409_DEMODLOCKING,
};

static struct s5h1411_config pvr2_s5h1411_config = {
	.output_mode   = S5H1411_PARALLEL_OUTPUT,
	.gpio          = S5H1411_GPIO_OFF,
	.vsb_if        = S5H1411_IF_44000,
	.qam_if        = S5H1411_IF_4000,
	.inversion     = S5H1411_INVERSION_ON,
	.status_mode   = S5H1411_DEMODLOCKING,
};

static struct tda18271_std_map hauppauge_tda18271_std_map = {
	.atsc_6   = { .if_freq = 5380, .agc_mode = 3, .std = 3,
		      .if_lvl = 6, .rfagc_top = 0x37, },
	.qam_6    = { .if_freq = 4000, .agc_mode = 3, .std = 0,
		      .if_lvl = 6, .rfagc_top = 0x37, },
};

static struct tda18271_config hauppauge_tda18271_config = {
	.std_map = &hauppauge_tda18271_std_map,
	.gate    = TDA18271_GATE_ANALOG,
	.output_opt = TDA18271_OUTPUT_LT_OFF,
};

static int pvr2_s5h1409_attach(struct pvr2_dvb_adapter *adap)
{
	adap->fe = dvb_attach(s5h1409_attach, &pvr2_s5h1409_config,
			      &adap->channel.hdw->i2c_adap);
	if (adap->fe)
		return 0;

	return -EIO;
}

static int pvr2_s5h1411_attach(struct pvr2_dvb_adapter *adap)
{
	adap->fe = dvb_attach(s5h1411_attach, &pvr2_s5h1411_config,
			      &adap->channel.hdw->i2c_adap);
	if (adap->fe)
		return 0;

	return -EIO;
}

static int pvr2_tda18271_8295_attach(struct pvr2_dvb_adapter *adap)
{
	dvb_attach(tda829x_attach, adap->fe,
		   &adap->channel.hdw->i2c_adap, 0x42,
		   &tda829x_no_probe);
	dvb_attach(tda18271_attach, adap->fe, 0x60,
		   &adap->channel.hdw->i2c_adap,
		   &hauppauge_tda18271_config);

	return 0;
}

static const struct pvr2_dvb_props pvr2_750xx_dvb_props = {
	.frontend_attach = pvr2_s5h1409_attach,
	.tuner_attach    = pvr2_tda18271_8295_attach,
};

static const struct pvr2_dvb_props pvr2_751xx_dvb_props = {
	.frontend_attach = pvr2_s5h1411_attach,
	.tuner_attach    = pvr2_tda18271_8295_attach,
};
#endif

#define PVR2_FIRMWARE_75xxx "v4l-pvrusb2-73xxx-01.fw"
static const char *pvr2_fw1_names_75xxx[] = {
		PVR2_FIRMWARE_75xxx,
};

static const struct pvr2_device_desc pvr2_device_750xx = {
		.description = "WinTV HVR-1950 Model 750xx",
		.shortname = "750xx",
		.client_table.lst = pvr2_cli_73xxx,
		.client_table.cnt = ARRAY_SIZE(pvr2_cli_73xxx),
		.fx2_firmware.lst = pvr2_fw1_names_75xxx,
		.fx2_firmware.cnt = ARRAY_SIZE(pvr2_fw1_names_75xxx),
		.flag_has_cx25840 = !0,
		.flag_has_hauppauge_rom = !0,
		.flag_has_analogtuner = !0,
		.flag_has_composite = !0,
		.flag_has_svideo = !0,
		.flag_fx2_16kb = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_HAUPPAUGE,
		.digital_control_scheme = PVR2_DIGITAL_SCHEME_HAUPPAUGE,
		.default_std_mask = V4L2_STD_NTSC_M,
		.led_scheme = PVR2_LED_SCHEME_HAUPPAUGE,
		.ir_scheme = PVR2_IR_SCHEME_ZILOG,
#ifdef CONFIG_VIDEO_PVRUSB2_DVB
		.dvb_props = &pvr2_750xx_dvb_props,
#endif
};

static const struct pvr2_device_desc pvr2_device_751xx = {
		.description = "WinTV HVR-1950 Model 751xx",
		.shortname = "751xx",
		.client_table.lst = pvr2_cli_73xxx,
		.client_table.cnt = ARRAY_SIZE(pvr2_cli_73xxx),
		.fx2_firmware.lst = pvr2_fw1_names_75xxx,
		.fx2_firmware.cnt = ARRAY_SIZE(pvr2_fw1_names_75xxx),
		.flag_has_cx25840 = !0,
		.flag_has_hauppauge_rom = !0,
		.flag_has_analogtuner = !0,
		.flag_has_composite = !0,
		.flag_has_svideo = !0,
		.flag_fx2_16kb = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_HAUPPAUGE,
		.digital_control_scheme = PVR2_DIGITAL_SCHEME_HAUPPAUGE,
		.default_std_mask = V4L2_STD_NTSC_M,
		.led_scheme = PVR2_LED_SCHEME_HAUPPAUGE,
		.ir_scheme = PVR2_IR_SCHEME_ZILOG,
#ifdef CONFIG_VIDEO_PVRUSB2_DVB
		.dvb_props = &pvr2_751xx_dvb_props,
#endif
};



/*------------------------------------------------------------------------*/

struct usb_device_id pvr2_device_table[] = {
	{ USB_DEVICE(0x2040, 0x2900),
	  .driver_info = (kernel_ulong_t)&pvr2_device_29xxx},
	{ USB_DEVICE(0x2040, 0x2950), /* Logically identical to 2900 */
	  .driver_info = (kernel_ulong_t)&pvr2_device_29xxx},
	{ USB_DEVICE(0x2040, 0x2400),
	  .driver_info = (kernel_ulong_t)&pvr2_device_24xxx},
	{ USB_DEVICE(0x1164, 0x0622),
	  .driver_info = (kernel_ulong_t)&pvr2_device_gotview_2},
	{ USB_DEVICE(0x1164, 0x0602),
	  .driver_info = (kernel_ulong_t)&pvr2_device_gotview_2d},
	{ USB_DEVICE(0x11ba, 0x1003),
	  .driver_info = (kernel_ulong_t)&pvr2_device_onair_creator},
	{ USB_DEVICE(0x11ba, 0x1001),
	  .driver_info = (kernel_ulong_t)&pvr2_device_onair_usb2},
	{ USB_DEVICE(0x2040, 0x7300),
	  .driver_info = (kernel_ulong_t)&pvr2_device_73xxx},
	{ USB_DEVICE(0x2040, 0x7500),
	  .driver_info = (kernel_ulong_t)&pvr2_device_750xx},
	{ USB_DEVICE(0x2040, 0x7501),
	  .driver_info = (kernel_ulong_t)&pvr2_device_751xx},
	{ USB_DEVICE(0x0ccd, 0x0039),
	  .driver_info = (kernel_ulong_t)&pvr2_device_av400},
	{ }
};

MODULE_DEVICE_TABLE(usb, pvr2_device_table);
MODULE_FIRMWARE(PVR2_FIRMWARE_29xxx);
MODULE_FIRMWARE(PVR2_FIRMWARE_24xxx);
MODULE_FIRMWARE(PVR2_FIRMWARE_73xxx);
MODULE_FIRMWARE(PVR2_FIRMWARE_75xxx);
