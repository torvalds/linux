
/* This header only makes send when included in a 2.0 compile */

#ifndef _PCI_COMPAT_H_
#define _PCI_COMPAT_H_

#ifdef __KERNEL__

#include <linux/bios32.h> /* pcibios_* */
#include <linux/pci.h> /* pcibios_* */
#include <linux/malloc.h> /* kmalloc */

/* fake the new pci interface based on the old one: encapsulate bus/devfn */
struct pci_fake_dev {
    u8 bus;
    u8 devfn;
    int index;
};
#define pci_dev pci_fake_dev /* the other pci_dev is unused by 2.0 drivers */

extern inline struct pci_dev *pci_find_device(unsigned int vendorid,
					      unsigned int devid,
					      struct pci_dev *from)
{
    struct pci_dev *pptr = kmalloc(sizeof(*pptr), GFP_KERNEL);
    int index = 0;
    int ret;

    if (!pptr) return NULL;
    if (from) index = pptr->index + 1;
    ret = pcibios_find_device(vendorid, devid, index,
			      &pptr->bus, &pptr->devfn);
    if (ret) { kfree(pptr); return NULL; }
    return pptr;
}

extern inline struct pci_dev *pci_find_class(unsigned int class,
					     struct pci_dev *from)
{
    return NULL; /* FIXME */
}

extern inline void pci_release_device(struct pci_dev *dev)
{
    kfree(dev);
}

/* struct pci_dev *pci_find_slot (unsigned int bus, unsigned int devfn); */

#define pci_present pcibios_present

extern inline int
pci_read_config_byte(struct pci_dev *dev, u8 where, u8 *val)
{
    return pcibios_read_config_byte(dev->bus, dev->devfn, where, val);
}

extern inline int
pci_read_config_word(struct pci_dev *dev, u8 where, u16 *val)
{
    return pcibios_read_config_word(dev->bus, dev->devfn, where, val);
}

extern inline int
pci_read_config_dword(struct pci_dev *dev, u8 where, u32 *val)
{
    return pcibios_read_config_dword(dev->bus, dev->devfn, where, val);
}

extern inline int
pci_write_config_byte(struct pci_dev *dev, u8 where, u8 val)
{
    return pcibios_write_config_byte(dev->bus, dev->devfn, where, val);
}

extern inline int
pci_write_config_word(struct pci_dev *dev, u8 where, u16 val)
{
    return pcibios_write_config_word(dev->bus, dev->devfn, where, val);
}

extern inline int
pci_write_config_dword(struct pci_dev *dev, u8 where, u32 val)
{
    return pcibios_write_config_dword(dev->bus, dev->devfn, where, val);
}

extern inline void pci_set_master(struct pci_dev *dev)
{
    u16 cmd;
    pcibios_read_config_word(dev->bus, dev->devfn, PCI_COMMAND, &cmd);
    cmd |= PCI_COMMAND_MASTER;
    pcibios_write_config_word(dev->bus, dev->devfn, PCI_COMMAND, cmd);
}

#endif /* __KERNEL__ */
#endif /* _PCI_COMPAT_H_ */
