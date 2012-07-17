#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

#define FIQ_START 			0

#define IRQ_LOCALTIMER                  29

#define IRQ_DMAC_0                      32
#define IRQ_DMAC_1                      33
#define IRQ_DDR_PCTL                    34
#define IRQ_GPU_GP                      35
#define IRQ_GPU_MMU                     36
#define IRQ_GPU_PP                      37
#define IRQ_VEPU                        38
#define IRQ_VDPU                        39
#define IRQ_CIF                         40
#define IRQ_LCDC                        41
#define IRQ_USB_OTG                     42
#define IRQ_USB_HOST                    43
#define IRQ_GPS                         44
#define IRQ_GPS_TIMER                   45
#define IRQ_SDMMC                       46
#define IRQ_SDIO                        47
#define IRQ_EMMC                        48
#define IRQ_SARADC                      49
#define IRQ_NANDC                       50
#define IRQ_I2S                         51
#define IRQ_UART0                       52
#define IRQ_UART1                       53
#define IRQ_UART2                       54
#define IRQ_SPI                         55
#define IRQ_I2C0                        56
#define IRQ_I2C1                        57
#define IRQ_I2C2                        58
#define IRQ_I2C3                        59
#define IRQ_TIMER0                      60
#define IRQ_TIMER1                      61
#define IRQ_PWM0                        62
#define IRQ_PWM1                        63
#define IRQ_PWM2                        64

#define IRQ_WDT                         66
#define IRQ_OTG_BVALID                  67
#define IRQ_GPIO0                       68
#define IRQ_GPIO1                       69
#define IRQ_GPIO2                       70
#define IRQ_GPIO3                       71

#define IRQ_PERI_AHB_USB_ARBITER        74
#define IRQ_PERI_AHB_EMEM_ARBITER       75
#define IRQ_RGA                         76
#define IRQ_HDMI                        77
#define IRQ_SDMMC_DETECT                78
#define IRQ_SDIO_DETECT                 79

#define IRQ_ARM_PMU                     86

//hhb@rock-chips.com this spi is used for fiq_debugger signal irq
#define IRQ_UART_SIGNAL			127
#if CONFIG_RK_DEBUG_UART >= 0 && CONFIG_RK_DEBUG_UART < 3
#define IRQ_DEBUG_UART			(IRQ_UART0 + CONFIG_RK_DEBUG_UART)
#endif

#define NR_GIC_IRQS                     (4 * 32)
#define NR_GPIO_IRQS                    (4 * 32)
#define NR_BOARD_IRQS                   64
#define NR_IRQS                         (NR_GIC_IRQS + NR_GPIO_IRQS + NR_BOARD_IRQS)

#endif
