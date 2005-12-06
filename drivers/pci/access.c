#include <linux/pci.h>
#include <linux/module.h>
#include <linux/ioport.h>

#include "pci.h"

/*
 * This interrupt-safe spinlock protects all accesses to PCI
 * configuration space.
 */

static DEFINE_SPINLOCK(pci_lock);

/*
 *  Wrappers for all PCI configuration access functions.  They just check
 *  alignment, do locking and call the low-level functions pointed to
 *  by pci_dev->ops.
 */

#define PCI_byte_BAD 0
#define PCI_word_BAD (pos & 1)
#define PCI_dword_BAD (pos & 3)

#define PCI_OP_READ(size,type,len) \
int pci_bus_read_config_##size \
	(struct pci_bus *bus, unsigned int devfn, int pos, type *value)	\
{									\
	int res;							\
	unsigned long flags;						\
	u32 data = 0;							\
	if (PCI_##size##_BAD) return PCIBIOS_BAD_REGISTER_NUMBER;	\
	spin_lock_irqsave(&pci_lock, flags);				\
	res = bus->ops->read(bus, devfn, pos, len, &data);		\
	*value = (type)data;						\
	spin_unlock_irqrestore(&pci_lock, flags);			\
	return res;							\
}

#define PCI_OP_WRITE(size,type,len) \
int pci_bus_write_config_##size \
	(struct pci_bus *bus, unsigned int devfn, int pos, type value)	\
{									\
	int res;							\
	unsigned long flags;						\
	if (PCI_##size##_BAD) return PCIBIOS_BAD_REGISTER_NUMBER;	\
	spin_lock_irqsave(&pci_lock, flags);				\
	res = bus->ops->write(bus, devfn, pos, len, value);		\
	spin_unlock_irqrestore(&pci_lock, flags);			\
	return res;							\
}

PCI_OP_READ(byte, u8, 1)
PCI_OP_READ(word, u16, 2)
PCI_OP_READ(dword, u32, 4)
PCI_OP_WRITE(byte, u8, 1)
PCI_OP_WRITE(word, u16, 2)
PCI_OP_WRITE(dword, u32, 4)

EXPORT_SYMBOL(pci_bus_read_config_byte);
EXPORT_SYMBOL(pci_bus_read_config_word);
EXPORT_SYMBOL(pci_bus_read_config_dword);
EXPORT_SYMBOL(pci_bus_write_config_byte);
EXPORT_SYMBOL(pci_bus_write_config_word);
EXPORT_SYMBOL(pci_bus_write_config_dword);

static u32 pci_user_cached_config(struct pci_dev *dev, int pos)
{
	u32 data;

	data = dev->saved_config_space[pos/sizeof(dev->saved_config_space[0])];
	data >>= (pos % sizeof(dev->saved_config_space[0])) * 8;
	return data;
}

#define PCI_USER_READ_CONFIG(size,type)					\
int pci_user_read_config_##size						\
	(struct pci_dev *dev, int pos, type *val)			\
{									\
	unsigned long flags;						\
	int ret = 0;							\
	u32 data = -1;							\
	if (PCI_##size##_BAD) return PCIBIOS_BAD_REGISTER_NUMBER;	\
	spin_lock_irqsave(&pci_lock, flags);				\
	if (likely(!dev->block_ucfg_access))				\
		ret = dev->bus->ops->read(dev->bus, dev->devfn,		\
					pos, sizeof(type), &data);	\
	else if (pos < sizeof(dev->saved_config_space))			\
		data = pci_user_cached_config(dev, pos); 		\
	spin_unlock_irqrestore(&pci_lock, flags);			\
	*val = (type)data;						\
	return ret;							\
}

#define PCI_USER_WRITE_CONFIG(size,type)				\
int pci_user_write_config_##size					\
	(struct pci_dev *dev, int pos, type val)			\
{									\
	unsigned long flags;						\
	int ret = -EIO;							\
	if (PCI_##size##_BAD) return PCIBIOS_BAD_REGISTER_NUMBER;	\
	spin_lock_irqsave(&pci_lock, flags);				\
	if (likely(!dev->block_ucfg_access))				\
		ret = dev->bus->ops->write(dev->bus, dev->devfn,	\
					pos, sizeof(type), val);	\
	spin_unlock_irqrestore(&pci_lock, flags);			\
	return ret;							\
}

PCI_USER_READ_CONFIG(byte, u8)
PCI_USER_READ_CONFIG(word, u16)
PCI_USER_READ_CONFIG(dword, u32)
PCI_USER_WRITE_CONFIG(byte, u8)
PCI_USER_WRITE_CONFIG(word, u16)
PCI_USER_WRITE_CONFIG(dword, u32)

/**
 * pci_block_user_cfg_access - Block userspace PCI config reads/writes
 * @dev:	pci device struct
 *
 * This function blocks any userspace PCI config accesses from occurring.
 * When blocked, any writes will be bit bucketed and reads will return the
 * data saved using pci_save_state for the first 64 bytes of config
 * space and return 0xff for all other config reads.
 **/
void pci_block_user_cfg_access(struct pci_dev *dev)
{
	unsigned long flags;

	pci_save_state(dev);

	/* spinlock to synchronize with anyone reading config space now */
	spin_lock_irqsave(&pci_lock, flags);
	dev->block_ucfg_access = 1;
	spin_unlock_irqrestore(&pci_lock, flags);
}
EXPORT_SYMBOL_GPL(pci_block_user_cfg_access);

/**
 * pci_unblock_user_cfg_access - Unblock userspace PCI config reads/writes
 * @dev:	pci device struct
 *
 * This function allows userspace PCI config accesses to resume.
 **/
void pci_unblock_user_cfg_access(struct pci_dev *dev)
{
	unsigned long flags;

	/* spinlock to synchronize with anyone reading saved config space */
	spin_lock_irqsave(&pci_lock, flags);
	dev->block_ucfg_access = 0;
	spin_unlock_irqrestore(&pci_lock, flags);
}
EXPORT_SYMBOL_GPL(pci_unblock_user_cfg_access);
