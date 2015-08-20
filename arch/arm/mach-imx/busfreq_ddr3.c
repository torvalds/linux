/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file busfreq_ddr3.c
 *
 * @brief iMX6 DDR3 frequency change specific file.
 *
 * @ingroup PM
 */
#include <asm/cacheflush.h>
#include <asm/fncpy.h>
#include <asm/io.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <asm/tlb.h>
#include <linux/busfreq-imx.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>

#include "hardware.h"
#include "common.h"

#define SMP_WFE_CODE_SIZE	0x400

#define MIN_DLL_ON_FREQ		333000000
#define MAX_DLL_OFF_FREQ	125000000
#define MMDC0_MPMUR0		0x8b8
#define MMDC0_MPMUR0_OFFSET	16
#define MMDC0_MPMUR0_MASK	0x3ff

/*
 * This structure is for passing necessary data for low level ocram
 * busfreq code(arch/arm/mach-imx/ddr3_freq_imx6.S), if this struct
 * definition is changed, the offset definition in
 * arch/arm/mach-imx/ddr3_freq_imx6.S must be also changed accordingly,
 * otherwise, the busfreq change function will be broken!
 *
 * This structure will be placed in front of the asm code on ocram.
 */
struct imx6_busfreq_info {
	u32 freq;
	void *ddr_settings;
	u32 dll_off;
	void *iomux_offsets;
	u32 mu_delay_val;
} __aligned(8);

static struct imx6_busfreq_info *imx6_busfreq_info;

/* DDR settings */
static unsigned long (*iram_ddr_settings)[2];
static unsigned long (*normal_mmdc_settings)[2];
static unsigned long (*iram_iomux_settings)[2];

static void __iomem *mmdc_base;
static void __iomem *iomux_base;
static void __iomem *gic_dist_base;

static int ddr_settings_size;
static int iomux_settings_size;
static int curr_ddr_rate;

void (*imx6_up_change_ddr_freq)(struct imx6_busfreq_info *busfreq_info);
extern void imx6_up_ddr3_freq_change(struct imx6_busfreq_info *busfreq_info);
void (*imx7d_change_ddr_freq)(u32 freq) = NULL;
extern void imx7d_ddr3_freq_change(u32 freq);
extern void imx_lpddr3_freq_change(u32 freq);

void (*mx6_change_ddr_freq)(u32 freq, void *ddr_settings,
	bool dll_mode, void *iomux_offsets) = NULL;

extern unsigned int ddr_normal_rate;
extern int low_bus_freq_mode;
extern int audio_bus_freq_mode;
extern void mx6_ddr3_freq_change(u32 freq, void *ddr_settings,
	bool dll_mode, void *iomux_offsets);

extern unsigned long save_ttbr1(void);
extern void restore_ttbr1(unsigned long ttbr1);
extern unsigned long ddr_freq_change_iram_base;

extern unsigned long ddr_freq_change_total_size;
extern unsigned long iram_tlb_phys_addr;

extern unsigned long mx6_ddr3_freq_change_start asm("mx6_ddr3_freq_change_start");
extern unsigned long mx6_ddr3_freq_change_end asm("mx6_ddr3_freq_change_end");
extern unsigned long imx6_up_ddr3_freq_change_start asm("imx6_up_ddr3_freq_change_start");
extern unsigned long imx6_up_ddr3_freq_change_end asm("imx6_up_ddr3_freq_change_end");

#ifdef CONFIG_SMP
static unsigned long wfe_freq_change_iram_base;
volatile u32 *wait_for_ddr_freq_update;
static unsigned int online_cpus;
static u32 *irqs_used;

void (*wfe_change_ddr_freq)(u32 cpuid, u32 *ddr_freq_change_done);
void (*imx7_wfe_change_ddr_freq)(u32 cpuid, u32 ocram_base);
extern void wfe_ddr3_freq_change(u32 cpuid, u32 *ddr_freq_change_done);
extern void imx7_smp_wfe(u32 cpuid, u32 ocram_base);
extern unsigned long wfe_ddr3_freq_change_start
	asm("wfe_ddr3_freq_change_start");
extern unsigned long wfe_ddr3_freq_change_end asm("wfe_ddr3_freq_change_end");
extern void __iomem *imx_scu_base;
#endif

unsigned long ddr3_dll_mx6sx[][2] = {
	{0x0c, 0x0},
	{0x10, 0x0},
	{0x1C, 0x04008032},
	{0x1C, 0x00048031},
	{0x1C, 0x05208030},
	{0x1C, 0x04008040},
	{0x818, 0x0},
};

unsigned long ddr3_calibration_mx6sx[][2] = {
	{0x83c, 0x0},
	{0x840, 0x0},
	{0x848, 0x0},
	{0x850, 0x0},
};

unsigned long iomux_offsets_mx6sx[][2] = {
	{0x330, 0x0},
	{0x334, 0x0},
	{0x338, 0x0},
	{0x33c, 0x0},
};

unsigned long iomux_offsets_mx6ul[][2] = {
	{0x280, 0x0},
	{0x284, 0x0},
};

unsigned long ddr3_dll_mx6q[][2] = {
	{0x0c, 0x0},
	{0x10, 0x0},
	{0x1C, 0x04088032},
	{0x1C, 0x0408803a},
	{0x1C, 0x08408030},
	{0x1C, 0x08408038},
	{0x818, 0x0},
};

unsigned long ddr3_calibration[][2] = {
	{0x83c, 0x0},
	{0x840, 0x0},
	{0x483c, 0x0},
	{0x4840, 0x0},
	{0x848, 0x0},
	{0x4848, 0x0},
	{0x850, 0x0},
	{0x4850, 0x0},
};

unsigned long iomux_offsets_mx6q[][2] = {
	{0x5A8, 0x0},
	{0x5B0, 0x0},
	{0x524, 0x0},
	{0x51C, 0x0},
	{0x518, 0x0},
	{0x50C, 0x0},
	{0x5B8, 0x0},
	{0x5C0, 0x0},
};

unsigned long ddr3_dll_mx6dl[][2] = {
	{0x0c, 0x0},
	{0x10, 0x0},
	{0x1C, 0x04008032},
	{0x1C, 0x0400803a},
	{0x1C, 0x07208030},
	{0x1C, 0x07208038},
	{0x818, 0x0},
};

unsigned long iomux_offsets_mx6dl[][2] = {
	{0x4BC, 0x0},
	{0x4C0, 0x0},
	{0x4C4, 0x0},
	{0x4C8, 0x0},
	{0x4CC, 0x0},
	{0x4D0, 0x0},
	{0x4D4, 0x0},
	{0x4D8, 0x0},
};

int can_change_ddr_freq(void)
{
	return 1;
}

#ifdef CONFIG_SMP
/*
 * each active core apart from the one changing
 * the DDR frequency will execute this function.
 * the rest of the cores have to remain in WFE
 * state until the frequency is changed.
 */
static irqreturn_t wait_in_wfe_irq(int irq, void *dev_id)
{
	u32 me;

	me = smp_processor_id();
#ifdef CONFIG_LOCAL_TIMERS
	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER,
		&me);
#endif
	if (cpu_is_imx7d())
		imx7_wfe_change_ddr_freq(0x8 * me,
			(u32)ddr_freq_change_iram_base);
	else
		wfe_change_ddr_freq(0xff << (me * 8),
			(u32 *)&iram_iomux_settings[0][1]);
#ifdef CONFIG_LOCAL_TIMERS
	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT,
		&me);
#endif

	return IRQ_HANDLED;
}
#endif

/* change the DDR frequency. */
int update_ddr_freq_imx_smp(int ddr_rate)
{
	int me = 0;
	unsigned long ttbr1;
	bool dll_off = false;
	int i;
#ifdef CONFIG_SMP
	unsigned int reg = 0;
	int cpu = 0;
#endif
	int mode = get_bus_freq_mode();

	if (!can_change_ddr_freq())
		return -1;

	if (ddr_rate == curr_ddr_rate)
		return 0;

	printk(KERN_DEBUG "\nBus freq set to %d start...\n", ddr_rate);

	if (cpu_is_imx6()) {
		if ((mode == BUS_FREQ_LOW) || (mode == BUS_FREQ_AUDIO))
			dll_off = true;

		iram_ddr_settings[0][0] = ddr_settings_size;
		iram_iomux_settings[0][0] = iomux_settings_size;
		if (ddr_rate == ddr_normal_rate) {
			for (i = 0; i < iram_ddr_settings[0][0]; i++) {
				iram_ddr_settings[i + 1][0] =
						normal_mmdc_settings[i][0];
				iram_ddr_settings[i + 1][1] =
						normal_mmdc_settings[i][1];
			}
		}
	}

	/* ensure that all Cores are in WFE. */
	local_irq_disable();

#ifdef CONFIG_SMP
	me = smp_processor_id();

	/* Make sure all the online cores are active */
	while (1) {
		bool not_exited_busfreq = false;
		u32 reg = 0;

		for_each_online_cpu(cpu) {
			if (cpu_is_imx7d())
				reg = *(wait_for_ddr_freq_update + 1);
			else if (cpu_is_imx6())
				reg = __raw_readl(imx_scu_base + 0x08);

			if (reg & (0x02 << (cpu * 8)))
				not_exited_busfreq = true;
		}
		if (!not_exited_busfreq)
			break;
	}

	wmb();
	*wait_for_ddr_freq_update = 1;
	dsb();
	if (cpu_is_imx7d())
		online_cpus = *(wait_for_ddr_freq_update + 1);
	else if (cpu_is_imx6())
		online_cpus = readl_relaxed(imx_scu_base + 0x08);
	for_each_online_cpu(cpu) {
		*((char *)(&online_cpus) + (u8)cpu) = 0x02;
		if (cpu != me) {
			/* set the interrupt to be pending in the GIC. */
			reg = 1 << (irqs_used[cpu] % 32);
			writel_relaxed(reg, gic_dist_base + GIC_DIST_PENDING_SET
				+ (irqs_used[cpu] / 32) * 4);
		}
	}
	/* Wait for the other active CPUs to idle */
	while (1) {
		u32 reg = 0;

		if (cpu_is_imx7d())
			reg = *(wait_for_ddr_freq_update + 1);
		else if (cpu_is_imx6())
			reg = readl_relaxed(imx_scu_base + 0x08);
		reg |= (0x02 << (me * 8));
		if (reg == online_cpus)
			break;
	}
#endif

	/* Ensure iram_tlb_phys_addr is flushed to DDR. */
	__cpuc_flush_dcache_area(&iram_tlb_phys_addr,
		sizeof(iram_tlb_phys_addr));
	if (cpu_is_imx6())
		outer_clean_range(virt_to_phys(&iram_tlb_phys_addr),
			virt_to_phys(&iram_tlb_phys_addr + 1));

	ttbr1 = save_ttbr1();
	/* Now we can change the DDR frequency. */
	if (cpu_is_imx7d())
		imx7d_change_ddr_freq(ddr_rate);
	else if (cpu_is_imx6())
		mx6_change_ddr_freq(ddr_rate, iram_ddr_settings,
			dll_off, iram_iomux_settings);
	restore_ttbr1(ttbr1);
	curr_ddr_rate = ddr_rate;

#ifdef CONFIG_SMP
	wmb();
	/* DDR frequency change is done . */
	*wait_for_ddr_freq_update = 0;
	dsb();

	/* wake up all the cores. */
	sev();
#endif

	local_irq_enable();

	printk(KERN_DEBUG "Bus freq set to %d done! cpu=%d\n", ddr_rate, me);

	return 0;
}

/* Used by i.MX6SX/i.MX6UL for updating the ddr frequency */
int update_ddr_freq_imx6_up(int ddr_rate)
{
	int i;
	bool dll_off = false;
	unsigned long ttbr1;
	int mode = get_bus_freq_mode();

	if (ddr_rate == curr_ddr_rate)
		return 0;

	printk(KERN_DEBUG "\nBus freq set to %d start...\n", ddr_rate);

	if ((mode == BUS_FREQ_LOW) || (mode == BUS_FREQ_AUDIO))
		dll_off = true;

	imx6_busfreq_info->dll_off = dll_off;
	iram_ddr_settings[0][0] = ddr_settings_size;
	iram_iomux_settings[0][0] = iomux_settings_size;
	for (i = 0; i < iram_ddr_settings[0][0]; i++) {
		iram_ddr_settings[i + 1][0] =
				normal_mmdc_settings[i][0];
		iram_ddr_settings[i + 1][1] =
				normal_mmdc_settings[i][1];
	}

	local_irq_disable();

	ttbr1 = save_ttbr1();
	imx6_busfreq_info->freq = ddr_rate;
	imx6_busfreq_info->ddr_settings = iram_ddr_settings;
	imx6_busfreq_info->iomux_offsets = iram_iomux_settings;
	imx6_busfreq_info->mu_delay_val  = ((readl_relaxed(mmdc_base + MMDC0_MPMUR0)
		>> MMDC0_MPMUR0_OFFSET) & MMDC0_MPMUR0_MASK);

	imx6_up_change_ddr_freq(imx6_busfreq_info);
	restore_ttbr1(ttbr1);
	curr_ddr_rate = ddr_rate;

	local_irq_enable();

	printk(KERN_DEBUG "Bus freq set to %d done!\n", ddr_rate);

	return 0;
}

int init_ddrc_ddr_settings(struct platform_device *busfreq_pdev)
{
	int ddr_type = imx_ddrc_get_ddr_type();
#ifdef CONFIG_SMP
	struct device_node *node;
	u32 cpu;
	struct device *dev = &busfreq_pdev->dev;
	int err;
	struct irq_data *d;

	node = of_find_compatible_node(NULL, NULL, "arm,cortex-a7-gic");
	if (!node) {
		printk(KERN_ERR "failed to find imx7d-a7-gic device tree data!\n");
		return -EINVAL;
	}
	gic_dist_base = of_iomap(node, 0);
	WARN(!gic_dist_base, "unable to map gic dist registers\n");

	irqs_used = devm_kzalloc(dev, sizeof(u32) * num_present_cpus(),
					GFP_KERNEL);
	for_each_online_cpu(cpu) {
		int irq;
		/*
		 * set up a reserved interrupt to get all
		 * the active cores into a WFE state
		 * before changing the DDR frequency.
		 */
		irq = platform_get_irq(busfreq_pdev, cpu);
		err = request_irq(irq, wait_in_wfe_irq,
			IRQF_PERCPU, "ddrc", NULL);
		if (err) {
			dev_err(dev,
				"Busfreq:request_irq failed %d, err = %d\n",
				irq, err);
			return err;
		}
		err = irq_set_affinity(irq, cpumask_of(cpu));
		if (err) {
			dev_err(dev,
				"Busfreq: Cannot set irq affinity irq=%d\n",
				irq);
			return err;
		}
		d = irq_get_irq_data(irq);
		irqs_used[cpu] = d->hwirq + 32;
	}

	/* Store the variable used to communicate between cores */
	wait_for_ddr_freq_update = (u32 *)ddr_freq_change_iram_base;
	imx7_wfe_change_ddr_freq = (void *)fncpy(
		(void *)ddr_freq_change_iram_base + 0x8,
		&imx7_smp_wfe, SMP_WFE_CODE_SIZE - 0x8);
#endif
	if (ddr_type == IMX_DDR_TYPE_DDR3)
		imx7d_change_ddr_freq = (void *)fncpy(
			(void *)ddr_freq_change_iram_base + SMP_WFE_CODE_SIZE,
			&imx7d_ddr3_freq_change,
			MX7_BUSFREQ_OCRAM_SIZE - SMP_WFE_CODE_SIZE);
	else if (ddr_type == IMX_DDR_TYPE_LPDDR3
		|| ddr_type == IMX_DDR_TYPE_LPDDR2)
		imx7d_change_ddr_freq = (void *)fncpy(
			(void *)ddr_freq_change_iram_base +
			SMP_WFE_CODE_SIZE,
			&imx_lpddr3_freq_change,
			MX7_BUSFREQ_OCRAM_SIZE - SMP_WFE_CODE_SIZE);

	curr_ddr_rate = ddr_normal_rate;

	return 0;
}

/* Used by i.MX6SX/i.MX6UL for mmdc setting init. */
int init_mmdc_ddr3_settings_imx6_up(struct platform_device *busfreq_pdev)
{
	int i;
	struct device_node *node;
	unsigned long ddr_code_size;

	node = of_find_compatible_node(NULL, NULL, "fsl,imx6q-mmdc");
	if (!node) {
		printk(KERN_ERR "failed to find mmdc device tree data!\n");
		return -EINVAL;
	}
	mmdc_base = of_iomap(node, 0);
	WARN(!mmdc_base, "unable to map mmdc registers\n");

	if (cpu_is_imx6sx())
		node = of_find_compatible_node(NULL, NULL, "fsl,imx6sx-iomuxc");
	else
		node = of_find_compatible_node(NULL, NULL, "fsl,imx6ul-iomuxc");
	if (!node) {
		printk(KERN_ERR "failed to find iomuxc device tree data!\n");
		return -EINVAL;
	}
	iomux_base = of_iomap(node, 0);
	WARN(!iomux_base, "unable to map iomux registers\n");

	ddr_settings_size = ARRAY_SIZE(ddr3_dll_mx6sx) +
		ARRAY_SIZE(ddr3_calibration_mx6sx);

	normal_mmdc_settings = kmalloc((ddr_settings_size * 8), GFP_KERNEL);
	memcpy(normal_mmdc_settings, ddr3_dll_mx6sx,
		sizeof(ddr3_dll_mx6sx));
	memcpy(((char *)normal_mmdc_settings + sizeof(ddr3_dll_mx6sx)),
		ddr3_calibration_mx6sx, sizeof(ddr3_calibration_mx6sx));

	/* store the original DDR settings at boot. */
	for (i = 0; i < ddr_settings_size; i++) {
		/*
		 * writes via command mode register cannot be read back.
		 * hence hardcode them in the initial static array.
		 * this may require modification on a per customer basis.
		 */
		if (normal_mmdc_settings[i][0] != 0x1C)
			normal_mmdc_settings[i][1] =
				readl_relaxed(mmdc_base
				+ normal_mmdc_settings[i][0]);
	}

	if (cpu_is_imx6ul())
		iomux_settings_size = ARRAY_SIZE(iomux_offsets_mx6ul);
	else
		iomux_settings_size = ARRAY_SIZE(iomux_offsets_mx6sx);

	ddr_code_size = (&imx6_up_ddr3_freq_change_end -&imx6_up_ddr3_freq_change_start) *4 +
			sizeof(*imx6_busfreq_info);

	imx6_busfreq_info = (struct imx6_busfreq_info *)ddr_freq_change_iram_base;

	imx6_up_change_ddr_freq = (void *)fncpy((void *)ddr_freq_change_iram_base + sizeof(*imx6_busfreq_info),
		&imx6_up_ddr3_freq_change, ddr_code_size - sizeof(*imx6_busfreq_info));

	/*
	 * Store the size of the array in iRAM also,
	 * increase the size by 8 bytes.
	 */
	iram_iomux_settings = (void *)(ddr_freq_change_iram_base + ddr_code_size);
	iram_ddr_settings = iram_iomux_settings + (iomux_settings_size * 8) + 8;

	if ((ddr_code_size + (iomux_settings_size + ddr_settings_size) * 8 + 16)
		> ddr_freq_change_total_size) {
		printk(KERN_ERR "Not enough memory allocated for DDR Frequency change code.\n");
		return EINVAL;
	}

	for (i = 0; i < iomux_settings_size; i++) {
		if (cpu_is_imx6ul()) {
			iomux_offsets_mx6ul[i][1] =
			readl_relaxed(iomux_base +
				iomux_offsets_mx6ul[i][0]);
			iram_iomux_settings[i + 1][0] =
				iomux_offsets_mx6ul[i][0];
			iram_iomux_settings[i + 1][1] =
				iomux_offsets_mx6ul[i][1];
		} else {
			iomux_offsets_mx6sx[i][1] =
				readl_relaxed(iomux_base +
				iomux_offsets_mx6sx[i][0]);
			iram_iomux_settings[i + 1][0] =
				iomux_offsets_mx6sx[i][0];
			iram_iomux_settings[i + 1][1] =
				iomux_offsets_mx6sx[i][1];
		}
	}

	curr_ddr_rate = ddr_normal_rate;

	return 0;
}

int init_mmdc_ddr3_settings_imx6_smp(struct platform_device *busfreq_pdev)
{
	int i;
	struct device_node *node;
	unsigned long ddr_code_size;
	unsigned long wfe_code_size = 0;
#ifdef CONFIG_SMP
	u32 cpu;
	struct device *dev = &busfreq_pdev->dev;
	int err;
	struct irq_data *d;
#endif

	node = of_find_compatible_node(NULL, NULL, "fsl,imx6q-mmdc-combine");
	if (!node) {
		printk(KERN_ERR "failed to find imx6q-mmdc device tree data!\n");
		return -EINVAL;
	}
	mmdc_base = of_iomap(node, 0);
	WARN(!mmdc_base, "unable to map mmdc registers\n");

	node = NULL;
	if (cpu_is_imx6q())
		node = of_find_compatible_node(NULL, NULL, "fsl,imx6q-iomuxc");
	if (cpu_is_imx6dl())
		node = of_find_compatible_node(NULL, NULL,
			"fsl,imx6dl-iomuxc");
	if (!node) {
		printk(KERN_ERR "failed to find imx6q-iomux device tree data!\n");
		return -EINVAL;
	}
	iomux_base = of_iomap(node, 0);
	WARN(!iomux_base, "unable to map iomux registers\n");

	node = NULL;
	node = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-gic");
	if (!node) {
		printk(KERN_ERR "failed to find imx6q-a9-gic device tree data!\n");
		return -EINVAL;
	}
	gic_dist_base = of_iomap(node, 0);
	WARN(!gic_dist_base, "unable to map gic dist registers\n");

	if (cpu_is_imx6q())
		ddr_settings_size = ARRAY_SIZE(ddr3_dll_mx6q) +
			ARRAY_SIZE(ddr3_calibration);
	if (cpu_is_imx6dl())
		ddr_settings_size = ARRAY_SIZE(ddr3_dll_mx6dl) +
			ARRAY_SIZE(ddr3_calibration);

	normal_mmdc_settings = kmalloc((ddr_settings_size * 8), GFP_KERNEL);
	if (cpu_is_imx6q()) {
		memcpy(normal_mmdc_settings, ddr3_dll_mx6q,
			sizeof(ddr3_dll_mx6q));
		memcpy(((char *)normal_mmdc_settings + sizeof(ddr3_dll_mx6q)),
			ddr3_calibration, sizeof(ddr3_calibration));
	}
	if (cpu_is_imx6dl()) {
		memcpy(normal_mmdc_settings, ddr3_dll_mx6dl,
			sizeof(ddr3_dll_mx6dl));
		memcpy(((char *)normal_mmdc_settings + sizeof(ddr3_dll_mx6dl)),
			ddr3_calibration, sizeof(ddr3_calibration));
	}
	/* store the original DDR settings at boot. */
	for (i = 0; i < ddr_settings_size; i++) {
		/*
		 * writes via command mode register cannot be read back.
		 * hence hardcode them in the initial static array.
		 * this may require modification on a per customer basis.
		 */
		if (normal_mmdc_settings[i][0] != 0x1C)
			normal_mmdc_settings[i][1] =
				readl_relaxed(mmdc_base
				+ normal_mmdc_settings[i][0]);
	}

#ifdef CONFIG_SMP
	irqs_used = devm_kzalloc(dev, sizeof(u32) * num_present_cpus(),
					GFP_KERNEL);

	for_each_online_cpu(cpu) {
		int irq;

		/*
		 * set up a reserved interrupt to get all
		 * the active cores into a WFE state
		 * before changing the DDR frequency.
		 */
		irq = platform_get_irq(busfreq_pdev, cpu);
		err = request_irq(irq, wait_in_wfe_irq,
			IRQF_PERCPU, "mmdc_1", NULL);
		if (err) {
			dev_err(dev,
				"Busfreq:request_irq failed %d, err = %d\n",
				irq, err);
			return err;
		}
		err = irq_set_affinity(irq, cpumask_of(cpu));
		if (err) {
			dev_err(dev,
				"Busfreq: Cannot set irq affinity irq=%d,\n",
				irq);
			return err;
		}
		d = irq_get_irq_data(irq);
		irqs_used[cpu] = d->hwirq + 32;
	}
#endif
	iomux_settings_size = ARRAY_SIZE(iomux_offsets_mx6q);

	ddr_code_size = (&mx6_ddr3_freq_change_end -
		&mx6_ddr3_freq_change_start) * 4;

	mx6_change_ddr_freq = (void *)fncpy((void *)ddr_freq_change_iram_base,
		&mx6_ddr3_freq_change, ddr_code_size);

	/*
	 * Store the size of the array in iRAM also,
	 * increase the size by 8 bytes.
	 */
	iram_iomux_settings = (void *)(ddr_freq_change_iram_base +
		ddr_code_size);
	iram_ddr_settings = iram_iomux_settings + (iomux_settings_size * 8) + 8;
#ifdef CONFIG_SMP
	wfe_freq_change_iram_base = (unsigned long)((u32 *)iram_ddr_settings +
		(ddr_settings_size * 8) + 8);

	if (wfe_freq_change_iram_base & (FNCPY_ALIGN - 1))
			wfe_freq_change_iram_base += FNCPY_ALIGN -
			((uintptr_t)wfe_freq_change_iram_base % (FNCPY_ALIGN));

	wfe_code_size = (&wfe_ddr3_freq_change_end -
		&wfe_ddr3_freq_change_start) *4;

	wfe_change_ddr_freq = (void *)fncpy((void *)wfe_freq_change_iram_base,
		&wfe_ddr3_freq_change, wfe_code_size);

	/*
	 * Store the variable used to communicate
	 * between cores in a non-cacheable IRAM area
	 */
	wait_for_ddr_freq_update = (u32 *)&iram_iomux_settings[0][1];
#endif

	if ((ddr_code_size + wfe_code_size + (iomux_settings_size +
		ddr_settings_size) * 8 + 16)
		> ddr_freq_change_total_size) {
		printk(KERN_ERR "Not enough memory for DDR Freq scale.\n");
		return EINVAL;
	}

	if (cpu_is_imx6q()) {
		/* store the IOMUX settings at boot. */
		for (i = 0; i < iomux_settings_size; i++) {
			iomux_offsets_mx6q[i][1] =
				readl_relaxed(iomux_base +
					iomux_offsets_mx6q[i][0]);
			iram_iomux_settings[i + 1][0] =
				iomux_offsets_mx6q[i][0];
			iram_iomux_settings[i + 1][1] =
				iomux_offsets_mx6q[i][1];
		}
	}

	if (cpu_is_imx6dl()) {
		for (i = 0; i < iomux_settings_size; i++) {
			iomux_offsets_mx6dl[i][1] =
				readl_relaxed(iomux_base +
					iomux_offsets_mx6dl[i][0]);
			iram_iomux_settings[i + 1][0] =
				iomux_offsets_mx6dl[i][0];
			iram_iomux_settings[i + 1][1] =
				iomux_offsets_mx6dl[i][1];
		}
	}

	curr_ddr_rate = ddr_normal_rate;

	return 0;
}
