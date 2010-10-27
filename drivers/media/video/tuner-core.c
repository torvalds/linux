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
#include <linux/videodev2.h>
#include <media/tuner.h>
#include <media/tuner-types.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-i2c-drv.h>
#include "mt20xx.h"
#include "tda8290.h"
#include "tea5761.h"
#include "tea5767.h"
#include "tuner-xc2028.h"
#include "tuner-simple.h"
#include "tda9887.h"
#include "xc5000.h"
#include "tda18271.h"

#define UNSET (-1U)

#define PREFIX t->i2c->driver->driver.name

/** This macro allows us to probe dynamically, avoiding static links */
#ifdef CONFIG_MEDIA_ATTACH
#define tuner_symbol_probe(FUNCTION, ARGS...) ({ \
	int __r = -EINVAL; \
	typeof(&FUNCTION) __a = symbol_request(FUNCTION); \
	if (__a) { \
		__r = (int) __a(ARGS); \
		symbol_put(FUNCTION); \
	} else { \
		printk(KERN_ERR "TUNER: Unable to find " \
				"symbol "#FUNCTION"()\n"); \
	} \
	__r; \
})

static void tuner_detach(struct dvb_frontend *fe)
{
	if (fe->ops.tuner_ops.release) {
		fe->ops.tuner_ops.release(fe);
		symbol_put_addr(fe->ops.tuner_ops.release);
	}
	if (fe->ops.analog_ops.release) {
		fe->ops.analog_ops.release(fe);
		symbol_put_addr(fe->ops.analog_ops.release);
	}
}
#else
#define tuner_symbol_probe(FUNCTION, ARGS...) ({ \
	FUNCTION(ARGS); \
})

static void tuner_detach(struct dvb_frontend *fe)
{
	if (fe->ops.tuner_ops.release)
		fe->ops.tuner_ops.release(fe);
	if (fe->ops.analog_ops.release)
		fe->ops.analog_ops.release(fe);
}
#endif

struct tuner {
	/* device */
	struct dvb_frontend fe;
	struct i2c_client   *i2c;
	struct v4l2_subdev  sd;
	struct list_head    list;
	unsigned int        using_v4l2:1;

	/* keep track of the current settings */
	v4l2_std_id         std;
	unsigned int        tv_freq;
	unsigned int        radio_freq;
	unsigned int        audmode;

	unsigned int        mode;
	unsigned int        mode_mask; /* Combination of allowable modes */

	unsigned int        type; /* chip type id */
	unsigned int        config;
	const char          *name;
};

static inline struct tuner *to_tuner(struct v4l2_subdev *sd)
{
	return container_of(sd, struct tuner, sd);
}


/* insmod options used at init time => read/only */
static unsigned int addr;
static unsigned int no_autodetect;
static unsigned int show_i2c;

/* insmod options used at runtime => read/write */
static int tuner_debug;

#define tuner_warn(fmt, arg...) do {			\
	printk(KERN_WARNING "%s %d-%04x: " fmt, PREFIX, \
	       i2c_adapter_id(t->i2c->adapter),		\
	       t->i2c->addr, ##arg);			\
	 } while (0)

#define tuner_info(fmt, arg...) do {			\
	printk(KERN_INFO "%s %d-%04x: " fmt, PREFIX,	\
	       i2c_adapter_id(t->i2c->adapter),		\
	       t->i2c->addr, ##arg);			\
	 } while (0)

#define tuner_err(fmt, arg...) do {			\
	printk(KERN_ERR "%s %d-%04x: " fmt, PREFIX,	\
	       i2c_adapter_id(t->i2c->adapter),		\
	       t->i2c->addr, ##arg);			\
	 } while (0)

#define tuner_dbg(fmt, arg...) do {				\
	if (tuner_debug)					\
		printk(KERN_DEBUG "%s %d-%04x: " fmt, PREFIX,	\
		       i2c_adapter_id(t->i2c->adapter),		\
		       t->i2c->addr, ##arg);			\
	 } while (0)

/* ------------------------------------------------------------------------ */

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

/* ---------------------------------------------------------------------- */

static void fe_set_params(struct dvb_frontend *fe,
			  struct analog_parameters *params)
{
	struct dvb_tuner_ops *fe_tuner_ops = &fe->ops.tuner_ops;
	struct tuner *t = fe->analog_demod_priv;

	if (NULL == fe_tuner_ops->set_analog_params) {
		tuner_warn("Tuner frontend module has no way to set freq\n");
		return;
	}
	fe_tuner_ops->set_analog_params(fe, params);
}

static void fe_standby(struct dvb_frontend *fe)
{
	struct dvb_tuner_ops *fe_tuner_ops = &fe->ops.tuner_ops;

	if (fe_tuner_ops->sleep)
		fe_tuner_ops->sleep(fe);
}

static int fe_has_signal(struct dvb_frontend *fe)
{
	u16 strength = 0;

	if (fe->ops.tuner_ops.get_rf_strength)
		fe->ops.tuner_ops.get_rf_strength(fe, &strength);

	return strength;
}

static int fe_set_config(struct dvb_frontend *fe, void *priv_cfg)
{
	struct dvb_tuner_ops *fe_tuner_ops = &fe->ops.tuner_ops;
	struct tuner *t = fe->analog_demod_priv;

	if (fe_tuner_ops->set_config)
		return fe_tuner_ops->set_config(fe, priv_cfg);

	tuner_warn("Tuner frontend module has no way to set config\n");

	return 0;
}

static void tuner_status(struct dvb_frontend *fe);

static struct analog_demod_ops tuner_analog_ops = {
	.set_params     = fe_set_params,
	.standby        = fe_standby,
	.has_signal     = fe_has_signal,
	.set_config     = fe_set_config,
	.tuner_status   = tuner_status
};

/* Set tuner frequency,  freq in Units of 62.5kHz = 1/16MHz */
static void set_tv_freq(struct i2c_client *c, unsigned int freq)
{
	struct tuner *t = to_tuner(i2c_get_clientdata(c));
	struct analog_demod_ops *analog_ops = &t->fe.ops.analog_ops;

	struct analog_parameters params = {
		.mode      = t->mode,
		.audmode   = t->audmode,
		.std       = t->std
	};

	if (t->type == UNSET) {
		tuner_warn ("tuner type not set\n");
		return;
	}
	if (NULL == analog_ops->set_params) {
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
	params.frequency = freq;

	analog_ops->set_params(&t->fe, &params);
}

static void set_radio_freq(struct i2c_client *c, unsigned int freq)
{
	struct tuner *t = to_tuner(i2c_get_clientdata(c));
	struct analog_demod_ops *analog_ops = &t->fe.ops.analog_ops;

	struct analog_parameters params = {
		.mode      = t->mode,
		.audmode   = t->audmode,
		.std       = t->std
	};

	if (t->type == UNSET) {
		tuner_warn ("tuner type not set\n");
		return;
	}
	if (NULL == analog_ops->set_params) {
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
	params.frequency = freq;

	analog_ops->set_params(&t->fe, &params);
}

static void set_freq(struct i2c_client *c, unsigned long freq)
{
	struct tuner *t = to_tuner(i2c_get_clientdata(c));

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
	default:
		tuner_dbg("freq set: unknown mode: 0x%04x!\n",t->mode);
	}
}

static struct xc5000_config xc5000_cfg;

static void set_type(struct i2c_client *c, unsigned int type,
		     unsigned int new_mode_mask, unsigned int new_config,
		     int (*tuner_callback) (void *dev, int component, int cmd, int arg))
{
	struct tuner *t = to_tuner(i2c_get_clientdata(c));
	struct dvb_tuner_ops *fe_tuner_ops = &t->fe.ops.tuner_ops;
	struct analog_demod_ops *analog_ops = &t->fe.ops.analog_ops;
	unsigned char buffer[4];
	int tune_now = 1;

	if (type == UNSET || type == TUNER_ABSENT) {
		tuner_dbg ("tuner 0x%02x: Tuner type absent\n",c->addr);
		return;
	}

	t->type = type;
	/* prevent invalid config values */
	t->config = new_config < 256 ? new_config : 0;
	if (tuner_callback != NULL) {
		tuner_dbg("defining GPIO callback\n");
		t->fe.callback = tuner_callback;
	}

	if (t->mode == T_UNINITIALIZED) {
		tuner_dbg ("tuner 0x%02x: called during i2c_client register by adapter's attach_inform\n", c->addr);

		return;
	}

	/* discard private data, in case set_type() was previously called */
	tuner_detach(&t->fe);
	t->fe.analog_demod_priv = NULL;

	switch (t->type) {
	case TUNER_MT2032:
		if (!dvb_attach(microtune_attach,
			   &t->fe, t->i2c->adapter, t->i2c->addr))
			goto attach_failed;
		break;
	case TUNER_PHILIPS_TDA8290:
	{
		struct tda829x_config cfg = {
			.lna_cfg        = t->config,
		};
		if (!dvb_attach(tda829x_attach, &t->fe, t->i2c->adapter,
				t->i2c->addr, &cfg))
			goto attach_failed;
		break;
	}
	case TUNER_TEA5767:
		if (!dvb_attach(tea5767_attach, &t->fe,
				t->i2c->adapter, t->i2c->addr))
			goto attach_failed;
		t->mode_mask = T_RADIO;
		break;
	case TUNER_TEA5761:
		if (!dvb_attach(tea5761_attach, &t->fe,
				t->i2c->adapter, t->i2c->addr))
			goto attach_failed;
		t->mode_mask = T_RADIO;
		break;
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
		if (!dvb_attach(simple_tuner_attach, &t->fe,
				t->i2c->adapter, t->i2c->addr, t->type))
			goto attach_failed;
		break;
	case TUNER_PHILIPS_TD1316:
		buffer[0] = 0x0b;
		buffer[1] = 0xdc;
		buffer[2] = 0x86;
		buffer[3] = 0xa4;
		i2c_master_send(c, buffer, 4);
		if (!dvb_attach(simple_tuner_attach, &t->fe,
				t->i2c->adapter, t->i2c->addr, t->type))
			goto attach_failed;
		break;
	case TUNER_XC2028:
	{
		struct xc2028_config cfg = {
			.i2c_adap  = t->i2c->adapter,
			.i2c_addr  = t->i2c->addr,
		};
		if (!dvb_attach(xc2028_attach, &t->fe, &cfg))
			goto attach_failed;
		tune_now = 0;
		break;
	}
	case TUNER_TDA9887:
		if (!dvb_attach(tda9887_attach,
			   &t->fe, t->i2c->adapter, t->i2c->addr))
			goto attach_failed;
		break;
	case TUNER_XC5000:
	{
		xc5000_cfg.i2c_address	  = t->i2c->addr;
		/* if_khz will be set when the digital dvb_attach() occurs */
		xc5000_cfg.if_khz	  = 0;
		if (!dvb_attach(xc5000_attach,
				&t->fe, t->i2c->adapter, &xc5000_cfg))
			goto attach_failed;
		tune_now = 0;
		break;
	}
	case TUNER_NXP_TDA18271:
	{
		struct tda18271_config cfg = {
			.config = t->config,
		};

		if (!dvb_attach(tda18271_attach, &t->fe, t->i2c->addr,
				t->i2c->adapter, &cfg))
			goto attach_failed;
		tune_now = 0;
		break;
	}
	default:
		if (!dvb_attach(simple_tuner_attach, &t->fe,
				t->i2c->adapter, t->i2c->addr, t->type))
			goto attach_failed;

		break;
	}

	if ((NULL == analog_ops->set_params) &&
	    (fe_tuner_ops->set_analog_params)) {

		t->name = fe_tuner_ops->info.name;

		t->fe.analog_demod_priv = t;
		memcpy(analog_ops, &tuner_analog_ops,
		       sizeof(struct analog_demod_ops));

	} else {
		t->name = analog_ops->info.name;
	}

	tuner_dbg("type set to %s\n", t->name);

	if (t->mode_mask == T_UNINITIALIZED)
		t->mode_mask = new_mode_mask;

	/* Some tuners require more initialization setup before use,
	   such as firmware download or device calibration.
	   trying to set a frequency here will just fail
	   FIXME: better to move set_freq to the tuner code. This is needed
	   on analog tuners for PLL to properly work
	 */
	if (tune_now)
		set_freq(c, (V4L2_TUNER_RADIO == t->mode) ?
			    t->radio_freq : t->tv_freq);

	tuner_dbg("%s %s I2C addr 0x%02x with type %d used for 0x%02x\n",
		  c->adapter->name, c->driver->driver.name, c->addr << 1, type,
		  t->mode_mask);
	return;

attach_failed:
	tuner_dbg("Tuner attach for type = %d failed.\n", t->type);
	t->type = TUNER_ABSENT;
	t->mode_mask = T_UNINITIALIZED;

	return;
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
	struct tuner *t = to_tuner(i2c_get_clientdata(c));

	if ( (t->type == UNSET && ((tun_setup->addr == ADDR_UNSET) &&
		(t->mode_mask & tun_setup->mode_mask))) ||
		(tun_setup->addr == c->addr)) {
			set_type(c, tun_setup->type, tun_setup->mode_mask,
				 tun_setup->config, tun_setup->tuner_callback);
	} else
		tuner_dbg("set addr discarded for type %i, mask %x. "
			  "Asked to change tuner at addr 0x%02x, with mask %x\n",
			  t->type, t->mode_mask,
			  tun_setup->addr, tun_setup->mode_mask);
}

static inline int check_mode(struct tuner *t, char *cmd)
{
	if ((1 << t->mode & t->mode_mask) == 0) {
		return -EINVAL;
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

static void tuner_status(struct dvb_frontend *fe)
{
	struct tuner *t = fe->analog_demod_priv;
	unsigned long freq, freq_fraction;
	struct dvb_tuner_ops *fe_tuner_ops = &fe->ops.tuner_ops;
	struct analog_demod_ops *analog_ops = &fe->ops.analog_ops;
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
	if (analog_ops->has_signal)
		tuner_info("Signal strength: %d\n",
			   analog_ops->has_signal(fe));
	if (analog_ops->is_stereo)
		tuner_info("Stereo:          %s\n",
			   analog_ops->is_stereo(fe) ? "yes" : "no");
}

/* ---------------------------------------------------------------------- */

/*
 * Switch tuner to other mode. If tuner support both tv and radio,
 * set another frequency to some value (This is needed for some pal
 * tuners to avoid locking). Otherwise, just put second tuner in
 * standby mode.
 */

static inline int set_mode(struct i2c_client *client, struct tuner *t, int mode, char *cmd)
{
	struct analog_demod_ops *analog_ops = &t->fe.ops.analog_ops;

	if (mode == t->mode)
		return 0;

	t->mode = mode;

	if (check_mode(t, cmd) == -EINVAL) {
		tuner_dbg("Tuner doesn't support this mode. "
			  "Putting tuner to sleep\n");
		t->mode = T_STANDBY;
		if (analog_ops->standby)
			analog_ops->standby(&t->fe);
		return -EINVAL;
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

static int tuner_s_type_addr(struct v4l2_subdev *sd, struct tuner_setup *type)
{
	struct tuner *t = to_tuner(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	tuner_dbg("Calling set_type_addr for type=%d, addr=0x%02x, mode=0x%02x, config=0x%02x\n",
			type->type,
			type->addr,
			type->mode_mask,
			type->config);

	set_addr(client, type);
	return 0;
}

static int tuner_s_radio(struct v4l2_subdev *sd)
{
	struct tuner *t = to_tuner(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (set_mode(client, t, V4L2_TUNER_RADIO, "s_radio") == -EINVAL)
		return 0;
	if (t->radio_freq)
		set_freq(client, t->radio_freq);
	return 0;
}

static int tuner_s_power(struct v4l2_subdev *sd, int on)
{
	struct tuner *t = to_tuner(sd);
	struct analog_demod_ops *analog_ops = &t->fe.ops.analog_ops;

	if (on)
		return 0;

	tuner_dbg("Putting tuner to sleep\n");

	if (check_mode(t, "s_power") == -EINVAL)
		return 0;
	t->mode = T_STANDBY;
	if (analog_ops->standby)
		analog_ops->standby(&t->fe);
	return 0;
}

static int tuner_s_config(struct v4l2_subdev *sd, const struct v4l2_priv_tun_config *cfg)
{
	struct tuner *t = to_tuner(sd);
	struct analog_demod_ops *analog_ops = &t->fe.ops.analog_ops;

	if (t->type != cfg->tuner)
		return 0;

	if (analog_ops->set_config) {
		analog_ops->set_config(&t->fe, cfg->priv);
		return 0;
	}

	tuner_dbg("Tuner frontend module has no way to set config\n");
	return 0;
}

/* --- v4l ioctls --- */
/* take care: bttv does userspace copying, we'll get a
   kernel pointer here... */
static int tuner_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct tuner *t = to_tuner(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (set_mode(client, t, V4L2_TUNER_ANALOG_TV, "s_std") == -EINVAL)
		return 0;

	switch_v4l2();

	t->std = std;
	tuner_fixup_std(t);
	if (t->tv_freq)
		set_freq(client, t->tv_freq);
	return 0;
}

static int tuner_s_frequency(struct v4l2_subdev *sd, struct v4l2_frequency *f)
{
	struct tuner *t = to_tuner(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (set_mode(client, t, f->type, "s_frequency") == -EINVAL)
		return 0;
	switch_v4l2();
	set_freq(client, f->frequency);

	return 0;
}

static int tuner_g_frequency(struct v4l2_subdev *sd, struct v4l2_frequency *f)
{
	struct tuner *t = to_tuner(sd);
	struct dvb_tuner_ops *fe_tuner_ops = &t->fe.ops.tuner_ops;

	if (check_mode(t, "g_frequency") == -EINVAL)
		return 0;
	switch_v4l2();
	f->type = t->mode;
	if (fe_tuner_ops->get_frequency) {
		u32 abs_freq;

		fe_tuner_ops->get_frequency(&t->fe, &abs_freq);
		f->frequency = (V4L2_TUNER_RADIO == t->mode) ?
			DIV_ROUND_CLOSEST(abs_freq * 2, 125) :
			DIV_ROUND_CLOSEST(abs_freq, 62500);
		return 0;
	}
	f->frequency = (V4L2_TUNER_RADIO == t->mode) ?
		t->radio_freq : t->tv_freq;
	return 0;
}

static int tuner_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	struct tuner *t = to_tuner(sd);
	struct analog_demod_ops *analog_ops = &t->fe.ops.analog_ops;
	struct dvb_tuner_ops *fe_tuner_ops = &t->fe.ops.tuner_ops;

	if (check_mode(t, "g_tuner") == -EINVAL)
		return 0;
	switch_v4l2();

	vt->type = t->mode;
	if (analog_ops->get_afc)
		vt->afc = analog_ops->get_afc(&t->fe);
	if (t->mode == V4L2_TUNER_ANALOG_TV)
		vt->capability |= V4L2_TUNER_CAP_NORM;
	if (t->mode != V4L2_TUNER_RADIO) {
		vt->rangelow = tv_range[0] * 16;
		vt->rangehigh = tv_range[1] * 16;
		return 0;
	}

	/* radio mode */
	vt->rxsubchans =
		V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
	if (fe_tuner_ops->get_status) {
		u32 tuner_status;

		fe_tuner_ops->get_status(&t->fe, &tuner_status);
		vt->rxsubchans =
			(tuner_status & TUNER_STATUS_STEREO) ?
			V4L2_TUNER_SUB_STEREO :
			V4L2_TUNER_SUB_MONO;
	} else {
		if (analog_ops->is_stereo) {
			vt->rxsubchans =
				analog_ops->is_stereo(&t->fe) ?
				V4L2_TUNER_SUB_STEREO :
				V4L2_TUNER_SUB_MONO;
		}
	}
	if (analog_ops->has_signal)
		vt->signal = analog_ops->has_signal(&t->fe);
	vt->capability |=
		V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO;
	vt->audmode = t->audmode;
	vt->rangelow = radio_range[0] * 16000;
	vt->rangehigh = radio_range[1] * 16000;
	return 0;
}

static int tuner_s_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	struct tuner *t = to_tuner(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (check_mode(t, "s_tuner") == -EINVAL)
		return 0;

	switch_v4l2();

	/* do nothing unless we're a radio tuner */
	if (t->mode != V4L2_TUNER_RADIO)
		return 0;
	t->audmode = vt->audmode;
	set_radio_freq(client, t->radio_freq);
	return 0;
}

static int tuner_log_status(struct v4l2_subdev *sd)
{
	struct tuner *t = to_tuner(sd);
	struct analog_demod_ops *analog_ops = &t->fe.ops.analog_ops;

	if (analog_ops->tuner_status)
		analog_ops->tuner_status(&t->fe);
	return 0;
}

static int tuner_suspend(struct i2c_client *c, pm_message_t state)
{
	struct tuner *t = to_tuner(i2c_get_clientdata(c));

	tuner_dbg("suspend\n");
	/* FIXME: power down ??? */
	return 0;
}

static int tuner_resume(struct i2c_client *c)
{
	struct tuner *t = to_tuner(i2c_get_clientdata(c));

	tuner_dbg("resume\n");
	if (V4L2_TUNER_RADIO == t->mode) {
		if (t->radio_freq)
			set_freq(c, t->radio_freq);
	} else {
		if (t->tv_freq)
			set_freq(c, t->tv_freq);
	}
	return 0;
}

static int tuner_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	/* TUNER_SET_CONFIG is still called by tuner-simple.c, so we have
	   to handle it here.
	   There must be a better way of doing this... */
	switch (cmd) {
	case TUNER_SET_CONFIG:
		return tuner_s_config(sd, arg);
	}
	return -ENOIOCTLCMD;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops tuner_core_ops = {
	.log_status = tuner_log_status,
	.s_std = tuner_s_std,
	.s_power = tuner_s_power,
};

static const struct v4l2_subdev_tuner_ops tuner_tuner_ops = {
	.s_radio = tuner_s_radio,
	.g_tuner = tuner_g_tuner,
	.s_tuner = tuner_s_tuner,
	.s_frequency = tuner_s_frequency,
	.g_frequency = tuner_g_frequency,
	.s_type_addr = tuner_s_type_addr,
	.s_config = tuner_s_config,
};

static const struct v4l2_subdev_ops tuner_ops = {
	.core = &tuner_core_ops,
	.tuner = &tuner_tuner_ops,
};

/* ---------------------------------------------------------------------- */

static LIST_HEAD(tuner_list);

/* Search for existing radio and/or TV tuners on the given I2C adapter.
   Note that when this function is called from tuner_probe you can be
   certain no other devices will be added/deleted at the same time, I2C
   core protects against that. */
static void tuner_lookup(struct i2c_adapter *adap,
		struct tuner **radio, struct tuner **tv)
{
	struct tuner *pos;

	*radio = NULL;
	*tv = NULL;

	list_for_each_entry(pos, &tuner_list, list) {
		int mode_mask;

		if (pos->i2c->adapter != adap ||
		    strcmp(pos->i2c->driver->driver.name, "tuner"))
			continue;

		mode_mask = pos->mode_mask & ~T_STANDBY;
		if (*radio == NULL && mode_mask == T_RADIO)
			*radio = pos;
		/* Note: currently TDA9887 is the only demod-only
		   device. If other devices appear then we need to
		   make this test more general. */
		else if (*tv == NULL && pos->type != TUNER_TDA9887 &&
			 (pos->mode_mask & (T_ANALOG_TV | T_DIGITAL_TV)))
			*tv = pos;
	}
}

/* During client attach, set_type is called by adapter's attach_inform callback.
   set_type must then be completed by tuner_probe.
 */
static int tuner_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct tuner *t;
	struct tuner *radio;
	struct tuner *tv;

	t = kzalloc(sizeof(struct tuner), GFP_KERNEL);
	if (NULL == t)
		return -ENOMEM;
	v4l2_i2c_subdev_init(&t->sd, client, &tuner_ops);
	t->i2c = client;
	t->name = "(tuner unset)";
	t->type = UNSET;
	t->audmode = V4L2_TUNER_MODE_STEREO;
	t->mode_mask = T_UNINITIALIZED;

	if (show_i2c) {
		unsigned char buffer[16];
		int i, rc;

		memset(buffer, 0, sizeof(buffer));
		rc = i2c_master_recv(client, buffer, sizeof(buffer));
		tuner_info("I2C RECV = ");
		for (i = 0; i < rc; i++)
			printk(KERN_CONT "%02x ", buffer[i]);
		printk("\n");
	}
	/* HACK: This test was added to avoid tuner to probe tda9840 and
	   tea6415c on the MXB card */
	if (client->adapter->id == I2C_HW_SAA7146 && client->addr < 0x4a) {
		kfree(t);
		return -ENODEV;
	}

	/* autodetection code based on the i2c addr */
	if (!no_autodetect) {
		switch (client->addr) {
		case 0x10:
			if (tuner_symbol_probe(tea5761_autodetection,
					       t->i2c->adapter,
					       t->i2c->addr) >= 0) {
				t->type = TUNER_TEA5761;
				t->mode_mask = T_RADIO;
				t->mode = T_STANDBY;
				/* Sets freq to FM range */
				t->radio_freq = 87.5 * 16000;
				tuner_lookup(t->i2c->adapter, &radio, &tv);
				if (tv)
					tv->mode_mask &= ~T_RADIO;

				goto register_client;
			}
			kfree(t);
			return -ENODEV;
		case 0x42:
		case 0x43:
		case 0x4a:
		case 0x4b:
			/* If chip is not tda8290, don't register.
			   since it can be tda9887*/
			if (tuner_symbol_probe(tda829x_probe, t->i2c->adapter,
					       t->i2c->addr) >= 0) {
				tuner_dbg("tda829x detected\n");
			} else {
				/* Default is being tda9887 */
				t->type = TUNER_TDA9887;
				t->mode_mask = T_RADIO | T_ANALOG_TV |
					       T_DIGITAL_TV;
				t->mode = T_STANDBY;
				goto register_client;
			}
			break;
		case 0x60:
			if (tuner_symbol_probe(tea5767_autodetection,
					       t->i2c->adapter, t->i2c->addr)
					>= 0) {
				t->type = TUNER_TEA5767;
				t->mode_mask = T_RADIO;
				t->mode = T_STANDBY;
				/* Sets freq to FM range */
				t->radio_freq = 87.5 * 16000;
				tuner_lookup(t->i2c->adapter, &radio, &tv);
				if (tv)
					tv->mode_mask &= ~T_RADIO;

				goto register_client;
			}
			break;
		}
	}

	/* Initializes only the first TV tuner on this adapter. Why only the
	   first? Because there are some devices (notably the ones with TI
	   tuners) that have more than one i2c address for the *same* device.
	   Experience shows that, except for just one case, the first
	   address is the right one. The exception is a Russian tuner
	   (ACORP_Y878F). So, the desired behavior is just to enable the
	   first found TV tuner. */
	tuner_lookup(t->i2c->adapter, &radio, &tv);
	if (tv == NULL) {
		t->mode_mask = T_ANALOG_TV | T_DIGITAL_TV;
		if (radio == NULL)
			t->mode_mask |= T_RADIO;
		tuner_dbg("Setting mode_mask to 0x%02x\n", t->mode_mask);
		t->tv_freq = 400 * 16; /* Sets freq to VHF High */
		t->radio_freq = 87.5 * 16000; /* Sets freq to FM range */
	}

	/* Should be just before return */
register_client:
	tuner_info("chip found @ 0x%x (%s)\n", client->addr << 1,
		       client->adapter->name);

	/* Sets a default mode */
	if (t->mode_mask & T_ANALOG_TV) {
		t->mode = V4L2_TUNER_ANALOG_TV;
	} else  if (t->mode_mask & T_RADIO) {
		t->mode = V4L2_TUNER_RADIO;
	} else {
		t->mode = V4L2_TUNER_DIGITAL_TV;
	}
	set_type(client, t->type, t->mode_mask, t->config, t->fe.callback);
	list_add_tail(&t->list, &tuner_list);
	return 0;
}

static int tuner_remove(struct i2c_client *client)
{
	struct tuner *t = to_tuner(i2c_get_clientdata(client));

	v4l2_device_unregister_subdev(&t->sd);
	tuner_detach(&t->fe);
	t->fe.analog_demod_priv = NULL;

	list_del(&t->list);
	kfree(t);
	return 0;
}

/* ----------------------------------------------------------------------- */

/* This driver supports many devices and the idea is to let the driver
   detect which device is present. So rather than listing all supported
   devices here, we pretend to support a single, fake device type. */
static const struct i2c_device_id tuner_id[] = {
	{ "tuner", }, /* autodetect */
	{ }
};
MODULE_DEVICE_TABLE(i2c, tuner_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "tuner",
	.probe = tuner_probe,
	.remove = tuner_remove,
	.command = tuner_command,
	.suspend = tuner_suspend,
	.resume = tuner_resume,
	.id_table = tuner_id,
};

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
