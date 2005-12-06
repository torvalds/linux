/* $Id: parport_probe.c,v 1.1 1999/07/03 08:56:17 davem Exp $
 * Parallel port device probing code
 *
 * Authors:    Carsten Gross, carsten@sol.wohnheim.uni-ulm.de
 *             Philip Blundell <philb@gnu.org>
 */

#include <linux/module.h>
#include <linux/parport.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <asm/uaccess.h>

static struct {
	char *token;
	char *descr;
} classes[] = {
	{ "",            "Legacy device" },
	{ "PRINTER",     "Printer" },
	{ "MODEM",       "Modem" },
	{ "NET",         "Network device" },
	{ "HDC",       	 "Hard disk" },
	{ "PCMCIA",      "PCMCIA" },
	{ "MEDIA",       "Multimedia device" },
	{ "FDC",         "Floppy disk" },
	{ "PORTS",       "Ports" },
	{ "SCANNER",     "Scanner" },
	{ "DIGICAM",     "Digital camera" },
	{ "",            "Unknown device" },
	{ "",            "Unspecified" },
	{ "SCSIADAPTER", "SCSI adapter" },
	{ NULL,          NULL }
};

static void pretty_print(struct parport *port, int device)
{
	struct parport_device_info *info = &port->probe_info[device + 1];

	printk(KERN_INFO "%s", port->name);

	if (device >= 0)
		printk (" (addr %d)", device);

	printk (": %s", classes[info->class].descr);
	if (info->class)
		printk(", %s %s", info->mfr, info->model);

	printk("\n");
}

static void parse_data(struct parport *port, int device, char *str)
{
	char *txt = kmalloc(strlen(str)+1, GFP_KERNEL);
	char *p = txt, *q;
	int guessed_class = PARPORT_CLASS_UNSPEC;
	struct parport_device_info *info = &port->probe_info[device + 1];

	if (!txt) {
		printk(KERN_WARNING "%s probe: memory squeeze\n", port->name);
		return;
	}
	strcpy(txt, str);
	while (p) {
		char *sep;
		q = strchr(p, ';');
		if (q) *q = 0;
		sep = strchr(p, ':');
		if (sep) {
			char *u;
			*(sep++) = 0;
			/* Get rid of trailing blanks */
			u = sep + strlen (sep) - 1;
			while (u >= p && *u == ' ')
				*u-- = '\0';
			u = p;
			while (*u) {
				*u = toupper(*u);
				u++;
			}
			if (!strcmp(p, "MFG") || !strcmp(p, "MANUFACTURER")) {
				kfree(info->mfr);
				info->mfr = kstrdup(sep, GFP_KERNEL);
			} else if (!strcmp(p, "MDL") || !strcmp(p, "MODEL")) {
				kfree(info->model);
				info->model = kstrdup(sep, GFP_KERNEL);
			} else if (!strcmp(p, "CLS") || !strcmp(p, "CLASS")) {
				int i;

				kfree(info->class_name);
				info->class_name = kstrdup(sep, GFP_KERNEL);
				for (u = sep; *u; u++)
					*u = toupper(*u);
				for (i = 0; classes[i].token; i++) {
					if (!strcmp(classes[i].token, sep)) {
						info->class = i;
						goto rock_on;
					}
				}
				printk(KERN_WARNING "%s probe: warning, class '%s' not understood.\n", port->name, sep);
				info->class = PARPORT_CLASS_OTHER;
			} else if (!strcmp(p, "CMD") ||
				   !strcmp(p, "COMMAND SET")) {
				kfree(info->cmdset);
				info->cmdset = kstrdup(sep, GFP_KERNEL);
				/* if it speaks printer language, it's
				   probably a printer */
				if (strstr(sep, "PJL") || strstr(sep, "PCL"))
					guessed_class = PARPORT_CLASS_PRINTER;
			} else if (!strcmp(p, "DES") || !strcmp(p, "DESCRIPTION")) {
				kfree(info->description);
				info->description = kstrdup(sep, GFP_KERNEL);
			}
		}
	rock_on:
		if (q)
			p = q + 1;
		else
			p = NULL;
	}

	/* If the device didn't tell us its class, maybe we have managed to
	   guess one from the things it did say. */
	if (info->class == PARPORT_CLASS_UNSPEC)
		info->class = guessed_class;

	pretty_print (port, device);

	kfree(txt);
}

/* Get Std 1284 Device ID. */
ssize_t parport_device_id (int devnum, char *buffer, size_t len)
{
	ssize_t retval = -ENXIO;
	struct pardevice *dev = parport_open (devnum, "Device ID probe",
					      NULL, NULL, NULL, 0, NULL);
	if (!dev)
		return -ENXIO;

	parport_claim_or_block (dev);

	/* Negotiate to compatibility mode, and then to device ID mode.
	 * (This is in case we are already in device ID mode.) */
	parport_negotiate (dev->port, IEEE1284_MODE_COMPAT);
	retval = parport_negotiate (dev->port,
				    IEEE1284_MODE_NIBBLE | IEEE1284_DEVICEID);

	if (!retval) {
		int idlen;
		unsigned char length[2];

		/* First two bytes are MSB,LSB of inclusive length. */
		retval = parport_read (dev->port, length, 2);

		if (retval != 2) goto end_id;

		idlen = (length[0] << 8) + length[1] - 2;
		/*
		 * Check if the caller-allocated buffer is large enough
		 * otherwise bail out or there will be an at least off by one.
		 */
		if (idlen + 1 < len)
			len = idlen;
		else {
			retval = -EINVAL;
			goto out;
		}
		retval = parport_read (dev->port, buffer, len);

		if (retval != len)
			printk (KERN_DEBUG "%s: only read %Zd of %Zd ID bytes\n",
				dev->port->name, retval,
				len);

		/* Some printer manufacturers mistakenly believe that
                   the length field is supposed to be _exclusive_.
		   In addition, there are broken devices out there
                   that don't even finish off with a semi-colon. */
		if (buffer[len - 1] != ';') {
			ssize_t diff;
			diff = parport_read (dev->port, buffer + len, 2);
			retval += diff;

			if (diff)
				printk (KERN_DEBUG
					"%s: device reported incorrect "
					"length field (%d, should be %Zd)\n",
					dev->port->name, idlen, retval);
			else {
				/* One semi-colon short of a device ID. */
				buffer[len++] = ';';
				printk (KERN_DEBUG "%s: faking semi-colon\n",
					dev->port->name);

				/* If we get here, I don't think we
                                   need to worry about the possible
                                   standard violation of having read
                                   more than we were told to.  The
                                   device is non-compliant anyhow. */
			}
		}

	end_id:
		buffer[len] = '\0';
		parport_negotiate (dev->port, IEEE1284_MODE_COMPAT);
	}

	if (retval > 2)
		parse_data (dev->port, dev->daisy, buffer);

out:
	parport_release (dev);
	parport_close (dev);
	return retval;
}
