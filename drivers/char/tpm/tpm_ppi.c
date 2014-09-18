#include <linux/acpi.h>
#include "tpm.h"

#define TPM_PPI_REVISION_ID	1
#define TPM_PPI_FN_VERSION	1
#define TPM_PPI_FN_SUBREQ	2
#define TPM_PPI_FN_GETREQ	3
#define TPM_PPI_FN_GETACT	4
#define TPM_PPI_FN_GETRSP	5
#define TPM_PPI_FN_SUBREQ2	7
#define TPM_PPI_FN_GETOPR	8
#define PPI_TPM_REQ_MAX		22
#define PPI_VS_REQ_START	128
#define PPI_VS_REQ_END		255
#define PPI_VERSION_LEN		3

static const u8 tpm_ppi_uuid[] = {
	0xA6, 0xFA, 0xDD, 0x3D,
	0x1B, 0x36,
	0xB4, 0x4E,
	0xA4, 0x24,
	0x8D, 0x10, 0x08, 0x9D, 0x16, 0x53
};

static char tpm_ppi_version[PPI_VERSION_LEN + 1];
static acpi_handle tpm_ppi_handle;

static acpi_status ppi_callback(acpi_handle handle, u32 level, void *context,
				void **return_value)
{
	union acpi_object *obj;

	if (!acpi_check_dsm(handle, tpm_ppi_uuid, TPM_PPI_REVISION_ID,
			    1 << TPM_PPI_FN_VERSION))
		return AE_OK;

	/* Cache version string */
	obj = acpi_evaluate_dsm_typed(handle, tpm_ppi_uuid,
				      TPM_PPI_REVISION_ID, TPM_PPI_FN_VERSION,
				      NULL, ACPI_TYPE_STRING);
	if (obj) {
		strlcpy(tpm_ppi_version, obj->string.pointer,
			PPI_VERSION_LEN + 1);
		ACPI_FREE(obj);
	}

	*return_value = handle;

	return AE_CTRL_TERMINATE;
}

static inline union acpi_object *
tpm_eval_dsm(int func, acpi_object_type type, union acpi_object *argv4)
{
	BUG_ON(!tpm_ppi_handle);
	return acpi_evaluate_dsm_typed(tpm_ppi_handle, tpm_ppi_uuid,
				       TPM_PPI_REVISION_ID, func, argv4, type);
}

static ssize_t tpm_show_ppi_version(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", tpm_ppi_version);
}

static ssize_t tpm_show_ppi_request(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	ssize_t size = -EINVAL;
	union acpi_object *obj;

	obj = tpm_eval_dsm(TPM_PPI_FN_GETREQ, ACPI_TYPE_PACKAGE, NULL);
	if (!obj)
		return -ENXIO;

	/*
	 * output.pointer should be of package type, including two integers.
	 * The first is function return code, 0 means success and 1 means
	 * error. The second is pending TPM operation requested by the OS, 0
	 * means none and >0 means operation value.
	 */
	if (obj->package.count == 2 &&
	    obj->package.elements[0].type == ACPI_TYPE_INTEGER &&
	    obj->package.elements[1].type == ACPI_TYPE_INTEGER) {
		if (obj->package.elements[0].integer.value)
			size = -EFAULT;
		else
			size = scnprintf(buf, PAGE_SIZE, "%llu\n",
				 obj->package.elements[1].integer.value);
	}

	ACPI_FREE(obj);

	return size;
}

static ssize_t tpm_store_ppi_request(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	u32 req;
	u64 ret;
	int func = TPM_PPI_FN_SUBREQ;
	union acpi_object *obj, tmp;
	union acpi_object argv4 = ACPI_INIT_DSM_ARGV4(1, &tmp);

	/*
	 * the function to submit TPM operation request to pre-os environment
	 * is updated with function index from SUBREQ to SUBREQ2 since PPI
	 * version 1.1
	 */
	if (acpi_check_dsm(tpm_ppi_handle, tpm_ppi_uuid, TPM_PPI_REVISION_ID,
			   1 << TPM_PPI_FN_SUBREQ2))
		func = TPM_PPI_FN_SUBREQ2;

	/*
	 * PPI spec defines params[3].type as ACPI_TYPE_PACKAGE. Some BIOS
	 * accept buffer/string/integer type, but some BIOS accept buffer/
	 * string/package type. For PPI version 1.0 and 1.1, use buffer type
	 * for compatibility, and use package type since 1.2 according to spec.
	 */
	if (strcmp(tpm_ppi_version, "1.2") < 0) {
		if (sscanf(buf, "%d", &req) != 1)
			return -EINVAL;
		argv4.type = ACPI_TYPE_BUFFER;
		argv4.buffer.length = sizeof(req);
		argv4.buffer.pointer = (u8 *)&req;
	} else {
		tmp.type = ACPI_TYPE_INTEGER;
		if (sscanf(buf, "%llu", &tmp.integer.value) != 1)
			return -EINVAL;
	}

	obj = tpm_eval_dsm(func, ACPI_TYPE_INTEGER, &argv4);
	if (!obj) {
		return -ENXIO;
	} else {
		ret = obj->integer.value;
		ACPI_FREE(obj);
	}

	if (ret == 0)
		return (acpi_status)count;

	return (ret == 1) ? -EPERM : -EFAULT;
}

static ssize_t tpm_show_ppi_transition_action(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	u32 ret;
	acpi_status status;
	union acpi_object *obj = NULL;
	union acpi_object tmp = {
		.buffer.type = ACPI_TYPE_BUFFER,
		.buffer.length = 0,
		.buffer.pointer = NULL
	};

	static char *info[] = {
		"None",
		"Shutdown",
		"Reboot",
		"OS Vendor-specific",
		"Error",
	};

	/*
	 * PPI spec defines params[3].type as empty package, but some platforms
	 * (e.g. Capella with PPI 1.0) need integer/string/buffer type, so for
	 * compatibility, define params[3].type as buffer, if PPI version < 1.2
	 */
	if (strcmp(tpm_ppi_version, "1.2") < 0)
		obj = &tmp;
	obj = tpm_eval_dsm(TPM_PPI_FN_GETACT, ACPI_TYPE_INTEGER, obj);
	if (!obj) {
		return -ENXIO;
	} else {
		ret = obj->integer.value;
		ACPI_FREE(obj);
	}

	if (ret < ARRAY_SIZE(info) - 1)
		status = scnprintf(buf, PAGE_SIZE, "%d: %s\n", ret, info[ret]);
	else
		status = scnprintf(buf, PAGE_SIZE, "%d: %s\n", ret,
				   info[ARRAY_SIZE(info)-1]);
	return status;
}

static ssize_t tpm_show_ppi_response(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	acpi_status status = -EINVAL;
	union acpi_object *obj, *ret_obj;
	u64 req, res;

	obj = tpm_eval_dsm(TPM_PPI_FN_GETRSP, ACPI_TYPE_PACKAGE, NULL);
	if (!obj)
		return -ENXIO;

	/*
	 * parameter output.pointer should be of package type, including
	 * 3 integers. The first means function return code, the second means
	 * most recent TPM operation request, and the last means response to
	 * the most recent TPM operation request. Only if the first is 0, and
	 * the second integer is not 0, the response makes sense.
	 */
	ret_obj = obj->package.elements;
	if (obj->package.count < 3 ||
	    ret_obj[0].type != ACPI_TYPE_INTEGER ||
	    ret_obj[1].type != ACPI_TYPE_INTEGER ||
	    ret_obj[2].type != ACPI_TYPE_INTEGER)
		goto cleanup;

	if (ret_obj[0].integer.value) {
		status = -EFAULT;
		goto cleanup;
	}

	req = ret_obj[1].integer.value;
	res = ret_obj[2].integer.value;
	if (req) {
		if (res == 0)
			status = scnprintf(buf, PAGE_SIZE, "%llu %s\n", req,
					   "0: Success");
		else if (res == 0xFFFFFFF0)
			status = scnprintf(buf, PAGE_SIZE, "%llu %s\n", req,
					   "0xFFFFFFF0: User Abort");
		else if (res == 0xFFFFFFF1)
			status = scnprintf(buf, PAGE_SIZE, "%llu %s\n", req,
					   "0xFFFFFFF1: BIOS Failure");
		else if (res >= 1 && res <= 0x00000FFF)
			status = scnprintf(buf, PAGE_SIZE, "%llu %llu: %s\n",
					   req, res, "Corresponding TPM error");
		else
			status = scnprintf(buf, PAGE_SIZE, "%llu %llu: %s\n",
					   req, res, "Error");
	} else {
		status = scnprintf(buf, PAGE_SIZE, "%llu: %s\n",
				   req, "No Recent Request");
	}

cleanup:
	ACPI_FREE(obj);
	return status;
}

static ssize_t show_ppi_operations(char *buf, u32 start, u32 end)
{
	int i;
	u32 ret;
	char *str = buf;
	union acpi_object *obj, tmp;
	union acpi_object argv = ACPI_INIT_DSM_ARGV4(1, &tmp);

	static char *info[] = {
		"Not implemented",
		"BIOS only",
		"Blocked for OS by BIOS",
		"User required",
		"User not required",
	};

	if (!acpi_check_dsm(tpm_ppi_handle, tpm_ppi_uuid, TPM_PPI_REVISION_ID,
			    1 << TPM_PPI_FN_GETOPR))
		return -EPERM;

	tmp.integer.type = ACPI_TYPE_INTEGER;
	for (i = start; i <= end; i++) {
		tmp.integer.value = i;
		obj = tpm_eval_dsm(TPM_PPI_FN_GETOPR, ACPI_TYPE_INTEGER, &argv);
		if (!obj) {
			return -ENOMEM;
		} else {
			ret = obj->integer.value;
			ACPI_FREE(obj);
		}

		if (ret > 0 && ret < ARRAY_SIZE(info))
			str += scnprintf(str, PAGE_SIZE, "%d %d: %s\n",
					 i, ret, info[ret]);
	}

	return str - buf;
}

static ssize_t tpm_show_ppi_tcg_operations(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	return show_ppi_operations(buf, 0, PPI_TPM_REQ_MAX);
}

static ssize_t tpm_show_ppi_vs_operations(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return show_ppi_operations(buf, PPI_VS_REQ_START, PPI_VS_REQ_END);
}

static DEVICE_ATTR(version, S_IRUGO, tpm_show_ppi_version, NULL);
static DEVICE_ATTR(request, S_IRUGO | S_IWUSR | S_IWGRP,
		   tpm_show_ppi_request, tpm_store_ppi_request);
static DEVICE_ATTR(transition_action, S_IRUGO,
		   tpm_show_ppi_transition_action, NULL);
static DEVICE_ATTR(response, S_IRUGO, tpm_show_ppi_response, NULL);
static DEVICE_ATTR(tcg_operations, S_IRUGO, tpm_show_ppi_tcg_operations, NULL);
static DEVICE_ATTR(vs_operations, S_IRUGO, tpm_show_ppi_vs_operations, NULL);

static struct attribute *ppi_attrs[] = {
	&dev_attr_version.attr,
	&dev_attr_request.attr,
	&dev_attr_transition_action.attr,
	&dev_attr_response.attr,
	&dev_attr_tcg_operations.attr,
	&dev_attr_vs_operations.attr, NULL,
};
static struct attribute_group ppi_attr_grp = {
	.name = "ppi",
	.attrs = ppi_attrs
};

int tpm_add_ppi(struct kobject *parent)
{
	/* Cache TPM ACPI handle and version string */
	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
			    ppi_callback, NULL, NULL, &tpm_ppi_handle);
	return tpm_ppi_handle ? sysfs_create_group(parent, &ppi_attr_grp) : 0;
}

void tpm_remove_ppi(struct kobject *parent)
{
	if (tpm_ppi_handle)
		sysfs_remove_group(parent, &ppi_attr_grp);
}
