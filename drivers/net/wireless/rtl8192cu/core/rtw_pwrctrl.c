/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#define _RTW_PWRCTRL_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>

#ifdef CONFIG_SDIO_HCI
#include <sdio_ops.h>
#endif

#ifdef CONFIG_IPS
void ips_enter(_adapter * padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	

	_enter_pwrlock(&pwrpriv->lock);

	pwrpriv->bips_processing = _TRUE;	

	// syn ips_mode with request
	pwrpriv->ips_mode = pwrpriv->ips_mode_req;
	
	pwrpriv->ips_enter_cnts++;	
	DBG_8192C("==>ips_enter cnts:%d\n",pwrpriv->ips_enter_cnts);
	
	if(rf_off == pwrpriv->change_rfpwrstate )
	{	
		DBG_8192C("==>power_saving_ctrl_wk_hdl change rf to OFF...LED(0x%08x).... \n\n",rtw_read32(padapter,0x4c));
		
		if(pwrpriv->ips_mode == IPS_LEVEL_2)
			pwrpriv->bkeepfwalive = _TRUE;
		
		rtw_ips_pwr_down(padapter);
		pwrpriv->rf_pwrstate = rf_off;
	}	
	pwrpriv->bips_processing = _FALSE;	
	_exit_pwrlock(&pwrpriv->lock);
	
}

int ips_leave(_adapter * padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct security_priv* psecuritypriv=&(padapter->securitypriv);
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);
	int result = _SUCCESS;
	sint keyid;
	_enter_pwrlock(&pwrpriv->lock);
	if((pwrpriv->rf_pwrstate == rf_off) &&(!pwrpriv->bips_processing))
	{
		pwrpriv->change_rfpwrstate = rf_on;
		pwrpriv->ips_leave_cnts++;
		DBG_8192C("==>ips_leave cnts:%d\n",pwrpriv->ips_leave_cnts);

		result = rtw_ips_pwr_up(padapter);		
		pwrpriv->bips_processing = _TRUE;
		pwrpriv->rf_pwrstate = rf_on;

		if((_WEP40_ == psecuritypriv->dot11PrivacyAlgrthm) ||(_WEP104_ == psecuritypriv->dot11PrivacyAlgrthm))
		{
			DBG_8192C("==>%s,channel(%d),processing(%x)\n",__FUNCTION__,padapter->mlmeextpriv.cur_channel,pwrpriv->bips_processing);
			set_channel_bwmode(padapter, padapter->mlmeextpriv.cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);			
			for(keyid=0;keyid<4;keyid++){
				if(pmlmepriv->key_mask & BIT(keyid)){
					if(keyid == psecuritypriv->dot11PrivacyKeyIndex)
						result=rtw_set_key(padapter,psecuritypriv, keyid, 1);
					else
						result=rtw_set_key(padapter,psecuritypriv, keyid, 0);
				}
			}
		}
		
		DBG_8192C("==> ips_leave.....LED(0x%08x)...\n",rtw_read32(padapter,0x4c));
		pwrpriv->bips_processing = _FALSE;

		pwrpriv->bkeepfwalive = _FALSE;


	}
	_exit_pwrlock(&pwrpriv->lock);
	return result;
}


#endif

#ifdef CONFIG_AUTOSUSPEND
extern void autosuspend_enter(_adapter* padapter);	
extern int autoresume_enter(_adapter* padapter);
#endif

#ifdef SUPPORT_HW_RFOFF_DETECTED
int rtw_hw_suspend(_adapter *padapter );
int rtw_hw_resume(_adapter *padapter);
#endif

#ifdef PLATFORM_LINUX
void rtw_ps_processor(_adapter*padapter)
{
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
#endif //CONFIG_P2P
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	int res;
	rt_rf_power_state rfpwrstate;
	
#ifdef SUPPORT_HW_RFOFF_DETECTED
	if(pwrpriv->bips_processing == _TRUE)	return;		
	
	//DBG_8192C("==> fw report state(0x%x)\n",rtw_read8(padapter,0x1ca));	
	if(padapter->pwrctrlpriv.bHWPwrPindetect) 
	{
	#ifdef CONFIG_AUTOSUSPEND
		if(padapter->registrypriv.usbss_enable)
		{
			if(pwrpriv->rf_pwrstate == rf_on)
			{
				if(padapter->net_closed == _TRUE)
					pwrpriv->ps_flag = _TRUE;

				rfpwrstate = RfOnOffDetect(padapter);
				DBG_8192C("@@@@- #1  %s==> rfstate:%s \n",__FUNCTION__,(rfpwrstate==rf_on)?"rf_on":"rf_off");
				if(rfpwrstate!= pwrpriv->rf_pwrstate)
				{
					if(rfpwrstate == rf_off)
					{
						pwrpriv->change_rfpwrstate = rf_off;
						
						pwrpriv->bkeepfwalive = _TRUE;	
						pwrpriv->brfoffbyhw = _TRUE;						
						
						autosuspend_enter(padapter);							
					}
				}
			}			
		}
		else
	#endif //CONFIG_AUTOSUSPEND
		{
			rfpwrstate = RfOnOffDetect(padapter);
			DBG_8192C("@@@@- #2  %s==> rfstate:%s \n",__FUNCTION__,(rfpwrstate==rf_on)?"rf_on":"rf_off");

			if(rfpwrstate!= pwrpriv->rf_pwrstate)
			{
				if(rfpwrstate == rf_off)
				{	
					pwrpriv->change_rfpwrstate = rf_off;														
					pwrpriv->brfoffbyhw = _TRUE;
					padapter->bCardDisableWOHSM = _TRUE;
					rtw_hw_suspend(padapter );	
				}
				else
				{
					pwrpriv->change_rfpwrstate = rf_on;
					rtw_hw_resume(padapter );			
				}
				DBG_8192C("current rf_pwrstate(%s)\n",(pwrpriv->rf_pwrstate == rf_off)?"rf_off":"rf_on");
			}
		}
		pwrpriv->pwr_state_check_cnts ++;	
	}
#endif //SUPPORT_HW_RFOFF_DETECTED

	if( pwrpriv->power_mgnt == PS_MODE_ACTIVE )	return;

	if((pwrpriv->rf_pwrstate == rf_on) && ((pwrpriv->pwr_state_check_cnts%4)==0))
	{
		if (	(check_fwstate(pmlmepriv, _FW_LINKED|_FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE) ||
			(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) ||
			(padapter->bup == _FALSE)	
			#ifdef CONFIG_P2P
			|| !rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)
			#endif //CONFIG_P2P
		)
		{
			return;
		}
			
		DBG_8192C("==>%s .fw_state(%x)\n",__FUNCTION__,get_fwstate(pmlmepriv));
		pwrpriv->change_rfpwrstate = rf_off;

		#ifdef CONFIG_AUTOSUSPEND
		if(padapter->registrypriv.usbss_enable)
		{		
			if(padapter->pwrctrlpriv.bHWPwrPindetect) 
				pwrpriv->bkeepfwalive = _TRUE;	

			if(padapter->net_closed == _TRUE)
				pwrpriv->ps_flag = _TRUE;

			padapter->bCardDisableWOHSM = _TRUE;
			autosuspend_enter(padapter);
		}		
		else if(padapter->pwrctrlpriv.bHWPwrPindetect)
		{
		}
		else
		#endif //CONFIG_AUTOSUSPEND
		{
			#ifdef CONFIG_IPS	
			ips_enter(padapter);			
			#endif
		}
	}

	
}

void pwr_state_check_handler(void *FunctionContext)
{
	_adapter *padapter = (_adapter *)FunctionContext;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
#endif //CONFIG_P2P
	//DBG_871X("%s\n", __FUNCTION__);

#ifdef SUPPORT_HW_RFOFF_DETECTED
	//DBG_8192C("%s...bHWPwrPindetect(%d)\n",__FUNCTION__,padapter->pwrctrlpriv.bHWPwrPindetect);
	if(padapter->pwrctrlpriv.bHWPwrPindetect)
	{
		rtw_ps_cmd(padapter);		
		rtw_set_pwr_state_check_timer(&padapter->pwrctrlpriv);
	}	
	else	
#endif
	{
		//if(padapter->net_closed == _TRUE)		return;
		//DBG_8192C("==>%s .fw_state(%x)\n", __FUNCTION__, get_fwstate(pmlmepriv));
		if (	(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) ||	
			(check_fwstate(pmlmepriv, _FW_LINKED|_FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE)  ||
			(padapter->bup == _FALSE)		
#ifdef CONFIG_P2P
			|| !rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)
#endif //CONFIG_P2P
			)
		{	
			//other pwr ctrl....	
			rtw_set_pwr_state_check_timer(&padapter->pwrctrlpriv);
		}
		else
		{	
			if((pwrpriv->rf_pwrstate == rf_on) &&(_FALSE == pwrpriv->bips_processing))
			{	
				pwrpriv->change_rfpwrstate = rf_off;
				pwrctrlpriv->pwr_state_check_cnts = 0;
				DBG_8192C("==>pwr_state_check_handler .fw_state(%x)\n",get_fwstate(pmlmepriv));				
				rtw_ps_cmd(padapter);				
			}

		}
	}
	


}
#endif


#ifdef CONFIG_LPS
void rtw_set_rpwm(_adapter * padapter, u8 val8)
{
	u8	rpwm;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

_func_enter_;

	if(pwrpriv->rpwm == val8){
		RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("Already set rpwm [%d] ! \n", val8));
		return;
	}

	if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE)){
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("rtw_set_rpwm=> bDriverStopped or bSurpriseRemoved \n"));
		return;
	}
	rpwm = val8 |pwrpriv->tog;

	pwrpriv->rpwm = val8;
	
	RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("rtw_set_rpwm: value = %x\n", rpwm));

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_SET_RPWM, (u8 *)(&rpwm));
	
	pwrpriv->tog += 0x80;

_func_exit_;
}

u8 PS_RDY_CHECK(_adapter * padapter)
{
	u32 curr_time, delta_time;
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);

	curr_time = rtw_get_current_time();	

	delta_time = curr_time -pwrpriv->DelayLPSLastTimeStamp;

	if(delta_time < LPS_DELAY_TIME)
	{		
		return _FALSE;
	}

	if (	(check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE) ||
		(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) )
		return _FALSE;

	if(_TRUE == pwrpriv->bInSuspend )
		return _FALSE;
	
	if( (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) && (padapter->securitypriv.binstallGrpkey == _FALSE) )
	{
		DBG_8192C("Group handshake still in progress !!!\n");
		return _FALSE;
	}

	return _TRUE;
}

void rtw_set_ps_mode(_adapter * padapter, u8 ps_mode, u8 smart_ps)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
#endif //CONFIG_P2P

_func_enter_;

	RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("========= Power Mode is :%d, Smart_PS = %d\n", ps_mode,smart_ps));
	//DBG_8192C("========= Power Mode is :%d, Smart_PS = %d\n", ps_mode,smart_ps);

	if(ps_mode > PM_Card_Disable) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("ps_mode:%d error\n", ps_mode));
		return;
	}

	if((pwrpriv->pwr_mode == ps_mode) &&
		(pwrpriv->smart_ps == smart_ps)){
		return;
	}

	//if(pwrpriv->pwr_mode == PS_MODE_ACTIVE)
	if(ps_mode == PS_MODE_ACTIVE)
	{
#ifdef CONFIG_P2P
		if(pwdinfo->opp_ps == 0)
#endif //CONFIG_P2P
		{
			DBG_8192C("rtw_set_ps_mode(): Busy Traffic , Leave 802.11 power save..\n");
			pwrpriv->smart_ps = smart_ps;
			pwrpriv->pwr_mode = ps_mode;
			rtw_set_rpwm(padapter, PS_STATE_S4);
			padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));
			pwrpriv->bFwCurrentInPSMode = _FALSE;
		}
	}
	else
	{
		if(PS_RDY_CHECK(padapter))
		{
			DBG_8192C("rtw_set_ps_mode(): Enter 802.11 power save mode...\n");
			pwrpriv->smart_ps = smart_ps;
			pwrpriv->pwr_mode = ps_mode;
			pwrpriv->bFwCurrentInPSMode = _TRUE;
			padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));
#ifdef CONFIG_P2P
			// Set CTWindow after LPS
			if(pwdinfo->opp_ps == 1)
			//if(pwdinfo->p2p_ps_enable == _TRUE)
				p2p_ps_wk_cmd(padapter, P2P_PS_ENABLE, 0);
#endif //CONFIG_P2P
			rtw_set_rpwm(padapter, PS_STATE_S2);
		}
		//else
		//{
		//	pwrpriv->pwr_mode = PS_MODE_ACTIVE;
		//}
	}

_func_exit_;
}


//
//	Description:
//		Enter the leisure power save mode.
//
void LPS_Enter(PADAPTER padapter)
{
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);

_func_enter_;

//	DBG_871X("+LeisurePSEnter\n");

	if (	(check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE) ||
		(check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) )
		return;
	
	if(_TRUE == pwrpriv->bInSuspend )
		return ;
	
	if (pwrpriv->bLeisurePs)
	{
		// Idle for a while if we connect to AP a while ago.
		if(pwrpriv->LpsIdleCount >= 2) //  4 Sec 
		{
			if(pwrpriv->pwr_mode == PS_MODE_ACTIVE)
			{
				rtw_set_ps_mode(padapter, pwrpriv->power_mgnt, 2);
			}	
		}
		else
			pwrpriv->LpsIdleCount++;
	}

_func_exit_;
}


//
//	Description:
//		Leave the leisure power save mode.
//
void LPS_Leave(PADAPTER padapter)
{
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);

_func_enter_;

	//DBG_8192C("LeisurePSLeave()...\n");

	if (pwrpriv->bLeisurePs)
	{	
		if(pwrpriv->pwr_mode != PS_MODE_ACTIVE)
		{
			rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0);
		}
	}

_func_exit_;
}

#endif

//
// Description: Leave all power save mode: LPS, FwLPS, IPS if needed.
// Move code to function by tynli. 2010.03.26. 
//
void LeaveAllPowerSaveMode(IN PADAPTER Adapter)
{
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	u32 LPSLeaveTimeOut = 10000;
	//u32 IPSLeaveTimeOut = 10000;

_func_enter_;

	//DBG_8192C("%s.....\n",__FUNCTION__);
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
	{ //connect
#ifdef CONFIG_P2P
		p2p_ps_wk_cmd(Adapter, P2P_PS_DISABLE, 0);
#endif //CONFIG_P2P
#ifdef CONFIG_LPS
		//DBG_8192C("==> leave LPS.......\n");
		LPS_Leave(Adapter);

		if (Adapter->pwrctrlpriv.bLeisurePs)
		{
			BOOLEAN bAwake = _TRUE;
			Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&bAwake));
			while(!bAwake)
			{
				rtw_usleep_os(100);
				LPSLeaveTimeOut--;
				if(LPSLeaveTimeOut <= 0)
				{
					DBG_8192C("Wait for FW LPS leave too long!!! LPSLeaveTimeOut  = %d\n", LPSLeaveTimeOut );
					break;
				}
				Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&bAwake));
			}
		}
#endif
	}
	else
	{
		if(Adapter->pwrctrlpriv.rf_pwrstate== rf_off)
		{
			#ifdef CONFIG_AUTOSUSPEND
			if(Adapter->registrypriv.usbss_enable)
			{
				#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
				usb_disable_autosuspend(Adapter->dvobjpriv.pusbdev);
				#elif (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,22) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,34))
				Adapter->dvobjpriv.pusbdev->autosuspend_disabled = Adapter->bDisableAutosuspend;//autosuspend disabled by the user
				#endif
			}		
			else
			#endif
			{
			/*
				#ifdef CONFIG_IPS
				if(_FALSE == ips_leave(Adapter))
				{
					DBG_8192C("======> ips_leave fail.............\n");			
				}
				#endif
			*/
			}		
		}	
	}

_func_exit_;
}

#ifdef CONFIG_PWRCTRL

/*
Caller:ISR handler...

This will be called when CPWM interrupt is up.

using to update cpwn of drv; and drv willl make a decision to up or down pwr level
*/
void cpwm_int_hdl(_adapter *padapter, struct reportpwrstate_parm *preportpwrstate)
{
	struct pwrctrl_priv *pwrpriv = &(padapter->pwrctrlpriv);
	struct cmd_priv	*pcmdpriv = &(padapter->cmdpriv);
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);

_func_enter_;

	if(pwrpriv->cpwm_tog == ((preportpwrstate->state)&0x80)){
		RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("cpwm_int_hdl : cpwm_tog = %x this time cpwm=0x%x  toggle bit didn't change !!!\n",pwrpriv->cpwm_tog ,preportpwrstate->state));	
		goto exit;
	}

	_enter_pwrlock(&pwrpriv->lock);

	pwrpriv->cpwm = (preportpwrstate->state)&0xf;

	if(pwrpriv->cpwm >= PS_STATE_S2){
		if(pwrpriv->alives & CMD_ALIVE)
			_rtw_up_sema(&(pcmdpriv->cmd_queue_sema));

		if(pwrpriv->alives & XMIT_ALIVE)
			_rtw_up_sema(&(pxmitpriv->xmit_sema));
	}
	pwrpriv->cpwm_tog=  (preportpwrstate->state)&0x80;
	_exit_pwrlock(&pwrpriv->lock);
exit:
	RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("cpwm_int_hdl : cpwm = %x !!!\n",pwrpriv->cpwm));

_func_exit_;

}


__inline static void	register_task_alive(struct pwrctrl_priv *pwrctrl, uint tag)
{
_func_enter_;
		pwrctrl->alives |= tag;
_func_exit_;
}

__inline static void	unregister_task_alive(struct pwrctrl_priv *pwrctrl, uint tag)
{
_func_enter_;

	if (pwrctrl->alives & tag)
		pwrctrl->alives ^= tag;

_func_exit_;	
}
#endif

#ifdef CONFIG_RESUME_IN_WORKQUEUE
static void resume_workitem_callback(struct work_struct *work);
#endif //CONFIG_RESUME_IN_WORKQUEUE

void	rtw_init_pwrctrl_priv(_adapter *padapter)
{
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

_func_enter_;

#ifdef PLATFORM_WINDOWS
	pwrctrlpriv->pnp_current_pwr_state=NdisDeviceStateD0;
#endif

	_init_pwrlock(&pwrctrlpriv->lock);
	pwrctrlpriv->rf_pwrstate = rf_on;
	pwrctrlpriv->ips_enter_cnts=0;
	pwrctrlpriv->ips_leave_cnts=0;

	pwrctrlpriv->ips_mode = padapter->registrypriv.ips_mode;
	pwrctrlpriv->ips_mode_req = padapter->registrypriv.ips_mode;

	pwrctrlpriv->pwr_state_check_interval = 2000;
	pwrctrlpriv->pwr_state_check_cnts = 0;
	pwrctrlpriv->bInternalAutoSuspend = _FALSE;
	pwrctrlpriv->bInSuspend = _FALSE;
	pwrctrlpriv->bkeepfwalive = _FALSE;
	
#ifdef CONFIG_AUTOSUSPEND	
#ifdef SUPPORT_HW_RFOFF_DETECTED
	pwrctrlpriv->pwr_state_check_interval = (pwrctrlpriv->bHWPwrPindetect) ?1000:2000;		
#endif	
#endif
	
	pwrctrlpriv->LpsIdleCount = 0;
	//pwrctrlpriv->FWCtrlPSMode =padapter->registrypriv.power_mgnt;// PS_MODE_MIN;
	pwrctrlpriv->power_mgnt =padapter->registrypriv.power_mgnt;// PS_MODE_MIN;
	pwrctrlpriv->bLeisurePs = (PS_MODE_ACTIVE != pwrctrlpriv->power_mgnt)?_TRUE:_FALSE;

	pwrctrlpriv->bFwCurrentInPSMode = _FALSE;

	pwrctrlpriv->cpwm = PS_STATE_S4;

	pwrctrlpriv->pwr_mode = PS_MODE_ACTIVE;

	
	pwrctrlpriv->smart_ps = 0;

	pwrctrlpriv->tog = 0x80;

#ifdef PLATFORM_LINUX		
	_init_timer(&(pwrctrlpriv->pwr_state_check_timer), padapter->pnetdev, pwr_state_check_handler, (u8 *)padapter);
#endif

	#ifdef CONFIG_RESUME_IN_WORKQUEUE
	_init_workitem(&pwrctrlpriv->resume_work, resume_workitem_callback, NULL);
	pwrctrlpriv->rtw_workqueue = create_singlethread_workqueue("rtw_workqueue");
	#endif //CONFIG_RESUME_IN_WORKQUEUE

	#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	pwrctrlpriv->early_suspend.suspend = NULL;
	rtw_register_early_suspend(pwrctrlpriv);
	#endif //CONFIG_HAS_EARLYSUSPEND || CONFIG_ANDROID_POWER


_func_exit_;

}


void	rtw_free_pwrctrl_priv(_adapter *adapter)
{
	struct pwrctrl_priv *pwrctrlpriv = &adapter->pwrctrlpriv;

_func_enter_;

	//_rtw_memset((unsigned char *)pwrctrlpriv, 0, sizeof(struct pwrctrl_priv));


	#ifdef CONFIG_RESUME_IN_WORKQUEUE
	if (pwrctrlpriv->rtw_workqueue) { 
		flush_workqueue(pwrctrlpriv->rtw_workqueue);
		destroy_workqueue(pwrctrlpriv->rtw_workqueue);
	}
	#endif


	#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	rtw_unregister_early_suspend(pwrctrlpriv);
	#endif //CONFIG_HAS_EARLYSUSPEND || CONFIG_ANDROID_POWER

	_free_pwrlock(&pwrctrlpriv->lock);

_func_exit_;
}


/*
Caller: rtw_xmit_thread

Check if the fw_pwrstate is okay for xmit.
If not (cpwm is less than P1 state), then the sub-routine
will raise the cpwm to be greater than or equal to P1. 

Calling Context: Passive

Return Value:

_SUCCESS: rtw_xmit_thread can write fifo/txcmd afterwards.
_FAIL: rtw_xmit_thread can not do anything.
*/
sint rtw_register_tx_alive(_adapter *padapter)
{
	uint res = _SUCCESS;
	
#ifdef CONFIG_PWRCTRL

	struct pwrctrl_priv *pwrctrl = &padapter->pwrctrlpriv;

_func_enter_;

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, XMIT_ALIVE);
	RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("rtw_register_tx_alive: cpwm:%d alives:%x\n", pwrctrl->cpwm, pwrctrl->alives));

	if(pwrctrl->cpwm < PS_STATE_S2){
		rtw_set_rpwm(padapter, PS_STATE_S3);
		res = _FAIL;
	}

	_exit_pwrlock(&pwrctrl->lock);
	
_func_exit_;

#endif	/* CONFIG_PWRCTRL */

	return res;	

}

/*
Caller: rtw_cmd_thread

Check if the fw_pwrstate is okay for issuing cmd.
If not (cpwm should be is less than P2 state), then the sub-routine
will raise the cpwm to be greater than or equal to P2. 

Calling Context: Passive

Return Value:

_SUCCESS: rtw_cmd_thread can issue cmds to firmware afterwards.
_FAIL: rtw_cmd_thread can not do anything.
*/
sint rtw_register_cmd_alive(_adapter *padapter)
{
	uint res = _SUCCESS;
	
#ifdef CONFIG_PWRCTRL

	struct pwrctrl_priv *pwrctrl = &padapter->pwrctrlpriv;

_func_enter_;

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, CMD_ALIVE);
	RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("rtw_register_cmd_alive: cpwm:%d alives:%x\n", pwrctrl->cpwm, pwrctrl->alives));

	if(pwrctrl->cpwm < PS_STATE_S2){
		rtw_set_rpwm(padapter, PS_STATE_S3);
		res = _FAIL;
	}

	_exit_pwrlock(&pwrctrl->lock);
_func_exit_;
#endif

	return res;
}


/*
Caller: rx_isr

Calling Context: Dispatch/ISR

Return Value:

*/
sint rtw_register_rx_alive(_adapter *padapter)
{

#ifdef CONFIG_PWRCTRL

	struct pwrctrl_priv *pwrctrl = &padapter->pwrctrlpriv;

_func_enter_;

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, RECV_ALIVE);

	RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("rtw_register_rx_alive: cpwm:%d alives:%x\n", pwrctrl->cpwm, pwrctrl->alives));

	_exit_pwrlock(&pwrctrl->lock);

_func_exit_;
	
#endif /*CONFIG_PWRCTRL*/

	return _SUCCESS;
}


/*
Caller: evt_isr or evt_thread

Calling Context: Dispatch/ISR or Passive

Return Value:
*/
sint rtw_register_evt_alive(_adapter *padapter)
{

#ifdef CONFIG_PWRCTRL

	struct pwrctrl_priv *pwrctrl = &padapter->pwrctrlpriv;

_func_enter_;

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, EVT_ALIVE);

	RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_info_,("rtw_register_evt_alive: cpwm:%d alives:%x\n", pwrctrl->cpwm, pwrctrl->alives));

	_exit_pwrlock(&pwrctrl->lock);

_func_exit_;

#endif /*CONFIG_PWRCTRL*/

	return _SUCCESS;
}


/*
Caller: ISR

If ISR's txdone,
No more pkts for TX,
Then driver shall call this fun. to power down firmware again.
*/

void rtw_unregister_tx_alive(_adapter *padapter)
{
#ifdef CONFIG_PWRCTRL

	struct pwrctrl_priv *pwrctrl = &padapter->pwrctrlpriv;

_func_enter_;

	_enter_pwrlock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, XMIT_ALIVE);

	if((pwrctrl->cpwm > PS_STATE_S2) && (pwrctrl->pwr_mode > PS_MODE_ACTIVE)){
		if(pwrctrl->alives == 0){
			rtw_set_rpwm(padapter, PS_STATE_S0);
		}
	}

	RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("rtw_unregister_tx_alive: cpwm:%d alives:%x\n", pwrctrl->cpwm, pwrctrl->alives));
	
	_exit_pwrlock(&pwrctrl->lock);

_func_exit_;

#endif /*CONFIG_PWRCTRL*/
}

/*
Caller: ISR

If ISR's txdone,
No more pkts for TX,
Then driver shall call this fun. to power down firmware again.
*/

void rtw_unregister_cmd_alive(_adapter *padapter)
{
#ifdef CONFIG_PWRCTRL

	struct pwrctrl_priv *pwrctrl = &padapter->pwrctrlpriv;

_func_enter_;

	_enter_pwrlock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, CMD_ALIVE);

	if((pwrctrl->cpwm > PS_STATE_S2) && (pwrctrl->pwr_mode > PS_MODE_ACTIVE)){
		if((pwrctrl->alives == 0)&&(check_fwstate(&padapter->mlmepriv, _FW_UNDER_LINKING)!=_TRUE)){
			rtw_set_rpwm(padapter, PS_STATE_S0);
		}
	}

	RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("rtw_unregister_cmd_alive: cpwm:%d alives:%x\n", pwrctrl->cpwm, pwrctrl->alives));

	_exit_pwrlock(&pwrctrl->lock);

_func_exit_;

#endif /*CONFIG_PWRCTRL*/
}


/*

Caller: ISR

*/
void rtw_unregister_rx_alive(_adapter *padapter)
{
#ifdef CONFIG_PWRCTRL

	struct pwrctrl_priv *pwrctrl = &padapter->pwrctrlpriv;

_func_enter_;

	_enter_pwrlock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, RECV_ALIVE);

	RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("rtw_unregister_rx_alive: cpwm:%d alives:%x\n", pwrctrl->cpwm, pwrctrl->alives));
	
	_exit_pwrlock(&pwrctrl->lock);

_func_exit_;

#endif
}


void rtw_unregister_evt_alive(_adapter *padapter)
{
#ifdef CONFIG_PWRCTRL

	struct pwrctrl_priv *pwrctrl = &padapter->pwrctrlpriv;

_func_enter_;

	_enter_pwrlock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, EVT_ALIVE);

	RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("rtw_unregister_evt_alive: cpwm:%d alives:%x\n", pwrctrl->cpwm, pwrctrl->alives));
	
	_exit_pwrlock(&pwrctrl->lock);

_func_exit_;

#endif /*CONFIG_PWRCTRL*/
}

#ifdef CONFIG_RESUME_IN_WORKQUEUE
#ifdef CONFIG_USB_HCI
extern int rtw_resume_process(struct usb_interface *pusb_intf);
#endif
static void resume_workitem_callback(struct work_struct *work)
{
	struct pwrctrl_priv *pwrpriv = container_of(work, struct pwrctrl_priv, resume_work);
	_adapter *adapter = container_of(pwrpriv, _adapter, pwrctrlpriv);

	DBG_871X("%s\n",__FUNCTION__);

	#ifdef CONFIG_USB_HCI
	rtw_resume_process(adapter->dvobjpriv.pusbintf);
	#elif defined(CONFIG_PCI_HCI)
	#endif
	
}

void rtw_resume_in_workqueue(struct pwrctrl_priv *pwrpriv)
{
	// accquire system's suspend lock preventing from falliing asleep while resume in workqueue
	rtw_lock_suspend();
	
	#if 1
	queue_work(pwrpriv->rtw_workqueue, &pwrpriv->resume_work);	
	#else
	_set_workitem(&pwrpriv->resume_work);
	#endif
}
#endif //CONFIG_RESUME_IN_WORKQUEUE

#ifdef CONFIG_HAS_EARLYSUSPEND
#ifdef CONFIG_USB_HCI
extern int rtw_resume_process(struct usb_interface *pusb_intf);
#endif
static void rtw_early_suspend(struct early_suspend *h)
{
	struct pwrctrl_priv *pwrpriv = container_of(h, struct pwrctrl_priv, early_suspend);
	DBG_871X("%s\n",__FUNCTION__);
	
	//jeff: do nothing but set do_late_resume to false
	pwrpriv->do_late_resume = _FALSE;
}

static void rtw_late_resume(struct early_suspend *h)
{
	struct pwrctrl_priv *pwrpriv = container_of(h, struct pwrctrl_priv, early_suspend);
	_adapter *adapter = container_of(pwrpriv, _adapter, pwrctrlpriv);

	DBG_871X("%s\n",__FUNCTION__);
	if(pwrpriv->do_late_resume) {
		#ifdef CONFIG_USB_HCI
		rtw_resume_process(adapter->dvobjpriv.pusbintf);
		pwrpriv->do_late_resume = _FALSE;
		#elif defined(CONFIG_PCI_HCI)
		#endif
	}
}

void rtw_register_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	DBG_871X("%s\n", __FUNCTION__);

	//jeff: set the early suspend level before blank screen, so we wll do late resume after scree is lit
	pwrpriv->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 20;
	pwrpriv->early_suspend.suspend = rtw_early_suspend;
	pwrpriv->early_suspend.resume = rtw_late_resume;
	register_early_suspend(&pwrpriv->early_suspend);	

	
}

void rtw_unregister_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	DBG_871X("%s\n", __FUNCTION__);

	pwrpriv->do_late_resume = _FALSE;

	if (pwrpriv->early_suspend.suspend) 
		unregister_early_suspend(&pwrpriv->early_suspend);

	pwrpriv->early_suspend.suspend = NULL;
	pwrpriv->early_suspend.resume = NULL;
}
#endif //CONFIG_HAS_EARLYSUSPEND

#ifdef CONFIG_ANDROID_POWER
#ifdef CONFIG_USB_HCI
extern int rtw_resume_process(struct usb_interface *pusb_intf);
#endif
static void rtw_early_suspend(android_early_suspend_t *h)
{
	struct pwrctrl_priv *pwrpriv = container_of(h, struct pwrctrl_priv, early_suspend);
	DBG_871X("%s\n",__FUNCTION__);
	
	//jeff: do nothing but set do_late_resume to false
	pwrpriv->do_late_resume = _FALSE;
}

static void rtw_late_resume(android_early_suspend_t *h)
{
	struct pwrctrl_priv *pwrpriv = container_of(h, struct pwrctrl_priv, early_suspend);
	_adapter *adapter = container_of(pwrpriv, _adapter, pwrctrlpriv);

	DBG_871X("%s\n",__FUNCTION__);
	if(pwrpriv->do_late_resume) {
		#ifdef CONFIG_USB_HCI
		rtw_resume_process(adapter->dvobjpriv.pusbintf);
		pwrpriv->do_late_resume = _FALSE;
		#elif defined(CONFIG_PCI_HCI)
		#endif
	}
}

void rtw_register_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	DBG_871X("%s\n", __FUNCTION__);

	//jeff: set the early suspend level before blank screen, so we wll do late resume after scree is lit
	pwrpriv->early_suspend.level = ANDROID_EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 20;
	pwrpriv->early_suspend.suspend = rtw_early_suspend;
	pwrpriv->early_suspend.resume = rtw_late_resume;
	android_register_early_suspend(&pwrpriv->early_suspend);	
}

void rtw_unregister_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	DBG_871X("%s\n", __FUNCTION__);

	pwrpriv->do_late_resume = _FALSE;

	if (pwrpriv->early_suspend.suspend) 
		android_unregister_early_suspend(&pwrpriv->early_suspend);

	pwrpriv->early_suspend.suspend = NULL;
	pwrpriv->early_suspend.resume = NULL;
}
#endif //CONFIG_ANDROID_POWER

u8 rtw_interface_ps_func(_adapter *padapter,HAL_INTF_PS_FUNC efunc_id,u8* val)
{
	u8 bResult = _TRUE;
	if(padapter->HalFunc.interface_ps_func)
	{
		bResult = padapter->HalFunc.interface_ps_func(padapter,efunc_id,val);
	}
	return bResult;
}

/*
* rtw_pwr_wakeup - Wake the NIC up from: 1)IPS. 2)USB autosuspend
* @adapter: pointer to _adapter structure
* 
* Return _SUCCESS or _FAIL
*/
int _rtw_pwr_wakeup(_adapter *padapter, const char *caller)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;	
	int ret = _SUCCESS;

	//System suspend is not allowed to wakeup
	if((pwrpriv->bInternalAutoSuspend == _FALSE) && (_TRUE == pwrpriv->bInSuspend )){
		ret = _FAIL;
		goto exit;
	}

	//I think this should be check in IPS, LPS, autosuspend functions...
	//if( pwrpriv->power_mgnt == PS_MODE_ACTIVE ) {
	//	goto exit;
	//}

	//block???
	if((pwrpriv->bInternalAutoSuspend == _TRUE)  && (padapter->net_closed == _TRUE)) {
		ret = _FAIL;
		goto exit;
	}

	//I think this should be check in IPS, LPS, autosuspend functions...
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
	{
		ret = _SUCCESS;
		goto exit;
	}
		
	if(rf_off == pwrpriv->rf_pwrstate )
	{		
#ifdef CONFIG_USB_HCI
#ifdef CONFIG_AUTOSUSPEND
		 if(pwrpriv->brfoffbyhw==_TRUE)
		{
			DBG_8192C("hw still in rf_off state ...........\n");
			ret = _FAIL;
			goto exit;
		}
		else if(padapter->registrypriv.usbss_enable)
		{
			DBG_8192C("\n %s call autoresume_enter....\n",__FUNCTION__);	
			if(_FAIL ==  autoresume_enter(padapter))
			{
				DBG_8192C("======> autoresume fail.............\n");
				ret = _FAIL;
				goto exit;
			}	
		}
		else
#endif
#endif
		{
#ifdef CONFIG_IPS
			DBG_8192C("\n %s call ips_leave....\n",__FUNCTION__);				
			if(_FAIL ==  ips_leave(padapter))
			{
				DBG_8192C("======> ips_leave fail.............\n");
				ret = _FAIL;
				goto exit;
			}
#endif
		}
	}else {
		//Jeff: reset timer to avoid falling ips or selective suspend soon
		if(pwrpriv->bips_processing == _FALSE)
			rtw_set_pwr_state_check_timer(pwrpriv);
	}

	//TODO: the following checking need to be merged...
	if(padapter->bDriverStopped
		|| !padapter->bup
		|| !padapter->hw_init_completed
	){
		DBG_8192C("%s: bDriverStopped=%d, bup=%d, hw_init_completed=%u\n"
			, caller
		   	, padapter->bDriverStopped
		   	, padapter->bup
		   	, padapter->hw_init_completed);
		ret= _FALSE;
		goto exit;
	}

exit:
	return ret;

}


