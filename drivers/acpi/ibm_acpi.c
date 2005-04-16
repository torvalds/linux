/*
 *  ibm_acpi.c - IBM ThinkPad ACPI Extras
 *
 *
 *  Copyright (C) 2004 Borislav Deianov
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Changelog:
 *
 *  2004-08-09	0.1	initial release, support for X series
 *  2004-08-14	0.2	support for T series, X20
 *			bluetooth enable/disable
 *			hotkey events disabled by default
 *			removed fan control, currently useless
 *  2004-08-17	0.3	support for R40
 *			lcd off, brightness control
 *			thinklight on/off
 *  2004-09-16	0.4	support for module parameters
 *			hotkey mask can be prefixed by 0x
 *			video output switching
 *			video expansion control
 *			ultrabay eject support
 *			removed lcd brightness/on/off control, didn't work
 *  2004-10-18	0.5	thinklight support on A21e, G40, R32, T20, T21, X20
 *			proc file format changed
 *			video_switch command
 *			experimental cmos control
 *			experimental led control
 *			experimental acpi sounds
 *  2004-10-19	0.6	use acpi_bus_register_driver() to claim HKEY device
 *  2004-10-23	0.7	fix module loading on A21e, A22p, T20, T21, X20
 *			fix LED control on A21e
 *  2004-11-08	0.8	fix init error case, don't return from a macro
 *				thanks to Chris Wright <chrisw@osdl.org>
 */

#define IBM_VERSION "0.8"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#include <acpi/acpi_drivers.h>
#include <acpi/acnamesp.h>

#define IBM_NAME "ibm"
#define IBM_DESC "IBM ThinkPad ACPI Extras"
#define IBM_FILE "ibm_acpi"
#define IBM_URL "http://ibm-acpi.sf.net/"

#define IBM_DIR IBM_NAME

#define IBM_LOG IBM_FILE ": "
#define IBM_ERR	   KERN_ERR    IBM_LOG
#define IBM_NOTICE KERN_NOTICE IBM_LOG
#define IBM_INFO   KERN_INFO   IBM_LOG
#define IBM_DEBUG  KERN_DEBUG  IBM_LOG

#define IBM_MAX_ACPI_ARGS 3

#define __unused __attribute__ ((unused))

static int experimental;
module_param(experimental, int, 0);

static acpi_handle root_handle = NULL;

#define IBM_HANDLE(object, parent, paths...)			\
	static acpi_handle  object##_handle;			\
	static acpi_handle *object##_parent = &parent##_handle;	\
	static char        *object##_paths[] = { paths }

IBM_HANDLE(ec, root,
	   "\\_SB.PCI0.ISA.EC",    /* A21e, A22p, T20, T21, X20 */
	   "\\_SB.PCI0.LPC.EC",    /* all others */
);

IBM_HANDLE(vid, root, 
	   "\\_SB.PCI0.VID",       /* A21e, G40, X30, X40 */
	   "\\_SB.PCI0.AGP.VID",   /* all others */
);

IBM_HANDLE(cmos, root,
	   "\\UCMS",               /* R50, R50p, R51, T4x, X31, X40 */
	   "\\CMOS",               /* A3x, G40, R32, T23, T30, X22, X24, X30 */
	   "\\CMS",                /* R40, R40e */
);                                 /* A21e, A22p, T20, T21, X20 */

IBM_HANDLE(dock, root,
	   "\\_SB.GDCK",           /* X30, X31, X40 */
	   "\\_SB.PCI0.DOCK",      /* A22p, T20, T21, X20 */
	   "\\_SB.PCI0.PCI1.DOCK", /* all others */
);                                 /* A21e, G40, R32, R40, R40e */

IBM_HANDLE(bay, root,
	   "\\_SB.PCI0.IDE0.SCND.MSTR");      /* all except A21e */
IBM_HANDLE(bayej, root,
	   "\\_SB.PCI0.IDE0.SCND.MSTR._EJ0"); /* all except A2x, A3x */

IBM_HANDLE(lght, root, "\\LGHT");  /* A21e, A22p, T20, T21, X20 */
IBM_HANDLE(hkey, ec,   "HKEY");    /* all */
IBM_HANDLE(led,  ec,   "LED");     /* all except A21e, A22p, T20, T21, X20 */
IBM_HANDLE(sysl, ec,   "SYSL");    /* A21e, A22p, T20, T21, X20 */
IBM_HANDLE(bled, ec,   "BLED");    /* A22p, T20, T21, X20 */
IBM_HANDLE(beep, ec,   "BEEP");    /* all models */

struct ibm_struct {
	char *name;

	char *hid;
	struct acpi_driver *driver;
	
	int  (*init)   (struct ibm_struct *);
	int  (*read)   (struct ibm_struct *, char *);
	int  (*write)  (struct ibm_struct *, char *);
	void (*exit)   (struct ibm_struct *);

	void (*notify) (struct ibm_struct *, u32);	
	acpi_handle *handle;
	int type;
	struct acpi_device *device;

	int driver_registered;
	int proc_created;
	int init_called;
	int notify_installed;

	int supported;
	union {
		struct {
			int status;
			int mask;
		} hotkey;
		struct {
			int autoswitch;
		} video;
	} state;

	int experimental;
};

static struct proc_dir_entry *proc_dir = NULL;

#define onoff(status,bit) ((status) & (1 << (bit)) ? "on" : "off")
#define enabled(status,bit) ((status) & (1 << (bit)) ? "enabled" : "disabled")
#define strlencmp(a,b) (strncmp((a), (b), strlen(b)))

static int acpi_evalf(acpi_handle handle,
		      void *res, char *method, char *fmt, ...)
{
	char *fmt0 = fmt;
        struct acpi_object_list	params;
        union acpi_object	in_objs[IBM_MAX_ACPI_ARGS];
        struct acpi_buffer	result;
        union acpi_object	out_obj;
        acpi_status		status;
	va_list			ap;
	char			res_type;
	int			success;
	int			quiet;

	if (!*fmt) {
		printk(IBM_ERR "acpi_evalf() called with empty format\n");
		return 0;
	}

	if (*fmt == 'q') {
		quiet = 1;
		fmt++;
	} else
		quiet = 0;

	res_type = *(fmt++);

	params.count = 0;
	params.pointer = &in_objs[0];

	va_start(ap, fmt);
	while (*fmt) {
		char c = *(fmt++);
		switch (c) {
		case 'd':	/* int */
			in_objs[params.count].integer.value = va_arg(ap, int);
			in_objs[params.count++].type = ACPI_TYPE_INTEGER;
			break;
		/* add more types as needed */
		default:
			printk(IBM_ERR "acpi_evalf() called "
			       "with invalid format character '%c'\n", c);
			return 0;
		}
	}
	va_end(ap);

	result.length = sizeof(out_obj);
	result.pointer = &out_obj;

	status = acpi_evaluate_object(handle, method, &params, &result);

	switch (res_type) {
	case 'd':	/* int */
		if (res)
			*(int *)res = out_obj.integer.value;
		success = status == AE_OK && out_obj.type == ACPI_TYPE_INTEGER;
		break;
	case 'v':	/* void */
		success = status == AE_OK;
		break;
	/* add more types as needed */
	default:
		printk(IBM_ERR "acpi_evalf() called "
		       "with invalid format character '%c'\n", res_type);
		return 0;
	}

	if (!success && !quiet)
		printk(IBM_ERR "acpi_evalf(%s, %s, ...) failed: %d\n",
		       method, fmt0, status);

	return success;
}

static void __unused acpi_print_int(acpi_handle handle, char *method)
{
	int i;

	if (acpi_evalf(handle, &i, method, "d"))
		printk(IBM_INFO "%s = 0x%x\n", method, i);
	else
		printk(IBM_ERR "error calling %s\n", method);
}

static char *next_cmd(char **cmds)
{
	char *start = *cmds;
	char *end;

	while ((end = strchr(start, ',')) && end == start)
		start = end + 1;

	if (!end)
		return NULL;

	*end = 0;
	*cmds = end + 1;
	return start;
}

static int driver_init(struct ibm_struct *ibm)
{
	printk(IBM_INFO "%s v%s\n", IBM_DESC, IBM_VERSION);
	printk(IBM_INFO "%s\n", IBM_URL);

	return 0;
}

static int driver_read(struct ibm_struct *ibm, char *p)
{
	int len = 0;

	len += sprintf(p + len, "driver:\t\t%s\n", IBM_DESC);
	len += sprintf(p + len, "version:\t%s\n", IBM_VERSION);

	return len;
}

static int hotkey_get(struct ibm_struct *ibm, int *status, int *mask)
{
	if (!acpi_evalf(hkey_handle, status, "DHKC", "d"))
		return -EIO;
	if (ibm->supported) {
		if (!acpi_evalf(hkey_handle, mask, "DHKN", "qd"))
			return -EIO;
	} else {
		*mask = ibm->state.hotkey.mask;
	}
	return 0;
}

static int hotkey_set(struct ibm_struct *ibm, int status, int mask)
{
	int i;

	if (!acpi_evalf(hkey_handle, NULL, "MHKC", "vd", status))
		return -EIO;

	if (!ibm->supported)
		return 0;

	for (i=0; i<32; i++) {
		int bit = ((1 << i) & mask) != 0;
		if (!acpi_evalf(hkey_handle, NULL, "MHKM", "vdd", i+1, bit))
			return -EIO;
	}

	return 0;
}

static int hotkey_init(struct ibm_struct *ibm)
{
	int ret;

	ibm->supported = 1;
	ret = hotkey_get(ibm,
			 &ibm->state.hotkey.status,
			 &ibm->state.hotkey.mask);
	if (ret < 0) {
		/* mask not supported on A21e, A22p, T20, T21, X20, X22, X24 */
		ibm->supported = 0;
		ret = hotkey_get(ibm,
				 &ibm->state.hotkey.status,
				 &ibm->state.hotkey.mask);
	}

	return ret;
}	

static int hotkey_read(struct ibm_struct *ibm, char *p)
{
	int status, mask;
	int len = 0;

	if (hotkey_get(ibm, &status, &mask) < 0)
		return -EIO;

	len += sprintf(p + len, "status:\t\t%s\n", enabled(status, 0));
	if (ibm->supported) {
		len += sprintf(p + len, "mask:\t\t0x%04x\n", mask);
		len += sprintf(p + len,
			       "commands:\tenable, disable, reset, <mask>\n");
	} else {
		len += sprintf(p + len, "mask:\t\tnot supported\n");
		len += sprintf(p + len, "commands:\tenable, disable, reset\n");
	}

	return len;
}

static int hotkey_write(struct ibm_struct *ibm, char *buf)
{
	int status, mask;
	char *cmd;
	int do_cmd = 0;

	if (hotkey_get(ibm, &status, &mask) < 0)
		return -ENODEV;

	while ((cmd = next_cmd(&buf))) {
		if (strlencmp(cmd, "enable") == 0) {
			status = 1;
		} else if (strlencmp(cmd, "disable") == 0) {
			status = 0;
		} else if (strlencmp(cmd, "reset") == 0) {
			status = ibm->state.hotkey.status;
			mask   = ibm->state.hotkey.mask;
		} else if (sscanf(cmd, "0x%x", &mask) == 1) {
			/* mask set */
		} else if (sscanf(cmd, "%x", &mask) == 1) {
			/* mask set */
		} else
			return -EINVAL;
		do_cmd = 1;
	}

	if (do_cmd && hotkey_set(ibm, status, mask) < 0)
		return -EIO;

	return 0;
}	

static void hotkey_exit(struct ibm_struct *ibm)
{
	hotkey_set(ibm, ibm->state.hotkey.status, ibm->state.hotkey.mask);
}

static void hotkey_notify(struct ibm_struct *ibm, u32 event)
{
	int hkey;

	if (acpi_evalf(hkey_handle, &hkey, "MHKP", "d"))
		acpi_bus_generate_event(ibm->device, event, hkey);
	else {
		printk(IBM_ERR "unknown hotkey event %d\n", event);
		acpi_bus_generate_event(ibm->device, event, 0);
	}	
}

static int bluetooth_init(struct ibm_struct *ibm)
{
	/* bluetooth not supported on A21e, G40, T20, T21, X20 */
	ibm->supported = acpi_evalf(hkey_handle, NULL, "GBDC", "qv");

	return 0;
}

static int bluetooth_status(struct ibm_struct *ibm)
{
	int status;

	if (!ibm->supported || !acpi_evalf(hkey_handle, &status, "GBDC", "d"))
		status = 0;

	return status;
}

static int bluetooth_read(struct ibm_struct *ibm, char *p)
{
	int len = 0;
	int status = bluetooth_status(ibm);

	if (!ibm->supported)
		len += sprintf(p + len, "status:\t\tnot supported\n");
	else if (!(status & 1))
		len += sprintf(p + len, "status:\t\tnot installed\n");
	else {
		len += sprintf(p + len, "status:\t\t%s\n", enabled(status, 1));
		len += sprintf(p + len, "commands:\tenable, disable\n");
	}

	return len;
}

static int bluetooth_write(struct ibm_struct *ibm, char *buf)
{
	int status = bluetooth_status(ibm);
	char *cmd;
	int do_cmd = 0;

	if (!ibm->supported)
		return -EINVAL;

	while ((cmd = next_cmd(&buf))) {
		if (strlencmp(cmd, "enable") == 0) {
			status |= 2;
		} else if (strlencmp(cmd, "disable") == 0) {
			status &= ~2;
		} else
			return -EINVAL;
		do_cmd = 1;
	}

	if (do_cmd && !acpi_evalf(hkey_handle, NULL, "SBDC", "vd", status))
	    return -EIO;

	return 0;
}

static int video_init(struct ibm_struct *ibm)
{
	if (!acpi_evalf(vid_handle,
			&ibm->state.video.autoswitch, "^VDEE", "d"))
		return -ENODEV;

	return 0;
}

static int video_status(struct ibm_struct *ibm)
{
	int status = 0;
	int i;

	acpi_evalf(NULL, NULL, "\\VUPS", "vd", 1);
	if (acpi_evalf(NULL, &i, "\\VCDC", "d"))
		status |= 0x02 * i;

	acpi_evalf(NULL, NULL, "\\VUPS", "vd", 0);
	if (acpi_evalf(NULL, &i, "\\VCDL", "d"))
		status |= 0x01 * i;
	if (acpi_evalf(NULL, &i, "\\VCDD", "d"))
		status |= 0x08 * i;

	if (acpi_evalf(vid_handle, &i, "^VDEE", "d"))
		status |= 0x10 * (i & 1);

	return status;
}

static int video_read(struct ibm_struct *ibm, char *p)
{
	int status = video_status(ibm);
	int len = 0;

	len += sprintf(p + len, "lcd:\t\t%s\n", enabled(status, 0));
	len += sprintf(p + len, "crt:\t\t%s\n", enabled(status, 1));
	len += sprintf(p + len, "dvi:\t\t%s\n", enabled(status, 3));
	len += sprintf(p + len, "auto:\t\t%s\n", enabled(status, 4));
	len += sprintf(p + len, "commands:\tlcd_enable, lcd_disable, "
		       "crt_enable, crt_disable\n");
	len += sprintf(p + len, "commands:\tdvi_enable, dvi_disable, "
		       "auto_enable, auto_disable\n");
	len += sprintf(p + len, "commands:\tvideo_switch, expand_toggle\n");

	return len;
}

static int video_write(struct ibm_struct *ibm, char *buf)
{
	char *cmd;
	int enable, disable, status;

	enable = disable = 0;

	while ((cmd = next_cmd(&buf))) {
		if (strlencmp(cmd, "lcd_enable") == 0) {
			enable |= 0x01;
		} else if (strlencmp(cmd, "lcd_disable") == 0) {
			disable |= 0x01;
		} else if (strlencmp(cmd, "crt_enable") == 0) {
			enable |= 0x02;
		} else if (strlencmp(cmd, "crt_disable") == 0) {
			disable |= 0x02;
		} else if (strlencmp(cmd, "dvi_enable") == 0) {
			enable |= 0x08;
		} else if (strlencmp(cmd, "dvi_disable") == 0) {
			disable |= 0x08;
		} else if (strlencmp(cmd, "auto_enable") == 0) {
			if (!acpi_evalf(vid_handle, NULL, "_DOS", "vd", 1))
				return -EIO;
		} else if (strlencmp(cmd, "auto_disable") == 0) {
			if (!acpi_evalf(vid_handle, NULL, "_DOS", "vd", 0))
				return -EIO;
		} else if (strlencmp(cmd, "video_switch") == 0) {
			int autoswitch;
			if (!acpi_evalf(vid_handle, &autoswitch, "^VDEE", "d"))
				return -EIO;
			if (!acpi_evalf(vid_handle, NULL, "_DOS", "vd", 1))
				return -EIO;
			if (!acpi_evalf(vid_handle, NULL, "VSWT", "v"))
				return -EIO;
			if (!acpi_evalf(vid_handle, NULL, "_DOS", "vd",
					autoswitch))
				return -EIO;
		} else if (strlencmp(cmd, "expand_toggle") == 0) {
			if (!acpi_evalf(NULL, NULL, "\\VEXP", "v"))
				return -EIO;
		} else
			return -EINVAL;
	}

	if (enable || disable) {
		status = (video_status(ibm) & 0x0f & ~disable) | enable;
		if (!acpi_evalf(NULL, NULL, "\\VUPS", "vd", 0x80))
			return -EIO;
		if (!acpi_evalf(NULL, NULL, "\\VSDS", "vdd", status, 1))
			return -EIO;
	}

	return 0;
}

static void video_exit(struct ibm_struct *ibm)
{
	acpi_evalf(vid_handle, NULL, "_DOS", "vd",
		   ibm->state.video.autoswitch);
}

static int light_init(struct ibm_struct *ibm)
{
	/* kblt not supported on G40, R32, X20 */
	ibm->supported = acpi_evalf(ec_handle, NULL, "KBLT", "qv");

	return 0;
}

static int light_read(struct ibm_struct *ibm, char *p)
{
	int len = 0;
	int status = 0;

	if (ibm->supported) {
		if (!acpi_evalf(ec_handle, &status, "KBLT", "d"))
			return -EIO;
		len += sprintf(p + len, "status:\t\t%s\n", onoff(status, 0));
	} else
		len += sprintf(p + len, "status:\t\tunknown\n");

	len += sprintf(p + len, "commands:\ton, off\n");

	return len;
}

static int light_write(struct ibm_struct *ibm, char *buf)
{
	int cmos_cmd, lght_cmd;
	char *cmd;
	int success;
	
	while ((cmd = next_cmd(&buf))) {
		if (strlencmp(cmd, "on") == 0) {
			cmos_cmd = 0x0c;
			lght_cmd = 1;
		} else if (strlencmp(cmd, "off") == 0) {
			cmos_cmd = 0x0d;
			lght_cmd = 0;
		} else
			return -EINVAL;
		
		success = cmos_handle ?
			acpi_evalf(cmos_handle, NULL, NULL, "vd", cmos_cmd) :
			acpi_evalf(lght_handle, NULL, NULL, "vd", lght_cmd);
		if (!success)
			return -EIO;
	}

	return 0;
}

static int _sta(acpi_handle handle)
{
	int status;

	if (!handle || !acpi_evalf(handle, &status, "_STA", "d"))
		status = 0;

	return status;
}

#define dock_docked() (_sta(dock_handle) & 1)

static int dock_read(struct ibm_struct *ibm, char *p)
{
	int len = 0;
	int docked = dock_docked();

	if (!dock_handle)
		len += sprintf(p + len, "status:\t\tnot supported\n");
	else if (!docked)
		len += sprintf(p + len, "status:\t\tundocked\n");
	else {
		len += sprintf(p + len, "status:\t\tdocked\n");
		len += sprintf(p + len, "commands:\tdock, undock\n");
	}

	return len;
}

static int dock_write(struct ibm_struct *ibm, char *buf)
{
	char *cmd;

	if (!dock_docked())
		return -EINVAL;

	while ((cmd = next_cmd(&buf))) {
		if (strlencmp(cmd, "undock") == 0) {
			if (!acpi_evalf(dock_handle, NULL, "_DCK", "vd", 0))
				return -EIO;
			if (!acpi_evalf(dock_handle, NULL, "_EJ0", "vd", 1))
				return -EIO;
		} else if (strlencmp(cmd, "dock") == 0) {
			if (!acpi_evalf(dock_handle, NULL, "_DCK", "vd", 1))
				return -EIO;
		} else
			return -EINVAL;
	}

	return 0;
}	

static void dock_notify(struct ibm_struct *ibm, u32 event)
{
	int docked = dock_docked();

	if (event == 3 && docked)
		acpi_bus_generate_event(ibm->device, event, 1); /* button */
	else if (event == 3 && !docked)
		acpi_bus_generate_event(ibm->device, event, 2); /* undock */
	else if (event == 0 && docked)
		acpi_bus_generate_event(ibm->device, event, 3); /* dock */
	else {
		printk(IBM_ERR "unknown dock event %d, status %d\n",
		       event, _sta(dock_handle));
		acpi_bus_generate_event(ibm->device, event, 0); /* unknown */
	}
}

#define bay_occupied() (_sta(bay_handle) & 1)

static int bay_init(struct ibm_struct *ibm)
{
	/* bay not supported on A21e, A22p, A31, A31p, G40, R32, R40e */
	ibm->supported = bay_handle && bayej_handle &&
		acpi_evalf(bay_handle, NULL, "_STA", "qv");

	return 0;
}

static int bay_read(struct ibm_struct *ibm, char *p)
{
	int len = 0;
	int occupied = bay_occupied();
	
	if (!ibm->supported)
		len += sprintf(p + len, "status:\t\tnot supported\n");
	else if (!occupied)
		len += sprintf(p + len, "status:\t\tunoccupied\n");
	else {
		len += sprintf(p + len, "status:\t\toccupied\n");
		len += sprintf(p + len, "commands:\teject\n");
	}

	return len;
}

static int bay_write(struct ibm_struct *ibm, char *buf)
{
	char *cmd;

	while ((cmd = next_cmd(&buf))) {
		if (strlencmp(cmd, "eject") == 0) {
			if (!ibm->supported ||
			    !acpi_evalf(bay_handle, NULL, "_EJ0", "vd", 1))
				return -EIO;
		} else
			return -EINVAL;
	}

	return 0;
}	

static void bay_notify(struct ibm_struct *ibm, u32 event)
{
	acpi_bus_generate_event(ibm->device, event, 0);
}

static int cmos_read(struct ibm_struct *ibm, char *p)
{
	int len = 0;

	/* cmos not supported on A21e, A22p, T20, T21, X20 */
	if (!cmos_handle)
		len += sprintf(p + len, "status:\t\tnot supported\n");
	else {
		len += sprintf(p + len, "status:\t\tsupported\n");
		len += sprintf(p + len, "commands:\t<int>\n");
	}

	return len;
}

static int cmos_write(struct ibm_struct *ibm, char *buf)
{
	char *cmd;
	int cmos_cmd;

	if (!cmos_handle)
		return -EINVAL;

	while ((cmd = next_cmd(&buf))) {
		if (sscanf(cmd, "%u", &cmos_cmd) == 1) {
			/* cmos_cmd set */
		} else
			return -EINVAL;

		if (!acpi_evalf(cmos_handle, NULL, NULL, "vd", cmos_cmd))
			return -EIO;
	}

	return 0;
}	
		
static int led_read(struct ibm_struct *ibm, char *p)
{
	int len = 0;

	len += sprintf(p + len, "commands:\t"
		       "<int> on, <int> off, <int> blink\n");

	return len;
}

static int led_write(struct ibm_struct *ibm, char *buf)
{
	char *cmd;
	unsigned int led;
	int led_cmd, sysl_cmd, bled_a, bled_b;

	while ((cmd = next_cmd(&buf))) {
		if (sscanf(cmd, "%u", &led) != 1)
			return -EINVAL;

		if (strstr(cmd, "blink")) {
			led_cmd = 0xc0;
			sysl_cmd = 2;
			bled_a = 2;
			bled_b = 1;
		} else if (strstr(cmd, "on")) {
			led_cmd = 0x80;
			sysl_cmd = 1;
			bled_a = 2;
			bled_b = 0;
		} else if (strstr(cmd, "off")) {
			led_cmd = sysl_cmd = bled_a = bled_b = 0;
		} else
			return -EINVAL;
		
		if (led_handle) {
			if (!acpi_evalf(led_handle, NULL, NULL, "vdd",
					led, led_cmd))
				return -EIO;
		} else if (led < 2) {
			if (acpi_evalf(sysl_handle, NULL, NULL, "vdd",
				       led, sysl_cmd))
				return -EIO;
		} else if (led == 2 && bled_handle) {
			if (acpi_evalf(bled_handle, NULL, NULL, "vdd",
				       bled_a, bled_b))
				return -EIO;
		} else
			return -EINVAL;
	}

	return 0;
}	
		
static int beep_read(struct ibm_struct *ibm, char *p)
{
	int len = 0;

	len += sprintf(p + len, "commands:\t<int>\n");

	return len;
}

static int beep_write(struct ibm_struct *ibm, char *buf)
{
	char *cmd;
	int beep_cmd;

	while ((cmd = next_cmd(&buf))) {
		if (sscanf(cmd, "%u", &beep_cmd) == 1) {
			/* beep_cmd set */
		} else
			return -EINVAL;

		if (!acpi_evalf(beep_handle, NULL, NULL, "vd", beep_cmd))
			return -EIO;
	}

	return 0;
}	
		
static struct ibm_struct ibms[] = {
	{
		.name	= "driver",
		.init	= driver_init,
		.read	= driver_read,
	},
	{
		.name	= "hotkey",
		.hid	= "IBM0068",
		.init	= hotkey_init,
		.read	= hotkey_read,
		.write	= hotkey_write,
		.exit	= hotkey_exit,
		.notify	= hotkey_notify,
		.handle	= &hkey_handle,
		.type	= ACPI_DEVICE_NOTIFY,
	},
	{
		.name	= "bluetooth",
		.init	= bluetooth_init,
		.read	= bluetooth_read,
		.write	= bluetooth_write,
	},
	{
		.name	= "video",
		.init	= video_init,
		.read	= video_read,
		.write	= video_write,
		.exit	= video_exit,
	},
	{
		.name	= "light",
		.init	= light_init,
		.read	= light_read,
		.write	= light_write,
	},
	{
		.name	= "dock",
		.read	= dock_read,
		.write	= dock_write,
		.notify	= dock_notify,
		.handle	= &dock_handle,
		.type	= ACPI_SYSTEM_NOTIFY,
	},
	{
		.name	= "bay",
		.init	= bay_init,
		.read	= bay_read,
		.write	= bay_write,
		.notify	= bay_notify,
		.handle	= &bay_handle,
		.type	= ACPI_SYSTEM_NOTIFY,
	},
	{
		.name	= "cmos",
		.read	= cmos_read,
		.write	= cmos_write,
		.experimental = 1,
	},
	{
		.name	= "led",
		.read	= led_read,
		.write	= led_write,
		.experimental = 1,
	},
	{
		.name	= "beep",
		.read	= beep_read,
		.write	= beep_write,
		.experimental = 1,
	},
};
#define NUM_IBMS (sizeof(ibms)/sizeof(ibms[0]))

static int dispatch_read(char *page, char **start, off_t off, int count,
			 int *eof, void *data)
{
	struct ibm_struct *ibm = (struct ibm_struct *)data;
	int len;
	
	if (!ibm || !ibm->read)
		return -EINVAL;

	len = ibm->read(ibm, page);
	if (len < 0)
		return len;

	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

static int dispatch_write(struct file *file, const char __user *userbuf,
			  unsigned long count, void *data)
{
	struct ibm_struct *ibm = (struct ibm_struct *)data;
	char *kernbuf;
	int ret;

	if (!ibm || !ibm->write)
		return -EINVAL;

	kernbuf = kmalloc(count + 2, GFP_KERNEL);
	if (!kernbuf)
		return -ENOMEM;

        if (copy_from_user(kernbuf, userbuf, count)) {
		kfree(kernbuf);
                return -EFAULT;
	}

	kernbuf[count] = 0;
	strcat(kernbuf, ",");
	ret = ibm->write(ibm, kernbuf);
	if (ret == 0)
		ret = count;

	kfree(kernbuf);

        return ret;
}

static void dispatch_notify(acpi_handle handle, u32 event, void *data)
{
	struct ibm_struct *ibm = (struct ibm_struct *)data;

	if (!ibm || !ibm->notify)
		return;

	ibm->notify(ibm, event);
}

static int setup_notify(struct ibm_struct *ibm)
{
	acpi_status status;
	int ret;

	if (!*ibm->handle)
		return 0;

	ret = acpi_bus_get_device(*ibm->handle, &ibm->device);
	if (ret < 0) {
		printk(IBM_ERR "%s device not present\n", ibm->name);
		return 0;
	}

	acpi_driver_data(ibm->device) = ibm;
	sprintf(acpi_device_class(ibm->device), "%s/%s", IBM_NAME, ibm->name);

	status = acpi_install_notify_handler(*ibm->handle, ibm->type,
					     dispatch_notify, ibm);
	if (ACPI_FAILURE(status)) {
		printk(IBM_ERR "acpi_install_notify_handler(%s) failed: %d\n",
		       ibm->name, status);
		return -ENODEV;
	}

	ibm->notify_installed = 1;

	return 0;
}

static int device_add(struct acpi_device *device)
{
	return 0;
}

static int register_driver(struct ibm_struct *ibm)
{
	int ret;

	ibm->driver = kmalloc(sizeof(struct acpi_driver), GFP_KERNEL);
	if (!ibm->driver) {
		printk(IBM_ERR "kmalloc(ibm->driver) failed\n");
		return -1;
	}

	memset(ibm->driver, 0, sizeof(struct acpi_driver));
	sprintf(ibm->driver->name, "%s/%s", IBM_NAME, ibm->name);
	ibm->driver->ids = ibm->hid;
	ibm->driver->ops.add = &device_add;

	ret = acpi_bus_register_driver(ibm->driver);
	if (ret < 0) {
		printk(IBM_ERR "acpi_bus_register_driver(%s) failed: %d\n",
		       ibm->hid, ret);
		kfree(ibm->driver);
	}

	return ret;
}

static int ibm_init(struct ibm_struct *ibm)
{
	int ret;
	struct proc_dir_entry *entry;

	if (ibm->experimental && !experimental)
		return 0;

	if (ibm->hid) {
		ret = register_driver(ibm);
		if (ret < 0)
			return ret;
		ibm->driver_registered = 1;
	}

	if (ibm->init) {
		ret = ibm->init(ibm);
		if (ret != 0)
			return ret;
		ibm->init_called = 1;
	}

	entry = create_proc_entry(ibm->name, S_IFREG | S_IRUGO | S_IWUSR,
				  proc_dir);
	if (!entry) {
		printk(IBM_ERR "unable to create proc entry %s\n", ibm->name);
		return -ENODEV;
	}
	entry->owner = THIS_MODULE;
	ibm->proc_created = 1;
	
	entry->data = ibm;
	if (ibm->read)
		entry->read_proc = &dispatch_read;
	if (ibm->write)
		entry->write_proc = &dispatch_write;

	if (ibm->notify) {
		ret = setup_notify(ibm);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void ibm_exit(struct ibm_struct *ibm)
{
	if (ibm->notify_installed)
		acpi_remove_notify_handler(*ibm->handle, ibm->type,
					   dispatch_notify);

	if (ibm->proc_created)
		remove_proc_entry(ibm->name, proc_dir);

	if (ibm->init_called && ibm->exit)
		ibm->exit(ibm);

	if (ibm->driver_registered) {
		acpi_bus_unregister_driver(ibm->driver);
		kfree(ibm->driver);
	}
}

static int ibm_handle_init(char *name,
			   acpi_handle *handle, acpi_handle parent,
			   char **paths, int num_paths, int required)
{
	int i;
	acpi_status status;

	for (i=0; i<num_paths; i++) {
		status = acpi_get_handle(parent, paths[i], handle);
		if (ACPI_SUCCESS(status))
			return 0;
	}
	
	*handle = NULL;

	if (required) {
		printk(IBM_ERR "%s object not found\n", name);
		return -1;
	}

	return 0;
}

#define IBM_HANDLE_INIT(object, required)				\
	ibm_handle_init(#object, &object##_handle, *object##_parent,	\
		object##_paths, sizeof(object##_paths)/sizeof(char*), required)


static int set_ibm_param(const char *val, struct kernel_param *kp)
{
	unsigned int i;
	char arg_with_comma[32];

	if (strlen(val) > 30)
		return -ENOSPC;

	strcpy(arg_with_comma, val);
	strcat(arg_with_comma, ",");

	for (i=0; i<NUM_IBMS; i++)
		if (strcmp(ibms[i].name, kp->name) == 0)
			return ibms[i].write(&ibms[i], arg_with_comma);
	BUG();
	return -EINVAL;
}

#define IBM_PARAM(feature) \
	module_param_call(feature, set_ibm_param, NULL, NULL, 0)

static void acpi_ibm_exit(void)
{
	int i;

	for (i=NUM_IBMS-1; i>=0; i--)
		ibm_exit(&ibms[i]);

	remove_proc_entry(IBM_DIR, acpi_root_dir);
}

static int __init acpi_ibm_init(void)
{
	int ret, i;

	if (acpi_disabled)
		return -ENODEV;

	/* these handles are required */
	if (IBM_HANDLE_INIT(ec,	  1) < 0 ||
	    IBM_HANDLE_INIT(hkey, 1) < 0 ||
	    IBM_HANDLE_INIT(vid,  1) < 0 ||
	    IBM_HANDLE_INIT(beep, 1) < 0)
		return -ENODEV;

	/* these handles have alternatives */
	IBM_HANDLE_INIT(lght, 0);
	if (IBM_HANDLE_INIT(cmos, !lght_handle) < 0)
		return -ENODEV;
	IBM_HANDLE_INIT(sysl, 0);
	if (IBM_HANDLE_INIT(led, !sysl_handle) < 0)
		return -ENODEV;

	/* these handles are not required */
	IBM_HANDLE_INIT(dock,  0);
	IBM_HANDLE_INIT(bay,   0);
	IBM_HANDLE_INIT(bayej, 0);
	IBM_HANDLE_INIT(bled,  0);

	proc_dir = proc_mkdir(IBM_DIR, acpi_root_dir);
	if (!proc_dir) {
		printk(IBM_ERR "unable to create proc dir %s", IBM_DIR);
		return -ENODEV;
	}
	proc_dir->owner = THIS_MODULE;
	
	for (i=0; i<NUM_IBMS; i++) {
		ret = ibm_init(&ibms[i]);
		if (ret < 0) {
			acpi_ibm_exit();
			return ret;
		}
	}

	return 0;
}

module_init(acpi_ibm_init);
module_exit(acpi_ibm_exit);

MODULE_AUTHOR("Borislav Deianov");
MODULE_DESCRIPTION(IBM_DESC);
MODULE_LICENSE("GPL");

IBM_PARAM(hotkey);
IBM_PARAM(bluetooth);
IBM_PARAM(video);
IBM_PARAM(light);
IBM_PARAM(dock);
IBM_PARAM(bay);
IBM_PARAM(cmos);
IBM_PARAM(led);
IBM_PARAM(beep);
