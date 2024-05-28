// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2023-2024 Intel Corporation
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#define pr_fmt(fmt) "ACPI: NHLT: " fmt

#include <linux/acpi.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/minmax.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <acpi/nhlt.h>

static struct acpi_table_nhlt *acpi_gbl_nhlt;

static struct acpi_table_nhlt empty_nhlt = {
	.header = {
		.signature = ACPI_SIG_NHLT,
	},
};

/**
 * acpi_nhlt_get_gbl_table - Retrieve a pointer to the first NHLT table.
 *
 * If there is no NHLT in the system, acpi_gbl_nhlt will instead point to an
 * empty table.
 *
 * Return: ACPI status code of the operation.
 */
acpi_status acpi_nhlt_get_gbl_table(void)
{
	acpi_status status;

	status = acpi_get_table(ACPI_SIG_NHLT, 0, (struct acpi_table_header **)(&acpi_gbl_nhlt));
	if (!acpi_gbl_nhlt)
		acpi_gbl_nhlt = &empty_nhlt;
	return status;
}
EXPORT_SYMBOL_GPL(acpi_nhlt_get_gbl_table);

/**
 * acpi_nhlt_put_gbl_table - Release the global NHLT table.
 */
void acpi_nhlt_put_gbl_table(void)
{
	acpi_put_table((struct acpi_table_header *)acpi_gbl_nhlt);
}
EXPORT_SYMBOL_GPL(acpi_nhlt_put_gbl_table);

/**
 * acpi_nhlt_endpoint_match - Verify if an endpoint matches criteria.
 * @ep:			the endpoint to check.
 * @link_type:		the hardware link type, e.g.: PDM or SSP.
 * @dev_type:		the device type.
 * @dir:		stream direction.
 * @bus_id:		the ID of virtual bus hosting the endpoint.
 *
 * Either of @link_type, @dev_type, @dir or @bus_id may be set to a negative
 * value to ignore the parameter when matching.
 *
 * Return: %true if endpoint matches specified criteria or %false otherwise.
 */
bool acpi_nhlt_endpoint_match(const struct acpi_nhlt_endpoint *ep,
			      int link_type, int dev_type, int dir, int bus_id)
{
	return ep &&
	       (link_type < 0 || ep->link_type == link_type) &&
	       (dev_type < 0 || ep->device_type == dev_type) &&
	       (bus_id < 0 || ep->virtual_bus_id == bus_id) &&
	       (dir < 0 || ep->direction == dir);
}
EXPORT_SYMBOL_GPL(acpi_nhlt_endpoint_match);

/**
 * acpi_nhlt_tb_find_endpoint - Search a NHLT table for an endpoint.
 * @tb:			the table to search.
 * @link_type:		the hardware link type, e.g.: PDM or SSP.
 * @dev_type:		the device type.
 * @dir:		stream direction.
 * @bus_id:		the ID of virtual bus hosting the endpoint.
 *
 * Either of @link_type, @dev_type, @dir or @bus_id may be set to a negative
 * value to ignore the parameter during the search.
 *
 * Return: A pointer to endpoint matching the criteria, %NULL if not found or
 * an ERR_PTR() otherwise.
 */
struct acpi_nhlt_endpoint *
acpi_nhlt_tb_find_endpoint(const struct acpi_table_nhlt *tb,
			   int link_type, int dev_type, int dir, int bus_id)
{
	struct acpi_nhlt_endpoint *ep;

	for_each_nhlt_endpoint(tb, ep)
		if (acpi_nhlt_endpoint_match(ep, link_type, dev_type, dir, bus_id))
			return ep;
	return NULL;
}
EXPORT_SYMBOL_GPL(acpi_nhlt_tb_find_endpoint);

/**
 * acpi_nhlt_find_endpoint - Search all NHLT tables for an endpoint.
 * @link_type:		the hardware link type, e.g.: PDM or SSP.
 * @dev_type:		the device type.
 * @dir:		stream direction.
 * @bus_id:		the ID of virtual bus hosting the endpoint.
 *
 * Either of @link_type, @dev_type, @dir or @bus_id may be set to a negative
 * value to ignore the parameter during the search.
 *
 * Return: A pointer to endpoint matching the criteria, %NULL if not found or
 * an ERR_PTR() otherwise.
 */
struct acpi_nhlt_endpoint *
acpi_nhlt_find_endpoint(int link_type, int dev_type, int dir, int bus_id)
{
	/* TODO: Currently limited to table of index 0. */
	return acpi_nhlt_tb_find_endpoint(acpi_gbl_nhlt, link_type, dev_type, dir, bus_id);
}
EXPORT_SYMBOL_GPL(acpi_nhlt_find_endpoint);

/**
 * acpi_nhlt_endpoint_find_fmtcfg - Search endpoint's formats configuration space
 *                                  for a specific format.
 * @ep:			the endpoint to search.
 * @ch:			number of channels.
 * @rate:		samples per second.
 * @vbps:		valid bits per sample.
 * @bps:		bits per sample.
 *
 * Return: A pointer to format matching the criteria, %NULL if not found or
 * an ERR_PTR() otherwise.
 */
struct acpi_nhlt_format_config *
acpi_nhlt_endpoint_find_fmtcfg(const struct acpi_nhlt_endpoint *ep,
			       u16 ch, u32 rate, u16 vbps, u16 bps)
{
	struct acpi_nhlt_wave_formatext *wav;
	struct acpi_nhlt_format_config *fmt;

	for_each_nhlt_endpoint_fmtcfg(ep, fmt) {
		wav = &fmt->format;

		if (wav->valid_bits_per_sample == vbps &&
		    wav->samples_per_sec == rate &&
		    wav->bits_per_sample == bps &&
		    wav->channel_count == ch)
			return fmt;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(acpi_nhlt_endpoint_find_fmtcfg);

/**
 * acpi_nhlt_tb_find_fmtcfg - Search a NHLT table for a specific format.
 * @tb:			the table to search.
 * @link_type:		the hardware link type, e.g.: PDM or SSP.
 * @dev_type:		the device type.
 * @dir:		stream direction.
 * @bus_id:		the ID of virtual bus hosting the endpoint.
 *
 * @ch:			number of channels.
 * @rate:		samples per second.
 * @vbps:		valid bits per sample.
 * @bps:		bits per sample.
 *
 * Either of @link_type, @dev_type, @dir or @bus_id may be set to a negative
 * value to ignore the parameter during the search.
 *
 * Return: A pointer to format matching the criteria, %NULL if not found or
 * an ERR_PTR() otherwise.
 */
struct acpi_nhlt_format_config *
acpi_nhlt_tb_find_fmtcfg(const struct acpi_table_nhlt *tb,
			 int link_type, int dev_type, int dir, int bus_id,
			 u16 ch, u32 rate, u16 vbps, u16 bps)
{
	struct acpi_nhlt_format_config *fmt;
	struct acpi_nhlt_endpoint *ep;

	for_each_nhlt_endpoint(tb, ep) {
		if (!acpi_nhlt_endpoint_match(ep, link_type, dev_type, dir, bus_id))
			continue;

		fmt = acpi_nhlt_endpoint_find_fmtcfg(ep, ch, rate, vbps, bps);
		if (fmt)
			return fmt;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(acpi_nhlt_tb_find_fmtcfg);

/**
 * acpi_nhlt_find_fmtcfg - Search all NHLT tables for a specific format.
 * @link_type:		the hardware link type, e.g.: PDM or SSP.
 * @dev_type:		the device type.
 * @dir:		stream direction.
 * @bus_id:		the ID of virtual bus hosting the endpoint.
 *
 * @ch:			number of channels.
 * @rate:		samples per second.
 * @vbps:		valid bits per sample.
 * @bps:		bits per sample.
 *
 * Either of @link_type, @dev_type, @dir or @bus_id may be set to a negative
 * value to ignore the parameter during the search.
 *
 * Return: A pointer to format matching the criteria, %NULL if not found or
 * an ERR_PTR() otherwise.
 */
struct acpi_nhlt_format_config *
acpi_nhlt_find_fmtcfg(int link_type, int dev_type, int dir, int bus_id,
		      u16 ch, u32 rate, u16 vbps, u16 bps)
{
	/* TODO: Currently limited to table of index 0. */
	return acpi_nhlt_tb_find_fmtcfg(acpi_gbl_nhlt, link_type, dev_type, dir, bus_id,
					ch, rate, vbps, bps);
}
EXPORT_SYMBOL_GPL(acpi_nhlt_find_fmtcfg);

static bool acpi_nhlt_config_is_micdevice(struct acpi_nhlt_config *cfg)
{
	return cfg->capabilities_size >= sizeof(struct acpi_nhlt_micdevice_config);
}

static bool acpi_nhlt_config_is_vendor_micdevice(struct acpi_nhlt_config *cfg)
{
	struct acpi_nhlt_vendor_micdevice_config *devcfg = __acpi_nhlt_config_caps(cfg);

	return cfg->capabilities_size >= sizeof(*devcfg) &&
	       cfg->capabilities_size == struct_size(devcfg, mics, devcfg->mics_count);
}

/**
 * acpi_nhlt_endpoint_mic_count - Retrieve number of digital microphones for a PDM endpoint.
 * @ep:			the endpoint to return microphones count for.
 *
 * Return: A number of microphones or an error code if an invalid endpoint is provided.
 */
int acpi_nhlt_endpoint_mic_count(const struct acpi_nhlt_endpoint *ep)
{
	union acpi_nhlt_device_config *devcfg;
	struct acpi_nhlt_format_config *fmt;
	struct acpi_nhlt_config *cfg;
	u16 max_ch = 0;

	if (!ep || ep->link_type != ACPI_NHLT_LINKTYPE_PDM)
		return -EINVAL;

	/* Find max number of channels based on formats configuration. */
	for_each_nhlt_endpoint_fmtcfg(ep, fmt)
		max_ch = max(fmt->format.channel_count, max_ch);

	cfg = __acpi_nhlt_endpoint_config(ep);
	devcfg = __acpi_nhlt_config_caps(cfg);

	/* If @ep is not a mic array, fallback to channels count. */
	if (!acpi_nhlt_config_is_micdevice(cfg) ||
	    devcfg->gen.config_type != ACPI_NHLT_CONFIGTYPE_MICARRAY)
		return max_ch;

	switch (devcfg->mic.array_type) {
	case ACPI_NHLT_ARRAYTYPE_LINEAR2_SMALL:
	case ACPI_NHLT_ARRAYTYPE_LINEAR2_BIG:
		return 2;

	case ACPI_NHLT_ARRAYTYPE_LINEAR4_GEO1:
	case ACPI_NHLT_ARRAYTYPE_PLANAR4_LSHAPED:
	case ACPI_NHLT_ARRAYTYPE_LINEAR4_GEO2:
		return 4;

	case ACPI_NHLT_ARRAYTYPE_VENDOR:
		if (!acpi_nhlt_config_is_vendor_micdevice(cfg))
			return -EINVAL;
		return devcfg->vendor_mic.mics_count;

	default:
		pr_warn("undefined mic array type: %#x\n", devcfg->mic.array_type);
		return max_ch;
	}
}
EXPORT_SYMBOL_GPL(acpi_nhlt_endpoint_mic_count);
