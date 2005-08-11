/*
 * tveeprom - eeprom decoder for tvcard configuration eeproms
 *
 * Data and decoding routines shamelessly borrowed from bttv-cards.c
 * eeprom access routine shamelessly borrowed from bttv-if.c
 * which are:

    Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
                           & Marcus Metzler (mocm@thp.uni-koeln.de)
    (c) 1999-2001 Gerd Knorr <kraxel@goldbach.in-berlin.de>

 * Adjustments to fit a more general model and all bugs:

 	Copyright (C) 2003 John Klar <linpvr at projectplasma.com>

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/videodev.h>
#include <linux/i2c.h>

#include <media/tuner.h>
#include <media/tveeprom.h>

MODULE_DESCRIPTION("i2c Hauppauge eeprom decoder driver");
MODULE_AUTHOR("John Klar");
MODULE_LICENSE("GPL");

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-2)");

#define STRM(array,i) (i < sizeof(array)/sizeof(char*) ? array[i] : "unknown")

#define dprintk(num, args...) \
	do { \
		if (debug >= num) \
			printk(KERN_INFO "tveeprom: " args); \
	} while (0)

#define TVEEPROM_KERN_ERR(args...) printk(KERN_ERR "tveeprom: " args);
#define TVEEPROM_KERN_INFO(args...) printk(KERN_INFO "tveeprom: " args);

/* ----------------------------------------------------------------------- */
/* some hauppauge specific stuff                                           */

static struct HAUPPAUGE_TUNER_FMT
{
	int	id;
	char *name;
}
hauppauge_tuner_fmt[] =
{
	{ 0x00000000, "unknown1" },
	{ 0x00000000, "unknown2" },
	{ 0x00000007, "PAL(B/G)" },
	{ 0x00001000, "NTSC(M)" },
	{ 0x00000010, "PAL(I)" },
	{ 0x00400000, "SECAM(L/L´)" },
	{ 0x00000e00, "PAL(D/K)" },
	{ 0x03000000, "ATSC Digital" },
};

/* This is the full list of possible tuners. Many thanks to Hauppauge for
   supplying this information. Note that many tuners where only used for
   testing and never made it to the outside world. So you will only see
   a subset in actual produced cards. */
static struct HAUPPAUGE_TUNER
{
	int  id;
	char *name;
}
hauppauge_tuner[] =
{
	/* 0-9 */
	{ TUNER_ABSENT,        "None" },
	{ TUNER_ABSENT,        "External" },
	{ TUNER_ABSENT,        "Unspecified" },
	{ TUNER_PHILIPS_PAL,   "Philips FI1216" },
	{ TUNER_PHILIPS_SECAM, "Philips FI1216MF" },
	{ TUNER_PHILIPS_NTSC,  "Philips FI1236" },
	{ TUNER_PHILIPS_PAL_I, "Philips FI1246" },
	{ TUNER_PHILIPS_PAL_DK,"Philips FI1256" },
	{ TUNER_PHILIPS_PAL,   "Philips FI1216 MK2" },
	{ TUNER_PHILIPS_SECAM, "Philips FI1216MF MK2" },
	/* 10-19 */
	{ TUNER_PHILIPS_NTSC,  "Philips FI1236 MK2" },
	{ TUNER_PHILIPS_PAL_I, "Philips FI1246 MK2" },
	{ TUNER_PHILIPS_PAL_DK,"Philips FI1256 MK2" },
	{ TUNER_TEMIC_NTSC,    "Temic 4032FY5" },
	{ TUNER_TEMIC_PAL,     "Temic 4002FH5" },
	{ TUNER_TEMIC_PAL_I,   "Temic 4062FY5" },
	{ TUNER_PHILIPS_PAL,   "Philips FR1216 MK2" },
	{ TUNER_PHILIPS_SECAM, "Philips FR1216MF MK2" },
	{ TUNER_PHILIPS_NTSC,  "Philips FR1236 MK2" },
	{ TUNER_PHILIPS_PAL_I, "Philips FR1246 MK2" },
	/* 20-29 */
	{ TUNER_PHILIPS_PAL_DK,"Philips FR1256 MK2" },
	{ TUNER_PHILIPS_PAL,   "Philips FM1216" },
	{ TUNER_PHILIPS_SECAM, "Philips FM1216MF" },
	{ TUNER_PHILIPS_NTSC,  "Philips FM1236" },
	{ TUNER_PHILIPS_PAL_I, "Philips FM1246" },
	{ TUNER_PHILIPS_PAL_DK,"Philips FM1256" },
	{ TUNER_TEMIC_4036FY5_NTSC, "Temic 4036FY5" },
	{ TUNER_ABSENT,        "Samsung TCPN9082D" },
	{ TUNER_ABSENT,        "Samsung TCPM9092P" },
	{ TUNER_TEMIC_4006FH5_PAL, "Temic 4006FH5" },
	/* 30-39 */
	{ TUNER_ABSENT,        "Samsung TCPN9085D" },
	{ TUNER_ABSENT,        "Samsung TCPB9085P" },
	{ TUNER_ABSENT,        "Samsung TCPL9091P" },
	{ TUNER_TEMIC_4039FR5_NTSC, "Temic 4039FR5" },
	{ TUNER_PHILIPS_FQ1216ME,   "Philips FQ1216 ME" },
	{ TUNER_TEMIC_4066FY5_PAL_I, "Temic 4066FY5" },
        { TUNER_PHILIPS_NTSC,        "Philips TD1536" },
        { TUNER_PHILIPS_NTSC,        "Philips TD1536D" },
	{ TUNER_PHILIPS_NTSC,  "Philips FMR1236" }, /* mono radio */
	{ TUNER_ABSENT,        "Philips FI1256MP" },
	/* 40-49 */
	{ TUNER_ABSENT,        "Samsung TCPQ9091P" },
	{ TUNER_TEMIC_4006FN5_MULTI_PAL, "Temic 4006FN5" },
	{ TUNER_TEMIC_4009FR5_PAL, "Temic 4009FR5" },
	{ TUNER_TEMIC_4046FM5,     "Temic 4046FM5" },
	{ TUNER_TEMIC_4009FN5_MULTI_PAL_FM, "Temic 4009FN5" },
	{ TUNER_ABSENT,        "Philips TD1536D FH 44"},
	{ TUNER_LG_NTSC_FM,    "LG TP18NSR01F"},
	{ TUNER_LG_PAL_FM,     "LG TP18PSB01D"},
	{ TUNER_LG_PAL,        "LG TP18PSB11D"},
	{ TUNER_LG_PAL_I_FM,   "LG TAPC-I001D"},
	/* 50-59 */
	{ TUNER_LG_PAL_I,      "LG TAPC-I701D"},
	{ TUNER_ABSENT,        "Temic 4042FI5"},
	{ TUNER_MICROTUNE_4049FM5, "Microtune 4049 FM5"},
	{ TUNER_ABSENT,        "LG TPI8NSR11F"},
	{ TUNER_ABSENT,        "Microtune 4049 FM5 Alt I2C"},
	{ TUNER_ABSENT,        "Philips FQ1216ME MK3"},
	{ TUNER_ABSENT,        "Philips FI1236 MK3"},
	{ TUNER_PHILIPS_FM1216ME_MK3, "Philips FM1216 ME MK3"},
	{ TUNER_ABSENT,        "Philips FM1236 MK3"},
	{ TUNER_ABSENT,        "Philips FM1216MP MK3"},
	/* 60-69 */
	{ TUNER_ABSENT,        "LG S001D MK3"},
	{ TUNER_ABSENT,        "LG M001D MK3"},
	{ TUNER_ABSENT,        "LG S701D MK3"},
	{ TUNER_ABSENT,        "LG M701D MK3"},
	{ TUNER_ABSENT,        "Temic 4146FM5"},
	{ TUNER_ABSENT,        "Temic 4136FY5"},
	{ TUNER_ABSENT,        "Temic 4106FH5"},
	{ TUNER_ABSENT,        "Philips FQ1216LMP MK3"},
	{ TUNER_LG_NTSC_TAPE,  "LG TAPE H001F MK3"},
	{ TUNER_ABSENT,        "LG TAPE H701F MK3"},
	/* 70-79 */
	{ TUNER_ABSENT,        "LG TALN H200T"},
	{ TUNER_ABSENT,        "LG TALN H250T"},
	{ TUNER_ABSENT,        "LG TALN M200T"},
	{ TUNER_ABSENT,        "LG TALN Z200T"},
	{ TUNER_ABSENT,        "LG TALN S200T"},
	{ TUNER_ABSENT,        "Thompson DTT7595"},
	{ TUNER_ABSENT,        "Thompson DTT7592"},
	{ TUNER_ABSENT,        "Silicon TDA8275C1 8290"},
	{ TUNER_ABSENT,        "Silicon TDA8275C1 8290 FM"},
	{ TUNER_ABSENT,        "Thompson DTT757"},
	/* 80-89 */
	{ TUNER_ABSENT,        "Philips FQ1216LME MK3"},
	{ TUNER_ABSENT,        "LG TAPC G701D"},
	{ TUNER_LG_NTSC_NEW_TAPC, "LG TAPC H791F"},
	{ TUNER_ABSENT,        "TCL 2002MB 3"},
	{ TUNER_ABSENT,        "TCL 2002MI 3"},
	{ TUNER_TCL_2002N,     "TCL 2002N 6A"},
	{ TUNER_ABSENT,        "Philips FQ1236 MK3"},
	{ TUNER_ABSENT,        "Samsung TCPN 2121P30A"},
	{ TUNER_ABSENT,        "Samsung TCPE 4121P30A"},
	{ TUNER_PHILIPS_FM1216ME_MK3, "TCL MFPE05 2"},
	/* 90-99 */
	{ TUNER_ABSENT,        "LG TALN H202T"},
	{ TUNER_PHILIPS_FQ1216AME_MK4, "Philips FQ1216AME MK4"},
	{ TUNER_PHILIPS_FQ1236A_MK4, "Philips FQ1236A MK4"},
	{ TUNER_ABSENT,        "Philips FQ1286A MK4"},
	{ TUNER_ABSENT,        "Philips FQ1216ME MK5"},
	{ TUNER_ABSENT,        "Philips FQ1236 MK5"},
	{ TUNER_ABSENT,        "Unspecified"},
	{ TUNER_LG_PAL_TAPE,   "LG PAL (TAPE Series)"},
};

static char *sndtype[] = {
	"None", "TEA6300", "TEA6320", "TDA9850", "MSP3400C", "MSP3410D",
	"MSP3415", "MSP3430", "MSP3438", "CS5331", "MSP3435", "MSP3440",
	"MSP3445", "MSP3411", "MSP3416", "MSP3425",

	"Type 0x10","Type 0x11","Type 0x12","Type 0x13",
	"Type 0x14","Type 0x15","Type 0x16","Type 0x17",
	"Type 0x18","MSP4418","Type 0x1a","MSP4448",
	"Type 0x1c","Type 0x1d","Type 0x1e","Type 0x1f",
};

static int hasRadioTuner(int tunerType)
{
        switch (tunerType) {
                case 18: //PNPEnv_TUNER_FR1236_MK2:
                case 23: //PNPEnv_TUNER_FM1236:
                case 38: //PNPEnv_TUNER_FMR1236:
                case 16: //PNPEnv_TUNER_FR1216_MK2:
                case 19: //PNPEnv_TUNER_FR1246_MK2:
                case 21: //PNPEnv_TUNER_FM1216:
                case 24: //PNPEnv_TUNER_FM1246:
                case 17: //PNPEnv_TUNER_FR1216MF_MK2:
                case 22: //PNPEnv_TUNER_FM1216MF:
                case 20: //PNPEnv_TUNER_FR1256_MK2:
                case 25: //PNPEnv_TUNER_FM1256:
                case 33: //PNPEnv_TUNER_4039FR5:
                case 42: //PNPEnv_TUNER_4009FR5:
                case 52: //PNPEnv_TUNER_4049FM5:
                case 54: //PNPEnv_TUNER_4049FM5_AltI2C:
                case 44: //PNPEnv_TUNER_4009FN5:
                case 31: //PNPEnv_TUNER_TCPB9085P:
                case 30: //PNPEnv_TUNER_TCPN9085D:
                case 46: //PNPEnv_TUNER_TP18NSR01F:
                case 47: //PNPEnv_TUNER_TP18PSB01D:
                case 49: //PNPEnv_TUNER_TAPC_I001D:
                case 60: //PNPEnv_TUNER_TAPE_S001D_MK3:
                case 57: //PNPEnv_TUNER_FM1216ME_MK3:
                case 59: //PNPEnv_TUNER_FM1216MP_MK3:
                case 58: //PNPEnv_TUNER_FM1236_MK3:
                case 68: //PNPEnv_TUNER_TAPE_H001F_MK3:
                case 61: //PNPEnv_TUNER_TAPE_M001D_MK3:
                case 78: //PNPEnv_TUNER_TDA8275C1_8290_FM:
                case 89: //PNPEnv_TUNER_TCL_MFPE05_2:
                case 92: //PNPEnv_TUNER_PHILIPS_FQ1236A_MK4:
                    return 1;
        }
        return 0;
}

void tveeprom_hauppauge_analog(struct tveeprom *tvee, unsigned char *eeprom_data)
{
	/* ----------------------------------------------
	** The hauppauge eeprom format is tagged
	**
	** if packet[0] == 0x84, then packet[0..1] == length
	** else length = packet[0] & 3f;
	** if packet[0] & f8 == f8, then EOD and packet[1] == checksum
	**
	** In our (ivtv) case we're interested in the following:
	** tuner type: tag [00].05 or [0a].01 (index into hauppauge_tuner)
	** tuner fmts: tag [00].04 or [0a].00 (bitmask index into hauppauge_tuner_fmt)
	** radio:      tag [00].{last} or [0e].00  (bitmask.  bit2=FM)
	** audio proc: tag [02].01 or [05].00 (lower nibble indexes lut?)

	** Fun info:
	** model:      tag [00].07-08 or [06].00-01
	** revision:   tag [00].09-0b or [06].04-06
	** serial#:    tag [01].05-07 or [04].04-06

	** # of inputs/outputs ???
	*/

	int i, j, len, done, beenhere, tag, tuner = 0, t_format = 0;
	char *t_name = NULL, *t_fmt_name = NULL;

	dprintk(1, "%s\n",__FUNCTION__);
	tvee->revision = done = len = beenhere = 0;
	for (i = 0; !done && i < 256; i += len) {
		dprintk(2, "processing pos = %02x (%02x, %02x)\n",
			i, eeprom_data[i], eeprom_data[i + 1]);

		if (eeprom_data[i] == 0x84) {
			len = eeprom_data[i + 1] + (eeprom_data[i + 2] << 8);
			i+=3;
		} else if ((eeprom_data[i] & 0xf0) == 0x70) {
			if ((eeprom_data[i] & 0x08)) {
				/* verify checksum! */
				done = 1;
				break;
			}
			len = eeprom_data[i] & 0x07;
			++i;
		} else {
			TVEEPROM_KERN_ERR("Encountered bad packet header [%02x]. "
				   "Corrupt or not a Hauppauge eeprom.\n", eeprom_data[i]);
			return;
		}

		dprintk(1, "%3d [%02x] ", len, eeprom_data[i]);
		for(j = 1; j < len; j++) {
			dprintk(1, "%02x ", eeprom_data[i + j]);
		}
		dprintk(1, "\n");

		/* process by tag */
		tag = eeprom_data[i];
		switch (tag) {
		case 0x00:
			tuner = eeprom_data[i+6];
			t_format = eeprom_data[i+5];
			tvee->has_radio = eeprom_data[i+len-1];
			tvee->model =
				eeprom_data[i+8] +
				(eeprom_data[i+9] << 8);
			tvee->revision = eeprom_data[i+10] +
				(eeprom_data[i+11] << 8) +
				(eeprom_data[i+12] << 16);
			break;
		case 0x01:
			tvee->serial_number =
				eeprom_data[i+6] +
				(eeprom_data[i+7] << 8) +
				(eeprom_data[i+8] << 16);
			break;
		case 0x02:
			tvee->audio_processor = eeprom_data[i+2] & 0x0f;
			break;
		case 0x04:
			tvee->serial_number =
				eeprom_data[i+5] +
				(eeprom_data[i+6] << 8) +
				(eeprom_data[i+7] << 16);
			break;
		case 0x05:
			tvee->audio_processor = eeprom_data[i+1] & 0x0f;
			break;
		case 0x06:
			tvee->model =
				eeprom_data[i+1] +
				(eeprom_data[i+2] << 8);
			tvee->revision = eeprom_data[i+5] +
				(eeprom_data[i+6] << 8) +
				(eeprom_data[i+7] << 16);
			break;
		case 0x0a:
			if(beenhere == 0) {
				tuner = eeprom_data[i+2];
				t_format = eeprom_data[i+1];
				beenhere = 1;
				break;
			} else {
				break;
			}
		case 0x0e:
			tvee->has_radio = eeprom_data[i+1];
			break;
		default:
			dprintk(1, "Not sure what to do with tag [%02x]\n", tag);
			/* dump the rest of the packet? */
		}

	}

	if (!done) {
		TVEEPROM_KERN_ERR("Ran out of data!\n");
		return;
	}

	if (tvee->revision != 0) {
		tvee->rev_str[0] = 32 + ((tvee->revision >> 18) & 0x3f);
		tvee->rev_str[1] = 32 + ((tvee->revision >> 12) & 0x3f);
		tvee->rev_str[2] = 32 + ((tvee->revision >>  6) & 0x3f);
		tvee->rev_str[3] = 32 + ( tvee->revision        & 0x3f);
		tvee->rev_str[4] = 0;
	}

        if (hasRadioTuner(tuner) && !tvee->has_radio) {
	    TVEEPROM_KERN_INFO("The eeprom says no radio is present, but the tuner type\n");
	    TVEEPROM_KERN_INFO("indicates otherwise. I will assume that radio is present.\n");
            tvee->has_radio = 1;
        }

	if (tuner < sizeof(hauppauge_tuner)/sizeof(struct HAUPPAUGE_TUNER)) {
		tvee->tuner_type = hauppauge_tuner[tuner].id;
		t_name = hauppauge_tuner[tuner].name;
	} else {
		t_name = "<unknown>";
	}

	tvee->tuner_formats = 0;
	t_fmt_name = "<none>";
	for (i = 0; i < 8; i++) {
		if (t_format & (1<<i)) {
			tvee->tuner_formats |= hauppauge_tuner_fmt[i].id;
			/* yuck */
			t_fmt_name = hauppauge_tuner_fmt[i].name;
		}
	}


	TVEEPROM_KERN_INFO("Hauppauge: model = %d, rev = %s, serial# = %d\n",
		   tvee->model,
		   tvee->rev_str,
		   tvee->serial_number);
	TVEEPROM_KERN_INFO("tuner = %s (idx = %d, type = %d)\n",
		   t_name,
		   tuner,
		   tvee->tuner_type);
	TVEEPROM_KERN_INFO("tuner fmt = %s (eeprom = 0x%02x, v4l2 = 0x%08x)\n",
		   t_fmt_name,
		   t_format,
		   tvee->tuner_formats);

	TVEEPROM_KERN_INFO("audio_processor = %s (type = %d)\n",
		   STRM(sndtype,tvee->audio_processor),
		   tvee->audio_processor);

}
EXPORT_SYMBOL(tveeprom_hauppauge_analog);

/* ----------------------------------------------------------------------- */
/* generic helper functions                                                */

int tveeprom_read(struct i2c_client *c, unsigned char *eedata, int len)
{
	unsigned char buf;
	int err;

	dprintk(1, "%s\n",__FUNCTION__);
	buf = 0;
	if (1 != (err = i2c_master_send(c,&buf,1))) {
		printk(KERN_INFO "tveeprom(%s): Huh, no eeprom present (err=%d)?\n",
		       c->name,err);
		return -1;
	}
	if (len != (err = i2c_master_recv(c,eedata,len))) {
		printk(KERN_WARNING "tveeprom(%s): i2c eeprom read error (err=%d)\n",
		       c->name,err);
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL(tveeprom_read);

#if 0
int tveeprom_dump(unsigned char *eedata, int len)
{
	int i;

	dprintk(1, "%s\n",__FUNCTION__);
	for (i = 0; i < len; i++) {
		if (0 == (i % 16))
			printk(KERN_INFO "tveeprom: %02x:",i);
		printk(" %02x",eedata[i]);
		if (15 == (i % 16))
			printk("\n");
	}
	return 0;
}
EXPORT_SYMBOL(tveeprom_dump);
#endif  /*  0  */

/* ----------------------------------------------------------------------- */
/* needed for ivtv.sf.net at the moment.  Should go away in the long       */
/* run, just call the exported tveeprom_* directly, there is no point in   */
/* using the indirect way via i2c_driver->command()                        */

#ifndef I2C_DRIVERID_TVEEPROM
# define I2C_DRIVERID_TVEEPROM I2C_DRIVERID_EXP2
#endif

static unsigned short normal_i2c[] = {
	0xa0 >> 1,
	I2C_CLIENT_END,
};

I2C_CLIENT_INSMOD;

static struct i2c_driver i2c_driver_tveeprom;

static int
tveeprom_command(struct i2c_client *client,
		 unsigned int       cmd,
		 void              *arg)
{
	struct tveeprom eeprom;
	u32 *eeprom_props = arg;
	u8 *buf;

	switch (cmd) {
	case 0:
		buf = kmalloc(256,GFP_KERNEL);
		memset(buf,0,256);
		tveeprom_read(client,buf,256);
		tveeprom_hauppauge_analog(&eeprom,buf);
		kfree(buf);
		eeprom_props[0] = eeprom.tuner_type;
		eeprom_props[1] = eeprom.tuner_formats;
		eeprom_props[2] = eeprom.model;
		eeprom_props[3] = eeprom.revision;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int
tveeprom_detect_client(struct i2c_adapter *adapter,
		       int                 address,
		       int                 kind)
{
	struct i2c_client *client;

	dprintk(1,"%s: id 0x%x @ 0x%x\n",__FUNCTION__,
	       adapter->id, address << 1);
	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (NULL == client)
		return -ENOMEM;
	memset(client, 0, sizeof(struct i2c_client));
	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver_tveeprom;
	client->flags = I2C_CLIENT_ALLOW_USE;
	snprintf(client->name, sizeof(client->name), "tveeprom");
        i2c_attach_client(client);
	return 0;
}

static int
tveeprom_attach_adapter (struct i2c_adapter *adapter)
{
	dprintk(1,"%s: id 0x%x\n",__FUNCTION__,adapter->id);
	if (adapter->id != (I2C_ALGO_BIT | I2C_HW_B_BT848))
		return 0;
	return i2c_probe(adapter, &addr_data, tveeprom_detect_client);
}

static int
tveeprom_detach_client (struct i2c_client *client)
{
	int err;

	err = i2c_detach_client(client);
	if (err < 0)
		return err;
	kfree(client);
	return 0;
}

static struct i2c_driver i2c_driver_tveeprom = {
	.owner          = THIS_MODULE,
	.name           = "tveeprom",
	.id             = I2C_DRIVERID_TVEEPROM,
	.flags          = I2C_DF_NOTIFY,
	.attach_adapter = tveeprom_attach_adapter,
	.detach_client  = tveeprom_detach_client,
	.command        = tveeprom_command,
};

static int __init tveeprom_init(void)
{
	return i2c_add_driver(&i2c_driver_tveeprom);
}

static void __exit tveeprom_exit(void)
{
	i2c_del_driver(&i2c_driver_tveeprom);
}

module_init(tveeprom_init);
module_exit(tveeprom_exit);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
