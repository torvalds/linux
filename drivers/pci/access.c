#include <linux/pci.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/wait.h>

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

/*
 * The following routines are to prevent the user from accessing PCI config
 * space when it's unsafe to do so.  Some devices require this during BIST and
 * we're required to prevent it during D-state transitions.
 *
 * We have a bit per device to indicate it's blocked and a global wait queue
 * for callers to sleep on until devices are unblocked.
 */
static DECLARE_WAIT_QUEUE_HEAD(pci_ucfg_wait);

static noinline void pci_wait_ucfg(struct pci_dev *dev)
{
	DECLARE_WAITQUEUE(wait, current);

	__add_wait_queue(&pci_ucfg_wait, &wait);
	do {
		set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(&pci_lock);
		schedule();
		spin_lock_irq(&pci_lock);
	} while (dev->block_ucfg_access);
	__remove_wait_queue(&pci_ucfg_wait, &wait);
}

#define PCI_USER_READ_CONFIG(size,type)					\
int pci_user_read_config_##size						\
	(struct pci_dev *dev, int pos, type *val)			\
{									\
	int ret = 0;							\
	u32 data = -1;							\
	if (PCI_##size##_BAD) return PCIBIOS_BAD_REGISTER_NUMBER;	\
	spin_lock_irq(&pci_lock);					\
	if (unlikely(dev->block_ucfg_access)) pci_wait_ucfg(dev);	\
	ret = dev->bus->ops->read(dev->bus, dev->devfn,			\
					pos, sizeof(type), &data);	\
	spin_unlock_irq(&pci_lock);					\
	*val = (type)data;						\
	return ret;							\
}

#define PCI_USER_WRITE_CONFIG(size,type)				\
int pci_user_write_config_##size					\
	(struct pci_dev *dev, int pos, type val)			\
{									\
	int ret = -EIO;							\
	if (PCI_##size##_BAD) return PCIBIOS_BAD_REGISTER_NUMBER;	\
	spin_lock_irq(&pci_lock);					\
	if (unlikely(dev->block_ucfg_access)) pci_wait_ucfg(dev);	\
	ret = dev->bus->ops->write(dev->bus, dev->devfn,		\
					pos, sizeof(type), val);	\
	spin_unlock_irq(&pci_lock);					\
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
 * When user access is blocked, any reads or writes to config space will
 * sleep until access is unblocked again.  We don't allow nesting of
 * block/unblock calls.
 */
void pci_block_user_cfg_access(struct pci_dev *dev)
{
	unsigned long flags;
	int was_blocked;

	spin_lock_irqsave(&pci_lock, flags);
	was_blocked = dev->block_ucfg_access;
	dev->block_ucfg_access = 1;
	spin_unlock_irqrestore(&pci_lock, flags);

	/* If we BUG() inside the pci_lock, we're guaranteed to hose
	 * the machine */
	BUG_ON(was_blocked);
}
EXPORT_SYMBOL_GPL(pci_block_user_cfg_access);

/**
 * pci_unblock_user_cfg_access - Unblock userspace PCI config reads/writes
 * @dev:	pci device struct
 *
 * This function allows userspace PCI config accesses to resume.
 */
void pci_unblock_user_cfg_access(struct pci_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pci_lock, flags);

	/* This indicates a problem in the caller, but we don't need
	 * to kill them, unlike a double-block above. */
	WARN_ON(!dev->block_ucfg_access);

	dev->block_ucfg_access = 0;
	wake_up_all(&pci_ucfg_wait);
	spin_unlock_irqrestore(&pci_lock, flags);
}
EXPORT_SYMBOL_GPL(pci_unblock_user_cfg_access);
