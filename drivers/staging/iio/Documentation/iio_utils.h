/* IIO - useful set of util functionality
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <dirent.h>
#include <errno.h>

/* Made up value to limit allocation sizes */
#define IIO_MAX_NAME_LENGTH 30

#define FORMAT_SCAN_ELEMENTS_DIR "%s/scan_elements"
#define FORMAT_TYPE_FILE "%s_type"

const char *iio_dir = "/sys/bus/iio/devices/";

/**
 * iioutils_break_up_name() - extract generic name from full channel name
 * @full_name: the full channel name
 * @generic_name: the output generic channel name
 **/
inline int iioutils_break_up_name(const char *full_name,
				  char **generic_name)
{
	char *current;
	char *w, *r;
	char *working;
	current = strdup(full_name);
	working = strtok(current, "_\0");
	w = working;
	r = working;

	while (*r != '\0') {
		if (!isdigit(*r)) {
			*w = *r;
			w++;
		}
		r++;
	}
	*w = '\0';
	*generic_name = strdup(working);
	free(current);

	return 0;
}

/**
 * struct iio_channel_info - information about a given channel
 * @name: channel name
 * @generic_name: general name for channel type
 * @scale: scale factor to be applied for conversion to si units
 * @offset: offset to be applied for conversion to si units
 * @index: the channel index in the buffer output
 * @bytes: number of bytes occupied in buffer output
 * @mask: a bit mask for the raw output
 * @is_signed: is the raw value stored signed
 * @enabled: is this channel enabled
 **/
struct iio_channel_info {
	char *name;
	char *generic_name;
	float scale;
	float offset;
	unsigned index;
	unsigned bytes;
	unsigned bits_used;
	unsigned shift;
	uint64_t mask;
	unsigned be;
	unsigned is_signed;
	unsigned location;
};

/**
 * iioutils_get_type() - find and process _type attribute data
 * @is_signed: output whether channel is signed
 * @bytes: output how many bytes the channel storage occupies
 * @mask: output a bit mask for the raw data
 * @be: big endian
 * @device_dir: the iio device directory
 * @name: the channel name
 * @generic_name: the channel type name
 **/
inline int iioutils_get_type(unsigned *is_signed,
			     unsigned *bytes,
			     unsigned *bits_used,
			     unsigned *shift,
			     uint64_t *mask,
			     unsigned *be,
			     const char *device_dir,
			     const char *name,
			     const char *generic_name)
{
	FILE *sysfsfp;
	int ret;
	DIR *dp;
	char *scan_el_dir, *builtname, *builtname_generic, *filename = 0;
	char signchar, endianchar;
	unsigned padint;
	const struct dirent *ent;

	ret = asprintf(&scan_el_dir, FORMAT_SCAN_ELEMENTS_DIR, device_dir);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = asprintf(&builtname, FORMAT_TYPE_FILE, name);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_free_scan_el_dir;
	}
	ret = asprintf(&builtname_generic, FORMAT_TYPE_FILE, generic_name);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_free_builtname;
	}

	dp = opendir(scan_el_dir);
	if (dp == NULL) {
		ret = -errno;
		goto error_free_builtname_generic;
	}
	while (ent = readdir(dp), ent != NULL)
		/*
		 * Do we allow devices to override a generic name with
		 * a specific one?
		 */
		if ((strcmp(builtname, ent->d_name) == 0) ||
		    (strcmp(builtname_generic, ent->d_name) == 0)) {
			ret = asprintf(&filename,
				       "%s/%s", scan_el_dir, ent->d_name);
			if (ret < 0) {
				ret = -ENOMEM;
				goto error_closedir;
			}
			sysfsfp = fopen(filename, "r");
			if (sysfsfp == NULL) {
				printf("failed to open %s\n", filename);
				ret = -errno;
				goto error_free_filename;
			}

			ret = fscanf(sysfsfp,
				     "%ce:%c%u/%u>>%u",
				     &endianchar,
				     &signchar,
				     bits_used,
				     &padint, shift);
			if (ret < 0) {
				printf("failed to pass scan type description\n");
				ret = -errno;
				goto error_close_sysfsfp;
			}
			*be = (endianchar == 'b');
			*bytes = padint / 8;
			if (*bits_used == 64)
				*mask = ~0;
			else
				*mask = (1 << *bits_used) - 1;
			if (signchar == 's')
				*is_signed = 1;
			else
				*is_signed = 0;
			fclose(sysfsfp);
			free(filename);

			filename = 0;
			sysfsfp = 0;
		}
error_close_sysfsfp:
	if (sysfsfp)
		fclose(sysfsfp);
error_free_filename:
	if (filename)
		free(filename);
error_closedir:
	closedir(dp);
error_free_builtname_generic:
	free(builtname_generic);
error_free_builtname:
	free(builtname);
error_free_scan_el_dir:
	free(scan_el_dir);
error_ret:
	return ret;
}

inline int iioutils_get_param_float(float *output,
				    const char *param_name,
				    const char *device_dir,
				    const char *name,
				    const char *generic_name)
{
	FILE *sysfsfp;
	int ret;
	DIR *dp;
	char *builtname, *builtname_generic;
	char *filename = NULL;
	const struct dirent *ent;

	ret = asprintf(&builtname, "%s_%s", name, param_name);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = asprintf(&builtname_generic,
		       "%s_%s", generic_name, param_name);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_free_builtname;
	}
	dp = opendir(device_dir);
	if (dp == NULL) {
		ret = -errno;
		goto error_free_builtname_generic;
	}
	while (ent = readdir(dp), ent != NULL)
		if ((strcmp(builtname, ent->d_name) == 0) ||
		    (strcmp(builtname_generic, ent->d_name) == 0)) {
			ret = asprintf(&filename,
				       "%s/%s", device_dir, ent->d_name);
			if (ret < 0) {
				ret = -ENOMEM;
				goto error_closedir;
			}
			sysfsfp = fopen(filename, "r");
			if (!sysfsfp) {
				ret = -errno;
				goto error_free_filename;
			}
			fscanf(sysfsfp, "%f", output);
			break;
		}
error_free_filename:
	if (filename)
		free(filename);
error_closedir:
	closedir(dp);
error_free_builtname_generic:
	free(builtname_generic);
error_free_builtname:
	free(builtname);
error_ret:
	return ret;
}

/**
 * bsort_channel_array_by_index() - reorder so that the array is in index order
 *
 **/

inline void bsort_channel_array_by_index(struct iio_channel_info **ci_array,
					 int cnt)
{

	struct iio_channel_info temp;
	int x, y;

	for (x = 0; x < cnt; x++)
		for (y = 0; y < (cnt - 1); y++)
			if ((*ci_array)[y].index > (*ci_array)[y+1].index) {
				temp = (*ci_array)[y + 1];
				(*ci_array)[y + 1] = (*ci_array)[y];
				(*ci_array)[y] = temp;
			}
}

/**
 * build_channel_array() - function to figure out what channels are present
 * @device_dir: the IIO device directory in sysfs
 * @
 **/
inline int build_channel_array(const char *device_dir,
			      struct iio_channel_info **ci_array,
			      int *counter)
{
	DIR *dp;
	FILE *sysfsfp;
	int count, i;
	struct iio_channel_info *current;
	int ret;
	const struct dirent *ent;
	char *scan_el_dir;
	char *filename;

	*counter = 0;
	ret = asprintf(&scan_el_dir, FORMAT_SCAN_ELEMENTS_DIR, device_dir);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_ret;
	}
	dp = opendir(scan_el_dir);
	if (dp == NULL) {
		ret = -errno;
		goto error_free_name;
	}
	while (ent = readdir(dp), ent != NULL)
		if (strcmp(ent->d_name + strlen(ent->d_name) - strlen("_en"),
			   "_en") == 0) {
			ret = asprintf(&filename,
				       "%s/%s", scan_el_dir, ent->d_name);
			if (ret < 0) {
				ret = -ENOMEM;
				goto error_close_dir;
			}
			sysfsfp = fopen(filename, "r");
			if (sysfsfp == NULL) {
				ret = -errno;
				free(filename);
				goto error_close_dir;
			}
			fscanf(sysfsfp, "%u", &ret);
			if (ret == 1)
				(*counter)++;
			fclose(sysfsfp);
			free(filename);
		}
	*ci_array = malloc(sizeof(**ci_array) * (*counter));
	if (*ci_array == NULL) {
		ret = -ENOMEM;
		goto error_close_dir;
	}
	seekdir(dp, 0);
	count = 0;
	while (ent = readdir(dp), ent != NULL) {
		if (strcmp(ent->d_name + strlen(ent->d_name) - strlen("_en"),
			   "_en") == 0) {
			int current_enabled = 0;
			current = &(*ci_array)[count++];
			ret = asprintf(&filename,
				       "%s/%s", scan_el_dir, ent->d_name);
			if (ret < 0) {
				ret = -ENOMEM;
				/* decrement count to avoid freeing name */
				count--;
				goto error_cleanup_array;
			}
			sysfsfp = fopen(filename, "r");
			if (sysfsfp == NULL) {
				free(filename);
				ret = -errno;
				goto error_cleanup_array;
			}
			fscanf(sysfsfp, "%u", &current_enabled);
			fclose(sysfsfp);

			if (!current_enabled) {
				free(filename);
				count--;
				continue;
			}

			current->scale = 1.0;
			current->offset = 0;
			current->name = strndup(ent->d_name,
						strlen(ent->d_name) -
						strlen("_en"));
			if (current->name == NULL) {
				free(filename);
				ret = -ENOMEM;
				goto error_cleanup_array;
			}
			/* Get the generic and specific name elements */
			ret = iioutils_break_up_name(current->name,
						     &current->generic_name);
			if (ret) {
				free(filename);
				goto error_cleanup_array;
			}
			ret = asprintf(&filename,
				       "%s/%s_index",
				       scan_el_dir,
				       current->name);
			if (ret < 0) {
				free(filename);
				ret = -ENOMEM;
				goto error_cleanup_array;
			}
			sysfsfp = fopen(filename, "r");
			fscanf(sysfsfp, "%u", &current->index);
			fclose(sysfsfp);
			free(filename);
			/* Find the scale */
			ret = iioutils_get_param_float(&current->scale,
						       "scale",
						       device_dir,
						       current->name,
						       current->generic_name);
			if (ret < 0)
				goto error_cleanup_array;
			ret = iioutils_get_param_float(&current->offset,
						       "offset",
						       device_dir,
						       current->name,
						       current->generic_name);
			if (ret < 0)
				goto error_cleanup_array;
			ret = iioutils_get_type(&current->is_signed,
						&current->bytes,
						&current->bits_used,
						&current->shift,
						&current->mask,
						&current->be,
						device_dir,
						current->name,
						current->generic_name);
		}
	}

	closedir(dp);
	/* reorder so that the array is in index order */
	bsort_channel_array_by_index(ci_array, *counter);

	return 0;

error_cleanup_array:
	for (i = count - 1;  i >= 0; i--)
		free((*ci_array)[i].name);
	free(*ci_array);
error_close_dir:
	closedir(dp);
error_free_name:
	free(scan_el_dir);
error_ret:
	return ret;
}

/**
 * find_type_by_name() - function to match top level types by name
 * @name: top level type instance name
 * @type: the type of top level instance being sort
 *
 * Typical types this is used for are device and trigger.
 **/
inline int find_type_by_name(const char *name, const char *type)
{
	const struct dirent *ent;
	int number, numstrlen;

	FILE *nameFile;
	DIR *dp;
	char thisname[IIO_MAX_NAME_LENGTH];
	char *filename;

	dp = opendir(iio_dir);
	if (dp == NULL) {
		printf("No industrialio devices available\n");
		return -ENODEV;
	}

	while (ent = readdir(dp), ent != NULL) {
		if (strcmp(ent->d_name, ".") != 0 &&
			strcmp(ent->d_name, "..") != 0 &&
			strlen(ent->d_name) > strlen(type) &&
			strncmp(ent->d_name, type, strlen(type)) == 0) {
			numstrlen = sscanf(ent->d_name + strlen(type),
					   "%d",
					   &number);
			/* verify the next character is not a colon */
			if (strncmp(ent->d_name + strlen(type) + numstrlen,
					":",
					1) != 0) {
				filename = malloc(strlen(iio_dir)
						+ strlen(type)
						+ numstrlen
						+ 6);
				if (filename == NULL) {
					closedir(dp);
					return -ENOMEM;
				}
				sprintf(filename, "%s%s%d/name",
					iio_dir,
					type,
					number);
				nameFile = fopen(filename, "r");
				if (!nameFile) {
					free(filename);
					continue;
				}
				free(filename);
				fscanf(nameFile, "%s", thisname);
				fclose(nameFile);
				if (strcmp(name, thisname) == 0) {
					closedir(dp);
					return number;
				}
			}
		}
	}
	closedir(dp);
	return -ENODEV;
}

inline int _write_sysfs_int(char *filename, char *basedir, int val, int verify)
{
	int ret = 0;
	FILE *sysfsfp;
	int test;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL)
		return -ENOMEM;
	sprintf(temp, "%s/%s", basedir, filename);
	sysfsfp = fopen(temp, "w");
	if (sysfsfp == NULL) {
		printf("failed to open %s\n", temp);
		ret = -errno;
		goto error_free;
	}
	fprintf(sysfsfp, "%d", val);
	fclose(sysfsfp);
	if (verify) {
		sysfsfp = fopen(temp, "r");
		if (sysfsfp == NULL) {
			printf("failed to open %s\n", temp);
			ret = -errno;
			goto error_free;
		}
		fscanf(sysfsfp, "%d", &test);
		fclose(sysfsfp);
		if (test != val) {
			printf("Possible failure in int write %d to %s%s\n",
				val,
				basedir,
				filename);
			ret = -1;
		}
	}
error_free:
	free(temp);
	return ret;
}

int write_sysfs_int(char *filename, char *basedir, int val)
{
	return _write_sysfs_int(filename, basedir, val, 0);
}

int write_sysfs_int_and_verify(char *filename, char *basedir, int val)
{
	return _write_sysfs_int(filename, basedir, val, 1);
}

int _write_sysfs_string(char *filename, char *basedir, char *val, int verify)
{
	int ret = 0;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL) {
		printf("Memory allocation failed\n");
		return -ENOMEM;
	}
	sprintf(temp, "%s/%s", basedir, filename);
	sysfsfp = fopen(temp, "w");
	if (sysfsfp == NULL) {
		printf("Could not open %s\n", temp);
		ret = -errno;
		goto error_free;
	}
	fprintf(sysfsfp, "%s", val);
	fclose(sysfsfp);
	if (verify) {
		sysfsfp = fopen(temp, "r");
		if (sysfsfp == NULL) {
			printf("could not open file to verify\n");
			ret = -errno;
			goto error_free;
		}
		fscanf(sysfsfp, "%s", temp);
		fclose(sysfsfp);
		if (strcmp(temp, val) != 0) {
			printf("Possible failure in string write of %s "
				"Should be %s "
				"written to %s\%s\n",
				temp,
				val,
				basedir,
				filename);
			ret = -1;
		}
	}
error_free:
	free(temp);

	return ret;
}

/**
 * write_sysfs_string_and_verify() - string write, readback and verify
 * @filename: name of file to write to
 * @basedir: the sysfs directory in which the file is to be found
 * @val: the string to write
 **/
int write_sysfs_string_and_verify(char *filename, char *basedir, char *val)
{
	return _write_sysfs_string(filename, basedir, val, 1);
}

int write_sysfs_string(char *filename, char *basedir, char *val)
{
	return _write_sysfs_string(filename, basedir, val, 0);
}

int read_sysfs_posint(char *filename, char *basedir)
{
	int ret;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL) {
		printf("Memory allocation failed");
		return -ENOMEM;
	}
	sprintf(temp, "%s/%s", basedir, filename);
	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL) {
		ret = -errno;
		goto error_free;
	}
	fscanf(sysfsfp, "%d\n", &ret);
	fclose(sysfsfp);
error_free:
	free(temp);
	return ret;
}

int read_sysfs_float(char *filename, char *basedir, float *val)
{
	float ret = 0;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL) {
		printf("Memory allocation failed");
		return -ENOMEM;
	}
	sprintf(temp, "%s/%s", basedir, filename);
	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL) {
		ret = -errno;
		goto error_free;
	}
	fscanf(sysfsfp, "%f\n", val);
	fclose(sysfsfp);
error_free:
	free(temp);
	return ret;
}

read_sysfs_string(const char *filename, const char *basedir, char *str)
{
	float ret = 0;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL) {
		printf("Memory allocation failed");
		return -ENOMEM;
	}
	sprintf(temp, "%s/%s", basedir, filename);
	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL) {
		ret = -errno;
		goto error_free;
	}
	fscanf(sysfsfp, "%s\n", str);
	fclose(sysfsfp);
error_free:
	free(temp);
	return ret;
}
