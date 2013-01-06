#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

#define FIQ_START 			0

#define IRQ_LOCALTIMER                  29

#define RK30XX_IRQ(x)                   (x + 32)

#define IRQ_DMAC1_0                     RK30XX_IRQ(0)
#define IRQ_DMAC1_1                     RK30XX_IRQ(1)
#define IRQ_DMAC2_0                     RK30XX_IRQ(2)
#define IRQ_DMAC2_1                     RK30XX_IRQ(3)
#define IRQ_DDR_PCTL                    RK30XX_IRQ(4)
#define IRQ_HSIC                        37
#define IRQ_GPU                         39
#define IRQ_GPU_GP                      RK30XX_IRQ(5)
#define IRQ_GPU_MMU                     RK30XX_IRQ(6)
#define IRQ_GPU_PP                      RK30XX_IRQ(7)

#define IRQ_VEPU                        RK30XX_IRQ(9)
#define IRQ_VDPU                        RK30XX_IRQ(10)
#define IRQ_CIF0                        RK30XX_IRQ(11)
#define IRQ_CIF1                        RK30XX_IRQ(12)
#define IRQ_LCDC0                       RK30XX_IRQ(13)
#define IRQ_LCDC1                       RK30XX_IRQ(14)
#define IRQ_IPP                         RK30XX_IRQ(15)
#define IRQ_USB_OTG                     RK30XX_IRQ(16)
#define IRQ_USB_HOST                    RK30XX_IRQ(17)
#define IRQ_GPS                         50
#define IRQ_MAC                         RK30XX_IRQ(19)
#define IRQ_I2S2_2CH                    RK30XX_IRQ(20)
#define IRQ_TSADC                       RK30XX_IRQ(21)
#define IRQ_HSADC                       RK30XX_IRQ(22)
#define IRQ_SDMMC                       RK30XX_IRQ(23)
#define IRQ_SDIO                        RK30XX_IRQ(24)
#define IRQ_EMMC                        RK30XX_IRQ(25)
#define IRQ_SARADC                      RK30XX_IRQ(26)
#define IRQ_NANDC                       RK30XX_IRQ(27)

#define IRQ_SMC                         RK30XX_IRQ(29)
#define IRQ_PIDF                        RK30XX_IRQ(30)
#define IRQ_I2S0_8CH                    RK30XX_IRQ(31)
#define IRQ_I2S1_2CH                    RK30XX_IRQ(32)
#define IRQ_SPDIF                       RK30XX_IRQ(33)
#define IRQ_UART0                       RK30XX_IRQ(34)
#define IRQ_UART1                       RK30XX_IRQ(35)
#define IRQ_UART2                       RK30XX_IRQ(36)
#define IRQ_UART3                       RK30XX_IRQ(37)
#define IRQ_SPI0                        RK30XX_IRQ(38)
#define IRQ_SPI1                        RK30XX_IRQ(39)
#define IRQ_I2C0                        RK30XX_IRQ(40)
#define IRQ_I2C1                        RK30XX_IRQ(41)
#define IRQ_I2C2                        RK30XX_IRQ(42)
#define IRQ_I2C3                        RK30XX_IRQ(43)
#define IRQ_TIMER0                      RK30XX_IRQ(44)
#define IRQ_TIMER1                      RK30XX_IRQ(45)
#define IRQ_TIMER2                      RK30XX_IRQ(46)
#define IRQ_PWM0                        RK30XX_IRQ(47)
#define IRQ_PWM1                        RK30XX_IRQ(48)
#define IRQ_PWM2                        RK30XX_IRQ(49)
#define IRQ_PWM3                        RK30XX_IRQ(50)
#define IRQ_WDT                         RK30XX_IRQ(51)
#define IRQ_I2C4                        RK30XX_IRQ(52)
#define IRQ_PMU                         RK30XX_IRQ(53)
#define IRQ_GPIO0                       RK30XX_IRQ(54)
#define IRQ_GPIO1                       RK30XX_IRQ(55)
#define IRQ_GPIO2                       RK30XX_IRQ(56)
#define IRQ_GPIO3                       RK30XX_IRQ(57)
#define IRQ_GPIO4                       RK30XX_IRQ(58)

#define IRQ_GPIO6                       RK30XX_IRQ(60)
#define IRQ_PERI_AHB_USB_ARBITER        RK30XX_IRQ(61)
#define IRQ_PERI_AHB_EMEM_ARBITER       RK30XX_IRQ(62)
#define IRQ_RGA                         RK30XX_IRQ(63)
#define IRQ_HDMI                        RK30XX_IRQ(64)

#define IRQ_SDMMC_DETECT                RK30XX_IRQ(66)
#define IRQ_SDIO_DETECT                 RK30XX_IRQ(67)
#define IRQ_GPU_OBSRV_MAINFAULT         RK30XX_IRQ(68)
#define IRQ_PMU_STOP_EXIT_INT           RK30XX_IRQ(69)
#define IRQ_OBSERVER_MAINFAULT          RK30XX_IRQ(70)
#define IRQ_VPU_OBSRV_MAINFAULT         RK30XX_IRQ(71)
#define IRQ_PERI_OBSRV_MAINFAULT        RK30XX_IRQ(72)
#define IRQ_VIO1_OBSRV_MAINFAULT        RK30XX_IRQ(73)
#define IRQ_VIO0_OBSRV_MAINFAULT        RK30XX_IRQ(74)
#define IRQ_DMAC_OBSRV_MAINFAULT        RK30XX_IRQ(75)

//hhb@rock-chips.com this spi is used for fiq_debugger signal irq
#define IRQ_UART_SIGNAL			RK30XX_IRQ(80)
#if CONFIG_RK_DEBUG_UART >= 0 && CONFIG_RK_DEBUG_UART < 4
#define IRQ_DEBUG_UART			(IRQ_UART0 + CONFIG_RK_DEBUG_UART)
#endif

#define IRQ_ARM_PMU                     RK30XX_IRQ(103)

#define NR_GIC_IRQS                     (5 * 32)
#define NR_GPIO_IRQS                    (7 * 32)
#define NR_BOARD_IRQS                   64
#define NR_IRQS                         (NR_GIC_IRQS + NR_GPIO_IRQS + NR_BOARD_IRQS)

#define IRQ_BOARD_BASE                  (NR_GIC_IRQS + NR_GPIO_IRQS)

#endif
