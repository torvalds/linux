/* radio-cadet.c - A video4linux driver for the ADS Cadet AM/FM Radio Card
 *
 * by Fred Gleason <fredg@wava.com>
 * Version 0.3.3
 *
 * (Loosely) based on code for the Aztech radio card by
 *
 * Russell Kroll    (rkroll@exploits.org)
 * Quay Ly
 * Donald Song
 * Jason Lewis      (jlewis@twilight.vtc.vsc.edu)
 * Scott McGrath    (smcgrath@twilight.vtc.vsc.edu)
 * William McGrath  (wmcgrath@twilight.vtc.vsc.edu)
 *
 * History:
 * 2000-04-29	Russell Kroll <rkroll@exploits.org>
 *		Added ISAPnP detection for Linux 2.3/2.4
 *
 * 2001-01-10	Russell Kroll <rkroll@exploits.org>
 *		Removed dead CONFIG_RADIO_CADET_PORT code
 *		PnP detection on load is now default (no args necessary)
 *
 * 2002-01-17	Adam Belay <ambx1@neo.rr.com>
 *		Updated to latest pnp code
 *
 * 2003-01-31	Alan Cox <alan@lxorguk.ukuu.org.uk>
 *		Cleaned up locking, delay code, general odds and ends
 *
 * 2006-07-30	Hans J. Koch <koch@hjk-az.de>
 *		Changed API to V4L2
 */

#include <linux/version.h>
#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* udelay			*/
#include <asm/io.h>		/* outb, outb_p			*/
#include <asm/uaccess.h>	/* copy to/from user		*/
#include <linux/videodev2.h>	/* V4L2 API defs		*/
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <linux/param.h>
#include <linux/pnp.h>

#define RDS_BUFFER 256
#define RDS_RX_FLAG 1
#define MBS_RX_FLAG 2

#define CADET_VERSION KERNEL_VERSION(0,3,3)

static struct v4l2_queryctrl radio_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = 0,
		.maximum       = 0xff,
		.step          = 1,
		.default_value = 0xff,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}
};

static int io=-1;		/* default to isapnp activation */
static int radio_nr = -1;
static int users;
static int curtuner;
static int tunestat;
static int sigstrength;
static wait_queue_head_t read_queue;
static struct timer_list readtimer;
static __u8 rdsin, rdsout, rdsstat;
static unsigned char rdsbuf[RDS_BUFFER];
static spinlock_t cadet_io_lock;

static int cadet_probe(void);

/*
 * Signal Strength Threshold Values
 * The V4L API spec does not define any particular unit for the signal
 * strength value.  These values are in microvolts of RF at the tuner's input.
 */
static __u16 sigtable[2][4]={{5,10,30,150},{28,40,63,1000}};


static int
cadet_getstereo(void)
{
	int ret = V4L2_TUNER_SUB_MONO;
	if(curtuner != 0)	/* Only FM has stereo capability! */
		return V4L2_TUNER_SUB_MONO;

	spin_lock(&cadet_io_lock);
	outb(7,io);          /* Select tuner control */
	if( (inb(io+1) & 0x40) == 0)
		ret = V4L2_TUNER_SUB_STEREO;
	spin_unlock(&cadet_io_lock);
	return ret;
}

static unsigned
cadet_gettune(void)
{
	int curvol,i;
	unsigned fifo=0;

	/*
	 * Prepare for read
	 */

	spin_lock(&cadet_io_lock);

	outb(7,io);       /* Select tuner control */
	curvol=inb(io+1); /* Save current volume/mute setting */
	outb(0x00,io+1);  /* Ensure WRITE-ENABLE is LOW */
	tunestat=0xffff;

	/*
	 * Read the shift register
	 */
	for(i=0;i<25;i++) {
		fifo=(fifo<<1)|((inb(io+1)>>7)&0x01);
		if(i<24) {
			outb(0x01,io+1);
			tunestat&=inb(io+1);
			outb(0x00,io+1);
		}
	}

	/*
	 * Restore volume/mute setting
	 */
	outb(curvol,io+1);
	spin_unlock(&cadet_io_lock);

	return fifo;
}

static unsigned
cadet_getfreq(void)
{
	int i;
	unsigned freq=0,test,fifo=0;

	/*
	 * Read current tuning
	 */
	fifo=cadet_gettune();

	/*
	 * Convert to actual frequency
	 */
	if(curtuner==0) {    /* FM */
		test=12500;
		for(i=0;i<14;i++) {
			if((fifo&0x01)!=0) {
				freq+=test;
			}
			test=test<<1;
			fifo=fifo>>1;
		}
		freq-=10700000;           /* IF frequency is 10.7 MHz */
		freq=(freq*16)/1000000;   /* Make it 1/16 MHz */
	}
	if(curtuner==1) {    /* AM */
		freq=((fifo&0x7fff)-2010)*16;
	}

	return freq;
}

static void
cadet_settune(unsigned fifo)
{
	int i;
	unsigned test;

	spin_lock(&cadet_io_lock);

	outb(7,io);                /* Select tuner control */
	/*
	 * Write the shift register
	 */
	test=0;
	test=(fifo>>23)&0x02;      /* Align data for SDO */
	test|=0x1c;                /* SDM=1, SWE=1, SEN=1, SCK=0 */
	outb(7,io);                /* Select tuner control */
	outb(test,io+1);           /* Initialize for write */
	for(i=0;i<25;i++) {
		test|=0x01;              /* Toggle SCK High */
		outb(test,io+1);
		test&=0xfe;              /* Toggle SCK Low */
		outb(test,io+1);
		fifo=fifo<<1;            /* Prepare the next bit */
		test=0x1c|((fifo>>23)&0x02);
		outb(test,io+1);
	}
	spin_unlock(&cadet_io_lock);
}

static void
cadet_setfreq(unsigned freq)
{
	unsigned fifo;
	int i,j,test;
	int curvol;

	/*
	 * Formulate a fifo command
	 */
	fifo=0;
	if(curtuner==0) {    /* FM */
		test=102400;
		freq=(freq*1000)/16;       /* Make it kHz */
		freq+=10700;               /* IF is 10700 kHz */
		for(i=0;i<14;i++) {
			fifo=fifo<<1;
			if(freq>=test) {
				fifo|=0x01;
				freq-=test;
			}
			test=test>>1;
		}
	}
	if(curtuner==1) {    /* AM */
		fifo=(freq/16)+2010;            /* Make it kHz */
		fifo|=0x100000;            /* Select AM Band */
	}

	/*
	 * Save current volume/mute setting
	 */

	spin_lock(&cadet_io_lock);
	outb(7,io);                /* Select tuner control */
	curvol=inb(io+1);
	spin_unlock(&cadet_io_lock);

	/*
	 * Tune the card
	 */
	for(j=3;j>-1;j--) {
		cadet_settune(fifo|(j<<16));

		spin_lock(&cadet_io_lock);
		outb(7,io);         /* Select tuner control */
		outb(curvol,io+1);
		spin_unlock(&cadet_io_lock);

		msleep(100);

		cadet_gettune();
		if((tunestat & 0x40) == 0) {   /* Tuned */
			sigstrength=sigtable[curtuner][j];
			return;
		}
	}
	sigstrength=0;
}


static int
cadet_getvol(void)
{
	int ret = 0;

	spin_lock(&cadet_io_lock);

	outb(7,io);                /* Select tuner control */
	if((inb(io + 1) & 0x20) != 0)
		ret = 0xffff;

	spin_unlock(&cadet_io_lock);
	return ret;
}


static void
cadet_setvol(int vol)
{
	spin_lock(&cadet_io_lock);
	outb(7,io);                /* Select tuner control */
	if(vol>0)
		outb(0x20,io+1);
	else
		outb(0x00,io+1);
	spin_unlock(&cadet_io_lock);
}

static void
cadet_handler(unsigned long data)
{
	/*
	 * Service the RDS fifo
	 */

	if(spin_trylock(&cadet_io_lock))
	{
		outb(0x3,io);       /* Select RDS Decoder Control */
		if((inb(io+1)&0x20)!=0) {
			printk(KERN_CRIT "cadet: RDS fifo overflow\n");
		}
		outb(0x80,io);      /* Select RDS fifo */
		while((inb(io)&0x80)!=0) {
			rdsbuf[rdsin]=inb(io+1);
			if(rdsin==rdsout)
				printk(KERN_WARNING "cadet: RDS buffer overflow\n");
			else
				rdsin++;
		}
		spin_unlock(&cadet_io_lock);
	}

	/*
	 * Service pending read
	 */
	if( rdsin!=rdsout)
		wake_up_interruptible(&read_queue);

	/*
	 * Clean up and exit
	 */
	init_timer(&readtimer);
	readtimer.function=cadet_handler;
	readtimer.data=(unsigned long)0;
	readtimer.expires=jiffies+msecs_to_jiffies(50);
	add_timer(&readtimer);
}



static ssize_t
cadet_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	int i=0;
	unsigned char readbuf[RDS_BUFFER];

	if(rdsstat==0) {
		spin_lock(&cadet_io_lock);
		rdsstat=1;
		outb(0x80,io);        /* Select RDS fifo */
		spin_unlock(&cadet_io_lock);
		init_timer(&readtimer);
		readtimer.function=cadet_handler;
		readtimer.data=(unsigned long)0;
		readtimer.expires=jiffies+msecs_to_jiffies(50);
		add_timer(&readtimer);
	}
	if(rdsin==rdsout) {
		if (file->f_flags & O_NONBLOCK)
			return -EWOULDBLOCK;
		interruptible_sleep_on(&read_queue);
	}
	while( i<count && rdsin!=rdsout)
		readbuf[i++]=rdsbuf[rdsout++];

	if (copy_to_user(data,readbuf,i))
		return -EFAULT;
	return i;
}


static int vidioc_querycap(struct file *file, void *priv,
				struct v4l2_capability *v)
{
	v->capabilities =
		V4L2_CAP_TUNER |
		V4L2_CAP_READWRITE;
	v->version = CADET_VERSION;
	strcpy(v->driver, "ADS Cadet");
	strcpy(v->card, "ADS Cadet");
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	v->type = V4L2_TUNER_RADIO;
	switch (v->index) {
	case 0:
		strcpy(v->name, "FM");
		v->capability = V4L2_TUNER_CAP_STEREO;
		v->rangelow = 1400;     /* 87.5 MHz */
		v->rangehigh = 1728;    /* 108.0 MHz */
		v->rxsubchans=cadet_getstereo();
		switch (v->rxsubchans){
		case V4L2_TUNER_SUB_MONO:
			v->audmode = V4L2_TUNER_MODE_MONO;
			break;
		case V4L2_TUNER_SUB_STEREO:
			v->audmode = V4L2_TUNER_MODE_STEREO;
			break;
		default: ;
		}
		break;
	case 1:
		strcpy(v->name, "AM");
		v->capability = V4L2_TUNER_CAP_LOW;
		v->rangelow = 8320;      /* 520 kHz */
		v->rangehigh = 26400;    /* 1650 kHz */
		v->rxsubchans = V4L2_TUNER_SUB_MONO;
		v->audmode = V4L2_TUNER_MODE_MONO;
		break;
	default:
		return -EINVAL;
	}
	v->signal = sigstrength; /* We might need to modify scaling of this */
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	if((v->index != 0)&&(v->index != 1))
		return -EINVAL;
	curtuner = v->index;
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	f->tuner = curtuner;
	f->type = V4L2_TUNER_RADIO;
	f->frequency = cadet_getfreq();
	return 0;
}


static int vidioc_s_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	if (f->type != V4L2_TUNER_RADIO)
		return -EINVAL;
	if((curtuner==0)&&((f->frequency<1400)||(f->frequency>1728)))
		return -EINVAL;
	if((curtuner==1)&&((f->frequency<8320)||(f->frequency>26400)))
		return -EINVAL;
	cadet_setfreq(f->frequency);
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
				struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(radio_qctrl); i++) {
		if (qc->id && qc->id == radio_qctrl[i].id) {
			memcpy(qc, &(radio_qctrl[i]),
						sizeof(*qc));
			return 0;
		}
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	switch (ctrl->id){
	case V4L2_CID_AUDIO_MUTE: /* TODO: Handle this correctly */
		ctrl->value = (cadet_getvol() == 0);
		break;
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = cadet_getvol();
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	switch (ctrl->id){
	case V4L2_CID_AUDIO_MUTE: /* TODO: Handle this correctly */
		if (ctrl->value)
			cadet_setvol(0);
		else
			cadet_setvol(0xffff);
		break;
	case V4L2_CID_AUDIO_VOLUME:
		cadet_setvol(ctrl->value);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_g_audio(struct file *file, void *priv,
				struct v4l2_audio *a)
{
	if (a->index > 1)
		return -EINVAL;
	strcpy(a->name, "Radio");
	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *priv,
				struct v4l2_audio *a)
{
	if (a->index != 0)
		return -EINVAL;
	return 0;
}

static int
cadet_open(struct file *file)
{
	users++;
	if (1 == users) init_waitqueue_head(&read_queue);
	return 0;
}

static int
cadet_release(struct file *file)
{
	users--;
	if (0 == users){
		del_timer_sync(&readtimer);
		rdsstat=0;
	}
	return 0;
}

static unsigned int
cadet_poll(struct file *file, struct poll_table_struct *wait)
{
	poll_wait(file,&read_queue,wait);
	if(rdsin != rdsout)
		return POLLIN | POLLRDNORM;
	return 0;
}


static const struct v4l2_file_operations cadet_fops = {
	.owner		= THIS_MODULE,
	.open		= cadet_open,
	.release       	= cadet_release,
	.read		= cadet_read,
	.ioctl		= video_ioctl2,
	.poll		= cadet_poll,
};

static const struct v4l2_ioctl_ops cadet_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_queryctrl   = vidioc_queryctrl,
	.vidioc_g_ctrl      = vidioc_g_ctrl,
	.vidioc_s_ctrl      = vidioc_s_ctrl,
	.vidioc_g_audio     = vidioc_g_audio,
	.vidioc_s_audio     = vidioc_s_audio,
	.vidioc_g_input     = vidioc_g_input,
	.vidioc_s_input     = vidioc_s_input,
};

static struct video_device cadet_radio = {
	.name		= "Cadet radio",
	.fops           = &cadet_fops,
	.ioctl_ops 	= &cadet_ioctl_ops,
	.release	= video_device_release_empty,
};

#ifdef CONFIG_PNP

static struct pnp_device_id cadet_pnp_devices[] = {
	/* ADS Cadet AM/FM Radio Card */
	{.id = "MSM0c24", .driver_data = 0},
	{.id = ""}
};

MODULE_DEVICE_TABLE(pnp, cadet_pnp_devices);

static int cadet_pnp_probe(struct pnp_dev * dev, const struct pnp_device_id *dev_id)
{
	if (!dev)
		return -ENODEV;
	/* only support one device */
	if (io > 0)
		return -EBUSY;

	if (!pnp_port_valid(dev, 0)) {
		return -ENODEV;
	}

	io = pnp_port_start(dev, 0);

	printk ("radio-cadet: PnP reports device at %#x\n", io);

	return io;
}

static struct pnp_driver cadet_pnp_driver = {
	.name		= "radio-cadet",
	.id_table	= cadet_pnp_devices,
	.probe		= cadet_pnp_probe,
	.remove		= NULL,
};

#else
static struct pnp_driver cadet_pnp_driver;
#endif

static int cadet_probe(void)
{
	static int iovals[8]={0x330,0x332,0x334,0x336,0x338,0x33a,0x33c,0x33e};
	int i;

	for(i=0;i<8;i++) {
		io=iovals[i];
		if (request_region(io, 2, "cadet-probe")) {
			cadet_setfreq(1410);
			if(cadet_getfreq()==1410) {
				release_region(io, 2);
				return io;
			}
			release_region(io, 2);
		}
	}
	return -1;
}

/*
 * io should only be set if the user has used something like
 * isapnp (the userspace program) to initialize this card for us
 */

static int __init cadet_init(void)
{
	spin_lock_init(&cadet_io_lock);

	/*
	 *	If a probe was requested then probe ISAPnP first (safest)
	 */
	if (io < 0)
		pnp_register_driver(&cadet_pnp_driver);
	/*
	 *	If that fails then probe unsafely if probe is requested
	 */
	if(io < 0)
		io = cadet_probe ();

	/*
	 *	Else we bail out
	 */

	if(io < 0) {
#ifdef MODULE
		printk(KERN_ERR "You must set an I/O address with io=0x???\n");
#endif
		goto fail;
	}
	if (!request_region(io,2,"cadet"))
		goto fail;
	if (video_register_device(&cadet_radio, VFL_TYPE_RADIO, radio_nr) < 0) {
		release_region(io,2);
		goto fail;
	}
	printk(KERN_INFO "ADS Cadet Radio Card at 0x%x\n",io);
	return 0;
fail:
	pnp_unregister_driver(&cadet_pnp_driver);
	return -1;
}



MODULE_AUTHOR("Fred Gleason, Russell Kroll, Quay Lu, Donald Song, Jason Lewis, Scott McGrath, William McGrath");
MODULE_DESCRIPTION("A driver for the ADS Cadet AM/FM/RDS radio card.");
MODULE_LICENSE("GPL");

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of Cadet card (0x330,0x332,0x334,0x336,0x338,0x33a,0x33c,0x33e)");
module_param(radio_nr, int, 0);

static void __exit cadet_cleanup_module(void)
{
	video_unregister_device(&cadet_radio);
	release_region(io,2);
	pnp_unregister_driver(&cadet_pnp_driver);
}

module_init(cadet_init);
module_exit(cadet_cleanup_module);

