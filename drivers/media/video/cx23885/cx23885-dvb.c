/*
 *  Driver for the Conexant CX23885 PCIe bridge
 *
 *  Copyright (c) 2006 Steven Toth <stoth@hauppauge.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/file.h>
#include <linux/suspend.h>

#include "cx23885.h"
#include <media/v4l2-common.h>

#include "s5h1409.h"
#include "mt2131.h"
#include "tda8290.h"
#include "tda18271.h"
#include "lgdt330x.h"
#include "xc5000.h"
#include "dvb-pll.h"
#include "tuner-xc2028.h"
#include "tuner-xc2028-types.h"

static unsigned int debug;

#define dprintk(level, fmt, arg...)\
	do { if (debug >= level)\
		printk(KERN_DEBUG "%s/0: " fmt, dev->name, ## arg);\
	} while (0)

/* ------------------------------------------------------------------ */

static unsigned int alt_tuner;
module_param(alt_tuner, int, 0644);
MODULE_PARM_DESC(alt_tuner, "Enable alternate tuner configuration");

/* ------------------------------------------------------------------ */

static int dvb_buf_setup(struct videobuf_queue *q,
			 unsigned int *count, unsigned int *size)
{
	struct cx23885_tsport *port = q->priv_data;

	port->ts_packet_size  = 188 * 4;
	port->ts_packet_count = 32;

	*size  = port->ts_packet_size * port->ts_packet_count;
	*count = 32;
	return 0;
}

static int dvb_buf_prepare(struct videobuf_queue *q,
			   struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct cx23885_tsport *port = q->priv_data;
	return cx23885_buf_prepare(q, port, (struct cx23885_buffer*)vb, field);
}

static void dvb_buf_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct cx23885_tsport *port = q->priv_data;
	cx23885_buf_queue(port, (struct cx23885_buffer*)vb);
}

static void dvb_buf_release(struct videobuf_queue *q,
			    struct videobuf_buffer *vb)
{
	cx23885_free_buffer(q, (struct cx23885_buffer*)vb);
}

static struct videobuf_queue_ops dvb_qops = {
	.buf_setup    = dvb_buf_setup,
	.buf_prepare  = dvb_buf_prepare,
	.buf_queue    = dvb_buf_queue,
	.buf_release  = dvb_buf_release,
};

static struct s5h1409_config hauppauge_generic_config = {
	.demod_address = 0x32 >> 1,
	.output_mode   = S5H1409_SERIAL_OUTPUT,
	.gpio          = S5H1409_GPIO_ON,
	.qam_if        = 44000,
	.inversion     = S5H1409_INVERSION_OFF,
	.status_mode   = S5H1409_DEMODLOCKING,
	.mpeg_timing   = S5H1409_MPEGTIMING_CONTINOUS_NONINVERTING_CLOCK,
};

static struct s5h1409_config hauppauge_ezqam_config = {
	.demod_address = 0x32 >> 1,
	.output_mode   = S5H1409_SERIAL_OUTPUT,
	.gpio          = S5H1409_GPIO_OFF,
	.qam_if        = 4000,
	.inversion     = S5H1409_INVERSION_ON,
	.status_mode   = S5H1409_DEMODLOCKING,
	.mpeg_timing   = S5H1409_MPEGTIMING_CONTINOUS_NONINVERTING_CLOCK,
};

static struct s5h1409_config hauppauge_hvr1800lp_config = {
	.demod_address = 0x32 >> 1,
	.output_mode   = S5H1409_SERIAL_OUTPUT,
	.gpio          = S5H1409_GPIO_OFF,
	.qam_if        = 44000,
	.inversion     = S5H1409_INVERSION_OFF,
	.status_mode   = S5H1409_DEMODLOCKING,
	.mpeg_timing   = S5H1409_MPEGTIMING_CONTINOUS_NONINVERTING_CLOCK,
};

static struct s5h1409_config hauppauge_hvr1500_config = {
	.demod_address = 0x32 >> 1,
	.output_mode   = S5H1409_SERIAL_OUTPUT,
	.gpio          = S5H1409_GPIO_OFF,
	.inversion     = S5H1409_INVERSION_OFF,
	.status_mode   = S5H1409_DEMODLOCKING,
	.mpeg_timing   = S5H1409_MPEGTIMING_CONTINOUS_NONINVERTING_CLOCK,
};

static struct mt2131_config hauppauge_generic_tunerconfig = {
	0x61
};

static struct lgdt330x_config fusionhdtv_5_express = {
	.demod_address = 0x0e,
	.demod_chip = LGDT3303,
	.serial_mpeg = 0x40,
};

static struct s5h1409_config hauppauge_hvr1500q_config = {
	.demod_address = 0x32 >> 1,
	.output_mode   = S5H1409_SERIAL_OUTPUT,
	.gpio          = S5H1409_GPIO_ON,
	.qam_if        = 44000,
	.inversion     = S5H1409_INVERSION_OFF,
	.status_mode   = S5H1409_DEMODLOCKING,
	.mpeg_timing   = S5H1409_MPEGTIMING_CONTINOUS_NONINVERTING_CLOCK,
};

static struct xc5000_config hauppauge_hvr1500q_tunerconfig = {
	.i2c_address      = 0x61,
	.if_khz           = 5380,
	.tuner_callback   = cx23885_tuner_callback
};

static struct tda829x_config tda829x_no_probe = {
	.probe_tuner = TDA829X_DONT_PROBE,
};

static struct tda18271_std_map hauppauge_tda18271_std_map = {
	.atsc_6   = { .if_freq = 5380, .std_bits = 0x1b },
	.qam_6    = { .if_freq = 4000, .std_bits = 0x18 },
};

static struct tda18271_config hauppauge_tda18271_config = {
	.std_map = &hauppauge_tda18271_std_map,
	.gate    = TDA18271_GATE_ANALOG,
};

static int cx23885_hvr1500_xc3028_callback(void *ptr, int command, int arg)
{
	struct cx23885_tsport *port = ptr;
	struct cx23885_dev *dev = port->dev;

	switch (command) {
	case XC2028_TUNER_RESET:
		/* Send the tuner in then out of reset */
		/* GPIO-2 xc3028 tuner */
		dprintk(1, "%s: XC2028_TUNER_RESET %d\n", __FUNCTION__, arg);

		cx_set(GP0_IO, 0x00040000);
		cx_clear(GP0_IO, 0x00000004);
		msleep(5);

		cx_set(GP0_IO, 0x00040004);
		msleep(5);
		break;
	case XC2028_RESET_CLK:
		dprintk(1, "%s: XC2028_RESET_CLK %d\n", __FUNCTION__, arg);
		break;
	default:
		dprintk(1, "%s: unknown command %d, arg %d\n", __FUNCTION__,
			command, arg);
		return -EINVAL;
	}

	return 0;
}

static int dvb_register(struct cx23885_tsport *port)
{
	struct cx23885_dev *dev = port->dev;
	struct cx23885_i2c *i2c_bus = NULL;

	/* init struct videobuf_dvb */
	port->dvb.name = dev->name;

	/* init frontend */
	switch (dev->board) {
	case CX23885_BOARD_HAUPPAUGE_HVR1250:
		i2c_bus = &dev->i2c_bus[0];
		port->dvb.frontend = dvb_attach(s5h1409_attach,
						&hauppauge_generic_config,
						&i2c_bus->i2c_adap);
		if (port->dvb.frontend != NULL) {
			dvb_attach(mt2131_attach, port->dvb.frontend,
				   &i2c_bus->i2c_adap,
				   &hauppauge_generic_tunerconfig, 0);
		}
		break;
	case CX23885_BOARD_HAUPPAUGE_HVR1800:
		i2c_bus = &dev->i2c_bus[0];
		switch (alt_tuner) {
		case 1:
			port->dvb.frontend =
				dvb_attach(s5h1409_attach,
					   &hauppauge_ezqam_config,
					   &i2c_bus->i2c_adap);
			if (port->dvb.frontend != NULL) {
				dvb_attach(tda829x_attach, port->dvb.frontend,
					   &dev->i2c_bus[1].i2c_adap, 0x42,
					   &tda829x_no_probe);
				dvb_attach(tda18271_attach, port->dvb.frontend,
					   0x60, &dev->i2c_bus[1].i2c_adap,
					   &hauppauge_tda18271_config);
			}
			break;
		case 0:
		default:
			port->dvb.frontend =
				dvb_attach(s5h1409_attach,
					   &hauppauge_generic_config,
					   &i2c_bus->i2c_adap);
			if (port->dvb.frontend != NULL)
				dvb_attach(mt2131_attach, port->dvb.frontend,
					   &i2c_bus->i2c_adap,
					   &hauppauge_generic_tunerconfig, 0);
			break;
		}
		break;
	case CX23885_BOARD_HAUPPAUGE_HVR1800lp:
		i2c_bus = &dev->i2c_bus[0];
		port->dvb.frontend = dvb_attach(s5h1409_attach,
						&hauppauge_hvr1800lp_config,
						&i2c_bus->i2c_adap);
		if (port->dvb.frontend != NULL) {
			dvb_attach(mt2131_attach, port->dvb.frontend,
				   &i2c_bus->i2c_adap,
				   &hauppauge_generic_tunerconfig, 0);
		}
		break;
	case CX23885_BOARD_DVICO_FUSIONHDTV_5_EXP:
		i2c_bus = &dev->i2c_bus[0];
		port->dvb.frontend = dvb_attach(lgdt330x_attach,
						&fusionhdtv_5_express,
						&i2c_bus->i2c_adap);
		if (port->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, port->dvb.frontend, 0x61,
				   &i2c_bus->i2c_adap, DVB_PLL_LG_TDVS_H06XF);
		}
		break;
	case CX23885_BOARD_HAUPPAUGE_HVR1500Q:
		i2c_bus = &dev->i2c_bus[1];
		port->dvb.frontend = dvb_attach(s5h1409_attach,
						&hauppauge_hvr1500q_config,
						&dev->i2c_bus[0].i2c_adap);
		if (port->dvb.frontend != NULL) {
			hauppauge_hvr1500q_tunerconfig.priv = i2c_bus;
			dvb_attach(xc5000_attach, port->dvb.frontend,
				&i2c_bus->i2c_adap,
				&hauppauge_hvr1500q_tunerconfig);
		}
		break;
	case CX23885_BOARD_HAUPPAUGE_HVR1500:
		i2c_bus = &dev->i2c_bus[1];
		port->dvb.frontend = dvb_attach(s5h1409_attach,
						&hauppauge_hvr1500_config,
						&dev->i2c_bus[0].i2c_adap);
		if (port->dvb.frontend != NULL) {
			struct dvb_frontend *fe;
			struct xc2028_config cfg = {
				.i2c_adap  = &i2c_bus->i2c_adap,
				.i2c_addr  = 0x61,
				.video_dev = port,
				.callback  = cx23885_hvr1500_xc3028_callback,
			};
			static struct xc2028_ctrl ctl = {
				.fname       = "xc3028-v27.fw",
				.max_len     = 64,
				.scode_table = OREN538,
			};

			fe = dvb_attach(xc2028_attach,
					port->dvb.frontend, &cfg);
			if (fe != NULL && fe->ops.tuner_ops.set_config != NULL)
				fe->ops.tuner_ops.set_config(fe, &ctl);
		}
		break;
	default:
		printk("%s: The frontend of your DVB/ATSC card isn't supported yet\n",
		       dev->name);
		break;
	}
	if (NULL == port->dvb.frontend) {
		printk("%s: frontend initialization failed\n", dev->name);
		return -1;
	}

	/* Put the analog decoder in standby to keep it quiet */
	cx23885_call_i2c_clients(i2c_bus, TUNER_SET_STANDBY, NULL);

	if (port->dvb.frontend->ops.analog_ops.standby)
		port->dvb.frontend->ops.analog_ops.standby(port->dvb.frontend);

	/* register everything */
	return videobuf_dvb_register(&port->dvb, THIS_MODULE, port,
				     &dev->pci->dev);
}

int cx23885_dvb_register(struct cx23885_tsport *port)
{
	struct cx23885_dev *dev = port->dev;
	int err;

	dprintk(1, "%s\n", __FUNCTION__);
	dprintk(1, " ->being probed by Card=%d Name=%s, PCI %02x:%02x\n",
		dev->board,
		dev->name,
		dev->pci_bus,
		dev->pci_slot);

	err = -ENODEV;

	/* dvb stuff */
	printk("%s: cx23885 based dvb card\n", dev->name);
	videobuf_queue_pci_init(&port->dvb.dvbq, &dvb_qops, dev->pci, &port->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_TOP,
			    sizeof(struct cx23885_buffer), port);
	err = dvb_register(port);
	if (err != 0)
		printk("%s() dvb_register failed err = %d\n", __FUNCTION__, err);

	return err;
}

int cx23885_dvb_unregister(struct cx23885_tsport *port)
{
	/* dvb */
	if(port->dvb.frontend)
		videobuf_dvb_unregister(&port->dvb);

	return 0;
}

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 * kate: eol "unix"; indent-width 3; remove-trailing-space on; replace-trailing-space-save on; tab-width 8; replace-tabs off; space-indent off; mixed-indent off
*/
