/* tuner-xc2028
 *
 * Copyright (c) 2007 Mauro Carvalho Chehab (mchehab@infradead.org)
 * This code is placed under the terms of the GNU General Public License v2
 */

#include <linux/i2c.h>
#include <asm/div64.h>
#include <linux/firmware.h>
#include <linux/videodev.h>
#include <linux/delay.h>
#include "tuner-driver.h"
#include "tuner-xc2028.h"

/* Firmwares used on tm5600/tm6000 + xc2028/xc3028 */
static const char *firmware_6M = "tm6000_xc3028_DTV_6M.fw";
static const char *firmware_8M = "tm6000_xc3028_78M.fw";
static const char *firmware_DK = "tm6000_xc3028_DK_PAL_MTS.fw";
static const char *firmware_MN = "tm6000_xc3028_MN_BTSC.fw";

struct xc2028_data {
	v4l2_std_id	firm_type;	/* video stds supported by current firmware */
	int		bandwidth;	/* Firmware bandwidth: 6M, 7M or 8M */
	int		need_load_generic; /* The generic firmware were loaded? */
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

	if (t->tuner_callback) {
		rc = t->tuner_callback( c->adapter->algo_data,
					XC2028_RESET_CLK, 0);
		if (rc<0)
			return rc;
	}

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

	tuner_info("Loading firmware %s\n", name);
	rc = request_firmware(&fw, name, &c->dev);
	if (rc < 0) {
		tuner_info("Error %d while requesting firmware\n", rc);
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

static int check_firmware(struct i2c_client *c)
{
	int			rc, version;
	struct tuner		*t = i2c_get_clientdata(c);
	struct xc2028_data	*xc2028 = t->priv;
	const char		*name;

	if (!t->tuner_callback) {
		printk(KERN_ERR "xc2028: need tuner_callback to load firmware\n");
		return -EINVAL;
	}

	if (xc2028->need_load_generic) {
		if (xc2028->bandwidth==6)
			name = firmware_6M;
		else
			name = firmware_8M;

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
	}

	if (xc2028->firm_type & t->std)
		return 0;

	if (t->std & V4L2_STD_MN)
		name=firmware_MN;
	else
		name=firmware_DK;

	rc = load_firmware(c,name);
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

	if (check_firmware(c)<0)
		return 0;

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

static void set_tv_freq(struct i2c_client *c, unsigned int freq)
{
	int           rc;
	unsigned char buf[5];
	struct tuner  *t  = i2c_get_clientdata(c);
	unsigned long div = (freq*62500l+DIV/2)/DIV;

	if (check_firmware(c)<0)
		return;

	/* Reset GPIO 1 */
	if (t->tuner_callback) {
		rc = t->tuner_callback( c->adapter->algo_data,
					XC2028_TUNER_RESET, 0);
		if (rc<0)
			return;
	}
	msleep(10);

	send_seq (c, {0x12, 0x39});
	send_seq (c, {0x0c, 0x80, 0xf0, 0xf7, 0x3e, 0x75, 0xc1, 0x8a, 0xe4});
	send_seq (c, {0x0c, 0x02, 0x00});
	send_seq (c, {0x05, 0x0f, 0xee, 0xaa, 0x5f, 0xea, 0x90});
	send_seq (c, {0x06, 0x00, 0x0a, 0x4d, 0x8c, 0xf2, 0xd8, 0xcf, 0x30});
	send_seq (c, {0x06, 0x79, 0x9f});
	send_seq (c, {0x0b, 0x0d, 0xa4, 0x6c});
	send_seq (c, {0x0a, 0x01, 0x67, 0x24, 0x40, 0x08, 0xc3, 0x20, 0x10});
	send_seq (c, {0x0a, 0x64, 0x3c, 0xfa, 0xf7, 0xe1, 0x0c, 0x2c});
	send_seq (c, {0x09, 0x0b});
	send_seq (c, {0x10, 0x13});
	send_seq (c, {0x16, 0x12});
	send_seq (c, {0x1f, 0x02});
	send_seq (c, {0x21, 0x02});
	send_seq (c, {0x01, 0x02});
	send_seq (c, {0x2b, 0x10});
	send_seq (c, {0x02, 0x02});
	send_seq (c, {0x02, 0x03});
	send_seq (c, {0x00, 0x8c});

	send_seq (c, {0x00, 0x01, 0x00, 0x00});
	send_seq (c, {0x00, 0xcc, 0x20, 0x06});
	send_seq (c, {0x2b, 0x1a});
	send_seq (c, {0x2b, 0x1b});
	send_seq (c, {0x14, 0x01, 0x1b, 0x19, 0xb5, 0x29, 0xab, 0x09, 0x55});
	send_seq (c, {0x14, 0x44, 0x05, 0x65});
	send_seq (c, {0x13, 0x18, 0x08, 0x00, 0x00, 0x6c, 0x18, 0x16, 0x8c});
	send_seq (c, {0x13, 0x49, 0x2a, 0xab});
	send_seq (c, {0x0d, 0x01, 0x4b, 0x03, 0x97, 0x55, 0xc7, 0xd7, 0x00});
	send_seq (c, {0x0d, 0xa1, 0xeb, 0x8f, 0x5c});
	send_seq (c, {0x1a, 0x00, 0x00, 0x16, 0x8a, 0x40, 0x00, 0x00, 0x00, 0x20});
	send_seq (c, {0x2d, 0x01});
	send_seq (c, {0x18, 0x00});
	send_seq (c, {0x1b, 0x0d, 0x86, 0x51, 0xd2, 0x35, 0xa4, 0x92, 0xa5});
	send_seq (c, {0x1b, 0xb5, 0x25, 0x65});
	send_seq (c, {0x1d, 0x00});
	send_seq (c, {0x0f, 0x00, 0x29, 0x56, 0xb0, 0x00, 0xb6});
	send_seq (c, {0x20, 0x00});
	send_seq (c, {0x1e, 0x09, 0x02, 0x5b, 0x6c, 0x00, 0x4b, 0x81, 0x56});
	send_seq (c, {0x1e, 0x46, 0x69, 0x0b});
	send_seq (c, {0x22, 0x32});
	send_seq (c, {0x23, 0x0a});
	send_seq (c, {0x25, 0x00, 0x09, 0x90, 0x09, 0x06, 0x64, 0x02, 0x41});
	send_seq (c, {0x26, 0xcc});
	send_seq (c, {0x29, 0x40});
	send_seq (c, {0x21, 0x03});
	send_seq (c, {0x00, 0x8c});
	send_seq (c, {0x00, 0x00, 0x00, 0x00});

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

	if (init) {
		printk (KERN_ERR "Module already initialized!\n");
		return 0;
	}
	init++;

	xc2028 = kzalloc(sizeof(*xc2028), GFP_KERNEL);
	if (!xc2028)
		return -ENOMEM;
	t->priv = xc2028;

#ifdef HACK
	xc2028->firm_type=1;
	xc2028->bandwidth=6;
#endif
	xc2028->bandwidth=6;
	xc2028->need_load_generic=1;

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
