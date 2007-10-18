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

#include <asm/uaccess.h>

#if defined(CONFIG_SYSCTL) && defined(CONFIG_PROC_FS)

#define PARPORT_MIN_TIMESLICE_VALUE 1ul 
#define PARPORT_MAX_TIMESLICE_VALUE ((unsigned long) HZ)
#define PARPORT_MIN_SPINTIME_VALUE 1
#define PARPORT_MAX_SPINTIME_VALUE 1000

static int do_active_device(ctl_table *table, int write, struct file *filp,
		      void __user *result, size_t *lenp, loff_t *ppos)
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
			len += sprintf(buffer, "%s\n", dev->name);
		}
	}

	if(!len) {
		len += sprintf(buffer, "%s\n", "none");
	}

	if (len > *lenp)
		len = *lenp;
	else
		*lenp = len;

	*ppos += len;

	return copy_to_user(result, buffer, len) ? -EFAULT : 0;
}

#ifdef CONFIG_PARPORT_1284
static int do_autoprobe(ctl_table *table, int write, struct file *filp,
			void __user *result, size_t *lenp, loff_t *ppos)
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
		len += sprintf (buffer + len, "CLASS:%s;\n", str);

	if ((str = info->model) != NULL)
		len += sprintf (buffer + len, "MODEL:%s;\n", str);

	if ((str = info->mfr) != NULL)
		len += sprintf (buffer + len, "MANUFACTURER:%s;\n", str);

	if ((str = info->description) != NULL)
		len += sprintf (buffer + len, "DESCRIPTION:%s;\n", str);

	if ((str = info->cmdset) != NULL)
		len += sprintf (buffer + len, "COMMAND SET:%s;\n", str);

	if (len > *lenp)
		len = *lenp;
	else
		*lenp = len;

	*ppos += len;

	return copy_to_user (result, buffer, len) ? -EFAULT : 0;
}
#endif /* IEEE1284.3 support. */

static int do_hardware_base_addr (ctl_table *table, int write,
				  struct file *filp, void __user *result,
				  size_t *lenp, loff_t *ppos)
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

	len += sprintf (buffer, "%lu\t%lu\n", port->base, port->base_hi);

	if (len > *lenp)
		len = *lenp;
	else
		*lenp = len;

	*ppos += len;

	return copy_to_user(result, buffer, len) ? -EFAULT : 0;
}

static int do_hardware_irq (ctl_table *table, int write,
			    struct file *filp, void __user *result,
			    size_t *lenp, loff_t *ppos)
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

	len += sprintf (buffer, "%d\n", port->irq);

	if (len > *lenp)
		len = *lenp;
	else
		*lenp = len;

	*ppos += len;

	return copy_to_user(result, buffer, len) ? -EFAULT : 0;
}

static int do_hardware_dma (ctl_table *table, int write,
			    struct file *filp, void __user *result,
			    size_t *lenp, loff_t *ppos)
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

	len += sprintf (buffer, "%d\n", port->dma);

	if (len > *lenp)
		len = *lenp;
	else
		*lenp = len;

	*ppos += len;

	return copy_to_user(result, buffer, len) ? -EFAULT : 0;
}

static int do_hardware_modes (ctl_table *table, int write,
			      struct file *filp, void __user *result,
			      size_t *lenp, loff_t *ppos)
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
#define printmode(x) {if(port->modes&PARPORT_MODE_##x){len+=sprintf(buffer+len,"%s%s",f?",":"",#x);f++;}}
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

	return copy_to_user(result, buffer, len) ? -EFAULT : 0;
}

#define PARPORT_PORT_DIR(CHILD) { .ctl_name = 0, .procname = NULL, .mode = 0555, .child = CHILD }
#define PARPORT_PARPORT_DIR(CHILD) { .ctl_name = DEV_PARPORT, .procname = "parport", \
                                     .mode = 0555, .child = CHILD }
#define PARPORT_DEV_DIR(CHILD) { .ctl_name = CTL_DEV, .procname = "dev", .mode = 0555, .child = CHILD }
#define PARPORT_DEVICES_ROOT_DIR  {  .procname = "devices", \
                                    .mode = 0555, .child = NULL }

static const unsigned long parport_min_timeslice_value =
PARPORT_MIN_TIMESLICE_VALUE;

static const unsigned long parport_max_timeslice_value =
PARPORT_MAX_TIMESLICE_VALUE;

static const  int parport_min_spintime_value =
PARPORT_MIN_SPINTIME_VALUE;

static const int parport_max_spintime_value =
PARPORT_MAX_SPINTIME_VALUE;


struct parport_sysctl_table {
	struct ctl_table_header *sysctl_header;
	ctl_table vars[12];
	ctl_table device_dir[2];
	ctl_table port_dir[2];
	ctl_table parport_dir[2];
	ctl_table dev_dir[2];
};

static const struct parport_sysctl_table parport_sysctl_template = {
	.sysctl_header = NULL,
        {
		{
			.procname	= "spintime",
			.data		= NULL,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= &proc_dointvec_minmax,
			.extra1		= (void*) &parport_min_spintime_value,
			.extra2		= (void*) &parport_max_spintime_value
		},
		{
			.procname	= "base-addr",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= &do_hardware_base_addr
		},
		{
			.procname	= "irq",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= &do_hardware_irq
		},
		{
			.procname	= "dma",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= &do_hardware_dma
		},
		{
			.procname	= "modes",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= &do_hardware_modes
		},
		PARPORT_DEVICES_ROOT_DIR,
#ifdef CONFIG_PARPORT_1284
		{
			.procname	= "autoprobe",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= &do_autoprobe
		},
		{
			.procname	= "autoprobe0",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	=  &do_autoprobe
		},
		{
			.procname	= "autoprobe1",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= &do_autoprobe
		},
		{
			.procname	= "autoprobe2",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= &do_autoprobe
		},
		{
			.procname	= "autoprobe3",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= &do_autoprobe
		},
#endif /* IEEE 1284 support */
		{}
	},
	{
		{
			.procname	= "active",
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0444,
			.proc_handler	= &do_active_device
		},
		{}
	},
	{
		PARPORT_PORT_DIR(NULL),
		{}
	},
	{
		PARPORT_PARPORT_DIR(NULL),
		{}
	},
	{
		PARPORT_DEV_DIR(NULL),
		{}
	}
};

struct parport_device_sysctl_table
{
	struct ctl_table_header *sysctl_header;
	ctl_table vars[2];
	ctl_table device_dir[2];
	ctl_table devices_root_dir[2];
	ctl_table port_dir[2];
	ctl_table parport_dir[2];
	ctl_table dev_dir[2];
};

static const struct parport_device_sysctl_table
parport_device_sysctl_template = {
	.sysctl_header = NULL,
	{
		{
			.procname 	= "timeslice",
			.data		= NULL,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= &proc_doulongvec_ms_jiffies_minmax,
			.extra1		= (void*) &parport_min_timeslice_value,
			.extra2		= (void*) &parport_max_timeslice_value
		},
	},
	{
		{
			.ctl_name	= 0,
			.procname	= NULL,
			.data		= NULL,
			.maxlen		= 0,
			.mode		= 0555,
			.child		= NULL
		},
		{}
	},
	{
		PARPORT_DEVICES_ROOT_DIR,
		{}
	},
	{
		PARPORT_PORT_DIR(NULL),
		{}
	},
	{
		PARPORT_PARPORT_DIR(NULL),
		{}
	},
	{
		PARPORT_DEV_DIR(NULL),
		{}
	}
};

struct parport_default_sysctl_table
{
	struct ctl_table_header *sysctl_header;
	ctl_table vars[3];
        ctl_table default_dir[2];
	ctl_table parport_dir[2];
	ctl_table dev_dir[2];
};

extern unsigned long parport_default_timeslice;
extern int parport_default_spintime;

static struct parport_default_sysctl_table
parport_default_sysctl_table = {
	.sysctl_header	= NULL,
	{
		{
			.procname	= "timeslice",
			.data		= &parport_default_timeslice,
			.maxlen		= sizeof(parport_default_timeslice),
			.mode		= 0644,
			.proc_handler	= &proc_doulongvec_ms_jiffies_minmax,
			.extra1		= (void*) &parport_min_timeslice_value,
			.extra2		= (void*) &parport_max_timeslice_value
		},
		{
			.procname	= "spintime",
			.data		= &parport_default_spintime,
			.maxlen		= sizeof(parport_default_spintime),
			.mode		= 0644,
			.proc_handler	= &proc_dointvec_minmax,
			.extra1		= (void*) &parport_min_spintime_value,
			.extra2		= (void*) &parport_max_spintime_value
		},
		{}
	},
	{
		{
			.ctl_name	= DEV_PARPORT_DEFAULT,
			.procname	= "default",
			.mode		= 0555,
			.child		= parport_default_sysctl_table.vars
		},
		{}
	},
	{
		PARPORT_PARPORT_DIR(parport_default_sysctl_table.default_dir),
		{}
	},
	{
		PARPORT_DEV_DIR(parport_default_sysctl_table.parport_dir),
		{}
	}
};


int parport_proc_register(struct parport *port)
{
	struct parport_sysctl_table *t;
	int i;

	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	memcpy(t, &parport_sysctl_template, sizeof(*t));

	t->device_dir[0].extra1 = port;

	for (i = 0; i < 5; i++)
		t->vars[i].extra1 = port;

	t->vars[0].data = &port->spintime;
	t->vars[5].child = t->device_dir;
	
	for (i = 0; i < 5; i++)
		t->vars[6 + i].extra2 = &port->probe_info[i];

	t->port_dir[0].procname = port->name;
	t->port_dir[0].ctl_name = 0;

	t->port_dir[0].child = t->vars;
	t->parport_dir[0].child = t->port_dir;
	t->dev_dir[0].child = t->parport_dir;

	t->sysctl_header = register_sysctl_table(t->dev_dir);
	if (t->sysctl_header == NULL) {
		kfree(t);
		t = NULL;
	}
	port->sysctl_table = t;
	return 0;
}

int parport_proc_unregister(struct parport *port)
{
	if (port->sysctl_table) {
		struct parport_sysctl_table *t = port->sysctl_table;
		port->sysctl_table = NULL;
		unregister_sysctl_table(t->sysctl_header);
		kfree(t);
	}
	return 0;
}

int parport_device_proc_register(struct pardevice *device)
{
	struct parport_device_sysctl_table *t;
	struct parport * port = device->port;
	
	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	memcpy(t, &parport_device_sysctl_template, sizeof(*t));

	t->dev_dir[0].child = t->parport_dir;
	t->parport_dir[0].child = t->port_dir;
	t->port_dir[0].procname = port->name;
	t->port_dir[0].ctl_name = 0;
	t->port_dir[0].child = t->devices_root_dir;
	t->devices_root_dir[0].child = t->device_dir;

	t->device_dir[0].ctl_name = 0;
	t->device_dir[0].procname = device->name;
	t->device_dir[0].child = t->vars;
	t->vars[0].data = &device->timeslice;

	t->sysctl_header = register_sysctl_table(t->dev_dir);
	if (t->sysctl_header == NULL) {
		kfree(t);
		t = NULL;
	}
	device->sysctl_table = t;
	return 0;
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
	parport_default_sysctl_table.sysctl_header =
		register_sysctl_table(parport_default_sysctl_table.dev_dir);
	return 0;
}

static void __exit parport_default_proc_unregister(void)
{
	if (parport_default_sysctl_table.sysctl_header) {
		unregister_sysctl_table(parport_default_sysctl_table.
					sysctl_header);
		parport_default_sysctl_table.sysctl_header = NULL;
	}
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
	return 0;
}

static void __exit parport_default_proc_unregister (void)
{
}
#endif

module_init(parport_default_proc_register)
module_exit(parport_default_proc_unregister)
