/*
 * /proc/bus/pnp interface for Plug and Play devices
 *
 * Written by David Hinds, dahinds@users.sourceforge.net
 * Modified by Thomas Hood
 *
 * The .../devices and .../<node> and .../boot/<node> files are
 * utilized by the lspnp and setpnp utilities, supplied with the
 * pcmcia-cs package.
 *     http://pcmcia-cs.sourceforge.net
 *
 * The .../escd file is utilized by the lsescd utility written by
 * Gunther Mayer.
 *     http://home.t-online.de/home/gunther.mayer/lsescd
 *
 * The .../legacy_device_resources file is not used yet.
 *
 * The other files are human-readable.
 */

//#include <pcmcia/config.h>
//#include <pcmcia/k_compat.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/pnpbios.h>
#include <linux/init.h>

#include <asm/uaccess.h>

#include "pnpbios.h"

static struct proc_dir_entry *proc_pnp = NULL;
static struct proc_dir_entry *proc_pnp_boot = NULL;

static int proc_read_pnpconfig(char *buf, char **start, off_t pos,
			       int count, int *eof, void *data)
{
	struct pnp_isa_config_struc pnps;

	if (pnp_bios_isapnp_config(&pnps))
		return -EIO;
	return snprintf(buf, count,
			"structure_revision %d\n"
			"number_of_CSNs %d\n"
			"ISA_read_data_port 0x%x\n",
			pnps.revision, pnps.no_csns, pnps.isa_rd_data_port);
}

static int proc_read_escdinfo(char *buf, char **start, off_t pos,
			      int count, int *eof, void *data)
{
	struct escd_info_struc escd;

	if (pnp_bios_escd_info(&escd))
		return -EIO;
	return snprintf(buf, count,
			"min_ESCD_write_size %d\n"
			"ESCD_size %d\n"
			"NVRAM_base 0x%x\n",
			escd.min_escd_write_size,
			escd.escd_size, escd.nv_storage_base);
}

#define MAX_SANE_ESCD_SIZE (32*1024)
static int proc_read_escd(char *buf, char **start, off_t pos,
			  int count, int *eof, void *data)
{
	struct escd_info_struc escd;
	char *tmpbuf;
	int escd_size, escd_left_to_read, n;

	if (pnp_bios_escd_info(&escd))
		return -EIO;

	/* sanity check */
	if (escd.escd_size > MAX_SANE_ESCD_SIZE) {
		printk(KERN_ERR
		       "PnPBIOS: proc_read_escd: ESCD size reported by BIOS escd_info call is too great\n");
		return -EFBIG;
	}

	tmpbuf = kzalloc(escd.escd_size, GFP_KERNEL);
	if (!tmpbuf)
		return -ENOMEM;

	if (pnp_bios_read_escd(tmpbuf, escd.nv_storage_base)) {
		kfree(tmpbuf);
		return -EIO;
	}

	escd_size =
	    (unsigned char)(tmpbuf[0]) + (unsigned char)(tmpbuf[1]) * 256;

	/* sanity check */
	if (escd_size > MAX_SANE_ESCD_SIZE) {
		printk(KERN_ERR
		       "PnPBIOS: proc_read_escd: ESCD size reported by BIOS read_escd call is too great\n");
		return -EFBIG;
	}

	escd_left_to_read = escd_size - pos;
	if (escd_left_to_read < 0)
		escd_left_to_read = 0;
	if (escd_left_to_read == 0)
		*eof = 1;
	n = min(count, escd_left_to_read);
	memcpy(buf, tmpbuf + pos, n);
	kfree(tmpbuf);
	*start = buf;
	return n;
}

static int proc_read_legacyres(char *buf, char **start, off_t pos,
			       int count, int *eof, void *data)
{
	/* Assume that the following won't overflow the buffer */
	if (pnp_bios_get_stat_res(buf))
		return -EIO;

	return count;		// FIXME: Return actual length
}

static int proc_read_devices(char *buf, char **start, off_t pos,
			     int count, int *eof, void *data)
{
	struct pnp_bios_node *node;
	u8 nodenum;
	char *p = buf;

	if (pos >= 0xff)
		return 0;

	node = kzalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	for (nodenum = pos; nodenum < 0xff;) {
		u8 thisnodenum = nodenum;
		/* 26 = the number of characters per line sprintf'ed */
		if ((p - buf + 26) > count)
			break;
		if (pnp_bios_get_dev_node(&nodenum, PNPMODE_DYNAMIC, node))
			break;
		p += sprintf(p, "%02x\t%08x\t%02x:%02x:%02x\t%04x\n",
			     node->handle, node->eisa_id,
			     node->type_code[0], node->type_code[1],
			     node->type_code[2], node->flags);
		if (nodenum <= thisnodenum) {
			printk(KERN_ERR
			       "%s Node number 0x%x is out of sequence following node 0x%x. Aborting.\n",
			       "PnPBIOS: proc_read_devices:",
			       (unsigned int)nodenum,
			       (unsigned int)thisnodenum);
			*eof = 1;
			break;
		}
	}
	kfree(node);
	if (nodenum == 0xff)
		*eof = 1;
	*start = (char *)((off_t) nodenum - pos);
	return p - buf;
}

static int proc_read_node(char *buf, char **start, off_t pos,
			  int count, int *eof, void *data)
{
	struct pnp_bios_node *node;
	int boot = (long)data >> 8;
	u8 nodenum = (long)data;
	int len;

	node = kzalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	if (pnp_bios_get_dev_node(&nodenum, boot, node)) {
		kfree(node);
		return -EIO;
	}
	len = node->size - sizeof(struct pnp_bios_node);
	memcpy(buf, node->data, len);
	kfree(node);
	return len;
}

static int proc_write_node(struct file *file, const char __user * buf,
			   unsigned long count, void *data)
{
	struct pnp_bios_node *node;
	int boot = (long)data >> 8;
	u8 nodenum = (long)data;
	int ret = count;

	node = kzalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	if (pnp_bios_get_dev_node(&nodenum, boot, node)) {
		ret = -EIO;
		goto out;
	}
	if (count != node->size - sizeof(struct pnp_bios_node)) {
		ret = -EINVAL;
		goto out;
	}
	if (copy_from_user(node->data, buf, count)) {
		ret = -EFAULT;
		goto out;
	}
	if (pnp_bios_set_dev_node(node->handle, boot, node) != 0) {
		ret = -EINVAL;
		goto out;
	}
	ret = count;
      out:
	kfree(node);
	return ret;
}

int pnpbios_interface_attach_device(struct pnp_bios_node *node)
{
	char name[3];
	struct proc_dir_entry *ent;

	sprintf(name, "%02x", node->handle);

	if (!proc_pnp)
		return -EIO;
	if (!pnpbios_dont_use_current_config) {
		ent = create_proc_entry(name, 0, proc_pnp);
		if (ent) {
			ent->read_proc = proc_read_node;
			ent->write_proc = proc_write_node;
			ent->data = (void *)(long)(node->handle);
		}
	}

	if (!proc_pnp_boot)
		return -EIO;
	ent = create_proc_entry(name, 0, proc_pnp_boot);
	if (ent) {
		ent->read_proc = proc_read_node;
		ent->write_proc = proc_write_node;
		ent->data = (void *)(long)(node->handle + 0x100);
		return 0;
	}

	return -EIO;
}

/*
 * When this is called, pnpbios functions are assumed to
 * work and the pnpbios_dont_use_current_config flag
 * should already have been set to the appropriate value
 */
int __init pnpbios_proc_init(void)
{
	proc_pnp = proc_mkdir("pnp", proc_bus);
	if (!proc_pnp)
		return -EIO;
	proc_pnp_boot = proc_mkdir("boot", proc_pnp);
	if (!proc_pnp_boot)
		return -EIO;
	create_proc_read_entry("devices", 0, proc_pnp, proc_read_devices, NULL);
	create_proc_read_entry("configuration_info", 0, proc_pnp,
			       proc_read_pnpconfig, NULL);
	create_proc_read_entry("escd_info", 0, proc_pnp, proc_read_escdinfo,
			       NULL);
	create_proc_read_entry("escd", S_IRUSR, proc_pnp, proc_read_escd, NULL);
	create_proc_read_entry("legacy_device_resources", 0, proc_pnp,
			       proc_read_legacyres, NULL);

	return 0;
}

void __exit pnpbios_proc_exit(void)
{
	int i;
	char name[3];

	if (!proc_pnp)
		return;

	for (i = 0; i < 0xff; i++) {
		sprintf(name, "%02x", i);
		if (!pnpbios_dont_use_current_config)
			remove_proc_entry(name, proc_pnp);
		remove_proc_entry(name, proc_pnp_boot);
	}
	remove_proc_entry("legacy_device_resources", proc_pnp);
	remove_proc_entry("escd", proc_pnp);
	remove_proc_entry("escd_info", proc_pnp);
	remove_proc_entry("configuration_info", proc_pnp);
	remove_proc_entry("devices", proc_pnp);
	remove_proc_entry("boot", proc_pnp);
	remove_proc_entry("pnp", proc_bus);

	return;
}
