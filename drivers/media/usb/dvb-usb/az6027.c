/* DVB USB compliant Linux driver for the AZUREWAVE DVB-S/S2 USB2.0 (AZ6027)
 * receiver.
 *
 * Copyright (C) 2009 Adams.Xu <adams.xu@azwave.com.cn>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "az6027.h"

#include "stb0899_drv.h"
#include "stb0899_reg.h"
#include "stb0899_cfg.h"

#include "stb6100.h"
#include "stb6100_cfg.h"
#include "dvb_ca_en50221.h"

int dvb_usb_az6027_debug;
module_param_named(debug, dvb_usb_az6027_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2,rc=4 (or-able))." DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct az6027_device_state {
	struct dvb_ca_en50221 ca;
	struct mutex ca_mutex;
	u8 power_state;
};

static const struct stb0899_s1_reg az6027_stb0899_s1_init_1[] = {

	/* 0x0000000b, SYSREG */
	{ STB0899_DEV_ID		, 0x30 },
	{ STB0899_DISCNTRL1		, 0x32 },
	{ STB0899_DISCNTRL2     	, 0x80 },
	{ STB0899_DISRX_ST0     	, 0x04 },
	{ STB0899_DISRX_ST1     	, 0x00 },
	{ STB0899_DISPARITY     	, 0x00 },
	{ STB0899_DISSTATUS		, 0x20 },
	{ STB0899_DISF22        	, 0x99 },
	{ STB0899_DISF22RX      	, 0xa8 },
	/* SYSREG ? */
	{ STB0899_ACRPRESC      	, 0x11 },
	{ STB0899_ACRDIV1       	, 0x0a },
	{ STB0899_ACRDIV2       	, 0x05 },
	{ STB0899_DACR1         	, 0x00 },
	{ STB0899_DACR2         	, 0x00 },
	{ STB0899_OUTCFG        	, 0x00 },
	{ STB0899_MODECFG       	, 0x00 },
	{ STB0899_IRQSTATUS_3		, 0xfe },
	{ STB0899_IRQSTATUS_2		, 0x03 },
	{ STB0899_IRQSTATUS_1		, 0x7c },
	{ STB0899_IRQSTATUS_0		, 0xf4 },
	{ STB0899_IRQMSK_3      	, 0xf3 },
	{ STB0899_IRQMSK_2      	, 0xfc },
	{ STB0899_IRQMSK_1      	, 0xff },
	{ STB0899_IRQMSK_0		, 0xff },
	{ STB0899_IRQCFG		, 0x00 },
	{ STB0899_I2CCFG        	, 0x88 },
	{ STB0899_I2CRPT        	, 0x58 },
	{ STB0899_IOPVALUE5		, 0x00 },
	{ STB0899_IOPVALUE4		, 0x33 },
	{ STB0899_IOPVALUE3		, 0x6d },
	{ STB0899_IOPVALUE2		, 0x90 },
	{ STB0899_IOPVALUE1		, 0x60 },
	{ STB0899_IOPVALUE0		, 0x00 },
	{ STB0899_GPIO00CFG     	, 0x82 },
	{ STB0899_GPIO01CFG     	, 0x82 },
	{ STB0899_GPIO02CFG     	, 0x82 },
	{ STB0899_GPIO03CFG     	, 0x82 },
	{ STB0899_GPIO04CFG     	, 0x82 },
	{ STB0899_GPIO05CFG     	, 0x82 },
	{ STB0899_GPIO06CFG     	, 0x82 },
	{ STB0899_GPIO07CFG     	, 0x82 },
	{ STB0899_GPIO08CFG     	, 0x82 },
	{ STB0899_GPIO09CFG     	, 0x82 },
	{ STB0899_GPIO10CFG     	, 0x82 },
	{ STB0899_GPIO11CFG     	, 0x82 },
	{ STB0899_GPIO12CFG     	, 0x82 },
	{ STB0899_GPIO13CFG     	, 0x82 },
	{ STB0899_GPIO14CFG     	, 0x82 },
	{ STB0899_GPIO15CFG     	, 0x82 },
	{ STB0899_GPIO16CFG     	, 0x82 },
	{ STB0899_GPIO17CFG     	, 0x82 },
	{ STB0899_GPIO18CFG     	, 0x82 },
	{ STB0899_GPIO19CFG     	, 0x82 },
	{ STB0899_GPIO20CFG     	, 0x82 },
	{ STB0899_SDATCFG       	, 0xb8 },
	{ STB0899_SCLTCFG       	, 0xba },
	{ STB0899_AGCRFCFG      	, 0x1c }, /* 0x11 */
	{ STB0899_GPIO22        	, 0x82 }, /* AGCBB2CFG */
	{ STB0899_GPIO21        	, 0x91 }, /* AGCBB1CFG */
	{ STB0899_DIRCLKCFG     	, 0x82 },
	{ STB0899_CLKOUT27CFG   	, 0x7e },
	{ STB0899_STDBYCFG      	, 0x82 },
	{ STB0899_CS0CFG        	, 0x82 },
	{ STB0899_CS1CFG        	, 0x82 },
	{ STB0899_DISEQCOCFG    	, 0x20 },
	{ STB0899_GPIO32CFG		, 0x82 },
	{ STB0899_GPIO33CFG		, 0x82 },
	{ STB0899_GPIO34CFG		, 0x82 },
	{ STB0899_GPIO35CFG		, 0x82 },
	{ STB0899_GPIO36CFG		, 0x82 },
	{ STB0899_GPIO37CFG		, 0x82 },
	{ STB0899_GPIO38CFG		, 0x82 },
	{ STB0899_GPIO39CFG		, 0x82 },
	{ STB0899_NCOARSE       	, 0x17 }, /* 0x15 = 27 Mhz Clock, F/3 = 198MHz, F/6 = 99MHz */
	{ STB0899_SYNTCTRL      	, 0x02 }, /* 0x00 = CLK from CLKI, 0x02 = CLK from XTALI */
	{ STB0899_FILTCTRL      	, 0x00 },
	{ STB0899_SYSCTRL       	, 0x01 },
	{ STB0899_STOPCLK1      	, 0x20 },
	{ STB0899_STOPCLK2      	, 0x00 },
	{ STB0899_INTBUFSTATUS		, 0x00 },
	{ STB0899_INTBUFCTRL    	, 0x0a },
	{ 0xffff			, 0xff },
};

static const struct stb0899_s1_reg az6027_stb0899_s1_init_3[] = {
	{ STB0899_DEMOD         	, 0x00 },
	{ STB0899_RCOMPC        	, 0xc9 },
	{ STB0899_AGC1CN        	, 0x01 },
	{ STB0899_AGC1REF       	, 0x10 },
	{ STB0899_RTC			, 0x23 },
	{ STB0899_TMGCFG        	, 0x4e },
	{ STB0899_AGC2REF       	, 0x34 },
	{ STB0899_TLSR          	, 0x84 },
	{ STB0899_CFD           	, 0xf7 },
	{ STB0899_ACLC			, 0x87 },
	{ STB0899_BCLC          	, 0x94 },
	{ STB0899_EQON          	, 0x41 },
	{ STB0899_LDT           	, 0xf1 },
	{ STB0899_LDT2          	, 0xe3 },
	{ STB0899_EQUALREF      	, 0xb4 },
	{ STB0899_TMGRAMP       	, 0x10 },
	{ STB0899_TMGTHD        	, 0x30 },
	{ STB0899_IDCCOMP		, 0xfd },
	{ STB0899_QDCCOMP		, 0xff },
	{ STB0899_POWERI		, 0x0c },
	{ STB0899_POWERQ		, 0x0f },
	{ STB0899_RCOMP			, 0x6c },
	{ STB0899_AGCIQIN		, 0x80 },
	{ STB0899_AGC2I1		, 0x06 },
	{ STB0899_AGC2I2		, 0x00 },
	{ STB0899_TLIR			, 0x30 },
	{ STB0899_RTF			, 0x7f },
	{ STB0899_DSTATUS		, 0x00 },
	{ STB0899_LDI			, 0xbc },
	{ STB0899_CFRM			, 0xea },
	{ STB0899_CFRL			, 0x31 },
	{ STB0899_NIRM			, 0x2b },
	{ STB0899_NIRL			, 0x80 },
	{ STB0899_ISYMB			, 0x1d },
	{ STB0899_QSYMB			, 0xa6 },
	{ STB0899_SFRH          	, 0x2f },
	{ STB0899_SFRM          	, 0x68 },
	{ STB0899_SFRL          	, 0x40 },
	{ STB0899_SFRUPH        	, 0x2f },
	{ STB0899_SFRUPM        	, 0x68 },
	{ STB0899_SFRUPL        	, 0x40 },
	{ STB0899_EQUAI1		, 0x02 },
	{ STB0899_EQUAQ1		, 0xff },
	{ STB0899_EQUAI2		, 0x04 },
	{ STB0899_EQUAQ2		, 0x05 },
	{ STB0899_EQUAI3		, 0x02 },
	{ STB0899_EQUAQ3		, 0xfd },
	{ STB0899_EQUAI4		, 0x03 },
	{ STB0899_EQUAQ4		, 0x07 },
	{ STB0899_EQUAI5		, 0x08 },
	{ STB0899_EQUAQ5		, 0xf5 },
	{ STB0899_DSTATUS2		, 0x00 },
	{ STB0899_VSTATUS       	, 0x00 },
	{ STB0899_VERROR		, 0x86 },
	{ STB0899_IQSWAP		, 0x2a },
	{ STB0899_ECNT1M		, 0x00 },
	{ STB0899_ECNT1L		, 0x00 },
	{ STB0899_ECNT2M		, 0x00 },
	{ STB0899_ECNT2L		, 0x00 },
	{ STB0899_ECNT3M		, 0x0a },
	{ STB0899_ECNT3L		, 0xad },
	{ STB0899_FECAUTO1      	, 0x06 },
	{ STB0899_FECM			, 0x01 },
	{ STB0899_VTH12         	, 0xb0 },
	{ STB0899_VTH23         	, 0x7a },
	{ STB0899_VTH34			, 0x58 },
	{ STB0899_VTH56         	, 0x38 },
	{ STB0899_VTH67         	, 0x34 },
	{ STB0899_VTH78         	, 0x24 },
	{ STB0899_PRVIT         	, 0xff },
	{ STB0899_VITSYNC       	, 0x19 },
	{ STB0899_RSULC         	, 0xb1 }, /* DVB = 0xb1, DSS = 0xa1 */
	{ STB0899_TSULC         	, 0x42 },
	{ STB0899_RSLLC         	, 0x41 },
	{ STB0899_TSLPL			, 0x12 },
	{ STB0899_TSCFGH        	, 0x0c },
	{ STB0899_TSCFGM        	, 0x00 },
	{ STB0899_TSCFGL        	, 0x00 },
	{ STB0899_TSOUT			, 0x69 }, /* 0x0d for CAM */
	{ STB0899_RSSYNCDEL     	, 0x00 },
	{ STB0899_TSINHDELH     	, 0x02 },
	{ STB0899_TSINHDELM		, 0x00 },
	{ STB0899_TSINHDELL		, 0x00 },
	{ STB0899_TSLLSTKM		, 0x1b },
	{ STB0899_TSLLSTKL		, 0xb3 },
	{ STB0899_TSULSTKM		, 0x00 },
	{ STB0899_TSULSTKL		, 0x00 },
	{ STB0899_PCKLENUL		, 0xbc },
	{ STB0899_PCKLENLL		, 0xcc },
	{ STB0899_RSPCKLEN		, 0xbd },
	{ STB0899_TSSTATUS		, 0x90 },
	{ STB0899_ERRCTRL1      	, 0xb6 },
	{ STB0899_ERRCTRL2      	, 0x95 },
	{ STB0899_ERRCTRL3      	, 0x8d },
	{ STB0899_DMONMSK1		, 0x27 },
	{ STB0899_DMONMSK0		, 0x03 },
	{ STB0899_DEMAPVIT      	, 0x5c },
	{ STB0899_PLPARM		, 0x19 },
	{ STB0899_PDELCTRL      	, 0x48 },
	{ STB0899_PDELCTRL2     	, 0x00 },
	{ STB0899_BBHCTRL1      	, 0x00 },
	{ STB0899_BBHCTRL2      	, 0x00 },
	{ STB0899_HYSTTHRESH    	, 0x77 },
	{ STB0899_MATCSTM		, 0x00 },
	{ STB0899_MATCSTL		, 0x00 },
	{ STB0899_UPLCSTM		, 0x00 },
	{ STB0899_UPLCSTL		, 0x00 },
	{ STB0899_DFLCSTM		, 0x00 },
	{ STB0899_DFLCSTL		, 0x00 },
	{ STB0899_SYNCCST		, 0x00 },
	{ STB0899_SYNCDCSTM		, 0x00 },
	{ STB0899_SYNCDCSTL		, 0x00 },
	{ STB0899_ISI_ENTRY		, 0x00 },
	{ STB0899_ISI_BIT_EN		, 0x00 },
	{ STB0899_MATSTRM		, 0xf0 },
	{ STB0899_MATSTRL		, 0x02 },
	{ STB0899_UPLSTRM		, 0x45 },
	{ STB0899_UPLSTRL		, 0x60 },
	{ STB0899_DFLSTRM		, 0xe3 },
	{ STB0899_DFLSTRL		, 0x00 },
	{ STB0899_SYNCSTR		, 0x47 },
	{ STB0899_SYNCDSTRM		, 0x05 },
	{ STB0899_SYNCDSTRL		, 0x18 },
	{ STB0899_CFGPDELSTATUS1	, 0x19 },
	{ STB0899_CFGPDELSTATUS2	, 0x2b },
	{ STB0899_BBFERRORM		, 0x00 },
	{ STB0899_BBFERRORL		, 0x01 },
	{ STB0899_UPKTERRORM		, 0x00 },
	{ STB0899_UPKTERRORL		, 0x00 },
	{ 0xffff			, 0xff },
};



struct stb0899_config az6027_stb0899_config = {
	.init_dev		= az6027_stb0899_s1_init_1,
	.init_s2_demod		= stb0899_s2_init_2,
	.init_s1_demod		= az6027_stb0899_s1_init_3,
	.init_s2_fec		= stb0899_s2_init_4,
	.init_tst		= stb0899_s1_init_5,

	.demod_address 		= 0xd0, /* 0x68, 0xd0 >> 1 */

	.xtal_freq		= 27000000,
	.inversion		= IQ_SWAP_ON, /* 1 */

	.lo_clk			= 76500000,
	.hi_clk			= 99000000,

	.esno_ave		= STB0899_DVBS2_ESNO_AVE,
	.esno_quant		= STB0899_DVBS2_ESNO_QUANT,
	.avframes_coarse	= STB0899_DVBS2_AVFRAMES_COARSE,
	.avframes_fine		= STB0899_DVBS2_AVFRAMES_FINE,
	.miss_threshold		= STB0899_DVBS2_MISS_THRESHOLD,
	.uwp_threshold_acq	= STB0899_DVBS2_UWP_THRESHOLD_ACQ,
	.uwp_threshold_track	= STB0899_DVBS2_UWP_THRESHOLD_TRACK,
	.uwp_threshold_sof	= STB0899_DVBS2_UWP_THRESHOLD_SOF,
	.sof_search_timeout	= STB0899_DVBS2_SOF_SEARCH_TIMEOUT,

	.btr_nco_bits		= STB0899_DVBS2_BTR_NCO_BITS,
	.btr_gain_shift_offset	= STB0899_DVBS2_BTR_GAIN_SHIFT_OFFSET,
	.crl_nco_bits		= STB0899_DVBS2_CRL_NCO_BITS,
	.ldpc_max_iter		= STB0899_DVBS2_LDPC_MAX_ITER,

	.tuner_get_frequency	= stb6100_get_frequency,
	.tuner_set_frequency	= stb6100_set_frequency,
	.tuner_set_bandwidth	= stb6100_set_bandwidth,
	.tuner_get_bandwidth	= stb6100_get_bandwidth,
	.tuner_set_rfsiggain	= NULL,
};

struct stb6100_config az6027_stb6100_config = {
	.tuner_address	= 0xc0,
	.refclock	= 27000000,
};


/* check for mutex FIXME */
static int az6027_usb_in_op(struct dvb_usb_device *d, u8 req,
			    u16 value, u16 index, u8 *b, int blen)
{
	int ret = -1;
	if (mutex_lock_interruptible(&d->usb_mutex))
		return -EAGAIN;

	ret = usb_control_msg(d->udev,
			      usb_rcvctrlpipe(d->udev, 0),
			      req,
			      USB_TYPE_VENDOR | USB_DIR_IN,
			      value,
			      index,
			      b,
			      blen,
			      2000);

	if (ret < 0) {
		warn("usb in operation failed. (%d)", ret);
		ret = -EIO;
	} else
		ret = 0;

	deb_xfer("in: req. %02x, val: %04x, ind: %04x, buffer: ", req, value, index);
	debug_dump(b, blen, deb_xfer);

	mutex_unlock(&d->usb_mutex);
	return ret;
}

static int az6027_usb_out_op(struct dvb_usb_device *d,
			     u8 req,
			     u16 value,
			     u16 index,
			     u8 *b,
			     int blen)
{
	int ret;

	deb_xfer("out: req. %02x, val: %04x, ind: %04x, buffer: ", req, value, index);
	debug_dump(b, blen, deb_xfer);

	if (mutex_lock_interruptible(&d->usb_mutex))
		return -EAGAIN;

	ret = usb_control_msg(d->udev,
			      usb_sndctrlpipe(d->udev, 0),
			      req,
			      USB_TYPE_VENDOR | USB_DIR_OUT,
			      value,
			      index,
			      b,
			      blen,
			      2000);

	if (ret != blen) {
		warn("usb out operation failed. (%d)", ret);
		mutex_unlock(&d->usb_mutex);
		return -EIO;
	} else{
		mutex_unlock(&d->usb_mutex);
		return 0;
	}
}

static int az6027_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;

	deb_info("%s %d", __func__, onoff);

	req = 0xBC;
	value = onoff;
	index = 0;
	blen = 0;

	ret = az6027_usb_out_op(adap->dev, req, value, index, NULL, blen);
	if (ret != 0)
		warn("usb out operation failed. (%d)", ret);

	return ret;
}

/* keys for the enclosed remote control */
static struct rc_map_table rc_map_az6027_table[] = {
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
};

/* remote control stuff (does not work with my box) */
static int az6027_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	return 0;
}

/*
int az6027_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 v = onoff;
	return az6027_usb_out_op(d,0xBC,v,3,NULL,1);
}
*/

static int az6027_ci_read_attribute_mem(struct dvb_ca_en50221 *ca,
					int slot,
					int address)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6027_device_state *state = (struct az6027_device_state *)d->priv;

	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;
	u8 *b;

	if (slot != 0)
		return -EINVAL;

	b = kmalloc(12, GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	mutex_lock(&state->ca_mutex);

	req = 0xC1;
	value = address;
	index = 0;
	blen = 1;

	ret = az6027_usb_in_op(d, req, value, index, b, blen);
	if (ret < 0) {
		warn("usb in operation failed. (%d)", ret);
		ret = -EINVAL;
	} else {
		ret = b[0];
	}

	mutex_unlock(&state->ca_mutex);
	kfree(b);
	return ret;
}

static int az6027_ci_write_attribute_mem(struct dvb_ca_en50221 *ca,
					 int slot,
					 int address,
					 u8 value)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6027_device_state *state = (struct az6027_device_state *)d->priv;

	int ret;
	u8 req;
	u16 value1;
	u16 index;
	int blen;

	deb_info("%s %d", __func__, slot);
	if (slot != 0)
		return -EINVAL;

	mutex_lock(&state->ca_mutex);
	req = 0xC2;
	value1 = address;
	index = value;
	blen = 0;

	ret = az6027_usb_out_op(d, req, value1, index, NULL, blen);
	if (ret != 0)
		warn("usb out operation failed. (%d)", ret);

	mutex_unlock(&state->ca_mutex);
	return ret;
}

static int az6027_ci_read_cam_control(struct dvb_ca_en50221 *ca,
				      int slot,
				      u8 address)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6027_device_state *state = (struct az6027_device_state *)d->priv;

	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;
	u8 *b;

	if (slot != 0)
		return -EINVAL;

	b = kmalloc(12, GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	mutex_lock(&state->ca_mutex);

	req = 0xC3;
	value = address;
	index = 0;
	blen = 2;

	ret = az6027_usb_in_op(d, req, value, index, b, blen);
	if (ret < 0) {
		warn("usb in operation failed. (%d)", ret);
		ret = -EINVAL;
	} else {
		if (b[0] == 0)
			warn("Read CI IO error");

		ret = b[1];
		deb_info("read cam data = %x from 0x%x", b[1], value);
	}

	mutex_unlock(&state->ca_mutex);
	kfree(b);
	return ret;
}

static int az6027_ci_write_cam_control(struct dvb_ca_en50221 *ca,
				       int slot,
				       u8 address,
				       u8 value)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6027_device_state *state = (struct az6027_device_state *)d->priv;

	int ret;
	u8 req;
	u16 value1;
	u16 index;
	int blen;

	if (slot != 0)
		return -EINVAL;

	mutex_lock(&state->ca_mutex);
	req = 0xC4;
	value1 = address;
	index = value;
	blen = 0;

	ret = az6027_usb_out_op(d, req, value1, index, NULL, blen);
	if (ret != 0) {
		warn("usb out operation failed. (%d)", ret);
		goto failed;
	}

failed:
	mutex_unlock(&state->ca_mutex);
	return ret;
}

static int CI_CamReady(struct dvb_ca_en50221 *ca, int slot)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;

	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;
	u8 *b;

	b = kmalloc(12, GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	req = 0xC8;
	value = 0;
	index = 0;
	blen = 1;

	ret = az6027_usb_in_op(d, req, value, index, b, blen);
	if (ret < 0) {
		warn("usb in operation failed. (%d)", ret);
		ret = -EIO;
	} else{
		ret = b[0];
	}
	kfree(b);
	return ret;
}

static int az6027_ci_slot_reset(struct dvb_ca_en50221 *ca, int slot)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6027_device_state *state = (struct az6027_device_state *)d->priv;

	int ret, i;
	u8 req;
	u16 value;
	u16 index;
	int blen;

	mutex_lock(&state->ca_mutex);

	req = 0xC6;
	value = 1;
	index = 0;
	blen = 0;

	ret = az6027_usb_out_op(d, req, value, index, NULL, blen);
	if (ret != 0) {
		warn("usb out operation failed. (%d)", ret);
		goto failed;
	}

	msleep(500);
	req = 0xC6;
	value = 0;
	index = 0;
	blen = 0;

	ret = az6027_usb_out_op(d, req, value, index, NULL, blen);
	if (ret != 0) {
		warn("usb out operation failed. (%d)", ret);
		goto failed;
	}

	for (i = 0; i < 15; i++) {
		msleep(100);

		if (CI_CamReady(ca, slot)) {
			deb_info("CAM Ready");
			break;
		}
	}
	msleep(5000);

failed:
	mutex_unlock(&state->ca_mutex);
	return ret;
}

static int az6027_ci_slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
	return 0;
}

static int az6027_ci_slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6027_device_state *state = (struct az6027_device_state *)d->priv;

	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;

	deb_info("%s", __func__);
	mutex_lock(&state->ca_mutex);
	req = 0xC7;
	value = 1;
	index = 0;
	blen = 0;

	ret = az6027_usb_out_op(d, req, value, index, NULL, blen);
	if (ret != 0) {
		warn("usb out operation failed. (%d)", ret);
		goto failed;
	}

failed:
	mutex_unlock(&state->ca_mutex);
	return ret;
}

static int az6027_ci_poll_slot_status(struct dvb_ca_en50221 *ca, int slot, int open)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6027_device_state *state = (struct az6027_device_state *)d->priv;
	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;
	u8 *b;

	b = kmalloc(12, GFP_KERNEL);
	if (!b)
		return -ENOMEM;
	mutex_lock(&state->ca_mutex);

	req = 0xC5;
	value = 0;
	index = 0;
	blen = 1;

	ret = az6027_usb_in_op(d, req, value, index, b, blen);
	if (ret < 0) {
		warn("usb in operation failed. (%d)", ret);
		ret = -EIO;
	} else
		ret = 0;

	if (!ret && b[0] == 1) {
		ret = DVB_CA_EN50221_POLL_CAM_PRESENT |
		      DVB_CA_EN50221_POLL_CAM_READY;
	}

	mutex_unlock(&state->ca_mutex);
	kfree(b);
	return ret;
}


static void az6027_ci_uninit(struct dvb_usb_device *d)
{
	struct az6027_device_state *state;

	deb_info("%s", __func__);

	if (NULL == d)
		return;

	state = (struct az6027_device_state *)d->priv;
	if (NULL == state)
		return;

	if (NULL == state->ca.data)
		return;

	dvb_ca_en50221_release(&state->ca);

	memset(&state->ca, 0, sizeof(state->ca));
}


static int az6027_ci_init(struct dvb_usb_adapter *a)
{
	struct dvb_usb_device *d = a->dev;
	struct az6027_device_state *state = (struct az6027_device_state *)d->priv;
	int ret;

	deb_info("%s", __func__);

	mutex_init(&state->ca_mutex);

	state->ca.owner			= THIS_MODULE;
	state->ca.read_attribute_mem	= az6027_ci_read_attribute_mem;
	state->ca.write_attribute_mem	= az6027_ci_write_attribute_mem;
	state->ca.read_cam_control	= az6027_ci_read_cam_control;
	state->ca.write_cam_control	= az6027_ci_write_cam_control;
	state->ca.slot_reset		= az6027_ci_slot_reset;
	state->ca.slot_shutdown		= az6027_ci_slot_shutdown;
	state->ca.slot_ts_enable	= az6027_ci_slot_ts_enable;
	state->ca.poll_slot_status	= az6027_ci_poll_slot_status;
	state->ca.data			= d;

	ret = dvb_ca_en50221_init(&a->dvb_adap,
				  &state->ca,
				  0, /* flags */
				  1);/* n_slots */
	if (ret != 0) {
		err("Cannot initialize CI: Error %d.", ret);
		memset(&state->ca, 0, sizeof(state->ca));
		return ret;
	}

	deb_info("CI initialized.");

	return 0;
}

/*
static int az6027_read_mac_addr(struct dvb_usb_device *d, u8 mac[6])
{
	az6027_usb_in_op(d, 0xb7, 6, 0, &mac[0], 6);
	return 0;
}
*/

static int az6027_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{

	u8 buf;
	struct dvb_usb_adapter *adap = fe->dvb->priv;

	struct i2c_msg i2c_msg = {
		.addr	= 0x99,
		.flags	= 0,
		.buf	= &buf,
		.len	= 1
	};

	/*
	 * 2   --18v
	 * 1   --13v
	 * 0   --off
	 */
	switch (voltage) {
	case SEC_VOLTAGE_13:
		buf = 1;
		i2c_transfer(&adap->dev->i2c_adap, &i2c_msg, 1);
		break;

	case SEC_VOLTAGE_18:
		buf = 2;
		i2c_transfer(&adap->dev->i2c_adap, &i2c_msg, 1);
		break;

	case SEC_VOLTAGE_OFF:
		buf = 0;
		i2c_transfer(&adap->dev->i2c_adap, &i2c_msg, 1);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}


static int az6027_frontend_poweron(struct dvb_usb_adapter *adap)
{
	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;

	req = 0xBC;
	value = 1; /* power on */
	index = 3;
	blen = 0;

	ret = az6027_usb_out_op(adap->dev, req, value, index, NULL, blen);
	if (ret != 0)
		return -EIO;

	return 0;
}
static int az6027_frontend_reset(struct dvb_usb_adapter *adap)
{
	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;

	/* reset demodulator */
	req = 0xC0;
	value = 1; /* high */
	index = 3;
	blen = 0;

	ret = az6027_usb_out_op(adap->dev, req, value, index, NULL, blen);
	if (ret != 0)
		return -EIO;

	req = 0xC0;
	value = 0; /* low */
	index = 3;
	blen = 0;
	msleep_interruptible(200);

	ret = az6027_usb_out_op(adap->dev, req, value, index, NULL, blen);
	if (ret != 0)
		return -EIO;

	msleep_interruptible(200);

	req = 0xC0;
	value = 1; /*high */
	index = 3;
	blen = 0;

	ret = az6027_usb_out_op(adap->dev, req, value, index, NULL, blen);
	if (ret != 0)
		return -EIO;

	msleep_interruptible(200);
	return 0;
}

static int az6027_frontend_tsbypass(struct dvb_usb_adapter *adap, int onoff)
{
	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;

	/* TS passthrough */
	req = 0xC7;
	value = onoff;
	index = 0;
	blen = 0;

	ret = az6027_usb_out_op(adap->dev, req, value, index, NULL, blen);
	if (ret != 0)
		return -EIO;

	return 0;
}

static int az6027_frontend_attach(struct dvb_usb_adapter *adap)
{

	az6027_frontend_poweron(adap);
	az6027_frontend_reset(adap);

	deb_info("adap = %p, dev = %p\n", adap, adap->dev);
	adap->fe_adap[0].fe = stb0899_attach(&az6027_stb0899_config, &adap->dev->i2c_adap);

	if (adap->fe_adap[0].fe) {
		deb_info("found STB0899 DVB-S/DVB-S2 frontend @0x%02x", az6027_stb0899_config.demod_address);
		if (stb6100_attach(adap->fe_adap[0].fe, &az6027_stb6100_config, &adap->dev->i2c_adap)) {
			deb_info("found STB6100 DVB-S/DVB-S2 frontend @0x%02x", az6027_stb6100_config.tuner_address);
			adap->fe_adap[0].fe->ops.set_voltage = az6027_set_voltage;
			az6027_ci_init(adap);
		} else {
			adap->fe_adap[0].fe = NULL;
		}
	} else
		warn("no front-end attached\n");

	az6027_frontend_tsbypass(adap, 0);

	return 0;
}

static struct dvb_usb_device_properties az6027_properties;

static void az6027_usb_disconnect(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	az6027_ci_uninit(d);
	dvb_usb_device_exit(intf);
}


static int az6027_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf,
				   &az6027_properties,
				   THIS_MODULE,
				   NULL,
				   adapter_nr);
}

/* I2C */
static int az6027_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[], int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i = 0, j = 0, len = 0;
	u16 index;
	u16 value;
	int length;
	u8 req;
	u8 *data;

	data = kmalloc(256, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0) {
		kfree(data);
		return -EAGAIN;
	}

	if (num > 2)
		warn("more than 2 i2c messages at a time is not handled yet. TODO.");

	for (i = 0; i < num; i++) {

		if (msg[i].addr == 0x99) {
			req = 0xBE;
			index = 0;
			value = msg[i].buf[0] & 0x00ff;
			length = 1;
			az6027_usb_out_op(d, req, value, index, data, length);
		}

		if (msg[i].addr == 0xd0) {
			/* write/read request */
			if (i + 1 < num && (msg[i + 1].flags & I2C_M_RD)) {
				req = 0xB9;
				index = (((msg[i].buf[0] << 8) & 0xff00) | (msg[i].buf[1] & 0x00ff));
				value = msg[i].addr + (msg[i].len << 8);
				length = msg[i + 1].len + 6;
				az6027_usb_in_op(d, req, value, index, data, length);
				len = msg[i + 1].len;
				for (j = 0; j < len; j++)
					msg[i + 1].buf[j] = data[j + 5];

				i++;
			} else {

				/* demod 16bit addr */
				req = 0xBD;
				index = (((msg[i].buf[0] << 8) & 0xff00) | (msg[i].buf[1] & 0x00ff));
				value = msg[i].addr + (2 << 8);
				length = msg[i].len - 2;
				len = msg[i].len - 2;
				for (j = 0; j < len; j++)
					data[j] = msg[i].buf[j + 2];
				az6027_usb_out_op(d, req, value, index, data, length);
			}
		}

		if (msg[i].addr == 0xc0) {
			if (msg[i].flags & I2C_M_RD) {

				req = 0xB9;
				index = 0x0;
				value = msg[i].addr;
				length = msg[i].len + 6;
				az6027_usb_in_op(d, req, value, index, data, length);
				len = msg[i].len;
				for (j = 0; j < len; j++)
					msg[i].buf[j] = data[j + 5];

			} else {

				req = 0xBD;
				index = msg[i].buf[0] & 0x00FF;
				value = msg[i].addr + (1 << 8);
				length = msg[i].len - 1;
				len = msg[i].len - 1;

				for (j = 0; j < len; j++)
					data[j] = msg[i].buf[j + 1];

				az6027_usb_out_op(d, req, value, index, data, length);
			}
		}
	}
	mutex_unlock(&d->i2c_mutex);
	kfree(data);

	return i;
}


static u32 az6027_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm az6027_i2c_algo = {
	.master_xfer   = az6027_i2c_xfer,
	.functionality = az6027_i2c_func,
};

static int az6027_identify_state(struct usb_device *udev,
				 struct dvb_usb_device_properties *props,
				 struct dvb_usb_device_description **desc,
				 int *cold)
{
	u8 *b;
	s16 ret;

	b = kmalloc(16, GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	ret = usb_control_msg(udev,
				  usb_rcvctrlpipe(udev, 0),
				  0xb7,
				  USB_TYPE_VENDOR | USB_DIR_IN,
				  6,
				  0,
				  b,
				  6,
				  USB_CTRL_GET_TIMEOUT);

	*cold = ret <= 0;
	kfree(b);
	deb_info("cold: %d\n", *cold);
	return 0;
}


static struct usb_device_id az6027_usb_table[] = {
	{ USB_DEVICE(USB_VID_AZUREWAVE, USB_PID_AZUREWAVE_AZ6027) },
	{ USB_DEVICE(USB_VID_TERRATEC,  USB_PID_TERRATEC_DVBS2CI_V1) },
	{ USB_DEVICE(USB_VID_TERRATEC,  USB_PID_TERRATEC_DVBS2CI_V2) },
	{ USB_DEVICE(USB_VID_TECHNISAT, USB_PID_TECHNISAT_USB2_HDCI_V1) },
	{ USB_DEVICE(USB_VID_TECHNISAT, USB_PID_TECHNISAT_USB2_HDCI_V2) },
	{ USB_DEVICE(USB_VID_ELGATO, USB_PID_ELGATO_EYETV_SAT) },
	{ },
};

MODULE_DEVICE_TABLE(usb, az6027_usb_table);

static struct dvb_usb_device_properties az6027_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.usb_ctrl = CYPRESS_FX2,
	.firmware            = "dvb-usb-az6027-03.fw",
	.no_reconnect        = 1,

	.size_of_priv     = sizeof(struct az6027_device_state),
	.identify_state		= az6027_identify_state,
	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = az6027_streaming_ctrl,
			.frontend_attach  = az6027_frontend_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 10,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 4096,
					}
				}
			},
		}},
		}
	},
/*
	.power_ctrl       = az6027_power_ctrl,
	.read_mac_address = az6027_read_mac_addr,
 */
	.rc.legacy = {
		.rc_map_table     = rc_map_az6027_table,
		.rc_map_size      = ARRAY_SIZE(rc_map_az6027_table),
		.rc_interval      = 400,
		.rc_query         = az6027_rc_query,
	},

	.i2c_algo         = &az6027_i2c_algo,

	.num_device_descs = 6,
	.devices = {
		{
			.name = "AZUREWAVE DVB-S/S2 USB2.0 (AZ6027)",
			.cold_ids = { &az6027_usb_table[0], NULL },
			.warm_ids = { NULL },
		}, {
			.name = "TERRATEC S7",
			.cold_ids = { &az6027_usb_table[1], NULL },
			.warm_ids = { NULL },
		}, {
			.name = "TERRATEC S7 MKII",
			.cold_ids = { &az6027_usb_table[2], NULL },
			.warm_ids = { NULL },
		}, {
			.name = "Technisat SkyStar USB 2 HD CI",
			.cold_ids = { &az6027_usb_table[3], NULL },
			.warm_ids = { NULL },
		}, {
			.name = "Technisat SkyStar USB 2 HD CI",
			.cold_ids = { &az6027_usb_table[4], NULL },
			.warm_ids = { NULL },
		}, {
			.name = "Elgato EyeTV Sat",
			.cold_ids = { &az6027_usb_table[5], NULL },
			.warm_ids = { NULL },
		},
		{ NULL },
	}
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver az6027_usb_driver = {
	.name		= "dvb_usb_az6027",
	.probe 		= az6027_usb_probe,
	.disconnect 	= az6027_usb_disconnect,
	.id_table 	= az6027_usb_table,
};

module_usb_driver(az6027_usb_driver);

MODULE_AUTHOR("Adams Xu <Adams.xu@azwave.com.cn>");
MODULE_DESCRIPTION("Driver for AZUREWAVE DVB-S/S2 USB2.0 (AZ6027)");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
