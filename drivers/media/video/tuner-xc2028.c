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
#include <linux/mutex.h>
#include "tuner-i2c.h"
#include "tuner-xc2028.h"

#include <linux/dvb/frontend.h>
#include "dvb_frontend.h"

#define PREFIX "xc2028 "

static LIST_HEAD(xc2028_list);

/* Firmwares used on tm5600/tm6000 + xc2028/xc3028 */

/* Generic firmwares */
static const char *firmware_INIT0      = "tm_xc3028_MTS_init0.fw";
static const char *firmware_8MHZ_INIT0 = "tm_xc3028_8M_MTS_init0.fw";
static const char *firmware_INIT1      = "tm_xc3028_68M_MTS_init1.fw";

/* Standard-specific firmwares */
static const char *firmware_6M         = "tm_xc3028_DTV_6M.fw";
static const char *firmware_7M         = "tm_xc3028_DTV_7M.fw";
static const char *firmware_8M         = "tm_xc3028_DTV_8M.fw";
static const char *firmware_B          = "tm_xc3028_B_PAL.fw";
static const char *firmware_DK         = "tm_xc3028_DK_PAL_MTS.fw";
static const char *firmware_MN         = "tm_xc3028_MN_BTSC.fw";

struct xc2028_data {
	struct list_head        xc2028_list;
	struct tuner_i2c_props  i2c_props;
	int                     (*tuner_callback) (void *dev,
						   int command, int arg);
	struct device           *dev;
	void			*video_dev;
	int			count;
	u32			frequency;

	v4l2_std_id		firm_type;	   /* video stds supported
							by current firmware */
	fe_bandwidth_t		bandwidth;	   /* Firmware bandwidth:
							      6M, 7M or 8M */
	int			need_load_generic; /* The generic firmware
							      were loaded? */
	enum tuner_mode	mode;
	struct i2c_client	*i2c_client;

	struct mutex lock;
};

#define i2c_send(rc, priv, buf, size)					\
if (size != (rc = tuner_i2c_xfer_send(&priv->i2c_props, buf, size)))	\
	tuner_info("i2c output error: rc = %d (should be %d)\n",	\
	rc, (int)size);

#define i2c_rcv(rc, priv, buf, size)					\
if (size != (rc = tuner_i2c_xfer_recv(&priv->i2c_props, buf, size)))	\
	tuner_info("i2c input error: rc = %d (should be %d)\n",		\
	rc, (int)size);

#define send_seq(priv, data...)						\
{	int rc;								\
	static u8 _val[] = data;					\
	if (sizeof(_val) !=						\
			(rc = tuner_i2c_xfer_send (&priv->i2c_props,	\
						_val, sizeof(_val)))) {	\
		tuner_info("Error on line %d: %d\n",__LINE__,rc);	\
		return -EINVAL;							\
	}								\
	msleep (10);							\
}

static int xc2028_get_reg(struct xc2028_data *priv, u16 reg)
{
	int rc;
	unsigned char buf[1];

	tuner_info("%s called\n", __FUNCTION__);

	buf[0]= reg;

	i2c_send(rc, priv, buf, sizeof(buf));
	if (rc<0)
		return rc;

	i2c_rcv(rc, priv, buf, 2);
	if (rc<0)
		return rc;

	return (buf[1])|(buf[0]<<8);
}

static int load_firmware (struct dvb_frontend *fe, const char *name)
{
	struct xc2028_data      *priv = fe->tuner_priv;
	const struct firmware *fw=NULL;
	unsigned char         *p, *endp;
	int                   len=0, rc=0;
	static const char     firmware_ver[] = "tm6000/xcv v1";

	tuner_info("%s called\n", __FUNCTION__);

	tuner_info("Loading firmware %s\n", name);
	rc = request_firmware(&fw, name, priv->dev);
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
			rc = priv->tuner_callback(priv->video_dev,
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

		i2c_send(rc, priv, p, len);
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

static int check_firmware(struct dvb_frontend *fe, enum tuner_mode new_mode,
						v4l2_std_id std,
						fe_bandwidth_t bandwidth)
{
	struct xc2028_data      *priv = fe->tuner_priv;
	int			rc, version;
	const char		*name;
	int change_digital_bandwidth;

	tuner_info("%s called\n", __FUNCTION__);

	tuner_info( "I am in mode %u and I should switch to mode %i\n",
						    priv->mode, new_mode);

	/* first of all, determine whether we have switched the mode */
	if(new_mode != priv->mode) {
		priv->mode = new_mode;
		priv->need_load_generic = 1;
	}

	change_digital_bandwidth = (priv->mode == T_DIGITAL_TV
				 && bandwidth != priv->bandwidth) ? 1 : 0;
	tuner_info("old bandwidth %u, new bandwidth %u\n", priv->bandwidth,
								   bandwidth);

	if (priv->need_load_generic) {
		if (priv->bandwidth==8)
			name = firmware_8MHZ_INIT0;
		else
			name = firmware_INIT0;

		/* Reset is needed before loading firmware */
		rc = priv->tuner_callback(priv->video_dev,
					  XC2028_TUNER_RESET, 0);
		if (rc<0)
			return rc;

		rc = load_firmware(fe,name);
		if (rc<0)
			return rc;

		priv->need_load_generic=0;
		priv->firm_type=0;
		if(priv->mode == T_DIGITAL_TV) {
			change_digital_bandwidth=1;
		}
	}

	tuner_info("I should change bandwidth %u\n",
						   change_digital_bandwidth);

	/* FIXME: t->std makes no sense here */
	if (change_digital_bandwidth) {
		switch(bandwidth) {
			case BANDWIDTH_8_MHZ:
				std = V4L2_STD_DTV_8MHZ;
			break;

			case BANDWIDTH_7_MHZ:
				std = V4L2_STD_DTV_7MHZ;
			break;

			case BANDWIDTH_6_MHZ:
				std = V4L2_STD_DTV_6MHZ;
			break;

			default:
				tuner_info("error: bandwidth not supported.\n");
		};
		priv->bandwidth = bandwidth;
	}

	if (priv->firm_type & std) {
		tuner_info("xc3028: no need to load a std-specific firmware.\n");
		return 0;
	}

	rc = load_firmware(fe,firmware_INIT1);

	if (std & V4L2_STD_MN)
		name=firmware_MN;
	else if (std & V4L2_STD_DTV_6MHZ)
		name=firmware_6M;
	else if (std & V4L2_STD_DTV_7MHZ)
		name=firmware_7M;
	else if (std & V4L2_STD_DTV_8MHZ)
		name=firmware_8M;
	else if (std & V4L2_STD_PAL_B)
		name=firmware_B;
	else
		name=firmware_DK;

	tuner_info("loading firmware named %s.\n", name);
	rc = load_firmware(fe, name);
	if (rc<0)
		return rc;

	version = xc2028_get_reg(priv, 0x4);
	tuner_info("Firmware version is %d.%d\n",
					(version>>4)&0x0f,(version)&0x0f);

	priv->firm_type=std;

	return 0;
}

static int xc2028_signal(struct dvb_frontend *fe, u16 *strength)
{
	struct xc2028_data *priv = fe->tuner_priv;
	int                frq_lock, signal=0;

	tuner_info("%s called\n", __FUNCTION__);

	mutex_lock(&priv->lock);

	*strength = 0;

	frq_lock = xc2028_get_reg(priv, 0x2);
	if (frq_lock<=0)
		goto ret;

	/* Frequency is locked. Return signal quality */

	signal = xc2028_get_reg(priv, 0x40);

	if(signal<=0) {
		signal=frq_lock;
	}

ret:
	mutex_unlock(&priv->lock);

	*strength = signal;

	return 0;
}

#define DIV 15625

static int generic_set_tv_freq(struct dvb_frontend *fe, u32 freq /* in Hz */,
				enum tuner_mode new_mode,
				v4l2_std_id std,
				fe_bandwidth_t bandwidth)
{
	struct xc2028_data *priv = fe->tuner_priv;
	int           rc=-EINVAL;
	unsigned char buf[5];
	u32 div, offset = 0;

	tuner_info("%s called\n", __FUNCTION__);

	/* HACK: It seems that specific firmware need to be reloaded
	   when freq is changed */

	mutex_lock(&priv->lock);

	priv->firm_type=0;

	/* Reset GPIO 1 */
	rc = priv->tuner_callback(priv->video_dev, XC2028_TUNER_RESET, 0);
	if (rc<0)
		goto ret;

	msleep(10);
	tuner_info("should set frequency %d kHz)\n", freq / 1000);

	if (check_firmware(fe, new_mode, std, bandwidth)<0)
		goto ret;

	if(new_mode == T_DIGITAL_TV)
		offset = 2750000;

	div = (freq - offset + DIV/2)/DIV;

	/* CMD= Set frequency */
	send_seq(priv, {0x00, 0x02, 0x00, 0x00});
	rc = priv->tuner_callback(priv->video_dev, XC2028_RESET_CLK, 1);
	if (rc<0)
		goto ret;

	msleep(10);

	buf[0]= 0xff & (div>>24);
	buf[1]= 0xff & (div>>16);
	buf[2]= 0xff & (div>>8);
	buf[3]= 0xff & (div);
	buf[4]= 0;

	i2c_send(rc, priv, buf, sizeof(buf));
	if (rc<0)
		goto ret;
	msleep(100);

	priv->frequency=freq;

	printk("divider= %02x %02x %02x %02x (freq=%d.%02d)\n",
		 buf[1],buf[2],buf[3],buf[4],
		 freq / 1000000, (freq%1000000)/10000);

	rc=0;

ret:
	mutex_unlock(&priv->lock);

	return rc;
}

static int xc2028_set_tv_freq(struct dvb_frontend *fe,
			struct analog_parameters *p)
{
	struct xc2028_data *priv = fe->tuner_priv;

	tuner_info("%s called\n", __FUNCTION__);

	return generic_set_tv_freq(fe, 62500l*p->frequency, T_ANALOG_TV,
					      p->std,
					      BANDWIDTH_8_MHZ /* NOT USED */);
}

static int xc2028_set_params(struct dvb_frontend *fe,
			     struct dvb_frontend_parameters *p)
{
	struct xc2028_data *priv = fe->tuner_priv;

	tuner_info("%s called\n", __FUNCTION__);

	/* FIXME: Only OFDM implemented */
	if (fe->ops.info.type != FE_OFDM) {
		tuner_info ("DTV type not implemented.\n");
		return -EINVAL;
	}

	return generic_set_tv_freq(fe, p->frequency, T_DIGITAL_TV,
						0, /* NOT USED */
						p->u.ofdm.bandwidth);

}

static int xc2028_dvb_release(struct dvb_frontend *fe)
{
	struct xc2028_data *priv = fe->tuner_priv;

	tuner_info("%s called\n", __FUNCTION__);

	priv->count--;

	if (!priv->count)
		kfree (priv);

	return 0;
}

static int xc2028_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct xc2028_data *priv = fe->tuner_priv;

	tuner_info("%s called\n", __FUNCTION__);

	*frequency = priv->frequency;

	return 0;
}

static const struct dvb_tuner_ops xc2028_dvb_tuner_ops = {
	.info = {
			.name           = "Xceive XC3028",
			.frequency_min  =  42000000,
			.frequency_max  = 864000000,
			.frequency_step =     50000,
		},

	.set_analog_params = xc2028_set_tv_freq,
	.release           = xc2028_dvb_release,
	.get_frequency     = xc2028_get_frequency,
	.get_rf_strength   = xc2028_signal,
	.set_params        = xc2028_set_params,

// 	int (*sleep)(struct dvb_frontend *fe);
// 	int (*get_bandwidth)(struct dvb_frontend *fe, u32 *bandwidth);
// 	int (*get_status)(struct dvb_frontend *fe, u32 *status);
};

int xc2028_attach(struct dvb_frontend *fe, struct i2c_adapter* i2c_adap,
		  u8 i2c_addr, struct device *dev, void *video_dev,
		  int (*tuner_callback) (void *dev, int command,int arg))
{
	struct xc2028_data *priv;

	printk( KERN_INFO PREFIX "Xcv2028/3028 init called!\n");

	if (NULL == dev)
		return -ENODEV;

	if (NULL == video_dev)
		return -ENODEV;

	if (!tuner_callback) {
		printk( KERN_ERR PREFIX "No tuner callback!\n");
		return -EINVAL;
	}

	list_for_each_entry(priv, &xc2028_list, xc2028_list) {
		if (priv->dev == dev) {
			dev = NULL;
			priv->count++;
		}
	}

	if (dev) {
		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (priv == NULL)
			return -ENOMEM;

		fe->tuner_priv = priv;

		priv->bandwidth=BANDWIDTH_6_MHZ;
		priv->need_load_generic=1;
		priv->mode = T_UNINITIALIZED;
		priv->i2c_props.addr = i2c_addr;
		priv->i2c_props.adap = i2c_adap;
		priv->dev = dev;
		priv->video_dev = video_dev;
		priv->tuner_callback = tuner_callback;

		mutex_init(&priv->lock);

		list_add_tail(&priv->xc2028_list,&xc2028_list);
	}

	memcpy(&fe->ops.tuner_ops, &xc2028_dvb_tuner_ops,
					       sizeof(xc2028_dvb_tuner_ops));

	tuner_info("type set to %s\n", "XCeive xc2028/xc3028 tuner");

	return 0;
}

EXPORT_SYMBOL(xc2028_attach);

MODULE_DESCRIPTION("Xceive xc2028/xc3028 tuner driver");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_LICENSE("GPL");
