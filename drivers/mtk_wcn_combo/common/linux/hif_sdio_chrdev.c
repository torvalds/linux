#include "hif_sdio.h"
#include "hif_sdio_chrdev.h"



static int hif_sdio_proc(void * pvData);
static int hif_sdio_open(struct inode *inode, struct file *file);
static int hif_sdio_release(struct inode *inode, struct file *file);
static long hif_sdio_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static ssize_t hif_sdio_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);



unsigned int hifSdioMajor = 0;

#define COMBO_IOC_MAGIC        'h'
#define COMBO_IOCTL_GET_CHIP_ID  _IOR(COMBO_IOC_MAGIC, 0, int)
#define COMBO_IOCTL_SET_CHIP_ID  _IOW(COMBO_IOC_MAGIC, 1, int)

MTK_WCN_HIF_SDIO_CHIP_INFO gChipInfoArray[] = {
    /* MT6620 */ /* Not an SDIO standard class device */
    { {SDIO_DEVICE(0x037A, 0x020A)}, 0x6620 }, /* SDIO1:FUNC1:WIFI */
    { {SDIO_DEVICE(0x037A, 0x020B)}, 0x6620 }, /* SDIO2:FUNC1:BT+FM+GPS */
    { {SDIO_DEVICE(0x037A, 0x020C)}, 0x6620 }, /* 2-function (SDIO2:FUNC1:BT+FM+GPS, FUNC2:WIFI) */
    
    /* MT6628 */ /* SDIO1: Wi-Fi, SDIO2: BGF */
    { {SDIO_DEVICE(0x037A, 0x6628)}, 0x6628},

};



struct file_operations hifDevOps = 
{
	.owner = THIS_MODULE,
	.open = hif_sdio_open,
	.release = hif_sdio_release,
	.unlocked_ioctl = hif_sdio_unlocked_ioctl,
	.read = hif_sdio_read,
};

struct class *pHifClass = NULL;
struct device *pHifDev = NULL;
UCHAR *HifClassName = "hifsdiod";
UCHAR *kObjName = "hifsdiod";


struct task_struct *gConIdQueryThread;
wait_queue_head_t gHifsdiodEvent;

//OSAL_THREAD           gConIdQueryThread;
//OSAL_EVENT            gHifsdiodEvent;
UCHAR *gConIdQueryName = "consys-id-query";
INT32 gComboChipId = -1;



INT32 hifsdiod_start(void)
{
    int iRet = -1;
	init_waitqueue_head(&gHifsdiodEvent);
#if 0
    osal_event_init(&gHifsdiodEvent);
    gConIdQueryThread.pThreadData = (VOID *)NULL;
    gConIdQueryThread.pThreadFunc = (VOID *)hif_sdio_proc;
    osal_memcpy(gConIdQueryThread.threadName, gConIdQueryName , osal_strlen(gConIdQueryName));
	

    iRet = osal_thread_create(&gConIdQueryThread);
    if (iRet < 0) 
    {
        HIF_SDIO_ERR_FUNC("osal_thread_create fail...\n");
        goto ERR_EXIT1;
    }
#else
    gConIdQueryThread = kthread_create(hif_sdio_proc, NULL, gConIdQueryName);
    if (NULL == gConIdQueryThread) 
    {
        HIF_SDIO_ERR_FUNC("osal_thread_create fail...\n");
        goto ERR_EXIT1;
    }

#endif

#if 0
    /* Start STPd thread*/
    iRet = osal_thread_run(&gConIdQueryThread);
    if(iRet < 0)
    {
        HIF_SDIO_ERR_FUNC("osal_thread_run FAILS\n");
        goto ERR_EXIT1;
    }
#else
    if (gConIdQueryThread) {
        wake_up_process(gConIdQueryThread);
    }
	else
	{
	    goto ERR_EXIT1;
	}
#endif
    iRet = 0;
	HIF_SDIO_INFO_FUNC("succeed\n");

	return iRet;
	
ERR_EXIT1:
	HIF_SDIO_ERR_FUNC("failed\n");
	return iRet;
 }


INT32 hifsdiod_stop(void)
{
    if (gConIdQueryThread) {
		HIF_SDIO_INFO_FUNC("inform hifsdiod exit..\n");
        kthread_stop(gConIdQueryThread);
		gConIdQueryThread = NULL;
    }
	return 0;
}


static int hif_sdio_proc(void * pvData)
{
    while (!kthread_should_stop()) 
    {
        //HIF_SDIO_INFO_FUNC("enter sleep.\n");
		osal_msleep(10000);
		//HIF_SDIO_INFO_FUNC("wakeup\n");		
    }
    HIF_SDIO_INFO_FUNC("hifsdiod exit.\n"); 
	return 0;
}


static int hif_sdio_open(struct inode *inode, struct file *file)
{
    HIF_SDIO_INFO_FUNC(" ++\n");
	HIF_SDIO_INFO_FUNC(" --\n");
	return 0;
}

static int hif_sdio_release(struct inode *inode, struct file *file)
{
    HIF_SDIO_INFO_FUNC(" ++\n");
	HIF_SDIO_INFO_FUNC(" --\n");

	return 0;
}


static ssize_t hif_sdio_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)

{
    HIF_SDIO_INFO_FUNC(" ++\n");
	HIF_SDIO_INFO_FUNC(" --\n");

    return 0;
}

static long hif_sdio_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int retval = 0;

    HIF_SDIO_DBG_FUNC("cmd (%d)\n", cmd);

    switch(cmd)
    {
        case COMBO_IOCTL_GET_CHIP_ID:
			gComboChipId = 0x6628;
			retval = gComboChipId;
			HIF_SDIO_INFO_FUNC("get combo chip id: 0x%x\n", gComboChipId);
			break;
		case COMBO_IOCTL_SET_CHIP_ID:
			gComboChipId = arg;
			HIF_SDIO_INFO_FUNC("set combo chip id to 0x%x\n", gComboChipId);
			break;
		default:
			HIF_SDIO_WARN_FUNC("unknown cmd (%d)\n", cmd);
			retval = 0;
			break;
    }
	return retval;
}
	

INT32 hif_sdio_is_chipid_valid (INT32 chipId)
{
	INT32 index = -1;
	
    INT32 left = 0;
	INT32 middle = 0;
    INT32 right = sizeof (gChipInfoArray) / sizeof (gChipInfoArray[0]) - 1;
	if ((chipId < gChipInfoArray[left].chipId) || (chipId > gChipInfoArray[right].chipId))
		return index;

	middle = (left + right) / 2;
	
	while (left <= right)
	{
	    if (chipId > gChipInfoArray[middle].chipId)
	    {
	        left = middle + 1;
	    }
		else if (chipId < gChipInfoArray[middle].chipId)
		{
		    right = middle - 1;
		}
		else
		{
		    index = middle;
			break;
		}
		middle = (left + right) / 2;
	}
	
    if (0 > index)
    {
    	HIF_SDIO_ERR_FUNC("no supported chipid found\n");
    }
	else
	{
		HIF_SDIO_INFO_FUNC("index:%d, chipId:0x%x\n", index, gChipInfoArray[index].chipId);
	}

	return index;
}

INT32 hif_sdio_match_chipid_by_dev_id (const struct sdio_device_id *id)
{
    INT32 maxIndex = sizeof (gChipInfoArray) / sizeof (gChipInfoArray[0]);
    INT32 index = 0;
    struct sdio_device_id *localId = NULL;
	INT32 chipId = -1;
	for (index = 0; index < maxIndex; index++)
	{
	    localId = &(gChipInfoArray[index].deviceId);
	    if ((localId->vendor == id->vendor) && (localId->device == id->device))
	    {
	        chipId = gChipInfoArray[index].chipId;
			HIF_SDIO_INFO_FUNC("valid chipId found, index(%d), vendor id(0x%x), device id(0x%x), chip id(0x%x)\n", index, localId->vendor, localId->device, chipId);
			gComboChipId = chipId;
	        break;
	    }
	}
	if (0 > chipId)
	{
	    HIF_SDIO_ERR_FUNC("No valid chipId found, vendor id(0x%x), device id(0x%x)\n", id->vendor, id->device);
	}
	
    return chipId;
}


INT32 mtk_wcn_hif_sdio_query_chipid(INT32 waitFlag)
{
    UINT32 timeSlotMs = 200;
	UINT32 maxTimeSlot = 15;
	UINT32 counter = 0;
    //gComboChipId = 0x6628;
    if (0 == waitFlag)
		return gComboChipId;
	if (0 <= hif_sdio_is_chipid_valid(gComboChipId))
		return gComboChipId;
    wmt_plat_pwr_ctrl(FUNC_ON);
	wmt_plat_sdio_ctrl(WMT_SDIO_SLOT_SDIO1, FUNC_ON);
	while (counter < maxTimeSlot)
	{
	    if (0 <= hif_sdio_is_chipid_valid(gComboChipId))
			break;
	    osal_msleep(timeSlotMs);
	    counter++;
	}
	
	wmt_plat_sdio_ctrl(WMT_SDIO_SLOT_SDIO1, FUNC_OFF);
	wmt_plat_pwr_ctrl(FUNC_OFF);
	return gComboChipId;
}
EXPORT_SYMBOL(mtk_wcn_hif_sdio_query_chipid);

INT32 mtk_wcn_hif_sdio_tell_chipid(INT32 chipId)
{

	gComboChipId = chipId;
    HIF_SDIO_INFO_FUNC("set combo chip id to 0x%x\n", gComboChipId);

	return gComboChipId;
}
EXPORT_SYMBOL(mtk_wcn_hif_sdio_tell_chipid);

INT32 hif_sdio_create_dev_node(void)
{

	INT32 iResult = -1;
	
	HIF_SDIO_DBG_FUNC( "++");
	iResult = register_chrdev(hifSdioMajor, kObjName, &hifDevOps);
	if(0 > iResult)
	{
		HIF_SDIO_ERR_FUNC("register_chrdev failed.\n");
		iResult = -1;
	}
	else
	{
		hifSdioMajor = hifSdioMajor == 0 ? iResult : hifSdioMajor;
		HIF_SDIO_INFO_FUNC("register_chrdev succeed, mtk_jajor = %d\n", hifSdioMajor);
		pHifClass = class_create(THIS_MODULE, HifClassName);
		if(IS_ERR(pHifClass))
		{
			HIF_SDIO_ERR_FUNC("class_create error\n");
			iResult = -2;
		}
		else
		{
			pHifDev = device_create(pHifClass, NULL, MKDEV(hifSdioMajor, 0), NULL, HifClassName, "%d", 0);
			if(IS_ERR(pHifDev))
			{
				HIF_SDIO_ERR_FUNC("device_create error:%ld\n", PTR_ERR(pHifDev));
				iResult = -3;
			}
			else
			{
			    HIF_SDIO_INFO_FUNC("device_create succeed\n");
			    iResult = 0;
			}
		}
	}
    return iResult;
}


INT32 hif_sdio_remove_dev_node(void)
{
    if(pHifDev != NULL)
	{
		device_destroy(pHifClass, MKDEV(hifSdioMajor, 0));
		pHifDev = NULL;
	}
	if(pHifClass != NULL)
	{
		class_destroy(pHifClass);
		pHifClass = NULL;
	}
	
	if(hifSdioMajor != 0)
	{
		unregister_chrdev(hifSdioMajor, kObjName);
		hifSdioMajor = 0;
	}
    return 0;
}


