// SPDX-License-Identifier: GPL-2.0
/* (C) 2015 Pengutronix, Alexander Aring <aar@pengutronix.de>
 *
 * Authors:
 * Alexander Aring <aar@pengutronix.de>
 * Eric Anholt <eric@anholt.net>
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <dt-bindings/power/raspberrypi-power.h>
#include <soc/bcm2835/raspberrypi-firmware.h>

/*
 * Firmware indices for the old power domains interface.  Only a few
 * of them were actually implemented.
 */
#define RPI_OLD_POWER_DOMAIN_USB		3
#define RPI_OLD_POWER_DOMAIN_V3D		10

struct rpi_power_domain {
	u32 domain;
	bool enabled;
	bool old_interface;
	struct generic_pm_domain base;
	struct rpi_firmware *fw;
};

struct rpi_power_domains {
	bool has_new_interface;
	struct genpd_onecell_data xlate;
	struct rpi_firmware *fw;
	struct rpi_power_domain domains[RPI_POWER_DOMAIN_COUNT];
};

/*
 * Packet definition used by RPI_FIRMWARE_SET_POWER_STATE and
 * RPI_FIRMWARE_SET_DOMAIN_STATE
 */
struct rpi_power_domain_packet {
	u32 domain;
	u32 on;
};

/*
 * Asks the firmware to enable or disable power on a specific power
 * domain.
 */
static int rpi_firmware_set_power(struct rpi_power_domain *rpi_domain, bool on)
{
	struct rpi_power_domain_packet packet;

	packet.domain = rpi_domain->domain;
	packet.on = on;
	return rpi_firmware_property(rpi_domain->fw,
				     rpi_domain->old_interface ?
				     RPI_FIRMWARE_SET_POWER_STATE :
				     RPI_FIRMWARE_SET_DOMAIN_STATE,
				     &packet, sizeof(packet));
}

static int rpi_domain_off(struct generic_pm_domain *domain)
{
	struct rpi_power_domain *rpi_domain =
		container_of(domain, struct rpi_power_domain, base);

	return rpi_firmware_set_power(rpi_domain, false);
}

static int rpi_domain_on(struct generic_pm_domain *domain)
{
	struct rpi_power_domain *rpi_domain =
		container_of(domain, struct rpi_power_domain, base);

	return rpi_firmware_set_power(rpi_domain, true);
}

static void rpi_common_init_power_domain(struct rpi_power_domains *rpi_domains,
					 int xlate_index, const char *name)
{
	struct rpi_power_domain *dom = &rpi_domains->domains[xlate_index];

	dom->fw = rpi_domains->fw;

	dom->base.name = name;
	dom->base.power_on = rpi_domain_on;
	dom->base.power_off = rpi_domain_off;

	/*
	 * Treat all power domains as off at boot.
	 *
	 * The firmware itself may be keeping some domains on, but
	 * from Linux's perspective all we control is the refcounts
	 * that we give to the firmware, and we can't ask the firmware
	 * to turn off something that we haven't ourselves turned on.
	 */
	pm_genpd_init(&dom->base, NULL, true);

	rpi_domains->xlate.domains[xlate_index] = &dom->base;
}

static void rpi_init_power_domain(struct rpi_power_domains *rpi_domains,
				  int xlate_index, const char *name)
{
	struct rpi_power_domain *dom = &rpi_domains->domains[xlate_index];

	if (!rpi_domains->has_new_interface)
		return;

	/* The DT binding index is the firmware's domain index minus one. */
	dom->domain = xlate_index + 1;

	rpi_common_init_power_domain(rpi_domains, xlate_index, name);
}

static void rpi_init_old_power_domain(struct rpi_power_domains *rpi_domains,
				      int xlate_index, int domain,
				      const char *name)
{
	struct rpi_power_domain *dom = &rpi_domains->domains[xlate_index];

	dom->old_interface = true;
	dom->domain = domain;

	rpi_common_init_power_domain(rpi_domains, xlate_index, name);
}

/*
 * Detects whether the firmware supports the new power domains interface.
 *
 * The firmware doesn't actually return an error on an unknown tag,
 * and just skips over it, so we do the detection by putting an
 * unexpected value in the return field and checking if it was
 * unchanged.
 */
static bool
rpi_has_new_domain_support(struct rpi_power_domains *rpi_domains)
{
	struct rpi_power_domain_packet packet;
	int ret;

	packet.domain = RPI_POWER_DOMAIN_ARM;
	packet.on = ~0;

	ret = rpi_firmware_property(rpi_domains->fw,
				    RPI_FIRMWARE_GET_DOMAIN_STATE,
				    &packet, sizeof(packet));

	return ret == 0 && packet.on != ~0;
}

static int rpi_power_probe(struct platform_device *pdev)
{
	struct device_node *fw_np;
	struct device *dev = &pdev->dev;
	struct rpi_power_domains *rpi_domains;

	rpi_domains = devm_kzalloc(dev, sizeof(*rpi_domains), GFP_KERNEL);
	if (!rpi_domains)
		return -ENOMEM;

	rpi_domains->xlate.domains =
		devm_kcalloc(dev,
			     RPI_POWER_DOMAIN_COUNT,
			     sizeof(*rpi_domains->xlate.domains),
			     GFP_KERNEL);
	if (!rpi_domains->xlate.domains)
		return -ENOMEM;

	rpi_domains->xlate.num_domains = RPI_POWER_DOMAIN_COUNT;

	fw_np = of_parse_phandle(pdev->dev.of_node, "firmware", 0);
	if (!fw_np) {
		dev_err(&pdev->dev, "no firmware node\n");
		return -ENODEV;
	}

	rpi_domains->fw = rpi_firmware_get(fw_np);
	of_node_put(fw_np);
	if (!rpi_domains->fw)
		return -EPROBE_DEFER;

	rpi_domains->has_new_interface =
		rpi_has_new_domain_support(rpi_domains);

	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_I2C0, "I2C0");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_I2C1, "I2C1");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_I2C2, "I2C2");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_VIDEO_SCALER,
			      "VIDEO_SCALER");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_VPU1, "VPU1");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_HDMI, "HDMI");

	/*
	 * Use the old firmware interface for USB power, so that we
	 * can turn it on even if the firmware hasn't been updated.
	 */
	rpi_init_old_power_domain(rpi_domains, RPI_POWER_DOMAIN_USB,
				  RPI_OLD_POWER_DOMAIN_USB, "USB");

	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_VEC, "VEC");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_JPEG, "JPEG");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_H264, "H264");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_V3D, "V3D");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_ISP, "ISP");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_UNICAM0, "UNICAM0");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_UNICAM1, "UNICAM1");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_CCP2RX, "CCP2RX");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_CSI2, "CSI2");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_CPI, "CPI");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_DSI0, "DSI0");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_DSI1, "DSI1");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_TRANSPOSER,
			      "TRANSPOSER");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_CCP2TX, "CCP2TX");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_CDP, "CDP");
	rpi_init_power_domain(rpi_domains, RPI_POWER_DOMAIN_ARM, "ARM");

	of_genpd_add_provider_onecell(dev->of_node, &rpi_domains->xlate);

	platform_set_drvdata(pdev, rpi_domains);

	return 0;
}

static const struct of_device_id rpi_power_of_match[] = {
	{ .compatible = "raspberrypi,bcm2835-power", },
	{},
};
MODULE_DEVICE_TABLE(of, rpi_power_of_match);

static struct platform_driver rpi_power_driver = {
	.driver = {
		.name = "raspberrypi-power",
		.of_match_table = rpi_power_of_match,
	},
	.probe		= rpi_power_probe,
};
builtin_platform_driver(rpi_power_driver);

MODULE_AUTHOR("Alexander Aring <aar@pengutronix.de>");
MODULE_AUTHOR("Eric Anholt <eric@anholt.net>");
MODULE_DESCRIPTION("Raspberry Pi power domain driver");
MODULE_LICENSE("GPL v2");
