/* Driver for Bt832 CMOS Camera Video Processor
    i2c-addresses: 0x88 or 0x8a

  The BT832 interfaces to a Quartzsight Digital Camera (352x288, 25 or 30 fps)
  via a 9 pin connector ( 4-wire SDATA, 2-wire i2c, SCLK, VCC, GND).
  It outputs an 8-bit 4:2:2 YUV or YCrCb video signal which can be directly
  connected to bt848/bt878 GPIO pins on this purpose.
  (see: VLSI Vision Ltd. www.vvl.co.uk for camera datasheets)
  
  Supported Cards:
  -  Pixelview Rev.4E: 0x8a
		GPIO 0x400000 toggles Bt832 RESET, and the chip changes to i2c 0x88 !

  (c) Gunther Mayer, 2002

  STATUS:
  - detect chip and hexdump
  - reset chip and leave low power mode
  - detect camera present

  TODO:
  - make it work (find correct setup for Bt832 and Bt878)
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/videodev.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "id.h"
#include "audiochip.h"
#include "bttv.h"
#include "bt832.h"

MODULE_LICENSE("GPL");

/* Addresses to scan */
static unsigned short normal_i2c[] = {I2C_CLIENT_END};
static unsigned short normal_i2c_range[] = {I2C_BT832_ALT1>>1,I2C_BT832_ALT2>>1,I2C_CLIENT_END};
I2C_CLIENT_INSMOD;

/* ---------------------------------------------------------------------- */

#define dprintk     if (debug) printk

static int bt832_detach(struct i2c_client *client);


static struct i2c_driver driver;
static struct i2c_client client_template;

struct bt832 {
        struct i2c_client client;
};

int bt832_hexdump(struct i2c_client *i2c_client_s, unsigned char *buf)
{
	int i,rc;
	buf[0]=0x80; // start at register 0 with auto-increment
        if (1 != (rc = i2c_master_send(i2c_client_s,buf,1)))
                printk("bt832: i2c i/o error: rc == %d (should be 1)\n",rc);

        for(i=0;i<65;i++)
                buf[i]=0;
        if (65 != (rc=i2c_master_recv(i2c_client_s,buf,65)))
                printk("bt832: i2c i/o error: rc == %d (should be 65)\n",rc);

        // Note: On READ the first byte is the current index
        //  (e.g. 0x80, what we just wrote)

        if(1) {
                int i;
                printk("BT832 hexdump:\n");
                for(i=1;i<65;i++) {
			if(i!=1) {
			  if(((i-1)%8)==0) printk(" ");
                          if(((i-1)%16)==0) printk("\n");
			}
                        printk(" %02x",buf[i]);
                }
                printk("\n");
        }
	return 0;
}

// Return: 1 (is a bt832), 0 (No bt832 here)
int bt832_init(struct i2c_client *i2c_client_s)
{
	unsigned char *buf;
	int rc;

	buf=kmalloc(65,GFP_KERNEL);
	bt832_hexdump(i2c_client_s,buf);
	
	if(buf[0x40] != 0x31) {
		printk("bt832: this i2c chip is no bt832 (id=%02x). Detaching.\n",buf[0x40]);
		kfree(buf);
		return 0;
	}

        printk("Write 0 tp VPSTATUS\n");
        buf[0]=BT832_VP_STATUS; // Reg.52
        buf[1]= 0x00;
        if (2 != (rc = i2c_master_send(i2c_client_s,buf,2)))
                printk("bt832: i2c i/o error VPS: rc == %d (should be 2)\n",rc);

        bt832_hexdump(i2c_client_s,buf);


	// Leave low power mode:
	printk("Bt832: leave low power mode.\n");
	buf[0]=BT832_CAM_SETUP0; //0x39 57
	buf[1]=0x08;
	if (2 != (rc = i2c_master_send(i2c_client_s,buf,2)))
                printk("bt832: i2c i/o error LLPM: rc == %d (should be 2)\n",rc);

        bt832_hexdump(i2c_client_s,buf);

	printk("Write 0 tp VPSTATUS\n");
        buf[0]=BT832_VP_STATUS; // Reg.52
        buf[1]= 0x00;
        if (2 != (rc = i2c_master_send(i2c_client_s,buf,2)))
                printk("bt832: i2c i/o error VPS: rc == %d (should be 2)\n",rc);

        bt832_hexdump(i2c_client_s,buf);


	// Enable Output
	printk("Enable Output\n");
	buf[0]=BT832_VP_CONTROL1; // Reg.40
	buf[1]= 0x27 & (~0x01); // Default | !skip
	if (2 != (rc = i2c_master_send(i2c_client_s,buf,2)))
                printk("bt832: i2c i/o error EO: rc == %d (should be 2)\n",rc);
	
        bt832_hexdump(i2c_client_s,buf);

#if 0
	// Full 30/25 Frame rate
	printk("Full 30/25 Frame rate\n");
	buf[0]=BT832_VP_CONTROL0; // Reg.39
        buf[1]= 0x00;
        if (2 != (rc = i2c_master_send(i2c_client_s,buf,2)))
                printk("bt832: i2c i/o error FFR: rc == %d (should be 2)\n",rc);

        bt832_hexdump(i2c_client_s,buf);
#endif

#if 1
	// for testing (even works when no camera attached)
	printk("bt832: *** Generate NTSC M Bars *****\n");
	buf[0]=BT832_VP_TESTCONTROL0; // Reg. 42
	buf[1]=3; // Generate NTSC System M bars, Generate Frame timing internally
        if (2 != (rc = i2c_master_send(i2c_client_s,buf,2)))
                printk("bt832: i2c i/o error MBAR: rc == %d (should be 2)\n",rc);
#endif

	printk("Bt832: Camera Present: %s\n",
		(buf[1+BT832_CAM_STATUS] & BT832_56_CAMERA_PRESENT) ? "yes":"no");

        bt832_hexdump(i2c_client_s,buf);
	kfree(buf);
	return 1;
}



static int bt832_attach(struct i2c_adapter *adap, int addr,
			  unsigned short flags, int kind)
{
	struct bt832 *t;

	printk("bt832_attach\n");

        client_template.adapter = adap;
        client_template.addr    = addr;

        printk("bt832: chip found @ 0x%x\n", addr<<1);

        if (NULL == (t = kmalloc(sizeof(*t), GFP_KERNEL)))
                return -ENOMEM;
	memset(t,0,sizeof(*t));
	t->client = client_template;
        t->client.data = t;
        i2c_attach_client(&t->client);

	if(! bt832_init(&t->client)) {
		bt832_detach(&t->client);
		return -1;
	}
        
	return 0;
}

static int bt832_probe(struct i2c_adapter *adap)
{
	if (adap->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adap, &addr_data, bt832_attach);
	return 0;
}

static int bt832_detach(struct i2c_client *client)
{
	struct bt832 *t = (struct bt832*)client->data;

	printk("bt832: detach.\n");
	i2c_detach_client(client);
	kfree(t);
	return 0;
}

static int
bt832_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct bt832 *t = (struct bt832*)client->data;

	printk("bt832: command %x\n",cmd);

        switch (cmd) {
		case BT832_HEXDUMP: {
			unsigned char *buf;
			buf=kmalloc(65,GFP_KERNEL);
			bt832_hexdump(&t->client,buf);
			kfree(buf);
		}
		break;
		case BT832_REATTACH:
			printk("bt832: re-attach\n");
			i2c_del_driver(&driver);
			i2c_add_driver(&driver);
		break;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver driver = {
	.owner          = THIS_MODULE,
        .name           = "i2c bt832 driver",
        .id             = -1, /* FIXME */
        .flags          = I2C_DF_NOTIFY,
        .attach_adapter = bt832_probe,
        .detach_client  = bt832_detach,
        .command        = bt832_command,
};
static struct i2c_client client_template =
{
        .name   = "bt832",
	.flags  = I2C_CLIENT_ALLOW_USE,
        .driver = &driver,
};


int bt832_init_module(void)
{
	i2c_add_driver(&driver);
	return 0;
}

static void bt832_cleanup_module(void)
{
	i2c_del_driver(&driver);
}

module_init(bt832_init_module);
module_exit(bt832_cleanup_module);

