/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 * Copyright (C) 2014 Kevin Cernekee <cernekee@gmail.com>
 */

#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/memblock.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/smp.h>
#include <asm/addrspace.h>
#include <asm/bmips.h>
#include <asm/bootinfo.h>
#include <asm/cpu-type.h>
#include <asm/mipsregs.h>
#include <asm/prom.h>
#include <asm/smp-ops.h>
#include <asm/time.h>
#include <asm/traps.h>
#include <asm/fw/cfe/cfe_api.h>

#define RELO_NORMAL_VEC		BIT(18)

#define REG_BCM6328_OTP		((void __iomem *)CKSEG1ADDR(0x1000062c))
#define BCM6328_TP1_DISABLED	BIT(9)

extern bool bmips_rac_flush_disable;

static const unsigned long kbase = VMLINUX_LOAD_ADDRESS & 0xfff00000;

struct bmips_quirk {
	const char		*compatible;
	void			(*quirk_fn)(void);
};

static void kbase_setup(void)
{
	__raw_writel(kbase | RELO_NORMAL_VEC,
		     BMIPS_GET_CBR() + BMIPS_RELO_VECTOR_CONTROL_1);
	ebase = kbase;
}

static void bcm3384_viper_quirks(void)
{
	/*
	 * Some experimental CM boxes are set up to let CM own the Viper TP0
	 * and let Linux own TP1.  This requires moving the kernel
	 * load address to a non-conflicting region (e.g. via
	 * CONFIG_PHYSICAL_START) and supplying an alternate DTB.
	 * If we detect this condition, we need to move the MIPS exception
	 * vectors up to an area that we own.
	 *
	 * This is distinct from the OTHER special case mentioned in
	 * smp-bmips.c (boot on TP1, but enable SMP, then TP0 becomes our
	 * logical CPU#1).  For the Viper TP1 case, SMP is off limits.
	 *
	 * Also note that many BMIPS435x CPUs do not have a
	 * BMIPS_RELO_VECTOR_CONTROL_1 register, so it isn't safe to just
	 * write VMLINUX_LOAD_ADDRESS into that register on every SoC.
	 */
	board_ebase_setup = &kbase_setup;
	bmips_smp_enabled = 0;
}

static void bcm63xx_fixup_cpu1(void)
{
	/*
	 * The bootloader has set up the CPU1 reset vector at
	 * 0xa000_0200.
	 * This conflicts with the special interrupt vector (IV).
	 * The bootloader has also set up CPU1 to respond to the wrong
	 * IPI interrupt.
	 * Here we will start up CPU1 in the background and ask it to
	 * reconfigure itself then go back to sleep.
	 */
	memcpy((void *)0xa0000200, &bmips_smp_movevec, 0x20);
	__sync();
	set_c0_cause(C_SW0);
	cpumask_set_cpu(1, &bmips_booted_mask);
}

static void bcm6328_quirks(void)
{
	/* Check CPU1 status in OTP (it is usually disabled) */
	if (__raw_readl(REG_BCM6328_OTP) & BCM6328_TP1_DISABLED)
		bmips_smp_enabled = 0;
	else
		bcm63xx_fixup_cpu1();
}

static void bcm6358_quirks(void)
{
	/*
	 * BCM3368/BCM6358 need special handling for their shared TLB, so
	 * disable SMP for now
	 */
	bmips_smp_enabled = 0;

	/*
	 * RAC flush causes kernel panics on BCM6358 when booting from TP1
	 * because the bootloader is not initializing it properly.
	 */
	bmips_rac_flush_disable = !!(read_c0_brcm_cmt_local() & (1 << 31));
}

static void bcm6368_quirks(void)
{
	bcm63xx_fixup_cpu1();
}

static const struct bmips_quirk bmips_quirk_list[] = {
	{ "brcm,bcm3368",		&bcm6358_quirks			},
	{ "brcm,bcm3384-viper",		&bcm3384_viper_quirks		},
	{ "brcm,bcm33843-viper",	&bcm3384_viper_quirks		},
	{ "brcm,bcm6328",		&bcm6328_quirks			},
	{ "brcm,bcm6358",		&bcm6358_quirks			},
	{ "brcm,bcm6362",		&bcm6368_quirks			},
	{ "brcm,bcm6368",		&bcm6368_quirks			},
	{ "brcm,bcm63168",		&bcm6368_quirks			},
	{ "brcm,bcm63268",		&bcm6368_quirks			},
	{ },
};

static void __init bmips_init_cfe(void)
{
	cfe_seal = fw_arg3;

	if (cfe_seal != CFE_EPTSEAL)
		return;

	cfe_init(fw_arg0, fw_arg2);
}

void __init prom_init(void)
{
	bmips_init_cfe();
	bmips_cpu_setup();
	register_bmips_smp_ops();
}

const char *get_system_type(void)
{
	return "Generic BMIPS kernel";
}

void __init plat_time_init(void)
{
	struct device_node *np;
	u32 freq;

	np = of_find_node_by_name(NULL, "cpus");
	if (!np)
		panic("missing 'cpus' DT node");
	if (of_property_read_u32(np, "mips-hpt-frequency", &freq) < 0)
		panic("missing 'mips-hpt-frequency' property");
	of_node_put(np);

	mips_hpt_frequency = freq;
}

void __init plat_mem_setup(void)
{
	void *dtb;
	const struct bmips_quirk *q;

	set_io_port_base(0);
	ioport_resource.start = 0;
	ioport_resource.end = ~0;

	/*
	 * intended to somewhat resemble ARM; see
	 * Documentation/arch/arm/booting.rst
	 */
	if (fw_arg0 == 0 && fw_arg1 == 0xffffffff)
		dtb = phys_to_virt(fw_arg2);
	else
		dtb = get_fdt();

	if (!dtb)
		cfe_die("no dtb found");

	__dt_setup_arch(dtb);

	for (q = bmips_quirk_list; q->quirk_fn; q++) {
		if (of_flat_dt_is_compatible(of_get_flat_dt_root(),
					     q->compatible)) {
			q->quirk_fn();
		}
	}
}

void __init device_tree_init(void)
{
	struct device_node *np;

	unflatten_and_copy_device_tree();

	/* Disable SMP boot unless both CPUs are listed in DT and !disabled */
	np = of_find_node_by_name(NULL, "cpus");
	if (np && of_get_available_child_count(np) <= 1)
		bmips_smp_enabled = 0;
	of_node_put(np);
}

static int __init plat_dev_init(void)
{
	of_clk_init(NULL);
	return 0;
}

arch_initcall(plat_dev_init);
