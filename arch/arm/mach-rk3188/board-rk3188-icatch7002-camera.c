#ifdef CONFIG_VIDEO_RK29
#include <plat/rk_camera.h>
#include "../../../drivers/spi/rk29_spim.h"
#include <linux/spi/spi.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <mach/gpio.h>
#include <mach/irqs.h>
#include <linux/miscdevice.h>
#include <asm/dma.h>
#include <linux/preempt.h>
#include <mach/board.h>
#include <linux/miscdevice.h>

struct spi_device* g_icatch_spi_dev = NULL;


static struct rk29xx_spi_chip spi_icatch = {
	//.poll_mode = 1,
	.enable_dma = 0,
};
//user must define this struct according to hardware config	
static struct spi_board_info board_spi_icatch_devices[] = {	
	{
		.modalias  = "spi_icatch",
		.bus_num = 0,	//0 or 1
		.max_speed_hz  = 24*1000*1000,
		.chip_select   = 0, 
		.mode = SPI_MODE_0,
		.controller_data = &spi_icatch,
	},	
	
};


static int __devinit spi_icatch_probe(struct spi_device *spi)
{	
	struct spi_test_data *spi_test_data;
	int ret = 0;
	
	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	ret = spi_setup(spi);
	if (ret < 0){
		dev_err(spi, "ERR: fail to setup spi\n");
		return -1;
	}	

	g_icatch_spi_dev = spi;

	printk("%s:bus_num=%d,ok\n",__func__,spi->master->bus_num);

	return ret;

}


static struct spi_driver spi_icatch_driver = {
	.driver = {
		.name		= "spi_icatch",
		.bus		= &spi_bus_type,
		.owner		= THIS_MODULE,
	},

	.probe		= spi_icatch_probe,
};

static struct miscdevice spi_test_icatch = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "spi_misc_icatch",
};

static int __init spi_icatch_init(void)
{	
	spi_register_board_info(board_spi_icatch_devices, ARRAY_SIZE(board_spi_icatch_devices));
	
	misc_register(&spi_test_icatch);
	return spi_register_driver(&spi_icatch_driver);
}

static void __exit spi_icatch_exit(void)
{
	
	misc_deregister(&spi_test_icatch);
	return spi_unregister_driver(&spi_icatch_driver);
}

module_init(spi_icatch_init);
module_exit(spi_icatch_exit);

/* Notes:

Simple camera device registration:

       new_camera_device(sensor_name,\       // sensor name, it is equal to CONFIG_SENSOR_X
                          face,\              // sensor face information, it can be back or front
                          pwdn_io,\           // power down gpio configuration, it is equal to CONFIG_SENSOR_POWERDN_PIN_XX
                          flash_attach,\      // sensor is attach flash or not
                          mir,\               // sensor image mirror and flip control information
                          i2c_chl,\           // i2c channel which the sensor attached in hardware, it is equal to CONFIG_SENSOR_IIC_ADAPTER_ID_X
                          cif_chl)  \         // cif channel which the sensor attached in hardware, it is equal to CONFIG_SENSOR_CIF_INDEX_X

Comprehensive camera device registration:

      new_camera_device_ex(sensor_name,\
                             face,\
                             ori,\            // sensor orientation, it is equal to CONFIG_SENSOR_ORIENTATION_X
                             pwr_io,\         // sensor power gpio configuration, it is equal to CONFIG_SENSOR_POWER_PIN_XX
                             pwr_active,\     // sensor power active level, is equal to CONFIG_SENSOR_RESETACTIVE_LEVEL_X
                             rst_io,\         // sensor reset gpio configuration, it is equal to CONFIG_SENSOR_RESET_PIN_XX
                             rst_active,\     // sensor reset active level, is equal to CONFIG_SENSOR_RESETACTIVE_LEVEL_X
                             pwdn_io,\
                             pwdn_active,\    // sensor power down active level, is equal to CONFIG_SENSOR_POWERDNACTIVE_LEVEL_X
                             flash_attach,\
                             res,\            // sensor resolution, this is real resolution or resoltuion after interpolate
                             mir,\
                             i2c_chl,\
                             i2c_spd,\        // i2c speed , 100000 = 100KHz
                             i2c_addr,\       // the i2c slave device address for sensor
                             cif_chl,\
                             mclk)\           // sensor input clock rate, 24 or 48
                          
*/
static struct rkcamera_platform_data new_camera[] = {
  new_camera_device_ex(RK29_CAM_ISP_ICATCH7002_OV5693,
                         back,
                         180,            // sensor orientation, it is equal to CONFIG_SENSOR_ORIENTATION_X
                         INVALID_VALUE,         // sensor power gpio configuration, it is equal to CONFIG_SENSOR_POWER_PIN_XX
                         INVALID_VALUE,     // sensor power active level, is equal to CONFIG_SENSOR_RESETACTIVE_LEVEL_X
                         RK30_PIN0_PC1,         // sensor reset gpio configuration, it is equal to CONFIG_SENSOR_RESET_PIN_XX
                         0x0,     // sensor reset active level, is equal to CONFIG_SENSOR_RESETACTIVE_LEVEL_X
                         RK30_PIN0_PC0,
                         0x1,    // sensor power down active level, is equal to CONFIG_SENSOR_POWERDNACTIVE_LEVEL_X
                         INVALID_VALUE,
                         CONS(RK29_CAM_ISP_ICATCH7002_OV5693,_FULL_RESOLUTION),            // sensor resolution, this is real resolution or resoltuion after interpolate
                         0,
                         3,
                         300000,        // i2c speed , 100000 = 100KHz
                         CONS(RK29_CAM_ISP_ICATCH7002_OV5693,_I2C_ADDR),       // the i2c slave device address for sensor
                         0,
                         24),           // sensor input clock rate, 24 or 48
	new_camera_device_ex(RK29_CAM_ISP_ICATCH7002_MI1040, //RK29_CAM_ISP_ICATCH7002_MI1040,
                         front,
                         360,            // sensor orientation, it is equal to CONFIG_SENSOR_ORIENTATION_X
                         INVALID_VALUE,         // sensor power gpio configuration, it is equal to CONFIG_SENSOR_POWER_PIN_XX
                         INVALID_VALUE,     // sensor power active level, is equal to CONFIG_SENSOR_RESETACTIVE_LEVEL_X
                         RK30_PIN0_PC1,         // sensor reset gpio configuration, it is equal to CONFIG_SENSOR_RESET_PIN_XX
                         0x0,     // sensor reset active level, is equal to CONFIG_SENSOR_RESETACTIVE_LEVEL_X
                         RK30_PIN0_PC0,
                         0x1,    // sensor power down active level, is equal to CONFIG_SENSOR_POWERDNACTIVE_LEVEL_X
                         INVALID_VALUE,
                         CONS(RK29_CAM_ISP_ICATCH7002_MI1040,_FULL_RESOLUTION),            // sensor resolution, this is real resolution or resoltuion after interpolate
                         0,
                         3,
                         300000,        // i2c speed , 100000 = 100KHz
                         CONS(RK29_CAM_ISP_ICATCH7002_MI1040,_I2C_ADDR),       // the i2c slave device address for sensor
                         0,
                         24),           // sensor input clock rate, 24 or 48
	new_camera_device_end  
};
#endif  //#ifdef CONFIG_VIDEO_RK29
/*---------------- Camera Sensor Configuration Macro End------------------------*/
#include "../../../drivers/media/video/rk30_camera.c"
/*---------------- Camera Sensor Macro Define End  ---------*/

#define PMEM_CAM_SIZE PMEM_CAM_NECESSARY
/*****************************************************************************************
 * camera  devices
 * author: ddl@rock-chips.com
 *****************************************************************************************/
#ifdef CONFIG_VIDEO_RK29
#define CONFIG_SENSOR_POWER_IOCTL_USR	   1 //define this refer to your board layout
#define CONFIG_SENSOR_RESET_IOCTL_USR	   1
#define CONFIG_SENSOR_POWERDOWN_IOCTL_USR	   0
#define CONFIG_SENSOR_FLASH_IOCTL_USR	   0

static void rk_cif_power(struct rk29camera_gpio_res *res,int on)
{
	struct regulator *ldo_18,*ldo_28;
    int camera_reset = res->gpio_reset;
    int camera_pwrdn = res->gpio_powerdown;

	  
	ldo_28 = regulator_get(NULL, "ricoh_ldo8");	// vcc28_cif
	ldo_18 = regulator_get(NULL, "ricoh_ldo5");	// vcc18_cif
	if (ldo_28 == NULL || IS_ERR(ldo_28) || ldo_18 == NULL || IS_ERR(ldo_18)){
		printk("get cif ldo failed!\n");
		return;
	}
	if(on == 0){
		while(regulator_is_enabled(ldo_28)>0)	
			regulator_disable(ldo_28);
		regulator_put(ldo_28);

		while(regulator_is_enabled(ldo_18)>0)
			regulator_disable(ldo_18);

		iomux_set(GPIO1_A4);
		iomux_set(GPIO1_A5) ;
		iomux_set(GPIO1_A6);
		iomux_set(GPIO1_A7);
		iomux_set(GPIO3_B6);
		iomux_set(GPIO3_B7);
		iomux_set(GPIO0_C0);
		gpio_set_value(RK30_PIN1_PA4,0);
		gpio_set_value(RK30_PIN1_PA5,0);
		gpio_set_value(RK30_PIN1_PA6,0);	// for clk 24M
		gpio_set_value(RK30_PIN1_PA7,0);
		gpio_set_value(RK30_PIN3_PB6,0);
		gpio_set_value(RK30_PIN3_PB7,0);
		gpio_set_value(RK30_PIN0_PC0,0);

		printk("%s off ldo5 vcc18_cif=%dmV end\n", __func__, regulator_get_voltage(ldo_18));
		regulator_put(ldo_18);
		
	}
	else{
        // reset must be low
        if (camera_reset != INVALID_GPIO) 
        	gpio_set_value(camera_reset, 0);
    	printk("%s ResetPin=%d ..PinLevel = %x\n",res->dev_name,camera_reset,0);
        //pwdn musb be low
        if (camera_pwrdn != INVALID_GPIO) 
        	gpio_set_value(camera_pwrdn, 0);
    	printk("%s PwrdnPin=%d ..PinLevel = %x\n",res->dev_name,camera_pwrdn,0);
    	
		gpio_set_value(RK30_PIN1_PA6,1);	//Vincent_Liu@asus.com for clk 24M


		regulator_set_voltage(ldo_18, 1800000, 1800000);
		regulator_enable(ldo_18);
		printk("%s set ldo5 vcc18_cif=%dmV end\n", __func__, regulator_get_voltage(ldo_18));
		regulator_put(ldo_18);

		regulator_set_voltage(ldo_28, 2800000, 2800000);
		regulator_enable(ldo_28);
		//printk("%s set ldo7 vcc28_cif=%dmV end\n", __func__, regulator_get_voltage(ldo_28));
		regulator_put(ldo_28);

	}
}

#if CONFIG_SENSOR_RESET_IOCTL_USR
static int sensor_reset_usr_cb (struct rk29camera_gpio_res *res,int on)
{
	//#error "CONFIG_SENSOR_RESET_IOCTL_USR is 1, sensor_reset_usr_cb function must be writed!!";
		
    int camera_reset = res->gpio_reset;
    int camera_ioflag = res->gpio_flag;
    int camera_io_init = res->gpio_init;  
    int ret = 0;
    
    if (camera_reset != INVALID_GPIO) {
		if (camera_io_init & RK29_CAM_RESETACTIVE_MASK) {
			if (on) {
	        	gpio_set_value(camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
	        	printk("%s ResetPin=%d ..PinLevel = %x\n",res->dev_name,camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
				mdelay(6);
			} else {
				gpio_set_value(camera_reset,(((~camera_ioflag)&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
        		printk("%s ResetPin= %d..PinLevel = %x\n",res->dev_name, camera_reset, (((~camera_ioflag)&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
                mdelay(6);
        		iomux_set(SPI0_CLK);
        		iomux_set(SPI0_RXD);
        		iomux_set(SPI0_TXD);
        		iomux_set(SPI0_CS0);
        		iomux_set(I2C3_SDA);
        		iomux_set(I2C3_SCL);
                mdelay(6);
	        }
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			printk("%s ResetPin=%d request failed!", res->dev_name,camera_reset);
		}
    } else {
		ret = RK29_CAM_EIO_INVALID;
    }
    return ret;

}
#endif

#if CONFIG_SENSOR_POWER_IOCTL_USR
static int sensor_power_usr_cb (struct rk29camera_gpio_res *res,int on)
{
	//#error "CONFIG_SENSOR_POWER_IOCTL_USR is 1, sensor_power_usr_cb function must be writed!!";
	rk_cif_power(res,on);
	return 0;
}
#endif

#if CONFIG_SENSOR_FLASH_IOCTL_USR
static int sensor_flash_usr_cb (struct rk29camera_gpio_res *res,int on)
{
	#error "CONFIG_SENSOR_FLASH_IOCTL_USR is 1, sensor_flash_usr_cb function must be writed!!";
}
#endif

static struct rk29camera_platform_ioctl_cb	sensor_ioctl_cb = {
	#if CONFIG_SENSOR_POWER_IOCTL_USR
	.sensor_power_cb = sensor_power_usr_cb,
	#else
	.sensor_power_cb = NULL,
	#endif

	#if CONFIG_SENSOR_RESET_IOCTL_USR
	.sensor_reset_cb = sensor_reset_usr_cb,
	#else
	.sensor_reset_cb = NULL,
	#endif

	#if CONFIG_SENSOR_POWERDOWN_IOCTL_USR
	.sensor_powerdown_cb = sensor_powerdown_usr_cb,
	#else
	.sensor_powerdown_cb = NULL,
	#endif

	#if CONFIG_SENSOR_FLASH_IOCTL_USR
	.sensor_flash_cb = sensor_flash_usr_cb,
	#else
	.sensor_flash_cb = NULL,
	#endif
};
static rk_sensor_user_init_data_s rk_init_data_sensor[RK_CAM_NUM] ;
#include "../../../drivers/media/video/rk30_camera.c"

#endif /* CONFIG_VIDEO_RK29 */
