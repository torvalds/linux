#include <linux/firmware.h>
#include "core.h"
#include "fw.h"
//#include "debug.h"
#include "ecrnx_platform.h"
#include "usb.h"
#include "ecrnx_rx.h"
#include "eswin_utils.h"
#include "ecrnx_defs.h"
#include "usb_host_interface.h"

bool loopback;
module_param(loopback, bool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(loopback, "HIF loopback");

int power_save;
module_param(power_save, int, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(power_save, "Power Save(0: disable, 1:enable)");

int disable_cqm = 0;
module_param(disable_cqm, int, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(disable_cqm, "Disable CQM (0: disable, 1:enable)");


int listen_interval = 0;
module_param(listen_interval, int, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(listen_interval, "Listen Interval");

int bss_max_idle = 0;
module_param(bss_max_idle, int, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(bss_max_idle, "BSS Max Idle");


bool dl_fw = true;
module_param(dl_fw, bool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(dl_fw, "download firmware");


#ifdef CONFIG_ECRNX_WIFO_CAIL
bool amt_mode;
module_param(amt_mode, bool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(amt_mode, "calibrate mode");
#endif

bool set_gain;
module_param(set_gain, bool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(set_gain, "set gain delta");

char *fw_name = NULL;
struct eswin *pEswin = NULL;
bool usb_status = false;

module_param(fw_name, charp, S_IRUGO);
MODULE_PARM_DESC(fw_name, "Firmware file name");

extern int ecrnx_data_cfm_callback(void *priv, void *host_id);
extern int ecrnx_msg_cfm_callback(void *priv, void *host_id);
static void eswin_core_register_work(struct work_struct *work)
{
	int ret;
	struct eswin *tr = container_of(work, struct eswin, register_work.work);

	ECRNX_DBG("%s entry, dl_fw = %d!!", __func__, dl_fw);

    if(dl_fw){
     	if (fw_name) {
    		ECRNX_PRINT("fw file name: %s\n",fw_name);
    	}
    	else {
    		fw_name = "ECR6600U_transport.bin";
    	}
    }
	
	if (dl_fw  && eswin_fw_file_chech(tr)) {
	    ECRNX_DBG("%s entry, start fw download!!", __func__);
        if( eswin_fw_file_download(tr) < 0)
        {
            release_firmware(tr->fw);
            return;
        } 
        release_firmware(tr->fw);
		dl_fw = false;
		ECRNX_DBG("%s entry, finish and stop fw download!!", __func__);
		schedule_delayed_work(&tr->register_work, msecs_to_jiffies(1000));
		return;
	}

#ifdef CONFIG_ECRNX_WIFO_CAIL
	ECRNX_DBG("%s entry, amt_mode = %d!!", __func__, amt_mode);
#endif

	tr->rx_callback = ecrnx_rx_callback;
	tr->data_cfm_callback = ecrnx_data_cfm_callback;
	tr->msg_cfm_callback = ecrnx_msg_cfm_callback;
	tr->ops->start(tr);
	ret = ecrnx_platform_init(tr, &tr->umac_priv);
	set_bit(ESWIN_FLAG_CORE_REGISTERED, &tr->dev_flags);

    ECRNX_DBG("%s exit!!", __func__);

    return;
}
bool register_status = false;

int eswin_core_register(struct eswin *tr)
{
    if(register_status == true)
    {
        return 0;
    }
    register_status = true;
	ECRNX_DBG("%s entry!!", __func__);
	schedule_delayed_work(&tr->register_work, msecs_to_jiffies(1));
	return 0;
}

void eswin_core_unregister(struct eswin *tr)
{
	ECRNX_DBG("%s entry!!", __func__);
    if(register_status == false)
    {
        return;
    }
    register_status = false;
    msleep(20);
	cancel_delayed_work_sync(&tr->register_work);

	if (!test_bit(ESWIN_FLAG_CORE_REGISTERED, &tr->dev_flags))
		return;
    tr->rx_callback = NULL;
    ecrnx_platform_deinit(tr->umac_priv);
    eswin_core_destroy(tr);
}

int usb_host_send(void *buff, int len, int flag)
{
	int ret = -1;
	struct eswin * tr= pEswin;
	struct sk_buff *skb = NULL;

    //ECRNX_DBG("%s-%d: flag:0x%08x, mask:0x%x, desc:0x%x, type:0x%x \n", __func__, __LINE__, flag, FLAG_MSG_TYPE_MASK, TX_FLAG_TX_DESC, (flag & FLAG_MSG_TYPE_MASK));
	if((u8_l)(flag & FLAG_MSG_TYPE_MASK) == TX_FLAG_TX_DESC)
	{
	    skb = (struct sk_buff*)buff;
	}
	else
	{
        skb = dev_alloc_skb(len + sizeof(int)); //add the flag length (len + 4)

        memcpy(skb->data, (char*)&flag, sizeof(int));
        memcpy((char*)skb->data + sizeof(int), buff, len); //put rx desc tag to skb
        skb->len = len + sizeof(int);
	}

	ECRNX_DBG("usb_host_send, skb:0x%08x, skb_len:%d, frame_len:%d, flag:0x%08x \n", skb, skb->len, len, flag);

	if (tr->ops->xmit) {
		ret = tr->ops->xmit(tr, skb);
	} else {
		ECRNX_ERR("eswin_sdio_work error, ops->xmit is null\n");
	}

	return ret;
}

extern void ecrnx_send_handle_register(void * fn);
struct eswin * eswin_core_create(size_t priv_size, struct device *dev, struct usb_ops * ops)
{
	struct eswin * tr;

	tr = (struct eswin *)kzalloc(sizeof(struct eswin) + priv_size, GFP_KERNEL);
	if(!tr) {
		return NULL;
	}

	pEswin = tr;

	tr->dev = dev;
	tr->ops = ops;

	ecrnx_send_handle_register(usb_host_send);

    if(usb_status == false)
    {
        INIT_DELAYED_WORK(&tr->register_work, eswin_core_register_work);
        usb_status = true;
    }

	tr->state = ESWIN_STATE_INIT;

	ECRNX_DBG(" %s exit!!", __func__);
	return tr;

}

void eswin_core_destroy(struct eswin *tr)
{
	tr->state = ESWIN_STATE_CLOSEED;

	ECRNX_DBG("%s entry!!", __func__);
    usb_status = false;

	//flush_workqueue(tr->workqueue);
	//destroy_workqueue(tr->workqueue);
    //TODO:
	//eswin_mac_destroy(tr);
}


//MODULE_AUTHOR("Transa-Semi");
//MODULE_LICENSE("Dual BSD/GPL");
//MODULE_DESCRIPTION("Core module for Transa-Semi 802.11 WLAN SDIO driver");
