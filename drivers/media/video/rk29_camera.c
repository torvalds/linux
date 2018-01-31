/* SPDX-License-Identifier: GPL-2.0 */
#include <mach/rk29_camera.h> 
#include <mach/iomux.h>

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
        case RK29_PIN0_PA0:        
        case RK29_PIN0_PA1:        
        case RK29_PIN0_PA2:
        case RK29_PIN0_PA3:
        case RK29_PIN0_PA4:
        {
            break;	
        }
        case RK29_PIN0_PA5:
        {
             rk29_mux_api_set(GPIO0A5_FLASHDQS_NAME,0);
            break;	
        }
        case RK29_PIN0_PA6:
        {
             rk29_mux_api_set(GPIO0A6_MIIMD_NAME,0);
            break;	
        }
        case RK29_PIN0_PA7:
        {
             rk29_mux_api_set(GPIO0A7_MIIMDCLK_NAME,0);
            break;	
        }
        case RK29_PIN0_PB0:
        {
             rk29_mux_api_set(GPIO0B0_EBCSDCE0_SMCADDR0_HOSTDATA0_NAME,0);
            break;	
        }
        case RK29_PIN0_PB1:
        {
             rk29_mux_api_set(GPIO0B1_EBCSDCE1_SMCADDR1_HOSTDATA1_NAME,0);
            break;	
        }
        case RK29_PIN0_PB2:
        {
             rk29_mux_api_set(GPIO0B2_EBCSDCE2_SMCADDR2_HOSTDATA2_NAME,0);
            break;	
        }
        case RK29_PIN0_PB3:
        {
             rk29_mux_api_set(GPIO0B3_EBCBORDER0_SMCADDR3_HOSTDATA3_NAME,0);
            break;	
        }
        case RK29_PIN0_PB4:
        {
             rk29_mux_api_set(GPIO0B4_EBCBORDER1_SMCWEN_NAME,0);
            break;	
        }
        case RK29_PIN0_PB5:
        {
             rk29_mux_api_set(GPIO0B5_EBCVCOM_SMCBLSN0_NAME,0);
            break;	
        }
        case RK29_PIN0_PB6:
        {
             rk29_mux_api_set(GPIO0B6_EBCSDSHR_SMCBLSN1_HOSTINT_NAME,0);
            break;	
        }
        case RK29_PIN0_PB7:
        {
             rk29_mux_api_set(GPIO0B7_EBCGDOE_SMCOEN_NAME,0);
            break;	
        }
        case RK29_PIN0_PC0:
        {
             rk29_mux_api_set(GPIO0C0_EBCGDSP_SMCDATA8_NAME,0);
            break;	
        }
        case RK29_PIN0_PC1:
        {
             rk29_mux_api_set(GPIO0C1_EBCGDR1_SMCDATA9_NAME,0);
            break;	
        }
        case RK29_PIN0_PC2:
        {
             rk29_mux_api_set(GPIO0C2_EBCSDCE0_SMCDATA10_NAME,0);
            break;	
        }
        case RK29_PIN0_PC3:
        {
             rk29_mux_api_set(GPIO0C3_EBCSDCE1_SMCDATA11_NAME,0);
            break;	
        }
        case RK29_PIN0_PC4:
        {
             rk29_mux_api_set(GPIO0C4_EBCSDCE2_SMCDATA12_NAME,0);
            break;	
        }
        case RK29_PIN0_PC5:
        {
             rk29_mux_api_set(GPIO0C5_EBCSDCE3_SMCDATA13_NAME,0);
            break;	
        }
        case RK29_PIN0_PC6:
        {
             rk29_mux_api_set(GPIO0C6_EBCSDCE4_SMCDATA14_NAME,0);
            break;	
        }
        case RK29_PIN0_PC7:
        {
             rk29_mux_api_set(GPIO0C7_EBCSDCE5_SMCDATA15_NAME,0);
            break;	
        }
        case RK29_PIN0_PD0:
        {
             rk29_mux_api_set(GPIO0D0_EBCSDOE_SMCADVN_NAME,0);
            break;	
        }
        case RK29_PIN0_PD1:
        {
             rk29_mux_api_set(GPIO0D1_EBCGDCLK_SMCADDR4_HOSTDATA4_NAME,0);
            break;	
        }
        case RK29_PIN0_PD2:
        {
             rk29_mux_api_set(GPIO0D2_FLASHCSN1_NAME,0);
            break;	
        }
        case RK29_PIN0_PD3:
        {
             rk29_mux_api_set(GPIO0D3_FLASHCSN2_NAME,0);
            break;	
        }
        case RK29_PIN0_PD4:
        {
             rk29_mux_api_set(GPIO0D4_FLASHCSN3_NAME,0);
            break;	
        }
        case RK29_PIN0_PD5:
        {
             rk29_mux_api_set(GPIO0D5_FLASHCSN4_NAME,0);
            break;	
        }
        case RK29_PIN0_PD6:
        {
             rk29_mux_api_set(GPIO0D6_FLASHCSN5_NAME,0);
            break;	
        }
        case RK29_PIN0_PD7:
        {
             rk29_mux_api_set(GPIO0D7_FLASHCSN6_NAME,0);
            break;	
        }
        case RK29_PIN1_PA0:
        {
             rk29_mux_api_set(GPIO1A0_FLASHCS7_MDDRTQ_NAME,0);
            break;	
        }
        case RK29_PIN1_PA1:
        {
             rk29_mux_api_set(GPIO1A1_SMCCSN0_NAME,0);
            break;	
        }
        case RK29_PIN1_PA2:
        {
             rk29_mux_api_set(GPIO1A2_SMCCSN1_NAME,0);
            break;	
        }
        case RK29_PIN1_PA3:
        {
             rk29_mux_api_set(GPIO1A3_EMMCDETECTN_SPI1CS1_NAME,0);
            break;	
        }
        case RK29_PIN1_PA4:
        {
             rk29_mux_api_set(GPIO1A4_EMMCWRITEPRT_SPI0CS1_NAME,0);
            break;	
        }
        case RK29_PIN1_PA5:
        {
             rk29_mux_api_set(GPIO1A5_EMMCPWREN_PWM3_NAME,0);
            break;	
        }
        case RK29_PIN1_PA6:
        {
             rk29_mux_api_set(GPIO1A6_I2C1SDA_NAME,0);
            break;	
        }
        case RK29_PIN1_PA7:
        {
             rk29_mux_api_set(GPIO1A7_I2C1SCL_NAME,0);
            break;	
        }
        case RK29_PIN1_PB0:
        {
             rk29_mux_api_set(GPIO1B0_VIPDATA0_NAME,0);
            break;	
        }
        case RK29_PIN1_PB1:
        {
             rk29_mux_api_set(GPIO1B1_VIPDATA1_NAME,0);
            break;	
        }
        case RK29_PIN1_PB2:
        {
             rk29_mux_api_set(GPIO1B2_VIPDATA2_NAME,0);
            break;	
        }
        case RK29_PIN1_PB3:
        {
             rk29_mux_api_set(GPIO1B3_VIPDATA3_NAME,0);
            break;	
        }
        case RK29_PIN1_PB4:
        {
             rk29_mux_api_set(GPIO1B4_VIPCLKOUT_NAME,0);
            break;	
        }
        case RK29_PIN1_PB5:
        {
             rk29_mux_api_set(GPIO1B5_PWM0_NAME,0);
            break;	
        }
        case RK29_PIN1_PB6:
        {
             rk29_mux_api_set(GPIO1B6_UART0SIN_NAME,0);
            break;	
        }
        case RK29_PIN1_PB7:
        {
             rk29_mux_api_set(GPIO1B7_UART0SOUT_NAME,0);
            break;	
        }
        case RK29_PIN1_PC0:
        {
             rk29_mux_api_set(GPIO1C0_UART0CTSN_SDMMC1DETECTN_NAME,0);
            break;	
        }
        case RK29_PIN1_PC1:
        {
             rk29_mux_api_set(GPIO1C1_UART0RTSN_SDMMC1WRITEPRT_NAME,0);
            break;	
        }
        case RK29_PIN1_PC2:
        {
             rk29_mux_api_set(GPIO1C2_SDMMC1CMD_NAME,0);
            break;	
        }
        case RK29_PIN1_PC3:
        {
             rk29_mux_api_set(GPIO1C3_SDMMC1DATA0_NAME,0);
            break;	
        }
        case RK29_PIN1_PC4:
        {
             rk29_mux_api_set(GPIO1C4_SDMMC1DATA1_NAME,0);
            break;	
        }
        case RK29_PIN1_PC5:
        {
             rk29_mux_api_set(GPIO1C5_SDMMC1DATA2_NAME,0);
            break;	
        }
        case RK29_PIN1_PC6:
        {
             rk29_mux_api_set(GPIO1C6_SDMMC1DATA3_NAME,0);
            break;	
        }
        case RK29_PIN1_PC7:
        {
             rk29_mux_api_set(GPIO1C7_SDMMC1CLKOUT_NAME,0);
            break;	
        }
        case RK29_PIN1_PD0:
        {
             rk29_mux_api_set(GPIO1D0_SDMMC0CLKOUT_NAME,0);
            break;	
        }
        case RK29_PIN1_PD1:
        {
             rk29_mux_api_set(GPIO1D1_SDMMC0CMD_NAME,0);
            break;	
        }
        case RK29_PIN1_PD2:
        {
             rk29_mux_api_set(GPIO1D2_SDMMC0DATA0_NAME,0);
            break;	
        }
        case RK29_PIN1_PD3:
        {
             rk29_mux_api_set(GPIO1D3_SDMMC0DATA1_NAME,0);
            break;	
        }
        case RK29_PIN1_PD4:
        {
             rk29_mux_api_set(GPIO1D4_SDMMC0DATA2_NAME,0);
            break;	
        }
        case RK29_PIN1_PD5:
        {
             rk29_mux_api_set(GPIO1D5_SDMMC0DATA3_NAME,0);
            break;	
        }
        case RK29_PIN1_PD6:
        {
             rk29_mux_api_set(GPIO1D6_SDMMC0DATA4_NAME,0);
            break;	
        }
        case RK29_PIN1_PD7:
        {
             rk29_mux_api_set(GPIO1D7_SDMMC0DATA5_NAME,0);
            break;	
        }
        case RK29_PIN2_PA0:
        {
             rk29_mux_api_set(GPIO2A0_SDMMC0DATA6_NAME,0);
            break;	
        }
        case RK29_PIN2_PA1:
        {
             rk29_mux_api_set(GPIO2A1_SDMMC0DATA7_NAME,0);
            break;	
        }
        case RK29_PIN2_PA2:
        {
             rk29_mux_api_set(GPIO2A2_SDMMC0DETECTN_NAME,0);
            break;	
        }
        case RK29_PIN2_PA3:
        {
             rk29_mux_api_set(GPIO2A3_SDMMC0WRITEPRT_PWM2_NAME,0);
            break;	
        }
        case RK29_PIN2_PA4:
        {
             rk29_mux_api_set(GPIO2A4_UART1SIN_NAME,0);
            break;	
        }
        case RK29_PIN2_PA5:
        {
             rk29_mux_api_set(GPIO2A5_UART1SOUT_NAME,0);
            break;	
        }
        case RK29_PIN2_PA6:
        {
             rk29_mux_api_set(GPIO2A6_UART2CTSN_NAME,0);
            break;	
        }
        case RK29_PIN2_PA7:
        {
             rk29_mux_api_set(GPIO2A7_UART2RTSN_NAME,0);
            break;	
        }
        case RK29_PIN2_PB0:
        {
             rk29_mux_api_set(GPIO2B0_UART2SIN_NAME,0);
            break;	
        }
        case RK29_PIN2_PB1:
        {
             rk29_mux_api_set(GPIO2B1_UART2SOUT_NAME,0);
            break;	
        }
        case RK29_PIN2_PB2:
        {
             rk29_mux_api_set(GPIO2B2_UART3SIN_NAME,0);
            break;	
        }
        case RK29_PIN2_PB3:
        {
             rk29_mux_api_set(GPIO2B3_UART3SOUT_NAME,0);
            break;	
        }
        case RK29_PIN2_PB4:
        {
             rk29_mux_api_set(GPIO2B4_UART3CTSN_I2C3SDA_NAME,0);
            break;	
        }
        case RK29_PIN2_PB5:
        {
             rk29_mux_api_set(GPIO2B5_UART3RTSN_I2C3SCL_NAME,0);
            break;	
        }
        case RK29_PIN2_PB6:
        {
             rk29_mux_api_set(GPIO2B6_I2C0SDA_NAME,0);
            break;	
        }
        case RK29_PIN2_PB7:
        {
             rk29_mux_api_set(GPIO2B7_I2C0SCL_NAME,0);
            break;	
        }
        case RK29_PIN2_PC0:
        {
             rk29_mux_api_set(GPIO2C0_SPI0CLK_NAME,0);
            break;	
        }
        case RK29_PIN2_PC1:
        {
             rk29_mux_api_set(GPIO2C1_SPI0CSN0_NAME,0);
            break;	
        }
        case RK29_PIN2_PC2:
        {
             rk29_mux_api_set(GPIO2C2_SPI0TXD_NAME,0);
            break;	
        }
        case RK29_PIN2_PC3:
        {
             rk29_mux_api_set(GPIO2C3_SPI0RXD_NAME,0);
            break;	
        }
        case RK29_PIN2_PC4:
        {
             rk29_mux_api_set(GPIO2C4_SPI1CLK_NAME,0);
            break;	
        }
        case RK29_PIN2_PC5:
        {
             rk29_mux_api_set(GPIO2C5_SPI1CSN0_NAME,0);
            break;	
        }
        case RK29_PIN2_PC6:
        {
             rk29_mux_api_set(GPIO2C6_SPI1TXD_NAME,0);
            break;	
        }
        case RK29_PIN2_PC7:
        {
             rk29_mux_api_set(GPIO2C7_SPI1RXD_NAME,0);
            break;	
        }
        case RK29_PIN2_PD0:
        {
             rk29_mux_api_set(GPIO2D0_I2S0CLK_MIIRXCLKIN_NAME,0);
            break;	
        }
        case RK29_PIN2_PD1:
        {
             rk29_mux_api_set(GPIO2D1_I2S0SCLK_MIICRS_NAME,0);
            break;	
        }
        case RK29_PIN2_PD2:
        {
             rk29_mux_api_set(GPIO2D2_I2S0LRCKRX_MIITXERR_NAME,0);
            break;	
        }
        case RK29_PIN2_PD3:
        {
             rk29_mux_api_set(GPIO2D3_I2S0SDI_MIICOL_NAME,0);
            break;	
        }
        case RK29_PIN2_PD4:
        {
             rk29_mux_api_set(GPIO2D4_I2S0SDO0_MIIRXD2_NAME,0);
            break;	
        }
        case RK29_PIN2_PD5:
        {
             rk29_mux_api_set(GPIO2D5_I2S0SDO1_MIIRXD3_NAME,0);
            break;	
        }
        case RK29_PIN2_PD6:
        {
             rk29_mux_api_set(GPIO2D6_I2S0SDO2_MIITXD2_NAME,0);
            break;	
        }
        case RK29_PIN2_PD7:
        {
             rk29_mux_api_set(GPIO2D7_I2S0SDO3_MIITXD3_NAME,0);
            break;	
        }
        case RK29_PIN3_PA0:
        {
             rk29_mux_api_set(GPIO3A0_I2S1CLK_NAME,0);
            break;	
        }
        case RK29_PIN3_PA1:
        {
             rk29_mux_api_set(GPIO3A1_I2S1SCLK_NAME,0);
            break;	
        }
        case RK29_PIN3_PA2:
        {
             rk29_mux_api_set(GPIO3A2_I2S1LRCKRX_NAME,0);
            break;	
        }
        case RK29_PIN3_PA3:
        {
             rk29_mux_api_set(GPIO3A3_I2S1SDI_NAME,0);
            break;	
        }
        case RK29_PIN3_PA4:
        {
             rk29_mux_api_set(GPIO3A4_I2S1SDO_NAME,0);
            break;	
        }
        case RK29_PIN3_PA5:
        {
             rk29_mux_api_set(GPIO3A5_I2S1LRCKTX_NAME,0);
            break;	
        }
        case RK29_PIN3_PA6:
        {
             rk29_mux_api_set(GPIO3A6_SMCADDR14_HOSTDATA14_NAME,0);
            break;	
        }
        case RK29_PIN3_PA7:
        {
             rk29_mux_api_set(GPIO3A7_SMCADDR15_HOSTDATA15_NAME,0);
            break;	
        }
        case RK29_PIN3_PB0:
        {
             rk29_mux_api_set(GPIO3B0_EMMCLKOUT_NAME,0);
            break;	
        }
        case RK29_PIN3_PB1:
        {
             rk29_mux_api_set(GPIO3B1_EMMCMD_NAME,0);
            break;	
        }
        case RK29_PIN3_PB2:
        {
             rk29_mux_api_set(GPIO3B2_EMMCDATA0_NAME,0);
            break;	
        }
        case RK29_PIN3_PB3:
        {
             rk29_mux_api_set(GPIO3B3_EMMCDATA1_NAME,0);
            break;	
        }
        case RK29_PIN3_PB4:
        {
             rk29_mux_api_set(GPIO3B4_EMMCDATA2_NAME,0);
            break;	
        }
        case RK29_PIN3_PB5:
        {
             rk29_mux_api_set(GPIO3B5_EMMCDATA3_NAME,0);
            break;	
        }
        case RK29_PIN3_PB6:
        {
             rk29_mux_api_set(GPIO3B6_EMMCDATA4_NAME,0);
            break;	
        }
        case RK29_PIN3_PB7:
        {
             rk29_mux_api_set(GPIO3B7_EMMCDATA5_NAME,0);
            break;	
        }
        case RK29_PIN3_PC0:
        {
             rk29_mux_api_set(GPIO3C0_EMMCDATA6_NAME,0);
            break;	
        }
        case RK29_PIN3_PC1:
        {
             rk29_mux_api_set(GPIO3C1_EMMCDATA7_NAME,0);
            break;	
        }
        case RK29_PIN3_PC2:
        {
             rk29_mux_api_set(GPIO3C2_SMCADDR13_HOSTDATA13_NAME,0);
            break;	
        }
        case RK29_PIN3_PC3:
        {
             rk29_mux_api_set(GPIO3C3_SMCADDR10_HOSTDATA10_NAME,0);
            break;	
        }
        case RK29_PIN3_PC4:
        {
             rk29_mux_api_set(GPIO3C4_SMCADDR11_HOSTDATA11_NAME,0);
            break;	
        }
        case RK29_PIN3_PC5:
        {
             rk29_mux_api_set(GPIO3C5_SMCADDR12_HOSTDATA12_NAME,0);
            break;	
        }
        case RK29_PIN3_PC6:
        {
             rk29_mux_api_set(GPIO3C6_SMCADDR16_HOSTDATA16_NAME,0);
            break;	
        }
        case RK29_PIN3_PC7:
        {
             rk29_mux_api_set(GPIO3C7_SMCADDR17_HOSTDATA17_NAME,0);
            break;	
        }
        case RK29_PIN3_PD0:
        {
             rk29_mux_api_set(GPIO3D0_SMCADDR18_HOSTADDR0_NAME,0);
            break;	
        }
        case RK29_PIN3_PD1:
        {
             rk29_mux_api_set(GPIO3D1_SMCADDR19_HOSTADDR1_NAME,0);
            break;	
        }
        case RK29_PIN3_PD2:
        {
             rk29_mux_api_set(GPIO3D2_HOSTCSN_NAME,0);
            break;	
        }
        case RK29_PIN3_PD3:
        {
             rk29_mux_api_set(GPIO3D3_HOSTRDN_NAME,0);
            break;	
        }
        case RK29_PIN3_PD4:
        {
             rk29_mux_api_set(GPIO3D4_HOSTWRN_NAME,0);
            break;	
        }
        case RK29_PIN3_PD5:
        {
             rk29_mux_api_set(GPIO3D5_SMCADDR7_HOSTDATA7_NAME,0);
            break;	
        }
        case RK29_PIN3_PD6:
        {
             rk29_mux_api_set(GPIO3D6_SMCADDR8_HOSTDATA8_NAME,0);
            break;	
        }
        case RK29_PIN3_PD7:
        {
             rk29_mux_api_set(GPIO3D7_SMCADDR9_HOSTDATA9_NAME,0);
            break;	
        }
        case RK29_PIN4_PA0:
        case RK29_PIN4_PA1:
        case RK29_PIN4_PA2:
        case RK29_PIN4_PA3:
        case RK29_PIN4_PA4:
        {            
            break;	
        }
        case RK29_PIN4_PA5:
        {
             rk29_mux_api_set(GPIO4A5_OTG0DRVVBUS_NAME,0);
            break;	
        }
        case RK29_PIN4_PA6:
        {
             rk29_mux_api_set(GPIO4A6_OTG1DRVVBUS_NAME,0);
            break;	
        }
        case RK29_PIN4_PA7:
        {
             rk29_mux_api_set(GPIO4A7_SPDIFTX_NAME,0);
            break;	
        }
        case RK29_PIN4_PB0:
        {
             rk29_mux_api_set(GPIO4B0_FLASHDATA8_NAME,0);
            break;	
        }
        case RK29_PIN4_PB1:
        {
             rk29_mux_api_set(GPIO4B1_FLASHDATA9_NAME,0);
            break;	
        }
        case RK29_PIN4_PB2:
        {
             rk29_mux_api_set(GPIO4B2_FLASHDATA10_NAME,0);
            break;	
        }
        case RK29_PIN4_PB3:
        {
             rk29_mux_api_set(GPIO4B3_FLASHDATA11_NAME,0);
            break;	
        }
        case RK29_PIN4_PB4:
        {
             rk29_mux_api_set(GPIO4B4_FLASHDATA12_NAME,0);
            break;	
        }
        case RK29_PIN4_PB5:
        {
             rk29_mux_api_set(GPIO4B5_FLASHDATA13_NAME,0);
            break;	
        }
        case RK29_PIN4_PB6:
        {
             rk29_mux_api_set(GPIO4B6_FLASHDATA14_NAME ,0);
            break;	
        }
        case RK29_PIN4_PB7:
        {
             rk29_mux_api_set(GPIO4B7_FLASHDATA15_NAME,0);
            break;	
        }
        case RK29_PIN4_PC0:
        {
             rk29_mux_api_set(GPIO4C0_RMIICLKOUT_RMIICLKIN_NAME,0);
            break;	
        }
        case RK29_PIN4_PC1:
        {
             rk29_mux_api_set(GPIO4C1_RMIITXEN_MIITXEN_NAME,0);
            break;	
        }
        case RK29_PIN4_PC2:
        {
             rk29_mux_api_set(GPIO4C2_RMIITXD1_MIITXD1_NAME,0);
            break;	
        }
        case RK29_PIN4_PC3:
        {
             rk29_mux_api_set(GPIO4C3_RMIITXD0_MIITXD0_NAME,0);
            break;	
        }
        case RK29_PIN4_PC4:
        {
             rk29_mux_api_set(GPIO4C4_RMIIRXERR_MIIRXERR_NAME,0);
            break;	
        }
        case RK29_PIN4_PC5:
        {
             rk29_mux_api_set(GPIO4C5_RMIICSRDVALID_MIIRXDVALID_NAME,0);
            break;	
        }
        case RK29_PIN4_PC6:
        {
             rk29_mux_api_set(GPIO4C6_RMIIRXD1_MIIRXD1_NAME,0);
            break;	
        }

        case RK29_PIN4_PC7:
        {
             rk29_mux_api_set(GPIO4C7_RMIIRXD0_MIIRXD0_NAME,0);
            break;	
        }
        case RK29_PIN4_PD0:
        case RK29_PIN4_PD1:
        {
             rk29_mux_api_set(GPIO4D10_CPUTRACEDATA10_NAME,0);             
            break;	
        }
        case RK29_PIN4_PD2:
        case RK29_PIN4_PD3:
        {
             rk29_mux_api_set(GPIO4D32_CPUTRACEDATA32_NAME,0);           
            break;	
        }
        case RK29_PIN4_PD4:
        {
             rk29_mux_api_set(GPIO4D4_CPUTRACECLK_NAME,0);
            break;	
        }
        case RK29_PIN4_PD5:
        {
             rk29_mux_api_set(GPIO4D5_CPUTRACECTL_NAME,0);
            break;	
        }
        case RK29_PIN4_PD6:
        {
             rk29_mux_api_set(GPIO4D6_I2S0LRCKTX0_NAME,0);
            break;	
        }
        case RK29_PIN4_PD7:
        {
             rk29_mux_api_set(GPIO4D7_I2S0LRCKTX1_NAME,0);
            break;	
        } 
        case RK29_PIN5_PA0:
        case RK29_PIN5_PA1:
        case RK29_PIN5_PA2:
        {      
            break;	
        }
        case RK29_PIN5_PA3:
        {
             rk29_mux_api_set(GPIO5A3_MIITXCLKIN_NAME,0);
            break;	
        }
        case RK29_PIN5_PA4:
        {
             rk29_mux_api_set(GPIO5A4_TSSYNC_NAME,0);
            break;	
        }
        case RK29_PIN5_PA5:
        {
             rk29_mux_api_set(GPIO5A5_HSADCDATA0_NAME,0);
            break;	
        }
        case RK29_PIN5_PA6:
        {
             rk29_mux_api_set(GPIO5A6_HSADCDATA1_NAME,0);
            break;	
        }
        case RK29_PIN5_PA7:
        {
             rk29_mux_api_set(GPIO5A7_HSADCDATA2_NAME,0);
            break;	
        }
        case RK29_PIN5_PB0:
        {
             rk29_mux_api_set(GPIO5B0_HSADCDATA3_NAME,0);
            break;	
        }
        case RK29_PIN5_PB1:
        {
             rk29_mux_api_set(GPIO5B1_HSADCDATA4_NAME,0);
            break;	
        }
        case RK29_PIN5_PB2:
        {
             rk29_mux_api_set(GPIO5B2_HSADCDATA5_NAME,0);
            break;	
        }
        case RK29_PIN5_PB3:
        {
             rk29_mux_api_set(GPIO5B3_HSADCDATA6_NAME,0);
            break;	
        }
        case RK29_PIN5_PB4:
        {
             rk29_mux_api_set(GPIO5B4_HSADCDATA7_NAME,0);
            break;	
        }
        case RK29_PIN5_PB5:
        {
             rk29_mux_api_set(GPIO5B5_HSADCDATA8_NAME,0);
            break;	
        }
        case RK29_PIN5_PB6:
        {
             rk29_mux_api_set(GPIO5B6_HSADCDATA9_NAME,0);
            break;	
        }
        case RK29_PIN5_PB7:
        {
             rk29_mux_api_set(GPIO5B7_HSADCCLKOUTGPSCLK_NAME,0);
            break;	
        }
        case RK29_PIN5_PC0:
        {
             rk29_mux_api_set(GPIO5C0_EBCSDDO0_SMCDATA0_NAME,0);
            break;	
        }
        case RK29_PIN5_PC1:
        {
             rk29_mux_api_set(GPIO5C1_EBCSDDO1_SMCDATA1_NAME,0);
            break;	
        }
        case RK29_PIN5_PC2:
        {
             rk29_mux_api_set(GPIO5C2_EBCSDDO2_SMCDATA2_NAME,0);
            break;	
        }
        case RK29_PIN5_PC3:
        {
             rk29_mux_api_set(GPIO5C3_EBCSDDO3_SMCDATA3_NAME,0);
            break;	
        }
        case RK29_PIN5_PC4:
        {
             rk29_mux_api_set(GPIO5C4_EBCSDDO4_SMCDATA4_NAME,0);
            break;	
        }
        case RK29_PIN5_PC5:
        {
             rk29_mux_api_set(GPIO5C5_EBCSDDO5_SMCDATA5_NAME,0);
            break;	
        }
        case RK29_PIN5_PC6:
        {
             rk29_mux_api_set(GPIO5C6_EBCSDDO6_SMCDATA6_NAME,0);
            break;	
        }
        case RK29_PIN5_PC7:
        {
             rk29_mux_api_set(GPIO5C7_EBCSDDO7_SMCDATA7_NAME,0);
            break;	
        }
        case RK29_PIN5_PD0:
        {
             rk29_mux_api_set(GPIO5D0_EBCSDLE_SMCADDR5_HOSTDATA5_NAME,0);
            break;	
        }
        case RK29_PIN5_PD1:
        {
             rk29_mux_api_set(GPIO5D1_EBCSDCLK_SMCADDR6_HOSTDATA6_NAME,0);
            break;	
        }
        case RK29_PIN5_PD2:
        {
             rk29_mux_api_set(GPIO5D2_PWM1_UART1SIRIN_NAME,0);
            break;	
        }
        case RK29_PIN5_PD3:
        {
             rk29_mux_api_set(GPIO5D3_I2C2SDA_NAME,0);
            break;	
        }
        case RK29_PIN5_PD4:
        {
             rk29_mux_api_set(GPIO5D4_I2C2SCL_NAME,0);
            break;	
        }
        case RK29_PIN5_PD5:
        {
             rk29_mux_api_set(GPIO5D5_SDMMC0PWREN_NAME,0);
            break;	
        }
        case RK29_PIN5_PD6:
        {
             rk29_mux_api_set(GPIO5D6_SDMMC1PWREN_NAME,0);
            break;	
        }
        case RK29_PIN5_PD7:
        case RK29_PIN6_PA0:
        case RK29_PIN6_PA1:
        case RK29_PIN6_PA2:
        case RK29_PIN6_PA3:
        case RK29_PIN6_PA4:
        case RK29_PIN6_PA5:
        case RK29_PIN6_PA6:
        case RK29_PIN6_PA7:
        case RK29_PIN6_PB0:
        case RK29_PIN6_PB1:
        case RK29_PIN6_PB2:
        case RK29_PIN6_PB3:
        case RK29_PIN6_PB4:
        case RK29_PIN6_PB5:
        case RK29_PIN6_PB6:
        case RK29_PIN6_PB7:
        case RK29_PIN6_PC0:
        case RK29_PIN6_PC1:
        case RK29_PIN6_PC2:
        case RK29_PIN6_PC3:
        {
            break;
        }
        case RK29_PIN6_PC4:
        case RK29_PIN6_PC5:
        {
             rk29_mux_api_set(GPIO6C54_CPUTRACEDATA54_NAME,0);
            break;	
        }
        case RK29_PIN6_PC6:
        case RK29_PIN6_PC7:
        {
             rk29_mux_api_set(GPIO6C76_CPUTRACEDATA76_NAME,0);
            break;	
        }
        case RK29_PIN6_PD0:
        case RK29_PIN6_PD1:
        case RK29_PIN6_PD2:
        case RK29_PIN6_PD3:
        case RK29_PIN6_PD4:
        case RK29_PIN6_PD5:
        case RK29_PIN6_PD6:
        case RK29_PIN6_PD7:
        {
            break;	
        }    
        default:
        {
            printk("Pin=%d isn't RK29 GPIO, Please init it's iomux yourself!",pin);
            break;
        }
    }
    return 0;
}
#include "../../../arch/arm/plat-rk/rk_camera.c"

#if (CONFIG_SENSOR_IIC_ADDR_0 != 0x00)
static struct i2c_board_info rk29_i2c_cam_info_0[] = {
	{
		I2C_BOARD_INFO(SENSOR_NAME_0, CONFIG_SENSOR_IIC_ADDR_0>>1)
	},
};

static struct soc_camera_link rk29_iclink_0 = {
	.bus_id		= RK29_CAM_PLATFORM_DEV_ID,
	.power		= rk_sensor_power,
#if (CONFIG_SENSOR_RESET_PIN_0 != INVALID_GPIO)
    .reset      = rk_sensor_reset,
#endif    
	.powerdown  = rk_sensor_powerdown,
	.board_info	= &rk29_i2c_cam_info_0[0],
	.i2c_adapter_id	= CONFIG_SENSOR_IIC_ADAPTER_ID_0,
	.module_name	= SENSOR_NAME_0,
};

/*platform_device : soc-camera need  */
static struct platform_device rk29_soc_camera_pdrv_0 = {
	.name	= "soc-camera-pdrv",
	.id	= 0,
	.dev	= {
		.init_name = SENSOR_DEVICE_NAME_0,
		.platform_data = &rk29_iclink_0,
	},
};
#endif
#if (CONFIG_SENSOR_IIC_ADDR_1 != 0x00)
static struct i2c_board_info rk29_i2c_cam_info_1[] = {
	{
		I2C_BOARD_INFO(SENSOR_NAME_1, CONFIG_SENSOR_IIC_ADDR_1>>1)
	},
};

static struct soc_camera_link rk29_iclink_1 = {
	.bus_id		= RK29_CAM_PLATFORM_DEV_ID,
	.power		= rk_sensor_power,
#if (CONFIG_SENSOR_RESET_PIN_1 != INVALID_GPIO)
    .reset      = rk_sensor_reset,
#endif  	
	.powerdown  = rk_sensor_powerdown,
	.board_info	= &rk29_i2c_cam_info_1[0],
	.i2c_adapter_id	= CONFIG_SENSOR_IIC_ADAPTER_ID_1,
	.module_name	= SENSOR_NAME_1,
};

/*platform_device : soc-camera need  */
static struct platform_device rk29_soc_camera_pdrv_1 = {
	.name	= "soc-camera-pdrv",
	.id	= 1,
	.dev	= {
		.init_name = SENSOR_DEVICE_NAME_1,
		.platform_data = &rk29_iclink_1,
	},
};
#endif

static u64 rockchip_device_camera_dmamask = 0xffffffffUL;
static struct resource rk29_camera_resource[] = {
	[0] = {
		.start = RK29_VIP_PHYS,
		.end   = RK29_VIP_PHYS + RK29_VIP_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_VIP,
		.end   = IRQ_VIP,
		.flags = IORESOURCE_IRQ,
	}
};

/*platform_device : */
static struct platform_device rk29_device_camera = {
	.name		  = RK29_CAM_DRV_NAME,
	.id		  = RK29_CAM_PLATFORM_DEV_ID,               /* This is used to put cameras on this interface */
	.num_resources	  = ARRAY_SIZE(rk29_camera_resource),
	.resource	  = rk29_camera_resource,
	.dev            = {
		.dma_mask = &rockchip_device_camera_dmamask,
		.coherent_dma_mask = 0xffffffffUL,
		.platform_data  = &rk_camera_platform_data,
	}
};

static void rk_init_camera_plateform_data(void)
{
    int i,dev_idx;
    
    dev_idx = 0;
    for (i=0; i<RK_CAM_NUM; i++) {
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

static int rk_register_camera_devices(void)
{   
    int i;
    
	rk_init_camera_plateform_data();
    for (i=0; i<RK_CAM_NUM; i++) {
        if (rk_camera_platform_data.register_dev[i].device_info.name
            && !strcmp(rk_camera_platform_data.register_dev[i].device_info.dev.init_name,SENSOR_DEVICE_NAME_0)
            && !strcmp(rk_camera_platform_data.register_dev[i].device_info.dev.init_name,SENSOR_DEVICE_NAME_1)) {
            platform_device_register(&rk_camera_platform_data.register_dev[i].device_info);
        }
    }
	return 0;
}

module_init(rk_register_camera_devices);

#endif

#endif //#ifdef CONFIG_VIDEO_RK29
