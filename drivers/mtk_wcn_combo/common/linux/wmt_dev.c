/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */


/*! \file
    \brief brief description

    Detailed descriptions here.

*/

/*******************************************************************************
* Copyright (c) 2009 MediaTek Inc.
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

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WMT-DEV]"


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include "osal_linux.h"
#include "osal_typedef.h"
#include "osal.h"
#include "wmt_dev.h"
#include "core_exp.h"


#define MTK_WMT_VERSION  "Combo WMT Driver - v1.0"
#define MTK_WMT_DATE     "2011/10/04"
#define WMT_DEV_MAJOR 190 // never used number
#define WMT_DEV_NUM 1

#define WMT_DEV_INIT_TO_MS (2 * 1000) // 2000 ms

#if CFG_WMT_DBG_SUPPORT
#define WMT_DBG_PROCNAME "driver/wmt_dbg"
#endif

#define WMT_DRIVER_NAME "mtk_stp_wmt"


/* Linux UCHAR device */
static int gWmtMajor = WMT_DEV_MAJOR;
static struct cdev gWmtCdev;
static atomic_t gWmtRefCnt = ATOMIC_INIT(0);
/* WMT driver information */
static UINT8 gLpbkBuf[1024] = {0};
static UINT32 gLpbkBufLog; // George LPBK debug
static int gWmtInitDone = 0;
static wait_queue_head_t gWmtInitWq;

#if CFG_WMT_DBG_SUPPORT
static struct proc_dir_entry *gWmtDbgEntry = NULL;
#endif


#if CFG_WMT_DBG_SUPPORT

static INT32 wmt_dev_dbg_read(CHAR *page, CHAR **start, LONG off, INT32 count, INT32 *eof, VOID *data)
{
    return wmt_dbg_proc_read(page,start,off,count,eof,data);
}


static INT32 wmt_dev_dbg_write(struct file *file, const CHAR *buffer, ULONG count, VOID *data){

    CHAR buf[256];
    CHAR *pBuf;
    ULONG len = count;

    WMT_INFO_FUNC("write parameter len = %d\n\r", (int)len);
    if (len >= osal_sizeof(buf)){
        WMT_ERR_FUNC("input handling fail!\n");
        len = osal_sizeof(buf) - 1;
        return -1;
    }

    if (copy_from_user(buf,buffer,len)/*copy_from_user(buf, buffer, len)*/){
        return -EFAULT;
    }
    buf[len] = '\0';
    WMT_INFO_FUNC("write parameter data = %s\n\r", buf);
    pBuf = buf;
    wmt_dbg_proc_write(pBuf);
    return len;
}

INT32 wmt_dev_dbg_setup(VOID)
{
    gWmtDbgEntry = create_proc_entry(WMT_DBG_PROCNAME, 0666, NULL);
    if (gWmtDbgEntry == NULL){
        WMT_ERR_FUNC("Unable to create /proc entry\n\r");
        return -1;
    }
    gWmtDbgEntry->read_proc = wmt_dev_dbg_read;
    gWmtDbgEntry->write_proc = wmt_dev_dbg_write;
    return 0;
}

INT32 wmt_dev_dbg_remove(VOID)
{
    if (NULL != gWmtDbgEntry)
    {
        remove_proc_entry(WMT_DBG_PROCNAME, NULL);
    }
    return 0;
}

#endif

INT32 wmt_dev_read_file (
    UCHAR *pName,
    const u8 **ppBufPtr,
    INT32 offset,
    INT32 padSzBuf
    )
{
    INT32 iRet = -1;
    struct file *fd;
    //ssize_t iRet;
    INT32 file_len;
    INT32 read_len;
    void *pBuf;

    //struct cred *cred = get_task_cred(current);
    const struct cred *cred = get_current_cred();

    if (!ppBufPtr ) {
        WMT_ERR_FUNC("invalid ppBufptr!\n");
        return -1;
    }
    *ppBufPtr = NULL;

    WMT_INFO_FUNC("open (%s) O_RDONLY, 0\n", pName);
    fd = filp_open(pName, O_RDONLY, 0);
    if (!fd || IS_ERR(fd) || !fd->f_op || !fd->f_op->read) {
        WMT_ERR_FUNC("failed to open or read!(0x%p, %d, %d)\n", fd, cred->fsuid, cred->fsgid);
        return -1;
    }

    file_len = fd->f_path.dentry->d_inode->i_size;
    pBuf = vmalloc((file_len + BCNT_PATCH_BUF_HEADROOM + 3) & ~0x3UL);
    if (!pBuf) {
        WMT_ERR_FUNC("failed to vmalloc(%d)\n", (INT32)((file_len + 3) & ~0x3UL));
        goto read_file_done;
    }

    do {
        if (fd->f_pos != offset) {
            if (fd->f_op->llseek) {
                if (fd->f_op->llseek(fd, offset, 0) != offset) {
                    WMT_ERR_FUNC("failed to seek!!\n");
                    goto read_file_done;
                }
            }
            else {
                fd->f_pos = offset;
            }
        }

        read_len = fd->f_op->read(fd, pBuf + padSzBuf, file_len, &fd->f_pos);
        if (read_len != file_len) {
            WMT_WARN_FUNC("read abnormal: read_len(%d), file_len(%d)\n", read_len, file_len);
        }
    } while (false);

    iRet = 0;
    *ppBufPtr = pBuf;

read_file_done:
    if (iRet) {
        if (pBuf) {
            vfree(pBuf);
        }
    }

    filp_close(fd, NULL);

    return (iRet) ? iRet : read_len;
}

// TODO: [ChangeFeature][George] refine this function name for general filesystem read operation, not patch only.
INT32 wmt_dev_patch_get (
    UCHAR *pPatchName,
    OSAL_FIRMWARE **ppPatch,
    INT32 padSzBuf
    )
{
    INT32 iRet = -1;
    uid_t orig_uid;
    gid_t orig_gid;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29))
    //struct cred *cred = get_task_cred(current);
    struct cred *cred = (struct cred *)get_current_cred();
#endif

    mm_segment_t orig_fs = get_fs();

    if (*ppPatch) {
        WMT_WARN_FUNC("f/w patch already exists \n");
        if ((*ppPatch)->data) {
            vfree((*ppPatch)->data);
        }
        kfree(*ppPatch);
        *ppPatch = NULL;
    }

    if (!osal_strlen(pPatchName)) {
        WMT_ERR_FUNC("empty f/w name\n");
        osal_assert((osal_strlen(pPatchName) > 0));
        return -1;
    }

    *ppPatch = (OSAL_FIRMWARE *)kzalloc(sizeof(OSAL_FIRMWARE), /*GFP_KERNEL*/GFP_ATOMIC);
    if (!(*ppPatch)) {
        WMT_ERR_FUNC("kzalloc(%d) fail\n", sizeof(OSAL_FIRMWARE));
        return -2;
    }

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29))
    orig_uid = cred->fsuid;
    orig_gid = cred->fsgid;
    cred->fsuid = cred->fsgid = 0;
#else
    orig_uid = current->fsuid;
    orig_gid = current->fsgid;
    current->fsuid = current->fsgid = 0;
#endif

    set_fs(get_ds());

    /* load patch file from fs */
    iRet = wmt_dev_read_file(pPatchName, &((*ppPatch)->data), 0, padSzBuf);
    set_fs(orig_fs);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29))
    cred->fsuid = orig_uid;
    cred->fsgid = orig_gid;
#else
    current->fsuid = orig_uid;
    current->fsgid = orig_gid;
#endif

    if (iRet > 0) {
        (*ppPatch)->size = iRet;
        WMT_DBG_FUNC("load (%s) to addr(0x%p) success\n", pPatchName, (*ppPatch)->data);
        return 0;
    }
    else {
        kfree((*ppPatch));
        *ppPatch = NULL;
        WMT_ERR_FUNC("load file (%s) fail, iRet(%d) \n", pPatchName, iRet);
        return -1;
    }
}

//INT32 wmt_dev_patch_put(osal_firmware **ppPatch)
INT32 wmt_dev_patch_put(OSAL_FIRMWARE **ppPatch)

{
    if (NULL != *ppPatch )
    {
        if ((*ppPatch)->data) {
            vfree((*ppPatch)->data);
            (*ppPatch)->data = NULL;
        }
        kfree(*ppPatch);
        *ppPatch = NULL;
    }
    return 0;

}

MTK_WCN_BOOL wmt_dev_is_file_exist(UCHAR *pFileName)
{
    struct file *fd = NULL;
    //ssize_t iRet;
    INT32 fileLen = -1;
    const struct cred *cred = get_current_cred();
    if (pFileName == NULL)
    {
        WMT_ERR_FUNC("invalid file name pointer(%p)\n", pFileName);
        return MTK_WCN_BOOL_FALSE;
    }
    if (osal_strlen(pFileName) < osal_strlen(defaultPatchName))
    {
        WMT_ERR_FUNC("invalid file name(%s)\n", pFileName);
        return MTK_WCN_BOOL_FALSE;
    }


    //struct cred *cred = get_task_cred(current);

    fd = filp_open(pFileName, O_RDONLY, 0);
    if (!fd || IS_ERR(fd) || !fd->f_op || !fd->f_op->read) {
        WMT_ERR_FUNC("failed to open or read(%s)!(0x%p, %d, %d)\n", pFileName, fd, cred->fsuid, cred->fsgid);
        return MTK_WCN_BOOL_FALSE;
    }
    fileLen = fd->f_path.dentry->d_inode->i_size;
    filp_close(fd, NULL);
    fd = NULL;
    if (fileLen <= 0)
    {
        WMT_ERR_FUNC("invalid file(%s), length(%d)\n", pFileName, fileLen);
        return MTK_WCN_BOOL_FALSE;
    }
    WMT_ERR_FUNC("valid file(%s), length(%d)\n", pFileName, fileLen);
    return true;

}


ssize_t
WMT_write (
    struct file *filp,
    const char __user *buf,
    size_t count,
    loff_t *f_pos
    )
{
    INT32 iRet = 0;
    UCHAR wrBuf[OSAL_NAME_MAX+1] = {0};
    INT32 copySize = (count < OSAL_NAME_MAX) ? count : OSAL_NAME_MAX;

    WMT_LOUD_FUNC("count:%d copySize:%d\n", count, copySize);

    if (copySize > 0) {
        if (copy_from_user(wrBuf, buf, copySize)) {
            iRet = -EFAULT;
            goto write_done;
        }
        iRet = copySize;
        wrBuf[OSAL_NAME_MAX] = '\0';

        if (!strncasecmp(wrBuf, "ok", OSAL_NAME_MAX)) {
            WMT_DBG_FUNC("resp str ok\n");
            //pWmtDevCtx->cmd_result = 0;
            wmt_lib_trigger_cmd_signal(0);
        }
        else {
            WMT_WARN_FUNC("warning resp str (%s)\n", wrBuf);
            //pWmtDevCtx->cmd_result = -1;
            wmt_lib_trigger_cmd_signal(-1);
        }
        //complete(&pWmtDevCtx->cmd_comp);

    }

write_done:
    return iRet;
}

ssize_t
WMT_read (
    struct file *filp,
    char __user *buf,
    size_t count,
    loff_t *f_pos
    )
{
    INT32 iRet = 0;
    PUCHAR pCmd = NULL;
    UINT32 cmdLen = 0;
    pCmd = wmt_lib_get_cmd();

    if (pCmd != NULL)
    {
        cmdLen = osal_strlen(pCmd) < OSAL_NAME_MAX ? osal_strlen(pCmd) : OSAL_NAME_MAX;
        WMT_DBG_FUNC("cmd str(%s)\n", pCmd);
        if (copy_to_user(buf, pCmd, cmdLen)) {
            iRet = -EFAULT;
        }
        else
        {
            iRet = cmdLen;
        }
    }
#if 0
    if (test_and_clear_bit(WMT_STAT_CMD, &pWmtDevCtx->state)) {
        iRet = osal_strlen(localBuf) < OSAL_NAME_MAX ? osal_strlen(localBuf) : OSAL_NAME_MAX;
        // we got something from STP driver
        WMT_DBG_FUNC("copy cmd to user by read:%s\n", localBuf);
        if (copy_to_user(buf, localBuf, iRet)) {
            iRet = -EFAULT;
            goto read_done;
        }
    }
#endif
    return iRet;
}

unsigned int WMT_poll(struct file *filp, poll_table *wait)
{
    UINT32 mask = 0;
    P_OSAL_EVENT pEvent = wmt_lib_get_cmd_event();

    poll_wait(filp, (wait_queue_head_t *)pEvent->pWaitQueue,  wait);
    /* empty let select sleep */
    if (MTK_WCN_BOOL_TRUE == wmt_lib_get_cmd_status())
    {
        mask |= POLLIN | POLLRDNORM;  /* readable */
    }
#if 0
    if (test_bit(WMT_STAT_CMD, &pWmtDevCtx->state)) {
        mask |= POLLIN | POLLRDNORM;  /* readable */
    }
#endif
    mask |= POLLOUT | POLLWRNORM; /* writable */
    return mask;
}

//INT32 WMT_ioctl(struct inode *inode, struct file *filp, UINT32 cmd, unsigned long arg)
long
WMT_unlocked_ioctl (
    struct file *filp,
    unsigned int cmd,
    unsigned long arg
    )
{
    INT32 iRet = 0;
    WMT_DBG_FUNC("cmd (%u), arg (0x%lx)\n", cmd, arg);

    switch(cmd) {
    case 4: /* patch location */
        {
#if 0
        WMT_DBG_FUNC("old patch file: %s \n", pWmtDevCtx->cPatchName);
        if (copy_from_user(pWmtDevCtx->cPatchName, (void *)arg, OSAL_NAME_MAX)) {
            iRet = -EFAULT;
            break;
        }
        pWmtDevCtx->cPatchName[OSAL_NAME_MAX] = '\0';
        WMT_DBG_FUNC("new patch file name: %s \n", pWmtDevCtx->cPatchName);
#endif
            UCHAR cPatchName[OSAL_NAME_MAX + 1];
            if (copy_from_user(cPatchName, (void *)arg, OSAL_NAME_MAX)) {
                iRet = -EFAULT;
                break;
            }
            cPatchName[OSAL_NAME_MAX] = '\0';
            wmt_lib_set_patch_name(cPatchName);
        }
        break;

    case 5: /* stp/hif/fm mode */

        /* set hif conf */
        do {
            P_OSAL_OP pOp;
            MTK_WCN_BOOL bRet;
            P_OSAL_SIGNAL pSignal = NULL;
            P_WMT_HIF_CONF pHif = NULL;

            iRet = wmt_lib_set_hif(arg);
            if (0 != iRet)
            {
                WMT_INFO_FUNC("wmt_lib_set_hif fail\n");
                break;
            }

            pOp = wmt_lib_get_free_op();
            if (!pOp) {
                WMT_INFO_FUNC("get_free_lxop fail\n");
                break;
            }
            pSignal = &pOp->signal;
            pOp->op.opId = WMT_OPID_HIF_CONF;

            pHif = wmt_lib_get_hif();

            osal_memcpy(&pOp->op.au4OpData[0], pHif, sizeof(WMT_HIF_CONF));
            pOp->op.u4InfoBit = WMT_OP_HIF_BIT;
            pSignal->timeoutValue = 0;

            bRet = wmt_lib_put_act_op(pOp);
            WMT_DBG_FUNC("WMT_OPID_HIF_CONF result(%d) \n", bRet);
            iRet = (MTK_WCN_BOOL_FALSE == bRet) ? -EFAULT : 0;
        } while (0);

        break;

    case 6: /* test turn on/off func */

        do {
            MTK_WCN_BOOL bRet = MTK_WCN_BOOL_FALSE;
            if (arg & 0x80000000)
            {
                bRet = mtk_wcn_wmt_func_on(arg & 0xF);
            }
            else
            {
                bRet = mtk_wcn_wmt_func_off(arg & 0xF);
            }
            iRet = (MTK_WCN_BOOL_FALSE == bRet) ? -EFAULT : 0;
         } while (0);

        break;

        case 7:
        /*switch Loopback function on/off
                  arg:     bit0 = 1:turn loopback function on
                  bit0 = 0:turn loopback function off
                */
        do{
            MTK_WCN_BOOL bRet = MTK_WCN_BOOL_FALSE;
            if (arg & 0x01)
            {
                bRet = mtk_wcn_wmt_func_on(WMTDRV_TYPE_LPBK);
            }
            else
            {
                bRet = mtk_wcn_wmt_func_off(WMTDRV_TYPE_LPBK);
            }
            iRet = (MTK_WCN_BOOL_FALSE == bRet) ? -EFAULT : 0;
          }while(0);


          break;


        case 8:
        do {
            P_OSAL_OP pOp;
            MTK_WCN_BOOL bRet;
            UINT32 u4Wait;
            //UINT8 lpbk_buf[1024] = {0};
            UINT32 effectiveLen = 0;
            P_OSAL_SIGNAL pSignal = NULL;

            if (copy_from_user(&effectiveLen, (void *)arg, sizeof(effectiveLen))) {
                iRet = -EFAULT;
                WMT_ERR_FUNC("copy_from_user failed at %d\n", __LINE__);
                break;
            }
            if (effectiveLen > sizeof(gLpbkBuf))
            {
                iRet = -EFAULT;
                WMT_ERR_FUNC("length is too long\n");
                break;
            }
            WMT_DBG_FUNC("len = %d\n", effectiveLen);

            pOp = wmt_lib_get_free_op();
            if (!pOp) {
                WMT_WARN_FUNC("get_free_lxop fail \n");
                iRet = -EFAULT;
                break;
            }
            u4Wait = 2000;
            if (copy_from_user(&gLpbkBuf[0], (void *)arg + sizeof(unsigned long), effectiveLen)) {
                WMT_ERR_FUNC("copy_from_user failed at %d\n", __LINE__);
                iRet = -EFAULT;
                break;
            }
            pSignal = &pOp->signal;
            pOp->op.opId = WMT_OPID_LPBK;
            pOp->op.au4OpData[0] = effectiveLen;    //packet length
            pOp->op.au4OpData[1] = (UINT32)&gLpbkBuf[0];        //packet buffer pointer
            memcpy(&gLpbkBufLog, &gLpbkBuf[((effectiveLen >=4) ? effectiveLen-4:0)], 4);
            pSignal->timeoutValue = MAX_EACH_WMT_CMD;
            WMT_DBG_FUNC("OPID(%d)type(%d)start\n",
                pOp->op.opId,
                pOp->op.au4OpData[0]);
            wmt_lib_disable_psm_monitor();
            bRet = wmt_lib_put_act_op(pOp);
            wmt_lib_enable_psm_monitor();
            if (MTK_WCN_BOOL_FALSE == bRet) {
                WMT_WARN_FUNC("OPID(%d) type(%d) buf tail(0x%08x) fail\n",
                pOp->op.opId,
                    pOp->op.au4OpData[0],
                    gLpbkBufLog);
                iRet = -1;
                break;
            }
            else {
                WMT_DBG_FUNC("OPID(%d)length(%d) ok\n",
                    pOp->op.opId, pOp->op.au4OpData[0]);
                iRet = pOp->op.au4OpData[0] ;
                if (copy_to_user((void *)arg + sizeof(ULONG) + sizeof(UCHAR[2048]), gLpbkBuf, iRet)) {
                    iRet = -EFAULT;
                    break;
                }
            }
        }while(0);

        break;
#if 0
        case 9:
        {
            #define LOG_BUF_SZ 300
            UCHAR buf[LOG_BUF_SZ];
            INT32 len = 0;
            INT32 remaining = 0;

            remaining = mtk_wcn_stp_btm_get_dmp(buf, &len);

            if (remaining == 0){
                WMT_DBG_FUNC("waiting dmp \n");
                wait_event_interruptible(dmp_wq, dmp_flag != 0);
                dmp_flag = 0;
                remaining = mtk_wcn_stp_btm_get_dmp(buf, &len);

                //WMT_INFO_FUNC("len = %d ###%s#\n", len, buf);
            } else {
                WMT_LOUD_FUNC("no waiting dmp \n");
            }

            if (unlikely((len+sizeof(INT32)) >= LOG_BUF_SZ)){
                WMT_ERR_FUNC("len is larger buffer\n");
                iRet = -EFAULT;
                goto fail_exit;
            }

            buf[sizeof(INT32)+len]='\0';

            if (copy_to_user((void *)arg, (UCHAR *)&len, sizeof(INT32))){
                iRet = -EFAULT;
                goto fail_exit;
            }

            if (copy_to_user((void *)arg + sizeof(INT32), buf, len)){
                iRet = -EFAULT;
                goto fail_exit;
            }
        }
        break;

        case 10:
        {
            WMT_INFO_FUNC("Enable combo trace32 dump\n");
            wmt_cdev_t32dmp_enable();
            WMT_INFO_FUNC("Enable STP debugging mode\n");
            mtk_wcn_stp_dbg_enable();
        }
        break;

        case 11:
        {
            WMT_INFO_FUNC("Disable combo trace32 dump\n");
            wmt_cdev_t32dmp_disable();
            WMT_INFO_FUNC("Disable STP debugging mode\n");
            mtk_wcn_stp_dbg_disable();
        }
        break;
#endif
    default:
        iRet = -EINVAL;
        WMT_WARN_FUNC("unknown cmd (%d)\n", cmd);
        break;
    }


    return iRet;
}

static int WMT_open(struct inode *inode, struct file *file)
{
    WMT_INFO_FUNC("major %d minor %d (pid %d)\n",
        imajor(inode),
        iminor(inode),
        current->pid
        );

    if (atomic_inc_return(&gWmtRefCnt) == 1) {
        WMT_INFO_FUNC("1st call \n");
    }

    return 0;
}

static int WMT_close(struct inode *inode, struct file *file)
{
    WMT_INFO_FUNC("major %d minor %d (pid %d)\n",
        imajor(inode),
        iminor(inode),
        current->pid
        );

    if (atomic_dec_return(&gWmtRefCnt) == 0) {
        WMT_INFO_FUNC("last call \n");
    }

    return 0;
}

ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
long (*unlocked_ioctl) (struct file *, unsigned int, unsigned long);

struct file_operations gWmtFops = {
    .open = WMT_open,
    .release = WMT_close,
    .read = WMT_read,
    .write = WMT_write,
//    .ioctl = WMT_ioctl,
    .unlocked_ioctl = WMT_unlocked_ioctl,
    .poll = WMT_poll,
};


static int WMT_init(void)
{
    dev_t devID = MKDEV(gWmtMajor, 0);
    INT32 cdevErr = -1;
    INT32 ret = -1;

    /* init start */
    gWmtInitDone = 0;
    init_waitqueue_head((wait_queue_head_t *)&gWmtInitWq);

    WMT_INFO_FUNC("WMT Version= %s DATE=%s\n" , MTK_WMT_VERSION, MTK_WMT_DATE);
    /* Prepare a UCHAR device */
    /*static allocate chrdev*/

    stp_drv_init();

    ret = register_chrdev_region(devID, WMT_DEV_NUM, WMT_DRIVER_NAME);
    if (ret) {
        WMT_ERR_FUNC("fail to register chrdev\n");
        return ret;
    }

    cdev_init(&gWmtCdev, &gWmtFops);
    gWmtCdev.owner = THIS_MODULE;

    cdevErr = cdev_add(&gWmtCdev, devID, WMT_DEV_NUM);
    if (cdevErr) {
        WMT_ERR_FUNC("cdev_add() fails (%d) \n", cdevErr);
        goto error;
    }
    WMT_INFO_FUNC("driver(major %d) installed \n", gWmtMajor);


#if 0
    pWmtDevCtx = wmt_drv_create();
    if (!pWmtDevCtx) {
        WMT_ERR_FUNC("wmt_drv_create() fails \n");
        goto error;
    }

    ret = wmt_drv_init(pWmtDevCtx);
    if (ret) {
        WMT_ERR_FUNC("wmt_drv_init() fails (%d) \n", ret);
        goto error;
    }

    WMT_INFO_FUNC("stp_btmcb_reg\n");
    wmt_cdev_btmcb_reg();

    ret = wmt_drv_start(pWmtDevCtx);
    if (ret) {
        WMT_ERR_FUNC("wmt_drv_start() fails (%d) \n", ret);
        goto error;
    }
#endif
    ret = wmt_lib_init();
    if (ret) {
        WMT_ERR_FUNC("wmt_lib_init() fails (%d) \n", ret);
        goto error;
    }
#if CFG_WMT_DBG_SUPPORT
    wmt_dev_dbg_setup();
#endif


    WMT_INFO_FUNC("success \n");
    return 0;

error:

    wmt_lib_deinit();
#if CFG_WMT_DBG_SUPPORT
    wmt_dev_dbg_remove();

#endif
    if (cdevErr == 0) {
        cdev_del(&gWmtCdev);
    }

    if (ret == 0) {
        unregister_chrdev_region(devID, WMT_DEV_NUM);
        gWmtMajor = -1;
    }

    stp_drv_exit();

    WMT_ERR_FUNC("fail \n");

    return -1;
}

static void WMT_exit (void)
{
    dev_t dev = MKDEV(gWmtMajor, 0);


    wmt_lib_deinit();

#if CFG_WMT_DBG_SUPPORT
    wmt_dev_dbg_remove();
#endif

    cdev_del(&gWmtCdev);
    unregister_chrdev_region(dev, WMT_DEV_NUM);
    gWmtMajor = -1;

    stp_drv_exit();

    WMT_INFO_FUNC("done\n");
}

module_init(WMT_init);
module_exit(WMT_exit);
MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("MediaTek Inc WCN");
MODULE_DESCRIPTION("MTK WCN combo driver for WMT character device");

module_param(gWmtMajor, uint, 0);


