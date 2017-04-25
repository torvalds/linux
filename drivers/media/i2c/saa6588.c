/*
    Driver for SAA6588 RDS decoder

    (c) 2005 Hans J. Koch

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
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/uaccess.h>

#include <media/i2c/saa6588.h>
#include <media/v4l2-device.h>


/* insmod options */
static unsigned int debug;
static unsigned int xtal;
static unsigned int mmbs;
static unsigned int plvl;
static unsigned int bufblocks = 100;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages");
module_param(xtal, int, 0);
MODULE_PARM_DESC(xtal, "select oscillator frequency (0..3), default 0");
module_param(mmbs, int, 0);
MODULE_PARM_DESC(mmbs, "enable MMBS mode: 0=off (default), 1=on");
module_param(plvl, int, 0);
MODULE_PARM_DESC(plvl, "select pause level (0..3), default 0");
module_param(bufblocks, int, 0);
MODULE_PARM_DESC(bufblocks, "number of buffered blocks, default 100");

MODULE_DESCRIPTION("v4l2 driver module for SAA6588 RDS decoder");
MODULE_AUTHOR("Hans J. Koch <koch@hjk-az.de>");

MODULE_LICENSE("GPL");

/* ---------------------------------------------------------------------- */

#define UNSET       (-1U)
#define PREFIX      "saa6588: "
#define dprintk     if (debug) printk

struct saa6588 {
	struct v4l2_subdev sd;
	struct delayed_work work;
	spinlock_t lock;
	unsigned char *buffer;
	unsigned int buf_size;
	unsigned int rd_index;
	unsigned int wr_index;
	unsigned int block_count;
	unsigned char last_blocknum;
	wait_queue_head_t read_queue;
	int data_available_for_read;
	u8 sync;
};

static inline struct saa6588 *to_saa6588(struct v4l2_subdev *sd)
{
	return container_of(sd, struct saa6588, sd);
}

/* ---------------------------------------------------------------------- */

/*
 * SAA6588 defines
 */

/* Initialization and mode control byte (0w) */

/* bit 0+1 (DAC0/DAC1) */
#define cModeStandard           0x00
#define cModeFastPI             0x01
#define cModeReducedRequest     0x02
#define cModeInvalid            0x03

/* bit 2 (RBDS) */
#define cProcessingModeRDS      0x00
#define cProcessingModeRBDS     0x04

/* bit 3+4 (SYM0/SYM1) */
#define cErrCorrectionNone      0x00
#define cErrCorrection2Bits     0x08
#define cErrCorrection5Bits     0x10
#define cErrCorrectionNoneRBDS  0x18

/* bit 5 (NWSY) */
#define cSyncNormal             0x00
#define cSyncRestart            0x20

/* bit 6 (TSQD) */
#define cSigQualityDetectOFF    0x00
#define cSigQualityDetectON     0x40

/* bit 7 (SQCM) */
#define cSigQualityTriggered    0x00
#define cSigQualityContinous    0x80

/* Pause level and flywheel control byte (1w) */

/* bits 0..5 (FEB0..FEB5) */
#define cFlywheelMaxBlocksMask  0x3F
#define cFlywheelDefault        0x20

/* bits 6+7 (PL0/PL1) */
#define cPauseLevel_11mV	0x00
#define cPauseLevel_17mV        0x40
#define cPauseLevel_27mV        0x80
#define cPauseLevel_43mV        0xC0

/* Pause time/oscillator frequency/quality detector control byte (1w) */

/* bits 0..4 (SQS0..SQS4) */
#define cQualityDetectSensMask  0x1F
#define cQualityDetectDefault   0x0F

/* bit 5 (SOSC) */
#define cSelectOscFreqOFF	0x00
#define cSelectOscFreqON	0x20

/* bit 6+7 (PTF0/PTF1) */
#define cOscFreq_4332kHz	0x00
#define cOscFreq_8664kHz	0x40
#define cOscFreq_12996kHz	0x80
#define cOscFreq_17328kHz	0xC0

/* ---------------------------------------------------------------------- */

static bool block_from_buf(struct saa6588 *s, unsigned char *buf)
{
	int i;

	if (s->rd_index == s->wr_index) {
		if (debug > 2)
			dprintk(PREFIX "Read: buffer empty.\n");
		return false;
	}

	if (debug > 2) {
		dprintk(PREFIX "Read: ");
		for (i = s->rd_index; i < s->rd_index + 3; i++)
			dprintk("0x%02x ", s->buffer[i]);
	}

	memcpy(buf, &s->buffer[s->rd_index], 3);

	s->rd_index += 3;
	if (s->rd_index >= s->buf_size)
		s->rd_index = 0;
	s->block_count--;

	if (debug > 2)
		dprintk("%d blocks total.\n", s->block_count);

	return true;
}

static void read_from_buf(struct saa6588 *s, struct saa6588_command *a)
{
	unsigned char __user *buf_ptr = a->buffer;
	unsigned char buf[3];
	unsigned long flags;
	unsigned int rd_blocks;
	unsigned int i;

	a->result = 0;
	if (!a->buffer)
		return;

	while (!a->nonblocking && !s->data_available_for_read) {
		int ret = wait_event_interruptible(s->read_queue,
					     s->data_available_for_read);
		if (ret == -ERESTARTSYS) {
			a->result = -EINTR;
			return;
		}
	}

	rd_blocks = a->block_count;
	spin_lock_irqsave(&s->lock, flags);
	if (rd_blocks > s->block_count)
		rd_blocks = s->block_count;
	spin_unlock_irqrestore(&s->lock, flags);

	if (!rd_blocks)
		return;

	for (i = 0; i < rd_blocks; i++) {
		bool got_block;

		spin_lock_irqsave(&s->lock, flags);
		got_block = block_from_buf(s, buf);
		spin_unlock_irqrestore(&s->lock, flags);
		if (!got_block)
			break;
		if (copy_to_user(buf_ptr, buf, 3)) {
			a->result = -EFAULT;
			return;
		}
		buf_ptr += 3;
		a->result += 3;
	}
	spin_lock_irqsave(&s->lock, flags);
	s->data_available_for_read = (s->block_count > 0);
	spin_unlock_irqrestore(&s->lock, flags);
}

static void block_to_buf(struct saa6588 *s, unsigned char *blockbuf)
{
	unsigned int i;

	if (debug > 3)
		dprintk(PREFIX "New block: ");

	for (i = 0; i < 3; ++i) {
		if (debug > 3)
			dprintk("0x%02x ", blockbuf[i]);
		s->buffer[s->wr_index] = blockbuf[i];
		s->wr_index++;
	}

	if (s->wr_index >= s->buf_size)
		s->wr_index = 0;

	if (s->wr_index == s->rd_index) {
		s->rd_index += 3;
		if (s->rd_index >= s->buf_size)
			s->rd_index = 0;
	} else
		s->block_count++;

	if (debug > 3)
		dprintk("%d blocks total.\n", s->block_count);
}

static void saa6588_i2c_poll(struct saa6588 *s)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s->sd);
	unsigned long flags;
	unsigned char tmpbuf[6];
	unsigned char blocknum;
	unsigned char tmp;

	/* Although we only need 3 bytes, we have to read at least 6.
	   SAA6588 returns garbage otherwise. */
	if (6 != i2c_master_recv(client, &tmpbuf[0], 6)) {
		if (debug > 1)
			dprintk(PREFIX "read error!\n");
		return;
	}

	s->sync = tmpbuf[0] & 0x10;
	if (!s->sync)
		return;
	blocknum = tmpbuf[0] >> 5;
	if (blocknum == s->last_blocknum) {
		if (debug > 3)
			dprintk("Saw block %d again.\n", blocknum);
		return;
	}

	s->last_blocknum = blocknum;

	/*
	   Byte order according to v4l2 specification:

	   Byte 0: Least Significant Byte of RDS Block
	   Byte 1: Most Significant Byte of RDS Block
	   Byte 2 Bit 7: Error bit. Indicates that an uncorrectable error
	   occurred during reception of this block.
	   Bit 6: Corrected bit. Indicates that an error was
	   corrected for this data block.
	   Bits 5-3: Same as bits 0-2.
	   Bits 2-0: Block number.

	   SAA6588 byte order is Status-MSB-LSB, so we have to swap the
	   first and the last of the 3 bytes block.
	 */

	swap(tmpbuf[2], tmpbuf[0]);

	/* Map 'Invalid block E' to 'Invalid Block' */
	if (blocknum == 6)
		blocknum = V4L2_RDS_BLOCK_INVALID;
	/* And if are not in mmbs mode, then 'Block E' is also mapped
	   to 'Invalid Block'. As far as I can tell MMBS is discontinued,
	   and if there is ever a need to support E blocks, then please
	   contact the linux-media mailinglist. */
	else if (!mmbs && blocknum == 5)
		blocknum = V4L2_RDS_BLOCK_INVALID;
	tmp = blocknum;
	tmp |= blocknum << 3;	/* Received offset == Offset Name (OK ?) */
	if ((tmpbuf[2] & 0x03) == 0x03)
		tmp |= V4L2_RDS_BLOCK_ERROR;	 /* uncorrectable error */
	else if ((tmpbuf[2] & 0x03) != 0x00)
		tmp |= V4L2_RDS_BLOCK_CORRECTED; /* corrected error */
	tmpbuf[2] = tmp;	/* Is this enough ? Should we also check other bits ? */

	spin_lock_irqsave(&s->lock, flags);
	block_to_buf(s, tmpbuf);
	spin_unlock_irqrestore(&s->lock, flags);
	s->data_available_for_read = 1;
	wake_up_interruptible(&s->read_queue);
}

static void saa6588_work(struct work_struct *work)
{
	struct saa6588 *s = container_of(work, struct saa6588, work.work);

	saa6588_i2c_poll(s);
	schedule_delayed_work(&s->work, msecs_to_jiffies(20));
}

static void saa6588_configure(struct saa6588 *s)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s->sd);
	unsigned char buf[3];
	int rc;

	buf[0] = cSyncRestart;
	if (mmbs)
		buf[0] |= cProcessingModeRBDS;

	buf[1] = cFlywheelDefault;
	switch (plvl) {
	case 0:
		buf[1] |= cPauseLevel_11mV;
		break;
	case 1:
		buf[1] |= cPauseLevel_17mV;
		break;
	case 2:
		buf[1] |= cPauseLevel_27mV;
		break;
	case 3:
		buf[1] |= cPauseLevel_43mV;
		break;
	default:		/* nothing */
		break;
	}

	buf[2] = cQualityDetectDefault | cSelectOscFreqON;

	switch (xtal) {
	case 0:
		buf[2] |= cOscFreq_4332kHz;
		break;
	case 1:
		buf[2] |= cOscFreq_8664kHz;
		break;
	case 2:
		buf[2] |= cOscFreq_12996kHz;
		break;
	case 3:
		buf[2] |= cOscFreq_17328kHz;
		break;
	default:		/* nothing */
		break;
	}

	dprintk(PREFIX "writing: 0w=0x%02x 1w=0x%02x 2w=0x%02x\n",
		buf[0], buf[1], buf[2]);

	rc = i2c_master_send(client, buf, 3);
	if (rc != 3)
		printk(PREFIX "i2c i/o error: rc == %d (should be 3)\n", rc);
}

/* ---------------------------------------------------------------------- */

static long saa6588_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct saa6588 *s = to_saa6588(sd);
	struct saa6588_command *a = arg;

	switch (cmd) {
		/* --- close() for /dev/radio --- */
	case SAA6588_CMD_CLOSE:
		s->data_available_for_read = 1;
		wake_up_interruptible(&s->read_queue);
		s->data_available_for_read = 0;
		a->result = 0;
		break;
		/* --- read() for /dev/radio --- */
	case SAA6588_CMD_READ:
		read_from_buf(s, a);
		break;
		/* --- poll() for /dev/radio --- */
	case SAA6588_CMD_POLL:
		a->result = 0;
		if (s->data_available_for_read)
			a->result |= POLLIN | POLLRDNORM;
		poll_wait(a->instance, &s->read_queue, a->event_list);
		break;

	default:
		/* nothing */
		return -ENOIOCTLCMD;
	}
	return 0;
}

static int saa6588_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	struct saa6588 *s = to_saa6588(sd);

	vt->capability |= V4L2_TUNER_CAP_RDS | V4L2_TUNER_CAP_RDS_BLOCK_IO;
	if (s->sync)
		vt->rxsubchans |= V4L2_TUNER_SUB_RDS;
	return 0;
}

static int saa6588_s_tuner(struct v4l2_subdev *sd, const struct v4l2_tuner *vt)
{
	struct saa6588 *s = to_saa6588(sd);

	saa6588_configure(s);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops saa6588_core_ops = {
	.ioctl = saa6588_ioctl,
};

static const struct v4l2_subdev_tuner_ops saa6588_tuner_ops = {
	.g_tuner = saa6588_g_tuner,
	.s_tuner = saa6588_s_tuner,
};

static const struct v4l2_subdev_ops saa6588_ops = {
	.core = &saa6588_core_ops,
	.tuner = &saa6588_tuner_ops,
};

/* ---------------------------------------------------------------------- */

static int saa6588_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct saa6588 *s;
	struct v4l2_subdev *sd;

	v4l_info(client, "saa6588 found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	s = devm_kzalloc(&client->dev, sizeof(*s), GFP_KERNEL);
	if (s == NULL)
		return -ENOMEM;

	s->buf_size = bufblocks * 3;

	s->buffer = devm_kzalloc(&client->dev, s->buf_size, GFP_KERNEL);
	if (s->buffer == NULL)
		return -ENOMEM;
	sd = &s->sd;
	v4l2_i2c_subdev_init(sd, client, &saa6588_ops);
	spin_lock_init(&s->lock);
	s->block_count = 0;
	s->wr_index = 0;
	s->rd_index = 0;
	s->last_blocknum = 0xff;
	init_waitqueue_head(&s->read_queue);
	s->data_available_for_read = 0;

	saa6588_configure(s);

	/* start polling via eventd */
	INIT_DELAYED_WORK(&s->work, saa6588_work);
	schedule_delayed_work(&s->work, 0);
	return 0;
}

static int saa6588_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct saa6588 *s = to_saa6588(sd);

	v4l2_device_unregister_subdev(sd);

	cancel_delayed_work_sync(&s->work);

	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id saa6588_id[] = {
	{ "saa6588", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, saa6588_id);

static struct i2c_driver saa6588_driver = {
	.driver = {
		.name	= "saa6588",
	},
	.probe		= saa6588_probe,
	.remove		= saa6588_remove,
	.id_table	= saa6588_id,
};

module_i2c_driver(saa6588_driver);
