/*
 * linux/drivers/video/nvidia/nvidia.c - nVidia fb driver
 *
 * Copyright 2004 Antonino Daplas <adaplas@pol.net>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif
#ifdef CONFIG_PPC_OF
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#endif
#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif

#include "nv_local.h"
#include "nv_type.h"
#include "nv_proto.h"
#include "nv_dma.h"

#ifndef CONFIG_PCI		/* sanity check */
#error This driver requires PCI support.
#endif

#undef CONFIG_FB_NVIDIA_DEBUG
#ifdef CONFIG_FB_NVIDIA_DEBUG
#define NVTRACE          printk
#else
#define NVTRACE          if (0) printk
#endif

#define NVTRACE_ENTER(...)  NVTRACE("%s START\n", __FUNCTION__)
#define NVTRACE_LEAVE(...)  NVTRACE("%s END\n", __FUNCTION__)

#ifdef CONFIG_FB_NVIDIA_DEBUG
#define assert(expr) \
	if (!(expr)) { \
	printk( "Assertion failed! %s,%s,%s,line=%d\n",\
	#expr,__FILE__,__FUNCTION__,__LINE__); \
	BUG(); \
	}
#else
#define assert(expr)
#endif

#define PFX "nvidiafb: "

/* HW cursor parameters */
#define MAX_CURS		32

static struct pci_device_id nvidiafb_pci_tbl[] = {
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_TNT,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_TNT2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_UTNT2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_TNT_UNKNOWN,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_VTNT2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_UVTNT2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_ITNT2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_SDR,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_DDR,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_MX,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_MX2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_GO,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO2_MXR,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_GTS,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_GTS2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_ULTRA,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO2_PRO,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_MX_460,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_MX_440,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_MX_420,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_MX_440_SE,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_440_GO,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_420_GO,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_460_GO,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_420_GO_M32,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_500XGL,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_440_GO_M64,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_200,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_550XGL,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_500_GOGL,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_410_GO_M16,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_MX_440_8X,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_MX_440SE_8X,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_MX_420_8X,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_MX_4000,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_448_GO,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_488_GO,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_580_XGL,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_MX_MAC,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_280_NVS,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_380_XGL,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_IGEFORCE2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE3,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE3_1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE3_2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_DDC,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_TI_4600,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_TI_4400,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_TI_4200,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_900XGL,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_750XGL,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_700XGL,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_TI_4800,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_TI_4800_8X,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_TI_4800SE,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_4200_GO,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_980_XGL,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_780_XGL,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_700_GOGL,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5800_ULTRA,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5800,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_FX_2000,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_FX_1000,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5600_ULTRA,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5600,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5600SE,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_GO5600,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_GO5650,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_FX_GO700,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5200,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5200_ULTRA,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5200_1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5200SE,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_GO5200,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_GO5250,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_GO5250_32,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_GO_5200,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_NVS_280_PCI,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_FX_500,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_GO5300,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_GO5100,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5900_ULTRA,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5900,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5900XT,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5950_ULTRA,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_FX_3000,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5700_ULTRA,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5700,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5700LE,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5700VE,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_GO5700_1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_GO5700_2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_FX_GO1000,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_FX_1100,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5500,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5100,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_FX_700,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_5900ZT,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_6800_ULTRA,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_6800,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_6800_LE,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_6800_GT,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_FX_4000,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_6600_GT,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_6600,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_6610_XL,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_FX_540,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_6200,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCIE_DEVICE_ID_NVIDIA_GEFORCE_6800_ALT1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCIE_DEVICE_ID_NVIDIA_GEFORCE_6600_ALT1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCIE_DEVICE_ID_NVIDIA_GEFORCE_6600_ALT2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCIE_DEVICE_ID_NVIDIA_GEFORCE_6200_ALT1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCIE_DEVICE_ID_NVIDIA_GEFORCE_6800_GT,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0252,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0313,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0316,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0317,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x031D,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x031E,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x031F,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0329,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x032F,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0345,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0349,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x034B,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x034F,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x00c0,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_GEFORCE_6800A,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_GEFORCE_6800A_LE,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_GEFORCE_GO_6800,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_GEFORCE_GO_6800_ULTRA,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_QUADRO_FX_GO1400,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x00cd,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_QUADRO_FX_1400,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0142,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0143,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0144,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0145,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0146,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0147,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0148,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0149,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x014b,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x14c,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x014d,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0160,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_6200_TURBOCACHE,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0162,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0163,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_GO_6200,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0165,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_GO_6250,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_GO_6200_1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_GO_6250_1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0169,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x016b,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x016c,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x016d,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x016e,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0210,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_6800B,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_6800B_LE,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_6800B_GT,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_7800_GT,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_7800_GTX,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_GO_7800,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_GO_7800_GTX,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x021d,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x021e,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0220,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0221,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0222,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NVIDIA, 0x0228,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}			/* terminate list */
};

MODULE_DEVICE_TABLE(pci, nvidiafb_pci_tbl);

/* command line data, set in nvidiafb_setup() */
static int flatpanel __devinitdata = -1;	/* Autodetect later */
static int fpdither __devinitdata = -1;
static int forceCRTC __devinitdata = -1;
static int hwcur __devinitdata = 0;
static int noaccel __devinitdata = 0;
static int noscale __devinitdata = 0;
static int paneltweak __devinitdata = 0;
static int vram __devinitdata = 0;
static int bpp __devinitdata = 8;
#ifdef CONFIG_MTRR
static int nomtrr __devinitdata = 0;
#endif

static char *mode_option __devinitdata = NULL;

static struct fb_fix_screeninfo __devinitdata nvidiafb_fix = {
	.type = FB_TYPE_PACKED_PIXELS,
	.xpanstep = 8,
	.ypanstep = 1,
};

static struct fb_var_screeninfo __devinitdata nvidiafb_default_var = {
	.xres = 640,
	.yres = 480,
	.xres_virtual = 640,
	.yres_virtual = 480,
	.bits_per_pixel = 8,
	.red = {0, 8, 0},
	.green = {0, 8, 0},
	.blue = {0, 8, 0},
	.transp = {0, 0, 0},
	.activate = FB_ACTIVATE_NOW,
	.height = -1,
	.width = -1,
	.pixclock = 39721,
	.left_margin = 40,
	.right_margin = 24,
	.upper_margin = 32,
	.lower_margin = 11,
	.hsync_len = 96,
	.vsync_len = 2,
	.vmode = FB_VMODE_NONINTERLACED
};

/*
 * Backlight control
 */
#ifdef CONFIG_PMAC_BACKLIGHT

static int nvidia_backlight_levels[] = {
	0x158,
	0x192,
	0x1c6,
	0x200,
	0x234,
	0x268,
	0x2a2,
	0x2d6,
	0x310,
	0x344,
	0x378,
	0x3b2,
	0x3e6,
	0x41a,
	0x454,
	0x534,
};

/* ------------------------------------------------------------------------- *
 *
 * Backlight operations
 *
 * ------------------------------------------------------------------------- */

static int nvidia_set_backlight_enable(int on, int level, void *data)
{
	struct nvidia_par *par = data;
	u32 tmp_pcrt, tmp_pmc, fpcontrol;

	tmp_pmc = NV_RD32(par->PMC, 0x10F0) & 0x0000FFFF;
	tmp_pcrt = NV_RD32(par->PCRTC0, 0x081C) & 0xFFFFFFFC;
	fpcontrol = NV_RD32(par->PRAMDAC, 0x0848) & 0xCFFFFFCC;

	if (on && (level > BACKLIGHT_OFF)) {
		tmp_pcrt |= 0x1;
		tmp_pmc |= (1 << 31);	// backlight bit
		tmp_pmc |= nvidia_backlight_levels[level - 1] << 16;
	}

	if (on)
		fpcontrol |= par->fpSyncs;
	else
		fpcontrol |= 0x20000022;

	NV_WR32(par->PCRTC0, 0x081C, tmp_pcrt);
	NV_WR32(par->PMC, 0x10F0, tmp_pmc);
	NV_WR32(par->PRAMDAC, 0x848, fpcontrol);

	return 0;
}

static int nvidia_set_backlight_level(int level, void *data)
{
	return nvidia_set_backlight_enable(1, level, data);
}

static struct backlight_controller nvidia_backlight_controller = {
	nvidia_set_backlight_enable,
	nvidia_set_backlight_level
};

#endif				/* CONFIG_PMAC_BACKLIGHT */

static void nvidiafb_load_cursor_image(struct nvidia_par *par, u8 * data8,
				       u16 bg, u16 fg, u32 w, u32 h)
{
	u32 *data = (u32 *) data8;
	int i, j, k = 0;
	u32 b, tmp;

	w = (w + 1) & ~1;

	for (i = 0; i < h; i++) {
		b = *data++;
		reverse_order(&b);

		for (j = 0; j < w / 2; j++) {
			tmp = 0;
#if defined (__BIG_ENDIAN)
			tmp = (b & (1 << 31)) ? fg << 16 : bg << 16;
			b <<= 1;
			tmp |= (b & (1 << 31)) ? fg : bg;
			b <<= 1;
#else
			tmp = (b & 1) ? fg : bg;
			b >>= 1;
			tmp |= (b & 1) ? fg << 16 : bg << 16;
			b >>= 1;
#endif
			NV_WR32(&par->CURSOR[k++], 0, tmp);
		}
		k += (MAX_CURS - w) / 2;
	}
}

static void nvidia_write_clut(struct nvidia_par *par,
			      u8 regnum, u8 red, u8 green, u8 blue)
{
	NVWriteDacMask(par, 0xff);
	NVWriteDacWriteAddr(par, regnum);
	NVWriteDacData(par, red);
	NVWriteDacData(par, green);
	NVWriteDacData(par, blue);
}

static void nvidia_read_clut(struct nvidia_par *par,
			     u8 regnum, u8 * red, u8 * green, u8 * blue)
{
	NVWriteDacMask(par, 0xff);
	NVWriteDacReadAddr(par, regnum);
	*red = NVReadDacData(par);
	*green = NVReadDacData(par);
	*blue = NVReadDacData(par);
}

static int nvidia_panel_tweak(struct nvidia_par *par,
			      struct _riva_hw_state *state)
{
	int tweak = 0;

   if (par->paneltweak) {
	   tweak = par->paneltweak;
   } else {
	   /* begin flat panel hacks */
	   /* This is unfortunate, but some chips need this register
	      tweaked or else you get artifacts where adjacent pixels are
	      swapped.  There are no hard rules for what to set here so all
	      we can do is experiment and apply hacks. */

	   if(((par->Chipset & 0xffff) == 0x0328) && (state->bpp == 32)) {
		   /* At least one NV34 laptop needs this workaround. */
		   tweak = -1;
	   }

	   if((par->Chipset & 0xfff0) == 0x0310) {
		   tweak = 1;
	   }
	   /* end flat panel hacks */
   }

   return tweak;
}

static void nvidia_save_vga(struct nvidia_par *par,
			    struct _riva_hw_state *state)
{
	int i;

	NVTRACE_ENTER();
	NVLockUnlock(par, 0);

	NVUnloadStateExt(par, state);

	state->misc_output = NVReadMiscOut(par);

	for (i = 0; i < NUM_CRT_REGS; i++)
		state->crtc[i] = NVReadCrtc(par, i);

	for (i = 0; i < NUM_ATC_REGS; i++)
		state->attr[i] = NVReadAttr(par, i);

	for (i = 0; i < NUM_GRC_REGS; i++)
		state->gra[i] = NVReadGr(par, i);

	for (i = 0; i < NUM_SEQ_REGS; i++)
		state->seq[i] = NVReadSeq(par, i);
	NVTRACE_LEAVE();
}

#undef DUMP_REG

static void nvidia_write_regs(struct nvidia_par *par)
{
	struct _riva_hw_state *state = &par->ModeReg;
	int i;

	NVTRACE_ENTER();

	NVLoadStateExt(par, state);

	NVWriteMiscOut(par, state->misc_output);

	for (i = 1; i < NUM_SEQ_REGS; i++) {
#ifdef DUMP_REG
		printk(" SEQ[%02x] = %08x\n", i, state->seq[i]);
#endif
		NVWriteSeq(par, i, state->seq[i]);
	}

	/* Ensure CRTC registers 0-7 are unlocked by clearing bit 7 of CRTC[17] */
	NVWriteCrtc(par, 0x11, state->crtc[0x11] & ~0x80);

	for (i = 0; i < NUM_CRT_REGS; i++) {
		switch (i) {
		case 0x19:
		case 0x20 ... 0x40:
			break;
		default:
#ifdef DUMP_REG
			printk("CRTC[%02x] = %08x\n", i, state->crtc[i]);
#endif
			NVWriteCrtc(par, i, state->crtc[i]);
		}
	}

	for (i = 0; i < NUM_GRC_REGS; i++) {
#ifdef DUMP_REG
		printk(" GRA[%02x] = %08x\n", i, state->gra[i]);
#endif
		NVWriteGr(par, i, state->gra[i]);
	}

	for (i = 0; i < NUM_ATC_REGS; i++) {
#ifdef DUMP_REG
		printk("ATTR[%02x] = %08x\n", i, state->attr[i]);
#endif
		NVWriteAttr(par, i, state->attr[i]);
	}

	NVTRACE_LEAVE();
}

static void nvidia_vga_protect(struct nvidia_par *par, int on)
{
	unsigned char tmp;

	if (on) {
		/*
		 * Turn off screen and disable sequencer.
		 */
		tmp = NVReadSeq(par, 0x01);

		NVWriteSeq(par, 0x00, 0x01);		/* Synchronous Reset */
		NVWriteSeq(par, 0x01, tmp | 0x20);	/* disable the display */
	} else {
		/*
		 * Reenable sequencer, then turn on screen.
		 */

		tmp = NVReadSeq(par, 0x01);

		NVWriteSeq(par, 0x01, tmp & ~0x20);	/* reenable display */
		NVWriteSeq(par, 0x00, 0x03);		/* End Reset */
	}
}



static int nvidia_calc_regs(struct fb_info *info)
{
	struct nvidia_par *par = info->par;
	struct _riva_hw_state *state = &par->ModeReg;
	int i, depth = fb_get_color_depth(&info->var, &info->fix);
	int h_display = info->var.xres / 8 - 1;
	int h_start = (info->var.xres + info->var.right_margin) / 8 - 1;
	int h_end = (info->var.xres + info->var.right_margin +
		     info->var.hsync_len) / 8 - 1;
	int h_total = (info->var.xres + info->var.right_margin +
		       info->var.hsync_len + info->var.left_margin) / 8 - 5;
	int h_blank_s = h_display;
	int h_blank_e = h_total + 4;
	int v_display = info->var.yres - 1;
	int v_start = info->var.yres + info->var.lower_margin - 1;
	int v_end = (info->var.yres + info->var.lower_margin +
		     info->var.vsync_len) - 1;
	int v_total = (info->var.yres + info->var.lower_margin +
		       info->var.vsync_len + info->var.upper_margin) - 2;
	int v_blank_s = v_display;
	int v_blank_e = v_total + 1;

	/*
	 * Set all CRTC values.
	 */

	if (info->var.vmode & FB_VMODE_INTERLACED)
		v_total |= 1;

	if (par->FlatPanel == 1) {
		v_start = v_total - 3;
		v_end = v_total - 2;
		v_blank_s = v_start;
		h_start = h_total - 5;
		h_end = h_total - 2;
		h_blank_e = h_total + 4;
	}

	state->crtc[0x0] = Set8Bits(h_total);
	state->crtc[0x1] = Set8Bits(h_display);
	state->crtc[0x2] = Set8Bits(h_blank_s);
	state->crtc[0x3] = SetBitField(h_blank_e, 4: 0, 4:0)
		| SetBit(7);
	state->crtc[0x4] = Set8Bits(h_start);
	state->crtc[0x5] = SetBitField(h_blank_e, 5: 5, 7:7)
		| SetBitField(h_end, 4: 0, 4:0);
	state->crtc[0x6] = SetBitField(v_total, 7: 0, 7:0);
	state->crtc[0x7] = SetBitField(v_total, 8: 8, 0:0)
		| SetBitField(v_display, 8: 8, 1:1)
		| SetBitField(v_start, 8: 8, 2:2)
		| SetBitField(v_blank_s, 8: 8, 3:3)
		| SetBit(4)
		| SetBitField(v_total, 9: 9, 5:5)
		| SetBitField(v_display, 9: 9, 6:6)
		| SetBitField(v_start, 9: 9, 7:7);
	state->crtc[0x9] = SetBitField(v_blank_s, 9: 9, 5:5)
		| SetBit(6)
		| ((info->var.vmode & FB_VMODE_DOUBLE) ? 0x80 : 0x00);
	state->crtc[0x10] = Set8Bits(v_start);
	state->crtc[0x11] = SetBitField(v_end, 3: 0, 3:0) | SetBit(5);
	state->crtc[0x12] = Set8Bits(v_display);
	state->crtc[0x13] = ((info->var.xres_virtual / 8) *
			     (info->var.bits_per_pixel / 8));
	state->crtc[0x15] = Set8Bits(v_blank_s);
	state->crtc[0x16] = Set8Bits(v_blank_e);

	state->attr[0x10] = 0x01;

	if (par->Television)
		state->attr[0x11] = 0x00;

	state->screen = SetBitField(h_blank_e, 6: 6, 4:4)
		| SetBitField(v_blank_s, 10: 10, 3:3)
		| SetBitField(v_start, 10: 10, 2:2)
		| SetBitField(v_display, 10: 10, 1:1)
		| SetBitField(v_total, 10: 10, 0:0);

	state->horiz = SetBitField(h_total, 8: 8, 0:0)
		| SetBitField(h_display, 8: 8, 1:1)
		| SetBitField(h_blank_s, 8: 8, 2:2)
		| SetBitField(h_start, 8: 8, 3:3);

	state->extra = SetBitField(v_total, 11: 11, 0:0)
		| SetBitField(v_display, 11: 11, 2:2)
		| SetBitField(v_start, 11: 11, 4:4)
		| SetBitField(v_blank_s, 11: 11, 6:6);

	if (info->var.vmode & FB_VMODE_INTERLACED) {
		h_total = (h_total >> 1) & ~1;
		state->interlace = Set8Bits(h_total);
		state->horiz |= SetBitField(h_total, 8: 8, 4:4);
	} else {
		state->interlace = 0xff;	/* interlace off */
	}

	/*
	 * Calculate the extended registers.
	 */

	if (depth < 24)
		i = depth;
	else
		i = 32;

	if (par->Architecture >= NV_ARCH_10)
		par->CURSOR = (volatile u32 __iomem *)(info->screen_base +
						       par->CursorStart);

	if (info->var.sync & FB_SYNC_HOR_HIGH_ACT)
		state->misc_output &= ~0x40;
	else
		state->misc_output |= 0x40;
	if (info->var.sync & FB_SYNC_VERT_HIGH_ACT)
		state->misc_output &= ~0x80;
	else
		state->misc_output |= 0x80;

	NVCalcStateExt(par, state, i, info->var.xres_virtual,
		       info->var.xres, info->var.yres_virtual,
		       1000000000 / info->var.pixclock, info->var.vmode);

	state->scale = NV_RD32(par->PRAMDAC, 0x00000848) & 0xfff000ff;
	if (par->FlatPanel == 1) {
		state->pixel |= (1 << 7);

		if (!par->fpScaler || (par->fpWidth <= info->var.xres)
		    || (par->fpHeight <= info->var.yres)) {
			state->scale |= (1 << 8);
		}

		if (!par->crtcSync_read) {
			state->crtcSync = NV_RD32(par->PRAMDAC, 0x0828);
			par->crtcSync_read = 1;
		}

		par->PanelTweak = nvidia_panel_tweak(par, state);
	}

	state->vpll = state->pll;
	state->vpll2 = state->pll;
	state->vpllB = state->pllB;
	state->vpll2B = state->pllB;

	VGA_WR08(par->PCIO, 0x03D4, 0x1C);
	state->fifo = VGA_RD08(par->PCIO, 0x03D5) & ~(1<<5);

	if (par->CRTCnumber) {
		state->head = NV_RD32(par->PCRTC0, 0x00000860) & ~0x00001000;
		state->head2 = NV_RD32(par->PCRTC0, 0x00002860) | 0x00001000;
		state->crtcOwner = 3;
		state->pllsel |= 0x20000800;
		state->vpll = NV_RD32(par->PRAMDAC0, 0x00000508);
		if (par->twoStagePLL)
			state->vpllB = NV_RD32(par->PRAMDAC0, 0x00000578);
	} else if (par->twoHeads) {
		state->head = NV_RD32(par->PCRTC0, 0x00000860) | 0x00001000;
		state->head2 = NV_RD32(par->PCRTC0, 0x00002860) & ~0x00001000;
		state->crtcOwner = 0;
		state->vpll2 = NV_RD32(par->PRAMDAC0, 0x0520);
		if (par->twoStagePLL)
			state->vpll2B = NV_RD32(par->PRAMDAC0, 0x057C);
	}

	state->cursorConfig = 0x00000100;

	if (info->var.vmode & FB_VMODE_DOUBLE)
		state->cursorConfig |= (1 << 4);

	if (par->alphaCursor) {
		if ((par->Chipset & 0x0ff0) != 0x0110)
			state->cursorConfig |= 0x04011000;
		else
			state->cursorConfig |= 0x14011000;
		state->general |= (1 << 29);
	} else
		state->cursorConfig |= 0x02000000;

	if (par->twoHeads) {
		if ((par->Chipset & 0x0ff0) == 0x0110) {
			state->dither = NV_RD32(par->PRAMDAC, 0x0528) &
			    ~0x00010000;
			if (par->FPDither)
				state->dither |= 0x00010000;
		} else {
			state->dither = NV_RD32(par->PRAMDAC, 0x083C) & ~1;
			if (par->FPDither)
				state->dither |= 1;
		}
	}

	state->timingH = 0;
	state->timingV = 0;
	state->displayV = info->var.xres;

	return 0;
}

static void nvidia_init_vga(struct fb_info *info)
{
	struct nvidia_par *par = info->par;
	struct _riva_hw_state *state = &par->ModeReg;
	int i;

	for (i = 0; i < 0x10; i++)
		state->attr[i] = i;
	state->attr[0x10] = 0x41;
	state->attr[0x11] = 0xff;
	state->attr[0x12] = 0x0f;
	state->attr[0x13] = 0x00;
	state->attr[0x14] = 0x00;

	memset(state->crtc, 0x00, NUM_CRT_REGS);
	state->crtc[0x0a] = 0x20;
	state->crtc[0x17] = 0xe3;
	state->crtc[0x18] = 0xff;
	state->crtc[0x28] = 0x40;

	memset(state->gra, 0x00, NUM_GRC_REGS);
	state->gra[0x05] = 0x40;
	state->gra[0x06] = 0x05;
	state->gra[0x07] = 0x0f;
	state->gra[0x08] = 0xff;

	state->seq[0x00] = 0x03;
	state->seq[0x01] = 0x01;
	state->seq[0x02] = 0x0f;
	state->seq[0x03] = 0x00;
	state->seq[0x04] = 0x0e;

	state->misc_output = 0xeb;
}

static int nvidiafb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct nvidia_par *par = info->par;
	u8 data[MAX_CURS * MAX_CURS / 8];
	int i, set = cursor->set;
	u16 fg, bg;

	if (cursor->image.width > MAX_CURS || cursor->image.height > MAX_CURS)
		return -ENXIO;

	NVShowHideCursor(par, 0);

	if (par->cursor_reset) {
		set = FB_CUR_SETALL;
		par->cursor_reset = 0;
	}

	if (set & FB_CUR_SETSIZE)
		memset_io(par->CURSOR, 0, MAX_CURS * MAX_CURS * 2);

	if (set & FB_CUR_SETPOS) {
		u32 xx, yy, temp;

		yy = cursor->image.dy - info->var.yoffset;
		xx = cursor->image.dx - info->var.xoffset;
		temp = xx & 0xFFFF;
		temp |= yy << 16;

		NV_WR32(par->PRAMDAC, 0x0000300, temp);
	}

	if (set & (FB_CUR_SETSHAPE | FB_CUR_SETCMAP | FB_CUR_SETIMAGE)) {
		u32 bg_idx = cursor->image.bg_color;
		u32 fg_idx = cursor->image.fg_color;
		u32 s_pitch = (cursor->image.width + 7) >> 3;
		u32 d_pitch = MAX_CURS / 8;
		u8 *dat = (u8 *) cursor->image.data;
		u8 *msk = (u8 *) cursor->mask;
		u8 *src;

		src = kmalloc(s_pitch * cursor->image.height, GFP_ATOMIC);

		if (src) {
			switch (cursor->rop) {
			case ROP_XOR:
				for (i = 0; i < s_pitch * cursor->image.height; i++)
					src[i] = dat[i] ^ msk[i];
				break;
			case ROP_COPY:
			default:
				for (i = 0; i < s_pitch * cursor->image.height; i++)
					src[i] = dat[i] & msk[i];
				break;
			}

			fb_pad_aligned_buffer(data, d_pitch, src, s_pitch,
						cursor->image.height);

			bg = ((info->cmap.red[bg_idx] & 0xf8) << 7) |
			    ((info->cmap.green[bg_idx] & 0xf8) << 2) |
			    ((info->cmap.blue[bg_idx] & 0xf8) >> 3) | 1 << 15;

			fg = ((info->cmap.red[fg_idx] & 0xf8) << 7) |
			    ((info->cmap.green[fg_idx] & 0xf8) << 2) |
			    ((info->cmap.blue[fg_idx] & 0xf8) >> 3) | 1 << 15;

			NVLockUnlock(par, 0);

			nvidiafb_load_cursor_image(par, data, bg, fg,
						   cursor->image.width,
						   cursor->image.height);
			kfree(src);
		}
	}

	if (cursor->enable)
		NVShowHideCursor(par, 1);

	return 0;
}

static int nvidiafb_set_par(struct fb_info *info)
{
	struct nvidia_par *par = info->par;

	NVTRACE_ENTER();

	NVLockUnlock(par, 1);
	if (!par->FlatPanel || !par->twoHeads)
		par->FPDither = 0;

	if (par->FPDither < 0) {
		if ((par->Chipset & 0x0ff0) == 0x0110)
			par->FPDither = !!(NV_RD32(par->PRAMDAC, 0x0528)
					   & 0x00010000);
		else
			par->FPDither = !!(NV_RD32(par->PRAMDAC, 0x083C) & 1);
		printk(KERN_INFO PFX "Flat panel dithering %s\n",
		       par->FPDither ? "enabled" : "disabled");
	}

	info->fix.visual = (info->var.bits_per_pixel == 8) ?
	    FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;

	nvidia_init_vga(info);
	nvidia_calc_regs(info);

	NVLockUnlock(par, 0);
	if (par->twoHeads) {
		VGA_WR08(par->PCIO, 0x03D4, 0x44);
		VGA_WR08(par->PCIO, 0x03D5, par->ModeReg.crtcOwner);
		NVLockUnlock(par, 0);
	}

	nvidia_vga_protect(par, 1);

	nvidia_write_regs(par);

#if defined (__BIG_ENDIAN)
	/* turn on LFB swapping */
	{
		unsigned char tmp;

		VGA_WR08(par->PCIO, 0x3d4, 0x46);
		tmp = VGA_RD08(par->PCIO, 0x3d5);
		tmp |= (1 << 7);
		VGA_WR08(par->PCIO, 0x3d5, tmp);
    }
#endif

	info->fix.line_length = (info->var.xres_virtual *
				 info->var.bits_per_pixel) >> 3;
	if (info->var.accel_flags) {
		info->fbops->fb_imageblit = nvidiafb_imageblit;
		info->fbops->fb_fillrect = nvidiafb_fillrect;
		info->fbops->fb_copyarea = nvidiafb_copyarea;
		info->fbops->fb_sync = nvidiafb_sync;
		info->pixmap.scan_align = 4;
		info->flags &= ~FBINFO_HWACCEL_DISABLED;
		NVResetGraphics(info);
	} else {
		info->fbops->fb_imageblit = cfb_imageblit;
		info->fbops->fb_fillrect = cfb_fillrect;
		info->fbops->fb_copyarea = cfb_copyarea;
		info->fbops->fb_sync = NULL;
		info->pixmap.scan_align = 1;
		info->flags |= FBINFO_HWACCEL_DISABLED;
	}

	par->cursor_reset = 1;

	nvidia_vga_protect(par, 0);

	NVTRACE_LEAVE();
	return 0;
}

static int nvidiafb_setcolreg(unsigned regno, unsigned red, unsigned green,
			      unsigned blue, unsigned transp,
			      struct fb_info *info)
{
	struct nvidia_par *par = info->par;
	int i;

	NVTRACE_ENTER();
	if (regno >= (1 << info->var.green.length))
		return -EINVAL;

	if (info->var.grayscale) {
		/* gray = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}

	if (regno < 16 && info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		((u32 *) info->pseudo_palette)[regno] =
		    (regno << info->var.red.offset) |
		    (regno << info->var.green.offset) |
		    (regno << info->var.blue.offset);
	}

	switch (info->var.bits_per_pixel) {
	case 8:
		/* "transparent" stuff is completely ignored. */
		nvidia_write_clut(par, regno, red >> 8, green >> 8, blue >> 8);
		break;
	case 16:
		if (info->var.green.length == 5) {
			for (i = 0; i < 8; i++) {
				nvidia_write_clut(par, regno * 8 + i, red >> 8,
						  green >> 8, blue >> 8);
			}
		} else {
			u8 r, g, b;

			if (regno < 32) {
				for (i = 0; i < 8; i++) {
					nvidia_write_clut(par, regno * 8 + i,
							  red >> 8, green >> 8,
							  blue >> 8);
				}
			}

			nvidia_read_clut(par, regno * 4, &r, &g, &b);

			for (i = 0; i < 4; i++)
				nvidia_write_clut(par, regno * 4 + i, r,
						  green >> 8, b);
		}
		break;
	case 32:
		nvidia_write_clut(par, regno, red >> 8, green >> 8, blue >> 8);
		break;
	default:
		/* do nothing */
		break;
	}

	NVTRACE_LEAVE();
	return 0;
}

static int nvidiafb_check_var(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct nvidia_par *par = info->par;
	int memlen, vramlen, mode_valid = 0;
	int pitch, err = 0;

	NVTRACE_ENTER();

	var->transp.offset = 0;
	var->transp.length = 0;

	var->xres &= ~7;

	if (var->bits_per_pixel <= 8)
		var->bits_per_pixel = 8;
	else if (var->bits_per_pixel <= 16)
		var->bits_per_pixel = 16;
	else
		var->bits_per_pixel = 32;

	switch (var->bits_per_pixel) {
	case 8:
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 16:
		var->green.length = (var->green.length < 6) ? 5 : 6;
		var->red.length = 5;
		var->blue.length = 5;
		var->transp.length = 6 - var->green.length;
		var->blue.offset = 0;
		var->green.offset = 5;
		var->red.offset = 5 + var->green.length;
		var->transp.offset = (5 + var->red.offset) & 15;
		break;
	case 32:		/* RGBA 8888 */
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.length = 8;
		var->transp.offset = 24;
		break;
	}

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	if (!info->monspecs.hfmax || !info->monspecs.vfmax ||
	    !info->monspecs.dclkmax || !fb_validate_mode(var, info))
		mode_valid = 1;

	/* calculate modeline if supported by monitor */
	if (!mode_valid && info->monspecs.gtf) {
		if (!fb_get_mode(FB_MAXTIMINGS, 0, var, info))
			mode_valid = 1;
	}

	if (!mode_valid) {
		struct fb_videomode *mode;

		mode = fb_find_best_mode(var, &info->modelist);
		if (mode) {
			fb_videomode_to_var(var, mode);
			mode_valid = 1;
		}
	}

	if (!mode_valid && info->monspecs.modedb_len)
		return -EINVAL;

	if (par->fpWidth && par->fpHeight && (par->fpWidth < var->xres ||
					      par->fpHeight < var->yres))
		return -EINVAL;

	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;

	var->xres_virtual = (var->xres_virtual + 63) & ~63;

	vramlen = info->screen_size;
	pitch = ((var->xres_virtual * var->bits_per_pixel) + 7) / 8;
	memlen = pitch * var->yres_virtual;

	if (memlen > vramlen) {
		var->yres_virtual = vramlen / pitch;

		if (var->yres_virtual < var->yres) {
			var->yres_virtual = var->yres;
			var->xres_virtual = vramlen / var->yres_virtual;
			var->xres_virtual /= var->bits_per_pixel / 8;
			var->xres_virtual &= ~63;
			pitch = (var->xres_virtual *
				 var->bits_per_pixel + 7) / 8;
			memlen = pitch * var->yres;

			if (var->xres_virtual < var->xres) {
				printk("nvidiafb: required video memory, "
				       "%d bytes, for %dx%d-%d (virtual) "
				       "is out of range\n",
				       memlen, var->xres_virtual,
				       var->yres_virtual, var->bits_per_pixel);
				err = -ENOMEM;
			}
		}
	}

	if (var->accel_flags) {
		if (var->yres_virtual > 0x7fff)
			var->yres_virtual = 0x7fff;
		if (var->xres_virtual > 0x7fff)
			var->xres_virtual = 0x7fff;
	}

	var->xres_virtual &= ~63;

	NVTRACE_LEAVE();

	return err;
}

static int nvidiafb_pan_display(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	struct nvidia_par *par = info->par;
	u32 total;

	total = var->yoffset * info->fix.line_length + var->xoffset;

	NVSetStartAddress(par, total);

	return 0;
}

static int nvidiafb_blank(int blank, struct fb_info *info)
{
	struct nvidia_par *par = info->par;
	unsigned char tmp, vesa;

	tmp = NVReadSeq(par, 0x01) & ~0x20;	/* screen on/off */
	vesa = NVReadCrtc(par, 0x1a) & ~0xc0;	/* sync on/off */

	NVTRACE_ENTER();

	if (blank)
		tmp |= 0x20;

	switch (blank) {
	case FB_BLANK_UNBLANK:
	case FB_BLANK_NORMAL:
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		vesa |= 0x80;
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		vesa |= 0x40;
		break;
	case FB_BLANK_POWERDOWN:
		vesa |= 0xc0;
		break;
	}

	NVWriteSeq(par, 0x01, tmp);
	NVWriteCrtc(par, 0x1a, vesa);

#ifdef CONFIG_PMAC_BACKLIGHT
	if (par->FlatPanel && _machine == _MACH_Pmac) {
		set_backlight_enable(!blank);
	}
#endif

	NVTRACE_LEAVE();

	return 0;
}

static struct fb_ops nvidia_fb_ops = {
	.owner          = THIS_MODULE,
	.fb_check_var   = nvidiafb_check_var,
	.fb_set_par     = nvidiafb_set_par,
	.fb_setcolreg   = nvidiafb_setcolreg,
	.fb_pan_display = nvidiafb_pan_display,
	.fb_blank       = nvidiafb_blank,
	.fb_fillrect    = nvidiafb_fillrect,
	.fb_copyarea    = nvidiafb_copyarea,
	.fb_imageblit   = nvidiafb_imageblit,
	.fb_cursor      = nvidiafb_cursor,
	.fb_sync        = nvidiafb_sync,
};

static int __devinit nvidia_set_fbinfo(struct fb_info *info)
{
	struct fb_monspecs *specs = &info->monspecs;
	struct fb_videomode modedb;
	struct nvidia_par *par = info->par;
	int lpitch;

	NVTRACE_ENTER();
	info->flags = FBINFO_DEFAULT
	    | FBINFO_HWACCEL_IMAGEBLIT
	    | FBINFO_HWACCEL_FILLRECT
	    | FBINFO_HWACCEL_COPYAREA
	    | FBINFO_HWACCEL_YPAN;

	fb_videomode_to_modelist(info->monspecs.modedb,
				 info->monspecs.modedb_len, &info->modelist);
	fb_var_to_videomode(&modedb, &nvidiafb_default_var);

	switch (bpp) {
	case 0 ... 8:
		bpp = 8;
		break;
	case 9 ... 16:
		bpp = 16;
		break;
	default:
		bpp = 32;
		break;
	}

	if (specs->modedb != NULL) {
		struct fb_videomode *modedb;

		modedb = fb_find_best_display(specs, &info->modelist);
		fb_videomode_to_var(&nvidiafb_default_var, modedb);
		nvidiafb_default_var.bits_per_pixel = bpp;
	} else if (par->fpWidth && par->fpHeight) {
		char buf[16];

		memset(buf, 0, 16);
		snprintf(buf, 15, "%dx%dMR", par->fpWidth, par->fpHeight);
		fb_find_mode(&nvidiafb_default_var, info, buf, specs->modedb,
			     specs->modedb_len, &modedb, bpp);
	}

	if (mode_option)
		fb_find_mode(&nvidiafb_default_var, info, mode_option,
			     specs->modedb, specs->modedb_len, &modedb, bpp);

	info->var = nvidiafb_default_var;
	info->fix.visual = (info->var.bits_per_pixel == 8) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;
	info->pseudo_palette = par->pseudo_palette;
	fb_alloc_cmap(&info->cmap, 256, 0);
	fb_destroy_modedb(info->monspecs.modedb);
	info->monspecs.modedb = NULL;

	/* maximize virtual vertical length */
	lpitch = info->var.xres_virtual *
		((info->var.bits_per_pixel + 7) >> 3);
	info->var.yres_virtual = info->screen_size / lpitch;

	info->pixmap.scan_align = 4;
	info->pixmap.buf_align = 4;
	info->pixmap.access_align = 32;
	info->pixmap.size = 8 * 1024;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;

	if (!hwcur)
	    info->fbops->fb_cursor = NULL;

	info->var.accel_flags = (!noaccel);

	switch (par->Architecture) {
	case NV_ARCH_04:
		info->fix.accel = FB_ACCEL_NV4;
		break;
	case NV_ARCH_10:
		info->fix.accel = FB_ACCEL_NV_10;
		break;
	case NV_ARCH_20:
		info->fix.accel = FB_ACCEL_NV_20;
		break;
	case NV_ARCH_30:
		info->fix.accel = FB_ACCEL_NV_30;
		break;
	case NV_ARCH_40:
		info->fix.accel = FB_ACCEL_NV_40;
		break;
	}

	NVTRACE_LEAVE();

	return nvidiafb_check_var(&info->var, info);
}

static u32 __devinit nvidia_get_chipset(struct fb_info *info)
{
	struct nvidia_par *par = info->par;
	u32 id = (par->pci_dev->vendor << 16) | par->pci_dev->device;

	printk("nvidiafb: PCI id - %x\n", id);
	if ((id & 0xfff0) == 0x00f0) {
		/* pci-e */
		printk("nvidiafb: PCI-E card\n");
		id = NV_RD32(par->REGS, 0x1800);

		if ((id & 0x0000ffff) == 0x000010DE)
			id = 0x10DE0000 | (id >> 16);
		else if ((id & 0xffff0000) == 0xDE100000) /* wrong endian */
			id = 0x10DE0000 | ((id << 8) & 0x0000ff00) |
                            ((id >> 8) & 0x000000ff);
	}

	printk("nvidiafb: Actual id - %x\n", id);
	return id;
}

static u32 __devinit nvidia_get_arch(struct fb_info *info)
{
	struct nvidia_par *par = info->par;
	u32 arch = 0;

	switch (par->Chipset & 0x0ff0) {
	case 0x0100:		/* GeForce 256 */
	case 0x0110:		/* GeForce2 MX */
	case 0x0150:		/* GeForce2 */
	case 0x0170:		/* GeForce4 MX */
	case 0x0180:		/* GeForce4 MX (8x AGP) */
	case 0x01A0:		/* nForce */
	case 0x01F0:		/* nForce2 */
		arch = NV_ARCH_10;
		break;
	case 0x0200:		/* GeForce3 */
	case 0x0250:		/* GeForce4 Ti */
	case 0x0280:		/* GeForce4 Ti (8x AGP) */
		arch = NV_ARCH_20;
		break;
	case 0x0300:		/* GeForceFX 5800 */
	case 0x0310:		/* GeForceFX 5600 */
	case 0x0320:		/* GeForceFX 5200 */
	case 0x0330:		/* GeForceFX 5900 */
	case 0x0340:		/* GeForceFX 5700 */
		arch = NV_ARCH_30;
		break;
	case 0x0040:
	case 0x00C0:
	case 0x0120:
	case 0x0130:
	case 0x0140:
	case 0x0160:
	case 0x01D0:
	case 0x0090:
	case 0x0210:
	case 0x0220:
	case 0x0230:
	case 0x0290:
	case 0x0390:
		arch = NV_ARCH_40;
		break;
	case 0x0020:		/* TNT, TNT2 */
		arch = NV_ARCH_04;
		break;
	default:		/* unknown architecture */
		break;
	}

	return arch;
}

static int __devinit nvidiafb_probe(struct pci_dev *pd,
				    const struct pci_device_id *ent)
{
	struct nvidia_par *par;
	struct fb_info *info;
	unsigned short cmd;


	NVTRACE_ENTER();
	assert(pd != NULL);

	info = framebuffer_alloc(sizeof(struct nvidia_par), &pd->dev);

	if (!info)
		goto err_out;

	par = info->par;
	par->pci_dev = pd;

	info->pixmap.addr = kmalloc(8 * 1024, GFP_KERNEL);

	if (info->pixmap.addr == NULL)
		goto err_out_kfree;

	memset(info->pixmap.addr, 0, 8 * 1024);

	if (pci_enable_device(pd)) {
		printk(KERN_ERR PFX "cannot enable PCI device\n");
		goto err_out_enable;
	}

	if (pci_request_regions(pd, "nvidiafb")) {
		printk(KERN_ERR PFX "cannot request PCI regions\n");
		goto err_out_request;
	}

	par->FlatPanel = flatpanel;
	if (flatpanel == 1)
		printk(KERN_INFO PFX "flatpanel support enabled\n");
	par->FPDither = fpdither;

	par->CRTCnumber = forceCRTC;
	par->FpScale = (!noscale);
	par->paneltweak = paneltweak;

	/* enable IO and mem if not already done */
	pci_read_config_word(pd, PCI_COMMAND, &cmd);
	cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
	pci_write_config_word(pd, PCI_COMMAND, cmd);

	nvidiafb_fix.mmio_start = pci_resource_start(pd, 0);
	nvidiafb_fix.smem_start = pci_resource_start(pd, 1);
	nvidiafb_fix.mmio_len = pci_resource_len(pd, 0);

	par->REGS = ioremap(nvidiafb_fix.mmio_start, nvidiafb_fix.mmio_len);

	if (!par->REGS) {
		printk(KERN_ERR PFX "cannot ioremap MMIO base\n");
		goto err_out_free_base0;
	}

	par->Chipset = nvidia_get_chipset(info);
	printk(KERN_INFO PFX "nVidia device/chipset %X\n", par->Chipset);
	par->Architecture = nvidia_get_arch(info);

	if (par->Architecture == 0) {
		printk(KERN_ERR PFX "unknown NV_ARCH\n");
		goto err_out_arch;
	}

	sprintf(nvidiafb_fix.id, "NV%x", (pd->device & 0x0ff0) >> 4);

	if (NVCommonSetup(info))
		goto err_out_arch;

	par->FbAddress = nvidiafb_fix.smem_start;
	par->FbMapSize = par->RamAmountKBytes * 1024;
	if (vram && vram * 1024 * 1024 < par->FbMapSize)
		par->FbMapSize = vram * 1024 * 1024;

	/* Limit amount of vram to 64 MB */
	if (par->FbMapSize > 64 * 1024 * 1024)
		par->FbMapSize = 64 * 1024 * 1024;

	if(par->Architecture >= NV_ARCH_40)
  	        par->FbUsableSize = par->FbMapSize - (560 * 1024);
	else
		par->FbUsableSize = par->FbMapSize - (128 * 1024);
	par->ScratchBufferSize = (par->Architecture < NV_ARCH_10) ? 8 * 1024 :
	    16 * 1024;
	par->ScratchBufferStart = par->FbUsableSize - par->ScratchBufferSize;
	par->CursorStart = par->FbUsableSize + (32 * 1024);

	info->screen_base = ioremap(nvidiafb_fix.smem_start, par->FbMapSize);
	info->screen_size = par->FbUsableSize;
	nvidiafb_fix.smem_len = par->RamAmountKBytes * 1024;

	if (!info->screen_base) {
		printk(KERN_ERR PFX "cannot ioremap FB base\n");
		goto err_out_free_base1;
	}

	par->FbStart = info->screen_base;

#ifdef CONFIG_MTRR
	if (!nomtrr) {
		par->mtrr.vram = mtrr_add(nvidiafb_fix.smem_start,
					  par->RamAmountKBytes * 1024,
					  MTRR_TYPE_WRCOMB, 1);
		if (par->mtrr.vram < 0) {
			printk(KERN_ERR PFX "unable to setup MTRR\n");
		} else {
			par->mtrr.vram_valid = 1;
			/* let there be speed */
			printk(KERN_INFO PFX "MTRR set to ON\n");
		}
	}
#endif				/* CONFIG_MTRR */

	info->fbops = &nvidia_fb_ops;
	info->fix = nvidiafb_fix;

	if (nvidia_set_fbinfo(info) < 0) {
		printk(KERN_ERR PFX "error setting initial video mode\n");
		goto err_out_iounmap_fb;
	}

	nvidia_save_vga(par, &par->SavedReg);

	if (register_framebuffer(info) < 0) {
		printk(KERN_ERR PFX "error registering nVidia framebuffer\n");
		goto err_out_iounmap_fb;
	}

	pci_set_drvdata(pd, info);

	printk(KERN_INFO PFX
	       "PCI nVidia %s framebuffer (%dMB @ 0x%lX)\n",
	       info->fix.id,
	       par->FbMapSize / (1024 * 1024), info->fix.smem_start);
#ifdef CONFIG_PMAC_BACKLIGHT
	if (par->FlatPanel && _machine == _MACH_Pmac)
		register_backlight_controller(&nvidia_backlight_controller,
					      par, "mnca");
#endif
	NVTRACE_LEAVE();
	return 0;

err_out_iounmap_fb:
	iounmap(info->screen_base);
err_out_free_base1:
	fb_destroy_modedb(info->monspecs.modedb);
	nvidia_delete_i2c_busses(par);
err_out_arch:
	iounmap(par->REGS);
err_out_free_base0:
	pci_release_regions(pd);
err_out_request:
	pci_disable_device(pd);
err_out_enable:
	kfree(info->pixmap.addr);
err_out_kfree:
	framebuffer_release(info);
err_out:
	return -ENODEV;
}

static void __exit nvidiafb_remove(struct pci_dev *pd)
{
	struct fb_info *info = pci_get_drvdata(pd);
	struct nvidia_par *par = info->par;

	NVTRACE_ENTER();
	if (!info)
		return;

	unregister_framebuffer(info);
#ifdef CONFIG_MTRR
	if (par->mtrr.vram_valid)
		mtrr_del(par->mtrr.vram, info->fix.smem_start,
			 info->fix.smem_len);
#endif				/* CONFIG_MTRR */

	iounmap(info->screen_base);
	fb_destroy_modedb(info->monspecs.modedb);
	nvidia_delete_i2c_busses(par);
	iounmap(par->REGS);
	pci_release_regions(pd);
	pci_disable_device(pd);
	kfree(info->pixmap.addr);
	framebuffer_release(info);
	pci_set_drvdata(pd, NULL);
	NVTRACE_LEAVE();
}

/* ------------------------------------------------------------------------- *
 *
 * initialization
 *
 * ------------------------------------------------------------------------- */

#ifndef MODULE
static int __devinit nvidiafb_setup(char *options)
{
	char *this_opt;

	NVTRACE_ENTER();
	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "forceCRTC", 9)) {
			char *p;

			p = this_opt + 9;
			if (!*p || !*(++p))
				continue;
			forceCRTC = *p - '0';
			if (forceCRTC < 0 || forceCRTC > 1)
				forceCRTC = -1;
		} else if (!strncmp(this_opt, "flatpanel", 9)) {
			flatpanel = 1;
		} else if (!strncmp(this_opt, "hwcur", 5)) {
			hwcur = 1;
		} else if (!strncmp(this_opt, "noaccel", 6)) {
			noaccel = 1;
		} else if (!strncmp(this_opt, "noscale", 7)) {
			noscale = 1;
		} else if (!strncmp(this_opt, "paneltweak:", 11)) {
			paneltweak = simple_strtoul(this_opt+11, NULL, 0);
		} else if (!strncmp(this_opt, "vram:", 5)) {
			vram = simple_strtoul(this_opt+5, NULL, 0);
#ifdef CONFIG_MTRR
		} else if (!strncmp(this_opt, "nomtrr", 6)) {
			nomtrr = 1;
#endif
		} else if (!strncmp(this_opt, "fpdither:", 9)) {
			fpdither = simple_strtol(this_opt+9, NULL, 0);
		} else if (!strncmp(this_opt, "bpp:", 4)) {
			bpp = simple_strtoul(this_opt+4, NULL, 0);
		} else
			mode_option = this_opt;
	}
	NVTRACE_LEAVE();
	return 0;
}
#endif				/* !MODULE */

static struct pci_driver nvidiafb_driver = {
	.name = "nvidiafb",
	.id_table = nvidiafb_pci_tbl,
	.probe = nvidiafb_probe,
	.remove = __exit_p(nvidiafb_remove),
};

/* ------------------------------------------------------------------------- *
 *
 * modularization
 *
 * ------------------------------------------------------------------------- */

static int __devinit nvidiafb_init(void)
{
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("nvidiafb", &option))
		return -ENODEV;
	nvidiafb_setup(option);
#endif
	return pci_register_driver(&nvidiafb_driver);
}

module_init(nvidiafb_init);

#ifdef MODULE
static void __exit nvidiafb_exit(void)
{
	pci_unregister_driver(&nvidiafb_driver);
}

module_exit(nvidiafb_exit);

module_param(flatpanel, int, 0);
MODULE_PARM_DESC(flatpanel,
		 "Enables experimental flat panel support for some chipsets. "
		 "(0=disabled, 1=enabled, -1=autodetect) (default=-1)");
module_param(fpdither, int, 0);
MODULE_PARM_DESC(fpdither,
		 "Enables dithering of flat panel for 6 bits panels. "
		 "(0=disabled, 1=enabled, -1=autodetect) (default=-1)");
module_param(hwcur, int, 0);
MODULE_PARM_DESC(hwcur,
		 "Enables hardware cursor implementation. (0 or 1=enabled) "
		 "(default=0)");
module_param(noaccel, int, 0);
MODULE_PARM_DESC(noaccel,
		 "Disables hardware acceleration. (0 or 1=disable) "
		 "(default=0)");
module_param(noscale, int, 0);
MODULE_PARM_DESC(noscale,
		 "Disables screen scaleing. (0 or 1=disable) "
		 "(default=0, do scaling)");
module_param(paneltweak, int, 0);
MODULE_PARM_DESC(paneltweak,
		 "Tweak display settings for flatpanels. "
		 "(default=0, no tweaks)");
module_param(forceCRTC, int, 0);
MODULE_PARM_DESC(forceCRTC,
		 "Forces usage of a particular CRTC in case autodetection "
		 "fails. (0 or 1) (default=autodetect)");
module_param(vram, int, 0);
MODULE_PARM_DESC(vram,
		 "amount of framebuffer memory to remap in MiB"
		 "(default=0 - remap entire memory)");
module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option, "Specify initial video mode");
module_param(bpp, int, 0);
MODULE_PARM_DESC(bpp, "pixel width in bits"
		 "(default=8)");
#ifdef CONFIG_MTRR
module_param(nomtrr, bool, 0);
MODULE_PARM_DESC(nomtrr, "Disables MTRR support (0 or 1=disabled) "
		 "(default=0)");
#endif

MODULE_AUTHOR("Antonino Daplas");
MODULE_DESCRIPTION("Framebuffer driver for nVidia graphics chipset");
MODULE_LICENSE("GPL");
#endif				/* MODULE */

