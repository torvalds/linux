#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include "../rk30_hdmi.h"
#include "../rk30_hdmi_hw.h"
#include "rk30_hdmi_hdcp.h"

static struct hdcp *hdcp = NULL;

static void hdcp_work_queue(struct work_struct *work);

/*-----------------------------------------------------------------------------
 * Function: hdcp_submit_work
 *-----------------------------------------------------------------------------
 */
struct delayed_work *hdcp_submit_work(int event, int delay)
{
	struct hdcp_delayed_work *work;

	HDCP_DBG("%s event %04x delay %d", __FUNCTION__, event, delay);
	
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

	return;
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
	HDCP_DBG("%s hdcp->retry_cnt %d \n", __FUNCTION__, hdcp->retry_cnt);
	if (hdcp->hdmi_state == HDMI_STOPPED) {
		return;
	}

	rk30_hdcp_disable(hdcp);
	rk30_hdmi_control_output(false);
	
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

	HDCP_DBG("HDCP: authentication start");

	status = rk30_hdcp_start_authentication(hdcp);

	if (status != HDCP_OK) {
		HDCP_DBG("HDCP: authentication failed");
		hdcp_wq_authentication_failure();
	} else {
		hdcp->hdcp_state = HDCP_AUTHENTICATION_1ST;
	}
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_authentication_1st
 *-----------------------------------------------------------------------------
 */
static int hdcp_wq_authentication_1st(void)
{
	int status = HDCP_OK;

	HDCP_DBG("1st authen start");
	
	status = rk30_hdcp_authentication_1st(hdcp);

	if (status == -HDCP_DDC_ERROR)
		hdcp->pending_wq_event = hdcp_submit_work(HDCP_AUTH_START_1ST, 1000);
	else if (status != HDCP_OK) {
		printk(KERN_INFO "HDCP: 1st authen failed %d", status);
//		hdcp->retry_cnt = 0;
		hdcp_wq_authentication_failure();
	}
	else {
		HDCP_DBG("HDCP: 1st Authentication successful");
		hdcp->hdcp_state = HDCP_WAIT_R0_DELAY;
//		hdcp.auth_state = HDCP_STATE_AUTH_1ST_STEP;
	}
	return HDCP_OK;
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_check_r0
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_check_r0(void)
{
	int status = rk30_hdcp_lib_step1_r0_check(hdcp);

	if (status == -HDCP_CANCELLED_AUTH) {
		HDCP_DBG("Authentication step 1/R0 cancelled.");
		return;
	} else if (status < 0)
		hdcp_wq_authentication_failure();
	else {
		if (hdcp_lib_check_repeater_bit_in_tx(hdcp)) {
			/* Repeater */
			printk(KERN_INFO "HDCP: authentication step 1 "
					 "successful - Repeater\n");

			hdcp->hdcp_state = HDCP_WAIT_KSV_LIST;
//			hdcp.auth_state = HDCP_STATE_AUTH_2ND_STEP;

			hdcp->pending_wq_event =
				hdcp_submit_work(HDCP_AUTH_START_2ND, 0);
				
		} else {
			/* Receiver */
			printk(KERN_INFO "HDCP: authentication step 1 "
					 "successful - Receiver\n");

			hdcp->hdcp_state = HDCP_LINK_INTEGRITY_CHECK;
		}
	}
}
/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_check_bksv
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_step2_authentication(void)
{
	int status = HDCP_OK;

	HDCP_DBG("%s", __FUNCTION__);
	
	status = rk30_hdcp_authentication_2nd(hdcp);

	if (status == -HDCP_CANCELLED_AUTH) {
		HDCP_DBG("Authentication step 2nd cancelled.");
		return;
	}
	else if (status < 0) {
		printk(KERN_INFO "HDCP: step2 authentication failed");
		hdcp_wq_authentication_failure();
	}
	else {
		HDCP_DBG("HDCP: step2 authentication successful");

		hdcp->hdcp_state = HDCP_LINK_INTEGRITY_CHECK;
	}
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_authentication_3rd
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_authentication_3rd(void)
{
	int status = rk30_hdcp_lib_step3_r0_check(hdcp);

	if (status == -HDCP_CANCELLED_AUTH) {
		HDCP_DBG("Authentication step 3/Ri cancelled.");
		return;
	} else if (status < 0)
		hdcp_wq_authentication_failure();
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_disable
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_disable(int event)
{
	printk(KERN_INFO "HDCP: disabled");

	hdcp_cancel_work(&hdcp->pending_wq_event);
	rk30_hdcp_disable(hdcp);
	if(event == HDCP_DISABLE_CTL) {
		hdcp->hdcp_state = HDCP_DISABLED;
		if(hdcp->hdmi_state == HDMI_STARTED)
			rk30_hdmi_control_output(true);			
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
	
	HDCP_DBG("hdcp_work_queue() - START - %u hdmi=%d hdcp=%d evt= %x %d",
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
		if(hdcp->retry_times == 0)
			hdcp->retry_cnt = HDCP_INFINITE_REAUTH;
		else
			hdcp->retry_cnt = hdcp->retry_times;
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
		
		case HDCP_AUTHENTICATION_1ST:
			if(event == HDCP_AUTH_START_1ST)
				hdcp_wq_authentication_1st();
			break;
		
		case HDCP_WAIT_R0_DELAY:
			if(event == HDCP_R0_EXP_EVENT)
				hdcp_wq_check_r0();
			break;
			
		case HDCP_WAIT_KSV_LIST:
			/* KSV failure */
			if (event == HDCP_FAIL_EVENT) {
				printk(KERN_INFO "HDCP: KSV switch failure\n");
	
				hdcp_wq_authentication_failure();
			}
			/* KSV list ready event */
			else if (event == HDCP_AUTH_START_2ND)
				hdcp_wq_step2_authentication();
			break;
		
		case HDCP_LINK_INTEGRITY_CHECK:
			/* Ri failure */
			if (event == HDCP_FAIL_EVENT) {
				printk(KERN_INFO "HDCP: Ri check failure\n");
				hdcp_wq_authentication_failure();
			}
			else if(event == HDCP_RI_EXP_EVENT)
				hdcp_wq_authentication_3rd();
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
	HDCP_DBG("hdcp_start_frame_cb()");

	/* Cancel any pending work */
	if (hdcp->pending_start)
		hdcp_cancel_work(&hdcp->pending_start);
	if (hdcp->pending_wq_event)
		hdcp_cancel_work(&hdcp->pending_wq_event);
	hdcp->pending_disable = 0;
	hdcp->pending_start = hdcp_submit_work(HDCP_START_FRAME_EVENT,
							HDCP_ENABLE_DELAY);
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_irq_cb
 *-----------------------------------------------------------------------------
 */
static void hdcp_irq_cb(int interrupt)
{
	rk30_hdcp_irq(hdcp);
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_power_on_cb
 *-----------------------------------------------------------------------------
 */
static int hdcp_power_on_cb(void)
{
	HDCP_DBG("%s", __FUNCTION__);
	return rk30_hdcp_load_key2mem(hdcp, hdcp->keys);
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_power_off_cb
 *-----------------------------------------------------------------------------
 */
static void hdcp_power_off_cb(void)
{
	HDCP_DBG("%s", __FUNCTION__);
	if(!hdcp->enable)
		return;
	
	hdcp_cancel_work(&hdcp->pending_start);
	hdcp_cancel_work(&hdcp->pending_wq_event);
	hdcp->pending_disable = 1;
	init_completion(&hdcp->complete);
	/* Post event to workqueue */
	if (hdcp_submit_work(HDCP_STOP_FRAME_EVENT, 0))	
		wait_for_completion_interruptible_timeout(&hdcp->complete,
							msecs_to_jiffies(2000));
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
	
	rk30_hdcp_load_key2mem(hdcp, hdcp->keys);
	printk(KERN_INFO "HDCP: loaded hdcp key success\n");

	if(fw->size > HDCP_KEY_SIZE) {
		HDCP_DBG("%s invalid key size %d", __FUNCTION__, fw->size - HDCP_KEY_SIZE);
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

static int __init rk30_hdcp_init(void)
{
	int ret;
	
	HDCP_DBG("[%s] %u", __FUNCTION__, jiffies_to_msecs(jiffies));
	
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
	
	hdcp->hdmi = rk30_hdmi_register_hdcp_callbacks(	hdcp_start_frame_cb,
										hdcp_irq_cb,
										hdcp_power_on_cb,
										hdcp_power_off_cb);
										
	HDCP_DBG("%s success %u", __FUNCTION__, jiffies_to_msecs(jiffies));
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

static void __exit rk30_hdcp_exit(void)
{
	if(hdcp) {
		mutex_lock(&hdcp->lock);
		rk30_hdmi_register_hdcp_callbacks(0, 0, 0, 0);
		device_remove_file(mdev.this_device, &dev_attr_enable);
		misc_deregister(&mdev);
		destroy_workqueue(hdcp->workqueue);
		if(hdcp->keys)
			kfree(hdcp->keys);
		if(hdcp->invalidkeys)
			kfree(hdcp->invalidkeys);
		mutex_unlock(&hdcp->lock);
		kfree(hdcp);
	}
}

//module_init(rk30_hdcp_init);
device_initcall_sync(rk30_hdcp_init);
module_exit(rk30_hdcp_exit);