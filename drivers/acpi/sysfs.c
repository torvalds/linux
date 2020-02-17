// SPDX-License-Identifier: GPL-2.0
/*
 * sysfs.c - ACPI sysfs interface to userspace.
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/acpi.h>

#include "internal.h"

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME("sysfs");

#ifdef CONFIG_ACPI_DEBUG
/*
 * ACPI debug sysfs I/F, including:
 * /sys/modules/acpi/parameters/debug_layer
 * /sys/modules/acpi/parameters/debug_level
 * /sys/modules/acpi/parameters/trace_method_name
 * /sys/modules/acpi/parameters/trace_state
 * /sys/modules/acpi/parameters/trace_debug_layer
 * /sys/modules/acpi/parameters/trace_debug_level
 */

struct acpi_dlayer {
	const char *name;
	unsigned long value;
};
struct acpi_dlevel {
	const char *name;
	unsigned long value;
};
#define ACPI_DEBUG_INIT(v)	{ .name = #v, .value = v }

static const struct acpi_dlayer acpi_debug_layers[] = {
	ACPI_DEBUG_INIT(ACPI_UTILITIES),
	ACPI_DEBUG_INIT(ACPI_HARDWARE),
	ACPI_DEBUG_INIT(ACPI_EVENTS),
	ACPI_DEBUG_INIT(ACPI_TABLES),
	ACPI_DEBUG_INIT(ACPI_NAMESPACE),
	ACPI_DEBUG_INIT(ACPI_PARSER),
	ACPI_DEBUG_INIT(ACPI_DISPATCHER),
	ACPI_DEBUG_INIT(ACPI_EXECUTER),
	ACPI_DEBUG_INIT(ACPI_RESOURCES),
	ACPI_DEBUG_INIT(ACPI_CA_DEBUGGER),
	ACPI_DEBUG_INIT(ACPI_OS_SERVICES),
	ACPI_DEBUG_INIT(ACPI_CA_DISASSEMBLER),
	ACPI_DEBUG_INIT(ACPI_COMPILER),
	ACPI_DEBUG_INIT(ACPI_TOOLS),

	ACPI_DEBUG_INIT(ACPI_BUS_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_AC_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_BATTERY_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_BUTTON_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_SBS_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_FAN_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_PCI_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_POWER_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_CONTAINER_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_SYSTEM_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_THERMAL_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_MEMORY_DEVICE_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_VIDEO_COMPONENT),
	ACPI_DEBUG_INIT(ACPI_PROCESSOR_COMPONENT),
};

static const struct acpi_dlevel acpi_debug_levels[] = {
	ACPI_DEBUG_INIT(ACPI_LV_INIT),
	ACPI_DEBUG_INIT(ACPI_LV_DEBUG_OBJECT),
	ACPI_DEBUG_INIT(ACPI_LV_INFO),
	ACPI_DEBUG_INIT(ACPI_LV_REPAIR),
	ACPI_DEBUG_INIT(ACPI_LV_TRACE_POINT),

	ACPI_DEBUG_INIT(ACPI_LV_INIT_NAMES),
	ACPI_DEBUG_INIT(ACPI_LV_PARSE),
	ACPI_DEBUG_INIT(ACPI_LV_LOAD),
	ACPI_DEBUG_INIT(ACPI_LV_DISPATCH),
	ACPI_DEBUG_INIT(ACPI_LV_EXEC),
	ACPI_DEBUG_INIT(ACPI_LV_NAMES),
	ACPI_DEBUG_INIT(ACPI_LV_OPREGION),
	ACPI_DEBUG_INIT(ACPI_LV_BFIELD),
	ACPI_DEBUG_INIT(ACPI_LV_TABLES),
	ACPI_DEBUG_INIT(ACPI_LV_VALUES),
	ACPI_DEBUG_INIT(ACPI_LV_OBJECTS),
	ACPI_DEBUG_INIT(ACPI_LV_RESOURCES),
	ACPI_DEBUG_INIT(ACPI_LV_USER_REQUESTS),
	ACPI_DEBUG_INIT(ACPI_LV_PACKAGE),

	ACPI_DEBUG_INIT(ACPI_LV_ALLOCATIONS),
	ACPI_DEBUG_INIT(ACPI_LV_FUNCTIONS),
	ACPI_DEBUG_INIT(ACPI_LV_OPTIMIZATIONS),

	ACPI_DEBUG_INIT(ACPI_LV_MUTEX),
	ACPI_DEBUG_INIT(ACPI_LV_THREADS),
	ACPI_DEBUG_INIT(ACPI_LV_IO),
	ACPI_DEBUG_INIT(ACPI_LV_INTERRUPTS),

	ACPI_DEBUG_INIT(ACPI_LV_AML_DISASSEMBLE),
	ACPI_DEBUG_INIT(ACPI_LV_VERBOSE_INFO),
	ACPI_DEBUG_INIT(ACPI_LV_FULL_TABLES),
	ACPI_DEBUG_INIT(ACPI_LV_EVENTS),
};

static int param_get_debug_layer(char *buffer, const struct kernel_param *kp)
{
	int result = 0;
	int i;

	result = sprintf(buffer, "%-25s\tHex        SET\n", "Description");

	for (i = 0; i < ARRAY_SIZE(acpi_debug_layers); i++) {
		result += sprintf(buffer + result, "%-25s\t0x%08lX [%c]\n",
				  acpi_debug_layers[i].name,
				  acpi_debug_layers[i].value,
				  (acpi_dbg_layer & acpi_debug_layers[i].value)
				  ? '*' : ' ');
	}
	result +=
	    sprintf(buffer + result, "%-25s\t0x%08X [%c]\n", "ACPI_ALL_DRIVERS",
		    ACPI_ALL_DRIVERS,
		    (acpi_dbg_layer & ACPI_ALL_DRIVERS) ==
		    ACPI_ALL_DRIVERS ? '*' : (acpi_dbg_layer & ACPI_ALL_DRIVERS)
		    == 0 ? ' ' : '-');
	result +=
	    sprintf(buffer + result,
		    "--\ndebug_layer = 0x%08X ( * = enabled)\n",
		    acpi_dbg_layer);

	return result;
}

static int param_get_debug_level(char *buffer, const struct kernel_param *kp)
{
	int result = 0;
	int i;

	result = sprintf(buffer, "%-25s\tHex        SET\n", "Description");

	for (i = 0; i < ARRAY_SIZE(acpi_debug_levels); i++) {
		result += sprintf(buffer + result, "%-25s\t0x%08lX [%c]\n",
				  acpi_debug_levels[i].name,
				  acpi_debug_levels[i].value,
				  (acpi_dbg_level & acpi_debug_levels[i].value)
				  ? '*' : ' ');
	}
	result +=
	    sprintf(buffer + result, "--\ndebug_level = 0x%08X (* = enabled)\n",
		    acpi_dbg_level);

	return result;
}

static const struct kernel_param_ops param_ops_debug_layer = {
	.set = param_set_uint,
	.get = param_get_debug_layer,
};

static const struct kernel_param_ops param_ops_debug_level = {
	.set = param_set_uint,
	.get = param_get_debug_level,
};

module_param_cb(debug_layer, &param_ops_debug_layer, &acpi_dbg_layer, 0644);
module_param_cb(debug_level, &param_ops_debug_level, &acpi_dbg_level, 0644);

static char trace_method_name[1024];

static int param_set_trace_method_name(const char *val,
				       const struct kernel_param *kp)
{
	u32 saved_flags = 0;
	bool is_abs_path = true;

	if (*val != '\\')
		is_abs_path = false;

	if ((is_abs_path && strlen(val) > 1023) ||
	    (!is_abs_path && strlen(val) > 1022)) {
		pr_err("%s: string parameter too long\n", kp->name);
		return -ENOSPC;
	}

	/*
	 * It's not safe to update acpi_gbl_trace_method_name without
	 * having the tracer stopped, so we save the original tracer
	 * state and disable it.
	 */
	saved_flags = acpi_gbl_trace_flags;
	(void)acpi_debug_trace(NULL,
			       acpi_gbl_trace_dbg_level,
			       acpi_gbl_trace_dbg_layer,
			       0);

	/* This is a hack.  We can't kmalloc in early boot. */
	if (is_abs_path)
		strcpy(trace_method_name, val);
	else {
		trace_method_name[0] = '\\';
		strcpy(trace_method_name+1, val);
	}

	/* Restore the original tracer state */
	(void)acpi_debug_trace(trace_method_name,
			       acpi_gbl_trace_dbg_level,
			       acpi_gbl_trace_dbg_layer,
			       saved_flags);

	return 0;
}

static int param_get_trace_method_name(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%s", acpi_gbl_trace_method_name);
}

static const struct kernel_param_ops param_ops_trace_method = {
	.set = param_set_trace_method_name,
	.get = param_get_trace_method_name,
};

static const struct kernel_param_ops param_ops_trace_attrib = {
	.set = param_set_uint,
	.get = param_get_uint,
};

module_param_cb(trace_method_name, &param_ops_trace_method, &trace_method_name, 0644);
module_param_cb(trace_debug_layer, &param_ops_trace_attrib, &acpi_gbl_trace_dbg_layer, 0644);
module_param_cb(trace_debug_level, &param_ops_trace_attrib, &acpi_gbl_trace_dbg_level, 0644);

static int param_set_trace_state(const char *val,
				 const struct kernel_param *kp)
{
	acpi_status status;
	const char *method = trace_method_name;
	u32 flags = 0;

/* So "xxx-once" comparison should go prior than "xxx" comparison */
#define acpi_compare_param(val, key)	\
	strncmp((val), (key), sizeof(key) - 1)

	if (!acpi_compare_param(val, "enable")) {
		method = NULL;
		flags = ACPI_TRACE_ENABLED;
	} else if (!acpi_compare_param(val, "disable"))
		method = NULL;
	else if (!acpi_compare_param(val, "method-once"))
		flags = ACPI_TRACE_ENABLED | ACPI_TRACE_ONESHOT;
	else if (!acpi_compare_param(val, "method"))
		flags = ACPI_TRACE_ENABLED;
	else if (!acpi_compare_param(val, "opcode-once"))
		flags = ACPI_TRACE_ENABLED | ACPI_TRACE_ONESHOT | ACPI_TRACE_OPCODE;
	else if (!acpi_compare_param(val, "opcode"))
		flags = ACPI_TRACE_ENABLED | ACPI_TRACE_OPCODE;
	else
		return -EINVAL;

	status = acpi_debug_trace(method,
				  acpi_gbl_trace_dbg_level,
				  acpi_gbl_trace_dbg_layer,
				  flags);
	if (ACPI_FAILURE(status))
		return -EBUSY;

	return 0;
}

static int param_get_trace_state(char *buffer, const struct kernel_param *kp)
{
	if (!(acpi_gbl_trace_flags & ACPI_TRACE_ENABLED))
		return sprintf(buffer, "disable");
	else {
		if (acpi_gbl_trace_method_name) {
			if (acpi_gbl_trace_flags & ACPI_TRACE_ONESHOT)
				return sprintf(buffer, "method-once");
			else
				return sprintf(buffer, "method");
		} else
			return sprintf(buffer, "enable");
	}
	return 0;
}

module_param_call(trace_state, param_set_trace_state, param_get_trace_state,
		  NULL, 0644);
#endif /* CONFIG_ACPI_DEBUG */


/* /sys/modules/acpi/parameters/aml_debug_output */

module_param_named(aml_debug_output, acpi_gbl_enable_aml_debug_object,
		   byte, 0644);
MODULE_PARM_DESC(aml_debug_output,
		 "To enable/disable the ACPI Debug Object output.");

/* /sys/module/acpi/parameters/acpica_version */
static int param_get_acpica_version(char *buffer,
				    const struct kernel_param *kp)
{
	int result;

	result = sprintf(buffer, "%x", ACPI_CA_VERSION);

	return result;
}

module_param_call(acpica_version, NULL, param_get_acpica_version, NULL, 0444);

/*
 * ACPI table sysfs I/F:
 * /sys/firmware/acpi/tables/
 * /sys/firmware/acpi/tables/data/
 * /sys/firmware/acpi/tables/dynamic/
 */

static LIST_HEAD(acpi_table_attr_list);
static struct kobject *tables_kobj;
static struct kobject *tables_data_kobj;
static struct kobject *dynamic_tables_kobj;
static struct kobject *hotplug_kobj;

#define ACPI_MAX_TABLE_INSTANCES	999
#define ACPI_INST_SIZE			4 /* including trailing 0 */

struct acpi_table_attr {
	struct bin_attribute attr;
	char name[ACPI_NAME_SIZE];
	int instance;
	char filename[ACPI_NAME_SIZE+ACPI_INST_SIZE];
	struct list_head node;
};

struct acpi_data_attr {
	struct bin_attribute attr;
	u64	addr;
};

static ssize_t acpi_table_show(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr, char *buf,
			       loff_t offset, size_t count)
{
	struct acpi_table_attr *table_attr =
	    container_of(bin_attr, struct acpi_table_attr, attr);
	struct acpi_table_header *table_header = NULL;
	acpi_status status;
	ssize_t rc;

	status = acpi_get_table(table_attr->name, table_attr->instance,
				&table_header);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	rc = memory_read_from_buffer(buf, count, &offset, table_header,
			table_header->length);
	acpi_put_table(table_header);
	return rc;
}

static int acpi_table_attr_init(struct kobject *tables_obj,
				struct acpi_table_attr *table_attr,
				struct acpi_table_header *table_header)
{
	struct acpi_table_header *header = NULL;
	struct acpi_table_attr *attr = NULL;
	char instance_str[ACPI_INST_SIZE];

	sysfs_attr_init(&table_attr->attr.attr);
	ACPI_MOVE_NAME(table_attr->name, table_header->signature);

	list_for_each_entry(attr, &acpi_table_attr_list, node) {
		if (ACPI_COMPARE_NAME(table_attr->name, attr->name))
			if (table_attr->instance < attr->instance)
				table_attr->instance = attr->instance;
	}
	table_attr->instance++;
	if (table_attr->instance > ACPI_MAX_TABLE_INSTANCES) {
		pr_warn("%4.4s: too many table instances\n",
			table_attr->name);
		return -ERANGE;
	}

	ACPI_MOVE_NAME(table_attr->filename, table_header->signature);
	table_attr->filename[ACPI_NAME_SIZE] = '\0';
	if (table_attr->instance > 1 || (table_attr->instance == 1 &&
					 !acpi_get_table
					 (table_header->signature, 2, &header))) {
		snprintf(instance_str, sizeof(instance_str), "%u",
			 table_attr->instance);
		strcat(table_attr->filename, instance_str);
	}

	table_attr->attr.size = table_header->length;
	table_attr->attr.read = acpi_table_show;
	table_attr->attr.attr.name = table_attr->filename;
	table_attr->attr.attr.mode = 0400;

	return sysfs_create_bin_file(tables_obj, &table_attr->attr);
}

acpi_status acpi_sysfs_table_handler(u32 event, void *table, void *context)
{
	struct acpi_table_attr *table_attr;

	switch (event) {
	case ACPI_TABLE_EVENT_INSTALL:
		table_attr =
		    kzalloc(sizeof(struct acpi_table_attr), GFP_KERNEL);
		if (!table_attr)
			return AE_NO_MEMORY;

		if (acpi_table_attr_init(dynamic_tables_kobj,
					 table_attr, table)) {
			kfree(table_attr);
			return AE_ERROR;
		}
		list_add_tail(&table_attr->node, &acpi_table_attr_list);
		break;
	case ACPI_TABLE_EVENT_LOAD:
	case ACPI_TABLE_EVENT_UNLOAD:
	case ACPI_TABLE_EVENT_UNINSTALL:
		/*
		 * we do not need to do anything right now
		 * because the table is not deleted from the
		 * global table list when unloading it.
		 */
		break;
	default:
		return AE_BAD_PARAMETER;
	}
	return AE_OK;
}

static ssize_t acpi_data_show(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *bin_attr, char *buf,
			      loff_t offset, size_t count)
{
	struct acpi_data_attr *data_attr;
	void __iomem *base;
	ssize_t rc;

	data_attr = container_of(bin_attr, struct acpi_data_attr, attr);

	base = acpi_os_map_memory(data_attr->addr, data_attr->attr.size);
	if (!base)
		return -ENOMEM;
	rc = memory_read_from_buffer(buf, count, &offset, base,
				     data_attr->attr.size);
	acpi_os_unmap_memory(base, data_attr->attr.size);

	return rc;
}

static int acpi_bert_data_init(void *th, struct acpi_data_attr *data_attr)
{
	struct acpi_table_bert *bert = th;

	if (bert->header.length < sizeof(struct acpi_table_bert) ||
	    bert->region_length < sizeof(struct acpi_hest_generic_status)) {
		kfree(data_attr);
		return -EINVAL;
	}
	data_attr->addr = bert->address;
	data_attr->attr.size = bert->region_length;
	data_attr->attr.attr.name = "BERT";

	return sysfs_create_bin_file(tables_data_kobj, &data_attr->attr);
}

static struct acpi_data_obj {
	char *name;
	int (*fn)(void *, struct acpi_data_attr *);
} acpi_data_objs[] = {
	{ ACPI_SIG_BERT, acpi_bert_data_init },
};

#define NUM_ACPI_DATA_OBJS ARRAY_SIZE(acpi_data_objs)

static int acpi_table_data_init(struct acpi_table_header *th)
{
	struct acpi_data_attr *data_attr;
	int i;

	for (i = 0; i < NUM_ACPI_DATA_OBJS; i++) {
		if (ACPI_COMPARE_NAME(th->signature, acpi_data_objs[i].name)) {
			data_attr = kzalloc(sizeof(*data_attr), GFP_KERNEL);
			if (!data_attr)
				return -ENOMEM;
			sysfs_attr_init(&data_attr->attr.attr);
			data_attr->attr.read = acpi_data_show;
			data_attr->attr.attr.mode = 0400;
			return acpi_data_objs[i].fn(th, data_attr);
		}
	}
	return 0;
}

static int acpi_tables_sysfs_init(void)
{
	struct acpi_table_attr *table_attr;
	struct acpi_table_header *table_header = NULL;
	int table_index;
	acpi_status status;
	int ret;

	tables_kobj = kobject_create_and_add("tables", acpi_kobj);
	if (!tables_kobj)
		goto err;

	tables_data_kobj = kobject_create_and_add("data", tables_kobj);
	if (!tables_data_kobj)
		goto err_tables_data;

	dynamic_tables_kobj = kobject_create_and_add("dynamic", tables_kobj);
	if (!dynamic_tables_kobj)
		goto err_dynamic_tables;

	for (table_index = 0;; table_index++) {
		status = acpi_get_table_by_index(table_index, &table_header);

		if (status == AE_BAD_PARAMETER)
			break;

		if (ACPI_FAILURE(status))
			continue;

		table_attr = kzalloc(sizeof(*table_attr), GFP_KERNEL);
		if (!table_attr)
			return -ENOMEM;

		ret = acpi_table_attr_init(tables_kobj,
					   table_attr, table_header);
		if (ret) {
			kfree(table_attr);
			return ret;
		}
		list_add_tail(&table_attr->node, &acpi_table_attr_list);
		acpi_table_data_init(table_header);
	}

	kobject_uevent(tables_kobj, KOBJ_ADD);
	kobject_uevent(tables_data_kobj, KOBJ_ADD);
	kobject_uevent(dynamic_tables_kobj, KOBJ_ADD);

	return 0;
err_dynamic_tables:
	kobject_put(tables_data_kobj);
err_tables_data:
	kobject_put(tables_kobj);
err:
	return -ENOMEM;
}

/*
 * Detailed ACPI IRQ counters:
 * /sys/firmware/acpi/interrupts/
 */

u32 acpi_irq_handled;
u32 acpi_irq_not_handled;

#define COUNT_GPE 0
#define COUNT_SCI 1		/* acpi_irq_handled */
#define COUNT_SCI_NOT 2		/* acpi_irq_not_handled */
#define COUNT_ERROR 3		/* other */
#define NUM_COUNTERS_EXTRA 4

struct event_counter {
	u32 count;
	u32 flags;
};

static struct event_counter *all_counters;
static u32 num_gpes;
static u32 num_counters;
static struct attribute **all_attrs;
static u32 acpi_gpe_count;

static struct attribute_group interrupt_stats_attr_group = {
	.name = "interrupts",
};

static struct kobj_attribute *counter_attrs;

static void delete_gpe_attr_array(void)
{
	struct event_counter *tmp = all_counters;

	all_counters = NULL;
	kfree(tmp);

	if (counter_attrs) {
		int i;

		for (i = 0; i < num_gpes; i++)
			kfree(counter_attrs[i].attr.name);

		kfree(counter_attrs);
	}
	kfree(all_attrs);

	return;
}

static void gpe_count(u32 gpe_number)
{
	acpi_gpe_count++;

	if (!all_counters)
		return;

	if (gpe_number < num_gpes)
		all_counters[gpe_number].count++;
	else
		all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS +
			     COUNT_ERROR].count++;

	return;
}

static void fixed_event_count(u32 event_number)
{
	if (!all_counters)
		return;

	if (event_number < ACPI_NUM_FIXED_EVENTS)
		all_counters[num_gpes + event_number].count++;
	else
		all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS +
			     COUNT_ERROR].count++;

	return;
}

static void acpi_global_event_handler(u32 event_type, acpi_handle device,
	u32 event_number, void *context)
{
	if (event_type == ACPI_EVENT_TYPE_GPE) {
		gpe_count(event_number);
		pr_debug("GPE event 0x%02x\n", event_number);
	} else if (event_type == ACPI_EVENT_TYPE_FIXED) {
		fixed_event_count(event_number);
		pr_debug("Fixed event 0x%02x\n", event_number);
	} else {
		pr_debug("Other event 0x%02x\n", event_number);
	}
}

static int get_status(u32 index, acpi_event_status *status,
		      acpi_handle *handle)
{
	int result;

	if (index >= num_gpes + ACPI_NUM_FIXED_EVENTS)
		return -EINVAL;

	if (index < num_gpes) {
		result = acpi_get_gpe_device(index, handle);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_NOT_FOUND,
					"Invalid GPE 0x%x", index));
			return result;
		}
		result = acpi_get_gpe_status(*handle, index, status);
	} else if (index < (num_gpes + ACPI_NUM_FIXED_EVENTS))
		result = acpi_get_event_status(index - num_gpes, status);

	return result;
}

static ssize_t counter_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	int index = attr - counter_attrs;
	int size;
	acpi_handle handle;
	acpi_event_status status;
	int result = 0;

	all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_SCI].count =
	    acpi_irq_handled;
	all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_SCI_NOT].count =
	    acpi_irq_not_handled;
	all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_GPE].count =
	    acpi_gpe_count;
	size = sprintf(buf, "%8u", all_counters[index].count);

	/* "gpe_all" or "sci" */
	if (index >= num_gpes + ACPI_NUM_FIXED_EVENTS)
		goto end;

	result = get_status(index, &status, &handle);
	if (result)
		goto end;

	if (status & ACPI_EVENT_FLAG_ENABLE_SET)
		size += sprintf(buf + size, "  EN");
	else
		size += sprintf(buf + size, "    ");
	if (status & ACPI_EVENT_FLAG_STATUS_SET)
		size += sprintf(buf + size, " STS");
	else
		size += sprintf(buf + size, "    ");

	if (!(status & ACPI_EVENT_FLAG_HAS_HANDLER))
		size += sprintf(buf + size, " invalid     ");
	else if (status & ACPI_EVENT_FLAG_ENABLED)
		size += sprintf(buf + size, " enabled     ");
	else if (status & ACPI_EVENT_FLAG_WAKE_ENABLED)
		size += sprintf(buf + size, " wake_enabled");
	else
		size += sprintf(buf + size, " disabled    ");
	if (status & ACPI_EVENT_FLAG_MASKED)
		size += sprintf(buf + size, " masked  ");
	else
		size += sprintf(buf + size, " unmasked");

end:
	size += sprintf(buf + size, "\n");
	return result ? result : size;
}

/*
 * counter_set() sets the specified counter.
 * setting the total "sci" file to any value clears all counters.
 * enable/disable/clear a gpe/fixed event in user space.
 */
static ssize_t counter_set(struct kobject *kobj,
			   struct kobj_attribute *attr, const char *buf,
			   size_t size)
{
	int index = attr - counter_attrs;
	acpi_event_status status;
	acpi_handle handle;
	int result = 0;
	unsigned long tmp;

	if (index == num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_SCI) {
		int i;
		for (i = 0; i < num_counters; ++i)
			all_counters[i].count = 0;
		acpi_gpe_count = 0;
		acpi_irq_handled = 0;
		acpi_irq_not_handled = 0;
		goto end;
	}

	/* show the event status for both GPEs and Fixed Events */
	result = get_status(index, &status, &handle);
	if (result)
		goto end;

	if (!(status & ACPI_EVENT_FLAG_HAS_HANDLER)) {
		printk(KERN_WARNING PREFIX
		       "Can not change Invalid GPE/Fixed Event status\n");
		return -EINVAL;
	}

	if (index < num_gpes) {
		if (!strcmp(buf, "disable\n") &&
		    (status & ACPI_EVENT_FLAG_ENABLED))
			result = acpi_disable_gpe(handle, index);
		else if (!strcmp(buf, "enable\n") &&
			 !(status & ACPI_EVENT_FLAG_ENABLED))
			result = acpi_enable_gpe(handle, index);
		else if (!strcmp(buf, "clear\n") &&
			 (status & ACPI_EVENT_FLAG_STATUS_SET))
			result = acpi_clear_gpe(handle, index);
		else if (!strcmp(buf, "mask\n"))
			result = acpi_mask_gpe(handle, index, TRUE);
		else if (!strcmp(buf, "unmask\n"))
			result = acpi_mask_gpe(handle, index, FALSE);
		else if (!kstrtoul(buf, 0, &tmp))
			all_counters[index].count = tmp;
		else
			result = -EINVAL;
	} else if (index < num_gpes + ACPI_NUM_FIXED_EVENTS) {
		int event = index - num_gpes;
		if (!strcmp(buf, "disable\n") &&
		    (status & ACPI_EVENT_FLAG_ENABLE_SET))
			result = acpi_disable_event(event, ACPI_NOT_ISR);
		else if (!strcmp(buf, "enable\n") &&
			 !(status & ACPI_EVENT_FLAG_ENABLE_SET))
			result = acpi_enable_event(event, ACPI_NOT_ISR);
		else if (!strcmp(buf, "clear\n") &&
			 (status & ACPI_EVENT_FLAG_STATUS_SET))
			result = acpi_clear_event(event);
		else if (!kstrtoul(buf, 0, &tmp))
			all_counters[index].count = tmp;
		else
			result = -EINVAL;
	} else
		all_counters[index].count = strtoul(buf, NULL, 0);

	if (ACPI_FAILURE(result))
		result = -EINVAL;
end:
	return result ? result : size;
}

/*
 * A Quirk Mechanism for GPE Flooding Prevention:
 *
 * Quirks may be needed to prevent GPE flooding on a specific GPE. The
 * flooding typically cannot be detected and automatically prevented by
 * ACPI_GPE_DISPATCH_NONE check because there is a _Lxx/_Exx prepared in
 * the AML tables. This normally indicates a feature gap in Linux, thus
 * instead of providing endless quirk tables, we provide a boot parameter
 * for those who want this quirk. For example, if the users want to prevent
 * the GPE flooding for GPE 00, they need to specify the following boot
 * parameter:
 *   acpi_mask_gpe=0x00
 * The masking status can be modified by the following runtime controlling
 * interface:
 *   echo unmask > /sys/firmware/acpi/interrupts/gpe00
 */
#define ACPI_MASKABLE_GPE_MAX	0x100
static DECLARE_BITMAP(acpi_masked_gpes_map, ACPI_MASKABLE_GPE_MAX) __initdata;

static int __init acpi_gpe_set_masked_gpes(char *val)
{
	u8 gpe;

	if (kstrtou8(val, 0, &gpe))
		return -EINVAL;
	set_bit(gpe, acpi_masked_gpes_map);

	return 1;
}
__setup("acpi_mask_gpe=", acpi_gpe_set_masked_gpes);

void __init acpi_gpe_apply_masked_gpes(void)
{
	acpi_handle handle;
	acpi_status status;
	u16 gpe;

	for_each_set_bit(gpe, acpi_masked_gpes_map, ACPI_MASKABLE_GPE_MAX) {
		status = acpi_get_gpe_device(gpe, &handle);
		if (ACPI_SUCCESS(status)) {
			pr_info("Masking GPE 0x%x.\n", gpe);
			(void)acpi_mask_gpe(handle, gpe, TRUE);
		}
	}
}

void acpi_irq_stats_init(void)
{
	acpi_status status;
	int i;

	if (all_counters)
		return;

	num_gpes = acpi_current_gpe_count;
	num_counters = num_gpes + ACPI_NUM_FIXED_EVENTS + NUM_COUNTERS_EXTRA;

	all_attrs = kcalloc(num_counters + 1, sizeof(struct attribute *),
			    GFP_KERNEL);
	if (all_attrs == NULL)
		return;

	all_counters = kcalloc(num_counters, sizeof(struct event_counter),
			       GFP_KERNEL);
	if (all_counters == NULL)
		goto fail;

	status = acpi_install_global_event_handler(acpi_global_event_handler, NULL);
	if (ACPI_FAILURE(status))
		goto fail;

	counter_attrs = kcalloc(num_counters, sizeof(struct kobj_attribute),
				GFP_KERNEL);
	if (counter_attrs == NULL)
		goto fail;

	for (i = 0; i < num_counters; ++i) {
		char buffer[12];
		char *name;

		if (i < num_gpes)
			sprintf(buffer, "gpe%02X", i);
		else if (i == num_gpes + ACPI_EVENT_PMTIMER)
			sprintf(buffer, "ff_pmtimer");
		else if (i == num_gpes + ACPI_EVENT_GLOBAL)
			sprintf(buffer, "ff_gbl_lock");
		else if (i == num_gpes + ACPI_EVENT_POWER_BUTTON)
			sprintf(buffer, "ff_pwr_btn");
		else if (i == num_gpes + ACPI_EVENT_SLEEP_BUTTON)
			sprintf(buffer, "ff_slp_btn");
		else if (i == num_gpes + ACPI_EVENT_RTC)
			sprintf(buffer, "ff_rt_clk");
		else if (i == num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_GPE)
			sprintf(buffer, "gpe_all");
		else if (i == num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_SCI)
			sprintf(buffer, "sci");
		else if (i == num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_SCI_NOT)
			sprintf(buffer, "sci_not");
		else if (i == num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_ERROR)
			sprintf(buffer, "error");
		else
			sprintf(buffer, "bug%02X", i);

		name = kstrdup(buffer, GFP_KERNEL);
		if (name == NULL)
			goto fail;

		sysfs_attr_init(&counter_attrs[i].attr);
		counter_attrs[i].attr.name = name;
		counter_attrs[i].attr.mode = 0644;
		counter_attrs[i].show = counter_show;
		counter_attrs[i].store = counter_set;

		all_attrs[i] = &counter_attrs[i].attr;
	}

	interrupt_stats_attr_group.attrs = all_attrs;
	if (!sysfs_create_group(acpi_kobj, &interrupt_stats_attr_group))
		return;

fail:
	delete_gpe_attr_array();
	return;
}

static void __exit interrupt_stats_exit(void)
{
	sysfs_remove_group(acpi_kobj, &interrupt_stats_attr_group);

	delete_gpe_attr_array();

	return;
}

static ssize_t
acpi_show_profile(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	return sprintf(buf, "%d\n", acpi_gbl_FADT.preferred_profile);
}

static const struct device_attribute pm_profile_attr =
	__ATTR(pm_profile, S_IRUGO, acpi_show_profile, NULL);

static ssize_t hotplug_enabled_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	struct acpi_hotplug_profile *hotplug = to_acpi_hotplug_profile(kobj);

	return sprintf(buf, "%d\n", hotplug->enabled);
}

static ssize_t hotplug_enabled_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t size)
{
	struct acpi_hotplug_profile *hotplug = to_acpi_hotplug_profile(kobj);
	unsigned int val;

	if (kstrtouint(buf, 10, &val) || val > 1)
		return -EINVAL;

	acpi_scan_hotplug_enabled(hotplug, val);
	return size;
}

static struct kobj_attribute hotplug_enabled_attr =
	__ATTR(enabled, S_IRUGO | S_IWUSR, hotplug_enabled_show,
		hotplug_enabled_store);

static struct attribute *hotplug_profile_attrs[] = {
	&hotplug_enabled_attr.attr,
	NULL
};

static struct kobj_type acpi_hotplug_profile_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.default_attrs = hotplug_profile_attrs,
};

void acpi_sysfs_add_hotplug_profile(struct acpi_hotplug_profile *hotplug,
				    const char *name)
{
	int error;

	if (!hotplug_kobj)
		goto err_out;

	error = kobject_init_and_add(&hotplug->kobj,
		&acpi_hotplug_profile_ktype, hotplug_kobj, "%s", name);
	if (error)
		goto err_out;

	kobject_uevent(&hotplug->kobj, KOBJ_ADD);
	return;

 err_out:
	pr_err(PREFIX "Unable to add hotplug profile '%s'\n", name);
}

static ssize_t force_remove_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", 0);
}

static ssize_t force_remove_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t size)
{
	bool val;
	int ret;

	ret = strtobool(buf, &val);
	if (ret < 0)
		return ret;

	if (val) {
		pr_err("Enabling force_remove is not supported anymore. Please report to linux-acpi@vger.kernel.org if you depend on this functionality\n");
		return -EINVAL;
	}
	return size;
}

static const struct kobj_attribute force_remove_attr =
	__ATTR(force_remove, S_IRUGO | S_IWUSR, force_remove_show,
	       force_remove_store);

int __init acpi_sysfs_init(void)
{
	int result;

	result = acpi_tables_sysfs_init();
	if (result)
		return result;

	hotplug_kobj = kobject_create_and_add("hotplug", acpi_kobj);
	if (!hotplug_kobj)
		return -ENOMEM;

	result = sysfs_create_file(hotplug_kobj, &force_remove_attr.attr);
	if (result)
		return result;

	result = sysfs_create_file(acpi_kobj, &pm_profile_attr.attr);
	return result;
}
