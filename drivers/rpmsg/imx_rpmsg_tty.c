/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * derived from the omap-rpmsg implementation.
 * Remote processor messaging transport - tty driver
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/virtio.h>

/*
 * struct rpmsgtty_port - Wrapper struct for imx rpmsg tty port.
 * @port:		TTY port data
 */
struct rpmsgtty_port {
	struct tty_port		port;
	spinlock_t		rx_lock;
	struct rpmsg_channel	*rpdev;
};

static struct rpmsgtty_port rpmsg_tty_port;

#define RPMSG_MAX_SIZE		(512 - sizeof(struct rpmsg_hdr))
#define MSG		"hello world!"

static void rpmsg_tty_cb(struct rpmsg_channel *rpdev, void *data, int len,
						void *priv, u32 src)
{
	int space;
	unsigned char *cbuf;
	struct rpmsgtty_port *cport = &rpmsg_tty_port;

	/* flush the recv-ed none-zero data to tty node */
	if (len == 0)
		return;

	dev_dbg(&rpdev->dev, "msg(<- src 0x%x) len %d\n", src, len);

	print_hex_dump(KERN_DEBUG, __func__, DUMP_PREFIX_NONE, 16, 1,
			data, len,  true);

	spin_lock_bh(&cport->rx_lock);
	space = tty_prepare_flip_string(&cport->port, &cbuf, len);
	if (space <= 0) {
		dev_err(&rpdev->dev, "No memory for tty_prepare_flip_string\n");
		spin_unlock_bh(&cport->rx_lock);
		return;
	}

	memcpy(cbuf, data, len);
	tty_flip_buffer_push(&cport->port);
	spin_unlock_bh(&cport->rx_lock);
}

static struct tty_port_operations  rpmsgtty_port_ops = { };

static int rpmsgtty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	return tty_port_install(&rpmsg_tty_port.port, driver, tty);
}

static int rpmsgtty_open(struct tty_struct *tty, struct file *filp)
{
	return tty_port_open(tty->port, tty, filp);
}

static void rpmsgtty_close(struct tty_struct *tty, struct file *filp)
{
	return tty_port_close(tty->port, tty, filp);
}

static int rpmsgtty_write(struct tty_struct *tty, const unsigned char *buf,
			 int total)
{
	int count, ret = 0;
	const unsigned char *tbuf;
	struct rpmsgtty_port *rptty_port = container_of(tty->port,
			struct rpmsgtty_port, port);
	struct rpmsg_channel *rpdev = rptty_port->rpdev;

	if (NULL == buf) {
		pr_err("buf shouldn't be null.\n");
		return -ENOMEM;
	}

	count = total;
	tbuf = buf;
	do {
		/* send a message to our remote processor */
		ret = rpmsg_send(rpdev, (void *)tbuf,
			count > RPMSG_MAX_SIZE ? RPMSG_MAX_SIZE : count);
		if (ret) {
			dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", ret);
			return ret;
		}

		if (count > RPMSG_MAX_SIZE) {
			count -= RPMSG_MAX_SIZE;
			tbuf += RPMSG_MAX_SIZE;
		} else {
			count = 0;
		}
	} while (count > 0);

	return total;
}

static int rpmsgtty_write_room(struct tty_struct *tty)
{
	/* report the space in the rpmsg buffer */
	return RPMSG_MAX_SIZE;
}

static const struct tty_operations imxrpmsgtty_ops = {
	.install		= rpmsgtty_install,
	.open			= rpmsgtty_open,
	.close			= rpmsgtty_close,
	.write			= rpmsgtty_write,
	.write_room		= rpmsgtty_write_room,
};

static struct tty_driver *rpmsgtty_driver;

static int rpmsg_tty_probe(struct rpmsg_channel *rpdev)
{
	int err;
	struct rpmsgtty_port *cport = &rpmsg_tty_port;

	dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n",
			rpdev->src, rpdev->dst);

	/*
	 * send a message to our remote processor, and tell remote
	 * processor about this channel
	 */
	err = rpmsg_send(rpdev, MSG, strlen(MSG));
	if (err) {
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", err);
		return err;
	}

	rpmsgtty_driver = tty_alloc_driver(1, TTY_DRIVER_UNNUMBERED_NODE);
	if (IS_ERR(rpmsgtty_driver))
		return PTR_ERR(rpmsgtty_driver);

	rpmsgtty_driver->driver_name = "rpmsg_tty";
	rpmsgtty_driver->name = "ttyRPMSG";
	rpmsgtty_driver->major = TTYAUX_MAJOR;
	rpmsgtty_driver->minor_start = 3;
	rpmsgtty_driver->type = TTY_DRIVER_TYPE_CONSOLE;
	rpmsgtty_driver->init_termios = tty_std_termios;

	tty_set_operations(rpmsgtty_driver, &imxrpmsgtty_ops);

	tty_port_init(&cport->port);
	cport->port.ops = &rpmsgtty_port_ops;
	spin_lock_init(&cport->rx_lock);
	cport->port.low_latency = cport->port.flags | ASYNC_LOW_LATENCY;

	err = tty_register_driver(rpmsgtty_driver);
	if (err < 0) {
		pr_err("Couldn't install rpmsg tty driver: err %d\n", err);
		goto error;
	} else
		pr_info("Install rpmsg tty driver!\n");
	cport->rpdev = rpdev;

	return 0;

error:
	tty_unregister_driver(rpmsgtty_driver);
	put_tty_driver(rpmsgtty_driver);
	tty_port_destroy(&cport->port);
	rpmsgtty_driver = NULL;

	return err;
}

static void rpmsg_tty_remove(struct rpmsg_channel *rpdev)
{
	struct rpmsgtty_port *cport = &rpmsg_tty_port;

	dev_info(&rpdev->dev, "rpmsg tty driver is removed\n");

	tty_unregister_driver(rpmsgtty_driver);
	put_tty_driver(rpmsgtty_driver);
	tty_port_destroy(&cport->port);
	rpmsgtty_driver = NULL;
}

static struct rpmsg_device_id rpmsg_driver_tty_id_table[] = {
	{ .name	= "rpmsg-openamp-demo-channel" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_driver_tty_id_table);

static struct rpmsg_driver rpmsg_tty_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_driver_tty_id_table,
	.probe		= rpmsg_tty_probe,
	.callback	= rpmsg_tty_cb,
	.remove		= rpmsg_tty_remove,
};

static int __init init(void)
{
	return register_rpmsg_driver(&rpmsg_tty_driver);
}

static void __exit fini(void)
{
	unregister_rpmsg_driver(&rpmsg_tty_driver);
}
module_init(init);
module_exit(fini);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("iMX virtio remote processor messaging tty driver");
MODULE_LICENSE("GPL v2");
