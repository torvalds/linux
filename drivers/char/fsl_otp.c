/*
 * Freescale On-Chip OTP driver
 *
 * Copyright (C) 2010-2016 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#define HW_OCOTP_CTRL			0x00000000
#define HW_OCOTP_CTRL_SET		0x00000004
#define BP_OCOTP_CTRL_WR_UNLOCK		16
#define BM_OCOTP_CTRL_WR_UNLOCK		0xFFFF0000
#define BM_OCOTP_CTRL_RELOAD_SHADOWS	0x00000400
#define BM_OCOTP_CTRL_ERROR		0x00000200
#define BM_OCOTP_CTRL_BUSY		0x00000100
#define BP_OCOTP_CTRL_ADDR		0
#define BM_OCOTP_CTRL_ADDR		0x0000007F
#define BM_OCOTP_CTRL_ADDR_MX7D		0x0000000F

#define HW_OCOTP_TIMING			0x00000010
#define BP_OCOTP_TIMING_STROBE_READ	16
#define BM_OCOTP_TIMING_STROBE_READ	0x003F0000
#define BP_OCOTP_TIMING_RELAX		12
#define BM_OCOTP_TIMING_RELAX		0x0000F000
#define BP_OCOTP_TIMING_STROBE_PROG	0
#define BM_OCOTP_TIMING_STROBE_PROG	0x00000FFF

#define BP_TIMING_FSOURCE		12
#define BM_TIMING_FSOURCE		0x0007F000
#define BV_TIMING_FSOURCE_NS		1001
#define BP_TIMING_PROG			0
#define BM_TIMING_PROG			0x00000FFF
#define BV_TIMING_PROG_US		10

#define HW_OCOTP_DATA			0x00000020

#define HW_OCOTP_DATA0_MX7D		0x00000020
#define HW_OCOTP_DATA1_MX7D		0x00000030
#define HW_OCOTP_DATA2_MX7D		0x00000040
#define HW_OCOTP_DATA3_MX7D		0x00000050

#define HW_OCOTP_CUST_N(n)	(0x00000400 + (n) * 0x10)
#define BF(value, field)	(((value) << BP_##field) & BM_##field)

#define DEF_RELAX		20	/* > 16.5ns */

#define BANK8(a, b, c, d, e, f, g, h) { \
	"HW_OCOTP_"#a, "HW_OCOTP_"#b, "HW_OCOTP_"#c, "HW_OCOTP_"#d, \
	"HW_OCOTP_"#e, "HW_OCOTP_"#f, "HW_OCOTP_"#g, "HW_OCOTP_"#h, \
}

#define BANK4(a, b, c, d) { \
	"HW_OCOTP_"#a, "HW_OCOTP_"#b, "HW_OCOTP_"#c, "HW_OCOTP_"#d, \
}

static const char *imx6q_otp_desc[16][8] = {
	BANK8(LOCK, CFG0, CFG1, CFG2, CFG3, CFG4, CFG5, CFG6),
	BANK8(MEM0, MEM1, MEM2, MEM3, MEM4, ANA0, ANA1, ANA2),
	BANK8(OTPMK0, OTPMK1, OTPMK2, OTPMK3, OTPMK4, OTPMK5, OTPMK6, OTPMK7),
	BANK8(SRK0, SRK1, SRK2, SRK3, SRK4, SRK5, SRK6, SRK7),
	BANK8(RESP0, HSJC_RESP1, MAC0, MAC1, HDCP_KSV0, HDCP_KSV1, GP1, GP2),
	BANK8(DTCP_KEY0,  DTCP_KEY1,  DTCP_KEY2,  DTCP_KEY3,  DTCP_KEY4,  MISC_CONF,  FIELD_RETURN, SRK_REVOKE),
	BANK8(HDCP_KEY0,  HDCP_KEY1,  HDCP_KEY2,  HDCP_KEY3,  HDCP_KEY4,  HDCP_KEY5,  HDCP_KEY6,  HDCP_KEY7),
	BANK8(HDCP_KEY8,  HDCP_KEY9,  HDCP_KEY10, HDCP_KEY11, HDCP_KEY12, HDCP_KEY13, HDCP_KEY14, HDCP_KEY15),
	BANK8(HDCP_KEY16, HDCP_KEY17, HDCP_KEY18, HDCP_KEY19, HDCP_KEY20, HDCP_KEY21, HDCP_KEY22, HDCP_KEY23),
	BANK8(HDCP_KEY24, HDCP_KEY25, HDCP_KEY26, HDCP_KEY27, HDCP_KEY28, HDCP_KEY29, HDCP_KEY30, HDCP_KEY31),
	BANK8(HDCP_KEY32, HDCP_KEY33, HDCP_KEY34, HDCP_KEY35, HDCP_KEY36, HDCP_KEY37, HDCP_KEY38, HDCP_KEY39),
	BANK8(HDCP_KEY40, HDCP_KEY41, HDCP_KEY42, HDCP_KEY43, HDCP_KEY44, HDCP_KEY45, HDCP_KEY46, HDCP_KEY47),
	BANK8(HDCP_KEY48, HDCP_KEY49, HDCP_KEY50, HDCP_KEY51, HDCP_KEY52, HDCP_KEY53, HDCP_KEY54, HDCP_KEY55),
	BANK8(HDCP_KEY56, HDCP_KEY57, HDCP_KEY58, HDCP_KEY59, HDCP_KEY60, HDCP_KEY61, HDCP_KEY62, HDCP_KEY63),
	BANK8(HDCP_KEY64, HDCP_KEY65, HDCP_KEY66, HDCP_KEY67, HDCP_KEY68, HDCP_KEY69, HDCP_KEY70, HDCP_KEY71),
	BANK8(CRC0, CRC1, CRC2, CRC3, CRC4, CRC5, CRC6, CRC7),
};

static const char *imx6sl_otp_desc[][8] = {
	BANK8(LOCK, CFG0, CFG1, CFG2, CFG3, CFG4, CFG5, CFG6),
	BANK8(MEM0, MEM1, MEM2, MEM3, MEM4, ANA0, ANA1, ANA2),
	BANK8(OTPMK0, OTPMK1, OTPMK2, OTPMK3, OTPMK4, OTPMK5, OTPMK6, OTPMK7),
	BANK8(SRK0, SRK1, SRK2, SRK3, SRK4, SRK5, SRK6, SRK7),
	BANK8(SJC_RESP0, SJC_RESP1, MAC0, MAC1, CRC0, CRC1, GP1, GP2),
	BANK8(SW_GP0, SW_GP1, SW_GP2, SW_GP3, SW_GP4, MISC_CONF, FIELD_RETURN, SRK_REVOKE),
	BANK8(GP_LO0, GP_LO1, GP_LO2, GP_LO3, GP_LO4, GP_LO5, GP_LO6, GP_LO7),
	BANK8(GP_HI0, GP_HI1, GP_HI2, GP_HI3, GP_HI4, GP_HI5, GP_HI6, GP_HI7),
};

static const char *imx6ul_otp_desc[][8] = {
	BANK8(LOCK, CFG0, CFG1, CFG2, CFG3, CFG4, CFG5, CFG6),
	BANK8(MEM0, MEM1, MEM2, MEM3, MEM4, ANA0, ANA1, ANA2),
	BANK8(OTPMK0, OTPMK1, OTPMK2, OTPMK3, OTPMK4, OTPMK5, OTPMK6, OTPMK7),
	BANK8(SRK0, SRK1, SRK2, SRK3, SRK4, SRK5, SRK6, SRK7),
	BANK8(SJC_RESP0, SJC_RESP1, MAC0, MAC1, MAC2, CRC, GP1, GP2),
	BANK8(SW_GP0, SW_GP1, SW_GP2, SW_GP3, SW_GP4,  MISC_CONF,  FIELD_RETURN, SRK_REVOKE),
	BANK8(ROM_PATCH0, ROM_PATCH1, ROM_PATCH2, ROM_PATCH3, ROM_PATCH4, ROM_PATCH5, ROM_PATCH6, ROM_PATCH7),
	BANK8(ROM_PATCH8, ROM_PATCH9, ROM_PATCH10, ROM_PATCH11, ROM_PATCH12, ROM_PATCH13, ROM_PATCH14, ROM_PATCH15),
	BANK8(GP30, GP31, GP32, GP33, GP34, GP35, GP36, GP37),
	BANK8(GP38, GP39, GP310, GP311, GP312, GP313, GP314, GP315),
	BANK8(GP40, GP41, GP42, GP43, GP44, GP45, GP46, GP47),
	BANK8(GP48, GP49, GP410, GP411, GP412, GP413, GP414, GP415),
	BANK8(GP50, GP51, GP52, GP53, GP54, GP55, GP56, GP57),
	BANK8(GP58, GP59, GP510, GP511, GP512, GP513, GP514, GP515),
	BANK8(GP60, GP61, GP62, GP63, GP64, GP65, GP66, GP67),
	BANK8(GP70, GP71, GP72, GP73, GP80, GP81, GP82, GP83),
};

static const char *imx6ull_otp_desc[][8] = {
	BANK8(LOCK, CFG0, CFG1, CFG2, CFG3, CFG4, CFG5, CFG6),
	BANK8(MEM0, MEM1, MEM2, MEM3, MEM4, ANA0, ANA1, ANA2),
	BANK8(OTPMK0, OTPMK1, OTPMK2, OTPMK3, OTPMK4, OTPMK5, OTPMK6, OTPMK7),
	BANK8(SRK0, SRK1, SRK2, SRK3, SRK4, SRK5, SRK6, SRK7),
	BANK8(SJC_RESP0, SJC_RESP1, MAC0, MAC1, MAC2, CRC, GP1, GP2),
	BANK8(SW_GP0, SW_GP1, SW_GP2, SW_GP3, SW_GP4,  MISC_CONF,  FIELD_RETURN, SRK_REVOKE),
	BANK8(ROM_PATCH0, ROM_PATCH1, ROM_PATCH2, ROM_PATCH3, ROM_PATCH4, ROM_PATCH5, ROM_PATCH6, ROM_PATCH7),
	BANK8(GP30, GP31, GP32, GP33, GP40, GP41, GP42, GP43),
};

static const char *imx7d_otp_desc[][4] = {
	BANK4(LOCK, TESTER0, TESTER1, TESTER2),
	BANK4(TESTER3, TESTER4, TESTER5, BOOT_CFG0),
	BANK4(BOOT_CFG1, BOOT_CFG2, BOOT_CFG3, BOOT_CFG4),
	BANK4(MEM_TRIM0, MEM_TRIM1, ANA0, ANA1),
	BANK4(OTPMK0, OTPMK1, OTPMK2, OTPMK3),
	BANK4(OTPMK4, OTPMK5, OTPMK6, OTPMK7),
	BANK4(SRK0, SRK1, SRK2, SRK3),
	BANK4(SRK4, SRK5, SRK6, SRK7),
	BANK4(SJC_RESP0, SJC_RESP1, USB_ID, FIELD_RETURN),
	BANK4(MAC_ADDR0, MAC_ADDR1, MAC_ADDR2, SRK_REVOKE),
	BANK4(MAU_KEY0, MAU_KEY1, MAU_KEY2, MAU_KEY3),
	BANK4(MAU_KEY4, MAU_KEY5, MAU_KEY6, MAU_KEY7),
	BANK4(ROM_PATCH0, ROM_PATCH1, ROM_PATCH2, ROM_PATCH3),
	BANK4(ROM_PATCH4, ROM_PATCH5, ROM_PATCH6, ROM_PATCH7),
	BANK4(GP10, GP11, GP20, GP21),
	BANK4(CRC_GP10, CRC_GP11, CRC_GP20, CRC_GP21),
};

static DEFINE_MUTEX(otp_mutex);
static void __iomem *otp_base;
static struct clk *otp_clk;
struct kobject *otp_kobj;
struct kobj_attribute *otp_kattr;
struct attribute_group *otp_attr_group;

enum fsl_otp_devtype {
	FSL_OTP_MX6Q,
	FSL_OTP_MX6DL,
	FSL_OTP_MX6SX,
	FSL_OTP_MX6SL,
	FSL_OTP_MX6UL,
	FSL_OTP_MX6ULL,
	FSL_OTP_MX7D,
};

struct fsl_otp_devtype_data {
	enum fsl_otp_devtype devtype;
	const char **bank_desc;
	int fuse_nums;
	void (*set_otp_timing)(void);
};

static struct fsl_otp_devtype_data *fsl_otp;

/*
 * fsl_otp_bank_physical and fsl_otp_word_physical are used to
 * find the physical index of the word. Only used for calculating
 * offset of the word, means only effective when reading fuse.
 * Do not use the two functions for prog fuse. Always use the word
 * index from fuse map to prog the fuse.
 *
 * Take i.MX6UL for example:
 * there are holes between bank 5 and bank 6. The hole is 0x100 bytes.
 * To bank 15, word 7, the word index is 15 * 8 + 7. The physical word
 * index is 15 * 8 + 0x100 / 0x10 + 7, 0x100 contains 16 words.
 * So use 15 * 8 + 7 to prog the fuse. And when reading, account the hole
 * using offset 0x400 + (15 * 8 + 0x100 / 0x10 + 7) * 0x10.
 *
 * There is a hole in shadow registers address map of size 0x100
 * between bank 5 and bank 6 on iMX6QP, iMX6DQ, iMX6SDL, iMX6SX and iMX6UL.
 * Bank 5 ends at 0x6F0 and Bank 6 starts at 0x800. When reading the fuses,
 * account for this hole in address space.
 *
 * Similar hole exists between bank 14 and bank 15 of size 0x80
 * on iMX6QP, iMX6DQ, iMX6SDL and iMX6SX.
 * Note: iMX6SL has only 0-7 banks and there is no hole.
 * Note: iMX6UL doesn't have this one.
 */
static u32 fsl_otp_bank_physical(struct fsl_otp_devtype_data *d, int bank)
{
	u32 phy_bank;

	if ((bank == 0) || (d->devtype == FSL_OTP_MX6SL) ||
	    (d->devtype == FSL_OTP_MX7D))
		phy_bank = bank;
	else if ((d->devtype == FSL_OTP_MX6UL) || (d->devtype == FSL_OTP_MX6ULL)) {
		if (bank >= 6)
			phy_bank = fsl_otp_bank_physical(d, 5) + bank - 3;
		else
			phy_bank = bank;
	} else {
		if (bank >= 15)
			phy_bank = fsl_otp_bank_physical(d, 14) + bank - 13;
		else if (bank >= 6)
			phy_bank = fsl_otp_bank_physical(d, 5) + bank - 3;
		else
			phy_bank = bank;
	}

	return phy_bank;
}

static u32 fsl_otp_word_physical(struct fsl_otp_devtype_data *d, int index)
{
	u32 phy_bank_off;
	u32 word_off, bank_off;
	u32 words_per_bank;

	if (d->devtype == FSL_OTP_MX7D)
		words_per_bank = 4;
	else
		words_per_bank = 8;

	bank_off = index / words_per_bank;
	word_off = index % words_per_bank;
	phy_bank_off = fsl_otp_bank_physical(d, bank_off);

	return phy_bank_off * words_per_bank + word_off;
}

static void imx6_set_otp_timing(void)
{
	unsigned long clk_rate = 0;
	unsigned long strobe_read, relex, strobe_prog;
	u32 timing = 0;

	clk_rate = clk_get_rate(otp_clk);

	/* do optimization for too many zeros */
	relex = clk_rate / (1000000000 / DEF_RELAX) - 1;
	strobe_prog = clk_rate / (1000000000 / 10000) + 2 * (DEF_RELAX + 1) - 1;
	strobe_read = clk_rate / (1000000000 / 40) + 2 * (DEF_RELAX + 1) - 1;

	timing = BF(relex, OCOTP_TIMING_RELAX);
	timing |= BF(strobe_read, OCOTP_TIMING_STROBE_READ);
	timing |= BF(strobe_prog, OCOTP_TIMING_STROBE_PROG);

	__raw_writel(timing, otp_base + HW_OCOTP_TIMING);
}

static void imx7_set_otp_timing(void)
{
	unsigned long clk_rate;
	u32 fsource, prog;
	u32 timing = 0;
	u32 reg;

	clk_rate = clk_get_rate(otp_clk);

	fsource = DIV_ROUND_UP((clk_rate / 1000) * BV_TIMING_FSOURCE_NS,
			       1000000) + 1;
	prog = DIV_ROUND_CLOSEST(clk_rate * BV_TIMING_PROG_US, 1000000) + 1;
	timing = BF(fsource, TIMING_FSOURCE) | BF(prog, TIMING_PROG);
	reg = __raw_readl(otp_base + HW_OCOTP_TIMING);
	reg &= ~(BM_TIMING_FSOURCE | BM_TIMING_PROG);
	reg |= timing;
	__raw_writel(reg, otp_base + HW_OCOTP_TIMING);
}

static struct fsl_otp_devtype_data imx6q_data = {
	.devtype = FSL_OTP_MX6Q,
	.bank_desc = (const char **)imx6q_otp_desc,
	.fuse_nums = 16 * 8,
	.set_otp_timing = imx6_set_otp_timing,
};

static struct fsl_otp_devtype_data imx6sl_data = {
	.devtype = FSL_OTP_MX6SL,
	.bank_desc = (const char **)imx6sl_otp_desc,
	.fuse_nums = 8 * 8,
	.set_otp_timing = imx6_set_otp_timing,
};

static struct fsl_otp_devtype_data imx6ul_data = {
	.devtype = FSL_OTP_MX6UL,
	.bank_desc = (const char **)imx6ul_otp_desc,
	.fuse_nums = 16 * 8,
	.set_otp_timing = imx6_set_otp_timing,
};

static struct fsl_otp_devtype_data imx6ull_data = {
	.devtype = FSL_OTP_MX6ULL,
	.bank_desc = (const char **)imx6ull_otp_desc,
	/* Bank 7 and Bank 8 are 4 words each */
	.fuse_nums = 8 * 8,
	.set_otp_timing = imx6_set_otp_timing,
};

static struct fsl_otp_devtype_data imx7d_data = {
	.devtype = FSL_OTP_MX7D,
	.bank_desc = (const char **)imx7d_otp_desc,
	.fuse_nums = 16 * 4,
	.set_otp_timing = imx7_set_otp_timing,
};

static int otp_wait_busy(u32 flags)
{
	int count;
	u32 c;

	for (count = 10000; count >= 0; count--) {
		c = __raw_readl(otp_base + HW_OCOTP_CTRL);
		if (!(c & (BM_OCOTP_CTRL_BUSY | BM_OCOTP_CTRL_ERROR | flags)))
			break;
		cpu_relax();
	}

	if (count < 0)
		return -ETIMEDOUT;

	return 0;
}

static ssize_t fsl_otp_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buf)
{
	unsigned int index = attr - otp_kattr;
	unsigned int phy_index;
	u32 value = 0;
	int ret;

	if (!fsl_otp)
		return -ENODEV;

	ret = clk_prepare_enable(otp_clk);
	if (ret)
		return -ENODEV;

	mutex_lock(&otp_mutex);

	phy_index = fsl_otp_word_physical(fsl_otp, index);
	fsl_otp->set_otp_timing();
	ret = otp_wait_busy(0);
	if (ret)
		goto out;

	value = __raw_readl(otp_base + HW_OCOTP_CUST_N(phy_index));

out:
	mutex_unlock(&otp_mutex);
	clk_disable_unprepare(otp_clk);
	return ret ? 0 : sprintf(buf, "0x%x\n", value);
}

static int imx6_otp_write_bits(int addr, u32 data, u32 magic)
{
	u32 c; /* for control register */

	/* init the control register */
	c = __raw_readl(otp_base + HW_OCOTP_CTRL);
	c &= ~BM_OCOTP_CTRL_ADDR;
	c |= BF(addr, OCOTP_CTRL_ADDR);
	c |= BF(magic, OCOTP_CTRL_WR_UNLOCK);
	__raw_writel(c, otp_base + HW_OCOTP_CTRL);

	/* init the data register */
	__raw_writel(data, otp_base + HW_OCOTP_DATA);
	otp_wait_busy(0);

	mdelay(2); /* Write Postamble */

	return 0;
}

static int imx7_otp_write_bits(int addr, u32 data, u32 magic)
{
	u32 c; /* for control register */

	/* init the control register */
	c = __raw_readl(otp_base + HW_OCOTP_CTRL);
	c &= ~BM_OCOTP_CTRL_ADDR_MX7D;
	/* convert to bank address */
	c |= BF((addr >> 2), OCOTP_CTRL_ADDR);
	c |= BF(magic, OCOTP_CTRL_WR_UNLOCK);
	__raw_writel(c, otp_base + HW_OCOTP_CTRL);

	/* init the data register */
	switch (addr & 0x3) {
	case 0:
		__raw_writel(0, otp_base + HW_OCOTP_DATA1_MX7D);
		__raw_writel(0, otp_base + HW_OCOTP_DATA2_MX7D);
		__raw_writel(0, otp_base + HW_OCOTP_DATA3_MX7D);
		__raw_writel(data, otp_base + HW_OCOTP_DATA0_MX7D);
		break;
	case 1:
		__raw_writel(data, otp_base + HW_OCOTP_DATA1_MX7D);
		__raw_writel(0, otp_base + HW_OCOTP_DATA2_MX7D);
		__raw_writel(0, otp_base + HW_OCOTP_DATA3_MX7D);
		__raw_writel(0, otp_base + HW_OCOTP_DATA0_MX7D);
		break;
	case 2:
		__raw_writel(0, otp_base + HW_OCOTP_DATA1_MX7D);
		__raw_writel(data, otp_base + HW_OCOTP_DATA2_MX7D);
		__raw_writel(0, otp_base + HW_OCOTP_DATA3_MX7D);
		__raw_writel(0, otp_base + HW_OCOTP_DATA0_MX7D);
		break;
	case 3:
		__raw_writel(0, otp_base + HW_OCOTP_DATA1_MX7D);
		__raw_writel(0, otp_base + HW_OCOTP_DATA2_MX7D);
		__raw_writel(data, otp_base + HW_OCOTP_DATA3_MX7D);
		__raw_writel(0, otp_base + HW_OCOTP_DATA0_MX7D);
		break;
	}
	__raw_writel(data, otp_base + HW_OCOTP_DATA);
	otp_wait_busy(0);

	mdelay(2); /* Write Postamble */

	return 0;

}

static ssize_t fsl_otp_store(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	unsigned int index = attr - otp_kattr;
	unsigned long value;
	int ret;

	if (!fsl_otp)
		return -ENODEV;

	ret = kstrtoul(buf, 16, &value);
	if (ret < 0)
		return -EINVAL;

	ret = clk_prepare_enable(otp_clk);
	if (ret)
		return -ENODEV;

	mutex_lock(&otp_mutex);

	fsl_otp->set_otp_timing();
	ret = otp_wait_busy(0);
	if (ret)
		goto out;

	if (fsl_otp->devtype == FSL_OTP_MX7D)
		imx7_otp_write_bits(index, value, 0x3e77);
	else
		imx6_otp_write_bits(index, value, 0x3e77);

	/* Reload all the shadow registers */
	__raw_writel(BM_OCOTP_CTRL_RELOAD_SHADOWS,
		     otp_base + HW_OCOTP_CTRL_SET);
	udelay(1);
	otp_wait_busy(BM_OCOTP_CTRL_RELOAD_SHADOWS);

out:
	mutex_unlock(&otp_mutex);
	clk_disable_unprepare(otp_clk);
	return ret ? 0 : count;
}

static const struct of_device_id fsl_otp_dt_ids[] = {
	{ .compatible = "fsl,imx6q-ocotp", .data = (void *)&imx6q_data, },
	{ .compatible = "fsl,imx6sl-ocotp", .data = (void *)&imx6sl_data, },
	{ .compatible = "fsl,imx6ul-ocotp", .data = (void *)&imx6ul_data, },
	{ .compatible = "fsl,imx6ull-ocotp", .data = (void *)&imx6ull_data, },
	{ .compatible = "fsl,imx7d-ocotp", .data = (void *)&imx7d_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_otp_dt_ids);

static int fsl_otp_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct attribute **attrs;
	const char **desc;
	int i, num;
	int ret;
	const struct of_device_id *of_id =
		of_match_device(fsl_otp_dt_ids, &pdev->dev);

	fsl_otp = (struct fsl_otp_devtype_data *)of_id->data;
	if (!fsl_otp) {
		dev_err(&pdev->dev, "No driver data provided!\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	otp_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(otp_base)) {
		ret = PTR_ERR(otp_base);
		dev_err(&pdev->dev, "failed to ioremap resource: %d\n", ret);
		return ret;
	}

	otp_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(otp_clk)) {
		ret = PTR_ERR(otp_clk);
		dev_err(&pdev->dev, "failed to get clock: %d\n", ret);
		return ret;
	}

	desc = fsl_otp->bank_desc;
	num = fsl_otp->fuse_nums;

	/* The last one is NULL, which is used to detect the end */
	attrs = devm_kzalloc(&pdev->dev, (num + 1) * sizeof(*attrs),
			     GFP_KERNEL);
	otp_kattr = devm_kzalloc(&pdev->dev, num * sizeof(*otp_kattr),
				 GFP_KERNEL);
	otp_attr_group = devm_kzalloc(&pdev->dev, sizeof(*otp_attr_group),
				      GFP_KERNEL);
	if (!attrs || !otp_kattr || !otp_attr_group)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		sysfs_attr_init(&otp_kattr[i].attr);
		otp_kattr[i].attr.name = desc[i];
		otp_kattr[i].attr.mode = 0600;
		otp_kattr[i].show = fsl_otp_show;
		otp_kattr[i].store = fsl_otp_store;
		attrs[i] = &otp_kattr[i].attr;
	}
	otp_attr_group->attrs = attrs;

	otp_kobj = kobject_create_and_add("fsl_otp", NULL);
	if (!otp_kobj) {
		dev_err(&pdev->dev, "failed to add kobject\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(otp_kobj, otp_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs group: %d\n", ret);
		kobject_put(otp_kobj);
		return ret;
	}

	mutex_init(&otp_mutex);

	return 0;
}

static int fsl_otp_remove(struct platform_device *pdev)
{
	sysfs_remove_group(otp_kobj, otp_attr_group);
	kobject_put(otp_kobj);

	return 0;
}

static struct platform_driver fsl_otp_driver = {
	.driver		= {
		.name   = "imx-ocotp",
		.owner	= THIS_MODULE,
		.of_match_table = fsl_otp_dt_ids,
	},
	.probe		= fsl_otp_probe,
	.remove		= fsl_otp_remove,
};
module_platform_driver(fsl_otp_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huang Shijie <b32955@freescale.com>");
MODULE_DESCRIPTION("Freescale i.MX OCOTP driver");
