// SPDX-License-Identifier: GPL-2.0
/* Sysctl interface for parport devices.
 * 
 * Authors: David Campbell
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *          Philip Blundell <philb@gnu.org>
 *          Andrea Arcangeli
 *          Riccardo Facchetti <fizban@tin.it>
 *
 * based on work by Grant Guenther <grant@torque.net>
 *              and Philip Blundell
 *
 * Cleaned up include files - Russell King <linux@arm.uk.linux.org>
 */

#include <linux/string.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/parport.h>
#include <linux/ctype.h>
#include <linux/sysctl.h>
#include <linux/device.h>

#include <linux/uaccess.h>

#if defined(CONFIG_SYSCTL) && defined(CONFIG_PROC_FS)

#define PARPORT_MIN_TIMESLICE_VALUE 1ul 
#define PARPORT_MAX_TIMESLICE_VALUE ((unsigned long) HZ)
#define PARPORT_MIN_SPINTIME_VALUE 1
#define PARPORT_MAX_SPINTIME_VALUE 1000

static int do_active_device(struct ctl_table *table, int write,
		      void *result, size_t *lenp, loff_t *ppos)
{
	struct parport *port = (struct parport *)table->extra1;
	char buffer[256];
	struct pardevice *dev;
	int len = 0;

	if (write)		/* can't happen anyway */
		return -EACCES;

	if (*ppos) {
		*lenp = 0;
		return 0;
	}
	
	for (dev = port->devices; dev ; dev = dev->next) {
		if(dev == port->cad) {
			len += snprintf(buffer, sizeof(buffer), "%s\n", dev->name);
		}
	}

	if(!len) {
		len += snprintf(buffer, sizeof(buffer), "%s\n", "none");
	}

	if (len > *lenp)
		len = *lenp;
	else
		*lenp = len;

	*ppos += len;
	memcpy(result, buffer, len);
	return 0;
}

#ifdef CONFIG_PARPORT_1284
static int do_autoprobe(struct ctl_table *table, int write,
			void *result, size_t *lenp, loff_t *ppos)
{
	struct parport_device_info *info = table->extra2;
	const char *str;
	char buffer[256];
	int len = 0;

	if (write) /* permissions stop this */
		return -EACCES;

	if (*ppos) {
		*lenp = 0;
		return 0;
	}
	
	if ((str = info->class_name) != NULL)
		len += snprintf (buffer + len, sizeof(buffer) - len, "CLASS:%s;\n", str);

	if ((str = info->model) != NULL)
		len += snprintf (buffer + len, sizeof(buffer) - len, "MODEL:%s;\n", str);

	if ((str = info->mfr) != NULL)
		len += snprintf (buffer + len, sizeof(buffer) - len, "MANUFACTURER:%s;\n", str);

	if ((str = info->description) != NULL)
		len += snprintf (buffer + len, sizeof(buffer) - len, "DESCRIPTION:%s;\n", str);

	if ((str = info->cmdset) != NULL)
		len += snprintf (buffer + len, sizeof(buffer) - len, "COMMAND SET:%s;\n", str);

	if (len > *lenp)
		len = *lenp;
	else
		*lenp = len;

	*ppos += len;

	memcpy(result, buffer, len);
	return 0;
}
#endif /* IEEE1284.3 support. */

static int do_hardware_base_addr(struct ctl_table *table, int write,
				 void *result, size_t *lenp, loff_t *ppos)
{
	struct parport *port = (struct parport *)table->extra1;
	char buffer[64];
	int len = 0;

	if (*ppos) {
		*lenp = 0;
		return 0;
	}

	if (write) /* permissions prevent this anyway */
		return -EACCES;

	len += snprintf (buffer, sizeof(buffer), "%lu\t%lu\n", port->base, port->base_hi);

	if (len > *lenp)
		len = *lenp;
	else
		*lenp = len;

	*ppos += len;
	memcpy(result, buffer, len);
	return 0;
}

static int do_hardware_irq(struct ctl_table *table, int write,
			   void *result, size_t *lenp, loff_t *ppos)
{
	struct parport *port = (struct parport *)table->extra1;
	char buffer[20];
	int len = 0;

	if (*ppos) {
		*lenp = 0;
		return 0;
	}

	if (write) /* permissions prevent this anyway */
		return -EACCES;

	len += snprintf (buffer, sizeof(buffer), "%d\n", port->irq);

	if (len > *lenp)
		len = *lenp;
	else
		*lenp = len;

	*ppos += len;
	memcpy(result, buffer, len);
	return 0;
}

static int do_hardware_dma(struct ctl_table *table, int write,
			   void *result, size_t *lenp, loff_t *ppos)
{
	struct parport *port = (struct parport *)table->extra1;
	char buffer[20];
	int len = 0;

	if (*ppos) {
		*lenp = 0;
		return 0;
	}

	if (write) /* permissions prevent this anyway */
		return -EACCES;

	len += snprintf (buffer, sizeof(buffer), "%d\n", port->dma);

	if (len > *lenp)
		len = *lenp;
	else
		*lenp = len;

	*ppos += len;
	memcpy(result, buffer, len);
	return 0;
}

static int do_hardware_modes(struct ctl_table *table, int write,
			     void *result, size_t *lenp, loff_t *ppos)
{
	struct parport *port = (struct parport *)table->extra1;
	char buffer[40];
	int len = 0;

	if (*ppos) {
		*lenp = 0;
		return 0;
	}

	if (write) /* permissions prevent this anyway */
		return -EACCES;

	{
#define printmode(x)							\
do {									\
	if (port->modes & PARPORT_MODE_##x)				\
		len += snprintf(buffer + len, sizeof(buffer) - len, "%s%s", f++ ? "," : "", #x); \
} while (0)
		int f = 0;
		printmode(PCSPP);
		printmode(TRISTATE);
		printmode(COMPAT);
		printmode(EPP);
		printmode(ECP);
		printmode(DMA);
#undef printmode
	}
	buffer[len++] = '\n';

	if (len > *lenp)
		len = *lenp;
	else
		*lenp = len;

	*ppos += len;
	memcpy(result, buffer, len);
	return 0;
}

static const unsigned long parport_min_timeslice_value =
PARPORT_MIN_TIMESLICE_VALUE;

static const unsigned long parport_max_timeslice_value =
PARPORT_MAX_TIMESLICE_VALUE;

static const  int parport_min_spintime_value =
PARPORT_MIN_SPINTIME_VALUE;

static const int parport_max_spintime_value =
PARPORT_MAX_SPINTIME_VALUE;


struct parport_sysctl_table {
	struct ctl_table_header *port_header;
	struct ctl_table_header *devices_header;
#ifdef CONFIG_PARPORT_1284
	struct ctl_table vars[10];
#else
	struct ctl_table vars[5];
#endif /* IEEE 1284 support */
	struct ctl_table device_dir[1];
};

static const struct parport_sysctl_table parport_sysctl_template = {
	.port_header = NULL,
	.devices_header = NULL,
	{
		{
			.procname	= "spintime",
			.data		= NULL,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec_minmax,
			.extra1		= (void*) &parport_min_spintime_value,
			.extra2		= (void*) &parport_max_spintime_value
		},
		{
			.procname	= "base-addr",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= do_hardware_base_addr
		},
		{
			.procname	= "irq",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= do_hardware_irq
		},
		{
			.procname	= "dma",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= do_hardware_dma
		},
		{
			.procname	= "modes",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= do_hardware_modes
		},
#ifdef CONFIG_PARPORT_1284
		{
			.procname	= "autoprobe",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= do_autoprobe
		},
		{
			.procname	= "autoprobe0",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= do_autoprobe
		},
		{
			.procname	= "autoprobe1",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= do_autoprobe
		},
		{
			.procname	= "autoprobe2",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= do_autoprobe
		},
		{
			.procname	= "autoprobe3",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= do_autoprobe
		},
#endif /* IEEE 1284 support */
	},
	{
		{
			.procname	= "active",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= do_active_device
		},
	},
};

struct parport_device_sysctl_table
{
	struct ctl_table_header *sysctl_header;
	struct ctl_table vars[1];
	struct ctl_table device_dir[1];
};

static const struct parport_device_sysctl_table
parport_device_sysctl_template = {
	.sysctl_header = NULL,
	{
		{
			.procname 	= "timeslice",
			.data		= NULL,
			.maxlen		= sizeof(unsigned long),
			.mode		= 0644,
			.proc_handler	= proc_doulongvec_ms_jiffies_minmax,
			.extra1		= (void*) &parport_min_timeslice_value,
			.extra2		= (void*) &parport_max_timeslice_value
		},
	},
	{
		{
			.procname	= NULL,
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0555,
		},
	}
};

struct parport_default_sysctl_table
{
	struct ctl_table_header *sysctl_header;
	struct ctl_table vars[2];
};

static struct parport_default_sysctl_table
parport_default_sysctl_table = {
	.sysctl_header	= NULL,
	{
		{
			.procname	= "timeslice",
			.data		= &parport_default_timeslice,
			.maxlen		= sizeof(parport_default_timeslice),
			.mode		= 0644,
			.proc_handler	= proc_doulongvec_ms_jiffies_minmax,
			.extra1		= (void*) &parport_min_timeslice_value,
			.extra2		= (void*) &parport_max_timeslice_value
		},
		{
			.procname	= "spintime",
			.data		= &parport_default_spintime,
			.maxlen		= sizeof(parport_default_spintime),
			.mode		= 0644,
			.proc_handler	= proc_dointvec_minmax,
			.extra1		= (void*) &parport_min_spintime_value,
			.extra2		= (void*) &parport_max_spintime_value
		},
	}
};

int parport_proc_register(struct parport *port)
{
	struct parport_sysctl_table *t;
	char *tmp_dir_path;
	int i, err = 0;

	t = kmemdup(&parport_sysctl_template, sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;

	t->device_dir[0].extra1 = port;

	t->vars[0].data = &port->spintime;
	for (i = 0; i < 5; i++) {
		t->vars[i].extra1 = port;
#ifdef CONFIG_PARPORT_1284
		t->vars[5 + i].extra2 = &port->probe_info[i];
#endif /* IEEE 1284 support */
	}

	tmp_dir_path = kasprintf(GFP_KERNEL, "dev/parport/%s/devices", port->name);
	if (!tmp_dir_path) {
		err = -ENOMEM;
		goto exit_free_t;
	}

	t->devices_header = register_sysctl(tmp_dir_path, t->device_dir);
	if (t->devices_header == NULL) {
		err = -ENOENT;
		goto  exit_free_tmp_dir_path;
	}

	kfree(tmp_dir_path);

	tmp_dir_path = kasprintf(GFP_KERNEL, "dev/parport/%s", port->name);
	if (!tmp_dir_path) {
		err = -ENOMEM;
		goto unregister_devices_h;
	}

	t->port_header = register_sysctl(tmp_dir_path, t->vars);
	if (t->port_header == NULL) {
		err = -ENOENT;
		goto unregister_devices_h;
	}

	port->sysctl_table = t;

	kfree(tmp_dir_path);
	return 0;

unregister_devices_h:
	unregister_sysctl_table(t->devices_header);

exit_free_tmp_dir_path:
	kfree(tmp_dir_path);

exit_free_t:
	kfree(t);
	return err;
}

int parport_proc_unregister(struct parport *port)
{
	if (port->sysctl_table) {
		struct parport_sysctl_table *t = port->sysctl_table;
		port->sysctl_table = NULL;
		unregister_sysctl_table(t->devices_header);
		unregister_sysctl_table(t->port_header);
		kfree(t);
	}
	return 0;
}

int parport_device_proc_register(struct pardevice *device)
{
	struct parport_device_sysctl_table *t;
	struct parport * port = device->port;
	char *tmp_dir_path;
	int err = 0;
	
	t = kmemdup(&parport_device_sysctl_template, sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;

	/* Allocate a buffer for two paths: dev/parport/PORT/devices/DEVICE. */
	tmp_dir_path = kasprintf(GFP_KERNEL, "dev/parport/%s/devices/%s", port->name, device->name);
	if (!tmp_dir_path) {
		err = -ENOMEM;
		goto exit_free_t;
	}

	t->vars[0].data = &device->timeslice;

	t->sysctl_header = register_sysctl(tmp_dir_path, t->vars);
	if (t->sysctl_header == NULL) {
		kfree(t);
		t = NULL;
	}
	device->sysctl_table = t;

	kfree(tmp_dir_path);
	return 0;

exit_free_t:
	kfree(t);

	return err;
}

int parport_device_proc_unregister(struct pardevice *device)
{
	if (device->sysctl_table) {
		struct parport_device_sysctl_table *t = device->sysctl_table;
		device->sysctl_table = NULL;
		unregister_sysctl_table(t->sysctl_header);
		kfree(t);
	}
	return 0;
}

static int __init parport_default_proc_register(void)
{
	int ret;

	parport_default_sysctl_table.sysctl_header =
		register_sysctl("dev/parport/default", parport_default_sysctl_table.vars);
	if (!parport_default_sysctl_table.sysctl_header)
		return -ENOMEM;
	ret = parport_bus_init();
	if (ret) {
		unregister_sysctl_table(parport_default_sysctl_table.
					sysctl_header);
		return ret;
	}
	return 0;
}

static void __exit parport_default_proc_unregister(void)
{
	if (parport_default_sysctl_table.sysctl_header) {
		unregister_sysctl_table(parport_default_sysctl_table.
					sysctl_header);
		parport_default_sysctl_table.sysctl_header = NULL;
	}
	parport_bus_exit();
}

#else /* no sysctl or no procfs*/

int parport_proc_register(struct parport *pp)
{
	return 0;
}

int parport_proc_unregister(struct parport *pp)
{
	return 0;
}

int parport_device_proc_register(struct pardevice *device)
{
	return 0;
}

int parport_device_proc_unregister(struct pardevice *device)
{
	return 0;
}

static int __init parport_default_proc_register (void)
{
	return parport_bus_init();
}

static void __exit parport_default_proc_unregister (void)
{
	parport_bus_exit();
}
#endif

subsys_initcall(parport_default_proc_register)
module_exit(parport_default_proc_unregister)
