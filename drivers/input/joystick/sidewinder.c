// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 1998-2005 Vojtech Pavlik
 */

/*
 * Microsoft SideWinder joystick family driver for Linux
 */

/*
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/gameport.h>
#include <linux/jiffies.h>

#define DRIVER_DESC	"Microsoft SideWinder joystick family driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * These are really magic values. Changing them can make a problem go away,
 * as well as break everything.
 */

#undef SW_DEBUG
#undef SW_DEBUG_DATA

#define SW_START	600	/* The time we wait for the first bit [600 us] */
#define SW_STROBE	60	/* Max time per bit [60 us] */
#define SW_TIMEOUT	6	/* Wait for everything to settle [6 ms] */
#define SW_KICK		45	/* Wait after A0 fall till kick [45 us] */
#define SW_END		8	/* Number of bits before end of packet to kick */
#define SW_FAIL		16	/* Number of packet read errors to fail and reinitialize */
#define SW_BAD		2	/* Number of packet read errors to switch off 3d Pro optimization */
#define SW_OK		64	/* Number of packet read successes to switch optimization back on */
#define SW_LENGTH	512	/* Max number of bits in a packet */

#ifdef SW_DEBUG
#define dbg(format, arg...) printk(KERN_DEBUG __FILE__ ": " format "\n" , ## arg)
#else
#define dbg(format, arg...) do {} while (0)
#endif

/*
 * SideWinder joystick types ...
 */

#define SW_ID_3DP	0
#define SW_ID_GP	1
#define SW_ID_PP	2
#define SW_ID_FFP	3
#define SW_ID_FSP	4
#define SW_ID_FFW	5

/*
 * Names, buttons, axes ...
 */

static char *sw_name[] = {	"3D Pro", "GamePad", "Precision Pro", "Force Feedback Pro", "FreeStyle Pro",
				"Force Feedback Wheel" };

static char sw_abs[][7] = {
	{ ABS_X, ABS_Y, ABS_RZ, ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y },
	{ ABS_X, ABS_Y },
	{ ABS_X, ABS_Y, ABS_RZ, ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y },
	{ ABS_X, ABS_Y, ABS_RZ, ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y },
	{ ABS_X, ABS_Y,         ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y },
	{ ABS_RX, ABS_RUDDER,   ABS_THROTTLE }};

static char sw_bit[][7] = {
	{ 10, 10,  9, 10,  1,  1 },
	{  1,  1                 },
	{ 10, 10,  6,  7,  1,  1 },
	{ 10, 10,  6,  7,  1,  1 },
	{ 10, 10,  6,  1,  1     },
	{ 10,  7,  7,  1,  1     }};

static short sw_btn[][12] = {
	{ BTN_TRIGGER, BTN_TOP, BTN_THUMB, BTN_THUMB2, BTN_BASE, BTN_BASE2, BTN_BASE3, BTN_BASE4, BTN_MODE },
	{ BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_TL, BTN_TR, BTN_START, BTN_MODE },
	{ BTN_TRIGGER, BTN_THUMB, BTN_TOP, BTN_TOP2, BTN_BASE, BTN_BASE2, BTN_BASE3, BTN_BASE4, BTN_SELECT },
	{ BTN_TRIGGER, BTN_THUMB, BTN_TOP, BTN_TOP2, BTN_BASE, BTN_BASE2, BTN_BASE3, BTN_BASE4, BTN_SELECT },
	{ BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_TL, BTN_TR, BTN_START, BTN_MODE, BTN_SELECT },
	{ BTN_TRIGGER, BTN_TOP, BTN_THUMB, BTN_THUMB2, BTN_BASE, BTN_BASE2, BTN_BASE3, BTN_BASE4 }};

static struct {
	int x;
	int y;
} sw_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

struct sw {
	struct gameport *gameport;
	struct input_dev *dev[4];
	char name[64];
	char phys[4][32];
	int length;
	int type;
	int bits;
	int number;
	int fail;
	int ok;
	int reads;
	int bads;
};

/*
 * sw_read_packet() is a function which reads either a data packet, or an
 * identification packet from a SideWinder joystick. The protocol is very,
 * very, very braindamaged. Microsoft patented it in US patent #5628686.
 */

static int sw_read_packet(struct gameport *gameport, unsigned char *buf, int length, int id)
{
	unsigned long flags;
	int timeout, bitout, sched, i, kick, start, strobe;
	unsigned char pending, u, v;

	i = -id;						/* Don't care about data, only want ID */
	timeout = id ? gameport_time(gameport, SW_TIMEOUT * 1000) : 0; /* Set up global timeout for ID packet */
	kick = id ? gameport_time(gameport, SW_KICK) : 0;	/* Set up kick timeout for ID packet */
	start = gameport_time(gameport, SW_START);
	strobe = gameport_time(gameport, SW_STROBE);
	bitout = start;
	pending = 0;
	sched = 0;

        local_irq_save(flags);					/* Quiet, please */

	gameport_trigger(gameport);				/* Trigger */
	v = gameport_read(gameport);

	do {
		bitout--;
		u = v;
		v = gameport_read(gameport);
	} while (!(~v & u & 0x10) && (bitout > 0));		/* Wait for first falling edge on clock */

	if (bitout > 0)
		bitout = strobe;				/* Extend time if not timed out */

	while ((timeout > 0 || bitout > 0) && (i < length)) {

		timeout--;
		bitout--;					/* Decrement timers */
		sched--;

		u = v;
		v = gameport_read(gameport);

		if ((~u & v & 0x10) && (bitout > 0)) {		/* Rising edge on clock - data bit */
			if (i >= 0)				/* Want this data */
				buf[i] = v >> 5;		/* Store it */
			i++;					/* Advance index */
			bitout = strobe;			/* Extend timeout for next bit */
		}

		if (kick && (~v & u & 0x01)) {			/* Falling edge on axis 0 */
			sched = kick;				/* Schedule second trigger */
			kick = 0;				/* Don't schedule next time on falling edge */
			pending = 1;				/* Mark schedule */
		}

		if (pending && sched < 0 && (i > -SW_END)) {	/* Second trigger time */
			gameport_trigger(gameport);		/* Trigger */
			bitout = start;				/* Long bit timeout */
			pending = 0;				/* Unmark schedule */
			timeout = 0;				/* Switch from global to bit timeouts */
		}
	}

	local_irq_restore(flags);					/* Done - relax */

#ifdef SW_DEBUG_DATA
	{
		int j;
		printk(KERN_DEBUG "sidewinder.c: Read %d triplets. [", i);
		for (j = 0; j < i; j++) printk("%d", buf[j]);
		printk("]\n");
	}
#endif

	return i;
}

/*
 * sw_get_bits() and GB() compose bits from the triplet buffer into a __u64.
 * Parameter 'pos' is bit number inside packet where to start at, 'num' is number
 * of bits to be read, 'shift' is offset in the resulting __u64 to start at, bits
 * is number of bits per triplet.
 */

#define GB(pos,num) sw_get_bits(buf, pos, num, sw->bits)

static __u64 sw_get_bits(unsigned char *buf, int pos, int num, char bits)
{
	__u64 data = 0;
	int tri = pos % bits;						/* Start position */
	int i   = pos / bits;
	int bit = 0;

	while (num--) {
		data |= (__u64)((buf[i] >> tri++) & 1) << bit++;	/* Transfer bit */
		if (tri == bits) {
			i++;						/* Next triplet */
			tri = 0;
		}
	}

	return data;
}

/*
 * sw_init_digital() initializes a SideWinder 3D Pro joystick
 * into digital mode.
 */

static void sw_init_digital(struct gameport *gameport)
{
	static const int seq[] = { 140, 140+725, 140+300, 0 };
	unsigned long flags;
	int i, t;

        local_irq_save(flags);

	i = 0;
        do {
                gameport_trigger(gameport);			/* Trigger */
		t = gameport_time(gameport, SW_TIMEOUT * 1000);
		while ((gameport_read(gameport) & 1) && t) t--;	/* Wait for axis to fall back to 0 */
                udelay(seq[i]);					/* Delay magic time */
        } while (seq[++i]);

	gameport_trigger(gameport);				/* Last trigger */

	local_irq_restore(flags);
}

/*
 * sw_parity() computes parity of __u64
 */

static int sw_parity(__u64 t)
{
	int x = t ^ (t >> 32);

	x ^= x >> 16;
	x ^= x >> 8;
	x ^= x >> 4;
	x ^= x >> 2;
	x ^= x >> 1;
	return x & 1;
}

/*
 * sw_ccheck() checks synchronization bits and computes checksum of nibbles.
 */

static int sw_check(__u64 t)
{
	unsigned char sum = 0;

	if ((t & 0x8080808080808080ULL) ^ 0x80)			/* Sync */
		return -1;

	while (t) {						/* Sum */
		sum += t & 0xf;
		t >>= 4;
	}

	return sum & 0xf;
}

/*
 * sw_parse() analyzes SideWinder joystick data, and writes the results into
 * the axes and buttons arrays.
 */

static int sw_parse(unsigned char *buf, struct sw *sw)
{
	int hat, i, j;
	struct input_dev *dev;

	switch (sw->type) {

		case SW_ID_3DP:

			if (sw_check(GB(0,64)) || (hat = (GB(6,1) << 3) | GB(60,3)) > 8)
				return -1;

			dev = sw->dev[0];

			input_report_abs(dev, ABS_X,        (GB( 3,3) << 7) | GB(16,7));
			input_report_abs(dev, ABS_Y,        (GB( 0,3) << 7) | GB(24,7));
			input_report_abs(dev, ABS_RZ,       (GB(35,2) << 7) | GB(40,7));
			input_report_abs(dev, ABS_THROTTLE, (GB(32,3) << 7) | GB(48,7));

			input_report_abs(dev, ABS_HAT0X, sw_hat_to_axis[hat].x);
			input_report_abs(dev, ABS_HAT0Y, sw_hat_to_axis[hat].y);

			for (j = 0; j < 7; j++)
				input_report_key(dev, sw_btn[SW_ID_3DP][j], !GB(j+8,1));

			input_report_key(dev, BTN_BASE4, !GB(38,1));
			input_report_key(dev, BTN_BASE5, !GB(37,1));

			input_sync(dev);

			return 0;

		case SW_ID_GP:

			for (i = 0; i < sw->number; i ++) {

				if (sw_parity(GB(i*15,15)))
					return -1;

				input_report_abs(sw->dev[i], ABS_X, GB(i*15+3,1) - GB(i*15+2,1));
				input_report_abs(sw->dev[i], ABS_Y, GB(i*15+0,1) - GB(i*15+1,1));

				for (j = 0; j < 10; j++)
					input_report_key(sw->dev[i], sw_btn[SW_ID_GP][j], !GB(i*15+j+4,1));

				input_sync(sw->dev[i]);
			}

			return 0;

		case SW_ID_PP:
		case SW_ID_FFP:

			if (!sw_parity(GB(0,48)) || (hat = GB(42,4)) > 8)
				return -1;

			dev = sw->dev[0];
			input_report_abs(dev, ABS_X,        GB( 9,10));
			input_report_abs(dev, ABS_Y,        GB(19,10));
			input_report_abs(dev, ABS_RZ,       GB(36, 6));
			input_report_abs(dev, ABS_THROTTLE, GB(29, 7));

			input_report_abs(dev, ABS_HAT0X, sw_hat_to_axis[hat].x);
			input_report_abs(dev, ABS_HAT0Y, sw_hat_to_axis[hat].y);

			for (j = 0; j < 9; j++)
				input_report_key(dev, sw_btn[SW_ID_PP][j], !GB(j,1));

			input_sync(dev);

			return 0;

		case SW_ID_FSP:

			if (!sw_parity(GB(0,43)) || (hat = GB(28,4)) > 8)
				return -1;

			dev = sw->dev[0];
			input_report_abs(dev, ABS_X,        GB( 0,10));
			input_report_abs(dev, ABS_Y,        GB(16,10));
			input_report_abs(dev, ABS_THROTTLE, GB(32, 6));

			input_report_abs(dev, ABS_HAT0X, sw_hat_to_axis[hat].x);
			input_report_abs(dev, ABS_HAT0Y, sw_hat_to_axis[hat].y);

			for (j = 0; j < 6; j++)
				input_report_key(dev, sw_btn[SW_ID_FSP][j], !GB(j+10,1));

			input_report_key(dev, BTN_TR,     !GB(26,1));
			input_report_key(dev, BTN_START,  !GB(27,1));
			input_report_key(dev, BTN_MODE,   !GB(38,1));
			input_report_key(dev, BTN_SELECT, !GB(39,1));

			input_sync(dev);

			return 0;

		case SW_ID_FFW:

			if (!sw_parity(GB(0,33)))
				return -1;

			dev = sw->dev[0];
			input_report_abs(dev, ABS_RX,       GB( 0,10));
			input_report_abs(dev, ABS_RUDDER,   GB(10, 6));
			input_report_abs(dev, ABS_THROTTLE, GB(16, 6));

			for (j = 0; j < 8; j++)
				input_report_key(dev, sw_btn[SW_ID_FFW][j], !GB(j+22,1));

			input_sync(dev);

			return 0;
	}

	return -1;
}

/*
 * sw_read() reads SideWinder joystick data, and reinitializes
 * the joystick in case of persistent problems. This is the function that is
 * called from the generic code to poll the joystick.
 */

static int sw_read(struct sw *sw)
{
	unsigned char buf[SW_LENGTH];
	int i;

	i = sw_read_packet(sw->gameport, buf, sw->length, 0);

	if (sw->type == SW_ID_3DP && sw->length == 66 && i != 66) {		/* Broken packet, try to fix */

		if (i == 64 && !sw_check(sw_get_bits(buf,0,64,1))) {		/* Last init failed, 1 bit mode */
			printk(KERN_WARNING "sidewinder.c: Joystick in wrong mode on %s"
				" - going to reinitialize.\n", sw->gameport->phys);
			sw->fail = SW_FAIL;					/* Reinitialize */
			i = 128;						/* Bogus value */
		}

		if (i < 66 && GB(0,64) == GB(i*3-66,64))			/* 1 == 3 */
			i = 66;							/* Everything is fine */

		if (i < 66 && GB(0,64) == GB(66,64))				/* 1 == 2 */
			i = 66;							/* Everything is fine */

		if (i < 66 && GB(i*3-132,64) == GB(i*3-66,64)) {		/* 2 == 3 */
			memmove(buf, buf + i - 22, 22);				/* Move data */
			i = 66;							/* Carry on */
		}
	}

	if (i == sw->length && !sw_parse(buf, sw)) {				/* Parse data */

		sw->fail = 0;
		sw->ok++;

		if (sw->type == SW_ID_3DP && sw->length == 66			/* Many packets OK */
			&& sw->ok > SW_OK) {

			printk(KERN_INFO "sidewinder.c: No more trouble on %s"
				" - enabling optimization again.\n", sw->gameport->phys);
			sw->length = 22;
		}

		return 0;
	}

	sw->ok = 0;
	sw->fail++;

	if (sw->type == SW_ID_3DP && sw->length == 22 && sw->fail > SW_BAD) {	/* Consecutive bad packets */

		printk(KERN_INFO "sidewinder.c: Many bit errors on %s"
			" - disabling optimization.\n", sw->gameport->phys);
		sw->length = 66;
	}

	if (sw->fail < SW_FAIL)
		return -1;							/* Not enough, don't reinitialize yet */

	printk(KERN_WARNING "sidewinder.c: Too many bit errors on %s"
		" - reinitializing joystick.\n", sw->gameport->phys);

	if (!i && sw->type == SW_ID_3DP) {					/* 3D Pro can be in analog mode */
		mdelay(3 * SW_TIMEOUT);
		sw_init_digital(sw->gameport);
	}

	mdelay(SW_TIMEOUT);
	i = sw_read_packet(sw->gameport, buf, SW_LENGTH, 0);			/* Read normal data packet */
	mdelay(SW_TIMEOUT);
	sw_read_packet(sw->gameport, buf, SW_LENGTH, i);			/* Read ID packet, this initializes the stick */

	sw->fail = SW_FAIL;

	return -1;
}

static void sw_poll(struct gameport *gameport)
{
	struct sw *sw = gameport_get_drvdata(gameport);

	sw->reads++;
	if (sw_read(sw))
		sw->bads++;
}

static int sw_open(struct input_dev *dev)
{
	struct sw *sw = input_get_drvdata(dev);

	gameport_start_polling(sw->gameport);
	return 0;
}

static void sw_close(struct input_dev *dev)
{
	struct sw *sw = input_get_drvdata(dev);

	gameport_stop_polling(sw->gameport);
}

/*
 * sw_print_packet() prints the contents of a SideWinder packet.
 */

static void sw_print_packet(char *name, int length, unsigned char *buf, char bits)
{
	int i;

	printk(KERN_INFO "sidewinder.c: %s packet, %d bits. [", name, length);
	for (i = (((length + 3) >> 2) - 1); i >= 0; i--)
		printk("%x", (int)sw_get_bits(buf, i << 2, 4, bits));
	printk("]\n");
}

/*
 * sw_3dp_id() translates the 3DP id into a human legible string.
 * Unfortunately I don't know how to do this for the other SW types.
 */

static void sw_3dp_id(unsigned char *buf, char *comment, size_t size)
{
	int i;
	char pnp[8], rev[9];

	for (i = 0; i < 7; i++)						/* ASCII PnP ID */
		pnp[i] = sw_get_bits(buf, 24+8*i, 8, 1);

	for (i = 0; i < 8; i++)						/* ASCII firmware revision */
		rev[i] = sw_get_bits(buf, 88+8*i, 8, 1);

	pnp[7] = rev[8] = 0;

	snprintf(comment, size, " [PnP %d.%02d id %s rev %s]",
		(int) ((sw_get_bits(buf, 8, 6, 1) << 6) |		/* Two 6-bit values */
			sw_get_bits(buf, 16, 6, 1)) / 100,
		(int) ((sw_get_bits(buf, 8, 6, 1) << 6) |
			sw_get_bits(buf, 16, 6, 1)) % 100,
		 pnp, rev);
}

/*
 * sw_guess_mode() checks the upper two button bits for toggling -
 * indication of that the joystick is in 3-bit mode. This is documented
 * behavior for 3DP ID packet, and for example the FSP does this in
 * normal packets instead. Fun ...
 */

static int sw_guess_mode(unsigned char *buf, int len)
{
	int i;
	unsigned char xor = 0;

	for (i = 1; i < len; i++)
		xor |= (buf[i - 1] ^ buf[i]) & 6;

	return !!xor * 2 + 1;
}

/*
 * sw_connect() probes for SideWinder type joysticks.
 */

static int sw_connect(struct gameport *gameport, struct gameport_driver *drv)
{
	struct sw *sw;
	struct input_dev *input_dev;
	int i, j, k, l;
	int err = 0;
	unsigned char *buf = NULL;	/* [SW_LENGTH] */
	unsigned char *idbuf = NULL;	/* [SW_LENGTH] */
	unsigned char m = 1;
	char comment[40];

	comment[0] = 0;

	sw = kzalloc(sizeof(struct sw), GFP_KERNEL);
	buf = kmalloc(SW_LENGTH, GFP_KERNEL);
	idbuf = kmalloc(SW_LENGTH, GFP_KERNEL);
	if (!sw || !buf || !idbuf) {
		err = -ENOMEM;
		goto fail1;
	}

	sw->gameport = gameport;

	gameport_set_drvdata(gameport, sw);

	err = gameport_open(gameport, drv, GAMEPORT_MODE_RAW);
	if (err)
		goto fail1;

	dbg("Init 0: Opened %s, io %#x, speed %d",
		gameport->phys, gameport->io, gameport->speed);

	i = sw_read_packet(gameport, buf, SW_LENGTH, 0);		/* Read normal packet */
	msleep(SW_TIMEOUT);
	dbg("Init 1: Mode %d. Length %d.", m , i);

	if (!i) {							/* No data. 3d Pro analog mode? */
		sw_init_digital(gameport);				/* Switch to digital */
		msleep(SW_TIMEOUT);
		i = sw_read_packet(gameport, buf, SW_LENGTH, 0);	/* Retry reading packet */
		msleep(SW_TIMEOUT);
		dbg("Init 1b: Length %d.", i);
		if (!i) {						/* No data -> FAIL */
			err = -ENODEV;
			goto fail2;
		}
	}

	j = sw_read_packet(gameport, idbuf, SW_LENGTH, i);		/* Read ID. This initializes the stick */
	m |= sw_guess_mode(idbuf, j);					/* ID packet should carry mode info [3DP] */
	dbg("Init 2: Mode %d. ID Length %d.", m, j);

	if (j <= 0) {							/* Read ID failed. Happens in 1-bit mode on PP */
		msleep(SW_TIMEOUT);
		i = sw_read_packet(gameport, buf, SW_LENGTH, 0);	/* Retry reading packet */
		m |= sw_guess_mode(buf, i);
		dbg("Init 2b: Mode %d. Length %d.", m, i);
		if (!i) {
			err = -ENODEV;
			goto fail2;
		}
		msleep(SW_TIMEOUT);
		j = sw_read_packet(gameport, idbuf, SW_LENGTH, i);	/* Retry reading ID */
		dbg("Init 2c: ID Length %d.", j);
	}

	sw->type = -1;
	k = SW_FAIL;							/* Try SW_FAIL times */
	l = 0;

	do {
		k--;
		msleep(SW_TIMEOUT);
		i = sw_read_packet(gameport, buf, SW_LENGTH, 0);	/* Read data packet */
		dbg("Init 3: Mode %d. Length %d. Last %d. Tries %d.", m, i, l, k);

		if (i > l) {						/* Longer? As we can only lose bits, it makes */
									/* no sense to try detection for a packet shorter */
			l = i;						/* than the previous one */

			sw->number = 1;
			sw->gameport = gameport;
			sw->length = i;
			sw->bits = m;

			dbg("Init 3a: Case %d.\n", i * m);

			switch (i * m) {
				case 60:
					sw->number++;			/* fall through */
				case 45:				/* Ambiguous packet length */
					if (j <= 40) {			/* ID length less or eq 40 -> FSP */
				case 43:
						sw->type = SW_ID_FSP;
						break;
					}
					sw->number++;			/* fall through */
				case 30:
					sw->number++;			/* fall through */
				case 15:
					sw->type = SW_ID_GP;
					break;
				case 33:
				case 31:
					sw->type = SW_ID_FFW;
					break;
				case 48:				/* Ambiguous */
					if (j == 14) {			/* ID length 14*3 -> FFP */
						sw->type = SW_ID_FFP;
						sprintf(comment, " [AC %s]", sw_get_bits(idbuf,38,1,3) ? "off" : "on");
					} else
						sw->type = SW_ID_PP;
					break;
				case 66:
					sw->bits = 3;			/* fall through */
				case 198:
					sw->length = 22;		/* fall through */
				case 64:
					sw->type = SW_ID_3DP;
					if (j == 160)
						sw_3dp_id(idbuf, comment, sizeof(comment));
					break;
			}
		}

	} while (k && sw->type == -1);

	if (sw->type == -1) {
		printk(KERN_WARNING "sidewinder.c: unknown joystick device detected "
			"on %s, contact <vojtech@ucw.cz>\n", gameport->phys);
		sw_print_packet("ID", j * 3, idbuf, 3);
		sw_print_packet("Data", i * m, buf, m);
		err = -ENODEV;
		goto fail2;
	}

#ifdef SW_DEBUG
	sw_print_packet("ID", j * 3, idbuf, 3);
	sw_print_packet("Data", i * m, buf, m);
#endif

	gameport_set_poll_handler(gameport, sw_poll);
	gameport_set_poll_interval(gameport, 20);

	k = i;
	l = j;

	for (i = 0; i < sw->number; i++) {
		int bits, code;

		snprintf(sw->name, sizeof(sw->name),
			 "Microsoft SideWinder %s", sw_name[sw->type]);
		snprintf(sw->phys[i], sizeof(sw->phys[i]),
			 "%s/input%d", gameport->phys, i);

		sw->dev[i] = input_dev = input_allocate_device();
		if (!input_dev) {
			err = -ENOMEM;
			goto fail3;
		}

		input_dev->name = sw->name;
		input_dev->phys = sw->phys[i];
		input_dev->id.bustype = BUS_GAMEPORT;
		input_dev->id.vendor = GAMEPORT_ID_VENDOR_MICROSOFT;
		input_dev->id.product = sw->type;
		input_dev->id.version = 0x0100;
		input_dev->dev.parent = &gameport->dev;

		input_set_drvdata(input_dev, sw);

		input_dev->open = sw_open;
		input_dev->close = sw_close;

		input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

		for (j = 0; (bits = sw_bit[sw->type][j]); j++) {
			int min, max, fuzz, flat;

			code = sw_abs[sw->type][j];
			min = bits == 1 ? -1 : 0;
			max = (1 << bits) - 1;
			fuzz = (bits >> 1) >= 2 ? 1 << ((bits >> 1) - 2) : 0;
			flat = code == ABS_THROTTLE || bits < 5 ?
				0 : 1 << (bits - 5);

			input_set_abs_params(input_dev, code,
					     min, max, fuzz, flat);
		}

		for (j = 0; (code = sw_btn[sw->type][j]); j++)
			__set_bit(code, input_dev->keybit);

		dbg("%s%s [%d-bit id %d data %d]\n", sw->name, comment, m, l, k);

		err = input_register_device(sw->dev[i]);
		if (err)
			goto fail4;
	}

 out:	kfree(buf);
	kfree(idbuf);

	return err;

 fail4:	input_free_device(sw->dev[i]);
 fail3:	while (--i >= 0)
		input_unregister_device(sw->dev[i]);
 fail2:	gameport_close(gameport);
 fail1:	gameport_set_drvdata(gameport, NULL);
	kfree(sw);
	goto out;
}

static void sw_disconnect(struct gameport *gameport)
{
	struct sw *sw = gameport_get_drvdata(gameport);
	int i;

	for (i = 0; i < sw->number; i++)
		input_unregister_device(sw->dev[i]);
	gameport_close(gameport);
	gameport_set_drvdata(gameport, NULL);
	kfree(sw);
}

static struct gameport_driver sw_drv = {
	.driver		= {
		.name	= "sidewinder",
		.owner	= THIS_MODULE,
	},
	.description	= DRIVER_DESC,
	.connect	= sw_connect,
	.disconnect	= sw_disconnect,
};

module_gameport_driver(sw_drv);
