#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include "rockchip-hdmi-cec.h"
#include "linux/ioctl.h"
#include "linux/pagemap.h"

static struct cec_device *cec_dev;

static int cecreadframe(struct cec_framedata *frame)
{
	if (frame == NULL || !cec_dev ||
	    cec_dev->readframe == NULL || !cec_dev->enable)
		return -1;
	else
		return cec_dev->readframe(cec_dev->hdmi, frame);
}

static int cecsendframe(struct cec_framedata *frame)
{
	if (frame == NULL || !cec_dev || cec_dev->readframe == NULL)
		return -1;
	else
		return cec_dev->sendframe(cec_dev->hdmi, frame);
}

static void cecworkfunc(struct work_struct *work)
{
	struct cec_delayed_work *cec_w =
		container_of(work, struct cec_delayed_work, work.work);
	struct cecframelist *list_node;

	switch (cec_w->event) {
	case EVENT_ENUMERATE:
		break;
	case EVENT_RX_FRAME:
		list_node = kmalloc(sizeof(*list_node), GFP_KERNEL);
		if (!list_node)
			return;
		cecreadframe(&list_node->cecframe);
		if (cec_dev->enable) {
			mutex_lock(&cec_dev->cec_lock);
			list_add_tail(&(list_node->framelist),
				      &cec_dev->ceclist);
			sysfs_notify(&cec_dev->device.this_device->kobj,
				     NULL, "stat");
			mutex_unlock(&cec_dev->cec_lock);
		} else {
			kfree(list_node);
		}
		break;
	default:
		break;
	}

	kfree(cec_w->data);
	kfree(cec_w);
}

void rockchip_hdmi_cec_submit_work(int event, int delay, void *data)
{
	struct cec_delayed_work *work;

	CECDBG("%s event %04x delay %d\n", __func__, event, delay);

	if (!cec_dev)
		return;

	work = kmalloc(sizeof(*work), GFP_ATOMIC);

	if (work) {
		INIT_DELAYED_WORK(&work->work, cecworkfunc);
		work->event = event;
		work->data = data;
		queue_delayed_work(cec_dev->workqueue,
				   &work->work,
				   msecs_to_jiffies(delay));
	} else {
		CECDBG(KERN_WARNING "CEC: Cannot allocate memory\n");
	}
}

void rockchip_hdmi_cec_set_pa(int devpa)
{
	struct list_head *pos, *n;

	if (cec_dev) {
		cec_dev->address_phy = devpa;
		pr_info("%s %x\n", __func__, devpa);
		/*when hdmi hpd , ceclist will be reset*/
		mutex_lock(&cec_dev->cec_lock);
		if (!list_empty(&cec_dev->ceclist)) {
			list_for_each_safe(pos, n, &cec_dev->ceclist) {
				list_del(pos);
				kfree(pos);
			}
		}
		INIT_LIST_HEAD(&cec_dev->ceclist);
		sysfs_notify(&cec_dev->device.this_device->kobj, NULL, "stat");
		mutex_unlock(&cec_dev->cec_lock);
	}
}

static ssize_t  cec_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", cec_dev->enable);
}

static ssize_t cec_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret;

	ret = kstrtoint(buf, 0, &(cec_dev->enable));
	return count;
}

static ssize_t  cec_phy_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n", cec_dev->address_phy);
}

static ssize_t cec_phy_store(struct device *dev,
			     struct device_attribute *attr,
			 const char *buf, size_t count)
{
	int ret;

	ret = kstrtoint(buf, 0, &(cec_dev->address_phy));
	return count;
}

static ssize_t  cec_logic_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%02x\n", cec_dev->address_logic);
}

static ssize_t cec_logic_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int ret;

	ret = kstrtoint(buf, 0, &(cec_dev->address_logic));
	return count;
}

static ssize_t  cec_state_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int stat;

	mutex_lock(&cec_dev->cec_lock);
	if (!cec_dev->address_phy)
		stat = 0;
	else if (list_empty(&cec_dev->ceclist))
		stat = 1;
	else
		stat = 2;
	mutex_unlock(&cec_dev->cec_lock);
	return snprintf(buf, PAGE_SIZE, "%d\n", stat);
}

static struct device_attribute cec_attrs[] = {
	__ATTR(logic, 0666, cec_logic_show, cec_logic_store),
	__ATTR(phy, 0666, cec_phy_show, cec_phy_store),
	__ATTR(enable, 0666, cec_enable_show, cec_enable_store),
	__ATTR(stat, S_IRUGO, cec_state_show, NULL),
};

static long cec_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;
	void __user *argp;
	struct cec_framedata cecsendtemp;
	struct cecframelist *listemp;

	argp = (void __user *)arg;
	switch (cmd) {
	case HDMI_IOCTL_CECSETLA:
		ret = copy_from_user(&cec_dev->address_logic,
				     argp, sizeof(int));
		if (cec_dev->setceclogicaddr)
			cec_dev->setceclogicaddr(cec_dev->hdmi,
						 cec_dev->address_logic);
		break;
	case HDMI_IOCTL_CECSEND:
		ret = copy_from_user(&cecsendtemp, argp,
				     sizeof(struct cec_framedata));
		ret = cecsendframe(&cecsendtemp);
		cecsendtemp.returnval = ret;
		ret = copy_to_user(argp, &cecsendtemp,
				   sizeof(struct cec_framedata));
		break;
	case HDMI_IOCTL_CECENAB:
		ret = copy_from_user(&cec_dev->enable, argp, sizeof(int));
		break;
	case HDMI_IOCTL_CECPHY:
		ret = copy_to_user(argp, &(cec_dev->address_phy), sizeof(int));
		break;
	case HDMI_IOCTL_CECLOGIC:
		ret = copy_to_user(argp, &(cec_dev->address_logic),
				   sizeof(int));
		break;
	case HDMI_IOCTL_CECREAD:
		mutex_lock(&cec_dev->cec_lock);
		if (!list_empty(&cec_dev->ceclist)) {
			listemp = list_entry(cec_dev->ceclist.next,
					     struct cecframelist, framelist);
			ret = copy_to_user(argp, &listemp->cecframe,
					   sizeof(struct cec_framedata));
			list_del(&listemp->framelist);
			kfree(listemp);
		}
		mutex_unlock(&cec_dev->cec_lock);
		break;
	case HDMI_IOCTL_CECCLEARLA:
		break;
	case HDMI_IOCTL_CECWAKESTATE:
		ret = copy_to_user(argp, &(cec_dev->hdmi->sleep), sizeof(int));
		break;

	default:
		break;
	}
	return 0;
}

static const struct file_operations cec_fops = {
	.owner		= THIS_MODULE,
	.compat_ioctl	= cec_ioctl,
	.unlocked_ioctl	= cec_ioctl,
};

int rockchip_hdmi_cec_init(struct hdmi *hdmi,
			   int (*sendframe)(struct hdmi *,
					    struct cec_framedata *),
			   int (*readframe)(struct hdmi *,
					    struct cec_framedata *),
			   void (*setceclogicaddr)(struct hdmi *, int))
{
	int ret, i;

	cec_dev = kmalloc(sizeof(*cec_dev), GFP_KERNEL);
	if (!cec_dev)
		return -ENOMEM;

	memset(cec_dev, 0, sizeof(struct cec_device));
	mutex_init(&cec_dev->cec_lock);
	INIT_LIST_HEAD(&cec_dev->ceclist);
	cec_dev->hdmi = hdmi;
	cec_dev->enable = 1;
	cec_dev->sendframe = sendframe;
	cec_dev->readframe = readframe;
	cec_dev->setceclogicaddr = setceclogicaddr;
	cec_dev->workqueue = create_singlethread_workqueue("hdmi-cec");
	if (cec_dev->workqueue == NULL) {
		pr_err("HDMI CEC: create workqueue failed.\n");
		return -1;
	}
	cec_dev->device.minor = MISC_DYNAMIC_MINOR;
	cec_dev->device.name = "cec";
	cec_dev->device.mode = 0666;
	cec_dev->device.fops = &cec_fops;
	if (misc_register(&cec_dev->device)) {
		pr_err("CEC: Could not add cec misc driver\n");
		goto error;
	}
	for (i = 0; i < ARRAY_SIZE(cec_attrs); i++) {
		ret = device_create_file(cec_dev->device.this_device,
					 &cec_attrs[i]);
		if (ret) {
			pr_err("CEC: Could not add sys file\n");
			goto error1;
		}
	}
	return 0;

error1:
	misc_deregister(&cec_dev->device);
error:
	return -EINVAL;
}
