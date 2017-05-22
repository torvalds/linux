#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
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

#define PCI_OP_READ(size, type, len) \
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

#define PCI_OP_WRITE(size, type, len) \
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

int pci_generic_config_read(struct pci_bus *bus, unsigned int devfn,
			    int where, int size, u32 *val)
{
	void __iomem *addr;

	addr = bus->ops->map_bus(bus, devfn, where);
	if (!addr) {
		*val = ~0;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (size == 1)
		*val = readb(addr);
	else if (size == 2)
		*val = readw(addr);
	else
		*val = readl(addr);

	return PCIBIOS_SUCCESSFUL;
}
EXPORT_SYMBOL_GPL(pci_generic_config_read);

int pci_generic_config_write(struct pci_bus *bus, unsigned int devfn,
			     int where, int size, u32 val)
{
	void __iomem *addr;

	addr = bus->ops->map_bus(bus, devfn, where);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (size == 1)
		writeb(val, addr);
	else if (size == 2)
		writew(val, addr);
	else
		writel(val, addr);

	return PCIBIOS_SUCCESSFUL;
}
EXPORT_SYMBOL_GPL(pci_generic_config_write);

int pci_generic_config_read32(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 *val)
{
	void __iomem *addr;

	addr = bus->ops->map_bus(bus, devfn, where & ~0x3);
	if (!addr) {
		*val = ~0;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	*val = readl(addr);

	if (size <= 2)
		*val = (*val >> (8 * (where & 3))) & ((1 << (size * 8)) - 1);

	return PCIBIOS_SUCCESSFUL;
}
EXPORT_SYMBOL_GPL(pci_generic_config_read32);

int pci_generic_config_write32(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 val)
{
	void __iomem *addr;
	u32 mask, tmp;

	addr = bus->ops->map_bus(bus, devfn, where & ~0x3);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (size == 4) {
		writel(val, addr);
		return PCIBIOS_SUCCESSFUL;
	}

	/*
	 * In general, hardware that supports only 32-bit writes on PCI is
	 * not spec-compliant.  For example, software may perform a 16-bit
	 * write.  If the hardware only supports 32-bit accesses, we must
	 * do a 32-bit read, merge in the 16 bits we intend to write,
	 * followed by a 32-bit write.  If the 16 bits we *don't* intend to
	 * write happen to have any RW1C (write-one-to-clear) bits set, we
	 * just inadvertently cleared something we shouldn't have.
	 */
	dev_warn_ratelimited(&bus->dev, "%d-byte config write to %04x:%02x:%02x.%d offset %#x may corrupt adjacent RW1C bits\n",
			     size, pci_domain_nr(bus), bus->number,
			     PCI_SLOT(devfn), PCI_FUNC(devfn), where);

	mask = ~(((1 << (size * 8)) - 1) << ((where & 0x3) * 8));
	tmp = readl(addr) & mask;
	tmp |= val << ((where & 0x3) * 8);
	writel(tmp, addr);

	return PCIBIOS_SUCCESSFUL;
}
EXPORT_SYMBOL_GPL(pci_generic_config_write32);

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
#define PCI_USER_READ_CONFIG(size, type)					\
int pci_user_read_config_##size						\
	(struct pci_dev *dev, int pos, type *val)			\
{									\
	int ret = PCIBIOS_SUCCESSFUL;					\
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
	return pcibios_err_to_errno(ret);				\
}									\
EXPORT_SYMBOL_GPL(pci_user_read_config_##size);

/* Returns 0 on success, negative values indicate error. */
#define PCI_USER_WRITE_CONFIG(size, type)				\
int pci_user_write_config_##size					\
	(struct pci_dev *dev, int pos, type val)			\
{									\
	int ret = PCIBIOS_SUCCESSFUL;					\
	if (PCI_##size##_BAD)						\
		return -EINVAL;						\
	raw_spin_lock_irq(&pci_lock);				\
	if (unlikely(dev->block_cfg_access))				\
		pci_wait_cfg(dev);					\
	ret = dev->bus->ops->write(dev->bus, dev->devfn,		\
					pos, sizeof(type), val);	\
	raw_spin_unlock_irq(&pci_lock);				\
	return pcibios_err_to_errno(ret);				\
}									\
EXPORT_SYMBOL_GPL(pci_user_write_config_##size);

PCI_USER_READ_CONFIG(byte, u8)
PCI_USER_READ_CONFIG(word, u16)
PCI_USER_READ_CONFIG(dword, u32)
PCI_USER_WRITE_CONFIG(byte, u8)
PCI_USER_WRITE_CONFIG(word, u16)
PCI_USER_WRITE_CONFIG(dword, u32)

/* VPD access through PCI 2.2+ VPD capability */

/**
 * pci_read_vpd - Read one entry from Vital Product Data
 * @dev:	pci device struct
 * @pos:	offset in vpd space
 * @count:	number of bytes to read
 * @buf:	pointer to where to store result
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
 */
ssize_t pci_write_vpd(struct pci_dev *dev, loff_t pos, size_t count, const void *buf)
{
	if (!dev->vpd || !dev->vpd->ops)
		return -ENODEV;
	return dev->vpd->ops->write(dev, pos, count, buf);
}
EXPORT_SYMBOL(pci_write_vpd);

/**
 * pci_set_vpd_size - Set size of Vital Product Data space
 * @dev:	pci device struct
 * @len:	size of vpd space
 */
int pci_set_vpd_size(struct pci_dev *dev, size_t len)
{
	if (!dev->vpd || !dev->vpd->ops)
		return -ENODEV;
	return dev->vpd->ops->set_size(dev, len);
}
EXPORT_SYMBOL(pci_set_vpd_size);

#define PCI_VPD_MAX_SIZE (PCI_VPD_ADDR_MASK + 1)

/**
 * pci_vpd_size - determine actual size of Vital Product Data
 * @dev:	pci device struct
 * @old_size:	current assumed size, also maximum allowed size
 */
static size_t pci_vpd_size(struct pci_dev *dev, size_t old_size)
{
	size_t off = 0;
	unsigned char header[1+2];	/* 1 byte tag, 2 bytes length */

	while (off < old_size &&
	       pci_read_vpd(dev, off, 1, header) == 1) {
		unsigned char tag;

		if (header[0] & PCI_VPD_LRDT) {
			/* Large Resource Data Type Tag */
			tag = pci_vpd_lrdt_tag(header);
			/* Only read length from known tag items */
			if ((tag == PCI_VPD_LTIN_ID_STRING) ||
			    (tag == PCI_VPD_LTIN_RO_DATA) ||
			    (tag == PCI_VPD_LTIN_RW_DATA)) {
				if (pci_read_vpd(dev, off+1, 2,
						 &header[1]) != 2) {
					dev_warn(&dev->dev,
						 "invalid large VPD tag %02x size at offset %zu",
						 tag, off + 1);
					return 0;
				}
				off += PCI_VPD_LRDT_TAG_SIZE +
					pci_vpd_lrdt_size(header);
			}
		} else {
			/* Short Resource Data Type Tag */
			off += PCI_VPD_SRDT_TAG_SIZE +
				pci_vpd_srdt_size(header);
			tag = pci_vpd_srdt_tag(header);
		}

		if (tag == PCI_VPD_STIN_END)	/* End tag descriptor */
			return off;

		if ((tag != PCI_VPD_LTIN_ID_STRING) &&
		    (tag != PCI_VPD_LTIN_RO_DATA) &&
		    (tag != PCI_VPD_LTIN_RW_DATA)) {
			dev_warn(&dev->dev,
				 "invalid %s VPD tag %02x at offset %zu",
				 (header[0] & PCI_VPD_LRDT) ? "large" : "short",
				 tag, off);
			return 0;
		}
	}
	return 0;
}

/*
 * Wait for last operation to complete.
 * This code has to spin since there is no other notification from the PCI
 * hardware. Since the VPD is often implemented by serial attachment to an
 * EEPROM, it may take many milliseconds to complete.
 *
 * Returns 0 on success, negative values indicate error.
 */
static int pci_vpd_wait(struct pci_dev *dev)
{
	struct pci_vpd *vpd = dev->vpd;
	unsigned long timeout = jiffies + msecs_to_jiffies(125);
	unsigned long max_sleep = 16;
	u16 status;
	int ret;

	if (!vpd->busy)
		return 0;

	while (time_before(jiffies, timeout)) {
		ret = pci_user_read_config_word(dev, vpd->cap + PCI_VPD_ADDR,
						&status);
		if (ret < 0)
			return ret;

		if ((status & PCI_VPD_ADDR_F) == vpd->flag) {
			vpd->busy = 0;
			return 0;
		}

		if (fatal_signal_pending(current))
			return -EINTR;

		usleep_range(10, max_sleep);
		if (max_sleep < 1024)
			max_sleep *= 2;
	}

	dev_warn(&dev->dev, "VPD access failed.  This is likely a firmware bug on this device.  Contact the card vendor for a firmware update\n");
	return -ETIMEDOUT;
}

static ssize_t pci_vpd_read(struct pci_dev *dev, loff_t pos, size_t count,
			    void *arg)
{
	struct pci_vpd *vpd = dev->vpd;
	int ret;
	loff_t end = pos + count;
	u8 *buf = arg;

	if (pos < 0)
		return -EINVAL;

	if (!vpd->valid) {
		vpd->valid = 1;
		vpd->len = pci_vpd_size(dev, vpd->len);
	}

	if (vpd->len == 0)
		return -EIO;

	if (pos > vpd->len)
		return 0;

	if (end > vpd->len) {
		end = vpd->len;
		count = end - pos;
	}

	if (mutex_lock_killable(&vpd->lock))
		return -EINTR;

	ret = pci_vpd_wait(dev);
	if (ret < 0)
		goto out;

	while (pos < end) {
		u32 val;
		unsigned int i, skip;

		ret = pci_user_write_config_word(dev, vpd->cap + PCI_VPD_ADDR,
						 pos & ~3);
		if (ret < 0)
			break;
		vpd->busy = 1;
		vpd->flag = PCI_VPD_ADDR_F;
		ret = pci_vpd_wait(dev);
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

static ssize_t pci_vpd_write(struct pci_dev *dev, loff_t pos, size_t count,
			     const void *arg)
{
	struct pci_vpd *vpd = dev->vpd;
	const u8 *buf = arg;
	loff_t end = pos + count;
	int ret = 0;

	if (pos < 0 || (pos & 3) || (count & 3))
		return -EINVAL;

	if (!vpd->valid) {
		vpd->valid = 1;
		vpd->len = pci_vpd_size(dev, vpd->len);
	}

	if (vpd->len == 0)
		return -EIO;

	if (end > vpd->len)
		return -EINVAL;

	if (mutex_lock_killable(&vpd->lock))
		return -EINTR;

	ret = pci_vpd_wait(dev);
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

		vpd->busy = 1;
		vpd->flag = 0;
		ret = pci_vpd_wait(dev);
		if (ret < 0)
			break;

		pos += sizeof(u32);
	}
out:
	mutex_unlock(&vpd->lock);
	return ret ? ret : count;
}

static int pci_vpd_set_size(struct pci_dev *dev, size_t len)
{
	struct pci_vpd *vpd = dev->vpd;

	if (len == 0 || len > PCI_VPD_MAX_SIZE)
		return -EIO;

	vpd->valid = 1;
	vpd->len = len;

	return 0;
}

static const struct pci_vpd_ops pci_vpd_ops = {
	.read = pci_vpd_read,
	.write = pci_vpd_write,
	.set_size = pci_vpd_set_size,
};

static ssize_t pci_vpd_f0_read(struct pci_dev *dev, loff_t pos, size_t count,
			       void *arg)
{
	struct pci_dev *tdev = pci_get_slot(dev->bus,
					    PCI_DEVFN(PCI_SLOT(dev->devfn), 0));
	ssize_t ret;

	if (!tdev)
		return -ENODEV;

	ret = pci_read_vpd(tdev, pos, count, arg);
	pci_dev_put(tdev);
	return ret;
}

static ssize_t pci_vpd_f0_write(struct pci_dev *dev, loff_t pos, size_t count,
				const void *arg)
{
	struct pci_dev *tdev = pci_get_slot(dev->bus,
					    PCI_DEVFN(PCI_SLOT(dev->devfn), 0));
	ssize_t ret;

	if (!tdev)
		return -ENODEV;

	ret = pci_write_vpd(tdev, pos, count, arg);
	pci_dev_put(tdev);
	return ret;
}

static int pci_vpd_f0_set_size(struct pci_dev *dev, size_t len)
{
	struct pci_dev *tdev = pci_get_slot(dev->bus,
					    PCI_DEVFN(PCI_SLOT(dev->devfn), 0));
	int ret;

	if (!tdev)
		return -ENODEV;

	ret = pci_set_vpd_size(tdev, len);
	pci_dev_put(tdev);
	return ret;
}

static const struct pci_vpd_ops pci_vpd_f0_ops = {
	.read = pci_vpd_f0_read,
	.write = pci_vpd_f0_write,
	.set_size = pci_vpd_f0_set_size,
};

int pci_vpd_init(struct pci_dev *dev)
{
	struct pci_vpd *vpd;
	u8 cap;

	cap = pci_find_capability(dev, PCI_CAP_ID_VPD);
	if (!cap)
		return -ENODEV;

	vpd = kzalloc(sizeof(*vpd), GFP_ATOMIC);
	if (!vpd)
		return -ENOMEM;

	vpd->len = PCI_VPD_MAX_SIZE;
	if (dev->dev_flags & PCI_DEV_FLAGS_VPD_REF_F0)
		vpd->ops = &pci_vpd_f0_ops;
	else
		vpd->ops = &pci_vpd_ops;
	mutex_init(&vpd->lock);
	vpd->cap = cap;
	vpd->busy = 0;
	vpd->valid = 0;
	dev->vpd = vpd;
	return 0;
}

void pci_vpd_release(struct pci_dev *dev)
{
	kfree(dev->vpd);
}

/**
 * pci_cfg_access_lock - Lock PCI config reads/writes
 * @dev:	pci device struct
 *
 * When access is locked, any userspace reads or writes to config
 * space and concurrent lock requests will sleep until access is
 * allowed via pci_cfg_access_unlock() again.
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
	raw_spin_unlock_irqrestore(&pci_lock, flags);

	wake_up_all(&pci_cfg_wait);
}
EXPORT_SYMBOL_GPL(pci_cfg_access_unlock);

static inline int pcie_cap_version(const struct pci_dev *dev)
{
	return pcie_caps_reg(dev) & PCI_EXP_FLAGS_VERS;
}

static bool pcie_downstream_port(const struct pci_dev *dev)
{
	int type = pci_pcie_type(dev);

	return type == PCI_EXP_TYPE_ROOT_PORT ||
	       type == PCI_EXP_TYPE_DOWNSTREAM ||
	       type == PCI_EXP_TYPE_PCIE_BRIDGE;
}

bool pcie_cap_has_lnkctl(const struct pci_dev *dev)
{
	int type = pci_pcie_type(dev);

	return type == PCI_EXP_TYPE_ENDPOINT ||
	       type == PCI_EXP_TYPE_LEG_END ||
	       type == PCI_EXP_TYPE_ROOT_PORT ||
	       type == PCI_EXP_TYPE_UPSTREAM ||
	       type == PCI_EXP_TYPE_DOWNSTREAM ||
	       type == PCI_EXP_TYPE_PCI_BRIDGE ||
	       type == PCI_EXP_TYPE_PCIE_BRIDGE;
}

static inline bool pcie_cap_has_sltctl(const struct pci_dev *dev)
{
	return pcie_downstream_port(dev) &&
	       pcie_caps_reg(dev) & PCI_EXP_FLAGS_SLOT;
}

static inline bool pcie_cap_has_rtctl(const struct pci_dev *dev)
{
	int type = pci_pcie_type(dev);

	return type == PCI_EXP_TYPE_ROOT_PORT ||
	       type == PCI_EXP_TYPE_RC_EC;
}

static bool pcie_capability_reg_implemented(struct pci_dev *dev, int pos)
{
	if (!pci_is_pcie(dev))
		return false;

	switch (pos) {
	case PCI_EXP_FLAGS:
		return true;
	case PCI_EXP_DEVCAP:
	case PCI_EXP_DEVCTL:
	case PCI_EXP_DEVSTA:
		return true;
	case PCI_EXP_LNKCAP:
	case PCI_EXP_LNKCTL:
	case PCI_EXP_LNKSTA:
		return pcie_cap_has_lnkctl(dev);
	case PCI_EXP_SLTCAP:
	case PCI_EXP_SLTCTL:
	case PCI_EXP_SLTSTA:
		return pcie_cap_has_sltctl(dev);
	case PCI_EXP_RTCTL:
	case PCI_EXP_RTCAP:
	case PCI_EXP_RTSTA:
		return pcie_cap_has_rtctl(dev);
	case PCI_EXP_DEVCAP2:
	case PCI_EXP_DEVCTL2:
	case PCI_EXP_LNKCAP2:
	case PCI_EXP_LNKCTL2:
	case PCI_EXP_LNKSTA2:
		return pcie_cap_version(dev) > 1;
	default:
		return false;
	}
}

/*
 * Note that these accessor functions are only for the "PCI Express
 * Capability" (see PCIe spec r3.0, sec 7.8).  They do not apply to the
 * other "PCI Express Extended Capabilities" (AER, VC, ACS, MFVC, etc.)
 */
int pcie_capability_read_word(struct pci_dev *dev, int pos, u16 *val)
{
	int ret;

	*val = 0;
	if (pos & 1)
		return -EINVAL;

	if (pcie_capability_reg_implemented(dev, pos)) {
		ret = pci_read_config_word(dev, pci_pcie_cap(dev) + pos, val);
		/*
		 * Reset *val to 0 if pci_read_config_word() fails, it may
		 * have been written as 0xFFFF if hardware error happens
		 * during pci_read_config_word().
		 */
		if (ret)
			*val = 0;
		return ret;
	}

	/*
	 * For Functions that do not implement the Slot Capabilities,
	 * Slot Status, and Slot Control registers, these spaces must
	 * be hardwired to 0b, with the exception of the Presence Detect
	 * State bit in the Slot Status register of Downstream Ports,
	 * which must be hardwired to 1b.  (PCIe Base Spec 3.0, sec 7.8)
	 */
	if (pci_is_pcie(dev) && pcie_downstream_port(dev) &&
	    pos == PCI_EXP_SLTSTA)
		*val = PCI_EXP_SLTSTA_PDS;

	return 0;
}
EXPORT_SYMBOL(pcie_capability_read_word);

int pcie_capability_read_dword(struct pci_dev *dev, int pos, u32 *val)
{
	int ret;

	*val = 0;
	if (pos & 3)
		return -EINVAL;

	if (pcie_capability_reg_implemented(dev, pos)) {
		ret = pci_read_config_dword(dev, pci_pcie_cap(dev) + pos, val);
		/*
		 * Reset *val to 0 if pci_read_config_dword() fails, it may
		 * have been written as 0xFFFFFFFF if hardware error happens
		 * during pci_read_config_dword().
		 */
		if (ret)
			*val = 0;
		return ret;
	}

	if (pci_is_pcie(dev) && pcie_downstream_port(dev) &&
	    pos == PCI_EXP_SLTSTA)
		*val = PCI_EXP_SLTSTA_PDS;

	return 0;
}
EXPORT_SYMBOL(pcie_capability_read_dword);

int pcie_capability_write_word(struct pci_dev *dev, int pos, u16 val)
{
	if (pos & 1)
		return -EINVAL;

	if (!pcie_capability_reg_implemented(dev, pos))
		return 0;

	return pci_write_config_word(dev, pci_pcie_cap(dev) + pos, val);
}
EXPORT_SYMBOL(pcie_capability_write_word);

int pcie_capability_write_dword(struct pci_dev *dev, int pos, u32 val)
{
	if (pos & 3)
		return -EINVAL;

	if (!pcie_capability_reg_implemented(dev, pos))
		return 0;

	return pci_write_config_dword(dev, pci_pcie_cap(dev) + pos, val);
}
EXPORT_SYMBOL(pcie_capability_write_dword);

int pcie_capability_clear_and_set_word(struct pci_dev *dev, int pos,
				       u16 clear, u16 set)
{
	int ret;
	u16 val;

	ret = pcie_capability_read_word(dev, pos, &val);
	if (!ret) {
		val &= ~clear;
		val |= set;
		ret = pcie_capability_write_word(dev, pos, val);
	}

	return ret;
}
EXPORT_SYMBOL(pcie_capability_clear_and_set_word);

int pcie_capability_clear_and_set_dword(struct pci_dev *dev, int pos,
					u32 clear, u32 set)
{
	int ret;
	u32 val;

	ret = pcie_capability_read_dword(dev, pos, &val);
	if (!ret) {
		val &= ~clear;
		val |= set;
		ret = pcie_capability_write_dword(dev, pos, val);
	}

	return ret;
}
EXPORT_SYMBOL(pcie_capability_clear_and_set_dword);

int pci_read_config_byte(const struct pci_dev *dev, int where, u8 *val)
{
	if (pci_dev_is_disconnected(dev)) {
		*val = ~0;
		return -ENODEV;
	}
	return pci_bus_read_config_byte(dev->bus, dev->devfn, where, val);
}
EXPORT_SYMBOL(pci_read_config_byte);

int pci_read_config_word(const struct pci_dev *dev, int where, u16 *val)
{
	if (pci_dev_is_disconnected(dev)) {
		*val = ~0;
		return -ENODEV;
	}
	return pci_bus_read_config_word(dev->bus, dev->devfn, where, val);
}
EXPORT_SYMBOL(pci_read_config_word);

int pci_read_config_dword(const struct pci_dev *dev, int where,
					u32 *val)
{
	if (pci_dev_is_disconnected(dev)) {
		*val = ~0;
		return -ENODEV;
	}
	return pci_bus_read_config_dword(dev->bus, dev->devfn, where, val);
}
EXPORT_SYMBOL(pci_read_config_dword);

int pci_write_config_byte(const struct pci_dev *dev, int where, u8 val)
{
	if (pci_dev_is_disconnected(dev))
		return -ENODEV;
	return pci_bus_write_config_byte(dev->bus, dev->devfn, where, val);
}
EXPORT_SYMBOL(pci_write_config_byte);

int pci_write_config_word(const struct pci_dev *dev, int where, u16 val)
{
	if (pci_dev_is_disconnected(dev))
		return -ENODEV;
	return pci_bus_write_config_word(dev->bus, dev->devfn, where, val);
}
EXPORT_SYMBOL(pci_write_config_word);

int pci_write_config_dword(const struct pci_dev *dev, int where,
					 u32 val)
{
	if (pci_dev_is_disconnected(dev))
		return -ENODEV;
	return pci_bus_write_config_dword(dev->bus, dev->devfn, where, val);
}
EXPORT_SYMBOL(pci_write_config_dword);
