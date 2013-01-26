#ifndef __MACH_CPU_AXI_H
#define __MACH_CPU_AXI_H

#define CPU_AXI_QOS_PRIORITY    0x08
#define CPU_AXI_QOS_MODE        0x0c
#define CPU_AXI_QOS_BANDWIDTH   0x10
#define CPU_AXI_QOS_SATURATION  0x14

#define CPU_AXI_QOS_MODE_NONE           0
#define CPU_AXI_QOS_MODE_FIXED          1
#define CPU_AXI_QOS_MODE_LIMITER        2
#define CPU_AXI_QOS_MODE_REGULATOR      3

#define CPU_AXI_QOS_PRIORITY_LEVEL(h, l)        (((h & 3) << 2) | (l & 3))
#define CPU_AXI_SET_QOS_PRIORITY(h, l, NAME) \
	writel_relaxed(CPU_AXI_QOS_PRIORITY_LEVEL(h, l), CPU_AXI_##NAME##_QOS_BASE + CPU_AXI_QOS_PRIORITY)
#define CPU_AXI_QOS_NUM_REGS 4
#define CPU_AXI_SAVE_QOS(array, NAME) do { \
	array[0] = readl_relaxed(CPU_AXI_##NAME##_QOS_BASE + CPU_AXI_QOS_PRIORITY); \
	array[1] = readl_relaxed(CPU_AXI_##NAME##_QOS_BASE + CPU_AXI_QOS_MODE); \
	array[2] = readl_relaxed(CPU_AXI_##NAME##_QOS_BASE + CPU_AXI_QOS_BANDWIDTH); \
	array[3] = readl_relaxed(CPU_AXI_##NAME##_QOS_BASE + CPU_AXI_QOS_SATURATION); \
} while (0)
#define CPU_AXI_RESTORE_QOS(array, NAME) do { \
	writel_relaxed(array[0], CPU_AXI_##NAME##_QOS_BASE + CPU_AXI_QOS_PRIORITY); \
	writel_relaxed(array[1], CPU_AXI_##NAME##_QOS_BASE + CPU_AXI_QOS_MODE); \
	writel_relaxed(array[2], CPU_AXI_##NAME##_QOS_BASE + CPU_AXI_QOS_BANDWIDTH); \
	writel_relaxed(array[3], CPU_AXI_##NAME##_QOS_BASE + CPU_AXI_QOS_SATURATION); \
} while (0)

#define CPU_AXI_LCDC0_QOS_BASE          (RK30_CPU_AXI_BUS_BASE + 0x7000)
#define CPU_AXI_CIF0_QOS_BASE           (RK30_CPU_AXI_BUS_BASE + 0x7080)
#define CPU_AXI_IPP_QOS_BASE            (RK30_CPU_AXI_BUS_BASE + 0x7100)
#define CPU_AXI_LCDC1_QOS_BASE          (RK30_CPU_AXI_BUS_BASE + 0x7180)
#define CPU_AXI_CIF1_QOS_BASE           (RK30_CPU_AXI_BUS_BASE + 0x7200)
#define CPU_AXI_RGA_QOS_BASE            (RK30_CPU_AXI_BUS_BASE + 0x7280)

#define CPU_AXI_PERI_QOS_BASE           (RK30_CPU_AXI_BUS_BASE + 0x4000)

#define CPU_AXI_GPU_QOS_BASE            (RK30_CPU_AXI_BUS_BASE + 0x5000)

#define CPU_AXI_VPU_QOS_BASE            (RK30_CPU_AXI_BUS_BASE + 0x6000)

#ifdef CONFIG_ARCH_RK3188
#define CPU_AXI_DMAC_QOS_BASE           (RK30_CPU_AXI_BUS_BASE + 0x1000)
#define CPU_AXI_CPU0_QOS_BASE           (RK30_CPU_AXI_BUS_BASE + 0x2000)
#define CPU_AXI_CPU1R_QOS_BASE          (RK30_CPU_AXI_BUS_BASE + 0x2080)
#define CPU_AXI_CPU1W_QOS_BASE          (RK30_CPU_AXI_BUS_BASE + 0x2100)
#else
#define CPU_AXI_CPU0_QOS_BASE           (RK30_CPU_AXI_BUS_BASE + 0x0080)
#define CPU_AXI_DMAC_QOS_BASE           (RK30_CPU_AXI_BUS_BASE + 0x0100)
#define CPU_AXI_CPU1R_QOS_BASE          (RK30_CPU_AXI_BUS_BASE + 0x0180)
#define CPU_AXI_CPU1W_QOS_BASE          (RK30_CPU_AXI_BUS_BASE + 0x0380)
#endif

#endif
