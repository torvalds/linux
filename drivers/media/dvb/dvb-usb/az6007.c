/* DVB USB compliant Linux driver for the AzureWave 6017 USB2.0 DVB-S
 * receiver.
 * see Documentation/dvb/README.dvb-usb for more information
 */

#include "az6007.h"
#include "drxk.h"
#include "mt2063.h"
#include "dvb_ca_en50221.h"

/* HACK: Should be moved to the right place */
#define USB_PID_AZUREWAVE_6007		0xccd
#define USB_PID_TERRATEC_H7		0x10b4

/* debug */
int dvb_usb_az6007_debug;
module_param_named(debug,dvb_usb_az6007_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2,rc=4 (or-able))." DVB_USB_DEBUG_STATUS);


static int az6007_type =0;
module_param(az6007_type, int, 0644);
MODULE_PARM_DESC(az6007_type, "select delivery mode (0=DVB-T, 1=DVB-T");

//module_param_named(type, 6007_type, int, 0644);
//MODULE_PARM_DESC(type, "select delivery mode (0=DVB-T, 1=DVB-C)");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);


struct az6007_device_state {
	struct dvb_ca_en50221 ca;
	struct mutex ca_mutex;
	u8 power_state;

	/* Due to DRX-K - probably need changes */
	int (*gate_ctrl)(struct dvb_frontend *, int);
	struct semaphore      pll_mutex;
	bool			dont_attach_fe1;
};

struct drxk_config terratec_h7_drxk = {
	.adr = 0x29,
	.single_master = 1,
	.no_i2c_bridge = 0,
	.microcode_name = "dvb-usb-terratec-h5-drxk.fw",
};

static int drxk_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct dvb_usb_adapter *adap = fe->sec_priv;
	struct az6007_device_state *st;
	int status;

	info("%s: %s", __func__, enable? "enable" : "disable" );

	if (!adap)
		return -EINVAL;

	st = adap->priv;

	if (!st)
		return -EINVAL;


	if (enable) {
#if 0
		down(&st->pll_mutex);
#endif
		status = st->gate_ctrl(fe, 1);
	} else {
#if 0
		status = st->gate_ctrl(fe, 0);
#endif
		up(&st->pll_mutex);
	}
	return status;
}

struct mt2063_config az6007_mt2063_config = {
	.tuner_address = 0x60,
	.refclock = 36125000,
};

/* check for mutex FIXME */
int az6007_usb_in_op(struct dvb_usb_device *d, u8 req, u16 value, u16 index, u8 *b, int blen)
{
	int ret = -1;

		ret = usb_control_msg(d->udev,
			usb_rcvctrlpipe(d->udev,0),
			req,
			USB_TYPE_VENDOR | USB_DIR_IN,
			value,index,b,blen,
			5000);

	if (ret < 0) {
		warn("usb in operation failed. (%d)", ret);
		return -EIO;
	}

	deb_xfer("in: req. %02x, val: %04x, ind: %04x, buffer: ",req,value,index);
	debug_dump(b,blen,deb_xfer);

	return ret;
}

static int az6007_usb_out_op(struct dvb_usb_device *d, u8 req, u16 value,
			     u16 index, u8 *b, int blen)
{
	int ret;

#if 0
	int i=0, cyc=0, rem=0;
	cyc = blen/64;
	rem = blen%64;
#endif

	deb_xfer("out: req. %02x, val: %04x, ind: %04x, buffer: ",req,value,index);
	debug_dump(b,blen,deb_xfer);


#if 0
	if (blen>64)
	{
		for (i=0; i<cyc; i++)
		{
			if ((ret = usb_control_msg(d->udev,
				usb_sndctrlpipe(d->udev,0),
				req,
				USB_TYPE_VENDOR | USB_DIR_OUT,
				value,index+i*64,b+i*64,64,
				5000)) != 64) {
				warn("usb out operation failed. (%d)",ret);
				return -EIO;
			}
		}

		if (rem>0)
		{
			if ((ret = usb_control_msg(d->udev,
				usb_sndctrlpipe(d->udev,0),
				req,
				USB_TYPE_VENDOR | USB_DIR_OUT,
				value,index+cyc*64,b+cyc*64,rem,
				5000)) != rem) {
				warn("usb out operation failed. (%d)",ret);
				return -EIO;
			}
		}
	}
	else
#endif
	{
		if ((ret = usb_control_msg(d->udev,
				usb_sndctrlpipe(d->udev,0),
				req,
				USB_TYPE_VENDOR | USB_DIR_OUT,
				value,index,b,blen,
				5000)) != blen) {
			warn("usb out operation failed. (%d)",ret);
			return -EIO;
		}
	}

	return 0;
}

static int az6007_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	return 0;
}

/* keys for the enclosed remote control */
struct rc_map_table rc_map_az6007_table[] = {
	{ 0x0001, KEY_1 },
	{ 0x0002, KEY_2 },
};

/* remote control stuff (does not work with my box) */
static int az6007_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	return 0;
#if 0
	u8 key[10];
	int i;

/* remove the following return to enabled remote querying */


	az6007_usb_in_op(d,READ_REMOTE_REQ,0,0,key,10);

	deb_rc("remote query key: %x %d\n",key[1],key[1]);

	if (key[1] == 0x44) {
		*state = REMOTE_NO_KEY_PRESSED;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(az6007_rc_keys); i++)
		if (az6007_rc_keys[i].custom == key[1]) {
			*state = REMOTE_KEY_PRESSED;
			*event = az6007_rc_keys[i].event;
			break;
		}
	return 0;
#endif
}

/*
int az6007_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 v = onoff;
	return az6007_usb_out_op(d,0xBC,v,3,NULL,1);
}
*/

static int az6007_read_mac_addr(struct dvb_usb_device *d,u8 mac[6])
{
	az6007_usb_in_op(d, 0xb7, 6, 0, &mac[0], 6);
	return 0;
}

static int az6007_frontend_poweron(struct dvb_usb_adapter *adap)
{
	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;

	info("az6007_frontend_poweron adap=%p adap->dev=%p", adap, adap->dev);

	req = 0xBC;
	value = 1;//power on
	index = 3;
	blen =0;

	if((ret = az6007_usb_out_op(adap->dev,req,value,index,NULL,blen)) != 0)
	{
		err("az6007_frontend_poweron failed!!!");
		 return -EIO;
	}

	msleep_interruptible(200);

	req = 0xBC;
	value = 0;//power on
	index = 3;
	blen =0;

	if((ret = az6007_usb_out_op(adap->dev,req,value,index,NULL,blen)) != 0)
	{
		err("az6007_frontend_poweron failed!!!");
		 return -EIO;
	}

	msleep_interruptible(200);

	req = 0xBC;
	value = 1;//power on
	index = 3;
	blen =0;

	if((ret = az6007_usb_out_op(adap->dev,req,value,index,NULL,blen)) != 0)
	{
		err("az6007_frontend_poweron failed!!!");
		 return -EIO;
	}
	info("az6007_frontend_poweron: OK");

	return 0;
}

static int az6007_frontend_reset(struct dvb_usb_adapter *adap)
{
	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;

	info("az6007_frontend_reset adap=%p adap->dev=%p", adap, adap->dev);

	//reset demodulator
	req = 0xC0;
	value = 1;//high
	index = 3;
	blen =0;
	if((ret = az6007_usb_out_op(adap->dev,req,value,index,NULL,blen)) != 0)
	{
		err("az6007_frontend_reset failed 1 !!!");
		   return -EIO;
	}

	req = 0xC0;
	value = 0;//low
	index = 3;
	blen =0;
	msleep_interruptible(200);
	if((ret = az6007_usb_out_op(adap->dev,req,value,index,NULL,blen)) != 0)
	{
		err("az6007_frontend_reset failed 2 !!!");
		   return -EIO;
	}
	msleep_interruptible(200);
	req = 0xC0;
	value = 1;//high
	index = 3;
	blen =0;

	if((ret = az6007_usb_out_op(adap->dev,req,value,index,NULL,blen)) != 0)
	{
		err("az6007_frontend_reset failed 3 !!!");
		   return -EIO;
	}

	msleep_interruptible(200);

	info("reset az6007 frontend");

	return 0;
}

static int az6007_led_on_off(struct usb_interface *intf, int onoff)
{
	int ret = -1;
	u8 req;
	u16 value;
	u16 index;
	int blen;
	//TS through
	req = 0xBC;
	value = onoff;
	index = 0;
	blen =0;

	ret = usb_control_msg(interface_to_usbdev(intf),
		usb_rcvctrlpipe(interface_to_usbdev(intf),0),
		req,
		USB_TYPE_VENDOR | USB_DIR_OUT,
		value,index,NULL,blen,
		2000);

	if (ret < 0) {
		warn("usb in operation failed. (%d)", ret);
		ret = -EIO;
	} else
		ret = 0;


	deb_xfer("in: req. %02x, val: %04x, ind: %04x, buffer: ",req,value,index);

	return ret;
}

static int az6007_frontend_tsbypass(struct dvb_usb_adapter *adap,int onoff)
{
	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;
	//TS through
	req = 0xC7;
	value = onoff;
	index = 0;
	blen =0;

	if((ret = az6007_usb_out_op(adap->dev,req,value,index,NULL,blen)) != 0)
		   return -EIO;
	return 0;
}

static int az6007_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct az6007_device_state *st = adap->priv;

	int result;

	BUG_ON(!st);

	az6007_frontend_poweron(adap);
	az6007_frontend_reset(adap);

	info("az6007_frontend_attach: drxk");

	adap->fe = dvb_attach(drxk_attach, &terratec_h7_drxk,
			      &adap->dev->i2c_adap, &adap->fe2);
	if (!adap->fe) {
		result = -EINVAL;
		goto out_free;
	}

	info("Setting hacks");

	/* FIXME: do we need a pll semaphore? */
	adap->fe->sec_priv = adap;
	sema_init(&st->pll_mutex, 1);
	st->gate_ctrl = adap->fe->ops.i2c_gate_ctrl;
	adap->fe->ops.i2c_gate_ctrl = drxk_gate_ctrl;
	adap->fe2->id = 1;

	info("az6007_frontend_attach: mt2063");
	/* Attach mt2063 to DVB-C frontend */
	if (adap->fe->ops.i2c_gate_ctrl)
		adap->fe->ops.i2c_gate_ctrl(adap->fe, 1);
	if (!dvb_attach(mt2063_attach, adap->fe, &az6007_mt2063_config,
			&adap->dev->i2c_adap)) {
		result = -EINVAL;

		goto out_free;
	}
	if (adap->fe->ops.i2c_gate_ctrl)
		adap->fe->ops.i2c_gate_ctrl(adap->fe, 0);

	/* Hack - needed due to drxk */
	adap->fe2->tuner_priv = adap->fe->tuner_priv;
	memcpy(&adap->fe2->ops.tuner_ops,
	       &adap->fe->ops.tuner_ops,
	       sizeof(adap->fe->ops.tuner_ops));
	return 0;

out_free:
	if (adap->fe)
		dvb_frontend_detach(adap->fe);
	adap->fe = NULL;
	adap->fe2 = NULL;

	return result;
}

static struct dvb_usb_device_properties az6007_properties;

static void
az6007_usb_disconnect(struct usb_interface *intf)
{
	dvb_usb_device_exit (intf);
}

/* I2C */
static int az6007_i2c_xfer(struct i2c_adapter *adap,struct i2c_msg msgs[],int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i, j, len;
	int ret = 0;
	u16 index;
	u16 value;
	int length;
	u8 req, addr;
	u8 data[512];

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		addr = msgs[i].addr << 1;
		if (((i + 1) < num)
		    && (msgs[i].len == 1)
		    && (!msgs[i].flags & I2C_M_RD)
		    && (msgs[i + 1].flags & I2C_M_RD)
		    && (msgs[i].addr == msgs[i + 1].addr)) {
			/*
			 * A write + read xfer for the same address, where
			 * the first xfer has just 1 byte length.
			 * Need to join both into one operation
			 */
			if (dvb_usb_az6007_debug & 2)
				printk(KERN_DEBUG
				       "az6007 I2C xfer write+read addr=0x%x len=%d/%d: ",
				       addr, msgs[i].len, msgs[i + 1].len);
			req = 0xb9;
			index = msgs[i].buf[0];
			value = addr | (1 << 8);
			length = 6 + msgs[i + 1].len;
			len = msgs[i + 1].len;
			ret = az6007_usb_in_op(d,req,value,index,data,length);
			if (ret >= len) {
				for (j = 0; j < len; j++) {
					msgs[i + 1].buf[j] = data[j + 5];
					if (dvb_usb_az6007_debug & 2)
						printk(KERN_CONT
						       "0x%02x ",
						       msgs[i + 1].buf[j]);
				}
			} else
				ret = -EIO;
			i++;
		} else if (!(msgs[i].flags & I2C_M_RD)) {
			/* write bytes */
			if (dvb_usb_az6007_debug & 2)
				printk(KERN_DEBUG
				       "az6007 I2C xfer write addr=0x%x len=%d: ",
				       addr, msgs[i].len);
			req = 0xbd;
			index = msgs[i].buf[0];
			value = addr | (1 << 8);
			length = msgs[i].len - 1;
			len = msgs[i].len - 1;
			if (dvb_usb_az6007_debug & 2)
				printk(KERN_CONT
				       "(0x%02x) ", msgs[i].buf[0]);
			for (j = 0; j < len; j++)
			{
				data[j] = msgs[i].buf[j + 1];
				if (dvb_usb_az6007_debug & 2)
					printk(KERN_CONT
					       "0x%02x ", data[j]);
			}
			ret = az6007_usb_out_op(d,req,value,index,data,length);
		} else {
			/* read bytes */
			if (dvb_usb_az6007_debug & 2)
				printk(KERN_DEBUG
				       "az6007 I2C xfer read addr=0x%x len=%d: ",
				       addr, msgs[i].len);
			req = 0xb9;
			index = msgs[i].buf[0];
			value = addr;
			length = msgs[i].len + 6;
			len = msgs[i].len;
			ret = az6007_usb_in_op(d,req,value,index,data,length);
			for (j = 0; j < len; j++)
			{
				msgs[i].buf[j] = data[j + 5];
				if (dvb_usb_az6007_debug & 2)
					printk(KERN_CONT
					       "0x%02x ", data[j + 5]);
			}
		}
		if (dvb_usb_az6007_debug & 2)
			printk(KERN_CONT "\n");
		if (ret < 0)
			goto err;
	}
err:
	mutex_unlock(&d->i2c_mutex);

	if (ret < 0) {
		info("%s ERROR: %i\n", __func__, ret);
		return ret;
	}
	return num;
}


static u32 az6007_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm az6007_i2c_algo = {
	.master_xfer   = az6007_i2c_xfer,
	.functionality = az6007_i2c_func,
#ifdef NEED_ALGO_CONTROL
	.algo_control = dummy_algo_control,
#endif
};

int az6007_identify_state(struct usb_device *udev, struct dvb_usb_device_properties *props,
			struct dvb_usb_device_description **desc, int *cold)
{
	u8 b[16];
	s16 ret = usb_control_msg(udev, usb_rcvctrlpipe(udev,0),
		0xb7, USB_TYPE_VENDOR | USB_DIR_IN, 6, 0, b, 6, USB_CTRL_GET_TIMEOUT);

	info("FW GET_VERSION length: %d",ret);

	*cold = ret <= 0;

	info("cold: %d", *cold);
	return 0;
}

static int az6007_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	az6007_led_on_off(intf, 0);

	return dvb_usb_device_init(intf, &az6007_properties,
				   THIS_MODULE, NULL, adapter_nr);
}

static struct usb_device_id az6007_usb_table [] = {
	    { USB_DEVICE(USB_VID_AZUREWAVE, USB_PID_AZUREWAVE_6007) },
	    { USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_H7) },
	    { 0 },
};

MODULE_DEVICE_TABLE(usb, az6007_usb_table);

static struct dvb_usb_device_properties az6007_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.usb_ctrl = CYPRESS_FX2,
	//.download_firmware = az6007_download_firmware,
	.firmware            = "dvb-usb-az6007-03.fw",
	.no_reconnect        = 1,

	.identify_state		= az6007_identify_state,
	.num_adapters = 1,
	.adapter = {
		{
			//.caps             = DVB_USB_ADAP_RECEIVES_204_BYTE_TS,

			.streaming_ctrl   = az6007_streaming_ctrl,
			.frontend_attach  = az6007_frontend_attach,

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
			.size_of_priv     = sizeof(struct az6007_device_state),
		}
	},
	//.power_ctrl       = az6007_power_ctrl,
	.read_mac_address = az6007_read_mac_addr,

	.rc.legacy = {
		.rc_map_table  = rc_map_az6007_table,
		.rc_map_size  = ARRAY_SIZE(rc_map_az6007_table),
		.rc_interval      = 400,
		.rc_query         = az6007_rc_query,
	},
	.i2c_algo         = &az6007_i2c_algo,

	.num_device_descs = 2,
	.devices = {
		{ .name = "AzureWave DTV StarBox DVB-T/C USB2.0 (az6007)",
		  .cold_ids = { &az6007_usb_table[0], NULL },
		  .warm_ids = { NULL },
		},
		{ .name = "TerraTec DTV StarBox DVB-T/C USB2.0 (az6007)",
		  .cold_ids = { &az6007_usb_table[1], NULL },
		  .warm_ids = { NULL },
		},
		{ NULL },
	}
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver az6007_usb_driver = {
	.name		= "dvb_usb_az6007",
	.probe 		= az6007_usb_probe,
	.disconnect = dvb_usb_device_exit,
	//.disconnect 	= az6007_usb_disconnect,
	.id_table 	= az6007_usb_table,
};

/* module stuff */
static int __init az6007_usb_module_init(void)
{
	int result;
	info("az6007 usb module init");
	if ((result = usb_register(&az6007_usb_driver))) {
		err("usb_register failed. (%d)",result);
		return result;
	}

	return 0;
}

static void __exit az6007_usb_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	info("az6007 usb module exit");
	usb_deregister(&az6007_usb_driver);
}

module_init(az6007_usb_module_init);
module_exit(az6007_usb_module_exit);

MODULE_AUTHOR("Henry Wang <Henry.wang@AzureWave.com>");
MODULE_DESCRIPTION("Driver for AzureWave 6007 DVB-C/T USB2.0 and clones");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
