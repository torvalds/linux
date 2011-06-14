#include <mach/rk29_iomap.h>
#include <mach/board.h>
#include <mach/sram.h>
#include <asm/io.h>
#include <mach/iomux.h>

#define GRF_GPIO0_DIR     0x000
#define GRF_GPIO1_DIR     0x004
#define GRF_GPIO2_DIR     0x008
#define GRF_GPIO3_DIR     0x00c
#define GRF_GPIO4_DIR     0x010
#define GRF_GPIO5_DIR     0x014


#define GRF_GPIO0_DO      0x018
#define GRF_GPIO1_DO      0x01c
#define GRF_GPIO2_DO      0x020
#define GRF_GPIO3_DO      0x024
#define GRF_GPIO4_DO      0x028
#define GRF_GPIO5_DO      0x02c

#define GRF_GPIO0_EN      0x030
#define GRF_GPIO1_EN      0x034
#define GRF_GPIO2_EN      0x038
#define GRF_GPIO3_EN      0x03c
#define GRF_GPIO4_EN      0x040
#define GRF_GPIO5_EN      0x044


#define GRF_GPIO0L_IOMUX  0x048
#define GRF_GPIO0H_IOMUX  0x04c
#define GRF_GPIO1L_IOMUX  0x050
#define GRF_GPIO1H_IOMUX  0x054
#define GRF_GPIO2L_IOMUX  0x058
#define GRF_GPIO2H_IOMUX  0x05c
#define GRF_GPIO3L_IOMUX  0x060
#define GRF_GPIO3H_IOMUX  0x064
#define GRF_GPIO4L_IOMUX  0x068
#define GRF_GPIO4H_IOMUX  0x06c
#define GRF_GPIO5L_IOMUX  0x070
#define GRF_GPIO5H_IOMUX  0x074

#define grf_readl(offset) readl(RK29_GRF_BASE + offset)
#define grf_writel(v, offset) do { writel(v, RK29_GRF_BASE + offset); readl(RK29_GRF_BASE + offset); } while (0)

#if CONFIG_MACH_RK29_A22

typedef struct GPIO_IOMUX
{
    unsigned int GPIOL_IOMUX;
    unsigned int GPIOH_IOMUX;
}GPIO_IOMUX_PM;

//GRF Registers
typedef  struct REG_FILE_GRF
{
   unsigned int GRF_GPIO_DIR[6]; 
   unsigned int GRF_GPIO_DO[6];
   unsigned int GRF_GPIO_EN[6];
   GPIO_IOMUX_PM GRF_GPIO_IOMUX[6];
   unsigned int GRF_GPIO_PULL[7];
} GRF_REG_SAVE;

GRF_REG_SAVE  __sramdata pm_grf;

void __sramfunc pm_spi_gpio_prepare(void)
{
	
	pm_grf.GRF_GPIO_IOMUX[1].GPIOL_IOMUX = grf_readl(GRF_GPIO1L_IOMUX);
	pm_grf.GRF_GPIO_IOMUX[2].GPIOH_IOMUX = grf_readl(GRF_GPIO2H_IOMUX);


	pm_grf.GRF_GPIO_PULL[1] = grf_readl(GRF_GPIO1_PULL);
	pm_grf.GRF_GPIO_PULL[2] = grf_readl(GRF_GPIO2_PULL);

	pm_grf.GRF_GPIO_EN[1] = grf_readl(GRF_GPIO1_EN);
	pm_grf.GRF_GPIO_EN[2] = grf_readl(GRF_GPIO2_EN);

}


void __sramfunc pm_spi_gpio_suspend(void)
{
	int io1L_iomux;
	int io2H_iomux;
	int io1_pull,io2_pull;
	int io1_en,io2_en;
	pm_spi_gpio_prepare();

	io1L_iomux = grf_readl(GRF_GPIO1L_IOMUX);
	io2H_iomux = grf_readl(GRF_GPIO2H_IOMUX);

	io1L_iomux &= (~((0x03<<6)|(0x03 <<8)));
	io2H_iomux &=  0xffff0000;
	grf_writel(io1L_iomux&(~((0x03<<6)|(0x03 <<8))), GRF_GPIO1L_IOMUX);
	grf_writel(io2H_iomux&0xffff0000, GRF_GPIO2H_IOMUX);

	io1_pull = grf_readl(GRF_GPIO1_PULL);
	io2_pull = grf_readl(GRF_GPIO2_PULL);
	
	grf_writel(io1_pull|0x18,GRF_GPIO1_PULL);
	grf_writel(io2_pull|0x00ff0000,GRF_GPIO2_PULL);

	io1_en = grf_readl(GRF_GPIO1_EN);
	io2_en = grf_readl(GRF_GPIO2_EN);
	
	grf_writel(io1_en|0x18,GRF_GPIO1_EN);
	grf_writel(io2_en|0x00ff0000,GRF_GPIO2_EN);





}

void __sramfunc pm_spi_gpio_resume(void)
{

	grf_writel(pm_grf.GRF_GPIO_EN[1],GRF_GPIO1_EN);
	grf_writel(pm_grf.GRF_GPIO_EN[2],GRF_GPIO2_EN);
	grf_writel(pm_grf.GRF_GPIO_PULL[1],GRF_GPIO1_PULL);
	grf_writel(pm_grf.GRF_GPIO_PULL[2],GRF_GPIO2_PULL);

	grf_writel(pm_grf.GRF_GPIO_IOMUX[1].GPIOL_IOMUX, GRF_GPIO1L_IOMUX);
	grf_writel(pm_grf.GRF_GPIO_IOMUX[2].GPIOH_IOMUX, GRF_GPIO2H_IOMUX);




}

#else
void __sramfunc pm_spi_gpio_prepare(void)
{

}

void __sramfunc pm_spi_gpio_suspend(void)
{

}
void __sramfunc pm_spi_gpio_resume(void)
{

}
#endif
