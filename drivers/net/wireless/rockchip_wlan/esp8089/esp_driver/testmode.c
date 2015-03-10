/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * test mode    
 */

#ifdef TEST_MODE

#include <linux/kernel.h>
#include <linux/etherdevice.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/nl80211.h>
#include <linux/ieee80211.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/mmc/host.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/genetlink.h>
#include "esp_pub.h"
#include "esp_sip.h"
#include "esp_ctrl.h"
#include "esp_sif.h"
#include "esp_debug.h"
#include "esp_wl.h"
#include "testmode.h"
#include "esp_path.h"
#include "esp_file.h"
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
    #include <net/regulatory.h>
#endif

static int queue_flag = 0;

static u32 connected_nl;
static struct genl_info info_copy;
static struct esp_sip *sip_copy = NULL;
static u8 *sdio_buff = NULL;
static struct sdiotest_param sdioTest;
static u8 *sdiotest_buf = NULL;

#define SIP sip_copy
#define OUT_DONE() \
        do { \
 	     printk(KERN_DEBUG "esp_sdio: error occured in %s\n", __func__); \
        } while(0)

/* ESP TEST netlinf family */
static struct genl_family test_genl_family = {
        .id = GENL_ID_GENERATE,
        .hdrsize = 0,
        .name = "esp_sdio",
        .version = 1,
        .maxattr = TEST_ATTR_MAX,
};

struct loopback_param_s {
        u32 packet_num;
        u32 packet_id;
};

static struct loopback_param_s loopback_param;
u32 get_loopback_num()
{
        return loopback_param.packet_num;
}

u32 get_loopback_id()
{
        return loopback_param.packet_id;
}

void inc_loopback_id()
{
        loopback_param.packet_id++;
}

#define REGISTER_REPLY(info) \
        memcpy((char *)&info_copy, (char *)(info), sizeof(struct genl_info))


static void sip_send_test_cmd(struct esp_sip *sip, struct sk_buff *skb)
{
	if (queue_flag == 0)
        	skb_queue_tail(&sip->epub->txq, skb);
	else
        	skb_queue_head(&sip->epub->txq, skb);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
        if(sif_get_ate_config() == 0){
            ieee80211_queue_work(sip->epub->hw, &sip->epub->tx_work);
        } else {
            queue_work(sip->epub->esp_wkq, &sip->epub->tx_work);
        } 
#else
        queue_work(sip->epub->esp_wkq, &sip->epub->tx_work);
#endif

}

static int esp_test_cmd_reply(struct genl_info *info, u32 cmd_type, char *reply_info)
{
        struct sk_buff *skb;
        void *hdr;

        /*directly send ask_info to target, and waiting for report*/
        skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
        if (skb == NULL)
                goto out;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
        hdr = genlmsg_put(skb,  info->snd_portid, info->snd_seq, &test_genl_family, 0, cmd_type);
#else
        hdr = genlmsg_put(skb,  info->snd_pid, info->snd_seq, &test_genl_family, 0, cmd_type);
#endif
        if (hdr == NULL)
                goto nla_put_failure;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
        nla_put_string(skb, TEST_ATTR_STR, reply_info);
#else
        NLA_PUT_STRING(skb, TEST_ATTR_STR, reply_info);
#endif
        genlmsg_end(skb, hdr);
        genlmsg_reply(skb, info);
        return 0;

nla_put_failure:
        nlmsg_free(skb);
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_echo(struct sk_buff *skb_2,
                         struct genl_info *info)
{
        char *echo_info;
        int res;

        if (info == NULL)
                goto out;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
        connected_nl = info->snd_portid;
	printk(KERN_DEBUG "esp_sdio: received a echo, "
               "from portid %d\n", info->snd_portid);
#else
        connected_nl = info->snd_pid;
        esp_dbg(ESP_DBG_ERROR, "esp_sdio: received a echo, "
               "from pid %d\n", info->snd_pid);
#endif
	sip_debug_show(SIP);
	
        /*get echo info*/
        echo_info = nla_data(info->attrs[TEST_ATTR_STR]);

	if (strncmp(echo_info, "queue_head", 10) == 0) {
        	esp_dbg(ESP_DBG_ERROR, "echo : change to queue head");
		queue_flag = 1;
	}
	if (strncmp(echo_info, "queue_tail", 10) == 0) {
        	esp_dbg(ESP_DBG_ERROR, "echo : change to queue head");
		queue_flag = 0;
	}

        res=esp_test_cmd_reply(info, TEST_CMD_ECHO, echo_info);
        return res;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_sdiospeed(struct sk_buff *skb_2,
                              struct genl_info *info)
{
        char *speed_info;
        int res;

        if (info == NULL)
                goto out;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
        connected_nl = info->snd_portid;
#else
        connected_nl = info->snd_pid;
#endif
        /*get echo info*/
        speed_info = nla_data(info->attrs[TEST_ATTR_STR]);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
        esp_dbg(ESP_DBG_ERROR, "esp_sdio: received a sdio speed %s, "
               "from portid %d\n", speed_info, info->snd_portid);
#else
        esp_dbg(ESP_DBG_ERROR, "esp_sdio: received a sdio speed %s, "
               "from pid %d\n", speed_info, info->snd_pid);
#endif
        if (!strcmp(speed_info, "high")) {
                sif_platform_target_speed(1);
        } else if (!strcmp(speed_info, "low")) {
                sif_platform_target_speed(0);
        } else {
                esp_dbg(ESP_DBG_ERROR, "%s:  %s unsupported\n", __func__, speed_info);
        }

        res=esp_test_cmd_reply(info, TEST_CMD_SDIOSPEED, speed_info);
        return res;
out:
        OUT_DONE();
        return -EINVAL;
}

static void * ate_done_data;
static char ate_reply_str[128];
void esp_test_ate_done_cb(char *ep)
{
	memset(ate_reply_str, 0, sizeof(ate_reply_str));
	strcpy(ate_reply_str, ep);

	esp_dbg(ESP_DBG_ERROR, "%s %s\n", __func__, ate_reply_str);

	if (ate_done_data)
		complete(ate_done_data);
}

static int esp_test_ate(struct sk_buff *skb_2,
                         struct genl_info *info)
{
        char *ate_info = NULL;
	u16 len = 0;
        int res = 0;
	struct sk_buff *skb;
	char *str;
	bool stop_sdt = false;

	DECLARE_COMPLETION_ONSTACK(complete);

        if (info == NULL)
                goto out;


	ate_done_data = &complete;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
        connected_nl = info->snd_portid;
#else
        connected_nl = info->snd_pid;
#endif

        //esp_dbg(ESP_DBG_ERROR, "esp_sdio: received a ate cmd, "
        //       "from pid %d\n", info->snd_pid);

        /*get echo info*/
        ate_info = nla_data(info->attrs[TEST_ATTR_STR]);
	if (!ate_info)
                goto out;

	len = nla_len(info->attrs[TEST_ATTR_STR]);

	//esp_dbg(ESP_DBG_ERROR, "%s str %s len %d cmd %d\n", __func__, ate_info, len, SIP_CMD_ATE);

	skb = sip_alloc_ctrl_skbuf(SIP, (sizeof(struct sip_hdr)+len), SIP_CMD_ATE);
        if (!skb)
                goto out;

        //atecmd = (struct sip_cmd_ate*)(skb->data + sizeof(struct sip_hdr));
	str = (char *)(skb->data + sizeof(struct sip_hdr));
        //atecmd->len = len;
	strcpy(str, ate_info);
	//esp_dbg(ESP_DBG_ERROR, "%s cmdstr %s \n", __func__, str);

	if (atomic_read(&sdioTest.start)) {
		atomic_set(&sdioTest.start, 0);
		stop_sdt = true;
		msleep(10);
		//esp_dbg(ESP_DBG_ERROR, "%s stop sdt \n", __func__);
	}

        sip_send_test_cmd(SIP, skb);
	//esp_dbg(ESP_DBG_ERROR, "%s sent cmd \n", __func__);

	wait_for_completion(&complete);
	
	//esp_dbg(ESP_DBG_ERROR, "%s completed \n", __func__);

        esp_test_cmd_reply(info, TEST_CMD_ATE, ate_reply_str);

	if (stop_sdt) {
		//esp_dbg(ESP_DBG_ERROR, "%s restart sdt \n", __func__);
		atomic_set(&sdioTest.start, 1);
		wake_up_process(sdioTest.thread);
	}

        return res;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_process_sdio_test(struct sdiotest_param *param)
{
//#define sdiotest_BASE_ADDR 0x7f80 //MAC 5.0
#define SDIOTEST_FLAG_ADDR 0xc000
#define SDIOTEST_BASE_ADDR (SDIOTEST_FLAG_ADDR+4) //MAC 6.0
#define SDIO_BUF_SIZE (16*1024-4)

	int ret = 0;
	static int counter = 0;

	//esp_dbg(ESP_DBG_ERROR, "idle_period %d mode %d addr 0x%08x\n", param->idle_period,
//        						param->mode, param->addr);
	if (sdioTest.mode == 1) { //read mode
		ret = esp_common_read_with_addr(SIP->epub, SDIOTEST_BASE_ADDR, sdiotest_buf, SDIO_BUF_SIZE, ESP_SIF_SYNC);

	} else if ((sdioTest.mode >= 3)&&(sdioTest.mode <= 7)) { //write mode
		ret = esp_common_write_with_addr(SIP->epub, SDIOTEST_BASE_ADDR, sdiotest_buf, SDIO_BUF_SIZE, ESP_SIF_SYNC);
/*
	} else if (sdioTest.mode == 3) { //read & write mode
		ret = esp_common_read_with_addr(SIP->epub, SDIOTEST_BASE_ADDR, sdiotest_buf, 16*1024, ESP_SIF_SYNC);
		sdiotest_buf[0]++;
		ret = esp_common_write_with_addr(SIP->epub, SDIOTEST_BASE_ADDR, sdiotest_buf, 16*1024, ESP_SIF_SYNC);
*/
	} else if (sdioTest.mode == 2) { //byte read mode(to test sdio_cmd)
		ret = esp_common_read_with_addr(SIP->epub, SDIOTEST_BASE_ADDR, sdiotest_buf, 8, ESP_SIF_SYNC);
        }
	
	if (sdioTest.idle_period > 1000) {
		show_buf(sdiotest_buf, 128);
	} else if (sdioTest.idle_period == 0) {
		if (sdioTest.mode == 1) {//read mode
			if (!(counter++%5000)) {
				esp_dbg(ESP_DBG_ERROR, "%s %d\n", __func__, counter);
			}
                } else if (sdioTest.mode == 2) { //byte read mode
                        if (!(counter++%5000)) {
                                esp_dbg(ESP_DBG_ERROR, "%s %d\n", __func__, counter);
                        }
		} else {//write mode
			//msleep(3);
			if (!(counter++%30000)) {
				esp_dbg(ESP_DBG_ERROR, "%s %d\n", __func__, counter);
			}
		}
	} else {
		if (!(counter++%1000)) {
			esp_dbg(ESP_DBG_ERROR, "%s %d\n", __func__, counter);
		}
	}


	if (ret)
		esp_dbg(ESP_DBG_ERROR, "%s mode %d err %d \n", __func__, sdioTest.mode, ret);	

	return ret;
}	

static int esp_test_sdiotest_thread(void *param)
{
	struct sdiotest_param *testParam = (struct sdiotest_param *)param;
	struct sched_param schedParam = { .sched_priority = 1 };
	unsigned long idle_period = MAX_SCHEDULE_TIMEOUT;
	int ret = 0;

	sched_setscheduler(current, SCHED_FIFO, &schedParam);

	while (!kthread_should_stop()) {
		
		if (0 == atomic_read(&testParam->start)) {
			esp_dbg(ESP_DBG_ERROR, "%s suspend\n", __func__);
			set_current_state(TASK_INTERRUPTIBLE);
			if (!kthread_should_stop())
				schedule_timeout(MAX_SCHEDULE_TIMEOUT);
			set_current_state(TASK_RUNNING);
		}

		if (testParam->idle_period)
			idle_period = msecs_to_jiffies(testParam->idle_period);
		else 
			idle_period = 0;

		ret = esp_process_sdio_test(testParam);

		/*
		 * Give other threads a chance to run in the presence of
		 * errors.
		 */
		if (ret < 0) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!kthread_should_stop())
				schedule_timeout(2*HZ);
			set_current_state(TASK_RUNNING);
			atomic_set(&testParam->start, 0);
		}

		//esp_dbg(ESP_DBG_ERROR, "%s idle_period %lu\n", __func__, idle_period);
		if (idle_period) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!kthread_should_stop()) {
				schedule_timeout(idle_period);
			}
			set_current_state(TASK_RUNNING);
		}
	};


	esp_dbg(ESP_DBG_ERROR, "%s exit\n", __func__);

	return ret;

}

static int esp_test_sdiotest(struct sk_buff *skb_2,
                         struct genl_info *info)
{
        int res = 0;
	char reply_str[32];
	u32 start = 0;
	int para_num = 0;
	int i;
        u32 data_mask = 0xffffffff;
        u8 * sdio_test_flag = NULL;
        int ret = 0;
	bool stop_sdt = false;

        if (info == NULL)
                goto out;

	start = nla_get_u32(info->attrs[TEST_ATTR_PARA0]);
	esp_dbg(ESP_DBG_ERROR, "%s start 0x%08x\n", __func__, start);
	para_num = start & 0xf;
	start >>= 31;
	if (!start)
		goto _turnoff;

	esp_dbg(ESP_DBG_ERROR, "%s paranum %d start %u\n", __func__, para_num, start);
	para_num--;

	do {
		if ((para_num--) > 0) {
			sdioTest.mode = nla_get_u32(info->attrs[TEST_ATTR_PARA1]);
			if ((sdioTest.mode >= 3)&&(sdioTest.mode <= 7)) { // write mode, fill the test buf
                                data_mask = (sdioTest.mode == 3)? 0xffffffff : (0x11111111<<(sdioTest.mode-4));
				for (i = 0; i<SDIO_BUF_SIZE/ 4; i++) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))

					*(u32 *)&sdiotest_buf[i*4] = get_random_int() & data_mask;
#else
					*(u32 *)&sdiotest_buf[i*4] = random32() & data_mask;
#endif
				}
			}
			esp_dbg(ESP_DBG_ERROR, "%s mode %d \n", __func__, sdioTest.mode);
		}

		if ((para_num--) > 0) {
			sdioTest.addr = nla_get_u32(info->attrs[TEST_ATTR_PARA2]);
			esp_dbg(ESP_DBG_ERROR, "%s addr 0x%x \n", __func__, sdioTest.addr);
		}

		if ((para_num--) > 0) {
			sdioTest.idle_period = nla_get_u32(info->attrs[TEST_ATTR_PARA3]);
			esp_dbg(ESP_DBG_ERROR, "%s idle_period %u \n", __func__, sdioTest.idle_period);
		}
	} while (0);
        
	esp_test_cmd_reply(info, TEST_CMD_SDIOTEST, reply_str);
	msleep(10);
  
#if 1
	if (start == 1) {
		//esp_dbg(ESP_DBG_ERROR, "%s start== 1\n", __func__);
                
                if((sdioTest.mode == 8)|(sdioTest.mode == 9)) {
                   
                   if (atomic_read(&sdioTest.start)) {
                      atomic_set(&sdioTest.start, 0);
                      stop_sdt = true;
                      msleep(10);
                   }

	           sdio_test_flag = kzalloc(4, GFP_KERNEL);
		   if (sdio_test_flag == NULL) {
		      esp_dbg(ESP_DBG_ERROR, "no mem for sdio_tst_flag!!\n");
		      goto out;
                   }

                   if(sdioTest.mode == 8){
                      ret = esp_common_read_with_addr(SIP->epub, sdioTest.addr, sdio_test_flag, 4, ESP_SIF_SYNC);

                      esp_dbg(ESP_DBG_ERROR, "%s sdio read: 0x%x 0x%x\n", __func__, sdioTest.addr, *(u32 *)sdio_test_flag);
                   }else{

		      *(u32 *)sdio_test_flag = sdioTest.idle_period;

                      ret = esp_common_write_with_addr(SIP->epub, sdioTest.addr, sdio_test_flag, 4, ESP_SIF_SYNC);
                      
                      esp_dbg(ESP_DBG_ERROR, "%s sdio : write 0x%x 0x%x done!\n", __func__, sdioTest.addr, *(u32 *)sdio_test_flag);
                   }

                   kfree(sdio_test_flag);
                   sdio_test_flag = NULL; 

                } else {
                   //set flag to inform firmware sdio-test start
		   sdio_test_flag = kzalloc(4, GFP_KERNEL);
		   if (sdio_test_flag == NULL) {
			esp_dbg(ESP_DBG_ERROR, "no mem for sdio_tst_flag!!\n");
			goto out;
		   } 
               
		   *(u32 *)sdio_test_flag = 0x56781234;
                   if (atomic_read(&sdioTest.start)) {
                      atomic_set(&sdioTest.start, 0);
                      stop_sdt = true;
                      msleep(10);
                      //esp_dbg(ESP_DBG_ERROR, "%s stop sdt \n", __func__);
                   }
                   ret = esp_common_write_with_addr(SIP->epub, SDIOTEST_FLAG_ADDR, sdio_test_flag, 4, ESP_SIF_SYNC);
/*
                if (stop_sdt) {
                   //esp_dbg(ESP_DBG_ERROR, "%s restart sdt \n", __func__);
                   atomic_set(&sdioTest.start, 1);
                   wake_up_process(sdioTest.thread);
                }
*/
		//esp_dbg(ESP_SHOW, "%s sdio_test_flag sent to target \n", __func__);
		//esp_dbg(ESP_DBG_ERROR, "%s sdio_test_flag sent to target \n", __func__);
		   kfree(sdio_test_flag);
		   sdio_test_flag = NULL;

		   atomic_set(&sdioTest.start, 1);
		   if (sdioTest.thread == NULL) {
			sdioTest.thread = kthread_run(esp_test_sdiotest_thread, 
				&sdioTest, 
				"kespsdiotestd");
		   } else {
			wake_up_process(sdioTest.thread);
		   }
		   strcpy(reply_str, "sdt started\n");
                }
	} else {
		//esp_dbg(ESP_DBG_ERROR, "%s start== 0\n", __func__);
_turnoff:		
		atomic_set(&sdioTest.start, 0);
		strcpy(reply_str, "sdt stopped\n");
	}
#endif


        return res;
out:
        OUT_DONE();
        return -EINVAL;
}


static int esp_test_ask(struct sk_buff *skb_2,
                        struct genl_info *info)
{
        char *ask_info;
        int res;

        if (info == NULL)
                goto out;

        /*get echo info*/
        ask_info = nla_data(info->attrs[TEST_ATTR_STR]);

        /*directly send ask_info to target, and waiting for report*/
        res=esp_test_cmd_reply(info, TEST_CMD_ASK, "ok");
        return res;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_sleep(struct sk_buff *skb_2,
                          struct genl_info *info)
{
        struct sip_cmd_sleep *sleepcmd;
        struct sk_buff *skb = NULL;
        int res;

        if (info == NULL)
                goto out;

        skb = sip_alloc_ctrl_skbuf(SIP, sizeof(struct sip_cmd_sleep), SIP_CMD_SLEEP);
        if (!skb)
                goto out;

        sleepcmd = (struct sip_cmd_sleep *)(skb->data + sizeof(struct sip_tx_info));
        sleepcmd->sleep_mode       =  nla_get_u32(info->attrs[TEST_ATTR_PARA0]);
        sleepcmd->sleep_tm_ms     =  nla_get_u32(info->attrs[TEST_ATTR_PARA1]);
        sleepcmd->wakeup_tm_ms  =  nla_get_u32(info->attrs[TEST_ATTR_PARA2]);
        sleepcmd->sleep_times       =  nla_get_u32(info->attrs[TEST_ATTR_PARA3]);

        sip_send_test_cmd(SIP, skb);

        /*directly send ask_info to target, and waiting for report*/
        res=esp_test_cmd_reply(info, TEST_CMD_SLEEP, "ok");
        return res;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_wakeup(struct sk_buff *skb_2,
                           struct genl_info *info)
{
        struct sip_cmd_wakeup *wakeupcmd;
        struct sk_buff *skb = NULL;
        //int res;

        if (info == NULL)
                goto out;

        skb = sip_alloc_ctrl_skbuf(SIP, sizeof(struct sip_cmd_wakeup), SIP_CMD_WAKEUP);
        if (!skb)
                goto out;
        wakeupcmd = (struct sip_cmd_wakeup *)(skb->data + sizeof(struct sip_tx_info));
        wakeupcmd->check_data = nla_get_u32(info->attrs[TEST_ATTR_PARA0]);

        /*directly send reply_info to target, and waiting for report*/
        REGISTER_REPLY(info);
        //res=esp_test_cmd_reply(info, TEST_CMD_WAKEUP, "ok");

        sip_send_test_cmd(SIP, skb);
        return 0;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_loopback(struct sk_buff *skb_2,
                             struct genl_info *info)
{
        u32 txpacket_len;
        u32 rxpacket_len;

        if (info == NULL)
                goto out;

        txpacket_len   = nla_get_u32(info->attrs[TEST_ATTR_PARA0]);
        rxpacket_len   = nla_get_u32(info->attrs[TEST_ATTR_PARA1]);
        loopback_param.packet_num    = nla_get_u32(info->attrs[TEST_ATTR_PARA2]);
        loopback_param.packet_id=0;
        REGISTER_REPLY(info);
        return sip_send_loopback_mblk(SIP, txpacket_len, rxpacket_len, 0);
out:
        OUT_DONE();
        return -EINVAL;
}

/*
u8 probe_req_frm[] = {0x40,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x03,0x8F,0x11,0x22,0x88,
                      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x01,0x08,0x82,0x84,0x8B,0x96,
                      0x0C,0x12,0x18,0x24,0x32,0x04,0x30,0x48,0x60,0x6C
                     };
*/

static int sip_send_tx_frame(struct esp_sip *sip, u32 packet_len)
{

        struct sk_buff *skb = NULL;
        u8 *ptr = NULL;
        int i;

        skb = alloc_skb(packet_len, GFP_KERNEL);
        skb->len = packet_len;
        ptr = skb->data;
        /* fill up pkt payload */
        for (i = 0; i < skb->len; i++) {
                ptr[i] = i;
        }

		if(sif_get_ate_config() == 0){
			sip_tx_data_pkt_enqueue(sip->epub, skb);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
        	ieee80211_queue_work(sip->epub->hw, &sip->epub->tx_work);
#else
        	queue_work(sip->epub->esp_wkq, &sip->epub->tx_work);
#endif
        } else {
        	skb_queue_tail(&sip->epub->txq, skb);
            queue_work(sip->epub->esp_wkq, &sip->epub->tx_work);
        }
    
        return 0;
}

static int esp_test_tx(struct sk_buff *skb_2,
                       struct genl_info *info)
{
        u32 txpacket_len;
        u32 res;

        if (info == NULL)
                goto out;

        txpacket_len     = nla_get_u32(info->attrs[TEST_ATTR_PARA0]);
        REGISTER_REPLY(info);
        sip_send_tx_frame(SIP, txpacket_len);
        res=esp_test_cmd_reply(info, TEST_CMD_TX, "tx out");
        return res;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_genl(struct sk_buff *skb_2,
                         struct genl_info *info)
{
        struct sip_cmd_debug *dbgcmd;
        struct sk_buff *skb = NULL;
        int i;

        if (info == NULL)
                goto out;

        skb = sip_alloc_ctrl_skbuf(SIP, sizeof(struct sip_hdr) + sizeof(struct sip_cmd_debug), SIP_CMD_DEBUG);
        if (!skb)
                goto out;

        dbgcmd = (struct sip_cmd_debug *)(skb->data + sizeof(struct sip_hdr));
        dbgcmd->cmd_type = nla_get_u32(info->attrs[TEST_ATTR_CMD_TYPE]);
        dbgcmd->para_num = nla_get_u32(info->attrs[TEST_ATTR_PARA_NUM]);
        esp_dbg(ESP_DBG_ERROR, "%s dbgcmdType %d paraNum %d\n", __func__, dbgcmd->cmd_type, dbgcmd->para_num);
        for (i=0; i<dbgcmd->para_num; i++)
                dbgcmd->para[i] = nla_get_u32(info->attrs[TEST_ATTR_PARA(i)]);

        /*directly send reply_info to target, and waiting for report*/
        REGISTER_REPLY(info);

        sip_send_test_cmd(SIP, skb);
        return 0;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_sdio_wr(struct sk_buff *skb_2,
                            struct genl_info *info)
{
        int res;
        u32 func_no, addr, value;

        if (info == NULL)
                goto out;

        func_no = nla_get_u32(info->attrs[TEST_ATTR_PARA0]);
        addr = nla_get_u32(info->attrs[TEST_ATTR_PARA1]);
        value = nla_get_u32(info->attrs[TEST_ATTR_PARA2]);

        if(!func_no) {
		res = esp_common_writebyte_with_addr(SIP->epub, addr, (u8)value, ESP_SIF_SYNC);
        } else {
                memcpy(sdio_buff, (u8 *)&value, 4);
		res = esp_common_write_with_addr(SIP->epub, addr, sdio_buff, 4, ESP_SIF_SYNC);
        }

        /*directly send reply_info to target, and waiting for report*/
        REGISTER_REPLY(info);
        if (!res)
                esp_test_cmd_reply(info, TEST_CMD_SDIO_WR, "write ok!");
        else
                esp_test_cmd_reply(info, TEST_CMD_SDIO_WR, "write fail!");

out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_sdio_rd(struct sk_buff *skb_2,
                            struct genl_info *info)
{
        int res;
        u32 func_no, addr, value;
        char value_str[12];

        if (info == NULL)
                goto out;

        func_no = nla_get_u32(info->attrs[TEST_ATTR_PARA0]);
        addr = nla_get_u32(info->attrs[TEST_ATTR_PARA1]);

        if(!func_no) {
                memset(sdio_buff, 0, 4);
                res = esp_common_readbyte_with_addr(SIP->epub, addr, &sdio_buff[0], ESP_SIF_SYNC);
        } else {
                res = esp_common_read_with_addr(SIP->epub, addr, sdio_buff, 4, ESP_SIF_SYNC);
        }
        memcpy((u8 *)&value, sdio_buff, 4);

        /*directly send reply_info to target, and waiting for report*/
        REGISTER_REPLY(info);
        if (!res) {
                sprintf((char *)&value_str, "0x%x", value);
                esp_test_cmd_reply(info, TEST_CMD_SDIO_RD, value_str);
        } else
                esp_test_cmd_reply(info, TEST_CMD_SDIO_RD, "read fail!");

out:
        OUT_DONE();
        return -EINVAL;
}

/* TEST_CMD netlink policy */

static struct nla_policy test_genl_policy[TEST_ATTR_MAX + 1] = {
        [TEST_ATTR_CMD_NAME]= { .type = NLA_NUL_STRING, .len = GENL_NAMSIZ - 1 },
        [TEST_ATTR_CMD_TYPE] = { .type = NLA_U32 },
        [TEST_ATTR_PARA_NUM] = { .type = NLA_U32 },
        [TEST_ATTR_PARA0] = { .type = NLA_U32 },
        [TEST_ATTR_PARA1] = { .type = NLA_U32 },
        [TEST_ATTR_PARA2] = { .type = NLA_U32 },
        [TEST_ATTR_PARA3] = { .type = NLA_U32 },
        [TEST_ATTR_PARA4] = { .type = NLA_U32 },
        [TEST_ATTR_PARA5] = { .type = NLA_U32 },
        [TEST_ATTR_PARA6] = { .type = NLA_U32 },
        [TEST_ATTR_PARA7] = { .type = NLA_U32 },
        [TEST_ATTR_STR]	= { .type = NLA_NUL_STRING, .len = 256-1 },
};

/* Generic Netlink operations array */
static struct genl_ops esp_test_ops[] = {
        {
                .cmd = TEST_CMD_ECHO,
                .policy = test_genl_policy,
                .doit = esp_test_echo,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_ASK,
                .policy = test_genl_policy,
                .doit = esp_test_ask,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_SLEEP,
                .policy = test_genl_policy,
                .doit = esp_test_sleep,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_WAKEUP,
                .policy = test_genl_policy,
                .doit = esp_test_wakeup,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_LOOPBACK,
                .policy = test_genl_policy,
                .doit = esp_test_loopback,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_TX,
                .policy = test_genl_policy,
                .doit = esp_test_tx,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_DEBUG,
                .policy = test_genl_policy,
                .doit = esp_test_genl,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_SDIO_WR,
                .policy = test_genl_policy,
                .doit = esp_test_sdio_wr,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_SDIO_RD,
                .policy = test_genl_policy,
                .doit = esp_test_sdio_rd,
                .flags = GENL_ADMIN_PERM,
        },
	    {
                .cmd = TEST_CMD_ATE,
                .policy = test_genl_policy,
                .doit = esp_test_ate,
                .flags = GENL_ADMIN_PERM,
        },
	{
                .cmd = TEST_CMD_SDIOTEST,
                .policy = test_genl_policy,
                .doit = esp_test_sdiotest,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_SDIOSPEED,
                .policy = test_genl_policy,
                .doit = esp_test_sdiospeed,
                .flags = GENL_ADMIN_PERM,
        },
};

static int esp_test_netlink_notify(struct notifier_block *nb,
                                   unsigned long state,
                                   void *_notify)
{
        struct netlink_notify *notify = _notify;

        if (state != NETLINK_URELEASE)
                return NOTIFY_DONE;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
        if (notify->portid == connected_nl) {
#else
        if (notify->pid == connected_nl) {
#endif
                esp_dbg(ESP_DBG_ERROR, "esp_sdio: user released netlink"
                       " socket \n");
                connected_nl = 0;
        }
        return NOTIFY_DONE;

}

static struct notifier_block test_netlink_notifier = {
        .notifier_call = esp_test_netlink_notify,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
/**
 * copy from net/netlink/genetlink.c(linux kernel 2.6.32)
 *
 * genl_register_family_with_ops - register a generic netlink family
 * @family: generic netlink family
 * @ops: operations to be registered
 * @n_ops: number of elements to register
 * 
 * Registers the specified family and operations from the specified table.
 * Only one family may be registered with the same family name or identifier.
 * 
 * The family id may equal GENL_ID_GENERATE causing an unique id to
 * be automatically generated and assigned.
 * 
 * Either a doit or dumpit callback must be specified for every registered
 * operation or the function will fail. Only one operation structure per
 * command identifier may be registered.
 * 
 * See include/net/genetlink.h for more documenation on the operations
 * structure.
 * 
 * This is equivalent to calling genl_register_family() followed by
 * genl_register_ops() for every operation entry in the table taking
 * care to unregister the family on error path.
 * 
 * Return 0 on success or a negative error code.
 */
int genl_register_family_with_ops(struct genl_family *family,
        struct genl_ops *ops, size_t n_ops)
{
    int err, i;

    err = genl_register_family(family);
    if (err)
        return err;

    for (i = 0; i < n_ops; ++i, ++ops) {
        err = genl_register_ops(family, ops);
        if (err)
            goto err_out;
    }
    return 0;
err_out:
    genl_unregister_family(family);
    return err;
}
#endif

int test_init_netlink(struct esp_sip *sip)
{
        int rc;
        esp_dbg(ESP_DBG_ERROR, "esp_sdio: initializing netlink\n");

        sip_copy=sip;
        /* temp buffer for sdio test */
        sdio_buff = kzalloc(8, GFP_KERNEL);
	    sdiotest_buf = kzalloc(16*1024, GFP_KERNEL);

        rc = genl_register_family_with_ops(&test_genl_family,
                                           esp_test_ops, ARRAY_SIZE(esp_test_ops));
        if (rc)
                goto failure;

        rc = netlink_register_notifier(&test_netlink_notifier);
        if (rc)
                goto failure;

        return 0;

failure:
        esp_dbg(ESP_DBG_ERROR, "esp_sdio: error occured in %s\n", __func__);
	if (sdio_buff) {
		kfree(sdio_buff);
		sdio_buff = NULL;
	}
	if (sdiotest_buf) {
		kfree(sdiotest_buf);
		sdiotest_buf = NULL;
	}
        return -EINVAL;
}

void test_exit_netlink(void)
{
        int ret;

	if (sdio_buff == NULL)
		return;

    kfree(sdio_buff);
	sdio_buff = NULL;
    sip_copy = NULL;

	if (sdioTest.thread) {
		kthread_stop(sdioTest.thread);
		sdioTest.thread = NULL;
	}
	if (sdiotest_buf) {
		kfree(sdiotest_buf);
		sdiotest_buf = NULL;
	}
        esp_dbg(ESP_DBG_ERROR, "esp_sdio: closing netlink\n");
        /* unregister the notifier */
        netlink_unregister_notifier(&test_netlink_notifier);
        /* unregister the family */
        ret = genl_unregister_family(&test_genl_family);
        if (ret)
                esp_dbg(ESP_DBG_ERROR, "esp_sdio: "
                       "unregister family %i\n", ret);
}
void esp_test_cmd_event(u32 cmd_type, char *reply_info)
{
        esp_test_cmd_reply(&info_copy, cmd_type, reply_info);
}

void esp_stability_test(char *filename,struct esp_pub *epub)
{
    int i = 0;
    int count =32;
    int m = 36;
    int n = 100;
    int s = 10;
    char test_res_str[560];
    char *tx_buf =  kzalloc(512*32, GFP_KERNEL);
    char *rx_buf =  kzalloc(512*32, GFP_KERNEL);
#ifdef ESP_USE_SPI
    struct esp_spi_ctrl *sctrl = NULL;
    struct spi_device *spi = NULL;
    if (epub == NULL) {
    	ESSERT(0);
	return;
    }
    sctrl = (struct esp_spi_ctrl *)epub->sif;
    spi = sctrl->spi;
    if (spi == NULL) {
    	ESSERT(0);
	return;
    }
#endif

    while(--m)
    {
        if(m >32)
        {
            count = count *2;
        }else
        {
            count = 512*(33-m);
        }
        n =255;

        while(n--)
        {
            s =100;
            while(s--)
            {

                for(i = 0;i<count;i++)
                {
                    tx_buf[i]=i^0X5A;
                }
#ifdef ESP_USE_SPI
                if(count == 512)
                {
                    spi_bus_lock(spi->master);
                    sif_spi_write_bytes(spi, 0x8010, tx_buf, count, NOT_DUMMYMODE);
                    spi_bus_unlock(spi->master);
                    memset(rx_buf,0xff,count);

                    spi_bus_lock(spi->master);
                    sif_spi_read_bytes(spi, 0x8010, tx_buf, count, NOT_DUMMYMODE);
                    spi_bus_unlock(spi->master);
                }
#endif
                esp_common_write_with_addr((epub), 0x8010, tx_buf, count,  ESP_SIF_SYNC);
                memset(rx_buf,0xff,count);
                esp_common_read_with_addr((epub), 0x8010, rx_buf, count,  ESP_SIF_SYNC);

                for(i =0;i<count;i++)
                {
                    if(tx_buf[i] !=rx_buf[i])
                    {
                        printk("-----------tx_buf--------------\n");
                        show_buf(tx_buf,count);
                        printk("-----------rx_buf-------------\n");
                        show_buf(rx_buf,count);
                        printk("--rx_buf != tx_buf----i= %d----\n",i);

                        sprintf(test_res_str, "error, count = %d !!!\n", count);

                        printk("%s\n", test_res_str);

                        esp_readwrite_file(filename, NULL, test_res_str, strlen(test_res_str));

                        goto _out;
                    }
                }

            }

#if (!defined(CONFIG_DEBUG_FS) || !defined(DEBUGFS_BOOTMODE)) && !defined(ESP_CLASS)
            request_init_conf();
#endif
            if(sif_get_ate_config() == 0)
            {

                sprintf(test_res_str, "ok, count = %d !!!\n", count);

                printk("%s\n", test_res_str);

                esp_readwrite_file(filename, NULL, test_res_str, strlen(test_res_str));

                goto _out;
            }

            //for apk test
            if(sif_get_ate_config() == 3)
            {
                sprintf(test_res_str, "error, count = %d !!!\n", count);

                printk("%s\n", test_res_str);

                esp_readwrite_file(filename, NULL, test_res_str, strlen(test_res_str));

                goto _out;
            }

        }
    }

    sprintf(test_res_str, "ok, count = %d !!!\n", count);
    printk("%s\n", test_res_str);

    esp_readwrite_file(filename, NULL, test_res_str, strlen(test_res_str));

_out:
    kfree(rx_buf);
    kfree(tx_buf);
}

void esp_rate_test(char *filename,struct esp_pub *epub)
{
    char *tx_buf =  kzalloc(512*32, GFP_KERNEL);
    char *rx_buf =  kzalloc(512*32, GFP_KERNEL);
    char test_res_str[560];
    unsigned long jiffies_start_rw;
    unsigned long jiffies_end_rw;   
    unsigned long test_time_rw;
    unsigned long jiffies_start_read;
    unsigned long jiffies_end_read;   
    unsigned long test_time_read;
    unsigned long jiffies_start_write;
    unsigned long jiffies_end_write;   
    unsigned long test_time_write;
    int n = 1024;

    memset(tx_buf,0x0,512*32);

    esp_common_write_with_addr((epub), 0x8010, tx_buf, 512*32, ESP_SIF_SYNC);
    jiffies_start_rw = jiffies;

    while(n--){
        esp_common_write_with_addr((epub), 0x8010, tx_buf, 512*32, ESP_SIF_SYNC);
        esp_common_read_with_addr((epub), 0x8010, rx_buf, 512*32,  ESP_SIF_SYNC);
    }
    jiffies_end_rw = jiffies;
    test_time_rw = jiffies_to_msecs(jiffies_end_rw - jiffies_start_rw);

    n = 1024;
    jiffies_start_read = jiffies;
    while(n--){
        esp_common_read_with_addr((epub), 0x8010, rx_buf, 512*32,  ESP_SIF_SYNC);
        esp_common_read_with_addr((epub), 0x8010, rx_buf, 512*32,  ESP_SIF_SYNC);
    }
    jiffies_end_read = jiffies;
    test_time_read = jiffies_to_msecs(jiffies_end_read - jiffies_start_read);

    n= 1024;  
    jiffies_start_write = jiffies;
    while(n--){
        esp_common_write_with_addr((epub), 0x8010, tx_buf, 512*32,  ESP_SIF_SYNC);
        esp_common_write_with_addr((epub), 0x8010, tx_buf, 512*32,  ESP_SIF_SYNC);
    }
    jiffies_end_write = jiffies;
    test_time_write = jiffies_to_msecs(jiffies_end_write - jiffies_start_write);

    sprintf(test_res_str, "ok,rw_time=%lu,read_time=%lu,write_time=%lu !!!\n",test_time_rw,test_time_read,test_time_write);

    printk("%s\n", test_res_str);

    esp_readwrite_file(filename, NULL, test_res_str, strlen(test_res_str));

    kfree(tx_buf);
    kfree(rx_buf);
}

#ifdef ESP_USE_SPI
void esp_resp_test(char *filename,struct esp_pub *epub)
{
    int i = 0;
    int count =32;
    int m = 36;
    int n = 10;
    int s = 3;
    char *test_res_str = kzalloc(1024,GFP_KERNEL);
    char *tx_buf =  kzalloc(512*32, GFP_KERNEL);
    char *rx_buf =  kzalloc(512*32, GFP_KERNEL);
    struct esp_spi_ctrl *sctrl = NULL;
    struct spi_device *spi = NULL;
    struct esp_spi_resp *spi_resp;
    if (epub == NULL) {
	ESSERT(0);
	return;
    }
    sctrl = (struct esp_spi_ctrl *)epub->sif;
    spi = sctrl->spi;
    if (spi == NULL) {
	ESSERT(0);
	return;
    }

    while(m--)
    {
        if(m >32)
        {
            count = 64;
        }else
        {
            count = 512*(33-m);
        }
        n =10;

        while(n--)
        {
            s =3;
            while(s--)
            {

                for(i = 0;i<count;i++)
                {
                    tx_buf[i]=i^0X5A;
                }
                if(count == 512)
                {
                    spi_bus_lock(spi->master);
                    sif_spi_write_bytes(spi, 0x8010, tx_buf, count, NOT_DUMMYMODE);
                    spi_bus_unlock(spi->master);
                    memset(rx_buf,0xff,count);

                    spi_bus_lock(spi->master);
                    sif_spi_read_bytes(spi, 0x8010, rx_buf, count, NOT_DUMMYMODE);
                    spi_bus_unlock(spi->master);
                    for(i =0;i<count;i++)
                    {
                        if(tx_buf[i] !=rx_buf[i])
                        {
                            printk("-----------tx_buf------\n");
                            show_buf(tx_buf,count);
                            printk("-----------rx_buf------\n");
                            show_buf(rx_buf,count);
                            printk("------rx_buf != tx_buf--i = %d----\n",i);

                            sprintf(test_res_str, "error, count = %d !!!\n", count);

                            printk("%s\n", test_res_str);

                            esp_readwrite_file(filename, NULL, test_res_str, strlen(test_res_str));

                            goto _out;
                        }
                    }


                }
                esp_common_write_with_addr((epub), 0x8010, tx_buf, count,  ESP_SIF_SYNC);
                memset(rx_buf,0xff,count);
                esp_common_read_with_addr((epub), 0x8010, rx_buf, count,  ESP_SIF_SYNC);

                for(i =0;i<count;i++)
                {
                    if(tx_buf[i] !=rx_buf[i])
                    {
                        printk("-----------tx_buf--------------\n");
                        show_buf(tx_buf,count);
                        printk("-----------rx_buf-------------\n");
                        show_buf(rx_buf,count);
                        printk("------------------rx_buf != tx_buf------i = %d-------------\n",i);

                        sprintf(test_res_str, "error, count = %d !!!\n", count);

                        printk("%s\n", test_res_str);

                        esp_readwrite_file(filename, NULL, test_res_str, strlen(test_res_str));

                        goto _out;
                    }
                }

            }

        }
    }
    spi_resp = sif_get_spi_resp();
    
    sprintf(test_res_str, "ok, max_dataW_resp_size=%d--max_dataR_resp_size=%d--max_block_dataW_resp_size=%d--max_block_dataR_resp_size=%d--max_cmd_resp_size=%d-- !!!\n",
              spi_resp->max_dataW_resp_size,spi_resp->max_dataR_resp_size,spi_resp->max_block_dataW_resp_size,spi_resp->max_block_dataR_resp_size,spi_resp->max_cmd_resp_size);

    printk("%s\n", test_res_str);

    esp_readwrite_file(filename, NULL, test_res_str, strlen(test_res_str));

_out:
    kfree(rx_buf);
    kfree(tx_buf);
    kfree(test_res_str);
}
#endif

void esp_noisefloor_test(char *filename,struct esp_pub *epub)
{
    char *tx_buf =  kzalloc(512*32, GFP_KERNEL);
    char *rx_buf =  kzalloc(512*32, GFP_KERNEL);
    char *res_buf = kzalloc(128, GFP_KERNEL);
    char test_res_str[560];
    unsigned long jiffies_start;   
    int i = 0;

    for(i = 0;i<512*32;i++)
    {
        tx_buf[i]=(i*11)^0X5A;
    }

    jiffies_start = jiffies;

    while(jiffies_to_msecs(jiffies - jiffies_start) < 6000){
        esp_common_write_with_addr((epub), 0x9010, tx_buf, 512*32, ESP_SIF_SYNC);
        memset(rx_buf,0xff,512*32);
        esp_common_read_with_addr((epub), 0x9010, rx_buf, 512*32,  ESP_SIF_SYNC);
    }

    //read result
    esp_common_read_with_addr((epub), 0x14, res_buf,128 ,  ESP_SIF_SYNC); 

    if ( (res_buf[38] && 0xf)  == (0xa && 0xf) )
    {
        sprintf(test_res_str, "ok, APST RN:%d,%d; %d,%d; %d,%d; %d,%d; %d,%d; %d,%d; %d,%d; %d,%d; %d,%d; %d,%d; %d,%d; %d,%d; %d,%d; %d,%d; APED\n",
                res_buf[40]-400,res_buf[0]-400, //chanell 1 
                res_buf[41]-400,res_buf[1]-400,
                res_buf[42]-400,res_buf[2]-400,
                res_buf[43]-400,res_buf[3]-400,
                res_buf[100]-400,res_buf[4]-400,
                res_buf[101]-400,res_buf[5]-400,
                res_buf[102]-400,res_buf[6]-400,
                res_buf[103]-400,res_buf[7]-400,
                res_buf[104]-400,res_buf[16]-400,
                res_buf[105]-400,res_buf[17]-400,
                res_buf[106]-400,res_buf[18]-400,
                res_buf[107]-400,res_buf[19]-400,
                res_buf[36]-400,res_buf[20]-400,
                res_buf[37]-400,res_buf[21]-400);

    } else
    {
        sprintf(test_res_str, "error !!!\n");
    }

    printk("%s\n", test_res_str);

    esp_readwrite_file(filename, NULL, test_res_str, strlen(test_res_str));

    kfree(res_buf);
    kfree(tx_buf);
    kfree(rx_buf);
}

void esp_test_init(struct esp_pub *epub)
{
    char filename[256];

    if (mod_eagle_path_get() == NULL)
        sprintf(filename, "%s/%s", FWPATH, "test_results");
    else
        sprintf(filename, "%s/%s", mod_eagle_path_get(), "test_results");

    sif_lock_bus(epub);
    sif_hda_io_enable(epub);
    sif_unlock_bus(epub);

    if(sif_get_ate_config() == 2){
        esp_stability_test(filename,epub);

    } else if(sif_get_ate_config() == 4){
        esp_rate_test(filename,epub);
    }
#ifdef ESP_USE_SPI
    else if(sif_get_ate_config() == 5){
        esp_resp_test(filename,epub);
    }        
#endif
    else if(sif_get_ate_config() == 6){
        esp_noisefloor_test(filename,epub);
    }
}

#endif  //ifdef TEST_MODE

