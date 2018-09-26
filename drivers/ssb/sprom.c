/*
 * Sonics Silicon Backplane
 * Common SPROM support routines
 *
 * Copyright (C) 2005-2008 Michael Buesch <m@bues.ch>
 * Copyright (C) 2005 Martin Langer <martin-langer@gmx.de>
 * Copyright (C) 2005 Stefano Brivio <st3@riseup.net>
 * Copyright (C) 2005 Danny van Dyk <kugelfang@gentoo.org>
 * Copyright (C) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "ssb_private.h"

#include <linux/ctype.h>
#include <linux/slab.h>


static int(*get_fallback_sprom)(struct ssb_bus *dev, struct ssb_sprom *out);


static int sprom2hex(const u16 *sprom, char *buf, size_t buf_len,
		     size_t sprom_size_words)
{
	int i, pos = 0;

	for (i = 0; i < sprom_size_words; i++)
		pos += snprintf(buf + pos, buf_len - pos - 1,
				"%04X", swab16(sprom[i]) & 0xFFFF);
	pos += snprintf(buf + pos, buf_len - pos - 1, "\n");

	return pos + 1;
}

static int hex2sprom(u16 *sprom, const char *dump, size_t len,
		     size_t sprom_size_words)
{
	char c, tmp[5] = { 0 };
	int err, cnt = 0;
	unsigned long parsed;

	/* Strip whitespace at the end. */
	while (len) {
		c = dump[len - 1];
		if (!isspace(c) && c != '\0')
			break;
		len--;
	}
	/* Length must match exactly. */
	if (len != sprom_size_words * 4)
		return -EINVAL;

	while (cnt < sprom_size_words) {
		memcpy(tmp, dump, 4);
		dump += 4;
		err = kstrtoul(tmp, 16, &parsed);
		if (err)
			return err;
		sprom[cnt++] = swab16((u16)parsed);
	}

	return 0;
}

/* Common sprom device-attribute show-handler */
ssize_t ssb_attr_sprom_show(struct ssb_bus *bus, char *buf,
			    int (*sprom_read)(struct ssb_bus *bus, u16 *sprom))
{
	u16 *sprom;
	int err = -ENOMEM;
	ssize_t count = 0;
	size_t sprom_size_words = bus->sprom_size;

	sprom = kcalloc(sprom_size_words, sizeof(u16), GFP_KERNEL);
	if (!sprom)
		goto out;

	/* Use interruptible locking, as the SPROM write might
	 * be holding the lock for several seconds. So allow userspace
	 * to cancel operation. */
	err = -ERESTARTSYS;
	if (mutex_lock_interruptible(&bus->sprom_mutex))
		goto out_kfree;
	err = sprom_read(bus, sprom);
	mutex_unlock(&bus->sprom_mutex);

	if (!err)
		count = sprom2hex(sprom, buf, PAGE_SIZE, sprom_size_words);

out_kfree:
	kfree(sprom);
out:
	return err ? err : count;
}

/* Common sprom device-attribute store-handler */
ssize_t ssb_attr_sprom_store(struct ssb_bus *bus,
			     const char *buf, size_t count,
			     int (*sprom_check_crc)(const u16 *sprom, size_t size),
			     int (*sprom_write)(struct ssb_bus *bus, const u16 *sprom))
{
	u16 *sprom;
	int res = 0, err = -ENOMEM;
	size_t sprom_size_words = bus->sprom_size;
	struct ssb_freeze_context freeze;

	sprom = kcalloc(bus->sprom_size, sizeof(u16), GFP_KERNEL);
	if (!sprom)
		goto out;
	err = hex2sprom(sprom, buf, count, sprom_size_words);
	if (err) {
		err = -EINVAL;
		goto out_kfree;
	}
	err = sprom_check_crc(sprom, sprom_size_words);
	if (err) {
		err = -EINVAL;
		goto out_kfree;
	}

	/* Use interruptible locking, as the SPROM write might
	 * be holding the lock for several seconds. So allow userspace
	 * to cancel operation. */
	err = -ERESTARTSYS;
	if (mutex_lock_interruptible(&bus->sprom_mutex))
		goto out_kfree;
	err = ssb_devices_freeze(bus, &freeze);
	if (err) {
		pr_err("SPROM write: Could not freeze all devices\n");
		goto out_unlock;
	}
	res = sprom_write(bus, sprom);
	err = ssb_devices_thaw(&freeze);
	if (err)
		pr_err("SPROM write: Could not thaw all devices\n");
out_unlock:
	mutex_unlock(&bus->sprom_mutex);
out_kfree:
	kfree(sprom);
out:
	if (res)
		return res;
	return err ? err : count;
}

/**
 * ssb_arch_register_fallback_sprom - Registers a method providing a
 * fallback SPROM if no SPROM is found.
 *
 * @sprom_callback: The callback function.
 *
 * With this function the architecture implementation may register a
 * callback handler which fills the SPROM data structure. The fallback is
 * only used for PCI based SSB devices, where no valid SPROM can be found
 * in the shadow registers.
 *
 * This function is useful for weird architectures that have a half-assed
 * SSB device hardwired to their PCI bus.
 *
 * Note that it does only work with PCI attached SSB devices. PCMCIA
 * devices currently don't use this fallback.
 * Architectures must provide the SPROM for native SSB devices anyway, so
 * the fallback also isn't used for native devices.
 *
 * This function is available for architecture code, only. So it is not
 * exported.
 */
int ssb_arch_register_fallback_sprom(int (*sprom_callback)(struct ssb_bus *bus,
				     struct ssb_sprom *out))
{
	if (get_fallback_sprom)
		return -EEXIST;
	get_fallback_sprom = sprom_callback;

	return 0;
}

int ssb_fill_sprom_with_fallback(struct ssb_bus *bus, struct ssb_sprom *out)
{
	if (!get_fallback_sprom)
		return -ENOENT;

	return get_fallback_sprom(bus, out);
}

/* http://bcm-v4.sipsolutions.net/802.11/IsSpromAvailable */
bool ssb_is_sprom_available(struct ssb_bus *bus)
{
	/* status register only exists on chipcomon rev >= 11 and we need check
	   for >= 31 only */
	/* this routine differs from specs as we do not access SPROM directly
	   on PCMCIA */
	if (bus->bustype == SSB_BUSTYPE_PCI &&
	    bus->chipco.dev &&	/* can be unavailable! */
	    bus->chipco.dev->id.revision >= 31)
		return bus->chipco.capabilities & SSB_CHIPCO_CAP_SPROM;

	return true;
}
