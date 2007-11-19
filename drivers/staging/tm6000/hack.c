





/*
   hack.h - hackish code that needs to be improved (or removed) at a
	    later point

   Copyright (C) 2007 Michel Ludwig <michel.ludwig@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation version 2

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "hack.h"

#include "tm6000.h"

#include <linux/usb.h>

static inline int tm6000_snd_control_msg(struct tm6000_core *dev, __u8 request, __u16 value, __u16 index, void *data, __u16 size)
{
	return tm6000_read_write_usb (dev, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, request, value, index, data, size);
}

static int pseudo_zl10353_pll(struct tm6000_core *tm6000_dev, struct dvb_frontend_parameters *p)
{
	int ret;
	u8 *data = kzalloc(50*sizeof(u8), GFP_KERNEL);

printk(KERN_ALERT "should set frequency %u\n", p->frequency);
printk(KERN_ALERT "and bandwith %u\n", p->u.ofdm.bandwidth);

	if(tm6000_dev->dvb->frontend->ops.tuner_ops.set_params) {
		tm6000_dev->dvb->frontend->ops.tuner_ops.set_params(tm6000_dev->dvb->frontend, p);
	}
	else {
		printk(KERN_ALERT "pseudo zl10353: couldn't set tuner parameters\n");
	}

	// init ZL10353
	data[0] = 0x0b;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x501e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x80;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x551e, 0x00, data, 0x1);
	msleep(100);
	data[0] = 0x01;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0xea1e, 0x00, data, 0x1);
	msleep(100);
	data[0] = 0x00;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0xea1e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x1c;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x561e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x40;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x5e1e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x36;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x641e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x67;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x651e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0xe5;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x661e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x19;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x6c1e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0xe9;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x6d1e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x44;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x511e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x46;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x521e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x15;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x531e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x0f;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x541e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x75;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x5c1e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x01;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x701e, 0x00, data, 0x1);
	msleep(15);
	data[0] = 0x00;
	ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x701e, 0x00, data, 0x1);
	msleep(15);

	msleep(50);

	switch(p->u.ofdm.bandwidth) {
		case BANDWIDTH_8_MHZ:
			data[0] = 0x00;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x701e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x36;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x641e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x67;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x651e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0xe5;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x661e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x19;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x6c1e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0xe9;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x6d1e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x44;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x511e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x46;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x521e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x15;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x531e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x0f;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x541e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x75;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x5c1e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x01;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x701e, 0x00, data, 0x1);
			msleep(15);
		break;

		default:
			printk(KERN_ALERT "tm6000: bandwidth not supported\n");
		case BANDWIDTH_7_MHZ:
			data[0] = 0x00;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x701e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x35;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x641e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x5a;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x651e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0xe9;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x661e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x19;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x6c1e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0xe9;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x6d1e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x44;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x511e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x46;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x521e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x15;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x531e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x0f;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x541e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x86;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x5c1e, 0x00, data, 0x1);
			msleep(15);
			data[0] = 0x01;
			ret = tm6000_snd_control_msg(tm6000_dev, 0x10, 0x701e, 0x00, data, 0x1);
			msleep(15);
		break;
	}

	kfree(data);

	return 0;
};



int pseudo_zl10353_set_frontend(struct dvb_frontend *fe,
				  struct dvb_frontend_parameters *p)
{
	struct tm6000_core *tm6000_dev = fe->dvb->priv;
	u32 status;

	if(p != NULL) {
// 		mutex_lock(&tm6000_dev->mutex);
		pseudo_zl10353_pll(tm6000_dev, p);
// 		mutex_unlock(&tm6000_dev->mutex);
	}

	if(tm6000_dev->dvb->frontend->ops.read_status) {
		tm6000_dev->dvb->frontend->ops.read_status(tm6000_dev->dvb->frontend, &status);
		printk(KERN_ALERT "demodulator status: FE_HAS_CARRIER %i \n", (status & FE_HAS_CARRIER));
		printk(KERN_ALERT "demodulator status: FE_HAS_VITERBI %i \n", (status & FE_HAS_VITERBI));
		printk(KERN_ALERT "demodulator status: FE_HAS_LOCK %i \n", (status & FE_HAS_LOCK));
		printk(KERN_ALERT "demodulator status: FE_HAS_SYNC %i \n", (status & FE_HAS_SYNC));
		printk(KERN_ALERT "demodulator status: FE_HAS_SIGNAL %i \n", (status & FE_HAS_SIGNAL));
	}
	else {
		printk(KERN_ALERT "pseudo zl10353: couldn't read demodulator status\n");
	}
	return 0;
}

int pseudo_zl10353_read_status(struct dvb_frontend *fe, fe_status_t *status)
{

	*status = FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK | FE_HAS_SIGNAL;

	return 0;
}

struct dvb_frontend* pseudo_zl10353_attach(struct tm6000_core *dev,
					   const struct zl10353_config *config,
								   struct i2c_adapter *i2c)
{
	struct tm6000_dvb *dvb = dev->dvb;

	dvb->frontend = dvb_attach(zl10353_attach, config, i2c);
	if(!dvb->frontend) {
		printk(KERN_ERR "Error during zl10353_attach!\n");
		return NULL;
	}

	/* override some functions with our implementations */
	dvb->frontend->ops.set_frontend = pseudo_zl10353_set_frontend;
	dvb->frontend->ops.read_status = pseudo_zl10353_read_status;
	dvb->frontend->frontend_priv = dev;

	return dvb->frontend;
}
