/*
 * Copyright (c) 2010 - 2012 Espressif System.
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/firmware.h>
#include <linux/sched.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <linux/time.h>
#include <linux/moduleparam.h>

#include "esp_pub.h"
#include "esp_sip.h"
#include "esp_sif.h"
#include "esp_debug.h"
#ifdef ANDROID
#include "esp_android.h"
#endif /* ANDROID */
#include "esp_wl.h"
#include "slc_host_register.h"
#include "esp_android.h"

struct completion *gl_bootup_cplx = NULL;

#ifndef FPGA_DEBUG
static int esp_download_fw(struct esp_pub * epub);
#endif /* !FGPA_DEBUG */

static bool modparam_no_txampdu = false;
static bool modparam_no_rxampdu = false;
module_param_named(no_txampdu, modparam_no_txampdu, bool, 0444);
MODULE_PARM_DESC(no_txampdu, "Disable tx ampdu.");
module_param_named(no_rxampdu, modparam_no_rxampdu, bool, 0444);
MODULE_PARM_DESC(no_rxampdu, "Disable rx ampdu.");

static char *modparam_eagle_path = "";
module_param_named(eagle_path, modparam_eagle_path, charp, 0444);
MODULE_PARM_DESC(eagle_path, "eagle path");

bool mod_support_no_txampdu()
{
        return modparam_no_txampdu;
}

bool mod_support_no_rxampdu()
{
        return modparam_no_rxampdu;
}

void mod_support_no_txampdu_set(bool value)
{
	modparam_no_txampdu = value;
}

char *mod_eagle_path_get(void)
{
	if (modparam_eagle_path[0] == '\0')
		return NULL;

	return modparam_eagle_path;
}

int esp_pub_init_all(struct esp_pub *epub)
{
        int ret = 0;
        
	/* completion for bootup event poll*/
	DECLARE_COMPLETION_ONSTACK(complete);
	atomic_set(&epub->ps.state, ESP_PM_OFF);
	if(epub->sdio_state == ESP_SDIO_STATE_FIRST_INIT){
		epub->sip = sip_attach(epub);

		esp_dump_var("esp_msg_level", NULL, &esp_msg_level, ESP_U32);

#if defined(ANDROID) && defined (ESP_ANDROID_LOGGER)
		esp_dump_var("log_off", NULL, &log_off, ESP_U32);
#endif /* ESP_ANDROID_LOGGER */

		ret = sip_prepare_boot(epub->sip);
		if (ret)
			return ret;
	} else {
		atomic_set(&epub->sip->state, SIP_PREPARE_BOOT);
		atomic_set(&epub->sip->tx_credits, 0);
	}

#ifndef FPGA_DEBUG
        ret = esp_download_fw(epub);

        if (ret) {
                esp_dbg(ESP_DBG_ERROR, "download firmware failed\n");
                return ret;
        }

        esp_dbg(ESP_DBG_TRACE, "download firmware OK \n");
#else
#ifndef SDIO_TEST
        sip_send_bootup(epub->sip);
#endif /* !SDIO_TEST */

#endif /* FPGA_DEBUG */

	gl_bootup_cplx = &complete;

	sif_enable_irq(epub);
	
	if(epub->sdio_state == ESP_SDIO_STATE_SECOND_INIT){
		sip_poll_bootup_event(epub->sip);
	} else {
		sip_poll_resetting_event(epub->sip);
	}

	gl_bootup_cplx = NULL;

        return ret;
}

#if 0
void esp_ps_config(struct esp_pub *epub, struct esp_ps *ps, bool on)
{
        unsigned long time = jiffies - ps->last_config_time;
        u32 time_msec = jiffies_to_msecs(time);

        ps->last_config_time = jiffies;

        if (on && (atomic_read(&ps->state) == ESP_PM_TURNING_ON || atomic_read(&ps->state) == ESP_PM_ON)) {
                esp_dbg(ESP_DBG_PS, "%s same state\n", __func__);
                return;
        }

	ps->nulldata_pm_on = false;

        esp_dbg(ESP_DBG_PS, "%s PS %s, dtim %u maxslp %u period %u\n", __func__, on?"ON":"OFF", ps->dtim_period, ps->max_sleep_period, time_msec);

        //NB: turn on ps may need additional check, make sure don't hurt iperf downlink since pkt may be sparse during rx

        if (on) {
                esp_dbg(ESP_DBG_PS, "%s ps state %d => turning ON\n", __func__, atomic_read(&ps->state));
                atomic_set(&ps->state, ESP_PM_TURNING_ON);
        } else {
                esp_dbg(ESP_DBG_PS, "%s ps state %d => turning OFF\n", __func__, atomic_read(&ps->state));
                atomic_set(&ps->state, ESP_PM_TURNING_OFF);
        }

        sip_send_ps_config(epub, ps);
}
#endif

void
esp_dsr(struct esp_pub *epub)
{
        sip_rx(epub);
}


struct esp_fw_hdr {
        u8 magic;
        u8 blocks;
        u8 pad[2];
        u32 entry_addr;
} __packed;

struct esp_fw_blk_hdr {
        u32 load_addr;
        u32 data_len;
} __packed;

#define ESP_FW_NAME1 "eagle_fw1.bin"
#define ESP_FW_NAME2 "eagle_fw2.bin"

#ifndef FPGA_DEBUG
static int esp_download_fw(struct esp_pub * epub)
{
#ifndef HAS_FW
        const struct firmware *fw_entry;
#endif /* !HAS_FW */
        u8 * fw_buf = NULL;
        u32 offset = 0;
        int ret = 0;
        u8 blocks;
        struct esp_fw_hdr *fhdr;
        struct esp_fw_blk_hdr *bhdr=NULL;
        struct sip_cmd_bootup bootcmd;

#ifndef HAS_FW

char * esp_fw_name = epub->sdio_state == ESP_SDIO_STATE_FIRST_INIT ? ESP_FW_NAME1 : ESP_FW_NAME2;

#ifdef ANDROID
        ret = android_request_firmware(&fw_entry, esp_fw_name, epub->dev);
#else
        ret = request_firmware(&fw_entry, esp_fw_name, epub->dev);
#endif //ANDROID

        if (ret)
                return ret;

        fw_buf = kmemdup(fw_entry->data, fw_entry->size, GFP_KERNEL);

#ifdef ANDROID
        android_release_firmware(fw_entry);
#else
        release_firmware(fw_entry);
#endif //ANDROID

        if (fw_buf == NULL) {
                return -ENOMEM;
        }
#else

#include "eagle_fw1.h"
#include "eagle_fw2.h"
        fw_buf = epub->sdio_state == ESP_SDIO_STATE_FIRST_INIT ? &eagle_fw1[0] : &eagle_fw2[0];

#endif /* HAS_FW */

        fhdr = (struct esp_fw_hdr *)fw_buf;

        if (fhdr->magic != 0xE9) {
                esp_dbg(ESP_DBG_ERROR, "%s wrong magic! \n", __func__);
                goto _err;
        }

        blocks = fhdr->blocks;
        offset += sizeof(struct esp_fw_hdr);

        while (blocks) {

                bhdr = (struct esp_fw_blk_hdr *)(&fw_buf[offset]);
                offset += sizeof(struct esp_fw_blk_hdr);

                ret = sip_write_memory(epub->sip, bhdr->load_addr, &fw_buf[offset], bhdr->data_len);

                if (ret) {
                        esp_dbg(ESP_DBG_ERROR, "%s Failed to write fw, err: %d\n", __func__, ret);
                        goto _err;
                }

                blocks--;
                offset += bhdr->data_len;
        }

        /* TODO: last byte should be the checksum and skip checksum for now */

        bootcmd.boot_addr = fhdr->entry_addr;
        ret = sip_send_cmd(epub->sip, SIP_CMD_BOOTUP, sizeof(struct sip_cmd_bootup), &bootcmd);

        if (ret)
                goto _err;

_err:
#ifndef HAS_FW
        kfree(fw_buf);
#endif /* !HAS_FW */

        return ret;

}
#endif /* !FPGA_DEBUG */




