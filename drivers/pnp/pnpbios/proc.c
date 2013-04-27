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
 *
 * The .../legacy_device_resources file is not used yet.
 *
 * The other files are human-readable.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/pnp.h>
#include <linux/seq_file.h>
#include <linux/init.h>

#include <asm/uaccess.h>

#include "pnpbios.h"

static struct proc_dir_entry *proc_pnp = NULL;
static struct proc_dir_entry *proc_pnp_boot = NULL;

static int pnpconfig_proc_show(struct seq_file *m, void *v)
{
	struct pnp_isa_config_struc pnps;

	if (pnp_bios_isapnp_config(&pnps))
		return -EIO;
	seq_printf(m, "structure_revision %d\n"
		      "number_of_CSNs %d\n"
		      "ISA_read_data_port 0x%x\n",
		   pnps.revision, pnps.no_csns, pnps.isa_rd_data_port);
	return 0;
}

static int pnpconfig_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pnpconfig_proc_show, NULL);
}

static const struct file_operations pnpconfig_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= pnpconfig_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int escd_info_proc_show(struct seq_file *m, void *v)
{
	struct escd_info_struc escd;

	if (pnp_bios_escd_info(&escd))
		return -EIO;
	seq_printf(m, "min_ESCD_write_size %d\n"
			"ESCD_size %d\n"
			"NVRAM_base 0x%x\n",
			escd.min_escd_write_size,
			escd.escd_size, escd.nv_storage_base);
	return 0;
}

static int escd_info_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, escd_info_proc_show, NULL);
}

static const struct file_operations escd_info_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= escd_info_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#define MAX_SANE_ESCD_SIZE (32*1024)
static int escd_proc_show(struct seq_file *m, void *v)
{
	struct escd_info_struc escd;
	char *tmpbuf;
	int escd_size;

	if (pnp_bios_escd_info(&escd))
		return -EIO;

	/* sanity check */
	if (escd.escd_size > MAX_SANE_ESCD_SIZE) {
		printk(KERN_ERR
		       "PnPBIOS: %s: ESCD size reported by BIOS escd_info call is too great\n", __func__);
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
		printk(KERN_ERR "PnPBIOS: %s: ESCD size reported by"
				" BIOS read_escd call is too great\n", __func__);
		kfree(tmpbuf);
		return -EFBIG;
	}

	seq_write(m, tmpbuf, escd_size);
	kfree(tmpbuf);
	return 0;
}

static int escd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, escd_proc_show, NULL);
}

static const struct file_operations escd_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= escd_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int pnp_legacyres_proc_show(struct seq_file *m, void *v)
{
	void *buf;

	buf = kmalloc(65536, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	if (pnp_bios_get_stat_res(buf)) {
		kfree(buf);
		return -EIO;
	}

	seq_write(m, buf, 65536);
	kfree(buf);
	return 0;
}

static int pnp_legacyres_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pnp_legacyres_proc_show, NULL);
}

static const struct file_operations pnp_legacyres_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= pnp_legacyres_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int pnp_devices_proc_show(struct seq_file *m, void *v)
{
	struct pnp_bios_node *node;
	u8 nodenum;

	node = kzalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	for (nodenum = 0; nodenum < 0xff;) {
		u8 thisnodenum = nodenum;

		if (pnp_bios_get_dev_node(&nodenum, PNPMODE_DYNAMIC, node))
			break;
		seq_printf(m, "%02x\t%08x\t%3phC\t%04x\n",
			     node->handle, node->eisa_id,
			     node->type_code, node->flags);
		if (nodenum <= thisnodenum) {
			printk(KERN_ERR
			       "%s Node number 0x%x is out of sequence following node 0x%x. Aborting.\n",
			       "PnPBIOS: proc_read_devices:",
			       (unsigned int)nodenum,
			       (unsigned int)thisnodenum);
			break;
		}
	}
	kfree(node);
	return 0;
}

static int pnp_devices_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pnp_devices_proc_show, NULL);
}

static const struct file_operations pnp_devices_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= pnp_devices_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int pnpbios_proc_show(struct seq_file *m, void *v)
{
	void *data = m->private;
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
	seq_write(m, node->data, len);
	kfree(node);
	return 0;
}

static int pnpbios_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pnpbios_proc_show, PDE(inode)->data);
}

static ssize_t pnpbios_proc_write(struct file *file, const char __user *buf,
				  size_t count, loff_t *pos)
{
	void *data = PDE(file_inode(file))->data;
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

static const struct file_operations pnpbios_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= pnpbios_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= pnpbios_proc_write,
};

int pnpbios_interface_attach_device(struct pnp_bios_node *node)
{
	char name[3];

	sprintf(name, "%02x", node->handle);

	if (!proc_pnp)
		return -EIO;
	if (!pnpbios_dont_use_current_config) {
		proc_create_data(name, 0644, proc_pnp, &pnpbios_proc_fops,
				 (void *)(long)(node->handle));
	}

	if (!proc_pnp_boot)
		return -EIO;
	if (proc_create_data(name, 0644, proc_pnp_boot, &pnpbios_proc_fops,
			     (void *)(long)(node->handle + 0x100)))
		return 0;
	return -EIO;
}

/*
 * When this is called, pnpbios functions are assumed to
 * work and the pnpbios_dont_use_current_config flag
 * should already have been set to the appropriate value
 */
int __init pnpbios_proc_init(void)
{
	proc_pnp = proc_mkdir("bus/pnp", NULL);
	if (!proc_pnp)
		return -EIO;
	proc_pnp_boot = proc_mkdir("boot", proc_pnp);
	if (!proc_pnp_boot)
		return -EIO;
	proc_create("devices", 0, proc_pnp, &pnp_devices_proc_fops);
	proc_create("configuration_info", 0, proc_pnp, &pnpconfig_proc_fops);
	proc_create("escd_info", 0, proc_pnp, &escd_info_proc_fops);
	proc_create("escd", S_IRUSR, proc_pnp, &escd_proc_fops);
	proc_create("legacy_device_resources", 0, proc_pnp, &pnp_legacyres_proc_fops);

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
	remove_proc_entry("bus/pnp", NULL);
}
