#ifndef _PM_I_H
#define _PM_I_H

/*
 * Copyright (c) 2011-2015 yanggq.young@newbietech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <mach/platform.h>
#include <mach/ccmu.h>
#include <mach/hardware.h>
#include "pm.h"
#include "mem_mapping.h"

#include "standby/super/super_clock.h"
#include "standby/super/super_power.h"
#include "standby/super/super_twi.h"


typedef struct __MEM_TWIC_REG
{
    volatile __u32 reg_saddr;
    volatile __u32 reg_xsaddr;
    volatile __u32 reg_data;
    volatile __u32 reg_ctl;
    volatile __u32 reg_status;
    volatile __u32 reg_clkr;
    volatile __u32 reg_reset;
    volatile __u32 reg_efr;
    volatile __u32 reg_lctl;

} __mem_twic_reg_t;


struct gic_distributor_state{
	//distributor
	volatile __u32 GICD_CTLR;					//offset 0x00, distributor Contrl reg
	volatile __u32 GICD_IGROUPRn;					//offset 0x80, Interrupt Grp Reg ?           
	volatile __u32 GICD_ISENABLERn[0x180/4];			//offset 0x100-0x17c, Interrupt Set-Enable Reg       
	volatile __u32 GICD_ICENABLERn[0x180/4];			//offset 0x180-0x1fc, Iterrupt Clear-Enable Reg      
	volatile __u32 GICD_ISPENDRn[0x180/4];				//offset 0x200-0x27c, Iterrupt Set-Pending Reg       
	volatile __u32 GICD_ICPENDRn[0x180/4];				//offset 0x280-0x2fc, Iterrupt Clear-Pending Reg     
	volatile __u32 GICD_ISACTIVERn[0x180/4]; 			//offset 0x300-0x37c, GICv2 Iterrupt Set-Active Reg        
	volatile __u32 GICD_ICACTIVERn[0x180/4]; 			//offset 0x380-0x3fc, Iterrupt Clear-Active Reg        
	volatile __u32 GICD_IPRIORITYRn[0x180/4]; 			//offset 0x400-0x7F8, Iterrupt Priority Reg          
	volatile __u32 GICD_ITARGETSRn[(0x400-0x20)/4];			//offset 0x820-0xbf8, Iterrupt Processor Targets Reg         
	volatile __u32 GICD_ICFGRn[0x100/4];				//offset 0xc00-0xcfc, Iterrupt Config Reg            
	volatile __u32 GICD_NSACRn[0x100/4];				//offset 0xE00-0xEfc, non-secure Access Ctrl Reg ?   
	volatile __u32 GICD_CPENDSGIRn[0x10/4];				//offset 0xf10-0xf1c, SGI Clear-Pending Reg          
	volatile __u32 GICD_SPENDSGIRn[0x10/4];				//offset 0xf20-0xf2c, SGI Set-Pending Reg       	
};

struct gic_cpu_interface_state{
	//cpu interface reg
	volatile __u32 GICC_CTLR_PMR_BPR[0xc/4];			//offset 0x00-0x08, cpu interface Ctrl Reg	+ 	Interrupt Priority Mask Reg	 + 	  Binary Point Reg	
	volatile __u32 GICC_ABPR;				//offset 0x1c, Aliased Binary Point Reg	
	volatile __u32 GICC_APRn[0x10/4];			//offset 0xd0-0xdc, Active Priorities Reg 		  
	volatile __u32 GICC_NSAPRn[0x10/4];			//offset 0xe0-0xec, Non-secure Active Priorities Reg
};

struct gic_distributor_disc{
	//distributor
	volatile __u32 GICD_CTLR;					//offset 0x00, distributor Contrl reg
	volatile __u32 reserved0[0x7c/4];				//0ffset 0x04-0x7c
	volatile __u32 GICD_IGROUPRn;					//offset 0x80, Interrupt Grp Reg ?           
	volatile __u32 reserved1[0x7c/4];				//0ffset 0x84-0xfc
	volatile __u32 GICD_ISENABLERn[0x180/4];			//offset 0x100-0x17c, Interrupt Set-Enable Reg       
	volatile __u32 GICD_ICENABLERn[0x180/4];			//offset 0x180-0x1fc, Iterrupt Clear-Enable Reg      
	volatile __u32 GICD_ISPENDRn[0x180/4];				//offset 0x200-0x27c, Iterrupt Set-Pending Reg       
	volatile __u32 GICD_ICPENDRn[0x180/4];				//offset 0x280-0x2fc, Iterrupt Clear-Pending Reg     
	volatile __u32 GICD_ISACTIVERn[0x180/4]; 			//offset 0x300-0x37c, GICv2 Iterrupt Set-Active Reg        
	volatile __u32 GICD_ICACTIVERn[0x180/4]; 			//offset 0x380-0x3fc, Iterrupt Clear-Active Reg        
	volatile __u32 GICD_IPRIORITYRn[0x180/4]; 			//offset 0x400-0x7F8, Iterrupt Priority Reg          
	volatile __u32 reserved2[0x24/4];				//0ffset 0x7fc-0x81c
	volatile __u32 GICD_ITARGETSRn[(0x400-0x20)/4];			//offset 0x820-0xbf8, Iterrupt Processor Targets Reg         
	volatile __u32 reserved3;					//0ffset 0xbfc
	volatile __u32 GICD_ICFGRn[0x100/4];				//offset 0xc00-0xcfc, Iterrupt Config Reg            
	volatile __u32 reserved4[0x100/4];				//0ffset 0xd00-0xdfc
	volatile __u32 GICD_NSACRn[0x100/4];				//offset 0xE00-0xEfc, non-secure Access Ctrl Reg ?   
	volatile __u32 reserved5[0x10/4];				//0ffset 0xf00-0xf0c
	volatile __u32 GICD_CPENDSGIRn[0x10/4];				//offset 0xf10-0xf1c, SGI Clear-Pending Reg          
	volatile __u32 GICD_SPENDSGIRn[0x10/4];				//offset 0xf20-0xf2c, SGI Set-Pending Reg       
	volatile __u32 reserved6[0xd0/4];				//0ffset 0xf30-0xffc         
	
};

struct gic_cpu_interface_disc{
	//cpu interface reg
	volatile __u32 GICC_CTLR_PMR_BPR[0xc/4];			//offset 0x00-0x08, cpu interface Ctrl Reg	+ 	Interrupt Priority Mask Reg	 + 	  Binary Point Reg	
	volatile __u32 reserved7[0x10/4];			//0ffset 0xC-0x18, readonly or writeonly      
	volatile __u32 GICC_ABPR;				//offset 0x1c, Aliased Binary Point Reg	
	volatile __u32 reserved8[0xb0/4];			//0ffset 0x20-0xcf, readonly or writeonly      
	volatile __u32 GICC_APRn[0x10/4];			//offset 0xd0-0xdc, Active Priorities Reg 		  
	volatile __u32 GICC_NSAPRn[0x10/4];			//offset 0xe0-0xec, Non-secure Active Priorities Reg
	volatile __u32 reserved9[0x10/4];			//0ffset 0xf0-0xfc, readonly or writeonly  
	volatile __u32 reserved10[0xf00/4];			//0ffset 0x100-0xffc, readonly or writeonly  
	volatile __u32 reserved11;				//0ffset 0x1000, readonly or writeonly   

};

struct gic_state{
	struct gic_distributor_state m_distributor;
	struct gic_cpu_interface_state m_interface;
};

struct twi_state{
	__mem_twic_reg_t *twi_reg;
	__u32 twi_reg_backup[7];
};

struct gpio_state{
	__u32 gpio_reg_back[GPIO_REG_LENGTH];
};

struct sram_state{
	__u32 sram_reg_back[SRAM_REG_LENGTH];
};

//save module state
__s32 mem_twi_save(struct twi_state *ptwi_state);
__s32 mem_twi_restore(struct twi_state *ptwi_state);
__s32 mem_gpio_save(struct gpio_state *pgpio_state);
__s32 mem_gpio_restore(struct gpio_state *pgpio_state);
__s32 mem_sram_save(struct sram_state *psram_state);
__s32 mem_sram_restore(struct sram_state *psram_state);
__s32 mem_ccu_save(__ccmu_reg_list_t *pReg);
__s32 mem_ccu_restore(__ccmu_reg_list_t *pReg);

#endif /*_PM_I_H*/

