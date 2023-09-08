/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2022 Google, Inc
 *
 * MAXIM TCPC header file.
 */
#ifndef TCPCI_MAXIM_H_
#define TCPCI_MAXIM_H_

#define VENDOR_CC_STATUS2                       0x85
#define CC1_VUFP_RD0P5                          BIT(1)
#define CC2_VUFP_RD0P5                          BIT(5)
#define TCPC_VENDOR_FLADC_STATUS                0x89

#define TCPC_VENDOR_CC_CTRL1                    0x8c
#define CCCONNDRY                               BIT(7)
#define CCCOMPEN                                BIT(5)

#define TCPC_VENDOR_CC_CTRL2                    0x8d
#define SBUOVPDIS                               BIT(7)
#define CCOVPDIS                                BIT(6)
#define SBURPCTRL                               BIT(5)
#define CCLPMODESEL_MASK                        GENMASK(4, 3)
#define ULTRA_LOW_POWER_MODE                    BIT(3)
#define CCRPCTRL_MASK                           GENMASK(2, 0)
#define UA_1_SRC                                1
#define UA_80_SRC                               3

#define TCPC_VENDOR_CC_CTRL3                    0x8e
#define CCWTRDEB_MASK                           GENMASK(7, 6)
#define CCWTRDEB_SHIFT                          6
#define CCWTRDEB_1MS                            1
#define CCWTRSEL_MASK                           GENMASK(5, 3)
#define CCWTRSEL_SHIFT                          3
#define CCWTRSEL_1V                             0x4
#define CCLADDERDIS                             BIT(2)
#define WTRCYCLE_MASK                           BIT(0)
#define WTRCYCLE_SHIFT                          0
#define WTRCYCLE_2_4_S                          0
#define WTRCYCLE_4_8_S                          1

#define TCPC_VENDOR_ADC_CTRL1                   0x91
#define ADCINSEL_MASK                           GENMASK(7, 5)
#define ADC_CHANNEL_OFFSET                      5
#define ADCEN                                   BIT(0)

enum contamiant_state {
	NOT_DETECTED,
	DETECTED,
	SINK,
};

/*
 * @potential_contaminant:
 *		Last returned result to tcpm indicating whether the TCPM port
 *		has potential contaminant.
 */
struct max_tcpci_chip {
	struct tcpci_data data;
	struct tcpci *tcpci;
	struct device *dev;
	struct i2c_client *client;
	struct tcpm_port *port;
	enum contamiant_state contaminant_state;
};

static inline int max_tcpci_read16(struct max_tcpci_chip *chip, unsigned int reg, u16 *val)
{
	return regmap_raw_read(chip->data.regmap, reg, val, sizeof(u16));
}

static inline int max_tcpci_write16(struct max_tcpci_chip *chip, unsigned int reg, u16 val)
{
	return regmap_raw_write(chip->data.regmap, reg, &val, sizeof(u16));
}

static inline int max_tcpci_read8(struct max_tcpci_chip *chip, unsigned int reg, u8 *val)
{
	return regmap_raw_read(chip->data.regmap, reg, val, sizeof(u8));
}

static inline int max_tcpci_write8(struct max_tcpci_chip *chip, unsigned int reg, u8 val)
{
	return regmap_raw_write(chip->data.regmap, reg, &val, sizeof(u8));
}

bool max_contaminant_is_contaminant(struct max_tcpci_chip *chip, bool disconnect_while_debounce);

#endif  // TCPCI_MAXIM_H_
