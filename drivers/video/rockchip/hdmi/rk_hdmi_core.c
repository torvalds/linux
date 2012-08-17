#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/hdmi.h>
#include <linux/input.h>


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
static void __hdmi_changed(struct hdmi *hdmi)
{
	int precent;
	
	mutex_lock(&hdmi->lock);
	precent = hdmi->ops->hdmi_precent(hdmi);
	if(precent && (hdmi->mode == DISP_ON_LCD) && hdmi->display_on){
		if(hdmi->ops->insert(hdmi) == 0){
			hdmi->mode = hdmi->display_on;
			kobject_uevent(&hdmi->dev->kobj, KOBJ_CHANGE);
		}
		else
			hdmi_dbg(hdmi->dev, "insert error\n");
        hdmi_set_backlight(hdmi->display_on==DISP_ON_HDMI?HDMI_DISABLE: HDMI_ENABLE);

	}
	else if(precent &&(hdmi->mode != hdmi->display_on)&& hdmi->display_on){
	    hdmi->mode = hdmi->display_on;
        hdmi_set_backlight(hdmi->display_on==DISP_ON_HDMI?HDMI_DISABLE: HDMI_ENABLE); 
	}
	else if((!precent || !hdmi->display_on) && hdmi->mode != DISP_ON_LCD){
		if(hdmi->ops->remove(hdmi) == 0){
			hdmi->mode = DISP_ON_LCD;
			hdmi_set_backlight(HDMI_ENABLE);
			kobject_uevent(&hdmi->dev->kobj, KOBJ_CHANGE);
		}
		else
			hdmi_dbg(hdmi->dev, "remove error\n");
	}
	mutex_unlock(&hdmi->lock);
	return;
}

void hdmi_changed(struct hdmi *hdmi, int msec)
{	
	schedule_delayed_work(&hdmi->work, msecs_to_jiffies(msec));
	return;
}
void hdmi_suspend(struct hdmi *hdmi)
{
	del_timer(&hdmi->timer);
	flush_delayed_work(&hdmi->work);
	if(hdmi->mode != DISP_ON_LCD){
		hdmi->ops->remove(hdmi);
		hdmi->mode = DISP_ON_LCD;
	}
	return;
}
void hdmi_resume(struct hdmi *hdmi)
{
	mod_timer(&hdmi->timer, jiffies + msecs_to_jiffies(10));
	return;
}

static void hdmi_changed_work(struct work_struct *work)
{
	struct hdmi *hdmi = container_of(work, struct hdmi,
						work.work);
	
	__hdmi_changed(hdmi);
	return;
}

void *hdmi_priv(struct hdmi *hdmi)
{
	return (void *)hdmi->priv;
}
static void hdmi_detect_timer(unsigned long data)
{
	struct hdmi *hdmi = (struct hdmi*)data;
	
	int precent =  hdmi->ops->hdmi_precent(hdmi);

	if((precent && hdmi->mode == DISP_ON_LCD) ||
			(!precent && hdmi->mode != DISP_ON_LCD))
		hdmi_changed(hdmi, 100);
	mod_timer(&hdmi->timer, jiffies + msecs_to_jiffies(200));
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

	INIT_DELAYED_WORK(&hdmi->work, hdmi_changed_work);

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
	mutex_init(&hdmi->lock);
	setup_timer(&hdmi->timer, hdmi_detect_timer,(unsigned long)hdmi);
	mod_timer(&hdmi->timer, jiffies + msecs_to_jiffies(200));
	return hdmi;
}
void hdmi_unregister(struct hdmi *hdmi)
{
	int id;

	if(!hdmi)
		return;
	id = hdmi->id;
	del_timer(&hdmi->timer);
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
int hdmi_get_scale(void)
{
	struct hdmi* hdmi = get_hdmi_struct(0);
	if(!hdmi)
		return 100;
	else if(hdmi->mode != DISP_ON_LCD)
		return hdmi->scale;
	else
	    return 100;
}

int hdmi_set_scale(int event, char *data, int len)
{
	int result;
	struct hdmi* hdmi = get_hdmi_struct(0);

	if(!hdmi)
		return -1;
	if(len != 4)
		return -1;
	if(fb_get_video_mode() || hdmi->mode == DISP_ON_LCD)
		return -1;

	result = data[0] | data[1]<<1 | data[2]<<2;
	if(event != MOUSE_NONE && (result & event) != event)
		return -1;

	hdmi->scale += data[3];
	
	hdmi->scale = (hdmi->scale>100)?100:hdmi->scale;
	hdmi->scale = (hdmi->scale<MIN_SCALE)?MIN_SCALE:hdmi->scale;
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

