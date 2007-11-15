/* tuner-xc2028
 *
 * Copyright (c) 2007 Mauro Carvalho Chehab (mchehab@infradead.org)
 *
 * Copyright (c) 2007 Michel Ludwig (michel.ludwig@gmail.com)
 *       - frontend interface
 *
 * This code is placed under the terms of the GNU General Public License v2
 */

#include <linux/i2c.h>
#include <asm/div64.h>
#include <linux/firmware.h>
#include <linux/videodev2.h>
#include <linux/delay.h>
#include <media/tuner.h>
#include <linux/mutex.h>
#include "tuner-i2c.h"
#include "tuner-xc2028.h"
#include "tuner-xc2028-types.h"

#include <linux/dvb/frontend.h>
#include "dvb_frontend.h"

#define PREFIX "xc2028"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable verbose debug messages");

static LIST_HEAD(xc2028_list);
/* struct for storing firmware table */
struct firmware_description {
	unsigned int  type;
	v4l2_std_id   id;
	unsigned char *ptr;
	unsigned int  size;
};

struct xc2028_data {
	struct list_head        xc2028_list;
	struct tuner_i2c_props  i2c_props;
	int                     (*tuner_callback) (void *dev,
						   int command, int arg);
	struct device           *dev;
	void			*video_dev;
	int			count;
	__u32			frequency;

	struct firmware_description *firm;
	int			firm_size;

	__u16			version;

	struct xc2028_ctrl	ctrl;

	v4l2_std_id		firm_type;	   /* video stds supported
							by current firmware */
	fe_bandwidth_t		bandwidth;	   /* Firmware bandwidth:
							      6M, 7M or 8M */
	int			need_load_generic; /* The generic firmware
							      were loaded? */

	int			max_len;	/* Max firmware chunk */

	enum tuner_mode	mode;
	struct i2c_client	*i2c_client;

	struct mutex lock;
};

#define i2c_send(rc, priv, buf, size) do {				\
	rc = tuner_i2c_xfer_send(&priv->i2c_props, buf, size);		\
	if (size != rc)							\
		tuner_err("i2c output error: rc = %d (should be %d)\n",	\
			   rc, (int)size);				\
} while (0)

#define i2c_rcv(rc, priv, buf, size) do {				\
	rc = tuner_i2c_xfer_recv(&priv->i2c_props, buf, size);		\
	if (size != rc)							\
		tuner_err("i2c input error: rc = %d (should be %d)\n",	\
			   rc, (int)size); 				\
} while (0)

#define send_seq(priv, data...)	do {					\
	int rc;								\
	static u8 _val[] = data;					\
	if (sizeof(_val) !=						\
			(rc = tuner_i2c_xfer_send(&priv->i2c_props,	\
						_val, sizeof(_val)))) {	\
		tuner_err("Error on line %d: %d\n", __LINE__, rc);	\
		return -EINVAL;						\
	}								\
	msleep(10);							\
} while (0)

static unsigned int xc2028_get_reg(struct xc2028_data *priv, u16 reg)
{
	int rc;
	unsigned char buf[2];

	tuner_dbg("%s called\n", __FUNCTION__);

	buf[0] = reg>>8;
	buf[1] = (unsigned char) reg;

	i2c_send(rc, priv, buf, 2);
	if (rc < 0)
		return rc;

	i2c_rcv(rc, priv, buf, 2);
	if (rc < 0)
		return rc;

	return (buf[1]) | (buf[0] << 8);
}

void dump_firm_type(unsigned int type)
{
	 if (type & BASE)
		printk("BASE ");
	 if (type & INIT1)
		printk("INIT1 ");
	 if (type & F8MHZ)
		printk("F8MHZ ");
	 if (type & MTS)
		printk("MTS ");
	 if (type & D2620)
		printk("D2620 ");
	 if (type & D2633)
		printk("D2633 ");
	 if (type & DTV6)
		printk("DTV6 ");
	 if (type & QAM)
		printk("QAM ");
	 if (type & DTV7)
		printk("DTV7 ");
	 if (type & DTV78)
		printk("DTV78 ");
	 if (type & DTV8)
		printk("DTV8 ");
	 if (type & FM)
		printk("FM ");
	 if (type & INPUT1)
		printk("INPUT1 ");
	 if (type & LCD)
		printk("LCD ");
	 if (type & NOGD)
		printk("NOGD ");
	 if (type & MONO)
		printk("MONO ");
	 if (type & ATSC)
		printk("ATSC ");
	 if (type & IF)
		printk("IF ");
	 if (type & LG60)
		printk("LG60 ");
	 if (type & ATI638)
		printk("ATI638 ");
	 if (type & OREN538)
		printk("OREN538 ");
	 if (type & OREN36)
		printk("OREN36 ");
	 if (type & TOYOTA388)
		printk("TOYOTA388 ");
	 if (type & TOYOTA794)
		printk("TOYOTA794 ");
	 if (type & DIBCOM52)
		printk("DIBCOM52 ");
	 if (type & ZARLINK456)
		printk("ZARLINK456 ");
	 if (type & CHINA)
		printk("CHINA ");
	 if (type & F6MHZ)
		printk("F6MHZ ");
	 if (type & INPUT2)
		printk("INPUT2 ");
	 if (type & SCODE)
		printk("SCODE ");
}

static void free_firmware(struct xc2028_data *priv)
{
	int i;

	if (!priv->firm)
		return;

	for (i = 0; i < priv->firm_size; i++)
		kfree(priv->firm[i].ptr);

	kfree(priv->firm);

	priv->firm = NULL;
	priv->need_load_generic = 1;
}

static int load_all_firmwares(struct dvb_frontend *fe)
{
	struct xc2028_data    *priv = fe->tuner_priv;
	const struct firmware *fw   = NULL;
	unsigned char         *p, *endp;
	int                   rc = 0;
	int		      n, n_array;
	char		      name[33];

	tuner_dbg("%s called\n", __FUNCTION__);

	tuner_info("Reading firmware %s\n", priv->ctrl.fname);
	rc = request_firmware(&fw, priv->ctrl.fname, priv->dev);
	if (rc < 0) {
		if (rc == -ENOENT)
			tuner_err("Error: firmware %s not found.\n",
				   priv->ctrl.fname);
		else
			tuner_err("Error %d while requesting firmware %s \n",
				   rc, priv->ctrl.fname);

		return rc;
	}
	p = fw->data;
	endp = p + fw->size;

	if (fw->size < sizeof(name) - 1 + 2) {
		tuner_err("Error: firmware size is zero!\n");
		rc = -EINVAL;
		goto done;
	}

	memcpy(name, p, sizeof(name) - 1);
	name[sizeof(name) - 1] = 0;
	p += sizeof(name) - 1;

	priv->version = le16_to_cpu(*(__u16 *) p);
	p += 2;

	tuner_info("Firmware: %s, ver %d.%d\n", name,
		   priv->version >> 8, priv->version & 0xff);

	if (p + 2 > endp)
		goto corrupt;

	n_array = le16_to_cpu(*(__u16 *) p);
	p += 2;

	tuner_info("There are %d firmwares at %s\n",
		   n_array, priv->ctrl.fname);

	priv->firm = kzalloc(sizeof(*priv->firm) * n_array, GFP_KERNEL);

	if (!fw) {
		tuner_err("Not enough memory for reading firmware.\n");
		rc = -ENOMEM;
		goto done;
	}

	priv->firm_size = n_array;
	n = -1;
	while (p < endp) {
		__u32 type, size;
		v4l2_std_id id;

		n++;
		if (n >= n_array) {
			tuner_err("Too much firmwares at the file\n");
			goto corrupt;
		}

		/* Checks if there's enough bytes to read */
		if (p + sizeof(type) + sizeof(id) + sizeof(size) > endp) {
			tuner_err("Firmware header is incomplete!\n");
			goto corrupt;
		}

		type = le32_to_cpu(*(__u32 *) p);
		p += sizeof(type);

		id = le64_to_cpu(*(v4l2_std_id *) p);
		p += sizeof(id);

		size = le32_to_cpu(*(v4l2_std_id *) p);
		p += sizeof(size);

		if ((!size) || (size + p > endp)) {
			tuner_err("Firmware type ");
			dump_firm_type(type);
			printk("(%x), id %lx is corrupted "
			       "(size=%ld, expected %d)\n",
			       type, (unsigned long)id, endp - p, size);
			goto corrupt;
		}

		priv->firm[n].ptr = kzalloc(size, GFP_KERNEL);
		if (!priv->firm[n].ptr) {
			tuner_err("Not enough memory.\n");
			rc = -ENOMEM;
			goto err;
		}
		tuner_info("Reading firmware type ");
		dump_firm_type(type);
		printk("(%x), id %lx, size=%d.\n",
			   type, (unsigned long)id, size);

		memcpy(priv->firm[n].ptr, p, size);
		priv->firm[n].type = type;
		priv->firm[n].id   = id;
		priv->firm[n].size = size;

		p += size;
	}

	if (n + 1 != priv->firm_size) {
		tuner_err("Firmware file is incomplete!\n");
		goto corrupt;
	}

	goto done;

corrupt:
	rc = -EINVAL;
	tuner_err("Error: firmware file is corrupted!\n");

err:
	tuner_info("Releasing loaded firmware file.\n");

	free_firmware(priv);

done:
	release_firmware(fw);
	tuner_dbg("Firmware files loaded.\n");

	return rc;
}

static int seek_firmware(struct dvb_frontend *fe, unsigned int type,
			 v4l2_std_id *id)
{
	struct xc2028_data *priv = fe->tuner_priv;
	int                i;

	tuner_dbg("%s called\n", __FUNCTION__);

	if (!priv->firm) {
		tuner_err("Error! firmware not loaded\n");
		return -EINVAL;
	}

	if (((type & ~SCODE) == 0) && (*id == 0))
		*id = V4L2_STD_PAL;

	/* Seek for exact match */
	for (i = 0; i < priv->firm_size; i++) {
		if ((type == priv->firm[i].type) && (*id == priv->firm[i].id))
			goto found;
	}

	/* Seek for generic video standard match */
	for (i = 0; i < priv->firm_size; i++) {
		if ((type == priv->firm[i].type) && (*id & priv->firm[i].id))
			goto found;
	}

	/*FIXME: Would make sense to seek for type "hint" match ? */

	i = -EINVAL;
	goto ret;

found:
	*id = priv->firm[i].id;

ret:
	tuner_dbg("%s firmware for type=", (i < 0)? "Can't find": "Found");
	if (debug) {
		dump_firm_type(type);
		printk("(%x), id %08lx.\n", type, (unsigned long)*id);
	}
	return i;
}

static int load_firmware(struct dvb_frontend *fe, unsigned int type,
			 v4l2_std_id *id)
{
	struct xc2028_data *priv = fe->tuner_priv;
	int                pos, rc;
	unsigned char      *p, *endp, buf[priv->max_len];

	tuner_dbg("%s called\n", __FUNCTION__);

	pos = seek_firmware(fe, type, id);
	if (pos < 0)
		return pos;

	tuner_info("Loading firmware for type=");
	dump_firm_type(type);
	printk("(%x), id %08lx.\n", type, (unsigned long)*id);

	p = priv->firm[pos].ptr;

	if (!p) {
		tuner_err("Firmware pointer were freed!");
		return -EINVAL;
	}
	endp = p + priv->firm[pos].size;

	while (p < endp) {
		__u16 size;

		/* Checks if there's enough bytes to read */
		if (p + sizeof(size) > endp) {
			tuner_err("Firmware chunk size is wrong\n");
			return -EINVAL;
		}

		size = le16_to_cpu(*(__u16 *) p);
		p += sizeof(size);

		if (size == 0xffff)
			return 0;

		if (!size) {
			/* Special callback command received */
			rc = priv->tuner_callback(priv->video_dev,
						  XC2028_TUNER_RESET, 0);
			if (rc < 0) {
				tuner_err("Error at RESET code %d\n",
					   (*p) & 0x7f);
				return -EINVAL;
			}
			continue;
		}

		/* Checks for a sleep command */
		if (size & 0x8000) {
			msleep(size & 0x7fff);
			continue;
		}

		if ((size + p > endp)) {
			tuner_err("missing bytes: need %d, have %d\n",
				   size, (int)(endp - p));
			return -EINVAL;
		}

		buf[0] = *p;
		p++;
		size--;

		/* Sends message chunks */
		while (size > 0) {
			int len = (size < priv->max_len - 1) ?
				   size : priv->max_len - 1;

			memcpy(buf + 1, p, len);

			i2c_send(rc, priv, buf, len + 1);
			if (rc < 0) {
				tuner_err("%d returned from send\n", rc);
				return -EINVAL;
			}

			p += len;
			size -= len;
		}
	}
	return 0;
}

static int load_scode(struct dvb_frontend *fe, unsigned int type,
			 v4l2_std_id *id, int scode)
{
	struct xc2028_data *priv = fe->tuner_priv;
	int                pos, rc;
	unsigned char	   *p;

	tuner_dbg("%s called\n", __FUNCTION__);

	pos = seek_firmware(fe, type, id);
	if (pos < 0)
		return pos;

	p = priv->firm[pos].ptr;

	if (!p) {
		tuner_err("Firmware pointer were freed!");
		return -EINVAL;
	}

	if ((priv->firm[pos].size != 12 * 16) || (scode >= 16))
		return -EINVAL;

	if (priv->version < 0x0202) {
		send_seq(priv, {0x20, 0x00, 0x00, 0x00});
	} else {
		send_seq(priv, {0xa0, 0x00, 0x00, 0x00});
	}

	i2c_send(rc, priv, p + 12 * scode, 12);

	send_seq(priv, {0x00, 0x8c});

	return 0;
}

static int check_firmware(struct dvb_frontend *fe, enum tuner_mode new_mode,
			  v4l2_std_id std, fe_bandwidth_t bandwidth)
{
	struct xc2028_data      *priv = fe->tuner_priv;
	int			rc, version, hwmodel;
	v4l2_std_id		std0 = 0;
	unsigned int		type0 = 0, type = 0;
	int			change_digital_bandwidth;

	tuner_dbg("%s called\n", __FUNCTION__);

	if (!priv->firm) {
		if (!priv->ctrl.fname)
			return -EINVAL;

		rc = load_all_firmwares(fe);
		if (rc < 0)
			return rc;
	}

	tuner_dbg("I am in mode %u and I should switch to mode %i\n",
		   priv->mode, new_mode);

	/* first of all, determine whether we have switched the mode */
	if (new_mode != priv->mode) {
		priv->mode = new_mode;
		priv->need_load_generic = 1;
	}

	change_digital_bandwidth = (priv->mode == T_DIGITAL_TV
				    && bandwidth != priv->bandwidth) ? 1 : 0;
	tuner_dbg("old bandwidth %u, new bandwidth %u\n", priv->bandwidth,
		   bandwidth);

	if (priv->need_load_generic) {
		/* Reset is needed before loading firmware */
		rc = priv->tuner_callback(priv->video_dev,
					  XC2028_TUNER_RESET, 0);
		if (rc < 0)
			return rc;

		type0 = BASE;

		if (priv->ctrl.type == XC2028_FIRM_MTS)
			type0 |= MTS;

		if (priv->bandwidth == 8)
			type0 |= F8MHZ;

		/* FIXME: How to load FM and FM|INPUT1 firmwares? */

		rc = load_firmware(fe, type0, &std0);
		if (rc < 0) {
			tuner_err("Error %d while loading generic firmware\n",
				  rc);
			return rc;
		}

		priv->need_load_generic = 0;
		priv->firm_type = 0;
		if (priv->mode == T_DIGITAL_TV)
			change_digital_bandwidth = 1;
	}

	tuner_dbg("I should change bandwidth %u\n", change_digital_bandwidth);

	if (change_digital_bandwidth) {

		/*FIXME: Should allow selecting between D2620 and D2633 */
		type |= D2620;

		/* FIXME: When should select a DTV78 firmware?
		 */
		switch (bandwidth) {
		case BANDWIDTH_8_MHZ:
			type |= DTV8;
			break;
		case BANDWIDTH_7_MHZ:
			type |= DTV7;
			break;
		case BANDWIDTH_6_MHZ:
			/* FIXME: Should allow select also ATSC */
			type |= DTV6 | QAM;
			break;

		default:
			tuner_err("error: bandwidth not supported.\n");
		};
		priv->bandwidth = bandwidth;
	}

	/* Load INIT1, if needed */
	tuner_dbg("Load init1 firmware, if exists\n");
	type0 = BASE | INIT1;
	if (priv->ctrl.type == XC2028_FIRM_MTS)
		type0 |= MTS;

	/* FIXME: Should handle errors - if INIT1 found */
	rc = load_firmware(fe, type0, &std0);

	/* FIXME: Should add support for FM radio
	 */

	if (priv->ctrl.type == XC2028_FIRM_MTS)
		type |= MTS;

	if (priv->firm_type & std) {
		tuner_dbg("Std-specific firmware already loaded.\n");
		return 0;
	}

	rc = load_firmware(fe, type, &std);
	if (rc < 0)
		return rc;

	/* Load SCODE firmware, if exists */
	tuner_dbg("Trying to load scode 0\n");
	type |= SCODE;

	rc = load_scode(fe, type, &std, 0);

	version = xc2028_get_reg(priv, 0x0004);
	hwmodel = xc2028_get_reg(priv, 0x0008);

	tuner_info("Device is Xceive %d version %d.%d, "
		   "firmware version %d.%d\n",
		   hwmodel, (version & 0xf000) >> 12, (version & 0xf00) >> 8,
		   (version & 0xf0) >> 4, version & 0xf);

	priv->firm_type = std;

	return 0;
}

static int xc2028_signal(struct dvb_frontend *fe, u16 *strength)
{
	struct xc2028_data *priv = fe->tuner_priv;
	int                frq_lock, signal = 0;

	tuner_dbg("%s called\n", __FUNCTION__);

	mutex_lock(&priv->lock);

	*strength = 0;

	/* Sync Lock Indicator */
	frq_lock = xc2028_get_reg(priv, 0x0002);
	if (frq_lock <= 0)
		goto ret;

	/* Frequency is locked. Return signal quality */

	/* Get SNR of the video signal */
	signal = xc2028_get_reg(priv, 0x0040);

	if (signal <= 0)
		signal = frq_lock;

ret:
	mutex_unlock(&priv->lock);

	*strength = signal;

	return 0;
}

#define DIV 15625

static int generic_set_tv_freq(struct dvb_frontend *fe, u32 freq /* in Hz */ ,
			       enum tuner_mode new_mode,
			       v4l2_std_id std, fe_bandwidth_t bandwidth)
{
	struct xc2028_data *priv = fe->tuner_priv;
	int		   rc = -EINVAL;
	unsigned char	   buf[5];
	u32		   div, offset = 0;

	tuner_dbg("%s called\n", __FUNCTION__);

	mutex_lock(&priv->lock);

	/* HACK: It seems that specific firmware need to be reloaded
	   when freq is changed */

	priv->firm_type = 0;

	/* Reset GPIO 1 */
	rc = priv->tuner_callback(priv->video_dev, XC2028_TUNER_RESET, 0);
	if (rc < 0)
		goto ret;

	msleep(10);
	tuner_dbg("should set frequency %d kHz)\n", freq / 1000);

	if (check_firmware(fe, new_mode, std, bandwidth) < 0)
		goto ret;

	if (new_mode == T_DIGITAL_TV)
		offset = 2750000;

	div = (freq - offset + DIV / 2) / DIV;

	/* CMD= Set frequency */

	if (priv->version < 0x0202) {
		send_seq(priv, {0x00, 0x02, 0x00, 0x00});
	} else {
		send_seq(priv, {0x80, 0x02, 0x00, 0x00});
	}

	rc = priv->tuner_callback(priv->video_dev, XC2028_RESET_CLK, 1);
	if (rc < 0)
		goto ret;

	msleep(10);

	buf[0] = 0xff & (div >> 24);
	buf[1] = 0xff & (div >> 16);
	buf[2] = 0xff & (div >> 8);
	buf[3] = 0xff & (div);
	buf[4] = 0;

	i2c_send(rc, priv, buf, sizeof(buf));
	if (rc < 0)
		goto ret;
	msleep(100);

	priv->frequency = freq;

	tuner_dbg("divider= %02x %02x %02x %02x (freq=%d.%02d)\n",
	       buf[1], buf[2], buf[3], buf[4],
	       freq / 1000000, (freq % 1000000) / 10000);

	rc = 0;

ret:
	mutex_unlock(&priv->lock);

	return rc;
}

static int xc2028_set_tv_freq(struct dvb_frontend *fe,
			      struct analog_parameters *p)
{
	struct xc2028_data *priv = fe->tuner_priv;

	tuner_dbg("%s called\n", __FUNCTION__);

	return generic_set_tv_freq(fe, 62500l * p->frequency, T_ANALOG_TV,
				   p->std, BANDWIDTH_8_MHZ /* NOT USED */);
}

static int xc2028_set_params(struct dvb_frontend *fe,
			     struct dvb_frontend_parameters *p)
{
	struct xc2028_data *priv = fe->tuner_priv;

	tuner_dbg("%s called\n", __FUNCTION__);

	/* FIXME: Only OFDM implemented */
	if (fe->ops.info.type != FE_OFDM) {
		tuner_err("DTV type not implemented.\n");
		return -EINVAL;
	}

	return generic_set_tv_freq(fe, p->frequency, T_DIGITAL_TV,
				   0 /* NOT USED */,
				   p->u.ofdm.bandwidth);

}

static int xc2028_dvb_release(struct dvb_frontend *fe)
{
	struct xc2028_data *priv = fe->tuner_priv;

	tuner_dbg("%s called\n", __FUNCTION__);

	priv->count--;

	if (!priv->count) {
		list_del(&priv->xc2028_list);

		kfree(priv->ctrl.fname);

		free_firmware(priv);
		kfree(priv);
	}

	return 0;
}

static int xc2028_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct xc2028_data *priv = fe->tuner_priv;

	tuner_dbg("%s called\n", __FUNCTION__);

	*frequency = priv->frequency;

	return 0;
}

static int xc2028_set_config(struct dvb_frontend *fe, void *priv_cfg)
{
	struct xc2028_data *priv = fe->tuner_priv;
	struct xc2028_ctrl *p    = priv_cfg;

	tuner_dbg("%s called\n", __FUNCTION__);

	priv->ctrl.type = p->type;

	if (p->fname) {
		kfree(priv->ctrl.fname);

		priv->ctrl.fname = kmalloc(strlen(p->fname) + 1, GFP_KERNEL);
		if (!priv->ctrl.fname)
			return -ENOMEM;

		free_firmware(priv);
		strcpy(priv->ctrl.fname, p->fname);
	}

	if (p->max_len > 0)
		priv->max_len = p->max_len;

	return 0;
}

static const struct dvb_tuner_ops xc2028_dvb_tuner_ops = {
	.info = {
		 .name = "Xceive XC3028",
		 .frequency_min = 42000000,
		 .frequency_max = 864000000,
		 .frequency_step = 50000,
		 },

	.set_config	   = xc2028_set_config,
	.set_analog_params = xc2028_set_tv_freq,
	.release           = xc2028_dvb_release,
	.get_frequency     = xc2028_get_frequency,
	.get_rf_strength   = xc2028_signal,
	.set_params        = xc2028_set_params,

};

int xc2028_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c_adap,
		  u8 i2c_addr, struct device *dev, void *video_dev,
		  int (*tuner_callback) (void *dev, int command, int arg))
{
	struct xc2028_data *priv;

	if (debug)
		printk(KERN_DEBUG PREFIX "Xcv2028/3028 init called!\n");

	if (NULL == dev)
		return -ENODEV;

	if (NULL == video_dev)
		return -ENODEV;

	if (!tuner_callback) {
		printk(KERN_ERR PREFIX "No tuner callback!\n");
		return -EINVAL;
	}

	list_for_each_entry(priv, &xc2028_list, xc2028_list) {
		if (priv->dev == dev)
			dev = NULL;
	}

	if (dev) {
		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (priv == NULL)
			return -ENOMEM;

		fe->tuner_priv = priv;

		priv->bandwidth = BANDWIDTH_6_MHZ;
		priv->need_load_generic = 1;
		priv->mode = T_UNINITIALIZED;
		priv->i2c_props.addr = i2c_addr;
		priv->i2c_props.adap = i2c_adap;
		priv->dev = dev;
		priv->video_dev = video_dev;
		priv->tuner_callback = tuner_callback;
		priv->max_len = 13;


		mutex_init(&priv->lock);

		list_add_tail(&priv->xc2028_list, &xc2028_list);
	}
	priv->count++;

	memcpy(&fe->ops.tuner_ops, &xc2028_dvb_tuner_ops,
	       sizeof(xc2028_dvb_tuner_ops));

	tuner_info("type set to %s\n", "XCeive xc2028/xc3028 tuner");

	return 0;
}
EXPORT_SYMBOL(xc2028_attach);

MODULE_DESCRIPTION("Xceive xc2028/xc3028 tuner driver");
MODULE_AUTHOR("Michel Ludwig <michel.ludwig@gmail.com>");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_LICENSE("GPL");
