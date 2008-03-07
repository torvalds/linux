#include "firesat.h"
#include "firesat-rc.h"

#include <linux/input.h>

static u16 firesat_irtable[] = {
	KEY_ESC,
	KEY_F9,
	KEY_1,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_6,
	KEY_7,
	KEY_8,
	KEY_9,
	KEY_I,
	KEY_0,
	KEY_ENTER,
	KEY_RED,
	KEY_UP,
	KEY_GREEN,
	KEY_F10,
	KEY_SPACE,
	KEY_F11,
	KEY_YELLOW,
	KEY_DOWN,
	KEY_BLUE,
	KEY_Z,
	KEY_P,
	KEY_PAGEDOWN,
	KEY_LEFT,
	KEY_W,
	KEY_RIGHT,
	KEY_P,
	KEY_M,
	KEY_R,
	KEY_V,
	KEY_C,
	0
};

static struct input_dev firesat_idev;

int firesat_register_rc(void)
{
	int index;

	memset(&firesat_idev, 0, sizeof(firesat_idev));

	firesat_idev.evbit[0] = BIT(EV_KEY);

	for (index = 0; firesat_irtable[index] != 0; index++)
		set_bit(firesat_irtable[index], firesat_idev.keybit);

	return input_register_device(&firesat_idev);
}

int firesat_unregister_rc(void)
{
	input_unregister_device(&firesat_idev);
	return 0;
}

int firesat_got_remotecontrolcode(u16 code)
{
	u16 keycode;

	if (code > 0x4500 && code < 0x4520)
		keycode = firesat_irtable[code - 0x4501];
	else if (code > 0x453f && code < 0x4543)
		keycode = firesat_irtable[code - 0x4521];
	else {
		printk(KERN_DEBUG "%s: invalid key code 0x%04x\n", __func__,
		       code);
		return -EINVAL;
	}

	input_report_key(&firesat_idev, keycode, 1);
	input_report_key(&firesat_idev, keycode, 0);

	return 0;
}
