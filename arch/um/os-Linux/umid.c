/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <init.h>
#include <os.h>

#define UML_DIR "~/.uml/"

#define UMID_LEN 64

/* Changed by set_umid, which is run early in boot */
static char umid[UMID_LEN] = { 0 };

/* Changed by set_uml_dir and make_uml_dir, which are run early in boot */
static char *uml_dir = UML_DIR;

static int __init make_uml_dir(void)
{
	char dir[512] = { '\0' };
	int len, err;

	if (*uml_dir == '~') {
		char *home = getenv("HOME");

		err = -ENOENT;
		if (home == NULL) {
			printk(UM_KERN_ERR
				"%s: no value in environment for $HOME\n",
				__func__);
			goto err;
		}
		strlcpy(dir, home, sizeof(dir));
		uml_dir++;
	}
	strlcat(dir, uml_dir, sizeof(dir));
	len = strlen(dir);
	if (len > 0 && dir[len - 1] != '/')
		strlcat(dir, "/", sizeof(dir));

	err = -ENOMEM;
	uml_dir = malloc(strlen(dir) + 1);
	if (uml_dir == NULL) {
		printk(UM_KERN_ERR "%s : malloc failed, errno = %d\n",
			__func__, errno);
		goto err;
	}
	strcpy(uml_dir, dir);

	if ((mkdir(uml_dir, 0777) < 0) && (errno != EEXIST)) {
		printk(UM_KERN_ERR "Failed to mkdir '%s': %s\n",
			uml_dir, strerror(errno));
		err = -errno;
		goto err_free;
	}
	return 0;

err_free:
	free(uml_dir);
err:
	uml_dir = NULL;
	return err;
}

/*
 * Unlinks the files contained in @dir and then removes @dir.
 * Doesn't handle directory trees, so it's not like rm -rf, but almost such. We
 * ignore ENOENT errors for anything (they happen, strangely enough - possibly
 * due to races between multiple dying UML threads).
 */
static int remove_files_and_dir(char *dir)
{
	DIR *directory;
	struct dirent *ent;
	int len;
	char file[256];
	int ret;

	directory = opendir(dir);
	if (directory == NULL) {
		if (errno != ENOENT)
			return -errno;
		else
			return 0;
	}

	while ((ent = readdir(directory)) != NULL) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;
		len = strlen(dir) + sizeof("/") + strlen(ent->d_name) + 1;
		if (len > sizeof(file)) {
			ret = -E2BIG;
			goto out;
		}

		sprintf(file, "%s/%s", dir, ent->d_name);
		if (unlink(file) < 0 && errno != ENOENT) {
			ret = -errno;
			goto out;
		}
	}

	if (rmdir(dir) < 0 && errno != ENOENT) {
		ret = -errno;
		goto out;
	}

	ret = 0;
out:
	closedir(directory);
	return ret;
}

/*
 * This says that there isn't already a user of the specified directory even if
 * there are errors during the checking.  This is because if these errors
 * happen, the directory is unusable by the pre-existing UML, so we might as
 * well take it over.  This could happen either by
 * 	the existing UML somehow corrupting its umid directory
 * 	something other than UML sticking stuff in the directory
 *	this boot racing with a shutdown of the other UML
 * In any of these cases, the directory isn't useful for anything else.
 *
 * Boolean return: 1 if in use, 0 otherwise.
 */
static inline int is_umdir_used(char *dir)
{
	char pid[sizeof("nnnnn\0")], *end, *file;
	int dead, fd, p, n, err;
	size_t filelen;

	err = asprintf(&file, "%s/pid", dir);
	if (err < 0)
		return 0;

	filelen = strlen(file);

	n = snprintf(file, filelen, "%s/pid", dir);
	if (n >= filelen) {
		printk(UM_KERN_ERR "is_umdir_used - pid filename too long\n");
		err = -E2BIG;
		goto out;
	}

	dead = 0;
	fd = open(file, O_RDONLY);
	if (fd < 0) {
		fd = -errno;
		if (fd != -ENOENT) {
			printk(UM_KERN_ERR "is_umdir_used : couldn't open pid "
			       "file '%s', err = %d\n", file, -fd);
		}
		goto out;
	}

	err = 0;
	n = read(fd, pid, sizeof(pid));
	if (n < 0) {
		printk(UM_KERN_ERR "is_umdir_used : couldn't read pid file "
		       "'%s', err = %d\n", file, errno);
		goto out_close;
	} else if (n == 0) {
		printk(UM_KERN_ERR "is_umdir_used : couldn't read pid file "
		       "'%s', 0-byte read\n", file);
		goto out_close;
	}

	p = strtoul(pid, &end, 0);
	if (end == pid) {
		printk(UM_KERN_ERR "is_umdir_used : couldn't parse pid file "
		       "'%s', errno = %d\n", file, errno);
		goto out_close;
	}

	if ((kill(p, 0) == 0) || (errno != ESRCH)) {
		printk(UM_KERN_ERR "umid \"%s\" is already in use by pid %d\n",
		       umid, p);
		return 1;
	}

out_close:
	close(fd);
out:
	free(file);
	return 0;
}

/*
 * Try to remove the directory @dir unless it's in use.
 * Precondition: @dir exists.
 * Returns 0 for success, < 0 for failure in removal or if the directory is in
 * use.
 */
static int umdir_take_if_dead(char *dir)
{
	int ret;
	if (is_umdir_used(dir))
		return -EEXIST;

	ret = remove_files_and_dir(dir);
	if (ret) {
		printk(UM_KERN_ERR "is_umdir_used - remove_files_and_dir "
		       "failed with err = %d\n", ret);
	}
	return ret;
}

static void __init create_pid_file(void)
{
	char pid[sizeof("nnnnn\0")], *file;
	int fd, n;

	file = malloc(strlen(uml_dir) + UMID_LEN + sizeof("/pid\0"));
	if (!file)
		return;

	if (umid_file_name("pid", file, sizeof(file)))
		goto out;

	fd = open(file, O_RDWR | O_CREAT | O_EXCL, 0644);
	if (fd < 0) {
		printk(UM_KERN_ERR "Open of machine pid file \"%s\" failed: "
		       "%s\n", file, strerror(errno));
		goto out;
	}

	snprintf(pid, sizeof(pid), "%d\n", getpid());
	n = write(fd, pid, strlen(pid));
	if (n != strlen(pid))
		printk(UM_KERN_ERR "Write of pid file failed - err = %d\n",
		       errno);

	close(fd);
out:
	free(file);
}

int __init set_umid(char *name)
{
	if (strlen(name) > UMID_LEN - 1)
		return -E2BIG;

	strlcpy(umid, name, sizeof(umid));

	return 0;
}

/* Changed in make_umid, which is called during early boot */
static int umid_setup = 0;

static int __init make_umid(void)
{
	int fd, err;
	char tmp[256];

	if (umid_setup)
		return 0;

	make_uml_dir();

	if (*umid == '\0') {
		strlcpy(tmp, uml_dir, sizeof(tmp));
		strlcat(tmp, "XXXXXX", sizeof(tmp));
		fd = mkstemp(tmp);
		if (fd < 0) {
			printk(UM_KERN_ERR "make_umid - mkstemp(%s) failed: "
			       "%s\n", tmp, strerror(errno));
			err = -errno;
			goto err;
		}

		close(fd);

		set_umid(&tmp[strlen(uml_dir)]);

		/*
		 * There's a nice tiny little race between this unlink and
		 * the mkdir below.  It'd be nice if there were a mkstemp
		 * for directories.
		 */
		if (unlink(tmp)) {
			err = -errno;
			goto err;
		}
	}

	snprintf(tmp, sizeof(tmp), "%s%s", uml_dir, umid);
	err = mkdir(tmp, 0777);
	if (err < 0) {
		err = -errno;
		if (err != -EEXIST)
			goto err;

		if (umdir_take_if_dead(tmp) < 0)
			goto err;

		err = mkdir(tmp, 0777);
	}
	if (err) {
		err = -errno;
		printk(UM_KERN_ERR "Failed to create '%s' - err = %d\n", umid,
		       errno);
		goto err;
	}

	umid_setup = 1;

	create_pid_file();

	err = 0;
 err:
	return err;
}

static int __init make_umid_init(void)
{
	if (!make_umid())
		return 0;

	/*
	 * If initializing with the given umid failed, then try again with
	 * a random one.
	 */
	printk(UM_KERN_ERR "Failed to initialize umid \"%s\", trying with a "
	       "random umid\n", umid);
	*umid = '\0';
	make_umid();

	return 0;
}

__initcall(make_umid_init);

int __init umid_file_name(char *name, char *buf, int len)
{
	int n, err;

	err = make_umid();
	if (err)
		return err;

	n = snprintf(buf, len, "%s%s/%s", uml_dir, umid, name);
	if (n >= len) {
		printk(UM_KERN_ERR "umid_file_name : buffer too short\n");
		return -E2BIG;
	}

	return 0;
}

char *get_umid(void)
{
	return umid;
}

static int __init set_uml_dir(char *name, int *add)
{
	if (*name == '\0') {
		os_warn("uml_dir can't be an empty string\n");
		return 0;
	}

	if (name[strlen(name) - 1] == '/') {
		uml_dir = name;
		return 0;
	}

	uml_dir = malloc(strlen(name) + 2);
	if (uml_dir == NULL) {
		os_warn("Failed to malloc uml_dir - error = %d\n", errno);

		/*
		 * Return 0 here because do_initcalls doesn't look at
		 * the return value.
		 */
		return 0;
	}
	sprintf(uml_dir, "%s/", name);

	return 0;
}

__uml_setup("uml_dir=", set_uml_dir,
"uml_dir=<directory>\n"
"    The location to place the pid and umid files.\n\n"
);

static void remove_umid_dir(void)
{
	char *dir, err;

	dir = malloc(strlen(uml_dir) + UMID_LEN + 1);
	if (!dir)
		return;

	sprintf(dir, "%s%s", uml_dir, umid);
	err = remove_files_and_dir(dir);
	if (err)
		os_warn("%s - remove_files_and_dir failed with err = %d\n",
			__func__, err);

	free(dir);
}

__uml_exitcall(remove_umid_dir);
