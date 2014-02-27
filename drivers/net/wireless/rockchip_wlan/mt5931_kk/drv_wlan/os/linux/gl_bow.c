/*
** $Id: @(#) gl_bow.c@@
*/

/*! \file   gl_bow.c
    \brief  Main routines of Linux driver interface for 802.11 PAL (BT 3.0 + HS)

    This file contains the main routines of Linux driver for MediaTek Inc. 802.11
    Wireless LAN Adapters.
*/

/*******************************************************************************
* Copyright (c) 2007 MediaTek Inc.
*
* All rights reserved. Copying, compilation, modification, distribution
* or any other use whatsoever of this material is strictly prohibited
* except in accordance with a Software License Agreement with
* MediaTek Inc.
********************************************************************************
*/

/*******************************************************************************
* LEGAL DISCLAIMER
*
* BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND
* AGREES THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK
* SOFTWARE") RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
* PROVIDED TO BUYER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY
* DISCLAIMS ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT
* LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
* PARTICULAR PURPOSE OR NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE
* ANY WARRANTY WHATSOEVER WITH RESPECT TO THE SOFTWARE OF ANY THIRD PARTY
* WHICH MAY BE USED BY, INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK
* SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY
* WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE
* FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S SPECIFICATION OR TO
* CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
* BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
* LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL
* BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT
* ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
* BUYER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
* THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
* WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT
* OF LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING
* THEREOF AND RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN
* FRANCISCO, CA, UNDER THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE
* (ICC).
********************************************************************************
*/

/*
** $Log: gl_bow.c $
** 
** 07 24 2012 yuche.tsai
** NULL
** Bug fix for JB.
 *
 * 02 16 2012 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * [ALPS00235223] [Rose][ICS][Cross Feature][AEE-IPANIC]The device reboot automatically and then the "KE" pops up after you turn on the "Airplane mode".(once)
 * 
 * [Root Cause]
 * PAL operates BOW char dev poll after BOW char dev is registered.
 * 
 * [Solution]
 * Rejects PAL char device operation after BOW is unregistered or when wlan GLUE_FLAG_HALT is set.
 * 
 * This is a workaround for BOW driver robustness, happens only in ICS.
 * 
 * Root cause should be fixed by CR [ALPS00231570]
 *
 * 02 03 2012 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * [ALPS00118114] [Rose][ICS][Free Test][Bluetooth]The "KE" pops up after you turn on the airplane mode.(5/5)
 * 
 * [Root Cause]
 * PAL operates BOW char dev poll after BOW char dev is registered.
 * 
 * [Solution]
 * Rejects PAL char device operation after BOW is unregistered.
 * 
 * Happens only in ICS.
 * 
 * Notified PAL owener to reivew MTKBT/PAL closing BOW char dev procedure.
 * 
 * [Side Effect]
 * None.
 *
 * 01 16 2012 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Support BOW for 5GHz band.
 *
 * 11 10 2011 cp.wu
 * [WCXRP00001098] [MT6620 Wi-Fi][Driver] Replace printk by DBG LOG macros in linux porting layer
 * 1. eliminaite direct calls to printk in porting layer.
 * 2. replaced by DBGLOG, which would be XLOG on ALPS platforms.
 *
 * 10 25 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Modify ampc0 char device for major number 151 for all MT6575 projects.
 *
 * 07 28 2011 cp.wu
 * [WCXRP00000884] [MT6620 Wi-Fi][Driver] Deprecate ioctl interface by unlocked ioctl
 * unlocked_ioctl returns as long instead of int.
 *
 * 07 28 2011 cp.wu
 * [WCXRP00000884] [MT6620 Wi-Fi][Driver] Deprecate ioctl interface by unlocked ioctl
 * migrate to unlocked ioctl interface
 *
 * 04 12 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Add WMM IE for BOW initiator data.
 *
 * 04 10 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Change Link disconnection event procedure for hotspot and change skb length check to 1514 bytes.
 *
 * 04 09 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Change Link connection event procedure and change skb length check to 1512 bytes.
 *
 * 03 27 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Support multiple physical link.
 *
 * 03 06 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Sync BOW Driver to latest person development branch version..
 *
 * 03 03 2011 jeffrey.chang
 * [WCXRP00000512] [MT6620 Wi-Fi][Driver] modify the net device relative functions to support the H/W multiple queue
 * support concurrent network
 *
 * 03 03 2011 jeffrey.chang
 * [WCXRP00000512] [MT6620 Wi-Fi][Driver] modify the net device relative functions to support the H/W multiple queue
 * replace alloc_netdev to alloc_netdev_mq for BoW
 *
 * 03 03 2011 jeffrey.chang
 * [WCXRP00000512] [MT6620 Wi-Fi][Driver] modify the net device relative functions to support the H/W multiple queue
 * modify net device relative functions to support multiple H/W queues
 *
 * 02 15 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Update net register and BOW for concurrent features.
 *
 * 02 10 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Fix kernel API change issue.
 * Before ALPS 2.2 (2.2 included), kfifo_alloc() is 
 * struct kfifo *kfifo_alloc(unsigned int size, gfp_t gfp_mask, spinlock_t *lock);
 * After ALPS 2.3, kfifo_alloc() is changed to
 * int kfifo_alloc(struct kfifo *fifo, unsigned int size, gfp_t gfp_mask);
 *
 * 02 09 2011 cp.wu
 * [WCXRP00000430] [MT6620 Wi-Fi][Firmware][Driver] Create V1.2 branch for MT6620E1 and MT6620E3
 * create V1.2 driver branch based on label MT6620_WIFI_DRIVER_V1_2_110209_1031 
 * with BOW and P2P enabled as default
 *
 * 02 08 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Replace kfifo_get and kfifo_put with kfifo_out and kfifo_in.
 * Update BOW get MAC status, remove returning event for AIS network type.
 *
 * 01 12 2011 cp.wu
 * [WCXRP00000357] [MT6620 Wi-Fi][Driver][Bluetooth over Wi-Fi] add another net device interface for BT AMP
 * implementation of separate BT_OVER_WIFI data path.
 *
 * 01 12 2011 cp.wu
 * [WCXRP00000356] [MT6620 Wi-Fi][Driver] fill mac header length for security frames 'cause hardware header translation needs such information
 * fill mac header length information for 802.1x frames.
 *
 * 11 11 2010 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Fix BoW timer assert issue.
 *
 * 09 14 2010 chinghwa.yu
 * NULL
 * Add bowRunEventAAAComplete.
 *
 * 09 14 2010 cp.wu
 * NULL
 * correct typo: POLLOUT instead of POLL_OUT
 *
 * 09 13 2010 cp.wu
 * NULL
 * add waitq for poll() and read().
 *
 * 08 24 2010 chinghwa.yu
 * NULL
 * Update BOW for the 1st time.
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base 
 * [MT6620 5931] Create driver base
 *
 * 05 05 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * change variable names for multiple physical link to match with coding convention
 *
 * 05 05 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * multiple BoW interfaces need to compare with peer address
 *
 * 04 28 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * change prefix for data structure used to communicate with 802.11 PAL
 * to avoid ambiguous naming with firmware interface
 *
 * 04 28 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * fix kalIndicateBOWEvent.
 *
 * 04 27 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * add multiple physical link support
 *
 * 04 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * add framework for BT-over-Wi-Fi support.
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * 1) prPendingCmdInfo is replaced by queue for multiple handler capability
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * 2) command sequence number is now increased atomically 
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * 3) private data could be hold and taken use for other purpose
**
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "gl_os.h"
#include "debug.h"
#include "wlan_lib.h"
#include "gl_wext.h"
#include "precomp.h"
#include <linux/poll.h>
#include "bss.h"

#if CFG_ENABLE_BT_OVER_WIFI

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
/* @FIXME if there is command/event with payload length > 28 */
#define MAX_BUFFER_SIZE         (64)

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

#if CFG_BOW_TEST
    UINT_32 g_u4PrevSysTime = 0;
    UINT_32 g_u4CurrentSysTime = 0;
    UINT_32 g_arBowRevPalPacketTime[11];
#endif

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

// forward declarations
static ssize_t
mt6620_ampc_read(
    IN struct file *filp,
    IN char __user *buf,
    IN size_t size,
    IN OUT loff_t *ppos);

static ssize_t
mt6620_ampc_write(
    IN struct file *filp,
    OUT const char __user *buf,
    IN size_t size,
    IN OUT loff_t *ppos);

static long
mt6620_ampc_ioctl(
    IN struct file *filp,
    IN unsigned int cmd,
    IN OUT unsigned long arg);

static unsigned int
mt6620_ampc_poll(
    IN struct file *filp,
    IN poll_table *wait);

static int
mt6620_ampc_open(
    IN struct inode *inodep,
    IN struct file *filp);

static int
mt6620_ampc_release(
    IN struct inode *inodep,
    IN struct file *filp);


// character file operations
static const struct file_operations mt6620_ampc_fops = {
    //.owner              = THIS_MODULE,
    .read               = mt6620_ampc_read,
    .write              = mt6620_ampc_write,
    .unlocked_ioctl     = mt6620_ampc_ioctl,
    .poll               = mt6620_ampc_poll,
    .open               = mt6620_ampc_open,
    .release            = mt6620_ampc_release,
};

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/


/*----------------------------------------------------------------------------*/
/*!
* \brief Register for character device to communicate with 802.11 PAL
*
* \param[in] prGlueInfo      Pointer to glue info
*
* \return   TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
glRegisterAmpc (
    IN P_GLUE_INFO_T prGlueInfo
    )
{
    ASSERT(prGlueInfo);

    if(prGlueInfo->rBowInfo.fgIsRegistered == TRUE) {
        return FALSE;
    }
    else {
#if 0
        // 1. allocate major number dynamically

    if(alloc_chrdev_region(&(prGlueInfo->rBowInfo.u4DeviceNumber),
                    0,  // first minor number
                    1,  // number
                    GLUE_BOW_DEVICE_NAME) !=0)

            return FALSE;
#endif

#if 1

#if defined (CONFIG_AMPC_CDEV_NUM)
    prGlueInfo->rBowInfo.u4DeviceNumber = MKDEV(CONFIG_AMPC_CDEV_NUM, 0);
#else
    prGlueInfo->rBowInfo.u4DeviceNumber = MKDEV(226, 0);
#endif

    if(register_chrdev_region(prGlueInfo->rBowInfo.u4DeviceNumber,
                    1,  // number
                    GLUE_BOW_DEVICE_NAME) !=0)

            return FALSE;
#endif

        // 2. spin-lock initialization
 //       spin_lock_init(&(prGlueInfo->rBowInfo.rSpinLock));

        // 3. initialize kfifo
/*        prGlueInfo->rBowInfo.prKfifo = kfifo_alloc(GLUE_BOW_KFIFO_DEPTH,
                GFP_KERNEL,
                &(prGlueInfo->rBowInfo.rSpinLock));*/
            if ((kfifo_alloc((struct kfifo *) &(prGlueInfo->rBowInfo.rKfifo), GLUE_BOW_KFIFO_DEPTH, GFP_KERNEL)))
                goto fail_kfifo_alloc;

//        if(prGlueInfo->rBowInfo.prKfifo == NULL)
        if(&(prGlueInfo->rBowInfo.rKfifo) == NULL)
            goto fail_kfifo_alloc;

        // 4. initialize cdev
        cdev_init(&(prGlueInfo->rBowInfo.cdev), &mt6620_ampc_fops);
       // prGlueInfo->rBowInfo.cdev.owner = THIS_MODULE;
        prGlueInfo->rBowInfo.cdev.ops = &mt6620_ampc_fops;

        // 5. add character device
        if(cdev_add(&(prGlueInfo->rBowInfo.cdev),
                    prGlueInfo->rBowInfo.u4DeviceNumber,
                    1))
            goto fail_cdev_add;


        // 6. in queue initialization
        init_waitqueue_head(&(prGlueInfo->rBowInfo.outq));

        // 7. finish
        prGlueInfo->rBowInfo.fgIsRegistered = TRUE;
        return TRUE;

fail_cdev_add:
            kfifo_free(&(prGlueInfo->rBowInfo.rKfifo));
//        kfifo_free(prGlueInfo->rBowInfo.prKfifo);
fail_kfifo_alloc:
        unregister_chrdev_region(prGlueInfo->rBowInfo.u4DeviceNumber, 1);
        return FALSE;
    }
} /* end of glRegisterAmpc */


/*----------------------------------------------------------------------------*/
/*!
* \brief Unregister character device for communicating with 802.11 PAL
*
* \param[in] prGlueInfo      Pointer to glue info
*
* \return   TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
glUnregisterAmpc (
    IN P_GLUE_INFO_T prGlueInfo
    )
{
    ASSERT(prGlueInfo);

    if(prGlueInfo->rBowInfo.fgIsRegistered == FALSE) {
        return FALSE;
    }
    else {
        prGlueInfo->rBowInfo.fgIsRegistered = FALSE;

        // 1. free netdev if necessary
#if CFG_BOW_SEPARATE_DATA_PATH
        kalUninitBowDevice(prGlueInfo);
#endif

        // 2. removal of character device
        cdev_del(&(prGlueInfo->rBowInfo.cdev));

        // 3. free kfifo
//        kfifo_free(prGlueInfo->rBowInfo.prKfifo);
        kfifo_free(&(prGlueInfo->rBowInfo.rKfifo));
//        prGlueInfo->rBowInfo.prKfifo = NULL;
//        prGlueInfo->rBowInfo.rKfifo = NULL;

        // 4. free device number
        unregister_chrdev_region(prGlueInfo->rBowInfo.u4DeviceNumber, 1);

        return TRUE;
    }
} /* end of glUnregisterAmpc */


/*----------------------------------------------------------------------------*/
/*!
* \brief read handler for character device to communicate with 802.11 PAL
*
* \param[in]
* \return
*           Follows Linux Character Device Interface
*
*/
/*----------------------------------------------------------------------------*/
static ssize_t
mt6620_ampc_read(
    IN struct file *filp,
    IN char __user *buf,
    IN size_t size,
    IN OUT loff_t *ppos)
{
    UINT_8 aucBuffer[MAX_BUFFER_SIZE];
    ssize_t retval;

    P_GLUE_INFO_T prGlueInfo;
    prGlueInfo = (P_GLUE_INFO_T)(filp->private_data);

    ASSERT(prGlueInfo);

    if ((prGlueInfo->rBowInfo.fgIsRegistered == FALSE) || (prGlueInfo->u4Flag & GLUE_FLAG_HALT)) {
        return -EFAULT;
    }

    // size check
//    if(kfifo_len(prGlueInfo->rBowInfo.prKfifo) >= size)
    if(kfifo_len(&(prGlueInfo->rBowInfo.rKfifo)) >= size)
        retval = size;
    else
        retval = kfifo_len(&(prGlueInfo->rBowInfo.rKfifo));
//        retval = kfifo_len(prGlueInfo->rBowInfo.prKfifo);

//    kfifo_get(prGlueInfo->rBowInfo.prKfifo, aucBuffer, retval);
//    kfifo_out(prGlueInfo->rBowInfo.prKfifo, aucBuffer, retval);
    if (!(kfifo_out(&(prGlueInfo->rBowInfo.rKfifo), aucBuffer, retval)))
        retval = -EIO;

    if(copy_to_user(buf, aucBuffer, retval))
        retval = -EIO;

    return retval;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief write handler for character device to communicate with 802.11 PAL
*
* \param[in]
* \return
*           Follows Linux Character Device Interface
*
*/
/*----------------------------------------------------------------------------*/
static ssize_t
mt6620_ampc_write(
    IN struct file *filp,
    OUT const char __user *buf,
    IN size_t size,
    IN OUT loff_t *ppos)
{
#if CFG_BOW_TEST
    UINT_8 i;
#endif

    UINT_8 aucBuffer[MAX_BUFFER_SIZE];
    P_AMPC_COMMAND prCmd;
    P_GLUE_INFO_T prGlueInfo;

    prGlueInfo = (P_GLUE_INFO_T)(filp->private_data);
    ASSERT(prGlueInfo);

    if ((prGlueInfo->rBowInfo.fgIsRegistered == FALSE) || (prGlueInfo->u4Flag & GLUE_FLAG_HALT)) {
        return -EFAULT;
    }

    if(size > MAX_BUFFER_SIZE)
        return -EINVAL;
    else if(copy_from_user(aucBuffer, buf, size))
        return -EIO;

#if CFG_BOW_TEST
    DBGLOG(BOW, EVENT, ("AMP driver CMD buffer size : %d.\n", size));

    for(i = 0; i < MAX_BUFFER_SIZE; i++)
    {
        DBGLOG(BOW, EVENT, ("AMP write content : 0x%x.\n", aucBuffer[i]));
    }

    DBGLOG(BOW, EVENT, ("BoW CMD write.\n"));
#endif

    prCmd = (P_AMPC_COMMAND) aucBuffer;

 #if CFG_BOW_TEST
    DBGLOG(BOW, EVENT, ("AMP write content payload length : %d.\n", prCmd->rHeader.u2PayloadLength));

    DBGLOG(BOW, EVENT, ("AMP write content header length : %d.\n", sizeof(AMPC_COMMAND_HEADER_T)));
 #endif

    // size check
    if(prCmd->rHeader.u2PayloadLength + sizeof(AMPC_COMMAND_HEADER_T) != size)
    {
  #if CFG_BOW_TEST
        DBGLOG(BOW, EVENT, ("Wrong CMD total length.\n"));
  #endif

        return -EINVAL;
    }

    if(wlanbowHandleCommand(prGlueInfo->prAdapter, prCmd) == WLAN_STATUS_SUCCESS)
        return size;
    else
        return -EINVAL;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief ioctl handler for character device to communicate with 802.11 PAL
*
* \param[in]
* \return
*           Follows Linux Character Device Interface
*
*/
/*----------------------------------------------------------------------------*/
static long
mt6620_ampc_ioctl(
    IN struct file *filp,
    IN unsigned int cmd,
    IN OUT unsigned long arg)
{
    int err = 0;
    P_GLUE_INFO_T prGlueInfo;
    prGlueInfo = (P_GLUE_INFO_T)(filp->private_data);

    ASSERT(prGlueInfo);

    if ((prGlueInfo->rBowInfo.fgIsRegistered == FALSE) || (prGlueInfo->u4Flag & GLUE_FLAG_HALT)) {
        return -EFAULT;
    }

    // permission check
    if(_IOC_DIR(cmd) & _IOC_READ)
        err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
        err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    if (err)
        return -EFAULT;

    // no ioctl is implemented yet
    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief ioctl handler for character device to communicate with 802.11 PAL
*
* \param[in]
* \return
*           Follows Linux Character Device Interface
*
*/
/*----------------------------------------------------------------------------*/
static unsigned int
mt6620_ampc_poll(
    IN struct file *filp,
    IN poll_table *wait)
{
    unsigned int retval;
    P_GLUE_INFO_T prGlueInfo;
    prGlueInfo = (P_GLUE_INFO_T)(filp->private_data);

    ASSERT(prGlueInfo);

    if ((prGlueInfo->rBowInfo.fgIsRegistered == FALSE) || (prGlueInfo->u4Flag & GLUE_FLAG_HALT)) {
        return -EFAULT;
    }

    poll_wait(filp, &prGlueInfo->rBowInfo.outq, wait);

    retval = (POLLOUT | POLLWRNORM); // always accepts incoming command packets

//    DBGLOG(BOW, EVENT, ("mt6620_ampc_pol, POLLOUT | POLLWRNORM, %x\n", retval));

//    if(kfifo_len(prGlueInfo->rBowInfo.prKfifo) > 0)
    if(kfifo_len(&(prGlueInfo->rBowInfo.rKfifo)) > 0)
    {
        retval |= (POLLIN | POLLRDNORM);

//        DBGLOG(BOW, EVENT, ("mt6620_ampc_pol, POLLIN | POLLRDNORM, %x\n", retval));

    }

    return retval;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief open handler for character device to communicate with 802.11 PAL
*
* \param[in]
* \return
*           Follows Linux Character Device Interface
*
*/
/*----------------------------------------------------------------------------*/
static int
mt6620_ampc_open(
    IN struct inode *inodep,
    IN struct file *filp)
{
     P_GLUE_INFO_T  prGlueInfo;
     P_GL_BOW_INFO  prBowInfo;

     prBowInfo = container_of(inodep->i_cdev, GL_BOW_INFO, cdev);
     ASSERT(prBowInfo);

     prGlueInfo = container_of(prBowInfo, GLUE_INFO_T, rBowInfo);
     ASSERT(prGlueInfo);

     // set-up private data
     filp->private_data = prGlueInfo;

     return 0;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief close handler for character device to communicate with 802.11 PAL
*
* \param[in]
* \return
*           Follows Linux Character Device Interface
*
*/
/*----------------------------------------------------------------------------*/
static int
mt6620_ampc_release(
    IN struct inode *inodep,
    IN struct file *filp)
{
    P_GLUE_INFO_T prGlueInfo;
    prGlueInfo = (P_GLUE_INFO_T)(filp->private_data);

    ASSERT(prGlueInfo);

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to indicate event for Bluetooth over Wi-Fi
*
* \param[in]
*           prGlueInfo
*           prEvent
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID
kalIndicateBOWEvent(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN P_AMPC_EVENT prEvent
    )
{
    size_t u4AvailSize, u4EventSize;

    ASSERT(prGlueInfo);
    ASSERT(prEvent);

    // check device
    if ((prGlueInfo->rBowInfo.fgIsRegistered == FALSE) || (prGlueInfo->u4Flag & GLUE_FLAG_HALT)) {
        return;
    }

/*    u4AvailSize = 
        GLUE_BOW_KFIFO_DEPTH - kfifo_len(prGlueInfo->rBowInfo.prKfifo);*/

    u4AvailSize =
        GLUE_BOW_KFIFO_DEPTH - kfifo_len(&(prGlueInfo->rBowInfo.rKfifo));


    u4EventSize =
        prEvent->rHeader.u2PayloadLength + sizeof(AMPC_EVENT_HEADER_T);

    // check kfifo availability
    if(u4AvailSize < u4EventSize) {
        DBGLOG(BOW, EVENT, ("[bow] no space for event: %d/%d\n",
                u4EventSize,
                u4AvailSize));
        return;
    }

    // queue into kfifo
//    kfifo_put(prGlueInfo->rBowInfo.prKfifo, (PUINT_8)prEvent, u4EventSize);
//    kfifo_in(prGlueInfo->rBowInfo.prKfifo, (PUINT_8)prEvent, u4EventSize);
    kfifo_in(&(prGlueInfo->rBowInfo.rKfifo), (PUINT_8)prEvent, u4EventSize);
    wake_up_interruptible(&(prGlueInfo->rBowInfo.outq));

    return;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Bluetooth-over-Wi-Fi state from glue layer
*
* \param[in]
*           prGlueInfo
*           rPeerAddr
* \return
*           ENUM_BOW_DEVICE_STATE
*/
/*----------------------------------------------------------------------------*/
ENUM_BOW_DEVICE_STATE
kalGetBowState (
    IN P_GLUE_INFO_T        prGlueInfo,
    IN UINT_8                     aucPeerAddress[6]
    )
{
    UINT_8 i;

    ASSERT(prGlueInfo);

#if CFG_BOW_TEST
    DBGLOG(BOW, EVENT, ("kalGetBowState.\n"));
#endif

    for(i = 0 ; i < CFG_BOW_PHYSICAL_LINK_NUM ; i++)
    {
        if(EQUAL_MAC_ADDR(prGlueInfo->rBowInfo.arPeerAddr, aucPeerAddress) == 0)
        {

#if CFG_BOW_TEST
    DBGLOG(BOW, EVENT, ("kalGetBowState, aucPeerAddress %x, %x:%x:%x:%x:%x:%x.\n", i,
        aucPeerAddress[0],
        aucPeerAddress[1],
        aucPeerAddress[2],
        aucPeerAddress[3],
        aucPeerAddress[4],
        aucPeerAddress[5]));

    DBGLOG(BOW, EVENT, ("kalGetBowState, prGlueInfo->rBowInfo.aeState %x, %x.\n", i, prGlueInfo->rBowInfo.aeState[i]));

#endif

            return prGlueInfo->rBowInfo.aeState[i];
        }
    }

    return BOW_DEVICE_STATE_DISCONNECTED;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to set Bluetooth-over-Wi-Fi state in glue layer
*
* \param[in]
*           prGlueInfo
*           eBowState
*           rPeerAddr
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
kalSetBowState (
    IN P_GLUE_INFO_T            prGlueInfo,
    IN ENUM_BOW_DEVICE_STATE    eBowState,
    IN UINT_8                                 aucPeerAddress[6]
    )
{
    UINT_8 i;

    ASSERT(prGlueInfo);

#if CFG_BOW_TEST
    DBGLOG(BOW, EVENT, ("kalSetBowState.\n"));

    DBGLOG(BOW, EVENT, ("kalSetBowState, prGlueInfo->rBowInfo.arPeerAddr, %x:%x:%x:%x:%x:%x.\n",
        prGlueInfo->rBowInfo.arPeerAddr[0],
        prGlueInfo->rBowInfo.arPeerAddr[1],
        prGlueInfo->rBowInfo.arPeerAddr[2],
        prGlueInfo->rBowInfo.arPeerAddr[3],
        prGlueInfo->rBowInfo.arPeerAddr[4],
        prGlueInfo->rBowInfo.arPeerAddr[5]));

    DBGLOG(BOW, EVENT, ("kalSetBowState, aucPeerAddress, %x:%x:%x:%x:%x:%x.\n",
        aucPeerAddress[0],
        aucPeerAddress[1],
        aucPeerAddress[2],
        aucPeerAddress[3],
        aucPeerAddress[4],
        aucPeerAddress[5]));
#endif

    for(i = 0 ; i < CFG_BOW_PHYSICAL_LINK_NUM ; i++)
    {
        if(EQUAL_MAC_ADDR(prGlueInfo->rBowInfo.arPeerAddr, aucPeerAddress) == 0)
        {
            prGlueInfo->rBowInfo.aeState[i] = eBowState;

#if CFG_BOW_TEST
    DBGLOG(BOW, EVENT, ("kalSetBowState, aucPeerAddress %x, %x:%x:%x:%x:%x:%x.\n", i,
        aucPeerAddress[0],
        aucPeerAddress[1],
        aucPeerAddress[2],
        aucPeerAddress[3],
        aucPeerAddress[4],
        aucPeerAddress[5]));

    DBGLOG(BOW, EVENT, ("kalSetBowState, prGlueInfo->rBowInfo.aeState %x, %x.\n", i, prGlueInfo->rBowInfo.aeState[i]));
#endif

            return TRUE;
        }
    }

    return FALSE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Bluetooth-over-Wi-Fi global state
*
* \param[in]
*           prGlueInfo
*
* \return
*           BOW_DEVICE_STATE_DISCONNECTED
*               in case there is no BoW connection or
*               BoW connection under initialization
*
*           BOW_DEVICE_STATE_STARTING
*               in case there is no BoW connection but
*               some BoW connection under initialization
*
*           BOW_DEVICE_STATE_CONNECTED
*               in case there is any BoW connection available
*/
/*----------------------------------------------------------------------------*/
ENUM_BOW_DEVICE_STATE
kalGetBowGlobalState (
    IN P_GLUE_INFO_T    prGlueInfo
    )
{
    UINT_32 i;

    ASSERT(prGlueInfo);


//Henry, can reduce this logic to indentify state change

    for(i = 0 ; i < CFG_BOW_PHYSICAL_LINK_NUM ; i++) {
        if(prGlueInfo->rBowInfo.aeState[i] == BOW_DEVICE_STATE_CONNECTED) {
            return BOW_DEVICE_STATE_CONNECTED;
        }
    }

    for(i = 0 ; i < CFG_BOW_PHYSICAL_LINK_NUM ; i++) {
        if(prGlueInfo->rBowInfo.aeState[i] == BOW_DEVICE_STATE_STARTING) {
            return BOW_DEVICE_STATE_STARTING;
        }
    }

    return BOW_DEVICE_STATE_DISCONNECTED;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Bluetooth-over-Wi-Fi operating frequency
*
* \param[in]
*           prGlueInfo
*
* \return
*           in unit of KHz
*/
/*----------------------------------------------------------------------------*/
UINT_32
kalGetBowFreqInKHz(
    IN P_GLUE_INFO_T            prGlueInfo
    )
{
    ASSERT(prGlueInfo);

    return prGlueInfo->rBowInfo.u4FreqInKHz;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Bluetooth-over-Wi-Fi role
*
* \param[in]
*           prGlueInfo
*
* \return
*           0: Responder
*           1: Initiator
*/
/*----------------------------------------------------------------------------*/
UINT_8
kalGetBowRole(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN PARAM_MAC_ADDRESS    rPeerAddr
    )
{
    UINT_32 i;

    ASSERT(prGlueInfo);

    for(i = 0 ; i < CFG_BOW_PHYSICAL_LINK_NUM ; i++) {
        if(EQUAL_MAC_ADDR(prGlueInfo->rBowInfo.arPeerAddr[i], rPeerAddr) == 0) {
            return prGlueInfo->rBowInfo.aucRole[i];
        }
    }

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to set Bluetooth-over-Wi-Fi role
*
* \param[in]
*           prGlueInfo
*           ucRole
*                   0: Responder
*                   1: Initiator
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID
kalSetBowRole(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN UINT_8               ucRole,
    IN PARAM_MAC_ADDRESS    rPeerAddr
    )
{
    UINT_32 i;

    ASSERT(prGlueInfo);
    ASSERT(ucRole <= 1);

    for(i = 0 ; i < CFG_BOW_PHYSICAL_LINK_NUM ; i++) {
        if(EQUAL_MAC_ADDR(prGlueInfo->rBowInfo.arPeerAddr[i], rPeerAddr) == 0) {
            prGlueInfo->rBowInfo.aucRole[i] = ucRole; //Henry, 0 : Responder, 1 : Initiator
        }
    }
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to get available Bluetooth-over-Wi-Fi physical link number
*
* \param[in]
*           prGlueInfo
* \return
*           UINT_32
*               how many physical links are aviailable
*/
/*----------------------------------------------------------------------------*/
UINT_8
kalGetBowAvailablePhysicalLinkCount(
    IN P_GLUE_INFO_T        prGlueInfo
    )
{
    UINT_8 i;
    UINT_8 ucLinkCount = 0;

    ASSERT(prGlueInfo);

    for(i = 0 ; i < CFG_BOW_PHYSICAL_LINK_NUM ; i++) {
        if(prGlueInfo->rBowInfo.aeState[i] == BOW_DEVICE_STATE_DISCONNECTED) {
            ucLinkCount++;
        }
    }

#if 0//CFG_BOW_TEST
    DBGLOG(BOW, EVENT, ("kalGetBowAvailablePhysicalLinkCount, ucLinkCount, %c.\n", ucLinkCount));
#endif

    return ucLinkCount;
}

#if CFG_BOW_SEPARATE_DATA_PATH

/* Net Device Hooks */
/*----------------------------------------------------------------------------*/
/*!
 * \brief A function for net_device open (ifup)
 *
 * \param[in] prDev      Pointer to struct net_device.
 *
 * \retval 0     The execution succeeds.
 * \retval < 0   The execution failed.
 */
/*----------------------------------------------------------------------------*/
static int
bowOpen(
    IN struct net_device *prDev
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_ADAPTER_T prAdapter = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    /* 2. carrier on & start TX queue */
    netif_carrier_on(prDev);
    netif_tx_start_all_queues(prDev);

    return 0; /* success */
}


/*----------------------------------------------------------------------------*/
/*!
 * \brief A function for net_device stop (ifdown)
 *
 * \param[in] prDev      Pointer to struct net_device.
 *
 * \retval 0     The execution succeeds.
 * \retval < 0   The execution failed.
 */
/*----------------------------------------------------------------------------*/
static int
bowStop(
    IN struct net_device *prDev
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_ADAPTER_T prAdapter = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    /* 1. stop TX queue */
    netif_tx_stop_all_queues(prDev);

    /* 2. turn of carrier */
    if(netif_carrier_ok(prDev)) {
        netif_carrier_off(prDev);
    }

    return 0;
};


/*----------------------------------------------------------------------------*/
/*!
 * \brief This function is TX entry point of NET DEVICE.
 *
 * \param[in] prSkb  Pointer of the sk_buff to be sent
 * \param[in] prDev  Pointer to struct net_device
 *
 * \retval NETDEV_TX_OK - on success.
 * \retval NETDEV_TX_BUSY - on failure, packet will be discarded by upper layer.
 */
/*----------------------------------------------------------------------------*/
static int
bowHardStartXmit(
    IN struct sk_buff *prSkb,
    IN struct net_device *prDev
    )
{
    P_GLUE_INFO_T prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));

    P_QUE_ENTRY_T prQueueEntry = NULL;
    P_QUE_T prTxQueue = NULL;
    UINT_16 u2QueueIdx = 0;
    UINT_8 ucDSAP, ucSSAP, ucControl;
    UINT_8 aucOUI[3];
    PUINT_8 aucLookAheadBuf = NULL;

#if CFG_BOW_TEST
    UINT_32 i;
#endif

    GLUE_SPIN_LOCK_DECLARATION();

    ASSERT(prSkb);
    ASSERT(prDev);
    ASSERT(prGlueInfo);

    aucLookAheadBuf = prSkb->data;

    ucDSAP = *(PUINT_8) &aucLookAheadBuf[ETH_LLC_OFFSET];
    ucSSAP = *(PUINT_8) &aucLookAheadBuf[ETH_LLC_OFFSET + 1];
    ucControl = *(PUINT_8) &aucLookAheadBuf[ETH_LLC_OFFSET + 2];
    aucOUI[0] = *(PUINT_8) &aucLookAheadBuf[ETH_SNAP_OFFSET];
    aucOUI[1] = *(PUINT_8) &aucLookAheadBuf[ETH_SNAP_OFFSET + 1];
    aucOUI[2] = *(PUINT_8) &aucLookAheadBuf[ETH_SNAP_OFFSET + 2];

    if (!(ucDSAP == ETH_LLC_DSAP_SNAP &&
            ucSSAP == ETH_LLC_SSAP_SNAP &&
            ucControl == ETH_LLC_CONTROL_UNNUMBERED_INFORMATION &&
            aucOUI[0] == ETH_SNAP_BT_SIG_OUI_0 &&
            aucOUI[1] == ETH_SNAP_BT_SIG_OUI_1 &&
            aucOUI[2] == ETH_SNAP_BT_SIG_OUI_2) || (prSkb->len > 1514))
    {

#if CFG_BOW_TEST
        DBGLOG(BOW, TRACE, ("Invalid BOW packet, skip tx\n"));
#endif

        dev_kfree_skb(prSkb);
        return NETDEV_TX_OK;
     }

    if (prGlueInfo->u4Flag & GLUE_FLAG_HALT) {
        DBGLOG(BOW, TRACE, ("GLUE_FLAG_HALT skip tx\n"));
        dev_kfree_skb(prSkb);
        return NETDEV_TX_OK;
    }

    prQueueEntry = (P_QUE_ENTRY_T) GLUE_GET_PKT_QUEUE_ENTRY(prSkb);
    prTxQueue = &prGlueInfo->rTxQueue;

#if CFG_BOW_TEST
    DBGLOG(BOW, TRACE, ("Tx sk_buff->len: %d\n", prSkb->len));
    DBGLOG(BOW, TRACE, ("Tx sk_buff->data_len: %d\n", prSkb->data_len));
    DBGLOG(BOW, TRACE, ("Tx sk_buff->data:\n"));

    for(i = 0; i < prSkb->len; i++)
    {
        DBGLOG(BOW, TRACE, ("%4x", prSkb->data[i]));

        if((i+1)%16 ==0)
        {
            DBGLOG(BOW, TRACE, ("\n"));
        }
    }

    DBGLOG(BOW, TRACE, ("\n");
#endif

#if CFG_BOW_TEST
//    g_u4CurrentSysTime = (OS_SYSTIME)kalGetTimeTick();

    g_u4CurrentSysTime = (OS_SYSTIME) jiffies_to_usecs(jiffies);

    i = g_u4CurrentSysTime - g_u4PrevSysTime;

    if ( (i >> 10) > 0)
    {
        i = 10;
    }
    else
    {
        i = i >> 7;
    }

    g_arBowRevPalPacketTime[i]++;

    g_u4PrevSysTime = g_u4CurrentSysTime;

#endif

    if (wlanProcessSecurityFrame(prGlueInfo->prAdapter, (P_NATIVE_PACKET) prSkb) == FALSE) {
    	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_TX_QUE);
    	QUEUE_INSERT_TAIL(prTxQueue, prQueueEntry);
        GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_TX_QUE);


    	GLUE_INC_REF_CNT(prGlueInfo->i4TxPendingFrameNum);
    	GLUE_INC_REF_CNT(prGlueInfo->ai4TxPendingFrameNumPerQueue[NETWORK_TYPE_BOW_INDEX][u2QueueIdx]);

    	if (prGlueInfo->ai4TxPendingFrameNumPerQueue[NETWORK_TYPE_BOW_INDEX][u2QueueIdx] >= CFG_TX_STOP_NETIF_PER_QUEUE_THRESHOLD) {
            netif_stop_subqueue(prDev, u2QueueIdx);
    	}
    }
    else {
        GLUE_INC_REF_CNT(prGlueInfo->i4TxPendingSecurityFrameNum);
    }

    kalSetEvent(prGlueInfo);

    /* For Linux, we'll always return OK FLAG, because we'll free this skb by ourself */
    return NETDEV_TX_OK;
}


// callbacks for netdevice
static const struct net_device_ops bow_netdev_ops = {
    .ndo_open               = bowOpen,
    .ndo_stop               = bowStop,
    .ndo_start_xmit         = bowHardStartXmit,
};

/*----------------------------------------------------------------------------*/
/*!
* \brief initialize net device for Bluetooth-over-Wi-Fi
*
* \param[in]
*           prGlueInfo
*           prDevName
*
* \return
*           TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
kalInitBowDevice(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN const char           *prDevName
    )
{
    P_ADAPTER_T prAdapter;
    P_GL_HIF_INFO_T prHif;
    PARAM_MAC_ADDRESS rMacAddr;

    ASSERT(prGlueInfo);
    ASSERT(prGlueInfo->rBowInfo.fgIsRegistered == TRUE);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    prHif = &prGlueInfo->rHifInfo;
    ASSERT(prHif);

    if(prGlueInfo->rBowInfo.fgIsNetRegistered == FALSE) {
        prGlueInfo->rBowInfo.prDevHandler = alloc_netdev_mq(sizeof(P_GLUE_INFO_T), prDevName, ether_setup, CFG_MAX_TXQ_NUM);

        if (!prGlueInfo->rBowInfo.prDevHandler) {
            return FALSE;
        }
        else {
            /* 1. setup netdev */
            /* 1.1 Point to shared glue structure */
            *((P_GLUE_INFO_T *) netdev_priv(prGlueInfo->rBowInfo.prDevHandler)) = prGlueInfo;

            /* 1.2 fill hardware address */
            COPY_MAC_ADDR(rMacAddr, prAdapter->rMyMacAddr);
            rMacAddr[0] |= 0x2; // change to local administrated address
            memcpy(prGlueInfo->rBowInfo.prDevHandler->dev_addr, rMacAddr, ETH_ALEN);
            memcpy(prGlueInfo->rBowInfo.prDevHandler->perm_addr, prGlueInfo->rBowInfo.prDevHandler->dev_addr, ETH_ALEN);

            /* 1.3 register callback functions */
            prGlueInfo->rBowInfo.prDevHandler->netdev_ops = &bow_netdev_ops;

#if (MTK_WCN_HIF_SDIO == 0)
            SET_NETDEV_DEV(prGlueInfo->rBowInfo.prDevHandler, &(prHif->func->dev));
#endif

            register_netdev(prGlueInfo->rBowInfo.prDevHandler);

            /* 2. net device initialize */
            netif_carrier_off(prGlueInfo->rBowInfo.prDevHandler);
            netif_tx_stop_all_queues(prGlueInfo->rBowInfo.prDevHandler);

            /* 3. finish */
            prGlueInfo->rBowInfo.fgIsNetRegistered = TRUE;
        }
    }

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief uninitialize net device for Bluetooth-over-Wi-Fi
*
* \param[in]
*           prGlueInfo
*
* \return
*           TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
kalUninitBowDevice(
    IN P_GLUE_INFO_T        prGlueInfo
    )
{
    ASSERT(prGlueInfo);
    //ASSERT(prGlueInfo->rBowInfo.fgIsRegistered == TRUE);

    if(prGlueInfo->rBowInfo.fgIsNetRegistered == TRUE) {

        prGlueInfo->rBowInfo.fgIsNetRegistered = FALSE;

        if(netif_carrier_ok(prGlueInfo->rBowInfo.prDevHandler)) {
            netif_carrier_off(prGlueInfo->rBowInfo.prDevHandler);
        }

        netif_tx_stop_all_queues(prGlueInfo->rBowInfo.prDevHandler);

        /* netdevice unregistration & free */
        unregister_netdev(prGlueInfo->rBowInfo.prDevHandler);
        free_netdev(prGlueInfo->rBowInfo.prDevHandler);
        prGlueInfo->rBowInfo.prDevHandler = NULL;

        return TRUE;

    }
    else {
        return FALSE;
    }
}

#endif // CFG_BOW_SEPARATE_DATA_PATH
#endif // CFG_ENABLE_BT_OVER_WIFI

