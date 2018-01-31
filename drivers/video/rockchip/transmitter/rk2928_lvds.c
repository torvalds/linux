/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/io.h>
#include <mach/io.h>
#include <linux/rk_screen.h>
#include "rk2928_lvds.h"

static void rk_output_lvds(rk_screen *screen)
{
	LVDSWrReg(m_PDN_CBG(1)|m_PD_PLL(0)|m_PDN(1)|m_OEN(0) 	\
					|m_DS(DS_10PF)|m_MSBSEL(DATA_D0_MSB) 	\
					|m_OUT_FORMAT(screen->lvds_format) 		\
					|m_LCDC_SEL(screen->lcdc_id));

       printk("%s>>connect to lcdc output interface%d\n",__func__,screen->lcdc_id);
}

static void rk_output_lvttl(rk_screen *screen)
{
	LVDSWrReg(m_PDN_CBG(0)|m_PD_PLL(1)|m_PDN(0)|m_OEN(1) 	\
					|m_DS(DS_10PF)|m_MSBSEL(DATA_D0_MSB) 	\
					|m_OUT_FORMAT(screen->lvds_format) 		\
					|m_LCDC_SEL(screen->lcdc_id));
        printk("%s>>connect to lcdc output interface%d\n",__func__,screen->lcdc_id);
}

static void rk_output_disable(void)
{
	LVDSWrReg(m_PDN_CBG(0)|m_PD_PLL(1)|m_PDN(0)|m_OEN(0));
        printk("%s: reg = 0x%x\n",  __func__, LVDSRdReg());
}

static int rk_lvds_set_param(rk_screen *screen,bool enable )
{
	if(OUT_ENABLE == enable){
		switch(screen->type){
			case SCREEN_LVDS:
					rk_output_lvds(screen);
                                        
					break;
			case SCREEN_RGB:
					rk_output_lvttl(screen);
					break;
			default:
				printk("%s>>>>LVDS not support this screen type %d,power down LVDS\n",__func__,screen->type);
					rk_output_disable();
					break;
		}
	}else{
		rk_output_disable();
	}
	return 0;
}

int rk_lvds_register(rk_screen *screen)
{
	if(screen->sscreen_set == NULL)
		screen->sscreen_set = rk_lvds_set_param;

	rk_lvds_set_param(screen , OUT_ENABLE);

	return 0;
}
