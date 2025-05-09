// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Xilinx, Inc.
 * Copyright (C) 2022 - 2023, Advanced Micro Devices, Inc.
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/firmware/xlnx-zynqmp.h>

#define SILICON_REVISION_MASK 0xF
#define P_USER_0_64_UPPER_MASK	GENMASK(31, 16)
#define P_USER_127_LOWER_4_BIT_MASK GENMASK(3, 0)
#define WORD_INBYTES		4
#define SOC_VER_SIZE		0x4
#define EFUSE_MEMORY_SIZE	0x177
#define UNUSED_SPACE		0x8
#define ZYNQMP_NVMEM_SIZE	(SOC_VER_SIZE + UNUSED_SPACE + \
				 EFUSE_MEMORY_SIZE)
#define SOC_VERSION_OFFSET	0x0
#define EFUSE_START_OFFSET	0xC
#define EFUSE_END_OFFSET	0xFC
#define EFUSE_PUF_START_OFFSET	0x100
#define EFUSE_PUF_MID_OFFSET	0x140
#define EFUSE_PUF_END_OFFSET	0x17F
#define EFUSE_NOT_ENABLED	29

/*
 * efuse access type
 */
enum efuse_access {
	EFUSE_READ = 0,
	EFUSE_WRITE
};

/**
 * struct xilinx_efuse - the basic structure
 * @src:	address of the buffer to store the data to be write/read
 * @size:	read/write word count
 * @offset:	read/write offset
 * @flag:	0 - represents efuse read and 1- represents efuse write
 * @pufuserfuse:0 - represents non-puf efuses, offset is used for read/write
 *		1 - represents puf user fuse row number.
 *
 * this structure stores all the required details to
 * read/write efuse memory.
 */
struct xilinx_efuse {
	u64 src;
	u32 size;
	u32 offset;
	enum efuse_access flag;
	u32 pufuserfuse;
};

static int zynqmp_efuse_access(void *context, unsigned int offset,
			       void *val, size_t bytes, enum efuse_access flag,
			       unsigned int pufflag)
{
	struct device *dev = context;
	struct xilinx_efuse *efuse;
	dma_addr_t dma_addr;
	dma_addr_t dma_buf;
	size_t words = bytes / WORD_INBYTES;
	int ret;
	int value;
	char *data;

	if (bytes % WORD_INBYTES != 0) {
		dev_err(dev, "Bytes requested should be word aligned\n");
		return -EOPNOTSUPP;
	}

	if (pufflag == 0 && offset % WORD_INBYTES) {
		dev_err(dev, "Offset requested should be word aligned\n");
		return -EOPNOTSUPP;
	}

	if (pufflag == 1 && flag == EFUSE_WRITE) {
		memcpy(&value, val, bytes);
		if ((offset == EFUSE_PUF_START_OFFSET ||
		     offset == EFUSE_PUF_MID_OFFSET) &&
		    value & P_USER_0_64_UPPER_MASK) {
			dev_err(dev, "Only lower 4 bytes are allowed to be programmed in P_USER_0 & P_USER_64\n");
			return -EOPNOTSUPP;
		}

		if (offset == EFUSE_PUF_END_OFFSET &&
		    (value & P_USER_127_LOWER_4_BIT_MASK)) {
			dev_err(dev, "Only MSB 28 bits are allowed to be programmed for P_USER_127\n");
			return -EOPNOTSUPP;
		}
	}

	efuse = dma_alloc_coherent(dev, sizeof(struct xilinx_efuse),
				   &dma_addr, GFP_KERNEL);
	if (!efuse)
		return -ENOMEM;

	data = dma_alloc_coherent(dev, sizeof(bytes),
				  &dma_buf, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto efuse_data_fail;
	}

	if (flag == EFUSE_WRITE) {
		memcpy(data, val, bytes);
		efuse->flag = EFUSE_WRITE;
	} else {
		efuse->flag = EFUSE_READ;
	}

	efuse->src = dma_buf;
	efuse->size = words;
	efuse->offset = offset;
	efuse->pufuserfuse = pufflag;

	zynqmp_pm_efuse_access(dma_addr, (u32 *)&ret);
	if (ret != 0) {
		if (ret == EFUSE_NOT_ENABLED) {
			dev_err(dev, "efuse access is not enabled\n");
			ret = -EOPNOTSUPP;
		} else {
			dev_err(dev, "Error in efuse read %x\n", ret);
			ret = -EPERM;
		}
		goto efuse_access_err;
	}

	if (flag == EFUSE_READ)
		memcpy(val, data, bytes);
efuse_access_err:
	dma_free_coherent(dev, sizeof(bytes),
			  data, dma_buf);
efuse_data_fail:
	dma_free_coherent(dev, sizeof(struct xilinx_efuse),
			  efuse, dma_addr);

	return ret;
}

static int zynqmp_nvmem_read(void *context, unsigned int offset, void *val, size_t bytes)
{
	struct device *dev = context;
	int ret;
	int pufflag = 0;
	int idcode;
	int version;

	if (offset >= EFUSE_PUF_START_OFFSET && offset <= EFUSE_PUF_END_OFFSET)
		pufflag = 1;

	switch (offset) {
	/* Soc version offset is zero */
	case SOC_VERSION_OFFSET:
		if (bytes != SOC_VER_SIZE)
			return -EOPNOTSUPP;

		ret = zynqmp_pm_get_chipid((u32 *)&idcode, (u32 *)&version);
		if (ret < 0)
			return ret;

		dev_dbg(dev, "Read chipid val %x %x\n", idcode, version);
		*(int *)val = version & SILICON_REVISION_MASK;
		break;
	/* Efuse offset starts from 0xc */
	case EFUSE_START_OFFSET ... EFUSE_END_OFFSET:
	case EFUSE_PUF_START_OFFSET ... EFUSE_PUF_END_OFFSET:
		ret = zynqmp_efuse_access(context, offset, val,
					  bytes, EFUSE_READ, pufflag);
		break;
	default:
		*(u32 *)val = 0xDEADBEEF;
		ret = 0;
		break;
	}

	return ret;
}

static int zynqmp_nvmem_write(void *context,
			      unsigned int offset, void *val, size_t bytes)
{
	int pufflag = 0;

	if (offset < EFUSE_START_OFFSET || offset > EFUSE_PUF_END_OFFSET)
		return -EOPNOTSUPP;

	if (offset >= EFUSE_PUF_START_OFFSET && offset <= EFUSE_PUF_END_OFFSET)
		pufflag = 1;

	return zynqmp_efuse_access(context, offset,
				   val, bytes, EFUSE_WRITE, pufflag);
}

static const struct of_device_id zynqmp_nvmem_match[] = {
	{ .compatible = "xlnx,zynqmp-nvmem-fw", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, zynqmp_nvmem_match);

static int zynqmp_nvmem_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvmem_config econfig = {};

	econfig.name = "zynqmp-nvmem";
	econfig.owner = THIS_MODULE;
	econfig.word_size = 1;
	econfig.size = ZYNQMP_NVMEM_SIZE;
	econfig.dev = dev;
	econfig.priv = dev;
	econfig.add_legacy_fixed_of_cells = true;
	econfig.reg_read = zynqmp_nvmem_read;
	econfig.reg_write = zynqmp_nvmem_write;

	return PTR_ERR_OR_ZERO(devm_nvmem_register(dev, &econfig));
}

static struct platform_driver zynqmp_nvmem_driver = {
	.probe = zynqmp_nvmem_probe,
	.driver = {
		.name = "zynqmp-nvmem",
		.of_match_table = zynqmp_nvmem_match,
	},
};

module_platform_driver(zynqmp_nvmem_driver);

MODULE_AUTHOR("Michal Simek <michal.simek@amd.com>, Nava kishore Manne <nava.kishore.manne@amd.com>");
MODULE_DESCRIPTION("ZynqMP NVMEM driver");
MODULE_LICENSE("GPL");
