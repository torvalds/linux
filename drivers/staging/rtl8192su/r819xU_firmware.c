/**************************************************************************************************
 * Procedure:    Init boot code/firmware code/data session
 *
 * Description: This routine will intialize firmware. If any error occurs during the initialization
 * 		process, the routine shall terminate immediately and return fail.
 *		NIC driver should call NdisOpenFile only from MiniportInitialize.
 *
 * Arguments:   The pointer of the adapter

 * Returns:
 *        NDIS_STATUS_FAILURE - the following initialization process should be terminated
 *        NDIS_STATUS_SUCCESS - if firmware initialization process success
**************************************************************************************************/
//#include "ieee80211.h"
#include "r8192U.h"
#include "r8192U_hw.h"
#include "r819xU_firmware_img.h"
#include "r819xU_firmware.h"
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
#include <linux/firmware.h>
#endif
void firmware_init_param(struct net_device *dev)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	rt_firmware		*pfirmware = priv->pFirmware;

	pfirmware->cmdpacket_frag_thresold = GET_COMMAND_PACKET_FRAG_THRESHOLD(MAX_TRANSMIT_BUFFER_SIZE);
}

/*
 * segment the img and use the ptr and length to remember info on each segment
 *
 */
bool fw_download_code(struct net_device *dev, u8 *code_virtual_address, u32 buffer_len)
{
	struct r8192_priv   *priv = ieee80211_priv(dev);
	bool 		    rt_status = true;
	u16		    frag_threshold;
	u16		    frag_length, frag_offset = 0;
	//u16		    total_size;
	int		    i;

	rt_firmware	    *pfirmware = priv->pFirmware;
	struct sk_buff	    *skb;
	unsigned char	    *seg_ptr;
	cb_desc		    *tcb_desc;
	u8                  bLastIniPkt;

	firmware_init_param(dev);
	//Fragmentation might be required
	frag_threshold = pfirmware->cmdpacket_frag_thresold;
	do {
		if((buffer_len - frag_offset) > frag_threshold) {
			frag_length = frag_threshold ;
			bLastIniPkt = 0;

		} else {
			frag_length = buffer_len - frag_offset;
			bLastIniPkt = 1;

		}

		/* Allocate skb buffer to contain firmware info and tx descriptor info
		 * add 4 to avoid packet appending overflow.
		 * */
		#ifdef RTL8192U
		skb  = dev_alloc_skb(USB_HWDESC_HEADER_LEN + frag_length + 4);
		#else
		skb  = dev_alloc_skb(frag_length + 4);
		#endif
		memcpy((unsigned char *)(skb->cb),&dev,sizeof(dev));
		tcb_desc = (cb_desc*)(skb->cb + MAX_DEV_ADDR_SIZE);
		tcb_desc->queue_index = TXCMD_QUEUE;
		tcb_desc->bCmdOrInit = DESC_PACKET_TYPE_INIT;
		tcb_desc->bLastIniPkt = bLastIniPkt;

		#ifdef RTL8192U
		skb_reserve(skb, USB_HWDESC_HEADER_LEN);
		#endif
		seg_ptr = skb->data;
		/*
		 * Transform from little endian to big endian
                 * and pending  zero
		 */
		for(i=0 ; i < frag_length; i+=4) {
			*seg_ptr++ = ((i+0)<frag_length)?code_virtual_address[i+3]:0;
			*seg_ptr++ = ((i+1)<frag_length)?code_virtual_address[i+2]:0;
			*seg_ptr++ = ((i+2)<frag_length)?code_virtual_address[i+1]:0;
			*seg_ptr++ = ((i+3)<frag_length)?code_virtual_address[i+0]:0;
		}
		tcb_desc->txbuf_size= (u16)i;
		skb_put(skb, i);

		if(!priv->ieee80211->check_nic_enough_desc(dev,tcb_desc->queue_index)||
			(!skb_queue_empty(&priv->ieee80211->skb_waitQ[tcb_desc->queue_index]))||\
			(priv->ieee80211->queue_stop) ) {
			RT_TRACE(COMP_FIRMWARE,"=====================================================> tx full!\n");
			skb_queue_tail(&priv->ieee80211->skb_waitQ[tcb_desc->queue_index], skb);
		} else {
			priv->ieee80211->softmac_hard_start_xmit(skb,dev);
		}

		code_virtual_address += frag_length;
		frag_offset += frag_length;

	}while(frag_offset < buffer_len);

	return rt_status;

#if 0
cmdsend_downloadcode_fail:
	rt_status = false;
	RT_TRACE(COMP_ERR, "CmdSendDownloadCode fail !!\n");
	return rt_status;
#endif
}

bool
fwSendNullPacket(
	struct net_device *dev,
	u32			Length
)
{
	bool	rtStatus = true;
	struct r8192_priv   *priv = ieee80211_priv(dev);
	struct sk_buff	    *skb;
	cb_desc		    *tcb_desc;
	unsigned char	    *ptr_buf;
	bool	bLastInitPacket = false;

	//PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	//Get TCB and local buffer from common pool. (It is shared by CmdQ, MgntQ, and USB coalesce DataQ)
	skb  = dev_alloc_skb(Length+ 4);
	memcpy((unsigned char *)(skb->cb),&dev,sizeof(dev));
	tcb_desc = (cb_desc*)(skb->cb + MAX_DEV_ADDR_SIZE);
	tcb_desc->queue_index = TXCMD_QUEUE;
	tcb_desc->bCmdOrInit = DESC_PACKET_TYPE_INIT;
	tcb_desc->bLastIniPkt = bLastInitPacket;
	ptr_buf = skb_put(skb, Length);
	memset(ptr_buf,0,Length);
	tcb_desc->txbuf_size= (u16)Length;

	if(!priv->ieee80211->check_nic_enough_desc(dev,tcb_desc->queue_index)||
			(!skb_queue_empty(&priv->ieee80211->skb_waitQ[tcb_desc->queue_index]))||\
			(priv->ieee80211->queue_stop) ) {
			RT_TRACE(COMP_FIRMWARE,"===================NULL packet==================================> tx full!\n");
			skb_queue_tail(&priv->ieee80211->skb_waitQ[tcb_desc->queue_index], skb);
		} else {
			priv->ieee80211->softmac_hard_start_xmit(skb,dev);
		}

	//PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);
	return rtStatus;
}

#if 0
/*
 * Procedure  :   Download code into IMEM or DMEM
 * Description:   This routine will intialize firmware. If any error occurs during the initialization
 *				process, the routine shall terminate immediately and return fail.
 *				The routine copy virtual address get from opening of file into shared memory
 *				allocated during initialization. If code size larger than a conitneous shared
 *				memory may contain, the code should be divided into several section.
 *				!!!NOTES This finction should only be called during MPInitialization because
 *				A NIC driver should call NdisOpenFile only from MiniportInitialize.
 * Arguments :    The pointer of the adapter
 *			   Code address (Virtual address, should fill descriptor with physical address)
 *			   Code size
 * Returns  :
 *        RT_STATUS_FAILURE - the following initialization process should be terminated
 *        RT_STATUS_SUCCESS - if firmware initialization process success
 */
bool fwsend_download_code(struct net_device *dev)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	rt_firmware		*pfirmware = (rt_firmware*)(&priv->firmware);

	bool			rt_status = true;
	u16			length = 0;
	u16			offset = 0;
	u16			frag_threhold;
	bool			last_init_packet = false;
	u32			check_txcmdwait_queueemptytime = 100000;
	u16			cmd_buf_len;
	u8			*ptr_cmd_buf;

	/* reset to 0 for first segment of img download */
	pfirmware->firmware_seg_index = 1;

	if(pfirmware->firmware_seg_index == pfirmware->firmware_seg_maxnum) {
		last_init_packet = 1;
	}

	cmd_buf_len = pfirmware->firmware_seg_container[pfirmware->firmware_seg_index-1].seg_size;
	ptr_cmd_buf = pfirmware->firmware_seg_container[pfirmware->firmware_seg_index-1].seg_ptr;
	rtl819xU_tx_cmd(dev, ptr_cmd_buf, cmd_buf_len, last_init_packet, DESC_PACKET_TYPE_INIT);

	rt_status = true;
	return rt_status;
}
#endif

//-----------------------------------------------------------------------------
// Procedure:    Check whether main code is download OK. If OK, turn on CPU
//
// Description:   CPU register locates in different page against general register.
//			    Switch to CPU register in the begin and switch back before return
//
//
// Arguments:   The pointer of the adapter
//
// Returns:
//        NDIS_STATUS_FAILURE - the following initialization process should be terminated
//        NDIS_STATUS_SUCCESS - if firmware initialization process success
//-----------------------------------------------------------------------------
bool CPUcheck_maincodeok_turnonCPU(struct net_device *dev)
{
	struct r8192_priv  *priv = ieee80211_priv(dev);
	bool rt_status = true;
	int  check_putcodeOK_time = 200000, check_bootOk_time = 200000;
	u32  CPU_status = 0;

	/* Check whether put code OK */
	do {
		CPU_status = read_nic_dword(dev, CPU_GEN);

		if((CPU_status&CPU_GEN_PUT_CODE_OK) || (priv->usb_error==true))
			break;

	}while(check_putcodeOK_time--);

	if(!(CPU_status&CPU_GEN_PUT_CODE_OK)) {
		RT_TRACE(COMP_ERR, "Download Firmware: Put code fail!\n");
		goto CPUCheckMainCodeOKAndTurnOnCPU_Fail;
	} else {
		RT_TRACE(COMP_FIRMWARE, "Download Firmware: Put code ok!\n");
	}

	/* Turn On CPU */
	CPU_status = read_nic_dword(dev, CPU_GEN);
	write_nic_byte(dev, CPU_GEN, (u8)((CPU_status|CPU_GEN_PWR_STB_CPU)&0xff));
	mdelay(1000);

	/* Check whether CPU boot OK */
	do {
		CPU_status = read_nic_dword(dev, CPU_GEN);

		if((CPU_status&CPU_GEN_BOOT_RDY)||(priv->usb_error == true))
			break;
	}while(check_bootOk_time--);

	if(!(CPU_status&CPU_GEN_BOOT_RDY)) {
		goto CPUCheckMainCodeOKAndTurnOnCPU_Fail;
	} else {
		RT_TRACE(COMP_FIRMWARE, "Download Firmware: Boot ready!\n");
	}

	return rt_status;

CPUCheckMainCodeOKAndTurnOnCPU_Fail:
	RT_TRACE(COMP_ERR, "ERR in %s()\n", __FUNCTION__);
	rt_status = FALSE;
	return rt_status;
}

bool CPUcheck_firmware_ready(struct net_device *dev)
{
	struct r8192_priv  *priv = ieee80211_priv(dev);
	bool		rt_status = true;
	int		check_time = 200000;
	u32		CPU_status = 0;

	/* Check Firmware Ready */
	do {
		CPU_status = read_nic_dword(dev, CPU_GEN);

		if((CPU_status&CPU_GEN_FIRM_RDY)||(priv->usb_error == true))
			break;

	}while(check_time--);

	if(!(CPU_status&CPU_GEN_FIRM_RDY))
		goto CPUCheckFirmwareReady_Fail;
	else
		RT_TRACE(COMP_FIRMWARE, "Download Firmware: Firmware ready!\n");

	return rt_status;

CPUCheckFirmwareReady_Fail:
	RT_TRACE(COMP_ERR, "ERR in %s()\n", __FUNCTION__);
	rt_status = false;
	return rt_status;

}

bool init_firmware(struct net_device *dev)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	bool			rt_status = TRUE;

	u8			*firmware_img_buf[3] = { &rtl8190_fwboot_array[0],
						   	 &rtl8190_fwmain_array[0],
						   	 &rtl8190_fwdata_array[0]};

	u32			firmware_img_len[3] = { sizeof(rtl8190_fwboot_array),
						   	sizeof(rtl8190_fwmain_array),
						   	sizeof(rtl8190_fwdata_array)};
	u32			file_length = 0;
	u8			*mapped_file = NULL;
	u32			init_step = 0;
	opt_rst_type_e	rst_opt = OPT_SYSTEM_RESET;
	firmware_init_step_e 	starting_state = FW_INIT_STEP0_BOOT;

	rt_firmware		*pfirmware = priv->pFirmware;
	const struct firmware 	*fw_entry;
	const char *fw_name[3] = { "RTL8192U/boot.img",
                           "RTL8192U/main.img",
			   "RTL8192U/data.img"};
	int rc;

	RT_TRACE(COMP_FIRMWARE, " PlatformInitFirmware()==>\n");

	if (pfirmware->firmware_status == FW_STATUS_0_INIT ) {
		/* it is called by reset */
		rst_opt = OPT_SYSTEM_RESET;
		starting_state = FW_INIT_STEP0_BOOT;
		// TODO: system reset

	}else if(pfirmware->firmware_status == FW_STATUS_5_READY) {
		/* it is called by Initialize */
		rst_opt = OPT_FIRMWARE_RESET;
		starting_state = FW_INIT_STEP2_DATA;
	}else {
		 RT_TRACE(COMP_FIRMWARE, "PlatformInitFirmware: undefined firmware state\n");
	}

	/*
	 * Download boot, main, and data image for System reset.
	 * Download data image for firmware reseta
	 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	priv->firmware_source = FW_SOURCE_HEADER_FILE;
#else
	priv->firmware_source = FW_SOURCE_IMG_FILE;
#endif
	for(init_step = starting_state; init_step <= FW_INIT_STEP2_DATA; init_step++) {
		/*
		 * Open Image file, and map file to contineous memory if open file success.
		 * or read image file from array. Default load from IMG file
		 */
		if(rst_opt == OPT_SYSTEM_RESET) {
			switch(priv->firmware_source) {
				case FW_SOURCE_IMG_FILE:
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
					if(pfirmware->firmware_buf_size[init_step] == 0) {
						rc = request_firmware(&fw_entry, fw_name[init_step],&priv->udev->dev);
						if(rc < 0 ) {
							RT_TRACE(COMP_ERR, "request firmware fail!\n");
							goto download_firmware_fail;
						}

						if(fw_entry->size > sizeof(pfirmware->firmware_buf[init_step])) {
							//RT_TRACE(COMP_ERR, "img file size exceed the container buffer fail!\n");
							 RT_TRACE(COMP_FIRMWARE, "img file size exceed the container buffer fail!, entry_size = %d, buf_size = %d\n",fw_entry->size,sizeof(pfirmware->firmware_buf[init_step]));

							goto download_firmware_fail;
						}

						if(init_step != FW_INIT_STEP1_MAIN) {
							memcpy(pfirmware->firmware_buf[init_step],fw_entry->data,fw_entry->size);
							pfirmware->firmware_buf_size[init_step] = fw_entry->size;
						} else {
#ifdef RTL8190P
							memcpy(pfirmware->firmware_buf[init_step],fw_entry->data,fw_entry->size);
							pfirmware->firmware_buf_size[init_step] = fw_entry->size;
#else
							memset(pfirmware->firmware_buf[init_step],0,128);
							memcpy(&pfirmware->firmware_buf[init_step][128],fw_entry->data,fw_entry->size);
							mapped_file = pfirmware->firmware_buf[init_step];
							pfirmware->firmware_buf_size[init_step] = fw_entry->size+128;
#endif
						}
						//pfirmware->firmware_buf_size = file_length;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
						if(rst_opt == OPT_SYSTEM_RESET) {
							release_firmware(fw_entry);
						}
#endif
					}
					mapped_file = pfirmware->firmware_buf[init_step];
					file_length = pfirmware->firmware_buf_size[init_step];
#endif

					break;

				case FW_SOURCE_HEADER_FILE:
					mapped_file =  firmware_img_buf[init_step];
					file_length  = firmware_img_len[init_step];
					if(init_step == FW_INIT_STEP2_DATA) {
						memcpy(pfirmware->firmware_buf[init_step], mapped_file, file_length);
						pfirmware->firmware_buf_size[init_step] = file_length;
					}
					break;

				default:
					break;
			}


		}else if(rst_opt == OPT_FIRMWARE_RESET ) {
			/* we only need to download data.img here */
			mapped_file = pfirmware->firmware_buf[init_step];
			file_length = pfirmware->firmware_buf_size[init_step];
		}

		/* Download image file */
		/* The firmware download process is just as following,
		 * 1. that is each packet will be segmented and inserted to the wait queue.
		 * 2. each packet segment will be put in the skb_buff packet.
		 * 3. each skb_buff packet data content will already include the firmware info
		 *   and Tx descriptor info
		 * */
		rt_status = fw_download_code(dev,mapped_file,file_length);

		if(rt_status != TRUE) {
			goto download_firmware_fail;
		}

		switch(init_step) {
			case FW_INIT_STEP0_BOOT:
				/* Download boot
				 * initialize command descriptor.
				 * will set polling bit when firmware code is also configured
				 */
				pfirmware->firmware_status = FW_STATUS_1_MOVE_BOOT_CODE;
#ifdef RTL8190P
				// To initialize IMEM, CPU move code  from 0x80000080, hence, we send 0x80 byte packet
				rt_status = fwSendNullPacket(dev, RTL8190_CPU_START_OFFSET);
				if(rt_status != true)
				{
					RT_TRACE(COMP_INIT, "fwSendNullPacket() fail ! \n");
					goto  download_firmware_fail;
				}
#endif
				//mdelay(1000);
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
				if(rt_status != TRUE) {
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
				if(rt_status != TRUE) {
					RT_TRACE(COMP_ERR, "CPUcheck_firmware_ready fail(%d)!\n",rt_status);
					goto download_firmware_fail;
				}

				/* wait until data code is initialized ready.*/
				pfirmware->firmware_status = FW_STATUS_5_READY;
				break;
		}
	}

	RT_TRACE(COMP_FIRMWARE, "Firmware Download Success\n");
	//assert(pfirmware->firmware_status == FW_STATUS_5_READY, ("Firmware Download Fail\n"));

	return rt_status;

download_firmware_fail:
	RT_TRACE(COMP_ERR, "ERR in %s()\n", __FUNCTION__);
	rt_status = FALSE;
	return rt_status;

}

#if 0
/*
 * Procedure:   (1)  Transform firmware code from little endian to big endian if required.
 *	        (2)  Number of bytes in Firmware downloading should be multiple
 *	   	     of 4 bytes. If length is not multiple of 4 bytes, appending of zeros is required
 *
 */
void CmdAppendZeroAndEndianTransform(
	u1Byte	*pDst,
	u1Byte	*pSrc,
	u2Byte   	*pLength)
{

	u2Byte	ulAppendBytes = 0, i;
	u2Byte	ulLength = *pLength;

//test only
	//memset(pDst, 0xcc, 12);


	/* Transform from little endian to big endian */
//#if DEV_BUS_TYPE==PCI_INTERFACE
#if 0
	for( i=0 ; i<(*pLength) ; i+=4)
	{
		if((i+3) < (*pLength))	pDst[i+0] = pSrc[i+3];
		if((i+2) < (*pLength))	pDst[i+1] = pSrc[i+2];
		if((i+1) < (*pLength))	pDst[i+2] = pSrc[i+1];
		if((i+0) < (*pLength))	pDst[i+3] = pSrc[i+0];
	}
#else
	pDst += USB_HWDESC_HEADER_LEN;
	ulLength -= USB_HWDESC_HEADER_LEN;

	for( i=0 ; i<ulLength ; i+=4) {
		if((i+3) < ulLength)	pDst[i+0] = pSrc[i+3];
		if((i+2) < ulLength)	pDst[i+1] = pSrc[i+2];
		if((i+1) < ulLength)	pDst[i+2] = pSrc[i+1];
		if((i+0) < ulLength)	pDst[i+3] = pSrc[i+0];

	}
#endif

	//1(2) Append Zero
	if(  ((*pLength) % 4)  >0)
	{
		ulAppendBytes = 4-((*pLength) % 4);

		for(i=0 ; i<ulAppendBytes; i++)
			pDst[  4*((*pLength)/4)  + i ] = 0x0;

		*pLength += ulAppendBytes;
	}
}
#endif

#if 0
RT_STATUS
CmdSendPacket(
	PADAPTER				Adapter,
	PRT_TCB					pTcb,
	PRT_TX_LOCAL_BUFFER 			pBuf,
	u4Byte					BufferLen,
	u4Byte					PacketType,
	BOOLEAN					bLastInitPacket
	)
{
	s2Byte		i;
	u1Byte		QueueID;
	u2Byte		firstDesc,curDesc = 0;
	u2Byte		FragIndex=0, FragBufferIndex=0;

	RT_STATUS	rtStatus = RT_STATUS_SUCCESS;

	CmdInitTCB(Adapter, pTcb, pBuf, BufferLen);


	if(CmdCheckFragment(Adapter, pTcb, pBuf))
		CmdFragmentTCB(Adapter, pTcb);
	else
		pTcb->FragLength[0] = (u2Byte)pTcb->BufferList[0].Length;

	QueueID=pTcb->SpecifiedQueueID;
#if DEV_BUS_TYPE!=USB_INTERFACE
	firstDesc=curDesc=Adapter->NextTxDescToFill[QueueID];
#endif

#if DEV_BUS_TYPE!=USB_INTERFACE
	if(VacancyTxDescNum(Adapter, QueueID) > pTcb->BufferCount)
#else
	if(PlatformIsTxQueueAvailable(Adapter, QueueID, pTcb->BufferCount) &&
		RTIsListEmpty(&Adapter->TcbWaitQueue[QueueID]))
#endif
	{
		pTcb->nDescUsed=0;

		for(i=0 ; i<pTcb->BufferCount ; i++)
		{
			Adapter->HalFunc.TxFillCmdDescHandler(
				Adapter,
				pTcb,
				QueueID,							//QueueIndex
				curDesc,							//index
				FragBufferIndex==0,						//bFirstSeg
				FragBufferIndex==(pTcb->FragBufCount[FragIndex]-1),		//bLastSeg
				pTcb->BufferList[i].VirtualAddress,				//VirtualAddress
				pTcb->BufferList[i].PhysicalAddressLow,				//PhyAddressLow
				pTcb->BufferList[i].Length,					//BufferLen
				i!=0,								//bSetOwnBit
				(i==(pTcb->BufferCount-1)) && bLastInitPacket,			//bLastInitPacket
				PacketType,							//DescPacketType
				pTcb->FragLength[FragIndex]					//PktLen
				);

			if(FragBufferIndex==(pTcb->FragBufCount[FragIndex]-1))
			{ // Last segment of the fragment.
				pTcb->nFragSent++;
			}

			FragBufferIndex++;
			if(FragBufferIndex==pTcb->FragBufCount[FragIndex])
			{
				FragIndex++;
				FragBufferIndex=0;
			}

#if DEV_BUS_TYPE!=USB_INTERFACE
			curDesc=(curDesc+1)%Adapter->NumTxDesc[QueueID];
#endif
			pTcb->nDescUsed++;
		}

#if DEV_BUS_TYPE!=USB_INTERFACE
		RTInsertTailList(&Adapter->TcbBusyQueue[QueueID], &pTcb->List);
		IncrementTxDescToFill(Adapter, QueueID, pTcb->nDescUsed);
		Adapter->HalFunc.SetTxDescOWNHandler(Adapter, QueueID, firstDesc);
		// TODO: should call poll use QueueID
		Adapter->HalFunc.TxPollingHandler(Adapter, TXCMD_QUEUE);
#endif
	}
	else
#if DEV_BUS_TYPE!=USB_INTERFACE
		goto CmdSendPacket_Fail;
#else
	{
		pTcb->bLastInitPacket = bLastInitPacket;
		RTInsertTailList(&Adapter->TcbWaitQueue[pTcb->SpecifiedQueueID], &pTcb->List);
	}
#endif

	return rtStatus;

#if DEV_BUS_TYPE!=USB_INTERFACE
CmdSendPacket_Fail:
	rtStatus = RT_STATUS_FAILURE;
	return rtStatus;
#endif

}
#endif




#if 0
RT_STATUS
FWSendNullPacket(
	IN	PADAPTER		Adapter,
	IN	u4Byte			Length
)
{
	RT_STATUS	rtStatus = RT_STATUS_SUCCESS;


	PRT_TCB					pTcb;
	PRT_TX_LOCAL_BUFFER 	pBuf;
	BOOLEAN					bLastInitPacket = FALSE;

	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

#if DEV_BUS_TYPE==USB_INTERFACE
	Length += USB_HWDESC_HEADER_LEN;
#endif

	//Get TCB and local buffer from common pool. (It is shared by CmdQ, MgntQ, and USB coalesce DataQ)
	if(MgntGetBuffer(Adapter, &pTcb, &pBuf))
	{
		PlatformZeroMemory(pBuf->Buffer.VirtualAddress, Length);
		rtStatus = CmdSendPacket(Adapter, pTcb, pBuf, Length, DESC_PACKET_TYPE_INIT, bLastInitPacket);	//0 : always set LastInitPacket to zero
//#if HAL_CODE_BASE != RTL8190HW
//		// TODO: for test only
//		ReturnTCB(Adapter, pTcb, RT_STATUS_SUCCESS);
//#endif
		if(rtStatus == RT_STATUS_FAILURE)
			goto CmdSendNullPacket_Fail;
	}else
		goto CmdSendNullPacket_Fail;

	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);
	return rtStatus;


CmdSendNullPacket_Fail:
	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);
	rtStatus = RT_STATUS_FAILURE;
	RT_ASSERT(rtStatus == RT_STATUS_SUCCESS, ("CmdSendDownloadCode fail !!\n"));
	return rtStatus;
}
#endif


