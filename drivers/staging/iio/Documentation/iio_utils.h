/* IIO - useful set of util functionality
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#define IIO_EVENT_CODE_RING_50_FULL 200
#define IIO_EVENT_CODE_RING_75_FULL 201
#define IIO_EVENT_CODE_RING_100_FULL 202

struct iio_event_data {
	int id;
	__s64 timestamp;
};


inline char *find_ring_subelement(const char *directory, const char *subelement)
{
	DIR *dp;
	const struct dirent *ent;
	int pos;
	char temp[100];
	char *returnstring;
	dp = opendir(directory);
	if (dp == NULL) {
		printf("could not directory: %s\n", directory);
		return NULL;
	}
	while (ent = readdir(dp), ent != NULL) {
		if (strcmp(ent->d_name, ".") != 0 &&
		    strcmp(ent->d_name, "..") != 0)  {
			if (strncmp(ent->d_name, subelement, strlen(subelement)) == 0) {
				int length = sprintf(temp, "%s%s%s", directory, ent->d_name, "/");
				returnstring = malloc(length+1);
				strncpy(returnstring, temp, length+1);
				return returnstring;

			}
		}
	}
	return 0;
}


char *find_type_by_name(const char *name, const char *type)
{
	const char *iio_dir = "/sys/class/iio/";
	const struct dirent *ent;
	int cnt, pos, pos2;

	FILE *nameFile;
	DIR *dp;
	char thisname[100];
	char temp[100];

	char *returnstring = NULL;
	struct stat Stat;
	pos = sprintf(temp, "%s", iio_dir);
	dp = opendir(iio_dir);
	if (dp == NULL) {
		printf("No industrialio devices available");
		return NULL;
	}
	while (ent = readdir(dp), ent != NULL) {
		cnt++;
		/*reject . and .. */
		if (strcmp(ent->d_name, ".") != 0 &&
		    strcmp(ent->d_name, "..") != 0)  {
			/*make sure it isn't a trigger!*/
			if (strncmp(ent->d_name, type, strlen(type)) == 0) {
				/* build full path to new file */
				pos2 = pos + sprintf(temp + pos, "%s/", ent->d_name);
				sprintf(temp + pos2, "name");
				printf("search location %s\n", temp);
				nameFile = fopen(temp, "r");
				if (!nameFile) {
					sprintf(temp + pos2, "modalias", ent->d_name);
					nameFile = fopen(temp, "r");
					if (!nameFile) {
						printf("Failed to find a name for device\n");
						return NULL;
					}
				}
				fscanf(nameFile, "%s", thisname);
				if (strcmp(name, thisname) == 0) {
					returnstring = malloc(strlen(temp) + 1);
					sprintf(temp + pos2, "");
					strcpy(returnstring, temp);
					return returnstring;
				}
				fclose(nameFile);

			}
		}
	}
}

int write_sysfs_int(char *filename, char *basedir, int val)
{
	int ret;
	FILE  *sysfsfp;
	char temp[100];
	sprintf(temp, "%s%s", basedir, filename);
	sysfsfp = fopen(temp, "w");
	if (sysfsfp == NULL)
		return -1;
	fprintf(sysfsfp, "%d", val);
	fclose(sysfsfp);
	return 0;
}

/**
 * write_sysfs_string_and_verify() - string write, readback and verify
 * @filename: name of file to write to
 * @basedir: the sysfs directory in which the file is to be found
 * @val: the string to write
 **/
int write_sysfs_string_and_verify(char *filename, char *basedir, char *val)
{
	int ret;
	FILE  *sysfsfp;
	char temp[100];
	sprintf(temp, "%s%s", basedir, filename);
	sysfsfp = fopen(temp, "w");
	if (sysfsfp == NULL)
		return -1;
	fprintf(sysfsfp, "%s", val);
	fclose(sysfsfp);

	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL)
		return -1;
	fscanf(sysfsfp, "%s", temp);
	if (strcmp(temp, val) != 0) {
		printf("Possible failure in string write %s to %s%s \n",
		       val,
		       basedir,
		       filename);
		return -1;
	}
	return 0;
}

int read_sysfs_posint(char *filename, char *basedir)
{
	int ret;
	FILE  *sysfsfp;
	char temp[100];
	sprintf(temp, "%s%s", basedir, filename);
	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL)
		return -1;
	fscanf(sysfsfp, "%d\n", &ret);
	fclose(sysfsfp);
	return ret;
}
