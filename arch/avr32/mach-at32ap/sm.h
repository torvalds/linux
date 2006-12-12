/*
 * Register definitions for SM
 *
 * System Manager
 */
#ifndef __ASM_AVR32_SM_H__
#define __ASM_AVR32_SM_H__

/* SM register offsets */
#define SM_PM_MCCTRL                            0x0000
#define SM_PM_CKSEL                             0x0004
#define SM_PM_CPU_MASK                          0x0008
#define SM_PM_HSB_MASK                          0x000c
#define SM_PM_PBA_MASK                         0x0010
#define SM_PM_PBB_MASK                         0x0014
#define SM_PM_PLL0                              0x0020
#define SM_PM_PLL1                              0x0024
#define SM_PM_VCTRL                             0x0030
#define SM_PM_VMREF                             0x0034
#define SM_PM_VMV                               0x0038
#define SM_PM_IER                               0x0040
#define SM_PM_IDR                               0x0044
#define SM_PM_IMR                               0x0048
#define SM_PM_ISR                               0x004c
#define SM_PM_ICR                               0x0050
#define SM_PM_GCCTRL                            0x0060
#define SM_RTC_CTRL                             0x0080
#define SM_RTC_VAL                              0x0084
#define SM_RTC_TOP                              0x0088
#define SM_RTC_IER                              0x0090
#define SM_RTC_IDR                              0x0094
#define SM_RTC_IMR                              0x0098
#define SM_RTC_ISR                              0x009c
#define SM_RTC_ICR                              0x00a0
#define SM_WDT_CTRL                             0x00b0
#define SM_WDT_CLR                              0x00b4
#define SM_WDT_EXT                              0x00b8
#define SM_RC_RCAUSE                            0x00c0
#define SM_EIM_IER                              0x0100
#define SM_EIM_IDR                              0x0104
#define SM_EIM_IMR                              0x0108
#define SM_EIM_ISR                              0x010c
#define SM_EIM_ICR                              0x0110
#define SM_EIM_MODE                             0x0114
#define SM_EIM_EDGE                             0x0118
#define SM_EIM_LEVEL                            0x011c
#define SM_EIM_TEST                             0x0120
#define SM_EIM_NMIC                             0x0124

/* Bitfields in PM_MCCTRL */

/* Bitfields in PM_CKSEL */
#define SM_CPUSEL_OFFSET                        0
#define SM_CPUSEL_SIZE                          3
#define SM_CPUDIV_OFFSET                        7
#define SM_CPUDIV_SIZE                          1
#define SM_HSBSEL_OFFSET                        8
#define SM_HSBSEL_SIZE                          3
#define SM_HSBDIV_OFFSET                        15
#define SM_HSBDIV_SIZE                          1
#define SM_PBASEL_OFFSET                       16
#define SM_PBASEL_SIZE                         3
#define SM_PBADIV_OFFSET                       23
#define SM_PBADIV_SIZE                         1
#define SM_PBBSEL_OFFSET                       24
#define SM_PBBSEL_SIZE                         3
#define SM_PBBDIV_OFFSET                       31
#define SM_PBBDIV_SIZE                         1

/* Bitfields in PM_CPU_MASK */

/* Bitfields in PM_HSB_MASK */

/* Bitfields in PM_PBA_MASK */

/* Bitfields in PM_PBB_MASK */

/* Bitfields in PM_PLL0 */
#define SM_PLLEN_OFFSET                         0
#define SM_PLLEN_SIZE                           1
#define SM_PLLOSC_OFFSET                        1
#define SM_PLLOSC_SIZE                          1
#define SM_PLLOPT_OFFSET                        2
#define SM_PLLOPT_SIZE                          3
#define SM_PLLDIV_OFFSET                        8
#define SM_PLLDIV_SIZE                          8
#define SM_PLLMUL_OFFSET                        16
#define SM_PLLMUL_SIZE                          8
#define SM_PLLCOUNT_OFFSET                      24
#define SM_PLLCOUNT_SIZE                        6
#define SM_PLLTEST_OFFSET                       31
#define SM_PLLTEST_SIZE                         1

/* Bitfields in PM_PLL1 */

/* Bitfields in PM_VCTRL */
#define SM_VAUTO_OFFSET                         0
#define SM_VAUTO_SIZE                           1
#define SM_PM_VCTRL_VAL_OFFSET                  8
#define SM_PM_VCTRL_VAL_SIZE                    7

/* Bitfields in PM_VMREF */
#define SM_REFSEL_OFFSET                        0
#define SM_REFSEL_SIZE                          4

/* Bitfields in PM_VMV */
#define SM_PM_VMV_VAL_OFFSET                    0
#define SM_PM_VMV_VAL_SIZE                      8

/* Bitfields in PM_IER */

/* Bitfields in PM_IDR */

/* Bitfields in PM_IMR */

/* Bitfields in PM_ISR */

/* Bitfields in PM_ICR */
#define SM_LOCK0_OFFSET                         0
#define SM_LOCK0_SIZE                           1
#define SM_LOCK1_OFFSET                         1
#define SM_LOCK1_SIZE                           1
#define SM_WAKE_OFFSET                          2
#define SM_WAKE_SIZE                            1
#define SM_VOK_OFFSET                           3
#define SM_VOK_SIZE                             1
#define SM_VMRDY_OFFSET                         4
#define SM_VMRDY_SIZE                           1
#define SM_CKRDY_OFFSET                         5
#define SM_CKRDY_SIZE                           1

/* Bitfields in PM_GCCTRL */
#define SM_OSCSEL_OFFSET                        0
#define SM_OSCSEL_SIZE                          1
#define SM_PLLSEL_OFFSET                        1
#define SM_PLLSEL_SIZE                          1
#define SM_CEN_OFFSET                           2
#define SM_CEN_SIZE                             1
#define SM_CPC_OFFSET                           3
#define SM_CPC_SIZE                             1
#define SM_DIVEN_OFFSET                         4
#define SM_DIVEN_SIZE                           1
#define SM_DIV_OFFSET                           8
#define SM_DIV_SIZE                             8

/* Bitfields in RTC_CTRL */
#define SM_PCLR_OFFSET                          1
#define SM_PCLR_SIZE                            1
#define SM_TOPEN_OFFSET                         2
#define SM_TOPEN_SIZE                           1
#define SM_CLKEN_OFFSET                         3
#define SM_CLKEN_SIZE                           1
#define SM_PSEL_OFFSET                          8
#define SM_PSEL_SIZE                            16

/* Bitfields in RTC_VAL */
#define SM_RTC_VAL_VAL_OFFSET                   0
#define SM_RTC_VAL_VAL_SIZE                     31

/* Bitfields in RTC_TOP */
#define SM_RTC_TOP_VAL_OFFSET                   0
#define SM_RTC_TOP_VAL_SIZE                     32

/* Bitfields in RTC_IER */

/* Bitfields in RTC_IDR */

/* Bitfields in RTC_IMR */

/* Bitfields in RTC_ISR */

/* Bitfields in RTC_ICR */
#define SM_TOPI_OFFSET                          0
#define SM_TOPI_SIZE                            1

/* Bitfields in WDT_CTRL */
#define SM_KEY_OFFSET                           24
#define SM_KEY_SIZE                             8

/* Bitfields in WDT_CLR */

/* Bitfields in WDT_EXT */

/* Bitfields in RC_RCAUSE */
#define SM_POR_OFFSET                           0
#define SM_POR_SIZE                             1
#define SM_BOD_OFFSET                           1
#define SM_BOD_SIZE                             1
#define SM_EXT_OFFSET                           2
#define SM_EXT_SIZE                             1
#define SM_WDT_OFFSET                           3
#define SM_WDT_SIZE                             1
#define SM_NTAE_OFFSET                          4
#define SM_NTAE_SIZE                            1
#define SM_SERP_OFFSET                          5
#define SM_SERP_SIZE                            1

/* Bitfields in EIM_IER */

/* Bitfields in EIM_IDR */

/* Bitfields in EIM_IMR */

/* Bitfields in EIM_ISR */

/* Bitfields in EIM_ICR */

/* Bitfields in EIM_MODE */

/* Bitfields in EIM_EDGE */
#define SM_INT0_OFFSET                          0
#define SM_INT0_SIZE                            1
#define SM_INT1_OFFSET                          1
#define SM_INT1_SIZE                            1
#define SM_INT2_OFFSET                          2
#define SM_INT2_SIZE                            1
#define SM_INT3_OFFSET                          3
#define SM_INT3_SIZE                            1

/* Bitfields in EIM_LEVEL */

/* Bitfields in EIM_TEST */
#define SM_TESTEN_OFFSET                        31
#define SM_TESTEN_SIZE                          1

/* Bitfields in EIM_NMIC */
#define SM_EN_OFFSET                            0
#define SM_EN_SIZE                              1

/* Bit manipulation macros */
#define SM_BIT(name)                            (1 << SM_##name##_OFFSET)
#define SM_BF(name,value)                       (((value) & ((1 << SM_##name##_SIZE) - 1)) << SM_##name##_OFFSET)
#define SM_BFEXT(name,value)                    (((value) >> SM_##name##_OFFSET) & ((1 << SM_##name##_SIZE) - 1))
#define SM_BFINS(name,value,old)                (((old) & ~(((1 << SM_##name##_SIZE) - 1) << SM_##name##_OFFSET)) | SM_BF(name,value))

/* Register access macros */
#define sm_readl(port,reg)					\
	__raw_readl((port)->regs + SM_##reg)
#define sm_writel(port,reg,value)				\
	__raw_writel((value), (port)->regs + SM_##reg)

#endif /* __ASM_AVR32_SM_H__ */
