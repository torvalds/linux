/*
 * Copyright (C) 2006-2010 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Authors: 	Shlomi Gridish <gridish@freescale.com>
 * 		Li Yang <leoli@freescale.com>
 * Based on cpm2_common.c from Dan Malek (dmalek@jlc.net)
 *
 * Description:
 * General Purpose functions for the global management of the
 * QUICC Engine (QE).
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/crc32.h>
#include <linux/mod_devicetable.h>
#include <linux/of_platform.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <soc/fsl/qe/immap_qe.h>
#include <soc/fsl/qe/qe.h>
#include <asm/prom.h>
#include <asm/rheap.h>

static void qe_snums_init(void);
static int qe_sdma_init(void);

static DEFINE_SPINLOCK(qe_lock);
DEFINE_SPINLOCK(cmxgcr_lock);
EXPORT_SYMBOL(cmxgcr_lock);

/* QE snum state */
enum qe_snum_state {
	QE_SNUM_STATE_USED,
	QE_SNUM_STATE_FREE
};

/* QE snum */
struct qe_snum {
	u8 num;
	enum qe_snum_state state;
};

/* We allocate this here because it is used almost exclusively for
 * the communication processor devices.
 */
struct qe_immap __iomem *qe_immr;
EXPORT_SYMBOL(qe_immr);

static struct qe_snum snums[QE_NUM_OF_SNUM];	/* Dynamically allocated SNUMs */
static unsigned int qe_num_of_snum;

static phys_addr_t qebase = -1;

phys_addr_t get_qe_base(void)
{
	struct device_node *qe;
	int ret;
	struct resource res;

	if (qebase != -1)
		return qebase;

	qe = of_find_compatible_node(NULL, NULL, "fsl,qe");
	if (!qe) {
		qe = of_find_node_by_type(NULL, "qe");
		if (!qe)
			return qebase;
	}

	ret = of_address_to_resource(qe, 0, &res);
	if (!ret)
		qebase = res.start;
	of_node_put(qe);

	return qebase;
}

EXPORT_SYMBOL(get_qe_base);

void qe_reset(void)
{
	if (qe_immr == NULL)
		qe_immr = ioremap(get_qe_base(), QE_IMMAP_SIZE);

	qe_snums_init();

	qe_issue_cmd(QE_RESET, QE_CR_SUBBLOCK_INVALID,
		     QE_CR_PROTOCOL_UNSPECIFIED, 0);

	/* Reclaim the MURAM memory for our use. */
	qe_muram_init();

	if (qe_sdma_init())
		panic("sdma init failed!");
}

int qe_issue_cmd(u32 cmd, u32 device, u8 mcn_protocol, u32 cmd_input)
{
	unsigned long flags;
	u8 mcn_shift = 0, dev_shift = 0;
	u32 ret;

	spin_lock_irqsave(&qe_lock, flags);
	if (cmd == QE_RESET) {
		out_be32(&qe_immr->cp.cecr, (u32) (cmd | QE_CR_FLG));
	} else {
		if (cmd == QE_ASSIGN_PAGE) {
			/* Here device is the SNUM, not sub-block */
			dev_shift = QE_CR_SNUM_SHIFT;
		} else if (cmd == QE_ASSIGN_RISC) {
			/* Here device is the SNUM, and mcnProtocol is
			 * e_QeCmdRiscAssignment value */
			dev_shift = QE_CR_SNUM_SHIFT;
			mcn_shift = QE_CR_MCN_RISC_ASSIGN_SHIFT;
		} else {
			if (device == QE_CR_SUBBLOCK_USB)
				mcn_shift = QE_CR_MCN_USB_SHIFT;
			else
				mcn_shift = QE_CR_MCN_NORMAL_SHIFT;
		}

		out_be32(&qe_immr->cp.cecdr, cmd_input);
		out_be32(&qe_immr->cp.cecr,
			 (cmd | QE_CR_FLG | ((u32) device << dev_shift) | (u32)
			  mcn_protocol << mcn_shift));
	}

	/* wait for the QE_CR_FLG to clear */
	ret = spin_event_timeout((in_be32(&qe_immr->cp.cecr) & QE_CR_FLG) == 0,
			   100, 0);
	/* On timeout (e.g. failure), the expression will be false (ret == 0),
	   otherwise it will be true (ret == 1). */
	spin_unlock_irqrestore(&qe_lock, flags);

	return ret == 1;
}
EXPORT_SYMBOL(qe_issue_cmd);

/* Set a baud rate generator. This needs lots of work. There are
 * 16 BRGs, which can be connected to the QE channels or output
 * as clocks. The BRGs are in two different block of internal
 * memory mapped space.
 * The BRG clock is the QE clock divided by 2.
 * It was set up long ago during the initial boot phase and is
 * is given to us.
 * Baud rate clocks are zero-based in the driver code (as that maps
 * to port numbers). Documentation uses 1-based numbering.
 */
static unsigned int brg_clk = 0;

unsigned int qe_get_brg_clk(void)
{
	struct device_node *qe;
	int size;
	const u32 *prop;

	if (brg_clk)
		return brg_clk;

	qe = of_find_compatible_node(NULL, NULL, "fsl,qe");
	if (!qe) {
		qe = of_find_node_by_type(NULL, "qe");
		if (!qe)
			return brg_clk;
	}

	prop = of_get_property(qe, "brg-frequency", &size);
	if (prop && size == sizeof(*prop))
		brg_clk = *prop;

	of_node_put(qe);

	return brg_clk;
}
EXPORT_SYMBOL(qe_get_brg_clk);

/* Program the BRG to the given sampling rate and multiplier
 *
 * @brg: the BRG, QE_BRG1 - QE_BRG16
 * @rate: the desired sampling rate
 * @multiplier: corresponds to the value programmed in GUMR_L[RDCR] or
 * GUMR_L[TDCR].  E.g., if this BRG is the RX clock, and GUMR_L[RDCR]=01,
 * then 'multiplier' should be 8.
 */
int qe_setbrg(enum qe_clock brg, unsigned int rate, unsigned int multiplier)
{
	u32 divisor, tempval;
	u32 div16 = 0;

	if ((brg < QE_BRG1) || (brg > QE_BRG16))
		return -EINVAL;

	divisor = qe_get_brg_clk() / (rate * multiplier);

	if (divisor > QE_BRGC_DIVISOR_MAX + 1) {
		div16 = QE_BRGC_DIV16;
		divisor /= 16;
	}

	/* Errata QE_General4, which affects some MPC832x and MPC836x SOCs, says
	   that the BRG divisor must be even if you're not using divide-by-16
	   mode. */
	if (!div16 && (divisor & 1) && (divisor > 3))
		divisor++;

	tempval = ((divisor - 1) << QE_BRGC_DIVISOR_SHIFT) |
		QE_BRGC_ENABLE | div16;

	out_be32(&qe_immr->brg.brgc[brg - QE_BRG1], tempval);

	return 0;
}
EXPORT_SYMBOL(qe_setbrg);

/* Convert a string to a QE clock source enum
 *
 * This function takes a string, typically from a property in the device
 * tree, and returns the corresponding "enum qe_clock" value.
*/
enum qe_clock qe_clock_source(const char *source)
{
	unsigned int i;

	if (strcasecmp(source, "none") == 0)
		return QE_CLK_NONE;

	if (strcmp(source, "tsync_pin") == 0)
		return QE_TSYNC_PIN;

	if (strcmp(source, "rsync_pin") == 0)
		return QE_RSYNC_PIN;

	if (strncasecmp(source, "brg", 3) == 0) {
		i = simple_strtoul(source + 3, NULL, 10);
		if ((i >= 1) && (i <= 16))
			return (QE_BRG1 - 1) + i;
		else
			return QE_CLK_DUMMY;
	}

	if (strncasecmp(source, "clk", 3) == 0) {
		i = simple_strtoul(source + 3, NULL, 10);
		if ((i >= 1) && (i <= 24))
			return (QE_CLK1 - 1) + i;
		else
			return QE_CLK_DUMMY;
	}

	return QE_CLK_DUMMY;
}
EXPORT_SYMBOL(qe_clock_source);

/* Initialize SNUMs (thread serial numbers) according to
 * QE Module Control chapter, SNUM table
 */
static void qe_snums_init(void)
{
	int i;
	static const u8 snum_init_76[] = {
		0x04, 0x05, 0x0C, 0x0D, 0x14, 0x15, 0x1C, 0x1D,
		0x24, 0x25, 0x2C, 0x2D, 0x34, 0x35, 0x88, 0x89,
		0x98, 0x99, 0xA8, 0xA9, 0xB8, 0xB9, 0xC8, 0xC9,
		0xD8, 0xD9, 0xE8, 0xE9, 0x44, 0x45, 0x4C, 0x4D,
		0x54, 0x55, 0x5C, 0x5D, 0x64, 0x65, 0x6C, 0x6D,
		0x74, 0x75, 0x7C, 0x7D, 0x84, 0x85, 0x8C, 0x8D,
		0x94, 0x95, 0x9C, 0x9D, 0xA4, 0xA5, 0xAC, 0xAD,
		0xB4, 0xB5, 0xBC, 0xBD, 0xC4, 0xC5, 0xCC, 0xCD,
		0xD4, 0xD5, 0xDC, 0xDD, 0xE4, 0xE5, 0xEC, 0xED,
		0xF4, 0xF5, 0xFC, 0xFD,
	};
	static const u8 snum_init_46[] = {
		0x04, 0x05, 0x0C, 0x0D, 0x14, 0x15, 0x1C, 0x1D,
		0x24, 0x25, 0x2C, 0x2D, 0x34, 0x35, 0x88, 0x89,
		0x98, 0x99, 0xA8, 0xA9, 0xB8, 0xB9, 0xC8, 0xC9,
		0xD8, 0xD9, 0xE8, 0xE9, 0x08, 0x09, 0x18, 0x19,
		0x28, 0x29, 0x38, 0x39, 0x48, 0x49, 0x58, 0x59,
		0x68, 0x69, 0x78, 0x79, 0x80, 0x81,
	};
	static const u8 *snum_init;

	qe_num_of_snum = qe_get_num_of_snums();

	if (qe_num_of_snum == 76)
		snum_init = snum_init_76;
	else
		snum_init = snum_init_46;

	for (i = 0; i < qe_num_of_snum; i++) {
		snums[i].num = snum_init[i];
		snums[i].state = QE_SNUM_STATE_FREE;
	}
}

int qe_get_snum(void)
{
	unsigned long flags;
	int snum = -EBUSY;
	int i;

	spin_lock_irqsave(&qe_lock, flags);
	for (i = 0; i < qe_num_of_snum; i++) {
		if (snums[i].state == QE_SNUM_STATE_FREE) {
			snums[i].state = QE_SNUM_STATE_USED;
			snum = snums[i].num;
			break;
		}
	}
	spin_unlock_irqrestore(&qe_lock, flags);

	return snum;
}
EXPORT_SYMBOL(qe_get_snum);

void qe_put_snum(u8 snum)
{
	int i;

	for (i = 0; i < qe_num_of_snum; i++) {
		if (snums[i].num == snum) {
			snums[i].state = QE_SNUM_STATE_FREE;
			break;
		}
	}
}
EXPORT_SYMBOL(qe_put_snum);

static int qe_sdma_init(void)
{
	struct sdma __iomem *sdma = &qe_immr->sdma;
	static unsigned long sdma_buf_offset = (unsigned long)-ENOMEM;

	if (!sdma)
		return -ENODEV;

	/* allocate 2 internal temporary buffers (512 bytes size each) for
	 * the SDMA */
	if (IS_ERR_VALUE(sdma_buf_offset)) {
		sdma_buf_offset = qe_muram_alloc(512 * 2, 4096);
		if (IS_ERR_VALUE(sdma_buf_offset))
			return -ENOMEM;
	}

	out_be32(&sdma->sdebcr, (u32) sdma_buf_offset & QE_SDEBCR_BA_MASK);
 	out_be32(&sdma->sdmr, (QE_SDMR_GLB_1_MSK |
 					(0x1 << QE_SDMR_CEN_SHIFT)));

	return 0;
}

/* The maximum number of RISCs we support */
#define MAX_QE_RISC     4

/* Firmware information stored here for qe_get_firmware_info() */
static struct qe_firmware_info qe_firmware_info;

/*
 * Set to 1 if QE firmware has been uploaded, and therefore
 * qe_firmware_info contains valid data.
 */
static int qe_firmware_uploaded;

/*
 * Upload a QE microcode
 *
 * This function is a worker function for qe_upload_firmware().  It does
 * the actual uploading of the microcode.
 */
static void qe_upload_microcode(const void *base,
	const struct qe_microcode *ucode)
{
	const __be32 *code = base + be32_to_cpu(ucode->code_offset);
	unsigned int i;

	if (ucode->major || ucode->minor || ucode->revision)
		printk(KERN_INFO "qe-firmware: "
			"uploading microcode '%s' version %u.%u.%u\n",
			ucode->id, ucode->major, ucode->minor, ucode->revision);
	else
		printk(KERN_INFO "qe-firmware: "
			"uploading microcode '%s'\n", ucode->id);

	/* Use auto-increment */
	out_be32(&qe_immr->iram.iadd, be32_to_cpu(ucode->iram_offset) |
		QE_IRAM_IADD_AIE | QE_IRAM_IADD_BADDR);

	for (i = 0; i < be32_to_cpu(ucode->count); i++)
		out_be32(&qe_immr->iram.idata, be32_to_cpu(code[i]));
	
	/* Set I-RAM Ready Register */
	out_be32(&qe_immr->iram.iready, be32_to_cpu(QE_IRAM_READY));
}

/*
 * Upload a microcode to the I-RAM at a specific address.
 *
 * See Documentation/powerpc/qe_firmware.txt for information on QE microcode
 * uploading.
 *
 * Currently, only version 1 is supported, so the 'version' field must be
 * set to 1.
 *
 * The SOC model and revision are not validated, they are only displayed for
 * informational purposes.
 *
 * 'calc_size' is the calculated size, in bytes, of the firmware structure and
 * all of the microcode structures, minus the CRC.
 *
 * 'length' is the size that the structure says it is, including the CRC.
 */
int qe_upload_firmware(const struct qe_firmware *firmware)
{
	unsigned int i;
	unsigned int j;
	u32 crc;
	size_t calc_size = sizeof(struct qe_firmware);
	size_t length;
	const struct qe_header *hdr;

	if (!firmware) {
		printk(KERN_ERR "qe-firmware: invalid pointer\n");
		return -EINVAL;
	}

	hdr = &firmware->header;
	length = be32_to_cpu(hdr->length);

	/* Check the magic */
	if ((hdr->magic[0] != 'Q') || (hdr->magic[1] != 'E') ||
	    (hdr->magic[2] != 'F')) {
		printk(KERN_ERR "qe-firmware: not a microcode\n");
		return -EPERM;
	}

	/* Check the version */
	if (hdr->version != 1) {
		printk(KERN_ERR "qe-firmware: unsupported version\n");
		return -EPERM;
	}

	/* Validate some of the fields */
	if ((firmware->count < 1) || (firmware->count > MAX_QE_RISC)) {
		printk(KERN_ERR "qe-firmware: invalid data\n");
		return -EINVAL;
	}

	/* Validate the length and check if there's a CRC */
	calc_size += (firmware->count - 1) * sizeof(struct qe_microcode);

	for (i = 0; i < firmware->count; i++)
		/*
		 * For situations where the second RISC uses the same microcode
		 * as the first, the 'code_offset' and 'count' fields will be
		 * zero, so it's okay to add those.
		 */
		calc_size += sizeof(__be32) *
			be32_to_cpu(firmware->microcode[i].count);

	/* Validate the length */
	if (length != calc_size + sizeof(__be32)) {
		printk(KERN_ERR "qe-firmware: invalid length\n");
		return -EPERM;
	}

	/* Validate the CRC */
	crc = be32_to_cpu(*(__be32 *)((void *)firmware + calc_size));
	if (crc != crc32(0, firmware, calc_size)) {
		printk(KERN_ERR "qe-firmware: firmware CRC is invalid\n");
		return -EIO;
	}

	/*
	 * If the microcode calls for it, split the I-RAM.
	 */
	if (!firmware->split)
		setbits16(&qe_immr->cp.cercr, QE_CP_CERCR_CIR);

	if (firmware->soc.model)
		printk(KERN_INFO
			"qe-firmware: firmware '%s' for %u V%u.%u\n",
			firmware->id, be16_to_cpu(firmware->soc.model),
			firmware->soc.major, firmware->soc.minor);
	else
		printk(KERN_INFO "qe-firmware: firmware '%s'\n",
			firmware->id);

	/*
	 * The QE only supports one microcode per RISC, so clear out all the
	 * saved microcode information and put in the new.
	 */
	memset(&qe_firmware_info, 0, sizeof(qe_firmware_info));
	strlcpy(qe_firmware_info.id, firmware->id, sizeof(qe_firmware_info.id));
	qe_firmware_info.extended_modes = firmware->extended_modes;
	memcpy(qe_firmware_info.vtraps, firmware->vtraps,
		sizeof(firmware->vtraps));

	/* Loop through each microcode. */
	for (i = 0; i < firmware->count; i++) {
		const struct qe_microcode *ucode = &firmware->microcode[i];

		/* Upload a microcode if it's present */
		if (ucode->code_offset)
			qe_upload_microcode(firmware, ucode);

		/* Program the traps for this processor */
		for (j = 0; j < 16; j++) {
			u32 trap = be32_to_cpu(ucode->traps[j]);

			if (trap)
				out_be32(&qe_immr->rsp[i].tibcr[j], trap);
		}

		/* Enable traps */
		out_be32(&qe_immr->rsp[i].eccr, be32_to_cpu(ucode->eccr));
	}

	qe_firmware_uploaded = 1;

	return 0;
}
EXPORT_SYMBOL(qe_upload_firmware);

/*
 * Get info on the currently-loaded firmware
 *
 * This function also checks the device tree to see if the boot loader has
 * uploaded a firmware already.
 */
struct qe_firmware_info *qe_get_firmware_info(void)
{
	static int initialized;
	struct property *prop;
	struct device_node *qe;
	struct device_node *fw = NULL;
	const char *sprop;
	unsigned int i;

	/*
	 * If we haven't checked yet, and a driver hasn't uploaded a firmware
	 * yet, then check the device tree for information.
	 */
	if (qe_firmware_uploaded)
		return &qe_firmware_info;

	if (initialized)
		return NULL;

	initialized = 1;

	/*
	 * Newer device trees have an "fsl,qe" compatible property for the QE
	 * node, but we still need to support older device trees.
	*/
	qe = of_find_compatible_node(NULL, NULL, "fsl,qe");
	if (!qe) {
		qe = of_find_node_by_type(NULL, "qe");
		if (!qe)
			return NULL;
	}

	/* Find the 'firmware' child node */
	for_each_child_of_node(qe, fw) {
		if (strcmp(fw->name, "firmware") == 0)
			break;
	}

	of_node_put(qe);

	/* Did we find the 'firmware' node? */
	if (!fw)
		return NULL;

	qe_firmware_uploaded = 1;

	/* Copy the data into qe_firmware_info*/
	sprop = of_get_property(fw, "id", NULL);
	if (sprop)
		strlcpy(qe_firmware_info.id, sprop,
			sizeof(qe_firmware_info.id));

	prop = of_find_property(fw, "extended-modes", NULL);
	if (prop && (prop->length == sizeof(u64))) {
		const u64 *iprop = prop->value;

		qe_firmware_info.extended_modes = *iprop;
	}

	prop = of_find_property(fw, "virtual-traps", NULL);
	if (prop && (prop->length == 32)) {
		const u32 *iprop = prop->value;

		for (i = 0; i < ARRAY_SIZE(qe_firmware_info.vtraps); i++)
			qe_firmware_info.vtraps[i] = iprop[i];
	}

	of_node_put(fw);

	return &qe_firmware_info;
}
EXPORT_SYMBOL(qe_get_firmware_info);

unsigned int qe_get_num_of_risc(void)
{
	struct device_node *qe;
	int size;
	unsigned int num_of_risc = 0;
	const u32 *prop;

	qe = of_find_compatible_node(NULL, NULL, "fsl,qe");
	if (!qe) {
		/* Older devices trees did not have an "fsl,qe"
		 * compatible property, so we need to look for
		 * the QE node by name.
		 */
		qe = of_find_node_by_type(NULL, "qe");
		if (!qe)
			return num_of_risc;
	}

	prop = of_get_property(qe, "fsl,qe-num-riscs", &size);
	if (prop && size == sizeof(*prop))
		num_of_risc = *prop;

	of_node_put(qe);

	return num_of_risc;
}
EXPORT_SYMBOL(qe_get_num_of_risc);

unsigned int qe_get_num_of_snums(void)
{
	struct device_node *qe;
	int size;
	unsigned int num_of_snums;
	const u32 *prop;

	num_of_snums = 28; /* The default number of snum for threads is 28 */
	qe = of_find_compatible_node(NULL, NULL, "fsl,qe");
	if (!qe) {
		/* Older devices trees did not have an "fsl,qe"
		 * compatible property, so we need to look for
		 * the QE node by name.
		 */
		qe = of_find_node_by_type(NULL, "qe");
		if (!qe)
			return num_of_snums;
	}

	prop = of_get_property(qe, "fsl,qe-num-snums", &size);
	if (prop && size == sizeof(*prop)) {
		num_of_snums = *prop;
		if ((num_of_snums < 28) || (num_of_snums > QE_NUM_OF_SNUM)) {
			/* No QE ever has fewer than 28 SNUMs */
			pr_err("QE: number of snum is invalid\n");
			of_node_put(qe);
			return -EINVAL;
		}
	}

	of_node_put(qe);

	return num_of_snums;
}
EXPORT_SYMBOL(qe_get_num_of_snums);

static int __init qe_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,qe");
	if (!np)
		return -ENODEV;
	qe_reset();
	of_node_put(np);
	return 0;
}
subsys_initcall(qe_init);

#if defined(CONFIG_SUSPEND) && defined(CONFIG_PPC_85xx)
static int qe_resume(struct platform_device *ofdev)
{
	if (!qe_alive_during_sleep())
		qe_reset();
	return 0;
}

static int qe_probe(struct platform_device *ofdev)
{
	return 0;
}

static const struct of_device_id qe_ids[] = {
	{ .compatible = "fsl,qe", },
	{ },
};

static struct platform_driver qe_driver = {
	.driver = {
		.name = "fsl-qe",
		.of_match_table = qe_ids,
	},
	.probe = qe_probe,
	.resume = qe_resume,
};

static int __init qe_drv_init(void)
{
	return platform_driver_register(&qe_driver);
}
device_initcall(qe_drv_init);
#endif /* defined(CONFIG_SUSPEND) && defined(CONFIG_PPC_85xx) */
