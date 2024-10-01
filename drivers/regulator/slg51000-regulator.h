/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * SLG51000 High PSRR, Multi-Output Regulators
 * Copyright (C) 2019  Dialog Semiconductor
 *
 * Author: Eric Jeong <eric.jeong.opensource@diasemi.com>
 */

#ifndef __SLG51000_REGISTERS_H__
#define __SLG51000_REGISTERS_H__

/* Registers */

#define SLG51000_SYSCTL_PATN_ID_B0              0x1105
#define SLG51000_SYSCTL_PATN_ID_B1              0x1106
#define SLG51000_SYSCTL_PATN_ID_B2              0x1107
#define SLG51000_SYSCTL_SYS_CONF_A              0x1109
#define SLG51000_SYSCTL_SYS_CONF_D              0x110c
#define SLG51000_SYSCTL_MATRIX_CONF_A           0x110d
#define SLG51000_SYSCTL_MATRIX_CONF_B           0x110e
#define SLG51000_SYSCTL_REFGEN_CONF_C           0x1111
#define SLG51000_SYSCTL_UVLO_CONF_A             0x1112
#define SLG51000_SYSCTL_FAULT_LOG1              0x1115
#define SLG51000_SYSCTL_EVENT                   0x1116
#define SLG51000_SYSCTL_STATUS                  0x1117
#define SLG51000_SYSCTL_IRQ_MASK                0x1118
#define SLG51000_IO_GPIO1_CONF                  0x1500
#define SLG51000_IO_GPIO2_CONF                  0x1501
#define SLG51000_IO_GPIO3_CONF                  0x1502
#define SLG51000_IO_GPIO4_CONF                  0x1503
#define SLG51000_IO_GPIO5_CONF                  0x1504
#define SLG51000_IO_GPIO6_CONF                  0x1505
#define SLG51000_IO_GPIO_STATUS                 0x1506
#define SLG51000_LUTARRAY_LUT_VAL_0             0x1600
#define SLG51000_LUTARRAY_LUT_VAL_1             0x1601
#define SLG51000_LUTARRAY_LUT_VAL_2             0x1602
#define SLG51000_LUTARRAY_LUT_VAL_3             0x1603
#define SLG51000_LUTARRAY_LUT_VAL_4             0x1604
#define SLG51000_LUTARRAY_LUT_VAL_5             0x1605
#define SLG51000_LUTARRAY_LUT_VAL_6             0x1606
#define SLG51000_LUTARRAY_LUT_VAL_7             0x1607
#define SLG51000_LUTARRAY_LUT_VAL_8             0x1608
#define SLG51000_LUTARRAY_LUT_VAL_9             0x1609
#define SLG51000_LUTARRAY_LUT_VAL_10            0x160a
#define SLG51000_LUTARRAY_LUT_VAL_11            0x160b
#define SLG51000_MUXARRAY_INPUT_SEL_0           0x1700
#define SLG51000_MUXARRAY_INPUT_SEL_1           0x1701
#define SLG51000_MUXARRAY_INPUT_SEL_2           0x1702
#define SLG51000_MUXARRAY_INPUT_SEL_3           0x1703
#define SLG51000_MUXARRAY_INPUT_SEL_4           0x1704
#define SLG51000_MUXARRAY_INPUT_SEL_5           0x1705
#define SLG51000_MUXARRAY_INPUT_SEL_6           0x1706
#define SLG51000_MUXARRAY_INPUT_SEL_7           0x1707
#define SLG51000_MUXARRAY_INPUT_SEL_8           0x1708
#define SLG51000_MUXARRAY_INPUT_SEL_9           0x1709
#define SLG51000_MUXARRAY_INPUT_SEL_10          0x170a
#define SLG51000_MUXARRAY_INPUT_SEL_11          0x170b
#define SLG51000_MUXARRAY_INPUT_SEL_12          0x170c
#define SLG51000_MUXARRAY_INPUT_SEL_13          0x170d
#define SLG51000_MUXARRAY_INPUT_SEL_14          0x170e
#define SLG51000_MUXARRAY_INPUT_SEL_15          0x170f
#define SLG51000_MUXARRAY_INPUT_SEL_16          0x1710
#define SLG51000_MUXARRAY_INPUT_SEL_17          0x1711
#define SLG51000_MUXARRAY_INPUT_SEL_18          0x1712
#define SLG51000_MUXARRAY_INPUT_SEL_19          0x1713
#define SLG51000_MUXARRAY_INPUT_SEL_20          0x1714
#define SLG51000_MUXARRAY_INPUT_SEL_21          0x1715
#define SLG51000_MUXARRAY_INPUT_SEL_22          0x1716
#define SLG51000_MUXARRAY_INPUT_SEL_23          0x1717
#define SLG51000_MUXARRAY_INPUT_SEL_24          0x1718
#define SLG51000_MUXARRAY_INPUT_SEL_25          0x1719
#define SLG51000_MUXARRAY_INPUT_SEL_26          0x171a
#define SLG51000_MUXARRAY_INPUT_SEL_27          0x171b
#define SLG51000_MUXARRAY_INPUT_SEL_28          0x171c
#define SLG51000_MUXARRAY_INPUT_SEL_29          0x171d
#define SLG51000_MUXARRAY_INPUT_SEL_30          0x171e
#define SLG51000_MUXARRAY_INPUT_SEL_31          0x171f
#define SLG51000_MUXARRAY_INPUT_SEL_32          0x1720
#define SLG51000_MUXARRAY_INPUT_SEL_33          0x1721
#define SLG51000_MUXARRAY_INPUT_SEL_34          0x1722
#define SLG51000_MUXARRAY_INPUT_SEL_35          0x1723
#define SLG51000_MUXARRAY_INPUT_SEL_36          0x1724
#define SLG51000_MUXARRAY_INPUT_SEL_37          0x1725
#define SLG51000_MUXARRAY_INPUT_SEL_38          0x1726
#define SLG51000_MUXARRAY_INPUT_SEL_39          0x1727
#define SLG51000_MUXARRAY_INPUT_SEL_40          0x1728
#define SLG51000_MUXARRAY_INPUT_SEL_41          0x1729
#define SLG51000_MUXARRAY_INPUT_SEL_42          0x172a
#define SLG51000_MUXARRAY_INPUT_SEL_43          0x172b
#define SLG51000_MUXARRAY_INPUT_SEL_44          0x172c
#define SLG51000_MUXARRAY_INPUT_SEL_45          0x172d
#define SLG51000_MUXARRAY_INPUT_SEL_46          0x172e
#define SLG51000_MUXARRAY_INPUT_SEL_47          0x172f
#define SLG51000_MUXARRAY_INPUT_SEL_48          0x1730
#define SLG51000_MUXARRAY_INPUT_SEL_49          0x1731
#define SLG51000_MUXARRAY_INPUT_SEL_50          0x1732
#define SLG51000_MUXARRAY_INPUT_SEL_51          0x1733
#define SLG51000_MUXARRAY_INPUT_SEL_52          0x1734
#define SLG51000_MUXARRAY_INPUT_SEL_53          0x1735
#define SLG51000_MUXARRAY_INPUT_SEL_54          0x1736
#define SLG51000_MUXARRAY_INPUT_SEL_55          0x1737
#define SLG51000_MUXARRAY_INPUT_SEL_56          0x1738
#define SLG51000_MUXARRAY_INPUT_SEL_57          0x1739
#define SLG51000_MUXARRAY_INPUT_SEL_58          0x173a
#define SLG51000_MUXARRAY_INPUT_SEL_59          0x173b
#define SLG51000_MUXARRAY_INPUT_SEL_60          0x173c
#define SLG51000_MUXARRAY_INPUT_SEL_61          0x173d
#define SLG51000_MUXARRAY_INPUT_SEL_62          0x173e
#define SLG51000_MUXARRAY_INPUT_SEL_63          0x173f
#define SLG51000_PWRSEQ_RESOURCE_EN_0           0x1900
#define SLG51000_PWRSEQ_RESOURCE_EN_1           0x1901
#define SLG51000_PWRSEQ_RESOURCE_EN_2           0x1902
#define SLG51000_PWRSEQ_RESOURCE_EN_3           0x1903
#define SLG51000_PWRSEQ_RESOURCE_EN_4           0x1904
#define SLG51000_PWRSEQ_RESOURCE_EN_5           0x1905
#define SLG51000_PWRSEQ_SLOT_TIME_MIN_UP0       0x1906
#define SLG51000_PWRSEQ_SLOT_TIME_MIN_DOWN0     0x1907
#define SLG51000_PWRSEQ_SLOT_TIME_MIN_UP1       0x1908
#define SLG51000_PWRSEQ_SLOT_TIME_MIN_DOWN1     0x1909
#define SLG51000_PWRSEQ_SLOT_TIME_MIN_UP2       0x190a
#define SLG51000_PWRSEQ_SLOT_TIME_MIN_DOWN2     0x190b
#define SLG51000_PWRSEQ_SLOT_TIME_MIN_UP3       0x190c
#define SLG51000_PWRSEQ_SLOT_TIME_MIN_DOWN3     0x190d
#define SLG51000_PWRSEQ_SLOT_TIME_MIN_UP4       0x190e
#define SLG51000_PWRSEQ_SLOT_TIME_MIN_DOWN4     0x190f
#define SLG51000_PWRSEQ_SLOT_TIME_MIN_UP5       0x1910
#define SLG51000_PWRSEQ_SLOT_TIME_MIN_DOWN5     0x1911
#define SLG51000_PWRSEQ_SLOT_TIME_MAX_CONF_A    0x1912
#define SLG51000_PWRSEQ_SLOT_TIME_MAX_CONF_B    0x1913
#define SLG51000_PWRSEQ_SLOT_TIME_MAX_CONF_C    0x1914
#define SLG51000_PWRSEQ_INPUT_SENSE_CONF_A      0x1915
#define SLG51000_PWRSEQ_INPUT_SENSE_CONF_B      0x1916
#define SLG51000_LDO1_VSEL                      0x2000
#define SLG51000_LDO1_MINV                      0x2060
#define SLG51000_LDO1_MAXV                      0x2061
#define SLG51000_LDO1_MISC1                     0x2064
#define SLG51000_LDO1_VSEL_ACTUAL               0x2065
#define SLG51000_LDO1_EVENT                     0x20c0
#define SLG51000_LDO1_STATUS                    0x20c1
#define SLG51000_LDO1_IRQ_MASK                  0x20c2
#define SLG51000_LDO2_VSEL                      0x2200
#define SLG51000_LDO2_MINV                      0x2260
#define SLG51000_LDO2_MAXV                      0x2261
#define SLG51000_LDO2_MISC1                     0x2264
#define SLG51000_LDO2_VSEL_ACTUAL               0x2265
#define SLG51000_LDO2_EVENT                     0x22c0
#define SLG51000_LDO2_STATUS                    0x22c1
#define SLG51000_LDO2_IRQ_MASK                  0x22c2
#define SLG51000_LDO3_VSEL                      0x2300
#define SLG51000_LDO3_MINV                      0x2360
#define SLG51000_LDO3_MAXV                      0x2361
#define SLG51000_LDO3_CONF1                     0x2364
#define SLG51000_LDO3_CONF2                     0x2365
#define SLG51000_LDO3_VSEL_ACTUAL               0x2366
#define SLG51000_LDO3_EVENT                     0x23c0
#define SLG51000_LDO3_STATUS                    0x23c1
#define SLG51000_LDO3_IRQ_MASK                  0x23c2
#define SLG51000_LDO4_VSEL                      0x2500
#define SLG51000_LDO4_MINV                      0x2560
#define SLG51000_LDO4_MAXV                      0x2561
#define SLG51000_LDO4_CONF1                     0x2564
#define SLG51000_LDO4_CONF2                     0x2565
#define SLG51000_LDO4_VSEL_ACTUAL               0x2566
#define SLG51000_LDO4_EVENT                     0x25c0
#define SLG51000_LDO4_STATUS                    0x25c1
#define SLG51000_LDO4_IRQ_MASK                  0x25c2
#define SLG51000_LDO5_VSEL                      0x2700
#define SLG51000_LDO5_MINV                      0x2760
#define SLG51000_LDO5_MAXV                      0x2761
#define SLG51000_LDO5_TRIM2                     0x2763
#define SLG51000_LDO5_CONF1                     0x2765
#define SLG51000_LDO5_CONF2                     0x2766
#define SLG51000_LDO5_VSEL_ACTUAL               0x2767
#define SLG51000_LDO5_EVENT                     0x27c0
#define SLG51000_LDO5_STATUS                    0x27c1
#define SLG51000_LDO5_IRQ_MASK                  0x27c2
#define SLG51000_LDO6_VSEL                      0x2900
#define SLG51000_LDO6_MINV                      0x2960
#define SLG51000_LDO6_MAXV                      0x2961
#define SLG51000_LDO6_TRIM2                     0x2963
#define SLG51000_LDO6_CONF1                     0x2965
#define SLG51000_LDO6_CONF2                     0x2966
#define SLG51000_LDO6_VSEL_ACTUAL               0x2967
#define SLG51000_LDO6_EVENT                     0x29c0
#define SLG51000_LDO6_STATUS                    0x29c1
#define SLG51000_LDO6_IRQ_MASK                  0x29c2
#define SLG51000_LDO7_VSEL                      0x3100
#define SLG51000_LDO7_MINV                      0x3160
#define SLG51000_LDO7_MAXV                      0x3161
#define SLG51000_LDO7_CONF1                     0x3164
#define SLG51000_LDO7_CONF2                     0x3165
#define SLG51000_LDO7_VSEL_ACTUAL               0x3166
#define SLG51000_LDO7_EVENT                     0x31c0
#define SLG51000_LDO7_STATUS                    0x31c1
#define SLG51000_LDO7_IRQ_MASK                  0x31c2
#define SLG51000_OTP_EVENT                      0x782b
#define SLG51000_OTP_IRQ_MASK                   0x782d
#define SLG51000_OTP_LOCK_OTP_PROG              0x78fe
#define SLG51000_OTP_LOCK_CTRL                  0x78ff
#define SLG51000_LOCK_GLOBAL_LOCK_CTRL1         0x8000

/* Register Bit Fields */

/* SLG51000_SYSCTL_PATTERN_ID_BYTE0 = 0x1105 */
#define SLG51000_PATTERN_ID_BYTE0_SHIFT         0
#define SLG51000_PATTERN_ID_BYTE0_MASK          (0xff << 0)

/* SLG51000_SYSCTL_PATTERN_ID_BYTE1 = 0x1106 */
#define SLG51000_PATTERN_ID_BYTE1_SHIFT         0
#define SLG51000_PATTERN_ID_BYTE1_MASK          (0xff << 0)

/* SLG51000_SYSCTL_PATTERN_ID_BYTE2 = 0x1107 */
#define SLG51000_PATTERN_ID_BYTE2_SHIFT         0
#define SLG51000_PATTERN_ID_BYTE2_MASK          (0xff << 0)

/* SLG51000_SYSCTL_SYS_CONF_A = 0x1109 */
#define SLG51000_I2C_ADDRESS_SHIFT              0
#define SLG51000_I2C_ADDRESS_MASK               (0x7f << 0)
#define SLG51000_I2C_DISABLE_SHIFT              7
#define SLG51000_I2C_DISABLE_MASK               (0x01 << 7)

/* SLG51000_SYSCTL_SYS_CONF_D = 0x110c */
#define SLG51000_CS_T_DEB_SHIFT                 6
#define SLG51000_CS_T_DEB_MASK                  (0x03 << 6)
#define SLG51000_I2C_CLR_MODE_SHIFT             5
#define SLG51000_I2C_CLR_MODE_MASK              (0x01 << 5)

/* SLG51000_SYSCTL_MATRIX_CTRL_CONF_A = 0x110d */
#define SLG51000_RESOURCE_CTRL_SHIFT            0
#define SLG51000_RESOURCE_CTRL_MASK             (0xff << 0)

/* SLG51000_SYSCTL_MATRIX_CTRL_CONF_B = 0x110e */
#define SLG51000_MATRIX_EVENT_SENSE_SHIFT       0
#define SLG51000_MATRIX_EVENT_SENSE_MASK        (0x07 << 0)

/* SLG51000_SYSCTL_REFGEN_CONF_C = 0x1111 */
#define SLG51000_REFGEN_SEL_TEMP_WARN_DEBOUNCE_SHIFT    2
#define SLG51000_REFGEN_SEL_TEMP_WARN_DEBOUNCE_MASK     (0x03 << 2)
#define SLG51000_REFGEN_SEL_TEMP_WARN_THR_SHIFT         0
#define SLG51000_REFGEN_SEL_TEMP_WARN_THR_MASK          (0x03 << 0)

/* SLG51000_SYSCTL_UVLO_CONF_A = 0x1112 */
#define SLG51000_VMON_UVLO_SEL_THR_SHIFT        0
#define SLG51000_VMON_UVLO_SEL_THR_MASK         (0x1f << 0)

/* SLG51000_SYSCTL_FAULT_LOG1 = 0x1115 */
#define SLG51000_FLT_POR_SHIFT                  5
#define SLG51000_FLT_POR_MASK                   (0x01 << 5)
#define SLG51000_FLT_RST_SHIFT                  4
#define SLG51000_FLT_RST_MASK                   (0x01 << 4)
#define SLG51000_FLT_POWER_SEQ_CRASH_REQ_SHIFT  2
#define SLG51000_FLT_POWER_SEQ_CRASH_REQ_MASK   (0x01 << 2)
#define SLG51000_FLT_OVER_TEMP_SHIFT            1
#define SLG51000_FLT_OVER_TEMP_MASK             (0x01 << 1)

/* SLG51000_SYSCTL_EVENT = 0x1116 */
#define SLG51000_EVT_MATRIX_SHIFT               1
#define SLG51000_EVT_MATRIX_MASK                (0x01 << 1)
#define SLG51000_EVT_HIGH_TEMP_WARN_SHIFT       0
#define SLG51000_EVT_HIGH_TEMP_WARN_MASK        (0x01 << 0)

/* SLG51000_SYSCTL_STATUS = 0x1117 */
#define SLG51000_STA_MATRIX_SHIFT               1
#define SLG51000_STA_MATRIX_MASK                (0x01 << 1)
#define SLG51000_STA_HIGH_TEMP_WARN_SHIFT       0
#define SLG51000_STA_HIGH_TEMP_WARN_MASK        (0x01 << 0)

/* SLG51000_SYSCTL_IRQ_MASK = 0x1118 */
#define SLG51000_IRQ_MATRIX_SHIFT               1
#define SLG51000_IRQ_MATRIX_MASK                (0x01 << 1)
#define SLG51000_IRQ_HIGH_TEMP_WARN_SHIFT       0
#define SLG51000_IRQ_HIGH_TEMP_WARN_MASK        (0x01 << 0)

/* SLG51000_IO_GPIO1_CONF ~ SLG51000_IO_GPIO5_CONF =
 * 0x1500, 0x1501, 0x1502, 0x1503, 0x1504
 */
#define SLG51000_GPIO_DIR_SHIFT                 7
#define SLG51000_GPIO_DIR_MASK                  (0x01 << 7)
#define SLG51000_GPIO_SENS_SHIFT                5
#define SLG51000_GPIO_SENS_MASK                 (0x03 << 5)
#define SLG51000_GPIO_INVERT_SHIFT              4
#define SLG51000_GPIO_INVERT_MASK               (0x01 << 4)
#define SLG51000_GPIO_BYP_SHIFT                 3
#define SLG51000_GPIO_BYP_MASK                  (0x01 << 3)
#define SLG51000_GPIO_T_DEB_SHIFT               1
#define SLG51000_GPIO_T_DEB_MASK                (0x03 << 1)
#define SLG51000_GPIO_LEVEL_SHIFT               0
#define SLG51000_GPIO_LEVEL_MASK                (0x01 << 0)

/* SLG51000_IO_GPIO6_CONF = 0x1505 */
#define SLG51000_GPIO6_SENS_SHIFT               5
#define SLG51000_GPIO6_SENS_MASK                (0x03 << 5)
#define SLG51000_GPIO6_INVERT_SHIFT             4
#define SLG51000_GPIO6_INVERT_MASK              (0x01 << 4)
#define SLG51000_GPIO6_T_DEB_SHIFT              1
#define SLG51000_GPIO6_T_DEB_MASK               (0x03 << 1)
#define SLG51000_GPIO6_LEVEL_SHIFT              0
#define SLG51000_GPIO6_LEVEL_MASK               (0x01 << 0)

/* SLG51000_IO_GPIO_STATUS = 0x1506 */
#define SLG51000_GPIO6_STATUS_SHIFT             5
#define SLG51000_GPIO6_STATUS_MASK              (0x01 << 5)
#define SLG51000_GPIO5_STATUS_SHIFT             4
#define SLG51000_GPIO5_STATUS_MASK              (0x01 << 4)
#define SLG51000_GPIO4_STATUS_SHIFT             3
#define SLG51000_GPIO4_STATUS_MASK              (0x01 << 3)
#define SLG51000_GPIO3_STATUS_SHIFT             2
#define SLG51000_GPIO3_STATUS_MASK              (0x01 << 2)
#define SLG51000_GPIO2_STATUS_SHIFT             1
#define SLG51000_GPIO2_STATUS_MASK              (0x01 << 1)
#define SLG51000_GPIO1_STATUS_SHIFT             0
#define SLG51000_GPIO1_STATUS_MASK              (0x01 << 0)

/* SLG51000_LUTARRAY_LUT_VAL_0 ~ SLG51000_LUTARRAY_LUT_VAL_11
 * 0x1600, 0x1601, 0x1602, 0x1603, 0x1604, 0x1605,
 * 0x1606, 0x1607, 0x1608, 0x1609, 0x160a, 0x160b
 */
#define SLG51000_LUT_VAL_SHIFT                  0
#define SLG51000_LUT_VAL_MASK                   (0xff << 0)

/* SLG51000_MUXARRAY_INPUT_SEL_0 ~ SLG51000_MUXARRAY_INPUT_SEL_63
 * 0x1700, 0x1701, 0x1702, 0x1703, 0x1704, 0x1705,
 * 0x1706, 0x1707, 0x1708, 0x1709, 0x170a, 0x170b,
 * 0x170c, 0x170d, 0x170e, 0x170f, 0x1710, 0x1711,
 * 0x1712, 0x1713, 0x1714, 0x1715, 0x1716, 0x1717,
 * 0x1718, 0x1719, 0x171a, 0x171b, 0x171c, 0x171d,
 * 0x171e, 0x171f, 0x1720, 0x1721, 0x1722, 0x1723,
 * 0x1724, 0x1725, 0x1726, 0x1727, 0x1728, 0x1729,
 * 0x173a, 0x173b, 0x173c, 0x173d, 0x173e, 0x173f,
 */
#define SLG51000_INPUT_SEL_SHIFT                0
#define SLG51000_INPUT_SEL_MASK                 (0x3f << 0)

/* SLG51000_PWRSEQ_RESOURCE_EN_0 ~ SLG51000_PWRSEQ_RESOURCE_EN_5
 * 0x1900, 0x1901, 0x1902, 0x1903, 0x1904, 0x1905
 */
#define SLG51000_RESOURCE_EN_DOWN0_SHIFT        4
#define SLG51000_RESOURCE_EN_DOWN0_MASK         (0x07 << 4)
#define SLG51000_RESOURCE_EN_UP0_SHIFT          0
#define SLG51000_RESOURCE_EN_UP0_MASK           (0x07 << 0)

/* SLG51000_PWRSEQ_SLOT_TIME_MIN_UP0 ~ SLG51000_PWRSEQ_SLOT_TIME_MIN_UP5
 * 0x1906, 0x1908, 0x190a, 0x190c, 0x190e, 0x1910
 */
#define SLG51000_SLOT_TIME_MIN_UP_SHIFT         0
#define SLG51000_SLOT_TIME_MIN_UP_MASK          (0xff << 0)

/* SLG51000_PWRSEQ_SLOT_TIME_MIN_DOWN0 ~ SLG51000_PWRSEQ_SLOT_TIME_MIN_DOWN5
 * 0x1907, 0x1909, 0x190b, 0x190d, 0x190f, 0x1911
 */
#define SLG51000_SLOT_TIME_MIN_DOWN_SHIFT       0
#define SLG51000_SLOT_TIME_MIN_DOWN_MASK        (0xff << 0)

/* SLG51000_PWRSEQ_SLOT_TIME_MAX_CONF_A ~ SLG51000_PWRSEQ_SLOT_TIME_MAX_CONF_C
 * 0x1912, 0x1913, 0x1914
 */
#define SLG51000_SLOT_TIME_MAX_DOWN1_SHIFT      6
#define SLG51000_SLOT_TIME_MAX_DOWN1_MASK       (0x03 << 6)
#define SLG51000_SLOT_TIME_MAX_UP1_SHIFT        4
#define SLG51000_SLOT_TIME_MAX_UP1_MASK         (0x03 << 4)
#define SLG51000_SLOT_TIME_MAX_DOWN0_SHIFT      2
#define SLG51000_SLOT_TIME_MAX_DOWN0_MASK       (0x03 << 2)
#define SLG51000_SLOT_TIME_MAX_UP0_SHIFT        0
#define SLG51000_SLOT_TIME_MAX_UP0_MASK         (0x03 << 0)

/* SLG51000_PWRSEQ_INPUT_SENSE_CONF_A = 0x1915 */
#define SLG51000_TRIG_UP_SENSE_SHIFT            6
#define SLG51000_TRIG_UP_SENSE_MASK             (0x01 << 6)
#define SLG51000_UP_EN_SENSE5_SHIFT             5
#define SLG51000_UP_EN_SENSE5_MASK              (0x01 << 5)
#define SLG51000_UP_EN_SENSE4_SHIFT             4
#define SLG51000_UP_EN_SENSE4_MASK              (0x01 << 4)
#define SLG51000_UP_EN_SENSE3_SHIFT             3
#define SLG51000_UP_EN_SENSE3_MASK              (0x01 << 3)
#define SLG51000_UP_EN_SENSE2_SHIFT             2
#define SLG51000_UP_EN_SENSE2_MASK              (0x01 << 2)
#define SLG51000_UP_EN_SENSE1_SHIFT             1
#define SLG51000_UP_EN_SENSE1_MASK              (0x01 << 1)
#define SLG51000_UP_EN_SENSE0_SHIFT             0
#define SLG51000_UP_EN_SENSE0_MASK              (0x01 << 0)

/* SLG51000_PWRSEQ_INPUT_SENSE_CONF_B = 0x1916 */
#define SLG51000_CRASH_DETECT_SENSE_SHIFT       7
#define SLG51000_CRASH_DETECT_SENSE_MASK        (0x01 << 7)
#define SLG51000_TRIG_DOWN_SENSE_SHIFT          6
#define SLG51000_TRIG_DOWN_SENSE_MASK           (0x01 << 6)
#define SLG51000_DOWN_EN_SENSE5_SHIFT           5
#define SLG51000_DOWN_EN_SENSE5_MASK            (0x01 << 5)
#define SLG51000_DOWN_EN_SENSE4_SHIFT           4
#define SLG51000_DOWN_EN_SENSE4_MASK            (0x01 << 4)
#define SLG51000_DOWN_EN_SENSE3_SHIFT           3
#define SLG51000_DOWN_EN_SENSE3_MASK            (0x01 << 3)
#define SLG51000_DOWN_EN_SENSE2_SHIFT           2
#define SLG51000_DOWN_EN_SENSE2_MASK            (0x01 << 2)
#define SLG51000_DOWN_EN_SENSE1_SHIFT           1
#define SLG51000_DOWN_EN_SENSE1_MASK            (0x01 << 1)
#define SLG51000_DOWN_EN_SENSE0_SHIFT           0
#define SLG51000_DOWN_EN_SENSE0_MASK            (0x01 << 0)

/* SLG51000_LDO1_VSEL ~ SLG51000_LDO7_VSEL =
 * 0x2000, 0x2200, 0x2300, 0x2500, 0x2700, 0x2900, 0x3100
 */
#define SLG51000_VSEL_SHIFT                     0
#define SLG51000_VSEL_MASK                      (0xff << 0)

/* SLG51000_LDO1_MINV ~ SLG51000_LDO7_MINV =
 * 0x2060, 0x2260, 0x2360, 0x2560, 0x2760, 0x2960, 0x3160
 */
#define SLG51000_MINV_SHIFT                     0
#define SLG51000_MINV_MASK                      (0xff << 0)

/* SLG51000_LDO1_MAXV ~ SLG51000_LDO7_MAXV =
 * 0x2061, 0x2261, 0x2361, 0x2561, 0x2761, 0x2961, 0x3161
 */
#define SLG51000_MAXV_SHIFT                     0
#define SLG51000_MAXV_MASK                      (0xff << 0)

/* SLG51000_LDO1_MISC1 = 0x2064, SLG51000_LDO2_MISC1 = 0x2264 */
#define SLG51000_SEL_VRANGE_SHIFT               0
#define SLG51000_SEL_VRANGE_MASK                (0x01 << 0)

/* SLG51000_LDO1_VSEL_ACTUAL ~ SLG51000_LDO7_VSEL_ACTUAL =
 * 0x2065, 0x2265, 0x2366, 0x2566, 0x2767, 0x2967, 0x3166
 */
#define SLG51000_VSEL_ACTUAL_SHIFT              0
#define SLG51000_VSEL_ACTUAL_MASK               (0xff << 0)

/* SLG51000_LDO1_EVENT ~ SLG51000_LDO7_EVENT =
 * 0x20c0, 0x22c0, 0x23c0, 0x25c0, 0x27c0, 0x29c0, 0x31c0
 */
#define SLG51000_EVT_ILIM_FLAG_SHIFT            0
#define SLG51000_EVT_ILIM_FLAG_MASK             (0x01 << 0)
#define SLG51000_EVT_VOUT_OK_FLAG_SHIFT         1
#define SLG51000_EVT_VOUT_OK_FLAG_MASK          (0x01 << 1)

/* SLG51000_LDO1_STATUS ~ SLG51000_LDO7_STATUS =
 * 0x20c1, 0x22c1, 0x23c1, 0x25c1, 0x27c1, 0x29c1, 0x31c1
 */
#define SLG51000_STA_ILIM_FLAG_SHIFT            0
#define SLG51000_STA_ILIM_FLAG_MASK             (0x01 << 0)
#define SLG51000_STA_VOUT_OK_FLAG_SHIFT         1
#define SLG51000_STA_VOUT_OK_FLAG_MASK          (0x01 << 1)

/* SLG51000_LDO1_IRQ_MASK ~ SLG51000_LDO7_IRQ_MASK =
 * 0x20c2, 0x22c2, 0x23c2, 0x25c2, 0x27c2, 0x29c2, 0x31c2
 */
#define SLG51000_IRQ_ILIM_FLAG_SHIFT            0
#define SLG51000_IRQ_ILIM_FLAG_MASK             (0x01 << 0)

/* SLG51000_LDO3_CONF1 ~ SLG51000_LDO7_CONF1 =
 * 0x2364, 0x2564, 0x2765, 0x2965, 0x3164
 */
#define SLG51000_SEL_START_ILIM_SHIFT           0
#define SLG51000_SEL_START_ILIM_MASK            (0x7f << 0)

/* SLG51000_LDO3_CONF2 ~ SLG51000_LDO7_CONF2 =
 * 0x2365, 0x2565, 0x2766, 0x2966, 0x3165
 */
#define SLG51000_SEL_FUNC_ILIM_SHIFT            0
#define SLG51000_SEL_FUNC_ILIM_MASK             (0x7f << 0)

/* SLG51000_LDO5_TRIM2 = 0x2763, SLG51000_LDO6_TRIM2 = 0x2963 */
#define SLG51000_SEL_BYP_SLEW_RATE_SHIFT        2
#define SLG51000_SEL_BYP_SLEW_RATE_MASK         (0x03 << 2)
#define SLG51000_SEL_BYP_VGATE_SHIFT            1
#define SLG51000_SEL_BYP_VGATE_MASK             (0x01 << 1)
#define SLG51000_SEL_BYP_MODE_SHIFT             0
#define SLG51000_SEL_BYP_MODE_MASK              (0x01 << 0)

/* SLG51000_OTP_EVENT = 0x782b */
#define SLG51000_EVT_CRC_SHIFT                  0
#define SLG51000_EVT_CRC_MASK                   (0x01 << 0)

/* SLG51000_OTP_IRQ_MASK = 0x782d */
#define SLG51000_IRQ_CRC_SHIFT                  0
#define SLG51000_IRQ_CRC_MASK                   (0x01 << 0)

/* SLG51000_OTP_LOCK_OTP_PROG = 0x78fe */
#define SLG51000_LOCK_OTP_PROG_SHIFT            0
#define SLG51000_LOCK_OTP_PROG_MASK             (0x01 << 0)

/* SLG51000_OTP_LOCK_CTRL = 0x78ff */
#define SLG51000_LOCK_DFT_SHIFT                 1
#define SLG51000_LOCK_DFT_MASK                  (0x01 << 1)
#define SLG51000_LOCK_RWT_SHIFT                 0
#define SLG51000_LOCK_RWT_MASK                  (0x01 << 0)

/* SLG51000_LOCK_GLOBAL_LOCK_CTRL1 = 0x8000 */
#define SLG51000_LDO7_LOCK_SHIFT                7
#define SLG51000_LDO7_LOCK_MASK                 (0x01 << 7)
#define SLG51000_LDO6_LOCK_SHIFT                6
#define SLG51000_LDO6_LOCK_MASK                 (0x01 << 6)
#define SLG51000_LDO5_LOCK_SHIFT                5
#define SLG51000_LDO5_LOCK_MASK                 (0x01 << 5)
#define SLG51000_LDO4_LOCK_SHIFT                4
#define SLG51000_LDO4_LOCK_MASK                 (0x01 << 4)
#define SLG51000_LDO3_LOCK_SHIFT                3
#define SLG51000_LDO3_LOCK_MASK                 (0x01 << 3)
#define SLG51000_LDO2_LOCK_SHIFT                2
#define SLG51000_LDO2_LOCK_MASK                 (0x01 << 2)
#define SLG51000_LDO1_LOCK_SHIFT                1
#define SLG51000_LDO1_LOCK_MASK                 (0x01 << 1)

#endif /* __SLG51000_REGISTERS_H__ */

