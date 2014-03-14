/*
 * INT3400 thermal driver
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Zhang Rui <rui.zhang@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>

struct art {
	acpi_handle source;
	acpi_handle target;
	u64 weight;
	u64 ac0_max;
	u64 ac1_max;
	u64 ac2_max;
	u64 ac3_max;
	u64 ac4_max;
	u64 ac5_max;
	u64 ac6_max;
	u64 ac7_max;
	u64 ac8_max;
	u64 ac9_max;
};

struct trt {
	acpi_handle source;
	acpi_handle target;
	u64 influence;
	u64 sampling_period;
	u64 reverved1;
	u64 reverved2;
	u64 reverved3;
	u64 reverved4;
};

struct int3400_thermal_priv {
	struct acpi_device *adev;
	int art_count;
	struct art *arts;
	int trt_count;
	struct trt *trts;
};

static int parse_art(struct int3400_thermal_priv *priv)
{
	acpi_handle handle = priv->adev->handle;
	acpi_status status;
	int result = 0;
	int i;
	struct acpi_device *adev;
	union acpi_object *p;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer element = { 0, NULL };
	struct acpi_buffer art_format = {
				sizeof("RRNNNNNNNNNNN"), "RRNNNNNNNNNNN" };

	if (!acpi_has_method(handle, "_ART"))
		return 0;

	status = acpi_evaluate_object(handle, "_ART", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	p = buffer.pointer;
	if (!p || (p->type != ACPI_TYPE_PACKAGE)) {
		pr_err("Invalid _ART data\n");
		result = -EFAULT;
		goto end;
	}

	/* ignore p->package.elements[0], as this is _ART Revision field */
	priv->art_count = p->package.count - 1;
	priv->arts = kzalloc(sizeof(struct art) * priv->art_count, GFP_KERNEL);
	if (!priv->arts) {
		result = -ENOMEM;
		goto end;
	}

	for (i = 0; i < priv->art_count; i++) {
		struct art *art = &(priv->arts[i]);

		element.length = sizeof(struct art);
		element.pointer = art;

		status = acpi_extract_package(&(p->package.elements[i + 1]),
					      &art_format, &element);
		if (ACPI_FAILURE(status)) {
			pr_err("Invalid _ART data");
			result = -EFAULT;
			kfree(priv->arts);
			goto end;
		}
		result = acpi_bus_get_device(art->source, &adev);
		if (!result)
			acpi_create_platform_device(adev, NULL);
		else
			pr_warn("Failed to get source ACPI device\n");
		result = acpi_bus_get_device(art->target, &adev);
		if (!result)
			acpi_create_platform_device(adev, NULL);
		else
			pr_warn("Failed to get source ACPI device\n");
	}
end:
	kfree(buffer.pointer);
	return result;
}

static int parse_trt(struct int3400_thermal_priv *priv)
{
	acpi_handle handle = priv->adev->handle;
	acpi_status status;
	int result = 0;
	int i;
	struct acpi_device *adev;
	union acpi_object *p;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer element = { 0, NULL };
	struct acpi_buffer trt_format = { sizeof("RRNNNNNN"), "RRNNNNNN" };

	if (!acpi_has_method(handle, "_TRT"))
		return 0;

	status = acpi_evaluate_object(handle, "_TRT", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	p = buffer.pointer;
	if (!p || (p->type != ACPI_TYPE_PACKAGE)) {
		pr_err("Invalid _TRT data\n");
		result = -EFAULT;
		goto end;
	}

	priv->trt_count = p->package.count;
	priv->trts = kzalloc(sizeof(struct trt) * priv->trt_count, GFP_KERNEL);
	if (!priv->trts) {
		result = -ENOMEM;
		goto end;
	}

	for (i = 0; i < priv->trt_count; i++) {
		struct trt *trt = &(priv->trts[i]);

		element.length = sizeof(struct trt);
		element.pointer = trt;

		status = acpi_extract_package(&(p->package.elements[i]),
					      &trt_format, &element);
		if (ACPI_FAILURE(status)) {
			pr_err("Invalid _ART data");
			result = -EFAULT;
			kfree(priv->trts);
			goto end;
		}

		result = acpi_bus_get_device(trt->source, &adev);
		if (!result)
			acpi_create_platform_device(adev, NULL);
		else
			pr_warn("Failed to get source ACPI device\n");
		result = acpi_bus_get_device(trt->target, &adev);
		if (!result)
			acpi_create_platform_device(adev, NULL);
		else
			pr_warn("Failed to get target ACPI device\n");
	}
end:
	kfree(buffer.pointer);
	return result;
}

static int int3400_thermal_probe(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	struct int3400_thermal_priv *priv;
	int result;

	if (!adev)
		return -ENODEV;

	priv = kzalloc(sizeof(struct int3400_thermal_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->adev = adev;

	result = parse_art(priv);
	if (result)
		goto free_priv;

	result = parse_trt(priv);
	if (result)
		goto free_art;

	platform_set_drvdata(pdev, priv);

	return 0;
free_art:
	kfree(priv->arts);
free_priv:
	kfree(priv);
	return result;
}

static int int3400_thermal_remove(struct platform_device *pdev)
{
	struct int3400_thermal_priv *priv = platform_get_drvdata(pdev);

	kfree(priv->trts);
	kfree(priv->arts);
	kfree(priv);
	return 0;
}

static const struct acpi_device_id int3400_thermal_match[] = {
	{"INT3400", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, int3400_thermal_match);

static struct platform_driver int3400_thermal_driver = {
	.probe = int3400_thermal_probe,
	.remove = int3400_thermal_remove,
	.driver = {
		   .name = "int3400 thermal",
		   .owner = THIS_MODULE,
		   .acpi_match_table = ACPI_PTR(int3400_thermal_match),
		   },
};

module_platform_driver(int3400_thermal_driver);

MODULE_DESCRIPTION("INT3400 Thermal driver");
MODULE_AUTHOR("Zhang Rui <rui.zhang@intel.com>");
MODULE_LICENSE("GPL");
