/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright 2024 NXP
 */

#ifndef __CLOCK_IMX95_H
#define __CLOCK_IMX95_H

/* The index should match i.MX95 SCMI Firmware */
#define IMX95_CLK_32K                       1
#define IMX95_CLK_24M                       2
#define IMX95_CLK_FRO                       3
#define IMX95_CLK_SYSPLL1_VCO               4
#define IMX95_CLK_SYSPLL1_PFD0_UNGATED      5
#define IMX95_CLK_SYSPLL1_PFD0              6
#define IMX95_CLK_SYSPLL1_PFD0_DIV2         7
#define IMX95_CLK_SYSPLL1_PFD1_UNGATED      8
#define IMX95_CLK_SYSPLL1_PFD1              9
#define IMX95_CLK_SYSPLL1_PFD1_DIV2         10
#define IMX95_CLK_SYSPLL1_PFD2_UNGATED      11
#define IMX95_CLK_SYSPLL1_PFD2              12
#define IMX95_CLK_SYSPLL1_PFD2_DIV2         13
#define IMX95_CLK_AUDIOPLL1_VCO             14
#define IMX95_CLK_AUDIOPLL1                 15
#define IMX95_CLK_AUDIOPLL2_VCO             16
#define IMX95_CLK_AUDIOPLL2                 17
#define IMX95_CLK_VIDEOPLL1_VCO             18
#define IMX95_CLK_VIDEOPLL1                 19
#define IMX95_CLK_RESERVED20                20
#define IMX95_CLK_RESERVED21                21
#define IMX95_CLK_RESERVED22                22
#define IMX95_CLK_RESERVED23                23
#define IMX95_CLK_ARMPLL_VCO                24
#define IMX95_CLK_ARMPLL_PFD0_UNGATED       25
#define IMX95_CLK_ARMPLL_PFD0               26
#define IMX95_CLK_ARMPLL_PFD1_UNGATED       27
#define IMX95_CLK_ARMPLL_PFD1               28
#define IMX95_CLK_ARMPLL_PFD2_UNGATED       29
#define IMX95_CLK_ARMPLL_PFD2               30
#define IMX95_CLK_ARMPLL_PFD3_UNGATED       31
#define IMX95_CLK_ARMPLL_PFD3               32
#define IMX95_CLK_DRAMPLL_VCO               33
#define IMX95_CLK_DRAMPLL                   34
#define IMX95_CLK_HSIOPLL_VCO               35
#define IMX95_CLK_HSIOPLL                   36
#define IMX95_CLK_LDBPLL_VCO                37
#define IMX95_CLK_LDBPLL                    38
#define IMX95_CLK_EXT1                      39
#define IMX95_CLK_EXT2                      40

#define IMX95_CCM_NUM_CLK_SRC               41

#define IMX95_CLK_ADC                      (IMX95_CCM_NUM_CLK_SRC + 0)
#define IMX95_CLK_TMU                      (IMX95_CCM_NUM_CLK_SRC + 1)
#define IMX95_CLK_BUSAON                   (IMX95_CCM_NUM_CLK_SRC + 2)
#define IMX95_CLK_CAN1                     (IMX95_CCM_NUM_CLK_SRC + 3)
#define IMX95_CLK_I3C1                     (IMX95_CCM_NUM_CLK_SRC + 4)
#define IMX95_CLK_I3C1SLOW                 (IMX95_CCM_NUM_CLK_SRC + 5)
#define IMX95_CLK_LPI2C1                   (IMX95_CCM_NUM_CLK_SRC + 6)
#define IMX95_CLK_LPI2C2                   (IMX95_CCM_NUM_CLK_SRC + 7)
#define IMX95_CLK_LPSPI1                   (IMX95_CCM_NUM_CLK_SRC + 8)
#define IMX95_CLK_LPSPI2                   (IMX95_CCM_NUM_CLK_SRC + 9)
#define IMX95_CLK_LPTMR1                   (IMX95_CCM_NUM_CLK_SRC + 10)
#define IMX95_CLK_LPUART1                  (IMX95_CCM_NUM_CLK_SRC + 11)
#define IMX95_CLK_LPUART2                  (IMX95_CCM_NUM_CLK_SRC + 12)
#define IMX95_CLK_M33                      (IMX95_CCM_NUM_CLK_SRC + 13)
#define IMX95_CLK_M33SYSTICK               (IMX95_CCM_NUM_CLK_SRC + 14)
#define IMX95_CLK_MQS1                     (IMX95_CCM_NUM_CLK_SRC + 15)
#define IMX95_CLK_PDM                      (IMX95_CCM_NUM_CLK_SRC + 16)
#define IMX95_CLK_SAI1                     (IMX95_CCM_NUM_CLK_SRC + 17)
#define IMX95_CLK_SENTINEL                 (IMX95_CCM_NUM_CLK_SRC + 18)
#define IMX95_CLK_TPM2                     (IMX95_CCM_NUM_CLK_SRC + 19)
#define IMX95_CLK_TSTMR1                   (IMX95_CCM_NUM_CLK_SRC + 20)
#define IMX95_CLK_CAMAPB                   (IMX95_CCM_NUM_CLK_SRC + 21)
#define IMX95_CLK_CAMAXI                   (IMX95_CCM_NUM_CLK_SRC + 22)
#define IMX95_CLK_CAMCM0                   (IMX95_CCM_NUM_CLK_SRC + 23)
#define IMX95_CLK_CAMISI                   (IMX95_CCM_NUM_CLK_SRC + 24)
#define IMX95_CLK_MIPIPHYCFG               (IMX95_CCM_NUM_CLK_SRC + 25)
#define IMX95_CLK_MIPIPHYPLLBYPASS         (IMX95_CCM_NUM_CLK_SRC + 26)
#define IMX95_CLK_MIPIPHYPLLREF            (IMX95_CCM_NUM_CLK_SRC + 27)
#define IMX95_CLK_MIPITESTBYTE             (IMX95_CCM_NUM_CLK_SRC + 28)
#define IMX95_CLK_A55                      (IMX95_CCM_NUM_CLK_SRC + 29)
#define IMX95_CLK_A55MTRBUS                (IMX95_CCM_NUM_CLK_SRC + 30)
#define IMX95_CLK_A55PERIPH                (IMX95_CCM_NUM_CLK_SRC + 31)
#define IMX95_CLK_DRAMALT                  (IMX95_CCM_NUM_CLK_SRC + 32)
#define IMX95_CLK_DRAMAPB                  (IMX95_CCM_NUM_CLK_SRC + 33)
#define IMX95_CLK_DISPAPB                  (IMX95_CCM_NUM_CLK_SRC + 34)
#define IMX95_CLK_DISPAXI                  (IMX95_CCM_NUM_CLK_SRC + 35)
#define IMX95_CLK_DISPDP                   (IMX95_CCM_NUM_CLK_SRC + 36)
#define IMX95_CLK_DISPOCRAM                (IMX95_CCM_NUM_CLK_SRC + 37)
#define IMX95_CLK_DISPUSB31                (IMX95_CCM_NUM_CLK_SRC + 38)
#define IMX95_CLK_DISP1PIX                 (IMX95_CCM_NUM_CLK_SRC + 39)
#define IMX95_CLK_DISP2PIX                 (IMX95_CCM_NUM_CLK_SRC + 40)
#define IMX95_CLK_DISP3PIX                 (IMX95_CCM_NUM_CLK_SRC + 41)
#define IMX95_CLK_GPUAPB                   (IMX95_CCM_NUM_CLK_SRC + 42)
#define IMX95_CLK_GPU                      (IMX95_CCM_NUM_CLK_SRC + 43)
#define IMX95_CLK_HSIOACSCAN480M           (IMX95_CCM_NUM_CLK_SRC + 44)
#define IMX95_CLK_HSIOACSCAN80M            (IMX95_CCM_NUM_CLK_SRC + 45)
#define IMX95_CLK_HSIO                     (IMX95_CCM_NUM_CLK_SRC + 46)
#define IMX95_CLK_HSIOPCIEAUX              (IMX95_CCM_NUM_CLK_SRC + 47)
#define IMX95_CLK_HSIOPCIETEST160M         (IMX95_CCM_NUM_CLK_SRC + 48)
#define IMX95_CLK_HSIOPCIETEST400M         (IMX95_CCM_NUM_CLK_SRC + 49)
#define IMX95_CLK_HSIOPCIETEST500M         (IMX95_CCM_NUM_CLK_SRC + 50)
#define IMX95_CLK_HSIOUSBTEST50M           (IMX95_CCM_NUM_CLK_SRC + 51)
#define IMX95_CLK_HSIOUSBTEST60M           (IMX95_CCM_NUM_CLK_SRC + 52)
#define IMX95_CLK_BUSM7                    (IMX95_CCM_NUM_CLK_SRC + 53)
#define IMX95_CLK_M7                       (IMX95_CCM_NUM_CLK_SRC + 54)
#define IMX95_CLK_M7SYSTICK                (IMX95_CCM_NUM_CLK_SRC + 55)
#define IMX95_CLK_BUSNETCMIX               (IMX95_CCM_NUM_CLK_SRC + 56)
#define IMX95_CLK_ENET                     (IMX95_CCM_NUM_CLK_SRC + 57)
#define IMX95_CLK_ENETPHYTEST200M          (IMX95_CCM_NUM_CLK_SRC + 58)
#define IMX95_CLK_ENETPHYTEST500M          (IMX95_CCM_NUM_CLK_SRC + 59)
#define IMX95_CLK_ENETPHYTEST667M          (IMX95_CCM_NUM_CLK_SRC + 60)
#define IMX95_CLK_ENETREF                  (IMX95_CCM_NUM_CLK_SRC + 61)
#define IMX95_CLK_ENETTIMER1               (IMX95_CCM_NUM_CLK_SRC + 62)
#define IMX95_CLK_MQS2                     (IMX95_CCM_NUM_CLK_SRC + 63)
#define IMX95_CLK_SAI2                     (IMX95_CCM_NUM_CLK_SRC + 64)
#define IMX95_CLK_NOCAPB                   (IMX95_CCM_NUM_CLK_SRC + 65)
#define IMX95_CLK_NOC                      (IMX95_CCM_NUM_CLK_SRC + 66)
#define IMX95_CLK_NPUAPB                   (IMX95_CCM_NUM_CLK_SRC + 67)
#define IMX95_CLK_NPU                      (IMX95_CCM_NUM_CLK_SRC + 68)
#define IMX95_CLK_CCMCKO1                  (IMX95_CCM_NUM_CLK_SRC + 69)
#define IMX95_CLK_CCMCKO2                  (IMX95_CCM_NUM_CLK_SRC + 70)
#define IMX95_CLK_CCMCKO3                  (IMX95_CCM_NUM_CLK_SRC + 71)
#define IMX95_CLK_CCMCKO4                  (IMX95_CCM_NUM_CLK_SRC + 72)
#define IMX95_CLK_VPUAPB                   (IMX95_CCM_NUM_CLK_SRC + 73)
#define IMX95_CLK_VPU                      (IMX95_CCM_NUM_CLK_SRC + 74)
#define IMX95_CLK_VPUDSP                   (IMX95_CCM_NUM_CLK_SRC + 75)
#define IMX95_CLK_VPUJPEG                  (IMX95_CCM_NUM_CLK_SRC + 76)
#define IMX95_CLK_AUDIOXCVR                (IMX95_CCM_NUM_CLK_SRC + 77)
#define IMX95_CLK_BUSWAKEUP                (IMX95_CCM_NUM_CLK_SRC + 78)
#define IMX95_CLK_CAN2                     (IMX95_CCM_NUM_CLK_SRC + 79)
#define IMX95_CLK_CAN3                     (IMX95_CCM_NUM_CLK_SRC + 80)
#define IMX95_CLK_CAN4                     (IMX95_CCM_NUM_CLK_SRC + 81)
#define IMX95_CLK_CAN5                     (IMX95_CCM_NUM_CLK_SRC + 82)
#define IMX95_CLK_FLEXIO1                  (IMX95_CCM_NUM_CLK_SRC + 83)
#define IMX95_CLK_FLEXIO2                  (IMX95_CCM_NUM_CLK_SRC + 84)
#define IMX95_CLK_FLEXSPI1                 (IMX95_CCM_NUM_CLK_SRC + 85)
#define IMX95_CLK_I3C2                     (IMX95_CCM_NUM_CLK_SRC + 86)
#define IMX95_CLK_I3C2SLOW                 (IMX95_CCM_NUM_CLK_SRC + 87)
#define IMX95_CLK_LPI2C3                   (IMX95_CCM_NUM_CLK_SRC + 88)
#define IMX95_CLK_LPI2C4                   (IMX95_CCM_NUM_CLK_SRC + 89)
#define IMX95_CLK_LPI2C5                   (IMX95_CCM_NUM_CLK_SRC + 90)
#define IMX95_CLK_LPI2C6                   (IMX95_CCM_NUM_CLK_SRC + 91)
#define IMX95_CLK_LPI2C7                   (IMX95_CCM_NUM_CLK_SRC + 92)
#define IMX95_CLK_LPI2C8                   (IMX95_CCM_NUM_CLK_SRC + 93)
#define IMX95_CLK_LPSPI3                   (IMX95_CCM_NUM_CLK_SRC + 94)
#define IMX95_CLK_LPSPI4                   (IMX95_CCM_NUM_CLK_SRC + 95)
#define IMX95_CLK_LPSPI5                   (IMX95_CCM_NUM_CLK_SRC + 96)
#define IMX95_CLK_LPSPI6                   (IMX95_CCM_NUM_CLK_SRC + 97)
#define IMX95_CLK_LPSPI7                   (IMX95_CCM_NUM_CLK_SRC + 98)
#define IMX95_CLK_LPSPI8                   (IMX95_CCM_NUM_CLK_SRC + 99)
#define IMX95_CLK_LPTMR2                   (IMX95_CCM_NUM_CLK_SRC + 100)
#define IMX95_CLK_LPUART3                  (IMX95_CCM_NUM_CLK_SRC + 101)
#define IMX95_CLK_LPUART4                  (IMX95_CCM_NUM_CLK_SRC + 102)
#define IMX95_CLK_LPUART5                  (IMX95_CCM_NUM_CLK_SRC + 103)
#define IMX95_CLK_LPUART6                  (IMX95_CCM_NUM_CLK_SRC + 104)
#define IMX95_CLK_LPUART7                  (IMX95_CCM_NUM_CLK_SRC + 105)
#define IMX95_CLK_LPUART8                  (IMX95_CCM_NUM_CLK_SRC + 106)
#define IMX95_CLK_SAI3                     (IMX95_CCM_NUM_CLK_SRC + 107)
#define IMX95_CLK_SAI4                     (IMX95_CCM_NUM_CLK_SRC + 108)
#define IMX95_CLK_SAI5                     (IMX95_CCM_NUM_CLK_SRC + 109)
#define IMX95_CLK_SPDIF                    (IMX95_CCM_NUM_CLK_SRC + 110)
#define IMX95_CLK_SWOTRACE                 (IMX95_CCM_NUM_CLK_SRC + 111)
#define IMX95_CLK_TPM4                     (IMX95_CCM_NUM_CLK_SRC + 112)
#define IMX95_CLK_TPM5                     (IMX95_CCM_NUM_CLK_SRC + 113)
#define IMX95_CLK_TPM6                     (IMX95_CCM_NUM_CLK_SRC + 114)
#define IMX95_CLK_TSTMR2                   (IMX95_CCM_NUM_CLK_SRC + 115)
#define IMX95_CLK_USBPHYBURUNIN            (IMX95_CCM_NUM_CLK_SRC + 116)
#define IMX95_CLK_USDHC1                   (IMX95_CCM_NUM_CLK_SRC + 117)
#define IMX95_CLK_USDHC2                   (IMX95_CCM_NUM_CLK_SRC + 118)
#define IMX95_CLK_USDHC3                   (IMX95_CCM_NUM_CLK_SRC + 119)
#define IMX95_CLK_V2XPK                    (IMX95_CCM_NUM_CLK_SRC + 120)
#define IMX95_CLK_WAKEUPAXI                (IMX95_CCM_NUM_CLK_SRC + 121)
#define IMX95_CLK_XSPISLVROOT              (IMX95_CCM_NUM_CLK_SRC + 122)
#define IMX95_CLK_SEL_EXT                  (IMX95_CCM_NUM_CLK_SRC + 123 + 0)
#define IMX95_CLK_SEL_A55C0                (IMX95_CCM_NUM_CLK_SRC + 123 + 1)
#define IMX95_CLK_SEL_A55C1                (IMX95_CCM_NUM_CLK_SRC + 123 + 2)
#define IMX95_CLK_SEL_A55C2                (IMX95_CCM_NUM_CLK_SRC + 123 + 3)
#define IMX95_CLK_SEL_A55C3                (IMX95_CCM_NUM_CLK_SRC + 123 + 4)
#define IMX95_CLK_SEL_A55C4                (IMX95_CCM_NUM_CLK_SRC + 123 + 5)
#define IMX95_CLK_SEL_A55C5                (IMX95_CCM_NUM_CLK_SRC + 123 + 6)
#define IMX95_CLK_SEL_A55P                 (IMX95_CCM_NUM_CLK_SRC + 123 + 7)
#define IMX95_CLK_SEL_DRAM                 (IMX95_CCM_NUM_CLK_SRC + 123 + 8)
#define IMX95_CLK_SEL_TEMPSENSE            (IMX95_CCM_NUM_CLK_SRC + 123 + 9)

#endif	/* __CLOCK_IMX95_H */
