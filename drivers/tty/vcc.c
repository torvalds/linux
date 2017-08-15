/* vcc.c: sun4v virtual channel concentrator
 *
 * Copyright (C) 2017 Oracle. All rights reserved.
 */

#include <linux/module.h>
#include <linux/tty.h>

#define DRV_MODULE_NAME		"vcc"
#define DRV_MODULE_VERSION	"1.1"
#define DRV_MODULE_RELDATE	"July 1, 2017"

static char version[] =
	DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")";

MODULE_DESCRIPTION("Sun LDOM virtual console concentrator driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

#define VCC_MAX_PORTS		1024
#define VCC_MINOR_START		0	/* must be zero */

static const char vcc_driver_name[] = "vcc";
static const char vcc_device_node[] = "vcc";
static struct tty_driver *vcc_tty_driver;

int vcc_dbg;
int vcc_dbg_ldc;
int vcc_dbg_vio;

module_param(vcc_dbg, uint, 0664);
module_param(vcc_dbg_ldc, uint, 0664);
module_param(vcc_dbg_vio, uint, 0664);

#define VCC_DBG_DRV	0x1
#define VCC_DBG_LDC	0x2
#define VCC_DBG_PKT	0x4

#define vccdbg(f, a...)						\
	do {							\
		if (vcc_dbg & VCC_DBG_DRV)			\
			pr_info(f, ## a);			\
	} while (0)						\

#define vccdbgl(l)						\
	do {							\
		if (vcc_dbg & VCC_DBG_LDC)			\
			ldc_print(l);				\
	} while (0)						\

#define vccdbgp(pkt)						\
	do {							\
		if (vcc_dbg & VCC_DBG_PKT) {			\
			int i;					\
			for (i = 0; i < pkt.tag.stype; i++)	\
				pr_info("[%c]", pkt.data[i]);	\
		}						\
	} while (0)						\

/* Note: Be careful when adding flags to this line discipline.  Don't
 * add anything that will cause echoing or we'll go into recursive
 * loop echoing chars back and forth with the console drivers.
 */
static struct ktermios vcc_tty_termios = {
	.c_iflag = IGNBRK | IGNPAR,
	.c_oflag = OPOST,
	.c_cflag = B38400 | CS8 | CREAD | HUPCL,
	.c_cc = INIT_C_CC,
	.c_ispeed = 38400,
	.c_ospeed = 38400
};

static const struct tty_operations vcc_ops;

#define VCC_TTY_FLAGS   (TTY_DRIVER_DYNAMIC_DEV | TTY_DRIVER_REAL_RAW)

static int vcc_tty_init(void)
{
	int rv;

	pr_info("VCC: %s\n", version);

	vcc_tty_driver = tty_alloc_driver(VCC_MAX_PORTS, VCC_TTY_FLAGS);
	if (!vcc_tty_driver) {
		pr_err("VCC: TTY driver alloc failed\n");
		return -ENOMEM;
	}

	vcc_tty_driver->driver_name = vcc_driver_name;
	vcc_tty_driver->name = vcc_device_node;

	vcc_tty_driver->minor_start = VCC_MINOR_START;
	vcc_tty_driver->type = TTY_DRIVER_TYPE_SYSTEM;
	vcc_tty_driver->init_termios = vcc_tty_termios;

	tty_set_operations(vcc_tty_driver, &vcc_ops);

	rv = tty_register_driver(vcc_tty_driver);
	if (rv) {
		pr_err("VCC: TTY driver registration failed\n");
		put_tty_driver(vcc_tty_driver);
		vcc_tty_driver = NULL;
		return rv;
	}

	vccdbg("VCC: TTY driver registered\n");

	return 0;
}

static void vcc_tty_exit(void)
{
	tty_unregister_driver(vcc_tty_driver);
	put_tty_driver(vcc_tty_driver);
	vccdbg("VCC: TTY driver unregistered\n");

	vcc_tty_driver = NULL;
}

static int __init vcc_init(void)
{
	int rv;

	rv = vcc_tty_init();
	if (rv) {
		pr_err("VCC: TTY init failed\n");
		return rv;
	}

	return rv;
}

static void __exit vcc_exit(void)
{
	vcc_tty_exit();
	vccdbg("VCC: TTY driver unregistered\n");
}

module_init(vcc_init);
module_exit(vcc_exit);
