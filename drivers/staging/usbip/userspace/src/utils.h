
#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#define _GNU_SOURCE
#include <string.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <sysfs/libsysfs.h>
#include <glib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>



/* Be sync to kernel header */
#define BUS_ID_SIZE 20

int read_string(char *path, char *, size_t len);
int read_integer(char *path);
int getdevicename(char *busid, char *name, size_t len);
int getdriver(char *busid, int conf, int infnum, char *driver, size_t len);
int read_bNumInterfaces(char *busid);
int read_bConfigurationValue(char *busid);
int write_integer(char *path, int value);
int write_bConfigurationValue(char *busid, int config);
int read_bDeviceClass(char *busid);
int readline(int sockfd, char *str, int strlen);
int writeline(int sockfd, char *buff, int bufflen);
