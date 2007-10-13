/*
 *
 * i2c tv tuner chip device driver
 * core core, i.e. kernel interfaces, registering and so on
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/videodev.h>
#include <media/tuner.h>
#include <media/tuner-types.h>
#include <media/v4l2-common.h>
#include "tuner-driver.h"
#include "mt20xx.h"
#include "tda8290.h"
#include "tea5761.h"
#include "tea5767.h"
#include "tuner-simple.h"

#define UNSET (-1U)

/* standard i2c insmod options */
static unsigned short normal_i2c[] = {
#ifdef CONFIG_TUNER_TEA5761
	0x10,
#endif
	0x42, 0x43, 0x4a, 0x4b,			/* tda8290 */
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	I2C_CLIENT_END
};

I2C_CLIENT_INSMOD;

/* insmod options used at init time => read/only */
static unsigned int addr = 0;
static unsigned int no_autodetect = 0;
static unsigned int show_i2c = 0;

/* insmod options used at runtime => read/write */
int tuner_debug = 0;

static unsigned int tv_range[2] = { 44, 958 };
static unsigned int radio_range[2] = { 65, 108 };

static char pal[] = "--";
static char secam[] = "--";
static char ntsc[] = "-";


module_param(addr, int, 0444);
module_param(no_autodetect, int, 0444);
module_param(show_i2c, int, 0444);
module_param_named(debug,tuner_debug, int, 0644);
module_param_string(pal, pal, sizeof(pal), 0644);
module_param_string(secam, secam, sizeof(secam), 0644);
module_param_string(ntsc, ntsc, sizeof(ntsc), 0644);
module_param_array(tv_range, int, NULL, 0644);
module_param_array(radio_range, int, NULL, 0644);

MODULE_DESCRIPTION("device driver for various TV and TV+FM radio tuners");
MODULE_AUTHOR("Ralph Metzler, Gerd Knorr, Gunther Mayer");
MODULE_LICENSE("GPL");

static struct i2c_driver driver;
static struct i2c_client client_template;

/* ---------------------------------------------------------------------- */

static void fe_set_freq(struct tuner *t, unsigned int freq)
{
	struct dvb_tuner_ops *fe_tuner_ops = &t->fe.ops.tuner_ops;

	struct analog_parameters params = {
		.frequency = freq,
		.mode      = t->mode,
		.audmode   = t->audmode,
		.std       = t->std
	};

	if (NULL == fe_tuner_ops->set_analog_params) {
		tuner_warn("Tuner frontend module has no way to set freq\n");
		return;
	}
	fe_tuner_ops->set_analog_params(&t->fe, &params);
}

static void fe_release(struct tuner *t)
{
	struct dvb_tuner_ops *fe_tuner_ops = &t->fe.ops.tuner_ops;

	if (fe_tuner_ops->release)
		fe_tuner_ops->release(&t->fe);
}

static void fe_standby(struct tuner *t)
{
	struct dvb_tuner_ops *fe_tuner_ops = &t->fe.ops.tuner_ops;

	if (fe_tuner_ops->sleep)
		fe_tuner_ops->sleep(&t->fe);
}

static int fe_has_signal(struct tuner *t)
{
	struct dvb_tuner_ops *fe_tuner_ops = &t->fe.ops.tuner_ops;
	u16 strength;

	if (fe_tuner_ops->get_rf_strength)
		fe_tuner_ops->get_rf_strength(&t->fe, &strength);

	return strength;
}

/* Set tuner frequency,  freq in Units of 62.5kHz = 1/16MHz */
static void set_tv_freq(struct i2c_client *c, unsigned int freq)
{
	struct tuner *t = i2c_get_clientdata(c);

	if (t->type == UNSET) {
		tuner_warn ("tuner type not set\n");
		return;
	}
	if (NULL == t->ops.set_tv_freq) {
		tuner_warn ("Tuner has no way to set tv freq\n");
		return;
	}
	if (freq < tv_range[0] * 16 || freq > tv_range[1] * 16) {
		tuner_dbg ("TV freq (%d.%02d) out of range (%d-%d)\n",
			   freq / 16, freq % 16 * 100 / 16, tv_range[0],
			   tv_range[1]);
		/* V4L2 spec: if the freq is not possible then the closest
		   possible value should be selected */
		if (freq < tv_range[0] * 16)
			freq = tv_range[0] * 16;
		else
			freq = tv_range[1] * 16;
	}
	t->ops.set_tv_freq(t, freq);
}

static void set_radio_freq(struct i2c_client *c, unsigned int freq)
{
	struct tuner *t = i2c_get_clientdata(c);

	if (t->type == UNSET) {
		tuner_warn ("tuner type not set\n");
		return;
	}
	if (NULL == t->ops.set_radio_freq) {
		tuner_warn ("tuner has no way to set radio frequency\n");
		return;
	}
	if (freq < radio_range[0] * 16000 || freq > radio_range[1] * 16000) {
		tuner_dbg ("radio freq (%d.%02d) out of range (%d-%d)\n",
			   freq / 16000, freq % 16000 * 100 / 16000,
			   radio_range[0], radio_range[1]);
		/* V4L2 spec: if the freq is not possible then the closest
		   possible value should be selected */
		if (freq < radio_range[0] * 16000)
			freq = radio_range[0] * 16000;
		else
			freq = radio_range[1] * 16000;
	}

	t->ops.set_radio_freq(t, freq);
}

static void set_freq(struct i2c_client *c, unsigned long freq)
{
	struct tuner *t = i2c_get_clientdata(c);

	switch (t->mode) {
	case V4L2_TUNER_RADIO:
		tuner_dbg("radio freq set to %lu.%02lu\n",
			  freq / 16000, freq % 16000 * 100 / 16000);
		set_radio_freq(c, freq);
		t->radio_freq = freq;
		break;
	case V4L2_TUNER_ANALOG_TV:
	case V4L2_TUNER_DIGITAL_TV:
		tuner_dbg("tv freq set to %lu.%02lu\n",
			  freq / 16, freq % 16 * 100 / 16);
		set_tv_freq(c, freq);
		t->tv_freq = freq;
		break;
	}
}

static void tuner_i2c_address_check(struct tuner *t)
{
	if ((t->type == UNSET || t->type == TUNER_ABSENT) ||
	    ((t->i2c.addr < 0x64) || (t->i2c.addr > 0x6f)))
		return;

	tuner_warn("====================== WARNING! ======================\n");
	tuner_warn("Support for tuners in i2c address range 0x64 thru 0x6f\n");
	tuner_warn("will soon be dropped. This message indicates that your\n");
	tuner_warn("hardware has a %s tuner at i2c address 0x%02x.\n",
		   t->i2c.name, t->i2c.addr);
	tuner_warn("To ensure continued support for your device, please\n");
	tuner_warn("send a copy of this message, along with full dmesg\n");
	tuner_warn("output to v4l-dvb-maintainer@linuxtv.org\n");
	tuner_warn("Please use subject line: \"obsolete tuner i2c address.\"\n");
	tuner_warn("driver: %s, addr: 0x%02x, type: %d (%s)\n",
		   t->i2c.adapter->name, t->i2c.addr, t->type,
		   tuners[t->type].name);
	tuner_warn("====================== WARNING! ======================\n");
}

static void attach_tda8290(struct tuner *t)
{
	struct tda8290_config cfg = {
		.lna_cfg        = &t->config,
		.tuner_callback = t->tuner_callback
	};
	tda8290_attach(&t->fe, t->i2c.adapter, t->i2c.addr, &cfg);
}

static void attach_simple_tuner(struct tuner *t)
{
	struct simple_tuner_config cfg = {
		.type = t->type,
		.tun  = &tuners[t->type]
	};
	simple_tuner_attach(&t->fe, t->i2c.adapter, t->i2c.addr, &cfg);
}

static void set_type(struct i2c_client *c, unsigned int type,
		     unsigned int new_mode_mask, unsigned int new_config,
		     int (*tuner_callback) (void *dev, int command,int arg))
{
	struct tuner *t = i2c_get_clientdata(c);
	struct dvb_tuner_ops *fe_tuner_ops = &t->fe.ops.tuner_ops;
	unsigned char buffer[4];

	if (type == UNSET || type == TUNER_ABSENT) {
		tuner_dbg ("tuner 0x%02x: Tuner type absent\n",c->addr);
		return;
	}

	if (type >= tuner_count) {
		tuner_warn ("tuner 0x%02x: Tuner count greater than %d\n",c->addr,tuner_count);
		return;
	}

	t->type = type;
	t->config = new_config;
	if (tuner_callback != NULL) {
		tuner_dbg("defining GPIO callback\n");
		t->tuner_callback = tuner_callback;
	}

	/* This code detects calls by card attach_inform */
	if (NULL == t->i2c.dev.driver) {
		tuner_dbg ("tuner 0x%02x: called during i2c_client register by adapter's attach_inform\n", c->addr);

		return;
	}

	/* discard private data, in case set_type() was previously called */
	if (t->ops.release)
		t->ops.release(t);
	else {
		kfree(t->priv);
		t->priv = NULL;
	}

	switch (t->type) {
	case TUNER_MT2032:
		microtune_attach(&t->fe, t->i2c.adapter, t->i2c.addr);
		break;
	case TUNER_PHILIPS_TDA8290:
	{
		attach_tda8290(t);
		break;
	}
	case TUNER_TEA5767:
		if (tea5767_attach(&t->fe, t->i2c.adapter, t->i2c.addr) == NULL) {
			t->type = TUNER_ABSENT;
			t->mode_mask = T_UNINITIALIZED;
			return;
		}
		t->mode_mask = T_RADIO;
		break;
#ifdef CONFIG_TUNER_TEA5761
	case TUNER_TEA5761:
		if (tea5761_attach(&t->fe, t->i2c.adapter, t->i2c.addr) == NULL) {
			t->type = TUNER_ABSENT;
			t->mode_mask = T_UNINITIALIZED;
			return;
		}
		t->mode_mask = T_RADIO;
		break;
#endif
	case TUNER_PHILIPS_FMD1216ME_MK3:
		buffer[0] = 0x0b;
		buffer[1] = 0xdc;
		buffer[2] = 0x9c;
		buffer[3] = 0x60;
		i2c_master_send(c, buffer, 4);
		mdelay(1);
		buffer[2] = 0x86;
		buffer[3] = 0x54;
		i2c_master_send(c, buffer, 4);
		attach_simple_tuner(t);
		break;
	case TUNER_PHILIPS_TD1316:
		buffer[0] = 0x0b;
		buffer[1] = 0xdc;
		buffer[2] = 0x86;
		buffer[3] = 0xa4;
		i2c_master_send(c,buffer,4);
		attach_simple_tuner(t);
		break;
	case TUNER_TDA9887:
		tda9887_tuner_init(t);
		break;
	default:
		attach_simple_tuner(t);
		break;
	}

	if (fe_tuner_ops->set_analog_params) {
		strlcpy(t->i2c.name, fe_tuner_ops->info.name, sizeof(t->i2c.name));

		t->ops.set_tv_freq    = fe_set_freq;
		t->ops.set_radio_freq = fe_set_freq;
		t->ops.standby        = fe_standby;
		t->ops.release        = fe_release;
		t->ops.has_signal     = fe_has_signal;
	}

	tuner_info("type set to %s\n", t->i2c.name);

	if (t->mode_mask == T_UNINITIALIZED)
		t->mode_mask = new_mode_mask;

	set_freq(c, (V4L2_TUNER_RADIO == t->mode) ? t->radio_freq : t->tv_freq);
	tuner_dbg("%s %s I2C addr 0x%02x with type %d used for 0x%02x\n",
		  c->adapter->name, c->driver->driver.name, c->addr << 1, type,
		  t->mode_mask);
	tuner_i2c_address_check(t);
}

/*
 * This function apply tuner config to tuner specified
 * by tun_setup structure. I addr is unset, then admin status
 * and tun addr status is more precise then current status,
 * it's applied. Otherwise status and type are applied only to
 * tuner with exactly the same addr.
*/

static void set_addr(struct i2c_client *c, struct tuner_setup *tun_setup)
{
	struct tuner *t = i2c_get_clientdata(c);

	tuner_dbg("set addr for type %i\n", t->type);

	if ( (t->type == UNSET && ((tun_setup->addr == ADDR_UNSET) &&
		(t->mode_mask & tun_setup->mode_mask))) ||
		(tun_setup->addr == c->addr)) {
			set_type(c, tun_setup->type, tun_setup->mode_mask,
				 tun_setup->config, tun_setup->tuner_callback);
	}
}

static inline int check_mode(struct tuner *t, char *cmd)
{
	if ((1 << t->mode & t->mode_mask) == 0) {
		return EINVAL;
	}

	switch (t->mode) {
	case V4L2_TUNER_RADIO:
		tuner_dbg("Cmd %s accepted for radio\n", cmd);
		break;
	case V4L2_TUNER_ANALOG_TV:
		tuner_dbg("Cmd %s accepted for analog TV\n", cmd);
		break;
	case V4L2_TUNER_DIGITAL_TV:
		tuner_dbg("Cmd %s accepted for digital TV\n", cmd);
		break;
	}
	return 0;
}

/* get more precise norm info from insmod option */
static int tuner_fixup_std(struct tuner *t)
{
	if ((t->std & V4L2_STD_PAL) == V4L2_STD_PAL) {
		switch (pal[0]) {
		case '6':
			tuner_dbg ("insmod fixup: PAL => PAL-60\n");
			t->std = V4L2_STD_PAL_60;
			break;
		case 'b':
		case 'B':
		case 'g':
		case 'G':
			tuner_dbg ("insmod fixup: PAL => PAL-BG\n");
			t->std = V4L2_STD_PAL_BG;
			break;
		case 'i':
		case 'I':
			tuner_dbg ("insmod fixup: PAL => PAL-I\n");
			t->std = V4L2_STD_PAL_I;
			break;
		case 'd':
		case 'D':
		case 'k':
		case 'K':
			tuner_dbg ("insmod fixup: PAL => PAL-DK\n");
			t->std = V4L2_STD_PAL_DK;
			break;
		case 'M':
		case 'm':
			tuner_dbg ("insmod fixup: PAL => PAL-M\n");
			t->std = V4L2_STD_PAL_M;
			break;
		case 'N':
		case 'n':
			if (pal[1] == 'c' || pal[1] == 'C') {
				tuner_dbg("insmod fixup: PAL => PAL-Nc\n");
				t->std = V4L2_STD_PAL_Nc;
			} else {
				tuner_dbg ("insmod fixup: PAL => PAL-N\n");
				t->std = V4L2_STD_PAL_N;
			}
			break;
		case '-':
			/* default parameter, do nothing */
			break;
		default:
			tuner_warn ("pal= argument not recognised\n");
			break;
		}
	}
	if ((t->std & V4L2_STD_SECAM) == V4L2_STD_SECAM) {
		switch (secam[0]) {
		case 'b':
		case 'B':
		case 'g':
		case 'G':
		case 'h':
		case 'H':
			tuner_dbg("insmod fixup: SECAM => SECAM-BGH\n");
			t->std = V4L2_STD_SECAM_B | V4L2_STD_SECAM_G | V4L2_STD_SECAM_H;
			break;
		case 'd':
		case 'D':
		case 'k':
		case 'K':
			tuner_dbg ("insmod fixup: SECAM => SECAM-DK\n");
			t->std = V4L2_STD_SECAM_DK;
			break;
		case 'l':
		case 'L':
			if ((secam[1]=='C')||(secam[1]=='c')) {
				tuner_dbg ("insmod fixup: SECAM => SECAM-L'\n");
				t->std = V4L2_STD_SECAM_LC;
			} else {
				tuner_dbg ("insmod fixup: SECAM => SECAM-L\n");
				t->std = V4L2_STD_SECAM_L;
			}
			break;
		case '-':
			/* default parameter, do nothing */
			break;
		default:
			tuner_warn ("secam= argument not recognised\n");
			break;
		}
	}

	if ((t->std & V4L2_STD_NTSC) == V4L2_STD_NTSC) {
		switch (ntsc[0]) {
		case 'm':
		case 'M':
			tuner_dbg("insmod fixup: NTSC => NTSC-M\n");
			t->std = V4L2_STD_NTSC_M;
			break;
		case 'j':
		case 'J':
			tuner_dbg("insmod fixup: NTSC => NTSC_M_JP\n");
			t->std = V4L2_STD_NTSC_M_JP;
			break;
		case 'k':
		case 'K':
			tuner_dbg("insmod fixup: NTSC => NTSC_M_KR\n");
			t->std = V4L2_STD_NTSC_M_KR;
			break;
		case '-':
			/* default parameter, do nothing */
			break;
		default:
			tuner_info("ntsc= argument not recognised\n");
			break;
		}
	}
	return 0;
}

static void tuner_status(struct tuner *t)
{
	unsigned long freq, freq_fraction;
	struct dvb_tuner_ops *fe_tuner_ops = &t->fe.ops.tuner_ops;
	const char *p;

	switch (t->mode) {
		case V4L2_TUNER_RADIO: 	    p = "radio"; break;
		case V4L2_TUNER_ANALOG_TV:  p = "analog TV"; break;
		case V4L2_TUNER_DIGITAL_TV: p = "digital TV"; break;
		default: p = "undefined"; break;
	}
	if (t->mode == V4L2_TUNER_RADIO) {
		freq = t->radio_freq / 16000;
		freq_fraction = (t->radio_freq % 16000) * 100 / 16000;
	} else {
		freq = t->tv_freq / 16;
		freq_fraction = (t->tv_freq % 16) * 100 / 16;
	}
	tuner_info("Tuner mode:      %s\n", p);
	tuner_info("Frequency:       %lu.%02lu MHz\n", freq, freq_fraction);
	tuner_info("Standard:        0x%08lx\n", (unsigned long)t->std);
	if (t->mode != V4L2_TUNER_RADIO)
	       return;
	if (fe_tuner_ops->get_status) {
		u32 tuner_status;

		fe_tuner_ops->get_status(&t->fe, &tuner_status);
		if (tuner_status & TUNER_STATUS_LOCKED)
			tuner_info("Tuner is locked.\n");
		if (tuner_status & TUNER_STATUS_STEREO)
			tuner_info("Stereo:          yes\n");
	}
	if (t->ops.has_signal) {
		tuner_info("Signal strength: %d\n", t->ops.has_signal(t));
	}
	if (t->ops.is_stereo) {
		tuner_info("Stereo:          %s\n", t->ops.is_stereo(t) ? "yes" : "no");
	}
}

/* ---------------------------------------------------------------------- */

/* static vars: used only in tuner_attach and tuner_probe */
static unsigned default_mode_mask;

/* During client attach, set_type is called by adapter's attach_inform callback.
   set_type must then be completed by tuner_attach.
 */
static int tuner_attach(struct i2c_adapter *adap, int addr, int kind)
{
	struct tuner *t;

	client_template.adapter = adap;
	client_template.addr = addr;

	t = kzalloc(sizeof(struct tuner), GFP_KERNEL);
	if (NULL == t)
		return -ENOMEM;
	memcpy(&t->i2c, &client_template, sizeof(struct i2c_client));
	i2c_set_clientdata(&t->i2c, t);
	t->type = UNSET;
	t->audmode = V4L2_TUNER_MODE_STEREO;
	t->mode_mask = T_UNINITIALIZED;
	t->ops.tuner_status = tuner_status;

	if (show_i2c) {
		unsigned char buffer[16];
		int i,rc;

		memset(buffer, 0, sizeof(buffer));
		rc = i2c_master_recv(&t->i2c, buffer, sizeof(buffer));
		tuner_info("I2C RECV = ");
		for (i=0;i<rc;i++)
			printk("%02x ",buffer[i]);
		printk("\n");
	}
	/* HACK: This test were added to avoid tuner to probe tda9840 and tea6415c on the MXB card */
	if (adap->id == I2C_HW_SAA7146 && addr < 0x4a)
		return -ENODEV;

	/* autodetection code based on the i2c addr */
	if (!no_autodetect) {
		switch (addr) {
#ifdef CONFIG_TUNER_TEA5761
		case 0x10:
			if (tea5761_autodetection(t->i2c.adapter, t->i2c.addr) != EINVAL) {
				t->type = TUNER_TEA5761;
				t->mode_mask = T_RADIO;
				t->mode = T_STANDBY;
				t->radio_freq = 87.5 * 16000; /* Sets freq to FM range */
				default_mode_mask &= ~T_RADIO;

				goto register_client;
			}
			break;
#endif
		case 0x42:
		case 0x43:
		case 0x4a:
		case 0x4b:
			/* If chip is not tda8290, don't register.
			   since it can be tda9887*/
			if (tda8290_probe(t->i2c.adapter, t->i2c.addr) == 0) {
				tuner_dbg("chip at addr %x is a tda8290\n", addr);
			} else {
				/* Default is being tda9887 */
				t->type = TUNER_TDA9887;
				t->mode_mask = T_RADIO | T_ANALOG_TV | T_DIGITAL_TV;
				t->mode = T_STANDBY;
				goto register_client;
			}
			break;
		case 0x60:
			if (tea5767_autodetection(t->i2c.adapter, t->i2c.addr) != EINVAL) {
				t->type = TUNER_TEA5767;
				t->mode_mask = T_RADIO;
				t->mode = T_STANDBY;
				t->radio_freq = 87.5 * 16000; /* Sets freq to FM range */
				default_mode_mask &= ~T_RADIO;

				goto register_client;
			}
			break;
		}
	}

	/* Initializes only the first adapter found */
	if (default_mode_mask != T_UNINITIALIZED) {
		tuner_dbg ("Setting mode_mask to 0x%02x\n", default_mode_mask);
		t->mode_mask = default_mode_mask;
		t->tv_freq = 400 * 16; /* Sets freq to VHF High */
		t->radio_freq = 87.5 * 16000; /* Sets freq to FM range */
		default_mode_mask = T_UNINITIALIZED;
	}

	/* Should be just before return */
register_client:
	tuner_info("chip found @ 0x%x (%s)\n", addr << 1, adap->name);
	i2c_attach_client (&t->i2c);
	set_type (&t->i2c,t->type, t->mode_mask, t->config, t->tuner_callback);
	return 0;
}

static int tuner_probe(struct i2c_adapter *adap)
{
	if (0 != addr) {
		normal_i2c[0] = addr;
		normal_i2c[1] = I2C_CLIENT_END;
	}

	/* HACK: Ignore 0x6b and 0x6f on cx88 boards.
	 * FusionHDTV5 RT Gold has an ir receiver at 0x6b
	 * and an RTC at 0x6f which can get corrupted if probed.
	 */
	if ((adap->id == I2C_HW_B_CX2388x) ||
	    (adap->id == I2C_HW_B_CX23885)) {
		unsigned int i = 0;

		while (i < I2C_CLIENT_MAX_OPTS && ignore[i] != I2C_CLIENT_END)
			i += 2;
		if (i + 4 < I2C_CLIENT_MAX_OPTS) {
			ignore[i+0] = adap->nr;
			ignore[i+1] = 0x6b;
			ignore[i+2] = adap->nr;
			ignore[i+3] = 0x6f;
			ignore[i+4] = I2C_CLIENT_END;
		} else
			printk(KERN_WARNING "tuner: "
			       "too many options specified "
			       "in i2c probe ignore list!\n");
	}

	default_mode_mask = T_RADIO | T_ANALOG_TV | T_DIGITAL_TV;

	if (adap->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adap, &addr_data, tuner_attach);
	return 0;
}

static int tuner_detach(struct i2c_client *client)
{
	struct tuner *t = i2c_get_clientdata(client);
	int err;

	err = i2c_detach_client(&t->i2c);
	if (err) {
		tuner_warn
		    ("Client deregistration failed, client not detached.\n");
		return err;
	}

	if (t->ops.release)
		t->ops.release(t);
	else {
		kfree(t->priv);
	}
	kfree(t);
	return 0;
}

/*
 * Switch tuner to other mode. If tuner support both tv and radio,
 * set another frequency to some value (This is needed for some pal
 * tuners to avoid locking). Otherwise, just put second tuner in
 * standby mode.
 */

static inline int set_mode(struct i2c_client *client, struct tuner *t, int mode, char *cmd)
{
	if (mode == t->mode)
		return 0;

	t->mode = mode;

	if (check_mode(t, cmd) == EINVAL) {
		t->mode = T_STANDBY;
		if (t->ops.standby)
			t->ops.standby(t);
		return EINVAL;
	}
	return 0;
}

#define switch_v4l2()	if (!t->using_v4l2) \
			    tuner_dbg("switching to v4l2\n"); \
			t->using_v4l2 = 1;

static inline int check_v4l2(struct tuner *t)
{
	/* bttv still uses both v4l1 and v4l2 calls to the tuner (v4l2 for
	   TV, v4l1 for radio), until that is fixed this code is disabled.
	   Otherwise the radio (v4l1) wouldn't tune after using the TV (v4l2)
	   first. */
	return 0;
}

static int tuner_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct tuner *t = i2c_get_clientdata(client);
	struct dvb_tuner_ops *fe_tuner_ops = &t->fe.ops.tuner_ops;

	if (tuner_debug>1)
		v4l_i2c_print_ioctl(&(t->i2c),cmd);

	switch (cmd) {
	/* --- configuration --- */
	case TUNER_SET_TYPE_ADDR:
		tuner_dbg ("Calling set_type_addr for type=%d, addr=0x%02x, mode=0x%02x, config=0x%02x\n",
				((struct tuner_setup *)arg)->type,
				((struct tuner_setup *)arg)->addr,
				((struct tuner_setup *)arg)->mode_mask,
				((struct tuner_setup *)arg)->config);

		set_addr(client, (struct tuner_setup *)arg);
		break;
	case AUDC_SET_RADIO:
		if (set_mode(client, t, V4L2_TUNER_RADIO, "AUDC_SET_RADIO")
				== EINVAL)
			return 0;
		if (t->radio_freq)
			set_freq(client, t->radio_freq);
		break;
	case TUNER_SET_STANDBY:
		if (check_mode(t, "TUNER_SET_STANDBY") == EINVAL)
			return 0;
		t->mode = T_STANDBY;
		if (t->ops.standby)
			t->ops.standby(t);
		break;
#ifdef CONFIG_VIDEO_V4L1
	case VIDIOCSAUDIO:
		if (check_mode(t, "VIDIOCSAUDIO") == EINVAL)
			return 0;
		if (check_v4l2(t) == EINVAL)
			return 0;

		/* Should be implemented, since bttv calls it */
		tuner_dbg("VIDIOCSAUDIO not implemented.\n");
		break;
	case VIDIOCSCHAN:
		{
			static const v4l2_std_id map[] = {
				[VIDEO_MODE_PAL] = V4L2_STD_PAL,
				[VIDEO_MODE_NTSC] = V4L2_STD_NTSC_M,
				[VIDEO_MODE_SECAM] = V4L2_STD_SECAM,
				[4 /* bttv */ ] = V4L2_STD_PAL_M,
				[5 /* bttv */ ] = V4L2_STD_PAL_N,
				[6 /* bttv */ ] = V4L2_STD_NTSC_M_JP,
			};
			struct video_channel *vc = arg;

			if (check_v4l2(t) == EINVAL)
				return 0;

			if (set_mode(client,t,V4L2_TUNER_ANALOG_TV, "VIDIOCSCHAN")==EINVAL)
				return 0;

			if (vc->norm < ARRAY_SIZE(map))
				t->std = map[vc->norm];
			tuner_fixup_std(t);
			if (t->tv_freq)
				set_tv_freq(client, t->tv_freq);
			return 0;
		}
	case VIDIOCSFREQ:
		{
			unsigned long *v = arg;

			if (check_mode(t, "VIDIOCSFREQ") == EINVAL)
				return 0;
			if (check_v4l2(t) == EINVAL)
				return 0;

			set_freq(client, *v);
			return 0;
		}
	case VIDIOCGTUNER:
		{
			struct video_tuner *vt = arg;

			if (check_mode(t, "VIDIOCGTUNER") == EINVAL)
				return 0;
			if (check_v4l2(t) == EINVAL)
				return 0;

			if (V4L2_TUNER_RADIO == t->mode) {
				if (fe_tuner_ops->get_status) {
					u32 tuner_status;

					fe_tuner_ops->get_status(&t->fe, &tuner_status);
					if (tuner_status & TUNER_STATUS_STEREO)
						vt->flags |= VIDEO_TUNER_STEREO_ON;
					else
						vt->flags &= ~VIDEO_TUNER_STEREO_ON;
				} else {
					if (t->ops.is_stereo) {
						if (t->ops.is_stereo(t))
							vt->flags |=
								VIDEO_TUNER_STEREO_ON;
						else
							vt->flags &=
								~VIDEO_TUNER_STEREO_ON;
					}
				}
				if (t->ops.has_signal)
					vt->signal = t->ops.has_signal(t);

				vt->flags |= VIDEO_TUNER_LOW;	/* Allow freqs at 62.5 Hz */

				vt->rangelow = radio_range[0] * 16000;
				vt->rangehigh = radio_range[1] * 16000;

			} else {
				vt->rangelow = tv_range[0] * 16;
				vt->rangehigh = tv_range[1] * 16;
			}

			return 0;
		}
	case VIDIOCGAUDIO:
		{
			struct video_audio *va = arg;

			if (check_mode(t, "VIDIOCGAUDIO") == EINVAL)
				return 0;
			if (check_v4l2(t) == EINVAL)
				return 0;

			if (V4L2_TUNER_RADIO == t->mode) {
				if (fe_tuner_ops->get_status) {
					u32 tuner_status;

					fe_tuner_ops->get_status(&t->fe, &tuner_status);
					va->mode = (tuner_status & TUNER_STATUS_STEREO)
					    ? VIDEO_SOUND_STEREO : VIDEO_SOUND_MONO;
				} else if (t->ops.is_stereo)
					va->mode = t->ops.is_stereo(t)
					    ? VIDEO_SOUND_STEREO : VIDEO_SOUND_MONO;
			}
			return 0;
		}
#endif
	case TDA9887_SET_CONFIG:
		if (t->type == TUNER_TDA9887) {
			int *i = arg;

			t->tda9887_config = *i;
			set_freq(client, t->tv_freq);
		}
		break;
	/* --- v4l ioctls --- */
	/* take care: bttv does userspace copying, we'll get a
	   kernel pointer here... */
	case VIDIOC_S_STD:
		{
			v4l2_std_id *id = arg;

			if (set_mode (client, t, V4L2_TUNER_ANALOG_TV, "VIDIOC_S_STD")
					== EINVAL)
				return 0;

			switch_v4l2();

			t->std = *id;
			tuner_fixup_std(t);
			if (t->tv_freq)
				set_freq(client, t->tv_freq);
			break;
		}
	case VIDIOC_S_FREQUENCY:
		{
			struct v4l2_frequency *f = arg;

			if (set_mode (client, t, f->type, "VIDIOC_S_FREQUENCY")
					== EINVAL)
				return 0;
			switch_v4l2();
			set_freq(client,f->frequency);

			break;
		}
	case VIDIOC_G_FREQUENCY:
		{
			struct v4l2_frequency *f = arg;

			if (check_mode(t, "VIDIOC_G_FREQUENCY") == EINVAL)
				return 0;
			switch_v4l2();
			f->type = t->mode;
			if (fe_tuner_ops->get_frequency) {
				u32 abs_freq;

				fe_tuner_ops->get_frequency(&t->fe, &abs_freq);
				f->frequency = (V4L2_TUNER_RADIO == t->mode) ?
					(abs_freq * 2 + 125/2) / 125 :
					(abs_freq + 62500/2) / 62500;
				break;
			}
			f->frequency = (V4L2_TUNER_RADIO == t->mode) ?
				t->radio_freq : t->tv_freq;
			break;
		}
	case VIDIOC_G_TUNER:
		{
			struct v4l2_tuner *tuner = arg;

			if (check_mode(t, "VIDIOC_G_TUNER") == EINVAL)
				return 0;
			switch_v4l2();

			tuner->type = t->mode;
			if (t->ops.get_afc)
				tuner->afc=t->ops.get_afc(t);
			if (t->mode == V4L2_TUNER_ANALOG_TV)
				tuner->capability |= V4L2_TUNER_CAP_NORM;
			if (t->mode != V4L2_TUNER_RADIO) {
				tuner->rangelow = tv_range[0] * 16;
				tuner->rangehigh = tv_range[1] * 16;
				break;
			}

			/* radio mode */
			tuner->rxsubchans =
				V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
			if (fe_tuner_ops->get_status) {
				u32 tuner_status;

				fe_tuner_ops->get_status(&t->fe, &tuner_status);
				tuner->rxsubchans = (tuner_status & TUNER_STATUS_STEREO) ?
					V4L2_TUNER_SUB_STEREO : V4L2_TUNER_SUB_MONO;
			} else {
				if (t->ops.is_stereo) {
					tuner->rxsubchans = t->ops.is_stereo(t) ?
						V4L2_TUNER_SUB_STEREO : V4L2_TUNER_SUB_MONO;
				}
			}
			if (t->ops.has_signal)
				tuner->signal = t->ops.has_signal(t);
			tuner->capability |=
			    V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO;
			tuner->audmode = t->audmode;
			tuner->rangelow = radio_range[0] * 16000;
			tuner->rangehigh = radio_range[1] * 16000;
			break;
		}
	case VIDIOC_S_TUNER:
		{
			struct v4l2_tuner *tuner = arg;

			if (check_mode(t, "VIDIOC_S_TUNER") == EINVAL)
				return 0;

			switch_v4l2();

			/* do nothing unless we're a radio tuner */
			if (t->mode != V4L2_TUNER_RADIO)
				break;
			t->audmode = tuner->audmode;
			set_radio_freq(client, t->radio_freq);
			break;
		}
	case VIDIOC_LOG_STATUS:
		if (t->ops.tuner_status)
			t->ops.tuner_status(t);
		break;
	}

	return 0;
}

static int tuner_suspend(struct i2c_client *c, pm_message_t state)
{
	struct tuner *t = i2c_get_clientdata (c);

	tuner_dbg ("suspend\n");
	/* FIXME: power down ??? */
	return 0;
}

static int tuner_resume(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata (c);

	tuner_dbg ("resume\n");
	if (V4L2_TUNER_RADIO == t->mode) {
		if (t->radio_freq)
			set_freq(c, t->radio_freq);
	} else {
		if (t->tv_freq)
			set_freq(c, t->tv_freq);
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver driver = {
	.id = I2C_DRIVERID_TUNER,
	.attach_adapter = tuner_probe,
	.detach_client = tuner_detach,
	.command = tuner_command,
	.suspend = tuner_suspend,
	.resume  = tuner_resume,
	.driver = {
		.name    = "tuner",
	},
};
static struct i2c_client client_template = {
	.name = "(tuner unset)",
	.driver = &driver,
};

static int __init tuner_init_module(void)
{
	return i2c_add_driver(&driver);
}

static void __exit tuner_cleanup_module(void)
{
	i2c_del_driver(&driver);
}

module_init(tuner_init_module);
module_exit(tuner_cleanup_module);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
