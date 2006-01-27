/*
 *
 * device driver for philips saa7134 based TV cards
 * oss dsp interface
 *
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *     2005 conversion to standalone module:
 *         Ricardo Cerqueira <v4l@cerqueira.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/sound.h>
#include <linux/soundcard.h>

#include "saa7134-reg.h"
#include "saa7134.h"

/* ------------------------------------------------------------------ */

static unsigned int debug  = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug,"enable debug messages [oss]");

static unsigned int rate  = 0;
module_param(rate, int, 0444);
MODULE_PARM_DESC(rate,"sample rate (valid are: 32000,48000)");

static unsigned int dsp_nr[]   = {[0 ... (SAA7134_MAXBOARDS - 1)] = UNSET };
MODULE_PARM_DESC(dsp_nr, "device numbers for SAA7134 capture interface(s).");
module_param_array(dsp_nr,   int, NULL, 0444);

static unsigned int mixer_nr[] = {[0 ... (SAA7134_MAXBOARDS - 1)] = UNSET };
MODULE_PARM_DESC(mixer_nr, "mixer numbers for SAA7134 capture interface(s).");
module_param_array(mixer_nr, int, NULL, 0444);

#define dprintk(fmt, arg...)	if (debug) \
	printk(KERN_DEBUG "%s/oss: " fmt, dev->name , ## arg)


/* ------------------------------------------------------------------ */

static int dsp_buffer_conf(struct saa7134_dev *dev, int blksize, int blocks)
{
	if (blksize < 0x100)
		blksize = 0x100;
	if (blksize > 0x10000)
		blksize = 0x10000;

	if (blocks < 2)
		blocks = 2;
	if ((blksize * blocks) > 1024*1024)
		blocks = 1024*1024 / blksize;

	dev->dmasound.blocks  = blocks;
	dev->dmasound.blksize = blksize;
	dev->dmasound.bufsize = blksize * blocks;

	dprintk("buffer config: %d blocks / %d bytes, %d kB total\n",
		blocks,blksize,blksize * blocks / 1024);
	return 0;
}

static int dsp_buffer_init(struct saa7134_dev *dev)
{
	int err;

	if (!dev->dmasound.bufsize)
		BUG();
	videobuf_dma_init(&dev->dmasound.dma);
	err = videobuf_dma_init_kernel(&dev->dmasound.dma, PCI_DMA_FROMDEVICE,
				       (dev->dmasound.bufsize + PAGE_SIZE) >> PAGE_SHIFT);
	if (0 != err)
		return err;
	return 0;
}

static int dsp_buffer_free(struct saa7134_dev *dev)
{
	if (!dev->dmasound.blksize)
		BUG();
	videobuf_dma_free(&dev->dmasound.dma);
	dev->dmasound.blocks  = 0;
	dev->dmasound.blksize = 0;
	dev->dmasound.bufsize = 0;
	return 0;
}

static void dsp_dma_start(struct saa7134_dev *dev)
{
	dev->dmasound.dma_blk     = 0;
	dev->dmasound.dma_running = 1;
	saa7134_set_dmabits(dev);
}

static void dsp_dma_stop(struct saa7134_dev *dev)
{
	dev->dmasound.dma_blk     = -1;
	dev->dmasound.dma_running = 0;
	saa7134_set_dmabits(dev);
}

static int dsp_rec_start(struct saa7134_dev *dev)
{
	int err, bswap, sign;
	u32 fmt, control;
	unsigned long flags;

	/* prepare buffer */
	if (0 != (err = videobuf_dma_pci_map(dev->pci,&dev->dmasound.dma)))
		return err;
	if (0 != (err = saa7134_pgtable_alloc(dev->pci,&dev->dmasound.pt)))
		goto fail1;
	if (0 != (err = saa7134_pgtable_build(dev->pci,&dev->dmasound.pt,
					      dev->dmasound.dma.sglist,
					      dev->dmasound.dma.sglen,
					      0)))
		goto fail2;

	/* sample format */
	switch (dev->dmasound.afmt) {
	case AFMT_U8:
	case AFMT_S8:     fmt = 0x00;  break;
	case AFMT_U16_LE:
	case AFMT_U16_BE:
	case AFMT_S16_LE:
	case AFMT_S16_BE: fmt = 0x01;  break;
	default:
		err = -EINVAL;
		goto fail2;
	}

	switch (dev->dmasound.afmt) {
	case AFMT_S8:
	case AFMT_S16_LE:
	case AFMT_S16_BE: sign = 1; break;
	default:          sign = 0; break;
	}

	switch (dev->dmasound.afmt) {
	case AFMT_U16_BE:
	case AFMT_S16_BE: bswap = 1; break;
	default:          bswap = 0; break;
	}

	switch (dev->pci->device) {
	case PCI_DEVICE_ID_PHILIPS_SAA7134:
		if (1 == dev->dmasound.channels)
			fmt |= (1 << 3);
		if (2 == dev->dmasound.channels)
			fmt |= (3 << 3);
		if (sign)
			fmt |= 0x04;
		fmt |= (TV == dev->dmasound.input) ? 0xc0 : 0x80;

		saa_writeb(SAA7134_NUM_SAMPLES0, ((dev->dmasound.blksize - 1) & 0x0000ff));
		saa_writeb(SAA7134_NUM_SAMPLES1, ((dev->dmasound.blksize - 1) & 0x00ff00) >>  8);
		saa_writeb(SAA7134_NUM_SAMPLES2, ((dev->dmasound.blksize - 1) & 0xff0000) >> 16);
		saa_writeb(SAA7134_AUDIO_FORMAT_CTRL, fmt);

		break;
	case PCI_DEVICE_ID_PHILIPS_SAA7133:
	case PCI_DEVICE_ID_PHILIPS_SAA7135:
		if (1 == dev->dmasound.channels)
			fmt |= (1 << 4);
		if (2 == dev->dmasound.channels)
			fmt |= (2 << 4);
		if (!sign)
			fmt |= 0x04;
		saa_writel(SAA7133_NUM_SAMPLES, dev->dmasound.blksize -4);
		saa_writel(SAA7133_AUDIO_CHANNEL, 0x543210 | (fmt << 24));
		break;
	}
	dprintk("rec_start: afmt=%d ch=%d  =>  fmt=0x%x swap=%c\n",
		dev->dmasound.afmt, dev->dmasound.channels, fmt,
		bswap ? 'b' : '-');

	/* dma: setup channel 6 (= AUDIO) */
	control = SAA7134_RS_CONTROL_BURST_16 |
		SAA7134_RS_CONTROL_ME |
		(dev->dmasound.pt.dma >> 12);
	if (bswap)
		control |= SAA7134_RS_CONTROL_BSWAP;
	saa_writel(SAA7134_RS_BA1(6),0);
	saa_writel(SAA7134_RS_BA2(6),dev->dmasound.blksize);
	saa_writel(SAA7134_RS_PITCH(6),0);
	saa_writel(SAA7134_RS_CONTROL(6),control);

	/* start dma */
	dev->dmasound.recording_on = 1;
	spin_lock_irqsave(&dev->slock,flags);
	dsp_dma_start(dev);
	spin_unlock_irqrestore(&dev->slock,flags);
	return 0;

 fail2:
	saa7134_pgtable_free(dev->pci,&dev->dmasound.pt);
 fail1:
	videobuf_dma_pci_unmap(dev->pci,&dev->dmasound.dma);
	return err;
}

static int dsp_rec_stop(struct saa7134_dev *dev)
{
	unsigned long flags;

	dprintk("rec_stop dma_blk=%d\n",dev->dmasound.dma_blk);

	/* stop dma */
	dev->dmasound.recording_on = 0;
	spin_lock_irqsave(&dev->slock,flags);
	dsp_dma_stop(dev);
	spin_unlock_irqrestore(&dev->slock,flags);

	/* unlock buffer */
	saa7134_pgtable_free(dev->pci,&dev->dmasound.pt);
	videobuf_dma_pci_unmap(dev->pci,&dev->dmasound.dma);
	return 0;
}

/* ------------------------------------------------------------------ */

static int dsp_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct saa7134_dev *h,*dev = NULL;
	struct list_head *list;
	int err;

	list_for_each(list,&saa7134_devlist) {
		h = list_entry(list, struct saa7134_dev, devlist);
		if (h->dmasound.minor_dsp == minor)
			dev = h;
	}
	if (NULL == dev)
		return -ENODEV;

	down(&dev->dmasound.lock);
	err = -EBUSY;
	if (dev->dmasound.users_dsp)
		goto fail1;
	dev->dmasound.users_dsp++;
	file->private_data = dev;

	dev->dmasound.afmt        = AFMT_U8;
	dev->dmasound.channels    = 1;
	dev->dmasound.read_count  = 0;
	dev->dmasound.read_offset = 0;
	dsp_buffer_conf(dev,PAGE_SIZE,64);
	err = dsp_buffer_init(dev);
	if (0 != err)
		goto fail2;

	up(&dev->dmasound.lock);
	return 0;

 fail2:
	dev->dmasound.users_dsp--;
 fail1:
	up(&dev->dmasound.lock);
	return err;
}

static int dsp_release(struct inode *inode, struct file *file)
{
	struct saa7134_dev *dev = file->private_data;

	down(&dev->dmasound.lock);
	if (dev->dmasound.recording_on)
		dsp_rec_stop(dev);
	dsp_buffer_free(dev);
	dev->dmasound.users_dsp--;
	file->private_data = NULL;
	up(&dev->dmasound.lock);
	return 0;
}

static ssize_t dsp_read(struct file *file, char __user *buffer,
			size_t count, loff_t *ppos)
{
	struct saa7134_dev *dev = file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	unsigned int bytes;
	unsigned long flags;
	int err,ret = 0;

	add_wait_queue(&dev->dmasound.wq, &wait);
	down(&dev->dmasound.lock);
	while (count > 0) {
		/* wait for data if needed */
		if (0 == dev->dmasound.read_count) {
			if (!dev->dmasound.recording_on) {
				err = dsp_rec_start(dev);
				if (err < 0) {
					if (0 == ret)
						ret = err;
					break;
				}
			}
			if (dev->dmasound.recording_on &&
			    !dev->dmasound.dma_running) {
				/* recover from overruns */
				spin_lock_irqsave(&dev->slock,flags);
				dsp_dma_start(dev);
				spin_unlock_irqrestore(&dev->slock,flags);
			}
			if (file->f_flags & O_NONBLOCK) {
				if (0 == ret)
					ret = -EAGAIN;
				break;
			}
			up(&dev->dmasound.lock);
			set_current_state(TASK_INTERRUPTIBLE);
			if (0 == dev->dmasound.read_count)
				schedule();
			set_current_state(TASK_RUNNING);
			down(&dev->dmasound.lock);
			if (signal_pending(current)) {
				if (0 == ret)
					ret = -EINTR;
				break;
			}
		}

		/* copy data to userspace */
		bytes = count;
		if (bytes > dev->dmasound.read_count)
			bytes = dev->dmasound.read_count;
		if (bytes > dev->dmasound.bufsize - dev->dmasound.read_offset)
			bytes = dev->dmasound.bufsize - dev->dmasound.read_offset;
		if (copy_to_user(buffer + ret,
				 dev->dmasound.dma.vmalloc + dev->dmasound.read_offset,
				 bytes)) {
			if (0 == ret)
				ret = -EFAULT;
			break;
		}

		ret   += bytes;
		count -= bytes;
		dev->dmasound.read_count  -= bytes;
		dev->dmasound.read_offset += bytes;
		if (dev->dmasound.read_offset == dev->dmasound.bufsize)
			dev->dmasound.read_offset = 0;
	}
	up(&dev->dmasound.lock);
	remove_wait_queue(&dev->dmasound.wq, &wait);
	return ret;
}

static ssize_t dsp_write(struct file *file, const char __user *buffer,
			 size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static const char *osspcm_ioctls[] = {
	"RESET", "SYNC", "SPEED", "STEREO", "GETBLKSIZE", "SETFMT",
	"CHANNELS", "?", "POST", "SUBDIVIDE", "SETFRAGMENT", "GETFMTS",
	"GETOSPACE", "GETISPACE", "NONBLOCK", "GETCAPS", "GET/SETTRIGGER",
	"GETIPTR", "GETOPTR", "MAPINBUF", "MAPOUTBUF", "SETSYNCRO",
	"SETDUPLEX", "GETODELAY"
};
#define OSSPCM_IOCTLS ARRAY_SIZE(osspcm_ioctls)

static void saa7134_oss_print_ioctl(char *name, unsigned int cmd)
{
	char *dir;

	switch (_IOC_DIR(cmd)) {
	case _IOC_NONE:              dir = "--"; break;
	case _IOC_READ:              dir = "r-"; break;
	case _IOC_WRITE:             dir = "-w"; break;
	case _IOC_READ | _IOC_WRITE: dir = "rw"; break;
	default:                     dir = "??"; break;
	}
	switch (_IOC_TYPE(cmd)) {
	case 'P':
		printk(KERN_DEBUG "%s: ioctl 0x%08x (oss dsp, %s, SNDCTL_DSP_%s)\n",
		       name, cmd, dir, (_IOC_NR(cmd) < OSSPCM_IOCTLS) ?
		       osspcm_ioctls[_IOC_NR(cmd)] : "???");
		break;
	case 'M':
		printk(KERN_DEBUG "%s: ioctl 0x%08x (oss mixer, %s, #%d)\n",
		       name, cmd, dir, _IOC_NR(cmd));
		break;
	default:
		printk(KERN_DEBUG "%s: ioctl 0x%08x (???, %s, #%d)\n",
		       name, cmd, dir, _IOC_NR(cmd));
	}
}

static int dsp_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct saa7134_dev *dev = file->private_data;
	void __user *argp = (void __user *) arg;
	int __user *p = argp;
	int val = 0;

	if (debug > 1)
		saa7134_oss_print_ioctl(dev->name,cmd);
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, p);
	case SNDCTL_DSP_GETCAPS:
		return 0;

	case SNDCTL_DSP_SPEED:
		if (get_user(val, p))
			return -EFAULT;
		/* fall through */
	case SOUND_PCM_READ_RATE:
		return put_user(dev->dmasound.rate, p);

	case SNDCTL_DSP_STEREO:
		if (get_user(val, p))
			return -EFAULT;
		down(&dev->dmasound.lock);
		dev->dmasound.channels = val ? 2 : 1;
		if (dev->dmasound.recording_on) {
			dsp_rec_stop(dev);
			dsp_rec_start(dev);
		}
		up(&dev->dmasound.lock);
		return put_user(dev->dmasound.channels-1, p);

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, p))
			return -EFAULT;
		if (val != 1 && val != 2)
			return -EINVAL;
		down(&dev->dmasound.lock);
		dev->dmasound.channels = val;
		if (dev->dmasound.recording_on) {
			dsp_rec_stop(dev);
			dsp_rec_start(dev);
		}
		up(&dev->dmasound.lock);
		/* fall through */
	case SOUND_PCM_READ_CHANNELS:
		return put_user(dev->dmasound.channels, p);

	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
		return put_user(AFMT_U8     | AFMT_S8     |
				AFMT_U16_LE | AFMT_U16_BE |
				AFMT_S16_LE | AFMT_S16_BE, p);

	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt */
		if (get_user(val, p))
			return -EFAULT;
		switch (val) {
		case AFMT_QUERY:
			/* nothing to do */
			break;
		case AFMT_U8:
		case AFMT_S8:
		case AFMT_U16_LE:
		case AFMT_U16_BE:
		case AFMT_S16_LE:
		case AFMT_S16_BE:
			down(&dev->dmasound.lock);
			dev->dmasound.afmt = val;
			if (dev->dmasound.recording_on) {
				dsp_rec_stop(dev);
				dsp_rec_start(dev);
			}
			up(&dev->dmasound.lock);
			return put_user(dev->dmasound.afmt, p);
		default:
			return -EINVAL;
		}

	case SOUND_PCM_READ_BITS:
		switch (dev->dmasound.afmt) {
		case AFMT_U8:
		case AFMT_S8:
			return put_user(8, p);
		case AFMT_U16_LE:
		case AFMT_U16_BE:
		case AFMT_S16_LE:
		case AFMT_S16_BE:
			return put_user(16, p);
		default:
			return -EINVAL;
		}

	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_RESET:
		down(&dev->dmasound.lock);
		if (dev->dmasound.recording_on)
			dsp_rec_stop(dev);
		up(&dev->dmasound.lock);
		return 0;
	case SNDCTL_DSP_GETBLKSIZE:
		return put_user(dev->dmasound.blksize, p);

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, p))
			return -EFAULT;
		if (dev->dmasound.recording_on)
			return -EBUSY;
		dsp_buffer_free(dev);
		/* used to be arg >> 16 instead of val >> 16; fixed */
		dsp_buffer_conf(dev,1 << (val & 0xffff), (val >> 16) & 0xffff);
		dsp_buffer_init(dev);
		return 0;

	case SNDCTL_DSP_SYNC:
		/* NOP */
		return 0;

	case SNDCTL_DSP_GETISPACE:
	{
		audio_buf_info info;
		info.fragsize   = dev->dmasound.blksize;
		info.fragstotal = dev->dmasound.blocks;
		info.bytes      = dev->dmasound.read_count;
		info.fragments  = info.bytes / info.fragsize;
		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	default:
		return -EINVAL;
	}
}

static unsigned int dsp_poll(struct file *file, struct poll_table_struct *wait)
{
	struct saa7134_dev *dev = file->private_data;
	unsigned int mask = 0;

	poll_wait(file, &dev->dmasound.wq, wait);

	if (0 == dev->dmasound.read_count) {
		down(&dev->dmasound.lock);
		if (!dev->dmasound.recording_on)
			dsp_rec_start(dev);
		up(&dev->dmasound.lock);
	} else
		mask |= (POLLIN | POLLRDNORM);
	return mask;
}

struct file_operations saa7134_dsp_fops = {
	.owner   = THIS_MODULE,
	.open    = dsp_open,
	.release = dsp_release,
	.read    = dsp_read,
	.write   = dsp_write,
	.ioctl   = dsp_ioctl,
	.poll    = dsp_poll,
	.llseek  = no_llseek,
};

/* ------------------------------------------------------------------ */

static int
mixer_recsrc_7134(struct saa7134_dev *dev)
{
	int analog_io,rate;

	switch (dev->dmasound.input) {
	case TV:
		saa_andorb(SAA7134_AUDIO_FORMAT_CTRL, 0xc0, 0xc0);
		saa_andorb(SAA7134_SIF_SAMPLE_FREQ,   0x03, 0x00);
		break;
	case LINE1:
	case LINE2:
	case LINE2_LEFT:
		analog_io = (LINE1 == dev->dmasound.input) ? 0x00 : 0x08;
		rate = (32000 == dev->dmasound.rate) ? 0x01 : 0x03;
		saa_andorb(SAA7134_ANALOG_IO_SELECT,  0x08, analog_io);
		saa_andorb(SAA7134_AUDIO_FORMAT_CTRL, 0xc0, 0x80);
		saa_andorb(SAA7134_SIF_SAMPLE_FREQ,   0x03, rate);
		break;
	}
	return 0;
}

static int
mixer_recsrc_7133(struct saa7134_dev *dev)
{
	u32 anabar, xbarin;

	xbarin = 0x03; // adc
    anabar = 0;
	switch (dev->dmasound.input) {
	case TV:
		xbarin = 0; // Demodulator
	anabar = 2; // DACs
		break;
	case LINE1:
		anabar = 0;  // aux1, aux1
		break;
	case LINE2:
	case LINE2_LEFT:
		anabar = 9;  // aux2, aux2
		break;
	}
    /* output xbar always main channel */
	saa_dsp_writel(dev, 0x46c >> 2, 0xbbbb10);
	saa_dsp_writel(dev, 0x464 >> 2, xbarin);
	saa_writel(0x594 >> 2, anabar);

	return 0;
}

static int
mixer_recsrc(struct saa7134_dev *dev, enum saa7134_audio_in src)
{
	static const char *iname[] = { "Oops", "TV", "LINE1", "LINE2" };

	dev->dmasound.count++;
	dev->dmasound.input = src;
	dprintk("mixer input = %s\n",iname[dev->dmasound.input]);

	switch (dev->pci->device) {
	case PCI_DEVICE_ID_PHILIPS_SAA7134:
		mixer_recsrc_7134(dev);
		break;
	case PCI_DEVICE_ID_PHILIPS_SAA7133:
	case PCI_DEVICE_ID_PHILIPS_SAA7135:
		mixer_recsrc_7133(dev);
		break;
	}
	return 0;
}

static int
mixer_level(struct saa7134_dev *dev, enum saa7134_audio_in src, int level)
{
	switch (dev->pci->device) {
	case PCI_DEVICE_ID_PHILIPS_SAA7134:
		switch (src) {
		case TV:
			/* nothing */
			break;
		case LINE1:
			saa_andorb(SAA7134_ANALOG_IO_SELECT,  0x10,
				   (100 == level) ? 0x00 : 0x10);
			break;
		case LINE2:
		case LINE2_LEFT:
			saa_andorb(SAA7134_ANALOG_IO_SELECT,  0x20,
				   (100 == level) ? 0x00 : 0x20);
			break;
		}
		break;
	case PCI_DEVICE_ID_PHILIPS_SAA7133:
	case PCI_DEVICE_ID_PHILIPS_SAA7135:
		/* nothing */
		break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */

static int mixer_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct saa7134_dev *h,*dev = NULL;
	struct list_head *list;

	list_for_each(list,&saa7134_devlist) {
		h = list_entry(list, struct saa7134_dev, devlist);
		if (h->dmasound.minor_mixer == minor)
			dev = h;
	}
	if (NULL == dev)
		return -ENODEV;

	file->private_data = dev;
	return 0;
}

static int mixer_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static int mixer_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct saa7134_dev *dev = file->private_data;
	enum saa7134_audio_in input;
	int val,ret;
	void __user *argp = (void __user *) arg;
	int __user *p = argp;

	if (debug > 1)
		saa7134_oss_print_ioctl(dev->name,cmd);
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, p);
	case SOUND_MIXER_INFO:
	{
		mixer_info info;
		memset(&info,0,sizeof(info));
		strlcpy(info.id,   "TV audio", sizeof(info.id));
		strlcpy(info.name, dev->name,  sizeof(info.name));
		info.modify_counter = dev->dmasound.count;
		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	case SOUND_OLD_MIXER_INFO:
	{
		_old_mixer_info info;
		memset(&info,0,sizeof(info));
		strlcpy(info.id,   "TV audio", sizeof(info.id));
		strlcpy(info.name, dev->name,  sizeof(info.name));
		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	case MIXER_READ(SOUND_MIXER_CAPS):
		return put_user(SOUND_CAP_EXCL_INPUT, p);
	case MIXER_READ(SOUND_MIXER_STEREODEVS):
		return put_user(0, p);
	case MIXER_READ(SOUND_MIXER_RECMASK):
	case MIXER_READ(SOUND_MIXER_DEVMASK):
		val = SOUND_MASK_LINE1 | SOUND_MASK_LINE2;
		if (32000 == dev->dmasound.rate)
			val |= SOUND_MASK_VIDEO;
		return put_user(val, p);

	case MIXER_WRITE(SOUND_MIXER_RECSRC):
		if (get_user(val, p))
			return -EFAULT;
		input = dev->dmasound.input;
		if (32000 == dev->dmasound.rate  &&
		    val & SOUND_MASK_VIDEO  &&  dev->dmasound.input != TV)
			input = TV;
		if (val & SOUND_MASK_LINE1  &&  dev->dmasound.input != LINE1)
			input = LINE1;
		if (val & SOUND_MASK_LINE2  &&  dev->dmasound.input != LINE2)
			input = LINE2;
		if (input != dev->dmasound.input)
			mixer_recsrc(dev,input);
		/* fall throuth */
	case MIXER_READ(SOUND_MIXER_RECSRC):
		switch (dev->dmasound.input) {
		case TV:    ret = SOUND_MASK_VIDEO; break;
		case LINE1: ret = SOUND_MASK_LINE1; break;
		case LINE2: ret = SOUND_MASK_LINE2; break;
		default:    ret = 0;
		}
		return put_user(ret, p);

	case MIXER_WRITE(SOUND_MIXER_VIDEO):
	case MIXER_READ(SOUND_MIXER_VIDEO):
		if (32000 != dev->dmasound.rate)
			return -EINVAL;
		return put_user(100 | 100 << 8, p);

	case MIXER_WRITE(SOUND_MIXER_LINE1):
		if (get_user(val, p))
			return -EFAULT;
		val &= 0xff;
		val = (val <= 50) ? 50 : 100;
		dev->dmasound.line1 = val;
		mixer_level(dev,LINE1,dev->dmasound.line1);
		/* fall throuth */
	case MIXER_READ(SOUND_MIXER_LINE1):
		return put_user(dev->dmasound.line1 | dev->dmasound.line1 << 8, p);

	case MIXER_WRITE(SOUND_MIXER_LINE2):
		if (get_user(val, p))
			return -EFAULT;
		val &= 0xff;
		val = (val <= 50) ? 50 : 100;
		dev->dmasound.line2 = val;
		mixer_level(dev,LINE2,dev->dmasound.line2);
		/* fall throuth */
	case MIXER_READ(SOUND_MIXER_LINE2):
		return put_user(dev->dmasound.line2 | dev->dmasound.line2 << 8, p);

	default:
		return -EINVAL;
	}
}

struct file_operations saa7134_mixer_fops = {
	.owner   = THIS_MODULE,
	.open    = mixer_open,
	.release = mixer_release,
	.ioctl   = mixer_ioctl,
	.llseek  = no_llseek,
};

/* ------------------------------------------------------------------ */

static irqreturn_t saa7134_oss_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct saa7134_dmasound *dmasound = dev_id;
	struct saa7134_dev *dev = dmasound->priv_data;
	unsigned long report, status;
	int loop, handled = 0;

	for (loop = 0; loop < 10; loop++) {
		report = saa_readl(SAA7134_IRQ_REPORT);
		status = saa_readl(SAA7134_IRQ_STATUS);

		if (report & SAA7134_IRQ_REPORT_DONE_RA3) {
			handled = 1;
			saa_writel(SAA7134_IRQ_REPORT,report);
			saa7134_irq_oss_done(dev, status);
		} else {
			goto out;
		}
	}

	if (loop == 10) {
		dprintk("error! looping IRQ!");
	}
out:
	return IRQ_RETVAL(handled);
}

int saa7134_oss_init1(struct saa7134_dev *dev)
{

	if ((request_irq(dev->pci->irq, saa7134_oss_irq,
			 SA_SHIRQ | SA_INTERRUPT, dev->name,
			(void*) &dev->dmasound)) < 0)
		return -1;

	/* general */
	init_MUTEX(&dev->dmasound.lock);
	init_waitqueue_head(&dev->dmasound.wq);

	switch (dev->pci->device) {
	case PCI_DEVICE_ID_PHILIPS_SAA7133:
	case PCI_DEVICE_ID_PHILIPS_SAA7135:
		saa_writel(0x588 >> 2, 0x00000fff);
		saa_writel(0x58c >> 2, 0x00543210);
		saa_dsp_writel(dev, 0x46c >> 2, 0xbbbbbb);
		break;
	}

	/* dsp */
	dev->dmasound.rate = 32000;
	if (rate)
		dev->dmasound.rate = rate;
	dev->dmasound.rate = (dev->dmasound.rate > 40000) ? 48000 : 32000;

	/* mixer */
	dev->dmasound.line1 = 50;
	dev->dmasound.line2 = 50;
	mixer_level(dev,LINE1,dev->dmasound.line1);
	mixer_level(dev,LINE2,dev->dmasound.line2);
	mixer_recsrc(dev, (dev->dmasound.rate == 32000) ? TV : LINE2);

	return 0;
}

int saa7134_oss_fini(struct saa7134_dev *dev)
{
	/* nothing */
	return 0;
}

void saa7134_irq_oss_done(struct saa7134_dev *dev, unsigned long status)
{
	int next_blk, reg = 0;

	spin_lock(&dev->slock);
	if (UNSET == dev->dmasound.dma_blk) {
		dprintk("irq: recording stopped\n");
		goto done;
	}
	if (0 != (status & 0x0f000000))
		dprintk("irq: lost %ld\n", (status >> 24) & 0x0f);
	if (0 == (status & 0x10000000)) {
		/* odd */
		if (0 == (dev->dmasound.dma_blk & 0x01))
			reg = SAA7134_RS_BA1(6);
	} else {
		/* even */
		if (1 == (dev->dmasound.dma_blk & 0x01))
			reg = SAA7134_RS_BA2(6);
	}
	if (0 == reg) {
		dprintk("irq: field oops [%s]\n",
			(status & 0x10000000) ? "even" : "odd");
		goto done;
	}
	if (dev->dmasound.read_count >= dev->dmasound.blksize * (dev->dmasound.blocks-2)) {
		dprintk("irq: overrun [full=%d/%d]\n",dev->dmasound.read_count,
			dev->dmasound.bufsize);
		dsp_dma_stop(dev);
		goto done;
	}

	/* next block addr */
	next_blk = (dev->dmasound.dma_blk + 2) % dev->dmasound.blocks;
	saa_writel(reg,next_blk * dev->dmasound.blksize);
	if (debug > 2)
		dprintk("irq: ok, %s, next_blk=%d, addr=%x\n",
			(status & 0x10000000) ? "even" : "odd ", next_blk,
			next_blk * dev->dmasound.blksize);

	/* update status & wake waiting readers */
	dev->dmasound.dma_blk = (dev->dmasound.dma_blk + 1) % dev->dmasound.blocks;
	dev->dmasound.read_count += dev->dmasound.blksize;
	wake_up(&dev->dmasound.wq);

 done:
	spin_unlock(&dev->slock);
}

static int saa7134_dsp_create(struct saa7134_dev *dev)
{
	int err;

	err = dev->dmasound.minor_dsp =
		register_sound_dsp(&saa7134_dsp_fops,
				   dsp_nr[dev->nr]);
	if (err < 0) {
		goto fail;
	}
	printk(KERN_INFO "%s: registered device dsp%d\n",
	       dev->name,dev->dmasound.minor_dsp >> 4);

	err = dev->dmasound.minor_mixer =
		register_sound_mixer(&saa7134_mixer_fops,
				     mixer_nr[dev->nr]);
	if (err < 0)
		goto fail;
	printk(KERN_INFO "%s: registered device mixer%d\n",
	       dev->name,dev->dmasound.minor_mixer >> 4);

	return 0;

fail:
	unregister_sound_dsp(dev->dmasound.minor_dsp);
	return 0;


}

static int oss_device_init(struct saa7134_dev *dev)
{
	dev->dmasound.priv_data = dev;
	saa7134_oss_init1(dev);
	saa7134_dsp_create(dev);
	return 1;
}

static int oss_device_exit(struct saa7134_dev *dev)
{

	unregister_sound_mixer(dev->dmasound.minor_mixer);
	unregister_sound_dsp(dev->dmasound.minor_dsp);

	saa7134_oss_fini(dev);

	if (dev->pci->irq > 0) {
		synchronize_irq(dev->pci->irq);
		free_irq(dev->pci->irq,&dev->dmasound);
	}

	dev->dmasound.priv_data = NULL;
	return 1;
}

static int saa7134_oss_init(void)
{
	struct saa7134_dev *dev = NULL;
	struct list_head *list;

	if (!dmasound_init && !dmasound_exit) {
		dmasound_init = oss_device_init;
		dmasound_exit = oss_device_exit;
	} else {
		printk(KERN_WARNING "saa7134 OSS: can't load, DMA sound handler already assigned (probably to ALSA)\n");
		return -EBUSY;
	}

	printk(KERN_INFO "saa7134 OSS driver for DMA sound loaded\n");


	list_for_each(list,&saa7134_devlist) {
		dev = list_entry(list, struct saa7134_dev, devlist);
		if (dev->dmasound.priv_data == NULL) {
			oss_device_init(dev);
		} else {
			printk(KERN_ERR "saa7134 OSS: DMA sound is being handled by ALSA, ignoring %s\n",dev->name);
			return -EBUSY;
		}
	}

	if (dev == NULL)
		printk(KERN_INFO "saa7134 OSS: no saa7134 cards found\n");

	return 0;

}

static void saa7134_oss_exit(void)
{
	struct saa7134_dev *dev = NULL;
	struct list_head *list;

	list_for_each(list,&saa7134_devlist) {
		dev = list_entry(list, struct saa7134_dev, devlist);

		/* Device isn't registered by OSS, probably ALSA's */
		if (!dev->dmasound.minor_dsp)
			continue;

		oss_device_exit(dev);

	}

	dmasound_init = NULL;
	dmasound_exit = NULL;

	printk(KERN_INFO "saa7134 OSS driver for DMA sound unloaded\n");

	return;
}

/* We initialize this late, to make sure the sound system is up and running */
late_initcall(saa7134_oss_init);
module_exit(saa7134_oss_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
