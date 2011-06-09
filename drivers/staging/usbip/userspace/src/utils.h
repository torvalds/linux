#ifndef __UTILS_H
#define __UTILS_H

#include <stdlib.h>

int modify_match_busid(char *busid, int add);
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

#endif /* __UTILS_H */
