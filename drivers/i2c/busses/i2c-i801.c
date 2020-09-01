/*
    Copyright (c) 1998 - 2002  Frodo Looijaard <frodol@dds.nl>,
    Philip Edelbrock <phil@netroedge.com>, and Mark D. Studebaker
    <mdsxyz123@yahoo.com>
    Copyright (C) 2007 - 2014  Jean Delvare <jdelvare@suse.de>
    Copyright (C) 2010         Intel Corporation,
                               David Woodhouse <dwmw2@infradead.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
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
 * Broxton (SOC)		0x5ad4	32	hard	yes	yes	yes
 * Lewisburg (PCH)		0xa1a3	32	hard	yes	yes	yes
 * Lewisburg Supersku (PCH)	0xa223	32	hard	yes	yes	yes
 * Kaby Lake PCH-H (PCH)	0xa2a3	32	hard	yes	yes	yes
 * Gemini Lake (SOC)		0x31d4	32	hard	yes	yes	yes
 * Cannon Lake-H (PCH)		0xa323	32	hard	yes	yes	yes
 * Cannon Lake-LP (PCH)		0x9da3	32	hard	yes	yes	yes
 * Cedar Fork (PCH)		0x18df	32	hard	yes	yes	yes
 * Ice Lake-LP (PCH)		0x34a3	32	hard	yes	yes	yes
 * Comet Lake (PCH)		0x02a3	32	hard	yes	yes	yes
 *
 * Features supported by this driver:
 * Software PEC				no
 * Hardware PEC				yes
 * Block buffer				yes
 * Block process call transaction	no
 * I2C block read transaction		yes (doesn't use the block buffer)
 * Slave mode				no
 * SMBus Host Notify			yes
 * Interrupt processing			yes
 *
 * See the file Documentation/i2c/busses/i2c-i801 for details.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c-smbus.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/platform_data/itco_wdt.h>
#include <linux/pm_runtime.h>

#if IS_ENABLED(CONFIG_I2C_MUX_GPIO) && defined CONFIG_DMI
#include <linux/gpio.h>
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
#define SMBPCICTL	0x004
#define SMBPCISTS	0x006
#define SMBHSTCFG	0x040
#define TCOBASE		0x050
#define TCOCTL		0x054

#define ACPIBASE		0x040
#define ACPIBASE_SMI_OFF	0x030
#define ACPICTRL		0x044
#define ACPICTRL_EN		0x080

#define SBREG_BAR		0x10
#define SBREG_SMBCTRL		0xc6000c
#define SBREG_SMBCTRL_DNV	0xcf000c

/* Host status bits for SMBPCISTS */
#define SMBPCISTS_INTS		BIT(3)

/* Control bits for SMBPCICTL */
#define SMBPCICTL_INTDIS	BIT(10)

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

/* Other settings */
#define MAX_RETRIES		400

/* I801 command constants */
#define I801_QUICK		0x00
#define I801_BYTE		0x04
#define I801_BYTE_DATA		0x08
#define I801_WORD_DATA		0x0C
#define I801_PROC_CALL		0x10	/* unimplemented */
#define I801_BLOCK_DATA		0x14
#define I801_I2C_BLOCK_DATA	0x18	/* ICH5 and later */

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
#define SMBSLVCMD_HST_NTFY_INTREN	BIT(0)

#define STATUS_ERROR_FLAGS	(SMBHSTSTS_FAILED | SMBHSTSTS_BUS_ERR | \
				 SMBHSTSTS_DEV_ERR)

#define STATUS_FLAGS		(SMBHSTSTS_BYTE_DONE | SMBHSTSTS_INTR | \
				 STATUS_ERROR_FLAGS)

/* Older devices have their ID defined in <linux/pci_ids.h> */
#define PCI_DEVICE_ID_INTEL_BAYTRAIL_SMBUS		0x0f12
#define PCI_DEVICE_ID_INTEL_CDF_SMBUS			0x18df
#define PCI_DEVICE_ID_INTEL_DNV_SMBUS			0x19df
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
#define PCI_DEVICE_ID_INTEL_5_3400_SERIES_SMBUS		0x3b30
#define PCI_DEVICE_ID_INTEL_BROXTON_SMBUS		0x5ad4
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
#define PCI_DEVICE_ID_INTEL_SUNRISEPOINT_H_SMBUS	0xa123
#define PCI_DEVICE_ID_INTEL_LEWISBURG_SMBUS		0xa1a3
#define PCI_DEVICE_ID_INTEL_LEWISBURG_SSKU_SMBUS	0xa223
#define PCI_DEVICE_ID_INTEL_KABYLAKE_PCH_H_SMBUS	0xa2a3
#define PCI_DEVICE_ID_INTEL_CANNONLAKE_H_SMBUS		0xa323
#define PCI_DEVICE_ID_INTEL_COMETLAKE_SMBUS		0x02a3

struct i801_mux_config {
	char *gpio_chip;
	unsigned values[3];
	int n_values;
	unsigned classes[3];
	unsigned gpios[2];		/* Relative to gpio_chip->base */
	int n_gpios;
};

struct i801_priv {
	struct i2c_adapter adapter;
	unsigned long smba;
	unsigned char original_hstcfg;
	unsigned char original_slvcmd;
	struct pci_dev *pci_dev;
	unsigned int features;

	/* isr processing */
	wait_queue_head_t waitq;
	u8 status;

	/* Command state used by isr for byte-by-byte block transactions */
	u8 cmd;
	bool is_read;
	int count;
	int len;
	u8 *data;

#if IS_ENABLED(CONFIG_I2C_MUX_GPIO) && defined CONFIG_DMI
	const struct i801_mux_config *mux_drvdata;
	struct platform_device *mux_pdev;
#endif
	struct platform_device *tco_pdev;

	/*
	 * If set to true the host controller registers are reserved for
	 * ACPI AML use. Protected by acpi_lock.
	 */
	bool acpi_reserved;
	struct mutex acpi_lock;
};

#define FEATURE_SMBUS_PEC	BIT(0)
#define FEATURE_BLOCK_BUFFER	BIT(1)
#define FEATURE_BLOCK_PROC	BIT(2)
#define FEATURE_I2C_BLOCK_READ	BIT(3)
#define FEATURE_IRQ		BIT(4)
#define FEATURE_HOST_NOTIFY	BIT(5)
/* Not really a feature, but it's convenient to handle it as such */
#define FEATURE_IDF		BIT(15)
#define FEATURE_TCO		BIT(16)

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

/* Make sure the SMBus host is ready to start transmitting.
   Return 0 if it is, -EBUSY if it is not. */
static int i801_check_pre(struct i801_priv *priv)
{
	int status;

	status = inb_p(SMBHSTSTS(priv));
	if (status & SMBHSTSTS_HOST_BUSY) {
		dev_err(&priv->pci_dev->dev, "SMBus is busy, can't use it!\n");
		return -EBUSY;
	}

	status &= STATUS_FLAGS;
	if (status) {
		dev_dbg(&priv->pci_dev->dev, "Clearing status flags (%02x)\n",
			status);
		outb_p(status, SMBHSTSTS(priv));
		status = inb_p(SMBHSTSTS(priv)) & STATUS_FLAGS;
		if (status) {
			dev_err(&priv->pci_dev->dev,
				"Failed clearing status flags (%02x)\n",
				status);
			return -EBUSY;
		}
	}

	/*
	 * Clear CRC status if needed.
	 * During normal operation, i801_check_post() takes care
	 * of it after every operation.  We do it here only in case
	 * the hardware was already in this state when the driver
	 * started.
	 */
	if (priv->features & FEATURE_SMBUS_PEC) {
		status = inb_p(SMBAUXSTS(priv)) & SMBAUXSTS_CRCE;
		if (status) {
			dev_dbg(&priv->pci_dev->dev,
				"Clearing aux status flags (%02x)\n", status);
			outb_p(status, SMBAUXSTS(priv));
			status = inb_p(SMBAUXSTS(priv)) & SMBAUXSTS_CRCE;
			if (status) {
				dev_err(&priv->pci_dev->dev,
					"Failed clearing aux status flags (%02x)\n",
					status);
				return -EBUSY;
			}
		}
	}

	return 0;
}

/*
 * Convert the status register to an error code, and clear it.
 * Note that status only contains the bits we want to clear, not the
 * actual register value.
 */
static int i801_check_post(struct i801_priv *priv, int status)
{
	int result = 0;

	/*
	 * If the SMBus is still busy, we give up
	 * Note: This timeout condition only happens when using polling
	 * transactions.  For interrupt operation, NAK/timeout is indicated by
	 * DEV_ERR.
	 */
	if (unlikely(status < 0)) {
		dev_err(&priv->pci_dev->dev, "Transaction timeout\n");
		/* try to stop the current command */
		dev_dbg(&priv->pci_dev->dev, "Terminating the current operation\n");
		outb_p(inb_p(SMBHSTCNT(priv)) | SMBHSTCNT_KILL,
		       SMBHSTCNT(priv));
		usleep_range(1000, 2000);
		outb_p(inb_p(SMBHSTCNT(priv)) & (~SMBHSTCNT_KILL),
		       SMBHSTCNT(priv));

		/* Check if it worked */
		status = inb_p(SMBHSTSTS(priv));
		if ((status & SMBHSTSTS_HOST_BUSY) ||
		    !(status & SMBHSTSTS_FAILED))
			dev_err(&priv->pci_dev->dev,
				"Failed terminating the transaction\n");
		outb_p(STATUS_FLAGS, SMBHSTSTS(priv));
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
		if ((priv->features & FEATURE_SMBUS_PEC) &&
		    (inb_p(SMBAUXSTS(priv)) & SMBAUXSTS_CRCE)) {
			outb_p(SMBAUXSTS_CRCE, SMBAUXSTS(priv));
			result = -EBADMSG;
			dev_dbg(&priv->pci_dev->dev, "PEC error\n");
		} else {
			result = -ENXIO;
			dev_dbg(&priv->pci_dev->dev, "No response\n");
		}
	}
	if (status & SMBHSTSTS_BUS_ERR) {
		result = -EAGAIN;
		dev_dbg(&priv->pci_dev->dev, "Lost arbitration\n");
	}

	/* Clear status flags except BYTE_DONE, to be cleared by caller */
	outb_p(status, SMBHSTSTS(priv));

	return result;
}

/* Wait for BUSY being cleared and either INTR or an error flag being set */
static int i801_wait_intr(struct i801_priv *priv)
{
	int timeout = 0;
	int status;

	/* We will always wait for a fraction of a second! */
	do {
		usleep_range(250, 500);
		status = inb_p(SMBHSTSTS(priv));
	} while (((status & SMBHSTSTS_HOST_BUSY) ||
		  !(status & (STATUS_ERROR_FLAGS | SMBHSTSTS_INTR))) &&
		 (timeout++ < MAX_RETRIES));

	if (timeout > MAX_RETRIES) {
		dev_dbg(&priv->pci_dev->dev, "INTR Timeout!\n");
		return -ETIMEDOUT;
	}
	return status & (STATUS_ERROR_FLAGS | SMBHSTSTS_INTR);
}

/* Wait for either BYTE_DONE or an error flag being set */
static int i801_wait_byte_done(struct i801_priv *priv)
{
	int timeout = 0;
	int status;

	/* We will always wait for a fraction of a second! */
	do {
		usleep_range(250, 500);
		status = inb_p(SMBHSTSTS(priv));
	} while (!(status & (STATUS_ERROR_FLAGS | SMBHSTSTS_BYTE_DONE)) &&
		 (timeout++ < MAX_RETRIES));

	if (timeout > MAX_RETRIES) {
		dev_dbg(&priv->pci_dev->dev, "BYTE_DONE Timeout!\n");
		return -ETIMEDOUT;
	}
	return status & STATUS_ERROR_FLAGS;
}

static int i801_transaction(struct i801_priv *priv, int xact)
{
	int status;
	int result;
	const struct i2c_adapter *adap = &priv->adapter;

	result = i801_check_pre(priv);
	if (result < 0)
		return result;

	if (priv->features & FEATURE_IRQ) {
		outb_p(xact | SMBHSTCNT_INTREN | SMBHSTCNT_START,
		       SMBHSTCNT(priv));
		result = wait_event_timeout(priv->waitq,
					    (status = priv->status),
					    adap->timeout);
		if (!result) {
			status = -ETIMEDOUT;
			dev_warn(&priv->pci_dev->dev,
				 "Timeout waiting for interrupt!\n");
		}
		priv->status = 0;
		return i801_check_post(priv, status);
	}

	/* the current contents of SMBHSTCNT can be overwritten, since PEC,
	 * SMBSCMD are passed in xact */
	outb_p(xact | SMBHSTCNT_START, SMBHSTCNT(priv));

	status = i801_wait_intr(priv);
	return i801_check_post(priv, status);
}

static int i801_block_transaction_by_block(struct i801_priv *priv,
					   union i2c_smbus_data *data,
					   char read_write, int hwpec)
{
	int i, len;
	int status;

	inb_p(SMBHSTCNT(priv)); /* reset the data buffer index */

	/* Use 32-byte buffer to process this transaction */
	if (read_write == I2C_SMBUS_WRITE) {
		len = data->block[0];
		outb_p(len, SMBHSTDAT0(priv));
		for (i = 0; i < len; i++)
			outb_p(data->block[i+1], SMBBLKDAT(priv));
	}

	status = i801_transaction(priv, I801_BLOCK_DATA |
				  (hwpec ? SMBHSTCNT_PEC_EN : 0));
	if (status)
		return status;

	if (read_write == I2C_SMBUS_READ) {
		len = inb_p(SMBHSTDAT0(priv));
		if (len < 1 || len > I2C_SMBUS_BLOCK_MAX)
			return -EPROTO;

		data->block[0] = len;
		for (i = 0; i < len; i++)
			data->block[i + 1] = inb_p(SMBBLKDAT(priv));
	}
	return 0;
}

static void i801_isr_byte_done(struct i801_priv *priv)
{
	if (priv->is_read) {
		/* For SMBus block reads, length is received with first byte */
		if (((priv->cmd & 0x1c) == I801_BLOCK_DATA) &&
		    (priv->count == 0)) {
			priv->len = inb_p(SMBHSTDAT0(priv));
			if (priv->len < 1 || priv->len > I2C_SMBUS_BLOCK_MAX) {
				dev_err(&priv->pci_dev->dev,
					"Illegal SMBus block read size %d\n",
					priv->len);
				/* FIXME: Recover */
				priv->len = I2C_SMBUS_BLOCK_MAX;
			} else {
				dev_dbg(&priv->pci_dev->dev,
					"SMBus block read size is %d\n",
					priv->len);
			}
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

	/* Clear BYTE_DONE to continue with next byte */
	outb_p(SMBHSTSTS_BYTE_DONE, SMBHSTSTS(priv));
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
 *    When any of these occur, update ->status and wake up the waitq.
 *    ->status must be cleared before kicking off the next transaction.
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
	pci_read_config_word(priv->pci_dev, SMBPCISTS, &pcists);
	if (!(pcists & SMBPCISTS_INTS))
		return IRQ_NONE;

	if (priv->features & FEATURE_HOST_NOTIFY) {
		status = inb_p(SMBSLVSTS(priv));
		if (status & SMBSLVSTS_HST_NTFY_STS)
			return i801_host_notify_isr(priv);
	}

	status = inb_p(SMBHSTSTS(priv));
	if (status & SMBHSTSTS_BYTE_DONE)
		i801_isr_byte_done(priv);

	/*
	 * Clear irq sources and report transaction result.
	 * ->status must be cleared before the next transaction is started.
	 */
	status &= SMBHSTSTS_INTR | STATUS_ERROR_FLAGS;
	if (status) {
		outb_p(status, SMBHSTSTS(priv));
		priv->status = status;
		wake_up(&priv->waitq);
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
					       char read_write, int command,
					       int hwpec)
{
	int i, len;
	int smbcmd;
	int status;
	int result;
	const struct i2c_adapter *adap = &priv->adapter;

	result = i801_check_pre(priv);
	if (result < 0)
		return result;

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

		outb_p(priv->cmd | SMBHSTCNT_START, SMBHSTCNT(priv));
		result = wait_event_timeout(priv->waitq,
					    (status = priv->status),
					    adap->timeout);
		if (!result) {
			status = -ETIMEDOUT;
			dev_warn(&priv->pci_dev->dev,
				 "Timeout waiting for interrupt!\n");
		}
		priv->status = 0;
		return i801_check_post(priv, status);
	}

	for (i = 1; i <= len; i++) {
		if (i == len && read_write == I2C_SMBUS_READ)
			smbcmd |= SMBHSTCNT_LAST_BYTE;
		outb_p(smbcmd, SMBHSTCNT(priv));

		if (i == 1)
			outb_p(inb(SMBHSTCNT(priv)) | SMBHSTCNT_START,
			       SMBHSTCNT(priv));

		status = i801_wait_byte_done(priv);
		if (status)
			goto exit;

		if (i == 1 && read_write == I2C_SMBUS_READ
		 && command != I2C_SMBUS_I2C_BLOCK_DATA) {
			len = inb_p(SMBHSTDAT0(priv));
			if (len < 1 || len > I2C_SMBUS_BLOCK_MAX) {
				dev_err(&priv->pci_dev->dev,
					"Illegal SMBus block read size %d\n",
					len);
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

		/* Retrieve/store value in SMBBLKDAT */
		if (read_write == I2C_SMBUS_READ)
			data->block[i] = inb_p(SMBBLKDAT(priv));
		if (read_write == I2C_SMBUS_WRITE && i+1 <= len)
			outb_p(data->block[i+1], SMBBLKDAT(priv));

		/* signals SMBBLKDAT ready */
		outb_p(SMBHSTSTS_BYTE_DONE, SMBHSTSTS(priv));
	}

	status = i801_wait_intr(priv);
exit:
	return i801_check_post(priv, status);
}

static int i801_set_block_buffer_mode(struct i801_priv *priv)
{
	outb_p(inb_p(SMBAUXCTL(priv)) | SMBAUXCTL_E32B, SMBAUXCTL(priv));
	if ((inb_p(SMBAUXCTL(priv)) & SMBAUXCTL_E32B) == 0)
		return -EIO;
	return 0;
}

/* Block transaction function */
static int i801_block_transaction(struct i801_priv *priv,
				  union i2c_smbus_data *data, char read_write,
				  int command, int hwpec)
{
	int result = 0;
	unsigned char hostc;

	if (command == I2C_SMBUS_I2C_BLOCK_DATA) {
		if (read_write == I2C_SMBUS_WRITE) {
			/* set I2C_EN bit in configuration register */
			pci_read_config_byte(priv->pci_dev, SMBHSTCFG, &hostc);
			pci_write_config_byte(priv->pci_dev, SMBHSTCFG,
					      hostc | SMBHSTCFG_I2C_EN);
		} else if (!(priv->features & FEATURE_I2C_BLOCK_READ)) {
			dev_err(&priv->pci_dev->dev,
				"I2C block read is unsupported!\n");
			return -EOPNOTSUPP;
		}
	}

	if (read_write == I2C_SMBUS_WRITE
	 || command == I2C_SMBUS_I2C_BLOCK_DATA) {
		if (data->block[0] < 1)
			data->block[0] = 1;
		if (data->block[0] > I2C_SMBUS_BLOCK_MAX)
			data->block[0] = I2C_SMBUS_BLOCK_MAX;
	} else {
		data->block[0] = 32;	/* max for SMBus block reads */
	}

	/* Experience has shown that the block buffer can only be used for
	   SMBus (not I2C) block transactions, even though the datasheet
	   doesn't mention this limitation. */
	if ((priv->features & FEATURE_BLOCK_BUFFER)
	 && command != I2C_SMBUS_I2C_BLOCK_DATA
	 && i801_set_block_buffer_mode(priv) == 0)
		result = i801_block_transaction_by_block(priv, data,
							 read_write, hwpec);
	else
		result = i801_block_transaction_byte_by_byte(priv, data,
							     read_write,
							     command, hwpec);

	if (command == I2C_SMBUS_I2C_BLOCK_DATA
	 && read_write == I2C_SMBUS_WRITE) {
		/* restore saved configuration register value */
		pci_write_config_byte(priv->pci_dev, SMBHSTCFG, hostc);
	}
	return result;
}

/* Return negative errno on error. */
static s32 i801_access(struct i2c_adapter *adap, u16 addr,
		       unsigned short flags, char read_write, u8 command,
		       int size, union i2c_smbus_data *data)
{
	int hwpec;
	int block = 0;
	int ret = 0, xact = 0;
	struct i801_priv *priv = i2c_get_adapdata(adap);

	mutex_lock(&priv->acpi_lock);
	if (priv->acpi_reserved) {
		mutex_unlock(&priv->acpi_lock);
		return -EBUSY;
	}

	pm_runtime_get_sync(&priv->pci_dev->dev);

	hwpec = (priv->features & FEATURE_SMBUS_PEC) && (flags & I2C_CLIENT_PEC)
		&& size != I2C_SMBUS_QUICK
		&& size != I2C_SMBUS_I2C_BLOCK_DATA;

	switch (size) {
	case I2C_SMBUS_QUICK:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD(priv));
		xact = I801_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD(priv));
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(command, SMBHSTCMD(priv));
		xact = I801_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD(priv));
		outb_p(command, SMBHSTCMD(priv));
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(data->byte, SMBHSTDAT0(priv));
		xact = I801_BYTE_DATA;
		break;
	case I2C_SMBUS_WORD_DATA:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD(priv));
		outb_p(command, SMBHSTCMD(priv));
		if (read_write == I2C_SMBUS_WRITE) {
			outb_p(data->word & 0xff, SMBHSTDAT0(priv));
			outb_p((data->word & 0xff00) >> 8, SMBHSTDAT1(priv));
		}
		xact = I801_WORD_DATA;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD(priv));
		outb_p(command, SMBHSTCMD(priv));
		block = 1;
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		/*
		 * NB: page 240 of ICH5 datasheet shows that the R/#W
		 * bit should be cleared here, even when reading.
		 * However if SPD Write Disable is set (Lynx Point and later),
		 * the read will fail if we don't set the R/#W bit.
		 */
		outb_p(((addr & 0x7f) << 1) |
		       ((priv->original_hstcfg & SMBHSTCFG_SPD_WD) ?
			(read_write & 0x01) : 0),
		       SMBHSTADD(priv));
		if (read_write == I2C_SMBUS_READ) {
			/* NB: page 240 of ICH5 datasheet also shows
			 * that DATA1 is the cmd field when reading */
			outb_p(command, SMBHSTDAT1(priv));
		} else
			outb_p(command, SMBHSTCMD(priv));
		block = 1;
		break;
	default:
		dev_err(&priv->pci_dev->dev, "Unsupported transaction %d\n",
			size);
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (hwpec)	/* enable/disable hardware PEC */
		outb_p(inb_p(SMBAUXCTL(priv)) | SMBAUXCTL_CRC, SMBAUXCTL(priv));
	else
		outb_p(inb_p(SMBAUXCTL(priv)) & (~SMBAUXCTL_CRC),
		       SMBAUXCTL(priv));

	if (block)
		ret = i801_block_transaction(priv, data, read_write, size,
					     hwpec);
	else
		ret = i801_transaction(priv, xact);

	/* Some BIOSes don't like it when PEC is enabled at reboot or resume
	   time, so we forcibly disable it after every transaction. Turn off
	   E32B for the same reason. */
	if (hwpec || block)
		outb_p(inb_p(SMBAUXCTL(priv)) &
		       ~(SMBAUXCTL_CRC | SMBAUXCTL_E32B), SMBAUXCTL(priv));

	if (block)
		goto out;
	if (ret)
		goto out;
	if ((read_write == I2C_SMBUS_WRITE) || (xact == I801_QUICK))
		goto out;

	switch (xact & 0x7f) {
	case I801_BYTE:	/* Result put in SMBHSTDAT0 */
	case I801_BYTE_DATA:
		data->byte = inb_p(SMBHSTDAT0(priv));
		break;
	case I801_WORD_DATA:
		data->word = inb_p(SMBHSTDAT0(priv)) +
			     (inb_p(SMBHSTDAT1(priv)) << 8);
		break;
	}

out:
	pm_runtime_mark_last_busy(&priv->pci_dev->dev);
	pm_runtime_put_autosuspend(&priv->pci_dev->dev);
	mutex_unlock(&priv->acpi_lock);
	return ret;
}


static u32 i801_func(struct i2c_adapter *adapter)
{
	struct i801_priv *priv = i2c_get_adapdata(adapter);

	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_SMBUS_BLOCK_DATA | I2C_FUNC_SMBUS_WRITE_I2C_BLOCK |
	       ((priv->features & FEATURE_SMBUS_PEC) ? I2C_FUNC_SMBUS_PEC : 0) |
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

	if (!(SMBSLVCMD_HST_NTFY_INTREN & priv->original_slvcmd))
		outb_p(SMBSLVCMD_HST_NTFY_INTREN | priv->original_slvcmd,
		       SMBSLVCMD(priv));

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

static const struct pci_device_id i801_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AA_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AB_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_2) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801EB_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB_4) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH6_16) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_17) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB2_17) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH8_5) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH9_6) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_EP80579_1) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH10_4) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH10_5) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_5_3400_SERIES_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_COUGARPOINT_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS_IDF0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS_IDF1) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS_IDF2) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_DH89XXCC_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_PANTHERPOINT_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_LYNXPOINT_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_LYNXPOINT_LP_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_AVOTON_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_WELLSBURG_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_WELLSBURG_SMBUS_MS0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_WELLSBURG_SMBUS_MS1) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_WELLSBURG_SMBUS_MS2) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_COLETOCREEK_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_GEMINILAKE_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_WILDCATPOINT_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_WILDCATPOINT_LP_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_BAYTRAIL_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_BRASWELL_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_SUNRISEPOINT_H_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_SUNRISEPOINT_LP_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_CDF_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_DNV_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_BROXTON_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_LEWISBURG_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_LEWISBURG_SSKU_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_KABYLAKE_PCH_H_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_CANNONLAKE_H_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_CANNONLAKE_LP_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICELAKE_LP_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_COMETLAKE_SMBUS) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, i801_ids);

#if defined CONFIG_X86 && defined CONFIG_DMI
static unsigned char apanel_addr;

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
		strlcpy(info.type, dmi_devices[i].i2c_type, I2C_NAME_SIZE);
		i2c_new_device(adap, &info);
		break;
	}
}

/* We use our own function to check for onboard devices instead of
   dmi_find_device() as some buggy BIOS's have the devices we are interested
   in marked as disabled */
static void dmi_check_onboard_devices(const struct dmi_header *dm, void *adap)
{
	int i, count;

	if (dm->type != 10)
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

/* Register optional slaves */
static void i801_probe_optional_slaves(struct i801_priv *priv)
{
	/* Only register slaves on main SMBus channel */
	if (priv->features & FEATURE_IDF)
		return;

	if (apanel_addr) {
		struct i2c_board_info info;

		memset(&info, 0, sizeof(struct i2c_board_info));
		info.addr = apanel_addr;
		strlcpy(info.type, "fujitsu_apanel", I2C_NAME_SIZE);
		i2c_new_device(&priv->adapter, &info);
	}

	if (dmi_name_in_vendors("FUJITSU"))
		dmi_walk(dmi_check_onboard_devices, &priv->adapter);
}
#else
static void __init input_apanel_init(void) {}
static void i801_probe_optional_slaves(struct i801_priv *priv) {}
#endif	/* CONFIG_X86 && CONFIG_DMI */

#if IS_ENABLED(CONFIG_I2C_MUX_GPIO) && defined CONFIG_DMI
static struct i801_mux_config i801_mux_config_asus_z8_d12 = {
	.gpio_chip = "gpio_ich",
	.values = { 0x02, 0x03 },
	.n_values = 2,
	.classes = { I2C_CLASS_SPD, I2C_CLASS_SPD },
	.gpios = { 52, 53 },
	.n_gpios = 2,
};

static struct i801_mux_config i801_mux_config_asus_z8_d18 = {
	.gpio_chip = "gpio_ich",
	.values = { 0x02, 0x03, 0x01 },
	.n_values = 3,
	.classes = { I2C_CLASS_SPD, I2C_CLASS_SPD, I2C_CLASS_SPD },
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

/* Setup multiplexing if needed */
static int i801_add_mux(struct i801_priv *priv)
{
	struct device *dev = &priv->adapter.dev;
	const struct i801_mux_config *mux_config;
	struct i2c_mux_gpio_platform_data gpio_data;
	int err;

	if (!priv->mux_drvdata)
		return 0;
	mux_config = priv->mux_drvdata;

	/* Prepare the platform data */
	memset(&gpio_data, 0, sizeof(struct i2c_mux_gpio_platform_data));
	gpio_data.parent = priv->adapter.nr;
	gpio_data.values = mux_config->values;
	gpio_data.n_values = mux_config->n_values;
	gpio_data.classes = mux_config->classes;
	gpio_data.gpio_chip = mux_config->gpio_chip;
	gpio_data.gpios = mux_config->gpios;
	gpio_data.n_gpios = mux_config->n_gpios;
	gpio_data.idle = I2C_MUX_GPIO_NO_IDLE;

	/* Register the mux device */
	priv->mux_pdev = platform_device_register_data(dev, "i2c-mux-gpio",
				PLATFORM_DEVID_AUTO, &gpio_data,
				sizeof(struct i2c_mux_gpio_platform_data));
	if (IS_ERR(priv->mux_pdev)) {
		err = PTR_ERR(priv->mux_pdev);
		priv->mux_pdev = NULL;
		dev_err(dev, "Failed to register i2c-mux-gpio device\n");
		return err;
	}

	return 0;
}

static void i801_del_mux(struct i801_priv *priv)
{
	if (priv->mux_pdev)
		platform_device_unregister(priv->mux_pdev);
}

static unsigned int i801_get_adapter_class(struct i801_priv *priv)
{
	const struct dmi_system_id *id;
	const struct i801_mux_config *mux_config;
	unsigned int class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	int i;

	id = dmi_first_match(mux_dmi_table);
	if (id) {
		/* Remove branch classes from trunk */
		mux_config = id->driver_data;
		for (i = 0; i < mux_config->n_values; i++)
			class &= ~mux_config->classes[i];

		/* Remember for later */
		priv->mux_drvdata = mux_config;
	}

	return class;
}
#else
static inline int i801_add_mux(struct i801_priv *priv) { return 0; }
static inline void i801_del_mux(struct i801_priv *priv) { }

static inline unsigned int i801_get_adapter_class(struct i801_priv *priv)
{
	return I2C_CLASS_HWMON | I2C_CLASS_SPD;
}
#endif

static const struct itco_wdt_platform_data tco_platform_data = {
	.name = "Intel PCH",
	.version = 4,
};

static DEFINE_SPINLOCK(p2sb_spinlock);

static void i801_add_tco(struct i801_priv *priv)
{
	struct pci_dev *pci_dev = priv->pci_dev;
	struct resource tco_res[3], *res;
	struct platform_device *pdev;
	unsigned int devfn;
	u32 tco_base, tco_ctl;
	u32 base_addr, ctrl_val;
	u64 base64_addr;
	u8 hidden;

	if (!(priv->features & FEATURE_TCO))
		return;

	pci_read_config_dword(pci_dev, TCOBASE, &tco_base);
	pci_read_config_dword(pci_dev, TCOCTL, &tco_ctl);
	if (!(tco_ctl & TCOCTL_EN))
		return;

	memset(tco_res, 0, sizeof(tco_res));

	res = &tco_res[ICH_RES_IO_TCO];
	res->start = tco_base & ~1;
	res->end = res->start + 32 - 1;
	res->flags = IORESOURCE_IO;

	/*
	 * Power Management registers.
	 */
	devfn = PCI_DEVFN(PCI_SLOT(pci_dev->devfn), 2);
	pci_bus_read_config_dword(pci_dev->bus, devfn, ACPIBASE, &base_addr);

	res = &tco_res[ICH_RES_IO_SMI];
	res->start = (base_addr & ~1) + ACPIBASE_SMI_OFF;
	res->end = res->start + 3;
	res->flags = IORESOURCE_IO;

	/*
	 * Enable the ACPI I/O space.
	 */
	pci_bus_read_config_dword(pci_dev->bus, devfn, ACPICTRL, &ctrl_val);
	ctrl_val |= ACPICTRL_EN;
	pci_bus_write_config_dword(pci_dev->bus, devfn, ACPICTRL, ctrl_val);

	/*
	 * We must access the NO_REBOOT bit over the Primary to Sideband
	 * bridge (P2SB). The BIOS prevents the P2SB device from being
	 * enumerated by the PCI subsystem, so we need to unhide/hide it
	 * to lookup the P2SB BAR.
	 */
	spin_lock(&p2sb_spinlock);

	devfn = PCI_DEVFN(PCI_SLOT(pci_dev->devfn), 1);

	/* Unhide the P2SB device, if it is hidden */
	pci_bus_read_config_byte(pci_dev->bus, devfn, 0xe1, &hidden);
	if (hidden)
		pci_bus_write_config_byte(pci_dev->bus, devfn, 0xe1, 0x0);

	pci_bus_read_config_dword(pci_dev->bus, devfn, SBREG_BAR, &base_addr);
	base64_addr = base_addr & 0xfffffff0;

	pci_bus_read_config_dword(pci_dev->bus, devfn, SBREG_BAR + 0x4, &base_addr);
	base64_addr |= (u64)base_addr << 32;

	/* Hide the P2SB device, if it was hidden before */
	if (hidden)
		pci_bus_write_config_byte(pci_dev->bus, devfn, 0xe1, hidden);
	spin_unlock(&p2sb_spinlock);

	res = &tco_res[ICH_RES_MEM_OFF];
	if (pci_dev->device == PCI_DEVICE_ID_INTEL_DNV_SMBUS)
		res->start = (resource_size_t)base64_addr + SBREG_SMBCTRL_DNV;
	else
		res->start = (resource_size_t)base64_addr + SBREG_SMBCTRL;

	res->end = res->start + 3;
	res->flags = IORESOURCE_MEM;

	pdev = platform_device_register_resndata(&pci_dev->dev, "iTCO_wdt", -1,
						 tco_res, 3, &tco_platform_data,
						 sizeof(tco_platform_data));
	if (IS_ERR(pdev)) {
		dev_warn(&pci_dev->dev, "failed to create iTCO device\n");
		return;
	}

	priv->tco_pdev = pdev;
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
	mutex_lock(&priv->acpi_lock);

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

	mutex_unlock(&priv->acpi_lock);

	return status;
}

static int i801_acpi_probe(struct i801_priv *priv)
{
	struct acpi_device *adev;
	acpi_status status;

	adev = ACPI_COMPANION(&priv->pci_dev->dev);
	if (adev) {
		status = acpi_install_address_space_handler(adev->handle,
				ACPI_ADR_SPACE_SYSTEM_IO, i801_acpi_io_handler,
				NULL, priv);
		if (ACPI_SUCCESS(status))
			return 0;
	}

	return acpi_check_resource_conflict(&priv->pci_dev->resource[SMBBAR]);
}

static void i801_acpi_remove(struct i801_priv *priv)
{
	struct acpi_device *adev;

	adev = ACPI_COMPANION(&priv->pci_dev->dev);
	if (!adev)
		return;

	acpi_remove_address_space_handler(adev->handle,
		ACPI_ADR_SPACE_SYSTEM_IO, i801_acpi_io_handler);

	mutex_lock(&priv->acpi_lock);
	if (priv->acpi_reserved)
		pm_runtime_put(&priv->pci_dev->dev);
	mutex_unlock(&priv->acpi_lock);
}
#else
static inline int i801_acpi_probe(struct i801_priv *priv) { return 0; }
static inline void i801_acpi_remove(struct i801_priv *priv) { }
#endif

static unsigned char i801_setup_hstcfg(struct i801_priv *priv)
{
	unsigned char hstcfg = priv->original_hstcfg;

	hstcfg &= ~SMBHSTCFG_I2C_EN;	/* SMBus timing */
	hstcfg |= SMBHSTCFG_HST_EN;
	pci_write_config_byte(priv->pci_dev, SMBHSTCFG, hstcfg);
	return hstcfg;
}

static int i801_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	unsigned char temp;
	int err, i;
	struct i801_priv *priv;

	priv = devm_kzalloc(&dev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_adapdata(&priv->adapter, priv);
	priv->adapter.owner = THIS_MODULE;
	priv->adapter.class = i801_get_adapter_class(priv);
	priv->adapter.algo = &smbus_algorithm;
	priv->adapter.dev.parent = &dev->dev;
	ACPI_COMPANION_SET(&priv->adapter.dev, ACPI_COMPANION(&dev->dev));
	priv->adapter.retries = 3;
	mutex_init(&priv->acpi_lock);

	priv->pci_dev = dev;
	switch (dev->device) {
	case PCI_DEVICE_ID_INTEL_SUNRISEPOINT_H_SMBUS:
	case PCI_DEVICE_ID_INTEL_SUNRISEPOINT_LP_SMBUS:
	case PCI_DEVICE_ID_INTEL_CANNONLAKE_H_SMBUS:
	case PCI_DEVICE_ID_INTEL_CANNONLAKE_LP_SMBUS:
	case PCI_DEVICE_ID_INTEL_LEWISBURG_SMBUS:
	case PCI_DEVICE_ID_INTEL_LEWISBURG_SSKU_SMBUS:
	case PCI_DEVICE_ID_INTEL_CDF_SMBUS:
	case PCI_DEVICE_ID_INTEL_DNV_SMBUS:
	case PCI_DEVICE_ID_INTEL_KABYLAKE_PCH_H_SMBUS:
	case PCI_DEVICE_ID_INTEL_ICELAKE_LP_SMBUS:
	case PCI_DEVICE_ID_INTEL_COMETLAKE_SMBUS:
		priv->features |= FEATURE_I2C_BLOCK_READ;
		priv->features |= FEATURE_IRQ;
		priv->features |= FEATURE_SMBUS_PEC;
		priv->features |= FEATURE_BLOCK_BUFFER;
		/* If we have ACPI based watchdog use that instead */
		if (!acpi_has_watchdog())
			priv->features |= FEATURE_TCO;
		priv->features |= FEATURE_HOST_NOTIFY;
		break;

	case PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS_IDF0:
	case PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS_IDF1:
	case PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS_IDF2:
	case PCI_DEVICE_ID_INTEL_WELLSBURG_SMBUS_MS0:
	case PCI_DEVICE_ID_INTEL_WELLSBURG_SMBUS_MS1:
	case PCI_DEVICE_ID_INTEL_WELLSBURG_SMBUS_MS2:
		priv->features |= FEATURE_IDF;
		/* fall through */
	default:
		priv->features |= FEATURE_I2C_BLOCK_READ;
		priv->features |= FEATURE_IRQ;
		/* fall through */
	case PCI_DEVICE_ID_INTEL_82801DB_3:
		priv->features |= FEATURE_SMBUS_PEC;
		priv->features |= FEATURE_BLOCK_BUFFER;
		/* fall through */
	case PCI_DEVICE_ID_INTEL_82801CA_3:
		priv->features |= FEATURE_HOST_NOTIFY;
		/* fall through */
	case PCI_DEVICE_ID_INTEL_82801BA_2:
	case PCI_DEVICE_ID_INTEL_82801AB_3:
	case PCI_DEVICE_ID_INTEL_82801AA_3:
		break;
	}

	/* Disable features on user request */
	for (i = 0; i < ARRAY_SIZE(i801_feature_names); i++) {
		if (priv->features & disable_features & (1 << i))
			dev_notice(&dev->dev, "%s disabled by user\n",
				   i801_feature_names[i]);
	}
	priv->features &= ~disable_features;

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

	err = pcim_iomap_regions(dev, 1 << SMBBAR,
				 dev_driver_string(&dev->dev));
	if (err) {
		dev_err(&dev->dev,
			"Failed to request SMBus region 0x%lx-0x%Lx\n",
			priv->smba,
			(unsigned long long)pci_resource_end(dev, SMBBAR));
		i801_acpi_remove(priv);
		return err;
	}

	pci_read_config_byte(priv->pci_dev, SMBHSTCFG, &priv->original_hstcfg);
	temp = i801_setup_hstcfg(priv);
	if (!(priv->original_hstcfg & SMBHSTCFG_HST_EN))
		dev_info(&dev->dev, "Enabling SMBus device\n");

	if (temp & SMBHSTCFG_SMB_SMI_EN) {
		dev_dbg(&dev->dev, "SMBus using interrupt SMI#\n");
		/* Disable SMBus interrupt feature if SMBus using SMI# */
		priv->features &= ~FEATURE_IRQ;
	}
	if (temp & SMBHSTCFG_SPD_WD)
		dev_info(&dev->dev, "SPD Write Disable is set\n");

	/* Clear special mode bits */
	if (priv->features & (FEATURE_SMBUS_PEC | FEATURE_BLOCK_BUFFER))
		outb_p(inb_p(SMBAUXCTL(priv)) &
		       ~(SMBAUXCTL_CRC | SMBAUXCTL_E32B), SMBAUXCTL(priv));

	/* Remember original Host Notify setting */
	if (priv->features & FEATURE_HOST_NOTIFY)
		priv->original_slvcmd = inb_p(SMBSLVCMD(priv));

	/* Default timeout in interrupt mode: 200 ms */
	priv->adapter.timeout = HZ / 5;

	if (dev->irq == IRQ_NOTCONNECTED)
		priv->features &= ~FEATURE_IRQ;

	if (priv->features & FEATURE_IRQ) {
		u16 pcictl, pcists;

		/* Complain if an interrupt is already pending */
		pci_read_config_word(priv->pci_dev, SMBPCISTS, &pcists);
		if (pcists & SMBPCISTS_INTS)
			dev_warn(&dev->dev, "An interrupt is pending!\n");

		/* Check if interrupts have been disabled */
		pci_read_config_word(priv->pci_dev, SMBPCICTL, &pcictl);
		if (pcictl & SMBPCICTL_INTDIS) {
			dev_info(&dev->dev, "Interrupts are disabled\n");
			priv->features &= ~FEATURE_IRQ;
		}
	}

	if (priv->features & FEATURE_IRQ) {
		init_waitqueue_head(&priv->waitq);

		err = devm_request_irq(&dev->dev, dev->irq, i801_isr,
				       IRQF_SHARED,
				       dev_driver_string(&dev->dev), priv);
		if (err) {
			dev_err(&dev->dev, "Failed to allocate irq %d: %d\n",
				dev->irq, err);
			priv->features &= ~FEATURE_IRQ;
		}
	}
	dev_info(&dev->dev, "SMBus using %s\n",
		 priv->features & FEATURE_IRQ ? "PCI interrupt" : "polling");

	i801_add_tco(priv);

	snprintf(priv->adapter.name, sizeof(priv->adapter.name),
		"SMBus I801 adapter at %04lx", priv->smba);
	err = i2c_add_adapter(&priv->adapter);
	if (err) {
		i801_acpi_remove(priv);
		return err;
	}

	i801_enable_host_notify(&priv->adapter);

	i801_probe_optional_slaves(priv);
	/* We ignore errors - multiplexing is optional */
	i801_add_mux(priv);

	pci_set_drvdata(dev, priv);

	pm_runtime_set_autosuspend_delay(&dev->dev, 1000);
	pm_runtime_use_autosuspend(&dev->dev);
	pm_runtime_put_autosuspend(&dev->dev);
	pm_runtime_allow(&dev->dev);

	return 0;
}

static void i801_remove(struct pci_dev *dev)
{
	struct i801_priv *priv = pci_get_drvdata(dev);

	pm_runtime_forbid(&dev->dev);
	pm_runtime_get_noresume(&dev->dev);

	i801_disable_host_notify(priv);
	i801_del_mux(priv);
	i2c_del_adapter(&priv->adapter);
	i801_acpi_remove(priv);
	pci_write_config_byte(dev, SMBHSTCFG, priv->original_hstcfg);

	platform_device_unregister(priv->tco_pdev);

	/*
	 * do not call pci_disable_device(dev) since it can cause hard hangs on
	 * some systems during power-off (eg. Fujitsu-Siemens Lifebook E8010)
	 */
}

static void i801_shutdown(struct pci_dev *dev)
{
	struct i801_priv *priv = pci_get_drvdata(dev);

	/* Restore config registers to avoid hard hang on some systems */
	i801_disable_host_notify(priv);
	pci_write_config_byte(dev, SMBHSTCFG, priv->original_hstcfg);
}

#ifdef CONFIG_PM_SLEEP
static int i801_suspend(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct i801_priv *priv = pci_get_drvdata(pci_dev);

	pci_write_config_byte(pci_dev, SMBHSTCFG, priv->original_hstcfg);
	return 0;
}

static int i801_resume(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct i801_priv *priv = pci_get_drvdata(pci_dev);

	i801_setup_hstcfg(priv);
	i801_enable_host_notify(&priv->adapter);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(i801_pm_ops, i801_suspend, i801_resume);

static struct pci_driver i801_driver = {
	.name		= "i801_smbus",
	.id_table	= i801_ids,
	.probe		= i801_probe,
	.remove		= i801_remove,
	.shutdown	= i801_shutdown,
	.driver		= {
		.pm	= &i801_pm_ops,
	},
};

static int __init i2c_i801_init(void)
{
	if (dmi_name_in_vendors("FUJITSU"))
		input_apanel_init();
	return pci_register_driver(&i801_driver);
}

static void __exit i2c_i801_exit(void)
{
	pci_unregister_driver(&i801_driver);
}

MODULE_AUTHOR("Mark D. Studebaker <mdsxyz123@yahoo.com>, Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("I801 SMBus driver");
MODULE_LICENSE("GPL");

module_init(i2c_i801_init);
module_exit(i2c_i801_exit);
