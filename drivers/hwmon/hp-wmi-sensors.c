// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * hwmon driver for HP (and some HP Compaq) business-class computers that
 * report numeric sensor data via Windows Management Instrumentation (WMI).
 *
 * Copyright (C) 2023 James Seo <james@equiv.tech>
 *
 * References:
 * [1] Hewlett-Packard Development Company, L.P.,
 *     "HP Client Management Interface Technical White Paper", 2005. [Online].
 *     Available: https://h20331.www2.hp.com/hpsub/downloads/cmi_whitepaper.pdf
 * [2] Hewlett-Packard Development Company, L.P.,
 *     "HP Retail Manageability", 2012. [Online].
 *     Available: http://h10032.www1.hp.com/ctg/Manual/c03291135.pdf
 * [3] Linux Hardware Project, A. Ponomarenko et al.,
 *     "linuxhw/ACPI - Collect ACPI table dumps", 2018. [Online].
 *     Available: https://github.com/linuxhw/ACPI
 * [4] P. Rohár, "bmfdec - Decompile binary MOF file (BMF) from WMI buffer",
 *     2017. [Online]. Available: https://github.com/pali/bmfdec
 * [5] Microsoft Corporation, "Driver-Defined WMI Data Items", 2017. [Online].
 *     Available: https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/driver-defined-wmi-data-items
 */

#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/nls.h>
#include <linux/units.h>
#include <linux/wmi.h>

#define HP_WMI_EVENT_NAMESPACE		"root\\WMI"
#define HP_WMI_EVENT_CLASS		"HPBIOS_BIOSEvent"
#define HP_WMI_EVENT_GUID		"95F24279-4D7B-4334-9387-ACCDC67EF61C"
#define HP_WMI_NUMERIC_SENSOR_GUID	"8F1F6435-9F42-42C8-BADC-0E9424F20C9A"
#define HP_WMI_PLATFORM_EVENTS_GUID	"41227C2D-80E1-423F-8B8E-87E32755A0EB"

/* Patterns for recognizing sensors and matching events to channels. */

#define HP_WMI_PATTERN_SYS_TEMP		"Chassis Thermal Index"
#define HP_WMI_PATTERN_SYS_TEMP2	"System Ambient Temperature"
#define HP_WMI_PATTERN_CPU_TEMP		"CPU Thermal Index"
#define HP_WMI_PATTERN_CPU_TEMP2	"CPU Temperature"
#define HP_WMI_PATTERN_TEMP_SENSOR	"Thermal Index"
#define HP_WMI_PATTERN_TEMP_ALARM	"Thermal Critical"
#define HP_WMI_PATTERN_INTRUSION_ALARM	"Hood Intrusion"
#define HP_WMI_PATTERN_FAN_ALARM	"Stall"
#define HP_WMI_PATTERN_TEMP		"Temperature"
#define HP_WMI_PATTERN_CPU		"CPU"

/* These limits are arbitrary. The WMI implementation may vary by system. */

#define HP_WMI_MAX_STR_SIZE		128U
#define HP_WMI_MAX_PROPERTIES		32U
#define HP_WMI_MAX_INSTANCES		32U

enum hp_wmi_type {
	HP_WMI_TYPE_OTHER			= 1,
	HP_WMI_TYPE_TEMPERATURE			= 2,
	HP_WMI_TYPE_VOLTAGE			= 3,
	HP_WMI_TYPE_CURRENT			= 4,
	HP_WMI_TYPE_AIR_FLOW			= 12,
	HP_WMI_TYPE_INTRUSION			= 0xabadb01, /* Custom. */
};

enum hp_wmi_category {
	HP_WMI_CATEGORY_SENSOR			= 3,
};

enum hp_wmi_severity {
	HP_WMI_SEVERITY_UNKNOWN			= 0,
	HP_WMI_SEVERITY_OK			= 5,
	HP_WMI_SEVERITY_DEGRADED_WARNING	= 10,
	HP_WMI_SEVERITY_MINOR_FAILURE		= 15,
	HP_WMI_SEVERITY_MAJOR_FAILURE		= 20,
	HP_WMI_SEVERITY_CRITICAL_FAILURE	= 25,
	HP_WMI_SEVERITY_NON_RECOVERABLE_ERROR	= 30,
};

enum hp_wmi_status {
	HP_WMI_STATUS_OK			= 2,
	HP_WMI_STATUS_DEGRADED			= 3,
	HP_WMI_STATUS_STRESSED			= 4,
	HP_WMI_STATUS_PREDICTIVE_FAILURE	= 5,
	HP_WMI_STATUS_ERROR			= 6,
	HP_WMI_STATUS_NON_RECOVERABLE_ERROR	= 7,
	HP_WMI_STATUS_NO_CONTACT		= 12,
	HP_WMI_STATUS_LOST_COMMUNICATION	= 13,
	HP_WMI_STATUS_ABORTED			= 14,
	HP_WMI_STATUS_SUPPORTING_ENTITY_IN_ERROR = 16,

	/* Occurs combined with one of "OK", "Degraded", and "Error" [1]. */
	HP_WMI_STATUS_COMPLETED			= 17,
};

enum hp_wmi_units {
	HP_WMI_UNITS_OTHER			= 1,
	HP_WMI_UNITS_DEGREES_C			= 2,
	HP_WMI_UNITS_DEGREES_F			= 3,
	HP_WMI_UNITS_DEGREES_K			= 4,
	HP_WMI_UNITS_VOLTS			= 5,
	HP_WMI_UNITS_AMPS			= 6,
	HP_WMI_UNITS_RPM			= 19,
};

enum hp_wmi_property {
	HP_WMI_PROPERTY_NAME			= 0,
	HP_WMI_PROPERTY_DESCRIPTION		= 1,
	HP_WMI_PROPERTY_SENSOR_TYPE		= 2,
	HP_WMI_PROPERTY_OTHER_SENSOR_TYPE	= 3,
	HP_WMI_PROPERTY_OPERATIONAL_STATUS	= 4,
	HP_WMI_PROPERTY_SIZE			= 5,
	HP_WMI_PROPERTY_POSSIBLE_STATES		= 6,
	HP_WMI_PROPERTY_CURRENT_STATE		= 7,
	HP_WMI_PROPERTY_BASE_UNITS		= 8,
	HP_WMI_PROPERTY_UNIT_MODIFIER		= 9,
	HP_WMI_PROPERTY_CURRENT_READING		= 10,
	HP_WMI_PROPERTY_RATE_UNITS		= 11,
};

static const acpi_object_type hp_wmi_property_map[] = {
	[HP_WMI_PROPERTY_NAME]			= ACPI_TYPE_STRING,
	[HP_WMI_PROPERTY_DESCRIPTION]		= ACPI_TYPE_STRING,
	[HP_WMI_PROPERTY_SENSOR_TYPE]		= ACPI_TYPE_INTEGER,
	[HP_WMI_PROPERTY_OTHER_SENSOR_TYPE]	= ACPI_TYPE_STRING,
	[HP_WMI_PROPERTY_OPERATIONAL_STATUS]	= ACPI_TYPE_INTEGER,
	[HP_WMI_PROPERTY_SIZE]			= ACPI_TYPE_INTEGER,
	[HP_WMI_PROPERTY_POSSIBLE_STATES]	= ACPI_TYPE_STRING,
	[HP_WMI_PROPERTY_CURRENT_STATE]		= ACPI_TYPE_STRING,
	[HP_WMI_PROPERTY_BASE_UNITS]		= ACPI_TYPE_INTEGER,
	[HP_WMI_PROPERTY_UNIT_MODIFIER]		= ACPI_TYPE_INTEGER,
	[HP_WMI_PROPERTY_CURRENT_READING]	= ACPI_TYPE_INTEGER,
	[HP_WMI_PROPERTY_RATE_UNITS]		= ACPI_TYPE_INTEGER,
};

enum hp_wmi_platform_events_property {
	HP_WMI_PLATFORM_EVENTS_PROPERTY_NAME		    = 0,
	HP_WMI_PLATFORM_EVENTS_PROPERTY_DESCRIPTION	    = 1,
	HP_WMI_PLATFORM_EVENTS_PROPERTY_SOURCE_NAMESPACE    = 2,
	HP_WMI_PLATFORM_EVENTS_PROPERTY_SOURCE_CLASS	    = 3,
	HP_WMI_PLATFORM_EVENTS_PROPERTY_CATEGORY	    = 4,
	HP_WMI_PLATFORM_EVENTS_PROPERTY_POSSIBLE_SEVERITY   = 5,
	HP_WMI_PLATFORM_EVENTS_PROPERTY_POSSIBLE_STATUS	    = 6,
};

static const acpi_object_type hp_wmi_platform_events_property_map[] = {
	[HP_WMI_PLATFORM_EVENTS_PROPERTY_NAME]		    = ACPI_TYPE_STRING,
	[HP_WMI_PLATFORM_EVENTS_PROPERTY_DESCRIPTION]	    = ACPI_TYPE_STRING,
	[HP_WMI_PLATFORM_EVENTS_PROPERTY_SOURCE_NAMESPACE]  = ACPI_TYPE_STRING,
	[HP_WMI_PLATFORM_EVENTS_PROPERTY_SOURCE_CLASS]	    = ACPI_TYPE_STRING,
	[HP_WMI_PLATFORM_EVENTS_PROPERTY_CATEGORY]	    = ACPI_TYPE_INTEGER,
	[HP_WMI_PLATFORM_EVENTS_PROPERTY_POSSIBLE_SEVERITY] = ACPI_TYPE_INTEGER,
	[HP_WMI_PLATFORM_EVENTS_PROPERTY_POSSIBLE_STATUS]   = ACPI_TYPE_INTEGER,
};

enum hp_wmi_event_property {
	HP_WMI_EVENT_PROPERTY_NAME		= 0,
	HP_WMI_EVENT_PROPERTY_DESCRIPTION	= 1,
	HP_WMI_EVENT_PROPERTY_CATEGORY		= 2,
	HP_WMI_EVENT_PROPERTY_SEVERITY		= 3,
	HP_WMI_EVENT_PROPERTY_STATUS		= 4,
};

static const acpi_object_type hp_wmi_event_property_map[] = {
	[HP_WMI_EVENT_PROPERTY_NAME]		= ACPI_TYPE_STRING,
	[HP_WMI_EVENT_PROPERTY_DESCRIPTION]	= ACPI_TYPE_STRING,
	[HP_WMI_EVENT_PROPERTY_CATEGORY]	= ACPI_TYPE_INTEGER,
	[HP_WMI_EVENT_PROPERTY_SEVERITY]	= ACPI_TYPE_INTEGER,
	[HP_WMI_EVENT_PROPERTY_STATUS]		= ACPI_TYPE_INTEGER,
};

static const enum hwmon_sensor_types hp_wmi_hwmon_type_map[] = {
	[HP_WMI_TYPE_TEMPERATURE]		= hwmon_temp,
	[HP_WMI_TYPE_VOLTAGE]			= hwmon_in,
	[HP_WMI_TYPE_CURRENT]			= hwmon_curr,
	[HP_WMI_TYPE_AIR_FLOW]			= hwmon_fan,
};

static const u32 hp_wmi_hwmon_attributes[hwmon_max] = {
	[hwmon_chip]	  = HWMON_C_REGISTER_TZ,
	[hwmon_temp]	  = HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_FAULT,
	[hwmon_in]	  = HWMON_I_INPUT | HWMON_I_LABEL,
	[hwmon_curr]	  = HWMON_C_INPUT | HWMON_C_LABEL,
	[hwmon_fan]	  = HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_FAULT,
	[hwmon_intrusion] = HWMON_INTRUSION_ALARM,
};

/*
 * struct hp_wmi_numeric_sensor - a HPBIOS_BIOSNumericSensor instance
 *
 * Two variants of HPBIOS_BIOSNumericSensor are known. The first is specified
 * in [1] and appears to be much more widespread. The second was discovered by
 * decoding BMOF blobs [4], seems to be found only in some newer ZBook systems
 * [3], and has two new properties and a slightly different property order.
 *
 * These differences don't matter on Windows, where WMI object properties are
 * accessed by name. For us, supporting both variants gets ugly and hacky at
 * times. The fun begins now; this struct is defined as per the new variant.
 *
 * Effective MOF definition:
 *
 *   #pragma namespace("\\\\.\\root\\HP\\InstrumentedBIOS");
 *   class HPBIOS_BIOSNumericSensor {
 *     [read] string Name;
 *     [read] string Description;
 *     [read, ValueMap {"0","1","2","3","4","5","6","7","8","9",
 *      "10","11","12"}, Values {"Unknown","Other","Temperature",
 *      "Voltage","Current","Tachometer","Counter","Switch","Lock",
 *      "Humidity","Smoke Detection","Presence","Air Flow"}]
 *     uint32 SensorType;
 *     [read] string OtherSensorType;
 *     [read, ValueMap {"0","1","2","3","4","5","6","7","8","9",
 *      "10","11","12","13","14","15","16","17","18","..",
 *      "0x8000.."}, Values {"Unknown","Other","OK","Degraded",
 *      "Stressed","Predictive Failure","Error",
 *      "Non-Recoverable Error","Starting","Stopping","Stopped",
 *      "In Service","No Contact","Lost Communication","Aborted",
 *      "Dormant","Supporting Entity in Error","Completed",
 *      "Power Mode","DMTF Reserved","Vendor Reserved"}]
 *     uint32 OperationalStatus;
 *     [read] uint32 Size;
 *     [read] string PossibleStates[];
 *     [read] string CurrentState;
 *     [read, ValueMap {"0","1","2","3","4","5","6","7","8","9",
 *      "10","11","12","13","14","15","16","17","18","19","20",
 *      "21","22","23","24","25","26","27","28","29","30","31",
 *      "32","33","34","35","36","37","38","39","40","41","42",
 *      "43","44","45","46","47","48","49","50","51","52","53",
 *      "54","55","56","57","58","59","60","61","62","63","64",
 *      "65"}, Values {"Unknown","Other","Degrees C","Degrees F",
 *      "Degrees K","Volts","Amps","Watts","Joules","Coulombs",
 *      "VA","Nits","Lumens","Lux","Candelas","kPa","PSI",
 *      "Newtons","CFM","RPM","Hertz","Seconds","Minutes",
 *      "Hours","Days","Weeks","Mils","Inches","Feet",
 *      "Cubic Inches","Cubic Feet","Meters","Cubic Centimeters",
 *      "Cubic Meters","Liters","Fluid Ounces","Radians",
 *      "Steradians","Revolutions","Cycles","Gravities","Ounces",
 *      "Pounds","Foot-Pounds","Ounce-Inches","Gauss","Gilberts",
 *      "Henries","Farads","Ohms","Siemens","Moles","Becquerels",
 *      "PPM (parts/million)","Decibels","DbA","DbC","Grays",
 *      "Sieverts","Color Temperature Degrees K","Bits","Bytes",
 *      "Words (data)","DoubleWords","QuadWords","Percentage"}]
 *     uint32 BaseUnits;
 *     [read] sint32 UnitModifier;
 *     [read] uint32 CurrentReading;
 *     [read] uint32 RateUnits;
 *   };
 *
 * Effective MOF definition of old variant [1] (sans redundant info):
 *
 *   class HPBIOS_BIOSNumericSensor {
 *     [read] string Name;
 *     [read] string Description;
 *     [read] uint32 SensorType;
 *     [read] string OtherSensorType;
 *     [read] uint32 OperationalStatus;
 *     [read] string CurrentState;
 *     [read] string PossibleStates[];
 *     [read] uint32 BaseUnits;
 *     [read] sint32 UnitModifier;
 *     [read] uint32 CurrentReading;
 *   };
 */
struct hp_wmi_numeric_sensor {
	const char *name;
	const char *description;
	u32 sensor_type;
	const char *other_sensor_type;	/* Explains "Other" SensorType. */
	u32 operational_status;
	u8 size;			/* Count of PossibleStates[]. */
	const char **possible_states;
	const char *current_state;
	u32 base_units;
	s32 unit_modifier;
	u32 current_reading;
	u32 rate_units;
};

/*
 * struct hp_wmi_platform_events - a HPBIOS_PlatformEvents instance
 *
 * Instances of this object reveal the set of possible HPBIOS_BIOSEvent
 * instances for the current system, but it may not always be present.
 *
 * Effective MOF definition:
 *
 *   #pragma namespace("\\\\.\\root\\HP\\InstrumentedBIOS");
 *   class HPBIOS_PlatformEvents {
 *     [read] string Name;
 *     [read] string Description;
 *     [read] string SourceNamespace;
 *     [read] string SourceClass;
 *     [read, ValueMap {"0","1","2","3","4",".."}, Values {
 *      "Unknown","Configuration Change","Button Pressed",
 *      "Sensor","BIOS Settings","Reserved"}]
 *     uint32 Category;
 *     [read, ValueMap{"0","5","10","15","20","25","30",".."},
 *      Values{"Unknown","OK","Degraded/Warning","Minor Failure",
 *      "Major Failure","Critical Failure","Non-recoverable Error",
 *      "DMTF Reserved"}]
 *     uint32 PossibleSeverity;
 *     [read, ValueMap {"0","1","2","3","4","5","6","7","8","9",
 *      "10","11","12","13","14","15","16","17","18","..",
 *      "0x8000.."}, Values {"Unknown","Other","OK","Degraded",
 *      "Stressed","Predictive Failure","Error",
 *      "Non-Recoverable Error","Starting","Stopping","Stopped",
 *      "In Service","No Contact","Lost Communication","Aborted",
 *      "Dormant","Supporting Entity in Error","Completed",
 *      "Power Mode","DMTF Reserved","Vendor Reserved"}]
 *     uint32 PossibleStatus;
 *   };
 */
struct hp_wmi_platform_events {
	const char *name;
	const char *description;
	const char *source_namespace;
	const char *source_class;
	u32 category;
	u32 possible_severity;
	u32 possible_status;
};

/*
 * struct hp_wmi_event - a HPBIOS_BIOSEvent instance
 *
 * Effective MOF definition [1] (corrected below from original):
 *
 *   #pragma namespace("\\\\.\\root\\WMI");
 *   class HPBIOS_BIOSEvent : WMIEvent {
 *     [read] string Name;
 *     [read] string Description;
 *     [read ValueMap {"0","1","2","3","4"}, Values {"Unknown",
 *      "Configuration Change","Button Pressed","Sensor",
 *      "BIOS Settings"}]
 *     uint32 Category;
 *     [read, ValueMap {"0","5","10","15","20","25","30"},
 *      Values {"Unknown","OK","Degraded/Warning",
 *      "Minor Failure","Major Failure","Critical Failure",
 *      "Non-recoverable Error"}]
 *     uint32 Severity;
 *     [read, ValueMap {"0","1","2","3","4","5","6","7","8",
 *      "9","10","11","12","13","14","15","16","17","18","..",
 *      "0x8000.."}, Values {"Unknown","Other","OK","Degraded",
 *      "Stressed","Predictive Failure","Error",
 *      "Non-Recoverable Error","Starting","Stopping","Stopped",
 *      "In Service","No Contact","Lost Communication","Aborted",
 *      "Dormant","Supporting Entity in Error","Completed",
 *      "Power Mode","DMTF Reserved","Vendor Reserved"}]
 *     uint32 Status;
 *   };
 */
struct hp_wmi_event {
	const char *name;
	const char *description;
	u32 category;
};

/*
 * struct hp_wmi_info - sensor info
 * @nsensor: numeric sensor properties
 * @instance: its WMI instance number
 * @state: pointer to driver state
 * @has_alarm: whether sensor has an alarm flag
 * @alarm: alarm flag
 * @type: its hwmon sensor type
 * @cached_val: current sensor reading value, scaled for hwmon
 * @last_updated: when these readings were last updated
 */
struct hp_wmi_info {
	struct hp_wmi_numeric_sensor nsensor;
	u8 instance;
	void *state;			/* void *: Avoid forward declaration. */
	bool has_alarm;
	bool alarm;
	enum hwmon_sensor_types type;
	long cached_val;
	unsigned long last_updated;	/* In jiffies. */

};

/*
 * struct hp_wmi_sensors - driver state
 * @wdev: pointer to the parent WMI device
 * @info_map: sensor info structs by hwmon type and channel number
 * @channel_count: count of hwmon channels by hwmon type
 * @has_intrusion: whether an intrusion sensor is present
 * @intrusion: intrusion flag
 * @lock: mutex to lock polling WMI and changes to driver state
 */
struct hp_wmi_sensors {
	struct wmi_device *wdev;
	struct hp_wmi_info **info_map[hwmon_max];
	u8 channel_count[hwmon_max];
	bool has_intrusion;
	bool intrusion;

	struct mutex lock;	/* Lock polling WMI and driver state changes. */
};

static bool is_raw_wmi_string(const u8 *pointer, u32 length)
{
	const u16 *ptr;
	u16 len;

	/* WMI strings are length-prefixed UTF-16 [5]. */
	if (length <= sizeof(*ptr))
		return false;

	length -= sizeof(*ptr);
	ptr = (const u16 *)pointer;
	len = *ptr;

	return len <= length && !(len & 1);
}

static char *convert_raw_wmi_string(const u8 *buf)
{
	const wchar_t *src;
	unsigned int cps;
	unsigned int len;
	char *dst;
	int i;

	src = (const wchar_t *)buf;

	/* Count UTF-16 code points. Exclude trailing null padding. */
	cps = *src / sizeof(*src);
	while (cps && !src[cps])
		cps--;

	/* Each code point becomes up to 3 UTF-8 characters. */
	len = min(cps * 3, HP_WMI_MAX_STR_SIZE - 1);

	dst = kmalloc((len + 1) * sizeof(*dst), GFP_KERNEL);
	if (!dst)
		return NULL;

	i = utf16s_to_utf8s(++src, cps, UTF16_LITTLE_ENDIAN, dst, len);
	dst[i] = '\0';

	return dst;
}

/* hp_wmi_strdup - devm_kstrdup, but length-limited */
static char *hp_wmi_strdup(struct device *dev, const char *src)
{
	char *dst;
	size_t len;

	len = strnlen(src, HP_WMI_MAX_STR_SIZE - 1);

	dst = devm_kmalloc(dev, (len + 1) * sizeof(*dst), GFP_KERNEL);
	if (!dst)
		return NULL;

	strscpy(dst, src, len + 1);

	return dst;
}

/* hp_wmi_wstrdup - hp_wmi_strdup, but for a raw WMI string */
static char *hp_wmi_wstrdup(struct device *dev, const u8 *buf)
{
	char *src;
	char *dst;

	src = convert_raw_wmi_string(buf);
	if (!src)
		return NULL;

	dst = hp_wmi_strdup(dev, strim(src));	/* Note: Copy is trimmed. */

	kfree(src);

	return dst;
}

/*
 * hp_wmi_get_wobj - poll WMI for a WMI object instance
 * @guid: WMI object GUID
 * @instance: WMI object instance number
 *
 * Returns a new WMI object instance on success, or NULL on error.
 * Caller must kfree() the result.
 */
static union acpi_object *hp_wmi_get_wobj(const char *guid, u8 instance)
{
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status err;

	err = wmi_query_block(guid, instance, &out);
	if (ACPI_FAILURE(err))
		return NULL;

	return out.pointer;
}

/* hp_wmi_wobj_instance_count - find count of WMI object instances */
static u8 hp_wmi_wobj_instance_count(const char *guid)
{
	int count;

	count = wmi_instance_count(guid);

	return clamp(count, 0, (int)HP_WMI_MAX_INSTANCES);
}

static int check_wobj(const union acpi_object *wobj,
		      const acpi_object_type property_map[], int last_prop)
{
	acpi_object_type type = wobj->type;
	acpi_object_type valid_type;
	union acpi_object *elements;
	u32 elem_count;
	int prop;

	if (type != ACPI_TYPE_PACKAGE)
		return -EINVAL;

	elem_count = wobj->package.count;
	if (elem_count != last_prop + 1)
		return -EINVAL;

	elements = wobj->package.elements;
	for (prop = 0; prop <= last_prop; prop++) {
		type = elements[prop].type;
		valid_type = property_map[prop];
		if (type != valid_type) {
			if (type == ACPI_TYPE_BUFFER &&
			    valid_type == ACPI_TYPE_STRING &&
			    is_raw_wmi_string(elements[prop].buffer.pointer,
					      elements[prop].buffer.length))
				continue;
			return -EINVAL;
		}
	}

	return 0;
}

static int extract_acpi_value(struct device *dev,
			      union acpi_object *element,
			      acpi_object_type type,
			      u32 *out_value, char **out_string)
{
	switch (type) {
	case ACPI_TYPE_INTEGER:
		*out_value = element->integer.value;
		break;

	case ACPI_TYPE_STRING:
		*out_string = element->type == ACPI_TYPE_BUFFER ?
			hp_wmi_wstrdup(dev, element->buffer.pointer) :
			hp_wmi_strdup(dev, strim(element->string.pointer));
		if (!*out_string)
			return -ENOMEM;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * check_numeric_sensor_wobj - validate a HPBIOS_BIOSNumericSensor instance
 * @wobj: pointer to WMI object instance to check
 * @out_size: out pointer to count of possible states
 * @out_is_new: out pointer to whether this is a "new" variant object
 *
 * Returns 0 on success, or a negative error code on error.
 */
static int check_numeric_sensor_wobj(const union acpi_object *wobj,
				     u8 *out_size, bool *out_is_new)
{
	acpi_object_type type = wobj->type;
	int prop = HP_WMI_PROPERTY_NAME;
	acpi_object_type valid_type;
	union acpi_object *elements;
	u32 elem_count;
	int last_prop;
	bool is_new;
	u8 count;
	u32 j;
	u32 i;

	if (type != ACPI_TYPE_PACKAGE)
		return -EINVAL;

	/*
	 * elements is a variable-length array of ACPI objects, one for
	 * each property of the WMI object instance, except that the
	 * strings in PossibleStates[] are flattened into this array
	 * as if each individual string were a property by itself.
	 */
	elements = wobj->package.elements;

	elem_count = wobj->package.count;
	if (elem_count <= HP_WMI_PROPERTY_SIZE ||
	    elem_count > HP_WMI_MAX_PROPERTIES)
		return -EINVAL;

	type = elements[HP_WMI_PROPERTY_SIZE].type;
	switch (type) {
	case ACPI_TYPE_INTEGER:
		is_new = true;
		last_prop = HP_WMI_PROPERTY_RATE_UNITS;
		break;

	case ACPI_TYPE_STRING:
		is_new = false;
		last_prop = HP_WMI_PROPERTY_CURRENT_READING;
		break;

	default:
		return -EINVAL;
	}

	/*
	 * In general, the count of PossibleStates[] must be > 0.
	 * Also, the old variant lacks the Size property, so we may need to
	 * reduce the value of last_prop by 1 when doing arithmetic with it.
	 */
	if (elem_count < last_prop - !is_new + 1)
		return -EINVAL;

	count = elem_count - (last_prop - !is_new);

	for (i = 0; i < elem_count && prop <= last_prop; i++, prop++) {
		type = elements[i].type;
		valid_type = hp_wmi_property_map[prop];
		if (type != valid_type)
			return -EINVAL;

		switch (prop) {
		case HP_WMI_PROPERTY_OPERATIONAL_STATUS:
			/* Old variant: CurrentState follows OperationalStatus. */
			if (!is_new)
				prop = HP_WMI_PROPERTY_CURRENT_STATE - 1;
			break;

		case HP_WMI_PROPERTY_SIZE:
			/* New variant: Size == count of PossibleStates[]. */
			if (count != elements[i].integer.value)
				return -EINVAL;
			break;

		case HP_WMI_PROPERTY_POSSIBLE_STATES:
			/* PossibleStates[0] has already been type-checked. */
			for (j = 0; i + 1 < elem_count && j + 1 < count; j++) {
				type = elements[++i].type;
				if (type != valid_type)
					return -EINVAL;
			}

			/* Old variant: BaseUnits follows PossibleStates[]. */
			if (!is_new)
				prop = HP_WMI_PROPERTY_BASE_UNITS - 1;
			break;

		case HP_WMI_PROPERTY_CURRENT_STATE:
			/* Old variant: PossibleStates[] follows CurrentState. */
			if (!is_new)
				prop = HP_WMI_PROPERTY_POSSIBLE_STATES - 1;
			break;
		}
	}

	if (prop != last_prop + 1)
		return -EINVAL;

	*out_size = count;
	*out_is_new = is_new;

	return 0;
}

static int
numeric_sensor_is_connected(const struct hp_wmi_numeric_sensor *nsensor)
{
	u32 operational_status = nsensor->operational_status;

	return operational_status != HP_WMI_STATUS_NO_CONTACT;
}

static int numeric_sensor_has_fault(const struct hp_wmi_numeric_sensor *nsensor)
{
	u32 operational_status = nsensor->operational_status;

	switch (operational_status) {
	case HP_WMI_STATUS_DEGRADED:
	case HP_WMI_STATUS_STRESSED:		/* e.g. Overload, overtemp. */
	case HP_WMI_STATUS_PREDICTIVE_FAILURE:	/* e.g. Fan removed. */
	case HP_WMI_STATUS_ERROR:
	case HP_WMI_STATUS_NON_RECOVERABLE_ERROR:
	case HP_WMI_STATUS_NO_CONTACT:
	case HP_WMI_STATUS_LOST_COMMUNICATION:
	case HP_WMI_STATUS_ABORTED:
	case HP_WMI_STATUS_SUPPORTING_ENTITY_IN_ERROR:

	/* Assume combination by addition; bitwise OR doesn't make sense. */
	case HP_WMI_STATUS_COMPLETED + HP_WMI_STATUS_DEGRADED:
	case HP_WMI_STATUS_COMPLETED + HP_WMI_STATUS_ERROR:
		return true;
	}

	return false;
}

/* scale_numeric_sensor - scale sensor reading for hwmon */
static long scale_numeric_sensor(const struct hp_wmi_numeric_sensor *nsensor)
{
	u32 current_reading = nsensor->current_reading;
	s32 unit_modifier = nsensor->unit_modifier;
	u32 sensor_type = nsensor->sensor_type;
	u32 base_units = nsensor->base_units;
	s32 target_modifier;
	long val;

	/* Fan readings are in RPM units; others are in milliunits. */
	target_modifier = sensor_type == HP_WMI_TYPE_AIR_FLOW ? 0 : -3;

	val = current_reading;

	for (; unit_modifier < target_modifier; unit_modifier++)
		val = DIV_ROUND_CLOSEST(val, 10);

	for (; unit_modifier > target_modifier; unit_modifier--) {
		if (val > LONG_MAX / 10) {
			val = LONG_MAX;
			break;
		}
		val *= 10;
	}

	if (sensor_type == HP_WMI_TYPE_TEMPERATURE) {
		switch (base_units) {
		case HP_WMI_UNITS_DEGREES_F:
			val -= MILLI * 32;
			val = val <= LONG_MAX / 5 ?
				      DIV_ROUND_CLOSEST(val * 5, 9) :
				      DIV_ROUND_CLOSEST(val, 9) * 5;
			break;

		case HP_WMI_UNITS_DEGREES_K:
			val = milli_kelvin_to_millicelsius(val);
			break;
		}
	}

	return val;
}

/*
 * classify_numeric_sensor - classify a numeric sensor
 * @nsensor: pointer to numeric sensor struct
 *
 * Returns an enum hp_wmi_type value on success,
 * or a negative value if the sensor type is unsupported.
 */
static int classify_numeric_sensor(const struct hp_wmi_numeric_sensor *nsensor)
{
	u32 sensor_type = nsensor->sensor_type;
	u32 base_units = nsensor->base_units;
	const char *name = nsensor->name;

	switch (sensor_type) {
	case HP_WMI_TYPE_TEMPERATURE:
		/*
		 * Some systems have sensors named "X Thermal Index" in "Other"
		 * units. Tested CPU sensor examples were found to be in °C,
		 * albeit perhaps "differently" accurate; e.g. readings were
		 * reliably -6°C vs. coretemp on a HP Compaq Elite 8300, and
		 * +8°C on an EliteOne G1 800. But this is still within the
		 * realm of plausibility for cheaply implemented motherboard
		 * sensors, and chassis readings were about as expected.
		 */
		if ((base_units == HP_WMI_UNITS_OTHER &&
		     strstr(name, HP_WMI_PATTERN_TEMP_SENSOR)) ||
		    base_units == HP_WMI_UNITS_DEGREES_C ||
		    base_units == HP_WMI_UNITS_DEGREES_F ||
		    base_units == HP_WMI_UNITS_DEGREES_K)
			return HP_WMI_TYPE_TEMPERATURE;
		break;

	case HP_WMI_TYPE_VOLTAGE:
		if (base_units == HP_WMI_UNITS_VOLTS)
			return HP_WMI_TYPE_VOLTAGE;
		break;

	case HP_WMI_TYPE_CURRENT:
		if (base_units == HP_WMI_UNITS_AMPS)
			return HP_WMI_TYPE_CURRENT;
		break;

	case HP_WMI_TYPE_AIR_FLOW:
		/*
		 * Strangely, HP considers fan RPM sensor type to be
		 * "Air Flow" instead of the more intuitive "Tachometer".
		 */
		if (base_units == HP_WMI_UNITS_RPM)
			return HP_WMI_TYPE_AIR_FLOW;
		break;
	}

	return -EINVAL;
}

static int
populate_numeric_sensor_from_wobj(struct device *dev,
				  struct hp_wmi_numeric_sensor *nsensor,
				  union acpi_object *wobj, bool *out_is_new)
{
	int last_prop = HP_WMI_PROPERTY_RATE_UNITS;
	int prop = HP_WMI_PROPERTY_NAME;
	const char **possible_states;
	union acpi_object *element;
	acpi_object_type type;
	char *string;
	bool is_new;
	u32 value;
	u8 size;
	int err;

	err = check_numeric_sensor_wobj(wobj, &size, &is_new);
	if (err)
		return err;

	possible_states = devm_kcalloc(dev, size, sizeof(*possible_states),
				       GFP_KERNEL);
	if (!possible_states)
		return -ENOMEM;

	element = wobj->package.elements;
	nsensor->possible_states = possible_states;
	nsensor->size = size;

	if (!is_new)
		last_prop = HP_WMI_PROPERTY_CURRENT_READING;

	for (; prop <= last_prop; prop++) {
		type = hp_wmi_property_map[prop];

		err = extract_acpi_value(dev, element, type, &value, &string);
		if (err)
			return err;

		element++;

		switch (prop) {
		case HP_WMI_PROPERTY_NAME:
			nsensor->name = string;
			break;

		case HP_WMI_PROPERTY_DESCRIPTION:
			nsensor->description = string;
			break;

		case HP_WMI_PROPERTY_SENSOR_TYPE:
			if (value > HP_WMI_TYPE_AIR_FLOW)
				return -EINVAL;

			nsensor->sensor_type = value;
			break;

		case HP_WMI_PROPERTY_OTHER_SENSOR_TYPE:
			nsensor->other_sensor_type = string;
			break;

		case HP_WMI_PROPERTY_OPERATIONAL_STATUS:
			nsensor->operational_status = value;

			/* Old variant: CurrentState follows OperationalStatus. */
			if (!is_new)
				prop = HP_WMI_PROPERTY_CURRENT_STATE - 1;
			break;

		case HP_WMI_PROPERTY_SIZE:
			break;			/* Already set. */

		case HP_WMI_PROPERTY_POSSIBLE_STATES:
			*possible_states++ = string;
			if (--size)
				prop--;

			/* Old variant: BaseUnits follows PossibleStates[]. */
			if (!is_new && !size)
				prop = HP_WMI_PROPERTY_BASE_UNITS - 1;
			break;

		case HP_WMI_PROPERTY_CURRENT_STATE:
			nsensor->current_state = string;

			/* Old variant: PossibleStates[] follows CurrentState. */
			if (!is_new)
				prop = HP_WMI_PROPERTY_POSSIBLE_STATES - 1;
			break;

		case HP_WMI_PROPERTY_BASE_UNITS:
			nsensor->base_units = value;
			break;

		case HP_WMI_PROPERTY_UNIT_MODIFIER:
			/* UnitModifier is signed. */
			nsensor->unit_modifier = (s32)value;
			break;

		case HP_WMI_PROPERTY_CURRENT_READING:
			nsensor->current_reading = value;
			break;

		case HP_WMI_PROPERTY_RATE_UNITS:
			nsensor->rate_units = value;
			break;

		default:
			return -EINVAL;
		}
	}

	*out_is_new = is_new;

	return 0;
}

/* update_numeric_sensor_from_wobj - update fungible sensor properties */
static void
update_numeric_sensor_from_wobj(struct device *dev,
				struct hp_wmi_numeric_sensor *nsensor,
				const union acpi_object *wobj)
{
	const union acpi_object *elements;
	const union acpi_object *element;
	const char *new_string;
	char *trimmed;
	char *string;
	bool is_new;
	int offset;
	u8 size;
	int err;

	err = check_numeric_sensor_wobj(wobj, &size, &is_new);
	if (err)
		return;

	elements = wobj->package.elements;

	element = &elements[HP_WMI_PROPERTY_OPERATIONAL_STATUS];
	nsensor->operational_status = element->integer.value;

	/*
	 * In general, an index offset is needed after PossibleStates[0].
	 * On a new variant, CurrentState is after PossibleStates[]. This is
	 * not the case on an old variant, but we still need to offset the
	 * read because CurrentState is where Size would be on a new variant.
	 */
	offset = is_new ? size - 1 : -2;

	element = &elements[HP_WMI_PROPERTY_CURRENT_STATE + offset];
	string = element->type == ACPI_TYPE_BUFFER ?
		convert_raw_wmi_string(element->buffer.pointer) :
		element->string.pointer;

	if (string) {
		trimmed = strim(string);
		if (strcmp(trimmed, nsensor->current_state)) {
			new_string = hp_wmi_strdup(dev, trimmed);
			if (new_string) {
				devm_kfree(dev, nsensor->current_state);
				nsensor->current_state = new_string;
			}
		}
		if (element->type == ACPI_TYPE_BUFFER)
			kfree(string);
	}

	/* Old variant: -2 (not -1) because it lacks the Size property. */
	if (!is_new)
		offset = (int)size - 2;	/* size is > 0, i.e. may be 1. */

	element = &elements[HP_WMI_PROPERTY_UNIT_MODIFIER + offset];
	nsensor->unit_modifier = (s32)element->integer.value;

	element = &elements[HP_WMI_PROPERTY_CURRENT_READING + offset];
	nsensor->current_reading = element->integer.value;
}

/*
 * check_platform_events_wobj - validate a HPBIOS_PlatformEvents instance
 * @wobj: pointer to WMI object instance to check
 *
 * Returns 0 on success, or a negative error code on error.
 */
static int check_platform_events_wobj(const union acpi_object *wobj)
{
	return check_wobj(wobj, hp_wmi_platform_events_property_map,
			  HP_WMI_PLATFORM_EVENTS_PROPERTY_POSSIBLE_STATUS);
}

static int
populate_platform_events_from_wobj(struct device *dev,
				   struct hp_wmi_platform_events *pevents,
				   union acpi_object *wobj)
{
	int last_prop = HP_WMI_PLATFORM_EVENTS_PROPERTY_POSSIBLE_STATUS;
	int prop = HP_WMI_PLATFORM_EVENTS_PROPERTY_NAME;
	union acpi_object *element;
	acpi_object_type type;
	char *string;
	u32 value;
	int err;

	err = check_platform_events_wobj(wobj);
	if (err)
		return err;

	element = wobj->package.elements;

	for (; prop <= last_prop; prop++, element++) {
		type = hp_wmi_platform_events_property_map[prop];

		err = extract_acpi_value(dev, element, type, &value, &string);
		if (err)
			return err;

		switch (prop) {
		case HP_WMI_PLATFORM_EVENTS_PROPERTY_NAME:
			pevents->name = string;
			break;

		case HP_WMI_PLATFORM_EVENTS_PROPERTY_DESCRIPTION:
			pevents->description = string;
			break;

		case HP_WMI_PLATFORM_EVENTS_PROPERTY_SOURCE_NAMESPACE:
			if (strcasecmp(HP_WMI_EVENT_NAMESPACE, string))
				return -EINVAL;

			pevents->source_namespace = string;
			break;

		case HP_WMI_PLATFORM_EVENTS_PROPERTY_SOURCE_CLASS:
			if (strcasecmp(HP_WMI_EVENT_CLASS, string))
				return -EINVAL;

			pevents->source_class = string;
			break;

		case HP_WMI_PLATFORM_EVENTS_PROPERTY_CATEGORY:
			pevents->category = value;
			break;

		case HP_WMI_PLATFORM_EVENTS_PROPERTY_POSSIBLE_SEVERITY:
			pevents->possible_severity = value;
			break;

		case HP_WMI_PLATFORM_EVENTS_PROPERTY_POSSIBLE_STATUS:
			pevents->possible_status = value;
			break;

		default:
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * check_event_wobj - validate a HPBIOS_BIOSEvent instance
 * @wobj: pointer to WMI object instance to check
 *
 * Returns 0 on success, or a negative error code on error.
 */
static int check_event_wobj(const union acpi_object *wobj)
{
	return check_wobj(wobj, hp_wmi_event_property_map,
			  HP_WMI_EVENT_PROPERTY_STATUS);
}

static int populate_event_from_wobj(struct device *dev,
				    struct hp_wmi_event *event,
				    union acpi_object *wobj)
{
	int prop = HP_WMI_EVENT_PROPERTY_NAME;
	union acpi_object *element;
	acpi_object_type type;
	char *string;
	u32 value;
	int err;

	err = check_event_wobj(wobj);
	if (err)
		return err;

	element = wobj->package.elements;

	for (; prop <= HP_WMI_EVENT_PROPERTY_CATEGORY; prop++, element++) {
		type = hp_wmi_event_property_map[prop];

		err = extract_acpi_value(dev, element, type, &value, &string);
		if (err)
			return err;

		switch (prop) {
		case HP_WMI_EVENT_PROPERTY_NAME:
			event->name = string;
			break;

		case HP_WMI_EVENT_PROPERTY_DESCRIPTION:
			event->description = string;
			break;

		case HP_WMI_EVENT_PROPERTY_CATEGORY:
			event->category = value;
			break;

		default:
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * classify_event - classify an event
 * @name: event name
 * @category: event category
 *
 * Classify instances of both HPBIOS_PlatformEvents and HPBIOS_BIOSEvent from
 * property values. Recognition criteria are based on multiple ACPI dumps [3].
 *
 * Returns an enum hp_wmi_type value on success,
 * or a negative value if the event type is unsupported.
 */
static int classify_event(const char *event_name, u32 category)
{
	if (category != HP_WMI_CATEGORY_SENSOR)
		return -EINVAL;

	/* Fan events have Name "X Stall". */
	if (strstr(event_name, HP_WMI_PATTERN_FAN_ALARM))
		return HP_WMI_TYPE_AIR_FLOW;

	/* Intrusion events have Name "Hood Intrusion". */
	if (!strcmp(event_name, HP_WMI_PATTERN_INTRUSION_ALARM))
		return HP_WMI_TYPE_INTRUSION;

	/*
	 * Temperature events have Name either "Thermal Caution" or
	 * "Thermal Critical". Deal only with "Thermal Critical" events.
	 *
	 * "Thermal Caution" events have Status "Stressed", informing us that
	 * the OperationalStatus of the related sensor has become "Stressed".
	 * However, this is already a fault condition that will clear itself
	 * when the sensor recovers, so we have no further interest in them.
	 */
	if (!strcmp(event_name, HP_WMI_PATTERN_TEMP_ALARM))
		return HP_WMI_TYPE_TEMPERATURE;

	return -EINVAL;
}

/*
 * interpret_info - interpret sensor for hwmon
 * @info: pointer to sensor info struct
 *
 * Should be called after the numeric sensor member has been updated.
 */
static void interpret_info(struct hp_wmi_info *info)
{
	const struct hp_wmi_numeric_sensor *nsensor = &info->nsensor;

	info->cached_val = scale_numeric_sensor(nsensor);
	info->last_updated = jiffies;
}

/*
 * hp_wmi_update_info - poll WMI to update sensor info
 * @state: pointer to driver state
 * @info: pointer to sensor info struct
 *
 * Returns 0 on success, or a negative error code on error.
 */
static int hp_wmi_update_info(struct hp_wmi_sensors *state,
			      struct hp_wmi_info *info)
{
	struct hp_wmi_numeric_sensor *nsensor = &info->nsensor;
	struct device *dev = &state->wdev->dev;
	const union acpi_object *wobj;
	u8 instance = info->instance;
	int ret = 0;

	if (time_after(jiffies, info->last_updated + HZ)) {
		mutex_lock(&state->lock);

		wobj = hp_wmi_get_wobj(HP_WMI_NUMERIC_SENSOR_GUID, instance);
		if (!wobj) {
			ret = -EIO;
			goto out_unlock;
		}

		update_numeric_sensor_from_wobj(dev, nsensor, wobj);

		interpret_info(info);

		kfree(wobj);

out_unlock:
		mutex_unlock(&state->lock);
	}

	return ret;
}

static int basic_string_show(struct seq_file *seqf, void *ignored)
{
	const char *str = seqf->private;

	seq_printf(seqf, "%s\n", str);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(basic_string);

static int fungible_show(struct seq_file *seqf, enum hp_wmi_property prop)
{
	struct hp_wmi_numeric_sensor *nsensor;
	struct hp_wmi_sensors *state;
	struct hp_wmi_info *info;
	int err;

	info = seqf->private;
	state = info->state;
	nsensor = &info->nsensor;

	err = hp_wmi_update_info(state, info);
	if (err)
		return err;

	switch (prop) {
	case HP_WMI_PROPERTY_OPERATIONAL_STATUS:
		seq_printf(seqf, "%u\n", nsensor->operational_status);
		break;

	case HP_WMI_PROPERTY_CURRENT_STATE:
		seq_printf(seqf, "%s\n", nsensor->current_state);
		break;

	case HP_WMI_PROPERTY_UNIT_MODIFIER:
		seq_printf(seqf, "%d\n", nsensor->unit_modifier);
		break;

	case HP_WMI_PROPERTY_CURRENT_READING:
		seq_printf(seqf, "%u\n", nsensor->current_reading);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int operational_status_show(struct seq_file *seqf, void *ignored)
{
	return fungible_show(seqf, HP_WMI_PROPERTY_OPERATIONAL_STATUS);
}
DEFINE_SHOW_ATTRIBUTE(operational_status);

static int current_state_show(struct seq_file *seqf, void *ignored)
{
	return fungible_show(seqf, HP_WMI_PROPERTY_CURRENT_STATE);
}
DEFINE_SHOW_ATTRIBUTE(current_state);

static int possible_states_show(struct seq_file *seqf, void *ignored)
{
	struct hp_wmi_numeric_sensor *nsensor = seqf->private;
	u8 i;

	for (i = 0; i < nsensor->size; i++)
		seq_printf(seqf, "%s%s", i ? "," : "",
			   nsensor->possible_states[i]);

	seq_puts(seqf, "\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(possible_states);

static int unit_modifier_show(struct seq_file *seqf, void *ignored)
{
	return fungible_show(seqf, HP_WMI_PROPERTY_UNIT_MODIFIER);
}
DEFINE_SHOW_ATTRIBUTE(unit_modifier);

static int current_reading_show(struct seq_file *seqf, void *ignored)
{
	return fungible_show(seqf, HP_WMI_PROPERTY_CURRENT_READING);
}
DEFINE_SHOW_ATTRIBUTE(current_reading);

/* hp_wmi_devm_debugfs_remove - devm callback for debugfs cleanup */
static void hp_wmi_devm_debugfs_remove(void *res)
{
	debugfs_remove_recursive(res);
}

/* hp_wmi_debugfs_init - create and populate debugfs directory tree */
static void hp_wmi_debugfs_init(struct device *dev, struct hp_wmi_info *info,
				struct hp_wmi_platform_events *pevents,
				u8 icount, u8 pcount, bool is_new)
{
	struct hp_wmi_numeric_sensor *nsensor;
	char buf[HP_WMI_MAX_STR_SIZE];
	struct dentry *debugfs;
	struct dentry *entries;
	struct dentry *dir;
	int err;
	u8 i;

	/* dev_name() gives a not-very-friendly GUID for WMI devices. */
	scnprintf(buf, sizeof(buf), "hp-wmi-sensors-%u", dev->id);

	debugfs = debugfs_create_dir(buf, NULL);
	if (IS_ERR(debugfs))
		return;

	err = devm_add_action_or_reset(dev, hp_wmi_devm_debugfs_remove,
				       debugfs);
	if (err)
		return;

	entries = debugfs_create_dir("sensor", debugfs);

	for (i = 0; i < icount; i++, info++) {
		nsensor = &info->nsensor;

		scnprintf(buf, sizeof(buf), "%u", i);
		dir = debugfs_create_dir(buf, entries);

		debugfs_create_file("name", 0444, dir,
				    (void *)nsensor->name,
				    &basic_string_fops);

		debugfs_create_file("description", 0444, dir,
				    (void *)nsensor->description,
				    &basic_string_fops);

		debugfs_create_u32("sensor_type", 0444, dir,
				   &nsensor->sensor_type);

		debugfs_create_file("other_sensor_type", 0444, dir,
				    (void *)nsensor->other_sensor_type,
				    &basic_string_fops);

		debugfs_create_file("operational_status", 0444, dir,
				    info, &operational_status_fops);

		debugfs_create_file("possible_states", 0444, dir,
				    nsensor, &possible_states_fops);

		debugfs_create_file("current_state", 0444, dir,
				    info, &current_state_fops);

		debugfs_create_u32("base_units", 0444, dir,
				   &nsensor->base_units);

		debugfs_create_file("unit_modifier", 0444, dir,
				    info, &unit_modifier_fops);

		debugfs_create_file("current_reading", 0444, dir,
				    info, &current_reading_fops);

		if (is_new)
			debugfs_create_u32("rate_units", 0444, dir,
					   &nsensor->rate_units);
	}

	if (!pcount)
		return;

	entries = debugfs_create_dir("platform_events", debugfs);

	for (i = 0; i < pcount; i++, pevents++) {
		scnprintf(buf, sizeof(buf), "%u", i);
		dir = debugfs_create_dir(buf, entries);

		debugfs_create_file("name", 0444, dir,
				    (void *)pevents->name,
				    &basic_string_fops);

		debugfs_create_file("description", 0444, dir,
				    (void *)pevents->description,
				    &basic_string_fops);

		debugfs_create_file("source_namespace", 0444, dir,
				    (void *)pevents->source_namespace,
				    &basic_string_fops);

		debugfs_create_file("source_class", 0444, dir,
				    (void *)pevents->source_class,
				    &basic_string_fops);

		debugfs_create_u32("category", 0444, dir,
				   &pevents->category);

		debugfs_create_u32("possible_severity", 0444, dir,
				   &pevents->possible_severity);

		debugfs_create_u32("possible_status", 0444, dir,
				   &pevents->possible_status);
	}
}

static umode_t hp_wmi_hwmon_is_visible(const void *drvdata,
				       enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	const struct hp_wmi_sensors *state = drvdata;
	const struct hp_wmi_info *info;

	if (type == hwmon_intrusion)
		return state->has_intrusion ? 0644 : 0;

	if (!state->info_map[type] || !state->info_map[type][channel])
		return 0;

	info = state->info_map[type][channel];

	if ((type == hwmon_temp && attr == hwmon_temp_alarm) ||
	    (type == hwmon_fan  && attr == hwmon_fan_alarm))
		return info->has_alarm ? 0444 : 0;

	return 0444;
}

static int hp_wmi_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *out_val)
{
	struct hp_wmi_sensors *state = dev_get_drvdata(dev);
	const struct hp_wmi_numeric_sensor *nsensor;
	struct hp_wmi_info *info;
	int err;

	if (type == hwmon_intrusion) {
		*out_val = state->intrusion ? 1 : 0;

		return 0;
	}

	info = state->info_map[type][channel];

	if ((type == hwmon_temp && attr == hwmon_temp_alarm) ||
	    (type == hwmon_fan  && attr == hwmon_fan_alarm)) {
		*out_val = info->alarm ? 1 : 0;
		info->alarm = false;

		return 0;
	}

	nsensor = &info->nsensor;

	err = hp_wmi_update_info(state, info);
	if (err)
		return err;

	if ((type == hwmon_temp && attr == hwmon_temp_fault) ||
	    (type == hwmon_fan  && attr == hwmon_fan_fault))
		*out_val = numeric_sensor_has_fault(nsensor);
	else
		*out_val = info->cached_val;

	return 0;
}

static int hp_wmi_hwmon_read_string(struct device *dev,
				    enum hwmon_sensor_types type, u32 attr,
				    int channel, const char **out_str)
{
	const struct hp_wmi_sensors *state = dev_get_drvdata(dev);
	const struct hp_wmi_info *info;

	info = state->info_map[type][channel];
	*out_str = info->nsensor.name;

	return 0;
}

static int hp_wmi_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long val)
{
	struct hp_wmi_sensors *state = dev_get_drvdata(dev);

	if (val)
		return -EINVAL;

	mutex_lock(&state->lock);

	state->intrusion = false;

	mutex_unlock(&state->lock);

	return 0;
}

static const struct hwmon_ops hp_wmi_hwmon_ops = {
	.is_visible  = hp_wmi_hwmon_is_visible,
	.read	     = hp_wmi_hwmon_read,
	.read_string = hp_wmi_hwmon_read_string,
	.write	     = hp_wmi_hwmon_write,
};

static struct hwmon_chip_info hp_wmi_chip_info = {
	.ops         = &hp_wmi_hwmon_ops,
	.info        = NULL,
};

static struct hp_wmi_info *match_fan_event(struct hp_wmi_sensors *state,
					   const char *event_description)
{
	struct hp_wmi_info **ptr_info = state->info_map[hwmon_fan];
	u8 fan_count = state->channel_count[hwmon_fan];
	struct hp_wmi_info *info;
	const char *name;
	u8 i;

	/* Fan event has Description "X Speed". Sensor has Name "X[ Speed]". */

	for (i = 0; i < fan_count; i++, ptr_info++) {
		info = *ptr_info;
		name = info->nsensor.name;

		if (strstr(event_description, name))
			return info;
	}

	return NULL;
}

static u8 match_temp_events(struct hp_wmi_sensors *state,
			    const char *event_description,
			    struct hp_wmi_info *temp_info[])
{
	struct hp_wmi_info **ptr_info = state->info_map[hwmon_temp];
	u8 temp_count = state->channel_count[hwmon_temp];
	struct hp_wmi_info *info;
	const char *name;
	u8 count = 0;
	bool is_cpu;
	bool is_sys;
	u8 i;

	/* Description is either "CPU Thermal Index" or "Chassis Thermal Index". */

	is_cpu = !strcmp(event_description, HP_WMI_PATTERN_CPU_TEMP);
	is_sys = !strcmp(event_description, HP_WMI_PATTERN_SYS_TEMP);
	if (!is_cpu && !is_sys)
		return 0;

	/*
	 * CPU event: Match one sensor with Name either "CPU Thermal Index" or
	 * "CPU Temperature", or multiple with Name(s) "CPU[#] Temperature".
	 *
	 * Chassis event: Match one sensor with Name either
	 * "Chassis Thermal Index" or "System Ambient Temperature".
	 */

	for (i = 0; i < temp_count; i++, ptr_info++) {
		info = *ptr_info;
		name = info->nsensor.name;

		if ((is_cpu && (!strcmp(name, HP_WMI_PATTERN_CPU_TEMP) ||
				!strcmp(name, HP_WMI_PATTERN_CPU_TEMP2))) ||
		    (is_sys && (!strcmp(name, HP_WMI_PATTERN_SYS_TEMP) ||
				!strcmp(name, HP_WMI_PATTERN_SYS_TEMP2)))) {
			temp_info[0] = info;
			return 1;
		}

		if (is_cpu && (strstr(name, HP_WMI_PATTERN_CPU) &&
			       strstr(name, HP_WMI_PATTERN_TEMP)))
			temp_info[count++] = info;
	}

	return count;
}

/* hp_wmi_devm_debugfs_remove - devm callback for WMI event handler removal */
static void hp_wmi_devm_notify_remove(void *ignored)
{
	wmi_remove_notify_handler(HP_WMI_EVENT_GUID);
}

/* hp_wmi_notify - WMI event notification handler */
static void hp_wmi_notify(u32 value, void *context)
{
	struct hp_wmi_info *temp_info[HP_WMI_MAX_INSTANCES] = {};
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	struct hp_wmi_sensors *state = context;
	struct device *dev = &state->wdev->dev;
	struct hp_wmi_event event = {};
	struct hp_wmi_info *fan_info;
	union acpi_object *wobj;
	acpi_status err;
	int event_type;
	u8 count;

	/*
	 * The following warning may occur in the kernel log:
	 *
	 *   ACPI Warning: \_SB.WMID._WED: Return type mismatch -
	 *     found Package, expected Integer/String/Buffer
	 *
	 * After using [4] to decode BMOF blobs found in [3], careless copying
	 * of BIOS code seems the most likely explanation for this warning.
	 * HP_WMI_EVENT_GUID refers to \\.\root\WMI\HPBIOS_BIOSEvent on
	 * business-class systems, but it refers to \\.\root\WMI\hpqBEvnt on
	 * non-business-class systems. Per the existing hp-wmi driver, it
	 * looks like an instance of hpqBEvnt delivered as event data may
	 * indeed take the form of a raw ACPI_BUFFER on non-business-class
	 * systems ("may" because ASL shows some BIOSes do strange things).
	 *
	 * In any case, we can ignore this warning, because we always validate
	 * the event data to ensure it is an ACPI_PACKAGE containing a
	 * HPBIOS_BIOSEvent instance.
	 */

	mutex_lock(&state->lock);

	err = wmi_get_event_data(value, &out);
	if (ACPI_FAILURE(err))
		goto out_unlock;

	wobj = out.pointer;

	err = populate_event_from_wobj(dev, &event, wobj);
	if (err) {
		dev_warn(dev, "Bad event data (ACPI type %d)\n", wobj->type);
		goto out_free_wobj;
	}

	event_type = classify_event(event.name, event.category);
	switch (event_type) {
	case HP_WMI_TYPE_AIR_FLOW:
		fan_info = match_fan_event(state, event.description);
		if (fan_info)
			fan_info->alarm = true;
		break;

	case HP_WMI_TYPE_INTRUSION:
		state->intrusion = true;
		break;

	case HP_WMI_TYPE_TEMPERATURE:
		count = match_temp_events(state, event.description, temp_info);
		while (count)
			temp_info[--count]->alarm = true;
		break;

	default:
		break;
	}

out_free_wobj:
	kfree(wobj);

	devm_kfree(dev, event.name);
	devm_kfree(dev, event.description);

out_unlock:
	mutex_unlock(&state->lock);
}

static int init_platform_events(struct device *dev,
				struct hp_wmi_platform_events **out_pevents,
				u8 *out_pcount)
{
	struct hp_wmi_platform_events *pevents_arr;
	struct hp_wmi_platform_events *pevents;
	union acpi_object *wobj;
	u8 count;
	int err;
	u8 i;

	count = hp_wmi_wobj_instance_count(HP_WMI_PLATFORM_EVENTS_GUID);
	if (!count) {
		*out_pcount = 0;

		dev_dbg(dev, "No platform events\n");

		return 0;
	}

	pevents_arr = devm_kcalloc(dev, count, sizeof(*pevents), GFP_KERNEL);
	if (!pevents_arr)
		return -ENOMEM;

	for (i = 0, pevents = pevents_arr; i < count; i++, pevents++) {
		wobj = hp_wmi_get_wobj(HP_WMI_PLATFORM_EVENTS_GUID, i);
		if (!wobj)
			return -EIO;

		err = populate_platform_events_from_wobj(dev, pevents, wobj);

		kfree(wobj);

		if (err)
			return err;
	}

	*out_pevents = pevents_arr;
	*out_pcount = count;

	dev_dbg(dev, "Found %u platform events\n", count);

	return 0;
}

static int init_numeric_sensors(struct hp_wmi_sensors *state,
				struct hp_wmi_info *connected[],
				struct hp_wmi_info **out_info,
				u8 *out_icount, u8 *out_count,
				bool *out_is_new)
{
	struct hp_wmi_info ***info_map = state->info_map;
	u8 *channel_count = state->channel_count;
	struct device *dev = &state->wdev->dev;
	struct hp_wmi_numeric_sensor *nsensor;
	u8 channel_index[hwmon_max] = {};
	enum hwmon_sensor_types type;
	struct hp_wmi_info *info_arr;
	struct hp_wmi_info *info;
	union acpi_object *wobj;
	u8 count = 0;
	bool is_new;
	u8 icount;
	int wtype;
	int err;
	u8 c;
	u8 i;

	icount = hp_wmi_wobj_instance_count(HP_WMI_NUMERIC_SENSOR_GUID);
	if (!icount)
		return -ENODATA;

	info_arr = devm_kcalloc(dev, icount, sizeof(*info), GFP_KERNEL);
	if (!info_arr)
		return -ENOMEM;

	for (i = 0, info = info_arr; i < icount; i++, info++) {
		wobj = hp_wmi_get_wobj(HP_WMI_NUMERIC_SENSOR_GUID, i);
		if (!wobj)
			return -EIO;

		info->instance = i;
		info->state = state;
		nsensor = &info->nsensor;

		err = populate_numeric_sensor_from_wobj(dev, nsensor, wobj,
							&is_new);

		kfree(wobj);

		if (err)
			return err;

		if (!numeric_sensor_is_connected(nsensor))
			continue;

		wtype = classify_numeric_sensor(nsensor);
		if (wtype < 0)
			continue;

		type = hp_wmi_hwmon_type_map[wtype];

		channel_count[type]++;

		info->type = type;

		interpret_info(info);

		connected[count++] = info;
	}

	dev_dbg(dev, "Found %u sensors (%u connected)\n", i, count);

	for (i = 0; i < count; i++) {
		info = connected[i];
		type = info->type;
		c = channel_index[type]++;

		if (!info_map[type]) {
			info_map[type] = devm_kcalloc(dev, channel_count[type],
						      sizeof(*info_map),
						      GFP_KERNEL);
			if (!info_map[type])
				return -ENOMEM;
		}

		info_map[type][c] = info;
	}

	*out_info = info_arr;
	*out_icount = icount;
	*out_count = count;
	*out_is_new = is_new;

	return 0;
}

static bool find_event_attributes(struct hp_wmi_sensors *state,
				  struct hp_wmi_platform_events *pevents,
				  u8 pevents_count)
{
	/*
	 * The existence of this HPBIOS_PlatformEvents instance:
	 *
	 *   {
	 *     Name = "Rear Chassis Fan0 Stall";
	 *     Description = "Rear Chassis Fan0 Speed";
	 *     Category = 3;           // "Sensor"
	 *     PossibleSeverity = 25;  // "Critical Failure"
	 *     PossibleStatus = 5;     // "Predictive Failure"
	 *     [...]
	 *   }
	 *
	 * means that this HPBIOS_BIOSEvent instance may occur:
	 *
	 *   {
	 *     Name = "Rear Chassis Fan0 Stall";
	 *     Description = "Rear Chassis Fan0 Speed";
	 *     Category = 3;           // "Sensor"
	 *     Severity = 25;          // "Critical Failure"
	 *     Status = 5;             // "Predictive Failure"
	 *   }
	 *
	 * After the event occurs (e.g. because the fan was unplugged),
	 * polling the related HPBIOS_BIOSNumericSensor instance gives:
	 *
	 *   {
	 *      Name = "Rear Chassis Fan0";
	 *      Description = "Reports rear chassis fan0 speed";
	 *      OperationalStatus = 5; // "Predictive Failure", was 3 ("OK")
	 *      CurrentReading = 0;
	 *      [...]
	 *   }
	 *
	 * In this example, the hwmon fan channel for "Rear Chassis Fan0"
	 * should support the alarm flag and have it be set if the related
	 * HPBIOS_BIOSEvent instance occurs.
	 *
	 * In addition to fan events, temperature (CPU/chassis) and intrusion
	 * events are relevant to hwmon [2]. Note that much information in [2]
	 * is unreliable; it is referenced in addition to ACPI dumps [3] merely
	 * to support the conclusion that sensor and event names/descriptions
	 * are systematic enough to allow this driver to match them.
	 *
	 * Complications and limitations:
	 *
	 * - Strings are freeform and may vary, cf. sensor Name "CPU0 Fan"
	 *   on a Z420 vs. "CPU Fan Speed" on an EliteOne 800 G1.
	 * - Leading/trailing whitespace is a rare but real possibility [3].
	 * - The HPBIOS_PlatformEvents object may not exist or its instances
	 *   may show that the system only has e.g. BIOS setting-related
	 *   events (cf. the ProBook 4540s and ProBook 470 G0 [3]).
	 */

	struct hp_wmi_info *temp_info[HP_WMI_MAX_INSTANCES] = {};
	const char *event_description;
	struct hp_wmi_info *fan_info;
	bool has_events = false;
	const char *event_name;
	u32 event_category;
	int event_type;
	u8 count;
	u8 i;

	for (i = 0; i < pevents_count; i++, pevents++) {
		event_name = pevents->name;
		event_description = pevents->description;
		event_category = pevents->category;

		event_type = classify_event(event_name, event_category);
		switch (event_type) {
		case HP_WMI_TYPE_AIR_FLOW:
			fan_info = match_fan_event(state, event_description);
			if (!fan_info)
				break;

			fan_info->has_alarm = true;
			has_events = true;
			break;

		case HP_WMI_TYPE_INTRUSION:
			state->has_intrusion = true;
			has_events = true;
			break;

		case HP_WMI_TYPE_TEMPERATURE:
			count = match_temp_events(state, event_description,
						  temp_info);
			if (!count)
				break;

			while (count)
				temp_info[--count]->has_alarm = true;
			has_events = true;
			break;

		default:
			break;
		}
	}

	return has_events;
}

static int make_chip_info(struct hp_wmi_sensors *state, bool has_events)
{
	const struct hwmon_channel_info **ptr_channel_info;
	struct hp_wmi_info ***info_map = state->info_map;
	u8 *channel_count = state->channel_count;
	struct hwmon_channel_info *channel_info;
	struct device *dev = &state->wdev->dev;
	enum hwmon_sensor_types type;
	u8 type_count = 0;
	u32 *config;
	u32 attr;
	u8 count;
	u8 i;

	if (channel_count[hwmon_temp])
		channel_count[hwmon_chip] = 1;

	if (has_events && state->has_intrusion)
		channel_count[hwmon_intrusion] = 1;

	for (type = hwmon_chip; type < hwmon_max; type++)
		if (channel_count[type])
			type_count++;

	channel_info = devm_kcalloc(dev, type_count,
				    sizeof(*channel_info), GFP_KERNEL);
	if (!channel_info)
		return -ENOMEM;

	ptr_channel_info = devm_kcalloc(dev, type_count + 1,
					sizeof(*ptr_channel_info), GFP_KERNEL);
	if (!ptr_channel_info)
		return -ENOMEM;

	hp_wmi_chip_info.info = ptr_channel_info;

	for (type = hwmon_chip; type < hwmon_max; type++) {
		count = channel_count[type];
		if (!count)
			continue;

		config = devm_kcalloc(dev, count + 1,
				      sizeof(*config), GFP_KERNEL);
		if (!config)
			return -ENOMEM;

		attr = hp_wmi_hwmon_attributes[type];
		channel_info->type = type;
		channel_info->config = config;
		memset32(config, attr, count);

		*ptr_channel_info++ = channel_info++;

		if (!has_events || (type != hwmon_temp && type != hwmon_fan))
			continue;

		attr = type == hwmon_temp ? HWMON_T_ALARM : HWMON_F_ALARM;

		for (i = 0; i < count; i++)
			if (info_map[type][i]->has_alarm)
				config[i] |= attr;
	}

	return 0;
}

static bool add_event_handler(struct hp_wmi_sensors *state)
{
	struct device *dev = &state->wdev->dev;
	int err;

	err = wmi_install_notify_handler(HP_WMI_EVENT_GUID,
					 hp_wmi_notify, state);
	if (err) {
		dev_info(dev, "Failed to subscribe to WMI event\n");
		return false;
	}

	err = devm_add_action_or_reset(dev, hp_wmi_devm_notify_remove, NULL);
	if (err)
		return false;

	return true;
}

static int hp_wmi_sensors_init(struct hp_wmi_sensors *state)
{
	struct hp_wmi_info *connected[HP_WMI_MAX_INSTANCES];
	struct hp_wmi_platform_events *pevents = NULL;
	struct device *dev = &state->wdev->dev;
	struct hp_wmi_info *info;
	struct device *hwdev;
	bool has_events;
	bool is_new;
	u8 icount;
	u8 pcount;
	u8 count;
	int err;

	err = init_platform_events(dev, &pevents, &pcount);
	if (err)
		return err;

	err = init_numeric_sensors(state, connected, &info,
				   &icount, &count, &is_new);
	if (err)
		return err;

	if (IS_ENABLED(CONFIG_DEBUG_FS))
		hp_wmi_debugfs_init(dev, info, pevents, icount, pcount, is_new);

	if (!count)
		return 0;	/* No connected sensors; debugfs only. */

	has_events = find_event_attributes(state, pevents, pcount);

	/* Survive failure to install WMI event handler. */
	if (has_events && !add_event_handler(state))
		has_events = false;

	err = make_chip_info(state, has_events);
	if (err)
		return err;

	hwdev = devm_hwmon_device_register_with_info(dev, "hp_wmi_sensors",
						     state, &hp_wmi_chip_info,
						     NULL);
	return PTR_ERR_OR_ZERO(hwdev);
}

static int hp_wmi_sensors_probe(struct wmi_device *wdev, const void *context)
{
	struct device *dev = &wdev->dev;
	struct hp_wmi_sensors *state;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->wdev = wdev;

	mutex_init(&state->lock);

	dev_set_drvdata(dev, state);

	return hp_wmi_sensors_init(state);
}

static const struct wmi_device_id hp_wmi_sensors_id_table[] = {
	{ HP_WMI_NUMERIC_SENSOR_GUID, NULL },
	{},
};

static struct wmi_driver hp_wmi_sensors_driver = {
	.driver   = { .name = "hp-wmi-sensors" },
	.id_table = hp_wmi_sensors_id_table,
	.probe    = hp_wmi_sensors_probe,
};
module_wmi_driver(hp_wmi_sensors_driver);

MODULE_AUTHOR("James Seo <james@equiv.tech>");
MODULE_DESCRIPTION("HP WMI Sensors driver");
MODULE_LICENSE("GPL");
