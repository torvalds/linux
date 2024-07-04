// SPDX-License-Identifier: GPL-2.0-or-later
/*
    Copyright (c) 1998 - 2002  Frodo Looijaard <frodol@dds.nl>,
    Philip Edelbrock <phil@netroedge.com>, and Mark D. Studebaker
    <mdsxyz123@yahoo.com>
    Copyright (C) 2007 - 2014  Jean Delvare <jdelvare@suse.de>
    Copyright (C) 2010         Intel Corporation,
                               David Woodhouse <dwmw2@infradead.org>

*/

/*
 * Supports the following Intel I/O Controller Hubs (ICH):
 *
 *					I/O			Block	I2C
 *					region	SMBus	Block	proc.	block
 * Chip name			PCI ID	size	PEC	buffer	call	read
 * ---------------------------------------------------------------------------
 * 82801AA (ICH)		0x2413	16	no	no	no	no
 * 82801AB (ICH0)		0x2423	16	no	no	no	no
 * 82801BA (ICH2)		0x2443	16	no	no	no	no
 * 82801CA (ICH3)		0x2483	32	soft	no	no	no
 * 82801DB (ICH4)		0x24c3	32	hard	yes	no	no
 * 82801E (ICH5)		0x24d3	32	hard	yes	yes	yes
 * 6300ESB			0x25a4	32	hard	yes	yes	yes
 * 82801F (ICH6)		0x266a	32	hard	yes	yes	yes
 * 6310ESB/6320ESB		0x269b	32	hard	yes	yes	yes
 * 82801G (ICH7)		0x27da	32	hard	yes	yes	yes
 * 82801H (ICH8)		0x283e	32	hard	yes	yes	yes
 * 82801I (ICH9)		0x2930	32	hard	yes	yes	yes
 * EP80579 (Tolapai)		0x5032	32	hard	yes	yes	yes
 * ICH10			0x3a30	32	hard	yes	yes	yes
 * ICH10			0x3a60	32	hard	yes	yes	yes
 * 5/3400 Series (PCH)		0x3b30	32	hard	yes	yes	yes
 * 6 Series (PCH)		0x1c22	32	hard	yes	yes	yes
 * Patsburg (PCH)		0x1d22	32	hard	yes	yes	yes
 * Patsburg (PCH) IDF		0x1d70	32	hard	yes	yes	yes
 * Patsburg (PCH) IDF		0x1d71	32	hard	yes	yes	yes
 * Patsburg (PCH) IDF		0x1d72	32	hard	yes	yes	yes
 * DH89xxCC (PCH)		0x2330	32	hard	yes	yes	yes
 * Panther Point (PCH)		0x1e22	32	hard	yes	yes	yes
 * Lynx Point (PCH)		0x8c22	32	hard	yes	yes	yes
 * Lynx Point-LP (PCH)		0x9c22	32	hard	yes	yes	yes
 * Avoton (SOC)			0x1f3c	32	hard	yes	yes	yes
 * Wellsburg (PCH)		0x8d22	32	hard	yes	yes	yes
 * Wellsburg (PCH) MS		0x8d7d	32	hard	yes	yes	yes
 * Wellsburg (PCH) MS		0x8d7e	32	hard	yes	yes	yes
 * Wellsburg (PCH) MS		0x8d7f	32	hard	yes	yes	yes
 * Coleto Creek (PCH)		0x23b0	32	hard	yes	yes	yes
 * Wildcat Point (PCH)		0x8ca2	32	hard	yes	yes	yes
 * Wildcat Point-LP (PCH)	0x9ca2	32	hard	yes	yes	yes
 * BayTrail (SOC)		0x0f12	32	hard	yes	yes	yes
 * Braswell (SOC)		0x2292	32	hard	yes	yes	yes
 * Sunrise Point-H (PCH) 	0xa123  32	hard	yes	yes	yes
 * Sunrise Point-LP (PCH)	0x9d23	32	hard	yes	yes	yes
 * DNV (SOC)			0x19df	32	hard	yes	yes	yes
 * Emmitsburg (PCH)		0x1bc9	32	hard	yes	yes	yes
 * Broxton (SOC)		0x5ad4	32	hard	yes	yes	yes
 * Lewisburg (PCH)		0xa1a3	32	hard	yes	yes	yes
 * Lewisburg Supersku (PCH)	0xa223	32	hard	yes	yes	yes
 * Kaby Lake PCH-H (PCH)	0xa2a3	32	hard	yes	yes	yes
 * Gemini Lake (SOC)		0x31d4	32	hard	yes	yes	yes
 * Cannon Lake-H (PCH)		0xa323	32	hard	yes	yes	yes
 * Cannon Lake-LP (PCH)		0x9da3	32	hard	yes	yes	yes
 * Cedar Fork (PCH)		0x18df	32	hard	yes	yes	yes
 * Ice Lake-LP (PCH)		0x34a3	32	hard	yes	yes	yes
 * Ice Lake-N (PCH)		0x38a3	32	hard	yes	yes	yes
 * Comet Lake (PCH)		0x02a3	32	hard	yes	yes	yes
 * Comet Lake-H (PCH)		0x06a3	32	hard	yes	yes	yes
 * Elkhart Lake (PCH)		0x4b23	32	hard	yes	yes	yes
 * Tiger Lake-LP (PCH)		0xa0a3	32	hard	yes	yes	yes
 * Tiger Lake-H (PCH)		0x43a3	32	hard	yes	yes	yes
 * Jasper Lake (SOC)		0x4da3	32	hard	yes	yes	yes
 * Comet Lake-V (PCH)		0xa3a3	32	hard	yes	yes	yes
 * Alder Lake-S (PCH)		0x7aa3	32	hard	yes	yes	yes
 * Alder Lake-P (PCH)		0x51a3	32	hard	yes	yes	yes
 * Alder Lake-M (PCH)		0x54a3	32	hard	yes	yes	yes
 * Raptor Lake-S (PCH)		0x7a23	32	hard	yes	yes	yes
 * Meteor Lake-P (SOC)		0x7e22	32	hard	yes	yes	yes
 * Meteor Lake SoC-S (SOC)	0xae22	32	hard	yes	yes	yes
 * Meteor Lake PCH-S (PCH)	0x7f23	32	hard	yes	yes	yes
 * Birch Stream (SOC)		0x5796	32	hard	yes	yes	yes
 *
 * Features supported by this driver:
 * Software PEC				no
 * Hardware PEC				yes
 * Block buffer				yes
 * Block process call transaction	yes
 * I2C block read transaction		yes (doesn't use the block buffer)
 * Slave mode				no
 * SMBus Host Notify			yes
 * Interrupt processing			yes
 *
 * See the file Documentation/i2c/busses/i2c-i801.rst for details.
 */

#define DRV_NAME	"i801_smbus"

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/i2c-smbus.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/platform_data/itco_wdt.h>
#include <linux/platform_data/x86/p2sb.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>

#ifdef CONFIG_I2C_I801_MUX
#include <linux/gpio/machine.h>
#include <linux/platform_data/i2c-mux-gpio.h>
#endif

/* I801 SMBus address offsets */
#define SMBHSTSTS(p)	(0 + (p)->smba)
#define SMBHSTCNT(p)	(2 + (p)->smba)
#define SMBHSTCMD(p)	(3 + (p)->smba)
#define SMBHSTADD(p)	(4 + (p)->smba)
#define SMBHSTDAT0(p)	(5 + (p)->smba)
#define SMBHSTDAT1(p)	(6 + (p)->smba)
#define SMBBLKDAT(p)	(7 + (p)->smba)
#define SMBPEC(p)	(8 + (p)->smba)		/* ICH3 and later */
#define SMBAUXSTS(p)	(12 + (p)->smba)	/* ICH4 and later */
#define SMBAUXCTL(p)	(13 + (p)->smba)	/* ICH4 and later */
#define SMBSLVSTS(p)	(16 + (p)->smba)	/* ICH3 and later */
#define SMBSLVCMD(p)	(17 + (p)->smba)	/* ICH3 and later */
#define SMBNTFDADD(p)	(20 + (p)->smba)	/* ICH3 and later */

/* PCI Address Constants */
#define SMBBAR		4
#define SMBHSTCFG	0x040
#define TCOBASE		0x050
#define TCOCTL		0x054

#define SBREG_SMBCTRL		0xc6000c
#define SBREG_SMBCTRL_DNV	0xcf000c

/* Host configuration bits for SMBHSTCFG */
#define SMBHSTCFG_HST_EN	BIT(0)
#define SMBHSTCFG_SMB_SMI_EN	BIT(1)
#define SMBHSTCFG_I2C_EN	BIT(2)
#define SMBHSTCFG_SPD_WD	BIT(4)

/* TCO configuration bits for TCOCTL */
#define TCOCTL_EN		BIT(8)

/* Auxiliary status register bits, ICH4+ only */
#define SMBAUXSTS_CRCE		BIT(0)
#define SMBAUXSTS_STCO		BIT(1)

/* Auxiliary control register bits, ICH4+ only */
#define SMBAUXCTL_CRC		BIT(0)
#define SMBAUXCTL_E32B		BIT(1)

/* I801 command constants */
#define I801_QUICK		0x00
#define I801_BYTE		0x04
#define I801_BYTE_DATA		0x08
#define I801_WORD_DATA		0x0C
#define I801_PROC_CALL		0x10
#define I801_BLOCK_DATA		0x14
#define I801_I2C_BLOCK_DATA	0x18	/* ICH5 and later */
#define I801_BLOCK_PROC_CALL	0x1C

/* I801 Host Control register bits */
#define SMBHSTCNT_INTREN	BIT(0)
#define SMBHSTCNT_KILL		BIT(1)
#define SMBHSTCNT_LAST_BYTE	BIT(5)
#define SMBHSTCNT_START		BIT(6)
#define SMBHSTCNT_PEC_EN	BIT(7)	/* ICH3 and later */

/* I801 Hosts Status register bits */
#define SMBHSTSTS_BYTE_DONE	BIT(7)
#define SMBHSTSTS_INUSE_STS	BIT(6)
#define SMBHSTSTS_SMBALERT_STS	BIT(5)
#define SMBHSTSTS_FAILED	BIT(4)
#define SMBHSTSTS_BUS_ERR	BIT(3)
#define SMBHSTSTS_DEV_ERR	BIT(2)
#define SMBHSTSTS_INTR		BIT(1)
#define SMBHSTSTS_HOST_BUSY	BIT(0)

/* Host Notify Status register bits */
#define SMBSLVSTS_HST_NTFY_STS	BIT(0)

/* Host Notify Command register bits */
#define SMBSLVCMD_SMBALERT_DISABLE	BIT(2)
#define SMBSLVCMD_HST_NTFY_INTREN	BIT(0)

#define STATUS_ERROR_FLAGS	(SMBHSTSTS_FAILED | SMBHSTSTS_BUS_ERR | \
				 SMBHSTSTS_DEV_ERR)

#define STATUS_FLAGS		(SMBHSTSTS_BYTE_DONE | SMBHSTSTS_INTR | \
				 STATUS_ERROR_FLAGS)

#define SMBUS_LEN_SENTINEL (I2C_SMBUS_BLOCK_MAX + 1)

/* Older devices have their ID defined in <linux/pci_ids.h> */
#define PCI_DEVICE_ID_INTEL_COMETLAKE_SMBUS		0x02a3
#define PCI_DEVICE_ID_INTEL_COMETLAKE_H_SMBUS		0x06a3
#define PCI_DEVICE_ID_INTEL_BAYTRAIL_SMBUS		0x0f12
#define PCI_DEVICE_ID_INTEL_CDF_SMBUS			0x18df
#define PCI_DEVICE_ID_INTEL_DNV_SMBUS			0x19df
#define PCI_DEVICE_ID_INTEL_EBG_SMBUS			0x1bc9
#define PCI_DEVICE_ID_INTEL_COUGARPOINT_SMBUS		0x1c22
#define PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS		0x1d22
/* Patsburg also has three 'Integrated Device Function' SMBus controllers */
#define PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS_IDF0		0x1d70
#define PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS_IDF1		0x1d71
#define PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS_IDF2		0x1d72
#define PCI_DEVICE_ID_INTEL_PANTHERPOINT_SMBUS		0x1e22
#define PCI_DEVICE_ID_INTEL_AVOTON_SMBUS		0x1f3c
#define PCI_DEVICE_ID_INTEL_BRASWELL_SMBUS		0x2292
#define PCI_DEVICE_ID_INTEL_DH89XXCC_SMBUS		0x2330
#define PCI_DEVICE_ID_INTEL_COLETOCREEK_SMBUS		0x23b0
#define PCI_DEVICE_ID_INTEL_GEMINILAKE_SMBUS		0x31d4
#define PCI_DEVICE_ID_INTEL_ICELAKE_LP_SMBUS		0x34a3
#define PCI_DEVICE_ID_INTEL_ICELAKE_N_SMBUS		0x38a3
#define PCI_DEVICE_ID_INTEL_5_3400_SERIES_SMBUS		0x3b30
#define PCI_DEVICE_ID_INTEL_TIGERLAKE_H_SMBUS		0x43a3
#define PCI_DEVICE_ID_INTEL_ELKHART_LAKE_SMBUS		0x4b23
#define PCI_DEVICE_ID_INTEL_JASPER_LAKE_SMBUS		0x4da3
#define PCI_DEVICE_ID_INTEL_ALDER_LAKE_P_SMBUS		0x51a3
#define PCI_DEVICE_ID_INTEL_ALDER_LAKE_M_SMBUS		0x54a3
#define PCI_DEVICE_ID_INTEL_BIRCH_STREAM_SMBUS		0x5796
#define PCI_DEVICE_ID_INTEL_BROXTON_SMBUS		0x5ad4
#define PCI_DEVICE_ID_INTEL_RAPTOR_LAKE_S_SMBUS		0x7a23
#define PCI_DEVICE_ID_INTEL_ALDER_LAKE_S_SMBUS		0x7aa3
#define PCI_DEVICE_ID_INTEL_METEOR_LAKE_P_SMBUS		0x7e22
#define PCI_DEVICE_ID_INTEL_METEOR_LAKE_PCH_S_SMBUS	0x7f23
#define PCI_DEVICE_ID_INTEL_LYNXPOINT_SMBUS		0x8c22
#define PCI_DEVICE_ID_INTEL_WILDCATPOINT_SMBUS		0x8ca2
#define PCI_DEVICE_ID_INTEL_WELLSBURG_SMBUS		0x8d22
#define PCI_DEVICE_ID_INTEL_WELLSBURG_SMBUS_MS0		0x8d7d
#define PCI_DEVICE_ID_INTEL_WELLSBURG_SMBUS_MS1		0x8d7e
#define PCI_DEVICE_ID_INTEL_WELLSBURG_SMBUS_MS2		0x8d7f
#define PCI_DEVICE_ID_INTEL_LYNXPOINT_LP_SMBUS		0x9c22
#define PCI_DEVICE_ID_INTEL_WILDCATPOINT_LP_SMBUS	0x9ca2
#define PCI_DEVICE_ID_INTEL_SUNRISEPOINT_LP_SMBUS	0x9d23
#define PCI_DEVICE_ID_INTEL_CANNONLAKE_LP_SMBUS		0x9da3
#define PCI_DEVICE_ID_INTEL_TIGERLAKE_LP_SMBUS		0xa0a3
#define PCI_DEVICE_ID_INTEL_SUNRISEPOINT_H_SMBUS	0xa123
#define PCI_DEVICE_ID_INTEL_LEWISBURG_SMBUS		0xa1a3
#define PCI_DEVICE_ID_INTEL_LEWISBURG_SSKU_SMBUS	0xa223
#define PCI_DEVICE_ID_INTEL_KABYLAKE_PCH_H_SMBUS	0xa2a3
#define PCI_DEVICE_ID_INTEL_CANNONLAKE_H_SMBUS		0xa323
#define PCI_DEVICE_ID_INTEL_COMETLAKE_V_SMBUS		0xa3a3
#define PCI_DEVICE_ID_INTEL_METEOR_LAKE_SOC_S_SMBUS	0xae22

struct i801_mux_config {
	char *gpio_chip;
	unsigned values[3];
	int n_values;
	unsigned gpios[2];		/* Relative to gpio_chip->base */
	int n_gpios;
};

struct i801_priv {
	struct i2c_adapter adapter;
	unsigned long smba;
	unsigned char original_hstcfg;
	unsigned char original_hstcnt;
	unsigned char original_slvcmd;
	struct pci_dev *pci_dev;
	unsigned int features;

	/* isr processing */
	struct completion done;
	u8 status;

	/* Command state used by isr for byte-by-byte block transactions */
	u8 cmd;
	bool is_read;
	int count;
	int len;
	u8 *data;

#ifdef CONFIG_I2C_I801_MUX
	struct platform_device *mux_pdev;
	struct gpiod_lookup_table *lookup;
	struct notifier_block mux_notifier_block;
#endif
	struct platform_device *tco_pdev;

	/*
	 * If set to true the host controller registers are reserved for
	 * ACPI AML use.
	 */
	bool acpi_reserved;
};

#define FEATURE_SMBUS_PEC	BIT(0)
#define FEATURE_BLOCK_BUFFER	BIT(1)
#define FEATURE_BLOCK_PROC	BIT(2)
#define FEATURE_I2C_BLOCK_READ	BIT(3)
#define FEATURE_IRQ		BIT(4)
#define FEATURE_HOST_NOTIFY	BIT(5)
/* Not really a feature, but it's convenient to handle it as such */
#define FEATURE_IDF		BIT(15)
#define FEATURE_TCO_SPT		BIT(16)
#define FEATURE_TCO_CNL		BIT(17)

static const char *i801_feature_names[] = {
	"SMBus PEC",
	"Block buffer",
	"Block process call",
	"I2C block read",
	"Interrupt",
	"SMBus Host Notify",
};

static unsigned int disable_features;
module_param(disable_features, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(disable_features, "Disable selected driver features:\n"
	"\t\t  0x01  disable SMBus PEC\n"
	"\t\t  0x02  disable the block buffer\n"
	"\t\t  0x08  disable the I2C block read functionality\n"
	"\t\t  0x10  don't use interrupts\n"
	"\t\t  0x20  disable SMBus Host Notify ");

static int i801_get_block_len(struct i801_priv *priv)
{
	u8 len = inb_p(SMBHSTDAT0(priv));

	if (len < 1 || len > I2C_SMBUS_BLOCK_MAX) {
		pci_err(priv->pci_dev, "Illegal SMBus block read size %u\n", len);
		return -EPROTO;
	}

	return len;
}

static int i801_check_and_clear_pec_error(struct i801_priv *priv)
{
	u8 status;

	if (!(priv->features & FEATURE_SMBUS_PEC))
		return 0;

	status = inb_p(SMBAUXSTS(priv)) & SMBAUXSTS_CRCE;
	if (status) {
		outb_p(status, SMBAUXSTS(priv));
		return -EBADMSG;
	}

	return 0;
}

/* Make sure the SMBus host is ready to start transmitting.
   Return 0 if it is, -EBUSY if it is not. */
static int i801_check_pre(struct i801_priv *priv)
{
	int status, result;

	status = inb_p(SMBHSTSTS(priv));
	if (status & SMBHSTSTS_HOST_BUSY) {
		pci_err(priv->pci_dev, "SMBus is busy, can't use it!\n");
		return -EBUSY;
	}

	status &= STATUS_FLAGS;
	if (status) {
		pci_dbg(priv->pci_dev, "Clearing status flags (%02x)\n", status);
		outb_p(status, SMBHSTSTS(priv));
	}

	/*
	 * Clear CRC status if needed.
	 * During normal operation, i801_check_post() takes care
	 * of it after every operation.  We do it here only in case
	 * the hardware was already in this state when the driver
	 * started.
	 */
	result = i801_check_and_clear_pec_error(priv);
	if (result)
		pci_dbg(priv->pci_dev, "Clearing aux status flag CRCE\n");

	return 0;
}

static int i801_check_post(struct i801_priv *priv, int status)
{
	int result = 0;

	/*
	 * If the SMBus is still busy, we give up
	 */
	if (unlikely(status < 0)) {
		/* try to stop the current command */
		outb_p(SMBHSTCNT_KILL, SMBHSTCNT(priv));
		usleep_range(1000, 2000);
		outb_p(0, SMBHSTCNT(priv));

		/* Check if it worked */
		status = inb_p(SMBHSTSTS(priv));
		if ((status & SMBHSTSTS_HOST_BUSY) ||
		    !(status & SMBHSTSTS_FAILED))
			dev_dbg(&priv->pci_dev->dev,
				"Failed terminating the transaction\n");
		return -ETIMEDOUT;
	}

	if (status & SMBHSTSTS_FAILED) {
		result = -EIO;
		dev_err(&priv->pci_dev->dev, "Transaction failed\n");
	}
	if (status & SMBHSTSTS_DEV_ERR) {
		/*
		 * This may be a PEC error, check and clear it.
		 *
		 * AUXSTS is handled differently from HSTSTS.
		 * For HSTSTS, i801_isr() or i801_wait_intr()
		 * has already cleared the error bits in hardware,
		 * and we are passed a copy of the original value
		 * in "status".
		 * For AUXSTS, the hardware register is left
		 * for us to handle here.
		 * This is asymmetric, slightly iffy, but safe,
		 * since all this code is serialized and the CRCE
		 * bit is harmless as long as it's cleared before
		 * the next operation.
		 */
		result = i801_check_and_clear_pec_error(priv);
		if (result) {
			pci_dbg(priv->pci_dev, "PEC error\n");
		} else {
			result = -ENXIO;
			pci_dbg(priv->pci_dev, "No response\n");
		}
	}
	if (status & SMBHSTSTS_BUS_ERR) {
		result = -EAGAIN;
		dev_dbg(&priv->pci_dev->dev, "Lost arbitration\n");
	}

	return result;
}

/* Wait for BUSY being cleared and either INTR or an error flag being set */
static int i801_wait_intr(struct i801_priv *priv)
{
	unsigned long timeout = jiffies + priv->adapter.timeout;
	int status, busy;

	do {
		usleep_range(250, 500);
		status = inb_p(SMBHSTSTS(priv));
		busy = status & SMBHSTSTS_HOST_BUSY;
		status &= STATUS_ERROR_FLAGS | SMBHSTSTS_INTR;
		if (!busy && status)
			return status & STATUS_ERROR_FLAGS;
	} while (time_is_after_eq_jiffies(timeout));

	return -ETIMEDOUT;
}

/* Wait for either BYTE_DONE or an error flag being set */
static int i801_wait_byte_done(struct i801_priv *priv)
{
	unsigned long timeout = jiffies + priv->adapter.timeout;
	int status;

	do {
		usleep_range(250, 500);
		status = inb_p(SMBHSTSTS(priv));
		if (status & (STATUS_ERROR_FLAGS | SMBHSTSTS_BYTE_DONE))
			return status & STATUS_ERROR_FLAGS;
	} while (time_is_after_eq_jiffies(timeout));

	return -ETIMEDOUT;
}

static int i801_transaction(struct i801_priv *priv, int xact)
{
	unsigned long result;
	const struct i2c_adapter *adap = &priv->adapter;

	if (priv->features & FEATURE_IRQ) {
		reinit_completion(&priv->done);
		outb_p(xact | SMBHSTCNT_INTREN | SMBHSTCNT_START,
		       SMBHSTCNT(priv));
		result = wait_for_completion_timeout(&priv->done, adap->timeout);
		return result ? priv->status : -ETIMEDOUT;
	}

	outb_p(xact | SMBHSTCNT_START, SMBHSTCNT(priv));

	return i801_wait_intr(priv);
}

static int i801_block_transaction_by_block(struct i801_priv *priv,
					   union i2c_smbus_data *data,
					   char read_write, int command)
{
	int i, len, status, xact;

	switch (command) {
	case I2C_SMBUS_BLOCK_PROC_CALL:
		xact = I801_BLOCK_PROC_CALL;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		xact = I801_BLOCK_DATA;
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Set block buffer mode */
	outb_p(inb_p(SMBAUXCTL(priv)) | SMBAUXCTL_E32B, SMBAUXCTL(priv));

	if (read_write == I2C_SMBUS_WRITE) {
		len = data->block[0];
		outb_p(len, SMBHSTDAT0(priv));
		inb_p(SMBHSTCNT(priv));	/* reset the data buffer index */
		for (i = 0; i < len; i++)
			outb_p(data->block[i+1], SMBBLKDAT(priv));
	}

	status = i801_transaction(priv, xact);
	if (status)
		goto out;

	if (read_write == I2C_SMBUS_READ ||
	    command == I2C_SMBUS_BLOCK_PROC_CALL) {
		len = i801_get_block_len(priv);
		if (len < 0) {
			status = len;
			goto out;
		}

		data->block[0] = len;
		inb_p(SMBHSTCNT(priv));	/* reset the data buffer index */
		for (i = 0; i < len; i++)
			data->block[i + 1] = inb_p(SMBBLKDAT(priv));
	}
out:
	outb_p(inb_p(SMBAUXCTL(priv)) & ~SMBAUXCTL_E32B, SMBAUXCTL(priv));
	return status;
}

static void i801_isr_byte_done(struct i801_priv *priv)
{
	if (priv->is_read) {
		/*
		 * At transfer start i801_smbus_block_transaction() marks
		 * the block length as invalid. Check for this sentinel value
		 * and read the block length from SMBHSTDAT0.
		 */
		if (priv->len == SMBUS_LEN_SENTINEL) {
			priv->len = i801_get_block_len(priv);
			if (priv->len < 0)
				/* FIXME: Recover */
				priv->len = I2C_SMBUS_BLOCK_MAX;

			priv->data[-1] = priv->len;
		}

		/* Read next byte */
		if (priv->count < priv->len)
			priv->data[priv->count++] = inb(SMBBLKDAT(priv));
		else
			dev_dbg(&priv->pci_dev->dev,
				"Discarding extra byte on block read\n");

		/* Set LAST_BYTE for last byte of read transaction */
		if (priv->count == priv->len - 1)
			outb_p(priv->cmd | SMBHSTCNT_LAST_BYTE,
			       SMBHSTCNT(priv));
	} else if (priv->count < priv->len - 1) {
		/* Write next byte, except for IRQ after last byte */
		outb_p(priv->data[++priv->count], SMBBLKDAT(priv));
	}
}

static irqreturn_t i801_host_notify_isr(struct i801_priv *priv)
{
	unsigned short addr;

	addr = inb_p(SMBNTFDADD(priv)) >> 1;

	/*
	 * With the tested platforms, reading SMBNTFDDAT (22 + (p)->smba)
	 * always returns 0. Our current implementation doesn't provide
	 * data, so we just ignore it.
	 */
	i2c_handle_smbus_host_notify(&priv->adapter, addr);

	/* clear Host Notify bit and return */
	outb_p(SMBSLVSTS_HST_NTFY_STS, SMBSLVSTS(priv));
	return IRQ_HANDLED;
}

/*
 * There are three kinds of interrupts:
 *
 * 1) i801 signals transaction completion with one of these interrupts:
 *      INTR - Success
 *      DEV_ERR - Invalid command, NAK or communication timeout
 *      BUS_ERR - SMI# transaction collision
 *      FAILED - transaction was canceled due to a KILL request
 *    When any of these occur, update ->status and signal completion.
 *
 * 2) For byte-by-byte (I2C read/write) transactions, one BYTE_DONE interrupt
 *    occurs for each byte of a byte-by-byte to prepare the next byte.
 *
 * 3) Host Notify interrupts
 */
static irqreturn_t i801_isr(int irq, void *dev_id)
{
	struct i801_priv *priv = dev_id;
	u16 pcists;
	u8 status;

	/* Confirm this is our interrupt */
	pci_read_config_word(priv->pci_dev, PCI_STATUS, &pcists);
	if (!(pcists & PCI_STATUS_INTERRUPT))
		return IRQ_NONE;

	if (priv->features & FEATURE_HOST_NOTIFY) {
		status = inb_p(SMBSLVSTS(priv));
		if (status & SMBSLVSTS_HST_NTFY_STS)
			return i801_host_notify_isr(priv);
	}

	status = inb_p(SMBHSTSTS(priv));
	if ((status & (SMBHSTSTS_BYTE_DONE | STATUS_ERROR_FLAGS)) == SMBHSTSTS_BYTE_DONE)
		i801_isr_byte_done(priv);

	/*
	 * Clear IRQ sources: SMB_ALERT status is set after signal assertion
	 * independently of the interrupt generation being blocked or not
	 * so clear it always when the status is set.
	 */
	status &= STATUS_FLAGS | SMBHSTSTS_SMBALERT_STS;
	outb_p(status, SMBHSTSTS(priv));

	status &= STATUS_ERROR_FLAGS | SMBHSTSTS_INTR;
	if (status) {
		priv->status = status & STATUS_ERROR_FLAGS;
		complete(&priv->done);
	}

	return IRQ_HANDLED;
}

/*
 * For "byte-by-byte" block transactions:
 *   I2C write uses cmd=I801_BLOCK_DATA, I2C_EN=1
 *   I2C read uses cmd=I801_I2C_BLOCK_DATA
 */
static int i801_block_transaction_byte_by_byte(struct i801_priv *priv,
					       union i2c_smbus_data *data,
					       char read_write, int command)
{
	int i, len;
	int smbcmd;
	int status;
	unsigned long result;
	const struct i2c_adapter *adap = &priv->adapter;

	if (command == I2C_SMBUS_BLOCK_PROC_CALL)
		return -EOPNOTSUPP;

	len = data->block[0];

	if (read_write == I2C_SMBUS_WRITE) {
		outb_p(len, SMBHSTDAT0(priv));
		outb_p(data->block[1], SMBBLKDAT(priv));
	}

	if (command == I2C_SMBUS_I2C_BLOCK_DATA &&
	    read_write == I2C_SMBUS_READ)
		smbcmd = I801_I2C_BLOCK_DATA;
	else
		smbcmd = I801_BLOCK_DATA;

	if (priv->features & FEATURE_IRQ) {
		priv->is_read = (read_write == I2C_SMBUS_READ);
		if (len == 1 && priv->is_read)
			smbcmd |= SMBHSTCNT_LAST_BYTE;
		priv->cmd = smbcmd | SMBHSTCNT_INTREN;
		priv->len = len;
		priv->count = 0;
		priv->data = &data->block[1];

		reinit_completion(&priv->done);
		outb_p(priv->cmd | SMBHSTCNT_START, SMBHSTCNT(priv));
		result = wait_for_completion_timeout(&priv->done, adap->timeout);
		return result ? priv->status : -ETIMEDOUT;
	}

	if (len == 1 && read_write == I2C_SMBUS_READ)
		smbcmd |= SMBHSTCNT_LAST_BYTE;
	outb_p(smbcmd | SMBHSTCNT_START, SMBHSTCNT(priv));

	for (i = 1; i <= len; i++) {
		status = i801_wait_byte_done(priv);
		if (status)
			return status;

		/*
		 * At transfer start i801_smbus_block_transaction() marks
		 * the block length as invalid. Check for this sentinel value
		 * and read the block length from SMBHSTDAT0.
		 */
		if (len == SMBUS_LEN_SENTINEL) {
			len = i801_get_block_len(priv);
			if (len < 0) {
				/* Recover */
				while (inb_p(SMBHSTSTS(priv)) &
				       SMBHSTSTS_HOST_BUSY)
					outb_p(SMBHSTSTS_BYTE_DONE,
					       SMBHSTSTS(priv));
				outb_p(SMBHSTSTS_INTR, SMBHSTSTS(priv));
				return -EPROTO;
			}
			data->block[0] = len;
		}

		if (read_write == I2C_SMBUS_READ) {
			data->block[i] = inb_p(SMBBLKDAT(priv));
			if (i == len - 1)
				outb_p(smbcmd | SMBHSTCNT_LAST_BYTE, SMBHSTCNT(priv));
		}

		if (read_write == I2C_SMBUS_WRITE && i+1 <= len)
			outb_p(data->block[i+1], SMBBLKDAT(priv));

		/* signals SMBBLKDAT ready */
		outb_p(SMBHSTSTS_BYTE_DONE, SMBHSTSTS(priv));
	}

	return i801_wait_intr(priv);
}

static void i801_set_hstadd(struct i801_priv *priv, u8 addr, char read_write)
{
	outb_p((addr << 1) | (read_write & 0x01), SMBHSTADD(priv));
}

/* Single value transaction function */
static int i801_simple_transaction(struct i801_priv *priv, union i2c_smbus_data *data,
				   u8 addr, u8 hstcmd, char read_write, int command)
{
	int xact, ret;

	switch (command) {
	case I2C_SMBUS_QUICK:
		i801_set_hstadd(priv, addr, read_write);
		xact = I801_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		i801_set_hstadd(priv, addr, read_write);
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(hstcmd, SMBHSTCMD(priv));
		xact = I801_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		i801_set_hstadd(priv, addr, read_write);
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(data->byte, SMBHSTDAT0(priv));
		outb_p(hstcmd, SMBHSTCMD(priv));
		xact = I801_BYTE_DATA;
		break;
	case I2C_SMBUS_WORD_DATA:
		i801_set_hstadd(priv, addr, read_write);
		if (read_write == I2C_SMBUS_WRITE) {
			outb_p(data->word & 0xff, SMBHSTDAT0(priv));
			outb_p((data->word & 0xff00) >> 8, SMBHSTDAT1(priv));
		}
		outb_p(hstcmd, SMBHSTCMD(priv));
		xact = I801_WORD_DATA;
		break;
	case I2C_SMBUS_PROC_CALL:
		i801_set_hstadd(priv, addr, I2C_SMBUS_WRITE);
		outb_p(data->word & 0xff, SMBHSTDAT0(priv));
		outb_p((data->word & 0xff00) >> 8, SMBHSTDAT1(priv));
		outb_p(hstcmd, SMBHSTCMD(priv));
		read_write = I2C_SMBUS_READ;
		xact = I801_PROC_CALL;
		break;
	default:
		pci_err(priv->pci_dev, "Unsupported transaction %d\n", command);
		return -EOPNOTSUPP;
	}

	ret = i801_transaction(priv, xact);
	if (ret || read_write == I2C_SMBUS_WRITE)
		return ret;

	switch (command) {
	case I2C_SMBUS_BYTE:
	case I2C_SMBUS_BYTE_DATA:
		data->byte = inb_p(SMBHSTDAT0(priv));
		break;
	case I2C_SMBUS_WORD_DATA:
	case I2C_SMBUS_PROC_CALL:
		data->word = inb_p(SMBHSTDAT0(priv)) +
			     (inb_p(SMBHSTDAT1(priv)) << 8);
		break;
	}

	return 0;
}

static int i801_smbus_block_transaction(struct i801_priv *priv, union i2c_smbus_data *data,
					u8 addr, u8 hstcmd, char read_write, int command)
{
	if (read_write == I2C_SMBUS_READ && command == I2C_SMBUS_BLOCK_DATA)
		/* Mark block length as invalid */
		data->block[0] = SMBUS_LEN_SENTINEL;
	else if (data->block[0] < 1 || data->block[0] > I2C_SMBUS_BLOCK_MAX)
		return -EPROTO;

	if (command == I2C_SMBUS_BLOCK_PROC_CALL)
		/* Needs to be flagged as write transaction */
		i801_set_hstadd(priv, addr, I2C_SMBUS_WRITE);
	else
		i801_set_hstadd(priv, addr, read_write);
	outb_p(hstcmd, SMBHSTCMD(priv));

	if (priv->features & FEATURE_BLOCK_BUFFER)
		return i801_block_transaction_by_block(priv, data, read_write, command);
	else
		return i801_block_transaction_byte_by_byte(priv, data, read_write, command);
}

static int i801_i2c_block_transaction(struct i801_priv *priv, union i2c_smbus_data *data,
				      u8 addr, u8 hstcmd, char read_write, int command)
{
	int result;
	u8 hostc;

	if (data->block[0] < 1 || data->block[0] > I2C_SMBUS_BLOCK_MAX)
		return -EPROTO;
	/*
	 * NB: page 240 of ICH5 datasheet shows that the R/#W bit should be cleared here,
	 * even when reading. However if SPD Write Disable is set (Lynx Point and later),
	 * the read will fail if we don't set the R/#W bit.
	 */
	i801_set_hstadd(priv, addr,
			priv->original_hstcfg & SMBHSTCFG_SPD_WD ? read_write : I2C_SMBUS_WRITE);

	/* NB: page 240 of ICH5 datasheet shows that DATA1 is the cmd field when reading */
	if (read_write == I2C_SMBUS_READ)
		outb_p(hstcmd, SMBHSTDAT1(priv));
	else
		outb_p(hstcmd, SMBHSTCMD(priv));

	if (read_write == I2C_SMBUS_WRITE) {
		/* set I2C_EN bit in configuration register */
		pci_read_config_byte(priv->pci_dev, SMBHSTCFG, &hostc);
		pci_write_config_byte(priv->pci_dev, SMBHSTCFG, hostc | SMBHSTCFG_I2C_EN);
	} else if (!(priv->features & FEATURE_I2C_BLOCK_READ)) {
		pci_err(priv->pci_dev, "I2C block read is unsupported!\n");
		return -EOPNOTSUPP;
	}

	/* Block buffer isn't supported for I2C block transactions */
	result = i801_block_transaction_byte_by_byte(priv, data, read_write, command);

	/* restore saved configuration register value */
	if (read_write == I2C_SMBUS_WRITE)
		pci_write_config_byte(priv->pci_dev, SMBHSTCFG, hostc);

	return result;
}

/* Return negative errno on error. */
static s32 i801_access(struct i2c_adapter *adap, u16 addr,
		       unsigned short flags, char read_write, u8 command,
		       int size, union i2c_smbus_data *data)
{
	int hwpec, ret;
	struct i801_priv *priv = i2c_get_adapdata(adap);

	if (priv->acpi_reserved)
		return -EBUSY;

	pm_runtime_get_sync(&priv->pci_dev->dev);

	ret = i801_check_pre(priv);
	if (ret)
		goto out;

	hwpec = (priv->features & FEATURE_SMBUS_PEC) && (flags & I2C_CLIENT_PEC)
		&& size != I2C_SMBUS_QUICK
		&& size != I2C_SMBUS_I2C_BLOCK_DATA;

	if (hwpec)	/* enable/disable hardware PEC */
		outb_p(inb_p(SMBAUXCTL(priv)) | SMBAUXCTL_CRC, SMBAUXCTL(priv));
	else
		outb_p(inb_p(SMBAUXCTL(priv)) & (~SMBAUXCTL_CRC),
		       SMBAUXCTL(priv));

	if (size == I2C_SMBUS_BLOCK_DATA || size == I2C_SMBUS_BLOCK_PROC_CALL)
		ret = i801_smbus_block_transaction(priv, data, addr, command, read_write, size);
	else if (size == I2C_SMBUS_I2C_BLOCK_DATA)
		ret = i801_i2c_block_transaction(priv, data, addr, command, read_write, size);
	else
		ret = i801_simple_transaction(priv, data, addr, command, read_write, size);

	ret = i801_check_post(priv, ret);

	/* Some BIOSes don't like it when PEC is enabled at reboot or resume
	 * time, so we forcibly disable it after every transaction.
	 */
	if (hwpec)
		outb_p(inb_p(SMBAUXCTL(priv)) & ~SMBAUXCTL_CRC, SMBAUXCTL(priv));
out:
	/*
	 * Unlock the SMBus device for use by BIOS/ACPI,
	 * and clear status flags if not done already.
	 */
	outb_p(SMBHSTSTS_INUSE_STS | STATUS_FLAGS, SMBHSTSTS(priv));

	pm_runtime_mark_last_busy(&priv->pci_dev->dev);
	pm_runtime_put_autosuspend(&priv->pci_dev->dev);
	return ret;
}


static u32 i801_func(struct i2c_adapter *adapter)
{
	struct i801_priv *priv = i2c_get_adapdata(adapter);

	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_SMBUS_PROC_CALL |
	       I2C_FUNC_SMBUS_BLOCK_DATA | I2C_FUNC_SMBUS_WRITE_I2C_BLOCK |
	       ((priv->features & FEATURE_SMBUS_PEC) ? I2C_FUNC_SMBUS_PEC : 0) |
	       ((priv->features & FEATURE_BLOCK_PROC) ?
		I2C_FUNC_SMBUS_BLOCK_PROC_CALL : 0) |
	       ((priv->features & FEATURE_I2C_BLOCK_READ) ?
		I2C_FUNC_SMBUS_READ_I2C_BLOCK : 0) |
	       ((priv->features & FEATURE_HOST_NOTIFY) ?
		I2C_FUNC_SMBUS_HOST_NOTIFY : 0);
}

static void i801_enable_host_notify(struct i2c_adapter *adapter)
{
	struct i801_priv *priv = i2c_get_adapdata(adapter);

	if (!(priv->features & FEATURE_HOST_NOTIFY))
		return;

	/*
	 * Enable host notify interrupt and block the generation of interrupt
	 * from the SMB_ALERT signal because the driver does not support
	 * SMBus Alert.
	 */
	outb_p(SMBSLVCMD_HST_NTFY_INTREN | SMBSLVCMD_SMBALERT_DISABLE |
	       priv->original_slvcmd, SMBSLVCMD(priv));

	/* clear Host Notify bit to allow a new notification */
	outb_p(SMBSLVSTS_HST_NTFY_STS, SMBSLVSTS(priv));
}

static void i801_disable_host_notify(struct i801_priv *priv)
{
	if (!(priv->features & FEATURE_HOST_NOTIFY))
		return;

	outb_p(priv->original_slvcmd, SMBSLVCMD(priv));
}

static const struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer	= i801_access,
	.functionality	= i801_func,
};

#define FEATURES_ICH4	(FEATURE_SMBUS_PEC | FEATURE_BLOCK_BUFFER | \
			 FEATURE_HOST_NOTIFY)
#define FEATURES_ICH5	(FEATURES_ICH4 | FEATURE_BLOCK_PROC | \
			 FEATURE_I2C_BLOCK_READ | FEATURE_IRQ)

static const struct pci_device_id i801_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, 82801AA_3,			0)				 },
	{ PCI_DEVICE_DATA(INTEL, 82801AB_3,			0)				 },
	{ PCI_DEVICE_DATA(INTEL, 82801BA_2,			0)				 },
	{ PCI_DEVICE_DATA(INTEL, 82801CA_3,			FEATURE_HOST_NOTIFY)		 },
	{ PCI_DEVICE_DATA(INTEL, 82801DB_3,			FEATURES_ICH4)			 },
	{ PCI_DEVICE_DATA(INTEL, 82801EB_3,			FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, ESB_4,				FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, ICH6_16,			FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, ICH7_17,			FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, ESB2_17,			FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, ICH8_5,			FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, ICH9_6,			FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, EP80579_1,			FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, ICH10_4,			FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, ICH10_5,			FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, 5_3400_SERIES_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, COUGARPOINT_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, PATSBURG_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, PATSBURG_SMBUS_IDF0,		FEATURES_ICH5 | FEATURE_IDF)	 },
	{ PCI_DEVICE_DATA(INTEL, PATSBURG_SMBUS_IDF1,		FEATURES_ICH5 | FEATURE_IDF)	 },
	{ PCI_DEVICE_DATA(INTEL, PATSBURG_SMBUS_IDF2,		FEATURES_ICH5 | FEATURE_IDF)	 },
	{ PCI_DEVICE_DATA(INTEL, DH89XXCC_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, PANTHERPOINT_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, LYNXPOINT_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, LYNXPOINT_LP_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, AVOTON_SMBUS,			FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, WELLSBURG_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, WELLSBURG_SMBUS_MS0,		FEATURES_ICH5 | FEATURE_IDF)	 },
	{ PCI_DEVICE_DATA(INTEL, WELLSBURG_SMBUS_MS1,		FEATURES_ICH5 | FEATURE_IDF)	 },
	{ PCI_DEVICE_DATA(INTEL, WELLSBURG_SMBUS_MS2,		FEATURES_ICH5 | FEATURE_IDF)	 },
	{ PCI_DEVICE_DATA(INTEL, COLETOCREEK_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, GEMINILAKE_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, WILDCATPOINT_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, WILDCATPOINT_LP_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, BAYTRAIL_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, BRASWELL_SMBUS,		FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, SUNRISEPOINT_H_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_SPT) },
	{ PCI_DEVICE_DATA(INTEL, SUNRISEPOINT_LP_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_SPT) },
	{ PCI_DEVICE_DATA(INTEL, CDF_SMBUS,			FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, DNV_SMBUS,			FEATURES_ICH5 | FEATURE_TCO_SPT) },
	{ PCI_DEVICE_DATA(INTEL, EBG_SMBUS,			FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, BROXTON_SMBUS,			FEATURES_ICH5)			 },
	{ PCI_DEVICE_DATA(INTEL, LEWISBURG_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_SPT) },
	{ PCI_DEVICE_DATA(INTEL, LEWISBURG_SSKU_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_SPT) },
	{ PCI_DEVICE_DATA(INTEL, KABYLAKE_PCH_H_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_SPT) },
	{ PCI_DEVICE_DATA(INTEL, CANNONLAKE_H_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, CANNONLAKE_LP_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, ICELAKE_LP_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, ICELAKE_N_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, COMETLAKE_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, COMETLAKE_H_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, COMETLAKE_V_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_SPT) },
	{ PCI_DEVICE_DATA(INTEL, ELKHART_LAKE_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, TIGERLAKE_LP_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, TIGERLAKE_H_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, JASPER_LAKE_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, ALDER_LAKE_S_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, ALDER_LAKE_P_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, ALDER_LAKE_M_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, RAPTOR_LAKE_S_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, METEOR_LAKE_P_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, METEOR_LAKE_SOC_S_SMBUS,	FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, METEOR_LAKE_PCH_S_SMBUS,	FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ PCI_DEVICE_DATA(INTEL, BIRCH_STREAM_SMBUS,		FEATURES_ICH5 | FEATURE_TCO_CNL) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, i801_ids);

#if defined CONFIG_X86 && defined CONFIG_DMI
static unsigned char apanel_addr __ro_after_init;

/* Scan the system ROM for the signature "FJKEYINF" */
static __init const void __iomem *bios_signature(const void __iomem *bios)
{
	ssize_t offset;
	const unsigned char signature[] = "FJKEYINF";

	for (offset = 0; offset < 0x10000; offset += 0x10) {
		if (check_signature(bios + offset, signature,
				    sizeof(signature)-1))
			return bios + offset;
	}
	return NULL;
}

static void __init input_apanel_init(void)
{
	void __iomem *bios;
	const void __iomem *p;

	bios = ioremap(0xF0000, 0x10000); /* Can't fail */
	p = bios_signature(bios);
	if (p) {
		/* just use the first address */
		apanel_addr = readb(p + 8 + 3) >> 1;
	}
	iounmap(bios);
}

struct dmi_onboard_device_info {
	const char *name;
	u8 type;
	unsigned short i2c_addr;
	const char *i2c_type;
};

static const struct dmi_onboard_device_info dmi_devices[] = {
	{ "Syleus", DMI_DEV_TYPE_OTHER, 0x73, "fscsyl" },
	{ "Hermes", DMI_DEV_TYPE_OTHER, 0x73, "fscher" },
	{ "Hades",  DMI_DEV_TYPE_OTHER, 0x73, "fschds" },
};

static void dmi_check_onboard_device(u8 type, const char *name,
				     struct i2c_adapter *adap)
{
	int i;
	struct i2c_board_info info;

	for (i = 0; i < ARRAY_SIZE(dmi_devices); i++) {
		/* & ~0x80, ignore enabled/disabled bit */
		if ((type & ~0x80) != dmi_devices[i].type)
			continue;
		if (strcasecmp(name, dmi_devices[i].name))
			continue;

		memset(&info, 0, sizeof(struct i2c_board_info));
		info.addr = dmi_devices[i].i2c_addr;
		strscpy(info.type, dmi_devices[i].i2c_type, I2C_NAME_SIZE);
		i2c_new_client_device(adap, &info);
		break;
	}
}

/* We use our own function to check for onboard devices instead of
   dmi_find_device() as some buggy BIOS's have the devices we are interested
   in marked as disabled */
static void dmi_check_onboard_devices(const struct dmi_header *dm, void *adap)
{
	int i, count;

	if (dm->type != DMI_ENTRY_ONBOARD_DEVICE)
		return;

	count = (dm->length - sizeof(struct dmi_header)) / 2;
	for (i = 0; i < count; i++) {
		const u8 *d = (char *)(dm + 1) + (i * 2);
		const char *name = ((char *) dm) + dm->length;
		u8 type = d[0];
		u8 s = d[1];

		if (!s)
			continue;
		s--;
		while (s > 0 && name[0]) {
			name += strlen(name) + 1;
			s--;
		}
		if (name[0] == 0) /* Bogus string reference */
			continue;

		dmi_check_onboard_device(type, name, adap);
	}
}

/* NOTE: Keep this list in sync with drivers/platform/x86/dell-smo8800.c */
static const char *const acpi_smo8800_ids[] = {
	"SMO8800",
	"SMO8801",
	"SMO8810",
	"SMO8811",
	"SMO8820",
	"SMO8821",
	"SMO8830",
	"SMO8831",
};

static acpi_status check_acpi_smo88xx_device(acpi_handle obj_handle,
					     u32 nesting_level,
					     void *context,
					     void **return_value)
{
	struct acpi_device_info *info;
	acpi_status status;
	char *hid;
	int i;

	status = acpi_get_object_info(obj_handle, &info);
	if (ACPI_FAILURE(status))
		return AE_OK;

	if (!(info->valid & ACPI_VALID_HID))
		goto smo88xx_not_found;

	hid = info->hardware_id.string;
	if (!hid)
		goto smo88xx_not_found;

	i = match_string(acpi_smo8800_ids, ARRAY_SIZE(acpi_smo8800_ids), hid);
	if (i < 0)
		goto smo88xx_not_found;

	kfree(info);

	*return_value = NULL;
	return AE_CTRL_TERMINATE;

smo88xx_not_found:
	kfree(info);
	return AE_OK;
}

static bool is_dell_system_with_lis3lv02d(void)
{
	void *err = ERR_PTR(-ENOENT);

	if (!dmi_match(DMI_SYS_VENDOR, "Dell Inc."))
		return false;

	/*
	 * Check that ACPI device SMO88xx is present and is functioning.
	 * Function acpi_get_devices() already filters all ACPI devices
	 * which are not present or are not functioning.
	 * ACPI device SMO88xx represents our ST microelectronics lis3lv02d
	 * accelerometer but unfortunately ACPI does not provide any other
	 * information (like I2C address).
	 */
	acpi_get_devices(NULL, check_acpi_smo88xx_device, NULL, &err);

	return !IS_ERR(err);
}

/*
 * Accelerometer's I2C address is not specified in DMI nor ACPI,
 * so it is needed to define mapping table based on DMI product names.
 */
static const struct {
	const char *dmi_product_name;
	unsigned short i2c_addr;
} dell_lis3lv02d_devices[] = {
	/*
	 * Dell platform team told us that these Latitude devices have
	 * ST microelectronics accelerometer at I2C address 0x29.
	 */
	{ "Latitude E5250",     0x29 },
	{ "Latitude E5450",     0x29 },
	{ "Latitude E5550",     0x29 },
	{ "Latitude E6440",     0x29 },
	{ "Latitude E6440 ATG", 0x29 },
	{ "Latitude E6540",     0x29 },
	/*
	 * Additional individual entries were added after verification.
	 */
	{ "Latitude 5480",      0x29 },
	{ "Precision 3540",     0x29 },
	{ "Vostro V131",        0x1d },
	{ "Vostro 5568",        0x29 },
	{ "XPS 15 7590",        0x29 },
};

static void register_dell_lis3lv02d_i2c_device(struct i801_priv *priv)
{
	struct i2c_board_info info;
	const char *dmi_product_name;
	int i;

	dmi_product_name = dmi_get_system_info(DMI_PRODUCT_NAME);
	for (i = 0; i < ARRAY_SIZE(dell_lis3lv02d_devices); ++i) {
		if (strcmp(dmi_product_name,
			   dell_lis3lv02d_devices[i].dmi_product_name) == 0)
			break;
	}

	if (i == ARRAY_SIZE(dell_lis3lv02d_devices)) {
		dev_warn(&priv->pci_dev->dev,
			 "Accelerometer lis3lv02d is present on SMBus but its"
			 " address is unknown, skipping registration\n");
		return;
	}

	memset(&info, 0, sizeof(struct i2c_board_info));
	info.addr = dell_lis3lv02d_devices[i].i2c_addr;
	strscpy(info.type, "lis3lv02d", I2C_NAME_SIZE);
	i2c_new_client_device(&priv->adapter, &info);
}

/* Register optional slaves */
static void i801_probe_optional_slaves(struct i801_priv *priv)
{
	/* Only register slaves on main SMBus channel */
	if (priv->features & FEATURE_IDF)
		return;

	if (apanel_addr) {
		struct i2c_board_info info = {
			.addr = apanel_addr,
			.type = "fujitsu_apanel",
		};

		i2c_new_client_device(&priv->adapter, &info);
	}

	if (dmi_name_in_vendors("FUJITSU"))
		dmi_walk(dmi_check_onboard_devices, &priv->adapter);

	if (is_dell_system_with_lis3lv02d())
		register_dell_lis3lv02d_i2c_device(priv);

	/* Instantiate SPD EEPROMs unless the SMBus is multiplexed */
#ifdef CONFIG_I2C_I801_MUX
	if (!priv->mux_pdev)
#endif
		i2c_register_spd(&priv->adapter);
}
#else
static void __init input_apanel_init(void) {}
static void i801_probe_optional_slaves(struct i801_priv *priv) {}
#endif	/* CONFIG_X86 && CONFIG_DMI */

#ifdef CONFIG_I2C_I801_MUX
static struct i801_mux_config i801_mux_config_asus_z8_d12 = {
	.gpio_chip = "gpio_ich",
	.values = { 0x02, 0x03 },
	.n_values = 2,
	.gpios = { 52, 53 },
	.n_gpios = 2,
};

static struct i801_mux_config i801_mux_config_asus_z8_d18 = {
	.gpio_chip = "gpio_ich",
	.values = { 0x02, 0x03, 0x01 },
	.n_values = 3,
	.gpios = { 52, 53 },
	.n_gpios = 2,
};

static const struct dmi_system_id mux_dmi_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "Z8NA-D6(C)"),
		},
		.driver_data = &i801_mux_config_asus_z8_d12,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "Z8P(N)E-D12(X)"),
		},
		.driver_data = &i801_mux_config_asus_z8_d12,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "Z8NH-D12"),
		},
		.driver_data = &i801_mux_config_asus_z8_d12,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "Z8PH-D12/IFB"),
		},
		.driver_data = &i801_mux_config_asus_z8_d12,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "Z8NR-D12"),
		},
		.driver_data = &i801_mux_config_asus_z8_d12,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "Z8P(N)H-D12"),
		},
		.driver_data = &i801_mux_config_asus_z8_d12,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "Z8PG-D18"),
		},
		.driver_data = &i801_mux_config_asus_z8_d18,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "Z8PE-D18"),
		},
		.driver_data = &i801_mux_config_asus_z8_d18,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "Z8PS-D12"),
		},
		.driver_data = &i801_mux_config_asus_z8_d12,
	},
	{ }
};

static int i801_notifier_call(struct notifier_block *nb, unsigned long action,
			      void *data)
{
	struct i801_priv *priv = container_of(nb, struct i801_priv, mux_notifier_block);
	struct device *dev = data;

	if (action != BUS_NOTIFY_ADD_DEVICE ||
	    dev->type != &i2c_adapter_type ||
	    i2c_root_adapter(dev) != &priv->adapter)
		return NOTIFY_DONE;

	/* Call i2c_register_spd for muxed child segments */
	i2c_register_spd(to_i2c_adapter(dev));

	return NOTIFY_OK;
}

/* Setup multiplexing if needed */
static void i801_add_mux(struct i801_priv *priv)
{
	struct device *dev = &priv->adapter.dev;
	const struct i801_mux_config *mux_config;
	struct i2c_mux_gpio_platform_data gpio_data;
	struct gpiod_lookup_table *lookup;
	const struct dmi_system_id *id;
	int i;

	id = dmi_first_match(mux_dmi_table);
	if (!id)
		return;

	mux_config = id->driver_data;

	/* Prepare the platform data */
	memset(&gpio_data, 0, sizeof(struct i2c_mux_gpio_platform_data));
	gpio_data.parent = priv->adapter.nr;
	gpio_data.values = mux_config->values;
	gpio_data.n_values = mux_config->n_values;
	gpio_data.idle = I2C_MUX_GPIO_NO_IDLE;

	/* Register GPIO descriptor lookup table */
	lookup = devm_kzalloc(dev,
			      struct_size(lookup, table, mux_config->n_gpios + 1),
			      GFP_KERNEL);
	if (!lookup)
		return;
	lookup->dev_id = "i2c-mux-gpio";
	for (i = 0; i < mux_config->n_gpios; i++)
		lookup->table[i] = GPIO_LOOKUP(mux_config->gpio_chip,
					       mux_config->gpios[i], "mux", 0);
	gpiod_add_lookup_table(lookup);

	priv->mux_notifier_block.notifier_call = i801_notifier_call;
	if (bus_register_notifier(&i2c_bus_type, &priv->mux_notifier_block))
		return;
	/*
	 * Register the mux device, we use PLATFORM_DEVID_NONE here
	 * because since we are referring to the GPIO chip by name we are
	 * anyways in deep trouble if there is more than one of these
	 * devices, and there should likely only be one platform controller
	 * hub.
	 */
	priv->mux_pdev = platform_device_register_data(dev, "i2c-mux-gpio",
				PLATFORM_DEVID_NONE, &gpio_data,
				sizeof(struct i2c_mux_gpio_platform_data));
	if (IS_ERR(priv->mux_pdev)) {
		gpiod_remove_lookup_table(lookup);
		devm_kfree(dev, lookup);
		dev_err(dev, "Failed to register i2c-mux-gpio device\n");
	} else {
		priv->lookup = lookup;
	}
}

static void i801_del_mux(struct i801_priv *priv)
{
	bus_unregister_notifier(&i2c_bus_type, &priv->mux_notifier_block);
	platform_device_unregister(priv->mux_pdev);
	gpiod_remove_lookup_table(priv->lookup);
}
#else
static inline void i801_add_mux(struct i801_priv *priv) { }
static inline void i801_del_mux(struct i801_priv *priv) { }
#endif

static struct platform_device *
i801_add_tco_spt(struct pci_dev *pci_dev, struct resource *tco_res)
{
	static const struct itco_wdt_platform_data pldata = {
		.name = "Intel PCH",
		.version = 4,
	};
	struct resource *res;
	int ret;

	/*
	 * We must access the NO_REBOOT bit over the Primary to Sideband
	 * (P2SB) bridge.
	 */

	res = &tco_res[1];
	ret = p2sb_bar(pci_dev->bus, 0, res);
	if (ret)
		return ERR_PTR(ret);

	if (pci_dev->device == PCI_DEVICE_ID_INTEL_DNV_SMBUS)
		res->start += SBREG_SMBCTRL_DNV;
	else
		res->start += SBREG_SMBCTRL;

	res->end = res->start + 3;

	return platform_device_register_resndata(&pci_dev->dev, "iTCO_wdt", -1,
					tco_res, 2, &pldata, sizeof(pldata));
}

static struct platform_device *
i801_add_tco_cnl(struct pci_dev *pci_dev, struct resource *tco_res)
{
	static const struct itco_wdt_platform_data pldata = {
		.name = "Intel PCH",
		.version = 6,
	};

	return platform_device_register_resndata(&pci_dev->dev, "iTCO_wdt", -1,
						 tco_res, 1, &pldata, sizeof(pldata));
}

static void i801_add_tco(struct i801_priv *priv)
{
	struct pci_dev *pci_dev = priv->pci_dev;
	struct resource tco_res[2], *res;
	u32 tco_base, tco_ctl;

	/* If we have ACPI based watchdog use that instead */
	if (acpi_has_watchdog())
		return;

	if (!(priv->features & (FEATURE_TCO_SPT | FEATURE_TCO_CNL)))
		return;

	pci_read_config_dword(pci_dev, TCOBASE, &tco_base);
	pci_read_config_dword(pci_dev, TCOCTL, &tco_ctl);
	if (!(tco_ctl & TCOCTL_EN))
		return;

	memset(tco_res, 0, sizeof(tco_res));
	/*
	 * Always populate the main iTCO IO resource here. The second entry
	 * for NO_REBOOT MMIO is filled by the SPT specific function.
	 */
	res = &tco_res[0];
	res->start = tco_base & ~1;
	res->end = res->start + 32 - 1;
	res->flags = IORESOURCE_IO;

	if (priv->features & FEATURE_TCO_CNL)
		priv->tco_pdev = i801_add_tco_cnl(pci_dev, tco_res);
	else
		priv->tco_pdev = i801_add_tco_spt(pci_dev, tco_res);

	if (IS_ERR(priv->tco_pdev))
		dev_warn(&pci_dev->dev, "failed to create iTCO device\n");
}

#ifdef CONFIG_ACPI
static bool i801_acpi_is_smbus_ioport(const struct i801_priv *priv,
				      acpi_physical_address address)
{
	return address >= priv->smba &&
	       address <= pci_resource_end(priv->pci_dev, SMBBAR);
}

static acpi_status
i801_acpi_io_handler(u32 function, acpi_physical_address address, u32 bits,
		     u64 *value, void *handler_context, void *region_context)
{
	struct i801_priv *priv = handler_context;
	struct pci_dev *pdev = priv->pci_dev;
	acpi_status status;

	/*
	 * Once BIOS AML code touches the OpRegion we warn and inhibit any
	 * further access from the driver itself. This device is now owned
	 * by the system firmware.
	 */
	i2c_lock_bus(&priv->adapter, I2C_LOCK_SEGMENT);

	if (!priv->acpi_reserved && i801_acpi_is_smbus_ioport(priv, address)) {
		priv->acpi_reserved = true;

		dev_warn(&pdev->dev, "BIOS is accessing SMBus registers\n");
		dev_warn(&pdev->dev, "Driver SMBus register access inhibited\n");

		/*
		 * BIOS is accessing the host controller so prevent it from
		 * suspending automatically from now on.
		 */
		pm_runtime_get_sync(&pdev->dev);
	}

	if ((function & ACPI_IO_MASK) == ACPI_READ)
		status = acpi_os_read_port(address, (u32 *)value, bits);
	else
		status = acpi_os_write_port(address, (u32)*value, bits);

	i2c_unlock_bus(&priv->adapter, I2C_LOCK_SEGMENT);

	return status;
}

static int i801_acpi_probe(struct i801_priv *priv)
{
	acpi_handle ah = ACPI_HANDLE(&priv->pci_dev->dev);
	acpi_status status;

	status = acpi_install_address_space_handler(ah, ACPI_ADR_SPACE_SYSTEM_IO,
						    i801_acpi_io_handler, NULL, priv);
	if (ACPI_SUCCESS(status))
		return 0;

	return acpi_check_resource_conflict(&priv->pci_dev->resource[SMBBAR]);
}

static void i801_acpi_remove(struct i801_priv *priv)
{
	acpi_handle ah = ACPI_HANDLE(&priv->pci_dev->dev);

	acpi_remove_address_space_handler(ah, ACPI_ADR_SPACE_SYSTEM_IO, i801_acpi_io_handler);
}
#else
static inline int i801_acpi_probe(struct i801_priv *priv) { return 0; }
static inline void i801_acpi_remove(struct i801_priv *priv) { }
#endif

static void i801_setup_hstcfg(struct i801_priv *priv)
{
	unsigned char hstcfg = priv->original_hstcfg;

	hstcfg &= ~SMBHSTCFG_I2C_EN;	/* SMBus timing */
	hstcfg |= SMBHSTCFG_HST_EN;
	pci_write_config_byte(priv->pci_dev, SMBHSTCFG, hstcfg);
}

static void i801_restore_regs(struct i801_priv *priv)
{
	outb_p(priv->original_hstcnt, SMBHSTCNT(priv));
	pci_write_config_byte(priv->pci_dev, SMBHSTCFG, priv->original_hstcfg);
}

static int i801_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int err, i;
	struct i801_priv *priv;

	priv = devm_kzalloc(&dev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_adapdata(&priv->adapter, priv);
	priv->adapter.owner = THIS_MODULE;
	priv->adapter.class = I2C_CLASS_HWMON;
	priv->adapter.algo = &smbus_algorithm;
	priv->adapter.dev.parent = &dev->dev;
	acpi_use_parent_companion(&priv->adapter.dev);
	priv->adapter.retries = 3;

	priv->pci_dev = dev;
	priv->features = id->driver_data;

	/* Disable features on user request */
	for (i = 0; i < ARRAY_SIZE(i801_feature_names); i++) {
		if (priv->features & disable_features & (1 << i))
			dev_notice(&dev->dev, "%s disabled by user\n",
				   i801_feature_names[i]);
	}
	priv->features &= ~disable_features;

	/* The block process call uses block buffer mode */
	if (!(priv->features & FEATURE_BLOCK_BUFFER))
		priv->features &= ~FEATURE_BLOCK_PROC;

	err = pcim_enable_device(dev);
	if (err) {
		dev_err(&dev->dev, "Failed to enable SMBus PCI device (%d)\n",
			err);
		return err;
	}
	pcim_pin_device(dev);

	/* Determine the address of the SMBus area */
	priv->smba = pci_resource_start(dev, SMBBAR);
	if (!priv->smba) {
		dev_err(&dev->dev,
			"SMBus base address uninitialized, upgrade BIOS\n");
		return -ENODEV;
	}

	if (i801_acpi_probe(priv))
		return -ENODEV;

	err = pcim_iomap_regions(dev, 1 << SMBBAR, DRV_NAME);
	if (err) {
		dev_err(&dev->dev,
			"Failed to request SMBus region 0x%lx-0x%Lx\n",
			priv->smba,
			(unsigned long long)pci_resource_end(dev, SMBBAR));
		i801_acpi_remove(priv);
		return err;
	}

	pci_read_config_byte(priv->pci_dev, SMBHSTCFG, &priv->original_hstcfg);
	i801_setup_hstcfg(priv);
	if (!(priv->original_hstcfg & SMBHSTCFG_HST_EN))
		dev_info(&dev->dev, "Enabling SMBus device\n");

	if (priv->original_hstcfg & SMBHSTCFG_SMB_SMI_EN) {
		dev_dbg(&dev->dev, "SMBus using interrupt SMI#\n");
		/* Disable SMBus interrupt feature if SMBus using SMI# */
		priv->features &= ~FEATURE_IRQ;
	}
	if (priv->original_hstcfg & SMBHSTCFG_SPD_WD)
		dev_info(&dev->dev, "SPD Write Disable is set\n");

	/* Clear special mode bits */
	if (priv->features & (FEATURE_SMBUS_PEC | FEATURE_BLOCK_BUFFER))
		outb_p(inb_p(SMBAUXCTL(priv)) &
		       ~(SMBAUXCTL_CRC | SMBAUXCTL_E32B), SMBAUXCTL(priv));

	/* Default timeout in interrupt mode: 200 ms */
	priv->adapter.timeout = HZ / 5;

	if (dev->irq == IRQ_NOTCONNECTED)
		priv->features &= ~FEATURE_IRQ;

	if (priv->features & FEATURE_IRQ) {
		u16 pcists;

		/* Complain if an interrupt is already pending */
		pci_read_config_word(priv->pci_dev, PCI_STATUS, &pcists);
		if (pcists & PCI_STATUS_INTERRUPT)
			dev_warn(&dev->dev, "An interrupt is pending!\n");
	}

	if (priv->features & FEATURE_IRQ) {
		init_completion(&priv->done);

		err = devm_request_irq(&dev->dev, dev->irq, i801_isr,
				       IRQF_SHARED, DRV_NAME, priv);
		if (err) {
			dev_err(&dev->dev, "Failed to allocate irq %d: %d\n",
				dev->irq, err);
			priv->features &= ~FEATURE_IRQ;
		}
	}
	dev_info(&dev->dev, "SMBus using %s\n",
		 priv->features & FEATURE_IRQ ? "PCI interrupt" : "polling");

	/* Host notification uses an interrupt */
	if (!(priv->features & FEATURE_IRQ))
		priv->features &= ~FEATURE_HOST_NOTIFY;

	/* Remember original Interrupt and Host Notify settings */
	priv->original_hstcnt = inb_p(SMBHSTCNT(priv)) & ~SMBHSTCNT_KILL;
	if (priv->features & FEATURE_HOST_NOTIFY)
		priv->original_slvcmd = inb_p(SMBSLVCMD(priv));

	i801_add_tco(priv);

	snprintf(priv->adapter.name, sizeof(priv->adapter.name),
		"SMBus I801 adapter at %04lx", priv->smba);
	err = i2c_add_adapter(&priv->adapter);
	if (err) {
		platform_device_unregister(priv->tco_pdev);
		i801_acpi_remove(priv);
		i801_restore_regs(priv);
		return err;
	}

	i801_enable_host_notify(&priv->adapter);

	/* We ignore errors - multiplexing is optional */
	i801_add_mux(priv);
	i801_probe_optional_slaves(priv);

	pci_set_drvdata(dev, priv);

	dev_pm_set_driver_flags(&dev->dev, DPM_FLAG_NO_DIRECT_COMPLETE);
	pm_runtime_set_autosuspend_delay(&dev->dev, 1000);
	pm_runtime_use_autosuspend(&dev->dev);
	pm_runtime_put_autosuspend(&dev->dev);
	pm_runtime_allow(&dev->dev);

	return 0;
}

static void i801_remove(struct pci_dev *dev)
{
	struct i801_priv *priv = pci_get_drvdata(dev);

	i801_disable_host_notify(priv);
	i801_del_mux(priv);
	i2c_del_adapter(&priv->adapter);
	i801_acpi_remove(priv);

	platform_device_unregister(priv->tco_pdev);

	/* if acpi_reserved is set then usage_count is incremented already */
	if (!priv->acpi_reserved)
		pm_runtime_get_noresume(&dev->dev);

	i801_restore_regs(priv);

	/*
	 * do not call pci_disable_device(dev) since it can cause hard hangs on
	 * some systems during power-off (eg. Fujitsu-Siemens Lifebook E8010)
	 */
}

static void i801_shutdown(struct pci_dev *dev)
{
	struct i801_priv *priv = pci_get_drvdata(dev);

	i801_disable_host_notify(priv);
	/* Restore config registers to avoid hard hang on some systems */
	i801_restore_regs(priv);
}

static int i801_suspend(struct device *dev)
{
	struct i801_priv *priv = dev_get_drvdata(dev);

	i2c_mark_adapter_suspended(&priv->adapter);
	i801_restore_regs(priv);

	return 0;
}

static int i801_resume(struct device *dev)
{
	struct i801_priv *priv = dev_get_drvdata(dev);

	i801_setup_hstcfg(priv);
	i801_enable_host_notify(&priv->adapter);
	i2c_mark_adapter_resumed(&priv->adapter);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(i801_pm_ops, i801_suspend, i801_resume);

static struct pci_driver i801_driver = {
	.name		= DRV_NAME,
	.id_table	= i801_ids,
	.probe		= i801_probe,
	.remove		= i801_remove,
	.shutdown	= i801_shutdown,
	.driver		= {
		.pm	= pm_sleep_ptr(&i801_pm_ops),
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static int __init i2c_i801_init(struct pci_driver *drv)
{
	if (dmi_name_in_vendors("FUJITSU"))
		input_apanel_init();
	return pci_register_driver(drv);
}

MODULE_AUTHOR("Mark D. Studebaker <mdsxyz123@yahoo.com>");
MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("I801 SMBus driver");
MODULE_LICENSE("GPL");

module_driver(i801_driver, i2c_i801_init, pci_unregister_driver);
