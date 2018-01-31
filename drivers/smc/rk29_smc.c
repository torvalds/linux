/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/backlight.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/earlysuspend.h>
#include <linux/cpufreq.h>
#include <linux/wakelock.h>

#include <asm/io.h>
#include <asm/div64.h>
#include <asm/uaccess.h>


#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/rk29_iomap.h>
#include <mach/pmu.h>

void __iomem *rank0_vir_base;  // virtual basic address of lcdc register
struct clk      *smc_clk = NULL;
struct clk      *smc_axi_clk = NULL;
void __iomem *reg_vir_base;  // virtual basic address of lcdc register

int smc0_enable(int enable)
{
    if(enable){
        clk_enable(smc_axi_clk);
        clk_enable(smc_clk);
        __raw_writel(__raw_readl(RK29_GRF_BASE+0xbc) | 0x2000 , (RK29_GRF_BASE+0xbc));

        __raw_writel((0x801), (reg_vir_base+0x18));
        __raw_writel(0x00400000, (reg_vir_base+0x10));
        __raw_writel((15 | (14<<8) | (15<<4) | (5<<11) ), (reg_vir_base+0x14));
        //__raw_writel((15 | (10<<8) | (15<<4) | (7<<11) ), (reg_vir_base+0x14));

        __raw_writel(0x00400000, (reg_vir_base+0x10));
    }   else    {
        clk_disable(smc_axi_clk);
        clk_disable(smc_clk);
    }
    return 0;
}

int smc0_init(u8 **base_addr)
{
    u32 reg_phy_base;       // physical basic address of lcdc register
	u32 len;               // physical map length of lcdc register
    struct resource *mem;

    u32 rank0_phy_base;       // physical basic address of lcdc register
	u32 rank0_len;               // physical map length of lcdc register
    struct resource *rank0_mem;

    printk(" %s %d \n",__FUNCTION__, __LINE__);

    if(smc_axi_clk == NULL)smc_axi_clk = clk_get(NULL, "aclk_smc");
    if(smc_clk == NULL)smc_clk = clk_get(NULL, "smc");

    rank0_phy_base = 0x11000000;  //0x12000000;//
    rank0_len = SZ_4K;
    rank0_mem = request_mem_region(rank0_phy_base, rank0_len, "smc_rank0");
    if (rank0_mem == NULL)
    {
        printk("failed to get rank0 memory region [%d]\n",__LINE__);
    }

    rank0_vir_base = ioremap(rank0_phy_base, rank0_len);
    if (rank0_vir_base == NULL)
    {
        printk("ioremap() of rank0 failed\n");
    }

    //*base_addr = rank0_vir_base;

    reg_phy_base = RK29_SMC_PHYS;
    len = SZ_16K;
    mem = request_mem_region(reg_phy_base, len, "smc reg");
    if (mem == NULL)
    {
        printk("failed to get memory region [%d]\n",__LINE__);
    }

    reg_vir_base = ioremap(reg_phy_base, len);
    if (reg_vir_base == NULL)
    {
        printk("ioremap() of registers failed\n");
    }

    smc0_enable(1);

    rk29_mux_api_set(GPIO0B7_EBCGDOE_SMCOEN_NAME, GPIO0L_SMC_OE_N);
    rk29_mux_api_set(GPIO0B6_EBCSDSHR_SMCBLSN1_HOSTINT_NAME, GPIO0L_SMC_BLS_N_1 );
    rk29_mux_api_set(GPIO0B5_EBCVCOM_SMCBLSN0_NAME, GPIO0L_SMC_BLS_N_0 );
    rk29_mux_api_set(GPIO0B4_EBCBORDER1_SMCWEN_NAME, GPIO0L_SMC_WE_N);

    rk29_mux_api_set(GPIO0B3_EBCBORDER0_SMCADDR3_HOSTDATA3_NAME, GPIO0L_SMC_ADDR3);
    rk29_mux_api_set(GPIO0B2_EBCSDCE2_SMCADDR2_HOSTDATA2_NAME, GPIO0L_SMC_ADDR2);
    rk29_mux_api_set(GPIO0B1_EBCSDCE1_SMCADDR1_HOSTDATA1_NAME, GPIO0L_SMC_ADDR1);
    rk29_mux_api_set(GPIO0B0_EBCSDCE0_SMCADDR0_HOSTDATA0_NAME, GPIO0L_SMC_ADDR0);

    rk29_mux_api_set(GPIO1A1_SMCCSN0_NAME, GPIO1L_SMC_CSN0);
  //  rk29_mux_api_set(GPIO1A1_SMCCSN0_NAME, GPIO1L_GPIO1A1);

  //  if(gpio_request(RK29_PIN1_PA1, NULL) != 0)
    {
  //      gpio_free(RK29_PIN1_PA1);
 //       printk(">>>>>> RK29_PIN1_PA1 gpio_request err \n ");
    }
  //  gpio_direction_output(RK29_PIN1_PA1, GPIO_LOW);

    rk29_mux_api_set(GPIO1A2_SMCCSN1_NAME, GPIO1L_SMC_CSN1);
    rk29_mux_api_set(GPIO0D0_EBCSDOE_SMCADVN_NAME, GPIO0H_SMC_ADV_N);

    rk29_mux_api_set(GPIO5C0_EBCSDDO0_SMCDATA0_NAME, GPIO5H_SMC_DATA0);
    rk29_mux_api_set(GPIO5C1_EBCSDDO1_SMCDATA1_NAME, GPIO5H_SMC_DATA1);
    rk29_mux_api_set(GPIO5C2_EBCSDDO2_SMCDATA2_NAME, GPIO5H_SMC_DATA2);
    rk29_mux_api_set(GPIO5C3_EBCSDDO3_SMCDATA3_NAME, GPIO5H_SMC_DATA3);
    rk29_mux_api_set(GPIO5C4_EBCSDDO4_SMCDATA4_NAME, GPIO5H_SMC_DATA4);
    rk29_mux_api_set(GPIO5C5_EBCSDDO5_SMCDATA5_NAME, GPIO5H_SMC_DATA5);
    rk29_mux_api_set(GPIO5C6_EBCSDDO6_SMCDATA6_NAME, GPIO5H_SMC_DATA6);
    rk29_mux_api_set(GPIO5C7_EBCSDDO7_SMCDATA7_NAME, GPIO5H_SMC_DATA7);

    rk29_mux_api_set(GPIO0C0_EBCGDSP_SMCDATA8_NAME, GPIO0H_SMC_DATA8);
    rk29_mux_api_set(GPIO0C1_EBCGDR1_SMCDATA9_NAME, GPIO0H_SMC_DATA9);
    rk29_mux_api_set(GPIO0C2_EBCSDCE0_SMCDATA10_NAME, GPIO0H_SMC_DATA10);
    rk29_mux_api_set(GPIO0C3_EBCSDCE1_SMCDATA11_NAME, GPIO0H_SMC_DATA11);
    rk29_mux_api_set(GPIO0C4_EBCSDCE2_SMCDATA12_NAME, GPIO0H_SMC_DATA12);
    rk29_mux_api_set(GPIO0C5_EBCSDCE3_SMCDATA13_NAME, GPIO0H_SMC_DATA13);
    rk29_mux_api_set(GPIO0C6_EBCSDCE4_SMCDATA14_NAME, GPIO0H_SMC_DATA14);
    rk29_mux_api_set(GPIO0C7_EBCSDCE5_SMCDATA15_NAME, GPIO0H_SMC_DATA15);

    return 0;

}



int smc0_write(u32 addr, u16 data)
{
  //  __raw_writel(data, rank0_vir_base + addr);
    u16 *p = rank0_vir_base + addr;
	int readdata;
    *p = data;
	udelay(2);
	//readdata = *p;
	//mdelay(5);
	//mdelay(10);
    //printk("%s addr=%x, data = %x, read date = %x\n",__FUNCTION__,addr,data,readdata);
    return 0;
}

int smc0_read(u32 addr)
{
    u16 * p = rank0_vir_base + addr;
	int readdata = *p; 
	//mdelay(5);
	//printk("%s addr=%x, read date = %x\n",__FUNCTION__,addr,readdata);
    return readdata;//__raw_readl(rank0_vir_base + addr);
}

void  smc0_exit(void)
{
     smc0_enable(0);
}



