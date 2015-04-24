#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/platform_device.h>

#include <linux/clk-provider.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/smp_twd.h>

#include <mach/hardware.h>
#include <mach/platform.h>

extern void shared_3ds_printf(const char *str, ...);

/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/

static struct map_desc nintendo3ds_ctr_io_desc[] __initdata = {
	{
		.virtual	= IO_ADDRESS(NINTENDO3DS_CTR_PRIV_MEM_BASE),
		.pfn		= __phys_to_pfn(NINTENDO3DS_CTR_PRIV_MEM_BASE),
		.length		= NINTENDO3DS_CTR_PRIV_MEM_SIZE,
		.type		= MT_DEVICE,
	}
};


#ifdef CONFIG_HAVE_ARM_TWD
static DEFINE_TWD_LOCAL_TIMER(twd_local_timer,
			      NINTENDO3DS_CTR_CPU_TIMER_WATCHDOG_BASE,
			      NINTENDO3DS_CTR_GIC_TIMER_ID);

static void __init nintendo3ds_ctr_twd_init(void)
{
	int err = twd_local_timer_register(&twd_local_timer);
	if (err)
		pr_err("twd_local_timer_register failed %d\n", err);
}
#else
#define nintendo3ds_ctr_twd_init()	do {} while(0)
#endif

static void __init nintendo3ds_ctr_map_io(void)
{
	shared_3ds_printf("nintendo3ds_ctr_map_io\n");
	//iotable_init(nintendo3ds_ctr_io_desc, ARRAY_SIZE(nintendo3ds_ctr_io_desc));
}

static void __init nintendo3ds_ctr_restart(enum reboot_mode mode, const char *cmd)
{
	shared_3ds_printf("nintendo3ds_ctr_restart\n");
}

static void __init nintendo3ds_ctr_init_irq(void)
{
	int i;

	shared_3ds_printf("nintendo3ds_ctr_init_irq\n");

	// Enable clear all the GID interrupts
	// Pending clear all the GID interrupts
	for (i = 0; i < 8; i++) {
		writel(0xFFFFFFFF, __io_address(NINTENDO3DS_CTR_GIC_INT_PEND_CLR_REG(i)));
		writel(0xFFFFFFFF, __io_address(NINTENDO3DS_CTR_GIC_INT_EN_CLR_REG(i)));
	}

	gic_init(0, 29, __io_address(NINTENDO3DS_CTR_GIC_BASE),
		__io_address(NINTENDO3DS_CTR_CPU_INT_INTERFACE_BASE));

	//shared_3ds_printf("nintendo3ds_ctr_init_irq DONE\n");
	
	//gic_init(unsigned int nr, int start, void __iomem *dist , void __iomem *cpu)
	
	// Enable CPU Interrupt Interface
	//writel(1, __io_address(NINTENDO3DS_CTR_CPU_INT_INTERFACE_CONTROL));

	// Enable Global Interrupt Distributor
	//writel(1, __io_address(NINTENDO3DS_CTR_GID_IDCR));
}

static void __init nintendo3ds_ctr_handle_irq(struct pt_regs *regs)
{

}

static void __init nintendo3ds_ctr_dt_init_time(void)
{
	shared_3ds_printf("nintendo3ds_ctr_dt_init_time\n");
	nintendo3ds_ctr_twd_init();
}



static void __init nintendo3ds_ctr_dt_init_machine(void)
{
	shared_3ds_printf("nintendo3ds_ctr_dt_init_machine\n");
	of_platform_populate(NULL, NULL, NULL, NULL);
}



static const char *nintendo3ds_ctr_dt_platform_compat[] __initconst = {
	"nintendo3ds,ctr",
	NULL,
};

DT_MACHINE_START(NINTENDO3DS_DT, "Nintendo 3DS (CTR) (Device Tree)")
	.init_irq     = nintendo3ds_ctr_init_irq,
	.map_io		  = nintendo3ds_ctr_map_io,
	.handle_irq   = nintendo3ds_ctr_handle_irq,
	.init_time    = nintendo3ds_ctr_dt_init_time,
	.init_machine = nintendo3ds_ctr_dt_init_machine,
	.dt_compat	  = nintendo3ds_ctr_dt_platform_compat,
	.restart      = nintendo3ds_ctr_restart,
MACHINE_END
