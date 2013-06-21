/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        newbie Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : standby_key.c
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 15:16
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#include "standby_i.h"

static __standby_key_reg_t  *KeyReg;
static __u32 KeyCtrl, KeyIntc, KeyInts, KeyData0, KeyData1;

//==============================================================================
// QUERRY KEY FOR WAKE UP SYSTEM FROM STANDBY
//==============================================================================


/*
*********************************************************************************************************
*                                     INIT KEY FOR STANDBY
*
* Description: init key for standby.
*
* Arguments  : none
*
* Returns    : EPDK_OK;
*********************************************************************************************************
*/
__s32 standby_key_init(void)
{
    /* set key register base */
    KeyReg = (__standby_key_reg_t *)SW_VA_LRADC_IO_BASE;

    /* backup LRADC registers */
    KeyCtrl = KeyReg->Lradc_Ctrl;
    KeyIntc = KeyReg->Lradc_Intc;
    KeyReg->Lradc_Ctrl = 0;
    standby_mdelay(10);
    KeyReg->Lradc_Ctrl = (0x1<<6)|(0x1<<0);
    KeyReg->Lradc_Intc = (0x1<<1);
    KeyReg->Lradc_Ints = (0x1<<1);

    return 0;
}


/*
*********************************************************************************************************
*                                     EXIT KEY FOR STANDBY
*
* Description: exit key for standby.
*
* Arguments  : none
*
* Returns    : EPDK_OK;
*********************************************************************************************************
*/
__s32 standby_key_exit(void)
{
    KeyReg->Lradc_Ctrl =  KeyCtrl;
    KeyReg->Lradc_Intc =  KeyIntc;
    return 0;
}
/*
*********************************************************************************************************
*                                     QUERY KEY FOR WAKEUP STANDBY
*
* Description: query key for wakeup standby.
*
* Arguments  : none
*
* Returns    : result;
*               EPDK_TRUE,      get a key;
*               EPDK_FALSE,     no key;
*********************************************************************************************************
*/
__s32 standby_query_key(void)
{
    if(KeyReg->Lradc_Ints & 0x2)
    {
        KeyReg->Lradc_Ints = 0x2;
        return 0;
    }
    return -1;
}

