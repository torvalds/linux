// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm ICE (Inline Crypto Engine) support.
 *
 * Copyright (c) 2013-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2019, Google LLC
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <linux/firmware/qcom/qcom_scm.h>

#include <soc/qcom/ice.h>

#define AES_256_XTS_KEY_SIZE			64   /* for raw keys only */
#define QCOM_ICE_HWKM_WRAPPED_KEY_SIZE		100  /* assuming HWKM v2 */

/* QCOM ICE registers */

#define QCOM_ICE_REG_CONTROL			0x0000
#define QCOM_ICE_LEGACY_MODE_ENABLED		BIT(0)

#define QCOM_ICE_REG_VERSION			0x0008

#define QCOM_ICE_REG_FUSE_SETTING		0x0010
#define QCOM_ICE_FUSE_SETTING_MASK		BIT(0)
#define QCOM_ICE_FORCE_HW_KEY0_SETTING_MASK	BIT(1)
#define QCOM_ICE_FORCE_HW_KEY1_SETTING_MASK	BIT(2)

#define QCOM_ICE_REG_BIST_STATUS		0x0070
#define QCOM_ICE_BIST_STATUS_MASK		GENMASK(31, 28)

#define QCOM_ICE_REG_ADVANCED_CONTROL		0x1000

#define QCOM_ICE_REG_CRYPTOCFG_BASE		0x4040
#define QCOM_ICE_REG_CRYPTOCFG_SIZE		0x80
#define QCOM_ICE_REG_CRYPTOCFG(slot) (QCOM_ICE_REG_CRYPTOCFG_BASE + \
				      QCOM_ICE_REG_CRYPTOCFG_SIZE * (slot))
union crypto_cfg {
	__le32 regval;
	struct {
		u8 dusize;
		u8 capidx;
		u8 reserved;
#define QCOM_ICE_HWKM_CFG_ENABLE_VAL		BIT(7)
		u8 cfge;
	};
};

/* QCOM ICE HWKM (Hardware Key Manager) registers */

#define HWKM_OFFSET				0x8000

#define QCOM_ICE_REG_HWKM_TZ_KM_CTL		(HWKM_OFFSET + 0x1000)
#define QCOM_ICE_HWKM_DISABLE_CRC_CHECKS_VAL	(BIT(1) | BIT(2))

#define QCOM_ICE_REG_HWKM_TZ_KM_STATUS		(HWKM_OFFSET + 0x1004)
#define QCOM_ICE_HWKM_KT_CLEAR_DONE		BIT(0)
#define QCOM_ICE_HWKM_BOOT_CMD_LIST0_DONE	BIT(1)
#define QCOM_ICE_HWKM_BOOT_CMD_LIST1_DONE	BIT(2)
#define QCOM_ICE_HWKM_CRYPTO_BIST_DONE_V2	BIT(7)
#define QCOM_ICE_HWKM_BIST_DONE_V2		BIT(9)

#define QCOM_ICE_REG_HWKM_BANK0_BANKN_IRQ_STATUS (HWKM_OFFSET + 0x2008)
#define QCOM_ICE_HWKM_RSP_FIFO_CLEAR_VAL	BIT(3)

#define QCOM_ICE_REG_HWKM_BANK0_BBAC_0		(HWKM_OFFSET + 0x5000)
#define QCOM_ICE_REG_HWKM_BANK0_BBAC_1		(HWKM_OFFSET + 0x5004)
#define QCOM_ICE_REG_HWKM_BANK0_BBAC_2		(HWKM_OFFSET + 0x5008)
#define QCOM_ICE_REG_HWKM_BANK0_BBAC_3		(HWKM_OFFSET + 0x500C)
#define QCOM_ICE_REG_HWKM_BANK0_BBAC_4		(HWKM_OFFSET + 0x5010)

#define qcom_ice_writel(engine, val, reg)	\
	writel((val), (engine)->base + (reg))

#define qcom_ice_readl(engine, reg)	\
	readl((engine)->base + (reg))

static bool qcom_ice_use_wrapped_keys;
module_param_named(use_wrapped_keys, qcom_ice_use_wrapped_keys, bool, 0660);
MODULE_PARM_DESC(use_wrapped_keys,
		 "Support wrapped keys instead of raw keys, if available on the platform");

struct qcom_ice {
	struct device *dev;
	void __iomem *base;

	struct clk *core_clk;
	bool use_hwkm;
	bool hwkm_init_complete;
};

static bool qcom_ice_check_supported(struct qcom_ice *ice)
{
	u32 regval = qcom_ice_readl(ice, QCOM_ICE_REG_VERSION);
	struct device *dev = ice->dev;
	int major = FIELD_GET(GENMASK(31, 24), regval);
	int minor = FIELD_GET(GENMASK(23, 16), regval);
	int step = FIELD_GET(GENMASK(15, 0), regval);

	/* For now this driver only supports ICE version 3 and 4. */
	if (major != 3 && major != 4) {
		dev_warn(dev, "Unsupported ICE version: v%d.%d.%d\n",
			 major, minor, step);
		return false;
	}

	dev_info(dev, "Found QC Inline Crypto Engine (ICE) v%d.%d.%d\n",
		 major, minor, step);

	/* If fuses are blown, ICE might not work in the standard way. */
	regval = qcom_ice_readl(ice, QCOM_ICE_REG_FUSE_SETTING);
	if (regval & (QCOM_ICE_FUSE_SETTING_MASK |
		      QCOM_ICE_FORCE_HW_KEY0_SETTING_MASK |
		      QCOM_ICE_FORCE_HW_KEY1_SETTING_MASK)) {
		dev_warn(dev, "Fuses are blown; ICE is unusable!\n");
		return false;
	}

	/*
	 * Check for HWKM support and decide whether to use it or not.  ICE
	 * v3.2.1 and later have HWKM v2.  ICE v3.2.0 has HWKM v1.  Earlier ICE
	 * versions don't have HWKM at all.  However, for HWKM to be fully
	 * usable by Linux, the TrustZone software also needs to support certain
	 * SCM calls including the ones to generate and prepare keys.  That
	 * effectively makes the earliest supported SoC be SM8650, which has
	 * HWKM v2.  Therefore, this driver doesn't include support for HWKM v1,
	 * and it checks for the SCM call support before it decides to use HWKM.
	 *
	 * Also, since HWKM and legacy mode are mutually exclusive, and
	 * ICE-capable storage driver(s) need to know early on whether to
	 * advertise support for raw keys or wrapped keys, HWKM cannot be used
	 * unconditionally.  A module parameter is used to opt into using it.
	 */
	if ((major >= 4 ||
	     (major == 3 && (minor >= 3 || (minor == 2 && step >= 1)))) &&
	    qcom_scm_has_wrapped_key_support()) {
		if (qcom_ice_use_wrapped_keys) {
			dev_info(dev, "Using HWKM. Supporting wrapped keys only.\n");
			ice->use_hwkm = true;
		} else {
			dev_info(dev, "Not using HWKM. Supporting raw keys only.\n");
		}
	} else if (qcom_ice_use_wrapped_keys) {
		dev_warn(dev, "A supported HWKM is not present. Ignoring qcom_ice.use_wrapped_keys=1.\n");
	} else {
		dev_info(dev, "A supported HWKM is not present. Supporting raw keys only.\n");
	}
	return true;
}

static void qcom_ice_low_power_mode_enable(struct qcom_ice *ice)
{
	u32 regval;

	regval = qcom_ice_readl(ice, QCOM_ICE_REG_ADVANCED_CONTROL);

	/* Enable low power mode sequence */
	regval |= 0x7000;
	qcom_ice_writel(ice, regval, QCOM_ICE_REG_ADVANCED_CONTROL);
}

static void qcom_ice_optimization_enable(struct qcom_ice *ice)
{
	u32 regval;

	/* ICE Optimizations Enable Sequence */
	regval = qcom_ice_readl(ice, QCOM_ICE_REG_ADVANCED_CONTROL);
	regval |= 0xd807100;
	/* ICE HPG requires delay before writing */
	udelay(5);
	qcom_ice_writel(ice, regval, QCOM_ICE_REG_ADVANCED_CONTROL);
	udelay(5);
}

/*
 * Wait until the ICE BIST (built-in self-test) has completed.
 *
 * This may be necessary before ICE can be used.
 * Note that we don't really care whether the BIST passed or failed;
 * we really just want to make sure that it isn't still running. This is
 * because (a) the BIST is a FIPS compliance thing that never fails in
 * practice, (b) ICE is documented to reject crypto requests if the BIST
 * fails, so we needn't do it in software too, and (c) properly testing
 * storage encryption requires testing the full storage stack anyway,
 * and not relying on hardware-level self-tests.
 */
static int qcom_ice_wait_bist_status(struct qcom_ice *ice)
{
	u32 regval;
	int err;

	err = readl_poll_timeout(ice->base + QCOM_ICE_REG_BIST_STATUS,
				 regval, !(regval & QCOM_ICE_BIST_STATUS_MASK),
				 50, 5000);
	if (err) {
		dev_err(ice->dev, "Timed out waiting for ICE self-test to complete\n");
		return err;
	}

	if (ice->use_hwkm &&
	    qcom_ice_readl(ice, QCOM_ICE_REG_HWKM_TZ_KM_STATUS) !=
	    (QCOM_ICE_HWKM_KT_CLEAR_DONE |
	     QCOM_ICE_HWKM_BOOT_CMD_LIST0_DONE |
	     QCOM_ICE_HWKM_BOOT_CMD_LIST1_DONE |
	     QCOM_ICE_HWKM_CRYPTO_BIST_DONE_V2 |
	     QCOM_ICE_HWKM_BIST_DONE_V2)) {
		dev_err(ice->dev, "HWKM self-test error!\n");
		/*
		 * Too late to revoke use_hwkm here, as it was already
		 * propagated up the stack into the crypto capabilities.
		 */
	}
	return 0;
}

static void qcom_ice_hwkm_init(struct qcom_ice *ice)
{
	u32 regval;

	if (!ice->use_hwkm)
		return;

	BUILD_BUG_ON(QCOM_ICE_HWKM_WRAPPED_KEY_SIZE >
		     BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE);
	/*
	 * When ICE is in HWKM mode, it only supports wrapped keys.
	 * When ICE is in legacy mode, it only supports raw keys.
	 *
	 * Put ICE in HWKM mode.  ICE defaults to legacy mode.
	 */
	regval = qcom_ice_readl(ice, QCOM_ICE_REG_CONTROL);
	regval &= ~QCOM_ICE_LEGACY_MODE_ENABLED;
	qcom_ice_writel(ice, regval, QCOM_ICE_REG_CONTROL);

	/* Disable CRC checks.  This HWKM feature is not used. */
	qcom_ice_writel(ice, QCOM_ICE_HWKM_DISABLE_CRC_CHECKS_VAL,
			QCOM_ICE_REG_HWKM_TZ_KM_CTL);

	/*
	 * Allow the HWKM slave to read and write the keyslots in the ICE HWKM
	 * slave.  Without this, TrustZone cannot program keys into ICE.
	 */
	qcom_ice_writel(ice, GENMASK(31, 0), QCOM_ICE_REG_HWKM_BANK0_BBAC_0);
	qcom_ice_writel(ice, GENMASK(31, 0), QCOM_ICE_REG_HWKM_BANK0_BBAC_1);
	qcom_ice_writel(ice, GENMASK(31, 0), QCOM_ICE_REG_HWKM_BANK0_BBAC_2);
	qcom_ice_writel(ice, GENMASK(31, 0), QCOM_ICE_REG_HWKM_BANK0_BBAC_3);
	qcom_ice_writel(ice, GENMASK(31, 0), QCOM_ICE_REG_HWKM_BANK0_BBAC_4);

	/* Clear the HWKM response FIFO. */
	qcom_ice_writel(ice, QCOM_ICE_HWKM_RSP_FIFO_CLEAR_VAL,
			QCOM_ICE_REG_HWKM_BANK0_BANKN_IRQ_STATUS);
	ice->hwkm_init_complete = true;
}

int qcom_ice_enable(struct qcom_ice *ice)
{
	qcom_ice_low_power_mode_enable(ice);
	qcom_ice_optimization_enable(ice);
	qcom_ice_hwkm_init(ice);
	return qcom_ice_wait_bist_status(ice);
}
EXPORT_SYMBOL_GPL(qcom_ice_enable);

int qcom_ice_resume(struct qcom_ice *ice)
{
	struct device *dev = ice->dev;
	int err;

	err = clk_prepare_enable(ice->core_clk);
	if (err) {
		dev_err(dev, "failed to enable core clock (%d)\n",
			err);
		return err;
	}
	qcom_ice_hwkm_init(ice);
	return qcom_ice_wait_bist_status(ice);
}
EXPORT_SYMBOL_GPL(qcom_ice_resume);

int qcom_ice_suspend(struct qcom_ice *ice)
{
	clk_disable_unprepare(ice->core_clk);
	ice->hwkm_init_complete = false;

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_ice_suspend);

static unsigned int translate_hwkm_slot(struct qcom_ice *ice, unsigned int slot)
{
	return slot * 2;
}

static int qcom_ice_program_wrapped_key(struct qcom_ice *ice, unsigned int slot,
					const struct blk_crypto_key *bkey)
{
	struct device *dev = ice->dev;
	union crypto_cfg cfg = {
		.dusize = bkey->crypto_cfg.data_unit_size / 512,
		.capidx = QCOM_SCM_ICE_CIPHER_AES_256_XTS,
		.cfge = QCOM_ICE_HWKM_CFG_ENABLE_VAL,
	};
	int err;

	if (!ice->use_hwkm) {
		dev_err_ratelimited(dev, "Got wrapped key when not using HWKM\n");
		return -EINVAL;
	}
	if (!ice->hwkm_init_complete) {
		dev_err_ratelimited(dev, "HWKM not yet initialized\n");
		return -EINVAL;
	}

	/* Clear CFGE before programming the key. */
	qcom_ice_writel(ice, 0x0, QCOM_ICE_REG_CRYPTOCFG(slot));

	/* Call into TrustZone to program the wrapped key using HWKM. */
	err = qcom_scm_ice_set_key(translate_hwkm_slot(ice, slot), bkey->bytes,
				   bkey->size, cfg.capidx, cfg.dusize);
	if (err) {
		dev_err_ratelimited(dev,
				    "qcom_scm_ice_set_key failed; err=%d, slot=%u\n",
				    err, slot);
		return err;
	}

	/* Set CFGE after programming the key. */
	qcom_ice_writel(ice, le32_to_cpu(cfg.regval),
			QCOM_ICE_REG_CRYPTOCFG(slot));
	return 0;
}

int qcom_ice_program_key(struct qcom_ice *ice, unsigned int slot,
			 const struct blk_crypto_key *blk_key)
{
	struct device *dev = ice->dev;
	union {
		u8 bytes[AES_256_XTS_KEY_SIZE];
		u32 words[AES_256_XTS_KEY_SIZE / sizeof(u32)];
	} key;
	int i;
	int err;

	/* Only AES-256-XTS has been tested so far. */
	if (blk_key->crypto_cfg.crypto_mode !=
	    BLK_ENCRYPTION_MODE_AES_256_XTS) {
		dev_err_ratelimited(dev, "Unsupported crypto mode: %d\n",
				    blk_key->crypto_cfg.crypto_mode);
		return -EINVAL;
	}

	if (blk_key->crypto_cfg.key_type == BLK_CRYPTO_KEY_TYPE_HW_WRAPPED)
		return qcom_ice_program_wrapped_key(ice, slot, blk_key);

	if (ice->use_hwkm) {
		dev_err_ratelimited(dev, "Got raw key when using HWKM\n");
		return -EINVAL;
	}

	if (blk_key->size != AES_256_XTS_KEY_SIZE) {
		dev_err_ratelimited(dev, "Incorrect key size\n");
		return -EINVAL;
	}
	memcpy(key.bytes, blk_key->bytes, AES_256_XTS_KEY_SIZE);

	/* The SCM call requires that the key words are encoded in big endian */
	for (i = 0; i < ARRAY_SIZE(key.words); i++)
		__cpu_to_be32s(&key.words[i]);

	err = qcom_scm_ice_set_key(slot, key.bytes, AES_256_XTS_KEY_SIZE,
				   QCOM_SCM_ICE_CIPHER_AES_256_XTS,
				   blk_key->crypto_cfg.data_unit_size / 512);

	memzero_explicit(&key, sizeof(key));

	return err;
}
EXPORT_SYMBOL_GPL(qcom_ice_program_key);

int qcom_ice_evict_key(struct qcom_ice *ice, int slot)
{
	if (ice->hwkm_init_complete)
		slot = translate_hwkm_slot(ice, slot);
	return qcom_scm_ice_invalidate_key(slot);
}
EXPORT_SYMBOL_GPL(qcom_ice_evict_key);

/**
 * qcom_ice_get_supported_key_type() - Get the supported key type
 * @ice: ICE driver data
 *
 * Return: the blk-crypto key type that the ICE driver is configured to use.
 * This is the key type that ICE-capable storage drivers should advertise as
 * supported in the crypto capabilities of any disks they register.
 */
enum blk_crypto_key_type qcom_ice_get_supported_key_type(struct qcom_ice *ice)
{
	if (ice->use_hwkm)
		return BLK_CRYPTO_KEY_TYPE_HW_WRAPPED;
	return BLK_CRYPTO_KEY_TYPE_RAW;
}
EXPORT_SYMBOL_GPL(qcom_ice_get_supported_key_type);

/**
 * qcom_ice_derive_sw_secret() - Derive software secret from wrapped key
 * @ice: ICE driver data
 * @eph_key: an ephemerally-wrapped key
 * @eph_key_size: size of @eph_key in bytes
 * @sw_secret: output buffer for the software secret
 *
 * Use HWKM to derive the "software secret" from a hardware-wrapped key that is
 * given in ephemerally-wrapped form.
 *
 * Return: 0 on success; -EBADMSG if the given ephemerally-wrapped key is
 *	   invalid; or another -errno value.
 */
int qcom_ice_derive_sw_secret(struct qcom_ice *ice,
			      const u8 *eph_key, size_t eph_key_size,
			      u8 sw_secret[BLK_CRYPTO_SW_SECRET_SIZE])
{
	int err = qcom_scm_derive_sw_secret(eph_key, eph_key_size,
					    sw_secret,
					    BLK_CRYPTO_SW_SECRET_SIZE);
	if (err == -EIO || err == -EINVAL)
		err = -EBADMSG; /* probably invalid key */
	return err;
}
EXPORT_SYMBOL_GPL(qcom_ice_derive_sw_secret);

/**
 * qcom_ice_generate_key() - Generate a wrapped key for inline encryption
 * @ice: ICE driver data
 * @lt_key: output buffer for the long-term wrapped key
 *
 * Use HWKM to generate a new key and return it as a long-term wrapped key.
 *
 * Return: the size of the resulting wrapped key on success; -errno on failure.
 */
int qcom_ice_generate_key(struct qcom_ice *ice,
			  u8 lt_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE])
{
	int err;

	err = qcom_scm_generate_ice_key(lt_key, QCOM_ICE_HWKM_WRAPPED_KEY_SIZE);
	if (err)
		return err;

	return QCOM_ICE_HWKM_WRAPPED_KEY_SIZE;
}
EXPORT_SYMBOL_GPL(qcom_ice_generate_key);

/**
 * qcom_ice_prepare_key() - Prepare a wrapped key for inline encryption
 * @ice: ICE driver data
 * @lt_key: a long-term wrapped key
 * @lt_key_size: size of @lt_key in bytes
 * @eph_key: output buffer for the ephemerally-wrapped key
 *
 * Use HWKM to re-wrap a long-term wrapped key with the per-boot ephemeral key.
 *
 * Return: the size of the resulting wrapped key on success; -EBADMSG if the
 *	   given long-term wrapped key is invalid; or another -errno value.
 */
int qcom_ice_prepare_key(struct qcom_ice *ice,
			 const u8 *lt_key, size_t lt_key_size,
			 u8 eph_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE])
{
	int err;

	err = qcom_scm_prepare_ice_key(lt_key, lt_key_size,
				       eph_key, QCOM_ICE_HWKM_WRAPPED_KEY_SIZE);
	if (err == -EIO || err == -EINVAL)
		err = -EBADMSG; /* probably invalid key */
	if (err)
		return err;

	return QCOM_ICE_HWKM_WRAPPED_KEY_SIZE;
}
EXPORT_SYMBOL_GPL(qcom_ice_prepare_key);

/**
 * qcom_ice_import_key() - Import a raw key for inline encryption
 * @ice: ICE driver data
 * @raw_key: the raw key to import
 * @raw_key_size: size of @raw_key in bytes
 * @lt_key: output buffer for the long-term wrapped key
 *
 * Use HWKM to import a raw key and return it as a long-term wrapped key.
 *
 * Return: the size of the resulting wrapped key on success; -errno on failure.
 */
int qcom_ice_import_key(struct qcom_ice *ice,
			const u8 *raw_key, size_t raw_key_size,
			u8 lt_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE])
{
	int err;

	err = qcom_scm_import_ice_key(raw_key, raw_key_size,
				      lt_key, QCOM_ICE_HWKM_WRAPPED_KEY_SIZE);
	if (err)
		return err;

	return QCOM_ICE_HWKM_WRAPPED_KEY_SIZE;
}
EXPORT_SYMBOL_GPL(qcom_ice_import_key);

static struct qcom_ice *qcom_ice_create(struct device *dev,
					void __iomem *base)
{
	struct qcom_ice *engine;

	if (!qcom_scm_is_available())
		return ERR_PTR(-EPROBE_DEFER);

	if (!qcom_scm_ice_available()) {
		dev_warn(dev, "ICE SCM interface not found\n");
		return NULL;
	}

	engine = devm_kzalloc(dev, sizeof(*engine), GFP_KERNEL);
	if (!engine)
		return ERR_PTR(-ENOMEM);

	engine->dev = dev;
	engine->base = base;

	/*
	 * Legacy DT binding uses different clk names for each consumer,
	 * so lets try those first. If none of those are a match, it means
	 * the we only have one clock and it is part of the dedicated DT node.
	 * Also, enable the clock before we check what HW version the driver
	 * supports.
	 */
	engine->core_clk = devm_clk_get_optional_enabled(dev, "ice_core_clk");
	if (!engine->core_clk)
		engine->core_clk = devm_clk_get_optional_enabled(dev, "ice");
	if (!engine->core_clk)
		engine->core_clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(engine->core_clk))
		return ERR_CAST(engine->core_clk);

	if (!qcom_ice_check_supported(engine))
		return ERR_PTR(-EOPNOTSUPP);

	dev_dbg(dev, "Registered Qualcomm Inline Crypto Engine\n");

	return engine;
}

/**
 * of_qcom_ice_get() - get an ICE instance from a DT node
 * @dev: device pointer for the consumer device
 *
 * This function will provide an ICE instance either by creating one for the
 * consumer device if its DT node provides the 'ice' reg range and the 'ice'
 * clock (for legacy DT style). On the other hand, if consumer provides a
 * phandle via 'qcom,ice' property to an ICE DT, the ICE instance will already
 * be created and so this function will return that instead.
 *
 * Return: ICE pointer on success, NULL if there is no ICE data provided by the
 * consumer or ERR_PTR() on error.
 */
static struct qcom_ice *of_qcom_ice_get(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct qcom_ice *ice;
	struct resource *res;
	void __iomem *base;
	struct device_link *link;

	if (!dev || !dev->of_node)
		return ERR_PTR(-ENODEV);

	/*
	 * In order to support legacy style devicetree bindings, we need
	 * to create the ICE instance using the consumer device and the reg
	 * range called 'ice' it provides.
	 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ice");
	if (res) {
		base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(base))
			return ERR_CAST(base);

		/* create ICE instance using consumer dev */
		return qcom_ice_create(&pdev->dev, base);
	}

	/*
	 * If the consumer node does not provider an 'ice' reg range
	 * (legacy DT binding), then it must at least provide a phandle
	 * to the ICE devicetree node, otherwise ICE is not supported.
	 */
	struct device_node *node __free(device_node) = of_parse_phandle(dev->of_node,
									"qcom,ice", 0);
	if (!node)
		return NULL;

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		dev_err(dev, "Cannot find device node %s\n", node->name);
		return ERR_PTR(-EPROBE_DEFER);
	}

	ice = platform_get_drvdata(pdev);
	if (!ice) {
		dev_err(dev, "Cannot get ice instance from %s\n",
			dev_name(&pdev->dev));
		platform_device_put(pdev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	link = device_link_add(dev, &pdev->dev, DL_FLAG_AUTOREMOVE_SUPPLIER);
	if (!link) {
		dev_err(&pdev->dev,
			"Failed to create device link to consumer %s\n",
			dev_name(dev));
		platform_device_put(pdev);
		ice = ERR_PTR(-EINVAL);
	}

	return ice;
}

static void qcom_ice_put(const struct qcom_ice *ice)
{
	struct platform_device *pdev = to_platform_device(ice->dev);

	if (!platform_get_resource_byname(pdev, IORESOURCE_MEM, "ice"))
		platform_device_put(pdev);
}

static void devm_of_qcom_ice_put(struct device *dev, void *res)
{
	qcom_ice_put(*(struct qcom_ice **)res);
}

/**
 * devm_of_qcom_ice_get() - Devres managed helper to get an ICE instance from
 * a DT node.
 * @dev: device pointer for the consumer device.
 *
 * This function will provide an ICE instance either by creating one for the
 * consumer device if its DT node provides the 'ice' reg range and the 'ice'
 * clock (for legacy DT style). On the other hand, if consumer provides a
 * phandle via 'qcom,ice' property to an ICE DT, the ICE instance will already
 * be created and so this function will return that instead.
 *
 * Return: ICE pointer on success, NULL if there is no ICE data provided by the
 * consumer or ERR_PTR() on error.
 */
struct qcom_ice *devm_of_qcom_ice_get(struct device *dev)
{
	struct qcom_ice *ice, **dr;

	dr = devres_alloc(devm_of_qcom_ice_put, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	ice = of_qcom_ice_get(dev);
	if (!IS_ERR_OR_NULL(ice)) {
		*dr = ice;
		devres_add(dev, dr);
	} else {
		devres_free(dr);
	}

	return ice;
}
EXPORT_SYMBOL_GPL(devm_of_qcom_ice_get);

static int qcom_ice_probe(struct platform_device *pdev)
{
	struct qcom_ice *engine;
	void __iomem *base;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_warn(&pdev->dev, "ICE registers not found\n");
		return PTR_ERR(base);
	}

	engine = qcom_ice_create(&pdev->dev, base);
	if (IS_ERR(engine))
		return PTR_ERR(engine);

	platform_set_drvdata(pdev, engine);

	return 0;
}

static const struct of_device_id qcom_ice_of_match_table[] = {
	{ .compatible = "qcom,inline-crypto-engine" },
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_ice_of_match_table);

static struct platform_driver qcom_ice_driver = {
	.probe	= qcom_ice_probe,
	.driver = {
		.name = "qcom-ice",
		.of_match_table = qcom_ice_of_match_table,
	},
};

module_platform_driver(qcom_ice_driver);

MODULE_DESCRIPTION("Qualcomm Inline Crypto Engine driver");
MODULE_LICENSE("GPL");
