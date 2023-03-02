/* SPDX-License-Identifier: GPL-2.0 */

#ifndef	__LATTICE_SYSCONFIG_H
#define	__LATTICE_SYSCONFIG_H

#define	SYSCONFIG_ISC_ENABLE		{0xC6, 0x00, 0x00, 0x00}
#define	SYSCONFIG_ISC_DISABLE		{0x26, 0x00, 0x00, 0x00}
#define	SYSCONFIG_ISC_ERASE		{0x0E, 0x01, 0x00, 0x00}
#define	SYSCONFIG_LSC_READ_STATUS	{0x3C, 0x00, 0x00, 0x00}
#define	SYSCONFIG_LSC_CHECK_BUSY	{0xF0, 0x00, 0x00, 0x00}
#define	SYSCONFIG_LSC_REFRESH		{0x79, 0x00, 0x00, 0x00}
#define	SYSCONFIG_LSC_INIT_ADDR		{0x46, 0x00, 0x00, 0x00}
#define	SYSCONFIG_LSC_BITSTREAM_BURST	{0x7a, 0x00, 0x00, 0x00}

#define	SYSCONFIG_STATUS_DONE		BIT(8)
#define	SYSCONFIG_STATUS_BUSY		BIT(12)
#define	SYSCONFIG_STATUS_FAIL		BIT(13)
#define	SYSCONFIG_STATUS_ERR		GENMASK(25, 23)

#define	SYSCONFIG_POLL_INTERVAL_US	30
#define	SYSCONFIG_POLL_BUSY_TIMEOUT_US	1000000
#define	SYSCONFIG_POLL_GPIO_TIMEOUT_US	100000

struct sysconfig_priv {
	struct gpio_desc *program;
	struct gpio_desc *init;
	struct gpio_desc *done;
	struct device *dev;
	int (*command_transfer)(struct sysconfig_priv *priv, const void *tx_buf,
				size_t tx_len, void *rx_buf, size_t rx_len);
	int (*bitstream_burst_write_init)(struct sysconfig_priv *priv);
	int (*bitstream_burst_write)(struct sysconfig_priv *priv,
				     const char *tx_buf, size_t tx_len);
	int (*bitstream_burst_write_complete)(struct sysconfig_priv *priv);
};

int sysconfig_probe(struct sysconfig_priv *priv);

#endif /* __LATTICE_SYSCONFIG_H */
