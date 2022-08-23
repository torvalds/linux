/* Copyright (C) 2005 - 2008 Jeff Dike <jdike@{linux.intel,addtoit}.com> */

/* Much of this ripped from drivers/char/hw_random.c, see there for other
 * copyright.
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */
#include <linux/sched/signal.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/hw_random.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <init.h>
#include <irq_kern.h>
#include <os.h>

/*
 * core module information
 */
#define RNG_MODULE_NAME "hw_random"

/* Changed at init time, in the non-modular case, and at module load
 * time, in the module case.  Presumably, the module subsystem
 * protects against a module being loaded twice at the same time.
 */
static int random_fd = -1;
static struct hwrng hwrng;
static DECLARE_COMPLETION(have_data);

static int rng_dev_read(struct hwrng *rng, void *buf, size_t max, bool block)
{
	int ret;

	for (;;) {
		ret = os_read_file(random_fd, buf, max);
		if (block && ret == -EAGAIN) {
			add_sigio_fd(random_fd);

			ret = wait_for_completion_killable(&have_data);

			ignore_sigio_fd(random_fd);
			deactivate_fd(random_fd, RANDOM_IRQ);

			if (ret < 0)
				break;
		} else {
			break;
		}
	}

	return ret != -EAGAIN ? ret : 0;
}

static irqreturn_t random_interrupt(int irq, void *data)
{
	complete(&have_data);

	return IRQ_HANDLED;
}

/*
 * rng_init - initialize RNG module
 */
static int __init rng_init (void)
{
	int err;

	err = os_open_file("/dev/random", of_read(OPENFLAGS()), 0);
	if (err < 0)
		goto out;

	random_fd = err;
	err = um_request_irq(RANDOM_IRQ, random_fd, IRQ_READ, random_interrupt,
			     0, "random", NULL);
	if (err < 0)
		goto err_out_cleanup_hw;

	sigio_broken(random_fd);
	hwrng.name = RNG_MODULE_NAME;
	hwrng.read = rng_dev_read;
	hwrng.quality = 1024;

	err = hwrng_register(&hwrng);
	if (err) {
		pr_err(RNG_MODULE_NAME " registering failed (%d)\n", err);
		goto err_out_cleanup_hw;
	}
out:
	return err;

err_out_cleanup_hw:
	os_close_file(random_fd);
	random_fd = -1;
	goto out;
}

/*
 * rng_cleanup - shutdown RNG module
 */

static void cleanup(void)
{
	free_irq_by_fd(random_fd);
	os_close_file(random_fd);
}

static void __exit rng_cleanup(void)
{
	hwrng_unregister(&hwrng);
	os_close_file(random_fd);
}

module_init (rng_init);
module_exit (rng_cleanup);
__uml_exitcall(cleanup);

MODULE_DESCRIPTION("UML Host Random Number Generator (RNG) driver");
MODULE_LICENSE("GPL");
