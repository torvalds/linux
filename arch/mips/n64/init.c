// SPDX-License-Identifier: GPL-2.0
/*
 *  Nintendo 64 init.
 *
 *  Copyright (C) 2021	Lauri Kasanen
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/memblock.h>
#include <linux/platform_device.h>
#include <linux/platform_data/simplefb.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/fw/fw.h>
#include <asm/time.h>

#define IO_MEM_RESOURCE_START	0UL
#define IO_MEM_RESOURCE_END	0x1fffffffUL

/*
 * System-specifc irq names for clarity
 */
#define MIPS_CPU_IRQ(x)		(MIPS_CPU_IRQ_BASE + (x))
#define MIPS_SOFTINT0_IRQ	MIPS_CPU_IRQ(0)
#define MIPS_SOFTINT1_IRQ	MIPS_CPU_IRQ(1)
#define RCP_IRQ			MIPS_CPU_IRQ(2)
#define CART_IRQ		MIPS_CPU_IRQ(3)
#define PRENMI_IRQ		MIPS_CPU_IRQ(4)
#define RDBR_IRQ		MIPS_CPU_IRQ(5)
#define RDBW_IRQ		MIPS_CPU_IRQ(6)
#define TIMER_IRQ		MIPS_CPU_IRQ(7)

static void __init iomem_resource_init(void)
{
	iomem_resource.start = IO_MEM_RESOURCE_START;
	iomem_resource.end = IO_MEM_RESOURCE_END;
}

const char *get_system_type(void)
{
	return "Nintendo 64";
}

void __init prom_init(void)
{
	fw_init_cmdline();
}

#define W 320
#define H 240
#define REG_BASE ((u32 *) CKSEG1ADDR(0x4400000))

static void __init n64rdp_write_reg(const u8 reg, const u32 value)
{
	__raw_writel(value, REG_BASE + reg);
}

#undef REG_BASE

static const u32 ntsc_320[] __initconst = {
	0x00013212, 0x00000000, 0x00000140, 0x00000200,
	0x00000000, 0x03e52239, 0x0000020d, 0x00000c15,
	0x0c150c15, 0x006c02ec, 0x002501ff, 0x000e0204,
	0x00000200, 0x00000400
};

#define MI_REG_BASE 0x4300000
#define NUM_MI_REGS 4
#define AI_REG_BASE 0x4500000
#define NUM_AI_REGS 6
#define PI_REG_BASE 0x4600000
#define NUM_PI_REGS 5
#define SI_REG_BASE 0x4800000
#define NUM_SI_REGS 7

static int __init n64_platform_init(void)
{
	static const char simplefb_resname[] = "FB";
	static const struct simplefb_platform_data mode = {
		.width = W,
		.height = H,
		.stride = W * 2,
		.format = "r5g5b5a1"
	};
	struct resource res[3];
	void *orig;
	unsigned long phys;
	unsigned i;

	memset(res, 0, sizeof(struct resource) * 3);
	res[0].flags = IORESOURCE_MEM;
	res[0].start = MI_REG_BASE;
	res[0].end = MI_REG_BASE + NUM_MI_REGS * 4 - 1;

	res[1].flags = IORESOURCE_MEM;
	res[1].start = AI_REG_BASE;
	res[1].end = AI_REG_BASE + NUM_AI_REGS * 4 - 1;

	res[2].flags = IORESOURCE_IRQ;
	res[2].start = RCP_IRQ;
	res[2].end = RCP_IRQ;

	platform_device_register_simple("n64audio", -1, res, 3);

	memset(&res[0], 0, sizeof(res[0]));
	res[0].flags = IORESOURCE_MEM;
	res[0].start = PI_REG_BASE;
	res[0].end = PI_REG_BASE + NUM_PI_REGS * 4 - 1;

	platform_device_register_simple("n64cart", -1, res, 1);

	memset(&res[0], 0, sizeof(res[0]));
	res[0].flags = IORESOURCE_MEM;
	res[0].start = SI_REG_BASE;
	res[0].end = SI_REG_BASE + NUM_SI_REGS * 4 - 1;

	platform_device_register_simple("n64joy", -1, res, 1);

	/* The framebuffer needs 64-byte alignment */
	orig = kzalloc(W * H * 2 + 63, GFP_DMA | GFP_KERNEL);
	if (!orig)
		return -ENOMEM;
	phys = virt_to_phys(orig);
	phys += 63;
	phys &= ~63;

	for (i = 0; i < ARRAY_SIZE(ntsc_320); i++) {
		if (i == 1)
			n64rdp_write_reg(i, phys);
		else
			n64rdp_write_reg(i, ntsc_320[i]);
	}

	/* setup IORESOURCE_MEM as framebuffer memory */
	memset(&res[0], 0, sizeof(res[0]));
	res[0].flags = IORESOURCE_MEM;
	res[0].name = simplefb_resname;
	res[0].start = phys;
	res[0].end = phys + W * H * 2 - 1;

	platform_device_register_resndata(NULL, "simple-framebuffer", 0,
					  &res[0], 1, &mode, sizeof(mode));

	return 0;
}

#undef W
#undef H

arch_initcall(n64_platform_init);

void __init plat_mem_setup(void)
{
	iomem_resource_init();
	memblock_add(0x0, 8 * 1024 * 1024); /* Bootloader blocks the 4mb config */
}

void __init plat_time_init(void)
{
	/* 93.75 MHz cpu, count register runs at half rate */
	mips_hpt_frequency = 93750000 / 2;
}
