#ifndef __ARCH_DOVE_MPP_CODED_H
#define __ARCH_DOVE_MPP_CODED_H

#define MPP(_num, _mode, _pmu, _grp, _au1, _nfc) (	\
/* MPP/group number */		((_num) & 0xff) |		\
/* MPP select value */		(((_mode) & 0xf) << 8) |	\
/* MPP PMU */			((!!(_pmu)) << 12) |		\
/* group flag */		((!!(_grp)) << 13) |		\
/* AU1 flag */			((!!(_au1)) << 14) |		\
/* NFCE flag */			((!!(_nfc)) << 15))

#define MPP_MAX	71

#define MPP_NUM(x)    ((x) & 0xff)
#define MPP_SEL(x)    (((x) >> 8) & 0xf)

#define MPP_PMU_MASK		MPP(0, 0x0, 1, 0, 0, 0)
#define MPP_GRP_MASK		MPP(0, 0x0, 0, 1, 0, 0)
#define MPP_AU1_MASK		MPP(0, 0x0, 0, 0, 1, 0)
#define MPP_NFC_MASK		MPP(0, 0x0, 0, 0, 0, 1)

#define MPP_END			MPP(0xff, 0xf, 1, 1, 1, 1)

#define MPP_PMU_DRIVE_0		0x1
#define MPP_PMU_DRIVE_1		0x2
#define MPP_PMU_SDI		0x3
#define MPP_PMU_CPU_PWRDWN	0x4
#define MPP_PMU_STBY_PWRDWN	0x5
#define MPP_PMU_CORE_PWR_GOOD	0x8
#define MPP_PMU_BAT_FAULT	0xa
#define MPP_PMU_EXT0_WU		0xb
#define MPP_PMU_EXT1_WU		0xc
#define MPP_PMU_EXT2_WU		0xd
#define MPP_PMU_BLINK		0xe
#define MPP_PMU(_num, _mode)	MPP((_num), MPP_PMU_##_mode, 1, 0, 0, 0)

#define MPP_PIN(_num, _mode)	MPP((_num), (_mode), 0, 0, 0, 0)
#define MPP_GRP(_grp, _mode)	MPP((_grp), (_mode), 0, 1, 0, 0)
#define MPP_GRP_AU1(_mode)	MPP(0, (_mode), 0, 0, 1, 0)
#define MPP_GRP_NFC(_mode)	MPP(0, (_mode), 0, 0, 0, 1)

#define MPP0_GPIO0		MPP_PIN(0, 0x0)
#define MPP0_UA2_RTSn		MPP_PIN(0, 0x2)
#define MPP0_SDIO0_CD		MPP_PIN(0, 0x3)
#define MPP0_LCD0_PWM		MPP_PIN(0, 0xf)

#define MPP1_GPIO1		MPP_PIN(1, 0x0)
#define MPP1_UA2_CTSn		MPP_PIN(1, 0x2)
#define MPP1_SDIO0_WP		MPP_PIN(1, 0x3)
#define MPP1_LCD1_PWM		MPP_PIN(1, 0xf)

#define MPP2_GPIO2		MPP_PIN(2, 0x0)
#define MPP2_SATA_PRESENT	MPP_PIN(2, 0x1)
#define MPP2_UA2_TXD		MPP_PIN(2, 0x2)
#define MPP2_SDIO0_BUS_POWER	MPP_PIN(2, 0x3)
#define MPP2_UA_RTSn1		MPP_PIN(2, 0x4)

#define MPP3_GPIO3		MPP_PIN(3, 0x0)
#define MPP3_SATA_ACT		MPP_PIN(3, 0x1)
#define MPP3_UA2_RXD		MPP_PIN(3, 0x2)
#define MPP3_SDIO0_LED_CTRL	MPP_PIN(3, 0x3)
#define MPP3_UA_CTSn1		MPP_PIN(3, 0x4)
#define MPP3_SPI_LCD_CS1	MPP_PIN(3, 0xf)

#define MPP4_GPIO4		MPP_PIN(4, 0x0)
#define MPP4_UA3_RTSn		MPP_PIN(4, 0x2)
#define MPP4_SDIO1_CD		MPP_PIN(4, 0x3)
#define MPP4_SPI_1_MISO		MPP_PIN(4, 0x4)

#define MPP5_GPIO5		MPP_PIN(5, 0x0)
#define MPP5_UA3_CTSn		MPP_PIN(5, 0x2)
#define MPP5_SDIO1_WP		MPP_PIN(5, 0x3)
#define MPP5_SPI_1_CS		MPP_PIN(5, 0x4)

#define MPP6_GPIO6		MPP_PIN(6, 0x0)
#define MPP6_UA3_TXD		MPP_PIN(6, 0x2)
#define MPP6_SDIO1_BUS_POWER	MPP_PIN(6, 0x3)
#define MPP6_SPI_1_MOSI		MPP_PIN(6, 0x4)

#define MPP7_GPIO7		MPP_PIN(7, 0x0)
#define MPP7_UA3_RXD		MPP_PIN(7, 0x2)
#define MPP7_SDIO1_LED_CTRL	MPP_PIN(7, 0x3)
#define MPP7_SPI_1_SCK		MPP_PIN(7, 0x4)

#define MPP8_GPIO8		MPP_PIN(8, 0x0)
#define MPP8_WD_RST_OUT		MPP_PIN(8, 0x1)

#define MPP9_GPIO9		MPP_PIN(9, 0x0)
#define MPP9_PEX1_CLKREQn	MPP_PIN(9, 0x5)

#define MPP10_GPIO10		MPP_PIN(10, 0x0)
#define MPP10_SSP_SCLK		MPP_PIN(10, 0x5)

#define MPP11_GPIO11		MPP_PIN(11, 0x0)
#define MPP11_SATA_PRESENT	MPP_PIN(11, 0x1)
#define MPP11_SATA_ACT		MPP_PIN(11, 0x2)
#define MPP11_SDIO0_LED_CTRL	MPP_PIN(11, 0x3)
#define MPP11_SDIO1_LED_CTRL	MPP_PIN(11, 0x4)
#define MPP11_PEX0_CLKREQn	MPP_PIN(11, 0x5)

#define MPP12_GPIO12		MPP_PIN(12, 0x0)
#define MPP12_SATA_ACT		MPP_PIN(12, 0x1)
#define MPP12_UA2_RTSn		MPP_PIN(12, 0x2)
#define MPP12_AD0_I2S_EXT_MCLK	MPP_PIN(12, 0x3)
#define MPP12_SDIO1_CD		MPP_PIN(12, 0x4)

#define MPP13_GPIO13		MPP_PIN(13, 0x0)
#define MPP13_UA2_CTSn		MPP_PIN(13, 0x2)
#define MPP13_AD1_I2S_EXT_MCLK	MPP_PIN(13, 0x3)
#define MPP13_SDIO1WP		MPP_PIN(13, 0x4)
#define MPP13_SSP_EXTCLK	MPP_PIN(13, 0x5)

#define MPP14_GPIO14		MPP_PIN(14, 0x0)
#define MPP14_UA2_TXD		MPP_PIN(14, 0x2)
#define MPP14_SDIO1_BUS_POWER	MPP_PIN(14, 0x4)
#define MPP14_SSP_RXD		MPP_PIN(14, 0x5)

#define MPP15_GPIO15		MPP_PIN(15, 0x0)
#define MPP15_UA2_RXD		MPP_PIN(15, 0x2)
#define MPP15_SDIO1_LED_CTRL	MPP_PIN(15, 0x4)
#define MPP15_SSP_SFRM		MPP_PIN(15, 0x5)

#define MPP16_GPIO16		MPP_PIN(16, 0x0)
#define MPP16_UA3_RTSn		MPP_PIN(16, 0x2)
#define MPP16_SDIO0_CD		MPP_PIN(16, 0x3)
#define MPP16_SPI_LCD_CS1	MPP_PIN(16, 0x4)
#define MPP16_AC97_SDATA_IN1	MPP_PIN(16, 0x5)

#define MPP17_GPIO17		MPP_PIN(17, 0x0)
#define MPP17_AC97_SYSCLK_OUT	MPP_PIN(17, 0x1)
#define MPP17_UA3_CTSn		MPP_PIN(17, 0x2)
#define MPP17_SDIO0_WP		MPP_PIN(17, 0x3)
#define MPP17_TW_SDA2		MPP_PIN(17, 0x4)
#define MPP17_AC97_SDATA_IN2	MPP_PIN(17, 0x5)

#define MPP18_GPIO18		MPP_PIN(18, 0x0)
#define MPP18_UA3_TXD		MPP_PIN(18, 0x2)
#define MPP18_SDIO0_BUS_POWER	MPP_PIN(18, 0x3)
#define MPP18_LCD0_PWM		MPP_PIN(18, 0x4)
#define MPP18_AC_SDATA_IN3	MPP_PIN(18, 0x5)

#define MPP19_GPIO19		MPP_PIN(19, 0x0)
#define MPP19_UA3_RXD		MPP_PIN(19, 0x2)
#define MPP19_SDIO0_LED_CTRL	MPP_PIN(19, 0x3)
#define MPP19_TW_SCK2		MPP_PIN(19, 0x4)

#define MPP20_GPIO20		MPP_PIN(20, 0x0)
#define MPP20_AC97_SYSCLK_OUT	MPP_PIN(20, 0x1)
#define MPP20_SPI_LCD_MISO	MPP_PIN(20, 0x2)
#define MPP20_SDIO1_CD		MPP_PIN(20, 0x3)
#define MPP20_SDIO0_CD		MPP_PIN(20, 0x5)
#define MPP20_SPI_1_MISO	MPP_PIN(20, 0x6)

#define MPP21_GPIO21		MPP_PIN(21, 0x0)
#define MPP21_UA1_RTSn		MPP_PIN(21, 0x1)
#define MPP21_SPI_LCD_CS0	MPP_PIN(21, 0x2)
#define MPP21_SDIO1_WP		MPP_PIN(21, 0x3)
#define MPP21_SSP_SFRM		MPP_PIN(21, 0x4)
#define MPP21_SDIO0_WP		MPP_PIN(21, 0x5)
#define MPP21_SPI_1_CS		MPP_PIN(21, 0x6)

#define MPP22_GPIO22		MPP_PIN(22, 0x0)
#define MPP22_UA1_CTSn		MPP_PIN(22, 0x1)
#define MPP22_SPI_LCD_MOSI	MPP_PIN(22, 0x2)
#define MPP22_SDIO1_BUS_POWER	MPP_PIN(22, 0x3)
#define MPP22_SSP_TXD		MPP_PIN(22, 0x4)
#define MPP22_SDIO0_BUS_POWER	MPP_PIN(22, 0x5)
#define MPP22_SPI_1_MOSI	MPP_PIN(22, 0x6)

#define MPP23_GPIO23		MPP_PIN(23, 0x0)
#define MPP23_SPI_LCD_SCK	MPP_PIN(23, 0x2)
#define MPP23_SDIO1_LED_CTRL	MPP_PIN(23, 0x3)
#define MPP23_SSP_SCLK		MPP_PIN(23, 0x4)
#define MPP23_SDIO0_LED_CTRL	MPP_PIN(23, 0x5)
#define MPP23_SPI_1_SCK		MPP_PIN(23, 0x6)

/* for MPP groups _num is a group index */
enum dove_mpp_grp_idx {
	MPP_24_39 = 2,
	MPP_40_45 = 0,
	MPP_46_51 = 1,
	MPP_58_61 = 5,
	MPP_62_63 = 4,
};

#define MPP24_39_GPIO		MPP_GRP(MPP_24_39, 0x1)
#define MPP24_39_CAM		MPP_GRP(MPP_24_39, 0x0)

#define MPP40_45_GPIO		MPP_GRP(MPP_40_45, 0x1)
#define MPP40_45_SD0		MPP_GRP(MPP_40_45, 0x0)

#define MPP46_51_GPIO		MPP_GRP(MPP_46_51, 0x1)
#define MPP46_51_SD1		MPP_GRP(MPP_46_51, 0x0)

#define MPP58_61_GPIO		MPP_GRP(MPP_58_61, 0x1)
#define MPP58_61_SPI		MPP_GRP(MPP_58_61, 0x0)

#define MPP62_63_GPIO		MPP_GRP(MPP_62_63, 0x1)
#define MPP62_63_UA1		MPP_GRP(MPP_62_63, 0x0)

/* The MPP[64:71] control differs from other groups */
#define MPP64_71_GPO		MPP_GRP_NFC(0x1)
#define MPP64_71_NFC		MPP_GRP_NFC(0x0)

/*
 * The MPP[52:57] functionality is encoded by 4 bits in different
 * registers. The _num field in this case encodes those bits in
 * correspodence with Table 135 of 88AP510 Functional specification
 */
#define MPP52_57_AU1		MPP_GRP_AU1(0x0)
#define MPP52_57_AU1_GPIO57	MPP_GRP_AU1(0x2)
#define MPP52_57_GPIO		MPP_GRP_AU1(0xa)
#define MPP52_57_TW_GPIO	MPP_GRP_AU1(0xb)
#define MPP52_57_AU1_SSP	MPP_GRP_AU1(0xc)
#define MPP52_57_SSP_GPIO	MPP_GRP_AU1(0xe)
#define MPP52_57_SSP_TW		MPP_GRP_AU1(0xf)

void dove_mpp_conf(unsigned int *mpp_list);

#endif	/* __ARCH_DOVE_MPP_CODED_H */
