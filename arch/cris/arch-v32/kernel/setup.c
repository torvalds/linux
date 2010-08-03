/*
 * Display CPU info in /proc/cpuinfo.
 *
 * Copyright (C) 2003, Axis Communications AB.
 */

#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/param.h>

#include <linux/i2c.h>
#include <linux/platform_device.h>

#ifdef CONFIG_PROC_FS

#define HAS_FPU         0x0001
#define HAS_MMU         0x0002
#define HAS_ETHERNET100 0x0004
#define HAS_TOKENRING   0x0008
#define HAS_SCSI        0x0010
#define HAS_ATA         0x0020
#define HAS_USB         0x0040
#define HAS_IRQ_BUG     0x0080
#define HAS_MMU_BUG     0x0100

struct cpu_info {
	char *cpu_model;
	unsigned short rev;
	unsigned short cache_size;
	unsigned short flags;
};

/* Some of these model are here for historical reasons only. */
static struct cpu_info cpinfo[] = {
	{"ETRAX 1", 0, 0, 0},
	{"ETRAX 2", 1, 0, 0},
	{"ETRAX 3", 2, 0, 0},
	{"ETRAX 4", 3, 0, 0},
	{"Simulator", 7, 8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA},
	{"ETRAX 100", 8, 8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA | HAS_IRQ_BUG},
	{"ETRAX 100", 9, 8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA},

	{"ETRAX 100LX", 10, 8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA | HAS_USB
			     | HAS_MMU | HAS_MMU_BUG},

	{"ETRAX 100LX v2", 11, 8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA | HAS_USB
			        | HAS_MMU},
#ifdef CONFIG_ETRAXFS
	{"ETRAX FS", 32, 32, HAS_ETHERNET100 | HAS_ATA | HAS_MMU},
#else
	{"ARTPEC-3", 32, 32, HAS_ETHERNET100 | HAS_MMU},
#endif
	{"Unknown", 0, 0, 0}
};

int show_cpuinfo(struct seq_file *m, void *v)
{
	int i;
	int cpu = (int)v - 1;
	unsigned long revision;
	struct cpu_info *info;

	info = &cpinfo[ARRAY_SIZE(cpinfo) - 1];

#ifdef CONFIG_SMP
	if (!cpu_online(cpu))
		return 0;
#endif

	revision = rdvr();

	for (i = 0; i < ARRAY_SIZE(cpinfo); i++) {
		if (cpinfo[i].rev == revision) {
			info = &cpinfo[i];
			break;
		}
	}

	return seq_printf(m,
		"processor\t: %d\n"
		"cpu\t\t: CRIS\n"
		"cpu revision\t: %lu\n"
		"cpu model\t: %s\n"
		"cache size\t: %d KB\n"
		"fpu\t\t: %s\n"
		"mmu\t\t: %s\n"
		"mmu DMA bug\t: %s\n"
		"ethernet\t: %s Mbps\n"
		"token ring\t: %s\n"
		"scsi\t\t: %s\n"
		"ata\t\t: %s\n"
		"usb\t\t: %s\n"
		"bogomips\t: %lu.%02lu\n\n",

		cpu,
		revision,
		info->cpu_model,
		info->cache_size,
		info->flags & HAS_FPU ? "yes" : "no",
		info->flags & HAS_MMU ? "yes" : "no",
		info->flags & HAS_MMU_BUG ? "yes" : "no",
		info->flags & HAS_ETHERNET100 ? "10/100" : "10",
		info->flags & HAS_TOKENRING ? "4/16 Mbps" : "no",
		info->flags & HAS_SCSI ? "yes" : "no",
		info->flags & HAS_ATA ? "yes" : "no",
		info->flags & HAS_USB ? "yes" : "no",
		(loops_per_jiffy * HZ + 500) / 500000,
		((loops_per_jiffy * HZ + 500) / 5000) % 100);
}

#endif /* CONFIG_PROC_FS */

void show_etrax_copyright(void)
{
#ifdef CONFIG_ETRAXFS
	printk(KERN_INFO "Linux/CRISv32 port on ETRAX FS "
		"(C) 2003, 2004 Axis Communications AB\n");
#else
	printk(KERN_INFO "Linux/CRISv32 port on ARTPEC-3 "
		"(C) 2003-2009 Axis Communications AB\n");
#endif
}

static struct i2c_board_info __initdata i2c_info[] = {
	{I2C_BOARD_INFO("camblock", 0x43)},
	{I2C_BOARD_INFO("tmp100", 0x48)},
	{I2C_BOARD_INFO("tmp100", 0x4A)},
	{I2C_BOARD_INFO("tmp100", 0x4C)},
	{I2C_BOARD_INFO("tmp100", 0x4D)},
	{I2C_BOARD_INFO("tmp100", 0x4E)},
#ifdef CONFIG_RTC_DRV_PCF8563
	{I2C_BOARD_INFO("pcf8563", 0x51)},
#endif
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	{I2C_BOARD_INFO("vgpio", 0x20)},
	{I2C_BOARD_INFO("vgpio", 0x21)},
#endif
	{I2C_BOARD_INFO("pca9536", 0x41)},
	{I2C_BOARD_INFO("fnp300", 0x40)},
	{I2C_BOARD_INFO("fnp300", 0x42)},
	{I2C_BOARD_INFO("adc101", 0x54)},
};

static struct i2c_board_info __initdata i2c_info2[] = {
	{I2C_BOARD_INFO("camblock", 0x43)},
	{I2C_BOARD_INFO("tmp100", 0x48)},
	{I2C_BOARD_INFO("tmp100", 0x4A)},
	{I2C_BOARD_INFO("tmp100", 0x4C)},
	{I2C_BOARD_INFO("tmp100", 0x4D)},
	{I2C_BOARD_INFO("tmp100", 0x4E)},
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	{I2C_BOARD_INFO("vgpio", 0x20)},
	{I2C_BOARD_INFO("vgpio", 0x21)},
#endif
	{I2C_BOARD_INFO("pca9536", 0x41)},
	{I2C_BOARD_INFO("fnp300", 0x40)},
	{I2C_BOARD_INFO("fnp300", 0x42)},
	{I2C_BOARD_INFO("adc101", 0x54)},
};

static struct i2c_board_info __initdata i2c_info3[] = {
	{I2C_BOARD_INFO("adc101", 0x54)},
};

static int __init etrax_init(void)
{
	i2c_register_board_info(0, i2c_info, ARRAY_SIZE(i2c_info));
	i2c_register_board_info(1, i2c_info2, ARRAY_SIZE(i2c_info2));
	i2c_register_board_info(2, i2c_info3, ARRAY_SIZE(i2c_info3));
	return 0;
}
arch_initcall(etrax_init);
