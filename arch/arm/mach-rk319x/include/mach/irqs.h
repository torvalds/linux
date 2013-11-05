#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

#define FIQ_START                       0

#define IRQ_LOCALTIMER                  29

#define IRQ_DMAC1_0                     32
#define IRQ_DMAC1_1                     33
#define IRQ_DMAC2_0                     34
#define IRQ_DMAC2_1                     35
#define IRQ_DDR_PCTL                    36
#define IRQ_GPU_PP                      37
#define IRQ_GPU_MMU                     38
#define IRQ_GPU_GP                      39
#define IRQ_RGA                         40
#define IRQ_VEPU                        41
#define IRQ_VDPU                        42
#define IRQ_VPU_MMU                     43
#define IRQ_CIF0                        44
#define IRQ_LCDC0                       45
#define IRQ_LCDC1                       46
#define IRQ_PMU                         47
#define IRQ_USB_OTG                     48
#define IRQ_USB_HOST                    49
#define IRQ_HSIC                        50
#define IRQ_GPS                         51
#define IRQ_HSADC                       52
#define IRQ_SDMMC                       53
#define IRQ_SDIO                        54
#define IRQ_EMMC                        55
#define IRQ_SDMMC_DETECT                56
#define IRQ_SDIO_DETECT                 57
#define IRQ_SARADC                      58
#define IRQ_NANDC                       59
#define IRQ_SMC                         60
#define IRQ_I2S1_2CH                    61
#define IRQ_TSADC                       62
#define IRQ_GPS_TIMER                   63
#define IRQ_SPDIF                       64
#define IRQ_UART0                       65
#define IRQ_UART1                       66
#define IRQ_UART2                       67
#define IRQ_UART3                       68
#define IRQ_SPI0                        69
#define IRQ_SPI1                        70
#define IRQ_I2C0                        71
#define IRQ_I2C1                        72
#define IRQ_I2C2                        73
#define IRQ_I2C3                        74
#define IRQ_I2C4                        75
#define IRQ_TIMER0                      76
#define IRQ_TIMER1                      77
#define IRQ_TIMER2                      78
#define IRQ_WDT                         79
#define IRQ_PWM0                        80
#define IRQ_PWM1                        81
#define IRQ_PWM2                        82
#define IRQ_PWM3                        83
#define IRQ_TIMER3                      84
#define IRQ_TIMER4                      85
#define IRQ_GPIO0                       86
#define IRQ_GPIO1                       87
#define IRQ_GPIO2                       88
#define IRQ_GPIO3                       89
#define IRQ_GPIO4                       90
#define IRQ_PERI_AHB_USB_ARBITER        91
#define IRQ_IEP                         92
#define IRQ_OTG_BVALID                  93
#define IRQ_OTG0_ID                     94
#define IRQ_OTG0_LINESTATE              95
#define IRQ_OTG1_LINESTATE              96
#define IRQ_NOC_OBSRV                   97
#define IRQ_MIPI_DSI_CONTROLLER         98
#define IRQ_HDMI                        99
#define IRQ_CRYPTO                      100
#define IRQ_ISP                         101
#define IRQ_RK_PWM                      102
#define IRQ_MAILBOX0                    103
#define IRQ_MAILBOX1                    104
#define IRQ_MAILBOX2                    105
#define IRQ_MAILBOX3                    106
#define IRQ_BB_DMA                      107
#define IRQ_BB_WDT                      108
#define IRQ_BB_I2S0                     109
#define IRQ_BB_I2S1                     110
#define IRQ_BB_PMU                      111
#define IRQ_SD_DETECT_DOUBLE_EDGE       112

#define IRQ_UART_SIGNAL                 115

#define IRQ_ARM_PMU                     156

#define NR_GIC_IRQS                     (5 * 32)
#define NR_GPIO_IRQS                    (5 * 32)
#define NR_BOARD_IRQS                   64
#define NR_IRQS                         (NR_GIC_IRQS + NR_GPIO_IRQS + NR_BOARD_IRQS)

#define IRQ_BOARD_BASE                  (NR_GIC_IRQS + NR_GPIO_IRQS)

#endif
