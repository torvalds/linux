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

static char audio_std[8];
module_param_string(audio_std, audio_std, sizeof(audio_std), 0);
MODULE_PARM_DESC(audio_std,
	"Audio standard. XC3028 audio decoder explicitly "
	"needs to know what audio\n"
	"standard is needed for some video standards with audio A2 or NICAM.\n"
	"The valid values are:\n"
	"A2\n"
	"A2/A\n"
	"A2/B\n"
	"NICAM\n"
	"NICAM/A\n"
	"NICAM/B\n");

static LIST_HEAD(xc2028_list);
static DEFINE_MUTEX(xc2028_list_mutex);

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
	void			*video_dev;
	int			count;
	__u32			frequency;

	struct firmware_description *firm;
	int			firm_size;
	__u16			firm_version;

	struct xc2028_ctrl	ctrl;

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

#define i2c_send(priv, buf, size) ({					\
	int _rc;							\
	_rc = tuner_i2c_xfer_send(&priv->i2c_props, buf, size);		\
	if (size != _rc)						\
		tuner_info("i2c output error: rc = %d (should be %d)\n",\
			   _rc, (int)size);				\
	_rc;								\
})

#define i2c_rcv(priv, buf, size) ({					\
	int _rc;							\
	_rc = tuner_i2c_xfer_recv(&priv->i2c_props, buf, size);		\
	if (size != _rc)						\
		tuner_err("i2c input error: rc = %d (should be %d)\n",	\
			   _rc, (int)size); 				\
	_rc;								\
})

#define i2c_send_recv(priv, obuf, osize, ibuf, isize) ({		\
	int _rc;							\
	_rc = tuner_i2c_xfer_send_recv(&priv->i2c_props, obuf, osize,	\
				       ibuf, isize);			\
	if (isize != _rc)						\
		tuner_err("i2c input error: rc = %d (should be %d)\n",	\
			   _rc, (int)isize); 				\
	_rc;								\
})

#define send_seq(priv, data...)	({					\
	static u8 _val[] = data;					\
	int _rc;							\
	if (sizeof(_val) !=						\
			(_rc = tuner_i2c_xfer_send(&priv->i2c_props,	\
						_val, sizeof(_val)))) {	\
		tuner_err("Error on line %d: %d\n", __LINE__, _rc);	\
	} else 								\
		msleep(10);						\
	_rc;								\
})

static unsigned int xc2028_get_reg(struct xc2028_data *priv, u16 reg, u16 *val)
{
	unsigned char buf[2];
	unsigned char ibuf[2];

	tuner_dbg("%s %04x called\n", __FUNCTION__, reg);

	buf[0] = reg >> 8;
	buf[1] = (unsigned char) reg;

	if (i2c_send_recv(priv, buf, 2, ibuf, 2) != 2)
		return -EIO;

	*val = (ibuf[1]) | (ibuf[0] << 8);
	return 0;
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

static  v4l2_std_id parse_audio_std_option(void)
{
	if (strcasecmp(audio_std, "A2") == 0)
		return V4L2_STD_A2;
	if (strcasecmp(audio_std, "A2/A") == 0)
		return V4L2_STD_A2_A;
	if (strcasecmp(audio_std, "A2/B") == 0)
		return V4L2_STD_A2_B;
	if (strcasecmp(audio_std, "NICAM") == 0)
		return V4L2_STD_NICAM;
	if (strcasecmp(audio_std, "NICAM/A") == 0)
		return V4L2_STD_NICAM_A;
	if (strcasecmp(audio_std, "NICAM/B") == 0)
		return V4L2_STD_NICAM_B;

	return 0;
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
	priv->firm_size = 0;
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

	tuner_dbg("Reading firmware %s\n", priv->ctrl.fname);
	rc = request_firmware(&fw, priv->ctrl.fname,
			      &priv->i2c_props.adap->dev);
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

	if (fw->size < sizeof(name) - 1 + 2 + 2) {
		tuner_err("Error: firmware file %s has invalid size!\n",
			  priv->ctrl.fname);
		goto corrupt;
	}

	memcpy(name, p, sizeof(name) - 1);
	name[sizeof(name) - 1] = 0;
	p += sizeof(name) - 1;

	priv->firm_version = le16_to_cpu(*(__u16 *) p);
	p += 2;

	n_array = le16_to_cpu(*(__u16 *) p);
	p += 2;

	tuner_info("Loading %d firmware images from %s, type: %s, ver %d.%d\n",
		   n_array, priv->ctrl.fname, name,
		   priv->firm_version >> 8, priv->firm_version & 0xff);

	priv->firm = kzalloc(sizeof(*priv->firm) * n_array, GFP_KERNEL);
	if (priv->firm == NULL) {
		tuner_err("Not enough memory to load firmware file.\n");
		rc = -ENOMEM;
		goto err;
	}
	priv->firm_size = n_array;

	n = -1;
	while (p < endp) {
		__u32 type, size;
		v4l2_std_id id;

		n++;
		if (n >= n_array) {
			tuner_err("More firmware images in file than "
				  "were expected!\n");
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

		size = le32_to_cpu(*(__u32 *) p);
		p += sizeof(size);

		if ((!size) || (size + p > endp)) {
			tuner_err("Firmware type ");
			dump_firm_type(type);
			printk("(%x), id %llx is corrupted "
			       "(size=%d, expected %d)\n",
			       type, (unsigned long long)id,
			       (unsigned)(endp - p), size);
			goto corrupt;
		}

		priv->firm[n].ptr = kzalloc(size, GFP_KERNEL);
		if (priv->firm[n].ptr == NULL) {
			tuner_err("Not enough memory to load firmware file.\n");
			rc = -ENOMEM;
			goto err;
		}
		tuner_dbg("Reading firmware type ");
		if (debug) {
			dump_firm_type(type);
			printk("(%x), id %llx, size=%d.\n",
				   type, (unsigned long long)id, size);
		}

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
	tuner_info("Releasing partially loaded firmware file.\n");
	free_firmware(priv);

done:
	release_firmware(fw);
	if (rc == 0)
		tuner_dbg("Firmware files loaded.\n");

	return rc;
}

static int seek_firmware(struct dvb_frontend *fe, unsigned int type,
			 v4l2_std_id *id)
{
	struct xc2028_data *priv = fe->tuner_priv;
	int                 i, best_i = -1, best_nr_matches = 0;

	tuner_dbg("%s called, want type=", __FUNCTION__);
	if (debug) {
		dump_firm_type(type);
		printk("(%x), id %016llx.\n", type, (unsigned long long)*id);
	}

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
		v4l2_std_id match_mask;
		int nr_matches;

		if (type != priv->firm[i].type)
			continue;

		match_mask = *id & priv->firm[i].id;
		if (!match_mask)
			continue;

		if ((*id & match_mask) == *id)
			goto found; /* Supports all the requested standards */

		nr_matches = hweight64(match_mask);
		if (nr_matches > best_nr_matches) {
			best_nr_matches = nr_matches;
			best_i = i;
		}
	}

	if (best_nr_matches > 0) {
		tuner_dbg("Selecting best matching firmware (%d bits) for "
			  "type=", best_nr_matches);
		dump_firm_type(type);
		printk("(%x), id %016llx:\n", type, (unsigned long long)*id);
		i = best_i;
		goto found;
	}

	/*FIXME: Would make sense to seek for type "hint" match ? */

	i = -ENOENT;
	goto ret;

found:
	*id = priv->firm[i].id;

ret:
	tuner_dbg("%s firmware for type=", (i < 0) ? "Can't find" : "Found");
	if (debug) {
		dump_firm_type(type);
		printk("(%x), id %016llx.\n", type, (unsigned long long)*id);
	}
	return i;
}

static int load_firmware(struct dvb_frontend *fe, unsigned int type,
			 v4l2_std_id *id)
{
	struct xc2028_data *priv = fe->tuner_priv;
	int                pos, rc;
	unsigned char      *p, *endp, buf[priv->ctrl.max_len];

	tuner_dbg("%s called\n", __FUNCTION__);

	pos = seek_firmware(fe, type, id);
	if (pos < 0)
		return pos;

	tuner_info("Loading firmware for type=");
	dump_firm_type(priv->firm[pos].type);
	printk("(%x), id %016llx.\n", priv->firm[pos].type,
	       (unsigned long long)*id);

	p = priv->firm[pos].ptr;
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
		if (size >= 0xff00) {
			switch (size) {
			case 0xff00:
				rc = priv->tuner_callback(priv->video_dev,
							XC2028_RESET_CLK, 0);
				if (rc < 0) {
					tuner_err("Error at RESET code %d\n",
						  (*p) & 0x7f);
					return -EINVAL;
				}
				break;
			default:
				tuner_info("Invalid RESET code %d\n",
					   size & 0x7f);
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
			int len = (size < priv->ctrl.max_len - 1) ?
				   size : priv->ctrl.max_len - 1;

			memcpy(buf + 1, p, len);

			rc = i2c_send(priv, buf, len + 1);
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

	/* 16 SCODE entries per file; each SCODE entry is 12 bytes and
	 * has a 2-byte size header in the firmware format. */
	if (priv->firm[pos].size != 14 * 16 || scode >= 16 ||
	    le16_to_cpu(*(__u16 *)(p + 14 * scode)) != 12)
		return -EINVAL;

	tuner_info("Loading SCODE for type=");
	dump_firm_type(priv->firm[pos].type);
	printk("(%x), id %016llx.\n", priv->firm[pos].type,
	       (unsigned long long)*id);

	if (priv->firm_version < 0x0202)
		rc = send_seq(priv, {0x20, 0x00, 0x00, 0x00});
	else
		rc = send_seq(priv, {0xa0, 0x00, 0x00, 0x00});
	if (rc < 0)
		return -EIO;

	rc = i2c_send(priv, p + 14 * scode + 2, 12);
	if (rc < 0)
		return -EIO;

	rc = send_seq(priv, {0x00, 0x8c});
	if (rc < 0)
		return -EIO;

	return 0;
}

static int check_firmware(struct dvb_frontend *fe, enum tuner_mode new_mode,
			  v4l2_std_id std, fe_bandwidth_t bandwidth)
{
	struct xc2028_data      *priv = fe->tuner_priv;
	int			rc;
	u16			version, hwmodel;
	v4l2_std_id		std0 = 0;
	unsigned int		type0 = 0, type = 0;
	int			change_digital_bandwidth;

	tuner_dbg("%s called\n", __FUNCTION__);

	if (!priv->firm) {
		if (!priv->ctrl.fname) {
			tuner_info("xc2028/3028 firmware name not set!\n");
			return -EINVAL;
		}

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

		if (bandwidth == BANDWIDTH_7_MHZ ||
		    bandwidth == BANDWIDTH_8_MHZ)
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
		if (priv->ctrl.d2633)
			type |= D2633;
		else
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

	if (!change_digital_bandwidth && priv->mode == T_DIGITAL_TV)
		return 0;

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

	/* Add audio hack to std mask */
	std |= parse_audio_std_option();

	rc = load_firmware(fe, type, &std);
	if (rc < 0)
		return rc;

	/* Load SCODE firmware, if exists */
	tuner_dbg("Trying to load scode 0\n");
	type |= SCODE;

	rc = load_scode(fe, type, &std, 0);

	xc2028_get_reg(priv, 0x0004, &version);
	xc2028_get_reg(priv, 0x0008, &hwmodel);

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
	u16                 frq_lock, signal = 0;
	int                 rc;

	tuner_dbg("%s called\n", __FUNCTION__);

	mutex_lock(&priv->lock);

	/* Sync Lock Indicator */
	rc = xc2028_get_reg(priv, 0x0002, &frq_lock);
	if (rc < 0 || frq_lock == 0)
		goto ret;

	/* Frequency is locked. Return signal quality */

	/* Get SNR of the video signal */
	rc = xc2028_get_reg(priv, 0x0040, &signal);
	if (rc < 0)
		signal = -frq_lock;

ret:
	mutex_unlock(&priv->lock);

	*strength = signal;

	return rc;
}

#define DIV 15625

static int generic_set_tv_freq(struct dvb_frontend *fe, u32 freq /* in Hz */ ,
			       enum tuner_mode new_mode,
			       v4l2_std_id std, fe_bandwidth_t bandwidth)
{
	struct xc2028_data *priv = fe->tuner_priv;
	int		   rc = -EINVAL;
	unsigned char	   buf[4];
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
	tuner_dbg("should set frequency %d kHz\n", freq / 1000);

	if (check_firmware(fe, new_mode, std, bandwidth) < 0)
		goto ret;

	if (new_mode == T_DIGITAL_TV) {
		offset = 2750000;
		if (priv->bandwidth == BANDWIDTH_7_MHZ)
			offset -= 500000;
	}

	div = (freq - offset + DIV / 2) / DIV;

	/* CMD= Set frequency */
	if (priv->firm_version < 0x0202)
		rc = send_seq(priv, {0x00, 0x02, 0x00, 0x00});
	else
		rc = send_seq(priv, {0x80, 0x02, 0x00, 0x00});
	if (rc < 0)
		goto ret;

	rc = priv->tuner_callback(priv->video_dev, XC2028_RESET_CLK, 1);
	if (rc < 0)
		goto ret;

	msleep(10);

	buf[0] = 0xff & (div >> 24);
	buf[1] = 0xff & (div >> 16);
	buf[2] = 0xff & (div >> 8);
	buf[3] = 0xff & (div);

	rc = i2c_send(priv, buf, sizeof(buf));
	if (rc < 0)
		goto ret;
	msleep(100);

	priv->frequency = freq;

	tuner_dbg("divisor= %02x %02x %02x %02x (freq=%d.%03d)\n",
	       buf[0], buf[1], buf[2], buf[3],
	       freq / 1000000, (freq % 1000000) / 1000);

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
				   p->std, BANDWIDTH_8_MHZ);
				   /* XXX Are some analog standards 6MHz? */
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

	mutex_lock(&xc2028_list_mutex);

	priv->count--;

	if (!priv->count) {
		list_del(&priv->xc2028_list);

		kfree(priv->ctrl.fname);

		free_firmware(priv);
		kfree(priv);
		fe->tuner_priv = NULL;
	}

	mutex_unlock(&xc2028_list_mutex);

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
	int                 rc   = 0;

	tuner_dbg("%s called\n", __FUNCTION__);

	mutex_lock(&priv->lock);

	kfree(priv->ctrl.fname);
	free_firmware(priv);

	memcpy(&priv->ctrl, p, sizeof(priv->ctrl));
	priv->ctrl.fname = NULL;

	if (p->fname) {
		priv->ctrl.fname = kstrdup(p->fname, GFP_KERNEL);
		if (priv->ctrl.fname == NULL)
			rc = -ENOMEM;
	}

	if (priv->ctrl.max_len < 9)
		priv->ctrl.max_len = 13;

	mutex_unlock(&priv->lock);

	return rc;
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

void *xc2028_attach(struct dvb_frontend *fe, struct xc2028_config *cfg)
{
	struct xc2028_data *priv;
	void               *video_dev;

	if (debug)
		printk(KERN_DEBUG PREFIX ": Xcv2028/3028 init called!\n");

	if (NULL == cfg->video_dev)
		return NULL;

	if (!fe) {
		printk(KERN_ERR PREFIX ": No frontend!\n");
		return NULL;
	}

	video_dev = cfg->video_dev;

	mutex_lock(&xc2028_list_mutex);

	list_for_each_entry(priv, &xc2028_list, xc2028_list) {
		if (priv->video_dev == cfg->video_dev) {
			video_dev = NULL;
			break;
		}
	}

	if (video_dev) {
		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (priv == NULL) {
			mutex_unlock(&xc2028_list_mutex);
			return NULL;
		}

		priv->bandwidth = BANDWIDTH_6_MHZ;
		priv->need_load_generic = 1;
		priv->mode = T_UNINITIALIZED;
		priv->i2c_props.addr = cfg->i2c_addr;
		priv->i2c_props.adap = cfg->i2c_adap;
		priv->video_dev = video_dev;
		priv->tuner_callback = cfg->callback;
		priv->ctrl.max_len = 13;

		mutex_init(&priv->lock);

		list_add_tail(&priv->xc2028_list, &xc2028_list);
	}

	fe->tuner_priv = priv;
	priv->count++;

	memcpy(&fe->ops.tuner_ops, &xc2028_dvb_tuner_ops,
	       sizeof(xc2028_dvb_tuner_ops));

	tuner_info("type set to %s\n", "XCeive xc2028/xc3028 tuner");

	mutex_unlock(&xc2028_list_mutex);

	return fe;
}

EXPORT_SYMBOL(xc2028_attach);

MODULE_DESCRIPTION("Xceive xc2028/xc3028 tuner driver");
MODULE_AUTHOR("Michel Ludwig <michel.ludwig@gmail.com>");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_LICENSE("GPL");
