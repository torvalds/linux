/* -*- mode: c; c-basic-offset: 8 -*- */

/*
 * MCA bus support functions for legacy (2.4) API.
 *
 * Legacy API means the API that operates in terms of MCA slot number
 *
 * (C) 2002 James Bottomley <James.Bottomley@HansenPartnership.com>
 *
**-----------------------------------------------------------------------------
**  
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/mca-legacy.h>
#include <asm/io.h>

/* NOTE: This structure is stack allocated */
struct mca_find_adapter_info {
	int			id;
	int			slot;
	struct mca_device	*mca_dev;
};

/* The purpose of this iterator is to loop over all the devices and
 * find the one with the smallest slot number that's just greater than
 * or equal to the required slot with a matching id */
static int mca_find_adapter_callback(struct device *dev, void *data)
{
	struct mca_find_adapter_info *info = data;
	struct mca_device *mca_dev = to_mca_device(dev);

	if(mca_dev->pos_id != info->id)
		return 0;

	if(mca_dev->slot < info->slot)
		return 0;

	if(!info->mca_dev || info->mca_dev->slot >= mca_dev->slot)
		info->mca_dev = mca_dev;

	return 0;
}

/**
 *	mca_find_adapter - scan for adapters
 *	@id:	MCA identification to search for
 *	@start:	starting slot
 *
 *	Search the MCA configuration for adapters matching the 16bit
 *	ID given. The first time it should be called with start as zero
 *	and then further calls made passing the return value of the
 *	previous call until %MCA_NOTFOUND is returned.
 *
 *	Disabled adapters are not reported.
 */

int mca_find_adapter(int id, int start)
{
	struct mca_find_adapter_info info;

	if(id == 0xffff)
		return MCA_NOTFOUND;

	info.slot = start;
	info.id = id;
	info.mca_dev = NULL;

	for(;;) {
		bus_for_each_dev(&mca_bus_type, NULL, &info, mca_find_adapter_callback);

		if(info.mca_dev == NULL)
			return MCA_NOTFOUND;

		if(info.mca_dev->status != MCA_ADAPTER_DISABLED)
			break;

		/* OK, found adapter but it was disabled.  Go around
		 * again, excluding the slot we just found */

		info.slot = info.mca_dev->slot + 1;
		info.mca_dev = NULL;
	}
		
	return info.mca_dev->slot;
}
EXPORT_SYMBOL(mca_find_adapter);

/*--------------------------------------------------------------------*/

/**
 *	mca_find_unused_adapter - scan for unused adapters
 *	@id:	MCA identification to search for
 *	@start:	starting slot
 *
 *	Search the MCA configuration for adapters matching the 16bit
 *	ID given. The first time it should be called with start as zero
 *	and then further calls made passing the return value of the
 *	previous call until %MCA_NOTFOUND is returned.
 *
 *	Adapters that have been claimed by drivers and those that
 *	are disabled are not reported. This function thus allows a driver
 *	to scan for further cards when some may already be driven.
 */

int mca_find_unused_adapter(int id, int start)
{
	struct mca_find_adapter_info info = { 0 };

	if (!MCA_bus || id == 0xffff)
		return MCA_NOTFOUND;

	info.slot = start;
	info.id = id;
	info.mca_dev = NULL;

	for(;;) {
		bus_for_each_dev(&mca_bus_type, NULL, &info, mca_find_adapter_callback);

		if(info.mca_dev == NULL)
			return MCA_NOTFOUND;

		if(info.mca_dev->status != MCA_ADAPTER_DISABLED
		   && !info.mca_dev->driver_loaded)
			break;

		/* OK, found adapter but it was disabled or already in
		 * use.  Go around again, excluding the slot we just
		 * found */

		info.slot = info.mca_dev->slot + 1;
		info.mca_dev = NULL;
	}
		
	return info.mca_dev->slot;
}
EXPORT_SYMBOL(mca_find_unused_adapter);

/* NOTE: stack allocated structure */
struct mca_find_device_by_slot_info {
	int			slot;
	struct mca_device 	*mca_dev;
};

static int mca_find_device_by_slot_callback(struct device *dev, void *data)
{
	struct mca_find_device_by_slot_info *info = data;
	struct mca_device *mca_dev = to_mca_device(dev);

	if(mca_dev->slot == info->slot)
		info->mca_dev = mca_dev;

	return 0;
}

struct mca_device *mca_find_device_by_slot(int slot)
{
	struct mca_find_device_by_slot_info info;

	info.slot = slot;
	info.mca_dev = NULL;

	bus_for_each_dev(&mca_bus_type, NULL, &info, mca_find_device_by_slot_callback);

	return info.mca_dev;
}

/**
 *	mca_read_stored_pos - read POS register from boot data
 *	@slot: slot number to read from
 *	@reg:  register to read from
 *
 *	Fetch a POS value that was stored at boot time by the kernel
 *	when it scanned the MCA space. The register value is returned.
 *	Missing or invalid registers report 0.
 */
unsigned char mca_read_stored_pos(int slot, int reg)
{
	struct mca_device *mca_dev = mca_find_device_by_slot(slot);

	if(!mca_dev)
		return 0;

	return mca_device_read_stored_pos(mca_dev, reg);
}
EXPORT_SYMBOL(mca_read_stored_pos);


/**
 *	mca_read_pos - read POS register from card
 *	@slot: slot number to read from
 *	@reg:  register to read from
 *
 *	Fetch a POS value directly from the hardware to obtain the
 *	current value. This is much slower than mca_read_stored_pos and
 *	may not be invoked from interrupt context. It handles the
 *	deep magic required for onboard devices transparently.
 */

unsigned char mca_read_pos(int slot, int reg)
{
	struct mca_device *mca_dev = mca_find_device_by_slot(slot);

	if(!mca_dev)
		return 0;

	return mca_device_read_pos(mca_dev, reg);
}
EXPORT_SYMBOL(mca_read_pos);

		
/**
 *	mca_write_pos - read POS register from card
 *	@slot: slot number to read from
 *	@reg:  register to read from
 *	@byte: byte to write to the POS registers
 *
 *	Store a POS value directly from the hardware. You should not
 *	normally need to use this function and should have a very good
 *	knowledge of MCA bus before you do so. Doing this wrongly can
 *	damage the hardware.
 *
 *	This function may not be used from interrupt context.
 *
 *	Note that this a technically a Bad Thing, as IBM tech stuff says
 *	you should only set POS values through their utilities.
 *	However, some devices such as the 3c523 recommend that you write
 *	back some data to make sure the configuration is consistent.
 *	I'd say that IBM is right, but I like my drivers to work.
 *
 *	This function can't do checks to see if multiple devices end up
 *	with the same resources, so you might see magic smoke if someone
 *	screws up.
 */

void mca_write_pos(int slot, int reg, unsigned char byte)
{
	struct mca_device *mca_dev = mca_find_device_by_slot(slot);

	if(!mca_dev)
		return;

	mca_device_write_pos(mca_dev, reg, byte);
}
EXPORT_SYMBOL(mca_write_pos);

/**
 *	mca_set_adapter_name - Set the description of the card
 *	@slot: slot to name
 *	@name: text string for the namen
 *
 *	This function sets the name reported via /proc for this
 *	adapter slot. This is for user information only. Setting a
 *	name deletes any previous name.
 */

void mca_set_adapter_name(int slot, char* name)
{
	struct mca_device *mca_dev = mca_find_device_by_slot(slot);

	if(!mca_dev)
		return;

	mca_device_set_name(mca_dev, name);
}
EXPORT_SYMBOL(mca_set_adapter_name);

/**
 *	mca_mark_as_used - claim an MCA device
 *	@slot:	slot to claim
 *	FIXME:  should we make this threadsafe
 *
 *	Claim an MCA slot for a device driver. If the
 *	slot is already taken the function returns 1,
 *	if it is not taken it is claimed and 0 is
 *	returned.
 */

int mca_mark_as_used(int slot)
{
	struct mca_device *mca_dev = mca_find_device_by_slot(slot);

	if(!mca_dev)
		/* FIXME: this is actually a severe error */
		return 1;

	if(mca_device_claimed(mca_dev))
		return 1;

	mca_device_set_claim(mca_dev, 1);

	return 0;
}
EXPORT_SYMBOL(mca_mark_as_used);

/**
 *	mca_mark_as_unused - release an MCA device
 *	@slot:	slot to claim
 *
 *	Release the slot for other drives to use.
 */

void mca_mark_as_unused(int slot)
{
	struct mca_device *mca_dev = mca_find_device_by_slot(slot);

	if(!mca_dev)
		return;

	mca_device_set_claim(mca_dev, 0);
}
EXPORT_SYMBOL(mca_mark_as_unused);

