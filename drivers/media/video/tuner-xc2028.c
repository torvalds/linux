/* tuner-xc2028
 *
 * Copyright (c) 2007 Mauro Carvalho Chehab (mchehab@infradead.org)
 * Copyright (c) 2007 Michel Ludwig (michel.ludwig@gmail.com)
 *       - frontend interface
 * This code is placed under the terms of the GNU General Public License v2
 */

#include <linux/i2c.h>
#include <asm/div64.h>
#include <linux/firmware.h>
#include <linux/videodev.h>
#include <linux/delay.h>
#include <media/tuner.h>
#include "tuner-driver.h"
#include "tuner-xc2028.h"

#include <linux/dvb/frontend.h>
#include "dvb_frontend.h"

/* digital TV standards */
#define V4L2_STD_DTV_6MHZ       ((v4l2_std_id)0x04000000)
#define V4L2_STD_DTV_7MHZ       ((v4l2_std_id)0x08000000)
#define V4L2_STD_DTV_8MHZ       ((v4l2_std_id)0x10000000)

/* Firmwares used on tm5600/tm6000 + xc2028/xc3028 */

/* Generic firmwares */
static const char *firmware_INIT0      = "tm_xc3028_MTS_init0.fw";
static const char *firmware_8MHZ_INIT0 = "tm_xc3028_8M_MTS_init0.fw";
static const char *firmware_INIT1      = "tm_xc3028_68M_MTS_init1.fw";

/* Standard-specific firmwares */
static const char *firmware_6M         = "tm_xc3028_DTV_6M.fw";
static const char *firmware_7M         = "tm_xc3028_7M.fw";
static const char *firmware_8M         = "tm_xc3028_8M.fw";
static const char *firmware_B          = "tm_xc3028_B_PAL.fw";
static const char *firmware_DK         = "tm_xc3028_DK_PAL_MTS.fw";
static const char *firmware_MN         = "tm_xc3028_MN_BTSC.fw";

struct xc2028_data {
	v4l2_std_id		firm_type;	   /* video stds supported
							by current firmware */
	fe_bandwidth_t		bandwidth;	   /* Firmware bandwidth:
							      6M, 7M or 8M */
	int			need_load_generic; /* The generic firmware
							      were loaded? */
	enum tuner_mode	mode;
	struct i2c_client	*i2c_client;
};

#define i2c_send(rc,c,buf,size)						\
if (size != (rc = i2c_master_send(c, buf, size)))			\
	tuner_warn("i2c output error: rc = %d (should be %d)\n",	\
	rc, (int)size);

#define i2c_rcv(rc,c,buf,size)						\
if (size != (rc = i2c_master_recv(c, buf, size)))			\
	tuner_warn("i2c input error: rc = %d (should be %d)\n",		\
	rc, (int)size);

#define send_seq(c, data...)						\
{	int rc;								\
	const static u8 _val[] = data;					\
	if (sizeof(_val) !=						\
				(rc = i2c_master_send 			\
				(c, _val, sizeof(_val)))) {		\
		printk(KERN_ERR "Error on line %d: %d\n",__LINE__,rc);	\
		return;							\
	}								\
	msleep (10);							\
}

static int xc2028_get_reg(struct i2c_client *c, u16 reg)
{
	int rc;
	unsigned char buf[1];
	struct tuner *t = i2c_get_clientdata(c);

	buf[0]= reg;

	i2c_send(rc, c, buf, sizeof(buf));
	if (rc<0)
		return rc;

	i2c_rcv(rc, c, buf, 2);
	if (rc<0)
		return rc;

	return (buf[1])|(buf[0]<<8);
}

static int load_firmware (struct i2c_client *c, const char *name)
{
	const struct firmware *fw=NULL;
	struct tuner          *t = i2c_get_clientdata(c);
	unsigned char         *p, *endp;
	int                   len=0, rc=0;
	static const char     firmware_ver[] = "tm6000/xcv v1";

	tuner_info("xc2028: Loading firmware %s\n", name);
	rc = request_firmware(&fw, name, &c->dev);
	if (rc < 0) {
		if (rc==-ENOENT)
			tuner_info("Error: firmware %s not found.\n", name);
		else
			tuner_info("Error %d while requesting firmware %s \n", rc, name);

		return rc;
	}
	p=fw->data;
	endp=p+fw->size;

	if(fw->size==0) {
		tuner_info("Error: firmware size is zero!\n");
		rc=-EINVAL;
		goto err;
	}
	if (fw->size<sizeof(firmware_ver)-1) {
		/* Firmware is incorrect */
		tuner_info("Error: firmware size is less than header (%d<%d)!\n",
			   (int)fw->size,(int)sizeof(firmware_ver)-1);
		rc=-EINVAL;
		goto err;
	}

	if (memcmp(p,firmware_ver,sizeof(firmware_ver)-1)) {
		/* Firmware is incorrect */
		tuner_info("Error: firmware is not for tm5600/6000 + Xcv2028/3028!\n");
		rc=-EINVAL;
		goto err;
	}
	p+=sizeof(firmware_ver)-1;

	while(p<endp) {
		if ((*p) & 0x80) {
			/* Special callback command received */
			rc = t->tuner_callback(c->adapter->algo_data,
					     XC2028_TUNER_RESET, (*p)&0x7f);
			if (rc<0) {
				tuner_info("Error at RESET code %d\n",
								(*p)&0x7f);
				goto err;
			}
			p++;
			continue;
		}
		len=*p;
		p++;
		if (p+len+1>endp) {
			/* Firmware is incorrect */
			tuner_info("Error: firmware is truncated!\n");
			rc=-EINVAL;
			goto err;
		}
		if (len<=0) {
			tuner_info("Error: firmware file is corrupted!\n");
			rc=-EINVAL;
			goto err;
		}

		i2c_send(rc, c, p, len);
		if (rc<0)
			goto err;
		p+=len;

		if (*p)
			msleep(*p);
		p++;
	}


err:
	release_firmware(fw);

	return rc;
}

static int check_firmware(struct i2c_client *c, enum tuner_mode new_mode,
						fe_bandwidth_t bandwidth)
{
	int			rc, version;
	struct tuner		*t = i2c_get_clientdata(c);
	struct xc2028_data	*xc2028 = t->priv;
	const char		*name;
	int change_digital_bandwidth;

	if (!t->tuner_callback) {
		printk(KERN_ERR "xc2028: need tuner_callback to load firmware\n");
		return -EINVAL;
	}

	printk(KERN_INFO "xc2028: I am in mode %u and I should switch to mode %i\n",
							    xc2028->mode, new_mode);

	/* first of all, determine whether we have switched the mode */
	if(new_mode != xc2028->mode) {
		xc2028->mode = new_mode;
		xc2028->need_load_generic = 1;
	}

	change_digital_bandwidth = (xc2028->mode == T_DIGITAL_TV
				 && bandwidth != xc2028->bandwidth) ? 1 : 0;
	tuner_info("xc2028: old bandwidth %u, new bandwidth %u\n", xc2028->bandwidth,
								   bandwidth);

	if (xc2028->need_load_generic) {
		if (xc2028->bandwidth==8)
			name = firmware_8MHZ_INIT0;
		else
			name = firmware_INIT0;

		/* Reset is needed before loading firmware */
		rc = t->tuner_callback(c->adapter->algo_data,
				     XC2028_TUNER_RESET, 0);
		if (rc<0)
			return rc;

		rc = load_firmware(c,name);
		if (rc<0)
			return rc;

		xc2028->need_load_generic=0;
		xc2028->firm_type=0;
		if(xc2028->mode == T_DIGITAL_TV) {
			change_digital_bandwidth=1;
		}
	}

	tuner_info("xc2028: I should change bandwidth %u\n",
						   change_digital_bandwidth);

	if (change_digital_bandwidth) {
		switch(bandwidth) {
			case BANDWIDTH_8_MHZ:
				t->std = V4L2_STD_DTV_8MHZ;
			break;

			case BANDWIDTH_7_MHZ:
				t->std = V4L2_STD_DTV_7MHZ;
			break;

			case BANDWIDTH_6_MHZ:
				t->std = V4L2_STD_DTV_6MHZ;
			break;

			default:
				tuner_info("error: bandwidth not supported.\n");
		};
		xc2028->bandwidth = bandwidth;
	}

	if (xc2028->firm_type & t->std) {
		tuner_info("xc3028: no need to load a std-specific firmware.\n");
		return 0;
	}

	rc = load_firmware(c,firmware_INIT1);

	if (t->std & V4L2_STD_MN)
		name=firmware_MN;
	else if (t->std & V4L2_STD_DTV_6MHZ)
		name=firmware_6M;
	else if (t->std & V4L2_STD_DTV_7MHZ)
		name=firmware_7M;
	else if (t->std & V4L2_STD_DTV_8MHZ)
		name=firmware_8M;
	else if (t->std & V4L2_STD_PAL_B)
		name=firmware_B;
	else
		name=firmware_DK;

	tuner_info("xc2028: loading firmware named %s.\n", name);
	rc = load_firmware(c, name);
	if (rc<0)
		return rc;

	version = xc2028_get_reg(c, 0x4);
	tuner_info("Firmware version is %d.%d\n",
					(version>>4)&0x0f,(version)&0x0f);

	xc2028->firm_type=t->std;

	return 0;
}

static int xc2028_signal(struct i2c_client *c)
{
	int lock, signal;

	printk(KERN_INFO "xc2028: %s called\n", __FUNCTION__);

	lock = xc2028_get_reg(c, 0x2);
	if (lock<=0)
		return lock;

	/* Frequency is locked. Return signal quality */

	signal = xc2028_get_reg(c, 0x40);

	if(signal<=0)
		return lock;

	return signal;
}

#define DIV 15625

static void generic_set_tv_freq(struct i2c_client *c, u32 freq /* in Hz */,
				enum tuner_mode new_mode, fe_bandwidth_t bandwidth)
{
	int           rc;
	unsigned char buf[5];
	struct tuner  *t  = i2c_get_clientdata(c);
	u32 div, offset = 0;

	printk("xc3028: should set frequency %d kHz)\n", freq / 1000);

	if (check_firmware(c, new_mode, bandwidth)<0)
		return;

	if(new_mode == T_DIGITAL_TV) {
		switch(bandwidth) {
			case BANDWIDTH_8_MHZ:
				offset = 2750000;
			break;

			case BANDWIDTH_7_MHZ:
				offset = 2750000;
			break;

			case BANDWIDTH_6_MHZ:
			default:
				printk(KERN_ERR "xc2028: bandwidth not implemented!\n");
		}
	}

	div = (freq - offset + DIV/2)/DIV;

	/* Reset GPIO 1 */
	if (t->tuner_callback) {
		rc = t->tuner_callback( c->adapter->algo_data,
					XC2028_TUNER_RESET, 0);
		if (rc<0)
			return;
	}
	msleep(10);

	char *name;

	rc = load_firmware(c,firmware_INIT1);

	if (t->std & V4L2_STD_MN)
		name=firmware_MN;
	else
		name=firmware_DK;

	rc = load_firmware(c,name);
	/* CMD= Set frequency */
	send_seq(c, {0x00, 0x02, 0x00, 0x00});
	if (t->tuner_callback) {
		rc = t->tuner_callback( c->adapter->algo_data,
					XC2028_RESET_CLK, 1);
		if (rc<0)
			return;
	}

	msleep(10);
//	send_seq(c, {0x00, 0x00, 0x10, 0xd0, 0x00});
//	msleep(100);

	buf[0]= 0xff & (div>>24);
	buf[1]= 0xff & (div>>16);
	buf[2]= 0xff & (div>>8);
	buf[3]= 0xff & (div);
	buf[4]= 0;

	i2c_send(rc, c, buf, sizeof(buf));
	if (rc<0)
		return;
	msleep(100);

	printk("divider= %02x %02x %02x %02x (freq=%d.%02d)\n",
		 buf[1],buf[2],buf[3],buf[4],
		 freq / 16, freq % 16 * 100 / 16);
//	printk("signal=%d\n",xc2028_signal(c));
}


static void set_tv_freq(struct i2c_client *c, unsigned int freq)
{
	printk(KERN_INFO "xc2028: %s called\n", __FUNCTION__);

	generic_set_tv_freq(c, freq * 62500l, T_ANALOG_TV,
					      BANDWIDTH_8_MHZ /* unimportant */);
}

static void xc2028_release(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);

	kfree(t->priv);
	t->priv = NULL;
}

static struct tuner_operations tea5767_tuner_ops = {
	.set_tv_freq    = set_tv_freq,
	.has_signal     = xc2028_signal,
	.release        = xc2028_release,
//	.is_stereo      = xc2028_stereo,
};


static int init=0;

int xc2028_tuner_init(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);
	int version = xc2028_get_reg(c, 0x4);
	int prd_id = xc2028_get_reg(c, 0x8);
	struct xc2028_data *xc2028;

	tuner_info("Xcv2028/3028 init called!\n");

	if (init) {
		printk (KERN_ERR "Module already initialized!\n");
		return 0;
	}
	init++;

	xc2028 = kzalloc(sizeof(*xc2028), GFP_KERNEL);
	if (!xc2028)
		return -ENOMEM;
	t->priv = xc2028;

	xc2028->bandwidth=BANDWIDTH_6_MHZ;
	xc2028->need_load_generic=1;
	xc2028->mode = T_UNINITIALIZED;

	/* FIXME: Check where t->priv will be freed */

	if (version<0)
		version=0;

	if (prd_id<0)
		prd_id=0;

	strlcpy(c->name, "xc2028", sizeof(c->name));
	tuner_info("type set to %d (%s, hw ver=%d.%d, fw ver=%d.%d, id=0x%04x)\n",
		   t->type, c->name,
		   (version>>12)&0x0f,(version>>8)&0x0f,
		   (version>>4)&0x0f,(version)&0x0f, prd_id);

	memcpy(&t->ops, &tea5767_tuner_ops, sizeof(struct tuner_operations));

	return 0;
}

static int xc3028_set_params(struct dvb_frontend *fe,
			     struct dvb_frontend_parameters *p)
{
	struct i2c_client *c = fe->tuner_priv;

	printk(KERN_INFO "xc2028: %s called\n", __FUNCTION__);

	generic_set_tv_freq(c, p->frequency, T_DIGITAL_TV,
					     p->u.ofdm.bandwidth);

	return 0;
}

static int xc3028_dvb_release(struct dvb_frontend *fe)
{
	printk(KERN_INFO "xc2028: %s called\n", __FUNCTION__);

	fe->tuner_priv = NULL;

	return 0;
}

static int xc3028_dvb_init(struct dvb_frontend *fe)
{
	printk(KERN_INFO "xc2028: %s called\n", __FUNCTION__);

	return 0;
}

static const struct dvb_tuner_ops xc3028_dvb_tuner_ops = {
	.info = {
			.name           = "Xceive XC3028",
			.frequency_min  =  42000000,
			.frequency_max  = 864000000,
			.frequency_step =     50000,
		},

	.release = xc3028_dvb_release,
	.init = xc3028_dvb_init,

// 	int (*sleep)(struct dvb_frontend *fe);

	/** This is for simple PLLs - set all parameters in one go. */
	.set_params = xc3028_set_params,

	/** This is support for demods like the mt352 - fills out the supplied buffer with what to write. */
// 	int (*calc_regs)(struct dvb_frontend *fe, struct dvb_frontend_parameters *p, u8 *buf, int buf_len);

// 	int (*get_frequency)(struct dvb_frontend *fe, u32 *frequency);
// 	int (*get_bandwidth)(struct dvb_frontend *fe, u32 *bandwidth);

// 	int (*get_status)(struct dvb_frontend *fe, u32 *status);
};

int xc2028_attach(struct i2c_client *c, struct dvb_frontend *fe)
{
	fe->tuner_priv = c;

	memcpy(&fe->ops.tuner_ops, &xc3028_dvb_tuner_ops, sizeof(fe->ops.tuner_ops));

	return 0;
}

EXPORT_SYMBOL(xc2028_attach);

