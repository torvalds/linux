// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device-mapper.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include "admin-state.h"
#include "block-map.h"
#include "completion.h"
#include "constants.h"
#include "data-vio.h"
#include "dedupe.h"
#include "dump.h"
#include "encodings.h"
#include "errors.h"
#include "flush.h"
#include "io-submitter.h"
#include "logger.h"
#include "memory-alloc.h"
#include "message-stats.h"
#include "recovery-journal.h"
#include "repair.h"
#include "slab-depot.h"
#include "status-codes.h"
#include "string-utils.h"
#include "thread-device.h"
#include "thread-registry.h"
#include "thread-utils.h"
#include "types.h"
#include "vdo.h"
#include "vio.h"

enum admin_phases {
	GROW_LOGICAL_PHASE_START,
	GROW_LOGICAL_PHASE_GROW_BLOCK_MAP,
	GROW_LOGICAL_PHASE_END,
	GROW_LOGICAL_PHASE_ERROR,
	GROW_PHYSICAL_PHASE_START,
	GROW_PHYSICAL_PHASE_COPY_SUMMARY,
	GROW_PHYSICAL_PHASE_UPDATE_COMPONENTS,
	GROW_PHYSICAL_PHASE_USE_NEW_SLABS,
	GROW_PHYSICAL_PHASE_END,
	GROW_PHYSICAL_PHASE_ERROR,
	LOAD_PHASE_START,
	LOAD_PHASE_LOAD_DEPOT,
	LOAD_PHASE_MAKE_DIRTY,
	LOAD_PHASE_PREPARE_TO_ALLOCATE,
	LOAD_PHASE_SCRUB_SLABS,
	LOAD_PHASE_DATA_REDUCTION,
	LOAD_PHASE_FINISHED,
	LOAD_PHASE_DRAIN_JOURNAL,
	LOAD_PHASE_WAIT_FOR_READ_ONLY,
	PRE_LOAD_PHASE_START,
	PRE_LOAD_PHASE_LOAD_COMPONENTS,
	PRE_LOAD_PHASE_END,
	PREPARE_GROW_PHYSICAL_PHASE_START,
	RESUME_PHASE_START,
	RESUME_PHASE_ALLOW_READ_ONLY_MODE,
	RESUME_PHASE_DEDUPE,
	RESUME_PHASE_DEPOT,
	RESUME_PHASE_JOURNAL,
	RESUME_PHASE_BLOCK_MAP,
	RESUME_PHASE_LOGICAL_ZONES,
	RESUME_PHASE_PACKER,
	RESUME_PHASE_FLUSHER,
	RESUME_PHASE_DATA_VIOS,
	RESUME_PHASE_END,
	SUSPEND_PHASE_START,
	SUSPEND_PHASE_PACKER,
	SUSPEND_PHASE_DATA_VIOS,
	SUSPEND_PHASE_DEDUPE,
	SUSPEND_PHASE_FLUSHES,
	SUSPEND_PHASE_LOGICAL_ZONES,
	SUSPEND_PHASE_BLOCK_MAP,
	SUSPEND_PHASE_JOURNAL,
	SUSPEND_PHASE_DEPOT,
	SUSPEND_PHASE_READ_ONLY_WAIT,
	SUSPEND_PHASE_WRITE_SUPER_BLOCK,
	SUSPEND_PHASE_END,
};

static const char * const ADMIN_PHASE_NAMES[] = {
	"GROW_LOGICAL_PHASE_START",
	"GROW_LOGICAL_PHASE_GROW_BLOCK_MAP",
	"GROW_LOGICAL_PHASE_END",
	"GROW_LOGICAL_PHASE_ERROR",
	"GROW_PHYSICAL_PHASE_START",
	"GROW_PHYSICAL_PHASE_COPY_SUMMARY",
	"GROW_PHYSICAL_PHASE_UPDATE_COMPONENTS",
	"GROW_PHYSICAL_PHASE_USE_NEW_SLABS",
	"GROW_PHYSICAL_PHASE_END",
	"GROW_PHYSICAL_PHASE_ERROR",
	"LOAD_PHASE_START",
	"LOAD_PHASE_LOAD_DEPOT",
	"LOAD_PHASE_MAKE_DIRTY",
	"LOAD_PHASE_PREPARE_TO_ALLOCATE",
	"LOAD_PHASE_SCRUB_SLABS",
	"LOAD_PHASE_DATA_REDUCTION",
	"LOAD_PHASE_FINISHED",
	"LOAD_PHASE_DRAIN_JOURNAL",
	"LOAD_PHASE_WAIT_FOR_READ_ONLY",
	"PRE_LOAD_PHASE_START",
	"PRE_LOAD_PHASE_LOAD_COMPONENTS",
	"PRE_LOAD_PHASE_END",
	"PREPARE_GROW_PHYSICAL_PHASE_START",
	"RESUME_PHASE_START",
	"RESUME_PHASE_ALLOW_READ_ONLY_MODE",
	"RESUME_PHASE_DEDUPE",
	"RESUME_PHASE_DEPOT",
	"RESUME_PHASE_JOURNAL",
	"RESUME_PHASE_BLOCK_MAP",
	"RESUME_PHASE_LOGICAL_ZONES",
	"RESUME_PHASE_PACKER",
	"RESUME_PHASE_FLUSHER",
	"RESUME_PHASE_DATA_VIOS",
	"RESUME_PHASE_END",
	"SUSPEND_PHASE_START",
	"SUSPEND_PHASE_PACKER",
	"SUSPEND_PHASE_DATA_VIOS",
	"SUSPEND_PHASE_DEDUPE",
	"SUSPEND_PHASE_FLUSHES",
	"SUSPEND_PHASE_LOGICAL_ZONES",
	"SUSPEND_PHASE_BLOCK_MAP",
	"SUSPEND_PHASE_JOURNAL",
	"SUSPEND_PHASE_DEPOT",
	"SUSPEND_PHASE_READ_ONLY_WAIT",
	"SUSPEND_PHASE_WRITE_SUPER_BLOCK",
	"SUSPEND_PHASE_END",
};

/* If we bump this, update the arrays below */
#define TABLE_VERSION 4

/* arrays for handling different table versions */
static const u8 REQUIRED_ARGC[] = { 10, 12, 9, 7, 6 };
/* pool name no longer used. only here for verification of older versions */
static const u8 POOL_NAME_ARG_INDEX[] = { 8, 10, 8 };

/*
 * Track in-use instance numbers using a flat bit array.
 *
 * O(n) run time isn't ideal, but if we have 1000 VDO devices in use simultaneously we still only
 * need to scan 16 words, so it's not likely to be a big deal compared to other resource usage.
 */

/*
 * This minimum size for the bit array creates a numbering space of 0-999, which allows
 * successive starts of the same volume to have different instance numbers in any
 * reasonably-sized test. Changing instances on restart allows vdoMonReport to detect that
 * the ephemeral stats have reset to zero.
 */
#define BIT_COUNT_MINIMUM 1000
/* Grow the bit array by this many bits when needed */
#define BIT_COUNT_INCREMENT 100

struct instance_tracker {
	unsigned int bit_count;
	unsigned long *words;
	unsigned int count;
	unsigned int next;
};

static DEFINE_MUTEX(instances_lock);
static struct instance_tracker instances;

/**
 * free_device_config() - Free a device config created by parse_device_config().
 * @config: The config to free.
 */
static void free_device_config(struct device_config *config)
{
	if (config == NULL)
		return;

	if (config->owned_device != NULL)
		dm_put_device(config->owning_target, config->owned_device);

	vdo_free(config->parent_device_name);
	vdo_free(config->original_string);

	/* Reduce the chance a use-after-free (as in BZ 1669960) happens to work. */
	memset(config, 0, sizeof(*config));
	vdo_free(config);
}

/**
 * get_version_number() - Decide the version number from argv.
 *
 * @argc: The number of table values.
 * @argv: The array of table values.
 * @error_ptr: A pointer to return a error string in.
 * @version_ptr: A pointer to return the version.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int get_version_number(int argc, char **argv, char **error_ptr,
			      unsigned int *version_ptr)
{
	/* version, if it exists, is in a form of V<n> */
	if (sscanf(argv[0], "V%u", version_ptr) == 1) {
		if (*version_ptr < 1 || *version_ptr > TABLE_VERSION) {
			*error_ptr = "Unknown version number detected";
			return VDO_BAD_CONFIGURATION;
		}
	} else {
		/* V0 actually has no version number in the table string */
		*version_ptr = 0;
	}

	/*
	 * V0 and V1 have no optional parameters. There will always be a parameter for thread
	 * config, even if it's a "." to show it's an empty list.
	 */
	if (*version_ptr <= 1) {
		if (argc != REQUIRED_ARGC[*version_ptr]) {
			*error_ptr = "Incorrect number of arguments for version";
			return VDO_BAD_CONFIGURATION;
		}
	} else if (argc < REQUIRED_ARGC[*version_ptr]) {
		*error_ptr = "Incorrect number of arguments for version";
		return VDO_BAD_CONFIGURATION;
	}

	if (*version_ptr != TABLE_VERSION) {
		vdo_log_warning("Detected version mismatch between kernel module and tools kernel: %d, tool: %d",
				TABLE_VERSION, *version_ptr);
		vdo_log_warning("Please consider upgrading management tools to match kernel.");
	}
	return VDO_SUCCESS;
}

/* Free a list of non-NULL string pointers, and then the list itself. */
static void free_string_array(char **string_array)
{
	unsigned int offset;

	for (offset = 0; string_array[offset] != NULL; offset++)
		vdo_free(string_array[offset]);
	vdo_free(string_array);
}

/*
 * Split the input string into substrings, separated at occurrences of the indicated character,
 * returning a null-terminated list of string pointers.
 *
 * The string pointers and the pointer array itself should both be freed with vdo_free() when no
 * longer needed. This can be done with vdo_free_string_array (below) if the pointers in the array
 * are not changed. Since the array and copied strings are allocated by this function, it may only
 * be used in contexts where allocation is permitted.
 *
 * Empty substrings are not ignored; that is, returned substrings may be empty strings if the
 * separator occurs twice in a row.
 */
static int split_string(const char *string, char separator, char ***substring_array_ptr)
{
	unsigned int current_substring = 0, substring_count = 1;
	const char *s;
	char **substrings;
	int result;
	ptrdiff_t length;

	for (s = string; *s != 0; s++) {
		if (*s == separator)
			substring_count++;
	}

	result = vdo_allocate(substring_count + 1, char *, "string-splitting array",
			      &substrings);
	if (result != VDO_SUCCESS)
		return result;

	for (s = string; *s != 0; s++) {
		if (*s == separator) {
			ptrdiff_t length = s - string;

			result = vdo_allocate(length + 1, char, "split string",
					      &substrings[current_substring]);
			if (result != VDO_SUCCESS) {
				free_string_array(substrings);
				return result;
			}
			/*
			 * Trailing NUL is already in place after allocation; deal with the zero or
			 * more non-NUL bytes in the string.
			 */
			if (length > 0)
				memcpy(substrings[current_substring], string, length);
			string = s + 1;
			current_substring++;
			BUG_ON(current_substring >= substring_count);
		}
	}
	/* Process final string, with no trailing separator. */
	BUG_ON(current_substring != (substring_count - 1));
	length = strlen(string);

	result = vdo_allocate(length + 1, char, "split string",
			      &substrings[current_substring]);
	if (result != VDO_SUCCESS) {
		free_string_array(substrings);
		return result;
	}
	memcpy(substrings[current_substring], string, length);
	current_substring++;
	/* substrings[current_substring] is NULL already */
	*substring_array_ptr = substrings;
	return VDO_SUCCESS;
}

/*
 * Join the input substrings into one string, joined with the indicated character, returning a
 * string. array_length is a bound on the number of valid elements in substring_array, in case it
 * is not NULL-terminated.
 */
static int join_strings(char **substring_array, size_t array_length, char separator,
			char **string_ptr)
{
	size_t string_length = 0;
	size_t i;
	int result;
	char *output, *current_position;

	for (i = 0; (i < array_length) && (substring_array[i] != NULL); i++)
		string_length += strlen(substring_array[i]) + 1;

	result = vdo_allocate(string_length, char, __func__, &output);
	if (result != VDO_SUCCESS)
		return result;

	current_position = &output[0];

	for (i = 0; (i < array_length) && (substring_array[i] != NULL); i++) {
		current_position = vdo_append_to_buffer(current_position,
							output + string_length, "%s",
							substring_array[i]);
		*current_position = separator;
		current_position++;
	}

	/* We output one too many separators; replace the last with a zero byte. */
	if (current_position != output)
		*(current_position - 1) = '\0';

	*string_ptr = output;
	return VDO_SUCCESS;
}

/**
 * parse_bool() - Parse a two-valued option into a bool.
 * @bool_str: The string value to convert to a bool.
 * @true_str: The string value which should be converted to true.
 * @false_str: The string value which should be converted to false.
 * @bool_ptr: A pointer to return the bool value in.
 *
 * Return: VDO_SUCCESS or an error if bool_str is neither true_str nor false_str.
 */
static inline int __must_check parse_bool(const char *bool_str, const char *true_str,
					  const char *false_str, bool *bool_ptr)
{
	bool value = false;

	if (strcmp(bool_str, true_str) == 0)
		value = true;
	else if (strcmp(bool_str, false_str) == 0)
		value = false;
	else
		return VDO_BAD_CONFIGURATION;

	*bool_ptr = value;
	return VDO_SUCCESS;
}

/**
 * process_one_thread_config_spec() - Process one component of a thread parameter configuration
 *				      string and update the configuration data structure.
 * @thread_param_type: The type of thread specified.
 * @count: The thread count requested.
 * @config: The configuration data structure to update.
 *
 * If the thread count requested is invalid, a message is logged and -EINVAL returned. If the
 * thread name is unknown, a message is logged but no error is returned.
 *
 * Return: VDO_SUCCESS or -EINVAL
 */
static int process_one_thread_config_spec(const char *thread_param_type,
					  unsigned int count,
					  struct thread_count_config *config)
{
	/* Handle limited thread parameters */
	if (strcmp(thread_param_type, "bioRotationInterval") == 0) {
		if (count == 0) {
			vdo_log_error("thread config string error:  'bioRotationInterval' of at least 1 is required");
			return -EINVAL;
		} else if (count > VDO_BIO_ROTATION_INTERVAL_LIMIT) {
			vdo_log_error("thread config string error: 'bioRotationInterval' cannot be higher than %d",
				      VDO_BIO_ROTATION_INTERVAL_LIMIT);
			return -EINVAL;
		}
		config->bio_rotation_interval = count;
		return VDO_SUCCESS;
	}
	if (strcmp(thread_param_type, "logical") == 0) {
		if (count > MAX_VDO_LOGICAL_ZONES) {
			vdo_log_error("thread config string error: at most %d 'logical' threads are allowed",
				      MAX_VDO_LOGICAL_ZONES);
			return -EINVAL;
		}
		config->logical_zones = count;
		return VDO_SUCCESS;
	}
	if (strcmp(thread_param_type, "physical") == 0) {
		if (count > MAX_VDO_PHYSICAL_ZONES) {
			vdo_log_error("thread config string error: at most %d 'physical' threads are allowed",
				      MAX_VDO_PHYSICAL_ZONES);
			return -EINVAL;
		}
		config->physical_zones = count;
		return VDO_SUCCESS;
	}
	/* Handle other thread count parameters */
	if (count > MAXIMUM_VDO_THREADS) {
		vdo_log_error("thread config string error: at most %d '%s' threads are allowed",
			      MAXIMUM_VDO_THREADS, thread_param_type);
		return -EINVAL;
	}
	if (strcmp(thread_param_type, "hash") == 0) {
		config->hash_zones = count;
		return VDO_SUCCESS;
	}
	if (strcmp(thread_param_type, "cpu") == 0) {
		if (count == 0) {
			vdo_log_error("thread config string error: at least one 'cpu' thread required");
			return -EINVAL;
		}
		config->cpu_threads = count;
		return VDO_SUCCESS;
	}
	if (strcmp(thread_param_type, "ack") == 0) {
		config->bio_ack_threads = count;
		return VDO_SUCCESS;
	}
	if (strcmp(thread_param_type, "bio") == 0) {
		if (count == 0) {
			vdo_log_error("thread config string error: at least one 'bio' thread required");
			return -EINVAL;
		}
		config->bio_threads = count;
		return VDO_SUCCESS;
	}

	/*
	 * Don't fail, just log. This will handle version mismatches between user mode tools and
	 * kernel.
	 */
	vdo_log_info("unknown thread parameter type \"%s\"", thread_param_type);
	return VDO_SUCCESS;
}

/**
 * parse_one_thread_config_spec() - Parse one component of a thread parameter configuration string
 *				    and update the configuration data structure.
 * @spec: The thread parameter specification string.
 * @config: The configuration data to be updated.
 */
static int parse_one_thread_config_spec(const char *spec,
					struct thread_count_config *config)
{
	unsigned int count;
	char **fields;
	int result;

	result = split_string(spec, '=', &fields);
	if (result != VDO_SUCCESS)
		return result;

	if ((fields[0] == NULL) || (fields[1] == NULL) || (fields[2] != NULL)) {
		vdo_log_error("thread config string error: expected thread parameter assignment, saw \"%s\"",
			      spec);
		free_string_array(fields);
		return -EINVAL;
	}

	result = kstrtouint(fields[1], 10, &count);
	if (result) {
		vdo_log_error("thread config string error: integer value needed, found \"%s\"",
			      fields[1]);
		free_string_array(fields);
		return result;
	}

	result = process_one_thread_config_spec(fields[0], count, config);
	free_string_array(fields);
	return result;
}

/**
 * parse_thread_config_string() - Parse the configuration string passed and update the specified
 *				  counts and other parameters of various types of threads to be
 *				  created.
 * @string: Thread parameter configuration string.
 * @config: The thread configuration data to update.
 *
 * The configuration string should contain one or more comma-separated specs of the form
 * "typename=number"; the supported type names are "cpu", "ack", "bio", "bioRotationInterval",
 * "logical", "physical", and "hash".
 *
 * If an error occurs during parsing of a single key/value pair, we deem it serious enough to stop
 * further parsing.
 *
 * This function can't set the "reason" value the caller wants to pass back, because we'd want to
 * format it to say which field was invalid, and we can't allocate the "reason" strings
 * dynamically. So if an error occurs, we'll log the details and pass back an error.
 *
 * Return: VDO_SUCCESS or -EINVAL or -ENOMEM
 */
static int parse_thread_config_string(const char *string,
				      struct thread_count_config *config)
{
	int result = VDO_SUCCESS;
	char **specs;

	if (strcmp(".", string) != 0) {
		unsigned int i;

		result = split_string(string, ',', &specs);
		if (result != VDO_SUCCESS)
			return result;

		for (i = 0; specs[i] != NULL; i++) {
			result = parse_one_thread_config_spec(specs[i], config);
			if (result != VDO_SUCCESS)
				break;
		}
		free_string_array(specs);
	}
	return result;
}

/**
 * process_one_key_value_pair() - Process one component of an optional parameter string and update
 *				  the configuration data structure.
 * @key: The optional parameter key name.
 * @value: The optional parameter value.
 * @config: The configuration data structure to update.
 *
 * If the value requested is invalid, a message is logged and -EINVAL returned. If the key is
 * unknown, a message is logged but no error is returned.
 *
 * Return: VDO_SUCCESS or -EINVAL
 */
static int process_one_key_value_pair(const char *key, unsigned int value,
				      struct device_config *config)
{
	/* Non thread optional parameters */
	if (strcmp(key, "maxDiscard") == 0) {
		if (value == 0) {
			vdo_log_error("optional parameter error: at least one max discard block required");
			return -EINVAL;
		}
		/* Max discard sectors in blkdev_issue_discard is UINT_MAX >> 9 */
		if (value > (UINT_MAX / VDO_BLOCK_SIZE)) {
			vdo_log_error("optional parameter error: at most %d max discard	 blocks are allowed",
				      UINT_MAX / VDO_BLOCK_SIZE);
			return -EINVAL;
		}
		config->max_discard_blocks = value;
		return VDO_SUCCESS;
	}
	/* Handles unknown key names */
	return process_one_thread_config_spec(key, value, &config->thread_counts);
}

/**
 * parse_one_key_value_pair() - Parse one key/value pair and update the configuration data
 *				structure.
 * @key: The optional key name.
 * @value: The optional value.
 * @config: The configuration data to be updated.
 *
 * Return: VDO_SUCCESS or error.
 */
static int parse_one_key_value_pair(const char *key, const char *value,
				    struct device_config *config)
{
	unsigned int count;
	int result;

	if (strcmp(key, "deduplication") == 0)
		return parse_bool(value, "on", "off", &config->deduplication);

	if (strcmp(key, "compression") == 0)
		return parse_bool(value, "on", "off", &config->compression);

	/* The remaining arguments must have integral values. */
	result = kstrtouint(value, 10, &count);
	if (result) {
		vdo_log_error("optional config string error: integer value needed, found \"%s\"",
			      value);
		return result;
	}
	return process_one_key_value_pair(key, count, config);
}

/**
 * parse_key_value_pairs() - Parse all key/value pairs from a list of arguments.
 * @argc: The total number of arguments in list.
 * @argv: The list of key/value pairs.
 * @config: The device configuration data to update.
 *
 * If an error occurs during parsing of a single key/value pair, we deem it serious enough to stop
 * further parsing.
 *
 * This function can't set the "reason" value the caller wants to pass back, because we'd want to
 * format it to say which field was invalid, and we can't allocate the "reason" strings
 * dynamically. So if an error occurs, we'll log the details and return the error.
 *
 * Return: VDO_SUCCESS or error
 */
static int parse_key_value_pairs(int argc, char **argv, struct device_config *config)
{
	int result = VDO_SUCCESS;

	while (argc) {
		result = parse_one_key_value_pair(argv[0], argv[1], config);
		if (result != VDO_SUCCESS)
			break;

		argc -= 2;
		argv += 2;
	}

	return result;
}

/**
 * parse_optional_arguments() - Parse the configuration string passed in for optional arguments.
 * @arg_set: The structure holding the arguments to parse.
 * @error_ptr: Pointer to a buffer to hold the error string.
 * @config: Pointer to device configuration data to update.
 *
 * For V0/V1 configurations, there will only be one optional parameter; the thread configuration.
 * The configuration string should contain one or more comma-separated specs of the form
 * "typename=number"; the supported type names are "cpu", "ack", "bio", "bioRotationInterval",
 * "logical", "physical", and "hash".
 *
 * For V2 configurations and beyond, there could be any number of arguments. They should contain
 * one or more key/value pairs separated by a space.
 *
 * Return: VDO_SUCCESS or error
 */
static int parse_optional_arguments(struct dm_arg_set *arg_set, char **error_ptr,
				    struct device_config *config)
{
	int result = VDO_SUCCESS;

	if (config->version == 0 || config->version == 1) {
		result = parse_thread_config_string(arg_set->argv[0],
						    &config->thread_counts);
		if (result != VDO_SUCCESS) {
			*error_ptr = "Invalid thread-count configuration";
			return VDO_BAD_CONFIGURATION;
		}
	} else {
		if ((arg_set->argc % 2) != 0) {
			*error_ptr = "Odd number of optional arguments given but they should be <key> <value> pairs";
			return VDO_BAD_CONFIGURATION;
		}
		result = parse_key_value_pairs(arg_set->argc, arg_set->argv, config);
		if (result != VDO_SUCCESS) {
			*error_ptr = "Invalid optional argument configuration";
			return VDO_BAD_CONFIGURATION;
		}
	}
	return result;
}

/**
 * handle_parse_error() - Handle a parsing error.
 * @config: The config to free.
 * @error_ptr: A place to store a constant string about the error.
 * @error_str: A constant string to store in error_ptr.
 */
static void handle_parse_error(struct device_config *config, char **error_ptr,
			       char *error_str)
{
	free_device_config(config);
	*error_ptr = error_str;
}

/**
 * parse_device_config() - Convert the dmsetup table into a struct device_config.
 * @argc: The number of table values.
 * @argv: The array of table values.
 * @ti: The target structure for this table.
 * @config_ptr: A pointer to return the allocated config.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int parse_device_config(int argc, char **argv, struct dm_target *ti,
			       struct device_config **config_ptr)
{
	bool enable_512e;
	size_t logical_bytes = to_bytes(ti->len);
	struct dm_arg_set arg_set;
	char **error_ptr = &ti->error;
	struct device_config *config = NULL;
	int result;

	if ((logical_bytes % VDO_BLOCK_SIZE) != 0) {
		handle_parse_error(config, error_ptr,
				   "Logical size must be a multiple of 4096");
		return VDO_BAD_CONFIGURATION;
	}

	if (argc == 0) {
		handle_parse_error(config, error_ptr, "Incorrect number of arguments");
		return VDO_BAD_CONFIGURATION;
	}

	result = vdo_allocate(1, struct device_config, "device_config", &config);
	if (result != VDO_SUCCESS) {
		handle_parse_error(config, error_ptr,
				   "Could not allocate config structure");
		return VDO_BAD_CONFIGURATION;
	}

	config->owning_target = ti;
	config->logical_blocks = logical_bytes / VDO_BLOCK_SIZE;
	INIT_LIST_HEAD(&config->config_list);

	/* Save the original string. */
	result = join_strings(argv, argc, ' ', &config->original_string);
	if (result != VDO_SUCCESS) {
		handle_parse_error(config, error_ptr, "Could not populate string");
		return VDO_BAD_CONFIGURATION;
	}

	vdo_log_info("table line: %s", config->original_string);

	config->thread_counts = (struct thread_count_config) {
		.bio_ack_threads = 1,
		.bio_threads = DEFAULT_VDO_BIO_SUBMIT_QUEUE_COUNT,
		.bio_rotation_interval = DEFAULT_VDO_BIO_SUBMIT_QUEUE_ROTATE_INTERVAL,
		.cpu_threads = 1,
		.logical_zones = 0,
		.physical_zones = 0,
		.hash_zones = 0,
	};
	config->max_discard_blocks = 1;
	config->deduplication = true;
	config->compression = false;

	arg_set.argc = argc;
	arg_set.argv = argv;

	result = get_version_number(argc, argv, error_ptr, &config->version);
	if (result != VDO_SUCCESS) {
		/* get_version_number sets error_ptr itself. */
		handle_parse_error(config, error_ptr, *error_ptr);
		return result;
	}
	/* Move the arg pointer forward only if the argument was there. */
	if (config->version >= 1)
		dm_shift_arg(&arg_set);

	result = vdo_duplicate_string(dm_shift_arg(&arg_set), "parent device name",
				      &config->parent_device_name);
	if (result != VDO_SUCCESS) {
		handle_parse_error(config, error_ptr,
				   "Could not copy parent device name");
		return VDO_BAD_CONFIGURATION;
	}

	/* Get the physical blocks, if known. */
	if (config->version >= 1) {
		result = kstrtoull(dm_shift_arg(&arg_set), 10, &config->physical_blocks);
		if (result != VDO_SUCCESS) {
			handle_parse_error(config, error_ptr,
					   "Invalid physical block count");
			return VDO_BAD_CONFIGURATION;
		}
	}

	/* Get the logical block size and validate */
	result = parse_bool(dm_shift_arg(&arg_set), "512", "4096", &enable_512e);
	if (result != VDO_SUCCESS) {
		handle_parse_error(config, error_ptr, "Invalid logical block size");
		return VDO_BAD_CONFIGURATION;
	}
	config->logical_block_size = (enable_512e ? 512 : 4096);

	/* Skip past the two no longer used read cache options. */
	if (config->version <= 1)
		dm_consume_args(&arg_set, 2);

	/* Get the page cache size. */
	result = kstrtouint(dm_shift_arg(&arg_set), 10, &config->cache_size);
	if (result != VDO_SUCCESS) {
		handle_parse_error(config, error_ptr,
				   "Invalid block map page cache size");
		return VDO_BAD_CONFIGURATION;
	}

	/* Get the block map era length. */
	result = kstrtouint(dm_shift_arg(&arg_set), 10, &config->block_map_maximum_age);
	if (result != VDO_SUCCESS) {
		handle_parse_error(config, error_ptr, "Invalid block map maximum age");
		return VDO_BAD_CONFIGURATION;
	}

	/* Skip past the no longer used MD RAID5 optimization mode */
	if (config->version <= 2)
		dm_consume_args(&arg_set, 1);

	/* Skip past the no longer used write policy setting */
	if (config->version <= 3)
		dm_consume_args(&arg_set, 1);

	/* Skip past the no longer used pool name for older table lines */
	if (config->version <= 2) {
		/*
		 * Make sure the enum to get the pool name from argv directly is still in sync with
		 * the parsing of the table line.
		 */
		if (&arg_set.argv[0] != &argv[POOL_NAME_ARG_INDEX[config->version]]) {
			handle_parse_error(config, error_ptr,
					   "Pool name not in expected location");
			return VDO_BAD_CONFIGURATION;
		}
		dm_shift_arg(&arg_set);
	}

	/* Get the optional arguments and validate. */
	result = parse_optional_arguments(&arg_set, error_ptr, config);
	if (result != VDO_SUCCESS) {
		/* parse_optional_arguments sets error_ptr itself. */
		handle_parse_error(config, error_ptr, *error_ptr);
		return result;
	}

	/*
	 * Logical, physical, and hash zone counts can all be zero; then we get one thread doing
	 * everything, our older configuration. If any zone count is non-zero, the others must be
	 * as well.
	 */
	if (((config->thread_counts.logical_zones == 0) !=
	     (config->thread_counts.physical_zones == 0)) ||
	    ((config->thread_counts.physical_zones == 0) !=
	     (config->thread_counts.hash_zones == 0))) {
		handle_parse_error(config, error_ptr,
				   "Logical, physical, and hash zones counts must all be zero or all non-zero");
		return VDO_BAD_CONFIGURATION;
	}

	if (config->cache_size <
	    (2 * MAXIMUM_VDO_USER_VIOS * config->thread_counts.logical_zones)) {
		handle_parse_error(config, error_ptr,
				   "Insufficient block map cache for logical zones");
		return VDO_BAD_CONFIGURATION;
	}

	result = dm_get_device(ti, config->parent_device_name,
			       dm_table_get_mode(ti->table), &config->owned_device);
	if (result != 0) {
		vdo_log_error("couldn't open device \"%s\": error %d",
			      config->parent_device_name, result);
		handle_parse_error(config, error_ptr, "Unable to open storage device");
		return VDO_BAD_CONFIGURATION;
	}

	if (config->version == 0) {
		u64 device_size = bdev_nr_bytes(config->owned_device->bdev);

		config->physical_blocks = device_size / VDO_BLOCK_SIZE;
	}

	*config_ptr = config;
	return result;
}

static struct vdo *get_vdo_for_target(struct dm_target *ti)
{
	return ((struct device_config *) ti->private)->vdo;
}


static int vdo_map_bio(struct dm_target *ti, struct bio *bio)
{
	struct vdo *vdo = get_vdo_for_target(ti);
	struct vdo_work_queue *current_work_queue;
	const struct admin_state_code *code = vdo_get_admin_state_code(&vdo->admin.state);

	VDO_ASSERT_LOG_ONLY(code->normal, "vdo should not receive bios while in state %s",
			    code->name);

	/* Count all incoming bios. */
	vdo_count_bios(&vdo->stats.bios_in, bio);


	/* Handle empty bios.  Empty flush bios are not associated with a vio. */
	if ((bio_op(bio) == REQ_OP_FLUSH) || ((bio->bi_opf & REQ_PREFLUSH) != 0)) {
		vdo_launch_flush(vdo, bio);
		return DM_MAPIO_SUBMITTED;
	}

	/* This could deadlock, */
	current_work_queue = vdo_get_current_work_queue();
	BUG_ON((current_work_queue != NULL) &&
	       (vdo == vdo_get_work_queue_owner(current_work_queue)->vdo));
	vdo_launch_bio(vdo->data_vio_pool, bio);
	return DM_MAPIO_SUBMITTED;
}

static void vdo_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct vdo *vdo = get_vdo_for_target(ti);

	limits->logical_block_size = vdo->device_config->logical_block_size;
	limits->physical_block_size = VDO_BLOCK_SIZE;

	/* The minimum io size for random io */
	limits->io_min = VDO_BLOCK_SIZE;
	/* The optimal io size for streamed/sequential io */
	limits->io_opt = VDO_BLOCK_SIZE;

	/*
	 * Sets the maximum discard size that will be passed into VDO. This value comes from a
	 * table line value passed in during dmsetup create.
	 *
	 * The value 1024 is the largest usable value on HD systems. A 2048 sector discard on a
	 * busy HD system takes 31 seconds. We should use a value no higher than 1024, which takes
	 * 15 to 16 seconds on a busy HD system. However, using large values results in 120 second
	 * blocked task warnings in kernel logs. In order to avoid these warnings, we choose to
	 * use the smallest reasonable value.
	 *
	 * The value is used by dm-thin to determine whether to pass down discards. The block layer
	 * splits large discards on this boundary when this is set.
	 */
	limits->max_hw_discard_sectors =
		(vdo->device_config->max_discard_blocks * VDO_SECTORS_PER_BLOCK);

	/*
	 * Force discards to not begin or end with a partial block by stating the granularity is
	 * 4k.
	 */
	limits->discard_granularity = VDO_BLOCK_SIZE;
}

static int vdo_iterate_devices(struct dm_target *ti, iterate_devices_callout_fn fn,
			       void *data)
{
	struct device_config *config = get_vdo_for_target(ti)->device_config;

	return fn(ti, config->owned_device, 0,
		  config->physical_blocks * VDO_SECTORS_PER_BLOCK, data);
}

/*
 * Status line is:
 *    <device> <operating mode> <in recovery> <index state> <compression state>
 *    <used physical blocks> <total physical blocks>
 */

static void vdo_status(struct dm_target *ti, status_type_t status_type,
		       unsigned int status_flags, char *result, unsigned int maxlen)
{
	struct vdo *vdo = get_vdo_for_target(ti);
	struct vdo_statistics *stats;
	struct device_config *device_config;
	/* N.B.: The DMEMIT macro uses the variables named "sz", "result", "maxlen". */
	int sz = 0;

	switch (status_type) {
	case STATUSTYPE_INFO:
		/* Report info for dmsetup status */
		mutex_lock(&vdo->stats_mutex);
		vdo_fetch_statistics(vdo, &vdo->stats_buffer);
		stats = &vdo->stats_buffer;

		DMEMIT("/dev/%pg %s %s %s %s %llu %llu",
		       vdo_get_backing_device(vdo), stats->mode,
		       stats->in_recovery_mode ? "recovering" : "-",
		       vdo_get_dedupe_index_state_name(vdo->hash_zones),
		       vdo_get_compressing(vdo) ? "online" : "offline",
		       stats->data_blocks_used + stats->overhead_blocks_used,
		       stats->physical_blocks);
		mutex_unlock(&vdo->stats_mutex);
		break;

	case STATUSTYPE_TABLE:
		/* Report the string actually specified in the beginning. */
		device_config = (struct device_config *) ti->private;
		DMEMIT("%s", device_config->original_string);
		break;

	case STATUSTYPE_IMA:
		/* FIXME: We ought to be more detailed here, but this is what thin does. */
		*result = '\0';
		break;
	}
}

static block_count_t __must_check get_underlying_device_block_count(const struct vdo *vdo)
{
	return bdev_nr_bytes(vdo_get_backing_device(vdo)) / VDO_BLOCK_SIZE;
}

static int __must_check process_vdo_message_locked(struct vdo *vdo, unsigned int argc,
						   char **argv)
{
	if ((argc == 2) && (strcasecmp(argv[0], "compression") == 0)) {
		if (strcasecmp(argv[1], "on") == 0) {
			vdo_set_compressing(vdo, true);
			return 0;
		}

		if (strcasecmp(argv[1], "off") == 0) {
			vdo_set_compressing(vdo, false);
			return 0;
		}

		vdo_log_warning("invalid argument '%s' to dmsetup compression message",
				argv[1]);
		return -EINVAL;
	}

	vdo_log_warning("unrecognized dmsetup message '%s' received", argv[0]);
	return -EINVAL;
}

/*
 * If the message is a dump, just do it. Otherwise, check that no other message is being processed,
 * and only proceed if so.
 * Returns -EBUSY if another message is being processed
 */
static int __must_check process_vdo_message(struct vdo *vdo, unsigned int argc,
					    char **argv)
{
	int result;

	/*
	 * All messages which may be processed in parallel with other messages should be handled
	 * here before the atomic check below. Messages which should be exclusive should be
	 * processed in process_vdo_message_locked().
	 */

	/* Dump messages should always be processed */
	if (strcasecmp(argv[0], "dump") == 0)
		return vdo_dump(vdo, argc, argv, "dmsetup message");

	if (argc == 1) {
		if (strcasecmp(argv[0], "dump-on-shutdown") == 0) {
			vdo->dump_on_shutdown = true;
			return 0;
		}

		/* Index messages should always be processed */
		if ((strcasecmp(argv[0], "index-close") == 0) ||
		    (strcasecmp(argv[0], "index-create") == 0) ||
		    (strcasecmp(argv[0], "index-disable") == 0) ||
		    (strcasecmp(argv[0], "index-enable") == 0))
			return vdo_message_dedupe_index(vdo->hash_zones, argv[0]);
	}

	if (atomic_cmpxchg(&vdo->processing_message, 0, 1) != 0)
		return -EBUSY;

	result = process_vdo_message_locked(vdo, argc, argv);

	/* Pairs with the implicit barrier in cmpxchg just above */
	smp_wmb();
	atomic_set(&vdo->processing_message, 0);
	return result;
}

static int vdo_message(struct dm_target *ti, unsigned int argc, char **argv,
		       char *result_buffer, unsigned int maxlen)
{
	struct registered_thread allocating_thread, instance_thread;
	struct vdo *vdo;
	int result;

	if (argc == 0) {
		vdo_log_warning("unspecified dmsetup message");
		return -EINVAL;
	}

	vdo = get_vdo_for_target(ti);
	vdo_register_allocating_thread(&allocating_thread, NULL);
	vdo_register_thread_device_id(&instance_thread, &vdo->instance);

	/*
	 * Must be done here so we don't map return codes. The code in dm-ioctl expects a 1 for a
	 * return code to look at the buffer and see if it is full or not.
	 */
	if ((argc == 1) && (strcasecmp(argv[0], "stats") == 0)) {
		vdo_write_stats(vdo, result_buffer, maxlen);
		result = 1;
	} else {
		result = vdo_status_to_errno(process_vdo_message(vdo, argc, argv));
	}

	vdo_unregister_thread_device_id();
	vdo_unregister_allocating_thread();
	return result;
}

static void configure_target_capabilities(struct dm_target *ti)
{
	ti->discards_supported = 1;
	ti->flush_supported = true;
	ti->num_discard_bios = 1;
	ti->num_flush_bios = 1;

	/*
	 * If this value changes, please make sure to update the value for max_discard_sectors
	 * accordingly.
	 */
	BUG_ON(dm_set_target_max_io_len(ti, VDO_SECTORS_PER_BLOCK) != 0);
}

/*
 * Implements vdo_filter_fn.
 */
static bool vdo_uses_device(struct vdo *vdo, const void *context)
{
	const struct device_config *config = context;

	return vdo_get_backing_device(vdo)->bd_dev == config->owned_device->bdev->bd_dev;
}

/**
 * get_thread_id_for_phase() - Get the thread id for the current phase of the admin operation in
 *                             progress.
 */
static thread_id_t __must_check get_thread_id_for_phase(struct vdo *vdo)
{
	switch (vdo->admin.phase) {
	case RESUME_PHASE_PACKER:
	case RESUME_PHASE_FLUSHER:
	case SUSPEND_PHASE_PACKER:
	case SUSPEND_PHASE_FLUSHES:
		return vdo->thread_config.packer_thread;

	case RESUME_PHASE_DATA_VIOS:
	case SUSPEND_PHASE_DATA_VIOS:
		return vdo->thread_config.cpu_thread;

	case LOAD_PHASE_DRAIN_JOURNAL:
	case RESUME_PHASE_JOURNAL:
	case SUSPEND_PHASE_JOURNAL:
		return vdo->thread_config.journal_thread;

	default:
		return vdo->thread_config.admin_thread;
	}
}

static struct vdo_completion *prepare_admin_completion(struct vdo *vdo,
						       vdo_action_fn callback,
						       vdo_action_fn error_handler)
{
	struct vdo_completion *completion = &vdo->admin.completion;

	/*
	 * We can't use vdo_prepare_completion_for_requeue() here because we don't want to reset
	 * any error in the completion.
	 */
	completion->callback = callback;
	completion->error_handler = error_handler;
	completion->callback_thread_id = get_thread_id_for_phase(vdo);
	completion->requeue = true;
	return completion;
}

/**
 * advance_phase() - Increment the phase of the current admin operation and prepare the admin
 *                   completion to run on the thread for the next phase.
 * @vdo: The on which an admin operation is being performed
 *
 * Return: The current phase
 */
static u32 advance_phase(struct vdo *vdo)
{
	u32 phase = vdo->admin.phase++;

	vdo->admin.completion.callback_thread_id = get_thread_id_for_phase(vdo);
	vdo->admin.completion.requeue = true;
	return phase;
}

/*
 * Perform an administrative operation (load, suspend, grow logical, or grow physical). This method
 * should not be called from vdo threads.
 */
static int perform_admin_operation(struct vdo *vdo, u32 starting_phase,
				   vdo_action_fn callback, vdo_action_fn error_handler,
				   const char *type)
{
	int result;
	struct vdo_administrator *admin = &vdo->admin;

	if (atomic_cmpxchg(&admin->busy, 0, 1) != 0) {
		return vdo_log_error_strerror(VDO_COMPONENT_BUSY,
					      "Can't start %s operation, another operation is already in progress",
					      type);
	}

	admin->phase = starting_phase;
	reinit_completion(&admin->callback_sync);
	vdo_reset_completion(&admin->completion);
	vdo_launch_completion(prepare_admin_completion(vdo, callback, error_handler));

	/*
	 * Using the "interruptible" interface means that Linux will not log a message when we wait
	 * for more than 120 seconds.
	 */
	while (wait_for_completion_interruptible(&admin->callback_sync)) {
		/* However, if we get a signal in a user-mode process, we could spin... */
		fsleep(1000);
	}

	result = admin->completion.result;
	/* pairs with implicit barrier in cmpxchg above */
	smp_wmb();
	atomic_set(&admin->busy, 0);
	return result;
}

/* Assert that we are operating on the correct thread for the current phase. */
static void assert_admin_phase_thread(struct vdo *vdo, const char *what)
{
	VDO_ASSERT_LOG_ONLY(vdo_get_callback_thread_id() == get_thread_id_for_phase(vdo),
			    "%s on correct thread for %s", what,
			    ADMIN_PHASE_NAMES[vdo->admin.phase]);
}

/**
 * finish_operation_callback() - Callback to finish an admin operation.
 * @completion: The admin_completion.
 */
static void finish_operation_callback(struct vdo_completion *completion)
{
	struct vdo_administrator *admin = &completion->vdo->admin;

	vdo_finish_operation(&admin->state, completion->result);
	complete(&admin->callback_sync);
}

/**
 * decode_from_super_block() - Decode the VDO state from the super block and validate that it is
 *                             correct.
 * @vdo: The vdo being loaded.
 *
 * On error from this method, the component states must be destroyed explicitly. If this method
 * returns successfully, the component states must not be destroyed.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int __must_check decode_from_super_block(struct vdo *vdo)
{
	const struct device_config *config = vdo->device_config;
	int result;

	result = vdo_decode_component_states(vdo->super_block.buffer, &vdo->geometry,
					     &vdo->states);
	if (result != VDO_SUCCESS)
		return result;

	vdo_set_state(vdo, vdo->states.vdo.state);
	vdo->load_state = vdo->states.vdo.state;

	/*
	 * If the device config specifies a larger logical size than was recorded in the super
	 * block, just accept it.
	 */
	if (vdo->states.vdo.config.logical_blocks < config->logical_blocks) {
		vdo_log_warning("Growing logical size: a logical size of %llu blocks was specified, but that differs from the %llu blocks configured in the vdo super block",
				(unsigned long long) config->logical_blocks,
				(unsigned long long) vdo->states.vdo.config.logical_blocks);
		vdo->states.vdo.config.logical_blocks = config->logical_blocks;
	}

	result = vdo_validate_component_states(&vdo->states, vdo->geometry.nonce,
					       config->physical_blocks,
					       config->logical_blocks);
	if (result != VDO_SUCCESS)
		return result;

	vdo->layout = vdo->states.layout;
	return VDO_SUCCESS;
}

/**
 * decode_vdo() - Decode the component data portion of a super block and fill in the corresponding
 *                portions of the vdo being loaded.
 * @vdo: The vdo being loaded.
 *
 * This will also allocate the recovery journal and slab depot. If this method is called with an
 * asynchronous layer (i.e. a thread config which specifies at least one base thread), the block
 * map and packer will be constructed as well.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int __must_check decode_vdo(struct vdo *vdo)
{
	block_count_t maximum_age, journal_length;
	struct partition *partition;
	int result;

	result = decode_from_super_block(vdo);
	if (result != VDO_SUCCESS) {
		vdo_destroy_component_states(&vdo->states);
		return result;
	}

	maximum_age = vdo_convert_maximum_age(vdo->device_config->block_map_maximum_age);
	journal_length =
		vdo_get_recovery_journal_length(vdo->states.vdo.config.recovery_journal_size);
	if (maximum_age > (journal_length / 2)) {
		return vdo_log_error_strerror(VDO_BAD_CONFIGURATION,
					      "maximum age: %llu exceeds limit %llu",
					      (unsigned long long) maximum_age,
					      (unsigned long long) (journal_length / 2));
	}

	if (maximum_age == 0) {
		return vdo_log_error_strerror(VDO_BAD_CONFIGURATION,
					      "maximum age must be greater than 0");
	}

	result = vdo_enable_read_only_entry(vdo);
	if (result != VDO_SUCCESS)
		return result;

	partition = vdo_get_known_partition(&vdo->layout,
					    VDO_RECOVERY_JOURNAL_PARTITION);
	result = vdo_decode_recovery_journal(vdo->states.recovery_journal,
					     vdo->states.vdo.nonce, vdo, partition,
					     vdo->states.vdo.complete_recoveries,
					     vdo->states.vdo.config.recovery_journal_size,
					     &vdo->recovery_journal);
	if (result != VDO_SUCCESS)
		return result;

	partition = vdo_get_known_partition(&vdo->layout, VDO_SLAB_SUMMARY_PARTITION);
	result = vdo_decode_slab_depot(vdo->states.slab_depot, vdo, partition,
				       &vdo->depot);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_decode_block_map(vdo->states.block_map,
				      vdo->states.vdo.config.logical_blocks, vdo,
				      vdo->recovery_journal, vdo->states.vdo.nonce,
				      vdo->device_config->cache_size, maximum_age,
				      &vdo->block_map);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_make_physical_zones(vdo, &vdo->physical_zones);
	if (result != VDO_SUCCESS)
		return result;

	/* The logical zones depend on the physical zones already existing. */
	result = vdo_make_logical_zones(vdo, &vdo->logical_zones);
	if (result != VDO_SUCCESS)
		return result;

	return vdo_make_hash_zones(vdo, &vdo->hash_zones);
}

/**
 * pre_load_callback() - Callback to initiate a pre-load, registered in vdo_initialize().
 * @completion: The admin completion.
 */
static void pre_load_callback(struct vdo_completion *completion)
{
	struct vdo *vdo = completion->vdo;
	int result;

	assert_admin_phase_thread(vdo, __func__);

	switch (advance_phase(vdo)) {
	case PRE_LOAD_PHASE_START:
		result = vdo_start_operation(&vdo->admin.state,
					     VDO_ADMIN_STATE_PRE_LOADING);
		if (result != VDO_SUCCESS) {
			vdo_continue_completion(completion, result);
			return;
		}

		vdo_load_super_block(vdo, completion);
		return;

	case PRE_LOAD_PHASE_LOAD_COMPONENTS:
		vdo_continue_completion(completion, decode_vdo(vdo));
		return;

	case PRE_LOAD_PHASE_END:
		break;

	default:
		vdo_set_completion_result(completion, UDS_BAD_STATE);
	}

	finish_operation_callback(completion);
}

static void release_instance(unsigned int instance)
{
	mutex_lock(&instances_lock);
	if (instance >= instances.bit_count) {
		VDO_ASSERT_LOG_ONLY(false,
				    "instance number %u must be less than bit count %u",
				    instance, instances.bit_count);
	} else if (test_bit(instance, instances.words) == 0) {
		VDO_ASSERT_LOG_ONLY(false, "instance number %u must be allocated", instance);
	} else {
		__clear_bit(instance, instances.words);
		instances.count -= 1;
	}
	mutex_unlock(&instances_lock);
}

static void set_device_config(struct dm_target *ti, struct vdo *vdo,
			      struct device_config *config)
{
	list_del_init(&config->config_list);
	list_add_tail(&config->config_list, &vdo->device_config_list);
	config->vdo = vdo;
	ti->private = config;
	configure_target_capabilities(ti);
}

static int vdo_initialize(struct dm_target *ti, unsigned int instance,
			  struct device_config *config)
{
	struct vdo *vdo;
	int result;
	u64 block_size = VDO_BLOCK_SIZE;
	u64 logical_size = to_bytes(ti->len);
	block_count_t logical_blocks = logical_size / block_size;

	vdo_log_info("loading device '%s'", vdo_get_device_name(ti));
	vdo_log_debug("Logical block size     = %llu", (u64) config->logical_block_size);
	vdo_log_debug("Logical blocks         = %llu", logical_blocks);
	vdo_log_debug("Physical block size    = %llu", (u64) block_size);
	vdo_log_debug("Physical blocks        = %llu", config->physical_blocks);
	vdo_log_debug("Block map cache blocks = %u", config->cache_size);
	vdo_log_debug("Block map maximum age  = %u", config->block_map_maximum_age);
	vdo_log_debug("Deduplication          = %s", (config->deduplication ? "on" : "off"));
	vdo_log_debug("Compression            = %s", (config->compression ? "on" : "off"));

	vdo = vdo_find_matching(vdo_uses_device, config);
	if (vdo != NULL) {
		vdo_log_error("Existing vdo already uses device %s",
			      vdo->device_config->parent_device_name);
		ti->error = "Cannot share storage device with already-running VDO";
		return VDO_BAD_CONFIGURATION;
	}

	result = vdo_make(instance, config, &ti->error, &vdo);
	if (result != VDO_SUCCESS) {
		vdo_log_error("Could not create VDO device. (VDO error %d, message %s)",
			      result, ti->error);
		vdo_destroy(vdo);
		return result;
	}

	result = perform_admin_operation(vdo, PRE_LOAD_PHASE_START, pre_load_callback,
					 finish_operation_callback, "pre-load");
	if (result != VDO_SUCCESS) {
		ti->error = ((result == VDO_INVALID_ADMIN_STATE) ?
			     "Pre-load is only valid immediately after initialization" :
			     "Cannot load metadata from device");
		vdo_log_error("Could not start VDO device. (VDO error %d, message %s)",
			      result, ti->error);
		vdo_destroy(vdo);
		return result;
	}

	set_device_config(ti, vdo, config);
	vdo->device_config = config;
	return VDO_SUCCESS;
}

/* Implements vdo_filter_fn. */
static bool __must_check vdo_is_named(struct vdo *vdo, const void *context)
{
	struct dm_target *ti = vdo->device_config->owning_target;
	const char *device_name = vdo_get_device_name(ti);

	return strcmp(device_name, context) == 0;
}

/**
 * get_bit_array_size() - Return the number of bytes needed to store a bit array of the specified
 *                        capacity in an array of unsigned longs.
 * @bit_count: The number of bits the array must hold.
 *
 * Return: the number of bytes needed for the array representation.
 */
static size_t get_bit_array_size(unsigned int bit_count)
{
	/* Round up to a multiple of the word size and convert to a byte count. */
	return (BITS_TO_LONGS(bit_count) * sizeof(unsigned long));
}

/**
 * grow_bit_array() - Re-allocate the bitmap word array so there will more instance numbers that
 *                    can be allocated.
 *
 * Since the array is initially NULL, this also initializes the array the first time we allocate an
 * instance number.
 *
 * Return: VDO_SUCCESS or an error code from the allocation
 */
static int grow_bit_array(void)
{
	unsigned int new_count = max(instances.bit_count + BIT_COUNT_INCREMENT,
				     (unsigned int) BIT_COUNT_MINIMUM);
	unsigned long *new_words;
	int result;

	result = vdo_reallocate_memory(instances.words,
				       get_bit_array_size(instances.bit_count),
				       get_bit_array_size(new_count),
				       "instance number bit array", &new_words);
	if (result != VDO_SUCCESS)
		return result;

	instances.bit_count = new_count;
	instances.words = new_words;
	return VDO_SUCCESS;
}

/**
 * allocate_instance() - Allocate an instance number.
 * @instance_ptr: A point to hold the instance number
 *
 * Return: VDO_SUCCESS or an error code
 *
 * This function must be called while holding the instances lock.
 */
static int allocate_instance(unsigned int *instance_ptr)
{
	unsigned int instance;
	int result;

	/* If there are no unallocated instances, grow the bit array. */
	if (instances.count >= instances.bit_count) {
		result = grow_bit_array();
		if (result != VDO_SUCCESS)
			return result;
	}

	/*
	 * There must be a zero bit somewhere now. Find it, starting just after the last instance
	 * allocated.
	 */
	instance = find_next_zero_bit(instances.words, instances.bit_count,
				      instances.next);
	if (instance >= instances.bit_count) {
		/* Nothing free after next, so wrap around to instance zero. */
		instance = find_first_zero_bit(instances.words, instances.bit_count);
		result = VDO_ASSERT(instance < instances.bit_count,
				    "impossibly, no zero bit found");
		if (result != VDO_SUCCESS)
			return result;
	}

	__set_bit(instance, instances.words);
	instances.count++;
	instances.next = instance + 1;
	*instance_ptr = instance;
	return VDO_SUCCESS;
}

static int construct_new_vdo_registered(struct dm_target *ti, unsigned int argc,
					char **argv, unsigned int instance)
{
	int result;
	struct device_config *config;

	result = parse_device_config(argc, argv, ti, &config);
	if (result != VDO_SUCCESS) {
		vdo_log_error_strerror(result, "parsing failed: %s", ti->error);
		release_instance(instance);
		return -EINVAL;
	}

	/* Beyond this point, the instance number will be cleaned up for us if needed */
	result = vdo_initialize(ti, instance, config);
	if (result != VDO_SUCCESS) {
		release_instance(instance);
		free_device_config(config);
		return vdo_status_to_errno(result);
	}

	return VDO_SUCCESS;
}

static int construct_new_vdo(struct dm_target *ti, unsigned int argc, char **argv)
{
	int result;
	unsigned int instance;
	struct registered_thread instance_thread;

	mutex_lock(&instances_lock);
	result = allocate_instance(&instance);
	mutex_unlock(&instances_lock);
	if (result != VDO_SUCCESS)
		return -ENOMEM;

	vdo_register_thread_device_id(&instance_thread, &instance);
	result = construct_new_vdo_registered(ti, argc, argv, instance);
	vdo_unregister_thread_device_id();
	return result;
}

/**
 * check_may_grow_physical() - Callback to check that we're not in recovery mode, used in
 *                             vdo_prepare_to_grow_physical().
 * @completion: The admin completion.
 */
static void check_may_grow_physical(struct vdo_completion *completion)
{
	struct vdo *vdo = completion->vdo;

	assert_admin_phase_thread(vdo, __func__);

	/* These checks can only be done from a vdo thread. */
	if (vdo_is_read_only(vdo))
		vdo_set_completion_result(completion, VDO_READ_ONLY);

	if (vdo_in_recovery_mode(vdo))
		vdo_set_completion_result(completion, VDO_RETRY_AFTER_REBUILD);

	finish_operation_callback(completion);
}

static block_count_t get_partition_size(struct layout *layout, enum partition_id id)
{
	return vdo_get_known_partition(layout, id)->count;
}

/**
 * grow_layout() - Make the layout for growing a vdo.
 * @vdo: The vdo preparing to grow.
 * @old_size: The current size of the vdo.
 * @new_size: The size to which the vdo will be grown.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int grow_layout(struct vdo *vdo, block_count_t old_size, block_count_t new_size)
{
	int result;
	block_count_t min_new_size;

	if (vdo->next_layout.size == new_size) {
		/* We are already prepared to grow to the new size, so we're done. */
		return VDO_SUCCESS;
	}

	/* Make a copy completion if there isn't one */
	if (vdo->partition_copier == NULL) {
		vdo->partition_copier = dm_kcopyd_client_create(NULL);
		if (IS_ERR(vdo->partition_copier)) {
			result = PTR_ERR(vdo->partition_copier);
			vdo->partition_copier = NULL;
			return result;
		}
	}

	/* Free any unused preparation. */
	vdo_uninitialize_layout(&vdo->next_layout);

	/*
	 * Make a new layout with the existing partition sizes for everything but the slab depot
	 * partition.
	 */
	result = vdo_initialize_layout(new_size, vdo->layout.start,
				       get_partition_size(&vdo->layout,
							  VDO_BLOCK_MAP_PARTITION),
				       get_partition_size(&vdo->layout,
							  VDO_RECOVERY_JOURNAL_PARTITION),
				       get_partition_size(&vdo->layout,
							  VDO_SLAB_SUMMARY_PARTITION),
				       &vdo->next_layout);
	if (result != VDO_SUCCESS) {
		dm_kcopyd_client_destroy(vdo_forget(vdo->partition_copier));
		return result;
	}

	/* Ensure the new journal and summary are entirely within the added blocks. */
	min_new_size = (old_size +
			get_partition_size(&vdo->next_layout,
					   VDO_SLAB_SUMMARY_PARTITION) +
			get_partition_size(&vdo->next_layout,
					   VDO_RECOVERY_JOURNAL_PARTITION));
	if (min_new_size > new_size) {
		/* Copying the journal and summary would destroy some old metadata. */
		vdo_uninitialize_layout(&vdo->next_layout);
		dm_kcopyd_client_destroy(vdo_forget(vdo->partition_copier));
		return VDO_INCREMENT_TOO_SMALL;
	}

	return VDO_SUCCESS;
}

static int prepare_to_grow_physical(struct vdo *vdo, block_count_t new_physical_blocks)
{
	int result;
	block_count_t current_physical_blocks = vdo->states.vdo.config.physical_blocks;

	vdo_log_info("Preparing to resize physical to %llu",
		     (unsigned long long) new_physical_blocks);
	VDO_ASSERT_LOG_ONLY((new_physical_blocks > current_physical_blocks),
			    "New physical size is larger than current physical size");
	result = perform_admin_operation(vdo, PREPARE_GROW_PHYSICAL_PHASE_START,
					 check_may_grow_physical,
					 finish_operation_callback,
					 "prepare grow-physical");
	if (result != VDO_SUCCESS)
		return result;

	result = grow_layout(vdo, current_physical_blocks, new_physical_blocks);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_prepare_to_grow_slab_depot(vdo->depot,
						vdo_get_known_partition(&vdo->next_layout,
									VDO_SLAB_DEPOT_PARTITION));
	if (result != VDO_SUCCESS) {
		vdo_uninitialize_layout(&vdo->next_layout);
		return result;
	}

	vdo_log_info("Done preparing to resize physical");
	return VDO_SUCCESS;
}

/**
 * validate_new_device_config() - Check whether a new device config represents a valid modification
 *				  to an existing config.
 * @to_validate: The new config to validate.
 * @config: The existing config.
 * @may_grow: Set to true if growing the logical and physical size of the vdo is currently
 *	      permitted.
 * @error_ptr: A pointer to hold the reason for any error.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int validate_new_device_config(struct device_config *to_validate,
				      struct device_config *config, bool may_grow,
				      char **error_ptr)
{
	if (to_validate->owning_target->begin != config->owning_target->begin) {
		*error_ptr = "Starting sector cannot change";
		return VDO_PARAMETER_MISMATCH;
	}

	if (to_validate->logical_block_size != config->logical_block_size) {
		*error_ptr = "Logical block size cannot change";
		return VDO_PARAMETER_MISMATCH;
	}

	if (to_validate->logical_blocks < config->logical_blocks) {
		*error_ptr = "Can't shrink VDO logical size";
		return VDO_PARAMETER_MISMATCH;
	}

	if (to_validate->cache_size != config->cache_size) {
		*error_ptr = "Block map cache size cannot change";
		return VDO_PARAMETER_MISMATCH;
	}

	if (to_validate->block_map_maximum_age != config->block_map_maximum_age) {
		*error_ptr = "Block map maximum age cannot change";
		return VDO_PARAMETER_MISMATCH;
	}

	if (memcmp(&to_validate->thread_counts, &config->thread_counts,
		   sizeof(struct thread_count_config)) != 0) {
		*error_ptr = "Thread configuration cannot change";
		return VDO_PARAMETER_MISMATCH;
	}

	if (to_validate->physical_blocks < config->physical_blocks) {
		*error_ptr = "Removing physical storage from a VDO is not supported";
		return VDO_NOT_IMPLEMENTED;
	}

	if (!may_grow && (to_validate->physical_blocks > config->physical_blocks)) {
		*error_ptr = "VDO physical size may not grow in current state";
		return VDO_NOT_IMPLEMENTED;
	}

	return VDO_SUCCESS;
}

static int prepare_to_modify(struct dm_target *ti, struct device_config *config,
			     struct vdo *vdo)
{
	int result;
	bool may_grow = (vdo_get_admin_state(vdo) != VDO_ADMIN_STATE_PRE_LOADED);

	result = validate_new_device_config(config, vdo->device_config, may_grow,
					    &ti->error);
	if (result != VDO_SUCCESS)
		return -EINVAL;

	if (config->logical_blocks > vdo->device_config->logical_blocks) {
		block_count_t logical_blocks = vdo->states.vdo.config.logical_blocks;

		vdo_log_info("Preparing to resize logical to %llu",
			     (unsigned long long) config->logical_blocks);
		VDO_ASSERT_LOG_ONLY((config->logical_blocks > logical_blocks),
				    "New logical size is larger than current size");

		result = vdo_prepare_to_grow_block_map(vdo->block_map,
						       config->logical_blocks);
		if (result != VDO_SUCCESS) {
			ti->error = "Device vdo_prepare_to_grow_logical failed";
			return result;
		}

		vdo_log_info("Done preparing to resize logical");
	}

	if (config->physical_blocks > vdo->device_config->physical_blocks) {
		result = prepare_to_grow_physical(vdo, config->physical_blocks);
		if (result != VDO_SUCCESS) {
			if (result == VDO_PARAMETER_MISMATCH) {
				/*
				 * If we don't trap this case, vdo_status_to_errno() will remap
				 * it to -EIO, which is misleading and ahistorical.
				 */
				result = -EINVAL;
			}

			if (result == VDO_TOO_MANY_SLABS)
				ti->error = "Device vdo_prepare_to_grow_physical failed (specified physical size too big based on formatted slab size)";
			else
				ti->error = "Device vdo_prepare_to_grow_physical failed";

			return result;
		}
	}

	if (strcmp(config->parent_device_name, vdo->device_config->parent_device_name) != 0) {
		const char *device_name = vdo_get_device_name(config->owning_target);

		vdo_log_info("Updating backing device of %s from %s to %s", device_name,
			     vdo->device_config->parent_device_name,
			     config->parent_device_name);
	}

	return VDO_SUCCESS;
}

static int update_existing_vdo(const char *device_name, struct dm_target *ti,
			       unsigned int argc, char **argv, struct vdo *vdo)
{
	int result;
	struct device_config *config;

	result = parse_device_config(argc, argv, ti, &config);
	if (result != VDO_SUCCESS)
		return -EINVAL;

	vdo_log_info("preparing to modify device '%s'", device_name);
	result = prepare_to_modify(ti, config, vdo);
	if (result != VDO_SUCCESS) {
		free_device_config(config);
		return vdo_status_to_errno(result);
	}

	set_device_config(ti, vdo, config);
	return VDO_SUCCESS;
}

static int vdo_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	int result;
	struct registered_thread allocating_thread, instance_thread;
	const char *device_name;
	struct vdo *vdo;

	vdo_register_allocating_thread(&allocating_thread, NULL);
	device_name = vdo_get_device_name(ti);
	vdo = vdo_find_matching(vdo_is_named, device_name);
	if (vdo == NULL) {
		result = construct_new_vdo(ti, argc, argv);
	} else {
		vdo_register_thread_device_id(&instance_thread, &vdo->instance);
		result = update_existing_vdo(device_name, ti, argc, argv, vdo);
		vdo_unregister_thread_device_id();
	}

	vdo_unregister_allocating_thread();
	return result;
}

static void vdo_dtr(struct dm_target *ti)
{
	struct device_config *config = ti->private;
	struct vdo *vdo = vdo_forget(config->vdo);

	list_del_init(&config->config_list);
	if (list_empty(&vdo->device_config_list)) {
		const char *device_name;

		/* This was the last config referencing the VDO. Free it. */
		unsigned int instance = vdo->instance;
		struct registered_thread allocating_thread, instance_thread;

		vdo_register_thread_device_id(&instance_thread, &instance);
		vdo_register_allocating_thread(&allocating_thread, NULL);

		device_name = vdo_get_device_name(ti);
		vdo_log_info("stopping device '%s'", device_name);
		if (vdo->dump_on_shutdown)
			vdo_dump_all(vdo, "device shutdown");

		vdo_destroy(vdo_forget(vdo));
		vdo_log_info("device '%s' stopped", device_name);
		vdo_unregister_thread_device_id();
		vdo_unregister_allocating_thread();
		release_instance(instance);
	} else if (config == vdo->device_config) {
		/*
		 * The VDO still references this config. Give it a reference to a config that isn't
		 * being destroyed.
		 */
		vdo->device_config = list_first_entry(&vdo->device_config_list,
						      struct device_config, config_list);
	}

	free_device_config(config);
	ti->private = NULL;
}

static void vdo_presuspend(struct dm_target *ti)
{
	get_vdo_for_target(ti)->suspend_type =
		(dm_noflush_suspending(ti) ? VDO_ADMIN_STATE_SUSPENDING : VDO_ADMIN_STATE_SAVING);
}

/**
 * write_super_block_for_suspend() - Update the VDO state and save the super block.
 * @completion: The admin completion
 */
static void write_super_block_for_suspend(struct vdo_completion *completion)
{
	struct vdo *vdo = completion->vdo;

	switch (vdo_get_state(vdo)) {
	case VDO_DIRTY:
	case VDO_NEW:
		vdo_set_state(vdo, VDO_CLEAN);
		break;

	case VDO_CLEAN:
	case VDO_READ_ONLY_MODE:
	case VDO_FORCE_REBUILD:
	case VDO_RECOVERING:
	case VDO_REBUILD_FOR_UPGRADE:
		break;

	case VDO_REPLAYING:
	default:
		vdo_continue_completion(completion, UDS_BAD_STATE);
		return;
	}

	vdo_save_components(vdo, completion);
}

/**
 * suspend_callback() - Callback to initiate a suspend, registered in vdo_postsuspend().
 * @completion: The sub-task completion.
 */
static void suspend_callback(struct vdo_completion *completion)
{
	struct vdo *vdo = completion->vdo;
	struct admin_state *state = &vdo->admin.state;
	int result;

	assert_admin_phase_thread(vdo, __func__);

	switch (advance_phase(vdo)) {
	case SUSPEND_PHASE_START:
		if (vdo_get_admin_state_code(state)->quiescent) {
			/* Already suspended */
			break;
		}

		vdo_continue_completion(completion,
					vdo_start_operation(state, vdo->suspend_type));
		return;

	case SUSPEND_PHASE_PACKER:
		/*
		 * If the VDO was already resumed from a prior suspend while read-only, some of the
		 * components may not have been resumed. By setting a read-only error here, we
		 * guarantee that the result of this suspend will be VDO_READ_ONLY and not
		 * VDO_INVALID_ADMIN_STATE in that case.
		 */
		if (vdo_in_read_only_mode(vdo))
			vdo_set_completion_result(completion, VDO_READ_ONLY);

		vdo_drain_packer(vdo->packer, completion);
		return;

	case SUSPEND_PHASE_DATA_VIOS:
		drain_data_vio_pool(vdo->data_vio_pool, completion);
		return;

	case SUSPEND_PHASE_DEDUPE:
		vdo_drain_hash_zones(vdo->hash_zones, completion);
		return;

	case SUSPEND_PHASE_FLUSHES:
		vdo_drain_flusher(vdo->flusher, completion);
		return;

	case SUSPEND_PHASE_LOGICAL_ZONES:
		/*
		 * Attempt to flush all I/O before completing post suspend work. We believe a
		 * suspended device is expected to have persisted all data written before the
		 * suspend, even if it hasn't been flushed yet.
		 */
		result = vdo_synchronous_flush(vdo);
		if (result != VDO_SUCCESS)
			vdo_enter_read_only_mode(vdo, result);

		vdo_drain_logical_zones(vdo->logical_zones,
					vdo_get_admin_state_code(state), completion);
		return;

	case SUSPEND_PHASE_BLOCK_MAP:
		vdo_drain_block_map(vdo->block_map, vdo_get_admin_state_code(state),
				    completion);
		return;

	case SUSPEND_PHASE_JOURNAL:
		vdo_drain_recovery_journal(vdo->recovery_journal,
					   vdo_get_admin_state_code(state), completion);
		return;

	case SUSPEND_PHASE_DEPOT:
		vdo_drain_slab_depot(vdo->depot, vdo_get_admin_state_code(state),
				     completion);
		return;

	case SUSPEND_PHASE_READ_ONLY_WAIT:
		vdo_wait_until_not_entering_read_only_mode(completion);
		return;

	case SUSPEND_PHASE_WRITE_SUPER_BLOCK:
		if (vdo_is_state_suspending(state) || (completion->result != VDO_SUCCESS)) {
			/* If we didn't save the VDO or there was an error, we're done. */
			break;
		}

		write_super_block_for_suspend(completion);
		return;

	case SUSPEND_PHASE_END:
		break;

	default:
		vdo_set_completion_result(completion, UDS_BAD_STATE);
	}

	finish_operation_callback(completion);
}

static void vdo_postsuspend(struct dm_target *ti)
{
	struct vdo *vdo = get_vdo_for_target(ti);
	struct registered_thread instance_thread;
	const char *device_name;
	int result;

	vdo_register_thread_device_id(&instance_thread, &vdo->instance);
	device_name = vdo_get_device_name(vdo->device_config->owning_target);
	vdo_log_info("suspending device '%s'", device_name);

	/*
	 * It's important to note any error here does not actually stop device-mapper from
	 * suspending the device. All this work is done post suspend.
	 */
	result = perform_admin_operation(vdo, SUSPEND_PHASE_START, suspend_callback,
					 suspend_callback, "suspend");

	if ((result == VDO_SUCCESS) || (result == VDO_READ_ONLY)) {
		/*
		 * Treat VDO_READ_ONLY as a success since a read-only suspension still leaves the
		 * VDO suspended.
		 */
		vdo_log_info("device '%s' suspended", device_name);
	} else if (result == VDO_INVALID_ADMIN_STATE) {
		vdo_log_error("Suspend invoked while in unexpected state: %s",
			      vdo_get_admin_state(vdo)->name);
	} else {
		vdo_log_error_strerror(result, "Suspend of device '%s' failed",
				       device_name);
	}

	vdo_unregister_thread_device_id();
}

/**
 * was_new() - Check whether the vdo was new when it was loaded.
 * @vdo: The vdo to query.
 *
 * Return: true if the vdo was new.
 */
static bool was_new(const struct vdo *vdo)
{
	return (vdo->load_state == VDO_NEW);
}

/**
 * requires_repair() - Check whether a vdo requires recovery or rebuild.
 * @vdo: The vdo to query.
 *
 * Return: true if the vdo must be repaired.
 */
static bool __must_check requires_repair(const struct vdo *vdo)
{
	switch (vdo_get_state(vdo)) {
	case VDO_DIRTY:
	case VDO_FORCE_REBUILD:
	case VDO_REPLAYING:
	case VDO_REBUILD_FOR_UPGRADE:
		return true;

	default:
		return false;
	}
}

/**
 * get_load_type() - Determine how the slab depot was loaded.
 * @vdo: The vdo.
 *
 * Return: How the depot was loaded.
 */
static enum slab_depot_load_type get_load_type(struct vdo *vdo)
{
	if (vdo_state_requires_read_only_rebuild(vdo->load_state))
		return VDO_SLAB_DEPOT_REBUILD_LOAD;

	if (vdo_state_requires_recovery(vdo->load_state))
		return VDO_SLAB_DEPOT_RECOVERY_LOAD;

	return VDO_SLAB_DEPOT_NORMAL_LOAD;
}

/**
 * load_callback() - Callback to do the destructive parts of loading a VDO.
 * @completion: The sub-task completion.
 */
static void load_callback(struct vdo_completion *completion)
{
	struct vdo *vdo = completion->vdo;
	int result;

	assert_admin_phase_thread(vdo, __func__);

	switch (advance_phase(vdo)) {
	case LOAD_PHASE_START:
		result = vdo_start_operation(&vdo->admin.state, VDO_ADMIN_STATE_LOADING);
		if (result != VDO_SUCCESS) {
			vdo_continue_completion(completion, result);
			return;
		}

		/* Prepare the recovery journal for new entries. */
		vdo_open_recovery_journal(vdo->recovery_journal, vdo->depot,
					  vdo->block_map);
		vdo_allow_read_only_mode_entry(completion);
		return;

	case LOAD_PHASE_LOAD_DEPOT:
		vdo_set_dedupe_state_normal(vdo->hash_zones);
		if (vdo_is_read_only(vdo)) {
			/*
			 * In read-only mode we don't use the allocator and it may not even be
			 * readable, so don't bother trying to load it.
			 */
			vdo_set_completion_result(completion, VDO_READ_ONLY);
			break;
		}

		if (requires_repair(vdo)) {
			vdo_repair(completion);
			return;
		}

		vdo_load_slab_depot(vdo->depot,
				    (was_new(vdo) ? VDO_ADMIN_STATE_FORMATTING :
				     VDO_ADMIN_STATE_LOADING),
				    completion, NULL);
		return;

	case LOAD_PHASE_MAKE_DIRTY:
		vdo_set_state(vdo, VDO_DIRTY);
		vdo_save_components(vdo, completion);
		return;

	case LOAD_PHASE_PREPARE_TO_ALLOCATE:
		vdo_initialize_block_map_from_journal(vdo->block_map,
						      vdo->recovery_journal);
		vdo_prepare_slab_depot_to_allocate(vdo->depot, get_load_type(vdo),
						   completion);
		return;

	case LOAD_PHASE_SCRUB_SLABS:
		if (vdo_state_requires_recovery(vdo->load_state))
			vdo_enter_recovery_mode(vdo);

		vdo_scrub_all_unrecovered_slabs(vdo->depot, completion);
		return;

	case LOAD_PHASE_DATA_REDUCTION:
		WRITE_ONCE(vdo->compressing, vdo->device_config->compression);
		if (vdo->device_config->deduplication) {
			/*
			 * Don't try to load or rebuild the index first (and log scary error
			 * messages) if this is known to be a newly-formatted volume.
			 */
			vdo_start_dedupe_index(vdo->hash_zones, was_new(vdo));
		}

		vdo->allocations_allowed = false;
		fallthrough;

	case LOAD_PHASE_FINISHED:
		break;

	case LOAD_PHASE_DRAIN_JOURNAL:
		vdo_drain_recovery_journal(vdo->recovery_journal, VDO_ADMIN_STATE_SAVING,
					   completion);
		return;

	case LOAD_PHASE_WAIT_FOR_READ_ONLY:
		/* Avoid an infinite loop */
		completion->error_handler = NULL;
		vdo->admin.phase = LOAD_PHASE_FINISHED;
		vdo_wait_until_not_entering_read_only_mode(completion);
		return;

	default:
		vdo_set_completion_result(completion, UDS_BAD_STATE);
	}

	finish_operation_callback(completion);
}

/**
 * handle_load_error() - Handle an error during the load operation.
 * @completion: The admin completion.
 *
 * If at all possible, brings the vdo online in read-only mode. This handler is registered in
 * vdo_preresume_registered().
 */
static void handle_load_error(struct vdo_completion *completion)
{
	struct vdo *vdo = completion->vdo;

	if (vdo_requeue_completion_if_needed(completion,
					     vdo->thread_config.admin_thread))
		return;

	if (vdo_state_requires_read_only_rebuild(vdo->load_state) &&
	    (vdo->admin.phase == LOAD_PHASE_MAKE_DIRTY)) {
		vdo_log_error_strerror(completion->result, "aborting load");
		vdo->admin.phase = LOAD_PHASE_DRAIN_JOURNAL;
		load_callback(vdo_forget(completion));
		return;
	}

	vdo_log_error_strerror(completion->result,
			       "Entering read-only mode due to load error");
	vdo->admin.phase = LOAD_PHASE_WAIT_FOR_READ_ONLY;
	vdo_enter_read_only_mode(vdo, completion->result);
	completion->result = VDO_READ_ONLY;
	load_callback(completion);
}

/**
 * write_super_block_for_resume() - Update the VDO state and save the super block.
 * @completion: The admin completion
 */
static void write_super_block_for_resume(struct vdo_completion *completion)
{
	struct vdo *vdo = completion->vdo;

	switch (vdo_get_state(vdo)) {
	case VDO_CLEAN:
	case VDO_NEW:
		vdo_set_state(vdo, VDO_DIRTY);
		vdo_save_components(vdo, completion);
		return;

	case VDO_DIRTY:
	case VDO_READ_ONLY_MODE:
	case VDO_FORCE_REBUILD:
	case VDO_RECOVERING:
	case VDO_REBUILD_FOR_UPGRADE:
		/* No need to write the super block in these cases */
		vdo_launch_completion(completion);
		return;

	case VDO_REPLAYING:
	default:
		vdo_continue_completion(completion, UDS_BAD_STATE);
	}
}

/**
 * resume_callback() - Callback to resume a VDO.
 * @completion: The admin completion.
 */
static void resume_callback(struct vdo_completion *completion)
{
	struct vdo *vdo = completion->vdo;
	int result;

	assert_admin_phase_thread(vdo, __func__);

	switch (advance_phase(vdo)) {
	case RESUME_PHASE_START:
		result = vdo_start_operation(&vdo->admin.state,
					     VDO_ADMIN_STATE_RESUMING);
		if (result != VDO_SUCCESS) {
			vdo_continue_completion(completion, result);
			return;
		}

		write_super_block_for_resume(completion);
		return;

	case RESUME_PHASE_ALLOW_READ_ONLY_MODE:
		vdo_allow_read_only_mode_entry(completion);
		return;

	case RESUME_PHASE_DEDUPE:
		vdo_resume_hash_zones(vdo->hash_zones, completion);
		return;

	case RESUME_PHASE_DEPOT:
		vdo_resume_slab_depot(vdo->depot, completion);
		return;

	case RESUME_PHASE_JOURNAL:
		vdo_resume_recovery_journal(vdo->recovery_journal, completion);
		return;

	case RESUME_PHASE_BLOCK_MAP:
		vdo_resume_block_map(vdo->block_map, completion);
		return;

	case RESUME_PHASE_LOGICAL_ZONES:
		vdo_resume_logical_zones(vdo->logical_zones, completion);
		return;

	case RESUME_PHASE_PACKER:
	{
		bool was_enabled = vdo_get_compressing(vdo);
		bool enable = vdo->device_config->compression;

		if (enable != was_enabled)
			WRITE_ONCE(vdo->compressing, enable);
		vdo_log_info("compression is %s", (enable ? "enabled" : "disabled"));

		vdo_resume_packer(vdo->packer, completion);
		return;
	}

	case RESUME_PHASE_FLUSHER:
		vdo_resume_flusher(vdo->flusher, completion);
		return;

	case RESUME_PHASE_DATA_VIOS:
		resume_data_vio_pool(vdo->data_vio_pool, completion);
		return;

	case RESUME_PHASE_END:
		break;

	default:
		vdo_set_completion_result(completion, UDS_BAD_STATE);
	}

	finish_operation_callback(completion);
}

/**
 * grow_logical_callback() - Callback to initiate a grow logical.
 * @completion: The admin completion.
 *
 * Registered in perform_grow_logical().
 */
static void grow_logical_callback(struct vdo_completion *completion)
{
	struct vdo *vdo = completion->vdo;
	int result;

	assert_admin_phase_thread(vdo, __func__);

	switch (advance_phase(vdo)) {
	case GROW_LOGICAL_PHASE_START:
		if (vdo_is_read_only(vdo)) {
			vdo_log_error_strerror(VDO_READ_ONLY,
					       "Can't grow logical size of a read-only VDO");
			vdo_set_completion_result(completion, VDO_READ_ONLY);
			break;
		}

		result = vdo_start_operation(&vdo->admin.state,
					     VDO_ADMIN_STATE_SUSPENDED_OPERATION);
		if (result != VDO_SUCCESS) {
			vdo_continue_completion(completion, result);
			return;
		}

		vdo->states.vdo.config.logical_blocks = vdo->block_map->next_entry_count;
		vdo_save_components(vdo, completion);
		return;

	case GROW_LOGICAL_PHASE_GROW_BLOCK_MAP:
		vdo_grow_block_map(vdo->block_map, completion);
		return;

	case GROW_LOGICAL_PHASE_END:
		break;

	case GROW_LOGICAL_PHASE_ERROR:
		vdo_enter_read_only_mode(vdo, completion->result);
		break;

	default:
		vdo_set_completion_result(completion, UDS_BAD_STATE);
	}

	finish_operation_callback(completion);
}

/**
 * handle_logical_growth_error() - Handle an error during the grow physical process.
 * @completion: The admin completion.
 */
static void handle_logical_growth_error(struct vdo_completion *completion)
{
	struct vdo *vdo = completion->vdo;

	if (vdo->admin.phase == GROW_LOGICAL_PHASE_GROW_BLOCK_MAP) {
		/*
		 * We've failed to write the new size in the super block, so set our in memory
		 * config back to the old size.
		 */
		vdo->states.vdo.config.logical_blocks = vdo->block_map->entry_count;
		vdo_abandon_block_map_growth(vdo->block_map);
	}

	vdo->admin.phase = GROW_LOGICAL_PHASE_ERROR;
	grow_logical_callback(completion);
}

/**
 * perform_grow_logical() - Grow the logical size of the vdo.
 * @vdo: The vdo to grow.
 * @new_logical_blocks: The size to which the vdo should be grown.
 *
 * Context: This method may only be called when the vdo has been suspended and must not be called
 * from a base thread.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int perform_grow_logical(struct vdo *vdo, block_count_t new_logical_blocks)
{
	int result;

	if (vdo->device_config->logical_blocks == new_logical_blocks) {
		/*
		 * A table was loaded for which we prepared to grow, but a table without that
		 * growth was what we are resuming with.
		 */
		vdo_abandon_block_map_growth(vdo->block_map);
		return VDO_SUCCESS;
	}

	vdo_log_info("Resizing logical to %llu",
		     (unsigned long long) new_logical_blocks);
	if (vdo->block_map->next_entry_count != new_logical_blocks)
		return VDO_PARAMETER_MISMATCH;

	result = perform_admin_operation(vdo, GROW_LOGICAL_PHASE_START,
					 grow_logical_callback,
					 handle_logical_growth_error, "grow logical");
	if (result != VDO_SUCCESS)
		return result;

	vdo_log_info("Logical blocks now %llu", (unsigned long long) new_logical_blocks);
	return VDO_SUCCESS;
}

static void copy_callback(int read_err, unsigned long write_err, void *context)
{
	struct vdo_completion *completion = context;
	int result = (((read_err == 0) && (write_err == 0)) ? VDO_SUCCESS : -EIO);

	vdo_continue_completion(completion, result);
}

static void partition_to_region(struct partition *partition, struct vdo *vdo,
				struct dm_io_region *region)
{
	physical_block_number_t pbn = partition->offset - vdo->geometry.bio_offset;

	*region = (struct dm_io_region) {
		.bdev = vdo_get_backing_device(vdo),
		.sector = pbn * VDO_SECTORS_PER_BLOCK,
		.count = partition->count * VDO_SECTORS_PER_BLOCK,
	};
}

/**
 * copy_partition() - Copy a partition from the location specified in the current layout to that in
 *                    the next layout.
 * @vdo: The vdo preparing to grow.
 * @id: The ID of the partition to copy.
 * @parent: The completion to notify when the copy is complete.
 */
static void copy_partition(struct vdo *vdo, enum partition_id id,
			   struct vdo_completion *parent)
{
	struct dm_io_region read_region, write_regions[1];
	struct partition *from = vdo_get_known_partition(&vdo->layout, id);
	struct partition *to = vdo_get_known_partition(&vdo->next_layout, id);

	partition_to_region(from, vdo, &read_region);
	partition_to_region(to, vdo, &write_regions[0]);
	dm_kcopyd_copy(vdo->partition_copier, &read_region, 1, write_regions, 0,
		       copy_callback, parent);
}

/**
 * grow_physical_callback() - Callback to initiate a grow physical.
 * @completion: The admin completion.
 *
 * Registered in perform_grow_physical().
 */
static void grow_physical_callback(struct vdo_completion *completion)
{
	struct vdo *vdo = completion->vdo;
	int result;

	assert_admin_phase_thread(vdo, __func__);

	switch (advance_phase(vdo)) {
	case GROW_PHYSICAL_PHASE_START:
		if (vdo_is_read_only(vdo)) {
			vdo_log_error_strerror(VDO_READ_ONLY,
					       "Can't grow physical size of a read-only VDO");
			vdo_set_completion_result(completion, VDO_READ_ONLY);
			break;
		}

		result = vdo_start_operation(&vdo->admin.state,
					     VDO_ADMIN_STATE_SUSPENDED_OPERATION);
		if (result != VDO_SUCCESS) {
			vdo_continue_completion(completion, result);
			return;
		}

		/* Copy the journal into the new layout. */
		copy_partition(vdo, VDO_RECOVERY_JOURNAL_PARTITION, completion);
		return;

	case GROW_PHYSICAL_PHASE_COPY_SUMMARY:
		copy_partition(vdo, VDO_SLAB_SUMMARY_PARTITION, completion);
		return;

	case GROW_PHYSICAL_PHASE_UPDATE_COMPONENTS:
		vdo_uninitialize_layout(&vdo->layout);
		vdo->layout = vdo->next_layout;
		vdo_forget(vdo->next_layout.head);
		vdo->states.vdo.config.physical_blocks = vdo->layout.size;
		vdo_update_slab_depot_size(vdo->depot);
		vdo_save_components(vdo, completion);
		return;

	case GROW_PHYSICAL_PHASE_USE_NEW_SLABS:
		vdo_use_new_slabs(vdo->depot, completion);
		return;

	case GROW_PHYSICAL_PHASE_END:
		vdo->depot->summary_origin =
			vdo_get_known_partition(&vdo->layout,
						VDO_SLAB_SUMMARY_PARTITION)->offset;
		vdo->recovery_journal->origin =
			vdo_get_known_partition(&vdo->layout,
						VDO_RECOVERY_JOURNAL_PARTITION)->offset;
		break;

	case GROW_PHYSICAL_PHASE_ERROR:
		vdo_enter_read_only_mode(vdo, completion->result);
		break;

	default:
		vdo_set_completion_result(completion, UDS_BAD_STATE);
	}

	vdo_uninitialize_layout(&vdo->next_layout);
	finish_operation_callback(completion);
}

/**
 * handle_physical_growth_error() - Handle an error during the grow physical process.
 * @completion: The sub-task completion.
 */
static void handle_physical_growth_error(struct vdo_completion *completion)
{
	completion->vdo->admin.phase = GROW_PHYSICAL_PHASE_ERROR;
	grow_physical_callback(completion);
}

/**
 * perform_grow_physical() - Grow the physical size of the vdo.
 * @vdo: The vdo to resize.
 * @new_physical_blocks: The new physical size in blocks.
 *
 * Context: This method may only be called when the vdo has been suspended and must not be called
 * from a base thread.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int perform_grow_physical(struct vdo *vdo, block_count_t new_physical_blocks)
{
	int result;
	block_count_t new_depot_size, prepared_depot_size;
	block_count_t old_physical_blocks = vdo->states.vdo.config.physical_blocks;

	/* Skip any noop grows. */
	if (old_physical_blocks == new_physical_blocks)
		return VDO_SUCCESS;

	if (new_physical_blocks != vdo->next_layout.size) {
		/*
		 * Either the VDO isn't prepared to grow, or it was prepared to grow to a different
		 * size. Doing this check here relies on the fact that the call to this method is
		 * done under the dmsetup message lock.
		 */
		vdo_uninitialize_layout(&vdo->next_layout);
		vdo_abandon_new_slabs(vdo->depot);
		return VDO_PARAMETER_MISMATCH;
	}

	/* Validate that we are prepared to grow appropriately. */
	new_depot_size =
		vdo_get_known_partition(&vdo->next_layout, VDO_SLAB_DEPOT_PARTITION)->count;
	prepared_depot_size = (vdo->depot->new_slabs == NULL) ? 0 : vdo->depot->new_size;
	if (prepared_depot_size != new_depot_size)
		return VDO_PARAMETER_MISMATCH;

	result = perform_admin_operation(vdo, GROW_PHYSICAL_PHASE_START,
					 grow_physical_callback,
					 handle_physical_growth_error, "grow physical");
	if (result != VDO_SUCCESS)
		return result;

	vdo_log_info("Physical block count was %llu, now %llu",
		     (unsigned long long) old_physical_blocks,
		     (unsigned long long) new_physical_blocks);
	return VDO_SUCCESS;
}

/**
 * apply_new_vdo_configuration() - Attempt to make any configuration changes from the table being
 *                                 resumed.
 * @vdo: The vdo being resumed.
 * @config: The new device configuration derived from the table with which the vdo is being
 *          resumed.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int __must_check apply_new_vdo_configuration(struct vdo *vdo,
						    struct device_config *config)
{
	int result;

	result = perform_grow_logical(vdo, config->logical_blocks);
	if (result != VDO_SUCCESS) {
		vdo_log_error("grow logical operation failed, result = %d", result);
		return result;
	}

	result = perform_grow_physical(vdo, config->physical_blocks);
	if (result != VDO_SUCCESS)
		vdo_log_error("resize operation failed, result = %d", result);

	return result;
}

static int vdo_preresume_registered(struct dm_target *ti, struct vdo *vdo)
{
	struct device_config *config = ti->private;
	const char *device_name = vdo_get_device_name(ti);
	block_count_t backing_blocks;
	int result;

	backing_blocks = get_underlying_device_block_count(vdo);
	if (backing_blocks < config->physical_blocks) {
		/* FIXME: can this still happen? */
		vdo_log_error("resume of device '%s' failed: backing device has %llu blocks but VDO physical size is %llu blocks",
			      device_name, (unsigned long long) backing_blocks,
			      (unsigned long long) config->physical_blocks);
		return -EINVAL;
	}

	if (vdo_get_admin_state(vdo) == VDO_ADMIN_STATE_PRE_LOADED) {
		vdo_log_info("starting device '%s'", device_name);
		result = perform_admin_operation(vdo, LOAD_PHASE_START, load_callback,
						 handle_load_error, "load");
		if ((result != VDO_SUCCESS) && (result != VDO_READ_ONLY)) {
			/*
			 * Something has gone very wrong. Make sure everything has drained and
			 * leave the device in an unresumable state.
			 */
			vdo_log_error_strerror(result,
					       "Start failed, could not load VDO metadata");
			vdo->suspend_type = VDO_ADMIN_STATE_STOPPING;
			perform_admin_operation(vdo, SUSPEND_PHASE_START,
						suspend_callback, suspend_callback,
						"suspend");
			return result;
		}

		/* Even if the VDO is read-only, it is now able to handle read requests. */
		vdo_log_info("device '%s' started", device_name);
	}

	vdo_log_info("resuming device '%s'", device_name);

	/* If this fails, the VDO was not in a state to be resumed. This should never happen. */
	result = apply_new_vdo_configuration(vdo, config);
	BUG_ON(result == VDO_INVALID_ADMIN_STATE);

	/*
	 * Now that we've tried to modify the vdo, the new config *is* the config, whether the
	 * modifications worked or not.
	 */
	vdo->device_config = config;

	/*
	 * Any error here is highly unexpected and the state of the vdo is questionable, so we mark
	 * it read-only in memory. Because we are suspended, the read-only state will not be
	 * written to disk.
	 */
	if (result != VDO_SUCCESS) {
		vdo_log_error_strerror(result,
				       "Commit of modifications to device '%s' failed",
				       device_name);
		vdo_enter_read_only_mode(vdo, result);
		return result;
	}

	if (vdo_get_admin_state(vdo)->normal) {
		/* The VDO was just started, so we don't need to resume it. */
		return VDO_SUCCESS;
	}

	result = perform_admin_operation(vdo, RESUME_PHASE_START, resume_callback,
					 resume_callback, "resume");
	BUG_ON(result == VDO_INVALID_ADMIN_STATE);
	if (result == VDO_READ_ONLY) {
		/* Even if the vdo is read-only, it has still resumed. */
		result = VDO_SUCCESS;
	}

	if (result != VDO_SUCCESS)
		vdo_log_error("resume of device '%s' failed with error: %d", device_name,
			      result);

	return result;
}

static int vdo_preresume(struct dm_target *ti)
{
	struct registered_thread instance_thread;
	struct vdo *vdo = get_vdo_for_target(ti);
	int result;

	vdo_register_thread_device_id(&instance_thread, &vdo->instance);
	result = vdo_preresume_registered(ti, vdo);
	if ((result == VDO_PARAMETER_MISMATCH) || (result == VDO_INVALID_ADMIN_STATE))
		result = -EINVAL;
	vdo_unregister_thread_device_id();
	return vdo_status_to_errno(result);
}

static void vdo_resume(struct dm_target *ti)
{
	struct registered_thread instance_thread;

	vdo_register_thread_device_id(&instance_thread,
				      &get_vdo_for_target(ti)->instance);
	vdo_log_info("device '%s' resumed", vdo_get_device_name(ti));
	vdo_unregister_thread_device_id();
}

/*
 * If anything changes that affects how user tools will interact with vdo, update the version
 * number and make sure documentation about the change is complete so tools can properly update
 * their management code.
 */
static struct target_type vdo_target_bio = {
	.features = DM_TARGET_SINGLETON,
	.name = "vdo",
	.version = { 9, 0, 0 },
	.module = THIS_MODULE,
	.ctr = vdo_ctr,
	.dtr = vdo_dtr,
	.io_hints = vdo_io_hints,
	.iterate_devices = vdo_iterate_devices,
	.map = vdo_map_bio,
	.message = vdo_message,
	.status = vdo_status,
	.presuspend = vdo_presuspend,
	.postsuspend = vdo_postsuspend,
	.preresume = vdo_preresume,
	.resume = vdo_resume,
};

static bool dm_registered;

static void vdo_module_destroy(void)
{
	vdo_log_debug("unloading");

	if (dm_registered)
		dm_unregister_target(&vdo_target_bio);

	VDO_ASSERT_LOG_ONLY(instances.count == 0,
			    "should have no instance numbers still in use, but have %u",
			    instances.count);
	vdo_free(instances.words);
	memset(&instances, 0, sizeof(struct instance_tracker));
}

static int __init vdo_init(void)
{
	int result = 0;

	/* Memory tracking must be initialized first for accurate accounting. */
	vdo_memory_init();
	vdo_initialize_threads_mutex();
	vdo_initialize_thread_device_registry();
	vdo_initialize_device_registry_once();

	/* Add VDO errors to the set of errors registered by the indexer. */
	result = vdo_register_status_codes();
	if (result != VDO_SUCCESS) {
		vdo_log_error("vdo_register_status_codes failed %d", result);
		vdo_module_destroy();
		return result;
	}

	result = dm_register_target(&vdo_target_bio);
	if (result < 0) {
		vdo_log_error("dm_register_target failed %d", result);
		vdo_module_destroy();
		return result;
	}
	dm_registered = true;

	return result;
}

static void __exit vdo_exit(void)
{
	vdo_module_destroy();
	/* Memory tracking cleanup must be done last. */
	vdo_memory_exit();
}

module_init(vdo_init);
module_exit(vdo_exit);

module_param_named(log_level, vdo_log_level, uint, 0644);
MODULE_PARM_DESC(log_level, "Log level for log messages");

MODULE_DESCRIPTION(DM_NAME " target for transparent deduplication");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");
