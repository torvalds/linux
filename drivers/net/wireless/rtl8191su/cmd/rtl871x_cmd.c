/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/ 
#define _RTL871X_CMD_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <cmd_osdep.h>
#include <mlme_osdep.h>
#include <rtl871x_byteorder.h>

#ifdef PLATFORM_LINUX
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0))
#include <linux/smp_lock.h>
#endif
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/usb.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21))
#include <linux/usb_ch9.h>
#else
#include <linux/usb/ch9.h>
#endif
#include <linux/circ_buf.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
#include <asm/semaphore.h>
#else
#include <linux/semaphore.h>
#endif
#include <linux/rtnetlink.h>
#endif

#ifdef PLATFORM_WINDOWS
#include <wlan_bssdef.h>
#endif


/*
Caller and the cmd_thread can protect cmd_q by spin_lock.
No irqsave is necessary.
*/

sint _init_cmd_priv (struct cmd_priv *pcmdpriv)
{
	sint res = _SUCCESS;

_func_enter_;
	_init_sema(&(pcmdpriv->cmd_queue_sema), 0);
	//_init_sema(&(pcmdpriv->cmd_done_sema), 0);
	_init_sema(&(pcmdpriv->terminate_cmdthread_sema), 0);


	_init_queue(&(pcmdpriv->cmd_queue));

	//allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf

	pcmdpriv->cmd_seq = 1;

	pcmdpriv->cmd_allocated_buf = _malloc(MAX_CMDSZ + CMDBUFF_ALIGN_SZ);

	if (pcmdpriv->cmd_allocated_buf == NULL){
		res= _FAIL;
		goto exit;
		}
	pcmdpriv->cmd_buf = pcmdpriv->cmd_allocated_buf  +  CMDBUFF_ALIGN_SZ - ( (uint)(pcmdpriv->cmd_allocated_buf) & (CMDBUFF_ALIGN_SZ-1));

	pcmdpriv->rsp_allocated_buf = _malloc(MAX_RSPSZ + 4);

	if (pcmdpriv->rsp_allocated_buf == NULL){
		res= _FAIL;
		goto exit;
		}
	pcmdpriv->rsp_buf = pcmdpriv->rsp_allocated_buf  +  4 - ( (uint)(pcmdpriv->rsp_allocated_buf) & 3);

	pcmdpriv->cmd_issued_cnt = pcmdpriv->cmd_done_cnt = pcmdpriv->rsp_cnt = 0;

exit:
_func_exit_;
	return res;
}


sint _init_evt_priv(struct evt_priv *pevtpriv)
{
	sint res=_SUCCESS;

_func_enter_;
#ifdef CONFIG_EVENT_THREAD_MODE
	_init_sema(&(pevtpriv->evt_notify), 0);
	_init_sema(&(pevtpriv->terminate_evtthread_sema), 0);
#endif
#ifdef CONFIG_H2CLBK
	_init_sema(&(pevtpriv->lbkevt_done), 0);
	pevtpriv->lbkevt_limit = 0;
	pevtpriv->lbkevt_num = 0;
	pevtpriv->cmdevt_parm = NULL;
#endif

	//allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf
	pevtpriv->event_seq = 0;
	pevtpriv->evt_allocated_buf = _malloc(MAX_EVTSZ + 4);

	if (pevtpriv->evt_allocated_buf == NULL){
		res= _FAIL;
		goto exit;
		}
	pevtpriv->evt_buf = pevtpriv->evt_allocated_buf  +  4 - ((unsigned int)(pevtpriv->evt_allocated_buf) & 3);

	pevtpriv->evt_done_cnt = 0;
#ifdef CONFIG_SDIO_HCI
	pevtpriv->allocated_c2h_mem = _malloc(C2H_MEM_SZ +4);

	if (pevtpriv->allocated_c2h_mem == NULL){
		res= _FAIL;
		goto exit;
	}

	pevtpriv->c2h_mem = pevtpriv->allocated_c2h_mem +  4\
	- ( (u32)(pevtpriv->allocated_c2h_mem) & 3);
#ifdef PLATFORM_OS_XP
	pevtpriv->pc2h_mdl= IoAllocateMdl((u8 *)pevtpriv->c2h_mem, C2H_MEM_SZ , FALSE, FALSE, NULL);

	if(pevtpriv->pc2h_mdl == NULL){
		res= _FAIL;
		goto exit;
	}
	MmBuildMdlForNonPagedPool(pevtpriv->pc2h_mdl);
#endif
#endif

exit:

#ifdef CONFIG_RECV_BH

	_init_queue(&(pevtpriv->evt_queue));

#endif

_func_exit_;

	return res;
}

void _free_evt_priv (struct	evt_priv *pevtpriv)
{
_func_enter_;

	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("+_free_evt_priv \n"));

#ifdef CONFIG_EVENT_THREAD_MODE
	_free_sema(&(pevtpriv->evt_notify));
	_free_sema(&(pevtpriv->terminate_evtthread_sema));
#endif

	if (pevtpriv->evt_allocated_buf)
		_mfree(pevtpriv->evt_allocated_buf, MAX_EVTSZ + 4);

	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("-_free_evt_priv \n"));

_func_exit_;

}
void _free_cmd_priv (struct	cmd_priv *pcmdpriv)
{
_func_enter_;

	if(pcmdpriv){
		_spinlock_free(&(pcmdpriv->cmd_queue.lock));
		_free_sema(&(pcmdpriv->cmd_queue_sema));
		//_free_sema(&(pcmdpriv->cmd_done_sema));
		_free_sema(&(pcmdpriv->terminate_cmdthread_sema));

		if (pcmdpriv->cmd_allocated_buf)
			_mfree(pcmdpriv->cmd_allocated_buf, MAX_CMDSZ + CMDBUFF_ALIGN_SZ);

		if (pcmdpriv->rsp_allocated_buf)
			_mfree(pcmdpriv->rsp_allocated_buf, MAX_RSPSZ + 4);
	}
_func_exit_;
}

/*
Calling Context:

enqueue_cmd can only be called between kernel thread,
since only spin_lock is used.

ISR/Call-Back functions can't call this sub-function.

*/

sint _enqueue_cmd(_queue *queue, struct cmd_obj *obj)
{
_func_enter_;

	if (obj == NULL)
		goto exit;

    {
        unsigned long   flags;

        spin_lock_irqsave(&queue->lock, flags);
    	
    	list_insert_tail(&obj->list, &queue->queue);
        
        spin_unlock_irqrestore(&queue->lock, flags);
    }

exit:

_func_exit_;

	return _SUCCESS;
}

struct cmd_obj* _dequeue_cmd(_queue *queue)
{
	struct cmd_obj *obj;

_func_enter_;

    {
        unsigned long   flags;
        
        spin_lock_irqsave(&(queue->lock), flags);

    	if (is_list_empty(&(queue->queue)))
    		obj = NULL;
    	else
    	{
    		obj = LIST_CONTAINOR(get_next(&(queue->queue)), struct cmd_obj, list);
    		list_delete(&obj->list);
    	}

        spin_unlock_irqrestore(&(queue->lock), flags);
    }

_func_exit_;

	return obj;
}

u32 init_cmd_priv(struct cmd_priv *pcmdpriv)
{
	u32	res;
_func_enter_;
	res = _init_cmd_priv (pcmdpriv);
_func_exit_;
	return res;
}

u32 init_evt_priv(struct evt_priv *pevtpriv)
{
	int res;
_func_enter_;
	res = _init_evt_priv(pevtpriv);
_func_exit_;
	return res;
}

void free_evt_priv(struct evt_priv *pevtpriv)
{
_func_enter_;
	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("free_evt_priv\n"));
	_free_evt_priv(pevtpriv);
_func_exit_;
}

void free_cmd_priv(struct cmd_priv *pcmdpriv)
{
_func_enter_;
	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("free_cmd_priv\n"));
	_free_cmd_priv(pcmdpriv);
_func_exit_;
}

u32 enqueue_cmd(struct cmd_priv *pcmdpriv, struct cmd_obj *obj)
{
	int res;

_func_enter_;

	if (pcmdpriv->padapter->eeprompriv.bautoload_fail_flag == _TRUE) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
			 ("pcmdpriv->padapter->eeprompriv.bautoload_fail_flag=%x\n",
			  pcmdpriv->padapter->eeprompriv.bautoload_fail_flag));
		return _FAIL;
	}

	res = _enqueue_cmd(&pcmdpriv->cmd_queue, obj);

	_up_sema(&pcmdpriv->cmd_queue_sema);

_func_exit_;

	return res;
}

u32 enqueue_cmd_ex(struct cmd_priv *pcmdpriv, struct cmd_obj *obj)
{
	_irqL irqL;
	_queue *queue;

_func_enter_;

	if (obj == NULL)
		goto exit;

	if (pcmdpriv->padapter->eeprompriv.bautoload_fail_flag == _TRUE)
		return _FAIL;

	queue = &pcmdpriv->cmd_queue;

	_enter_critical(&queue->lock, &irqL);

	list_insert_tail(&obj->list, &queue->queue);

	_exit_critical(&queue->lock, &irqL);

	_up_sema(&pcmdpriv->cmd_queue_sema);

exit:

_func_exit_;

	return _SUCCESS;
}

struct cmd_obj* dequeue_cmd(_queue *queue)
{
	struct cmd_obj *pcmd;
_func_enter_;
	pcmd = _dequeue_cmd(queue);
_func_exit_;
	return pcmd;
}

u32 enqueue_evt(struct evt_priv *pevtpriv, struct evt_obj *obj)
{
	_irqL irqL;
	int res;
	_queue *queue = &pevtpriv->evt_queue;

_func_enter_;

	res = _SUCCESS;

	if (obj == NULL) {
		res = _FAIL;
		goto exit;
	}

	_enter_critical_ex(&queue->lock, &irqL);

	list_insert_tail(&obj->list, &queue->queue);

	_exit_critical_ex(&queue->lock, &irqL);

	//evt_notify_isr(pevtpriv);

exit:

_func_exit_;

	return res;
}

struct evt_obj* dequeue_evt(_queue *queue)
{
	_irqL irqL;
	struct	evt_obj	*pevtobj;

_func_enter_;

	_enter_critical_ex(&queue->lock, &irqL);

	if (is_list_empty(&(queue->queue)))
		pevtobj = NULL;
	else
	{
		pevtobj = LIST_CONTAINOR(get_next(&(queue->queue)), struct evt_obj, list);
		list_delete(&pevtobj->list);
	}

	_exit_critical_ex(&queue->lock, &irqL);

_func_exit_;

	return pevtobj;
}

void cmd_clr_isr(struct cmd_priv *pcmdpriv)
{
_func_enter_;
	pcmdpriv->cmd_done_cnt++;
	//_up_sema(&(pcmdpriv->cmd_done_sema));
_func_exit_;
}

void evt_notify_isr(struct evt_priv *pevtpriv)
{
_func_enter_;
	pevtpriv->evt_done_cnt++;
#ifdef CONFIG_EVENT_THREAD_MODE
	_up_sema(&(pevtpriv->evt_notify));
#endif
_func_exit_;
}

void free_cmd_obj(struct cmd_obj *pcmd)
{
_func_enter_;

	if((pcmd->cmdcode!=_JoinBss_CMD_) &&(pcmd->cmdcode!= _CreateBss_CMD_))
	{
		//free parmbuf in cmd_obj
		_mfree((unsigned char*)pcmd->parmbuf, pcmd->cmdsz);
	}

	if(pcmd->rsp!=NULL)
	{
		if(pcmd->rspsz!= 0)
		{
			//free rsp in cmd_obj
			_mfree((unsigned char*)pcmd->rsp, pcmd->rspsz);
		}
	}

	//free cmd_obj
	_mfree((unsigned char*)pcmd, sizeof(struct cmd_obj));

_func_exit_;
}

void free_evt_obj(struct evt_obj *pevtobj)
{
_func_enter_;

	if(pevtobj->parmbuf)
		_mfree((unsigned char*)pevtobj->parmbuf, pevtobj->evtsz);

	_mfree((unsigned char*)pevtobj, sizeof(struct evt_obj));

_func_exit_;
}

/*
u8 setstandby_cmd(unsigned char  *adapter)
*/
u8 setstandby_cmd(_adapter *padapter, uint action)
{
	struct cmd_obj		*ph2c;
	struct usb_suspend_parm	*psetusbsuspend;
	struct cmd_priv 	*pcmdpriv = &padapter->cmdpriv;

	u8 ret = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		ret = _FAIL;
		goto exit;
	}

	psetusbsuspend = (struct usb_suspend_parm*)_malloc(sizeof(struct usb_suspend_parm));
	if (psetusbsuspend == NULL) {
		_mfree((u8*)ph2c, sizeof(struct cmd_obj));
		ret = _FAIL;
		goto exit;
	}

	psetusbsuspend->action = action;

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetusbsuspend, GEN_CMD_CODE(_SetUsbSuspend));

	enqueue_cmd(pcmdpriv, ph2c);

exit:

_func_exit_;

	return ret;
}

/*
sitesurvey_cmd(~)
	### NOTE:#### (!!!!)
	MUST TAKE CARE THAT BEFORE CALLING THIS FUNC, YOU SHOULD HAVE LOCKED pmlmepriv->lock
*/
u8 sitesurvey_cmd(_adapter *padapter, NDIS_802_11_SSID *pssid)
{
	struct cmd_obj		*ph2c;
	struct sitesurvey_parm	*psurveyPara;
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL)
		return _FAIL;

	_memset(ph2c, 0, sizeof(struct cmd_obj));

	psurveyPara = (struct sitesurvey_parm*)_malloc(sizeof(struct sitesurvey_parm));
	if (psurveyPara == NULL) {
		_mfree((unsigned char*) ph2c, sizeof(struct cmd_obj));
		return _FAIL;
	}

	_memset(psurveyPara, 0, sizeof(struct sitesurvey_parm));

	init_h2fwcmd_w_parm_no_rsp(ph2c, psurveyPara, GEN_CMD_CODE(_SiteSurvey));

	psurveyPara->bsslimit = cpu_to_le32(48);
	psurveyPara->passive_mode = cpu_to_le32(pmlmepriv->passive_mode);
	psurveyPara->ss_ssidlen= cpu_to_le32(0);// pssid->SsidLength;
	_memset(psurveyPara->ss_ssid, 0, IW_ESSID_MAX_SIZE + 1);
	if ((pssid != NULL) && (pssid->SsidLength)) {
		_memcpy(psurveyPara->ss_ssid, pssid->Ssid, pssid->SsidLength);
		psurveyPara->ss_ssidlen = cpu_to_le32(pssid->SsidLength);
		// Commented by Kurt 20120323
		// In Android 4.0 system, it would set passive scan cmd before set scan
		// We have to change it to active scan to send probe request.
		psurveyPara->passive_mode = 1;	// 1: active
	}

	set_fwstate(pmlmepriv, _FW_UNDER_SURVEY);

	enqueue_cmd(pcmdpriv, ph2c);

	_set_timer(&pmlmepriv->scan_to_timer, SCANNING_TIMEOUT);
	padapter->ledpriv.LedControlHandler(padapter, LED_CTL_SITE_SURVEY);
	padapter->blnEnableRxFF0Filter = 0;

_func_exit_;

	return _SUCCESS;
}

u8 setdatarate_cmd(_adapter *padapter, u8 *rateset)
{
	struct cmd_obj		*ph2c;
	struct setdatarate_parm	*pbsetdataratepara;
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pbsetdataratepara = (struct setdatarate_parm*)_malloc(sizeof(struct setdatarate_parm));
	if (pbsetdataratepara == NULL) {
		_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pbsetdataratepara, GEN_CMD_CODE(_SetDataRate));
#ifdef MP_FIRMWARE_OFFLOAD
	pbsetdataratepara->curr_rateidx = *(u32*)rateset;
//	_memcpy(pbsetdataratepara, rateset, sizeof(u32));
#else
	pbsetdataratepara->mac_id = 5;
	_memcpy(pbsetdataratepara->datarates, rateset, NumRates);
#endif
	enqueue_cmd(pcmdpriv, ph2c);
exit:

_func_exit_;

	return res;
}

u8 set_chplan_cmd(_adapter *padapter, int chplan)
{
	struct cmd_obj		*ph2c;
	struct SetChannelPlan_param *psetchplanpara;
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

_func_enter_;

	//check input parameter
	if(!rtw_is_channel_plan_valid(chplan)) {
		res = _FAIL;
		goto exit;
	}
	
	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	psetchplanpara= (struct SetChannelPlan_param*)_malloc(sizeof(struct SetChannelPlan_param));
	if (psetchplanpara== NULL) {
		_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetchplanpara, GEN_CMD_CODE(_SetChannelPlan));
	
#ifdef MP_FIRMWARE_OFFLOAD

#else
	psetchplanpara->ChannelPlan= chplan;
#endif
	enqueue_cmd(pcmdpriv, ph2c);

exit:

_func_exit_;

	return res;
}

u8 setbasicrate_cmd(_adapter *padapter, u8 *rateset)
{
	struct cmd_obj*			ph2c;
	struct setbasicrate_parm*	pssetbasicratepara;
	struct cmd_priv*		pcmdpriv=&padapter->cmdpriv;
	u8	res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res= _FAIL;
		goto exit;
	}
	pssetbasicratepara = (struct setbasicrate_parm*)_malloc(sizeof(struct setbasicrate_parm));

	if (pssetbasicratepara == NULL) {
		_mfree((u8*) ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pssetbasicratepara, _SetBasicRate_CMD_);

	_memcpy(pssetbasicratepara->basicrates, rateset, NumRates);

	enqueue_cmd(pcmdpriv, ph2c);
exit:

_func_exit_;

	return res;
}


/*
unsigned char setphy_cmd(unsigned char  *adapter)

1.  be called only after update_registrypriv_dev_network( ~) or mp testing program
2.  for AdHoc/Ap mode or mp mode?

*/
u8 setphy_cmd(_adapter *padapter, u8 modem, u8 ch)
{
	struct cmd_obj		*ph2c;
	struct setphy_parm	*psetphypara;
	struct cmd_priv 	*pcmdpriv = &padapter->cmdpriv;
//	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
//	struct registry_priv	*pregistry_priv = &padapter->registrypriv;
//	NDIS_WLAN_BSSID_EX	*dev_network = &padapter->registrypriv.dev_network;
	u8 res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if(ph2c==NULL){
		res= _FAIL;
		goto exit;
		}
	psetphypara = (struct setphy_parm*)_malloc(sizeof(struct setphy_parm));

	if(psetphypara==NULL){
		_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		res= _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetphypara, _SetPhy_CMD_);

	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("CH=%d, modem=%d", ch, modem));

	psetphypara->modem = modem;
	psetphypara->rfchannel = ch;

	enqueue_cmd(pcmdpriv, ph2c);
exit:
_func_exit_;
	return res;
}

//power tracking mechanism setting
u8 setptm_cmd(_adapter*padapter, u8 type)
{
	struct cmd_obj*			ph2c;
	struct writePTM_parm*		pwriteptmparm;
	struct cmd_priv 			*pcmdpriv=&padapter->cmdpriv;
	u8	res=_SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if(ph2c==NULL){
		res= _FAIL;
		goto exit;
		}
	pwriteptmparm = (struct writePTM_parm*)_malloc(sizeof(struct writePTM_parm));

	if(pwriteptmparm==NULL){
		_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		res= _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pwriteptmparm, GEN_CMD_CODE(_SetPT));

	pwriteptmparm->type= type;

	enqueue_cmd(pcmdpriv, ph2c);
exit:

_func_exit_;

	return res;

}

u8 setfwdig_cmd(_adapter*padapter, u8 type)
{

	struct cmd_obj*			ph2c;
	struct writePTM_parm*		pwriteptmparm;
	struct cmd_priv 			*pcmdpriv=&padapter->cmdpriv;
	u8	res=_SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if(ph2c==NULL){
		res= _FAIL;
		goto exit;
		}
	pwriteptmparm = (struct writePTM_parm*)_malloc(sizeof(struct setdig_parm));

	if(pwriteptmparm==NULL){
		_mfree((u8 *) ph2c, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pwriteptmparm, GEN_CMD_CODE(_SetDIG));

	pwriteptmparm->type= type;

	enqueue_cmd(pcmdpriv, ph2c);

exit:

_func_exit_;

	return res;
}

u8 setfwra_cmd(_adapter*padapter, u8 type)
{

	struct cmd_obj*			ph2c;
	struct writePTM_parm*		pwriteptmparm;
	struct cmd_priv 			*pcmdpriv=&padapter->cmdpriv;
	u8	res=_SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if(ph2c==NULL){
		res= _FAIL;
		goto exit;
		}
	pwriteptmparm = (struct writePTM_parm*)_malloc(sizeof(struct setra_parm));

	if(pwriteptmparm==NULL){
		_mfree((u8 *) ph2c, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pwriteptmparm, GEN_CMD_CODE(_SetRA));

	pwriteptmparm->type= type;

	enqueue_cmd(pcmdpriv, ph2c);

exit:

_func_exit_;

	return res;
}

u8 setbbreg_cmd(_adapter*padapter, u8 offset, u8 val)
{
	struct cmd_obj*			ph2c;
	struct writeBB_parm*		pwritebbparm;
	struct cmd_priv 			*pcmdpriv=&padapter->cmdpriv;
	u8	res=_SUCCESS;
_func_enter_;
	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if(ph2c==NULL){
		res= _FAIL;
		goto exit;
		}
	pwritebbparm = (struct writeBB_parm*)_malloc(sizeof(struct writeBB_parm));

	if(pwritebbparm==NULL){
		_mfree((u8 *) ph2c, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pwritebbparm, GEN_CMD_CODE(_SetBBReg));

	pwritebbparm->offset = offset;
	pwritebbparm->value = val;

	enqueue_cmd(pcmdpriv, ph2c);
exit:
_func_exit_;
	return res;
}

u8 getbbreg_cmd(_adapter  *padapter, u8 offset, u8 *pval)
{
	struct cmd_obj*			ph2c;
	struct readBB_parm*		prdbbparm;
	struct cmd_priv 			*pcmdpriv=&padapter->cmdpriv;
	u8	res=_SUCCESS;

_func_enter_;
	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if(ph2c==NULL){
		res=_FAIL;
		goto exit;
		}
	prdbbparm = (struct readBB_parm*)_malloc(sizeof(struct readBB_parm));

	if(prdbbparm ==NULL){
		_mfree((unsigned char *) ph2c, sizeof(struct	cmd_obj));
		return _FAIL;
	}

	_init_listhead(&ph2c->list);
	ph2c->cmdcode =GEN_CMD_CODE(_GetBBReg);
	ph2c->parmbuf = (unsigned char *)prdbbparm;
	ph2c->cmdsz =  sizeof(struct readBB_parm);
	ph2c->rsp = pval;
	ph2c->rspsz = sizeof(struct readBB_rsp);

	prdbbparm ->offset = offset;

	enqueue_cmd(pcmdpriv, ph2c);
exit:
_func_exit_;
	return res;
}

u8 setrfreg_cmd(_adapter  *padapter, u8 offset, u32 val)
{
	struct cmd_obj*			ph2c;
	struct writeRF_parm*		pwriterfparm;
	struct cmd_priv 		*pcmdpriv=&padapter->cmdpriv;
	u8	res=_SUCCESS;
_func_enter_;
	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if(ph2c==NULL){
		res= _FAIL;
		goto exit;
	}
	pwriterfparm = (struct writeRF_parm*)_malloc(sizeof(struct writeRF_parm));

	if(pwriterfparm==NULL){
		_mfree((u8 *) ph2c, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pwriterfparm, GEN_CMD_CODE(_SetRFReg));

	pwriterfparm->offset = offset;
	pwriterfparm->value = val;

	enqueue_cmd(pcmdpriv, ph2c);
exit:
_func_exit_;
	return res;
}

u8 getrfreg_cmd(_adapter *padapter, u8 offset, u8 *pval)
{
	struct cmd_obj		*ph2c;
	struct readRF_parm	*prdrfparm;
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	prdrfparm = (struct readRF_parm*)_malloc(sizeof(struct readRF_parm));
	if (prdrfparm == NULL) {
		_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		res= _FAIL;
		goto exit;
	}

	_init_listhead(&ph2c->list);
	ph2c->cmdcode =GEN_CMD_CODE(_GetRFReg);
	ph2c->parmbuf = (unsigned char *)prdrfparm;
	ph2c->cmdsz =  sizeof(struct readRF_parm);
	ph2c->rsp = pval;
	ph2c->rspsz = sizeof(struct readRF_rsp);

	prdrfparm ->offset = offset;

	enqueue_cmd(pcmdpriv, ph2c);

exit:

_func_exit_;

	return res;
}

void getbbrfreg_cmdrsp_callback(_adapter *padapter, struct cmd_obj *pcmd)
{
_func_enter_;

	//free_cmd_obj(pcmd);
	_mfree((unsigned char*) pcmd->parmbuf, pcmd->cmdsz);
	_mfree((unsigned char*) pcmd, sizeof(struct cmd_obj));

#ifdef CONFIG_MP_INCLUDED
	padapter->mppriv.workparam.bcompleted= _TRUE;
#endif

_func_exit_;
}

void readtssi_cmdrsp_callback(_adapter *padapter,  struct cmd_obj *pcmd)
{
 _func_enter_;

	_mfree((unsigned char*) pcmd->parmbuf, pcmd->cmdsz);
	_mfree((unsigned char*) pcmd, sizeof(struct cmd_obj));

	padapter->mppriv.workparam.bcompleted = _TRUE;

_func_exit_;
}

u8 createbss_cmd(_adapter *padapter)
{
	struct cmd_obj		*pcmd;
	struct cmd_priv		*pcmdpriv=&padapter->cmdpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	WLAN_BSSID_EX		*pdev_network = &padapter->registrypriv.dev_network;
	u8 res = _SUCCESS;

_func_enter_;

	padapter->ledpriv.LedControlHandler(padapter, LED_CTL_START_TO_LINK);

	if (pmlmepriv->assoc_ssid.SsidLength == 0) {
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,(" createbss for Any SSid:%s\n",pmlmepriv->assoc_ssid.Ssid));
	} else {
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,(" createbss for SSid:%s\n", pmlmepriv->assoc_ssid.Ssid));
	}

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		res = _FAIL;
		goto exit;
	}

	_init_listhead(&pcmd->list);
	pcmd->cmdcode = _CreateBss_CMD_;
	pcmd->parmbuf = (unsigned char *)pdev_network;
	pcmd->cmdsz = get_NDIS_WLAN_BSSID_EX_sz((NDIS_WLAN_BSSID_EX*)pdev_network);
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	//notes: translate IELength & Length after assign the Length to cmdsz;
	pdev_network->Length = cpu_to_le32(pcmd->cmdsz);
	pdev_network->IELength = cpu_to_le32(pdev_network->IELength);
	pdev_network->Ssid.SsidLength = cpu_to_le32(pdev_network->Ssid.SsidLength);

	enqueue_cmd(pcmdpriv, pcmd);

exit:

_func_exit_;

	return res;
}

u8 createbss_cmd_ex(_adapter *padapter, unsigned char *pbss, unsigned int sz)
{
	struct cmd_obj	*pcmd;
	struct cmd_priv	*pcmdpriv=&padapter->cmdpriv;
	u8 res = _SUCCESS;

_func_enter_;

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		res = _FAIL;
		goto exit;
	}

	_init_listhead(&pcmd->list);
	pcmd->cmdcode = GEN_CMD_CODE(_CreateBss);
	pcmd->parmbuf = pbss;
	pcmd->cmdsz = sz;
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	enqueue_cmd(pcmdpriv, pcmd);

exit:

_func_exit_;

	return res;
}

u8 joinbss_cmd(_adapter  *padapter, struct wlan_network* pnetwork)
{
	u8 *auth, res = _SUCCESS;
	uint t_len = 0;
	NDIS_WLAN_BSSID_EX	*psecnetwork;
	struct cmd_obj		*pcmd;
	struct cmd_priv 	*pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv   	*pqospriv = &pmlmepriv->qospriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	NDIS_802_11_NETWORK_INFRASTRUCTURE ndis_network_mode = pnetwork->network.InfrastructureMode;

_func_enter_;

	padapter->ledpriv.LedControlHandler(padapter, LED_CTL_START_TO_LINK);

	if (pmlmepriv->assoc_ssid.SsidLength == 0) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("+Join cmd: Any SSid\n"));
	} else {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_notice_, ("+Join cmd: SSid=[%s]\n", pmlmepriv->assoc_ssid.Ssid));
	}

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("joinbss_cmd: memory allocate for cmd_obj fail!!!\n"));
		goto exit;
	}

	t_len = sizeof (ULONG) + sizeof (NDIS_802_11_MAC_ADDRESS) + 2 +
			sizeof (NDIS_802_11_SSID) + sizeof (ULONG) +
			sizeof (NDIS_802_11_RSSI) + sizeof (NDIS_802_11_NETWORK_TYPE) +
			sizeof (NDIS_802_11_CONFIGURATION) +
			sizeof (NDIS_802_11_NETWORK_INFRASTRUCTURE) +
			sizeof (NDIS_802_11_RATES_EX)+ sizeof(ULONG) + MAX_IE_SZ;

	//for hidden ap to set fw_state here
	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) != _TRUE)
	{
		switch(ndis_network_mode)
		{
			case Ndis802_11IBSS:
				pmlmepriv->fw_state |=WIFI_ADHOC_STATE;
				break;

			case Ndis802_11Infrastructure:
				pmlmepriv->fw_state |= WIFI_STATION_STATE;
				break;

			case Ndis802_11APMode:
			case Ndis802_11AutoUnknown:
			case Ndis802_11InfrastructureMax:
				break;

		}
	}

	psecnetwork = (NDIS_WLAN_BSSID_EX *)&psecuritypriv->sec_bss;
	if (psecnetwork == NULL)
	{
		if (pcmd != NULL)
			_mfree((unsigned char *)pcmd, sizeof(struct cmd_obj));

		res = _FAIL;

		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("joinbss_cmd :psecnetwork==NULL!!!\n"));

		goto exit;
	}

	_memset(psecnetwork, 0, t_len);

	_memcpy(psecnetwork, &pnetwork->network, t_len);

	auth = &psecuritypriv->authenticator_ie[0];
	psecuritypriv->authenticator_ie[0] = (unsigned char)psecnetwork->IELength;

	if((psecnetwork->IELength-12) < (256-1)) {
		_memcpy(&psecuritypriv->authenticator_ie[1], &psecnetwork->IEs[12], psecnetwork->IELength-12);
	} else {
		_memcpy(&psecuritypriv->authenticator_ie[1], &psecnetwork->IEs[12], (256-1));
	}

	psecnetwork->IELength = 0;
        // Added by Albert 2009/02/18
        // If the the driver wants to use the bssid to create the connection.
        // If not,  we have to copy the connecting AP's MAC address to it so that
        // the driver just has the bssid information for PMKIDList searching.

        if (pmlmepriv->assoc_by_bssid == _FALSE )
        {
            _memcpy( &pmlmepriv->assoc_bssid[ 0 ], &pnetwork->network.MacAddress[ 0 ], ETH_ALEN );
        }

	psecnetwork->IELength = restruct_sec_ie(padapter, &pnetwork->network.IEs[0], &psecnetwork->IEs[0], pnetwork->network.IELength);


	pqospriv->qos_option = 0;

	if (pregistrypriv->wmm_enable)
	{
		u32 tmp_len;

		tmp_len = restruct_wmm_ie(padapter, &pnetwork->network.IEs[0], &psecnetwork->IEs[0], pnetwork->network.IELength, psecnetwork->IELength);

		if (psecnetwork->IELength != tmp_len)
		{
			psecnetwork->IELength = tmp_len;
			pqospriv->qos_option = 1; //There is WMM IE in this corresp. beacon
		}
		else
		{
			pqospriv->qos_option = 0;//There is no WMM IE in this corresp. beacon
		}
	}

#ifdef CONFIG_80211N_HT

	if (pregistrypriv->ht_enable)
	{
		//	Added by Albert 2010/06/23
		//	For the WEP mode, we will use the bg mode to do the connection to avoid some IOT issue.
		//	Especially for Realtek 8192u SoftAP.
		if (( padapter->securitypriv.dot11PrivacyAlgrthm != _WEP40_ ) &&
			( padapter->securitypriv.dot11PrivacyAlgrthm != _WEP104_ ))
		{
			//restructure_ht_ie
			restructure_ht_ie(padapter, &pnetwork->network.IEs[0], &psecnetwork->IEs[0],
								pnetwork->network.IELength, &psecnetwork->IELength);
		}
	}

#endif

	psecuritypriv->supplicant_ie[0] = (u8)psecnetwork->IELength;

	if(psecnetwork->IELength < (256-1))
	{
	_memcpy(&psecuritypriv->supplicant_ie[1], &psecnetwork->IEs[0], psecnetwork->IELength);
	}
	else
	{
		_memcpy(&psecuritypriv->supplicant_ie[1], &psecnetwork->IEs[0], (256-1));
	}

	pcmd->cmdsz = get_NDIS_WLAN_BSSID_EX_sz(psecnetwork);//get cmdsz before endian conversion

	//wlan_network endian conversion
	psecnetwork->Length = cpu_to_le32(psecnetwork->Length);
	psecnetwork->Ssid.SsidLength= cpu_to_le32(psecnetwork->Ssid.SsidLength);
	psecnetwork->Privacy = cpu_to_le32(psecnetwork->Privacy);
	psecnetwork->Rssi = cpu_to_le32(psecnetwork->Rssi);
	psecnetwork->NetworkTypeInUse = cpu_to_le32(psecnetwork->NetworkTypeInUse);
	psecnetwork->Configuration.ATIMWindow = cpu_to_le32(psecnetwork->Configuration.ATIMWindow);
	psecnetwork->Configuration.BeaconPeriod = cpu_to_le32(psecnetwork->Configuration.BeaconPeriod);
	psecnetwork->Configuration.DSConfig = cpu_to_le32(psecnetwork->Configuration.DSConfig);
	psecnetwork->Configuration.FHConfig.DwellTime=cpu_to_le32(psecnetwork->Configuration.FHConfig.DwellTime);
	psecnetwork->Configuration.FHConfig.HopPattern=cpu_to_le32(psecnetwork->Configuration.FHConfig.HopPattern);
	psecnetwork->Configuration.FHConfig.HopSet=cpu_to_le32(psecnetwork->Configuration.FHConfig.HopSet);
	psecnetwork->Configuration.FHConfig.Length=cpu_to_le32(psecnetwork->Configuration.FHConfig.Length);
	psecnetwork->Configuration.Length = cpu_to_le32(psecnetwork->Configuration.Length);
	psecnetwork->InfrastructureMode = cpu_to_le32(psecnetwork->InfrastructureMode);
	psecnetwork->IELength = cpu_to_le32(psecnetwork->IELength);

	_init_listhead(&pcmd->list);
	pcmd->cmdcode = _JoinBss_CMD_;
	pcmd->parmbuf = (unsigned char *)psecnetwork;
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	enqueue_cmd(pcmdpriv, pcmd);

exit:

_func_exit_;

	return res;
}

u8 disassoc_cmd(_adapter*padapter) // for sta_mode
{
	struct cmd_obj		*pdisconnect_cmd;
	struct disconnect_parm	*pdisconnect;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;

	u8 res = _SUCCESS;

_func_enter_;

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_notice_, ("+disassoc_cmd\n"));

	//if ((check_fwstate(pmlmepriv, _FW_LINKED)) == _TRUE) {

		pdisconnect_cmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
		if (pdisconnect_cmd == NULL) {
			res = _FAIL;
			goto exit;
		}

		pdisconnect = (struct disconnect_parm*)_malloc(sizeof(struct disconnect_parm));
		if (pdisconnect == NULL) {
			_mfree((u8 *)pdisconnect_cmd, sizeof(struct cmd_obj));
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(pdisconnect_cmd, pdisconnect, _DisConnect_CMD_);
		enqueue_cmd(pcmdpriv, pdisconnect_cmd);
	//}

exit:

_func_exit_;

	return res;
}

u8 setopmode_cmd(_adapter *padapter, NDIS_802_11_NETWORK_INFRASTRUCTURE networktype)
{
	struct cmd_obj		*ph2c;
	struct setopmode_parm	*psetop;
	struct cmd_priv		*pcmdpriv= &padapter->cmdpriv;
	u8 res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FALSE;
		goto exit;
	}
	psetop = (struct setopmode_parm*)_malloc(sizeof(struct setopmode_parm));

	if (psetop == NULL) {
		_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		res = _FALSE;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetop, _SetOpMode_CMD_);
	psetop->mode = (u8)networktype;

	enqueue_cmd(pcmdpriv, ph2c);

exit:

_func_exit_;

	return res;
}

u8 setstakey_cmd(_adapter *padapter, u8 *psta, u8 unicast_key)
{
	struct cmd_obj		*ph2c;
	struct set_stakey_parm	*psetstakey_para;
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;
	struct set_stakey_rsp	*psetstakey_rsp = NULL;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	struct sta_info		*sta = (struct sta_info*)psta;
	u8 res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	psetstakey_para = (struct set_stakey_parm*)_malloc(sizeof(struct set_stakey_parm));
	if (psetstakey_para == NULL) {
		_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	psetstakey_rsp = (struct set_stakey_rsp*)_malloc(sizeof(struct set_stakey_rsp));
	if (psetstakey_rsp == NULL) {
		_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		_mfree((u8 *) psetstakey_para, sizeof(struct set_stakey_parm));
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SetStaKey_CMD_);
	ph2c->rsp = (u8 *) psetstakey_rsp;
	ph2c->rspsz = sizeof(struct set_stakey_rsp);

	_memcpy(psetstakey_para->addr, sta->hwaddr, ETH_ALEN);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
		psetstakey_para->algorithm =(unsigned char) psecuritypriv->dot11PrivacyAlgrthm;
	else
		GET_ENCRY_ALGO(psecuritypriv, sta, psetstakey_para->algorithm, _FALSE);

	if (unicast_key == _TRUE) {
		_memcpy(&psetstakey_para->key, &sta->dot118021x_UncstKey, 16);
        } else {
		_memcpy(&psetstakey_para->key, &psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid-1].skey, 16);
        }

	//jeff: set this becasue at least sw key is ready
	padapter->securitypriv.busetkipkey=_TRUE;

	enqueue_cmd(pcmdpriv, ph2c);

exit:

_func_exit_;

	return res;
}

u8 setrfintfs_cmd(_adapter *padapter, u8 mode)
{
	struct cmd_obj		*ph2c;
	struct setrfintfs_parm	*psetrfintfsparm;
	struct cmd_priv		*pcmdpriv=&padapter->cmdpriv;
	u8 res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	psetrfintfsparm = (struct setrfintfs_parm*)_malloc(sizeof(struct setrfintfs_parm));
	if (psetrfintfsparm == NULL) {
		_mfree((unsigned char *) ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetrfintfsparm, GEN_CMD_CODE(_SetRFIntFs));
	psetrfintfsparm->rfintfs = mode;

	enqueue_cmd(pcmdpriv, ph2c);

exit:

_func_exit_;

	return res;
}

u8 setrttbl_cmd(_adapter *padapter, struct setratable_parm *prate_table)
{
	struct cmd_obj		*ph2c;
	struct setratable_parm	*psetrttblparm;
	struct cmd_priv 	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if(ph2c==NULL){
		res= _FAIL;
		goto exit;
	}
	psetrttblparm = (struct setratable_parm*)_malloc(sizeof(struct setratable_parm));

	if(psetrttblparm==NULL){
		_mfree((unsigned char *) ph2c, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetrttblparm, GEN_CMD_CODE(_SetRaTable));

	_memcpy(psetrttblparm,prate_table,sizeof(struct setratable_parm));

	enqueue_cmd(pcmdpriv, ph2c);
exit:
_func_exit_;
	return res;

}

u8 getrttbl_cmd(_adapter  *padapter, struct getratable_rsp *pval)
{
	struct cmd_obj		*ph2c;
	struct getratable_parm	*pgetrttblparm;
	struct cmd_priv 	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if(ph2c==NULL){
		res= _FAIL;
		goto exit;
	}
	pgetrttblparm = (struct getratable_parm*)_malloc(sizeof(struct getratable_parm));

	if(pgetrttblparm==NULL){
		_mfree((unsigned char *) ph2c, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

//	init_h2fwcmd_w_parm_no_rsp(ph2c, psetrttblparm, GEN_CMD_CODE(_SetRaTable));

	_init_listhead(&ph2c->list);
	ph2c->cmdcode =GEN_CMD_CODE(_GetRaTable);
	ph2c->parmbuf = (unsigned char *)pgetrttblparm;
	ph2c->cmdsz =  sizeof(struct getratable_parm);
	ph2c->rsp = (u8*)pval;
	ph2c->rspsz = sizeof(struct getratable_rsp);

	pgetrttblparm ->rsvd = 0x0;

	enqueue_cmd(pcmdpriv, ph2c);
exit:
_func_exit_;
	return res;

}

u8 gettssi_cmd(_adapter *padapter, u8 offset, u8 *pval)
{
	struct cmd_priv 	*pcmdpriv = &padapter->cmdpriv;
	struct cmd_obj		*ph2c;
	struct readTSSI_parm	*prdtssiparm;
	u8 res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	prdtssiparm = (struct readTSSI_parm*)_malloc(sizeof(struct readTSSI_parm));
	if (prdtssiparm == NULL) {
		_mfree((unsigned char *) ph2c, sizeof(struct	cmd_obj));
		return _FAIL;
	}

	_init_listhead(&ph2c->list);
	ph2c->cmdcode =GEN_CMD_CODE(_ReadTSSI);
	ph2c->parmbuf = (unsigned char *)prdtssiparm;
	ph2c->cmdsz =sizeof(struct readTSSI_parm);;
	ph2c->rsp = pval;
	ph2c->rspsz = sizeof(struct readTSSI_rsp);

	prdtssiparm ->offset = offset;
	enqueue_cmd(pcmdpriv, ph2c);

exit:

_func_exit_;

	return res;
}

u8 setMacAddr_cmd(_adapter *padapter, u8 *mac_addr)
{
	struct cmd_priv 		*pcmdpriv = &padapter->cmdpriv;
	struct cmd_obj			*ph2c;
	struct SetMacAddr_param	*psetMacAddr_para;

	u8 res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	psetMacAddr_para = (struct SetMacAddr_param*)_malloc(sizeof(struct SetMacAddr_param));
	if (psetMacAddr_para == NULL) {
		_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetMacAddr_para, _SetMacAddress_CMD_);

	_memcpy(psetMacAddr_para->MacAddr, mac_addr,ETH_ALEN);

	enqueue_cmd(pcmdpriv, ph2c);

exit:

_func_exit_;

	return res;
 }

u8 setassocsta_cmd(_adapter *padapter, u8 *mac_addr)
{
	struct cmd_priv 		*pcmdpriv = &padapter->cmdpriv;
	struct cmd_obj			*ph2c;
	struct set_assocsta_parm	*psetassocsta_para;
	struct set_stakey_rsp		*psetassocsta_rsp = NULL;
	u8	res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	psetassocsta_para = (struct set_assocsta_parm*)_malloc(sizeof(struct set_assocsta_parm));
	if (psetassocsta_para == NULL) {
		_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	psetassocsta_rsp = (struct set_stakey_rsp*)_malloc(sizeof(struct set_assocsta_rsp));
	if (psetassocsta_rsp == NULL) {
		_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		_mfree((u8 *) psetassocsta_para, sizeof(struct set_assocsta_parm));
		return _FAIL;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetassocsta_para, _SetAssocSta_CMD_);
	ph2c->rsp = (u8 *) psetassocsta_rsp;
	ph2c->rspsz = sizeof(struct set_assocsta_rsp);

	_memcpy(psetassocsta_para->addr, mac_addr,ETH_ALEN);

	enqueue_cmd(pcmdpriv, ph2c);

exit:

_func_exit_;

	return res;
 }

u8 addbareq_cmd(_adapter *padapter, u8 tid)
{
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;
	struct cmd_obj		*ph2c;
	struct addBaReq_parm	*paddbareq_parm;
	u8	res = _SUCCESS;

_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	paddbareq_parm = (struct addBaReq_parm*)_malloc(sizeof(struct addBaReq_parm));
	if (paddbareq_parm == NULL) {
		_mfree((unsigned char *)ph2c, sizeof(struct	cmd_obj));
		res = _FAIL;
		goto exit;
	}

	paddbareq_parm->tid = tid;

	init_h2fwcmd_w_parm_no_rsp(ph2c, paddbareq_parm, GEN_CMD_CODE(_AddBAReq));

	//printk("addbareq_cmd, tid=%d\n", tid);

	//enqueue_cmd(pcmdpriv, ph2c);
	enqueue_cmd_ex(pcmdpriv, ph2c);

exit:

_func_exit_;

	return res;
}

u8 wdg_wk_cmd(_adapter*padapter)
{
	struct cmd_obj*		ph2c;
	struct drvint_cmd_parm  *pdrvintcmd_param;	
	struct cmd_priv	*pcmdpriv=&padapter->cmdpriv;
	u8	res=_SUCCESS;
	
_func_enter_;	

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));	
	if(ph2c==NULL){
		res= _FAIL;
		goto exit;
	}
	
	pdrvintcmd_param = (struct drvint_cmd_parm*)_malloc(sizeof(struct drvint_cmd_parm)); 
	if(pdrvintcmd_param==NULL){
		_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res= _FAIL;
		goto exit;
	}

	pdrvintcmd_param->i_cid = WDG_WK_CID;
	pdrvintcmd_param->sz = 0;
	pdrvintcmd_param->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvintcmd_param, _DRV_INT_CMD_);

	
	//enqueue_cmd(pcmdpriv, ph2c);	
	enqueue_cmd_ex(pcmdpriv, ph2c);
	
exit:
	
_func_exit_;

	return res;

}

void survey_cmd_callback(_adapter *padapter, struct cmd_obj *pcmd)
{
	struct 	mlme_priv *pmlmepriv = &padapter->mlmepriv;

_func_enter_;

	if (pcmd->res != H2C_SUCCESS) {
		clr_fwstate(pmlmepriv, _FW_UNDER_SURVEY);
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nsurvey_cmd_callback : clr _FW_UNDER_SURVEY "));
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\n ********Error: MgntActSet_802_11_BSSID_LIST_SCAN Fail ************\n\n."));
	}

	// free cmd
	free_cmd_obj(pcmd);

_func_exit_;
}
void disassoc_cmd_callback(_adapter *padapter, struct cmd_obj *pcmd)
{
	_irqL	irqL;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

_func_enter_;

	if (pcmd->res != H2C_SUCCESS)
	{
		_enter_critical(&pmlmepriv->lock, &irqL);
		set_fwstate(pmlmepriv, _FW_LINKED);
		_exit_critical(&pmlmepriv->lock, &irqL);

		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\n ***Error: disconnect_cmd_callback Fail ***\n."));

		goto exit;
	}

	// free cmd
	free_cmd_obj(pcmd);

exit:

_func_exit_;
}


void joinbss_cmd_callback(_adapter *padapter, struct cmd_obj *pcmd)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

_func_enter_;

	if ((pcmd->res != H2C_SUCCESS))
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("********Error:select_and_join_from_scanned_queue Wait Sema  Fail ************\n"));
		_set_timer(&pmlmepriv->assoc_timer, 1);
	}

	free_cmd_obj(pcmd);

_func_exit_;
}

void createbss_cmd_callback(_adapter *padapter, struct cmd_obj *pcmd)
{
	_irqL	irqL;
	u8	timer_cancelled;
	struct sta_info		*psta = NULL;
	struct wlan_network	*pwlan = NULL;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	NDIS_WLAN_BSSID_EX	*pnetwork = (NDIS_WLAN_BSSID_EX *)pcmd->parmbuf;
	struct wlan_network	*tgt_network = &(pmlmepriv->cur_network);

_func_enter_;

	if((pcmd->res != H2C_SUCCESS))
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\n ********Error: createbss_cmd_callback  Fail ************\n\n."));
		_set_timer(&pmlmepriv->assoc_timer, 1 );
	}

	_cancel_timer(&pmlmepriv->assoc_timer, &timer_cancelled);

       //endian_convert
        pnetwork->Length = le32_to_cpu(pnetwork->Length);
  	pnetwork->Ssid.SsidLength = le32_to_cpu(pnetwork->Ssid.SsidLength);
	pnetwork->Privacy =le32_to_cpu(pnetwork->Privacy);
	pnetwork->Rssi = le32_to_cpu(pnetwork->Rssi);
	pnetwork->NetworkTypeInUse =le32_to_cpu(pnetwork->NetworkTypeInUse);
	pnetwork->Configuration.ATIMWindow = le32_to_cpu(pnetwork->Configuration.ATIMWindow);
	//pnetwork->Configuration.BeaconPeriod = le32_to_cpu(pnetwork->Configuration.BeaconPeriod);
	pnetwork->Configuration.DSConfig =le32_to_cpu(pnetwork->Configuration.DSConfig);
	pnetwork->Configuration.FHConfig.DwellTime=le32_to_cpu(pnetwork->Configuration.FHConfig.DwellTime);
	pnetwork->Configuration.FHConfig.HopPattern=le32_to_cpu(pnetwork->Configuration.FHConfig.HopPattern);
	pnetwork->Configuration.FHConfig.HopSet=le32_to_cpu(pnetwork->Configuration.FHConfig.HopSet);
	pnetwork->Configuration.FHConfig.Length=le32_to_cpu(pnetwork->Configuration.FHConfig.Length);
	pnetwork->Configuration.Length = le32_to_cpu(pnetwork->Configuration.Length);
	pnetwork->InfrastructureMode = le32_to_cpu(pnetwork->InfrastructureMode);
	pnetwork->IELength = le32_to_cpu(pnetwork->IELength);


	_enter_critical(&pmlmepriv->lock, &irqL);


	if((pmlmepriv->fw_state) & WIFI_AP_STATE)
	{
		psta = get_stainfo(&padapter->stapriv, pnetwork->MacAddress);
		if(!psta)
		{
			psta = alloc_stainfo(&padapter->stapriv, pnetwork->MacAddress);
			if (psta == NULL) 
			{ 
				RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nCan't alloc sta_info when createbss_cmd_callback\n"));
				goto createbss_cmd_fail ;
			}
		}	
			
		indicate_connect( padapter);
	}
	else
	{
		pwlan = _alloc_network(pmlmepriv);

		if ( pwlan == NULL)
		{
			pwlan = get_oldest_wlan_network(&pmlmepriv->scanned_queue);
			if( pwlan == NULL)
			{
				RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\n Error:  can't get pwlan in joinbss_event_callback \n"));
				goto createbss_cmd_fail;
			}
			pwlan->last_scanned = get_current_time();
		}
		else
		{
			list_insert_tail(&(pwlan->list), &pmlmepriv->scanned_queue.queue);
		}

		pnetwork->Length = get_NDIS_WLAN_BSSID_EX_sz(pnetwork);
		_memcpy(&(pwlan->network), pnetwork, pnetwork->Length);
		pwlan->fixed = _TRUE;

		//list_insert_tail(&(pwlan->list), &pmlmepriv->scanned_queue.queue);

		// copy pdev_network information to 	pmlmepriv->cur_network
		_memcpy(&tgt_network->network, pnetwork, (get_NDIS_WLAN_BSSID_EX_sz(pnetwork)));

		// reset DSConfig
		//tgt_network->network.Configuration.DSConfig = (u32)ch2freq(pnetwork->Configuration.DSConfig);


		if(pmlmepriv->fw_state & _FW_UNDER_LINKING)
		    pmlmepriv->fw_state ^= _FW_UNDER_LINKING;

#if 0
		if((pmlmepriv->fw_state) & WIFI_AP_STATE)
		{
			psta = alloc_stainfo(&padapter->stapriv, pnetwork->MacAddress);

			if (psta == NULL) { // for AP Mode & Adhoc Master Mode
				RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nCan't alloc sta_info when createbss_cmd_callback\n"));
				goto createbss_cmd_fail ;
			}

			indicate_connect( padapter);
		}
		else {

			//indicate_disconnect(dev);
		}
#endif

		// we will set _FW_LINKED when there is one more sat to join us (stassoc_event_callback)

	}

createbss_cmd_fail:

	_exit_critical(&pmlmepriv->lock, &irqL);

	free_cmd_obj(pcmd);

_func_exit_;

}



void setstaKey_cmdrsp_callback(_adapter*	padapter ,  struct cmd_obj *pcmd)
{

	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct set_stakey_rsp	*psetstakey_rsp = (struct set_stakey_rsp*) (pcmd->rsp);
	struct sta_info		*psta = get_stainfo(pstapriv, psetstakey_rsp->addr);

_func_enter_;

	if(psta==NULL)
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nERROR: setstaKey_cmdrsp_callback => can't get sta_info \n\n"));
		goto exit;
	}

	psta->aid = psta->mac_id = psetstakey_rsp->keyid; //CAM_ID(CAM_ENTRY)

exit:

	free_cmd_obj(pcmd);

_func_exit_;

}
void setassocsta_cmdrsp_callback(_adapter*	padapter,  struct cmd_obj *pcmd)
{
	_irqL	irqL;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct set_assocsta_parm	*passocsta_parm = (struct set_assocsta_parm*)(pcmd->parmbuf);
	struct set_assocsta_rsp	*passocsta_rsp = (struct set_assocsta_rsp*) (pcmd->rsp);
	struct sta_info		*psta = get_stainfo(pstapriv, passocsta_parm->addr);

_func_enter_;

	if(psta==NULL)
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nERROR: setassocsta_cmdrsp_callbac => can't get sta_info \n\n"));
		goto exit;
	}

	psta->aid = psta->mac_id = passocsta_rsp->cam_id;

	_enter_critical(&pmlmepriv->lock, &irqL);

	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) && (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE))
		pmlmepriv->fw_state ^= _FW_UNDER_LINKING;

	set_fwstate(pmlmepriv, _FW_LINKED);
	_exit_critical(&pmlmepriv->lock, &irqL);

	free_cmd_obj(pcmd);
exit:
_func_exit_;
}

void getrttbl_cmd_cmdrsp_callback(_adapter*	padapter,  struct cmd_obj *pcmd)
{
_func_enter_;

	free_cmd_obj(pcmd);
#ifdef CONFIG_MP_INCLUDED
	padapter->mppriv.workparam.bcompleted=_TRUE;
#endif

_func_exit_;

}

#ifdef CONFIG_PWRCTRL
u8  setatim_cmd(_adapter* adapter, u8 add, u8 txid)
{

	struct	cmd_obj*	ph2c;
	struct	setatim_parm* psetatim;
	struct	cmd_priv   *pcmdpriv= &( adapter->cmdpriv);
	u8	res=_SUCCESS;
_func_enter_;
	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if(ph2c==NULL){
		goto exit;
		res= _FAIL;
	}
	psetatim = (struct setatim_parm*)_malloc(sizeof(struct setatim_parm));

	if(psetatim == NULL){
		_mfree((unsigned char *) ph2c, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

	//NdisZeroMemory();
	psetatim->op = add;
	psetatim->txid = txid;


	init_h2fwcmd_w_parm_no_rsp(ph2c, psetatim, _SetAtim_CMD_);

	enqueue_cmd(pcmdpriv, ph2c);
exit:
_func_exit_;
	return res;
}

u8 setpwrmode_cmd(_adapter* adapter, u32 ps_mode, u32 smart_ps)
{
	struct	cmd_obj*	ph2c;
	struct	setpwrmode_parm* psetpwr;
	struct	cmd_priv   *pcmdpriv= &( adapter->cmdpriv);
	u8 res=_SUCCESS;

_func_enter_;

	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("setpwrmode_cmd mode  = %x, smart_ps = %x", ps_mode,smart_ps));

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if(ph2c==NULL){
		res= _FAIL;
		goto exit;
		}

	psetpwr = (struct setpwrmode_parm*)_malloc(sizeof(struct setpwrmode_parm));
	if(psetpwr == NULL){
		_mfree((unsigned char *) ph2c, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

	_memset(psetpwr, 0, sizeof(struct setpwrmode_parm));

	psetpwr->mode = (unsigned char)ps_mode;
	psetpwr->bcn_pass_cnt = 0;
	psetpwr->smart_ps = (unsigned char)smart_ps;

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetpwr, _SetPwrMode_CMD_);

	enqueue_cmd(pcmdpriv, ph2c);
exit:
_func_exit_;
	return res;
}
#endif

u8 disconnectCtrlEx_cmd(_adapter* adapter, u32 enableDrvCtrl, u32 tryPktCnt, u32 tryPktInterval, u32 firstStageTO){
	struct	cmd_obj*	ph2c;
	struct	DisconnectCtrlEx_param * param;
	struct	cmd_priv   *pcmdpriv= &( adapter->cmdpriv);
	u8 res=_SUCCESS;


_func_enter_;

	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,
		("%s  = %x, TryPktCnt = %x, TryPktInterval = %x, FirstStageTO = %x"
		, __function__ , enableDrvCtrl, tryPktCnt, tryPktInterval, firstStageTO)
	);

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if(ph2c==NULL){
		res= _FAIL;
		goto exit;
	}

	param = (struct DisconnectCtrlEx_param *)_malloc(sizeof(struct DisconnectCtrlEx_param));
	if(param == NULL){
		_mfree((unsigned char *) ph2c, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

	_memset(param, 0, sizeof(struct DisconnectCtrlEx_param));

	param->EnableDrvCtrl = (unsigned char)enableDrvCtrl;
	param->TryPktCnt = (unsigned char)tryPktCnt;
	param->TryPktInterval = (unsigned char)tryPktInterval;
	param->FirstStageTO = (unsigned int)firstStageTO;

	init_h2fwcmd_w_parm_no_rsp(ph2c, param, GEN_CMD_CODE(_DisconnectCtrlEx));

	enqueue_cmd(pcmdpriv, ph2c);
exit:
_func_exit_;
	return res;

}

