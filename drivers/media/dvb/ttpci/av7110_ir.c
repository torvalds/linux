#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/input.h>
#include <linux/proc_fs.h>
#include <asm/bitops.h>

#include "av7110.h"
#include "av7110_hw.h"

#define UP_TIMEOUT (HZ*7/25)

/* enable ir debugging by or'ing debug with 16 */

static int av_cnt;
static struct av7110 *av_list[4];
static struct input_dev *input_dev;

static u8 delay_timer_finished;

static u16 key_map [256] = {
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


static void av7110_emit_keyup(unsigned long data)
{
	if (!data || !test_bit(data, input_dev->key))
		return;

	input_event(input_dev, EV_KEY, data, !!0);
}


static struct timer_list keyup_timer = { .function = av7110_emit_keyup };


static void av7110_emit_key(unsigned long parm)
{
	struct av7110 *av7110 = (struct av7110 *) parm;
	u32 ir_config = av7110->ir_config;
	u32 ircom = av7110->ir_command;
	u8 data;
	u8 addr;
	static u16 old_toggle = 0;
	u16 new_toggle;
	u16 keycode;

	/* extract device address and data */
	switch (ir_config & 0x0003) {
	case 0:	/* RC5: 5 bits device address, 6 bits data */
		data = ircom & 0x3f;
		addr = (ircom >> 6) & 0x1f;
		break;

	case 1:	/* RCMM: 8(?) bits device address, 8(?) bits data */
		data = ircom & 0xff;
		addr = (ircom >> 8) & 0xff;
		break;

	case 2:	/* extended RC5: 5 bits device address, 7 bits data */
		data = ircom & 0x3f;
		addr = (ircom >> 6) & 0x1f;
		/* invert 7th data bit for backward compatibility with RC5 keymaps */
		if (!(ircom & 0x1000))
			data |= 0x40;
		break;

	default:
		printk("invalid ir_config %x\n", ir_config);
		return;
	}

	keycode = key_map[data];

	dprintk(16, "code %08x -> addr %i data 0x%02x -> keycode %i\n",
		ircom, addr, data, keycode);

	/* check device address (if selected) */
	if (ir_config & 0x4000)
		if (addr != ((ir_config >> 16) & 0xff))
			return;

	if (!keycode) {
		printk ("%s: unknown key 0x%02x!!\n", __FUNCTION__, data);
		return;
	}

	if ((ir_config & 0x0003) == 1)
		new_toggle = 0; /* RCMM */
	else
		new_toggle = (ircom & 0x800); /* RC5, extended RC5 */

	if (timer_pending(&keyup_timer)) {
		del_timer(&keyup_timer);
		if (keyup_timer.data != keycode || new_toggle != old_toggle) {
			delay_timer_finished = 0;
			input_event(input_dev, EV_KEY, keyup_timer.data, !!0);
			input_event(input_dev, EV_KEY, keycode, !0);
		} else
			if (delay_timer_finished)
				input_event(input_dev, EV_KEY, keycode, 2);
	} else {
		delay_timer_finished = 0;
		input_event(input_dev, EV_KEY, keycode, !0);
	}

	keyup_timer.expires = jiffies + UP_TIMEOUT;
	keyup_timer.data = keycode;

	add_timer(&keyup_timer);

	old_toggle = new_toggle;
}

static void input_register_keys(void)
{
	int i;

	memset(input_dev->keybit, 0, sizeof(input_dev->keybit));

	for (i = 0; i < ARRAY_SIZE(key_map); i++) {
		if (key_map[i] > KEY_MAX)
			key_map[i] = 0;
		else if (key_map[i] > KEY_RESERVED)
			set_bit(key_map[i], input_dev->keybit);
	}
}


static void input_repeat_key(unsigned long data)
{
	/* called by the input driver after rep[REP_DELAY] ms */
	delay_timer_finished = 1;
}


static int av7110_setup_irc_config(struct av7110 *av7110, u32 ir_config)
{
	int ret = 0;

	dprintk(4, "%p\n", av7110);
	if (av7110) {
		ret = av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, SetIR, 1, ir_config);
		av7110->ir_config = ir_config;
	}
	return ret;
}


static int av7110_ir_write_proc(struct file *file, const char __user *buffer,
				unsigned long count, void *data)
{
	char *page;
	int size = 4 + 256 * sizeof(u16);
	u32 ir_config;
	int i;

	if (count < size)
		return -EINVAL;

	page = (char *) vmalloc(size);
	if (!page)
		return -ENOMEM;

	if (copy_from_user(page, buffer, size)) {
		vfree(page);
		return -EFAULT;
	}

	memcpy(&ir_config, page, 4);
	memcpy(&key_map, page + 4, 256 * sizeof(u16));
	vfree(page);
	if (FW_VERSION(av_list[0]->arm_app) >= 0x2620 && !(ir_config & 0x0001))
		ir_config |= 0x0002; /* enable extended RC5 */
	for (i = 0; i < av_cnt; i++)
		av7110_setup_irc_config(av_list[i], ir_config);
	input_register_keys();
	return count;
}


static void ir_handler(struct av7110 *av7110, u32 ircom)
{
	dprintk(4, "ircommand = %08x\n", ircom);
	av7110->ir_command = ircom;
	tasklet_schedule(&av7110->ir_tasklet);
}


int __devinit av7110_ir_init(struct av7110 *av7110)
{
	static struct proc_dir_entry *e;

	if (av_cnt >= sizeof av_list/sizeof av_list[0])
		return -ENOSPC;

	av7110_setup_irc_config(av7110, 0x0001);
	av_list[av_cnt++] = av7110;

	if (av_cnt == 1) {
		init_timer(&keyup_timer);
		keyup_timer.data = 0;

		input_dev = input_allocate_device();
		if (!input_dev)
			return -ENOMEM;

		input_dev->name = "DVB on-card IR receiver";

		set_bit(EV_KEY, input_dev->evbit);
		set_bit(EV_REP, input_dev->evbit);
		input_register_keys();
		input_register_device(input_dev);
		input_dev->timer.function = input_repeat_key;

		e = create_proc_entry("av7110_ir", S_IFREG | S_IRUGO | S_IWUSR, NULL);
		if (e) {
			e->write_proc = av7110_ir_write_proc;
			e->size = 4 + 256 * sizeof(u16);
		}
	}

	tasklet_init(&av7110->ir_tasklet, av7110_emit_key, (unsigned long) av7110);
	av7110->ir_handler = ir_handler;

	return 0;
}


void __devexit av7110_ir_exit(struct av7110 *av7110)
{
	int i;

	if (av_cnt == 0)
		return;

	av7110->ir_handler = NULL;
	tasklet_kill(&av7110->ir_tasklet);
	for (i = 0; i < av_cnt; i++)
		if (av_list[i] == av7110) {
			av_list[i] = av_list[av_cnt-1];
			av_list[av_cnt-1] = NULL;
			break;
		}

	if (av_cnt == 1) {
		del_timer_sync(&keyup_timer);
		remove_proc_entry("av7110_ir", NULL);
		input_unregister_device(input_dev);
	}

	av_cnt--;
}

//MODULE_AUTHOR("Holger Waechtler <holger@convergence.de>");
//MODULE_LICENSE("GPL");
