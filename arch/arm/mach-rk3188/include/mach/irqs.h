#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

#define FIQ_START                       0

#define IRQ_LOCALTIMER                  29

#define IRQ_DMAC1_0                     32
#define IRQ_DMAC1_1                     33
#define IRQ_DMAC2_0                     34
#define IRQ_DMAC2_1                     35
#define IRQ_DDR_PCTL                    36
#define IRQ_HSIC                        37
#define IRQ_OTG_BVALID                  38
#define IRQ_GPU_PP                      39
#define IRQ_GPU_MMU                     40
#define IRQ_VEPU                        41
#define IRQ_VDPU                        42
#define IRQ_CIF0                        43
#define IRQ_GPU_GP                      44
#define IRQ_LCDC0                       45
#define IRQ_LCDC1                       46
#define IRQ_IPP                         47
#define IRQ_USB_OTG                     48
#define IRQ_USB_HOST                    49
#define IRQ_GPS                         50
#define IRQ_MAC                         51
#define IRQ_GPS_TIMER                   52

#define IRQ_HSADC                       54
#define IRQ_SDMMC                       55
#define IRQ_SDIO                        56
#define IRQ_EMMC                        57
#define IRQ_SARADC                      58
#define IRQ_NANDC                       59

#define IRQ_SMC                         61
#define IRQ_PIDF                        62

#define IRQ_I2S1_2CH                    64
#define IRQ_SPDIF                       65
#define IRQ_UART0                       66
#define IRQ_UART1                       67
#define IRQ_UART2                       68
#define IRQ_UART3                       69
#define IRQ_SPI0                        70
#define IRQ_SPI1                        71
#define IRQ_I2C0                        72
#define IRQ_I2C1                        73
#define IRQ_I2C2                        74
#define IRQ_I2C3                        75
#define IRQ_TIMER0                      76
#define IRQ_TIMER1                      77
#define IRQ_TIMER2                      78
#define IRQ_PWM0                        79
#define IRQ_PWM1                        80
#define IRQ_PWM2                        81
#define IRQ_PWM3                        82
#define IRQ_WDT                         83
#define IRQ_I2C4                        84
#define IRQ_PMU                         85
#define IRQ_GPIO0                       86
#define IRQ_GPIO1                       87
#define IRQ_GPIO2                       88
#define IRQ_GPIO3                       89
#define IRQ_TIMER3                      90
#define IRQ_TIMER4                      91
#define IRQ_TIMER5                      92
#define IRQ_PERI_AHB_USB_ARBITER        93
#define IRQ_PERI_AHB_EMEM_ARBITER       94
#define IRQ_RGA                         95
#define IRQ_TIMER6                      96

#define IRQ_SDMMC_DETECT                98
#define IRQ_SDIO_DETECT                 99
#define IRQ_GPU_OBSRV_MAINFAULT         100
#define IRQ_PMU_STOP_EXIT_INT           101
#define IRQ_OBSERVER_MAINFAULT          102
#define IRQ_VPU_OBSRV_MAINFAULT         103
#define IRQ_PERI_OBSRV_MAINFAULT        104
#define IRQ_VIO1_OBSRV_MAINFAULT        105
#define IRQ_VIO0_OBSRV_MAINFAULT        106
#define IRQ_DMAC_OBSRV_MAINFAULT        107

#define IRQ_UART_SIGNAL                 112

#define IRQ_ARM_PMU                     151

#define NR_GIC_IRQS                     (5 * 32)
#define NR_GPIO_IRQS                    (4 * 32)
#define NR_BOARD_IRQS                   64
#define NR_IRQS                         (NR_GIC_IRQS + NR_GPIO_IRQS + NR_BOARD_IRQS)

#define IRQ_BOARD_BASE                  (NR_GIC_IRQS + NR_GPIO_IRQS)

#endif
