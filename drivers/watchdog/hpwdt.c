/*
 *	HP WatchDog Driver
 *	based on
 *
 *	SoftDog	0.05:	A Software Watchdog Device
 *
 *	(c) Copyright 2007 Hewlett-Packard Development Company, L.P.
 *	Thomas Mingarelli <thomas.mingarelli@hp.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 2 as published by the Free Software Foundation
 *
 */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#ifdef CONFIG_HPWDT_NMI_DECODING
#include <linux/dmi.h>
#include <linux/spinlock.h>
#include <linux/nmi.h>
#include <linux/kdebug.h>
#include <linux/notifier.h>
#include <asm/cacheflush.h>
#endif /* CONFIG_HPWDT_NMI_DECODING */
#include <asm/nmi.h>

#define HPWDT_VERSION			"1.3.0"
#define SECS_TO_TICKS(secs)		((secs) * 1000 / 128)
#define TICKS_TO_SECS(ticks)		((ticks) * 128 / 1000)
#define HPWDT_MAX_TIMER			TICKS_TO_SECS(65535)
#define DEFAULT_MARGIN			30

static unsigned int soft_margin = DEFAULT_MARGIN;	/* in seconds */
static unsigned int reload;			/* the computed soft_margin */
static int nowayout = WATCHDOG_NOWAYOUT;
static char expect_release;
static unsigned long hpwdt_is_open;

static void __iomem *pci_mem_addr;		/* the PCI-memory address */
static unsigned long __iomem *hpwdt_timer_reg;
static unsigned long __iomem *hpwdt_timer_con;

static DEFINE_PCI_DEVICE_TABLE(hpwdt_devices) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_COMPAQ, 0xB203) },	/* iLO2 */
	{ PCI_DEVICE(PCI_VENDOR_ID_HP, 0x3306) },	/* iLO3 */
	{0},			/* terminate list */
};
MODULE_DEVICE_TABLE(pci, hpwdt_devices);

#ifdef CONFIG_HPWDT_NMI_DECODING
#define PCI_BIOS32_SD_VALUE		0x5F32335F	/* "_32_" */
#define CRU_BIOS_SIGNATURE_VALUE	0x55524324
#define PCI_BIOS32_PARAGRAPH_LEN	16
#define PCI_ROM_BASE1			0x000F0000
#define ROM_SIZE			0x10000

struct bios32_service_dir {
	u32 signature;
	u32 entry_point;
	u8 revision;
	u8 length;
	u8 checksum;
	u8 reserved[5];
};

/* type 212 */
struct smbios_cru64_info {
	u8 type;
	u8 byte_length;
	u16 handle;
	u32 signature;
	u64 physical_address;
	u32 double_length;
	u32 double_offset;
};
#define SMBIOS_CRU64_INFORMATION	212

/* type 219 */
struct smbios_proliant_info {
	u8 type;
	u8 byte_length;
	u16 handle;
	u32 power_features;
	u32 omega_features;
	u32 reserved;
	u32 misc_features;
};
#define SMBIOS_ICRU_INFORMATION		219


struct cmn_registers {
	union {
		struct {
			u8 ral;
			u8 rah;
			u16 rea2;
		};
		u32 reax;
	} u1;
	union {
		struct {
			u8 rbl;
			u8 rbh;
			u8 reb2l;
			u8 reb2h;
		};
		u32 rebx;
	} u2;
	union {
		struct {
			u8 rcl;
			u8 rch;
			u16 rec2;
		};
		u32 recx;
	} u3;
	union {
		struct {
			u8 rdl;
			u8 rdh;
			u16 red2;
		};
		u32 redx;
	} u4;

	u32 resi;
	u32 redi;
	u16 rds;
	u16 res;
	u32 reflags;
}  __attribute__((packed));

static unsigned int hpwdt_nmi_decoding;
static unsigned int allow_kdump;
static unsigned int priority;		/* hpwdt at end of die_notify list */
static unsigned int is_icru;
static DEFINE_SPINLOCK(rom_lock);
static void *cru_rom_addr;
static struct cmn_registers cmn_regs;

extern asmlinkage void asminline_call(struct cmn_registers *pi86Regs,
						unsigned long *pRomEntry);

#ifdef CONFIG_X86_32
/* --32 Bit Bios------------------------------------------------------------ */

#define HPWDT_ARCH	32

asm(".text                          \n\t"
    ".align 4                       \n"
    "asminline_call:                \n\t"
    "pushl       %ebp               \n\t"
    "movl        %esp, %ebp         \n\t"
    "pusha                          \n\t"
    "pushf                          \n\t"
    "push        %es                \n\t"
    "push        %ds                \n\t"
    "pop         %es                \n\t"
    "movl        8(%ebp),%eax       \n\t"
    "movl        4(%eax),%ebx       \n\t"
    "movl        8(%eax),%ecx       \n\t"
    "movl        12(%eax),%edx      \n\t"
    "movl        16(%eax),%esi      \n\t"
    "movl        20(%eax),%edi      \n\t"
    "movl        (%eax),%eax        \n\t"
    "push        %cs                \n\t"
    "call        *12(%ebp)          \n\t"
    "pushf                          \n\t"
    "pushl       %eax               \n\t"
    "movl        8(%ebp),%eax       \n\t"
    "movl        %ebx,4(%eax)       \n\t"
    "movl        %ecx,8(%eax)       \n\t"
    "movl        %edx,12(%eax)      \n\t"
    "movl        %esi,16(%eax)      \n\t"
    "movl        %edi,20(%eax)      \n\t"
    "movw        %ds,24(%eax)       \n\t"
    "movw        %es,26(%eax)       \n\t"
    "popl        %ebx               \n\t"
    "movl        %ebx,(%eax)        \n\t"
    "popl        %ebx               \n\t"
    "movl        %ebx,28(%eax)      \n\t"
    "pop         %es                \n\t"
    "popf                           \n\t"
    "popa                           \n\t"
    "leave                          \n\t"
    "ret                            \n\t"
    ".previous");


/*
 *	cru_detect
 *
 *	Routine Description:
 *	This function uses the 32-bit BIOS Service Directory record to
 *	search for a $CRU record.
 *
 *	Return Value:
 *	0        :  SUCCESS
 *	<0       :  FAILURE
 */
static int __devinit cru_detect(unsigned long map_entry,
	unsigned long map_offset)
{
	void *bios32_map;
	unsigned long *bios32_entrypoint;
	unsigned long cru_physical_address;
	unsigned long cru_length;
	unsigned long physical_bios_base = 0;
	unsigned long physical_bios_offset = 0;
	int retval = -ENODEV;

	bios32_map = ioremap(map_entry, (2 * PAGE_SIZE));

	if (bios32_map == NULL)
		return -ENODEV;

	bios32_entrypoint = bios32_map + map_offset;

	cmn_regs.u1.reax = CRU_BIOS_SIGNATURE_VALUE;

	asminline_call(&cmn_regs, bios32_entrypoint);

	if (cmn_regs.u1.ral != 0) {
		printk(KERN_WARNING
			"hpwdt: Call succeeded but with an error: 0x%x\n",
			cmn_regs.u1.ral);
	} else {
		physical_bios_base = cmn_regs.u2.rebx;
		physical_bios_offset = cmn_regs.u4.redx;
		cru_length = cmn_regs.u3.recx;
		cru_physical_address =
			physical_bios_base + physical_bios_offset;

		/* If the values look OK, then map it in. */
		if ((physical_bios_base + physical_bios_offset)) {
			cru_rom_addr =
				ioremap(cru_physical_address, cru_length);
			if (cru_rom_addr)
				retval = 0;
		}

		printk(KERN_DEBUG "hpwdt: CRU Base Address:   0x%lx\n",
			physical_bios_base);
		printk(KERN_DEBUG "hpwdt: CRU Offset Address: 0x%lx\n",
			physical_bios_offset);
		printk(KERN_DEBUG "hpwdt: CRU Length:         0x%lx\n",
			cru_length);
		printk(KERN_DEBUG "hpwdt: CRU Mapped Address: %p\n",
			&cru_rom_addr);
	}
	iounmap(bios32_map);
	return retval;
}

/*
 *	bios_checksum
 */
static int __devinit bios_checksum(const char __iomem *ptr, int len)
{
	char sum = 0;
	int i;

	/*
	 * calculate checksum of size bytes. This should add up
	 * to zero if we have a valid header.
	 */
	for (i = 0; i < len; i++)
		sum += ptr[i];

	return ((sum == 0) && (len > 0));
}

/*
 *	bios32_present
 *
 *	Routine Description:
 *	This function finds the 32-bit BIOS Service Directory
 *
 *	Return Value:
 *	0        :  SUCCESS
 *	<0       :  FAILURE
 */
static int __devinit bios32_present(const char __iomem *p)
{
	struct bios32_service_dir *bios_32_ptr;
	int length;
	unsigned long map_entry, map_offset;

	bios_32_ptr = (struct bios32_service_dir *) p;

	/*
	 * Search for signature by checking equal to the swizzled value
	 * instead of calling another routine to perform a strcmp.
	 */
	if (bios_32_ptr->signature == PCI_BIOS32_SD_VALUE) {
		length = bios_32_ptr->length * PCI_BIOS32_PARAGRAPH_LEN;
		if (bios_checksum(p, length)) {
			/*
			 * According to the spec, we're looking for the
			 * first 4KB-aligned address below the entrypoint
			 * listed in the header. The Service Directory code
			 * is guaranteed to occupy no more than 2 4KB pages.
			 */
			map_entry = bios_32_ptr->entry_point & ~(PAGE_SIZE - 1);
			map_offset = bios_32_ptr->entry_point - map_entry;

			return cru_detect(map_entry, map_offset);
		}
	}
	return -ENODEV;
}

static int __devinit detect_cru_service(void)
{
	char __iomem *p, *q;
	int rc = -1;

	/*
	 * Search from 0x0f0000 through 0x0fffff, inclusive.
	 */
	p = ioremap(PCI_ROM_BASE1, ROM_SIZE);
	if (p == NULL)
		return -ENOMEM;

	for (q = p; q < p + ROM_SIZE; q += 16) {
		rc = bios32_present(q);
		if (!rc)
			break;
	}
	iounmap(p);
	return rc;
}
/* ------------------------------------------------------------------------- */
#endif /* CONFIG_X86_32 */
#ifdef CONFIG_X86_64
/* --64 Bit Bios------------------------------------------------------------ */

#define HPWDT_ARCH	64

asm(".text                      \n\t"
    ".align 4                   \n"
    "asminline_call:            \n\t"
    "pushq      %rbp            \n\t"
    "movq       %rsp, %rbp      \n\t"
    "pushq      %rax            \n\t"
    "pushq      %rbx            \n\t"
    "pushq      %rdx            \n\t"
    "pushq      %r12            \n\t"
    "pushq      %r9             \n\t"
    "movq       %rsi, %r12      \n\t"
    "movq       %rdi, %r9       \n\t"
    "movl       4(%r9),%ebx     \n\t"
    "movl       8(%r9),%ecx     \n\t"
    "movl       12(%r9),%edx    \n\t"
    "movl       16(%r9),%esi    \n\t"
    "movl       20(%r9),%edi    \n\t"
    "movl       (%r9),%eax      \n\t"
    "call       *%r12           \n\t"
    "pushfq                     \n\t"
    "popq        %r12           \n\t"
    "movl       %eax, (%r9)     \n\t"
    "movl       %ebx, 4(%r9)    \n\t"
    "movl       %ecx, 8(%r9)    \n\t"
    "movl       %edx, 12(%r9)   \n\t"
    "movl       %esi, 16(%r9)   \n\t"
    "movl       %edi, 20(%r9)   \n\t"
    "movq       %r12, %rax      \n\t"
    "movl       %eax, 28(%r9)   \n\t"
    "popq       %r9             \n\t"
    "popq       %r12            \n\t"
    "popq       %rdx            \n\t"
    "popq       %rbx            \n\t"
    "popq       %rax            \n\t"
    "leave                      \n\t"
    "ret                        \n\t"
    ".previous");

/*
 *	dmi_find_cru
 *
 *	Routine Description:
 *	This function checks whether or not a SMBIOS/DMI record is
 *	the 64bit CRU info or not
 */
static void __devinit dmi_find_cru(const struct dmi_header *dm, void *dummy)
{
	struct smbios_cru64_info *smbios_cru64_ptr;
	unsigned long cru_physical_address;

	if (dm->type == SMBIOS_CRU64_INFORMATION) {
		smbios_cru64_ptr = (struct smbios_cru64_info *) dm;
		if (smbios_cru64_ptr->signature == CRU_BIOS_SIGNATURE_VALUE) {
			cru_physical_address =
				smbios_cru64_ptr->physical_address +
				smbios_cru64_ptr->double_offset;
			cru_rom_addr = ioremap(cru_physical_address,
				smbios_cru64_ptr->double_length);
			set_memory_x((unsigned long)cru_rom_addr & PAGE_MASK,
				smbios_cru64_ptr->double_length >> PAGE_SHIFT);
		}
	}
}

static int __devinit detect_cru_service(void)
{
	cru_rom_addr = NULL;

	dmi_walk(dmi_find_cru, NULL);

	/* if cru_rom_addr has been set then we found a CRU service */
	return ((cru_rom_addr != NULL) ? 0 : -ENODEV);
}
/* ------------------------------------------------------------------------- */
#endif /* CONFIG_X86_64 */
#endif /* CONFIG_HPWDT_NMI_DECODING */

/*
 *	Watchdog operations
 */
static void hpwdt_start(void)
{
	reload = SECS_TO_TICKS(soft_margin);
	iowrite16(reload, hpwdt_timer_reg);
	iowrite16(0x85, hpwdt_timer_con);
}

static void hpwdt_stop(void)
{
	unsigned long data;

	data = ioread16(hpwdt_timer_con);
	data &= 0xFE;
	iowrite16(data, hpwdt_timer_con);
}

static void hpwdt_ping(void)
{
	iowrite16(reload, hpwdt_timer_reg);
}

static int hpwdt_change_timer(int new_margin)
{
	if (new_margin < 1 || new_margin > HPWDT_MAX_TIMER) {
		printk(KERN_WARNING
			"hpwdt: New value passed in is invalid: %d seconds.\n",
			new_margin);
		return -EINVAL;
	}

	soft_margin = new_margin;
	printk(KERN_DEBUG
		"hpwdt: New timer passed in is %d seconds.\n",
		new_margin);
	reload = SECS_TO_TICKS(soft_margin);

	return 0;
}

static int hpwdt_time_left(void)
{
	return TICKS_TO_SECS(ioread16(hpwdt_timer_reg));
}

#ifdef CONFIG_HPWDT_NMI_DECODING
/*
 *	NMI Handler
 */
static int hpwdt_pretimeout(unsigned int ulReason, struct pt_regs *regs)
{
	unsigned long rom_pl;
	static int die_nmi_called;

	if (!hpwdt_nmi_decoding)
		goto out;

	spin_lock_irqsave(&rom_lock, rom_pl);
	if (!die_nmi_called && !is_icru)
		asminline_call(&cmn_regs, cru_rom_addr);
	die_nmi_called = 1;
	spin_unlock_irqrestore(&rom_lock, rom_pl);

	if (allow_kdump)
		hpwdt_stop();

	if (!is_icru) {
		if (cmn_regs.u1.ral == 0) {
			panic("An NMI occurred, "
				"but unable to determine source.\n");
		}
	}
	panic("An NMI occurred, please see the Integrated "
		"Management Log for details.\n");

out:
	return NMI_DONE;
}
#endif /* CONFIG_HPWDT_NMI_DECODING */

/*
 *	/dev/watchdog handling
 */
static int hpwdt_open(struct inode *inode, struct file *file)
{
	/* /dev/watchdog can only be opened once */
	if (test_and_set_bit(0, &hpwdt_is_open))
		return -EBUSY;

	/* Start the watchdog */
	hpwdt_start();
	hpwdt_ping();

	return nonseekable_open(inode, file);
}

static int hpwdt_release(struct inode *inode, struct file *file)
{
	/* Stop the watchdog */
	if (expect_release == 42) {
		hpwdt_stop();
	} else {
		printk(KERN_CRIT
			"hpwdt: Unexpected close, not stopping watchdog!\n");
		hpwdt_ping();
	}

	expect_release = 0;

	/* /dev/watchdog is being closed, make sure it can be re-opened */
	clear_bit(0, &hpwdt_is_open);

	return 0;
}

static ssize_t hpwdt_write(struct file *file, const char __user *data,
	size_t len, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (len) {
		if (!nowayout) {
			size_t i;

			/* note: just in case someone wrote the magic character
			 * five months ago... */
			expect_release = 0;

			/* scan to see whether or not we got the magic char. */
			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_release = 42;
			}
		}

		/* someone wrote to us, we should reload the timer */
		hpwdt_ping();
	}

	return len;
}

static const struct watchdog_info ident = {
	.options = WDIOF_SETTIMEOUT |
		   WDIOF_KEEPALIVEPING |
		   WDIOF_MAGICCLOSE,
	.identity = "HP iLO2+ HW Watchdog Timer",
};

static long hpwdt_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_margin;
	int ret = -ENOTTY;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = 0;
		if (copy_to_user(argp, &ident, sizeof(ident)))
			ret = -EFAULT;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		ret = put_user(0, p);
		break;

	case WDIOC_KEEPALIVE:
		hpwdt_ping();
		ret = 0;
		break;

	case WDIOC_SETTIMEOUT:
		ret = get_user(new_margin, p);
		if (ret)
			break;

		ret = hpwdt_change_timer(new_margin);
		if (ret)
			break;

		hpwdt_ping();
		/* Fall */
	case WDIOC_GETTIMEOUT:
		ret = put_user(soft_margin, p);
		break;

	case WDIOC_GETTIMELEFT:
		ret = put_user(hpwdt_time_left(), p);
		break;
	}
	return ret;
}

/*
 *	Kernel interfaces
 */
static const struct file_operations hpwdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = hpwdt_write,
	.unlocked_ioctl = hpwdt_ioctl,
	.open = hpwdt_open,
	.release = hpwdt_release,
};

static struct miscdevice hpwdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &hpwdt_fops,
};

/*
 *	Init & Exit
 */

#ifdef CONFIG_HPWDT_NMI_DECODING
#ifdef CONFIG_X86_LOCAL_APIC
static void __devinit hpwdt_check_nmi_decoding(struct pci_dev *dev)
{
	/*
	 * If nmi_watchdog is turned off then we can turn on
	 * our nmi decoding capability.
	 */
	hpwdt_nmi_decoding = 1;
}
#else
static void __devinit hpwdt_check_nmi_decoding(struct pci_dev *dev)
{
	dev_warn(&dev->dev, "NMI decoding is disabled. "
		"Your kernel does not support a NMI Watchdog.\n");
}
#endif /* CONFIG_X86_LOCAL_APIC */

/*
 *	dmi_find_icru
 *
 *	Routine Description:
 *	This function checks whether or not we are on an iCRU-based server.
 *	This check is independent of architecture and needs to be made for
 *	any ProLiant system.
 */
static void __devinit dmi_find_icru(const struct dmi_header *dm, void *dummy)
{
	struct smbios_proliant_info *smbios_proliant_ptr;

	if (dm->type == SMBIOS_ICRU_INFORMATION) {
		smbios_proliant_ptr = (struct smbios_proliant_info *) dm;
		if (smbios_proliant_ptr->misc_features & 0x01)
			is_icru = 1;
	}
}

static int __devinit hpwdt_init_nmi_decoding(struct pci_dev *dev)
{
	int retval;

	/*
	 * On typical CRU-based systems we need to map that service in
	 * the BIOS. For 32 bit Operating Systems we need to go through
	 * the 32 Bit BIOS Service Directory. For 64 bit Operating
	 * Systems we get that service through SMBIOS.
	 *
	 * On systems that support the new iCRU service all we need to
	 * do is call dmi_walk to get the supported flag value and skip
	 * the old cru detect code.
	 */
	dmi_walk(dmi_find_icru, NULL);
	if (!is_icru) {

		/*
		* We need to map the ROM to get the CRU service.
		* For 32 bit Operating Systems we need to go through the 32 Bit
		* BIOS Service Directory
		* For 64 bit Operating Systems we get that service through SMBIOS.
		*/
		retval = detect_cru_service();
		if (retval < 0) {
			dev_warn(&dev->dev,
				"Unable to detect the %d Bit CRU Service.\n",
				HPWDT_ARCH);
			return retval;
		}

		/*
		* We know this is the only CRU call we need to make so lets keep as
		* few instructions as possible once the NMI comes in.
		*/
		cmn_regs.u1.rah = 0x0D;
		cmn_regs.u1.ral = 0x02;
	}

	/*
	 * If the priority is set to 1, then we will be put first on the
	 * die notify list to handle a critical NMI. The default is to
	 * be last so other users of the NMI signal can function.
	 */
	retval = register_nmi_handler(NMI_UNKNOWN, hpwdt_pretimeout,
					(priority) ? NMI_FLAG_FIRST : 0,
					"hpwdt");
	if (retval != 0) {
		dev_warn(&dev->dev,
			"Unable to register a die notifier (err=%d).\n",
			retval);
		if (cru_rom_addr)
			iounmap(cru_rom_addr);
	}

	dev_info(&dev->dev,
			"HP Watchdog Timer Driver: NMI decoding initialized"
			", allow kernel dump: %s (default = 0/OFF)"
			", priority: %s (default = 0/LAST).\n",
			(allow_kdump == 0) ? "OFF" : "ON",
			(priority == 0) ? "LAST" : "FIRST");
	return 0;
}

static void hpwdt_exit_nmi_decoding(void)
{
	unregister_nmi_handler(NMI_UNKNOWN, "hpwdt");
	if (cru_rom_addr)
		iounmap(cru_rom_addr);
}
#else /* !CONFIG_HPWDT_NMI_DECODING */
static void __devinit hpwdt_check_nmi_decoding(struct pci_dev *dev)
{
}

static int __devinit hpwdt_init_nmi_decoding(struct pci_dev *dev)
{
	return 0;
}

static void hpwdt_exit_nmi_decoding(void)
{
}
#endif /* CONFIG_HPWDT_NMI_DECODING */

static int __devinit hpwdt_init_one(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	int retval;

	/*
	 * Check if we can do NMI decoding or not
	 */
	hpwdt_check_nmi_decoding(dev);

	/*
	 * First let's find out if we are on an iLO2+ server. We will
	 * not run on a legacy ASM box.
	 * So we only support the G5 ProLiant servers and higher.
	 */
	if (dev->subsystem_vendor != PCI_VENDOR_ID_HP) {
		dev_warn(&dev->dev,
			"This server does not have an iLO2+ ASIC.\n");
		return -ENODEV;
	}

	if (pci_enable_device(dev)) {
		dev_warn(&dev->dev,
			"Not possible to enable PCI Device: 0x%x:0x%x.\n",
			ent->vendor, ent->device);
		return -ENODEV;
	}

	pci_mem_addr = pci_iomap(dev, 1, 0x80);
	if (!pci_mem_addr) {
		dev_warn(&dev->dev,
			"Unable to detect the iLO2+ server memory.\n");
		retval = -ENOMEM;
		goto error_pci_iomap;
	}
	hpwdt_timer_reg = pci_mem_addr + 0x70;
	hpwdt_timer_con = pci_mem_addr + 0x72;

	/* Make sure that we have a valid soft_margin */
	if (hpwdt_change_timer(soft_margin))
		hpwdt_change_timer(DEFAULT_MARGIN);

	/* Initialize NMI Decoding functionality */
	retval = hpwdt_init_nmi_decoding(dev);
	if (retval != 0)
		goto error_init_nmi_decoding;

	retval = misc_register(&hpwdt_miscdev);
	if (retval < 0) {
		dev_warn(&dev->dev,
			"Unable to register miscdev on minor=%d (err=%d).\n",
			WATCHDOG_MINOR, retval);
		goto error_misc_register;
	}

	dev_info(&dev->dev, "HP Watchdog Timer Driver: %s"
			", timer margin: %d seconds (nowayout=%d).\n",
			HPWDT_VERSION, soft_margin, nowayout);
	return 0;

error_misc_register:
	hpwdt_exit_nmi_decoding();
error_init_nmi_decoding:
	pci_iounmap(dev, pci_mem_addr);
error_pci_iomap:
	pci_disable_device(dev);
	return retval;
}

static void __devexit hpwdt_exit(struct pci_dev *dev)
{
	if (!nowayout)
		hpwdt_stop();

	misc_deregister(&hpwdt_miscdev);
	hpwdt_exit_nmi_decoding();
	pci_iounmap(dev, pci_mem_addr);
	pci_disable_device(dev);
}

static struct pci_driver hpwdt_driver = {
	.name = "hpwdt",
	.id_table = hpwdt_devices,
	.probe = hpwdt_init_one,
	.remove = __devexit_p(hpwdt_exit),
};

static void __exit hpwdt_cleanup(void)
{
	pci_unregister_driver(&hpwdt_driver);
}

static int __init hpwdt_init(void)
{
	return pci_register_driver(&hpwdt_driver);
}

MODULE_AUTHOR("Tom Mingarelli");
MODULE_DESCRIPTION("hp watchdog driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(HPWDT_VERSION);
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

module_param(soft_margin, int, 0);
MODULE_PARM_DESC(soft_margin, "Watchdog timeout in seconds");

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

#ifdef CONFIG_HPWDT_NMI_DECODING
module_param(allow_kdump, int, 0);
MODULE_PARM_DESC(allow_kdump, "Start a kernel dump after NMI occurs");

module_param(priority, int, 0);
MODULE_PARM_DESC(priority, "The hpwdt driver handles NMIs first or last"
		" (default = 0/Last)\n");
#endif /* !CONFIG_HPWDT_NMI_DECODING */

module_init(hpwdt_init);
module_exit(hpwdt_cleanup);
