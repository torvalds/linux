
#include <mach/iomux.h>
#include <media/soc_camera.h>
#include <linux/android_pmem.h>
#include <mach/rk30_camera.h>
#ifndef PMEM_CAM_SIZE
#include "../../../arch/arm/plat-rk/rk_camera.c"
#else
/*****************************************************************************************
 * camera  devices
 * author: ddl@rock-chips.com
 *****************************************************************************************/
#ifdef CONFIG_VIDEO_RK29 

static int rk_sensor_iomux(int pin)
{    
    switch (pin)
    {
        case RK30_PIN0_PA0: 
		{
			 rk30_mux_api_set(GPIO0A0_HDMIHOTPLUGIN_NAME,0);
			break;	
		}
        case RK30_PIN0_PA1: 
		{
			 rk30_mux_api_set(GPIO0A1_HDMII2CSCL_NAME,0);
			break;	
		}
        case RK30_PIN0_PA2:
		{
			 rk30_mux_api_set(GPIO0A2_HDMII2CSDA_NAME,0);
			break;	
		}
        case RK30_PIN0_PA3:
		{
			 rk30_mux_api_set(GPIO0A3_PWM0_NAME,0);
			break;	
		}
        case RK30_PIN0_PA4:
		{
			 rk30_mux_api_set(GPIO0A4_PWM1_NAME,0);
			break;	
		}
        case RK30_PIN0_PA5:
		{
			 rk30_mux_api_set(GPIO0A5_OTGDRVVBUS_NAME,0);
			break;	
		}
        case RK30_PIN0_PA6:
        {
             rk30_mux_api_set(GPIO0A6_HOSTDRVVBUS_NAME,0);
            break;	
        }
        case RK30_PIN0_PA7:
        {
             rk30_mux_api_set(GPIO0A7_I2S8CHSDI_NAME,0);
            break;	
        }
        case RK30_PIN0_PB0:
        {
             rk30_mux_api_set(GPIO0B0_I2S8CHCLK_NAME,0);
            break;	
        }
        case RK30_PIN0_PB1:
        {
             rk30_mux_api_set(GPIO0B1_I2S8CHSCLK_NAME,0);
            break;	
        }
        case RK30_PIN0_PB2:
        {
             rk30_mux_api_set(GPIO0B2_I2S8CHLRCKRX_NAME,0);
            break;	
        }
        case RK30_PIN0_PB3:
        {
             rk30_mux_api_set(GPIO0B3_I2S8CHLRCKTX_NAME,0);
            break;	
        }
        case RK30_PIN0_PB4:
        {
             rk30_mux_api_set(GPIO0B4_I2S8CHSDO0_NAME,0);
            break;	
        }
        case RK30_PIN0_PB5:
        {
             rk30_mux_api_set(GPIO0B5_I2S8CHSDO1_NAME,0);
            break;	
        }
        case RK30_PIN0_PB6:
        {
             rk30_mux_api_set(GPIO0B6_I2S8CHSDO2_NAME,0);
            break;	
        }
        case RK30_PIN0_PB7:
        {
             rk30_mux_api_set(GPIO0B7_I2S8CHSDO3_NAME,0);
            break;	
        }
        case RK30_PIN0_PC0:
        {
             rk30_mux_api_set(GPIO0C0_I2S12CHCLK_NAME,0);
            break;	
        }
        case RK30_PIN0_PC1:
        {
             rk30_mux_api_set(GPIO0C1_I2S12CHSCLK_NAME,0);
            break;	
        }
        case RK30_PIN0_PC2:
        {
             rk30_mux_api_set(GPIO0C2_I2S12CHLRCKRX_NAME,0);
            break;	
        }
        case RK30_PIN0_PC3:
        {
             rk30_mux_api_set(GPIO0C3_I2S12CHLRCKTX_NAME,0);
            break;	
        }
        case RK30_PIN0_PC4:
        {
             rk30_mux_api_set(GPIO0C4_I2S12CHSDI_NAME,0);
            break;	
        }
        case RK30_PIN0_PC5:
        {
             rk30_mux_api_set(GPIO0C5_I2S12CHSDO_NAME,0);
            break;	
        }
        case RK30_PIN0_PC6:
        {
             rk30_mux_api_set(GPIO0C6_TRACECLK_SMCADDR2_NAME,0);
            break;	
        }
        case RK30_PIN0_PC7:
        {
             rk30_mux_api_set(GPIO0C7_TRACECTL_SMCADDR3_NAME,0);
            break;	
        }
        case RK30_PIN0_PD0:
        {
             rk30_mux_api_set(GPIO0D0_I2S22CHCLK_SMCCSN0_NAME,0);
            break;	
        }
        case RK30_PIN0_PD1:
        {
             rk30_mux_api_set(GPIO0D1_I2S22CHSCLK_SMCWEN_NAME,0);
            break;	
        }
        case RK30_PIN0_PD2:
        {
             rk30_mux_api_set(GPIO0D2_I2S22CHLRCKRX_SMCOEN_NAME,0);
            break;	
        }
        case RK30_PIN0_PD3:
        {
             rk30_mux_api_set(GPIO0D3_I2S22CHLRCKTX_SMCADVN_NAME,0);
            break;	
        }
        case RK30_PIN0_PD4:
        {
             rk30_mux_api_set(GPIO0D4_I2S22CHSDI_SMCADDR0_NAME,0);
            break;	
        }
        case RK30_PIN0_PD5:
        {
             rk30_mux_api_set(GPIO0D5_I2S22CHSDO_SMCADDR1_NAME,0);
            break;	
        }
        case RK30_PIN0_PD6:
        {
             rk30_mux_api_set(GPIO0D6_PWM2_NAME,0);
            break;	
        }
        case RK30_PIN0_PD7:
        {
             rk30_mux_api_set(GPIO0D7_PWM3_NAME,0);
            break;	
        }
        case RK30_PIN1_PA0:
        {
             rk30_mux_api_set(GPIO1A0_UART0SIN_NAME,0);
            break;	
        }
        case RK30_PIN1_PA1:
        {
             rk30_mux_api_set(GPIO1A1_UART0SOUT_NAME,0);
            break;	
        }
        case RK30_PIN1_PA2:
        {
             rk30_mux_api_set(GPIO1A2_UART0CTSN_NAME,0);
            break;	
        }
        case RK30_PIN1_PA3:
        {
             rk30_mux_api_set(GPIO1A3_UART0RTSN_NAME,0);
            break;	
        }
        case RK30_PIN1_PA4:
        {
             rk30_mux_api_set(GPIO1A4_UART1SIN_SPI0CSN0_NAME,0);
            break;	
        }
        case RK30_PIN1_PA5:
        {
             rk30_mux_api_set(GPIO1A5_UART1SOUT_SPI0CLK_NAME,0);
            break;	
        }
        case RK30_PIN1_PA6:
        {
             rk30_mux_api_set(GPIO1A6_UART1CTSN_SPI0RXD_NAME,0);
            break;	
        }
        case RK30_PIN1_PA7:
        {
             rk30_mux_api_set(GPIO1A7_UART1RTSN_SPI0TXD_NAME,0);
            break;	
        }
        case RK30_PIN1_PB0:
        {
             rk30_mux_api_set(GPIO1B0_UART2SIN_NAME,0);
            break;	
        }
        case RK30_PIN1_PB1:
        {
             rk30_mux_api_set(GPIO1B1_UART2SOUT_NAME,0);
            break;	
        }
        case RK30_PIN1_PB2:
        {
             rk30_mux_api_set(GPIO1B2_SPDIFTX_NAME,0);
            break;	
        }
        case RK30_PIN1_PB3:
        {
             rk30_mux_api_set(GPIO1B3_CIF0CLKOUT_NAME,0);
            break;	
        }
        case RK30_PIN1_PB4:
        {
             rk30_mux_api_set(GPIO1B4_CIF0DATA0_NAME,0);
            break;	
        }
        case RK30_PIN1_PB5:
        {
             rk30_mux_api_set(GPIO1B5_CIF0DATA1_NAME,0);
            break;	
        }
        case RK30_PIN1_PB6:
        {
             rk30_mux_api_set(GPIO1B6_CIFDATA10_NAME,0);
            break;	
        }
        case RK30_PIN1_PB7:
        {
             rk30_mux_api_set(GPIO1B7_CIFDATA11_NAME,0);
            break;	
        }
        case RK30_PIN1_PC0:
        {
             rk30_mux_api_set(GPIO1C0_CIF1DATA2_RMIICLKOUT_RMIICLKIN_NAME,0);
            break;	
        }
        case RK30_PIN1_PC1:
        {
             rk30_mux_api_set(GPIO1C1_CIFDATA3_RMIITXEN_NAME,0);
            break;	
        }
        case RK30_PIN1_PC2:
        {
             rk30_mux_api_set(GPIO1C2_CIF1DATA4_RMIITXD1_NAME,0);
            break;	
        }
        case RK30_PIN1_PC3:
        {
             rk30_mux_api_set(GPIO1C3_CIFDATA5_RMIITXD0_NAME,0);
            break;	
        }
        case RK30_PIN1_PC4:
        {
             rk30_mux_api_set(GPIO1C4_CIFDATA6_RMIIRXERR_NAME,0);
            break;	
        }
        case RK30_PIN1_PC5:
        {
             rk29_mux_api_set(GPIO1C5_CIFDATA7_RMIICRSDVALID_NAME,0);
            break;	
        }
        case RK30_PIN1_PC6:
        {
             rk30_mux_api_set(GPIO1C6_CIFDATA8_RMIIRXD1_NAME,0);
            break;	
        }
        case RK30_PIN1_PC7:
        {
             rk30_mux_api_set(GPIO1C7_CIFDATA9_RMIIRXD0_NAME,0);
            break;	
        }
        case RK30_PIN1_PD0:
        {
             rk30_mux_api_set(GPIO1D0_CIF1VSYNC_MIIMD_NAME,0);
            break;	
        }
        case RK30_PIN1_PD1:
        {
             rk30_mux_api_set(GPIO1D1_CIF1HREF_MIIMDCLK_NAME,0);
            break;	
        }
        case RK30_PIN1_PD2:
        {
             rk30_mux_api_set(GPIO1D2_CIF1CLKIN_NAME,0);
            break;	
        }
        case RK30_PIN1_PD3:
        {
             rk30_mux_api_set(GPIO1D3_CIF1DATA0_NAME,0);
            break;	
        }
        case RK30_PIN1_PD4:
        {
             rk30_mux_api_set(GPIO1D4_CIF1DATA1_NAME,0);
            break;	
        }
        case RK30_PIN1_PD5:
        {
             rk30_mux_api_set(GPIO1D5_CIF1DATA10_NAME,0);
            break;	
        }
        case RK30_PIN1_PD6:
        {
             rk30_mux_api_set(GPIO1D6_CIF1DATA11_NAME,0);
            break;	
        }
        case RK30_PIN1_PD7:
        {
             rk30_mux_api_set(GPIO1D7_CIF1CLKOUT_NAME,0);
            break;	
        }
        case RK30_PIN2_PA0:
        {
             rk30_mux_api_set(GPIO2A0_LCDC1DATA0_SMCADDR4_NAME,0);
            break;	
        }
        case RK30_PIN2_PA1:
        {
             rk30_mux_api_set(GPIO2A1_LCDC1DATA1_SMCADDR5_NAME,0);
            break;	
        }
        case RK30_PIN2_PA2:
        {
             rk30_mux_api_set(GPIO2A2_LCDCDATA2_SMCADDR6_NAME,0);
            break;	
        }
        case RK30_PIN2_PA3:
        {
             rk30_mux_api_set(GPIO2A3_LCDCDATA3_SMCADDR7_NAME,0);
            break;	
        }
        case RK30_PIN2_PA4:
        {
             rk30_mux_api_set(GPIO2A4_LCDC1DATA4_SMCADDR8_NAME,0);
            break;	
        }
        case RK30_PIN2_PA5:
        {
             rk30_mux_api_set(GPIO2A5_LCDC1DATA5_SMCADDR9_NAME,0);
            break;	
        }
        case RK30_PIN2_PA6:
        {
             rk30_mux_api_set(GPIO2A6_LCDC1DATA6_SMCADDR10_NAME,0);
            break;	
        }
        case RK30_PIN2_PA7:
        {
             rk30_mux_api_set(GPIO2A7_LCDC1DATA7_SMCADDR11_NAME,0);
            break;	
        }
        case RK30_PIN2_PB0:
        {
             rk30_mux_api_set(GPIO2B0_LCDC1DATA8_SMCADDR12_NAME,0);
            break;	
        }
        case RK30_PIN2_PB1:
        {
             rk30_mux_api_set(GPIO2B1_LCDC1DATA9_SMCADDR13_NAME,0);
            break;	
        }
        case RK30_PIN2_PB2:
        {
             rk30_mux_api_set(GPIO2B2_LCDC1DATA10_SMCADDR14_NAME,0);
            break;	
        }
        case RK30_PIN2_PB3:
        {
             rk30_mux_api_set(GPIO2B3_LCDC1DATA11_SMCADDR15_NAME,0);
            break;	
        }
        case RK30_PIN2_PB4:
        {
             rk30_mux_api_set(GPIO2B4_LCDC1DATA12_SMCADDR16_HSADCDATA9_NAME,0);
            break;	
        }
        case RK30_PIN2_PB5:
        {
             rk30_mux_api_set(GPIO2B5_LCDC1DATA13_SMCADDR17_HSADCDATA8_NAME,0);
            break;	
        }
        case RK30_PIN2_PB6:
        {
             rk30_mux_api_set(GPIO2B6_LCDC1DATA14_SMCADDR18_TSSYNC_NAME,0);
            break;	
        }
        case RK30_PIN2_PB7:
        {
             rk30_mux_api_set(GPIO2B7_LCDC1DATA15_SMCADDR19_HSADCDATA7_NAME,0);
            break;	
        }
        case RK30_PIN2_PC0:
        {
             rk30_mux_api_set(GPIO2C0_LCDCDATA16_GPSCLK_HSADCCLKOUT_NAME,0);
            break;	
        }
        case RK30_PIN2_PC1:
        {
             rk30_mux_api_set(GPIO2C1_LCDC1DATA17_SMCBLSN0_HSADCDATA6_NAME,0);
            break;	
        }
        case RK30_PIN2_PC2:
        {
             rk30_mux_api_set(GPIO2C2_LCDC1DATA18_SMCBLSN1_HSADCDATA5_NAME,0);
            break;	
        }
        case RK30_PIN2_PC3:
        {
             rk29_mux_api_set(GPIO2C3_LCDC1DATA19_SPI1CLK_HSADCDATA0_NAME,0);
            break;	
        }
        case RK30_PIN2_PC4:
        {
             rk30_mux_api_set(GPIO2C4_LCDC1DATA20_SPI1CSN0_HSADCDATA1_NAME,0);
            break;	
        }
        case RK30_PIN2_PC5:
        {
             rk30_mux_api_set(GPIO2C5_LCDC1DATA21_SPI1TXD_HSADCDATA2_NAME,0);
            break;	
        }
        case RK30_PIN2_PC6:
        {
             rk30_mux_api_set(GPIO2C6_LCDC1DATA22_SPI1RXD_HSADCDATA3_NAME,0);
            break;	
        }
        case RK30_PIN2_PC7:
        {
             rk30_mux_api_set(GPIO2C7_LCDC1DATA23_SPI1CSN1_HSADCDATA4_NAME,0);
            break;	
        }
        case RK30_PIN2_PD0:
        {
             rk30_mux_api_set(GPIO2D0_LCDC1DCLK_NAME,0);
            break;	
        }
        case RK30_PIN2_PD1:
        {
             rk30_mux_api_set(GPIO2D1_LCDC1DEN_SMCCSN1_NAME,0);
            break;	
        }
        case RK30_PIN2_PD2:
        {
             rk30_mux_api_set(GPIO2D2_LCDC1HSYNC_NAME,0);
            break;	
        }
        case RK30_PIN2_PD3:
        {
             rk30_mux_api_set(GPIO2D3_LCDC1VSYNC_NAME,0);
            break;	
        }
        case RK30_PIN2_PD4:
        {
             rk30_mux_api_set(GPIO2D4_I2C0SDA_NAME,0);
            break;	
        }
        case RK30_PIN2_PD5:
        {
             rk30_mux_api_set(GPIO2D5_I2C0SCL_NAME,0);
            break;	
        }
        case RK30_PIN2_PD6:
        {
             rk30_mux_api_set(GPIO2D6_I2C1SDA_NAME,0);
            break;	
        }
        case RK30_PIN2_PD7:
        {
             rk30_mux_api_set(GPIO2D7_I2C1SCL_NAME,0);
            break;	
        }
        case RK30_PIN3_PA0:
        {
             rk30_mux_api_set(GPIO3A0_I2C2SDA_NAME,0);
            break;	
        }
        case RK30_PIN3_PA1:
        {
             rk30_mux_api_set(GPIO3A1_I2C2SCL_NAME,0);
            break;	
        }
        case RK30_PIN3_PA2:
        {
             rk30_mux_api_set(GPIO3A2_I2C3SDA_NAME,0);
            break;	
        }
        case RK30_PIN3_PA3:
        {
             rk30_mux_api_set(GPIO3A3_I2C3SCL_NAME,0);
            break;	
        }
        case RK30_PIN3_PA4:
        {
             rk30_mux_api_set(GPIO3A4_I2C4SDA_NAME,0);
            break;	
        }
        case RK30_PIN3_PA5:
        {
             rk30_mux_api_set(GPIO3A5_I2C4SCL_NAME,0);
            break;	
        }
        case RK30_PIN3_PA6:
        {
             rk30_mux_api_set(GPIO3A6_SDMMC0RSTNOUT_NAME,0);
            break;	
        }
        case RK30_PIN3_PA7:
        {
             rk30_mux_api_set(GPIO3A7_SDMMC0PWREN_NAME,0);
            break;	
        }
        case RK30_PIN3_PB0:
        {
             rk30_mux_api_set(GPIO3B0_SDMMC0CLKOUT_NAME,0);
            break;	
        }
        case RK30_PIN3_PB1:
        {
             rk30_mux_api_set(GPIO3B1_SDMMC0CMD_NAME,0);
            break;	
        }
        case RK30_PIN3_PB2:
        {
             rk30_mux_api_set(GPIO3B2_SDMMC0DATA0_NAME,0);
            break;	
        }
        case RK30_PIN3_PB3:
        {
             rk30_mux_api_set(GPIO3B3_SDMMC0DATA1_NAME,0);
            break;	
        }
        case RK30_PIN3_PB4:
        {
             rk30_mux_api_set(GPIO3B4_SDMMC0DATA2_NAME,0);
            break;	
        }
        case RK30_PIN3_PB5:
        {
             rk30_mux_api_set(GPIO3B5_SDMMC0DATA3_NAME,0);
            break;	
        }
        case RK30_PIN3_PB6:
        {
             rk30_mux_api_set(GPIO3B6_SDMMC0DETECTN_NAME,0);
            break;	
        }
        case RK30_PIN3_PB7:
        {
             rk30_mux_api_set(GPIO3B7_SDMMC0WRITEPRT_NAME,0);
            break;	
        }
        case RK30_PIN3_PC0:
        {
             rk30_mux_api_set(GPIO3C0_SMMC1CMD_NAME,0);
            break;	
        }
        case RK30_PIN3_PC1:
        {
             rk30_mux_api_set(GPIO3C1_SDMMC1DATA0_NAME,0);
            break;	
        }
        case RK30_PIN3_PC2:
        {
             rk30_mux_api_set(GPIO3C2_SDMMC1DATA1_NAME,0);
            break;	
        }
        case RK30_PIN3_PC3:
        {
             rk30_mux_api_set(GPIO3C3_SDMMC1DATA2_NAME,0);
            break;	
        }
        case RK30_PIN3_PC4:
        {
             rk30_mux_api_set(GPIO3C4_SDMMC1DATA3_NAME,0);
            break;	
        }
        case RK30_PIN3_PC5:
        {
             rk30_mux_api_set(GPIO3C5_SDMMC1CLKOUT_NAME,0);
            break;	
        }
        case RK30_PIN3_PC6:
        {
             rk30_mux_api_set(GPIO3C6_SDMMC1DETECTN_NAME,0);
            break;	
        }
        case RK30_PIN3_PC7:
        {
             rk30_mux_api_set(GPIO3C7_SDMMC1WRITEPRT_NAME,0);
            break;	
        }
        case RK30_PIN3_PD0:
        {
             rk30_mux_api_set(GPIO3D0_SDMMC1PWREN_NAME,0);
            break;	
        }
        case RK30_PIN3_PD1:
        {
             rk30_mux_api_set(GPIO3D1_SDMMC1BACKENDPWR_NAME,0);
            break;	
        }
        case RK30_PIN3_PD2:
        {
             rk30_mux_api_set(GPIO3D2_SDMMC1INTN_NAME,0);
            break;	
        }
        case RK30_PIN3_PD3:
        {
             rk30_mux_api_set(GPIO3D3_UART3SIN_NAME,0);
            break;	
        }
        case RK30_PIN3_PD4:
        {
             rk30_mux_api_set(GPIO3D4_UART3SOUT_NAME,0);
            break;	
        }
        case RK30_PIN3_PD5:
        {
             rk30_mux_api_set(GPIO3D5_UART3CTSN_NAME,0);
            break;	
        }
        case RK30_PIN3_PD6:
        {
             rk30_mux_api_set(GPIO3D6_UART3RTSN_NAME,0);
            break;	
        }
        case RK30_PIN3_PD7:
        {
             rk30_mux_api_set(GPIO3D7_FLASHDQS_EMMCCLKOUT_NAME,0);
            break;	
        }
        case RK30_PIN4_PA0:
	{
		 rk30_mux_api_set(GPIO4A0_FLASHDATA8_NAME,0);
		break;	
	}
        case RK30_PIN4_PA1:
	{
		 rk30_mux_api_set(GPIO4A1_FLASHDATA9_NAME,0);
		break;	
	}
        case RK30_PIN4_PA2:
	{
		 rk30_mux_api_set(GPIO4A2_FLASHDATA10_NAME,0);
		break;	
	}
			
        case RK30_PIN4_PA3:
	{
		 rk30_mux_api_set(GPIO4A3_FLASHDATA11_NAME,0);
		break;	
	}
        case RK30_PIN4_PA4:
	{
		 rk30_mux_api_set(GPIO4A4_FLASHDATA12_NAME,0);
		break;	
	}
        case RK30_PIN4_PA5:
        {
             rk30_mux_api_set(GPIO4A5_FLASHDATA13_NAME,0);
            break;	
        }
        case RK30_PIN4_PA6:
        {
             rk30_mux_api_set(GPIO4A6_FLASHDATA14_NAME,0);
            break;	
        }
        case RK30_PIN4_PA7:
        {
             rk30_mux_api_set(GPIO4A7_FLASHDATA15_NAME,0);
            break;	
        }
        case RK30_PIN4_PB0:
        {
             rk30_mux_api_set(GPIO4B0_FLASHCSN1_NAME,0);
            break;	
        }
        case RK30_PIN4_PB1:
        {
             rk30_mux_api_set(GPIO4B1_FLASHCSN2_EMMCCMD_NAME,0);
            break;	
        }
        case RK30_PIN4_PB2:
        {
             rk30_mux_api_set(GPIO4B2_FLASHCSN3_EMMCRSTNOUT_NAME,0);
            break;	
        }
        case RK30_PIN4_PB3:
        {
             rk30_mux_api_set(GPIO4B3_FLASHCSN4_NAME,0);
            break;	
        }
        case RK30_PIN4_PB4:
        {
             rk30_mux_api_set(GPIO4B4_FLASHCSN5_NAME,0);
            break;	
        }
        case RK30_PIN4_PB5:
        {
             rk30_mux_api_set(GPIO4B5_FLASHCSN6_NAME,0);
            break;	
        }
        case RK30_PIN4_PB6:
        {
             rk30_mux_api_set(GPIO4B6_FLASHCSN7_NAME ,0);
            break;	
        }
        case RK30_PIN4_PB7:
        {
             rk30_mux_api_set(GPIO4B7_SPI0CSN1_NAME,0);
            break;	
        }
        case RK30_PIN4_PC0:
        {
             rk30_mux_api_set(GPIO4C0_SMCDATA0_TRACEDATA0_NAME,0);
            break;	
        }
        case RK30_PIN4_PC1:
        {
             rk30_mux_api_set(GPIO4C1_SMCDATA1_TRACEDATA1_NAME,0);
            break;	
        }
        case RK30_PIN4_PC2:
        {
             rk30_mux_api_set(GPIO4C2_SMCDATA2_TRACEDATA2_NAME,0);
            break;	
        }
        case RK30_PIN4_PC3:
        {
             rk30_mux_api_set(GPIO4C3_SMCDATA3_TRACEDATA3_NAME,0);
            break;	
        }
        case RK30_PIN4_PC4:
        {
             rk30_mux_api_set(GPIO4C4_SMCDATA4_TRACEDATA4_NAME,0);
            break;	
        }
        case RK30_PIN4_PC5:
        {
             rk30_mux_api_set(GPIO4C5_SMCDATA5_TRACEDATA5_NAME,0);
            break;	
        }
        case RK30_PIN4_PC6:
        {
             rk30_mux_api_set(GPIO4C6_SMCDATA6_TRACEDATA6_NAME,0);
            break;	
        }


        case RK30_PIN4_PC7:
        {
             rk30_mux_api_set(GPIO4C7_SMCDATA7_TRACEDATA7_NAME,0);
            break;	
        }
        case RK30_PIN4_PD0:
	    {
		     rk30_mux_api_set(GPIO4D0_SMCDATA8_TRACEDATA8_NAME,0);			   
		     break;	
	    }
        case RK30_PIN4_PD1:
        {
             rk30_mux_api_set(GPIO4D1_SMCDATA9_TRACEDATA9_NAME,0);             
             break;	
        }
        case RK30_PIN4_PD2:
	    {
		     rk30_mux_api_set(GPIO4D2_SMCDATA10_TRACEDATA10_NAME,0);			            
		     break;	
	    }
        case RK30_PIN4_PD3:
        {
             rk30_mux_api_set(GPIO4D3_SMCDATA11_TRACEDATA11_NAME,0);           
             break;	
        }
        case RK30_PIN4_PD4:
        {
             rk30_mux_api_set(GPIO4D4_SMCDATA12_TRACEDATA12_NAME,0);
            break;	
        }
        case RK30_PIN4_PD5:
        {
             rk30_mux_api_set(GPIO4D5_SMCDATA13_TRACEDATA13_NAME,0);
            break;	
        }
        case RK30_PIN4_PD6:
        {
             rk30_mux_api_set(GPIO4D6_SMCDATA14_TRACEDATA14_NAME,0);
            break;	
        }
        case RK30_PIN4_PD7:
        {
             rk30_mux_api_set(GPIO4D7_SMCDATA15_TRACEDATA15_NAME,0);
            break;	
        } 
        case RK30_PIN6_PA0:
        case RK30_PIN6_PA1:
        case RK30_PIN6_PA2:
        case RK30_PIN6_PA3:
        case RK30_PIN6_PA4:
        case RK30_PIN6_PA5:
        case RK30_PIN6_PA6:
        case RK30_PIN6_PA7:
        case RK30_PIN6_PB0:
        case RK30_PIN6_PB1:
        case RK30_PIN6_PB2:
        case RK30_PIN6_PB3:
        case RK30_PIN6_PB4:
        case RK30_PIN6_PB5:
        case RK30_PIN6_PB6:
			break;
        case RK30_PIN6_PB7:
		{
			 rk30_mux_api_set(GPIO6B7_TESTCLOCKOUT_NAME,0);
			break;	
		} 
        default:
        {
            printk("Pin=%d isn't RK GPIO, Please init it's iomux yourself!",pin);
            break;
        }
    }
    return 0;
}
#define PMEM_CAM_BASE 0 //just for compile ,no meaning
#include "../../../arch/arm/plat-rk/rk_camera.c"



static u64 rockchip_device_camera_dmamask = 0xffffffffUL;
static struct resource rk_camera_resource_host_0[] = {
	[0] = {
		.start = RK30_CIF0_PHYS,
		.end   = RK30_CIF0_PHYS + RK30_CIF0_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_CIF0,
		.end   = IRQ_CIF0,
		.flags = IORESOURCE_IRQ,
	}
};
static struct resource rk_camera_resource_host_1[] = {
	[0] = {
		.start = RK30_CIF1_PHYS,
		.end   = RK30_CIF1_PHYS + RK30_CIF1_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_CIF1,
		.end   = IRQ_CIF1,
		.flags = IORESOURCE_IRQ,
	}
};
/*platform_device : */
 struct platform_device rk_device_camera_host_0 = {
	.name		  = RK29_CAM_DRV_NAME,
	.id 	  = RK_CAM_PLATFORM_DEV_ID_0,				/* This is used to put cameras on this interface */
	.num_resources	  = ARRAY_SIZE(rk_camera_resource_host_0),
	.resource	  = rk_camera_resource_host_0,
	.dev			= {
		.dma_mask = &rockchip_device_camera_dmamask,
		.coherent_dma_mask = 0xffffffffUL,
		.platform_data	= &rk_camera_platform_data,
	}
};
/*platform_device : */
 struct platform_device rk_device_camera_host_1 = {
	.name		  = RK29_CAM_DRV_NAME,
	.id 	  = RK_CAM_PLATFORM_DEV_ID_1,				/* This is used to put cameras on this interface */
	.num_resources	  = ARRAY_SIZE(rk_camera_resource_host_1),
	.resource	  = rk_camera_resource_host_1,
	.dev			= {
		.dma_mask = &rockchip_device_camera_dmamask,
		.coherent_dma_mask = 0xffffffffUL,
		.platform_data	= &rk_camera_platform_data,
	}
};

static void rk_init_camera_plateform_data(void)
{
    int i,dev_idx;
    
    dev_idx = 0;
    for (i=0; i<RK_CAM_NUM; i++) {
        rk_camera_platform_data.sensor_init_data[i] = &rk_init_data_sensor[i];
        if (rk_camera_platform_data.register_dev[i].device_info.name) {            
            rk_camera_platform_data.register_dev[i].link_info.board_info = 
                &rk_camera_platform_data.register_dev[i].i2c_cam_info;
            rk_camera_platform_data.register_dev[i].device_info.id = dev_idx;
            rk_camera_platform_data.register_dev[i].device_info.dev.platform_data = 
                &rk_camera_platform_data.register_dev[i].link_info;
            dev_idx++;
        }
    }
}

static void rk30_camera_request_reserve_mem(void)
{
#ifdef CONFIG_VIDEO_RK29_WORK_IPP
    #ifdef VIDEO_RKCIF_WORK_SIMUL_OFF
        rk_camera_platform_data.meminfo.name = "camera_ipp_mem";
        rk_camera_platform_data.meminfo.start = board_mem_reserve_add("camera_ipp_mem",PMEM_CAMIPP_NECESSARY);
        rk_camera_platform_data.meminfo.size= PMEM_CAMIPP_NECESSARY;

        memcpy(&rk_camera_platform_data.meminfo_cif1,&rk_camera_platform_data.meminfo,sizeof(struct rk29camera_mem_res));
    #else
        rk_camera_platform_data.meminfo.name = "camera_ipp_mem_0";
        rk_camera_platform_data.meminfo.start = board_mem_reserve_add("camera_ipp_mem_0",PMEM_CAMIPP_NECESSARY_CIF_0);
        rk_camera_platform_data.meminfo.size= PMEM_CAMIPP_NECESSARY_CIF_0;
        
        rk_camera_platform_data.meminfo_cif1.name = "camera_ipp_mem_1";
        rk_camera_platform_data.meminfo_cif1.start =board_mem_reserve_add("camera_ipp_mem_1",PMEM_CAMIPP_NECESSARY_CIF_1);
        rk_camera_platform_data.meminfo_cif1.size= PMEM_CAMIPP_NECESSARY_CIF_1;
    #endif
 #endif
 #if PMEM_CAM_NECESSARY
        android_pmem_cam_pdata.start = board_mem_reserve_add((char*)(android_pmem_cam_pdata.name),PMEM_CAM_NECESSARY);
        android_pmem_cam_pdata.size= PMEM_CAM_NECESSARY;
 #endif

}
static int rk_register_camera_devices(void)
{
    int i;
    int host_registered_0,host_registered_1;
    
	rk_init_camera_plateform_data();

    host_registered_0 = 0;
    host_registered_1 = 0;
    for (i=0; i<RK_CAM_NUM; i++) {
        if (rk_camera_platform_data.register_dev[i].device_info.name) {
            if (rk_camera_platform_data.register_dev[i].link_info.bus_id == RK_CAM_PLATFORM_DEV_ID_0) {
                if (!host_registered_0) {
                    platform_device_register(&rk_device_camera_host_0);
                    host_registered_0 = 1;
                }
            } else if (rk_camera_platform_data.register_dev[i].link_info.bus_id == RK_CAM_PLATFORM_DEV_ID_1) {
                if (!host_registered_1) {
                    platform_device_register(&rk_device_camera_host_1);
                    host_registered_1 = 1;
                }
            } 
        }
    }

    for (i=0; i<RK_CAM_NUM; i++) {
        if (rk_camera_platform_data.register_dev[i].device_info.name) {
            platform_device_register(&rk_camera_platform_data.register_dev[i].device_info);
        }
    }
 #if PMEM_CAM_NECESSARY
            platform_device_register(&android_pmem_cam_device);
 #endif
    
	return 0;
}

module_init(rk_register_camera_devices);
#endif

#endif //#ifdef CONFIG_VIDEO_RK
