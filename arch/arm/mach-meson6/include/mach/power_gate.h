#ifndef __POWER_MGR_HEADER_
#define __POWER_MGR_HEADER_

#include <mach/am_regs.h>
#include <mach/clock.h>
/* clock gate control */

#define CLK_GATE_ON(_MOD) \
    do{                     \
        if(GCLK_ref[GCLK_IDX_##_MOD]++ == 0){ \
            if (0) printk(KERN_INFO "gate on %s %x, %x\n", GCLK_NAME_##_MOD, GCLK_REG_##_MOD, GCLK_MASK_##_MOD); \
            SET_CBUS_REG_MASK(GCLK_REG_##_MOD, GCLK_MASK_##_MOD); \
        } \
    }while(0)


#define CLK_GATE_OFF(_MOD) \
    do{                             \
        if(GCLK_ref[GCLK_IDX_##_MOD] == 0)    \
            break;                  \
        if(--GCLK_ref[GCLK_IDX_##_MOD] == 0){ \
            if (0) printk(KERN_INFO "gate off %s %x, %x\n", GCLK_NAME_##_MOD, GCLK_REG_##_MOD, GCLK_MASK_##_MOD); \
            CLEAR_CBUS_REG_MASK(GCLK_REG_##_MOD, GCLK_MASK_##_MOD); \
        } \
    }while(0)

#define IS_CLK_GATE_ON(_MOD) (READ_CBUS_REG(GCLK_REG_##_MOD) & (GCLK_MASK_##_MOD))
#define GATE_INIT(_MOD) GCLK_ref[GCLK_IDX_##_MOD] = IS_CLK_GATE_ON(_MOD)?1:0

#define GCLK_IDX_DDR         0
#define GCLK_NAME_DDR      "DDR"
#define GCLK_DEV_DDR      "CLKGATE_DDR"
#define GCLK_REG_DDR      (HHI_GCLK_MPEG0)
#define GCLK_MASK_DDR      (1<<0)

#define GCLK_IDX_DOS         1
#define GCLK_NAME_DOS      "DOS"
#define GCLK_DEV_DOS      "CLKGATE_DOS"
#define GCLK_REG_DOS      (HHI_GCLK_MPEG0)
#define GCLK_MASK_DOS      (1<<1)

#define GCLK_IDX_MIPI_APB_CLK         2
#define GCLK_NAME_MIPI_APB_CLK      "MIPI_APB_CLK"
#define GCLK_DEV_MIPI_APB_CLK      "CLKGATE_MIPI_APB_CLK"
#define GCLK_REG_MIPI_APB_CLK      (HHI_GCLK_MPEG0)
#define GCLK_MASK_MIPI_APB_CLK      (1<<2)

#define GCLK_IDX_MIPI_SYS_CLK         3
#define GCLK_NAME_MIPI_SYS_CLK      "MIPI_SYS_CLK"
#define GCLK_DEV_MIPI_SYS_CLK      "CLKGATE_MIPI_SYS_CLK"
#define GCLK_REG_MIPI_SYS_CLK      (HHI_GCLK_MPEG0)
#define GCLK_MASK_MIPI_SYS_CLK      (1<<3)

#define GCLK_IDX_AHB_BRIDGE         4
#define GCLK_NAME_AHB_BRIDGE      "AHB_BRIDGE"
#define GCLK_DEV_AHB_BRIDGE      "CLKGATE_AHB_BRIDGE"
#define GCLK_REG_AHB_BRIDGE      (HHI_GCLK_MPEG0)
#define GCLK_MASK_AHB_BRIDGE      (1<<4)

#define GCLK_IDX_ISA         5
#define GCLK_NAME_ISA      "ISA"
#define GCLK_DEV_ISA      "CLKGATE_ISA"
#define GCLK_REG_ISA      (HHI_GCLK_MPEG0)
#define GCLK_MASK_ISA      (1<<5)

#define GCLK_IDX_APB_CBUS         6
#define GCLK_NAME_APB_CBUS      "APB_CBUS"
#define GCLK_DEV_APB_CBUS      "CLKGATE_APB_CBUS"
#define GCLK_REG_APB_CBUS      (HHI_GCLK_MPEG0)
#define GCLK_MASK_APB_CBUS      (1<<6)

#define GCLK_IDX__1200XXX       7
#define GCLK_NAME__1200XXX      "_1200XXX"
#define GCLK_DEV__1200XXX      "CLKGATE__1200XXX"
#define GCLK_REG__1200XXX      (HHI_GCLK_MPEG0)
#define GCLK_MASK__1200XXX      (1<<7)

#define GCLK_IDX_SPICC         8
#define GCLK_NAME_SPICC      "SPICC"
#define GCLK_DEV_SPICC      "CLKGATE_SPICC"
#define GCLK_REG_SPICC      (HHI_GCLK_MPEG0)
#define GCLK_MASK_SPICC      (1<<8)

#define GCLK_IDX_I2C         9
#define GCLK_NAME_I2C      "I2C"
#define GCLK_DEV_I2C      "CLKGATE_I2C"
#define GCLK_REG_I2C      (HHI_GCLK_MPEG0)
#define GCLK_MASK_I2C      (1<<9)

#define GCLK_IDX_SAR_ADC         10
#define GCLK_NAME_SAR_ADC      "SAR_ADC"
#define GCLK_DEV_SAR_ADC      "CLKGATE_SAR_ADC"
#define GCLK_REG_SAR_ADC      (HHI_GCLK_MPEG0)
#define GCLK_MASK_SAR_ADC      (1<<10)

#define GCLK_IDX_SMART_CARD_MPEG_DOMAIN         11
#define GCLK_NAME_SMART_CARD_MPEG_DOMAIN      "SMART_CARD_MPEG_DOMAIN"
#define GCLK_DEV_SMART_CARD_MPEG_DOMAIN      "CLKGATE_SMART_CARD_MPEG_DOMAIN"
#define GCLK_REG_SMART_CARD_MPEG_DOMAIN      (HHI_GCLK_MPEG0)
#define GCLK_MASK_SMART_CARD_MPEG_DOMAIN      (1<<11)

#define GCLK_IDX_RANDOM_NUM_GEN         12
#define GCLK_NAME_RANDOM_NUM_GEN      "RANDOM_NUM_GEN"
#define GCLK_DEV_RANDOM_NUM_GEN      "CLKGATE_RANDOM_NUM_GEN"
#define GCLK_REG_RANDOM_NUM_GEN      (HHI_GCLK_MPEG0)
#define GCLK_MASK_RANDOM_NUM_GEN      (1<<12)

#define GCLK_IDX_UART0         13
#define GCLK_NAME_UART0      "UART0"
#define GCLK_DEV_UART0      "CLKGATE_UART0"
#define GCLK_REG_UART0      (HHI_GCLK_MPEG0)
#define GCLK_MASK_UART0      (1<<13)

#define GCLK_IDX_SDHC         14
#define GCLK_NAME_SDHC      "SDHC"
#define GCLK_DEV_SDHC      "CLKGATE_SDHC"
#define GCLK_REG_SDHC      (HHI_GCLK_MPEG0)
#define GCLK_MASK_SDHC      (1<<14)

#define GCLK_IDX_STREAM         15
#define GCLK_NAME_STREAM      "STREAM"
#define GCLK_DEV_STREAM      "CLKGATE_STREAM"
#define GCLK_REG_STREAM      (HHI_GCLK_MPEG0)
#define GCLK_MASK_STREAM      (1<<15)

#define GCLK_IDX_ASYNC_FIFO         16
#define GCLK_NAME_ASYNC_FIFO      "ASYNC_FIFO"
#define GCLK_DEV_ASYNC_FIFO      "CLKGATE_ASYNC_FIFO"
#define GCLK_REG_ASYNC_FIFO      (HHI_GCLK_MPEG0)
#define GCLK_MASK_ASYNC_FIFO      (1<<16)

#define GCLK_IDX_SDIO         17
#define GCLK_NAME_SDIO      "SDIO"
#define GCLK_DEV_SDIO      "CLKGATE_SDIO"
#define GCLK_REG_SDIO      (HHI_GCLK_MPEG0)
#define GCLK_MASK_SDIO      (1<<17)

#define GCLK_IDX_AUD_BUF         18
#define GCLK_NAME_AUD_BUF      "AUD_BUF"
#define GCLK_DEV_AUD_BUF      "CLKGATE_AUD_BUF"
#define GCLK_REG_AUD_BUF      (HHI_GCLK_MPEG0)
#define GCLK_MASK_AUD_BUF      (1<<18)

#define GCLK_IDX_HIU_PARSER         19
#define GCLK_NAME_HIU_PARSER      "HIU_PARSER"
#define GCLK_DEV_HIU_PARSER      "CLKGATE_HIU_PARSER"
#define GCLK_REG_HIU_PARSER      (HHI_GCLK_MPEG0)
#define GCLK_MASK_HIU_PARSER      (1<<19)

#define GCLK_IDX_RESERVED0         20
#define GCLK_NAME_RESERVED0      "RESERVED0"
#define GCLK_DEV_RESERVED0      "CLKGATE_RESERVED0"
#define GCLK_REG_RESERVED0      (HHI_GCLK_MPEG0)
#define GCLK_MASK_RESERVED0      (1<<20)

#define GCLK_IDX_RESERVED1         21
#define GCLK_NAME_RESERVED1      "RESERVED1"
#define GCLK_DEV_RESERVED1      "CLKGATE_RESERVED1"
#define GCLK_REG_RESERVED1      (HHI_GCLK_MPEG0)
#define GCLK_MASK_RESERVED1      (1<<21)

#define GCLK_IDX_BT656_IN         22
#define GCLK_NAME_BT656_IN      "BT656_IN"
#define GCLK_DEV_BT656_IN      "CLKGATE_BT656_IN"
#define GCLK_REG_BT656_IN      (HHI_GCLK_MPEG0)
#define GCLK_MASK_BT656_IN      (1<<22)

#define GCLK_IDX_ASSIST_MISC         23
#define GCLK_NAME_ASSIST_MISC      "ASSIST_MISC"
#define GCLK_DEV_ASSIST_MISC      "CLKGATE_ASSIST_MISC"
#define GCLK_REG_ASSIST_MISC      (HHI_GCLK_MPEG0)
#define GCLK_MASK_ASSIST_MISC      (1<<23)

#define GCLK_IDX_VENC_I_TOP         24
#define GCLK_NAME_VENC_I_TOP      "VENC_I_TOP"
#define GCLK_DEV_VENC_I_TOP      "CLKGATE_VENC_I_TOP"
#define GCLK_REG_VENC_I_TOP      (HHI_GCLK_MPEG0)
#define GCLK_MASK_VENC_I_TOP      (1<<24)

#define GCLK_IDX_VENC_P_TOP         25
#define GCLK_NAME_VENC_P_TOP      "VENC_P_TOP"
#define GCLK_DEV_VENC_P_TOP      "CLKGATE_VENC_P_TOP"
#define GCLK_REG_VENC_P_TOP      (HHI_GCLK_MPEG0)
#define GCLK_MASK_VENC_P_TOP      (1<<25)

#define GCLK_IDX_VENC_T_TOP         26
#define GCLK_NAME_VENC_T_TOP      "VENC_T_TOP"
#define GCLK_DEV_VENC_T_TOP      "CLKGATE_VENC_T_TOP"
#define GCLK_REG_VENC_T_TOP      (HHI_GCLK_MPEG0)
#define GCLK_MASK_VENC_T_TOP      (1<<26)

#define GCLK_IDX_VENC_DAC         27
#define GCLK_NAME_VENC_DAC      "VENC_DAC"
#define GCLK_DEV_VENC_DAC      "CLKGATE_VENC_DAC"
#define GCLK_REG_VENC_DAC      (HHI_GCLK_MPEG0)
#define GCLK_MASK_VENC_DAC      (1<<27)

#define GCLK_IDX_VI_CORE         28
#define GCLK_NAME_VI_CORE      "VI_CORE"
#define GCLK_DEV_VI_CORE      "CLKGATE_VI_CORE"
#define GCLK_REG_VI_CORE      (HHI_GCLK_MPEG0)
#define GCLK_MASK_VI_CORE      (1<<28)

#define GCLK_IDX_RESERVED2         29
#define GCLK_NAME_RESERVED2      "RESERVED2"
#define GCLK_DEV_RESERVED2      "CLKGATE_RESERVED2"
#define GCLK_REG_RESERVED2      (HHI_GCLK_MPEG0)
#define GCLK_MASK_RESERVED2      (1<<29)

#define GCLK_IDX_SPI2         30
#define GCLK_NAME_SPI2      "SPI2"
#define GCLK_DEV_SPI2      "CLKGATE_SPI2"
#define GCLK_REG_SPI2      (HHI_GCLK_MPEG0)
#define GCLK_MASK_SPI2      (1<<30)

#define GCLK_IDX_RESERVED3         31
#define GCLK_NAME_RESERVED3      "RESERVED3"
#define GCLK_DEV_RESERVED3      "CLKGATE_RESERVED3"
#define GCLK_REG_RESERVED3      (HHI_GCLK_MPEG0)
#define GCLK_MASK_RESERVED3      (1<<31)

#define GCLK_IDX_RESERVED4         32
#define GCLK_NAME_RESERVED4      "RESERVED4"
#define GCLK_DEV_RESERVED4      "CLKGATE_RESERVED4"
#define GCLK_REG_RESERVED4      (HHI_GCLK_MPEG1)
#define GCLK_MASK_RESERVED4      (1<<0)

#define GCLK_IDX_SPI1         33
#define GCLK_NAME_SPI1      "SPI1"
#define GCLK_DEV_SPI1      "CLKGATE_SPI1"
#define GCLK_REG_SPI1      (HHI_GCLK_MPEG1)
#define GCLK_MASK_SPI1      (1<<1)

#define GCLK_IDX_AUD_IN         34
#define GCLK_NAME_AUD_IN      "AUD_IN"
#define GCLK_DEV_AUD_IN      "CLKGATE_AUD_IN"
#define GCLK_REG_AUD_IN      (HHI_GCLK_MPEG1)
#define GCLK_MASK_AUD_IN      (1<<2)

#define GCLK_IDX_ETHERNET         35
#define GCLK_NAME_ETHERNET      "ETHERNET"
#define GCLK_DEV_ETHERNET      "CLKGATE_ETHERNET"
#define GCLK_REG_ETHERNET      (HHI_GCLK_MPEG1)
#define GCLK_MASK_ETHERNET      (1<<3)

#define GCLK_IDX_DEMUX         36
#define GCLK_NAME_DEMUX      "DEMUX"
#define GCLK_DEV_DEMUX      "CLKGATE_DEMUX"
#define GCLK_REG_DEMUX      (HHI_GCLK_MPEG1)
#define GCLK_MASK_DEMUX      (1<<4)

#define GCLK_IDX_RESERVED5         37
#define GCLK_NAME_RESERVED5      "RESERVED5"
#define GCLK_DEV_RESERVED5      "CLKGATE_RESERVED5"
#define GCLK_REG_RESERVED5      (HHI_GCLK_MPEG0)
#define GCLK_MASK_RESERVED5      (1<<5)

#define GCLK_IDX_AIU_AI_TOP_GLUE         38
#define GCLK_NAME_AIU_AI_TOP_GLUE      "AIU_AI_TOP_GLUE"
#define GCLK_DEV_AIU_AI_TOP_GLUE      "CLKGATE_AIU_AI_TOP_GLUE"
#define GCLK_REG_AIU_AI_TOP_GLUE      (HHI_GCLK_MPEG1)
#define GCLK_MASK_AIU_AI_TOP_GLUE      (1<<6)

#define GCLK_IDX_AIU_IEC958         39
#define GCLK_NAME_AIU_IEC958      "AIU_IEC958"
#define GCLK_DEV_AIU_IEC958      "CLKGATE_AIU_IEC958"
#define GCLK_REG_AIU_IEC958      (HHI_GCLK_MPEG1)
#define GCLK_MASK_AIU_IEC958      (1<<7)

#define GCLK_IDX_AIU_I2S_OUT         40
#define GCLK_NAME_AIU_I2S_OUT      "AIU_I2S_OUT"
#define GCLK_DEV_AIU_I2S_OUT      "CLKGATE_AIU_I2S_OUT"
#define GCLK_REG_AIU_I2S_OUT      (HHI_GCLK_MPEG1)
#define GCLK_MASK_AIU_I2S_OUT      (1<<8)

#define GCLK_IDX_AIU_AMCLK_MEASURE         41
#define GCLK_NAME_AIU_AMCLK_MEASURE      "AIU_AMCLK_MEASURE"
#define GCLK_DEV_AIU_AMCLK_MEASURE      "CLKGATE_AIU_AMCLK_MEASURE"
#define GCLK_REG_AIU_AMCLK_MEASURE      (HHI_GCLK_MPEG1)
#define GCLK_MASK_AIU_AMCLK_MEASURE      (1<<9)

#define GCLK_IDX_AIU_AIFIFO2         42
#define GCLK_NAME_AIU_AIFIFO2      "AIU_AIFIFO2"
#define GCLK_DEV_AIU_AIFIFO2      "CLKGATE_AIU_AIFIFO2"
#define GCLK_REG_AIU_AIFIFO2      (HHI_GCLK_MPEG1)
#define GCLK_MASK_AIU_AIFIFO2      (1<<10)

#define GCLK_IDX_AIU_AUD_MIXER         43
#define GCLK_NAME_AIU_AUD_MIXER      "AIU_AUD_MIXER"
#define GCLK_DEV_AIU_AUD_MIXER      "CLKGATE_AIU_AUD_MIXER"
#define GCLK_REG_AIU_AUD_MIXER      (HHI_GCLK_MPEG1)
#define GCLK_MASK_AIU_AUD_MIXER      (1<<11)

#define GCLK_IDX_AIU_MIXER_REG         44
#define GCLK_NAME_AIU_MIXER_REG      "AIU_MIXER_REG"
#define GCLK_DEV_AIU_MIXER_REG      "CLKGATE_AIU_MIXER_REG"
#define GCLK_REG_AIU_MIXER_REG      (HHI_GCLK_MPEG1)
#define GCLK_MASK_AIU_MIXER_REG      (1<<12)

#define GCLK_IDX_AIU_ADC         45
#define GCLK_NAME_AIU_ADC      "AIU_ADC"
#define GCLK_DEV_AIU_ADC      "CLKGATE_AIU_ADC"
#define GCLK_REG_AIU_ADC      (HHI_GCLK_MPEG1)
#define GCLK_MASK_AIU_ADC      (1<<13)

#define GCLK_IDX_BLK_MOV         46
#define GCLK_NAME_BLK_MOV      "BLK_MOV"
#define GCLK_DEV_BLK_MOV      "CLKGATE_BLK_MOV"
#define GCLK_REG_BLK_MOV      (HHI_GCLK_MPEG1)
#define GCLK_MASK_BLK_MOV      (1<<14)

#define GCLK_IDX_RESERVED6         47
#define GCLK_NAME_RESERVED6      "RESERVED6"
#define GCLK_DEV_RESERVED6      "CLKGATE_RESERVED6"
#define GCLK_REG_RESERVED6      (HHI_GCLK_MPEG0)
#define GCLK_MASK_RESERVED6      (1<<15)

#define GCLK_IDX_UART1         48
#define GCLK_NAME_UART1      "UART1"
#define GCLK_DEV_UART1      "CLKGATE_UART1"
#define GCLK_REG_UART1      (HHI_GCLK_MPEG1)
#define GCLK_MASK_UART1      (1<<16)

#define GCLK_IDX_LED_PWM         49
#define GCLK_NAME_LED_PWM      "LED_PWM"
#define GCLK_DEV_LED_PWM      "CLKGATE_LED_PWM"
#define GCLK_REG_LED_PWM      (HHI_GCLK_MPEG1)
#define GCLK_MASK_LED_PWM      (1<<17)

#define GCLK_IDX_VGHL_PWM         50
#define GCLK_NAME_VGHL_PWM      "VGHL_PWM"
#define GCLK_DEV_VGHL_PWM      "CLKGATE_VGHL_PWM"
#define GCLK_REG_VGHL_PWM      (HHI_GCLK_MPEG1)
#define GCLK_MASK_VGHL_PWM      (1<<18)

#define GCLK_IDX_RESERVED7         51
#define GCLK_NAME_RESERVED7      "RESERVED7"
#define GCLK_DEV_RESERVED7      "CLKGATE_RESERVED7"
#define GCLK_REG_RESERVED7      (HHI_GCLK_MPEG1)
#define GCLK_MASK_RESERVED7      (1<<19)

#define GCLK_IDX_GE2D         52
#define GCLK_NAME_GE2D      "GE2D"
#define GCLK_DEV_GE2D      "CLKGATE_GE2D"
#define GCLK_REG_GE2D      (HHI_GCLK_MPEG1)
#define GCLK_MASK_GE2D      (1<<20)

#define GCLK_IDX_USB0         53
#define GCLK_NAME_USB0      "USB0"
#define GCLK_DEV_USB0      "CLKGATE_USB0"
#define GCLK_REG_USB0      (HHI_GCLK_MPEG1)
#define GCLK_MASK_USB0      (1<<21)

#define GCLK_IDX_USB1         54
#define GCLK_NAME_USB1      "USB1"
#define GCLK_DEV_USB1      "CLKGATE_USB1"
#define GCLK_REG_USB1      (HHI_GCLK_MPEG1)
#define GCLK_MASK_USB1      (1<<22)

#define GCLK_IDX_RESET         55
#define GCLK_NAME_RESET      "RESET"
#define GCLK_DEV_RESET      "CLKGATE_RESET"
#define GCLK_REG_RESET      (HHI_GCLK_MPEG1)
#define GCLK_MASK_RESET      (1<<23)

#define GCLK_IDX_NAND         56
#define GCLK_NAME_NAND      "NAND"
#define GCLK_DEV_NAND      "CLKGATE_NAND"
#define GCLK_REG_NAND      (HHI_GCLK_MPEG1)
#define GCLK_MASK_NAND      (1<<24)

#define GCLK_IDX_HIU_PARSER_TOP         57
#define GCLK_NAME_HIU_PARSER_TOP      "HIU_PARSER_TOP"
#define GCLK_DEV_HIU_PARSER_TOP      "CLKGATE_HIU_PARSER_TOP"
#define GCLK_REG_HIU_PARSER_TOP      (HHI_GCLK_MPEG1)
#define GCLK_MASK_HIU_PARSER_TOP      (1<<25)

#define GCLK_IDX_RESERVED8         58
#define GCLK_NAME_RESERVED8      "RESERVED8"
#define GCLK_DEV_RESERVED8      "CLKGATE_RESERVED8"
#define GCLK_REG_RESERVED8      (HHI_GCLK_MPEG1)
#define GCLK_MASK_RESERVED8      (1<<26)

#define GCLK_IDX_MIPI_PHY         59
#define GCLK_NAME_MIPI_PHY      "MIPI_PHY"
#define GCLK_DEV_MIPI_PHY      "CLKGATE_MIPI_PHY"
#define GCLK_REG_MIPI_PHY      (HHI_GCLK_MPEG1)
#define GCLK_MASK_MIPI_PHY      (1<<27)

#define GCLK_IDX_VIDEO_IN         60
#define GCLK_NAME_VIDEO_IN      "VIDEO_IN"
#define GCLK_DEV_VIDEO_IN      "CLKGATE_VIDEO_IN"
#define GCLK_REG_VIDEO_IN      (HHI_GCLK_MPEG1)
#define GCLK_MASK_VIDEO_IN      (1<<28)

#define GCLK_IDX_AHB_ARB0         61
#define GCLK_NAME_AHB_ARB0      "AHB_ARB0"
#define GCLK_DEV_AHB_ARB0      "CLKGATE_AHB_ARB0"
#define GCLK_REG_AHB_ARB0      (HHI_GCLK_MPEG1)
#define GCLK_MASK_AHB_ARB0      (1<<29)

#define GCLK_IDX_EFUSE         62
#define GCLK_NAME_EFUSE      "EFUSE"
#define GCLK_DEV_EFUSE      "CLKGATE_EFUSE"
#define GCLK_REG_EFUSE      (HHI_GCLK_MPEG1)
#define GCLK_MASK_EFUSE      (1<<30)

#define GCLK_IDX_ROM_CLK         63
#define GCLK_NAME_ROM_CLK      "ROM_CLK"
#define GCLK_DEV_ROM_CLK      "CLKGATE_ROM_CLK"
#define GCLK_REG_ROM_CLK      (HHI_GCLK_MPEG1)
#define GCLK_MASK_ROM_CLK      (1<<31)

/**************************************************************/

#define GCLK_IDX_RESERVED9         64
#define GCLK_NAME_RESERVED9      "RESERVED9"
#define GCLK_DEV_RESERVED9      "CLKGATE_RESERVED9"
#define GCLK_REG_RESERVED9      (HHI_GCLK_MPEG2)
#define GCLK_MASK_RESERVED9      (1<<0)

#define GCLK_IDX_AHB_DATA_BUS         65
#define GCLK_NAME_AHB_DATA_BUS      "AHB_DATA_BUS"
#define GCLK_DEV_AHB_DATA_BUS      "CLKGATE_AHB_DATA_BUS"
#define GCLK_REG_AHB_DATA_BUS      (HHI_GCLK_MPEG2)
#define GCLK_MASK_AHB_DATA_BUS      (1<<1)

#define GCLK_IDX_AHB_CONTROL_BUS         66
#define GCLK_NAME_AHB_CONTROL_BUS      "AHB_CONTROL_BUS"
#define GCLK_DEV_AHB_CONTROL_BUS      "CLKGATE_AHB_CONTROL_BUS"
#define GCLK_REG_AHB_CONTROL_BUS      (HHI_GCLK_MPEG2)
#define GCLK_MASK_AHB_CONTROL_BUS      (1<<2)

#define GCLK_IDX_HDMI_INTR_SYNC         67
#define GCLK_NAME_HDMI_INTR_SYNC      "HDMI_INTR_SYNC"
#define GCLK_DEV_HDMI_INTR_SYNC      "CLKGATE_HDMI_INTR_SYNC"
#define GCLK_REG_HDMI_INTR_SYNC      (HHI_GCLK_MPEG2)
#define GCLK_MASK_HDMI_INTR_SYNC      (1<<3)

#define GCLK_IDX_HDMI_PCLK         68
#define GCLK_NAME_HDMI_PCLK      "HDMI_PCLK"
#define GCLK_DEV_HDMI_PCLK      "CLKGATE_HDMI_PCLK"
#define GCLK_REG_HDMI_PCLK      (HHI_GCLK_MPEG2)
#define GCLK_MASK_HDMI_PCLK      (1<<4)

#define GCLK_IDX_RESERVED10         69
#define GCLK_NAME_RESERVED10      "RESERVED10"
#define GCLK_DEV_RESERVED10      "CLKGATE_RESERVED10"
#define GCLK_REG_RESERVED10      (HHI_GCLK_MPEG2)
#define GCLK_MASK_RESERVED10      (1<<5)

#define GCLK_IDX_RESERVED11         70
#define GCLK_NAME_RESERVED11      "RESERVED11"
#define GCLK_DEV_RESERVED11      "CLKGATE_RESERVED11"
#define GCLK_REG_RESERVED11      (HHI_GCLK_MPEG2)
#define GCLK_MASK_RESERVED11      (1<<6)

#define GCLK_IDX_RESERVED12         71
#define GCLK_NAME_RESERVED12      "RESERVED12"
#define GCLK_DEV_RESERVED12      "CLKGATE_RESERVED12"
#define GCLK_REG_RESERVED12      (HHI_GCLK_MPEG2)
#define GCLK_MASK_RESERVED12      (1<<7)

#define GCLK_IDX_MISC_USB1_TO_DDR         72
#define GCLK_NAME_MISC_USB1_TO_DDR      "MISC_USB1_TO_DDR"
#define GCLK_DEV_MISC_USB1_TO_DDR      "CLKGATE_MISC_USB1_TO_DDR"
#define GCLK_REG_MISC_USB1_TO_DDR      (HHI_GCLK_MPEG2)
#define GCLK_MASK_MISC_USB1_TO_DDR      (1<<8)

#define GCLK_IDX_MISC_USB0_TO_DDR         73
#define GCLK_NAME_MISC_USB0_TO_DDR      "MISC_USB0_TO_DDR"
#define GCLK_DEV_MISC_USB0_TO_DDR      "CLKGATE_MISC_USB0_TO_DDR"
#define GCLK_REG_MISC_USB0_TO_DDR      (HHI_GCLK_MPEG2)
#define GCLK_MASK_MISC_USB0_TO_DDR      (1<<9)

#define GCLK_IDX_RESERVED13        74
#define GCLK_NAME_RESERVED13      "RESERVED13"
#define GCLK_DEV_RESERVED13      "CLKGATE_RESERVED13"
#define GCLK_REG_RESERVED13      (HHI_GCLK_MPEG2)
#define GCLK_MASK_RESERVED13      (1<<10)

#define GCLK_IDX_MMC_PCLK         75
#define GCLK_NAME_MMC_PCLK      "MMC_PCLK"
#define GCLK_DEV_MMC_PCLK      "CLKGATE_MMC_PCLK"
#define GCLK_REG_MMC_PCLK      (HHI_GCLK_MPEG2)
#define GCLK_MASK_MMC_PCLK      (1<<11)

#define GCLK_IDX_MISC_DVIN         76
#define GCLK_NAME_MISC_DVIN      "MISC_DVIN"
#define GCLK_DEV_MISC_DVIN      "CLKGATE_MISC_DVIN"
#define GCLK_REG_MISC_DVIN      (HHI_GCLK_MPEG2)
#define GCLK_MASK_MISC_DVIN      (1<<12)

#define GCLK_IDX_MISC_RDMA         77
#define GCLK_NAME_MISC_RDMA      "MISC_RDMA"
#define GCLK_DEV_MISC_RDMA      "CLKGATE_MISC_RDMA"
#define GCLK_REG_MISC_RDMA      (HHI_GCLK_MPEG2)
#define GCLK_MASK_MISC_RDMA      (1<<13)

#define GCLK_IDX_RESERVED14         78
#define GCLK_NAME_RESERVED14      "RESERVED14"
#define GCLK_DEV_RESERVED14      "CLKGATE_RESERVED14"
#define GCLK_REG_RESERVED14      (HHI_GCLK_MPEG2)
#define GCLK_MASK_RESERVED14      (1<<14)

#define GCLK_IDX_UART2         79
#define GCLK_NAME_UART2      "UART2"
#define GCLK_DEV_UART2      "CLKGATE_UART2"
#define GCLK_REG_UART2      (HHI_GCLK_MPEG2)
#define GCLK_MASK_UART2      (1<<15)

#define GCLK_IDX_VENCI_INT         80
#define GCLK_NAME_VENCI_INT      "VENCI_INT"
#define GCLK_DEV_VENCI_INT      "CLKGATE_VENCI_INT"
#define GCLK_REG_VENCI_INT      (HHI_GCLK_MPEG2)
#define GCLK_MASK_VENCI_INT      (1<<16)

#define GCLK_IDX_VIU2         81
#define GCLK_NAME_VIU2      "VIU2"
#define GCLK_DEV_VIU2      "CLKGATE_VIU2"
#define GCLK_REG_VIU2      (HHI_GCLK_MPEG2)
#define GCLK_MASK_VIU2      (1<<17)

#define GCLK_IDX_VENCP_INT         82
#define GCLK_NAME_VENCP_INT      "VENCP_INT"
#define GCLK_DEV_VENCP_INT      "CLKGATE_VENCP_INT"
#define GCLK_REG_VENCP_INT      (HHI_GCLK_MPEG2)
#define GCLK_MASK_VENCP_INT      (1<<18)

#define GCLK_IDX_VENCT_INT         83
#define GCLK_NAME_VENCT_INT      "VENCT_INT"
#define GCLK_DEV_VENCT_INT      "CLKGATE_VENCT_INT"
#define GCLK_REG_VENCT_INT      (HHI_GCLK_MPEG2)
#define GCLK_MASK_VENCT_INT      (1<<19)

#define GCLK_IDX_VENCL_INT         84
#define GCLK_NAME_VENCL_INT      "VENCL_INT"
#define GCLK_DEV_VENCL_INT      "CLKGATE_VENCL_INT"
#define GCLK_REG_VENCL_INT      (HHI_GCLK_MPEG2)
#define GCLK_MASK_VENCL_INT      (1<<20)

#define GCLK_IDX_VENC_L_TOP         85
#define GCLK_NAME_VENC_L_TOP      "VENC_L_TOP"
#define GCLK_DEV_VENC_L_TOP      "CLKGATE_VENC_L_TOP"
#define GCLK_REG_VENC_L_TOP      (HHI_GCLK_MPEG2)
#define GCLK_MASK_VENC_L_TOP      (1<<21)

#define GCLK_IDX_UART3        86
#define GCLK_NAME_UART3      "UART3"
#define GCLK_DEV_UART3      "CLKGATE_UART3"
#define GCLK_REG_UART3      (HHI_GCLK_MPEG2)
#define GCLK_MASK_UART3      (1<<22)

/**************************************************************/

#define GCLK_IDX_VCLK2_VENCI         87
#define GCLK_NAME_VCLK2_VENCI      "VCLK2_VENCI"
#define GCLK_DEV_VCLK2_VENCI      "CLKGATE_VCLK2_VENCI"
#define GCLK_REG_VCLK2_VENCI      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_VENCI      (1<<1)

#define GCLK_IDX_VCLK2_VENCI1         88
#define GCLK_NAME_VCLK2_VENCI1      "VCLK2_VENCI1"
#define GCLK_DEV_VCLK2_VENCI1      "CLKGATE_VCLK2_VENCI1"
#define GCLK_REG_VCLK2_VENCI1      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_VENCI1      (1<<2)

#define GCLK_IDX_VCLK2_VENCP         89
#define GCLK_NAME_VCLK2_VENCP      "VCLK2_VENCP"
#define GCLK_DEV_VCLK2_VENCP      "CLKGATE_VCLK2_VENCP"
#define GCLK_REG_VCLK2_VENCP      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_VENCP      (1<<3)

#define GCLK_IDX_VCLK2_VENCP1         90
#define GCLK_NAME_VCLK2_VENCP1      "VCLK2_VENCP1"
#define GCLK_DEV_VCLK2_VENCP1      "CLKGATE_VCLK2_VENCP1"
#define GCLK_REG_VCLK2_VENCP1      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_VENCP1      (1<<4)

#define GCLK_IDX_VCLK2_VENCT         91
#define GCLK_NAME_VCLK2_VENCT      "VCLK2_VENCT"
#define GCLK_DEV_VCLK2_VENCT      "CLKGATE_VCLK2_VENCT"
#define GCLK_REG_VCLK2_VENCT      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_VENCT      (1<<5)

#define GCLK_IDX_VCLK2_VENCT1         92
#define GCLK_NAME_VCLK2_VENCT1      "VCLK2_VENCT1"
#define GCLK_DEV_VCLK2_VENCT1      "CLKGATE_VCLK2_VENCT1"
#define GCLK_REG_VCLK2_VENCT1      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_VENCT1      (1<<6)

#define GCLK_IDX_VCLK2_OTHER         93
#define GCLK_NAME_VCLK2_OTHER      "VCLK2_OTHER"
#define GCLK_DEV_VCLK2_OTHER      "CLKGATE_VCLK2_OTHER"
#define GCLK_REG_VCLK2_OTHER      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_OTHER      (1<<7)

#define GCLK_IDX_VCLK2_ENCI         94
#define GCLK_NAME_VCLK2_ENCI      "VCLK2_ENCI"
#define GCLK_DEV_VCLK2_ENCI      "CLKGATE_VCLK2_ENCI"
#define GCLK_REG_VCLK2_ENCI      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_ENCI      (1<<8)

#define GCLK_IDX_VCLK2_ENCP         95
#define GCLK_NAME_VCLK2_ENCP      "VCLK2_ENCP"
#define GCLK_DEV_VCLK2_ENCP      "CLKGATE_VCLK2_ENCP"
#define GCLK_REG_VCLK2_ENCP      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_ENCP      (1<<9)

#define GCLK_IDX_DAC_CLK         96
#define GCLK_NAME_DAC_CLK      "DAC_CLK"
#define GCLK_DEV_DAC_CLK      "CLKGATE_DAC_CLK"
#define GCLK_REG_DAC_CLK      (HHI_GCLK_OTHER)
#define GCLK_MASK_DAC_CLK      (1<<10)

#define GCLK_IDX_AIU_AOCLK         97
#define GCLK_NAME_AIU_AOCLK      "AIU_AOCLK"
#define GCLK_DEV_AIU_AOCLK      "CLKGATE_AIU_AOCLK"
#define GCLK_REG_AIU_AOCLK      (HHI_GCLK_OTHER)
#define GCLK_MASK_AIU_AOCLK      (1<<14)

#define GCLK_IDX_AIU_AMCLK         98
#define GCLK_NAME_AIU_AMCLK      "AIU_AMCLK"
#define GCLK_DEV_AIU_AMCLK      "CLKGATE_AIU_AMCLK"
#define GCLK_REG_AIU_AMCLK      (HHI_GCLK_OTHER)
#define GCLK_MASK_AIU_AMCLK      (1<<15)

#define GCLK_IDX_AIU_ICE958_AMCLK         99
#define GCLK_NAME_AIU_ICE958_AMCLK      "AIU_ICE958_AMCLK"
#define GCLK_DEV_AIU_ICE958_AMCLK      "CLKGATE_AIU_ICE958_AMCLK"
#define GCLK_REG_AIU_ICE958_AMCLK      (HHI_GCLK_OTHER)
#define GCLK_MASK_AIU_ICE958_AMCLK      (1<<16)

#define GCLK_IDX_VCLK1_HDMI         100
#define GCLK_NAME_VCLK1_HDMI      "VCLK1_HDMI"
#define GCLK_DEV_VCLK1_HDMI      "CLKGATE_VCLK1_HDMI"
#define GCLK_REG_VCLK1_HDMI      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK1_HDMI      (1<<17)

#define GCLK_IDX_AIU_AUDIN_SCLK         101
#define GCLK_NAME_AIU_AUDIN_SCLK      "AIU_AUDIN_SCLK"
#define GCLK_DEV_AIU_AUDIN_SCLK      "CLKGATE_AIU_AUDIN_SCLK"
#define GCLK_REG_AIU_AUDIN_SCLK      (HHI_GCLK_OTHER)
#define GCLK_MASK_AIU_AUDIN_SCLK      (1<<18)

#define GCLK_IDX_ENC480P         102
#define GCLK_NAME_ENC480P      "ENC480P"
#define GCLK_DEV_ENC480P      "CLKGATE_ENC480P"
#define GCLK_REG_ENC480P      (HHI_GCLK_OTHER)
#define GCLK_MASK_ENC480P      (1<<20)

#define GCLK_IDX_VCLK2_ENCT         103
#define GCLK_NAME_VCLK2_ENCT      "VCLK2_ENCT"
#define GCLK_DEV_VCLK2_ENCT      "CLKGATE_VCLK2_ENCT"
#define GCLK_REG_VCLK2_ENCT      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_ENCT      (1<<22)

#define GCLK_IDX_VCLK2_ENCL         104
#define GCLK_NAME_VCLK2_ENCL      "VCLK2_ENCL"
#define GCLK_DEV_VCLK2_ENCL      "CLKGATE_VCLK2_ENCL"
#define GCLK_REG_VCLK2_ENCL      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_ENCL      (1<<23)

#define GCLK_IDX_MMC_CLK         105
#define GCLK_NAME_MMC_CLK      "MMC_CLK"
#define GCLK_DEV_MMC_CLK      "CLKGATE_MMC_CLK"
#define GCLK_REG_MMC_CLK      (HHI_GCLK_OTHER)
#define GCLK_MASK_MMC_CLK      (1<<24)

#define GCLK_IDX_VCLK2_VENCL         106
#define GCLK_NAME_VCLK2_VENCL      "VCLK2_VENCL"
#define GCLK_DEV_VCLK2_VENCL      "CLKGATE_VCLK2_VENCL"
#define GCLK_REG_VCLK2_VENCL      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_VENCL      (1<<25)

#define GCLK_IDX_VCLK2_OTHER1         107
#define GCLK_NAME_VCLK2_OTHER1      "VCLK2_OTHER1"
#define GCLK_DEV_VCLK2_OTHER1      "CLKGATE_VCLK2_OTHER1"
#define GCLK_REG_VCLK2_OTHER1      (HHI_GCLK_OTHER)
#define GCLK_MASK_VCLK2_OTHER1      (1<<26)

/**************************************************************/

#define GCLK_IDX_MEDIA_CPU         108
#define GCLK_NAME_MEDIA_CPU      "MEDIA_CPU"
#define GCLK_DEV_MEDIA_CPU      "CLKGATE_MEDIA_CPU"
#define GCLK_REG_MEDIA_CPU      (HHI_GCLK_AO)
#define GCLK_MASK_MEDIA_CPU      (1<<0)

#define GCLK_IDX_AHB_SRAM         109
#define GCLK_NAME_AHB_SRAM      "AHB_SRAM"
#define GCLK_DEV_AHB_SRAM      "CLKGATE_AHB_SRAM"
#define GCLK_REG_AHB_SRAM      (HHI_GCLK_AO)
#define GCLK_MASK_AHB_SRAM      (1<<1)

#define GCLK_IDX_AHB_BUS         110
#define GCLK_NAME_AHB_BUS      "AHB_BUS"
#define GCLK_DEV_AHB_BUS      "CLKGATE_AHB_BUS"
#define GCLK_REG_AHB_BUS      (HHI_GCLK_AO)
#define GCLK_MASK_AHB_BUS      (1<<2)

#define GCLK_IDX_AO_REGS         111
#define GCLK_NAME_AO_REGS      "AO_REGS"
#define GCLK_DEV_AO_REGS      "CLKGATE_AO_REGS"
#define GCLK_REG_AO_REGS      (HHI_GCLK_AO)
#define GCLK_MASK_AO_REGS      (1<<3)

#define GCLK_IDX_MAX 112
extern unsigned char GCLK_ref[GCLK_IDX_MAX];

#define REGISTER_CLK(_MOD) \
static struct clk CLK_##_MOD = {            \
    .name       = GCLK_NAME_##_MOD,             \
    .clock_index = GCLK_IDX_##_MOD,          \
    .clock_gate_reg_adr = GCLK_REG_##_MOD,  \
    .clock_gate_reg_mask = GCLK_MASK_##_MOD,    \
}

#define CLK_LOOKUP_ITEM(_MOD) \
    {           \
            .dev_id = GCLK_DEV_##_MOD, \
            .con_id = GCLK_NAME_##_MOD, \
            .clk    = &CLK_##_MOD,   \
    }



/**********************/
/* internal audio dac control */
#define ADAC_RESET                      (0x5000+0x00*4)
#define ADAC_LATCH                      (0x5000+0x01*4)
#define ADAC_POWER_CTRL_REG1            (0x5000+0x10*4)
#define ADAC_POWER_CTRL_REG2            (0x5000+0x11*4)

extern int audio_internal_dac_disable(void);

/* video dac control */
extern int  video_dac_enable(unsigned char enable_mask);

extern int  video_dac_disable(void);


#endif
