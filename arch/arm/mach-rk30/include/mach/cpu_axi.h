#ifndef __MACH_CPU_AXI_H
#define __MACH_CPU_AXI_H

#include <plat/cpu_axi.h>

#define CPU_AXI_BUS_BASE                RK30_CPU_AXI_BUS_BASE

#define CPU_AXI_CPU0_QOS_BASE           (CPU_AXI_BUS_BASE + 0x0080)
#define CPU_AXI_DMAC_QOS_BASE           (CPU_AXI_BUS_BASE + 0x0100)
#define CPU_AXI_CPU1R_QOS_BASE          (CPU_AXI_BUS_BASE + 0x0180)
#define CPU_AXI_CPU1W_QOS_BASE          (CPU_AXI_BUS_BASE + 0x0380)
#define CPU_AXI_PERI_QOS_BASE           (CPU_AXI_BUS_BASE + 0x4000)
#define CPU_AXI_GPU_QOS_BASE            (CPU_AXI_BUS_BASE + 0x5000)
#define CPU_AXI_VPU_QOS_BASE            (CPU_AXI_BUS_BASE + 0x6000)
#define CPU_AXI_LCDC0_QOS_BASE          (CPU_AXI_BUS_BASE + 0x7000)
#define CPU_AXI_CIF0_QOS_BASE           (CPU_AXI_BUS_BASE + 0x7080)
#define CPU_AXI_IPP_QOS_BASE            (CPU_AXI_BUS_BASE + 0x7100)
#define CPU_AXI_LCDC1_QOS_BASE          (CPU_AXI_BUS_BASE + 0x7180)
#define CPU_AXI_CIF1_QOS_BASE           (CPU_AXI_BUS_BASE + 0x7200)
#define CPU_AXI_RGA_QOS_BASE            (CPU_AXI_BUS_BASE + 0x7280)

#endif
