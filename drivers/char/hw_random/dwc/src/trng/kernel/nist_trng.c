// SPDX-License-Identifier: GPL-2.0
/*
 * This Synopsys software and associated documentation (hereinafter the
 * "Software") is an unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you. The
 * Software IS NOT an item of Licensed Software or a Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Products
 * with Synopsys or any supplement thereto. Synopsys is a registered trademark
 * of Synopsys, Inc. Other names included in the SOFTWARE may be the
 * trademarks of their respective owners.
 *
 * The contents of this file are dual-licensed; you may select either version
 * 2 of the GNU General Public License ("GPL") or the BSD-3-Clause license
 * ("BSD-3-Clause"). The GPL is included in the COPYING file accompanying the
 * SOFTWARE. The BSD License is copied below.
 *
 * BSD-3-Clause License:
 * Copyright (c) 2012-2017 Synopsys, Inc. and/or its affiliates.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer, without
 *    modification.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. The names of the above-listed copyright holders may not be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <asm/uaccess.h>
#include <asm/param.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <linux/crypto.h>
#include <crypto/internal/rng.h>

#include "nisttrng.h"

#define SYNOPSYS_HWRNG_DRIVER_NAME "hwrng-nist_trng"

#define num_gen_bytes 64
static unsigned long max_reads = 128;

struct synopsys_nisttrng_driver {
	struct nist_trng_state nisttrng;
	void *hwrng_drv;
	void *crypto_drv;
	unsigned char rand_out[num_gen_bytes];
};

static unsigned int xxd_vtrng;

static void nisttrng_reinit(struct nist_trng_state *nist_trng)
{
	int err;

	err = nisttrng_uninstantiate(nist_trng);
	if (err && err != CRYPTO_NOT_INSTANTIATED)
		goto ERR;

	err = nisttrng_instantiate(nist_trng, 128, 1, NULL);
	if (err)
		goto ERR;

ERR:
	DEBUG("NIST_TRNG:  Trying to reinitialize after a fatal alarm: %d\n",
	      err);
}

static int nisttrng_platform_driver_read(struct platform_device *pdev,
					 void *buf, size_t max, bool wait)
{
	struct synopsys_nisttrng_driver *data = 0;
	int nisttrng_error = -1;
	u32 *out = kmalloc(max, GFP_KERNEL);
	unsigned int vtrng;

	if (!out) {
		SYNHW_PRINT("memory not allocated\n");
		return -1;
	}

	if (!pdev || !buf || !max)
		return nisttrng_error;

	data = platform_get_drvdata(pdev);
	if (data == 0)
		return nisttrng_error;

	if (data->nisttrng.config.build_cfg0.edu_present) {
		vtrng = xxd_vtrng % ((data->nisttrng.config.edu_build_cfg0
					      .public_vtrng_channels) +
				     1);
		if (vtrng == 0) {
			/* private vtrng */
			nisttrng_error = nisttrng_generate(&data->nisttrng, out, max,
							   data->nisttrng.status.sec_strength ? 256 : 128,
							   data->nisttrng.status.pred_resist, NULL);
		} else {
			/* public vtrng */
			nisttrng_error = nisttrng_generate_public_vtrng(&data->nisttrng, out, max, vtrng - 1);
		}
		xxd_vtrng++;
	} else {
		/* nist core vtrng */
		nisttrng_error = nisttrng_generate(&data->nisttrng, out, max,
						   data->nisttrng.status.sec_strength ? 256 : 128,
						   data->nisttrng.status.pred_resist, NULL);
	}
	if (nisttrng_error < 0) {
		if (data->nisttrng.status.alarm_code)
			nisttrng_reinit(&data->nisttrng);

		return nisttrng_error;
	}

	memcpy(buf, out, max);
	kfree(out);

	return max;
}

int nisttrng_hwrng_driver_read(struct hwrng *rng, void *buf, size_t max,
			       bool wait)
{
	struct platform_device *pdev = 0;

	if (rng == 0)
		return -1;

	pdev = (struct platform_device *)rng->priv;
	return nisttrng_platform_driver_read(pdev, buf, max, wait);
}

static ssize_t ckr_show(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "rel_num=%u, ext_ver=%u, ext_enum=%u\n",
		       priv->nisttrng.config.corekit_rel.rel_num,
		       priv->nisttrng.config.corekit_rel.ext_ver,
		       priv->nisttrng.config.corekit_rel.ext_enum);
}

static ssize_t features_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf,
		"drbg_arch = %u, diag_basic_trng=%u, diag_st_hlt=%u, diag_ns=%u, secure_rst_state=%u, extra_ps_present=%u\n",
		priv->nisttrng.config.features.drbg_arch,
		priv->nisttrng.config.features.diag_level_basic_trng,
		priv->nisttrng.config.features.diag_level_stat_hlt,
		priv->nisttrng.config.features.diag_level_ns,
		priv->nisttrng.config.features.secure_rst_state,
		priv->nisttrng.config.features.extra_ps_present);
}

static ssize_t secure_show(struct device *dev, struct device_attribute *devattr,
			   char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", NIST_TRNG_REG_SMODE_GET_SECURE_EN(pdu_io_read32(priv->nisttrng.base +
					NIST_TRNG_REG_SMODE)) ? "on" : "off");
}

static ssize_t secure_store(struct device *dev,
			    struct device_attribute *devattr, const char *buf,
			    size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	int ret;

	ret = nisttrng_set_secure_mode(&priv->nisttrng,
				       sysfs_streq(buf, "on") ? 1 : 0);
	if (ret)
		return -1;

	return count;
}

static ssize_t nonce_show(struct device *dev, struct device_attribute *devattr,
			  char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", NIST_TRNG_REG_SMODE_GET_NONCE(pdu_io_read32(priv->nisttrng.base +
					NIST_TRNG_REG_SMODE)) ? "on" : "off");
}

static ssize_t nonce_store(struct device *dev, struct device_attribute *devattr,
			   const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	int ret;

	ret = nisttrng_set_nonce_mode(&priv->nisttrng,
				      sysfs_streq(buf, "on") ? 1 : 0);
	if (ret)
		return -1;

	return count;
}

static ssize_t sec_strength_show(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n",
		       priv->nisttrng.status.sec_strength ? "256" : "128");
}

static ssize_t sec_strength_store(struct device *dev,
				  struct device_attribute *devattr,
				  const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	int tmp;
	int ret;

	if (count > 8)
		return -1;

	foo[8] = 0;
	memcpy(foo, buf, 8);
	ret = kstrtoint(foo, 10, &tmp);
	if (ret)
		return ret;

	ret = nisttrng_set_sec_strength(&priv->nisttrng, tmp);
	if (ret)
		return -1;

	return count;
}

static ssize_t rand_reg_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	unsigned int x;
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	for (x = 0; x < 4; x++) {
		sprintf(buf + 8 * x, "%08lx",
			pdu_io_read32(priv->nisttrng.base +
				      NIST_TRNG_REG_RAND0 + 3 - x));
	}

	strcat(buf, "\n");
	return strlen(buf);
}

static ssize_t seed_reg_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	unsigned int x;
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	for (x = 0; x < 12; x++) {
		sprintf(buf + 8 * x, "%08lx",
			pdu_io_read32(priv->nisttrng.base +
				      NIST_TRNG_REG_SEED0 + 11 - x));
	}
	strcat(buf, "\n");
	return strlen(buf);
}

static ssize_t seed_reg_store(struct device *dev,
			      struct device_attribute *devattr, const char *buf,
			      size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	unsigned int x, tmp;
	int ret;

	// string must be at least 12 32-bit words long in 0 padded hex
	if (count < (2 * 12 * 4))
		return -1;

	foo[8] = 0;
	for (x = 0; x < 12; x++) {
		memcpy(foo, buf + x * 8, 8);
		ret = kstrtouint(foo, 16, &tmp);
		if (ret)
			return ret;

		pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_SEED0 + x,
			       tmp);
	}

	return count;
}

static ssize_t npa_data_reg_show(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	unsigned int x;
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	for (x = 0; x < 16; x++) {
		sprintf(buf + 8 * x, "%08lx",
			pdu_io_read32(priv->nisttrng.base +
				      NIST_TRNG_REG_NPA_DATA0 + 15 - x));
	}

	strcat(buf, "\n");
	return strlen(buf);
}

static ssize_t npa_data_reg_store(struct device *dev,
				  struct device_attribute *devattr,
				  const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	unsigned int x, tmp;
	int ret;

	// string must be at least 16 32-bit words long in 0 padded hex
	if (count < (2 * 16 * 4))
		return -1;

	foo[8] = 0;
	for (x = 0; x < 16; x++) {
		memcpy(foo, buf + x * 8, 8);
		ret = kstrtouint(foo, 16, &tmp);
		if (ret)
			return ret;

		pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_NPA_DATA0 + x, tmp);
	}

	return count;
}

static ssize_t ctrl_reg_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		       pdu_io_read32(priv->nisttrng.base + NIST_TRNG_REG_CTRL));
}

static ssize_t ctrl_reg_store(struct device *dev,
			      struct device_attribute *devattr, const char *buf,
			      size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	unsigned int tmp;
	int ret;

	// string must be at least a 32-bit word in 0 padded hex
	if (count < 8)
		return -1;

	foo[8] = 0;
	memcpy(foo, buf, 8);
	ret = kstrtouint(foo, 16, &tmp);
	if (ret)
		return ret;

	pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_CTRL, tmp);
	return count;
}

static ssize_t istat_reg_show(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		pdu_io_read32(priv->nisttrng.base + NIST_TRNG_REG_ISTAT));
}

static ssize_t istat_reg_store(struct device *dev,
			       struct device_attribute *devattr,
			       const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	unsigned int tmp;
	int ret;

	// string must be at least a 32-bit word in 0 padded hex
	if (count < 8)
		return -1;

	foo[8] = 0;
	memcpy(foo, buf, 8);
	ret = kstrtouint(foo, 16, &tmp);
	if (ret)
		return ret;

	pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_ISTAT, tmp);
	return count;
}

static ssize_t mode_reg_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		       pdu_io_read32(priv->nisttrng.base + NIST_TRNG_REG_MODE));
}

static ssize_t mode_reg_store(struct device *dev,
			      struct device_attribute *devattr, const char *buf,
			      size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	unsigned int tmp;
	int ret;

	// string must be at least a 32-bit word in 0 padded hex
	if (count < 8)
		return -1;

	foo[8] = 0;
	memcpy(foo, buf, 8);
	ret = kstrtouint(foo, 16, &tmp);
	if (ret)
		return ret;

	pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_MODE, tmp);

	return count;
}

static ssize_t smode_reg_show(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		pdu_io_read32(priv->nisttrng.base + NIST_TRNG_REG_SMODE));
}

static ssize_t smode_reg_store(struct device *dev,
			       struct device_attribute *devattr,
			       const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	unsigned int tmp;
	int ret;

	// string must be at least a 32-bit word in 0 padded hex
	if (count < 8)
		return -1;

	foo[8] = 0;
	memcpy(foo, buf, 8);
	ret = kstrtouint(foo, 16, &tmp);
	if (ret)
		return ret;

	pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_SMODE, tmp);
	return count;
}

static ssize_t alarm_reg_show(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		pdu_io_read32(priv->nisttrng.base + NIST_TRNG_REG_ALARM));
}

static ssize_t alarm_reg_store(struct device *dev,
			       struct device_attribute *devattr,
			       const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	unsigned int tmp;
	int ret;

	// string must be at least a 32-bit word in 0 padded hex
	if (count < 8)
		return -1;

	foo[8] = 0;
	memcpy(foo, buf, 8);
	ret = kstrtouint(foo, 16, &tmp);
	if (ret)
		return ret;

	pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_ALARM, tmp);
	return count;
}

static ssize_t stat_reg_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		       pdu_io_read32(priv->nisttrng.base + NIST_TRNG_REG_STAT));
}

static ssize_t ia_wdata_reg_store(struct device *dev,
				  struct device_attribute *devattr,
				  const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	unsigned int tmp;
	int ret;

	// string must be at least a 32-bit word in 0 padded hex
	if (count < 8)
		return -1;

	foo[8] = 0;
	memcpy(foo, buf, 8);
	ret = kstrtouint(foo, 16, &tmp);
	if (ret)
		return ret;

	pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_IA_WDATA, tmp);
	return count;
}

static ssize_t ia_wdata_reg_show(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		pdu_io_read32(priv->nisttrng.base + NIST_TRNG_REG_IA_WDATA));
}

static ssize_t ia_rdata_reg_show(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		pdu_io_read32(priv->nisttrng.base + NIST_TRNG_REG_IA_RDATA));
}

static ssize_t ia_addr_reg_store(struct device *dev,
				 struct device_attribute *devattr,
				 const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	unsigned int tmp;
	int ret;

	// string must be at least a 32-bit word in 0 padded hex
	if (count < 8)
		return -1;

	foo[8] = 0;
	memcpy(foo, buf, 8);
	ret = kstrtouint(foo, 16, &tmp);
	if (ret)
		return ret;

	pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_IA_ADDR, tmp);
	return count;
}

static ssize_t ia_addr_reg_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		pdu_io_read32(priv->nisttrng.base + NIST_TRNG_REG_IA_ADDR));
}

static ssize_t ia_cmd_reg_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	unsigned int tmp;
	int ret;

	// string must be at least a 32-bit word in 0 padded hex
	if (count < 8)
		return -1;

	foo[8] = 0;
	memcpy(foo, buf, 8);
	ret = kstrtouint(foo, 16, &tmp);
	if (ret)
		return ret;

	pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_IA_CMD, tmp);
	return count;
}

static ssize_t ia_cmd_reg_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		pdu_io_read32(priv->nisttrng.base + NIST_TRNG_REG_IA_CMD));
}

static ssize_t rnc_reg_show(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		pdu_io_read32(priv->nisttrng.base + NIST_TRNG_EDU_RNC_CTRL));
}

static ssize_t rnc_reg_store(struct device *dev,
			     struct device_attribute *devattr, const char *buf,
			     size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	unsigned int tmp;
	int ret;

	// string must be at least a 32-bit word in 0 padded hex
	if (count < 8)
		return -1;

	foo[8] = 0;
	memcpy(foo, buf, 8);
	ret = kstrtouint(foo, 16, &tmp);
	if (ret)
		return ret;

	pdu_io_write32(priv->nisttrng.base + NIST_TRNG_EDU_RNC_CTRL, tmp);

	return count;
}

static ssize_t rbc_reg_show(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		pdu_io_read32(priv->nisttrng.base + NIST_TRNG_EDU_RBC_CTRL));
}

static ssize_t rbc_reg_store(struct device *dev,
			     struct device_attribute *devattr, const char *buf,
			     size_t count)
{
	char opts_str[5];
	unsigned int opts_int;
	int enable, rbc_num, rate, urun_blnk, ret;
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	opts_str[4] = 0;
	memcpy(opts_str, buf, 4);
	ret = kstrtouint(opts_str, 16, &opts_int);
	if (ret)
		return ret;

	SYNHW_PRINT("%s %x\n", __func__, opts_int);

	enable = (opts_int >> 12 & 0xf);
	if (enable > 1) {
		SYNHW_PRINT("incorrect enable  %x\n", enable);
		return -1;
	}

	rbc_num = (opts_int >> 8 & 0xf);
	if (rbc_num > priv->nisttrng.config.edu_build_cfg0.rbc_channels - 1) {
		SYNHW_PRINT("incorrect rbc_num  %x\n", rbc_num);
		return -1;
	}

	rate = (opts_int >> 4 & 0xf);
	if (rate > 8) {
		SYNHW_PRINT("incorrect rate  %x\n", rate);
		return -1;
	}

	urun_blnk = (opts_int & 0xf);
	if (urun_blnk > 3) {
		SYNHW_PRINT("incorrect urun_blnk  %x\n", urun_blnk);
		return -1;
	}

	SYNHW_PRINT("enable %x rbc_num %x  rate %x urun_blnk  %x\n", enable,
		    rbc_num, rate, urun_blnk);

	ret = nisttrng_rbc(&priv->nisttrng, enable, rbc_num, rate,
			   urun_blnk);
	if (ret)
		return -1;

	return count;
}

static ssize_t hw_state_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	u32 addr;
	int i;
	int tot_char;

	addr = 0x20;
	tot_char = sprintf(buf, "Key = ");
	for (i = 0; i < 8; i++) {
		pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_IA_ADDR,
			       addr + 7 - i);
		pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_IA_CMD,
			       0x80000000);
		tot_char += sprintf(buf + tot_char, "%08lx",
				    pdu_io_read32(priv->nisttrng.base +
						  NIST_TRNG_REG_IA_RDATA));
	}
	tot_char += sprintf(buf + tot_char, "\n");

	addr = 0x28;
	tot_char += sprintf(buf + tot_char, "V = ");
	for (i = 0; i < 4; i++) {
		pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_IA_ADDR,
			       addr + 3 - i);
		pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_IA_CMD,
			       0x80000000);
		tot_char += sprintf(buf + tot_char, "%08lx",
				    pdu_io_read32(priv->nisttrng.base +
						  NIST_TRNG_REG_IA_RDATA));
	}

	tot_char += sprintf(buf + tot_char, "\n");

	return tot_char;
}

static ssize_t max_bits_per_req_store(struct device *dev,
				      struct device_attribute *devattr,
				      const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	unsigned long tmp;
	int ret;

	// string must be at least a 32-bit word in 0 padded hex
	if (count < 8)
		return -1;

	foo[8] = 0;
	memcpy(foo, buf, 8);
	ret = kstrtoul(foo, 16, &tmp);
	if (ret)
		return ret;

	ret = nisttrng_set_reminder_max_bits_per_req(&priv->nisttrng,
						     tmp);
	if (ret)
		return -1;

	return count;
}

static ssize_t max_bits_per_req_show(struct device *dev,
				     struct device_attribute *devattr,
				     char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08lx\n",
		       priv->nisttrng.counters.max_bits_per_req);
}

static ssize_t max_req_per_seed_store(struct device *dev,
				      struct device_attribute *devattr,
				      const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[17];
	unsigned long long tmp;
	int ret;

	// string must be at least a 64-bit word in 0 padded hex
	if (count < 16)
		return -1;

	foo[16] = 0;
	memcpy(foo, buf, 16);
	ret = kstrtoull(foo, 16, &tmp);
	if (ret)
		return ret;

	ret = nisttrng_set_reminder_max_req_per_seed(&priv->nisttrng,
						     tmp);
	if (ret)
		return -1;

	return count;
}

static ssize_t max_req_per_seed_show(struct device *dev,
				     struct device_attribute *devattr,
				     char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	return sprintf(buf, "%08llx\n",
		       priv->nisttrng.counters.max_req_per_seed);
}

static ssize_t collect_ent_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	int rep;
	int i, j;
	int ret;
	u32 tmp;
	int t;

	t = NIST_TRNG_RETRY_MAX;

	// Change to TEST mode
	DEBUG("Change to TEST mode\n");
	pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_SMODE, 0x00000028);
	// Turn on the noise collect mode
	DEBUG("Turn on the noise collect mode\n");
	pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_SMODE, 0x80000028);

	// issue generate entropy command
	DEBUG("Issue a GEN_NOISE command\n");
	pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_CTRL,
		       NIST_TRNG_REG_CTRL_CMD_GEN_NOISE);

	// read raw noise
	// 2 reads if sec_strength is 128 and 3 reads if it is 256
	if (priv->nisttrng.status.sec_strength == SEC_STRNT_AES128)
		rep = 2;
	else if (priv->nisttrng.status.sec_strength == SEC_STRNT_AES256)
		rep = 3;

	for (i = 0; i < rep; i++) {
		t = NIST_TRNG_RETRY_MAX;
		tmp = 0;
		DEBUG("Wait for NOISE_RDY interrupt.\n");
		do {
			tmp = pdu_io_read32(priv->nisttrng.base +
					    NIST_TRNG_REG_ISTAT);
		} while (!(tmp & (NIST_TRNG_REG_ISTAT_NOISE_RDY |
				  NIST_TRNG_REG_ISTAT_ALARMS)) &&
			 --t);

		DEBUG("Read NPA_DATAx\n");
		for (j = 0; j < 16; j++) {
			sprintf(buf + 128 * i + 8 * j, "%08lx",
				pdu_io_read32(priv->nisttrng.base +
					      NIST_TRNG_REG_NPA_DATA0 + j));
		}

		// clear NOISE_RDY IRQ
		DEBUG("Clear NOISE_RDY interrupt.\n");
		ret = nisttrng_wait_on_noise_rdy(&priv->nisttrng);
		if (ret)
			return -1;
	}

	DEBUG("Wait for DONE\n");
	ret = nisttrng_wait_on_done(&priv->nisttrng);
	if (ret)
		return -1;

	strcat(buf, "\n");
	return strlen(buf);
}

static ssize_t collect_ent_nsout_show(struct device *dev,
				      struct device_attribute *devattr,
				      char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	int rep;
	int i;
	int ret;

	// generate entropy
	ret = nisttrng_get_entropy_input(&priv->nisttrng, NULL, 0);
	if (ret)
		return -1;

	// read NS_OUTPUTx
	// 32 reads if sec_strength is 128 and 48 reads if it is 256
	if (priv->nisttrng.status.sec_strength == SEC_STRNT_AES128)
		rep = 32;
	else if (priv->nisttrng.status.sec_strength == SEC_STRNT_AES256)
		rep = 48;

	for (i = 0; i < rep; i++) {
		pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_IA_ADDR,
			       0x70 + rep - 1 - i);
		pdu_io_write32(priv->nisttrng.base + NIST_TRNG_REG_IA_CMD,
			       0x80000000);
		sprintf(buf + 8 * i, "%08lx",
			pdu_io_read32(priv->nisttrng.base +
				      NIST_TRNG_REG_IA_RDATA));
	}

	strcat(buf, "\n");
	return strlen(buf);
}

static ssize_t nonce_seed_with_df_store(struct device *dev,
					struct device_attribute *devattr,
					const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	u32 seed[48] = { 0 };
	int rep;
	int i;
	int ret;

	if (priv->nisttrng.status.sec_strength == SEC_STRNT_AES128)
		rep = 2;
	else if (priv->nisttrng.status.sec_strength == SEC_STRNT_AES256)
		rep = 3;

	DEBUG("Number of char in input = %zu\n", count);
	if (count != (rep * 128))
		return -1;

	foo[8] = 0;
	for (i = 0; i < (rep * 16); i++) {
		memcpy(foo, buf + i * 8, 8);
		ret = kstrtouint(foo, 16, (seed + (rep * 16 - 1) - i));
		if (ret)
			return ret;
	}

	ret = nisttrng_get_entropy_input(&priv->nisttrng, seed, 1);
	if (ret)
		return -1;

	return count;
}

static ssize_t nonce_seed_direct_store(struct device *dev,
				       struct device_attribute *devattr,
				       const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char foo[9];
	u32 seed[12] = { 0 };
	int rep;
	int i;
	int ret;

	if (priv->nisttrng.status.sec_strength == SEC_STRNT_AES128)
		rep = 2;
	else if (priv->nisttrng.status.sec_strength == SEC_STRNT_AES256)
		rep = 3;

	DEBUG("Number of char in input = %zu\n", count);
	if (count != (rep * 32))
		return -1;

	foo[8] = 0;
	for (i = 0; i < (rep * 4); i++) {
		memcpy(foo, buf + i * 8, 8);
		ret = kstrtouint(foo, 16, (seed + (rep * 4 - 1) - i));
		if (ret)
			return ret;
	}

	ret = nisttrng_get_entropy_input(&priv->nisttrng, seed, 0);
	if (ret)
		return -1;

	return count;
}

static ssize_t instantiate_store(struct device *dev,
				 struct device_attribute *devattr,
				 const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char opts_str[101];
	unsigned int opts_int;
	int req_sec_strength = 256;
	int pred_resist = 1;
	bool ps_exists = 0;
	u32 ps[12];
	unsigned int ps_length;
	int i;
	int ret;

	/* First 3 digits:
	 * they have to be 0 or 1
	 * 2-1-0 --> 2: predictoin resistance, 1: security strength, 0: personilizatoin string existence
	 */
	opts_str[3] = 0;
	memcpy(opts_str, buf, 3);
	ret = kstrtouint(opts_str, 2, &opts_int);
	if (ret)
		return ret;

	if (((opts_str[0] != '0') && (opts_str[0] != '1')) ||
	    ((opts_str[1] != '0') && (opts_str[1] != '1')) ||
	    ((opts_str[2] != '0') && (opts_str[2] != '1'))) {
		SYNHW_PRINT("Invalid input options: First 3 digits can only be 1 or 0\n");
		return -1;
	}

	if (opts_int & 1)
		ps_exists = 1;
	else
		ps_exists = 0;

	if (opts_int & 2)
		req_sec_strength = 256;
	else
		req_sec_strength = 128;

	if (opts_int & 4)
		pred_resist = 1;
	else
		pred_resist = 0;

	/* check input option length */
	if (!ps_exists) {
		if (count != 3) {
			SYNHW_PRINT("Invalid input options: If personilization string does not exist, options has to be 3 char.\n");
			return -1;
		}
	} else {
		if (req_sec_strength == 128) {
			if (count != 64 + 4) { // +4 for options and "-"
				SYNHW_PRINT("Invalid input options: If personilization string exists and security strength is 128-bit, options has to be 68 char (not %zu char).\n",
					    count);
				return -1;
			}
		} else if (req_sec_strength == 256) {
			if (count !=
			    96 + 4) { // +4 for options and "-", +1 because of the termination char that count includes
				SYNHW_PRINT("Invalid input options: If personilization string exists and security strength is 256-bit, options has to be 100 char (not %zu char).\n",
					    count);
				return -1;
			}
		} else {
			SYNHW_PRINT("Invalid input options\n");
			return -1;
		}
	}

	/* Personilization string */
	for (i = 0; i < 12; i++)
		ps[i] = 0;

	if (req_sec_strength == 128)
		ps_length = 64;
	else if (req_sec_strength == 256)
		ps_length = 96;
	else
		SYNHW_PRINT("Invalid security strength\n");

	if (ps_exists) {
		opts_str[1] = 0;
		memcpy(opts_str, buf + 3, 1);

		if (opts_str[0] == '-') {
			opts_str[8] = 0;
			for (i = 0; i < ps_length / 8; i++) {
				memcpy(opts_str, buf + 4 + i * 8, 8);
				ret = kstrtouint(opts_str, 16,
						 ps + (ps_length / 8 - 1) - i);
				if (ret)
					return ret;
			}
		} else {
			SYNHW_PRINT("4th character of input has to be \"-\" when personilization string exists\n");
		}

		ret = nisttrng_instantiate(&priv->nisttrng,
					   req_sec_strength, pred_resist,
					   ps);
		if (ret)
			return -1;

	} else {
		ret = nisttrng_instantiate(&priv->nisttrng,
					   req_sec_strength, pred_resist,
					   NULL);
		if (ret)
			return -1;
	}

	return count;
}

static ssize_t uninstantiate_store(struct device *dev,
				   struct device_attribute *devattr,
				   const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);

	nisttrng_uninstantiate(&priv->nisttrng);

	return count;
}

static ssize_t reseed_store(struct device *dev, struct device_attribute *devattr,
			    const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char opts_str[100];
	unsigned int opts_int;
	int pred_resist = 1;
	bool addin_exists = 0;
	u32 addin[12];
	unsigned int addin_length;
	int i;
	int ret;

	/* First 2 digits:
	 * they have to be 0 or 1
	 * 1-0 --> 1: predictoin resistance, 0: additional input string existence
	 */
	opts_str[2] = 0;
	memcpy(opts_str, buf, 2);
	ret = kstrtouint(opts_str, 2, &opts_int);
	if (ret)
		return ret;

	if (((opts_str[0] != '0') && (opts_str[0] != '1')) ||
	    ((opts_str[1] != '0') && (opts_str[1] != '1'))) {
		SYNHW_PRINT("Invalid input options: First 2 digits can only be 1 or 0\n");
		return -1;
	}

	if (opts_int & 1)
		addin_exists = 1;
	else
		addin_exists = 0;

	if (opts_int & 2)
		pred_resist = 1;
	else
		pred_resist = 0;

	/* check input option length */
	if (!addin_exists) {
		if (count != 2) {
			SYNHW_PRINT("Invalid input options: If additional input does not exist, options has to be 2 char.\n");
			return -1;
		}
	} else {
		if (priv->nisttrng.status.sec_strength == SEC_STRNT_AES128) {
			if (count != 64 + 3) { // +3 for options and "-"
				SYNHW_PRINT("Invalid input options: If additional input exists and security strength is 128-bit, options has to be 67 char.\n");
				return -1;
			}
		} else if (priv->nisttrng.status.sec_strength ==
			   SEC_STRNT_AES256) {
			if (count != 96 + 3) { // +3 for options and "-"
				SYNHW_PRINT("Invalid input options: If additional input exists and security strength is 256-bit, options has to be 99 char.\n");
				return -1;
			}
		} else {
			SYNHW_PRINT("Invalid input options\n");
			return -1;
		}
	}

	/* Additional input */
	for (i = 0; i < 12; i++)
		addin[i] = 0;

	if (priv->nisttrng.status.sec_strength == SEC_STRNT_AES128)
		addin_length = 64;
	else if (priv->nisttrng.status.sec_strength == SEC_STRNT_AES256)
		addin_length = 96;
	else
		SYNHW_PRINT("Invalid security strength\n");

	if (addin_exists) {
		opts_str[1] = 0;
		memcpy(opts_str, buf + 2, 1);

		if (opts_str[0] == '-') {
			opts_str[8] = 0;
			for (i = 0; i < addin_length / 8; i++) {
				memcpy(opts_str, buf + 3 + i * 8, 8);
				ret = kstrtouint(opts_str, 16, addin + (addin_length / 8 - 1) - i);
				if (ret)
					return ret;
			}
		} else {
			SYNHW_PRINT("3rd character of input has to be \"-\" when additional input exists\n");
		}

		ret = nisttrng_reseed(&priv->nisttrng, pred_resist,
				      addin);
		if (ret)
			return -1;

	} else {
		ret = nisttrng_reseed(&priv->nisttrng, pred_resist,
				      NULL);
		if (ret)
			return -1;
	}

	return count;
}

static ssize_t generate_store(struct device *dev,
			      struct device_attribute *devattr, const char *buf,
			      size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char opts_str[101];
	unsigned int opts_int;
	int req_sec_strength = 128;
	int pred_resist = 1;
	bool addin_exists = 0;
	unsigned char out[num_gen_bytes];
	u32 addin[12];
	unsigned int addin_length;
	int i;
	int ret;

	/* First 3 digits:
	 * they have to be 0 or 1
	 * 2-1-0 --> 2: predictoin resistance, 1: security strength, 0: additional input string existence
	 */
	opts_str[3] = 0;
	memcpy(opts_str, buf, 3);
	ret = kstrtouint(opts_str, 2, &opts_int);
	if (ret)
		return ret;

	if (((opts_str[0] != '0') && (opts_str[0] != '1')) ||
	    ((opts_str[1] != '0') && (opts_str[1] != '1')) ||
	    ((opts_str[2] != '0') && (opts_str[2] != '1'))) {
		SYNHW_PRINT("Invalid input options: First 3 digits can only be 1 or 0\n");
		return -1;
	}

	if (opts_int & 1)
		addin_exists = 1;
	else
		addin_exists = 0;

	if (opts_int & 2)
		req_sec_strength = 256;
	else
		req_sec_strength = 128;

	if (opts_int & 4)
		pred_resist = 1;
	else
		pred_resist = 0;

	/* check input option length */
	if (!addin_exists) {
		if (count != 3) {
			SYNHW_PRINT("Invalid input options: If additional input does not exist, options has to be 3 char.\n");
			return -1;
		}
	} else {
		if (req_sec_strength == 128) {
			if (count != 64 + 4) { // +4 for options and "-"
				SYNHW_PRINT("Invalid input options: If additional input exists and security strength is 128-bit, options has to be 68 char.\n");
				return -1;
			}
		} else if (req_sec_strength == 256) {
			if (count != 96 + 4) { // +4 for options and "-"
				SYNHW_PRINT("Invalid input options: If additional input exists and security strength is 256-bit, options has to be 100 char.\n");
				return -1;
			}
		} else {
			SYNHW_PRINT("Invalid input options\n");
			return -1;
		}
	}

	/* Additional input */
	for (i = 0; i < 12; i++)
		addin[i] = 0;

	if (req_sec_strength == 128)
		addin_length = 64;
	else if (req_sec_strength == 256)
		addin_length = 96;
	else
		SYNHW_PRINT("Invalid security strength\n");

	if (addin_exists) {
		opts_str[1] = 0;
		memcpy(opts_str, buf + 3, 1);

		if (opts_str[0] == '-') {
			opts_str[8] = 0;
			for (i = 0; i < addin_length / 8; i++) {
				memcpy(opts_str, buf + 4 + i * 8, 8);
				ret = kstrtouint(opts_str, 16, addin + (addin_length / 8 - 1) - i);
				if (ret)
					return ret;
			}
		} else {
			SYNHW_PRINT("4th character of input has to be \"-\" when additional input exists\n");
		}

		ret = nisttrng_generate(&priv->nisttrng, (u32 *)out,
					num_gen_bytes, req_sec_strength,
					pred_resist, addin);
		if (ret)
			return -1;

	} else {
		ret = nisttrng_generate(&priv->nisttrng, (u32 *)out,
					num_gen_bytes, req_sec_strength,
					pred_resist, NULL);
		if (ret)
			return -1;
	}

	/* store the result */
	memcpy(priv->rand_out, out, sizeof(out));

	return count;
}

static ssize_t generate_pub_vtrng_store(struct device *dev,
					struct device_attribute *devattr,
					const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	char opts_str[2];
	unsigned int opts_int;
	unsigned char out[num_gen_bytes];
	int ret;

	opts_str[1] = 0;
	memcpy(opts_str, buf, 1);
	ret = kstrtouint(opts_str, 16, &opts_int);
	if (ret)
		return ret;

	SYNHW_PRINT("%s %d %d %d %d\n", __func__, opts_str[0],
		    priv->nisttrng.config.edu_build_cfg0.public_vtrng_channels,
		    opts_str[1], opts_int);

	ret = nisttrng_generate_public_vtrng(&priv->nisttrng,
					     (u32 *)out,
					     num_gen_bytes, opts_int);
	if (ret)
		return -1;

	memcpy(priv->rand_out, out, sizeof(out));

	return count;
}

/* rand_out_show displays last generated random number (num_gen_bytes number of bytes), not just the last block. */
static ssize_t rand_out_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	unsigned int i, j;
	unsigned long rand;
	bool all_zero = true;

	/* If all bits of the rand_reg register are 0, display 0 */
	for (i = 0; i < 4; i++) {
		rand = pdu_io_read32(priv->nisttrng.base + NIST_TRNG_REG_RAND0 +
				     (3 - i));
		if (rand != 0) {
			all_zero = false;
			break;
		}
	}

	if (all_zero) {
		sprintf(buf + 2 * i, "%02x", 0);
	} else {
		for (i = 0; i < (num_gen_bytes / 16); i++) {
			for (j = 0; j < 16; j++) {
				sprintf(buf + 2 * (i * 16 + j), "%02x",
					priv->rand_out[(i + 1) * 16 - 1 - j]);
			}
		}
		j = 0;
		while (i * 16 + j < num_gen_bytes) {
			sprintf(buf + 2 * (i * 16 + j), "%02x",
				priv->rand_out[num_gen_bytes - 1 - j]);
			j++;
		}
	}

	strcat(buf, "\n");
	return strlen(buf);
}

/* rand_out_vtrng_show displays last generated random number (num_gen_bytes number of bytes), not just the last block. */
static ssize_t rand_out_vtrng_show(struct device *dev,
				   struct device_attribute *devattr, char *buf)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	unsigned int i, j;

	/* If all bits of the rand_reg register are 0, display 0 */

	for (i = 0; i < (num_gen_bytes / 16); i++) {
		for (j = 0; j < 16; j++) {
			sprintf(buf + 2 * (i * 16 + j), "%02x",
				priv->rand_out[(i + 1) * 16 - 1 - j]);
		}
	}

	j = 0;
	while (i * 16 + j < num_gen_bytes) {
		sprintf(buf + 2 * (i * 16 + j), "%02x",
			priv->rand_out[num_gen_bytes - 1 - j]);
		j++;
	}

	strcat(buf, "\n");
	return strlen(buf);
}

static ssize_t kat_store(struct device *dev, struct device_attribute *devattr,
			 const char *buf, size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	int ret;

	if (sysfs_streq(buf, "full")) {
		ret = nisttrng_full_kat(&priv->nisttrng);
		if (ret)
			return -1;

	} else if (sysfs_streq(buf, "00")) {
		ret = nisttrng_kat(&priv->nisttrng, 0, 0);
		if (ret)
			return -1;

	} else if (sysfs_streq(buf, "01")) {
		ret = nisttrng_kat(&priv->nisttrng, 0, 1);
		if (ret)
			return -1;

	} else if (sysfs_streq(buf, "10")) {
		ret = nisttrng_kat(&priv->nisttrng, 1, 0);
		if (ret)
			return -1;

	} else if (sysfs_streq(buf, "11")) {
		ret = nisttrng_kat(&priv->nisttrng, 1, 1);
		if (ret)
			return -1;

	} else {
		ret = nisttrng_full_kat(&priv->nisttrng);
		if (ret)
			return -1;
	}

	return count;
}

static void str_to_384_bit(char *buf, u32 *out)
{
	char foo[9];
	int i;
	int ret;

	foo[8] = 0;
	for (i = 0; i < 12; i++) {
		memcpy(foo, buf + i * 8, 8);
		ret = kstrtouint(foo, 16, out + 11 - i);
	}
}

/* This attribute is only for test purpuses */
static ssize_t test_attr_store(struct device *dev,
			       struct device_attribute *devattr, const char *buf,
			       size_t count)
{
	struct synopsys_nisttrng_driver *priv = dev_get_drvdata(dev);
	int i;
	int err;
	u32 addin[12];
	u32 ps[12];
	char *out;

	char buf_seed1[96] =
		"c54805274bde00aa5289e0513579019707666d2fa7a1c8908865891c87c0c652335a4d3cc415bc30742b164647f8820f";
	char buf_ps1[96] =
		"d63fb5afa2101fa4b8a6c3b89d9c250ac728fc1ddad0e7585b5d54728ed20c2f940e89155596e3b963635b6d6088164b";
	char buf_addin1[96] =
		"744bfae3c23a5cc9a3b373b6c50795068d35eb8a339746ac810d16f864e880061082edf9d2687c211960aa83400f85f9";
	char buf_seed2[96] =
		"b2ad31d1f20dcf30dd526ec9156c07f270216bdb59197325bab180675929888ab699c54fb21819b7d921d6346bff2f7f";
	char buf_addin2[96] =
		"ad55c682962aa4fe9ebc227c9402e79b0aa7874844d33eaee7e2d15baf81d9d33936e4d93f28ad109657b512aee115a5";
	char buf_seed3[96] =
		"eca449048d26fd38f8ca435237dce66eadec7069ee5dd0b70084b819a711c0820a7556bbd0ae20f06e5169278b593b71";
	u32 tmp[12];

	for (i = 0; i < 12; i++)
		addin[i] = i;

	for (i = 0; i < 12; i++)
		ps[i] = i + 100;

	/* SDK doc example - Prediction Resistance not available, no Reseed */
	err = nisttrng_uninstantiate(&priv->nisttrng);
	if (err && err != CRYPTO_NOT_INSTANTIATED)
		return -1;

	if (nisttrng_instantiate(&priv->nisttrng, 128, 0, ps) < 0)
		return -1;

	out = kmalloc(10, GFP_KERNEL);
	if (nisttrng_generate(&priv->nisttrng, out, 10, 128, 0, addin) < 0)
		return -1;

	DEBUG("----- Generate 10 bytes\n");
	for (i = 0; i < 10; i++)
		DEBUG("%02x", out[i]);

	DEBUG("\n");
	kfree(out);

	out = kmalloc(512, GFP_KERNEL);
	if (nisttrng_generate(&priv->nisttrng, out, 512, 128, 0, addin) < 0)
		return -1;

	DEBUG("----- Generate 512 bytes\n");
	for (i = 0; i < 512; i++)
		DEBUG("%02x", out[i]);

	DEBUG("\n");
	kfree(out);

	out = kmalloc(41, GFP_KERNEL);
	if (nisttrng_generate(&priv->nisttrng, out, 41, 128, 0, addin) < 0)
		return -1;

	DEBUG("----- Generate 41 bytes\n");
	for (i = 0; i < 41; i++)
		DEBUG("%02x", out[i]);

	DEBUG("\n");
	kfree(out);

	err = nisttrng_uninstantiate(&priv->nisttrng);
	if (err < 0 && err != CRYPTO_NOT_INSTANTIATED)
		return -1;

	/* SDK doc example - DRBG Validation */
	err = nisttrng_uninstantiate(&priv->nisttrng);
	if (err && err != CRYPTO_NOT_INSTANTIATED)
		return -1;

	if (nisttrng_set_nonce_mode(&priv->nisttrng, 1) < 0)
		return -1;

	out = kmalloc(64, GFP_KERNEL);
	str_to_384_bit(buf_seed1, tmp);
	if (nisttrng_get_entropy_input(&priv->nisttrng, tmp, 0) < 0)
		return -1;

	str_to_384_bit(buf_ps1, tmp);
	if (nisttrng_instantiate(&priv->nisttrng, 256, 1, tmp) < 0)
		return -1;

	str_to_384_bit(buf_seed2, tmp);
	if (nisttrng_get_entropy_input(&priv->nisttrng, tmp, 0) < 0)
		return -1;

	str_to_384_bit(buf_addin1, tmp);
	if (nisttrng_generate(&priv->nisttrng, out, 64, 256, 1, tmp) < 0)
		return -1;

	str_to_384_bit(buf_seed3, tmp);
	if (nisttrng_get_entropy_input(&priv->nisttrng, tmp, 0) < 0)
		return -1;

	str_to_384_bit(buf_addin2, tmp);
	if (nisttrng_generate(&priv->nisttrng, out, 64, 256, 1, tmp) < 0)
		return -1;

	memcpy(priv->rand_out, out, 64);

	return count;
}

static DEVICE_ATTR_RO(ckr);
static DEVICE_ATTR_RO(features);
static DEVICE_ATTR_RW(secure);
static DEVICE_ATTR_RW(nonce);
static DEVICE_ATTR_RW(sec_strength);

static DEVICE_ATTR_RW(mode_reg);
static DEVICE_ATTR_RW(smode_reg);
static DEVICE_ATTR_RW(alarm_reg);
static DEVICE_ATTR_RO(rand_reg);
static DEVICE_ATTR_RO(rand_out);
static DEVICE_ATTR_RO(rand_out_vtrng);
static DEVICE_ATTR_RW(seed_reg);
static DEVICE_ATTR_RW(npa_data_reg);
static DEVICE_ATTR_RW(ctrl_reg);
static DEVICE_ATTR_RW(istat_reg);
static DEVICE_ATTR_RO(stat_reg);
static DEVICE_ATTR_RW(rnc_reg);
static DEVICE_ATTR_RW(rbc_reg);

static DEVICE_ATTR_RW(ia_wdata_reg);
static DEVICE_ATTR_RO(ia_rdata_reg);
static DEVICE_ATTR_RW(ia_addr_reg);
static DEVICE_ATTR_RW(ia_cmd_reg);
static DEVICE_ATTR_RO(hw_state);

static DEVICE_ATTR_RO(collect_ent);
static DEVICE_ATTR_RO(collect_ent_nsout);
static DEVICE_ATTR_WO(nonce_seed_with_df);
static DEVICE_ATTR_WO(nonce_seed_direct);
static DEVICE_ATTR_WO(instantiate);
static DEVICE_ATTR_WO(uninstantiate);
static DEVICE_ATTR_WO(reseed);
static DEVICE_ATTR_WO(generate);
static DEVICE_ATTR_WO(generate_pub_vtrng);
static DEVICE_ATTR_WO(kat);

static DEVICE_ATTR_RW(max_bits_per_req);
static DEVICE_ATTR_RW(max_req_per_seed);

static DEVICE_ATTR_WO(test_attr);

static const struct attribute_group nisttrng_attr_group = {
	.attrs =
		(struct attribute *[]){
			&dev_attr_ckr.attr,
			//&dev_attr_stepping.attr,
			&dev_attr_features.attr, &dev_attr_secure.attr,
			&dev_attr_nonce.attr, &dev_attr_sec_strength.attr,

			&dev_attr_mode_reg.attr, &dev_attr_smode_reg.attr,
			&dev_attr_alarm_reg.attr, &dev_attr_rand_reg.attr,
			&dev_attr_rand_out.attr, &dev_attr_rand_out_vtrng.attr,
			&dev_attr_seed_reg.attr, &dev_attr_npa_data_reg.attr,
			&dev_attr_ctrl_reg.attr, &dev_attr_istat_reg.attr,
			&dev_attr_stat_reg.attr, &dev_attr_rnc_reg.attr,
			&dev_attr_rbc_reg.attr,

			&dev_attr_ia_wdata_reg.attr,
			&dev_attr_ia_rdata_reg.attr, &dev_attr_ia_addr_reg.attr,
			&dev_attr_ia_cmd_reg.attr, &dev_attr_hw_state.attr,

			&dev_attr_collect_ent.attr,
			&dev_attr_collect_ent_nsout.attr,
			&dev_attr_nonce_seed_with_df.attr,
			&dev_attr_nonce_seed_direct.attr,
			&dev_attr_instantiate.attr,
			&dev_attr_uninstantiate.attr, &dev_attr_reseed.attr,
			&dev_attr_generate.attr,
			&dev_attr_generate_pub_vtrng.attr, &dev_attr_kat.attr,

			&dev_attr_max_bits_per_req.attr,
			&dev_attr_max_req_per_seed.attr,

			&dev_attr_test_attr.attr, NULL },
};

static int nisttrng_self_test(struct nist_trng_state *nist_trng)
{
	u32 seed[16], out[4], x, y;

	static const u32 exp128[10][4] = {
		{ 0x5db79bb2, 0xc3a0df1e, 0x099482b6,
		  0xc319981e }, // The 1st generated output
		{ 0xb344d301, 0xdbd97ca0, 0x6e66e668,
		  0x0bcd4625 }, // The 2nd generate output
		{ 0xec553f18, 0xa0e5c3cb, 0x752c03c2,
		  0x5e7b04f7 }, // The 3rd generate output
		{ 0xcfe23e6e, 0x5302edc2, 0xdbf7b05b,
		  0x2c817c0f }, // The 4th generate output
		{ 0xbd5a8726, 0x028c43d0, 0xb77ac4e3,
		  0x0844ba2c }, // The 5th generate output
		{ 0xa63b4c0e, 0x8d11d0ba, 0x08b5a10f,
		  0xab731aff }, // The 6th generate output
		{ 0xb7b56a2f, 0x1d84d1f0, 0xe48d1a0a,
		  0x43a010a6 }, // The 7th generate output
		{ 0xcf66439d, 0xc937451d, 0x75c34d20,
		  0x21a21398 }, // The 8th generate output
		{ 0xcb6f0a57, 0x5ff34705, 0x08838e49,
		  0x21137614 }, // The 9th generate output
		{ 0x61c48b24, 0x25c18d29, 0xc6005e4e,
		  0xae3b0389 }, // The 10th generate output
	};

	static const u32 exp256[10][4] = {
		{ 0x1f1a1441, 0xa0865ece, 0x9ff8d5b9,
		  0x3f78ace6 }, // The 1st generated output
		{ 0xf8190a86, 0x6d6ded2a, 0xc4d0e9bf,
		  0x24dab55c }, // The 2nd generate output
		{ 0xd3948b74, 0x3dfea516, 0x9c3b86a2,
		  0xeb184b41 }, // The 3rd generate output
		{ 0x2eb82ab6, 0x2aceefda, 0xc0cf6a5f,
		  0xa45cb333 }, // The 4th generate output
		{ 0xa49b1c7b, 0x5b51bac7, 0x7586770b,
		  0x8cb2c392 }, // The 5th generate output
		{ 0x3f3ba09d, 0xa2c9ad29, 0x9687fb8f,
		  0xa5ae3fd5 }, // The 6th generate output
		{ 0x11dd1076, 0xe37e86cb, 0xced0220a,
		  0x00448c4f }, // The 7th generate output
		{ 0x955a5e52, 0x84ee38b1, 0xb3271e5f,
		  0x097751e3 }, // The 8th generate output
		{ 0x5cd73ba8, 0xd8a36a1e, 0xa8a2d7c3,
		  0xa96de048 }, // The 9th generate output
		{ 0xfb374c63, 0x827b85fa, 0x244e0c7a,
		  0xa09afd39 }, // The 10th generate output
	};

	int ret, enable, rate, urun;
	u32 tmp;

	for (x = 0; x < 16; x++)
		seed[x] = 0x12345679 * (x + 1);

	DEBUG("Doing a self-test with security strength of 128\n");
	ret = nisttrng_uninstantiate(nist_trng);
	if (ret && ret != CRYPTO_NOT_INSTANTIATED)
		goto ERR;

	//if ((ret = nisttrng_set_secure_mode(nist_trng, 0)))  { goto ERR; }
	ret = nisttrng_set_nonce_mode(nist_trng, 1);
	if (ret)
		goto ERR;

	ret = nisttrng_set_sec_strength(nist_trng, 128);
	if (ret)
		goto ERR;

	ret = nisttrng_get_entropy_input(nist_trng, seed, 0);
	if (ret)
		goto ERR;

	ret = nisttrng_instantiate(nist_trng, 128, 0, NULL);
	if (ret)
		goto ERR;

	if (nist_trng->config.build_cfg0.edu_present) {
		ret = nisttrng_wait_fifo_full(nist_trng);
		if (ret)
			goto ERR;
	}

	ret = nisttrng_generate(nist_trng, out, 16, 128, 0, NULL);
	if (ret)
		goto ERR;

	if (nist_trng->config.features.extra_ps_present) {
		DEBUG("skip KAT with extra_ps_present\n");
	} else {
		DEBUG("nist_trng: AES-128 Self-test output: ");
		for (x = 0; x < 4; x++)
			DEBUG("0x%08lx ", (unsigned long)out[x]);

		if (nist_trng->config.build_cfg0.edu_present) {
			if (nist_trng->config.edu_build_cfg0
				    .esm_channel) { //if esm_channel is available the first random number goes to esm
				for (x = 0; x < 4; x++) {
					if (out[x] != exp128[1][x])
						ret = 1;
				}
			}
		} else {
			for (x = 0; x < 4; x++) {
				if (out[x] != exp128[0][x])
					ret = 1;
			}
		}

		if (ret) {
			SYNHW_PRINT("...  FAILED comparison\n");
			ret = -1;
			goto ERR;
		} else {
			DEBUG("...  PASSED\n");
		}
	}

	// if edu is available check all the pvtrng's
	if (nist_trng->config.build_cfg0.edu_present) {
		for (x = 0;
		     x < nist_trng->config.edu_build_cfg0.public_vtrng_channels;
		     x++) {
			DEBUG("vtrng %d\n", x);
			ret = nisttrng_generate_public_vtrng(nist_trng, out, 16, x);
			if (ret)
				goto ERR;

			for (y = 0; y < 4; y++) {
				DEBUG("0x%08lx ", (unsigned long)out[y]);
				if (out[y] != exp128[x + 2][y])
					ret = 1;
			}
			if (ret) {
				SYNHW_PRINT("...  FAILED comparison\n");
				ret = -1;
				goto ERR;
			} else {
				DEBUG("...  PASSED\n");
			}
		}
	}
	// if edu is available empty the fifo before creating the new instance with strength of 256
	if (nist_trng->config.build_cfg0.edu_present) {
		nisttrng_rnc(nist_trng,
			     NIST_TRNG_EDU_RNC_CTRL_CMD_RNC_FINISH_TO_IDLE);
		tmp = NIST_TRNG_REG_ISTAT_DONE;
		//always clear the busy bit after disabling RNC
		pdu_io_write32(nist_trng->base + NIST_TRNG_REG_ISTAT, tmp);
		tmp = pdu_io_read32(nist_trng->base + NIST_TRNG_REG_ISTAT);
		do {
			ret = nisttrng_generate_public_vtrng(nist_trng, out, 16, 0);
			if (ret)
				goto ERR;

			tmp = pdu_io_read32(nist_trng->base +
					    NIST_TRNG_EDU_STAT);

		} while (!NIST_TRNG_EDU_STAT_FIFO_EMPTY(tmp));
	}

	if (nist_trng->config.features.drbg_arch == AES256) {
		// test AES-256 mode
		DEBUG("Doing a self-test with security strength of 256\n");
		ret = nisttrng_uninstantiate(nist_trng);
		if (ret && ret != CRYPTO_NOT_INSTANTIATED)
			goto ERR;

		ret = nisttrng_set_nonce_mode(nist_trng, 1);
		if (ret)
			goto ERR;

		ret = nisttrng_set_sec_strength(nist_trng, 256);
		if (ret)
			goto ERR;

		ret = nisttrng_get_entropy_input(nist_trng, seed, 0);
		if (ret)
			goto ERR;

		ret = nisttrng_instantiate(nist_trng, 256, 0, NULL);
		if (ret)
			goto ERR;

		ret = nisttrng_generate(nist_trng, out, 16, 256, 0, NULL);
		if (ret)
			goto ERR;

		if (nist_trng->config.features.extra_ps_present) {
			DEBUG("skip KAT with extra_ps_present\n");
		} else {
			DEBUG("nist_trng: AES-256 Self-test output: ");
			for (x = 0; x < 4; x++)
				DEBUG("0x%08lx ", (unsigned long)out[x]);

			for (x = 0; x < 4; x++) {
				if (out[x] != exp256[0][x])
					ret = 1;
			}
			if (ret) {
				SYNHW_PRINT("...  FAILED comparison\n");
				ret = -1;
				goto ERR;
			} else {
				DEBUG("...  PASSED\n");
			}
		}
	}

	// if edu is available check all the pvtrng's
	if (nist_trng->config.build_cfg0.edu_present) {
		for (x = 0;
		     x < nist_trng->config.edu_build_cfg0.public_vtrng_channels;
		     x++) {
			DEBUG("vtrng 256 %d\n", x);
			ret = nisttrng_generate_public_vtrng(nist_trng, out, 16, x);
			if (ret)
				goto ERR;

			for (y = 0; y < 4; y++) {
				DEBUG("0x%08lx ", (unsigned long)out[y]);
				if (out[y] != exp256[x + 1][y])
					ret = 1;
			}
			if (ret) {
				SYNHW_PRINT("...  FAILED comparison\n");
				ret = -1;
				goto ERR;
			} else {
				DEBUG("...  PASSED\n");
			}
		}

		//Test  RBC channels
		// enable RBC channels  with rate of 2 and urun 1
		enable = 1;
		rate = 2;
		urun = 1;
		for (x = 0; x < nist_trng->config.edu_build_cfg0.rbc_channels;
		     x++) {
			ret = nisttrng_rbc(nist_trng, enable, x, rate, urun);
			if (ret)
				goto ERR;

			tmp = pdu_io_read32(nist_trng->base +
					    NIST_TRNG_EDU_RBC_CTRL);

			switch (x) {
			case 0:
				if (rate != NISTTRNG_EDU_RBC_CTRL_GET_CH_RATE(tmp, _NIST_TRNG_EDU_RBC_CTRL_CH0_RATE) ||
				    urun != NISTTRNG_EDU_RBC_CTRL_GET_CH_URUN_BLANK(tmp, _NIST_TRNG_EDU_RBC_CTRL_CH0_URUN_BLANK)) {
					goto ERR;
				}
				break;
			case 1:
				if (rate != NISTTRNG_EDU_RBC_CTRL_GET_CH_RATE(tmp, _NIST_TRNG_EDU_RBC_CTRL_CH1_RATE) ||
				    urun != NISTTRNG_EDU_RBC_CTRL_GET_CH_URUN_BLANK(tmp, _NIST_TRNG_EDU_RBC_CTRL_CH1_URUN_BLANK)) {
					goto ERR;
				}
				break;
			case 2:
				if (rate != NISTTRNG_EDU_RBC_CTRL_GET_CH_RATE(tmp, _NIST_TRNG_EDU_RBC_CTRL_CH2_RATE) ||
				    urun != NISTTRNG_EDU_RBC_CTRL_GET_CH_URUN_BLANK(tmp, _NIST_TRNG_EDU_RBC_CTRL_CH2_URUN_BLANK)) {
					goto ERR;
				}
				break;
			default:
				DEBUG("Incorrect rbc_num = %d\n", x);
				goto ERR;
			}
		}
		DEBUG("RBC test passed\n");
	}

	//IF RNCis not disable, disable it
	if (pdu_io_read32(nist_trng->base + NIST_TRNG_EDU_RNC_CTRL) !=
	    NIST_TRNG_EDU_RNC_CTRL_CMD_RNC_FINISH_TO_IDLE) {
		nisttrng_rnc(nist_trng,
			     NIST_TRNG_EDU_RNC_CTRL_CMD_RNC_FINISH_TO_IDLE);
		tmp = NIST_TRNG_REG_ISTAT_DONE;
		//always clear the busy bit after disabling RNC
		pdu_io_write32(nist_trng->base + NIST_TRNG_REG_ISTAT, tmp);
	}

	/* back to the noise mode */
	ret = nisttrng_set_nonce_mode(nist_trng, 0);
	if (ret)
		goto ERR;

	ret = nisttrng_zeroize(nist_trng);
	if (ret)
		goto ERR;
ERR:
	return ret;
}

static int nisttrng_driver_probe(struct platform_device *pdev)
{
	struct synopsys_nisttrng_driver *data;
	struct hwrng *hwrng_driver_info = 0;
	struct resource *cfg, *irq;
	u32 *base_addr;
	int ret;

	// version
	SYNHW_PRINT("DWC_TRNG_DriverSDK_%s\n", TRNG_VERSION);

	cfg = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

	if (!cfg || !irq) {
		SYNHW_PRINT("no memory or IRQ resource\n");
		return -ENOMEM;
	}

	DEBUG("=================================================================\n");
	DEBUG("nisttrng_probe: Device at %08lx(%08lx) of size %lu bytes\n",
	      (unsigned long)cfg->start, (unsigned long)cfg->end,
	      (unsigned long)resource_size(cfg));

	data = devm_kzalloc(&pdev->dev, sizeof(struct synopsys_nisttrng_driver),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	base_addr = pdu_linux_map_regs(&pdev->dev, cfg);
	if (IS_ERR(base_addr)) {
		dev_err(&pdev->dev, "unable to remap io mem\n");
		return PTR_ERR(base_addr);
	}

	ret = nisttrng_init(&data->nisttrng, (u32 *)base_addr);
	if (ret) {
		SYNHW_PRINT("NIST_TRNG init failed (%d)\n", ret);
		devm_kfree(&pdev->dev, data);
		return ret;
	}

	/* if max_reads is not 0, change the max_req_per_seed according to max_reads */
	if (max_reads) {
		ret = nisttrng_set_reminder_max_req_per_seed(&data->nisttrng, max_reads);
		if (ret) {
			SYNHW_PRINT("NIST_TRNG maximum request-per-seed setup failed (%d)\n",
				    ret);
			devm_kfree(&pdev->dev, data);
			return ret;
		}
	}

	// issue quick self test
	ret = nisttrng_self_test(&data->nisttrng);
	if (ret) {
		devm_kfree(&pdev->dev, data);
		return -ENOMEM;
	}

	// ready the device for use
	ret = nisttrng_instantiate(&data->nisttrng,
				   data->nisttrng.config.features.drbg_arch ? 256 : 128, 1, NULL);
	if (ret) {
		SYNHW_PRINT("NIST_TRNG instantiate failed (%d)\n", ret);
		devm_kfree(&pdev->dev, data);
		return -ENOMEM;
	}

	// at this point the device should be ready for a call to gen_random
	hwrng_driver_info =
		devm_kzalloc(&pdev->dev, sizeof(struct hwrng), GFP_KERNEL);
	if (!hwrng_driver_info) {
		devm_kfree(&pdev->dev, data);
		return -ENOMEM;
	}

	hwrng_driver_info->name = devm_kzalloc(&pdev->dev,
					       sizeof(SYNOPSYS_HWRNG_DRIVER_NAME) + 1, GFP_KERNEL);
	if (!hwrng_driver_info->name) {
		devm_kfree(&pdev->dev, data);
		devm_kfree(&pdev->dev, hwrng_driver_info);
		return -ENOMEM;
	}

	memset((void *)hwrng_driver_info->name, 0,
	       sizeof(SYNOPSYS_HWRNG_DRIVER_NAME) + 1);
	strscpy((char *)hwrng_driver_info->name, SYNOPSYS_HWRNG_DRIVER_NAME,
		sizeof(SYNOPSYS_HWRNG_DRIVER_NAME));

	hwrng_driver_info->read = &nisttrng_hwrng_driver_read;
	hwrng_driver_info->data_present = 0;
	hwrng_driver_info->priv = (unsigned long)pdev;
	hwrng_driver_info->quality = 1024;

	data->hwrng_drv = hwrng_driver_info;
	ret = hwrng_register(hwrng_driver_info);

	if (ret) {
		SYNHW_PRINT("unable to load HWRNG driver (error %d)\n", ret);
		devm_kfree(&pdev->dev, (void *)hwrng_driver_info->name);
		devm_kfree(&pdev->dev, hwrng_driver_info);
		devm_kfree(&pdev->dev, data);
		return ret;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &nisttrng_attr_group);
	if (ret < 0) {
		SYNHW_PRINT("unable to initialize sysfs group (error %d)\n",
			    ret);
		hwrng_unregister(hwrng_driver_info);
		devm_kfree(&pdev->dev, (void *)hwrng_driver_info->name);
		devm_kfree(&pdev->dev, hwrng_driver_info);
		devm_kfree(&pdev->dev, data);
		return ret;
	}
	SYNHW_PRINT("SYN NIST_TRNG registering HW_RANDOM\n");
	return 0;
}

static int nisttrng_driver_remove(struct platform_device *pdev)
{
	struct synopsys_nisttrng_driver *data = platform_get_drvdata(pdev);
	struct hwrng *hwrng_driver_info = (struct hwrng *)data->hwrng_drv;

	SYNHW_PRINT("SYN NIST_TRNG unregistering from HW_RANDOM\n");
	hwrng_unregister(hwrng_driver_info);
	sysfs_remove_group(&pdev->dev.kobj, &nisttrng_attr_group);
	devm_kfree(&pdev->dev, (void *)hwrng_driver_info->name);
	devm_kfree(&pdev->dev, hwrng_driver_info);
	devm_kfree(&pdev->dev, data);
	return 0;
}

static struct platform_driver s_nisttrng_platform_driver_info = {
	.probe      = nisttrng_driver_probe,
	.remove     = nisttrng_driver_remove,
	.driver     = {
		.name = "nist_trng",
		.owner   = THIS_MODULE,
	},
};

static int __init nisttrng_platform_driver_start(void)
{
	return platform_driver_register(&s_nisttrng_platform_driver_info);
}

static void __exit nisttrng_platform_driver_end(void)
{
	platform_driver_unregister(&s_nisttrng_platform_driver_info);
}

module_init(nisttrng_platform_driver_start);
module_exit(nisttrng_platform_driver_end);

module_param(max_reads, ulong, 0);
MODULE_PARM_DESC(max_reads, "Max # of reads between reseeds (default is 128)");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Synopsys, Inc.");
