#include <linux/kernel.h>
#include <linux/lightnvm.h>
#include <linux/miscdevice.h>
#include <linux/kobject.h>
#include <linux/blk-mq.h>

#include "lightnvm.h"

static ssize_t nvm_dev_attr_show(struct device *dev,
				 struct device_attribute *dattr, char *page)
{
	struct nvm_dev *ndev = container_of(dev, struct nvm_dev, dev);
	struct nvm_id *id = &ndev->identity;
	struct nvm_id_group *grp = &id->groups[0];
	struct attribute *attr = &dattr->attr;

	if (strcmp(attr->name, "version") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", id->ver_id);
	} else if (strcmp(attr->name, "vendor_opcode") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", id->vmnt);
	} else if (strcmp(attr->name, "capabilities") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", id->cap);
	} else if (strcmp(attr->name, "device_mode") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", id->dom);
	} else if (strcmp(attr->name, "media_manager") == 0) {
		if (!ndev->mt)
			return scnprintf(page, PAGE_SIZE, "%s\n", "none");
		return scnprintf(page, PAGE_SIZE, "%s\n", ndev->mt->name);
	} else if (strcmp(attr->name, "ppa_format") == 0) {
		return scnprintf(page, PAGE_SIZE,
			"0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
			id->ppaf.ch_offset, id->ppaf.ch_len,
			id->ppaf.lun_offset, id->ppaf.lun_len,
			id->ppaf.pln_offset, id->ppaf.pln_len,
			id->ppaf.blk_offset, id->ppaf.blk_len,
			id->ppaf.pg_offset, id->ppaf.pg_len,
			id->ppaf.sect_offset, id->ppaf.sect_len);
	} else if (strcmp(attr->name, "media_type") == 0) {	/* u8 */
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->mtype);
	} else if (strcmp(attr->name, "flash_media_type") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->fmtype);
	} else if (strcmp(attr->name, "num_channels") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->num_ch);
	} else if (strcmp(attr->name, "num_luns") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->num_lun);
	} else if (strcmp(attr->name, "num_planes") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->num_pln);
	} else if (strcmp(attr->name, "num_blocks") == 0) {	/* u16 */
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->num_blk);
	} else if (strcmp(attr->name, "num_pages") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->num_pg);
	} else if (strcmp(attr->name, "page_size") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->fpg_sz);
	} else if (strcmp(attr->name, "hw_sector_size") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->csecs);
	} else if (strcmp(attr->name, "oob_sector_size") == 0) {/* u32 */
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->sos);
	} else if (strcmp(attr->name, "read_typ") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->trdt);
	} else if (strcmp(attr->name, "read_max") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->trdm);
	} else if (strcmp(attr->name, "prog_typ") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->tprt);
	} else if (strcmp(attr->name, "prog_max") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->tprm);
	} else if (strcmp(attr->name, "erase_typ") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->tbet);
	} else if (strcmp(attr->name, "erase_max") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n", grp->tbem);
	} else if (strcmp(attr->name, "multiplane_modes") == 0) {
		return scnprintf(page, PAGE_SIZE, "0x%08x\n", grp->mpos);
	} else if (strcmp(attr->name, "media_capabilities") == 0) {
		return scnprintf(page, PAGE_SIZE, "0x%08x\n", grp->mccap);
	} else if (strcmp(attr->name, "max_phys_secs") == 0) {
		return scnprintf(page, PAGE_SIZE, "%u\n",
				ndev->ops->max_phys_sect);
	} else {
		return scnprintf(page,
				 PAGE_SIZE,
				 "Unhandled attr(%s) in `nvm_dev_attr_show`\n",
				 attr->name);
	}
}

#define NVM_DEV_ATTR_RO(_name)						\
	DEVICE_ATTR(_name, S_IRUGO, nvm_dev_attr_show, NULL)

static NVM_DEV_ATTR_RO(version);
static NVM_DEV_ATTR_RO(vendor_opcode);
static NVM_DEV_ATTR_RO(capabilities);
static NVM_DEV_ATTR_RO(device_mode);
static NVM_DEV_ATTR_RO(ppa_format);
static NVM_DEV_ATTR_RO(media_manager);

static NVM_DEV_ATTR_RO(media_type);
static NVM_DEV_ATTR_RO(flash_media_type);
static NVM_DEV_ATTR_RO(num_channels);
static NVM_DEV_ATTR_RO(num_luns);
static NVM_DEV_ATTR_RO(num_planes);
static NVM_DEV_ATTR_RO(num_blocks);
static NVM_DEV_ATTR_RO(num_pages);
static NVM_DEV_ATTR_RO(page_size);
static NVM_DEV_ATTR_RO(hw_sector_size);
static NVM_DEV_ATTR_RO(oob_sector_size);
static NVM_DEV_ATTR_RO(read_typ);
static NVM_DEV_ATTR_RO(read_max);
static NVM_DEV_ATTR_RO(prog_typ);
static NVM_DEV_ATTR_RO(prog_max);
static NVM_DEV_ATTR_RO(erase_typ);
static NVM_DEV_ATTR_RO(erase_max);
static NVM_DEV_ATTR_RO(multiplane_modes);
static NVM_DEV_ATTR_RO(media_capabilities);
static NVM_DEV_ATTR_RO(max_phys_secs);

#define NVM_DEV_ATTR(_name) (dev_attr_##_name##)

static struct attribute *nvm_dev_attrs[] = {
	&dev_attr_version.attr,
	&dev_attr_vendor_opcode.attr,
	&dev_attr_capabilities.attr,
	&dev_attr_device_mode.attr,
	&dev_attr_media_manager.attr,

	&dev_attr_ppa_format.attr,
	&dev_attr_media_type.attr,
	&dev_attr_flash_media_type.attr,
	&dev_attr_num_channels.attr,
	&dev_attr_num_luns.attr,
	&dev_attr_num_planes.attr,
	&dev_attr_num_blocks.attr,
	&dev_attr_num_pages.attr,
	&dev_attr_page_size.attr,
	&dev_attr_hw_sector_size.attr,
	&dev_attr_oob_sector_size.attr,
	&dev_attr_read_typ.attr,
	&dev_attr_read_max.attr,
	&dev_attr_prog_typ.attr,
	&dev_attr_prog_max.attr,
	&dev_attr_erase_typ.attr,
	&dev_attr_erase_max.attr,
	&dev_attr_multiplane_modes.attr,
	&dev_attr_media_capabilities.attr,
	&dev_attr_max_phys_secs.attr,
	NULL,
};

static struct attribute_group nvm_dev_attr_group = {
	.name = "lightnvm",
	.attrs = nvm_dev_attrs,
};

static const struct attribute_group *nvm_dev_attr_groups[] = {
	&nvm_dev_attr_group,
	NULL,
};

static void nvm_dev_release(struct device *device)
{
	struct nvm_dev *dev = container_of(device, struct nvm_dev, dev);
	struct request_queue *q = dev->q;

	pr_debug("nvm/sysfs: `nvm_dev_release`\n");

	blk_mq_unregister_dev(device, q);

	nvm_free(dev);
}

static struct device_type nvm_type = {
	.name		= "lightnvm",
	.groups		= nvm_dev_attr_groups,
	.release	= nvm_dev_release,
};

int nvm_sysfs_register_dev(struct nvm_dev *dev)
{
	int ret;

	if (!dev->parent_dev)
		return 0;

	dev->dev.parent = dev->parent_dev;
	dev_set_name(&dev->dev, "%s", dev->name);
	dev->dev.type = &nvm_type;
	device_initialize(&dev->dev);
	ret = device_add(&dev->dev);

	if (!ret)
		blk_mq_register_dev(&dev->dev, dev->q);

	return ret;
}

void nvm_sysfs_unregister_dev(struct nvm_dev *dev)
{
	if (dev && dev->parent_dev)
		kobject_put(&dev->dev.kobj);
}
