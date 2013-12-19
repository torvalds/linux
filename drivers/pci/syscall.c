/*
 *	pci_syscall.c
 *
 * For architectures where we want to allow direct access
 * to the PCI config stuff - it would probably be preferable
 * on PCs too, but there people just do it by hand with the
 * magic northbridge registers..
 */

#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/syscalls.h>
#include <asm/uaccess.h>
#include "pci.h"

SYSCALL_DEFINE5(pciconfig_read, unsigned long, bus, unsigned long, dfn,
		unsigned long, off, unsigned long, len, void __user *, buf)
{
	struct pci_dev *dev;
	u8 byte;
	u16 word;
	u32 dword;
	long err;
	long cfg_ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	err = -ENODEV;
	dev = pci_get_bus_and_slot(bus, dfn);
	if (!dev)
		goto error;

	switch (len) {
	case 1:
		cfg_ret = pci_user_read_config_byte(dev, off, &byte);
		break;
	case 2:
		cfg_ret = pci_user_read_config_word(dev, off, &word);
		break;
	case 4:
		cfg_ret = pci_user_read_config_dword(dev, off, &dword);
		break;
	default:
		err = -EINVAL;
		goto error;
	}

	err = -EIO;
	if (cfg_ret != PCIBIOS_SUCCESSFUL)
		goto error;

	switch (len) {
	case 1:
		err = put_user(byte, (unsigned char __user *)buf);
		break;
	case 2:
		err = put_user(word, (unsigned short __user *)buf);
		break;
	case 4:
		err = put_user(dword, (unsigned int __user *)buf);
		break;
	}
	pci_dev_put(dev);
	return err;

error:
	/* ??? XFree86 doesn't even check the return value.  They
	   just look for 0xffffffff in the output, since that's what
	   they get instead of a machine check on x86.  */
	switch (len) {
	case 1:
		put_user(-1, (unsigned char __user *)buf);
		break;
	case 2:
		put_user(-1, (unsigned short __user *)buf);
		break;
	case 4:
		put_user(-1, (unsigned int __user *)buf);
		break;
	}
	pci_dev_put(dev);
	return err;
}

SYSCALL_DEFINE5(pciconfig_write, unsigned long, bus, unsigned long, dfn,
		unsigned long, off, unsigned long, len, void __user *, buf)
{
	struct pci_dev *dev;
	u8 byte;
	u16 word;
	u32 dword;
	int err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	dev = pci_get_bus_and_slot(bus, dfn);
	if (!dev)
		return -ENODEV;

	switch(len) {
	case 1:
		err = get_user(byte, (u8 __user *)buf);
		if (err)
			break;
		err = pci_user_write_config_byte(dev, off, byte);
		if (err != PCIBIOS_SUCCESSFUL)
			err = -EIO;
		break;

	case 2:
		err = get_user(word, (u16 __user *)buf);
		if (err)
			break;
		err = pci_user_write_config_word(dev, off, word);
		if (err != PCIBIOS_SUCCESSFUL)
			err = -EIO;
		break;

	case 4:
		err = get_user(dword, (u32 __user *)buf);
		if (err)
			break;
		err = pci_user_write_config_dword(dev, off, dword);
		if (err != PCIBIOS_SUCCESSFUL)
			err = -EIO;
		break;

	default:
		err = -EINVAL;
		break;
	}
	pci_dev_put(dev);
	return err;
}
