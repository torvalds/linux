#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/wakeup_reason.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include "pm.h"
#include <linux/irqchip/arm-gic.h>
#include "pm-pie.c"

__weak void rk_usb_power_down(void);
__weak void rk_usb_power_up(void);

#define GPIO_INTEN 0x30
#define GPIO_INT_STATUS 0x40
#define GIC_DIST_PENDING_SET 0x200
#define DUMP_GPIO_INTEN(ID)\
do { \
	u32 en = readl_relaxed(RK_GPIO_VIRT(ID) + GPIO_INTEN);\
	if (en) {\
		rkpm_ddr_printascii("GPIO" #ID "_INTEN: "); \
		rkpm_ddr_printhex(en); \
		rkpm_ddr_printch('\n'); \
	} \
} while (0)
#define RK312X_CRU_UNGATING_OPS(id) cru_writel(\
	CRU_W_MSK_SETBITS(0,  (id) % 16, 0x1), RK312X_CRU_GATEID_CONS((id)))
#define RK312X_CRU_GATING_OPS(id) cru_writel(\
	CRU_W_MSK_SETBITS(1, (id) % 16, 0x1), RK312X_CRU_GATEID_CONS((id)))

enum rk_plls_id {
	APLL_ID = 0,
	DPLL_ID,
	CPLL_ID,
	GPLL_ID,
	END_PLL_ID,
};

static inline void  uart_printch(char bbyte)
{
	u32 reg_save[2];
	u32 u_clk_id = (RK312X_CLKGATE_UART0_SRC + CONFIG_RK_DEBUG_UART * 2);
	u32 u_pclk_id = (RK312X_CLKGATE_PCLK_UART0 + CONFIG_RK_DEBUG_UART);

	reg_save[0] = cru_readl(RK312X_CRU_GATEID_CONS(u_clk_id));
	reg_save[1] = cru_readl(RK312X_CRU_GATEID_CONS(u_pclk_id));
	RK312X_CRU_UNGATING_OPS(u_clk_id);
	RK312X_CRU_UNGATING_OPS(u_pclk_id);
	rkpm_udelay(1);

write_uart:
	writel_relaxed(bbyte, RK_DEBUG_UART_VIRT);
	dsb();

	while (!(readl_relaxed(RK_DEBUG_UART_VIRT + 0x14) & 0x40))
		barrier();

	if (bbyte == '\n') {
		bbyte = '\r';
		goto write_uart;
	}

	cru_writel(reg_save[0] | CRU_W_MSK(u_clk_id
		% 16, 0x1), RK312X_CRU_GATEID_CONS(u_clk_id));
	cru_writel(reg_save[1] | CRU_W_MSK(u_pclk_id
		% 16, 0x1), RK312X_CRU_GATEID_CONS(u_pclk_id));

	if (0) {
write_uart1:
		writel_relaxed(bbyte, RK_DEBUG_UART_VIRT);
		dsb();

		while (!(readl_relaxed(RK_DEBUG_UART_VIRT + 0x14) & 0x40))
			barrier();
	if (bbyte == '\n') {
		bbyte = '\r';
		goto write_uart1;
		}
	}
}

void PIE_FUNC(sram_printch)(char byte)
{
	uart_printch(byte);
}
static void pll_udelay(u32 udelay)
{
	u32 mode;

	mode = cru_readl(RK312X_CRU_MODE_CON);
	cru_writel(RK312X_PLL_MODE_SLOW(APLL_ID), RK312X_CRU_MODE_CON);
	rkpm_udelay(udelay * 5);
	cru_writel(mode|(RK312X_PLL_MODE_MSK(APLL_ID)
		<< 16), RK312X_CRU_MODE_CON);
}

#define RK312X_PLL_PWR_DN_MSK (0x01 << 1)
#define RK312X_PLL_BYPASS CRU_W_MSK_SETBITS(1, 0xF, 0x01)
#define RK312X_PLL_NOBYPASS CRU_W_MSK_SETBITS(0, 0xF, 0x01)
#define RK312X_PLL_RESET CRU_W_MSK_SETBITS(1, 14, 0x01)
#define RK312X_PLL_RESET_RESUME CRU_W_MSK_SETBITS(0, 14, 0x01)
#define RK312X_PLL_POWERDOWN CRU_W_MSK_SETBITS(1, 0xD, 0x01)
#define RK312X_PLL_POWERON CRU_W_MSK_SETBITS(0, 0xD, 0x01)

static u32 plls_con0_save[END_PLL_ID];
static u32 plls_con1_save[END_PLL_ID];
static u32 plls_con2_save[END_PLL_ID];
static u32 cru_mode_con;

static void pm_pll_wait_lock(u32 pll_idx)
{
	u32 delay = 600000U;

	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	while (delay > 0) {
		if ((cru_readl(RK312X_PLL_CONS(pll_idx, 1)) & (0x1 << 10)))
			break;
		delay--;
	}
	if (delay == 0) {
		rkpm_ddr_printascii("unlock-pll:");
		rkpm_ddr_printhex(pll_idx);
		rkpm_ddr_printch('\n');
	}
}

static inline void plls_suspend(u32 pll_id)
{
	plls_con0_save[pll_id] = cru_readl(RK312X_PLL_CONS((pll_id), 0));
	plls_con1_save[pll_id] = cru_readl(RK312X_PLL_CONS((pll_id), 1));
	plls_con2_save[pll_id] = cru_readl(RK312X_PLL_CONS((pll_id), 2));

	/*cru_writel(RK312X_PLL_BYPASS, RK312X_PLL_CONS((pll_id), 0));*/
	cru_writel(RK312X_PLL_POWERDOWN, RK312X_PLL_CONS((pll_id), 1));
}
static inline void plls_resume(u32 pll_id)
{
	u32 pllcon0, pllcon1, pllcon2;

	/*cru_writel(RK312X_PLL_MODE_SLOW(pll_id), RK312X_CRU_MODE_CON);*/
	/*cru_writel(RK312X_PLL_NOBYPASS, RK312x_PLL_CONS((pll_id), 0));*/

	pllcon0 = plls_con0_save[pll_id];
	/*cru_readl(RK312x_PLL_CONS((pll_id),0));*/
	pllcon1 = plls_con1_save[pll_id];
	/*cru_readl(RK12x_PLL_CONS((pll_id),1));*/
	pllcon2 = plls_con2_save[pll_id];
	/*cru_readl(RK312x_PLL_CONS((pll_id),2));*/

	cru_writel(RK312X_PLL_POWERON, RK312X_PLL_CONS((pll_id), 1));

	pll_udelay(5);

	/*return form rest*/
	/*cru_writel(RK312X_PLL_RESET_RESUME, RK312X_PLL_CONS(pll_id, 1));*/

	/*wating lock state*/
	pll_udelay(168);
	pm_pll_wait_lock(pll_id);

}
static u32 clk_sel0, clk_sel1, clk_sel10, clk_sel24, clk_sel29;
static void pm_plls_suspend(void)
{
	clk_sel0 = cru_readl(RK312X_CRU_CLKSELS_CON(0));
	clk_sel1 = cru_readl(RK312X_CRU_CLKSELS_CON(1));
	clk_sel10 = cru_readl(RK312X_CRU_CLKSELS_CON(10));
	clk_sel24 = cru_readl(RK312X_CRU_CLKSELS_CON(24));
	clk_sel29 = cru_readl(RK312X_CRU_CLKSELS_CON(29));

	cru_mode_con = cru_readl(RK312X_CRU_MODE_CON);

	/*CPLL*/
	cru_writel(RK312X_PLL_MODE_SLOW(CPLL_ID), RK312X_CRU_MODE_CON);

	/*GPLL*/
	cru_writel(RK312X_PLL_MODE_SLOW(GPLL_ID), RK312X_CRU_MODE_CON);

	/*crypto*/
	cru_writel(CRU_W_MSK_SETBITS(3, 0, 0x3), RK312X_CRU_CLKSELS_CON(24));

	/* peri aclk hclk pclk*/
	cru_writel(0
		|CRU_W_MSK_SETBITS(0, 0, 0x1f)/*1 aclk*/
		|CRU_W_MSK_SETBITS(0, 8, 0x3)/*2   hclk 0 1:1,1 2:1 ,2 4:1*/
		|CRU_W_MSK_SETBITS(0, 12, 0x3)/* 2     0~3  1 2 4 8 div*/
		, RK312X_CRU_CLKSELS_CON(10));

	/* pmu*/
	cru_writel(CRU_W_MSK_SETBITS(0, 8, 0x1f), RK312X_CRU_CLKSELS_CON(29));
	plls_suspend(CPLL_ID);
	plls_suspend(GPLL_ID);

	/*apll*/
	cru_writel(RK312X_PLL_MODE_SLOW(APLL_ID), RK312X_CRU_MODE_CON);
	/*a7_core*/
	cru_writel(0 | CRU_W_MSK_SETBITS(0, 0, 0x1f)
	, RK312X_CRU_CLKSELS_CON(0));

	/*pclk_dbg*/
	cru_writel(0 | CRU_W_MSK_SETBITS(3, 0, 0x7)
	, RK312X_CRU_CLKSELS_CON(1));
	plls_suspend(APLL_ID);
}

static void pm_plls_resume(void)
{
	plls_resume(APLL_ID);
	cru_writel(clk_sel0 | CRU_W_MSK(0, 0x1f), RK312X_CRU_CLKSELS_CON(0));
	cru_writel(clk_sel1 | CRU_W_MSK(0, 0x7), RK312X_CRU_CLKSELS_CON(1));
	cru_writel(cru_mode_con
		|(RK312X_PLL_MODE_MSK(APLL_ID) << 16), RK312X_CRU_MODE_CON);

	/* pmu alive */
	plls_resume(GPLL_ID);
	/*peri aclk hclk pclk*/
	cru_writel(clk_sel10 | (CRU_W_MSK(0, 0x1f) | CRU_W_MSK(8, 0x3)
	| CRU_W_MSK(12, 0x3)), RK312X_CRU_CLKSELS_CON(10));
	/* crypto*/
	cru_writel(clk_sel24 | CRU_W_MSK(0, 0x3), RK312X_CRU_CLKSELS_CON(24));
	cru_writel(clk_sel29 | CRU_W_MSK(8, 0x1f), RK312X_CRU_CLKSELS_CON(29));
	cru_writel(cru_mode_con | (RK312X_PLL_MODE_MSK(GPLL_ID) << 16)
		, RK312X_CRU_MODE_CON);

	plls_resume(CPLL_ID);
	cru_writel(cru_mode_con | (RK312X_PLL_MODE_MSK(CPLL_ID) << 16)
		, RK312X_CRU_MODE_CON);
}
#ifdef CONFIG_RK_LAST_LOG
extern void rk_last_log_text(char *text, size_t size);
#endif

static void  ddr_printch(char byte)
{
	uart_printch(byte);
#ifdef CONFIG_RK_LAST_LOG
	rk_last_log_text(&byte, 1);

	if (byte == '\n') {
		byte = '\r';
		rk_last_log_text(&byte, 1);
	}
#endif
	pll_udelay(2);
}

static noinline void rk312x_pm_dump_inten(void)
{
	DUMP_GPIO_INTEN(0);
	DUMP_GPIO_INTEN(1);
	DUMP_GPIO_INTEN(2);
	DUMP_GPIO_INTEN(3);
}
static noinline void rk312x_pm_dump_irq(void)
{
	u32 irq[4];
	int i;

	u32 irq_gpio = (readl_relaxed(RK_GIC_VIRT +
GIC_DIST_PENDING_SET + 8) >> 4) & 0x0F;

	for (i = 0; i < ARRAY_SIZE(irq); i++)
		irq[i] = readl_relaxed(RK_GIC_VIRT +
GIC_DIST_PENDING_SET + (1 + i) * 4);
	for (i = 0; i < ARRAY_SIZE(irq); i++) {
		if (irq[i])
			log_wakeup_reason(32 * (i + 1) + fls(irq[i]) - 1);
	}
	for (i = 0; i <= 3; i++) {
		if (irq_gpio & (1 << i)) {
			pr_debug("wakeup gpio%d: %08x\n", i
				, readl_relaxed(RK_GPIO_VIRT(i)
				+ GPIO_INT_STATUS));
			rkpm_ddr_printascii("wakeup gpio");
			rkpm_ddr_printhex(i);
			rkpm_ddr_printascii(":");
			rkpm_ddr_printhex(readl_relaxed(RK_GPIO_VIRT(i)
				+ GPIO_INT_STATUS));
			rkpm_ddr_printch('\n');
		}
	}
}

static void rkpm_prepare(void)
{
	rk312x_pm_dump_inten();
}
static void rkpm_finish(void)
{
	rk312x_pm_dump_irq();
}
enum rk312x_pwr_mode_con {
	pmu_power_mode_en = 0,
	pmu_clk_core_src_gate_en,
	pmu_clk_bus_src_gate_en,
	pmu_global_int_disable,

	pmu_core_pd_en,
	pmu_use_if,
	pmu_wait_osc_24m,
	pmu_sref_enter_en,

	pmu_ddr_gating_en,
	pmu_int_en,
	pmu_ddr0io_ret_de_req,

	pmu_clr_crypto = 16,
	pmu_clr_msch,
	pmu_clr_sys,
	pmu_clr_core,

	pmu_clr_gpu,
	pmu_clr_vio,
	pmu_clr_video,
	pmu_clr_peri,
};
#define SOC_REMAP 12
#define GRF_SOC_CON0 0x140

#define RK312X_IMEM_VIRT (RK_BOOTRAM_VIRT + SZ_32K)
#define RKPM_BOOTRAM_PHYS (RK312X_IMEM_PHYS)
#define RKPM_BOOTRAM_BASE (RK312X_IMEM_VIRT)
#define RKPM_BOOTRAM_SIZE (RK312X_IMEM_SIZE)

#define RKPM_BOOT_CODE_OFFSET (0x0)
#define RK312XPM_BOOT_CODE_SIZE	(0x700)

#define RK312XPM_BOOT_DATA_OFFSET (RKPM_BOOT_CODE_OFFSET \
	+ RK312XPM_BOOT_CODE_SIZE)
#define RKPM_BOOT_DATA_SIZE	(RKPM_BOOTDATA_ARR_SIZE*4)

#define RK312XPM_BOOT_DDRCODE_OFFSET (RK312XPM_BOOT_DATA_OFFSET\
	+RKPM_BOOT_DATA_SIZE)

#define  RKPM_BOOT_CODE_PHY  (RKPM_BOOTRAM_PHYS + RKPM_BOOT_CODE_OFFSET)
#define  RKPM_BOOT_CODE_BASE  (RKPM_BOOTRAM_BASE + RKPM_BOOT_CODE_OFFSET)

#define  RKPM_BOOT_DATA_PHY  (RKPM_BOOTRAM_PHYS + RK312XPM_BOOT_DATA_OFFSET)
#define  RKPM_BOOT_DATA_BASE  (RKPM_BOOTRAM_BASE + RK312XPM_BOOT_DATA_OFFSET)

/*ddr resume data in boot ram*/
#define  RKPM_BOOT_DDRCODE_PHY   (RKPM_BOOTRAM_PHYS \
	+ RK312XPM_BOOT_DDRCODE_OFFSET)
#define  RKPM_BOOT_DDRCODE_BASE  (RKPM_BOOTRAM_BASE \
	+ RK312XPM_BOOT_DDRCODE_OFFSET)


/*#define RKPM_BOOT_CPUSP_PHY (RKPM_BOOTRAM_PHYS+((RKPM_BOOTRAM_SIZE-1)&~0x7))*/
#define RKPM_BOOT_CPUSP_PHY (0x00 + ((RKPM_BOOTRAM_SIZE - 1) & (~(0x7))))

/*the value is used to control cpu resume flow*/
static u32 sleep_resume_data[RKPM_BOOTDATA_ARR_SIZE];
static char *resume_data_base = (char *)(RKPM_BOOT_DATA_BASE);
/*static char *resume_data_phy=  (char *)( RKPM_BOOT_DATA_PHY);*/

/*****save boot sram**********************/
#define BOOT_RAM_SAVE_SIZE (RKPM_BOOTRAM_SIZE + 4 * 10)
#define INT_RAM_SIZE (64 * 1024)
static char boot_ram_data[BOOT_RAM_SAVE_SIZE];/*8K + 40byte*/
static char int_ram_data[INT_RAM_SIZE];
/******resume code *****************
#define RKPM_BOOT_CODE_OFFSET (0x00)
#define RKPM_BOOT_CODE_PHY (RKPM_BOOTRAM_PHYS + PM_BOOT_CODE_OFFSET)
#define RKPM_BOOT_CODE_BASE (RKPM_BOOTRAM_BASE + PM_BOOT_CODE_OFFSET)
#define RKPM_BOOT_CODE_SIZE (0x100)
**************************************************/
extern void rk312x_pm_slp_cpu_resume(void);

static void sram_data_for_sleep(char *boot_save, char *int_save, u32 flag)
{
	char *addr_base, *addr_phy, *data_src, *data_dst;
	u32 sr_size, data_size;

	addr_base = (char *)RKPM_BOOTRAM_BASE;
	addr_phy = (char *)RKPM_BOOTRAM_PHYS;
	sr_size = RKPM_BOOTRAM_SIZE;  /*SZ8k*/
	/**********save boot sarm***********************************/
	if (boot_save)
		memcpy(boot_save, addr_base, sr_size);
	/**********move  resume code and data to boot sram*************/
	data_dst = (char *)RKPM_BOOT_CODE_BASE;
	data_src = (char *)rk312x_pm_slp_cpu_resume;
	data_size = RK312XPM_BOOT_CODE_SIZE;

	memcpy(data_dst, data_src, data_size);
	data_dst = (char *)resume_data_base;
	data_src = (char *)sleep_resume_data;
	data_size = sizeof(sleep_resume_data);
	memcpy((char *)data_dst, (char *)data_src, data_size);
}
static u32 rk312x_powermode;
static u32 pmu_pwrmode_con;
static u32 gpio_pmic_sleep_mode;
static u32 grf_soc_con0;
static u32 pmu_wakeup_conf;
static u32 pmic_sleep_gpio;

static u32 rkpm_slp_mode_set(u32 ctrbits)
{
	u32 pwr_mode_config;

	if ((RKPM_CTR_ARMOFF_LPMD & ctrbits) == 0)
		return 0;

	pmu_wakeup_conf = pmu_readl(RK312X_PMU_WAKEUP_CFG);
	/*grf_soc_con0 = grf_readl(GRF_SOC_CON0);*/
	/*grf_writel((1 << SOC_REMAP)
	| (1 << (SOC_REMAP + 16)), GRF_SOC_CON0);*/
	/*grf_gpio1a_iomux = grf_readl(0x00b8);*/
	/*grf_writel(0x00030003, 0xb8);*/

	pmu_writel(0x01, RK312X_PMU_WAKEUP_CFG);
	/* arm interrupt wake-up enable*/
	/*grf_writel((1 << SOC_REMAP)
	| (1 << (SOC_REMAP + 16)), GRF_SOC_CON0);*/
	/*remap bit control, when soc_remap = 1, the bootrom is mapped to
	 address 0x10100000,and internal memory is mapped to address 0x0.*/

	/*grf_writel(0X00030002, 0xb4);
	rk3126 GPIO1A1 : RK3128 GPIO3C1 iomux pmic-sleep*/
	if (pmic_sleep_gpio == 0x1a10) {
		ddr_printch('a');
		gpio_pmic_sleep_mode = grf_readl(0xb8);
		grf_writel(0X000C000C, 0xb8);
	}
	/*rk3126 GPIO1A1 : RK3128 GPIO3C1 iomux pmic-sleep*/
	if (pmic_sleep_gpio == 0x3c10) {
		ddr_printch('c');
		gpio_pmic_sleep_mode = grf_readl(0xe0);
		grf_writel(0X000C0008, 0xe0);
	}
	/*rk3126 GPIO3C1 : RK3128 GPIO3C1 iomux pmic-sleep*/

	pwr_mode_config = BIT(pmu_power_mode_en) | BIT(pmu_global_int_disable);
	pmu_pwrmode_con = pmu_readl(RK312X_PMU_PWRMODE_CON);
	if (rkpm_chk_val_ctrbits(ctrbits, RKPM_CTR_IDLEAUTO_MD)) {
		rkpm_ddr_printascii("-autoidle-");
		pwr_mode_config |= BIT(pmu_clk_core_src_gate_en);
	} else if (rkpm_chk_val_ctrbits(ctrbits, RKPM_CTR_ARMDP_LPMD)) {
		rkpm_ddr_printascii("-armdp-");
		pwr_mode_config |= BIT(pmu_core_pd_en);
	} else if (rkpm_chk_val_ctrbits(ctrbits, RKPM_CTR_ARMOFF_LPMD)) {
		rkpm_ddr_printascii("-armoff-");
		/*arm power off */
		pwr_mode_config |= 0
				|BIT(pmu_clk_core_src_gate_en)
				|BIT(pmu_clk_bus_src_gate_en)
				| BIT(pmu_core_pd_en)
			/*	| BIT(pmu_use_if)//aaa*/
			/*	| BIT(pmu_sref_enter_en)*/
				|BIT(pmu_int_en)
			/*	| BIT(pmu_wait_osc_24m)*/
			/*	| BIT(pmu_ddr_gating_en)*/
			/*	| BIT(pmu_ddr0io_ret_de_req)*/
				| BIT(pmu_clr_core)
			/*	| BIT(pmu_clr_crypto)*/
			/*	| BIT(pmu_clr_sys)*/
				/*| BIT(pmu_clr_vio)*/
				/*| BIT(pmu_clr_video)*/
			/*| BIT(pmu_clr_peri)*/
			/*	| BIT(pmu_clr_msch)*/
				/*| BIT(pmu_clr_gpu) */
				;
	} else if (rkpm_chk_val_ctrbits(ctrbits, RKPM_CTR_ARMOFF_LPMD)) {
		rkpm_ddr_printascii("-armoff ddr -");
		/*arm power off */
		pwr_mode_config |= 0
				|BIT(pmu_clk_core_src_gate_en)
				|BIT(pmu_clk_bus_src_gate_en)
				| BIT(pmu_core_pd_en)
			/*	| BIT(pmu_use_if)//aaa*/
				| BIT(pmu_sref_enter_en)
				|BIT(pmu_int_en)
			/*	| BIT(pmu_wait_osc_24m)*/
				| BIT(pmu_ddr_gating_en)
				| BIT(pmu_ddr0io_ret_de_req)
				| BIT(pmu_clr_core)
				| BIT(pmu_clr_crypto)
				| BIT(pmu_clr_sys)
				/*| BIT(pmu_clr_vio)*/
				/*| BIT(pmu_clr_video)*/
				| BIT(pmu_clr_peri)
				| BIT(pmu_clr_msch)
				/*| BIT(pmu_clr_gpu) */
				;
	}
	pmu_writel(32 * 30, RK312X_PMU_OSC_CNT);
	pmu_writel(0xbb80, RK312X_PMU_CORE_PWRUP_CNT);
	pmu_writel(pwr_mode_config, RK312X_PMU_PWRMODE_CON);
	rk312x_powermode = pwr_mode_config;
	return pmu_readl(RK312X_PMU_PWRMODE_CON);
}

static void sram_code_data_save(u32 pwrmode)
{
	sleep_resume_data[RKPM_BOOTDATA_L2LTY_F] = 0;
	if (rkpm_chk_ctrbits(RKPM_CTR_VOL_PWM0))
		sleep_resume_data[RKPM_BOOTDATA_ARM_ERRATA_818325_F] |= 0x01;
	if (rkpm_chk_ctrbits(RKPM_CTR_VOL_PWM1))
		sleep_resume_data[RKPM_BOOTDATA_ARM_ERRATA_818325_F] |= 0x02;
	if (rkpm_chk_ctrbits(RKPM_CTR_VOL_PWM2))
		sleep_resume_data[RKPM_BOOTDATA_ARM_ERRATA_818325_F] |= 0x04;
	sleep_resume_data[RKPM_BOOTDATA_DDR_F] = 0;
	sleep_resume_data[RKPM_BOOTDATA_CPUSP] = RKPM_BOOT_CPUSP_PHY;
	/*in sys resume ,ddr is need resume*/
	sleep_resume_data[RKPM_BOOTDATA_CPUCODE] = virt_to_phys(cpu_resume);
	/*in sys resume ,ddr is need resume*/

	sram_data_for_sleep(boot_ram_data, int_ram_data, 1);
	flush_cache_all();
	outer_flush_all();
	local_flush_tlb_all();
}

#define  RK_GICD_BASE (RK_GIC_VIRT)
#define RK_GICC_BASE (RK_GIC_VIRT+RK312X_GIC_DIST_SIZE)
#define PM_IRQN_START 32
#define PM_IRQN_END	107
#define gic_reg_dump(a, b, c)  {}
static u32 slp_gic_save[260+50];

static void rkpm_gic_dist_save(u32 *context)
{
	int i = 0, j, irqstart = 0;
	unsigned int gic_irqs;

	gic_irqs = readl_relaxed(RK_GICD_BASE + GIC_DIST_CTR) & 0x1f;
	gic_irqs = (gic_irqs + 1) * 32;
	if (gic_irqs > 1020)
		gic_irqs = 1020;

	irqstart = PM_IRQN_START;

	i = 0;

	for (j = irqstart; j < gic_irqs; j += 16)
		context[i++] = readl_relaxed(RK_GICD_BASE
		+ GIC_DIST_CONFIG + (j * 4) / 16);
	gic_reg_dump("gic level", j, RK_GICD_BASE + GIC_DIST_CONFIG);

	/*
	* Set all global interrupts to this CPU only.
	*/
	for (j = 0; j < gic_irqs; j += 4)
		context[i++] = readl_relaxed(RK_GICD_BASE
		+ GIC_DIST_TARGET +	(j * 4) / 4);
	gic_reg_dump("gic trig", j, RK_GICD_BASE + GIC_DIST_TARGET);

	for (j = 0; j < gic_irqs; j += 4)
		context[i++] = readl_relaxed(RK_GICD_BASE
		+ GIC_DIST_PRI + (j * 4) / 4);
	gic_reg_dump("gic pri", j, RK_GICD_BASE + GIC_DIST_PRI);

	for (j = 0; j < gic_irqs; j += 32)
		context[i++] = readl_relaxed(RK_GICD_BASE
		+ GIC_DIST_IGROUP + (j * 4) / 32);
	gic_reg_dump("gic secure", j, RK_GICD_BASE + 0x80);

	for (j = irqstart; j < gic_irqs; j += 32)
		context[i++] = readl_relaxed(RK_GICD_BASE
		+ GIC_DIST_PENDING_SET + (j * 4) / 32);
	gic_reg_dump("gic PENDING",  j,  RK_GICD_BASE
		+ GIC_DIST_PENDING_SET);

	for (j = 0; j < gic_irqs; j += 32)
		context[i++] = readl_relaxed(RK_GICD_BASE
		+ GIC_DIST_ENABLE_SET + (j * 4) / 32);
	gic_reg_dump("gic en", j, RK_GICD_BASE + GIC_DIST_ENABLE_SET);
	gic_reg_dump("gicc", 0x1c, RK_GICC_BASE);
	gic_reg_dump("giccfc", 0,  RK_GICC_BASE + 0xfc);

	context[i++] = readl_relaxed(RK_GICC_BASE + GIC_CPU_PRIMASK);
	context[i++] = readl_relaxed(RK_GICD_BASE + GIC_DIST_CTRL);
	context[i++] = readl_relaxed(RK_GICC_BASE + GIC_CPU_CTRL);
	/*
	context[i++]=readl_relaxed(RK_GICC_BASE + GIC_CPU_BINPOINT);
	context[i++]=readl_relaxed(RK_GICC_BASE + GIC_CPU_PRIMASK);
	context[i++]=readl_relaxed(RK_GICC_BASE + GIC_DIST_SOFTINT);
	context[i++]=readl_relaxed(RK_GICC_BASE + GIC_CPU_CTRL);
	context[i++]=readl_relaxed(RK_GICD_BASE + GIC_DIST_CTRL);
	*/
	for (j = irqstart; j < gic_irqs; j += 32) {
		writel_relaxed(0xffffffff, RK_GICD_BASE
			+ GIC_DIST_ENABLE_CLEAR + j * 4 / 32);
		dsb();
	}
	writel_relaxed(0xffff0000, RK_GICD_BASE + GIC_DIST_ENABLE_CLEAR);
	writel_relaxed(0x0000ffff, RK_GICD_BASE + GIC_DIST_ENABLE_SET);

	writel_relaxed(0, RK_GICC_BASE + GIC_CPU_CTRL);
	writel_relaxed(0, RK_GICD_BASE + GIC_DIST_CTRL);
}
static void  rkpm_peri_save(u32 power_mode)
{
	rkpm_gic_dist_save(&slp_gic_save[0]);
}

static void rkpm_save_setting(u32 ctrbits)
{
	u32 pd_cpu;

	if ((RKPM_CTR_ARMOFF_LPMD & ctrbits) == 0)
		return;

	rkpm_slp_mode_set(ctrbits);
	if (rk312x_powermode & BIT(pmu_power_mode_en)) {
		sram_code_data_save(rk312x_powermode);
		rkpm_peri_save(rk312x_powermode);
	}
	grf_soc_con0 = grf_readl(GRF_SOC_CON0);

	grf_writel((1 << SOC_REMAP) | (1 << (SOC_REMAP + 16)), GRF_SOC_CON0);

	for (pd_cpu = PD_CPU_1; pd_cpu <= PD_CPU_3; pd_cpu++) {
		writel_relaxed(0x20000 << (pd_cpu - PD_CPU_1),
			       RK_CRU_VIRT + RK312X_CRU_SOFTRSTS_CON(0));
		dsb();
		udelay(10);
	}
}

#define UART_DLL	0	/* Out: Divisor Latch Low */
#define UART_DLM	1	/* Out: Divisor Latch High */
#define UART_IER	1
#define UART_FCR	2
#define UART_LCR	3	/* Out: Line Control Register */
#define UART_MCR	4

static void slp312x_uartdbg_resume(void)
{
	void __iomem *b_addr = RK_DEBUG_UART_VIRT;
	u32 pclk_id = RK312X_CLKGATE_PCLK_UART2;
	u32 clk_id = (RK312X_CLKGATE_UART0_SRC + 2 * 2);
	u32 gate_reg[2];
	u32 rfl_reg, lsr_reg;

	gate_reg[0] = cru_readl(RK312X_CRU_GATEID_CONS(pclk_id));
	gate_reg[1] = cru_readl(RK312X_CRU_GATEID_CONS(clk_id));

	RK312X_CRU_UNGATING_OPS(pclk_id);
	grf_writel(0x00f00000, 0x00c0);

	do {
		cru_writel(CRU_W_MSK_SETBITS(0x2, 8, 0x3)
			, RK312X_CRU_CLKSELS_CON(16));
		cru_writel(0|CRU_W_MSK_SETBITS(1, 9, 0x1)
			, RK312X_CRU_SOFTRSTS_CON(2));
		dsb();
		dsb();
		rkpm_udelay(10);
		cru_writel(0 | CRU_W_MSK_SETBITS(0, 9, 0x1)
			, RK312X_CRU_SOFTRSTS_CON(2));

		reg_writel(0x83, b_addr + UART_LCR*4);

		reg_writel(0xd, b_addr + UART_DLL*4);
		reg_writel(0x0, b_addr + UART_DLM*4);

		reg_writel(0x3, b_addr + UART_LCR*4);

		reg_writel(0x5, b_addr + UART_IER*4);
		reg_writel(0xc1, b_addr + UART_FCR*4);

		rfl_reg = readl_relaxed(b_addr + 0x84);
		lsr_reg = readl_relaxed(b_addr + 0x14);
	} while ((rfl_reg & 0x1f) || (lsr_reg & 0xf));

	cru_writel(CRU_W_MSK_SETBITS(0x2, 8, 0x3), RK312X_CRU_CLKSELS_CON(16));

	grf_writel(0x00f000a0, 0x00c0);

	cru_writel(gate_reg[0] | CRU_W_MSK(pclk_id % 16, 0x1)
		, RK312X_CRU_GATEID_CONS(pclk_id));
	cru_writel(gate_reg[1] | CRU_W_MSK(clk_id % 16, 0x1)
		, RK312X_CRU_GATEID_CONS(clk_id));
}

static void rkpm_gic_dist_resume(u32 *context)
{
	int i = 0, j, irqstart = 0;
	unsigned int gic_irqs;

	gic_irqs = readl_relaxed(RK_GICD_BASE + GIC_DIST_CTR) & 0x1f;
	gic_irqs = (gic_irqs + 1) * 32;
	if (gic_irqs > 1020)
		gic_irqs = 1020;

	irqstart = PM_IRQN_START;

	writel_relaxed(0, RK_GICC_BASE + GIC_CPU_CTRL);
	dsb();
	writel_relaxed(0, RK_GICD_BASE + GIC_DIST_CTRL);
	dsb();
	for (j = irqstart; j < gic_irqs; j += 32) {
		writel_relaxed(0xffffffff, RK_GICD_BASE
			+ GIC_DIST_ENABLE_CLEAR + j * 4 / 32);
		dsb();
	}

	i = 0;

	for (j = irqstart; j < gic_irqs; j += 16) {
		writel_relaxed(context[i++], RK_GICD_BASE
			+ GIC_DIST_CONFIG + j * 4 / 16);
		dsb();
	}
	gic_reg_dump("gic level",  j,  RK_GICD_BASE + GIC_DIST_CONFIG);

	/*
	* Set all global interrupts to this CPU only.
	*/
	for (j = 0; j < gic_irqs; j += 4) {
		writel_relaxed(context[i++], RK_GICD_BASE
			+ GIC_DIST_TARGET +  (j * 4) / 4);
		dsb();
	}
	gic_reg_dump("gic target", j, RK_GICD_BASE + GIC_DIST_TARGET);

	for (j = 0; j < gic_irqs; j += 4) {
		writel_relaxed(context[i++], RK_GICD_BASE
			+ GIC_DIST_PRI + (j * 4) / 4);
		dsb();
	}
	gic_reg_dump("gic pri",  j,  RK_GICD_BASE + GIC_DIST_PRI);

	for (j = 0; j < gic_irqs; j += 32) {
		writel_relaxed(context[i++], RK_GICD_BASE
			+ GIC_DIST_IGROUP + (j * 4) / 32);
		dsb();
	}
	gic_reg_dump("gic secu",  j, RK_GICD_BASE + 0x80);

	for (j = irqstart; j < gic_irqs; j += 32) {
		/*writel_relaxed(context[i++],
		RK_GICD_BASE + GIC_DIST_PENDING_SET + j * 4 / 32);*/
		i++;
		dsb();
	}

	gic_reg_dump("gic pending",  j,  RK_GICD_BASE + GIC_DIST_PENDING_SET);

	if (0) {
		for (j = 0; j < gic_irqs; j += 32) {
			writel_relaxed(context[i++], RK_GICD_BASE
				+ GIC_DIST_ENABLE_CLEAR + j * 4 / 32);
			dsb();
		}
		gic_reg_dump("gic disable", j, RK_GICD_BASE
			+ GIC_DIST_ENABLE_CLEAR);
	} else {
		for (j = irqstart; j < gic_irqs; j += 32)
			writel_relaxed(0xffffffff, RK_GICD_BASE
			+ GIC_DIST_ENABLE_CLEAR + j * 4 / 32);
		writel_relaxed(0xffff0000,  RK_GICD_BASE
			+ GIC_DIST_ENABLE_CLEAR);
		writel_relaxed(0x0000ffff,  RK_GICD_BASE + GIC_DIST_ENABLE_SET);
	}

	/*enable*/
	for (j = 0; j < gic_irqs; j += 32) {
		writel_relaxed(context[i++], RK_GICD_BASE
			+ GIC_DIST_ENABLE_SET + (j * 4) / 32);
		dsb();
	}

	gic_reg_dump("gic enable", j, RK_GICD_BASE + GIC_DIST_ENABLE_SET);

	writel_relaxed(context[i++], RK_GICC_BASE + GIC_CPU_PRIMASK);
	writel_relaxed(context[i++], RK_GICD_BASE + GIC_DIST_CTRL);
	writel_relaxed(context[i++], RK_GICC_BASE + GIC_CPU_CTRL);

	gic_reg_dump("gicc", 0x1c, RK_GICC_BASE);
	gic_reg_dump("giccfc", 0, RK_GICC_BASE + 0xfc);
}

static void sram_data_resume(char *boot_save, char *int_save, u32 flag)
{
	char *addr_base, *addr_phy;
	u32 sr_size;

	addr_base = (char *)RKPM_BOOTRAM_BASE;
	addr_phy = (char *)RKPM_BOOTRAM_PHYS;
	sr_size = RKPM_BOOTRAM_SIZE;
	/* save boot sram*/
	if (boot_save)
		memcpy(addr_base, boot_save,  sr_size);

	flush_icache_range((unsigned long)addr_base
		, (unsigned long)addr_base + sr_size);
	outer_clean_range((phys_addr_t) addr_phy
		, (phys_addr_t)addr_phy+sr_size);
}

static inline void sram_code_data_resume(u32 pwrmode)
{
	sram_data_resume(boot_ram_data
		, int_ram_data, sleep_resume_data[RKPM_BOOTDATA_DDR_F]);
}

static inline void  rkpm_slp_mode_set_resume(void)
{
	u32 pd_cpu;

	pmu_writel(pmu_wakeup_conf, RK312X_PMU_WAKEUP_CFG);
	/* arm interrupt wake-up enable*/
	pmu_writel(pmu_pwrmode_con, RK312X_PMU_PWRMODE_CON);

	for (pd_cpu = PD_CPU_1; pd_cpu <= PD_CPU_3; pd_cpu++) {
		writel_relaxed(0x20002 << (pd_cpu - PD_CPU_1),
			       RK_CRU_VIRT + RK312X_CRU_SOFTRSTS_CON(0));
		dsb();
		udelay(10);
	}

	grf_writel(grf_soc_con0 | (1 << (SOC_REMAP + 16)), GRF_SOC_CON0);

	if ((pmic_sleep_gpio == 0) || (pmic_sleep_gpio == 0x1a10))
		grf_writel(0X000C0000 | gpio_pmic_sleep_mode, 0xb8);
	/*rk3126 GPIO1A1 : RK3128 GPIO3C1 iomux pmic-sleep*/
	if (pmic_sleep_gpio == 0x3c10)
		grf_writel(0X000C0000 | gpio_pmic_sleep_mode, 0xe0);
}

void fiq_glue_resume(void);

static inline void  rkpm_peri_resume(u32 power_mode)
{
	rkpm_gic_dist_resume(&slp_gic_save[0]);
#ifndef CONFIG_ARM_TRUSTZONE
	fiq_glue_resume();
#endif
}

static void rkpm_save_setting_resume(void)
{
	if (rk312x_powermode == 0)
		return;

	rkpm_slp_mode_set_resume();
	if (rk312x_powermode & BIT(pmu_power_mode_en)) {
		rkpm_peri_resume(rk312x_powermode);
		sram_code_data_resume(rk312x_powermode);
	}
}
static inline void  rkpm_peri_resume_first(u32 power_mode)
{
	slp312x_uartdbg_resume();
}
extern void rk_sram_suspend(void);
static void rkpm_slp_setting(void)
{
	rk_usb_power_down();
	rk_sram_suspend();
}
static void rkpm_save_setting_resume_first(void)
{
	rk_usb_power_up();
	rkpm_peri_resume_first(pmu_pwrmode_con);
}
static u32 clk_ungt_msk[RK312X_CRU_CLKGATES_CON_CNT];
/*first clk gating setting*/

static u32 clk_ungt_msk_1[RK312X_CRU_CLKGATES_CON_CNT];
/* first clk gating setting*/

static u32 clk_ungt_save[RK312X_CRU_CLKGATES_CON_CNT];
/*first clk gating value saveing*/

static u32 *p_rkpm_clkgt_last_set;
#define CLK_MSK_GATING(msk, con) cru_writel((msk << 16) | 0xffff, con)
#define CLK_MSK_UNGATING(msk, con) cru_writel(((~msk) << 16) | 0xffff, con)

static void gtclks_suspend(void)
{
	int i;

	for (i = 0; i < RK312X_CRU_CLKGATES_CON_CNT; i++) {
		clk_ungt_save[i] = cru_readl(RK312X_CRU_CLKGATES_CON(i));
		CLK_MSK_UNGATING(clk_ungt_msk[i], RK312X_CRU_CLKGATES_CON(i));
	}
}

static void gtclks_resume(void)
{
	int i;

	for (i = 0; i < RK312X_CRU_CLKGATES_CON_CNT; i++)
		cru_writel(clk_ungt_save[i] | 0xffff0000
		, RK312X_CRU_CLKGATES_CON(i));
}
static void clks_gating_suspend_init(void)
{
	p_rkpm_clkgt_last_set = &clk_ungt_msk_1[0];
	if (clk_suspend_clkgt_info_get(clk_ungt_msk, p_rkpm_clkgt_last_set
		, RK312X_CRU_CLKGATES_CON_CNT) == RK312X_CRU_CLKGATES_CON(0))
		rkpm_set_ops_gtclks(gtclks_suspend, gtclks_resume);
}
static void pmic_sleep_gpio_get_dts_info(struct device_node *parent)
{
	struct property *prop;

	prop = of_find_property(parent, "rockchip,pmic-suspend_gpios", NULL);
	if (!prop)
		return;
	if (!prop->value)
		return;

	of_property_read_u32_array(parent, "rockchip,pmic-suspend_gpios"
		, &pmic_sleep_gpio, 1);
}

#define SRAM_LOOPS_PER_USEC     24
#define SRAM_LOOP(loops)       \
	do {\
		unsigned int i = (loops);\
		if (i < 7)\
			i = 7;\
	barrier();\
	asm volatile(".align 4; 1: subs %0, %0, #1; bne 1b;" : "+r" (i));\
	} while (0)
/* delay on slow mode */
#define sram_udelay(usecs)      SRAM_LOOP((usecs)*SRAM_LOOPS_PER_USEC)
/* delay on deep slow mode */
#define sram_32k_udelay(usecs)  SRAM_LOOP(((usecs)*\
	SRAM_LOOPS_PER_USEC)/(24000000/32768))

void PIE_FUNC(ddr_suspend)(void);
void PIE_FUNC(ddr_resume)(void);

#define PWM_VOLTAGE 0x600

void PIE_FUNC(pwm_regulator_suspend)(void)
{
	int gpio0_inout;
	int gpio0_ddr;

	cru_writel(0x1e000000, 0xf0);

	if (rkpm_chk_sram_ctrbit(RKPM_CTR_VOL_PWM0)) {
		grf_writel(0x00100000, 0xb4);/*iomux  gpio0d2*/
		gpio0_inout = readl_relaxed(RK_GPIO_VIRT(0) + 0x04);
		gpio0_ddr = readl_relaxed(RK_GPIO_VIRT(0));
		writel_relaxed(gpio0_inout | 0x04000000
			, RK_GPIO_VIRT(0) + 0x04);
		dsb();
		writel_relaxed(gpio0_ddr | 0x04000000, RK_GPIO_VIRT(0));
	}

	if (rkpm_chk_sram_ctrbit(RKPM_CTR_VOL_PWM1)) {
		grf_writel(0x00400000, 0xb4);/*iomux  gpio0d3*/
		gpio0_inout = readl_relaxed(RK_GPIO_VIRT(0) + 0x04);
		gpio0_ddr = readl_relaxed(RK_GPIO_VIRT(0));
		writel_relaxed(gpio0_inout | 0x08000000
			, RK_GPIO_VIRT(0) + 0x04);
		dsb();
		writel_relaxed(gpio0_ddr | 0x08000000, RK_GPIO_VIRT(0));
	}

	if (rkpm_chk_sram_ctrbit(RKPM_CTR_VOL_PWM2)) {
		grf_writel(0x01000000, 0xb4);/*iomux  gpio0d4*/
		gpio0_inout = readl_relaxed(RK_GPIO_VIRT(0) + 0x04);
		gpio0_ddr = readl_relaxed(RK_GPIO_VIRT(0));
		writel_relaxed(gpio0_inout | 0x10000000
			, RK_GPIO_VIRT(0) + 0x04);
		dsb();
		writel_relaxed(gpio0_ddr | 0x10000000, RK_GPIO_VIRT(0));
	}

}

void PIE_FUNC(pwm_regulator_resume)(void)
{

}


static void reg_pread(void)
{
	int i;
	volatile u32 n;

	volatile u32 *temp = (volatile unsigned int *)rockchip_sram_virt;

	flush_cache_all();
	outer_flush_all();
	local_flush_tlb_all();

	for (i = 0; i < 2; i++) {
		n = temp[1024 * i];
		barrier();
	}

	n = readl_relaxed(RK_GPIO_VIRT(0));
	n = readl_relaxed(RK_GPIO_VIRT(1));
	n = readl_relaxed(RK_GPIO_VIRT(1) + 4);

	n = readl_relaxed(RK_GPIO_VIRT(2));
	n = readl_relaxed(RK_GPIO_VIRT(3));

	n = readl_relaxed(RK_DEBUG_UART_VIRT);
	n = readl_relaxed(RK_CPU_AXI_BUS_VIRT);
	n = readl_relaxed(RK_DDR_VIRT);
	n = readl_relaxed(RK_GRF_VIRT);
	n = readl_relaxed(RK_CRU_VIRT);
	n = readl_relaxed(RK_PMU_VIRT);
	n = readl_relaxed(RK_PWM_VIRT);
}
void PIE_FUNC(msch_bus_idle_request)(void)
{
	u32 val;

	rkpm_sram_printch('6');
	val = pmu_readl(RK312X_PMU_IDLE_REQ);
	val |= 0x40;
	pmu_writel(val, RK312X_PMU_IDLE_REQ);
	dsb();
	while (((pmu_readl(RK312X_PMU_IDLE_ST) & 0x00400040) != 0x00400040))
		;
}
static void __init rk312x_suspend_init(void)
{
	struct device_node *parent;
	u32 pm_ctrbits;

	pr_info("%s\n", __func__);
	parent = of_find_node_by_name(NULL, "rockchip_suspend");

	if (IS_ERR_OR_NULL(parent)) {
		PM_ERR("%s dev node err\n",  __func__);
		return;
	}

	if (of_property_read_u32_array(parent
		, "rockchip,ctrbits", &pm_ctrbits, 1)) {
		PM_ERR("%s:get pm ctr error\n", __func__);
	return;
	}
	pmic_sleep_gpio_get_dts_info(parent);
	rkpm_set_ctrbits(pm_ctrbits);
	clks_gating_suspend_init();
	rkpm_set_ops_prepare_finish(rkpm_prepare, rkpm_finish);
	rkpm_set_ops_plls(pm_plls_suspend, pm_plls_resume);
	rkpm_set_ops_save_setting(rkpm_save_setting
		, rkpm_save_setting_resume);
	rkpm_set_ops_regs_sleep(rkpm_slp_setting
		, rkpm_save_setting_resume_first);
	rkpm_set_ops_regs_pread(reg_pread);
	rkpm_set_sram_ops_ddr(fn_to_pie(rockchip_pie_chunk
		, &FUNC(ddr_suspend))
		, fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_resume)));
	rkpm_set_sram_ops_bus(fn_to_pie(rockchip_pie_chunk
		, &FUNC(msch_bus_idle_request)));
	rkpm_set_sram_ops_volt(fn_to_pie(rockchip_pie_chunk
		, &FUNC(pwm_regulator_suspend))
		, fn_to_pie(rockchip_pie_chunk, &FUNC(pwm_regulator_resume)));
	rkpm_set_sram_ops_printch(fn_to_pie(rockchip_pie_chunk
		, &FUNC(sram_printch)));
	rkpm_set_ops_printch(ddr_printch);
}
