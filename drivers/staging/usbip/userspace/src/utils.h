/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __UTILS_H
#define __UTILS_H

int modify_match_busid(char *busid, int add);

#endif /* __UTILS_H */
#include <sysfs/libsysfs.h>
#include <glib.h>
#include <unistd.h>
#include <stdio.h>
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
