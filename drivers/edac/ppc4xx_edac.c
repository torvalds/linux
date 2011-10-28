/*
 * Copyright (c) 2008 Nuovation System Designs, LLC
 *   Grant Erickson <gerickson@nuovations.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 */

#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/types.h>

#include <asm/dcr.h>

#include "edac_core.h"
#include "ppc4xx_edac.h"

/*
 * This file implements a driver for monitoring and handling events
 * associated with the IMB DDR2 ECC controller found in the AMCC/IBM
 * 405EX[r], 440SP, 440SPe, 460EX, 460GT and 460SX.
 *
 * As realized in the 405EX[r], this controller features:
 *
 *   - Support for registered- and non-registered DDR1 and DDR2 memory.
 *   - 32-bit or 16-bit memory interface with optional ECC.
 *
 *     o ECC support includes:
 *
 *       - 4-bit SEC/DED
 *       - Aligned-nibble error detect
 *       - Bypass mode
 *
 *   - Two (2) memory banks/ranks.
 *   - Up to 1 GiB per bank/rank in 32-bit mode and up to 512 MiB per
 *     bank/rank in 16-bit mode.
 *
 * As realized in the 440SP and 440SPe, this controller changes/adds:
 *
 *   - 64-bit or 32-bit memory interface with optional ECC.
 *
 *     o ECC support includes:
 *
 *       - 8-bit SEC/DED
 *       - Aligned-nibble error detect
 *       - Bypass mode
 *
 *   - Up to 4 GiB per bank/rank in 64-bit mode and up to 2 GiB
 *     per bank/rank in 32-bit mode.
 *
 * As realized in the 460EX and 460GT, this controller changes/adds:
 *
 *   - 64-bit or 32-bit memory interface with optional ECC.
 *
 *     o ECC support includes:
 *
 *       - 8-bit SEC/DED
 *       - Aligned-nibble error detect
 *       - Bypass mode
 *
 *   - Four (4) memory banks/ranks.
 *   - Up to 16 GiB per bank/rank in 64-bit mode and up to 8 GiB
 *     per bank/rank in 32-bit mode.
 *
 * At present, this driver has ONLY been tested against the controller
 * realization in the 405EX[r] on the AMCC Kilauea and Haleakala
 * boards (256 MiB w/o ECC memory soldered onto the board) and a
 * proprietary board based on those designs (128 MiB ECC memory, also
 * soldered onto the board).
 *
 * Dynamic feature detection and handling needs to be added for the
 * other realizations of this controller listed above.
 *
 * Eventually, this driver will likely be adapted to the above variant
 * realizations of this controller as well as broken apart to handle
 * the other known ECC-capable controllers prevalent in other 4xx
 * processors:
 *
 *   - IBM SDRAM (405GP, 405CR and 405EP) "ibm,sdram-4xx"
 *   - IBM DDR1 (440GP, 440GX, 440EP and 440GR) "ibm,sdram-4xx-ddr"
 *   - Denali DDR1/DDR2 (440EPX and 440GRX) "denali,sdram-4xx-ddr2"
 *
 * For this controller, unfortunately, correctable errors report
 * nothing more than the beat/cycle and byte/lane the correction
 * occurred on and the check bit group that covered the error.
 *
 * In contrast, uncorrectable errors also report the failing address,
 * the bus master and the transaction direction (i.e. read or write)
 *
 * Regardless of whether the error is a CE or a UE, we report the
 * following pieces of information in the driver-unique message to the
 * EDAC subsystem:
 *
 *   - Device tree path
 *   - Bank(s)
 *   - Check bit error group
 *   - Beat(s)/lane(s)
 */

/* Preprocessor Definitions */

#define EDAC_OPSTATE_INT_STR		"interrupt"
#define EDAC_OPSTATE_POLL_STR		"polled"
#define EDAC_OPSTATE_UNKNOWN_STR	"unknown"

#define PPC4XX_EDAC_MODULE_NAME		"ppc4xx_edac"
#define PPC4XX_EDAC_MODULE_REVISION	"v1.0.0"

#define PPC4XX_EDAC_MESSAGE_SIZE	256

/*
 * Kernel logging without an EDAC instance
 */
#define ppc4xx_edac_printk(level, fmt, arg...) \
	edac_printk(level, "PPC4xx MC", fmt, ##arg)

/*
 * Kernel logging with an EDAC instance
 */
#define ppc4xx_edac_mc_printk(level, mci, fmt, arg...) \
	edac_mc_chipset_printk(mci, level, "PPC4xx", fmt, ##arg)

/*
 * Macros to convert bank configuration size enumerations into MiB and
 * page values.
 */
#define SDRAM_MBCF_SZ_MiB_MIN		4
#define SDRAM_MBCF_SZ_TO_MiB(n)		(SDRAM_MBCF_SZ_MiB_MIN \
					 << (SDRAM_MBCF_SZ_DECODE(n)))
#define SDRAM_MBCF_SZ_TO_PAGES(n)	(SDRAM_MBCF_SZ_MiB_MIN \
					 << (20 - PAGE_SHIFT + \
					     SDRAM_MBCF_SZ_DECODE(n)))

/*
 * The ibm,sdram-4xx-ddr2 Device Control Registers (DCRs) are
 * indirectly acccessed and have a base and length defined by the
 * device tree. The base can be anything; however, we expect the
 * length to be precisely two registers, the first for the address
 * window and the second for the data window.
 */
#define SDRAM_DCR_RESOURCE_LEN		2
#define SDRAM_DCR_ADDR_OFFSET		0
#define SDRAM_DCR_DATA_OFFSET		1

/*
 * Device tree interrupt indices
 */
#define INTMAP_ECCDED_INDEX		0	/* Double-bit Error Detect */
#define INTMAP_ECCSEC_INDEX		1	/* Single-bit Error Correct */

/* Type Definitions */

/*
 * PPC4xx SDRAM memory controller private instance data
 */
struct ppc4xx_edac_pdata {
	dcr_host_t dcr_host;	/* Indirect DCR address/data window mapping */
	struct {
		int sec;	/* Single-bit correctable error IRQ assigned */
		int ded;	/* Double-bit detectable error IRQ assigned */
	} irqs;
};

/*
 * Various status data gathered and manipulated when checking and
 * reporting ECC status.
 */
struct ppc4xx_ecc_status {
	u32 ecces;
	u32 besr;
	u32 bearh;
	u32 bearl;
	u32 wmirq;
};

/* Function Prototypes */

static int ppc4xx_edac_probe(struct platform_device *device)
static int ppc4xx_edac_remove(struct platform_device *device);

/* Global Variables */

/*
 * Device tree node type and compatible tuples this driver can match
 * on.
 */
static struct of_device_id ppc4xx_edac_match[] = {
	{
		.compatible	= "ibm,sdram-4xx-ddr2"
	},
	{ }
};

static struct platform_driver ppc4xx_edac_driver = {
	.probe			= ppc4xx_edac_probe,
	.remove			= ppc4xx_edac_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = PPC4XX_EDAC_MODULE_NAME
		.of_match_table = ppc4xx_edac_match,
	},
};

/*
 * TODO: The row and channel parameters likely need to be dynamically
 * set based on the aforementioned variant controller realizations.
 */
static const unsigned ppc4xx_edac_nr_csrows = 2;
static const unsigned ppc4xx_edac_nr_chans = 1;

/*
 * Strings associated with PLB master IDs capable of being posted in
 * SDRAM_BESR or SDRAM_WMIRQ on uncorrectable ECC errors.
 */
static const char * const ppc4xx_plb_masters[9] = {
	[SDRAM_PLB_M0ID_ICU]	= "ICU",
	[SDRAM_PLB_M0ID_PCIE0]	= "PCI-E 0",
	[SDRAM_PLB_M0ID_PCIE1]	= "PCI-E 1",
	[SDRAM_PLB_M0ID_DMA]	= "DMA",
	[SDRAM_PLB_M0ID_DCU]	= "DCU",
	[SDRAM_PLB_M0ID_OPB]	= "OPB",
	[SDRAM_PLB_M0ID_MAL]	= "MAL",
	[SDRAM_PLB_M0ID_SEC]	= "SEC",
	[SDRAM_PLB_M0ID_AHB]	= "AHB"
};

/**
 * mfsdram - read and return controller register data
 * @dcr_host: A pointer to the DCR mapping.
 * @idcr_n: The indirect DCR register to read.
 *
 * This routine reads and returns the data associated with the
 * controller's specified indirect DCR register.
 *
 * Returns the read data.
 */
static inline u32
mfsdram(const dcr_host_t *dcr_host, unsigned int idcr_n)
{
	return __mfdcri(dcr_host->base + SDRAM_DCR_ADDR_OFFSET,
			dcr_host->base + SDRAM_DCR_DATA_OFFSET,
			idcr_n);
}

/**
 * mtsdram - write controller register data
 * @dcr_host: A pointer to the DCR mapping.
 * @idcr_n: The indirect DCR register to write.
 * @value: The data to write.
 *
 * This routine writes the provided data to the controller's specified
 * indirect DCR register.
 */
static inline void
mtsdram(const dcr_host_t *dcr_host, unsigned int idcr_n, u32 value)
{
	return __mtdcri(dcr_host->base + SDRAM_DCR_ADDR_OFFSET,
			dcr_host->base + SDRAM_DCR_DATA_OFFSET,
			idcr_n,
			value);
}

/**
 * ppc4xx_edac_check_bank_error - check a bank for an ECC bank error
 * @status: A pointer to the ECC status structure to check for an
 *          ECC bank error.
 * @bank: The bank to check for an ECC error.
 *
 * This routine determines whether the specified bank has an ECC
 * error.
 *
 * Returns true if the specified bank has an ECC error; otherwise,
 * false.
 */
static bool
ppc4xx_edac_check_bank_error(const struct ppc4xx_ecc_status *status,
			     unsigned int bank)
{
	switch (bank) {
	case 0:
		return status->ecces & SDRAM_ECCES_BK0ER;
	case 1:
		return status->ecces & SDRAM_ECCES_BK1ER;
	default:
		return false;
	}
}

/**
 * ppc4xx_edac_generate_bank_message - generate interpretted bank status message
 * @mci: A pointer to the EDAC memory controller instance associated
 *       with the bank message being generated.
 * @status: A pointer to the ECC status structure to generate the
 *          message from.
 * @buffer: A pointer to the buffer in which to generate the
 *          message.
 * @size: The size, in bytes, of space available in buffer.
 *
 * This routine generates to the provided buffer the portion of the
 * driver-unique report message associated with the ECCESS[BKNER]
 * field of the specified ECC status.
 *
 * Returns the number of characters generated on success; otherwise, <
 * 0 on error.
 */
static int
ppc4xx_edac_generate_bank_message(const struct mem_ctl_info *mci,
				  const struct ppc4xx_ecc_status *status,
				  char *buffer,
				  size_t size)
{
	int n, total = 0;
	unsigned int row, rows;

	n = snprintf(buffer, size, "%s: Banks: ", mci->dev_name);

	if (n < 0 || n >= size)
		goto fail;

	buffer += n;
	size -= n;
	total += n;

	for (rows = 0, row = 0; row < mci->nr_csrows; row++) {
		if (ppc4xx_edac_check_bank_error(status, row)) {
			n = snprintf(buffer, size, "%s%u",
					(rows++ ? ", " : ""), row);

			if (n < 0 || n >= size)
				goto fail;

			buffer += n;
			size -= n;
			total += n;
		}
	}

	n = snprintf(buffer, size, "%s; ", rows ? "" : "None");

	if (n < 0 || n >= size)
		goto fail;

	buffer += n;
	size -= n;
	total += n;

 fail:
	return total;
}

/**
 * ppc4xx_edac_generate_checkbit_message - generate interpretted checkbit message
 * @mci: A pointer to the EDAC memory controller instance associated
 *       with the checkbit message being generated.
 * @status: A pointer to the ECC status structure to generate the
 *          message from.
 * @buffer: A pointer to the buffer in which to generate the
 *          message.
 * @size: The size, in bytes, of space available in buffer.
 *
 * This routine generates to the provided buffer the portion of the
 * driver-unique report message associated with the ECCESS[CKBER]
 * field of the specified ECC status.
 *
 * Returns the number of characters generated on success; otherwise, <
 * 0 on error.
 */
static int
ppc4xx_edac_generate_checkbit_message(const struct mem_ctl_info *mci,
				      const struct ppc4xx_ecc_status *status,
				      char *buffer,
				      size_t size)
{
	const struct ppc4xx_edac_pdata *pdata = mci->pvt_info;
	const char *ckber = NULL;

	switch (status->ecces & SDRAM_ECCES_CKBER_MASK) {
	case SDRAM_ECCES_CKBER_NONE:
		ckber = "None";
		break;
	case SDRAM_ECCES_CKBER_32_ECC_0_3:
		ckber = "ECC0:3";
		break;
	case SDRAM_ECCES_CKBER_32_ECC_4_8:
		switch (mfsdram(&pdata->dcr_host, SDRAM_MCOPT1) &
			SDRAM_MCOPT1_WDTH_MASK) {
		case SDRAM_MCOPT1_WDTH_16:
			ckber = "ECC0:3";
			break;
		case SDRAM_MCOPT1_WDTH_32:
			ckber = "ECC4:8";
			break;
		default:
			ckber = "Unknown";
			break;
		}
		break;
	case SDRAM_ECCES_CKBER_32_ECC_0_8:
		ckber = "ECC0:8";
		break;
	default:
		ckber = "Unknown";
		break;
	}

	return snprintf(buffer, size, "Checkbit Error: %s", ckber);
}

/**
 * ppc4xx_edac_generate_lane_message - generate interpretted byte lane message
 * @mci: A pointer to the EDAC memory controller instance associated
 *       with the byte lane message being generated.
 * @status: A pointer to the ECC status structure to generate the
 *          message from.
 * @buffer: A pointer to the buffer in which to generate the
 *          message.
 * @size: The size, in bytes, of space available in buffer.
 *
 * This routine generates to the provided buffer the portion of the
 * driver-unique report message associated with the ECCESS[BNCE]
 * field of the specified ECC status.
 *
 * Returns the number of characters generated on success; otherwise, <
 * 0 on error.
 */
static int
ppc4xx_edac_generate_lane_message(const struct mem_ctl_info *mci,
				  const struct ppc4xx_ecc_status *status,
				  char *buffer,
				  size_t size)
{
	int n, total = 0;
	unsigned int lane, lanes;
	const unsigned int first_lane = 0;
	const unsigned int lane_count = 16;

	n = snprintf(buffer, size, "; Byte Lane Errors: ");

	if (n < 0 || n >= size)
		goto fail;

	buffer += n;
	size -= n;
	total += n;

	for (lanes = 0, lane = first_lane; lane < lane_count; lane++) {
		if ((status->ecces & SDRAM_ECCES_BNCE_ENCODE(lane)) != 0) {
			n = snprintf(buffer, size,
				     "%s%u",
				     (lanes++ ? ", " : ""), lane);

			if (n < 0 || n >= size)
				goto fail;

			buffer += n;
			size -= n;
			total += n;
		}
	}

	n = snprintf(buffer, size, "%s; ", lanes ? "" : "None");

	if (n < 0 || n >= size)
		goto fail;

	buffer += n;
	size -= n;
	total += n;

 fail:
	return total;
}

/**
 * ppc4xx_edac_generate_ecc_message - generate interpretted ECC status message
 * @mci: A pointer to the EDAC memory controller instance associated
 *       with the ECCES message being generated.
 * @status: A pointer to the ECC status structure to generate the
 *          message from.
 * @buffer: A pointer to the buffer in which to generate the
 *          message.
 * @size: The size, in bytes, of space available in buffer.
 *
 * This routine generates to the provided buffer the portion of the
 * driver-unique report message associated with the ECCESS register of
 * the specified ECC status.
 *
 * Returns the number of characters generated on success; otherwise, <
 * 0 on error.
 */
static int
ppc4xx_edac_generate_ecc_message(const struct mem_ctl_info *mci,
				 const struct ppc4xx_ecc_status *status,
				 char *buffer,
				 size_t size)
{
	int n, total = 0;

	n = ppc4xx_edac_generate_bank_message(mci, status, buffer, size);

	if (n < 0 || n >= size)
		goto fail;

	buffer += n;
	size -= n;
	total += n;

	n = ppc4xx_edac_generate_checkbit_message(mci, status, buffer, size);

	if (n < 0 || n >= size)
		goto fail;

	buffer += n;
	size -= n;
	total += n;

	n = ppc4xx_edac_generate_lane_message(mci, status, buffer, size);

	if (n < 0 || n >= size)
		goto fail;

	buffer += n;
	size -= n;
	total += n;

 fail:
	return total;
}

/**
 * ppc4xx_edac_generate_plb_message - generate interpretted PLB status message
 * @mci: A pointer to the EDAC memory controller instance associated
 *       with the PLB message being generated.
 * @status: A pointer to the ECC status structure to generate the
 *          message from.
 * @buffer: A pointer to the buffer in which to generate the
 *          message.
 * @size: The size, in bytes, of space available in buffer.
 *
 * This routine generates to the provided buffer the portion of the
 * driver-unique report message associated with the PLB-related BESR
 * and/or WMIRQ registers of the specified ECC status.
 *
 * Returns the number of characters generated on success; otherwise, <
 * 0 on error.
 */
static int
ppc4xx_edac_generate_plb_message(const struct mem_ctl_info *mci,
				 const struct ppc4xx_ecc_status *status,
				 char *buffer,
				 size_t size)
{
	unsigned int master;
	bool read;

	if ((status->besr & SDRAM_BESR_MASK) == 0)
		return 0;

	if ((status->besr & SDRAM_BESR_M0ET_MASK) == SDRAM_BESR_M0ET_NONE)
		return 0;

	read = ((status->besr & SDRAM_BESR_M0RW_MASK) == SDRAM_BESR_M0RW_READ);

	master = SDRAM_BESR_M0ID_DECODE(status->besr);

	return snprintf(buffer, size,
			"%s error w/ PLB master %u \"%s\"; ",
			(read ? "Read" : "Write"),
			master,
			(((master >= SDRAM_PLB_M0ID_FIRST) &&
			  (master <= SDRAM_PLB_M0ID_LAST)) ?
			 ppc4xx_plb_masters[master] : "UNKNOWN"));
}

/**
 * ppc4xx_edac_generate_message - generate interpretted status message
 * @mci: A pointer to the EDAC memory controller instance associated
 *       with the driver-unique message being generated.
 * @status: A pointer to the ECC status structure to generate the
 *          message from.
 * @buffer: A pointer to the buffer in which to generate the
 *          message.
 * @size: The size, in bytes, of space available in buffer.
 *
 * This routine generates to the provided buffer the driver-unique
 * EDAC report message from the specified ECC status.
 */
static void
ppc4xx_edac_generate_message(const struct mem_ctl_info *mci,
			     const struct ppc4xx_ecc_status *status,
			     char *buffer,
			     size_t size)
{
	int n;

	if (buffer == NULL || size == 0)
		return;

	n = ppc4xx_edac_generate_ecc_message(mci, status, buffer, size);

	if (n < 0 || n >= size)
		return;

	buffer += n;
	size -= n;

	ppc4xx_edac_generate_plb_message(mci, status, buffer, size);
}

#ifdef DEBUG
/**
 * ppc4xx_ecc_dump_status - dump controller ECC status registers
 * @mci: A pointer to the EDAC memory controller instance
 *       associated with the status being dumped.
 * @status: A pointer to the ECC status structure to generate the
 *          dump from.
 *
 * This routine dumps to the kernel log buffer the raw and
 * interpretted specified ECC status.
 */
static void
ppc4xx_ecc_dump_status(const struct mem_ctl_info *mci,
		       const struct ppc4xx_ecc_status *status)
{
	char message[PPC4XX_EDAC_MESSAGE_SIZE];

	ppc4xx_edac_generate_message(mci, status, message, sizeof(message));

	ppc4xx_edac_mc_printk(KERN_INFO, mci,
			      "\n"
			      "\tECCES: 0x%08x\n"
			      "\tWMIRQ: 0x%08x\n"
			      "\tBESR:  0x%08x\n"
			      "\tBEAR:  0x%08x%08x\n"
			      "\t%s\n",
			      status->ecces,
			      status->wmirq,
			      status->besr,
			      status->bearh,
			      status->bearl,
			      message);
}
#endif /* DEBUG */

/**
 * ppc4xx_ecc_get_status - get controller ECC status
 * @mci: A pointer to the EDAC memory controller instance
 *       associated with the status being retrieved.
 * @status: A pointer to the ECC status structure to populate the
 *          ECC status with.
 *
 * This routine reads and masks, as appropriate, all the relevant
 * status registers that deal with ibm,sdram-4xx-ddr2 ECC errors.
 * While we read all of them, for correctable errors, we only expect
 * to deal with ECCES. For uncorrectable errors, we expect to deal
 * with all of them.
 */
static void
ppc4xx_ecc_get_status(const struct mem_ctl_info *mci,
		      struct ppc4xx_ecc_status *status)
{
	const struct ppc4xx_edac_pdata *pdata = mci->pvt_info;
	const dcr_host_t *dcr_host = &pdata->dcr_host;

	status->ecces = mfsdram(dcr_host, SDRAM_ECCES) & SDRAM_ECCES_MASK;
	status->wmirq = mfsdram(dcr_host, SDRAM_WMIRQ) & SDRAM_WMIRQ_MASK;
	status->besr  = mfsdram(dcr_host, SDRAM_BESR)  & SDRAM_BESR_MASK;
	status->bearl = mfsdram(dcr_host, SDRAM_BEARL);
	status->bearh = mfsdram(dcr_host, SDRAM_BEARH);
}

/**
 * ppc4xx_ecc_clear_status - clear controller ECC status
 * @mci: A pointer to the EDAC memory controller instance
 *       associated with the status being cleared.
 * @status: A pointer to the ECC status structure containing the
 *          values to write to clear the ECC status.
 *
 * This routine clears--by writing the masked (as appropriate) status
 * values back to--the status registers that deal with
 * ibm,sdram-4xx-ddr2 ECC errors.
 */
static void
ppc4xx_ecc_clear_status(const struct mem_ctl_info *mci,
			const struct ppc4xx_ecc_status *status)
{
	const struct ppc4xx_edac_pdata *pdata = mci->pvt_info;
	const dcr_host_t *dcr_host = &pdata->dcr_host;

	mtsdram(dcr_host, SDRAM_ECCES,	status->ecces & SDRAM_ECCES_MASK);
	mtsdram(dcr_host, SDRAM_WMIRQ,	status->wmirq & SDRAM_WMIRQ_MASK);
	mtsdram(dcr_host, SDRAM_BESR,	status->besr & SDRAM_BESR_MASK);
	mtsdram(dcr_host, SDRAM_BEARL,	0);
	mtsdram(dcr_host, SDRAM_BEARH,	0);
}

/**
 * ppc4xx_edac_handle_ce - handle controller correctable ECC error (CE)
 * @mci: A pointer to the EDAC memory controller instance
 *       associated with the correctable error being handled and reported.
 * @status: A pointer to the ECC status structure associated with
 *          the correctable error being handled and reported.
 *
 * This routine handles an ibm,sdram-4xx-ddr2 controller ECC
 * correctable error. Per the aforementioned discussion, there's not
 * enough status available to use the full EDAC correctable error
 * interface, so we just pass driver-unique message to the "no info"
 * interface.
 */
static void
ppc4xx_edac_handle_ce(struct mem_ctl_info *mci,
		      const struct ppc4xx_ecc_status *status)
{
	int row;
	char message[PPC4XX_EDAC_MESSAGE_SIZE];

	ppc4xx_edac_generate_message(mci, status, message, sizeof(message));

	for (row = 0; row < mci->nr_csrows; row++)
		if (ppc4xx_edac_check_bank_error(status, row))
			edac_mc_handle_ce_no_info(mci, message);
}

/**
 * ppc4xx_edac_handle_ue - handle controller uncorrectable ECC error (UE)
 * @mci: A pointer to the EDAC memory controller instance
 *       associated with the uncorrectable error being handled and
 *       reported.
 * @status: A pointer to the ECC status structure associated with
 *          the uncorrectable error being handled and reported.
 *
 * This routine handles an ibm,sdram-4xx-ddr2 controller ECC
 * uncorrectable error.
 */
static void
ppc4xx_edac_handle_ue(struct mem_ctl_info *mci,
		      const struct ppc4xx_ecc_status *status)
{
	const u64 bear = ((u64)status->bearh << 32 | status->bearl);
	const unsigned long page = bear >> PAGE_SHIFT;
	const unsigned long offset = bear & ~PAGE_MASK;
	int row;
	char message[PPC4XX_EDAC_MESSAGE_SIZE];

	ppc4xx_edac_generate_message(mci, status, message, sizeof(message));

	for (row = 0; row < mci->nr_csrows; row++)
		if (ppc4xx_edac_check_bank_error(status, row))
			edac_mc_handle_ue(mci, page, offset, row, message);
}

/**
 * ppc4xx_edac_check - check controller for ECC errors
 * @mci: A pointer to the EDAC memory controller instance
 *       associated with the ibm,sdram-4xx-ddr2 controller being
 *       checked.
 *
 * This routine is used to check and post ECC errors and is called by
 * both the EDAC polling thread and this driver's CE and UE interrupt
 * handler.
 */
static void
ppc4xx_edac_check(struct mem_ctl_info *mci)
{
#ifdef DEBUG
	static unsigned int count;
#endif
	struct ppc4xx_ecc_status status;

	ppc4xx_ecc_get_status(mci, &status);

#ifdef DEBUG
	if (count++ % 30 == 0)
		ppc4xx_ecc_dump_status(mci, &status);
#endif

	if (status.ecces & SDRAM_ECCES_UE)
		ppc4xx_edac_handle_ue(mci, &status);

	if (status.ecces & SDRAM_ECCES_CE)
		ppc4xx_edac_handle_ce(mci, &status);

	ppc4xx_ecc_clear_status(mci, &status);
}

/**
 * ppc4xx_edac_isr - SEC (CE) and DED (UE) interrupt service routine
 * @irq:    The virtual interrupt number being serviced.
 * @dev_id: A pointer to the EDAC memory controller instance
 *          associated with the interrupt being handled.
 *
 * This routine implements the interrupt handler for both correctable
 * (CE) and uncorrectable (UE) ECC errors for the ibm,sdram-4xx-ddr2
 * controller. It simply calls through to the same routine used during
 * polling to check, report and clear the ECC status.
 *
 * Unconditionally returns IRQ_HANDLED.
 */
static irqreturn_t
ppc4xx_edac_isr(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;

	ppc4xx_edac_check(mci);

	return IRQ_HANDLED;
}

/**
 * ppc4xx_edac_get_dtype - return the controller memory width
 * @mcopt1: The 32-bit Memory Controller Option 1 register value
 *          currently set for the controller, from which the width
 *          is derived.
 *
 * This routine returns the EDAC device type width appropriate for the
 * current controller configuration.
 *
 * TODO: This needs to be conditioned dynamically through feature
 * flags or some such when other controller variants are supported as
 * the 405EX[r] is 16-/32-bit and the others are 32-/64-bit with the
 * 16- and 64-bit field definition/value/enumeration (b1) overloaded
 * among them.
 *
 * Returns a device type width enumeration.
 */
static enum dev_type __devinit
ppc4xx_edac_get_dtype(u32 mcopt1)
{
	switch (mcopt1 & SDRAM_MCOPT1_WDTH_MASK) {
	case SDRAM_MCOPT1_WDTH_16:
		return DEV_X2;
	case SDRAM_MCOPT1_WDTH_32:
		return DEV_X4;
	default:
		return DEV_UNKNOWN;
	}
}

/**
 * ppc4xx_edac_get_mtype - return controller memory type
 * @mcopt1: The 32-bit Memory Controller Option 1 register value
 *          currently set for the controller, from which the memory type
 *          is derived.
 *
 * This routine returns the EDAC memory type appropriate for the
 * current controller configuration.
 *
 * Returns a memory type enumeration.
 */
static enum mem_type __devinit
ppc4xx_edac_get_mtype(u32 mcopt1)
{
	bool rden = ((mcopt1 & SDRAM_MCOPT1_RDEN_MASK) == SDRAM_MCOPT1_RDEN);

	switch (mcopt1 & SDRAM_MCOPT1_DDR_TYPE_MASK) {
	case SDRAM_MCOPT1_DDR2_TYPE:
		return rden ? MEM_RDDR2 : MEM_DDR2;
	case SDRAM_MCOPT1_DDR1_TYPE:
		return rden ? MEM_RDDR : MEM_DDR;
	default:
		return MEM_UNKNOWN;
	}
}

/**
 * ppc4xx_edac_init_csrows - initialize driver instance rows
 * @mci: A pointer to the EDAC memory controller instance
 *       associated with the ibm,sdram-4xx-ddr2 controller for which
 *       the csrows (i.e. banks/ranks) are being initialized.
 * @mcopt1: The 32-bit Memory Controller Option 1 register value
 *          currently set for the controller, from which bank width
 *          and memory typ information is derived.
 *
 * This routine initializes the virtual "chip select rows" associated
 * with the EDAC memory controller instance. An ibm,sdram-4xx-ddr2
 * controller bank/rank is mapped to a row.
 *
 * Returns 0 if OK; otherwise, -EINVAL if the memory bank size
 * configuration cannot be determined.
 */
static int __devinit
ppc4xx_edac_init_csrows(struct mem_ctl_info *mci, u32 mcopt1)
{
	const struct ppc4xx_edac_pdata *pdata = mci->pvt_info;
	int status = 0;
	enum mem_type mtype;
	enum dev_type dtype;
	enum edac_type edac_mode;
	int row;
	u32 mbxcf, size;
	static u32 ppc4xx_last_page;

	/* Establish the memory type and width */

	mtype = ppc4xx_edac_get_mtype(mcopt1);
	dtype = ppc4xx_edac_get_dtype(mcopt1);

	/* Establish EDAC mode */

	if (mci->edac_cap & EDAC_FLAG_SECDED)
		edac_mode = EDAC_SECDED;
	else if (mci->edac_cap & EDAC_FLAG_EC)
		edac_mode = EDAC_EC;
	else
		edac_mode = EDAC_NONE;

	/*
	 * Initialize each chip select row structure which correspond
	 * 1:1 with a controller bank/rank.
	 */

	for (row = 0; row < mci->nr_csrows; row++) {
		struct csrow_info *csi = &mci->csrows[row];

		/*
		 * Get the configuration settings for this
		 * row/bank/rank and skip disabled banks.
		 */

		mbxcf = mfsdram(&pdata->dcr_host, SDRAM_MBXCF(row));

		if ((mbxcf & SDRAM_MBCF_BE_MASK) != SDRAM_MBCF_BE_ENABLE)
			continue;

		/* Map the bank configuration size setting to pages. */

		size = mbxcf & SDRAM_MBCF_SZ_MASK;

		switch (size) {
		case SDRAM_MBCF_SZ_4MB:
		case SDRAM_MBCF_SZ_8MB:
		case SDRAM_MBCF_SZ_16MB:
		case SDRAM_MBCF_SZ_32MB:
		case SDRAM_MBCF_SZ_64MB:
		case SDRAM_MBCF_SZ_128MB:
		case SDRAM_MBCF_SZ_256MB:
		case SDRAM_MBCF_SZ_512MB:
		case SDRAM_MBCF_SZ_1GB:
		case SDRAM_MBCF_SZ_2GB:
		case SDRAM_MBCF_SZ_4GB:
		case SDRAM_MBCF_SZ_8GB:
			csi->nr_pages = SDRAM_MBCF_SZ_TO_PAGES(size);
			break;
		default:
			ppc4xx_edac_mc_printk(KERN_ERR, mci,
					      "Unrecognized memory bank %d "
					      "size 0x%08x\n",
					      row, SDRAM_MBCF_SZ_DECODE(size));
			status = -EINVAL;
			goto done;
		}

		csi->first_page = ppc4xx_last_page;
		csi->last_page	= csi->first_page + csi->nr_pages - 1;
		csi->page_mask	= 0;

		/*
		 * It's unclear exactly what grain should be set to
		 * here. The SDRAM_ECCES register allows resolution of
		 * an error down to a nibble which would potentially
		 * argue for a grain of '1' byte, even though we only
		 * know the associated address for uncorrectable
		 * errors. This value is not used at present for
		 * anything other than error reporting so getting it
		 * wrong should be of little consequence. Other
		 * possible values would be the PLB width (16), the
		 * page size (PAGE_SIZE) or the memory width (2 or 4).
		 */

		csi->grain	= 1;

		csi->mtype	= mtype;
		csi->dtype	= dtype;

		csi->edac_mode	= edac_mode;

		ppc4xx_last_page += csi->nr_pages;
	}

 done:
	return status;
}

/**
 * ppc4xx_edac_mc_init - initialize driver instance
 * @mci: A pointer to the EDAC memory controller instance being
 *       initialized.
 * @op: A pointer to the OpenFirmware device tree node associated
 *      with the controller this EDAC instance is bound to.
 * @dcr_host: A pointer to the DCR data containing the DCR mapping
 *            for this controller instance.
 * @mcopt1: The 32-bit Memory Controller Option 1 register value
 *          currently set for the controller, from which ECC capabilities
 *          and scrub mode are derived.
 *
 * This routine performs initialization of the EDAC memory controller
 * instance and related driver-private data associated with the
 * ibm,sdram-4xx-ddr2 memory controller the instance is bound to.
 *
 * Returns 0 if OK; otherwise, < 0 on error.
 */
static int __devinit
ppc4xx_edac_mc_init(struct mem_ctl_info *mci,
		    struct platform_device *op,
		    const dcr_host_t *dcr_host,
		    u32 mcopt1)
{
	int status = 0;
	const u32 memcheck = (mcopt1 & SDRAM_MCOPT1_MCHK_MASK);
	struct ppc4xx_edac_pdata *pdata = NULL;
	const struct device_node *np = op->dev.of_node;

	if (of_match_device(ppc4xx_edac_match, &op->dev) == NULL)
		return -EINVAL;

	/* Initial driver pointers and private data */

	mci->dev		= &op->dev;

	dev_set_drvdata(mci->dev, mci);

	pdata			= mci->pvt_info;

	pdata->dcr_host		= *dcr_host;
	pdata->irqs.sec		= NO_IRQ;
	pdata->irqs.ded		= NO_IRQ;

	/* Initialize controller capabilities and configuration */

	mci->mtype_cap		= (MEM_FLAG_DDR | MEM_FLAG_RDDR |
				   MEM_FLAG_DDR2 | MEM_FLAG_RDDR2);

	mci->edac_ctl_cap	= (EDAC_FLAG_NONE |
				   EDAC_FLAG_EC |
				   EDAC_FLAG_SECDED);

	mci->scrub_cap		= SCRUB_NONE;
	mci->scrub_mode		= SCRUB_NONE;

	/*
	 * Update the actual capabilites based on the MCOPT1[MCHK]
	 * settings. Scrubbing is only useful if reporting is enabled.
	 */

	switch (memcheck) {
	case SDRAM_MCOPT1_MCHK_CHK:
		mci->edac_cap	= EDAC_FLAG_EC;
		break;
	case SDRAM_MCOPT1_MCHK_CHK_REP:
		mci->edac_cap	= (EDAC_FLAG_EC | EDAC_FLAG_SECDED);
		mci->scrub_mode	= SCRUB_SW_SRC;
		break;
	default:
		mci->edac_cap	= EDAC_FLAG_NONE;
		break;
	}

	/* Initialize strings */

	mci->mod_name		= PPC4XX_EDAC_MODULE_NAME;
	mci->mod_ver		= PPC4XX_EDAC_MODULE_REVISION;
	mci->ctl_name		= match->compatible,
	mci->dev_name		= np->full_name;

	/* Initialize callbacks */

	mci->edac_check		= ppc4xx_edac_check;
	mci->ctl_page_to_phys	= NULL;

	/* Initialize chip select rows */

	status = ppc4xx_edac_init_csrows(mci, mcopt1);

	if (status)
		ppc4xx_edac_mc_printk(KERN_ERR, mci,
				      "Failed to initialize rows!\n");

	return status;
}

/**
 * ppc4xx_edac_register_irq - setup and register controller interrupts
 * @op: A pointer to the OpenFirmware device tree node associated
 *      with the controller this EDAC instance is bound to.
 * @mci: A pointer to the EDAC memory controller instance
 *       associated with the ibm,sdram-4xx-ddr2 controller for which
 *       interrupts are being registered.
 *
 * This routine parses the correctable (CE) and uncorrectable error (UE)
 * interrupts from the device tree node and maps and assigns them to
 * the associated EDAC memory controller instance.
 *
 * Returns 0 if OK; otherwise, -ENODEV if the interrupts could not be
 * mapped and assigned.
 */
static int __devinit
ppc4xx_edac_register_irq(struct platform_device *op, struct mem_ctl_info *mci)
{
	int status = 0;
	int ded_irq, sec_irq;
	struct ppc4xx_edac_pdata *pdata = mci->pvt_info;
	struct device_node *np = op->dev.of_node;

	ded_irq = irq_of_parse_and_map(np, INTMAP_ECCDED_INDEX);
	sec_irq = irq_of_parse_and_map(np, INTMAP_ECCSEC_INDEX);

	if (ded_irq == NO_IRQ || sec_irq == NO_IRQ) {
		ppc4xx_edac_mc_printk(KERN_ERR, mci,
				      "Unable to map interrupts.\n");
		status = -ENODEV;
		goto fail;
	}

	status = request_irq(ded_irq,
			     ppc4xx_edac_isr,
			     IRQF_DISABLED,
			     "[EDAC] MC ECCDED",
			     mci);

	if (status < 0) {
		ppc4xx_edac_mc_printk(KERN_ERR, mci,
				      "Unable to request irq %d for ECC DED",
				      ded_irq);
		status = -ENODEV;
		goto fail1;
	}

	status = request_irq(sec_irq,
			     ppc4xx_edac_isr,
			     IRQF_DISABLED,
			     "[EDAC] MC ECCSEC",
			     mci);

	if (status < 0) {
		ppc4xx_edac_mc_printk(KERN_ERR, mci,
				      "Unable to request irq %d for ECC SEC",
				      sec_irq);
		status = -ENODEV;
		goto fail2;
	}

	ppc4xx_edac_mc_printk(KERN_INFO, mci, "ECCDED irq is %d\n", ded_irq);
	ppc4xx_edac_mc_printk(KERN_INFO, mci, "ECCSEC irq is %d\n", sec_irq);

	pdata->irqs.ded = ded_irq;
	pdata->irqs.sec = sec_irq;

	return 0;

 fail2:
	free_irq(sec_irq, mci);

 fail1:
	free_irq(ded_irq, mci);

 fail:
	return status;
}

/**
 * ppc4xx_edac_map_dcrs - locate and map controller registers
 * @np: A pointer to the device tree node containing the DCR
 *      resources to map.
 * @dcr_host: A pointer to the DCR data to populate with the
 *            DCR mapping.
 *
 * This routine attempts to locate in the device tree and map the DCR
 * register resources associated with the controller's indirect DCR
 * address and data windows.
 *
 * Returns 0 if the DCRs were successfully mapped; otherwise, < 0 on
 * error.
 */
static int __devinit
ppc4xx_edac_map_dcrs(const struct device_node *np, dcr_host_t *dcr_host)
{
	unsigned int dcr_base, dcr_len;

	if (np == NULL || dcr_host == NULL)
		return -EINVAL;

	/* Get the DCR resource extent and sanity check the values. */

	dcr_base = dcr_resource_start(np, 0);
	dcr_len = dcr_resource_len(np, 0);

	if (dcr_base == 0 || dcr_len == 0) {
		ppc4xx_edac_printk(KERN_ERR,
				   "Failed to obtain DCR property.\n");
		return -ENODEV;
	}

	if (dcr_len != SDRAM_DCR_RESOURCE_LEN) {
		ppc4xx_edac_printk(KERN_ERR,
				   "Unexpected DCR length %d, expected %d.\n",
				   dcr_len, SDRAM_DCR_RESOURCE_LEN);
		return -ENODEV;
	}

	/*  Attempt to map the DCR extent. */

	*dcr_host = dcr_map(np, dcr_base, dcr_len);

	if (!DCR_MAP_OK(*dcr_host)) {
		ppc4xx_edac_printk(KERN_INFO, "Failed to map DCRs.\n");
		    return -ENODEV;
	}

	return 0;
}

/**
 * ppc4xx_edac_probe - check controller and bind driver
 * @op: A pointer to the OpenFirmware device tree node associated
 *      with the controller being probed for driver binding.
 *
 * This routine probes a specific ibm,sdram-4xx-ddr2 controller
 * instance for binding with the driver.
 *
 * Returns 0 if the controller instance was successfully bound to the
 * driver; otherwise, < 0 on error.
 */
static int __devinit ppc4xx_edac_probe(struct platform_device *op)
{
	int status = 0;
	u32 mcopt1, memcheck;
	dcr_host_t dcr_host;
	const struct device_node *np = op->dev.of_node;
	struct mem_ctl_info *mci = NULL;
	static int ppc4xx_edac_instance;

	/*
	 * At this point, we only support the controller realized on
	 * the AMCC PPC 405EX[r]. Reject anything else.
	 */

	if (!of_device_is_compatible(np, "ibm,sdram-405ex") &&
	    !of_device_is_compatible(np, "ibm,sdram-405exr")) {
		ppc4xx_edac_printk(KERN_NOTICE,
				   "Only the PPC405EX[r] is supported.\n");
		return -ENODEV;
	}

	/*
	 * Next, get the DCR property and attempt to map it so that we
	 * can probe the controller.
	 */

	status = ppc4xx_edac_map_dcrs(np, &dcr_host);

	if (status)
		return status;

	/*
	 * First determine whether ECC is enabled at all. If not,
	 * there is no useful checking or monitoring that can be done
	 * for this controller.
	 */

	mcopt1 = mfsdram(&dcr_host, SDRAM_MCOPT1);
	memcheck = (mcopt1 & SDRAM_MCOPT1_MCHK_MASK);

	if (memcheck == SDRAM_MCOPT1_MCHK_NON) {
		ppc4xx_edac_printk(KERN_INFO, "%s: No ECC memory detected or "
				   "ECC is disabled.\n", np->full_name);
		status = -ENODEV;
		goto done;
	}

	/*
	 * At this point, we know ECC is enabled, allocate an EDAC
	 * controller instance and perform the appropriate
	 * initialization.
	 */

	mci = edac_mc_alloc(sizeof(struct ppc4xx_edac_pdata),
			    ppc4xx_edac_nr_csrows,
			    ppc4xx_edac_nr_chans,
			    ppc4xx_edac_instance);

	if (mci == NULL) {
		ppc4xx_edac_printk(KERN_ERR, "%s: "
				   "Failed to allocate EDAC MC instance!\n",
				   np->full_name);
		status = -ENOMEM;
		goto done;
	}

	status = ppc4xx_edac_mc_init(mci, op, &dcr_host, mcopt1);

	if (status) {
		ppc4xx_edac_mc_printk(KERN_ERR, mci,
				      "Failed to initialize instance!\n");
		goto fail;
	}

	/*
	 * We have a valid, initialized EDAC instance bound to the
	 * controller. Attempt to register it with the EDAC subsystem
	 * and, if necessary, register interrupts.
	 */

	if (edac_mc_add_mc(mci)) {
		ppc4xx_edac_mc_printk(KERN_ERR, mci,
				      "Failed to add instance!\n");
		status = -ENODEV;
		goto fail;
	}

	if (edac_op_state == EDAC_OPSTATE_INT) {
		status = ppc4xx_edac_register_irq(op, mci);

		if (status)
			goto fail1;
	}

	ppc4xx_edac_instance++;

	return 0;

 fail1:
	edac_mc_del_mc(mci->dev);

 fail:
	edac_mc_free(mci);

 done:
	return status;
}

/**
 * ppc4xx_edac_remove - unbind driver from controller
 * @op: A pointer to the OpenFirmware device tree node associated
 *      with the controller this EDAC instance is to be unbound/removed
 *      from.
 *
 * This routine unbinds the EDAC memory controller instance associated
 * with the specified ibm,sdram-4xx-ddr2 controller described by the
 * OpenFirmware device tree node passed as a parameter.
 *
 * Unconditionally returns 0.
 */
static int
ppc4xx_edac_remove(struct platform_device *op)
{
	struct mem_ctl_info *mci = dev_get_drvdata(&op->dev);
	struct ppc4xx_edac_pdata *pdata = mci->pvt_info;

	if (edac_op_state == EDAC_OPSTATE_INT) {
		free_irq(pdata->irqs.sec, mci);
		free_irq(pdata->irqs.ded, mci);
	}

	dcr_unmap(pdata->dcr_host, SDRAM_DCR_RESOURCE_LEN);

	edac_mc_del_mc(mci->dev);
	edac_mc_free(mci);

	return 0;
}

/**
 * ppc4xx_edac_opstate_init - initialize EDAC reporting method
 *
 * This routine ensures that the EDAC memory controller reporting
 * method is mapped to a sane value as the EDAC core defines the value
 * to EDAC_OPSTATE_INVAL by default. We don't call the global
 * opstate_init as that defaults to polling and we want interrupt as
 * the default.
 */
static inline void __init
ppc4xx_edac_opstate_init(void)
{
	switch (edac_op_state) {
	case EDAC_OPSTATE_POLL:
	case EDAC_OPSTATE_INT:
		break;
	default:
		edac_op_state = EDAC_OPSTATE_INT;
		break;
	}

	ppc4xx_edac_printk(KERN_INFO, "Reporting type: %s\n",
			   ((edac_op_state == EDAC_OPSTATE_POLL) ?
			    EDAC_OPSTATE_POLL_STR :
			    ((edac_op_state == EDAC_OPSTATE_INT) ?
			     EDAC_OPSTATE_INT_STR :
			     EDAC_OPSTATE_UNKNOWN_STR)));
}

/**
 * ppc4xx_edac_init - driver/module insertion entry point
 *
 * This routine is the driver/module insertion entry point. It
 * initializes the EDAC memory controller reporting state and
 * registers the driver as an OpenFirmware device tree platform
 * driver.
 */
static int __init
ppc4xx_edac_init(void)
{
	ppc4xx_edac_printk(KERN_INFO, PPC4XX_EDAC_MODULE_REVISION "\n");

	ppc4xx_edac_opstate_init();

	return platform_driver_register(&ppc4xx_edac_driver);
}

/**
 * ppc4xx_edac_exit - driver/module removal entry point
 *
 * This routine is the driver/module removal entry point. It
 * unregisters the driver as an OpenFirmware device tree platform
 * driver.
 */
static void __exit
ppc4xx_edac_exit(void)
{
	platform_driver_unregister(&ppc4xx_edac_driver);
}

module_init(ppc4xx_edac_init);
module_exit(ppc4xx_edac_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Grant Erickson <gerickson@nuovations.com>");
MODULE_DESCRIPTION("EDAC MC Driver for the PPC4xx IBM DDR2 Memory Controller");
module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting State: "
		 "0=" EDAC_OPSTATE_POLL_STR ", 2=" EDAC_OPSTATE_INT_STR);
