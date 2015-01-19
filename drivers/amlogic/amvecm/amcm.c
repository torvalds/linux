/*
 * Color Management
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *         Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <mach/am_regs.h>
#include <linux/module.h>
#include <linux/amlogic/cm.h>
#include <linux/amlogic/aml_common.h>
#include <linux/amlogic/vframe.h>
#include "cm_regs.h"
#include "amcm.h"

#if 0
struct cm_region_s cm_region;
struct cm_top_s    cm_top;
struct cm_demo_s   cm_demo;
#endif

unsigned int cm2_patch_flag = 0;

#ifdef AMVIDEO_REG_TABLE_DYNAMIC
void am_set_regmap(unsigned int cnt, struct am_reg_s *p)
{
    unsigned short i;
    unsigned int temp = 0;

    for (i=0; i<cnt; i++) {
        switch (p->type)
        {
            case REG_TYPE_PHY:
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: bus type: phy..............\n", __func__);
                #endif
            break;
            case REG_TYPE_CBUS:
                if (p->mask == 0xffffffff)
                    WRITE_CBUS_REG(p->addr, p->val);
                else
                    WRITE_CBUS_REG(p->addr, (READ_CBUS_REG(p->addr) & (~p->mask)) | (p->val & p->mask));
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: cbus: Reg0x%x = 0x%x...............\n", __func__, p->addr, (p->val & p->mask));
                #endif
            break;
            case REG_TYPE_APB:
                if (p->mask == 0xffffffff)
                    WRITE_APB_REG(p->addr, p->val);
                else
                    WRITE_APB_REG(p->addr, (READ_APB_REG(p->addr) & (~p->mask)) | (p->val & p->mask));
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: apb bus: Reg0x%x = 0x%x...............\n", __func__, p->addr, (p->val & p->mask));
                #endif
            break;
            case REG_TYPE_MPEG:
                if (p->mask == 0xffffffff)
                    WRITE_MPEG_REG(p->addr, p->val);
                else
                    WRITE_MPEG_REG(p->addr, (READ_MPEG_REG(p->addr) & (~p->mask)) | (p->val & p->mask));
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: mpeg: Reg0x%x = 0x%x...............\n", __func__, p->addr, (p->val & p->mask));
                #endif
            break;
            case REG_TYPE_AXI:
                if (p->mask == 0xffffffff)
                    WRITE_AXI_REG(p->addr, p->val);
                else
                    WRITE_AXI_REG(p->addr, (READ_AXI_REG(p->addr) & (~p->mask)) | (p->val & p->mask));
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: axi: Reg0x%x = 0x%x...............\n", __func__, p->addr, (p->val & p->mask));
                #endif
            break;
            case REG_TYPE_AHB:
                if (p->mask == 0xffffffff)
                    WRITE_AHB_REG(p->addr, p->val);
                else
                    WRITE_AHB_REG(p->addr, (READ_AHB_REG(p->addr) & (~p->mask)) | (p->val & p->mask));
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: ahb: Reg0x%x = 0x%x...............\n", __func__, p->addr, (p->val & p->mask));
                #endif
            break;
            case REG_TYPE_INDEX_VPPCHROMA:
                WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, p->addr);
                if (p->mask == 0xffffffff)
                {
                    WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, p->val);
                }
                else
                {
                    temp = READ_CBUS_REG(VPP_CHROMA_DATA_PORT);
                    WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, p->addr);
                    WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, (temp & (~p->mask)) | (p->val & p->mask));
                }
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: vppchroma: 0x1d70:port0x%x = 0x%x...............\n", __func__, p->addr, (p->val & p->mask));
                #endif
            break;
            case REG_TYPE_INDEX_GAMMA:
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: bus type: REG_TYPE_INDEX_GAMMA..............\n", __func__);
                #endif
            break;
            case VALUE_TYPE_CONTRAST_BRIGHTNESS:
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: bus type: VALUE_TYPE_CONTRAST_BRIGHTNESS..............\n", __func__);
                #endif
            break;
            case REG_TYPE_INDEX_VPP_COEF:
		    	if (((p->addr&0xf) == 0)||((p->addr&0xf) == 0x8))
		    		{
		            WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, p->addr);
					WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, p->val);
		    		}
				else
					{
					WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, p->val);
					}
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: vppcoef: 0x1d70:port0x%x = 0x%x...............\n", __func__, p->addr, (p->val & p->mask));
                #endif
            break;
            default:
                pr_info("%s: bus type error!!!bustype = 0x%x................\n", __func__, p->type);
            break;
        }
        p++;
    }

    return;
}
#else
void am_set_regmap(struct am_regs_s *p)
{
    unsigned short i;
    unsigned int temp = 0;

    for (i=0; i<p->length; i++) {
        switch (p->am_reg[i].type)
        {
            case REG_TYPE_PHY:
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: bus type: phy..............\n", __func__);
                #endif
            break;
            case REG_TYPE_CBUS:
                if (p->am_reg[i].mask == 0xffffffff)
                    WRITE_CBUS_REG(p->am_reg[i].addr, p->am_reg[i].val);
                else
                    WRITE_CBUS_REG(p->am_reg[i].addr, (READ_CBUS_REG(p->am_reg[i].addr) & (~(p->am_reg[i].mask))) | (p->am_reg[i].val & p->am_reg[i].mask));
                #ifdef PQ_DEBUG_EN
					pr_info("%s: cbus: Reg0x%x(%u)=0x%x(%u)val=%x(%u)mask=%x(%u)\n", __func__, p->am_reg[i].addr,p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),(p->am_reg[i].val & p->am_reg[i].mask),
					p->am_reg[i].val,p->am_reg[i].val,p->am_reg[i].mask,p->am_reg[i].mask);
                #endif
            break;
            case REG_TYPE_APB:
                if (p->am_reg[i].mask == 0xffffffff)
                    WRITE_APB_REG(p->am_reg[i].addr, p->am_reg[i].val);
                else
                    WRITE_APB_REG(p->am_reg[i].addr, (READ_APB_REG(p->am_reg[i].addr) & (~(p->am_reg[i].mask))) | (p->am_reg[i].val & p->am_reg[i].mask));
                #ifdef PQ_DEBUG_EN
					pr_info("%s: apb: Reg0x%x(%u)=0x%x(%u)val=%x(%u)mask=%x(%u)\n", __func__, p->am_reg[i].addr,p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),(p->am_reg[i].val & p->am_reg[i].mask),
					p->am_reg[i].val,p->am_reg[i].val,p->am_reg[i].mask,p->am_reg[i].mask);
                #endif
            break;
            case REG_TYPE_MPEG:
                if (p->am_reg[i].mask == 0xffffffff)
                    WRITE_MPEG_REG(p->am_reg[i].addr, p->am_reg[i].val);
                else
                    WRITE_MPEG_REG(p->am_reg[i].addr, (READ_MPEG_REG(p->am_reg[i].addr) & (~(p->am_reg[i].mask))) | (p->am_reg[i].val & p->am_reg[i].mask));
                #ifdef PQ_DEBUG_EN
					pr_info("%s: mpeg: Reg0x%x(%u)=0x%x(%u)val=%x(%u)mask=%x(%u)\n", __func__, p->am_reg[i].addr,p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),(p->am_reg[i].val & p->am_reg[i].mask),
					p->am_reg[i].val,p->am_reg[i].val,p->am_reg[i].mask,p->am_reg[i].mask);
                #endif
            break;
            case REG_TYPE_AXI:
                if (p->am_reg[i].mask == 0xffffffff)
                    WRITE_AXI_REG(p->am_reg[i].addr, p->am_reg[i].val);
                else
                    WRITE_AXI_REG(p->am_reg[i].addr, (READ_AXI_REG(p->am_reg[i].addr) & (~(p->am_reg[i].mask))) | (p->am_reg[i].val & p->am_reg[i].mask));
                #ifdef PQ_DEBUG_EN
					pr_info("%s: axi: Reg0x%x(%u)=0x%x(%u)val=%x(%u)mask=%x(%u)\n", __func__, p->am_reg[i].addr,p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),(p->am_reg[i].val & p->am_reg[i].mask),
					p->am_reg[i].val,p->am_reg[i].val,p->am_reg[i].mask,p->am_reg[i].mask);
                #endif
            break;
		#if 0
            case REG_TYPE_AHB:
                if (p->am_reg[i].mask == 0xffffffff)
                    WRITE_AHB_REG(p->am_reg[i].addr, p->am_reg[i].val);
                else
                    WRITE_AHB_REG(p->am_reg[i].addr, (READ_AHB_REG(p->am_reg[i].addr) & (~(p->am_reg[i].mask))) | (p->am_reg[i].val & p->am_reg[i].mask));
                #ifdef PQ_DEBUG_EN
					pr_info("%s: ahb: Reg0x%x(%u)=0x%x(%u)val=%x(%u)mask=%x(%u)\n", __func__, p->am_reg[i].addr,p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),(p->am_reg[i].val & p->am_reg[i].mask),
					p->am_reg[i].val,p->am_reg[i].val,p->am_reg[i].mask,p->am_reg[i].mask);
                #endif
            break;
		#endif
            case REG_TYPE_INDEX_VPPCHROMA:
				/*  add for vm2 demo frame size setting */
				if (p->am_reg[i].addr == 0x20f) {
                	if ((p->am_reg[i].val & 0xff) != 0) {
						cm2_patch_flag = p->am_reg[i].val;
                    	p->am_reg[i].val = p->am_reg[i].val & 0xffffff00;
                    }
                    else
                  		cm2_patch_flag = 0;
                }
                WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, p->am_reg[i].addr);
                if (p->am_reg[i].mask == 0xffffffff)
                {
                    WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, p->am_reg[i].val);
                }
                else
                {
                    temp = READ_CBUS_REG(VPP_CHROMA_DATA_PORT);
                    WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, p->am_reg[i].addr);
                    WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, (temp & (~(p->am_reg[i].mask))) | (p->am_reg[i].val & p->am_reg[i].mask));
                }
                #ifdef PQ_DEBUG_EN
					pr_info("%s: chroma: Reg0x%x(%u)=0x%x(%u)val=%x(%u)mask=%x(%u)\n", __func__, p->am_reg[i].addr,p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),(p->am_reg[i].val & p->am_reg[i].mask),
					p->am_reg[i].val,p->am_reg[i].val,p->am_reg[i].mask,p->am_reg[i].mask);
                #endif
            break;
            case REG_TYPE_INDEX_GAMMA:
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: bus type: REG_TYPE_INDEX_GAMMA..............\n", __func__);
                #endif
            break;
            case VALUE_TYPE_CONTRAST_BRIGHTNESS:
                #ifdef PQ_DEBUG_EN
                    pr_info("%s: bus type: VALUE_TYPE_CONTRAST_BRIGHTNESS..............\n", __func__);
                #endif
            break;
			case REG_TYPE_INDEX_VPP_COEF:
				if (((p->am_reg[i].addr&0xf) == 0)||((p->am_reg[i].addr&0xf) == 0x8))
					{
					WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, p->am_reg[i].addr);
					WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, p->am_reg[i].val);
					}
				else
					{
					WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, p->am_reg[i].val);
					}
				#ifdef PQ_DEBUG_EN
					pr_info("%s: coef: Reg0x%x(%u)=0x%x(%u)val=%x(%u)mask=%x(%u)\n", __func__, p->am_reg[i].addr,p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),(p->am_reg[i].val & p->am_reg[i].mask),
					p->am_reg[i].val,p->am_reg[i].val,p->am_reg[i].mask,p->am_reg[i].mask);
                #endif
            break;
            default:
            #ifdef PQ_DEBUG_EN
                pr_info("%s: bus type error!!!bustype = 0x%x................\n", __func__, p->am_reg[i].type);
            #endif
            break;
        }
    }

    return;
}
#endif

