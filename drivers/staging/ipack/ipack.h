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

#include <linux/device.h>

#define IPACK_BOARD_NAME_SIZE			16
#define IPACK_IRQ_NAME_SIZE			50
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
	void         *address;
	unsigned int size;
};

/**
 *	struct ipack_device
 *
 *	@board_name: IP mezzanine board name
 *	@bus_name: IP carrier board name
 *	@bus_nr: IP bus number where the device is plugged
 *	@slot: Slot where the device is plugged in the carrier board
 *	@irq: IRQ vector
 *	@driver: Pointer to the ipack_driver that manages the device
 *	@ops: Carrier board operations to access the device
 *	@id_space: Virtual address to ID space.
 *	@io_space: Virtual address to IO space.
 *	@mem_space: Virtual address to MEM space.
 *	@dev: device in kernel representation.
 *
 * Warning: Direct access to mapped memory is possible but the endianness
 * is not the same with PCI carrier or VME carrier. The endianness is managed
 * by the carrier board throught @ops.
 */
struct ipack_device {
	char board_name[IPACK_BOARD_NAME_SIZE];
	char bus_name[IPACK_BOARD_NAME_SIZE];
	unsigned int bus_nr;
	unsigned int slot;
	unsigned int irq;
	struct ipack_driver *driver;
	struct ipack_bus_ops *ops;
	struct ipack_addr_space id_space;
	struct ipack_addr_space io_space;
	struct ipack_addr_space mem_space;
	struct device dev;
};

/*
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
 *	struct ipack_driver -- Specific data to each mezzanine board driver
 *
 *	@driver: Device driver kernel representation
 *	@ops: Mezzanine driver operations specific for the ipack bus.
 */
struct ipack_driver {
	struct module *owner;
	struct device_driver driver;
	struct ipack_driver_ops *ops;
};

/*
 *	ipack_driver_register -- Register a new mezzanine driver
 *
 * Called by the mezzanine driver to register itself as a driver
 * that can manage ipack devices.
 */

int ipack_driver_register(struct ipack_driver *edrv);
void ipack_driver_unregister(struct ipack_driver *edrv);

/*
 *	ipack_device_register -- register a new mezzanine device
 *
 * Register a new ipack device (mezzanine device). The call is done by
 * the carrier device driver.
 */
int ipack_device_register(struct ipack_device *dev);
void ipack_device_unregister(struct ipack_device *dev);

/**
 *	struct ipack_bus_ops - available operations on a bridge module
 *
 *	@map_space: map IP address space
 *	@unmap_space: unmap IP address space
 *	@request_irq: request IRQ
 *	@free_irq: free IRQ
 *	@read8: read unsigned char
 *	@read16: read unsigned short
 *	@read32: read unsigned int
 *	@write8: read unsigned char
 *	@write16: read unsigned short
 *	@write32: read unsigned int
 *	@remove_device: tell the bridge module that the device has been removed
 */
struct ipack_bus_ops {
	int (*map_space) (struct ipack_device *dev, unsigned int memory_size, int space);
	int (*unmap_space) (struct ipack_device *dev, int space);
	int (*request_irq) (struct ipack_device *dev, int vector, int (*handler)(void *), void *arg);
	int (*free_irq) (struct ipack_device *dev);
	int (*read8) (struct ipack_device *dev, int space, unsigned long offset, unsigned char *value);
	int (*read16) (struct ipack_device *dev, int space, unsigned long offset, unsigned short *value);
	int (*read32) (struct ipack_device *dev, int space, unsigned long offset, unsigned int *value);
	int (*write8) (struct ipack_device *dev, int space, unsigned long offset, unsigned char value);
	int (*write16) (struct ipack_device *dev, int space, unsigned long offset, unsigned short value);
	int (*write32) (struct ipack_device *dev, int space, unsigned long offset, unsigned int value);
	int (*remove_device) (struct ipack_device *dev);
};

/**
 *	struct ipack_bus_device
 *
 *	@dev: pointer to carrier device
 *	@slots: number of slots available
 *	@bus_nr: ipack bus number
 *	@vector: IRQ base vector. IRQ vectors are $vector + $slot_number
 */
struct ipack_bus_device {
	struct device *dev;
	int slots;
	int bus_nr;
	int vector;
};

/**
 *	ipack_bus_register -- register a new ipack bus
 *
 * The carrier board device driver should call this function to register itself
 * as available bus in ipack.
 */
int ipack_bus_register(struct ipack_bus_device *bus);

/**
 *	ipack_bus_unregister -- unregister an ipack bus
 */
int ipack_bus_unregister(struct ipack_bus_device *bus);
