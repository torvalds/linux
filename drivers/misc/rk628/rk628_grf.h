/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#ifndef RK628_GRF_H
#define RK628_GRF_H

#define GPIO_FUNC 0
#define MUX_FUNC1 1
#define MUX_FUNC2 2
#define MUX_FUNC3 3

/* GRF_SYSTEM_CON3 */
#define UART_CTS_DISABLE 0xB0
#define UART_CTS_ENABLE 0xB1

#define UART_RTS_DISABLE 0xA0
#define UART_RTS_ENABLE 0xA1

#define UART_IOMUX_DISABLE 0x90
#define UART_IOMUX_ENABLE 0x91

#define JTAG_DISABLE 0x80
#define JTAG_ENABLE 0x81

#define HDMIRX_CEC0 0x70
#define HDMIRX_CEC1 0x71

#define SELECT_RXDDC_SDA0 0x60
#define SELECT_RXDDC_SDA1 0x61

#define SELECT_RXDDC_SCL0 0x50
#define SELECT_RXDDC_SCL1 0x51

#define SELECT_I2S_LRM0 0x40
#define SELECT_I2S_LRM1 0x41

#define SELECT_I2S_DM0 0x30
#define SELECT_I2S_DM1 0x31

#define SELECT_I2S_SCKM0 0x20
#define SELECT_I2S_SCKM1 0x21

/* GPIO0_A */
#define GPIO_0A2 0x0a20
#define I2S_SCKM0 0x0a21

#define GPIO0A3 0x0a30
#define I2SLR_M0 0x0a31

#define GPIO0A4 0x0a40
#define I2SM0D0 0x0a41
#define UART_TXM1 0x0a42

#define GPIO0A5 0x0a50
#define I2SM0D1 0x0a51
#define UART_RXM1 0x0a52

#define GPIO0A6 0x0a60
#define I2SM0D2 0x0a61
#define UART_CTSNM1 0x0a62

#define GPIO0A7 0x0a70
#define I2SM0D3 0x0a71
#define UART_RTSNM1 0x0a72


/* GPIO0_B */
#define GPIO0B0 0x0b00
#define HPDIN 0x0b01

#define GPIO0B1 0x0b10
#define DDCSDATX 0x0b11

#define GPIO0B2 0x0b20
#define DDCSCLTX 0x0b21

#define GPIO0B3 0x0b30
#define CECTX 0x0b31


/* GPIO1_A */
#define GPIO1A0 0x1a00
#define TESTCLKOUT 0x1a01

#define GPIO1A1 0x1a10
#define XIPSFC_SCLK 0x1a11

#define GPIO1A2 0x1a20
#define I2SSCKM1 0x1a21

#define GPIO1A3 0x1a30
#define I2SM1LR 0x1a31

#define GPIO1A4 0x1a40
#define I2SM1D0 0x1a41

#define GPIO1A5 0x1a50
#define I2SM1D1 0x1a51

#define GPIO1A6 0x1a60
#define I2SM1D2 0x1a61

#define GPIO1A7 0x1a70
#define I2SM1D3 0x1a71


/* GPIO1_B */
#define GPIO1B0 0x1b00
#define HPDM0OUT 0x1b01

#define GPIO1B1 0x1b10
#define DDCM0SDARX 0x1b11

#define GPIO1B2 0x1b20
#define DDCM0SCLRX 0x1b21

#define GPIO1B3 0x1b30
#define CECM0RX 0x1b31

#define GPIO1B4 0x1b40
#define I2CS_SCL 0x1b41
#define I2CM_SCL 0x1b42

#define GPIO1B5 0x1b50
#define I2CS_SDA 0x1b51
#define I2CM_SDA 0x1b52


/* GPIO2_A */
#define GPIO2A0 0x2a00
#define VOPD0 0x2a01

#define GPIO2A1 0x2a10
#define VOPD1 0x2a11

#define GPIO2A2 0x2a20
#define VOPD2 0x2a21

#define GPIO2A3 0x2a30
#define VOPD3 0x2a31

#define GPIO2A4 0x2a40
#define VOPD4 0x2a41

#define GPIO2A5 0x2a50
#define VOPD5 0x2a51

#define GPIO2A6 0x2a60
#define VOPD6 0x2a61

#define GPIO2A7 0x2a70
#define VOPD7 0x2a71


/* GPIO2_B */
#define GPIO2B0 0x2b00
#define VOPD8 0x2b01

#define GPIO2B1 0x2b10
#define VOPD9 0x2b11

#define GPIO2B2 0x2b20
#define VOPD10 0x2b21

#define GPIO2B3 0x2b30
#define VOPD11 0x2b31

#define GPIO2B4 0x2b40
#define VOPD12 0x2b41

#define GPIO2B5 0x2b50
#define VOPD13 0x2b51

#define GPIO2B6 0x2b60
#define VOPD14 0x2b61

#define GPIO2B7 0x2b70
#define VOPD15 0x2b71


/* GPIO2_C */
#define GPIO2C0 0x2c00
#define VOPD16 0x2c01
#define XIPSFC_CSN 0x2c02

#define GPIO2C1 0x2c10
#define VOPD17 0x2c11
#define XIPSFC_MISO 0x2c12

#define GPIO2C2 0x2c20
#define VOPD18 0x2c21
#define XIPSFC_MOSI 0x2c22

#define GPIO2C3 0x2c30
#define VOPD19 0x2c31
#define RISVJTAG_TDO 0x2c32
#define UART_TXM0 0x2c33

#define GPIO2C4 0x2c40
#define VOPD20 0x2c41
#define RISVJTAG_TDI 0x2c42
#define UART_RXM0 0x2c43

#define GPIO2C5 0x2c50
#define VOPD21 0x2c51
#define RISVJTAG_TMS 0x2c52
#define UART_CTSNM0 0x2c53

#define GPIO2C6 0x2c60
#define VOPD22 0x2c61
#define RISVJTAG_TCK 0x2c62
#define UART_RTSNM0 0x2c63

#define GPIO2C7 0x2c70
#define VOPD23 0x2c71
#define RISVJTAG_TRSTN 0x2c72


/* GPIO3_A */
#define GPIO3A0 0x3a00
#define VOPDEN 0x3a01

#define GPIO3A1 0x3a10
#define VOPHSYNC 0x3a11

#define GPIO3A3 0x3a30
#define VOPVSYNC 0x3a31

#define GPIO3A4 0x3a40
#define HPDM1OUT 0x3a41

#define GPIO3A5 0x3a50
#define DDCM1SDARX 0x3a51

#define GPIO3A6 0x3a60
#define DDCM1SCLRX 0x3a61

#define GPIO3A7 0x3a70
#define CECM1RX 0x3a71


/* GPIO3_B */
#define GPIO3B0 0x3b00
#define VOPDCLK 0x3b01

#define GPIO3B1 0x3b10
#define GVIHPD 0x3b11

#define GPIO3B2 0x3b20
#define GVILOCK 0x3b21

#define GPIO3B4 0x3b40
#define SPIBOOT 0x3b41
#define INT 0x3b42


#endif // RK628_GRF_H


