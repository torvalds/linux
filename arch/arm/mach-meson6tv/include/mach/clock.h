/*
*  arch/arm/mach-meson6tv/include/mach/clock.h
*
*  Copyright (C) 2010-2013 AMLOGIC, INC.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __MACH_MESON6TV_CLOCK_H
#define __MACH_MESON6TV_CLOCK_H

#include <linux/types.h>
#include <linux/list.h>

struct clk_ops {
	//return: 0:success  1:fail
	int (*clk_ratechange_before)(unsigned long newrate,void* privdata);
	//return: 0:success  1:fail
	int (*clk_ratechange_after)(unsigned long newrate,void* privdata,int failed);
	//return: 0:success  1:fail
	int (*clk_enable_before)(void* privdata);
	//return: 0:success  1:fail
	int (*clk_enable_after)(void* privdata,int failed);
	//return: 0:success  1:fail
	int (*clk_disable_before)(void* privdata);
	//return: 0:success  1:fail.
	int (*clk_disable_after)(void* privdata,int failed);
	void* privdata;
	struct clk_ops* next;
};

struct clk {
    #define CLK_RATE_UNKNOWN (-1)
    unsigned long rate;///0xffffffff(-1) means unknown

    unsigned long(*get_rate)(struct clk *);
    int (*set_rate)(struct clk *, unsigned long);
    int (*enable)(struct clk *);///disable my self
    int (*disable)(struct clk *);///enable my self
    bool (*status)(struct clk *);
    int (*on_parent_changed)(struct clk *clk, int rate);
    int (*need_parent_changed)(struct clk *clk, int rate);
    int msr;
    unsigned long msr_mul;
    unsigned long msr_div;
    unsigned long min;
    unsigned long max;

    unsigned clk_gate_reg_adr;
    unsigned clk_gate_reg_mask;

    int  open_irq;

    struct list_head child;
    struct list_head sibling;
    struct clk * parent;
    struct clk_ops*  clk_ops;
    void * priv;

    unsigned long old_rate;//Just for store old cpu freq for set_sys_pll()
};
int  clk_register(struct clk *clk,const char *parent);
void clk_unregister(struct clk *clk);
int  clk_measure(char  index );

//return: 0:disabed. 1:enabled. 2:unknown
int  clk_get_status(struct clk * clk);

//return: 0:success  1: fail
int clk_ops_register(struct clk *clk, struct clk_ops *ops);
//return: 0:success  1: fail
int clk_ops_unregister(struct clk *clk, struct clk_ops *ops);


//M6 all pll controler use bit 29 as reset bit
#define M6_PLL_RESET(pll) aml_set_reg32_mask(pll,(1<<29));

//wait for pll lock
//must wait first (100us+) then polling lock bit to check
#define M6_PLL_WAIT_FOR_LOCK(pll) \
	do{\
		udelay(1000);\
	}while((aml_read_reg32(pll)&0x80000000)==0);

//M6 PLL control value
#define M6_PLL_CNTL_CST2 (0x814d3928)
#define M6_PLL_CNTL_CST3 (0x6b425012)
#define M6_PLL_CNTL_CST4 (0x110)

#define M6_PLL_CNTL_CST12 (0x04294000)
#define M6_PLL_CNTL_CST13 (0x026b4250)
#define M6_PLL_CNTL_CST14 (0x06278410)
#define M6_PLL_CNTL_CST15 (0x1e1)
#define M6_PLL_CNTL_CST16 (0xacac10ac)
#define M6_PLL_CNTL_CST17 (0x0108e000)


//DDR PLL
#define M6_DDR_PLL_CNTL_2 (M6_PLL_CNTL_CST2)
#define M6_DDR_PLL_CNTL_3 (M6_PLL_CNTL_CST3)
#define M6_DDR_PLL_CNTL_4 (M6_PLL_CNTL_CST4)

//SYS PLL
#define M6_SYS_PLL_CNTL_2 (M6_PLL_CNTL_CST2)
#define M6_SYS_PLL_CNTL_3 (M6_PLL_CNTL_CST3)
#define M6_SYS_PLL_CNTL_4 (M6_PLL_CNTL_CST4)

//VIID PLL
#define M6_VIID_PLL_CNTL_2 (M6_PLL_CNTL_CST2)
#define M6_VIID_PLL_CNTL_3 (M6_PLL_CNTL_CST3)
#define M6_VIID_PLL_CNTL_4 (M6_PLL_CNTL_CST4)
//Wr(HHI_VIID_PLL_CNTL,  0x20242 );	 //0x1047


//VID PLL
#define M6_VID_PLL_CNTL_2 (M6_PLL_CNTL_CST2)
#define M6_VID_PLL_CNTL_3 (M6_PLL_CNTL_CST3)
#define M6_VID_PLL_CNTL_4 (M6_PLL_CNTL_CST4)
//Wr(HHI_VID_PLL_CNTL,  0xb0442 ); //0x109c

//FIXED PLL/Multi-phase PLL
#define M6_MPLL_CNTL_2 (M6_PLL_CNTL_CST12)
#define M6_MPLL_CNTL_3 (M6_PLL_CNTL_CST13)
#define M6_MPLL_CNTL_4 (M6_PLL_CNTL_CST14)
#define M6_MPLL_CNTL_5 (M6_PLL_CNTL_CST15)
#define M6_MPLL_CNTL_6 (M6_PLL_CNTL_CST16)
#define M6_MPLL_CNTL_7 (M6_PLL_CNTL_CST17)
#define M6_MPLL_CNTL_8 (M6_PLL_CNTL_CST17)
#define M6_MPLL_CNTL_9 (M6_PLL_CNTL_CST17)
#define M6_MPLL_CNTL_10 (0)

extern unsigned long mali_clock_gating_lock(void);
extern void mali_clock_gating_unlock(unsigned long flags);
extern unsigned int clk_util_clk_msr(unsigned int clk_mux);

#endif // __MACH_MESON6TV_CLOCK_H
