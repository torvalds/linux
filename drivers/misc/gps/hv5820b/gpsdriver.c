///////////////////////////////////////////////////////////////////////////////////
//
// Filename: gpsdriver.c
// Author:	sjchen
// Copyright: 
// Date: 2012/07/09
// Description:
//			GPS driver
//
// Revision:
//		0.0.1
//
///////////////////////////////////////////////////////////////////////////////////
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/printk.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <linux/clk.h>
#include <asm/io.h>

//#include <mach/clock.h>


#include "gpsdrv.h"
#include "gpsdriver.h"
#include "lnxdrv.h"
#include "PALAPI.h"

#include <linux/platform_device.h>
#include <mach/gpio.h>
#include "hv5820b_gps.h"

///////////////////////////////////////////////////////////////////////////////////
// 
// macro declaration
//
///////////////////////////////////////////////////////////////////////////////////
#define GPS_MAJOR								( 61 )
#define SW_VA_GPS_IO_BASE				0x10400000
#define GPS_USE_SPI						


///////////////////////////////////////////////////////////////////////////////////
// 
// static variables declaration
//
///////////////////////////////////////////////////////////////////////////////////
static char   gpsstr[]="gps";
static struct miscdevice  gps_miscdev;
struct hv5820b_gps_data *hv5820b_pdata ;
void *gps_mem =NULL;
unsigned long gps_mem_address; //BSP define for u32MemoryPhyAddr
unsigned long gps_mem_size = 8*0x00100000;    //it must be more than 8MB

///////////////////////////////////////////////////////////////////////////////////
// 
// extern variables declaration
//
///////////////////////////////////////////////////////////////////////////////////
extern unsigned long gps_mem_address; //BSP define for u32MemoryPhyAddr
extern unsigned long gps_mem_size;    //it must be more than 8MB


#ifdef GPS_USE_SPI
#include <linux/adc.h>
void VtuneAndSpiCheck_rk(int enable_check);
int AdcValueGet(void);
int GetVtuneAdcValue_rk(void);
void ConfigRfSpi_rk(int Val );
struct adc_client *adc_gps_cleint;
#define LOW_SCALE_0_65V 600
#define HIGH_SCALE_0_95V 1000
void gps_callback(struct adc_client *client, void *callback_param, int result)
{
        return;
}
void VtuneAndSpiCheck_rk(int enable_check)
{
	int VtuneAvg;
	int SpiValue = 2;
	int cnt = 0;

	if( enable_check == 0)
		return;

	VtuneAvg = 0;	
	while( (VtuneAvg < LOW_SCALE_0_65V) || (VtuneAvg > HIGH_SCALE_0_95V) && cnt < 10)
	{	
		if( (SpiValue >= 8) || (SpiValue <= 0) )
			break;	

		VtuneAvg = AdcValueGet();
		
		if( VtuneAvg < LOW_SCALE_0_65V)
		{
			SpiValue++;
			ConfigRfSpi_rk( SpiValue);
		}

		if( VtuneAvg > HIGH_SCALE_0_95V)
		{
			SpiValue--;
			ConfigRfSpi_rk( SpiValue);
		}
		cnt ++;
	}
}

 
int AdcValueGet()
{
	// Read adc value
	int adc_value;
	int sampletime = 100;
	int AveValue;
	int SumValue;
	int i;	

	
	SumValue = 0;
	for(i = 0; i < sampletime; i++)
	{
		adc_value = GetVtuneAdcValue_rk();
		
		SumValue += adc_value;
	}
	AveValue = SumValue / sampletime;
	return AveValue;
}

int GetVtuneAdcValue_rk()
{
	int adc_val = 0;
	
	adc_val = adc_sync_read(adc_gps_cleint);
	if(adc_val < 0)
        	printk("GetVtuneAdcValue error");
	
	return adc_val;
}

void ConfigRfSpi_rk(int Val )
{
	int i;

#define GPS_SCLK  hv5820b_pdata->GpsSpiMOSI
#define GPS_MOSI  hv5820b_pdata->GpsSpiClk
#define GPS_SCS   hv5820b_pdata->GpsSpi_CSO


	PAL_Clr_GPIO_Pin(GPS_SCLK);
	PAL_Clr_GPIO_Pin(GPS_SCS);

	for(i = 0; i < 100000000; i++);

	PAL_Set_GPIO_Pin(GPS_SCLK);
	PAL_Set_GPIO_Pin(GPS_MOSI);  //b15


	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b14
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b13
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b12
	PAL_Clr_GPIO_Pin(GPS_MOSI);
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b11
	PAL_Set_GPIO_Pin(GPS_MOSI);
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b10
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b9
	PAL_Clr_GPIO_Pin(GPS_MOSI);
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b8
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b7
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b6
	if ((Val >> 4) & 1)
	    PAL_Set_GPIO_Pin(GPS_MOSI);
	else
	    PAL_Clr_GPIO_Pin(GPS_MOSI);
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b5
	if ((Val >> 3) & 1)
	    PAL_Set_GPIO_Pin(GPS_MOSI);
	else
	    PAL_Clr_GPIO_Pin(GPS_MOSI);	
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b4
        if ((Val >> 2) & 1)
	    PAL_Set_GPIO_Pin(GPS_MOSI);
	else
	    PAL_Clr_GPIO_Pin(GPS_MOSI);	
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b3
	if ((Val >> 1) & 1)
	    PAL_Set_GPIO_Pin(GPS_MOSI);
	else
	    PAL_Clr_GPIO_Pin(GPS_MOSI);
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b2
	if ((Val >> 0) & 1)
	    PAL_Set_GPIO_Pin(GPS_MOSI);
	else
	    PAL_Clr_GPIO_Pin(GPS_MOSI);
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b1
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCLK);  //b0
	for(i = 0; i < 100000000; i++);
	PAL_Clr_GPIO_Pin(GPS_SCLK);
	for(i = 0; i < 100000000; i++);
	for(i = 0; i < 100000000; i++);
	PAL_Set_GPIO_Pin(GPS_SCS);
}
#endif
static int hv5820b_gps_probe(struct platform_device *pdev)
{
	int err;
	struct hv5820b_gps_data *pdata = pdev->dev.platform_data;
	if(!pdata)
		return -1;
	hv5820b_pdata = pdata;
		
	printk("Enter GPSDrv_Init\n");

	err = request_irq ( GPS_BB_INT_MASK, gps_int_handler, IRQF_DISABLED, gpsstr, NULL );

	if ( err )
	{
		printk ( "gps_mod_init: gps request irq failed!\n" );

		return err;
	}

	err = misc_register( &gps_miscdev);
	if (err < 0)
	{
		return err;
	}

	gps_mem=kzalloc(gps_mem_size, GFP_KERNEL);
	gps_mem_address = (unsigned long)(&gps_mem);
	//TODO: 
	//Set the GPIO (GPS_VCC_EN) to low level in here
	//
	if(pdata->io_init)
		pdata->io_init();
	gpio_direction_output(pdata->GpsVCCEn, GPIO_LOW);
	#ifdef GPS_USE_SPI
	adc_gps_cleint = adc_register(2, gps_callback, NULL);
	#endif
	return 0;	
}
static int hv5820b_gps_remove(struct platform_device *pdev)
{
#ifdef GPS_USE_SPI
	adc_unregister(adc_gps_cleint);
#endif
	return 0;
}
static int hv5820b_gps_suspend(struct platform_device *pdev,  pm_message_t state)
{
	return 0;	
}
static int hv5820b_gps_resume(struct platform_device *pdev)
{
	return 0;
}
static struct platform_driver hv5820b_gps_driver = {
	.probe	= hv5820b_gps_probe,
	.remove = hv5820b_gps_remove,
	.suspend  	= hv5820b_gps_suspend,
	.resume		= hv5820b_gps_resume,
	.driver	= {
		.name	= "gps_hv5820b",
		.owner	= THIS_MODULE,
	},
};

///////////////////////////////////////////////////////////////////////////////////
// 
// Function Name: gps_mod_init
// Parameters: 
// Description:
// Notes: sjchen 2010/11/04
//
///////////////////////////////////////////////////////////////////////////////////
static int __init gps_mod_init(void)
{
	return platform_driver_register(&hv5820b_gps_driver);
}



///////////////////////////////////////////////////////////////////////////////////
// 
// Function Name: gps_mod_exit
// Parameters:
// Description:
// Notes: sjchen 2010/11/04
//
///////////////////////////////////////////////////////////////////////////////////
static void __exit gps_mod_exit ( void )
{
	//Disable baseband interrupt
	WriteGpsRegisterUlong ( BB_INT_ENA_OFFSET, 0 );

	free_irq( GPS_BB_INT_MASK, NULL );

	//unregister_chrdev ( GPS_MAJOR, gpsstr );
	misc_deregister(&gps_miscdev);
	platform_driver_unregister(&hv5820b_gps_driver);
	printk ( "GPS exit ok!\n");
}

///////////////////////////////////////////////////////////////////////////////////
// 
// Function Name:gps_ioctl
// Parameters:
// Description:
// Notes: sjchen 2010/11/04
//
///////////////////////////////////////////////////////////////////////////////////
long gps_ioctl(struct file *file, unsigned int cmd,
	      unsigned long arg) 
{
	int ret = 0;
	BB_DRV_VERSION __user    * pVersion;
	GPS_DRV_INIT   GpsInitStruct;

	switch(cmd) 
	{

	case IOCTL_BB_GPS_START:
		//TODO:
		// Gps baseband module initialize in here. 
		// Module power up
		if(hv5820b_pdata->enable_hclk_gps)
			hv5820b_pdata->enable_hclk_gps();
		
		memset(&GpsInitStruct,0,sizeof(GpsInitStruct));
		GpsInitStruct.u32GpsRegBase    = SW_VA_GPS_IO_BASE;                // GPS Reg Base address 
		GpsInitStruct.u32MemoryPhyAddr = gps_mem_address;                  // sample code
		GpsInitStruct.u32MemoryVirAddr = __phys_to_virt(gps_mem_address);
		GpsInitStruct.u32GpsSign = hv5820b_pdata->GpsSign;       //GPIO index
		GpsInitStruct.u32GpsMag = hv5820b_pdata->GpsMag;        //GPIO index
		GpsInitStruct.u32GpsClk = hv5820b_pdata->GpsClk;        //GPIO index
		GpsInitStruct.u32GpsVCCEn = hv5820b_pdata->GpsVCCEn;      //GPIO index
		GpsInitStruct.u32GpsSpi_CSO = hv5820b_pdata->GpsSpi_CSO;    //GPIO index
		GpsInitStruct.u32GpsSpiClk = hv5820b_pdata->GpsSpiClk;     //GPIO index
		GpsInitStruct.u32GpsSpiMOSI = hv5820b_pdata->GpsSpiMOSI;	  //GPIO index
		//TODO:
		//Add other member of struct GpsInitStruct
		Gps_Init( arg,&GpsInitStruct);
		#ifdef GPS_USE_SPI
		VtuneAndSpiCheck_rk(1);
		#endif
		break;

	case IOCTL_BB_UPDATEDATA:
		Gps_UpdateData(arg);
		break;


	case IOCTL_BB_GPS_STOP:
		Gps_Stop();
		
		//TODO:
		// Set the GPIO(GPS_VCC_EN) to low level
		// Close the module clk.
		gpio_direction_output(hv5820b_pdata->GpsVCCEn, GPIO_LOW);
		if(hv5820b_pdata->disable_hclk_gps)
			hv5820b_pdata->disable_hclk_gps();
		break;

	case IOCTL_BB_GET_VERSION:
		pVersion	= ( void __user * ) arg;

		pVersion->u32Major = DRV_MAJOR_VERSION;
		pVersion->u32Minor = DRV_MINOR_VERSION; 
		PAL_Sprintf(pVersion->strCompileTime,"%s,%s",__DATE__,__TIME__);

		break;

	default:

		printk ( "gpsdrv: ioctl number is worng %d\n",cmd);

		break;
	};

	return ret;
}


///////////////////////////////////////////////////////////////////////////////////
// 
// Function Name:gps_read
// Parameters:
// Description: Read gps data
// Notes: sjchen 2010/11/04
//
///////////////////////////////////////////////////////////////////////////////////
ssize_t gps_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	ssize_t  nsize;
	if((nsize = GpsDrv_Read(buf,size)))
	{
		return nsize;
	}

	return -EFAULT;
}

///////////////////////////////////////////////////////////////////////////////////
// 
// Function Name:gps_open
// Parameters:
// Description:
// Notes: sjchen 2010/11/04
//
///////////////////////////////////////////////////////////////////////////////////
static int gps_open (struct inode *inode, struct file *filp)
{
	return 0;          /* success */
}



///////////////////////////////////////////////////////////////////////////////////
// 
// Function Name:gps_close 
// Parameters:
// Description:
// Notes: sjchen 2010/11/04
//
///////////////////////////////////////////////////////////////////////////////////
static int gps_close (struct inode *inode, struct file *filp) 
{
 	return 0;
}

///////////////////////////////////////////////////////////////////////////////////
// 
// Function Name: driver struct
// Parameters:
// Description:
// Notes: sjchen 2010/11/04
//
///////////////////////////////////////////////////////////////////////////////////
static const struct file_operations gps_fops = 
{
	. owner   =	THIS_MODULE,
	. open    =	gps_open,
	. release =	gps_close,
	. unlocked_ioctl   =	gps_ioctl,
	. read    = 	gps_read,
};

static struct miscdevice  gps_miscdev = 
{
	.minor   = MISC_DYNAMIC_MINOR,
	.name    = gpsstr,
	.fops    = &gps_fops,
};


///////////////////////////////////////////////////////////////////////////////////
// 
// Module Name:
// Parameters:
// Description:
// Notes: sjchen 2010/11/04
//
///////////////////////////////////////////////////////////////////////////////////
module_init ( gps_mod_init );
module_exit ( gps_mod_exit );


MODULE_LICENSE("GPL");


