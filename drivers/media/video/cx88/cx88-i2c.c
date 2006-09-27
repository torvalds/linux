/*

    cx88-i2c.c  --  all the i2c code is here

    Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
			   & Marcus Metzler (mocm@thp.uni-koeln.de)
    (c) 2002 Yurij Sysoev <yurij@naturesoft.net>
    (c) 1999-2003 Gerd Knorr <kraxel@bytesex.org>

    (c) 2005 Mauro Carvalho Chehab <mchehab@infradead.org>
	- Multituner support and i2c address binding

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <asm/io.h>

#include "cx88.h"
#include <media/v4l2-common.h>

static unsigned int i2c_debug = 0;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug,"enable debug messages [i2c]");

static unsigned int i2c_scan = 0;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan,"scan i2c bus at insmod time");

static unsigned int i2c_udelay = 5;
module_param(i2c_udelay, int, 0644);
MODULE_PARM_DESC(i2c_udelay,"i2c delay at insmod time, in usecs "
		"(should be 5 or higher). Lower value means higher bus speed.");

#define dprintk(level,fmt, arg...)	if (i2c_debug >= level) \
	printk(KERN_DEBUG "%s: " fmt, core->name , ## arg)

/* ----------------------------------------------------------------------- */

static void cx8800_bit_setscl(void *data, int state)
{
	struct cx88_core *core = data;

	if (state)
		core->i2c_state |= 0x02;
	else
		core->i2c_state &= ~0x02;
	cx_write(MO_I2C, core->i2c_state);
	cx_read(MO_I2C);
}

static void cx8800_bit_setsda(void *data, int state)
{
	struct cx88_core *core = data;

	if (state)
		core->i2c_state |= 0x01;
	else
		core->i2c_state &= ~0x01;
	cx_write(MO_I2C, core->i2c_state);
	cx_read(MO_I2C);
}

static int cx8800_bit_getscl(void *data)
{
	struct cx88_core *core = data;
	u32 state;

	state = cx_read(MO_I2C);
	return state & 0x02 ? 1 : 0;
}

static int cx8800_bit_getsda(void *data)
{
	struct cx88_core *core = data;
	u32 state;

	state = cx_read(MO_I2C);
	return state & 0x01;
}

/* ----------------------------------------------------------------------- */

static int attach_inform(struct i2c_client *client)
{
	struct tuner_setup tun_setup;
	struct cx88_core *core = i2c_get_adapdata(client->adapter);

	dprintk(1, "%s i2c attach [addr=0x%x,client=%s]\n",
		client->driver->driver.name, client->addr, client->name);
	if (!client->driver->command)
		return 0;

	if (core->radio_type != UNSET) {
		if ((core->radio_addr==ADDR_UNSET)||(core->radio_addr==client->addr)) {
			tun_setup.mode_mask = T_RADIO;
			tun_setup.type = core->radio_type;
			tun_setup.addr = core->radio_addr;

			client->driver->command (client, TUNER_SET_TYPE_ADDR, &tun_setup);
		}
	}
	if (core->tuner_type != UNSET) {
		if ((core->tuner_addr==ADDR_UNSET)||(core->tuner_addr==client->addr)) {

			tun_setup.mode_mask = T_ANALOG_TV;
			tun_setup.type = core->tuner_type;
			tun_setup.addr = core->tuner_addr;

			client->driver->command (client,TUNER_SET_TYPE_ADDR, &tun_setup);
		}
	}

	if (core->tda9887_conf)
		client->driver->command(client, TDA9887_SET_CONFIG, &core->tda9887_conf);
	return 0;
}

static int detach_inform(struct i2c_client *client)
{
	struct cx88_core *core = i2c_get_adapdata(client->adapter);

	dprintk(1, "i2c detach [client=%s]\n", client->name);
	return 0;
}

void cx88_call_i2c_clients(struct cx88_core *core, unsigned int cmd, void *arg)
{
	if (0 != core->i2c_rc)
		return;

	if ( (core->dvbdev) && (core->dvbdev->dvb.frontend) ) {
		if (core->dvbdev->dvb.frontend->ops.i2c_gate_ctrl)
			core->dvbdev->dvb.frontend->ops.i2c_gate_ctrl(core->dvbdev->dvb.frontend, 1);

		i2c_clients_command(&core->i2c_adap, cmd, arg);

		if (core->dvbdev->dvb.frontend->ops.i2c_gate_ctrl)
			core->dvbdev->dvb.frontend->ops.i2c_gate_ctrl(core->dvbdev->dvb.frontend, 0);
	} else
		i2c_clients_command(&core->i2c_adap, cmd, arg);
}

static struct i2c_algo_bit_data cx8800_i2c_algo_template = {
	.setsda  = cx8800_bit_setsda,
	.setscl  = cx8800_bit_setscl,
	.getsda  = cx8800_bit_getsda,
	.getscl  = cx8800_bit_getscl,
	.udelay  = 16,
	.timeout = 200,
};

/* ----------------------------------------------------------------------- */

static struct i2c_adapter cx8800_i2c_adap_template = {
	.name              = "cx2388x",
	.owner             = THIS_MODULE,
	.id                = I2C_HW_B_CX2388x,
	.client_register   = attach_inform,
	.client_unregister = detach_inform,
};

static struct i2c_client cx8800_i2c_client_template = {
	.name	= "cx88xx internal",
};

static char *i2c_devs[128] = {
	[ 0x1c >> 1 ] = "lgdt330x",
	[ 0x86 >> 1 ] = "tda9887/cx22702",
	[ 0xa0 >> 1 ] = "eeprom",
	[ 0xc0 >> 1 ] = "tuner (analog)",
	[ 0xc2 >> 1 ] = "tuner (analog/dvb)",
};

static void do_i2c_scan(char *name, struct i2c_client *c)
{
	unsigned char buf;
	int i,rc;

	for (i = 0; i < 128; i++) {
		c->addr = i;
		rc = i2c_master_recv(c,&buf,0);
		if (rc < 0)
			continue;
		printk("%s: i2c scan: found device @ 0x%x  [%s]\n",
		       name, i << 1, i2c_devs[i] ? i2c_devs[i] : "???");
	}
}

/* init + register i2c algo-bit adapter */
int cx88_i2c_init(struct cx88_core *core, struct pci_dev *pci)
{
	/* Prevents usage of invalid delay values */
	if (i2c_udelay<5)
		i2c_udelay=5;
	cx8800_i2c_algo_template.udelay=i2c_udelay;

	memcpy(&core->i2c_adap, &cx8800_i2c_adap_template,
	       sizeof(core->i2c_adap));
	memcpy(&core->i2c_algo, &cx8800_i2c_algo_template,
	       sizeof(core->i2c_algo));
	memcpy(&core->i2c_client, &cx8800_i2c_client_template,
	       sizeof(core->i2c_client));

	if (core->tuner_type != TUNER_ABSENT)
		core->i2c_adap.class |= I2C_CLASS_TV_ANALOG;
	if (cx88_boards[core->board].mpeg & CX88_MPEG_DVB)
		core->i2c_adap.class |= I2C_CLASS_TV_DIGITAL;

	core->i2c_adap.dev.parent = &pci->dev;
	strlcpy(core->i2c_adap.name,core->name,sizeof(core->i2c_adap.name));
	core->i2c_algo.data = core;
	i2c_set_adapdata(&core->i2c_adap,core);
	core->i2c_adap.algo_data = &core->i2c_algo;
	core->i2c_client.adapter = &core->i2c_adap;

	cx8800_bit_setscl(core,1);
	cx8800_bit_setsda(core,1);

	core->i2c_rc = i2c_bit_add_bus(&core->i2c_adap);
	if (0 == core->i2c_rc) {
		dprintk(1, "i2c register ok\n");
		if (i2c_scan)
			do_i2c_scan(core->name,&core->i2c_client);
	} else
		printk("%s: i2c register FAILED\n", core->name);
	return core->i2c_rc;
}

/* ----------------------------------------------------------------------- */

EXPORT_SYMBOL(cx88_call_i2c_clients);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
