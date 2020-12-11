/***********************license start************************************
 * Copyright (c) 2003-2017 Cavium, Inc.
 * All rights reserved.
 *
 * License: one of 'Cavium License' or 'GNU General Public License Version 2'
 *
 * This file is provided under the terms of the Cavium License (see below)
 * or under the terms of GNU General Public License, Version 2, as
 * published by the Free Software Foundation. When using or redistributing
 * this file, you may do so under either license.
 *
 * Cavium License:  Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that the following
 * conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 *  * Neither the name of Cavium Inc. nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * This Software, including technical data, may be subject to U.S. export
 * control laws, including the U.S. Export Administration Act and its
 * associated regulations, and may be subject to export or import
 * regulations in other countries.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS
 * OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH
 * RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY
 * REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT
 * DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY)
 * WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A
 * PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET
 * ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE
 * ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES
 * WITH YOU.
 ***********************license end**************************************/

#include "common.h"
#include "zip_crypto.h"

#define DRV_NAME		"ThunderX-ZIP"

static struct zip_device *zip_dev[MAX_ZIP_DEVICES];

static const struct pci_device_id zip_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_THUNDERX_ZIP) },
	{ 0, }
};

void zip_reg_write(u64 val, u64 __iomem *addr)
{
	writeq(val, addr);
}

u64 zip_reg_read(u64 __iomem *addr)
{
	return readq(addr);
}

/*
 * Allocates new ZIP device structure
 * Returns zip_device pointer or NULL if cannot allocate memory for zip_device
 */
static struct zip_device *zip_alloc_device(struct pci_dev *pdev)
{
	struct zip_device *zip = NULL;
	int idx;

	for (idx = 0; idx < MAX_ZIP_DEVICES; idx++) {
		if (!zip_dev[idx])
			break;
	}

	/* To ensure that the index is within the limit */
	if (idx < MAX_ZIP_DEVICES)
		zip = devm_kzalloc(&pdev->dev, sizeof(*zip), GFP_KERNEL);

	if (!zip)
		return NULL;

	zip_dev[idx] = zip;
	zip->index = idx;
	return zip;
}

/**
 * zip_get_device - Get ZIP device based on node id of cpu
 *
 * @node: Node id of the current cpu
 * Return: Pointer to Zip device structure
 */
struct zip_device *zip_get_device(int node)
{
	if ((node < MAX_ZIP_DEVICES) && (node >= 0))
		return zip_dev[node];

	zip_err("ZIP device not found for node id %d\n", node);
	return NULL;
}

/**
 * zip_get_node_id - Get the node id of the current cpu
 *
 * Return: Node id of the current cpu
 */
int zip_get_node_id(void)
{
	return cpu_to_node(raw_smp_processor_id());
}

/* Initializes the ZIP h/w sub-system */
static int zip_init_hw(struct zip_device *zip)
{
	union zip_cmd_ctl    cmd_ctl;
	union zip_constants  constants;
	union zip_que_ena    que_ena;
	union zip_quex_map   que_map;
	union zip_que_pri    que_pri;

	union zip_quex_sbuf_addr que_sbuf_addr;
	union zip_quex_sbuf_ctl  que_sbuf_ctl;

	int q = 0;

	/* Enable the ZIP Engine(Core) Clock */
	cmd_ctl.u_reg64 = zip_reg_read(zip->reg_base + ZIP_CMD_CTL);
	cmd_ctl.s.forceclk = 1;
	zip_reg_write(cmd_ctl.u_reg64 & 0xFF, (zip->reg_base + ZIP_CMD_CTL));

	zip_msg("ZIP_CMD_CTL  : 0x%016llx",
		zip_reg_read(zip->reg_base + ZIP_CMD_CTL));

	constants.u_reg64 = zip_reg_read(zip->reg_base + ZIP_CONSTANTS);
	zip->depth    = constants.s.depth;
	zip->onfsize  = constants.s.onfsize;
	zip->ctxsize  = constants.s.ctxsize;

	zip_msg("depth: 0x%016llx , onfsize : 0x%016llx , ctxsize : 0x%016llx",
		zip->depth, zip->onfsize, zip->ctxsize);

	/*
	 * Program ZIP_QUE(0..7)_SBUF_ADDR and ZIP_QUE(0..7)_SBUF_CTL to
	 * have the correct buffer pointer and size configured for each
	 * instruction queue.
	 */
	for (q = 0; q < ZIP_NUM_QUEUES; q++) {
		que_sbuf_ctl.u_reg64 = 0ull;
		que_sbuf_ctl.s.size = (ZIP_CMD_QBUF_SIZE / sizeof(u64));
		que_sbuf_ctl.s.inst_be   = 0;
		que_sbuf_ctl.s.stream_id = 0;
		zip_reg_write(que_sbuf_ctl.u_reg64,
			      (zip->reg_base + ZIP_QUEX_SBUF_CTL(q)));

		zip_msg("QUEX_SBUF_CTL[%d]: 0x%016llx", q,
			zip_reg_read(zip->reg_base + ZIP_QUEX_SBUF_CTL(q)));
	}

	for (q = 0; q < ZIP_NUM_QUEUES; q++) {
		memset(&zip->iq[q], 0x0, sizeof(struct zip_iq));

		spin_lock_init(&zip->iq[q].lock);

		if (zip_cmd_qbuf_alloc(zip, q)) {
			while (q != 0) {
				q--;
				zip_cmd_qbuf_free(zip, q);
			}
			return -ENOMEM;
		}

		/* Initialize tail ptr to head */
		zip->iq[q].sw_tail = zip->iq[q].sw_head;
		zip->iq[q].hw_tail = zip->iq[q].sw_head;

		/* Write the physical addr to register */
		que_sbuf_addr.u_reg64   = 0ull;
		que_sbuf_addr.s.ptr = (__pa(zip->iq[q].sw_head) >>
				       ZIP_128B_ALIGN);

		zip_msg("QUE[%d]_PTR(PHYS): 0x%016llx", q,
			(u64)que_sbuf_addr.s.ptr);

		zip_reg_write(que_sbuf_addr.u_reg64,
			      (zip->reg_base + ZIP_QUEX_SBUF_ADDR(q)));

		zip_msg("QUEX_SBUF_ADDR[%d]: 0x%016llx", q,
			zip_reg_read(zip->reg_base + ZIP_QUEX_SBUF_ADDR(q)));

		zip_dbg("sw_head :0x%lx sw_tail :0x%lx hw_tail :0x%lx",
			zip->iq[q].sw_head, zip->iq[q].sw_tail,
			zip->iq[q].hw_tail);
		zip_dbg("sw_head phy addr : 0x%lx", que_sbuf_addr.s.ptr);
	}

	/*
	 * Queue-to-ZIP core mapping
	 * If a queue is not mapped to a particular core, it is equivalent to
	 * the ZIP core being disabled.
	 */
	que_ena.u_reg64 = 0x0ull;
	/* Enabling queues based on ZIP_NUM_QUEUES */
	for (q = 0; q < ZIP_NUM_QUEUES; q++)
		que_ena.s.ena |= (0x1 << q);
	zip_reg_write(que_ena.u_reg64, (zip->reg_base + ZIP_QUE_ENA));

	zip_msg("QUE_ENA      : 0x%016llx",
		zip_reg_read(zip->reg_base + ZIP_QUE_ENA));

	for (q = 0; q < ZIP_NUM_QUEUES; q++) {
		que_map.u_reg64 = 0ull;
		/* Mapping each queue to two ZIP cores */
		que_map.s.zce = 0x3;
		zip_reg_write(que_map.u_reg64,
			      (zip->reg_base + ZIP_QUEX_MAP(q)));

		zip_msg("QUE_MAP(%d)   : 0x%016llx", q,
			zip_reg_read(zip->reg_base + ZIP_QUEX_MAP(q)));
	}

	que_pri.u_reg64 = 0ull;
	for (q = 0; q < ZIP_NUM_QUEUES; q++)
		que_pri.s.pri |= (0x1 << q); /* Higher Priority RR */
	zip_reg_write(que_pri.u_reg64, (zip->reg_base + ZIP_QUE_PRI));

	zip_msg("QUE_PRI %016llx", zip_reg_read(zip->reg_base + ZIP_QUE_PRI));

	return 0;
}

static int zip_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct zip_device *zip = NULL;
	int    err;

	zip = zip_alloc_device(pdev);
	if (!zip)
		return -ENOMEM;

	dev_info(dev, "Found ZIP device %d %x:%x on Node %d\n", zip->index,
		 pdev->vendor, pdev->device, dev_to_node(dev));

	pci_set_drvdata(pdev, zip);
	zip->pdev = pdev;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device");
		goto err_free_device;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x", err);
		goto err_disable_device;
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to get usable DMA configuration\n");
		goto err_release_regions;
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to get 48-bit DMA for allocations\n");
		goto err_release_regions;
	}

	/* MAP configuration registers */
	zip->reg_base = pci_ioremap_bar(pdev, PCI_CFG_ZIP_PF_BAR0);
	if (!zip->reg_base) {
		dev_err(dev, "ZIP: Cannot map BAR0 CSR memory space, aborting");
		err = -ENOMEM;
		goto err_release_regions;
	}

	/* Initialize ZIP Hardware */
	err = zip_init_hw(zip);
	if (err)
		goto err_release_regions;

	return 0;

err_release_regions:
	if (zip->reg_base)
		iounmap(zip->reg_base);
	pci_release_regions(pdev);

err_disable_device:
	pci_disable_device(pdev);

err_free_device:
	pci_set_drvdata(pdev, NULL);

	/* Remove zip_dev from zip_device list, free the zip_device memory */
	zip_dev[zip->index] = NULL;
	devm_kfree(dev, zip);

	return err;
}

static void zip_remove(struct pci_dev *pdev)
{
	struct zip_device *zip = pci_get_drvdata(pdev);
	union zip_cmd_ctl cmd_ctl;
	int q = 0;

	if (!zip)
		return;

	if (zip->reg_base) {
		cmd_ctl.u_reg64 = 0x0ull;
		cmd_ctl.s.reset = 1;  /* Forces ZIP cores to do reset */
		zip_reg_write(cmd_ctl.u_reg64, (zip->reg_base + ZIP_CMD_CTL));
		iounmap(zip->reg_base);
	}

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	/*
	 * Free Command Queue buffers. This free should be called for all
	 * the enabled Queues.
	 */
	for (q = 0; q < ZIP_NUM_QUEUES; q++)
		zip_cmd_qbuf_free(zip, q);

	pci_set_drvdata(pdev, NULL);
	/* remove zip device from zip device list */
	zip_dev[zip->index] = NULL;
}

/* PCI Sub-System Interface */
static struct pci_driver zip_driver = {
	.name	    =  DRV_NAME,
	.id_table   =  zip_id_table,
	.probe	    =  zip_probe,
	.remove     =  zip_remove,
};

/* Kernel Crypto Subsystem Interface */

static struct crypto_alg zip_comp_deflate = {
	.cra_name		= "deflate",
	.cra_driver_name	= "deflate-cavium",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct zip_kernel_ctx),
	.cra_priority           = 300,
	.cra_module		= THIS_MODULE,
	.cra_init		= zip_alloc_comp_ctx_deflate,
	.cra_exit		= zip_free_comp_ctx,
	.cra_u			= { .compress = {
		.coa_compress	= zip_comp_compress,
		.coa_decompress	= zip_comp_decompress
		 } }
};

static struct crypto_alg zip_comp_lzs = {
	.cra_name		= "lzs",
	.cra_driver_name	= "lzs-cavium",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct zip_kernel_ctx),
	.cra_priority           = 300,
	.cra_module		= THIS_MODULE,
	.cra_init		= zip_alloc_comp_ctx_lzs,
	.cra_exit		= zip_free_comp_ctx,
	.cra_u			= { .compress = {
		.coa_compress	= zip_comp_compress,
		.coa_decompress	= zip_comp_decompress
		 } }
};

static struct scomp_alg zip_scomp_deflate = {
	.alloc_ctx		= zip_alloc_scomp_ctx_deflate,
	.free_ctx		= zip_free_scomp_ctx,
	.compress		= zip_scomp_compress,
	.decompress		= zip_scomp_decompress,
	.base			= {
		.cra_name		= "deflate",
		.cra_driver_name	= "deflate-scomp-cavium",
		.cra_module		= THIS_MODULE,
		.cra_priority           = 300,
	}
};

static struct scomp_alg zip_scomp_lzs = {
	.alloc_ctx		= zip_alloc_scomp_ctx_lzs,
	.free_ctx		= zip_free_scomp_ctx,
	.compress		= zip_scomp_compress,
	.decompress		= zip_scomp_decompress,
	.base			= {
		.cra_name		= "lzs",
		.cra_driver_name	= "lzs-scomp-cavium",
		.cra_module		= THIS_MODULE,
		.cra_priority           = 300,
	}
};

static int zip_register_compression_device(void)
{
	int ret;

	ret = crypto_register_alg(&zip_comp_deflate);
	if (ret < 0) {
		zip_err("Deflate algorithm registration failed\n");
		return ret;
	}

	ret = crypto_register_alg(&zip_comp_lzs);
	if (ret < 0) {
		zip_err("LZS algorithm registration failed\n");
		goto err_unregister_alg_deflate;
	}

	ret = crypto_register_scomp(&zip_scomp_deflate);
	if (ret < 0) {
		zip_err("Deflate scomp algorithm registration failed\n");
		goto err_unregister_alg_lzs;
	}

	ret = crypto_register_scomp(&zip_scomp_lzs);
	if (ret < 0) {
		zip_err("LZS scomp algorithm registration failed\n");
		goto err_unregister_scomp_deflate;
	}

	return ret;

err_unregister_scomp_deflate:
	crypto_unregister_scomp(&zip_scomp_deflate);
err_unregister_alg_lzs:
	crypto_unregister_alg(&zip_comp_lzs);
err_unregister_alg_deflate:
	crypto_unregister_alg(&zip_comp_deflate);

	return ret;
}

static void zip_unregister_compression_device(void)
{
	crypto_unregister_alg(&zip_comp_deflate);
	crypto_unregister_alg(&zip_comp_lzs);
	crypto_unregister_scomp(&zip_scomp_deflate);
	crypto_unregister_scomp(&zip_scomp_lzs);
}

/*
 * debugfs functions
 */
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

/* Displays ZIP device statistics */
static int zip_stats_show(struct seq_file *s, void *unused)
{
	u64 val = 0ull;
	u64 avg_chunk = 0ull, avg_cr = 0ull;
	u32 q = 0;

	int index  = 0;
	struct zip_device *zip;
	struct zip_stats  *st;

	for (index = 0; index < MAX_ZIP_DEVICES; index++) {
		u64 pending = 0;

		if (zip_dev[index]) {
			zip = zip_dev[index];
			st  = &zip->stats;

			/* Get all the pending requests */
			for (q = 0; q < ZIP_NUM_QUEUES; q++) {
				val = zip_reg_read((zip->reg_base +
						    ZIP_DBG_QUEX_STA(q)));
				pending += val >> 32 & 0xffffff;
			}

			val = atomic64_read(&st->comp_req_complete);
			avg_chunk = (val) ? atomic64_read(&st->comp_in_bytes) / val : 0;

			val = atomic64_read(&st->comp_out_bytes);
			avg_cr = (val) ? atomic64_read(&st->comp_in_bytes) / val : 0;
			seq_printf(s, "        ZIP Device %d Stats\n"
				      "-----------------------------------\n"
				      "Comp Req Submitted        : \t%lld\n"
				      "Comp Req Completed        : \t%lld\n"
				      "Compress In Bytes         : \t%lld\n"
				      "Compressed Out Bytes      : \t%lld\n"
				      "Average Chunk size        : \t%llu\n"
				      "Average Compression ratio : \t%llu\n"
				      "Decomp Req Submitted      : \t%lld\n"
				      "Decomp Req Completed      : \t%lld\n"
				      "Decompress In Bytes       : \t%lld\n"
				      "Decompressed Out Bytes    : \t%lld\n"
				      "Decompress Bad requests   : \t%lld\n"
				      "Pending Req               : \t%lld\n"
					"---------------------------------\n",
				       index,
				       (u64)atomic64_read(&st->comp_req_submit),
				       (u64)atomic64_read(&st->comp_req_complete),
				       (u64)atomic64_read(&st->comp_in_bytes),
				       (u64)atomic64_read(&st->comp_out_bytes),
				       avg_chunk,
				       avg_cr,
				       (u64)atomic64_read(&st->decomp_req_submit),
				       (u64)atomic64_read(&st->decomp_req_complete),
				       (u64)atomic64_read(&st->decomp_in_bytes),
				       (u64)atomic64_read(&st->decomp_out_bytes),
				       (u64)atomic64_read(&st->decomp_bad_reqs),
				       pending);
		}
	}
	return 0;
}

/* Clears stats data */
static int zip_clear_show(struct seq_file *s, void *unused)
{
	int index = 0;

	for (index = 0; index < MAX_ZIP_DEVICES; index++) {
		if (zip_dev[index]) {
			memset(&zip_dev[index]->stats, 0,
			       sizeof(struct zip_stats));
			seq_printf(s, "Cleared stats for zip %d\n", index);
		}
	}

	return 0;
}

static struct zip_registers zipregs[64] = {
	{"ZIP_CMD_CTL        ",  0x0000ull},
	{"ZIP_THROTTLE       ",  0x0010ull},
	{"ZIP_CONSTANTS      ",  0x00A0ull},
	{"ZIP_QUE0_MAP       ",  0x1400ull},
	{"ZIP_QUE1_MAP       ",  0x1408ull},
	{"ZIP_QUE_ENA        ",  0x0500ull},
	{"ZIP_QUE_PRI        ",  0x0508ull},
	{"ZIP_QUE0_DONE      ",  0x2000ull},
	{"ZIP_QUE1_DONE      ",  0x2008ull},
	{"ZIP_QUE0_DOORBELL  ",  0x4000ull},
	{"ZIP_QUE1_DOORBELL  ",  0x4008ull},
	{"ZIP_QUE0_SBUF_ADDR ",  0x1000ull},
	{"ZIP_QUE1_SBUF_ADDR ",  0x1008ull},
	{"ZIP_QUE0_SBUF_CTL  ",  0x1200ull},
	{"ZIP_QUE1_SBUF_CTL  ",  0x1208ull},
	{ NULL, 0}
};

/* Prints registers' contents */
static int zip_regs_show(struct seq_file *s, void *unused)
{
	u64 val = 0;
	int i = 0, index = 0;

	for (index = 0; index < MAX_ZIP_DEVICES; index++) {
		if (zip_dev[index]) {
			seq_printf(s, "--------------------------------\n"
				      "     ZIP Device %d Registers\n"
				      "--------------------------------\n",
				      index);

			i = 0;

			while (zipregs[i].reg_name) {
				val = zip_reg_read((zip_dev[index]->reg_base +
						    zipregs[i].reg_offset));
				seq_printf(s, "%s: 0x%016llx\n",
					   zipregs[i].reg_name, val);
				i++;
			}
		}
	}
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(zip_stats);
DEFINE_SHOW_ATTRIBUTE(zip_clear);
DEFINE_SHOW_ATTRIBUTE(zip_regs);

/* Root directory for thunderx_zip debugfs entry */
static struct dentry *zip_debugfs_root;

static void __init zip_debugfs_init(void)
{
	if (!debugfs_initialized())
		return;

	zip_debugfs_root = debugfs_create_dir("thunderx_zip", NULL);

	/* Creating files for entries inside thunderx_zip directory */
	debugfs_create_file("zip_stats", 0444, zip_debugfs_root, NULL,
			    &zip_stats_fops);

	debugfs_create_file("zip_clear", 0444, zip_debugfs_root, NULL,
			    &zip_clear_fops);

	debugfs_create_file("zip_regs", 0444, zip_debugfs_root, NULL,
			    &zip_regs_fops);

}

static void __exit zip_debugfs_exit(void)
{
	debugfs_remove_recursive(zip_debugfs_root);
}

#else
static void __init zip_debugfs_init(void) { }
static void __exit zip_debugfs_exit(void) { }
#endif
/* debugfs - end */

static int __init zip_init_module(void)
{
	int ret;

	zip_msg("%s\n", DRV_NAME);

	ret = pci_register_driver(&zip_driver);
	if (ret < 0) {
		zip_err("ZIP: pci_register_driver() failed\n");
		return ret;
	}

	/* Register with the Kernel Crypto Interface */
	ret = zip_register_compression_device();
	if (ret < 0) {
		zip_err("ZIP: Kernel Crypto Registration failed\n");
		goto err_pci_unregister;
	}

	/* comp-decomp statistics are handled with debugfs interface */
	zip_debugfs_init();

	return ret;

err_pci_unregister:
	pci_unregister_driver(&zip_driver);
	return ret;
}

static void __exit zip_cleanup_module(void)
{
	zip_debugfs_exit();

	/* Unregister from the kernel crypto interface */
	zip_unregister_compression_device();

	/* Unregister this driver for pci zip devices */
	pci_unregister_driver(&zip_driver);
}

module_init(zip_init_module);
module_exit(zip_cleanup_module);

MODULE_AUTHOR("Cavium Inc");
MODULE_DESCRIPTION("Cavium Inc ThunderX ZIP Driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, zip_id_table);
