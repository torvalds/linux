/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ACPI_ACPI_THERMAL_H
#define __ACPI_ACPI_THERMAL_H

#include <asm/ioctl.h>

#define ACPI_THERMAL_MAGIC 's'

#define ACPI_THERMAL_GET_TRT_LEN _IOR(ACPI_THERMAL_MAGIC, 1, unsigned long)
#define ACPI_THERMAL_GET_ART_LEN _IOR(ACPI_THERMAL_MAGIC, 2, unsigned long)
#define ACPI_THERMAL_GET_TRT_COUNT _IOR(ACPI_THERMAL_MAGIC, 3, unsigned long)
#define ACPI_THERMAL_GET_ART_COUNT _IOR(ACPI_THERMAL_MAGIC, 4, unsigned long)

#define ACPI_THERMAL_GET_TRT	_IOR(ACPI_THERMAL_MAGIC, 5, unsigned long)
#define ACPI_THERMAL_GET_ART	_IOR(ACPI_THERMAL_MAGIC, 6, unsigned long)

/*
 * ACPI_THERMAL_GET_PSVT_COUNT = Number of PSVT entries
 * ACPI_THERMAL_GET_PSVT_LEN = Total return data size (PSVT count x each
 * PSVT entry size)
 * ACPI_THERMAL_GET_PSVT = Get the data as an array of psvt_objects
 */
#define ACPI_THERMAL_GET_PSVT_LEN _IOR(ACPI_THERMAL_MAGIC, 7, unsigned long)
#define ACPI_THERMAL_GET_PSVT_COUNT _IOR(ACPI_THERMAL_MAGIC, 8, unsigned long)
#define ACPI_THERMAL_GET_PSVT	_IOR(ACPI_THERMAL_MAGIC, 9, unsigned long)

struct art {
	acpi_handle source;
	acpi_handle target;
	struct_group(data,
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
	);
} __packed;

struct trt {
	acpi_handle source;
	acpi_handle target;
	u64 influence;
	u64 sample_period;
	u64 reserved1;
	u64 reserved2;
	u64 reserved3;
	u64 reserved4;
} __packed;

#define ACPI_NR_PSVT_ELEMENTS	12
#define ACPI_PSVT_CONTROL_KNOB	7
#define ACPI_LIMIT_STR_MAX_LEN	8

struct psvt {
	acpi_handle source;
	acpi_handle target;
	u64 priority;
	u64 sample_period;
	u64 passive_temp;
	u64 source_domain;
	u64 control_knob;
	union {
		/* For limit_type = ACPI_TYPE_INTEGER */
		u64 integer;
		/* For limit_type = ACPI_TYPE_STRING */
		char string[ACPI_LIMIT_STR_MAX_LEN];
		char *str_ptr;
	} limit;
	u64 step_size;
	u64 limit_coeff;
	u64 unlimit_coeff;
	/* Spec calls this field reserved, so we borrow it for type info */
	u64 control_knob_type; /* ACPI_TYPE_STRING or ACPI_TYPE_INTEGER */
} __packed;

#define ACPI_NR_ART_ELEMENTS 13
/* for usrspace */
union art_object {
	struct {
		char source_device[8]; /* ACPI single name */
		char target_device[8]; /* ACPI single name */
		struct_group(data,
			u64 weight;
			u64 ac0_max_level;
			u64 ac1_max_level;
			u64 ac2_max_level;
			u64 ac3_max_level;
			u64 ac4_max_level;
			u64 ac5_max_level;
			u64 ac6_max_level;
			u64 ac7_max_level;
			u64 ac8_max_level;
			u64 ac9_max_level;
		);
	};
	u64 __data[ACPI_NR_ART_ELEMENTS];
};

union trt_object {
	struct {
		char source_device[8]; /* ACPI single name */
		char target_device[8]; /* ACPI single name */
		u64 influence;
		u64 sample_period;
		u64 reserved[4];
	};
	u64 __data[8];
};

union psvt_object {
	struct {
		char source_device[8];
		char target_device[8];
		u64 priority;
		u64 sample_period;
		u64 passive_temp;
		u64 source_domain;
		u64 control_knob;
		union {
			u64 integer;
			char string[ACPI_LIMIT_STR_MAX_LEN];
		} limit;
		u64 step_size;
		u64 limit_coeff;
		u64 unlimit_coeff;
		u64 control_knob_type;
	};
	u64 __data[ACPI_NR_PSVT_ELEMENTS];
};

#ifdef __KERNEL__
int acpi_thermal_rel_misc_device_add(acpi_handle handle);
int acpi_thermal_rel_misc_device_remove(acpi_handle handle);
int acpi_parse_art(acpi_handle handle, int *art_count, struct art **arts,
		bool create_dev);
int acpi_parse_trt(acpi_handle handle, int *trt_count, struct trt **trts,
		bool create_dev);
#endif

#endif /* __ACPI_ACPI_THERMAL_H */
