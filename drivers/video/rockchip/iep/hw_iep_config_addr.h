#ifndef HW_IEP_CONFIG_ADDR_H_
#define HW_IEP_CONFIG_ADDR_H_

#include <asm/io.h>

#define      IEP_BASE                      0x0 //ignore the IEP_BASE when program running in linux kernel //0x10108000

#define      IEP_CONFIG0      		       0x0000
#define      IEP_CONFIG1      		       0x0004

#define      IEP_STATUS              	   0x0008
#define      IEP_INT                 	   0x000C
#define      IEP_FRM_START         		   0x0010
#define      IEP_SOFT_RST           	   0x0014
#define      IEP_CONF_DONE                 0x0018

#define      IEP_VIR_IMG_WIDTH        	   0x0020

#define      IEP_IMG_SCL_FCT         	   0x0024

#define      IEP_SRC_IMG_SIZE         	   0x0028
#define      IEP_DST_IMG_SIZE         	   0x002C

#define      IEP_DST_IMG_WIDTH_TILE0  	   0x0030
#define      IEP_DST_IMG_WIDTH_TILE1  	   0x0034
#define      IEP_DST_IMG_WIDTH_TILE2  	   0x0038
#define      IEP_DST_IMG_WIDTH_TILE3  	   0x003C

#define      IEP_ENH_YUV_CNFG_0       	   0x0040
#define      IEP_ENH_YUV_CNFG_1       	   0x0044
#define      IEP_ENH_YUV_CNFG_2       	   0x0048
#define      IEP_ENH_RGB_CNFG        	   0x004C
#define      IEP_ENH_C_COE            	   0x0050

#define      IEP_SRC_ADDR_YRGB        	   0x0080
#define      IEP_SRC_ADDR_CBCR             0x0084
#define      IEP_SRC_ADDR_CR               0x0088
#define      IEP_SRC_ADDR_Y1               0x008C
#define      IEP_SRC_ADDR_CBCR1            0x0090
#define      IEP_SRC_ADDR_CR1              0x0094
#define      IEP_SRC_ADDR_Y_ITEMP          0x0098
#define      IEP_SRC_ADDR_CBCR_ITEMP       0x009C
#define      IEP_SRC_ADDR_CR_ITEMP         0x00A0
#define      IEP_SRC_ADDR_Y_FTEMP          0x00A4
#define      IEP_SRC_ADDR_CBCR_FTEMP       0x00A8
#define      IEP_SRC_ADDR_CR_FTEMP         0x00AC

#define      IEP_DST_ADDR_YRGB        	   0x00B0
#define      IEP_DST_ADDR_CBCR             0x00B4
#define      IEP_DST_ADDR_CR               0x00B8
#define      IEP_DST_ADDR_Y1               0x00BC
#define      IEP_DST_ADDR_CBCR1            0x00C0
#define      IEP_DST_ADDR_CR1              0x00C4
#define      IEP_DST_ADDR_Y_ITEMP          0x00C8
#define      IEP_DST_ADDR_CBCR_ITEMP       0x00CC
#define      IEP_DST_ADDR_CR_ITEMP         0x00D0
#define      IEP_DST_ADDR_Y_FTEMP          0x00D4
#define      IEP_DST_ADDR_CBCR_FTEMP       0x00D8
#define      IEP_DST_ADDR_CR_FTEMP         0x00DC

#define      IEP_DIL_MTN_TAB0              0x00E0
#define      IEP_DIL_MTN_TAB1              0x00E4
#define      IEP_DIL_MTN_TAB2              0x00E8
#define      IEP_DIL_MTN_TAB3              0x00EC
#define      IEP_DIL_MTN_TAB4              0x00F0
#define      IEP_DIL_MTN_TAB5              0x00F4
#define      IEP_DIL_MTN_TAB6              0x00F8
#define      IEP_DIL_MTN_TAB7              0x00FC

#define      IEP_ENH_CG_TAB                0x0100

#define      IEP_YUV_DNS_CRCT_TEMP         0x0400
#define      IEP_YUV_DNS_CRCT_SPAT         0x0800

#define      IEP_ENH_DDE_COE0              0x0C00
#define      IEP_ENH_DDE_COE1              0x0E00

#define      RAW_IEP_CONFIG0               0x0058
#define      RAW_IEP_CONFIG1      		   0x005C
#define      RAW_IEP_VIR_IMG_WIDTH         0x0060

#define      RAW_IEP_IMG_SCL_FCT      	   0x0064

#define      RAW_IEP_SRC_IMG_SIZE      	   0x0068
#define      RAW_IEP_DST_IMG_SIZE      	   0x006C

#define      RAW_IEP_ENH_YUV_CNFG_0        0x0070
#define      RAW_IEP_ENH_YUV_CNFG_1        0x0074
#define      RAW_IEP_ENH_YUV_CNFG_2        0x0078
#define      RAW_IEP_ENH_RGB_CNFG          0x007C

#if defined(CONFIG_IEP_MMU)
#define      IEP_MMU_BASE                  0x0800
#define      IEP_MMU_DTE_ADDR              (IEP_MMU_BASE+0x00)
#define      IEP_MMU_STATUS                (IEP_MMU_BASE+0x04)
#define      IEP_MMU_CMD                   (IEP_MMU_BASE+0x08)
#define      IEP_MMU_PAGE_FAULT_ADDR       (IEP_MMU_BASE+0x0c)
#define      IEP_MMU_ZAP_ONE_LINE          (IEP_MMU_BASE+0x10)
#define      IEP_MMU_INT_RAWSTAT           (IEP_MMU_BASE+0x14)
#define      IEP_MMU_INT_CLEAR             (IEP_MMU_BASE+0x18)
#define      IEP_MMU_INT_MASK              (IEP_MMU_BASE+0x1c)
#define      IEP_MMU_INT_STATUS            (IEP_MMU_BASE+0x20)
#define      IEP_MMU_AUTO_GATING           (IEP_MMU_BASE+0x24)
#endif

#define ReadReg32(base, raddr)	        (__raw_readl(base + raddr))
#define WriteReg32(base, waddr, value)	(__raw_writel(value, base + waddr))
#define ConfRegBits32(base, raddr, waddr, position, value)           WriteReg32(base, waddr, (ReadReg32(base, waddr)&~(position))|(value))
#define MaskRegBits32(base, waddr, position, value)                  WriteReg32(base, waddr, (ReadReg32(base, waddr)&~(position))|(value))

#endif
