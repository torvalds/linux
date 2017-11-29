/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017        Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017        Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include "iwl-drv.h"
#include "iwl-debug.h"
#include "acpi.h"

void *iwl_acpi_get_object(struct device *dev, acpi_string method)
{
	acpi_handle root_handle;
	acpi_handle handle;
	struct acpi_buffer buf = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_status status;

	root_handle = ACPI_HANDLE(dev);
	if (!root_handle) {
		IWL_DEBUG_DEV_RADIO(dev,
				    "Could not retrieve root port ACPI handle\n");
		return ERR_PTR(-ENOENT);
	}

	/* Get the method's handle */
	status = acpi_get_handle(root_handle, method, &handle);
	if (ACPI_FAILURE(status)) {
		IWL_DEBUG_DEV_RADIO(dev, "%s method not found\n", method);
		return ERR_PTR(-ENOENT);
	}

	/* Call the method with no arguments */
	status = acpi_evaluate_object(handle, NULL, NULL, &buf);
	if (ACPI_FAILURE(status)) {
		IWL_DEBUG_DEV_RADIO(dev, "%s invocation failed (0x%x)\n",
				    method, status);
		return ERR_PTR(-ENOENT);
	}

	return buf.pointer;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_object);

union acpi_object *iwl_acpi_get_wifi_pkg(struct device *dev,
					 union acpi_object *data,
					 int data_size)
{
	int i;
	union acpi_object *wifi_pkg;

	/*
	 * We need at least one entry in the wifi package that
	 * describes the domain, and one more entry, otherwise there's
	 * no point in reading it.
	 */
	if (WARN_ON_ONCE(data_size < 2))
		return ERR_PTR(-EINVAL);

	/*
	 * We need at least two packages, one for the revision and one
	 * for the data itself.  Also check that the revision is valid
	 * (i.e. it is an integer set to 0).
	 */
	if (data->type != ACPI_TYPE_PACKAGE ||
	    data->package.count < 2 ||
	    data->package.elements[0].type != ACPI_TYPE_INTEGER ||
	    data->package.elements[0].integer.value != 0) {
		IWL_DEBUG_DEV_RADIO(dev, "Unsupported packages structure\n");
		return ERR_PTR(-EINVAL);
	}

	/* loop through all the packages to find the one for WiFi */
	for (i = 1; i < data->package.count; i++) {
		union acpi_object *domain;

		wifi_pkg = &data->package.elements[i];

		/* skip entries that are not a package with the right size */
		if (wifi_pkg->type != ACPI_TYPE_PACKAGE ||
		    wifi_pkg->package.count != data_size)
			continue;

		domain = &wifi_pkg->package.elements[0];
		if (domain->type == ACPI_TYPE_INTEGER &&
		    domain->integer.value == ACPI_WIFI_DOMAIN)
			goto found;
	}

	return ERR_PTR(-ENOENT);

found:
	return wifi_pkg;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_wifi_pkg);

int iwl_acpi_get_mcc(struct device *dev, char *mcc)
{
	union acpi_object *wifi_pkg, *data;
	u32 mcc_val;
	int ret;

	data = iwl_acpi_get_object(dev, ACPI_WRDD_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	wifi_pkg = iwl_acpi_get_wifi_pkg(dev, data, ACPI_WRDD_WIFI_DATA_SIZE);
	if (IS_ERR(wifi_pkg)) {
		ret = PTR_ERR(wifi_pkg);
		goto out_free;
	}

	if (wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER) {
		ret = -EINVAL;
		goto out_free;
	}

	mcc_val = wifi_pkg->package.elements[1].integer.value;

	mcc[0] = (mcc_val >> 8) & 0xff;
	mcc[1] = mcc_val & 0xff;
	mcc[2] = '\0';

	ret = 0;
out_free:
	kfree(data);
	return ret;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_mcc);

u64 iwl_acpi_get_pwr_limit(struct device *dev)
{
	union acpi_object *data, *wifi_pkg;
	u64 dflt_pwr_limit;

	data = iwl_acpi_get_object(dev, ACPI_SPLC_METHOD);
	if (IS_ERR(data)) {
		dflt_pwr_limit = 0;
		goto out;
	}

	wifi_pkg = iwl_acpi_get_wifi_pkg(dev, data,
					 ACPI_SPLC_WIFI_DATA_SIZE);
	if (IS_ERR(wifi_pkg) ||
	    wifi_pkg->package.elements[1].integer.value != ACPI_TYPE_INTEGER) {
		dflt_pwr_limit = 0;
		goto out_free;
	}

	dflt_pwr_limit = wifi_pkg->package.elements[1].integer.value;
out_free:
	kfree(data);
out:
	return dflt_pwr_limit;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_pwr_limit);
