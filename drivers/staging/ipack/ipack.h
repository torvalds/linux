/*
 * Industry-pack bus.
 *
 * (C) 2011 Samuel Iglesias Gonsalvez <siglesia@cern.ch>, CERN
 * (C) 2012 Samuel Iglesias Gonsalvez <siglesias@igalia.com>, Igalia
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#include <linux/mod_devicetable.h>
#include <linux/device.h>

#define IPACK_IDPROM_OFFSET_I			0x01
#define IPACK_IDPROM_OFFSET_P			0x03
#define IPACK_IDPROM_OFFSET_A			0x05
#define IPACK_IDPROM_OFFSET_C			0x07
#define IPACK_IDPROM_OFFSET_MANUFACTURER_ID	0x09
#define IPACK_IDPROM_OFFSET_MODEL		0x0B
#define IPACK_IDPROM_OFFSET_REVISION		0x0D
#define IPACK_IDPROM_OFFSET_RESERVED		0x0F
#define IPACK_IDPROM_OFFSET_DRIVER_ID_L		0x11
#define IPACK_IDPROM_OFFSET_DRIVER_ID_H		0x13
#define IPACK_IDPROM_OFFSET_NUM_BYTES		0x15
#define IPACK_IDPROM_OFFSET_CRC			0x17

struct ipack_bus_ops;
struct ipack_driver;

enum ipack_space {
	IPACK_IO_SPACE    = 0,
	IPACK_ID_SPACE    = 1,
	IPACK_MEM_SPACE   = 2,
};

/**
 *	struct ipack_addr_space - Virtual address space mapped for a specified type.
 *
 *	@address: virtual address
 *	@size: size of the mapped space
 */
struct ipack_addr_space {
	void __iomem *address;
	unsigned int size;
};

/**
 *	struct ipack_device
 *
 *	@bus_nr: IP bus number where the device is plugged
 *	@slot: Slot where the device is plugged in the carrier board
 *	@irq: IRQ vector
 *	@driver: Pointer to the ipack_driver that manages the device
 *	@bus: ipack_bus_device where the device is plugged to.
 *	@id_space: Virtual address to ID space.
 *	@io_space: Virtual address to IO space.
 *	@mem_space: Virtual address to MEM space.
 *	@dev: device in kernel representation.
 *
 * Warning: Direct access to mapped memory is possible but the endianness
 * is not the same with PCI carrier or VME carrier. The endianness is managed
 * by the carrier board throught bus->ops.
 */
struct ipack_device {
	unsigned int bus_nr;
	unsigned int slot;
	unsigned int irq;
	struct ipack_driver *driver;
	struct ipack_bus_device *bus;
	struct ipack_addr_space id_space;
	struct ipack_addr_space io_space;
	struct ipack_addr_space mem_space;
	struct device dev;
};

/**
 *	struct ipack_driver_ops -- callbacks to mezzanine driver for installing/removing one device
 *
 *	@match: Match function
 *	@probe: Probe function
 *	@remove: tell the driver that the carrier board wants to remove one device
 */

struct ipack_driver_ops {
	int (*match) (struct ipack_device *dev);
	int (*probe) (struct ipack_device *dev);
	void (*remove) (struct ipack_device *dev);
};

/**
 *	struct ipack_driver -- Specific data to each ipack board driver
 *
 *	@driver: Device driver kernel representation
 *	@ops: Mezzanine driver operations specific for the ipack bus.
 */
struct ipack_driver {
	struct device_driver driver;
	const struct ipack_device_id *id_table;
	struct ipack_driver_ops *ops;
};

/**
 *	struct ipack_bus_ops - available operations on a bridge module
 *
 *	@map_space: map IP address space
 *	@unmap_space: unmap IP address space
 *	@request_irq: request IRQ
 *	@free_irq: free IRQ
 *	@remove_device: tell the bridge module that the device has been removed
 */
struct ipack_bus_ops {
	int (*map_space) (struct ipack_device *dev, unsigned int memory_size, int space);
	int (*unmap_space) (struct ipack_device *dev, int space);
	int (*request_irq) (struct ipack_device *dev, int vector, int (*handler)(void *), void *arg);
	int (*free_irq) (struct ipack_device *dev);
	int (*remove_device) (struct ipack_device *dev);
};

/**
 *	struct ipack_bus_device
 *
 *	@dev: pointer to carrier device
 *	@slots: number of slots available
 *	@bus_nr: ipack bus number
 *	@ops: bus operations for the mezzanine drivers
 */
struct ipack_bus_device {
	struct device *parent;
	int slots;
	int bus_nr;
	struct ipack_bus_ops *ops;
};

/**
 *	ipack_bus_register -- register a new ipack bus
 *
 * @parent: pointer to the parent device, if any.
 * @slots: number of slots available in the bus device.
 * @ops: bus operations for the mezzanine drivers.
 *
 * The carrier board device should call this function to register itself as
 * available bus device in ipack.
 */
struct ipack_bus_device *ipack_bus_register(struct device *parent, int slots,
					    struct ipack_bus_ops *ops);

/**
 *	ipack_bus_unregister -- unregister an ipack bus
 */
int ipack_bus_unregister(struct ipack_bus_device *bus);

/**
 *	ipack_driver_register -- Register a new driver
 *
 * Called by a ipack driver to register itself as a driver
 * that can manage ipack devices.
 */
int ipack_driver_register(struct ipack_driver *edrv, struct module *owner, char *name);
void ipack_driver_unregister(struct ipack_driver *edrv);

/**
 *	ipack_device_register -- register a new mezzanine device
 *
 * @bus: ipack bus device it is plugged to.
 * @slot: slot position in the bus device.
 * @irqv: IRQ vector for the mezzanine.
 *
 * Register a new ipack device (mezzanine device). The call is done by
 * the carrier device driver.
 */
struct ipack_device *ipack_device_register(struct ipack_bus_device *bus, int slot, int irqv);
void ipack_device_unregister(struct ipack_device *dev);

/**
 * DEFINE_IPACK_DEVICE_TABLE - macro used to describe a IndustryPack table
 * @_table: device table name
 *
 * This macro is used to create a struct ipack_device_id array (a device table)
 * in a generic manner.
 */
#define DEFINE_IPACK_DEVICE_TABLE(_table) \
	const struct ipack_device_id _table[] __devinitconst

/**
 * IPACK_DEVICE - macro used to describe a specific IndustryPack device
 * @_format: the format version (currently either 1 or 2, 8 bit value)
 * @vend:    the 8 or 24 bit IndustryPack Vendor ID
 * @dev:     the 8 or 16  bit IndustryPack Device ID
 *
 * This macro is used to create a struct ipack_device_id that matches a specific
 * device.
 */
#define IPACK_DEVICE(_format, vend, dev) \
	 .format = (_format), \
	 .vendor = (vend), \
	 .device = (dev)
