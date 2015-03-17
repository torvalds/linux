/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * main routine
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
#include "esp_file.h"
#include "esp_wl.h"

struct completion *gl_bootup_cplx = NULL;

#ifndef FPGA_DEBUG
static int esp_download_fw(struct esp_pub * epub);
#endif /* !FGPA_DEBUG */

static int modparam_no_txampdu = 0;
static int modparam_no_rxampdu = 0;
module_param_named(no_txampdu, modparam_no_txampdu, int, 0444);
MODULE_PARM_DESC(no_txampdu, "Disable tx ampdu.");
module_param_named(no_rxampdu, modparam_no_rxampdu, int, 0444);
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
		if (epub->sip == NULL) {
			printk(KERN_ERR "%s sip alloc failed\n", __func__);
			return -ENOMEM;
		}
	} else {
		atomic_set(&epub->sip->state, SIP_PREPARE_BOOT);
		atomic_set(&epub->sip->tx_credits, 0);
    }

	epub->sip->to_host_seq = 0;

#ifdef TEST_MODE
    if(sif_get_ate_config() != 0 &&  sif_get_ate_config() != 1 && sif_get_ate_config() !=6 )
    {
        esp_test_init(epub);
        return -1;
    }
#endif

#ifndef FPGA_DEBUG
        ret = esp_download_fw(epub);
#ifdef ESP_USE_SPI
	if(sif_get_ate_config() != 1)
        	epub->enable_int = 1;
#endif  
#ifdef TEST_MODE
        if(sif_get_ate_config() == 6)
        {
            sif_enable_irq(epub);
            mdelay(500);
            sif_disable_irq(epub);
            mdelay(1000);
            esp_test_init(epub);
            return -1;
        }
#endif
        if (ret) {
                esp_dbg(ESP_DBG_ERROR, "download firmware failed\n");
                return ret;
        }

        esp_dbg(ESP_DBG_TRACE, "download firmware OK \n");
#else
        sip_send_bootup(epub->sip);
#endif /* FPGA_DEBUG */

	gl_bootup_cplx = &complete;
	epub->wait_reset = 0;
	sif_enable_irq(epub);
	
	if(epub->sdio_state == ESP_SDIO_STATE_SECOND_INIT || sif_get_ate_config() == 1){
		ret = sip_poll_bootup_event(epub->sip);
	} else {
		ret = sip_poll_resetting_event(epub->sip);
        if (ret == 0) {
            sif_lock_bus(epub);
            sif_interrupt_target(epub, 7);
            sif_unlock_bus(epub);
        }
		
	}

	gl_bootup_cplx = NULL;

	if (sif_get_ate_config() == 1)
		ret = -EOPNOTSUPP;

        return ret;
}

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
#define ESP_FW_NAME3 "eagle_fw3.bin"

#ifndef FPGA_DEBUG
static int esp_download_fw(struct esp_pub * epub)
{
#ifndef HAS_FW
	char * esp_fw_name = NULL;
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

        if(sif_get_ate_config() == 1) {
		esp_fw_name = ESP_FW_NAME3;
	} else {
		esp_fw_name = epub->sdio_state == ESP_SDIO_STATE_FIRST_INIT ? ESP_FW_NAME1 : ESP_FW_NAME2;
	}
        ret = esp_request_firmware(&fw_entry, esp_fw_name, epub->dev);

        if (ret)
                return ret;

        fw_buf = kmemdup(fw_entry->data, fw_entry->size, GFP_KERNEL);

        esp_release_firmware(fw_entry);

        if (fw_buf == NULL) {
                return -ENOMEM;
        }
#else

#include "eagle_fw1.h"
#include "eagle_fw2.h"
#include "eagle_fw3.h"
        if(sif_get_ate_config() == 1){
            fw_buf =  &eagle_fw3[0];
        } else {
            fw_buf = epub->sdio_state == ESP_SDIO_STATE_FIRST_INIT ? &eagle_fw1[0] : &eagle_fw2[0];
        }
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




