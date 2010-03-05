/*
 * Driver for the remote control of SAA7146 based AV7110 cards
 *
 * Copyright (C) 1999-2003 Holger Waechtler <holger@convergence.de>
 * Copyright (C) 2003-2007 Oliver Endriss <o.endriss@gmx.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 */


#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/bitops.h>

#include "av7110.h"
#include "av7110_hw.h"


#define AV_CNT		4

#define IR_RC5		0
#define IR_RCMM		1
#define IR_RC5_EXT	2 /* internal only */

#define IR_ALL		0xffffffff

#define UP_TIMEOUT	(HZ*7/25)


/* Note: enable ir debugging by or'ing debug with 16 */

static int ir_protocol[AV_CNT] = { IR_RCMM, IR_RCMM, IR_RCMM, IR_RCMM};
module_param_array(ir_protocol, int, NULL, 0644);
MODULE_PARM_DESC(ir_protocol, "Infrared protocol: 0 RC5, 1 RCMM (default)");

static int ir_inversion[AV_CNT];
module_param_array(ir_inversion, int, NULL, 0644);
MODULE_PARM_DESC(ir_inversion, "Inversion of infrared signal: 0 not inverted (default), 1 inverted");

static uint ir_device_mask[AV_CNT] = { IR_ALL, IR_ALL, IR_ALL, IR_ALL };
module_param_array(ir_device_mask, uint, NULL, 0644);
MODULE_PARM_DESC(ir_device_mask, "Bitmask of infrared devices: bit 0..31 = device 0..31 (default: all)");


static int av_cnt;
static struct av7110 *av_list[AV_CNT];

static u16 default_key_map [256] = {
	KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7,
	KEY_8, KEY_9, KEY_BACK, 0, KEY_POWER, KEY_MUTE, 0, KEY_INFO,
	KEY_VOLUMEUP, KEY_VOLUMEDOWN, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	KEY_CHANNELUP, KEY_CHANNELDOWN, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, KEY_TEXT, 0, 0, KEY_TV, 0, 0, 0, 0, 0, KEY_SETUP, 0, 0,
	0, 0, 0, KEY_SUBTITLE, 0, 0, KEY_LANGUAGE, 0,
	KEY_RADIO, 0, 0, 0, 0, KEY_EXIT, 0, 0,
	KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_OK, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_RED, KEY_GREEN, KEY_YELLOW,
	KEY_BLUE, 0, 0, 0, 0, 0, 0, 0, KEY_MENU, KEY_LIST, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, KEY_UP, KEY_UP, KEY_DOWN, KEY_DOWN,
	0, 0, 0, 0, KEY_EPG, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_VCR
};


/* key-up timer */
static void av7110_emit_keyup(unsigned long parm)
{
	struct infrared *ir = (struct infrared *) parm;

	if (!ir || !test_bit(ir->last_key, ir->input_dev->key))
		return;

	input_report_key(ir->input_dev, ir->last_key, 0);
	input_sync(ir->input_dev);
}


/* tasklet */
static void av7110_emit_key(unsigned long parm)
{
	struct infrared *ir = (struct infrared *) parm;
	u32 ircom = ir->ir_command;
	u8 data;
	u8 addr;
	u16 toggle;
	u16 keycode;

	/* extract device address and data */
	switch (ir->protocol) {
	case IR_RC5: /* RC5: 5 bits device address, 6 bits data */
		data = ircom & 0x3f;
		addr = (ircom >> 6) & 0x1f;
		toggle = ircom & 0x0800;
		break;

	case IR_RCMM: /* RCMM: ? bits device address, ? bits data */
		data = ircom & 0xff;
		addr = (ircom >> 8) & 0x1f;
		toggle = ircom & 0x8000;
		break;

	case IR_RC5_EXT: /* extended RC5: 5 bits device address, 7 bits data */
		data = ircom & 0x3f;
		addr = (ircom >> 6) & 0x1f;
		/* invert 7th data bit for backward compatibility with RC5 keymaps */
		if (!(ircom & 0x1000))
			data |= 0x40;
		toggle = ircom & 0x0800;
		break;

	default:
		printk("%s invalid protocol %x\n", __func__, ir->protocol);
		return;
	}

	input_event(ir->input_dev, EV_MSC, MSC_RAW, (addr << 16) | data);
	input_event(ir->input_dev, EV_MSC, MSC_SCAN, data);

	keycode = ir->key_map[data];

	dprintk(16, "%s: code %08x -> addr %i data 0x%02x -> keycode %i\n",
		__func__, ircom, addr, data, keycode);

	/* check device address */
	if (!(ir->device_mask & (1 << addr)))
		return;

	if (!keycode) {
		printk ("%s: code %08x -> addr %i data 0x%02x -> unknown key!\n",
			__func__, ircom, addr, data);
		return;
	}

	if (timer_pending(&ir->keyup_timer)) {
		del_timer(&ir->keyup_timer);
		if (ir->last_key != keycode || toggle != ir->last_toggle) {
			ir->delay_timer_finished = 0;
			input_event(ir->input_dev, EV_KEY, ir->last_key, 0);
			input_event(ir->input_dev, EV_KEY, keycode, 1);
			input_sync(ir->input_dev);
		} else if (ir->delay_timer_finished) {
			input_event(ir->input_dev, EV_KEY, keycode, 2);
			input_sync(ir->input_dev);
		}
	} else {
		ir->delay_timer_finished = 0;
		input_event(ir->input_dev, EV_KEY, keycode, 1);
		input_sync(ir->input_dev);
	}

	ir->last_key = keycode;
	ir->last_toggle = toggle;

	ir->keyup_timer.expires = jiffies + UP_TIMEOUT;
	add_timer(&ir->keyup_timer);

}


/* register with input layer */
static void input_register_keys(struct infrared *ir)
{
	int i;

	set_bit(EV_KEY, ir->input_dev->evbit);
	set_bit(EV_REP, ir->input_dev->evbit);
	set_bit(EV_MSC, ir->input_dev->evbit);

	set_bit(MSC_RAW, ir->input_dev->mscbit);
	set_bit(MSC_SCAN, ir->input_dev->mscbit);

	memset(ir->input_dev->keybit, 0, sizeof(ir->input_dev->keybit));

	for (i = 0; i < ARRAY_SIZE(ir->key_map); i++) {
		if (ir->key_map[i] > KEY_MAX)
			ir->key_map[i] = 0;
		else if (ir->key_map[i] > KEY_RESERVED)
			set_bit(ir->key_map[i], ir->input_dev->keybit);
	}

	ir->input_dev->keycode = ir->key_map;
	ir->input_dev->keycodesize = sizeof(ir->key_map[0]);
	ir->input_dev->keycodemax = ARRAY_SIZE(ir->key_map);
}


/* called by the input driver after rep[REP_DELAY] ms */
static void input_repeat_key(unsigned long parm)
{
	struct infrared *ir = (struct infrared *) parm;

	ir->delay_timer_finished = 1;
}


/* check for configuration changes */
int av7110_check_ir_config(struct av7110 *av7110, int force)
{
	int i;
	int modified = force;
	int ret = -ENODEV;

	for (i = 0; i < av_cnt; i++)
		if (av7110 == av_list[i])
			break;

	if (i < av_cnt && av7110) {
		if ((av7110->ir.protocol & 1) != ir_protocol[i] ||
		    av7110->ir.inversion != ir_inversion[i])
			modified = true;

		if (modified) {
			/* protocol */
			if (ir_protocol[i]) {
				ir_protocol[i] = 1;
				av7110->ir.protocol = IR_RCMM;
				av7110->ir.ir_config = 0x0001;
			} else if (FW_VERSION(av7110->arm_app) >= 0x2620) {
				av7110->ir.protocol = IR_RC5_EXT;
				av7110->ir.ir_config = 0x0002;
			} else {
				av7110->ir.protocol = IR_RC5;
				av7110->ir.ir_config = 0x0000;
			}
			/* inversion */
			if (ir_inversion[i]) {
				ir_inversion[i] = 1;
				av7110->ir.ir_config |= 0x8000;
			}
			av7110->ir.inversion = ir_inversion[i];
			/* update ARM */
			ret = av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, SetIR, 1,
						av7110->ir.ir_config);
		} else
			ret = 0;

		/* address */
		if (av7110->ir.device_mask != ir_device_mask[i])
			av7110->ir.device_mask = ir_device_mask[i];
	}

	return ret;
}


/* /proc/av7110_ir interface */
static ssize_t av7110_ir_proc_write(struct file *file, const char __user *buffer,
				    size_t count, loff_t *pos)
{
	char *page;
	u32 ir_config;
	int size = sizeof ir_config + sizeof av_list[0]->ir.key_map;
	int i;

	if (count < size)
		return -EINVAL;

	page = vmalloc(size);
	if (!page)
		return -ENOMEM;

	if (copy_from_user(page, buffer, size)) {
		vfree(page);
		return -EFAULT;
	}

	memcpy(&ir_config, page, sizeof ir_config);

	for (i = 0; i < av_cnt; i++) {
		/* keymap */
		memcpy(av_list[i]->ir.key_map, page + sizeof ir_config,
			sizeof(av_list[i]->ir.key_map));
		/* protocol, inversion, address */
		ir_protocol[i] = ir_config & 0x0001;
		ir_inversion[i] = ir_config & 0x8000 ? 1 : 0;
		if (ir_config & 0x4000)
			ir_device_mask[i] = 1 << ((ir_config >> 16) & 0x1f);
		else
			ir_device_mask[i] = IR_ALL;
		/* update configuration */
		av7110_check_ir_config(av_list[i], false);
		input_register_keys(&av_list[i]->ir);
	}
	vfree(page);
	return count;
}

static const struct file_operations av7110_ir_proc_fops = {
	.owner		= THIS_MODULE,
	.write		= av7110_ir_proc_write,
};

/* interrupt handler */
static void ir_handler(struct av7110 *av7110, u32 ircom)
{
	dprintk(4, "ir command = %08x\n", ircom);
	av7110->ir.ir_command = ircom;
	tasklet_schedule(&av7110->ir.ir_tasklet);
}


int __devinit av7110_ir_init(struct av7110 *av7110)
{
	struct input_dev *input_dev;
	static struct proc_dir_entry *e;
	int err;

	if (av_cnt >= ARRAY_SIZE(av_list))
		return -ENOSPC;

	av_list[av_cnt++] = av7110;
	av7110_check_ir_config(av7110, true);

	init_timer(&av7110->ir.keyup_timer);
	av7110->ir.keyup_timer.function = av7110_emit_keyup;
	av7110->ir.keyup_timer.data = (unsigned long) &av7110->ir;

	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	av7110->ir.input_dev = input_dev;
	snprintf(av7110->ir.input_phys, sizeof(av7110->ir.input_phys),
		"pci-%s/ir0", pci_name(av7110->dev->pci));

	input_dev->name = "DVB on-card IR receiver";

	input_dev->phys = av7110->ir.input_phys;
	input_dev->id.bustype = BUS_PCI;
	input_dev->id.version = 2;
	if (av7110->dev->pci->subsystem_vendor) {
		input_dev->id.vendor = av7110->dev->pci->subsystem_vendor;
		input_dev->id.product = av7110->dev->pci->subsystem_device;
	} else {
		input_dev->id.vendor = av7110->dev->pci->vendor;
		input_dev->id.product = av7110->dev->pci->device;
	}
	input_dev->dev.parent = &av7110->dev->pci->dev;
	/* initial keymap */
	memcpy(av7110->ir.key_map, default_key_map, sizeof av7110->ir.key_map);
	input_register_keys(&av7110->ir);
	err = input_register_device(input_dev);
	if (err) {
		input_free_device(input_dev);
		return err;
	}
	input_dev->timer.function = input_repeat_key;
	input_dev->timer.data = (unsigned long) &av7110->ir;

	if (av_cnt == 1) {
		e = proc_create("av7110_ir", S_IWUSR, NULL, &av7110_ir_proc_fops);
		if (e)
			e->size = 4 + 256 * sizeof(u16);
	}

	tasklet_init(&av7110->ir.ir_tasklet, av7110_emit_key, (unsigned long) &av7110->ir);
	av7110->ir.ir_handler = ir_handler;

	return 0;
}


void __devexit av7110_ir_exit(struct av7110 *av7110)
{
	int i;

	if (av_cnt == 0)
		return;

	del_timer_sync(&av7110->ir.keyup_timer);
	av7110->ir.ir_handler = NULL;
	tasklet_kill(&av7110->ir.ir_tasklet);

	for (i = 0; i < av_cnt; i++)
		if (av_list[i] == av7110) {
			av_list[i] = av_list[av_cnt-1];
			av_list[av_cnt-1] = NULL;
			break;
		}

	if (av_cnt == 1)
		remove_proc_entry("av7110_ir", NULL);

	input_unregister_device(av7110->ir.input_dev);

	av_cnt--;
}

//MODULE_AUTHOR("Holger Waechtler <holger@convergence.de>, Oliver Endriss <o.endriss@gmx.de>");
//MODULE_LICENSE("GPL");
