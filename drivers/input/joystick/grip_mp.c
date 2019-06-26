// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Driver for the Gravis Grip Multiport, a gamepad "hub" that
 *  connects up to four 9-pin digital gamepads/joysticks.
 *  Driver tested on SMP and UP kernel versions 2.4.18-4 and 2.4.18-5.
 *
 *  Thanks to Chris Gassib for helpful advice.
 *
 *  Copyright (c)      2002 Brian Bonnlander, Bill Soudan
 *  Copyright (c) 1998-2000 Vojtech Pavlik
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gameport.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/jiffies.h>

#define DRIVER_DESC	"Gravis Grip Multiport driver"

MODULE_AUTHOR("Brian Bonnlander");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#ifdef GRIP_DEBUG
#define dbg(format, arg...) printk(KERN_ERR __FILE__ ": " format "\n" , ## arg)
#else
#define dbg(format, arg...) do {} while (0)
#endif

#define GRIP_MAX_PORTS	4
/*
 * Grip multiport state
 */

struct grip_port {
	struct input_dev *dev;
	int mode;
	int registered;

	/* individual gamepad states */
	int buttons;
	int xaxes;
	int yaxes;
	int dirty;     /* has the state been updated? */
};

struct grip_mp {
	struct gameport *gameport;
	struct grip_port *port[GRIP_MAX_PORTS];
	int reads;
	int bads;
};

/*
 * Multiport packet interpretation
 */

#define PACKET_FULL          0x80000000       /* packet is full                        */
#define PACKET_IO_FAST       0x40000000       /* 3 bits per gameport read              */
#define PACKET_IO_SLOW       0x20000000       /* 1 bit per gameport read               */
#define PACKET_MP_MORE       0x04000000       /* multiport wants to send more          */
#define PACKET_MP_DONE       0x02000000       /* multiport done sending                */

/*
 * Packet status code interpretation
 */

#define IO_GOT_PACKET        0x0100           /* Got a packet                           */
#define IO_MODE_FAST         0x0200           /* Used 3 data bits per gameport read     */
#define IO_SLOT_CHANGE       0x0800           /* Multiport physical slot status changed */
#define IO_DONE              0x1000           /* Multiport is done sending packets      */
#define IO_RETRY             0x4000           /* Try again later to get packet          */
#define IO_RESET             0x8000           /* Force multiport to resend all packets  */

/*
 * Gamepad configuration data.  Other 9-pin digital joystick devices
 * may work with the multiport, so this may not be an exhaustive list!
 * Commodore 64 joystick remains untested.
 */

#define GRIP_INIT_DELAY         2000          /*  2 ms */

#define GRIP_MODE_NONE		0
#define GRIP_MODE_RESET         1
#define GRIP_MODE_GP		2
#define GRIP_MODE_C64		3

static const int grip_btn_gp[]  = { BTN_TR, BTN_TL, BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, -1 };
static const int grip_btn_c64[] = { BTN_JOYSTICK, -1 };

static const int grip_abs_gp[]  = { ABS_X, ABS_Y, -1 };
static const int grip_abs_c64[] = { ABS_X, ABS_Y, -1 };

static const int *grip_abs[] = { NULL, NULL, grip_abs_gp, grip_abs_c64 };
static const int *grip_btn[] = { NULL, NULL, grip_btn_gp, grip_btn_c64 };

static const char *grip_name[] = { NULL, NULL, "Gravis Grip Pad", "Commodore 64 Joystick" };

static const int init_seq[] = {
	1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1,
	1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1,
	1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1,
	0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1 };

/* Maps multiport directional values to X,Y axis values (each axis encoded in 3 bits) */

static const int axis_map[] = { 5, 9, 1, 5, 6, 10, 2, 6, 4, 8, 0, 4, 5, 9, 1, 5 };

static int register_slot(int i, struct grip_mp *grip);

/*
 * Returns whether an odd or even number of bits are on in pkt.
 */

static int bit_parity(u32 pkt)
{
	int x = pkt ^ (pkt >> 16);
	x ^= x >> 8;
	x ^= x >> 4;
	x ^= x >> 2;
	x ^= x >> 1;
	return x & 1;
}

/*
 * Poll gameport; return true if all bits set in 'onbits' are on and
 * all bits set in 'offbits' are off.
 */

static inline int poll_until(u8 onbits, u8 offbits, int u_sec, struct gameport* gp, u8 *data)
{
	int i, nloops;

	nloops = gameport_time(gp, u_sec);
	for (i = 0; i < nloops; i++) {
		*data = gameport_read(gp);
		if ((*data & onbits) == onbits &&
		    (~(*data) & offbits) == offbits)
			return 1;
	}
	dbg("gameport timed out after %d microseconds.\n", u_sec);
	return 0;
}

/*
 * Gets a 28-bit packet from the multiport.
 *
 * After getting a packet successfully, commands encoded by sendcode may
 * be sent to the multiport.
 *
 * The multiport clock value is reflected in gameport bit B4.
 *
 * Returns a packet status code indicating whether packet is valid, the transfer
 * mode, and any error conditions.
 *
 * sendflags:      current I/O status
 * sendcode:   data to send to the multiport if sendflags is nonzero
 */

static int mp_io(struct gameport* gameport, int sendflags, int sendcode, u32 *packet)
{
	u8  raw_data;            /* raw data from gameport */
	u8  data_mask;           /* packet data bits from raw_data */
	u32 pkt;                 /* packet temporary storage */
	int bits_per_read;       /* num packet bits per gameport read */
	int portvals = 0;        /* used for port value sanity check */
	int i;

	/* Gameport bits B0, B4, B5 should first be off, then B4 should come on. */

	*packet = 0;
	raw_data = gameport_read(gameport);
	if (raw_data & 1)
		return IO_RETRY;

	for (i = 0; i < 64; i++) {
		raw_data = gameport_read(gameport);
		portvals |= 1 << ((raw_data >> 4) & 3); /* Demux B4, B5 */
	}

	if (portvals == 1) {                            /* B4, B5 off */
		raw_data = gameport_read(gameport);
		portvals = raw_data & 0xf0;

		if (raw_data & 0x31)
			return IO_RESET;
		gameport_trigger(gameport);

		if (!poll_until(0x10, 0, 308, gameport, &raw_data))
			return IO_RESET;
	} else
		return IO_RETRY;

	/* Determine packet transfer mode and prepare for packet construction. */

	if (raw_data & 0x20) {                 /* 3 data bits/read */
		portvals |= raw_data >> 4;     /* Compare B4-B7 before & after trigger */

		if (portvals != 0xb)
			return 0;
		data_mask = 7;
		bits_per_read = 3;
		pkt = (PACKET_FULL | PACKET_IO_FAST) >> 28;
	} else {                                 /* 1 data bit/read */
		data_mask = 1;
		bits_per_read = 1;
		pkt = (PACKET_FULL | PACKET_IO_SLOW) >> 28;
	}

	/* Construct a packet.  Final data bits must be zero. */

	while (1) {
		if (!poll_until(0, 0x10, 77, gameport, &raw_data))
			return IO_RESET;
		raw_data = (raw_data >> 5) & data_mask;

		if (pkt & PACKET_FULL)
			break;
		pkt = (pkt << bits_per_read) | raw_data;

		if (!poll_until(0x10, 0, 77, gameport, &raw_data))
			return IO_RESET;
	}

	if (raw_data)
		return IO_RESET;

	/* If 3 bits/read used, drop from 30 bits to 28. */

	if (bits_per_read == 3) {
		pkt = (pkt & 0xffff0000) | ((pkt << 1) & 0xffff);
		pkt = (pkt >> 2) | 0xf0000000;
	}

	if (bit_parity(pkt) == 1)
		return IO_RESET;

	/* Acknowledge packet receipt */

	if (!poll_until(0x30, 0, 77, gameport, &raw_data))
		return IO_RESET;

	raw_data = gameport_read(gameport);

	if (raw_data & 1)
		return IO_RESET;

	gameport_trigger(gameport);

	if (!poll_until(0, 0x20, 77, gameport, &raw_data))
		return IO_RESET;

        /* Return if we just wanted the packet or multiport wants to send more */

	*packet = pkt;
	if ((sendflags == 0) || ((sendflags & IO_RETRY) && !(pkt & PACKET_MP_DONE)))
		return IO_GOT_PACKET;

	if (pkt & PACKET_MP_MORE)
		return IO_GOT_PACKET | IO_RETRY;

	/* Multiport is done sending packets and is ready to receive data */

	if (!poll_until(0x20, 0, 77, gameport, &raw_data))
		return IO_GOT_PACKET | IO_RESET;

	raw_data = gameport_read(gameport);
	if (raw_data & 1)
		return IO_GOT_PACKET | IO_RESET;

	/* Trigger gameport based on bits in sendcode */

	gameport_trigger(gameport);
	do {
		if (!poll_until(0x20, 0x10, 116, gameport, &raw_data))
			return IO_GOT_PACKET | IO_RESET;

		if (!poll_until(0x30, 0, 193, gameport, &raw_data))
			return IO_GOT_PACKET | IO_RESET;

		if (raw_data & 1)
			return IO_GOT_PACKET | IO_RESET;

		if (sendcode & 1)
			gameport_trigger(gameport);

		sendcode >>= 1;
	} while (sendcode);

	return IO_GOT_PACKET | IO_MODE_FAST;
}

/*
 * Disables and restores interrupts for mp_io(), which does the actual I/O.
 */

static int multiport_io(struct gameport* gameport, int sendflags, int sendcode, u32 *packet)
{
	int status;
	unsigned long flags;

	local_irq_save(flags);
	status = mp_io(gameport, sendflags, sendcode, packet);
	local_irq_restore(flags);

	return status;
}

/*
 * Puts multiport into digital mode.  Multiport LED turns green.
 *
 * Returns true if a valid digital packet was received, false otherwise.
 */

static int dig_mode_start(struct gameport *gameport, u32 *packet)
{
	int i;
	int flags, tries = 0, bads = 0;

	for (i = 0; i < ARRAY_SIZE(init_seq); i++) {     /* Send magic sequence */
		if (init_seq[i])
			gameport_trigger(gameport);
		udelay(GRIP_INIT_DELAY);
	}

	for (i = 0; i < 16; i++)            /* Wait for multiport to settle */
		udelay(GRIP_INIT_DELAY);

	while (tries < 64 && bads < 8) {    /* Reset multiport and try getting a packet */

		flags = multiport_io(gameport, IO_RESET, 0x27, packet);

		if (flags & IO_MODE_FAST)
			return 1;

		if (flags & IO_RETRY)
			tries++;
		else
			bads++;
	}
	return 0;
}

/*
 * Packet structure: B0-B15   => gamepad state
 *                   B16-B20  => gamepad device type
 *                   B21-B24  => multiport slot index (1-4)
 *
 * Known device types: 0x1f (grip pad), 0x0 (no device).  Others may exist.
 *
 * Returns the packet status.
 */

static int get_and_decode_packet(struct grip_mp *grip, int flags)
{
	struct grip_port *port;
	u32 packet;
	int joytype = 0;
	int slot;

	/* Get a packet and check for validity */

	flags &= IO_RESET | IO_RETRY;
	flags = multiport_io(grip->gameport, flags, 0, &packet);
	grip->reads++;

	if (packet & PACKET_MP_DONE)
		flags |= IO_DONE;

	if (flags && !(flags & IO_GOT_PACKET)) {
		grip->bads++;
		return flags;
	}

	/* Ignore non-gamepad packets, e.g. multiport hardware version */

	slot = ((packet >> 21) & 0xf) - 1;
	if ((slot < 0) || (slot > 3))
		return flags;

	port = grip->port[slot];

	/*
	 * Handle "reset" packets, which occur at startup, and when gamepads
	 * are removed or plugged in.  May contain configuration of a new gamepad.
	 */

	joytype = (packet >> 16) & 0x1f;
	if (!joytype) {

		if (port->registered) {
			printk(KERN_INFO "grip_mp: removing %s, slot %d\n",
			       grip_name[port->mode], slot);
			input_unregister_device(port->dev);
			port->registered = 0;
		}
		dbg("Reset: grip multiport slot %d\n", slot);
		port->mode = GRIP_MODE_RESET;
		flags |= IO_SLOT_CHANGE;
		return flags;
	}

	/* Interpret a grip pad packet */

	if (joytype == 0x1f) {

		int dir = (packet >> 8) & 0xf;          /* eight way directional value */
		port->buttons = (~packet) & 0xff;
		port->yaxes = ((axis_map[dir] >> 2) & 3) - 1;
		port->xaxes = (axis_map[dir] & 3) - 1;
		port->dirty = 1;

		if (port->mode == GRIP_MODE_RESET)
			flags |= IO_SLOT_CHANGE;

		port->mode = GRIP_MODE_GP;

		if (!port->registered) {
			dbg("New Grip pad in multiport slot %d.\n", slot);
			if (register_slot(slot, grip)) {
				port->mode = GRIP_MODE_RESET;
				port->dirty = 0;
			}
		}
		return flags;
	}

	/* Handle non-grip device codes.  For now, just print diagnostics. */

	{
		static int strange_code = 0;
		if (strange_code != joytype) {
			printk(KERN_INFO "Possible non-grip pad/joystick detected.\n");
			printk(KERN_INFO "Got joy type 0x%x and packet 0x%x.\n", joytype, packet);
			strange_code = joytype;
		}
	}
	return flags;
}

/*
 * Returns true if all multiport slot states appear valid.
 */

static int slots_valid(struct grip_mp *grip)
{
	int flags, slot, invalid = 0, active = 0;

	flags = get_and_decode_packet(grip, 0);
	if (!(flags & IO_GOT_PACKET))
		return 0;

	for (slot = 0; slot < 4; slot++) {
		if (grip->port[slot]->mode == GRIP_MODE_RESET)
			invalid = 1;
		if (grip->port[slot]->mode != GRIP_MODE_NONE)
			active = 1;
	}

	/* Return true if no active slot but multiport sent all its data */
	if (!active)
		return (flags & IO_DONE) ? 1 : 0;

	/* Return false if invalid device code received */
	return invalid ? 0 : 1;
}

/*
 * Returns whether the multiport was placed into digital mode and
 * able to communicate its state successfully.
 */

static int multiport_init(struct grip_mp *grip)
{
	int dig_mode, initialized = 0, tries = 0;
	u32 packet;

	dig_mode = dig_mode_start(grip->gameport, &packet);
	while (!dig_mode && tries < 4) {
		dig_mode = dig_mode_start(grip->gameport, &packet);
		tries++;
	}

	if (dig_mode)
		dbg("multiport_init(): digital mode activated.\n");
	else {
		dbg("multiport_init(): unable to activate digital mode.\n");
		return 0;
	}

	/* Get packets, store multiport state, and check state's validity */
	for (tries = 0; tries < 4096; tries++) {
		if (slots_valid(grip)) {
			initialized = 1;
			break;
		}
	}
	dbg("multiport_init(): initialized == %d\n", initialized);
	return initialized;
}

/*
 * Reports joystick state to the linux input layer.
 */

static void report_slot(struct grip_mp *grip, int slot)
{
	struct grip_port *port = grip->port[slot];
	int i;

	/* Store button states with linux input driver */

	for (i = 0; i < 8; i++)
		input_report_key(port->dev, grip_btn_gp[i], (port->buttons >> i) & 1);

	/* Store axis states with linux driver */

	input_report_abs(port->dev, ABS_X, port->xaxes);
	input_report_abs(port->dev, ABS_Y, port->yaxes);

	/* Tell the receiver of the events to process them */

	input_sync(port->dev);

	port->dirty = 0;
}

/*
 * Get the multiport state.
 */

static void grip_poll(struct gameport *gameport)
{
	struct grip_mp *grip = gameport_get_drvdata(gameport);
	int i, npkts, flags;

	for (npkts = 0; npkts < 4; npkts++) {
		flags = IO_RETRY;
		for (i = 0; i < 32; i++) {
			flags = get_and_decode_packet(grip, flags);
			if ((flags & IO_GOT_PACKET) || !(flags & IO_RETRY))
				break;
		}
		if (flags & IO_DONE)
			break;
	}

	for (i = 0; i < 4; i++)
		if (grip->port[i]->dirty)
			report_slot(grip, i);
}

/*
 * Called when a joystick device file is opened
 */

static int grip_open(struct input_dev *dev)
{
	struct grip_mp *grip = input_get_drvdata(dev);

	gameport_start_polling(grip->gameport);
	return 0;
}

/*
 * Called when a joystick device file is closed
 */

static void grip_close(struct input_dev *dev)
{
	struct grip_mp *grip = input_get_drvdata(dev);

	gameport_stop_polling(grip->gameport);
}

/*
 * Tell the linux input layer about a newly plugged-in gamepad.
 */

static int register_slot(int slot, struct grip_mp *grip)
{
	struct grip_port *port = grip->port[slot];
	struct input_dev *input_dev;
	int j, t;
	int err;

	port->dev = input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	input_dev->name = grip_name[port->mode];
	input_dev->id.bustype = BUS_GAMEPORT;
	input_dev->id.vendor = GAMEPORT_ID_VENDOR_GRAVIS;
	input_dev->id.product = 0x0100 + port->mode;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &grip->gameport->dev;

	input_set_drvdata(input_dev, grip);

	input_dev->open = grip_open;
	input_dev->close = grip_close;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	for (j = 0; (t = grip_abs[port->mode][j]) >= 0; j++)
		input_set_abs_params(input_dev, t, -1, 1, 0, 0);

	for (j = 0; (t = grip_btn[port->mode][j]) >= 0; j++)
		if (t > 0)
			set_bit(t, input_dev->keybit);

	err = input_register_device(port->dev);
	if (err) {
		input_free_device(port->dev);
		return err;
	}

	port->registered = 1;

	if (port->dirty)	            /* report initial state, if any */
		report_slot(grip, slot);

	return 0;
}

static int grip_connect(struct gameport *gameport, struct gameport_driver *drv)
{
	struct grip_mp *grip;
	int err;

	if (!(grip = kzalloc(sizeof(struct grip_mp), GFP_KERNEL)))
		return -ENOMEM;

	grip->gameport = gameport;

	gameport_set_drvdata(gameport, grip);

	err = gameport_open(gameport, drv, GAMEPORT_MODE_RAW);
	if (err)
		goto fail1;

	gameport_set_poll_handler(gameport, grip_poll);
	gameport_set_poll_interval(gameport, 20);

	if (!multiport_init(grip)) {
		err = -ENODEV;
		goto fail2;
	}

	if (!grip->port[0]->mode && !grip->port[1]->mode && !grip->port[2]->mode && !grip->port[3]->mode) {
		/* nothing plugged in */
		err = -ENODEV;
		goto fail2;
	}

	return 0;

fail2:	gameport_close(gameport);
fail1:	gameport_set_drvdata(gameport, NULL);
	kfree(grip);
	return err;
}

static void grip_disconnect(struct gameport *gameport)
{
	struct grip_mp *grip = gameport_get_drvdata(gameport);
	int i;

	for (i = 0; i < 4; i++)
		if (grip->port[i]->registered)
			input_unregister_device(grip->port[i]->dev);
	gameport_close(gameport);
	gameport_set_drvdata(gameport, NULL);
	kfree(grip);
}

static struct gameport_driver grip_drv = {
	.driver		= {
		.name	= "grip_mp",
	},
	.description	= DRIVER_DESC,
	.connect	= grip_connect,
	.disconnect	= grip_disconnect,
};

module_gameport_driver(grip_drv);
