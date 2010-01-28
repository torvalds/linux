/*
 * Driver for the media bay on the PowerBook 3400 and 2400.
 *
 * Copyright (C) 1998 Paul Mackerras.
 *
 * Various evolutions by Benjamin Herrenschmidt & Henry Worth
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <asm/prom.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/mediabay.h>
#include <asm/sections.h>
#include <asm/ohare.h>
#include <asm/heathrow.h>
#include <asm/keylargo.h>
#include <linux/adb.h>
#include <linux/pmu.h>

#define MB_FCR32(bay, r)	((bay)->base + ((r) >> 2))
#define MB_FCR8(bay, r)		(((volatile u8 __iomem *)((bay)->base)) + (r))

#define MB_IN32(bay,r)		(in_le32(MB_FCR32(bay,r)))
#define MB_OUT32(bay,r,v)	(out_le32(MB_FCR32(bay,r), (v)))
#define MB_BIS(bay,r,v)		(MB_OUT32((bay), (r), MB_IN32((bay), r) | (v)))
#define MB_BIC(bay,r,v)		(MB_OUT32((bay), (r), MB_IN32((bay), r) & ~(v)))
#define MB_IN8(bay,r)		(in_8(MB_FCR8(bay,r)))
#define MB_OUT8(bay,r,v)	(out_8(MB_FCR8(bay,r), (v)))

struct media_bay_info;

struct mb_ops {
	char*	name;
	void	(*init)(struct media_bay_info *bay);
	u8	(*content)(struct media_bay_info *bay);
	void	(*power)(struct media_bay_info *bay, int on_off);
	int	(*setup_bus)(struct media_bay_info *bay, u8 device_id);
	void	(*un_reset)(struct media_bay_info *bay);
	void	(*un_reset_ide)(struct media_bay_info *bay);
};

struct media_bay_info {
	u32 __iomem			*base;
	int				content_id;
	int				state;
	int				last_value;
	int				value_count;
	int				timer;
	struct macio_dev		*mdev;
	struct mb_ops*			ops;
	int				index;
	int				cached_gpio;
	int				sleeping;
	int				user_lock;
	struct mutex			lock;
};

#define MAX_BAYS	2

static struct media_bay_info media_bays[MAX_BAYS];
static int media_bay_count = 0;

/*
 * Wait that number of ms between each step in normal polling mode
 */
#define MB_POLL_DELAY	25

/*
 * Consider the media-bay ID value stable if it is the same for
 * this number of milliseconds
 */
#define MB_STABLE_DELAY	100

/* Wait after powering up the media bay this delay in ms
 * timeout bumped for some powerbooks
 */
#define MB_POWER_DELAY	200

/*
 * Hold the media-bay reset signal true for this many ticks
 * after a device is inserted before releasing it.
 */
#define MB_RESET_DELAY	50

/*
 * Wait this long after the reset signal is released and before doing
 * further operations. After this delay, the IDE reset signal is released
 * too for an IDE device
 */
#define MB_SETUP_DELAY	100

/*
 * Wait this many ticks after an IDE device (e.g. CD-ROM) is inserted
 * (or until the device is ready) before calling into the driver
 */
#define MB_IDE_WAIT	1000

/*
 * States of a media bay
 */
enum {
	mb_empty = 0,		/* Idle */
	mb_powering_up,		/* power bit set, waiting MB_POWER_DELAY */
	mb_enabling_bay,	/* enable bits set, waiting MB_RESET_DELAY */
	mb_resetting,		/* reset bit unset, waiting MB_SETUP_DELAY */
	mb_ide_resetting,	/* IDE reset bit unser, waiting MB_IDE_WAIT */
	mb_up,			/* Media bay full */
	mb_powering_down	/* Powering down (avoid too fast down/up) */
};

#define MB_POWER_SOUND		0x08
#define MB_POWER_FLOPPY		0x04
#define MB_POWER_ATA		0x02
#define MB_POWER_PCI		0x01
#define MB_POWER_OFF		0x00

/*
 * Functions for polling content of media bay
 */
 
static u8
ohare_mb_content(struct media_bay_info *bay)
{
	return (MB_IN32(bay, OHARE_MBCR) >> 12) & 7;
}

static u8
heathrow_mb_content(struct media_bay_info *bay)
{
	return (MB_IN32(bay, HEATHROW_MBCR) >> 12) & 7;
}

static u8
keylargo_mb_content(struct media_bay_info *bay)
{
	int new_gpio;

	new_gpio = MB_IN8(bay, KL_GPIO_MEDIABAY_IRQ) & KEYLARGO_GPIO_INPUT_DATA;
	if (new_gpio) {
		bay->cached_gpio = new_gpio;
		return MB_NO;
	} else if (bay->cached_gpio != new_gpio) {
		MB_BIS(bay, KEYLARGO_MBCR, KL_MBCR_MB0_ENABLE);
		(void)MB_IN32(bay, KEYLARGO_MBCR);
		udelay(5);
		MB_BIC(bay, KEYLARGO_MBCR, 0x0000000F);
		(void)MB_IN32(bay, KEYLARGO_MBCR);
		udelay(5);
		bay->cached_gpio = new_gpio;
	}
	return (MB_IN32(bay, KEYLARGO_MBCR) >> 4) & 7;
}

/*
 * Functions for powering up/down the bay, puts the bay device
 * into reset state as well
 */

static void
ohare_mb_power(struct media_bay_info* bay, int on_off)
{
	if (on_off) {
		/* Power up device, assert it's reset line */
		MB_BIC(bay, OHARE_FCR, OH_BAY_RESET_N);
		MB_BIC(bay, OHARE_FCR, OH_BAY_POWER_N);
	} else {
		/* Disable all devices */
		MB_BIC(bay, OHARE_FCR, OH_BAY_DEV_MASK);
		MB_BIC(bay, OHARE_FCR, OH_FLOPPY_ENABLE);
		/* Cut power from bay, release reset line */
		MB_BIS(bay, OHARE_FCR, OH_BAY_POWER_N);
		MB_BIS(bay, OHARE_FCR, OH_BAY_RESET_N);
		MB_BIS(bay, OHARE_FCR, OH_IDE1_RESET_N);
	}
	MB_BIC(bay, OHARE_MBCR, 0x00000F00);
}

static void
heathrow_mb_power(struct media_bay_info* bay, int on_off)
{
	if (on_off) {
		/* Power up device, assert it's reset line */
		MB_BIC(bay, HEATHROW_FCR, HRW_BAY_RESET_N);
		MB_BIC(bay, HEATHROW_FCR, HRW_BAY_POWER_N);
	} else {
		/* Disable all devices */
		MB_BIC(bay, HEATHROW_FCR, HRW_BAY_DEV_MASK);
		MB_BIC(bay, HEATHROW_FCR, HRW_SWIM_ENABLE);
		/* Cut power from bay, release reset line */
		MB_BIS(bay, HEATHROW_FCR, HRW_BAY_POWER_N);
		MB_BIS(bay, HEATHROW_FCR, HRW_BAY_RESET_N);
		MB_BIS(bay, HEATHROW_FCR, HRW_IDE1_RESET_N);
	}
	MB_BIC(bay, HEATHROW_MBCR, 0x00000F00);
}

static void
keylargo_mb_power(struct media_bay_info* bay, int on_off)
{
	if (on_off) {
		/* Power up device, assert it's reset line */
            	MB_BIC(bay, KEYLARGO_MBCR, KL_MBCR_MB0_DEV_RESET);
            	MB_BIC(bay, KEYLARGO_MBCR, KL_MBCR_MB0_DEV_POWER);
	} else {
		/* Disable all devices */
		MB_BIC(bay, KEYLARGO_MBCR, KL_MBCR_MB0_DEV_MASK);
		MB_BIC(bay, KEYLARGO_FCR1, KL1_EIDE0_ENABLE);
		/* Cut power from bay, release reset line */
		MB_BIS(bay, KEYLARGO_MBCR, KL_MBCR_MB0_DEV_POWER);
		MB_BIS(bay, KEYLARGO_MBCR, KL_MBCR_MB0_DEV_RESET);
		MB_BIS(bay, KEYLARGO_FCR1, KL1_EIDE0_RESET_N);
	}
	MB_BIC(bay, KEYLARGO_MBCR, 0x0000000F);
}

/*
 * Functions for configuring the media bay for a given type of device,
 * enable the related busses
 */

static int
ohare_mb_setup_bus(struct media_bay_info* bay, u8 device_id)
{
	switch(device_id) {
		case MB_FD:
		case MB_FD1:
			MB_BIS(bay, OHARE_FCR, OH_BAY_FLOPPY_ENABLE);
			MB_BIS(bay, OHARE_FCR, OH_FLOPPY_ENABLE);
			return 0;
		case MB_CD:
			MB_BIC(bay, OHARE_FCR, OH_IDE1_RESET_N);
			MB_BIS(bay, OHARE_FCR, OH_BAY_IDE_ENABLE);
			return 0;
		case MB_PCI:
			MB_BIS(bay, OHARE_FCR, OH_BAY_PCI_ENABLE);
			return 0;
	}
	return -ENODEV;
}

static int
heathrow_mb_setup_bus(struct media_bay_info* bay, u8 device_id)
{
	switch(device_id) {
		case MB_FD:
		case MB_FD1:
			MB_BIS(bay, HEATHROW_FCR, HRW_BAY_FLOPPY_ENABLE);
			MB_BIS(bay, HEATHROW_FCR, HRW_SWIM_ENABLE);
			return 0;
		case MB_CD:
			MB_BIC(bay, HEATHROW_FCR, HRW_IDE1_RESET_N);
			MB_BIS(bay, HEATHROW_FCR, HRW_BAY_IDE_ENABLE);
			return 0;
		case MB_PCI:
			MB_BIS(bay, HEATHROW_FCR, HRW_BAY_PCI_ENABLE);
			return 0;
	}
	return -ENODEV;
}

static int
keylargo_mb_setup_bus(struct media_bay_info* bay, u8 device_id)
{
	switch(device_id) {
		case MB_CD:
			MB_BIS(bay, KEYLARGO_MBCR, KL_MBCR_MB0_IDE_ENABLE);
			MB_BIC(bay, KEYLARGO_FCR1, KL1_EIDE0_RESET_N);
			MB_BIS(bay, KEYLARGO_FCR1, KL1_EIDE0_ENABLE);
			return 0;
		case MB_PCI:
			MB_BIS(bay, KEYLARGO_MBCR, KL_MBCR_MB0_PCI_ENABLE);
			return 0;
		case MB_SOUND:
			MB_BIS(bay, KEYLARGO_MBCR, KL_MBCR_MB0_SOUND_ENABLE);
			return 0;
	}
	return -ENODEV;
}

/*
 * Functions for tweaking resets
 */

static void
ohare_mb_un_reset(struct media_bay_info* bay)
{
	MB_BIS(bay, OHARE_FCR, OH_BAY_RESET_N);
}

static void keylargo_mb_init(struct media_bay_info *bay)
{
	MB_BIS(bay, KEYLARGO_MBCR, KL_MBCR_MB0_ENABLE);
}

static void heathrow_mb_un_reset(struct media_bay_info* bay)
{
	MB_BIS(bay, HEATHROW_FCR, HRW_BAY_RESET_N);
}

static void keylargo_mb_un_reset(struct media_bay_info* bay)
{
	MB_BIS(bay, KEYLARGO_MBCR, KL_MBCR_MB0_DEV_RESET);
}

static void ohare_mb_un_reset_ide(struct media_bay_info* bay)
{
	MB_BIS(bay, OHARE_FCR, OH_IDE1_RESET_N);
}

static void heathrow_mb_un_reset_ide(struct media_bay_info* bay)
{
	MB_BIS(bay, HEATHROW_FCR, HRW_IDE1_RESET_N);
}

static void keylargo_mb_un_reset_ide(struct media_bay_info* bay)
{
	MB_BIS(bay, KEYLARGO_FCR1, KL1_EIDE0_RESET_N);
}

static inline void set_mb_power(struct media_bay_info* bay, int onoff)
{
	/* Power up up and assert the bay reset line */
	if (onoff) {
		bay->ops->power(bay, 1);
		bay->state = mb_powering_up;
		pr_debug("mediabay%d: powering up\n", bay->index);
	} else { 
		/* Make sure everything is powered down & disabled */
		bay->ops->power(bay, 0);
		bay->state = mb_powering_down;
		pr_debug("mediabay%d: powering down\n", bay->index);
	}
	bay->timer = msecs_to_jiffies(MB_POWER_DELAY);
}

static void poll_media_bay(struct media_bay_info* bay)
{
	int id = bay->ops->content(bay);

	static char *mb_content_types[] = {
		"a floppy drive",
		"a floppy drive",
		"an unsuported audio device",
		"an ATA device",
		"an unsupported PCI device",
		"an unknown device",
	};

	if (id != bay->last_value) {
		bay->last_value = id;
		bay->value_count = 0;
		return;
	}
	if (id == bay->content_id)
		return;

	bay->value_count += msecs_to_jiffies(MB_POLL_DELAY);
	if (bay->value_count >= msecs_to_jiffies(MB_STABLE_DELAY)) {
		/* If the device type changes without going thru
		 * "MB_NO", we force a pass by "MB_NO" to make sure
		 * things are properly reset
		 */
		if ((id != MB_NO) && (bay->content_id != MB_NO)) {
			id = MB_NO;
			pr_debug("mediabay%d: forcing MB_NO\n", bay->index);
		}
		pr_debug("mediabay%d: switching to %d\n", bay->index, id);
		set_mb_power(bay, id != MB_NO);
		bay->content_id = id;
		if (id >= MB_NO || id < 0)
			printk(KERN_INFO "mediabay%d: Bay is now empty\n", bay->index);
		else
			printk(KERN_INFO "mediabay%d: Bay contains %s\n",
			       bay->index, mb_content_types[id]);
	}
}

int check_media_bay(struct macio_dev *baydev)
{
	struct media_bay_info* bay;
	int id;

	if (baydev == NULL)
		return MB_NO;

	/* This returns an instant snapshot, not locking, sine
	 * we may be called with the bay lock held. The resulting
	 * fuzzyness of the result if called at the wrong time is
	 * not actually a huge deal
	 */
	bay = macio_get_drvdata(baydev);
	if (bay == NULL)
		return MB_NO;
	id = bay->content_id;
	if (bay->state != mb_up)
		return MB_NO;
	if (id == MB_FD1)
		return MB_FD;
	return id;
}
EXPORT_SYMBOL_GPL(check_media_bay);

void lock_media_bay(struct macio_dev *baydev)
{
	struct media_bay_info* bay;

	if (baydev == NULL)
		return;
	bay = macio_get_drvdata(baydev);
	if (bay == NULL)
		return;
	mutex_lock(&bay->lock);
	bay->user_lock = 1;
}
EXPORT_SYMBOL_GPL(lock_media_bay);

void unlock_media_bay(struct macio_dev *baydev)
{
	struct media_bay_info* bay;

	if (baydev == NULL)
		return;
	bay = macio_get_drvdata(baydev);
	if (bay == NULL)
		return;
	if (bay->user_lock) {
		bay->user_lock = 0;
		mutex_unlock(&bay->lock);
	}
}
EXPORT_SYMBOL_GPL(unlock_media_bay);

static int mb_broadcast_hotplug(struct device *dev, void *data)
{
	struct media_bay_info* bay = data;
	struct macio_dev *mdev;
	struct macio_driver *drv;
	int state;

	if (dev->bus != &macio_bus_type)
		return 0;

	state = bay->state == mb_up ? bay->content_id : MB_NO;
	if (state == MB_FD1)
		state = MB_FD;
	mdev = to_macio_device(dev);
	drv = to_macio_driver(dev->driver);
	if (dev->driver && drv->mediabay_event)
		drv->mediabay_event(mdev, state);
	return 0;
}

static void media_bay_step(int i)
{
	struct media_bay_info* bay = &media_bays[i];

	/* We don't poll when powering down */
	if (bay->state != mb_powering_down)
	    poll_media_bay(bay);

	/* If timer expired run state machine */
	if (bay->timer != 0) {
		bay->timer -= msecs_to_jiffies(MB_POLL_DELAY);
		if (bay->timer > 0)
			return;
		bay->timer = 0;
	}

	switch(bay->state) {
	case mb_powering_up:
	    	if (bay->ops->setup_bus(bay, bay->last_value) < 0) {
			pr_debug("mediabay%d: device not supported (kind:%d)\n",
				 i, bay->content_id);
	    		set_mb_power(bay, 0);
	    		break;
	    	}
	    	bay->timer = msecs_to_jiffies(MB_RESET_DELAY);
	    	bay->state = mb_enabling_bay;
		pr_debug("mediabay%d: enabling (kind:%d)\n", i, bay->content_id);
		break;
	case mb_enabling_bay:
		bay->ops->un_reset(bay);
	    	bay->timer = msecs_to_jiffies(MB_SETUP_DELAY);
	    	bay->state = mb_resetting;
		pr_debug("mediabay%d: releasing bay reset (kind:%d)\n",
			 i, bay->content_id);
	    	break;
	case mb_resetting:
		if (bay->content_id != MB_CD) {
			pr_debug("mediabay%d: bay is up (kind:%d)\n", i,
				 bay->content_id);
			bay->state = mb_up;
			device_for_each_child(&bay->mdev->ofdev.dev,
					      bay, mb_broadcast_hotplug);
			break;
	    	}
		pr_debug("mediabay%d: releasing ATA reset (kind:%d)\n",
			 i, bay->content_id);
		bay->ops->un_reset_ide(bay);
	    	bay->timer = msecs_to_jiffies(MB_IDE_WAIT);
	    	bay->state = mb_ide_resetting;
	    	break;

	case mb_ide_resetting:
		pr_debug("mediabay%d: bay is up (kind:%d)\n", i, bay->content_id);
		bay->state = mb_up;
		device_for_each_child(&bay->mdev->ofdev.dev,
				      bay, mb_broadcast_hotplug);
	    	break;

	case mb_powering_down:
	    	bay->state = mb_empty;
		device_for_each_child(&bay->mdev->ofdev.dev,
				      bay, mb_broadcast_hotplug);
		pr_debug("mediabay%d: end of power down\n", i);
	    	break;
	}
}

/*
 * This procedure runs as a kernel thread to poll the media bay
 * once each tick and register and unregister the IDE interface
 * with the IDE driver.  It needs to be a thread because
 * ide_register can't be called from interrupt context.
 */
static int media_bay_task(void *x)
{
	int	i;

	while (!kthread_should_stop()) {
		for (i = 0; i < media_bay_count; ++i) {
			mutex_lock(&media_bays[i].lock);
			if (!media_bays[i].sleeping)
				media_bay_step(i);
			mutex_unlock(&media_bays[i].lock);
		}

		msleep_interruptible(MB_POLL_DELAY);
	}
	return 0;
}

static int __devinit media_bay_attach(struct macio_dev *mdev, const struct of_device_id *match)
{
	struct media_bay_info* bay;
	u32 __iomem *regbase;
	struct device_node *ofnode;
	unsigned long base;
	int i;

	ofnode = mdev->ofdev.node;

	if (macio_resource_count(mdev) < 1)
		return -ENODEV;
	if (macio_request_resources(mdev, "media-bay"))
		return -EBUSY;
	/* Media bay registers are located at the beginning of the
         * mac-io chip, for now, we trick and align down the first
	 * resource passed in
         */
	base = macio_resource_start(mdev, 0) & 0xffff0000u;
	regbase = (u32 __iomem *)ioremap(base, 0x100);
	if (regbase == NULL) {
		macio_release_resources(mdev);
		return -ENOMEM;
	}
	
	i = media_bay_count++;
	bay = &media_bays[i];
	bay->mdev = mdev;
	bay->base = regbase;
	bay->index = i;
	bay->ops = match->data;
	bay->sleeping = 0;
	mutex_init(&bay->lock);

	/* Init HW probing */
	if (bay->ops->init)
		bay->ops->init(bay);

	printk(KERN_INFO "mediabay%d: Registered %s media-bay\n", i, bay->ops->name);

	/* Force an immediate detect */
	set_mb_power(bay, 0);
	msleep(MB_POWER_DELAY);
	bay->content_id = MB_NO;
	bay->last_value = bay->ops->content(bay);
	bay->value_count = msecs_to_jiffies(MB_STABLE_DELAY);
	bay->state = mb_empty;

	/* Mark us ready by filling our mdev data */
	macio_set_drvdata(mdev, bay);

	/* Startup kernel thread */
	if (i == 0)
		kthread_run(media_bay_task, NULL, "media-bay");

	return 0;

}

static int media_bay_suspend(struct macio_dev *mdev, pm_message_t state)
{
	struct media_bay_info	*bay = macio_get_drvdata(mdev);

	if (state.event != mdev->ofdev.dev.power.power_state.event
	    && (state.event & PM_EVENT_SLEEP)) {
		mutex_lock(&bay->lock);
		bay->sleeping = 1;
		set_mb_power(bay, 0);
		mutex_unlock(&bay->lock);
		msleep(MB_POLL_DELAY);
		mdev->ofdev.dev.power.power_state = state;
	}
	return 0;
}

static int media_bay_resume(struct macio_dev *mdev)
{
	struct media_bay_info	*bay = macio_get_drvdata(mdev);

	if (mdev->ofdev.dev.power.power_state.event != PM_EVENT_ON) {
		mdev->ofdev.dev.power.power_state = PMSG_ON;

	       	/* We re-enable the bay using it's previous content
	       	   only if it did not change. Note those bozo timings,
	       	   they seem to help the 3400 get it right.
	       	 */
	       	/* Force MB power to 0 */
		mutex_lock(&bay->lock);
	       	set_mb_power(bay, 0);
		msleep(MB_POWER_DELAY);
	       	if (bay->ops->content(bay) != bay->content_id) {
			printk("mediabay%d: Content changed during sleep...\n", bay->index);
			mutex_unlock(&bay->lock);
	       		return 0;
		}
	       	set_mb_power(bay, 1);
	       	bay->last_value = bay->content_id;
	       	bay->value_count = msecs_to_jiffies(MB_STABLE_DELAY);
	       	bay->timer = msecs_to_jiffies(MB_POWER_DELAY);
	       	do {
			msleep(MB_POLL_DELAY);
	       		media_bay_step(bay->index);
	       	} while((bay->state != mb_empty) &&
	       		(bay->state != mb_up));
		bay->sleeping = 0;
		mutex_unlock(&bay->lock);
	}
	return 0;
}


/* Definitions of "ops" structures.
 */
static struct mb_ops ohare_mb_ops = {
	.name		= "Ohare",
	.content	= ohare_mb_content,
	.power		= ohare_mb_power,
	.setup_bus	= ohare_mb_setup_bus,
	.un_reset	= ohare_mb_un_reset,
	.un_reset_ide	= ohare_mb_un_reset_ide,
};

static struct mb_ops heathrow_mb_ops = {
	.name		= "Heathrow",
	.content	= heathrow_mb_content,
	.power		= heathrow_mb_power,
	.setup_bus	= heathrow_mb_setup_bus,
	.un_reset	= heathrow_mb_un_reset,
	.un_reset_ide	= heathrow_mb_un_reset_ide,
};

static struct mb_ops keylargo_mb_ops = {
	.name		= "KeyLargo",
	.init		= keylargo_mb_init,
	.content	= keylargo_mb_content,
	.power		= keylargo_mb_power,
	.setup_bus	= keylargo_mb_setup_bus,
	.un_reset	= keylargo_mb_un_reset,
	.un_reset_ide	= keylargo_mb_un_reset_ide,
};

/*
 * It seems that the bit for the media-bay interrupt in the IRQ_LEVEL
 * register is always set when there is something in the media bay.
 * This causes problems for the interrupt code if we attach an interrupt
 * handler to the media-bay interrupt, because it tends to go into
 * an infinite loop calling the media bay interrupt handler.
 * Therefore we do it all by polling the media bay once each tick.
 */

static struct of_device_id media_bay_match[] =
{
	{
	.name		= "media-bay",
	.compatible	= "keylargo-media-bay",
	.data		= &keylargo_mb_ops,
	},
	{
	.name		= "media-bay",
	.compatible	= "heathrow-media-bay",
	.data		= &heathrow_mb_ops,
	},
	{
	.name		= "media-bay",
	.compatible	= "ohare-media-bay",
	.data		= &ohare_mb_ops,
	},
	{},
};

static struct macio_driver media_bay_driver =
{
	.name		= "media-bay",
	.match_table	= media_bay_match,
	.probe		= media_bay_attach,
	.suspend	= media_bay_suspend,
	.resume		= media_bay_resume
};

static int __init media_bay_init(void)
{
	int i;

	for (i=0; i<MAX_BAYS; i++) {
		memset((char *)&media_bays[i], 0, sizeof(struct media_bay_info));
		media_bays[i].content_id	= -1;
	}
	if (!machine_is(powermac))
		return 0;

	macio_register_driver(&media_bay_driver);	

	return 0;
}

device_initcall(media_bay_init);
