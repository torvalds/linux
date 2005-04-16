/* Sysctl interface for parport devices.
 * 
 * Authors: David Campbell <campbell@torque.net>
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
#include <linux/config.h>
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

#define PARPORT_PORT_DIR(child) { 0, NULL, NULL, 0, 0555, child }
#define PARPORT_PARPORT_DIR(child) { DEV_PARPORT, "parport", \
                                     NULL, 0, 0555, child }
#define PARPORT_DEV_DIR(child) { CTL_DEV, "dev", NULL, 0, 0555, child }
#define PARPORT_DEVICES_ROOT_DIR  { DEV_PARPORT_DEVICES, "devices", \
                                    NULL, 0, 0555, NULL }

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
	NULL,
        {
		{ DEV_PARPORT_SPINTIME, "spintime",
		  NULL, sizeof(int), 0644, NULL,
		  &proc_dointvec_minmax, NULL, NULL,
		  (void*) &parport_min_spintime_value,
		  (void*) &parport_max_spintime_value },
		{ DEV_PARPORT_BASE_ADDR, "base-addr",
		  NULL, 0, 0444, NULL,
		  &do_hardware_base_addr },
		{ DEV_PARPORT_IRQ, "irq",
		  NULL, 0, 0444, NULL,
		  &do_hardware_irq },
		{ DEV_PARPORT_DMA, "dma",
		  NULL, 0, 0444, NULL,
		  &do_hardware_dma },
		{ DEV_PARPORT_MODES, "modes",
		  NULL, 0, 0444, NULL,
		  &do_hardware_modes },
		PARPORT_DEVICES_ROOT_DIR,
#ifdef CONFIG_PARPORT_1284
		{ DEV_PARPORT_AUTOPROBE, "autoprobe",
		  NULL, 0, 0444, NULL,
		  &do_autoprobe },
		{ DEV_PARPORT_AUTOPROBE + 1, "autoprobe0",
		 NULL, 0, 0444, NULL,
		 &do_autoprobe },
		{ DEV_PARPORT_AUTOPROBE + 2, "autoprobe1",
		  NULL, 0, 0444, NULL,
		  &do_autoprobe },
		{ DEV_PARPORT_AUTOPROBE + 3, "autoprobe2",
		  NULL, 0, 0444, NULL,
		  &do_autoprobe },
		{ DEV_PARPORT_AUTOPROBE + 4, "autoprobe3",
		  NULL, 0, 0444, NULL,
		  &do_autoprobe },
#endif /* IEEE 1284 support */
		{0}
	},
	{ {DEV_PARPORT_DEVICES_ACTIVE, "active", NULL, 0, 0444, NULL,
	  &do_active_device }, {0}},
	{ PARPORT_PORT_DIR(NULL), {0}},
	{ PARPORT_PARPORT_DIR(NULL), {0}},
	{ PARPORT_DEV_DIR(NULL), {0}}
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
	NULL,
	{
		{ DEV_PARPORT_DEVICE_TIMESLICE, "timeslice",
		  NULL, sizeof(int), 0644, NULL,
		  &proc_doulongvec_ms_jiffies_minmax, NULL, NULL,
		  (void*) &parport_min_timeslice_value,
		  (void*) &parport_max_timeslice_value },
	},
	{ {0, NULL, NULL, 0, 0555, NULL}, {0}},
	{ PARPORT_DEVICES_ROOT_DIR, {0}},
	{ PARPORT_PORT_DIR(NULL), {0}},
	{ PARPORT_PARPORT_DIR(NULL), {0}},
	{ PARPORT_DEV_DIR(NULL), {0}}
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
	NULL,
	{
		{ DEV_PARPORT_DEFAULT_TIMESLICE, "timeslice",
		  &parport_default_timeslice,
		  sizeof(parport_default_timeslice), 0644, NULL,
		  &proc_doulongvec_ms_jiffies_minmax, NULL, NULL,
		  (void*) &parport_min_timeslice_value,
		  (void*) &parport_max_timeslice_value },
		{ DEV_PARPORT_DEFAULT_SPINTIME, "spintime",
		  &parport_default_spintime,
		  sizeof(parport_default_spintime), 0644, NULL,
		  &proc_dointvec_minmax, NULL, NULL,
		  (void*) &parport_min_spintime_value,
		  (void*) &parport_max_spintime_value },
		{0}
	},
	{ { DEV_PARPORT_DEFAULT, "default", NULL, 0, 0555,
	    parport_default_sysctl_table.vars },{0}},
	{
	PARPORT_PARPORT_DIR(parport_default_sysctl_table.default_dir), 
	{0}},
	{ PARPORT_DEV_DIR(parport_default_sysctl_table.parport_dir), {0}}
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

	for (i = 0; i < 8; i++)
		t->vars[i].extra1 = port;

	t->vars[0].data = &port->spintime;
	t->vars[5].child = t->device_dir;
	
	for (i = 0; i < 5; i++)
		t->vars[6 + i].extra2 = &port->probe_info[i];

	t->port_dir[0].procname = port->name;
	t->port_dir[0].ctl_name = port->number + 1; /* nb 0 isn't legal here */

	t->port_dir[0].child = t->vars;
	t->parport_dir[0].child = t->port_dir;
	t->dev_dir[0].child = t->parport_dir;

	t->sysctl_header = register_sysctl_table(t->dev_dir, 0);
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
	t->port_dir[0].ctl_name = port->number + 1; /* nb 0 isn't legal here */
	t->port_dir[0].child = t->devices_root_dir;
	t->devices_root_dir[0].child = t->device_dir;

#ifdef CONFIG_PARPORT_1284

	t->device_dir[0].ctl_name =
		parport_device_num(port->number, port->muxport,
				   device->daisy)
		+ 1;  /* nb 0 isn't legal here */ 

#else /* No IEEE 1284 support */

	/* parport_device_num isn't available. */
	t->device_dir[0].ctl_name = 1;
	
#endif /* IEEE 1284 support or not */

	t->device_dir[0].procname = device->name;
	t->device_dir[0].extra1 = device;
	t->device_dir[0].child = t->vars;
	t->vars[0].data = &device->timeslice;

	t->sysctl_header = register_sysctl_table(t->dev_dir, 0);
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
		register_sysctl_table(parport_default_sysctl_table.dev_dir, 0);
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
