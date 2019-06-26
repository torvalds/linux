// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA Device Feature List (DFL) Support
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Kang Luwei <luwei.kang@intel.com>
 *   Zhang Yi <yi.z.zhang@intel.com>
 *   Wu Hao <hao.wu@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 */
#include <linux/module.h>

#include "dfl.h"

static DEFINE_MUTEX(dfl_id_mutex);

/*
 * when adding a new feature dev support in DFL framework, it's required to
 * add a new item in enum dfl_id_type and provide related information in below
 * dfl_devs table which is indexed by dfl_id_type, e.g. name string used for
 * platform device creation (define name strings in dfl.h, as they could be
 * reused by platform device drivers).
 *
 * if the new feature dev needs chardev support, then it's required to add
 * a new item in dfl_chardevs table and configure dfl_devs[i].devt_type as
 * index to dfl_chardevs table. If no chardev support just set devt_type
 * as one invalid index (DFL_FPGA_DEVT_MAX).
 */
enum dfl_id_type {
	FME_ID,		/* fme id allocation and mapping */
	PORT_ID,	/* port id allocation and mapping */
	DFL_ID_MAX,
};

enum dfl_fpga_devt_type {
	DFL_FPGA_DEVT_FME,
	DFL_FPGA_DEVT_PORT,
	DFL_FPGA_DEVT_MAX,
};

static struct lock_class_key dfl_pdata_keys[DFL_ID_MAX];

static const char *dfl_pdata_key_strings[DFL_ID_MAX] = {
	"dfl-fme-pdata",
	"dfl-port-pdata",
};

/**
 * dfl_dev_info - dfl feature device information.
 * @name: name string of the feature platform device.
 * @dfh_id: id value in Device Feature Header (DFH) register by DFL spec.
 * @id: idr id of the feature dev.
 * @devt_type: index to dfl_chrdevs[].
 */
struct dfl_dev_info {
	const char *name;
	u32 dfh_id;
	struct idr id;
	enum dfl_fpga_devt_type devt_type;
};

/* it is indexed by dfl_id_type */
static struct dfl_dev_info dfl_devs[] = {
	{.name = DFL_FPGA_FEATURE_DEV_FME, .dfh_id = DFH_ID_FIU_FME,
	 .devt_type = DFL_FPGA_DEVT_FME},
	{.name = DFL_FPGA_FEATURE_DEV_PORT, .dfh_id = DFH_ID_FIU_PORT,
	 .devt_type = DFL_FPGA_DEVT_PORT},
};

/**
 * dfl_chardev_info - chardev information of dfl feature device
 * @name: nmae string of the char device.
 * @devt: devt of the char device.
 */
struct dfl_chardev_info {
	const char *name;
	dev_t devt;
};

/* indexed by enum dfl_fpga_devt_type */
static struct dfl_chardev_info dfl_chrdevs[] = {
	{.name = DFL_FPGA_FEATURE_DEV_FME},
	{.name = DFL_FPGA_FEATURE_DEV_PORT},
};

static void dfl_ids_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dfl_devs); i++)
		idr_init(&dfl_devs[i].id);
}

static void dfl_ids_destroy(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dfl_devs); i++)
		idr_destroy(&dfl_devs[i].id);
}

static int dfl_id_alloc(enum dfl_id_type type, struct device *dev)
{
	int id;

	WARN_ON(type >= DFL_ID_MAX);
	mutex_lock(&dfl_id_mutex);
	id = idr_alloc(&dfl_devs[type].id, dev, 0, 0, GFP_KERNEL);
	mutex_unlock(&dfl_id_mutex);

	return id;
}

static void dfl_id_free(enum dfl_id_type type, int id)
{
	WARN_ON(type >= DFL_ID_MAX);
	mutex_lock(&dfl_id_mutex);
	idr_remove(&dfl_devs[type].id, id);
	mutex_unlock(&dfl_id_mutex);
}

static enum dfl_id_type feature_dev_id_type(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dfl_devs); i++)
		if (!strcmp(dfl_devs[i].name, pdev->name))
			return i;

	return DFL_ID_MAX;
}

static enum dfl_id_type dfh_id_to_type(u32 id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dfl_devs); i++)
		if (dfl_devs[i].dfh_id == id)
			return i;

	return DFL_ID_MAX;
}

/*
 * introduce a global port_ops list, it allows port drivers to register ops
 * in such list, then other feature devices (e.g. FME), could use the port
 * functions even related port platform device is hidden. Below is one example,
 * in virtualization case of PCIe-based FPGA DFL device, when SRIOV is
 * enabled, port (and it's AFU) is turned into VF and port platform device
 * is hidden from system but it's still required to access port to finish FPGA
 * reconfiguration function in FME.
 */

static DEFINE_MUTEX(dfl_port_ops_mutex);
static LIST_HEAD(dfl_port_ops_list);

/**
 * dfl_fpga_port_ops_get - get matched port ops from the global list
 * @pdev: platform device to match with associated port ops.
 * Return: matched port ops on success, NULL otherwise.
 *
 * Please note that must dfl_fpga_port_ops_put after use the port_ops.
 */
struct dfl_fpga_port_ops *dfl_fpga_port_ops_get(struct platform_device *pdev)
{
	struct dfl_fpga_port_ops *ops = NULL;

	mutex_lock(&dfl_port_ops_mutex);
	if (list_empty(&dfl_port_ops_list))
		goto done;

	list_for_each_entry(ops, &dfl_port_ops_list, node) {
		/* match port_ops using the name of platform device */
		if (!strcmp(pdev->name, ops->name)) {
			if (!try_module_get(ops->owner))
				ops = NULL;
			goto done;
		}
	}

	ops = NULL;
done:
	mutex_unlock(&dfl_port_ops_mutex);
	return ops;
}
EXPORT_SYMBOL_GPL(dfl_fpga_port_ops_get);

/**
 * dfl_fpga_port_ops_put - put port ops
 * @ops: port ops.
 */
void dfl_fpga_port_ops_put(struct dfl_fpga_port_ops *ops)
{
	if (ops && ops->owner)
		module_put(ops->owner);
}
EXPORT_SYMBOL_GPL(dfl_fpga_port_ops_put);

/**
 * dfl_fpga_port_ops_add - add port_ops to global list
 * @ops: port ops to add.
 */
void dfl_fpga_port_ops_add(struct dfl_fpga_port_ops *ops)
{
	mutex_lock(&dfl_port_ops_mutex);
	list_add_tail(&ops->node, &dfl_port_ops_list);
	mutex_unlock(&dfl_port_ops_mutex);
}
EXPORT_SYMBOL_GPL(dfl_fpga_port_ops_add);

/**
 * dfl_fpga_port_ops_del - remove port_ops from global list
 * @ops: port ops to del.
 */
void dfl_fpga_port_ops_del(struct dfl_fpga_port_ops *ops)
{
	mutex_lock(&dfl_port_ops_mutex);
	list_del(&ops->node);
	mutex_unlock(&dfl_port_ops_mutex);
}
EXPORT_SYMBOL_GPL(dfl_fpga_port_ops_del);

/**
 * dfl_fpga_check_port_id - check the port id
 * @pdev: port platform device.
 * @pport_id: port id to compare.
 *
 * Return: 1 if port device matches with given port id, otherwise 0.
 */
int dfl_fpga_check_port_id(struct platform_device *pdev, void *pport_id)
{
	struct dfl_fpga_port_ops *port_ops = dfl_fpga_port_ops_get(pdev);
	int port_id;

	if (!port_ops || !port_ops->get_id)
		return 0;

	port_id = port_ops->get_id(pdev);
	dfl_fpga_port_ops_put(port_ops);

	return port_id == *(int *)pport_id;
}
EXPORT_SYMBOL_GPL(dfl_fpga_check_port_id);

/**
 * dfl_fpga_dev_feature_uinit - uinit for sub features of dfl feature device
 * @pdev: feature device.
 */
void dfl_fpga_dev_feature_uinit(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct dfl_feature *feature;

	dfl_fpga_dev_for_each_feature(pdata, feature)
		if (feature->ops) {
			feature->ops->uinit(pdev, feature);
			feature->ops = NULL;
		}
}
EXPORT_SYMBOL_GPL(dfl_fpga_dev_feature_uinit);

static int dfl_feature_instance_init(struct platform_device *pdev,
				     struct dfl_feature_platform_data *pdata,
				     struct dfl_feature *feature,
				     struct dfl_feature_driver *drv)
{
	int ret;

	ret = drv->ops->init(pdev, feature);
	if (ret)
		return ret;

	feature->ops = drv->ops;

	return ret;
}

/**
 * dfl_fpga_dev_feature_init - init for sub features of dfl feature device
 * @pdev: feature device.
 * @feature_drvs: drvs for sub features.
 *
 * This function will match sub features with given feature drvs list and
 * use matched drv to init related sub feature.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int dfl_fpga_dev_feature_init(struct platform_device *pdev,
			      struct dfl_feature_driver *feature_drvs)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct dfl_feature_driver *drv = feature_drvs;
	struct dfl_feature *feature;
	int ret;

	while (drv->ops) {
		dfl_fpga_dev_for_each_feature(pdata, feature) {
			/* match feature and drv using id */
			if (feature->id == drv->id) {
				ret = dfl_feature_instance_init(pdev, pdata,
								feature, drv);
				if (ret)
					goto exit;
			}
		}
		drv++;
	}

	return 0;
exit:
	dfl_fpga_dev_feature_uinit(pdev);
	return ret;
}
EXPORT_SYMBOL_GPL(dfl_fpga_dev_feature_init);

static void dfl_chardev_uinit(void)
{
	int i;

	for (i = 0; i < DFL_FPGA_DEVT_MAX; i++)
		if (MAJOR(dfl_chrdevs[i].devt)) {
			unregister_chrdev_region(dfl_chrdevs[i].devt,
						 MINORMASK + 1);
			dfl_chrdevs[i].devt = MKDEV(0, 0);
		}
}

static int dfl_chardev_init(void)
{
	int i, ret;

	for (i = 0; i < DFL_FPGA_DEVT_MAX; i++) {
		ret = alloc_chrdev_region(&dfl_chrdevs[i].devt, 0,
					  MINORMASK + 1, dfl_chrdevs[i].name);
		if (ret)
			goto exit;
	}

	return 0;

exit:
	dfl_chardev_uinit();
	return ret;
}

static dev_t dfl_get_devt(enum dfl_fpga_devt_type type, int id)
{
	if (type >= DFL_FPGA_DEVT_MAX)
		return 0;

	return MKDEV(MAJOR(dfl_chrdevs[type].devt), id);
}

/**
 * dfl_fpga_dev_ops_register - register cdev ops for feature dev
 *
 * @pdev: feature dev.
 * @fops: file operations for feature dev's cdev.
 * @owner: owning module/driver.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int dfl_fpga_dev_ops_register(struct platform_device *pdev,
			      const struct file_operations *fops,
			      struct module *owner)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);

	cdev_init(&pdata->cdev, fops);
	pdata->cdev.owner = owner;

	/*
	 * set parent to the feature device so that its refcount is
	 * decreased after the last refcount of cdev is gone, that
	 * makes sure the feature device is valid during device
	 * file's life-cycle.
	 */
	pdata->cdev.kobj.parent = &pdev->dev.kobj;

	return cdev_add(&pdata->cdev, pdev->dev.devt, 1);
}
EXPORT_SYMBOL_GPL(dfl_fpga_dev_ops_register);

/**
 * dfl_fpga_dev_ops_unregister - unregister cdev ops for feature dev
 * @pdev: feature dev.
 */
void dfl_fpga_dev_ops_unregister(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);

	cdev_del(&pdata->cdev);
}
EXPORT_SYMBOL_GPL(dfl_fpga_dev_ops_unregister);

/**
 * struct build_feature_devs_info - info collected during feature dev build.
 *
 * @dev: device to enumerate.
 * @cdev: the container device for all feature devices.
 * @feature_dev: current feature device.
 * @ioaddr: header register region address of feature device in enumeration.
 * @sub_features: a sub features linked list for feature device in enumeration.
 * @feature_num: number of sub features for feature device in enumeration.
 */
struct build_feature_devs_info {
	struct device *dev;
	struct dfl_fpga_cdev *cdev;
	struct platform_device *feature_dev;
	void __iomem *ioaddr;
	struct list_head sub_features;
	int feature_num;
};

/**
 * struct dfl_feature_info - sub feature info collected during feature dev build
 *
 * @fid: id of this sub feature.
 * @mmio_res: mmio resource of this sub feature.
 * @ioaddr: mapped base address of mmio resource.
 * @node: node in sub_features linked list.
 */
struct dfl_feature_info {
	u64 fid;
	struct resource mmio_res;
	void __iomem *ioaddr;
	struct list_head node;
};

static void dfl_fpga_cdev_add_port_dev(struct dfl_fpga_cdev *cdev,
				       struct platform_device *port)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&port->dev);

	mutex_lock(&cdev->lock);
	list_add(&pdata->node, &cdev->port_dev_list);
	get_device(&pdata->dev->dev);
	mutex_unlock(&cdev->lock);
}

/*
 * register current feature device, it is called when we need to switch to
 * another feature parsing or we have parsed all features on given device
 * feature list.
 */
static int build_info_commit_dev(struct build_feature_devs_info *binfo)
{
	struct platform_device *fdev = binfo->feature_dev;
	struct dfl_feature_platform_data *pdata;
	struct dfl_feature_info *finfo, *p;
	enum dfl_id_type type;
	int ret, index = 0;

	if (!fdev)
		return 0;

	type = feature_dev_id_type(fdev);
	if (WARN_ON_ONCE(type >= DFL_ID_MAX))
		return -EINVAL;

	/*
	 * we do not need to care for the memory which is associated with
	 * the platform device. After calling platform_device_unregister(),
	 * it will be automatically freed by device's release() callback,
	 * platform_device_release().
	 */
	pdata = kzalloc(dfl_feature_platform_data_size(binfo->feature_num),
			GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->dev = fdev;
	pdata->num = binfo->feature_num;
	pdata->dfl_cdev = binfo->cdev;
	mutex_init(&pdata->lock);
	lockdep_set_class_and_name(&pdata->lock, &dfl_pdata_keys[type],
				   dfl_pdata_key_strings[type]);

	/*
	 * the count should be initialized to 0 to make sure
	 *__fpga_port_enable() following __fpga_port_disable()
	 * works properly for port device.
	 * and it should always be 0 for fme device.
	 */
	WARN_ON(pdata->disable_count);

	fdev->dev.platform_data = pdata;

	/* each sub feature has one MMIO resource */
	fdev->num_resources = binfo->feature_num;
	fdev->resource = kcalloc(binfo->feature_num, sizeof(*fdev->resource),
				 GFP_KERNEL);
	if (!fdev->resource)
		return -ENOMEM;

	/* fill features and resource information for feature dev */
	list_for_each_entry_safe(finfo, p, &binfo->sub_features, node) {
		struct dfl_feature *feature = &pdata->features[index];

		/* save resource information for each feature */
		feature->id = finfo->fid;
		feature->resource_index = index;
		feature->ioaddr = finfo->ioaddr;
		fdev->resource[index++] = finfo->mmio_res;

		list_del(&finfo->node);
		kfree(finfo);
	}

	ret = platform_device_add(binfo->feature_dev);
	if (!ret) {
		if (type == PORT_ID)
			dfl_fpga_cdev_add_port_dev(binfo->cdev,
						   binfo->feature_dev);
		else
			binfo->cdev->fme_dev =
					get_device(&binfo->feature_dev->dev);
		/*
		 * reset it to avoid build_info_free() freeing their resource.
		 *
		 * The resource of successfully registered feature devices
		 * will be freed by platform_device_unregister(). See the
		 * comments in build_info_create_dev().
		 */
		binfo->feature_dev = NULL;
	}

	return ret;
}

static int
build_info_create_dev(struct build_feature_devs_info *binfo,
		      enum dfl_id_type type, void __iomem *ioaddr)
{
	struct platform_device *fdev;
	int ret;

	if (type >= DFL_ID_MAX)
		return -EINVAL;

	/* we will create a new device, commit current device first */
	ret = build_info_commit_dev(binfo);
	if (ret)
		return ret;

	/*
	 * we use -ENODEV as the initialization indicator which indicates
	 * whether the id need to be reclaimed
	 */
	fdev = platform_device_alloc(dfl_devs[type].name, -ENODEV);
	if (!fdev)
		return -ENOMEM;

	binfo->feature_dev = fdev;
	binfo->feature_num = 0;
	binfo->ioaddr = ioaddr;
	INIT_LIST_HEAD(&binfo->sub_features);

	fdev->id = dfl_id_alloc(type, &fdev->dev);
	if (fdev->id < 0)
		return fdev->id;

	fdev->dev.parent = &binfo->cdev->region->dev;
	fdev->dev.devt = dfl_get_devt(dfl_devs[type].devt_type, fdev->id);

	return 0;
}

static void build_info_free(struct build_feature_devs_info *binfo)
{
	struct dfl_feature_info *finfo, *p;

	/*
	 * it is a valid id, free it. See comments in
	 * build_info_create_dev()
	 */
	if (binfo->feature_dev && binfo->feature_dev->id >= 0) {
		dfl_id_free(feature_dev_id_type(binfo->feature_dev),
			    binfo->feature_dev->id);

		list_for_each_entry_safe(finfo, p, &binfo->sub_features, node) {
			list_del(&finfo->node);
			kfree(finfo);
		}
	}

	platform_device_put(binfo->feature_dev);

	devm_kfree(binfo->dev, binfo);
}

static inline u32 feature_size(void __iomem *start)
{
	u64 v = readq(start + DFH);
	u32 ofst = FIELD_GET(DFH_NEXT_HDR_OFST, v);
	/* workaround for private features with invalid size, use 4K instead */
	return ofst ? ofst : 4096;
}

static u64 feature_id(void __iomem *start)
{
	u64 v = readq(start + DFH);
	u16 id = FIELD_GET(DFH_ID, v);
	u8 type = FIELD_GET(DFH_TYPE, v);

	if (type == DFH_TYPE_FIU)
		return FEATURE_ID_FIU_HEADER;
	else if (type == DFH_TYPE_PRIVATE)
		return id;
	else if (type == DFH_TYPE_AFU)
		return FEATURE_ID_AFU;

	WARN_ON(1);
	return 0;
}

/*
 * when create sub feature instances, for private features, it doesn't need
 * to provide resource size and feature id as they could be read from DFH
 * register. For afu sub feature, its register region only contains user
 * defined registers, so never trust any information from it, just use the
 * resource size information provided by its parent FIU.
 */
static int
create_feature_instance(struct build_feature_devs_info *binfo,
			struct dfl_fpga_enum_dfl *dfl, resource_size_t ofst,
			resource_size_t size, u64 fid)
{
	struct dfl_feature_info *finfo;

	/* read feature size and id if inputs are invalid */
	size = size ? size : feature_size(dfl->ioaddr + ofst);
	fid = fid ? fid : feature_id(dfl->ioaddr + ofst);

	if (dfl->len - ofst < size)
		return -EINVAL;

	finfo = kzalloc(sizeof(*finfo), GFP_KERNEL);
	if (!finfo)
		return -ENOMEM;

	finfo->fid = fid;
	finfo->mmio_res.start = dfl->start + ofst;
	finfo->mmio_res.end = finfo->mmio_res.start + size - 1;
	finfo->mmio_res.flags = IORESOURCE_MEM;
	finfo->ioaddr = dfl->ioaddr + ofst;

	list_add_tail(&finfo->node, &binfo->sub_features);
	binfo->feature_num++;

	return 0;
}

static int parse_feature_port_afu(struct build_feature_devs_info *binfo,
				  struct dfl_fpga_enum_dfl *dfl,
				  resource_size_t ofst)
{
	u64 v = readq(binfo->ioaddr + PORT_HDR_CAP);
	u32 size = FIELD_GET(PORT_CAP_MMIO_SIZE, v) << 10;

	WARN_ON(!size);

	return create_feature_instance(binfo, dfl, ofst, size, FEATURE_ID_AFU);
}

static int parse_feature_afu(struct build_feature_devs_info *binfo,
			     struct dfl_fpga_enum_dfl *dfl,
			     resource_size_t ofst)
{
	if (!binfo->feature_dev) {
		dev_err(binfo->dev, "this AFU does not belong to any FIU.\n");
		return -EINVAL;
	}

	switch (feature_dev_id_type(binfo->feature_dev)) {
	case PORT_ID:
		return parse_feature_port_afu(binfo, dfl, ofst);
	default:
		dev_info(binfo->dev, "AFU belonging to FIU %s is not supported yet.\n",
			 binfo->feature_dev->name);
	}

	return 0;
}

static int parse_feature_fiu(struct build_feature_devs_info *binfo,
			     struct dfl_fpga_enum_dfl *dfl,
			     resource_size_t ofst)
{
	u32 id, offset;
	u64 v;
	int ret = 0;

	v = readq(dfl->ioaddr + ofst + DFH);
	id = FIELD_GET(DFH_ID, v);

	/* create platform device for dfl feature dev */
	ret = build_info_create_dev(binfo, dfh_id_to_type(id),
				    dfl->ioaddr + ofst);
	if (ret)
		return ret;

	ret = create_feature_instance(binfo, dfl, ofst, 0, 0);
	if (ret)
		return ret;
	/*
	 * find and parse FIU's child AFU via its NEXT_AFU register.
	 * please note that only Port has valid NEXT_AFU pointer per spec.
	 */
	v = readq(dfl->ioaddr + ofst + NEXT_AFU);

	offset = FIELD_GET(NEXT_AFU_NEXT_DFH_OFST, v);
	if (offset)
		return parse_feature_afu(binfo, dfl, ofst + offset);

	dev_dbg(binfo->dev, "No AFUs detected on FIU %d\n", id);

	return ret;
}

static int parse_feature_private(struct build_feature_devs_info *binfo,
				 struct dfl_fpga_enum_dfl *dfl,
				 resource_size_t ofst)
{
	if (!binfo->feature_dev) {
		dev_err(binfo->dev, "the private feature %llx does not belong to any AFU.\n",
			(unsigned long long)feature_id(dfl->ioaddr + ofst));
		return -EINVAL;
	}

	return create_feature_instance(binfo, dfl, ofst, 0, 0);
}

/**
 * parse_feature - parse a feature on given device feature list
 *
 * @binfo: build feature devices information.
 * @dfl: device feature list to parse
 * @ofst: offset to feature header on this device feature list
 */
static int parse_feature(struct build_feature_devs_info *binfo,
			 struct dfl_fpga_enum_dfl *dfl, resource_size_t ofst)
{
	u64 v;
	u32 type;

	v = readq(dfl->ioaddr + ofst + DFH);
	type = FIELD_GET(DFH_TYPE, v);

	switch (type) {
	case DFH_TYPE_AFU:
		return parse_feature_afu(binfo, dfl, ofst);
	case DFH_TYPE_PRIVATE:
		return parse_feature_private(binfo, dfl, ofst);
	case DFH_TYPE_FIU:
		return parse_feature_fiu(binfo, dfl, ofst);
	default:
		dev_info(binfo->dev,
			 "Feature Type %x is not supported.\n", type);
	}

	return 0;
}

static int parse_feature_list(struct build_feature_devs_info *binfo,
			      struct dfl_fpga_enum_dfl *dfl)
{
	void __iomem *start = dfl->ioaddr;
	void __iomem *end = dfl->ioaddr + dfl->len;
	int ret = 0;
	u32 ofst = 0;
	u64 v;

	/* walk through the device feature list via DFH's next DFH pointer. */
	for (; start < end; start += ofst) {
		if (end - start < DFH_SIZE) {
			dev_err(binfo->dev, "The region is too small to contain a feature.\n");
			return -EINVAL;
		}

		ret = parse_feature(binfo, dfl, start - dfl->ioaddr);
		if (ret)
			return ret;

		v = readq(start + DFH);
		ofst = FIELD_GET(DFH_NEXT_HDR_OFST, v);

		/* stop parsing if EOL(End of List) is set or offset is 0 */
		if ((v & DFH_EOL) || !ofst)
			break;
	}

	/* commit current feature device when reach the end of list */
	return build_info_commit_dev(binfo);
}

struct dfl_fpga_enum_info *dfl_fpga_enum_info_alloc(struct device *dev)
{
	struct dfl_fpga_enum_info *info;

	get_device(dev);

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		put_device(dev);
		return NULL;
	}

	info->dev = dev;
	INIT_LIST_HEAD(&info->dfls);

	return info;
}
EXPORT_SYMBOL_GPL(dfl_fpga_enum_info_alloc);

void dfl_fpga_enum_info_free(struct dfl_fpga_enum_info *info)
{
	struct dfl_fpga_enum_dfl *tmp, *dfl;
	struct device *dev;

	if (!info)
		return;

	dev = info->dev;

	/* remove all device feature lists in the list. */
	list_for_each_entry_safe(dfl, tmp, &info->dfls, node) {
		list_del(&dfl->node);
		devm_kfree(dev, dfl);
	}

	devm_kfree(dev, info);
	put_device(dev);
}
EXPORT_SYMBOL_GPL(dfl_fpga_enum_info_free);

/**
 * dfl_fpga_enum_info_add_dfl - add info of a device feature list to enum info
 *
 * @info: ptr to dfl_fpga_enum_info
 * @start: mmio resource address of the device feature list.
 * @len: mmio resource length of the device feature list.
 * @ioaddr: mapped mmio resource address of the device feature list.
 *
 * One FPGA device may have one or more Device Feature Lists (DFLs), use this
 * function to add information of each DFL to common data structure for next
 * step enumeration.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int dfl_fpga_enum_info_add_dfl(struct dfl_fpga_enum_info *info,
			       resource_size_t start, resource_size_t len,
			       void __iomem *ioaddr)
{
	struct dfl_fpga_enum_dfl *dfl;

	dfl = devm_kzalloc(info->dev, sizeof(*dfl), GFP_KERNEL);
	if (!dfl)
		return -ENOMEM;

	dfl->start = start;
	dfl->len = len;
	dfl->ioaddr = ioaddr;

	list_add_tail(&dfl->node, &info->dfls);

	return 0;
}
EXPORT_SYMBOL_GPL(dfl_fpga_enum_info_add_dfl);

static int remove_feature_dev(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	enum dfl_id_type type = feature_dev_id_type(pdev);
	int id = pdev->id;

	platform_device_unregister(pdev);

	dfl_id_free(type, id);

	return 0;
}

static void remove_feature_devs(struct dfl_fpga_cdev *cdev)
{
	device_for_each_child(&cdev->region->dev, NULL, remove_feature_dev);
}

/**
 * dfl_fpga_feature_devs_enumerate - enumerate feature devices
 * @info: information for enumeration.
 *
 * This function creates a container device (base FPGA region), enumerates
 * feature devices based on the enumeration info and creates platform devices
 * under the container device.
 *
 * Return: dfl_fpga_cdev struct on success, -errno on failure
 */
struct dfl_fpga_cdev *
dfl_fpga_feature_devs_enumerate(struct dfl_fpga_enum_info *info)
{
	struct build_feature_devs_info *binfo;
	struct dfl_fpga_enum_dfl *dfl;
	struct dfl_fpga_cdev *cdev;
	int ret = 0;

	if (!info->dev)
		return ERR_PTR(-ENODEV);

	cdev = devm_kzalloc(info->dev, sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return ERR_PTR(-ENOMEM);

	cdev->region = devm_fpga_region_create(info->dev, NULL, NULL);
	if (!cdev->region) {
		ret = -ENOMEM;
		goto free_cdev_exit;
	}

	cdev->parent = info->dev;
	mutex_init(&cdev->lock);
	INIT_LIST_HEAD(&cdev->port_dev_list);

	ret = fpga_region_register(cdev->region);
	if (ret)
		goto free_cdev_exit;

	/* create and init build info for enumeration */
	binfo = devm_kzalloc(info->dev, sizeof(*binfo), GFP_KERNEL);
	if (!binfo) {
		ret = -ENOMEM;
		goto unregister_region_exit;
	}

	binfo->dev = info->dev;
	binfo->cdev = cdev;

	/*
	 * start enumeration for all feature devices based on Device Feature
	 * Lists.
	 */
	list_for_each_entry(dfl, &info->dfls, node) {
		ret = parse_feature_list(binfo, dfl);
		if (ret) {
			remove_feature_devs(cdev);
			build_info_free(binfo);
			goto unregister_region_exit;
		}
	}

	build_info_free(binfo);

	return cdev;

unregister_region_exit:
	fpga_region_unregister(cdev->region);
free_cdev_exit:
	devm_kfree(info->dev, cdev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(dfl_fpga_feature_devs_enumerate);

/**
 * dfl_fpga_feature_devs_remove - remove all feature devices
 * @cdev: fpga container device.
 *
 * Remove the container device and all feature devices under given container
 * devices.
 */
void dfl_fpga_feature_devs_remove(struct dfl_fpga_cdev *cdev)
{
	struct dfl_feature_platform_data *pdata, *ptmp;

	remove_feature_devs(cdev);

	mutex_lock(&cdev->lock);
	if (cdev->fme_dev) {
		/* the fme should be unregistered. */
		WARN_ON(device_is_registered(cdev->fme_dev));
		put_device(cdev->fme_dev);
	}

	list_for_each_entry_safe(pdata, ptmp, &cdev->port_dev_list, node) {
		struct platform_device *port_dev = pdata->dev;

		/* the port should be unregistered. */
		WARN_ON(device_is_registered(&port_dev->dev));
		list_del(&pdata->node);
		put_device(&port_dev->dev);
	}
	mutex_unlock(&cdev->lock);

	fpga_region_unregister(cdev->region);
	devm_kfree(cdev->parent, cdev);
}
EXPORT_SYMBOL_GPL(dfl_fpga_feature_devs_remove);

/**
 * __dfl_fpga_cdev_find_port - find a port under given container device
 *
 * @cdev: container device
 * @data: data passed to match function
 * @match: match function used to find specific port from the port device list
 *
 * Find a port device under container device. This function needs to be
 * invoked with lock held.
 *
 * Return: pointer to port's platform device if successful, NULL otherwise.
 *
 * NOTE: you will need to drop the device reference with put_device() after use.
 */
struct platform_device *
__dfl_fpga_cdev_find_port(struct dfl_fpga_cdev *cdev, void *data,
			  int (*match)(struct platform_device *, void *))
{
	struct dfl_feature_platform_data *pdata;
	struct platform_device *port_dev;

	list_for_each_entry(pdata, &cdev->port_dev_list, node) {
		port_dev = pdata->dev;

		if (match(port_dev, data) && get_device(&port_dev->dev))
			return port_dev;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(__dfl_fpga_cdev_find_port);

static int __init dfl_fpga_init(void)
{
	int ret;

	dfl_ids_init();

	ret = dfl_chardev_init();
	if (ret)
		dfl_ids_destroy();

	return ret;
}

static void __exit dfl_fpga_exit(void)
{
	dfl_chardev_uinit();
	dfl_ids_destroy();
}

module_init(dfl_fpga_init);
module_exit(dfl_fpga_exit);

MODULE_DESCRIPTION("FPGA Device Feature List (DFL) Support");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
