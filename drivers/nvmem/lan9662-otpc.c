// SPDX-License-Identifier: GPL-2.0

#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define OTP_OTP_PWR_DN(t)			(t + 0x00)
#define OTP_OTP_PWR_DN_OTP_PWRDN_N		BIT(0)
#define OTP_OTP_ADDR_HI(t)			(t + 0x04)
#define OTP_OTP_ADDR_LO(t)			(t + 0x08)
#define OTP_OTP_PRGM_DATA(t)			(t + 0x10)
#define OTP_OTP_PRGM_MODE(t)			(t + 0x14)
#define OTP_OTP_PRGM_MODE_OTP_PGM_MODE_BYTE	BIT(0)
#define OTP_OTP_RD_DATA(t)			(t + 0x18)
#define OTP_OTP_FUNC_CMD(t)			(t + 0x20)
#define OTP_OTP_FUNC_CMD_OTP_PROGRAM		BIT(1)
#define OTP_OTP_FUNC_CMD_OTP_READ		BIT(0)
#define OTP_OTP_CMD_GO(t)			(t + 0x28)
#define OTP_OTP_CMD_GO_OTP_GO			BIT(0)
#define OTP_OTP_PASS_FAIL(t)			(t + 0x2c)
#define OTP_OTP_PASS_FAIL_OTP_READ_PROHIBITED	BIT(3)
#define OTP_OTP_PASS_FAIL_OTP_WRITE_PROHIBITED	BIT(2)
#define OTP_OTP_PASS_FAIL_OTP_FAIL		BIT(0)
#define OTP_OTP_STATUS(t)			(t + 0x30)
#define OTP_OTP_STATUS_OTP_CPUMPEN		BIT(1)
#define OTP_OTP_STATUS_OTP_BUSY			BIT(0)

#define OTP_MEM_SIZE 8192
#define OTP_SLEEP_US 10
#define OTP_TIMEOUT_US 500000

struct lan9662_otp {
	struct device *dev;
	void __iomem *base;
};

static bool lan9662_otp_wait_flag_clear(void __iomem *reg, u32 flag)
{
	u32 val;

	return readl_poll_timeout(reg, val, !(val & flag),
				  OTP_SLEEP_US, OTP_TIMEOUT_US);
}

static int lan9662_otp_power(struct lan9662_otp *otp, bool up)
{
	void __iomem *pwrdn = OTP_OTP_PWR_DN(otp->base);

	if (up) {
		writel(readl(pwrdn) & ~OTP_OTP_PWR_DN_OTP_PWRDN_N, pwrdn);
		if (lan9662_otp_wait_flag_clear(OTP_OTP_STATUS(otp->base),
						OTP_OTP_STATUS_OTP_CPUMPEN))
			return -ETIMEDOUT;
	} else {
		writel(readl(pwrdn) | OTP_OTP_PWR_DN_OTP_PWRDN_N, pwrdn);
	}

	return 0;
}

static int lan9662_otp_execute(struct lan9662_otp *otp)
{
	if (lan9662_otp_wait_flag_clear(OTP_OTP_CMD_GO(otp->base),
					OTP_OTP_CMD_GO_OTP_GO))
		return -ETIMEDOUT;

	if (lan9662_otp_wait_flag_clear(OTP_OTP_STATUS(otp->base),
					OTP_OTP_STATUS_OTP_BUSY))
		return -ETIMEDOUT;

	return 0;
}

static void lan9662_otp_set_address(struct lan9662_otp *otp, u32 offset)
{
	writel(0xff & (offset >> 8), OTP_OTP_ADDR_HI(otp->base));
	writel(0xff & offset, OTP_OTP_ADDR_LO(otp->base));
}

static int lan9662_otp_read_byte(struct lan9662_otp *otp, u32 offset, u8 *dst)
{
	u32 pass;
	int rc;

	lan9662_otp_set_address(otp, offset);
	writel(OTP_OTP_FUNC_CMD_OTP_READ, OTP_OTP_FUNC_CMD(otp->base));
	writel(OTP_OTP_CMD_GO_OTP_GO, OTP_OTP_CMD_GO(otp->base));
	rc = lan9662_otp_execute(otp);
	if (!rc) {
		pass = readl(OTP_OTP_PASS_FAIL(otp->base));
		if (pass & OTP_OTP_PASS_FAIL_OTP_READ_PROHIBITED)
			return -EACCES;
		*dst = (u8) readl(OTP_OTP_RD_DATA(otp->base));
	}
	return rc;
}

static int lan9662_otp_write_byte(struct lan9662_otp *otp, u32 offset, u8 data)
{
	u32 pass;
	int rc;

	lan9662_otp_set_address(otp, offset);
	writel(OTP_OTP_PRGM_MODE_OTP_PGM_MODE_BYTE, OTP_OTP_PRGM_MODE(otp->base));
	writel(data, OTP_OTP_PRGM_DATA(otp->base));
	writel(OTP_OTP_FUNC_CMD_OTP_PROGRAM, OTP_OTP_FUNC_CMD(otp->base));
	writel(OTP_OTP_CMD_GO_OTP_GO, OTP_OTP_CMD_GO(otp->base));

	rc = lan9662_otp_execute(otp);
	if (!rc) {
		pass = readl(OTP_OTP_PASS_FAIL(otp->base));
		if (pass & OTP_OTP_PASS_FAIL_OTP_WRITE_PROHIBITED)
			return -EACCES;
		if (pass & OTP_OTP_PASS_FAIL_OTP_FAIL)
			return -EIO;
	}
	return rc;
}

static int lan9662_otp_read(void *context, unsigned int offset,
			    void *_val, size_t bytes)
{
	struct lan9662_otp *otp = context;
	u8 *val = _val;
	uint8_t data;
	int i, rc = 0;

	lan9662_otp_power(otp, true);
	for (i = 0; i < bytes; i++) {
		rc = lan9662_otp_read_byte(otp, offset + i, &data);
		if (rc < 0)
			break;
		*val++ = data;
	}
	lan9662_otp_power(otp, false);

	return rc;
}

static int lan9662_otp_write(void *context, unsigned int offset,
			     void *_val, size_t bytes)
{
	struct lan9662_otp *otp = context;
	u8 *val = _val;
	u8 data, newdata;
	int i, rc = 0;

	lan9662_otp_power(otp, true);
	for (i = 0; i < bytes; i++) {
		/* Skip zero bytes */
		if (val[i]) {
			rc = lan9662_otp_read_byte(otp, offset + i, &data);
			if (rc < 0)
				break;

			newdata = data | val[i];
			if (newdata == data)
				continue;

			rc = lan9662_otp_write_byte(otp, offset + i,
						      newdata);
			if (rc < 0)
				break;
		}
	}
	lan9662_otp_power(otp, false);

	return rc;
}

static struct nvmem_config otp_config = {
	.name = "lan9662-otp",
	.stride = 1,
	.word_size = 1,
	.reg_read = lan9662_otp_read,
	.reg_write = lan9662_otp_write,
	.size = OTP_MEM_SIZE,
};

static int lan9662_otp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvmem_device *nvmem;
	struct lan9662_otp *otp;

	otp = devm_kzalloc(&pdev->dev, sizeof(*otp), GFP_KERNEL);
	if (!otp)
		return -ENOMEM;

	otp->dev = dev;
	otp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(otp->base))
		return PTR_ERR(otp->base);

	otp_config.priv = otp;
	otp_config.dev = dev;

	nvmem = devm_nvmem_register(dev, &otp_config);

	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct of_device_id lan9662_otp_match[] = {
	{ .compatible = "microchip,lan9662-otpc", },
	{ },
};
MODULE_DEVICE_TABLE(of, lan9662_otp_match);

static struct platform_driver lan9662_otp_driver = {
	.probe = lan9662_otp_probe,
	.driver = {
		.name = "lan9662-otp",
		.of_match_table = lan9662_otp_match,
	},
};
module_platform_driver(lan9662_otp_driver);

MODULE_AUTHOR("Horatiu Vultur <horatiu.vultur@microchip.com>");
MODULE_DESCRIPTION("lan9662 OTP driver");
MODULE_LICENSE("GPL");
