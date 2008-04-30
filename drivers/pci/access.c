#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/sched.h>
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

/* VPD access through PCI 2.2+ VPD capability */

#define PCI_VPD_PCI22_SIZE (PCI_VPD_ADDR_MASK + 1)

struct pci_vpd_pci22 {
	struct pci_vpd base;
	spinlock_t lock; /* controls access to hardware and the flags */
	u8	cap;
	bool	busy;
	bool	flag; /* value of F bit to wait for */
};

/* Wait for last operation to complete */
static int pci_vpd_pci22_wait(struct pci_dev *dev)
{
	struct pci_vpd_pci22 *vpd =
		container_of(dev->vpd, struct pci_vpd_pci22, base);
	u16 flag, status;
	int wait;
	int ret;

	if (!vpd->busy)
		return 0;

	flag = vpd->flag ? PCI_VPD_ADDR_F : 0;
	wait = vpd->flag ? 10 : 1000; /* read: 100 us; write: 10 ms */
	for (;;) {
		ret = pci_user_read_config_word(dev,
						vpd->cap + PCI_VPD_ADDR,
						&status);
		if (ret < 0)
			return ret;
		if ((status & PCI_VPD_ADDR_F) == flag) {
			vpd->busy = false;
			return 0;
		}
		if (wait-- == 0)
			return -ETIMEDOUT;
		udelay(10);
	}
}

static int pci_vpd_pci22_read(struct pci_dev *dev, int pos, int size,
			      char *buf)
{
	struct pci_vpd_pci22 *vpd =
		container_of(dev->vpd, struct pci_vpd_pci22, base);
	u32 val;
	int ret;
	int begin, end, i;

	if (pos < 0 || pos > PCI_VPD_PCI22_SIZE ||
	    size > PCI_VPD_PCI22_SIZE  - pos)
		return -EINVAL;
	if (size == 0)
		return 0;

	spin_lock_irq(&vpd->lock);
	ret = pci_vpd_pci22_wait(dev);
	if (ret < 0)
		goto out;
	ret = pci_user_write_config_word(dev, vpd->cap + PCI_VPD_ADDR,
					 pos & ~3);
	if (ret < 0)
		goto out;
	vpd->busy = true;
	vpd->flag = 1;
	ret = pci_vpd_pci22_wait(dev);
	if (ret < 0)
		goto out;
	ret = pci_user_read_config_dword(dev, vpd->cap + PCI_VPD_DATA,
					 &val);
out:
	spin_unlock_irq(&vpd->lock);
	if (ret < 0)
		return ret;

	/* Convert to bytes */
	begin = pos & 3;
	end = min(4, begin + size);
	for (i = 0; i < end; ++i) {
		if (i >= begin)
			*buf++ = val;
		val >>= 8;
	}
	return end - begin;
}

static int pci_vpd_pci22_write(struct pci_dev *dev, int pos, int size,
			       const char *buf)
{
	struct pci_vpd_pci22 *vpd =
		container_of(dev->vpd, struct pci_vpd_pci22, base);
	u32 val;
	int ret;

	if (pos < 0 || pos > PCI_VPD_PCI22_SIZE || pos & 3 ||
	    size > PCI_VPD_PCI22_SIZE - pos || size < 4)
		return -EINVAL;

	val = (u8) *buf++;
	val |= ((u8) *buf++) << 8;
	val |= ((u8) *buf++) << 16;
	val |= ((u32)(u8) *buf++) << 24;

	spin_lock_irq(&vpd->lock);
	ret = pci_vpd_pci22_wait(dev);
	if (ret < 0)
		goto out;
	ret = pci_user_write_config_dword(dev, vpd->cap + PCI_VPD_DATA,
					  val);
	if (ret < 0)
		goto out;
	ret = pci_user_write_config_word(dev, vpd->cap + PCI_VPD_ADDR,
					 pos | PCI_VPD_ADDR_F);
	if (ret < 0)
		goto out;
	vpd->busy = true;
	vpd->flag = 0;
	ret = pci_vpd_pci22_wait(dev);
out:
	spin_unlock_irq(&vpd->lock);
	if (ret < 0)
		return ret;

	return 4;
}

static int pci_vpd_pci22_get_size(struct pci_dev *dev)
{
	return PCI_VPD_PCI22_SIZE;
}

static void pci_vpd_pci22_release(struct pci_dev *dev)
{
	kfree(container_of(dev->vpd, struct pci_vpd_pci22, base));
}

static struct pci_vpd_ops pci_vpd_pci22_ops = {
	.read = pci_vpd_pci22_read,
	.write = pci_vpd_pci22_write,
	.get_size = pci_vpd_pci22_get_size,
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

	vpd->base.ops = &pci_vpd_pci22_ops;
	spin_lock_init(&vpd->lock);
	vpd->cap = cap;
	vpd->busy = false;
	dev->vpd = &vpd->base;
	return 0;
}

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
