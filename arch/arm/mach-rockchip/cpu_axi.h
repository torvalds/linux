#ifndef __CPU_AXI_H
#define __CPU_AXI_H

#define CPU_AXI_QOS_PRIORITY    0x08
#define CPU_AXI_QOS_MODE        0x0c
#define CPU_AXI_QOS_BANDWIDTH   0x10
#define CPU_AXI_QOS_SATURATION  0x14

#define CPU_AXI_QOS_MODE_NONE           0
#define CPU_AXI_QOS_MODE_FIXED          1
#define CPU_AXI_QOS_MODE_LIMITER        2
#define CPU_AXI_QOS_MODE_REGULATOR      3

#define CPU_AXI_QOS_PRIORITY_LEVEL(h, l)        ((((h) & 3) << 2) | ((l) & 3))
#define CPU_AXI_SET_QOS_PRIORITY(h, l, base) \
	writel_relaxed(CPU_AXI_QOS_PRIORITY_LEVEL(h, l), base + CPU_AXI_QOS_PRIORITY)

#define CPU_AXI_SET_QOS_MODE(mode, base) \
	writel_relaxed((mode) & 3, base + CPU_AXI_QOS_MODE)

#define CPU_AXI_SET_QOS_BANDWIDTH(bandwidth, base) \
	writel_relaxed((bandwidth) & 0x7ff, base + CPU_AXI_QOS_BANDWIDTH)

#define CPU_AXI_SET_QOS_SATURATION(saturation, base) \
	writel_relaxed((saturation) & 0x3ff, base + CPU_AXI_QOS_SATURATION)

#define CPU_AXI_QOS_NUM_REGS 4
#define CPU_AXI_SAVE_QOS(array, base) do { \
	array[0] = readl_relaxed(base + CPU_AXI_QOS_PRIORITY); \
	array[1] = readl_relaxed(base + CPU_AXI_QOS_MODE); \
	array[2] = readl_relaxed(base + CPU_AXI_QOS_BANDWIDTH); \
	array[3] = readl_relaxed(base + CPU_AXI_QOS_SATURATION); \
} while (0)
#define CPU_AXI_RESTORE_QOS(array, base) do { \
	writel_relaxed(array[0], base + CPU_AXI_QOS_PRIORITY); \
	writel_relaxed(array[1], base + CPU_AXI_QOS_MODE); \
	writel_relaxed(array[2], base + CPU_AXI_QOS_BANDWIDTH); \
	writel_relaxed(array[3], base + CPU_AXI_QOS_SATURATION); \
} while (0)

#define RK3188_CPU_AXI_DMAC_QOS_BASE    (RK_CPU_AXI_BUS_VIRT + 0x1000)
#define RK3188_CPU_AXI_CPU0_QOS_BASE    (RK_CPU_AXI_BUS_VIRT + 0x2000)
#define RK3188_CPU_AXI_CPU1R_QOS_BASE   (RK_CPU_AXI_BUS_VIRT + 0x2080)
#define RK3188_CPU_AXI_CPU1W_QOS_BASE   (RK_CPU_AXI_BUS_VIRT + 0x2100)
#define RK3188_CPU_AXI_PERI_QOS_BASE    (RK_CPU_AXI_BUS_VIRT + 0x4000)
#define RK3188_CPU_AXI_GPU_QOS_BASE     (RK_CPU_AXI_BUS_VIRT + 0x5000)
#define RK3188_CPU_AXI_VPU_QOS_BASE     (RK_CPU_AXI_BUS_VIRT + 0x6000)
#define RK3188_CPU_AXI_LCDC0_QOS_BASE   (RK_CPU_AXI_BUS_VIRT + 0x7000)
#define RK3188_CPU_AXI_CIF0_QOS_BASE    (RK_CPU_AXI_BUS_VIRT + 0x7080)
#define RK3188_CPU_AXI_IPP_QOS_BASE     (RK_CPU_AXI_BUS_VIRT + 0x7100)
#define RK3188_CPU_AXI_LCDC1_QOS_BASE   (RK_CPU_AXI_BUS_VIRT + 0x7180)
#define RK3188_CPU_AXI_CIF1_QOS_BASE    (RK_CPU_AXI_BUS_VIRT + 0x7200)
#define RK3188_CPU_AXI_RGA_QOS_BASE     (RK_CPU_AXI_BUS_VIRT + 0x7280)

#endif
