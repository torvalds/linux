/*
 * Copyright IBM Corp. 2012
 *
 * Author(s):
 *   Jan Glauber <jang@linux.vnet.ibm.com>
 */

#define COMPONENT "zPCI"
#define pr_fmt(fmt) COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/rculist.h>
#include <linux/hash.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <asm/hw_irq.h>

/* mapping of irq numbers to msi_desc */
static struct hlist_head *msi_hash;
static unsigned int msihash_shift = 6;
#define msi_hashfn(nr)	hash_long(nr, msihash_shift)

static DEFINE_SPINLOCK(msi_map_lock);

struct msi_desc *__irq_get_msi_desc(unsigned int irq)
{
	struct msi_map *map;

	hlist_for_each_entry_rcu(map,
			&msi_hash[msi_hashfn(irq)], msi_chain)
		if (map->irq == irq)
			return map->msi;
	return NULL;
}

int zpci_msi_set_mask_bits(struct msi_desc *msi, u32 mask, u32 flag)
{
	if (msi->msi_attrib.is_msix) {
		int offset = msi->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_VECTOR_CTRL;
		msi->masked = readl(msi->mask_base + offset);
		writel(flag, msi->mask_base + offset);
	} else {
		if (msi->msi_attrib.maskbit) {
			int pos;
			u32 mask_bits;

			pos = (long) msi->mask_base;
			pci_read_config_dword(msi->dev, pos, &mask_bits);
			mask_bits &= ~(mask);
			mask_bits |= flag & mask;
			pci_write_config_dword(msi->dev, pos, mask_bits);
		} else {
			return 0;
		}
	}

	msi->msi_attrib.maskbit = !!flag;
	return 1;
}

int zpci_setup_msi_irq(struct zpci_dev *zdev, struct msi_desc *msi,
			unsigned int nr, int offset)
{
	struct msi_map *map;
	struct msi_msg msg;
	int rc;

	map = kmalloc(sizeof(*map), GFP_KERNEL);
	if (map == NULL)
		return -ENOMEM;

	map->irq = nr;
	map->msi = msi;
	zdev->msi_map[nr & ZPCI_MSI_MASK] = map;

	pr_debug("%s hashing irq: %u  to bucket nr: %llu\n",
		__func__, nr, msi_hashfn(nr));
	hlist_add_head_rcu(&map->msi_chain, &msi_hash[msi_hashfn(nr)]);

	spin_lock(&msi_map_lock);
	rc = irq_set_msi_desc(nr, msi);
	if (rc) {
		spin_unlock(&msi_map_lock);
		hlist_del_rcu(&map->msi_chain);
		kfree(map);
		zdev->msi_map[nr & ZPCI_MSI_MASK] = NULL;
		return rc;
	}
	spin_unlock(&msi_map_lock);

	msg.data = nr - offset;
	msg.address_lo = zdev->msi_addr & 0xffffffff;
	msg.address_hi = zdev->msi_addr >> 32;
	write_msi_msg(nr, &msg);
	return 0;
}

void zpci_teardown_msi_irq(struct zpci_dev *zdev, struct msi_desc *msi)
{
	int irq = msi->irq & ZPCI_MSI_MASK;
	struct msi_map *map;

	msi->msg.address_lo = 0;
	msi->msg.address_hi = 0;
	msi->msg.data = 0;
	msi->irq = 0;
	zpci_msi_set_mask_bits(msi, 1, 1);

	spin_lock(&msi_map_lock);
	map = zdev->msi_map[irq];
	hlist_del_rcu(&map->msi_chain);
	kfree(map);
	zdev->msi_map[irq] = NULL;
	spin_unlock(&msi_map_lock);
}

/*
 * The msi hash table has 256 entries which is good for 4..20
 * devices (a typical device allocates 10 + CPUs MSI's). Maybe make
 * the hash table size adjustable later.
 */
int __init zpci_msihash_init(void)
{
	unsigned int i;

	msi_hash = kmalloc(256 * sizeof(*msi_hash), GFP_KERNEL);
	if (!msi_hash)
		return -ENOMEM;

	for (i = 0; i < (1U << msihash_shift); i++)
		INIT_HLIST_HEAD(&msi_hash[i]);
	return 0;
}

void __init zpci_msihash_exit(void)
{
	kfree(msi_hash);
}
