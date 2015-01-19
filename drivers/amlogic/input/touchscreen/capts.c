/*
 * linux/drivers/input/touchscreen/capts.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <mach/gpio.h>
#include <linux/capts.h>


#define MULTI_TOUCH

#define capts_debug_info//  printk

/* periodic polling delay and period */
#define TS_POLL_DELAY     (1 * 1000000)
#define TS_POLL_PERIOD   (10 * 1000000)

struct capts {
    struct device *dev;
    struct input_dev *input;
    spinlock_t lock;
    int irq;
    struct hrtimer timer;
    struct work_struct work;
    struct workqueue_struct *workqueue;
    struct ts_chip *chip;
    struct ts_platform_data *pdata;
    struct ts_event event[EVENT_MAX];
    int event_num;
    unsigned short use_attr_group :1;
    unsigned short pending :1;
    unsigned short is_suspended :1;
};

int capts_irq_type[TS_MODE_NUM] = {
    IRQF_TRIGGER_FALLING,
    IRQF_TRIGGER_RISING,
    IRQF_TRIGGER_LOW,
    IRQF_TRIGGER_HIGH,
    IRQF_TRIGGER_NONE,
    IRQF_TRIGGER_NONE,
    IRQF_TRIGGER_NONE,
};

static inline int capts_get_fingdown_state(struct capts *ts)
{
    int ret = 1;
    if (ts->pdata->mode < TS_MODE_TIMER_READ) {
        ret = ts->pdata->get_irq_level();
        ret = !(ret ^ (ts->pdata->mode & 1));
    }
    return ret;
}

static struct input_dev* capts_register_input(const char *name, struct ts_info *info)
{
    struct input_dev *input;
    
    input = input_allocate_device();
    if (input) { 
        input->name = name;
    
#ifdef MULTI_TOUCH
        /* multi touch need only EV_ABS */
        set_bit(EV_ABS, input->evbit);
        set_bit(ABS_MT_POSITION_X, input->absbit);
        set_bit(ABS_MT_POSITION_Y, input->absbit);
        /* sim with ABS_PRESSURE in single touch */
        set_bit(ABS_MT_TOUCH_MAJOR, input->absbit);
        /* sim with ABS_TOOL_WIDTH in single touch */
        set_bit(ABS_MT_WIDTH_MAJOR, input->absbit);
        input_set_abs_params(input, ABS_MT_POSITION_X, info->xmin, info->xmax, 0, 0);
        input_set_abs_params(input, ABS_MT_POSITION_Y, info->ymin, info->ymax, 0, 0);
        input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, info->zmin, info->zmax, 0, 0); 
        input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, info->wmin, info->wmax, 0, 0);   
#else /*single touch */
        set_bit(EV_ABS | EV_SYN | EV_KEY, input->evbit);
        set_bit(BTN_TOUCH, input->keybit); 
        input_set_abs_params(input, ABS_X, info->xmin, info->xmax, 0, 0);
        input_set_abs_params(input, ABS_Y, info->ymin, info->ymax, 0, 0);
        input_set_abs_params(input, ABS_PRESSURE, info->zmin, info->zmax, 0, 0);
        input_set_abs_params(input, ABS_TOOL_WIDTH, info->wmin, info->wmax, 0, 0);
#endif
    
        if (input_register_device(input) < 0) {
            input_free_device(input);
            input = 0;
        }
    }
    
    return input;
}


static void capts_report_down(struct input_dev *input, struct ts_event *event, int event_num)
{
    int i;
    
#ifdef MULTI_TOUCH
    for (i=0; i<event_num; i++) {
        capts_debug_info("point_%d: x=%d, y=%d, z=%d, w=%d\n",
            event->id, event->x, event->y, event->z, event->w);
        input_report_abs(input, ABS_MT_POSITION_X, event->x);
        input_report_abs(input, ABS_MT_POSITION_Y, event->y);
        input_report_abs(input, ABS_MT_TOUCH_MAJOR, event->z);
        input_report_abs(input, ABS_MT_WIDTH_MAJOR, event->w);
//        input_report_abs(input, ABS_MT_TRACKING_ID, event->id);
        input_mt_sync(input);
        event++;
    }
#else
    input_report_abs(input, ABS_X, event->x);
    input_report_abs(input, ABS_Y, event->y);
    input_report_abs(input, ABS_PRESSURE, event->z);
    input_report_abs(input, ABS_TOOL_WIDTH, event->w);
    input_report_key(input, BTN_TOUCH,  1);
#endif
    input_sync(input);
}


static void capts_report_up(struct input_dev *input)
{
#ifdef MULTI_TOUCH
    input_report_abs(input, ABS_MT_TOUCH_MAJOR, 0);
    input_report_abs(input, ABS_MT_WIDTH_MAJOR, 0);
    input_mt_sync(input);
#else
    input_report_abs(input, ABS_PRESSURE, 0);
    input_report_abs(input, ABS_TOOL_WIDTH, 0);
    input_report_key(input, BTN_TOUCH, 0);
#endif
    input_sync(input);
}

static void capts_report(struct capts *ts, struct ts_event *event, int event_num)
{
    if (!event_num) {
        ts->pending = 0;
        if (ts->event_num) {
            capts_report_up(ts->input);
            ts->event_num = 0;
            capts_debug_info( "UP\n");
        }
    }
    else if (event_num > 0) {
        ts->pending = 1;
        capts_report_down(ts->input, event, event_num);
        if (!ts->event_num) {
            ts->event_num = event_num;
            capts_debug_info( "DOWN\n");
        }
    }
}

static ssize_t capts_read(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct capts *ts = (struct capts *)dev_get_drvdata(dev);
    
    if (!strcmp(attr->attr.name, "version")) {
        strcpy(buf, ts->chip->version);
        return strlen(ts->chip->version);
    }
    else if (!strcmp(attr->attr.name, "information")) {
        memcpy(buf, &ts->pdata->info, sizeof(struct ts_info));
        return sizeof(struct ts_info);
    } 
 
    return 0;
}

static ssize_t capts_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret = 0;
    struct capts *ts = (struct capts *)dev_get_drvdata(dev);

    if (!strcmp(attr->attr.name, "calibration")) {
        if (!ts->chip->calibration) {
            printk("no need to calibrate!\n");
            return 0;
        }
        printk("calibrating... don't touch your panel!\n");
        spin_lock_irq(&ts->lock);
        while (ts->pending) {
            spin_unlock_irq(&ts->lock);
            msleep(1);
            spin_lock_irq(&ts->lock);
        }        
        if (ts->irq) {
            disable_irq(ts->irq);
        }

        if (ts->chip->calibration(dev) < 0) {
            printk("calibration failed\n");
            ret = 0;
        }
        else {
            printk("calibration ok\n");
            ret = count;
        }

        if (ts->irq) {
            enable_irq(ts->irq);
        }
        spin_unlock_irq(&ts->lock);
    }
    
    return count;
}

static DEVICE_ATTR(calibration, S_IRWXUGO, 0, capts_write);
static DEVICE_ATTR(version, S_IRWXUGO, capts_read, 0);
static DEVICE_ATTR(information, S_IRWXUGO, capts_read, 0);

static struct attribute *capts_attr[] = {
    &dev_attr_calibration.attr,
    &dev_attr_version.attr,
    &dev_attr_information.attr,
    NULL
};

static struct attribute_group capts_attr_group = {
    .name = NULL,
    .attrs = capts_attr,
};


/**
 * capts_work() - work queue handler (initiated by the interrupt handler)
 * @work:  delay work queue to handle
 */
static void capts_work(struct work_struct *work)
{
    int event_num;
    struct ts_event event[EVENT_MAX];
    struct capts *ts = container_of(work, struct capts, work);
    int period = ts->pdata->poll_period;
    if (!period) period = TS_POLL_PERIOD;

    switch (ts->pdata->mode) {
    case TS_MODE_TIMER_READ:
        event_num = ts->chip->get_event(ts->dev, &event[0]);
        capts_report(ts, &event[0], event_num);
        hrtimer_start(&ts->timer, ktime_set(0, period), HRTIMER_MODE_REL);
        break;
    
    case TS_MODE_TIMER_LOW:
    case TS_MODE_TIMER_HIGH:
        event_num = 0;
        if (capts_get_fingdown_state(ts)) {
            event_num = ts->chip->get_event(ts->dev, &event[0]);
        }
        capts_report(ts, &event[0], event_num);
        hrtimer_start(&ts->timer, ktime_set(0, period),
        HRTIMER_MODE_REL);
        break;

    case TS_MODE_INT_LOW:
    case TS_MODE_INT_HIGH:
        event_num = 0;
        if (capts_get_fingdown_state(ts)) {
            event_num = ts->chip->get_event(ts->dev, &event[0]);
        }
        capts_report(ts, &event[0], event_num);
        
        if (event_num) {
            hrtimer_start(&ts->timer, ktime_set(0, period), HRTIMER_MODE_REL);
        }
        else {
            enable_irq(ts->irq);
        }
        break;

    case TS_MODE_INT_FALLING:
    case TS_MODE_INT_RISING:
        event_num = ts->chip->get_event(ts->dev, &event[0]);
        capts_report(ts, &event[0], event_num);
        if (capts_get_fingdown_state(ts) && ts->pdata->cache_enable) {
            hrtimer_start(&ts->timer, ktime_set(0, period/3), HRTIMER_MODE_REL);
            capts_debug_info( "still down, read once more\n");
        }
        else {
            enable_irq(ts->irq);
            if (event_num < 0) {
                hrtimer_start(&ts->timer, ktime_set(0, period*3), HRTIMER_MODE_REL);
                capts_debug_info( "read data error, wait finger up\n");
            }
        }
        break;
        
    default:
        break;
    }
}


static enum hrtimer_restart capts_timer(struct hrtimer *timer)
{
    struct capts *ts = container_of(timer, struct capts, timer);
    
    queue_work(ts->workqueue, &ts->work);	

    return HRTIMER_NORESTART;
}


/**
 * capts_interrupt() - interrupt handler for touch events
 * @irq:       interrupt to handle
 * @context:    device-specific information
 */
static irqreturn_t capts_interrupt(int irq, void *context)
{
    struct capts *ts = (struct capts *) context;
    unsigned long flags;

    spin_lock_irqsave(&ts->lock, flags);
    if (capts_get_fingdown_state(ts)) {
       if (ts->timer.function) {
            hrtimer_cancel(&ts->timer);
        }
        ts->pending = 1;
        disable_irq_nosync(ts->irq);
        queue_work(ts->workqueue, &ts->work);
    }
    spin_unlock_irqrestore(&ts->lock, flags);
    
    return IRQ_HANDLED;
}

/**
 * capts_probe()
 * @dev:    device to initialize
 * @chip:   chip interface    
 */
int capts_probe(struct device *dev, struct ts_chip *chip)
{
    struct capts *ts = 0;
    struct ts_platform_data *pdata = dev->platform_data;
    int err = 0;
   
    if (!dev || !chip || !pdata) {
        err = -ENODEV;
        printk("capacitive touch screen: no chip registered\n");
        return err;
    }
     
    ts = kzalloc(sizeof(struct capts), GFP_KERNEL);
    if (!ts) {
        err = -ENOMEM;
        printk("%s: allocate ts failed\n", ts->chip->name);
        return err;
    }
    
    ts->dev = dev;
    ts->pdata = pdata;
    ts->chip = chip;
    ts->event_num = 0;
    ts->irq = 0;
    ts->timer.function = 0;
    ts->use_attr_group = 0;
    ts->pending = 0;
    ts->is_suspended = 0;
    spin_lock_init(&ts->lock);
    dev_set_drvdata(dev, ts);

    INIT_WORK(&ts->work, capts_work);
    ts->workqueue = create_singlethread_workqueue(dev->driver->name);
    if (!ts->workqueue) {
        err = -ENOMEM;
        printk("%s: can't create work queue\n", ts->chip->name);
        goto fail;
    }
    
    if (ts->chip->reset) {
        err = ts->chip->reset(dev);
        if (err) {
            printk("%s: reset failed, %d\n", ts->chip->name, err);
            goto fail;
        }
    }

    if (pdata->init_irq) {
        pdata->init_irq();
    }
    hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    ts->timer.function = capts_timer;
    if ((pdata->mode == TS_MODE_TIMER_LOW)
    || (pdata->mode == TS_MODE_TIMER_HIGH)
    || (pdata->mode == TS_MODE_TIMER_READ)) {
        /* no interrupt, use timer only */
        hrtimer_start(&ts->timer, ktime_set(0, TS_POLL_PERIOD),
        HRTIMER_MODE_REL);
    }
    else {
        ts->irq = pdata->irq;
        err = request_irq(pdata->irq, capts_interrupt,
        capts_irq_type[pdata->mode], dev->driver->name, ts);
        if (err) {
            printk("%s: request gpio irq failed, %d\n", ts->chip->name, err);
            goto fail;
        }
    }
    
    ts->input = capts_register_input(dev->driver->name, &ts->pdata->info);
    if (!ts->input) {
        err = -ENOMEM;
        goto fail;
    }
    
    err = sysfs_create_group(&dev->kobj, &capts_attr_group);
    if (err) {
        printk("%s: create device attribute group failed\n", ts->chip->name);
        goto fail;
    }
    ts->use_attr_group = 1;
    
    printk("%s: init ok\n", ts->chip->name);
    return 0;
    
fail:
    capts_remove(dev);
    return err;
}

/**
 * capts_remove()
 * @dev:    device to clean up
 */
int capts_remove(struct device *dev)
{  
    struct capts *ts = dev_get_drvdata(dev);
    
    dev_set_drvdata(dev, NULL);
    if (ts) {
        if (ts->irq) {
            free_irq(ts->irq, ts);
        }
        if (ts->timer.function) {
            hrtimer_cancel(&ts->timer);
        }
        if (ts->workqueue) {
            destroy_workqueue(ts->workqueue);
        }
        if (ts->input) {
            input_unregister_device(ts->input);
        }
        if (ts->use_attr_group) {
            sysfs_remove_group(&dev->kobj, &capts_attr_group);
        }
        kfree(ts);
    }
    
    return 0;
}

int capts_suspend(struct device *dev, pm_message_t msg)
{
    struct capts *ts = dev_get_drvdata(dev);
    
    spin_lock_irq(&ts->lock);

    ts->is_suspended = 1;
    while (ts->pending) {
        spin_unlock_irq(&ts->lock);
        msleep(1);
        spin_lock_irq(&ts->lock);
    }
    
    if (ts->irq) {
        disable_irq(ts->irq);
        if (device_may_wakeup(dev)) {
            enable_irq_wake(ts->irq);
        }
    }

    spin_unlock_irq(&ts->lock);
    if(ts->pdata->power_off)
		ts->pdata->power_off();
    return 0;
}

int capts_resume(struct device *dev)
{
    struct capts *ts = dev_get_drvdata(dev);
    if(ts->pdata->power_on)
		ts->pdata->power_on();
    spin_lock_irq(&ts->lock);

    ts->is_suspended = 0;
    if (ts->irq) {
        enable_irq(ts->irq);
        if (device_may_wakeup(dev)) {
            disable_irq_wake(ts->irq);
        }
    }

    spin_unlock_irq(&ts->lock);
    
    return 0;
}
