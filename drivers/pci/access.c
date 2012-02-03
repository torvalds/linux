#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/wait.h>

#include "pci.h"

/*
 * This interrupt-safe spinlock protects all accesses to PCI
 * configuration space.
 */

DEFINE_RAW_SPINLOCK(pci_lock);

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
	raw_spin_lock_irqsave(&pci_lock, flags);			\
	res = bus->ops->read(bus, devfn, pos, len, &data);		\
	*value = (type)data;						\
	raw_spin_unlock_irqrestore(&pci_lock, flags);		\
	return res;							\
}

#define PCI_OP_WRITE(size,type,len) \
int pci_bus_write_config_##size \
	(struct pci_bus *bus, unsigned int devfn, int pos, type value)	\
{									\
	int res;							\
	unsigned long flags;						\
	if (PCI_##size##_BAD) return PCIBIOS_BAD_REGISTER_NUMBER;	\
	raw_spin_lock_irqsave(&pci_lock, flags);			\
	res = bus->ops->write(bus, devfn, pos, len, value);		\
	raw_spin_unlock_irqrestore(&pci_lock, flags);		\
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

/**
 * pci_bus_set_ops - Set raw operations of pci bus
 * @bus:	pci bus struct
 * @ops:	new raw operations
 *
 * Return previous raw operations
 */
struct pci_ops *pci_bus_set_ops(struct pci_bus *bus, struct pci_ops *ops)
{
	struct pci_ops *old_ops;
	unsigned long flags;

	raw_spin_lock_irqsave(&pci_lock, flags);
	old_ops = bus->ops;
	bus->ops = ops;
	raw_spin_unlock_irqrestore(&pci_lock, flags);
	return old_ops;
}
EXPORT_SYMBOL(pci_bus_set_ops);

/**
 * pci_read_vpd - Read one entry from Vital Product Data
 * @dev:	pci device struct
 * @pos:	offset in vpd space
 * @count:	number of bytes to read
 * @buf:	pointer to where to store result
 *
 */
ssize_t pci_read_vpd(struct pci_dev *dev, loff_t pos, size_t count, void *buf)
{
	if (!dev->vpd || !dev->vpd->ops)
		return -ENODEV;
	return dev->vpd->ops->read(dev, pos, count, buf);
}
EXPORT_SYMBOL(pci_read_vpd);

/**
 * pci_write_vpd - Write entry to Vital Product Data
 * @dev:	pci device struct
 * @pos:	offset in vpd space
 * @count:	number of bytes to write
 * @buf:	buffer containing write data
 *
 */
ssize_t pci_write_vpd(struct pci_dev *dev, loff_t pos, size_t count, const void *buf)
{
	if (!dev->vpd || !dev->vpd->ops)
		return -ENODEV;
	return dev->vpd->ops->write(dev, pos, count, buf);
}
EXPORT_SYMBOL(pci_write_vpd);

/*
 * The following routines are to prevent the user from accessing PCI config
 * space when it's unsafe to do so.  Some devices require this during BIST and
 * we're required to prevent it during D-state transitions.
 *
 * We have a bit per device to indicate it's blocked and a global wait queue
 * for callers to sleep on until devices are unblocked.
 */
static DECLARE_WAIT_QUEUE_HEAD(pci_cfg_wait);

static noinline void pci_wait_cfg(struct pci_dev *dev)
{
	DECLARE_WAITQUEUE(wait, current);

	__add_wait_queue(&pci_cfg_wait, &wait);
	do {
		set_current_state(TASK_UNINTERRUPTIBLE);
		raw_spin_unlock_irq(&pci_lock);
		schedule();
		raw_spin_lock_irq(&pci_lock);
	} while (dev->block_cfg_access);
	__remove_wait_queue(&pci_cfg_wait, &wait);
}

/* Returns 0 on success, negative values indicate error. */
#define PCI_USER_READ_CONFIG(size,type)					\
int pci_user_read_config_##size						\
	(struct pci_dev *dev, int pos, type *val)			\
{									\
	int ret = 0;							\
	u32 data = -1;							\
	if (PCI_##size##_BAD)						\
		return -EINVAL;						\
	raw_spin_lock_irq(&pci_lock);				\
	if (unlikely(dev->block_cfg_access))				\
		pci_wait_cfg(dev);					\
	ret = dev->bus->ops->read(dev->bus, dev->devfn,			\
					pos, sizeof(type), &data);	\
	raw_spin_unlock_irq(&pci_lock);				\
	*val = (type)data;						\
	if (ret > 0)							\
		ret = -EINVAL;						\
	return ret;							\
}

/* Returns 0 on success, negative values indicate error. */
#define PCI_USER_WRITE_CONFIG(size,type)				\
int pci_user_write_config_##size					\
	(struct pci_dev *dev, int pos, type val)			\
{									\
	int ret = -EIO;							\
	if (PCI_##size##_BAD)						\
		return -EINVAL;						\
	raw_spin_lock_irq(&pci_lock);				\
	if (unlikely(dev->block_cfg_access))				\
		pci_wait_cfg(dev);					\
	ret = dev->bus->ops->write(dev->bus, dev->devfn,		\
					pos, sizeof(type), val);	\
	raw_spin_unlock_irq(&pci_lock);				\
	if (ret > 0)							\
		ret = -EINVAL;						\
	return ret;							\
}

PCI_USER_READ_CONFIG(byte, u8)
PCI_USER_READ_CONFIG(word, u16)
PCI_USER_READ_CONFIG(dword, u32)
PCI_USER_WRITE_CONFIG(byte, u8)
PCI_USER_WRITE_CONFIG(word, u16)
PCI_USER_WRITE_CONFIG(dword, u32)

/* VPD access through PCI 2.2+ VPD capability */

#define PCI_VPD_PCI22_SIZE (PCI_VPD_ADDR_MASK + 1)

struct pci_vpd_pci22 {
	struct pci_vpd base;
	struct mutex lock;
	u16	flag;
	bool	busy;
	u8	cap;
};

/*
 * Wait for last operation to complete.
 * This code has to spin since there is no other notification from the PCI
 * hardware. Since the VPD is often implemented by serial attachment to an
 * EEPROM, it may take many milliseconds to complete.
 *
 * Returns 0 on success, negative values indicate error.
 */
static int pci_vpd_pci22_wait(struct pci_dev *dev)
{
	struct pci_vpd_pci22 *vpd =
		container_of(dev->vpd, struct pci_vpd_pci22, base);
	unsigned long timeout = jiffies + HZ/20 + 2;
	u16 status;
	int ret;

	if (!vpd->busy)
		return 0;

	for (;;) {
		ret = pci_user_read_config_word(dev, vpd->cap + PCI_VPD_ADDR,
						&status);
		if (ret < 0)
			return ret;

		if ((status & PCI_VPD_ADDR_F) == vpd->flag) {
			vpd->busy = false;
			return 0;
		}

		if (time_after(jiffies, timeout)) {
			dev_printk(KERN_DEBUG, &dev->dev,
				   "vpd r/w failed.  This is likely a firmware "
				   "bug on this device.  Contact the card "
				   "vendor for a firmware update.");
			return -ETIMEDOUT;
		}
		if (fatal_signal_pending(current))
			return -EINTR;
		if (!cond_resched())
			udelay(10);
	}
}

static ssize_t pci_vpd_pci22_read(struct pci_dev *dev, loff_t pos, size_t count,
				  void *arg)
{
	struct pci_vpd_pci22 *vpd =
		container_of(dev->vpd, struct pci_vpd_pci22, base);
	int ret;
	loff_t end = pos + count;
	u8 *buf = arg;

	if (pos < 0 || pos > vpd->base.len || end > vpd->base.len)
		return -EINVAL;

	if (mutex_lock_killable(&vpd->lock))
		return -EINTR;

	ret = pci_vpd_pci22_wait(dev);
	if (ret < 0)
		goto out;

	while (pos < end) {
		u32 val;
		unsigned int i, skip;

		ret = pci_user_write_config_word(dev, vpd->cap + PCI_VPD_ADDR,
						 pos & ~3);
		if (ret < 0)
			break;
		vpd->busy = true;
		vpd->flag = PCI_VPD_ADDR_F;
		ret = pci_vpd_pci22_wait(dev);
		if (ret < 0)
			break;

		ret = pci_user_read_config_dword(dev, vpd->cap + PCI_VPD_DATA, &val);
		if (ret < 0)
			break;

		skip = pos & 3;
		for (i = 0;  i < sizeof(u32); i++) {
			if (i >= skip) {
				*buf++ = val;
				if (++pos == end)
					break;
			}
			val >>= 8;
		}
	}
out:
	mutex_unlock(&vpd->lock);
	return ret ? ret : count;
}

static ssize_t pci_vpd_pci22_write(struct pci_dev *dev, loff_t pos, size_t count,
				   const void *arg)
{
	struct pci_vpd_pci22 *vpd =
		container_of(dev->vpd, struct pci_vpd_pci22, base);
	const u8 *buf = arg;
	loff_t end = pos + count;
	int ret = 0;

	if (pos < 0 || (pos & 3) || (count & 3) || end > vpd->base.len)
		return -EINVAL;

	if (mutex_lock_killable(&vpd->lock))
		return -EINTR;

	ret = pci_vpd_pci22_wait(dev);
	if (ret < 0)
		goto out;

	while (pos < end) {
		u32 val;

		val = *buf++;
		val |= *buf++ << 8;
		val |= *buf++ << 16;
		val |= *buf++ << 24;

		ret = pci_user_write_config_dword(dev, vpd->cap + PCI_VPD_DATA, val);
		if (ret < 0)
			break;
		ret = pci_user_write_config_word(dev, vpd->cap + PCI_VPD_ADDR,
						 pos | PCI_VPD_ADDR_F);
		if (ret < 0)
			break;

		vpd->busy = true;
		vpd->flag = 0;
		ret = pci_vpd_pci22_wait(dev);
		if (ret < 0)
			break;

		pos += sizeof(u32);
	}
out:
	mutex_unlock(&vpd->lock);
	return ret ? ret : count;
}

static void pci_vpd_pci22_release(struct pci_dev *dev)
{
	kfree(container_of(dev->vpd, struct pci_vpd_pci22, base));
}

static const struct pci_vpd_ops pci_vpd_pci22_ops = {
	.read = pci_vpd_pci22_read,
	.write = pci_vpd_pci22_write,
	.release = pci_vpd_pci22_release,
};

int pci_vpd_pci22_init(struct pci_dev *dev)
{
	struct pci_vpd_pci22 *vpd;
	u8 cap;

	cap = pci_find_capability(dev, PCI_CAP_ID_VPD);
	if (!cap)
		return -ENODEV;
	vpd = kzalloc(sizeof(*vpd), GFP_ATOMIC);
	if (!vpd)
		return -ENOMEM;

	vpd->base.len = PCI_VPD_PCI22_SIZE;
	vpd->base.ops = &pci_vpd_pci22_ops;
	mutex_init(&vpd->lock);
	vpd->cap = cap;
	vpd->busy = false;
	dev->vpd = &vpd->base;
	return 0;
}

/**
 * pci_vpd_truncate - Set available Vital Product Data size
 * @dev:	pci device struct
 * @size:	available memory in bytes
 *
 * Adjust size of available VPD area.
 */
int pci_vpd_truncate(struct pci_dev *dev, size_t size)
{
	if (!dev->vpd)
		return -EINVAL;

	/* limited by the access method */
	if (size > dev->vpd->len)
		return -EINVAL;

	dev->vpd->len = size;
	if (dev->vpd->attr)
		dev->vpd->attr->size = size;

	return 0;
}
EXPORT_SYMBOL(pci_vpd_truncate);

/**
 * pci_cfg_access_lock - Lock PCI config reads/writes
 * @dev:	pci device struct
 *
 * When access is locked, any userspace reads or writes to config
 * space and concurrent lock requests will sleep until access is
 * allowed via pci_cfg_access_unlocked again.
 */
void pci_cfg_access_lock(struct pci_dev *dev)
{
	might_sleep();

	raw_spin_lock_irq(&pci_lock);
	if (dev->block_cfg_access)
		pci_wait_cfg(dev);
	dev->block_cfg_access = 1;
	raw_spin_unlock_irq(&pci_lock);
}
EXPORT_SYMBOL_GPL(pci_cfg_access_lock);

/**
 * pci_cfg_access_trylock - try to lock PCI config reads/writes
 * @dev:	pci device struct
 *
 * Same as pci_cfg_access_lock, but will return 0 if access is
 * already locked, 1 otherwise. This function can be used from
 * atomic contexts.
 */
bool pci_cfg_access_trylock(struct pci_dev *dev)
{
	unsigned long flags;
	bool locked = true;

	raw_spin_lock_irqsave(&pci_lock, flags);
	if (dev->block_cfg_access)
		locked = false;
	else
		dev->block_cfg_access = 1;
	raw_spin_unlock_irqrestore(&pci_lock, flags);

	return locked;
}
EXPORT_SYMBOL_GPL(pci_cfg_access_trylock);

/**
 * pci_cfg_access_unlock - Unlock PCI config reads/writes
 * @dev:	pci device struct
 *
 * This function allows PCI config accesses to resume.
 */
void pci_cfg_access_unlock(struct pci_dev *dev)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&pci_lock, flags);

	/* This indicates a problem in the caller, but we don't need
	 * to kill them, unlike a double-block above. */
	WARN_ON(!dev->block_cfg_access);

	dev->block_cfg_access = 0;
	wake_up_all(&pci_cfg_wait);
	raw_spin_unlock_irqrestore(&pci_lock, flags);
}
EXPORT_SYMBOL_GPL(pci_cfg_access_unlock);
