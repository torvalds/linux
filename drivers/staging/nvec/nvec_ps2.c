#include <linux/slab.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include "nvec.h"

#define START_STREAMING	{'\x06','\x03','\x01'}
#define STOP_STREAMING	{'\x06','\x04'}
#define SEND_COMMAND	{'\x06','\x01','\xf4','\x01'}

struct nvec_ps2
{
	struct serio *ser_dev;
	struct notifier_block notifier;
	struct nvec_chip *nvec;
};

static struct nvec_ps2 ps2_dev;

static int ps2_startstreaming(struct serio *ser_dev)
{
	unsigned char buf[] = START_STREAMING;
	nvec_write_async(ps2_dev.nvec, buf, sizeof(buf));
	return 0;
}

static void ps2_stopstreaming(struct serio *ser_dev)
{
	unsigned char buf[] = STOP_STREAMING;
	nvec_write_async(ps2_dev.nvec, buf, sizeof(buf));
}

/* is this really needed?
static void nvec_resp_handler(unsigned char *data) {
	serio_interrupt(ser_dev, data[4], 0);
}
*/

static int ps2_sendcommand(struct serio *ser_dev, unsigned char cmd)
{
	unsigned char buf[] = SEND_COMMAND;

	buf[2] = cmd & 0xff;

	dev_dbg(&ser_dev->dev, "Sending ps2 cmd %02x\n", cmd);
	nvec_write_async(ps2_dev.nvec, buf, sizeof(buf));

	return 0;
}

static int nvec_ps2_notifier(struct notifier_block *nb,
				unsigned long event_type, void *data)
{
	int i;
	unsigned char *msg = (unsigned char *)data;

	switch (event_type) {
		case NVEC_PS2_EVT:
			serio_interrupt(ps2_dev.ser_dev, msg[2], 0);
			return NOTIFY_STOP;

		case NVEC_PS2:
			if (msg[2] == 1)
				for(i = 0; i < (msg[1] - 2); i++)
					serio_interrupt(ps2_dev.ser_dev, msg[i+4], 0);
			else if (msg[1] != 2) /* !ack */
			{
				printk("nvec_ps2: unhandled mouse event ");
				for(i = 0; i <= (msg[1]+1); i++)
					printk("%02x ", msg[i]);
				printk(".\n");
			}

			return NOTIFY_STOP;
	}

	return NOTIFY_DONE;
}


int __init nvec_ps2(struct nvec_chip *nvec)
{
	struct serio *ser_dev = kzalloc(sizeof(struct serio), GFP_KERNEL);

	ser_dev->id.type=SERIO_8042;
	ser_dev->write=ps2_sendcommand;
	ser_dev->open=ps2_startstreaming;
	ser_dev->close=ps2_stopstreaming;

	strlcpy(ser_dev->name, "NVEC PS2", sizeof(ser_dev->name));
	strlcpy(ser_dev->phys, "NVEC I2C slave", sizeof(ser_dev->phys));

	ps2_dev.ser_dev = ser_dev;
	ps2_dev.notifier.notifier_call = nvec_ps2_notifier;
	ps2_dev.nvec = nvec;
	nvec_register_notifier(nvec, &ps2_dev.notifier, 0);

	serio_register_port(ser_dev);

	/* mouse reset */
	nvec_write_async(nvec, "\x06\x01\xff\x03", 4);

	return 0;
}
