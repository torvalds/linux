/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SSL_H__
#define __SSL_H__

extern int ssl_read(int fd, int line);
extern void ssl_receive_char(int line, char ch);

#endif

