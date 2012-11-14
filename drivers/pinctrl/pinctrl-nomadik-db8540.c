#include <linux/kernel.h>
#include <linux/pinctrl/pinctrl.h>
#include "pinctrl-nomadik.h"

/* All the pins that can be used for GPIO and some other functions */
#define _GPIO(offset)		(offset)

#define DB8540_PIN_AH6		_GPIO(0)
#define DB8540_PIN_AG7		_GPIO(1)
#define DB8540_PIN_AF2		_GPIO(2)
#define DB8540_PIN_AD3		_GPIO(3)
#define DB8540_PIN_AF6		_GPIO(4)
#define DB8540_PIN_AG6		_GPIO(5)
#define DB8540_PIN_AD5		_GPIO(6)
#define DB8540_PIN_AF7		_GPIO(7)
#define DB8540_PIN_AG5		_GPIO(8)
#define DB8540_PIN_AH5		_GPIO(9)
#define DB8540_PIN_AE4		_GPIO(10)
#define DB8540_PIN_AD1		_GPIO(11)
#define DB8540_PIN_AD2		_GPIO(12)
#define DB8540_PIN_AC2		_GPIO(13)
#define DB8540_PIN_AC4		_GPIO(14)
#define DB8540_PIN_AC3		_GPIO(15)
#define DB8540_PIN_AH7		_GPIO(16)
#define DB8540_PIN_AE7		_GPIO(17)
/* Hole */
#define DB8540_PIN_AF8		_GPIO(22)
#define DB8540_PIN_AH11		_GPIO(23)
#define DB8540_PIN_AG11		_GPIO(24)
#define DB8540_PIN_AF11		_GPIO(25)
#define DB8540_PIN_AH10		_GPIO(26)
#define DB8540_PIN_AG10		_GPIO(27)
#define DB8540_PIN_AF10		_GPIO(28)
/* Hole */
#define DB8540_PIN_AD4		_GPIO(33)
#define DB8540_PIN_AF3		_GPIO(34)
#define DB8540_PIN_AF5		_GPIO(35)
#define DB8540_PIN_AG4		_GPIO(36)
#define DB8540_PIN_AF9		_GPIO(37)
#define DB8540_PIN_AE8		_GPIO(38)
/* Hole */
#define DB8540_PIN_M26		_GPIO(64)
#define DB8540_PIN_M25		_GPIO(65)
#define DB8540_PIN_M27		_GPIO(66)
#define DB8540_PIN_N25		_GPIO(67)
/* Hole */
#define DB8540_PIN_M28		_GPIO(70)
#define DB8540_PIN_N26		_GPIO(71)
#define DB8540_PIN_M22		_GPIO(72)
#define DB8540_PIN_N22		_GPIO(73)
#define DB8540_PIN_N27		_GPIO(74)
#define DB8540_PIN_N28		_GPIO(75)
#define DB8540_PIN_P22		_GPIO(76)
#define DB8540_PIN_P28		_GPIO(77)
#define DB8540_PIN_P26		_GPIO(78)
#define DB8540_PIN_T22		_GPIO(79)
#define DB8540_PIN_R27		_GPIO(80)
#define DB8540_PIN_P27		_GPIO(81)
#define DB8540_PIN_R26		_GPIO(82)
#define DB8540_PIN_R25		_GPIO(83)
#define DB8540_PIN_U22		_GPIO(84)
#define DB8540_PIN_T27		_GPIO(85)
#define DB8540_PIN_T25		_GPIO(86)
#define DB8540_PIN_T26		_GPIO(87)
/* Hole */
#define DB8540_PIN_AF20		_GPIO(116)
#define DB8540_PIN_AG21		_GPIO(117)
#define DB8540_PIN_AH19		_GPIO(118)
#define DB8540_PIN_AE19		_GPIO(119)
#define DB8540_PIN_AG18		_GPIO(120)
#define DB8540_PIN_AH17		_GPIO(121)
#define DB8540_PIN_AF19		_GPIO(122)
#define DB8540_PIN_AF18		_GPIO(123)
#define DB8540_PIN_AE18		_GPIO(124)
#define DB8540_PIN_AG17		_GPIO(125)
#define DB8540_PIN_AF17		_GPIO(126)
#define DB8540_PIN_AE17		_GPIO(127)
#define DB8540_PIN_AC27		_GPIO(128)
#define DB8540_PIN_AD27		_GPIO(129)
#define DB8540_PIN_AE28		_GPIO(130)
#define DB8540_PIN_AG26		_GPIO(131)
#define DB8540_PIN_AF25		_GPIO(132)
#define DB8540_PIN_AE27		_GPIO(133)
#define DB8540_PIN_AF27		_GPIO(134)
#define DB8540_PIN_AG28		_GPIO(135)
#define DB8540_PIN_AF28		_GPIO(136)
#define DB8540_PIN_AG25		_GPIO(137)
#define DB8540_PIN_AG24		_GPIO(138)
#define DB8540_PIN_AD25		_GPIO(139)
#define DB8540_PIN_AH25		_GPIO(140)
#define DB8540_PIN_AF26		_GPIO(141)
#define DB8540_PIN_AF23		_GPIO(142)
#define DB8540_PIN_AG23		_GPIO(143)
#define DB8540_PIN_AE25		_GPIO(144)
#define DB8540_PIN_AH24		_GPIO(145)
#define DB8540_PIN_AJ25		_GPIO(146)
#define DB8540_PIN_AG27		_GPIO(147)
#define DB8540_PIN_AH23		_GPIO(148)
#define DB8540_PIN_AE26		_GPIO(149)
#define DB8540_PIN_AE24		_GPIO(150)
#define DB8540_PIN_AJ24		_GPIO(151)
#define DB8540_PIN_AE21		_GPIO(152)
#define DB8540_PIN_AG22		_GPIO(153)
#define DB8540_PIN_AF21		_GPIO(154)
#define DB8540_PIN_AF24		_GPIO(155)
#define DB8540_PIN_AH22		_GPIO(156)
#define DB8540_PIN_AJ23		_GPIO(157)
#define DB8540_PIN_AH21		_GPIO(158)
#define DB8540_PIN_AG20		_GPIO(159)
#define DB8540_PIN_AE23		_GPIO(160)
#define DB8540_PIN_AH20		_GPIO(161)
#define DB8540_PIN_AG19		_GPIO(162)
#define DB8540_PIN_AF22		_GPIO(163)
#define DB8540_PIN_AJ21		_GPIO(164)
#define DB8540_PIN_AD26		_GPIO(165)
#define DB8540_PIN_AD28		_GPIO(166)
#define DB8540_PIN_AC28		_GPIO(167)
#define DB8540_PIN_AC26		_GPIO(168)
/* Hole */
#define DB8540_PIN_J3		_GPIO(192)
#define DB8540_PIN_H1		_GPIO(193)
#define DB8540_PIN_J2		_GPIO(194)
#define DB8540_PIN_H2		_GPIO(195)
#define DB8540_PIN_H3		_GPIO(196)
#define DB8540_PIN_H4		_GPIO(197)
#define DB8540_PIN_G2		_GPIO(198)
#define DB8540_PIN_G3		_GPIO(199)
#define DB8540_PIN_G4		_GPIO(200)
#define DB8540_PIN_F2		_GPIO(201)
#define DB8540_PIN_C6		_GPIO(202)
#define DB8540_PIN_B6		_GPIO(203)
#define DB8540_PIN_B7		_GPIO(204)
#define DB8540_PIN_A7		_GPIO(205)
#define DB8540_PIN_D7		_GPIO(206)
#define DB8540_PIN_D8		_GPIO(207)
#define DB8540_PIN_F3		_GPIO(208)
#define DB8540_PIN_E2		_GPIO(209)
#define DB8540_PIN_C7		_GPIO(210)
#define DB8540_PIN_B8		_GPIO(211)
#define DB8540_PIN_C10		_GPIO(212)
#define DB8540_PIN_C8		_GPIO(213)
#define DB8540_PIN_C9		_GPIO(214)
/* Hole */
#define DB8540_PIN_B9		_GPIO(219)
#define DB8540_PIN_A10		_GPIO(220)
#define DB8540_PIN_D9		_GPIO(221)
#define DB8540_PIN_B11		_GPIO(222)
#define DB8540_PIN_B10		_GPIO(223)
#define DB8540_PIN_E10		_GPIO(224)
#define DB8540_PIN_B12		_GPIO(225)
#define DB8540_PIN_D10		_GPIO(226)
#define DB8540_PIN_D11		_GPIO(227)
#define DB8540_PIN_AJ6		_GPIO(228)
#define DB8540_PIN_B13		_GPIO(229)
#define DB8540_PIN_C12		_GPIO(230)
#define DB8540_PIN_B14		_GPIO(231)
#define DB8540_PIN_E11		_GPIO(232)
/* Hole */
#define DB8540_PIN_D12		_GPIO(256)
#define DB8540_PIN_D15		_GPIO(257)
#define DB8540_PIN_C13		_GPIO(258)
#define DB8540_PIN_C14		_GPIO(259)
#define DB8540_PIN_C18		_GPIO(260)
#define DB8540_PIN_C16		_GPIO(261)
#define DB8540_PIN_B16		_GPIO(262)
#define DB8540_PIN_D18		_GPIO(263)
#define DB8540_PIN_C15		_GPIO(264)
#define DB8540_PIN_C17		_GPIO(265)
#define DB8540_PIN_B17		_GPIO(266)
#define DB8540_PIN_D17		_GPIO(267)

/*
 * The names of the pins are denoted by GPIO number and ball name, even
 * though they can be used for other things than GPIO, this is the first
 * column in the table of the data sheet and often used on schematics and
 * such.
 */
static const struct pinctrl_pin_desc nmk_db8540_pins[] = {
	PINCTRL_PIN(DB8540_PIN_AH6, "GPIO0_AH6"),
	PINCTRL_PIN(DB8540_PIN_AG7, "GPIO1_AG7"),
	PINCTRL_PIN(DB8540_PIN_AF2, "GPIO2_AF2"),
	PINCTRL_PIN(DB8540_PIN_AD3, "GPIO3_AD3"),
	PINCTRL_PIN(DB8540_PIN_AF6, "GPIO4_AF6"),
	PINCTRL_PIN(DB8540_PIN_AG6, "GPIO5_AG6"),
	PINCTRL_PIN(DB8540_PIN_AD5, "GPIO6_AD5"),
	PINCTRL_PIN(DB8540_PIN_AF7, "GPIO7_AF7"),
	PINCTRL_PIN(DB8540_PIN_AG5, "GPIO8_AG5"),
	PINCTRL_PIN(DB8540_PIN_AH5, "GPIO9_AH5"),
	PINCTRL_PIN(DB8540_PIN_AE4, "GPIO10_AE4"),
	PINCTRL_PIN(DB8540_PIN_AD1, "GPIO11_AD1"),
	PINCTRL_PIN(DB8540_PIN_AD2, "GPIO12_AD2"),
	PINCTRL_PIN(DB8540_PIN_AC2, "GPIO13_AC2"),
	PINCTRL_PIN(DB8540_PIN_AC4, "GPIO14_AC4"),
	PINCTRL_PIN(DB8540_PIN_AC3, "GPIO15_AC3"),
	PINCTRL_PIN(DB8540_PIN_AH7, "GPIO16_AH7"),
	PINCTRL_PIN(DB8540_PIN_AE7, "GPIO17_AE7"),
	/* Hole */
	PINCTRL_PIN(DB8540_PIN_AF8, "GPIO22_AF8"),
	PINCTRL_PIN(DB8540_PIN_AH11, "GPIO23_AH11"),
	PINCTRL_PIN(DB8540_PIN_AG11, "GPIO24_AG11"),
	PINCTRL_PIN(DB8540_PIN_AF11, "GPIO25_AF11"),
	PINCTRL_PIN(DB8540_PIN_AH10, "GPIO26_AH10"),
	PINCTRL_PIN(DB8540_PIN_AG10, "GPIO27_AG10"),
	PINCTRL_PIN(DB8540_PIN_AF10, "GPIO28_AF10"),
	/* Hole */
	PINCTRL_PIN(DB8540_PIN_AD4, "GPIO33_AD4"),
	PINCTRL_PIN(DB8540_PIN_AF3, "GPIO34_AF3"),
	PINCTRL_PIN(DB8540_PIN_AF5, "GPIO35_AF5"),
	PINCTRL_PIN(DB8540_PIN_AG4, "GPIO36_AG4"),
	PINCTRL_PIN(DB8540_PIN_AF9, "GPIO37_AF9"),
	PINCTRL_PIN(DB8540_PIN_AE8, "GPIO38_AE8"),
	/* Hole */
	PINCTRL_PIN(DB8540_PIN_M26, "GPIO64_M26"),
	PINCTRL_PIN(DB8540_PIN_M25, "GPIO65_M25"),
	PINCTRL_PIN(DB8540_PIN_M27, "GPIO66_M27"),
	PINCTRL_PIN(DB8540_PIN_N25, "GPIO67_N25"),
	/* Hole */
	PINCTRL_PIN(DB8540_PIN_M28, "GPIO70_M28"),
	PINCTRL_PIN(DB8540_PIN_N26, "GPIO71_N26"),
	PINCTRL_PIN(DB8540_PIN_M22, "GPIO72_M22"),
	PINCTRL_PIN(DB8540_PIN_N22, "GPIO73_N22"),
	PINCTRL_PIN(DB8540_PIN_N27, "GPIO74_N27"),
	PINCTRL_PIN(DB8540_PIN_N28, "GPIO75_N28"),
	PINCTRL_PIN(DB8540_PIN_P22, "GPIO76_P22"),
	PINCTRL_PIN(DB8540_PIN_P28, "GPIO77_P28"),
	PINCTRL_PIN(DB8540_PIN_P26, "GPIO78_P26"),
	PINCTRL_PIN(DB8540_PIN_T22, "GPIO79_T22"),
	PINCTRL_PIN(DB8540_PIN_R27, "GPIO80_R27"),
	PINCTRL_PIN(DB8540_PIN_P27, "GPIO81_P27"),
	PINCTRL_PIN(DB8540_PIN_R26, "GPIO82_R26"),
	PINCTRL_PIN(DB8540_PIN_R25, "GPIO83_R25"),
	PINCTRL_PIN(DB8540_PIN_U22, "GPIO84_U22"),
	PINCTRL_PIN(DB8540_PIN_T27, "GPIO85_T27"),
	PINCTRL_PIN(DB8540_PIN_T25, "GPIO86_T25"),
	PINCTRL_PIN(DB8540_PIN_T26, "GPIO87_T26"),
	/* Hole */
	PINCTRL_PIN(DB8540_PIN_AF20, "GPIO116_AF20"),
	PINCTRL_PIN(DB8540_PIN_AG21, "GPIO117_AG21"),
	PINCTRL_PIN(DB8540_PIN_AH19, "GPIO118_AH19"),
	PINCTRL_PIN(DB8540_PIN_AE19, "GPIO119_AE19"),
	PINCTRL_PIN(DB8540_PIN_AG18, "GPIO120_AG18"),
	PINCTRL_PIN(DB8540_PIN_AH17, "GPIO121_AH17"),
	PINCTRL_PIN(DB8540_PIN_AF19, "GPIO122_AF19"),
	PINCTRL_PIN(DB8540_PIN_AF18, "GPIO123_AF18"),
	PINCTRL_PIN(DB8540_PIN_AE18, "GPIO124_AE18"),
	PINCTRL_PIN(DB8540_PIN_AG17, "GPIO125_AG17"),
	PINCTRL_PIN(DB8540_PIN_AF17, "GPIO126_AF17"),
	PINCTRL_PIN(DB8540_PIN_AE17, "GPIO127_AE17"),
	PINCTRL_PIN(DB8540_PIN_AC27, "GPIO128_AC27"),
	PINCTRL_PIN(DB8540_PIN_AD27, "GPIO129_AD27"),
	PINCTRL_PIN(DB8540_PIN_AE28, "GPIO130_AE28"),
	PINCTRL_PIN(DB8540_PIN_AG26, "GPIO131_AG26"),
	PINCTRL_PIN(DB8540_PIN_AF25, "GPIO132_AF25"),
	PINCTRL_PIN(DB8540_PIN_AE27, "GPIO133_AE27"),
	PINCTRL_PIN(DB8540_PIN_AF27, "GPIO134_AF27"),
	PINCTRL_PIN(DB8540_PIN_AG28, "GPIO135_AG28"),
	PINCTRL_PIN(DB8540_PIN_AF28, "GPIO136_AF28"),
	PINCTRL_PIN(DB8540_PIN_AG25, "GPIO137_AG25"),
	PINCTRL_PIN(DB8540_PIN_AG24, "GPIO138_AG24"),
	PINCTRL_PIN(DB8540_PIN_AD25, "GPIO139_AD25"),
	PINCTRL_PIN(DB8540_PIN_AH25, "GPIO140_AH25"),
	PINCTRL_PIN(DB8540_PIN_AF26, "GPIO141_AF26"),
	PINCTRL_PIN(DB8540_PIN_AF23, "GPIO142_AF23"),
	PINCTRL_PIN(DB8540_PIN_AG23, "GPIO143_AG23"),
	PINCTRL_PIN(DB8540_PIN_AE25, "GPIO144_AE25"),
	PINCTRL_PIN(DB8540_PIN_AH24, "GPIO145_AH24"),
	PINCTRL_PIN(DB8540_PIN_AJ25, "GPIO146_AJ25"),
	PINCTRL_PIN(DB8540_PIN_AG27, "GPIO147_AG27"),
	PINCTRL_PIN(DB8540_PIN_AH23, "GPIO148_AH23"),
	PINCTRL_PIN(DB8540_PIN_AE26, "GPIO149_AE26"),
	PINCTRL_PIN(DB8540_PIN_AE24, "GPIO150_AE24"),
	PINCTRL_PIN(DB8540_PIN_AJ24, "GPIO151_AJ24"),
	PINCTRL_PIN(DB8540_PIN_AE21, "GPIO152_AE21"),
	PINCTRL_PIN(DB8540_PIN_AG22, "GPIO153_AG22"),
	PINCTRL_PIN(DB8540_PIN_AF21, "GPIO154_AF21"),
	PINCTRL_PIN(DB8540_PIN_AF24, "GPIO155_AF24"),
	PINCTRL_PIN(DB8540_PIN_AH22, "GPIO156_AH22"),
	PINCTRL_PIN(DB8540_PIN_AJ23, "GPIO157_AJ23"),
	PINCTRL_PIN(DB8540_PIN_AH21, "GPIO158_AH21"),
	PINCTRL_PIN(DB8540_PIN_AG20, "GPIO159_AG20"),
	PINCTRL_PIN(DB8540_PIN_AE23, "GPIO160_AE23"),
	PINCTRL_PIN(DB8540_PIN_AH20, "GPIO161_AH20"),
	PINCTRL_PIN(DB8540_PIN_AG19, "GPIO162_AG19"),
	PINCTRL_PIN(DB8540_PIN_AF22, "GPIO163_AF22"),
	PINCTRL_PIN(DB8540_PIN_AJ21, "GPIO164_AJ21"),
	PINCTRL_PIN(DB8540_PIN_AD26, "GPIO165_AD26"),
	PINCTRL_PIN(DB8540_PIN_AD28, "GPIO166_AD28"),
	PINCTRL_PIN(DB8540_PIN_AC28, "GPIO167_AC28"),
	PINCTRL_PIN(DB8540_PIN_AC26, "GPIO168_AC26"),
	/* Hole */
	PINCTRL_PIN(DB8540_PIN_J3, "GPIO192_J3"),
	PINCTRL_PIN(DB8540_PIN_H1, "GPIO193_H1"),
	PINCTRL_PIN(DB8540_PIN_J2, "GPIO194_J2"),
	PINCTRL_PIN(DB8540_PIN_H2, "GPIO195_H2"),
	PINCTRL_PIN(DB8540_PIN_H3, "GPIO196_H3"),
	PINCTRL_PIN(DB8540_PIN_H4, "GPIO197_H4"),
	PINCTRL_PIN(DB8540_PIN_G2, "GPIO198_G2"),
	PINCTRL_PIN(DB8540_PIN_G3, "GPIO199_G3"),
	PINCTRL_PIN(DB8540_PIN_G4, "GPIO200_G4"),
	PINCTRL_PIN(DB8540_PIN_F2, "GPIO201_F2"),
	PINCTRL_PIN(DB8540_PIN_C6, "GPIO202_C6"),
	PINCTRL_PIN(DB8540_PIN_B6, "GPIO203_B6"),
	PINCTRL_PIN(DB8540_PIN_B7, "GPIO204_B7"),
	PINCTRL_PIN(DB8540_PIN_A7, "GPIO205_A7"),
	PINCTRL_PIN(DB8540_PIN_D7, "GPIO206_D7"),
	PINCTRL_PIN(DB8540_PIN_D8, "GPIO207_D8"),
	PINCTRL_PIN(DB8540_PIN_F3, "GPIO208_F3"),
	PINCTRL_PIN(DB8540_PIN_E2, "GPIO209_E2"),
	PINCTRL_PIN(DB8540_PIN_C7, "GPIO210_C7"),
	PINCTRL_PIN(DB8540_PIN_B8, "GPIO211_B8"),
	PINCTRL_PIN(DB8540_PIN_C10, "GPIO212_C10"),
	PINCTRL_PIN(DB8540_PIN_C8, "GPIO213_C8"),
	PINCTRL_PIN(DB8540_PIN_C9, "GPIO214_C9"),
	/* Hole */
	PINCTRL_PIN(DB8540_PIN_B9, "GPIO219_B9"),
	PINCTRL_PIN(DB8540_PIN_A10, "GPIO220_A10"),
	PINCTRL_PIN(DB8540_PIN_D9, "GPIO221_D9"),
	PINCTRL_PIN(DB8540_PIN_B11, "GPIO222_B11"),
	PINCTRL_PIN(DB8540_PIN_B10, "GPIO223_B10"),
	PINCTRL_PIN(DB8540_PIN_E10, "GPIO224_E10"),
	PINCTRL_PIN(DB8540_PIN_B12, "GPIO225_B12"),
	PINCTRL_PIN(DB8540_PIN_D10, "GPIO226_D10"),
	PINCTRL_PIN(DB8540_PIN_D11, "GPIO227_D11"),
	PINCTRL_PIN(DB8540_PIN_AJ6, "GPIO228_AJ6"),
	PINCTRL_PIN(DB8540_PIN_B13, "GPIO229_B13"),
	PINCTRL_PIN(DB8540_PIN_C12, "GPIO230_C12"),
	PINCTRL_PIN(DB8540_PIN_B14, "GPIO231_B14"),
	PINCTRL_PIN(DB8540_PIN_E11, "GPIO232_E11"),
	/* Hole */
	PINCTRL_PIN(DB8540_PIN_D12, "GPIO256_D12"),
	PINCTRL_PIN(DB8540_PIN_D15, "GPIO257_D15"),
	PINCTRL_PIN(DB8540_PIN_C13, "GPIO258_C13"),
	PINCTRL_PIN(DB8540_PIN_C14, "GPIO259_C14"),
	PINCTRL_PIN(DB8540_PIN_C18, "GPIO260_C18"),
	PINCTRL_PIN(DB8540_PIN_C16, "GPIO261_C16"),
	PINCTRL_PIN(DB8540_PIN_B16, "GPIO262_B16"),
	PINCTRL_PIN(DB8540_PIN_D18, "GPIO263_D18"),
	PINCTRL_PIN(DB8540_PIN_C15, "GPIO264_C15"),
	PINCTRL_PIN(DB8540_PIN_C17, "GPIO265_C17"),
	PINCTRL_PIN(DB8540_PIN_B17, "GPIO266_B17"),
	PINCTRL_PIN(DB8540_PIN_D17, "GPIO267_D17"),
};

#define DB8540_GPIO_RANGE(a, b, c) { .name = "db8540", .id = a, .base = b, \
			.pin_base = b, .npins = c }

/*
 * This matches the 32-pin gpio chips registered by the GPIO portion. This
 * cannot be const since we assign the struct gpio_chip * pointer at runtime.
 */
static struct pinctrl_gpio_range nmk_db8540_ranges[] = {
	DB8540_GPIO_RANGE(0, 0, 18),
	DB8540_GPIO_RANGE(0, 22, 7),
	DB8540_GPIO_RANGE(1, 33, 6),
	DB8540_GPIO_RANGE(2, 64, 4),
	DB8540_GPIO_RANGE(2, 70, 18),
	DB8540_GPIO_RANGE(3, 116, 12),
	DB8540_GPIO_RANGE(4, 128, 32),
	DB8540_GPIO_RANGE(5, 160, 9),
	DB8540_GPIO_RANGE(6, 192, 23),
	DB8540_GPIO_RANGE(6, 219, 5),
	DB8540_GPIO_RANGE(7, 224, 9),
	DB8540_GPIO_RANGE(8, 256, 12),
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
static const unsigned u0_a_1_pins[] = { DB8540_PIN_AH6, DB8540_PIN_AG7,
					DB8540_PIN_AF2, DB8540_PIN_AD3 };
static const unsigned u1rxtx_a_1_pins[] = { DB8540_PIN_AF6, DB8540_PIN_AG6 };
static const unsigned u1ctsrts_a_1_pins[] = { DB8540_PIN_AD5, DB8540_PIN_AF7 };
/* Image processor I2C line, this is driven by image processor firmware */
static const unsigned ipi2c_a_1_pins[] = { DB8540_PIN_AG5, DB8540_PIN_AH5 };
static const unsigned ipi2c_a_2_pins[] = { DB8540_PIN_AE4, DB8540_PIN_AD1 };
/* MSP0 can only be on these pins, but TXD and RXD can be flipped */
static const unsigned msp0txrx_a_1_pins[] = { DB8540_PIN_AD2, DB8540_PIN_AC3 };
static const unsigned msp0tfstck_a_1_pins[] = { DB8540_PIN_AC2,
	DB8540_PIN_AC4 };
static const unsigned msp0rfsrck_a_1_pins[] = { DB8540_PIN_AH7,
	DB8540_PIN_AE7 };
/* Basic pins of the MMC/SD card 0 interface */
static const unsigned mc0_a_1_pins[] = { DB8540_PIN_AH11, DB8540_PIN_AG11,
	DB8540_PIN_AF11, DB8540_PIN_AH10, DB8540_PIN_AG10, DB8540_PIN_AF10};
/* MSP1 can only be on these pins, but TXD and RXD can be flipped */
static const unsigned msp1txrx_a_1_pins[] = { DB8540_PIN_AD4, DB8540_PIN_AG4 };
static const unsigned msp1_a_1_pins[] = { DB8540_PIN_AF3, DB8540_PIN_AF5 };

static const unsigned modobsclk_a_1_pins[] = { DB8540_PIN_AF9 };
static const unsigned clkoutreq_a_1_pins[] = { DB8540_PIN_AE8 };
/* LCD interface */
static const unsigned lcdb_a_1_pins[] = { DB8540_PIN_M26, DB8540_PIN_M25,
	DB8540_PIN_M27, DB8540_PIN_N25 };
static const unsigned lcdvsi0_a_1_pins[] = { DB8540_PIN_AJ24 };
static const unsigned lcdvsi1_a_1_pins[] = { DB8540_PIN_AE21 };
static const unsigned lcd_d0_d7_a_1_pins[] = { DB8540_PIN_M28, DB8540_PIN_N26,
	DB8540_PIN_M22, DB8540_PIN_N22, DB8540_PIN_N27, DB8540_PIN_N28,
	DB8540_PIN_P22, DB8540_PIN_P28 };
/* D8 thru D11 often used as TVOUT lines */
static const unsigned lcd_d8_d11_a_1_pins[] = { DB8540_PIN_P26, DB8540_PIN_T22,
	DB8540_PIN_R27, DB8540_PIN_P27 };
static const unsigned lcd_d12_d23_a_1_pins[] = { DB8540_PIN_R26, DB8540_PIN_R25,
	DB8540_PIN_U22, DB8540_PIN_T27, DB8540_PIN_AG22, DB8540_PIN_AF21,
	DB8540_PIN_AF24, DB8540_PIN_AH22, DB8540_PIN_AJ23, DB8540_PIN_AH21,
	DB8540_PIN_AG20, DB8540_PIN_AE23 };
static const unsigned kp_a_1_pins[] = { DB8540_PIN_AH20, DB8540_PIN_AG19,
	DB8540_PIN_AF22, DB8540_PIN_AJ21, DB8540_PIN_T25, DB8540_PIN_T26 };
/* MC2 has 8 data lines and no direction control, so only for (e)MMC */
static const unsigned mc2_a_1_pins[] = { DB8540_PIN_AC27, DB8540_PIN_AD27,
	DB8540_PIN_AE28, DB8540_PIN_AG26, DB8540_PIN_AF25, DB8540_PIN_AE27,
	DB8540_PIN_AF27, DB8540_PIN_AG28, DB8540_PIN_AF28, DB8540_PIN_AG25,
	DB8540_PIN_AG24 };
static const unsigned ssp1_a_1_pins[] = {  DB8540_PIN_AD25, DB8540_PIN_AH25,
	DB8540_PIN_AF26, DB8540_PIN_AF23 };
static const unsigned ssp0_a_1_pins[] = { DB8540_PIN_AG23, DB8540_PIN_AE25,
	DB8540_PIN_AH24, DB8540_PIN_AJ25 };
static const unsigned i2c0_a_1_pins[] = { DB8540_PIN_AG27, DB8540_PIN_AH23 };
/*
 * Image processor GPIO pins are named "ipgpio" and have their own
 * numberspace
 */
static const unsigned ipgpio0_a_1_pins[] = { DB8540_PIN_AE26 };
static const unsigned ipgpio1_a_1_pins[] = { DB8540_PIN_AE24 };
/* modem i2s interface */
static const unsigned modi2s_a_1_pins[] = { DB8540_PIN_AD26, DB8540_PIN_AD28,
	DB8540_PIN_AC28, DB8540_PIN_AC26 };
static const unsigned spi2_a_1_pins[] = { DB8540_PIN_AF20, DB8540_PIN_AG21,
	DB8540_PIN_AH19, DB8540_PIN_AE19 };
static const unsigned u2txrx_a_1_pins[] = { DB8540_PIN_AG18, DB8540_PIN_AH17 };
static const unsigned u2ctsrts_a_1_pins[] = { DB8540_PIN_AF19,
	DB8540_PIN_AF18 };
static const unsigned modsmb_a_1_pins[] = { DB8540_PIN_AF17, DB8540_PIN_AE17 };
static const unsigned msp2sck_a_1_pins[] = { DB8540_PIN_J3 };
static const unsigned msp2txdtcktfs_a_1_pins[] = { DB8540_PIN_H1, DB8540_PIN_J2,
	DB8540_PIN_H2 };
static const unsigned msp2rxd_a_1_pins[] = { DB8540_PIN_H3 };
static const unsigned mc4_a_1_pins[] = { DB8540_PIN_H4, DB8540_PIN_G2,
	DB8540_PIN_G3, DB8540_PIN_G4, DB8540_PIN_F2, DB8540_PIN_C6,
	DB8540_PIN_B6, DB8540_PIN_B7, DB8540_PIN_A7, DB8540_PIN_D7,
	DB8540_PIN_D8 };
static const unsigned mc1_a_1_pins[] = { DB8540_PIN_F3, DB8540_PIN_E2,
	DB8540_PIN_C7, DB8540_PIN_B8, DB8540_PIN_C10, DB8540_PIN_C8,
	DB8540_PIN_C9 };
/* mc1_a_2_pins exclude MC1_FBCLK */
static const unsigned mc1_a_2_pins[] = { DB8540_PIN_F3,	DB8540_PIN_C7,
	DB8540_PIN_B8, DB8540_PIN_C10, DB8540_PIN_C8,
	DB8540_PIN_C9 };
static const unsigned hsir_a_1_pins[] = { DB8540_PIN_B9, DB8540_PIN_A10,
	DB8540_PIN_D9 };
static const unsigned hsit_a_1_pins[] = { DB8540_PIN_B11, DB8540_PIN_B10,
	DB8540_PIN_E10, DB8540_PIN_B12, DB8540_PIN_D10 };
static const unsigned hsit_a_2_pins[] = { DB8540_PIN_B11, DB8540_PIN_B10,
	DB8540_PIN_E10, DB8540_PIN_B12 };
static const unsigned clkout1_a_1_pins[] = { DB8540_PIN_D11 };
static const unsigned clkout1_a_2_pins[] = { DB8540_PIN_B13 };
static const unsigned clkout2_a_1_pins[] = { DB8540_PIN_AJ6 };
static const unsigned clkout2_a_2_pins[] = { DB8540_PIN_C12 };
static const unsigned msp4_a_1_pins[] = { DB8540_PIN_B14, DB8540_PIN_E11 };
static const unsigned usb_a_1_pins[] = { DB8540_PIN_D12, DB8540_PIN_D15,
	DB8540_PIN_C13, DB8540_PIN_C14, DB8540_PIN_C18, DB8540_PIN_C16,
	DB8540_PIN_B16, DB8540_PIN_D18, DB8540_PIN_C15, DB8540_PIN_C17,
	DB8540_PIN_B17, DB8540_PIN_D17 };
/* Altfunction B colum */
static const unsigned apetrig_b_1_pins[] = { DB8540_PIN_AH6, DB8540_PIN_AG7 };
static const unsigned modtrig_b_1_pins[] = { DB8540_PIN_AF2, DB8540_PIN_AD3 };
static const unsigned i2c4_b_1_pins[] = { DB8540_PIN_AF6, DB8540_PIN_AG6 };
static const unsigned i2c1_b_1_pins[] = { DB8540_PIN_AD5, DB8540_PIN_AF7 };
static const unsigned i2c2_b_1_pins[] = { DB8540_PIN_AG5, DB8540_PIN_AH5 };
static const unsigned i2c2_b_2_pins[] = { DB8540_PIN_AE4, DB8540_PIN_AD1 };
static const unsigned msp0txrx_b_1_pins[] = { DB8540_PIN_AD2, DB8540_PIN_AC3 };
static const unsigned i2c1_b_2_pins[] = { DB8540_PIN_AH7, DB8540_PIN_AE7 };
static const unsigned stmmod_b_1_pins[] = { DB8540_PIN_AH11, DB8540_PIN_AF11,
	DB8540_PIN_AH10, DB8540_PIN_AG10, DB8540_PIN_AF10 };
static const unsigned moduartstmmux_b_1_pins[] = { DB8540_PIN_AG11 };
static const unsigned msp1txrx_b_1_pins[] = { DB8540_PIN_AD4, DB8540_PIN_AG4 };
static const unsigned kp_b_1_pins[] = { DB8540_PIN_AJ24, DB8540_PIN_AE21,
	DB8540_PIN_M26, DB8540_PIN_M25, DB8540_PIN_M27, DB8540_PIN_N25,
	DB8540_PIN_M28, DB8540_PIN_N26, DB8540_PIN_M22, DB8540_PIN_N22,
	DB8540_PIN_N27, DB8540_PIN_N28, DB8540_PIN_P22, DB8540_PIN_P28,
	DB8540_PIN_P26, DB8540_PIN_T22, DB8540_PIN_R27, DB8540_PIN_P27,
	DB8540_PIN_R26, DB8540_PIN_R25 };
static const unsigned u2txrx_b_1_pins[] = { DB8540_PIN_U22, DB8540_PIN_T27 };
static const unsigned sm_b_1_pins[] = { DB8540_PIN_AG22, DB8540_PIN_AF21,
	DB8540_PIN_AF24, DB8540_PIN_AH22, DB8540_PIN_AJ23, DB8540_PIN_AH21,
	DB8540_PIN_AG20, DB8540_PIN_AE23, DB8540_PIN_AH20, DB8540_PIN_AF22,
	DB8540_PIN_AJ21, DB8540_PIN_AC27, DB8540_PIN_AD27, DB8540_PIN_AE28,
	DB8540_PIN_AG26, DB8540_PIN_AF25, DB8540_PIN_AE27, DB8540_PIN_AF27,
	DB8540_PIN_AG28, DB8540_PIN_AF28, DB8540_PIN_AG25, DB8540_PIN_AG24,
	DB8540_PIN_AD25 };
static const unsigned smcs0_b_1_pins[] = { DB8540_PIN_AG19 };
static const unsigned smcs1_b_1_pins[] = { DB8540_PIN_AE26 };
static const unsigned ipgpio7_b_1_pins[] = { DB8540_PIN_AH25 };
static const unsigned ipgpio2_b_1_pins[] = { DB8540_PIN_AF26 };
static const unsigned ipgpio3_b_1_pins[] = { DB8540_PIN_AF23 };
static const unsigned i2c6_b_1_pins[] = { DB8540_PIN_AG23, DB8540_PIN_AE25 };
static const unsigned i2c5_b_1_pins[] = { DB8540_PIN_AH24, DB8540_PIN_AJ25 };
static const unsigned u3txrx_b_1_pins[] = { DB8540_PIN_AF20, DB8540_PIN_AG21 };
static const unsigned u3ctsrts_b_1_pins[] = { DB8540_PIN_AH19,
	DB8540_PIN_AE19 };
static const unsigned i2c5_b_2_pins[] = { DB8540_PIN_AG18, DB8540_PIN_AH17 };
static const unsigned i2c4_b_2_pins[] = { DB8540_PIN_AF19, DB8540_PIN_AF18 };
static const unsigned u4txrx_b_1_pins[] = { DB8540_PIN_AE18, DB8540_PIN_AG17 };
static const unsigned u4ctsrts_b_1_pins[] = { DB8540_PIN_AF17,
	DB8540_PIN_AE17 };
static const unsigned ddrtrig_b_1_pins[] = { DB8540_PIN_J3 };
static const unsigned msp4_b_1_pins[] = { DB8540_PIN_H3 };
static const unsigned pwl_b_1_pins[] = { DB8540_PIN_C6 };
static const unsigned spi1_b_1_pins[] = { DB8540_PIN_E2, DB8540_PIN_C10,
	DB8540_PIN_C8, DB8540_PIN_C9 };
static const unsigned mc3_b_1_pins[] = { DB8540_PIN_B9, DB8540_PIN_A10,
	DB8540_PIN_D9, DB8540_PIN_B11, DB8540_PIN_B10, DB8540_PIN_E10,
	DB8540_PIN_B12 };
static const unsigned pwl_b_2_pins[] = { DB8540_PIN_D10 };
static const unsigned pwl_b_3_pins[] = { DB8540_PIN_B13 };
static const unsigned pwl_b_4_pins[] = { DB8540_PIN_C12 };
static const unsigned u2txrx_b_2_pins[] = { DB8540_PIN_B17, DB8540_PIN_D17 };

/* Altfunction C column */
static const unsigned ipgpio6_c_1_pins[] = { DB8540_PIN_AG6 };
static const unsigned ipgpio0_c_1_pins[] = { DB8540_PIN_AD5 };
static const unsigned ipgpio1_c_1_pins[] = { DB8540_PIN_AF7 };
static const unsigned ipgpio3_c_1_pins[] = { DB8540_PIN_AE4 };
static const unsigned ipgpio2_c_1_pins[] = { DB8540_PIN_AD1 };
static const unsigned u0_c_1_pins[] = { DB8540_PIN_AD4, DB8540_PIN_AF3,
	DB8540_PIN_AF5, DB8540_PIN_AG4 };
static const unsigned smcleale_c_1_pins[] = { DB8540_PIN_AJ24,
	DB8540_PIN_AE21 };
static const unsigned ipgpio4_c_1_pins[] = { DB8540_PIN_M26 };
static const unsigned ipgpio5_c_1_pins[] = { DB8540_PIN_M25 };
static const unsigned ipgpio6_c_2_pins[] = { DB8540_PIN_M27 };
static const unsigned ipgpio7_c_1_pins[] = { DB8540_PIN_N25 };
static const unsigned stmape_c_1_pins[] = { DB8540_PIN_M28, DB8540_PIN_N26,
	DB8540_PIN_M22, DB8540_PIN_N22, DB8540_PIN_N27 };
static const unsigned u2rxtx_c_1_pins[] = { DB8540_PIN_N28, DB8540_PIN_P22 };
static const unsigned modobsresout_c_1_pins[] = { DB8540_PIN_P28 };
static const unsigned ipgpio2_c_2_pins[] = { DB8540_PIN_P26 };
static const unsigned ipgpio3_c_2_pins[] = { DB8540_PIN_T22 };
static const unsigned ipgpio4_c_2_pins[] = { DB8540_PIN_R27 };
static const unsigned ipgpio5_c_2_pins[] = { DB8540_PIN_P27 };
static const unsigned modaccgpo_c_1_pins[] = { DB8540_PIN_R26, DB8540_PIN_R25,
	DB8540_PIN_U22 };
static const unsigned modobspwrrst_c_1_pins[] = { DB8540_PIN_T27 };
static const unsigned mc5_c_1_pins[] = { DB8540_PIN_AG22, DB8540_PIN_AF21,
	DB8540_PIN_AF24, DB8540_PIN_AH22, DB8540_PIN_AJ23, DB8540_PIN_AH21,
	DB8540_PIN_AG20, DB8540_PIN_AE23, DB8540_PIN_AH20, DB8540_PIN_AF22,
	DB8540_PIN_AJ21};
static const unsigned smps0_c_1_pins[] = { DB8540_PIN_AG19 };
static const unsigned moduart1_c_1_pins[] = { DB8540_PIN_T25, DB8540_PIN_T26 };
static const unsigned mc2rstn_c_1_pins[] = { DB8540_PIN_AE28 };
static const unsigned i2c5_c_1_pins[] = { DB8540_PIN_AG28, DB8540_PIN_AF28 };
static const unsigned ipgpio0_c_2_pins[] = { DB8540_PIN_AG25 };
static const unsigned ipgpio1_c_2_pins[] = { DB8540_PIN_AG24 };
static const unsigned kp_c_1_pins[] = { DB8540_PIN_AD25, DB8540_PIN_AH25,
	DB8540_PIN_AF26, DB8540_PIN_AF23 };
static const unsigned modrf_c_1_pins[] = { DB8540_PIN_AG23, DB8540_PIN_AE25,
	DB8540_PIN_AH24 };
static const unsigned smps1_c_1_pins[] = { DB8540_PIN_AE26 };
static const unsigned i2c5_c_2_pins[] = { DB8540_PIN_AH19, DB8540_PIN_AE19 };
static const unsigned u4ctsrts_c_1_pins[] = { DB8540_PIN_AG18,
	DB8540_PIN_AH17 };
static const unsigned u3rxtx_c_1_pins[] = { DB8540_PIN_AF19, DB8540_PIN_AF18 };
static const unsigned msp4_c_1_pins[] = { DB8540_PIN_J3 };
static const unsigned mc4rstn_c_1_pins[] = { DB8540_PIN_C6 };
static const unsigned spi0_c_1_pins[] = { DB8540_PIN_A10, DB8540_PIN_B10,
	DB8540_PIN_E10, DB8540_PIN_B12 };
static const unsigned i2c3_c_1_pins[] = { DB8540_PIN_B13, DB8540_PIN_C12 };

/* Other alt C1 column */
static const unsigned spi3_oc1_1_pins[] = { DB8540_PIN_AG5, DB8540_PIN_AH5,
	DB8540_PIN_AE4, DB8540_PIN_AD1 };
static const unsigned stmape_oc1_1_pins[] = { DB8540_PIN_AH11, DB8540_PIN_AF11,
	DB8540_PIN_AH10, DB8540_PIN_AG10, DB8540_PIN_AF10 };
static const unsigned u2_oc1_1_pins[] = { DB8540_PIN_AG11 };
static const unsigned remap0_oc1_1_pins[] = { DB8540_PIN_AJ24 };
static const unsigned remap1_oc1_1_pins[] = { DB8540_PIN_AE21 };
static const unsigned modobsrefclk_oc1_1_pins[] = { DB8540_PIN_M26 };
static const unsigned modobspwrctrl_oc1_1_pins[] = { DB8540_PIN_M25 };
static const unsigned modobsclkout_oc1_1_pins[] = { DB8540_PIN_M27 };
static const unsigned moduart1_oc1_1_pins[] = { DB8540_PIN_N25 };
static const unsigned modprcmudbg_oc1_1_pins[] = { DB8540_PIN_M28,
	DB8540_PIN_N26, DB8540_PIN_M22, DB8540_PIN_N22, DB8540_PIN_N27,
	DB8540_PIN_P22, DB8540_PIN_P28, DB8540_PIN_P26, DB8540_PIN_T22,
	DB8540_PIN_R26, DB8540_PIN_R25, DB8540_PIN_U22, DB8540_PIN_T27,
	DB8540_PIN_AH20, DB8540_PIN_AG19, DB8540_PIN_AF22, DB8540_PIN_AJ21,
	DB8540_PIN_T25};
static const unsigned modobsresout_oc1_1_pins[] = { DB8540_PIN_N28 };
static const unsigned modaccgpo_oc1_1_pins[] = { DB8540_PIN_R27, DB8540_PIN_P27,
	DB8540_PIN_T26 };
static const unsigned kp_oc1_1_pins[] = { DB8540_PIN_AG22, DB8540_PIN_AF21,
	DB8540_PIN_AF24, DB8540_PIN_AH22, DB8540_PIN_AJ23, DB8540_PIN_AH21,
	DB8540_PIN_AG20, DB8540_PIN_AE23 };
static const unsigned modxmip_oc1_1_pins[] = { DB8540_PIN_AD25, DB8540_PIN_AH25,
	DB8540_PIN_AG23, DB8540_PIN_AE25 };
static const unsigned i2c6_oc1_1_pins[] = { DB8540_PIN_AE26, DB8540_PIN_AE24 };
static const unsigned u2txrx_oc1_1_pins[] = { DB8540_PIN_B7, DB8540_PIN_A7 };
static const unsigned u2ctsrts_oc1_1_pins[] = { DB8540_PIN_D7, DB8540_PIN_D8 };

/* Other alt C2 column */
static const unsigned sbag_oc2_1_pins[] = { DB8540_PIN_AH11, DB8540_PIN_AG11,
	DB8540_PIN_AF11, DB8540_PIN_AH10, DB8540_PIN_AG10, DB8540_PIN_AF10 };
static const unsigned hxclk_oc2_1_pins[] = { DB8540_PIN_M25 };
static const unsigned modaccuart_oc2_1_pins[] = { DB8540_PIN_N25 };
static const unsigned stmmod_oc2_1_pins[] = { DB8540_PIN_M28, DB8540_PIN_N26,
	DB8540_PIN_M22, DB8540_PIN_N22, DB8540_PIN_N27 };
static const unsigned moduartstmmux_oc2_1_pins[] = { DB8540_PIN_N28 };
static const unsigned hxgpio_oc2_1_pins[] = { DB8540_PIN_P22, DB8540_PIN_P28,
	DB8540_PIN_P26, DB8540_PIN_T22, DB8540_PIN_R27, DB8540_PIN_P27,
	DB8540_PIN_R26, DB8540_PIN_R25 };
static const unsigned sbag_oc2_2_pins[] = { DB8540_PIN_U22, DB8540_PIN_T27,
	DB8540_PIN_AG22, DB8540_PIN_AF21, DB8540_PIN_AF24, DB8540_PIN_AH22 };
static const unsigned modobsservice_oc2_1_pins[] = { DB8540_PIN_AJ23 };
static const unsigned moduart0_oc2_1_pins[] = { DB8540_PIN_AG20,
	DB8540_PIN_AE23 };
static const unsigned stmape_oc2_1_pins[] = { DB8540_PIN_AH20, DB8540_PIN_AG19,
	DB8540_PIN_AF22, DB8540_PIN_AJ21, DB8540_PIN_T25 };
static const unsigned u2_oc2_1_pins[] = { DB8540_PIN_T26, DB8540_PIN_AH21 };
static const unsigned modxmip_oc2_1_pins[] = { DB8540_PIN_AE26,
	DB8540_PIN_AE24 };

/* Other alt C3 column */
static const unsigned modaccgpo_oc3_1_pins[] = { DB8540_PIN_AG11 };
static const unsigned tpui_oc3_1_pins[] = { DB8540_PIN_M26, DB8540_PIN_M25,
	DB8540_PIN_M27, DB8540_PIN_N25, DB8540_PIN_M28, DB8540_PIN_N26,
	DB8540_PIN_M22, DB8540_PIN_N22, DB8540_PIN_N27, DB8540_PIN_N28,
	DB8540_PIN_P22, DB8540_PIN_P28, DB8540_PIN_P26, DB8540_PIN_T22,
	DB8540_PIN_R27, DB8540_PIN_P27, DB8540_PIN_R26, DB8540_PIN_R25,
	DB8540_PIN_U22, DB8540_PIN_T27, DB8540_PIN_AG22, DB8540_PIN_AF21,
	DB8540_PIN_AF24, DB8540_PIN_AH22, DB8540_PIN_AJ23, DB8540_PIN_AH21,
	DB8540_PIN_AG20, DB8540_PIN_AE23, DB8540_PIN_AH20, DB8540_PIN_AG19,
	DB8540_PIN_AF22, DB8540_PIN_AJ21, DB8540_PIN_T25, DB8540_PIN_T26 };

/* Other alt C4 column */
static const unsigned hwobs_oc4_1_pins[] = { DB8540_PIN_M26, DB8540_PIN_M25,
	DB8540_PIN_M27, DB8540_PIN_N25, DB8540_PIN_M28, DB8540_PIN_N26,
	DB8540_PIN_M22, DB8540_PIN_N22, DB8540_PIN_N27, DB8540_PIN_N28,
	DB8540_PIN_P22, DB8540_PIN_P28, DB8540_PIN_P26, DB8540_PIN_T22,
	DB8540_PIN_R27, DB8540_PIN_P27, DB8540_PIN_R26, DB8540_PIN_R25 };
static const unsigned moduart1txrx_oc4_1_pins[] = { DB8540_PIN_U22,
	DB8540_PIN_T27 };
static const unsigned moduart1rtscts_oc4_1_pins[] = { DB8540_PIN_AG22,
	DB8540_PIN_AF21 };
static const unsigned modaccuarttxrx_oc4_1_pins[] = { DB8540_PIN_AF24,
	DB8540_PIN_AH22 };
static const unsigned modaccuartrtscts_oc4_1_pins[] = { DB8540_PIN_AJ23,
	DB8540_PIN_AH21 };
static const unsigned stmmod_oc4_1_pins[] = { DB8540_PIN_AH20, DB8540_PIN_AG19,
	DB8540_PIN_AF22, DB8540_PIN_AJ21, DB8540_PIN_T25 };
static const unsigned moduartstmmux_oc4_1_pins[] = { DB8540_PIN_T26 };

#define DB8540_PIN_GROUP(a, b) { .name = #a, .pins = a##_pins,		\
			.npins = ARRAY_SIZE(a##_pins), .altsetting = b }

static const struct nmk_pingroup nmk_db8540_groups[] = {
	/* Altfunction A column */
	DB8540_PIN_GROUP(u0_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(u1rxtx_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(u1ctsrts_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(ipi2c_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(ipi2c_a_2, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(msp0txrx_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(msp0tfstck_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(msp0rfsrck_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(mc0_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(msp1txrx_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(msp1_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(modobsclk_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(clkoutreq_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(lcdb_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(lcdvsi0_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(lcdvsi1_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(lcd_d0_d7_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(lcd_d8_d11_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(lcd_d12_d23_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(kp_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(mc2_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(ssp1_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(ssp0_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(i2c0_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(ipgpio0_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(ipgpio1_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(modi2s_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(spi2_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(u2txrx_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(u2ctsrts_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(modsmb_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(msp2sck_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(msp2txdtcktfs_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(msp2rxd_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(mc4_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(mc1_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(hsir_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(hsit_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(hsit_a_2, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(clkout1_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(clkout1_a_2, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(clkout2_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(clkout2_a_2, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(msp4_a_1, NMK_GPIO_ALT_A),
	DB8540_PIN_GROUP(usb_a_1, NMK_GPIO_ALT_A),
	/* Altfunction B column */
	DB8540_PIN_GROUP(apetrig_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(modtrig_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(i2c4_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(i2c1_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(i2c2_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(i2c2_b_2, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(msp0txrx_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(i2c1_b_2, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(stmmod_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(moduartstmmux_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(msp1txrx_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(kp_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(u2txrx_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(sm_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(smcs0_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(smcs1_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(ipgpio7_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(ipgpio2_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(ipgpio3_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(i2c6_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(i2c5_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(u3txrx_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(u3ctsrts_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(i2c5_b_2, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(i2c4_b_2, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(u4txrx_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(u4ctsrts_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(ddrtrig_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(msp4_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(pwl_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(spi1_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(mc3_b_1, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(pwl_b_2, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(pwl_b_3, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(pwl_b_4, NMK_GPIO_ALT_B),
	DB8540_PIN_GROUP(u2txrx_b_2, NMK_GPIO_ALT_B),
	/* Altfunction C column */
	DB8540_PIN_GROUP(ipgpio6_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio0_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio1_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio3_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio2_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(u0_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(smcleale_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio4_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio5_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio6_c_2, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio7_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(stmape_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(u2rxtx_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(modobsresout_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio2_c_2, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio3_c_2, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio4_c_2, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio5_c_2, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(modaccgpo_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(modobspwrrst_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(mc5_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(smps0_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(moduart1_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(mc2rstn_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(i2c5_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio0_c_2, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(ipgpio1_c_2, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(kp_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(modrf_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(smps1_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(i2c5_c_2, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(u4ctsrts_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(u3rxtx_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(msp4_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(mc4rstn_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(spi0_c_1, NMK_GPIO_ALT_C),
	DB8540_PIN_GROUP(i2c3_c_1, NMK_GPIO_ALT_C),

	/* Other alt C1 column */
	DB8540_PIN_GROUP(spi3_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(stmape_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(u2_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(remap0_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(remap1_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(modobsrefclk_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(modobspwrctrl_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(modobsclkout_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(moduart1_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(modprcmudbg_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(modobsresout_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(modaccgpo_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(kp_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(modxmip_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(i2c6_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(u2txrx_oc1_1, NMK_GPIO_ALT_C1),
	DB8540_PIN_GROUP(u2ctsrts_oc1_1, NMK_GPIO_ALT_C1),

	/* Other alt C2 column */
	DB8540_PIN_GROUP(sbag_oc2_1, NMK_GPIO_ALT_C2),
	DB8540_PIN_GROUP(hxclk_oc2_1, NMK_GPIO_ALT_C2),
	DB8540_PIN_GROUP(modaccuart_oc2_1, NMK_GPIO_ALT_C2),
	DB8540_PIN_GROUP(stmmod_oc2_1, NMK_GPIO_ALT_C2),
	DB8540_PIN_GROUP(moduartstmmux_oc2_1, NMK_GPIO_ALT_C2),
	DB8540_PIN_GROUP(hxgpio_oc2_1, NMK_GPIO_ALT_C2),
	DB8540_PIN_GROUP(sbag_oc2_2, NMK_GPIO_ALT_C2),
	DB8540_PIN_GROUP(modobsservice_oc2_1, NMK_GPIO_ALT_C2),
	DB8540_PIN_GROUP(moduart0_oc2_1, NMK_GPIO_ALT_C2),
	DB8540_PIN_GROUP(stmape_oc2_1, NMK_GPIO_ALT_C2),
	DB8540_PIN_GROUP(u2_oc2_1, NMK_GPIO_ALT_C2),
	DB8540_PIN_GROUP(modxmip_oc2_1, NMK_GPIO_ALT_C2),

	/* Other alt C3 column */
	DB8540_PIN_GROUP(modaccgpo_oc3_1, NMK_GPIO_ALT_C3),
	DB8540_PIN_GROUP(tpui_oc3_1, NMK_GPIO_ALT_C3),

	/* Other alt C4 column */
	DB8540_PIN_GROUP(hwobs_oc4_1, NMK_GPIO_ALT_C4),
	DB8540_PIN_GROUP(moduart1txrx_oc4_1, NMK_GPIO_ALT_C4),
	DB8540_PIN_GROUP(moduart1rtscts_oc4_1, NMK_GPIO_ALT_C4),
	DB8540_PIN_GROUP(modaccuarttxrx_oc4_1, NMK_GPIO_ALT_C4),
	DB8540_PIN_GROUP(modaccuartrtscts_oc4_1, NMK_GPIO_ALT_C4),
	DB8540_PIN_GROUP(stmmod_oc4_1, NMK_GPIO_ALT_C4),
	DB8540_PIN_GROUP(moduartstmmux_oc4_1, NMK_GPIO_ALT_C4),

};

/* We use this macro to define the groups applicable to a function */
#define DB8540_FUNC_GROUPS(a, b...)	   \
static const char * const a##_groups[] = { b };

DB8540_FUNC_GROUPS(apetrig, "apetrig_b_1");
DB8540_FUNC_GROUPS(clkout, "clkoutreq_a_1", "clkout1_a_1", "clkout1_a_2",
		"clkout2_a_1", "clkout2_a_2");
DB8540_FUNC_GROUPS(ddrtrig, "ddrtrig_b_1");
DB8540_FUNC_GROUPS(hsi, "hsir_a_1", "hsit_a_1", "hsit_a_2");
DB8540_FUNC_GROUPS(hwobs, "hwobs_oc4_1");
DB8540_FUNC_GROUPS(hx, "hxclk_oc2_1", "hxgpio_oc2_1");
DB8540_FUNC_GROUPS(i2c0, "i2c0_a_1");
DB8540_FUNC_GROUPS(i2c1, "i2c1_b_1", "i2c1_b_2");
DB8540_FUNC_GROUPS(i2c2, "i2c2_b_1", "i2c2_b_2");
DB8540_FUNC_GROUPS(i2c3, "i2c3_c_1", "i2c4_b_1");
DB8540_FUNC_GROUPS(i2c4, "i2c4_b_2");
DB8540_FUNC_GROUPS(i2c5, "i2c5_b_1", "i2c5_b_2", "i2c5_c_1", "i2c5_c_2");
DB8540_FUNC_GROUPS(i2c6, "i2c6_b_1", "i2c6_oc1_1");
/* The image processor has 8 GPIO pins that can be muxed out */
DB8540_FUNC_GROUPS(ipgpio, "ipgpio0_a_1", "ipgpio0_c_1", "ipgpio0_c_2",
		"ipgpio1_a_1", "ipgpio1_c_1", "ipgpio1_c_2",
		"ipgpio2_b_1", "ipgpio2_c_1", "ipgpio2_c_2",
		"ipgpio3_b_1", "ipgpio3_c_1", "ipgpio3_c_2",
		"ipgpio4_c_1", "ipgpio4_c_2",
		"ipgpio5_c_1", "ipgpio5_c_2",
		"ipgpio6_c_1", "ipgpio6_c_2",
		"ipgpio7_b_1", "ipgpio7_c_1");
DB8540_FUNC_GROUPS(ipi2c, "ipi2c_a_1", "ipi2c_a_2");
DB8540_FUNC_GROUPS(kp, "kp_a_1", "kp_b_1", "kp_c_1", "kp_oc1_1");
DB8540_FUNC_GROUPS(lcd, "lcd_d0_d7_a_1", "lcd_d12_d23_a_1", "lcd_d8_d11_a_1",
		"lcdvsi0_a_1", "lcdvsi1_a_1");
DB8540_FUNC_GROUPS(lcdb, "lcdb_a_1");
DB8540_FUNC_GROUPS(mc0, "mc0_a_1");
DB8540_FUNC_GROUPS(mc1, "mc1_a_1", "mc1_a_2");
DB8540_FUNC_GROUPS(mc2, "mc2_a_1", "mc2rstn_c_1");
DB8540_FUNC_GROUPS(mc3, "mc3_b_1");
DB8540_FUNC_GROUPS(mc4, "mc4_a_1", "mc4rstn_c_1");
DB8540_FUNC_GROUPS(mc5, "mc5_c_1");
DB8540_FUNC_GROUPS(modaccgpo, "modaccgpo_c_1", "modaccgpo_oc1_1",
		"modaccgpo_oc3_1");
DB8540_FUNC_GROUPS(modaccuart, "modaccuart_oc2_1", "modaccuarttxrx_oc4_1",
		"modaccuartrtccts_oc4_1");
DB8540_FUNC_GROUPS(modi2s, "modi2s_a_1");
DB8540_FUNC_GROUPS(modobs, "modobsclk_a_1", "modobsclkout_oc1_1",
		"modobspwrctrl_oc1_1", "modobspwrrst_c_1",
		"modobsrefclk_oc1_1", "modobsresout_c_1",
		"modobsresout_oc1_1", "modobsservice_oc2_1");
DB8540_FUNC_GROUPS(modprcmudbg, "modprcmudbg_oc1_1");
DB8540_FUNC_GROUPS(modrf, "modrf_c_1");
DB8540_FUNC_GROUPS(modsmb, "modsmb_a_1");
DB8540_FUNC_GROUPS(modtrig, "modtrig_b_1");
DB8540_FUNC_GROUPS(moduart, "moduart1_c_1", "moduart1_oc1_1",
		"moduart1txrx_oc4_1", "moduart1rtscts_oc4_1", "moduart0_oc2_1");
DB8540_FUNC_GROUPS(moduartstmmux, "moduartstmmux_b_1", "moduartstmmux_oc2_1",
		"moduartstmmux_oc4_1");
DB8540_FUNC_GROUPS(modxmip, "modxmip_oc1_1", "modxmip_oc2_1");
/*
 * MSP0 can only be on a certain set of pins, but the TX/RX pins can be
 * switched around by selecting the altfunction A or B.
 */
DB8540_FUNC_GROUPS(msp0, "msp0rfsrck_a_1", "msp0tfstck_a_1", "msp0txrx_a_1",
		"msp0txrx_b_1");
DB8540_FUNC_GROUPS(msp1, "msp1_a_1", "msp1txrx_a_1", "msp1txrx_b_1");
DB8540_FUNC_GROUPS(msp2, "msp2sck_a_1", "msp2txdtcktfs_a_1", "msp2rxd_a_1");
DB8540_FUNC_GROUPS(msp4, "msp4_a_1", "msp4_b_1", "msp4_c_1");
DB8540_FUNC_GROUPS(pwl, "pwl_b_1", "pwl_b_2", "pwl_b_3", "pwl_b_4");
DB8540_FUNC_GROUPS(remap, "remap0_oc1_1", "remap1_oc1_1");
DB8540_FUNC_GROUPS(sbag, "sbag_oc2_1", "sbag_oc2_2");
/* Select between CS0 on alt B or PS1 on alt C */
DB8540_FUNC_GROUPS(sm, "sm_b_1", "smcleale_c_1", "smcs0_b_1", "smcs1_b_1",
		"smps0_c_1", "smps1_c_1");
DB8540_FUNC_GROUPS(spi0, "spi0_c_1");
DB8540_FUNC_GROUPS(spi1, "spi1_b_1");
DB8540_FUNC_GROUPS(spi2, "spi2_a_1");
DB8540_FUNC_GROUPS(spi3, "spi3_oc1_1");
DB8540_FUNC_GROUPS(ssp0, "ssp0_a_1");
DB8540_FUNC_GROUPS(ssp1, "ssp1_a_1");
DB8540_FUNC_GROUPS(stmape, "stmape_c_1", "stmape_oc1_1", "stmape_oc2_1");
DB8540_FUNC_GROUPS(stmmod, "stmmod_b_1", "stmmod_oc2_1", "stmmod_oc4_1");
DB8540_FUNC_GROUPS(tpui, "tpui_oc3_1");
DB8540_FUNC_GROUPS(u0, "u0_a_1", "u0_c_1");
DB8540_FUNC_GROUPS(u1, "u1ctsrts_a_1", "u1rxtx_a_1");
DB8540_FUNC_GROUPS(u2, "u2_oc1_1", "u2_oc2_1", "u2ctsrts_a_1", "u2ctsrts_oc1_1",
		"u2rxtx_c_1", "u2txrx_a_1", "u2txrx_b_1", "u2txrx_b_2",
		"u2txrx_oc1_1");
DB8540_FUNC_GROUPS(u3, "u3ctsrts_b_1", "u3rxtx_c_1", "u3txrxa_b_1");
DB8540_FUNC_GROUPS(u4, "u4ctsrts_b_1", "u4ctsrts_c_1", "u4txrx_b_1");
DB8540_FUNC_GROUPS(usb, "usb_a_1");


#define FUNCTION(fname)					\
	{						\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

static const struct nmk_function nmk_db8540_functions[] = {
	FUNCTION(apetrig),
	FUNCTION(clkout),
	FUNCTION(ddrtrig),
	FUNCTION(hsi),
	FUNCTION(hwobs),
	FUNCTION(hx),
	FUNCTION(i2c0),
	FUNCTION(i2c1),
	FUNCTION(i2c2),
	FUNCTION(i2c3),
	FUNCTION(i2c4),
	FUNCTION(i2c5),
	FUNCTION(i2c6),
	FUNCTION(ipgpio),
	FUNCTION(ipi2c),
	FUNCTION(kp),
	FUNCTION(lcd),
	FUNCTION(lcdb),
	FUNCTION(mc0),
	FUNCTION(mc1),
	FUNCTION(mc2),
	FUNCTION(mc3),
	FUNCTION(mc4),
	FUNCTION(mc5),
	FUNCTION(modaccgpo),
	FUNCTION(modaccuart),
	FUNCTION(modi2s),
	FUNCTION(modobs),
	FUNCTION(modprcmudbg),
	FUNCTION(modrf),
	FUNCTION(modsmb),
	FUNCTION(modtrig),
	FUNCTION(moduart),
	FUNCTION(modxmip),
	FUNCTION(msp0),
	FUNCTION(msp1),
	FUNCTION(msp2),
	FUNCTION(msp4),
	FUNCTION(pwl),
	FUNCTION(remap),
	FUNCTION(sbag),
	FUNCTION(sm),
	FUNCTION(spi0),
	FUNCTION(spi1),
	FUNCTION(spi2),
	FUNCTION(spi3),
	FUNCTION(ssp0),
	FUNCTION(ssp1),
	FUNCTION(stmape),
	FUNCTION(stmmod),
	FUNCTION(tpui),
	FUNCTION(u0),
	FUNCTION(u1),
	FUNCTION(u2),
	FUNCTION(u3),
	FUNCTION(u4),
	FUNCTION(usb)
};

static const struct prcm_gpiocr_altcx_pin_desc db8540_altcx_pins[] = {
	PRCM_GPIOCR_ALTCX(8,	true, PRCM_IDX_GPIOCR1, 20,	/* SPI3_CLK */
				false, 0, 0,
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(9,	true, PRCM_IDX_GPIOCR1, 20,	/* SPI3_RXD */
				false, 0, 0,
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(10,	true, PRCM_IDX_GPIOCR1, 20,	/* SPI3_FRM */
				false, 0, 0,
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(11,	true, PRCM_IDX_GPIOCR1, 20,	/* SPI3_TXD */
				false, 0, 0,
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(23,	true, PRCM_IDX_GPIOCR1, 9,	/* STMAPE_CLK_a */
				true, PRCM_IDX_GPIOCR2, 10,	/* SBAG_CLK_a */
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(24,	true, PRCM_IDX_GPIOCR3, 30,	/* U2_RXD_g */
				true, PRCM_IDX_GPIOCR2, 10,	/* SBAG_VAL_a */
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(25,	true, PRCM_IDX_GPIOCR1, 9,	/* STMAPE_DAT_a[0] */
				true, PRCM_IDX_GPIOCR2, 10,	/* SBAG_D_a[0] */
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(26,	true, PRCM_IDX_GPIOCR1, 9,	/* STMAPE_DAT_a[1] */
				true, PRCM_IDX_GPIOCR2, 10,	/* SBAG_D_a[1] */
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(27,	true, PRCM_IDX_GPIOCR1, 9,	/* STMAPE_DAT_a[2] */
				true, PRCM_IDX_GPIOCR2, 10,	/* SBAG_D_a[2] */
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(28,	true, PRCM_IDX_GPIOCR1, 9,	/* STMAPE_DAT_a[3] */
				true, PRCM_IDX_GPIOCR2, 10,	/* SBAG_D_a[3] */
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(64,	true, PRCM_IDX_GPIOCR1, 15,	/* MODOBS_REFCLK_REQ */
				false, 0, 0,
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_CTL */
				true, PRCM_IDX_GPIOCR2, 23	/* HW_OBS_APE_PRCMU[17] */
	),
	PRCM_GPIOCR_ALTCX(65,	true, PRCM_IDX_GPIOCR1, 19,	/* MODOBS_PWRCTRL0 */
				true, PRCM_IDX_GPIOCR1, 24,	/* Hx_CLK */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_CLK */
				true, PRCM_IDX_GPIOCR2, 24	/* HW_OBS_APE_PRCMU[16] */
	),
	PRCM_GPIOCR_ALTCX(66,	true, PRCM_IDX_GPIOCR1, 15,	/* MODOBS_CLKOUT1 */
				false, 0, 0,
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[15] */
				true, PRCM_IDX_GPIOCR2, 25	/* HW_OBS_APE_PRCMU[15] */
	),
	PRCM_GPIOCR_ALTCX(67,	true, PRCM_IDX_GPIOCR1, 1,	/* MODUART1_TXD_a */
				true, PRCM_IDX_GPIOCR1, 6,	/* MODACCUART_TXD_a */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[14] */
				true, PRCM_IDX_GPIOCR2, 26	/* HW_OBS_APE_PRCMU[14] */
	),
	PRCM_GPIOCR_ALTCX(70,	true, PRCM_IDX_GPIOCR3, 6,	/* MOD_PRCMU_DEBUG[17] */
				true, PRCM_IDX_GPIOCR1, 10,	/* STMMOD_CLK_b */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[13] */
				true, PRCM_IDX_GPIOCR2, 27	/* HW_OBS_APE_PRCMU[13] */
	),
	PRCM_GPIOCR_ALTCX(71,	true, PRCM_IDX_GPIOCR3, 6,	/* MOD_PRCMU_DEBUG[16] */
				true, PRCM_IDX_GPIOCR1, 10,	/* STMMOD_DAT_b[3] */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[12] */
				true, PRCM_IDX_GPIOCR2, 27	/* HW_OBS_APE_PRCMU[12] */
	),
	PRCM_GPIOCR_ALTCX(72,	true, PRCM_IDX_GPIOCR3, 6,	/* MOD_PRCMU_DEBUG[15] */
				true, PRCM_IDX_GPIOCR1, 10,	/* STMMOD_DAT_b[2] */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[11] */
				true, PRCM_IDX_GPIOCR2, 27	/* HW_OBS_APE_PRCMU[11] */
	),
	PRCM_GPIOCR_ALTCX(73,	true, PRCM_IDX_GPIOCR3, 6,	/* MOD_PRCMU_DEBUG[14] */
				true, PRCM_IDX_GPIOCR1, 10,	/* STMMOD_DAT_b[1] */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[10] */
				true, PRCM_IDX_GPIOCR2, 27	/* HW_OBS_APE_PRCMU[10] */
	),
	PRCM_GPIOCR_ALTCX(74,	true, PRCM_IDX_GPIOCR3, 6,	/* MOD_PRCMU_DEBUG[13] */
				true, PRCM_IDX_GPIOCR1, 10,	/* STMMOD_DAT_b[0] */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[9] */
				true, PRCM_IDX_GPIOCR2, 27	/* HW_OBS_APE_PRCMU[9] */
	),
	PRCM_GPIOCR_ALTCX(75,	true, PRCM_IDX_GPIOCR1, 12,	/* MODOBS_RESOUT0_N */
				true, PRCM_IDX_GPIOCR2, 1,	/* MODUART_STMMUX_RXD_b */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[8] */
				true, PRCM_IDX_GPIOCR2, 28	/* HW_OBS_APE_PRCMU[8] */
	),
	PRCM_GPIOCR_ALTCX(76,	true, PRCM_IDX_GPIOCR3, 7,	/* MOD_PRCMU_DEBUG[12] */
				true, PRCM_IDX_GPIOCR1, 25,	/* Hx_GPIO[7] */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[7] */
				true, PRCM_IDX_GPIOCR2, 29	/* HW_OBS_APE_PRCMU[7] */
	),
	PRCM_GPIOCR_ALTCX(77,	true, PRCM_IDX_GPIOCR3, 7,	/* MOD_PRCMU_DEBUG[11] */
				true, PRCM_IDX_GPIOCR1, 25,	/* Hx_GPIO[6] */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[6] */
				true, PRCM_IDX_GPIOCR2, 29	/* HW_OBS_APE_PRCMU[6] */
	),
	PRCM_GPIOCR_ALTCX(78,	true, PRCM_IDX_GPIOCR3, 7,	/* MOD_PRCMU_DEBUG[10] */
				true, PRCM_IDX_GPIOCR1, 25,	/* Hx_GPIO[5] */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[5] */
				true, PRCM_IDX_GPIOCR2, 29	/* HW_OBS_APE_PRCMU[5] */
	),
	PRCM_GPIOCR_ALTCX(79,	true, PRCM_IDX_GPIOCR3, 7,	/* MOD_PRCMU_DEBUG[9] */
				true, PRCM_IDX_GPIOCR1, 25,	/* Hx_GPIO[4] */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[4] */
				true, PRCM_IDX_GPIOCR2, 29	/* HW_OBS_APE_PRCMU[4] */
	),
	PRCM_GPIOCR_ALTCX(80,	true, PRCM_IDX_GPIOCR1, 26,	/* MODACC_GPO[0] */
				true, PRCM_IDX_GPIOCR1, 25,	/* Hx_GPIO[3] */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[3] */
				true, PRCM_IDX_GPIOCR2, 30	/* HW_OBS_APE_PRCMU[3] */
	),
	PRCM_GPIOCR_ALTCX(81,	true, PRCM_IDX_GPIOCR2, 17,	/* MODACC_GPO[1] */
				true, PRCM_IDX_GPIOCR1, 25,	/* Hx_GPIO[2] */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[2] */
				true, PRCM_IDX_GPIOCR2, 30	/* HW_OBS_APE_PRCMU[2] */
	),
	PRCM_GPIOCR_ALTCX(82,	true, PRCM_IDX_GPIOCR3, 8,	/* MOD_PRCMU_DEBUG[8] */
				true, PRCM_IDX_GPIOCR1, 25,	/* Hx_GPIO[1] */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[1] */
				true, PRCM_IDX_GPIOCR2, 31	/* HW_OBS_APE_PRCMU[1] */
	),
	PRCM_GPIOCR_ALTCX(83,	true, PRCM_IDX_GPIOCR3, 8,	/* MOD_PRCMU_DEBUG[7] */
				true, PRCM_IDX_GPIOCR1, 25,	/* Hx_GPIO[0] */
				true, PRCM_IDX_GPIOCR1, 2,	/* TPIU_D[0] */
				true, PRCM_IDX_GPIOCR2, 31	/* HW_OBS_APE_PRCMU[0] */
	),
	PRCM_GPIOCR_ALTCX(84,	true, PRCM_IDX_GPIOCR3, 9,	/* MOD_PRCMU_DEBUG[6] */
				true, PRCM_IDX_GPIOCR1, 8,	/* SBAG_CLK_b */
				true, PRCM_IDX_GPIOCR1, 3,	/* TPIU_D[23] */
				true, PRCM_IDX_GPIOCR1, 16	/* MODUART1_RXD_b */
	),
	PRCM_GPIOCR_ALTCX(85,	true, PRCM_IDX_GPIOCR3, 9,	/* MOD_PRCMU_DEBUG[5] */
				true, PRCM_IDX_GPIOCR1, 8,	/* SBAG_D_b[3] */
				true, PRCM_IDX_GPIOCR1, 3,	/* TPIU_D[22] */
				true, PRCM_IDX_GPIOCR1, 16	/* MODUART1_TXD_b */
	),
	PRCM_GPIOCR_ALTCX(86,	true, PRCM_IDX_GPIOCR3, 9,	/* MOD_PRCMU_DEBUG[0] */
				true, PRCM_IDX_GPIOCR2, 18,	/* STMAPE_DAT_b[0] */
				true, PRCM_IDX_GPIOCR1, 14,	/* TPIU_D[25] */
				true, PRCM_IDX_GPIOCR1, 11	/* STMMOD_DAT_c[0] */
	),
	PRCM_GPIOCR_ALTCX(87,	true, PRCM_IDX_GPIOCR3, 0,	/* MODACC_GPO_a[5] */
				true, PRCM_IDX_GPIOCR2, 3,	/* U2_RXD_c */
				true, PRCM_IDX_GPIOCR1, 4,	/* TPIU_D[24] */
				true, PRCM_IDX_GPIOCR1, 21	/* MODUART_STMMUX_RXD_c */
	),
	PRCM_GPIOCR_ALTCX(151,	true, PRCM_IDX_GPIOCR1, 18,	/* REMAP0 */
				false, 0, 0,
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(152,	true, PRCM_IDX_GPIOCR1, 18,	/* REMAP1 */
				false, 0, 0,
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(153,	true, PRCM_IDX_GPIOCR3, 2,	/* KP_O_b[6] */
				true, PRCM_IDX_GPIOCR1, 8,	/* SBAG_D_b[2] */
				true, PRCM_IDX_GPIOCR1, 3,	/* TPIU_D[21] */
				true, PRCM_IDX_GPIOCR1, 0	/* MODUART1_RTS */
	),
	PRCM_GPIOCR_ALTCX(154,	true, PRCM_IDX_GPIOCR3, 2,	/* KP_I_b[6] */
				true, PRCM_IDX_GPIOCR1, 8,	/* SBAG_D_b[1] */
				true, PRCM_IDX_GPIOCR1, 3,	/* TPIU_D[20] */
				true, PRCM_IDX_GPIOCR1, 0	/* MODUART1_CTS */
	),
	PRCM_GPIOCR_ALTCX(155,	true, PRCM_IDX_GPIOCR3, 3,	/* KP_O_b[5] */
				true, PRCM_IDX_GPIOCR1, 8,	/* SBAG_D_b[0] */
				true, PRCM_IDX_GPIOCR1, 3,	/* TPIU_D[19] */
				true, PRCM_IDX_GPIOCR1, 5	/* MODACCUART_RXD_c */
	),
	PRCM_GPIOCR_ALTCX(156,	true, PRCM_IDX_GPIOCR3, 3,	/* KP_O_b[4] */
				true, PRCM_IDX_GPIOCR1, 8,	/* SBAG_VAL_b */
				true, PRCM_IDX_GPIOCR1, 3,	/* TPIU_D[18] */
				true, PRCM_IDX_GPIOCR1, 5	/* MODACCUART_TXD_b */
	),
	PRCM_GPIOCR_ALTCX(157,	true, PRCM_IDX_GPIOCR3, 4,	/* KP_I_b[5] */
				true, PRCM_IDX_GPIOCR1, 23,	/* MODOBS_SERVICE_N */
				true, PRCM_IDX_GPIOCR1, 3,	/* TPIU_D[17] */
				true, PRCM_IDX_GPIOCR1, 14	/* MODACCUART_RTS */
	),
	PRCM_GPIOCR_ALTCX(158,	true, PRCM_IDX_GPIOCR3, 4,	/* KP_I_b[4] */
				true, PRCM_IDX_GPIOCR2, 0,	/* U2_TXD_c */
				true, PRCM_IDX_GPIOCR1, 3,	/* TPIU_D[16] */
				true, PRCM_IDX_GPIOCR1, 14	/* MODACCUART_CTS */
	),
	PRCM_GPIOCR_ALTCX(159,	true, PRCM_IDX_GPIOCR3, 5,	/* KP_O_b[3] */
				true, PRCM_IDX_GPIOCR3, 10,	/* MODUART0_RXD */
				true, PRCM_IDX_GPIOCR1, 4,	/* TPIU_D[31] */
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(160,	true, PRCM_IDX_GPIOCR3, 5,	/* KP_I_b[3] */
				true, PRCM_IDX_GPIOCR3, 10,	/* MODUART0_TXD */
				true, PRCM_IDX_GPIOCR1, 4,	/* TPIU_D[30] */
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(161,	true, PRCM_IDX_GPIOCR3, 9,	/* MOD_PRCMU_DEBUG[4] */
				true, PRCM_IDX_GPIOCR2, 18,	/* STMAPE_CLK_b */
				true, PRCM_IDX_GPIOCR1, 4,	/* TPIU_D[29] */
				true, PRCM_IDX_GPIOCR1, 11	/* STMMOD_CLK_c */
	),
	PRCM_GPIOCR_ALTCX(162,	true, PRCM_IDX_GPIOCR3, 9,	/* MOD_PRCMU_DEBUG[3] */
				true, PRCM_IDX_GPIOCR2, 18,	/* STMAPE_DAT_b[3] */
				true, PRCM_IDX_GPIOCR1, 4,	/* TPIU_D[28] */
				true, PRCM_IDX_GPIOCR1, 11	/* STMMOD_DAT_c[3] */
	),
	PRCM_GPIOCR_ALTCX(163,	true, PRCM_IDX_GPIOCR3, 9,	/* MOD_PRCMU_DEBUG[2] */
				true, PRCM_IDX_GPIOCR2, 18,	/* STMAPE_DAT_b[2] */
				true, PRCM_IDX_GPIOCR1, 4,	/* TPIU_D[27] */
				true, PRCM_IDX_GPIOCR1, 11	/* STMMOD_DAT_c[2] */
	),
	PRCM_GPIOCR_ALTCX(164,	true, PRCM_IDX_GPIOCR3, 9,	/* MOD_PRCMU_DEBUG[1] */
				true, PRCM_IDX_GPIOCR2, 18,	/* STMAPE_DAT_b[1] */
				true, PRCM_IDX_GPIOCR1, 4,	/* TPIU_D[26] */
				true, PRCM_IDX_GPIOCR1, 11	/* STMMOD_DAT_c[1] */
	),
	PRCM_GPIOCR_ALTCX(204,	true, PRCM_IDX_GPIOCR2, 2,	/* U2_RXD_f */
				false, 0, 0,
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(205,	true, PRCM_IDX_GPIOCR2, 2,	/* U2_TXD_f */
				false, 0, 0,
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(206,	true, PRCM_IDX_GPIOCR2, 2,	/* U2_CTSn_b */
				false, 0, 0,
				false, 0, 0,
				false, 0, 0
	),
	PRCM_GPIOCR_ALTCX(207,	true, PRCM_IDX_GPIOCR2, 2,	/* U2_RTSn_b */
				false, 0, 0,
				false, 0, 0,
				false, 0, 0
	),
};

static const u16 db8540_prcm_gpiocr_regs[] = {
	[PRCM_IDX_GPIOCR1] = 0x138,
	[PRCM_IDX_GPIOCR2] = 0x574,
	[PRCM_IDX_GPIOCR3] = 0x2bc,
};

static const struct nmk_pinctrl_soc_data nmk_db8540_soc = {
	.gpio_ranges = nmk_db8540_ranges,
	.gpio_num_ranges = ARRAY_SIZE(nmk_db8540_ranges),
	.pins = nmk_db8540_pins,
	.npins = ARRAY_SIZE(nmk_db8540_pins),
	.functions = nmk_db8540_functions,
	.nfunctions = ARRAY_SIZE(nmk_db8540_functions),
	.groups = nmk_db8540_groups,
	.ngroups = ARRAY_SIZE(nmk_db8540_groups),
	.altcx_pins = db8540_altcx_pins,
	.npins_altcx = ARRAY_SIZE(db8540_altcx_pins),
	.prcm_gpiocr_registers = db8540_prcm_gpiocr_regs,
};

void __devinit
nmk_pinctrl_db8540_init(const struct nmk_pinctrl_soc_data **soc)
{
	*soc = &nmk_db8540_soc;
}
