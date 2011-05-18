#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/err.h>

#include <linux/hdmi-new.h>

struct class *hdmi_class;
struct hdmi_id_ref_info {
	struct hdmi *hdmi;
	int id;
	int ref;
}ref_info[HDMI_MAX_ID];
#ifdef CONFIG_SYSFS

extern int hdmi_create_attrs(struct hdmi *hdmi);
extern void hdmi_remove_attrs(struct hdmi *hdmi);

#else

static inline int hdmi_create_attrs(struct hdmi *hdmi)
{ return 0; }
static inline void hdmi_remove_attrs(struct hdmi *hdmi) {}

#endif /* CONFIG_SYSFS */


void hdmi_changed(struct hdmi *hdmi, int msec)
{
	schedule_delayed_work(&hdmi->changed_work, msecs_to_jiffies(msec));
}
int hdmi_suspend(struct hdmi *hdmi)
{
	flush_delayed_work(&hdmi->changed_work);
	return hdmi->ops->shutdown(hdmi);
}
int hdmi_resume(struct hdmi *hdmi)
{
	hdmi_changed(hdmi, 1);

	return 0;
}

static void hdmi_changed_work(struct work_struct *work)
{
	int precent, ret = 0;
	struct hdmi *hdmi = container_of(work, struct hdmi,
						changed_work.work);

	precent = hdmi->ops->hdmi_precent(hdmi);
	hdmi_dbg(hdmi->dev, "hdmi %s\n", (precent)?"insert" : "remove");

	if(precent)
		ret = hdmi->ops->insert(hdmi);
	else
		ret = hdmi->ops->remove(hdmi);
	if(ret < 0)
		dev_dbg(hdmi->dev, "hdmi changed error\n");
	kobject_uevent(&hdmi->dev->kobj, KOBJ_CHANGE);
}

void *hdmi_priv(struct hdmi *hdmi)
{
	return (void *)hdmi->priv;
}

struct hdmi *hdmi_register(int extra, struct device *parent)
{
	int rc = 0, i;
	char name[8];
	struct hdmi *hdmi = kzalloc(sizeof(struct hdmi)+ extra, GFP_KERNEL);

	if(!hdmi)
		return NULL;
	for(i = 0; i < HDMI_MAX_ID; i++) 
	{
		if(ref_info[i].ref == 0)
		{
			ref_info[i].ref = 1;
			hdmi->id = i;
			break;
		}
	}
	if(i == HDMI_MAX_ID)
	{
		kfree(hdmi);
		return NULL;
	}
	sprintf(name, "hdmi-%d", hdmi->id);
	
	hdmi->dev = device_create(hdmi_class, parent, 0,
				 "%s", name);
	if (IS_ERR(hdmi->dev)) {
		rc = PTR_ERR(hdmi->dev);
		goto dev_create_failed;
	}

	dev_set_drvdata(hdmi->dev, hdmi);
	ref_info[i].hdmi = hdmi;

	INIT_DELAYED_WORK(&hdmi->changed_work, hdmi_changed_work);

	rc = hdmi_create_attrs(hdmi);
	if (rc)
		goto create_attrs_failed;

	goto success;

create_attrs_failed:
	device_unregister(hdmi->dev);
dev_create_failed:
	hdmi_remove_attrs(hdmi);
	kfree(hdmi);
	return NULL;
success:
	return hdmi;
}
void hdmi_unregister(struct hdmi *hdmi)
{
	int id;

	if(!hdmi)
		return;
	id = hdmi->id;
	flush_scheduled_work();
	hdmi_remove_attrs(hdmi);
	device_unregister(hdmi->dev);

	kfree(hdmi);
	hdmi = NULL;
	ref_info[id].ref = 0;
	ref_info[id].hdmi = NULL;
}
struct hdmi *get_hdmi_struct(int nr)
{
	if(ref_info[nr].ref == 0)
		return NULL;
	else
		return ref_info[nr].hdmi;
}
int hdmi_is_insert(void)
{
	struct hdmi *hdmi = get_hdmi_struct(0);

	if(hdmi && hdmi->ops && hdmi->ops->hdmi_precent)
		return hdmi->ops->hdmi_precent(hdmi);
	else
		return 0;
}
static int __init hdmi_class_init(void)
{
	int i;
	
	hdmi_class = class_create(THIS_MODULE, "hdmi");

	if (IS_ERR(hdmi_class))
		return PTR_ERR(hdmi_class);
	for(i = 0; i < HDMI_MAX_ID; i++) {
		ref_info[i].id = i;
		ref_info[i].ref = 0;
		ref_info[i].hdmi = NULL;
	}
	return 0;
}

static void __exit hdmi_class_exit(void)
{
	class_destroy(hdmi_class);
}
EXPORT_SYMBOL(hdmi_changed);
EXPORT_SYMBOL(hdmi_register);
EXPORT_SYMBOL(hdmi_unregister);
EXPORT_SYMBOL(get_hdmi_struct);

subsys_initcall(hdmi_class_init);
module_exit(hdmi_class_exit);

