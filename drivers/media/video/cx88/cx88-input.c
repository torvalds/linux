/*
 *
 * Device driver for GPIO attached remote control interfaces
 * on Conexant 2388x based TV/DVB cards.
 *
 * Copyright (c) 2003 Pavel Machek
 * Copyright (c) 2004 Gerd Knorr
 * Copyright (c) 2004, 2005 Chris Pascoe
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/init.h>
#include <linux/hrtimer.h>
#include <linux/input.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "cx88.h"
#include <media/ir-core.h>
#include <media/ir-common.h>

#define MODULE_NAME "cx88xx"

/* ---------------------------------------------------------------------- */

struct cx88_IR {
	struct cx88_core *core;
	struct input_dev *input;
	struct ir_dev_props props;
	u64 ir_type;

	int users;

	char name[32];
	char phys[32];

	/* sample from gpio pin 16 */
	u32 sampling;
	u32 samples[16];
	int scount;

	/* poll external decoder */
	int polling;
	struct hrtimer timer;
	u32 gpio_addr;
	u32 last_gpio;
	u32 mask_keycode;
	u32 mask_keydown;
	u32 mask_keyup;
};

static int ir_debug;
module_param(ir_debug, int, 0644);	/* debug level [IR] */
MODULE_PARM_DESC(ir_debug, "enable debug messages [IR]");

#define ir_dprintk(fmt, arg...)	if (ir_debug) \
	printk(KERN_DEBUG "%s IR: " fmt , ir->core->name , ##arg)

/* ---------------------------------------------------------------------- */

static void cx88_ir_handle_key(struct cx88_IR *ir)
{
	struct cx88_core *core = ir->core;
	u32 gpio, data, auxgpio;

	/* read gpio value */
	gpio = cx_read(ir->gpio_addr);
	switch (core->boardnr) {
	case CX88_BOARD_NPGTECH_REALTV_TOP10FM:
		/* This board apparently uses a combination of 2 GPIO
		   to represent the keys. Additionally, the second GPIO
		   can be used for parity.

		   Example:

		   for key "5"
			gpio = 0x758, auxgpio = 0xe5 or 0xf5
		   for key "Power"
			gpio = 0x758, auxgpio = 0xed or 0xfd
		 */

		auxgpio = cx_read(MO_GP1_IO);
		/* Take out the parity part */
		gpio=(gpio & 0x7fd) + (auxgpio & 0xef);
		break;
	case CX88_BOARD_WINFAST_DTV1000:
	case CX88_BOARD_WINFAST_DTV1800H:
	case CX88_BOARD_WINFAST_TV2000_XP_GLOBAL:
		gpio = (gpio & 0x6ff) | ((cx_read(MO_GP1_IO) << 8) & 0x900);
		auxgpio = gpio;
		break;
	default:
		auxgpio = gpio;
	}
	if (ir->polling) {
		if (ir->last_gpio == auxgpio)
			return;
		ir->last_gpio = auxgpio;
	}

	/* extract data */
	data = ir_extract_bits(gpio, ir->mask_keycode);
	ir_dprintk("irq gpio=0x%x code=%d | %s%s%s\n",
		   gpio, data,
		   ir->polling ? "poll" : "irq",
		   (gpio & ir->mask_keydown) ? " down" : "",
		   (gpio & ir->mask_keyup) ? " up" : "");

	if (ir->core->boardnr == CX88_BOARD_NORWOOD_MICRO) {
		u32 gpio_key = cx_read(MO_GP0_IO);

		data = (data << 4) | ((gpio_key & 0xf0) >> 4);

		ir_keydown(ir->input, data, 0);

	} else if (ir->mask_keydown) {
		/* bit set on keydown */
		if (gpio & ir->mask_keydown)
			ir_keydown(ir->input, data, 0);

	} else if (ir->mask_keyup) {
		/* bit cleared on keydown */
		if (0 == (gpio & ir->mask_keyup))
			ir_keydown(ir->input, data, 0);

	} else {
		/* can't distinguish keydown/up :-/ */
		ir_keydown(ir->input, data, 0);
	}
}

static enum hrtimer_restart cx88_ir_work(struct hrtimer *timer)
{
	unsigned long missed;
	struct cx88_IR *ir = container_of(timer, struct cx88_IR, timer);

	cx88_ir_handle_key(ir);
	missed = hrtimer_forward_now(&ir->timer,
				     ktime_set(0, ir->polling * 1000000));
	if (missed > 1)
		ir_dprintk("Missed ticks %ld\n", missed - 1);

	return HRTIMER_RESTART;
}

static int __cx88_ir_start(void *priv)
{
	struct cx88_core *core = priv;
	struct cx88_IR *ir;

	if (!core || !core->ir)
		return -EINVAL;

	ir = core->ir;

	if (ir->polling) {
		hrtimer_init(&ir->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ir->timer.function = cx88_ir_work;
		hrtimer_start(&ir->timer,
			      ktime_set(0, ir->polling * 1000000),
			      HRTIMER_MODE_REL);
	}
	if (ir->sampling) {
		core->pci_irqmask |= PCI_INT_IR_SMPINT;
		cx_write(MO_DDS_IO, 0xa80a80);	/* 4 kHz sample rate */
		cx_write(MO_DDSCFG_IO, 0x5);	/* enable */
	}
	return 0;
}

static void __cx88_ir_stop(void *priv)
{
	struct cx88_core *core = priv;
	struct cx88_IR *ir;

	if (!core || !core->ir)
		return;

	ir = core->ir;
	if (ir->sampling) {
		cx_write(MO_DDSCFG_IO, 0x0);
		core->pci_irqmask &= ~PCI_INT_IR_SMPINT;
	}

	if (ir->polling)
		hrtimer_cancel(&ir->timer);
}

int cx88_ir_start(struct cx88_core *core)
{
	if (core->ir->users)
		return __cx88_ir_start(core);

	return 0;
}

void cx88_ir_stop(struct cx88_core *core)
{
	if (core->ir->users)
		__cx88_ir_stop(core);
}

static int cx88_ir_open(void *priv)
{
	struct cx88_core *core = priv;

	core->ir->users++;
	return __cx88_ir_start(core);
}

static void cx88_ir_close(void *priv)
{
	struct cx88_core *core = priv;

	core->ir->users--;
	if (!core->ir->users)
		__cx88_ir_stop(core);
}

/* ---------------------------------------------------------------------- */

int cx88_ir_init(struct cx88_core *core, struct pci_dev *pci)
{
	struct cx88_IR *ir;
	struct input_dev *input_dev;
	char *ir_codes = NULL;
	u64 ir_type = IR_TYPE_OTHER;
	int err = -ENOMEM;
	u32 hardware_mask = 0;	/* For devices with a hardware mask, when
				 * used with a full-code IR table
				 */

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ir || !input_dev)
		goto err_out_free;

	ir->input = input_dev;

	/* detect & configure */
	switch (core->boardnr) {
	case CX88_BOARD_DNTV_LIVE_DVB_T:
	case CX88_BOARD_KWORLD_DVB_T:
	case CX88_BOARD_KWORLD_DVB_T_CX22702:
		ir_codes = RC_MAP_DNTV_LIVE_DVB_T;
		ir->gpio_addr = MO_GP1_IO;
		ir->mask_keycode = 0x1f;
		ir->mask_keyup = 0x60;
		ir->polling = 50; /* ms */
		break;
	case CX88_BOARD_TERRATEC_CINERGY_1400_DVB_T1:
		ir_codes = RC_MAP_CINERGY_1400;
		ir_type = IR_TYPE_NEC;
		ir->sampling = 0xeb04; /* address */
		break;
	case CX88_BOARD_HAUPPAUGE:
	case CX88_BOARD_HAUPPAUGE_DVB_T1:
	case CX88_BOARD_HAUPPAUGE_NOVASE2_S1:
	case CX88_BOARD_HAUPPAUGE_NOVASPLUS_S1:
	case CX88_BOARD_HAUPPAUGE_HVR1100:
	case CX88_BOARD_HAUPPAUGE_HVR3000:
	case CX88_BOARD_HAUPPAUGE_HVR4000:
	case CX88_BOARD_HAUPPAUGE_HVR4000LITE:
	case CX88_BOARD_PCHDTV_HD3000:
	case CX88_BOARD_PCHDTV_HD5500:
	case CX88_BOARD_HAUPPAUGE_IRONLY:
		ir_codes = RC_MAP_HAUPPAUGE_NEW;
		ir_type = IR_TYPE_RC5;
		ir->sampling = 1;
		break;
	case CX88_BOARD_WINFAST_DTV2000H:
	case CX88_BOARD_WINFAST_DTV2000H_J:
	case CX88_BOARD_WINFAST_DTV1800H:
		ir_codes = RC_MAP_WINFAST;
		ir->gpio_addr = MO_GP0_IO;
		ir->mask_keycode = 0x8f8;
		ir->mask_keyup = 0x100;
		ir->polling = 50; /* ms */
		break;
	case CX88_BOARD_WINFAST2000XP_EXPERT:
	case CX88_BOARD_WINFAST_DTV1000:
	case CX88_BOARD_WINFAST_TV2000_XP_GLOBAL:
		ir_codes = RC_MAP_WINFAST;
		ir->gpio_addr = MO_GP0_IO;
		ir->mask_keycode = 0x8f8;
		ir->mask_keyup = 0x100;
		ir->polling = 1; /* ms */
		break;
	case CX88_BOARD_IODATA_GVBCTV7E:
		ir_codes = RC_MAP_IODATA_BCTV7E;
		ir->gpio_addr = MO_GP0_IO;
		ir->mask_keycode = 0xfd;
		ir->mask_keydown = 0x02;
		ir->polling = 5; /* ms */
		break;
	case CX88_BOARD_PROLINK_PLAYTVPVR:
	case CX88_BOARD_PIXELVIEW_PLAYTV_ULTRA_PRO:
		/*
		 * It seems that this hardware is paired with NEC extended
		 * address 0x866b. So, unfortunately, its usage with other
		 * IR's with different address won't work. Still, there are
		 * other IR's from the same manufacturer that works, like the
		 * 002-T mini RC, provided with newer PV hardware
		 */
		ir_codes = RC_MAP_PIXELVIEW_MK12;
		ir->gpio_addr = MO_GP1_IO;
		ir->mask_keyup = 0x80;
		ir->polling = 10; /* ms */
		hardware_mask = 0x3f;	/* Hardware returns only 6 bits from command part */
		break;
	case CX88_BOARD_PROLINK_PV_8000GT:
	case CX88_BOARD_PROLINK_PV_GLOBAL_XTREME:
		ir_codes = RC_MAP_PIXELVIEW_NEW;
		ir->gpio_addr = MO_GP1_IO;
		ir->mask_keycode = 0x3f;
		ir->mask_keyup = 0x80;
		ir->polling = 1; /* ms */
		break;
	case CX88_BOARD_KWORLD_LTV883:
		ir_codes = RC_MAP_PIXELVIEW;
		ir->gpio_addr = MO_GP1_IO;
		ir->mask_keycode = 0x1f;
		ir->mask_keyup = 0x60;
		ir->polling = 1; /* ms */
		break;
	case CX88_BOARD_ADSTECH_DVB_T_PCI:
		ir_codes = RC_MAP_ADSTECH_DVB_T_PCI;
		ir->gpio_addr = MO_GP1_IO;
		ir->mask_keycode = 0xbf;
		ir->mask_keyup = 0x40;
		ir->polling = 50; /* ms */
		break;
	case CX88_BOARD_MSI_TVANYWHERE_MASTER:
		ir_codes = RC_MAP_MSI_TVANYWHERE;
		ir->gpio_addr = MO_GP1_IO;
		ir->mask_keycode = 0x1f;
		ir->mask_keyup = 0x40;
		ir->polling = 1; /* ms */
		break;
	case CX88_BOARD_AVERTV_303:
	case CX88_BOARD_AVERTV_STUDIO_303:
		ir_codes         = RC_MAP_AVERTV_303;
		ir->gpio_addr    = MO_GP2_IO;
		ir->mask_keycode = 0xfb;
		ir->mask_keydown = 0x02;
		ir->polling      = 50; /* ms */
		break;
	case CX88_BOARD_OMICOM_SS4_PCI:
	case CX88_BOARD_SATTRADE_ST4200:
	case CX88_BOARD_TBS_8920:
	case CX88_BOARD_TBS_8910:
	case CX88_BOARD_PROF_7300:
	case CX88_BOARD_PROF_7301:
	case CX88_BOARD_PROF_6200:
		ir_codes = RC_MAP_TBS_NEC;
		ir_type = IR_TYPE_NEC;
		ir->sampling = 0xff00; /* address */
		break;
	case CX88_BOARD_TEVII_S460:
	case CX88_BOARD_TEVII_S420:
		ir_codes = RC_MAP_TEVII_NEC;
		ir_type = IR_TYPE_NEC;
		ir->sampling = 0xff00; /* address */
		break;
	case CX88_BOARD_DNTV_LIVE_DVB_T_PRO:
		ir_codes         = RC_MAP_DNTV_LIVE_DVBT_PRO;
		ir_type          = IR_TYPE_NEC;
		ir->sampling     = 0xff00; /* address */
		break;
	case CX88_BOARD_NORWOOD_MICRO:
		ir_codes         = RC_MAP_NORWOOD;
		ir->gpio_addr    = MO_GP1_IO;
		ir->mask_keycode = 0x0e;
		ir->mask_keyup   = 0x80;
		ir->polling      = 50; /* ms */
		break;
	case CX88_BOARD_NPGTECH_REALTV_TOP10FM:
		ir_codes         = RC_MAP_NPGTECH;
		ir->gpio_addr    = MO_GP0_IO;
		ir->mask_keycode = 0xfa;
		ir->polling      = 50; /* ms */
		break;
	case CX88_BOARD_PINNACLE_PCTV_HD_800i:
		ir_codes         = RC_MAP_PINNACLE_PCTV_HD;
		ir_type          = IR_TYPE_RC5;
		ir->sampling     = 1;
		break;
	case CX88_BOARD_POWERCOLOR_REAL_ANGEL:
		ir_codes         = RC_MAP_POWERCOLOR_REAL_ANGEL;
		ir->gpio_addr    = MO_GP2_IO;
		ir->mask_keycode = 0x7e;
		ir->polling      = 100; /* ms */
		break;
	case CX88_BOARD_TWINHAN_VP1027_DVBS:
		ir_codes         = RC_MAP_TWINHAN_VP1027_DVBS;
		ir_type          = IR_TYPE_NEC;
		ir->sampling     = 0xff00; /* address */
		break;
	}

	if (NULL == ir_codes) {
		err = -ENODEV;
		goto err_out_free;
	}

	/*
	 * The usage of mask_keycode were very convenient, due to several
	 * reasons. Among others, the scancode tables were using the scancode
	 * as the index elements. So, the less bits it was used, the smaller
	 * the table were stored. After the input changes, the better is to use
	 * the full scancodes, since it allows replacing the IR remote by
	 * another one. Unfortunately, there are still some hardware, like
	 * Pixelview Ultra Pro, where only part of the scancode is sent via
	 * GPIO. So, there's no way to get the full scancode. Due to that,
	 * hardware_mask were introduced here: it represents those hardware
	 * that has such limits.
	 */
	if (hardware_mask && !ir->mask_keycode)
		ir->mask_keycode = hardware_mask;

	/* init input device */
	snprintf(ir->name, sizeof(ir->name), "cx88 IR (%s)", core->board.name);
	snprintf(ir->phys, sizeof(ir->phys), "pci-%s/ir0", pci_name(pci));

	ir->ir_type = ir_type;

	input_dev->name = ir->name;
	input_dev->phys = ir->phys;
	input_dev->id.bustype = BUS_PCI;
	input_dev->id.version = 1;
	if (pci->subsystem_vendor) {
		input_dev->id.vendor = pci->subsystem_vendor;
		input_dev->id.product = pci->subsystem_device;
	} else {
		input_dev->id.vendor = pci->vendor;
		input_dev->id.product = pci->device;
	}
	input_dev->dev.parent = &pci->dev;
	/* record handles to ourself */
	ir->core = core;
	core->ir = ir;

	ir->props.priv = core;
	ir->props.open = cx88_ir_open;
	ir->props.close = cx88_ir_close;
	ir->props.scanmask = hardware_mask;

	/* all done */
	err = ir_input_register(ir->input, ir_codes, &ir->props, MODULE_NAME);
	if (err)
		goto err_out_free;

	return 0;

 err_out_free:
	core->ir = NULL;
	kfree(ir);
	return err;
}

int cx88_ir_fini(struct cx88_core *core)
{
	struct cx88_IR *ir = core->ir;

	/* skip detach on non attached boards */
	if (NULL == ir)
		return 0;

	cx88_ir_stop(core);
	ir_input_unregister(ir->input);
	kfree(ir);

	/* done */
	core->ir = NULL;
	return 0;
}

/* ---------------------------------------------------------------------- */

void cx88_ir_irq(struct cx88_core *core)
{
	struct cx88_IR *ir = core->ir;
	u32 samples, ircode;
	int i, start, range, toggle, dev, code;

	if (NULL == ir)
		return;
	if (!ir->sampling)
		return;

	samples = cx_read(MO_SAMPLE_IO);
	if (0 != samples && 0xffffffff != samples) {
		/* record sample data */
		if (ir->scount < ARRAY_SIZE(ir->samples))
			ir->samples[ir->scount++] = samples;
		return;
	}
	if (!ir->scount) {
		/* nothing to sample */
		return;
	}

	/* have a complete sample */
	if (ir->scount < ARRAY_SIZE(ir->samples))
		ir->samples[ir->scount++] = samples;
	for (i = 0; i < ir->scount; i++)
		ir->samples[i] = ~ir->samples[i];
	if (ir_debug)
		ir_dump_samples(ir->samples, ir->scount);

	/* decode it */
	switch (core->boardnr) {
	case CX88_BOARD_TEVII_S460:
	case CX88_BOARD_TEVII_S420:
	case CX88_BOARD_TERRATEC_CINERGY_1400_DVB_T1:
	case CX88_BOARD_DNTV_LIVE_DVB_T_PRO:
	case CX88_BOARD_OMICOM_SS4_PCI:
	case CX88_BOARD_SATTRADE_ST4200:
	case CX88_BOARD_TBS_8920:
	case CX88_BOARD_TBS_8910:
	case CX88_BOARD_PROF_7300:
	case CX88_BOARD_PROF_7301:
	case CX88_BOARD_PROF_6200:
	case CX88_BOARD_TWINHAN_VP1027_DVBS:
		ircode = ir_decode_pulsedistance(ir->samples, ir->scount, 1, 4);

		if (ircode == 0xffffffff) { /* decoding error */
			ir_dprintk("pulse distance decoding error\n");
			break;
		}

		ir_dprintk("pulse distance decoded: %x\n", ircode);

		if (ircode == 0) { /* key still pressed */
			ir_dprintk("pulse distance decoded repeat code\n");
			ir_repeat(ir->input);
			break;
		}

		if ((ircode & 0xffff) != (ir->sampling & 0xffff)) { /* wrong address */
			ir_dprintk("pulse distance decoded wrong address\n");
			break;
		}

		if (((~ircode >> 24) & 0xff) != ((ircode >> 16) & 0xff)) { /* wrong checksum */
			ir_dprintk("pulse distance decoded wrong check sum\n");
			break;
		}

		ir_dprintk("Key Code: %x\n", (ircode >> 16) & 0xff);
		ir_keydown(ir->input, (ircode >> 16) & 0xff, 0);
		break;
	case CX88_BOARD_HAUPPAUGE:
	case CX88_BOARD_HAUPPAUGE_DVB_T1:
	case CX88_BOARD_HAUPPAUGE_NOVASE2_S1:
	case CX88_BOARD_HAUPPAUGE_NOVASPLUS_S1:
	case CX88_BOARD_HAUPPAUGE_HVR1100:
	case CX88_BOARD_HAUPPAUGE_HVR3000:
	case CX88_BOARD_HAUPPAUGE_HVR4000:
	case CX88_BOARD_HAUPPAUGE_HVR4000LITE:
	case CX88_BOARD_PCHDTV_HD3000:
	case CX88_BOARD_PCHDTV_HD5500:
	case CX88_BOARD_HAUPPAUGE_IRONLY:
		ircode = ir_decode_biphase(ir->samples, ir->scount, 5, 7);
		ir_dprintk("biphase decoded: %x\n", ircode);
		/*
		 * RC5 has an extension bit which adds a new range
		 * of available codes, this is detected here. Also
		 * hauppauge remotes (black/silver) always use
		 * specific device ids. If we do not filter the
		 * device ids then messages destined for devices
		 * such as TVs (id=0) will get through to the
		 * device causing mis-fired events.
		 */
		/* split rc5 data block ... */
		start = (ircode & 0x2000) >> 13;
		range = (ircode & 0x1000) >> 12;
		toggle= (ircode & 0x0800) >> 11;
		dev   = (ircode & 0x07c0) >> 6;
		code  = (ircode & 0x003f) | ((range << 6) ^ 0x0040);
		if( start != 1)
			/* no key pressed */
			break;
		if ( dev != 0x1e && dev != 0x1f )
			/* not a hauppauge remote */
			break;
		ir_keydown(ir->input, code, toggle);
		break;
	case CX88_BOARD_PINNACLE_PCTV_HD_800i:
		ircode = ir_decode_biphase(ir->samples, ir->scount, 5, 7);
		ir_dprintk("biphase decoded: %x\n", ircode);
		if ((ircode & 0xfffff000) != 0x3000)
			break;
		/* Note: bit 0x800 being the toggle is assumed, not checked
		   with real hardware  */
		ir_keydown(ir->input, ircode & 0x3f, ircode & 0x0800 ? 1 : 0);
		break;
	}

	ir->scount = 0;
	return;
}


void cx88_i2c_init_ir(struct cx88_core *core)
{
	struct i2c_board_info info;
	const unsigned short addr_list[] = {
		0x18, 0x6b, 0x71,
		I2C_CLIENT_END
	};
	const unsigned short *addrp;
	/* Instantiate the IR receiver device, if present */
	if (0 != core->i2c_rc)
		return;

	memset(&info, 0, sizeof(struct i2c_board_info));
	strlcpy(info.type, "ir_video", I2C_NAME_SIZE);

	/*
	 * We can't call i2c_new_probed_device() because it uses
	 * quick writes for probing and at least some RC receiver
	 * devices only reply to reads.
	 * Also, Hauppauge XVR needs to be specified, as address 0x71
	 * conflicts with another remote type used with saa7134
	 */
	for (addrp = addr_list; *addrp != I2C_CLIENT_END; addrp++) {
		info.platform_data = NULL;
		memset(&core->init_data, 0, sizeof(core->init_data));

		if (*addrp == 0x71) {
			/* Hauppauge XVR */
			core->init_data.name = "cx88 Hauppauge XVR remote";
			core->init_data.ir_codes = RC_MAP_HAUPPAUGE_NEW;
			core->init_data.type = IR_TYPE_RC5;
			core->init_data.internal_get_key_func = IR_KBD_GET_KEY_HAUP_XVR;

			info.platform_data = &core->init_data;
		}
		if (i2c_smbus_xfer(&core->i2c_adap, *addrp, 0,
					I2C_SMBUS_READ, 0,
					I2C_SMBUS_QUICK, NULL) >= 0) {
			info.addr = *addrp;
			i2c_new_device(&core->i2c_adap, &info);
			break;
		}
	}
}

/* ---------------------------------------------------------------------- */

MODULE_AUTHOR("Gerd Knorr, Pavel Machek, Chris Pascoe");
MODULE_DESCRIPTION("input driver for cx88 GPIO-based IR remote controls");
MODULE_LICENSE("GPL");
