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
#include <asm/uaccess.h>

#include <media/rds.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-i2c-drv.h>


/* insmod options */
static unsigned int debug;
static unsigned int xtal;
static unsigned int rbds;
static unsigned int plvl;
static unsigned int bufblocks = 100;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages");
module_param(xtal, int, 0);
MODULE_PARM_DESC(xtal, "select oscillator frequency (0..3), default 0");
module_param(rbds, int, 0);
MODULE_PARM_DESC(rbds, "select mode, 0=RDS, 1=RBDS, default 0");
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

static int block_to_user_buf(struct saa6588 *s, unsigned char __user *user_buf)
{
	int i;

	if (s->rd_index == s->wr_index) {
		if (debug > 2)
			dprintk(PREFIX "Read: buffer empty.\n");
		return 0;
	}

	if (debug > 2) {
		dprintk(PREFIX "Read: ");
		for (i = s->rd_index; i < s->rd_index + 3; i++)
			dprintk("0x%02x ", s->buffer[i]);
	}

	if (copy_to_user(user_buf, &s->buffer[s->rd_index], 3))
		return -EFAULT;

	s->rd_index += 3;
	if (s->rd_index >= s->buf_size)
		s->rd_index = 0;
	s->block_count--;

	if (debug > 2)
		dprintk("%d blocks total.\n", s->block_count);

	return 1;
}

static void read_from_buf(struct saa6588 *s, struct rds_command *a)
{
	unsigned long flags;

	unsigned char __user *buf_ptr = a->buffer;
	unsigned int i;
	unsigned int rd_blocks;

	a->result = 0;
	if (!a->buffer)
		return;

	while (!s->data_available_for_read) {
		int ret = wait_event_interruptible(s->read_queue,
					     s->data_available_for_read);
		if (ret == -ERESTARTSYS) {
			a->result = -EINTR;
			return;
		}
	}

	spin_lock_irqsave(&s->lock, flags);
	rd_blocks = a->block_count;
	if (rd_blocks > s->block_count)
		rd_blocks = s->block_count;

	if (!rd_blocks) {
		spin_unlock_irqrestore(&s->lock, flags);
		return;
	}

	for (i = 0; i < rd_blocks; i++) {
		if (block_to_user_buf(s, buf_ptr)) {
			buf_ptr += 3;
			a->result++;
		} else
			break;
	}
	a->result *= 3;
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
	   SAA6588 returns garbage otherwise */
	if (6 != i2c_master_recv(client, &tmpbuf[0], 6)) {
		if (debug > 1)
			dprintk(PREFIX "read error!\n");
		return;
	}

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
	   Bits 5-3: Received Offset. Indicates the offset received
	   by the sync system.
	   Bits 2-0: Offset Name. Indicates the offset applied to this data.

	   SAA6588 byte order is Status-MSB-LSB, so we have to swap the
	   first and the last of the 3 bytes block.
	 */

	tmp = tmpbuf[2];
	tmpbuf[2] = tmpbuf[0];
	tmpbuf[0] = tmp;

	tmp = blocknum;
	tmp |= blocknum << 3;	/* Received offset == Offset Name (OK ?) */
	if ((tmpbuf[2] & 0x03) == 0x03)
		tmp |= 0x80;	/* uncorrectable error */
	else if ((tmpbuf[2] & 0x03) != 0x00)
		tmp |= 0x40;	/* corrected error */
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

static int saa6588_configure(struct saa6588 *s)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s->sd);
	unsigned char buf[3];
	int rc;

	buf[0] = cSyncRestart;
	if (rbds)
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

	return 0;
}

/* ---------------------------------------------------------------------- */

static long saa6588_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct saa6588 *s = to_saa6588(sd);
	struct rds_command *a = arg;

	switch (cmd) {
		/* --- open() for /dev/radio --- */
	case RDS_CMD_OPEN:
		a->result = 0;	/* return error if chip doesn't work ??? */
		break;
		/* --- close() for /dev/radio --- */
	case RDS_CMD_CLOSE:
		s->data_available_for_read = 1;
		wake_up_interruptible(&s->read_queue);
		a->result = 0;
		break;
		/* --- read() for /dev/radio --- */
	case RDS_CMD_READ:
		read_from_buf(s, a);
		break;
		/* --- poll() for /dev/radio --- */
	case RDS_CMD_POLL:
		a->result = 0;
		if (s->data_available_for_read) {
			a->result |= POLLIN | POLLRDNORM;
		}
		poll_wait(a->instance, &s->read_queue, a->event_list);
		break;

	default:
		/* nothing */
		return -ENOIOCTLCMD;
	}
	return 0;
}

static int saa6588_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_SAA6588, 0);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops saa6588_core_ops = {
	.g_chip_ident = saa6588_g_chip_ident,
	.ioctl = saa6588_ioctl,
};

static const struct v4l2_subdev_ops saa6588_ops = {
	.core = &saa6588_core_ops,
};

/* ---------------------------------------------------------------------- */

static int saa6588_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct saa6588 *s;
	struct v4l2_subdev *sd;

	v4l_info(client, "saa6588 found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (s == NULL)
		return -ENOMEM;

	s->buf_size = bufblocks * 3;

	s->buffer = kmalloc(s->buf_size, GFP_KERNEL);
	if (s->buffer == NULL) {
		kfree(s);
		return -ENOMEM;
	}
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

	kfree(s->buffer);
	kfree(s);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id saa6588_id[] = {
	{ "saa6588", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, saa6588_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "saa6588",
	.probe = saa6588_probe,
	.remove = saa6588_remove,
	.id_table = saa6588_id,
};
