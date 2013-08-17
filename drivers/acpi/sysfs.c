/*
 * sysfs.c - ACPI sysfs interface to userspace.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <acpi/acpi_drivers.h>

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME("sysfs");

#define PREFIX "ACPI: "

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

static char trace_method_name[6];
module_param_string(trace_method_name, trace_method_name, 6, 0644);
static unsigned int trace_debug_layer;
module_param(trace_debug_layer, uint, 0644);
static unsigned int trace_debug_level;
module_param(trace_debug_level, uint, 0644);

static int param_set_trace_state(const char *val, struct kernel_param *kp)
{
	int result = 0;

	if (!strncmp(val, "enable", strlen("enable"))) {
		result = acpi_debug_trace(trace_method_name, trace_debug_level,
					  trace_debug_layer, 0);
		if (result)
			result = -EBUSY;
		goto exit;
	}

	if (!strncmp(val, "disable", strlen("disable"))) {
		int name = 0;
		result = acpi_debug_trace((char *)&name, trace_debug_level,
					  trace_debug_layer, 0);
		if (result)
			result = -EBUSY;
		goto exit;
	}

	if (!strncmp(val, "1", 1)) {
		result = acpi_debug_trace(trace_method_name, trace_debug_level,
					  trace_debug_layer, 1);
		if (result)
			result = -EBUSY;
		goto exit;
	}

	result = -EINVAL;
exit:
	return result;
}

static int param_get_trace_state(char *buffer, struct kernel_param *kp)
{
	if (!acpi_gbl_trace_method_name)
		return sprintf(buffer, "disable");
	else {
		if (acpi_gbl_trace_flags & 1)
			return sprintf(buffer, "1");
		else
			return sprintf(buffer, "enable");
	}
	return 0;
}

module_param_call(trace_state, param_set_trace_state, param_get_trace_state,
		  NULL, 0644);
#endif /* CONFIG_ACPI_DEBUG */


/* /sys/modules/acpi/parameters/aml_debug_output */

module_param_named(aml_debug_output, acpi_gbl_enable_aml_debug_object,
		   bool, 0644);
MODULE_PARM_DESC(aml_debug_output,
		 "To enable/disable the ACPI Debug Object output.");

/* /sys/module/acpi/parameters/acpica_version */
static int param_get_acpica_version(char *buffer, struct kernel_param *kp)
{
	int result;

	result = sprintf(buffer, "%x", ACPI_CA_VERSION);

	return result;
}

module_param_call(acpica_version, NULL, param_get_acpica_version, NULL, 0444);

/*
 * ACPI table sysfs I/F:
 * /sys/firmware/acpi/tables/
 * /sys/firmware/acpi/tables/dynamic/
 */

static LIST_HEAD(acpi_table_attr_list);
static struct kobject *tables_kobj;
static struct kobject *dynamic_tables_kobj;

struct acpi_table_attr {
	struct bin_attribute attr;
	char name[8];
	int instance;
	struct list_head node;
};

static ssize_t acpi_table_show(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr, char *buf,
			       loff_t offset, size_t count)
{
	struct acpi_table_attr *table_attr =
	    container_of(bin_attr, struct acpi_table_attr, attr);
	struct acpi_table_header *table_header = NULL;
	acpi_status status;
	char name[ACPI_NAME_SIZE];

	if (strncmp(table_attr->name, "NULL", 4))
		memcpy(name, table_attr->name, ACPI_NAME_SIZE);
	else
		memcpy(name, "\0\0\0\0", 4);

	status = acpi_get_table(name, table_attr->instance, &table_header);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return memory_read_from_buffer(buf, count, &offset,
				       table_header, table_header->length);
}

static void acpi_table_attr_init(struct acpi_table_attr *table_attr,
				 struct acpi_table_header *table_header)
{
	struct acpi_table_header *header = NULL;
	struct acpi_table_attr *attr = NULL;

	sysfs_attr_init(&table_attr->attr.attr);
	if (table_header->signature[0] != '\0')
		memcpy(table_attr->name, table_header->signature,
		       ACPI_NAME_SIZE);
	else
		memcpy(table_attr->name, "NULL", 4);

	list_for_each_entry(attr, &acpi_table_attr_list, node) {
		if (!memcmp(table_attr->name, attr->name, ACPI_NAME_SIZE))
			if (table_attr->instance < attr->instance)
				table_attr->instance = attr->instance;
	}
	table_attr->instance++;

	if (table_attr->instance > 1 || (table_attr->instance == 1 &&
					 !acpi_get_table
					 (table_header->signature, 2, &header)))
		sprintf(table_attr->name + ACPI_NAME_SIZE, "%d",
			table_attr->instance);

	table_attr->attr.size = 0;
	table_attr->attr.read = acpi_table_show;
	table_attr->attr.attr.name = table_attr->name;
	table_attr->attr.attr.mode = 0400;

	return;
}

static acpi_status
acpi_sysfs_table_handler(u32 event, void *table, void *context)
{
	struct acpi_table_attr *table_attr;

	switch (event) {
	case ACPI_TABLE_EVENT_LOAD:
		table_attr =
		    kzalloc(sizeof(struct acpi_table_attr), GFP_KERNEL);
		if (!table_attr)
			return AE_NO_MEMORY;

		acpi_table_attr_init(table_attr, table);
		if (sysfs_create_bin_file(dynamic_tables_kobj,
					  &table_attr->attr)) {
			kfree(table_attr);
			return AE_ERROR;
		} else
			list_add_tail(&table_attr->node, &acpi_table_attr_list);
		break;
	case ACPI_TABLE_EVENT_UNLOAD:
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

static int acpi_tables_sysfs_init(void)
{
	struct acpi_table_attr *table_attr;
	struct acpi_table_header *table_header = NULL;
	int table_index = 0;
	int result;

	tables_kobj = kobject_create_and_add("tables", acpi_kobj);
	if (!tables_kobj)
		goto err;

	dynamic_tables_kobj = kobject_create_and_add("dynamic", tables_kobj);
	if (!dynamic_tables_kobj)
		goto err_dynamic_tables;

	do {
		result = acpi_get_table_by_index(table_index, &table_header);
		if (!result) {
			table_index++;
			table_attr = NULL;
			table_attr =
			    kzalloc(sizeof(struct acpi_table_attr), GFP_KERNEL);
			if (!table_attr)
				return -ENOMEM;

			acpi_table_attr_init(table_attr, table_header);
			result =
			    sysfs_create_bin_file(tables_kobj,
						  &table_attr->attr);
			if (result) {
				kfree(table_attr);
				return result;
			} else
				list_add_tail(&table_attr->node,
					      &acpi_table_attr_list);
		}
	} while (!result);
	kobject_uevent(tables_kobj, KOBJ_ADD);
	kobject_uevent(dynamic_tables_kobj, KOBJ_ADD);
	result = acpi_install_table_handler(acpi_sysfs_table_handler, NULL);

	return result == AE_OK ? 0 : -EINVAL;
err_dynamic_tables:
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

static void acpi_gbl_event_handler(u32 event_type, acpi_handle device,
	u32 event_number, void *context)
{
	if (event_type == ACPI_EVENT_TYPE_GPE)
		gpe_count(event_number);

	if (event_type == ACPI_EVENT_TYPE_FIXED)
		fixed_event_count(event_number);
}

static int get_status(u32 index, acpi_event_status *status,
		      acpi_handle *handle)
{
	int result = 0;

	if (index >= num_gpes + ACPI_NUM_FIXED_EVENTS)
		goto end;

	if (index < num_gpes) {
		result = acpi_get_gpe_device(index, handle);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_NOT_FOUND,
					"Invalid GPE 0x%x\n", index));
			goto end;
		}
		result = acpi_get_gpe_status(*handle, index, status);
	} else if (index < (num_gpes + ACPI_NUM_FIXED_EVENTS))
		result = acpi_get_event_status(index - num_gpes, status);

end:
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
	size = sprintf(buf, "%8d", all_counters[index].count);

	/* "gpe_all" or "sci" */
	if (index >= num_gpes + ACPI_NUM_FIXED_EVENTS)
		goto end;

	result = get_status(index, &status, &handle);
	if (result)
		goto end;

	if (!(status & ACPI_EVENT_FLAG_HANDLE))
		size += sprintf(buf + size, "   invalid");
	else if (status & ACPI_EVENT_FLAG_ENABLED)
		size += sprintf(buf + size, "   enabled");
	else if (status & ACPI_EVENT_FLAG_WAKE_ENABLED)
		size += sprintf(buf + size, "   wake_enabled");
	else
		size += sprintf(buf + size, "   disabled");

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

	if (!(status & ACPI_EVENT_FLAG_HANDLE)) {
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
			 (status & ACPI_EVENT_FLAG_SET))
			result = acpi_clear_gpe(handle, index);
		else
			all_counters[index].count = strtoul(buf, NULL, 0);
	} else if (index < num_gpes + ACPI_NUM_FIXED_EVENTS) {
		int event = index - num_gpes;
		if (!strcmp(buf, "disable\n") &&
		    (status & ACPI_EVENT_FLAG_ENABLED))
			result = acpi_disable_event(event, ACPI_NOT_ISR);
		else if (!strcmp(buf, "enable\n") &&
			 !(status & ACPI_EVENT_FLAG_ENABLED))
			result = acpi_enable_event(event, ACPI_NOT_ISR);
		else if (!strcmp(buf, "clear\n") &&
			 (status & ACPI_EVENT_FLAG_SET))
			result = acpi_clear_event(event);
		else
			all_counters[index].count = strtoul(buf, NULL, 0);
	} else
		all_counters[index].count = strtoul(buf, NULL, 0);

	if (ACPI_FAILURE(result))
		result = -EINVAL;
end:
	return result ? result : size;
}

void acpi_irq_stats_init(void)
{
	acpi_status status;
	int i;

	if (all_counters)
		return;

	num_gpes = acpi_current_gpe_count;
	num_counters = num_gpes + ACPI_NUM_FIXED_EVENTS + NUM_COUNTERS_EXTRA;

	all_attrs = kzalloc(sizeof(struct attribute *) * (num_counters + 1),
			    GFP_KERNEL);
	if (all_attrs == NULL)
		return;

	all_counters = kzalloc(sizeof(struct event_counter) * (num_counters),
			       GFP_KERNEL);
	if (all_counters == NULL)
		goto fail;

	status = acpi_install_global_event_handler(acpi_gbl_event_handler, NULL);
	if (ACPI_FAILURE(status))
		goto fail;

	counter_attrs = kzalloc(sizeof(struct kobj_attribute) * (num_counters),
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

		name = kzalloc(strlen(buffer) + 1, GFP_KERNEL);
		if (name == NULL)
			goto fail;
		strncpy(name, buffer, strlen(buffer) + 1);

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

int __init acpi_sysfs_init(void)
{
	int result;

	result = acpi_tables_sysfs_init();
	if (result)
		return result;
	result = sysfs_create_file(acpi_kobj, &pm_profile_attr.attr);
	return result;
}
