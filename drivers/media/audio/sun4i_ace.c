/*
**************************************************************************************************************
*											         eLDK
*						            the Easy Portable/Player Develop Kits
*									           desktop system 
*
*						        	 (c) Copyright 2009-2012, ,HUANGXIN China
*											 All Rights Reserved
*
* File    	: sun4i_ace.c
* By      	: HUANGXIN
* Func		: 
* Version	: v1.0
* ============================================================================================================
* 2011-6-2 16:10:39  HUANGXIN create this file, implements the fundemental interface;
**************************************************************************************************************
*/
#include "sun4i_ace_i.h"
#include <linux/module.h> 
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/io.h>

static __s32 ACE_EnableModule(__ace_module_type_e module, __u32 mode);


struct clk *hAceMClk, *hDramAceClk, *hAceAhbClk;
void *       ace_hsram;
EXPORT_SYMBOL(ace_hsram);

__s32       configTimes = 0;

#define ACE_REGS_BASE ACE_REGS_pBASE
struct semaphore		pSemAceClkAdjust; 						
struct semaphore		pSemAceCe;                              
struct semaphore		pSemAceConfig;      


struct clk *parent_clk = NULL;
int esCLK_SetMclkSrc(struct clk *clk, int parent)
{
	int ret = 0;

	if(clk == NULL){
		printk(" %s %d The clk is NULL\n ", __FUNCTION__, __LINE__);
		return ACE_FAIL;
	}

	if(parent == CSP_CCM_SYS_CLK_SDRAM_PLL){
		parent_clk = clk_get(NULL, "sdram_pll");
		if(parent_clk == NULL){
			printk("%s %d : get sdram pll fail \n", __FUNCTION__, __LINE__);
			return ACE_FAIL;
		}
		ret = clk_set_parent(clk, parent_clk);
	}

	return ret;
}

int esCLK_CloseMclkSrc(int parent)
{
	struct clk *parent_clk = NULL;

	if(parent == CSP_CCM_SYS_CLK_SDRAM_PLL){
		if (parent_clk) {
			printk("release parent clk\n");
			clk_put(parent_clk);
			parent_clk = NULL;
		}
	}

	return 0;
}

int esCLK_MclkOnOff(struct clk *clk, int onoff){	
	int ret = 1;

	if(onoff == 1){
		ret = clk_enable(clk);
		if(ret == -1){
			printk("%s %d : Enable clk fail\n", __FUNCTION__, __LINE__);
		}
	}
	else if(onoff == 0){
		clk_disable(clk);
		ret = 1;
	}
	
	return ret;
}
void *esMEM_SramReqBlk(int addr){	
	void *map_addr = NULL;

	addr |= 0xf0000000;
	map_addr = (void *)addr;
	
	return map_addr;
}

/*
*********************************************************************************************************
*                                   ACE INIT
*
* Description: initialise ACE moudle, create the manager for resource management.
*
* Arguments  : none
*
* Returns    : result;
*               ACE_OK   - init ACE successed;
*               ACE_FAIL - init ACE failed;
*
* Note       : This funciton just create manager, without hardware operation;
*********************************************************************************************************
*/
__s32 ACE_Init(void)
{
    //create semphore to sync clock adjust.    
 	sema_init(&pSemAceClkAdjust, 1);
	
	//ACE_REGS_BASE = ACE_PBASE_ADDR;
	/*ae,ce的相关寄存器都需要通过这个地址值映射偏移找到*/
	ace_hsram = ioremap(ACE_REGS_pBASE, 4096);
    if (!ace_hsram){
        printk("cannot map region for sram");
       return -1;
    }
    //create sem to sync ace config
    
    sema_init(&pSemAceConfig, 1);
       

    //create semphore to sync resource usage
    sema_init(&pSemAceCe, 1);
       
    configTimes = 0;
    return ACE_OK;
}
EXPORT_SYMBOL_GPL(ACE_Init);

/*
*********************************************************************************************************
*                                   ACE EXIT
*
* Description: exit ACE module, destroy resource manager;
*
* Arguments  : none
*
* Returns    : result;
*               ACE_OK   - exit ace module successed;
*               ACE_FAIL - exit ace module failed;
*
* Note       :
*********************************************************************************************************
*/
__s32 ACE_Exit(void)
{        
    iounmap((void *)ace_hsram);
    return ACE_OK;
}
EXPORT_SYMBOL_GPL(ACE_Exit);

/*
*********************************************************************************************************
*                                   REQUEST HARDWARE RESOURCE
*
* Description: require hardware resource.
*
* Arguments  : module   the hardware module which need be requested;
*              mode     mode of hardware module requested;
                            ACE_REQUEST_MODE_WAIT   - request hw resource with waiting mode;
                            ACE_REQUEST_MODE_NOWAIT - request hw resource with no-wait mode;
*              timeout  limitation of time out, just used under ACE_REQUEST_MODE_WAIT mode;
*
* Returns    : handle of hardware resource, NULL means request hw-resource failed;
*
* Note       :
*********************************************************************************************************
*/
s32 ACE_HwReq(__ace_module_type_e module, __ace_request_mode_e mode, __u32 timeout)
{
    if(ACE_MODULE_CE == module || ACE_MODULE_PNG == module || ACE_MODULE_TSCC == module )
    {
        //request semphore
        if(ACE_REQUEST_MODE_NOWAIT == mode)
        {
            if(down_trylock(&pSemAceCe)== 0)     //the resource is available or not
            {
                 return ACE_FAIL;
	    	}
	    }
        else if(ACE_REQUEST_MODE_WAIT == mode)
        {
            
            if(down_interruptible(&pSemAceCe))
            {
	    	    return  -ERESTARTSYS;
	        }
	    }        
        ACE_EnableModule(ACE_MODULE_CE, ACE_MODULE_ENABLE);
	    
    }
    else if(ACE_MODULE_AE == module)
    {			
        ACE_EnableModule(ACE_MODULE_AE, ACE_MODULE_ENABLE);
    }
    else
    {
		printk("%s, %d\n", __FILE__, __LINE__);
        return ACE_FAIL;			
    }

    return ACE_OK;
}
EXPORT_SYMBOL_GPL(ACE_HwReq);

/*
*********************************************************************************************************
*                                   RELEASE HARDWARE RESOURCE
*
* Description: release hardware resource;
*
* Arguments  : moudle   the module need be released;
*
* Returns    : result;
*                   ACE_OK,    release hardware resource successed;
*                   ACE_FAIL,  release hardware resource failed;
*
* Note       :
*********************************************************************************************************
*/
__s32 ACE_HwRel(__ace_module_type_e module)
{
	if (module == ACE_MODULE_CE) {
        //release semphore
	    up(&pSemAceCe);			
        ACE_EnableModule(ACE_MODULE_CE, ACE_MODULE_DISABLE);			
    } else if(module == ACE_MODULE_AE) {			
        ACE_EnableModule(ACE_MODULE_AE, ACE_MODULE_DISABLE);
    }
    
    return ACE_OK;
    
}
EXPORT_SYMBOL_GPL(ACE_HwRel);

/*
*********************************************************************************************************
*                                       GET ACE MODULE CLOCK
*
* Description: This function Get ACE module clock;
*
* Arguments  : 
*
* Returns    : nFreq    ce module clk freqrence value;
*********************************************************************************************************
*/
__u32 ACE_GetClk(void)
{
    __u32 temp = 0;
    temp = esCLK_GetSrcFreq(esCLK_GetMclkSrc(hAceMClk)); 
    return temp;
}
EXPORT_SYMBOL_GPL(ACE_GetClk);



/*
*********************************************************************************************************
*                                       ACE_EnableModule
*
* Description: 
*
* Arguments  : 
*
* Returns    :
*
* Note       :
*********************************************************************************************************
*/
static __s32 ACE_EnableModule(__ace_module_type_e module, __u32 mode)
{

    __u32 status = readReg(ACE_MODE_SELECTOR);
    if(ACE_MODULE_CE == module || ACE_MODULE_PNG == module || ACE_MODULE_TSCC == module )
    {
        if(ACE_MODULE_ENABLE == mode)
          {
              status |=  ACE_CE_ENABLE_MASK;
          }
          else if(ACE_MODULE_DISABLE == mode)
          {
              status &= (~ACE_CE_ENABLE_MASK);
          }
        
    }
    else if(ACE_MODULE_AE == module)
    {
        if(ACE_MODULE_ENABLE == mode)
          {
              status |=  ACE_AE_ENABLE_MASK;
          }
          else if(ACE_MODULE_DISABLE == mode)
          {
              status &= (~ACE_AE_ENABLE_MASK);
          }
    }
    writeReg(ACE_MODE_SELECTOR,status);
    return ACE_OK;  
}


