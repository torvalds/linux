/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/

#include "rtl_core.h"
#include "r8192E_hw.h"
#include "r8192E_hwimg.h"
#include "r8192E_firmware.h"
#include <linux/firmware.h>

static bool _rtl92e_wait_for_fw(struct net_device *dev, u32 mask, u32 timeout)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(timeout);

	while (time_before(jiffies, deadline)) {
		if (rtl92e_readl(dev, CPU_GEN) & mask)
			return true;
		mdelay(2);
	}
	return false;
}

static bool _rtl92e_fw_download_code(struct net_device *dev,
				     u8 *code_virtual_address, u32 buffer_len)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u16		    frag_length, frag_offset = 0;
	int		    i;
	struct sk_buff	    *skb;
	unsigned char	    *seg_ptr;
	struct cb_desc *tcb_desc;
	u8                  bLastIniPkt;

	do {
		if ((buffer_len - frag_offset) > CMDPACKET_FRAG_SIZE) {
			frag_length = CMDPACKET_FRAG_SIZE;
			bLastIniPkt = 0;

		} else {
			frag_length = buffer_len - frag_offset;
			bLastIniPkt = 1;

		}

		skb  = dev_alloc_skb(frag_length + 4);
		memcpy((unsigned char *)(skb->cb), &dev, sizeof(dev));
		tcb_desc = (struct cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
		tcb_desc->queue_index = TXCMD_QUEUE;
		tcb_desc->bCmdOrInit = DESC_PACKET_TYPE_INIT;
		tcb_desc->bLastIniPkt = bLastIniPkt;

		seg_ptr = skb->data;
		for (i = 0; i < frag_length; i += 4) {
			*seg_ptr++ = ((i+0) < frag_length) ?
				     code_virtual_address[i+3] : 0;
			*seg_ptr++ = ((i+1) < frag_length) ?
				     code_virtual_address[i+2] : 0;
			*seg_ptr++ = ((i+2) < frag_length) ?
				     code_virtual_address[i+1] : 0;
			*seg_ptr++ = ((i+3) < frag_length) ?
				     code_virtual_address[i+0] : 0;
		}
		tcb_desc->txbuf_size = (u16)i;
		skb_put(skb, i);

		if (!priv->rtllib->check_nic_enough_desc(dev, tcb_desc->queue_index) ||
		    (!skb_queue_empty(&priv->rtllib->skb_waitQ[tcb_desc->queue_index])) ||
		    (priv->rtllib->queue_stop)) {
			RT_TRACE(COMP_FIRMWARE,
				 "===================> tx full!\n");
			skb_queue_tail(&priv->rtllib->skb_waitQ
					[tcb_desc->queue_index], skb);
		} else {
		priv->rtllib->softmac_hard_start_xmit(skb, dev);
		}

		code_virtual_address += frag_length;
		frag_offset += frag_length;

	} while (frag_offset < buffer_len);

	rtl92e_writeb(dev, TPPoll, TPPoll_CQ);

	return true;
}

static bool _rtl92e_fw_boot_cpu(struct net_device *dev)
{
	u32		CPU_status = 0;

	if (!_rtl92e_wait_for_fw(dev, CPU_GEN_PUT_CODE_OK, 200)) {
		netdev_err(dev, "Firmware download failed.\n");
		goto CPUCheckMainCodeOKAndTurnOnCPU_Fail;
	}
	RT_TRACE(COMP_FIRMWARE, "Download Firmware: Put code ok!\n");

	CPU_status = rtl92e_readl(dev, CPU_GEN);
	rtl92e_writeb(dev, CPU_GEN,
		      (u8)((CPU_status|CPU_GEN_PWR_STB_CPU)&0xff));
	mdelay(1);

	if (!_rtl92e_wait_for_fw(dev, CPU_GEN_BOOT_RDY, 200)) {
		netdev_err(dev, "Firmware boot failed.\n");
		goto CPUCheckMainCodeOKAndTurnOnCPU_Fail;
	}

	RT_TRACE(COMP_FIRMWARE, "Download Firmware: Boot ready!\n");

	return true;

CPUCheckMainCodeOKAndTurnOnCPU_Fail:
	return false;
}

static bool _rtl92e_fw_check_ready(struct net_device *dev,
				   u8 load_fw_status)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_firmware *pfirmware = priv->pFirmware;
	bool rt_status  = true;

	switch (load_fw_status) {
	case FW_INIT_STEP0_BOOT:
		pfirmware->status = FW_STATUS_1_MOVE_BOOT_CODE;
		break;

	case FW_INIT_STEP1_MAIN:
		pfirmware->status = FW_STATUS_2_MOVE_MAIN_CODE;

		rt_status = _rtl92e_fw_boot_cpu(dev);
		if (rt_status)
			pfirmware->status = FW_STATUS_3_TURNON_CPU;
		else
			RT_TRACE(COMP_FIRMWARE, "_rtl92e_fw_boot_cpu fail!\n");

		break;

	case FW_INIT_STEP2_DATA:
		pfirmware->status = FW_STATUS_4_MOVE_DATA_CODE;
		mdelay(1);

		rt_status = _rtl92e_wait_for_fw(dev, CPU_GEN_FIRM_RDY, 20);
		if (rt_status)
			pfirmware->status = FW_STATUS_5_READY;
		else
			RT_TRACE(COMP_FIRMWARE,
				 "_rtl92e_is_fw_ready fail(%d)!\n",
				 rt_status);
		break;
	default:
		rt_status = false;
		RT_TRACE(COMP_FIRMWARE, "Unknown firmware status");
		break;
	}

	return rt_status;
}

static bool _rtl92e_fw_prepare(struct net_device *dev, struct rt_fw_blob *blob,
			       const char *name, u8 padding)
{
	const struct firmware *fw;
	int rc, i;
	bool ret = true;

	rc = request_firmware(&fw, name, &dev->dev);
	if (rc < 0)
		return false;

	if (round_up(fw->size, 4) > MAX_FW_SIZE - padding) {
		netdev_err(dev, "Firmware image %s too big for the device.\n",
			   name);
		ret = false;
		goto out;
	}

	if (padding)
		memset(blob->data, 0, padding);
	if (fw->size % 4)
		memset(blob->data + padding + fw->size, 0, 4);
	memcpy(blob->data + padding, fw->data, fw->size);

	blob->size = round_up(fw->size, 4) + padding;
out:
	release_firmware(fw);
	return ret;
}

bool rtl92e_init_fw(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	bool			rt_status = true;

	u32	file_length = 0;
	u8	*mapped_file = NULL;
	u8	i = 0;
	enum opt_rst_type rst_opt = OPT_SYSTEM_RESET;
	enum firmware_init_step starting_state = FW_INIT_STEP0_BOOT;

	struct rt_firmware *pfirmware = priv->pFirmware;

	RT_TRACE(COMP_FIRMWARE, " PlatformInitFirmware()==>\n");

	if (pfirmware->status == FW_STATUS_0_INIT) {
		rst_opt = OPT_SYSTEM_RESET;
		starting_state = FW_INIT_STEP0_BOOT;

	} else if (pfirmware->status == FW_STATUS_5_READY) {
		rst_opt = OPT_FIRMWARE_RESET;
		starting_state = FW_INIT_STEP2_DATA;
	} else {
		RT_TRACE(COMP_FIRMWARE,
			 "PlatformInitFirmware: undefined firmware state\n");
	}

	for (i = starting_state; i <= FW_INIT_STEP2_DATA; i++) {
		if (rst_opt == OPT_SYSTEM_RESET) {
			if (pfirmware->blobs[i].size == 0) {
				const char *fw_name[3] = {
					RTL8192E_BOOT_IMG_FW,
					RTL8192E_MAIN_IMG_FW,
					RTL8192E_DATA_IMG_FW
				};
				int pad = 0;

				if (i == FW_INIT_STEP1_MAIN)
					pad = 128;

				if (!_rtl92e_fw_prepare(dev,
							&pfirmware->blobs[i],
							fw_name[i],
							pad))
					goto download_firmware_fail;
			}
		}

		mapped_file = pfirmware->blobs[i].data;
		file_length = pfirmware->blobs[i].size;

		rt_status = _rtl92e_fw_download_code(dev, mapped_file,
						     file_length);
		if (!rt_status)
			goto download_firmware_fail;

		if (!_rtl92e_fw_check_ready(dev, i))
			goto download_firmware_fail;
	}

	RT_TRACE(COMP_FIRMWARE, "Firmware Download Success\n");
	return rt_status;

download_firmware_fail:
	netdev_err(dev, "%s: Failed to initialize firmware.\n", __func__);
	rt_status = false;
	return rt_status;

}
