/* -*- mode: c; c-basic-offset: 8 -*- */

/*
 * MCA device support functions
 *
 * These functions support the ongoing device access API.
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
#include <linux/mca.h>
#include <linux/string.h>

/**
 *	mca_device_read_stored_pos - read POS register from stored data
 *	@mca_dev: device to read from
 *	@reg:  register to read from
 *
 *	Fetch a POS value that was stored at boot time by the kernel
 *	when it scanned the MCA space. The register value is returned.
 *	Missing or invalid registers report 0.
 */
unsigned char mca_device_read_stored_pos(struct mca_device *mca_dev, int reg)
{
	if(reg < 0 || reg >= 8)
		return 0;

	return mca_dev->pos[reg];
}
EXPORT_SYMBOL(mca_device_read_stored_pos);

/**
 *	mca_device_read_pos - read POS register from card
 *	@mca_dev: device to read from
 *	@reg:  register to read from
 *
 *	Fetch a POS value directly from the hardware to obtain the
 *	current value. This is much slower than
 *	mca_device_read_stored_pos and may not be invoked from
 *	interrupt context. It handles the deep magic required for
 *	onboard devices transparently.
 */
unsigned char mca_device_read_pos(struct mca_device *mca_dev, int reg)
{
	struct mca_bus *mca_bus = to_mca_bus(mca_dev->dev.parent);

	return mca_bus->f.mca_read_pos(mca_dev, reg);

	return 	mca_dev->pos[reg];
}
EXPORT_SYMBOL(mca_device_read_pos);


/**
 *	mca_device_write_pos - read POS register from card
 *	@mca_dev: device to write pos register to
 *	@reg:  register to write to
 *	@byte: byte to write to the POS registers
 *
 *	Store a POS value directly to the hardware. You should not
 *	normally need to use this function and should have a very good
 *	knowledge of MCA bus before you do so. Doing this wrongly can
 *	damage the hardware.
 *
 *	This function may not be used from interrupt context.
 *
 */
void mca_device_write_pos(struct mca_device *mca_dev, int reg,
			  unsigned char byte)
{
	struct mca_bus *mca_bus = to_mca_bus(mca_dev->dev.parent);

	mca_bus->f.mca_write_pos(mca_dev, reg, byte);
}
EXPORT_SYMBOL(mca_device_write_pos);

/**
 *	mca_device_transform_irq - transform the ADF obtained IRQ
 *	@mca_device: device whose irq needs transforming
 *	@irq: input irq from ADF
 *
 *	MCA Adapter Definition Files (ADF) contain irq, ioport, memory
 *	etc. definitions.  In systems with more than one bus, these need
 *	to be transformed through bus mapping functions to get the real
 *	system global quantities.
 *
 *	This function transforms the interrupt number and returns the
 *	transformed system global interrupt
 */
int mca_device_transform_irq(struct mca_device *mca_dev, int irq)
{
	struct mca_bus *mca_bus = to_mca_bus(mca_dev->dev.parent);

	return mca_bus->f.mca_transform_irq(mca_dev, irq);
}
EXPORT_SYMBOL(mca_device_transform_irq);

/**
 *	mca_device_transform_ioport - transform the ADF obtained I/O port
 *	@mca_device: device whose port needs transforming
 *	@ioport: input I/O port from ADF
 *
 *	MCA Adapter Definition Files (ADF) contain irq, ioport, memory
 *	etc. definitions.  In systems with more than one bus, these need
 *	to be transformed through bus mapping functions to get the real
 *	system global quantities.
 *
 *	This function transforms the I/O port number and returns the
 *	transformed system global port number.
 *
 *	This transformation can be assumed to be linear for port ranges.
 */
int mca_device_transform_ioport(struct mca_device *mca_dev, int port)
{
	struct mca_bus *mca_bus = to_mca_bus(mca_dev->dev.parent);

	return mca_bus->f.mca_transform_ioport(mca_dev, port);
}
EXPORT_SYMBOL(mca_device_transform_ioport);

/**
 *	mca_device_transform_memory - transform the ADF obtained memory
 *	@mca_device: device whose memory region needs transforming
 *	@mem: memory region start from ADF
 *
 *	MCA Adapter Definition Files (ADF) contain irq, ioport, memory
 *	etc. definitions.  In systems with more than one bus, these need
 *	to be transformed through bus mapping functions to get the real
 *	system global quantities.
 *
 *	This function transforms the memory region start and returns the
 *	transformed system global memory region (physical).
 *
 *	This transformation can be assumed to be linear for region ranges.
 */
void *mca_device_transform_memory(struct mca_device *mca_dev, void *mem)
{
	struct mca_bus *mca_bus = to_mca_bus(mca_dev->dev.parent);

	return mca_bus->f.mca_transform_memory(mca_dev, mem);
}
EXPORT_SYMBOL(mca_device_transform_memory);


/**
 *	mca_device_claimed - check if claimed by driver
 *	@mca_dev:	device to check
 *
 *	Returns 1 if the slot has been claimed by a driver
 */

int mca_device_claimed(struct mca_device *mca_dev)
{
	return mca_dev->driver_loaded;
}
EXPORT_SYMBOL(mca_device_claimed);

/**
 *	mca_device_set_claim - set the claim value of the driver
 *	@mca_dev:	device to set value for
 *	@val:		claim value to set (1 claimed, 0 unclaimed)
 */
void mca_device_set_claim(struct mca_device *mca_dev, int val)
{
	mca_dev->driver_loaded = val;
}
EXPORT_SYMBOL(mca_device_set_claim);

/**
 *	mca_device_status - get the status of the device
 *	@mca_device:	device to get
 *
 *	returns an enumeration of the device status:
 *
 *	MCA_ADAPTER_NORMAL	adapter is OK.
 *	MCA_ADAPTER_NONE	no adapter at device (should never happen).
 *	MCA_ADAPTER_DISABLED	adapter is disabled.
 *	MCA_ADAPTER_ERROR	adapter cannot be initialised.
 */
enum MCA_AdapterStatus mca_device_status(struct mca_device *mca_dev)
{
	return mca_dev->status;
}
EXPORT_SYMBOL(mca_device_status);

/**
 *	mca_device_set_name - set the name of the device
 *	@mca_device:	device to set the name of
 *	@name:		name to set
 */
void mca_device_set_name(struct mca_device *mca_dev, const char *name)
{
	if(!mca_dev)
		return;

	strlcpy(mca_dev->name, name, sizeof(mca_dev->name));
}
EXPORT_SYMBOL(mca_device_set_name);
