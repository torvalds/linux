#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/serial_reg.h>
#include <linux/circ_buf.h>
#include <linux/gfp.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <mach/rk2818_iomap.h>
#include <linux/poll.h>
#include <mach/spi_fpga.h>
#include "spi_fpga_fw.h"

#define FPGA_GPIO_TYPE    0

#define FPGA_PIN_CRESET_B   RK2818_PIN_PC4
#define SPI_PIN_TX  RK2818_PIN_PB6
#define SPI_PIN_CLK RK2818_PIN_PB5
#define SPI_PIN_CS  RK2818_PIN_PB0

#if (FPGA_GPIO_TYPE == 0)
#define FPGA_CRESER_OUTPUT()    __raw_writel(__raw_readl(RK2818_GPIO0_BASE+GPIO_SWPORTC_DDR)|(0x1<<4), (RK2818_GPIO0_BASE+GPIO_SWPORTC_DDR))
#define FPGA_CRESET_HIGH()  __raw_writel(__raw_readl(RK2818_GPIO0_BASE+GPIO_SWPORTC_DR)|(0x1<<4), (RK2818_GPIO0_BASE+GPIO_SWPORTC_DR))
#define FPGA_CRESET_LOW()  __raw_writel(__raw_readl(RK2818_GPIO0_BASE+GPIO_SWPORTC_DR)&(~(0x1<<4)), (RK2818_GPIO0_BASE+GPIO_SWPORTC_DR))

#define FPGA_TX_OUTPUT()    __raw_writel(__raw_readl(RK2818_GPIO0_BASE+GPIO_SWPORTB_DDR)|(0x1<<6), (RK2818_GPIO0_BASE+GPIO_SWPORTB_DDR))
#define FPGA_TX_HIGH()  __raw_writel(__raw_readl(RK2818_GPIO0_BASE+GPIO_SWPORTB_DR)|(0x1<<6), (RK2818_GPIO0_BASE+GPIO_SWPORTB_DR))
#define FPGA_TX_LOW()  __raw_writel(__raw_readl(RK2818_GPIO0_BASE+GPIO_SWPORTB_DR)&(~(0x1<<6)), (RK2818_GPIO0_BASE+GPIO_SWPORTB_DR))

#define FPGA_CLK_OUTPUT()  __raw_writel(__raw_readl(RK2818_GPIO0_BASE+GPIO_SWPORTB_DDR)|(0x1<<5), (RK2818_GPIO0_BASE+GPIO_SWPORTB_DDR))
#define FPGA_CLK_HIGH()  __raw_writel(__raw_readl(RK2818_GPIO0_BASE+GPIO_SWPORTB_DR)|(0x1<<5), (RK2818_GPIO0_BASE+GPIO_SWPORTB_DR))
#define FPGA_CLK_LOW()  __raw_writel(__raw_readl(RK2818_GPIO0_BASE+GPIO_SWPORTB_DR)&(~(0x1<<5)), (RK2818_GPIO0_BASE+GPIO_SWPORTB_DR))

#define FPGA_CS_OUTPUT()  __raw_writel(__raw_readl(RK2818_GPIO0_BASE+GPIO_SWPORTB_DDR)|(0x1<<0), (RK2818_GPIO0_BASE+GPIO_SWPORTB_DDR))
#define FPGA_CS_HIGH()  __raw_writel(__raw_readl(RK2818_GPIO0_BASE+GPIO_SWPORTB_DR)|(0x1<<0), (RK2818_GPIO0_BASE+GPIO_SWPORTB_DR))
#define FPGA_CS_LOW()  __raw_writel(__raw_readl(RK2818_GPIO0_BASE+GPIO_SWPORTB_DR)&(~(0x1<<0)), (RK2818_GPIO0_BASE+GPIO_SWPORTB_DR))
#else
#define FPGA_CRESER_OUTPUT() gpio_direction_output(FPGA_PIN_CRESET_B,GPIO_HIGH)
#define FPGA_CRESET_HIGH()   gpio_set_value(FPGA_PIN_CRESET_B,GPIO_HIGH)
#define FPGA_CRESET_LOW()   gpio_set_value(FPGA_PIN_CRESET_B,GPIO_LOW)

#define FPGA_TX_OUTPUT() gpio_direction_output(SPI_PIN_TX,GPIO_HIGH)
#define FPGA_TX_HIGH()   gpio_set_value(SPI_PIN_TX,GPIO_HIGH)
#define FPGA_TX_LOW()   gpio_set_value(SPI_PIN_TX,GPIO_LOW)

#define FPGA_CLK_OUTPUT() gpio_direction_output(SPI_PIN_CLK,GPIO_HIGH)
#define FPGA_CLK_HIGH()   gpio_set_value(SPI_PIN_CLK,GPIO_HIGH)
#define FPGA_CLK_LOW()   gpio_set_value(SPI_PIN_CLK,GPIO_LOW)

#define FPGA_CS_OUTPUT() gpio_direction_output(SPI_PIN_CS,GPIO_HIGH)
#define FPGA_CS_HIGH()   gpio_set_value(SPI_PIN_CS,GPIO_HIGH)
#define FPGA_CS_LOW()   gpio_set_value(SPI_PIN_CS,GPIO_LOW)
#endif

static void __init spi_fpga_send_bytes(unsigned char * bytes, unsigned int len)
{
    unsigned int i,j;			
    unsigned char   spibit;
    unsigned char * fw = bytes;
    
    i=0;
    while (i < len) 
    {
        j=0;
        spibit = *fw++;
        while (j < 8) 
        {                    
            FPGA_CLK_LOW();
            if(spibit & 0x80) 
            {
                FPGA_TX_HIGH();
            }
            else 
            {
                FPGA_TX_LOW();
            }
            FPGA_CLK_HIGH();
            spibit = spibit<<1;
            j++;
        }
        i++;
    }
}

static void __init spi_fpga_wait(unsigned int num)
{
    unsigned int i = 0;

    while(i<(num<<1))
    {
        FPGA_CLK_LOW();
        i++;
        FPGA_CLK_HIGH();
		i++;
    }
}
static spinlock_t		lock_fw;
static void __init spi_fpga_dlfw(unsigned char * fpga_fw, unsigned int fpga_fw_len)
{
    int ret;
    unsigned long flags;
    unsigned char command1[6] = {0x7e, 0xaa, 0x99, 0x7e, 0x01, 0x0e};
    unsigned char command2[5] = {0x83, 0x00, 0x00, 0x26, 0x11};
    unsigned char command3[5] = {0x83, 0x00, 0x00, 0x27, 0x21};
    unsigned char command4[1] = {0x81};

    //local_irq_save(flags);
	spin_lock_irqsave(&lock_fw, flags);
    rk2818_mux_api_set(GPIOC_LCDC24BIT_SEL_NAME, IOMUXB_GPIO0_C2_7);
    //ret = gpio_request(FPGA_PIN_CRESET_B, NULL);
	//if (ret) {
	//	printk("%s:failed to request fpga download pin\n",__FUNCTION__);
	//}
    
	rk2818_mux_api_set(GPIOB_SPI0_MMC0_NAME,IOMUXA_GPIO0_B567);	
	//ret = gpio_request(SPI_PIN_CLK, NULL);
	//if (ret) {
	//	printk("%s:failed to request fpga clk pin\n",__FUNCTION__);
	//}
	
	//ret = gpio_request(SPI_PIN_TX, NULL);
	//if (ret) {
	//	printk("%s:failed to request fpga tx pin\n",__FUNCTION__);
	//}

	rk2818_mux_api_set(GPIOB0_SPI0CSN1_MMC1PCA_NAME, IOMUXA_GPIO0_B0);
	//ret = gpio_request(SPI_PIN_CS, NULL);
	//if (ret) {
	//	printk("%s:failed to request fpga cs pin\n",__FUNCTION__);
	//}

	FPGA_CS_LOW();
	FPGA_CS_OUTPUT();
	FPGA_CRESET_LOW();
	FPGA_CRESER_OUTPUT();
	FPGA_TX_LOW();
	FPGA_TX_OUTPUT();
	FPGA_CLK_LOW();
	FPGA_CLK_OUTPUT();
	
    //step 1
    FPGA_CS_LOW();
    FPGA_CRESET_LOW();
    udelay(10);//delay >= 200ns
    
    //step 2
    FPGA_CRESET_HIGH();
    udelay(500); //delay >= 300us for clear internal memory 

    //step 3
    //spi_fpga_wait(8); //need ???
    
    //step 4
    //FPGA_CS_HIGH();
    spi_fpga_wait(8);

    //step 5
    FPGA_CS_LOW();
    spi_fpga_send_bytes(command1, 6);


    //step 6
    //FPGA_CS_HIGH();
    spi_fpga_wait(13000);

    //step 7
    FPGA_CS_LOW();
    spi_fpga_send_bytes(command2, 5);


    //step 8
    //FPGA_CS_HIGH();
    spi_fpga_wait(8);

    //step 9
    FPGA_CS_LOW();
    spi_fpga_send_bytes(command3, 5);


    //step 10
    //FPGA_CS_HIGH();
    spi_fpga_wait(8);

    //step 11
    FPGA_CS_LOW();
    spi_fpga_send_bytes(command4, 1);


    //step 12
    //FPGA_CS_HIGH();
    spi_fpga_wait(8);

    //step 13
    //FPGA_CS_HIGH();
    spi_fpga_send_bytes(fpga_fw, fpga_fw_len);

    //step 14
    spi_fpga_wait(100);

    //step 15
    //local_irq_restore(flags);
    spin_unlock_irqrestore(&lock_fw, flags);
	
    //free gpio and set to spi
    //gpio_free(FPGA_PIN_CRESET_B);
    //gpio_free(SPI_PIN_TX);
    //gpio_free(SPI_PIN_CLK);
    //gpio_free(SPI_PIN_CS);
    rk2818_mux_api_mode_resume(GPIOB_SPI0_MMC0_NAME);	
    rk2818_mux_api_mode_resume(GPIOB0_SPI0CSN1_MMC1PCA_NAME);
    rk2818_mux_api_mode_resume(GPIOC_LCDC24BIT_SEL_NAME);
}

int __init fpga_dl_fw(void)
{
    printk("%s:start to load FPGA HEX.........\n",__FUNCTION__);

	rk2818_mux_api_set(GPIOE0_VIPDATA0_SEL_NAME,0);
	gpio_request(RK2818_PIN_PE0, NULL);
	gpio_direction_output(RK2818_PIN_PE0,1);
	udelay(2);
	gpio_direction_output(RK2818_PIN_PE0,0);

    spi_fpga_dlfw(spibyte, CONFIGURATION_SIZE);

	gpio_direction_output(RK2818_PIN_PE0,1);
	udelay(2);
	gpio_direction_output(RK2818_PIN_PE0,0);
	
}

