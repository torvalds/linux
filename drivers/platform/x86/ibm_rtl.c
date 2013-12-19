/*
 * IBM Real-Time Linux driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2010
 *
 * Author: Keith Mannthey <kmannth@us.ibm.com>
 *         Vernon Mauery <vernux@us.ibm.com>
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/dmi.h>
#include <linux/efi.h>
#include <linux/mutex.h>
#include <asm/bios_ebda.h>

#include <asm-generic/io-64-nonatomic-lo-hi.h>

static bool force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Force driver load, ignore DMI data");

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Show debug output");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Keith Mannthey <kmmanth@us.ibm.com>");
MODULE_AUTHOR("Vernon Mauery <vernux@us.ibm.com>");

#define RTL_ADDR_TYPE_IO    1
#define RTL_ADDR_TYPE_MMIO  2

#define RTL_CMD_ENTER_PRTM  1
#define RTL_CMD_EXIT_PRTM   2

/* The RTL table as presented by the EBDA: */
struct ibm_rtl_table {
	char signature[5]; /* signature should be "_RTL_" */
	u8 version;
	u8 rt_status;
	u8 command;
	u8 command_status;
	u8 cmd_address_type;
	u8 cmd_granularity;
	u8 cmd_offset;
	u16 reserve1;
	u32 cmd_port_address; /* platform dependent address */
	u32 cmd_port_value;   /* platform dependent value */
} __attribute__((packed));

/* to locate "_RTL_" signature do a masked 5-byte integer compare */
#define RTL_SIGNATURE 0x0000005f4c54525fULL
#define RTL_MASK      0x000000ffffffffffULL

#define RTL_DEBUG(fmt, ...)				\
do {							\
	if (debug)					\
		pr_info(fmt, ##__VA_ARGS__);		\
} while (0)

static DEFINE_MUTEX(rtl_lock);
static struct ibm_rtl_table __iomem *rtl_table;
static void __iomem *ebda_map;
static void __iomem *rtl_cmd_addr;
static u8 rtl_cmd_type;
static u8 rtl_cmd_width;

static void __iomem *rtl_port_map(phys_addr_t addr, unsigned long len)
{
	if (rtl_cmd_type == RTL_ADDR_TYPE_MMIO)
		return ioremap(addr, len);
	return ioport_map(addr, len);
}

static void rtl_port_unmap(void __iomem *addr)
{
	if (addr && rtl_cmd_type == RTL_ADDR_TYPE_MMIO)
		iounmap(addr);
	else
		ioport_unmap(addr);
}

static int ibm_rtl_write(u8 value)
{
	int ret = 0, count = 0;
	static u32 cmd_port_val;

	RTL_DEBUG("%s(%d)\n", __func__, value);

	value = value == 1 ? RTL_CMD_ENTER_PRTM : RTL_CMD_EXIT_PRTM;

	mutex_lock(&rtl_lock);

	if (ioread8(&rtl_table->rt_status) != value) {
		iowrite8(value, &rtl_table->command);

		switch (rtl_cmd_width) {
		case 8:
			cmd_port_val = ioread8(&rtl_table->cmd_port_value);
			RTL_DEBUG("cmd_port_val = %u\n", cmd_port_val);
			iowrite8((u8)cmd_port_val, rtl_cmd_addr);
			break;
		case 16:
			cmd_port_val = ioread16(&rtl_table->cmd_port_value);
			RTL_DEBUG("cmd_port_val = %u\n", cmd_port_val);
			iowrite16((u16)cmd_port_val, rtl_cmd_addr);
			break;
		case 32:
			cmd_port_val = ioread32(&rtl_table->cmd_port_value);
			RTL_DEBUG("cmd_port_val = %u\n", cmd_port_val);
			iowrite32(cmd_port_val, rtl_cmd_addr);
			break;
		}

		while (ioread8(&rtl_table->command)) {
			msleep(10);
			if (count++ > 500) {
				pr_err("Hardware not responding to "
				       "mode switch request\n");
				ret = -EIO;
				break;
			}

		}

		if (ioread8(&rtl_table->command_status)) {
			RTL_DEBUG("command_status reports failed command\n");
			ret = -EIO;
		}
	}

	mutex_unlock(&rtl_lock);
	return ret;
}

static ssize_t rtl_show_version(struct device *dev,
                                struct device_attribute *attr,
                                char *buf)
{
	return sprintf(buf, "%d\n", (int)ioread8(&rtl_table->version));
}

static ssize_t rtl_show_state(struct device *dev,
                              struct device_attribute *attr,
                              char *buf)
{
	return sprintf(buf, "%d\n", ioread8(&rtl_table->rt_status));
}

static ssize_t rtl_set_state(struct device *dev,
                             struct device_attribute *attr,
                             const char *buf,
                             size_t count)
{
	ssize_t ret;

	if (count < 1 || count > 2)
		return -EINVAL;

	switch (buf[0]) {
	case '0':
		ret = ibm_rtl_write(0);
		break;
	case '1':
		ret = ibm_rtl_write(1);
		break;
	default:
		ret = -EINVAL;
	}
	if (ret >= 0)
		ret = count;

	return ret;
}

static struct bus_type rtl_subsys = {
	.name = "ibm_rtl",
	.dev_name = "ibm_rtl",
};

static DEVICE_ATTR(version, S_IRUGO, rtl_show_version, NULL);
static DEVICE_ATTR(state, 0600, rtl_show_state, rtl_set_state);

static struct device_attribute *rtl_attributes[] = {
	&dev_attr_version,
	&dev_attr_state,
	NULL
};


static int rtl_setup_sysfs(void) {
	int ret, i;

	ret = subsys_system_register(&rtl_subsys, NULL);
	if (!ret) {
		for (i = 0; rtl_attributes[i]; i ++)
			device_create_file(rtl_subsys.dev_root, rtl_attributes[i]);
	}
	return ret;
}

static void rtl_teardown_sysfs(void) {
	int i;
	for (i = 0; rtl_attributes[i]; i ++)
		device_remove_file(rtl_subsys.dev_root, rtl_attributes[i]);
	bus_unregister(&rtl_subsys);
}


static struct dmi_system_id __initdata ibm_rtl_dmi_table[] = {
	{                                                  \
		.matches = {                               \
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),  \
		},                                         \
	},
	{ }
};

static int __init ibm_rtl_init(void) {
	unsigned long ebda_addr, ebda_size;
	unsigned int ebda_kb;
	int ret = -ENODEV, i;

	if (force)
		pr_warn("module loaded by force\n");
	/* first ensure that we are running on IBM HW */
	else if (efi_enabled(EFI_BOOT) || !dmi_check_system(ibm_rtl_dmi_table))
		return -ENODEV;

	/* Get the address for the Extended BIOS Data Area */
	ebda_addr = get_bios_ebda();
	if (!ebda_addr) {
		RTL_DEBUG("no BIOS EBDA found\n");
		return -ENODEV;
	}

	ebda_map = ioremap(ebda_addr, 4);
	if (!ebda_map)
		return -ENOMEM;

	/* First word in the EDBA is the Size in KB */
	ebda_kb = ioread16(ebda_map);
	RTL_DEBUG("EBDA is %d kB\n", ebda_kb);

	if (ebda_kb == 0)
		goto out;

	iounmap(ebda_map);
	ebda_size = ebda_kb*1024;

	/* Remap the whole table */
	ebda_map = ioremap(ebda_addr, ebda_size);
	if (!ebda_map)
		return -ENOMEM;

	/* search for the _RTL_ signature at the start of the table */
	for (i = 0 ; i < ebda_size/sizeof(unsigned int); i++) {
		struct ibm_rtl_table __iomem * tmp;
		tmp = (struct ibm_rtl_table __iomem *) (ebda_map+i);
		if ((readq(&tmp->signature) & RTL_MASK) == RTL_SIGNATURE) {
			phys_addr_t addr;
			unsigned int plen;
			RTL_DEBUG("found RTL_SIGNATURE at %p\n", tmp);
			rtl_table = tmp;
			/* The address, value, width and offset are platform
			 * dependent and found in the ibm_rtl_table */
			rtl_cmd_width = ioread8(&rtl_table->cmd_granularity);
			rtl_cmd_type = ioread8(&rtl_table->cmd_address_type);
			RTL_DEBUG("rtl_cmd_width = %u, rtl_cmd_type = %u\n",
				  rtl_cmd_width, rtl_cmd_type);
			addr = ioread32(&rtl_table->cmd_port_address);
			RTL_DEBUG("addr = %#llx\n", (unsigned long long)addr);
			plen = rtl_cmd_width/sizeof(char);
			rtl_cmd_addr = rtl_port_map(addr, plen);
			RTL_DEBUG("rtl_cmd_addr = %p\n", rtl_cmd_addr);
			if (!rtl_cmd_addr) {
				ret = -ENOMEM;
				break;
			}
			ret = rtl_setup_sysfs();
			break;
		}
	}

out:
	if (ret) {
		iounmap(ebda_map);
		rtl_port_unmap(rtl_cmd_addr);
	}

	return ret;
}

static void __exit ibm_rtl_exit(void)
{
	if (rtl_table) {
		RTL_DEBUG("cleaning up");
		/* do not leave the machine in SMI-free mode */
		ibm_rtl_write(0);
		/* unmap, unlink and remove all traces */
		rtl_teardown_sysfs();
		iounmap(ebda_map);
		rtl_port_unmap(rtl_cmd_addr);
	}
}

module_init(ibm_rtl_init);
module_exit(ibm_rtl_exit);
