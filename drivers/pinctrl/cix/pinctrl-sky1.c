// SPDX-License-Identifier: GPL-2.0
//
// Author: Jerry Zhu <Jerry.Zhu@cixtech.com>
// Author: Gary Yang <gary.yang@cixtech.com>

#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>
#include "linux/stddef.h"

#include "../core.h"
#include "pinctrl-sky1.h"

/* Pad names for the s5 domain pinmux subsystem */
static const char * const gpio1_group[] = {"GPIO1"};
static const char * const gpio2_group[] = {"GPIO2"};
static const char * const gpio3_group[] = {"GPIO3"};
static const char * const gpio4_group[] = {"GPIO4"};
static const char * const gpio5_group[] = {"GPIO5"};
static const char * const gpio6_group[] = {"GPIO6"};
static const char * const gpio7_group[] = {"GPIO7"};
static const char * const gpio8_group[] = {"GPIO8"};
static const char * const gpio9_group[] = {"GPIO9"};
static const char * const gpio10_group[] = {"GPIO10"};
static const char * const gpio11_group[] = {"GPIO11"};
static const char * const gpio12_group[] = {"GPIO12"};
static const char * const gpio13_group[] = {"GPIO13"};
static const char * const gpio14_group[] = {"GPIO14"};
static const char * const rsmrst_group[] = { };
static const char * const srst_group[] = { };
static const char * const slp_s3_group[] = { };
static const char * const slp_s5_group[] = { };
static const char * const pwrgd_group[] = { };
static const char * const pwrok_group[] = { };
static const char * const pwrbtn_group[] = { };
static const char * const ddrio_gate_group[] = { };
static const char * const jtag_gpio_group[] = { };
static const char * const jtag_tck_group[] = { };
static const char * const jtag_tdi_group[] = { };
static const char * const jtag_tdo_group[] = { };
static const char * const tms_group[] = { };
static const char * const trsl_group[] = { };
static const char * const sfi_i2c0_scl_group[] = {"SFI_I2C0_SCL",
					"SFI_I3C0_SCL"};
static const char * const sfi_i2c0_sda_group[] = {"SFI_I2C0_SDA",
					"SFI_I3C0_SDA"};
static const char * const sfi_i2c1_scl_group[] = {"SFI_I2C1_SCL",
					"SFI_I3C1_SCL", "SFI_SPI_CS0"};
static const char * const sfi_i2c1_sda_group[] = {"SFI_I2C1_SDA",
					"SFI_I3C1_SDA", "SFI_SPI_CS1"};
static const char * const sfi_gpio0_group[] = {"GPIO15", "SFI_SPI_SCK",
					"SFI_GPIO0"};
static const char * const sfi_gpio1_group[] = {"GPIO16", "SFI_SPI_MOSI",
					"SFI_GPIO1"};
static const char * const sfi_gpio2_group[] = {"GPIO17", "SFI_SPI_MISO",
					"SFI_GPIO2"};
static const char * const gpio18_group[] = {"SFI_GPIO3", "GPIO18"};
static const char * const gpio19_group[] = {"SFI_GPIO4", "GPIO19"};
static const char * const gpio20_group[] = {"SFI_GPIO5", "GPIO20"};
static const char * const gpio21_group[] = {"SFI_GPIO6", "GPIO21"};
static const char * const gpio22_group[] = {"SFI_GPIO7", "GPIO22"};
static const char * const gpio23_group[] = {"SFI_GPIO8", "GPIO23",
				"SFI_I3C0_PUR_EN_L"};
static const char * const gpio24_group[] = {"SFI_GPIO9", "GPIO24",
				"SFI_I3C1_PUR_EN_L"};
static const char * const spi1_miso_group[] = {"SPI1_MISO", "GPIO25"};
static const char * const spi1_cs0_group[] = {"SPI1_CS0", "GPIO26"};
static const char * const spi1_cs1_group[] = {"SPI1_CS1", "GPIO27"};
static const char * const spi1_mosi_group[] = {"SPI1_MOSI", "GPIO28"};
static const char * const spi1_clk_group[] = {"SPI1_CLK", "GPIO29"};
static const char * const gpio30_group[] = {"GPIO30", "USB_0C0_L"};
static const char * const gpio31_group[] = {"GPIO31", "USB_0C1_L"};
static const char * const gpio32_group[] = {"GPIO32", "USB_0C2_L"};
static const char * const gpio33_group[] = {"GPIO33", "USB_0C3_L"};
static const char * const gpio34_group[] = {"GPIO34", "USB_0C4_L"};
static const char * const gpio35_group[] = {"GPIO35", "USB_0C5_L"};
static const char * const gpio36_group[] = {"GPIO36", "USB_0C6_L"};
static const char * const gpio37_group[] = {"GPIO37", "USB_0C7_L"};
static const char * const gpio38_group[] = {"GPIO38", "USB_0C8_L"};
static const char * const gpio39_group[] = {"GPIO39", "USB_0C9_L"};
static const char * const gpio40_group[] = {"GPIO40", "USB_DRIVE_VBUS0"};
static const char * const gpio41_group[] = {"GPIO41", "USB_DRIVE_VBUS4"};
static const char * const gpio42_group[] = {"GPIO42", "USB_DRIVE_VBUS5"};
static const char * const se_qspi_clk_group[] = {"SE_QSPI_CLK", "QSPI_CLK"};
static const char * const se_qspi_cs_group[] = {"SE_QSPI_CS_L", "QSPI_CS_L"};
static const char * const se_qspi_data0_group[] = {"SE_QSPI_DATA0",
					"QSPI_DATA0"};
static const char * const se_qspi_data1_group[] = {"SE_QSPI_DATA1",
					"QSPI_DATA1"};
static const char * const se_qspi_data2_group[] = {"SE_QSPI_DATA2",
					"QSPI_DATA2"};
static const char * const se_qspi_data3_group[] = {"SE_QSPI_DATA3",
					"QSPI_DATA3"};
static const struct sky1_pin_desc sky1_pinctrl_s5_pads[] = {
	SKY_PINFUNCTION(PINCTRL_PIN(0, "GPIO1"), gpio1),
	SKY_PINFUNCTION(PINCTRL_PIN(1, "GPIO2"), gpio2),
	SKY_PINFUNCTION(PINCTRL_PIN(2, "GPIO3"), gpio3),
	SKY_PINFUNCTION(PINCTRL_PIN(3, "GPIO4"), gpio4),
	SKY_PINFUNCTION(PINCTRL_PIN(4, "GPIO5"), gpio5),
	SKY_PINFUNCTION(PINCTRL_PIN(5, "GPIO6"), gpio6),
	SKY_PINFUNCTION(PINCTRL_PIN(6, "GPIO7"), gpio7),
	SKY_PINFUNCTION(PINCTRL_PIN(7, "GPIO8"), gpio8),
	SKY_PINFUNCTION(PINCTRL_PIN(8, "GPIO9"), gpio9),
	SKY_PINFUNCTION(PINCTRL_PIN(9, "GPIO10"), gpio10),
	SKY_PINFUNCTION(PINCTRL_PIN(10, "GPIO11"), gpio11),
	SKY_PINFUNCTION(PINCTRL_PIN(11, "GPIO12"), gpio12),
	SKY_PINFUNCTION(PINCTRL_PIN(12, "GPIO13"), gpio13),
	SKY_PINFUNCTION(PINCTRL_PIN(13, "GPIO14"), gpio14),
	SKY_PINFUNCTION(PINCTRL_PIN(14, "RSMRST_L"), rsmrst),
	SKY_PINFUNCTION(PINCTRL_PIN(15, "SRST_L"), srst),
	SKY_PINFUNCTION(PINCTRL_PIN(16, "SLP_S3_L"), slp_s3),
	SKY_PINFUNCTION(PINCTRL_PIN(17, "SLP_S5_L"), slp_s5),
	SKY_PINFUNCTION(PINCTRL_PIN(18, "PWRGD"), pwrgd),
	SKY_PINFUNCTION(PINCTRL_PIN(19, "PWROK"), pwrok),
	SKY_PINFUNCTION(PINCTRL_PIN(20, "PWRBTN_L"), pwrbtn),
	SKY_PINFUNCTION(PINCTRL_PIN(21, "VDD_DDRIO_GATE"), ddrio_gate),
	SKY_PINFUNCTION(PINCTRL_PIN(22, "JTAG_GPIO_L"), jtag_gpio),
	SKY_PINFUNCTION(PINCTRL_PIN(23, "JTAG_TCK"), jtag_tck),
	SKY_PINFUNCTION(PINCTRL_PIN(24, "JTAG_TDI"), jtag_tdi),
	SKY_PINFUNCTION(PINCTRL_PIN(25, "JTAG_TDO"), jtag_tdo),
	SKY_PINFUNCTION(PINCTRL_PIN(26, "TMS"), tms),
	SKY_PINFUNCTION(PINCTRL_PIN(27, "TRSL_L"), trsl),
	SKY_PINFUNCTION(PINCTRL_PIN(28, "SFI_I2C0_SCL"), sfi_i2c0_scl),
	SKY_PINFUNCTION(PINCTRL_PIN(29, "SFI_I2C0_SDA"), sfi_i2c0_sda),
	SKY_PINFUNCTION(PINCTRL_PIN(30, "SFI_I2C1_SCL"), sfi_i2c1_scl),
	SKY_PINFUNCTION(PINCTRL_PIN(31, "SFI_I2C1_SDA"), sfi_i2c1_sda),
	SKY_PINFUNCTION(PINCTRL_PIN(32, "SFI_GPIO0"), sfi_gpio0),
	SKY_PINFUNCTION(PINCTRL_PIN(33, "SFI_GPIO1"), sfi_gpio1),
	SKY_PINFUNCTION(PINCTRL_PIN(34, "SFI_GPIO2"), sfi_gpio2),
	SKY_PINFUNCTION(PINCTRL_PIN(35, "GPIO18"), gpio18),
	SKY_PINFUNCTION(PINCTRL_PIN(36, "GPIO19"), gpio19),
	SKY_PINFUNCTION(PINCTRL_PIN(37, "GPIO20"), gpio20),
	SKY_PINFUNCTION(PINCTRL_PIN(38, "GPIO21"), gpio21),
	SKY_PINFUNCTION(PINCTRL_PIN(39, "GPIO22"), gpio22),
	SKY_PINFUNCTION(PINCTRL_PIN(40, "GPIO23"), gpio23),
	SKY_PINFUNCTION(PINCTRL_PIN(41, "GPIO24"), gpio24),
	SKY_PINFUNCTION(PINCTRL_PIN(42, "SPI1_MISO"), spi1_miso),
	SKY_PINFUNCTION(PINCTRL_PIN(43, "SPI1_CS0"), spi1_cs0),
	SKY_PINFUNCTION(PINCTRL_PIN(44, "SPI1_CS1"), spi1_cs1),
	SKY_PINFUNCTION(PINCTRL_PIN(45, "SPI1_MOSI"), spi1_mosi),
	SKY_PINFUNCTION(PINCTRL_PIN(46, "SPI1_CLK"), spi1_clk),
	SKY_PINFUNCTION(PINCTRL_PIN(47, "GPIO30"), gpio30),
	SKY_PINFUNCTION(PINCTRL_PIN(48, "GPIO31"), gpio31),
	SKY_PINFUNCTION(PINCTRL_PIN(49, "GPIO32"), gpio32),
	SKY_PINFUNCTION(PINCTRL_PIN(50, "GPIO33"), gpio33),
	SKY_PINFUNCTION(PINCTRL_PIN(51, "GPIO34"), gpio34),
	SKY_PINFUNCTION(PINCTRL_PIN(52, "GPIO35"), gpio35),
	SKY_PINFUNCTION(PINCTRL_PIN(53, "GPIO36"), gpio36),
	SKY_PINFUNCTION(PINCTRL_PIN(54, "GPIO37"), gpio37),
	SKY_PINFUNCTION(PINCTRL_PIN(55, "GPIO38"), gpio38),
	SKY_PINFUNCTION(PINCTRL_PIN(56, "GPIO39"), gpio39),
	SKY_PINFUNCTION(PINCTRL_PIN(57, "GPIO40"), gpio40),
	SKY_PINFUNCTION(PINCTRL_PIN(58, "GPIO41"), gpio41),
	SKY_PINFUNCTION(PINCTRL_PIN(59, "GPIO42"), gpio42),
	SKY_PINFUNCTION(PINCTRL_PIN(60, "SE_QSPI_CLK"), se_qspi_clk),
	SKY_PINFUNCTION(PINCTRL_PIN(61, "SE_QSPI_CS_L"), se_qspi_cs),
	SKY_PINFUNCTION(PINCTRL_PIN(62, "SE_QSPI_DATA0"), se_qspi_data0),
	SKY_PINFUNCTION(PINCTRL_PIN(63, "SE_QSPI_DATA1"), se_qspi_data1),
	SKY_PINFUNCTION(PINCTRL_PIN(64, "SE_QSPI_DATA2"), se_qspi_data2),
	SKY_PINFUNCTION(PINCTRL_PIN(65, "SE_QSPI_DATA3"), se_qspi_data3),
};

/* Pad names for the s0 domain pinmux subsystem */
static const char * const gpio43_group[] = {"GPIO43"};
static const char * const gpio44_group[] = {"GPIO44"};
static const char * const gpio45_group[] = {"GPIO45"};
static const char * const gpio46_group[] = {"GPIO46"};
static const char * const reset_in_group[] = { };
static const char * const plt_reset_group[] = { };
static const char * const thermtrip_group[] = { };
static const char * const prochot_group[] = { };
static const char * const pm_i2c0_clk_group[] = { };
static const char * const pm_i2c0_data_group[] = { };
static const char * const pm_i2c1_clk_group[] = { };
static const char * const pm_i2c1_data_group[] = { };
static const char * const pm_i2c2_clk_group[] = { };
static const char * const pm_i2c2_data_group[] = { };
static const char * const pm_i2c3_clk_group[] = { };
static const char * const pm_i2c3_data_group[] = { };
static const char * const strap0_group[] = { };
static const char * const strap1_group[] = { };
static const char * const dp2_digon_group[] = {"DP2_DIGON"};
static const char * const dp2_blon_group[] = {"DP2_BLON"};
static const char * const dp2_vary_bl_group[] = {"DP2_VARY_BL"};
static const char * const i2c7_scl_group[] = {"I2C7_SCL"};
static const char * const i2c7_sda_group[] = {"I2C7_SDA"};
static const char * const uart6_csu_se_txd_group[] = { };
static const char * const clk_req1_group[] = { };
static const char * const clk_req3_group[] = { };
static const char * const i2c5_scl_group[] = {"I2C5_SCL", "GPIO47"};
static const char * const i2c5_sda_group[] = {"I2C5_SDA", "GPIO48"};
static const char * const i2c6_scl_group[] = {"I2C6_SCL", "GPIO49"};
static const char * const i2c6_sda_group[] = {"I2C6_SDA", "GPIO50"};
static const char * const i2c0_scl_group[] = {"I2C0_SCL", "GPIO51"};
static const char * const i2c0_sda_group[] = {"I2C0_SDA", "GPIO52"};
static const char * const i2c1_scl_group[] = {"I2C1_SCL", "GPIO53"};
static const char * const i2c1_sda_group[] = {"I2C1_SDA", "GPIO54"};
static const char * const i2c2_scl_group[] = {"I2C2_SCL", "I3C0_SCL",
					"GPIO55"};
static const char * const i2c2_sda_group[] = {"I2C2_SDA", "I3C0_SDA",
					"GPIO56"};
static const char * const gpio57_group[] = {"GPIO57", "I3C0_PUR_EN_L"};
static const char * const i2c3_scl_group[] = {"I2C3_SCL", "I3C1_SCL",
					"GPIO58"};
static const char * const i2c3_sda_group[] = {"I2C3_SDA", "I3C1_SDA",
					"GPIO59"};
static const char * const gpio60_group[] = {"GPIO60", "I3C1_PUR_EN_L"};
static const char * const i2c4_scl_group[] = {"I2C4_SCL", "GPIO61"};
static const char * const i2c4_sda_group[] = {"I2C4_SDA", "GPIO62"};
static const char * const hda_bitclk_group[] = {"HDA_BITCLK", "I2S0_SCK",
				"I2S9_RSCK_DBG"};
static const char * const hda_rst_group[] = {"HDA_RST_L", "I2S0_DATA_IN",
				"I2S9_DATA_IN_DBG"};
static const char * const hda_sdin0_group[] = {"HDA_SDIN0", "I2S0_MCLK",
				"I2S9_TSCK_DBG"};
static const char * const hda_sdout0_group[] = {"HDA_SDOUT0", "I2S0_DATA_OUT",
				"I2S9_TWS_DBG"};
static const char * const hda_sync_group[] = {"HDA_SYNC", "I2S0_WS",
				"I2S9_RWS_DBG"};
static const char * const hda_sdin1_group[] = {"HDA_SDIN1", "GPIO63",
				"I2S9_DATA_IN1_DBG"};
static const char * const hda_sdout1_group[] = {"HDA_SDOUT1", "GPIO64",
				"I2S9_DATA_OUT0_DBG"};
static const char * const i2s1_mclk_group[] = {"I2S1_MCLK", "GPIO65"};
static const char * const i2s1_sck_group[] = {"I2S1_SCK", "GPIO66"};
static const char * const i2s1_ws_group[] = {"I2S1_WS", "GPIO67"};
static const char * const i2s1_data_in_group[] = {"I2S1_DATA_IN", "GPIO68"};
static const char * const i2s1_data_out_group[] = {"I2S1_DATA_OUT", "GPIO69"};
static const char * const i2s2_mck_group[] = {"I2S2_MCLK", "GPIO70"};
static const char * const i2s2_rsck_group[] = {"I2S2_RSCK", "GPIO71",
				"I2S5_RSCK_DBG", "I2S6_RSCK_DBG"};
static const char * const i2s2_rws_group[] = {"I2S2_RWS", "GPIO72",
				"I2S5_RWS_DBG", "I2S6_RWS_DBG"};
static const char * const i2s2_tsck_group[] = {"I2S2_TSCK", "GPIO73",
				"I2S5_TSCK_DBG", "I2S6_TSCK_DBG"};
static const char * const i2s2_tws_group[] = {"I2S2_TWS", "GPIO74",
				"I2S5_TWS_DBG", "I2S6_TWS_DBG"};
static const char * const i2s2_data_in0_group[] = {"I2S2_DATA_IN0", "GPIO75",
				"I2S5_DATA_IN0_DBG", "I2S6_DATA_IN0_DBG"};
static const char * const i2s2_data_in1_group[] = {"I2S2_DATA_IN1", "GPIO76",
				"I2S5_DATA_IN1_DBG", "I2S6_DATA_IN1_DBG"};
static const char * const i2s2_data_out0_group[] = {"I2S2_DATA_OUT0", "GPIO77",
				"I2S5_DATA_OUT0_DBG", "I2S6_DATA_OUT0_DBG"};
static const char * const i2s2_data_out1_group[] = {"I2S2_DATA_OUT1", "GPIO78",
				"I2S5_DATA_OUT1_DBG", "I2S6_DATA_OUT1_DBG"};
static const char * const i2s2_data_out2_group[] = {"I2S2_DATA_OUT2",
				"GPIO79"};
static const char * const i2s2_data_out3_group[] = {"I2S2_DATA_OUT3", "GPIO80",
				"I2S9_DATA_OUT1_DBG"};
static const char * const i2s3_mclk_group[] = {"I2S3_MCLK", "GPIO81"};
static const char * const i2s3_rsck_group[] = {"I2S3_RSCK", "GPIO82",
				"I2S7_RSCK_DBG", "I2S8_RSCK_DBG"};
static const char * const i2s3_rws_group[] = {"I2S3_RWS", "GPIO83",
				"I2S7_RWS_DBG", "I2S8_RWS_DBG"};
static const char * const i2s3_tsck_group[] = {"I2S3_TSCK", "GPIO84",
				"I2S7_TSCK_DBG", "I2S8_TSCK_DBG"};
static const char * const i2s3_tws_group[] = {"I2S3_TWS", "GPIO85",
				"I2S7_TWS_DBG", "I2S8_TWS_DBG"};
static const char * const i2s3_data_in0_group[] = {"I2S3_DATA_IN0", "GPIO86",
				"I2S7_DATA_IN0_DBG", "I2S8_DATA_IN0_DBG"};
static const char * const i2s3_data_in1_group[] = {"I2S3_DATA_IN1", "GPIO87",
				"I2S7_DATA_IN1_DBG", "I2S8_DATA_IN1_DBG"};
static const char * const i2s3_data_out0_group[] = {"I2S3_DATA_OUT0", "GPIO88",
				"I2S7_DATA_OUT0_DBG", "I2S8_DATA_OUT0_DBG"};
static const char * const i2s3_data_out1_group[] = {"I2S3_DATA_OUT1", "GPIO89",
				"I2S7_DATA_OUT1_DBG", "I2S8_DATA_OUT1_DBG"};
static const char * const gpio90_group[] = {"GPIO90", "I2S4_MCLK_LB"};
static const char * const gpio91_group[] = {"GPIO91", "I2S4_SCK_LB"};
static const char * const gpio92_group[] = {"GPIO92", "I2S4_WS_LB"};
static const char * const gpio93_group[] = {"GPIO93", "I2S4_DATA_IN_LB"};
static const char * const gpio94_group[] = {"GPIO94", "I2S4_DATA_OUT_LB"};
static const char * const uart0_txd_group[] = {"UART0_TXD", "PWM0", "GPIO95"};
static const char * const uart0_rxd_group[] = {"UART0_RXD", "PWM1", "GPIO96"};
static const char * const uart0_cts_group[] = {"UART0_CTS", "FAN_OUT2",
				"GPIO97"};
static const char * const uart0_rts_group[] = {"UART0_RTS", "FAN_TACH2",
				"GPIO98"};
static const char * const uart1_txd_group[] = {"UART1_TXD", "FAN_OUT0",
				"GPIO99"};
static const char * const uart1_rxd_group[] = {"UART1_RXD", "FAN_TACH0",
				"GPIO100"};
static const char * const uart1_cts_group[] = {"UART1_CTS", "FAN_OUT1",
				"GPIO101"};
static const char * const uart1_rts_group[] = {"UART1_RTS", "FAN_TACH1",
				"GPIO102"};
static const char * const uart2_txd_group[] = {"UART2_TXD", "GPIO103"};
static const char * const uart2_rxd_group[] = {"UART2_RXD", "GPIO104"};
static const char * const uart3_txd_group[] = {"UART3_TXD", "GPIO105"};
static const char * const uart3_rxd_group[] = {"UART3_RXD", "GPIO106"};
static const char * const uart3_cts_group[] = {"UART3_CTS", "GPIO107",
				"TRIGIN0"};
static const char * const uart3_rts_group[] = {"UART3_RTS", "GPIO108",
				"TRIGIN1"};
static const char * const uart4_csu_pm_txd_group[] = {"UART4_CSU_PM_TXD",
				"GPIO109"};
static const char * const uart4_csu_pm_rxd_group[] = {"UART4_CSU_PM_RXD",
				"GPIO110"};
static const char * const uart5_csu_se_txd_group[] = {"UART5_CSU_SE_TXD",
				"GPIO111"};
static const char * const uart5_csu_se_rxd_group[] = {"UART5_CSU_SE_RXD",
				"GPIO112"};
static const char * const uart6_csu_se_rxd_group[] = {"UART6_CSU_SE_RXD",
				"GPIO113"};
static const char * const clk_req0_group[] = {"CLK_REQ0_L", "GPIO114"};
static const char * const clk_req2_group[] = {"CLK_REQ2_L", "GPIO115"};
static const char * const clk_req4_group[] = {"CLK_REQ4_L", "GPIO116"};
static const char * const csi0_mclk0_group[] = {"CSI0_MCLK0", "GPIO117"};
static const char * const csi0_mclk1_group[] = {"CSI0_MCLK1", "GPIO118"};
static const char * const csi1_mclk0_group[] = {"CSI1_MCLK0", "GPIO119"};
static const char * const csi1_mclk1_group[] = {"CSI1_MCLK1", "GPIO120"};
static const char * const gpio121_group[] = {"GPIO121", "GMAC0_REFCLK_25M"};
static const char * const gpio122_group[] = {"GPIO122", "GMAC0_TX_CTL"};
static const char * const gpio123_group[] = {"GPIO123", "GMAC0_TXD0"};
static const char * const gpio124_group[] = {"GPIO124", "GMAC0_TXD1"};
static const char * const gpio125_group[] = {"GPIO125", "GMAC0_TXD2"};
static const char * const gpio126_group[] = {"GPIO126", "GMAC0_TXD3"};
static const char * const gpio127_group[] = {"GPIO127", "GMAC0_TX_CLK"};
static const char * const gpio128_group[] = {"GPIO128", "GMAC0_RX_CTL"};
static const char * const gpio129_group[] = {"GPIO129", "GMAC0_RXD0"};
static const char * const gpio130_group[] = {"GPIO130", "GMAC0_RXD1"};
static const char * const gpio131_group[] = {"GPIO131", "GMAC0_RXD2"};
static const char * const gpio132_group[] = {"GPIO132", "GMAC0_RXD3"};
static const char * const gpio133_group[] = {"GPIO133", "GMAC0_RX_CLK"};
static const char * const gpio134_group[] = {"GPIO134", "GMAC0_MDC"};
static const char * const gpio135_group[] = {"GPIO135", "GMAC0_MDIO"};
static const char * const gpio136_group[] = {"GPIO136", "GMAC1_REFCLK_25M"};
static const char * const gpio137_group[] = {"GPIO137", "GMAC1_TX_CTL"};
static const char * const gpio138_group[] = {"GPIO138", "GMAC1_TXD0",
				"SPI2_MISO"};
static const char * const gpio139_group[] = {"GPIO139", "GMAC1_TXD1",
				"SPI2_CS0"};
static const char * const gpio140_group[] = {"GPIO140", "GMAC1_TXD2",
				"SPI2_CS1"};
static const char * const gpio141_group[] = {"GPIO141", "GMAC1_TXD3",
				"SPI2_MOSI"};
static const char * const gpio142_group[] = {"GPIO142", "GMAC1_TX_CLK",
				"SPI2_CLK"};
static const char * const gpio143_group[] = {"GPIO143", "GMAC1_RX_CTL"};
static const char * const gpio144_group[] = {"GPIO144", "GMAC1_RXD0"};
static const char * const gpio145_group[] = {"GPIO145", "GMAC1_RXD1"};
static const char * const gpio146_group[] = {"GPIO146", "GMAC1_RXD2"};
static const char * const gpio147_group[] = {"GPIO147", "GMAC1_RXD3"};
static const char * const gpio148_group[] = {"GPIO148", "GMAC1_RX_CLK"};
static const char * const gpio149_group[] = {"GPIO149", "GMAC1_MDC"};
static const char * const gpio150_group[] = {"GPIO150", "GMAC1_MDIO"};
static const char * const gpio151_group[] = {"GPIO151", "PM_GPIO0"};
static const char * const gpio152_group[] = {"GPIO152", "PM_GPIO1"};
static const char * const gpio153_group[] = {"GPIO153", "PM_GPIO2"};
static const struct sky1_pin_desc sky1_pinctrl_pads[] = {
	SKY_PINFUNCTION(PINCTRL_PIN(0, "GPIO43"), gpio43),
	SKY_PINFUNCTION(PINCTRL_PIN(1, "GPIO44"), gpio44),
	SKY_PINFUNCTION(PINCTRL_PIN(2, "GPIO45"), gpio45),
	SKY_PINFUNCTION(PINCTRL_PIN(3, "GPIO46"), gpio46),
	SKY_PINFUNCTION(PINCTRL_PIN(4, "RESET_IN_L"), reset_in),
	SKY_PINFUNCTION(PINCTRL_PIN(5, "PLT_RESET_L"), plt_reset),
	SKY_PINFUNCTION(PINCTRL_PIN(6, "THERMTRIP_L"), thermtrip),
	SKY_PINFUNCTION(PINCTRL_PIN(7, "PROCHOT_L"), prochot),
	SKY_PINFUNCTION(PINCTRL_PIN(8, "PM_I2C0_CLK"), pm_i2c0_clk),
	SKY_PINFUNCTION(PINCTRL_PIN(9, "PM_I2C0_DATA"), pm_i2c0_data),
	SKY_PINFUNCTION(PINCTRL_PIN(10, "PM_I2C1_CLK"), pm_i2c1_clk),
	SKY_PINFUNCTION(PINCTRL_PIN(11, "PM_I2C1_DATA"), pm_i2c1_data),
	SKY_PINFUNCTION(PINCTRL_PIN(12, "PM_I2C2_CLK"), pm_i2c2_clk),
	SKY_PINFUNCTION(PINCTRL_PIN(13, "PM_I2C2_DATA"), pm_i2c2_data),
	SKY_PINFUNCTION(PINCTRL_PIN(14, "PM_I2C3_CLK"), pm_i2c3_clk),
	SKY_PINFUNCTION(PINCTRL_PIN(15, "PM_I2C3_DATA"), pm_i2c3_data),
	SKY_PINFUNCTION(PINCTRL_PIN(16, "STRAP0"), strap0),
	SKY_PINFUNCTION(PINCTRL_PIN(17, "STRAP1"), strap1),
	SKY_PINFUNCTION(PINCTRL_PIN(18, "DP2_DIGON"), dp2_digon),
	SKY_PINFUNCTION(PINCTRL_PIN(19, "DP2_BLON"), dp2_blon),
	SKY_PINFUNCTION(PINCTRL_PIN(20, "DP2_VARY_BL"), dp2_vary_bl),
	SKY_PINFUNCTION(PINCTRL_PIN(21, "I2C7_SCL"), i2c7_scl),
	SKY_PINFUNCTION(PINCTRL_PIN(22, "I2C7_SDA"), i2c7_sda),
	SKY_PINFUNCTION(PINCTRL_PIN(23, "UART6_CSU_SE_TXD"), uart6_csu_se_txd),
	SKY_PINFUNCTION(PINCTRL_PIN(24, "CLK_REQ1_L"), clk_req1),
	SKY_PINFUNCTION(PINCTRL_PIN(25, "CLK_REQ3_L"), clk_req3),
	SKY_PINFUNCTION(PINCTRL_PIN(26, "I2C5_SCL"), i2c5_scl),
	SKY_PINFUNCTION(PINCTRL_PIN(27, "I2C5_SDA"), i2c5_sda),
	SKY_PINFUNCTION(PINCTRL_PIN(28, "I2C6_SCL"), i2c6_scl),
	SKY_PINFUNCTION(PINCTRL_PIN(29, "I2C6_SDA"), i2c6_sda),
	SKY_PINFUNCTION(PINCTRL_PIN(30, "I2C0_CLK"), i2c0_scl),
	SKY_PINFUNCTION(PINCTRL_PIN(31, "I2C0_SDA"), i2c0_sda),
	SKY_PINFUNCTION(PINCTRL_PIN(32, "I2C1_CLK"), i2c1_scl),
	SKY_PINFUNCTION(PINCTRL_PIN(33, "I2C1_SDA"), i2c1_sda),
	SKY_PINFUNCTION(PINCTRL_PIN(34, "I2C2_SCL"), i2c2_scl),
	SKY_PINFUNCTION(PINCTRL_PIN(35, "I2C2_SDA"), i2c2_sda),
	SKY_PINFUNCTION(PINCTRL_PIN(36, "GPIO57"), gpio57),
	SKY_PINFUNCTION(PINCTRL_PIN(37, "I2C3_SCL"), i2c3_scl),
	SKY_PINFUNCTION(PINCTRL_PIN(38, "I2C3_SDA"), i2c3_sda),
	SKY_PINFUNCTION(PINCTRL_PIN(39, "GPIO60"), gpio60),
	SKY_PINFUNCTION(PINCTRL_PIN(40, "I2C4_SCL"), i2c4_scl),
	SKY_PINFUNCTION(PINCTRL_PIN(41, "I2C4_SDA"), i2c4_sda),
	SKY_PINFUNCTION(PINCTRL_PIN(42, "HDA_BITCLK"), hda_bitclk),
	SKY_PINFUNCTION(PINCTRL_PIN(43, "HDA_RST_L"), hda_rst),
	SKY_PINFUNCTION(PINCTRL_PIN(44, "HDA_SDIN0"), hda_sdin0),
	SKY_PINFUNCTION(PINCTRL_PIN(45, "HDA_SDOUT0"), hda_sdout0),
	SKY_PINFUNCTION(PINCTRL_PIN(46, "HDA_SYNC"), hda_sync),
	SKY_PINFUNCTION(PINCTRL_PIN(47, "HDA_SDIN1"), hda_sdin1),
	SKY_PINFUNCTION(PINCTRL_PIN(48, "HDA_SDOUT1"), hda_sdout1),
	SKY_PINFUNCTION(PINCTRL_PIN(49, "I2S1_MCLK"), i2s1_mclk),
	SKY_PINFUNCTION(PINCTRL_PIN(50, "I2S1_SCK"), i2s1_sck),
	SKY_PINFUNCTION(PINCTRL_PIN(51, "I2S1_WS"), i2s1_ws),
	SKY_PINFUNCTION(PINCTRL_PIN(52, "I2S1_DATA_IN"), i2s1_data_in),
	SKY_PINFUNCTION(PINCTRL_PIN(53, "I2S1_DATA_OUT"), i2s1_data_out),
	SKY_PINFUNCTION(PINCTRL_PIN(54, "I2S2_MCLK"), i2s2_mck),
	SKY_PINFUNCTION(PINCTRL_PIN(55, "I2S2_RSCK"), i2s2_rsck),
	SKY_PINFUNCTION(PINCTRL_PIN(56, "I2S2_RWS"), i2s2_rws),
	SKY_PINFUNCTION(PINCTRL_PIN(57, "I2S2_TSCK"), i2s2_tsck),
	SKY_PINFUNCTION(PINCTRL_PIN(58, "I2S2_TWS"), i2s2_tws),
	SKY_PINFUNCTION(PINCTRL_PIN(59, "I2S2_DATA_IN0"), i2s2_data_in0),
	SKY_PINFUNCTION(PINCTRL_PIN(60, "I2S2_DATA_IN1"), i2s2_data_in1),
	SKY_PINFUNCTION(PINCTRL_PIN(61, "I2S2_DATA_OUT0"), i2s2_data_out0),
	SKY_PINFUNCTION(PINCTRL_PIN(62, "I2S2_DATA_OUT1"), i2s2_data_out1),
	SKY_PINFUNCTION(PINCTRL_PIN(63, "I2S2_DATA_OUT2"), i2s2_data_out2),
	SKY_PINFUNCTION(PINCTRL_PIN(64, "I2S2_DATA_OUT3"), i2s2_data_out3),
	SKY_PINFUNCTION(PINCTRL_PIN(65, "I2S3_MCLK"), i2s3_mclk),
	SKY_PINFUNCTION(PINCTRL_PIN(66, "I2S3_RSCK"), i2s3_rsck),
	SKY_PINFUNCTION(PINCTRL_PIN(67, "I2S3_RWS"), i2s3_rws),
	SKY_PINFUNCTION(PINCTRL_PIN(68, "I2S3_TSCK"), i2s3_tsck),
	SKY_PINFUNCTION(PINCTRL_PIN(69, "I2S3_TWS"), i2s3_tws),
	SKY_PINFUNCTION(PINCTRL_PIN(70, "I2S3_DATA_IN0"), i2s3_data_in0),
	SKY_PINFUNCTION(PINCTRL_PIN(71, "I2S3_DATA_IN1"), i2s3_data_in1),
	SKY_PINFUNCTION(PINCTRL_PIN(72, "I2S3_DATA_OUT0"), i2s3_data_out0),
	SKY_PINFUNCTION(PINCTRL_PIN(73, "I2S3_DATA_OUT1"), i2s3_data_out1),
	SKY_PINFUNCTION(PINCTRL_PIN(74, "GPIO90"), gpio90),
	SKY_PINFUNCTION(PINCTRL_PIN(75, "GPIO91"), gpio91),
	SKY_PINFUNCTION(PINCTRL_PIN(76, "GPIO92"), gpio92),
	SKY_PINFUNCTION(PINCTRL_PIN(77, "GPIO93"), gpio93),
	SKY_PINFUNCTION(PINCTRL_PIN(78, "GPIO94"), gpio94),
	SKY_PINFUNCTION(PINCTRL_PIN(79, "UART0_TXD"), uart0_txd),
	SKY_PINFUNCTION(PINCTRL_PIN(80, "UART0_RXD"), uart0_rxd),
	SKY_PINFUNCTION(PINCTRL_PIN(81, "UART0_CTS"), uart0_cts),
	SKY_PINFUNCTION(PINCTRL_PIN(82, "UART0_RTS"), uart0_rts),
	SKY_PINFUNCTION(PINCTRL_PIN(83, "UART1_TXD"), uart1_txd),
	SKY_PINFUNCTION(PINCTRL_PIN(84, "UART1_RXD"), uart1_rxd),
	SKY_PINFUNCTION(PINCTRL_PIN(85, "UART1_CTS"), uart1_cts),
	SKY_PINFUNCTION(PINCTRL_PIN(86, "UART1_RTS"), uart1_rts),
	SKY_PINFUNCTION(PINCTRL_PIN(87, "UART2_TXD"), uart2_txd),
	SKY_PINFUNCTION(PINCTRL_PIN(88, "UART2_RXD"), uart2_rxd),
	SKY_PINFUNCTION(PINCTRL_PIN(89, "UART3_TXD"), uart3_txd),
	SKY_PINFUNCTION(PINCTRL_PIN(90, "UART3_RXD"), uart3_rxd),
	SKY_PINFUNCTION(PINCTRL_PIN(91, "UART3_CTS"), uart3_cts),
	SKY_PINFUNCTION(PINCTRL_PIN(92, "UART3_RTS"), uart3_rts),
	SKY_PINFUNCTION(PINCTRL_PIN(93, "UART4_CSU_PM_TXD"), uart4_csu_pm_txd),
	SKY_PINFUNCTION(PINCTRL_PIN(94, "UART4_CSU_PM_RXD"), uart4_csu_pm_rxd),
	SKY_PINFUNCTION(PINCTRL_PIN(95, "UART5_CSU_SE_TXD"), uart5_csu_se_txd),
	SKY_PINFUNCTION(PINCTRL_PIN(96, "UART5_CSU_SE_RXD"), uart5_csu_se_rxd),
	SKY_PINFUNCTION(PINCTRL_PIN(97, "UART6_CSU_SE_RXD"), uart6_csu_se_rxd),
	SKY_PINFUNCTION(PINCTRL_PIN(98, "CLK_REQ0_L"), clk_req0),
	SKY_PINFUNCTION(PINCTRL_PIN(99, "CLK_REQ2_L"), clk_req2),
	SKY_PINFUNCTION(PINCTRL_PIN(100, "CLK_REQ4_L"), clk_req4),
	SKY_PINFUNCTION(PINCTRL_PIN(101, "CSI0_MCLK0"), csi0_mclk0),
	SKY_PINFUNCTION(PINCTRL_PIN(102, "CSI0_MCLK1"), csi0_mclk1),
	SKY_PINFUNCTION(PINCTRL_PIN(103, "CSI1_MCLK0"), csi1_mclk0),
	SKY_PINFUNCTION(PINCTRL_PIN(104, "CSI1_MCLK1"), csi1_mclk1),
	SKY_PINFUNCTION(PINCTRL_PIN(105, "GPIO121"), gpio121),
	SKY_PINFUNCTION(PINCTRL_PIN(106, "GPIO122"), gpio122),
	SKY_PINFUNCTION(PINCTRL_PIN(107, "GPIO123"), gpio123),
	SKY_PINFUNCTION(PINCTRL_PIN(108, "GPIO124"), gpio124),
	SKY_PINFUNCTION(PINCTRL_PIN(109, "GPIO125"), gpio125),
	SKY_PINFUNCTION(PINCTRL_PIN(110, "GPIO126"), gpio126),
	SKY_PINFUNCTION(PINCTRL_PIN(111, "GPIO127"), gpio127),
	SKY_PINFUNCTION(PINCTRL_PIN(112, "GPIO128"), gpio128),
	SKY_PINFUNCTION(PINCTRL_PIN(113, "GPIO129"), gpio129),
	SKY_PINFUNCTION(PINCTRL_PIN(114, "GPIO130"), gpio130),
	SKY_PINFUNCTION(PINCTRL_PIN(115, "GPIO131"), gpio131),
	SKY_PINFUNCTION(PINCTRL_PIN(116, "GPIO132"), gpio132),
	SKY_PINFUNCTION(PINCTRL_PIN(117, "GPIO133"), gpio133),
	SKY_PINFUNCTION(PINCTRL_PIN(118, "GPIO134"), gpio134),
	SKY_PINFUNCTION(PINCTRL_PIN(119, "GPIO135"), gpio135),
	SKY_PINFUNCTION(PINCTRL_PIN(120, "GPIO136"), gpio136),
	SKY_PINFUNCTION(PINCTRL_PIN(121, "GPIO137"), gpio137),
	SKY_PINFUNCTION(PINCTRL_PIN(122, "GPIO138"), gpio138),
	SKY_PINFUNCTION(PINCTRL_PIN(123, "GPIO139"), gpio139),
	SKY_PINFUNCTION(PINCTRL_PIN(124, "GPIO140"), gpio140),
	SKY_PINFUNCTION(PINCTRL_PIN(125, "GPIO141"), gpio141),
	SKY_PINFUNCTION(PINCTRL_PIN(126, "GPIO142"), gpio142),
	SKY_PINFUNCTION(PINCTRL_PIN(127, "GPIO143"), gpio143),
	SKY_PINFUNCTION(PINCTRL_PIN(128, "GPIO144"), gpio144),
	SKY_PINFUNCTION(PINCTRL_PIN(129, "GPIO145"), gpio145),
	SKY_PINFUNCTION(PINCTRL_PIN(130, "GPIO146"), gpio146),
	SKY_PINFUNCTION(PINCTRL_PIN(131, "GPIO147"), gpio147),
	SKY_PINFUNCTION(PINCTRL_PIN(132, "GPIO148"), gpio148),
	SKY_PINFUNCTION(PINCTRL_PIN(133, "GPIO149"), gpio149),
	SKY_PINFUNCTION(PINCTRL_PIN(134, "GPIO150"), gpio150),
	SKY_PINFUNCTION(PINCTRL_PIN(135, "GPIO151"), gpio151),
	SKY_PINFUNCTION(PINCTRL_PIN(136, "GPIO152"), gpio152),
	SKY_PINFUNCTION(PINCTRL_PIN(137, "GPIO153"), gpio153),
};

static const struct sky1_pinctrl_soc_info sky1_pinctrl_s5_info = {
	.pins = sky1_pinctrl_s5_pads,
	.npins = ARRAY_SIZE(sky1_pinctrl_s5_pads),
};

static const struct sky1_pinctrl_soc_info sky1_pinctrl_info = {
	.pins = sky1_pinctrl_pads,
	.npins = ARRAY_SIZE(sky1_pinctrl_pads),
};

static const struct of_device_id sky1_pinctrl_of_match[] = {
	{ .compatible = "cix,sky1-pinctrl-s5", .data = &sky1_pinctrl_s5_info, },
	{ .compatible = "cix,sky1-pinctrl", .data = &sky1_pinctrl_info, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sky1_pinctrl_of_match);

static int __maybe_unused sky1_pinctrl_suspend(struct device *dev)
{
	struct sky1_pinctrl *spctl = dev_get_drvdata(dev);

	return pinctrl_force_sleep(spctl->pctl);
}

static int __maybe_unused sky1_pinctrl_resume(struct device *dev)
{
	struct sky1_pinctrl *spctl = dev_get_drvdata(dev);

	return pinctrl_force_default(spctl->pctl);
}

const struct dev_pm_ops sky1_pinctrl_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(sky1_pinctrl_suspend,
					sky1_pinctrl_resume)
};
EXPORT_SYMBOL_GPL(sky1_pinctrl_pm_ops);

static int sky1_pinctrl_probe(struct platform_device *pdev)
{
	const struct sky1_pinctrl_soc_info *pinctrl_info;

	pinctrl_info = device_get_match_data(&pdev->dev);
	if (!pinctrl_info)
		return -ENODEV;

	return sky1_base_pinctrl_probe(pdev, pinctrl_info);
}

static struct platform_driver sky1_pinctrl_driver = {
	.driver = {
		.name = "sky1-pinctrl",
		.of_match_table = sky1_pinctrl_of_match,
		.pm = &sky1_pinctrl_pm_ops,
	},
	.probe = sky1_pinctrl_probe,
};

static int __init sky1_pinctrl_init(void)
{
	return platform_driver_register(&sky1_pinctrl_driver);
}
arch_initcall(sky1_pinctrl_init);

MODULE_AUTHOR("Jerry Zhu <Jerry.Zhu@cixtech.com>");
MODULE_DESCRIPTION("Cix Sky1 pinctrl driver");
MODULE_LICENSE("GPL");
