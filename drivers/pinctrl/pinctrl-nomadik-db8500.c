#include <linux/kernel.h>
#include <linux/pinctrl/pinctrl.h>
#include "pinctrl-nomadik.h"

/* All the pins that can be used for GPIO and some other functions */
#define _GPIO(offset)		(offset)

#define DB8500_PIN_AJ5		_GPIO(0)
#define DB8500_PIN_AJ3		_GPIO(1)
#define DB8500_PIN_AH4		_GPIO(2)
#define DB8500_PIN_AH3		_GPIO(3)
#define DB8500_PIN_AH6		_GPIO(4)
#define DB8500_PIN_AG6		_GPIO(5)
#define DB8500_PIN_AF6		_GPIO(6)
#define DB8500_PIN_AG5		_GPIO(7)
#define DB8500_PIN_AD5		_GPIO(8)
#define DB8500_PIN_AE4		_GPIO(9)
#define DB8500_PIN_AF5		_GPIO(10)
#define DB8500_PIN_AG4		_GPIO(11)
#define DB8500_PIN_AC4		_GPIO(12)
#define DB8500_PIN_AF3		_GPIO(13)
#define DB8500_PIN_AE3		_GPIO(14)
#define DB8500_PIN_AC3		_GPIO(15)
#define DB8500_PIN_AD3		_GPIO(16)
#define DB8500_PIN_AD4		_GPIO(17)
#define DB8500_PIN_AC2		_GPIO(18)
#define DB8500_PIN_AC1		_GPIO(19)
#define DB8500_PIN_AB4		_GPIO(20)
#define DB8500_PIN_AB3		_GPIO(21)
#define DB8500_PIN_AA3		_GPIO(22)
#define DB8500_PIN_AA4		_GPIO(23)
#define DB8500_PIN_AB2		_GPIO(24)
#define DB8500_PIN_Y4		_GPIO(25)
#define DB8500_PIN_Y2		_GPIO(26)
#define DB8500_PIN_AA2		_GPIO(27)
#define DB8500_PIN_AA1		_GPIO(28)
#define DB8500_PIN_W2		_GPIO(29)
#define DB8500_PIN_W3		_GPIO(30)
#define DB8500_PIN_V3		_GPIO(31)
#define DB8500_PIN_V2		_GPIO(32)
#define DB8500_PIN_AF2		_GPIO(33)
#define DB8500_PIN_AE1		_GPIO(34)
#define DB8500_PIN_AE2		_GPIO(35)
#define DB8500_PIN_AG2		_GPIO(36)
/* Hole */
#define DB8500_PIN_F3		_GPIO(64)
#define DB8500_PIN_F1		_GPIO(65)
#define DB8500_PIN_G3		_GPIO(66)
#define DB8500_PIN_G2		_GPIO(67)
#define DB8500_PIN_E1		_GPIO(68)
#define DB8500_PIN_E2		_GPIO(69)
#define DB8500_PIN_G5		_GPIO(70)
#define DB8500_PIN_G4		_GPIO(71)
#define DB8500_PIN_H4		_GPIO(72)
#define DB8500_PIN_H3		_GPIO(73)
#define DB8500_PIN_J3		_GPIO(74)
#define DB8500_PIN_H2		_GPIO(75)
#define DB8500_PIN_J2		_GPIO(76)
#define DB8500_PIN_H1		_GPIO(77)
#define DB8500_PIN_F4		_GPIO(78)
#define DB8500_PIN_E3		_GPIO(79)
#define DB8500_PIN_E4		_GPIO(80)
#define DB8500_PIN_D2		_GPIO(81)
#define DB8500_PIN_C1		_GPIO(82)
#define DB8500_PIN_D3		_GPIO(83)
#define DB8500_PIN_C2		_GPIO(84)
#define DB8500_PIN_D5		_GPIO(85)
#define DB8500_PIN_C6		_GPIO(86)
#define DB8500_PIN_B3		_GPIO(87)
#define DB8500_PIN_C4		_GPIO(88)
#define DB8500_PIN_E6		_GPIO(89)
#define DB8500_PIN_A3		_GPIO(90)
#define DB8500_PIN_B6		_GPIO(91)
#define DB8500_PIN_D6		_GPIO(92)
#define DB8500_PIN_B7		_GPIO(93)
#define DB8500_PIN_D7		_GPIO(94)
#define DB8500_PIN_E8		_GPIO(95)
#define DB8500_PIN_D8		_GPIO(96)
#define DB8500_PIN_D9		_GPIO(97)
/* Hole */
#define DB8500_PIN_A5		_GPIO(128)
#define DB8500_PIN_B4		_GPIO(129)
#define DB8500_PIN_C8		_GPIO(130)
#define DB8500_PIN_A12		_GPIO(131)
#define DB8500_PIN_C10		_GPIO(132)
#define DB8500_PIN_B10		_GPIO(133)
#define DB8500_PIN_B9		_GPIO(134)
#define DB8500_PIN_A9		_GPIO(135)
#define DB8500_PIN_C7		_GPIO(136)
#define DB8500_PIN_A7		_GPIO(137)
#define DB8500_PIN_C5		_GPIO(138)
#define DB8500_PIN_C9		_GPIO(139)
#define DB8500_PIN_B11		_GPIO(140)
#define DB8500_PIN_C12		_GPIO(141)
#define DB8500_PIN_C11		_GPIO(142)
#define DB8500_PIN_D12		_GPIO(143)
#define DB8500_PIN_B13		_GPIO(144)
#define DB8500_PIN_C13		_GPIO(145)
#define DB8500_PIN_D13		_GPIO(146)
#define DB8500_PIN_C15		_GPIO(147)
#define DB8500_PIN_B16		_GPIO(148)
#define DB8500_PIN_B14		_GPIO(149)
#define DB8500_PIN_C14		_GPIO(150)
#define DB8500_PIN_D17		_GPIO(151)
#define DB8500_PIN_D16		_GPIO(152)
#define DB8500_PIN_B17		_GPIO(153)
#define DB8500_PIN_C16		_GPIO(154)
#define DB8500_PIN_C19		_GPIO(155)
#define DB8500_PIN_C17		_GPIO(156)
#define DB8500_PIN_A18		_GPIO(157)
#define DB8500_PIN_C18		_GPIO(158)
#define DB8500_PIN_B19		_GPIO(159)
#define DB8500_PIN_B20		_GPIO(160)
#define DB8500_PIN_D21		_GPIO(161)
#define DB8500_PIN_D20		_GPIO(162)
#define DB8500_PIN_C20		_GPIO(163)
#define DB8500_PIN_B21		_GPIO(164)
#define DB8500_PIN_C21		_GPIO(165)
#define DB8500_PIN_A22		_GPIO(166)
#define DB8500_PIN_B24		_GPIO(167)
#define DB8500_PIN_C22		_GPIO(168)
#define DB8500_PIN_D22		_GPIO(169)
#define DB8500_PIN_C23		_GPIO(170)
#define DB8500_PIN_D23		_GPIO(171)
/* Hole */
#define DB8500_PIN_AJ27		_GPIO(192)
#define DB8500_PIN_AH27		_GPIO(193)
#define DB8500_PIN_AF27		_GPIO(194)
#define DB8500_PIN_AG28		_GPIO(195)
#define DB8500_PIN_AG26		_GPIO(196)
#define DB8500_PIN_AH24		_GPIO(197)
#define DB8500_PIN_AG25		_GPIO(198)
#define DB8500_PIN_AH23		_GPIO(199)
#define DB8500_PIN_AH26		_GPIO(200)
#define DB8500_PIN_AF24		_GPIO(201)
#define DB8500_PIN_AF25		_GPIO(202)
#define DB8500_PIN_AE23		_GPIO(203)
#define DB8500_PIN_AF23		_GPIO(204)
#define DB8500_PIN_AG23		_GPIO(205)
#define DB8500_PIN_AG24		_GPIO(206)
#define DB8500_PIN_AJ23		_GPIO(207)
#define DB8500_PIN_AH16		_GPIO(208)
#define DB8500_PIN_AG15		_GPIO(209)
#define DB8500_PIN_AJ15		_GPIO(210)
#define DB8500_PIN_AG14		_GPIO(211)
#define DB8500_PIN_AF13		_GPIO(212)
#define DB8500_PIN_AG13		_GPIO(213)
#define DB8500_PIN_AH15		_GPIO(214)
#define DB8500_PIN_AH13		_GPIO(215)
#define DB8500_PIN_AG12		_GPIO(216)
#define DB8500_PIN_AH12		_GPIO(217)
#define DB8500_PIN_AH11		_GPIO(218)
#define DB8500_PIN_AG10		_GPIO(219)
#define DB8500_PIN_AH10		_GPIO(220)
#define DB8500_PIN_AJ11		_GPIO(221)
#define DB8500_PIN_AJ9		_GPIO(222)
#define DB8500_PIN_AH9		_GPIO(223)
#define DB8500_PIN_AG9		_GPIO(224)
#define DB8500_PIN_AG8		_GPIO(225)
#define DB8500_PIN_AF8		_GPIO(226)
#define DB8500_PIN_AH7		_GPIO(227)
#define DB8500_PIN_AJ6		_GPIO(228)
#define DB8500_PIN_AG7		_GPIO(229)
#define DB8500_PIN_AF7		_GPIO(230)
/* Hole */
#define DB8500_PIN_AF28		_GPIO(256)
#define DB8500_PIN_AE29		_GPIO(257)
#define DB8500_PIN_AD29		_GPIO(258)
#define DB8500_PIN_AC29		_GPIO(259)
#define DB8500_PIN_AD28		_GPIO(260)
#define DB8500_PIN_AD26		_GPIO(261)
#define DB8500_PIN_AE26		_GPIO(262)
#define DB8500_PIN_AG29		_GPIO(263)
#define DB8500_PIN_AE27		_GPIO(264)
#define DB8500_PIN_AD27		_GPIO(265)
#define DB8500_PIN_AC28		_GPIO(266)
#define DB8500_PIN_AC27		_GPIO(267)

/*
 * The names of the pins are denoted by GPIO number and ball name, even
 * though they can be used for other things than GPIO, this is the first
 * column in the table of the data sheet and often used on schematics and
 * such.
 */
static const struct pinctrl_pin_desc nmk_db8500_pins[] = {
	PINCTRL_PIN(DB8500_PIN_AJ5, "GPIO0_AJ5"),
	PINCTRL_PIN(DB8500_PIN_AJ3, "GPIO1_AJ3"),
	PINCTRL_PIN(DB8500_PIN_AH4, "GPIO2_AH4"),
	PINCTRL_PIN(DB8500_PIN_AH3, "GPIO3_AH3"),
	PINCTRL_PIN(DB8500_PIN_AH6, "GPIO4_AH6"),
	PINCTRL_PIN(DB8500_PIN_AG6, "GPIO5_AG6"),
	PINCTRL_PIN(DB8500_PIN_AF6, "GPIO6_AF6"),
	PINCTRL_PIN(DB8500_PIN_AG5, "GPIO7_AG5"),
	PINCTRL_PIN(DB8500_PIN_AD5, "GPIO8_AD5"),
	PINCTRL_PIN(DB8500_PIN_AE4, "GPIO9_AE4"),
	PINCTRL_PIN(DB8500_PIN_AF5, "GPIO10_AF5"),
	PINCTRL_PIN(DB8500_PIN_AG4, "GPIO11_AG4"),
	PINCTRL_PIN(DB8500_PIN_AC4, "GPIO12_AC4"),
	PINCTRL_PIN(DB8500_PIN_AF3, "GPIO13_AF3"),
	PINCTRL_PIN(DB8500_PIN_AE3, "GPIO14_AE3"),
	PINCTRL_PIN(DB8500_PIN_AC3, "GPIO15_AC3"),
	PINCTRL_PIN(DB8500_PIN_AD3, "GPIO16_AD3"),
	PINCTRL_PIN(DB8500_PIN_AD4, "GPIO17_AD4"),
	PINCTRL_PIN(DB8500_PIN_AC2, "GPIO18_AC2"),
	PINCTRL_PIN(DB8500_PIN_AC1, "GPIO19_AC1"),
	PINCTRL_PIN(DB8500_PIN_AB4, "GPIO20_AB4"),
	PINCTRL_PIN(DB8500_PIN_AB3, "GPIO21_AB3"),
	PINCTRL_PIN(DB8500_PIN_AA3, "GPIO22_AA3"),
	PINCTRL_PIN(DB8500_PIN_AA4, "GPIO23_AA4"),
	PINCTRL_PIN(DB8500_PIN_AB2, "GPIO24_AB2"),
	PINCTRL_PIN(DB8500_PIN_Y4, "GPIO25_Y4"),
	PINCTRL_PIN(DB8500_PIN_Y2, "GPIO26_Y2"),
	PINCTRL_PIN(DB8500_PIN_AA2, "GPIO27_AA2"),
	PINCTRL_PIN(DB8500_PIN_AA1, "GPIO28_AA1"),
	PINCTRL_PIN(DB8500_PIN_W2, "GPIO29_W2"),
	PINCTRL_PIN(DB8500_PIN_W3, "GPIO30_W3"),
	PINCTRL_PIN(DB8500_PIN_V3, "GPIO31_V3"),
	PINCTRL_PIN(DB8500_PIN_V2, "GPIO32_V2"),
	PINCTRL_PIN(DB8500_PIN_AF2, "GPIO33_AF2"),
	PINCTRL_PIN(DB8500_PIN_AE1, "GPIO34_AE1"),
	PINCTRL_PIN(DB8500_PIN_AE2, "GPIO35_AE2"),
	PINCTRL_PIN(DB8500_PIN_AG2, "GPIO36_AG2"),
	/* Hole */
	PINCTRL_PIN(DB8500_PIN_F3, "GPIO64_F3"),
	PINCTRL_PIN(DB8500_PIN_F1, "GPIO65_F1"),
	PINCTRL_PIN(DB8500_PIN_G3, "GPIO66_G3"),
	PINCTRL_PIN(DB8500_PIN_G2, "GPIO67_G2"),
	PINCTRL_PIN(DB8500_PIN_E1, "GPIO68_E1"),
	PINCTRL_PIN(DB8500_PIN_E2, "GPIO69_E2"),
	PINCTRL_PIN(DB8500_PIN_G5, "GPIO70_G5"),
	PINCTRL_PIN(DB8500_PIN_G4, "GPIO71_G4"),
	PINCTRL_PIN(DB8500_PIN_H4, "GPIO72_H4"),
	PINCTRL_PIN(DB8500_PIN_H3, "GPIO73_H3"),
	PINCTRL_PIN(DB8500_PIN_J3, "GPIO74_J3"),
	PINCTRL_PIN(DB8500_PIN_H2, "GPIO75_H2"),
	PINCTRL_PIN(DB8500_PIN_J2, "GPIO76_J2"),
	PINCTRL_PIN(DB8500_PIN_H1, "GPIO77_H1"),
	PINCTRL_PIN(DB8500_PIN_F4, "GPIO78_F4"),
	PINCTRL_PIN(DB8500_PIN_E3, "GPIO79_E3"),
	PINCTRL_PIN(DB8500_PIN_E4, "GPIO80_E4"),
	PINCTRL_PIN(DB8500_PIN_D2, "GPIO81_D2"),
	PINCTRL_PIN(DB8500_PIN_C1, "GPIO82_C1"),
	PINCTRL_PIN(DB8500_PIN_D3, "GPIO83_D3"),
	PINCTRL_PIN(DB8500_PIN_C2, "GPIO84_C2"),
	PINCTRL_PIN(DB8500_PIN_D5, "GPIO85_D5"),
	PINCTRL_PIN(DB8500_PIN_C6, "GPIO86_C6"),
	PINCTRL_PIN(DB8500_PIN_B3, "GPIO87_B3"),
	PINCTRL_PIN(DB8500_PIN_C4, "GPIO88_C4"),
	PINCTRL_PIN(DB8500_PIN_E6, "GPIO89_E6"),
	PINCTRL_PIN(DB8500_PIN_A3, "GPIO90_A3"),
	PINCTRL_PIN(DB8500_PIN_B6, "GPIO91_B6"),
	PINCTRL_PIN(DB8500_PIN_D6, "GPIO92_D6"),
	PINCTRL_PIN(DB8500_PIN_B7, "GPIO93_B7"),
	PINCTRL_PIN(DB8500_PIN_D7, "GPIO94_D7"),
	PINCTRL_PIN(DB8500_PIN_E8, "GPIO95_E8"),
	PINCTRL_PIN(DB8500_PIN_D8, "GPIO96_D8"),
	PINCTRL_PIN(DB8500_PIN_D9, "GPIO97_D9"),
	/* Hole */
	PINCTRL_PIN(DB8500_PIN_A5, "GPIO128_A5"),
	PINCTRL_PIN(DB8500_PIN_B4, "GPIO129_B4"),
	PINCTRL_PIN(DB8500_PIN_C8, "GPIO130_C8"),
	PINCTRL_PIN(DB8500_PIN_A12, "GPIO131_A12"),
	PINCTRL_PIN(DB8500_PIN_C10, "GPIO132_C10"),
	PINCTRL_PIN(DB8500_PIN_B10, "GPIO133_B10"),
	PINCTRL_PIN(DB8500_PIN_B9, "GPIO134_B9"),
	PINCTRL_PIN(DB8500_PIN_A9, "GPIO135_A9"),
	PINCTRL_PIN(DB8500_PIN_C7, "GPIO136_C7"),
	PINCTRL_PIN(DB8500_PIN_A7, "GPIO137_A7"),
	PINCTRL_PIN(DB8500_PIN_C5, "GPIO138_C5"),
	PINCTRL_PIN(DB8500_PIN_C9, "GPIO139_C9"),
	PINCTRL_PIN(DB8500_PIN_B11, "GPIO140_B11"),
	PINCTRL_PIN(DB8500_PIN_C12, "GPIO141_C12"),
	PINCTRL_PIN(DB8500_PIN_C11, "GPIO142_C11"),
	PINCTRL_PIN(DB8500_PIN_D12, "GPIO143_D12"),
	PINCTRL_PIN(DB8500_PIN_B13, "GPIO144_B13"),
	PINCTRL_PIN(DB8500_PIN_C13, "GPIO145_C13"),
	PINCTRL_PIN(DB8500_PIN_D13, "GPIO146_D13"),
	PINCTRL_PIN(DB8500_PIN_C15, "GPIO147_C15"),
	PINCTRL_PIN(DB8500_PIN_B16, "GPIO148_B16"),
	PINCTRL_PIN(DB8500_PIN_B14, "GPIO149_B14"),
	PINCTRL_PIN(DB8500_PIN_C14, "GPIO150_C14"),
	PINCTRL_PIN(DB8500_PIN_D17, "GPIO151_D17"),
	PINCTRL_PIN(DB8500_PIN_D16, "GPIO152_D16"),
	PINCTRL_PIN(DB8500_PIN_B17, "GPIO153_B17"),
	PINCTRL_PIN(DB8500_PIN_C16, "GPIO154_C16"),
	PINCTRL_PIN(DB8500_PIN_C19, "GPIO155_C19"),
	PINCTRL_PIN(DB8500_PIN_C17, "GPIO156_C17"),
	PINCTRL_PIN(DB8500_PIN_A18, "GPIO157_A18"),
	PINCTRL_PIN(DB8500_PIN_C18, "GPIO158_C18"),
	PINCTRL_PIN(DB8500_PIN_B19, "GPIO159_B19"),
	PINCTRL_PIN(DB8500_PIN_B20, "GPIO160_B20"),
	PINCTRL_PIN(DB8500_PIN_D21, "GPIO161_D21"),
	PINCTRL_PIN(DB8500_PIN_D20, "GPIO162_D20"),
	PINCTRL_PIN(DB8500_PIN_C20, "GPIO163_C20"),
	PINCTRL_PIN(DB8500_PIN_B21, "GPIO164_B21"),
	PINCTRL_PIN(DB8500_PIN_C21, "GPIO165_C21"),
	PINCTRL_PIN(DB8500_PIN_A22, "GPIO166_A22"),
	PINCTRL_PIN(DB8500_PIN_B24, "GPIO167_B24"),
	PINCTRL_PIN(DB8500_PIN_C22, "GPIO168_C22"),
	PINCTRL_PIN(DB8500_PIN_D22, "GPIO169_D22"),
	PINCTRL_PIN(DB8500_PIN_C23, "GPIO170_C23"),
	PINCTRL_PIN(DB8500_PIN_D23, "GPIO171_D23"),
	/* Hole */
	PINCTRL_PIN(DB8500_PIN_AJ27, "GPIO192_AJ27"),
	PINCTRL_PIN(DB8500_PIN_AH27, "GPIO193_AH27"),
	PINCTRL_PIN(DB8500_PIN_AF27, "GPIO194_AF27"),
	PINCTRL_PIN(DB8500_PIN_AG28, "GPIO195_AG28"),
	PINCTRL_PIN(DB8500_PIN_AG26, "GPIO196_AG26"),
	PINCTRL_PIN(DB8500_PIN_AH24, "GPIO197_AH24"),
	PINCTRL_PIN(DB8500_PIN_AG25, "GPIO198_AG25"),
	PINCTRL_PIN(DB8500_PIN_AH23, "GPIO199_AH23"),
	PINCTRL_PIN(DB8500_PIN_AH26, "GPIO200_AH26"),
	PINCTRL_PIN(DB8500_PIN_AF24, "GPIO201_AF24"),
	PINCTRL_PIN(DB8500_PIN_AF25, "GPIO202_AF25"),
	PINCTRL_PIN(DB8500_PIN_AE23, "GPIO203_AE23"),
	PINCTRL_PIN(DB8500_PIN_AF23, "GPIO204_AF23"),
	PINCTRL_PIN(DB8500_PIN_AG23, "GPIO205_AG23"),
	PINCTRL_PIN(DB8500_PIN_AG24, "GPIO206_AG24"),
	PINCTRL_PIN(DB8500_PIN_AJ23, "GPIO207_AJ23"),
	PINCTRL_PIN(DB8500_PIN_AH16, "GPIO208_AH16"),
	PINCTRL_PIN(DB8500_PIN_AG15, "GPIO209_AG15"),
	PINCTRL_PIN(DB8500_PIN_AJ15, "GPIO210_AJ15"),
	PINCTRL_PIN(DB8500_PIN_AG14, "GPIO211_AG14"),
	PINCTRL_PIN(DB8500_PIN_AF13, "GPIO212_AF13"),
	PINCTRL_PIN(DB8500_PIN_AG13, "GPIO213_AG13"),
	PINCTRL_PIN(DB8500_PIN_AH15, "GPIO214_AH15"),
	PINCTRL_PIN(DB8500_PIN_AH13, "GPIO215_AH13"),
	PINCTRL_PIN(DB8500_PIN_AG12, "GPIO216_AG12"),
	PINCTRL_PIN(DB8500_PIN_AH12, "GPIO217_AH12"),
	PINCTRL_PIN(DB8500_PIN_AH11, "GPIO218_AH11"),
	PINCTRL_PIN(DB8500_PIN_AG10, "GPIO219_AG10"),
	PINCTRL_PIN(DB8500_PIN_AH10, "GPIO220_AH10"),
	PINCTRL_PIN(DB8500_PIN_AJ11, "GPIO221_AJ11"),
	PINCTRL_PIN(DB8500_PIN_AJ9, "GPIO222_AJ9"),
	PINCTRL_PIN(DB8500_PIN_AH9, "GPIO223_AH9"),
	PINCTRL_PIN(DB8500_PIN_AG9, "GPIO224_AG9"),
	PINCTRL_PIN(DB8500_PIN_AG8, "GPIO225_AG8"),
	PINCTRL_PIN(DB8500_PIN_AF8, "GPIO226_AF8"),
	PINCTRL_PIN(DB8500_PIN_AH7, "GPIO227_AH7"),
	PINCTRL_PIN(DB8500_PIN_AJ6, "GPIO228_AJ6"),
	PINCTRL_PIN(DB8500_PIN_AG7, "GPIO229_AG7"),
	PINCTRL_PIN(DB8500_PIN_AF7, "GPIO230_AF7"),
	/* Hole */
	PINCTRL_PIN(DB8500_PIN_AF28, "GPIO256_AF28"),
	PINCTRL_PIN(DB8500_PIN_AE29, "GPIO257_AE29"),
	PINCTRL_PIN(DB8500_PIN_AD29, "GPIO258_AD29"),
	PINCTRL_PIN(DB8500_PIN_AC29, "GPIO259_AC29"),
	PINCTRL_PIN(DB8500_PIN_AD28, "GPIO260_AD28"),
	PINCTRL_PIN(DB8500_PIN_AD26, "GPIO261_AD26"),
	PINCTRL_PIN(DB8500_PIN_AE26, "GPIO262_AE26"),
	PINCTRL_PIN(DB8500_PIN_AG29, "GPIO263_AG29"),
	PINCTRL_PIN(DB8500_PIN_AE27, "GPIO264_AE27"),
	PINCTRL_PIN(DB8500_PIN_AD27, "GPIO265_AD27"),
	PINCTRL_PIN(DB8500_PIN_AC28, "GPIO266_AC28"),
	PINCTRL_PIN(DB8500_PIN_AC27, "GPIO267_AC27"),
};

#define DB8500_GPIO_RANGE(a, b, c) { .name = "DB8500", .id = a, .base = b, \
			.pin_base = b, .npins = c }

/*
 * This matches the 32-pin gpio chips registered by the GPIO portion. This
 * cannot be const since we assign the struct gpio_chip * pointer at runtime.
 */
static struct pinctrl_gpio_range nmk_db8500_ranges[] = {
	DB8500_GPIO_RANGE(0, 0, 32),
	DB8500_GPIO_RANGE(1, 32, 5),
	DB8500_GPIO_RANGE(2, 64, 32),
	DB8500_GPIO_RANGE(3, 96, 2),
	DB8500_GPIO_RANGE(4, 128, 32),
	DB8500_GPIO_RANGE(5, 160, 12),
	DB8500_GPIO_RANGE(6, 192, 32),
	DB8500_GPIO_RANGE(7, 224, 7),
	DB8500_GPIO_RANGE(8, 256, 12),
};

/*
 * Read the pin group names like this:
 * u0_a_1    = first groups of pins for uart0 on alt function a
 * i2c2_b_2  = second group of pins for i2c2 on alt function b
 *
 * The groups are arranged as sets per altfunction column, so we can
 * mux in one group at a time by selecting the same altfunction for them
 * all. When functions require pins on different altfunctions, you need
 * to combine several groups.
 */

/* Altfunction A column */
static const unsigned u0_a_1_pins[] = { DB8500_PIN_AJ5, DB8500_PIN_AJ3,
					DB8500_PIN_AH4, DB8500_PIN_AH3 };
static const unsigned u1rxtx_a_1_pins[] = { DB8500_PIN_AH6, DB8500_PIN_AG6 };
static const unsigned u1ctsrts_a_1_pins[] = { DB8500_PIN_AF6, DB8500_PIN_AG5 };
/* Image processor I2C line, this is driven by image processor firmware */
static const unsigned ipi2c_a_1_pins[] = { DB8500_PIN_AD5, DB8500_PIN_AE4 };
static const unsigned ipi2c_a_2_pins[] = { DB8500_PIN_AF5, DB8500_PIN_AG4 };
/* MSP0 can only be on these pins, but TXD and RXD can be flipped */
static const unsigned msp0txrx_a_1_pins[] = { DB8500_PIN_AC4, DB8500_PIN_AC3 };
static const unsigned msp0tfstck_a_1_pins[] = { DB8500_PIN_AF3, DB8500_PIN_AE3 };
static const unsigned msp0rfsrck_a_1_pins[] = { DB8500_PIN_AD3, DB8500_PIN_AD4 };
/* Basic pins of the MMC/SD card 0 interface */
static const unsigned mc0_a_1_pins[] = { DB8500_PIN_AC2, DB8500_PIN_AC1,
	DB8500_PIN_AB4, DB8500_PIN_AA3, DB8500_PIN_AA4, DB8500_PIN_AB2,
	DB8500_PIN_Y4, DB8500_PIN_Y2, DB8500_PIN_AA2, DB8500_PIN_AA1 };
/* Often only 4 bits are used, then these are not needed (only used for MMC) */
static const unsigned mc0_dat47_a_1_pins[] = { DB8500_PIN_W2, DB8500_PIN_W3,
	DB8500_PIN_V3, DB8500_PIN_V2};
static const unsigned mc0dat31dir_a_1_pins[] = { DB8500_PIN_AB3 };
/* MSP1 can only be on these pins, but TXD and RXD can be flipped */
static const unsigned msp1txrx_a_1_pins[] = { DB8500_PIN_AF2, DB8500_PIN_AG2 };
static const unsigned msp1_a_1_pins[] = { DB8500_PIN_AE1, DB8500_PIN_AE2 };
/* LCD interface */
static const unsigned lcdb_a_1_pins[] = { DB8500_PIN_F3, DB8500_PIN_F1,
					  DB8500_PIN_G3, DB8500_PIN_G2 };
static const unsigned lcdvsi0_a_1_pins[] = { DB8500_PIN_E1 };
static const unsigned lcdvsi1_a_1_pins[] = { DB8500_PIN_E2 };
static const unsigned lcd_d0_d7_a_1_pins[] = {
	DB8500_PIN_G5, DB8500_PIN_G4, DB8500_PIN_H4, DB8500_PIN_H3,
	DB8500_PIN_J3, DB8500_PIN_H2, DB8500_PIN_J2, DB8500_PIN_H1 };
/* D8 thru D11 often used as TVOUT lines */
static const unsigned lcd_d8_d11_a_1_pins[] = { DB8500_PIN_F4,
	DB8500_PIN_E3, DB8500_PIN_E4, DB8500_PIN_D2 };
static const unsigned lcd_d12_d23_a_1_pins[] = {
	DB8500_PIN_C1, DB8500_PIN_D3, DB8500_PIN_C2, DB8500_PIN_D5,
	DB8500_PIN_C6, DB8500_PIN_B3, DB8500_PIN_C4, DB8500_PIN_E6,
	DB8500_PIN_A3, DB8500_PIN_B6, DB8500_PIN_D6, DB8500_PIN_B7 };
static const unsigned kp_a_1_pins[] = { DB8500_PIN_D7, DB8500_PIN_E8,
	DB8500_PIN_D8, DB8500_PIN_D9 };
static const unsigned kpskaskb_a_1_pins[] = { DB8500_PIN_D17, DB8500_PIN_D16 };
static const unsigned kp_a_2_pins[] = {
	DB8500_PIN_B17, DB8500_PIN_C16, DB8500_PIN_C19, DB8500_PIN_C17,
	DB8500_PIN_A18, DB8500_PIN_C18, DB8500_PIN_B19, DB8500_PIN_B20,
	DB8500_PIN_D21, DB8500_PIN_D20, DB8500_PIN_C20, DB8500_PIN_B21,
	DB8500_PIN_C21, DB8500_PIN_A22, DB8500_PIN_B24, DB8500_PIN_C22 };
/* MC2 has 8 data lines and no direction control, so only for (e)MMC */
static const unsigned mc2_a_1_pins[] = { DB8500_PIN_A5, DB8500_PIN_B4,
	DB8500_PIN_C8, DB8500_PIN_A12, DB8500_PIN_C10, DB8500_PIN_B10,
	DB8500_PIN_B9, DB8500_PIN_A9, DB8500_PIN_C7, DB8500_PIN_A7,
	DB8500_PIN_C5 };
static const unsigned ssp1_a_1_pins[] = { DB8500_PIN_C9, DB8500_PIN_B11,
					  DB8500_PIN_C12, DB8500_PIN_C11 };
static const unsigned ssp0_a_1_pins[] = { DB8500_PIN_D12, DB8500_PIN_B13,
					  DB8500_PIN_C13, DB8500_PIN_D13 };
static const unsigned i2c0_a_1_pins[] = { DB8500_PIN_C15, DB8500_PIN_B16 };
/*
 * Image processor GPIO pins are named "ipgpio" and have their own
 * numberspace
 */
static const unsigned ipgpio0_a_1_pins[] = { DB8500_PIN_B14 };
static const unsigned ipgpio1_a_1_pins[] = { DB8500_PIN_C14 };
/* Three modem pins named RF_PURn, MODEM_STATE and MODEM_PWREN */
static const unsigned modem_a_1_pins[] = { DB8500_PIN_D22, DB8500_PIN_C23,
					   DB8500_PIN_D23 };
/*
 * This MSP cannot switch RX and TX, SCK in a separate group since this
 * seems to be optional.
 */
static const unsigned msp2sck_a_1_pins[] = { DB8500_PIN_AJ27 };
static const unsigned msp2_a_1_pins[] = { DB8500_PIN_AH27, DB8500_PIN_AF27,
					  DB8500_PIN_AG28, DB8500_PIN_AG26 };
static const unsigned mc4_a_1_pins[] = { DB8500_PIN_AH24, DB8500_PIN_AG25,
	DB8500_PIN_AH23, DB8500_PIN_AH26, DB8500_PIN_AF24, DB8500_PIN_AF25,
	DB8500_PIN_AE23, DB8500_PIN_AF23, DB8500_PIN_AG23, DB8500_PIN_AG24,
	DB8500_PIN_AJ23 };
/* MC1 has only 4 data pins, designed for SD or SDIO exclusively */
static const unsigned mc1_a_1_pins[] = { DB8500_PIN_AH16, DB8500_PIN_AG15,
	DB8500_PIN_AJ15, DB8500_PIN_AG14, DB8500_PIN_AF13, DB8500_PIN_AG13,
	DB8500_PIN_AH15 };
static const unsigned mc1_a_2_pins[] = { DB8500_PIN_AH16, DB8500_PIN_AJ15,
	DB8500_PIN_AG14, DB8500_PIN_AF13, DB8500_PIN_AG13,DB8500_PIN_AH15 };
static const unsigned mc1dir_a_1_pins[] = { DB8500_PIN_AH13, DB8500_PIN_AG12,
	DB8500_PIN_AH12, DB8500_PIN_AH11 };
static const unsigned hsir_a_1_pins[] = { DB8500_PIN_AG10, DB8500_PIN_AH10,
	DB8500_PIN_AJ11 };
static const unsigned hsit_a_1_pins[] = { DB8500_PIN_AJ9, DB8500_PIN_AH9,
	DB8500_PIN_AG9, DB8500_PIN_AG8, DB8500_PIN_AF8 };
static const unsigned hsit_a_2_pins[] = { DB8500_PIN_AJ9, DB8500_PIN_AH9,
	DB8500_PIN_AG9, DB8500_PIN_AG8 };
static const unsigned clkout_a_1_pins[] = { DB8500_PIN_AH7, DB8500_PIN_AJ6 };
static const unsigned clkout_a_2_pins[] = { DB8500_PIN_AG7, DB8500_PIN_AF7 };
static const unsigned usb_a_1_pins[] = { DB8500_PIN_AF28, DB8500_PIN_AE29,
	DB8500_PIN_AD29, DB8500_PIN_AC29, DB8500_PIN_AD28, DB8500_PIN_AD26,
	DB8500_PIN_AE26, DB8500_PIN_AG29, DB8500_PIN_AE27, DB8500_PIN_AD27,
	DB8500_PIN_AC28, DB8500_PIN_AC27 };

/* Altfunction B column */
static const unsigned trig_b_1_pins[] = { DB8500_PIN_AJ5, DB8500_PIN_AJ3 };
static const unsigned i2c4_b_1_pins[] = { DB8500_PIN_AH6, DB8500_PIN_AG6 };
static const unsigned i2c1_b_1_pins[] = { DB8500_PIN_AF6, DB8500_PIN_AG5 };
static const unsigned i2c2_b_1_pins[] = { DB8500_PIN_AD5, DB8500_PIN_AE4 };
static const unsigned i2c2_b_2_pins[] = { DB8500_PIN_AF5, DB8500_PIN_AG4 };
static const unsigned msp0txrx_b_1_pins[] = { DB8500_PIN_AC4, DB8500_PIN_AC3 };
static const unsigned i2c1_b_2_pins[] = { DB8500_PIN_AD3, DB8500_PIN_AD4 };
/* Just RX and TX for UART2 */
static const unsigned u2rxtx_b_1_pins[] = { DB8500_PIN_AC2, DB8500_PIN_AC1 };
static const unsigned uartmodtx_b_1_pins[] = { DB8500_PIN_AB4 };
static const unsigned msp0sck_b_1_pins[] = { DB8500_PIN_AB3 };
static const unsigned uartmodrx_b_1_pins[] = { DB8500_PIN_AA3 };
static const unsigned stmmod_b_1_pins[] = { DB8500_PIN_AA4, DB8500_PIN_Y4,
	DB8500_PIN_Y2, DB8500_PIN_AA2, DB8500_PIN_AA1 };
static const unsigned uartmodrx_b_2_pins[] = { DB8500_PIN_AB2 };
static const unsigned spi3_b_1_pins[] = { DB8500_PIN_W2, DB8500_PIN_W3,
					  DB8500_PIN_V3, DB8500_PIN_V2 };
static const unsigned msp1txrx_b_1_pins[] = { DB8500_PIN_AF2, DB8500_PIN_AG2 };
static const unsigned kp_b_1_pins[] = { DB8500_PIN_F3, DB8500_PIN_F1,
	DB8500_PIN_G3, DB8500_PIN_G2, DB8500_PIN_E1, DB8500_PIN_E2,
	DB8500_PIN_G5, DB8500_PIN_G4, DB8500_PIN_H4, DB8500_PIN_H3,
	DB8500_PIN_J3, DB8500_PIN_H2, DB8500_PIN_J2, DB8500_PIN_H1,
	DB8500_PIN_F4, DB8500_PIN_E3, DB8500_PIN_E4, DB8500_PIN_D2,
	DB8500_PIN_C1, DB8500_PIN_D3, DB8500_PIN_C2, DB8500_PIN_D5 };
static const unsigned kp_b_2_pins[] = { DB8500_PIN_F3, DB8500_PIN_F1,
	DB8500_PIN_G3, DB8500_PIN_G2, DB8500_PIN_F4, DB8500_PIN_E3};
static const unsigned sm_b_1_pins[] = { DB8500_PIN_C6, DB8500_PIN_B3,
	DB8500_PIN_C4, DB8500_PIN_E6, DB8500_PIN_A3, DB8500_PIN_B6,
	DB8500_PIN_D6, DB8500_PIN_B7, DB8500_PIN_D7, DB8500_PIN_D8,
	DB8500_PIN_D9, DB8500_PIN_A5, DB8500_PIN_B4, DB8500_PIN_C8,
	DB8500_PIN_A12, DB8500_PIN_C10, DB8500_PIN_B10, DB8500_PIN_B9,
	DB8500_PIN_A9, DB8500_PIN_C7, DB8500_PIN_A7, DB8500_PIN_C5,
	DB8500_PIN_C9 };
/* This chip select pin can be "ps0" in alt C so have it separately */
static const unsigned smcs0_b_1_pins[] = { DB8500_PIN_E8 };
/* This chip select pin can be "ps1" in alt C so have it separately */
static const unsigned smcs1_b_1_pins[] = { DB8500_PIN_B14 };
static const unsigned ipgpio7_b_1_pins[] = { DB8500_PIN_B11 };
static const unsigned ipgpio2_b_1_pins[] = { DB8500_PIN_C12 };
static const unsigned ipgpio3_b_1_pins[] = { DB8500_PIN_C11 };
static const unsigned lcdaclk_b_1_pins[] = { DB8500_PIN_C14 };
static const unsigned lcda_b_1_pins[] = { DB8500_PIN_D22,
	DB8500_PIN_C23, DB8500_PIN_D23 };
static const unsigned lcd_b_1_pins[] = { DB8500_PIN_D17, DB8500_PIN_D16,
	DB8500_PIN_B17, DB8500_PIN_C16, DB8500_PIN_C19, DB8500_PIN_C17,
	DB8500_PIN_A18, DB8500_PIN_C18, DB8500_PIN_B19, DB8500_PIN_B20,
	DB8500_PIN_D21, DB8500_PIN_D20, DB8500_PIN_C20, DB8500_PIN_B21,
	DB8500_PIN_C21, DB8500_PIN_A22, DB8500_PIN_B24, DB8500_PIN_C22 };
static const unsigned ddrtrig_b_1_pins[] = { DB8500_PIN_AJ27 };
static const unsigned pwl_b_1_pins[] = { DB8500_PIN_AF25 };
static const unsigned spi1_b_1_pins[] = { DB8500_PIN_AG15, DB8500_PIN_AF13,
					  DB8500_PIN_AG13, DB8500_PIN_AH15 };
static const unsigned mc3_b_1_pins[] = { DB8500_PIN_AH13, DB8500_PIN_AG12,
	DB8500_PIN_AH12, DB8500_PIN_AH11, DB8500_PIN_AG10, DB8500_PIN_AH10,
	DB8500_PIN_AJ11, DB8500_PIN_AJ9, DB8500_PIN_AH9, DB8500_PIN_AG9,
	DB8500_PIN_AG8 };
static const unsigned pwl_b_2_pins[] = { DB8500_PIN_AF8 };
static const unsigned pwl_b_3_pins[] = { DB8500_PIN_AG7 };
static const unsigned pwl_b_4_pins[] = { DB8500_PIN_AF7 };

/* Altfunction C column */
static const unsigned ipjtag_c_1_pins[] = { DB8500_PIN_AJ5, DB8500_PIN_AJ3,
	DB8500_PIN_AH4, DB8500_PIN_AH3, DB8500_PIN_AH6 };
static const unsigned ipgpio6_c_1_pins[] = { DB8500_PIN_AG6 };
static const unsigned ipgpio0_c_1_pins[] = { DB8500_PIN_AF6 };
static const unsigned ipgpio1_c_1_pins[] = { DB8500_PIN_AG5 };
static const unsigned ipgpio3_c_1_pins[] = { DB8500_PIN_AF5 };
static const unsigned ipgpio2_c_1_pins[] = { DB8500_PIN_AG4 };
static const unsigned slim0_c_1_pins[] = { DB8500_PIN_AD3, DB8500_PIN_AD4 };
/* Optional 4-bit Memory Stick interface */
static const unsigned ms_c_1_pins[] = { DB8500_PIN_AC2, DB8500_PIN_AC1,
	DB8500_PIN_AB3, DB8500_PIN_AA3, DB8500_PIN_AA4, DB8500_PIN_AB2,
	DB8500_PIN_Y4, DB8500_PIN_Y2, DB8500_PIN_AA2, DB8500_PIN_AA1 };
static const unsigned iptrigout_c_1_pins[] = { DB8500_PIN_AB4 };
static const unsigned u2rxtx_c_1_pins[] = { DB8500_PIN_W2, DB8500_PIN_W3 };
static const unsigned u2ctsrts_c_1_pins[] = { DB8500_PIN_V3, DB8500_PIN_V2 };
static const unsigned u0_c_1_pins[] = { DB8500_PIN_AF2, DB8500_PIN_AE1,
					DB8500_PIN_AE2, DB8500_PIN_AG2 };
static const unsigned ipgpio4_c_1_pins[] = { DB8500_PIN_F3 };
static const unsigned ipgpio5_c_1_pins[] = { DB8500_PIN_F1 };
static const unsigned ipgpio6_c_2_pins[] = { DB8500_PIN_G3 };
static const unsigned ipgpio7_c_1_pins[] = { DB8500_PIN_G2 };
static const unsigned smcleale_c_1_pins[] = { DB8500_PIN_E1, DB8500_PIN_E2 };
static const unsigned stmape_c_1_pins[] = { DB8500_PIN_G5, DB8500_PIN_G4,
	DB8500_PIN_H4, DB8500_PIN_H3, DB8500_PIN_J3 };
static const unsigned u2rxtx_c_2_pins[] = { DB8500_PIN_H2, DB8500_PIN_J2 };
static const unsigned ipgpio2_c_2_pins[] = { DB8500_PIN_F4 };
static const unsigned ipgpio3_c_2_pins[] = { DB8500_PIN_E3 };
static const unsigned ipgpio4_c_2_pins[] = { DB8500_PIN_E4 };
static const unsigned ipgpio5_c_2_pins[] = { DB8500_PIN_D2 };
static const unsigned mc5_c_1_pins[] = { DB8500_PIN_C6, DB8500_PIN_B3,
	DB8500_PIN_C4, DB8500_PIN_E6, DB8500_PIN_A3, DB8500_PIN_B6,
	DB8500_PIN_D6, DB8500_PIN_B7, DB8500_PIN_D7, DB8500_PIN_D8,
	DB8500_PIN_D9 };
static const unsigned mc2rstn_c_1_pins[] = { DB8500_PIN_C8 };
static const unsigned kp_c_1_pins[] = { DB8500_PIN_C9, DB8500_PIN_B11,
	DB8500_PIN_C12, DB8500_PIN_C11, DB8500_PIN_D17, DB8500_PIN_D16,
	DB8500_PIN_C23, DB8500_PIN_D23 };
static const unsigned smps0_c_1_pins[] = { DB8500_PIN_E8 };
static const unsigned smps1_c_1_pins[] = { DB8500_PIN_B14 };
static const unsigned u2rxtx_c_3_pins[] = { DB8500_PIN_B17, DB8500_PIN_C16 };
static const unsigned stmape_c_2_pins[] = { DB8500_PIN_C19, DB8500_PIN_C17,
	DB8500_PIN_A18, DB8500_PIN_C18, DB8500_PIN_B19 };
static const unsigned uartmodrx_c_1_pins[] = { DB8500_PIN_D21 };
static const unsigned uartmodtx_c_1_pins[] = { DB8500_PIN_D20 };
static const unsigned stmmod_c_1_pins[] = { DB8500_PIN_C20, DB8500_PIN_B21,
	DB8500_PIN_C21, DB8500_PIN_A22, DB8500_PIN_B24 };
static const unsigned usbsim_c_1_pins[] = { DB8500_PIN_D22 };
static const unsigned mc4rstn_c_1_pins[] = { DB8500_PIN_AF25 };
static const unsigned clkout_c_1_pins[] = { DB8500_PIN_AH13, DB8500_PIN_AH12 };
static const unsigned i2c3_c_1_pins[] = { DB8500_PIN_AG12, DB8500_PIN_AH11 };
static const unsigned spi0_c_1_pins[] = { DB8500_PIN_AH10, DB8500_PIN_AH9,
					  DB8500_PIN_AG9, DB8500_PIN_AG8 };
static const unsigned usbsim_c_2_pins[] = { DB8500_PIN_AF8 };
static const unsigned i2c3_c_2_pins[] = { DB8500_PIN_AG7, DB8500_PIN_AF7 };

/* Other C1 column */
static const unsigned kp_oc1_1_pins[] = { DB8500_PIN_C6, DB8500_PIN_B3,
	DB8500_PIN_C4, DB8500_PIN_E6, DB8500_PIN_A3, DB8500_PIN_B6,
	DB8500_PIN_D6, DB8500_PIN_B7 };
static const unsigned spi2_oc1_1_pins[] = { DB8500_PIN_AH13, DB8500_PIN_AG12,
	DB8500_PIN_AH12, DB8500_PIN_AH11 };
static const unsigned spi2_oc1_2_pins[] = { DB8500_PIN_AH13, DB8500_PIN_AH12,
	DB8500_PIN_AH11 };

#define DB8500_PIN_GROUP(a,b) { .name = #a, .pins = a##_pins,		\
			.npins = ARRAY_SIZE(a##_pins), .altsetting = b }

static const struct nmk_pingroup nmk_db8500_groups[] = {
	/* Altfunction A column */
	DB8500_PIN_GROUP(u0_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(u1rxtx_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(u1ctsrts_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(ipi2c_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(ipi2c_a_2, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(msp0txrx_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(msp0tfstck_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(msp0rfsrck_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(mc0_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(mc0_dat47_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(mc0dat31dir_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(msp1txrx_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(msp1_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(lcdb_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(lcdvsi0_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(lcdvsi1_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(lcd_d0_d7_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(lcd_d8_d11_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(lcd_d12_d23_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(kp_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(mc2_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(ssp1_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(ssp0_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(i2c0_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(ipgpio0_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(ipgpio1_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(msp2sck_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(msp2_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(mc4_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(mc1_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(mc1_a_2, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(hsir_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(hsit_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(hsit_a_2, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(clkout_a_1, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(clkout_a_2, NMK_GPIO_ALT_A),
	DB8500_PIN_GROUP(usb_a_1, NMK_GPIO_ALT_A),
	/* Altfunction B column */
	DB8500_PIN_GROUP(trig_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(i2c4_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(i2c1_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(i2c2_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(i2c2_b_2, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(msp0txrx_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(i2c1_b_2, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(u2rxtx_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(uartmodtx_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(msp0sck_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(uartmodrx_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(stmmod_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(uartmodrx_b_2, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(spi3_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(msp1txrx_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(kp_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(kp_b_2, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(sm_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(smcs0_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(smcs1_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(ipgpio7_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(ipgpio2_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(ipgpio3_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(lcdaclk_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(lcda_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(lcd_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(ddrtrig_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(pwl_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(spi1_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(mc3_b_1, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(pwl_b_2, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(pwl_b_3, NMK_GPIO_ALT_B),
	DB8500_PIN_GROUP(pwl_b_4, NMK_GPIO_ALT_B),
	/* Altfunction C column */
	DB8500_PIN_GROUP(ipjtag_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio6_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio0_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio1_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio3_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio2_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(slim0_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ms_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(iptrigout_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(u2rxtx_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(u2ctsrts_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(u0_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio4_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio5_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio6_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio7_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(smcleale_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(stmape_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(u2rxtx_c_2, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio2_c_2, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio3_c_2, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio4_c_2, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(ipgpio5_c_2, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(mc5_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(mc2rstn_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(kp_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(smps0_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(smps1_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(u2rxtx_c_3, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(stmape_c_2, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(uartmodrx_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(uartmodtx_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(stmmod_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(usbsim_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(mc4rstn_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(clkout_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(i2c3_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(spi0_c_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(usbsim_c_2, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(i2c3_c_2, NMK_GPIO_ALT_C),
	/* Other alt C1 column, these are still configured as alt C */
	DB8500_PIN_GROUP(kp_oc1_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(spi2_oc1_1, NMK_GPIO_ALT_C),
	DB8500_PIN_GROUP(spi2_oc1_2, NMK_GPIO_ALT_C),
};

/* We use this macro to define the groups applicable to a function */
#define DB8500_FUNC_GROUPS(a, b...)	   \
static const char * const a##_groups[] = { b };

DB8500_FUNC_GROUPS(u0, "u0_a_1", "u0_c_1");
DB8500_FUNC_GROUPS(u1, "u1rxtx_a_1", "u1ctsrts_a_1");
/*
 * UART2 can be muxed out with just RX/TX in four places, CTS+RTS is however
 * only available on two pins in alternative function C
 */
DB8500_FUNC_GROUPS(u2, "u2rxtx_b_1", "u2rxtx_c_1", "u2ctsrts_c_1",
		   "u2rxtx_c_2", "u2rxtx_c_3");
DB8500_FUNC_GROUPS(ipi2c, "ipi2c_a_1", "ipi2c_a_2");
/*
 * MSP0 can only be on a certain set of pins, but the TX/RX pins can be
 * switched around by selecting the altfunction A or B. The SCK pin is
 * only available on the altfunction B.
 */
DB8500_FUNC_GROUPS(msp0, "msp0txrx_a_1", "msp0tfstck_a_1", "msp0rfstck_a_1",
		   "msp0txrx_b_1", "msp0sck_b_1");
DB8500_FUNC_GROUPS(mc0, "mc0_a_1", "mc0_dat47_a_1", "mc0dat31dir_a_1");
/* MSP0 can swap RX/TX like MSP0 but has no SCK pin available */
DB8500_FUNC_GROUPS(msp1, "msp1txrx_a_1", "msp1_a_1", "msp1txrx_b_1");
DB8500_FUNC_GROUPS(lcdb, "lcdb_a_1");
DB8500_FUNC_GROUPS(lcd, "lcdvsi0_a_1", "lcdvsi1_a_1", "lcd_d0_d7_a_1",
	"lcd_d8_d11_a_1", "lcd_d12_d23_a_1", "lcd_b_1");
DB8500_FUNC_GROUPS(kp, "kp_a_1", "kp_b_1", "kp_b_2", "kp_c_1", "kp_oc1_1");
DB8500_FUNC_GROUPS(mc2, "mc2_a_1", "mc2rstn_c_1");
DB8500_FUNC_GROUPS(ssp1, "ssp1_a_1");
DB8500_FUNC_GROUPS(ssp0, "ssp0_a_1");
DB8500_FUNC_GROUPS(i2c0, "i2c0_a_1");
/* The image processor has 8 GPIO pins that can be muxed out */
DB8500_FUNC_GROUPS(ipgpio, "ipgpio0_a_1", "ipgpio1_a_1", "ipgpio7_b_1",
	"ipgpio2_b_1", "ipgpio3_b_1", "ipgpio6_c_1", "ipgpio0_c_1",
	"ipgpio1_c_1", "ipgpio3_c_1", "ipgpio2_c_1", "ipgpio4_c_1",
	"ipgpio5_c_1", "ipgpio6_c_2", "ipgpio7_c_1", "ipgpio2_c_2",
	"ipgpio3_c_2", "ipgpio4_c_2", "ipgpio5_c_2");
/* MSP2 can not invert the RX/TX pins but has the optional SCK pin */
DB8500_FUNC_GROUPS(msp2, "msp2sck_a_1", "msp2_a_1");
DB8500_FUNC_GROUPS(mc4, "mc4_a_1", "mc4rstn_c_1");
DB8500_FUNC_GROUPS(mc1, "mc1_a_1", "mc1_a_2", "mc1dir_a_1");
DB8500_FUNC_GROUPS(hsi, "hsir_a_1", "hsit_a_1", "hsit_a_2");
DB8500_FUNC_GROUPS(clkout, "clkout_a_1", "clkout_a_2", "clkout_c_1");
DB8500_FUNC_GROUPS(usb, "usb_a_1");
DB8500_FUNC_GROUPS(trig, "trig_b_1");
DB8500_FUNC_GROUPS(i2c4, "i2c4_b_1");
DB8500_FUNC_GROUPS(i2c1, "i2c1_b_1", "i2c1_b_2");
DB8500_FUNC_GROUPS(i2c2, "i2c2_b_1", "i2c2_b_2");
/*
 * The modem UART can output its RX and TX pins in some different places,
 * so select one of each.
 */
DB8500_FUNC_GROUPS(uartmod, "uartmodtx_b_1", "uartmodrx_b_1", "uartmodrx_b_2",
		   "uartmodrx_c_1", "uartmod_tx_c_1");
DB8500_FUNC_GROUPS(stmmod, "stmmod_b_1", "stmmod_c_1");
DB8500_FUNC_GROUPS(spi3, "spi3_b_1");
/* Select between CS0 on alt B or PS1 on alt C */
DB8500_FUNC_GROUPS(sm, "sm_b_1", "smcs0_b_1", "smcs1_b_1", "smcleale_c_1",
		   "smps0_c_1", "smps1_c_1");
DB8500_FUNC_GROUPS(lcda, "lcdaclk_b_1", "lcda_b_1");
DB8500_FUNC_GROUPS(ddrtrig, "ddrtrig_b_1");
DB8500_FUNC_GROUPS(pwl, "pwl_b_1", "pwl_b_2", "pwl_b_3", "pwl_b_4");
DB8500_FUNC_GROUPS(spi1, "spi1_b_1");
DB8500_FUNC_GROUPS(mc3, "mc3_b_1");
DB8500_FUNC_GROUPS(ipjtag, "ipjtag_c_1");
DB8500_FUNC_GROUPS(slim0, "slim0_c_1");
DB8500_FUNC_GROUPS(ms, "ms_c_1");
DB8500_FUNC_GROUPS(iptrigout, "iptrigout_c_1");
DB8500_FUNC_GROUPS(stmape, "stmape_c_1", "stmape_c_2");
DB8500_FUNC_GROUPS(mc5, "mc5_c_1");
DB8500_FUNC_GROUPS(usbsim, "usbsim_c_1", "usbsim_c_2");
DB8500_FUNC_GROUPS(i2c3, "i2c3_c_1", "i2c3_c_2");
DB8500_FUNC_GROUPS(spi0, "spi0_c_1");
DB8500_FUNC_GROUPS(spi2, "spi2_oc1_1", "spi2_oc1_2");

#define FUNCTION(fname)					\
	{						\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

static const struct nmk_function nmk_db8500_functions[] = {
	FUNCTION(u0),
	FUNCTION(u1),
	FUNCTION(u2),
	FUNCTION(ipi2c),
	FUNCTION(msp0),
	FUNCTION(mc0),
	FUNCTION(msp1),
	FUNCTION(lcdb),
	FUNCTION(lcd),
	FUNCTION(kp),
	FUNCTION(mc2),
	FUNCTION(ssp1),
	FUNCTION(ssp0),
	FUNCTION(i2c0),
	FUNCTION(ipgpio),
	FUNCTION(msp2),
	FUNCTION(mc4),
	FUNCTION(mc1),
	FUNCTION(hsi),
	FUNCTION(clkout),
	FUNCTION(usb),
	FUNCTION(trig),
	FUNCTION(i2c4),
	FUNCTION(i2c1),
	FUNCTION(i2c2),
	FUNCTION(uartmod),
	FUNCTION(stmmod),
	FUNCTION(spi3),
	FUNCTION(sm),
	FUNCTION(lcda),
	FUNCTION(ddrtrig),
	FUNCTION(pwl),
	FUNCTION(spi1),
	FUNCTION(mc3),
	FUNCTION(ipjtag),
	FUNCTION(slim0),
	FUNCTION(ms),
	FUNCTION(iptrigout),
	FUNCTION(stmape),
	FUNCTION(mc5),
	FUNCTION(usbsim),
	FUNCTION(i2c3),
	FUNCTION(spi0),
	FUNCTION(spi2),
};

static const struct nmk_pinctrl_soc_data nmk_db8500_soc = {
	.gpio_ranges = nmk_db8500_ranges,
	.gpio_num_ranges = ARRAY_SIZE(nmk_db8500_ranges),
	.pins = nmk_db8500_pins,
	.npins = ARRAY_SIZE(nmk_db8500_pins),
	.functions = nmk_db8500_functions,
	.nfunctions = ARRAY_SIZE(nmk_db8500_functions),
	.groups = nmk_db8500_groups,
	.ngroups = ARRAY_SIZE(nmk_db8500_groups),
};

void __devinit
nmk_pinctrl_db8500_init(const struct nmk_pinctrl_soc_data **soc)
{
	*soc = &nmk_db8500_soc;
}
