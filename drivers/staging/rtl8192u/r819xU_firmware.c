// SPDX-License-Identifier: GPL-2.0
/**************************************************************************************************
 * Procedure:    Init boot code/firmware code/data session
 *
 * Description: This routine will initialize firmware. If any error occurs during the initialization
 *		process, the routine shall terminate immediately and return fail.
 *		NIC driver should call NdisOpenFile only from MiniportInitialize.
 *
 * Arguments:   The pointer of the adapter

 * Returns:
 *        NDIS_STATUS_FAILURE - the following initialization process should be terminated
 *        NDIS_STATUS_SUCCESS - if firmware initialization process success
 **************************************************************************************************/

#include "r8192U.h"
#include "r8192U_hw.h"
#include "r819xU_firmware_img.h"
#include "r819xU_firmware.h"
#include <linux/firmware.h>

static void firmware_init_param(struct net_device *dev)
{
	struct r8192_priv	*priv = ieee80211_priv(dev);
	rt_firmware		*pfirmware = priv->pFirmware;

	pfirmware->cmdpacket_frag_threshold = GET_COMMAND_PACKET_FRAG_THRESHOLD(MAX_TRANSMIT_BUFFER_SIZE);
}

/*
 * segment the img and use the ptr and length to remember info on each segment
 *
 */
static bool fw_download_code(struct net_device *dev, u8 *code_virtual_address,
			     u32 buffer_len)
{
	struct r8192_priv   *priv = ieee80211_priv(dev);
	bool		    rt_status = true;
	u16		    frag_threshold;
	u16		    frag_length, frag_offset = 0;
	int		    i;

	rt_firmware	    *pfirmware = priv->pFirmware;
	struct sk_buff	    *skb;
	unsigned char	    *seg_ptr;
	struct cb_desc		    *tcb_desc;
	u8                  bLastIniPkt;
	u8		    index;

	firmware_init_param(dev);
	/* Fragmentation might be required */
	frag_threshold = pfirmware->cmdpacket_frag_threshold;
	do {
		if ((buffer_len - frag_offset) > frag_threshold) {
			frag_length = frag_threshold;
			bLastIniPkt = 0;

		} else {
			frag_length = buffer_len - frag_offset;
			bLastIniPkt = 1;

		}

		/* Allocate skb buffer to contain firmware info and tx descriptor info
		 * add 4 to avoid packet appending overflow.
		 */
		skb  = dev_alloc_skb(USB_HWDESC_HEADER_LEN + frag_length + 4);
		if (!skb)
			return false;
		memcpy((unsigned char *)(skb->cb), &dev, sizeof(dev));
		tcb_desc = (struct cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
		tcb_desc->queue_index = TXCMD_QUEUE;
		tcb_desc->bCmdOrInit = DESC_PACKET_TYPE_INIT;
		tcb_desc->bLastIniPkt = bLastIniPkt;

		skb_reserve(skb, USB_HWDESC_HEADER_LEN);
		seg_ptr = skb->data;
		/*
		 * Transform from little endian to big endian
		 * and pending  zero
		 */
		for (i = 0; i < frag_length; i += 4) {
			*seg_ptr++ = ((i+0) < frag_length)?code_virtual_address[i+3] : 0;
			*seg_ptr++ = ((i+1) < frag_length)?code_virtual_address[i+2] : 0;
			*seg_ptr++ = ((i+2) < frag_length)?code_virtual_address[i+1] : 0;
			*seg_ptr++ = ((i+3) < frag_length)?code_virtual_address[i+0] : 0;
		}
		tcb_desc->txbuf_size = (u16)i;
		skb_put(skb, i);

		index = tcb_desc->queue_index;
		if (!priv->ieee80211->check_nic_enough_desc(dev, index) ||
		       (!skb_queue_empty(&priv->ieee80211->skb_waitQ[index])) ||
		       (priv->ieee80211->queue_stop)) {
			RT_TRACE(COMP_FIRMWARE, "=====================================================> tx full!\n");
			skb_queue_tail(&priv->ieee80211->skb_waitQ[tcb_desc->queue_index], skb);
		} else {
			priv->ieee80211->softmac_hard_start_xmit(skb, dev);
		}

		code_virtual_address += frag_length;
		frag_offset += frag_length;

	} while (frag_offset < buffer_len);

	return rt_status;

}

/*
 * Procedure:	Check whether main code is download OK. If OK, turn on CPU
 *
 * Description:	CPU register locates in different page against general register.
 *	    Switch to CPU register in the begin and switch back before return
 *
 *
 * Arguments:   The pointer of the adapter
 *
 * Returns:
 *        NDIS_STATUS_FAILURE - the following initialization process should
 *				be terminated
 *        NDIS_STATUS_SUCCESS - if firmware initialization process success
 */
static bool CPUcheck_maincodeok_turnonCPU(struct net_device *dev)
{
	bool		rt_status = true;
	int		check_putcodeOK_time = 200000, check_bootOk_time = 200000;
	u32		CPU_status = 0;

	/* Check whether put code OK */
	do {
		read_nic_dword(dev, CPU_GEN, &CPU_status);

		if (CPU_status&CPU_GEN_PUT_CODE_OK)
			break;

	} while (check_putcodeOK_time--);

	if (!(CPU_status&CPU_GEN_PUT_CODE_OK)) {
		RT_TRACE(COMP_ERR, "Download Firmware: Put code fail!\n");
		goto CPUCheckMainCodeOKAndTurnOnCPU_Fail;
	} else {
		RT_TRACE(COMP_FIRMWARE, "Download Firmware: Put code ok!\n");
	}

	/* Turn On CPU */
	read_nic_dword(dev, CPU_GEN, &CPU_status);
	write_nic_byte(dev, CPU_GEN,
		       (u8)((CPU_status | CPU_GEN_PWR_STB_CPU) & 0xff));
	mdelay(1000);

	/* Check whether CPU boot OK */
	do {
		read_nic_dword(dev, CPU_GEN, &CPU_status);

		if (CPU_status&CPU_GEN_BOOT_RDY)
			break;
	} while (check_bootOk_time--);

	if (!(CPU_status&CPU_GEN_BOOT_RDY))
		goto CPUCheckMainCodeOKAndTurnOnCPU_Fail;
	else
		RT_TRACE(COMP_FIRMWARE, "Download Firmware: Boot ready!\n");

	return rt_status;

CPUCheckMainCodeOKAndTurnOnCPU_Fail:
	RT_TRACE(COMP_ERR, "ERR in %s()\n", __func__);
	rt_status = false;
	return rt_status;
}

static bool CPUcheck_firmware_ready(struct net_device *dev)
{

	bool		rt_status = true;
	int		check_time = 200000;
	u32		CPU_status = 0;

	/* Check Firmware Ready */
	do {
		read_nic_dword(dev, CPU_GEN, &CPU_status);

		if (CPU_status&CPU_GEN_FIRM_RDY)
			break;

	} while (check_time--);

	if (!(CPU_status&CPU_GEN_FIRM_RDY))
		goto CPUCheckFirmwareReady_Fail;
	else
		RT_TRACE(COMP_FIRMWARE, "Download Firmware: Firmware ready!\n");

	return rt_status;

CPUCheckFirmwareReady_Fail:
	RT_TRACE(COMP_ERR, "ERR in %s()\n", __func__);
	rt_status = false;
	return rt_status;

}

bool init_firmware(struct net_device *dev)
{
	struct r8192_priv	*priv = ieee80211_priv(dev);
	bool			rt_status = true;

	u32			file_length = 0;
	u8			*mapped_file = NULL;
	u32			init_step = 0;
	enum opt_rst_type_e	   rst_opt = OPT_SYSTEM_RESET;
	enum firmware_init_step_e  starting_state = FW_INIT_STEP0_BOOT;

	rt_firmware		*pfirmware = priv->pFirmware;
	const struct firmware	*fw_entry;
	const char *fw_name[3] = { "RTL8192U/boot.img",
			   "RTL8192U/main.img",
			   "RTL8192U/data.img"};
	int rc;

	RT_TRACE(COMP_FIRMWARE, " PlatformInitFirmware()==>\n");

	if (pfirmware->firmware_status == FW_STATUS_0_INIT) {
		/* it is called by reset */
		rst_opt = OPT_SYSTEM_RESET;
		starting_state = FW_INIT_STEP0_BOOT;
		/* TODO: system reset */

	} else if (pfirmware->firmware_status == FW_STATUS_5_READY) {
		/* it is called by Initialize */
		rst_opt = OPT_FIRMWARE_RESET;
		starting_state = FW_INIT_STEP2_DATA;
	} else {
		 RT_TRACE(COMP_FIRMWARE, "PlatformInitFirmware: undefined firmware state\n");
	}

	/*
	 * Download boot, main, and data image for System reset.
	 * Download data image for firmware reset
	 */
	for (init_step = starting_state; init_step <= FW_INIT_STEP2_DATA; init_step++) {
		/*
		 * Open image file, and map file to continuous memory if open file success.
		 * or read image file from array. Default load from IMG file
		 */
		if (rst_opt == OPT_SYSTEM_RESET) {
			rc = request_firmware(&fw_entry, fw_name[init_step], &priv->udev->dev);
			if (rc < 0) {
				RT_TRACE(COMP_ERR, "request firmware fail!\n");
				goto download_firmware_fail;
			}

			if (fw_entry->size > sizeof(pfirmware->firmware_buf)) {
				RT_TRACE(COMP_ERR, "img file size exceed the container buffer fail!\n");
				goto download_firmware_fail;
			}

			if (init_step != FW_INIT_STEP1_MAIN) {
				memcpy(pfirmware->firmware_buf, fw_entry->data, fw_entry->size);
				mapped_file = pfirmware->firmware_buf;
				file_length = fw_entry->size;
			} else {
				memset(pfirmware->firmware_buf, 0, 128);
				memcpy(&pfirmware->firmware_buf[128], fw_entry->data, fw_entry->size);
				mapped_file = pfirmware->firmware_buf;
				file_length = fw_entry->size + 128;
			}
			pfirmware->firmware_buf_size = file_length;
		} else if (rst_opt == OPT_FIRMWARE_RESET) {
			/* we only need to download data.img here */
			mapped_file = pfirmware->firmware_buf;
			file_length = pfirmware->firmware_buf_size;
		}

		/* Download image file */
		/* The firmware download process is just as following,
		 * 1. that is each packet will be segmented and inserted to the wait queue.
		 * 2. each packet segment will be put in the skb_buff packet.
		 * 3. each skb_buff packet data content will already include the firmware info
		 *   and Tx descriptor info
		 */
		rt_status = fw_download_code(dev, mapped_file, file_length);
		if (rst_opt == OPT_SYSTEM_RESET)
			release_firmware(fw_entry);

		if (!rt_status)
			goto download_firmware_fail;

		switch (init_step) {
		case FW_INIT_STEP0_BOOT:
			/* Download boot
			 * initialize command descriptor.
			 * will set polling bit when firmware code is also configured
			 */
			pfirmware->firmware_status = FW_STATUS_1_MOVE_BOOT_CODE;
			/* mdelay(1000); */
			/*
			 * To initialize IMEM, CPU move code  from 0x80000080,
			 * hence, we send 0x80 byte packet
			 */
			break;

		case FW_INIT_STEP1_MAIN:
			/* Download firmware code. Wait until Boot Ready and Turn on CPU */
			pfirmware->firmware_status = FW_STATUS_2_MOVE_MAIN_CODE;

			/* Check Put Code OK and Turn On CPU */
			rt_status = CPUcheck_maincodeok_turnonCPU(dev);
			if (!rt_status) {
				RT_TRACE(COMP_ERR, "CPUcheck_maincodeok_turnonCPU fail!\n");
				goto download_firmware_fail;
			}

			pfirmware->firmware_status = FW_STATUS_3_TURNON_CPU;
			break;

		case FW_INIT_STEP2_DATA:
			/* download initial data code */
			pfirmware->firmware_status = FW_STATUS_4_MOVE_DATA_CODE;
			mdelay(1);

			rt_status = CPUcheck_firmware_ready(dev);
			if (!rt_status) {
				RT_TRACE(COMP_ERR, "CPUcheck_firmware_ready fail(%d)!\n", rt_status);
				goto download_firmware_fail;
			}

			/* wait until data code is initialized ready.*/
			pfirmware->firmware_status = FW_STATUS_5_READY;
			break;
		}
	}

	RT_TRACE(COMP_FIRMWARE, "Firmware Download Success\n");
	return rt_status;

download_firmware_fail:
	RT_TRACE(COMP_ERR, "ERR in %s()\n", __func__);
	rt_status = false;
	return rt_status;

}

MODULE_FIRMWARE("RTL8192U/boot.img");
MODULE_FIRMWARE("RTL8192U/main.img");
MODULE_FIRMWARE("RTL8192U/data.img");
