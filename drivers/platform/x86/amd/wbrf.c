// SPDX-License-Identifier: GPL-2.0
/*
 * Wifi Frequency Band Manage Interface
 * Copyright (C) 2023 Advanced Micro Devices
 */

#include <linux/acpi.h>
#include <linux/acpi_amd_wbrf.h>

/*
 * Functions bit vector for WBRF method
 *
 * Bit 0: WBRF supported.
 * Bit 1: Function 1 (Add / Remove frequency) is supported.
 * Bit 2: Function 2 (Get frequency list) is supported.
 */
#define WBRF_ENABLED		0x0
#define WBRF_RECORD			0x1
#define WBRF_RETRIEVE		0x2

#define WBRF_REVISION		0x1

/*
 * The data structure used for WBRF_RETRIEVE is not naturally aligned.
 * And unfortunately the design has been settled down.
 */
struct amd_wbrf_ranges_out {
	u32			num_of_ranges;
	struct freq_band_range	band_list[MAX_NUM_OF_WBRF_RANGES];
} __packed;

static const guid_t wifi_acpi_dsm_guid =
	GUID_INIT(0x7b7656cf, 0xdc3d, 0x4c1c,
		  0x83, 0xe9, 0x66, 0xe7, 0x21, 0xde, 0x30, 0x70);

/*
 * Used to notify consumer (amdgpu driver currently) about
 * the wifi frequency is change.
 */
static BLOCKING_NOTIFIER_HEAD(wbrf_chain_head);

static int wbrf_record(struct acpi_device *adev, uint8_t action, struct wbrf_ranges_in_out *in)
{
	union acpi_object argv4;
	union acpi_object *tmp;
	union acpi_object *obj;
	u32 num_of_ranges = 0;
	u32 num_of_elements;
	u32 arg_idx = 0;
	int ret;
	u32 i;

	if (!in)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(in->band_list); i++) {
		if (in->band_list[i].start && in->band_list[i].end)
			num_of_ranges++;
	}

	/*
	 * The num_of_ranges value in the "in" object supplied by
	 * the caller is required to be equal to the number of
	 * entries in the band_list array in there.
	 */
	if (num_of_ranges != in->num_of_ranges)
		return -EINVAL;

	/*
	 * Every input frequency band comes with two end points(start/end)
	 * and each is accounted as an element. Meanwhile the range count
	 * and action type are accounted as an element each.
	 * So, the total element count = 2 * num_of_ranges + 1 + 1.
	 */
	num_of_elements = 2 * num_of_ranges + 2;

	tmp = kcalloc(num_of_elements, sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	argv4.package.type = ACPI_TYPE_PACKAGE;
	argv4.package.count = num_of_elements;
	argv4.package.elements = tmp;

	/* save the number of ranges*/
	tmp[0].integer.type = ACPI_TYPE_INTEGER;
	tmp[0].integer.value = num_of_ranges;

	/* save the action(WBRF_RECORD_ADD/REMOVE/RETRIEVE) */
	tmp[1].integer.type = ACPI_TYPE_INTEGER;
	tmp[1].integer.value = action;

	arg_idx = 2;
	for (i = 0; i < ARRAY_SIZE(in->band_list); i++) {
		if (!in->band_list[i].start || !in->band_list[i].end)
			continue;

		tmp[arg_idx].integer.type = ACPI_TYPE_INTEGER;
		tmp[arg_idx++].integer.value = in->band_list[i].start;
		tmp[arg_idx].integer.type = ACPI_TYPE_INTEGER;
		tmp[arg_idx++].integer.value = in->band_list[i].end;
	}

	obj = acpi_evaluate_dsm(adev->handle, &wifi_acpi_dsm_guid,
				WBRF_REVISION, WBRF_RECORD, &argv4);

	if (!obj)
		return -EINVAL;

	if (obj->type != ACPI_TYPE_INTEGER) {
		ret = -EINVAL;
		goto out;
	}

	ret = obj->integer.value;
	if (ret)
		ret = -EINVAL;

out:
	ACPI_FREE(obj);
	kfree(tmp);

	return ret;
}

/**
 * acpi_amd_wbrf_add_remove - add or remove the frequency band the device is using
 *
 * @dev: device pointer
 * @action: remove or add the frequency band into bios
 * @in: input structure containing the frequency band the device is using
 *
 * Broadcast to other consumers the frequency band the device starts
 * to use. Underneath the surface the information is cached into an
 * internal buffer first. Then a notification is sent to all those
 * registered consumers. So then they can retrieve that buffer to
 * know the latest active frequency bands. Consumers that haven't
 * yet been registered can retrieve the information from the cache
 * when they register.
 *
 * Return:
 * 0 for success add/remove wifi frequency band.
 * Returns a negative error code for failure.
 */
int acpi_amd_wbrf_add_remove(struct device *dev, uint8_t action, struct wbrf_ranges_in_out *in)
{
	struct acpi_device *adev;
	int ret;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENODEV;

	ret = wbrf_record(adev, action, in);
	if (ret)
		return ret;

	blocking_notifier_call_chain(&wbrf_chain_head, WBRF_CHANGED, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_amd_wbrf_add_remove);

/**
 * acpi_amd_wbrf_supported_producer - determine if the WBRF can be enabled
 *                                    for the device as a producer
 *
 * @dev: device pointer
 *
 * Check if the platform equipped with necessary implementations to
 * support WBRF for the device as a producer.
 *
 * Return:
 * true if WBRF is supported, otherwise returns false
 */
bool acpi_amd_wbrf_supported_producer(struct device *dev)
{
	struct acpi_device *adev;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return false;

	return acpi_check_dsm(adev->handle, &wifi_acpi_dsm_guid,
			      WBRF_REVISION, BIT(WBRF_RECORD));
}
EXPORT_SYMBOL_GPL(acpi_amd_wbrf_supported_producer);

/**
 * acpi_amd_wbrf_supported_consumer - determine if the WBRF can be enabled
 *                                    for the device as a consumer
 *
 * @dev: device pointer
 *
 * Determine if the platform equipped with necessary implementations to
 * support WBRF for the device as a consumer.
 *
 * Return:
 * true if WBRF is supported, otherwise returns false.
 */
bool acpi_amd_wbrf_supported_consumer(struct device *dev)
{
	struct acpi_device *adev;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return false;

	return acpi_check_dsm(adev->handle, &wifi_acpi_dsm_guid,
			      WBRF_REVISION, BIT(WBRF_RETRIEVE));
}
EXPORT_SYMBOL_GPL(acpi_amd_wbrf_supported_consumer);

/**
 * amd_wbrf_retrieve_freq_band - retrieve current active frequency bands
 *
 * @dev: device pointer
 * @out: output structure containing all the active frequency bands
 *
 * Retrieve the current active frequency bands which were broadcasted
 * by other producers. The consumer who calls this API should take
 * proper actions if any of the frequency band may cause RFI with its
 * own frequency band used.
 *
 * Return:
 * 0 for getting wifi freq band successfully.
 * Returns a negative error code for failure.
 */
int amd_wbrf_retrieve_freq_band(struct device *dev, struct wbrf_ranges_in_out *out)
{
	struct amd_wbrf_ranges_out acpi_out = {0};
	struct acpi_device *adev;
	union acpi_object *obj;
	union acpi_object param;
	int ret = 0;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENODEV;

	param.type = ACPI_TYPE_STRING;
	param.string.length = 0;
	param.string.pointer = NULL;

	obj = acpi_evaluate_dsm(adev->handle, &wifi_acpi_dsm_guid,
							WBRF_REVISION, WBRF_RETRIEVE, &param);
	if (!obj)
		return -EINVAL;

	/*
	 * The return buffer is with variable length and the format below:
	 * number_of_entries(1 DWORD):       Number of entries
	 * start_freq of 1st entry(1 QWORD): Start frequency of the 1st entry
	 * end_freq of 1st entry(1 QWORD):   End frequency of the 1st entry
	 * ...
	 * ...
	 * start_freq of the last entry(1 QWORD)
	 * end_freq of the last entry(1 QWORD)
	 *
	 * Thus the buffer length is determined by the number of entries.
	 * - For zero entry scenario, the buffer length will be 4 bytes.
	 * - For one entry scenario, the buffer length will be 20 bytes.
	 */
	if (obj->buffer.length > sizeof(acpi_out) || obj->buffer.length < 4) {
		dev_err(dev, "Wrong sized WBRT information");
		ret = -EINVAL;
		goto out;
	}
	memcpy(&acpi_out, obj->buffer.pointer, obj->buffer.length);

	out->num_of_ranges = acpi_out.num_of_ranges;
	memcpy(out->band_list, acpi_out.band_list, sizeof(acpi_out.band_list));

out:
	ACPI_FREE(obj);
	return ret;
}
EXPORT_SYMBOL_GPL(amd_wbrf_retrieve_freq_band);

/**
 * amd_wbrf_register_notifier - register for notifications of frequency
 *                                   band update
 *
 * @nb: driver notifier block
 *
 * The consumer should register itself via this API so that it can get
 * notified on the frequency band updates from other producers.
 *
 * Return:
 * 0 for registering a consumer driver successfully.
 * Returns a negative error code for failure.
 */
int amd_wbrf_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&wbrf_chain_head, nb);
}
EXPORT_SYMBOL_GPL(amd_wbrf_register_notifier);

/**
 * amd_wbrf_unregister_notifier - unregister for notifications of
 *                                     frequency band update
 *
 * @nb: driver notifier block
 *
 * The consumer should call this API when it is longer interested with
 * the frequency band updates from other producers. Usually, this should
 * be performed during driver cleanup.
 *
 * Return:
 * 0 for unregistering a consumer driver.
 * Returns a negative error code for failure.
 */
int amd_wbrf_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&wbrf_chain_head, nb);
}
EXPORT_SYMBOL_GPL(amd_wbrf_unregister_notifier);
