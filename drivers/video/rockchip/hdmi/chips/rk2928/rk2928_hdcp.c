#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include "rk2928_hdmi.h"
#include "rk2928_hdcp.h"

struct hdcp *hdcp = NULL;

static void hdcp_work_queue(struct work_struct *work);

/*-----------------------------------------------------------------------------
 * Function: hdcp_submit_work
 *-----------------------------------------------------------------------------
 */
static struct delayed_work *hdcp_submit_work(int event, int delay)
{
	struct hdcp_delayed_work *work;

	DBG("%s event %04x delay %d", __FUNCTION__, event, delay);
	
	work = kmalloc(sizeof(struct hdcp_delayed_work), GFP_ATOMIC);

	if (work) {
		INIT_DELAYED_WORK(&work->work, hdcp_work_queue);
		work->event = event;
		queue_delayed_work(hdcp->workqueue,
				   &work->work,
				   msecs_to_jiffies(delay));
	} else {
		printk(KERN_WARNING "HDCP: Cannot allocate memory to "
				    "create work\n");
		return 0;
	}

	return &work->work;
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_cancel_work
 *-----------------------------------------------------------------------------
 */
static void hdcp_cancel_work(struct delayed_work **work)
{
	int ret = 0;

	if (*work) {
		ret = cancel_delayed_work(*work);
		if (ret != 1) {
			ret = cancel_work_sync(&((*work)->work));
			printk(KERN_INFO "Canceling work failed - "
					 "cancel_work_sync done %d\n", ret);
		}
		kfree(*work);
		*work = 0;
	}
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_authentication_failure
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_authentication_failure(void)
{
	if (hdcp->hdmi_state == HDMI_STOPPED) {
		return;
	}

	rk2928_hdcp_disable();
	rk2928_hdmi_control_output(false);
	
	hdcp_cancel_work(&hdcp->pending_wq_event);
	
	if (hdcp->retry_cnt && (hdcp->hdmi_state != HDMI_STOPPED)) {
		if (hdcp->retry_cnt < HDCP_INFINITE_REAUTH) {
			hdcp->retry_cnt--;
			printk(KERN_INFO "HDCP: authentication failed - "
					 "retrying, attempts=%d\n",
							hdcp->retry_cnt);
		} else
			printk(KERN_INFO "HDCP: authentication failed - "
					 "retrying\n");

		hdcp->hdcp_state = HDCP_AUTHENTICATION_START;

		hdcp->pending_wq_event = hdcp_submit_work(HDCP_AUTH_REATT_EVENT,
							 HDCP_REAUTH_DELAY);
	} else {
		printk(KERN_INFO "HDCP: authentication failed - "
				 "HDCP disabled\n");
		hdcp->hdcp_state = HDCP_ENABLE_PENDING;
	}

}

/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_start_authentication
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_start_authentication(void)
{
	int status = HDCP_OK;

	hdcp->hdcp_state = HDCP_AUTHENTICATION_START;

	DBG("HDCP: authentication start");

	status = rk2928_hdcp_start_authentication();

	if (status != HDCP_OK) {
		DBG("HDCP: authentication failed");
		hdcp_wq_authentication_failure();
	} else {
		hdcp->hdcp_state = HDCP_WAIT_KSV_LIST;
//		hdcp->hdcp_state = HDCP_LINK_INTEGRITY_CHECK;
	}
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_check_bksv
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_check_bksv(void)
{
	int status = HDCP_OK;

	DBG("Check BKSV start");
	
	status = rk2928_hdcp_check_bksv();

	if (status != HDCP_OK) {
		printk(KERN_INFO "HDCP: Check BKSV failed");
		hdcp->retry_cnt = 0;
		hdcp_wq_authentication_failure();
	}
	else {
		DBG("HDCP: Check BKSV successful");

		hdcp->hdcp_state = HDCP_LINK_INTEGRITY_CHECK;

		/* Restore retry counter */
		if(hdcp->retry_times == 0)
			hdcp->retry_cnt = HDCP_INFINITE_REAUTH;
		else
			hdcp->retry_cnt = hdcp->retry_times;
	}
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_authentication_sucess
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_authentication_sucess(void)
{
	rk2928_hdmi_control_output(true);
	printk(KERN_INFO "HDCP: authentication pass");
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_disable
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_disable(int event)
{
	printk(KERN_INFO "HDCP: disabled");

	hdcp_cancel_work(&hdcp->pending_wq_event);
	rk2928_hdcp_disable();
	if(event == HDCP_DISABLE_CTL) {
		hdcp->hdcp_state = HDCP_DISABLED;
		if(hdcp->hdmi_state == HDMI_STARTED)
			rk2928_hdmi_control_output(true);
	}
	else if(event == HDCP_STOP_FRAME_EVENT)
		hdcp->hdcp_state = HDCP_ENABLE_PENDING;
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_work_queue
 *-----------------------------------------------------------------------------
 */
static void hdcp_work_queue(struct work_struct *work)
{
	struct hdcp_delayed_work *hdcp_w =
		container_of(work, struct hdcp_delayed_work, work.work);
	int event = hdcp_w->event;

	mutex_lock(&hdcp->lock);
	
	DBG("hdcp_work_queue() - START - %u hdmi=%d hdcp=%d evt= %x %d",
		jiffies_to_msecs(jiffies),
		hdcp->hdmi_state,
		hdcp->hdcp_state,
		(event & 0xFF00) >> 8,
		event & 0xFF);
	
	if(event == HDCP_STOP_FRAME_EVENT) {
		hdcp->hdmi_state = HDMI_STOPPED;
	}
	
	if (event == HDCP_DISABLE_CTL || event == HDCP_STOP_FRAME_EVENT) {
		hdcp_wq_disable(event);
	}
	
	if (event & HDCP_WORKQUEUE_SRC)
		hdcp->pending_wq_event = 0;
	
	/* First handle HDMI state */
	if (event == HDCP_START_FRAME_EVENT) {
		hdcp->pending_start = 0;
		hdcp->hdmi_state = HDMI_STARTED;
	}
	
	/**********************/
	/* HDCP state machine */
	/**********************/
	switch (hdcp->hdcp_state) {
		case HDCP_DISABLED:
			/* HDCP enable control or re-authentication event */
			if (event == HDCP_ENABLE_CTL) {
				if(hdcp->retry_times == 0)
					hdcp->retry_cnt = HDCP_INFINITE_REAUTH;
				else
					hdcp->retry_cnt = hdcp->retry_times;
				if (hdcp->hdmi_state == HDMI_STARTED)
					hdcp_wq_start_authentication();
				else
					hdcp->hdcp_state = HDCP_ENABLE_PENDING;
			}
			break;
		
		case HDCP_ENABLE_PENDING:
			/* HDMI start frame event */
			if (event == HDCP_START_FRAME_EVENT)
				hdcp_wq_start_authentication();

			break;
		
		case HDCP_AUTHENTICATION_START:
			/* Re-authentication */
			if (event == HDCP_AUTH_REATT_EVENT)
				hdcp_wq_start_authentication();
	
			break;
		
		case HDCP_WAIT_KSV_LIST:
			/* KSV failure */
			if (event == HDCP_FAIL_EVENT) {
				printk(KERN_INFO "HDCP: KSV switch failure\n");
	
				hdcp_wq_authentication_failure();
			}
			/* KSV list ready event */
			else if (event == HDCP_KSV_LIST_RDY_EVENT)
				hdcp_wq_check_bksv();
			break;
		
		case HDCP_LINK_INTEGRITY_CHECK:
			/* Ri failure */
			if (event == HDCP_FAIL_EVENT) {
				printk(KERN_INFO "HDCP: Ri check failure\n");
				hdcp_wq_authentication_failure();
			}
			else if(event == HDCP_AUTH_PASS_EVENT)
				hdcp_wq_authentication_sucess();
			break;
	
		default:
			printk(KERN_WARNING "HDCP: error - unknow HDCP state\n");
			break;
	}
	
	kfree(hdcp_w);
	if(event == HDCP_STOP_FRAME_EVENT)
		complete(&hdcp->complete);
		
	mutex_unlock(&hdcp->lock);
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_start_frame_cb
 *-----------------------------------------------------------------------------
 */
static void hdcp_start_frame_cb(void)
{
	DBG("hdcp_start_frame_cb()");

	/* Cancel any pending work */
	if (hdcp->pending_start)
		hdcp_cancel_work(&hdcp->pending_start);
	if (hdcp->pending_wq_event)
		hdcp_cancel_work(&hdcp->pending_wq_event);

	hdcp->pending_start = hdcp_submit_work(HDCP_START_FRAME_EVENT,
							HDCP_ENABLE_DELAY);
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_irq_cb
 *-----------------------------------------------------------------------------
 */
static void hdcp_irq_cb(int status)
{
	char interrupt1;
	char interrupt2;
	
	rk2928_hdcp_interrupt(&interrupt1, &interrupt2);
	DBG("%s 0x%02x 0x%02x", __FUNCTION__, interrupt1, interrupt2);
	if(interrupt1 & m_INT_HDCP_ERR)
	{
		if( (hdcp->hdcp_state != HDCP_DISABLED) &&
			(hdcp->hdcp_state != HDCP_ENABLE_PENDING) )
		{	
			hdcp_submit_work(HDCP_FAIL_EVENT, 0);
		}
	}
	else if(interrupt1 & (m_INT_BKSV_READY | m_INT_BKSV_UPDATE))
		hdcp_submit_work(HDCP_KSV_LIST_RDY_EVENT, 0);
	else if(interrupt1 & m_INT_AUTH_SUCCESS)
		hdcp_submit_work(HDCP_AUTH_PASS_EVENT, 0);
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_power_on_cb
 *-----------------------------------------------------------------------------
 */
static int hdcp_power_on_cb(void)
{
	DBG("%s", __FUNCTION__);
//	return rk2928_hdcp_load_key2mem(hdcp->keys);
	return HDCP_OK;
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_power_off_cb
 *-----------------------------------------------------------------------------
 */
static void hdcp_power_off_cb(void)
{
	DBG("%s", __FUNCTION__);
	if(!hdcp->enable)
		return;
	
	hdcp_cancel_work(&hdcp->pending_start);
	hdcp_cancel_work(&hdcp->pending_wq_event);
	init_completion(&hdcp->complete);
	/* Post event to workqueue */
	if (hdcp_submit_work(HDCP_STOP_FRAME_EVENT, 0))	
		wait_for_completion_interruptible_timeout(&hdcp->complete,
							msecs_to_jiffies(5000));
}

// Load HDCP key to external HDCP memory
static void hdcp_load_keys_cb(const struct firmware *fw, void *context)
{
	if (!fw) {
		pr_err("HDCP: failed to load keys\n");
		return;
	}
	
	if(fw->size < HDCP_KEY_SIZE) {
		pr_err("HDCP: firmware wrong size %d\n", fw->size);
		return;
	}
	
	hdcp->keys =  kmalloc(HDCP_KEY_SIZE, GFP_KERNEL);
	if(hdcp->keys == NULL) {
		pr_err("HDCP: can't allocated space for keys\n");
		return;
	}
	
	memcpy(hdcp->keys, fw->data, HDCP_KEY_SIZE);
	
	printk(KERN_INFO "HDCP: load hdcp key success\n");

	if(fw->size > HDCP_KEY_SIZE) {
		DBG("%s invalid key size %d", __FUNCTION__, fw->size - HDCP_KEY_SIZE);
		if((fw->size - HDCP_KEY_SIZE) % 5) {
			pr_err("HDCP: failed to load invalid keys\n");
			return;
		}
		hdcp->invalidkeys = kmalloc(fw->size - HDCP_KEY_SIZE, GFP_KERNEL);
		if(hdcp->invalidkeys == NULL) {
			pr_err("HDCP: can't allocated space for invalid keys\n");
			return;
		}
		memcpy(hdcp->invalidkeys, fw->data + HDCP_KEY_SIZE, fw->size - HDCP_KEY_SIZE);
		hdcp->invalidkey = (fw->size - HDCP_KEY_SIZE)/5;
		printk(KERN_INFO "HDCP: loaded hdcp invalid key success\n");
	}
}

static ssize_t hdcp_enable_read(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	int enable = 0;
	
	if(hdcp)
		enable = hdcp->enable;
		
	return snprintf(buf, PAGE_SIZE, "%d\n", enable);
}

static ssize_t hdcp_enable_write(struct device *device,
			   struct device_attribute *attr, const char *buf, size_t count)
{
	int enable;

	if(hdcp == NULL)
		return -EINVAL;
	
	sscanf(buf, "%d", &enable);
	if(hdcp->enable != enable)
	{
		/* Post event to workqueue */
		if(enable) {
			if (hdcp_submit_work(HDCP_ENABLE_CTL, 0) == 0)
				return -EFAULT;
		}
		else {
			hdcp_cancel_work(&hdcp->pending_start);
			hdcp_cancel_work(&hdcp->pending_wq_event);
		
			/* Post event to workqueue */
			if (hdcp_submit_work(HDCP_DISABLE_CTL, 0) == 0)
				return -EFAULT;
		}
		hdcp->enable = 	enable;
	}
	return count;
}

static DEVICE_ATTR(enable, S_IRUGO|S_IWUSR, hdcp_enable_read, hdcp_enable_write);

static ssize_t hdcp_trytimes_read(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	int trytimes = 0;
	
	if(hdcp)
		trytimes = hdcp->retry_times;
		
	return snprintf(buf, PAGE_SIZE, "%d\n", trytimes);
}

static ssize_t hdcp_trytimes_wrtie(struct device *device,
			   struct device_attribute *attr, const char *buf, size_t count)
{
	int trytimes;

	if(hdcp == NULL)
		return -EINVAL;
	
	sscanf(buf, "%d", &trytimes);
	if(hdcp->retry_times != trytimes)
		hdcp->retry_times = trytimes;
	
	return count;
}


static DEVICE_ATTR(trytimes, S_IRUGO|S_IWUSR, hdcp_trytimes_read, hdcp_trytimes_wrtie);


static struct miscdevice mdev;

static int __init rk2928_hdcp_init(void)
{
	int ret;
	
	DBG("[%s] %u", __FUNCTION__, jiffies_to_msecs(jiffies));
	
	hdcp = kmalloc(sizeof(struct hdcp), GFP_KERNEL);
	if(!hdcp)
	{
    	printk(KERN_ERR ">>HDCP: kmalloc fail!");
    	ret = -ENOMEM;
    	goto error0; 
	}
	memset(hdcp, 0, sizeof(struct hdcp));
	mutex_init(&hdcp->lock);
	
	mdev.minor = MISC_DYNAMIC_MINOR;
	mdev.name = "hdcp";
	mdev.mode = 0666;
	if (misc_register(&mdev)) {
		printk(KERN_ERR "HDCP: Could not add character driver\n");
		ret = HDMI_ERROR_FALSE;
		goto error1;
	}
	ret = device_create_file(mdev.this_device, &dev_attr_enable);
    if(ret)
    {
        printk(KERN_ERR "HDCP: Could not add sys file enable\n");
        ret = -EINVAL;
        goto error2;
    }
    
    ret = device_create_file(mdev.this_device, &dev_attr_trytimes);
    if(ret)
    {
        printk(KERN_ERR "HDCP: Could not add sys file trytimes\n");
        ret = -EINVAL;
        goto error3;
    }
    
    hdcp->workqueue = create_singlethread_workqueue("hdcp");
	if (hdcp->workqueue == NULL) {
		printk(KERN_ERR "HDCP,: create workqueue failed.\n");
		goto error4;
	}
    
    
    ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_NOHOTPLUG,
			      "hdcp.keys", mdev.this_device, GFP_KERNEL,
			      hdcp, hdcp_load_keys_cb);
	if (ret < 0) {
		printk(KERN_ERR "HDCP: request_firmware_nowait failed: %d\n", ret);
		goto error5;
	}
	
	rk2928_hdmi_register_hdcp_callbacks(hdcp_start_frame_cb,
										hdcp_irq_cb,
										hdcp_power_on_cb,
										hdcp_power_off_cb);
										
	DBG("%s success %u", __FUNCTION__, jiffies_to_msecs(jiffies));
	return 0;
	
error5:
	destroy_workqueue(hdcp->workqueue);
error4:
	device_remove_file(mdev.this_device, &dev_attr_trytimes);
error3:
	device_remove_file(mdev.this_device, &dev_attr_enable);
error2:
	misc_deregister(&mdev);
error1:
	if(hdcp->keys)
		kfree(hdcp->keys);
	if(hdcp->invalidkeys)
		kfree(hdcp->invalidkeys);
	kfree(hdcp);
error0:
	return ret;
}

static void __exit rk2928_hdcp_exit(void)
{
	device_remove_file(mdev.this_device, &dev_attr_enable);
	misc_deregister(&mdev);
	if(hdcp->keys)
		kfree(hdcp->keys);
	if(hdcp->invalidkeys)
		kfree(hdcp->invalidkeys);
	kfree(hdcp);
}

module_init(rk2928_hdcp_init);
module_exit(rk2928_hdcp_exit);
