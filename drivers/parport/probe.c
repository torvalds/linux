// SPDX-License-Identifier: GPL-2.0
/*
 * Parallel port device probing code
 *
 * Authors:    Carsten Gross, carsten@sol.wohnheim.uni-ulm.de
 *             Philip Blundell <philb@gnu.org>
 */

#include <linux/module.h>
#include <linux/parport.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static const struct {
	const char *token;
	const char *descr;
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

/* Read up to count-1 bytes of device id. Terminate buffer with
 * '\0'. Buffer begins with two Device ID length bytes as given by
 * device. */
static ssize_t parport_read_device_id (struct parport *port, char *buffer,
				       size_t count)
{
	unsigned char length[2];
	unsigned lelen, belen;
	size_t idlens[4];
	unsigned numidlens;
	unsigned current_idlen;
	ssize_t retval;
	size_t len;

	/* First two bytes are MSB,LSB of inclusive length. */
	retval = parport_read (port, length, 2);

	if (retval < 0)
		return retval;
	if (retval != 2)
		return -EIO;

	if (count < 2)
		return 0;
	memcpy(buffer, length, 2);
	len = 2;

	/* Some devices wrongly send LE length, and some send it two
	 * bytes short. Construct a sorted array of lengths to try. */
	belen = (length[0] << 8) + length[1];
	lelen = (length[1] << 8) + length[0];
	idlens[0] = min(belen, lelen);
	idlens[1] = idlens[0]+2;
	if (belen != lelen) {
		int off = 2;
		/* Don't try lengths of 0x100 and 0x200 as 1 and 2 */
		if (idlens[0] <= 2)
			off = 0;
		idlens[off] = max(belen, lelen);
		idlens[off+1] = idlens[off]+2;
		numidlens = off+2;
	}
	else {
		/* Some devices don't truly implement Device ID, but
		 * just return constant nibble forever. This catches
		 * also those cases. */
		if (idlens[0] == 0 || idlens[0] > 0xFFF) {
			printk (KERN_DEBUG "%s: reported broken Device ID"
				" length of %#zX bytes\n",
				port->name, idlens[0]);
			return -EIO;
		}
		numidlens = 2;
	}

	/* Try to respect the given ID length despite all the bugs in
	 * the ID length. Read according to shortest possible ID
	 * first. */
	for (current_idlen = 0; current_idlen < numidlens; ++current_idlen) {
		size_t idlen = idlens[current_idlen];
		if (idlen+1 >= count)
			break;

		retval = parport_read (port, buffer+len, idlen-len);

		if (retval < 0)
			return retval;
		len += retval;

		if (port->physport->ieee1284.phase != IEEE1284_PH_HBUSY_DAVAIL) {
			if (belen != len) {
				printk (KERN_DEBUG "%s: Device ID was %zd bytes"
					" while device told it would be %d"
					" bytes\n",
					port->name, len, belen);
			}
			goto done;
		}

		/* This might end reading the Device ID too
		 * soon. Hopefully the needed fields were already in
		 * the first 256 bytes or so that we must have read so
		 * far. */
		if (buffer[len-1] == ';') {
 			printk (KERN_DEBUG "%s: Device ID reading stopped"
				" before device told data not available. "
				"Current idlen %u of %u, len bytes %02X %02X\n",
				port->name, current_idlen, numidlens,
				length[0], length[1]);
			goto done;
		}
	}
	if (current_idlen < numidlens) {
		/* Buffer not large enough, read to end of buffer. */
		size_t idlen, len2;
		if (len+1 < count) {
			retval = parport_read (port, buffer+len, count-len-1);
			if (retval < 0)
				return retval;
			len += retval;
		}
		/* Read the whole ID since some devices would not
		 * otherwise give back the Device ID from beginning
		 * next time when asked. */
		idlen = idlens[current_idlen];
		len2 = len;
		while(len2 < idlen && retval > 0) {
			char tmp[4];
			retval = parport_read (port, tmp,
					       min(sizeof tmp, idlen-len2));
			if (retval < 0)
				return retval;
			len2 += retval;
		}
	}
	/* In addition, there are broken devices out there that don't
	   even finish off with a semi-colon. We do not need to care
	   about those at this time. */
 done:
	buffer[len] = '\0';
	return len;
}

/* Get Std 1284 Device ID. */
ssize_t parport_device_id (int devnum, char *buffer, size_t count)
{
	ssize_t retval = -ENXIO;
	struct pardevice *dev = parport_open(devnum, daisy_dev_name);
	if (!dev)
		return -ENXIO;

	parport_claim_or_block (dev);

	/* Negotiate to compatibility mode, and then to device ID
	 * mode. (This so that we start form beginning of device ID if
	 * already in device ID mode.) */
	parport_negotiate (dev->port, IEEE1284_MODE_COMPAT);
	retval = parport_negotiate (dev->port,
				    IEEE1284_MODE_NIBBLE | IEEE1284_DEVICEID);

	if (!retval) {
		retval = parport_read_device_id (dev->port, buffer, count);
		parport_negotiate (dev->port, IEEE1284_MODE_COMPAT);
		if (retval > 2)
			parse_data (dev->port, dev->daisy, buffer+2);
	}

	parport_release (dev);
	parport_close (dev);
	return retval;
}
