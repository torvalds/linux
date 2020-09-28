// SPDX-License-Identifier: GPL-2.0

#include <linux/anon_inodes.h>
#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/build_bug.h>
#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>
#include <uapi/linux/gpio.h>

#include "gpiolib.h"
#include "gpiolib-cdev.h"

/*
 * Array sizes must ensure 64-bit alignment and not create holes in the
 * struct packing.
 */
static_assert(IS_ALIGNED(GPIO_V2_LINES_MAX, 2));
static_assert(IS_ALIGNED(GPIO_MAX_NAME_SIZE, 8));

/*
 * Check that uAPI structs are 64-bit aligned for 32/64-bit compatibility
 */
static_assert(IS_ALIGNED(sizeof(struct gpio_v2_line_attribute), 8));
static_assert(IS_ALIGNED(sizeof(struct gpio_v2_line_config_attribute), 8));
static_assert(IS_ALIGNED(sizeof(struct gpio_v2_line_config), 8));
static_assert(IS_ALIGNED(sizeof(struct gpio_v2_line_request), 8));
static_assert(IS_ALIGNED(sizeof(struct gpio_v2_line_info), 8));
static_assert(IS_ALIGNED(sizeof(struct gpio_v2_line_info_changed), 8));
static_assert(IS_ALIGNED(sizeof(struct gpio_v2_line_event), 8));
static_assert(IS_ALIGNED(sizeof(struct gpio_v2_line_values), 8));

/* Character device interface to GPIO.
 *
 * The GPIO character device, /dev/gpiochipN, provides userspace an
 * interface to gpiolib GPIOs via ioctl()s.
 */

/*
 * GPIO line handle management
 */

#ifdef CONFIG_GPIO_CDEV_V1
/**
 * struct linehandle_state - contains the state of a userspace handle
 * @gdev: the GPIO device the handle pertains to
 * @label: consumer label used to tag descriptors
 * @descs: the GPIO descriptors held by this handle
 * @num_descs: the number of descriptors held in the descs array
 */
struct linehandle_state {
	struct gpio_device *gdev;
	const char *label;
	struct gpio_desc *descs[GPIOHANDLES_MAX];
	u32 num_descs;
};

#define GPIOHANDLE_REQUEST_VALID_FLAGS \
	(GPIOHANDLE_REQUEST_INPUT | \
	GPIOHANDLE_REQUEST_OUTPUT | \
	GPIOHANDLE_REQUEST_ACTIVE_LOW | \
	GPIOHANDLE_REQUEST_BIAS_PULL_UP | \
	GPIOHANDLE_REQUEST_BIAS_PULL_DOWN | \
	GPIOHANDLE_REQUEST_BIAS_DISABLE | \
	GPIOHANDLE_REQUEST_OPEN_DRAIN | \
	GPIOHANDLE_REQUEST_OPEN_SOURCE)

static int linehandle_validate_flags(u32 flags)
{
	/* Return an error if an unknown flag is set */
	if (flags & ~GPIOHANDLE_REQUEST_VALID_FLAGS)
		return -EINVAL;

	/*
	 * Do not allow both INPUT & OUTPUT flags to be set as they are
	 * contradictory.
	 */
	if ((flags & GPIOHANDLE_REQUEST_INPUT) &&
	    (flags & GPIOHANDLE_REQUEST_OUTPUT))
		return -EINVAL;

	/*
	 * Do not allow OPEN_SOURCE & OPEN_DRAIN flags in a single request. If
	 * the hardware actually supports enabling both at the same time the
	 * electrical result would be disastrous.
	 */
	if ((flags & GPIOHANDLE_REQUEST_OPEN_DRAIN) &&
	    (flags & GPIOHANDLE_REQUEST_OPEN_SOURCE))
		return -EINVAL;

	/* OPEN_DRAIN and OPEN_SOURCE flags only make sense for output mode. */
	if (!(flags & GPIOHANDLE_REQUEST_OUTPUT) &&
	    ((flags & GPIOHANDLE_REQUEST_OPEN_DRAIN) ||
	     (flags & GPIOHANDLE_REQUEST_OPEN_SOURCE)))
		return -EINVAL;

	/* Bias flags only allowed for input or output mode. */
	if (!((flags & GPIOHANDLE_REQUEST_INPUT) ||
	      (flags & GPIOHANDLE_REQUEST_OUTPUT)) &&
	    ((flags & GPIOHANDLE_REQUEST_BIAS_DISABLE) ||
	     (flags & GPIOHANDLE_REQUEST_BIAS_PULL_UP) ||
	     (flags & GPIOHANDLE_REQUEST_BIAS_PULL_DOWN)))
		return -EINVAL;

	/* Only one bias flag can be set. */
	if (((flags & GPIOHANDLE_REQUEST_BIAS_DISABLE) &&
	     (flags & (GPIOHANDLE_REQUEST_BIAS_PULL_DOWN |
		       GPIOHANDLE_REQUEST_BIAS_PULL_UP))) ||
	    ((flags & GPIOHANDLE_REQUEST_BIAS_PULL_DOWN) &&
	     (flags & GPIOHANDLE_REQUEST_BIAS_PULL_UP)))
		return -EINVAL;

	return 0;
}

static void linehandle_flags_to_desc_flags(u32 lflags, unsigned long *flagsp)
{
	assign_bit(FLAG_ACTIVE_LOW, flagsp,
		   lflags & GPIOHANDLE_REQUEST_ACTIVE_LOW);
	assign_bit(FLAG_OPEN_DRAIN, flagsp,
		   lflags & GPIOHANDLE_REQUEST_OPEN_DRAIN);
	assign_bit(FLAG_OPEN_SOURCE, flagsp,
		   lflags & GPIOHANDLE_REQUEST_OPEN_SOURCE);
	assign_bit(FLAG_PULL_UP, flagsp,
		   lflags & GPIOHANDLE_REQUEST_BIAS_PULL_UP);
	assign_bit(FLAG_PULL_DOWN, flagsp,
		   lflags & GPIOHANDLE_REQUEST_BIAS_PULL_DOWN);
	assign_bit(FLAG_BIAS_DISABLE, flagsp,
		   lflags & GPIOHANDLE_REQUEST_BIAS_DISABLE);
}

static long linehandle_set_config(struct linehandle_state *lh,
				  void __user *ip)
{
	struct gpiohandle_config gcnf;
	struct gpio_desc *desc;
	int i, ret;
	u32 lflags;

	if (copy_from_user(&gcnf, ip, sizeof(gcnf)))
		return -EFAULT;

	lflags = gcnf.flags;
	ret = linehandle_validate_flags(lflags);
	if (ret)
		return ret;

	for (i = 0; i < lh->num_descs; i++) {
		desc = lh->descs[i];
		linehandle_flags_to_desc_flags(gcnf.flags, &desc->flags);

		/*
		 * Lines have to be requested explicitly for input
		 * or output, else the line will be treated "as is".
		 */
		if (lflags & GPIOHANDLE_REQUEST_OUTPUT) {
			int val = !!gcnf.default_values[i];

			ret = gpiod_direction_output(desc, val);
			if (ret)
				return ret;
		} else if (lflags & GPIOHANDLE_REQUEST_INPUT) {
			ret = gpiod_direction_input(desc);
			if (ret)
				return ret;
		}

		blocking_notifier_call_chain(&desc->gdev->notifier,
					     GPIO_V2_LINE_CHANGED_CONFIG,
					     desc);
	}
	return 0;
}

static long linehandle_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	struct linehandle_state *lh = file->private_data;
	void __user *ip = (void __user *)arg;
	struct gpiohandle_data ghd;
	DECLARE_BITMAP(vals, GPIOHANDLES_MAX);
	int i;

	if (cmd == GPIOHANDLE_GET_LINE_VALUES_IOCTL) {
		/* NOTE: It's ok to read values of output lines. */
		int ret = gpiod_get_array_value_complex(false,
							true,
							lh->num_descs,
							lh->descs,
							NULL,
							vals);
		if (ret)
			return ret;

		memset(&ghd, 0, sizeof(ghd));
		for (i = 0; i < lh->num_descs; i++)
			ghd.values[i] = test_bit(i, vals);

		if (copy_to_user(ip, &ghd, sizeof(ghd)))
			return -EFAULT;

		return 0;
	} else if (cmd == GPIOHANDLE_SET_LINE_VALUES_IOCTL) {
		/*
		 * All line descriptors were created at once with the same
		 * flags so just check if the first one is really output.
		 */
		if (!test_bit(FLAG_IS_OUT, &lh->descs[0]->flags))
			return -EPERM;

		if (copy_from_user(&ghd, ip, sizeof(ghd)))
			return -EFAULT;

		/* Clamp all values to [0,1] */
		for (i = 0; i < lh->num_descs; i++)
			__assign_bit(i, vals, ghd.values[i]);

		/* Reuse the array setting function */
		return gpiod_set_array_value_complex(false,
						     true,
						     lh->num_descs,
						     lh->descs,
						     NULL,
						     vals);
	} else if (cmd == GPIOHANDLE_SET_CONFIG_IOCTL) {
		return linehandle_set_config(lh, ip);
	}
	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long linehandle_ioctl_compat(struct file *file, unsigned int cmd,
				    unsigned long arg)
{
	return linehandle_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static void linehandle_free(struct linehandle_state *lh)
{
	int i;

	for (i = 0; i < lh->num_descs; i++)
		if (lh->descs[i])
			gpiod_free(lh->descs[i]);
	kfree(lh->label);
	put_device(&lh->gdev->dev);
	kfree(lh);
}

static int linehandle_release(struct inode *inode, struct file *file)
{
	linehandle_free(file->private_data);
	return 0;
}

static const struct file_operations linehandle_fileops = {
	.release = linehandle_release,
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
	.unlocked_ioctl = linehandle_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = linehandle_ioctl_compat,
#endif
};

static int linehandle_create(struct gpio_device *gdev, void __user *ip)
{
	struct gpiohandle_request handlereq;
	struct linehandle_state *lh;
	struct file *file;
	int fd, i, ret;
	u32 lflags;

	if (copy_from_user(&handlereq, ip, sizeof(handlereq)))
		return -EFAULT;
	if ((handlereq.lines == 0) || (handlereq.lines > GPIOHANDLES_MAX))
		return -EINVAL;

	lflags = handlereq.flags;

	ret = linehandle_validate_flags(lflags);
	if (ret)
		return ret;

	lh = kzalloc(sizeof(*lh), GFP_KERNEL);
	if (!lh)
		return -ENOMEM;
	lh->gdev = gdev;
	get_device(&gdev->dev);

	/* Make sure this is terminated */
	handlereq.consumer_label[sizeof(handlereq.consumer_label)-1] = '\0';
	if (strlen(handlereq.consumer_label)) {
		lh->label = kstrdup(handlereq.consumer_label,
				    GFP_KERNEL);
		if (!lh->label) {
			ret = -ENOMEM;
			goto out_free_lh;
		}
	}

	lh->num_descs = handlereq.lines;

	/* Request each GPIO */
	for (i = 0; i < handlereq.lines; i++) {
		u32 offset = handlereq.lineoffsets[i];
		struct gpio_desc *desc = gpiochip_get_desc(gdev->chip, offset);

		if (IS_ERR(desc)) {
			ret = PTR_ERR(desc);
			goto out_free_lh;
		}

		ret = gpiod_request(desc, lh->label);
		if (ret)
			goto out_free_lh;
		lh->descs[i] = desc;
		linehandle_flags_to_desc_flags(handlereq.flags, &desc->flags);

		ret = gpiod_set_transitory(desc, false);
		if (ret < 0)
			goto out_free_lh;

		/*
		 * Lines have to be requested explicitly for input
		 * or output, else the line will be treated "as is".
		 */
		if (lflags & GPIOHANDLE_REQUEST_OUTPUT) {
			int val = !!handlereq.default_values[i];

			ret = gpiod_direction_output(desc, val);
			if (ret)
				goto out_free_lh;
		} else if (lflags & GPIOHANDLE_REQUEST_INPUT) {
			ret = gpiod_direction_input(desc);
			if (ret)
				goto out_free_lh;
		}

		blocking_notifier_call_chain(&desc->gdev->notifier,
					     GPIO_V2_LINE_CHANGED_REQUESTED, desc);

		dev_dbg(&gdev->dev, "registered chardev handle for line %d\n",
			offset);
	}

	fd = get_unused_fd_flags(O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto out_free_lh;
	}

	file = anon_inode_getfile("gpio-linehandle",
				  &linehandle_fileops,
				  lh,
				  O_RDONLY | O_CLOEXEC);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto out_put_unused_fd;
	}

	handlereq.fd = fd;
	if (copy_to_user(ip, &handlereq, sizeof(handlereq))) {
		/*
		 * fput() will trigger the release() callback, so do not go onto
		 * the regular error cleanup path here.
		 */
		fput(file);
		put_unused_fd(fd);
		return -EFAULT;
	}

	fd_install(fd, file);

	dev_dbg(&gdev->dev, "registered chardev handle for %d lines\n",
		lh->num_descs);

	return 0;

out_put_unused_fd:
	put_unused_fd(fd);
out_free_lh:
	linehandle_free(lh);
	return ret;
}
#endif /* CONFIG_GPIO_CDEV_V1 */

/**
 * struct line - contains the state of a requested line
 * @desc: the GPIO descriptor for this line.
 * @req: the corresponding line request
 * @irq: the interrupt triggered in response to events on this GPIO
 * @eflags: the edge flags, GPIO_V2_LINE_FLAG_EDGE_RISING and/or
 * GPIO_V2_LINE_FLAG_EDGE_FALLING, indicating the edge detection applied
 * @timestamp_ns: cache for the timestamp storing it between hardirq and
 * IRQ thread, used to bring the timestamp close to the actual event
 * @req_seqno: the seqno for the current edge event in the sequence of
 * events for the corresponding line request. This is drawn from the @req.
 * @line_seqno: the seqno for the current edge event in the sequence of
 * events for this line.
 */
struct line {
	struct gpio_desc *desc;
	/*
	 * -- edge detector specific fields --
	 */
	struct linereq *req;
	unsigned int irq;
	u64 eflags;
	/*
	 * timestamp_ns and req_seqno are accessed only by
	 * edge_irq_handler() and edge_irq_thread(), which are themselves
	 * mutually exclusive, so no additional protection is necessary.
	 */
	u64 timestamp_ns;
	u32 req_seqno;
	u32 line_seqno;
};

/**
 * struct linereq - contains the state of a userspace line request
 * @gdev: the GPIO device the line request pertains to
 * @label: consumer label used to tag GPIO descriptors
 * @num_lines: the number of lines in the lines array
 * @wait: wait queue that handles blocking reads of events
 * @event_buffer_size: the number of elements allocated in @events
 * @events: KFIFO for the GPIO events
 * @seqno: the sequence number for edge events generated on all lines in
 * this line request.  Note that this is not used when @num_lines is 1, as
 * the line_seqno is then the same and is cheaper to calculate.
 * @lines: the lines held by this line request, with @num_lines elements.
 */
struct linereq {
	struct gpio_device *gdev;
	const char *label;
	u32 num_lines;
	wait_queue_head_t wait;
	u32 event_buffer_size;
	DECLARE_KFIFO_PTR(events, struct gpio_v2_line_event);
	atomic_t seqno;
	struct line lines[];
};

#define GPIO_V2_LINE_BIAS_FLAGS \
	(GPIO_V2_LINE_FLAG_BIAS_PULL_UP | \
	 GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN | \
	 GPIO_V2_LINE_FLAG_BIAS_DISABLED)

#define GPIO_V2_LINE_DIRECTION_FLAGS \
	(GPIO_V2_LINE_FLAG_INPUT | \
	 GPIO_V2_LINE_FLAG_OUTPUT)

#define GPIO_V2_LINE_DRIVE_FLAGS \
	(GPIO_V2_LINE_FLAG_OPEN_DRAIN | \
	 GPIO_V2_LINE_FLAG_OPEN_SOURCE)

#define GPIO_V2_LINE_EDGE_FLAGS \
	(GPIO_V2_LINE_FLAG_EDGE_RISING | \
	 GPIO_V2_LINE_FLAG_EDGE_FALLING)

#define GPIO_V2_LINE_VALID_FLAGS \
	(GPIO_V2_LINE_FLAG_ACTIVE_LOW | \
	 GPIO_V2_LINE_DIRECTION_FLAGS | \
	 GPIO_V2_LINE_DRIVE_FLAGS | \
	 GPIO_V2_LINE_EDGE_FLAGS | \
	 GPIO_V2_LINE_BIAS_FLAGS)

static void linereq_put_event(struct linereq *lr,
			      struct gpio_v2_line_event *le)
{
	bool overflow = false;

	spin_lock(&lr->wait.lock);
	if (kfifo_is_full(&lr->events)) {
		overflow = true;
		kfifo_skip(&lr->events);
	}
	kfifo_in(&lr->events, le, 1);
	spin_unlock(&lr->wait.lock);
	if (!overflow)
		wake_up_poll(&lr->wait, EPOLLIN);
	else
		pr_debug_ratelimited("event FIFO is full - event dropped\n");
}

static irqreturn_t edge_irq_thread(int irq, void *p)
{
	struct line *line = p;
	struct linereq *lr = line->req;
	struct gpio_v2_line_event le;

	/* Do not leak kernel stack to userspace */
	memset(&le, 0, sizeof(le));

	if (line->timestamp_ns) {
		le.timestamp_ns = line->timestamp_ns;
	} else {
		/*
		 * We may be running from a nested threaded interrupt in
		 * which case we didn't get the timestamp from
		 * edge_irq_handler().
		 */
		le.timestamp_ns = ktime_get_ns();
		if (lr->num_lines != 1)
			line->req_seqno = atomic_inc_return(&lr->seqno);
	}
	line->timestamp_ns = 0;

	if (line->eflags == (GPIO_V2_LINE_FLAG_EDGE_RISING |
			     GPIO_V2_LINE_FLAG_EDGE_FALLING)) {
		int level = gpiod_get_value_cansleep(line->desc);

		if (level)
			/* Emit low-to-high event */
			le.id = GPIO_V2_LINE_EVENT_RISING_EDGE;
		else
			/* Emit high-to-low event */
			le.id = GPIO_V2_LINE_EVENT_FALLING_EDGE;
	} else if (line->eflags == GPIO_V2_LINE_FLAG_EDGE_RISING) {
		/* Emit low-to-high event */
		le.id = GPIO_V2_LINE_EVENT_RISING_EDGE;
	} else if (line->eflags == GPIO_V2_LINE_FLAG_EDGE_FALLING) {
		/* Emit high-to-low event */
		le.id = GPIO_V2_LINE_EVENT_FALLING_EDGE;
	} else {
		return IRQ_NONE;
	}
	line->line_seqno++;
	le.line_seqno = line->line_seqno;
	le.seqno = (lr->num_lines == 1) ? le.line_seqno : line->req_seqno;
	le.offset = gpio_chip_hwgpio(line->desc);

	linereq_put_event(lr, &le);

	return IRQ_HANDLED;
}

static irqreturn_t edge_irq_handler(int irq, void *p)
{
	struct line *line = p;
	struct linereq *lr = line->req;

	/*
	 * Just store the timestamp in hardirq context so we get it as
	 * close in time as possible to the actual event.
	 */
	line->timestamp_ns = ktime_get_ns();

	if (lr->num_lines != 1)
		line->req_seqno = atomic_inc_return(&lr->seqno);

	return IRQ_WAKE_THREAD;
}

static void edge_detector_stop(struct line *line)
{
	if (line->irq) {
		free_irq(line->irq, line);
		line->irq = 0;
	}
}

static int edge_detector_setup(struct line *line,
			       u64 eflags)
{
	unsigned long irqflags = 0;
	int irq, ret;

	if (eflags && !kfifo_initialized(&line->req->events)) {
		ret = kfifo_alloc(&line->req->events,
				  line->req->event_buffer_size, GFP_KERNEL);
		if (ret)
			return ret;
	}
	line->eflags = eflags;

	if (!eflags)
		return 0;

	irq = gpiod_to_irq(line->desc);
	if (irq < 0)
		return -ENXIO;

	if (eflags & GPIO_V2_LINE_FLAG_EDGE_RISING)
		irqflags |= test_bit(FLAG_ACTIVE_LOW, &line->desc->flags) ?
			IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
	if (eflags & GPIO_V2_LINE_FLAG_EDGE_FALLING)
		irqflags |= test_bit(FLAG_ACTIVE_LOW, &line->desc->flags) ?
			IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING;
	irqflags |= IRQF_ONESHOT;

	/* Request a thread to read the events */
	ret = request_threaded_irq(irq, edge_irq_handler, edge_irq_thread,
				   irqflags, line->req->label, line);
	if (ret)
		return ret;

	line->irq = irq;
	return 0;
}

static u64 gpio_v2_line_config_flags(struct gpio_v2_line_config *lc,
				     unsigned int line_idx)
{
	unsigned int i;
	u64 mask = BIT_ULL(line_idx);

	for (i = 0; i < lc->num_attrs; i++) {
		if ((lc->attrs[i].attr.id == GPIO_V2_LINE_ATTR_ID_FLAGS) &&
		    (lc->attrs[i].mask & mask))
			return lc->attrs[i].attr.flags;
	}
	return lc->flags;
}

static int gpio_v2_line_config_output_value(struct gpio_v2_line_config *lc,
					    unsigned int line_idx)
{
	unsigned int i;
	u64 mask = BIT_ULL(line_idx);

	for (i = 0; i < lc->num_attrs; i++) {
		if ((lc->attrs[i].attr.id == GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES) &&
		    (lc->attrs[i].mask & mask))
			return !!(lc->attrs[i].attr.values & mask);
	}
	return 0;
}

static int gpio_v2_line_flags_validate(u64 flags)
{
	/* Return an error if an unknown flag is set */
	if (flags & ~GPIO_V2_LINE_VALID_FLAGS)
		return -EINVAL;

	/*
	 * Do not allow both INPUT and OUTPUT flags to be set as they are
	 * contradictory.
	 */
	if ((flags & GPIO_V2_LINE_FLAG_INPUT) &&
	    (flags & GPIO_V2_LINE_FLAG_OUTPUT))
		return -EINVAL;

	/* Edge detection requires explicit input. */
	if ((flags & GPIO_V2_LINE_EDGE_FLAGS) &&
	    !(flags & GPIO_V2_LINE_FLAG_INPUT))
		return -EINVAL;

	/*
	 * Do not allow OPEN_SOURCE and OPEN_DRAIN flags in a single
	 * request. If the hardware actually supports enabling both at the
	 * same time the electrical result would be disastrous.
	 */
	if ((flags & GPIO_V2_LINE_FLAG_OPEN_DRAIN) &&
	    (flags & GPIO_V2_LINE_FLAG_OPEN_SOURCE))
		return -EINVAL;

	/* Drive requires explicit output direction. */
	if ((flags & GPIO_V2_LINE_DRIVE_FLAGS) &&
	    !(flags & GPIO_V2_LINE_FLAG_OUTPUT))
		return -EINVAL;

	/* Bias requires explicit direction. */
	if ((flags & GPIO_V2_LINE_BIAS_FLAGS) &&
	    !(flags & GPIO_V2_LINE_DIRECTION_FLAGS))
		return -EINVAL;

	/* Only one bias flag can be set. */
	if (((flags & GPIO_V2_LINE_FLAG_BIAS_DISABLED) &&
	     (flags & (GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN |
		       GPIO_V2_LINE_FLAG_BIAS_PULL_UP))) ||
	    ((flags & GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN) &&
	     (flags & GPIO_V2_LINE_FLAG_BIAS_PULL_UP)))
		return -EINVAL;

	return 0;
}

static int gpio_v2_line_config_validate(struct gpio_v2_line_config *lc,
					unsigned int num_lines)
{
	unsigned int i;
	u64 flags;
	int ret;

	if (lc->num_attrs > GPIO_V2_LINE_NUM_ATTRS_MAX)
		return -EINVAL;

	if (memchr_inv(lc->padding, 0, sizeof(lc->padding)))
		return -EINVAL;

	for (i = 0; i < num_lines; i++) {
		flags = gpio_v2_line_config_flags(lc, i);
		ret = gpio_v2_line_flags_validate(flags);
		if (ret)
			return ret;
	}
	return 0;
}

static void gpio_v2_line_config_flags_to_desc_flags(u64 flags,
						    unsigned long *flagsp)
{
	assign_bit(FLAG_ACTIVE_LOW, flagsp,
		   flags & GPIO_V2_LINE_FLAG_ACTIVE_LOW);

	if (flags & GPIO_V2_LINE_FLAG_OUTPUT)
		set_bit(FLAG_IS_OUT, flagsp);
	else if (flags & GPIO_V2_LINE_FLAG_INPUT)
		clear_bit(FLAG_IS_OUT, flagsp);

	assign_bit(FLAG_EDGE_RISING, flagsp,
		   flags & GPIO_V2_LINE_FLAG_EDGE_RISING);
	assign_bit(FLAG_EDGE_FALLING, flagsp,
		   flags & GPIO_V2_LINE_FLAG_EDGE_FALLING);

	assign_bit(FLAG_OPEN_DRAIN, flagsp,
		   flags & GPIO_V2_LINE_FLAG_OPEN_DRAIN);
	assign_bit(FLAG_OPEN_SOURCE, flagsp,
		   flags & GPIO_V2_LINE_FLAG_OPEN_SOURCE);

	assign_bit(FLAG_PULL_UP, flagsp,
		   flags & GPIO_V2_LINE_FLAG_BIAS_PULL_UP);
	assign_bit(FLAG_PULL_DOWN, flagsp,
		   flags & GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN);
	assign_bit(FLAG_BIAS_DISABLE, flagsp,
		   flags & GPIO_V2_LINE_FLAG_BIAS_DISABLED);
}

static long linereq_get_values(struct linereq *lr, void __user *ip)
{
	struct gpio_v2_line_values lv;
	DECLARE_BITMAP(vals, GPIO_V2_LINES_MAX);
	struct gpio_desc **descs;
	unsigned int i, didx, num_get;
	int ret;

	/* NOTE: It's ok to read values of output lines. */
	if (copy_from_user(&lv, ip, sizeof(lv)))
		return -EFAULT;

	for (num_get = 0, i = 0; i < lr->num_lines; i++) {
		if (lv.mask & BIT_ULL(i)) {
			num_get++;
			descs = &lr->lines[i].desc;
		}
	}

	if (num_get == 0)
		return -EINVAL;

	if (num_get != 1) {
		descs = kmalloc_array(num_get, sizeof(*descs), GFP_KERNEL);
		if (!descs)
			return -ENOMEM;
		for (didx = 0, i = 0; i < lr->num_lines; i++) {
			if (lv.mask & BIT_ULL(i)) {
				descs[didx] = lr->lines[i].desc;
				didx++;
			}
		}
	}
	ret = gpiod_get_array_value_complex(false, true, num_get,
					    descs, NULL, vals);

	if (num_get != 1)
		kfree(descs);
	if (ret)
		return ret;

	lv.bits = 0;
	for (didx = 0, i = 0; i < lr->num_lines; i++) {
		if (lv.mask & BIT_ULL(i)) {
			if (test_bit(didx, vals))
				lv.bits |= BIT_ULL(i);
			didx++;
		}
	}

	if (copy_to_user(ip, &lv, sizeof(lv)))
		return -EFAULT;

	return 0;
}

static long linereq_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct linereq *lr = file->private_data;
	void __user *ip = (void __user *)arg;

	if (cmd == GPIO_V2_LINE_GET_VALUES_IOCTL)
		return linereq_get_values(lr, ip);

	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long linereq_ioctl_compat(struct file *file, unsigned int cmd,
				 unsigned long arg)
{
	return linereq_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static __poll_t linereq_poll(struct file *file,
			    struct poll_table_struct *wait)
{
	struct linereq *lr = file->private_data;
	__poll_t events = 0;

	poll_wait(file, &lr->wait, wait);

	if (!kfifo_is_empty_spinlocked_noirqsave(&lr->events,
						 &lr->wait.lock))
		events = EPOLLIN | EPOLLRDNORM;

	return events;
}

static ssize_t linereq_read(struct file *file,
			    char __user *buf,
			    size_t count,
			    loff_t *f_ps)
{
	struct linereq *lr = file->private_data;
	struct gpio_v2_line_event le;
	ssize_t bytes_read = 0;
	int ret;

	if (count < sizeof(le))
		return -EINVAL;

	do {
		spin_lock(&lr->wait.lock);
		if (kfifo_is_empty(&lr->events)) {
			if (bytes_read) {
				spin_unlock(&lr->wait.lock);
				return bytes_read;
			}

			if (file->f_flags & O_NONBLOCK) {
				spin_unlock(&lr->wait.lock);
				return -EAGAIN;
			}

			ret = wait_event_interruptible_locked(lr->wait,
					!kfifo_is_empty(&lr->events));
			if (ret) {
				spin_unlock(&lr->wait.lock);
				return ret;
			}
		}

		ret = kfifo_out(&lr->events, &le, 1);
		spin_unlock(&lr->wait.lock);
		if (ret != 1) {
			/*
			 * This should never happen - we were holding the
			 * lock from the moment we learned the fifo is no
			 * longer empty until now.
			 */
			ret = -EIO;
			break;
		}

		if (copy_to_user(buf + bytes_read, &le, sizeof(le)))
			return -EFAULT;
		bytes_read += sizeof(le);
	} while (count >= bytes_read + sizeof(le));

	return bytes_read;
}

static void linereq_free(struct linereq *lr)
{
	unsigned int i;

	for (i = 0; i < lr->num_lines; i++) {
		edge_detector_stop(&lr->lines[i]);
		if (lr->lines[i].desc)
			gpiod_free(lr->lines[i].desc);
	}
	kfifo_free(&lr->events);
	kfree(lr->label);
	put_device(&lr->gdev->dev);
	kfree(lr);
}

static int linereq_release(struct inode *inode, struct file *file)
{
	struct linereq *lr = file->private_data;

	linereq_free(lr);
	return 0;
}

static const struct file_operations line_fileops = {
	.release = linereq_release,
	.read = linereq_read,
	.poll = linereq_poll,
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
	.unlocked_ioctl = linereq_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = linereq_ioctl_compat,
#endif
};

static int linereq_create(struct gpio_device *gdev, void __user *ip)
{
	struct gpio_v2_line_request ulr;
	struct gpio_v2_line_config *lc;
	struct linereq *lr;
	struct file *file;
	u64 flags;
	unsigned int i;
	int fd, ret;

	if (copy_from_user(&ulr, ip, sizeof(ulr)))
		return -EFAULT;

	if ((ulr.num_lines == 0) || (ulr.num_lines > GPIO_V2_LINES_MAX))
		return -EINVAL;

	if (memchr_inv(ulr.padding, 0, sizeof(ulr.padding)))
		return -EINVAL;

	lc = &ulr.config;
	ret = gpio_v2_line_config_validate(lc, ulr.num_lines);
	if (ret)
		return ret;

	lr = kzalloc(struct_size(lr, lines, ulr.num_lines), GFP_KERNEL);
	if (!lr)
		return -ENOMEM;

	lr->gdev = gdev;
	get_device(&gdev->dev);

	for (i = 0; i < ulr.num_lines; i++)
		lr->lines[i].req = lr;

	/* Make sure this is terminated */
	ulr.consumer[sizeof(ulr.consumer)-1] = '\0';
	if (strlen(ulr.consumer)) {
		/* label is only initialized if consumer is set */
		lr->label = kstrdup(ulr.consumer, GFP_KERNEL);
		if (!lr->label) {
			ret = -ENOMEM;
			goto out_free_linereq;
		}
	}

	init_waitqueue_head(&lr->wait);
	lr->event_buffer_size = ulr.event_buffer_size;
	if (lr->event_buffer_size == 0)
		lr->event_buffer_size = ulr.num_lines * 16;
	else if (lr->event_buffer_size > GPIO_V2_LINES_MAX * 16)
		lr->event_buffer_size = GPIO_V2_LINES_MAX * 16;

	atomic_set(&lr->seqno, 0);
	lr->num_lines = ulr.num_lines;

	/* Request each GPIO */
	for (i = 0; i < ulr.num_lines; i++) {
		u32 offset = ulr.offsets[i];
		struct gpio_desc *desc = gpiochip_get_desc(gdev->chip, offset);

		if (IS_ERR(desc)) {
			ret = PTR_ERR(desc);
			goto out_free_linereq;
		}

		ret = gpiod_request(desc, lr->label);
		if (ret)
			goto out_free_linereq;

		lr->lines[i].desc = desc;
		flags = gpio_v2_line_config_flags(lc, i);
		gpio_v2_line_config_flags_to_desc_flags(flags, &desc->flags);

		ret = gpiod_set_transitory(desc, false);
		if (ret < 0)
			goto out_free_linereq;

		/*
		 * Lines have to be requested explicitly for input
		 * or output, else the line will be treated "as is".
		 */
		if (flags & GPIO_V2_LINE_FLAG_OUTPUT) {
			int val = gpio_v2_line_config_output_value(lc, i);

			ret = gpiod_direction_output(desc, val);
			if (ret)
				goto out_free_linereq;
		} else if (flags & GPIO_V2_LINE_FLAG_INPUT) {
			ret = gpiod_direction_input(desc);
			if (ret)
				goto out_free_linereq;

			ret = edge_detector_setup(&lr->lines[i],
					flags & GPIO_V2_LINE_EDGE_FLAGS);
			if (ret)
				goto out_free_linereq;
		}

		blocking_notifier_call_chain(&desc->gdev->notifier,
					     GPIO_V2_LINE_CHANGED_REQUESTED, desc);

		dev_dbg(&gdev->dev, "registered chardev handle for line %d\n",
			offset);
	}

	fd = get_unused_fd_flags(O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto out_free_linereq;
	}

	file = anon_inode_getfile("gpio-line", &line_fileops, lr,
				  O_RDONLY | O_CLOEXEC);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto out_put_unused_fd;
	}

	ulr.fd = fd;
	if (copy_to_user(ip, &ulr, sizeof(ulr))) {
		/*
		 * fput() will trigger the release() callback, so do not go onto
		 * the regular error cleanup path here.
		 */
		fput(file);
		put_unused_fd(fd);
		return -EFAULT;
	}

	fd_install(fd, file);

	dev_dbg(&gdev->dev, "registered chardev handle for %d lines\n",
		lr->num_lines);

	return 0;

out_put_unused_fd:
	put_unused_fd(fd);
out_free_linereq:
	linereq_free(lr);
	return ret;
}

#ifdef CONFIG_GPIO_CDEV_V1

/*
 * GPIO line event management
 */

/**
 * struct lineevent_state - contains the state of a userspace event
 * @gdev: the GPIO device the event pertains to
 * @label: consumer label used to tag descriptors
 * @desc: the GPIO descriptor held by this event
 * @eflags: the event flags this line was requested with
 * @irq: the interrupt that trigger in response to events on this GPIO
 * @wait: wait queue that handles blocking reads of events
 * @events: KFIFO for the GPIO events
 * @timestamp: cache for the timestamp storing it between hardirq
 * and IRQ thread, used to bring the timestamp close to the actual
 * event
 */
struct lineevent_state {
	struct gpio_device *gdev;
	const char *label;
	struct gpio_desc *desc;
	u32 eflags;
	int irq;
	wait_queue_head_t wait;
	DECLARE_KFIFO(events, struct gpioevent_data, 16);
	u64 timestamp;
};

#define GPIOEVENT_REQUEST_VALID_FLAGS \
	(GPIOEVENT_REQUEST_RISING_EDGE | \
	GPIOEVENT_REQUEST_FALLING_EDGE)

static __poll_t lineevent_poll(struct file *file,
			       struct poll_table_struct *wait)
{
	struct lineevent_state *le = file->private_data;
	__poll_t events = 0;

	poll_wait(file, &le->wait, wait);

	if (!kfifo_is_empty_spinlocked_noirqsave(&le->events, &le->wait.lock))
		events = EPOLLIN | EPOLLRDNORM;

	return events;
}


static ssize_t lineevent_read(struct file *file,
			      char __user *buf,
			      size_t count,
			      loff_t *f_ps)
{
	struct lineevent_state *le = file->private_data;
	struct gpioevent_data ge;
	ssize_t bytes_read = 0;
	int ret;

	if (count < sizeof(ge))
		return -EINVAL;

	do {
		spin_lock(&le->wait.lock);
		if (kfifo_is_empty(&le->events)) {
			if (bytes_read) {
				spin_unlock(&le->wait.lock);
				return bytes_read;
			}

			if (file->f_flags & O_NONBLOCK) {
				spin_unlock(&le->wait.lock);
				return -EAGAIN;
			}

			ret = wait_event_interruptible_locked(le->wait,
					!kfifo_is_empty(&le->events));
			if (ret) {
				spin_unlock(&le->wait.lock);
				return ret;
			}
		}

		ret = kfifo_out(&le->events, &ge, 1);
		spin_unlock(&le->wait.lock);
		if (ret != 1) {
			/*
			 * This should never happen - we were holding the lock
			 * from the moment we learned the fifo is no longer
			 * empty until now.
			 */
			ret = -EIO;
			break;
		}

		if (copy_to_user(buf + bytes_read, &ge, sizeof(ge)))
			return -EFAULT;
		bytes_read += sizeof(ge);
	} while (count >= bytes_read + sizeof(ge));

	return bytes_read;
}

static void lineevent_free(struct lineevent_state *le)
{
	if (le->irq)
		free_irq(le->irq, le);
	if (le->desc)
		gpiod_free(le->desc);
	kfree(le->label);
	put_device(&le->gdev->dev);
	kfree(le);
}

static int lineevent_release(struct inode *inode, struct file *file)
{
	lineevent_free(file->private_data);
	return 0;
}

static long lineevent_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct lineevent_state *le = file->private_data;
	void __user *ip = (void __user *)arg;
	struct gpiohandle_data ghd;

	/*
	 * We can get the value for an event line but not set it,
	 * because it is input by definition.
	 */
	if (cmd == GPIOHANDLE_GET_LINE_VALUES_IOCTL) {
		int val;

		memset(&ghd, 0, sizeof(ghd));

		val = gpiod_get_value_cansleep(le->desc);
		if (val < 0)
			return val;
		ghd.values[0] = val;

		if (copy_to_user(ip, &ghd, sizeof(ghd)))
			return -EFAULT;

		return 0;
	}
	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long lineevent_ioctl_compat(struct file *file, unsigned int cmd,
				   unsigned long arg)
{
	return lineevent_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations lineevent_fileops = {
	.release = lineevent_release,
	.read = lineevent_read,
	.poll = lineevent_poll,
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
	.unlocked_ioctl = lineevent_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = lineevent_ioctl_compat,
#endif
};

static irqreturn_t lineevent_irq_thread(int irq, void *p)
{
	struct lineevent_state *le = p;
	struct gpioevent_data ge;
	int ret;

	/* Do not leak kernel stack to userspace */
	memset(&ge, 0, sizeof(ge));

	/*
	 * We may be running from a nested threaded interrupt in which case
	 * we didn't get the timestamp from lineevent_irq_handler().
	 */
	if (!le->timestamp)
		ge.timestamp = ktime_get_ns();
	else
		ge.timestamp = le->timestamp;

	if (le->eflags & GPIOEVENT_REQUEST_RISING_EDGE
	    && le->eflags & GPIOEVENT_REQUEST_FALLING_EDGE) {
		int level = gpiod_get_value_cansleep(le->desc);

		if (level)
			/* Emit low-to-high event */
			ge.id = GPIOEVENT_EVENT_RISING_EDGE;
		else
			/* Emit high-to-low event */
			ge.id = GPIOEVENT_EVENT_FALLING_EDGE;
	} else if (le->eflags & GPIOEVENT_REQUEST_RISING_EDGE) {
		/* Emit low-to-high event */
		ge.id = GPIOEVENT_EVENT_RISING_EDGE;
	} else if (le->eflags & GPIOEVENT_REQUEST_FALLING_EDGE) {
		/* Emit high-to-low event */
		ge.id = GPIOEVENT_EVENT_FALLING_EDGE;
	} else {
		return IRQ_NONE;
	}

	ret = kfifo_in_spinlocked_noirqsave(&le->events, &ge,
					    1, &le->wait.lock);
	if (ret)
		wake_up_poll(&le->wait, EPOLLIN);
	else
		pr_debug_ratelimited("event FIFO is full - event dropped\n");

	return IRQ_HANDLED;
}

static irqreturn_t lineevent_irq_handler(int irq, void *p)
{
	struct lineevent_state *le = p;

	/*
	 * Just store the timestamp in hardirq context so we get it as
	 * close in time as possible to the actual event.
	 */
	le->timestamp = ktime_get_ns();

	return IRQ_WAKE_THREAD;
}

static int lineevent_create(struct gpio_device *gdev, void __user *ip)
{
	struct gpioevent_request eventreq;
	struct lineevent_state *le;
	struct gpio_desc *desc;
	struct file *file;
	u32 offset;
	u32 lflags;
	u32 eflags;
	int fd;
	int ret;
	int irq, irqflags = 0;

	if (copy_from_user(&eventreq, ip, sizeof(eventreq)))
		return -EFAULT;

	offset = eventreq.lineoffset;
	lflags = eventreq.handleflags;
	eflags = eventreq.eventflags;

	desc = gpiochip_get_desc(gdev->chip, offset);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	/* Return an error if a unknown flag is set */
	if ((lflags & ~GPIOHANDLE_REQUEST_VALID_FLAGS) ||
	    (eflags & ~GPIOEVENT_REQUEST_VALID_FLAGS))
		return -EINVAL;

	/* This is just wrong: we don't look for events on output lines */
	if ((lflags & GPIOHANDLE_REQUEST_OUTPUT) ||
	    (lflags & GPIOHANDLE_REQUEST_OPEN_DRAIN) ||
	    (lflags & GPIOHANDLE_REQUEST_OPEN_SOURCE))
		return -EINVAL;

	/* Only one bias flag can be set. */
	if (((lflags & GPIOHANDLE_REQUEST_BIAS_DISABLE) &&
	     (lflags & (GPIOHANDLE_REQUEST_BIAS_PULL_DOWN |
			GPIOHANDLE_REQUEST_BIAS_PULL_UP))) ||
	    ((lflags & GPIOHANDLE_REQUEST_BIAS_PULL_DOWN) &&
	     (lflags & GPIOHANDLE_REQUEST_BIAS_PULL_UP)))
		return -EINVAL;

	le = kzalloc(sizeof(*le), GFP_KERNEL);
	if (!le)
		return -ENOMEM;
	le->gdev = gdev;
	get_device(&gdev->dev);

	/* Make sure this is terminated */
	eventreq.consumer_label[sizeof(eventreq.consumer_label)-1] = '\0';
	if (strlen(eventreq.consumer_label)) {
		le->label = kstrdup(eventreq.consumer_label,
				    GFP_KERNEL);
		if (!le->label) {
			ret = -ENOMEM;
			goto out_free_le;
		}
	}

	ret = gpiod_request(desc, le->label);
	if (ret)
		goto out_free_le;
	le->desc = desc;
	le->eflags = eflags;

	linehandle_flags_to_desc_flags(lflags, &desc->flags);

	ret = gpiod_direction_input(desc);
	if (ret)
		goto out_free_le;

	blocking_notifier_call_chain(&desc->gdev->notifier,
				     GPIO_V2_LINE_CHANGED_REQUESTED, desc);

	irq = gpiod_to_irq(desc);
	if (irq <= 0) {
		ret = -ENODEV;
		goto out_free_le;
	}
	le->irq = irq;

	if (eflags & GPIOEVENT_REQUEST_RISING_EDGE)
		irqflags |= test_bit(FLAG_ACTIVE_LOW, &desc->flags) ?
			IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
	if (eflags & GPIOEVENT_REQUEST_FALLING_EDGE)
		irqflags |= test_bit(FLAG_ACTIVE_LOW, &desc->flags) ?
			IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING;
	irqflags |= IRQF_ONESHOT;

	INIT_KFIFO(le->events);
	init_waitqueue_head(&le->wait);

	/* Request a thread to read the events */
	ret = request_threaded_irq(le->irq,
				   lineevent_irq_handler,
				   lineevent_irq_thread,
				   irqflags,
				   le->label,
				   le);
	if (ret)
		goto out_free_le;

	fd = get_unused_fd_flags(O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto out_free_le;
	}

	file = anon_inode_getfile("gpio-event",
				  &lineevent_fileops,
				  le,
				  O_RDONLY | O_CLOEXEC);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto out_put_unused_fd;
	}

	eventreq.fd = fd;
	if (copy_to_user(ip, &eventreq, sizeof(eventreq))) {
		/*
		 * fput() will trigger the release() callback, so do not go onto
		 * the regular error cleanup path here.
		 */
		fput(file);
		put_unused_fd(fd);
		return -EFAULT;
	}

	fd_install(fd, file);

	return 0;

out_put_unused_fd:
	put_unused_fd(fd);
out_free_le:
	lineevent_free(le);
	return ret;
}

static void gpio_v2_line_info_to_v1(struct gpio_v2_line_info *info_v2,
				    struct gpioline_info *info_v1)
{
	u64 flagsv2 = info_v2->flags;

	memcpy(info_v1->name, info_v2->name, sizeof(info_v1->name));
	memcpy(info_v1->consumer, info_v2->consumer, sizeof(info_v1->consumer));
	info_v1->line_offset = info_v2->offset;
	info_v1->flags = 0;

	if (flagsv2 & GPIO_V2_LINE_FLAG_USED)
		info_v1->flags |= GPIOLINE_FLAG_KERNEL;

	if (flagsv2 & GPIO_V2_LINE_FLAG_OUTPUT)
		info_v1->flags |= GPIOLINE_FLAG_IS_OUT;

	if (flagsv2 & GPIO_V2_LINE_FLAG_ACTIVE_LOW)
		info_v1->flags |= GPIOLINE_FLAG_ACTIVE_LOW;

	if (flagsv2 & GPIO_V2_LINE_FLAG_OPEN_DRAIN)
		info_v1->flags |= GPIOLINE_FLAG_OPEN_DRAIN;
	if (flagsv2 & GPIO_V2_LINE_FLAG_OPEN_SOURCE)
		info_v1->flags |= GPIOLINE_FLAG_OPEN_SOURCE;

	if (flagsv2 & GPIO_V2_LINE_FLAG_BIAS_PULL_UP)
		info_v1->flags |= GPIOLINE_FLAG_BIAS_PULL_UP;
	if (flagsv2 & GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN)
		info_v1->flags |= GPIOLINE_FLAG_BIAS_PULL_DOWN;
	if (flagsv2 & GPIO_V2_LINE_FLAG_BIAS_DISABLED)
		info_v1->flags |= GPIOLINE_FLAG_BIAS_DISABLE;
}

static void gpio_v2_line_info_changed_to_v1(
		struct gpio_v2_line_info_changed *lic_v2,
		struct gpioline_info_changed *lic_v1)
{
	gpio_v2_line_info_to_v1(&lic_v2->info, &lic_v1->info);
	lic_v1->timestamp = lic_v2->timestamp_ns;
	lic_v1->event_type = lic_v2->event_type;
}

#endif /* CONFIG_GPIO_CDEV_V1 */

static void gpio_desc_to_lineinfo(struct gpio_desc *desc,
				  struct gpio_v2_line_info *info)
{
	struct gpio_chip *gc = desc->gdev->chip;
	bool ok_for_pinctrl;
	unsigned long flags;

	memset(info, 0, sizeof(*info));
	info->offset = gpio_chip_hwgpio(desc);

	/*
	 * This function takes a mutex so we must check this before taking
	 * the spinlock.
	 *
	 * FIXME: find a non-racy way to retrieve this information. Maybe a
	 * lock common to both frameworks?
	 */
	ok_for_pinctrl =
		pinctrl_gpio_can_use_line(gc->base + info->offset);

	spin_lock_irqsave(&gpio_lock, flags);

	if (desc->name)
		strscpy(info->name, desc->name, sizeof(info->name));

	if (desc->label)
		strscpy(info->consumer, desc->label, sizeof(info->consumer));

	/*
	 * Userspace only need to know that the kernel is using this GPIO so
	 * it can't use it.
	 */
	info->flags = 0;
	if (test_bit(FLAG_REQUESTED, &desc->flags) ||
	    test_bit(FLAG_IS_HOGGED, &desc->flags) ||
	    test_bit(FLAG_USED_AS_IRQ, &desc->flags) ||
	    test_bit(FLAG_EXPORT, &desc->flags) ||
	    test_bit(FLAG_SYSFS, &desc->flags) ||
	    !ok_for_pinctrl)
		info->flags |= GPIO_V2_LINE_FLAG_USED;

	if (test_bit(FLAG_IS_OUT, &desc->flags))
		info->flags |= GPIO_V2_LINE_FLAG_OUTPUT;
	else
		info->flags |= GPIO_V2_LINE_FLAG_INPUT;

	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		info->flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;

	if (test_bit(FLAG_OPEN_DRAIN, &desc->flags))
		info->flags |= GPIO_V2_LINE_FLAG_OPEN_DRAIN;
	if (test_bit(FLAG_OPEN_SOURCE, &desc->flags))
		info->flags |= GPIO_V2_LINE_FLAG_OPEN_SOURCE;

	if (test_bit(FLAG_BIAS_DISABLE, &desc->flags))
		info->flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
	if (test_bit(FLAG_PULL_DOWN, &desc->flags))
		info->flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
	if (test_bit(FLAG_PULL_UP, &desc->flags))
		info->flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;

	if (test_bit(FLAG_EDGE_RISING, &desc->flags))
		info->flags |= GPIO_V2_LINE_FLAG_EDGE_RISING;
	if (test_bit(FLAG_EDGE_FALLING, &desc->flags))
		info->flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING;

	spin_unlock_irqrestore(&gpio_lock, flags);
}

struct gpio_chardev_data {
	struct gpio_device *gdev;
	wait_queue_head_t wait;
	DECLARE_KFIFO(events, struct gpio_v2_line_info_changed, 32);
	struct notifier_block lineinfo_changed_nb;
	unsigned long *watched_lines;
#ifdef CONFIG_GPIO_CDEV_V1
	atomic_t watch_abi_version;
#endif
};

#ifdef CONFIG_GPIO_CDEV_V1
/*
 * returns 0 if the versions match, else the previously selected ABI version
 */
static int lineinfo_ensure_abi_version(struct gpio_chardev_data *cdata,
				       unsigned int version)
{
	int abiv = atomic_cmpxchg(&cdata->watch_abi_version, 0, version);

	if (abiv == version)
		return 0;

	return abiv;
}
#endif

static int lineinfo_get(struct gpio_chardev_data *cdev, void __user *ip,
			bool watch)
{
	struct gpio_desc *desc;
	struct gpio_v2_line_info lineinfo;

	if (copy_from_user(&lineinfo, ip, sizeof(lineinfo)))
		return -EFAULT;

	if (memchr_inv(lineinfo.padding, 0, sizeof(lineinfo.padding)))
		return -EINVAL;

	desc = gpiochip_get_desc(cdev->gdev->chip, lineinfo.offset);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	if (watch) {
#ifdef CONFIG_GPIO_CDEV_V1
		if (lineinfo_ensure_abi_version(cdev, 2))
			return -EPERM;
#endif
		if (test_and_set_bit(lineinfo.offset, cdev->watched_lines))
			return -EBUSY;
	}
	gpio_desc_to_lineinfo(desc, &lineinfo);

	if (copy_to_user(ip, &lineinfo, sizeof(lineinfo))) {
		if (watch)
			clear_bit(lineinfo.offset, cdev->watched_lines);
		return -EFAULT;
	}

	return 0;
}

/*
 * gpio_ioctl() - ioctl handler for the GPIO chardev
 */
static long gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct gpio_chardev_data *cdev = file->private_data;
	struct gpio_device *gdev = cdev->gdev;
	struct gpio_chip *gc = gdev->chip;
	void __user *ip = (void __user *)arg;
	__u32 offset;

	/* We fail any subsequent ioctl():s when the chip is gone */
	if (!gc)
		return -ENODEV;

	/* Fill in the struct and pass to userspace */
	if (cmd == GPIO_GET_CHIPINFO_IOCTL) {
		struct gpiochip_info chipinfo;

		memset(&chipinfo, 0, sizeof(chipinfo));

		strscpy(chipinfo.name, dev_name(&gdev->dev),
			sizeof(chipinfo.name));
		strscpy(chipinfo.label, gdev->label,
			sizeof(chipinfo.label));
		chipinfo.lines = gdev->ngpio;
		if (copy_to_user(ip, &chipinfo, sizeof(chipinfo)))
			return -EFAULT;
		return 0;
#ifdef CONFIG_GPIO_CDEV_V1
	} else if (cmd == GPIO_GET_LINEINFO_IOCTL) {
		struct gpio_desc *desc;
		struct gpioline_info lineinfo;
		struct gpio_v2_line_info lineinfo_v2;

		if (copy_from_user(&lineinfo, ip, sizeof(lineinfo)))
			return -EFAULT;

		/* this doubles as a range check on line_offset */
		desc = gpiochip_get_desc(gc, lineinfo.line_offset);
		if (IS_ERR(desc))
			return PTR_ERR(desc);

		gpio_desc_to_lineinfo(desc, &lineinfo_v2);
		gpio_v2_line_info_to_v1(&lineinfo_v2, &lineinfo);

		if (copy_to_user(ip, &lineinfo, sizeof(lineinfo)))
			return -EFAULT;
		return 0;
	} else if (cmd == GPIO_GET_LINEHANDLE_IOCTL) {
		return linehandle_create(gdev, ip);
	} else if (cmd == GPIO_GET_LINEEVENT_IOCTL) {
		return lineevent_create(gdev, ip);
	} else if (cmd == GPIO_GET_LINEINFO_WATCH_IOCTL) {
		struct gpio_desc *desc;
		struct gpioline_info lineinfo;
		struct gpio_v2_line_info lineinfo_v2;

		if (copy_from_user(&lineinfo, ip, sizeof(lineinfo)))
			return -EFAULT;

		/* this doubles as a range check on line_offset */
		desc = gpiochip_get_desc(gc, lineinfo.line_offset);
		if (IS_ERR(desc))
			return PTR_ERR(desc);

		if (lineinfo_ensure_abi_version(cdev, 1))
			return -EPERM;

		if (test_and_set_bit(lineinfo.line_offset, cdev->watched_lines))
			return -EBUSY;

		gpio_desc_to_lineinfo(desc, &lineinfo_v2);
		gpio_v2_line_info_to_v1(&lineinfo_v2, &lineinfo);

		if (copy_to_user(ip, &lineinfo, sizeof(lineinfo))) {
			clear_bit(lineinfo.line_offset, cdev->watched_lines);
			return -EFAULT;
		}

		return 0;
#endif /* CONFIG_GPIO_CDEV_V1 */
	} else if (cmd == GPIO_V2_GET_LINEINFO_IOCTL ||
		   cmd == GPIO_V2_GET_LINEINFO_WATCH_IOCTL) {
		return lineinfo_get(cdev, ip,
				    cmd == GPIO_V2_GET_LINEINFO_WATCH_IOCTL);
	} else if (cmd == GPIO_V2_GET_LINE_IOCTL) {
		return linereq_create(gdev, ip);
	} else if (cmd == GPIO_GET_LINEINFO_UNWATCH_IOCTL) {
		if (copy_from_user(&offset, ip, sizeof(offset)))
			return -EFAULT;

		if (offset >= cdev->gdev->ngpio)
			return -EINVAL;

		if (!test_and_clear_bit(offset, cdev->watched_lines))
			return -EBUSY;

		return 0;
	}
	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long gpio_ioctl_compat(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	return gpio_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static struct gpio_chardev_data *
to_gpio_chardev_data(struct notifier_block *nb)
{
	return container_of(nb, struct gpio_chardev_data, lineinfo_changed_nb);
}

static int lineinfo_changed_notify(struct notifier_block *nb,
				   unsigned long action, void *data)
{
	struct gpio_chardev_data *cdev = to_gpio_chardev_data(nb);
	struct gpio_v2_line_info_changed chg;
	struct gpio_desc *desc = data;
	int ret;

	if (!test_bit(gpio_chip_hwgpio(desc), cdev->watched_lines))
		return NOTIFY_DONE;

	memset(&chg, 0, sizeof(chg));
	chg.event_type = action;
	chg.timestamp_ns = ktime_get_ns();
	gpio_desc_to_lineinfo(desc, &chg.info);

	ret = kfifo_in_spinlocked(&cdev->events, &chg, 1, &cdev->wait.lock);
	if (ret)
		wake_up_poll(&cdev->wait, EPOLLIN);
	else
		pr_debug_ratelimited("lineinfo event FIFO is full - event dropped\n");

	return NOTIFY_OK;
}

static __poll_t lineinfo_watch_poll(struct file *file,
				    struct poll_table_struct *pollt)
{
	struct gpio_chardev_data *cdev = file->private_data;
	__poll_t events = 0;

	poll_wait(file, &cdev->wait, pollt);

	if (!kfifo_is_empty_spinlocked_noirqsave(&cdev->events,
						 &cdev->wait.lock))
		events = EPOLLIN | EPOLLRDNORM;

	return events;
}

static ssize_t lineinfo_watch_read(struct file *file, char __user *buf,
				   size_t count, loff_t *off)
{
	struct gpio_chardev_data *cdev = file->private_data;
	struct gpio_v2_line_info_changed event;
	ssize_t bytes_read = 0;
	int ret;
	size_t event_size;

#ifndef CONFIG_GPIO_CDEV_V1
	event_size = sizeof(struct gpio_v2_line_info_changed);
	if (count < event_size)
		return -EINVAL;
#endif

	do {
		spin_lock(&cdev->wait.lock);
		if (kfifo_is_empty(&cdev->events)) {
			if (bytes_read) {
				spin_unlock(&cdev->wait.lock);
				return bytes_read;
			}

			if (file->f_flags & O_NONBLOCK) {
				spin_unlock(&cdev->wait.lock);
				return -EAGAIN;
			}

			ret = wait_event_interruptible_locked(cdev->wait,
					!kfifo_is_empty(&cdev->events));
			if (ret) {
				spin_unlock(&cdev->wait.lock);
				return ret;
			}
		}
#ifdef CONFIG_GPIO_CDEV_V1
		/* must be after kfifo check so watch_abi_version is set */
		if (atomic_read(&cdev->watch_abi_version) == 2)
			event_size = sizeof(struct gpio_v2_line_info_changed);
		else
			event_size = sizeof(struct gpioline_info_changed);
		if (count < event_size) {
			spin_unlock(&cdev->wait.lock);
			return -EINVAL;
		}
#endif
		ret = kfifo_out(&cdev->events, &event, 1);
		spin_unlock(&cdev->wait.lock);
		if (ret != 1) {
			ret = -EIO;
			break;
			/* We should never get here. See lineevent_read(). */
		}

#ifdef CONFIG_GPIO_CDEV_V1
		if (event_size == sizeof(struct gpio_v2_line_info_changed)) {
			if (copy_to_user(buf + bytes_read, &event, event_size))
				return -EFAULT;
		} else {
			struct gpioline_info_changed event_v1;

			gpio_v2_line_info_changed_to_v1(&event, &event_v1);
			if (copy_to_user(buf + bytes_read, &event_v1,
					 event_size))
				return -EFAULT;
		}
#else
		if (copy_to_user(buf + bytes_read, &event, event_size))
			return -EFAULT;
#endif
		bytes_read += event_size;
	} while (count >= bytes_read + sizeof(event));

	return bytes_read;
}

/**
 * gpio_chrdev_open() - open the chardev for ioctl operations
 * @inode: inode for this chardev
 * @file: file struct for storing private data
 * Returns 0 on success
 */
static int gpio_chrdev_open(struct inode *inode, struct file *file)
{
	struct gpio_device *gdev = container_of(inode->i_cdev,
						struct gpio_device, chrdev);
	struct gpio_chardev_data *cdev;
	int ret = -ENOMEM;

	/* Fail on open if the backing gpiochip is gone */
	if (!gdev->chip)
		return -ENODEV;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	cdev->watched_lines = bitmap_zalloc(gdev->chip->ngpio, GFP_KERNEL);
	if (!cdev->watched_lines)
		goto out_free_cdev;

	init_waitqueue_head(&cdev->wait);
	INIT_KFIFO(cdev->events);
	cdev->gdev = gdev;

	cdev->lineinfo_changed_nb.notifier_call = lineinfo_changed_notify;
	ret = blocking_notifier_chain_register(&gdev->notifier,
					       &cdev->lineinfo_changed_nb);
	if (ret)
		goto out_free_bitmap;

	get_device(&gdev->dev);
	file->private_data = cdev;

	ret = nonseekable_open(inode, file);
	if (ret)
		goto out_unregister_notifier;

	return ret;

out_unregister_notifier:
	blocking_notifier_chain_unregister(&gdev->notifier,
					   &cdev->lineinfo_changed_nb);
out_free_bitmap:
	bitmap_free(cdev->watched_lines);
out_free_cdev:
	kfree(cdev);
	return ret;
}

/**
 * gpio_chrdev_release() - close chardev after ioctl operations
 * @inode: inode for this chardev
 * @file: file struct for storing private data
 * Returns 0 on success
 */
static int gpio_chrdev_release(struct inode *inode, struct file *file)
{
	struct gpio_chardev_data *cdev = file->private_data;
	struct gpio_device *gdev = cdev->gdev;

	bitmap_free(cdev->watched_lines);
	blocking_notifier_chain_unregister(&gdev->notifier,
					   &cdev->lineinfo_changed_nb);
	put_device(&gdev->dev);
	kfree(cdev);

	return 0;
}

static const struct file_operations gpio_fileops = {
	.release = gpio_chrdev_release,
	.open = gpio_chrdev_open,
	.poll = lineinfo_watch_poll,
	.read = lineinfo_watch_read,
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.unlocked_ioctl = gpio_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gpio_ioctl_compat,
#endif
};

int gpiolib_cdev_register(struct gpio_device *gdev, dev_t devt)
{
	int ret;

	cdev_init(&gdev->chrdev, &gpio_fileops);
	gdev->chrdev.owner = THIS_MODULE;
	gdev->dev.devt = MKDEV(MAJOR(devt), gdev->id);

	ret = cdev_device_add(&gdev->chrdev, &gdev->dev);
	if (ret)
		return ret;

	chip_dbg(gdev->chip, "added GPIO chardev (%d:%d)\n",
		 MAJOR(devt), gdev->id);

	return 0;
}

void gpiolib_cdev_unregister(struct gpio_device *gdev)
{
	cdev_device_del(&gdev->chrdev, &gdev->dev);
}
