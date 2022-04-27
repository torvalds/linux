/*************************************************************************/ /*!
@File           km_apphint.c
@Title          Apphint routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific functions
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include "di_server.h"
#include "pvr_uaccess.h"
#include <linux/moduleparam.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <stdbool.h>

/* Common and SO layer */
#include "img_defs.h"
#include "sofunc_pvr.h"

/* for action device access */
#include "pvrsrv.h"
#include "device.h"
#include "rgxdevice.h"
#include "rgxfwutils.h"
#include "rgxhwperf.h"
#include "htbserver.h"
#include "rgxutils.h"
#include "rgxapi_km.h"


/* defines for default values */
#include "rgx_fwif_km.h"
#include "htbuffer_types.h"

#include "pvr_notifier.h"

#include "km_apphint_defs.h"
#include "km_apphint.h"

#if defined(PDUMP)
#include <stdarg.h>
#include "pdump_km.h"
#endif

/* Size of temporary buffers used to read and write AppHint data.
 * Must be large enough to contain any strings read or written but no larger
 * than 4096: which is the buffer size for the kernel_param_ops .get
 * function. And less than 1024 to keep the stack frame size within bounds.
 */
#define APPHINT_BUFFER_SIZE 512

#define APPHINT_DEVICES_MAX 16

/*
*******************************************************************************
 * AppHint mnemonic data type helper tables
******************************************************************************/
struct apphint_lookup {
	const char *name;
	int value;
};

static const struct apphint_lookup fwt_logtype_tbl[] = {
	{ "trace", 0},
	{ "none", 0}
#if defined(SUPPORT_TBI_INTERFACE)
	, { "tbi", 1}
#endif
};

static const struct apphint_lookup fwt_loggroup_tbl[] = {
	RGXFWIF_LOG_GROUP_NAME_VALUE_MAP
};

static const struct apphint_lookup htb_loggroup_tbl[] = {
#define X(a, b) { #b, HTB_LOG_GROUP_FLAG(a) },
	HTB_LOG_SFGROUPLIST
#undef X
};

static const struct apphint_lookup htb_opmode_tbl[] = {
	{ "droplatest", HTB_OPMODE_DROPLATEST},
	{ "dropoldest", HTB_OPMODE_DROPOLDEST},
	{ "block", HTB_OPMODE_BLOCK}
};

__maybe_unused
static const struct apphint_lookup htb_logmode_tbl[] = {
	{ "all", HTB_LOGMODE_ALLPID},
	{ "restricted", HTB_LOGMODE_RESTRICTEDPID}
};

__maybe_unused
static const struct apphint_lookup timecorr_clk_tbl[] = {
	{ "mono", 0 },
	{ "mono_raw", 1 },
	{ "sched", 2 }
};

/*
*******************************************************************************
 Data types
******************************************************************************/
union apphint_value {
	IMG_UINT64 UINT64;
	IMG_UINT32 UINT32;
	IMG_BOOL BOOL;
	IMG_CHAR *STRING;
};

union apphint_query_action {
	PVRSRV_ERROR (*UINT64)(const PVRSRV_DEVICE_NODE *device,
	                       const void *private_data, IMG_UINT64 *value);
	PVRSRV_ERROR (*UINT32)(const PVRSRV_DEVICE_NODE *device,
	                       const void *private_data, IMG_UINT32 *value);
	PVRSRV_ERROR (*BOOL)(const PVRSRV_DEVICE_NODE *device,
	                     const void *private_data, IMG_BOOL *value);
	PVRSRV_ERROR (*STRING)(const PVRSRV_DEVICE_NODE *device,
	                       const void *private_data, IMG_CHAR **value);
};

union apphint_set_action {
	PVRSRV_ERROR (*UINT64)(const PVRSRV_DEVICE_NODE *device,
	                       const void *private_data, IMG_UINT64 value);
	PVRSRV_ERROR (*UINT32)(const PVRSRV_DEVICE_NODE *device,
	                       const void *private_data, IMG_UINT32 value);
	PVRSRV_ERROR (*BOOL)(const PVRSRV_DEVICE_NODE *device,
	                     const void *private_data, IMG_BOOL value);
	PVRSRV_ERROR (*STRING)(const PVRSRV_DEVICE_NODE *device,
	                       const void *private_data, IMG_CHAR *value);
};

struct apphint_action {
	union apphint_query_action query; /*!< Query callbacks. */
	union apphint_set_action set;     /*!< Set callbacks. */
	const PVRSRV_DEVICE_NODE *device; /*!< Pointer to the device node.*/
	const void *private_data;         /*!< Opaque data passed to `query` and
	                                       `set` callbacks. */
	union apphint_value stored;       /*!< Value of the AppHint. */
	bool free;                        /*!< Flag indicating that memory has been
	                                       allocated for this AppHint and it
	                                       needs to be freed on deinit. */
	bool initialised;                 /*!< Flag indicating if the AppHint has
	                                       been already initialised. */
};

struct apphint_param {
	IMG_UINT32 id;
	APPHINT_DATA_TYPE data_type;
	const void *data_type_helper;
	IMG_UINT32 helper_size;
};

struct apphint_init_data {
	IMG_UINT32 id;			/* index into AppHint Table */
	APPHINT_CLASS class;
	const IMG_CHAR *name;
	union apphint_value default_value;
};

struct apphint_init_data_mapping {
	IMG_UINT32 device_apphint_id;
	IMG_UINT32 modparam_apphint_id;
};

struct apphint_class_state {
	APPHINT_CLASS class;
	IMG_BOOL enabled;
};

struct apphint_work {
	struct work_struct work;
	union apphint_value new_value;
	struct apphint_action *action;
};

/*
*******************************************************************************
 Initialization / configuration table data
******************************************************************************/
#define UINT32Bitfield UINT32
#define UINT32List UINT32

static const struct apphint_init_data init_data_buildvar[] = {
#define X(a, b, c, d, e) \
	{APPHINT_ID_ ## a, APPHINT_CLASS_ ## c, #a, {.b=d} },
	APPHINT_LIST_BUILDVAR_COMMON
	APPHINT_LIST_BUILDVAR
#undef X
};

static const struct apphint_init_data init_data_modparam[] = {
#define X(a, b, c, d, e) \
	{APPHINT_ID_ ## a, APPHINT_CLASS_ ## c, #a, {.b=d} },
	APPHINT_LIST_MODPARAM_COMMON
	APPHINT_LIST_MODPARAM
#undef X
};

static const struct apphint_init_data init_data_debuginfo[] = {
#define X(a, b, c, d, e) \
	{APPHINT_ID_ ## a, APPHINT_CLASS_ ## c, #a, {.b=d} },
	APPHINT_LIST_DEBUGINFO_COMMON
	APPHINT_LIST_DEBUGINFO
#undef X
};

static const struct apphint_init_data init_data_debuginfo_device[] = {
#define X(a, b, c, d, e) \
	{APPHINT_ID_ ## a, APPHINT_CLASS_ ## c, #a, {.b=d} },
	APPHINT_LIST_DEBUGINFO_DEVICE_COMMON
	APPHINT_LIST_DEBUGINFO_DEVICE
#undef X
};

static const struct apphint_init_data_mapping init_data_debuginfo_device_to_modparams[] = {
#define X(a, b) \
	{APPHINT_ID_ ## a, APPHINT_ID_ ## b},
	APPHINT_LIST_DEBUIGINFO_DEVICE_X_MODPARAM_INIT_COMMON
	APPHINT_LIST_DEBUIGINFO_DEVICE_X_MODPARAM_INIT
#undef X
};

#undef UINT32Bitfield
#undef UINT32List

__maybe_unused static const char NO_PARAM_TABLE[] = {};

static const struct apphint_param param_lookup[] = {
#define X(a, b, c, d, e) \
	{APPHINT_ID_ ## a, APPHINT_DATA_TYPE_ ## b, e, ARRAY_SIZE(e) },
	APPHINT_LIST_ALL
#undef X
};

static const struct apphint_class_state class_state[] = {
#define X(a) {APPHINT_CLASS_ ## a, APPHINT_ENABLED_CLASS_ ## a},
	APPHINT_CLASS_LIST
#undef X
};

/*
*******************************************************************************
 Global state
******************************************************************************/
/* If the union apphint_value becomes such that it is not possible to read
 * and write atomically, a mutex may be desirable to prevent a read returning
 * a partially written state.
 * This would require a statically initialized mutex outside of the
 * struct apphint_state to prevent use of an uninitialized mutex when
 * module_params are provided on the command line.
 *     static DEFINE_MUTEX(apphint_mutex);
 */
static struct apphint_state
{
	struct workqueue_struct *workqueue;
	DI_GROUP *debuginfo_device_rootdir[APPHINT_DEVICES_MAX];
	DI_ENTRY *debuginfo_device_entry[APPHINT_DEVICES_MAX][APPHINT_DEBUGINFO_DEVICE_ID_MAX];
	DI_GROUP *debuginfo_rootdir;
	DI_ENTRY *debuginfo_entry[APPHINT_DEBUGINFO_ID_MAX];
	DI_GROUP *buildvar_rootdir;
	DI_ENTRY *buildvar_entry[APPHINT_BUILDVAR_ID_MAX];

	unsigned num_devices;
	PVRSRV_DEVICE_NODE *devices[APPHINT_DEVICES_MAX];
	unsigned initialized;

	/* Array contains value space for 1 copy of all apphint values defined
	 * (for device 1) and N copies of device specific apphint values for
	 * multi-device platforms.
	 */
	struct apphint_action val[APPHINT_ID_MAX + ((APPHINT_DEVICES_MAX-1)*APPHINT_DEBUGINFO_DEVICE_ID_MAX)];

} apphint = {
/* statically initialise default values to ensure that any module_params
 * provided on the command line are not overwritten by defaults.
 */
	.val = {
#define UINT32Bitfield UINT32
#define UINT32List UINT32
#define X(a, b, c, d, e) \
	{ {NULL}, {NULL}, NULL, NULL, {.b=d}, false },
	APPHINT_LIST_ALL
#undef X
#undef UINT32Bitfield
#undef UINT32List
	},
	.initialized = 0,
	.num_devices = 0
};

#define APPHINT_DEBUGINFO_DEVICE_ID_OFFSET (APPHINT_ID_MAX-APPHINT_DEBUGINFO_DEVICE_ID_MAX)

static inline void
get_apphint_id_from_action_addr(const struct apphint_action * const addr,
                                APPHINT_ID * const id)
{
	*id = (APPHINT_ID)(addr - apphint.val);
	if (*id >= APPHINT_ID_MAX) {
		*id -= APPHINT_DEBUGINFO_DEVICE_ID_OFFSET;
		*id %= APPHINT_DEBUGINFO_DEVICE_ID_MAX;
		*id += APPHINT_DEBUGINFO_DEVICE_ID_OFFSET;
	}
}

static inline void
get_value_offset_from_device(const PVRSRV_DEVICE_NODE * const device,
                             int * const offset)
{
	int i;

	/* No device offset if not a device specific apphint */
	if (APPHINT_OF_DRIVER_NO_DEVICE == device) {
		*offset = 0;
		return;
	}

	for (i = 0; device && i < APPHINT_DEVICES_MAX; i++) {
		if (apphint.devices[i] == device)
			break;
	}
	if (APPHINT_DEVICES_MAX == i) {
		PVR_DPF((PVR_DBG_WARNING, "%s: Unregistered device", __func__));
		i = 0;
	}
	*offset = i * APPHINT_DEBUGINFO_DEVICE_ID_MAX;
}

/**
 * apphint_action_worker - perform an action after an AppHint update has been
 *                    requested by a UM process
 *                    And update the record of the current active value
 */
static void apphint_action_worker(struct work_struct *work)
{
	struct apphint_work *work_pkt = container_of(work,
	                                             struct apphint_work,
	                                             work);
	struct apphint_action *a = work_pkt->action;
	union apphint_value value = work_pkt->new_value;
	APPHINT_ID id;
	PVRSRV_ERROR result = PVRSRV_OK;

	get_apphint_id_from_action_addr(a, &id);

	if (a->set.UINT64) {
		switch (param_lookup[id].data_type) {
		case APPHINT_DATA_TYPE_UINT64:
			result = a->set.UINT64(a->device,
			                       a->private_data,
			                       value.UINT64);
			break;

		case APPHINT_DATA_TYPE_UINT32:
		case APPHINT_DATA_TYPE_UINT32Bitfield:
		case APPHINT_DATA_TYPE_UINT32List:
			result = a->set.UINT32(a->device,
			                       a->private_data,
			                       value.UINT32);
			break;

		case APPHINT_DATA_TYPE_BOOL:
			result = a->set.BOOL(a->device,
			                     a->private_data,
			                     value.BOOL);
			break;

		case APPHINT_DATA_TYPE_STRING:
			result = a->set.STRING(a->device,
								   a->private_data,
								   value.STRING);
			kfree(value.STRING);
			break;

		default:
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: unrecognised data type (%d), index (%d)",
			         __func__, param_lookup[id].data_type, id));
		}

		if (PVRSRV_OK != result) {
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: failed (%s)",
			         __func__, PVRSRVGetErrorString(result)));
		}
	} else {
		if (a->free) {
			kfree(a->stored.STRING);
		}
		a->stored = value;
		if (param_lookup[id].data_type == APPHINT_DATA_TYPE_STRING) {
			a->free = true;
		}
		PVR_DPF((PVR_DBG_MESSAGE,
		         "%s: AppHint value updated before handler is registered, ID(%d)",
		         __func__, id));
	}
	kfree((void *)work_pkt);
}

static void apphint_action(union apphint_value new_value,
                           struct apphint_action *action)
{
	struct apphint_work *work_pkt = kmalloc(sizeof(*work_pkt), GFP_KERNEL);

	/* queue apphint update on a serialized workqueue to avoid races */
	if (work_pkt) {
		work_pkt->new_value = new_value;
		work_pkt->action = action;
		INIT_WORK(&work_pkt->work, apphint_action_worker);
		if (0 == queue_work(apphint.workqueue, &work_pkt->work)) {
			PVR_DPF((PVR_DBG_ERROR,
				"%s: failed to queue apphint change request",
				__func__));
			goto err_exit;
		}
	} else {
		PVR_DPF((PVR_DBG_ERROR,
			"%s: failed to alloc memory for apphint change request",
			__func__));
		goto err_exit;
	}
	return;
err_exit:
	kfree(new_value.STRING);
}

/**
 * apphint_read - read the different AppHint data types
 * return -errno or the buffer size
 */
static int apphint_read(char *buffer, size_t count, APPHINT_ID ue,
			 union apphint_value *value)
{
	APPHINT_DATA_TYPE data_type = param_lookup[ue].data_type;
	int result = 0;

	switch (data_type) {
	case APPHINT_DATA_TYPE_UINT64:
		if (kstrtou64(buffer, 0, &value->UINT64) < 0) {
			PVR_DPF((PVR_DBG_ERROR,
				"%s: Invalid UINT64 input data for id %d: %s",
				__func__, ue, buffer));
			result = -EINVAL;
			goto err_exit;
		}
		break;
	case APPHINT_DATA_TYPE_UINT32:
		if (kstrtou32(buffer, 0, &value->UINT32) < 0) {
			PVR_DPF((PVR_DBG_ERROR,
				"%s: Invalid UINT32 input data for id %d: %s",
				__func__, ue, buffer));
			result = -EINVAL;
			goto err_exit;
		}
		break;
	case APPHINT_DATA_TYPE_BOOL:
		switch (buffer[0]) {
		case '0':
		case 'n':
		case 'N':
		case 'f':
		case 'F':
			value->BOOL = IMG_FALSE;
			break;
		case '1':
		case 'y':
		case 'Y':
		case 't':
		case 'T':
			value->BOOL = IMG_TRUE;
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR,
				"%s: Invalid BOOL input data for id %d: %s",
				__func__, ue, buffer));
			result = -EINVAL;
			goto err_exit;
		}
		break;
	case APPHINT_DATA_TYPE_UINT32List:
	{
		int i;
		struct apphint_lookup *lookup =
			(struct apphint_lookup *)
			param_lookup[ue].data_type_helper;
		int size = param_lookup[ue].helper_size;
		/* buffer may include '\n', remove it */
		char *arg = strsep(&buffer, "\n");

		if (lookup == (struct apphint_lookup *)NO_PARAM_TABLE) {
			result = -EINVAL;
			goto err_exit;
		}

		for (i = 0; i < size; i++) {
			if (strcasecmp(lookup[i].name, arg) == 0) {
				value->UINT32 = lookup[i].value;
				break;
			}
		}
		if (i == size) {
			if (OSStringLength(arg) == 0) {
				PVR_DPF((PVR_DBG_ERROR,
					"%s: No value set for AppHint",
					__func__));
			} else {
				PVR_DPF((PVR_DBG_ERROR,
					"%s: Unrecognised AppHint value (%s)",
					__func__, arg));
			}
			result = -EINVAL;
		}
		break;
	}
	case APPHINT_DATA_TYPE_UINT32Bitfield:
	{
		int i;
		struct apphint_lookup *lookup =
			(struct apphint_lookup *)
			param_lookup[ue].data_type_helper;
		int size = param_lookup[ue].helper_size;
		/* buffer may include '\n', remove it */
		char *string = strsep(&buffer, "\n");
		char *token = strsep(&string, ",");

		if (lookup == (struct apphint_lookup *)NO_PARAM_TABLE) {
			result = -EINVAL;
			goto err_exit;
		}

		value->UINT32 = 0;
		/* empty string is valid to clear the bitfield */
		while (token && *token) {
			for (i = 0; i < size; i++) {
				if (strcasecmp(lookup[i].name, token) == 0) {
					value->UINT32 |= lookup[i].value;
					break;
				}
			}
			if (i == size) {
				PVR_DPF((PVR_DBG_ERROR,
					"%s: Unrecognised AppHint value (%s)",
					__func__, token));
				result = -EINVAL;
				goto err_exit;
			}
			token = strsep(&string, ",");
		}
		break;
	}
	case APPHINT_DATA_TYPE_STRING:
	{
		/* buffer may include '\n', remove it */
		char *string = strsep(&buffer, "\n");
		size_t len = OSStringLength(string);

		if (!len) {
			result = -EINVAL;
			goto err_exit;
		}

		++len;

		value->STRING = kmalloc(len , GFP_KERNEL);
		if (!value->STRING) {
			result = -ENOMEM;
			goto err_exit;
		}

		OSStringLCopy(value->STRING, string, len);
		break;
	}
	default:
		result = -EINVAL;
		goto err_exit;
	}

err_exit:
	return (result < 0) ? result : count;
}

static PVRSRV_ERROR get_apphint_value_from_action(const struct apphint_action * const action,
												  union apphint_value * const value)
{
	APPHINT_ID id;
	APPHINT_DATA_TYPE data_type;
	PVRSRV_ERROR result = PVRSRV_OK;

	get_apphint_id_from_action_addr(action, &id);
	data_type = param_lookup[id].data_type;

	if (action->query.UINT64) {
		switch (data_type) {
		case APPHINT_DATA_TYPE_UINT64:
			result = action->query.UINT64(action->device,
										  action->private_data,
										  &value->UINT64);
			break;

		case APPHINT_DATA_TYPE_UINT32:
		case APPHINT_DATA_TYPE_UINT32Bitfield:
		case APPHINT_DATA_TYPE_UINT32List:
			result = action->query.UINT32(action->device,
										  action->private_data,
										  &value->UINT32);
			break;

		case APPHINT_DATA_TYPE_BOOL:
			result = action->query.BOOL(action->device,
										action->private_data,
										&value->BOOL);
			break;

		case APPHINT_DATA_TYPE_STRING:
			result = action->query.STRING(action->device,
										  action->private_data,
										  &value->STRING);
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: unrecognised data type (%d), index (%d)",
			         __func__, data_type, id));
		}
	} else {
		*value = action->stored;
	}

	if (PVRSRV_OK != result) {
		PVR_DPF((PVR_DBG_ERROR, "%s: failed (%d), index (%d)", __func__, result, id));
	}

	return result;
}

/**
 * apphint_write - write the current AppHint data to a buffer
 *
 * Returns length written or -errno
 */
static int apphint_write(char *buffer, const size_t size,
                         const struct apphint_action *a)
{
	const struct apphint_param *hint;
	int result = 0;
	APPHINT_ID id;
	union apphint_value value;

	get_apphint_id_from_action_addr(a, &id);
	hint = &param_lookup[id];

	result = get_apphint_value_from_action(a, &value);

	switch (hint->data_type) {
	case APPHINT_DATA_TYPE_UINT64:
		result += snprintf(buffer + result, size - result,
				"0x%016llx",
				value.UINT64);
		break;
	case APPHINT_DATA_TYPE_UINT32:
		result += snprintf(buffer + result, size - result,
				"0x%08x",
				value.UINT32);
		break;
	case APPHINT_DATA_TYPE_BOOL:
		result += snprintf(buffer + result, size - result,
			"%s",
			value.BOOL ? "Y" : "N");
		break;
	case APPHINT_DATA_TYPE_STRING:
		if (value.STRING) {
			result += snprintf(buffer + result, size - result,
				"%s",
				*value.STRING ? value.STRING : "(none)");
		} else {
			result += snprintf(buffer + result, size - result,
			"(none)");
		}
		break;
	case APPHINT_DATA_TYPE_UINT32List:
	{
		struct apphint_lookup *lookup =
			(struct apphint_lookup *) hint->data_type_helper;
		IMG_UINT32 i;

		if (lookup == (struct apphint_lookup *)NO_PARAM_TABLE) {
			result = -EINVAL;
			goto err_exit;
		}

		for (i = 0; i < hint->helper_size; i++) {
			if (lookup[i].value == value.UINT32) {
				result += snprintf(buffer + result,
						size - result,
						"%s",
						lookup[i].name);
				break;
			}
		}
		break;
	}
	case APPHINT_DATA_TYPE_UINT32Bitfield:
	{
		struct apphint_lookup *lookup =
			(struct apphint_lookup *) hint->data_type_helper;
		IMG_UINT32 i;

		if (lookup == (struct apphint_lookup *)NO_PARAM_TABLE) {
			result = -EINVAL;
			goto err_exit;
		}

		for (i = 0; i < hint->helper_size; i++) {
			if (lookup[i].value & value.UINT32) {
				result += snprintf(buffer + result,
						size - result,
						"%s,",
						lookup[i].name);
			}
		}
		if (result) {
			/* remove any trailing ',' */
			--result;
			*(buffer + result) = '\0';
		} else {
			result += snprintf(buffer + result,
					size - result, "none");
		}
		break;
	}
	default:
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: unrecognised data type (%d), index (%d)",
			 __func__, hint->data_type, id));
		result = -EINVAL;
	}

err_exit:
	return result;
}

/*
*******************************************************************************
 Module parameters initialization - different from debuginfo
******************************************************************************/
/**
 * apphint_kparam_set - Handle an update of a module parameter
 *
 * Returns 0, or -errno.  arg is in kp->arg.
 */
static int apphint_kparam_set(const char *val, const struct kernel_param *kp)
{
	char val_copy[APPHINT_BUFFER_SIZE];
	APPHINT_ID id;
	union apphint_value value;
	int result;

	/* need to discard const in case of string comparison */
	result = strlcpy(val_copy, val, APPHINT_BUFFER_SIZE);

	get_apphint_id_from_action_addr(kp->arg, &id);
	if (result < APPHINT_BUFFER_SIZE) {
		result = apphint_read(val_copy, result, id, &value);
		if (result >= 0) {
			((struct apphint_action *)kp->arg)->stored = value;
			((struct apphint_action *)kp->arg)->initialised = true;
			if (param_lookup[id].data_type == APPHINT_DATA_TYPE_STRING) {
				((struct apphint_action *)kp->arg)->free = true;
			}
		}
	} else {
		PVR_DPF((PVR_DBG_ERROR, "%s: String too long", __func__));
	}
	return (result > 0) ? 0 : result;
}

/**
 * apphint_kparam_get - handle a read of a module parameter
 *
 * Returns length written or -errno.  Buffer is 4k (ie. be short!)
 */
static int apphint_kparam_get(char *buffer, const struct kernel_param *kp)
{
	return apphint_write(buffer, PAGE_SIZE, kp->arg);
}

__maybe_unused
static const struct kernel_param_ops apphint_kparam_fops = {
	.set = apphint_kparam_set,
	.get = apphint_kparam_get,
};

/*
 * call module_param_cb() for all AppHints listed in APPHINT_LIST_MODPARAM_COMMON + APPHINT_LIST_MODPARAM
 * apphint_modparam_class_ ## resolves to apphint_modparam_enable() except for
 * AppHint classes that have been disabled.
 */

#define apphint_modparam_enable(name, number, perm) \
	module_param_cb(name, &apphint_kparam_fops, &apphint.val[number], perm);

#define X(a, b, c, d, e) \
	apphint_modparam_class_ ##c(a, APPHINT_ID_ ## a, 0444)
	APPHINT_LIST_MODPARAM_COMMON
	APPHINT_LIST_MODPARAM
#undef X

/*
*******************************************************************************
 Debug Info get (seq file) operations - supporting functions
******************************************************************************/
static void *apphint_di_start(OSDI_IMPL_ENTRY *s, IMG_UINT64 *pos)
{
	if (*pos == 0) {
		/* We want only one entry in the sequence, one call to show() */
		return (void *) 1;
	}

	PVR_UNREFERENCED_PARAMETER(s);

	return NULL;
}

static void apphint_di_stop(OSDI_IMPL_ENTRY *s, void *v)
{
	PVR_UNREFERENCED_PARAMETER(s);
	PVR_UNREFERENCED_PARAMETER(v);
}

static void *apphint_di_next(OSDI_IMPL_ENTRY *s, void *v, IMG_UINT64 *pos)
{
	PVR_UNREFERENCED_PARAMETER(s);
	PVR_UNREFERENCED_PARAMETER(v);
	PVR_UNREFERENCED_PARAMETER(pos);
	return NULL;
}

static int apphint_di_show(OSDI_IMPL_ENTRY *s, void *v)
{
	IMG_CHAR km_buffer[APPHINT_BUFFER_SIZE];
	int result;
	void *private = DIGetPrivData(s);

	PVR_UNREFERENCED_PARAMETER(v);

	result = apphint_write(km_buffer, APPHINT_BUFFER_SIZE, private);
	if (result < 0) {
		PVR_DPF((PVR_DBG_ERROR, "%s: failure", __func__));
	} else {
		/* debuginfo requires a trailing \n, module_params don't */
		result += snprintf(km_buffer + result,
				APPHINT_BUFFER_SIZE - result,
				"\n");
		DIPuts(s, km_buffer);
	}

	/* have to return 0 to see output */
	return (result < 0) ? result : 0;
}

/*
*******************************************************************************
 Debug Info supporting functions
******************************************************************************/

/**
 * apphint_set - Handle a DI value update
 */
static IMG_INT64 apphint_set(const IMG_CHAR *buffer, IMG_UINT64 count,
                             IMG_UINT64 *ppos, void *data)
{
	APPHINT_ID id;
	union apphint_value value;
	struct apphint_action *action = data;
	char km_buffer[APPHINT_BUFFER_SIZE];
	int result = 0;

	if (ppos == NULL)
		return -EIO;

	if (count >= APPHINT_BUFFER_SIZE) {
		PVR_DPF((PVR_DBG_ERROR, "%s: String too long (%" IMG_INT64_FMTSPECd ")",
			__func__, count));
		result = -EINVAL;
		goto err_exit;
	}

	/* apphint_read() modifies the buffer so we need to copy it */
	memcpy(km_buffer, buffer, count);
	/* count is larger than real buffer by 1 because DI framework appends
	 * a '\0' character at the end, but here we're ignoring this */
	count -= 1;
	km_buffer[count] = '\0';

	get_apphint_id_from_action_addr(action, &id);
	result = apphint_read(km_buffer, count, id, &value);
	if (result >= 0)
		apphint_action(value, action);

	*ppos += count;
err_exit:
	return result;
}

/**
 * apphint_debuginfo_init - Create the specified debuginfo entries
 */
static int apphint_debuginfo_init(const char *sub_dir,
		unsigned device_num,
		unsigned init_data_size,
		const struct apphint_init_data *init_data,
		DI_GROUP *parentdir,
		DI_GROUP **rootdir,
		DI_ENTRY *entry[])
{
	PVRSRV_ERROR result;
	unsigned i;
	unsigned device_value_offset = device_num * APPHINT_DEBUGINFO_DEVICE_ID_MAX;
	const DI_ITERATOR_CB iterator = {
		.pfnStart = apphint_di_start, .pfnStop = apphint_di_stop,
		.pfnNext  = apphint_di_next,  .pfnShow = apphint_di_show,
		.pfnWrite = apphint_set
	};

	if (*rootdir) {
		PVR_DPF((PVR_DBG_WARNING,
			"AppHint DebugFS already created, skipping"));
		result = -EEXIST;
		goto err_exit;
	}

	result = DICreateGroup(sub_dir, parentdir, rootdir);
	if (result != PVRSRV_OK) {
		PVR_DPF((PVR_DBG_WARNING,
			"Failed to create \"%s\" DebugFS directory.", sub_dir));
		goto err_exit;
	}

	for (i = 0; i < init_data_size; i++) {
		if (!class_state[init_data[i].class].enabled)
			continue;

		result = DICreateEntry(init_data[i].name,
				*rootdir,
				&iterator,
				(void *) &apphint.val[init_data[i].id + device_value_offset],
				DI_ENTRY_TYPE_GENERIC,
				&entry[i]);
		if (result != PVRSRV_OK) {
			PVR_DPF((PVR_DBG_WARNING,
				"Failed to create \"%s/%s\" DebugFS entry.",
				sub_dir, init_data[i].name));
		}
	}

	return 0;

err_exit:
	return result;
}

/**
 * apphint_debuginfo_deinit- destroy the debuginfo entries
 */
static void apphint_debuginfo_deinit(unsigned num_entries,
		DI_GROUP **rootdir,
		DI_ENTRY *entry[])
{
	unsigned i;

	for (i = 0; i < num_entries; i++) {
		if (entry[i]) {
			DIDestroyEntry(entry[i]);
		}
	}

	if (*rootdir) {
		DIDestroyGroup(*rootdir);
		*rootdir = NULL;
	}
}

/*
*******************************************************************************
 AppHint status dump implementation
******************************************************************************/
#if defined(PDUMP)
static void apphint_pdump_values(void *flags, const IMG_CHAR *format, ...)
{
	char km_buffer[APPHINT_BUFFER_SIZE];
	IMG_UINT32 ui32Flags = *(IMG_UINT32 *)flags;
	va_list ap;

	va_start(ap, format);
	(void)vsnprintf(km_buffer, APPHINT_BUFFER_SIZE, format, ap);
	va_end(ap);

	PDumpCommentKM(km_buffer, ui32Flags);
}
#endif

static IMG_BOOL is_apphint_value_equal(const APPHINT_DATA_TYPE data_type,
									const union apphint_value * const left,
									const union apphint_value * const right)
{
		switch (data_type) {
		case APPHINT_DATA_TYPE_UINT64:
			return left->UINT64 == right->UINT64;
		case APPHINT_DATA_TYPE_UINT32:
		case APPHINT_DATA_TYPE_UINT32List:
		case APPHINT_DATA_TYPE_UINT32Bitfield:
			return left->UINT32 == right->UINT32;
		case APPHINT_DATA_TYPE_BOOL:
			return left->BOOL == right->BOOL;
		case APPHINT_DATA_TYPE_STRING:
			return (OSStringNCompare(left->STRING, right->STRING, OSStringLength(right->STRING) + 1) == 0 ? IMG_TRUE : IMG_FALSE);
		default:
			PVR_DPF((PVR_DBG_WARNING, "%s: unhandled data type (%d)", __func__, data_type));
			return IMG_FALSE;
		}
}

static void apphint_dump_values(const char *group_name,
			int device_num,
			const struct apphint_init_data *group_data,
			int group_size,
			DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
			void *pvDumpDebugFile,
			bool list_all)
{
	int i, result;
	int device_value_offset = device_num * APPHINT_DEBUGINFO_DEVICE_ID_MAX;
	char km_buffer[APPHINT_BUFFER_SIZE];
	char count = 0;

	PVR_DUMPDEBUG_LOG("  %s", group_name);
	for (i = 0; i < group_size; i++)
	{
		IMG_UINT32 id = group_data[i].id;
		APPHINT_DATA_TYPE data_type = param_lookup[id].data_type;
		const struct apphint_action *action = &apphint.val[id + device_value_offset];
		union apphint_value value;

		result = get_apphint_value_from_action(action, &value);

		if (PVRSRV_OK != result) {
			continue;
		}

		/* List only apphints with non-default values */
		if (!list_all &&
			is_apphint_value_equal(data_type, &value, &group_data[i].default_value)) {
			continue;
		}

		result = apphint_write(km_buffer, APPHINT_BUFFER_SIZE, action);
		count++;

		if (result <= 0) {
			PVR_DUMPDEBUG_LOG("    %s: <Error>",
				group_data[i].name);
		} else {
			PVR_DUMPDEBUG_LOG("    %s: %s",
				group_data[i].name, km_buffer);
		}
	}

	if (count == 0) {
		PVR_DUMPDEBUG_LOG("    none");
	}
}

/**
 * Callback for debug dump
 */
static void apphint_dump_state(PVRSRV_DBGREQ_HANDLE hDebugRequestHandle,
			IMG_UINT32 ui32VerbLevel,
			DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
			void *pvDumpDebugFile)
{
	int i, result;
	char km_buffer[APPHINT_BUFFER_SIZE];
	PVRSRV_DEVICE_NODE *device = (PVRSRV_DEVICE_NODE *)hDebugRequestHandle;

	if (DD_VERB_LVL_ENABLED(ui32VerbLevel, DEBUG_REQUEST_VERBOSITY_HIGH)) {
		PVR_DUMPDEBUG_LOG("------[ AppHint Settings ]------");

		apphint_dump_values("Build Vars", 0,
			init_data_buildvar, ARRAY_SIZE(init_data_buildvar),
			pfnDumpDebugPrintf, pvDumpDebugFile, true);

		apphint_dump_values("Module Params", 0,
			init_data_modparam, ARRAY_SIZE(init_data_modparam),
			pfnDumpDebugPrintf, pvDumpDebugFile, false);

		apphint_dump_values("Debug Info Params", 0,
			init_data_debuginfo, ARRAY_SIZE(init_data_debuginfo),
			pfnDumpDebugPrintf, pvDumpDebugFile, false);

		for (i = 0; i < APPHINT_DEVICES_MAX; i++) {
			if (!apphint.devices[i]
			    || (device && device != apphint.devices[i]))
				continue;

			result = snprintf(km_buffer,
					  APPHINT_BUFFER_SIZE,
					  "Debug Info Params Device ID: %d",
					  i);
			if (0 > result)
				continue;

			apphint_dump_values(km_buffer, i,
					    init_data_debuginfo_device,
					    ARRAY_SIZE(init_data_debuginfo_device),
					    pfnDumpDebugPrintf,
					    pvDumpDebugFile,
						false);
		}
	}
}

/*
*******************************************************************************
 Public interface
******************************************************************************/
int pvr_apphint_init(void)
{
	int result, i;

	if (apphint.initialized) {
		result = -EEXIST;
		goto err_out;
	}

	for (i = 0; i < APPHINT_DEVICES_MAX; i++)
		apphint.devices[i] = NULL;

	/* create workqueue with strict execution ordering to ensure no
	 * race conditions when setting/updating apphints from different
	 * contexts
	 */
	apphint.workqueue = alloc_workqueue("apphint_workqueue",
	                                    WQ_UNBOUND | WQ_FREEZABLE, 1);
	if (!apphint.workqueue) {
		result = -ENOMEM;
		goto err_out;
	}

	result = apphint_debuginfo_init("apphint", 0,
		ARRAY_SIZE(init_data_debuginfo), init_data_debuginfo,
		NULL,
		&apphint.debuginfo_rootdir, apphint.debuginfo_entry);
	if (0 != result)
		goto err_out;

	result = apphint_debuginfo_init("buildvar", 0,
		ARRAY_SIZE(init_data_buildvar), init_data_buildvar,
		NULL,
		&apphint.buildvar_rootdir, apphint.buildvar_entry);

	apphint.initialized = 1;

err_out:
	return result;
}

int pvr_apphint_device_register(PVRSRV_DEVICE_NODE *device)
{
	int result, i;
	char device_num[APPHINT_BUFFER_SIZE];
	unsigned device_value_offset;

	if (!apphint.initialized) {
		result = -EAGAIN;
		goto err_out;
	}

	if (apphint.num_devices+1 >= APPHINT_DEVICES_MAX) {
		result = -EMFILE;
		goto err_out;
	}

	result = snprintf(device_num, APPHINT_BUFFER_SIZE, "%u", apphint.num_devices);
	if (result < 0) {
		PVR_DPF((PVR_DBG_WARNING,
			"snprintf failed (%d)", result));
		result = -EINVAL;
		goto err_out;
	}

	/* Set the default values for the new device */
	device_value_offset = apphint.num_devices * APPHINT_DEBUGINFO_DEVICE_ID_MAX;
	for (i = 0; i < APPHINT_DEBUGINFO_DEVICE_ID_MAX; i++) {
		apphint.val[init_data_debuginfo_device[i].id + device_value_offset].stored
			= init_data_debuginfo_device[i].default_value;
	}

	/* Set value of an apphint if mapping to module param exists for it
	 * and this module parameter has been initialised */
	for (i = 0; i < ARRAY_SIZE(init_data_debuginfo_device_to_modparams); i++) {
		const struct apphint_init_data_mapping *mapping =
			&init_data_debuginfo_device_to_modparams[i];
		const struct apphint_action *modparam_action =
			&apphint.val[mapping->modparam_apphint_id];
		struct apphint_action *device_action =
			&apphint.val[mapping->device_apphint_id + device_value_offset];

		/* Set only if the module parameter was explicitly set during the module
		 * load. */
		if (modparam_action->initialised) {
			device_action->stored = modparam_action->stored;
		}
	}

	result = apphint_debuginfo_init(device_num, apphint.num_devices,
	                              ARRAY_SIZE(init_data_debuginfo_device),
	                              init_data_debuginfo_device,
	                              apphint.debuginfo_rootdir,
	                              &apphint.debuginfo_device_rootdir[apphint.num_devices],
	                              apphint.debuginfo_device_entry[apphint.num_devices]);
	if (0 != result)
		goto err_out;

	apphint.devices[apphint.num_devices] = device;
	apphint.num_devices++;

	(void)SOPvrDbgRequestNotifyRegister(
			&device->hAppHintDbgReqNotify,
			device,
			apphint_dump_state,
			DEBUG_REQUEST_APPHINT,
			device);

err_out:
	return result;
}

void pvr_apphint_device_unregister(PVRSRV_DEVICE_NODE *device)
{
	int i;

	if (!apphint.initialized)
		return;

	/* find the device */
	for (i = 0; i < APPHINT_DEVICES_MAX; i++) {
		if (apphint.devices[i] == device)
			break;
	}

	if (APPHINT_DEVICES_MAX == i)
		return;

	if (device->hAppHintDbgReqNotify) {
		(void)SOPvrDbgRequestNotifyUnregister(
			device->hAppHintDbgReqNotify);
		device->hAppHintDbgReqNotify = NULL;
	}

	apphint_debuginfo_deinit(APPHINT_DEBUGINFO_DEVICE_ID_MAX,
	                       &apphint.debuginfo_device_rootdir[i],
	                       apphint.debuginfo_device_entry[i]);

	apphint.devices[i] = NULL;

	WARN_ON(apphint.num_devices==0);
	apphint.num_devices--;
}

void pvr_apphint_deinit(void)
{
	int i;

	if (!apphint.initialized)
		return;

	/* remove any remaining device data */
	for (i = 0; apphint.num_devices && i < APPHINT_DEVICES_MAX; i++) {
		if (apphint.devices[i])
			pvr_apphint_device_unregister(apphint.devices[i]);
	}

	/* free all alloc'd string apphints and set to NULL */
	for (i = 0; i < ARRAY_SIZE(apphint.val); i++) {
		if (apphint.val[i].free && apphint.val[i].stored.STRING) {
			kfree(apphint.val[i].stored.STRING);
			apphint.val[i].stored.STRING = NULL;
			apphint.val[i].free = false;
		}
	}

	apphint_debuginfo_deinit(APPHINT_DEBUGINFO_ID_MAX,
			&apphint.debuginfo_rootdir, apphint.debuginfo_entry);
	apphint_debuginfo_deinit(APPHINT_BUILDVAR_ID_MAX,
			&apphint.buildvar_rootdir, apphint.buildvar_entry);

	destroy_workqueue(apphint.workqueue);

	apphint.initialized = 0;
}

void pvr_apphint_dump_state(void)
{
#if defined(PDUMP)
	IMG_UINT32 ui32Flags = PDUMP_FLAGS_CONTINUOUS;

	apphint_dump_state(NULL, DEBUG_REQUEST_VERBOSITY_HIGH,
	                   apphint_pdump_values, (void *)&ui32Flags);
#endif
	apphint_dump_state(NULL, DEBUG_REQUEST_VERBOSITY_HIGH,
	                   NULL, NULL);
}

int pvr_apphint_get_uint64(APPHINT_ID ue, IMG_UINT64 *pVal)
{
	int error = -ERANGE;

	if (ue < APPHINT_ID_MAX) {
		*pVal = apphint.val[ue].stored.UINT64;
		error = 0;
	}
	return error;
}

int pvr_apphint_get_uint32(APPHINT_ID ue, IMG_UINT32 *pVal)
{
	int error = -ERANGE;

	if (ue < APPHINT_ID_MAX) {
		*pVal = apphint.val[ue].stored.UINT32;
		error = 0;
	}
	return error;
}

int pvr_apphint_get_bool(APPHINT_ID ue, IMG_BOOL *pVal)
{
	int error = -ERANGE;

	if (ue < APPHINT_ID_MAX) {
		error = 0;
		*pVal = apphint.val[ue].stored.BOOL;
	}
	return error;
}

int pvr_apphint_get_string(APPHINT_ID ue, IMG_CHAR *pBuffer, size_t size)
{
	int error = -ERANGE;
	if (ue < APPHINT_ID_MAX && apphint.val[ue].stored.STRING) {
		if (OSStringLCopy(pBuffer, apphint.val[ue].stored.STRING, size) < size) {
			error = 0;
		}
	}
	return error;
}

int pvr_apphint_set_uint64(APPHINT_ID ue, IMG_UINT64 Val)
{
	int error = -ERANGE;

	if ((ue < APPHINT_ID_MAX) &&
		(param_lookup[ue].data_type == APPHINT_DATA_TYPE_UINT64)) {

		if (apphint.val[ue].set.UINT64) {
			apphint.val[ue].set.UINT64(apphint.val[ue].device, apphint.val[ue].private_data, Val);
		} else {
			apphint.val[ue].stored.UINT64 = Val;
		}
		error = 0;
	}

	return error;
}

int pvr_apphint_set_uint32(APPHINT_ID ue, IMG_UINT32 Val)
{
	int error = -ERANGE;

	if ((ue < APPHINT_ID_MAX) &&
		(param_lookup[ue].data_type == APPHINT_DATA_TYPE_UINT32)) {

		if (apphint.val[ue].set.UINT32) {
			apphint.val[ue].set.UINT32(apphint.val[ue].device, apphint.val[ue].private_data, Val);
		} else {
			apphint.val[ue].stored.UINT32 = Val;
		}
		error = 0;
	}

	return error;
}

int pvr_apphint_set_bool(APPHINT_ID ue, IMG_BOOL Val)
{
	int error = -ERANGE;

	if ((ue < APPHINT_ID_MAX) &&
		(param_lookup[ue].data_type == APPHINT_DATA_TYPE_BOOL)) {

		error = 0;
		if (apphint.val[ue].set.BOOL) {
			apphint.val[ue].set.BOOL(apphint.val[ue].device, apphint.val[ue].private_data, Val);
		} else {
			apphint.val[ue].stored.BOOL = Val;
		}
	}

	return error;
}

int pvr_apphint_set_string(APPHINT_ID ue, IMG_CHAR *pBuffer, size_t size)
{
	int error = -ERANGE;

	if ((ue < APPHINT_ID_MAX) &&
		((param_lookup[ue].data_type == APPHINT_DATA_TYPE_STRING) &&
		apphint.val[ue].stored.STRING)) {

		if (apphint.val[ue].set.STRING) {
			error = apphint.val[ue].set.STRING(apphint.val[ue].device, apphint.val[ue].private_data, pBuffer);
		} else {
			if (strlcpy(apphint.val[ue].stored.STRING, pBuffer, size) < size) {
				error = 0;
			}
		}
	}

	return error;
}

void pvr_apphint_register_handlers_uint64(APPHINT_ID id,
	PVRSRV_ERROR (*query)(const PVRSRV_DEVICE_NODE *device, const void *private_data, IMG_UINT64 *value),
	PVRSRV_ERROR (*set)(const PVRSRV_DEVICE_NODE *device, const void *private_data, IMG_UINT64 value),
	const PVRSRV_DEVICE_NODE *device,
	const void *private_data)
{
	int device_value_offset;

	if (id >= APPHINT_ID_MAX) {
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: AppHint ID (%d) is out of range, max (%d)",
		         __func__, id, APPHINT_ID_MAX-1));
		return;
	}

	get_value_offset_from_device(device, &device_value_offset);

	switch (param_lookup[id].data_type) {
	case APPHINT_DATA_TYPE_UINT64:
		break;
	default:
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Does not match AppHint data type for ID (%d)",
		         __func__, id));
		return;
	}

	apphint.val[id + device_value_offset] = (struct apphint_action){
		.query.UINT64 = query,
		.set.UINT64 = set,
		.device = device,
		.private_data = private_data,
		.stored = apphint.val[id + device_value_offset].stored
	};
}

void pvr_apphint_register_handlers_uint32(APPHINT_ID id,
	PVRSRV_ERROR (*query)(const PVRSRV_DEVICE_NODE *device, const void *private_data, IMG_UINT32 *value),
	PVRSRV_ERROR (*set)(const PVRSRV_DEVICE_NODE *device, const void *private_data, IMG_UINT32 value),
	const PVRSRV_DEVICE_NODE *device,
	const void *private_data)
{
	int device_value_offset;

	if (id >= APPHINT_ID_MAX) {
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: AppHint ID (%d) is out of range, max (%d)",
		         __func__, id, APPHINT_ID_MAX-1));
		return;
	}

	get_value_offset_from_device(device, &device_value_offset);

	switch (param_lookup[id].data_type) {
	case APPHINT_DATA_TYPE_UINT32:
	case APPHINT_DATA_TYPE_UINT32Bitfield:
	case APPHINT_DATA_TYPE_UINT32List:
		break;

	default:
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Does not match AppHint data type for ID (%d)",
		         __func__, id));
		return;
	}

	apphint.val[id + device_value_offset] = (struct apphint_action){
		.query.UINT32 = query,
		.set.UINT32 = set,
		.device = device,
		.private_data = private_data,
		.stored = apphint.val[id + device_value_offset].stored
	};
}

void pvr_apphint_register_handlers_bool(APPHINT_ID id,
	PVRSRV_ERROR (*query)(const PVRSRV_DEVICE_NODE *device, const void *private_data, IMG_BOOL *value),
	PVRSRV_ERROR (*set)(const PVRSRV_DEVICE_NODE *device, const void *private_data, IMG_BOOL value),
	const PVRSRV_DEVICE_NODE *device,
	const void *private_data)
{
	int device_value_offset;

	if (id >= APPHINT_ID_MAX) {
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: AppHint ID (%d) is out of range, max (%d)",
		         __func__, id, APPHINT_ID_MAX-1));
		return;
	}

	get_value_offset_from_device(device, &device_value_offset);

	switch (param_lookup[id].data_type) {
	case APPHINT_DATA_TYPE_BOOL:
		break;

	default:
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Does not match AppHint data type for ID (%d)",
		         __func__, id));
		return;
	}

	apphint.val[id + device_value_offset] = (struct apphint_action){
		.query.BOOL = query,
		.set.BOOL = set,
		.device = device,
		.private_data = private_data,
		.stored = apphint.val[id + device_value_offset].stored
	};
}

void pvr_apphint_register_handlers_string(APPHINT_ID id,
	PVRSRV_ERROR (*query)(const PVRSRV_DEVICE_NODE *device, const void *private_data, IMG_CHAR **value),
	PVRSRV_ERROR (*set)(const PVRSRV_DEVICE_NODE *device, const void *private_data, IMG_CHAR *value),
	const PVRSRV_DEVICE_NODE *device,
	const void *private_data)
{
	int device_value_offset;

	if (id >= APPHINT_ID_MAX) {
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: AppHint ID (%d) is out of range, max (%d)",
		         __func__, id, APPHINT_ID_MAX-1));
		return;
	}

	get_value_offset_from_device(device, &device_value_offset);

	switch (param_lookup[id].data_type) {
	case APPHINT_DATA_TYPE_STRING:
		break;

	default:
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Does not match AppHint data type for ID (%d)",
		         __func__, id));
		return;
	}

	apphint.val[id + device_value_offset] = (struct apphint_action){
		.query.STRING = query,
		.set.STRING = set,
		.device = device,
		.private_data = private_data,
		.stored = apphint.val[id + device_value_offset].stored
	};
}

/* EOF */
