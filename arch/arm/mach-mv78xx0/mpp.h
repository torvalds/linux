/*
 * linux/arch/arm/mach-mv78xx0/mpp.h -- Multi Purpose Pins
 *
 *
 * sebastien requiem <sebastien@requiem.fr>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MV78X00_MPP_H
#define __MV78X00_MPP_H

#define MPP(_num, _sel, _in, _out, _78100_A0) (\
    /* MPP number */        ((_num) & 0xff) | \
    /* MPP select value */        (((_sel) & 0xf) << 8) | \
    /* may be input signal */    ((!!(_in)) << 12) | \
    /* may be output signal */    ((!!(_out)) << 13) | \
    /* available on A0 */    ((!!(_78100_A0)) << 14))

                /*   num sel  i  o  78100_A0  */

#define MPP_78100_A0_MASK    MPP(0, 0x0, 0, 0, 1)

#define MPP0_GPIO        MPP(0, 0x0, 1, 1, 1)
#define MPP0_GE0_COL        MPP(0, 0x1, 0, 0, 1)
#define MPP0_GE1_TXCLK        MPP(0, 0x2, 0, 0, 1)
#define MPP0_UNUSED        MPP(0, 0x3, 0, 0, 1)

#define MPP1_GPIO        MPP(1, 0x0, 1, 1, 1)
#define MPP1_GE0_RXERR        MPP(1, 0x1, 0, 0, 1)
#define MPP1_GE1_TXCTL        MPP(1, 0x2, 0, 0, 1)
#define MPP1_UNUSED        MPP(1, 0x3, 0, 0, 1)

#define MPP2_GPIO        MPP(2, 0x0, 1, 1, 1)
#define MPP2_GE0_CRS        MPP(2, 0x1, 0, 0, 1)
#define MPP2_GE1_RXCTL        MPP(2, 0x2, 0, 0, 1)
#define MPP2_UNUSED        MPP(2, 0x3, 0, 0, 1)

#define MPP3_GPIO        MPP(3, 0x0, 1, 1, 1)
#define MPP3_GE0_TXERR        MPP(3, 0x1, 0, 0, 1)
#define MPP3_GE1_RXCLK        MPP(3, 0x2, 0, 0, 1)
#define MPP3_UNUSED        MPP(3, 0x3, 0, 0, 1)

#define MPP4_GPIO        MPP(4, 0x0, 1, 1, 1)
#define MPP4_GE0_TXD4        MPP(4, 0x1, 0, 0, 1)
#define MPP4_GE1_TXD0        MPP(4, 0x2, 0, 0, 1)
#define MPP4_UNUSED        MPP(4, 0x3, 0, 0, 1)

#define MPP5_GPIO        MPP(5, 0x0, 1, 1, 1)
#define MPP5_GE0_TXD5        MPP(5, 0x1, 0, 0, 1)
#define MPP5_GE1_TXD1        MPP(5, 0x2, 0, 0, 1)
#define MPP5_UNUSED        MPP(5, 0x3, 0, 0, 1)

#define MPP6_GPIO        MPP(6, 0x0, 1, 1, 1)
#define MPP6_GE0_TXD6        MPP(6, 0x1, 0, 0, 1)
#define MPP6_GE1_TXD2        MPP(6, 0x2, 0, 0, 1)
#define MPP6_UNUSED        MPP(6, 0x3, 0, 0, 1)

#define MPP7_GPIO        MPP(7, 0x0, 1, 1, 1)
#define MPP7_GE0_TXD7        MPP(7, 0x1, 0, 0, 1)
#define MPP7_GE1_TXD3        MPP(7, 0x2, 0, 0, 1)
#define MPP7_UNUSED        MPP(7, 0x3, 0, 0, 1)

#define MPP8_GPIO        MPP(8, 0x0, 1, 1, 1)
#define MPP8_GE0_RXD4        MPP(8, 0x1, 0, 0, 1)
#define MPP8_GE1_RXD0        MPP(8, 0x2, 0, 0, 1)
#define MPP8_UNUSED        MPP(8, 0x3, 0, 0, 1)

#define MPP9_GPIO        MPP(9, 0x0, 1, 1, 1)
#define MPP9_GE0_RXD5        MPP(9, 0x1, 0, 0, 1)
#define MPP9_GE1_RXD1        MPP(9, 0x2, 0, 0, 1)
#define MPP9_UNUSED        MPP(9, 0x3, 0, 0, 1)

#define MPP10_GPIO        MPP(10, 0x0, 1, 1, 1)
#define MPP10_GE0_RXD6        MPP(10, 0x1, 0, 0, 1)
#define MPP10_GE1_RXD2        MPP(10, 0x2, 0, 0, 1)
#define MPP10_UNUSED        MPP(10, 0x3, 0, 0, 1)

#define MPP11_GPIO        MPP(11, 0x0, 1, 1, 1)
#define MPP11_GE0_RXD7        MPP(11, 0x1, 0, 0, 1)
#define MPP11_GE1_RXD3        MPP(11, 0x2, 0, 0, 1)
#define MPP11_UNUSED        MPP(11, 0x3, 0, 0, 1)

#define MPP12_GPIO        MPP(12, 0x0, 1, 1, 1)
#define MPP12_M_BB        MPP(12, 0x3, 0, 0, 1)
#define MPP12_UA0_CTSn        MPP(12, 0x4, 0, 0, 1)
#define MPP12_NAND_FLASH_REn0    MPP(12, 0x5, 0, 0, 1)
#define MPP12_TDM0_SCSn        MPP(12, 0X6, 0, 0, 1)
#define MPP12_UNUSED        MPP(12, 0x1, 0, 0, 1)

#define MPP13_GPIO        MPP(13, 0x0, 1, 1, 1)
#define MPP13_SYSRST_OUTn    MPP(13, 0x3, 0, 0, 1)
#define MPP13_UA0_RTSn        MPP(13, 0x4, 0, 0, 1)
#define MPP13_NAN_FLASH_WEn0    MPP(13, 0x5, 0, 0, 1)
#define MPP13_TDM_SCLK        MPP(13, 0x6, 0, 0, 1)
#define MPP13_UNUSED        MPP(13, 0x1, 0, 0, 1)

#define MPP14_GPIO        MPP(14, 0x0, 1, 1, 1)
#define MPP14_SATA1_ACTn    MPP(14, 0x3, 0, 0, 1)
#define MPP14_UA1_CTSn        MPP(14, 0x4, 0, 0, 1)
#define MPP14_NAND_FLASH_REn1    MPP(14, 0x5, 0, 0, 1)
#define MPP14_TDM_SMOSI        MPP(14, 0x6, 0, 0, 1)
#define MPP14_UNUSED        MPP(14, 0x1, 0, 0, 1)

#define MPP15_GPIO        MPP(15, 0x0, 1, 1, 1)
#define MPP15_SATA0_ACTn    MPP(15, 0x3, 0, 0, 1)
#define MPP15_UA1_RTSn        MPP(15, 0x4, 0, 0, 1)
#define MPP15_NAND_FLASH_WEn1    MPP(15, 0x5, 0, 0, 1)
#define MPP15_TDM_SMISO        MPP(15, 0x6, 0, 0, 1)
#define MPP15_UNUSED        MPP(15, 0x1, 0, 0, 1)

#define MPP16_GPIO        MPP(16, 0x0, 1, 1, 1)
#define MPP16_SATA1_PRESENTn    MPP(16, 0x3, 0, 0, 1)
#define MPP16_UA2_TXD        MPP(16, 0x4, 0, 0, 1)
#define MPP16_NAND_FLASH_REn3    MPP(16, 0x5, 0, 0, 1)
#define MPP16_TDM_INTn        MPP(16, 0x6, 0, 0, 1)
#define MPP16_UNUSED        MPP(16, 0x1, 0, 0, 1)


#define MPP17_GPIO        MPP(17, 0x0, 1, 1, 1)
#define MPP17_SATA0_PRESENTn    MPP(17, 0x3, 0, 0, 1)
#define MPP17_UA2_RXD        MPP(17, 0x4, 0, 0, 1)
#define MPP17_NAND_FLASH_WEn3    MPP(17, 0x5, 0, 0, 1)
#define MPP17_TDM_RSTn        MPP(17, 0x6, 0, 0, 1)
#define MPP17_UNUSED        MPP(17, 0x1, 0, 0, 1)


#define MPP18_GPIO        MPP(18, 0x0, 1, 1, 1)
#define MPP18_UA0_CTSn        MPP(18, 0x4, 0, 0, 1)
#define MPP18_BOOT_FLASH_REn    MPP(18, 0x5, 0, 0, 1)
#define MPP18_UNUSED        MPP(18, 0x1, 0, 0, 1)



#define MPP19_GPIO        MPP(19, 0x0, 1, 1, 1)
#define MPP19_UA0_CTSn        MPP(19, 0x4, 0, 0, 1)
#define MPP19_BOOT_FLASH_WEn    MPP(19, 0x5, 0, 0, 1)
#define MPP19_UNUSED        MPP(19, 0x1, 0, 0, 1)


#define MPP20_GPIO        MPP(20, 0x0, 1, 1, 1)
#define MPP20_UA1_CTSs        MPP(20, 0x4, 0, 0, 1)
#define MPP20_TDM_PCLK        MPP(20, 0x6, 0, 0, 0)
#define MPP20_UNUSED        MPP(20, 0x1, 0, 0, 1)



#define MPP21_GPIO        MPP(21, 0x0, 1, 1, 1)
#define MPP21_UA1_CTSs        MPP(21, 0x4, 0, 0, 1)
#define MPP21_TDM_FSYNC        MPP(21, 0x6, 0, 0, 0)
#define MPP21_UNUSED        MPP(21, 0x1, 0, 0, 1)



#define MPP22_GPIO        MPP(22, 0x0, 1, 1, 1)
#define MPP22_UA3_TDX        MPP(22, 0x4, 0, 0, 1)
#define MPP22_NAND_FLASH_REn2    MPP(22, 0x5, 0, 0, 1)
#define MPP22_TDM_DRX        MPP(22, 0x6, 0, 0, 1)
#define MPP22_UNUSED        MPP(22, 0x1, 0, 0, 1)



#define MPP23_GPIO        MPP(23, 0x0, 1, 1, 1)
#define MPP23_UA3_RDX        MPP(23, 0x4, 0, 0, 1)
#define MPP23_NAND_FLASH_WEn2    MPP(23, 0x5, 0, 0, 1)
#define MPP23_TDM_DTX        MPP(23, 0x6, 0, 0, 1)
#define MPP23_UNUSED        MPP(23, 0x1, 0, 0, 1)


#define MPP24_GPIO        MPP(24, 0x0, 1, 1, 1)
#define MPP24_UA2_TXD        MPP(24, 0x4, 0, 0, 1)
#define MPP24_TDM_INTn        MPP(24, 0x6, 0, 0, 1)
#define MPP24_UNUSED        MPP(24, 0x1, 0, 0, 1)


#define MPP25_GPIO        MPP(25, 0x0, 1, 1, 1)
#define MPP25_UA2_RXD        MPP(25, 0x4, 0, 0, 1)
#define MPP25_TDM_RSTn        MPP(25, 0x6, 0, 0, 1)
#define MPP25_UNUSED        MPP(25, 0x1, 0, 0, 1)


#define MPP26_GPIO        MPP(26, 0x0, 1, 1, 1)
#define MPP26_UA2_CTSn        MPP(26, 0x4, 0, 0, 1)
#define MPP26_TDM_PCLK        MPP(26, 0x6, 0, 0, 1)
#define MPP26_UNUSED        MPP(26, 0x1, 0, 0, 1)


#define MPP27_GPIO        MPP(27, 0x0, 1, 1, 1)
#define MPP27_UA2_RTSn        MPP(27, 0x4, 0, 0, 1)
#define MPP27_TDM_FSYNC        MPP(27, 0x6, 0, 0, 1)
#define MPP27_UNUSED        MPP(27, 0x1, 0, 0, 1)


#define MPP28_GPIO        MPP(28, 0x0, 1, 1, 1)
#define MPP28_UA3_TXD        MPP(28, 0x4, 0, 0, 1)
#define MPP28_TDM_DRX        MPP(28, 0x6, 0, 0, 1)
#define MPP28_UNUSED        MPP(28, 0x1, 0, 0, 1)

#define MPP29_GPIO        MPP(29, 0x0, 1, 1, 1)
#define MPP29_UA3_RXD        MPP(29, 0x4, 0, 0, 1)
#define MPP29_SYSRST_OUTn    MPP(29, 0x5, 0, 0, 1)
#define MPP29_TDM_DTX        MPP(29, 0x6, 0, 0, 1)
#define MPP29_UNUSED        MPP(29, 0x1, 0, 0, 1)

#define MPP30_GPIO        MPP(30, 0x0, 1, 1, 1)
#define MPP30_UA3_CTSn        MPP(30, 0x4, 0, 0, 1)
#define MPP30_UNUSED        MPP(30, 0x1, 0, 0, 1)

#define MPP31_GPIO        MPP(31, 0x0, 1, 1, 1)
#define MPP31_UA3_RTSn        MPP(31, 0x4, 0, 0, 1)
#define MPP31_TDM1_SCSn        MPP(31, 0x6, 0, 0, 1)
#define MPP31_UNUSED        MPP(31, 0x1, 0, 0, 1)


#define MPP32_GPIO        MPP(32, 0x1, 1, 1, 1)
#define MPP32_UA3_TDX        MPP(32, 0x4, 0, 0, 1)
#define MPP32_SYSRST_OUTn    MPP(32, 0x5, 0, 0, 1)
#define MPP32_TDM0_RXQ        MPP(32, 0x6, 0, 0, 1)
#define MPP32_UNUSED        MPP(32, 0x3, 0, 0, 1)


#define MPP33_GPIO        MPP(33, 0x1, 1, 1, 1)
#define MPP33_UA3_RDX        MPP(33, 0x4, 0, 0, 1)
#define MPP33_TDM0_TXQ        MPP(33, 0x6, 0, 0, 1)
#define MPP33_UNUSED        MPP(33, 0x3, 0, 0, 1)



#define MPP34_GPIO        MPP(34, 0x1, 1, 1, 1)
#define MPP34_UA2_TDX        MPP(34, 0x4, 0, 0, 1)
#define MPP34_TDM1_RXQ        MPP(34, 0x6, 0, 0, 1)
#define MPP34_UNUSED        MPP(34, 0x3, 0, 0, 1)



#define MPP35_GPIO        MPP(35, 0x1, 1, 1, 1)
#define MPP35_UA2_RDX        MPP(35, 0x4, 0, 0, 1)
#define MPP35_TDM1_TXQ        MPP(35, 0x6, 0, 0, 1)
#define MPP35_UNUSED        MPP(35, 0x3, 0, 0, 1)

#define MPP36_GPIO        MPP(36, 0x1, 1, 1, 1)
#define MPP36_UA0_CTSn        MPP(36, 0x2, 0, 0, 1)
#define MPP36_UA2_TDX        MPP(36, 0x4, 0, 0, 1)
#define MPP36_TDM0_SCSn        MPP(36, 0x6, 0, 0, 1)
#define MPP36_UNUSED        MPP(36, 0x3, 0, 0, 1)


#define MPP37_GPIO        MPP(37, 0x1, 1, 1, 1)
#define MPP37_UA0_RTSn        MPP(37, 0x2, 0, 0, 1)
#define MPP37_UA2_RXD        MPP(37, 0x4, 0, 0, 1)
#define MPP37_SYSRST_OUTn    MPP(37, 0x5, 0, 0, 1)
#define MPP37_TDM_SCLK        MPP(37, 0x6, 0, 0, 1)
#define MPP37_UNUSED        MPP(37, 0x3, 0, 0, 1)




#define MPP38_GPIO        MPP(38, 0x1, 1, 1, 1)
#define MPP38_UA1_CTSn        MPP(38, 0x2, 0, 0, 1)
#define MPP38_UA3_TXD        MPP(38, 0x4, 0, 0, 1)
#define MPP38_SYSRST_OUTn    MPP(38, 0x5, 0, 0, 1)
#define MPP38_TDM_SMOSI        MPP(38, 0x6, 0, 0, 1)
#define MPP38_UNUSED        MPP(38, 0x3, 0, 0, 1)




#define MPP39_GPIO        MPP(39, 0x1, 1, 1, 1)
#define MPP39_UA1_RTSn        MPP(39, 0x2, 0, 0, 1)
#define MPP39_UA3_RXD        MPP(39, 0x4, 0, 0, 1)
#define MPP39_SYSRST_OUTn    MPP(39, 0x5, 0, 0, 1)
#define MPP39_TDM_SMISO        MPP(39, 0x6, 0, 0, 1)
#define MPP39_UNUSED        MPP(39, 0x3, 0, 0, 1)



#define MPP40_GPIO        MPP(40, 0x1, 1, 1, 1)
#define MPP40_TDM_INTn        MPP(40, 0x6, 0, 0, 1)
#define MPP40_UNUSED        MPP(40, 0x0, 0, 0, 1)



#define MPP41_GPIO        MPP(41, 0x1, 1, 1, 1)
#define MPP41_TDM_RSTn        MPP(41, 0x6, 0, 0, 1)
#define MPP41_UNUSED        MPP(41, 0x0, 0, 0, 1)



#define MPP42_GPIO        MPP(42, 0x1, 1, 1, 1)
#define MPP42_TDM_PCLK        MPP(42, 0x6, 0, 0, 1)
#define MPP42_UNUSED        MPP(42, 0x0, 0, 0, 1)



#define MPP43_GPIO        MPP(43, 0x1, 1, 1, 1)
#define MPP43_TDM_FSYNC        MPP(43, 0x6, 0, 0, 1)
#define MPP43_UNUSED        MPP(43, 0x0, 0, 0, 1)



#define MPP44_GPIO        MPP(44, 0x1, 1, 1, 1)
#define MPP44_TDM_DRX        MPP(44, 0x6, 0, 0, 1)
#define MPP44_UNUSED        MPP(44, 0x0, 0, 0, 1)



#define MPP45_GPIO        MPP(45, 0x1, 1, 1, 1)
#define MPP45_SATA0_ACTn    MPP(45, 0x3, 0, 0, 1)
#define MPP45_TDM_DRX        MPP(45, 0x6, 0, 0, 1)
#define MPP45_UNUSED        MPP(45, 0x0, 0, 0, 1)


#define MPP46_GPIO        MPP(46, 0x1, 1, 1, 1)
#define MPP46_TDM_SCSn        MPP(46, 0x6, 0, 0, 1)
#define MPP46_UNUSED        MPP(46, 0x0, 0, 0, 1)


#define MPP47_GPIO        MPP(47, 0x1, 1, 1, 1)
#define MPP47_UNUSED        MPP(47, 0x0, 0, 0, 1)



#define MPP48_GPIO        MPP(48, 0x1, 1, 1, 1)
#define MPP48_SATA1_ACTn    MPP(48, 0x3, 0, 0, 1)
#define MPP48_UNUSED        MPP(48, 0x2, 0, 0, 1)



#define MPP49_GPIO        MPP(49, 0x1, 1, 1, 1)
#define MPP49_SATA0_ACTn    MPP(49, 0x3, 0, 0, 1)
#define MPP49_M_BB        MPP(49, 0x4, 0, 0, 1)
#define MPP49_UNUSED        MPP(49, 0x2, 0, 0, 1)


#define MPP_MAX            49

void mv78xx0_mpp_conf(unsigned int *mpp_list);

#endif
