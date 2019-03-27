/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2007 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2013 Oracle and/or its affiliates. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 * Implementation of the osm_db interface using simple text files
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_DB_FILES_C
#include <opensm/st.h>
#include <opensm/osm_db.h>
#include <opensm/osm_log.h>

/****d* Database/OSM_DB_MAX_LINE_LEN
 * NAME
 * OSM_DB_MAX_LINE_LEN
 *
 * DESCRIPTION
 * The Maximal line length allowed for the file
 *
 * SYNOPSIS
 */
#define OSM_DB_MAX_LINE_LEN 1024
/**********/

/****d* Database/OSM_DB_MAX_GUID_LEN
 * NAME
 * OSM_DB_MAX_GUID_LEN
 *
 * DESCRIPTION
 * The Maximal word length allowed for the file (guid or lid)
 *
 * SYNOPSIS
 */
#define OSM_DB_MAX_GUID_LEN 32
/**********/

/****s* OpenSM: Database/osm_db_domain_imp
 * NAME
 * osm_db_domain_imp
 *
 * DESCRIPTION
 * An implementation for domain of the database based on text files and
 *  hash tables.
 *
 * SYNOPSIS
 */
typedef struct osm_db_domain_imp {
	char *file_name;
	st_table *p_hash;
	cl_spinlock_t lock;
	boolean_t dirty;
} osm_db_domain_imp_t;
/*
 * FIELDS
 *
 * SEE ALSO
 * osm_db_domain_t
 *********/

/****s* OpenSM: Database/osm_db_imp_t
 * NAME
 * osm_db_imp_t
 *
 * DESCRIPTION
 * An implementation for file based database
 *
 * SYNOPSIS
 */
typedef struct osm_db_imp {
	const char *db_dir_name;
} osm_db_imp_t;
/*
 * FIELDS
 *
 * db_dir_name
 *   The directory holding the database
 *
 * SEE ALSO
 * osm_db_t
 *********/

void osm_db_construct(IN osm_db_t * p_db)
{
	memset(p_db, 0, sizeof(osm_db_t));
	cl_list_construct(&p_db->domains);
}

void osm_db_domain_destroy(IN osm_db_domain_t * p_db_domain)
{
	osm_db_domain_imp_t *p_domain_imp;
	p_domain_imp = (osm_db_domain_imp_t *) p_db_domain->p_domain_imp;

	osm_db_clear(p_db_domain);

	cl_spinlock_destroy(&p_domain_imp->lock);

	st_free_table(p_domain_imp->p_hash);
	free(p_domain_imp->file_name);
	free(p_domain_imp);
}

void osm_db_destroy(IN osm_db_t * p_db)
{
	osm_db_domain_t *p_domain;

	while ((p_domain = cl_list_remove_head(&p_db->domains)) != NULL) {
		osm_db_domain_destroy(p_domain);
		free(p_domain);
	}
	cl_list_destroy(&p_db->domains);
	free(p_db->p_db_imp);
}

int osm_db_init(IN osm_db_t * p_db, IN osm_log_t * p_log)
{
	osm_db_imp_t *p_db_imp;
	struct stat dstat;

	OSM_LOG_ENTER(p_log);

	p_db_imp = malloc(sizeof(osm_db_imp_t));
	if (!p_db_imp) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 6100: "
			"Failed to allocate db memory\n");
		return -1;
	}

	p_db_imp->db_dir_name = getenv("OSM_CACHE_DIR");
	if (!p_db_imp->db_dir_name || !(*p_db_imp->db_dir_name))
		p_db_imp->db_dir_name = OSM_DEFAULT_CACHE_DIR;

	/* Create the directory if it doesn't exist */
	/* There is a difference in creating directory between windows and linux */
#ifdef __WIN__
	{
		int ret;

		ret = SHCreateDirectoryEx(NULL, p_db_imp->db_dir_name, NULL);
		if (ret != ERROR_SUCCESS && ret != ERROR_ALREADY_EXISTS &&
			ret != ERROR_FILE_EXISTS)
			goto err;
	}
#else				/* __WIN__ */
	/* make sure the directory exists */
	if (lstat(p_db_imp->db_dir_name, &dstat)) {
		if (mkdir(p_db_imp->db_dir_name, 0755))
			goto err;
	}
#endif

	p_db->p_log = p_log;
	p_db->p_db_imp = (void *)p_db_imp;

	cl_list_init(&p_db->domains, 5);

	OSM_LOG_EXIT(p_log);

	return 0;

err:
	OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 6101: "
		"Failed to create the db directory:%s\n",
		p_db_imp->db_dir_name);
	free(p_db_imp);
	OSM_LOG_EXIT(p_log);
	return 1;
}

osm_db_domain_t *osm_db_domain_init(IN osm_db_t * p_db, IN const char *domain_name)
{
	osm_db_domain_t *p_domain;
	osm_db_domain_imp_t *p_domain_imp;
	size_t path_len;
	osm_log_t *p_log = p_db->p_log;
	FILE *p_file;

	OSM_LOG_ENTER(p_log);

	/* allocate a new domain object */
	p_domain = malloc(sizeof(osm_db_domain_t));
	if (p_domain == NULL) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 610C: "
			"Failed to allocate domain memory\n");
		goto Exit;
	}

	p_domain_imp = malloc(sizeof(osm_db_domain_imp_t));
	if (p_domain_imp == NULL) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 610D: "
			"Failed to allocate domain_imp memory\n");
		free(p_domain);
		p_domain = NULL;
		goto Exit;
	}

	path_len = strlen(((osm_db_imp_t *) p_db->p_db_imp)->db_dir_name)
	    + strlen(domain_name) + 2;

	/* set the domain file name */
	p_domain_imp->file_name = malloc(path_len);
	if (p_domain_imp->file_name == NULL) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 610E: "
			"Failed to allocate file_name memory\n");
		free(p_domain_imp);
		free(p_domain);
		p_domain = NULL;
		goto Exit;
	}
	snprintf(p_domain_imp->file_name, path_len, "%s/%s",
		 ((osm_db_imp_t *) p_db->p_db_imp)->db_dir_name, domain_name);

	/* make sure the file exists - or exit if not writable */
	p_file = fopen(p_domain_imp->file_name, "a+");
	if (!p_file) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 6102: "
			"Failed to open the db file:%s\n",
			p_domain_imp->file_name);
		free(p_domain_imp);
		free(p_domain);
		p_domain = NULL;
		goto Exit;
	}
	fclose(p_file);

	/* initialize the hash table object */
	p_domain_imp->p_hash = st_init_strtable();
	CL_ASSERT(p_domain_imp->p_hash != NULL);
	p_domain_imp->dirty = FALSE;

	p_domain->p_db = p_db;
	cl_list_insert_tail(&p_db->domains, p_domain);
	p_domain->p_domain_imp = p_domain_imp;
	cl_spinlock_construct(&p_domain_imp->lock);
	cl_spinlock_init(&p_domain_imp->lock);

Exit:
	OSM_LOG_EXIT(p_log);
	return p_domain;
}

int osm_db_restore(IN osm_db_domain_t * p_domain)
{

	osm_log_t *p_log = p_domain->p_db->p_log;
	osm_db_domain_imp_t *p_domain_imp =
	    (osm_db_domain_imp_t *) p_domain->p_domain_imp;
	FILE *p_file;
	int status;
	char sLine[OSM_DB_MAX_LINE_LEN];
	boolean_t before_key;
	char *p_first_word, *p_rest_of_line, *p_last;
	char *p_key = NULL;
	char *p_prev_val = NULL, *p_accum_val = NULL;
	char *endptr = NULL;
	unsigned int line_num;

	OSM_LOG_ENTER(p_log);

	/* take the lock on the domain */
	cl_spinlock_acquire(&p_domain_imp->lock);

	/* open the file - read mode */
	p_file = fopen(p_domain_imp->file_name, "r");

	if (!p_file) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 6103: "
			"Failed to open the db file:%s\n",
			p_domain_imp->file_name);
		status = 1;
		goto Exit;
	}

	/* parse the file allocating new hash tables as required */
	/*
	   states:
	   before_key (0) -> in_key (1)

	   before_key: if a word on the first byte - it is the key. state=in_key
	   the rest of the line is start of the value.
	   in_key: unless the line is empty - add it (with newlines) to the value.
	   if empty: state=before_key
	 */
	status = 0;
	before_key = TRUE;
	line_num = 0;
	/* if we got to EOF in the middle of a key we add a last newline */
	while ((fgets(sLine, OSM_DB_MAX_LINE_LEN, p_file) != NULL) ||
	       ((before_key == FALSE) && strcpy(sLine, "\n"))
	    ) {
		line_num++;
		if (before_key) {
			if ((sLine[0] != ' ') && (sLine[0] != '\t')
			    && (sLine[0] != '\n')) {
				/* we got a new key */
				before_key = FALSE;

				/* handle the key */
				p_first_word =
				    strtok_r(sLine, " \t\n", &p_last);
				if (!p_first_word) {
					OSM_LOG(p_log, OSM_LOG_ERROR,
						"ERR 6104: "
						"Failed to get key from line:%u : %s (file:%s)\n",
						line_num, sLine,
						p_domain_imp->file_name);
					status = 1;
					goto EndParsing;
				}
				if (strlen(p_first_word) > OSM_DB_MAX_GUID_LEN) {
					OSM_LOG(p_log, OSM_LOG_ERROR,
						"ERR 610A: "
						"Illegal key from line:%u : %s (file:%s)\n",
						line_num, sLine,
						p_domain_imp->file_name);
					status = 1;
					goto EndParsing;
				}

				p_key = malloc(sizeof(char) *
					       (strlen(p_first_word) + 1));
				strcpy(p_key, p_first_word);

				p_rest_of_line = strtok_r(NULL, "\n", &p_last);
				if (p_rest_of_line != NULL) {
					p_accum_val = malloc(sizeof(char) *
					    (strlen(p_rest_of_line) + 1));
					strcpy(p_accum_val, p_rest_of_line);
				} else {
					p_accum_val = malloc(2);
					strcpy(p_accum_val, "\0");
				}
			} else if (sLine[0] != '\n') {
				OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 6105: "
					"How did we get here? line:%u : %s (file:%s)\n",
					line_num, sLine,
					p_domain_imp->file_name);
				status = 1;
				goto EndParsing;
			}
		} /* before key */
		else {
			/* we already have a key */

			if (sLine[0] == '\n') {
				/* got an end of key */
				before_key = TRUE;

				/* make sure the key was not previously used */
				if (st_lookup(p_domain_imp->p_hash,
					      (st_data_t) p_key,
					      (void *)&p_prev_val)) {
					/* if previously used we ignore this guid */
					OSM_LOG(p_log, OSM_LOG_ERROR,
						"ERR 6106: "
						"Key:%s already exists in:%s with value:%s."
						" Removing it\n", p_key,
						p_domain_imp->file_name,
						p_prev_val);
						free(p_key);
						p_key = NULL;
						free(p_accum_val);
						p_accum_val = NULL;
						continue;
				} else {
					p_prev_val = NULL;
				}

				OSM_LOG(p_log, OSM_LOG_DEBUG,
					"Got key:%s value:%s\n", p_key,
					p_accum_val);

				/* check that the key is a number */
				if (!strtouq(p_key, &endptr, 0)
				    && *endptr != '\0') {
					OSM_LOG(p_log, OSM_LOG_ERROR,
						"ERR 610B: "
						"Key:%s is invalid\n", p_key);
						free(p_key);
						p_key = NULL;
						free(p_accum_val);
						p_accum_val = NULL;
				} else {
					/* store our key and value */
					st_insert(p_domain_imp->p_hash,
						  (st_data_t) p_key,
						  (st_data_t) p_accum_val);
				}
			} else {
				/* accumulate into the value */
				p_prev_val = p_accum_val;
				p_accum_val = malloc(strlen(p_prev_val) +
						     strlen(sLine) + 1);
				strcpy(p_accum_val, p_prev_val);
				free(p_prev_val);
				p_prev_val = NULL;
				strcat(p_accum_val, sLine);
			}
		}		/* in key */
	}			/* while lines or last line */

EndParsing:
	fclose(p_file);

Exit:
	cl_spinlock_release(&p_domain_imp->lock);
	OSM_LOG_EXIT(p_log);
	return status;
}

static int dump_tbl_entry(st_data_t key, st_data_t val, st_data_t arg)
{
	FILE *p_file = (FILE *) arg;
	char *p_key = (char *)key;
	char *p_val = (char *)val;

	fprintf(p_file, "%s %s\n\n", p_key, p_val);
	return ST_CONTINUE;
}

int osm_db_store(IN osm_db_domain_t * p_domain,
		 IN boolean_t fsync_high_avail_files)
{
	osm_log_t *p_log = p_domain->p_db->p_log;
	osm_db_domain_imp_t *p_domain_imp;
	FILE *p_file = NULL;
	int fd, status = 0;
	char *p_tmp_file_name = NULL;

	OSM_LOG_ENTER(p_log);

	p_domain_imp = (osm_db_domain_imp_t *) p_domain->p_domain_imp;

	p_tmp_file_name = malloc(sizeof(char) *
				 (strlen(p_domain_imp->file_name) + 8));
	if (!p_tmp_file_name) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 6113: "
			"Failed to allocate memory for temporary file name\n");
		goto Exit2;
	}
	strcpy(p_tmp_file_name, p_domain_imp->file_name);
	strcat(p_tmp_file_name, ".tmp");

	cl_spinlock_acquire(&p_domain_imp->lock);

	if (p_domain_imp->dirty == FALSE)
		goto Exit;

	/* open up the output file */
	p_file = fopen(p_tmp_file_name, "w");
	if (!p_file) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 6107: "
			"Failed to open the db file:%s for writing: err:%s\n",
			p_domain_imp->file_name, strerror(errno));
		status = 1;
		goto Exit;
	}

	st_foreach(p_domain_imp->p_hash, dump_tbl_entry, (st_data_t) p_file);

	if (fsync_high_avail_files) {
		if (fflush(p_file) == 0) {
			fd = fileno(p_file);
			if (fd != -1) {
				if (fsync(fd) == -1)
					OSM_LOG(p_log, OSM_LOG_ERROR,
						"ERR 6110: fsync() failed (%s) for %s\n",
						strerror(errno),
						p_domain_imp->file_name);
			} else
				OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 6111: "
					"fileno() failed for %s\n",
					p_domain_imp->file_name);
		} else
			OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 6112: "
				"fflush() failed (%s) for %s\n",
				strerror(errno), p_domain_imp->file_name);
	}

	fclose(p_file);

	status = rename(p_tmp_file_name, p_domain_imp->file_name);
	if (status) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 6108: "
			"Failed to rename the db file to:%s (err:%s)\n",
			p_domain_imp->file_name, strerror(errno));
		goto Exit;
	}
	p_domain_imp->dirty = FALSE;
Exit:
	cl_spinlock_release(&p_domain_imp->lock);
	free(p_tmp_file_name);
Exit2:
	OSM_LOG_EXIT(p_log);
	return status;
}

/* simply de-allocate the key and the value and return the code
   that makes the st_foreach delete the entry */
static int clear_tbl_entry(st_data_t key, st_data_t val, st_data_t arg)
{
	free((char *)key);
	free((char *)val);
	return ST_DELETE;
}

int osm_db_clear(IN osm_db_domain_t * p_domain)
{
	osm_db_domain_imp_t *p_domain_imp =
	    (osm_db_domain_imp_t *) p_domain->p_domain_imp;

	cl_spinlock_acquire(&p_domain_imp->lock);
	st_foreach(p_domain_imp->p_hash, clear_tbl_entry, (st_data_t) NULL);
	cl_spinlock_release(&p_domain_imp->lock);

	return 0;
}

static int get_key_of_tbl_entry(st_data_t key, st_data_t val, st_data_t arg)
{
	cl_list_t *p_list = (cl_list_t *) arg;
	cl_list_insert_tail(p_list, (void *)key);
	return ST_CONTINUE;
}

int osm_db_keys(IN osm_db_domain_t * p_domain, OUT cl_list_t * p_key_list)
{
	osm_db_domain_imp_t *p_domain_imp =
	    (osm_db_domain_imp_t *) p_domain->p_domain_imp;

	cl_spinlock_acquire(&p_domain_imp->lock);

	st_foreach(p_domain_imp->p_hash, get_key_of_tbl_entry,
		   (st_data_t) p_key_list);

	cl_spinlock_release(&p_domain_imp->lock);

	return 0;
}

char *osm_db_lookup(IN osm_db_domain_t * p_domain, IN char *p_key)
{
	osm_db_domain_imp_t *p_domain_imp =
	    (osm_db_domain_imp_t *) p_domain->p_domain_imp;
	char *p_val = NULL;

	cl_spinlock_acquire(&p_domain_imp->lock);

	if (!st_lookup(p_domain_imp->p_hash, (st_data_t) p_key, (void *)&p_val))
		p_val = NULL;

	cl_spinlock_release(&p_domain_imp->lock);

	return p_val;
}

int osm_db_update(IN osm_db_domain_t * p_domain, IN char *p_key, IN char *p_val)
{
	osm_log_t *p_log = p_domain->p_db->p_log;
	osm_db_domain_imp_t *p_domain_imp =
	    (osm_db_domain_imp_t *) p_domain->p_domain_imp;
	char *p_prev_val = NULL;
	char *p_new_key;
	char *p_new_val;

	cl_spinlock_acquire(&p_domain_imp->lock);

	if (st_lookup(p_domain_imp->p_hash,
		      (st_data_t) p_key, (void *)&p_prev_val)) {
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"Key:%s previously exists in:%s with value:%s\n",
			p_key, p_domain_imp->file_name, p_prev_val);
		p_new_key = p_key;
		/* same key, same value - nothing to update */
		if (p_prev_val && !strcmp(p_val, p_prev_val))
			goto Exit;
	} else {
		/* need to allocate the key */
		p_new_key = malloc(sizeof(char) * (strlen(p_key) + 1));
		strcpy(p_new_key, p_key);
	}

	/* need to arange a new copy of the  value */
	p_new_val = malloc(sizeof(char) * (strlen(p_val) + 1));
	strcpy(p_new_val, p_val);

	st_insert(p_domain_imp->p_hash, (st_data_t) p_new_key,
		  (st_data_t) p_new_val);

	if (p_prev_val)
		free(p_prev_val);

	p_domain_imp->dirty = TRUE;

Exit:
	cl_spinlock_release(&p_domain_imp->lock);

	return 0;
}

int osm_db_delete(IN osm_db_domain_t * p_domain, IN char *p_key)
{
	osm_log_t *p_log = p_domain->p_db->p_log;
	osm_db_domain_imp_t *p_domain_imp =
	    (osm_db_domain_imp_t *) p_domain->p_domain_imp;
	char *p_prev_val = NULL;
	int res;

	OSM_LOG_ENTER(p_log);

	cl_spinlock_acquire(&p_domain_imp->lock);
	if (st_delete(p_domain_imp->p_hash,
		      (void *)&p_key, (void *)&p_prev_val)) {
		if (st_lookup(p_domain_imp->p_hash,
			      (st_data_t) p_key, (void *)&p_prev_val)) {
			OSM_LOG(p_log, OSM_LOG_ERROR,
				"key:%s still exists in:%s with value:%s\n",
				p_key, p_domain_imp->file_name, p_prev_val);
			res = 1;
		} else {
			free(p_key);
			free(p_prev_val);
			p_domain_imp->dirty = TRUE;
			res = 0;
		}
	} else {
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"fail to find key:%s. delete failed\n", p_key);
		res = 1;
	}
	cl_spinlock_release(&p_domain_imp->lock);

	OSM_LOG_EXIT(p_log);
	return res;
}

#ifdef TEST_OSMDB
#include <stdlib.h>
#include <math.h>

int main(int argc, char **argv)
{
	osm_db_t db;
	osm_log_t log;
	osm_db_domain_t *p_dbd;
	cl_list_t keys;
	cl_list_iterator_t kI;
	char *p_key;
	char *p_val;
	int i;

	cl_list_construct(&keys);
	cl_list_init(&keys, 10);

	osm_log_init_v2(&log, TRUE, 0xff, "/var/log/osm_db_test.log", 0, FALSE);

	osm_db_construct(&db);
	if (osm_db_init(&db, &log)) {
		printf("db init failed\n");
		exit(1);
	}

	p_dbd = osm_db_domain_init(&db, "lid_by_guid");
	if (!p_dbd) {
		printf("db domain init failed\n");
		exit(1);
	}

	if (osm_db_restore(p_dbd)) {
		printf("failed to restore\n");
	}

	if (osm_db_keys(p_dbd, &keys)) {
		printf("failed to get keys\n");
	} else {
		kI = cl_list_head(&keys);
		while (kI != cl_list_end(&keys)) {
			p_key = cl_list_obj(kI);
			kI = cl_list_next(kI);

			p_val = osm_db_lookup(p_dbd, p_key);
			printf("key = %s val = %s\n", p_key, p_val);
		}
	}

	cl_list_remove_all(&keys);

	/* randomly add and remove numbers */
	for (i = 0; i < 10; i++) {
		int k;
		float v;
		int is_add;
		char val_buf[16];
		char key_buf[16];

		k = floor(1.0 * rand() / RAND_MAX * 100);
		v = rand();
		sprintf(key_buf, "%u", k);
		sprintf(val_buf, "%u", v);

		is_add = (rand() < RAND_MAX / 2);

		if (is_add) {
			osm_db_update(p_dbd, key_buf, val_buf);
		} else {
			osm_db_delete(p_dbd, key_buf);
		}
	}
	if (osm_db_keys(p_dbd, &keys)) {
		printf("failed to get keys\n");
	} else {
		kI = cl_list_head(&keys);
		while (kI != cl_list_end(&keys)) {
			p_key = cl_list_obj(kI);
			kI = cl_list_next(kI);

			p_val = osm_db_lookup(p_dbd, p_key);
			printf("key = %s val = %s\n", p_key, p_val);
		}
	}
	if (osm_db_store(p_dbd, FALSE))
		printf("failed to store\n");

	osm_db_destroy(&db);
	cl_list_destroy(&keys);
}
#endif
