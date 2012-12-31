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
#define _RTL8712_CMD_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <cmd_osdep.h>
#include <mlme_osdep.h>
#include <rtl871x_byteorder.h>
#include <circ_buf.h>
#include <rtl871x_ioctl_set.h>


#ifdef PLATFORM_LINUX
#ifdef CONFIG_SDIO_HCI
#include <linux/mmc/sdio_func.h>
#endif
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


#ifdef CONFIG_MLME_EXT
void mlme_cmd_hdl(_adapter *padapter, struct cmd_obj *pcmd)
{
	u8 ret;	
	u8 (*cmd_hdl)(_adapter *padapter, u8* pbuf);
	void (*pcmd_callback)(_adapter *padapter, struct cmd_obj *pcmd);
	struct	cmd_priv	*pcmdpriv = &(padapter->cmdpriv);
	u8 *pcmdbuf = pcmdpriv->cmd_buf;
	
	if(pcmd)
	{
		pcmdpriv->cmd_issued_cnt++;

		pcmd->cmdsz = _RND4((pcmd->cmdsz));//_RND4

		_memcpy(pcmdbuf, pcmd->parmbuf, pcmd->cmdsz);

		cmd_hdl = wlancmds[pcmd->cmdcode].h2cfuns;
		if(cmd_hdl)
		{
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("mlme_cmd_hdl(): cmd_hdl=0x%p, cmdcode=0x%x\n", cmd_hdl, pcmd->cmdcode));
			ret = cmd_hdl(padapter, pcmdbuf);
			pcmd->res = ret;
		}
		
		//pcmdpriv->cmd_seq++;//skip

		// invoke cmd->callback function		
		pcmd_callback = cmd_callback[pcmd->cmdcode].callback;
		if(pcmd_callback==NULL)
		{
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("mlme_cmd_hdl(): pcmd_callback=0x%p, cmdcode=0x%x\n", pcmd_callback, pcmd->cmdcode));
			free_cmd_obj(pcmd);
		}	
		else
		{
			//need conider that free cmd_obj in cmd_callback
			pcmd_callback(padapter, pcmd);
		}	
		
	}

}

void start_hw_event_posting(_adapter *padapter)
{	
	struct	event_node *node;	
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	volatile int *head = &(pmlmeext->c2hevent.head);
	volatile int *tail = &(pmlmeext->c2hevent.tail);
	
	
	if (*head == *tail)
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,(" hw event error! head:%d, tail:%d\n", *head, *tail));
		return;	
	}

	node = &(pmlmeext->c2hevent.nodes[*tail]);

	//assign event code, size...
	
	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("tail:%d, evt_code:%d, evt_sz:%d\n", *tail, node->evt_code, node->evt_sz)); 
	
	pmlmeext->c2h_res = ((node->evt_code << 24) | 
	 					((node->evt_sz) << 8) | pmlmeext->c2hevent.seq++);

	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("evt_sz = %d, val=%x\n", node->evt_sz, pmlmeext->c2h_res));
	
	pmlmeext->c2h_buf = node->node;
	
	evt_notify_isr(&padapter->evtpriv);

		
}

int event_queuing (_adapter *padapter, struct event_node *evtnode)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	volatile int *head = &pmlmeext->c2hevent.head;
	volatile int *tail = &pmlmeext->c2hevent.tail;
	int res = SUCCESS, hwen = 0;		

	if (CIRC_SPACE(*head, *tail, C2HEVENT_SZ))
	{		
		_memcpy(&(pmlmeext->c2hevent.nodes[*head]), evtnode, sizeof (struct event_node));	
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("event code: %x\n", evtnode->evt_code));
		
		if (*head == *tail)
			hwen = 1;
		
		*head = (*head + 1) & (C2HEVENT_SZ - 1);
		
	}
	else
		res = FAIL;

	
	if (hwen) {
		start_hw_event_posting(padapter);
	}
	
	return res;
	
}

void event_complete(_adapter *padapter)
{
	struct event_node *node;
	volatile int	*caller_ff_tail;
	int	caller_ff_sz;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	volatile int *head = &(pmlmeext->c2hevent.head);	
	volatile int *tail = &(pmlmeext->c2hevent.tail);

	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("+event_complete\n"));

	node = &(pmlmeext->c2hevent.nodes[*tail]);

	if (CIRC_CNT(*head, *tail, C2HEVENT_SZ) == 0) {
		return;
	}

	caller_ff_tail = node->caller_ff_tail;
	caller_ff_sz = node->caller_ff_sz;

	if (caller_ff_tail)
		*caller_ff_tail = ((*caller_ff_tail) + 1) & (caller_ff_sz - 1);
	
	*tail = ((*tail) + 1) & (C2HEVENT_SZ - 1);

	if (CIRC_CNT(*head, *tail, C2HEVENT_SZ) == 0) {
		return;
	}

	start_hw_event_posting(padapter);		

}

thread_return event_thread(thread_context context)
{
	unsigned int val, r_sz, ec;
	void (*event_callback)(_adapter *dev, u8 *pbuf);
	u8 	evt_seq, *peventbuf = NULL;
	_adapter * padapter = (_adapter *)context;
	struct evt_priv *pevt_priv = &(padapter->evtpriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

_func_enter_;
	
#ifdef PLATFORM_LINUX	
	daemonize("%s", padapter->pnetdev->name);
	allow_signal(SIGTERM);
#endif	

	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("@@@@@@@ start r871x event_thread @@@@@@@\n"));

	evt_seq = 0;

	while(1)
	{		
		if ((_down_sema(&(pevt_priv->evt_notify))) == _FAIL)
			break;
		
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE)){
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("event_thread:bDriverStopped or bSurpriseRemoved"));
			break;
		}
					
		val = pmlmeext->c2h_res;

		RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_, ("event_thread, c2h_res=%x, event_seq=%d\n", val, pevt_priv->event_seq));
		
		r_sz = (val >> 8) & 0xffff;
		ec = (val >> 24);
		
		// checking event sequence...		
		if ((val & 0x7f) != evt_seq/*pevt_priv->event_seq*/)
		{
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("Evetn Seq Error! %d vs %d\n", (val & 0xff), pevt_priv->event_seq));

			//pevt_priv->event_seq = ((val+1)&0x7f); 
			evt_seq = ((val+1)&0x7f); 	
			
			goto _abort_event_;
		}

		// checking if event code is valid
		if (ec >= MAX_C2HEVT)
		{
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("Event Code(%d) mismatch!\n", (val >> 24)));
			
			evt_seq = ((val+1)&0x7f); 	
			
			goto _abort_event_;
		}

		// checking if event size match the event parm size	
		if ((wlanevents[ec].parmsize != 0) && 
			(wlanevents[ec].parmsize != r_sz))
		{
			
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("Event(%d) Parm Size mismatch (%d vs %d)!\n", 
							ec, wlanevents[ec].parmsize, r_sz));
			
			evt_seq = ((val+1)&0x7f); 	
			
			goto _abort_event_;	
			
		}
		
		//update event sequence
	        evt_seq++;//update evt_seq
	        if(evt_seq > 127)
		        evt_seq=0;
		
		peventbuf = pevt_priv->evt_buf;

		if (peventbuf == NULL)
		{
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nCan't allocate memory buf for event code:%d, len:%d\n", (val >> 24), r_sz));
			goto _abort_event_;
		}
		
		_memcpy(peventbuf, pmlmeext->c2h_buf, r_sz);

		if(ec != GEN_EVT_CODE(_Survey))
		{
			_mfree(pmlmeext->c2h_buf, r_sz);
		}	

		if (peventbuf)
		{
			event_callback = wlanevents[ec].event_callback;
			if(event_callback)
			event_callback(padapter, peventbuf);
		}
		
_abort_event_:
	
		event_complete(padapter);
			
		peventbuf = NULL;
	
#ifdef PLATFORM_LINUX
		if (signal_pending (current)) {
			flush_signals(current);
        	}
#endif       			
		

	}

	_up_sema(&pevt_priv->terminate_evtthread_sema);
	
_func_exit_;	

	thread_exit();
	
}
#endif //end of CONFIG_MLME_EXT

static void check_hw_pbc(_adapter *padapter)
{
	u8	tmp1byte;
 
	write8(padapter, MAC_PINMUX_CTRL, (GPIOMUX_EN | GPIOSEL_GPIO)); 

	tmp1byte = read8(padapter, GPIO_IO_SEL);

	tmp1byte &= ~(HAL_8192S_HW_GPIO_WPS_BIT);

	write8(padapter, GPIO_IO_SEL, tmp1byte);

	tmp1byte = read8(padapter, GPIO_CTRL); 

	//printk("CheckPbcGPIO - Ox%x\n", tmp1byte);
 
	// Add by hpfan 2008.07.07 to fix read GPIO error from S3
	if (tmp1byte == 0xff)
		return ;
 
	if (tmp1byte&HAL_8192S_HW_GPIO_WPS_BIT)
	{
		// Here we only set bPbcPressed to true
		// After trigger PBC, the variable will be set to false

		DBG_8712("CheckPbcGPIO - PBC is pressed !!!!\n");

		//priv->bpbc_pressed = true;
#ifdef RTK_DMP_PLATFORM
		kobject_hotplug(&padapter->pnetdev->class_dev.kobj, KOBJ_NET_PBC);
		//kobject_hotplug(&dev->class_dev.kobj, KOBJ_NET_PBC);
#else
		if ( padapter->pid == 0 )
		{	//	0 is the default value and it means the application monitors the HW PBC doesn't privde its pid to driver.
			return;
		}

#ifdef PLATFORM_LINUX

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
		kill_pid(find_vpid(padapter->pid), SIGUSR1, 1);
#endif

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
		kill_proc(padapter->pid, SIGUSR1, 1);
#endif

#endif

#endif
	}
}


// query rx phy status from fw.
// Adhoc mode: beacon.
// Infrastructure mode: beacon , data.
// added by thomas 2010-02-09
static void query_fw_rx_phy_status(_adapter *padapter)
{
	u32	val32 =0;
	int pollingcnts = 50;

	if( check_fwstate(&padapter->mlmepriv, _FW_LINKED) == _TRUE) {
		//write value 0xf4000001 to IOCMD_CTRL
		write32(padapter, IOCMD_CTRL_REG, 0xf4000001);

		usleep_os(100);

		//Wait FW complete IO Cmd
		while( (0 != read32(padapter, IOCMD_CTRL_REG) ) && (pollingcnts>0) ){
			pollingcnts--;
			usleep_os(10);
		}

		if( pollingcnts != 0){	
			val32 = read32(padapter, IOCMD_DATA_REG);
		}
		else{//time out	
			val32 = 0;
			//DBG_8712 ("fw_iocmd_read timeout ........\n");
		}

		val32 = val32 >> 4;
		padapter->recvpriv.fw_rssi = (u8)signal_scale_mapping(val32);
		//printk("Get FW SignalStrength %d \n",padapter->recvpriv.fw_rssi);
	}
}


// check mlme, hw, phy, or dynamic algorithm status.
// added by thomas 2010-02-09
static void StatusWatchdogCallback(_adapter *padapter)
{
	check_hw_pbc(padapter);
	query_fw_rx_phy_status(padapter);
}


static void r871x_internal_cmd_hdl(_adapter *padapter, u8 *pbuf)
{
	struct drvint_cmd_parm *pdrvcmd;

	if(!pbuf)
		return;

	pdrvcmd = (struct drvint_cmd_parm*)pbuf;
	
	switch(pdrvcmd->i_cid)
	{
		case WDG_WK_CID:
			StatusWatchdogCallback(padapter);
			break;
		default:
			break;

	}

	if(pdrvcmd->pbuf)
	{
		_mfree(pdrvcmd->pbuf, pdrvcmd->sz);
	}

	
}

u8 read_macreg_hdl(_adapter *padapter, u8 *pbuf)
{	
	void (*pcmd_callback)(_adapter *dev, struct cmd_obj	*pcmd);
	struct cmd_obj *pcmd  = (struct cmd_obj *)pbuf;
	//struct reg_rw_parm *pcmd_parm = (struct reg_rw_parm *)pbuf;


	// invoke cmd->callback function		
	pcmd_callback = cmd_callback[pcmd->cmdcode].callback;
	if(pcmd_callback==NULL)
	{
		free_cmd_obj(pcmd);
	}	
	else
	{
		//need conider that free cmd_obj in cmd_callback
		pcmd_callback(padapter, pcmd);
	}	

	
	return H2C_SUCCESS;
	
}

u8 write_macreg_hdl(_adapter *padapter, u8 *pbuf)
{	
	void (*pcmd_callback)(_adapter *dev, struct cmd_obj	*pcmd);
	struct cmd_obj *pcmd  = (struct cmd_obj *)pbuf;
	//struct reg_rw_parm *pcmd_parm = (struct reg_rw_parm *)pbuf;


	// invoke cmd->callback function		
	pcmd_callback = cmd_callback[pcmd->cmdcode].callback;
	if(pcmd_callback==NULL)
	{
		free_cmd_obj(pcmd);
	}	
	else
	{
		//need conider that free cmd_obj in cmd_callback
		pcmd_callback(padapter, pcmd);
	}	
	
	return H2C_SUCCESS;
}

u8 read_bbreg_hdl(_adapter *padapter, u8 *pbuf)
{
	u32 val;
	void (*pcmd_callback)(_adapter *dev, struct cmd_obj	*pcmd);
	//struct reg_rw_parm *pcmd_parm;
	struct readBB_parm *prdbbparm;
	struct cmd_obj *pcmd  = (struct cmd_obj *)pbuf;
	

	prdbbparm = (struct readBB_parm *)pcmd->parmbuf;

	//val = QueryBBReg(padapter, prdbbparm->offset, 0xffffffff);

	if(pcmd->rsp && pcmd->rspsz>0)
	{
		_memcpy(pcmd->rsp, (u8*)&val, pcmd->rspsz);
	}

	// invoke cmd->callback function		
	pcmd_callback = cmd_callback[pcmd->cmdcode].callback;
	if(pcmd_callback==NULL)
	{
		free_cmd_obj(pcmd);
	}	
	else
	{
		//need conider that free cmd_obj in cmd_callback
		pcmd_callback(padapter, pcmd);
	}	

	return H2C_SUCCESS;
	
}


u8 write_bbreg_hdl(_adapter *padapter, u8 *pbuf)
{	
	void (*pcmd_callback)(_adapter *dev, struct cmd_obj	*pcmd);
	//struct reg_rw_parm *pcmd_parm;
	struct writeBB_parm	*pwritebbparm;
	struct cmd_obj *pcmd  = (struct cmd_obj *)pbuf;

	//pcmd_parm = (struct reg_rw_parm *)pcmd->parmbuf;
	pwritebbparm = (struct writeBB_parm *)pcmd->parmbuf;

	//SetBBReg(padapter, pwritebbparm->offset, 0xffffffff, pwritebbparm->value);

	// invoke cmd->callback function		
	pcmd_callback = cmd_callback[pcmd->cmdcode].callback;
	if(pcmd_callback==NULL)
	{
		free_cmd_obj(pcmd);
	}	
	else
	{
		//need conider that free cmd_obj in cmd_callback
		pcmd_callback(padapter, pcmd);
	}	

	return H2C_SUCCESS;
	
}

u8 read_rfreg_hdl(_adapter *padapter, u8 *pbuf)
{
	u32 val;
	void (*pcmd_callback)(_adapter *dev, struct cmd_obj	*pcmd);
	//struct reg_rw_parm *pcmd_parm;
	struct readRF_parm *prdrfparm;
	struct cmd_obj *pcmd  = (struct cmd_obj *)pbuf;

	prdrfparm = (struct readRF_parm *)pcmd->parmbuf;

	//val = QueryBBReg(padapter, prdrfparm->offset, 0xffffffff);

	if(pcmd->rsp && pcmd->rspsz>0)
	{
		_memcpy(pcmd->rsp, (u8*)&val, pcmd->rspsz);
	}

	// invoke cmd->callback function		
	pcmd_callback = cmd_callback[pcmd->cmdcode].callback;
	if(pcmd_callback==NULL)
	{
		free_cmd_obj(pcmd);
	}	
	else
	{
		//need conider that free cmd_obj in cmd_callback
		pcmd_callback(padapter, pcmd);
	}	

	return H2C_SUCCESS;
	
}


u8 write_rfreg_hdl(_adapter *padapter, u8 *pbuf)
{	
	void (*pcmd_callback)(_adapter *dev, struct cmd_obj	*pcmd);
	//struct reg_rw_parm *pcmd_parm;
	struct writeRF_parm *pwriterfparm;
	struct cmd_obj *pcmd  = (struct cmd_obj *)pbuf;

	//pcmd_parm = (struct reg_rw_parm *)pcmd->parmbuf;
	pwriterfparm = (struct writeRF_parm *)pcmd->parmbuf;

	//SetRFReg(padapter, pwriterfparm->offset, 0xffffffff, pwriterfparm->value);

	// invoke cmd->callback function		
	pcmd_callback = cmd_callback[pcmd->cmdcode].callback;
	if(pcmd_callback==NULL)
	{
		free_cmd_obj(pcmd);
	}	
	else
	{
		//need conider that free cmd_obj in cmd_callback
		pcmd_callback(padapter, pcmd);
	}	

	return H2C_SUCCESS;
	
}


/*
void drv_cmd_hdl(_adapter *padapter, struct cmd_obj *pcmd)
{	
	u8 ret;	
	u8 (*cmd_hdl)(_adapter *padapter, u8* pbuf);
	void (*pcmd_callback)(_adapter *padapter, struct cmd_obj *pcmd);
	struct	cmd_priv	*pcmdpriv = &(padapter->cmdpriv);
	u8 *pcmdbuf = pcmdpriv->cmd_buf;
	
	if(pcmd)
	{
		pcmdpriv->cmd_issued_cnt++;

		pcmd->cmdsz = _RND4((pcmd->cmdsz));//_RND4

		_memcpy(pcmdbuf, pcmd->parmbuf, pcmd->cmdsz);

		cmd_hdl = wlancmds[pcmd->cmdcode].h2cfuns;
		if(cmd_hdl)
		{
			DBG_8712("mlme_cmd_hdl(): cmd_hdl=0x%p, cmdcode=0x%x\n", cmd_hdl, pcmd->cmdcode);
			ret = cmd_hdl(padapter, pcmdbuf);
			pcmd->res = ret;
		}
		
		//pcmdpriv->cmd_seq++;//skip

		// invoke cmd->callback function		
		pcmd_callback = cmd_callback[pcmd->cmdcode].callback;
		if(pcmd_callback==NULL)
		{
			DBG_8712("mlme_cmd_hdl(): pcmd_callback=0x%p, cmdcode=0x%x\n", pcmd_callback, pcmd->cmdcode);
			free_cmd_obj(pcmd);
		}	
		else
		{
			//need conider that free cmd_obj in cmd_callback
			pcmd_callback(padapter, pcmd);
		}	
		
	}

}
*/

u8 sys_suspend_hdl(_adapter *padapter, u8 *pbuf)
{
	struct cmd_obj *pcmd  = (struct cmd_obj *)pbuf;
	struct usb_suspend_parm *psetusbsuspend;

	psetusbsuspend = (struct usb_suspend_parm *)pcmd->parmbuf;

#ifdef PLATFORM_WINDOWS
	if(psetusbsuspend->action == 1)
	{
		pnp_sleep_wk(padapter);
	}
	else
	{
		pnp_resume_wk(padapter);
	}
#endif
		
	free_cmd_obj(pcmd);	
	
	return H2C_SUCCESS;
	
}

void read_cmd_rsp(_adapter *padapter, u16 rspaddr, uint *prspbuf)
{
	

}


struct cmd_obj *cmd_hdl_filter(_adapter *padapter, struct cmd_obj *pcmd)
{
	struct cmd_obj *pcmd_r;
	
	if(pcmd == NULL)
		return pcmd;

	pcmd_r = NULL;

#ifdef CONFIG_MLME_EXT

	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("cmd_hdl_filter(): cmd_code=%d\n", pcmd->cmdcode));

	switch(pcmd->cmdcode)
	{
		case GEN_CMD_CODE(_Read_MACREG):			
		case GEN_CMD_CODE(_Write_MACREG):
			
			pcmd_r = pcmd;
			
			break;			
			
		case GEN_CMD_CODE(_CreateBss):

			//pcmd->parmbuf = (u8*)&padapter->mlmepriv.cur_network.network;
			//pcmd->cmdsz =  padapter->mlmepriv.cur_network.network.Length;					

			//mlme_cmd_hdl(padapter, pcmd);

			write32(padapter, RCR, read32(padapter, RCR)|0x00040000);//accept data frames;
			DBG_8712("when create_bss, RCR=0x%08x\n", read32(padapter, RCR));
			
			pcmd_r = pcmd;

			break;
			
		default:
			
			mlme_cmd_hdl(padapter, pcmd);

			pcmd_r = NULL;
			
			break;			
	}	

#else

	switch(pcmd->cmdcode)
	{
		case GEN_CMD_CODE(_Read_MACREG):
			read_macreg_hdl(padapter, (u8*)pcmd);
			pcmd_r = pcmd;			
			break;			
		case GEN_CMD_CODE(_Write_MACREG):
			write_macreg_hdl(padapter, (u8*)pcmd);
			pcmd_r = pcmd;
			break;
		case GEN_CMD_CODE(_Read_BBREG):
			read_bbreg_hdl(padapter, (u8*)pcmd);			
			break;	
		case GEN_CMD_CODE(_Write_BBREG):
			write_bbreg_hdl(padapter, (u8*)pcmd);			
			break;
		case GEN_CMD_CODE(_Read_RFREG):
			read_rfreg_hdl(padapter, (u8*)pcmd);			
			break;		
		case GEN_CMD_CODE(_Write_RFREG):
			write_rfreg_hdl(padapter, (u8*)pcmd);	
			break;			
		case GEN_CMD_CODE(_SetUsbSuspend):
			sys_suspend_hdl(padapter, (u8*)pcmd);
			break;
		
		case GEN_CMD_CODE(_JoinBss):

			//reset reg/hw/network setting before join_bss
			joinbss_reset(padapter);
		
#ifdef CONFIG_PWRCTRL
			/* Before set JoinBss_CMD to FW, driver must ensure FW is in PS_MODE_ACTIVE.
			Directly write rpwm to radio on and assign new pwr_mode to Driver, instead of use workitem to change state.*/
			if(padapter->pwrctrlpriv.pwr_mode > PS_MODE_ACTIVE){
				padapter->pwrctrlpriv.pwr_mode = PS_MODE_ACTIVE;
				_enter_pwrlock(&(padapter->pwrctrlpriv.lock));
				set_rpwm(padapter, PS_STATE_S4);
				_exit_pwrlock(&(padapter->pwrctrlpriv.lock));
			}
#endif
			pcmd_r = pcmd;

			break;
		case _DRV_INT_CMD_:		
			r871x_internal_cmd_hdl(padapter, pcmd->parmbuf);
			free_cmd_obj(pcmd);
			pcmd_r = NULL;
			break;
		default:				
		        pcmd_r = pcmd;
		break;			
		
	}	

#endif

	return pcmd_r;//if returning the pcmd_r == NULL, you must confirm the pcmd has been free.
	
}
u8 check_cmd_fifo(_adapter *padapter,uint sz){
	u8 res=_SUCCESS;
#ifdef CONFIG_SDIO_HCI
	uint public_pg=0;
	uint cmd_pg=0;
	public_pg=read8(padapter, SDIO_BCNQ_FREEPG);
	cmd_pg=read8(padapter, SDIO_CMDQ_FREEPG);
	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("check_cmd_fifo, public_pg=%x  cmd_pg=%x\n",public_pg,cmd_pg));
	cmd_pg=cmd_pg-public_pg;
	res=_FAIL;
	if((cmd_pg >2)||(public_pg >5)){
		if((public_pg+cmd_pg)< (sz>>8)){
	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("check_cmd_fifo, public_pg=0x%x  cmd_pg=0x%x  sz=0x%x\n",public_pg,cmd_pg,sz));
		res=_FAIL;
		}
		else
			res=_SUCCESS;
	}else{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("check_cmd_fifo, public_pg=%x  cmd_pg=%d   cmd pg is not enough\n",public_pg,cmd_pg));
		res= _FAIL;
	}
#endif	
	return res;
}

u8 fw_cmd(PADAPTER pAdapter, u32 cmd)
{
	int pollingcnts = 50;

	write32(pAdapter, IOCMD_CTRL_REG, cmd);
	usleep_os(100);
	while ((0 != read32(pAdapter, IOCMD_CTRL_REG)) && (pollingcnts > 0)) {
		pollingcnts--;
		usleep_os(10);
	}

	if (pollingcnts == 0) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("!!fw_cmd timeout ........\n"));
		return _FALSE;
	}

	return _TRUE;
}

void fw_cmd_data(PADAPTER pAdapter, u32 *value, u8 flag)
{
	if (flag == 0)	// set
		write32(pAdapter, IOCMD_DATA_REG, *value);
	else		// query
		*value = read32(pAdapter, IOCMD_DATA_REG);
}

thread_return cmd_thread(thread_context context)
{
	struct cmd_obj *pcmd;
	unsigned int cmdsz, wr_sz, *pcmdbuf, *prspbuf;
	struct tx_desc *pdesc;
	void (*pcmd_callback)(_adapter *dev, struct cmd_obj	*pcmd);
       _adapter *padapter = (_adapter *)context;
	struct	cmd_priv	*pcmdpriv = &(padapter->cmdpriv);
	
_func_enter_;

	thread_enter(padapter);

	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("start r8712 cmd_thread !!!!\n"));

	while(1)
	{
		if ((_down_sema(&(pcmdpriv->cmd_queue_sema))) == _FAIL)
			break;

		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE))
		{			
			RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("cmd_thread:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));		
			break;
		}
		
		if (register_cmd_alive(padapter) != _SUCCESS)
		{
			continue;
		}
_next:
		if(!(pcmd = dequeue_cmd(&(pcmdpriv->cmd_queue)))) {
			unregister_cmd_alive(padapter);
			continue;
		}

		pcmdbuf = (unsigned int*)pcmdpriv->cmd_buf;
		prspbuf = (unsigned int*)pcmdpriv->rsp_buf;

		pdesc = (struct tx_desc *)pcmdbuf;

		_memset(pdesc, 0, TXDESC_SIZE);

		RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("after dequeue_cmd\n"));

		pcmd = cmd_hdl_filter(padapter, pcmd);
	
		if(pcmd)//if pcmd != NULL, the cmd will be handled by f/w
		{
                        struct dvobj_priv    *pdvobj = (struct dvobj_priv   *)&padapter->dvobjpriv;
			u8                           blnPending = 0;

		 	pcmdpriv->cmd_issued_cnt++;
			cmdsz = _RND8( (pcmd->cmdsz));//_RND8

			wr_sz = TXDESC_SIZE + 8 + cmdsz;		 	
			
			if ( pdvobj->ishighspeed )
			{
				if ( ( wr_sz % 512 ) == 0 )
				{
					blnPending = 1;
				}
			}
			else
			{
				if ( ( wr_sz % 64 ) == 0 )
				{
					blnPending = 1;
				}
			}
			
			pdesc->txdw0 |= cpu_to_le32((wr_sz-TXDESC_SIZE)&0x0000ffff);

			if ( blnPending )
			{
				pdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE+OFFSET_SZ+8)<<OFFSET_SHT)&0x00ff0000);//32 bytes for TX Desc with 8 bytes offset
			}
			else
			{
				pdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE+OFFSET_SZ)<<OFFSET_SHT)&0x00ff0000);//default = 32 bytes for TX Desc
			}

			pdesc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);			
			pdesc->txdw1 |= cpu_to_le32((0x13<<QSEL_SHT)&0x00001f00);//QSEL=H2C-Command

			
			pcmdbuf += (TXDESC_SIZE>>2);		
			*pcmdbuf = cpu_to_le32((cmdsz&0x0000ffff) | (pcmd->cmdcode << 16) | (pcmdpriv->cmd_seq <<24));
			
			pcmdbuf += 2 ;//8 bytes aligment			
			_memcpy((u8*)pcmdbuf, pcmd->parmbuf, pcmd->cmdsz);

			while (check_cmd_fifo(padapter,wr_sz)==_FAIL){
				RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("cmd pg is not enough=>sleep 10 ms\n"));
				if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE))
				{			
					RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("cmd_thread:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));		
					break;
				}
				msleep_os(100);
				continue;
			}
			//write_mem(padapter, RTL8712_DMA_H2CCMD,  pcmd->cmdsz+4, pcmdpriv->cmd_buf);
			if ( blnPending )
			{
				wr_sz += 8;   // Appending 8 bytes to the payload.
			}
			
			write_mem(padapter, RTL8712_DMA_H2CCMD, wr_sz, (u8*)pdesc);
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("after dump cmd, code=%d\n", pcmd->cmdcode));
			
			pcmdpriv->cmd_seq++;

			if(pcmd->cmdcode == GEN_CMD_CODE(_CreateBss))
			{			
				pcmd->res = H2C_SUCCESS;

				pcmd_callback = cmd_callback[pcmd->cmdcode].callback;
				if(pcmd_callback)
				{
					pcmd_callback(padapter, pcmd);
				}
				
				continue;				
			}
#ifdef CONFIG_PWRCTRL
#ifdef CONFIG_USB_HCI
			if(pcmd->cmdcode == GEN_CMD_CODE(_SetPwrMode))
			{
				if(padapter->pwrctrlpriv.bSleep)
				{				
					_enter_pwrlock(&(padapter->pwrctrlpriv.lock));
					set_rpwm(padapter, PS_STATE_S2);
					_exit_pwrlock(&(padapter->pwrctrlpriv.lock));
				}
			}
#endif
#endif
			free_cmd_obj(pcmd);

			if(_queue_empty(&(pcmdpriv->cmd_queue))){
				unregister_cmd_alive(padapter);
				continue;
			}
			else{
				//DbgPrint("cmd queue is not empty\n");
				goto _next;
			}
		}
		else
		{
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("\nShall not be empty when dequeue cmd_queuu\n"));
			goto _next;
		}	
		
		flush_signals_thread();
	}

	// free all cmd_obj resources
	do{

		pcmd = dequeue_cmd(&(pcmdpriv->cmd_queue));
		if(pcmd==NULL)
			break;

		free_cmd_obj(pcmd);
		
	}while(1);


	_up_sema(&pcmdpriv->terminate_cmdthread_sema);

_func_exit_;	

	thread_exit();

}

void event_handle(_adapter *padapter, uint *peventbuf)
{
	u8 evt_code, evt_seq;
	u16 evt_sz;		
	void (*event_callback)(_adapter *dev, u8 *pbuf);	
	struct	evt_priv	*pevt_priv = &(padapter->evtpriv);	

	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("+event_handle\n"));

	if(peventbuf == NULL)
	{		
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("event_handle(): peventbuf is NULL !\n"));
		goto _abort_event_;
	}
	
	evt_sz = (u16)(le32_to_cpu(*peventbuf)&0xffff);
	evt_seq = (u8)((le32_to_cpu(*peventbuf)>>24)&0x7f);
	evt_code = (u8)((le32_to_cpu(*peventbuf)>>16)&0xff);
	
	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("event_handle(): evt_sz=%d, evt_seq=%d, evt_code=%d\n", evt_sz, evt_seq, evt_code));

	// checking event sequence...		
	if ((evt_seq & 0x7f) != pevt_priv->event_seq)
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("Event Seq Error! %d vs %d\n", (evt_seq & 0xff), pevt_priv->event_seq));
              pevt_priv->event_seq = ((evt_seq+1)&0x7f);
		goto _abort_event_;
	}

	// checking if event code is valid
	if (evt_code >= MAX_C2HEVT)
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("Event Code(%d) mismatch!\n", evt_code));
		pevt_priv->event_seq = ((evt_seq+1)&0x7f);
		goto _abort_event_;
	}
	else if((evt_code == GEN_EVT_CODE(_Survey)) && (evt_sz > sizeof(WLAN_BSSID_EX)))
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("_Survey Event SZ too big:%d\n", evt_sz));
		pevt_priv->event_seq = ((evt_seq+1)&0x7f);
		goto _abort_event_;
	}
		

	// checking if event size match the event parm size	
	if ((wlanevents[evt_code].parmsize != 0) && (wlanevents[evt_code].parmsize != evt_sz))
	{			
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("Event(%d) Parm Size mismatch (%d vs %d)!\n", 
					evt_code, wlanevents[evt_code].parmsize, evt_sz));
		pevt_priv->event_seq = ((evt_seq+1)&0x7f);
		goto _abort_event_;	
			
	}
	else if( (evt_sz==0) && (evt_code != GEN_EVT_CODE(_WPS_PBC)))
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("return fail : evt_code=%d, evt_sz=%d\n", evt_code, evt_sz));
		pevt_priv->event_seq = ((evt_seq+1)&0x7f);
		goto _abort_event_;	
	}

	pevt_priv->event_seq++;	//update evt_seq
	if(pevt_priv->event_seq >127)
		pevt_priv->event_seq=0;

	peventbuf = peventbuf+2;//move to event content, fw asks 8 bytes aligment				

	if(peventbuf)
	{
		event_callback = wlanevents[evt_code].event_callback;
		if(event_callback)
		{
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("before event_callback\n"));
			event_callback(padapter, (u8*)peventbuf);				
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("\n After event_callback:evt code=%d\n",evt_code));
		}
	}

	pevt_priv->evt_done_cnt++;

_abort_event_:

	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("-event_handle\n"));

	return;

}


void fwdbg_event_callback(_adapter *adapter , u8 *pbuf)
{
	if(pbuf)
	{	
		pbuf[60]='\0';
		if ( strlen( pbuf ) < 3 )
			return;
		
#ifdef PLATFORM_LINUX
                printk("fwdbg:%s\n", pbuf);      
#endif

                RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("fwdbg:%s\n", pbuf));
	}	

}


void dummy_event_callback(_adapter *adapter , u8 *pbuf)
{

	//MSG_8712("+dummy_event_callback\n");
	
	
	//read_macreg_cmd(adapter, 0xffffffff, (u8*)&val32);	

	

}

u8 read_macreg_cmd(_adapter  *padapter, u32 offset, u8 *pval)
{

struct reg_rw_parm
{
	unsigned short	rw;	//0: read, 1: write
	unsigned short	size;	//8, 16, 32 supported
	unsigned int	addr;
	unsigned int	value;
	unsigned int	rsvd;
};
	
	struct cmd_obj*			ph2c;
	struct reg_rw_parm 		*pcmd_parm;
	struct cmd_priv 			*pcmdpriv=&padapter->cmdpriv;
	u8	res=_SUCCESS;
	
_func_enter_;

	ph2c = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));	
	if(ph2c==NULL)
	{
		res=_FAIL;
		goto exit;
	}
	
	pcmd_parm = (struct reg_rw_parm*)_malloc(sizeof(struct reg_rw_parm)); 
	if(pcmd_parm ==NULL)
	{
		_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		return _FAIL;
	}

	_init_listhead(&ph2c->list);
	
	ph2c->cmdcode = GEN_CMD_CODE(_Read_MACREG);
	ph2c->parmbuf = (unsigned char *)pcmd_parm;
	ph2c->cmdsz =  sizeof(struct reg_rw_parm);
	ph2c->rsp = NULL;
	ph2c->rspsz = 0;
	
	pcmd_parm ->addr = offset;
	
	enqueue_cmd(pcmdpriv, ph2c);	
	
exit:
	
_func_exit_;	

	return res;
	
}

#ifdef CONFIG_EVENT_THREAD_MODE
#ifndef CONFIG_MLME_EXT
thread_return event_thread(thread_context context)
{
	u8 evt_code, evt_seq;
	u16 evt_sz;		
	void (*event_callback)(_adapter *dev, u8 *pbuf);
	struct evt_obj *pevtobj;
	uint 	*peventbuf;
	_adapter * padapter = (_adapter *)context;
	struct	evt_priv	*pevt_priv = &(padapter->evtpriv);

_func_enter_;

	
#ifdef PLATFORM_LINUX	
	daemonize("%s", padapter->pnetdev->name);
	allow_signal(SIGTERM);
#endif	

		
	RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("@@@@@@@ start r8712 event_thread @@@@@@@\n"));

	while(1)
	{
		
		if ((_down_sema(&(pevt_priv->evt_notify))) == _FAIL)
			break;

_next_event:

		pevtobj = NULL;
		peventbuf = NULL;		
	
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE)){		
			RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("event_thread:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));		
			break;
		}


		if(!(pevtobj = dequeue_evt(&(pevt_priv->evt_queue)))) {		
			continue;
		}


		peventbuf = (uint*)pevtobj->parmbuf;
		evt_sz = (u16)(*peventbuf&0xffff);
		evt_seq = (u8)((*peventbuf>>24)&0x7f);
		evt_code = (u8)((*peventbuf>>16)&0xff);

		peventbuf++;
		
		
		// checking event sequence...		
		if ((evt_seq & 0xff) == pevt_priv->event_seq)
		{
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("Evetn Seq Error! %d vs %d\n", (evt_seq & 0xff), pevt_priv->event_seq));
			goto _abort_event_;
		}

		// checking if event code is valid
		if (evt_code >= MAX_C2HEVT)
		{
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nEvent Code(%d) mismatch!\n", evt_code));
			goto _abort_event_;
		}

		// checking if event size match the event parm size	
		if ((wlanevents[evt_code].parmsize != 0) && 
			(wlanevents[evt_code].parmsize != evt_sz))
		{
			
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nEvent(%d) Parm Size mismatch (%d vs %d)!\n", 
			evt_code, wlanevents[evt_code].parmsize, evt_sz));
			goto _abort_event_;	
			
		}

	//	pevt_priv->event_seq = (evt_seq & 0xff);	//update evt_seq

		if (peventbuf == NULL)
		{
			RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nCan't allocate memory buf for event code:%d, len:%d\n", evt_code, evt_sz));
			goto _abort_event_;
		}
				
		if(peventbuf)
		{
			event_callback = wlanevents[evt_code].event_callback;
			event_callback(padapter, (u8*)peventbuf);				
		}

		//pevt_priv->evt_done_cnt++;
		
_abort_event_:

#ifdef PLATFORM_LINUX
		if (signal_pending (current)) {
			flush_signals(current);
        	}
#endif       


		if(peventbuf)
			_mfree(pevtobj->parmbuf, evt_sz);

		if(pevtobj)
			_mfree((u8*)pevtobj, sizeof(struct evt_obj));
		

		goto _next_event;
		

	}

#if 0
	// free all evt_obj resources
	do{	
		pevtobj = dequeue_evt(&(pevt_priv->evt_queue));	
		if(pevtobj==NULL)
			break;
		
		peventbuf = (uint*)pevtobj->parmbuf;		
		evt_sz = (u16)(*peventbuf&0xffff);

		if(peventbuf)
			_mfree((u8*)peventbuf, evt_sz);

		_mfree((u8*)pevtobj, sizeof(struct evt_obj));
		
	}while(1);
#endif

	_up_sema(&pevt_priv->terminate_evtthread_sema);
	
_func_exit_;	

	thread_exit();

}
#endif
#endif

#ifdef CONFIG_RECV_BH
#ifdef PLATFORM_LINUX
void recv_event_bh(void *priv)
{	
	u8 evt_code, evt_seq;
	u16 evt_sz;
	struct evt_obj *pevtobj = NULL;
	uint 	*peventbuf = NULL;
	_adapter *padapter = (_adapter*)priv;
	struct evt_priv *pevt_priv = &(padapter->evtpriv);

	//printk("+recv_event_bh\n");

	while(1)
	{	
		if(!(pevtobj = dequeue_evt(&(pevt_priv->evt_queue))))
		{		
			break;
		}

		peventbuf = (uint*)pevtobj->parmbuf;	

		//printk("recv_event_bh(), pevtobj=%p, peventbuf=%p\n", pevtobj, peventbuf);

		if(peventbuf)
		{
			evt_sz = (u16)(le32_to_cpu(*peventbuf)&0xffff);
			evt_seq = (u8)((le32_to_cpu(*peventbuf)>>24)&0x7f);
			evt_code = (u8)((le32_to_cpu(*peventbuf)>>16)&0xff);

			if ((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE))
			{
				event_handle(padapter, peventbuf);							
			}
			else
			{
				printk("recv_event_bh():bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved);
			}

			if(evt_sz<64)
			{
				_mfree((u8*)peventbuf, 64+8);
			}
			else
			{
				_mfree((u8*)peventbuf, evt_sz+8);
			}	

			if(pevtobj)
			{
				_mfree((u8 *)pevtobj, sizeof(struct evt_obj));			
			}
		
		}	
	
	}	
	
}
#endif
#endif


